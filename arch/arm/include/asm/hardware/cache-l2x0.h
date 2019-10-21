/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * arch/arm/include/asm/hardware/cache-l2x0.h
 *
 * Copyright (C) 2007 ARM Limited
 */

#ifndef __ASM_ARM_HARDWARE_L2X0_H
#define __ASM_ARM_HARDWARE_L2X0_H

#include <linux/errno.h>

#define L2X0_CACHE_ID			0x000
#define L2X0_CACHE_TYPE			0x004
#define L2X0_CTRL			0x100
#define L2X0_AUX_CTRL			0x104
#define L310_TAG_LATENCY_CTRL		0x108
#define L310_DATA_LATENCY_CTRL		0x10C
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
#define L310_ADDR_FILTER_START		0xC00
#define L310_ADDR_FILTER_END		0xC04
#define L2X0_TEST_OPERATION		0xF00
#define L2X0_LINE_DATA			0xF10
#define L2X0_LINE_TAG			0xF30
#define L2X0_DEBUG_CTRL			0xF40
#define L310_PREFETCH_CTRL		0xF60
#define L310_POWER_CTRL			0xF80
#define   L310_DYNAMIC_CLK_GATING_EN	(1 << 1)
#define   L310_STNDBY_MODE_EN		(1 << 0)

/* Registers shifts and masks */
#define L2X0_CACHE_ID_PART_MASK		(0xf << 6)
#define L2X0_CACHE_ID_PART_L210		(1 << 6)
#define L2X0_CACHE_ID_PART_L220		(2 << 6)
#define L2X0_CACHE_ID_PART_L310		(3 << 6)
#define L2X0_CACHE_ID_RTL_MASK          0x3f
#define L210_CACHE_ID_RTL_R0P2_02	0x00
#define L210_CACHE_ID_RTL_R0P1		0x01
#define L210_CACHE_ID_RTL_R0P2_01	0x02
#define L210_CACHE_ID_RTL_R0P3		0x03
#define L210_CACHE_ID_RTL_R0P4		0x0b
#define L210_CACHE_ID_RTL_R0P5		0x0f
#define L220_CACHE_ID_RTL_R1P7_01REL0	0x06
#define L310_CACHE_ID_RTL_R0P0		0x00
#define L310_CACHE_ID_RTL_R1P0		0x02
#define L310_CACHE_ID_RTL_R2P0		0x04
#define L310_CACHE_ID_RTL_R3P0		0x05
#define L310_CACHE_ID_RTL_R3P1		0x06
#define L310_CACHE_ID_RTL_R3P1_50REL0	0x07
#define L310_CACHE_ID_RTL_R3P2		0x08
#define L310_CACHE_ID_RTL_R3P3		0x09

#define L2X0_EVENT_CNT_CTRL_ENABLE	BIT(0)

#define L2X0_EVENT_CNT_CFG_SRC_SHIFT	2
#define L2X0_EVENT_CNT_CFG_SRC_MASK	0xf
#define L2X0_EVENT_CNT_CFG_SRC_DISABLED	0
#define L2X0_EVENT_CNT_CFG_INT_DISABLED	0
#define L2X0_EVENT_CNT_CFG_INT_INCR	1
#define L2X0_EVENT_CNT_CFG_INT_OVERFLOW	2

/* L2C auxiliary control register - bits common to L2C-210/220/310 */
#define L2C_AUX_CTRL_WAY_SIZE_SHIFT		17
#define L2C_AUX_CTRL_WAY_SIZE_MASK		(7 << 17)
#define L2C_AUX_CTRL_WAY_SIZE(n)		((n) << 17)
#define L2C_AUX_CTRL_EVTMON_ENABLE		BIT(20)
#define L2C_AUX_CTRL_PARITY_ENABLE		BIT(21)
#define L2C_AUX_CTRL_SHARED_OVERRIDE		BIT(22)
/* L2C-210/220 common bits */
#define L2X0_AUX_CTRL_DATA_RD_LATENCY_SHIFT	0
#define L2X0_AUX_CTRL_DATA_RD_LATENCY_MASK	(7 << 0)
#define L2X0_AUX_CTRL_DATA_WR_LATENCY_SHIFT	3
#define L2X0_AUX_CTRL_DATA_WR_LATENCY_MASK	(7 << 3)
#define L2X0_AUX_CTRL_TAG_LATENCY_SHIFT		6
#define L2X0_AUX_CTRL_TAG_LATENCY_MASK		(7 << 6)
#define L2X0_AUX_CTRL_DIRTY_LATENCY_SHIFT	9
#define L2X0_AUX_CTRL_DIRTY_LATENCY_MASK	(7 << 9)
#define L2X0_AUX_CTRL_ASSOC_SHIFT		13
#define L2X0_AUX_CTRL_ASSOC_MASK		(15 << 13)
/* L2C-210 specific bits */
#define L210_AUX_CTRL_WRAP_DISABLE		BIT(12)
#define L210_AUX_CTRL_WA_OVERRIDE		BIT(23)
#define L210_AUX_CTRL_EXCLUSIVE_ABORT		BIT(24)
/* L2C-220 specific bits */
#define L220_AUX_CTRL_EXCLUSIVE_CACHE		BIT(12)
#define L220_AUX_CTRL_FWA_SHIFT			23
#define L220_AUX_CTRL_FWA_MASK			(3 << 23)
#define L220_AUX_CTRL_NS_LOCKDOWN		BIT(26)
#define L220_AUX_CTRL_NS_INT_CTRL		BIT(27)
/* L2C-310 specific bits */
#define L310_AUX_CTRL_FULL_LINE_ZERO		BIT(0)	/* R2P0+ */
#define L310_AUX_CTRL_HIGHPRIO_SO_DEV		BIT(10)	/* R2P0+ */
#define L310_AUX_CTRL_STORE_LIMITATION		BIT(11)	/* R2P0+ */
#define L310_AUX_CTRL_EXCLUSIVE_CACHE		BIT(12)
#define L310_AUX_CTRL_ASSOCIATIVITY_16		BIT(16)
#define L310_AUX_CTRL_FWA_SHIFT			23
#define L310_AUX_CTRL_FWA_MASK			(3 << 23)
#define L310_AUX_CTRL_CACHE_REPLACE_RR		BIT(25)	/* R2P0+ */
#define L310_AUX_CTRL_NS_LOCKDOWN		BIT(26)
#define L310_AUX_CTRL_NS_INT_CTRL		BIT(27)
#define L310_AUX_CTRL_DATA_PREFETCH		BIT(28)
#define L310_AUX_CTRL_INSTR_PREFETCH		BIT(29)
#define L310_AUX_CTRL_EARLY_BRESP		BIT(30)	/* R2P0+ */

#define L310_LATENCY_CTRL_SETUP(n)		((n) << 0)
#define L310_LATENCY_CTRL_RD(n)			((n) << 4)
#define L310_LATENCY_CTRL_WR(n)			((n) << 8)

#define L310_ADDR_FILTER_EN		1

#define L310_PREFETCH_CTRL_OFFSET_MASK		0x1f
#define L310_PREFETCH_CTRL_DBL_LINEFILL_INCR	BIT(23)
#define L310_PREFETCH_CTRL_PREFETCH_DROP	BIT(24)
#define L310_PREFETCH_CTRL_DBL_LINEFILL_WRAP	BIT(27)
#define L310_PREFETCH_CTRL_DATA_PREFETCH	BIT(28)
#define L310_PREFETCH_CTRL_INSTR_PREFETCH	BIT(29)
#define L310_PREFETCH_CTRL_DBL_LINEFILL		BIT(30)

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

#ifdef CONFIG_CACHE_L2X0_PMU
void l2x0_pmu_register(void __iomem *base, u32 part);
void l2x0_pmu_suspend(void);
void l2x0_pmu_resume(void);
#else
static inline void l2x0_pmu_register(void __iomem *base, u32 part) {}
static inline void l2x0_pmu_suspend(void) {}
static inline void l2x0_pmu_resume(void) {}
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
	unsigned long aux2_ctrl;
};

extern struct l2x0_regs l2x0_saved_regs;

#endif /* __ASSEMBLY__ */

#endif
