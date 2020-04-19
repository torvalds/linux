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
	uint32_t	hblank_cycles;
	uint32_t	vblank_cycles;
	uint32_t	pixels_per_clock;
	uint32_t	nr_of_frames;
	uint32_t	pixels_per_line;
	uint32_t	lines_per_frame;
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
		uint32_t R1;
		uint32_t G1;
		uint32_t B1;

		/* be used by CHBO only */
		uint32_t R2;
		uint32_t G2;
		uint32_t B2;
	} color_cfg;

	struct {
		uint32_t	h_mask;		/* horizontal mask */
		uint32_t	v_mask;		/* vertical mask */
		uint32_t	hv_mask;	/* horizontal+vertical mask? */
	} mask_cfg;

	struct {
		int32_t	h_delta;	/* horizontal delta? */
		int32_t	v_delta;	/* vertical delta? */
	} delta_cfg;

	sync_generator_cfg_t	 sync_gen_cfg;
};

/*
 * "pixelgen_prbs_cfg_t" duplicates parts of
 * prbs_cfg_t" in "input_system_global.h".
 */
typedef struct pixelgen_prbs_cfg_s pixelgen_prbs_cfg_t;
struct pixelgen_prbs_cfg_s {
	int32_t	seed0;
	int32_t	seed1;

	sync_generator_cfg_t	sync_gen_cfg;
};

/* end of Pixel-generator: TPG. ("pixelgen_global.h") */
#endif /* __PIXELGEN_GLOBAL_H_INCLUDED__ */

