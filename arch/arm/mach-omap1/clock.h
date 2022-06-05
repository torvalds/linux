/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  linux/arch/arm/mach-omap1/clock.h
 *
 *  Copyright (C) 2004 - 2005, 2009 Nokia corporation
 *  Written by Tuukka Tikkanen <tuukka.tikkanen@elektrobit.com>
 *  Based on clocks.h by Tony Lindgren, Gordon McNutt and RidgeRun, Inc
 */

#ifndef __ARCH_ARM_MACH_OMAP1_CLOCK_H
#define __ARCH_ARM_MACH_OMAP1_CLOCK_H

#include <linux/clk.h>
#include <linux/list.h>

#include <linux/clkdev.h>

struct module;
struct clk;

struct omap_clk {
	u16				cpu;
	struct clk_lookup		lk;
};

#define CLK(dev, con, ck, cp)		\
	{				\
		 .cpu = cp,		\
		.lk = {			\
			.dev_id = dev,	\
			.con_id = con,	\
			.clk = ck,	\
		},			\
	}

/* Platform flags for the clkdev-OMAP integration code */
#define CK_310		(1 << 0)
#define CK_7XX		(1 << 1)	/* 7xx, 850 */
#define CK_1510		(1 << 2)
#define CK_16XX		(1 << 3)	/* 16xx, 17xx, 5912 */
#define CK_1710		(1 << 4)	/* 1710 extra for rate selection */


/* Temporary, needed during the common clock framework conversion */
#define __clk_get_name(clk)	(clk->name)

/**
 * struct clkops - some clock function pointers
 * @enable: fn ptr that enables the current clock in hardware
 * @disable: fn ptr that enables the current clock in hardware
 * @allow_idle: fn ptr that enables autoidle for the current clock in hardware
 */
struct clkops {
	int			(*enable)(struct clk *);
	void			(*disable)(struct clk *);
};

/*
 * struct clk.flags possibilities
 *
 * XXX document the rest of the clock flags here
 */
#define ENABLE_REG_32BIT	(1 << 0)	/* Use 32-bit access */
#define CLOCK_IDLE_CONTROL	(1 << 1)
#define CLOCK_NO_IDLE_PARENT	(1 << 2)

/**
 * struct clk - OMAP struct clk
 * @node: list_head connecting this clock into the full clock list
 * @ops: struct clkops * for this clock
 * @name: the name of the clock in the hardware (used in hwmod data and debug)
 * @parent: pointer to this clock's parent struct clk
 * @children: list_head connecting to the child clks' @sibling list_heads
 * @sibling: list_head connecting this clk to its parent clk's @children
 * @rate: current clock rate
 * @enable_reg: register to write to enable the clock (see @enable_bit)
 * @recalc: fn ptr that returns the clock's current rate
 * @set_rate: fn ptr that can change the clock's current rate
 * @round_rate: fn ptr that can round the clock's current rate
 * @init: fn ptr to do clock-specific initialization
 * @enable_bit: bitshift to write to enable/disable the clock (see @enable_reg)
 * @usecount: number of users that have requested this clock to be enabled
 * @fixed_div: when > 0, this clock's rate is its parent's rate / @fixed_div
 * @flags: see "struct clk.flags possibilities" above
 * @rate_offset: bitshift for rate selection bitfield (OMAP1 only)
 *
 * XXX @rate_offset should probably be removed and OMAP1
 * clock code converted to use clksel.
 *
 * XXX @usecount is poorly named.  It should be "enable_count" or
 * something similar.  "users" in the description refers to kernel
 * code (core code or drivers) that have called clk_enable() and not
 * yet called clk_disable(); the usecount of parent clocks is also
 * incremented by the clock code when clk_enable() is called on child
 * clocks and decremented by the clock code when clk_disable() is
 * called on child clocks.
 *
 * XXX @usecount, @children, @sibling should be marked for
 * internal use only.
 *
 * @children and @sibling are used to optimize parent-to-child clock
 * tree traversals.  (child-to-parent traversals use @parent.)
 *
 * XXX The notion of the clock's current rate probably needs to be
 * separated from the clock's target rate.
 */
struct clk {
	struct list_head	node;
	const struct clkops	*ops;
	const char		*name;
	struct clk		*parent;
	struct list_head	children;
	struct list_head	sibling;	/* node for children */
	unsigned long		rate;
	void __iomem		*enable_reg;
	unsigned long		(*recalc)(struct clk *);
	int			(*set_rate)(struct clk *, unsigned long);
	long			(*round_rate)(struct clk *, unsigned long);
	void			(*init)(struct clk *);
	u8			enable_bit;
	s8			usecount;
	u8			fixed_div;
	u8			flags;
	u8			rate_offset;
#if defined(CONFIG_PM_DEBUG) && defined(CONFIG_DEBUG_FS)
	struct dentry		*dent;	/* For visible tree hierarchy */
#endif
};

extern void clk_preinit(struct clk *clk);
extern int clk_register(struct clk *clk);
extern void clk_unregister(struct clk *clk);
extern void propagate_rate(struct clk *clk);
extern unsigned long followparent_recalc(struct clk *clk);
unsigned long omap_fixed_divisor_recalc(struct clk *clk);

extern const struct clkops clkops_null;

extern struct clk dummy_ck;

int omap1_clk_init(void);
void omap1_clk_late_init(void);
extern int omap1_clk_enable(struct clk *clk);
extern void omap1_clk_disable(struct clk *clk);
extern long omap1_clk_round_rate(struct clk *clk, unsigned long rate);
extern int omap1_clk_set_rate(struct clk *clk, unsigned long rate);
extern unsigned long omap1_ckctl_recalc(struct clk *clk);
extern int omap1_set_sossi_rate(struct clk *clk, unsigned long rate);
extern unsigned long omap1_sossi_recalc(struct clk *clk);
extern unsigned long omap1_ckctl_recalc_dsp_domain(struct clk *clk);
extern int omap1_clk_set_rate_dsp_domain(struct clk *clk, unsigned long rate);
extern int omap1_set_uart_rate(struct clk *clk, unsigned long rate);
extern unsigned long omap1_uart_recalc(struct clk *clk);
extern int omap1_set_ext_clk_rate(struct clk *clk, unsigned long rate);
extern long omap1_round_ext_clk_rate(struct clk *clk, unsigned long rate);
extern void omap1_init_ext_clk(struct clk *clk);
extern int omap1_select_table_rate(struct clk *clk, unsigned long rate);
extern long omap1_round_to_table_rate(struct clk *clk, unsigned long rate);
extern int omap1_clk_set_rate_ckctl_arm(struct clk *clk, unsigned long rate);
extern long omap1_clk_round_rate_ckctl_arm(struct clk *clk, unsigned long rate);

#ifdef CONFIG_OMAP_RESET_CLOCKS
extern void omap1_clk_disable_unused(struct clk *clk);
#else
#define omap1_clk_disable_unused	NULL
#endif

struct uart_clk {
	struct clk	clk;
	unsigned long	sysc_addr;
};

/* Provide a method for preventing idling some ARM IDLECT clocks */
struct arm_idlect1_clk {
	struct clk	clk;
	unsigned long	no_idle_count;
	__u8		idlect_shift;
};

/* ARM_CKCTL bit shifts */
#define CKCTL_PERDIV_OFFSET	0
#define CKCTL_LCDDIV_OFFSET	2
#define CKCTL_ARMDIV_OFFSET	4
#define CKCTL_DSPDIV_OFFSET	6
#define CKCTL_TCDIV_OFFSET	8
#define CKCTL_DSPMMUDIV_OFFSET	10
/*#define ARM_TIMXO		12*/
#define EN_DSPCK		13
/*#define ARM_INTHCK_SEL	14*/ /* Divide-by-2 for mpu inth_ck */
/* DSP_CKCTL bit shifts */
#define CKCTL_DSPPERDIV_OFFSET	0

/* ARM_IDLECT2 bit shifts */
#define EN_WDTCK	0
#define EN_XORPCK	1
#define EN_PERCK	2
#define EN_LCDCK	3
#define EN_LBCK		4 /* Not on 1610/1710 */
/*#define EN_HSABCK	5*/
#define EN_APICK	6
#define EN_TIMCK	7
#define DMACK_REQ	8
#define EN_GPIOCK	9 /* Not on 1610/1710 */
/*#define EN_LBFREECK	10*/
#define EN_CKOUT_ARM	11

/* ARM_IDLECT3 bit shifts */
#define EN_OCPI_CK	0
#define EN_TC1_CK	2
#define EN_TC2_CK	4

/* DSP_IDLECT2 bit shifts (0,1,2 are same as for ARM_IDLECT2) */
#define EN_DSPTIMCK	5

/* Various register defines for clock controls scattered around OMAP chip */
#define SDW_MCLK_INV_BIT	2	/* In ULPD_CLKC_CTRL */
#define USB_MCLK_EN_BIT		4	/* In ULPD_CLKC_CTRL */
#define USB_HOST_HHC_UHOST_EN	9	/* In MOD_CONF_CTRL_0 */
#define SWD_ULPD_PLL_CLK_REQ	1	/* In SWD_CLK_DIV_CTRL_SEL */
#define COM_ULPD_PLL_CLK_REQ	1	/* In COM_CLK_DIV_CTRL_SEL */
#define SWD_CLK_DIV_CTRL_SEL	0xfffe0874
#define COM_CLK_DIV_CTRL_SEL	0xfffe0878
#define SOFT_REQ_REG		0xfffe0834
#define SOFT_REQ_REG2		0xfffe0880

extern __u32 arm_idlect1_mask;
extern struct clk *api_ck_p, *ck_dpll1_p, *ck_ref_p;

extern const struct clkops clkops_dspck;
extern const struct clkops clkops_uart_16xx;
extern const struct clkops clkops_generic;

/* used for passing SoC type to omap1_{select,round_to}_table_rate() */
extern u32 cpu_mask;

#endif
