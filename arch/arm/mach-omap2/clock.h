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
#define to_clk_hw_omap(_hw) container_of(_hw, struct clk_hw_omap, hw)

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

#define DEFINE_CLK_OMAP_HSDIVIDER(_name, _parent_name,		\
				_parent_ptr, _flags,		\
				_clksel_reg, _clksel_mask)	\
	static const struct clksel _name##_div[] = {		\
		{						\
			.parent = _parent_ptr,			\
			.rates = div31_1to31_rates		\
		},						\
		{ .parent = NULL },				\
	};							\
	static struct clk _name;				\
	static const char *_name##_parent_names[] = {		\
		_parent_name,					\
	};							\
	static struct clk_hw_omap _name##_hw = {		\
		.hw = {						\
			.clk = &_name,				\
		},						\
		.clksel		= _name##_div,			\
		.clksel_reg	= _clksel_reg,			\
		.clksel_mask	= _clksel_mask,			\
		.ops		= &clkhwops_omap4_dpllmx,	\
	};							\
	DEFINE_STRUCT_CLK(_name, _name##_parent_names, omap_hsdivider_ops);

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
 * @last_rounded_m4xen: cache of the last M4X result of
 *			omap4_dpll_regm4xen_round_rate()
 * @last_rounded_lpmode: cache of the last lpmode result of
 *			 omap4_dpll_lpmode_recalc()
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
 * @lpmode_mask: mask of the DPLL low-power mode bitfield in @control_reg
 * @m4xen_mask: mask of the DPLL M4X multiplier bitfield in @control_reg
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
	u8			last_rounded_m4xen;
	u8			last_rounded_lpmode;
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
	u32			lpmode_mask;
	u32			m4xen_mask;
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

void omap2_init_clk_clkdm(struct clk_hw *clk);
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

u8 omap2_init_dpll_parent(struct clk_hw *hw);
unsigned long omap2_get_dpll_rate(struct clk_hw_omap *clk);

int omap2_dflt_clk_enable(struct clk_hw *hw);
void omap2_dflt_clk_disable(struct clk_hw *hw);
int omap2_dflt_clk_is_enabled(struct clk_hw *hw);
void omap2_clk_dflt_find_companion(struct clk_hw_omap *clk,
				   void __iomem **other_reg,
				   u8 *other_bit);
void omap2_clk_dflt_find_idlest(struct clk_hw_omap *clk,
				void __iomem **idlest_reg,
				u8 *idlest_bit, u8 *idlest_val);
void omap2_init_clk_hw_omap_clocks(struct clk *clk);
int omap2_clk_enable_autoidle_all(void);
int omap2_clk_disable_autoidle_all(void);
int omap2_clk_allow_idle(struct clk *clk);
int omap2_clk_deny_idle(struct clk *clk);
void omap2_clk_enable_init_clocks(const char **clk_names, u8 num_clocks);
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
extern struct clk dummy_ck;

extern const struct clk_hw_omap_ops clkhwops_omap3_dpll;
extern const struct clk_hw_omap_ops clkhwops_iclk_wait;
extern const struct clk_hw_omap_ops clkhwops_wait;
extern const struct clk_hw_omap_ops clkhwops_omap4_dpllmx;
extern const struct clk_hw_omap_ops clkhwops_iclk;
extern const struct clk_hw_omap_ops clkhwops_omap3430es2_ssi_wait;
extern const struct clk_hw_omap_ops clkhwops_omap3430es2_iclk_ssi_wait;
extern const struct clk_hw_omap_ops clkhwops_omap3430es2_dss_usbhost_wait;
extern const struct clk_hw_omap_ops clkhwops_omap3430es2_iclk_dss_usbhost_wait;
extern const struct clk_hw_omap_ops clkhwops_omap3430es2_iclk_hsotgusb_wait;
extern const struct clk_hw_omap_ops clkhwops_omap3430es2_hsotgusb_wait;
extern const struct clk_hw_omap_ops clkhwops_am35xx_ipss_module_wait;
extern const struct clk_hw_omap_ops clkhwops_am35xx_ipss_wait;
extern const struct clk_hw_omap_ops clkhwops_apll54;
extern const struct clk_hw_omap_ops clkhwops_apll96;
extern const struct clk_hw_omap_ops clkhwops_omap2xxx_dpll;
extern const struct clk_hw_omap_ops clkhwops_omap2430_i2chs_wait;

/* clksel_rate blocks shared between OMAP44xx and AM33xx */
extern const struct clksel_rate div_1_0_rates[];
extern const struct clksel_rate div3_1to4_rates[];
extern const struct clksel_rate div_1_1_rates[];
extern const struct clksel_rate div_1_2_rates[];
extern const struct clksel_rate div_1_3_rates[];
extern const struct clksel_rate div_1_4_rates[];
extern const struct clksel_rate div31_1to31_rates[];

extern int am33xx_clk_init(void);

extern int omap2_clkops_enable_clkdm(struct clk_hw *hw);
extern void omap2_clkops_disable_clkdm(struct clk_hw *hw);

extern void omap_clocks_register(struct omap_clk *oclks, int cnt);
#endif
