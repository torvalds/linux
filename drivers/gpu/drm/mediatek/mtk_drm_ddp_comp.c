/*
 * Copyright (c) 2015 MediaTek Inc.
 * Authors:
 *	YT Shen <yt.shen@mediatek.com>
 *	CK Hu <ck.hu@mediatek.com>
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
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <drm/drmP.h>
#include "mtk_drm_drv.h"
#include "mtk_drm_plane.h"
#include "mtk_drm_ddp_comp.h"
#include "mtk_drm_crtc.h"

#define DISP_OD_EN				0x0000
#define DISP_OD_INTEN				0x0008
#define DISP_OD_INTSTA				0x000c
#define DISP_OD_CFG				0x0020
#define DISP_OD_SIZE				0x0030
#define DISP_DITHER_5				0x0114
#define DISP_DITHER_7				0x011c
#define DISP_DITHER_15				0x013c
#define DISP_DITHER_16				0x0140

#define DISP_REG_UFO_START			0x0000

#define DISP_COLOR_CFG_MAIN			0x0400
#define DISP_COLOR_START			0x0c00
#define DISP_COLOR_WIDTH			0x0c50
#define DISP_COLOR_HEIGHT			0x0c54

#define DISP_AAL_EN				0x0000
#define DISP_AAL_SIZE				0x0030

#define DISP_GAMMA_EN				0x0000
#define DISP_GAMMA_CFG				0x0020
#define DISP_GAMMA_SIZE				0x0030
#define DISP_GAMMA_LUT				0x0700

#define LUT_10BIT_MASK				0x03ff

#define COLOR_BYPASS_ALL			BIT(7)
#define COLOR_SEQ_SEL				BIT(13)

#define OD_RELAYMODE				BIT(0)

#define UFO_BYPASS				BIT(2)

#define AAL_EN					BIT(0)

#define GAMMA_EN				BIT(0)
#define GAMMA_LUT_EN				BIT(1)

#define DISP_DITHERING				BIT(2)
#define DITHER_LSB_ERR_SHIFT_R(x)		(((x) & 0x7) << 28)
#define DITHER_OVFLW_BIT_R(x)			(((x) & 0x7) << 24)
#define DITHER_ADD_LSHIFT_R(x)			(((x) & 0x7) << 20)
#define DITHER_ADD_RSHIFT_R(x)			(((x) & 0x7) << 16)
#define DITHER_NEW_BIT_MODE			BIT(0)
#define DITHER_LSB_ERR_SHIFT_B(x)		(((x) & 0x7) << 28)
#define DITHER_OVFLW_BIT_B(x)			(((x) & 0x7) << 24)
#define DITHER_ADD_LSHIFT_B(x)			(((x) & 0x7) << 20)
#define DITHER_ADD_RSHIFT_B(x)			(((x) & 0x7) << 16)
#define DITHER_LSB_ERR_SHIFT_G(x)		(((x) & 0x7) << 12)
#define DITHER_OVFLW_BIT_G(x)			(((x) & 0x7) << 8)
#define DITHER_ADD_LSHIFT_G(x)			(((x) & 0x7) << 4)
#define DITHER_ADD_RSHIFT_G(x)			(((x) & 0x7) << 0)

void mtk_dither_set(struct mtk_ddp_comp *comp, unsigned int bpc,
		    unsigned int CFG)
{
	/* If bpc equal to 0, the dithering function didn't be enabled */
	if (bpc == 0)
		return;

	if (bpc >= MTK_MIN_BPC) {
		writel(0, comp->regs + DISP_DITHER_5);
		writel(0, comp->regs + DISP_DITHER_7);
		writel(DITHER_LSB_ERR_SHIFT_R(MTK_MAX_BPC - bpc) |
		       DITHER_ADD_LSHIFT_R(MTK_MAX_BPC - bpc) |
		       DITHER_NEW_BIT_MODE,
		       comp->regs + DISP_DITHER_15);
		writel(DITHER_LSB_ERR_SHIFT_B(MTK_MAX_BPC - bpc) |
		       DITHER_ADD_LSHIFT_B(MTK_MAX_BPC - bpc) |
		       DITHER_LSB_ERR_SHIFT_G(MTK_MAX_BPC - bpc) |
		       DITHER_ADD_LSHIFT_G(MTK_MAX_BPC - bpc),
		       comp->regs + DISP_DITHER_16);
		writel(DISP_DITHERING, comp->regs + CFG);
	}
}

static void mtk_color_config(struct mtk_ddp_comp *comp, unsigned int w,
			     unsigned int h, unsigned int vrefresh,
			     unsigned int bpc)
{
	writel(w, comp->regs + DISP_COLOR_WIDTH);
	writel(h, comp->regs + DISP_COLOR_HEIGHT);
}

static void mtk_color_start(struct mtk_ddp_comp *comp)
{
	writel(COLOR_BYPASS_ALL | COLOR_SEQ_SEL,
	       comp->regs + DISP_COLOR_CFG_MAIN);
	writel(0x1, comp->regs + DISP_COLOR_START);
}

static void mtk_od_config(struct mtk_ddp_comp *comp, unsigned int w,
			  unsigned int h, unsigned int vrefresh,
			  unsigned int bpc)
{
	writel(w << 16 | h, comp->regs + DISP_OD_SIZE);
	writel(OD_RELAYMODE, comp->regs + DISP_OD_CFG);
	mtk_dither_set(comp, bpc, DISP_OD_CFG);
}

static void mtk_od_start(struct mtk_ddp_comp *comp)
{
	writel(1, comp->regs + DISP_OD_EN);
}

static void mtk_ufoe_start(struct mtk_ddp_comp *comp)
{
	writel(UFO_BYPASS, comp->regs + DISP_REG_UFO_START);
}

static void mtk_aal_config(struct mtk_ddp_comp *comp, unsigned int w,
			   unsigned int h, unsigned int vrefresh,
			   unsigned int bpc)
{
	writel(h << 16 | w, comp->regs + DISP_AAL_SIZE);
}

static void mtk_aal_start(struct mtk_ddp_comp *comp)
{
	writel(AAL_EN, comp->regs + DISP_AAL_EN);
}

static void mtk_aal_stop(struct mtk_ddp_comp *comp)
{
	writel_relaxed(0x0, comp->regs + DISP_AAL_EN);
}

static void mtk_gamma_config(struct mtk_ddp_comp *comp, unsigned int w,
			     unsigned int h, unsigned int vrefresh,
			     unsigned int bpc)
{
	writel(h << 16 | w, comp->regs + DISP_GAMMA_SIZE);
	mtk_dither_set(comp, bpc, DISP_GAMMA_CFG);
}

static void mtk_gamma_start(struct mtk_ddp_comp *comp)
{
	writel(GAMMA_EN, comp->regs  + DISP_GAMMA_EN);
}

static void mtk_gamma_stop(struct mtk_ddp_comp *comp)
{
	writel_relaxed(0x0, comp->regs  + DISP_GAMMA_EN);
}

static void mtk_gamma_set(struct mtk_ddp_comp *comp,
			  struct drm_crtc_state *state)
{
	unsigned int i, reg;
	struct drm_color_lut *lut;
	void __iomem *lut_base;
	u32 word;

	if (state->gamma_lut) {
		reg = readl(comp->regs + DISP_GAMMA_CFG);
		reg = reg | GAMMA_LUT_EN;
		writel(reg, comp->regs + DISP_GAMMA_CFG);
		lut_base = comp->regs + DISP_GAMMA_LUT;
		lut = (struct drm_color_lut *)state->gamma_lut->data;
		for (i = 0; i < MTK_LUT_SIZE; i++) {
			word = (((lut[i].red >> 6) & LUT_10BIT_MASK) << 20) +
				(((lut[i].green >> 6) & LUT_10BIT_MASK) << 10) +
				((lut[i].blue >> 6) & LUT_10BIT_MASK);
			writel(word, (lut_base + i * 4));
		}
	}
}

static const struct mtk_ddp_comp_funcs ddp_aal = {
	.gamma_set = mtk_gamma_set,
	.config = mtk_aal_config,
	.start = mtk_aal_start,
	.stop = mtk_aal_stop,
};

static const struct mtk_ddp_comp_funcs ddp_gamma = {
	.gamma_set = mtk_gamma_set,
	.config = mtk_gamma_config,
	.start = mtk_gamma_start,
	.stop = mtk_gamma_stop,
};

static const struct mtk_ddp_comp_funcs ddp_color = {
	.config = mtk_color_config,
	.start = mtk_color_start,
};

static const struct mtk_ddp_comp_funcs ddp_od = {
	.config = mtk_od_config,
	.start = mtk_od_start,
};

static const struct mtk_ddp_comp_funcs ddp_ufoe = {
	.start = mtk_ufoe_start,
};

static const char * const mtk_ddp_comp_stem[MTK_DDP_COMP_TYPE_MAX] = {
	[MTK_DISP_OVL] = "ovl",
	[MTK_DISP_RDMA] = "rdma",
	[MTK_DISP_WDMA] = "wdma",
	[MTK_DISP_COLOR] = "color",
	[MTK_DISP_AAL] = "aal",
	[MTK_DISP_GAMMA] = "gamma",
	[MTK_DISP_UFOE] = "ufoe",
	[MTK_DSI] = "dsi",
	[MTK_DPI] = "dpi",
	[MTK_DISP_PWM] = "pwm",
	[MTK_DISP_MUTEX] = "mutex",
	[MTK_DISP_OD] = "od",
};

struct mtk_ddp_comp_match {
	enum mtk_ddp_comp_type type;
	int alias_id;
	const struct mtk_ddp_comp_funcs *funcs;
};

static const struct mtk_ddp_comp_match mtk_ddp_matches[DDP_COMPONENT_ID_MAX] = {
	[DDP_COMPONENT_AAL]	= { MTK_DISP_AAL,	0, &ddp_aal },
	[DDP_COMPONENT_COLOR0]	= { MTK_DISP_COLOR,	0, &ddp_color },
	[DDP_COMPONENT_COLOR1]	= { MTK_DISP_COLOR,	1, &ddp_color },
	[DDP_COMPONENT_DPI0]	= { MTK_DPI,		0, NULL },
	[DDP_COMPONENT_DSI0]	= { MTK_DSI,		0, NULL },
	[DDP_COMPONENT_DSI1]	= { MTK_DSI,		1, NULL },
	[DDP_COMPONENT_GAMMA]	= { MTK_DISP_GAMMA,	0, &ddp_gamma },
	[DDP_COMPONENT_OD]	= { MTK_DISP_OD,	0, &ddp_od },
	[DDP_COMPONENT_OVL0]	= { MTK_DISP_OVL,	0, NULL },
	[DDP_COMPONENT_OVL1]	= { MTK_DISP_OVL,	1, NULL },
	[DDP_COMPONENT_PWM0]	= { MTK_DISP_PWM,	0, NULL },
	[DDP_COMPONENT_RDMA0]	= { MTK_DISP_RDMA,	0, NULL },
	[DDP_COMPONENT_RDMA1]	= { MTK_DISP_RDMA,	1, NULL },
	[DDP_COMPONENT_RDMA2]	= { MTK_DISP_RDMA,	2, NULL },
	[DDP_COMPONENT_UFOE]	= { MTK_DISP_UFOE,	0, &ddp_ufoe },
	[DDP_COMPONENT_WDMA0]	= { MTK_DISP_WDMA,	0, NULL },
	[DDP_COMPONENT_WDMA1]	= { MTK_DISP_WDMA,	1, NULL },
};

int mtk_ddp_comp_get_id(struct device_node *node,
			enum mtk_ddp_comp_type comp_type)
{
	int id = of_alias_get_id(node, mtk_ddp_comp_stem[comp_type]);
	int i;

	for (i = 0; i < ARRAY_SIZE(mtk_ddp_matches); i++) {
		if (comp_type == mtk_ddp_matches[i].type &&
		    (id < 0 || id == mtk_ddp_matches[i].alias_id))
			return i;
	}

	return -EINVAL;
}

int mtk_ddp_comp_init(struct device *dev, struct device_node *node,
		      struct mtk_ddp_comp *comp, enum mtk_ddp_comp_id comp_id,
		      const struct mtk_ddp_comp_funcs *funcs)
{
	enum mtk_ddp_comp_type type;
	struct device_node *larb_node;
	struct platform_device *larb_pdev;

	if (comp_id < 0 || comp_id >= DDP_COMPONENT_ID_MAX)
		return -EINVAL;

	comp->id = comp_id;
	comp->funcs = funcs ?: mtk_ddp_matches[comp_id].funcs;

	if (comp_id == DDP_COMPONENT_DPI0 ||
	    comp_id == DDP_COMPONENT_DSI0 ||
	    comp_id == DDP_COMPONENT_PWM0) {
		comp->regs = NULL;
		comp->clk = NULL;
		comp->irq = 0;
		return 0;
	}

	comp->regs = of_iomap(node, 0);
	comp->irq = of_irq_get(node, 0);
	comp->clk = of_clk_get(node, 0);
	if (IS_ERR(comp->clk))
		comp->clk = NULL;

	type = mtk_ddp_matches[comp_id].type;

	/* Only DMA capable components need the LARB property */
	comp->larb_dev = NULL;
	if (type != MTK_DISP_OVL &&
	    type != MTK_DISP_RDMA &&
	    type != MTK_DISP_WDMA)
		return 0;

	larb_node = of_parse_phandle(node, "mediatek,larb", 0);
	if (!larb_node) {
		dev_err(dev,
			"Missing mediadek,larb phandle in %s node\n",
			node->full_name);
		return -EINVAL;
	}

	larb_pdev = of_find_device_by_node(larb_node);
	if (!larb_pdev) {
		dev_warn(dev, "Waiting for larb device %s\n",
			 larb_node->full_name);
		of_node_put(larb_node);
		return -EPROBE_DEFER;
	}
	of_node_put(larb_node);

	comp->larb_dev = &larb_pdev->dev;

	return 0;
}

int mtk_ddp_comp_register(struct drm_device *drm, struct mtk_ddp_comp *comp)
{
	struct mtk_drm_private *private = drm->dev_private;

	if (private->ddp_comp[comp->id])
		return -EBUSY;

	private->ddp_comp[comp->id] = comp;
	return 0;
}

void mtk_ddp_comp_unregister(struct drm_device *drm, struct mtk_ddp_comp *comp)
{
	struct mtk_drm_private *private = drm->dev_private;

	private->ddp_comp[comp->id] = NULL;
}
