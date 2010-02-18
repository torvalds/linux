/*
 * This file is part of wl1271
 *
 * Copyright (C) 1998-2009 Texas Instruments. All rights reserved.
 * Copyright (C) 2008-2010 Nokia Corporation
 *
 * Contact: Luciano Coelho <luciano.coelho@nokia.com>
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

#ifndef __WL1271_IO_H__
#define __WL1271_IO_H__

struct wl1271;

void wl1271_io_reset(struct wl1271 *wl);
void wl1271_io_init(struct wl1271 *wl);

/* Raw target IO, address is not translated */
void wl1271_raw_write(struct wl1271 *wl, int addr, void *buf,
		      size_t len, bool fixed);
void wl1271_raw_read(struct wl1271 *wl, int addr, void *buf,
		     size_t len, bool fixed);

/* Translated target IO */
void wl1271_read(struct wl1271 *wl, int addr, void *buf, size_t len,
		     bool fixed);
void wl1271_write(struct wl1271 *wl, int addr, void *buf, size_t len,
		      bool fixed);
u32 wl1271_read32(struct wl1271 *wl, int addr);
void wl1271_write32(struct wl1271 *wl, int addr, u32 val);

/* Top Register IO */
void wl1271_top_reg_write(struct wl1271 *wl, int addr, u16 val);
u16 wl1271_top_reg_read(struct wl1271 *wl, int addr);

int wl1271_set_partition(struct wl1271 *wl,
			 struct wl1271_partition_set *p);

static inline u32 wl1271_raw_read32(struct wl1271 *wl, int addr)
{
	wl1271_raw_read(wl, addr, &wl->buffer_32,
			    sizeof(wl->buffer_32), false);

	return wl->buffer_32;
}

static inline void wl1271_raw_write32(struct wl1271 *wl, int addr, u32 val)
{
	wl->buffer_32 = val;
	wl1271_raw_write(wl, addr, &wl->buffer_32,
			     sizeof(wl->buffer_32), false);
}
#endif
