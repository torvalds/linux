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

#ifndef _DPU_HW_CDM_H
#define _DPU_HW_CDM_H

#include "dpu_hw_mdss.h"
#include "dpu_hw_top.h"
#include "dpu_hw_blk.h"

struct dpu_hw_cdm;

struct dpu_hw_cdm_cfg {
	u32 output_width;
	u32 output_height;
	u32 output_bit_depth;
	u32 h_cdwn_type;
	u32 v_cdwn_type;
	const struct dpu_format *output_fmt;
	u32 output_type;
	int flags;
};

enum dpu_hw_cdwn_type {
	CDM_CDWN_DISABLE,
	CDM_CDWN_PIXEL_DROP,
	CDM_CDWN_AVG,
	CDM_CDWN_COSITE,
	CDM_CDWN_OFFSITE,
};

enum dpu_hw_cdwn_output_type {
	CDM_CDWN_OUTPUT_HDMI,
	CDM_CDWN_OUTPUT_WB,
};

enum dpu_hw_cdwn_output_bit_depth {
	CDM_CDWN_OUTPUT_8BIT,
	CDM_CDWN_OUTPUT_10BIT,
};

/**
 * struct dpu_hw_cdm_ops : Interface to the chroma down Hw driver functions
 *                         Assumption is these functions will be called after
 *                         clocks are enabled
 *  @setup_csc:            Programs the csc matrix
 *  @setup_cdwn:           Sets up the chroma down sub module
 *  @enable:               Enables the output to interface and programs the
 *                         output packer
 *  @disable:              Puts the cdm in bypass mode
 */
struct dpu_hw_cdm_ops {
	/**
	 * Programs the CSC matrix for conversion from RGB space to YUV space,
	 * it is optional to call this function as this matrix is automatically
	 * set during initialization, user should call this if it wants
	 * to program a different matrix than default matrix.
	 * @cdm:          Pointer to the chroma down context structure
	 * @data          Pointer to CSC configuration data
	 * return:        0 if success; error code otherwise
	 */
	int (*setup_csc_data)(struct dpu_hw_cdm *cdm,
			struct dpu_csc_cfg *data);

	/**
	 * Programs the Chroma downsample part.
	 * @cdm         Pointer to chroma down context
	 */
	int (*setup_cdwn)(struct dpu_hw_cdm *cdm,
	struct dpu_hw_cdm_cfg *cfg);

	/**
	 * Enable the CDM module
	 * @cdm         Pointer to chroma down context
	 */
	int (*enable)(struct dpu_hw_cdm *cdm,
	struct dpu_hw_cdm_cfg *cfg);

	/**
	 * Disable the CDM module
	 * @cdm         Pointer to chroma down context
	 */
	void (*disable)(struct dpu_hw_cdm *cdm);
};

struct dpu_hw_cdm {
	struct dpu_hw_blk base;
	struct dpu_hw_blk_reg_map hw;

	/* chroma down */
	const struct dpu_cdm_cfg *caps;
	enum  dpu_cdm  idx;

	/* mdp top hw driver */
	struct dpu_hw_mdp *hw_mdp;

	/* ops */
	struct dpu_hw_cdm_ops ops;
};

/**
 * dpu_hw_cdm - convert base object dpu_hw_base to container
 * @hw: Pointer to base hardware block
 * return: Pointer to hardware block container
 */
static inline struct dpu_hw_cdm *to_dpu_hw_cdm(struct dpu_hw_blk *hw)
{
	return container_of(hw, struct dpu_hw_cdm, base);
}

/**
 * dpu_hw_cdm_init - initializes the cdm hw driver object.
 * should be called once before accessing every cdm.
 * @idx:  cdm index for which driver object is required
 * @addr: mapped register io address of MDP
 * @m :   pointer to mdss catalog data
 * @hw_mdp:  pointer to mdp top hw driver object
 */
struct dpu_hw_cdm *dpu_hw_cdm_init(enum dpu_cdm idx,
		void __iomem *addr,
		struct dpu_mdss_cfg *m,
		struct dpu_hw_mdp *hw_mdp);

/**
 * dpu_hw_cdm_destroy - destroys CDM driver context
 * @cdm:   pointer to CDM driver context
 */
void dpu_hw_cdm_destroy(struct dpu_hw_cdm *cdm);

#endif /*_DPU_HW_CDM_H */
