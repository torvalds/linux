/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2015-2018, The Linux Foundation. All rights reserved.
 */

#ifndef _DPU_HW_PINGPONG_H
#define _DPU_HW_PINGPONG_H

#include "dpu_hw_catalog.h"
#include "dpu_hw_mdss.h"
#include "dpu_hw_util.h"
#include "dpu_hw_blk.h"

#define DITHER_MATRIX_SZ 16

struct dpu_hw_pingpong;

struct dpu_hw_tear_check {
	/*
	 * This is ratio of MDP VSYNC clk freq(Hz) to
	 * refresh rate divided by no of lines
	 */
	u32 vsync_count;
	u32 sync_cfg_height;
	u32 vsync_init_val;
	u32 sync_threshold_start;
	u32 sync_threshold_continue;
	u32 start_pos;
	u32 rd_ptr_irq;
	u8 hw_vsync_mode;
};

struct dpu_hw_pp_vsync_info {
	u32 rd_ptr_init_val;	/* value of rd pointer at vsync edge */
	u32 rd_ptr_frame_count;	/* num frames sent since enabling interface */
	u32 rd_ptr_line_count;	/* current line on panel (rd ptr) */
	u32 wr_ptr_line_count;	/* current line within pp fifo (wr ptr) */
};

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
 *  @setup_tearcheck : program tear check values
 *  @enable_tearcheck : enables tear check
 *  @get_vsync_info : retries timing info of the panel
 *  @setup_autorefresh : configure and enable the autorefresh config
 *  @get_autorefresh : retrieve autorefresh config from hardware
 *  @setup_dither : function to program the dither hw block
 *  @get_line_count: obtain current vertical line counter
 */
struct dpu_hw_pingpong_ops {
	/**
	 * enables vysnc generation and sets up init value of
	 * read pointer and programs the tear check cofiguration
	 */
	int (*setup_tearcheck)(struct dpu_hw_pingpong *pp,
			struct dpu_hw_tear_check *cfg);

	/**
	 * enables tear check block
	 */
	int (*enable_tearcheck)(struct dpu_hw_pingpong *pp,
			bool enable);

	/**
	 * read, modify, write to either set or clear listening to external TE
	 * @Return: 1 if TE was originally connected, 0 if not, or -ERROR
	 */
	int (*connect_external_te)(struct dpu_hw_pingpong *pp,
			bool enable_external_te);

	/**
	 * provides the programmed and current
	 * line_count
	 */
	int (*get_vsync_info)(struct dpu_hw_pingpong *pp,
			struct dpu_hw_pp_vsync_info  *info);

	/**
	 * configure and enable the autorefresh config
	 */
	void (*setup_autorefresh)(struct dpu_hw_pingpong *pp,
				  u32 frame_count, bool enable);

	/**
	 * retrieve autorefresh config from hardware
	 */
	bool (*get_autorefresh)(struct dpu_hw_pingpong *pp,
				u32 *frame_count);

	/**
	 * poll until write pointer transmission starts
	 * @Return: 0 on success, -ETIMEDOUT on timeout
	 */
	int (*poll_timeout_wr_ptr)(struct dpu_hw_pingpong *pp, u32 timeout_us);

	/**
	 * Obtain current vertical line counter
	 */
	u32 (*get_line_count)(struct dpu_hw_pingpong *pp);

	/**
	 * Setup dither matix for pingpong block
	 */
	void (*setup_dither)(struct dpu_hw_pingpong *pp,
			struct dpu_hw_dither_cfg *cfg);
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
 * dpu_hw_pingpong_init - initializes the pingpong driver for the passed
 *	pingpong idx.
 * @idx:  Pingpong index for which driver object is required
 * @addr: Mapped register io address of MDP
 * @m:    Pointer to mdss catalog data
 * Returns: Error code or allocated dpu_hw_pingpong context
 */
struct dpu_hw_pingpong *dpu_hw_pingpong_init(enum dpu_pingpong idx,
		void __iomem *addr,
		const struct dpu_mdss_cfg *m);

/**
 * dpu_hw_pingpong_destroy - destroys pingpong driver context
 *	should be called to free the context
 * @pp:   Pointer to PP driver context returned by dpu_hw_pingpong_init
 */
void dpu_hw_pingpong_destroy(struct dpu_hw_pingpong *pp);

#endif /*_DPU_HW_PINGPONG_H */
