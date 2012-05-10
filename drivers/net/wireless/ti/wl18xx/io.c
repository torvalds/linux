/*
 * This file is part of wl18xx
 *
 * Copyright (C) 2011 Texas Instruments
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

#include "../wlcore/wlcore.h"
#include "../wlcore/io.h"

#include "io.h"

void wl18xx_top_reg_write(struct wl1271 *wl, int addr, u16 val)
{
	u32 tmp;

	if (WARN_ON(addr % 2))
		return;

	if ((addr % 4) == 0) {
		tmp = wl1271_read32(wl, addr);
		tmp = (tmp & 0xffff0000) | val;
		wl1271_write32(wl, addr, tmp);
	} else {
		tmp = wl1271_read32(wl, addr - 2);
		tmp = (tmp & 0xffff) | (val << 16);
		wl1271_write32(wl, addr - 2, tmp);
	}
}

u16 wl18xx_top_reg_read(struct wl1271 *wl, int addr)
{
	u32 val;

	if (WARN_ON(addr % 2))
		return 0;

	if ((addr % 4) == 0) {
		/* address is 4-bytes aligned */
		val = wl1271_read32(wl, addr);
		return val & 0xffff;
	} else {
		val = wl1271_read32(wl, addr - 2);
		return (val & 0xffff0000) >> 16;
	}
}
