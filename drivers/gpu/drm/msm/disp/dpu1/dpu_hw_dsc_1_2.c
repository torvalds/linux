// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved
 */

#include <drm/display/drm_dsc_helper.h>

#include "dpu_kms.h"
#include "dpu_hw_catalog.h"
#include "dpu_hwio.h"
#include "dpu_hw_mdss.h"
#include "dpu_hw_dsc.h"

#define DSC_CMN_MAIN_CNF           0x00

/* DPU_DSC_ENC register offsets */
#define ENC_DF_CTRL                0x00
#define ENC_GENERAL_STATUS         0x04
#define ENC_HSLICE_STATUS          0x08
#define ENC_OUT_STATUS             0x0C
#define ENC_INT_STAT               0x10
#define ENC_INT_CLR                0x14
#define ENC_INT_MASK               0x18
#define DSC_MAIN_CONF              0x30
#define DSC_PICTURE_SIZE           0x34
#define DSC_SLICE_SIZE             0x38
#define DSC_MISC_SIZE              0x3C
#define DSC_HRD_DELAYS             0x40
#define DSC_RC_SCALE               0x44
#define DSC_RC_SCALE_INC_DEC       0x48
#define DSC_RC_OFFSETS_1           0x4C
#define DSC_RC_OFFSETS_2           0x50
#define DSC_RC_OFFSETS_3           0x54
#define DSC_RC_OFFSETS_4           0x58
#define DSC_FLATNESS_QP            0x5C
#define DSC_RC_MODEL_SIZE          0x60
#define DSC_RC_CONFIG              0x64
#define DSC_RC_BUF_THRESH_0        0x68
#define DSC_RC_BUF_THRESH_1        0x6C
#define DSC_RC_BUF_THRESH_2        0x70
#define DSC_RC_BUF_THRESH_3        0x74
#define DSC_RC_MIN_QP_0            0x78
#define DSC_RC_MIN_QP_1            0x7C
#define DSC_RC_MIN_QP_2            0x80
#define DSC_RC_MAX_QP_0            0x84
#define DSC_RC_MAX_QP_1            0x88
#define DSC_RC_MAX_QP_2            0x8C
#define DSC_RC_RANGE_BPG_OFFSETS_0 0x90
#define DSC_RC_RANGE_BPG_OFFSETS_1 0x94
#define DSC_RC_RANGE_BPG_OFFSETS_2 0x98

/* DPU_DSC_CTL register offsets */
#define DSC_CTL                    0x00
#define DSC_CFG                    0x04
#define DSC_DATA_IN_SWAP           0x08
#define DSC_CLK_CTRL               0x0C

static int _dsc_calc_output_buf_max_addr(struct dpu_hw_dsc *hw_dsc, int num_softslice)
{
	int max_addr = 2400 / num_softslice;

	if (hw_dsc->caps->features & BIT(DPU_DSC_NATIVE_42x_EN))
		max_addr /= 2;

	return max_addr - 1;
};

static void dpu_hw_dsc_disable_1_2(struct dpu_hw_dsc *hw_dsc)
{
	struct dpu_hw_blk_reg_map *hw;
	const struct dpu_dsc_sub_blks *sblk;

	if (!hw_dsc)
		return;

	hw = &hw_dsc->hw;
	sblk = hw_dsc->caps->sblk;
	DPU_REG_WRITE(hw, sblk->ctl.base + DSC_CFG, 0);

	DPU_REG_WRITE(hw, sblk->enc.base + ENC_DF_CTRL, 0);
	DPU_REG_WRITE(hw, sblk->enc.base + DSC_MAIN_CONF, 0);
}

static void dpu_hw_dsc_config_1_2(struct dpu_hw_dsc *hw_dsc,
				  struct drm_dsc_config *dsc,
				  u32 mode,
				  u32 initial_lines)
{
	struct dpu_hw_blk_reg_map *hw;
	const struct dpu_dsc_sub_blks *sblk;
	u32 data = 0;
	u32 det_thresh_flatness;
	u32 num_active_slice_per_enc;
	u32 bpp;

	if (!hw_dsc || !dsc)
		return;

	hw = &hw_dsc->hw;

	sblk = hw_dsc->caps->sblk;

	if (mode & DSC_MODE_SPLIT_PANEL)
		data |= BIT(0);

	if (mode & DSC_MODE_MULTIPLEX)
		data |= BIT(1);

	num_active_slice_per_enc = dsc->slice_count;
	if (mode & DSC_MODE_MULTIPLEX)
		num_active_slice_per_enc = dsc->slice_count / 2;

	data |= (num_active_slice_per_enc & 0x3) << 7;

	DPU_REG_WRITE(hw, DSC_CMN_MAIN_CNF, data);

	data = (initial_lines & 0xff);

	if (mode & DSC_MODE_VIDEO)
		data |= BIT(9);

	data |= (_dsc_calc_output_buf_max_addr(hw_dsc, num_active_slice_per_enc) << 18);

	DPU_REG_WRITE(hw, sblk->enc.base + ENC_DF_CTRL, data);

	data = (dsc->dsc_version_minor & 0xf) << 28;
	if (dsc->dsc_version_minor == 0x2) {
		if (dsc->native_422)
			data |= BIT(22);
		if (dsc->native_420)
			data |= BIT(21);
	}

	bpp = dsc->bits_per_pixel;
	/* as per hw requirement bpp should be programmed
	 * twice the actual value in case of 420 or 422 encoding
	 */
	if (dsc->native_422 || dsc->native_420)
		bpp = 2 * bpp;

	data |= bpp << 10;

	if (dsc->block_pred_enable)
		data |= BIT(20);

	if (dsc->convert_rgb)
		data |= BIT(4);

	data |= (dsc->line_buf_depth & 0xf) << 6;
	data |= dsc->bits_per_component & 0xf;

	DPU_REG_WRITE(hw, sblk->enc.base + DSC_MAIN_CONF, data);

	data = (dsc->pic_width & 0xffff) |
		((dsc->pic_height & 0xffff) << 16);

	DPU_REG_WRITE(hw, sblk->enc.base + DSC_PICTURE_SIZE, data);

	data = (dsc->slice_width & 0xffff) |
		((dsc->slice_height & 0xffff) << 16);

	DPU_REG_WRITE(hw, sblk->enc.base + DSC_SLICE_SIZE, data);

	DPU_REG_WRITE(hw, sblk->enc.base + DSC_MISC_SIZE,
		      (dsc->slice_chunk_size) & 0xffff);

	data = (dsc->initial_xmit_delay & 0xffff) |
		((dsc->initial_dec_delay & 0xffff) << 16);

	DPU_REG_WRITE(hw, sblk->enc.base + DSC_HRD_DELAYS, data);

	DPU_REG_WRITE(hw, sblk->enc.base + DSC_RC_SCALE,
		      dsc->initial_scale_value & 0x3f);

	data = (dsc->scale_increment_interval & 0xffff) |
		((dsc->scale_decrement_interval & 0x7ff) << 16);

	DPU_REG_WRITE(hw, sblk->enc.base + DSC_RC_SCALE_INC_DEC, data);

	data = (dsc->first_line_bpg_offset & 0x1f) |
		((dsc->second_line_bpg_offset & 0x1f) << 5);

	DPU_REG_WRITE(hw, sblk->enc.base + DSC_RC_OFFSETS_1, data);

	data = (dsc->nfl_bpg_offset & 0xffff) |
		((dsc->slice_bpg_offset & 0xffff) << 16);

	DPU_REG_WRITE(hw, sblk->enc.base + DSC_RC_OFFSETS_2, data);

	data = (dsc->initial_offset & 0xffff) |
		((dsc->final_offset & 0xffff) << 16);

	DPU_REG_WRITE(hw, sblk->enc.base + DSC_RC_OFFSETS_3, data);

	data = (dsc->nsl_bpg_offset & 0xffff) |
		((dsc->second_line_offset_adj & 0xffff) << 16);

	DPU_REG_WRITE(hw, sblk->enc.base + DSC_RC_OFFSETS_4, data);

	det_thresh_flatness = drm_dsc_flatness_det_thresh(dsc);
	data = (dsc->flatness_min_qp & 0x1f) |
		((dsc->flatness_max_qp & 0x1f) << 5) |
		((det_thresh_flatness & 0xff) << 10);

	DPU_REG_WRITE(hw, sblk->enc.base + DSC_FLATNESS_QP, data);

	DPU_REG_WRITE(hw, sblk->enc.base + DSC_RC_MODEL_SIZE,
		      (dsc->rc_model_size) & 0xffff);

	data = dsc->rc_edge_factor & 0xf;
	data |= (dsc->rc_quant_incr_limit0 & 0x1f) << 8;
	data |= (dsc->rc_quant_incr_limit1 & 0x1f) << 13;
	data |= (dsc->rc_tgt_offset_high & 0xf) << 20;
	data |= (dsc->rc_tgt_offset_low & 0xf) << 24;

	DPU_REG_WRITE(hw, sblk->enc.base + DSC_RC_CONFIG, data);

	/* program the dsc wrapper */
	data = BIT(0); /* encoder enable */
	if (dsc->native_422)
		data |= BIT(8);
	else if (dsc->native_420)
		data |= BIT(9);
	if (!dsc->convert_rgb)
		data |= BIT(10);
	if (dsc->bits_per_component == 8)
		data |= BIT(11);
	if (mode & DSC_MODE_SPLIT_PANEL)
		data |= BIT(12);
	if (mode & DSC_MODE_MULTIPLEX)
		data |= BIT(13);
	if (!(mode & DSC_MODE_VIDEO))
		data |= BIT(17);

	DPU_REG_WRITE(hw, sblk->ctl.base + DSC_CFG, data);
}

static void dpu_hw_dsc_config_thresh_1_2(struct dpu_hw_dsc *hw_dsc,
					 struct drm_dsc_config *dsc)
{
	struct dpu_hw_blk_reg_map *hw;
	const struct dpu_dsc_sub_blks *sblk;
	struct drm_dsc_rc_range_parameters *rc;

	if (!hw_dsc || !dsc)
		return;

	hw = &hw_dsc->hw;

	sblk = hw_dsc->caps->sblk;

	rc = dsc->rc_range_params;

	/*
	 * With BUF_THRESH -- 14 in total
	 * each register contains 4 thresh values with the last register
	 * containing only 2 thresh values
	 */
	DPU_REG_WRITE(hw, sblk->enc.base + DSC_RC_BUF_THRESH_0,
		      (dsc->rc_buf_thresh[0] << 0) |
		      (dsc->rc_buf_thresh[1] << 8) |
		      (dsc->rc_buf_thresh[2] << 16) |
		      (dsc->rc_buf_thresh[3] << 24));
	DPU_REG_WRITE(hw, sblk->enc.base + DSC_RC_BUF_THRESH_1,
		      (dsc->rc_buf_thresh[4] << 0) |
		      (dsc->rc_buf_thresh[5] << 8) |
		      (dsc->rc_buf_thresh[6] << 16) |
		      (dsc->rc_buf_thresh[7] << 24));
	DPU_REG_WRITE(hw, sblk->enc.base + DSC_RC_BUF_THRESH_2,
		      (dsc->rc_buf_thresh[8] << 0) |
		      (dsc->rc_buf_thresh[9] << 8) |
		      (dsc->rc_buf_thresh[10] << 16) |
		      (dsc->rc_buf_thresh[11] << 24));
	DPU_REG_WRITE(hw, sblk->enc.base + DSC_RC_BUF_THRESH_3,
		      (dsc->rc_buf_thresh[12] << 0) |
		      (dsc->rc_buf_thresh[13] << 8));

	/*
	 * with min/max_QP -- 5 bits
	 * each register contains 5 min_qp or max_qp for total of 15
	 *
	 * With BPG_OFFSET -- 6 bits
	 * each register contains 5 BPG_offset for total of 15
	 */
	DPU_REG_WRITE(hw, sblk->enc.base + DSC_RC_MIN_QP_0,
		      (rc[0].range_min_qp << 0) |
		      (rc[1].range_min_qp << 5) |
		      (rc[2].range_min_qp << 10) |
		      (rc[3].range_min_qp << 15) |
		      (rc[4].range_min_qp << 20));
	DPU_REG_WRITE(hw, sblk->enc.base + DSC_RC_MAX_QP_0,
		      (rc[0].range_max_qp << 0) |
		      (rc[1].range_max_qp << 5) |
		      (rc[2].range_max_qp << 10) |
		      (rc[3].range_max_qp << 15) |
		      (rc[4].range_max_qp << 20));
	DPU_REG_WRITE(hw, sblk->enc.base + DSC_RC_RANGE_BPG_OFFSETS_0,
		      (rc[0].range_bpg_offset << 0) |
		      (rc[1].range_bpg_offset << 6) |
		      (rc[2].range_bpg_offset << 12) |
		      (rc[3].range_bpg_offset << 18) |
		      (rc[4].range_bpg_offset << 24));

	DPU_REG_WRITE(hw, sblk->enc.base + DSC_RC_MIN_QP_1,
		      (rc[5].range_min_qp << 0) |
		      (rc[6].range_min_qp << 5) |
		      (rc[7].range_min_qp << 10) |
		      (rc[8].range_min_qp << 15) |
		      (rc[9].range_min_qp << 20));
	DPU_REG_WRITE(hw, sblk->enc.base + DSC_RC_MAX_QP_1,
		      (rc[5].range_max_qp << 0) |
		      (rc[6].range_max_qp << 5) |
		      (rc[7].range_max_qp << 10) |
		      (rc[8].range_max_qp << 15) |
		      (rc[9].range_max_qp << 20));
	DPU_REG_WRITE(hw, sblk->enc.base + DSC_RC_RANGE_BPG_OFFSETS_1,
		      (rc[5].range_bpg_offset << 0) |
		      (rc[6].range_bpg_offset << 6) |
		      (rc[7].range_bpg_offset << 12) |
		      (rc[8].range_bpg_offset << 18) |
		      (rc[9].range_bpg_offset << 24));

	DPU_REG_WRITE(hw, sblk->enc.base + DSC_RC_MIN_QP_2,
		      (rc[10].range_min_qp << 0) |
		      (rc[11].range_min_qp << 5) |
		      (rc[12].range_min_qp << 10) |
		      (rc[13].range_min_qp << 15) |
		      (rc[14].range_min_qp << 20));
	DPU_REG_WRITE(hw, sblk->enc.base + DSC_RC_MAX_QP_2,
		      (rc[10].range_max_qp << 0) |
		      (rc[11].range_max_qp << 5) |
		      (rc[12].range_max_qp << 10) |
		      (rc[13].range_max_qp << 15) |
		      (rc[14].range_max_qp << 20));
	DPU_REG_WRITE(hw, sblk->enc.base + DSC_RC_RANGE_BPG_OFFSETS_2,
		      (rc[10].range_bpg_offset << 0) |
		      (rc[11].range_bpg_offset << 6) |
		      (rc[12].range_bpg_offset << 12) |
		      (rc[13].range_bpg_offset << 18) |
		      (rc[14].range_bpg_offset << 24));
}

static void dpu_hw_dsc_bind_pingpong_blk_1_2(struct dpu_hw_dsc *hw_dsc,
					     const enum dpu_pingpong pp)
{
	struct dpu_hw_blk_reg_map *hw;
	const struct dpu_dsc_sub_blks *sblk;
	int mux_cfg = 0xf; /* Disabled */

	hw = &hw_dsc->hw;

	sblk = hw_dsc->caps->sblk;

	if (pp)
		mux_cfg = (pp - PINGPONG_0) & 0x7;

	DPU_REG_WRITE(hw, sblk->ctl.base + DSC_CTL, mux_cfg);
}

static void _setup_dcs_ops_1_2(struct dpu_hw_dsc_ops *ops,
			       const unsigned long features)
{
	ops->dsc_disable = dpu_hw_dsc_disable_1_2;
	ops->dsc_config = dpu_hw_dsc_config_1_2;
	ops->dsc_config_thresh = dpu_hw_dsc_config_thresh_1_2;
	ops->dsc_bind_pingpong_blk = dpu_hw_dsc_bind_pingpong_blk_1_2;
}

struct dpu_hw_dsc *dpu_hw_dsc_init_1_2(const struct dpu_dsc_cfg *cfg,
				       void __iomem *addr)
{
	struct dpu_hw_dsc *c;

	c = kzalloc(sizeof(*c), GFP_KERNEL);
	if (!c)
		return ERR_PTR(-ENOMEM);

	c->hw.blk_addr = addr + cfg->base;
	c->hw.log_mask = DPU_DBG_MASK_DSC;

	c->idx = cfg->id;
	c->caps = cfg;
	_setup_dcs_ops_1_2(&c->ops, c->caps->features);

	return c;
}
