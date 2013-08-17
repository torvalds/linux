/*
 * Copyright (C) 2010-2011 Samsung Electronics Co., Ltd.
 *
 * S5P series device definition for JPEG
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <mach/map.h>

static struct resource s5p_jpeg_resource[] = {
	[0] = DEFINE_RES_MEM(EXYNOS5_PA_JPEG, SZ_64K),
	[1] = DEFINE_RES_IRQ(IRQ_JPEG),
};

struct platform_device s5p_device_jpeg = {
	.name             = "s5p-jpeg",
	.id               = -1,
	.num_resources    = ARRAY_SIZE(s5p_jpeg_resource),
	.resource         = s5p_jpeg_resource,
};
