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

#include "mtk_crtc.h"
#include "mtk_ddp_comp.h"
#include "mtk_disp_drv.h"
#include "mtk_drm_drv.h"
#include "mtk_plane.h"


#define DISP_REG_DITHER_EN			0x0000
#define DITHER_EN				BIT(0)
#define DISP_REG_DITHER_CFG			0x0020
#define DITHER_RELAY_MODE			BIT(0)
#define DITHER_ENGINE_EN			BIT(1)
#define DISP_DITHERING				BIT(2)
#define DISP_REG_DITHER_SIZE			0x0030
#define DISP_REG_DITHER_5			0x0114
#define DISP_REG_DITHER_7			0x011c
#define DISP_REG_DITHER_15			0x013c
#define DITHER_LSB_ERR_SHIFT_R(x)		(((x) & 0x7) << 28)
#define DITHER_ADD_LSHIFT_R(x)			(((x) & 0x7) << 20)
#define DITHER_NEW_BIT_MODE			BIT(0)
#define DISP_REG_DITHER_16			0x0140
#define DITHER_LSB_ERR_SHIFT_B(x)		(((x) & 0x7) << 28)
#define DITHER_ADD_LSHIFT_B(x)			(((x) & 0x7) << 20)
#define DITHER_LSB_ERR_SHIFT_G(x)		(((x) & 0x7) << 12)
#define DITHER_ADD_LSHIFT_G(x)			(((x) & 0x7) << 4)

#define DISP_REG_DSC_CON			0x0000
#define DSC_EN					BIT(0)
#define DSC_DUAL_INOUT				BIT(2)
#define DSC_BYPASS				BIT(4)
#define DSC_UFOE_SEL				BIT(16)

#define DISP_REG_OD_EN				0x0000
#define DISP_REG_OD_CFG				0x0020
#define OD_RELAYMODE				BIT(0)
#define DISP_REG_OD_SIZE			0x0030

#define DISP_REG_POSTMASK_EN			0x0000
#define POSTMASK_EN					BIT(0)
#define DISP_REG_POSTMASK_CFG			0x0020
#define POSTMASK_RELAY_MODE				BIT(0)
#define DISP_REG_POSTMASK_SIZE			0x0030

#define DISP_REG_UFO_START			0x0000
#define UFO_BYPASS				BIT(2)

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
		mtk_ddp_write(cmdq_pkt, 0, cmdq_reg, regs, DISP_REG_DITHER_5);
		mtk_ddp_write(cmdq_pkt, 0, cmdq_reg, regs, DISP_REG_DITHER_7);
		mtk_ddp_write(cmdq_pkt,
			      DITHER_LSB_ERR_SHIFT_R(MTK_MAX_BPC - bpc) |
			      DITHER_ADD_LSHIFT_R(MTK_MAX_BPC - bpc) |
			      DITHER_NEW_BIT_MODE,
			      cmdq_reg, regs, DISP_REG_DITHER_15);
		mtk_ddp_write(cmdq_pkt,
			      DITHER_LSB_ERR_SHIFT_B(MTK_MAX_BPC - bpc) |
			      DITHER_ADD_LSHIFT_B(MTK_MAX_BPC - bpc) |
			      DITHER_LSB_ERR_SHIFT_G(MTK_MAX_BPC - bpc) |
			      DITHER_ADD_LSHIFT_G(MTK_MAX_BPC - bpc),
			      cmdq_reg, regs, DISP_REG_DITHER_16);
		mtk_ddp_write(cmdq_pkt, dither_en, cmdq_reg, regs, cfg);
	}
}

static void mtk_dither_config(struct device *dev, unsigned int w,
			      unsigned int h, unsigned int vrefresh,
			      unsigned int bpc, struct cmdq_pkt *cmdq_pkt)
{
	struct mtk_ddp_comp_dev *priv = dev_get_drvdata(dev);

	mtk_ddp_write(cmdq_pkt, w << 16 | h, &priv->cmdq_reg, priv->regs, DISP_REG_DITHER_SIZE);
	mtk_ddp_write(cmdq_pkt, DITHER_RELAY_MODE, &priv->cmdq_reg, priv->regs,
		      DISP_REG_DITHER_CFG);
	mtk_dither_set_common(priv->regs, &priv->cmdq_reg, bpc, DISP_REG_DITHER_CFG,
			      DITHER_ENGINE_EN, cmdq_pkt);
}

static void mtk_dither_start(struct device *dev)
{
	struct mtk_ddp_comp_dev *priv = dev_get_drvdata(dev);

	writel(DITHER_EN, priv->regs + DISP_REG_DITHER_EN);
}

static void mtk_dither_stop(struct device *dev)
{
	struct mtk_ddp_comp_dev *priv = dev_get_drvdata(dev);

	writel_relaxed(0x0, priv->regs + DISP_REG_DITHER_EN);
}

static void mtk_dither_set(struct device *dev, unsigned int bpc,
			   unsigned int cfg, struct cmdq_pkt *cmdq_pkt)
{
	struct mtk_ddp_comp_dev *priv = dev_get_drvdata(dev);

	mtk_dither_set_common(priv->regs, &priv->cmdq_reg, bpc, cfg,
			      DISP_DITHERING, cmdq_pkt);
}

static void mtk_dsc_config(struct device *dev, unsigned int w,
			   unsigned int h, unsigned int vrefresh,
			   unsigned int bpc, struct cmdq_pkt *cmdq_pkt)
{
	struct mtk_ddp_comp_dev *priv = dev_get_drvdata(dev);

	/* dsc bypass mode */
	mtk_ddp_write_mask(cmdq_pkt, DSC_BYPASS, &priv->cmdq_reg, priv->regs,
			   DISP_REG_DSC_CON, DSC_BYPASS);
	mtk_ddp_write_mask(cmdq_pkt, DSC_UFOE_SEL, &priv->cmdq_reg, priv->regs,
			   DISP_REG_DSC_CON, DSC_UFOE_SEL);
	mtk_ddp_write_mask(cmdq_pkt, DSC_DUAL_INOUT, &priv->cmdq_reg, priv->regs,
			   DISP_REG_DSC_CON, DSC_DUAL_INOUT);
}

static void mtk_dsc_start(struct device *dev)
{
	struct mtk_ddp_comp_dev *priv = dev_get_drvdata(dev);

	/* write with mask to reserve the value set in mtk_dsc_config */
	mtk_ddp_write_mask(NULL, DSC_EN, &priv->cmdq_reg, priv->regs, DISP_REG_DSC_CON, DSC_EN);
}

static void mtk_dsc_stop(struct device *dev)
{
	struct mtk_ddp_comp_dev *priv = dev_get_drvdata(dev);

	writel_relaxed(0x0, priv->regs + DISP_REG_DSC_CON);
}

static void mtk_od_config(struct device *dev, unsigned int w,
			  unsigned int h, unsigned int vrefresh,
			  unsigned int bpc, struct cmdq_pkt *cmdq_pkt)
{
	struct mtk_ddp_comp_dev *priv = dev_get_drvdata(dev);

	mtk_ddp_write(cmdq_pkt, w << 16 | h, &priv->cmdq_reg, priv->regs, DISP_REG_OD_SIZE);
	mtk_ddp_write(cmdq_pkt, OD_RELAYMODE, &priv->cmdq_reg, priv->regs, DISP_REG_OD_CFG);
	mtk_dither_set(dev, bpc, DISP_REG_OD_CFG, cmdq_pkt);
}

static void mtk_od_start(struct device *dev)
{
	struct mtk_ddp_comp_dev *priv = dev_get_drvdata(dev);

	writel(1, priv->regs + DISP_REG_OD_EN);
}

static void mtk_postmask_config(struct device *dev, unsigned int w,
				unsigned int h, unsigned int vrefresh,
				unsigned int bpc, struct cmdq_pkt *cmdq_pkt)
{
	struct mtk_ddp_comp_dev *priv = dev_get_drvdata(dev);

	mtk_ddp_write(cmdq_pkt, w << 16 | h, &priv->cmdq_reg, priv->regs,
		      DISP_REG_POSTMASK_SIZE);
	mtk_ddp_write(cmdq_pkt, POSTMASK_RELAY_MODE, &priv->cmdq_reg,
		      priv->regs, DISP_REG_POSTMASK_CFG);
}

static void mtk_postmask_start(struct device *dev)
{
	struct mtk_ddp_comp_dev *priv = dev_get_drvdata(dev);

	writel(POSTMASK_EN, priv->regs + DISP_REG_POSTMASK_EN);
}

static void mtk_postmask_stop(struct device *dev)
{
	struct mtk_ddp_comp_dev *priv = dev_get_drvdata(dev);

	writel_relaxed(0x0, priv->regs + DISP_REG_POSTMASK_EN);
}

static void mtk_ufoe_start(struct device *dev)
{
	struct mtk_ddp_comp_dev *priv = dev_get_drvdata(dev);

	writel(UFO_BYPASS, priv->regs + DISP_REG_UFO_START);
}

static const struct mtk_ddp_comp_funcs ddp_aal = {
	.clk_enable = mtk_aal_clk_enable,
	.clk_disable = mtk_aal_clk_disable,
	.gamma_get_lut_size = mtk_aal_gamma_get_lut_size,
	.gamma_set = mtk_aal_gamma_set,
	.config = mtk_aal_config,
	.start = mtk_aal_start,
	.stop = mtk_aal_stop,
};

static const struct mtk_ddp_comp_funcs ddp_ccorr = {
	.clk_enable = mtk_ccorr_clk_enable,
	.clk_disable = mtk_ccorr_clk_disable,
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
	.encoder_index = mtk_dpi_encoder_index,
};

static const struct mtk_ddp_comp_funcs ddp_dsc = {
	.clk_enable = mtk_ddp_clk_enable,
	.clk_disable = mtk_ddp_clk_disable,
	.config = mtk_dsc_config,
	.start = mtk_dsc_start,
	.stop = mtk_dsc_stop,
};

static const struct mtk_ddp_comp_funcs ddp_dsi = {
	.start = mtk_dsi_ddp_start,
	.stop = mtk_dsi_ddp_stop,
	.encoder_index = mtk_dsi_encoder_index,
};

static const struct mtk_ddp_comp_funcs ddp_gamma = {
	.clk_enable = mtk_gamma_clk_enable,
	.clk_disable = mtk_gamma_clk_disable,
	.gamma_get_lut_size = mtk_gamma_get_lut_size,
	.gamma_set = mtk_gamma_set,
	.config = mtk_gamma_config,
	.start = mtk_gamma_start,
	.stop = mtk_gamma_stop,
};

static const struct mtk_ddp_comp_funcs ddp_merge = {
	.clk_enable = mtk_merge_clk_enable,
	.clk_disable = mtk_merge_clk_disable,
	.start = mtk_merge_start,
	.stop = mtk_merge_stop,
	.config = mtk_merge_config,
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
	.register_vblank_cb = mtk_ovl_register_vblank_cb,
	.unregister_vblank_cb = mtk_ovl_unregister_vblank_cb,
	.enable_vblank = mtk_ovl_enable_vblank,
	.disable_vblank = mtk_ovl_disable_vblank,
	.supported_rotations = mtk_ovl_supported_rotations,
	.layer_nr = mtk_ovl_layer_nr,
	.layer_check = mtk_ovl_layer_check,
	.layer_config = mtk_ovl_layer_config,
	.bgclr_in_on = mtk_ovl_bgclr_in_on,
	.bgclr_in_off = mtk_ovl_bgclr_in_off,
	.get_formats = mtk_ovl_get_formats,
	.get_num_formats = mtk_ovl_get_num_formats,
};

static const struct mtk_ddp_comp_funcs ddp_postmask = {
	.clk_enable = mtk_ddp_clk_enable,
	.clk_disable = mtk_ddp_clk_disable,
	.config = mtk_postmask_config,
	.start = mtk_postmask_start,
	.stop = mtk_postmask_stop,
};

static const struct mtk_ddp_comp_funcs ddp_rdma = {
	.clk_enable = mtk_rdma_clk_enable,
	.clk_disable = mtk_rdma_clk_disable,
	.config = mtk_rdma_config,
	.start = mtk_rdma_start,
	.stop = mtk_rdma_stop,
	.register_vblank_cb = mtk_rdma_register_vblank_cb,
	.unregister_vblank_cb = mtk_rdma_unregister_vblank_cb,
	.enable_vblank = mtk_rdma_enable_vblank,
	.disable_vblank = mtk_rdma_disable_vblank,
	.layer_nr = mtk_rdma_layer_nr,
	.layer_config = mtk_rdma_layer_config,
	.get_formats = mtk_rdma_get_formats,
	.get_num_formats = mtk_rdma_get_num_formats,
};

static const struct mtk_ddp_comp_funcs ddp_ufoe = {
	.clk_enable = mtk_ddp_clk_enable,
	.clk_disable = mtk_ddp_clk_disable,
	.start = mtk_ufoe_start,
};

static const struct mtk_ddp_comp_funcs ddp_ovl_adaptor = {
	.power_on = mtk_ovl_adaptor_power_on,
	.power_off = mtk_ovl_adaptor_power_off,
	.clk_enable = mtk_ovl_adaptor_clk_enable,
	.clk_disable = mtk_ovl_adaptor_clk_disable,
	.config = mtk_ovl_adaptor_config,
	.start = mtk_ovl_adaptor_start,
	.stop = mtk_ovl_adaptor_stop,
	.layer_nr = mtk_ovl_adaptor_layer_nr,
	.layer_config = mtk_ovl_adaptor_layer_config,
	.register_vblank_cb = mtk_ovl_adaptor_register_vblank_cb,
	.unregister_vblank_cb = mtk_ovl_adaptor_unregister_vblank_cb,
	.enable_vblank = mtk_ovl_adaptor_enable_vblank,
	.disable_vblank = mtk_ovl_adaptor_disable_vblank,
	.dma_dev_get = mtk_ovl_adaptor_dma_dev_get,
	.connect = mtk_ovl_adaptor_connect,
	.disconnect = mtk_ovl_adaptor_disconnect,
	.add = mtk_ovl_adaptor_add_comp,
	.remove = mtk_ovl_adaptor_remove_comp,
	.get_formats = mtk_ovl_adaptor_get_formats,
	.get_num_formats = mtk_ovl_adaptor_get_num_formats,
	.mode_valid = mtk_ovl_adaptor_mode_valid,
};

static const char * const mtk_ddp_comp_stem[MTK_DDP_COMP_TYPE_MAX] = {
	[MTK_DISP_AAL] = "aal",
	[MTK_DISP_BLS] = "bls",
	[MTK_DISP_CCORR] = "ccorr",
	[MTK_DISP_COLOR] = "color",
	[MTK_DISP_DITHER] = "dither",
	[MTK_DISP_DSC] = "dsc",
	[MTK_DISP_GAMMA] = "gamma",
	[MTK_DISP_MERGE] = "merge",
	[MTK_DISP_MUTEX] = "mutex",
	[MTK_DISP_OD] = "od",
	[MTK_DISP_OVL] = "ovl",
	[MTK_DISP_OVL_2L] = "ovl-2l",
	[MTK_DISP_OVL_ADAPTOR] = "ovl_adaptor",
	[MTK_DISP_POSTMASK] = "postmask",
	[MTK_DISP_PWM] = "pwm",
	[MTK_DISP_RDMA] = "rdma",
	[MTK_DISP_UFOE] = "ufoe",
	[MTK_DISP_WDMA] = "wdma",
	[MTK_DP_INTF] = "dp-intf",
	[MTK_DPI] = "dpi",
	[MTK_DSI] = "dsi",
};

struct mtk_ddp_comp_match {
	enum mtk_ddp_comp_type type;
	int alias_id;
	const struct mtk_ddp_comp_funcs *funcs;
};

static const struct mtk_ddp_comp_match mtk_ddp_matches[DDP_COMPONENT_DRM_ID_MAX] = {
	[DDP_COMPONENT_AAL0]		= { MTK_DISP_AAL,		0, &ddp_aal },
	[DDP_COMPONENT_AAL1]		= { MTK_DISP_AAL,		1, &ddp_aal },
	[DDP_COMPONENT_BLS]		= { MTK_DISP_BLS,		0, NULL },
	[DDP_COMPONENT_CCORR]		= { MTK_DISP_CCORR,		0, &ddp_ccorr },
	[DDP_COMPONENT_COLOR0]		= { MTK_DISP_COLOR,		0, &ddp_color },
	[DDP_COMPONENT_COLOR1]		= { MTK_DISP_COLOR,		1, &ddp_color },
	[DDP_COMPONENT_DITHER0]		= { MTK_DISP_DITHER,		0, &ddp_dither },
	[DDP_COMPONENT_DP_INTF0]	= { MTK_DP_INTF,		0, &ddp_dpi },
	[DDP_COMPONENT_DP_INTF1]	= { MTK_DP_INTF,		1, &ddp_dpi },
	[DDP_COMPONENT_DPI0]		= { MTK_DPI,			0, &ddp_dpi },
	[DDP_COMPONENT_DPI1]		= { MTK_DPI,			1, &ddp_dpi },
	[DDP_COMPONENT_DRM_OVL_ADAPTOR]	= { MTK_DISP_OVL_ADAPTOR,	0, &ddp_ovl_adaptor },
	[DDP_COMPONENT_DSC0]		= { MTK_DISP_DSC,		0, &ddp_dsc },
	[DDP_COMPONENT_DSC1]		= { MTK_DISP_DSC,		1, &ddp_dsc },
	[DDP_COMPONENT_DSI0]		= { MTK_DSI,			0, &ddp_dsi },
	[DDP_COMPONENT_DSI1]		= { MTK_DSI,			1, &ddp_dsi },
	[DDP_COMPONENT_DSI2]		= { MTK_DSI,			2, &ddp_dsi },
	[DDP_COMPONENT_DSI3]		= { MTK_DSI,			3, &ddp_dsi },
	[DDP_COMPONENT_GAMMA]		= { MTK_DISP_GAMMA,		0, &ddp_gamma },
	[DDP_COMPONENT_MERGE0]		= { MTK_DISP_MERGE,		0, &ddp_merge },
	[DDP_COMPONENT_MERGE1]		= { MTK_DISP_MERGE,		1, &ddp_merge },
	[DDP_COMPONENT_MERGE2]		= { MTK_DISP_MERGE,		2, &ddp_merge },
	[DDP_COMPONENT_MERGE3]		= { MTK_DISP_MERGE,		3, &ddp_merge },
	[DDP_COMPONENT_MERGE4]		= { MTK_DISP_MERGE,		4, &ddp_merge },
	[DDP_COMPONENT_MERGE5]		= { MTK_DISP_MERGE,		5, &ddp_merge },
	[DDP_COMPONENT_OD0]		= { MTK_DISP_OD,		0, &ddp_od },
	[DDP_COMPONENT_OD1]		= { MTK_DISP_OD,		1, &ddp_od },
	[DDP_COMPONENT_OVL0]		= { MTK_DISP_OVL,		0, &ddp_ovl },
	[DDP_COMPONENT_OVL1]		= { MTK_DISP_OVL,		1, &ddp_ovl },
	[DDP_COMPONENT_OVL_2L0]		= { MTK_DISP_OVL_2L,		0, &ddp_ovl },
	[DDP_COMPONENT_OVL_2L1]		= { MTK_DISP_OVL_2L,		1, &ddp_ovl },
	[DDP_COMPONENT_OVL_2L2]		= { MTK_DISP_OVL_2L,		2, &ddp_ovl },
	[DDP_COMPONENT_POSTMASK0]	= { MTK_DISP_POSTMASK,		0, &ddp_postmask },
	[DDP_COMPONENT_PWM0]		= { MTK_DISP_PWM,		0, NULL },
	[DDP_COMPONENT_PWM1]		= { MTK_DISP_PWM,		1, NULL },
	[DDP_COMPONENT_PWM2]		= { MTK_DISP_PWM,		2, NULL },
	[DDP_COMPONENT_RDMA0]		= { MTK_DISP_RDMA,		0, &ddp_rdma },
	[DDP_COMPONENT_RDMA1]		= { MTK_DISP_RDMA,		1, &ddp_rdma },
	[DDP_COMPONENT_RDMA2]		= { MTK_DISP_RDMA,		2, &ddp_rdma },
	[DDP_COMPONENT_RDMA4]		= { MTK_DISP_RDMA,		4, &ddp_rdma },
	[DDP_COMPONENT_UFOE]		= { MTK_DISP_UFOE,		0, &ddp_ufoe },
	[DDP_COMPONENT_WDMA0]		= { MTK_DISP_WDMA,		0, NULL },
	[DDP_COMPONENT_WDMA1]		= { MTK_DISP_WDMA,		1, NULL },
};

static bool mtk_ddp_comp_find(struct device *dev,
			      const unsigned int *path,
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

static int mtk_ddp_comp_find_in_route(struct device *dev,
				      const struct mtk_drm_route *routes,
				      unsigned int num_routes,
				      struct mtk_ddp_comp *ddp_comp)
{
	unsigned int i;

	if (!routes)
		return -EINVAL;

	for (i = 0; i < num_routes; i++)
		if (dev == ddp_comp[routes[i].route_ddp].dev)
			return BIT(routes[i].crtc_id);

	return -ENODEV;
}

static bool mtk_ddp_path_available(const unsigned int *path,
				   unsigned int path_len,
				   struct device_node **comp_node)
{
	unsigned int i;

	if (!path || !path_len)
		return false;

	for (i = 0U; i < path_len; i++) {
		/* OVL_ADAPTOR doesn't have a device node */
		if (path[i] == DDP_COMPONENT_DRM_OVL_ADAPTOR)
			continue;

		if (!comp_node[path[i]])
			return false;
	}

	return true;
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

int mtk_find_possible_crtcs(struct drm_device *drm, struct device *dev)
{
	struct mtk_drm_private *private = drm->dev_private;
	const struct mtk_mmsys_driver_data *data;
	struct mtk_drm_private *priv_n;
	int i = 0, j;
	int ret;

	for (j = 0; j < private->data->mmsys_dev_num; j++) {
		priv_n = private->all_drm_private[j];
		data = priv_n->data;

		if (mtk_ddp_path_available(data->main_path, data->main_len,
					   priv_n->comp_node)) {
			if (mtk_ddp_comp_find(dev, data->main_path,
					      data->main_len,
					      priv_n->ddp_comp))
				return BIT(i);
			i++;
		}

		if (mtk_ddp_path_available(data->ext_path, data->ext_len,
					   priv_n->comp_node)) {
			if (mtk_ddp_comp_find(dev, data->ext_path,
					      data->ext_len,
					      priv_n->ddp_comp))
				return BIT(i);
			i++;
		}

		if (mtk_ddp_path_available(data->third_path, data->third_len,
					   priv_n->comp_node)) {
			if (mtk_ddp_comp_find(dev, data->third_path,
					      data->third_len,
					      priv_n->ddp_comp))
				return BIT(i);
			i++;
		}
	}

	ret = mtk_ddp_comp_find_in_route(dev,
					 private->data->conn_routes,
					 private->data->num_conn_routes,
					 private->ddp_comp);

	if (ret < 0)
		DRM_INFO("Failed to find comp in ddp table, ret = %d\n", ret);

	return ret;
}

int mtk_ddp_comp_init(struct device_node *node, struct mtk_ddp_comp *comp,
		      unsigned int comp_id)
{
	struct platform_device *comp_pdev;
	enum mtk_ddp_comp_type type;
	struct mtk_ddp_comp_dev *priv;
#if IS_REACHABLE(CONFIG_MTK_CMDQ)
	int ret;
#endif

	if (comp_id >= DDP_COMPONENT_DRM_ID_MAX)
		return -EINVAL;

	type = mtk_ddp_matches[comp_id].type;

	comp->id = comp_id;
	comp->funcs = mtk_ddp_matches[comp_id].funcs;
	/* Not all drm components have a DTS device node, such as ovl_adaptor,
	 * which is the drm bring up sub driver
	 */
	if (!node)
		return 0;

	comp_pdev = of_find_device_by_node(node);
	if (!comp_pdev) {
		DRM_INFO("Waiting for device %s\n", node->full_name);
		return -EPROBE_DEFER;
	}
	comp->dev = &comp_pdev->dev;

	if (type == MTK_DISP_AAL ||
	    type == MTK_DISP_BLS ||
	    type == MTK_DISP_CCORR ||
	    type == MTK_DISP_COLOR ||
	    type == MTK_DISP_GAMMA ||
	    type == MTK_DISP_MERGE ||
	    type == MTK_DISP_OVL ||
	    type == MTK_DISP_OVL_2L ||
	    type == MTK_DISP_PWM ||
	    type == MTK_DISP_RDMA ||
	    type == MTK_DPI ||
	    type == MTK_DP_INTF ||
	    type == MTK_DSI)
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
