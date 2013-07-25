/*
 * EIM driver for Freescale's i.MX chips
 *
 * Copyright (C) 2013 Freescale Semiconductor, Inc.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */
#include <linux/module.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/of_device.h>

struct imx_weim {
	void __iomem *base;
	struct clk *clk;
};

static const struct of_device_id weim_id_table[] = {
	{ .compatible = "fsl,imx6q-weim", },
	{}
};
MODULE_DEVICE_TABLE(of, weim_id_table);

#define CS_TIMING_LEN 6
#define CS_REG_RANGE  0x18

/* Parse and set the timing for this device. */
static int
weim_timing_setup(struct platform_device *pdev, struct device_node *np)
{
	struct imx_weim *weim = platform_get_drvdata(pdev);
	u32 value[CS_TIMING_LEN];
	u32 cs_idx;
	int ret;
	int i;

	/* get the CS index from this child node's "reg" property. */
	ret = of_property_read_u32(np, "reg", &cs_idx);
	if (ret)
		return ret;

	/* The weim has four chip selects. */
	if (cs_idx > 3)
		return -EINVAL;

	ret = of_property_read_u32_array(np, "fsl,weim-cs-timing",
					value, CS_TIMING_LEN);
	if (ret)
		return ret;

	/* set the timing for WEIM */
	for (i = 0; i < CS_TIMING_LEN; i++)
		writel(value[i], weim->base + cs_idx * CS_REG_RANGE + i * 4);
	return 0;
}

static int weim_parse_dt(struct platform_device *pdev)
{
	struct device_node *child;
	int ret;

	for_each_child_of_node(pdev->dev.of_node, child) {
		if (!child->name)
			continue;

		ret = weim_timing_setup(pdev, child);
		if (ret) {
			dev_err(&pdev->dev, "%s set timing failed.\n",
				child->full_name);
			return ret;
		}
	}

	ret = of_platform_populate(pdev->dev.of_node, NULL, NULL, &pdev->dev);
	if (ret)
		dev_err(&pdev->dev, "%s fail to create devices.\n",
			pdev->dev.of_node->full_name);
	return ret;
}

static int weim_probe(struct platform_device *pdev)
{
	struct imx_weim *weim;
	struct resource *res;
	int ret = -EINVAL;

	weim = devm_kzalloc(&pdev->dev, sizeof(*weim), GFP_KERNEL);
	if (!weim) {
		ret = -ENOMEM;
		goto weim_err;
	}
	platform_set_drvdata(pdev, weim);

	/* get the resource */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	weim->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(weim->base)) {
		ret = PTR_ERR(weim->base);
		goto weim_err;
	}

	/* get the clock */
	weim->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(weim->clk))
		goto weim_err;

	ret = clk_prepare_enable(weim->clk);
	if (ret)
		goto weim_err;

	/* parse the device node */
	ret = weim_parse_dt(pdev);
	if (ret) {
		clk_disable_unprepare(weim->clk);
		goto weim_err;
	}

	dev_info(&pdev->dev, "WEIM driver registered.\n");
	return 0;

weim_err:
	return ret;
}

static struct platform_driver weim_driver = {
	.driver = {
		.name = "imx-weim",
		.of_match_table = weim_id_table,
	},
	.probe   = weim_probe,
};

module_platform_driver(weim_driver);
MODULE_AUTHOR("Freescale Semiconductor Inc.");
MODULE_DESCRIPTION("i.MX EIM Controller Driver");
MODULE_LICENSE("GPL");
