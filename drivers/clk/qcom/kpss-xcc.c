// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2018, The Linux Foundation. All rights reserved.

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>

static const char *aux_parents[] = {
	"pll8_vote",
	"pxo",
};

static const u32 aux_parent_map[] = {
	3,
	0,
};

static const struct of_device_id kpss_xcc_match_table[] = {
	{ .compatible = "qcom,kpss-acc-v1", .data = (void *)1UL },
	{ .compatible = "qcom,kpss-gcc" },
	{}
};
MODULE_DEVICE_TABLE(of, kpss_xcc_match_table);

static int kpss_xcc_driver_probe(struct platform_device *pdev)
{
	const struct of_device_id *id;
	struct clk *clk;
	void __iomem *base;
	const char *name;

	id = of_match_device(kpss_xcc_match_table, &pdev->dev);
	if (!id)
		return -ENODEV;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	if (id->data) {
		if (of_property_read_string_index(pdev->dev.of_node,
						  "clock-output-names",
						  0, &name))
			return -ENODEV;
		base += 0x14;
	} else {
		name = "acpu_l2_aux";
		base += 0x28;
	}

	clk = clk_register_mux_table(&pdev->dev, name, aux_parents,
				     ARRAY_SIZE(aux_parents), 0, base, 0, 0x3,
				     0, aux_parent_map, NULL);

	platform_set_drvdata(pdev, clk);

	return PTR_ERR_OR_ZERO(clk);
}

static int kpss_xcc_driver_remove(struct platform_device *pdev)
{
	clk_unregister_mux(platform_get_drvdata(pdev));
	return 0;
}

static struct platform_driver kpss_xcc_driver = {
	.probe = kpss_xcc_driver_probe,
	.remove = kpss_xcc_driver_remove,
	.driver = {
		.name = "kpss-xcc",
		.of_match_table = kpss_xcc_match_table,
	},
};
module_platform_driver(kpss_xcc_driver);

MODULE_DESCRIPTION("Krait Processor Sub System (KPSS) Clock Driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:kpss-xcc");
