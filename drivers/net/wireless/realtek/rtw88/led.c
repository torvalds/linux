// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2025  Realtek Corporation
 */

#include "main.h"
#include "debug.h"
#include "led.h"

static int rtw_led_set_blocking(struct led_classdev *led,
				enum led_brightness brightness)
{
	struct rtw_dev *rtwdev = container_of(led, struct rtw_dev, led_cdev);

	rtwdev->chip->ops->led_set(led, brightness);

	return 0;
}

void rtw_led_init(struct rtw_dev *rtwdev)
{
	static const struct ieee80211_tpt_blink rtw_tpt_blink[] = {
		{ .throughput = 0 * 1024, .blink_time = 334 },
		{ .throughput = 1 * 1024, .blink_time = 260 },
		{ .throughput = 5 * 1024, .blink_time = 220 },
		{ .throughput = 10 * 1024, .blink_time = 190 },
		{ .throughput = 20 * 1024, .blink_time = 170 },
		{ .throughput = 50 * 1024, .blink_time = 150 },
		{ .throughput = 70 * 1024, .blink_time = 130 },
		{ .throughput = 100 * 1024, .blink_time = 110 },
		{ .throughput = 200 * 1024, .blink_time = 80 },
		{ .throughput = 300 * 1024, .blink_time = 50 },
	};
	struct led_classdev *led = &rtwdev->led_cdev;
	int err;

	if (!rtwdev->chip->ops->led_set)
		return;

	if (rtw_hci_type(rtwdev) == RTW_HCI_TYPE_PCIE)
		led->brightness_set = rtwdev->chip->ops->led_set;
	else
		led->brightness_set_blocking = rtw_led_set_blocking;

	snprintf(rtwdev->led_name, sizeof(rtwdev->led_name),
		 "rtw88-%s", dev_name(rtwdev->dev));

	led->name = rtwdev->led_name;
	led->max_brightness = LED_ON;
	led->default_trigger =
		ieee80211_create_tpt_led_trigger(rtwdev->hw,
						 IEEE80211_TPT_LEDTRIG_FL_RADIO,
						 rtw_tpt_blink,
						 ARRAY_SIZE(rtw_tpt_blink));

	err = led_classdev_register(rtwdev->dev, led);
	if (err) {
		rtw_warn(rtwdev, "Failed to register the LED, error %d\n", err);
		return;
	}

	rtwdev->led_registered = true;
}

void rtw_led_deinit(struct rtw_dev *rtwdev)
{
	struct led_classdev *led = &rtwdev->led_cdev;

	if (!rtwdev->led_registered)
		return;

	rtwdev->chip->ops->led_set(led, LED_OFF);
	led_classdev_unregister(led);
}
