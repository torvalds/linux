// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2021 Rockchip Electronics Co., Ltd
 */

#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/pm_clock.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>

struct rockchip_link_info {
	u32 shift;
	const char *name;
	const char *pname;
};

struct rockchip_link {
	int num;
	const struct rockchip_link_info *info;
};

struct rockchip_link_clk {
	void __iomem *base;
	struct clk_gate *gate;
	spinlock_t lock;
	u32 shift;
	u32 flag;
	const char *name;
	const char *pname;
	const char *link_name;
	const struct rockchip_link *link;
};

#define GFLAGS (CLK_GATE_HIWORD_MASK | CLK_GATE_SET_TO_DISABLE)

#define GATE_LINK(_name, _pname, _shift)	\
{						\
	.name = _name,				\
	.pname = _pname,			\
	.shift = (_shift),			\
}

static int register_clocks(struct rockchip_link_clk *priv, struct device *dev)
{
	struct clk_gate *gate;
	struct clk_init_data init = {};
	struct clk *clk;

	gate = devm_kzalloc(dev, sizeof(struct clk_gate), GFP_KERNEL);
	if (!gate)
		return -ENOMEM;

	init.name = priv->name;
	init.ops = &clk_gate_ops;
	init.flags |= CLK_SET_RATE_PARENT;
	init.parent_names = &priv->pname;
	init.num_parents = 1;

	/* struct clk_gate assignments */
	gate->reg = priv->base;
	gate->bit_idx = priv->shift;
	gate->flags = GFLAGS;
	gate->lock = &priv->lock;
	gate->hw.init = &init;

	clk = devm_clk_register(dev, &gate->hw);
	if (IS_ERR(clk))
		return -EINVAL;

	return of_clk_add_provider(dev->of_node, of_clk_src_simple_get, clk);
}

static const struct rockchip_link_info rk3588_clk_gate_link_info[] = {
	GATE_LINK("aclk_isp1_pre", "aclk_isp1_root", 6),
	GATE_LINK("hclk_isp1_pre", "hclk_isp1_root", 8),
	GATE_LINK("hclk_nvm", "hclk_nvm_root", 2),
	GATE_LINK("aclk_usb", "aclk_usb_root", 2),
	GATE_LINK("hclk_usb", "hclk_usb_root", 3),
	GATE_LINK("aclk_jpeg_decoder_pre", "aclk_jpeg_decoder_root", 7),
	GATE_LINK("aclk_vdpu_low_pre", "aclk_vdpu_low_root", 5),
	GATE_LINK("aclk_rkvenc1_pre", "aclk_rkvenc1_root", 3),
	GATE_LINK("hclk_rkvenc1_pre", "hclk_rkvenc1_root", 2),
	GATE_LINK("hclk_rkvdec0_pre", "hclk_rkvdec0_root", 5),
	GATE_LINK("aclk_rkvdec0_pre", "aclk_rkvdec0_root", 6),
	GATE_LINK("hclk_rkvdec1_pre", "hclk_rkvdec1_root", 4),
	GATE_LINK("aclk_rkvdec1_pre", "aclk_rkvdec1_root", 5),
	GATE_LINK("aclk_hdcp0_pre", "aclk_vo0_root", 9),
	GATE_LINK("hclk_vo0", "hclk_vo0_root", 5),
	GATE_LINK("aclk_hdcp1_pre", "aclk_hdcp1_root", 6),
	GATE_LINK("hclk_vo1", "hclk_vo1_root", 9),
	GATE_LINK("aclk_av1_pre", "aclk_av1_root", 1),
	GATE_LINK("pclk_av1_pre", "pclk_av1_root", 4),
	GATE_LINK("hclk_sdio_pre", "hclk_sdio_root", 1),
	GATE_LINK("pclk_vo0_grf", "pclk_vo0_root", 10),
	GATE_LINK("pclk_vo1_grf", "pclk_vo1_root", 12),
};

static const struct rockchip_link rk3588_clk_gate_link = {
	.num = ARRAY_SIZE(rk3588_clk_gate_link_info),
	.info = rk3588_clk_gate_link_info,
};

static const struct of_device_id rockchip_clk_link_of_match[] = {
	{
		.compatible = "rockchip,rk3588-clock-gate-link",
		.data = (void *)&rk3588_clk_gate_link,
	},
	{}
};
MODULE_DEVICE_TABLE(of, rockchip_clk_link_of_match);

static const struct rockchip_link_info *
rockchip_get_link_infos(const struct rockchip_link *link, const char *name)
{
	const struct rockchip_link_info *info = link->info;
	int i = 0;

	for (i = 0; i < link->num; i++) {
		if (strcmp(info->name, name) == 0)
			break;
		info++;
	}
	return info;
}

static int rockchip_clk_link_probe(struct platform_device *pdev)
{
	struct rockchip_link_clk *priv;
	struct device_node *node = pdev->dev.of_node;
	const struct of_device_id *match;
	const char *clk_name;
	const struct rockchip_link_info *link_info;
	int ret;

	match = of_match_node(rockchip_clk_link_of_match, node);
	if (!match)
		return -ENXIO;

	priv = devm_kzalloc(&pdev->dev, sizeof(struct rockchip_link_clk),
			    GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->link = match->data;

	spin_lock_init(&priv->lock);
	platform_set_drvdata(pdev, priv);

	priv->base = of_iomap(node, 0);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	if (of_property_read_string(node, "clock-output-names", &clk_name))
		priv->name = node->name;
	else
		priv->name = clk_name;

	link_info = rockchip_get_link_infos(priv->link, priv->name);
	priv->shift = link_info->shift;
	priv->pname = link_info->pname;

	pm_runtime_enable(&pdev->dev);
	ret = pm_clk_create(&pdev->dev);
	if (ret)
		goto disable_pm_runtime;

	ret = pm_clk_add(&pdev->dev, "link");

	if (ret)
		goto destroy_pm_clk;

	ret = register_clocks(priv, &pdev->dev);
	if (ret)
		goto destroy_pm_clk;

	return 0;

destroy_pm_clk:
	pm_clk_destroy(&pdev->dev);
disable_pm_runtime:
	pm_runtime_disable(&pdev->dev);

	return ret;
}

static int rockchip_clk_link_remove(struct platform_device *pdev)
{
	pm_clk_destroy(&pdev->dev);
	pm_runtime_disable(&pdev->dev);

	return 0;
}

static const struct dev_pm_ops rockchip_clk_link_pm_ops = {
	SET_RUNTIME_PM_OPS(pm_clk_suspend, pm_clk_resume, NULL)
};

static struct platform_driver rockchip_clk_link_driver = {
	.driver = {
		.name = "clock-link",
		.of_match_table = of_match_ptr(rockchip_clk_link_of_match),
		.pm = &rockchip_clk_link_pm_ops,
	},
	.probe = rockchip_clk_link_probe,
	.remove = rockchip_clk_link_remove,
};

static int __init rockchip_clk_link_drv_register(void)
{
	return platform_driver_register(&rockchip_clk_link_driver);
}
postcore_initcall_sync(rockchip_clk_link_drv_register);

static void __exit rockchip_clk_link_drv_unregister(void)
{
	platform_driver_unregister(&rockchip_clk_link_driver);
}
module_exit(rockchip_clk_link_drv_unregister);

MODULE_AUTHOR("Elaine Zhang <zhangqing@rock-chips.com>");
MODULE_DESCRIPTION("Clock driver for Niu Dependencies");
MODULE_LICENSE("GPL");
