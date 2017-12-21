/*
 * OMAP DPLL clock support
 *
 * Copyright (C) 2013 Texas Instruments, Inc.
 *
 * Tero Kristo <t-kristo@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/clk/ti.h>
#include "clock.h"

#undef pr_fmt
#define pr_fmt(fmt) "%s: " fmt, __func__

#if defined(CONFIG_ARCH_OMAP4) || defined(CONFIG_SOC_OMAP5) || \
	defined(CONFIG_SOC_DRA7XX)
static const struct clk_ops dpll_m4xen_ck_ops = {
	.enable		= &omap3_noncore_dpll_enable,
	.disable	= &omap3_noncore_dpll_disable,
	.recalc_rate	= &omap4_dpll_regm4xen_recalc,
	.round_rate	= &omap4_dpll_regm4xen_round_rate,
	.set_rate	= &omap3_noncore_dpll_set_rate,
	.set_parent	= &omap3_noncore_dpll_set_parent,
	.set_rate_and_parent	= &omap3_noncore_dpll_set_rate_and_parent,
	.determine_rate	= &omap4_dpll_regm4xen_determine_rate,
	.get_parent	= &omap2_init_dpll_parent,
};
#else
static const struct clk_ops dpll_m4xen_ck_ops = {};
#endif

#if defined(CONFIG_ARCH_OMAP3) || defined(CONFIG_ARCH_OMAP4) || \
	defined(CONFIG_SOC_OMAP5) || defined(CONFIG_SOC_DRA7XX) || \
	defined(CONFIG_SOC_AM33XX) || defined(CONFIG_SOC_AM43XX)
static const struct clk_ops dpll_core_ck_ops = {
	.recalc_rate	= &omap3_dpll_recalc,
	.get_parent	= &omap2_init_dpll_parent,
};

static const struct clk_ops dpll_ck_ops = {
	.enable		= &omap3_noncore_dpll_enable,
	.disable	= &omap3_noncore_dpll_disable,
	.recalc_rate	= &omap3_dpll_recalc,
	.round_rate	= &omap2_dpll_round_rate,
	.set_rate	= &omap3_noncore_dpll_set_rate,
	.set_parent	= &omap3_noncore_dpll_set_parent,
	.set_rate_and_parent	= &omap3_noncore_dpll_set_rate_and_parent,
	.determine_rate	= &omap3_noncore_dpll_determine_rate,
	.get_parent	= &omap2_init_dpll_parent,
};

static const struct clk_ops dpll_no_gate_ck_ops = {
	.recalc_rate	= &omap3_dpll_recalc,
	.get_parent	= &omap2_init_dpll_parent,
	.round_rate	= &omap2_dpll_round_rate,
	.set_rate	= &omap3_noncore_dpll_set_rate,
	.set_parent	= &omap3_noncore_dpll_set_parent,
	.set_rate_and_parent	= &omap3_noncore_dpll_set_rate_and_parent,
	.determine_rate	= &omap3_noncore_dpll_determine_rate,
};
#else
static const struct clk_ops dpll_core_ck_ops = {};
static const struct clk_ops dpll_ck_ops = {};
static const struct clk_ops dpll_no_gate_ck_ops = {};
const struct clk_hw_omap_ops clkhwops_omap3_dpll = {};
#endif

#ifdef CONFIG_ARCH_OMAP2
static const struct clk_ops omap2_dpll_core_ck_ops = {
	.get_parent	= &omap2_init_dpll_parent,
	.recalc_rate	= &omap2_dpllcore_recalc,
	.round_rate	= &omap2_dpll_round_rate,
	.set_rate	= &omap2_reprogram_dpllcore,
};
#else
static const struct clk_ops omap2_dpll_core_ck_ops = {};
#endif

#ifdef CONFIG_ARCH_OMAP3
static const struct clk_ops omap3_dpll_core_ck_ops = {
	.get_parent	= &omap2_init_dpll_parent,
	.recalc_rate	= &omap3_dpll_recalc,
	.round_rate	= &omap2_dpll_round_rate,
};
#else
static const struct clk_ops omap3_dpll_core_ck_ops = {};
#endif

#ifdef CONFIG_ARCH_OMAP3
static const struct clk_ops omap3_dpll_ck_ops = {
	.enable		= &omap3_noncore_dpll_enable,
	.disable	= &omap3_noncore_dpll_disable,
	.get_parent	= &omap2_init_dpll_parent,
	.recalc_rate	= &omap3_dpll_recalc,
	.set_rate	= &omap3_noncore_dpll_set_rate,
	.set_parent	= &omap3_noncore_dpll_set_parent,
	.set_rate_and_parent	= &omap3_noncore_dpll_set_rate_and_parent,
	.determine_rate	= &omap3_noncore_dpll_determine_rate,
	.round_rate	= &omap2_dpll_round_rate,
};

static const struct clk_ops omap3_dpll5_ck_ops = {
	.enable		= &omap3_noncore_dpll_enable,
	.disable	= &omap3_noncore_dpll_disable,
	.get_parent	= &omap2_init_dpll_parent,
	.recalc_rate	= &omap3_dpll_recalc,
	.set_rate	= &omap3_dpll5_set_rate,
	.set_parent	= &omap3_noncore_dpll_set_parent,
	.set_rate_and_parent	= &omap3_noncore_dpll_set_rate_and_parent,
	.determine_rate	= &omap3_noncore_dpll_determine_rate,
	.round_rate	= &omap2_dpll_round_rate,
};

static const struct clk_ops omap3_dpll_per_ck_ops = {
	.enable		= &omap3_noncore_dpll_enable,
	.disable	= &omap3_noncore_dpll_disable,
	.get_parent	= &omap2_init_dpll_parent,
	.recalc_rate	= &omap3_dpll_recalc,
	.set_rate	= &omap3_dpll4_set_rate,
	.set_parent	= &omap3_noncore_dpll_set_parent,
	.set_rate_and_parent	= &omap3_dpll4_set_rate_and_parent,
	.determine_rate	= &omap3_noncore_dpll_determine_rate,
	.round_rate	= &omap2_dpll_round_rate,
};
#endif

static const struct clk_ops dpll_x2_ck_ops = {
	.recalc_rate	= &omap3_clkoutx2_recalc,
};

/**
 * _register_dpll - low level registration of a DPLL clock
 * @hw: hardware clock definition for the clock
 * @node: device node for the clock
 *
 * Finalizes DPLL registration process. In case a failure (clk-ref or
 * clk-bypass is missing), the clock is added to retry list and
 * the initialization is retried on later stage.
 */
static void __init _register_dpll(void *user,
				  struct device_node *node)
{
	struct clk_hw *hw = user;
	struct clk_hw_omap *clk_hw = to_clk_hw_omap(hw);
	struct dpll_data *dd = clk_hw->dpll_data;
	struct clk *clk;

	clk = of_clk_get(node, 0);
	if (IS_ERR(clk)) {
		pr_debug("clk-ref missing for %s, retry later\n",
			 node->name);
		if (!ti_clk_retry_init(node, hw, _register_dpll))
			return;

		goto cleanup;
	}

	dd->clk_ref = __clk_get_hw(clk);

	clk = of_clk_get(node, 1);

	if (IS_ERR(clk)) {
		pr_debug("clk-bypass missing for %s, retry later\n",
			 node->name);
		if (!ti_clk_retry_init(node, hw, _register_dpll))
			return;

		goto cleanup;
	}

	dd->clk_bypass = __clk_get_hw(clk);

	/* register the clock */
	clk = ti_clk_register(NULL, &clk_hw->hw, node->name);

	if (!IS_ERR(clk)) {
		omap2_init_clk_hw_omap_clocks(&clk_hw->hw);
		of_clk_add_provider(node, of_clk_src_simple_get, clk);
		kfree(clk_hw->hw.init->parent_names);
		kfree(clk_hw->hw.init);
		return;
	}

cleanup:
	kfree(clk_hw->dpll_data);
	kfree(clk_hw->hw.init->parent_names);
	kfree(clk_hw->hw.init);
	kfree(clk_hw);
}

#if defined(CONFIG_ARCH_OMAP3) && defined(CONFIG_ATAGS)
void _get_reg(u8 module, u16 offset, struct clk_omap_reg *reg)
{
	reg->index = module;
	reg->offset = offset;
}

struct clk *ti_clk_register_dpll(struct ti_clk *setup)
{
	struct clk_hw_omap *clk_hw;
	struct clk_init_data init = { NULL };
	struct dpll_data *dd;
	struct clk *clk;
	struct ti_clk_dpll *dpll;
	const struct clk_ops *ops = &omap3_dpll_ck_ops;
	struct clk *clk_ref;
	struct clk *clk_bypass;

	dpll = setup->data;

	if (dpll->num_parents < 2)
		return ERR_PTR(-EINVAL);

	clk_ref = clk_get_sys(NULL, dpll->parents[0]);
	clk_bypass = clk_get_sys(NULL, dpll->parents[1]);

	if (IS_ERR_OR_NULL(clk_ref) || IS_ERR_OR_NULL(clk_bypass))
		return ERR_PTR(-EAGAIN);

	dd = kzalloc(sizeof(*dd), GFP_KERNEL);
	clk_hw = kzalloc(sizeof(*clk_hw), GFP_KERNEL);
	if (!dd || !clk_hw) {
		clk = ERR_PTR(-ENOMEM);
		goto cleanup;
	}

	clk_hw->dpll_data = dd;
	clk_hw->ops = &clkhwops_omap3_dpll;
	clk_hw->hw.init = &init;

	init.name = setup->name;
	init.ops = ops;

	init.num_parents = dpll->num_parents;
	init.parent_names = dpll->parents;

	_get_reg(dpll->module, dpll->control_reg, &dd->control_reg);
	_get_reg(dpll->module, dpll->idlest_reg, &dd->idlest_reg);
	_get_reg(dpll->module, dpll->mult_div1_reg, &dd->mult_div1_reg);
	_get_reg(dpll->module, dpll->autoidle_reg, &dd->autoidle_reg);

	dd->modes = dpll->modes;
	dd->div1_mask = dpll->div1_mask;
	dd->idlest_mask = dpll->idlest_mask;
	dd->mult_mask = dpll->mult_mask;
	dd->autoidle_mask = dpll->autoidle_mask;
	dd->enable_mask = dpll->enable_mask;
	dd->sddiv_mask = dpll->sddiv_mask;
	dd->dco_mask = dpll->dco_mask;
	dd->max_divider = dpll->max_divider;
	dd->min_divider = dpll->min_divider;
	dd->max_multiplier = dpll->max_multiplier;
	dd->auto_recal_bit = dpll->auto_recal_bit;
	dd->recal_en_bit = dpll->recal_en_bit;
	dd->recal_st_bit = dpll->recal_st_bit;

	dd->clk_ref = __clk_get_hw(clk_ref);
	dd->clk_bypass = __clk_get_hw(clk_bypass);

	if (dpll->flags & CLKF_CORE)
		ops = &omap3_dpll_core_ck_ops;

	if (dpll->flags & CLKF_PER)
		ops = &omap3_dpll_per_ck_ops;

	if (dpll->flags & CLKF_J_TYPE)
		dd->flags |= DPLL_J_TYPE;

	clk = ti_clk_register(NULL, &clk_hw->hw, setup->name);

	if (!IS_ERR(clk))
		return clk;

cleanup:
	kfree(dd);
	kfree(clk_hw);
	return clk;
}
#endif

#if defined(CONFIG_ARCH_OMAP4) || defined(CONFIG_SOC_OMAP5) || \
	defined(CONFIG_SOC_DRA7XX) || defined(CONFIG_SOC_AM33XX) || \
	defined(CONFIG_SOC_AM43XX)
/**
 * _register_dpll_x2 - Registers a DPLLx2 clock
 * @node: device node for this clock
 * @ops: clk_ops for this clock
 * @hw_ops: clk_hw_ops for this clock
 *
 * Initializes a DPLL x 2 clock from device tree data.
 */
static void _register_dpll_x2(struct device_node *node,
			      const struct clk_ops *ops,
			      const struct clk_hw_omap_ops *hw_ops)
{
	struct clk *clk;
	struct clk_init_data init = { NULL };
	struct clk_hw_omap *clk_hw;
	const char *name = node->name;
	const char *parent_name;

	parent_name = of_clk_get_parent_name(node, 0);
	if (!parent_name) {
		pr_err("%s must have parent\n", node->name);
		return;
	}

	clk_hw = kzalloc(sizeof(*clk_hw), GFP_KERNEL);
	if (!clk_hw)
		return;

	clk_hw->ops = hw_ops;
	clk_hw->hw.init = &init;

	init.name = name;
	init.ops = ops;
	init.parent_names = &parent_name;
	init.num_parents = 1;

#if defined(CONFIG_ARCH_OMAP4) || defined(CONFIG_SOC_OMAP5) || \
	defined(CONFIG_SOC_DRA7XX)
	if (hw_ops == &clkhwops_omap4_dpllmx) {
		int ret;

		/* Check if register defined, if not, drop hw-ops */
		ret = of_property_count_elems_of_size(node, "reg", 1);
		if (ret <= 0) {
			clk_hw->ops = NULL;
		} else if (ti_clk_get_reg_addr(node, 0, &clk_hw->clksel_reg)) {
			kfree(clk_hw);
			return;
		}
	}
#endif

	/* register the clock */
	clk = ti_clk_register(NULL, &clk_hw->hw, name);

	if (IS_ERR(clk)) {
		kfree(clk_hw);
	} else {
		omap2_init_clk_hw_omap_clocks(&clk_hw->hw);
		of_clk_add_provider(node, of_clk_src_simple_get, clk);
	}
}
#endif

/**
 * of_ti_dpll_setup - Setup function for OMAP DPLL clocks
 * @node: device node containing the DPLL info
 * @ops: ops for the DPLL
 * @ddt: DPLL data template to use
 *
 * Initializes a DPLL clock from device tree data.
 */
static void __init of_ti_dpll_setup(struct device_node *node,
				    const struct clk_ops *ops,
				    const struct dpll_data *ddt)
{
	struct clk_hw_omap *clk_hw = NULL;
	struct clk_init_data *init = NULL;
	const char **parent_names = NULL;
	struct dpll_data *dd = NULL;
	u8 dpll_mode = 0;

	dd = kzalloc(sizeof(*dd), GFP_KERNEL);
	clk_hw = kzalloc(sizeof(*clk_hw), GFP_KERNEL);
	init = kzalloc(sizeof(*init), GFP_KERNEL);
	if (!dd || !clk_hw || !init)
		goto cleanup;

	memcpy(dd, ddt, sizeof(*dd));

	clk_hw->dpll_data = dd;
	clk_hw->ops = &clkhwops_omap3_dpll;
	clk_hw->hw.init = init;

	init->name = node->name;
	init->ops = ops;

	init->num_parents = of_clk_get_parent_count(node);
	if (!init->num_parents) {
		pr_err("%s must have parent(s)\n", node->name);
		goto cleanup;
	}

	parent_names = kzalloc(sizeof(char *) * init->num_parents, GFP_KERNEL);
	if (!parent_names)
		goto cleanup;

	of_clk_parent_fill(node, parent_names, init->num_parents);

	init->parent_names = parent_names;

	if (ti_clk_get_reg_addr(node, 0, &dd->control_reg))
		goto cleanup;

	/*
	 * Special case for OMAP2 DPLL, register order is different due to
	 * missing idlest_reg, also clkhwops is different. Detected from
	 * missing idlest_mask.
	 */
	if (!dd->idlest_mask) {
		if (ti_clk_get_reg_addr(node, 1, &dd->mult_div1_reg))
			goto cleanup;
#ifdef CONFIG_ARCH_OMAP2
		clk_hw->ops = &clkhwops_omap2xxx_dpll;
		omap2xxx_clkt_dpllcore_init(&clk_hw->hw);
#endif
	} else {
		if (ti_clk_get_reg_addr(node, 1, &dd->idlest_reg))
			goto cleanup;

		if (ti_clk_get_reg_addr(node, 2, &dd->mult_div1_reg))
			goto cleanup;
	}

	if (dd->autoidle_mask) {
		if (ti_clk_get_reg_addr(node, 3, &dd->autoidle_reg))
			goto cleanup;
	}

	if (of_property_read_bool(node, "ti,low-power-stop"))
		dpll_mode |= 1 << DPLL_LOW_POWER_STOP;

	if (of_property_read_bool(node, "ti,low-power-bypass"))
		dpll_mode |= 1 << DPLL_LOW_POWER_BYPASS;

	if (of_property_read_bool(node, "ti,lock"))
		dpll_mode |= 1 << DPLL_LOCKED;

	if (dpll_mode)
		dd->modes = dpll_mode;

	_register_dpll(&clk_hw->hw, node);
	return;

cleanup:
	kfree(dd);
	kfree(parent_names);
	kfree(init);
	kfree(clk_hw);
}

#if defined(CONFIG_ARCH_OMAP4) || defined(CONFIG_SOC_OMAP5) || \
	defined(CONFIG_SOC_DRA7XX)
static void __init of_ti_omap4_dpll_x2_setup(struct device_node *node)
{
	_register_dpll_x2(node, &dpll_x2_ck_ops, &clkhwops_omap4_dpllmx);
}
CLK_OF_DECLARE(ti_omap4_dpll_x2_clock, "ti,omap4-dpll-x2-clock",
	       of_ti_omap4_dpll_x2_setup);
#endif

#if defined(CONFIG_SOC_AM33XX) || defined(CONFIG_SOC_AM43XX)
static void __init of_ti_am3_dpll_x2_setup(struct device_node *node)
{
	_register_dpll_x2(node, &dpll_x2_ck_ops, NULL);
}
CLK_OF_DECLARE(ti_am3_dpll_x2_clock, "ti,am3-dpll-x2-clock",
	       of_ti_am3_dpll_x2_setup);
#endif

#ifdef CONFIG_ARCH_OMAP3
static void __init of_ti_omap3_dpll_setup(struct device_node *node)
{
	const struct dpll_data dd = {
		.idlest_mask = 0x1,
		.enable_mask = 0x7,
		.autoidle_mask = 0x7,
		.mult_mask = 0x7ff << 8,
		.div1_mask = 0x7f,
		.max_multiplier = 2047,
		.max_divider = 128,
		.min_divider = 1,
		.freqsel_mask = 0xf0,
		.modes = (1 << DPLL_LOW_POWER_BYPASS) | (1 << DPLL_LOCKED),
	};

	if ((of_machine_is_compatible("ti,omap3630") ||
	     of_machine_is_compatible("ti,omap36xx")) &&
	    !strcmp(node->name, "dpll5_ck"))
		of_ti_dpll_setup(node, &omap3_dpll5_ck_ops, &dd);
	else
		of_ti_dpll_setup(node, &omap3_dpll_ck_ops, &dd);
}
CLK_OF_DECLARE(ti_omap3_dpll_clock, "ti,omap3-dpll-clock",
	       of_ti_omap3_dpll_setup);

static void __init of_ti_omap3_core_dpll_setup(struct device_node *node)
{
	const struct dpll_data dd = {
		.idlest_mask = 0x1,
		.enable_mask = 0x7,
		.autoidle_mask = 0x7,
		.mult_mask = 0x7ff << 16,
		.div1_mask = 0x7f << 8,
		.max_multiplier = 2047,
		.max_divider = 128,
		.min_divider = 1,
		.freqsel_mask = 0xf0,
	};

	of_ti_dpll_setup(node, &omap3_dpll_core_ck_ops, &dd);
}
CLK_OF_DECLARE(ti_omap3_core_dpll_clock, "ti,omap3-dpll-core-clock",
	       of_ti_omap3_core_dpll_setup);

static void __init of_ti_omap3_per_dpll_setup(struct device_node *node)
{
	const struct dpll_data dd = {
		.idlest_mask = 0x1 << 1,
		.enable_mask = 0x7 << 16,
		.autoidle_mask = 0x7 << 3,
		.mult_mask = 0x7ff << 8,
		.div1_mask = 0x7f,
		.max_multiplier = 2047,
		.max_divider = 128,
		.min_divider = 1,
		.freqsel_mask = 0xf00000,
		.modes = (1 << DPLL_LOW_POWER_STOP) | (1 << DPLL_LOCKED),
	};

	of_ti_dpll_setup(node, &omap3_dpll_per_ck_ops, &dd);
}
CLK_OF_DECLARE(ti_omap3_per_dpll_clock, "ti,omap3-dpll-per-clock",
	       of_ti_omap3_per_dpll_setup);

static void __init of_ti_omap3_per_jtype_dpll_setup(struct device_node *node)
{
	const struct dpll_data dd = {
		.idlest_mask = 0x1 << 1,
		.enable_mask = 0x7 << 16,
		.autoidle_mask = 0x7 << 3,
		.mult_mask = 0xfff << 8,
		.div1_mask = 0x7f,
		.max_multiplier = 4095,
		.max_divider = 128,
		.min_divider = 1,
		.sddiv_mask = 0xff << 24,
		.dco_mask = 0xe << 20,
		.flags = DPLL_J_TYPE,
		.modes = (1 << DPLL_LOW_POWER_STOP) | (1 << DPLL_LOCKED),
	};

	of_ti_dpll_setup(node, &omap3_dpll_per_ck_ops, &dd);
}
CLK_OF_DECLARE(ti_omap3_per_jtype_dpll_clock, "ti,omap3-dpll-per-j-type-clock",
	       of_ti_omap3_per_jtype_dpll_setup);
#endif

static void __init of_ti_omap4_dpll_setup(struct device_node *node)
{
	const struct dpll_data dd = {
		.idlest_mask = 0x1,
		.enable_mask = 0x7,
		.autoidle_mask = 0x7,
		.mult_mask = 0x7ff << 8,
		.div1_mask = 0x7f,
		.max_multiplier = 2047,
		.max_divider = 128,
		.min_divider = 1,
		.modes = (1 << DPLL_LOW_POWER_BYPASS) | (1 << DPLL_LOCKED),
	};

	of_ti_dpll_setup(node, &dpll_ck_ops, &dd);
}
CLK_OF_DECLARE(ti_omap4_dpll_clock, "ti,omap4-dpll-clock",
	       of_ti_omap4_dpll_setup);

static void __init of_ti_omap5_mpu_dpll_setup(struct device_node *node)
{
	const struct dpll_data dd = {
		.idlest_mask = 0x1,
		.enable_mask = 0x7,
		.autoidle_mask = 0x7,
		.mult_mask = 0x7ff << 8,
		.div1_mask = 0x7f,
		.max_multiplier = 2047,
		.max_divider = 128,
		.dcc_mask = BIT(22),
		.dcc_rate = 1400000000, /* DCC beyond 1.4GHz */
		.min_divider = 1,
		.modes = (1 << DPLL_LOW_POWER_BYPASS) | (1 << DPLL_LOCKED),
	};

	of_ti_dpll_setup(node, &dpll_ck_ops, &dd);
}
CLK_OF_DECLARE(of_ti_omap5_mpu_dpll_clock, "ti,omap5-mpu-dpll-clock",
	       of_ti_omap5_mpu_dpll_setup);

static void __init of_ti_omap4_core_dpll_setup(struct device_node *node)
{
	const struct dpll_data dd = {
		.idlest_mask = 0x1,
		.enable_mask = 0x7,
		.autoidle_mask = 0x7,
		.mult_mask = 0x7ff << 8,
		.div1_mask = 0x7f,
		.max_multiplier = 2047,
		.max_divider = 128,
		.min_divider = 1,
		.modes = (1 << DPLL_LOW_POWER_BYPASS) | (1 << DPLL_LOCKED),
	};

	of_ti_dpll_setup(node, &dpll_core_ck_ops, &dd);
}
CLK_OF_DECLARE(ti_omap4_core_dpll_clock, "ti,omap4-dpll-core-clock",
	       of_ti_omap4_core_dpll_setup);

#if defined(CONFIG_ARCH_OMAP4) || defined(CONFIG_SOC_OMAP5) || \
	defined(CONFIG_SOC_DRA7XX)
static void __init of_ti_omap4_m4xen_dpll_setup(struct device_node *node)
{
	const struct dpll_data dd = {
		.idlest_mask = 0x1,
		.enable_mask = 0x7,
		.autoidle_mask = 0x7,
		.mult_mask = 0x7ff << 8,
		.div1_mask = 0x7f,
		.max_multiplier = 2047,
		.max_divider = 128,
		.min_divider = 1,
		.m4xen_mask = 0x800,
		.lpmode_mask = 1 << 10,
		.modes = (1 << DPLL_LOW_POWER_BYPASS) | (1 << DPLL_LOCKED),
	};

	of_ti_dpll_setup(node, &dpll_m4xen_ck_ops, &dd);
}
CLK_OF_DECLARE(ti_omap4_m4xen_dpll_clock, "ti,omap4-dpll-m4xen-clock",
	       of_ti_omap4_m4xen_dpll_setup);

static void __init of_ti_omap4_jtype_dpll_setup(struct device_node *node)
{
	const struct dpll_data dd = {
		.idlest_mask = 0x1,
		.enable_mask = 0x7,
		.autoidle_mask = 0x7,
		.mult_mask = 0xfff << 8,
		.div1_mask = 0xff,
		.max_multiplier = 4095,
		.max_divider = 256,
		.min_divider = 1,
		.sddiv_mask = 0xff << 24,
		.flags = DPLL_J_TYPE,
		.modes = (1 << DPLL_LOW_POWER_BYPASS) | (1 << DPLL_LOCKED),
	};

	of_ti_dpll_setup(node, &dpll_m4xen_ck_ops, &dd);
}
CLK_OF_DECLARE(ti_omap4_jtype_dpll_clock, "ti,omap4-dpll-j-type-clock",
	       of_ti_omap4_jtype_dpll_setup);
#endif

static void __init of_ti_am3_no_gate_dpll_setup(struct device_node *node)
{
	const struct dpll_data dd = {
		.idlest_mask = 0x1,
		.enable_mask = 0x7,
		.mult_mask = 0x7ff << 8,
		.div1_mask = 0x7f,
		.max_multiplier = 2047,
		.max_divider = 128,
		.min_divider = 1,
		.max_rate = 1000000000,
		.modes = (1 << DPLL_LOW_POWER_BYPASS) | (1 << DPLL_LOCKED),
	};

	of_ti_dpll_setup(node, &dpll_no_gate_ck_ops, &dd);
}
CLK_OF_DECLARE(ti_am3_no_gate_dpll_clock, "ti,am3-dpll-no-gate-clock",
	       of_ti_am3_no_gate_dpll_setup);

static void __init of_ti_am3_jtype_dpll_setup(struct device_node *node)
{
	const struct dpll_data dd = {
		.idlest_mask = 0x1,
		.enable_mask = 0x7,
		.mult_mask = 0x7ff << 8,
		.div1_mask = 0x7f,
		.max_multiplier = 4095,
		.max_divider = 256,
		.min_divider = 2,
		.flags = DPLL_J_TYPE,
		.max_rate = 2000000000,
		.modes = (1 << DPLL_LOW_POWER_BYPASS) | (1 << DPLL_LOCKED),
	};

	of_ti_dpll_setup(node, &dpll_ck_ops, &dd);
}
CLK_OF_DECLARE(ti_am3_jtype_dpll_clock, "ti,am3-dpll-j-type-clock",
	       of_ti_am3_jtype_dpll_setup);

static void __init of_ti_am3_no_gate_jtype_dpll_setup(struct device_node *node)
{
	const struct dpll_data dd = {
		.idlest_mask = 0x1,
		.enable_mask = 0x7,
		.mult_mask = 0x7ff << 8,
		.div1_mask = 0x7f,
		.max_multiplier = 2047,
		.max_divider = 128,
		.min_divider = 1,
		.max_rate = 2000000000,
		.flags = DPLL_J_TYPE,
		.modes = (1 << DPLL_LOW_POWER_BYPASS) | (1 << DPLL_LOCKED),
	};

	of_ti_dpll_setup(node, &dpll_no_gate_ck_ops, &dd);
}
CLK_OF_DECLARE(ti_am3_no_gate_jtype_dpll_clock,
	       "ti,am3-dpll-no-gate-j-type-clock",
	       of_ti_am3_no_gate_jtype_dpll_setup);

static void __init of_ti_am3_dpll_setup(struct device_node *node)
{
	const struct dpll_data dd = {
		.idlest_mask = 0x1,
		.enable_mask = 0x7,
		.mult_mask = 0x7ff << 8,
		.div1_mask = 0x7f,
		.max_multiplier = 2047,
		.max_divider = 128,
		.min_divider = 1,
		.max_rate = 1000000000,
		.modes = (1 << DPLL_LOW_POWER_BYPASS) | (1 << DPLL_LOCKED),
	};

	of_ti_dpll_setup(node, &dpll_ck_ops, &dd);
}
CLK_OF_DECLARE(ti_am3_dpll_clock, "ti,am3-dpll-clock", of_ti_am3_dpll_setup);

static void __init of_ti_am3_core_dpll_setup(struct device_node *node)
{
	const struct dpll_data dd = {
		.idlest_mask = 0x1,
		.enable_mask = 0x7,
		.mult_mask = 0x7ff << 8,
		.div1_mask = 0x7f,
		.max_multiplier = 2047,
		.max_divider = 128,
		.min_divider = 1,
		.max_rate = 1000000000,
		.modes = (1 << DPLL_LOW_POWER_BYPASS) | (1 << DPLL_LOCKED),
	};

	of_ti_dpll_setup(node, &dpll_core_ck_ops, &dd);
}
CLK_OF_DECLARE(ti_am3_core_dpll_clock, "ti,am3-dpll-core-clock",
	       of_ti_am3_core_dpll_setup);

static void __init of_ti_omap2_core_dpll_setup(struct device_node *node)
{
	const struct dpll_data dd = {
		.enable_mask = 0x3,
		.mult_mask = 0x3ff << 12,
		.div1_mask = 0xf << 8,
		.max_divider = 16,
		.min_divider = 1,
	};

	of_ti_dpll_setup(node, &omap2_dpll_core_ck_ops, &dd);
}
CLK_OF_DECLARE(ti_omap2_core_dpll_clock, "ti,omap2-dpll-core-clock",
	       of_ti_omap2_core_dpll_setup);
