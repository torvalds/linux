/*
 * This file is part of wl1271
 *
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

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/crc7.h>
#include <linux/spi/spi.h>

#include "wl1271.h"
#include "wl12xx_80211.h"
#include "wl1271_spi.h"
#include "wl1271_io.h"

static int wl1271_translate_addr(struct wl1271 *wl, int addr)
{
	/*
	 * To translate, first check to which window of addresses the
	 * particular address belongs. Then subtract the starting address
	 * of that window from the address. Then, add offset of the
	 * translated region.
	 *
	 * The translated regions occur next to each other in physical device
	 * memory, so just add the sizes of the preceeding address regions to
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

/* Set the SPI partitions to access the chip addresses
 *
 * To simplify driver code, a fixed (virtual) memory map is defined for
 * register and memory addresses. Because in the chipset, in different stages
 * of operation, those addresses will move around, an address translation
 * mechanism is required.
 *
 * There are four partitions (three memory and one register partition),
 * which are mapped to two different areas of the hardware memory.
 *
 *                                Virtual address
 *                                     space
 *
 *                                    |    |
 *                                 ...+----+--> mem.start
 *          Physical address    ...   |    |
 *               space       ...      |    | [PART_0]
 *                        ...         |    |
 *  00000000  <--+----+...         ...+----+--> mem.start + mem.size
 *               |    |         ...   |    |
 *               |MEM |      ...      |    |
 *               |    |   ...         |    |
 *  mem.size  <--+----+...            |    | {unused area)
 *               |    |   ...         |    |
 *               |REG |      ...      |    |
 *  mem.size     |    |         ...   |    |
 *      +     <--+----+...         ...+----+--> reg.start
 *  reg.size     |    |   ...         |    |
 *               |MEM2|      ...      |    | [PART_1]
 *               |    |         ...   |    |
 *                                 ...+----+--> reg.start + reg.size
 *                                    |    |
 *
 */
int wl1271_set_partition(struct wl1271 *wl,
			 struct wl1271_partition_set *p)
{
	/* copy partition info */
	memcpy(&wl->part, p, sizeof(*p));

	wl1271_debug(DEBUG_SPI, "mem_start %08X mem_size %08X",
		     p->mem.start, p->mem.size);
	wl1271_debug(DEBUG_SPI, "reg_start %08X reg_size %08X",
		     p->reg.start, p->reg.size);
	wl1271_debug(DEBUG_SPI, "mem2_start %08X mem2_size %08X",
		     p->mem2.start, p->mem2.size);
	wl1271_debug(DEBUG_SPI, "mem3_start %08X mem3_size %08X",
		     p->mem3.start, p->mem3.size);

	/* write partition info to the chipset */
	wl1271_raw_write32(wl, HW_PART0_START_ADDR, p->mem.start);
	wl1271_raw_write32(wl, HW_PART0_SIZE_ADDR, p->mem.size);
	wl1271_raw_write32(wl, HW_PART1_START_ADDR, p->reg.start);
	wl1271_raw_write32(wl, HW_PART1_SIZE_ADDR, p->reg.size);
	wl1271_raw_write32(wl, HW_PART2_START_ADDR, p->mem2.start);
	wl1271_raw_write32(wl, HW_PART2_SIZE_ADDR, p->mem2.size);
	wl1271_raw_write32(wl, HW_PART3_START_ADDR, p->mem3.start);

	return 0;
}

void wl1271_io_reset(struct wl1271 *wl)
{
	wl1271_spi_reset(wl);
}

void wl1271_io_init(struct wl1271 *wl)
{
	wl1271_spi_init(wl);
}

void wl1271_raw_write(struct wl1271 *wl, int addr, void *buf,
		      size_t len, bool fixed)
{
	wl1271_spi_raw_write(wl, addr, buf, len, fixed);
}

void wl1271_raw_read(struct wl1271 *wl, int addr, void *buf,
		     size_t len, bool fixed)
{
	wl1271_spi_raw_read(wl, addr, buf, len, fixed);
}

void wl1271_read(struct wl1271 *wl, int addr, void *buf, size_t len,
		     bool fixed)
{
	int physical;

	physical = wl1271_translate_addr(wl, addr);

	wl1271_spi_raw_read(wl, physical, buf, len, fixed);
}

void wl1271_write(struct wl1271 *wl, int addr, void *buf, size_t len,
		  bool fixed)
{
	int physical;

	physical = wl1271_translate_addr(wl, addr);

	wl1271_spi_raw_write(wl, physical, buf, len, fixed);
}

u32 wl1271_read32(struct wl1271 *wl, int addr)
{
	return wl1271_raw_read32(wl, wl1271_translate_addr(wl, addr));
}

void wl1271_write32(struct wl1271 *wl, int addr, u32 val)
{
	wl1271_raw_write32(wl, wl1271_translate_addr(wl, addr), val);
}

void wl1271_top_reg_write(struct wl1271 *wl, int addr, u16 val)
{
	/* write address >> 1 + 0x30000 to OCP_POR_CTR */
	addr = (addr >> 1) + 0x30000;
	wl1271_write32(wl, OCP_POR_CTR, addr);

	/* write value to OCP_POR_WDATA */
	wl1271_write32(wl, OCP_DATA_WRITE, val);

	/* write 1 to OCP_CMD */
	wl1271_write32(wl, OCP_CMD, OCP_CMD_WRITE);
}

u16 wl1271_top_reg_read(struct wl1271 *wl, int addr)
{
	u32 val;
	int timeout = OCP_CMD_LOOP;

	/* write address >> 1 + 0x30000 to OCP_POR_CTR */
	addr = (addr >> 1) + 0x30000;
	wl1271_write32(wl, OCP_POR_CTR, addr);

	/* write 2 to OCP_CMD */
	wl1271_write32(wl, OCP_CMD, OCP_CMD_READ);

	/* poll for data ready */
	do {
		val = wl1271_read32(wl, OCP_DATA_READ);
	} while (!(val & OCP_READY_MASK) && --timeout);

	if (!timeout) {
		wl1271_warning("Top register access timed out.");
		return 0xffff;
	}

	/* check data status and return if OK */
	if ((val & OCP_STATUS_MASK) == OCP_STATUS_OK)
		return val & 0xffff;
	else {
		wl1271_warning("Top register access returned error.");
		return 0xffff;
	}
}

