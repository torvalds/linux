/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#ifndef __PIXELGEN_LOCAL_H_INCLUDED__
#define __PIXELGEN_LOCAL_H_INCLUDED__

#include "pixelgen_global.h"

typedef struct pixelgen_ctrl_state_s	pixelgen_ctrl_state_t;
struct pixelgen_ctrl_state_s {
	hrt_data	com_enable;
	hrt_data	prbs_rstval0;
	hrt_data	prbs_rstval1;
	hrt_data	syng_sid;
	hrt_data	syng_free_run;
	hrt_data	syng_pause;
	hrt_data	syng_nof_frames;
	hrt_data	syng_nof_pixels;
	hrt_data	syng_nof_line;
	hrt_data	syng_hblank_cyc;
	hrt_data	syng_vblank_cyc;
	hrt_data	syng_stat_hcnt;
	hrt_data	syng_stat_vcnt;
	hrt_data	syng_stat_fcnt;
	hrt_data	syng_stat_done;
	hrt_data	tpg_mode;
	hrt_data	tpg_hcnt_mask;
	hrt_data	tpg_vcnt_mask;
	hrt_data	tpg_xycnt_mask;
	hrt_data	tpg_hcnt_delta;
	hrt_data	tpg_vcnt_delta;
	hrt_data	tpg_r1;
	hrt_data	tpg_g1;
	hrt_data	tpg_b1;
	hrt_data	tpg_r2;
	hrt_data	tpg_g2;
	hrt_data	tpg_b2;
};
#endif /* __PIXELGEN_LOCAL_H_INCLUDED__ */
