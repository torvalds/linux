/*
 * This file is part of wl12xx
 *
 * Copyright (C) 2008 Nokia Corporation
 *
 * Contact: Kalle Valo <kalle.valo@nokia.com>
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

#include "wl1251.h"
#include "reg.h"
#include "wl1251_io.h"

static int wl1251_translate_reg_addr(struct wl1251 *wl, int addr)
{
	/* If the address is lower than REGISTERS_BASE, it means that this is
	 * a chip-specific register address, so look it up in the registers
	 * table */
	if (addr < REGISTERS_BASE) {
		/* Make sure we don't go over the table */
		if (addr >= ACX_REG_TABLE_LEN) {
			wl1251_error("address out of range (%d)", addr);
			return -EINVAL;
		}
		addr = wl->chip.acx_reg_table[addr];
	}

	return addr - wl->physical_reg_addr + wl->virtual_reg_addr;
}

static int wl1251_translate_mem_addr(struct wl1251 *wl, int addr)
{
	return addr - wl->physical_mem_addr + wl->virtual_mem_addr;
}

void wl1251_mem_read(struct wl1251 *wl, int addr, void *buf, size_t len)
{
	int physical;

	physical = wl1251_translate_mem_addr(wl, addr);

	wl1251_spi_read(wl, physical, buf, len);
}

void wl1251_mem_write(struct wl1251 *wl, int addr, void *buf, size_t len)
{
	int physical;

	physical = wl1251_translate_mem_addr(wl, addr);

	wl1251_spi_write(wl, physical, buf, len);
}

u32 wl1251_mem_read32(struct wl1251 *wl, int addr)
{
	return wl1251_read32(wl, wl1251_translate_mem_addr(wl, addr));
}

void wl1251_mem_write32(struct wl1251 *wl, int addr, u32 val)
{
	wl1251_write32(wl, wl1251_translate_mem_addr(wl, addr), val);
}

u32 wl1251_reg_read32(struct wl1251 *wl, int addr)
{
	return wl1251_read32(wl, wl1251_translate_reg_addr(wl, addr));
}

void wl1251_reg_write32(struct wl1251 *wl, int addr, u32 val)
{
	wl1251_write32(wl, wl1251_translate_reg_addr(wl, addr), val);
}
