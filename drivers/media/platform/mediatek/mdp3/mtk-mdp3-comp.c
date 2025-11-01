// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022 MediaTek Inc.
 * Author: Ping-Hsun Wu <ping-hsun.wu@mediatek.com>
 */

#include <linux/clk.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/pm_runtime.h>
#include "mtk-mdp3-cfg.h"
#include "mtk-mdp3-comp.h"
#include "mtk-mdp3-core.h"
#include "mtk-mdp3-regs.h"

#include "mdp_reg_aal.h"
#include "mdp_reg_ccorr.h"
#include "mdp_reg_color.h"
#include "mdp_reg_fg.h"
#include "mdp_reg_hdr.h"
#include "mdp_reg_merge.h"
#include "mdp_reg_ovl.h"
#include "mdp_reg_pad.h"
#include "mdp_reg_rdma.h"
#include "mdp_reg_rsz.h"
#include "mdp_reg_tdshp.h"
#include "mdp_reg_wdma.h"
#include "mdp_reg_wrot.h"

static u32 mdp_comp_alias_id[MDP_COMP_TYPE_COUNT];
static int p_id;

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
	u32 rdma0, rsz1;

	rdma0 = mdp_cfg_get_id_inner(ctx->comp->mdp_dev, MDP_COMP_RDMA0);
	rsz1 = mdp_cfg_get_id_inner(ctx->comp->mdp_dev, MDP_COMP_RSZ1);
	if (!rdma0 || !rsz1)
		return MDP_COMP_NONE;

	if (mdp_cfg && mdp_cfg->rdma_rsz1_sram_sharing)
		if (ctx->comp->inner_id == rdma0)
			return BIT(rdma0) | BIT(rsz1);

	return BIT(ctx->comp->inner_id);
}

static int init_rdma(struct mdp_comp_ctx *ctx, struct mdp_cmdq_cmd *cmd)
{
	const struct mdp_platform_config *mdp_cfg = __get_plat_cfg(ctx);
	phys_addr_t base = ctx->comp->reg_base;
	u8 subsys_id = ctx->comp->subsys_id;
	s32 rdma0;

	rdma0 = mdp_cfg_get_id_inner(ctx->comp->mdp_dev, MDP_COMP_RDMA0);
	if (!rdma0)
		return -EINVAL;

	if (mdp_cfg && mdp_cfg->rdma_support_10bit) {
		struct mdp_comp *prz1 = ctx->comp->mdp_dev->comp[MDP_COMP_RSZ1];

		/* Disable RSZ1 */
		if (ctx->comp->inner_id == rdma0 && prz1)
			MM_REG_WRITE_MASK(cmd, subsys_id, prz1->reg_base,
					  PRZ_ENABLE, 0x0, BIT(0));
	}

	/* Reset RDMA */
	MM_REG_WRITE_MASK(cmd, subsys_id, base, MDP_RDMA_RESET, BIT(0), BIT(0));
	MM_REG_POLL_MASK(cmd, subsys_id, base, MDP_RDMA_MON_STA_1, BIT(8), BIT(8));
	MM_REG_WRITE_MASK(cmd, subsys_id, base, MDP_RDMA_RESET, 0x0, BIT(0));
	return 0;
}

static int config_rdma_frame(struct mdp_comp_ctx *ctx,
			     struct mdp_cmdq_cmd *cmd,
			     const struct v4l2_rect *compose)
{
	const struct mdp_platform_config *mdp_cfg = __get_plat_cfg(ctx);
	u32 colorformat = ctx->input->buffer.format.colorformat;
	bool block10bit = MDP_COLOR_IS_10BIT_PACKED(colorformat);
	bool en_ufo = MDP_COLOR_IS_UFP(colorformat);
	phys_addr_t base = ctx->comp->reg_base;
	u8 subsys_id = ctx->comp->subsys_id;
	u32 rdma_con_mask = 0;
	u32 reg = 0;

	if (mdp_cfg && mdp_cfg->rdma_support_10bit) {
		if (block10bit)
			MM_REG_WRITE_MASK(cmd, subsys_id, base,
					  MDP_RDMA_RESV_DUMMY_0, 0x7, 0x7);
		else
			MM_REG_WRITE_MASK(cmd, subsys_id, base,
					  MDP_RDMA_RESV_DUMMY_0, 0x0, 0x7);
	}

	/* Setup smi control */
	MM_REG_WRITE_MASK(cmd, subsys_id, base, MDP_RDMA_GMCIF_CON,
			  (7 <<  4) + //burst type to 8
			  (1 << 16),  //enable pre-ultra
			  0x00030071);

	/* Setup source frame info */
	if (CFG_CHECK(MT8183, p_id))
		reg = CFG_COMP(MT8183, ctx->param, rdma.src_ctrl);
	else if (CFG_CHECK(MT8195, p_id))
		reg = CFG_COMP(MT8195, ctx->param, rdma.src_ctrl);
	MM_REG_WRITE_MASK(cmd, subsys_id, base, MDP_RDMA_SRC_CON, reg, 0x03C8FE0F);

	if (mdp_cfg)
		if (mdp_cfg->rdma_support_10bit && en_ufo) {
			/* Setup source buffer base */
			if (CFG_CHECK(MT8183, p_id))
				reg = CFG_COMP(MT8183, ctx->param, rdma.ufo_dec_y);
			else if (CFG_CHECK(MT8195, p_id))
				reg = CFG_COMP(MT8195, ctx->param, rdma.ufo_dec_y);
			MM_REG_WRITE(cmd, subsys_id, base,
				     MDP_RDMA_UFO_DEC_LENGTH_BASE_Y, reg);

			if (CFG_CHECK(MT8183, p_id))
				reg = CFG_COMP(MT8183, ctx->param, rdma.ufo_dec_c);
			else if (CFG_CHECK(MT8195, p_id))
				reg = CFG_COMP(MT8195, ctx->param, rdma.ufo_dec_c);
			MM_REG_WRITE(cmd, subsys_id, base,
				     MDP_RDMA_UFO_DEC_LENGTH_BASE_C, reg);

			/* Set 10bit source frame pitch */
			if (block10bit) {
				if (CFG_CHECK(MT8183, p_id))
					reg = CFG_COMP(MT8183, ctx->param, rdma.mf_bkgd_in_pxl);
				else if (CFG_CHECK(MT8195, p_id))
					reg = CFG_COMP(MT8195, ctx->param, rdma.mf_bkgd_in_pxl);
				MM_REG_WRITE_MASK(cmd, subsys_id, base,
						  MDP_RDMA_MF_BKGD_SIZE_IN_PXL,
						  reg, 0x001FFFFF);
			}
		}

	if (CFG_CHECK(MT8183, p_id)) {
		reg = CFG_COMP(MT8183, ctx->param, rdma.control);
		rdma_con_mask = 0x1110;
	} else if (CFG_CHECK(MT8195, p_id)) {
		reg = CFG_COMP(MT8195, ctx->param, rdma.control);
		rdma_con_mask = 0x1130;
	}
	MM_REG_WRITE_MASK(cmd, subsys_id, base, MDP_RDMA_CON, reg, rdma_con_mask);

	/* Setup source buffer base */
	if (CFG_CHECK(MT8183, p_id))
		reg = CFG_COMP(MT8183, ctx->param, rdma.iova[0]);
	else if (CFG_CHECK(MT8195, p_id))
		reg = CFG_COMP(MT8195, ctx->param, rdma.iova[0]);
	MM_REG_WRITE(cmd, subsys_id, base, MDP_RDMA_SRC_BASE_0, reg);

	if (CFG_CHECK(MT8183, p_id))
		reg = CFG_COMP(MT8183, ctx->param, rdma.iova[1]);
	else if (CFG_CHECK(MT8195, p_id))
		reg = CFG_COMP(MT8195, ctx->param, rdma.iova[1]);
	MM_REG_WRITE(cmd, subsys_id, base, MDP_RDMA_SRC_BASE_1, reg);

	if (CFG_CHECK(MT8183, p_id))
		reg = CFG_COMP(MT8183, ctx->param, rdma.iova[2]);
	else if (CFG_CHECK(MT8195, p_id))
		reg = CFG_COMP(MT8195, ctx->param, rdma.iova[2]);
	MM_REG_WRITE(cmd, subsys_id, base, MDP_RDMA_SRC_BASE_2, reg);

	/* Setup source buffer end */
	if (CFG_CHECK(MT8183, p_id))
		reg = CFG_COMP(MT8183, ctx->param, rdma.iova_end[0]);
	else if (CFG_CHECK(MT8195, p_id))
		reg = CFG_COMP(MT8195, ctx->param, rdma.iova_end[0]);
	MM_REG_WRITE(cmd, subsys_id, base, MDP_RDMA_SRC_END_0, reg);

	if (CFG_CHECK(MT8183, p_id))
		reg = CFG_COMP(MT8183, ctx->param, rdma.iova_end[1]);
	else if (CFG_CHECK(MT8195, p_id))
		reg = CFG_COMP(MT8195, ctx->param, rdma.iova_end[1]);
	MM_REG_WRITE(cmd, subsys_id, base, MDP_RDMA_SRC_END_1, reg);

	if (CFG_CHECK(MT8183, p_id))
		reg = CFG_COMP(MT8183, ctx->param, rdma.iova_end[2]);
	else if (CFG_CHECK(MT8195, p_id))
		reg = CFG_COMP(MT8195, ctx->param, rdma.iova_end[2]);
	MM_REG_WRITE(cmd, subsys_id, base, MDP_RDMA_SRC_END_2, reg);

	/* Setup source frame pitch */
	if (CFG_CHECK(MT8183, p_id))
		reg = CFG_COMP(MT8183, ctx->param, rdma.mf_bkgd);
	else if (CFG_CHECK(MT8195, p_id))
		reg = CFG_COMP(MT8195, ctx->param, rdma.mf_bkgd);
	MM_REG_WRITE_MASK(cmd, subsys_id, base, MDP_RDMA_MF_BKGD_SIZE_IN_BYTE,
			  reg, 0x001FFFFF);

	if (CFG_CHECK(MT8183, p_id))
		reg = CFG_COMP(MT8183, ctx->param, rdma.sf_bkgd);
	else if (CFG_CHECK(MT8195, p_id))
		reg = CFG_COMP(MT8195, ctx->param, rdma.sf_bkgd);
	MM_REG_WRITE_MASK(cmd, subsys_id, base, MDP_RDMA_SF_BKGD_SIZE_IN_BYTE,
			  reg, 0x001FFFFF);

	/* Setup color transform */
	if (CFG_CHECK(MT8183, p_id))
		reg = CFG_COMP(MT8183, ctx->param, rdma.transform);
	else if (CFG_CHECK(MT8195, p_id))
		reg = CFG_COMP(MT8195, ctx->param, rdma.transform);
	MM_REG_WRITE_MASK(cmd, subsys_id, base, MDP_RDMA_TRANSFORM_0,
			  reg, 0x0F110000);

	if (!mdp_cfg || !mdp_cfg->rdma_esl_setting)
		goto rdma_config_done;

	if (CFG_CHECK(MT8195, p_id))
		reg = CFG_COMP(MT8195, ctx->param, rdma.dmabuf_con0);
	MM_REG_WRITE_MASK(cmd, subsys_id, base, MDP_RDMA_DMABUF_CON_0,
			  reg, 0x0FFF00FF);

	if (CFG_CHECK(MT8195, p_id))
		reg = CFG_COMP(MT8195, ctx->param, rdma.ultra_th_high_con0);
	MM_REG_WRITE_MASK(cmd, subsys_id, base, MDP_RDMA_ULTRA_TH_HIGH_CON_0,
			  reg, 0x3FFFFFFF);

	if (CFG_CHECK(MT8195, p_id))
		reg = CFG_COMP(MT8195, ctx->param, rdma.ultra_th_low_con0);
	MM_REG_WRITE_MASK(cmd, subsys_id, base, MDP_RDMA_ULTRA_TH_LOW_CON_0,
			  reg, 0x3FFFFFFF);

	if (CFG_CHECK(MT8195, p_id))
		reg = CFG_COMP(MT8195, ctx->param, rdma.dmabuf_con1);
	MM_REG_WRITE_MASK(cmd, subsys_id, base, MDP_RDMA_DMABUF_CON_1,
			  reg, 0x0F7F007F);

	if (CFG_CHECK(MT8195, p_id))
		reg = CFG_COMP(MT8195, ctx->param, rdma.ultra_th_high_con1);
	MM_REG_WRITE_MASK(cmd, subsys_id, base, MDP_RDMA_ULTRA_TH_HIGH_CON_1,
			  reg, 0x3FFFFFFF);

	if (CFG_CHECK(MT8195, p_id))
		reg = CFG_COMP(MT8195, ctx->param, rdma.ultra_th_low_con1);
	MM_REG_WRITE_MASK(cmd, subsys_id, base, MDP_RDMA_ULTRA_TH_LOW_CON_1,
			  reg, 0x3FFFFFFF);

	if (CFG_CHECK(MT8195, p_id))
		reg = CFG_COMP(MT8195, ctx->param, rdma.dmabuf_con2);
	MM_REG_WRITE_MASK(cmd, subsys_id, base, MDP_RDMA_DMABUF_CON_2,
			  reg, 0x0F3F003F);

	if (CFG_CHECK(MT8195, p_id))
		reg = CFG_COMP(MT8195, ctx->param, rdma.ultra_th_high_con2);
	MM_REG_WRITE_MASK(cmd, subsys_id, base, MDP_RDMA_ULTRA_TH_HIGH_CON_2,
			  reg, 0x3FFFFFFF);

	if (CFG_CHECK(MT8195, p_id))
		reg = CFG_COMP(MT8195, ctx->param, rdma.ultra_th_low_con2);
	MM_REG_WRITE_MASK(cmd, subsys_id, base, MDP_RDMA_ULTRA_TH_LOW_CON_2,
			  reg, 0x3FFFFFFF);

	if (CFG_CHECK(MT8195, p_id))
		reg = CFG_COMP(MT8195, ctx->param, rdma.dmabuf_con3);
	MM_REG_WRITE_MASK(cmd, subsys_id, base, MDP_RDMA_DMABUF_CON_3,
			  reg, 0x0F3F003F);

rdma_config_done:
	return 0;
}

static int config_rdma_subfrm(struct mdp_comp_ctx *ctx,
			      struct mdp_cmdq_cmd *cmd, u32 index)
{
	const struct mdp_platform_config *mdp_cfg = __get_plat_cfg(ctx);
	u32 colorformat = ctx->input->buffer.format.colorformat;
	bool block10bit = MDP_COLOR_IS_10BIT_PACKED(colorformat);
	bool en_ufo = MDP_COLOR_IS_UFP(colorformat);
	phys_addr_t base = ctx->comp->reg_base;
	u8 subsys_id = ctx->comp->subsys_id;
	u32 csf_l = 0, csf_r = 0;
	u32 reg = 0;

	/* Enable RDMA */
	MM_REG_WRITE_MASK(cmd, subsys_id, base, MDP_RDMA_EN, BIT(0), BIT(0));

	/* Set Y pixel offset */
	if (CFG_CHECK(MT8183, p_id))
		reg = CFG_COMP(MT8183, ctx->param, rdma.subfrms[index].offset[0]);
	else if (CFG_CHECK(MT8195, p_id))
		reg = CFG_COMP(MT8195, ctx->param, rdma.subfrms[index].offset[0]);
	MM_REG_WRITE(cmd, subsys_id, base, MDP_RDMA_SRC_OFFSET_0, reg);

	/* Set 10bit UFO mode */
	if (mdp_cfg) {
		if (mdp_cfg->rdma_support_10bit && block10bit && en_ufo) {
			if (CFG_CHECK(MT8183, p_id))
				reg = CFG_COMP(MT8183, ctx->param, rdma.subfrms[index].offset_0_p);
			else if (CFG_CHECK(MT8195, p_id))
				reg = CFG_COMP(MT8195, ctx->param, rdma.subfrms[index].offset_0_p);
			MM_REG_WRITE(cmd, subsys_id, base,
				     MDP_RDMA_SRC_OFFSET_0_P, reg);
		}
	}

	/* Set U pixel offset */
	if (CFG_CHECK(MT8183, p_id))
		reg = CFG_COMP(MT8183, ctx->param, rdma.subfrms[index].offset[1]);
	else if (CFG_CHECK(MT8195, p_id))
		reg = CFG_COMP(MT8195, ctx->param, rdma.subfrms[index].offset[1]);
	MM_REG_WRITE(cmd, subsys_id, base, MDP_RDMA_SRC_OFFSET_1, reg);

	/* Set V pixel offset */
	if (CFG_CHECK(MT8183, p_id))
		reg = CFG_COMP(MT8183, ctx->param, rdma.subfrms[index].offset[2]);
	else if (CFG_CHECK(MT8195, p_id))
		reg = CFG_COMP(MT8195, ctx->param, rdma.subfrms[index].offset[2]);
	MM_REG_WRITE(cmd, subsys_id, base, MDP_RDMA_SRC_OFFSET_2, reg);

	/* Set source size */
	if (CFG_CHECK(MT8183, p_id))
		reg = CFG_COMP(MT8183, ctx->param, rdma.subfrms[index].src);
	else if (CFG_CHECK(MT8195, p_id))
		reg = CFG_COMP(MT8195, ctx->param, rdma.subfrms[index].src);
	MM_REG_WRITE_MASK(cmd, subsys_id, base, MDP_RDMA_MF_SRC_SIZE, reg,
			  0x1FFF1FFF);

	/* Set target size */
	if (CFG_CHECK(MT8183, p_id))
		reg = CFG_COMP(MT8183, ctx->param, rdma.subfrms[index].clip);
	else if (CFG_CHECK(MT8195, p_id))
		reg = CFG_COMP(MT8195, ctx->param, rdma.subfrms[index].clip);
	MM_REG_WRITE_MASK(cmd, subsys_id, base, MDP_RDMA_MF_CLIP_SIZE,
			  reg, 0x1FFF1FFF);

	/* Set crop offset */
	if (CFG_CHECK(MT8183, p_id))
		reg = CFG_COMP(MT8183, ctx->param, rdma.subfrms[index].clip_ofst);
	else if (CFG_CHECK(MT8195, p_id))
		reg = CFG_COMP(MT8195, ctx->param, rdma.subfrms[index].clip_ofst);
	MM_REG_WRITE_MASK(cmd, subsys_id, base, MDP_RDMA_MF_OFFSET_1,
			  reg, 0x003F001F);

	if (CFG_CHECK(MT8183, p_id)) {
		csf_l = CFG_COMP(MT8183, ctx->param, subfrms[index].in.left);
		csf_r = CFG_COMP(MT8183, ctx->param, subfrms[index].in.right);
	} else if (CFG_CHECK(MT8195, p_id)) {
		csf_l = CFG_COMP(MT8195, ctx->param, subfrms[index].in.left);
		csf_r = CFG_COMP(MT8195, ctx->param, subfrms[index].in.right);
	}
	if (mdp_cfg && mdp_cfg->rdma_upsample_repeat_only)
		if ((csf_r - csf_l + 1) > 320)
			MM_REG_WRITE_MASK(cmd, subsys_id, base,
					  MDP_RDMA_RESV_DUMMY_0, BIT(2), BIT(2));

	return 0;
}

static int wait_rdma_event(struct mdp_comp_ctx *ctx, struct mdp_cmdq_cmd *cmd)
{
	const struct mdp_platform_config *mdp_cfg = __get_plat_cfg(ctx);
	struct device *dev = &ctx->comp->mdp_dev->pdev->dev;
	phys_addr_t base = ctx->comp->reg_base;
	u8 subsys_id = ctx->comp->subsys_id;

	if (!mdp_cfg)
		return -EINVAL;

	if (ctx->comp->alias_id >= mdp_cfg->rdma_event_num) {
		dev_err(dev, "Invalid RDMA event %d\n", ctx->comp->alias_id);
		return -EINVAL;
	}

	MM_REG_WAIT(cmd, ctx->comp->gce_event[MDP_GCE_EVENT_EOF]);

	/* Disable RDMA */
	MM_REG_WRITE_MASK(cmd, subsys_id, base, MDP_RDMA_EN, 0x0, BIT(0));
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
	MM_REG_WRITE_MASK(cmd, subsys_id, base, PRZ_ENABLE, 0x10000, BIT(16));
	MM_REG_WRITE_MASK(cmd, subsys_id, base, PRZ_ENABLE, 0x0, BIT(16));
	/* Enable RSZ */
	MM_REG_WRITE_MASK(cmd, subsys_id, base, PRZ_ENABLE, BIT(0), BIT(0));

	if (CFG_CHECK(MT8195, p_id)) {
		struct device *dev;

		dev = ctx->comp->mdp_dev->mm_subsys[MDP_MM_SUBSYS_1].mmsys;
		mtk_mmsys_vpp_rsz_dcm_config(dev, true, NULL);
	}

	return 0;
}

static int config_rsz_frame(struct mdp_comp_ctx *ctx,
			    struct mdp_cmdq_cmd *cmd,
			    const struct v4l2_rect *compose)
{
	const struct mdp_platform_config *mdp_cfg = __get_plat_cfg(ctx);
	phys_addr_t base = ctx->comp->reg_base;
	u8 subsys_id = ctx->comp->subsys_id;
	bool bypass = FALSE;
	u32 reg = 0;

	if (mdp_cfg && mdp_cfg->rsz_etc_control)
		MM_REG_WRITE(cmd, subsys_id, base, RSZ_ETC_CONTROL, 0x0);

	if (CFG_CHECK(MT8183, p_id))
		bypass = CFG_COMP(MT8183, ctx->param, frame.bypass);
	else if (CFG_CHECK(MT8195, p_id))
		bypass = CFG_COMP(MT8195, ctx->param, frame.bypass);

	if (bypass) {
		/* Disable RSZ */
		MM_REG_WRITE_MASK(cmd, subsys_id, base, PRZ_ENABLE, 0x0, BIT(0));
		return 0;
	}

	if (CFG_CHECK(MT8183, p_id))
		reg = CFG_COMP(MT8183, ctx->param, rsz.control1);
	else if (CFG_CHECK(MT8195, p_id))
		reg = CFG_COMP(MT8195, ctx->param, rsz.control1);
	MM_REG_WRITE_MASK(cmd, subsys_id, base, PRZ_CONTROL_1, reg, 0x03FFFDF3);

	if (CFG_CHECK(MT8183, p_id))
		reg = CFG_COMP(MT8183, ctx->param, rsz.control2);
	else if (CFG_CHECK(MT8195, p_id))
		reg = CFG_COMP(MT8195, ctx->param, rsz.control2);
	MM_REG_WRITE_MASK(cmd, subsys_id, base, PRZ_CONTROL_2, reg, 0x0FFFC290);

	if (CFG_CHECK(MT8183, p_id))
		reg = CFG_COMP(MT8183, ctx->param, rsz.coeff_step_x);
	else if (CFG_CHECK(MT8195, p_id))
		reg = CFG_COMP(MT8195, ctx->param, rsz.coeff_step_x);
	MM_REG_WRITE_MASK(cmd, subsys_id, base, PRZ_HORIZONTAL_COEFF_STEP, reg,
			  0x007FFFFF);

	if (CFG_CHECK(MT8183, p_id))
		reg = CFG_COMP(MT8183, ctx->param, rsz.coeff_step_y);
	else if (CFG_CHECK(MT8195, p_id))
		reg = CFG_COMP(MT8195, ctx->param, rsz.coeff_step_y);
	MM_REG_WRITE_MASK(cmd, subsys_id, base, PRZ_VERTICAL_COEFF_STEP, reg,
			  0x007FFFFF);

	return 0;
}

static int config_rsz_subfrm(struct mdp_comp_ctx *ctx,
			     struct mdp_cmdq_cmd *cmd, u32 index)
{
	const struct mdp_platform_config *mdp_cfg = __get_plat_cfg(ctx);
	phys_addr_t base = ctx->comp->reg_base;
	u8 subsys_id = ctx->comp->subsys_id;
	u32 csf_l = 0, csf_r = 0;
	u32 reg = 0;
	u32 id;

	if (CFG_CHECK(MT8183, p_id))
		reg = CFG_COMP(MT8183, ctx->param, rsz.subfrms[index].control2);
	else if (CFG_CHECK(MT8195, p_id))
		reg = CFG_COMP(MT8195, ctx->param, rsz.subfrms[index].control2);
	MM_REG_WRITE_MASK(cmd, subsys_id, base, PRZ_CONTROL_2, reg, 0x00003800);

	if (CFG_CHECK(MT8183, p_id))
		reg = CFG_COMP(MT8183, ctx->param, rsz.subfrms[index].src);
	else if (CFG_CHECK(MT8195, p_id))
		reg = CFG_COMP(MT8195, ctx->param, rsz.subfrms[index].src);
	MM_REG_WRITE(cmd, subsys_id, base, PRZ_INPUT_IMAGE, reg);

	if (CFG_CHECK(MT8183, p_id)) {
		csf_l = CFG_COMP(MT8183, ctx->param, subfrms[index].in.left);
		csf_r = CFG_COMP(MT8183, ctx->param, subfrms[index].in.right);
	} else if (CFG_CHECK(MT8195, p_id)) {
		csf_l = CFG_COMP(MT8195, ctx->param, subfrms[index].in.left);
		csf_r = CFG_COMP(MT8195, ctx->param, subfrms[index].in.right);
	}
	if (mdp_cfg && mdp_cfg->rsz_disable_dcm_small_sample)
		if ((csf_r - csf_l + 1) <= 16)
			MM_REG_WRITE_MASK(cmd, subsys_id, base, PRZ_CONTROL_1,
					  BIT(27), BIT(27));

	if (CFG_CHECK(MT8183, p_id))
		reg = CFG_COMP(MT8183, ctx->param, subfrms[index].luma.left);
	else if (CFG_CHECK(MT8195, p_id))
		reg = CFG_COMP(MT8195, ctx->param, subfrms[index].luma.left);
	MM_REG_WRITE_MASK(cmd, subsys_id, base, PRZ_LUMA_HORIZONTAL_INTEGER_OFFSET,
			  reg, 0xFFFF);

	if (CFG_CHECK(MT8183, p_id))
		reg = CFG_COMP(MT8183, ctx->param, subfrms[index].luma.left_subpix);
	else if (CFG_CHECK(MT8195, p_id))
		reg = CFG_COMP(MT8195, ctx->param, subfrms[index].luma.left_subpix);
	MM_REG_WRITE_MASK(cmd, subsys_id, base, PRZ_LUMA_HORIZONTAL_SUBPIXEL_OFFSET,
			  reg, 0x1FFFFF);

	if (CFG_CHECK(MT8183, p_id))
		reg = CFG_COMP(MT8183, ctx->param, subfrms[index].luma.top);
	else if (CFG_CHECK(MT8195, p_id))
		reg = CFG_COMP(MT8195, ctx->param, subfrms[index].luma.top);
	MM_REG_WRITE_MASK(cmd, subsys_id, base, PRZ_LUMA_VERTICAL_INTEGER_OFFSET,
			  reg, 0xFFFF);

	if (CFG_CHECK(MT8183, p_id))
		reg = CFG_COMP(MT8183, ctx->param, subfrms[index].luma.top_subpix);
	else if (CFG_CHECK(MT8195, p_id))
		reg = CFG_COMP(MT8195, ctx->param, subfrms[index].luma.top_subpix);
	MM_REG_WRITE_MASK(cmd, subsys_id, base, PRZ_LUMA_VERTICAL_SUBPIXEL_OFFSET,
			  reg, 0x1FFFFF);

	if (CFG_CHECK(MT8183, p_id))
		reg = CFG_COMP(MT8183, ctx->param, subfrms[index].chroma.left);
	else if (CFG_CHECK(MT8195, p_id))
		reg = CFG_COMP(MT8195, ctx->param, subfrms[index].chroma.left);
	MM_REG_WRITE_MASK(cmd, subsys_id, base, PRZ_CHROMA_HORIZONTAL_INTEGER_OFFSET,
			  reg, 0xFFFF);

	if (CFG_CHECK(MT8183, p_id))
		reg = CFG_COMP(MT8183, ctx->param, subfrms[index].chroma.left_subpix);
	else if (CFG_CHECK(MT8195, p_id))
		reg = CFG_COMP(MT8195, ctx->param, subfrms[index].chroma.left_subpix);
	MM_REG_WRITE_MASK(cmd, subsys_id, base, PRZ_CHROMA_HORIZONTAL_SUBPIXEL_OFFSET,
			  reg, 0x1FFFFF);

	if (CFG_CHECK(MT8183, p_id))
		reg = CFG_COMP(MT8183, ctx->param, rsz.subfrms[index].clip);
	else if (CFG_CHECK(MT8195, p_id))
		reg = CFG_COMP(MT8195, ctx->param, rsz.subfrms[index].clip);
	MM_REG_WRITE(cmd, subsys_id, base, PRZ_OUTPUT_IMAGE, reg);

	if (CFG_CHECK(MT8195, p_id)) {
		struct device *dev;
		struct mdp_comp *merge;
		const struct mtk_mdp_driver_data *data = ctx->comp->mdp_dev->mdp_data;
		enum mtk_mdp_comp_id public_id = ctx->comp->public_id;

		switch (public_id) {
		case MDP_COMP_RSZ2:
			merge = ctx->comp->mdp_dev->comp[MDP_COMP_MERGE2];
			break;
		case MDP_COMP_RSZ3:
			merge = ctx->comp->mdp_dev->comp[MDP_COMP_MERGE3];
			break;
		default:
			goto rsz_subfrm_done;
		}

		if (CFG_CHECK(MT8195, p_id))
			reg = CFG_COMP(MT8195, ctx->param, rsz.subfrms[index].rsz_switch);

		id = data->comp_data[public_id].match.alias_id;
		dev = ctx->comp->mdp_dev->mm_subsys[MDP_MM_SUBSYS_1].mmsys;
		mtk_mmsys_vpp_rsz_merge_config(dev, id, reg, NULL);

		if (CFG_CHECK(MT8195, p_id))
			reg = CFG_COMP(MT8195, ctx->param, rsz.subfrms[index].merge_cfg);
		MM_REG_WRITE(cmd, merge->subsys_id, merge->reg_base,
			     MDP_MERGE_CFG_0, reg);
		MM_REG_WRITE(cmd, merge->subsys_id, merge->reg_base,
			     MDP_MERGE_CFG_4, reg);
		MM_REG_WRITE(cmd, merge->subsys_id, merge->reg_base,
			     MDP_MERGE_CFG_24, reg);
		MM_REG_WRITE(cmd, merge->subsys_id, merge->reg_base,
			     MDP_MERGE_CFG_25, reg);

		/* Bypass mode */
		MM_REG_WRITE(cmd, merge->subsys_id, merge->reg_base,
			     MDP_MERGE_CFG_12, BIT(0));
		MM_REG_WRITE(cmd, merge->subsys_id, merge->reg_base,
			     MDP_MERGE_ENABLE, BIT(0));
	}

rsz_subfrm_done:
	return 0;
}

static int advance_rsz_subfrm(struct mdp_comp_ctx *ctx,
			      struct mdp_cmdq_cmd *cmd, u32 index)
{
	const struct mdp_platform_config *mdp_cfg = __get_plat_cfg(ctx);

	if (mdp_cfg && mdp_cfg->rsz_disable_dcm_small_sample) {
		phys_addr_t base = ctx->comp->reg_base;
		u8 subsys_id = ctx->comp->subsys_id;
		u32 csf_l = 0, csf_r = 0;

		if (CFG_CHECK(MT8183, p_id)) {
			csf_l = CFG_COMP(MT8183, ctx->param, subfrms[index].in.left);
			csf_r = CFG_COMP(MT8183, ctx->param, subfrms[index].in.right);
		} else if (CFG_CHECK(MT8195, p_id)) {
			csf_l = CFG_COMP(MT8195, ctx->param, subfrms[index].in.left);
			csf_r = CFG_COMP(MT8195, ctx->param, subfrms[index].in.right);
		}

		if ((csf_r - csf_l + 1) <= 16)
			MM_REG_WRITE_MASK(cmd, subsys_id, base, PRZ_CONTROL_1, 0x0,
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
	MM_REG_WRITE_MASK(cmd, subsys_id, base, VIDO_SOFT_RST, BIT(0), BIT(0));
	MM_REG_POLL_MASK(cmd, subsys_id, base, VIDO_SOFT_RST_STAT, BIT(0), BIT(0));

	/* Reset setting */
	if (CFG_CHECK(MT8195, p_id))
		MM_REG_WRITE(cmd, subsys_id, base, VIDO_CTRL, 0x0);

	MM_REG_WRITE_MASK(cmd, subsys_id, base, VIDO_SOFT_RST, 0x0, BIT(0));
	MM_REG_POLL_MASK(cmd, subsys_id, base, VIDO_SOFT_RST_STAT, 0x0, BIT(0));
	return 0;
}

static int config_wrot_frame(struct mdp_comp_ctx *ctx,
			     struct mdp_cmdq_cmd *cmd,
			     const struct v4l2_rect *compose)
{
	const struct mdp_platform_config *mdp_cfg = __get_plat_cfg(ctx);
	phys_addr_t base = ctx->comp->reg_base;
	u8 subsys_id = ctx->comp->subsys_id;
	u32 reg = 0;

	/* Write frame base address */
	if (CFG_CHECK(MT8183, p_id))
		reg = CFG_COMP(MT8183, ctx->param, wrot.iova[0]);
	else if (CFG_CHECK(MT8195, p_id))
		reg = CFG_COMP(MT8195, ctx->param, wrot.iova[0]);
	MM_REG_WRITE(cmd, subsys_id, base, VIDO_BASE_ADDR, reg);

	if (CFG_CHECK(MT8183, p_id))
		reg = CFG_COMP(MT8183, ctx->param, wrot.iova[1]);
	else if (CFG_CHECK(MT8195, p_id))
		reg = CFG_COMP(MT8195, ctx->param, wrot.iova[1]);
	MM_REG_WRITE(cmd, subsys_id, base, VIDO_BASE_ADDR_C, reg);

	if (CFG_CHECK(MT8183, p_id))
		reg = CFG_COMP(MT8183, ctx->param, wrot.iova[2]);
	else if (CFG_CHECK(MT8195, p_id))
		reg = CFG_COMP(MT8195, ctx->param, wrot.iova[2]);
	MM_REG_WRITE(cmd, subsys_id, base, VIDO_BASE_ADDR_V, reg);

	if (mdp_cfg && mdp_cfg->wrot_support_10bit) {
		if (CFG_CHECK(MT8195, p_id))
			reg = CFG_COMP(MT8195, ctx->param, wrot.scan_10bit);
		MM_REG_WRITE_MASK(cmd, subsys_id, base, VIDO_SCAN_10BIT,
				  reg, 0x0000000F);

		if (CFG_CHECK(MT8195, p_id))
			reg = CFG_COMP(MT8195, ctx->param, wrot.pending_zero);
		MM_REG_WRITE_MASK(cmd, subsys_id, base, VIDO_PENDING_ZERO,
				  reg, 0x04000000);
	}

	if (CFG_CHECK(MT8195, p_id)) {
		reg = CFG_COMP(MT8195, ctx->param, wrot.bit_number);
		MM_REG_WRITE_MASK(cmd, subsys_id, base, VIDO_CTRL_2,
				  reg, 0x00000007);
	}

	/* Write frame related registers */
	if (CFG_CHECK(MT8183, p_id))
		reg = CFG_COMP(MT8183, ctx->param, wrot.control);
	else if (CFG_CHECK(MT8195, p_id))
		reg = CFG_COMP(MT8195, ctx->param, wrot.control);
	MM_REG_WRITE_MASK(cmd, subsys_id, base, VIDO_CTRL, reg, 0xF131510F);

	/* Write pre-ultra threshold */
	if (CFG_CHECK(MT8195, p_id)) {
		reg = CFG_COMP(MT8195, ctx->param, wrot.pre_ultra);
		MM_REG_WRITE_MASK(cmd, subsys_id, base, VIDO_DMA_PREULTRA, reg,
				  0x00FFFFFF);
	}

	/* Write frame Y pitch */
	if (CFG_CHECK(MT8183, p_id))
		reg = CFG_COMP(MT8183, ctx->param, wrot.stride[0]);
	else if (CFG_CHECK(MT8195, p_id))
		reg = CFG_COMP(MT8195, ctx->param, wrot.stride[0]);
	MM_REG_WRITE_MASK(cmd, subsys_id, base, VIDO_STRIDE, reg, 0x0000FFFF);

	/* Write frame UV pitch */
	if (CFG_CHECK(MT8183, p_id))
		reg = CFG_COMP(MT8183, ctx->param, wrot.stride[1]);
	else if (CFG_CHECK(MT8195, p_id))
		reg = CFG_COMP(MT8195, ctx->param, wrot.stride[1]);
	MM_REG_WRITE_MASK(cmd, subsys_id, base, VIDO_STRIDE_C, reg, 0xFFFF);

	if (CFG_CHECK(MT8183, p_id))
		reg = CFG_COMP(MT8183, ctx->param, wrot.stride[2]);
	else if (CFG_CHECK(MT8195, p_id))
		reg = CFG_COMP(MT8195, ctx->param, wrot.stride[2]);
	MM_REG_WRITE_MASK(cmd, subsys_id, base, VIDO_STRIDE_V, reg, 0xFFFF);

	/* Write matrix control */
	if (CFG_CHECK(MT8183, p_id))
		reg = CFG_COMP(MT8183, ctx->param, wrot.mat_ctrl);
	else if (CFG_CHECK(MT8195, p_id))
		reg = CFG_COMP(MT8195, ctx->param, wrot.mat_ctrl);
	MM_REG_WRITE_MASK(cmd, subsys_id, base, VIDO_MAT_CTRL, reg, 0xF3);

	/* Set the fixed ALPHA as 0xFF */
	MM_REG_WRITE_MASK(cmd, subsys_id, base, VIDO_DITHER, 0xFF000000,
			  0xFF000000);

	/* Set VIDO_EOL_SEL */
	MM_REG_WRITE_MASK(cmd, subsys_id, base, VIDO_RSV_1, BIT(31), BIT(31));

	/* Set VIDO_FIFO_TEST */
	if (CFG_CHECK(MT8183, p_id))
		reg = CFG_COMP(MT8183, ctx->param, wrot.fifo_test);
	else if (CFG_CHECK(MT8195, p_id))
		reg = CFG_COMP(MT8195, ctx->param, wrot.fifo_test);

	if (reg != 0)
		MM_REG_WRITE_MASK(cmd, subsys_id, base, VIDO_FIFO_TEST, reg,
				  0xFFF);

	/* Filter enable */
	if (mdp_cfg && mdp_cfg->wrot_filter_constraint) {
		if (CFG_CHECK(MT8183, p_id))
			reg = CFG_COMP(MT8183, ctx->param, wrot.filter);
		else if (CFG_CHECK(MT8195, p_id))
			reg = CFG_COMP(MT8195, ctx->param, wrot.filter);
		MM_REG_WRITE_MASK(cmd, subsys_id, base, VIDO_MAIN_BUF_SIZE, reg,
				  0x77);

		/* Turn off WROT DMA DCM */
		if (CFG_CHECK(MT8195, p_id))
			MM_REG_WRITE_MASK(cmd, subsys_id, base, VIDO_ROT_EN,
					  (0x1 << 23) + (0x1 << 20), 0x900000);
	}

	return 0;
}

static int config_wrot_subfrm(struct mdp_comp_ctx *ctx,
			      struct mdp_cmdq_cmd *cmd, u32 index)
{
	phys_addr_t base = ctx->comp->reg_base;
	u8 subsys_id = ctx->comp->subsys_id;
	u32 reg = 0;

	/* Write Y pixel offset */
	if (CFG_CHECK(MT8183, p_id))
		reg = CFG_COMP(MT8183, ctx->param, wrot.subfrms[index].offset[0]);
	else if (CFG_CHECK(MT8195, p_id))
		reg = CFG_COMP(MT8195, ctx->param, wrot.subfrms[index].offset[0]);
	MM_REG_WRITE_MASK(cmd, subsys_id, base, VIDO_OFST_ADDR, reg, 0x0FFFFFFF);

	/* Write U pixel offset */
	if (CFG_CHECK(MT8183, p_id))
		reg = CFG_COMP(MT8183, ctx->param, wrot.subfrms[index].offset[1]);
	else if (CFG_CHECK(MT8195, p_id))
		reg = CFG_COMP(MT8195, ctx->param, wrot.subfrms[index].offset[1]);
	MM_REG_WRITE_MASK(cmd, subsys_id, base, VIDO_OFST_ADDR_C, reg, 0x0FFFFFFF);

	/* Write V pixel offset */
	if (CFG_CHECK(MT8183, p_id))
		reg = CFG_COMP(MT8183, ctx->param, wrot.subfrms[index].offset[2]);
	else if (CFG_CHECK(MT8195, p_id))
		reg = CFG_COMP(MT8195, ctx->param, wrot.subfrms[index].offset[2]);
	MM_REG_WRITE_MASK(cmd, subsys_id, base, VIDO_OFST_ADDR_V, reg,
			  0x0FFFFFFF);

	/* Write source size */
	if (CFG_CHECK(MT8183, p_id))
		reg = CFG_COMP(MT8183, ctx->param, wrot.subfrms[index].src);
	else if (CFG_CHECK(MT8195, p_id))
		reg = CFG_COMP(MT8195, ctx->param, wrot.subfrms[index].src);
	MM_REG_WRITE_MASK(cmd, subsys_id, base, VIDO_IN_SIZE, reg, 0x1FFF1FFF);

	/* Write target size */
	if (CFG_CHECK(MT8183, p_id))
		reg = CFG_COMP(MT8183, ctx->param, wrot.subfrms[index].clip);
	else if (CFG_CHECK(MT8195, p_id))
		reg = CFG_COMP(MT8195, ctx->param, wrot.subfrms[index].clip);
	MM_REG_WRITE_MASK(cmd, subsys_id, base, VIDO_TAR_SIZE, reg, 0x1FFF1FFF);

	if (CFG_CHECK(MT8183, p_id))
		reg = CFG_COMP(MT8183, ctx->param, wrot.subfrms[index].clip_ofst);
	else if (CFG_CHECK(MT8195, p_id))
		reg = CFG_COMP(MT8195, ctx->param, wrot.subfrms[index].clip_ofst);
	MM_REG_WRITE_MASK(cmd, subsys_id, base, VIDO_CROP_OFST, reg, 0x1FFF1FFF);

	if (CFG_CHECK(MT8183, p_id))
		reg = CFG_COMP(MT8183, ctx->param, wrot.subfrms[index].main_buf);
	else if (CFG_CHECK(MT8195, p_id))
		reg = CFG_COMP(MT8195, ctx->param, wrot.subfrms[index].main_buf);
	MM_REG_WRITE_MASK(cmd, subsys_id, base, VIDO_MAIN_BUF_SIZE, reg,
			  0x1FFF7F00);

	/* Enable WROT */
	MM_REG_WRITE_MASK(cmd, subsys_id, base, VIDO_ROT_EN, BIT(0), BIT(0));

	return 0;
}

static int wait_wrot_event(struct mdp_comp_ctx *ctx, struct mdp_cmdq_cmd *cmd)
{
	const struct mdp_platform_config *mdp_cfg = __get_plat_cfg(ctx);
	struct device *dev = &ctx->comp->mdp_dev->pdev->dev;
	phys_addr_t base = ctx->comp->reg_base;
	u8 subsys_id = ctx->comp->subsys_id;

	if (!mdp_cfg)
		return -EINVAL;

	if (ctx->comp->alias_id >= mdp_cfg->wrot_event_num) {
		dev_err(dev, "Invalid WROT event %d!\n", ctx->comp->alias_id);
		return -EINVAL;
	}

	MM_REG_WAIT(cmd, ctx->comp->gce_event[MDP_GCE_EVENT_EOF]);

	if (mdp_cfg && mdp_cfg->wrot_filter_constraint)
		MM_REG_WRITE_MASK(cmd, subsys_id, base, VIDO_MAIN_BUF_SIZE, 0x0,
				  0x77);

	/* Disable WROT */
	MM_REG_WRITE_MASK(cmd, subsys_id, base, VIDO_ROT_EN, 0x0, BIT(0));

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
	MM_REG_WRITE_MASK(cmd, subsys_id, base, WDMA_RST, BIT(0), BIT(0));
	MM_REG_POLL_MASK(cmd, subsys_id, base, WDMA_FLOW_CTRL_DBG, BIT(0), BIT(0));
	MM_REG_WRITE_MASK(cmd, subsys_id, base, WDMA_RST, 0x0, BIT(0));
	return 0;
}

static int config_wdma_frame(struct mdp_comp_ctx *ctx,
			     struct mdp_cmdq_cmd *cmd,
			     const struct v4l2_rect *compose)
{
	phys_addr_t base = ctx->comp->reg_base;
	u8 subsys_id = ctx->comp->subsys_id;
	u32 reg = 0;

	MM_REG_WRITE(cmd, subsys_id, base, WDMA_BUF_CON2, 0x10101050);

	/* Setup frame information */
	if (CFG_CHECK(MT8183, p_id))
		reg = CFG_COMP(MT8183, ctx->param, wdma.wdma_cfg);
	MM_REG_WRITE_MASK(cmd, subsys_id, base, WDMA_CFG, reg, 0x0F01B8F0);
	/* Setup frame base address */
	if (CFG_CHECK(MT8183, p_id))
		reg = CFG_COMP(MT8183, ctx->param, wdma.iova[0]);
	MM_REG_WRITE(cmd, subsys_id, base, WDMA_DST_ADDR, reg);
	if (CFG_CHECK(MT8183, p_id))
		reg = CFG_COMP(MT8183, ctx->param, wdma.iova[1]);
	MM_REG_WRITE(cmd, subsys_id, base, WDMA_DST_U_ADDR, reg);
	if (CFG_CHECK(MT8183, p_id))
		reg = CFG_COMP(MT8183, ctx->param, wdma.iova[2]);
	MM_REG_WRITE(cmd, subsys_id, base, WDMA_DST_V_ADDR, reg);
	/* Setup Y pitch */
	if (CFG_CHECK(MT8183, p_id))
		reg = CFG_COMP(MT8183, ctx->param, wdma.w_in_byte);
	MM_REG_WRITE_MASK(cmd, subsys_id, base, WDMA_DST_W_IN_BYTE, reg,
			  0x0000FFFF);
	/* Setup UV pitch */
	if (CFG_CHECK(MT8183, p_id))
		reg = CFG_COMP(MT8183, ctx->param, wdma.uv_stride);
	MM_REG_WRITE_MASK(cmd, subsys_id, base, WDMA_DST_UV_PITCH, reg,
			  0x0000FFFF);
	/* Set the fixed ALPHA as 0xFF */
	MM_REG_WRITE_MASK(cmd, subsys_id, base, WDMA_ALPHA, 0x800000FF,
			  0x800000FF);

	return 0;
}

static int config_wdma_subfrm(struct mdp_comp_ctx *ctx,
			      struct mdp_cmdq_cmd *cmd, u32 index)
{
	phys_addr_t base = ctx->comp->reg_base;
	u8 subsys_id = ctx->comp->subsys_id;
	u32 reg = 0;

	/* Write Y pixel offset */
	if (CFG_CHECK(MT8183, p_id))
		reg = CFG_COMP(MT8183, ctx->param, wdma.subfrms[index].offset[0]);
	MM_REG_WRITE_MASK(cmd, subsys_id, base, WDMA_DST_ADDR_OFFSET, reg,
			  0x0FFFFFFF);
	/* Write U pixel offset */
	if (CFG_CHECK(MT8183, p_id))
		reg = CFG_COMP(MT8183, ctx->param, wdma.subfrms[index].offset[1]);
	MM_REG_WRITE_MASK(cmd, subsys_id, base, WDMA_DST_U_ADDR_OFFSET, reg,
			  0x0FFFFFFF);
	/* Write V pixel offset */
	if (CFG_CHECK(MT8183, p_id))
		reg = CFG_COMP(MT8183, ctx->param, wdma.subfrms[index].offset[2]);
	MM_REG_WRITE_MASK(cmd, subsys_id, base, WDMA_DST_V_ADDR_OFFSET, reg,
			  0x0FFFFFFF);
	/* Write source size */
	if (CFG_CHECK(MT8183, p_id))
		reg = CFG_COMP(MT8183, ctx->param, wdma.subfrms[index].src);
	MM_REG_WRITE_MASK(cmd, subsys_id, base, WDMA_SRC_SIZE, reg, 0x3FFF3FFF);
	/* Write target size */
	if (CFG_CHECK(MT8183, p_id))
		reg = CFG_COMP(MT8183, ctx->param, wdma.subfrms[index].clip);
	MM_REG_WRITE_MASK(cmd, subsys_id, base, WDMA_CLIP_SIZE, reg, 0x3FFF3FFF);
	/* Write clip offset */
	if (CFG_CHECK(MT8183, p_id))
		reg = CFG_COMP(MT8183, ctx->param, wdma.subfrms[index].clip_ofst);
	MM_REG_WRITE_MASK(cmd, subsys_id, base, WDMA_CLIP_COORD, reg, 0x3FFF3FFF);

	/* Enable WDMA */
	MM_REG_WRITE_MASK(cmd, subsys_id, base, WDMA_EN, BIT(0), BIT(0));

	return 0;
}

static int wait_wdma_event(struct mdp_comp_ctx *ctx, struct mdp_cmdq_cmd *cmd)
{
	phys_addr_t base = ctx->comp->reg_base;
	u8 subsys_id = ctx->comp->subsys_id;

	MM_REG_WAIT(cmd, ctx->comp->gce_event[MDP_GCE_EVENT_EOF]);
	/* Disable WDMA */
	MM_REG_WRITE_MASK(cmd, subsys_id, base, WDMA_EN, 0x0, BIT(0));
	return 0;
}

static const struct mdp_comp_ops wdma_ops = {
	.get_comp_flag = get_comp_flag,
	.init_comp = init_wdma,
	.config_frame = config_wdma_frame,
	.config_subfrm = config_wdma_subfrm,
	.wait_comp_event = wait_wdma_event,
};

static int reset_luma_hist(struct mdp_comp_ctx *ctx, struct mdp_cmdq_cmd *cmd)
{
	const struct mdp_platform_config *mdp_cfg = __get_plat_cfg(ctx);
	phys_addr_t base = ctx->comp->reg_base;
	u16 subsys_id = ctx->comp->subsys_id;
	u32 hist_num, i;

	if (!mdp_cfg)
		return -EINVAL;

	hist_num = mdp_cfg->tdshp_hist_num;

	/* Reset histogram */
	for (i = 0; i <= hist_num; i++)
		MM_REG_WRITE(cmd, subsys_id, base,
			     (MDP_LUMA_HIST_INIT + (i << 2)), 0);

	if (mdp_cfg->tdshp_constrain)
		MM_REG_WRITE(cmd, subsys_id, base,
			     MDP_DC_TWO_D_W1_RESULT_INIT, 0);

	if (mdp_cfg->tdshp_contour)
		for (i = 0; i < hist_num; i++)
			MM_REG_WRITE(cmd, subsys_id, base,
				     (MDP_CONTOUR_HIST_INIT + (i << 2)), 0);

	return 0;
}

static int init_tdshp(struct mdp_comp_ctx *ctx, struct mdp_cmdq_cmd *cmd)
{
	phys_addr_t base = ctx->comp->reg_base;
	u16 subsys_id = ctx->comp->subsys_id;

	MM_REG_WRITE_MASK(cmd, subsys_id, base, MDP_TDSHP_CTRL, BIT(0), BIT(0));
	/* Enable FIFO */
	MM_REG_WRITE_MASK(cmd, subsys_id, base, MDP_TDSHP_CFG, BIT(1), BIT(1));

	return reset_luma_hist(ctx, cmd);
}

static int config_tdshp_frame(struct mdp_comp_ctx *ctx,
			      struct mdp_cmdq_cmd *cmd,
			      const struct v4l2_rect *compose)
{
	phys_addr_t base = ctx->comp->reg_base;
	u16 subsys_id = ctx->comp->subsys_id;
	u32 reg = 0;

	if (CFG_CHECK(MT8195, p_id))
		reg = CFG_COMP(MT8195, ctx->param, tdshp.cfg);
	MM_REG_WRITE_MASK(cmd, subsys_id, base, MDP_TDSHP_CFG, reg, BIT(0));

	return 0;
}

static int config_tdshp_subfrm(struct mdp_comp_ctx *ctx,
			       struct mdp_cmdq_cmd *cmd, u32 index)
{
	phys_addr_t base = ctx->comp->reg_base;
	u16 subsys_id = ctx->comp->subsys_id;
	u32 reg = 0;

	if (CFG_CHECK(MT8195, p_id))
		reg = CFG_COMP(MT8195, ctx->param, tdshp.subfrms[index].src);
	MM_REG_WRITE(cmd, subsys_id, base, MDP_TDSHP_INPUT_SIZE, reg);

	if (CFG_CHECK(MT8195, p_id))
		reg = CFG_COMP(MT8195, ctx->param, tdshp.subfrms[index].clip_ofst);
	MM_REG_WRITE_MASK(cmd, subsys_id, base, MDP_TDSHP_OUTPUT_OFFSET, reg,
			  0x00FF00FF);

	if (CFG_CHECK(MT8195, p_id))
		reg = CFG_COMP(MT8195, ctx->param, tdshp.subfrms[index].clip);
	MM_REG_WRITE(cmd, subsys_id, base, MDP_TDSHP_OUTPUT_SIZE, reg);

	if (CFG_CHECK(MT8195, p_id))
		reg = CFG_COMP(MT8195, ctx->param, tdshp.subfrms[index].hist_cfg_0);
	MM_REG_WRITE(cmd, subsys_id, base, MDP_HIST_CFG_00, reg);

	if (CFG_CHECK(MT8195, p_id))
		reg = CFG_COMP(MT8195, ctx->param, tdshp.subfrms[index].hist_cfg_1);
	MM_REG_WRITE(cmd, subsys_id, base, MDP_HIST_CFG_01, reg);

	return 0;
}

static const struct mdp_comp_ops tdshp_ops = {
	.get_comp_flag = get_comp_flag,
	.init_comp = init_tdshp,
	.config_frame = config_tdshp_frame,
	.config_subfrm = config_tdshp_subfrm,
};

static int init_color(struct mdp_comp_ctx *ctx, struct mdp_cmdq_cmd *cmd)
{
	phys_addr_t base = ctx->comp->reg_base;
	u16 subsys_id = ctx->comp->subsys_id;

	MM_REG_WRITE_MASK(cmd, subsys_id, base, MDP_COLOR_START, 0x1,
			  BIT(1) | BIT(0));
	MM_REG_WRITE(cmd, subsys_id, base, MDP_COLOR_WIN_X_MAIN, 0xFFFF0000);
	MM_REG_WRITE(cmd, subsys_id, base, MDP_COLOR_WIN_Y_MAIN, 0xFFFF0000);

	/* Reset color matrix */
	MM_REG_WRITE_MASK(cmd, subsys_id, base, MDP_COLOR_CM1_EN, 0x0, BIT(0));
	MM_REG_WRITE_MASK(cmd, subsys_id, base, MDP_COLOR_CM2_EN, 0x0, BIT(0));

	/* Enable interrupt */
	MM_REG_WRITE_MASK(cmd, subsys_id, base, MDP_COLOR_INTEN, 0x7, 0x7);

	MM_REG_WRITE_MASK(cmd, subsys_id, base, MDP_COLOR_OUT_SEL, 0x333, 0x333);

	return 0;
}

static int config_color_frame(struct mdp_comp_ctx *ctx,
			      struct mdp_cmdq_cmd *cmd,
			      const struct v4l2_rect *compose)
{
	phys_addr_t base = ctx->comp->reg_base;
	u16 subsys_id = ctx->comp->subsys_id;
	u32 reg = 0;

	if (CFG_CHECK(MT8195, p_id))
		reg = CFG_COMP(MT8195, ctx->param, color.start);
	MM_REG_WRITE(cmd, subsys_id, base, MDP_COLOR_START, reg);

	return 0;
}

static int config_color_subfrm(struct mdp_comp_ctx *ctx,
			       struct mdp_cmdq_cmd *cmd, u32 index)
{
	phys_addr_t base = ctx->comp->reg_base;
	u16 subsys_id = ctx->comp->subsys_id;
	u32 reg = 0;

	if (CFG_CHECK(MT8195, p_id))
		reg = CFG_COMP(MT8195, ctx->param, color.subfrms[index].in_hsize);
	MM_REG_WRITE_MASK(cmd, subsys_id, base, MDP_COLOR_INTERNAL_IP_WIDTH,
			  reg, 0x00003FFF);

	if (CFG_CHECK(MT8195, p_id))
		reg = CFG_COMP(MT8195, ctx->param, color.subfrms[index].in_vsize);
	MM_REG_WRITE_MASK(cmd, subsys_id, base, MDP_COLOR_INTERNAL_IP_HEIGHT,
			  reg, 0x00003FFF);

	return 0;
}

static const struct mdp_comp_ops color_ops = {
	.get_comp_flag = get_comp_flag,
	.init_comp = init_color,
	.config_frame = config_color_frame,
	.config_subfrm = config_color_subfrm,
};

static int init_ccorr(struct mdp_comp_ctx *ctx, struct mdp_cmdq_cmd *cmd)
{
	phys_addr_t base = ctx->comp->reg_base;
	u8 subsys_id = ctx->comp->subsys_id;

	/* CCORR enable */
	MM_REG_WRITE_MASK(cmd, subsys_id, base, MDP_CCORR_EN, BIT(0), BIT(0));
	/* Relay mode */
	MM_REG_WRITE_MASK(cmd, subsys_id, base, MDP_CCORR_CFG, BIT(0), BIT(0));
	return 0;
}

static int config_ccorr_subfrm(struct mdp_comp_ctx *ctx,
			       struct mdp_cmdq_cmd *cmd, u32 index)
{
	phys_addr_t base = ctx->comp->reg_base;
	u8 subsys_id = ctx->comp->subsys_id;
	u32 csf_l = 0, csf_r = 0;
	u32 csf_t = 0, csf_b = 0;
	u32 hsize, vsize;

	if (CFG_CHECK(MT8183, p_id)) {
		csf_l = CFG_COMP(MT8183, ctx->param, subfrms[index].in.left);
		csf_r = CFG_COMP(MT8183, ctx->param, subfrms[index].in.right);
		csf_t = CFG_COMP(MT8183, ctx->param, subfrms[index].in.top);
		csf_b = CFG_COMP(MT8183, ctx->param, subfrms[index].in.bottom);
	}

	hsize = csf_r - csf_l + 1;
	vsize = csf_b - csf_t + 1;
	MM_REG_WRITE_MASK(cmd, subsys_id, base, MDP_CCORR_SIZE,
			  (hsize << 16) + (vsize <<  0), 0x1FFF1FFF);
	return 0;
}

static const struct mdp_comp_ops ccorr_ops = {
	.get_comp_flag = get_comp_flag,
	.init_comp = init_ccorr,
	.config_subfrm = config_ccorr_subfrm,
};

static int init_aal(struct mdp_comp_ctx *ctx, struct mdp_cmdq_cmd *cmd)
{
	phys_addr_t base = ctx->comp->reg_base;
	u16 subsys_id = ctx->comp->subsys_id;

	/* Always set MDP_AAL enable to 1 */
	MM_REG_WRITE_MASK(cmd, subsys_id, base, MDP_AAL_EN, BIT(0), BIT(0));

	return 0;
}

static int config_aal_frame(struct mdp_comp_ctx *ctx,
			    struct mdp_cmdq_cmd *cmd,
			    const struct v4l2_rect *compose)
{
	phys_addr_t base = ctx->comp->reg_base;
	u16 subsys_id = ctx->comp->subsys_id;
	u32 reg = 0;

	if (CFG_CHECK(MT8195, p_id))
		reg = CFG_COMP(MT8195, ctx->param, aal.cfg_main);
	MM_REG_WRITE_MASK(cmd, subsys_id, base, MDP_AAL_CFG_MAIN, reg, BIT(7));

	if (CFG_CHECK(MT8195, p_id))
		reg = CFG_COMP(MT8195, ctx->param, aal.cfg);
	MM_REG_WRITE_MASK(cmd, subsys_id, base, MDP_AAL_CFG, reg, BIT(0));

	return 0;
}

static int config_aal_subfrm(struct mdp_comp_ctx *ctx,
			     struct mdp_cmdq_cmd *cmd, u32 index)
{
	phys_addr_t base = ctx->comp->reg_base;
	u16 subsys_id = ctx->comp->subsys_id;
	u32 reg = 0;

	if (CFG_CHECK(MT8195, p_id))
		reg = CFG_COMP(MT8195, ctx->param, aal.subfrms[index].src);
	MM_REG_WRITE(cmd, subsys_id, base, MDP_AAL_SIZE, reg);

	if (CFG_CHECK(MT8195, p_id))
		reg = CFG_COMP(MT8195, ctx->param, aal.subfrms[index].clip_ofst);
	MM_REG_WRITE_MASK(cmd, subsys_id, base, MDP_AAL_OUTPUT_OFFSET, reg,
			  0x00FF00FF);

	if (CFG_CHECK(MT8195, p_id))
		reg = CFG_COMP(MT8195, ctx->param, aal.subfrms[index].clip);
	MM_REG_WRITE(cmd, subsys_id, base, MDP_AAL_OUTPUT_SIZE, reg);

	return 0;
}

static const struct mdp_comp_ops aal_ops = {
	.get_comp_flag = get_comp_flag,
	.init_comp = init_aal,
	.config_frame = config_aal_frame,
	.config_subfrm = config_aal_subfrm,
};

static int init_hdr(struct mdp_comp_ctx *ctx, struct mdp_cmdq_cmd *cmd)
{
	phys_addr_t base = ctx->comp->reg_base;
	u16 subsys_id = ctx->comp->subsys_id;

	/* Always set MDP_HDR enable to 1 */
	MM_REG_WRITE_MASK(cmd, subsys_id, base, MDP_HDR_TOP, BIT(0), BIT(0));

	return 0;
}

static int config_hdr_frame(struct mdp_comp_ctx *ctx,
			    struct mdp_cmdq_cmd *cmd,
			    const struct v4l2_rect *compose)
{
	phys_addr_t base = ctx->comp->reg_base;
	u16 subsys_id = ctx->comp->subsys_id;
	u32 reg = 0;

	if (CFG_CHECK(MT8195, p_id))
		reg = CFG_COMP(MT8195, ctx->param, hdr.top);
	MM_REG_WRITE_MASK(cmd, subsys_id, base, MDP_HDR_TOP, reg, BIT(29) | BIT(28));

	if (CFG_CHECK(MT8195, p_id))
		reg = CFG_COMP(MT8195, ctx->param, hdr.relay);
	MM_REG_WRITE_MASK(cmd, subsys_id, base, MDP_HDR_RELAY, reg, BIT(0));

	return 0;
}

static int config_hdr_subfrm(struct mdp_comp_ctx *ctx,
			     struct mdp_cmdq_cmd *cmd, u32 index)
{
	phys_addr_t base = ctx->comp->reg_base;
	u16 subsys_id = ctx->comp->subsys_id;
	u32 reg = 0;

	if (CFG_CHECK(MT8195, p_id))
		reg = CFG_COMP(MT8195, ctx->param, hdr.subfrms[index].win_size);
	MM_REG_WRITE(cmd, subsys_id, base, MDP_HDR_TILE_POS, reg);

	if (CFG_CHECK(MT8195, p_id))
		reg = CFG_COMP(MT8195, ctx->param, hdr.subfrms[index].src);
	MM_REG_WRITE_MASK(cmd, subsys_id, base, MDP_HDR_SIZE_0, reg, 0x1FFF1FFF);

	if (CFG_CHECK(MT8195, p_id))
		reg = CFG_COMP(MT8195, ctx->param, hdr.subfrms[index].clip_ofst0);
	MM_REG_WRITE_MASK(cmd, subsys_id, base, MDP_HDR_SIZE_1, reg, 0x1FFF1FFF);

	if (CFG_CHECK(MT8195, p_id))
		reg = CFG_COMP(MT8195, ctx->param, hdr.subfrms[index].clip_ofst1);
	MM_REG_WRITE_MASK(cmd, subsys_id, base, MDP_HDR_SIZE_2, reg, 0x1FFF1FFF);

	if (CFG_CHECK(MT8195, p_id))
		reg = CFG_COMP(MT8195, ctx->param, hdr.subfrms[index].hist_ctrl_0);
	MM_REG_WRITE_MASK(cmd, subsys_id, base, MDP_HDR_HIST_CTRL_0, reg, 0x00003FFF);

	if (CFG_CHECK(MT8195, p_id))
		reg = CFG_COMP(MT8195, ctx->param, hdr.subfrms[index].hist_ctrl_1);
	MM_REG_WRITE_MASK(cmd, subsys_id, base, MDP_HDR_HIST_CTRL_1, reg, 0x00003FFF);

	if (CFG_CHECK(MT8195, p_id))
		reg = CFG_COMP(MT8195, ctx->param, hdr.subfrms[index].hdr_top);
	MM_REG_WRITE_MASK(cmd, subsys_id, base, MDP_HDR_TOP, reg, BIT(6) | BIT(5));

	/* Enable histogram */
	if (CFG_CHECK(MT8195, p_id))
		reg = CFG_COMP(MT8195, ctx->param, hdr.subfrms[index].hist_addr);
	MM_REG_WRITE_MASK(cmd, subsys_id, base, MDP_HDR_HIST_ADDR, reg, BIT(9));

	return 0;
}

static const struct mdp_comp_ops hdr_ops = {
	.get_comp_flag = get_comp_flag,
	.init_comp = init_hdr,
	.config_frame = config_hdr_frame,
	.config_subfrm = config_hdr_subfrm,
};

static int init_fg(struct mdp_comp_ctx *ctx, struct mdp_cmdq_cmd *cmd)
{
	phys_addr_t base = ctx->comp->reg_base;
	u16 subsys_id = ctx->comp->subsys_id;

	MM_REG_WRITE_MASK(cmd, subsys_id, base, MDP_FG_TRIGGER, BIT(2), BIT(2));
	MM_REG_WRITE_MASK(cmd, subsys_id, base, MDP_FG_TRIGGER, 0x0, BIT(2));

	return 0;
}

static int config_fg_frame(struct mdp_comp_ctx *ctx,
			   struct mdp_cmdq_cmd *cmd,
			   const struct v4l2_rect *compose)
{
	phys_addr_t base = ctx->comp->reg_base;
	u16 subsys_id = ctx->comp->subsys_id;
	u32 reg = 0;

	if (CFG_CHECK(MT8195, p_id))
		reg = CFG_COMP(MT8195, ctx->param, fg.ctrl_0);
	MM_REG_WRITE_MASK(cmd, subsys_id, base, MDP_FG_FG_CTRL_0, reg, BIT(0));

	if (CFG_CHECK(MT8195, p_id))
		reg = CFG_COMP(MT8195, ctx->param, fg.ck_en);
	MM_REG_WRITE_MASK(cmd, subsys_id, base, MDP_FG_FG_CK_EN, reg, 0x7);

	return 0;
}

static int config_fg_subfrm(struct mdp_comp_ctx *ctx,
			    struct mdp_cmdq_cmd *cmd, u32 index)
{
	phys_addr_t base = ctx->comp->reg_base;
	u16 subsys_id = ctx->comp->subsys_id;
	u32 reg = 0;

	if (CFG_CHECK(MT8195, p_id))
		reg = CFG_COMP(MT8195, ctx->param, fg.subfrms[index].info_0);
	MM_REG_WRITE(cmd, subsys_id, base, MDP_FG_TILE_INFO_0, reg);

	if (CFG_CHECK(MT8195, p_id))
		reg = CFG_COMP(MT8195, ctx->param, fg.subfrms[index].info_1);
	MM_REG_WRITE(cmd, subsys_id, base, MDP_FG_TILE_INFO_1, reg);

	return 0;
}

static const struct mdp_comp_ops fg_ops = {
	.get_comp_flag = get_comp_flag,
	.init_comp = init_fg,
	.config_frame = config_fg_frame,
	.config_subfrm = config_fg_subfrm,
};

static int init_ovl(struct mdp_comp_ctx *ctx, struct mdp_cmdq_cmd *cmd)
{
	phys_addr_t base = ctx->comp->reg_base;
	u16 subsys_id = ctx->comp->subsys_id;

	MM_REG_WRITE(cmd, subsys_id, base, MDP_OVL_EN, BIT(0));

	/* Set to relay mode */
	MM_REG_WRITE(cmd, subsys_id, base, MDP_OVL_SRC_CON, BIT(9));
	MM_REG_WRITE(cmd, subsys_id, base, MDP_OVL_DP_CON, BIT(0));

	return 0;
}

static int config_ovl_frame(struct mdp_comp_ctx *ctx,
			    struct mdp_cmdq_cmd *cmd,
			    const struct v4l2_rect *compose)
{
	phys_addr_t base = ctx->comp->reg_base;
	u16 subsys_id = ctx->comp->subsys_id;
	u32 reg = 0;

	if (CFG_CHECK(MT8195, p_id))
		reg = CFG_COMP(MT8195, ctx->param, ovl.L0_con);
	MM_REG_WRITE_MASK(cmd, subsys_id, base, MDP_OVL_L0_CON, reg, BIT(29) | BIT(28));

	if (CFG_CHECK(MT8195, p_id))
		reg = CFG_COMP(MT8195, ctx->param, ovl.src_con);
	MM_REG_WRITE_MASK(cmd, subsys_id, base, MDP_OVL_SRC_CON, reg, BIT(0));

	return 0;
}

static int config_ovl_subfrm(struct mdp_comp_ctx *ctx,
			     struct mdp_cmdq_cmd *cmd, u32 index)
{
	phys_addr_t base = ctx->comp->reg_base;
	u16 subsys_id = ctx->comp->subsys_id;
	u32 reg = 0;

	if (CFG_CHECK(MT8195, p_id))
		reg = CFG_COMP(MT8195, ctx->param, ovl.subfrms[index].L0_src_size);
	MM_REG_WRITE(cmd, subsys_id, base, MDP_OVL_L0_SRC_SIZE, reg);

	/* Setup output size */
	if (CFG_CHECK(MT8195, p_id))
		reg = CFG_COMP(MT8195, ctx->param, ovl.subfrms[index].roi_size);
	MM_REG_WRITE(cmd, subsys_id, base, MDP_OVL_ROI_SIZE, reg);

	return 0;
}

static const struct mdp_comp_ops ovl_ops = {
	.get_comp_flag = get_comp_flag,
	.init_comp = init_ovl,
	.config_frame = config_ovl_frame,
	.config_subfrm = config_ovl_subfrm,
};

static int init_pad(struct mdp_comp_ctx *ctx, struct mdp_cmdq_cmd *cmd)
{
	phys_addr_t base = ctx->comp->reg_base;
	u16 subsys_id = ctx->comp->subsys_id;

	MM_REG_WRITE(cmd, subsys_id, base, MDP_PAD_CON, BIT(1));
	/* Reset */
	MM_REG_WRITE(cmd, subsys_id, base, MDP_PAD_W_SIZE, 0);
	MM_REG_WRITE(cmd, subsys_id, base, MDP_PAD_H_SIZE, 0);

	return 0;
}

static int config_pad_subfrm(struct mdp_comp_ctx *ctx,
			     struct mdp_cmdq_cmd *cmd, u32 index)
{
	phys_addr_t base = ctx->comp->reg_base;
	u16 subsys_id = ctx->comp->subsys_id;
	u32 reg = 0;

	if (CFG_CHECK(MT8195, p_id))
		reg = CFG_COMP(MT8195, ctx->param, pad.subfrms[index].pic_size);
	MM_REG_WRITE(cmd, subsys_id, base, MDP_PAD_PIC_SIZE, reg);

	return 0;
}

static const struct mdp_comp_ops pad_ops = {
	.get_comp_flag = get_comp_flag,
	.init_comp = init_pad,
	.config_subfrm = config_pad_subfrm,
};

static const struct mdp_comp_ops *mdp_comp_ops[MDP_COMP_TYPE_COUNT] = {
	[MDP_COMP_TYPE_RDMA] =		&rdma_ops,
	[MDP_COMP_TYPE_RSZ] =		&rsz_ops,
	[MDP_COMP_TYPE_WROT] =		&wrot_ops,
	[MDP_COMP_TYPE_WDMA] =		&wdma_ops,
	[MDP_COMP_TYPE_TDSHP] =		&tdshp_ops,
	[MDP_COMP_TYPE_COLOR] =		&color_ops,
	[MDP_COMP_TYPE_CCORR] =		&ccorr_ops,
	[MDP_COMP_TYPE_AAL] =		&aal_ops,
	[MDP_COMP_TYPE_HDR] =		&hdr_ops,
	[MDP_COMP_TYPE_FG] =		&fg_ops,
	[MDP_COMP_TYPE_OVL] =		&ovl_ops,
	[MDP_COMP_TYPE_PAD] =		&pad_ops,
};

static const struct of_device_id mdp_comp_dt_ids[] __maybe_unused = {
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
	}, {
		.compatible = "mediatek,mt8195-mdp3-rdma",
		.data = (void *)MDP_COMP_TYPE_RDMA,
	}, {
		.compatible = "mediatek,mt8195-mdp3-split",
		.data = (void *)MDP_COMP_TYPE_SPLIT,
	}, {
		.compatible = "mediatek,mt8195-mdp3-stitch",
		.data = (void *)MDP_COMP_TYPE_STITCH,
	}, {
		.compatible = "mediatek,mt8195-mdp3-fg",
		.data = (void *)MDP_COMP_TYPE_FG,
	}, {
		.compatible = "mediatek,mt8195-mdp3-hdr",
		.data = (void *)MDP_COMP_TYPE_HDR,
	}, {
		.compatible = "mediatek,mt8195-mdp3-aal",
		.data = (void *)MDP_COMP_TYPE_AAL,
	}, {
		.compatible = "mediatek,mt8195-mdp3-merge",
		.data = (void *)MDP_COMP_TYPE_MERGE,
	}, {
		.compatible = "mediatek,mt8195-mdp3-tdshp",
		.data = (void *)MDP_COMP_TYPE_TDSHP,
	}, {
		.compatible = "mediatek,mt8195-mdp3-color",
		.data = (void *)MDP_COMP_TYPE_COLOR,
	}, {
		.compatible = "mediatek,mt8195-mdp3-ovl",
		.data = (void *)MDP_COMP_TYPE_OVL,
	}, {
		.compatible = "mediatek,mt8195-mdp3-padding",
		.data = (void *)MDP_COMP_TYPE_PAD,
	}, {
		.compatible = "mediatek,mt8195-mdp3-tcc",
		.data = (void *)MDP_COMP_TYPE_TCC,
	}, {
		.compatible = "mediatek,mt8188-mdp3-rdma",
		.data = (void *)MDP_COMP_TYPE_RDMA,
	},
	{}
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

static int mdp_comp_get_id(struct mdp_dev *mdp, enum mdp_comp_type type, u32 alias_id)
{
	int i;

	for (i = 0; i < mdp->mdp_data->comp_data_len; i++)
		if (mdp->mdp_data->comp_data[i].match.type == type &&
		    mdp->mdp_data->comp_data[i].match.alias_id == alias_id)
			return i;
	return -ENODEV;
}

int mdp_comp_clock_on(struct device *dev, struct mdp_comp *comp)
{
	int i, ret;

	/* Only DMA capable components need the pm control */
	if (comp->comp_dev && is_dma_capable(comp->type)) {
		ret = pm_runtime_resume_and_get(comp->comp_dev);
		if (ret < 0) {
			dev_err(dev,
				"Failed to get power, err %d. type:%d id:%d\n",
				ret, comp->type, comp->inner_id);
			return ret;
		}
	}

	for (i = 0; i < comp->clk_num; i++) {
		if (IS_ERR_OR_NULL(comp->clks[i]))
			continue;
		ret = clk_prepare_enable(comp->clks[i]);
		if (ret) {
			dev_err(dev,
				"Failed to enable clk %d. type:%d id:%d\n",
				i, comp->type, comp->inner_id);
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
	if (comp->comp_dev && is_dma_capable(comp->type))
		pm_runtime_put_sync(comp->comp_dev);

	return ret;
}

void mdp_comp_clock_off(struct device *dev, struct mdp_comp *comp)
{
	int i;

	for (i = 0; i < comp->clk_num; i++) {
		if (IS_ERR_OR_NULL(comp->clks[i]))
			continue;
		clk_disable_unprepare(comp->clks[i]);
	}

	if (comp->comp_dev && is_dma_capable(comp->type))
		pm_runtime_put(comp->comp_dev);
}

int mdp_comp_clocks_on(struct device *dev, struct mdp_comp *comps, int num)
{
	int i, ret;

	for (i = 0; i < num; i++) {
		struct mdp_dev *m = comps[i].mdp_dev;
		enum mtk_mdp_comp_id id;
		const struct mdp_comp_blend *b;

		/* Bypass the dummy component*/
		if (!m)
			continue;

		ret = mdp_comp_clock_on(dev, &comps[i]);
		if (ret)
			return ret;

		id = comps[i].public_id;
		b = &m->mdp_data->comp_data[id].blend;

		if (b && b->aid_clk) {
			ret = mdp_comp_clock_on(dev, m->comp[b->b_id]);
			if (ret)
				return ret;
		}
	}

	return 0;
}

void mdp_comp_clocks_off(struct device *dev, struct mdp_comp *comps, int num)
{
	int i;

	for (i = 0; i < num; i++) {
		struct mdp_dev *m = comps[i].mdp_dev;
		enum mtk_mdp_comp_id id;
		const struct mdp_comp_blend *b;

		/* Bypass the dummy component*/
		if (!m)
			continue;

		mdp_comp_clock_off(dev, &comps[i]);

		id = comps[i].public_id;
		b = &m->mdp_data->comp_data[id].blend;

		if (b && b->aid_clk)
			mdp_comp_clock_off(dev, m->comp[b->b_id]);
	}
}

static int mdp_get_subsys_id(struct mdp_dev *mdp, struct device *dev,
			     struct device_node *node, struct mdp_comp *comp)
{
	struct platform_device *comp_pdev;
	struct cmdq_client_reg  cmdq_reg;
	int ret = 0;
	int index = 0;

	if (!dev || !node || !comp)
		return -EINVAL;

	comp_pdev = of_find_device_by_node(node);

	if (!comp_pdev) {
		dev_err(dev, "get comp_pdev fail! comp public id=%d, inner id=%d, type=%d\n",
			comp->public_id, comp->inner_id, comp->type);
		return -ENODEV;
	}

	index = mdp->mdp_data->comp_data[comp->public_id].info.dts_reg_ofst;
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
	int index;

	index = mdp->mdp_data->comp_data[comp->public_id].info.dts_reg_ofst;
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
	struct platform_device *pdev_c;
	int clk_ofst;
	int i;
	s32 event;

	if (id < 0 || id >= MDP_MAX_COMP_COUNT) {
		dev_err(dev, "Invalid component id %d\n", id);
		return -EINVAL;
	}

	pdev_c = of_find_device_by_node(node);
	if (!pdev_c) {
		dev_warn(dev, "can't find platform device of node:%s\n",
			 node->name);
		return -ENODEV;
	}

	comp->comp_dev = &pdev_c->dev;
	comp->public_id = id;
	comp->type = mdp->mdp_data->comp_data[id].match.type;
	comp->inner_id = mdp->mdp_data->comp_data[id].match.inner_id;
	comp->alias_id = mdp->mdp_data->comp_data[id].match.alias_id;
	comp->ops = mdp_comp_ops[comp->type];
	__mdp_comp_init(mdp, node, comp);

	comp->clk_num = mdp->mdp_data->comp_data[id].info.clk_num;
	comp->clks = devm_kzalloc(dev, sizeof(struct clk *) * comp->clk_num,
				  GFP_KERNEL);
	if (!comp->clks)
		return -ENOMEM;

	clk_ofst = mdp->mdp_data->comp_data[id].info.clk_ofst;

	for (i = 0; i < comp->clk_num; i++) {
		comp->clks[i] = of_clk_get(node, i + clk_ofst);
		if (IS_ERR(comp->clks[i]))
			break;
	}

	mdp_get_subsys_id(mdp, dev, node, comp);

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

	if (comp->comp_dev && comp->clks) {
		devm_kfree(&comp->mdp_dev->pdev->dev, comp->clks);
		comp->clks = NULL;
	}

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

	dev_dbg(dev, "%s type:%d alias:%d public id:%d inner id:%d base:%#x regs:%p\n",
		dev->of_node->name, comp->type, comp->alias_id, id, comp->inner_id,
		(u32)comp->reg_base, comp->regs);
	return comp;
}

static int mdp_comp_sub_create(struct mdp_dev *mdp)
{
	struct device *dev = &mdp->pdev->dev;
	struct device_node *node, *parent;
	int ret = 0;

	parent = dev->of_node->parent;

	for_each_child_of_node(parent, node) {
		const struct of_device_id *of_id;
		enum mdp_comp_type type;
		int id, alias_id;
		struct mdp_comp *comp;

		of_id = of_match_node(mdp->mdp_data->mdp_sub_comp_dt_ids, node);
		if (!of_id)
			continue;
		if (!of_device_is_available(node)) {
			dev_dbg(dev, "Skipping disabled sub comp. %pOF\n",
				node);
			continue;
		}

		type = (enum mdp_comp_type)(uintptr_t)of_id->data;
		alias_id = mdp_comp_alias_id[type];
		id = mdp_comp_get_id(mdp, type, alias_id);
		if (id < 0) {
			dev_err(dev,
				"Fail to get sub comp. id: type %d alias %d\n",
				type, alias_id);
			ret = -EINVAL;
			goto err_free_node;
		}
		mdp_comp_alias_id[type]++;

		comp = mdp_comp_create(mdp, node, id);
		if (IS_ERR(comp)) {
			ret = PTR_ERR(comp);
			goto err_free_node;
		}
	}
	return ret;

err_free_node:
	of_node_put(node);
	return ret;
}

void mdp_comp_destroy(struct mdp_dev *mdp)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(mdp->comp); i++) {
		if (mdp->comp[i]) {
			if (is_dma_capable(mdp->comp[i]->type))
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
	int ret;

	memset(mdp_comp_alias_id, 0, sizeof(mdp_comp_alias_id));
	p_id = mdp->mdp_data->mdp_plat_id;

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
		id = mdp_comp_get_id(mdp, type, alias_id);
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
			of_node_put(node);
			goto err_init_comps;
		}

		/* Only DMA capable components need the pm control */
		if (!is_dma_capable(comp->type))
			continue;
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
	enum mtk_mdp_comp_id public_id = MDP_COMP_NONE;
	u32 arg;
	int i, idx;

	if (!param) {
		dev_err(dev, "Invalid component param");
		return -EINVAL;
	}

	if (CFG_CHECK(MT8183, p_id))
		arg = CFG_COMP(MT8183, param, type);
	else if (CFG_CHECK(MT8195, p_id))
		arg = CFG_COMP(MT8195, param, type);
	else
		return -EINVAL;
	public_id = mdp_cfg_get_id_public(mdp, arg);
	if (public_id < 0) {
		dev_err(dev, "Invalid component id %d", public_id);
		return -EINVAL;
	}

	ctx->comp = mdp->comp[public_id];
	if (!ctx->comp) {
		dev_err(dev, "Uninit component inner id %d", arg);
		return -EINVAL;
	}

	ctx->param = param;
	if (CFG_CHECK(MT8183, p_id))
		arg = CFG_COMP(MT8183, param, input);
	else if (CFG_CHECK(MT8195, p_id))
		arg = CFG_COMP(MT8195, param, input);
	else
		return -EINVAL;
	ctx->input = &frame->inputs[arg];
	if (CFG_CHECK(MT8183, p_id))
		idx = CFG_COMP(MT8183, param, num_outputs);
	else if (CFG_CHECK(MT8195, p_id))
		idx = CFG_COMP(MT8195, param, num_outputs);
	else
		return -EINVAL;
	for (i = 0; i < idx; i++) {
		if (CFG_CHECK(MT8183, p_id))
			arg = CFG_COMP(MT8183, param, outputs[i]);
		else if (CFG_CHECK(MT8195, p_id))
			arg = CFG_COMP(MT8195, param, outputs[i]);
		else
			return -EINVAL;
		ctx->outputs[i] = &frame->outputs[arg];
	}
	return 0;
}
