// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021 MediaTek Inc.
 */

#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>
#include <linux/clk.h>
#include <linux/component.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/soc/mediatek/mtk-cmdq.h>
#include <linux/soc/mediatek/mtk-mmsys.h>

#include "mtk_drm_crtc.h"
#include "mtk_drm_ddp_comp.h"
#include "mtk_drm_drv.h"
#include "mtk_ethdr.h"

#define MIX_INTEN			0x4
#define MIX_FME_CPL_INTEN			BIT(1)
#define MIX_INTSTA			0x8
#define MIX_EN				0xc
#define MIX_RST				0x14
#define MIX_ROI_SIZE			0x18
#define MIX_DATAPATH_CON		0x1c
#define OUTPUT_NO_RND				BIT(3)
#define SOURCE_RGB_SEL				BIT(7)
#define BACKGROUND_RELAY			(4 << 9)
#define MIX_ROI_BGCLR			0x20
#define BGCLR_BLACK				0xff000000
#define MIX_SRC_CON			0x24
#define MIX_SRC_L0_EN				BIT(0)
#define MIX_L_SRC_CON(n)		(0x28 + 0x18 * (n))
#define NON_PREMULTI_SOURCE			(2 << 12)
#define MIX_L_SRC_SIZE(n)		(0x30 + 0x18 * (n))
#define MIX_L_SRC_OFFSET(n)		(0x34 + 0x18 * (n))
#define MIX_FUNC_DCM0			0x120
#define MIX_FUNC_DCM1			0x124
#define MIX_FUNC_DCM_ENABLE			0xffffffff

#define HDR_VDO_FE_0804_HDR_DM_FE	0x804
#define HDR_VDO_FE_0804_BYPASS_ALL		0xfd
#define HDR_GFX_FE_0204_GFX_HDR_FE	0x204
#define HDR_GFX_FE_0204_BYPASS_ALL		0xfd
#define HDR_VDO_BE_0204_VDO_DM_BE	0x204
#define HDR_VDO_BE_0204_BYPASS_ALL		0x7e

#define MIXER_INX_MODE_BYPASS			0
#define MIXER_INX_MODE_EVEN_EXTEND		1
#define DEFAULT_9BIT_ALPHA			0x100
#define	MIXER_ALPHA_AEN				BIT(8)
#define	MIXER_ALPHA				0xff
#define ETHDR_CLK_NUM				13

enum mtk_ethdr_comp_id {
	ETHDR_MIXER,
	ETHDR_VDO_FE0,
	ETHDR_VDO_FE1,
	ETHDR_GFX_FE0,
	ETHDR_GFX_FE1,
	ETHDR_VDO_BE,
	ETHDR_ADL_DS,
	ETHDR_ID_MAX
};

struct mtk_ethdr_comp {
	struct device		*dev;
	void __iomem		*regs;
	struct cmdq_client_reg	cmdq_base;
};

struct mtk_ethdr {
	struct mtk_ethdr_comp	ethdr_comp[ETHDR_ID_MAX];
	struct clk_bulk_data	ethdr_clk[ETHDR_CLK_NUM];
	struct device		*mmsys_dev;
	void			(*vblank_cb)(void *data);
	void			*vblank_cb_data;
	int			irq;
	struct reset_control	*reset_ctl;
};

static const char * const ethdr_clk_str[] = {
	"ethdr_top",
	"mixer",
	"vdo_fe0",
	"vdo_fe1",
	"gfx_fe0",
	"gfx_fe1",
	"vdo_be",
	"adl_ds",
	"vdo_fe0_async",
	"vdo_fe1_async",
	"gfx_fe0_async",
	"gfx_fe1_async",
	"vdo_be_async",
};

void mtk_ethdr_register_vblank_cb(struct device *dev,
				  void (*vblank_cb)(void *),
				  void *vblank_cb_data)
{
	struct mtk_ethdr *priv = dev_get_drvdata(dev);

	priv->vblank_cb = vblank_cb;
	priv->vblank_cb_data = vblank_cb_data;
}

void mtk_ethdr_unregister_vblank_cb(struct device *dev)
{
	struct mtk_ethdr *priv = dev_get_drvdata(dev);

	priv->vblank_cb = NULL;
	priv->vblank_cb_data = NULL;
}

void mtk_ethdr_enable_vblank(struct device *dev)
{
	struct mtk_ethdr *priv = dev_get_drvdata(dev);

	writel(MIX_FME_CPL_INTEN, priv->ethdr_comp[ETHDR_MIXER].regs + MIX_INTEN);
}

void mtk_ethdr_disable_vblank(struct device *dev)
{
	struct mtk_ethdr *priv = dev_get_drvdata(dev);

	writel(0x0, priv->ethdr_comp[ETHDR_MIXER].regs + MIX_INTEN);
}

static irqreturn_t mtk_ethdr_irq_handler(int irq, void *dev_id)
{
	struct mtk_ethdr *priv = dev_id;

	writel(0x0, priv->ethdr_comp[ETHDR_MIXER].regs + MIX_INTSTA);

	if (!priv->vblank_cb)
		return IRQ_NONE;

	priv->vblank_cb(priv->vblank_cb_data);

	return IRQ_HANDLED;
}

void mtk_ethdr_layer_config(struct device *dev, unsigned int idx,
			    struct mtk_plane_state *state,
			    struct cmdq_pkt *cmdq_pkt)
{
	struct mtk_ethdr *priv = dev_get_drvdata(dev);
	struct mtk_ethdr_comp *mixer = &priv->ethdr_comp[ETHDR_MIXER];
	struct mtk_plane_pending_state *pending = &state->pending;
	unsigned int offset = (pending->x & 1) << 31 | pending->y << 16 | pending->x;
	unsigned int align_width = ALIGN_DOWN(pending->width, 2);
	unsigned int alpha_con = 0;

	dev_dbg(dev, "%s+ idx:%d", __func__, idx);

	if (idx >= 4)
		return;

	if (!pending->enable) {
		mtk_ddp_write(cmdq_pkt, 0, &mixer->cmdq_base, mixer->regs, MIX_L_SRC_SIZE(idx));
		return;
	}

	if (state->base.fb && state->base.fb->format->has_alpha)
		alpha_con = MIXER_ALPHA_AEN | MIXER_ALPHA;

	mtk_mmsys_mixer_in_config(priv->mmsys_dev, idx + 1, alpha_con ? false : true,
				  DEFAULT_9BIT_ALPHA,
				  pending->x & 1 ? MIXER_INX_MODE_EVEN_EXTEND :
				  MIXER_INX_MODE_BYPASS, align_width / 2 - 1, cmdq_pkt);

	mtk_ddp_write(cmdq_pkt, pending->height << 16 | align_width, &mixer->cmdq_base,
		      mixer->regs, MIX_L_SRC_SIZE(idx));
	mtk_ddp_write(cmdq_pkt, offset, &mixer->cmdq_base, mixer->regs, MIX_L_SRC_OFFSET(idx));
	mtk_ddp_write_mask(cmdq_pkt, alpha_con, &mixer->cmdq_base, mixer->regs, MIX_L_SRC_CON(idx),
			   0x1ff);
	mtk_ddp_write_mask(cmdq_pkt, BIT(idx), &mixer->cmdq_base, mixer->regs, MIX_SRC_CON,
			   BIT(idx));
}

void mtk_ethdr_config(struct device *dev, unsigned int w,
		      unsigned int h, unsigned int vrefresh,
		      unsigned int bpc, struct cmdq_pkt *cmdq_pkt)
{
	struct mtk_ethdr *priv = dev_get_drvdata(dev);
	struct mtk_ethdr_comp *vdo_fe0 = &priv->ethdr_comp[ETHDR_VDO_FE0];
	struct mtk_ethdr_comp *vdo_fe1 = &priv->ethdr_comp[ETHDR_VDO_FE1];
	struct mtk_ethdr_comp *gfx_fe0 = &priv->ethdr_comp[ETHDR_GFX_FE0];
	struct mtk_ethdr_comp *gfx_fe1 = &priv->ethdr_comp[ETHDR_GFX_FE1];
	struct mtk_ethdr_comp *vdo_be = &priv->ethdr_comp[ETHDR_VDO_BE];
	struct mtk_ethdr_comp *mixer = &priv->ethdr_comp[ETHDR_MIXER];

	dev_dbg(dev, "%s-w:%d, h:%d\n", __func__, w, h);

	mtk_ddp_write(cmdq_pkt, HDR_VDO_FE_0804_BYPASS_ALL, &vdo_fe0->cmdq_base,
		      vdo_fe0->regs, HDR_VDO_FE_0804_HDR_DM_FE);

	mtk_ddp_write(cmdq_pkt, HDR_VDO_FE_0804_BYPASS_ALL, &vdo_fe1->cmdq_base,
		      vdo_fe1->regs, HDR_VDO_FE_0804_HDR_DM_FE);

	mtk_ddp_write(cmdq_pkt, HDR_GFX_FE_0204_BYPASS_ALL, &gfx_fe0->cmdq_base,
		      gfx_fe0->regs, HDR_GFX_FE_0204_GFX_HDR_FE);

	mtk_ddp_write(cmdq_pkt, HDR_GFX_FE_0204_BYPASS_ALL, &gfx_fe1->cmdq_base,
		      gfx_fe1->regs, HDR_GFX_FE_0204_GFX_HDR_FE);

	mtk_ddp_write(cmdq_pkt, HDR_VDO_BE_0204_BYPASS_ALL, &vdo_be->cmdq_base,
		      vdo_be->regs, HDR_VDO_BE_0204_VDO_DM_BE);

	mtk_ddp_write(cmdq_pkt, MIX_FUNC_DCM_ENABLE, &mixer->cmdq_base, mixer->regs, MIX_FUNC_DCM0);
	mtk_ddp_write(cmdq_pkt, MIX_FUNC_DCM_ENABLE, &mixer->cmdq_base, mixer->regs, MIX_FUNC_DCM1);
	mtk_ddp_write(cmdq_pkt, h << 16 | w, &mixer->cmdq_base, mixer->regs, MIX_ROI_SIZE);
	mtk_ddp_write(cmdq_pkt, BGCLR_BLACK, &mixer->cmdq_base, mixer->regs, MIX_ROI_BGCLR);
	mtk_ddp_write(cmdq_pkt, NON_PREMULTI_SOURCE, &mixer->cmdq_base, mixer->regs,
		      MIX_L_SRC_CON(0));
	mtk_ddp_write(cmdq_pkt, NON_PREMULTI_SOURCE, &mixer->cmdq_base, mixer->regs,
		      MIX_L_SRC_CON(1));
	mtk_ddp_write(cmdq_pkt, NON_PREMULTI_SOURCE, &mixer->cmdq_base, mixer->regs,
		      MIX_L_SRC_CON(2));
	mtk_ddp_write(cmdq_pkt, NON_PREMULTI_SOURCE, &mixer->cmdq_base, mixer->regs,
		      MIX_L_SRC_CON(3));
	mtk_ddp_write(cmdq_pkt, 0x0, &mixer->cmdq_base, mixer->regs, MIX_L_SRC_SIZE(0));
	mtk_ddp_write(cmdq_pkt, OUTPUT_NO_RND | SOURCE_RGB_SEL | BACKGROUND_RELAY,
		      &mixer->cmdq_base, mixer->regs, MIX_DATAPATH_CON);
	mtk_ddp_write_mask(cmdq_pkt, MIX_SRC_L0_EN, &mixer->cmdq_base, mixer->regs,
			   MIX_SRC_CON, MIX_SRC_L0_EN);

	mtk_mmsys_hdr_config(priv->mmsys_dev, w / 2, h, cmdq_pkt);
	mtk_mmsys_mixer_in_channel_swap(priv->mmsys_dev, 4, 0, cmdq_pkt);
}

void mtk_ethdr_start(struct device *dev)
{
	struct mtk_ethdr *priv = dev_get_drvdata(dev);
	struct mtk_ethdr_comp *mixer = &priv->ethdr_comp[ETHDR_MIXER];

	writel(1, mixer->regs + MIX_EN);
}

void mtk_ethdr_stop(struct device *dev)
{
	struct mtk_ethdr *priv = dev_get_drvdata(dev);
	struct mtk_ethdr_comp *mixer = &priv->ethdr_comp[ETHDR_MIXER];

	writel(0, mixer->regs + MIX_EN);
	writel(1, mixer->regs + MIX_RST);
	reset_control_reset(priv->reset_ctl);
	writel(0, mixer->regs + MIX_RST);
}

int mtk_ethdr_clk_enable(struct device *dev)
{
	int ret;
	struct mtk_ethdr *priv = dev_get_drvdata(dev);

	ret = clk_bulk_prepare_enable(ETHDR_CLK_NUM, priv->ethdr_clk);
	if (ret)
		dev_err(dev,
			"ethdr_clk prepare enable failed\n");
	return ret;
}

void mtk_ethdr_clk_disable(struct device *dev)
{
	struct mtk_ethdr *priv = dev_get_drvdata(dev);

	clk_bulk_disable_unprepare(ETHDR_CLK_NUM, priv->ethdr_clk);
}

static int mtk_ethdr_bind(struct device *dev, struct device *master,
			  void *data)
{
	struct mtk_ethdr *priv = dev_get_drvdata(dev);

	priv->mmsys_dev = data;
	return 0;
}

static void mtk_ethdr_unbind(struct device *dev, struct device *master, void *data)
{
}

static const struct component_ops mtk_ethdr_component_ops = {
	.bind	= mtk_ethdr_bind,
	.unbind = mtk_ethdr_unbind,
};

static int mtk_ethdr_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mtk_ethdr *priv;
	int ret;
	int i;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	for (i = 0; i < ETHDR_ID_MAX; i++) {
		priv->ethdr_comp[i].dev = dev;
		priv->ethdr_comp[i].regs = of_iomap(dev->of_node, i);
#if IS_REACHABLE(CONFIG_MTK_CMDQ)
		ret = cmdq_dev_get_client_reg(dev,
					      &priv->ethdr_comp[i].cmdq_base, i);
		if (ret)
			dev_dbg(dev, "get mediatek,gce-client-reg fail!\n");
#endif
		dev_dbg(dev, "[DRM]regs:0x%p, node:%d\n", priv->ethdr_comp[i].regs, i);
	}

	for (i = 0; i < ETHDR_CLK_NUM; i++)
		priv->ethdr_clk[i].id = ethdr_clk_str[i];
	ret = devm_clk_bulk_get_optional(dev, ETHDR_CLK_NUM, priv->ethdr_clk);
	if (ret)
		return ret;

	priv->irq = platform_get_irq(pdev, 0);
	if (priv->irq < 0)
		priv->irq = 0;

	if (priv->irq) {
		ret = devm_request_irq(dev, priv->irq, mtk_ethdr_irq_handler,
				       IRQF_TRIGGER_NONE, dev_name(dev), priv);
		if (ret < 0) {
			dev_err(dev, "Failed to request irq %d: %d\n", priv->irq, ret);
			return ret;
		}
	}

	priv->reset_ctl = devm_reset_control_array_get_optional_exclusive(dev);
	if (IS_ERR(priv->reset_ctl)) {
		dev_err_probe(dev, PTR_ERR(priv->reset_ctl), "cannot get ethdr reset control\n");
		return PTR_ERR(priv->reset_ctl);
	}

	platform_set_drvdata(pdev, priv);

	ret = component_add(dev, &mtk_ethdr_component_ops);
	if (ret)
		dev_notice(dev, "Failed to add component: %d\n", ret);

	return ret;
}

static void mtk_ethdr_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &mtk_ethdr_component_ops);
}

static const struct of_device_id mtk_ethdr_driver_dt_match[] = {
	{ .compatible = "mediatek,mt8195-disp-ethdr"},
	{},
};

MODULE_DEVICE_TABLE(of, mtk_ethdr_driver_dt_match);

struct platform_driver mtk_ethdr_driver = {
	.probe		= mtk_ethdr_probe,
	.remove_new	= mtk_ethdr_remove,
	.driver		= {
		.name	= "mediatek-disp-ethdr",
		.owner	= THIS_MODULE,
		.of_match_table = mtk_ethdr_driver_dt_match,
	},
};
