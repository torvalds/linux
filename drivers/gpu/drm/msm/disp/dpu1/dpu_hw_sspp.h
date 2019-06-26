/* Copyright (c) 2015-2018, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef _DPU_HW_SSPP_H
#define _DPU_HW_SSPP_H

#include "dpu_hw_catalog.h"
#include "dpu_hw_mdss.h"
#include "dpu_hw_util.h"
#include "dpu_hw_blk.h"
#include "dpu_formats.h"

struct dpu_hw_pipe;

/**
 * Flags
 */
#define DPU_SSPP_FLIP_LR		BIT(0)
#define DPU_SSPP_FLIP_UD		BIT(1)
#define DPU_SSPP_SOURCE_ROTATED_90	BIT(2)
#define DPU_SSPP_ROT_90			BIT(3)
#define DPU_SSPP_SOLID_FILL		BIT(4)

/**
 * Define all scaler feature bits in catalog
 */
#define DPU_SSPP_SCALER ((1UL << DPU_SSPP_SCALER_RGB) | \
	(1UL << DPU_SSPP_SCALER_QSEED2) | \
	(1UL << DPU_SSPP_SCALER_QSEED3))

/**
 * Component indices
 */
enum {
	DPU_SSPP_COMP_0,
	DPU_SSPP_COMP_1_2,
	DPU_SSPP_COMP_2,
	DPU_SSPP_COMP_3,

	DPU_SSPP_COMP_MAX
};

/**
 * DPU_SSPP_RECT_SOLO - multirect disabled
 * DPU_SSPP_RECT_0 - rect0 of a multirect pipe
 * DPU_SSPP_RECT_1 - rect1 of a multirect pipe
 *
 * Note: HW supports multirect with either RECT0 or
 * RECT1. Considering no benefit of such configs over
 * SOLO mode and to keep the plane management simple,
 * we dont support single rect multirect configs.
 */
enum dpu_sspp_multirect_index {
	DPU_SSPP_RECT_SOLO = 0,
	DPU_SSPP_RECT_0,
	DPU_SSPP_RECT_1,
};

enum dpu_sspp_multirect_mode {
	DPU_SSPP_MULTIRECT_NONE = 0,
	DPU_SSPP_MULTIRECT_PARALLEL,
	DPU_SSPP_MULTIRECT_TIME_MX,
};

enum {
	DPU_FRAME_LINEAR,
	DPU_FRAME_TILE_A4X,
	DPU_FRAME_TILE_A5X,
};

enum dpu_hw_filter {
	DPU_SCALE_FILTER_NEAREST = 0,
	DPU_SCALE_FILTER_BIL,
	DPU_SCALE_FILTER_PCMN,
	DPU_SCALE_FILTER_CA,
	DPU_SCALE_FILTER_MAX
};

enum dpu_hw_filter_alpa {
	DPU_SCALE_ALPHA_PIXEL_REP,
	DPU_SCALE_ALPHA_BIL
};

enum dpu_hw_filter_yuv {
	DPU_SCALE_2D_4X4,
	DPU_SCALE_2D_CIR,
	DPU_SCALE_1D_SEP,
	DPU_SCALE_BIL
};

struct dpu_hw_sharp_cfg {
	u32 strength;
	u32 edge_thr;
	u32 smooth_thr;
	u32 noise_thr;
};

struct dpu_hw_pixel_ext {
	/* scaling factors are enabled for this input layer */
	uint8_t enable_pxl_ext;

	int init_phase_x[DPU_MAX_PLANES];
	int phase_step_x[DPU_MAX_PLANES];
	int init_phase_y[DPU_MAX_PLANES];
	int phase_step_y[DPU_MAX_PLANES];

	/*
	 * Number of pixels extension in left, right, top and bottom direction
	 * for all color components. This pixel value for each color component
	 * should be sum of fetch + repeat pixels.
	 */
	int num_ext_pxls_left[DPU_MAX_PLANES];
	int num_ext_pxls_right[DPU_MAX_PLANES];
	int num_ext_pxls_top[DPU_MAX_PLANES];
	int num_ext_pxls_btm[DPU_MAX_PLANES];

	/*
	 * Number of pixels needs to be overfetched in left, right, top and
	 * bottom directions from source image for scaling.
	 */
	int left_ftch[DPU_MAX_PLANES];
	int right_ftch[DPU_MAX_PLANES];
	int top_ftch[DPU_MAX_PLANES];
	int btm_ftch[DPU_MAX_PLANES];

	/*
	 * Number of pixels needs to be repeated in left, right, top and
	 * bottom directions for scaling.
	 */
	int left_rpt[DPU_MAX_PLANES];
	int right_rpt[DPU_MAX_PLANES];
	int top_rpt[DPU_MAX_PLANES];
	int btm_rpt[DPU_MAX_PLANES];

	uint32_t roi_w[DPU_MAX_PLANES];
	uint32_t roi_h[DPU_MAX_PLANES];

	/*
	 * Filter type to be used for scaling in horizontal and vertical
	 * directions
	 */
	enum dpu_hw_filter horz_filter[DPU_MAX_PLANES];
	enum dpu_hw_filter vert_filter[DPU_MAX_PLANES];

};

/**
 * struct dpu_hw_pipe_cfg : Pipe description
 * @layout:    format layout information for programming buffer to hardware
 * @src_rect:  src ROI, caller takes into account the different operations
 *             such as decimation, flip etc to program this field
 * @dest_rect: destination ROI.
 * @index:     index of the rectangle of SSPP
 * @mode:      parallel or time multiplex multirect mode
 */
struct dpu_hw_pipe_cfg {
	struct dpu_hw_fmt_layout layout;
	struct drm_rect src_rect;
	struct drm_rect dst_rect;
	enum dpu_sspp_multirect_index index;
	enum dpu_sspp_multirect_mode mode;
};

/**
 * struct dpu_hw_pipe_qos_cfg : Source pipe QoS configuration
 * @danger_lut: LUT for generate danger level based on fill level
 * @safe_lut: LUT for generate safe level based on fill level
 * @creq_lut: LUT for generate creq level based on fill level
 * @creq_vblank: creq value generated to vbif during vertical blanking
 * @danger_vblank: danger value generated during vertical blanking
 * @vblank_en: enable creq_vblank and danger_vblank during vblank
 * @danger_safe_en: enable danger safe generation
 */
struct dpu_hw_pipe_qos_cfg {
	u32 danger_lut;
	u32 safe_lut;
	u64 creq_lut;
	u32 creq_vblank;
	u32 danger_vblank;
	bool vblank_en;
	bool danger_safe_en;
};

/**
 * enum CDP preload ahead address size
 */
enum {
	DPU_SSPP_CDP_PRELOAD_AHEAD_32,
	DPU_SSPP_CDP_PRELOAD_AHEAD_64
};

/**
 * struct dpu_hw_pipe_cdp_cfg : CDP configuration
 * @enable: true to enable CDP
 * @ubwc_meta_enable: true to enable ubwc metadata preload
 * @tile_amortize_enable: true to enable amortization control for tile format
 * @preload_ahead: number of request to preload ahead
 *	DPU_SSPP_CDP_PRELOAD_AHEAD_32,
 *	DPU_SSPP_CDP_PRELOAD_AHEAD_64
 */
struct dpu_hw_pipe_cdp_cfg {
	bool enable;
	bool ubwc_meta_enable;
	bool tile_amortize_enable;
	u32 preload_ahead;
};

/**
 * struct dpu_hw_pipe_ts_cfg - traffic shaper configuration
 * @size: size to prefill in bytes, or zero to disable
 * @time: time to prefill in usec, or zero to disable
 */
struct dpu_hw_pipe_ts_cfg {
	u64 size;
	u64 time;
};

/**
 * struct dpu_hw_sspp_ops - interface to the SSPP Hw driver functions
 * Caller must call the init function to get the pipe context for each pipe
 * Assumption is these functions will be called after clocks are enabled
 */
struct dpu_hw_sspp_ops {
	/**
	 * setup_format - setup pixel format cropping rectangle, flip
	 * @ctx: Pointer to pipe context
	 * @cfg: Pointer to pipe config structure
	 * @flags: Extra flags for format config
	 * @index: rectangle index in multirect
	 */
	void (*setup_format)(struct dpu_hw_pipe *ctx,
			const struct dpu_format *fmt, u32 flags,
			enum dpu_sspp_multirect_index index);

	/**
	 * setup_rects - setup pipe ROI rectangles
	 * @ctx: Pointer to pipe context
	 * @cfg: Pointer to pipe config structure
	 * @index: rectangle index in multirect
	 */
	void (*setup_rects)(struct dpu_hw_pipe *ctx,
			struct dpu_hw_pipe_cfg *cfg,
			enum dpu_sspp_multirect_index index);

	/**
	 * setup_pe - setup pipe pixel extension
	 * @ctx: Pointer to pipe context
	 * @pe_ext: Pointer to pixel ext settings
	 */
	void (*setup_pe)(struct dpu_hw_pipe *ctx,
			struct dpu_hw_pixel_ext *pe_ext);

	/**
	 * setup_sourceaddress - setup pipe source addresses
	 * @ctx: Pointer to pipe context
	 * @cfg: Pointer to pipe config structure
	 * @index: rectangle index in multirect
	 */
	void (*setup_sourceaddress)(struct dpu_hw_pipe *ctx,
			struct dpu_hw_pipe_cfg *cfg,
			enum dpu_sspp_multirect_index index);

	/**
	 * setup_csc - setup color space coversion
	 * @ctx: Pointer to pipe context
	 * @data: Pointer to config structure
	 */
	void (*setup_csc)(struct dpu_hw_pipe *ctx, struct dpu_csc_cfg *data);

	/**
	 * setup_solidfill - enable/disable colorfill
	 * @ctx: Pointer to pipe context
	 * @const_color: Fill color value
	 * @flags: Pipe flags
	 * @index: rectangle index in multirect
	 */
	void (*setup_solidfill)(struct dpu_hw_pipe *ctx, u32 color,
			enum dpu_sspp_multirect_index index);

	/**
	 * setup_multirect - setup multirect configuration
	 * @ctx: Pointer to pipe context
	 * @index: rectangle index in multirect
	 * @mode: parallel fetch / time multiplex multirect mode
	 */

	void (*setup_multirect)(struct dpu_hw_pipe *ctx,
			enum dpu_sspp_multirect_index index,
			enum dpu_sspp_multirect_mode mode);

	/**
	 * setup_sharpening - setup sharpening
	 * @ctx: Pointer to pipe context
	 * @cfg: Pointer to config structure
	 */
	void (*setup_sharpening)(struct dpu_hw_pipe *ctx,
			struct dpu_hw_sharp_cfg *cfg);

	/**
	 * setup_danger_safe_lut - setup danger safe LUTs
	 * @ctx: Pointer to pipe context
	 * @cfg: Pointer to pipe QoS configuration
	 *
	 */
	void (*setup_danger_safe_lut)(struct dpu_hw_pipe *ctx,
			struct dpu_hw_pipe_qos_cfg *cfg);

	/**
	 * setup_creq_lut - setup CREQ LUT
	 * @ctx: Pointer to pipe context
	 * @cfg: Pointer to pipe QoS configuration
	 *
	 */
	void (*setup_creq_lut)(struct dpu_hw_pipe *ctx,
			struct dpu_hw_pipe_qos_cfg *cfg);

	/**
	 * setup_qos_ctrl - setup QoS control
	 * @ctx: Pointer to pipe context
	 * @cfg: Pointer to pipe QoS configuration
	 *
	 */
	void (*setup_qos_ctrl)(struct dpu_hw_pipe *ctx,
			struct dpu_hw_pipe_qos_cfg *cfg);

	/**
	 * setup_histogram - setup histograms
	 * @ctx: Pointer to pipe context
	 * @cfg: Pointer to histogram configuration
	 */
	void (*setup_histogram)(struct dpu_hw_pipe *ctx,
			void *cfg);

	/**
	 * setup_scaler - setup scaler
	 * @ctx: Pointer to pipe context
	 * @pipe_cfg: Pointer to pipe configuration
	 * @pe_cfg: Pointer to pixel extension configuration
	 * @scaler_cfg: Pointer to scaler configuration
	 */
	void (*setup_scaler)(struct dpu_hw_pipe *ctx,
		struct dpu_hw_pipe_cfg *pipe_cfg,
		struct dpu_hw_pixel_ext *pe_cfg,
		void *scaler_cfg);

	/**
	 * get_scaler_ver - get scaler h/w version
	 * @ctx: Pointer to pipe context
	 */
	u32 (*get_scaler_ver)(struct dpu_hw_pipe *ctx);

	/**
	 * setup_cdp - setup client driven prefetch
	 * @ctx: Pointer to pipe context
	 * @cfg: Pointer to cdp configuration
	 */
	void (*setup_cdp)(struct dpu_hw_pipe *ctx,
			struct dpu_hw_pipe_cdp_cfg *cfg);
};

/**
 * struct dpu_hw_pipe - pipe description
 * @base: hardware block base structure
 * @hw: block hardware details
 * @catalog: back pointer to catalog
 * @mdp: pointer to associated mdp portion of the catalog
 * @idx: pipe index
 * @cap: pointer to layer_cfg
 * @ops: pointer to operations possible for this pipe
 */
struct dpu_hw_pipe {
	struct dpu_hw_blk base;
	struct dpu_hw_blk_reg_map hw;
	struct dpu_mdss_cfg *catalog;
	struct dpu_mdp_cfg *mdp;

	/* Pipe */
	enum dpu_sspp idx;
	const struct dpu_sspp_cfg *cap;

	/* Ops */
	struct dpu_hw_sspp_ops ops;
};

/**
 * dpu_hw_sspp_init - initializes the sspp hw driver object.
 * Should be called once before accessing every pipe.
 * @idx:  Pipe index for which driver object is required
 * @addr: Mapped register io address of MDP
 * @catalog : Pointer to mdss catalog data
 * @is_virtual_pipe: is this pipe virtual pipe
 */
struct dpu_hw_pipe *dpu_hw_sspp_init(enum dpu_sspp idx,
		void __iomem *addr, struct dpu_mdss_cfg *catalog,
		bool is_virtual_pipe);

/**
 * dpu_hw_sspp_destroy(): Destroys SSPP driver context
 * should be called during Hw pipe cleanup.
 * @ctx:  Pointer to SSPP driver context returned by dpu_hw_sspp_init
 */
void dpu_hw_sspp_destroy(struct dpu_hw_pipe *ctx);

#endif /*_DPU_HW_SSPP_H */

