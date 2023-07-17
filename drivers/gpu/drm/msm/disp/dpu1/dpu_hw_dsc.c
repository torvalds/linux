// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020-2022, Linaro Limited
 */

#include "dpu_kms.h"
#include "dpu_hw_catalog.h"
#include "dpu_hwio.h"
#include "dpu_hw_mdss.h"
#include "dpu_hw_dsc.h"

#define DSC_COMMON_MODE                 0x000
#define DSC_ENC                         0x004
#define DSC_PICTURE                     0x008
#define DSC_SLICE                       0x00C
#define DSC_CHUNK_SIZE                  0x010
#define DSC_DELAY                       0x014
#define DSC_SCALE_INITIAL               0x018
#define DSC_SCALE_DEC_INTERVAL          0x01C
#define DSC_SCALE_INC_INTERVAL          0x020
#define DSC_FIRST_LINE_BPG_OFFSET       0x024
#define DSC_BPG_OFFSET                  0x028
#define DSC_DSC_OFFSET                  0x02C
#define DSC_FLATNESS                    0x030
#define DSC_RC_MODEL_SIZE               0x034
#define DSC_RC                          0x038
#define DSC_RC_BUF_THRESH               0x03C
#define DSC_RANGE_MIN_QP                0x074
#define DSC_RANGE_MAX_QP                0x0B0
#define DSC_RANGE_BPG_OFFSET            0x0EC

#define DSC_CTL(m) (0x1800 - 0x3FC * (m - DSC_0))

static void dpu_hw_dsc_disable(struct dpu_hw_dsc *dsc)
{
	struct dpu_hw_blk_reg_map *c = &dsc->hw;

	DPU_REG_WRITE(c, DSC_COMMON_MODE, 0);
}

static void dpu_hw_dsc_config(struct dpu_hw_dsc *hw_dsc,
			      struct drm_dsc_config *dsc,
			      u32 mode,
			      u32 initial_lines)
{
	struct dpu_hw_blk_reg_map *c = &hw_dsc->hw;
	u32 data;
	u32 slice_last_group_size;
	u32 det_thresh_flatness;
	bool is_cmd_mode = !(mode & DSC_MODE_VIDEO);

	DPU_REG_WRITE(c, DSC_COMMON_MODE, mode);

	if (is_cmd_mode)
		initial_lines += 1;

	slice_last_group_size = 3 - (dsc->slice_width % 3);
	data = (initial_lines << 20);
	data |= ((slice_last_group_size - 1) << 18);
	/* bpp is 6.4 format, 4 LSBs bits are for fractional part */
	data |= (dsc->bits_per_pixel << 8);
	data |= (dsc->block_pred_enable << 7);
	data |= (dsc->line_buf_depth << 3);
	data |= (dsc->simple_422 << 2);
	data |= (dsc->convert_rgb << 1);
	data |= dsc->bits_per_component;

	DPU_REG_WRITE(c, DSC_ENC, data);

	data = dsc->pic_width << 16;
	data |= dsc->pic_height;
	DPU_REG_WRITE(c, DSC_PICTURE, data);

	data = dsc->slice_width << 16;
	data |= dsc->slice_height;
	DPU_REG_WRITE(c, DSC_SLICE, data);

	data = dsc->slice_chunk_size << 16;
	DPU_REG_WRITE(c, DSC_CHUNK_SIZE, data);

	data = dsc->initial_dec_delay << 16;
	data |= dsc->initial_xmit_delay;
	DPU_REG_WRITE(c, DSC_DELAY, data);

	data = dsc->initial_scale_value;
	DPU_REG_WRITE(c, DSC_SCALE_INITIAL, data);

	data = dsc->scale_decrement_interval;
	DPU_REG_WRITE(c, DSC_SCALE_DEC_INTERVAL, data);

	data = dsc->scale_increment_interval;
	DPU_REG_WRITE(c, DSC_SCALE_INC_INTERVAL, data);

	data = dsc->first_line_bpg_offset;
	DPU_REG_WRITE(c, DSC_FIRST_LINE_BPG_OFFSET, data);

	data = dsc->nfl_bpg_offset << 16;
	data |= dsc->slice_bpg_offset;
	DPU_REG_WRITE(c, DSC_BPG_OFFSET, data);

	data = dsc->initial_offset << 16;
	data |= dsc->final_offset;
	DPU_REG_WRITE(c, DSC_DSC_OFFSET, data);

	det_thresh_flatness = 7 + 2 * (dsc->bits_per_component - 8);
	data = det_thresh_flatness << 10;
	data |= dsc->flatness_max_qp << 5;
	data |= dsc->flatness_min_qp;
	DPU_REG_WRITE(c, DSC_FLATNESS, data);

	data = dsc->rc_model_size;
	DPU_REG_WRITE(c, DSC_RC_MODEL_SIZE, data);

	data = dsc->rc_tgt_offset_low << 18;
	data |= dsc->rc_tgt_offset_high << 14;
	data |= dsc->rc_quant_incr_limit1 << 9;
	data |= dsc->rc_quant_incr_limit0 << 4;
	data |= dsc->rc_edge_factor;
	DPU_REG_WRITE(c, DSC_RC, data);
}

static void dpu_hw_dsc_config_thresh(struct dpu_hw_dsc *hw_dsc,
				     struct drm_dsc_config *dsc)
{
	struct drm_dsc_rc_range_parameters *rc = dsc->rc_range_params;
	struct dpu_hw_blk_reg_map *c = &hw_dsc->hw;
	u32 off;
	int i;

	off = DSC_RC_BUF_THRESH;
	for (i = 0; i < DSC_NUM_BUF_RANGES - 1 ; i++) {
		DPU_REG_WRITE(c, off, dsc->rc_buf_thresh[i]);
		off += 4;
	}

	off = DSC_RANGE_MIN_QP;
	for (i = 0; i < DSC_NUM_BUF_RANGES; i++) {
		DPU_REG_WRITE(c, off, rc[i].range_min_qp);
		off += 4;
	}

	off = DSC_RANGE_MAX_QP;
	for (i = 0; i < 15; i++) {
		DPU_REG_WRITE(c, off, rc[i].range_max_qp);
		off += 4;
	}

	off = DSC_RANGE_BPG_OFFSET;
	for (i = 0; i < 15; i++) {
		DPU_REG_WRITE(c, off, rc[i].range_bpg_offset);
		off += 4;
	}
}

static void dpu_hw_dsc_bind_pingpong_blk(
		struct dpu_hw_dsc *hw_dsc,
		bool enable,
		const enum dpu_pingpong pp)
{
	struct dpu_hw_blk_reg_map *c = &hw_dsc->hw;
	int mux_cfg = 0xF;
	u32 dsc_ctl_offset;

	dsc_ctl_offset = DSC_CTL(hw_dsc->idx);

	if (enable)
		mux_cfg = (pp - PINGPONG_0) & 0x7;

	DRM_DEBUG_KMS("%s dsc:%d %s pp:%d\n",
			enable ? "Binding" : "Unbinding",
			hw_dsc->idx - DSC_0,
			enable ? "to" : "from",
			pp - PINGPONG_0);

	DPU_REG_WRITE(c, dsc_ctl_offset, mux_cfg);
}

static const struct dpu_dsc_cfg *_dsc_offset(enum dpu_dsc dsc,
				       const struct dpu_mdss_cfg *m,
				       void __iomem *addr,
				       struct dpu_hw_blk_reg_map *b)
{
	int i;

	for (i = 0; i < m->dsc_count; i++) {
		if (dsc == m->dsc[i].id) {
			b->blk_addr = addr + m->dsc[i].base;
			b->log_mask = DPU_DBG_MASK_DSC;
			return &m->dsc[i];
		}
	}

	return NULL;
}

static void _setup_dsc_ops(struct dpu_hw_dsc_ops *ops,
			   unsigned long cap)
{
	ops->dsc_disable = dpu_hw_dsc_disable;
	ops->dsc_config = dpu_hw_dsc_config;
	ops->dsc_config_thresh = dpu_hw_dsc_config_thresh;
	if (cap & BIT(DPU_DSC_OUTPUT_CTRL))
		ops->dsc_bind_pingpong_blk = dpu_hw_dsc_bind_pingpong_blk;
};

struct dpu_hw_dsc *dpu_hw_dsc_init(enum dpu_dsc idx, void __iomem *addr,
				   const struct dpu_mdss_cfg *m)
{
	struct dpu_hw_dsc *c;
	const struct dpu_dsc_cfg *cfg;

	c = kzalloc(sizeof(*c), GFP_KERNEL);
	if (!c)
		return ERR_PTR(-ENOMEM);

	cfg = _dsc_offset(idx, m, addr, &c->hw);
	if (IS_ERR_OR_NULL(cfg)) {
		kfree(c);
		return ERR_PTR(-EINVAL);
	}

	c->idx = idx;
	c->caps = cfg;
	_setup_dsc_ops(&c->ops, c->caps->features);

	return c;
}

void dpu_hw_dsc_destroy(struct dpu_hw_dsc *dsc)
{
	kfree(dsc);
}
