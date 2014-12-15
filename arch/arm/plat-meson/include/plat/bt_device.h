/*
 * arch/arm/plat-meson/include/plat/bt_device.h
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

#ifndef __PLAT_MESON_BT_DEVICE_H
#define __PLAT_MESON_BT_DEVICE_H

struct bt_dev_data {
	int gpio_reset;
	int gpio_en;
	int gpio_host_wake;
	int gpio_wake;
};

#endif
