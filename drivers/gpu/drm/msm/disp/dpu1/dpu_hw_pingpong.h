/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2015-2018, The Linux Foundation. All rights reserved.
 */

#ifndef _DPU_HW_PINGPONG_H
#define _DPU_HW_PINGPONG_H

#include "dpu_hw_catalog.h"
#include "dpu_hw_mdss.h"
#include "dpu_hw_util.h"

#define DITHER_MATRIX_SZ 16

struct dpu_hw_pingpong;

/**
 * struct dpu_hw_dither_cfg - dither feature structure
 * @flags: for customizing operations
 * @temporal_en: temperal dither enable
 * @c0_bitdepth: c0 component bit depth
 * @c1_bitdepth: c1 component bit depth
 * @c2_bitdepth: c2 component bit depth
 * @c3_bitdepth: c2 component bit depth
 * @matrix: dither strength matrix
 */
struct dpu_hw_dither_cfg {
	u64 flags;
	u32 temporal_en;
	u32 c0_bitdepth;
	u32 c1_bitdepth;
	u32 c2_bitdepth;
	u32 c3_bitdepth;
	u32 matrix[DITHER_MATRIX_SZ];
};

/**
 *
 * struct dpu_hw_pingpong_ops : Interface to the pingpong Hw driver functions
 *  Assumption is these functions will be called after clocks are enabled
 *  @enable_tearcheck: program and enable tear check block
 *  @disable_tearcheck: disable able tear check block
 *  @setup_dither : function to program the dither hw block
 *  @get_line_count: obtain current vertical line counter
 */
struct dpu_hw_pingpong_ops {
	/**
	 * enables vysnc generation and sets up init value of
	 * read pointer and programs the tear check cofiguration
	 */
	int (*enable_tearcheck)(struct dpu_hw_pingpong *pp,
			struct dpu_hw_tear_check *cfg);

	/**
	 * disables tear check block
	 */
	int (*disable_tearcheck)(struct dpu_hw_pingpong *pp);

	/**
	 * read, modify, write to either set or clear listening to external TE
	 * @Return: 1 if TE was originally connected, 0 if not, or -ERROR
	 */
	int (*connect_external_te)(struct dpu_hw_pingpong *pp,
			bool enable_external_te);

	/**
	 * Obtain current vertical line counter
	 */
	u32 (*get_line_count)(struct dpu_hw_pingpong *pp);

	/**
	 * Disable autorefresh if enabled
	 */
	void (*disable_autorefresh)(struct dpu_hw_pingpong *pp, uint32_t encoder_id, u16 vdisplay);

	/**
	 * Setup dither matix for pingpong block
	 */
	void (*setup_dither)(struct dpu_hw_pingpong *pp,
			struct dpu_hw_dither_cfg *cfg);
	/**
	 * Enable DSC
	 */
	int (*enable_dsc)(struct dpu_hw_pingpong *pp);

	/**
	 * Disable DSC
	 */
	void (*disable_dsc)(struct dpu_hw_pingpong *pp);

	/**
	 * Setup DSC
	 */
	int (*setup_dsc)(struct dpu_hw_pingpong *pp);
};

struct dpu_hw_merge_3d;

struct dpu_hw_pingpong {
	struct dpu_hw_blk base;
	struct dpu_hw_blk_reg_map hw;

	/* pingpong */
	enum dpu_pingpong idx;
	const struct dpu_pingpong_cfg *caps;
	struct dpu_hw_merge_3d *merge_3d;

	/* ops */
	struct dpu_hw_pingpong_ops ops;
};

/**
 * to_dpu_hw_pingpong - convert base object dpu_hw_base to container
 * @hw: Pointer to base hardware block
 * return: Pointer to hardware block container
 */
static inline struct dpu_hw_pingpong *to_dpu_hw_pingpong(struct dpu_hw_blk *hw)
{
	return container_of(hw, struct dpu_hw_pingpong, base);
}

/**
 * dpu_hw_pingpong_init() - initializes the pingpong driver for the passed
 * pingpong catalog entry.
 * @cfg:  Pingpong catalog entry for which driver object is required
 * @addr: Mapped register io address of MDP
 * @mdss_rev: dpu core's major and minor versions
 * Return: Error code or allocated dpu_hw_pingpong context
 */
struct dpu_hw_pingpong *dpu_hw_pingpong_init(const struct dpu_pingpong_cfg *cfg,
		void __iomem *addr, const struct dpu_mdss_version *mdss_rev);

/**
 * dpu_hw_pingpong_destroy - destroys pingpong driver context
 *	should be called to free the context
 * @pp:   Pointer to PP driver context returned by dpu_hw_pingpong_init
 */
void dpu_hw_pingpong_destroy(struct dpu_hw_pingpong *pp);

#endif /*_DPU_HW_PINGPONG_H */
