/*
 * TI Clock driver internal definitions
 *
 * Copyright (C) 2014 Texas Instruments, Inc
 *     Tero Kristo (t-kristo@ti.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */
#ifndef __DRIVERS_CLK_TI_CLOCK__
#define __DRIVERS_CLK_TI_CLOCK__

enum {
	TI_CLK_FIXED,
	TI_CLK_MUX,
	TI_CLK_DIVIDER,
	TI_CLK_COMPOSITE,
	TI_CLK_FIXED_FACTOR,
	TI_CLK_GATE,
	TI_CLK_DPLL,
};

/* Global flags */
#define CLKF_INDEX_POWER_OF_TWO		(1 << 0)
#define CLKF_INDEX_STARTS_AT_ONE	(1 << 1)
#define CLKF_SET_RATE_PARENT		(1 << 2)
#define CLKF_OMAP3			(1 << 3)
#define CLKF_AM35XX			(1 << 4)

/* Gate flags */
#define CLKF_SET_BIT_TO_DISABLE		(1 << 5)
#define CLKF_INTERFACE			(1 << 6)
#define CLKF_SSI			(1 << 7)
#define CLKF_DSS			(1 << 8)
#define CLKF_HSOTGUSB			(1 << 9)
#define CLKF_WAIT			(1 << 10)
#define CLKF_NO_WAIT			(1 << 11)
#define CLKF_HSDIV			(1 << 12)
#define CLKF_CLKDM			(1 << 13)

/* DPLL flags */
#define CLKF_LOW_POWER_STOP		(1 << 5)
#define CLKF_LOCK			(1 << 6)
#define CLKF_LOW_POWER_BYPASS		(1 << 7)
#define CLKF_PER			(1 << 8)
#define CLKF_CORE			(1 << 9)
#define CLKF_J_TYPE			(1 << 10)

#define CLK(dev, con, ck)		\
	{				\
		.lk = {			\
			.dev_id = dev,	\
			.con_id = con,	\
		},			\
		.clk = ck,		\
	}

struct ti_clk {
	const char *name;
	const char *clkdm_name;
	int type;
	void *data;
	struct ti_clk *patch;
	struct clk *clk;
};

struct ti_clk_alias {
	struct ti_clk *clk;
	struct clk_lookup lk;
	struct list_head link;
};

struct ti_clk_fixed {
	u32 frequency;
	u16 flags;
};

struct ti_clk_mux {
	u8 bit_shift;
	int num_parents;
	u16 reg;
	u8 module;
	const char **parents;
	u16 flags;
};

struct ti_clk_divider {
	const char *parent;
	u8 bit_shift;
	u16 max_div;
	u16 reg;
	u8 module;
	int *dividers;
	int num_dividers;
	u16 flags;
};

struct ti_clk_fixed_factor {
	const char *parent;
	u16 div;
	u16 mult;
	u16 flags;
};

struct ti_clk_gate {
	const char *parent;
	u8 bit_shift;
	u16 reg;
	u8 module;
	u16 flags;
};

struct ti_clk_composite {
	struct ti_clk_divider *divider;
	struct ti_clk_mux *mux;
	struct ti_clk_gate *gate;
	u16 flags;
};

struct ti_clk_clkdm_gate {
	const char *parent;
	u16 flags;
};

struct ti_clk_dpll {
	int num_parents;
	u16 control_reg;
	u16 idlest_reg;
	u16 autoidle_reg;
	u16 mult_div1_reg;
	u8 module;
	const char **parents;
	u16 flags;
	u8 modes;
	u32 mult_mask;
	u32 div1_mask;
	u32 enable_mask;
	u32 autoidle_mask;
	u32 freqsel_mask;
	u32 idlest_mask;
	u32 dco_mask;
	u32 sddiv_mask;
	u16 max_multiplier;
	u16 max_divider;
	u8 min_divider;
	u8 auto_recal_bit;
	u8 recal_en_bit;
	u8 recal_st_bit;
};

struct clk *ti_clk_register_gate(struct ti_clk *setup);
struct clk *ti_clk_register_interface(struct ti_clk *setup);
struct clk *ti_clk_register_mux(struct ti_clk *setup);
struct clk *ti_clk_register_divider(struct ti_clk *setup);
struct clk *ti_clk_register_composite(struct ti_clk *setup);
struct clk *ti_clk_register_dpll(struct ti_clk *setup);

struct clk_hw *ti_clk_build_component_div(struct ti_clk_divider *setup);
struct clk_hw *ti_clk_build_component_gate(struct ti_clk_gate *setup);
struct clk_hw *ti_clk_build_component_mux(struct ti_clk_mux *setup);

void ti_clk_patch_legacy_clks(struct ti_clk **patch);
struct clk *ti_clk_register_clk(struct ti_clk *setup);
int ti_clk_register_legacy_clks(struct ti_clk_alias *clks);

#endif
