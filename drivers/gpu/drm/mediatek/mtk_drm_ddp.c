/*
 * Copyright (c) 2015 MediaTek Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/clk.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include "mtk_drm_ddp.h"
#include "mtk_drm_ddp_comp.h"

#define DISP_REG_CONFIG_DISP_OVL0_MOUT_EN	0x040
#define DISP_REG_CONFIG_DISP_OVL1_MOUT_EN	0x044
#define DISP_REG_CONFIG_DISP_OD_MOUT_EN		0x048
#define DISP_REG_CONFIG_DISP_GAMMA_MOUT_EN	0x04c
#define DISP_REG_CONFIG_DISP_UFOE_MOUT_EN	0x050
#define DISP_REG_CONFIG_DISP_COLOR0_SEL_IN	0x084
#define DISP_REG_CONFIG_DISP_COLOR1_SEL_IN	0x088
#define DISP_REG_CONFIG_DPI_SEL_IN		0x0ac
#define DISP_REG_CONFIG_DISP_RDMA1_MOUT_EN	0x0c8
#define DISP_REG_CONFIG_MMSYS_CG_CON0		0x100

#define DISP_REG_MUTEX_EN(n)	(0x20 + 0x20 * (n))
#define DISP_REG_MUTEX_RST(n)	(0x28 + 0x20 * (n))
#define DISP_REG_MUTEX_MOD(n)	(0x2c + 0x20 * (n))
#define DISP_REG_MUTEX_SOF(n)	(0x30 + 0x20 * (n))

#define MUTEX_MOD_DISP_OVL0		BIT(11)
#define MUTEX_MOD_DISP_OVL1		BIT(12)
#define MUTEX_MOD_DISP_RDMA0		BIT(13)
#define MUTEX_MOD_DISP_RDMA1		BIT(14)
#define MUTEX_MOD_DISP_RDMA2		BIT(15)
#define MUTEX_MOD_DISP_WDMA0		BIT(16)
#define MUTEX_MOD_DISP_WDMA1		BIT(17)
#define MUTEX_MOD_DISP_COLOR0		BIT(18)
#define MUTEX_MOD_DISP_COLOR1		BIT(19)
#define MUTEX_MOD_DISP_AAL		BIT(20)
#define MUTEX_MOD_DISP_GAMMA		BIT(21)
#define MUTEX_MOD_DISP_UFOE		BIT(22)
#define MUTEX_MOD_DISP_PWM0		BIT(23)
#define MUTEX_MOD_DISP_PWM1		BIT(24)
#define MUTEX_MOD_DISP_OD		BIT(25)

#define MUTEX_SOF_SINGLE_MODE		0
#define MUTEX_SOF_DSI0			1
#define MUTEX_SOF_DSI1			2
#define MUTEX_SOF_DPI0			3

#define OVL0_MOUT_EN_COLOR0		0x1
#define OD_MOUT_EN_RDMA0		0x1
#define UFOE_MOUT_EN_DSI0		0x1
#define COLOR0_SEL_IN_OVL0		0x1
#define OVL1_MOUT_EN_COLOR1		0x1
#define GAMMA_MOUT_EN_RDMA1		0x1
#define RDMA1_MOUT_DPI0			0x2
#define DPI0_SEL_IN_RDMA1		0x1
#define COLOR1_SEL_IN_OVL1		0x1

struct mtk_disp_mutex {
	int id;
	bool claimed;
};

struct mtk_ddp {
	struct device			*dev;
	struct clk			*clk;
	void __iomem			*regs;
	struct mtk_disp_mutex		mutex[10];
};

static const unsigned int mutex_mod[DDP_COMPONENT_ID_MAX] = {
	[DDP_COMPONENT_AAL] = MUTEX_MOD_DISP_AAL,
	[DDP_COMPONENT_COLOR0] = MUTEX_MOD_DISP_COLOR0,
	[DDP_COMPONENT_COLOR1] = MUTEX_MOD_DISP_COLOR1,
	[DDP_COMPONENT_GAMMA] = MUTEX_MOD_DISP_GAMMA,
	[DDP_COMPONENT_OD] = MUTEX_MOD_DISP_OD,
	[DDP_COMPONENT_OVL0] = MUTEX_MOD_DISP_OVL0,
	[DDP_COMPONENT_OVL1] = MUTEX_MOD_DISP_OVL1,
	[DDP_COMPONENT_PWM0] = MUTEX_MOD_DISP_PWM0,
	[DDP_COMPONENT_PWM1] = MUTEX_MOD_DISP_PWM1,
	[DDP_COMPONENT_RDMA0] = MUTEX_MOD_DISP_RDMA0,
	[DDP_COMPONENT_RDMA1] = MUTEX_MOD_DISP_RDMA1,
	[DDP_COMPONENT_RDMA2] = MUTEX_MOD_DISP_RDMA2,
	[DDP_COMPONENT_UFOE] = MUTEX_MOD_DISP_UFOE,
	[DDP_COMPONENT_WDMA0] = MUTEX_MOD_DISP_WDMA0,
	[DDP_COMPONENT_WDMA1] = MUTEX_MOD_DISP_WDMA1,
};

static unsigned int mtk_ddp_mout_en(enum mtk_ddp_comp_id cur,
				    enum mtk_ddp_comp_id next,
				    unsigned int *addr)
{
	unsigned int value;

	if (cur == DDP_COMPONENT_OVL0 && next == DDP_COMPONENT_COLOR0) {
		*addr = DISP_REG_CONFIG_DISP_OVL0_MOUT_EN;
		value = OVL0_MOUT_EN_COLOR0;
	} else if (cur == DDP_COMPONENT_OD && next == DDP_COMPONENT_RDMA0) {
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
	} else if (cur == DDP_COMPONENT_RDMA1 && next == DDP_COMPONENT_DPI0) {
		*addr = DISP_REG_CONFIG_DISP_RDMA1_MOUT_EN;
		value = RDMA1_MOUT_DPI0;
	} else {
		value = 0;
	}

	return value;
}

static unsigned int mtk_ddp_sel_in(enum mtk_ddp_comp_id cur,
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
	} else if (cur == DDP_COMPONENT_OVL1 && next == DDP_COMPONENT_COLOR1) {
		*addr = DISP_REG_CONFIG_DISP_COLOR1_SEL_IN;
		value = COLOR1_SEL_IN_OVL1;
	} else {
		value = 0;
	}

	return value;
}

void mtk_ddp_add_comp_to_path(void __iomem *config_regs,
			      enum mtk_ddp_comp_id cur,
			      enum mtk_ddp_comp_id next)
{
	unsigned int addr, value, reg;

	value = mtk_ddp_mout_en(cur, next, &addr);
	if (value) {
		reg = readl_relaxed(config_regs + addr) | value;
		writel_relaxed(reg, config_regs + addr);
	}

	value = mtk_ddp_sel_in(cur, next, &addr);
	if (value) {
		reg = readl_relaxed(config_regs + addr) | value;
		writel_relaxed(reg, config_regs + addr);
	}
}

void mtk_ddp_remove_comp_from_path(void __iomem *config_regs,
				   enum mtk_ddp_comp_id cur,
				   enum mtk_ddp_comp_id next)
{
	unsigned int addr, value, reg;

	value = mtk_ddp_mout_en(cur, next, &addr);
	if (value) {
		reg = readl_relaxed(config_regs + addr) & ~value;
		writel_relaxed(reg, config_regs + addr);
	}

	value = mtk_ddp_sel_in(cur, next, &addr);
	if (value) {
		reg = readl_relaxed(config_regs + addr) & ~value;
		writel_relaxed(reg, config_regs + addr);
	}
}

struct mtk_disp_mutex *mtk_disp_mutex_get(struct device *dev, unsigned int id)
{
	struct mtk_ddp *ddp = dev_get_drvdata(dev);

	if (id >= 10)
		return ERR_PTR(-EINVAL);
	if (ddp->mutex[id].claimed)
		return ERR_PTR(-EBUSY);

	ddp->mutex[id].claimed = true;

	return &ddp->mutex[id];
}

void mtk_disp_mutex_put(struct mtk_disp_mutex *mutex)
{
	struct mtk_ddp *ddp = container_of(mutex, struct mtk_ddp,
					   mutex[mutex->id]);

	WARN_ON(&ddp->mutex[mutex->id] != mutex);

	mutex->claimed = false;
}

int mtk_disp_mutex_prepare(struct mtk_disp_mutex *mutex)
{
	struct mtk_ddp *ddp = container_of(mutex, struct mtk_ddp,
					   mutex[mutex->id]);
	return clk_prepare_enable(ddp->clk);
}

void mtk_disp_mutex_unprepare(struct mtk_disp_mutex *mutex)
{
	struct mtk_ddp *ddp = container_of(mutex, struct mtk_ddp,
					   mutex[mutex->id]);
	clk_disable_unprepare(ddp->clk);
}

void mtk_disp_mutex_add_comp(struct mtk_disp_mutex *mutex,
			     enum mtk_ddp_comp_id id)
{
	struct mtk_ddp *ddp = container_of(mutex, struct mtk_ddp,
					   mutex[mutex->id]);
	unsigned int reg;

	WARN_ON(&ddp->mutex[mutex->id] != mutex);

	switch (id) {
	case DDP_COMPONENT_DSI0:
		reg = MUTEX_SOF_DSI0;
		break;
	case DDP_COMPONENT_DSI1:
		reg = MUTEX_SOF_DSI0;
		break;
	case DDP_COMPONENT_DPI0:
		reg = MUTEX_SOF_DPI0;
		break;
	default:
		reg = readl_relaxed(ddp->regs + DISP_REG_MUTEX_MOD(mutex->id));
		reg |= mutex_mod[id];
		writel_relaxed(reg, ddp->regs + DISP_REG_MUTEX_MOD(mutex->id));
		return;
	}

	writel_relaxed(reg, ddp->regs + DISP_REG_MUTEX_SOF(mutex->id));
}

void mtk_disp_mutex_remove_comp(struct mtk_disp_mutex *mutex,
				enum mtk_ddp_comp_id id)
{
	struct mtk_ddp *ddp = container_of(mutex, struct mtk_ddp,
					   mutex[mutex->id]);
	unsigned int reg;

	WARN_ON(&ddp->mutex[mutex->id] != mutex);

	switch (id) {
	case DDP_COMPONENT_DSI0:
	case DDP_COMPONENT_DSI1:
	case DDP_COMPONENT_DPI0:
		writel_relaxed(MUTEX_SOF_SINGLE_MODE,
			       ddp->regs + DISP_REG_MUTEX_SOF(mutex->id));
		break;
	default:
		reg = readl_relaxed(ddp->regs + DISP_REG_MUTEX_MOD(mutex->id));
		reg &= ~mutex_mod[id];
		writel_relaxed(reg, ddp->regs + DISP_REG_MUTEX_MOD(mutex->id));
		break;
	}
}

void mtk_disp_mutex_enable(struct mtk_disp_mutex *mutex)
{
	struct mtk_ddp *ddp = container_of(mutex, struct mtk_ddp,
					   mutex[mutex->id]);

	WARN_ON(&ddp->mutex[mutex->id] != mutex);

	writel(1, ddp->regs + DISP_REG_MUTEX_EN(mutex->id));
}

void mtk_disp_mutex_disable(struct mtk_disp_mutex *mutex)
{
	struct mtk_ddp *ddp = container_of(mutex, struct mtk_ddp,
					   mutex[mutex->id]);

	WARN_ON(&ddp->mutex[mutex->id] != mutex);

	writel(0, ddp->regs + DISP_REG_MUTEX_EN(mutex->id));
}

static int mtk_ddp_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mtk_ddp *ddp;
	struct resource *regs;
	int i;

	ddp = devm_kzalloc(dev, sizeof(*ddp), GFP_KERNEL);
	if (!ddp)
		return -ENOMEM;

	for (i = 0; i < 10; i++)
		ddp->mutex[i].id = i;

	ddp->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(ddp->clk)) {
		dev_err(dev, "Failed to get clock\n");
		return PTR_ERR(ddp->clk);
	}

	regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	ddp->regs = devm_ioremap_resource(dev, regs);
	if (IS_ERR(ddp->regs)) {
		dev_err(dev, "Failed to map mutex registers\n");
		return PTR_ERR(ddp->regs);
	}

	platform_set_drvdata(pdev, ddp);

	return 0;
}

static int mtk_ddp_remove(struct platform_device *pdev)
{
	return 0;
}

static const struct of_device_id ddp_driver_dt_match[] = {
	{ .compatible = "mediatek,mt8173-disp-mutex" },
	{},
};
MODULE_DEVICE_TABLE(of, ddp_driver_dt_match);

struct platform_driver mtk_ddp_driver = {
	.probe		= mtk_ddp_probe,
	.remove		= mtk_ddp_remove,
	.driver		= {
		.name	= "mediatek-ddp",
		.owner	= THIS_MODULE,
		.of_match_table = ddp_driver_dt_match,
	},
};
