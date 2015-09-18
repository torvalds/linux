/*
 * OMAP interface clock support
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

#include <linux/clk-provider.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/clk/ti.h>
#include "clock.h"

#undef pr_fmt
#define pr_fmt(fmt) "%s: " fmt, __func__

static const struct clk_ops ti_interface_clk_ops = {
	.init		= &omap2_init_clk_clkdm,
	.enable		= &omap2_dflt_clk_enable,
	.disable	= &omap2_dflt_clk_disable,
	.is_enabled	= &omap2_dflt_clk_is_enabled,
};

static struct clk *_register_interface(struct device *dev, const char *name,
				       const char *parent_name,
				       void __iomem *reg, u8 bit_idx,
				       const struct clk_hw_omap_ops *ops)
{
	struct clk_init_data init = { NULL };
	struct clk_hw_omap *clk_hw;
	struct clk *clk;

	clk_hw = kzalloc(sizeof(*clk_hw), GFP_KERNEL);
	if (!clk_hw)
		return ERR_PTR(-ENOMEM);

	clk_hw->hw.init = &init;
	clk_hw->ops = ops;
	clk_hw->flags = MEMMAP_ADDRESSING;
	clk_hw->enable_reg = reg;
	clk_hw->enable_bit = bit_idx;

	init.name = name;
	init.ops = &ti_interface_clk_ops;
	init.flags = 0;

	init.num_parents = 1;
	init.parent_names = &parent_name;

	clk = clk_register(NULL, &clk_hw->hw);

	if (IS_ERR(clk))
		kfree(clk_hw);
	else
		omap2_init_clk_hw_omap_clocks(&clk_hw->hw);

	return clk;
}

#if defined(CONFIG_ARCH_OMAP3) && defined(CONFIG_ATAGS)
struct clk *ti_clk_register_interface(struct ti_clk *setup)
{
	const struct clk_hw_omap_ops *ops = &clkhwops_iclk_wait;
	u32 reg;
	struct clk_omap_reg *reg_setup;
	struct ti_clk_gate *gate;

	gate = setup->data;
	reg_setup = (struct clk_omap_reg *)&reg;
	reg_setup->index = gate->module;
	reg_setup->offset = gate->reg;

	if (gate->flags & CLKF_NO_WAIT)
		ops = &clkhwops_iclk;

	if (gate->flags & CLKF_HSOTGUSB)
		ops = &clkhwops_omap3430es2_iclk_hsotgusb_wait;

	if (gate->flags & CLKF_DSS)
		ops = &clkhwops_omap3430es2_iclk_dss_usbhost_wait;

	if (gate->flags & CLKF_SSI)
		ops = &clkhwops_omap3430es2_iclk_ssi_wait;

	if (gate->flags & CLKF_AM35XX)
		ops = &clkhwops_am35xx_ipss_wait;

	return _register_interface(NULL, setup->name, gate->parent,
				   (void __iomem *)reg, gate->bit_shift, ops);
}
#endif

static void __init _of_ti_interface_clk_setup(struct device_node *node,
					      const struct clk_hw_omap_ops *ops)
{
	struct clk *clk;
	const char *parent_name;
	void __iomem *reg;
	u8 enable_bit = 0;
	u32 val;

	reg = ti_clk_get_reg_addr(node, 0);
	if (IS_ERR(reg))
		return;

	if (!of_property_read_u32(node, "ti,bit-shift", &val))
		enable_bit = val;

	parent_name = of_clk_get_parent_name(node, 0);
	if (!parent_name) {
		pr_err("%s must have a parent\n", node->name);
		return;
	}

	clk = _register_interface(NULL, node->name, parent_name, reg,
				  enable_bit, ops);

	if (!IS_ERR(clk))
		of_clk_add_provider(node, of_clk_src_simple_get, clk);
}

static void __init of_ti_interface_clk_setup(struct device_node *node)
{
	_of_ti_interface_clk_setup(node, &clkhwops_iclk_wait);
}
CLK_OF_DECLARE(ti_interface_clk, "ti,omap3-interface-clock",
	       of_ti_interface_clk_setup);

static void __init of_ti_no_wait_interface_clk_setup(struct device_node *node)
{
	_of_ti_interface_clk_setup(node, &clkhwops_iclk);
}
CLK_OF_DECLARE(ti_no_wait_interface_clk, "ti,omap3-no-wait-interface-clock",
	       of_ti_no_wait_interface_clk_setup);

#ifdef CONFIG_ARCH_OMAP3
static void __init of_ti_hsotgusb_interface_clk_setup(struct device_node *node)
{
	_of_ti_interface_clk_setup(node,
				   &clkhwops_omap3430es2_iclk_hsotgusb_wait);
}
CLK_OF_DECLARE(ti_hsotgusb_interface_clk, "ti,omap3-hsotgusb-interface-clock",
	       of_ti_hsotgusb_interface_clk_setup);

static void __init of_ti_dss_interface_clk_setup(struct device_node *node)
{
	_of_ti_interface_clk_setup(node,
				   &clkhwops_omap3430es2_iclk_dss_usbhost_wait);
}
CLK_OF_DECLARE(ti_dss_interface_clk, "ti,omap3-dss-interface-clock",
	       of_ti_dss_interface_clk_setup);

static void __init of_ti_ssi_interface_clk_setup(struct device_node *node)
{
	_of_ti_interface_clk_setup(node, &clkhwops_omap3430es2_iclk_ssi_wait);
}
CLK_OF_DECLARE(ti_ssi_interface_clk, "ti,omap3-ssi-interface-clock",
	       of_ti_ssi_interface_clk_setup);

static void __init of_ti_am35xx_interface_clk_setup(struct device_node *node)
{
	_of_ti_interface_clk_setup(node, &clkhwops_am35xx_ipss_wait);
}
CLK_OF_DECLARE(ti_am35xx_interface_clk, "ti,am35xx-interface-clock",
	       of_ti_am35xx_interface_clk_setup);
#endif

#ifdef CONFIG_SOC_OMAP2430
static void __init of_ti_omap2430_interface_clk_setup(struct device_node *node)
{
	_of_ti_interface_clk_setup(node, &clkhwops_omap2430_i2chs_wait);
}
CLK_OF_DECLARE(ti_omap2430_interface_clk, "ti,omap2430-interface-clock",
	       of_ti_omap2430_interface_clk_setup);
#endif
