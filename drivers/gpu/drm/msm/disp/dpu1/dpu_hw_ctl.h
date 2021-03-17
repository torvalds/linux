/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2015-2018, The Linux Foundation. All rights reserved.
 */

#ifndef _DPU_HW_CTL_H
#define _DPU_HW_CTL_H

#include "dpu_hw_mdss.h"
#include "dpu_hw_util.h"
#include "dpu_hw_catalog.h"
#include "dpu_hw_sspp.h"
#include "dpu_hw_blk.h"

/**
 * dpu_ctl_mode_sel: Interface mode selection
 * DPU_CTL_MODE_SEL_VID:    Video mode interface
 * DPU_CTL_MODE_SEL_CMD:    Command mode interface
 */
enum dpu_ctl_mode_sel {
	DPU_CTL_MODE_SEL_VID = 0,
	DPU_CTL_MODE_SEL_CMD
};

struct dpu_hw_ctl;
/**
 * struct dpu_hw_stage_cfg - blending stage cfg
 * @stage : SSPP_ID at each stage
 * @multirect_index: index of the rectangle of SSPP.
 */
struct dpu_hw_stage_cfg {
	enum dpu_sspp stage[DPU_STAGE_MAX][PIPES_PER_STAGE];
	enum dpu_sspp_multirect_index multirect_index
					[DPU_STAGE_MAX][PIPES_PER_STAGE];
};

/**
 * struct dpu_hw_intf_cfg :Describes how the DPU writes data to output interface
 * @intf :                 Interface id
 * @mode_3d:               3d mux configuration
 * @intf_mode_sel:         Interface mode, cmd / vid
 * @stream_sel:            Stream selection for multi-stream interfaces
 */
struct dpu_hw_intf_cfg {
	enum dpu_intf intf;
	enum dpu_3d_blend_mode mode_3d;
	enum dpu_ctl_mode_sel intf_mode_sel;
	int stream_sel;
};

/**
 * struct dpu_hw_ctl_ops - Interface to the wb Hw driver functions
 * Assumption is these functions will be called after clocks are enabled
 */
struct dpu_hw_ctl_ops {
	/**
	 * kickoff hw operation for Sw controlled interfaces
	 * DSI cmd mode and WB interface are SW controlled
	 * @ctx       : ctl path ctx pointer
	 */
	void (*trigger_start)(struct dpu_hw_ctl *ctx);

	/**
	 * kickoff prepare is in progress hw operation for sw
	 * controlled interfaces: DSI cmd mode and WB interface
	 * are SW controlled
	 * @ctx       : ctl path ctx pointer
	 */
	void (*trigger_pending)(struct dpu_hw_ctl *ctx);

	/**
	 * Clear the value of the cached pending_flush_mask
	 * No effect on hardware
	 * @ctx       : ctl path ctx pointer
	 */
	void (*clear_pending_flush)(struct dpu_hw_ctl *ctx);

	/**
	 * Query the value of the cached pending_flush_mask
	 * No effect on hardware
	 * @ctx       : ctl path ctx pointer
	 */
	u32 (*get_pending_flush)(struct dpu_hw_ctl *ctx);

	/**
	 * OR in the given flushbits to the cached pending_flush_mask
	 * No effect on hardware
	 * @ctx       : ctl path ctx pointer
	 * @flushbits : module flushmask
	 */
	void (*update_pending_flush)(struct dpu_hw_ctl *ctx,
		u32 flushbits);

	/**
	 * OR in the given flushbits to the cached pending_intf_flush_mask
	 * No effect on hardware
	 * @ctx       : ctl path ctx pointer
	 * @flushbits : module flushmask
	 */
	void (*update_pending_intf_flush)(struct dpu_hw_ctl *ctx,
		u32 flushbits);

	/**
	 * Write the value of the pending_flush_mask to hardware
	 * @ctx       : ctl path ctx pointer
	 */
	void (*trigger_flush)(struct dpu_hw_ctl *ctx);

	/**
	 * Read the value of the flush register
	 * @ctx       : ctl path ctx pointer
	 * @Return: value of the ctl flush register.
	 */
	u32 (*get_flush_register)(struct dpu_hw_ctl *ctx);

	/**
	 * Setup ctl_path interface config
	 * @ctx
	 * @cfg    : interface config structure pointer
	 */
	void (*setup_intf_cfg)(struct dpu_hw_ctl *ctx,
		struct dpu_hw_intf_cfg *cfg);

	int (*reset)(struct dpu_hw_ctl *c);

	/*
	 * wait_reset_status - checks ctl reset status
	 * @ctx       : ctl path ctx pointer
	 *
	 * This function checks the ctl reset status bit.
	 * If the reset bit is set, it keeps polling the status till the hw
	 * reset is complete.
	 * Returns: 0 on success or -error if reset incomplete within interval
	 */
	int (*wait_reset_status)(struct dpu_hw_ctl *ctx);

	uint32_t (*get_bitmask_sspp)(struct dpu_hw_ctl *ctx,
		enum dpu_sspp blk);

	uint32_t (*get_bitmask_mixer)(struct dpu_hw_ctl *ctx,
		enum dpu_lm blk);

	uint32_t (*get_bitmask_dspp)(struct dpu_hw_ctl *ctx,
		enum dpu_dspp blk);

	/**
	 * Query the value of the intf flush mask
	 * No effect on hardware
	 * @ctx       : ctl path ctx pointer
	 */
	int (*get_bitmask_intf)(struct dpu_hw_ctl *ctx,
		u32 *flushbits,
		enum dpu_intf blk);

	/**
	 * Query the value of the intf active flush mask
	 * No effect on hardware
	 * @ctx       : ctl path ctx pointer
	 */
	int (*get_bitmask_active_intf)(struct dpu_hw_ctl *ctx,
		u32 *flushbits, enum dpu_intf blk);

	/**
	 * Set all blend stages to disabled
	 * @ctx       : ctl path ctx pointer
	 */
	void (*clear_all_blendstages)(struct dpu_hw_ctl *ctx);

	/**
	 * Configure layer mixer to pipe configuration
	 * @ctx       : ctl path ctx pointer
	 * @lm        : layer mixer enumeration
	 * @cfg       : blend stage configuration
	 */
	void (*setup_blendstage)(struct dpu_hw_ctl *ctx,
		enum dpu_lm lm, struct dpu_hw_stage_cfg *cfg);
};

/**
 * struct dpu_hw_ctl : CTL PATH driver object
 * @base: hardware block base structure
 * @hw: block register map object
 * @idx: control path index
 * @caps: control path capabilities
 * @mixer_count: number of mixers
 * @mixer_hw_caps: mixer hardware capabilities
 * @pending_flush_mask: storage for pending ctl_flush managed via ops
 * @pending_intf_flush_mask: pending INTF flush
 * @ops: operation list
 */
struct dpu_hw_ctl {
	struct dpu_hw_blk base;
	struct dpu_hw_blk_reg_map hw;

	/* ctl path */
	int idx;
	const struct dpu_ctl_cfg *caps;
	int mixer_count;
	const struct dpu_lm_cfg *mixer_hw_caps;
	u32 pending_flush_mask;
	u32 pending_intf_flush_mask;

	/* ops */
	struct dpu_hw_ctl_ops ops;
};

/**
 * dpu_hw_ctl - convert base object dpu_hw_base to container
 * @hw: Pointer to base hardware block
 * return: Pointer to hardware block container
 */
static inline struct dpu_hw_ctl *to_dpu_hw_ctl(struct dpu_hw_blk *hw)
{
	return container_of(hw, struct dpu_hw_ctl, base);
}

/**
 * dpu_hw_ctl_init(): Initializes the ctl_path hw driver object.
 * should be called before accessing every ctl path registers.
 * @idx:  ctl_path index for which driver object is required
 * @addr: mapped register io address of MDP
 * @m :   pointer to mdss catalog data
 */
struct dpu_hw_ctl *dpu_hw_ctl_init(enum dpu_ctl idx,
		void __iomem *addr,
		const struct dpu_mdss_cfg *m);

/**
 * dpu_hw_ctl_destroy(): Destroys ctl driver context
 * should be called to free the context
 */
void dpu_hw_ctl_destroy(struct dpu_hw_ctl *ctx);

#endif /*_DPU_HW_CTL_H */
