/* SPDX-License-Identifier: GPL-2.0 */
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

#ifndef __PIXELGEN_GLOBAL_H_INCLUDED__
#define __PIXELGEN_GLOBAL_H_INCLUDED__

#include <type_support.h>

/**
 * Pixel-generator. ("pixelgen_global.h")
 */
/*
 * Duplicates "sync_generator_cfg_t" in "input_system_global.h".
 */
typedef struct sync_generator_cfg_s sync_generator_cfg_t;
struct sync_generator_cfg_s {
	u32	hblank_cycles;
	u32	vblank_cycles;
	u32	pixels_per_clock;
	u32	nr_of_frames;
	u32	pixels_per_line;
	u32	lines_per_frame;
};

typedef enum {
	PIXELGEN_TPG_MODE_RAMP = 0,
	PIXELGEN_TPG_MODE_CHBO,
	PIXELGEN_TPG_MODE_MONO,
	N_PIXELGEN_TPG_MODE
} pixelgen_tpg_mode_t;

/*
 * "pixelgen_tpg_cfg_t" duplicates parts of
 * "tpg_cfg_t" in "input_system_global.h".
 */
typedef struct pixelgen_tpg_cfg_s pixelgen_tpg_cfg_t;
struct pixelgen_tpg_cfg_s {
	pixelgen_tpg_mode_t	mode;	/* CHBO, MONO */

	struct {
		/* be used by CHBO and MON */
		u32 R1;
		u32 G1;
		u32 B1;

		/* be used by CHBO only */
		u32 R2;
		u32 G2;
		u32 B2;
	} color_cfg;

	struct {
		u32	h_mask;		/* horizontal mask */
		u32	v_mask;		/* vertical mask */
		u32	hv_mask;	/* horizontal+vertical mask? */
	} mask_cfg;

	struct {
		s32	h_delta;	/* horizontal delta? */
		s32	v_delta;	/* vertical delta? */
	} delta_cfg;

	sync_generator_cfg_t	 sync_gen_cfg;
};

/*
 * "pixelgen_prbs_cfg_t" duplicates parts of
 * prbs_cfg_t" in "input_system_global.h".
 */
typedef struct pixelgen_prbs_cfg_s pixelgen_prbs_cfg_t;
struct pixelgen_prbs_cfg_s {
	s32	seed0;
	s32	seed1;

	sync_generator_cfg_t	sync_gen_cfg;
};

/* end of Pixel-generator: TPG. ("pixelgen_global.h") */
#endif /* __PIXELGEN_GLOBAL_H_INCLUDED__ */
