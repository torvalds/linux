/*
 * mx3_camera.h - i.MX3x camera driver header file
 *
 * Copyright (C) 2008, Guennadi Liakhovetski, DENX Software Engineering, <lg@denx.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef _MX3_CAMERA_H_
#define _MX3_CAMERA_H_

#include <linux/device.h>

#define MX3_CAMERA_CLK_SRC	1
#define MX3_CAMERA_EXT_VSYNC	2
#define MX3_CAMERA_DP		4
#define MX3_CAMERA_PCP		8
#define MX3_CAMERA_HSP		0x10
#define MX3_CAMERA_VSP		0x20
#define MX3_CAMERA_DATAWIDTH_4	0x40
#define MX3_CAMERA_DATAWIDTH_8	0x80
#define MX3_CAMERA_DATAWIDTH_10	0x100
#define MX3_CAMERA_DATAWIDTH_15	0x200

#define MX3_CAMERA_DATAWIDTH_MASK (MX3_CAMERA_DATAWIDTH_4 | MX3_CAMERA_DATAWIDTH_8 | \
				   MX3_CAMERA_DATAWIDTH_10 | MX3_CAMERA_DATAWIDTH_15)

/**
 * struct mx3_camera_pdata - i.MX3x camera platform data
 * @flags:	MX3_CAMERA_* flags
 * @mclk_10khz:	master clock frequency in 10kHz units
 * @dma_dev:	IPU DMA device to match against in channel allocation
 */
struct mx3_camera_pdata {
	unsigned long flags;
	unsigned long mclk_10khz;
	struct device *dma_dev;
};

#endif
