/*
 *  Copyright (C) 2017 Chelsio Communications.  All rights reserved.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms and conditions of the GNU General Public License,
 *  version 2, as published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 *  more details.
 *
 *  The full GNU General Public License is included in this distribution in
 *  the file called "COPYING".
 *
 */

#ifndef __CUDBG_ENTITY_H__
#define __CUDBG_ENTITY_H__

#define EDC0_FLAG 3
#define EDC1_FLAG 4

struct card_mem {
	u16 size_edc0;
	u16 size_edc1;
	u16 mem_flag;
};

struct cudbg_mbox_log {
	struct mbox_cmd entry;
	u32 hi[MBOX_LEN / 8];
	u32 lo[MBOX_LEN / 8];
};

struct ireg_field {
	u32 ireg_addr;
	u32 ireg_data;
	u32 ireg_local_offset;
	u32 ireg_offset_range;
};

struct ireg_buf {
	struct ireg_field tp_pio;
	u32 outbuf[32];
};

#define IREG_NUM_ELEM 4

static const u32 t6_tp_pio_array[][IREG_NUM_ELEM] = {
	{0x7e40, 0x7e44, 0x020, 28}, /* t6_tp_pio_regs_20_to_3b */
	{0x7e40, 0x7e44, 0x040, 10}, /* t6_tp_pio_regs_40_to_49 */
	{0x7e40, 0x7e44, 0x050, 10}, /* t6_tp_pio_regs_50_to_59 */
	{0x7e40, 0x7e44, 0x060, 14}, /* t6_tp_pio_regs_60_to_6d */
	{0x7e40, 0x7e44, 0x06F, 1}, /* t6_tp_pio_regs_6f */
	{0x7e40, 0x7e44, 0x070, 6}, /* t6_tp_pio_regs_70_to_75 */
	{0x7e40, 0x7e44, 0x130, 18}, /* t6_tp_pio_regs_130_to_141 */
	{0x7e40, 0x7e44, 0x145, 19}, /* t6_tp_pio_regs_145_to_157 */
	{0x7e40, 0x7e44, 0x160, 1}, /* t6_tp_pio_regs_160 */
	{0x7e40, 0x7e44, 0x230, 25}, /* t6_tp_pio_regs_230_to_248 */
	{0x7e40, 0x7e44, 0x24a, 3}, /* t6_tp_pio_regs_24c */
	{0x7e40, 0x7e44, 0x8C0, 1} /* t6_tp_pio_regs_8c0 */
};

static const u32 t5_tp_pio_array[][IREG_NUM_ELEM] = {
	{0x7e40, 0x7e44, 0x020, 28}, /* t5_tp_pio_regs_20_to_3b */
	{0x7e40, 0x7e44, 0x040, 19}, /* t5_tp_pio_regs_40_to_52 */
	{0x7e40, 0x7e44, 0x054, 2}, /* t5_tp_pio_regs_54_to_55 */
	{0x7e40, 0x7e44, 0x060, 13}, /* t5_tp_pio_regs_60_to_6c */
	{0x7e40, 0x7e44, 0x06F, 1}, /* t5_tp_pio_regs_6f */
	{0x7e40, 0x7e44, 0x120, 4}, /* t5_tp_pio_regs_120_to_123 */
	{0x7e40, 0x7e44, 0x12b, 2}, /* t5_tp_pio_regs_12b_to_12c */
	{0x7e40, 0x7e44, 0x12f, 21}, /* t5_tp_pio_regs_12f_to_143 */
	{0x7e40, 0x7e44, 0x145, 19}, /* t5_tp_pio_regs_145_to_157 */
	{0x7e40, 0x7e44, 0x230, 25}, /* t5_tp_pio_regs_230_to_248 */
	{0x7e40, 0x7e44, 0x8C0, 1} /* t5_tp_pio_regs_8c0 */
};

static const u32 t6_tp_tm_pio_array[][IREG_NUM_ELEM] = {
	{0x7e18, 0x7e1c, 0x0, 12}
};

static const u32 t5_tp_tm_pio_array[][IREG_NUM_ELEM] = {
	{0x7e18, 0x7e1c, 0x0, 12}
};

static const u32 t6_tp_mib_index_array[6][IREG_NUM_ELEM] = {
	{0x7e50, 0x7e54, 0x0, 13},
	{0x7e50, 0x7e54, 0x10, 6},
	{0x7e50, 0x7e54, 0x18, 21},
	{0x7e50, 0x7e54, 0x30, 32},
	{0x7e50, 0x7e54, 0x50, 22},
	{0x7e50, 0x7e54, 0x68, 12}
};

static const u32 t5_tp_mib_index_array[9][IREG_NUM_ELEM] = {
	{0x7e50, 0x7e54, 0x0, 13},
	{0x7e50, 0x7e54, 0x10, 6},
	{0x7e50, 0x7e54, 0x18, 8},
	{0x7e50, 0x7e54, 0x20, 13},
	{0x7e50, 0x7e54, 0x30, 16},
	{0x7e50, 0x7e54, 0x40, 16},
	{0x7e50, 0x7e54, 0x50, 16},
	{0x7e50, 0x7e54, 0x60, 6},
	{0x7e50, 0x7e54, 0x68, 4}
};
#endif /* __CUDBG_ENTITY_H__ */
