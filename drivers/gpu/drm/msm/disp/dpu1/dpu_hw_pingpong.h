/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2015-2018, The Linux Foundation. All rights reserved.
 */

#ifndef _DPU_HW_PINGPONG_H
#define _DPU_HW_PINGPONG_H

#include "dpu_hw_catalog.h"
#include "dpu_hw_mdss.h"
#include "dpu_hw_util.h"
#include "dpu_hw_blk.h"

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
 *
 * struct dpu_hw_pingpong_ops : Interface to the pingpong Hw driver functions
 *  Assumption is these functions will be called after clocks are enabled
 *  @setup_tearcheck : program tear check values
 *  @enable_tearcheck : enables tear check
 *  @get_vsync_info : retries timing info of the panel
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
	 * poll until write pointer transmission starts
	 * @Return: 0 on success, -ETIMEDOUT on timeout
	 */
	int (*poll_timeout_wr_ptr)(struct dpu_hw_pingpong *pp, u32 timeout_us);

	/**
	 * Obtain current vertical line counter
	 */
	u32 (*get_line_count)(struct dpu_hw_pingpong *pp);
};

struct dpu_hw_pingpong {
	struct dpu_hw_blk base;
	struct dpu_hw_blk_reg_map hw;

	/* pingpong */
	enum dpu_pingpong idx;
	const struct dpu_pingpong_cfg *caps;

	/* ops */
	struct dpu_hw_pingpong_ops ops;
};

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
		struct dpu_mdss_cfg *m);

/**
 * dpu_hw_pingpong_destroy - destroys pingpong driver context
 *	should be called to free the context
 * @pp:   Pointer to PP driver context returned by dpu_hw_pingpong_init
 */
void dpu_hw_pingpong_destroy(struct dpu_hw_pingpong *pp);

#endif /*_DPU_HW_PINGPONG_H */
