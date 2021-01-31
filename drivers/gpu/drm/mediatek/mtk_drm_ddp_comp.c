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
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/soc/mediatek/mtk-cmdq.h>
#include <drm/drm_print.h>

#include "mtk_disp_drv.h"
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
#define DITHER_ENGINE_EN			BIT(1)
#define DISP_DITHER_SIZE			0x0030

#define LUT_10BIT_MASK				0x03ff

#define OD_RELAYMODE				BIT(0)

#define UFO_BYPASS				BIT(2)

#define AAL_EN					BIT(0)

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

struct mtk_ddp_comp_dev {
	struct clk *clk;
	void __iomem *regs;
	struct cmdq_client_reg cmdq_reg;
};

void mtk_ddp_write(struct cmdq_pkt *cmdq_pkt, unsigned int value,
		   struct cmdq_client_reg *cmdq_reg, void __iomem *regs,
		   unsigned int offset)
{
#if IS_REACHABLE(CONFIG_MTK_CMDQ)
	if (cmdq_pkt)
		cmdq_pkt_write(cmdq_pkt, cmdq_reg->subsys,
			       cmdq_reg->offset + offset, value);
	else
#endif
		writel(value, regs + offset);
}

void mtk_ddp_write_relaxed(struct cmdq_pkt *cmdq_pkt, unsigned int value,
			   struct cmdq_client_reg *cmdq_reg, void __iomem *regs,
			   unsigned int offset)
{
#if IS_REACHABLE(CONFIG_MTK_CMDQ)
	if (cmdq_pkt)
		cmdq_pkt_write(cmdq_pkt, cmdq_reg->subsys,
			       cmdq_reg->offset + offset, value);
	else
#endif
		writel_relaxed(value, regs + offset);
}

void mtk_ddp_write_mask(struct cmdq_pkt *cmdq_pkt, unsigned int value,
			struct cmdq_client_reg *cmdq_reg, void __iomem *regs,
			unsigned int offset, unsigned int mask)
{
#if IS_REACHABLE(CONFIG_MTK_CMDQ)
	if (cmdq_pkt) {
		cmdq_pkt_write_mask(cmdq_pkt, cmdq_reg->subsys,
				    cmdq_reg->offset + offset, value, mask);
	} else {
#endif
		u32 tmp = readl(regs + offset);

		tmp = (tmp & ~mask) | (value & mask);
		writel(tmp, regs + offset);
#if IS_REACHABLE(CONFIG_MTK_CMDQ)
	}
#endif
}

static int mtk_ddp_clk_enable(struct device *dev)
{
	struct mtk_ddp_comp_dev *priv = dev_get_drvdata(dev);

	return clk_prepare_enable(priv->clk);
}

static void mtk_ddp_clk_disable(struct device *dev)
{
	struct mtk_ddp_comp_dev *priv = dev_get_drvdata(dev);

	clk_disable_unprepare(priv->clk);
}

void mtk_dither_set_common(void __iomem *regs, struct cmdq_client_reg *cmdq_reg,
			   unsigned int bpc, unsigned int cfg,
			   unsigned int dither_en, struct cmdq_pkt *cmdq_pkt)
{
	/* If bpc equal to 0, the dithering function didn't be enabled */
	if (bpc == 0)
		return;

	if (bpc >= MTK_MIN_BPC) {
		mtk_ddp_write(cmdq_pkt, 0, cmdq_reg, regs, DISP_DITHER_5);
		mtk_ddp_write(cmdq_pkt, 0, cmdq_reg, regs, DISP_DITHER_7);
		mtk_ddp_write(cmdq_pkt,
			      DITHER_LSB_ERR_SHIFT_R(MTK_MAX_BPC - bpc) |
			      DITHER_ADD_LSHIFT_R(MTK_MAX_BPC - bpc) |
			      DITHER_NEW_BIT_MODE,
			      cmdq_reg, regs, DISP_DITHER_15);
		mtk_ddp_write(cmdq_pkt,
			      DITHER_LSB_ERR_SHIFT_B(MTK_MAX_BPC - bpc) |
			      DITHER_ADD_LSHIFT_B(MTK_MAX_BPC - bpc) |
			      DITHER_LSB_ERR_SHIFT_G(MTK_MAX_BPC - bpc) |
			      DITHER_ADD_LSHIFT_G(MTK_MAX_BPC - bpc),
			      cmdq_reg, regs, DISP_DITHER_16);
		mtk_ddp_write(cmdq_pkt, dither_en, cmdq_reg, regs, cfg);
	}
}

static void mtk_dither_set(struct device *dev, unsigned int bpc,
		    unsigned int cfg, struct cmdq_pkt *cmdq_pkt)
{
	struct mtk_ddp_comp_dev *priv = dev_get_drvdata(dev);

	mtk_dither_set_common(priv->regs, &priv->cmdq_reg, bpc, cfg,
			      DISP_DITHERING, cmdq_pkt);
}

static void mtk_od_config(struct device *dev, unsigned int w,
			  unsigned int h, unsigned int vrefresh,
			  unsigned int bpc, struct cmdq_pkt *cmdq_pkt)
{
	struct mtk_ddp_comp_dev *priv = dev_get_drvdata(dev);

	mtk_ddp_write(cmdq_pkt, w << 16 | h, &priv->cmdq_reg, priv->regs, DISP_OD_SIZE);
	mtk_ddp_write(cmdq_pkt, OD_RELAYMODE, &priv->cmdq_reg, priv->regs, DISP_OD_CFG);
	mtk_dither_set(dev, bpc, DISP_OD_CFG, cmdq_pkt);
}

static void mtk_od_start(struct device *dev)
{
	struct mtk_ddp_comp_dev *priv = dev_get_drvdata(dev);

	writel(1, priv->regs + DISP_OD_EN);
}

static void mtk_ufoe_start(struct device *dev)
{
	struct mtk_ddp_comp_dev *priv = dev_get_drvdata(dev);

	writel(UFO_BYPASS, priv->regs + DISP_REG_UFO_START);
}

static void mtk_aal_config(struct device *dev, unsigned int w,
			   unsigned int h, unsigned int vrefresh,
			   unsigned int bpc, struct cmdq_pkt *cmdq_pkt)
{
	struct mtk_ddp_comp_dev *priv = dev_get_drvdata(dev);

	mtk_ddp_write(cmdq_pkt, w << 16 | h, &priv->cmdq_reg, priv->regs, DISP_AAL_SIZE);
}

static void mtk_aal_gamma_set(struct device *dev, struct drm_crtc_state *state)
{
	struct mtk_ddp_comp_dev *priv = dev_get_drvdata(dev);

	mtk_gamma_set_common(priv->regs, state);
}

static void mtk_aal_start(struct device *dev)
{
	struct mtk_ddp_comp_dev *priv = dev_get_drvdata(dev);

	writel(AAL_EN, priv->regs + DISP_AAL_EN);
}

static void mtk_aal_stop(struct device *dev)
{
	struct mtk_ddp_comp_dev *priv = dev_get_drvdata(dev);

	writel_relaxed(0x0, priv->regs + DISP_AAL_EN);
}

static void mtk_ccorr_config(struct device *dev, unsigned int w,
			     unsigned int h, unsigned int vrefresh,
			     unsigned int bpc, struct cmdq_pkt *cmdq_pkt)
{
	struct mtk_ddp_comp_dev *priv = dev_get_drvdata(dev);

	mtk_ddp_write(cmdq_pkt, h << 16 | w, &priv->cmdq_reg, priv->regs, DISP_CCORR_SIZE);
	mtk_ddp_write(cmdq_pkt, CCORR_ENGINE_EN, &priv->cmdq_reg, priv->regs, DISP_CCORR_CFG);
}

static void mtk_ccorr_start(struct device *dev)
{
	struct mtk_ddp_comp_dev *priv = dev_get_drvdata(dev);

	writel(CCORR_EN, priv->regs + DISP_CCORR_EN);
}

static void mtk_ccorr_stop(struct device *dev)
{
	struct mtk_ddp_comp_dev *priv = dev_get_drvdata(dev);

	writel_relaxed(0x0, priv->regs + DISP_CCORR_EN);
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

static void mtk_ccorr_ctm_set(struct device *dev,
			      struct drm_crtc_state *state)
{
	struct mtk_ddp_comp_dev *priv = dev_get_drvdata(dev);
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
		      &priv->cmdq_reg, priv->regs, DISP_CCORR_COEF_0);
	mtk_ddp_write(cmdq_pkt, coeffs[2] << 16 | coeffs[3],
		      &priv->cmdq_reg, priv->regs, DISP_CCORR_COEF_1);
	mtk_ddp_write(cmdq_pkt, coeffs[4] << 16 | coeffs[5],
		      &priv->cmdq_reg, priv->regs, DISP_CCORR_COEF_2);
	mtk_ddp_write(cmdq_pkt, coeffs[6] << 16 | coeffs[7],
		      &priv->cmdq_reg, priv->regs, DISP_CCORR_COEF_3);
	mtk_ddp_write(cmdq_pkt, coeffs[8] << 16,
		      &priv->cmdq_reg, priv->regs, DISP_CCORR_COEF_4);
}

static void mtk_dither_config(struct device *dev, unsigned int w,
			      unsigned int h, unsigned int vrefresh,
			      unsigned int bpc, struct cmdq_pkt *cmdq_pkt)
{
	struct mtk_ddp_comp_dev *priv = dev_get_drvdata(dev);

	mtk_ddp_write(cmdq_pkt, h << 16 | w, &priv->cmdq_reg, priv->regs, DISP_DITHER_SIZE);
	mtk_ddp_write(cmdq_pkt, DITHER_RELAY_MODE, &priv->cmdq_reg, priv->regs, DISP_DITHER_CFG);
	mtk_dither_set_common(priv->regs, &priv->cmdq_reg, bpc, DISP_DITHER_CFG,
			      DITHER_ENGINE_EN, cmdq_pkt);
}

static void mtk_dither_start(struct device *dev)
{
	struct mtk_ddp_comp_dev *priv = dev_get_drvdata(dev);

	writel(DITHER_EN, priv->regs + DISP_DITHER_EN);
}

static void mtk_dither_stop(struct device *dev)
{
	struct mtk_ddp_comp_dev *priv = dev_get_drvdata(dev);

	writel_relaxed(0x0, priv->regs + DISP_DITHER_EN);
}

static const struct mtk_ddp_comp_funcs ddp_aal = {
	.clk_enable = mtk_ddp_clk_enable,
	.clk_disable = mtk_ddp_clk_disable,
	.gamma_set = mtk_aal_gamma_set,
	.config = mtk_aal_config,
	.start = mtk_aal_start,
	.stop = mtk_aal_stop,
};

static const struct mtk_ddp_comp_funcs ddp_ccorr = {
	.clk_enable = mtk_ddp_clk_enable,
	.clk_disable = mtk_ddp_clk_disable,
	.config = mtk_ccorr_config,
	.start = mtk_ccorr_start,
	.stop = mtk_ccorr_stop,
	.ctm_set = mtk_ccorr_ctm_set,
};

static const struct mtk_ddp_comp_funcs ddp_color = {
	.clk_enable = mtk_color_clk_enable,
	.clk_disable = mtk_color_clk_disable,
	.config = mtk_color_config,
	.start = mtk_color_start,
};

static const struct mtk_ddp_comp_funcs ddp_dither = {
	.clk_enable = mtk_ddp_clk_enable,
	.clk_disable = mtk_ddp_clk_disable,
	.config = mtk_dither_config,
	.start = mtk_dither_start,
	.stop = mtk_dither_stop,
};

static const struct mtk_ddp_comp_funcs ddp_dpi = {
	.start = mtk_dpi_start,
	.stop = mtk_dpi_stop,
};

static const struct mtk_ddp_comp_funcs ddp_dsi = {
	.start = mtk_dsi_ddp_start,
	.stop = mtk_dsi_ddp_stop,
};

static const struct mtk_ddp_comp_funcs ddp_gamma = {
	.clk_enable = mtk_gamma_clk_enable,
	.clk_disable = mtk_gamma_clk_disable,
	.gamma_set = mtk_gamma_set,
	.config = mtk_gamma_config,
	.start = mtk_gamma_start,
	.stop = mtk_gamma_stop,
};

static const struct mtk_ddp_comp_funcs ddp_od = {
	.clk_enable = mtk_ddp_clk_enable,
	.clk_disable = mtk_ddp_clk_disable,
	.config = mtk_od_config,
	.start = mtk_od_start,
};

static const struct mtk_ddp_comp_funcs ddp_ovl = {
	.clk_enable = mtk_ovl_clk_enable,
	.clk_disable = mtk_ovl_clk_disable,
	.config = mtk_ovl_config,
	.start = mtk_ovl_start,
	.stop = mtk_ovl_stop,
	.enable_vblank = mtk_ovl_enable_vblank,
	.disable_vblank = mtk_ovl_disable_vblank,
	.supported_rotations = mtk_ovl_supported_rotations,
	.layer_nr = mtk_ovl_layer_nr,
	.layer_check = mtk_ovl_layer_check,
	.layer_config = mtk_ovl_layer_config,
	.bgclr_in_on = mtk_ovl_bgclr_in_on,
	.bgclr_in_off = mtk_ovl_bgclr_in_off,
};

static const struct mtk_ddp_comp_funcs ddp_rdma = {
	.clk_enable = mtk_rdma_clk_enable,
	.clk_disable = mtk_rdma_clk_disable,
	.config = mtk_rdma_config,
	.start = mtk_rdma_start,
	.stop = mtk_rdma_stop,
	.enable_vblank = mtk_rdma_enable_vblank,
	.disable_vblank = mtk_rdma_disable_vblank,
	.layer_nr = mtk_rdma_layer_nr,
	.layer_config = mtk_rdma_layer_config,
};

static const struct mtk_ddp_comp_funcs ddp_ufoe = {
	.clk_enable = mtk_ddp_clk_enable,
	.clk_disable = mtk_ddp_clk_disable,
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
	[DDP_COMPONENT_COLOR0]	= { MTK_DISP_COLOR,	0, &ddp_color },
	[DDP_COMPONENT_COLOR1]	= { MTK_DISP_COLOR,	1, &ddp_color },
	[DDP_COMPONENT_DITHER]	= { MTK_DISP_DITHER,	0, &ddp_dither },
	[DDP_COMPONENT_DPI0]	= { MTK_DPI,		0, &ddp_dpi },
	[DDP_COMPONENT_DPI1]	= { MTK_DPI,		1, &ddp_dpi },
	[DDP_COMPONENT_DSI0]	= { MTK_DSI,		0, &ddp_dsi },
	[DDP_COMPONENT_DSI1]	= { MTK_DSI,		1, &ddp_dsi },
	[DDP_COMPONENT_DSI2]	= { MTK_DSI,		2, &ddp_dsi },
	[DDP_COMPONENT_DSI3]	= { MTK_DSI,		3, &ddp_dsi },
	[DDP_COMPONENT_GAMMA]	= { MTK_DISP_GAMMA,	0, &ddp_gamma },
	[DDP_COMPONENT_OD0]	= { MTK_DISP_OD,	0, &ddp_od },
	[DDP_COMPONENT_OD1]	= { MTK_DISP_OD,	1, &ddp_od },
	[DDP_COMPONENT_OVL0]	= { MTK_DISP_OVL,	0, &ddp_ovl },
	[DDP_COMPONENT_OVL1]	= { MTK_DISP_OVL,	1, &ddp_ovl },
	[DDP_COMPONENT_OVL_2L0]	= { MTK_DISP_OVL_2L,	0, &ddp_ovl },
	[DDP_COMPONENT_OVL_2L1]	= { MTK_DISP_OVL_2L,	1, &ddp_ovl },
	[DDP_COMPONENT_PWM0]	= { MTK_DISP_PWM,	0, NULL },
	[DDP_COMPONENT_PWM1]	= { MTK_DISP_PWM,	1, NULL },
	[DDP_COMPONENT_PWM2]	= { MTK_DISP_PWM,	2, NULL },
	[DDP_COMPONENT_RDMA0]	= { MTK_DISP_RDMA,	0, &ddp_rdma },
	[DDP_COMPONENT_RDMA1]	= { MTK_DISP_RDMA,	1, &ddp_rdma },
	[DDP_COMPONENT_RDMA2]	= { MTK_DISP_RDMA,	2, &ddp_rdma },
	[DDP_COMPONENT_UFOE]	= { MTK_DISP_UFOE,	0, &ddp_ufoe },
	[DDP_COMPONENT_WDMA0]	= { MTK_DISP_WDMA,	0, NULL },
	[DDP_COMPONENT_WDMA1]	= { MTK_DISP_WDMA,	1, NULL },
};

static bool mtk_drm_find_comp_in_ddp(struct device *dev,
				     const enum mtk_ddp_comp_id *path,
				     unsigned int path_len,
				     struct mtk_ddp_comp *ddp_comp)
{
	unsigned int i;

	if (path == NULL)
		return false;

	for (i = 0U; i < path_len; i++)
		if (dev == ddp_comp[path[i]].dev)
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
						struct device *dev)
{
	struct mtk_drm_private *private = drm->dev_private;
	unsigned int ret = 0;

	if (mtk_drm_find_comp_in_ddp(dev, private->data->main_path, private->data->main_len,
				     private->ddp_comp))
		ret = BIT(0);
	else if (mtk_drm_find_comp_in_ddp(dev, private->data->ext_path,
					  private->data->ext_len, private->ddp_comp))
		ret = BIT(1);
	else if (mtk_drm_find_comp_in_ddp(dev, private->data->third_path,
					  private->data->third_len, private->ddp_comp))
		ret = BIT(2);
	else
		DRM_INFO("Failed to find comp in ddp table\n");

	return ret;
}

static int mtk_ddp_get_larb_dev(struct device_node *node, struct mtk_ddp_comp *comp,
				struct device *dev)
{
	struct device_node *larb_node;
	struct platform_device *larb_pdev;

	larb_node = of_parse_phandle(node, "mediatek,larb", 0);
	if (!larb_node) {
		dev_err(dev, "Missing mediadek,larb phandle in %pOF node\n", node);
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

	return 0;
}

int mtk_ddp_comp_init(struct device_node *node, struct mtk_ddp_comp *comp,
		      enum mtk_ddp_comp_id comp_id)
{
	struct platform_device *comp_pdev;
	enum mtk_ddp_comp_type type;
	struct mtk_ddp_comp_dev *priv;
	int ret;

	if (comp_id < 0 || comp_id >= DDP_COMPONENT_ID_MAX)
		return -EINVAL;

	type = mtk_ddp_matches[comp_id].type;

	comp->id = comp_id;
	comp->funcs = mtk_ddp_matches[comp_id].funcs;
	comp_pdev = of_find_device_by_node(node);
	if (!comp_pdev) {
		DRM_INFO("Waiting for device %s\n", node->full_name);
		return -EPROBE_DEFER;
	}
	comp->dev = &comp_pdev->dev;

	/* Only DMA capable components need the LARB property */
	if (type == MTK_DISP_OVL ||
	    type == MTK_DISP_OVL_2L ||
	    type == MTK_DISP_RDMA ||
	    type == MTK_DISP_WDMA) {
		ret = mtk_ddp_get_larb_dev(node, comp, comp->dev);
		if (ret)
			return ret;
	}

	if (type == MTK_DISP_BLS ||
	    type == MTK_DISP_COLOR ||
	    type == MTK_DISP_GAMMA ||
	    type == MTK_DPI ||
	    type == MTK_DSI ||
	    type == MTK_DISP_OVL ||
	    type == MTK_DISP_OVL_2L ||
	    type == MTK_DISP_PWM ||
	    type == MTK_DISP_RDMA)
		return 0;

	priv = devm_kzalloc(comp->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->regs = of_iomap(node, 0);
	priv->clk = of_clk_get(node, 0);
	if (IS_ERR(priv->clk))
		return PTR_ERR(priv->clk);

#if IS_REACHABLE(CONFIG_MTK_CMDQ)
	ret = cmdq_dev_get_client_reg(comp->dev, &priv->cmdq_reg, 0);
	if (ret)
		dev_dbg(comp->dev, "get mediatek,gce-client-reg fail!\n");
#endif

	platform_set_drvdata(comp_pdev, priv);

	return 0;
}
