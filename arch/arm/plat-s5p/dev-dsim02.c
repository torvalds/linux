/* linux/arch/arm/plat-s5pc11x/dev-dsim0.c
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
		.start = EXYNOS4_PA_DSIM0,
		.end   = EXYNOS4_PA_DSIM0 + SZ_64K - 1,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = IRQ_MIPIDSI0,
		.end   = IRQ_MIPIDSI0,
		.flags = IORESOURCE_IRQ,
	},
};

struct platform_device s5p_device_mipi_dsim0 = {
	.name			= "s5p-mipi-dsim",
	.id			= 0,
	.num_resources		= ARRAY_SIZE(s5p_mipi_dsim_resource),
	.resource		= s5p_mipi_dsim_resource,
};
