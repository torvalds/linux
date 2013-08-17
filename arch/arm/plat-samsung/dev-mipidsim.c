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

#ifdef CONFIG_S5P_DEV_MIPI_DSIM0
static struct resource s5p_dsim0_resource[] = {
	[0] = DEFINE_RES_MEM(S5P_PA_DSIM0, SZ_32K),
	[1] = DEFINE_RES_IRQ(IRQ_MIPIDSI0),
};

struct platform_device s5p_device_mipi_dsim0 = {
	.name			= "s5p-mipi-dsim",
	.id			= 0,
	.num_resources		= ARRAY_SIZE(s5p_dsim0_resource),
	.resource		= s5p_dsim0_resource,
	.dev			= {
		.platform_data	= NULL,
	},
};

void __init s5p_dsim0_set_platdata(struct s5p_platform_mipi_dsim *pd)
{
	s3c_set_platdata(pd, sizeof(struct s5p_platform_mipi_dsim),
			&s5p_device_mipi_dsim0);
}
#else
static struct resource s5p_dsim1_resource[] = {
	[0] = DEFINE_RES_MEM(S5P_PA_DSIM1, SZ_32K),
	[1] = DEFINE_RES_IRQ(IRQ_MIPIDSI1),
};

struct platform_device s5p_device_mipi_dsim1 = {
	.name			= "s5p-mipi-dsim",
	.id			= 1,
	.num_resources		= ARRAY_SIZE(s5p_dsim1_resource),
	.resource		= s5p_dsim1_resource,
	.dev			= {
		.platform_data	= NULL,
	},
};

void __init s5p_dsim1_set_platdata(struct s5p_platform_mipi_dsim *pd)
{
	s3c_set_platdata(pd, sizeof(struct s5p_platform_mipi_dsim),
			&s5p_device_mipi_dsim1);
}
#endif
