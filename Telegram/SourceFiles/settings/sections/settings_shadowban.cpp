/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/sections/settings_shadowban.h"

#include "settings/settings_common_session.h"

#include "lang/lang_keys.h"
#include "lottie/lottie_icon.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "settings/sections/settings_main.h"
#include "settings/settings_builder.h"
#include "settings/settings_common.h"
#include "settings/settings_privacy_controllers.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/vertical_list.h"
#include "window/window_session_controller.h"
#include "styles/style_settings.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"

namespace Settings {
namespace {

using namespace Builder;

void BuildShadowbanSection(SectionBuilder &builder) {
	builder.add(nullptr, [] {
		return SearchEntry{
			.id = u"shadowban/add-user"_q,
			.title = tr::lng_shadowban_list_add(tr::now),
			.keywords = { u"shadowban"_q, u"hide"_q, u"ban"_q, u"add"_q },
		};
	});
}

class Shadowban final : public Section<Shadowban> {
public:
	Shadowban(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	void showFinished() override;

	[[nodiscard]] rpl::producer<QString> title() override;

	[[nodiscard]] base::weak_qptr<Ui::RpWidget> createPinnedToTop(
		not_null<QWidget*> parent) override;

private:
	void setupContent();
	void checkTotal(int total);

	void visibleTopBottomUpdated(int visibleTop, int visibleBottom) override;

	const not_null<Ui::VerticalLayout*> _container;

	base::unique_qptr<Ui::RpWidget> _loading;

	rpl::variable<int> _countShadowBanned;

	rpl::event_stream<> _showFinished;
	rpl::event_stream<bool> _emptinessChanges;

	QPointer<Ui::RpWidget> _addUserButton;

};

Shadowban::Shadowban(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller)
, _container(Ui::CreateChild<Ui::VerticalLayout>(this)) {
	setupContent();

	{
		auto padding = st::changePhoneIconPadding;
		padding.setBottom(padding.top());
		_loading = base::make_unique_q<Ui::PaddingWrap<>>(
			this,
			object_ptr<Ui::FlatLabel>(
				this,
				tr::lng_contacts_loading(),
				st::changePhoneDescription),
			std::move(padding));
		Ui::ResizeFitChild(
			this,
			_loading.get(),
			st::settingsBlockedHeightMin);
	}

	checkTotal(controller->session().settings().shadowBannedCount());
}

rpl::producer<QString> Shadowban::title() {
	return tr::lng_settings_shadowban();
}

base::weak_qptr<Ui::RpWidget> Shadowban::createPinnedToTop(
		not_null<QWidget*> parent) {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(parent.get());

	Ui::AddSkip(content);

	const auto addButton = AddButtonWithIcon(
		content,
		tr::lng_shadowban_list_add(),
		st::settingsButtonActive,
		{ &st::menuIconBlockSettings });
	_addUserButton = addButton;
	addButton->addClickHandler([=] {
		ShadowbanListController::AddNewPeer(controller());
	});

	Ui::AddSkip(content);
	Ui::AddDividerText(content, tr::lng_shadowban_list_about());

	{
		const auto subtitle = content->add(
			object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
				content,
				object_ptr<Ui::VerticalLayout>(content)))->setDuration(0);
		Ui::AddSkip(subtitle->entity());
		auto subtitleText = _countShadowBanned.value(
		) | rpl::map([=](int count) {
			return tr::lng_shadowban_list_subtitle(tr::now, lt_count, count);
		});
		Ui::AddSubsectionTitle(
			subtitle->entity(),
			rpl::duplicate(subtitleText),
			st::settingsBlockedListSubtitleAddPadding);
		subtitle->toggleOn(
			rpl::merge(
				_emptinessChanges.events() | rpl::map(!rpl::mappers::_1),
				_countShadowBanned.value() | rpl::map(rpl::mappers::_1 > 0)
			) | rpl::distinct_until_changed());

		std::move(
			subtitleText
		) | rpl::on_next([=] {
			subtitle->entity()->resizeToWidth(content->width());
		}, subtitle->lifetime());
	}

	return base::make_weak(not_null<Ui::RpWidget*>{ content });
}

void Shadowban::setupContent() {
	using namespace rpl::mappers;

	const auto listWrap = _container->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			_container,
			object_ptr<Ui::VerticalLayout>(_container)));
	listWrap->toggleOn(
		_emptinessChanges.events_starting_with(true) | rpl::map(!_1),
		anim::type::instant);

	{
		struct State {
			std::unique_ptr<ShadowbanListController> controller;
			std::unique_ptr<PeerListContentDelegateSimple> delegate;
		};

		auto controller = std::make_unique<ShadowbanListController>(
			this->controller());
		controller->setStyleOverrides(&st::settingsBlockedList);
		const auto content = listWrap->entity()->add(
			object_ptr<PeerListContent>(this, controller.get()));

		const auto state = content->lifetime().make_state<State>();
		state->controller = std::move(controller);
		state->delegate = std::make_unique<PeerListContentDelegateSimple>();

		state->delegate->setContent(content);
		state->controller->setDelegate(state->delegate.get());

		state->controller->rowsCountChanges(
		) | rpl::on_next([=](int total) {
			_countShadowBanned = total;
			checkTotal(total);
		}, content->lifetime());
		_countShadowBanned = content->fullRowsCount();
	}

	const auto emptyWrap = _container->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			_container,
			object_ptr<Ui::VerticalLayout>(_container)));
	emptyWrap->toggleOn(
		_emptinessChanges.events_starting_with(false),
		anim::type::instant);

	{
		const auto content = emptyWrap->entity();
		auto icon = CreateLottieIcon(
			content,
			{
				.name = u"blocked_peers_empty"_q,
				.sizeOverride = st::normalBoxLottieSize,
			},
			st::settingsBlockedListIconPadding);
		content->add(std::move(icon.widget));

		_showFinished.events(
		) | rpl::on_next([animate = std::move(icon.animate)] {
			animate(anim::repeat::once);
		}, content->lifetime());

		content->add(
			object_ptr<Ui::FlatLabel>(
				content,
				tr::lng_shadowban_list_empty_title(),
				st::changePhoneTitle),
			st::changePhoneTitlePadding,
			style::al_top);

		content->add(
			object_ptr<Ui::FlatLabel>(
				content,
				tr::lng_shadowban_list_empty_description(),
				st::changePhoneDescription),
			st::changePhoneDescriptionPadding,
			style::al_top);

		Ui::AddSkip(content, st::settingsBlockedListIconPadding.top());
	}

	widthValue(
	) | rpl::on_next([=](int width) {
		_container->resizeToWidth(width);
	}, _container->lifetime());

	rpl::combine(
		_container->heightValue(),
		_emptinessChanges.events_starting_with(true)
	) | rpl::on_next([=](int height, bool empty) {
		const auto subtitled = !empty || (_countShadowBanned.current() > 0);
		const auto total = st::settingsBlockedHeightMin;
		const auto padding = st::defaultSubsectionTitlePadding
			+ st::settingsBlockedListSubtitleAddPadding;
		const auto subtitle = st::defaultVerticalListSkip
			+ padding.top()
			+ st::defaultSubsectionTitle.style.font->height
			+ padding.bottom();
		const auto min = total - (subtitled ? subtitle : 0);
		resize(width(), std::max(height, min));
	}, _container->lifetime());

	const SectionBuildMethod buildMethod = [](
			not_null<Ui::VerticalLayout*> container,
			not_null<Window::SessionController*> controller,
			Fn<void(Type)> showOther,
			rpl::producer<> showFinished) {
		auto &lifetime = container->lifetime();
		const auto highlights = lifetime.make_state<HighlightRegistry>();

		auto builder = SectionBuilder(WidgetContext{
			.container = container,
			.controller = controller,
			.showOther = std::move(showOther),
			.isPaused = Window::PausedIn(
				controller,
				Window::GifPauseReason::Layer),
			.highlights = highlights,
		});

		BuildShadowbanSection(builder);

		std::move(showFinished) | rpl::on_next([=] {
			for (const auto &[id, entry] : *highlights) {
				if (entry.widget) {
					controller->checkHighlightControl(
						id,
						entry.widget,
						base::duplicate(entry.args));
				}
			}
		}, lifetime);
	};

	build(_container, buildMethod);
}

void Shadowban::checkTotal(int total) {
	_loading = nullptr;
	_emptinessChanges.fire(total <= 0);
}

void Shadowban::visibleTopBottomUpdated(int visibleTop, int visibleBottom) {
	setChildVisibleTopBottom(_container, visibleTop, visibleBottom);
}

void Shadowban::showFinished() {
	Section::showFinished();
	_showFinished.fire({});
	controller()->checkHighlightControl(
		u"shadowban/add-user"_q,
		_addUserButton);
}

const auto kMeta = BuildHelper({
	.id = Shadowban::Id(),
	.parentId = MainId(),
	.title = &tr::lng_settings_shadowban,
	.icon = &st::menuIconBlock,
}, [](SectionBuilder &builder) {
	BuildShadowbanSection(builder);
});

} // namespace

Type ShadowbanId() {
	return Shadowban::Id();
}

} // namespace Settings
