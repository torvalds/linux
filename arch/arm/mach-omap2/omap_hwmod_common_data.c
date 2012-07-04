/*
 * omap_hwmod common data structures
 *
 * Copyright (C) 2010 Texas Instruments, Inc.
 * Thara Gopinath <thara@ti.com>
 * Benoît Cousson
 *
 * Copyright (C) 2010 Nokia Corporation
 * Paul Walmsley
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This data/structures are to be used while defining OMAP on-chip module
 * data and their integration with other OMAP modules and Linux.
 */

#include <plat/omap_hwmod.h>

#include "omap_hwmod_common_data.h"

/**
 * struct omap_hwmod_sysc_type1 - TYPE1 sysconfig scheme.
 *
 * To be used by hwmod structure to specify the sysconfig offsets
 * if the device ip is compliant with the original PRCM protocol
 * defined for OMAP2420.
 */
struct omap_hwmod_sysc_fields omap_hwmod_sysc_type1 = {
	.midle_shift	= SYSC_TYPE1_MIDLEMODE_SHIFT,
	.clkact_shift	= SYSC_TYPE1_CLOCKACTIVITY_SHIFT,
	.sidle_shift	= SYSC_TYPE1_SIDLEMODE_SHIFT,
	.enwkup_shift	= SYSC_TYPE1_ENAWAKEUP_SHIFT,
	.srst_shift	= SYSC_TYPE1_SOFTRESET_SHIFT,
	.autoidle_shift	= SYSC_TYPE1_AUTOIDLE_SHIFT,
};

/**
 * struct omap_hwmod_sysc_type2 - TYPE2 sysconfig scheme.
 *
 * To be used by hwmod structure to specify the sysconfig offsets if the
 * device ip is compliant with the new PRCM protocol defined for new
 * OMAP4 IPs.
 */
struct omap_hwmod_sysc_fields omap_hwmod_sysc_type2 = {
	.midle_shift	= SYSC_TYPE2_MIDLEMODE_SHIFT,
	.sidle_shift	= SYSC_TYPE2_SIDLEMODE_SHIFT,
	.srst_shift	= SYSC_TYPE2_SOFTRESET_SHIFT,
	.dmadisable_shift = SYSC_TYPE2_DMADISABLE_SHIFT,
};

struct omap_dss_dispc_dev_attr omap2_3_dss_dispc_dev_attr = {
	.manager_count		= 2,
	.has_framedonetv_irq	= 0
};
