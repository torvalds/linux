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

#ifndef _DPU_HW_TOP_H
#define _DPU_HW_TOP_H

#include "dpu_hw_catalog.h"
#include "dpu_hw_mdss.h"
#include "dpu_hw_util.h"
#include "dpu_hw_blk.h"

struct dpu_hw_mdp;

/**
 * struct traffic_shaper_cfg: traffic shaper configuration
 * @en        : enable/disable traffic shaper
 * @rd_client : true if read client; false if write client
 * @client_id : client identifier
 * @bpc_denom : denominator of byte per clk
 * @bpc_numer : numerator of byte per clk
 */
struct traffic_shaper_cfg {
	bool en;
	bool rd_client;
	u32 client_id;
	u32 bpc_denom;
	u64 bpc_numer;
};

/**
 * struct split_pipe_cfg - pipe configuration for dual display panels
 * @en        : Enable/disable dual pipe confguration
 * @mode      : Panel interface mode
 * @intf      : Interface id for main control path
 * @split_flush_en: Allows both the paths to be flushed when master path is
 *              flushed
 */
struct split_pipe_cfg {
	bool en;
	enum dpu_intf_mode mode;
	enum dpu_intf intf;
	bool split_flush_en;
};

/**
 * struct cdm_output_cfg: output configuration for cdm
 * @intf_en   : enable/disable interface output
 */
struct cdm_output_cfg {
	bool intf_en;
};

/**
 * struct dpu_danger_safe_status: danger and safe status signals
 * @mdp: top level status
 * @sspp: source pipe status
 */
struct dpu_danger_safe_status {
	u8 mdp;
	u8 sspp[SSPP_MAX];
};

/**
 * struct dpu_vsync_source_cfg - configure vsync source and configure the
 *                                    watchdog timers if required.
 * @pp_count: number of ping pongs active
 * @frame_rate: Display frame rate
 * @ppnumber: ping pong index array
 * @vsync_source: vsync source selection
 */
struct dpu_vsync_source_cfg {
	u32 pp_count;
	u32 frame_rate;
	u32 ppnumber[PINGPONG_MAX];
	u32 vsync_source;
};

/**
 * struct dpu_hw_mdp_ops - interface to the MDP TOP Hw driver functions
 * Assumption is these functions will be called after clocks are enabled.
 * @setup_split_pipe : Programs the pipe control registers
 * @setup_pp_split : Programs the pp split control registers
 * @setup_cdm_output : programs cdm control
 * @setup_traffic_shaper : programs traffic shaper control
 */
struct dpu_hw_mdp_ops {
	/** setup_split_pipe() : Regsiters are not double buffered, thisk
	 * function should be called before timing control enable
	 * @mdp  : mdp top context driver
	 * @cfg  : upper and lower part of pipe configuration
	 */
	void (*setup_split_pipe)(struct dpu_hw_mdp *mdp,
			struct split_pipe_cfg *p);

	/**
	 * setup_cdm_output() : Setup selection control of the cdm data path
	 * @mdp  : mdp top context driver
	 * @cfg  : cdm output configuration
	 */
	void (*setup_cdm_output)(struct dpu_hw_mdp *mdp,
			struct cdm_output_cfg *cfg);

	/**
	 * setup_traffic_shaper() : Setup traffic shaper control
	 * @mdp  : mdp top context driver
	 * @cfg  : traffic shaper configuration
	 */
	void (*setup_traffic_shaper)(struct dpu_hw_mdp *mdp,
			struct traffic_shaper_cfg *cfg);

	/**
	 * setup_clk_force_ctrl - set clock force control
	 * @mdp: mdp top context driver
	 * @clk_ctrl: clock to be controlled
	 * @enable: force on enable
	 * @return: if the clock is forced-on by this function
	 */
	bool (*setup_clk_force_ctrl)(struct dpu_hw_mdp *mdp,
			enum dpu_clk_ctrl_type clk_ctrl, bool enable);

	/**
	 * get_danger_status - get danger status
	 * @mdp: mdp top context driver
	 * @status: Pointer to danger safe status
	 */
	void (*get_danger_status)(struct dpu_hw_mdp *mdp,
			struct dpu_danger_safe_status *status);

	/**
	 * setup_vsync_source - setup vsync source configuration details
	 * @mdp: mdp top context driver
	 * @cfg: vsync source selection configuration
	 */
	void (*setup_vsync_source)(struct dpu_hw_mdp *mdp,
				struct dpu_vsync_source_cfg *cfg);

	/**
	 * get_safe_status - get safe status
	 * @mdp: mdp top context driver
	 * @status: Pointer to danger safe status
	 */
	void (*get_safe_status)(struct dpu_hw_mdp *mdp,
			struct dpu_danger_safe_status *status);

	/**
	 * reset_ubwc - reset top level UBWC configuration
	 * @mdp: mdp top context driver
	 * @m: pointer to mdss catalog data
	 */
	void (*reset_ubwc)(struct dpu_hw_mdp *mdp, struct dpu_mdss_cfg *m);

	/**
	 * intf_audio_select - select the external interface for audio
	 * @mdp: mdp top context driver
	 */
	void (*intf_audio_select)(struct dpu_hw_mdp *mdp);
};

struct dpu_hw_mdp {
	struct dpu_hw_blk base;
	struct dpu_hw_blk_reg_map hw;

	/* top */
	enum dpu_mdp idx;
	const struct dpu_mdp_cfg *caps;

	/* ops */
	struct dpu_hw_mdp_ops ops;
};

/**
 * to_dpu_hw_mdp - convert base object dpu_hw_base to container
 * @hw: Pointer to base hardware block
 * return: Pointer to hardware block container
 */
static inline struct dpu_hw_mdp *to_dpu_hw_mdp(struct dpu_hw_blk *hw)
{
	return container_of(hw, struct dpu_hw_mdp, base);
}

/**
 * dpu_hw_mdptop_init - initializes the top driver for the passed idx
 * @idx:  Interface index for which driver object is required
 * @addr: Mapped register io address of MDP
 * @m:    Pointer to mdss catalog data
 */
struct dpu_hw_mdp *dpu_hw_mdptop_init(enum dpu_mdp idx,
		void __iomem *addr,
		const struct dpu_mdss_cfg *m);

void dpu_hw_mdp_destroy(struct dpu_hw_mdp *mdp);

#endif /*_DPU_HW_TOP_H */
