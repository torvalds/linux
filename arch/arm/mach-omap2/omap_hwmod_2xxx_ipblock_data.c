/*
 * omap_hwmod_2xxx_ipblock_data.c - common IP block data for OMAP2xxx
 *
 * Copyright (C) 2011 Nokia Corporation
 * Paul Walmsley
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <plat/omap_hwmod.h>
#include <plat/serial.h>

#include <mach/irqs.h>

#include "omap_hwmod_common_data.h"

struct omap_hwmod_irq_info omap2xxx_timer12_mpu_irqs[] = {
	{ .irq = 48, },
	{ .irq = -1 }
};
