/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 * Copyright (c) 2015-2021, The Linux Foundation. All rights reserved.
 */

#ifndef _DPU_HW_UTIL_H
#define _DPU_HW_UTIL_H

#include <linux/io.h>
#include <linux/slab.h>
#include "dpu_hw_mdss.h"
#include "dpu_hw_catalog.h"

#define REG_MASK(n)                     ((BIT(n)) - 1)
#define MISR_FRAME_COUNT_MASK           0xFF
#define MISR_CTRL_ENABLE                BIT(8)
#define MISR_CTRL_STATUS                BIT(9)
#define MISR_CTRL_STATUS_CLEAR          BIT(10)
#define MISR_CTRL_FREE_RUN_MASK         BIT(31)

/*
 * This is the common struct maintained by each sub block
 * for mapping the register offsets in this block to the
 * absoulute IO address
 * @blk_addr:     hw block register mapped address
 * @log_mask:     log mask for this block
 */
struct dpu_hw_blk_reg_map {
	void __iomem *blk_addr;
	u32 log_mask;
};

/**
 * struct dpu_hw_blk - opaque hardware block object
 */
struct dpu_hw_blk {
	/* opaque */
};

/**
 * struct dpu_hw_scaler3_de_cfg : QSEEDv3 detail enhancer configuration
 * @enable:         detail enhancer enable/disable
 * @sharpen_level1: sharpening strength for noise
 * @sharpen_level2: sharpening strength for signal
 * @ clip:          clip shift
 * @ limit:         limit value
 * @ thr_quiet:     quiet threshold
 * @ thr_dieout:    dieout threshold
 * @ thr_high:      low threshold
 * @ thr_high:      high threshold
 * @ prec_shift:    precision shift
 * @ adjust_a:      A-coefficients for mapping curve
 * @ adjust_b:      B-coefficients for mapping curve
 * @ adjust_c:      C-coefficients for mapping curve
 */
struct dpu_hw_scaler3_de_cfg {
	u32 enable;
	int16_t sharpen_level1;
	int16_t sharpen_level2;
	uint16_t clip;
	uint16_t limit;
	uint16_t thr_quiet;
	uint16_t thr_dieout;
	uint16_t thr_low;
	uint16_t thr_high;
	uint16_t prec_shift;
	int16_t adjust_a[DPU_MAX_DE_CURVES];
	int16_t adjust_b[DPU_MAX_DE_CURVES];
	int16_t adjust_c[DPU_MAX_DE_CURVES];
};


/**
 * struct dpu_hw_scaler3_cfg : QSEEDv3 configuration
 * @enable:        scaler enable
 * @dir_en:        direction detection block enable
 * @ init_phase_x: horizontal initial phase
 * @ phase_step_x: horizontal phase step
 * @ init_phase_y: vertical initial phase
 * @ phase_step_y: vertical phase step
 * @ preload_x:    horizontal preload value
 * @ preload_y:    vertical preload value
 * @ src_width:    source width
 * @ src_height:   source height
 * @ dst_width:    destination width
 * @ dst_height:   destination height
 * @ y_rgb_filter_cfg: y/rgb plane filter configuration
 * @ uv_filter_cfg: uv plane filter configuration
 * @ alpha_filter_cfg: alpha filter configuration
 * @ blend_cfg:    blend coefficients configuration
 * @ lut_flag:     scaler LUT update flags
 *                 0x1 swap LUT bank
 *                 0x2 update 2D filter LUT
 *                 0x4 update y circular filter LUT
 *                 0x8 update uv circular filter LUT
 *                 0x10 update y separable filter LUT
 *                 0x20 update uv separable filter LUT
 * @ dir_lut_idx:  2D filter LUT index
 * @ y_rgb_cir_lut_idx: y circular filter LUT index
 * @ uv_cir_lut_idx: uv circular filter LUT index
 * @ y_rgb_sep_lut_idx: y circular filter LUT index
 * @ uv_sep_lut_idx: uv separable filter LUT index
 * @ dir_lut:      pointer to 2D LUT
 * @ cir_lut:      pointer to circular filter LUT
 * @ sep_lut:      pointer to separable filter LUT
 * @ de: detail enhancer configuration
 * @ dir_weight:   Directional weight
 */
struct dpu_hw_scaler3_cfg {
	u32 enable;
	u32 dir_en;
	int32_t init_phase_x[DPU_MAX_PLANES];
	int32_t phase_step_x[DPU_MAX_PLANES];
	int32_t init_phase_y[DPU_MAX_PLANES];
	int32_t phase_step_y[DPU_MAX_PLANES];

	u32 preload_x[DPU_MAX_PLANES];
	u32 preload_y[DPU_MAX_PLANES];
	u32 src_width[DPU_MAX_PLANES];
	u32 src_height[DPU_MAX_PLANES];

	u32 dst_width;
	u32 dst_height;

	u32 y_rgb_filter_cfg;
	u32 uv_filter_cfg;
	u32 alpha_filter_cfg;
	u32 blend_cfg;

	u32 lut_flag;
	u32 dir_lut_idx;

	u32 y_rgb_cir_lut_idx;
	u32 uv_cir_lut_idx;
	u32 y_rgb_sep_lut_idx;
	u32 uv_sep_lut_idx;
	u32 *dir_lut;
	size_t dir_len;
	u32 *cir_lut;
	size_t cir_len;
	u32 *sep_lut;
	size_t sep_len;

	/*
	 * Detail enhancer settings
	 */
	struct dpu_hw_scaler3_de_cfg de;

	u32 dir_weight;
};

/**
 * struct dpu_drm_pix_ext_v1 - version 1 of pixel ext structure
 * @num_ext_pxls_lr: Number of total horizontal pixels
 * @num_ext_pxls_tb: Number of total vertical lines
 * @left_ftch:       Number of extra pixels to overfetch from left
 * @right_ftch:      Number of extra pixels to overfetch from right
 * @top_ftch:        Number of extra lines to overfetch from top
 * @btm_ftch:        Number of extra lines to overfetch from bottom
 * @left_rpt:        Number of extra pixels to repeat from left
 * @right_rpt:       Number of extra pixels to repeat from right
 * @top_rpt:         Number of extra lines to repeat from top
 * @btm_rpt:         Number of extra lines to repeat from bottom
 */
struct dpu_drm_pix_ext_v1 {
	/*
	 * Number of pixels ext in left, right, top and bottom direction
	 * for all color components.
	 */
	int32_t num_ext_pxls_lr[DPU_MAX_PLANES];
	int32_t num_ext_pxls_tb[DPU_MAX_PLANES];

	/*
	 * Number of pixels needs to be overfetched in left, right, top
	 * and bottom directions from source image for scaling.
	 */
	int32_t left_ftch[DPU_MAX_PLANES];
	int32_t right_ftch[DPU_MAX_PLANES];
	int32_t top_ftch[DPU_MAX_PLANES];
	int32_t btm_ftch[DPU_MAX_PLANES];
	/*
	 * Number of pixels needs to be repeated in left, right, top and
	 * bottom directions for scaling.
	 */
	int32_t left_rpt[DPU_MAX_PLANES];
	int32_t right_rpt[DPU_MAX_PLANES];
	int32_t top_rpt[DPU_MAX_PLANES];
	int32_t btm_rpt[DPU_MAX_PLANES];

};

/**
 * struct dpu_drm_de_v1 - version 1 of detail enhancer structure
 * @enable:         Enables/disables detail enhancer
 * @sharpen_level1: Sharpening strength for noise
 * @sharpen_level2: Sharpening strength for context
 * @clip:           Clip coefficient
 * @limit:          Detail enhancer limit factor
 * @thr_quiet:      Quite zone threshold
 * @thr_dieout:     Die-out zone threshold
 * @thr_low:        Linear zone left threshold
 * @thr_high:       Linear zone right threshold
 * @prec_shift:     Detail enhancer precision
 * @adjust_a:       Mapping curves A coefficients
 * @adjust_b:       Mapping curves B coefficients
 * @adjust_c:       Mapping curves C coefficients
 */
struct dpu_drm_de_v1 {
	uint32_t enable;
	int16_t sharpen_level1;
	int16_t sharpen_level2;
	uint16_t clip;
	uint16_t limit;
	uint16_t thr_quiet;
	uint16_t thr_dieout;
	uint16_t thr_low;
	uint16_t thr_high;
	uint16_t prec_shift;
	int16_t adjust_a[DPU_MAX_DE_CURVES];
	int16_t adjust_b[DPU_MAX_DE_CURVES];
	int16_t adjust_c[DPU_MAX_DE_CURVES];
};

/**
 * struct dpu_drm_scaler_v2 - version 2 of struct dpu_drm_scaler
 * @enable:            Scaler enable
 * @dir_en:            Detail enhancer enable
 * @pe:                Pixel extension settings
 * @horz_decimate:     Horizontal decimation factor
 * @vert_decimate:     Vertical decimation factor
 * @init_phase_x:      Initial scaler phase values for x
 * @phase_step_x:      Phase step values for x
 * @init_phase_y:      Initial scaler phase values for y
 * @phase_step_y:      Phase step values for y
 * @preload_x:         Horizontal preload value
 * @preload_y:         Vertical preload value
 * @src_width:         Source width
 * @src_height:        Source height
 * @dst_width:         Destination width
 * @dst_height:        Destination height
 * @y_rgb_filter_cfg:  Y/RGB plane filter configuration
 * @uv_filter_cfg:     UV plane filter configuration
 * @alpha_filter_cfg:  Alpha filter configuration
 * @blend_cfg:         Selection of blend coefficients
 * @lut_flag:          LUT configuration flags
 * @dir_lut_idx:       2d 4x4 LUT index
 * @y_rgb_cir_lut_idx: Y/RGB circular LUT index
 * @uv_cir_lut_idx:    UV circular LUT index
 * @y_rgb_sep_lut_idx: Y/RGB separable LUT index
 * @uv_sep_lut_idx:    UV separable LUT index
 * @de:                Detail enhancer settings
 */
struct dpu_drm_scaler_v2 {
	/*
	 * General definitions
	 */
	uint32_t enable;
	uint32_t dir_en;

	/*
	 * Pix ext settings
	 */
	struct dpu_drm_pix_ext_v1 pe;

	/*
	 * Decimation settings
	 */
	uint32_t horz_decimate;
	uint32_t vert_decimate;

	/*
	 * Phase settings
	 */
	int32_t init_phase_x[DPU_MAX_PLANES];
	int32_t phase_step_x[DPU_MAX_PLANES];
	int32_t init_phase_y[DPU_MAX_PLANES];
	int32_t phase_step_y[DPU_MAX_PLANES];

	uint32_t preload_x[DPU_MAX_PLANES];
	uint32_t preload_y[DPU_MAX_PLANES];
	uint32_t src_width[DPU_MAX_PLANES];
	uint32_t src_height[DPU_MAX_PLANES];

	uint32_t dst_width;
	uint32_t dst_height;

	uint32_t y_rgb_filter_cfg;
	uint32_t uv_filter_cfg;
	uint32_t alpha_filter_cfg;
	uint32_t blend_cfg;

	uint32_t lut_flag;
	uint32_t dir_lut_idx;

	/* for Y(RGB) and UV planes*/
	uint32_t y_rgb_cir_lut_idx;
	uint32_t uv_cir_lut_idx;
	uint32_t y_rgb_sep_lut_idx;
	uint32_t uv_sep_lut_idx;

	/*
	 * Detail enhancer settings
	 */
	struct dpu_drm_de_v1 de;
};

/**
 * struct dpu_hw_cdp_cfg : CDP configuration
 * @enable: true to enable CDP
 * @ubwc_meta_enable: true to enable ubwc metadata preload
 * @tile_amortize_enable: true to enable amortization control for tile format
 * @preload_ahead: number of request to preload ahead
 *	DPU_*_CDP_PRELOAD_AHEAD_32,
 *	DPU_*_CDP_PRELOAD_AHEAD_64
 */
struct dpu_hw_cdp_cfg {
	bool enable;
	bool ubwc_meta_enable;
	bool tile_amortize_enable;
	u32 preload_ahead;
};

u32 *dpu_hw_util_get_log_mask_ptr(void);

void dpu_reg_write(struct dpu_hw_blk_reg_map *c,
		u32 reg_off,
		u32 val,
		const char *name);
int dpu_reg_read(struct dpu_hw_blk_reg_map *c, u32 reg_off);

#define DPU_REG_WRITE(c, off, val) dpu_reg_write(c, off, val, #off)
#define DPU_REG_READ(c, off) dpu_reg_read(c, off)

void *dpu_hw_util_get_dir(void);

void dpu_hw_setup_scaler3(struct dpu_hw_blk_reg_map *c,
		struct dpu_hw_scaler3_cfg *scaler3_cfg,
		u32 scaler_offset, u32 scaler_version,
		const struct dpu_format *format);

u32 dpu_hw_get_scaler3_ver(struct dpu_hw_blk_reg_map *c,
		u32 scaler_offset);

void dpu_hw_csc_setup(struct dpu_hw_blk_reg_map  *c,
		u32 csc_reg_off,
		const struct dpu_csc_cfg *data, bool csc10);

u64 _dpu_hw_get_qos_lut(const struct dpu_qos_lut_tbl *tbl,
		u32 total_fl);

void dpu_hw_setup_misr(struct dpu_hw_blk_reg_map *c,
		u32 misr_ctrl_offset,
		bool enable,
		u32 frame_count);

int dpu_hw_collect_misr(struct dpu_hw_blk_reg_map *c,
		u32 misr_ctrl_offset,
		u32 misr_signature_offset,
		u32 *misr_value);

#endif /* _DPU_HW_UTIL_H */
