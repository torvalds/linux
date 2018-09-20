/*
 *  Dell AIO Serial Backlight Driver
 *
 *  Copyright (C) 2017 AceLan Kao <acelan.kao@canonical.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 */

#ifndef _DELL_UART_BACKLIGHT_H_
#define _DELL_UART_BACKLIGHT_H_

enum {
	DELL_UART_GET_FIRMWARE_VER,
	DELL_UART_GET_SCALAR,
	DELL_UART_GET_BRIGHTNESS,
	DELL_UART_SET_BRIGHTNESS,
	DELL_UART_SET_BACKLIGHT_POWER,
};

struct dell_uart_bl_cmd {
	unsigned char	cmd[10];
	unsigned char	ret[80];
	unsigned short	tx_len;
	unsigned short	rx_len;
};

#endif /* _DELL_UART_BACKLIGHT_H_ */
