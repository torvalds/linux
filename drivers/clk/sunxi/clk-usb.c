// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2013-2015 Emilio López
 *
 * Emilio López <emilio@elopez.com.ar>
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/reset-controller.h>
#include <linux/slab.h>
#include <linux/spinlock.h>


/*
 * sunxi_usb_reset... - reset bits in usb clk registers handling
 */

struct usb_reset_data {
	void __iomem			*reg;
	spinlock_t			*lock;
	struct clk			*clk;
	struct reset_controller_dev	rcdev;
};

static int sunxi_usb_reset_assert(struct reset_controller_dev *rcdev,
			      unsigned long id)
{
	struct usb_reset_data *data = container_of(rcdev,
						   struct usb_reset_data,
						   rcdev);
	unsigned long flags;
	u32 reg;

	clk_prepare_enable(data->clk);
	spin_lock_irqsave(data->lock, flags);

	reg = readl(data->reg);
	writel(reg & ~BIT(id), data->reg);

	spin_unlock_irqrestore(data->lock, flags);
	clk_disable_unprepare(data->clk);

	return 0;
}

static int sunxi_usb_reset_deassert(struct reset_controller_dev *rcdev,
				unsigned long id)
{
	struct usb_reset_data *data = container_of(rcdev,
						     struct usb_reset_data,
						     rcdev);
	unsigned long flags;
	u32 reg;

	clk_prepare_enable(data->clk);
	spin_lock_irqsave(data->lock, flags);

	reg = readl(data->reg);
	writel(reg | BIT(id), data->reg);

	spin_unlock_irqrestore(data->lock, flags);
	clk_disable_unprepare(data->clk);

	return 0;
}

static const struct reset_control_ops sunxi_usb_reset_ops = {
	.assert		= sunxi_usb_reset_assert,
	.deassert	= sunxi_usb_reset_deassert,
};

/**
 * sunxi_usb_clk_setup() - Setup function for usb gate clocks
 */

#define SUNXI_USB_MAX_SIZE 32

struct usb_clk_data {
	u32 clk_mask;
	u32 reset_mask;
	bool reset_needs_clk;
};

static void __init sunxi_usb_clk_setup(struct device_node *node,
				       const struct usb_clk_data *data,
				       spinlock_t *lock)
{
	struct clk_onecell_data *clk_data;
	struct usb_reset_data *reset_data;
	const char *clk_parent;
	const char *clk_name;
	void __iomem *reg;
	int qty;
	int i = 0;
	int j = 0;

	reg = of_io_request_and_map(node, 0, of_node_full_name(node));
	if (IS_ERR(reg))
		return;

	clk_parent = of_clk_get_parent_name(node, 0);
	if (!clk_parent)
		return;

	/* Worst-case size approximation and memory allocation */
	qty = find_last_bit((unsigned long *)&data->clk_mask,
			    SUNXI_USB_MAX_SIZE);

	clk_data = kmalloc(sizeof(struct clk_onecell_data), GFP_KERNEL);
	if (!clk_data)
		return;

	clk_data->clks = kcalloc(qty + 1, sizeof(struct clk *), GFP_KERNEL);
	if (!clk_data->clks) {
		kfree(clk_data);
		return;
	}

	for_each_set_bit(i, (unsigned long *)&data->clk_mask,
			 SUNXI_USB_MAX_SIZE) {
		of_property_read_string_index(node, "clock-output-names",
					      j, &clk_name);
		clk_data->clks[i] = clk_register_gate(NULL, clk_name,
						      clk_parent, 0,
						      reg, i, 0, lock);
		WARN_ON(IS_ERR(clk_data->clks[i]));

		j++;
	}

	/* Adjust to the real max */
	clk_data->clk_num = i;

	of_clk_add_provider(node, of_clk_src_onecell_get, clk_data);

	/* Register a reset controller for usb with reset bits */
	if (data->reset_mask == 0)
		return;

	reset_data = kzalloc(sizeof(*reset_data), GFP_KERNEL);
	if (!reset_data)
		return;

	if (data->reset_needs_clk) {
		reset_data->clk = of_clk_get(node, 0);
		if (IS_ERR(reset_data->clk)) {
			pr_err("Could not get clock for reset controls\n");
			kfree(reset_data);
			return;
		}
	}

	reset_data->reg = reg;
	reset_data->lock = lock;
	reset_data->rcdev.nr_resets = __fls(data->reset_mask) + 1;
	reset_data->rcdev.ops = &sunxi_usb_reset_ops;
	reset_data->rcdev.of_node = node;
	reset_controller_register(&reset_data->rcdev);
}

static const struct usb_clk_data sun4i_a10_usb_clk_data __initconst = {
	.clk_mask = BIT(8) | BIT(7) | BIT(6),
	.reset_mask = BIT(2) | BIT(1) | BIT(0),
};

static DEFINE_SPINLOCK(sun4i_a10_usb_lock);

static void __init sun4i_a10_usb_setup(struct device_node *node)
{
	sunxi_usb_clk_setup(node, &sun4i_a10_usb_clk_data, &sun4i_a10_usb_lock);
}
CLK_OF_DECLARE(sun4i_a10_usb, "allwinner,sun4i-a10-usb-clk", sun4i_a10_usb_setup);

static const struct usb_clk_data sun5i_a13_usb_clk_data __initconst = {
	.clk_mask = BIT(8) | BIT(6),
	.reset_mask = BIT(1) | BIT(0),
};

static void __init sun5i_a13_usb_setup(struct device_node *node)
{
	sunxi_usb_clk_setup(node, &sun5i_a13_usb_clk_data, &sun4i_a10_usb_lock);
}
CLK_OF_DECLARE(sun5i_a13_usb, "allwinner,sun5i-a13-usb-clk", sun5i_a13_usb_setup);

static const struct usb_clk_data sun6i_a31_usb_clk_data __initconst = {
	.clk_mask = BIT(18) | BIT(17) | BIT(16) | BIT(10) | BIT(9) | BIT(8),
	.reset_mask = BIT(2) | BIT(1) | BIT(0),
};

static void __init sun6i_a31_usb_setup(struct device_node *node)
{
	sunxi_usb_clk_setup(node, &sun6i_a31_usb_clk_data, &sun4i_a10_usb_lock);
}
CLK_OF_DECLARE(sun6i_a31_usb, "allwinner,sun6i-a31-usb-clk", sun6i_a31_usb_setup);

static const struct usb_clk_data sun8i_a23_usb_clk_data __initconst = {
	.clk_mask = BIT(16) | BIT(11) | BIT(10) | BIT(9) | BIT(8),
	.reset_mask = BIT(2) | BIT(1) | BIT(0),
};

static void __init sun8i_a23_usb_setup(struct device_node *node)
{
	sunxi_usb_clk_setup(node, &sun8i_a23_usb_clk_data, &sun4i_a10_usb_lock);
}
CLK_OF_DECLARE(sun8i_a23_usb, "allwinner,sun8i-a23-usb-clk", sun8i_a23_usb_setup);

static const struct usb_clk_data sun8i_h3_usb_clk_data __initconst = {
	.clk_mask =  BIT(19) | BIT(18) | BIT(17) | BIT(16) |
		     BIT(11) | BIT(10) | BIT(9) | BIT(8),
	.reset_mask = BIT(3) | BIT(2) | BIT(1) | BIT(0),
};

static void __init sun8i_h3_usb_setup(struct device_node *node)
{
	sunxi_usb_clk_setup(node, &sun8i_h3_usb_clk_data, &sun4i_a10_usb_lock);
}
CLK_OF_DECLARE(sun8i_h3_usb, "allwinner,sun8i-h3-usb-clk", sun8i_h3_usb_setup);

static const struct usb_clk_data sun9i_a80_usb_mod_data __initconst = {
	.clk_mask = BIT(6) | BIT(5) | BIT(4) | BIT(3) | BIT(2) | BIT(1),
	.reset_mask = BIT(19) | BIT(18) | BIT(17),
	.reset_needs_clk = 1,
};

static DEFINE_SPINLOCK(a80_usb_mod_lock);

static void __init sun9i_a80_usb_mod_setup(struct device_node *node)
{
	sunxi_usb_clk_setup(node, &sun9i_a80_usb_mod_data, &a80_usb_mod_lock);
}
CLK_OF_DECLARE(sun9i_a80_usb_mod, "allwinner,sun9i-a80-usb-mod-clk", sun9i_a80_usb_mod_setup);

static const struct usb_clk_data sun9i_a80_usb_phy_data __initconst = {
	.clk_mask = BIT(10) | BIT(5) | BIT(4) | BIT(3) | BIT(2) | BIT(1),
	.reset_mask = BIT(21) | BIT(20) | BIT(19) | BIT(18) | BIT(17),
	.reset_needs_clk = 1,
};

static DEFINE_SPINLOCK(a80_usb_phy_lock);

static void __init sun9i_a80_usb_phy_setup(struct device_node *node)
{
	sunxi_usb_clk_setup(node, &sun9i_a80_usb_phy_data, &a80_usb_phy_lock);
}
CLK_OF_DECLARE(sun9i_a80_usb_phy, "allwinner,sun9i-a80-usb-phy-clk", sun9i_a80_usb_phy_setup);
