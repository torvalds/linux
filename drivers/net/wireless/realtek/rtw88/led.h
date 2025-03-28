/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2025  Realtek Corporation
 */

#ifndef __RTW_LED_H
#define __RTW_LED_H

#ifdef CONFIG_RTW88_LEDS

void rtw_led_init(struct rtw_dev *rtwdev);
void rtw_led_deinit(struct rtw_dev *rtwdev);

#else

static inline void rtw_led_init(struct rtw_dev *rtwdev)
{
}

static inline void rtw_led_deinit(struct rtw_dev *rtwdev)
{
}

#endif

#endif
