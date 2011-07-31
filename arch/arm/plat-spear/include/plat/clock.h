/*
 * arch/arm/plat-spear/include/plat/clock.h
 *
 * Clock framework definitions for SPEAr platform
 *
 * Copyright (C) 2009 ST Microelectronics
 * Viresh Kumar<viresh.kumar@st.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef __PLAT_CLOCK_H
#define __PLAT_CLOCK_H

#include <linux/list.h>
#include <asm/clkdev.h>
#include <linux/types.h>

/* clk structure flags */
#define	ALWAYS_ENABLED		(1 << 0) /* clock always enabled */
#define	RESET_TO_ENABLE		(1 << 1) /* reset register bit to enable clk */

/**
 * struct clkops - clock operations
 * @enable: pointer to clock enable function
 * @disable: pointer to clock disable function
 */
struct clkops {
	int (*enable) (struct clk *);
	void (*disable) (struct clk *);
};

/**
 * struct pclk_info - parents info
 * @pclk: pointer to parent clk
 * @pclk_mask: value to be written for selecting this parent
 * @scalable: Is parent scalable (1 - YES, 0 - NO)
 */
struct pclk_info {
	struct clk *pclk;
	u8 pclk_mask;
	u8 scalable;
};

/**
 * struct pclk_sel - parents selection configuration
 * @pclk_info: pointer to array of parent clock info
 * @pclk_count: number of parents
 * @pclk_sel_reg: register for selecting a parent
 * @pclk_sel_mask: mask for selecting parent (can be used to clear bits also)
 */
struct pclk_sel {
	struct pclk_info *pclk_info;
	u8 pclk_count;
	unsigned int *pclk_sel_reg;
	unsigned int pclk_sel_mask;
};

/**
 * struct clk - clock structure
 * @usage_count: num of users who enabled this clock
 * @flags: flags for clock properties
 * @rate: programmed clock rate in Hz
 * @en_reg: clk enable/disable reg
 * @en_reg_bit: clk enable/disable bit
 * @ops: clk enable/disable ops - generic_clkops selected if NULL
 * @recalc: pointer to clock rate recalculate function
 * @pclk: current parent clk
 * @pclk_sel: pointer to parent selection structure
 * @pclk_sel_shift: register shift for selecting parent of this clock
 * @children: list for childrens or this clock
 * @sibling: node for list of clocks having same parents
 * @private_data: clock specific private data
 */
struct clk {
	unsigned int usage_count;
	unsigned int flags;
	unsigned long rate;
	unsigned int *en_reg;
	u8 en_reg_bit;
	const struct clkops *ops;
	void (*recalc) (struct clk *);

	struct clk *pclk;
	struct pclk_sel *pclk_sel;
	unsigned int pclk_sel_shift;

	struct list_head children;
	struct list_head sibling;
	void *private_data;
};

/* pll configuration structure */
struct pll_clk_config {
	unsigned int *mode_reg;
	unsigned int *cfg_reg;
};

/* ahb and apb bus configuration structure */
struct bus_clk_config {
	unsigned int *reg;
	unsigned int mask;
	unsigned int shift;
};

/*
 * Aux clk configuration structure: applicable to GPT, UART and FIRDA
 */
struct aux_clk_config {
	unsigned int *synth_reg;
};

/* platform specific clock functions */
void clk_register(struct clk_lookup *cl);
void recalc_root_clocks(void);

/* clock recalc functions */
void follow_parent(struct clk *clk);
void pll1_clk_recalc(struct clk *clk);
void bus_clk_recalc(struct clk *clk);
void gpt_clk_recalc(struct clk *clk);
void aux_clk_recalc(struct clk *clk);

#endif /* __PLAT_CLOCK_H */
