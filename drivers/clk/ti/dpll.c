// SPDX-License-Identifier: GPL-2.0-only
/*
 * OMAP DPLL clock support
 *
 * Copyright (C) 2013 Texas Instruments, Inc.
 *
 * Tero Kristo <t-kristo@ti.com>
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
	.set_rate	= &omap3_noncore_dpll_set_rate,
	.set_parent	= &omap3_noncore_dpll_set_parent,
	.set_rate_and_parent	= &omap3_noncore_dpll_set_rate_and_parent,
	.determine_rate	= &omap4_dpll_regm4xen_determine_rate,
	.get_parent	= &omap2_init_dpll_parent,
	.save_context	= &omap3_core_dpll_save_context,
	.restore_context = &omap3_core_dpll_restore_context,
};
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
	.set_rate	= &omap3_noncore_dpll_set_rate,
	.set_parent	= &omap3_noncore_dpll_set_parent,
	.set_rate_and_parent	= &omap3_noncore_dpll_set_rate_and_parent,
	.determine_rate	= &omap3_noncore_dpll_determine_rate,
	.get_parent	= &omap2_init_dpll_parent,
	.save_context	= &omap3_noncore_dpll_save_context,
	.restore_context = &omap3_noncore_dpll_restore_context,
};

static const struct clk_ops dpll_no_gate_ck_ops = {
	.recalc_rate	= &omap3_dpll_recalc,
	.get_parent	= &omap2_init_dpll_parent,
	.set_rate	= &omap3_noncore_dpll_set_rate,
	.set_parent	= &omap3_noncore_dpll_set_parent,
	.set_rate_and_parent	= &omap3_noncore_dpll_set_rate_and_parent,
	.determine_rate	= &omap3_noncore_dpll_determine_rate,
	.save_context	= &omap3_noncore_dpll_save_context,
	.restore_context = &omap3_noncore_dpll_restore_context
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
	.determine_rate	= &omap2_dpll_determine_rate,
	.set_rate	= &omap2_reprogram_dpllcore,
};
#else
static const struct clk_ops omap2_dpll_core_ck_ops = {};
#endif

#ifdef CONFIG_ARCH_OMAP3
static const struct clk_ops omap3_dpll_core_ck_ops = {
	.get_parent	= &omap2_init_dpll_parent,
	.recalc_rate	= &omap3_dpll_recalc,
	.determine_rate	= &omap2_dpll_determine_rate,
};

static const struct clk_ops omap3_dpll_ck_ops = {
	.enable		= &omap3_noncore_dpll_enable,
	.disable	= &omap3_noncore_dpll_disable,
	.get_parent	= &omap2_init_dpll_parent,
	.recalc_rate	= &omap3_dpll_recalc,
	.set_rate	= &omap3_noncore_dpll_set_rate,
	.set_parent	= &omap3_noncore_dpll_set_parent,
	.set_rate_and_parent	= &omap3_noncore_dpll_set_rate_and_parent,
	.determine_rate	= &omap3_noncore_dpll_determine_rate,
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
};
#endif

#if defined(CONFIG_ARCH_OMAP4) || defined(CONFIG_SOC_OMAP5) || \
        defined(CONFIG_SOC_DRA7XX) || defined(CONFIG_SOC_AM33XX) || \
	defined(CONFIG_SOC_AM43XX)
static const struct clk_ops dpll_x2_ck_ops = {
	.recalc_rate	= &omap3_clkoutx2_recalc,
};
#endif

/**
 * _register_dpll - low level registration of a DPLL clock
 * @user: pointer to the hardware clock definition for the clock
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
	const char *name;
	struct clk *clk;
	const struct clk_init_data *init = hw->init;

	clk = of_clk_get(node, 0);
	if (IS_ERR(clk)) {
		pr_debug("clk-ref missing for %pOFn, retry later\n",
			 node);
		if (!ti_clk_retry_init(node, hw, _register_dpll))
			return;

		goto cleanup;
	}

	dd->clk_ref = __clk_get_hw(clk);

	clk = of_clk_get(node, 1);

	if (IS_ERR(clk)) {
		pr_debug("clk-bypass missing for %pOFn, retry later\n",
			 node);
		if (!ti_clk_retry_init(node, hw, _register_dpll))
			return;

		goto cleanup;
	}

	dd->clk_bypass = __clk_get_hw(clk);

	/* register the clock */
	name = ti_dt_clk_name(node);
	clk = of_ti_clk_register_omap_hw(node, &clk_hw->hw, name);

	if (!IS_ERR(clk)) {
		of_clk_add_provider(node, of_clk_src_simple_get, clk);
		kfree(init->parent_names);
		kfree(init);
		return;
	}

cleanup:
	kfree(clk_hw->dpll_data);
	kfree(init->parent_names);
	kfree(init);
	kfree(clk_hw);
}

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
	const char *name = ti_dt_clk_name(node);
	const char *parent_name;

	parent_name = of_clk_get_parent_name(node, 0);
	if (!parent_name) {
		pr_err("%pOFn must have parent\n", node);
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
	clk = of_ti_clk_register_omap_hw(node, &clk_hw->hw, name);

	if (IS_ERR(clk))
		kfree(clk_hw);
	else
		of_clk_add_provider(node, of_clk_src_simple_get, clk);
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
	int ssc_clk_index;
	u8 dpll_mode = 0;
	u32 min_div;

	dd = kmemdup(ddt, sizeof(*dd), GFP_KERNEL);
	clk_hw = kzalloc(sizeof(*clk_hw), GFP_KERNEL);
	init = kzalloc(sizeof(*init), GFP_KERNEL);
	if (!dd || !clk_hw || !init)
		goto cleanup;

	clk_hw->dpll_data = dd;
	clk_hw->ops = &clkhwops_omap3_dpll;
	clk_hw->hw.init = init;

	init->name = ti_dt_clk_name(node);
	init->ops = ops;

	init->num_parents = of_clk_get_parent_count(node);
	if (!init->num_parents) {
		pr_err("%pOFn must have parent(s)\n", node);
		goto cleanup;
	}

	parent_names = kcalloc(init->num_parents, sizeof(char *), GFP_KERNEL);
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

		ssc_clk_index = 4;
	} else {
		ssc_clk_index = 3;
	}

	if (dd->ssc_deltam_int_mask && dd->ssc_deltam_frac_mask &&
	    dd->ssc_modfreq_mant_mask && dd->ssc_modfreq_exp_mask) {
		if (ti_clk_get_reg_addr(node, ssc_clk_index++,
					&dd->ssc_deltam_reg))
			goto cleanup;

		if (ti_clk_get_reg_addr(node, ssc_clk_index++,
					&dd->ssc_modfreq_reg))
			goto cleanup;

		of_property_read_u32(node, "ti,ssc-modfreq-hz",
				     &dd->ssc_modfreq);
		of_property_read_u32(node, "ti,ssc-deltam", &dd->ssc_deltam);
		dd->ssc_downspread =
			of_property_read_bool(node, "ti,ssc-downspread");
	}

	if (of_property_read_bool(node, "ti,low-power-stop"))
		dpll_mode |= 1 << DPLL_LOW_POWER_STOP;

	if (of_property_read_bool(node, "ti,low-power-bypass"))
		dpll_mode |= 1 << DPLL_LOW_POWER_BYPASS;

	if (of_property_read_bool(node, "ti,lock"))
		dpll_mode |= 1 << DPLL_LOCKED;

	if (!of_property_read_u32(node, "ti,min-div", &min_div) &&
	    min_div > dd->min_divider)
		dd->min_divider = min_div;

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
	     of_node_name_eq(node, "dpll5_ck"))
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
		.ssc_enable_mask = 0x1 << 12,
		.ssc_downspread_mask = 0x1 << 14,
		.mult_mask = 0x7ff << 8,
		.div1_mask = 0x7f,
		.ssc_deltam_int_mask = 0x3 << 18,
		.ssc_deltam_frac_mask = 0x3ffff,
		.ssc_modfreq_mant_mask = 0x7f,
		.ssc_modfreq_exp_mask = 0x7 << 8,
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
		.ssc_enable_mask = 0x1 << 12,
		.ssc_downspread_mask = 0x1 << 14,
		.mult_mask = 0x7ff << 8,
		.div1_mask = 0x7f,
		.ssc_deltam_int_mask = 0x3 << 18,
		.ssc_deltam_frac_mask = 0x3ffff,
		.ssc_modfreq_mant_mask = 0x7f,
		.ssc_modfreq_exp_mask = 0x7 << 8,
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
