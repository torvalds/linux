// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022 MediaTek Inc.
 * Author: Ping-Hsun Wu <ping-hsun.wu@mediatek.com>
 */

#include <linux/clk.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/pm_runtime.h>
#include "mtk-mdp3-comp.h"
#include "mtk-mdp3-core.h"
#include "mtk-mdp3-regs.h"

#include "mdp_reg_rdma.h"
#include "mdp_reg_ccorr.h"
#include "mdp_reg_rsz.h"
#include "mdp_reg_wrot.h"
#include "mdp_reg_wdma.h"

static u32 mdp_comp_alias_id[MDP_COMP_TYPE_COUNT];

static inline const struct mdp_platform_config *
__get_plat_cfg(const struct mdp_comp_ctx *ctx)
{
	if (!ctx)
		return NULL;

	return ctx->comp->mdp_dev->mdp_data->mdp_cfg;
}

static s64 get_comp_flag(const struct mdp_comp_ctx *ctx)
{
	const struct mdp_platform_config *mdp_cfg = __get_plat_cfg(ctx);

	if (mdp_cfg && mdp_cfg->rdma_rsz1_sram_sharing)
		if (ctx->comp->id == MDP_COMP_RDMA0)
			return BIT(MDP_COMP_RDMA0) | BIT(MDP_COMP_RSZ1);

	return BIT(ctx->comp->id);
}

static int init_rdma(struct mdp_comp_ctx *ctx, struct mdp_cmdq_cmd *cmd)
{
	const struct mdp_platform_config *mdp_cfg = __get_plat_cfg(ctx);
	phys_addr_t base = ctx->comp->reg_base;
	u8 subsys_id = ctx->comp->subsys_id;

	if (mdp_cfg && mdp_cfg->rdma_support_10bit) {
		struct mdp_comp *prz1 = ctx->comp->mdp_dev->comp[MDP_COMP_RSZ1];

		/* Disable RSZ1 */
		if (ctx->comp->id == MDP_COMP_RDMA0 && prz1)
			MM_REG_WRITE(cmd, subsys_id, prz1->reg_base, PRZ_ENABLE,
				     0x0, BIT(0));
	}

	/* Reset RDMA */
	MM_REG_WRITE(cmd, subsys_id, base, MDP_RDMA_RESET, BIT(0), BIT(0));
	MM_REG_POLL(cmd, subsys_id, base, MDP_RDMA_MON_STA_1, BIT(8), BIT(8));
	MM_REG_WRITE(cmd, subsys_id, base, MDP_RDMA_RESET, 0x0, BIT(0));
	return 0;
}

static int config_rdma_frame(struct mdp_comp_ctx *ctx,
			     struct mdp_cmdq_cmd *cmd,
			     const struct v4l2_rect *compose)
{
	const struct mdp_rdma_data *rdma = &ctx->param->rdma;
	const struct mdp_platform_config *mdp_cfg = __get_plat_cfg(ctx);
	u32 colorformat = ctx->input->buffer.format.colorformat;
	bool block10bit = MDP_COLOR_IS_10BIT_PACKED(colorformat);
	bool en_ufo = MDP_COLOR_IS_UFP(colorformat);
	phys_addr_t base = ctx->comp->reg_base;
	u8 subsys_id = ctx->comp->subsys_id;

	if (mdp_cfg && mdp_cfg->rdma_support_10bit) {
		if (block10bit)
			MM_REG_WRITE(cmd, subsys_id, base,
				     MDP_RDMA_RESV_DUMMY_0, 0x7, 0x7);
		else
			MM_REG_WRITE(cmd, subsys_id, base,
				     MDP_RDMA_RESV_DUMMY_0, 0x0, 0x7);
	}

	/* Setup smi control */
	MM_REG_WRITE(cmd, subsys_id, base, MDP_RDMA_GMCIF_CON,
		     (7 <<  4) + //burst type to 8
		     (1 << 16),  //enable pre-ultra
		     0x00030071);

	/* Setup source frame info */
	MM_REG_WRITE(cmd, subsys_id, base, MDP_RDMA_SRC_CON, rdma->src_ctrl,
		     0x03C8FE0F);

	if (mdp_cfg)
		if (mdp_cfg->rdma_support_10bit && en_ufo) {
			/* Setup source buffer base */
			MM_REG_WRITE(cmd, subsys_id,
				     base, MDP_RDMA_UFO_DEC_LENGTH_BASE_Y,
				     rdma->ufo_dec_y, 0xFFFFFFFF);
			MM_REG_WRITE(cmd, subsys_id,
				     base, MDP_RDMA_UFO_DEC_LENGTH_BASE_C,
				     rdma->ufo_dec_c, 0xFFFFFFFF);
			/* Set 10bit source frame pitch */
			if (block10bit)
				MM_REG_WRITE(cmd, subsys_id,
					     base, MDP_RDMA_MF_BKGD_SIZE_IN_PXL,
					     rdma->mf_bkgd_in_pxl, 0x001FFFFF);
		}

	MM_REG_WRITE(cmd, subsys_id, base, MDP_RDMA_CON, rdma->control,
		     0x1110);
	/* Setup source buffer base */
	MM_REG_WRITE(cmd, subsys_id, base, MDP_RDMA_SRC_BASE_0, rdma->iova[0],
		     0xFFFFFFFF);
	MM_REG_WRITE(cmd, subsys_id, base, MDP_RDMA_SRC_BASE_1, rdma->iova[1],
		     0xFFFFFFFF);
	MM_REG_WRITE(cmd, subsys_id, base, MDP_RDMA_SRC_BASE_2, rdma->iova[2],
		     0xFFFFFFFF);
	/* Setup source buffer end */
	MM_REG_WRITE(cmd, subsys_id, base, MDP_RDMA_SRC_END_0,
		     rdma->iova_end[0], 0xFFFFFFFF);
	MM_REG_WRITE(cmd, subsys_id, base, MDP_RDMA_SRC_END_1,
		     rdma->iova_end[1], 0xFFFFFFFF);
	MM_REG_WRITE(cmd, subsys_id, base, MDP_RDMA_SRC_END_2,
		     rdma->iova_end[2], 0xFFFFFFFF);
	/* Setup source frame pitch */
	MM_REG_WRITE(cmd, subsys_id, base, MDP_RDMA_MF_BKGD_SIZE_IN_BYTE,
		     rdma->mf_bkgd, 0x001FFFFF);
	MM_REG_WRITE(cmd, subsys_id, base, MDP_RDMA_SF_BKGD_SIZE_IN_BYTE,
		     rdma->sf_bkgd, 0x001FFFFF);
	/* Setup color transform */
	MM_REG_WRITE(cmd, subsys_id, base, MDP_RDMA_TRANSFORM_0,
		     rdma->transform, 0x0F110000);

	return 0;
}

static int config_rdma_subfrm(struct mdp_comp_ctx *ctx,
			      struct mdp_cmdq_cmd *cmd, u32 index)
{
	const struct mdp_rdma_subfrm *subfrm = &ctx->param->rdma.subfrms[index];
	const struct img_comp_subfrm *csf = &ctx->param->subfrms[index];
	const struct mdp_platform_config *mdp_cfg = __get_plat_cfg(ctx);
	u32 colorformat = ctx->input->buffer.format.colorformat;
	bool block10bit = MDP_COLOR_IS_10BIT_PACKED(colorformat);
	bool en_ufo = MDP_COLOR_IS_UFP(colorformat);
	phys_addr_t base = ctx->comp->reg_base;
	u8 subsys_id = ctx->comp->subsys_id;

	/* Enable RDMA */
	MM_REG_WRITE(cmd, subsys_id, base, MDP_RDMA_EN, BIT(0), BIT(0));

	/* Set Y pixel offset */
	MM_REG_WRITE(cmd, subsys_id, base, MDP_RDMA_SRC_OFFSET_0,
		     subfrm->offset[0], 0xFFFFFFFF);

	/* Set 10bit UFO mode */
	if (mdp_cfg)
		if (mdp_cfg->rdma_support_10bit && block10bit && en_ufo)
			MM_REG_WRITE(cmd, subsys_id, base,
				     MDP_RDMA_SRC_OFFSET_0_P,
				     subfrm->offset_0_p, 0xFFFFFFFF);

	/* Set U pixel offset */
	MM_REG_WRITE(cmd, subsys_id, base, MDP_RDMA_SRC_OFFSET_1,
		     subfrm->offset[1], 0xFFFFFFFF);
	/* Set V pixel offset */
	MM_REG_WRITE(cmd, subsys_id, base, MDP_RDMA_SRC_OFFSET_2,
		     subfrm->offset[2], 0xFFFFFFFF);
	/* Set source size */
	MM_REG_WRITE(cmd, subsys_id, base, MDP_RDMA_MF_SRC_SIZE, subfrm->src,
		     0x1FFF1FFF);
	/* Set target size */
	MM_REG_WRITE(cmd, subsys_id, base, MDP_RDMA_MF_CLIP_SIZE,
		     subfrm->clip, 0x1FFF1FFF);
	/* Set crop offset */
	MM_REG_WRITE(cmd, subsys_id, base, MDP_RDMA_MF_OFFSET_1,
		     subfrm->clip_ofst, 0x003F001F);

	if (mdp_cfg && mdp_cfg->rdma_upsample_repeat_only)
		if ((csf->in.right - csf->in.left + 1) > 320)
			MM_REG_WRITE(cmd, subsys_id, base,
				     MDP_RDMA_RESV_DUMMY_0, BIT(2), BIT(2));

	return 0;
}

static int wait_rdma_event(struct mdp_comp_ctx *ctx, struct mdp_cmdq_cmd *cmd)
{
	struct device *dev = &ctx->comp->mdp_dev->pdev->dev;
	phys_addr_t base = ctx->comp->reg_base;
	u8 subsys_id = ctx->comp->subsys_id;

	if (ctx->comp->alias_id == 0)
		MM_REG_WAIT(cmd, ctx->comp->gce_event[MDP_GCE_EVENT_EOF]);
	else
		dev_err(dev, "Do not support RDMA1_DONE event\n");

	/* Disable RDMA */
	MM_REG_WRITE(cmd, subsys_id, base, MDP_RDMA_EN, 0x0, BIT(0));
	return 0;
}

static const struct mdp_comp_ops rdma_ops = {
	.get_comp_flag = get_comp_flag,
	.init_comp = init_rdma,
	.config_frame = config_rdma_frame,
	.config_subfrm = config_rdma_subfrm,
	.wait_comp_event = wait_rdma_event,
};

static int init_rsz(struct mdp_comp_ctx *ctx, struct mdp_cmdq_cmd *cmd)
{
	phys_addr_t base = ctx->comp->reg_base;
	u8 subsys_id = ctx->comp->subsys_id;

	/* Reset RSZ */
	MM_REG_WRITE(cmd, subsys_id, base, PRZ_ENABLE, 0x10000, BIT(16));
	MM_REG_WRITE(cmd, subsys_id, base, PRZ_ENABLE, 0x0, BIT(16));
	/* Enable RSZ */
	MM_REG_WRITE(cmd, subsys_id, base, PRZ_ENABLE, BIT(0), BIT(0));
	return 0;
}

static int config_rsz_frame(struct mdp_comp_ctx *ctx,
			    struct mdp_cmdq_cmd *cmd,
			    const struct v4l2_rect *compose)
{
	const struct mdp_rsz_data *rsz = &ctx->param->rsz;
	phys_addr_t base = ctx->comp->reg_base;
	u8 subsys_id = ctx->comp->subsys_id;

	if (ctx->param->frame.bypass) {
		/* Disable RSZ */
		MM_REG_WRITE(cmd, subsys_id, base, PRZ_ENABLE, 0x0, BIT(0));
		return 0;
	}

	MM_REG_WRITE(cmd, subsys_id, base, PRZ_CONTROL_1, rsz->control1,
		     0x03FFFDF3);
	MM_REG_WRITE(cmd, subsys_id, base, PRZ_CONTROL_2, rsz->control2,
		     0x0FFFC290);
	MM_REG_WRITE(cmd, subsys_id, base, PRZ_HORIZONTAL_COEFF_STEP,
		     rsz->coeff_step_x, 0x007FFFFF);
	MM_REG_WRITE(cmd, subsys_id, base, PRZ_VERTICAL_COEFF_STEP,
		     rsz->coeff_step_y, 0x007FFFFF);
	return 0;
}

static int config_rsz_subfrm(struct mdp_comp_ctx *ctx,
			     struct mdp_cmdq_cmd *cmd, u32 index)
{
	const struct mdp_rsz_subfrm *subfrm = &ctx->param->rsz.subfrms[index];
	const struct img_comp_subfrm *csf = &ctx->param->subfrms[index];
	const struct mdp_platform_config *mdp_cfg = __get_plat_cfg(ctx);
	phys_addr_t base = ctx->comp->reg_base;
	u8 subsys_id = ctx->comp->subsys_id;

	MM_REG_WRITE(cmd, subsys_id, base, PRZ_CONTROL_2, subfrm->control2,
		     0x00003800);
	MM_REG_WRITE(cmd, subsys_id, base, PRZ_INPUT_IMAGE, subfrm->src,
		     0xFFFFFFFF);

	if (mdp_cfg && mdp_cfg->rsz_disable_dcm_small_sample)
		if ((csf->in.right - csf->in.left + 1) <= 16)
			MM_REG_WRITE(cmd, subsys_id, base, PRZ_CONTROL_1,
				     BIT(27), BIT(27));

	MM_REG_WRITE(cmd, subsys_id, base, PRZ_LUMA_HORIZONTAL_INTEGER_OFFSET,
		     csf->luma.left, 0xFFFF);
	MM_REG_WRITE(cmd, subsys_id,
		     base, PRZ_LUMA_HORIZONTAL_SUBPIXEL_OFFSET,
		     csf->luma.left_subpix, 0x1FFFFF);
	MM_REG_WRITE(cmd, subsys_id, base, PRZ_LUMA_VERTICAL_INTEGER_OFFSET,
		     csf->luma.top, 0xFFFF);
	MM_REG_WRITE(cmd, subsys_id, base, PRZ_LUMA_VERTICAL_SUBPIXEL_OFFSET,
		     csf->luma.top_subpix, 0x1FFFFF);
	MM_REG_WRITE(cmd, subsys_id,
		     base, PRZ_CHROMA_HORIZONTAL_INTEGER_OFFSET,
		     csf->chroma.left, 0xFFFF);
	MM_REG_WRITE(cmd, subsys_id,
		     base, PRZ_CHROMA_HORIZONTAL_SUBPIXEL_OFFSET,
		     csf->chroma.left_subpix, 0x1FFFFF);

	MM_REG_WRITE(cmd, subsys_id, base, PRZ_OUTPUT_IMAGE, subfrm->clip,
		     0xFFFFFFFF);

	return 0;
}

static int advance_rsz_subfrm(struct mdp_comp_ctx *ctx,
			      struct mdp_cmdq_cmd *cmd, u32 index)
{
	const struct mdp_platform_config *mdp_cfg = __get_plat_cfg(ctx);

	if (mdp_cfg && mdp_cfg->rsz_disable_dcm_small_sample) {
		const struct img_comp_subfrm *csf = &ctx->param->subfrms[index];
		phys_addr_t base = ctx->comp->reg_base;
		u8 subsys_id = ctx->comp->subsys_id;

		if ((csf->in.right - csf->in.left + 1) <= 16)
			MM_REG_WRITE(cmd, subsys_id, base, PRZ_CONTROL_1, 0x0,
				     BIT(27));
	}

	return 0;
}

static const struct mdp_comp_ops rsz_ops = {
	.get_comp_flag = get_comp_flag,
	.init_comp = init_rsz,
	.config_frame = config_rsz_frame,
	.config_subfrm = config_rsz_subfrm,
	.advance_subfrm = advance_rsz_subfrm,
};

static int init_wrot(struct mdp_comp_ctx *ctx, struct mdp_cmdq_cmd *cmd)
{
	phys_addr_t base = ctx->comp->reg_base;
	u8 subsys_id = ctx->comp->subsys_id;

	/* Reset WROT */
	MM_REG_WRITE(cmd, subsys_id, base, VIDO_SOFT_RST, BIT(0), BIT(0));
	MM_REG_POLL(cmd, subsys_id, base, VIDO_SOFT_RST_STAT, BIT(0), BIT(0));
	MM_REG_WRITE(cmd, subsys_id, base, VIDO_SOFT_RST, 0x0, BIT(0));
	MM_REG_POLL(cmd, subsys_id, base, VIDO_SOFT_RST_STAT, 0x0, BIT(0));
	return 0;
}

static int config_wrot_frame(struct mdp_comp_ctx *ctx,
			     struct mdp_cmdq_cmd *cmd,
			     const struct v4l2_rect *compose)
{
	const struct mdp_wrot_data *wrot = &ctx->param->wrot;
	const struct mdp_platform_config *mdp_cfg = __get_plat_cfg(ctx);
	phys_addr_t base = ctx->comp->reg_base;
	u8 subsys_id = ctx->comp->subsys_id;

	/* Write frame base address */
	MM_REG_WRITE(cmd, subsys_id, base, VIDO_BASE_ADDR, wrot->iova[0],
		     0xFFFFFFFF);
	MM_REG_WRITE(cmd, subsys_id, base, VIDO_BASE_ADDR_C, wrot->iova[1],
		     0xFFFFFFFF);
	MM_REG_WRITE(cmd, subsys_id, base, VIDO_BASE_ADDR_V, wrot->iova[2],
		     0xFFFFFFFF);
	/* Write frame related registers */
	MM_REG_WRITE(cmd, subsys_id, base, VIDO_CTRL, wrot->control,
		     0xF131510F);
	/* Write frame Y pitch */
	MM_REG_WRITE(cmd, subsys_id, base, VIDO_STRIDE, wrot->stride[0],
		     0x0000FFFF);
	/* Write frame UV pitch */
	MM_REG_WRITE(cmd, subsys_id, base, VIDO_STRIDE_C, wrot->stride[1],
		     0xFFFF);
	MM_REG_WRITE(cmd, subsys_id, base, VIDO_STRIDE_V, wrot->stride[2],
		     0xFFFF);
	/* Write matrix control */
	MM_REG_WRITE(cmd, subsys_id, base, VIDO_MAT_CTRL, wrot->mat_ctrl, 0xF3);

	/* Set the fixed ALPHA as 0xFF */
	MM_REG_WRITE(cmd, subsys_id, base, VIDO_DITHER, 0xFF000000,
		     0xFF000000);
	/* Set VIDO_EOL_SEL */
	MM_REG_WRITE(cmd, subsys_id, base, VIDO_RSV_1, BIT(31), BIT(31));
	/* Set VIDO_FIFO_TEST */
	if (wrot->fifo_test != 0)
		MM_REG_WRITE(cmd, subsys_id, base, VIDO_FIFO_TEST,
			     wrot->fifo_test, 0xFFF);
	/* Filter enable */
	if (mdp_cfg && mdp_cfg->wrot_filter_constraint)
		MM_REG_WRITE(cmd, subsys_id, base, VIDO_MAIN_BUF_SIZE,
			     wrot->filter, 0x77);

	return 0;
}

static int config_wrot_subfrm(struct mdp_comp_ctx *ctx,
			      struct mdp_cmdq_cmd *cmd, u32 index)
{
	const struct mdp_wrot_subfrm *subfrm = &ctx->param->wrot.subfrms[index];
	phys_addr_t base = ctx->comp->reg_base;
	u8 subsys_id = ctx->comp->subsys_id;

	/* Write Y pixel offset */
	MM_REG_WRITE(cmd, subsys_id, base, VIDO_OFST_ADDR,
		     subfrm->offset[0], 0x0FFFFFFF);
	/* Write U pixel offset */
	MM_REG_WRITE(cmd, subsys_id, base, VIDO_OFST_ADDR_C,
		     subfrm->offset[1], 0x0FFFFFFF);
	/* Write V pixel offset */
	MM_REG_WRITE(cmd, subsys_id, base, VIDO_OFST_ADDR_V,
		     subfrm->offset[2], 0x0FFFFFFF);
	/* Write source size */
	MM_REG_WRITE(cmd, subsys_id, base, VIDO_IN_SIZE, subfrm->src,
		     0x1FFF1FFF);
	/* Write target size */
	MM_REG_WRITE(cmd, subsys_id, base, VIDO_TAR_SIZE, subfrm->clip,
		     0x1FFF1FFF);
	MM_REG_WRITE(cmd, subsys_id, base, VIDO_CROP_OFST, subfrm->clip_ofst,
		     0x1FFF1FFF);

	MM_REG_WRITE(cmd, subsys_id, base, VIDO_MAIN_BUF_SIZE,
		     subfrm->main_buf, 0x1FFF7F00);

	/* Enable WROT */
	MM_REG_WRITE(cmd, subsys_id, base, VIDO_ROT_EN, BIT(0), BIT(0));

	return 0;
}

static int wait_wrot_event(struct mdp_comp_ctx *ctx, struct mdp_cmdq_cmd *cmd)
{
	const struct mdp_platform_config *mdp_cfg = __get_plat_cfg(ctx);
	struct device *dev = &ctx->comp->mdp_dev->pdev->dev;
	phys_addr_t base = ctx->comp->reg_base;
	u8 subsys_id = ctx->comp->subsys_id;

	if (ctx->comp->alias_id == 0)
		MM_REG_WAIT(cmd, ctx->comp->gce_event[MDP_GCE_EVENT_EOF]);
	else
		dev_err(dev, "Do not support WROT1_DONE event\n");

	if (mdp_cfg && mdp_cfg->wrot_filter_constraint)
		MM_REG_WRITE(cmd, subsys_id, base, VIDO_MAIN_BUF_SIZE, 0x0,
			     0x77);

	/* Disable WROT */
	MM_REG_WRITE(cmd, subsys_id, base, VIDO_ROT_EN, 0x0, BIT(0));

	return 0;
}

static const struct mdp_comp_ops wrot_ops = {
	.get_comp_flag = get_comp_flag,
	.init_comp = init_wrot,
	.config_frame = config_wrot_frame,
	.config_subfrm = config_wrot_subfrm,
	.wait_comp_event = wait_wrot_event,
};

static int init_wdma(struct mdp_comp_ctx *ctx, struct mdp_cmdq_cmd *cmd)
{
	phys_addr_t base = ctx->comp->reg_base;
	u8 subsys_id = ctx->comp->subsys_id;

	/* Reset WDMA */
	MM_REG_WRITE(cmd, subsys_id, base, WDMA_RST, BIT(0), BIT(0));
	MM_REG_POLL(cmd, subsys_id, base, WDMA_FLOW_CTRL_DBG, BIT(0), BIT(0));
	MM_REG_WRITE(cmd, subsys_id, base, WDMA_RST, 0x0, BIT(0));
	return 0;
}

static int config_wdma_frame(struct mdp_comp_ctx *ctx,
			     struct mdp_cmdq_cmd *cmd,
			     const struct v4l2_rect *compose)
{
	const struct mdp_wdma_data *wdma = &ctx->param->wdma;
	phys_addr_t base = ctx->comp->reg_base;
	u8 subsys_id = ctx->comp->subsys_id;

	MM_REG_WRITE(cmd, subsys_id, base, WDMA_BUF_CON2, 0x10101050,
		     0xFFFFFFFF);

	/* Setup frame information */
	MM_REG_WRITE(cmd, subsys_id, base, WDMA_CFG, wdma->wdma_cfg,
		     0x0F01B8F0);
	/* Setup frame base address */
	MM_REG_WRITE(cmd, subsys_id, base, WDMA_DST_ADDR,   wdma->iova[0],
		     0xFFFFFFFF);
	MM_REG_WRITE(cmd, subsys_id, base, WDMA_DST_U_ADDR, wdma->iova[1],
		     0xFFFFFFFF);
	MM_REG_WRITE(cmd, subsys_id, base, WDMA_DST_V_ADDR, wdma->iova[2],
		     0xFFFFFFFF);
	/* Setup Y pitch */
	MM_REG_WRITE(cmd, subsys_id, base, WDMA_DST_W_IN_BYTE,
		     wdma->w_in_byte, 0x0000FFFF);
	/* Setup UV pitch */
	MM_REG_WRITE(cmd, subsys_id, base, WDMA_DST_UV_PITCH,
		     wdma->uv_stride, 0x0000FFFF);
	/* Set the fixed ALPHA as 0xFF */
	MM_REG_WRITE(cmd, subsys_id, base, WDMA_ALPHA, 0x800000FF,
		     0x800000FF);

	return 0;
}

static int config_wdma_subfrm(struct mdp_comp_ctx *ctx,
			      struct mdp_cmdq_cmd *cmd, u32 index)
{
	const struct mdp_wdma_subfrm *subfrm = &ctx->param->wdma.subfrms[index];
	phys_addr_t base = ctx->comp->reg_base;
	u8 subsys_id = ctx->comp->subsys_id;

	/* Write Y pixel offset */
	MM_REG_WRITE(cmd, subsys_id, base, WDMA_DST_ADDR_OFFSET,
		     subfrm->offset[0], 0x0FFFFFFF);
	/* Write U pixel offset */
	MM_REG_WRITE(cmd, subsys_id, base, WDMA_DST_U_ADDR_OFFSET,
		     subfrm->offset[1], 0x0FFFFFFF);
	/* Write V pixel offset */
	MM_REG_WRITE(cmd, subsys_id, base, WDMA_DST_V_ADDR_OFFSET,
		     subfrm->offset[2], 0x0FFFFFFF);
	/* Write source size */
	MM_REG_WRITE(cmd, subsys_id, base, WDMA_SRC_SIZE, subfrm->src,
		     0x3FFF3FFF);
	/* Write target size */
	MM_REG_WRITE(cmd, subsys_id, base, WDMA_CLIP_SIZE, subfrm->clip,
		     0x3FFF3FFF);
	/* Write clip offset */
	MM_REG_WRITE(cmd, subsys_id, base, WDMA_CLIP_COORD, subfrm->clip_ofst,
		     0x3FFF3FFF);

	/* Enable WDMA */
	MM_REG_WRITE(cmd, subsys_id, base, WDMA_EN, BIT(0), BIT(0));

	return 0;
}

static int wait_wdma_event(struct mdp_comp_ctx *ctx, struct mdp_cmdq_cmd *cmd)
{
	phys_addr_t base = ctx->comp->reg_base;
	u8 subsys_id = ctx->comp->subsys_id;

	MM_REG_WAIT(cmd, ctx->comp->gce_event[MDP_GCE_EVENT_EOF]);
	/* Disable WDMA */
	MM_REG_WRITE(cmd, subsys_id, base, WDMA_EN, 0x0, BIT(0));
	return 0;
}

static const struct mdp_comp_ops wdma_ops = {
	.get_comp_flag = get_comp_flag,
	.init_comp = init_wdma,
	.config_frame = config_wdma_frame,
	.config_subfrm = config_wdma_subfrm,
	.wait_comp_event = wait_wdma_event,
};

static int init_ccorr(struct mdp_comp_ctx *ctx, struct mdp_cmdq_cmd *cmd)
{
	phys_addr_t base = ctx->comp->reg_base;
	u8 subsys_id = ctx->comp->subsys_id;

	/* CCORR enable */
	MM_REG_WRITE(cmd, subsys_id, base, MDP_CCORR_EN, BIT(0), BIT(0));
	/* Relay mode */
	MM_REG_WRITE(cmd, subsys_id, base, MDP_CCORR_CFG, BIT(0), BIT(0));
	return 0;
}

static int config_ccorr_subfrm(struct mdp_comp_ctx *ctx,
			       struct mdp_cmdq_cmd *cmd, u32 index)
{
	const struct img_comp_subfrm *csf = &ctx->param->subfrms[index];
	phys_addr_t base = ctx->comp->reg_base;
	u8 subsys_id = ctx->comp->subsys_id;
	u32 hsize, vsize;

	hsize = csf->in.right - csf->in.left + 1;
	vsize = csf->in.bottom - csf->in.top + 1;
	MM_REG_WRITE(cmd, subsys_id, base, MDP_CCORR_SIZE,
		     (hsize << 16) + (vsize <<  0), 0x1FFF1FFF);
	return 0;
}

static const struct mdp_comp_ops ccorr_ops = {
	.get_comp_flag = get_comp_flag,
	.init_comp = init_ccorr,
	.config_subfrm = config_ccorr_subfrm,
};

static const struct mdp_comp_ops *mdp_comp_ops[MDP_COMP_TYPE_COUNT] = {
	[MDP_COMP_TYPE_RDMA] =		&rdma_ops,
	[MDP_COMP_TYPE_RSZ] =		&rsz_ops,
	[MDP_COMP_TYPE_WROT] =		&wrot_ops,
	[MDP_COMP_TYPE_WDMA] =		&wdma_ops,
	[MDP_COMP_TYPE_CCORR] =		&ccorr_ops,
};

struct mdp_comp_match {
	enum mdp_comp_type	type;
	u32			alias_id;
};

static const struct mdp_comp_match mdp_comp_matches[MDP_MAX_COMP_COUNT] = {
	[MDP_COMP_WPEI] =	{ MDP_COMP_TYPE_WPEI, 0 },
	[MDP_COMP_WPEO] =	{ MDP_COMP_TYPE_EXTO, 2 },
	[MDP_COMP_WPEI2] =	{ MDP_COMP_TYPE_WPEI, 1 },
	[MDP_COMP_WPEO2] =	{ MDP_COMP_TYPE_EXTO, 3 },
	[MDP_COMP_ISP_IMGI] =	{ MDP_COMP_TYPE_IMGI, 0 },
	[MDP_COMP_ISP_IMGO] =	{ MDP_COMP_TYPE_EXTO, 0 },
	[MDP_COMP_ISP_IMG2O] =	{ MDP_COMP_TYPE_EXTO, 1 },

	[MDP_COMP_CAMIN] =	{ MDP_COMP_TYPE_DL_PATH, 0 },
	[MDP_COMP_CAMIN2] =	{ MDP_COMP_TYPE_DL_PATH, 1 },
	[MDP_COMP_RDMA0] =	{ MDP_COMP_TYPE_RDMA, 0 },
	[MDP_COMP_CCORR0] =	{ MDP_COMP_TYPE_CCORR, 0 },
	[MDP_COMP_RSZ0] =	{ MDP_COMP_TYPE_RSZ, 0 },
	[MDP_COMP_RSZ1] =	{ MDP_COMP_TYPE_RSZ, 1 },
	[MDP_COMP_PATH0_SOUT] =	{ MDP_COMP_TYPE_PATH, 0 },
	[MDP_COMP_PATH1_SOUT] =	{ MDP_COMP_TYPE_PATH, 1 },
	[MDP_COMP_WROT0] =	{ MDP_COMP_TYPE_WROT, 0 },
	[MDP_COMP_WDMA] =	{ MDP_COMP_TYPE_WDMA, 0 },
};

static const struct of_device_id mdp_comp_dt_ids[] = {
	{
		.compatible = "mediatek,mt8183-mdp3-rdma",
		.data = (void *)MDP_COMP_TYPE_RDMA,
	}, {
		.compatible = "mediatek,mt8183-mdp3-ccorr",
		.data = (void *)MDP_COMP_TYPE_CCORR,
	}, {
		.compatible = "mediatek,mt8183-mdp3-rsz",
		.data = (void *)MDP_COMP_TYPE_RSZ,
	}, {
		.compatible = "mediatek,mt8183-mdp3-wrot",
		.data = (void *)MDP_COMP_TYPE_WROT,
	}, {
		.compatible = "mediatek,mt8183-mdp3-wdma",
		.data = (void *)MDP_COMP_TYPE_WDMA,
	},
	{}
};

static const struct of_device_id mdp_sub_comp_dt_ids[] = {
	{
		.compatible = "mediatek,mt8183-mdp3-wdma",
		.data = (void *)MDP_COMP_TYPE_PATH,
	}, {
		.compatible = "mediatek,mt8183-mdp3-wrot",
		.data = (void *)MDP_COMP_TYPE_PATH,
	},
	{}
};

/* Used to describe the item order in MDP property */
struct mdp_comp_info {
	u32	clk_num;
	u32	clk_ofst;
	u32	dts_reg_ofst;
};

static const struct mdp_comp_info mdp_comp_dt_info[MDP_MAX_COMP_COUNT] = {
	[MDP_COMP_RDMA0]	= {2, 0, 0},
	[MDP_COMP_RSZ0]		= {1, 0, 0},
	[MDP_COMP_WROT0]	= {1, 0, 0},
	[MDP_COMP_WDMA]		= {1, 0, 0},
	[MDP_COMP_CCORR0]	= {1, 0, 0},
};

static inline bool is_dma_capable(const enum mdp_comp_type type)
{
	return (type == MDP_COMP_TYPE_RDMA ||
		type == MDP_COMP_TYPE_WROT ||
		type == MDP_COMP_TYPE_WDMA);
}

static inline bool is_bypass_gce_event(const enum mdp_comp_type type)
{
	/*
	 * Subcomponent PATH is only used for the direction of data flow and
	 * dose not need to wait for GCE event.
	 */
	return (type == MDP_COMP_TYPE_PATH);
}

static int mdp_comp_get_id(enum mdp_comp_type type, int alias_id)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(mdp_comp_matches); i++)
		if (mdp_comp_matches[i].type == type &&
		    mdp_comp_matches[i].alias_id == alias_id)
			return i;
	return -ENODEV;
}

int mdp_comp_clock_on(struct device *dev, struct mdp_comp *comp)
{
	int i, ret;

	if (comp->comp_dev) {
		ret = pm_runtime_resume_and_get(comp->comp_dev);
		if (ret < 0) {
			dev_err(dev,
				"Failed to get power, err %d. type:%d id:%d\n",
				ret, comp->type, comp->id);
			return ret;
		}
	}

	for (i = 0; i < ARRAY_SIZE(comp->clks); i++) {
		if (IS_ERR_OR_NULL(comp->clks[i]))
			continue;
		ret = clk_prepare_enable(comp->clks[i]);
		if (ret) {
			dev_err(dev,
				"Failed to enable clk %d. type:%d id:%d\n",
				i, comp->type, comp->id);
			goto err_revert;
		}
	}

	return 0;

err_revert:
	while (--i >= 0) {
		if (IS_ERR_OR_NULL(comp->clks[i]))
			continue;
		clk_disable_unprepare(comp->clks[i]);
	}
	if (comp->comp_dev)
		pm_runtime_put_sync(comp->comp_dev);

	return ret;
}

void mdp_comp_clock_off(struct device *dev, struct mdp_comp *comp)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(comp->clks); i++) {
		if (IS_ERR_OR_NULL(comp->clks[i]))
			continue;
		clk_disable_unprepare(comp->clks[i]);
	}

	if (comp->comp_dev)
		pm_runtime_put(comp->comp_dev);
}

int mdp_comp_clocks_on(struct device *dev, struct mdp_comp *comps, int num)
{
	int i, ret;

	for (i = 0; i < num; i++) {
		ret = mdp_comp_clock_on(dev, &comps[i]);
		if (ret)
			return ret;
	}

	return 0;
}

void mdp_comp_clocks_off(struct device *dev, struct mdp_comp *comps, int num)
{
	int i;

	for (i = 0; i < num; i++)
		mdp_comp_clock_off(dev, &comps[i]);
}

static int mdp_get_subsys_id(struct device *dev, struct device_node *node,
			     struct mdp_comp *comp)
{
	struct platform_device *comp_pdev;
	struct cmdq_client_reg  cmdq_reg;
	int ret = 0;
	int index = 0;

	if (!dev || !node || !comp)
		return -EINVAL;

	comp_pdev = of_find_device_by_node(node);

	if (!comp_pdev) {
		dev_err(dev, "get comp_pdev fail! comp id=%d type=%d\n",
			comp->id, comp->type);
		return -ENODEV;
	}

	index = mdp_comp_dt_info[comp->id].dts_reg_ofst;
	ret = cmdq_dev_get_client_reg(&comp_pdev->dev, &cmdq_reg, index);
	if (ret != 0) {
		dev_err(&comp_pdev->dev, "cmdq_dev_get_subsys fail!\n");
		put_device(&comp_pdev->dev);
		return -EINVAL;
	}

	comp->subsys_id = cmdq_reg.subsys;
	dev_dbg(&comp_pdev->dev, "subsys id=%d\n", cmdq_reg.subsys);
	put_device(&comp_pdev->dev);

	return 0;
}

static void __mdp_comp_init(struct mdp_dev *mdp, struct device_node *node,
			    struct mdp_comp *comp)
{
	struct resource res;
	phys_addr_t base;
	int index = mdp_comp_dt_info[comp->id].dts_reg_ofst;

	if (of_address_to_resource(node, index, &res) < 0)
		base = 0L;
	else
		base = res.start;

	comp->mdp_dev = mdp;
	comp->regs = of_iomap(node, 0);
	comp->reg_base = base;
}

static int mdp_comp_init(struct mdp_dev *mdp, struct device_node *node,
			 struct mdp_comp *comp, enum mtk_mdp_comp_id id)
{
	struct device *dev = &mdp->pdev->dev;
	int clk_num;
	int clk_ofst;
	int i;
	s32 event;

	if (id < 0 || id >= MDP_MAX_COMP_COUNT) {
		dev_err(dev, "Invalid component id %d\n", id);
		return -EINVAL;
	}

	comp->id = id;
	comp->type = mdp_comp_matches[id].type;
	comp->alias_id = mdp_comp_matches[id].alias_id;
	comp->ops = mdp_comp_ops[comp->type];
	__mdp_comp_init(mdp, node, comp);

	clk_num = mdp_comp_dt_info[id].clk_num;
	clk_ofst = mdp_comp_dt_info[id].clk_ofst;

	for (i = 0; i < clk_num; i++) {
		comp->clks[i] = of_clk_get(node, i + clk_ofst);
		if (IS_ERR(comp->clks[i]))
			break;
	}

	mdp_get_subsys_id(dev, node, comp);

	/* Set GCE SOF event */
	if (is_bypass_gce_event(comp->type) ||
	    of_property_read_u32_index(node, "mediatek,gce-events",
				       MDP_GCE_EVENT_SOF, &event))
		event = MDP_GCE_NO_EVENT;

	comp->gce_event[MDP_GCE_EVENT_SOF] = event;

	/* Set GCE EOF event */
	if (is_dma_capable(comp->type)) {
		if (of_property_read_u32_index(node, "mediatek,gce-events",
					       MDP_GCE_EVENT_EOF, &event)) {
			dev_err(dev, "Component id %d has no EOF\n", id);
			return -EINVAL;
		}
	} else {
		event = MDP_GCE_NO_EVENT;
	}

	comp->gce_event[MDP_GCE_EVENT_EOF] = event;

	return 0;
}

static void mdp_comp_deinit(struct mdp_comp *comp)
{
	if (!comp)
		return;

	if (comp->regs)
		iounmap(comp->regs);
}

static struct mdp_comp *mdp_comp_create(struct mdp_dev *mdp,
					struct device_node *node,
					enum mtk_mdp_comp_id id)
{
	struct device *dev = &mdp->pdev->dev;
	struct mdp_comp *comp;
	int ret;

	if (mdp->comp[id])
		return ERR_PTR(-EEXIST);

	comp = devm_kzalloc(dev, sizeof(*comp), GFP_KERNEL);
	if (!comp)
		return ERR_PTR(-ENOMEM);

	ret = mdp_comp_init(mdp, node, comp, id);
	if (ret) {
		devm_kfree(dev, comp);
		return ERR_PTR(ret);
	}
	mdp->comp[id] = comp;
	mdp->comp[id]->mdp_dev = mdp;

	dev_dbg(dev, "%s type:%d alias:%d id:%d base:%#x regs:%p\n",
		dev->of_node->name, comp->type, comp->alias_id, id,
		(u32)comp->reg_base, comp->regs);
	return comp;
}

static int mdp_comp_sub_create(struct mdp_dev *mdp)
{
	struct device *dev = &mdp->pdev->dev;
	struct device_node *node, *parent;

	parent = dev->of_node->parent;

	for_each_child_of_node(parent, node) {
		const struct of_device_id *of_id;
		enum mdp_comp_type type;
		int id, alias_id;
		struct mdp_comp *comp;

		of_id = of_match_node(mdp_sub_comp_dt_ids, node);
		if (!of_id)
			continue;
		if (!of_device_is_available(node)) {
			dev_dbg(dev, "Skipping disabled sub comp. %pOF\n",
				node);
			continue;
		}

		type = (enum mdp_comp_type)(uintptr_t)of_id->data;
		alias_id = mdp_comp_alias_id[type];
		id = mdp_comp_get_id(type, alias_id);
		if (id < 0) {
			dev_err(dev,
				"Fail to get sub comp. id: type %d alias %d\n",
				type, alias_id);
			return -EINVAL;
		}
		mdp_comp_alias_id[type]++;

		comp = mdp_comp_create(mdp, node, id);
		if (IS_ERR(comp))
			return PTR_ERR(comp);
	}

	return 0;
}

void mdp_comp_destroy(struct mdp_dev *mdp)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(mdp->comp); i++) {
		if (mdp->comp[i]) {
			pm_runtime_disable(mdp->comp[i]->comp_dev);
			mdp_comp_deinit(mdp->comp[i]);
			devm_kfree(mdp->comp[i]->comp_dev, mdp->comp[i]);
			mdp->comp[i] = NULL;
		}
	}
}

int mdp_comp_config(struct mdp_dev *mdp)
{
	struct device *dev = &mdp->pdev->dev;
	struct device_node *node, *parent;
	struct platform_device *pdev;
	int ret;

	memset(mdp_comp_alias_id, 0, sizeof(mdp_comp_alias_id));

	parent = dev->of_node->parent;
	/* Iterate over sibling MDP function blocks */
	for_each_child_of_node(parent, node) {
		const struct of_device_id *of_id;
		enum mdp_comp_type type;
		int id, alias_id;
		struct mdp_comp *comp;

		of_id = of_match_node(mdp_comp_dt_ids, node);
		if (!of_id)
			continue;

		if (!of_device_is_available(node)) {
			dev_dbg(dev, "Skipping disabled component %pOF\n",
				node);
			continue;
		}

		type = (enum mdp_comp_type)(uintptr_t)of_id->data;
		alias_id = mdp_comp_alias_id[type];
		id = mdp_comp_get_id(type, alias_id);
		if (id < 0) {
			dev_err(dev,
				"Fail to get component id: type %d alias %d\n",
				type, alias_id);
			continue;
		}
		mdp_comp_alias_id[type]++;

		comp = mdp_comp_create(mdp, node, id);
		if (IS_ERR(comp)) {
			ret = PTR_ERR(comp);
			goto err_init_comps;
		}

		/* Only DMA capable components need the pm control */
		comp->comp_dev = NULL;
		if (!is_dma_capable(comp->type))
			continue;

		pdev = of_find_device_by_node(node);
		if (!pdev) {
			dev_warn(dev, "can't find platform device of node:%s\n",
				 node->name);
			return -ENODEV;
		}

		comp->comp_dev = &pdev->dev;
		pm_runtime_enable(comp->comp_dev);
	}

	ret = mdp_comp_sub_create(mdp);
	if (ret)
		goto err_init_comps;

	return 0;

err_init_comps:
	mdp_comp_destroy(mdp);
	return ret;
}

int mdp_comp_ctx_config(struct mdp_dev *mdp, struct mdp_comp_ctx *ctx,
			const struct img_compparam *param,
			const struct img_ipi_frameparam *frame)
{
	struct device *dev = &mdp->pdev->dev;
	int i;

	if (param->type < 0 || param->type >= MDP_MAX_COMP_COUNT) {
		dev_err(dev, "Invalid component id %d", param->type);
		return -EINVAL;
	}

	ctx->comp = mdp->comp[param->type];
	if (!ctx->comp) {
		dev_err(dev, "Uninit component id %d", param->type);
		return -EINVAL;
	}

	ctx->param = param;
	ctx->input = &frame->inputs[param->input];
	for (i = 0; i < param->num_outputs; i++)
		ctx->outputs[i] = &frame->outputs[param->outputs[i]];
	return 0;
}
