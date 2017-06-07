/*
 * Copyright (c) 2011 Wind River Systems, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * Author:  Anders Wallin <anders.wallin@windriver.com>
 *
 */

#ifndef __STA2X11_VIP_H
#define __STA2X11_VIP_H

/**
 * struct vip_config - video input configuration data
 * @pwr_name: ADV powerdown name
 * @pwr_pin: ADV powerdown pin
 * @reset_name: ADV reset name
 * @reset_pin: ADV reset pin
 */
struct vip_config {
	const char *pwr_name;
	int pwr_pin;
	const char *reset_name;
	int reset_pin;
	int i2c_id;
	int i2c_addr;
};

#endif /* __STA2X11_VIP_H */
