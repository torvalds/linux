// SPDX-License-Identifier: GPL-2.0
/*
 * H8/300 divide clock driver
 *
 * Copyright 2015 Yoshinori Sato <ysato@users.sourceforge.jp>
 */

#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>

static DEFINE_SPINLOCK(clklock);

static void __init h8300_div_clk_setup(struct device_node *node)
{
	unsigned int num_parents;
	struct clk_hw *hw;
	const char *clk_name = node->name;
	const char *parent_name;
	void __iomem *divcr = NULL;
	int width;
	int offset;

	num_parents = of_clk_get_parent_count(node);
	if (!num_parents) {
		pr_err("%s: no parent found\n", clk_name);
		return;
	}

	divcr = of_iomap(node, 0);
	if (divcr == NULL) {
		pr_err("%s: failed to map divide register\n", clk_name);
		goto error;
	}
	offset = (unsigned long)divcr & 3;
	offset = (3 - offset) * 8;
	divcr = (void __iomem *)((unsigned long)divcr & ~3);

	parent_name = of_clk_get_parent_name(node, 0);
	of_property_read_u32(node, "renesas,width", &width);
	hw = clk_hw_register_divider(NULL, clk_name, parent_name,
				   CLK_SET_RATE_GATE, divcr, offset, width,
				   CLK_DIVIDER_POWER_OF_TWO, &clklock);
	if (!IS_ERR(hw)) {
		of_clk_add_hw_provider(node, of_clk_hw_simple_get, hw);
		return;
	}
	pr_err("%s: failed to register %s div clock (%ld)\n",
	       __func__, clk_name, PTR_ERR(hw));
error:
	if (divcr)
		iounmap(divcr);
}

CLK_OF_DECLARE(h8300_div_clk, "renesas,h8300-div-clock", h8300_div_clk_setup);
