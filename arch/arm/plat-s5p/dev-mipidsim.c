/* linux/arch/arm/plat-s5p/dev-mipidsim.c
 *
 * Copyright (c) 2011 Samsung Electronics
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/fb.h>

#include <mach/map.h>
#include <mach/irqs.h>
#include <mach/regs-clock.h>

#include <plat/devs.h>
#include <plat/cpu.h>
#include <plat/fb.h>

#include <plat/dsim.h>
#include <plat/mipi_dsi.h>

static struct resource s5p_dsim_resource[] = {
	[0] = {
		.start = S5P_PA_DSIM0,
		.end   = S5P_PA_DSIM0 + SZ_64K - 1,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = IRQ_MIPIDSI0,
		.end   = IRQ_MIPIDSI0,
		.flags = IORESOURCE_IRQ,
	},
};

struct platform_device s5p_device_mipi_dsim = {
	.name			= "s5p-mipi-dsim",
	.id			= -1,
	.num_resources		= ARRAY_SIZE(s5p_dsim_resource),
	.resource		= s5p_dsim_resource,
	.dev			= {
		.platform_data	= NULL,
	},
};

void __init s5p_dsim_set_platdata(struct s5p_platform_mipi_dsim *pd) {
        s3c_set_platdata(pd, sizeof(struct s5p_platform_mipi_dsim), &s5p_device_mipi_dsim);
}
