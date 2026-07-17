/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2026 Realtek Corporation
 */

#ifndef __RTW89_LED_H__
#define __RTW89_LED_H__

#include "core.h"

void rtw89_led_gpio_config(struct rtw89_dev *rtwdev,
			   const struct rtw89_led_gpio_entry *e);
void rtw89_led_gpio_set(struct rtw89_dev *rtwdev,
			const struct rtw89_led_gpio_entry *e,
			enum led_brightness brightness);

#ifdef CONFIG_RTW89_LEDS
void rtw89_led_init(struct rtw89_dev *rtwdev);
void rtw89_led_deinit(struct rtw89_dev *rtwdev);
#else
static inline void rtw89_led_init(struct rtw89_dev *rtwdev) {}
static inline void rtw89_led_deinit(struct rtw89_dev *rtwdev) {}
#endif

#ifdef CONFIG_RTW89_LEDS_MC
int rtw89_led_mc_init(struct rtw89_dev *rtwdev, const struct rtw89_led_desc *desc);
void rtw89_led_mc_deinit(struct rtw89_dev *rtwdev);
#else
static inline int rtw89_led_mc_init(struct rtw89_dev *rtwdev,
				    const struct rtw89_led_desc *desc)
{
	return -EOPNOTSUPP;
}

static inline void rtw89_led_mc_deinit(struct rtw89_dev *rtwdev) {}
#endif

#endif
