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

#ifndef __IO_H__
#define __IO_H__

#include <linux/irqreturn.h>
#include "reg.h"

#define HW_ACCESS_MEMORY_MAX_RANGE	0x1FFC0

#define HW_PARTITION_REGISTERS_ADDR     0x1FFC0
#define HW_PART0_SIZE_ADDR              (HW_PARTITION_REGISTERS_ADDR)
#define HW_PART0_START_ADDR             (HW_PARTITION_REGISTERS_ADDR + 4)
#define HW_PART1_SIZE_ADDR              (HW_PARTITION_REGISTERS_ADDR + 8)
#define HW_PART1_START_ADDR             (HW_PARTITION_REGISTERS_ADDR + 12)
#define HW_PART2_SIZE_ADDR              (HW_PARTITION_REGISTERS_ADDR + 16)
#define HW_PART2_START_ADDR             (HW_PARTITION_REGISTERS_ADDR + 20)
#define HW_PART3_START_ADDR             (HW_PARTITION_REGISTERS_ADDR + 24)

#define HW_ACCESS_REGISTER_SIZE         4

#define HW_ACCESS_PRAM_MAX_RANGE	0x3c000

struct wl1271;

void wl1271_disable_interrupts(struct wl1271 *wl);
void wl1271_enable_interrupts(struct wl1271 *wl);

void wl1271_io_reset(struct wl1271 *wl);
void wl1271_io_init(struct wl1271 *wl);

static inline struct device *wl1271_wl_to_dev(struct wl1271 *wl)
{
	return wl->if_ops->dev(wl);
}


/* Raw target IO, address is not translated */
static inline void wl1271_raw_write(struct wl1271 *wl, int addr, void *buf,
				    size_t len, bool fixed)
{
	wl->if_ops->write(wl, addr, buf, len, fixed);
}

static inline void wl1271_raw_read(struct wl1271 *wl, int addr, void *buf,
				   size_t len, bool fixed)
{
	wl->if_ops->read(wl, addr, buf, len, fixed);
}

static inline u32 wl1271_raw_read32(struct wl1271 *wl, int addr)
{
	wl1271_raw_read(wl, addr, &wl->buffer_32,
			    sizeof(wl->buffer_32), false);

	return le32_to_cpu(wl->buffer_32);
}

static inline void wl1271_raw_write32(struct wl1271 *wl, int addr, u32 val)
{
	wl->buffer_32 = cpu_to_le32(val);
	wl1271_raw_write(wl, addr, &wl->buffer_32,
			     sizeof(wl->buffer_32), false);
}

/* Translated target IO */
static inline int wl1271_translate_addr(struct wl1271 *wl, int addr)
{
	/*
	 * To translate, first check to which window of addresses the
	 * particular address belongs. Then subtract the starting address
	 * of that window from the address. Then, add offset of the
	 * translated region.
	 *
	 * The translated regions occur next to each other in physical device
	 * memory, so just add the sizes of the preceding address regions to
	 * get the offset to the new region.
	 *
	 * Currently, only the two first regions are addressed, and the
	 * assumption is that all addresses will fall into either of those
	 * two.
	 */
	if ((addr >= wl->part.reg.start) &&
	    (addr < wl->part.reg.start + wl->part.reg.size))
		return addr - wl->part.reg.start + wl->part.mem.size;
	else
		return addr - wl->part.mem.start;
}

static inline void wl1271_read(struct wl1271 *wl, int addr, void *buf,
			       size_t len, bool fixed)
{
	int physical;

	physical = wl1271_translate_addr(wl, addr);

	wl1271_raw_read(wl, physical, buf, len, fixed);
}

static inline void wl1271_write(struct wl1271 *wl, int addr, void *buf,
				size_t len, bool fixed)
{
	int physical;

	physical = wl1271_translate_addr(wl, addr);

	wl1271_raw_write(wl, physical, buf, len, fixed);
}

static inline void wl1271_read_hwaddr(struct wl1271 *wl, int hwaddr,
				      void *buf, size_t len, bool fixed)
{
	int physical;
	int addr;

	/* Addresses are stored internally as addresses to 32 bytes blocks */
	addr = hwaddr << 5;

	physical = wl1271_translate_addr(wl, addr);

	wl1271_raw_read(wl, physical, buf, len, fixed);
}

static inline u32 wl1271_read32(struct wl1271 *wl, int addr)
{
	return wl1271_raw_read32(wl, wl1271_translate_addr(wl, addr));
}

static inline void wl1271_write32(struct wl1271 *wl, int addr, u32 val)
{
	wl1271_raw_write32(wl, wl1271_translate_addr(wl, addr), val);
}

static inline void wl1271_power_off(struct wl1271 *wl)
{
	wl->if_ops->power(wl, false);
	clear_bit(WL1271_FLAG_GPIO_POWER, &wl->flags);
}

static inline int wl1271_power_on(struct wl1271 *wl)
{
	int ret = wl->if_ops->power(wl, true);
	if (ret == 0)
		set_bit(WL1271_FLAG_GPIO_POWER, &wl->flags);

	return ret;
}


/* Top Register IO */
void wl1271_top_reg_write(struct wl1271 *wl, int addr, u16 val);
u16 wl1271_top_reg_read(struct wl1271 *wl, int addr);

int wl1271_set_partition(struct wl1271 *wl,
			 struct wl1271_partition_set *p);

/* Functions from wl1271_main.c */

int wl1271_register_hw(struct wl1271 *wl);
void wl1271_unregister_hw(struct wl1271 *wl);
int wl1271_init_ieee80211(struct wl1271 *wl);
struct ieee80211_hw *wl1271_alloc_hw(void);
int wl1271_free_hw(struct wl1271 *wl);
irqreturn_t wl1271_irq(int irq, void *data);
bool wl1271_set_block_size(struct wl1271 *wl);
int wl1271_tx_dummy_packet(struct wl1271 *wl);
void wl1271_configure_filters(struct wl1271 *wl, unsigned int filters);

#endif
