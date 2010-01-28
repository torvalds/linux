/*
 * Definitions for RTL8187 leds
 *
 * Copyright 2009 Larry Finger <Larry.Finger@lwfinger.net>
 *
 * Based on the LED handling in the r8187 driver, which is:
 * Copyright (c) Realtek Semiconductor Corp. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef RTL8187_LED_H
#define RTL8187_LED_H

#ifdef CONFIG_RTL8187_LEDS

#define RTL8187_LED_MAX_NAME_LEN	21

#include <linux/leds.h>
#include <linux/types.h>

enum {
	LED_PIN_LED0,
	LED_PIN_LED1,
	LED_PIN_GPIO0,
	LED_PIN_HW
};

enum {
	EEPROM_CID_RSVD0 = 0x00,
	EEPROM_CID_RSVD1 = 0xFF,
	EEPROM_CID_ALPHA0 = 0x01,
	EEPROM_CID_SERCOMM_PS = 0x02,
	EEPROM_CID_HW = 0x03,
	EEPROM_CID_TOSHIBA = 0x04,
	EEPROM_CID_QMI = 0x07,
	EEPROM_CID_DELL = 0x08
};

struct rtl8187_led {
	struct ieee80211_hw *dev;
	/* The LED class device */
	struct led_classdev led_dev;
	/* The pin/method used to control the led */
	u8 ledpin;
	/* The unique name string for this LED device. */
	char name[RTL8187_LED_MAX_NAME_LEN + 1];
	/* If the LED is radio or tx/rx */
	bool is_radio;
};

void rtl8187_leds_init(struct ieee80211_hw *dev, u16 code);
void rtl8187_leds_exit(struct ieee80211_hw *dev);

#endif /* def CONFIG_RTL8187_LED */

#endif /* RTL8187_LED_H */
