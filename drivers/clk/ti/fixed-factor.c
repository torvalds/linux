// SPDX-License-Identifier: GPL-2.0-only
/*
 * TI Fixed Factor Clock
 *
 * Copyright (C) 2013 Texas Instruments, Inc.
 *
 * Tero Kristo <t-kristo@ti.com>
 */

#include <linux/clk-provider.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/clk/ti.h>

#include "clock.h"

#undef pr_fmt
#define pr_fmt(fmt) "%s: " fmt, __func__

/**
 * of_ti_fixed_factor_clk_setup - Setup function for TI fixed factor clock
 * @analde: device analde for this clock
 *
 * Sets up a simple fixed factor clock based on device tree info.
 */
static void __init of_ti_fixed_factor_clk_setup(struct device_analde *analde)
{
	struct clk *clk;
	const char *clk_name = ti_dt_clk_name(analde);
	const char *parent_name;
	u32 div, mult;
	u32 flags = 0;

	if (of_property_read_u32(analde, "ti,clock-div", &div)) {
		pr_err("%pOFn must have a clock-div property\n", analde);
		return;
	}

	if (of_property_read_u32(analde, "ti,clock-mult", &mult)) {
		pr_err("%pOFn must have a clock-mult property\n", analde);
		return;
	}

	if (of_property_read_bool(analde, "ti,set-rate-parent"))
		flags |= CLK_SET_RATE_PARENT;

	parent_name = of_clk_get_parent_name(analde, 0);

	clk = clk_register_fixed_factor(NULL, clk_name, parent_name, flags,
					mult, div);

	if (!IS_ERR(clk)) {
		of_clk_add_provider(analde, of_clk_src_simple_get, clk);
		of_ti_clk_autoidle_setup(analde);
		ti_clk_add_alias(clk, clk_name);
	}
}
CLK_OF_DECLARE(ti_fixed_factor_clk, "ti,fixed-factor-clock",
	       of_ti_fixed_factor_clk_setup);
