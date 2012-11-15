/*
 * SDRC register values for the Numonyx M65KXXXXAM
 *
 * Copyright (C) 2009 Integration Software and Electronic Engineering.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ARCH_ARM_MACH_OMAP2_SDRAM_NUMONYX_M65KXXXXAM
#define __ARCH_ARM_MACH_OMAP2_SDRAM_NUMONYX_M65KXXXXAM

#include "sdrc.h"

/* Numonyx  M65KXXXXAM */
static struct omap_sdrc_params m65kxxxxam_sdrc_params[] = {
	[0] = {
		.rate		= 200000000,
		.actim_ctrla	= 0xe321d4c6,
		.actim_ctrlb	= 0x00022328,
		.rfr_ctrl	= 0x0005e601,
		.mr		= 0x00000032,
	},
	[1] = {
		.rate		= 166000000,
		.actim_ctrla	= 0xba9dc485,
		.actim_ctrlb	= 0x00022321,
		.rfr_ctrl	= 0x0004dc01,
		.mr		= 0x00000032,
	},
	[2] = {
		.rate		= 133000000,
		.actim_ctrla	= 0x9a19b485,
		.actim_ctrlb	= 0x0002231b,
		.rfr_ctrl	= 0x0003de01,
		.mr		= 0x00000032,
	},
	[3] = {
		.rate		= 83000000,
		.actim_ctrla	= 0x594ca242,
		.actim_ctrlb	= 0x00022310,
		.rfr_ctrl	= 0x00025501,
		.mr		= 0x00000032,
	},
	[4] = {
		.rate			= 0
	},
};

#endif
