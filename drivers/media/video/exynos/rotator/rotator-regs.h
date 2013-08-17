/*
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Register header file for Exynos Rotator driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

/* Configuration */
#define ROTATOR_CONFIG				0x00
#define ROTATOR_CONFIG_IRQ_ILLEGAL		(1 << 9)
#define ROTATOR_CONFIG_IRQ_DONE			(1 << 8)

/* Image0 Control */
#define ROTATOR_CONTROL				0x10
#define ROTATOR_CONTROL_PATTERN_WRITE		(1 << 16)
#define ROTATOR_CONTROL_FMT_YCBCR420_3P		(0 << 8)
#define ROTATOR_CONTROL_FMT_YCBCR420_2P		(1 << 8)
#define ROTATOR_CONTROL_FMT_YCBCR422		(3 << 8)
#define ROTATOR_CONTROL_FMT_RGB565		(4 << 8)
#define ROTATOR_CONTROL_FMT_RGB888		(6 << 8)
#define ROTATOR_CONTROL_FMT_MASK		(7 << 8)
#define ROTATOR_CONTROL_FLIP_V			(2 << 6)
#define ROTATOR_CONTROL_FLIP_H			(3 << 6)
#define ROTATOR_CONTROL_FLIP_MASK		(3 << 6)
#define ROTATOR_CONTROL_ROT_90			(1 << 4)
#define ROTATOR_CONTROL_ROT_180			(2 << 4)
#define ROTATOR_CONTROL_ROT_270			(3 << 4)
#define ROTATOR_CONTROL_ROT_MASK		(3 << 4)
#define ROTATOR_CONTROL_START			(1 << 0)

/* Status */
#define ROTATOR_STATUS				0x20
#define ROTATOR_STATUS_IRQ_PENDING(x)		(1 << (x))
#define ROTATOR_STATUS_IRQ(x)			(((x) >> 8) & 0x3)
#define ROTATOR_STATUS_MASK			(3 << 0)

/* Sourc Image Base Address */
#define ROTATOR_SRC_IMG_ADDR(n)			(0x30 + ((n) << 2))

/* Source Image X,Y Size */
#define ROTATOR_SRCIMG				0x3c
#define ROTATOR_SRCIMG_YSIZE(x)			((x) << 16)
#define ROTATOR_SRCIMG_XSIZE(x)			((x) << 0)

/* Source Image X,Y Coordinates */
#define ROTATOR_SRC				0x40
#define ROTATOR_SRC_Y(x)			((x) << 16)
#define ROTATOR_SRC_X(x)			((x) << 0)

/* Source Image Rotation Size */
#define ROTATOR_SRCROT				0x44
#define ROTATOR_SRCROT_YSIZE(x)			((x) << 16)
#define ROTATOR_SRCROT_XSIZE(x)			((x) << 0)

/* Destination Image Base Address */
#define ROTATOR_DST_IMG_ADDR(n)			(0x50 + ((n) << 2))

/* Destination Image X,Y Size */
#define ROTATOR_DSTIMG				0x5c
#define ROTATOR_DSTIMG_YSIZE(x)			((x) << 16)
#define ROTATOR_DSTIMG_XSIZE(x)			((x) << 0)

/* Destination Image X,Y Coordinates */
#define ROTATOR_DST				0x60
#define ROTATOR_DST_Y(x)			((x) << 16)
#define ROTATOR_DST_X(x)			((x) << 0)
