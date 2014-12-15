/*
 * arch/arm/plat-meson/include/plat/wifi_power.h
 *
 * Copyright (C) 2010-2014 Amlogic, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __PLAT_MESON_WIFI_POWER_H
#define __PLAT_MESON_WIFI_POWER_H

struct wifi_power_platform_data {
	int power_gpio;
	int power_gpio2;
	int (*set_power)(int val);
	int (*set_reset)(int val);
	int (*set_carddetect)(int val);
	void *(*mem_prealloc)(int section, unsigned long size);
	int (*get_mac_addr)(unsigned char *buf);
	void *(*get_country_code)(char *ccode);
	void (*usb_set_power)(int val);
};

#endif /*__PLAT_MESON_WIFI_POWER_H*/
