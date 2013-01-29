/*
 * arch/arm/include/asm/hardware/cache-l2x0.h
 *
 * Copyright (C) 2007 ARM Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#ifndef __ASM_ARM_HARDWARE_L2X0_H
#define __ASM_ARM_HARDWARE_L2X0_H

#include <linux/errno.h>

#define L2X0_CACHE_ID			0x000
#define L2X0_CACHE_TYPE			0x004
#define L2X0_CTRL			0x100
#define L2X0_AUX_CTRL			0x104
#define L2X0_TAG_LATENCY_CTRL		0x108
#define L2X0_DATA_LATENCY_CTRL		0x10C
#define L2X0_EVENT_CNT_CTRL		0x200
#define L2X0_EVENT_CNT1_CFG		0x204
#define L2X0_EVENT_CNT0_CFG		0x208
#define L2X0_EVENT_CNT1_VAL		0x20C
#define L2X0_EVENT_CNT0_VAL		0x210
#define L2X0_INTR_MASK			0x214
#define L2X0_MASKED_INTR_STAT		0x218
#define L2X0_RAW_INTR_STAT		0x21C
#define L2X0_INTR_CLEAR			0x220
#define L2X0_CACHE_SYNC			0x730
#define L2X0_DUMMY_REG			0x740
#define L2X0_INV_LINE_PA		0x770
#define L2X0_INV_WAY			0x77C
#define L2X0_CLEAN_LINE_PA		0x7B0
#define L2X0_CLEAN_LINE_IDX		0x7B8
#define L2X0_CLEAN_WAY			0x7BC
#define L2X0_CLEAN_INV_LINE_PA		0x7F0
#define L2X0_CLEAN_INV_LINE_IDX		0x7F8
#define L2X0_CLEAN_INV_WAY		0x7FC
/*
 * The lockdown registers repeat 8 times for L310, the L210 has only one
 * D and one I lockdown register at 0x0900 and 0x0904.
 */
#define L2X0_LOCKDOWN_WAY_D_BASE	0x900
#define L2X0_LOCKDOWN_WAY_I_BASE	0x904
#define L2X0_LOCKDOWN_STRIDE		0x08
#define L2X0_ADDR_FILTER_START		0xC00
#define L2X0_ADDR_FILTER_END		0xC04
#define L2X0_TEST_OPERATION		0xF00
#define L2X0_LINE_DATA			0xF10
#define L2X0_LINE_TAG			0xF30
#define L2X0_DEBUG_CTRL			0xF40
#define L2X0_PREFETCH_CTRL		0xF60
#define L2X0_POWER_CTRL			0xF80
#define   L2X0_DYNAMIC_CLK_GATING_EN	(1 << 1)
#define   L2X0_STNDBY_MODE_EN		(1 << 0)

/* Registers shifts and masks */
#define L2X0_CACHE_ID_PART_MASK		(0xf << 6)
#define L2X0_CACHE_ID_PART_L210		(1 << 6)
#define L2X0_CACHE_ID_PART_L310		(3 << 6)
#define L2X0_CACHE_ID_RTL_MASK          0x3f
#define L2X0_CACHE_ID_RTL_R0P0          0x0
#define L2X0_CACHE_ID_RTL_R1P0          0x2
#define L2X0_CACHE_ID_RTL_R2P0          0x4
#define L2X0_CACHE_ID_RTL_R3P0          0x5
#define L2X0_CACHE_ID_RTL_R3P1          0x6
#define L2X0_CACHE_ID_RTL_R3P2          0x8

#define L2X0_AUX_CTRL_MASK			0xc0000fff
#define L2X0_AUX_CTRL_DATA_RD_LATENCY_SHIFT	0
#define L2X0_AUX_CTRL_DATA_RD_LATENCY_MASK	0x7
#define L2X0_AUX_CTRL_DATA_WR_LATENCY_SHIFT	3
#define L2X0_AUX_CTRL_DATA_WR_LATENCY_MASK	(0x7 << 3)
#define L2X0_AUX_CTRL_TAG_LATENCY_SHIFT		6
#define L2X0_AUX_CTRL_TAG_LATENCY_MASK		(0x7 << 6)
#define L2X0_AUX_CTRL_DIRTY_LATENCY_SHIFT	9
#define L2X0_AUX_CTRL_DIRTY_LATENCY_MASK	(0x7 << 9)
#define L2X0_AUX_CTRL_ASSOCIATIVITY_SHIFT	16
#define L2X0_AUX_CTRL_WAY_SIZE_SHIFT		17
#define L2X0_AUX_CTRL_WAY_SIZE_MASK		(0x7 << 17)
#define L2X0_AUX_CTRL_SHARE_OVERRIDE_SHIFT	22
#define L2X0_AUX_CTRL_NS_LOCKDOWN_SHIFT		26
#define L2X0_AUX_CTRL_NS_INT_CTRL_SHIFT		27
#define L2X0_AUX_CTRL_DATA_PREFETCH_SHIFT	28
#define L2X0_AUX_CTRL_INSTR_PREFETCH_SHIFT	29
#define L2X0_AUX_CTRL_EARLY_BRESP_SHIFT		30

#define L2X0_LATENCY_CTRL_SETUP_SHIFT	0
#define L2X0_LATENCY_CTRL_RD_SHIFT	4
#define L2X0_LATENCY_CTRL_WR_SHIFT	8

#define L2X0_ADDR_FILTER_EN		1

#define L2X0_CTRL_EN			1

#define L2X0_WAY_SIZE_SHIFT		3

#ifndef __ASSEMBLY__
extern void __init l2x0_init(void __iomem *base, u32 aux_val, u32 aux_mask);
#if defined(CONFIG_CACHE_L2X0) && defined(CONFIG_OF)
extern int l2x0_of_init(u32 aux_val, u32 aux_mask);
#else
static inline int l2x0_of_init(u32 aux_val, u32 aux_mask)
{
	return -ENODEV;
}
#endif

struct l2x0_regs {
	unsigned long phy_base;
	unsigned long aux_ctrl;
	/*
	 * Whether the following registers need to be saved/restored
	 * depends on platform
	 */
	unsigned long tag_latency;
	unsigned long data_latency;
	unsigned long filter_start;
	unsigned long filter_end;
	unsigned long prefetch_ctrl;
	unsigned long pwr_ctrl;
	unsigned long ctrl;
};

extern struct l2x0_regs l2x0_saved_regs;

#endif /* __ASSEMBLY__ */

#endif
