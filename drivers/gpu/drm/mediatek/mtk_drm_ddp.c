// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015 MediaTek Inc.
 */

#include <linux/clk.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include "mtk_drm_ddp.h"
#include "mtk_drm_ddp_comp.h"

#define MT2701_DISP_MUTEX0_MOD0			0x2c
#define MT2701_DISP_MUTEX0_SOF0			0x30

#define DISP_REG_MUTEX_EN(n)			(0x20 + 0x20 * (n))
#define DISP_REG_MUTEX(n)			(0x24 + 0x20 * (n))
#define DISP_REG_MUTEX_RST(n)			(0x28 + 0x20 * (n))
#define DISP_REG_MUTEX_MOD(mutex_mod_reg, n)	(mutex_mod_reg + 0x20 * (n))
#define DISP_REG_MUTEX_SOF(mutex_sof_reg, n)	(mutex_sof_reg + 0x20 * (n))
#define DISP_REG_MUTEX_MOD2(n)			(0x34 + 0x20 * (n))

#define INT_MUTEX				BIT(1)

#define MT8173_MUTEX_MOD_DISP_OVL0		11
#define MT8173_MUTEX_MOD_DISP_OVL1		12
#define MT8173_MUTEX_MOD_DISP_RDMA0		13
#define MT8173_MUTEX_MOD_DISP_RDMA1		14
#define MT8173_MUTEX_MOD_DISP_RDMA2		15
#define MT8173_MUTEX_MOD_DISP_WDMA0		16
#define MT8173_MUTEX_MOD_DISP_WDMA1		17
#define MT8173_MUTEX_MOD_DISP_COLOR0		18
#define MT8173_MUTEX_MOD_DISP_COLOR1		19
#define MT8173_MUTEX_MOD_DISP_AAL		20
#define MT8173_MUTEX_MOD_DISP_GAMMA		21
#define MT8173_MUTEX_MOD_DISP_UFOE		22
#define MT8173_MUTEX_MOD_DISP_PWM0		23
#define MT8173_MUTEX_MOD_DISP_PWM1		24
#define MT8173_MUTEX_MOD_DISP_OD		25

#define MT2712_MUTEX_MOD_DISP_PWM2		10
#define MT2712_MUTEX_MOD_DISP_OVL0		11
#define MT2712_MUTEX_MOD_DISP_OVL1		12
#define MT2712_MUTEX_MOD_DISP_RDMA0		13
#define MT2712_MUTEX_MOD_DISP_RDMA1		14
#define MT2712_MUTEX_MOD_DISP_RDMA2		15
#define MT2712_MUTEX_MOD_DISP_WDMA0		16
#define MT2712_MUTEX_MOD_DISP_WDMA1		17
#define MT2712_MUTEX_MOD_DISP_COLOR0		18
#define MT2712_MUTEX_MOD_DISP_COLOR1		19
#define MT2712_MUTEX_MOD_DISP_AAL0		20
#define MT2712_MUTEX_MOD_DISP_UFOE		22
#define MT2712_MUTEX_MOD_DISP_PWM0		23
#define MT2712_MUTEX_MOD_DISP_PWM1		24
#define MT2712_MUTEX_MOD_DISP_OD0		25
#define MT2712_MUTEX_MOD2_DISP_AAL1		33
#define MT2712_MUTEX_MOD2_DISP_OD1		34

#define MT2701_MUTEX_MOD_DISP_OVL		3
#define MT2701_MUTEX_MOD_DISP_WDMA		6
#define MT2701_MUTEX_MOD_DISP_COLOR		7
#define MT2701_MUTEX_MOD_DISP_BLS		9
#define MT2701_MUTEX_MOD_DISP_RDMA0		10
#define MT2701_MUTEX_MOD_DISP_RDMA1		12

#define MUTEX_SOF_SINGLE_MODE		0
#define MUTEX_SOF_DSI0			1
#define MUTEX_SOF_DSI1			2
#define MUTEX_SOF_DPI0			3
#define MUTEX_SOF_DPI1			4
#define MUTEX_SOF_DSI2			5
#define MUTEX_SOF_DSI3			6


struct mtk_disp_mutex {
	int id;
	bool claimed;
};

enum mtk_ddp_mutex_sof_id {
	DDP_MUTEX_SOF_SINGLE_MODE,
	DDP_MUTEX_SOF_DSI0,
	DDP_MUTEX_SOF_DSI1,
	DDP_MUTEX_SOF_DPI0,
	DDP_MUTEX_SOF_DPI1,
	DDP_MUTEX_SOF_DSI2,
	DDP_MUTEX_SOF_DSI3,
};

struct mtk_ddp_data {
	const unsigned int *mutex_mod;
	const unsigned int *mutex_sof;
	const unsigned int mutex_mod_reg;
	const unsigned int mutex_sof_reg;
	const bool no_clk;
};

struct mtk_ddp {
	struct device			*dev;
	struct clk			*clk;
	void __iomem			*regs;
	struct mtk_disp_mutex		mutex[10];
	const struct mtk_ddp_data	*data;
};

static const unsigned int mt2701_mutex_mod[DDP_COMPONENT_ID_MAX] = {
	[DDP_COMPONENT_BLS] = MT2701_MUTEX_MOD_DISP_BLS,
	[DDP_COMPONENT_COLOR0] = MT2701_MUTEX_MOD_DISP_COLOR,
	[DDP_COMPONENT_OVL0] = MT2701_MUTEX_MOD_DISP_OVL,
	[DDP_COMPONENT_RDMA0] = MT2701_MUTEX_MOD_DISP_RDMA0,
	[DDP_COMPONENT_RDMA1] = MT2701_MUTEX_MOD_DISP_RDMA1,
	[DDP_COMPONENT_WDMA0] = MT2701_MUTEX_MOD_DISP_WDMA,
};

static const unsigned int mt2712_mutex_mod[DDP_COMPONENT_ID_MAX] = {
	[DDP_COMPONENT_AAL0] = MT2712_MUTEX_MOD_DISP_AAL0,
	[DDP_COMPONENT_AAL1] = MT2712_MUTEX_MOD2_DISP_AAL1,
	[DDP_COMPONENT_COLOR0] = MT2712_MUTEX_MOD_DISP_COLOR0,
	[DDP_COMPONENT_COLOR1] = MT2712_MUTEX_MOD_DISP_COLOR1,
	[DDP_COMPONENT_OD0] = MT2712_MUTEX_MOD_DISP_OD0,
	[DDP_COMPONENT_OD1] = MT2712_MUTEX_MOD2_DISP_OD1,
	[DDP_COMPONENT_OVL0] = MT2712_MUTEX_MOD_DISP_OVL0,
	[DDP_COMPONENT_OVL1] = MT2712_MUTEX_MOD_DISP_OVL1,
	[DDP_COMPONENT_PWM0] = MT2712_MUTEX_MOD_DISP_PWM0,
	[DDP_COMPONENT_PWM1] = MT2712_MUTEX_MOD_DISP_PWM1,
	[DDP_COMPONENT_PWM2] = MT2712_MUTEX_MOD_DISP_PWM2,
	[DDP_COMPONENT_RDMA0] = MT2712_MUTEX_MOD_DISP_RDMA0,
	[DDP_COMPONENT_RDMA1] = MT2712_MUTEX_MOD_DISP_RDMA1,
	[DDP_COMPONENT_RDMA2] = MT2712_MUTEX_MOD_DISP_RDMA2,
	[DDP_COMPONENT_UFOE] = MT2712_MUTEX_MOD_DISP_UFOE,
	[DDP_COMPONENT_WDMA0] = MT2712_MUTEX_MOD_DISP_WDMA0,
	[DDP_COMPONENT_WDMA1] = MT2712_MUTEX_MOD_DISP_WDMA1,
};

static const unsigned int mt8173_mutex_mod[DDP_COMPONENT_ID_MAX] = {
	[DDP_COMPONENT_AAL0] = MT8173_MUTEX_MOD_DISP_AAL,
	[DDP_COMPONENT_COLOR0] = MT8173_MUTEX_MOD_DISP_COLOR0,
	[DDP_COMPONENT_COLOR1] = MT8173_MUTEX_MOD_DISP_COLOR1,
	[DDP_COMPONENT_GAMMA] = MT8173_MUTEX_MOD_DISP_GAMMA,
	[DDP_COMPONENT_OD0] = MT8173_MUTEX_MOD_DISP_OD,
	[DDP_COMPONENT_OVL0] = MT8173_MUTEX_MOD_DISP_OVL0,
	[DDP_COMPONENT_OVL1] = MT8173_MUTEX_MOD_DISP_OVL1,
	[DDP_COMPONENT_PWM0] = MT8173_MUTEX_MOD_DISP_PWM0,
	[DDP_COMPONENT_PWM1] = MT8173_MUTEX_MOD_DISP_PWM1,
	[DDP_COMPONENT_RDMA0] = MT8173_MUTEX_MOD_DISP_RDMA0,
	[DDP_COMPONENT_RDMA1] = MT8173_MUTEX_MOD_DISP_RDMA1,
	[DDP_COMPONENT_RDMA2] = MT8173_MUTEX_MOD_DISP_RDMA2,
	[DDP_COMPONENT_UFOE] = MT8173_MUTEX_MOD_DISP_UFOE,
	[DDP_COMPONENT_WDMA0] = MT8173_MUTEX_MOD_DISP_WDMA0,
	[DDP_COMPONENT_WDMA1] = MT8173_MUTEX_MOD_DISP_WDMA1,
};

static const unsigned int mt2712_mutex_sof[DDP_MUTEX_SOF_DSI3 + 1] = {
	[DDP_MUTEX_SOF_SINGLE_MODE] = MUTEX_SOF_SINGLE_MODE,
	[DDP_MUTEX_SOF_DSI0] = MUTEX_SOF_DSI0,
	[DDP_MUTEX_SOF_DSI1] = MUTEX_SOF_DSI1,
	[DDP_MUTEX_SOF_DPI0] = MUTEX_SOF_DPI0,
	[DDP_MUTEX_SOF_DPI1] = MUTEX_SOF_DPI1,
	[DDP_MUTEX_SOF_DSI2] = MUTEX_SOF_DSI2,
	[DDP_MUTEX_SOF_DSI3] = MUTEX_SOF_DSI3,
};

static const struct mtk_ddp_data mt2701_ddp_driver_data = {
	.mutex_mod = mt2701_mutex_mod,
	.mutex_sof = mt2712_mutex_sof,
	.mutex_mod_reg = MT2701_DISP_MUTEX0_MOD0,
	.mutex_sof_reg = MT2701_DISP_MUTEX0_SOF0,
};

static const struct mtk_ddp_data mt2712_ddp_driver_data = {
	.mutex_mod = mt2712_mutex_mod,
	.mutex_sof = mt2712_mutex_sof,
	.mutex_mod_reg = MT2701_DISP_MUTEX0_MOD0,
	.mutex_sof_reg = MT2701_DISP_MUTEX0_SOF0,
};

static const struct mtk_ddp_data mt8173_ddp_driver_data = {
	.mutex_mod = mt8173_mutex_mod,
	.mutex_sof = mt2712_mutex_sof,
	.mutex_mod_reg = MT2701_DISP_MUTEX0_MOD0,
	.mutex_sof_reg = MT2701_DISP_MUTEX0_SOF0,
};

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
	unsigned int sof_id;
	unsigned int offset;

	WARN_ON(&ddp->mutex[mutex->id] != mutex);

	switch (id) {
	case DDP_COMPONENT_DSI0:
		sof_id = DDP_MUTEX_SOF_DSI0;
		break;
	case DDP_COMPONENT_DSI1:
		sof_id = DDP_MUTEX_SOF_DSI0;
		break;
	case DDP_COMPONENT_DSI2:
		sof_id = DDP_MUTEX_SOF_DSI2;
		break;
	case DDP_COMPONENT_DSI3:
		sof_id = DDP_MUTEX_SOF_DSI3;
		break;
	case DDP_COMPONENT_DPI0:
		sof_id = DDP_MUTEX_SOF_DPI0;
		break;
	case DDP_COMPONENT_DPI1:
		sof_id = DDP_MUTEX_SOF_DPI1;
		break;
	default:
		if (ddp->data->mutex_mod[id] < 32) {
			offset = DISP_REG_MUTEX_MOD(ddp->data->mutex_mod_reg,
						    mutex->id);
			reg = readl_relaxed(ddp->regs + offset);
			reg |= 1 << ddp->data->mutex_mod[id];
			writel_relaxed(reg, ddp->regs + offset);
		} else {
			offset = DISP_REG_MUTEX_MOD2(mutex->id);
			reg = readl_relaxed(ddp->regs + offset);
			reg |= 1 << (ddp->data->mutex_mod[id] - 32);
			writel_relaxed(reg, ddp->regs + offset);
		}
		return;
	}

	writel_relaxed(ddp->data->mutex_sof[sof_id],
		       ddp->regs +
		       DISP_REG_MUTEX_SOF(ddp->data->mutex_sof_reg, mutex->id));
}

void mtk_disp_mutex_remove_comp(struct mtk_disp_mutex *mutex,
				enum mtk_ddp_comp_id id)
{
	struct mtk_ddp *ddp = container_of(mutex, struct mtk_ddp,
					   mutex[mutex->id]);
	unsigned int reg;
	unsigned int offset;

	WARN_ON(&ddp->mutex[mutex->id] != mutex);

	switch (id) {
	case DDP_COMPONENT_DSI0:
	case DDP_COMPONENT_DSI1:
	case DDP_COMPONENT_DSI2:
	case DDP_COMPONENT_DSI3:
	case DDP_COMPONENT_DPI0:
	case DDP_COMPONENT_DPI1:
		writel_relaxed(MUTEX_SOF_SINGLE_MODE,
			       ddp->regs +
			       DISP_REG_MUTEX_SOF(ddp->data->mutex_sof_reg,
						  mutex->id));
		break;
	default:
		if (ddp->data->mutex_mod[id] < 32) {
			offset = DISP_REG_MUTEX_MOD(ddp->data->mutex_mod_reg,
						    mutex->id);
			reg = readl_relaxed(ddp->regs + offset);
			reg &= ~(1 << ddp->data->mutex_mod[id]);
			writel_relaxed(reg, ddp->regs + offset);
		} else {
			offset = DISP_REG_MUTEX_MOD2(mutex->id);
			reg = readl_relaxed(ddp->regs + offset);
			reg &= ~(1 << (ddp->data->mutex_mod[id] - 32));
			writel_relaxed(reg, ddp->regs + offset);
		}
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

void mtk_disp_mutex_acquire(struct mtk_disp_mutex *mutex)
{
	struct mtk_ddp *ddp = container_of(mutex, struct mtk_ddp,
					   mutex[mutex->id]);
	u32 tmp;

	writel(1, ddp->regs + DISP_REG_MUTEX_EN(mutex->id));
	writel(1, ddp->regs + DISP_REG_MUTEX(mutex->id));
	if (readl_poll_timeout_atomic(ddp->regs + DISP_REG_MUTEX(mutex->id),
				      tmp, tmp & INT_MUTEX, 1, 10000))
		pr_err("could not acquire mutex %d\n", mutex->id);
}

void mtk_disp_mutex_release(struct mtk_disp_mutex *mutex)
{
	struct mtk_ddp *ddp = container_of(mutex, struct mtk_ddp,
					   mutex[mutex->id]);

	writel(0, ddp->regs + DISP_REG_MUTEX(mutex->id));
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

	ddp->data = of_device_get_match_data(dev);

	if (!ddp->data->no_clk) {
		ddp->clk = devm_clk_get(dev, NULL);
		if (IS_ERR(ddp->clk)) {
			if (PTR_ERR(ddp->clk) != -EPROBE_DEFER)
				dev_err(dev, "Failed to get clock\n");
			return PTR_ERR(ddp->clk);
		}
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
	{ .compatible = "mediatek,mt2701-disp-mutex",
	  .data = &mt2701_ddp_driver_data},
	{ .compatible = "mediatek,mt2712-disp-mutex",
	  .data = &mt2712_ddp_driver_data},
	{ .compatible = "mediatek,mt8173-disp-mutex",
	  .data = &mt8173_ddp_driver_data},
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
