// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014 MediaTek Inc.
 * Author: James Liao <jamesjj.liao@mediatek.com>
 */

#include <linux/device.h>
#include <linux/io.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/soc/mediatek/mtk-mmsys.h>

#define DISP_REG_CONFIG_DISP_OVL0_MOUT_EN	0x040
#define DISP_REG_CONFIG_DISP_OVL1_MOUT_EN	0x044
#define DISP_REG_CONFIG_DISP_OD_MOUT_EN		0x048
#define DISP_REG_CONFIG_DISP_GAMMA_MOUT_EN	0x04c
#define DISP_REG_CONFIG_DISP_UFOE_MOUT_EN	0x050
#define DISP_REG_CONFIG_DISP_COLOR0_SEL_IN	0x084
#define DISP_REG_CONFIG_DISP_COLOR1_SEL_IN	0x088
#define DISP_REG_CONFIG_DSIE_SEL_IN		0x0a4
#define DISP_REG_CONFIG_DSIO_SEL_IN		0x0a8
#define DISP_REG_CONFIG_DPI_SEL_IN		0x0ac
#define DISP_REG_CONFIG_DISP_RDMA2_SOUT		0x0b8
#define DISP_REG_CONFIG_DISP_RDMA0_SOUT_EN	0x0c4
#define DISP_REG_CONFIG_DISP_RDMA1_SOUT_EN	0x0c8
#define DISP_REG_CONFIG_MMSYS_CG_CON0		0x100

#define DISP_REG_CONFIG_DISP_OVL_MOUT_EN	0x030
#define DISP_REG_CONFIG_OUT_SEL			0x04c
#define DISP_REG_CONFIG_DSI_SEL			0x050
#define DISP_REG_CONFIG_DPI_SEL			0x064

#define OVL0_MOUT_EN_COLOR0			0x1
#define OD_MOUT_EN_RDMA0			0x1
#define OD1_MOUT_EN_RDMA1			BIT(16)
#define UFOE_MOUT_EN_DSI0			0x1
#define COLOR0_SEL_IN_OVL0			0x1
#define OVL1_MOUT_EN_COLOR1			0x1
#define GAMMA_MOUT_EN_RDMA1			0x1
#define RDMA0_SOUT_DPI0				0x2
#define RDMA0_SOUT_DPI1				0x3
#define RDMA0_SOUT_DSI1				0x1
#define RDMA0_SOUT_DSI2				0x4
#define RDMA0_SOUT_DSI3				0x5
#define RDMA1_SOUT_DPI0				0x2
#define RDMA1_SOUT_DPI1				0x3
#define RDMA1_SOUT_DSI1				0x1
#define RDMA1_SOUT_DSI2				0x4
#define RDMA1_SOUT_DSI3				0x5
#define RDMA2_SOUT_DPI0				0x2
#define RDMA2_SOUT_DPI1				0x3
#define RDMA2_SOUT_DSI1				0x1
#define RDMA2_SOUT_DSI2				0x4
#define RDMA2_SOUT_DSI3				0x5
#define DPI0_SEL_IN_RDMA1			0x1
#define DPI0_SEL_IN_RDMA2			0x3
#define DPI1_SEL_IN_RDMA1			(0x1 << 8)
#define DPI1_SEL_IN_RDMA2			(0x3 << 8)
#define DSI0_SEL_IN_RDMA1			0x1
#define DSI0_SEL_IN_RDMA2			0x4
#define DSI1_SEL_IN_RDMA1			0x1
#define DSI1_SEL_IN_RDMA2			0x4
#define DSI2_SEL_IN_RDMA1			(0x1 << 16)
#define DSI2_SEL_IN_RDMA2			(0x4 << 16)
#define DSI3_SEL_IN_RDMA1			(0x1 << 16)
#define DSI3_SEL_IN_RDMA2			(0x4 << 16)
#define COLOR1_SEL_IN_OVL1			0x1

#define OVL_MOUT_EN_RDMA			0x1
#define BLS_TO_DSI_RDMA1_TO_DPI1		0x8
#define BLS_TO_DPI_RDMA1_TO_DSI			0x2
#define DSI_SEL_IN_BLS				0x0
#define DPI_SEL_IN_BLS				0x0
#define DSI_SEL_IN_RDMA				0x1

struct mtk_mmsys_driver_data {
	const char *clk_driver;
};

static const struct mtk_mmsys_driver_data mt2701_mmsys_driver_data = {
	.clk_driver = "clk-mt2701-mm",
};

static const struct mtk_mmsys_driver_data mt2712_mmsys_driver_data = {
	.clk_driver = "clk-mt2712-mm",
};

static const struct mtk_mmsys_driver_data mt6779_mmsys_driver_data = {
	.clk_driver = "clk-mt6779-mm",
};

static const struct mtk_mmsys_driver_data mt6797_mmsys_driver_data = {
	.clk_driver = "clk-mt6797-mm",
};

static const struct mtk_mmsys_driver_data mt8173_mmsys_driver_data = {
	.clk_driver = "clk-mt8173-mm",
};

static const struct mtk_mmsys_driver_data mt8183_mmsys_driver_data = {
	.clk_driver = "clk-mt8183-mm",
};

static unsigned int mtk_mmsys_ddp_mout_en(enum mtk_ddp_comp_id cur,
					  enum mtk_ddp_comp_id next,
					  unsigned int *addr)
{
	unsigned int value;

	if (cur == DDP_COMPONENT_OVL0 && next == DDP_COMPONENT_COLOR0) {
		*addr = DISP_REG_CONFIG_DISP_OVL0_MOUT_EN;
		value = OVL0_MOUT_EN_COLOR0;
	} else if (cur == DDP_COMPONENT_OVL0 && next == DDP_COMPONENT_RDMA0) {
		*addr = DISP_REG_CONFIG_DISP_OVL_MOUT_EN;
		value = OVL_MOUT_EN_RDMA;
	} else if (cur == DDP_COMPONENT_OD0 && next == DDP_COMPONENT_RDMA0) {
		*addr = DISP_REG_CONFIG_DISP_OD_MOUT_EN;
		value = OD_MOUT_EN_RDMA0;
	} else if (cur == DDP_COMPONENT_UFOE && next == DDP_COMPONENT_DSI0) {
		*addr = DISP_REG_CONFIG_DISP_UFOE_MOUT_EN;
		value = UFOE_MOUT_EN_DSI0;
	} else if (cur == DDP_COMPONENT_OVL1 && next == DDP_COMPONENT_COLOR1) {
		*addr = DISP_REG_CONFIG_DISP_OVL1_MOUT_EN;
		value = OVL1_MOUT_EN_COLOR1;
	} else if (cur == DDP_COMPONENT_GAMMA && next == DDP_COMPONENT_RDMA1) {
		*addr = DISP_REG_CONFIG_DISP_GAMMA_MOUT_EN;
		value = GAMMA_MOUT_EN_RDMA1;
	} else if (cur == DDP_COMPONENT_OD1 && next == DDP_COMPONENT_RDMA1) {
		*addr = DISP_REG_CONFIG_DISP_OD_MOUT_EN;
		value = OD1_MOUT_EN_RDMA1;
	} else if (cur == DDP_COMPONENT_RDMA0 && next == DDP_COMPONENT_DPI0) {
		*addr = DISP_REG_CONFIG_DISP_RDMA0_SOUT_EN;
		value = RDMA0_SOUT_DPI0;
	} else if (cur == DDP_COMPONENT_RDMA0 && next == DDP_COMPONENT_DPI1) {
		*addr = DISP_REG_CONFIG_DISP_RDMA0_SOUT_EN;
		value = RDMA0_SOUT_DPI1;
	} else if (cur == DDP_COMPONENT_RDMA0 && next == DDP_COMPONENT_DSI1) {
		*addr = DISP_REG_CONFIG_DISP_RDMA0_SOUT_EN;
		value = RDMA0_SOUT_DSI1;
	} else if (cur == DDP_COMPONENT_RDMA0 && next == DDP_COMPONENT_DSI2) {
		*addr = DISP_REG_CONFIG_DISP_RDMA0_SOUT_EN;
		value = RDMA0_SOUT_DSI2;
	} else if (cur == DDP_COMPONENT_RDMA0 && next == DDP_COMPONENT_DSI3) {
		*addr = DISP_REG_CONFIG_DISP_RDMA0_SOUT_EN;
		value = RDMA0_SOUT_DSI3;
	} else if (cur == DDP_COMPONENT_RDMA1 && next == DDP_COMPONENT_DSI1) {
		*addr = DISP_REG_CONFIG_DISP_RDMA1_SOUT_EN;
		value = RDMA1_SOUT_DSI1;
	} else if (cur == DDP_COMPONENT_RDMA1 && next == DDP_COMPONENT_DSI2) {
		*addr = DISP_REG_CONFIG_DISP_RDMA1_SOUT_EN;
		value = RDMA1_SOUT_DSI2;
	} else if (cur == DDP_COMPONENT_RDMA1 && next == DDP_COMPONENT_DSI3) {
		*addr = DISP_REG_CONFIG_DISP_RDMA1_SOUT_EN;
		value = RDMA1_SOUT_DSI3;
	} else if (cur == DDP_COMPONENT_RDMA1 && next == DDP_COMPONENT_DPI0) {
		*addr = DISP_REG_CONFIG_DISP_RDMA1_SOUT_EN;
		value = RDMA1_SOUT_DPI0;
	} else if (cur == DDP_COMPONENT_RDMA1 && next == DDP_COMPONENT_DPI1) {
		*addr = DISP_REG_CONFIG_DISP_RDMA1_SOUT_EN;
		value = RDMA1_SOUT_DPI1;
	} else if (cur == DDP_COMPONENT_RDMA2 && next == DDP_COMPONENT_DPI0) {
		*addr = DISP_REG_CONFIG_DISP_RDMA2_SOUT;
		value = RDMA2_SOUT_DPI0;
	} else if (cur == DDP_COMPONENT_RDMA2 && next == DDP_COMPONENT_DPI1) {
		*addr = DISP_REG_CONFIG_DISP_RDMA2_SOUT;
		value = RDMA2_SOUT_DPI1;
	} else if (cur == DDP_COMPONENT_RDMA2 && next == DDP_COMPONENT_DSI1) {
		*addr = DISP_REG_CONFIG_DISP_RDMA2_SOUT;
		value = RDMA2_SOUT_DSI1;
	} else if (cur == DDP_COMPONENT_RDMA2 && next == DDP_COMPONENT_DSI2) {
		*addr = DISP_REG_CONFIG_DISP_RDMA2_SOUT;
		value = RDMA2_SOUT_DSI2;
	} else if (cur == DDP_COMPONENT_RDMA2 && next == DDP_COMPONENT_DSI3) {
		*addr = DISP_REG_CONFIG_DISP_RDMA2_SOUT;
		value = RDMA2_SOUT_DSI3;
	} else {
		value = 0;
	}

	return value;
}

static unsigned int mtk_mmsys_ddp_sel_in(enum mtk_ddp_comp_id cur,
					 enum mtk_ddp_comp_id next,
					 unsigned int *addr)
{
	unsigned int value;

	if (cur == DDP_COMPONENT_OVL0 && next == DDP_COMPONENT_COLOR0) {
		*addr = DISP_REG_CONFIG_DISP_COLOR0_SEL_IN;
		value = COLOR0_SEL_IN_OVL0;
	} else if (cur == DDP_COMPONENT_RDMA1 && next == DDP_COMPONENT_DPI0) {
		*addr = DISP_REG_CONFIG_DPI_SEL_IN;
		value = DPI0_SEL_IN_RDMA1;
	} else if (cur == DDP_COMPONENT_RDMA1 && next == DDP_COMPONENT_DPI1) {
		*addr = DISP_REG_CONFIG_DPI_SEL_IN;
		value = DPI1_SEL_IN_RDMA1;
	} else if (cur == DDP_COMPONENT_RDMA1 && next == DDP_COMPONENT_DSI0) {
		*addr = DISP_REG_CONFIG_DSIE_SEL_IN;
		value = DSI0_SEL_IN_RDMA1;
	} else if (cur == DDP_COMPONENT_RDMA1 && next == DDP_COMPONENT_DSI1) {
		*addr = DISP_REG_CONFIG_DSIO_SEL_IN;
		value = DSI1_SEL_IN_RDMA1;
	} else if (cur == DDP_COMPONENT_RDMA1 && next == DDP_COMPONENT_DSI2) {
		*addr = DISP_REG_CONFIG_DSIE_SEL_IN;
		value = DSI2_SEL_IN_RDMA1;
	} else if (cur == DDP_COMPONENT_RDMA1 && next == DDP_COMPONENT_DSI3) {
		*addr = DISP_REG_CONFIG_DSIO_SEL_IN;
		value = DSI3_SEL_IN_RDMA1;
	} else if (cur == DDP_COMPONENT_RDMA2 && next == DDP_COMPONENT_DPI0) {
		*addr = DISP_REG_CONFIG_DPI_SEL_IN;
		value = DPI0_SEL_IN_RDMA2;
	} else if (cur == DDP_COMPONENT_RDMA2 && next == DDP_COMPONENT_DPI1) {
		*addr = DISP_REG_CONFIG_DPI_SEL_IN;
		value = DPI1_SEL_IN_RDMA2;
	} else if (cur == DDP_COMPONENT_RDMA2 && next == DDP_COMPONENT_DSI0) {
		*addr = DISP_REG_CONFIG_DSIE_SEL_IN;
		value = DSI0_SEL_IN_RDMA2;
	} else if (cur == DDP_COMPONENT_RDMA2 && next == DDP_COMPONENT_DSI1) {
		*addr = DISP_REG_CONFIG_DSIO_SEL_IN;
		value = DSI1_SEL_IN_RDMA2;
	} else if (cur == DDP_COMPONENT_RDMA2 && next == DDP_COMPONENT_DSI2) {
		*addr = DISP_REG_CONFIG_DSIE_SEL_IN;
		value = DSI2_SEL_IN_RDMA2;
	} else if (cur == DDP_COMPONENT_RDMA2 && next == DDP_COMPONENT_DSI3) {
		*addr = DISP_REG_CONFIG_DSIE_SEL_IN;
		value = DSI3_SEL_IN_RDMA2;
	} else if (cur == DDP_COMPONENT_OVL1 && next == DDP_COMPONENT_COLOR1) {
		*addr = DISP_REG_CONFIG_DISP_COLOR1_SEL_IN;
		value = COLOR1_SEL_IN_OVL1;
	} else if (cur == DDP_COMPONENT_BLS && next == DDP_COMPONENT_DSI0) {
		*addr = DISP_REG_CONFIG_DSI_SEL;
		value = DSI_SEL_IN_BLS;
	} else {
		value = 0;
	}

	return value;
}

static void mtk_mmsys_ddp_sout_sel(void __iomem *config_regs,
				   enum mtk_ddp_comp_id cur,
				   enum mtk_ddp_comp_id next)
{
	if (cur == DDP_COMPONENT_BLS && next == DDP_COMPONENT_DSI0) {
		writel_relaxed(BLS_TO_DSI_RDMA1_TO_DPI1,
			       config_regs + DISP_REG_CONFIG_OUT_SEL);
	} else if (cur == DDP_COMPONENT_BLS && next == DDP_COMPONENT_DPI0) {
		writel_relaxed(BLS_TO_DPI_RDMA1_TO_DSI,
			       config_regs + DISP_REG_CONFIG_OUT_SEL);
		writel_relaxed(DSI_SEL_IN_RDMA,
			       config_regs + DISP_REG_CONFIG_DSI_SEL);
		writel_relaxed(DPI_SEL_IN_BLS,
			       config_regs + DISP_REG_CONFIG_DPI_SEL);
	}
}

void mtk_mmsys_ddp_connect(struct device *dev,
			   enum mtk_ddp_comp_id cur,
			   enum mtk_ddp_comp_id next)
{
	void __iomem *config_regs = dev_get_drvdata(dev);
	unsigned int addr, value, reg;

	value = mtk_mmsys_ddp_mout_en(cur, next, &addr);
	if (value) {
		reg = readl_relaxed(config_regs + addr) | value;
		writel_relaxed(reg, config_regs + addr);
	}

	mtk_mmsys_ddp_sout_sel(config_regs, cur, next);

	value = mtk_mmsys_ddp_sel_in(cur, next, &addr);
	if (value) {
		reg = readl_relaxed(config_regs + addr) | value;
		writel_relaxed(reg, config_regs + addr);
	}
}
EXPORT_SYMBOL_GPL(mtk_mmsys_ddp_connect);

void mtk_mmsys_ddp_disconnect(struct device *dev,
			      enum mtk_ddp_comp_id cur,
			      enum mtk_ddp_comp_id next)
{
	void __iomem *config_regs = dev_get_drvdata(dev);
	unsigned int addr, value, reg;

	value = mtk_mmsys_ddp_mout_en(cur, next, &addr);
	if (value) {
		reg = readl_relaxed(config_regs + addr) & ~value;
		writel_relaxed(reg, config_regs + addr);
	}

	value = mtk_mmsys_ddp_sel_in(cur, next, &addr);
	if (value) {
		reg = readl_relaxed(config_regs + addr) & ~value;
		writel_relaxed(reg, config_regs + addr);
	}
}
EXPORT_SYMBOL_GPL(mtk_mmsys_ddp_disconnect);

static int mtk_mmsys_probe(struct platform_device *pdev)
{
	const struct mtk_mmsys_driver_data *data;
	struct device *dev = &pdev->dev;
	struct platform_device *clks;
	struct platform_device *drm;
	void __iomem *config_regs;
	struct resource *mem;
	int ret;

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	config_regs = devm_ioremap_resource(dev, mem);
	if (IS_ERR(config_regs)) {
		ret = PTR_ERR(config_regs);
		dev_err(dev, "Failed to ioremap mmsys-config resource: %d\n",
			ret);
		return ret;
	}

	platform_set_drvdata(pdev, config_regs);

	data = of_device_get_match_data(&pdev->dev);

	clks = platform_device_register_data(&pdev->dev, data->clk_driver,
					     PLATFORM_DEVID_AUTO, NULL, 0);
	if (IS_ERR(clks))
		return PTR_ERR(clks);

	drm = platform_device_register_data(&pdev->dev, "mediatek-drm",
					    PLATFORM_DEVID_AUTO, NULL, 0);
	if (IS_ERR(drm)) {
		platform_device_unregister(clks);
		return PTR_ERR(drm);
	}

	return 0;
}

static const struct of_device_id of_match_mtk_mmsys[] = {
	{
		.compatible = "mediatek,mt2701-mmsys",
		.data = &mt2701_mmsys_driver_data,
	},
	{
		.compatible = "mediatek,mt2712-mmsys",
		.data = &mt2712_mmsys_driver_data,
	},
	{
		.compatible = "mediatek,mt6779-mmsys",
		.data = &mt6779_mmsys_driver_data,
	},
	{
		.compatible = "mediatek,mt6797-mmsys",
		.data = &mt6797_mmsys_driver_data,
	},
	{
		.compatible = "mediatek,mt8173-mmsys",
		.data = &mt8173_mmsys_driver_data,
	},
	{
		.compatible = "mediatek,mt8183-mmsys",
		.data = &mt8183_mmsys_driver_data,
	},
	{ }
};

static struct platform_driver mtk_mmsys_drv = {
	.driver = {
		.name = "mtk-mmsys",
		.of_match_table = of_match_mtk_mmsys,
	},
	.probe = mtk_mmsys_probe,
};

builtin_platform_driver(mtk_mmsys_drv);
