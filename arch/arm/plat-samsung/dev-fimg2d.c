/*
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Base S5P FIMG2D resource and device definitions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/export.h>
#include <plat/devs.h>
#include <plat/cpu.h>
#include <plat/fimg2d.h>
#include <mach/map.h>

#define S5P_PA_FIMG2D_OFFSET	0x02000000
#define S5P_PA_FIMG2D_3X	(S5P_PA_FIMG2D+S5P_PA_FIMG2D_OFFSET)

#ifdef CONFIG_VIDEO_EXYNOS_FIMG2D
static struct resource s5p_fimg2d_resource[] = {
	[0] = DEFINE_RES_MEM(S5P_PA_FIMG2D, SZ_4K),
	[1] = DEFINE_RES_IRQ(IRQ_2D),
};

struct platform_device s5p_device_fimg2d = {
	.name		= "s5p-fimg2d",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(s5p_fimg2d_resource),
	.resource	= s5p_fimg2d_resource
};

static struct fimg2d_platdata default_fimg2d_data __initdata = {
	.parent_clkname	= "mout_g2d0",
	.clkname	= "sclk_fimg2d",
	.gate_clkname	= "fimg2d",
	.clkrate	= 200 * MHZ,
};

void __init s5p_fimg2d_set_platdata(struct fimg2d_platdata *pd)
{
	struct fimg2d_platdata *npd;

	if (soc_is_exynos4210()) {
		s5p_fimg2d_resource[0].start = S5P_PA_FIMG2D_3X;
		s5p_fimg2d_resource[0].end = S5P_PA_FIMG2D_3X + SZ_4K - 1;
	}

	if (!pd)
		pd = &default_fimg2d_data;

	npd = s3c_set_platdata(pd, sizeof(struct fimg2d_platdata),
			&s5p_device_fimg2d);
}
#endif
