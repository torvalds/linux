// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * Copyright (c) 2015-2018, The Linux Foundation. All rights reserved.
 */
#define pr_fmt(fmt)	"[drm:%s:%d] " fmt, __func__, __LINE__

#include "msm_drv.h"
#include "dpu_kms.h"
#include "dpu_hw_mdss.h"
#include "dpu_hw_util.h"

/* using a file static variables for debugfs access */
static u32 dpu_hw_util_log_mask = DPU_DBG_MASK_NONE;

/* DPU_SCALER_QSEED3 */
#define QSEED3_HW_VERSION                  0x00
#define QSEED3_OP_MODE                     0x04
#define QSEED3_RGB2Y_COEFF                 0x08
#define QSEED3_PHASE_INIT                  0x0C
#define QSEED3_PHASE_STEP_Y_H              0x10
#define QSEED3_PHASE_STEP_Y_V              0x14
#define QSEED3_PHASE_STEP_UV_H             0x18
#define QSEED3_PHASE_STEP_UV_V             0x1C
#define QSEED3_PRELOAD                     0x20
#define QSEED3_DE_SHARPEN                  0x24
#define QSEED3_DE_SHARPEN_CTL              0x28
#define QSEED3_DE_SHAPE_CTL                0x2C
#define QSEED3_DE_THRESHOLD                0x30
#define QSEED3_DE_ADJUST_DATA_0            0x34
#define QSEED3_DE_ADJUST_DATA_1            0x38
#define QSEED3_DE_ADJUST_DATA_2            0x3C
#define QSEED3_SRC_SIZE_Y_RGB_A            0x40
#define QSEED3_SRC_SIZE_UV                 0x44
#define QSEED3_DST_SIZE                    0x48
#define QSEED3_COEF_LUT_CTRL               0x4C
#define QSEED3_COEF_LUT_SWAP_BIT           0
#define QSEED3_COEF_LUT_DIR_BIT            1
#define QSEED3_COEF_LUT_Y_CIR_BIT          2
#define QSEED3_COEF_LUT_UV_CIR_BIT         3
#define QSEED3_COEF_LUT_Y_SEP_BIT          4
#define QSEED3_COEF_LUT_UV_SEP_BIT         5
#define QSEED3_BUFFER_CTRL                 0x50
#define QSEED3_CLK_CTRL0                   0x54
#define QSEED3_CLK_CTRL1                   0x58
#define QSEED3_CLK_STATUS                  0x5C
#define QSEED3_PHASE_INIT_Y_H              0x90
#define QSEED3_PHASE_INIT_Y_V              0x94
#define QSEED3_PHASE_INIT_UV_H             0x98
#define QSEED3_PHASE_INIT_UV_V             0x9C
#define QSEED3_COEF_LUT                    0x100
#define QSEED3_FILTERS                     5
#define QSEED3_LUT_REGIONS                 4
#define QSEED3_CIRCULAR_LUTS               9
#define QSEED3_SEPARABLE_LUTS              10
#define QSEED3_LUT_SIZE                    60
#define QSEED3_ENABLE                      2
#define QSEED3_DIR_LUT_SIZE                (200 * sizeof(u32))
#define QSEED3_CIR_LUT_SIZE \
	(QSEED3_LUT_SIZE * QSEED3_CIRCULAR_LUTS * sizeof(u32))
#define QSEED3_SEP_LUT_SIZE \
	(QSEED3_LUT_SIZE * QSEED3_SEPARABLE_LUTS * sizeof(u32))

/* DPU_SCALER_QSEED3LITE */
#define QSEED3LITE_COEF_LUT_Y_SEP_BIT         4
#define QSEED3LITE_COEF_LUT_UV_SEP_BIT        5
#define QSEED3LITE_COEF_LUT_CTRL              0x4C
#define QSEED3LITE_COEF_LUT_SWAP_BIT          0
#define QSEED3LITE_DIR_FILTER_WEIGHT          0x60
#define QSEED3LITE_FILTERS                 2
#define QSEED3LITE_SEPARABLE_LUTS             10
#define QSEED3LITE_LUT_SIZE                   33
#define QSEED3LITE_SEP_LUT_SIZE \
	        (QSEED3LITE_LUT_SIZE * QSEED3LITE_SEPARABLE_LUTS * sizeof(u32))

/* QOS_LUT */
#define QOS_DANGER_LUT                    0x00
#define QOS_SAFE_LUT                      0x04
#define QOS_CREQ_LUT                      0x08
#define QOS_QOS_CTRL                      0x0C
#define QOS_CREQ_LUT_0                    0x14
#define QOS_CREQ_LUT_1                    0x18

/* QOS_QOS_CTRL */
#define QOS_QOS_CTRL_DANGER_SAFE_EN       BIT(0)
#define QOS_QOS_CTRL_DANGER_VBLANK_MASK   GENMASK(5, 4)
#define QOS_QOS_CTRL_VBLANK_EN            BIT(16)
#define QOS_QOS_CTRL_CREQ_VBLANK_MASK     GENMASK(21, 20)

void dpu_reg_write(struct dpu_hw_blk_reg_map *c,
		u32 reg_off,
		u32 val,
		const char *name)
{
	/* don't need to mutex protect this */
	if (c->log_mask & dpu_hw_util_log_mask)
		DPU_DEBUG_DRIVER("[%s:0x%X] <= 0x%X\n",
				name, reg_off, val);
	writel_relaxed(val, c->blk_addr + reg_off);
}

int dpu_reg_read(struct dpu_hw_blk_reg_map *c, u32 reg_off)
{
	return readl_relaxed(c->blk_addr + reg_off);
}

u32 *dpu_hw_util_get_log_mask_ptr(void)
{
	return &dpu_hw_util_log_mask;
}

static void _dpu_hw_setup_scaler3_lut(struct dpu_hw_blk_reg_map *c,
		struct dpu_hw_scaler3_cfg *scaler3_cfg, u32 offset)
{
	int i, j, filter;
	int config_lut = 0x0;
	unsigned long lut_flags;
	u32 lut_addr, lut_offset, lut_len;
	u32 *lut[QSEED3_FILTERS] = {NULL, NULL, NULL, NULL, NULL};
	static const uint32_t off_tbl[QSEED3_FILTERS][QSEED3_LUT_REGIONS][2] = {
		{{18, 0x000}, {12, 0x120}, {12, 0x1E0}, {8, 0x2A0} },
		{{6, 0x320}, {3, 0x3E0}, {3, 0x440}, {3, 0x4A0} },
		{{6, 0x500}, {3, 0x5c0}, {3, 0x620}, {3, 0x680} },
		{{6, 0x380}, {3, 0x410}, {3, 0x470}, {3, 0x4d0} },
		{{6, 0x560}, {3, 0x5f0}, {3, 0x650}, {3, 0x6b0} },
	};

	lut_flags = (unsigned long) scaler3_cfg->lut_flag;
	if (test_bit(QSEED3_COEF_LUT_DIR_BIT, &lut_flags) &&
		(scaler3_cfg->dir_len == QSEED3_DIR_LUT_SIZE)) {
		lut[0] = scaler3_cfg->dir_lut;
		config_lut = 1;
	}
	if (test_bit(QSEED3_COEF_LUT_Y_CIR_BIT, &lut_flags) &&
		(scaler3_cfg->y_rgb_cir_lut_idx < QSEED3_CIRCULAR_LUTS) &&
		(scaler3_cfg->cir_len == QSEED3_CIR_LUT_SIZE)) {
		lut[1] = scaler3_cfg->cir_lut +
			scaler3_cfg->y_rgb_cir_lut_idx * QSEED3_LUT_SIZE;
		config_lut = 1;
	}
	if (test_bit(QSEED3_COEF_LUT_UV_CIR_BIT, &lut_flags) &&
		(scaler3_cfg->uv_cir_lut_idx < QSEED3_CIRCULAR_LUTS) &&
		(scaler3_cfg->cir_len == QSEED3_CIR_LUT_SIZE)) {
		lut[2] = scaler3_cfg->cir_lut +
			scaler3_cfg->uv_cir_lut_idx * QSEED3_LUT_SIZE;
		config_lut = 1;
	}
	if (test_bit(QSEED3_COEF_LUT_Y_SEP_BIT, &lut_flags) &&
		(scaler3_cfg->y_rgb_sep_lut_idx < QSEED3_SEPARABLE_LUTS) &&
		(scaler3_cfg->sep_len == QSEED3_SEP_LUT_SIZE)) {
		lut[3] = scaler3_cfg->sep_lut +
			scaler3_cfg->y_rgb_sep_lut_idx * QSEED3_LUT_SIZE;
		config_lut = 1;
	}
	if (test_bit(QSEED3_COEF_LUT_UV_SEP_BIT, &lut_flags) &&
		(scaler3_cfg->uv_sep_lut_idx < QSEED3_SEPARABLE_LUTS) &&
		(scaler3_cfg->sep_len == QSEED3_SEP_LUT_SIZE)) {
		lut[4] = scaler3_cfg->sep_lut +
			scaler3_cfg->uv_sep_lut_idx * QSEED3_LUT_SIZE;
		config_lut = 1;
	}

	if (config_lut) {
		for (filter = 0; filter < QSEED3_FILTERS; filter++) {
			if (!lut[filter])
				continue;
			lut_offset = 0;
			for (i = 0; i < QSEED3_LUT_REGIONS; i++) {
				lut_addr = QSEED3_COEF_LUT + offset
					+ off_tbl[filter][i][1];
				lut_len = off_tbl[filter][i][0] << 2;
				for (j = 0; j < lut_len; j++) {
					DPU_REG_WRITE(c,
						lut_addr,
						(lut[filter])[lut_offset++]);
					lut_addr += 4;
				}
			}
		}
	}

	if (test_bit(QSEED3_COEF_LUT_SWAP_BIT, &lut_flags))
		DPU_REG_WRITE(c, QSEED3_COEF_LUT_CTRL + offset, BIT(0));

}

static void _dpu_hw_setup_scaler3lite_lut(struct dpu_hw_blk_reg_map *c,
		struct dpu_hw_scaler3_cfg *scaler3_cfg, u32 offset)
{
	int j, filter;
	int config_lut = 0x0;
	unsigned long lut_flags;
	u32 lut_addr, lut_offset;
	u32 *lut[QSEED3LITE_FILTERS] = {NULL, NULL};
	static const uint32_t off_tbl[QSEED3_FILTERS] = { 0x000, 0x200 };

	DPU_REG_WRITE(c, QSEED3LITE_DIR_FILTER_WEIGHT + offset, scaler3_cfg->dir_weight);

	if (!scaler3_cfg->sep_lut)
		return;

	lut_flags = (unsigned long) scaler3_cfg->lut_flag;
	if (test_bit(QSEED3_COEF_LUT_Y_SEP_BIT, &lut_flags) &&
		(scaler3_cfg->y_rgb_sep_lut_idx < QSEED3LITE_SEPARABLE_LUTS) &&
		(scaler3_cfg->sep_len == QSEED3LITE_SEP_LUT_SIZE)) {
		lut[0] = scaler3_cfg->sep_lut +
			scaler3_cfg->y_rgb_sep_lut_idx * QSEED3LITE_LUT_SIZE;
		config_lut = 1;
	}
	if (test_bit(QSEED3_COEF_LUT_UV_SEP_BIT, &lut_flags) &&
		(scaler3_cfg->uv_sep_lut_idx < QSEED3LITE_SEPARABLE_LUTS) &&
		(scaler3_cfg->sep_len == QSEED3LITE_SEP_LUT_SIZE)) {
		lut[1] = scaler3_cfg->sep_lut +
			scaler3_cfg->uv_sep_lut_idx * QSEED3LITE_LUT_SIZE;
		config_lut = 1;
	}

	if (config_lut) {
		for (filter = 0; filter < QSEED3LITE_FILTERS; filter++) {
			if (!lut[filter])
				continue;
			lut_offset = 0;
			lut_addr = QSEED3_COEF_LUT + offset + off_tbl[filter];
			for (j = 0; j < QSEED3LITE_LUT_SIZE; j++) {
				DPU_REG_WRITE(c,
					lut_addr,
					(lut[filter])[lut_offset++]);
				lut_addr += 4;
			}
		}
	}

	if (test_bit(QSEED3_COEF_LUT_SWAP_BIT, &lut_flags))
		DPU_REG_WRITE(c, QSEED3_COEF_LUT_CTRL + offset, BIT(0));

}

static void _dpu_hw_setup_scaler3_de(struct dpu_hw_blk_reg_map *c,
		struct dpu_hw_scaler3_de_cfg *de_cfg, u32 offset)
{
	u32 sharp_lvl, sharp_ctl, shape_ctl, de_thr;
	u32 adjust_a, adjust_b, adjust_c;

	if (!de_cfg->enable)
		return;

	sharp_lvl = (de_cfg->sharpen_level1 & 0x1FF) |
		((de_cfg->sharpen_level2 & 0x1FF) << 16);

	sharp_ctl = ((de_cfg->limit & 0xF) << 9) |
		((de_cfg->prec_shift & 0x7) << 13) |
		((de_cfg->clip & 0x7) << 16);

	shape_ctl = (de_cfg->thr_quiet & 0xFF) |
		((de_cfg->thr_dieout & 0x3FF) << 16);

	de_thr = (de_cfg->thr_low & 0x3FF) |
		((de_cfg->thr_high & 0x3FF) << 16);

	adjust_a = (de_cfg->adjust_a[0] & 0x3FF) |
		((de_cfg->adjust_a[1] & 0x3FF) << 10) |
		((de_cfg->adjust_a[2] & 0x3FF) << 20);

	adjust_b = (de_cfg->adjust_b[0] & 0x3FF) |
		((de_cfg->adjust_b[1] & 0x3FF) << 10) |
		((de_cfg->adjust_b[2] & 0x3FF) << 20);

	adjust_c = (de_cfg->adjust_c[0] & 0x3FF) |
		((de_cfg->adjust_c[1] & 0x3FF) << 10) |
		((de_cfg->adjust_c[2] & 0x3FF) << 20);

	DPU_REG_WRITE(c, QSEED3_DE_SHARPEN + offset, sharp_lvl);
	DPU_REG_WRITE(c, QSEED3_DE_SHARPEN_CTL + offset, sharp_ctl);
	DPU_REG_WRITE(c, QSEED3_DE_SHAPE_CTL + offset, shape_ctl);
	DPU_REG_WRITE(c, QSEED3_DE_THRESHOLD + offset, de_thr);
	DPU_REG_WRITE(c, QSEED3_DE_ADJUST_DATA_0 + offset, adjust_a);
	DPU_REG_WRITE(c, QSEED3_DE_ADJUST_DATA_1 + offset, adjust_b);
	DPU_REG_WRITE(c, QSEED3_DE_ADJUST_DATA_2 + offset, adjust_c);

}

void dpu_hw_setup_scaler3(struct dpu_hw_blk_reg_map *c,
		struct dpu_hw_scaler3_cfg *scaler3_cfg,
		u32 scaler_offset, u32 scaler_version,
		const struct msm_format *format)
{
	u32 op_mode = 0;
	u32 phase_init, preload, src_y_rgb, src_uv, dst;

	if (!scaler3_cfg->enable)
		goto end;

	op_mode |= BIT(0);
	op_mode |= (scaler3_cfg->y_rgb_filter_cfg & 0x3) << 16;

	if (format && MSM_FORMAT_IS_YUV(format)) {
		op_mode |= BIT(12);
		op_mode |= (scaler3_cfg->uv_filter_cfg & 0x3) << 24;
	}

	op_mode |= (scaler3_cfg->blend_cfg & 1) << 31;
	op_mode |= (scaler3_cfg->dir_en) ? BIT(4) : 0;

	preload =
		((scaler3_cfg->preload_x[0] & 0x7F) << 0) |
		((scaler3_cfg->preload_y[0] & 0x7F) << 8) |
		((scaler3_cfg->preload_x[1] & 0x7F) << 16) |
		((scaler3_cfg->preload_y[1] & 0x7F) << 24);

	src_y_rgb = (scaler3_cfg->src_width[0] & 0x1FFFF) |
		((scaler3_cfg->src_height[0] & 0x1FFFF) << 16);

	src_uv = (scaler3_cfg->src_width[1] & 0x1FFFF) |
		((scaler3_cfg->src_height[1] & 0x1FFFF) << 16);

	dst = (scaler3_cfg->dst_width & 0x1FFFF) |
		((scaler3_cfg->dst_height & 0x1FFFF) << 16);

	if (scaler3_cfg->de.enable) {
		_dpu_hw_setup_scaler3_de(c, &scaler3_cfg->de, scaler_offset);
		op_mode |= BIT(8);
	}

	if (scaler3_cfg->lut_flag) {
		if (scaler_version < 0x2004)
			_dpu_hw_setup_scaler3_lut(c, scaler3_cfg, scaler_offset);
		else
			_dpu_hw_setup_scaler3lite_lut(c, scaler3_cfg, scaler_offset);
	}

	if (scaler_version == 0x1002) {
		phase_init =
			((scaler3_cfg->init_phase_x[0] & 0x3F) << 0) |
			((scaler3_cfg->init_phase_y[0] & 0x3F) << 8) |
			((scaler3_cfg->init_phase_x[1] & 0x3F) << 16) |
			((scaler3_cfg->init_phase_y[1] & 0x3F) << 24);
		DPU_REG_WRITE(c, QSEED3_PHASE_INIT + scaler_offset, phase_init);
	} else {
		DPU_REG_WRITE(c, QSEED3_PHASE_INIT_Y_H + scaler_offset,
			scaler3_cfg->init_phase_x[0] & 0x1FFFFF);
		DPU_REG_WRITE(c, QSEED3_PHASE_INIT_Y_V + scaler_offset,
			scaler3_cfg->init_phase_y[0] & 0x1FFFFF);
		DPU_REG_WRITE(c, QSEED3_PHASE_INIT_UV_H + scaler_offset,
			scaler3_cfg->init_phase_x[1] & 0x1FFFFF);
		DPU_REG_WRITE(c, QSEED3_PHASE_INIT_UV_V + scaler_offset,
			scaler3_cfg->init_phase_y[1] & 0x1FFFFF);
	}

	DPU_REG_WRITE(c, QSEED3_PHASE_STEP_Y_H + scaler_offset,
		scaler3_cfg->phase_step_x[0] & 0xFFFFFF);

	DPU_REG_WRITE(c, QSEED3_PHASE_STEP_Y_V + scaler_offset,
		scaler3_cfg->phase_step_y[0] & 0xFFFFFF);

	DPU_REG_WRITE(c, QSEED3_PHASE_STEP_UV_H + scaler_offset,
		scaler3_cfg->phase_step_x[1] & 0xFFFFFF);

	DPU_REG_WRITE(c, QSEED3_PHASE_STEP_UV_V + scaler_offset,
		scaler3_cfg->phase_step_y[1] & 0xFFFFFF);

	DPU_REG_WRITE(c, QSEED3_PRELOAD + scaler_offset, preload);

	DPU_REG_WRITE(c, QSEED3_SRC_SIZE_Y_RGB_A + scaler_offset, src_y_rgb);

	DPU_REG_WRITE(c, QSEED3_SRC_SIZE_UV + scaler_offset, src_uv);

	DPU_REG_WRITE(c, QSEED3_DST_SIZE + scaler_offset, dst);

end:
	if (format && !MSM_FORMAT_IS_DX(format))
		op_mode |= BIT(14);

	if (format && format->alpha_enable) {
		op_mode |= BIT(10);
		if (scaler_version == 0x1002)
			op_mode |= (scaler3_cfg->alpha_filter_cfg & 0x1) << 30;
		else
			op_mode |= (scaler3_cfg->alpha_filter_cfg & 0x3) << 29;
	}

	DPU_REG_WRITE(c, QSEED3_OP_MODE + scaler_offset, op_mode);
}

void dpu_hw_csc_setup(struct dpu_hw_blk_reg_map *c,
		u32 csc_reg_off,
		const struct dpu_csc_cfg *data, bool csc10)
{
	static const u32 matrix_shift = 7;
	u32 clamp_shift = csc10 ? 16 : 8;
	u32 val;

	/* matrix coeff - convert S15.16 to S4.9 */
	val = ((data->csc_mv[0] >> matrix_shift) & 0x1FFF) |
		(((data->csc_mv[1] >> matrix_shift) & 0x1FFF) << 16);
	DPU_REG_WRITE(c, csc_reg_off, val);
	val = ((data->csc_mv[2] >> matrix_shift) & 0x1FFF) |
		(((data->csc_mv[3] >> matrix_shift) & 0x1FFF) << 16);
	DPU_REG_WRITE(c, csc_reg_off + 0x4, val);
	val = ((data->csc_mv[4] >> matrix_shift) & 0x1FFF) |
		(((data->csc_mv[5] >> matrix_shift) & 0x1FFF) << 16);
	DPU_REG_WRITE(c, csc_reg_off + 0x8, val);
	val = ((data->csc_mv[6] >> matrix_shift) & 0x1FFF) |
		(((data->csc_mv[7] >> matrix_shift) & 0x1FFF) << 16);
	DPU_REG_WRITE(c, csc_reg_off + 0xc, val);
	val = (data->csc_mv[8] >> matrix_shift) & 0x1FFF;
	DPU_REG_WRITE(c, csc_reg_off + 0x10, val);

	/* Pre clamp */
	val = (data->csc_pre_lv[0] << clamp_shift) | data->csc_pre_lv[1];
	DPU_REG_WRITE(c, csc_reg_off + 0x14, val);
	val = (data->csc_pre_lv[2] << clamp_shift) | data->csc_pre_lv[3];
	DPU_REG_WRITE(c, csc_reg_off + 0x18, val);
	val = (data->csc_pre_lv[4] << clamp_shift) | data->csc_pre_lv[5];
	DPU_REG_WRITE(c, csc_reg_off + 0x1c, val);

	/* Post clamp */
	val = (data->csc_post_lv[0] << clamp_shift) | data->csc_post_lv[1];
	DPU_REG_WRITE(c, csc_reg_off + 0x20, val);
	val = (data->csc_post_lv[2] << clamp_shift) | data->csc_post_lv[3];
	DPU_REG_WRITE(c, csc_reg_off + 0x24, val);
	val = (data->csc_post_lv[4] << clamp_shift) | data->csc_post_lv[5];
	DPU_REG_WRITE(c, csc_reg_off + 0x28, val);

	/* Pre-Bias */
	DPU_REG_WRITE(c, csc_reg_off + 0x2c, data->csc_pre_bv[0]);
	DPU_REG_WRITE(c, csc_reg_off + 0x30, data->csc_pre_bv[1]);
	DPU_REG_WRITE(c, csc_reg_off + 0x34, data->csc_pre_bv[2]);

	/* Post-Bias */
	DPU_REG_WRITE(c, csc_reg_off + 0x38, data->csc_post_bv[0]);
	DPU_REG_WRITE(c, csc_reg_off + 0x3c, data->csc_post_bv[1]);
	DPU_REG_WRITE(c, csc_reg_off + 0x40, data->csc_post_bv[2]);
}

/**
 * _dpu_hw_get_qos_lut - get LUT mapping based on fill level
 * @tbl:		Pointer to LUT table
 * @total_fl:		fill level
 * Return: LUT setting corresponding to the fill level
 */
u64 _dpu_hw_get_qos_lut(const struct dpu_qos_lut_tbl *tbl,
		u32 total_fl)
{
	int i;

	if (!tbl || !tbl->nentry || !tbl->entries)
		return 0;

	for (i = 0; i < tbl->nentry; i++)
		if (total_fl <= tbl->entries[i].fl)
			return tbl->entries[i].lut;

	/* if last fl is zero, use as default */
	if (!tbl->entries[i-1].fl)
		return tbl->entries[i-1].lut;

	return 0;
}

void _dpu_hw_setup_qos_lut(struct dpu_hw_blk_reg_map *c, u32 offset,
			   bool qos_8lvl,
			   const struct dpu_hw_qos_cfg *cfg)
{
	DPU_REG_WRITE(c, offset + QOS_DANGER_LUT, cfg->danger_lut);
	DPU_REG_WRITE(c, offset + QOS_SAFE_LUT, cfg->safe_lut);

	if (qos_8lvl) {
		DPU_REG_WRITE(c, offset + QOS_CREQ_LUT_0, cfg->creq_lut);
		DPU_REG_WRITE(c, offset + QOS_CREQ_LUT_1, cfg->creq_lut >> 32);
	} else {
		DPU_REG_WRITE(c, offset + QOS_CREQ_LUT, cfg->creq_lut);
	}

	DPU_REG_WRITE(c, offset + QOS_QOS_CTRL,
		      cfg->danger_safe_en ? QOS_QOS_CTRL_DANGER_SAFE_EN : 0);
}

/*
 * note: Aside from encoders, input_sel should be set to 0x0 by default
 */
void dpu_hw_setup_misr(struct dpu_hw_blk_reg_map *c,
		u32 misr_ctrl_offset, u8 input_sel)
{
	u32 config = 0;

	DPU_REG_WRITE(c, misr_ctrl_offset, MISR_CTRL_STATUS_CLEAR);

	/* Clear old MISR value (in case it's read before a new value is calculated)*/
	wmb();

	config = MISR_FRAME_COUNT | MISR_CTRL_ENABLE | MISR_CTRL_FREE_RUN_MASK |
		((input_sel & 0xF) << 24);
	DPU_REG_WRITE(c, misr_ctrl_offset, config);
}

int dpu_hw_collect_misr(struct dpu_hw_blk_reg_map *c,
		u32 misr_ctrl_offset,
		u32 misr_signature_offset,
		u32 *misr_value)
{
	u32 ctrl = 0;

	if (!misr_value)
		return -EINVAL;

	ctrl = DPU_REG_READ(c, misr_ctrl_offset);

	if (!(ctrl & MISR_CTRL_ENABLE))
		return -ENODATA;

	if (!(ctrl & MISR_CTRL_STATUS))
		return -EINVAL;

	*misr_value = DPU_REG_READ(c, misr_signature_offset);

	return 0;
}

#define CDP_ENABLE		BIT(0)
#define CDP_UBWC_META_ENABLE	BIT(1)
#define CDP_TILE_AMORTIZE_ENABLE BIT(2)
#define CDP_PRELOAD_AHEAD_64	BIT(3)

void dpu_setup_cdp(struct dpu_hw_blk_reg_map *c, u32 offset,
		   const struct msm_format *fmt, bool enable)
{
	u32 cdp_cntl = CDP_PRELOAD_AHEAD_64;

	if (enable)
		cdp_cntl |= CDP_ENABLE;
	if (MSM_FORMAT_IS_UBWC(fmt))
		cdp_cntl |= CDP_UBWC_META_ENABLE;
	if (MSM_FORMAT_IS_UBWC(fmt) ||
	    MSM_FORMAT_IS_TILE(fmt))
		cdp_cntl |= CDP_TILE_AMORTIZE_ENABLE;

	DPU_REG_WRITE(c, offset, cdp_cntl);
}

bool dpu_hw_clk_force_ctrl(struct dpu_hw_blk_reg_map *c,
			   const struct dpu_clk_ctrl_reg *clk_ctrl_reg,
			   bool enable)
{
	u32 reg_val, new_val;
	bool clk_forced_on;

	reg_val = DPU_REG_READ(c, clk_ctrl_reg->reg_off);

	if (enable)
		new_val = reg_val | BIT(clk_ctrl_reg->bit_off);
	else
		new_val = reg_val & ~BIT(clk_ctrl_reg->bit_off);

	DPU_REG_WRITE(c, clk_ctrl_reg->reg_off, new_val);

	clk_forced_on = !(reg_val & BIT(clk_ctrl_reg->bit_off));

	return clk_forced_on;
}

#define TO_S15D16(_x_)((_x_) << 7)

const struct dpu_csc_cfg dpu_csc_YUV2RGB_601L = {
	{
		/* S15.16 format */
		0x00012A00, 0x00000000, 0x00019880,
		0x00012A00, 0xFFFF9B80, 0xFFFF3000,
		0x00012A00, 0x00020480, 0x00000000,
	},
	/* signed bias */
	{ 0xfff0, 0xff80, 0xff80,},
	{ 0x0, 0x0, 0x0,},
	/* unsigned clamp */
	{ 0x10, 0xeb, 0x10, 0xf0, 0x10, 0xf0,},
	{ 0x00, 0xff, 0x00, 0xff, 0x00, 0xff,},
};

const struct dpu_csc_cfg dpu_csc10_YUV2RGB_601L = {
	{
		/* S15.16 format */
		0x00012A00, 0x00000000, 0x00019880,
		0x00012A00, 0xFFFF9B80, 0xFFFF3000,
		0x00012A00, 0x00020480, 0x00000000,
	},
	/* signed bias */
	{ 0xffc0, 0xfe00, 0xfe00,},
	{ 0x0, 0x0, 0x0,},
	/* unsigned clamp */
	{ 0x40, 0x3ac, 0x40, 0x3c0, 0x40, 0x3c0,},
	{ 0x00, 0x3ff, 0x00, 0x3ff, 0x00, 0x3ff,},
};

const struct dpu_csc_cfg dpu_csc10_rgb2yuv_601l = {
	{
		TO_S15D16(0x0083), TO_S15D16(0x0102), TO_S15D16(0x0032),
		TO_S15D16(0x1fb5), TO_S15D16(0x1f6c), TO_S15D16(0x00e1),
		TO_S15D16(0x00e1), TO_S15D16(0x1f45), TO_S15D16(0x1fdc)
	},
	{ 0x00, 0x00, 0x00 },
	{ 0x0040, 0x0200, 0x0200 },
	{ 0x000, 0x3ff, 0x000, 0x3ff, 0x000, 0x3ff },
	{ 0x040, 0x3ac, 0x040, 0x3c0, 0x040, 0x3c0 },
};
