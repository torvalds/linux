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

struct clk_omap_divider {
	struct clk_hw		hw;
	struct clk_omap_reg	reg;
	u8			shift;
	u8			width;
	u8			flags;
	s8			latch;
	const struct clk_div_table	*table;
	u32		context;
};

#define to_clk_omap_divider(_hw) container_of(_hw, struct clk_omap_divider, hw)

struct clk_omap_mux {
	struct clk_hw		hw;
	struct clk_omap_reg	reg;
	u32			*table;
	u32			mask;
	u8			shift;
	s8			latch;
	u8			flags;
	u8			saved_parent;
};

#define to_clk_omap_mux(_hw) container_of(_hw, struct clk_omap_mux, hw)

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

/* CLKCTRL flags */
#define CLKF_SW_SUP			BIT(5)
#define CLKF_HW_SUP			BIT(6)
#define CLKF_NO_IDLEST			BIT(7)

#define CLKF_SOC_MASK			GENMASK(11, 8)

#define CLKF_SOC_NONSEC			BIT(8)
#define CLKF_SOC_DRA72			BIT(9)
#define CLKF_SOC_DRA74			BIT(10)
#define CLKF_SOC_DRA76			BIT(11)

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

struct ti_clk_mux {
	u8 bit_shift;
	int num_parents;
	u16 reg;
	u8 module;
	const char * const *parents;
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

struct ti_clk_gate {
	const char *parent;
	u8 bit_shift;
	u16 reg;
	u8 module;
	u16 flags;
};

/* Composite clock component types */
enum {
	CLK_COMPONENT_TYPE_GATE = 0,
	CLK_COMPONENT_TYPE_DIVIDER,
	CLK_COMPONENT_TYPE_MUX,
	CLK_COMPONENT_TYPE_MAX,
};

/**
 * struct ti_dt_clk - OMAP DT clock alias declarations
 * @lk: clock lookup definition
 * @node_name: clock DT node to map to
 */
struct ti_dt_clk {
	struct clk_lookup		lk;
	char				*node_name;
};

#define DT_CLK(dev, con, name)		\
	{				\
		.lk = {			\
			.dev_id = dev,	\
			.con_id = con,	\
		},			\
		.node_name = name,	\
	}

/* CLKCTRL type definitions */
struct omap_clkctrl_div_data {
	const int *dividers;
	int max_div;
	u32 flags;
};

struct omap_clkctrl_bit_data {
	u8 bit;
	u8 type;
	const char * const *parents;
	const void *data;
};

struct omap_clkctrl_reg_data {
	u16 offset;
	const struct omap_clkctrl_bit_data *bit_data;
	u16 flags;
	const char *parent;
	const char *clkdm_name;
};

struct omap_clkctrl_data {
	u32 addr;
	const struct omap_clkctrl_reg_data *regs;
};

extern const struct omap_clkctrl_data omap4_clkctrl_data[];
extern const struct omap_clkctrl_data omap5_clkctrl_data[];
extern const struct omap_clkctrl_data dra7_clkctrl_data[];
extern const struct omap_clkctrl_data dra7_clkctrl_compat_data[];
extern struct ti_dt_clk dra7xx_compat_clks[];
extern const struct omap_clkctrl_data am3_clkctrl_data[];
extern const struct omap_clkctrl_data am3_clkctrl_compat_data[];
extern struct ti_dt_clk am33xx_compat_clks[];
extern const struct omap_clkctrl_data am4_clkctrl_data[];
extern const struct omap_clkctrl_data am4_clkctrl_compat_data[];
extern struct ti_dt_clk am43xx_compat_clks[];
extern const struct omap_clkctrl_data am438x_clkctrl_data[];
extern const struct omap_clkctrl_data am438x_clkctrl_compat_data[];
extern const struct omap_clkctrl_data dm814_clkctrl_data[];
extern const struct omap_clkctrl_data dm816_clkctrl_data[];

typedef void (*ti_of_clk_init_cb_t)(void *, struct device_node *);

struct clk *ti_clk_register(struct device *dev, struct clk_hw *hw,
			    const char *con);
struct clk *ti_clk_register_omap_hw(struct device *dev, struct clk_hw *hw,
				    const char *con);
int ti_clk_add_alias(struct device *dev, struct clk *clk, const char *con);
void ti_clk_add_aliases(void);

void ti_clk_latch(struct clk_omap_reg *reg, s8 shift);

struct clk_hw *ti_clk_build_component_mux(struct ti_clk_mux *setup);

int ti_clk_parse_divider_data(int *div_table, int num_dividers, int max_div,
			      u8 flags, u8 *width,
			      const struct clk_div_table **table);

int ti_clk_get_reg_addr(struct device_node *node, int index,
			struct clk_omap_reg *reg);
void ti_dt_clocks_register(struct ti_dt_clk *oclks);
int ti_clk_retry_init(struct device_node *node, void *user,
		      ti_of_clk_init_cb_t func);
int ti_clk_add_component(struct device_node *node, struct clk_hw *hw, int type);

int of_ti_clk_autoidle_setup(struct device_node *node);
void omap2_clk_enable_init_clocks(const char **clk_names, u8 num_clocks);

extern const struct clk_hw_omap_ops clkhwops_omap3_dpll;
extern const struct clk_hw_omap_ops clkhwops_omap4_dpllmx;
extern const struct clk_hw_omap_ops clkhwops_wait;
extern const struct clk_hw_omap_ops clkhwops_iclk;
extern const struct clk_hw_omap_ops clkhwops_iclk_wait;
extern const struct clk_hw_omap_ops clkhwops_omap2430_i2chs_wait;
extern const struct clk_hw_omap_ops clkhwops_omap3430es2_dss_usbhost_wait;
extern const struct clk_hw_omap_ops clkhwops_omap3430es2_iclk_hsotgusb_wait;
extern const struct clk_hw_omap_ops clkhwops_omap3430es2_iclk_dss_usbhost_wait;
extern const struct clk_hw_omap_ops clkhwops_omap3430es2_iclk_ssi_wait;
extern const struct clk_hw_omap_ops clkhwops_am35xx_ipss_module_wait;
extern const struct clk_hw_omap_ops clkhwops_am35xx_ipss_wait;

extern const struct clk_ops ti_clk_divider_ops;
extern const struct clk_ops ti_clk_mux_ops;
extern const struct clk_ops omap_gate_clk_ops;

extern struct ti_clk_features ti_clk_features;

void omap2_init_clk_clkdm(struct clk_hw *hw);
int omap2_clkops_enable_clkdm(struct clk_hw *hw);
void omap2_clkops_disable_clkdm(struct clk_hw *hw);

int omap2_dflt_clk_enable(struct clk_hw *hw);
void omap2_dflt_clk_disable(struct clk_hw *hw);
int omap2_dflt_clk_is_enabled(struct clk_hw *hw);
void omap2_clk_dflt_find_companion(struct clk_hw_omap *clk,
				   struct clk_omap_reg *other_reg,
				   u8 *other_bit);
void omap2_clk_dflt_find_idlest(struct clk_hw_omap *clk,
				struct clk_omap_reg *idlest_reg,
				u8 *idlest_bit, u8 *idlest_val);

void omap2_clkt_iclk_allow_idle(struct clk_hw_omap *clk);
void omap2_clkt_iclk_deny_idle(struct clk_hw_omap *clk);

u8 omap2_init_dpll_parent(struct clk_hw *hw);
int omap3_noncore_dpll_enable(struct clk_hw *hw);
void omap3_noncore_dpll_disable(struct clk_hw *hw);
int omap3_noncore_dpll_set_parent(struct clk_hw *hw, u8 index);
int omap3_noncore_dpll_set_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long parent_rate);
int omap3_noncore_dpll_set_rate_and_parent(struct clk_hw *hw,
					   unsigned long rate,
					   unsigned long parent_rate,
					   u8 index);
int omap3_noncore_dpll_determine_rate(struct clk_hw *hw,
				      struct clk_rate_request *req);
long omap2_dpll_round_rate(struct clk_hw *hw, unsigned long target_rate,
			   unsigned long *parent_rate);
unsigned long omap3_clkoutx2_recalc(struct clk_hw *hw,
				    unsigned long parent_rate);

/*
 * OMAP3_DPLL5_FREQ_FOR_USBHOST: USBHOST and USBTLL are the only clocks
 * that are sourced by DPLL5, and both of these require this clock
 * to be at 120 MHz for proper operation.
 */
#define OMAP3_DPLL5_FREQ_FOR_USBHOST	120000000

unsigned long omap3_dpll_recalc(struct clk_hw *hw, unsigned long parent_rate);
int omap3_dpll4_set_rate(struct clk_hw *clk, unsigned long rate,
			 unsigned long parent_rate);
int omap3_dpll4_set_rate_and_parent(struct clk_hw *hw, unsigned long rate,
				    unsigned long parent_rate, u8 index);
int omap3_dpll5_set_rate(struct clk_hw *hw, unsigned long rate,
			 unsigned long parent_rate);
void omap3_clk_lock_dpll5(void);

unsigned long omap4_dpll_regm4xen_recalc(struct clk_hw *hw,
					 unsigned long parent_rate);
long omap4_dpll_regm4xen_round_rate(struct clk_hw *hw,
				    unsigned long target_rate,
				    unsigned long *parent_rate);
int omap4_dpll_regm4xen_determine_rate(struct clk_hw *hw,
				       struct clk_rate_request *req);
int omap2_clk_for_each(int (*fn)(struct clk_hw_omap *hw));

extern struct ti_clk_ll_ops *ti_clk_ll_ops;

#endif
