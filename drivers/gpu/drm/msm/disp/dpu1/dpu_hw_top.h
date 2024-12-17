/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2015-2018, The Linux Foundation. All rights reserved.
 */

#ifndef _DPU_HW_TOP_H
#define _DPU_HW_TOP_H

#include "dpu_hw_catalog.h"
#include "dpu_hw_mdss.h"
#include "dpu_hw_util.h"

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
 * @en        : Enable/disable dual pipe configuration
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
	enum dpu_vsync_source vsync_source;
};

enum dpu_dp_phy_sel {
	DPU_DP_PHY_NONE,
	DPU_DP_PHY_0,
	DPU_DP_PHY_1,
	DPU_DP_PHY_2,
};

/**
 * struct dpu_hw_mdp_ops - interface to the MDP TOP Hw driver functions
 * Assumption is these functions will be called after clocks are enabled.
 * @setup_split_pipe : Programs the pipe control registers
 * @setup_pp_split : Programs the pp split control registers
 * @setup_traffic_shaper : programs traffic shaper control
 */
struct dpu_hw_mdp_ops {
	/** setup_split_pipe() : Registers are not double buffered, thisk
	 * function should be called before timing control enable
	 * @mdp  : mdp top context driver
	 * @cfg  : upper and lower part of pipe configuration
	 */
	void (*setup_split_pipe)(struct dpu_hw_mdp *mdp,
			struct split_pipe_cfg *p);

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
	 * dp_phy_intf_sel - configure intf to phy mapping
	 * @mdp: mdp top context driver
	 * @phys: list of phys the DP interfaces should be connected to. 0 disables the INTF.
	 */
	void (*dp_phy_intf_sel)(struct dpu_hw_mdp *mdp, enum dpu_dp_phy_sel phys[2]);

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
	const struct dpu_mdp_cfg *caps;

	/* ops */
	struct dpu_hw_mdp_ops ops;
};

struct dpu_hw_mdp *dpu_hw_mdptop_init(struct drm_device *dev,
				      const struct dpu_mdp_cfg *cfg,
				      void __iomem *addr,
				      const struct dpu_mdss_version *mdss_rev);

#endif /*_DPU_HW_TOP_H */
