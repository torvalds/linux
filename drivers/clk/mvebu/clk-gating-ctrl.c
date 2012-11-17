/*
 * Marvell MVEBU clock gating control.
 *
 * Sebastian Hesselbarth <sebastian.hesselbarth@gmail.com>
 * Andrew Lunn <andrew@lunn.ch>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */
#include <linux/kernel.h>
#include <linux/bitops.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/clk-provider.h>
#include <linux/clk/mvebu.h>
#include <linux/of.h>
#include <linux/of_address.h>

struct mvebu_gating_ctrl {
	spinlock_t lock;
	struct clk **gates;
	int num_gates;
};

struct mvebu_soc_descr {
	const char *name;
	const char *parent;
	int bit_idx;
};

#define to_clk_gate(_hw) container_of(_hw, struct clk_gate, hw)

static struct clk __init *mvebu_clk_gating_get_src(
	struct of_phandle_args *clkspec, void *data)
{
	struct mvebu_gating_ctrl *ctrl = (struct mvebu_gating_ctrl *)data;
	int n;

	if (clkspec->args_count < 1)
		return ERR_PTR(-EINVAL);

	for (n = 0; n < ctrl->num_gates; n++) {
		struct clk_gate *gate =
			to_clk_gate(__clk_get_hw(ctrl->gates[n]));
		if (clkspec->args[0] == gate->bit_idx)
			return ctrl->gates[n];
	}
	return ERR_PTR(-ENODEV);
}

static void __init mvebu_clk_gating_setup(
	struct device_node *np, const struct mvebu_soc_descr *descr)
{
	struct mvebu_gating_ctrl *ctrl;
	struct clk *clk;
	void __iomem *base;
	const char *default_parent = NULL;
	int n;

	base = of_iomap(np, 0);

	clk = of_clk_get(np, 0);
	if (!IS_ERR(clk)) {
		default_parent = __clk_get_name(clk);
		clk_put(clk);
	}

	ctrl = kzalloc(sizeof(struct mvebu_gating_ctrl), GFP_KERNEL);
	if (WARN_ON(!ctrl))
		return;

	spin_lock_init(&ctrl->lock);

	/*
	 * Count, allocate, and register clock gates
	 */
	for (n = 0; descr[n].name;)
		n++;

	ctrl->num_gates = n;
	ctrl->gates = kzalloc(ctrl->num_gates * sizeof(struct clk *),
			      GFP_KERNEL);
	if (WARN_ON(!ctrl->gates)) {
		kfree(ctrl);
		return;
	}

	for (n = 0; n < ctrl->num_gates; n++) {
		const char *parent =
			(descr[n].parent) ? descr[n].parent : default_parent;
		ctrl->gates[n] = clk_register_gate(NULL, descr[n].name, parent,
				   0, base, descr[n].bit_idx, 0, &ctrl->lock);
		WARN_ON(IS_ERR(ctrl->gates[n]));
	}
	of_clk_add_provider(np, mvebu_clk_gating_get_src, ctrl);
}

/*
 * SoC specific clock gating control
 */

#ifdef CONFIG_ARCH_DOVE
static const struct mvebu_soc_descr __initconst dove_gating_descr[] = {
	{ "usb0", NULL, 0 },
	{ "usb1", NULL, 1 },
	{ "ge", "gephy", 2 },
	{ "sata", NULL, 3 },
	{ "pex0", NULL, 4 },
	{ "pex1", NULL, 5 },
	{ "sdio0", NULL, 8 },
	{ "sdio1", NULL, 9 },
	{ "nand", NULL, 10 },
	{ "camera", NULL, 11 },
	{ "i2s0", NULL, 12 },
	{ "i2s1", NULL, 13 },
	{ "crypto", NULL, 15 },
	{ "ac97", NULL, 21 },
	{ "pdma", NULL, 22 },
	{ "xor0", NULL, 23 },
	{ "xor1", NULL, 24 },
	{ "gephy", NULL, 30 },
	{ }
};
#endif

#ifdef CONFIG_ARCH_KIRKWOOD
static const struct mvebu_soc_descr __initconst kirkwood_gating_descr[] = {
	{ "ge0", NULL, 0 },
	{ "pex0", NULL, 2 },
	{ "usb0", NULL, 3 },
	{ "sdio", NULL, 4 },
	{ "tsu", NULL, 5 },
	{ "runit", NULL, 7 },
	{ "xor0", NULL, 8 },
	{ "audio", NULL, 9 },
	{ "sata0", NULL, 14 },
	{ "sata1", NULL, 15 },
	{ "xor1", NULL, 16 },
	{ "crypto", NULL, 17 },
	{ "pex1", NULL, 18 },
	{ "ge1", NULL, 19 },
	{ "tdm", NULL, 20 },
	{ }
};
#endif

static const __initdata struct of_device_id clk_gating_match[] = {
#ifdef CONFIG_ARCH_DOVE
	{
		.compatible = "marvell,dove-gating-clock",
		.data = dove_gating_descr,
	},
#endif

#ifdef CONFIG_ARCH_KIRKWOOD
	{
		.compatible = "marvell,kirkwood-gating-clock",
		.data = kirkwood_gating_descr,
	},
#endif

	{ }
};

void __init mvebu_gating_clk_init(void)
{
	struct device_node *np;

	for_each_matching_node(np, clk_gating_match) {
		const struct of_device_id *match =
			of_match_node(clk_gating_match, np);
		mvebu_clk_gating_setup(np,
		       (const struct mvebu_soc_descr *)match->data);
	}
}
