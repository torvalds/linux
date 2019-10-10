// SPDX-License-Identifier: GPL-2.0-only
/*
 * This file is part of wl12xx
 *
 * Copyright (C) 2008 Nokia Corporation
 */

#include "wl1251.h"
#include "reg.h"
#include "io.h"

/* FIXME: this is static data nowadays and the table can be removed */
static enum wl12xx_acx_int_reg wl1251_io_reg_table[ACX_REG_TABLE_LEN] = {
	[ACX_REG_INTERRUPT_TRIG]     = (REGISTERS_BASE + 0x0474),
	[ACX_REG_INTERRUPT_TRIG_H]   = (REGISTERS_BASE + 0x0478),
	[ACX_REG_INTERRUPT_MASK]     = (REGISTERS_BASE + 0x0494),
	[ACX_REG_HINT_MASK_SET]      = (REGISTERS_BASE + 0x0498),
	[ACX_REG_HINT_MASK_CLR]      = (REGISTERS_BASE + 0x049C),
	[ACX_REG_INTERRUPT_NO_CLEAR] = (REGISTERS_BASE + 0x04B0),
	[ACX_REG_INTERRUPT_CLEAR]    = (REGISTERS_BASE + 0x04A4),
	[ACX_REG_INTERRUPT_ACK]      = (REGISTERS_BASE + 0x04A8),
	[ACX_REG_SLV_SOFT_RESET]     = (REGISTERS_BASE + 0x0000),
	[ACX_REG_EE_START]           = (REGISTERS_BASE + 0x080C),
	[ACX_REG_ECPU_CONTROL]       = (REGISTERS_BASE + 0x0804)
};

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
		addr = wl1251_io_reg_table[addr];
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

	wl->if_ops->read(wl, physical, buf, len);
}

void wl1251_mem_write(struct wl1251 *wl, int addr, void *buf, size_t len)
{
	int physical;

	physical = wl1251_translate_mem_addr(wl, addr);

	wl->if_ops->write(wl, physical, buf, len);
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

/* Set the partitions to access the chip addresses.
 *
 * There are two VIRTUAL partitions (the memory partition and the
 * registers partition), which are mapped to two different areas of the
 * PHYSICAL (hardware) memory.  This function also makes other checks to
 * ensure that the partitions are not overlapping.  In the diagram below, the
 * memory partition comes before the register partition, but the opposite is
 * also supported.
 *
 *                               PHYSICAL address
 *                                     space
 *
 *                                    |    |
 *                                 ...+----+--> mem_start
 *          VIRTUAL address     ...   |    |
 *               space       ...      |    | [PART_0]
 *                        ...         |    |
 * 0x00000000 <--+----+...         ...+----+--> mem_start + mem_size
 *               |    |         ...   |    |
 *               |MEM |      ...      |    |
 *               |    |   ...         |    |
 *  part_size <--+----+...            |    | {unused area)
 *               |    |   ...         |    |
 *               |REG |      ...      |    |
 *  part_size    |    |         ...   |    |
 *      +     <--+----+...         ...+----+--> reg_start
 *  reg_size              ...         |    |
 *                           ...      |    | [PART_1]
 *                              ...   |    |
 *                                 ...+----+--> reg_start + reg_size
 *                                    |    |
 *
 */
void wl1251_set_partition(struct wl1251 *wl,
			  u32 mem_start, u32 mem_size,
			  u32 reg_start, u32 reg_size)
{
	struct wl1251_partition partition[2];

	wl1251_debug(DEBUG_SPI, "mem_start %08X mem_size %08X",
		     mem_start, mem_size);
	wl1251_debug(DEBUG_SPI, "reg_start %08X reg_size %08X",
		     reg_start, reg_size);

	/* Make sure that the two partitions together don't exceed the
	 * address range */
	if ((mem_size + reg_size) > HW_ACCESS_MEMORY_MAX_RANGE) {
		wl1251_debug(DEBUG_SPI, "Total size exceeds maximum virtual"
			     " address range.  Truncating partition[0].");
		mem_size = HW_ACCESS_MEMORY_MAX_RANGE - reg_size;
		wl1251_debug(DEBUG_SPI, "mem_start %08X mem_size %08X",
			     mem_start, mem_size);
		wl1251_debug(DEBUG_SPI, "reg_start %08X reg_size %08X",
			     reg_start, reg_size);
	}

	if ((mem_start < reg_start) &&
	    ((mem_start + mem_size) > reg_start)) {
		/* Guarantee that the memory partition doesn't overlap the
		 * registers partition */
		wl1251_debug(DEBUG_SPI, "End of partition[0] is "
			     "overlapping partition[1].  Adjusted.");
		mem_size = reg_start - mem_start;
		wl1251_debug(DEBUG_SPI, "mem_start %08X mem_size %08X",
			     mem_start, mem_size);
		wl1251_debug(DEBUG_SPI, "reg_start %08X reg_size %08X",
			     reg_start, reg_size);
	} else if ((reg_start < mem_start) &&
		   ((reg_start + reg_size) > mem_start)) {
		/* Guarantee that the register partition doesn't overlap the
		 * memory partition */
		wl1251_debug(DEBUG_SPI, "End of partition[1] is"
			     " overlapping partition[0].  Adjusted.");
		reg_size = mem_start - reg_start;
		wl1251_debug(DEBUG_SPI, "mem_start %08X mem_size %08X",
			     mem_start, mem_size);
		wl1251_debug(DEBUG_SPI, "reg_start %08X reg_size %08X",
			     reg_start, reg_size);
	}

	partition[0].start = mem_start;
	partition[0].size  = mem_size;
	partition[1].start = reg_start;
	partition[1].size  = reg_size;

	wl->physical_mem_addr = mem_start;
	wl->physical_reg_addr = reg_start;

	wl->virtual_mem_addr = 0;
	wl->virtual_reg_addr = mem_size;

	wl->if_ops->write(wl, HW_ACCESS_PART0_SIZE_ADDR, partition,
		sizeof(partition));
}
