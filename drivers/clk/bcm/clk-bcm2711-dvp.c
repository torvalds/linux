// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright 2020 Cerno

#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/reset-controller.h>
#include <linux/reset/reset-simple.h>

#define DVP_HT_RPI_SW_INIT	0x04
#define DVP_HT_RPI_MISC_CONFIG	0x08

#define NR_CLOCKS	2
#define NR_RESETS	6

struct clk_dvp {
	struct clk_hw_onecell_data	*data;
	struct reset_simple_data	reset;
};

static const struct clk_parent_data clk_dvp_parent = {
	.index	= 0,
};

static int clk_dvp_probe(struct platform_device *pdev)
{
	struct clk_hw_onecell_data *data;
	struct resource *res;
	struct clk_dvp *dvp;
	void __iomem *base;
	int ret;

	dvp = devm_kzalloc(&pdev->dev, sizeof(*dvp), GFP_KERNEL);
	if (!dvp)
		return -ENOMEM;
	platform_set_drvdata(pdev, dvp);

	dvp->data = devm_kzalloc(&pdev->dev,
				 struct_size(dvp->data, hws, NR_CLOCKS),
				 GFP_KERNEL);
	if (!dvp->data)
		return -ENOMEM;
	data = dvp->data;

	base = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	dvp->reset.rcdev.owner = THIS_MODULE;
	dvp->reset.rcdev.nr_resets = NR_RESETS;
	dvp->reset.rcdev.ops = &reset_simple_ops;
	dvp->reset.rcdev.of_node = pdev->dev.of_node;
	dvp->reset.membase = base + DVP_HT_RPI_SW_INIT;
	spin_lock_init(&dvp->reset.lock);

	ret = devm_reset_controller_register(&pdev->dev, &dvp->reset.rcdev);
	if (ret)
		return ret;

	data->hws[0] = clk_hw_register_gate_parent_data(&pdev->dev,
							"hdmi0-108MHz",
							&clk_dvp_parent, 0,
							base + DVP_HT_RPI_MISC_CONFIG, 3,
							CLK_GATE_SET_TO_DISABLE,
							&dvp->reset.lock);
	if (IS_ERR(data->hws[0]))
		return PTR_ERR(data->hws[0]);

	data->hws[1] = clk_hw_register_gate_parent_data(&pdev->dev,
							"hdmi1-108MHz",
							&clk_dvp_parent, 0,
							base + DVP_HT_RPI_MISC_CONFIG, 4,
							CLK_GATE_SET_TO_DISABLE,
							&dvp->reset.lock);
	if (IS_ERR(data->hws[1])) {
		ret = PTR_ERR(data->hws[1]);
		goto unregister_clk0;
	}

	data->num = NR_CLOCKS;
	ret = of_clk_add_hw_provider(pdev->dev.of_node, of_clk_hw_onecell_get,
				     data);
	if (ret)
		goto unregister_clk1;

	return 0;

unregister_clk1:
	clk_hw_unregister_gate(data->hws[1]);

unregister_clk0:
	clk_hw_unregister_gate(data->hws[0]);
	return ret;
};

static int clk_dvp_remove(struct platform_device *pdev)
{
	struct clk_dvp *dvp = platform_get_drvdata(pdev);
	struct clk_hw_onecell_data *data = dvp->data;

	clk_hw_unregister_gate(data->hws[1]);
	clk_hw_unregister_gate(data->hws[0]);

	return 0;
}

static const struct of_device_id clk_dvp_dt_ids[] = {
	{ .compatible = "brcm,brcm2711-dvp", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, clk_dvp_dt_ids);

static struct platform_driver clk_dvp_driver = {
	.probe	= clk_dvp_probe,
	.remove	= clk_dvp_remove,
	.driver	= {
		.name		= "brcm2711-dvp",
		.of_match_table	= clk_dvp_dt_ids,
	},
};
module_platform_driver(clk_dvp_driver);

MODULE_AUTHOR("Maxime Ripard <maxime@cerno.tech>");
MODULE_DESCRIPTION("BCM2711 DVP clock driver");
MODULE_LICENSE("GPL");
