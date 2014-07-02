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
#include <linux/clk-provider.h>
#include <linux/clk/ti.h>

struct omap_clk {
	u16				cpu;
	struct clk_lookup		lk;
};

#define CLK(dev, con, ck)		\
	{				\
		.lk = {			\
			.dev_id = dev,	\
			.con_id = con,	\
			.clk = ck,	\
		},			\
	}

struct clockdomain;

#define DEFINE_STRUCT_CLK(_name, _parent_array_name, _clkops_name)	\
	static struct clk _name = {				\
		.name = #_name,					\
		.hw = &_name##_hw.hw,				\
		.parent_names = _parent_array_name,		\
		.num_parents = ARRAY_SIZE(_parent_array_name),	\
		.ops = &_clkops_name,				\
	};

#define DEFINE_STRUCT_CLK_FLAGS(_name, _parent_array_name,	\
				_clkops_name, _flags)		\
	static struct clk _name = {				\
		.name = #_name,					\
		.hw = &_name##_hw.hw,				\
		.parent_names = _parent_array_name,		\
		.num_parents = ARRAY_SIZE(_parent_array_name),	\
		.ops = &_clkops_name,				\
		.flags = _flags,				\
	};

#define DEFINE_STRUCT_CLK_HW_OMAP(_name, _clkdm_name)		\
	static struct clk_hw_omap _name##_hw = {		\
		.hw = {						\
			.clk = &_name,				\
		},						\
		.clkdm_name = _clkdm_name,			\
	};

#define DEFINE_CLK_OMAP_MUX(_name, _clkdm_name, _clksel,	\
			    _clksel_reg, _clksel_mask,		\
			    _parent_names, _ops)		\
	static struct clk _name;				\
	static struct clk_hw_omap _name##_hw = {		\
		.hw = {						\
			.clk = &_name,				\
		},						\
		.clksel		= _clksel,			\
		.clksel_reg	= _clksel_reg,			\
		.clksel_mask	= _clksel_mask,			\
		.clkdm_name	= _clkdm_name,			\
	};							\
	DEFINE_STRUCT_CLK(_name, _parent_names, _ops);

#define DEFINE_CLK_OMAP_MUX_GATE(_name, _clkdm_name, _clksel,	\
				 _clksel_reg, _clksel_mask,	\
				 _enable_reg, _enable_bit,	\
				 _hwops, _parent_names, _ops)	\
	static struct clk _name;				\
	static struct clk_hw_omap _name##_hw = {		\
		.hw = {						\
			.clk = &_name,				\
		},						\
		.ops		= _hwops,			\
		.enable_reg	= _enable_reg,			\
		.enable_bit	= _enable_bit,			\
		.clksel		= _clksel,			\
		.clksel_reg	= _clksel_reg,			\
		.clksel_mask	= _clksel_mask,			\
		.clkdm_name	= _clkdm_name,			\
	};							\
	DEFINE_STRUCT_CLK(_name, _parent_names, _ops);

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

unsigned long omap_fixed_divisor_recalc(struct clk_hw *hw,
					unsigned long parent_rate);

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

u32 omap3_dpll_autoidle_read(struct clk_hw_omap *clk);
void omap3_dpll_allow_idle(struct clk_hw_omap *clk);
void omap3_dpll_deny_idle(struct clk_hw_omap *clk);
int omap4_dpllmx_gatectrl_read(struct clk_hw_omap *clk);
void omap4_dpllmx_allow_gatectrl(struct clk_hw_omap *clk);
void omap4_dpllmx_deny_gatectrl(struct clk_hw_omap *clk);

void __init omap2_clk_disable_clkdm_control(void);

/* clkt_clksel.c public functions */
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

/* clkt_iclk.c public functions */
extern void omap2_clkt_iclk_allow_idle(struct clk_hw_omap *clk);
extern void omap2_clkt_iclk_deny_idle(struct clk_hw_omap *clk);

unsigned long omap2_get_dpll_rate(struct clk_hw_omap *clk);

void omap2_clk_dflt_find_companion(struct clk_hw_omap *clk,
				   void __iomem **other_reg,
				   u8 *other_bit);
void omap2_clk_dflt_find_idlest(struct clk_hw_omap *clk,
				void __iomem **idlest_reg,
				u8 *idlest_bit, u8 *idlest_val);
int omap2_clk_enable_autoidle_all(void);
int omap2_clk_allow_idle(struct clk *clk);
int omap2_clk_deny_idle(struct clk *clk);
int omap2_clk_switch_mpurate_at_boot(const char *mpurate_ck_name);
void omap2_clk_print_new_rates(const char *hfclkin_ck_name,
			       const char *core_ck_name,
			       const char *mpu_ck_name);

u32 omap2_clk_readl(struct clk_hw_omap *clk, void __iomem *reg);
void omap2_clk_writel(u32 val, struct clk_hw_omap *clk, void __iomem *reg);

extern u16 cpu_mask;

/*
 * Clock features setup. Used instead of CPU type checks.
 */
struct ti_clk_features {
	u32 flags;
	long fint_min;
	long fint_max;
	long fint_band1_max;
	long fint_band2_min;
	u8 dpll_bypass_vals;
};

#define TI_CLK_DPLL_HAS_FREQSEL		(1 << 0)

extern struct ti_clk_features ti_clk_features;

extern const struct clkops clkops_omap2_dflt_wait;
extern const struct clkops clkops_dummy;
extern const struct clkops clkops_omap2_dflt;

extern struct clk_functions omap2_clk_functions;

extern const struct clksel_rate gpt_32k_rates[];
extern const struct clksel_rate gpt_sys_rates[];
extern const struct clksel_rate gfx_l3_rates[];
extern const struct clksel_rate dsp_ick_rates[];
extern struct clk dummy_ck;

extern const struct clk_hw_omap_ops clkhwops_iclk_wait;
extern const struct clk_hw_omap_ops clkhwops_wait;
extern const struct clk_hw_omap_ops clkhwops_omap3430es2_ssi_wait;
extern const struct clk_hw_omap_ops clkhwops_omap3430es2_dss_usbhost_wait;
extern const struct clk_hw_omap_ops clkhwops_omap3430es2_hsotgusb_wait;
extern const struct clk_hw_omap_ops clkhwops_am35xx_ipss_module_wait;
extern const struct clk_hw_omap_ops clkhwops_apll54;
extern const struct clk_hw_omap_ops clkhwops_apll96;

/* clksel_rate blocks shared between OMAP44xx and AM33xx */
extern const struct clksel_rate div_1_0_rates[];
extern const struct clksel_rate div3_1to4_rates[];
extern const struct clksel_rate div_1_1_rates[];
extern const struct clksel_rate div_1_2_rates[];
extern const struct clksel_rate div_1_3_rates[];
extern const struct clksel_rate div_1_4_rates[];
extern const struct clksel_rate div31_1to31_rates[];

extern void __iomem *clk_memmaps[];

extern int am33xx_clk_init(void);

extern int omap2_clkops_enable_clkdm(struct clk_hw *hw);
extern void omap2_clkops_disable_clkdm(struct clk_hw *hw);

extern void omap_clocks_register(struct omap_clk *oclks, int cnt);

void __init ti_clk_init_features(void);
#endif
