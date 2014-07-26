/*
 * Copyright (C) 2014 Free Electrons
 *
 * License Terms: GNU General Public License v2
 * Author: Boris BREZILLON <boris.brezillon@free-electrons.com>
 *
 * Allwinner A31 APB0 clock gates driver
 *
 */

#include <linux/clk-provider.h>
#include <linux/clkdev.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>

#define SUN6I_APB0_GATES_MAX_SIZE	32

struct gates_data {
	DECLARE_BITMAP(mask, SUN6I_APB0_GATES_MAX_SIZE);
};

static const struct gates_data sun6i_a31_apb0_gates __initconst = {
	.mask = {0x7F},
};

static const struct gates_data sun8i_a23_apb0_gates __initconst = {
	.mask = {0x5D},
};

const struct of_device_id sun6i_a31_apb0_gates_clk_dt_ids[] = {
	{ .compatible = "allwinner,sun6i-a31-apb0-gates-clk", .data = &sun6i_a31_apb0_gates },
	{ .compatible = "allwinner,sun8i-a23-apb0-gates-clk", .data = &sun8i_a23_apb0_gates },
	{ /* sentinel */ }
};

static int sun6i_a31_apb0_gates_clk_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct clk_onecell_data *clk_data;
	const struct of_device_id *device;
	const struct gates_data *data;
	const char *clk_parent;
	const char *clk_name;
	struct resource *r;
	void __iomem *reg;
	int ngates;
	int i;
	int j = 0;

	if (!np)
		return -ENODEV;

	device = of_match_device(sun6i_a31_apb0_gates_clk_dt_ids, &pdev->dev);
	if (!device)
		return -ENODEV;
	data = device->data;

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	reg = devm_ioremap_resource(&pdev->dev, r);
	if (IS_ERR(reg))
		return PTR_ERR(reg);

	clk_parent = of_clk_get_parent_name(np, 0);
	if (!clk_parent)
		return -EINVAL;

	clk_data = devm_kzalloc(&pdev->dev, sizeof(struct clk_onecell_data),
				GFP_KERNEL);
	if (!clk_data)
		return -ENOMEM;

	/* Worst-case size approximation and memory allocation */
	ngates = find_last_bit(data->mask, SUN6I_APB0_GATES_MAX_SIZE);
	clk_data->clks = devm_kcalloc(&pdev->dev, (ngates + 1),
				      sizeof(struct clk *), GFP_KERNEL);
	if (!clk_data->clks)
		return -ENOMEM;

	for_each_set_bit(i, data->mask, SUN6I_APB0_GATES_MAX_SIZE) {
		of_property_read_string_index(np, "clock-output-names",
					      j, &clk_name);

		clk_data->clks[i] = clk_register_gate(&pdev->dev, clk_name,
						      clk_parent, 0, reg, i,
						      0, NULL);
		WARN_ON(IS_ERR(clk_data->clks[i]));
		clk_register_clkdev(clk_data->clks[i], clk_name, NULL);

		j++;
	}

	clk_data->clk_num = ngates + 1;

	return of_clk_add_provider(np, of_clk_src_onecell_get, clk_data);
}

static struct platform_driver sun6i_a31_apb0_gates_clk_driver = {
	.driver = {
		.name = "sun6i-a31-apb0-gates-clk",
		.owner = THIS_MODULE,
		.of_match_table = sun6i_a31_apb0_gates_clk_dt_ids,
	},
	.probe = sun6i_a31_apb0_gates_clk_probe,
};
module_platform_driver(sun6i_a31_apb0_gates_clk_driver);

MODULE_AUTHOR("Boris BREZILLON <boris.brezillon@free-electrons.com>");
MODULE_DESCRIPTION("Allwinner A31 APB0 gate clocks driver");
MODULE_LICENSE("GPL v2");
