/*
 * Copyright 2013 Emilio López
 * Emilio López <emilio@elopez.com.ar>
 *
 * Copyright 2013 Chen-Yu Tsai
 * Chen-Yu Tsai <wens@csie.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/clk-provider.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/slab.h>

static DEFINE_SPINLOCK(gmac_lock);

/**
 * sun7i_a20_gmac_clk_setup - Setup function for A20/A31 GMAC clock module
 *
 * This clock looks something like this
 *                               ________________________
 *  MII TX clock from PHY >-----|___________    _________|----> to GMAC core
 *  GMAC Int. RGMII TX clk >----|___________\__/__gate---|----> to PHY
 *  Ext. 125MHz RGMII TX clk >--|__divider__/            |
 *                              |________________________|
 *
 * The external 125 MHz reference is optional, i.e. GMAC can use its
 * internal TX clock just fine. The A31 GMAC clock module does not have
 * the divider controls for the external reference.
 *
 * To keep it simple, let the GMAC use either the MII TX clock for MII mode,
 * and its internal TX clock for GMII and RGMII modes. The GMAC driver should
 * select the appropriate source and gate/ungate the output to the PHY.
 *
 * Only the GMAC should use this clock. Altering the clock so that it doesn't
 * match the GMAC's operation parameters will result in the GMAC not being
 * able to send traffic out. The GMAC driver should set the clock rate and
 * enable/disable this clock to configure the required state. The clock
 * driver then responds by auto-reparenting the clock.
 */

#define SUN7I_A20_GMAC_GPIT	2
#define SUN7I_A20_GMAC_MASK	0x3
#define SUN7I_A20_GMAC_PARENTS	2

static u32 sun7i_a20_gmac_mux_table[SUN7I_A20_GMAC_PARENTS] = {
	0x00, /* Select mii_phy_tx_clk */
	0x02, /* Select gmac_int_tx_clk */
};

static void __init sun7i_a20_gmac_clk_setup(struct device_node *node)
{
	struct clk *clk;
	struct clk_mux *mux;
	struct clk_gate *gate;
	const char *clk_name = node->name;
	const char *parents[SUN7I_A20_GMAC_PARENTS];
	void __iomem *reg;

	if (of_property_read_string(node, "clock-output-names", &clk_name))
		return;

	/* allocate mux and gate clock structs */
	mux = kzalloc(sizeof(struct clk_mux), GFP_KERNEL);
	if (!mux)
		return;

	gate = kzalloc(sizeof(struct clk_gate), GFP_KERNEL);
	if (!gate)
		goto free_mux;

	/* gmac clock requires exactly 2 parents */
	if (of_clk_parent_fill(node, parents, 2) != 2)
		goto free_gate;

	reg = of_iomap(node, 0);
	if (!reg)
		goto free_gate;

	/* set up gate and fixed rate properties */
	gate->reg = reg;
	gate->bit_idx = SUN7I_A20_GMAC_GPIT;
	gate->lock = &gmac_lock;
	mux->reg = reg;
	mux->mask = SUN7I_A20_GMAC_MASK;
	mux->table = sun7i_a20_gmac_mux_table;
	mux->lock = &gmac_lock;

	clk = clk_register_composite(NULL, clk_name,
			parents, SUN7I_A20_GMAC_PARENTS,
			&mux->hw, &clk_mux_ops,
			NULL, NULL,
			&gate->hw, &clk_gate_ops,
			0);

	if (IS_ERR(clk))
		goto iounmap_reg;

	of_clk_add_provider(node, of_clk_src_simple_get, clk);

	return;

iounmap_reg:
	iounmap(reg);
free_gate:
	kfree(gate);
free_mux:
	kfree(mux);
}
CLK_OF_DECLARE(sun7i_a20_gmac, "allwinner,sun7i-a20-gmac-clk",
		sun7i_a20_gmac_clk_setup);
