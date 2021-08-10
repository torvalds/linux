// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015 MediaTek Inc.
 * Authors:
 *	YT Shen <yt.shen@mediatek.com>
 *	CK Hu <ck.hu@mediatek.com>
 */

#include <linux/clk.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/soc/mediatek/mtk-cmdq.h>
#include <drm/drm_print.h>

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

#define DISP_AAL_EN				0x0000
#define DISP_AAL_SIZE				0x0030
#define DISP_AAL_OUTPUT_SIZE			0x04d8

#define DISP_CCORR_EN				0x0000
#define CCORR_EN				BIT(0)
#define DISP_CCORR_CFG				0x0020
#define CCORR_RELAY_MODE			BIT(0)
#define CCORR_ENGINE_EN				BIT(1)
#define CCORR_GAMMA_OFF				BIT(2)
#define CCORR_WGAMUT_SRC_CLIP			BIT(3)
#define DISP_CCORR_SIZE				0x0030
#define DISP_CCORR_COEF_0			0x0080
#define DISP_CCORR_COEF_1			0x0084
#define DISP_CCORR_COEF_2			0x0088
#define DISP_CCORR_COEF_3			0x008C
#define DISP_CCORR_COEF_4			0x0090

#define DISP_DITHER_EN				0x0000
#define DITHER_EN				BIT(0)
#define DISP_DITHER_CFG				0x0020
#define DITHER_RELAY_MODE			BIT(0)
#define DISP_DITHER_SIZE			0x0030

#define DISP_GAMMA_EN				0x0000
#define DISP_GAMMA_CFG				0x0020
#define DISP_GAMMA_SIZE				0x0030
#define DISP_GAMMA_LUT				0x0700

#define LUT_10BIT_MASK				0x03ff

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

void mtk_ddp_write(struct cmdq_pkt *cmdq_pkt, unsigned int value,
		   struct mtk_ddp_comp *comp, unsigned int offset)
{
#if IS_REACHABLE(CONFIG_MTK_CMDQ)
	if (cmdq_pkt)
		cmdq_pkt_write(cmdq_pkt, comp->subsys,
			       comp->regs_pa + offset, value);
	else
#endif
		writel(value, comp->regs + offset);
}

void mtk_ddp_write_relaxed(struct cmdq_pkt *cmdq_pkt, unsigned int value,
			   struct mtk_ddp_comp *comp,
			   unsigned int offset)
{
#if IS_REACHABLE(CONFIG_MTK_CMDQ)
	if (cmdq_pkt)
		cmdq_pkt_write(cmdq_pkt, comp->subsys,
			       comp->regs_pa + offset, value);
	else
#endif
		writel_relaxed(value, comp->regs + offset);
}

void mtk_ddp_write_mask(struct cmdq_pkt *cmdq_pkt,
			unsigned int value,
			struct mtk_ddp_comp *comp,
			unsigned int offset,
			unsigned int mask)
{
#if IS_REACHABLE(CONFIG_MTK_CMDQ)
	if (cmdq_pkt) {
		cmdq_pkt_write_mask(cmdq_pkt, comp->subsys,
				    comp->regs_pa + offset, value, mask);
	} else {
#endif
		u32 tmp = readl(comp->regs + offset);

		tmp = (tmp & ~mask) | (value & mask);
		writel(tmp, comp->regs + offset);
#if IS_REACHABLE(CONFIG_MTK_CMDQ)
	}
#endif
}

void mtk_dither_set(struct mtk_ddp_comp *comp, unsigned int bpc,
		    unsigned int CFG, struct cmdq_pkt *cmdq_pkt)
{
	/* If bpc equal to 0, the dithering function didn't be enabled */
	if (bpc == 0)
		return;

	if (bpc >= MTK_MIN_BPC) {
		mtk_ddp_write(cmdq_pkt, 0, comp, DISP_DITHER_5);
		mtk_ddp_write(cmdq_pkt, 0, comp, DISP_DITHER_7);
		mtk_ddp_write(cmdq_pkt,
			      DITHER_LSB_ERR_SHIFT_R(MTK_MAX_BPC - bpc) |
			      DITHER_ADD_LSHIFT_R(MTK_MAX_BPC - bpc) |
			      DITHER_NEW_BIT_MODE,
			      comp, DISP_DITHER_15);
		mtk_ddp_write(cmdq_pkt,
			      DITHER_LSB_ERR_SHIFT_B(MTK_MAX_BPC - bpc) |
			      DITHER_ADD_LSHIFT_B(MTK_MAX_BPC - bpc) |
			      DITHER_LSB_ERR_SHIFT_G(MTK_MAX_BPC - bpc) |
			      DITHER_ADD_LSHIFT_G(MTK_MAX_BPC - bpc),
			      comp, DISP_DITHER_16);
		mtk_ddp_write(cmdq_pkt, DISP_DITHERING, comp, CFG);
	}
}

static void mtk_od_config(struct mtk_ddp_comp *comp, unsigned int w,
			  unsigned int h, unsigned int vrefresh,
			  unsigned int bpc, struct cmdq_pkt *cmdq_pkt)
{
	mtk_ddp_write(cmdq_pkt, w << 16 | h, comp, DISP_OD_SIZE);
	mtk_ddp_write(cmdq_pkt, OD_RELAYMODE, comp, DISP_OD_CFG);
	mtk_dither_set(comp, bpc, DISP_OD_CFG, cmdq_pkt);
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
			   unsigned int bpc, struct cmdq_pkt *cmdq_pkt)
{
	mtk_ddp_write(cmdq_pkt, w << 16 | h, comp, DISP_AAL_SIZE);
	mtk_ddp_write(cmdq_pkt, w << 16 | h, comp, DISP_AAL_OUTPUT_SIZE);
}

static void mtk_aal_start(struct mtk_ddp_comp *comp)
{
	writel(AAL_EN, comp->regs + DISP_AAL_EN);
}

static void mtk_aal_stop(struct mtk_ddp_comp *comp)
{
	writel_relaxed(0x0, comp->regs + DISP_AAL_EN);
}

static void mtk_ccorr_config(struct mtk_ddp_comp *comp, unsigned int w,
			     unsigned int h, unsigned int vrefresh,
			     unsigned int bpc, struct cmdq_pkt *cmdq_pkt)
{
	mtk_ddp_write(cmdq_pkt, h << 16 | w, comp, DISP_CCORR_SIZE);
	mtk_ddp_write(cmdq_pkt, CCORR_ENGINE_EN, comp, DISP_CCORR_CFG);
}

static void mtk_ccorr_start(struct mtk_ddp_comp *comp)
{
	writel(CCORR_EN, comp->regs + DISP_CCORR_EN);
}

static void mtk_ccorr_stop(struct mtk_ddp_comp *comp)
{
	writel_relaxed(0x0, comp->regs + DISP_CCORR_EN);
}

/* Converts a DRM S31.32 value to the HW S1.10 format. */
static u16 mtk_ctm_s31_32_to_s1_10(u64 in)
{
	u16 r;

	/* Sign bit. */
	r = in & BIT_ULL(63) ? BIT(11) : 0;

	if ((in & GENMASK_ULL(62, 33)) > 0) {
		/* identity value 0x100000000 -> 0x400, */
		/* if bigger this, set it to max 0x7ff. */
		r |= GENMASK(10, 0);
	} else {
		/* take the 11 most important bits. */
		r |= (in >> 22) & GENMASK(10, 0);
	}

	return r;
}

static void mtk_ccorr_ctm_set(struct mtk_ddp_comp *comp,
			      struct drm_crtc_state *state)
{
	struct drm_property_blob *blob = state->ctm;
	struct drm_color_ctm *ctm;
	const u64 *input;
	uint16_t coeffs[9] = { 0 };
	int i;
	struct cmdq_pkt *cmdq_pkt = NULL;

	if (!blob)
		return;

	ctm = (struct drm_color_ctm *)blob->data;
	input = ctm->matrix;

	for (i = 0; i < ARRAY_SIZE(coeffs); i++)
		coeffs[i] = mtk_ctm_s31_32_to_s1_10(input[i]);

	mtk_ddp_write(cmdq_pkt, coeffs[0] << 16 | coeffs[1],
		      comp, DISP_CCORR_COEF_0);
	mtk_ddp_write(cmdq_pkt, coeffs[2] << 16 | coeffs[3],
		      comp, DISP_CCORR_COEF_1);
	mtk_ddp_write(cmdq_pkt, coeffs[4] << 16 | coeffs[5],
		      comp, DISP_CCORR_COEF_2);
	mtk_ddp_write(cmdq_pkt, coeffs[6] << 16 | coeffs[7],
		      comp, DISP_CCORR_COEF_3);
	mtk_ddp_write(cmdq_pkt, coeffs[8] << 16,
		      comp, DISP_CCORR_COEF_4);
}

static void mtk_dither_config(struct mtk_ddp_comp *comp, unsigned int w,
			      unsigned int h, unsigned int vrefresh,
			      unsigned int bpc, struct cmdq_pkt *cmdq_pkt)
{
	mtk_ddp_write(cmdq_pkt, h << 16 | w, comp, DISP_DITHER_SIZE);
	mtk_ddp_write(cmdq_pkt, DITHER_RELAY_MODE, comp, DISP_DITHER_CFG);
}

static void mtk_dither_start(struct mtk_ddp_comp *comp)
{
	writel(DITHER_EN, comp->regs + DISP_DITHER_EN);
}

static void mtk_dither_stop(struct mtk_ddp_comp *comp)
{
	writel_relaxed(0x0, comp->regs + DISP_DITHER_EN);
}

static void mtk_gamma_config(struct mtk_ddp_comp *comp, unsigned int w,
			     unsigned int h, unsigned int vrefresh,
			     unsigned int bpc, struct cmdq_pkt *cmdq_pkt)
{
	mtk_ddp_write(cmdq_pkt, h << 16 | w, comp, DISP_GAMMA_SIZE);
	mtk_dither_set(comp, bpc, DISP_GAMMA_CFG, cmdq_pkt);
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

static const struct mtk_ddp_comp_funcs ddp_ccorr = {
	.config = mtk_ccorr_config,
	.start = mtk_ccorr_start,
	.stop = mtk_ccorr_stop,
	.ctm_set = mtk_ccorr_ctm_set,
};

static const struct mtk_ddp_comp_funcs ddp_dither = {
	.config = mtk_dither_config,
	.start = mtk_dither_start,
	.stop = mtk_dither_stop,
};

static const struct mtk_ddp_comp_funcs ddp_gamma = {
	.gamma_set = mtk_gamma_set,
	.config = mtk_gamma_config,
	.start = mtk_gamma_start,
	.stop = mtk_gamma_stop,
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
	[MTK_DISP_OVL_2L] = "ovl-2l",
	[MTK_DISP_RDMA] = "rdma",
	[MTK_DISP_WDMA] = "wdma",
	[MTK_DISP_COLOR] = "color",
	[MTK_DISP_CCORR] = "ccorr",
	[MTK_DISP_AAL] = "aal",
	[MTK_DISP_GAMMA] = "gamma",
	[MTK_DISP_DITHER] = "dither",
	[MTK_DISP_UFOE] = "ufoe",
	[MTK_DSI] = "dsi",
	[MTK_DPI] = "dpi",
	[MTK_DISP_PWM] = "pwm",
	[MTK_DISP_MUTEX] = "mutex",
	[MTK_DISP_OD] = "od",
	[MTK_DISP_BLS] = "bls",
};

struct mtk_ddp_comp_match {
	enum mtk_ddp_comp_type type;
	int alias_id;
	const struct mtk_ddp_comp_funcs *funcs;
};

static const struct mtk_ddp_comp_match mtk_ddp_matches[DDP_COMPONENT_ID_MAX] = {
	[DDP_COMPONENT_AAL0]	= { MTK_DISP_AAL,	0, &ddp_aal },
	[DDP_COMPONENT_AAL1]	= { MTK_DISP_AAL,	1, &ddp_aal },
	[DDP_COMPONENT_BLS]	= { MTK_DISP_BLS,	0, NULL },
	[DDP_COMPONENT_CCORR]	= { MTK_DISP_CCORR,	0, &ddp_ccorr },
	[DDP_COMPONENT_COLOR0]	= { MTK_DISP_COLOR,	0, NULL },
	[DDP_COMPONENT_COLOR1]	= { MTK_DISP_COLOR,	1, NULL },
	[DDP_COMPONENT_DITHER]	= { MTK_DISP_DITHER,	0, &ddp_dither },
	[DDP_COMPONENT_DPI0]	= { MTK_DPI,		0, NULL },
	[DDP_COMPONENT_DPI1]	= { MTK_DPI,		1, NULL },
	[DDP_COMPONENT_DSI0]	= { MTK_DSI,		0, NULL },
	[DDP_COMPONENT_DSI1]	= { MTK_DSI,		1, NULL },
	[DDP_COMPONENT_DSI2]	= { MTK_DSI,		2, NULL },
	[DDP_COMPONENT_DSI3]	= { MTK_DSI,		3, NULL },
	[DDP_COMPONENT_GAMMA]	= { MTK_DISP_GAMMA,	0, &ddp_gamma },
	[DDP_COMPONENT_OD0]	= { MTK_DISP_OD,	0, &ddp_od },
	[DDP_COMPONENT_OD1]	= { MTK_DISP_OD,	1, &ddp_od },
	[DDP_COMPONENT_OVL0]	= { MTK_DISP_OVL,	0, NULL },
	[DDP_COMPONENT_OVL1]	= { MTK_DISP_OVL,	1, NULL },
	[DDP_COMPONENT_OVL_2L0]	= { MTK_DISP_OVL_2L,	0, NULL },
	[DDP_COMPONENT_OVL_2L1]	= { MTK_DISP_OVL_2L,	1, NULL },
	[DDP_COMPONENT_PWM0]	= { MTK_DISP_PWM,	0, NULL },
	[DDP_COMPONENT_PWM1]	= { MTK_DISP_PWM,	1, NULL },
	[DDP_COMPONENT_PWM2]	= { MTK_DISP_PWM,	2, NULL },
	[DDP_COMPONENT_RDMA0]	= { MTK_DISP_RDMA,	0, NULL },
	[DDP_COMPONENT_RDMA1]	= { MTK_DISP_RDMA,	1, NULL },
	[DDP_COMPONENT_RDMA2]	= { MTK_DISP_RDMA,	2, NULL },
	[DDP_COMPONENT_UFOE]	= { MTK_DISP_UFOE,	0, &ddp_ufoe },
	[DDP_COMPONENT_WDMA0]	= { MTK_DISP_WDMA,	0, NULL },
	[DDP_COMPONENT_WDMA1]	= { MTK_DISP_WDMA,	1, NULL },
};

static bool mtk_drm_find_comp_in_ddp(struct mtk_ddp_comp ddp_comp,
				     const enum mtk_ddp_comp_id *path,
				     unsigned int path_len)
{
	unsigned int i;

	if (path == NULL)
		return false;

	for (i = 0U; i < path_len; i++)
		if (ddp_comp.id == path[i])
			return true;

	return false;
}

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

unsigned int mtk_drm_find_possible_crtc_by_comp(struct drm_device *drm,
						struct mtk_ddp_comp ddp_comp)
{
	struct mtk_drm_private *private = drm->dev_private;
	unsigned int ret = 0;

	if (mtk_drm_find_comp_in_ddp(ddp_comp, private->data->main_path, private->data->main_len))
		ret = BIT(0);
	else if (mtk_drm_find_comp_in_ddp(ddp_comp, private->data->ext_path,
					  private->data->ext_len))
		ret = BIT(1);
	else if (mtk_drm_find_comp_in_ddp(ddp_comp, private->data->third_path,
					  private->data->third_len))
		ret = BIT(2);
	else
		DRM_INFO("Failed to find comp in ddp table\n");

	return ret;
}

int mtk_ddp_comp_init(struct device *dev, struct device_node *node,
		      struct mtk_ddp_comp *comp, enum mtk_ddp_comp_id comp_id,
		      const struct mtk_ddp_comp_funcs *funcs)
{
	enum mtk_ddp_comp_type type;
	struct device_node *larb_node;
	struct platform_device *larb_pdev;
#if IS_REACHABLE(CONFIG_MTK_CMDQ)
	struct resource res;
	struct cmdq_client_reg cmdq_reg;
	int ret;
#endif

	if (comp_id < 0 || comp_id >= DDP_COMPONENT_ID_MAX)
		return -EINVAL;

	type = mtk_ddp_matches[comp_id].type;

	comp->id = comp_id;
	comp->funcs = funcs ?: mtk_ddp_matches[comp_id].funcs;

	if (comp_id == DDP_COMPONENT_BLS ||
	    comp_id == DDP_COMPONENT_DPI0 ||
	    comp_id == DDP_COMPONENT_DPI1 ||
	    comp_id == DDP_COMPONENT_DSI0 ||
	    comp_id == DDP_COMPONENT_DSI1 ||
	    comp_id == DDP_COMPONENT_DSI2 ||
	    comp_id == DDP_COMPONENT_DSI3 ||
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
		return PTR_ERR(comp->clk);

	/* Only DMA capable components need the LARB property */
	comp->larb_dev = NULL;
	if (type != MTK_DISP_OVL &&
	    type != MTK_DISP_OVL_2L &&
	    type != MTK_DISP_RDMA &&
	    type != MTK_DISP_WDMA)
		return 0;

	larb_node = of_parse_phandle(node, "mediatek,larb", 0);
	if (!larb_node) {
		dev_err(dev,
			"Missing mediadek,larb phandle in %pOF node\n", node);
		return -EINVAL;
	}

	larb_pdev = of_find_device_by_node(larb_node);
	if (!larb_pdev) {
		dev_warn(dev, "Waiting for larb device %pOF\n", larb_node);
		of_node_put(larb_node);
		return -EPROBE_DEFER;
	}
	of_node_put(larb_node);

	comp->larb_dev = &larb_pdev->dev;

#if IS_REACHABLE(CONFIG_MTK_CMDQ)
	if (of_address_to_resource(node, 0, &res) != 0) {
		dev_err(dev, "Missing reg in %s node\n", node->full_name);
		put_device(&larb_pdev->dev);
		return -EINVAL;
	}
	comp->regs_pa = res.start;

	ret = cmdq_dev_get_client_reg(dev, &cmdq_reg, 0);
	if (ret)
		dev_dbg(dev, "get mediatek,gce-client-reg fail!\n");
	else
		comp->subsys = cmdq_reg.subsys;
#endif
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
