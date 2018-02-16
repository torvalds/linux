/*
 * Copyright (C) 2014 Chen-Yu Tsai
 * Author: Chen-Yu Tsai <wens@csie.org>
 *
 * Allwinner A23 APB0 clock driver
 *
 * License Terms: GNU General Public License v2
 *
 * Based on clk-sun6i-apb0.c
 * Allwinner A31 APB0 clock driver
 *
 * Copyright (C) 2014 Free Electrons
 * Author: Boris BREZILLON <boris.brezillon@free-electrons.com>
 *
 */

#include <linux/clk-provider.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>

static struct clk *sun8i_a23_apb0_register(struct device_node *node,
					   void __iomem *reg)
{
	const char *clk_name = node->name;
	const char *clk_parent;
	struct clk *clk;
	int ret;

	clk_parent = of_clk_get_parent_name(node, 0);
	if (!clk_parent)
		return ERR_PTR(-EINVAL);

	of_property_read_string(node, "clock-output-names", &clk_name);

	/* The A23 APB0 clock is a standard 2 bit wide divider clock */
	clk = clk_register_divider(NULL, clk_name, clk_parent, 0, reg,
				   0, 2, 0, NULL);
	if (IS_ERR(clk))
		return clk;

	ret = of_clk_add_provider(node, of_clk_src_simple_get, clk);
	if (ret)
		goto err_unregister;

	return clk;

err_unregister:
	clk_unregister_divider(clk);

	return ERR_PTR(ret);
}

static void sun8i_a23_apb0_setup(struct device_node *node)
{
	void __iomem *reg;
	struct resource res;
	struct clk *clk;

	reg = of_io_request_and_map(node, 0, of_node_full_name(node));
	if (IS_ERR(reg)) {
		/*
		 * This happens with clk nodes instantiated through mfd,
		 * as those do not have their resources assigned in the
		 * device tree. Do not print an error in this case.
		 */
		if (PTR_ERR(reg) != -EINVAL)
			pr_err("Could not get registers for a23-apb0-clk\n");

		return;
	}

	clk = sun8i_a23_apb0_register(node, reg);
	if (IS_ERR(clk))
		goto err_unmap;

	return;

err_unmap:
	iounmap(reg);
	of_address_to_resource(node, 0, &res);
	release_mem_region(res.start, resource_size(&res));
}
CLK_OF_DECLARE_DRIVER(sun8i_a23_apb0, "allwinner,sun8i-a23-apb0-clk",
		      sun8i_a23_apb0_setup);

static int sun8i_a23_apb0_clk_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct resource *r;
	void __iomem *reg;
	struct clk *clk;

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	reg = devm_ioremap_resource(&pdev->dev, r);
	if (IS_ERR(reg))
		return PTR_ERR(reg);

	clk = sun8i_a23_apb0_register(np, reg);
	return PTR_ERR_OR_ZERO(clk);
}

static const struct of_device_id sun8i_a23_apb0_clk_dt_ids[] = {
	{ .compatible = "allwinner,sun8i-a23-apb0-clk" },
	{ /* sentinel */ }
};

static struct platform_driver sun8i_a23_apb0_clk_driver = {
	.driver = {
		.name = "sun8i-a23-apb0-clk",
		.of_match_table = sun8i_a23_apb0_clk_dt_ids,
	},
	.probe = sun8i_a23_apb0_clk_probe,
};
builtin_platform_driver(sun8i_a23_apb0_clk_driver);
