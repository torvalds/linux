/*
 * omap_hwmod_2xxx_3xxx_interconnect_data.c - common interconnect data, OMAP2/3
 *
 * Copyright (C) 2009-2011 Nokia Corporation
 * Paul Walmsley
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * XXX handle crossbar/shared link difference for L3?
 * XXX these should be marked initdata for multi-OMAP kernels
 */
#include <asm/sizes.h>

#include "omap_hwmod.h"

#include "omap_hwmod_common_data.h"

struct omap_hwmod_addr_space omap2_dma_system_addrs[] = {
	{
		.pa_start	= 0x48056000,
		.pa_end		= 0x48056000 + SZ_4K - 1,
		.flags		= ADDR_TYPE_RT,
	},
	{ },
};
