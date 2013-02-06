/* linux/arch/arm/plat-s5pc11x/dev-dsim1.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd
 *
 * InKi Dae <inki.dae@samsung.com>
 *
 * device definitions for Samsung SoC MIPI-DSIM.
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
#include <asm/irq.h>

#include <plat/devs.h>
#include <plat/cpu.h>
#include <plat/fb.h>

#include <plat/mipi_dsim2.h>

static struct resource s5p_mipi_dsim_resource[] = {
	[0] = {
		.start = S5P_PA_MIPI_DSIM1,
		.end   = S5P_PA_MIPI_DSIM1 + SZ_64K - 1,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = IRQ_MIPIDSI,
		.end   = IRQ_MIPIDSI,
		.flags = IORESOURCE_IRQ,
	},
};

static struct s5p_platform_mipi_dsim dsim_platform_data = {
	.phy_enable		= s5p_dsim_phy_enable,
	.dsim_config		= &dsim_config,
};

struct platform_device s5p_device_mipi_dsim1 = {
	.name			= "s5p-mipi-dsim",
	.id			= 1,
	.num_resources		= ARRAY_SIZE(s5p_mipi_dsim_resource),
	.resource		= s5p_mipi_dsim_resource,
};
