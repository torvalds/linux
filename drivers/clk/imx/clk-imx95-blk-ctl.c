// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2024 NXP
 */

#include <dt-bindings/clock/nxp,imx95-clock.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/pm_runtime.h>
#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/types.h>

enum {
	CLK_GATE,
	CLK_DIVIDER,
	CLK_MUX,
};

struct imx95_blk_ctl {
	struct device *dev;
	spinlock_t lock;
	struct clk *clk_apb;

	void __iomem *base;
	/* clock gate register */
	u32 clk_reg_restore;
};

struct imx95_blk_ctl_clk_dev_data {
	const char *name;
	const char * const *parent_names;
	u32 num_parents;
	u32 reg;
	u32 bit_idx;
	u32 bit_width;
	u32 clk_type;
	u32 flags;
	u32 flags2;
	u32 type;
};

struct imx95_blk_ctl_dev_data {
	const struct imx95_blk_ctl_clk_dev_data *clk_dev_data;
	u32 num_clks;
	bool rpm_enabled;
	u32 clk_reg_offset;
};

static const struct imx95_blk_ctl_clk_dev_data vpublk_clk_dev_data[] = {
	[IMX95_CLK_VPUBLK_WAVE] = {
		.name = "vpublk_wave_vpu",
		.parent_names = (const char *[]){ "vpu", },
		.num_parents = 1,
		.reg = 8,
		.bit_idx = 0,
		.type = CLK_GATE,
		.flags = CLK_SET_RATE_PARENT,
		.flags2 = CLK_GATE_SET_TO_DISABLE,
	},
	[IMX95_CLK_VPUBLK_JPEG_ENC] = {
		.name = "vpublk_jpeg_enc",
		.parent_names = (const char *[]){ "vpujpeg", },
		.num_parents = 1,
		.reg = 8,
		.bit_idx = 1,
		.type = CLK_GATE,
		.flags = CLK_SET_RATE_PARENT,
		.flags2 = CLK_GATE_SET_TO_DISABLE,
	},
	[IMX95_CLK_VPUBLK_JPEG_DEC] = {
		.name = "vpublk_jpeg_dec",
		.parent_names = (const char *[]){ "vpujpeg", },
		.num_parents = 1,
		.reg = 8,
		.bit_idx = 2,
		.type = CLK_GATE,
		.flags = CLK_SET_RATE_PARENT,
		.flags2 = CLK_GATE_SET_TO_DISABLE,
	}
};

static const struct imx95_blk_ctl_dev_data vpublk_dev_data = {
	.num_clks = ARRAY_SIZE(vpublk_clk_dev_data),
	.clk_dev_data = vpublk_clk_dev_data,
	.rpm_enabled = true,
	.clk_reg_offset = 8,
};

static const struct imx95_blk_ctl_clk_dev_data camblk_clk_dev_data[] = {
	[IMX95_CLK_CAMBLK_CSI2_FOR0] = {
		.name = "camblk_csi2_for0",
		.parent_names = (const char *[]){ "camisi", },
		.num_parents = 1,
		.reg = 0,
		.bit_idx = 0,
		.type = CLK_GATE,
		.flags = CLK_SET_RATE_PARENT,
		.flags2 = CLK_GATE_SET_TO_DISABLE,
	},
	[IMX95_CLK_CAMBLK_CSI2_FOR1] = {
		.name = "camblk_csi2_for1",
		.parent_names = (const char *[]){ "camisi", },
		.num_parents = 1,
		.reg = 0,
		.bit_idx = 1,
		.type = CLK_GATE,
		.flags = CLK_SET_RATE_PARENT,
		.flags2 = CLK_GATE_SET_TO_DISABLE,
	},
	[IMX95_CLK_CAMBLK_ISP_AXI] = {
		.name = "camblk_isp_axi",
		.parent_names = (const char *[]){ "camaxi", },
		.num_parents = 1,
		.reg = 0,
		.bit_idx = 4,
		.type = CLK_GATE,
		.flags = CLK_SET_RATE_PARENT,
		.flags2 = CLK_GATE_SET_TO_DISABLE,
	},
	[IMX95_CLK_CAMBLK_ISP_PIXEL] = {
		.name = "camblk_isp_pixel",
		.parent_names = (const char *[]){ "camisi", },
		.num_parents = 1,
		.reg = 0,
		.bit_idx = 5,
		.type = CLK_GATE,
		.flags = CLK_SET_RATE_PARENT,
		.flags2 = CLK_GATE_SET_TO_DISABLE,
	},
	[IMX95_CLK_CAMBLK_ISP] = {
		.name = "camblk_isp",
		.parent_names = (const char *[]){ "camisi", },
		.num_parents = 1,
		.reg = 0,
		.bit_idx = 6,
		.type = CLK_GATE,
		.flags = CLK_SET_RATE_PARENT,
		.flags2 = CLK_GATE_SET_TO_DISABLE,
	}
};

static const struct imx95_blk_ctl_dev_data camblk_dev_data = {
	.num_clks = ARRAY_SIZE(camblk_clk_dev_data),
	.clk_dev_data = camblk_clk_dev_data,
	.clk_reg_offset = 0,
};

static const struct imx95_blk_ctl_clk_dev_data lvds_clk_dev_data[] = {
	[IMX95_CLK_DISPMIX_LVDS_PHY_DIV] = {
		.name = "ldb_phy_div",
		.parent_names = (const char *[]){ "ldbpll", },
		.num_parents = 1,
		.reg = 0,
		.bit_idx = 0,
		.bit_width = 1,
		.type = CLK_DIVIDER,
		.flags2 = CLK_DIVIDER_POWER_OF_TWO,
	},
	[IMX95_CLK_DISPMIX_LVDS_CH0_GATE] = {
		.name = "lvds_ch0_gate",
		.parent_names = (const char *[]){ "ldb_phy_div", },
		.num_parents = 1,
		.reg = 0,
		.bit_idx = 1,
		.bit_width = 1,
		.type = CLK_GATE,
		.flags = CLK_SET_RATE_PARENT,
		.flags2 = CLK_GATE_SET_TO_DISABLE,
	},
	[IMX95_CLK_DISPMIX_LVDS_CH1_GATE] = {
		.name = "lvds_ch1_gate",
		.parent_names = (const char *[]){ "ldb_phy_div", },
		.num_parents = 1,
		.reg = 0,
		.bit_idx = 2,
		.bit_width = 1,
		.type = CLK_GATE,
		.flags = CLK_SET_RATE_PARENT,
		.flags2 = CLK_GATE_SET_TO_DISABLE,
	},
	[IMX95_CLK_DISPMIX_PIX_DI0_GATE] = {
		.name = "lvds_di0_gate",
		.parent_names = (const char *[]){ "ldb_pll_div7", },
		.num_parents = 1,
		.reg = 0,
		.bit_idx = 3,
		.bit_width = 1,
		.type = CLK_GATE,
		.flags = CLK_SET_RATE_PARENT,
		.flags2 = CLK_GATE_SET_TO_DISABLE,
	},
	[IMX95_CLK_DISPMIX_PIX_DI1_GATE] = {
		.name = "lvds_di1_gate",
		.parent_names = (const char *[]){ "ldb_pll_div7", },
		.num_parents = 1,
		.reg = 0,
		.bit_idx = 4,
		.bit_width = 1,
		.type = CLK_GATE,
		.flags = CLK_SET_RATE_PARENT,
		.flags2 = CLK_GATE_SET_TO_DISABLE,
	},
};

static const struct imx95_blk_ctl_dev_data lvds_csr_dev_data = {
	.num_clks = ARRAY_SIZE(lvds_clk_dev_data),
	.clk_dev_data = lvds_clk_dev_data,
	.clk_reg_offset = 0,
};

static const struct imx95_blk_ctl_clk_dev_data dispmix_csr_clk_dev_data[] = {
	[IMX95_CLK_DISPMIX_ENG0_SEL] = {
		.name = "disp_engine0_sel",
		.parent_names = (const char *[]){"videopll1", "dsi_pll", "ldb_pll_div7", },
		.num_parents = 4,
		.reg = 0,
		.bit_idx = 0,
		.bit_width = 2,
		.type = CLK_MUX,
		.flags = CLK_SET_RATE_NO_REPARENT | CLK_SET_RATE_PARENT,
	},
	[IMX95_CLK_DISPMIX_ENG1_SEL] = {
		.name = "disp_engine1_sel",
		.parent_names = (const char *[]){"videopll1", "dsi_pll", "ldb_pll_div7", },
		.num_parents = 4,
		.reg = 0,
		.bit_idx = 2,
		.bit_width = 2,
		.type = CLK_MUX,
		.flags = CLK_SET_RATE_NO_REPARENT | CLK_SET_RATE_PARENT,
	}
};

static const struct imx95_blk_ctl_dev_data dispmix_csr_dev_data = {
	.num_clks = ARRAY_SIZE(dispmix_csr_clk_dev_data),
	.clk_dev_data = dispmix_csr_clk_dev_data,
	.clk_reg_offset = 0,
};

static const struct imx95_blk_ctl_clk_dev_data netxmix_clk_dev_data[] = {
	[IMX95_CLK_NETCMIX_ENETC0_RMII] = {
		.name = "enetc0_rmii_sel",
		.parent_names = (const char *[]){"ext_enetref", "enetref"},
		.num_parents = 2,
		.reg = 4,
		.bit_idx = 5,
		.bit_width = 1,
		.type = CLK_MUX,
		.flags = CLK_SET_RATE_NO_REPARENT | CLK_SET_RATE_PARENT,
	},
	[IMX95_CLK_NETCMIX_ENETC1_RMII] = {
		.name = "enetc1_rmii_sel",
		.parent_names = (const char *[]){"ext_enetref", "enetref"},
		.num_parents = 2,
		.reg = 4,
		.bit_idx = 10,
		.bit_width = 1,
		.type = CLK_MUX,
		.flags = CLK_SET_RATE_NO_REPARENT | CLK_SET_RATE_PARENT,
	},
};

static const struct imx95_blk_ctl_dev_data netcmix_dev_data = {
	.num_clks = ARRAY_SIZE(netxmix_clk_dev_data),
	.clk_dev_data = netxmix_clk_dev_data,
	.clk_reg_offset = 0,
};

static const struct imx95_blk_ctl_clk_dev_data hsio_blk_ctl_clk_dev_data[] = {
	[0] = {
		.name = "hsio_blk_ctl_clk",
		.parent_names = (const char *[]){ "hsio_pll", },
		.num_parents = 1,
		.reg = 0,
		.bit_idx = 6,
		.bit_width = 1,
		.type = CLK_GATE,
		.flags = CLK_SET_RATE_PARENT,
	}
};

static const struct imx95_blk_ctl_dev_data hsio_blk_ctl_dev_data = {
	.num_clks = 1,
	.clk_dev_data = hsio_blk_ctl_clk_dev_data,
	.clk_reg_offset = 0,
};

static int imx95_bc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct imx95_blk_ctl_dev_data *bc_data;
	struct imx95_blk_ctl *bc;
	struct clk_hw_onecell_data *clk_hw_data;
	struct clk_hw **hws;
	void __iomem *base;
	int i, ret;

	bc = devm_kzalloc(dev, sizeof(*bc), GFP_KERNEL);
	if (!bc)
		return -ENOMEM;
	bc->dev = dev;
	dev_set_drvdata(&pdev->dev, bc);

	spin_lock_init(&bc->lock);

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	bc->base = base;
	bc->clk_apb = devm_clk_get(dev, NULL);
	if (IS_ERR(bc->clk_apb))
		return dev_err_probe(dev, PTR_ERR(bc->clk_apb), "failed to get APB clock\n");

	ret = clk_prepare_enable(bc->clk_apb);
	if (ret) {
		dev_err(dev, "failed to enable apb clock: %d\n", ret);
		return ret;
	}

	bc_data = of_device_get_match_data(dev);
	if (!bc_data)
		return devm_of_platform_populate(dev);

	clk_hw_data = devm_kzalloc(dev, struct_size(clk_hw_data, hws, bc_data->num_clks),
				   GFP_KERNEL);
	if (!clk_hw_data)
		return -ENOMEM;

	if (bc_data->rpm_enabled)
		pm_runtime_enable(&pdev->dev);

	clk_hw_data->num = bc_data->num_clks;
	hws = clk_hw_data->hws;

	for (i = 0; i < bc_data->num_clks; i++) {
		const struct imx95_blk_ctl_clk_dev_data *data = &bc_data->clk_dev_data[i];
		void __iomem *reg = base + data->reg;

		if (data->type == CLK_MUX) {
			hws[i] = clk_hw_register_mux(dev, data->name, data->parent_names,
						     data->num_parents, data->flags, reg,
						     data->bit_idx, data->bit_width,
						     data->flags2, &bc->lock);
		} else if (data->type == CLK_DIVIDER) {
			hws[i] = clk_hw_register_divider(dev, data->name, data->parent_names[0],
							 data->flags, reg, data->bit_idx,
							 data->bit_width, data->flags2, &bc->lock);
		} else {
			hws[i] = clk_hw_register_gate(dev, data->name, data->parent_names[0],
						      data->flags, reg, data->bit_idx,
						      data->flags2, &bc->lock);
		}
		if (IS_ERR(hws[i])) {
			ret = PTR_ERR(hws[i]);
			dev_err(dev, "failed to register: %s:%d\n", data->name, ret);
			goto cleanup;
		}
	}

	ret = of_clk_add_hw_provider(dev->of_node, of_clk_hw_onecell_get, clk_hw_data);
	if (ret)
		goto cleanup;

	ret = devm_of_platform_populate(dev);
	if (ret) {
		of_clk_del_provider(dev->of_node);
		goto cleanup;
	}

	if (pm_runtime_enabled(bc->dev))
		clk_disable_unprepare(bc->clk_apb);

	return 0;

cleanup:
	for (i = 0; i < bc_data->num_clks; i++) {
		if (IS_ERR_OR_NULL(hws[i]))
			continue;
		clk_hw_unregister(hws[i]);
	}

	if (bc_data->rpm_enabled)
		pm_runtime_disable(&pdev->dev);

	return ret;
}

#ifdef CONFIG_PM
static int imx95_bc_runtime_suspend(struct device *dev)
{
	struct imx95_blk_ctl *bc = dev_get_drvdata(dev);

	clk_disable_unprepare(bc->clk_apb);
	return 0;
}

static int imx95_bc_runtime_resume(struct device *dev)
{
	struct imx95_blk_ctl *bc = dev_get_drvdata(dev);

	return clk_prepare_enable(bc->clk_apb);
}
#endif

#ifdef CONFIG_PM_SLEEP
static int imx95_bc_suspend(struct device *dev)
{
	struct imx95_blk_ctl *bc = dev_get_drvdata(dev);
	const struct imx95_blk_ctl_dev_data *bc_data;
	int ret;

	bc_data = of_device_get_match_data(dev);
	if (!bc_data)
		return 0;

	if (bc_data->rpm_enabled) {
		ret = pm_runtime_get_sync(bc->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(bc->dev);
			return ret;
		}
	}

	bc->clk_reg_restore = readl(bc->base + bc_data->clk_reg_offset);

	return 0;
}

static int imx95_bc_resume(struct device *dev)
{
	struct imx95_blk_ctl *bc = dev_get_drvdata(dev);
	const struct imx95_blk_ctl_dev_data *bc_data;

	bc_data = of_device_get_match_data(dev);
	if (!bc_data)
		return 0;

	writel(bc->clk_reg_restore, bc->base + bc_data->clk_reg_offset);

	if (bc_data->rpm_enabled)
		pm_runtime_put(bc->dev);

	return 0;
}
#endif

static const struct dev_pm_ops imx95_bc_pm_ops = {
	SET_RUNTIME_PM_OPS(imx95_bc_runtime_suspend, imx95_bc_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(imx95_bc_suspend, imx95_bc_resume)
};

static const struct of_device_id imx95_bc_of_match[] = {
	{ .compatible = "nxp,imx95-camera-csr", .data = &camblk_dev_data },
	{ .compatible = "nxp,imx95-display-master-csr", },
	{ .compatible = "nxp,imx95-lvds-csr", .data = &lvds_csr_dev_data },
	{ .compatible = "nxp,imx95-display-csr", .data = &dispmix_csr_dev_data },
	{ .compatible = "nxp,imx95-hsio-blk-ctl", .data = &hsio_blk_ctl_dev_data },
	{ .compatible = "nxp,imx95-vpu-csr", .data = &vpublk_dev_data },
	{ .compatible = "nxp,imx95-netcmix-blk-ctrl", .data = &netcmix_dev_data},
	{ /* Sentinel */ },
};
MODULE_DEVICE_TABLE(of, imx95_bc_of_match);

static struct platform_driver imx95_bc_driver = {
	.probe = imx95_bc_probe,
	.driver = {
		.name = "imx95-blk-ctl",
		.of_match_table = imx95_bc_of_match,
		.pm = &imx95_bc_pm_ops,
	},
};
module_platform_driver(imx95_bc_driver);

MODULE_DESCRIPTION("NXP i.MX95 blk ctl driver");
MODULE_AUTHOR("Peng Fan <peng.fan@nxp.com>");
MODULE_LICENSE("GPL");
