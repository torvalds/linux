/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2015-2018, The Linux Foundation. All rights reserved.
 */

#ifndef _DPU_HW_SSPP_H
#define _DPU_HW_SSPP_H

#include "dpu_hw_catalog.h"
#include "dpu_hw_mdss.h"
#include "dpu_hw_util.h"
#include "dpu_formats.h"

struct dpu_hw_sspp;

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
#define DPU_SSPP_SCALER (BIT(DPU_SSPP_SCALER_RGB) | \
			 BIT(DPU_SSPP_SCALER_QSEED2) | \
			 BIT(DPU_SSPP_SCALER_QSEED3) | \
			 BIT(DPU_SSPP_SCALER_QSEED3LITE) | \
			 BIT(DPU_SSPP_SCALER_QSEED4))

/*
 * Define all CSC feature bits in catalog
 */
#define DPU_SSPP_CSC_ANY (BIT(DPU_SSPP_CSC) | \
			  BIT(DPU_SSPP_CSC_10BIT))

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
 * struct dpu_sw_pipe_cfg : software pipe configuration
 * @src_rect:  src ROI, caller takes into account the different operations
 *             such as decimation, flip etc to program this field
 * @dest_rect: destination ROI.
 */
struct dpu_sw_pipe_cfg {
	struct drm_rect src_rect;
	struct drm_rect dst_rect;
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
 * struct dpu_sw_pipe - software pipe description
 * @sspp:      backing SSPP pipe
 * @index:     index of the rectangle of SSPP
 * @mode:      parallel or time multiplex multirect mode
 */
struct dpu_sw_pipe {
	struct dpu_hw_sspp *sspp;
	enum dpu_sspp_multirect_index multirect_index;
	enum dpu_sspp_multirect_mode multirect_mode;
};

/**
 * struct dpu_hw_sspp_ops - interface to the SSPP Hw driver functions
 * Caller must call the init function to get the pipe context for each pipe
 * Assumption is these functions will be called after clocks are enabled
 */
struct dpu_hw_sspp_ops {
	/**
	 * setup_format - setup pixel format cropping rectangle, flip
	 * @pipe: Pointer to software pipe context
	 * @cfg: Pointer to pipe config structure
	 * @flags: Extra flags for format config
	 */
	void (*setup_format)(struct dpu_sw_pipe *pipe,
			     const struct dpu_format *fmt, u32 flags);

	/**
	 * setup_rects - setup pipe ROI rectangles
	 * @pipe: Pointer to software pipe context
	 * @cfg: Pointer to pipe config structure
	 */
	void (*setup_rects)(struct dpu_sw_pipe *pipe,
			    struct dpu_sw_pipe_cfg *cfg);

	/**
	 * setup_pe - setup pipe pixel extension
	 * @ctx: Pointer to pipe context
	 * @pe_ext: Pointer to pixel ext settings
	 */
	void (*setup_pe)(struct dpu_hw_sspp *ctx,
			struct dpu_hw_pixel_ext *pe_ext);

	/**
	 * setup_sourceaddress - setup pipe source addresses
	 * @pipe: Pointer to software pipe context
	 * @layout: format layout information for programming buffer to hardware
	 */
	void (*setup_sourceaddress)(struct dpu_sw_pipe *ctx,
				    struct dpu_hw_fmt_layout *layout);

	/**
	 * setup_csc - setup color space coversion
	 * @ctx: Pointer to pipe context
	 * @data: Pointer to config structure
	 */
	void (*setup_csc)(struct dpu_hw_sspp *ctx, const struct dpu_csc_cfg *data);

	/**
	 * setup_solidfill - enable/disable colorfill
	 * @pipe: Pointer to software pipe context
	 * @const_color: Fill color value
	 * @flags: Pipe flags
	 */
	void (*setup_solidfill)(struct dpu_sw_pipe *pipe, u32 color);

	/**
	 * setup_multirect - setup multirect configuration
	 * @pipe: Pointer to software pipe context
	 */

	void (*setup_multirect)(struct dpu_sw_pipe *pipe);

	/**
	 * setup_sharpening - setup sharpening
	 * @ctx: Pointer to pipe context
	 * @cfg: Pointer to config structure
	 */
	void (*setup_sharpening)(struct dpu_hw_sspp *ctx,
			struct dpu_hw_sharp_cfg *cfg);


	/**
	 * setup_qos_lut - setup QoS LUTs
	 * @ctx: Pointer to pipe context
	 * @cfg: LUT configuration
	 */
	void (*setup_qos_lut)(struct dpu_hw_sspp *ctx,
			struct dpu_hw_qos_cfg *cfg);

	/**
	 * setup_qos_ctrl - setup QoS control
	 * @ctx: Pointer to pipe context
	 * @danger_safe_en: flags controlling enabling of danger/safe QoS/LUT
	 */
	void (*setup_qos_ctrl)(struct dpu_hw_sspp *ctx,
			       bool danger_safe_en);

	/**
	 * setup_clk_force_ctrl - setup clock force control
	 * @ctx: Pointer to pipe context
	 * @enable: enable clock force if true
	 */
	bool (*setup_clk_force_ctrl)(struct dpu_hw_sspp *ctx,
				     bool enable);

	/**
	 * setup_histogram - setup histograms
	 * @ctx: Pointer to pipe context
	 * @cfg: Pointer to histogram configuration
	 */
	void (*setup_histogram)(struct dpu_hw_sspp *ctx,
			void *cfg);

	/**
	 * setup_scaler - setup scaler
	 * @scaler3_cfg: Pointer to scaler configuration
	 * @format: pixel format parameters
	 */
	void (*setup_scaler)(struct dpu_hw_sspp *ctx,
		struct dpu_hw_scaler3_cfg *scaler3_cfg,
		const struct dpu_format *format);

	/**
	 * get_scaler_ver - get scaler h/w version
	 * @ctx: Pointer to pipe context
	 */
	u32 (*get_scaler_ver)(struct dpu_hw_sspp *ctx);

	/**
	 * setup_cdp - setup client driven prefetch
	 * @pipe: Pointer to software pipe context
	 * @fmt: format used by the sw pipe
	 * @enable: whether the CDP should be enabled for this pipe
	 */
	void (*setup_cdp)(struct dpu_sw_pipe *pipe,
			  const struct dpu_format *fmt,
			  bool enable);
};

/**
 * struct dpu_hw_sspp - pipe description
 * @base: hardware block base structure
 * @hw: block hardware details
 * @ubwc: UBWC configuration data
 * @idx: pipe index
 * @cap: pointer to layer_cfg
 * @ops: pointer to operations possible for this pipe
 */
struct dpu_hw_sspp {
	struct dpu_hw_blk base;
	struct dpu_hw_blk_reg_map hw;
	const struct msm_mdss_data *ubwc;

	/* Pipe */
	enum dpu_sspp idx;
	const struct dpu_sspp_cfg *cap;

	/* Ops */
	struct dpu_hw_sspp_ops ops;
};

struct dpu_kms;
/**
 * dpu_hw_sspp_init() - Initializes the sspp hw driver object.
 * Should be called once before accessing every pipe.
 * @cfg:  Pipe catalog entry for which driver object is required
 * @addr: Mapped register io address of MDP
 * @mdss_data: UBWC / MDSS configuration data
 * @mdss_rev: dpu core's major and minor versions
 */
struct dpu_hw_sspp *dpu_hw_sspp_init(const struct dpu_sspp_cfg *cfg,
		void __iomem *addr, const struct msm_mdss_data *mdss_data,
		const struct dpu_mdss_version *mdss_rev);

/**
 * dpu_hw_sspp_destroy(): Destroys SSPP driver context
 * should be called during Hw pipe cleanup.
 * @ctx:  Pointer to SSPP driver context returned by dpu_hw_sspp_init
 */
void dpu_hw_sspp_destroy(struct dpu_hw_sspp *ctx);

int _dpu_hw_sspp_init_debugfs(struct dpu_hw_sspp *hw_pipe, struct dpu_kms *kms,
			      struct dentry *entry);

#endif /*_DPU_HW_SSPP_H */

