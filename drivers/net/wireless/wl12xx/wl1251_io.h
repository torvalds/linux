/*
 * This file is part of wl12xx
 *
 * Copyright (C) 2008 Nokia Corporation
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
#ifndef __WL1251_IO_H__
#define __WL1251_IO_H__

#include "wl1251.h"
#include "wl1251_spi.h"

/* Raw target IO, address is not translated */
void wl1251_spi_read(struct wl1251 *wl, int addr, void *buf, size_t len);
void wl1251_spi_write(struct wl1251 *wl, int addr, void *buf, size_t len);

static inline u32 wl1251_read32(struct wl1251 *wl, int addr)
{
	u32 response;

	wl1251_spi_read(wl, addr, &response, sizeof(u32));

	return response;
}

static inline void wl1251_write32(struct wl1251 *wl, int addr, u32 val)
{
	wl1251_spi_write(wl, addr, &val, sizeof(u32));
}

/* Memory target IO, address is translated to partition 0 */
void wl1251_mem_read(struct wl1251 *wl, int addr, void *buf, size_t len);
void wl1251_mem_write(struct wl1251 *wl, int addr, void *buf, size_t len);
u32 wl1251_mem_read32(struct wl1251 *wl, int addr);
void wl1251_mem_write32(struct wl1251 *wl, int addr, u32 val);
/* Registers IO */
u32 wl1251_reg_read32(struct wl1251 *wl, int addr);
void wl1251_reg_write32(struct wl1251 *wl, int addr, u32 val);

#endif
