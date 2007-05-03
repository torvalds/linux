/*
 * Copyright (C) 2005  Stephen Rothwell  IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#include <asm/mmu.h>
#include <asm/pgtable.h>
#include <asm/iseries/lpar_map.h>

/* The # is to stop gcc trying to make .text nonexecutable */
const struct LparMap __attribute__((__section__(".text #"))) xLparMap = {
	.xNumberEsids = HvEsidsToMap,
	.xNumberRanges = HvRangesToMap,
	.xSegmentTableOffs = STAB0_PAGE,

	.xEsids = {
		{ .xKernelEsid = GET_ESID(PAGE_OFFSET),
		  .xKernelVsid = KERNEL_VSID(PAGE_OFFSET), },
		{ .xKernelEsid = GET_ESID(VMALLOC_START),
		  .xKernelVsid = KERNEL_VSID(VMALLOC_START), },
	},

	.xRanges = {
		{ .xPages = HvPagesToMap,
		  .xOffset = 0,
		  .xVPN = KERNEL_VSID(PAGE_OFFSET) << (SID_SHIFT - HW_PAGE_SHIFT),
		},
	},
};
