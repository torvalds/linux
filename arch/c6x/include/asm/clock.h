/*
 * TI C64X clock definitions
 *
 * Copyright (C) 2010, 2011 Texas Instruments.
 * Contributed by: Mark Salter <msalter@redhat.com>
 *
 * Copied heavily from arm/mach-davinci/clock.h, so:
 *
 * Copyright (C) 2006-2007 Texas Instruments.
 * Copyright (C) 2008-2009 Deep Root Systems, LLC
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _ASM_C6X_CLOCK_H
#define _ASM_C6X_CLOCK_H

#ifndef __ASSEMBLER__

#include <linux/list.h>

/* PLL/Reset register offsets */
#define PLLCTL		0x100
#define PLLM		0x110
#define PLLPRE		0x114
#define PLLDIV1		0x118
#define PLLDIV2		0x11c
#define PLLDIV3		0x120
#define PLLPOST		0x128
#define PLLCMD		0x138
#define PLLSTAT		0x13c
#define PLLALNCTL	0x140
#define PLLDCHANGE	0x144
#define PLLCKEN		0x148
#define PLLCKSTAT	0x14c
#define PLLSYSTAT	0x150
#define PLLDIV4		0x160
#define PLLDIV5		0x164
#define PLLDIV6		0x168
#define PLLDIV7		0x16c
#define PLLDIV8		0x170
#define PLLDIV9		0x174
#define PLLDIV10	0x178
#define PLLDIV11	0x17c
#define PLLDIV12	0x180
#define PLLDIV13	0x184
#define PLLDIV14	0x188
#define PLLDIV15	0x18c
#define PLLDIV16	0x190

/* PLLM register bits */
#define PLLM_PLLM_MASK	0xff
#define PLLM_VAL(x)	((x) - 1)

/* PREDIV register bits */
#define PLLPREDIV_EN	BIT(15)
#define PLLPREDIV_VAL(x) ((x) - 1)

/* PLLCTL register bits */
#define PLLCTL_PLLEN	BIT(0)
#define PLLCTL_PLLPWRDN	BIT(1)
#define PLLCTL_PLLRST	BIT(3)
#define PLLCTL_PLLDIS	BIT(4)
#define PLLCTL_PLLENSRC	BIT(5)
#define PLLCTL_CLKMODE	BIT(8)

/* PLLCMD register bits */
#define PLLCMD_GOSTAT	BIT(0)

/* PLLSTAT register bits */
#define PLLSTAT_GOSTAT	BIT(0)

/* PLLDIV register bits */
#define PLLDIV_EN	BIT(15)
#define PLLDIV_RATIO_MASK 0x1f
#define PLLDIV_RATIO(x) ((x) - 1)

struct pll_data;

struct clk {
	struct list_head	node;
	struct module		*owner;
	const char		*name;
	unsigned long		rate;
	int			usecount;
	u32			flags;
	struct clk		*parent;
	struct list_head	children;	/* list of children */
	struct list_head	childnode;	/* parent's child list node */
	struct pll_data		*pll_data;
	u32			div;
	unsigned long (*recalc) (struct clk *);
	int (*set_rate) (struct clk *clk, unsigned long rate);
	int (*round_rate) (struct clk *clk, unsigned long rate);
};

/* Clock flags: SoC-specific flags start at BIT(16) */
#define ALWAYS_ENABLED		BIT(1)
#define CLK_PLL			BIT(2) /* PLL-derived clock */
#define PRE_PLL			BIT(3) /* source is before PLL mult/div */
#define FIXED_DIV_PLL		BIT(4) /* fixed divisor from PLL */
#define FIXED_RATE_PLL		BIT(5) /* fixed output rate PLL */

#define MAX_PLL_SYSCLKS 16

struct pll_data {
	void __iomem *base;
	u32 num;
	u32 flags;
	u32 input_rate;
	u32 bypass_delay; /* in loops */
	u32 reset_delay;  /* in loops */
	u32 lock_delay;   /* in loops */
	struct clk sysclks[MAX_PLL_SYSCLKS + 1];
};

/* pll_data flag bit */
#define PLL_HAS_PRE	BIT(0)
#define PLL_HAS_MUL	BIT(1)
#define PLL_HAS_POST	BIT(2)

#define CLK(dev, con, ck)	\
	{			\
		.dev_id = dev,	\
		.con_id = con,	\
		.clk = ck,	\
	}			\

extern void c6x_clks_init(struct clk_lookup *clocks);
extern int clk_register(struct clk *clk);
extern void clk_unregister(struct clk *clk);
extern void c64x_setup_clocks(void);

extern struct pll_data c6x_soc_pll1;

extern struct clk clkin1;
extern struct clk c6x_core_clk;
extern struct clk c6x_i2c_clk;
extern struct clk c6x_watchdog_clk;
extern struct clk c6x_mcbsp1_clk;
extern struct clk c6x_mcbsp2_clk;
extern struct clk c6x_mdio_clk;

#endif

#endif /* _ASM_C6X_CLOCK_H */
