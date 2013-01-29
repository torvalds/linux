/*
 * SDRC register values for the Qimonda HYB18M512160AF-6
 *
 * Copyright (C) 2008-2009 Texas Instruments, Inc.
 * Copyright (C) 2008-2009 Nokia Corporation
 *
 * Paul Walmsley
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef ARCH_ARM_MACH_OMAP2_SDRAM_QIMONDA_HYB18M512160AF6
#define ARCH_ARM_MACH_OMAP2_SDRAM_QIMONDA_HYB18M512160AF6

#include "sdrc.h"

/* Qimonda HYB18M512160AF-6 */
static struct omap_sdrc_params hyb18m512160af6_sdrc_params[] = {
	[0] = {
		.rate	     = 166000000,
		.actim_ctrla = 0x629db4c6,
		.actim_ctrlb = 0x00012214,
		.rfr_ctrl    = 0x0004dc01,
		.mr	     = 0x00000032,
	},
	[1] = {
		.rate	     = 165941176,
		.actim_ctrla = 0x629db4c6,
		.actim_ctrlb = 0x00012214,
		.rfr_ctrl    = 0x0004dc01,
		.mr	     = 0x00000032,
	},
	[2] = {
		.rate	     = 83000000,
		.actim_ctrla = 0x31512283,
		.actim_ctrlb = 0x0001220a,
		.rfr_ctrl    = 0x00025501,
		.mr	     = 0x00000022,
	},
	[3] = {
		.rate	     = 82970588,
		.actim_ctrla = 0x31512283,
		.actim_ctrlb = 0x0001220a,
		.rfr_ctrl    = 0x00025501,
		.mr	     = 0x00000022,
	},
	[4] = {
		.rate	     = 0
	},
};

#endif
