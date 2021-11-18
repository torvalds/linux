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
#include <linux/soc/mediatek/mtk-mmsys.h>
#include <linux/soc/mediatek/mtk-mutex.h>

#define MT2701_MUTEX0_MOD0			0x2c
#define MT2701_MUTEX0_SOF0			0x30
#define MT8183_MUTEX0_MOD0			0x30
#define MT8183_MUTEX0_SOF0			0x2c

#define DISP_REG_MUTEX_EN(n)			(0x20 + 0x20 * (n))
#define DISP_REG_MUTEX(n)			(0x24 + 0x20 * (n))
#define DISP_REG_MUTEX_RST(n)			(0x28 + 0x20 * (n))
#define DISP_REG_MUTEX_MOD(mutex_mod_reg, n)	(mutex_mod_reg + 0x20 * (n))
#define DISP_REG_MUTEX_SOF(mutex_sof_reg, n)	(mutex_sof_reg + 0x20 * (n))
#define DISP_REG_MUTEX_MOD2(n)			(0x34 + 0x20 * (n))

#define INT_MUTEX				BIT(1)

#define MT8167_MUTEX_MOD_DISP_PWM		1
#define MT8167_MUTEX_MOD_DISP_OVL0		6
#define MT8167_MUTEX_MOD_DISP_OVL1		7
#define MT8167_MUTEX_MOD_DISP_RDMA0		8
#define MT8167_MUTEX_MOD_DISP_RDMA1		9
#define MT8167_MUTEX_MOD_DISP_WDMA0		10
#define MT8167_MUTEX_MOD_DISP_CCORR		11
#define MT8167_MUTEX_MOD_DISP_COLOR		12
#define MT8167_MUTEX_MOD_DISP_AAL		13
#define MT8167_MUTEX_MOD_DISP_GAMMA		14
#define MT8167_MUTEX_MOD_DISP_DITHER		15
#define MT8167_MUTEX_MOD_DISP_UFOE		16

#define MT8192_MUTEX_MOD_DISP_OVL0		0
#define MT8192_MUTEX_MOD_DISP_OVL0_2L		1
#define MT8192_MUTEX_MOD_DISP_RDMA0		2
#define MT8192_MUTEX_MOD_DISP_COLOR0		4
#define MT8192_MUTEX_MOD_DISP_CCORR0		5
#define MT8192_MUTEX_MOD_DISP_AAL0		6
#define MT8192_MUTEX_MOD_DISP_GAMMA0		7
#define MT8192_MUTEX_MOD_DISP_POSTMASK0		8
#define MT8192_MUTEX_MOD_DISP_DITHER0		9
#define MT8192_MUTEX_MOD_DISP_OVL2_2L		16
#define MT8192_MUTEX_MOD_DISP_RDMA4		17

#define MT8183_MUTEX_MOD_DISP_RDMA0		0
#define MT8183_MUTEX_MOD_DISP_RDMA1		1
#define MT8183_MUTEX_MOD_DISP_OVL0		9
#define MT8183_MUTEX_MOD_DISP_OVL0_2L		10
#define MT8183_MUTEX_MOD_DISP_OVL1_2L		11
#define MT8183_MUTEX_MOD_DISP_WDMA0		12
#define MT8183_MUTEX_MOD_DISP_COLOR0		13
#define MT8183_MUTEX_MOD_DISP_CCORR0		14
#define MT8183_MUTEX_MOD_DISP_AAL0		15
#define MT8183_MUTEX_MOD_DISP_GAMMA0		16
#define MT8183_MUTEX_MOD_DISP_DITHER0		17

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

#define MT2712_MUTEX_SOF_SINGLE_MODE		0
#define MT2712_MUTEX_SOF_DSI0			1
#define MT2712_MUTEX_SOF_DSI1			2
#define MT2712_MUTEX_SOF_DPI0			3
#define MT2712_MUTEX_SOF_DPI1			4
#define MT2712_MUTEX_SOF_DSI2			5
#define MT2712_MUTEX_SOF_DSI3			6
#define MT8167_MUTEX_SOF_DPI0			2
#define MT8167_MUTEX_SOF_DPI1			3
#define MT8183_MUTEX_SOF_DSI0			1
#define MT8183_MUTEX_SOF_DPI0			2

#define MT8183_MUTEX_EOF_DSI0			(MT8183_MUTEX_SOF_DSI0 << 6)
#define MT8183_MUTEX_EOF_DPI0			(MT8183_MUTEX_SOF_DPI0 << 6)

struct mtk_mutex {
	int id;
	bool claimed;
};

enum mtk_mutex_sof_id {
	MUTEX_SOF_SINGLE_MODE,
	MUTEX_SOF_DSI0,
	MUTEX_SOF_DSI1,
	MUTEX_SOF_DPI0,
	MUTEX_SOF_DPI1,
	MUTEX_SOF_DSI2,
	MUTEX_SOF_DSI3,
};

struct mtk_mutex_data {
	const unsigned int *mutex_mod;
	const unsigned int *mutex_sof;
	const unsigned int mutex_mod_reg;
	const unsigned int mutex_sof_reg;
	const bool no_clk;
};

struct mtk_mutex_ctx {
	struct device			*dev;
	struct clk			*clk;
	void __iomem			*regs;
	struct mtk_mutex		mutex[10];
	const struct mtk_mutex_data	*data;
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

static const unsigned int mt8167_mutex_mod[DDP_COMPONENT_ID_MAX] = {
	[DDP_COMPONENT_AAL0] = MT8167_MUTEX_MOD_DISP_AAL,
	[DDP_COMPONENT_CCORR] = MT8167_MUTEX_MOD_DISP_CCORR,
	[DDP_COMPONENT_COLOR0] = MT8167_MUTEX_MOD_DISP_COLOR,
	[DDP_COMPONENT_DITHER] = MT8167_MUTEX_MOD_DISP_DITHER,
	[DDP_COMPONENT_GAMMA] = MT8167_MUTEX_MOD_DISP_GAMMA,
	[DDP_COMPONENT_OVL0] = MT8167_MUTEX_MOD_DISP_OVL0,
	[DDP_COMPONENT_OVL1] = MT8167_MUTEX_MOD_DISP_OVL1,
	[DDP_COMPONENT_PWM0] = MT8167_MUTEX_MOD_DISP_PWM,
	[DDP_COMPONENT_RDMA0] = MT8167_MUTEX_MOD_DISP_RDMA0,
	[DDP_COMPONENT_RDMA1] = MT8167_MUTEX_MOD_DISP_RDMA1,
	[DDP_COMPONENT_UFOE] = MT8167_MUTEX_MOD_DISP_UFOE,
	[DDP_COMPONENT_WDMA0] = MT8167_MUTEX_MOD_DISP_WDMA0,
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

static const unsigned int mt8183_mutex_mod[DDP_COMPONENT_ID_MAX] = {
	[DDP_COMPONENT_AAL0] = MT8183_MUTEX_MOD_DISP_AAL0,
	[DDP_COMPONENT_CCORR] = MT8183_MUTEX_MOD_DISP_CCORR0,
	[DDP_COMPONENT_COLOR0] = MT8183_MUTEX_MOD_DISP_COLOR0,
	[DDP_COMPONENT_DITHER] = MT8183_MUTEX_MOD_DISP_DITHER0,
	[DDP_COMPONENT_GAMMA] = MT8183_MUTEX_MOD_DISP_GAMMA0,
	[DDP_COMPONENT_OVL0] = MT8183_MUTEX_MOD_DISP_OVL0,
	[DDP_COMPONENT_OVL_2L0] = MT8183_MUTEX_MOD_DISP_OVL0_2L,
	[DDP_COMPONENT_OVL_2L1] = MT8183_MUTEX_MOD_DISP_OVL1_2L,
	[DDP_COMPONENT_RDMA0] = MT8183_MUTEX_MOD_DISP_RDMA0,
	[DDP_COMPONENT_RDMA1] = MT8183_MUTEX_MOD_DISP_RDMA1,
	[DDP_COMPONENT_WDMA0] = MT8183_MUTEX_MOD_DISP_WDMA0,
};

static const unsigned int mt8192_mutex_mod[DDP_COMPONENT_ID_MAX] = {
	[DDP_COMPONENT_AAL0] = MT8192_MUTEX_MOD_DISP_AAL0,
	[DDP_COMPONENT_CCORR] = MT8192_MUTEX_MOD_DISP_CCORR0,
	[DDP_COMPONENT_COLOR0] = MT8192_MUTEX_MOD_DISP_COLOR0,
	[DDP_COMPONENT_DITHER] = MT8192_MUTEX_MOD_DISP_DITHER0,
	[DDP_COMPONENT_GAMMA] = MT8192_MUTEX_MOD_DISP_GAMMA0,
	[DDP_COMPONENT_POSTMASK0] = MT8192_MUTEX_MOD_DISP_POSTMASK0,
	[DDP_COMPONENT_OVL0] = MT8192_MUTEX_MOD_DISP_OVL0,
	[DDP_COMPONENT_OVL_2L0] = MT8192_MUTEX_MOD_DISP_OVL0_2L,
	[DDP_COMPONENT_OVL_2L2] = MT8192_MUTEX_MOD_DISP_OVL2_2L,
	[DDP_COMPONENT_RDMA0] = MT8192_MUTEX_MOD_DISP_RDMA0,
	[DDP_COMPONENT_RDMA4] = MT8192_MUTEX_MOD_DISP_RDMA4,
};

static const unsigned int mt2712_mutex_sof[MUTEX_SOF_DSI3 + 1] = {
	[MUTEX_SOF_SINGLE_MODE] = MUTEX_SOF_SINGLE_MODE,
	[MUTEX_SOF_DSI0] = MUTEX_SOF_DSI0,
	[MUTEX_SOF_DSI1] = MUTEX_SOF_DSI1,
	[MUTEX_SOF_DPI0] = MUTEX_SOF_DPI0,
	[MUTEX_SOF_DPI1] = MUTEX_SOF_DPI1,
	[MUTEX_SOF_DSI2] = MUTEX_SOF_DSI2,
	[MUTEX_SOF_DSI3] = MUTEX_SOF_DSI3,
};

static const unsigned int mt8167_mutex_sof[MUTEX_SOF_DSI3 + 1] = {
	[MUTEX_SOF_SINGLE_MODE] = MUTEX_SOF_SINGLE_MODE,
	[MUTEX_SOF_DSI0] = MUTEX_SOF_DSI0,
	[MUTEX_SOF_DPI0] = MT8167_MUTEX_SOF_DPI0,
	[MUTEX_SOF_DPI1] = MT8167_MUTEX_SOF_DPI1,
};

/* Add EOF setting so overlay hardware can receive frame done irq */
static const unsigned int mt8183_mutex_sof[MUTEX_SOF_DSI3 + 1] = {
	[MUTEX_SOF_SINGLE_MODE] = MUTEX_SOF_SINGLE_MODE,
	[MUTEX_SOF_DSI0] = MUTEX_SOF_DSI0 | MT8183_MUTEX_EOF_DSI0,
	[MUTEX_SOF_DPI0] = MT8183_MUTEX_SOF_DPI0 | MT8183_MUTEX_EOF_DPI0,
};

static const struct mtk_mutex_data mt2701_mutex_driver_data = {
	.mutex_mod = mt2701_mutex_mod,
	.mutex_sof = mt2712_mutex_sof,
	.mutex_mod_reg = MT2701_MUTEX0_MOD0,
	.mutex_sof_reg = MT2701_MUTEX0_SOF0,
};

static const struct mtk_mutex_data mt2712_mutex_driver_data = {
	.mutex_mod = mt2712_mutex_mod,
	.mutex_sof = mt2712_mutex_sof,
	.mutex_mod_reg = MT2701_MUTEX0_MOD0,
	.mutex_sof_reg = MT2701_MUTEX0_SOF0,
};

static const struct mtk_mutex_data mt8167_mutex_driver_data = {
	.mutex_mod = mt8167_mutex_mod,
	.mutex_sof = mt8167_mutex_sof,
	.mutex_mod_reg = MT2701_MUTEX0_MOD0,
	.mutex_sof_reg = MT2701_MUTEX0_SOF0,
	.no_clk = true,
};

static const struct mtk_mutex_data mt8173_mutex_driver_data = {
	.mutex_mod = mt8173_mutex_mod,
	.mutex_sof = mt2712_mutex_sof,
	.mutex_mod_reg = MT2701_MUTEX0_MOD0,
	.mutex_sof_reg = MT2701_MUTEX0_SOF0,
};

static const struct mtk_mutex_data mt8183_mutex_driver_data = {
	.mutex_mod = mt8183_mutex_mod,
	.mutex_sof = mt8183_mutex_sof,
	.mutex_mod_reg = MT8183_MUTEX0_MOD0,
	.mutex_sof_reg = MT8183_MUTEX0_SOF0,
	.no_clk = true,
};

static const struct mtk_mutex_data mt8192_mutex_driver_data = {
	.mutex_mod = mt8192_mutex_mod,
	.mutex_sof = mt8183_mutex_sof,
	.mutex_mod_reg = MT8183_MUTEX0_MOD0,
	.mutex_sof_reg = MT8183_MUTEX0_SOF0,
};

struct mtk_mutex *mtk_mutex_get(struct device *dev)
{
	struct mtk_mutex_ctx *mtx = dev_get_drvdata(dev);
	int i;

	for (i = 0; i < 10; i++)
		if (!mtx->mutex[i].claimed) {
			mtx->mutex[i].claimed = true;
			return &mtx->mutex[i];
		}

	return ERR_PTR(-EBUSY);
}
EXPORT_SYMBOL_GPL(mtk_mutex_get);

void mtk_mutex_put(struct mtk_mutex *mutex)
{
	struct mtk_mutex_ctx *mtx = container_of(mutex, struct mtk_mutex_ctx,
						 mutex[mutex->id]);

	WARN_ON(&mtx->mutex[mutex->id] != mutex);

	mutex->claimed = false;
}
EXPORT_SYMBOL_GPL(mtk_mutex_put);

int mtk_mutex_prepare(struct mtk_mutex *mutex)
{
	struct mtk_mutex_ctx *mtx = container_of(mutex, struct mtk_mutex_ctx,
						 mutex[mutex->id]);
	return clk_prepare_enable(mtx->clk);
}
EXPORT_SYMBOL_GPL(mtk_mutex_prepare);

void mtk_mutex_unprepare(struct mtk_mutex *mutex)
{
	struct mtk_mutex_ctx *mtx = container_of(mutex, struct mtk_mutex_ctx,
						 mutex[mutex->id]);
	clk_disable_unprepare(mtx->clk);
}
EXPORT_SYMBOL_GPL(mtk_mutex_unprepare);

void mtk_mutex_add_comp(struct mtk_mutex *mutex,
			enum mtk_ddp_comp_id id)
{
	struct mtk_mutex_ctx *mtx = container_of(mutex, struct mtk_mutex_ctx,
						 mutex[mutex->id]);
	unsigned int reg;
	unsigned int sof_id;
	unsigned int offset;

	WARN_ON(&mtx->mutex[mutex->id] != mutex);

	switch (id) {
	case DDP_COMPONENT_DSI0:
		sof_id = MUTEX_SOF_DSI0;
		break;
	case DDP_COMPONENT_DSI1:
		sof_id = MUTEX_SOF_DSI0;
		break;
	case DDP_COMPONENT_DSI2:
		sof_id = MUTEX_SOF_DSI2;
		break;
	case DDP_COMPONENT_DSI3:
		sof_id = MUTEX_SOF_DSI3;
		break;
	case DDP_COMPONENT_DPI0:
		sof_id = MUTEX_SOF_DPI0;
		break;
	case DDP_COMPONENT_DPI1:
		sof_id = MUTEX_SOF_DPI1;
		break;
	default:
		if (mtx->data->mutex_mod[id] < 32) {
			offset = DISP_REG_MUTEX_MOD(mtx->data->mutex_mod_reg,
						    mutex->id);
			reg = readl_relaxed(mtx->regs + offset);
			reg |= 1 << mtx->data->mutex_mod[id];
			writel_relaxed(reg, mtx->regs + offset);
		} else {
			offset = DISP_REG_MUTEX_MOD2(mutex->id);
			reg = readl_relaxed(mtx->regs + offset);
			reg |= 1 << (mtx->data->mutex_mod[id] - 32);
			writel_relaxed(reg, mtx->regs + offset);
		}
		return;
	}

	writel_relaxed(mtx->data->mutex_sof[sof_id],
		       mtx->regs +
		       DISP_REG_MUTEX_SOF(mtx->data->mutex_sof_reg, mutex->id));
}
EXPORT_SYMBOL_GPL(mtk_mutex_add_comp);

void mtk_mutex_remove_comp(struct mtk_mutex *mutex,
			   enum mtk_ddp_comp_id id)
{
	struct mtk_mutex_ctx *mtx = container_of(mutex, struct mtk_mutex_ctx,
						 mutex[mutex->id]);
	unsigned int reg;
	unsigned int offset;

	WARN_ON(&mtx->mutex[mutex->id] != mutex);

	switch (id) {
	case DDP_COMPONENT_DSI0:
	case DDP_COMPONENT_DSI1:
	case DDP_COMPONENT_DSI2:
	case DDP_COMPONENT_DSI3:
	case DDP_COMPONENT_DPI0:
	case DDP_COMPONENT_DPI1:
		writel_relaxed(MUTEX_SOF_SINGLE_MODE,
			       mtx->regs +
			       DISP_REG_MUTEX_SOF(mtx->data->mutex_sof_reg,
						  mutex->id));
		break;
	default:
		if (mtx->data->mutex_mod[id] < 32) {
			offset = DISP_REG_MUTEX_MOD(mtx->data->mutex_mod_reg,
						    mutex->id);
			reg = readl_relaxed(mtx->regs + offset);
			reg &= ~(1 << mtx->data->mutex_mod[id]);
			writel_relaxed(reg, mtx->regs + offset);
		} else {
			offset = DISP_REG_MUTEX_MOD2(mutex->id);
			reg = readl_relaxed(mtx->regs + offset);
			reg &= ~(1 << (mtx->data->mutex_mod[id] - 32));
			writel_relaxed(reg, mtx->regs + offset);
		}
		break;
	}
}
EXPORT_SYMBOL_GPL(mtk_mutex_remove_comp);

void mtk_mutex_enable(struct mtk_mutex *mutex)
{
	struct mtk_mutex_ctx *mtx = container_of(mutex, struct mtk_mutex_ctx,
						 mutex[mutex->id]);

	WARN_ON(&mtx->mutex[mutex->id] != mutex);

	writel(1, mtx->regs + DISP_REG_MUTEX_EN(mutex->id));
}
EXPORT_SYMBOL_GPL(mtk_mutex_enable);

void mtk_mutex_disable(struct mtk_mutex *mutex)
{
	struct mtk_mutex_ctx *mtx = container_of(mutex, struct mtk_mutex_ctx,
						 mutex[mutex->id]);

	WARN_ON(&mtx->mutex[mutex->id] != mutex);

	writel(0, mtx->regs + DISP_REG_MUTEX_EN(mutex->id));
}
EXPORT_SYMBOL_GPL(mtk_mutex_disable);

void mtk_mutex_acquire(struct mtk_mutex *mutex)
{
	struct mtk_mutex_ctx *mtx = container_of(mutex, struct mtk_mutex_ctx,
						 mutex[mutex->id]);
	u32 tmp;

	writel(1, mtx->regs + DISP_REG_MUTEX_EN(mutex->id));
	writel(1, mtx->regs + DISP_REG_MUTEX(mutex->id));
	if (readl_poll_timeout_atomic(mtx->regs + DISP_REG_MUTEX(mutex->id),
				      tmp, tmp & INT_MUTEX, 1, 10000))
		pr_err("could not acquire mutex %d\n", mutex->id);
}
EXPORT_SYMBOL_GPL(mtk_mutex_acquire);

void mtk_mutex_release(struct mtk_mutex *mutex)
{
	struct mtk_mutex_ctx *mtx = container_of(mutex, struct mtk_mutex_ctx,
						 mutex[mutex->id]);

	writel(0, mtx->regs + DISP_REG_MUTEX(mutex->id));
}
EXPORT_SYMBOL_GPL(mtk_mutex_release);

static int mtk_mutex_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mtk_mutex_ctx *mtx;
	struct resource *regs;
	int i;

	mtx = devm_kzalloc(dev, sizeof(*mtx), GFP_KERNEL);
	if (!mtx)
		return -ENOMEM;

	for (i = 0; i < 10; i++)
		mtx->mutex[i].id = i;

	mtx->data = of_device_get_match_data(dev);

	if (!mtx->data->no_clk) {
		mtx->clk = devm_clk_get(dev, NULL);
		if (IS_ERR(mtx->clk)) {
			if (PTR_ERR(mtx->clk) != -EPROBE_DEFER)
				dev_err(dev, "Failed to get clock\n");
			return PTR_ERR(mtx->clk);
		}
	}

	regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	mtx->regs = devm_ioremap_resource(dev, regs);
	if (IS_ERR(mtx->regs)) {
		dev_err(dev, "Failed to map mutex registers\n");
		return PTR_ERR(mtx->regs);
	}

	platform_set_drvdata(pdev, mtx);

	return 0;
}

static int mtk_mutex_remove(struct platform_device *pdev)
{
	return 0;
}

static const struct of_device_id mutex_driver_dt_match[] = {
	{ .compatible = "mediatek,mt2701-disp-mutex",
	  .data = &mt2701_mutex_driver_data},
	{ .compatible = "mediatek,mt2712-disp-mutex",
	  .data = &mt2712_mutex_driver_data},
	{ .compatible = "mediatek,mt8167-disp-mutex",
	  .data = &mt8167_mutex_driver_data},
	{ .compatible = "mediatek,mt8173-disp-mutex",
	  .data = &mt8173_mutex_driver_data},
	{ .compatible = "mediatek,mt8183-disp-mutex",
	  .data = &mt8183_mutex_driver_data},
	{ .compatible = "mediatek,mt8192-disp-mutex",
	  .data = &mt8192_mutex_driver_data},
	{},
};
MODULE_DEVICE_TABLE(of, mutex_driver_dt_match);

static struct platform_driver mtk_mutex_driver = {
	.probe		= mtk_mutex_probe,
	.remove		= mtk_mutex_remove,
	.driver		= {
		.name	= "mediatek-mutex",
		.owner	= THIS_MODULE,
		.of_match_table = mutex_driver_dt_match,
	},
};

builtin_platform_driver(mtk_mutex_driver);
