/*
 * This file is part of wl18xx
 *
 * Copyright (C) 2011 Texas Instruments Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#ifndef __WL18XX_PRIV_H__
#define __WL18XX_PRIV_H__


#define WL18XX_CMD_MAX_SIZE          740

struct wl18xx_priv {
	/* buffer for sending commands to FW */
	u8 cmd_buf[WL18XX_CMD_MAX_SIZE];
};

#endif /* __WL18XX_PRIV_H__ */
