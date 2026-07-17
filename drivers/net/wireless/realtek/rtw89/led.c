// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2026 Realtek Corporation
 */

#include "core.h"
#include "debug.h"
#include "led.h"
#include "reg.h"

static const struct rtw89_led_gpio_entry rtw89_common_led_gpios[] = {{
	.pin = 8,
	.pinmux = {.addr = R_AX_GPIO8_15_FUNC_SEL, .mask = GENMASK(3, 0), .data = 0xf},
	.mode = {.addr = R_AX_GPIO_EXT_CTRL + 2, .data = BIT(0) | BIT(8)},
	.out = {.addr = R_AX_GPIO_EXT_CTRL + 1, .data = BIT(0)},
}};

static const struct rtw89_led_desc rtw89_common_led_desc = {
	.gpios = rtw89_common_led_gpios,
	.n_gpio = ARRAY_SIZE(rtw89_common_led_gpios),
};

void rtw89_led_gpio_config(struct rtw89_dev *rtwdev,
			   const struct rtw89_led_gpio_entry *e)
{
	rtw89_write32_mask(rtwdev, e->pinmux.addr, e->pinmux.mask, e->pinmux.data);
	rtw89_write16_clr(rtwdev, e->mode.addr, e->mode.data);
}

void rtw89_led_gpio_set(struct rtw89_dev *rtwdev,
			const struct rtw89_led_gpio_entry *e,
			enum led_brightness brightness)
{
	if (brightness == LED_OFF) {
		rtw89_write16_clr(rtwdev, e->mode.addr, e->mode.data);
	} else {
		rtw89_write16_set(rtwdev, e->mode.addr, e->mode.data);
		rtw89_write8_clr(rtwdev, e->out.addr, e->out.data);
	}
}

static int rtw89_led_set(struct led_classdev *led, enum led_brightness brightness)
{
	struct rtw89_led *rtw_led = container_of(led, struct rtw89_led, led);
	struct rtw89_dev *rtwdev = container_of(rtw_led, struct rtw89_dev, led);
	const struct rtw89_led_desc *desc = rtw_led->desc;
	const struct rtw89_led_gpio_entry *e = &desc->gpios[0];

	if (rtw_led->brightness_cache[0] == brightness) {
		rtw89_debug(rtwdev, RTW89_DBG_LED, "led_set: pin=%u skip (no change)\n",
			    e->pin);
		return 0;
	}

	wiphy_lock(rtwdev->hw->wiphy);

	rtw89_debug(rtwdev, RTW89_DBG_LED, "led_set: pin=%u brightness=%u\n",
		    e->pin, brightness);
	rtw89_led_gpio_set(rtwdev, e, brightness);
	rtw_led->brightness_cache[0] = brightness;

	wiphy_unlock(rtwdev->hw->wiphy);

	return 0;
}

static int rtw89_led_sc_init(struct rtw89_dev *rtwdev, const struct rtw89_led_desc *desc)
{
	struct rtw89_led *rtw_led = &rtwdev->led;
	int ret;

	snprintf(rtw_led->name, sizeof(rtw_led->name), "rtw89-%s",
		 wiphy_name(rtwdev->hw->wiphy));
	rtw_led->led.name = rtw_led->name;
	rtw_led->led.brightness_set_blocking = rtw89_led_set;
	rtw_led->led.max_brightness = LED_ON;
	rtw_led->led.default_trigger = ieee80211_get_assoc_led_name(rtwdev->hw);

	ret = led_classdev_register(rtwdev->dev, &rtw_led->led);
	if (ret) {
		rtw89_warn(rtwdev, "failed to register LED, ret=%d\n", ret);
		return ret;
	}

	rtw89_led_gpio_config(rtwdev, &desc->gpios[0]);

	return 0;
}

static void rtw89_led_sc_deinit(struct rtw89_dev *rtwdev)
{
	struct rtw89_led *rtw_led = &rtwdev->led;

	led_classdev_unregister(&rtw_led->led);
}

void rtw89_led_init(struct rtw89_dev *rtwdev)
{
	const struct rtw89_led_desc *desc = &rtw89_common_led_desc;
	struct rtw89_led *rtw_led = &rtwdev->led;
	const struct rtw89_board_variant *board = rtwdev->board;
	int ret;
	int i;

	/* single-GPIO monochrome LED is the only supported layout */
	BUILD_BUG_ON(ARRAY_SIZE(rtw89_common_led_gpios) != 1);

	if (board)
		desc = board->led_desc;
	if (!desc->n_gpio || desc->n_gpio > RTW89_LED_MAX_NUM)
		return;

	rtw_led->desc = desc;
	for (i = 0; i < ARRAY_SIZE(rtw_led->brightness_cache); i++)
		rtw_led->brightness_cache[i] = LED_OFF;

	if (desc->n_gpio == 1)
		ret = rtw89_led_sc_init(rtwdev, desc);
	else
		ret = rtw89_led_mc_init(rtwdev, desc);

	if (ret)
		return;

	rtw_led->registered = true;
}

void rtw89_led_deinit(struct rtw89_dev *rtwdev)
{
	struct rtw89_led *rtw_led = &rtwdev->led;
	const struct rtw89_led_desc *desc = rtw_led->desc;

	if (!rtw_led->registered)
		return;

	if (desc->n_gpio == 1)
		rtw89_led_sc_deinit(rtwdev);
	else
		rtw89_led_mc_deinit(rtwdev);
}
