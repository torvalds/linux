/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023, The Linux Foundation. All rights reserved.
 */

#ifndef _DPU_HW_CDM_H
#define _DPU_HW_CDM_H

#include "dpu_hw_mdss.h"
#include "dpu_hw_top.h"

struct dpu_hw_cdm;

/**
 * struct dpu_hw_cdm_cfg : current configuration of CDM block
 *
 *  @output_width:         output ROI width of CDM block
 *  @output_height:        output ROI height of CDM block
 *  @output_bit_depth:     output bit-depth of CDM block
 *  @h_cdwn_type:          downsample type used for horizontal pixels
 *  @v_cdwn_type:          downsample type used for vertical pixels
 *  @output_fmt:           handle to msm_format of CDM block
 *  @csc_cfg:              handle to CSC matrix programmed for CDM block
 *  @output_type:          interface to which CDM is paired (HDMI/WB)
 *  @pp_id:                ping-pong block to which CDM is bound to
 */
struct dpu_hw_cdm_cfg {
	u32 output_width;
	u32 output_height;
	u32 output_bit_depth;
	u32 h_cdwn_type;
	u32 v_cdwn_type;
	const struct msm_format *output_fmt;
	const struct dpu_csc_cfg *csc_cfg;
	u32 output_type;
	int pp_id;
};

/*
 * These values are used indicate which type of downsample is used
 * in the horizontal/vertical direction for the CDM block.
 */
enum dpu_hw_cdwn_type {
	CDM_CDWN_DISABLE,
	CDM_CDWN_PIXEL_DROP,
	CDM_CDWN_AVG,
	CDM_CDWN_COSITE,
	CDM_CDWN_OFFSITE,
};

/*
 * CDM block can be paired with WB or HDMI block. These values match
 * the input with which the CDM block is paired.
 */
enum dpu_hw_cdwn_output_type {
	CDM_CDWN_OUTPUT_HDMI,
	CDM_CDWN_OUTPUT_WB,
};

/*
 * CDM block can give an 8-bit or 10-bit output. These values
 * are used to indicate the output bit depth of CDM block
 */
enum dpu_hw_cdwn_output_bit_depth {
	CDM_CDWN_OUTPUT_8BIT,
	CDM_CDWN_OUTPUT_10BIT,
};

/*
 * CDM block can downsample using different methods. These values
 * are used to indicate the downsample method which can be used
 * either in the horizontal or vertical direction.
 */
enum dpu_hw_cdwn_op_mode_method_h_v {
	CDM_CDWN2_METHOD_PIXEL_DROP,
	CDM_CDWN2_METHOD_AVG,
	CDM_CDWN2_METHOD_COSITE,
	CDM_CDWN2_METHOD_OFFSITE
};

/**
 * struct dpu_hw_cdm_ops : Interface to the chroma down Hw driver functions
 *                         Assumption is these functions will be called after
 *                         clocks are enabled
 *  @enable:               Enables the output to interface and programs the
 *                         output packer
 *  @bind_pingpong_blk:    enable/disable the connection with pingpong which
 *                         will feed pixels to this cdm
 */
struct dpu_hw_cdm_ops {
	/**
	 * Enable the CDM module
	 * @cdm         Pointer to chroma down context
	 */
	int (*enable)(struct dpu_hw_cdm *cdm, struct dpu_hw_cdm_cfg *cfg);

	/**
	 * Enable/disable the connection with pingpong
	 * @cdm         Pointer to chroma down context
	 * @pp          pingpong block id.
	 */
	void (*bind_pingpong_blk)(struct dpu_hw_cdm *cdm, const enum dpu_pingpong pp);
};

/**
 * struct dpu_hw_cdm - cdm description
 * @base: Hardware block base structure
 * @hw: Block hardware details
 * @idx: CDM index
 * @caps: Pointer to cdm_cfg
 * @ops: handle to operations possible for this CDM
 */
struct dpu_hw_cdm {
	struct dpu_hw_blk base;
	struct dpu_hw_blk_reg_map hw;

	/* chroma down */
	const struct dpu_cdm_cfg *caps;
	enum  dpu_cdm  idx;

	/* ops */
	struct dpu_hw_cdm_ops ops;
};

/**
 * dpu_hw_cdm_init - initializes the cdm hw driver object.
 * should be called once before accessing every cdm.
 * @dev: DRM device handle
 * @cdm: CDM catalog entry for which driver object is required
 * @addr :   mapped register io address of MDSS
 * @mdss_rev: mdss hw core revision
 */
struct dpu_hw_cdm *dpu_hw_cdm_init(struct drm_device *dev,
				   const struct dpu_cdm_cfg *cdm, void __iomem *addr,
				   const struct dpu_mdss_version *mdss_rev);

static inline struct dpu_hw_cdm *to_dpu_hw_cdm(struct dpu_hw_blk *hw)
{
	return container_of(hw, struct dpu_hw_cdm, base);
}

#endif /*_DPU_HW_CDM_H */
