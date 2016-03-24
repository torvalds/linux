/*
 * TI DaVinci clock definitions
 *
 * Copyright (C) 2006-2007 Texas Instruments.
 * Copyright (C) 2008-2009 Deep Root Systems, LLC
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ARCH_ARM_DAVINCI_CLOCK_H
#define __ARCH_ARM_DAVINCI_CLOCK_H

#define DAVINCI_PLL1_BASE 0x01c40800
#define DAVINCI_PLL2_BASE 0x01c40c00
#define MAX_PLL 2

/* PLL/Reset register offsets */
#define PLLCTL          0x100
#define PLLCTL_PLLEN    BIT(0)
#define PLLCTL_PLLPWRDN	BIT(1)
#define PLLCTL_PLLRST	BIT(3)
#define PLLCTL_PLLDIS	BIT(4)
#define PLLCTL_PLLENSRC	BIT(5)
#define PLLCTL_CLKMODE  BIT(8)

#define PLLM		0x110
#define PLLM_PLLM_MASK  0xff

#define PREDIV          0x114
#define PLLDIV1         0x118
#define PLLDIV2         0x11c
#define PLLDIV3         0x120
#define POSTDIV         0x128
#define BPDIV           0x12c
#define PLLCMD		0x138
#define PLLSTAT		0x13c
#define PLLALNCTL	0x140
#define PLLDCHANGE	0x144
#define PLLCKEN		0x148
#define PLLCKSTAT	0x14c
#define PLLSYSTAT	0x150
#define PLLDIV4         0x160
#define PLLDIV5         0x164
#define PLLDIV6         0x168
#define PLLDIV7         0x16c
#define PLLDIV8         0x170
#define PLLDIV9         0x174
#define PLLDIV_EN       BIT(15)
#define PLLDIV_RATIO_MASK 0x1f

/*
 * OMAP-L138 system reference guide recommends a wait for 4 OSCIN/CLKIN
 * cycles to ensure that the PLLC has switched to bypass mode. Delay of 1us
 * ensures we are good for all > 4MHz OSCIN/CLKIN inputs. Typically the input
 * is ~25MHz. Units are micro seconds.
 */
#define PLL_BYPASS_TIME		1
/* From OMAP-L138 datasheet table 6-4. Units are micro seconds */
#define PLL_RESET_TIME		1
/*
 * From OMAP-L138 datasheet table 6-4; assuming prediv = 1, sqrt(pllm) = 4
 * Units are micro seconds.
 */
#define PLL_LOCK_TIME		20

#ifndef __ASSEMBLER__

#include <linux/list.h>
#include <linux/clkdev.h>

#define PLLSTAT_GOSTAT	BIT(0)
#define PLLCMD_GOSET	BIT(0)

struct pll_data {
	u32 phys_base;
	void __iomem *base;
	u32 num;
	u32 flags;
	u32 input_rate;
	u32 div_ratio_mask;
};
#define PLL_HAS_PREDIV          0x01
#define PLL_HAS_POSTDIV         0x02

struct clk {
	struct list_head	node;
	struct module		*owner;
	const char		*name;
	unsigned long		rate;
	unsigned long		maxrate;	/* H/W supported max rate */
	u8			usecount;
	u8			lpsc;
	u8			gpsc;
	u8			domain;
	u32			flags;
	struct clk              *parent;
	struct list_head	children; 	/* list of children */
	struct list_head	childnode;	/* parent's child list node */
	struct pll_data         *pll_data;
	u32                     div_reg;
	unsigned long (*recalc) (struct clk *);
	int (*set_rate) (struct clk *clk, unsigned long rate);
	int (*round_rate) (struct clk *clk, unsigned long rate);
	int (*reset) (struct clk *clk, bool reset);
	void (*clk_enable) (struct clk *clk);
	void (*clk_disable) (struct clk *clk);
	int (*set_parent) (struct clk *clk, struct clk *parent);
};

/* Clock flags: SoC-specific flags start at BIT(16) */
#define ALWAYS_ENABLED		BIT(1)
#define CLK_PSC			BIT(2)
#define CLK_PLL			BIT(3) /* PLL-derived clock */
#define PRE_PLL			BIT(4) /* source is before PLL mult/div */
#define PSC_SWRSTDISABLE	BIT(5) /* Disable state is SwRstDisable */
#define PSC_FORCE		BIT(6) /* Force module state transtition */
#define PSC_LRST		BIT(8) /* Use local reset on enable/disable */

#define CLK(dev, con, ck) 	\
	{			\
		.dev_id = dev,	\
		.con_id = con,	\
		.clk = ck,	\
	}			\

int davinci_clk_init(struct clk_lookup *clocks);
int davinci_set_pllrate(struct pll_data *pll, unsigned int prediv,
				unsigned int mult, unsigned int postdiv);
int davinci_set_sysclk_rate(struct clk *clk, unsigned long rate);
int davinci_set_refclk_rate(unsigned long rate);
int davinci_simple_set_rate(struct clk *clk, unsigned long rate);
int davinci_clk_reset(struct clk *clk, bool reset);

extern struct platform_device davinci_wdt_device;
extern void davinci_watchdog_reset(struct platform_device *);

#endif

#endif
