// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2021 MediaTek Inc.
 */

#include <linux/clk.h>
#include <linux/component.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/soc/mediatek/mtk-cmdq.h>

#include "mtk_drm_ddp_comp.h"
#include "mtk_drm_drv.h"
#include "mtk_disp_drv.h"

#define DISP_REG_MERGE_CTRL		0x000
#define MERGE_EN				1
#define DISP_REG_MERGE_CFG_0		0x010
#define DISP_REG_MERGE_CFG_1		0x014
#define DISP_REG_MERGE_CFG_4		0x020
#define DISP_REG_MERGE_CFG_10		0x038
/* no swap */
#define SWAP_MODE				0
#define FLD_SWAP_MODE				GENMASK(4, 0)
#define DISP_REG_MERGE_CFG_12		0x040
#define CFG_10_10_1PI_2PO_BUF_MODE		6
#define CFG_10_10_2PI_2PO_BUF_MODE		8
#define CFG_11_10_1PI_2PO_MERGE			18
#define FLD_CFG_MERGE_MODE			GENMASK(4, 0)
#define DISP_REG_MERGE_CFG_24		0x070
#define DISP_REG_MERGE_CFG_25		0x074
#define DISP_REG_MERGE_CFG_26		0x078
#define DISP_REG_MERGE_CFG_27		0x07c
#define DISP_REG_MERGE_CFG_36		0x0a0
#define ULTRA_EN				BIT(0)
#define PREULTRA_EN				BIT(4)
#define DISP_REG_MERGE_CFG_37		0x0a4
/* 0: Off, 1: SRAM0, 2: SRAM1, 3: SRAM0 + SRAM1 */
#define BUFFER_MODE				3
#define FLD_BUFFER_MODE				GENMASK(1, 0)
/*
 * For the ultra and preultra settings, 6us ~ 9us is experience value
 * and the maximum frequency of mmsys clock is 594MHz.
 */
#define DISP_REG_MERGE_CFG_40		0x0b0
/* 6 us, 594M pixel/sec */
#define ULTRA_TH_LOW				(6 * 594)
/* 8 us, 594M pixel/sec */
#define ULTRA_TH_HIGH				(8 * 594)
#define FLD_ULTRA_TH_LOW			GENMASK(15, 0)
#define FLD_ULTRA_TH_HIGH			GENMASK(31, 16)
#define DISP_REG_MERGE_CFG_41		0x0b4
/* 8 us, 594M pixel/sec */
#define PREULTRA_TH_LOW				(8 * 594)
/* 9 us, 594M pixel/sec */
#define PREULTRA_TH_HIGH			(9 * 594)
#define FLD_PREULTRA_TH_LOW			GENMASK(15, 0)
#define FLD_PREULTRA_TH_HIGH			GENMASK(31, 16)

#define DISP_REG_MERGE_MUTE_0		0xf00

struct mtk_disp_merge {
	void __iomem			*regs;
	struct clk			*clk;
	struct clk			*async_clk;
	struct cmdq_client_reg		cmdq_reg;
	bool				fifo_en;
	bool				mute_support;
	struct reset_control		*reset_ctl;
};

void mtk_merge_start(struct device *dev)
{
	mtk_merge_start_cmdq(dev, NULL);
}

void mtk_merge_stop(struct device *dev)
{
	mtk_merge_stop_cmdq(dev, NULL);
}

void mtk_merge_start_cmdq(struct device *dev, struct cmdq_pkt *cmdq_pkt)
{
	struct mtk_disp_merge *priv = dev_get_drvdata(dev);

	if (priv->mute_support)
		mtk_ddp_write(cmdq_pkt, 0x0, &priv->cmdq_reg, priv->regs,
			      DISP_REG_MERGE_MUTE_0);

	mtk_ddp_write(cmdq_pkt, 1, &priv->cmdq_reg, priv->regs,
		      DISP_REG_MERGE_CTRL);
}

void mtk_merge_stop_cmdq(struct device *dev, struct cmdq_pkt *cmdq_pkt)
{
	struct mtk_disp_merge *priv = dev_get_drvdata(dev);

	if (priv->mute_support)
		mtk_ddp_write(cmdq_pkt, 0x1, &priv->cmdq_reg, priv->regs,
			      DISP_REG_MERGE_MUTE_0);

	mtk_ddp_write(cmdq_pkt, 0, &priv->cmdq_reg, priv->regs,
		      DISP_REG_MERGE_CTRL);

	if (!cmdq_pkt && priv->async_clk)
		reset_control_reset(priv->reset_ctl);
}

static void mtk_merge_fifo_setting(struct mtk_disp_merge *priv,
				   struct cmdq_pkt *cmdq_pkt)
{
	mtk_ddp_write(cmdq_pkt, ULTRA_EN | PREULTRA_EN,
		      &priv->cmdq_reg, priv->regs, DISP_REG_MERGE_CFG_36);

	mtk_ddp_write_mask(cmdq_pkt, BUFFER_MODE,
			   &priv->cmdq_reg, priv->regs, DISP_REG_MERGE_CFG_37,
			   FLD_BUFFER_MODE);

	mtk_ddp_write_mask(cmdq_pkt, ULTRA_TH_LOW | ULTRA_TH_HIGH << 16,
			   &priv->cmdq_reg, priv->regs, DISP_REG_MERGE_CFG_40,
			   FLD_ULTRA_TH_LOW | FLD_ULTRA_TH_HIGH);

	mtk_ddp_write_mask(cmdq_pkt, PREULTRA_TH_LOW | PREULTRA_TH_HIGH << 16,
			   &priv->cmdq_reg, priv->regs, DISP_REG_MERGE_CFG_41,
			   FLD_PREULTRA_TH_LOW | FLD_PREULTRA_TH_HIGH);
}

void mtk_merge_config(struct device *dev, unsigned int w,
		      unsigned int h, unsigned int vrefresh,
		      unsigned int bpc, struct cmdq_pkt *cmdq_pkt)
{
	mtk_merge_advance_config(dev, w, 0, h, vrefresh, bpc, cmdq_pkt);
}

void mtk_merge_advance_config(struct device *dev, unsigned int l_w, unsigned int r_w,
			      unsigned int h, unsigned int vrefresh, unsigned int bpc,
			      struct cmdq_pkt *cmdq_pkt)
{
	struct mtk_disp_merge *priv = dev_get_drvdata(dev);
	unsigned int mode = CFG_10_10_1PI_2PO_BUF_MODE;

	if (!h || !l_w) {
		dev_err(dev, "%s: input width(%d) or height(%d) is invalid\n", __func__, l_w, h);
		return;
	}

	if (priv->fifo_en) {
		mtk_merge_fifo_setting(priv, cmdq_pkt);
		mode = CFG_10_10_2PI_2PO_BUF_MODE;
	}

	if (r_w)
		mode = CFG_11_10_1PI_2PO_MERGE;

	mtk_ddp_write(cmdq_pkt, h << 16 | l_w, &priv->cmdq_reg, priv->regs,
		      DISP_REG_MERGE_CFG_0);
	mtk_ddp_write(cmdq_pkt, h << 16 | r_w, &priv->cmdq_reg, priv->regs,
		      DISP_REG_MERGE_CFG_1);
	mtk_ddp_write(cmdq_pkt, h << 16 | (l_w + r_w), &priv->cmdq_reg, priv->regs,
		      DISP_REG_MERGE_CFG_4);
	/*
	 * DISP_REG_MERGE_CFG_24 is merge SRAM0 w/h
	 * DISP_REG_MERGE_CFG_25 is merge SRAM1 w/h.
	 * If r_w > 0, the merge is in merge mode (input0 and input1 merge together),
	 * the input0 goes to SRAM0, and input1 goes to SRAM1.
	 * If r_w = 0, the merge is in buffer mode, the input goes through SRAM0 and
	 * then to SRAM1. Both SRAM0 and SRAM1 are set to the same size.
	 */
	mtk_ddp_write(cmdq_pkt, h << 16 | l_w, &priv->cmdq_reg, priv->regs,
		      DISP_REG_MERGE_CFG_24);
	if (r_w)
		mtk_ddp_write(cmdq_pkt, h << 16 | r_w, &priv->cmdq_reg, priv->regs,
			      DISP_REG_MERGE_CFG_25);
	else
		mtk_ddp_write(cmdq_pkt, h << 16 | l_w, &priv->cmdq_reg, priv->regs,
			      DISP_REG_MERGE_CFG_25);

	/*
	 * DISP_REG_MERGE_CFG_26 and DISP_REG_MERGE_CFG_27 is only used in LR merge.
	 * Only take effect when the merge is setting to merge mode.
	 */
	mtk_ddp_write(cmdq_pkt, h << 16 | l_w, &priv->cmdq_reg, priv->regs,
		      DISP_REG_MERGE_CFG_26);
	mtk_ddp_write(cmdq_pkt, h << 16 | r_w, &priv->cmdq_reg, priv->regs,
		      DISP_REG_MERGE_CFG_27);

	mtk_ddp_write_mask(cmdq_pkt, SWAP_MODE, &priv->cmdq_reg, priv->regs,
			   DISP_REG_MERGE_CFG_10, FLD_SWAP_MODE);
	mtk_ddp_write_mask(cmdq_pkt, mode, &priv->cmdq_reg, priv->regs,
			   DISP_REG_MERGE_CFG_12, FLD_CFG_MERGE_MODE);
}

int mtk_merge_clk_enable(struct device *dev)
{
	int ret = 0;
	struct mtk_disp_merge *priv = dev_get_drvdata(dev);

	ret = clk_prepare_enable(priv->clk);
	if (ret) {
		dev_err(dev, "merge clk prepare enable failed\n");
		return ret;
	}

	ret = clk_prepare_enable(priv->async_clk);
	if (ret) {
		/* should clean up the state of priv->clk */
		clk_disable_unprepare(priv->clk);

		dev_err(dev, "async clk prepare enable failed\n");
		return ret;
	}

	return ret;
}

void mtk_merge_clk_disable(struct device *dev)
{
	struct mtk_disp_merge *priv = dev_get_drvdata(dev);

	clk_disable_unprepare(priv->async_clk);
	clk_disable_unprepare(priv->clk);
}

static int mtk_disp_merge_bind(struct device *dev, struct device *master,
			       void *data)
{
	return 0;
}

static void mtk_disp_merge_unbind(struct device *dev, struct device *master,
				  void *data)
{
}

static const struct component_ops mtk_disp_merge_component_ops = {
	.bind	= mtk_disp_merge_bind,
	.unbind = mtk_disp_merge_unbind,
};

static int mtk_disp_merge_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource *res;
	struct mtk_disp_merge *priv;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv->regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(priv->regs)) {
		dev_err(dev, "failed to ioremap merge\n");
		return PTR_ERR(priv->regs);
	}

	priv->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(priv->clk)) {
		dev_err(dev, "failed to get merge clk\n");
		return PTR_ERR(priv->clk);
	}

	priv->async_clk = devm_clk_get_optional(dev, "merge_async");
	if (IS_ERR(priv->async_clk)) {
		dev_err(dev, "failed to get merge async clock\n");
		return PTR_ERR(priv->async_clk);
	}

	if (priv->async_clk) {
		priv->reset_ctl = devm_reset_control_get_optional_exclusive(dev, NULL);
		if (IS_ERR(priv->reset_ctl))
			return PTR_ERR(priv->reset_ctl);
	}

#if IS_REACHABLE(CONFIG_MTK_CMDQ)
	ret = cmdq_dev_get_client_reg(dev, &priv->cmdq_reg, 0);
	if (ret)
		dev_dbg(dev, "get mediatek,gce-client-reg fail!\n");
#endif

	priv->fifo_en = of_property_read_bool(dev->of_node,
					      "mediatek,merge-fifo-en");

	priv->mute_support = of_property_read_bool(dev->of_node,
						   "mediatek,merge-mute");
	platform_set_drvdata(pdev, priv);

	ret = component_add(dev, &mtk_disp_merge_component_ops);
	if (ret != 0)
		dev_err(dev, "Failed to add component: %d\n", ret);

	return ret;
}

static int mtk_disp_merge_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &mtk_disp_merge_component_ops);

	return 0;
}

static const struct of_device_id mtk_disp_merge_driver_dt_match[] = {
	{ .compatible = "mediatek,mt8195-disp-merge", },
	{},
};

MODULE_DEVICE_TABLE(of, mtk_disp_merge_driver_dt_match);

struct platform_driver mtk_disp_merge_driver = {
	.probe = mtk_disp_merge_probe,
	.remove = mtk_disp_merge_remove,
	.driver = {
		.name = "mediatek-disp-merge",
		.owner = THIS_MODULE,
		.of_match_table = mtk_disp_merge_driver_dt_match,
	},
};
