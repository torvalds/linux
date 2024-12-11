/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * Copyright (c) 2015-2018, The Linux Foundation. All rights reserved.
 */

#ifndef _DPU_HW_INTF_H
#define _DPU_HW_INTF_H

#include "dpu_hw_catalog.h"
#include "dpu_hw_mdss.h"
#include "dpu_hw_util.h"

struct dpu_hw_intf;

/* intf timing settings */
struct dpu_hw_intf_timing_params {
	u32 width;		/* active width */
	u32 height;		/* active height */
	u32 xres;		/* Display panel width */
	u32 yres;		/* Display panel height */

	u32 h_back_porch;
	u32 h_front_porch;
	u32 v_back_porch;
	u32 v_front_porch;
	u32 hsync_pulse_width;
	u32 vsync_pulse_width;
	u32 hsync_polarity;
	u32 vsync_polarity;
	u32 border_clr;
	u32 underflow_clr;
	u32 hsync_skew;

	bool wide_bus_en;
	bool compression_en;
};

struct dpu_hw_intf_prog_fetch {
	u8 enable;
	/* vsync counter for the front porch pixel line */
	u32 fetch_start;
};

struct dpu_hw_intf_status {
	u8 is_en;		/* interface timing engine is enabled or not */
	u8 is_prog_fetch_en;	/* interface prog fetch counter is enabled or not */
	u32 frame_count;	/* frame count since timing engine enabled */
	u32 line_count;		/* current line count including blanking */
};

struct dpu_hw_intf_cmd_mode_cfg {
	u8 data_compress;	/* enable data compress between dpu and dsi */
	u8 wide_bus_en;		/* enable databus widen mode */
};

/**
 * struct dpu_hw_intf_ops : Interface to the interface Hw driver functions
 *  Assumption is these functions will be called after clocks are enabled
 * @ setup_timing_gen : programs the timing engine
 * @ setup_prog_fetch : enables/disables the programmable fetch logic
 * @ enable_timing: enable/disable timing engine
 * @ get_status: returns if timing engine is enabled or not
 * @ get_line_count: reads current vertical line counter
 * @bind_pingpong_blk: enable/disable the connection with pingpong which will
 *                     feed pixels to this interface
 * @setup_misr: enable/disable MISR
 * @collect_misr: read MISR signature
 * @enable_tearcheck:           Enables vsync generation and sets up init value of read
 *                              pointer and programs the tear check configuration
 * @disable_tearcheck:          Disables tearcheck block
 * @connect_external_te:        Read, modify, write to either set or clear listening to external TE
 *                              Return: 1 if TE was originally connected, 0 if not, or -ERROR
 * @get_vsync_info:             Provides the programmed and current line_count
 * @setup_autorefresh:          Configure and enable the autorefresh config
 * @get_autorefresh:            Retrieve autorefresh config from hardware
 *                              Return: 0 on success, -ETIMEDOUT on timeout
 * @vsync_sel:                  Select vsync signal for tear-effect configuration
 * @program_intf_cmd_cfg:       Program the DPU to interface datapath for command mode
 */
struct dpu_hw_intf_ops {
	void (*setup_timing_gen)(struct dpu_hw_intf *intf,
			const struct dpu_hw_intf_timing_params *p,
			const struct msm_format *fmt,
			const struct dpu_mdss_version *mdss_ver);

	void (*setup_prg_fetch)(struct dpu_hw_intf *intf,
			const struct dpu_hw_intf_prog_fetch *fetch);

	void (*enable_timing)(struct dpu_hw_intf *intf,
			u8 enable);

	void (*get_status)(struct dpu_hw_intf *intf,
			struct dpu_hw_intf_status *status);

	u32 (*get_line_count)(struct dpu_hw_intf *intf);

	void (*bind_pingpong_blk)(struct dpu_hw_intf *intf,
			const enum dpu_pingpong pp);
	void (*setup_misr)(struct dpu_hw_intf *intf);
	int (*collect_misr)(struct dpu_hw_intf *intf, u32 *misr_value);

	// Tearcheck on INTF since DPU 5.0.0

	int (*enable_tearcheck)(struct dpu_hw_intf *intf, struct dpu_hw_tear_check *cfg);

	int (*disable_tearcheck)(struct dpu_hw_intf *intf);

	int (*connect_external_te)(struct dpu_hw_intf *intf, bool enable_external_te);

	void (*vsync_sel)(struct dpu_hw_intf *intf, enum dpu_vsync_source vsync_source);

	/**
	 * Disable autorefresh if enabled
	 */
	void (*disable_autorefresh)(struct dpu_hw_intf *intf, uint32_t encoder_id, u16 vdisplay);

	void (*program_intf_cmd_cfg)(struct dpu_hw_intf *intf,
				     struct dpu_hw_intf_cmd_mode_cfg *cmd_mode_cfg);
};

struct dpu_hw_intf {
	struct dpu_hw_blk_reg_map hw;

	/* intf */
	enum dpu_intf idx;
	const struct dpu_intf_cfg *cap;

	/* ops */
	struct dpu_hw_intf_ops ops;
};

struct dpu_hw_intf *dpu_hw_intf_init(struct drm_device *dev,
				     const struct dpu_intf_cfg *cfg,
				     void __iomem *addr,
				     const struct dpu_mdss_version *mdss_rev);

#endif /*_DPU_HW_INTF_H */
