/*
 * Copyright (C) 2005  Stephen Rothwell  IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#include <asm/mmu.h>
#include <asm/page.h>
#include <asm/iseries/lpar_map.h>

const struct LparMap __attribute__((__section__(".text"))) xLparMap = {
	.xNumberEsids = HvEsidsToMap,
	.xNumberRanges = HvRangesToMap,
	.xSegmentTableOffs = STAB0_PAGE,

	.xEsids = {
		{ .xKernelEsid = GET_ESID(KERNELBASE),
		  .xKernelVsid = KERNEL_VSID(KERNELBASE), },
		{ .xKernelEsid = GET_ESID(VMALLOCBASE),
		  .xKernelVsid = KERNEL_VSID(VMALLOCBASE), },
	},

	.xRanges = {
		{ .xPages = HvPagesToMap,
		  .xOffset = 0,
		  .xVPN = KERNEL_VSID(KERNELBASE) << (SID_SHIFT - HW_PAGE_SHIFT),
		},
	},
};
