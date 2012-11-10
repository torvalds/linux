/*
 *  linux/arch/arm/mach-omap2/clock.h
 *
 *  Copyright (C) 2005-2009 Texas Instruments, Inc.
 *  Copyright (C) 2004-2011 Nokia Corporation
 *
 *  Contacts:
 *  Richard Woodruff <r-woodruff2@ti.com>
 *  Paul Walmsley
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ARCH_ARM_MACH_OMAP2_CLOCK_H
#define __ARCH_ARM_MACH_OMAP2_CLOCK_H

#include <linux/kernel.h>
#include <linux/list.h>

#include <linux/clkdev.h>

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
#define CK_242X		(1 << 0)
#define CK_243X		(1 << 1)	/* 243x, 253x */
#define CK_3430ES1	(1 << 2)	/* 34xxES1 only */
#define CK_3430ES2PLUS	(1 << 3)	/* 34xxES2, ES3, non-Sitara 35xx only */
#define CK_AM35XX	(1 << 4)	/* Sitara AM35xx */
#define CK_36XX		(1 << 5)	/* 36xx/37xx-specific clocks */
#define CK_443X		(1 << 6)
#define CK_TI816X	(1 << 7)
#define CK_446X		(1 << 8)
#define CK_AM33XX	(1 << 9)	/* AM33xx specific clocks */


#define CK_34XX		(CK_3430ES1 | CK_3430ES2PLUS)
#define CK_3XXX		(CK_34XX | CK_AM35XX | CK_36XX)

#ifdef CONFIG_COMMON_CLK
#include <linux/clk-provider.h>

struct clockdomain;
#define to_clk_hw_omap(_hw) container_of(_hw, struct clk_hw_omap, hw)

#else

struct module;
struct clk;
struct clockdomain;

/* Temporary, needed during the common clock framework conversion */
#define __clk_get_name(clk)	(clk->name)
#define __clk_get_parent(clk)	(clk->parent)
#define __clk_get_rate(clk)	(clk->rate)

/**
 * struct clkops - some clock function pointers
 * @enable: fn ptr that enables the current clock in hardware
 * @disable: fn ptr that enables the current clock in hardware
 * @find_idlest: function returning the IDLEST register for the clock's IP blk
 * @find_companion: function returning the "companion" clk reg for the clock
 * @allow_idle: fn ptr that enables autoidle for the current clock in hardware
 * @deny_idle: fn ptr that disables autoidle for the current clock in hardware
 *
 * A "companion" clk is an accompanying clock to the one being queried
 * that must be enabled for the IP module connected to the clock to
 * become accessible by the hardware.  Neither @find_idlest nor
 * @find_companion should be needed; that information is IP
 * block-specific; the hwmod code has been created to handle this, but
 * until hwmod data is ready and drivers have been converted to use PM
 * runtime calls in place of clk_enable()/clk_disable(), @find_idlest and
 * @find_companion must, unfortunately, remain.
 */
struct clkops {
	int			(*enable)(struct clk *);
	void			(*disable)(struct clk *);
	void			(*find_idlest)(struct clk *, void __iomem **,
					       u8 *, u8 *);
	void			(*find_companion)(struct clk *, void __iomem **,
						  u8 *);
	void			(*allow_idle)(struct clk *);
	void			(*deny_idle)(struct clk *);
};
#endif

/* struct clksel_rate.flags possibilities */
#define RATE_IN_242X		(1 << 0)
#define RATE_IN_243X		(1 << 1)
#define RATE_IN_3430ES1		(1 << 2)	/* 3430ES1 rates only */
#define RATE_IN_3430ES2PLUS	(1 << 3)	/* 3430 ES >= 2 rates only */
#define RATE_IN_36XX		(1 << 4)
#define RATE_IN_4430		(1 << 5)
#define RATE_IN_TI816X		(1 << 6)
#define RATE_IN_4460		(1 << 7)
#define RATE_IN_AM33XX		(1 << 8)
#define RATE_IN_TI814X		(1 << 9)

#define RATE_IN_24XX		(RATE_IN_242X | RATE_IN_243X)
#define RATE_IN_34XX		(RATE_IN_3430ES1 | RATE_IN_3430ES2PLUS)
#define RATE_IN_3XXX		(RATE_IN_34XX | RATE_IN_36XX)
#define RATE_IN_44XX		(RATE_IN_4430 | RATE_IN_4460)

/* RATE_IN_3430ES2PLUS_36XX includes 34xx/35xx with ES >=2, and all 36xx/37xx */
#define RATE_IN_3430ES2PLUS_36XX	(RATE_IN_3430ES2PLUS | RATE_IN_36XX)


/**
 * struct clksel_rate - register bitfield values corresponding to clk divisors
 * @val: register bitfield value (shifted to bit 0)
 * @div: clock divisor corresponding to @val
 * @flags: (see "struct clksel_rate.flags possibilities" above)
 *
 * @val should match the value of a read from struct clk.clksel_reg
 * AND'ed with struct clk.clksel_mask, shifted right to bit 0.
 *
 * @div is the divisor that should be applied to the parent clock's rate
 * to produce the current clock's rate.
 */
struct clksel_rate {
	u32			val;
	u8			div;
	u16			flags;
};

/**
 * struct clksel - available parent clocks, and a pointer to their divisors
 * @parent: struct clk * to a possible parent clock
 * @rates: available divisors for this parent clock
 *
 * A struct clksel is always associated with one or more struct clks
 * and one or more struct clksel_rates.
 */
struct clksel {
	struct clk		 *parent;
	const struct clksel_rate *rates;
};

/**
 * struct dpll_data - DPLL registers and integration data
 * @mult_div1_reg: register containing the DPLL M and N bitfields
 * @mult_mask: mask of the DPLL M bitfield in @mult_div1_reg
 * @div1_mask: mask of the DPLL N bitfield in @mult_div1_reg
 * @clk_bypass: struct clk pointer to the clock's bypass clock input
 * @clk_ref: struct clk pointer to the clock's reference clock input
 * @control_reg: register containing the DPLL mode bitfield
 * @enable_mask: mask of the DPLL mode bitfield in @control_reg
 * @last_rounded_rate: cache of the last rate result of omap2_dpll_round_rate()
 * @last_rounded_m: cache of the last M result of omap2_dpll_round_rate()
 * @max_multiplier: maximum valid non-bypass multiplier value (actual)
 * @last_rounded_n: cache of the last N result of omap2_dpll_round_rate()
 * @min_divider: minimum valid non-bypass divider value (actual)
 * @max_divider: maximum valid non-bypass divider value (actual)
 * @modes: possible values of @enable_mask
 * @autoidle_reg: register containing the DPLL autoidle mode bitfield
 * @idlest_reg: register containing the DPLL idle status bitfield
 * @autoidle_mask: mask of the DPLL autoidle mode bitfield in @autoidle_reg
 * @freqsel_mask: mask of the DPLL jitter correction bitfield in @control_reg
 * @idlest_mask: mask of the DPLL idle status bitfield in @idlest_reg
 * @auto_recal_bit: bitshift of the driftguard enable bit in @control_reg
 * @recal_en_bit: bitshift of the PRM_IRQENABLE_* bit for recalibration IRQs
 * @recal_st_bit: bitshift of the PRM_IRQSTATUS_* bit for recalibration IRQs
 * @flags: DPLL type/features (see below)
 *
 * Possible values for @flags:
 * DPLL_J_TYPE: "J-type DPLL" (only some 36xx, 4xxx DPLLs)
 *
 * @freqsel_mask is only used on the OMAP34xx family and AM35xx.
 *
 * XXX Some DPLLs have multiple bypass inputs, so it's not technically
 * correct to only have one @clk_bypass pointer.
 *
 * XXX The runtime-variable fields (@last_rounded_rate, @last_rounded_m,
 * @last_rounded_n) should be separated from the runtime-fixed fields
 * and placed into a different structure, so that the runtime-fixed data
 * can be placed into read-only space.
 */
struct dpll_data {
	void __iomem		*mult_div1_reg;
	u32			mult_mask;
	u32			div1_mask;
	struct clk		*clk_bypass;
	struct clk		*clk_ref;
	void __iomem		*control_reg;
	u32			enable_mask;
	unsigned long		last_rounded_rate;
	u16			last_rounded_m;
	u16			max_multiplier;
	u8			last_rounded_n;
	u8			min_divider;
	u16			max_divider;
	u8			modes;
	void __iomem		*autoidle_reg;
	void __iomem		*idlest_reg;
	u32			autoidle_mask;
	u32			freqsel_mask;
	u32			idlest_mask;
	u32			dco_mask;
	u32			sddiv_mask;
	u8			auto_recal_bit;
	u8			recal_en_bit;
	u8			recal_st_bit;
	u8			flags;
};

/*
 * struct clk.flags possibilities
 *
 * XXX document the rest of the clock flags here
 *
 * CLOCK_CLKOUTX2: (OMAP4 only) DPLL CLKOUT and CLKOUTX2 GATE_CTRL
 *     bits share the same register.  This flag allows the
 *     omap4_dpllmx*() code to determine which GATE_CTRL bit field
 *     should be used.  This is a temporary solution - a better approach
 *     would be to associate clock type-specific data with the clock,
 *     similar to the struct dpll_data approach.
 */
#define ENABLE_REG_32BIT	(1 << 0)	/* Use 32-bit access */
#define CLOCK_IDLE_CONTROL	(1 << 1)
#define CLOCK_NO_IDLE_PARENT	(1 << 2)
#define ENABLE_ON_INIT		(1 << 3)	/* Enable upon framework init */
#define INVERT_ENABLE		(1 << 4)	/* 0 enables, 1 disables */
#define CLOCK_CLKOUTX2		(1 << 5)

#ifdef CONFIG_COMMON_CLK
/**
 * struct clk_hw_omap - OMAP struct clk
 * @node: list_head connecting this clock into the full clock list
 * @enable_reg: register to write to enable the clock (see @enable_bit)
 * @enable_bit: bitshift to write to enable/disable the clock (see @enable_reg)
 * @flags: see "struct clk.flags possibilities" above
 * @clksel_reg: for clksel clks, register va containing src/divisor select
 * @clksel_mask: bitmask in @clksel_reg for the src/divisor selector
 * @clksel: for clksel clks, pointer to struct clksel for this clock
 * @dpll_data: for DPLLs, pointer to struct dpll_data for this clock
 * @clkdm_name: clockdomain name that this clock is contained in
 * @clkdm: pointer to struct clockdomain, resolved from @clkdm_name at runtime
 * @rate_offset: bitshift for rate selection bitfield (OMAP1 only)
 * @src_offset: bitshift for source selection bitfield (OMAP1 only)
 *
 * XXX @rate_offset, @src_offset should probably be removed and OMAP1
 * clock code converted to use clksel.
 *
 */

struct clk_hw_omap_ops;

struct clk_hw_omap {
	struct clk_hw		hw;
	struct list_head	node;
	unsigned long		fixed_rate;
	u8			fixed_div;
	void __iomem		*enable_reg;
	u8			enable_bit;
	u8			flags;
	void __iomem		*clksel_reg;
	u32			clksel_mask;
	const struct clksel	*clksel;
	struct dpll_data	*dpll_data;
	const char		*clkdm_name;
	struct clockdomain	*clkdm;
	const struct clk_hw_omap_ops	*ops;
};

struct clk_hw_omap_ops {
	void			(*find_idlest)(struct clk_hw_omap *oclk,
					void __iomem **idlest_reg,
					u8 *idlest_bit, u8 *idlest_val);
	void			(*find_companion)(struct clk_hw_omap *oclk,
					void __iomem **other_reg,
					u8 *other_bit);
	void			(*allow_idle)(struct clk_hw_omap *oclk);
	void			(*deny_idle)(struct clk_hw_omap *oclk);
};

unsigned long omap_fixed_divisor_recalc(struct clk_hw *hw,
					unsigned long parent_rate);
#else
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
 * @clksel_reg: for clksel clks, register va containing src/divisor select
 * @clksel_mask: bitmask in @clksel_reg for the src/divisor selector
 * @clksel: for clksel clks, pointer to struct clksel for this clock
 * @dpll_data: for DPLLs, pointer to struct dpll_data for this clock
 * @clkdm_name: clockdomain name that this clock is contained in
 * @clkdm: pointer to struct clockdomain, resolved from @clkdm_name at runtime
 * @rate_offset: bitshift for rate selection bitfield (OMAP1 only)
 * @src_offset: bitshift for source selection bitfield (OMAP1 only)
 *
 * XXX @rate_offset, @src_offset should probably be removed and OMAP1
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
 * XXX @clkdm, @usecount, @children, @sibling should be marked for
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
	void __iomem		*clksel_reg;
	u32			clksel_mask;
	const struct clksel	*clksel;
	struct dpll_data	*dpll_data;
	const char		*clkdm_name;
	struct clockdomain	*clkdm;
#if defined(CONFIG_PM_DEBUG) && defined(CONFIG_DEBUG_FS)
	struct dentry		*dent;	/* For visible tree hierarchy */
#endif
};

struct clk_functions {
	int		(*clk_enable)(struct clk *clk);
	void		(*clk_disable)(struct clk *clk);
	long		(*clk_round_rate)(struct clk *clk, unsigned long rate);
	int		(*clk_set_rate)(struct clk *clk, unsigned long rate);
	int		(*clk_set_parent)(struct clk *clk, struct clk *parent);
	void		(*clk_allow_idle)(struct clk *clk);
	void		(*clk_deny_idle)(struct clk *clk);
	void		(*clk_disable_unused)(struct clk *clk);
};

extern int mpurate;

extern int clk_init(struct clk_functions *custom_clocks);
extern void clk_preinit(struct clk *clk);
extern int clk_register(struct clk *clk);
extern void clk_reparent(struct clk *child, struct clk *parent);
extern void clk_unregister(struct clk *clk);
extern void propagate_rate(struct clk *clk);
extern void recalculate_root_clocks(void);
extern unsigned long followparent_recalc(struct clk *clk);
extern void clk_enable_init_clocks(void);
unsigned long omap_fixed_divisor_recalc(struct clk *clk);
extern struct clk *omap_clk_get_by_name(const char *name);
extern int omap_clk_enable_autoidle_all(void);
extern int omap_clk_disable_autoidle_all(void);

extern const struct clkops clkops_null;

extern struct clk dummy_ck;

#endif /* CONFIG_COMMON_CLK */

/* CM_CLKSEL2_PLL.CORE_CLK_SRC bits (2XXX) */
#define CORE_CLK_SRC_32K		0x0
#define CORE_CLK_SRC_DPLL		0x1
#define CORE_CLK_SRC_DPLL_X2		0x2

/* OMAP2xxx CM_CLKEN_PLL.EN_DPLL bits - for omap2_get_dpll_rate() */
#define OMAP2XXX_EN_DPLL_LPBYPASS		0x1
#define OMAP2XXX_EN_DPLL_FRBYPASS		0x2
#define OMAP2XXX_EN_DPLL_LOCKED			0x3

/* OMAP3xxx CM_CLKEN_PLL*.EN_*_DPLL bits - for omap2_get_dpll_rate() */
#define OMAP3XXX_EN_DPLL_LPBYPASS		0x5
#define OMAP3XXX_EN_DPLL_FRBYPASS		0x6
#define OMAP3XXX_EN_DPLL_LOCKED			0x7

/* OMAP4xxx CM_CLKMODE_DPLL*.EN_*_DPLL bits - for omap2_get_dpll_rate() */
#define OMAP4XXX_EN_DPLL_MNBYPASS		0x4
#define OMAP4XXX_EN_DPLL_LPBYPASS		0x5
#define OMAP4XXX_EN_DPLL_FRBYPASS		0x6
#define OMAP4XXX_EN_DPLL_LOCKED			0x7

/* CM_CLKEN_PLL*.EN* bit values - not all are available for every DPLL */
#define DPLL_LOW_POWER_STOP	0x1
#define DPLL_LOW_POWER_BYPASS	0x5
#define DPLL_LOCKED		0x7

/* DPLL Type and DCO Selection Flags */
#define DPLL_J_TYPE		0x1

#ifndef CONFIG_COMMON_CLK
int omap2_clk_enable(struct clk *clk);
void omap2_clk_disable(struct clk *clk);
long omap2_clk_round_rate(struct clk *clk, unsigned long rate);
int omap2_clk_set_rate(struct clk *clk, unsigned long rate);
int omap2_clk_set_parent(struct clk *clk, struct clk *new_parent);
#endif /* CONFIG_COMMON_CLK */

#ifdef CONFIG_COMMON_CLK
long omap2_dpll_round_rate(struct clk_hw *hw, unsigned long target_rate,
			unsigned long *parent_rate);
unsigned long omap3_dpll_recalc(struct clk_hw *hw, unsigned long parent_rate);
int omap3_noncore_dpll_enable(struct clk_hw *hw);
void omap3_noncore_dpll_disable(struct clk_hw *hw);
int omap3_noncore_dpll_set_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long parent_rate);
u32 omap3_dpll_autoidle_read(struct clk_hw_omap *clk);
void omap3_dpll_allow_idle(struct clk_hw_omap *clk);
void omap3_dpll_deny_idle(struct clk_hw_omap *clk);
unsigned long omap3_clkoutx2_recalc(struct clk_hw *hw,
				    unsigned long parent_rate);
int omap4_dpllmx_gatectrl_read(struct clk_hw_omap *clk);
void omap4_dpllmx_allow_gatectrl(struct clk_hw_omap *clk);
void omap4_dpllmx_deny_gatectrl(struct clk_hw_omap *clk);
unsigned long omap4_dpll_regm4xen_recalc(struct clk_hw *hw,
				unsigned long parent_rate);
long omap4_dpll_regm4xen_round_rate(struct clk_hw *hw,
				    unsigned long target_rate,
				    unsigned long *parent_rate);
#else
long omap2_dpll_round_rate(struct clk *clk, unsigned long target_rate);
unsigned long omap3_dpll_recalc(struct clk *clk);
unsigned long omap3_clkoutx2_recalc(struct clk *clk);
void omap3_dpll_allow_idle(struct clk *clk);
void omap3_dpll_deny_idle(struct clk *clk);
u32 omap3_dpll_autoidle_read(struct clk *clk);
int omap3_noncore_dpll_set_rate(struct clk *clk, unsigned long rate);
int omap3_noncore_dpll_enable(struct clk *clk);
void omap3_noncore_dpll_disable(struct clk *clk);
int omap4_dpllmx_gatectrl_read(struct clk *clk);
void omap4_dpllmx_allow_gatectrl(struct clk *clk);
void omap4_dpllmx_deny_gatectrl(struct clk *clk);
long omap4_dpll_regm4xen_round_rate(struct clk *clk, unsigned long target_rate);
unsigned long omap4_dpll_regm4xen_recalc(struct clk *clk);
#endif

#ifdef CONFIG_OMAP_RESET_CLOCKS
void omap2_clk_disable_unused(struct clk *clk);
#else
#define omap2_clk_disable_unused	NULL
#endif
#ifdef CONFIG_COMMON_CLK
void omap2_init_clk_clkdm(struct clk_hw *clk);
#else
void omap2_init_clk_clkdm(struct clk *clk);
#endif
void __init omap2_clk_disable_clkdm_control(void);

/* clkt_clksel.c public functions */
#ifdef CONFIG_COMMON_CLK
u32 omap2_clksel_round_rate_div(struct clk_hw_omap *clk,
				unsigned long target_rate,
				u32 *new_div);
u8 omap2_clksel_find_parent_index(struct clk_hw *hw);
unsigned long omap2_clksel_recalc(struct clk_hw *hw, unsigned long parent_rate);
long omap2_clksel_round_rate(struct clk_hw *hw, unsigned long target_rate,
				unsigned long *parent_rate);
int omap2_clksel_set_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long parent_rate);
int omap2_clksel_set_parent(struct clk_hw *hw, u8 field_val);
#else
u32 omap2_clksel_round_rate_div(struct clk *clk, unsigned long target_rate,
				u32 *new_div);
void omap2_init_clksel_parent(struct clk *clk);
unsigned long omap2_clksel_recalc(struct clk *clk);
long omap2_clksel_round_rate(struct clk *clk, unsigned long target_rate);
int omap2_clksel_set_rate(struct clk *clk, unsigned long rate);
int omap2_clksel_set_parent(struct clk *clk, struct clk *new_parent);
#endif

/* clkt_iclk.c public functions */
extern void omap2_clkt_iclk_allow_idle(struct clk *clk);
extern void omap2_clkt_iclk_deny_idle(struct clk *clk);

#ifdef CONFIG_COMMON_CLK
u8 omap2_init_dpll_parent(struct clk_hw *hw);
unsigned long omap2_get_dpll_rate(struct clk_hw_omap *clk);
#else
u32 omap2_get_dpll_rate(struct clk *clk);
void omap2_init_dpll_parent(struct clk *clk);
#endif

#ifdef CONFIG_COMMON_CLK
int omap2_dflt_clk_enable(struct clk_hw *hw);
void omap2_dflt_clk_disable(struct clk_hw *hw);
int omap2_dflt_clk_is_enabled(struct clk_hw *hw);
void omap2_clk_dflt_find_companion(struct clk_hw_omap *clk,
				   void __iomem **other_reg,
				   u8 *other_bit);
void omap2_clk_dflt_find_idlest(struct clk_hw_omap *clk,
				void __iomem **idlest_reg,
				u8 *idlest_bit, u8 *idlest_val);
#else
int omap2_dflt_clk_enable(struct clk *clk);
void omap2_dflt_clk_disable(struct clk *clk);
void omap2_clk_dflt_find_companion(struct clk *clk, void __iomem **other_reg,
				   u8 *other_bit);
void omap2_clk_dflt_find_idlest(struct clk *clk, void __iomem **idlest_reg,
				u8 *idlest_bit, u8 *idlest_val);
#endif
int omap2_clk_switch_mpurate_at_boot(const char *mpurate_ck_name);
void omap2_clk_print_new_rates(const char *hfclkin_ck_name,
			       const char *core_ck_name,
			       const char *mpu_ck_name);

extern u16 cpu_mask;

extern const struct clkops clkops_omap2_dflt_wait;
extern const struct clkops clkops_dummy;
extern const struct clkops clkops_omap2_dflt;

extern struct clk_functions omap2_clk_functions;

extern const struct clksel_rate gpt_32k_rates[];
extern const struct clksel_rate gpt_sys_rates[];
extern const struct clksel_rate gfx_l3_rates[];
extern const struct clksel_rate dsp_ick_rates[];

#ifdef CONFIG_COMMON_CLK
extern const struct clk_hw_omap_ops clkhwops_omap3_dpll;
extern const struct clk_hw_omap_ops clkhwops_iclk_wait;
extern const struct clk_hw_omap_ops clkhwops_wait;
extern const struct clk_hw_omap_ops clkhwops_omap4_dpllmx;
#endif

extern const struct clkops clkops_omap2_iclk_dflt_wait;
extern const struct clkops clkops_omap2_iclk_dflt;
extern const struct clkops clkops_omap2_iclk_idle_only;
extern const struct clkops clkops_omap2_mdmclk_dflt_wait;
extern const struct clkops clkops_omap2xxx_dpll_ops;
extern const struct clkops clkops_omap3_noncore_dpll_ops;
extern const struct clkops clkops_omap3_core_dpll_ops;
extern const struct clkops clkops_omap4_dpllmx_ops;

/* clksel_rate blocks shared between OMAP44xx and AM33xx */
extern const struct clksel_rate div_1_0_rates[];
extern const struct clksel_rate div_1_1_rates[];
extern const struct clksel_rate div_1_2_rates[];
extern const struct clksel_rate div_1_3_rates[];
extern const struct clksel_rate div_1_4_rates[];
extern const struct clksel_rate div31_1to31_rates[];

#ifndef CONFIG_COMMON_CLK
/* clocks shared between various OMAP SoCs */
extern struct clk virt_19200000_ck;
extern struct clk virt_26000000_ck;
#endif

extern int am33xx_clk_init(void);

#ifdef CONFIG_COMMON_CLK
extern int omap2_clkops_enable_clkdm(struct clk_hw *hw);
extern void omap2_clkops_disable_clkdm(struct clk_hw *hw);
#endif

#endif
