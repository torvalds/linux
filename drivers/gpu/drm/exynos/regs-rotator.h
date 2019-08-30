/* SPDX-License-Identifier: GPL-2.0-only */
/* drivers/gpu/drm/exynos/regs-rotator.h
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * Register definition file for Samsung Rotator Interface (Rotator) driver
*/

#ifndef EXYNOS_REGS_ROTATOR_H
#define EXYNOS_REGS_ROTATOR_H

/* Configuration */
#define ROT_CONFIG			0x00
#define ROT_CONFIG_IRQ			(3 << 8)

/* Image Control */
#define ROT_CONTROL			0x10
#define ROT_CONTROL_PATTERN_WRITE	(1 << 16)
#define ROT_CONTROL_FMT_YCBCR420_2P	(1 << 8)
#define ROT_CONTROL_FMT_RGB888		(6 << 8)
#define ROT_CONTROL_FMT_MASK		(7 << 8)
#define ROT_CONTROL_FLIP_VERTICAL	(2 << 6)
#define ROT_CONTROL_FLIP_HORIZONTAL	(3 << 6)
#define ROT_CONTROL_FLIP_MASK		(3 << 6)
#define ROT_CONTROL_ROT_90		(1 << 4)
#define ROT_CONTROL_ROT_180		(2 << 4)
#define ROT_CONTROL_ROT_270		(3 << 4)
#define ROT_CONTROL_ROT_MASK		(3 << 4)
#define ROT_CONTROL_START		(1 << 0)

/* Status */
#define ROT_STATUS			0x20
#define ROT_STATUS_IRQ_PENDING(x)	(1 << (x))
#define ROT_STATUS_IRQ(x)		(((x) >> 8) & 0x3)
#define ROT_STATUS_IRQ_VAL_COMPLETE	1
#define ROT_STATUS_IRQ_VAL_ILLEGAL	2

/* Buffer Address */
#define ROT_SRC_BUF_ADDR(n)		(0x30 + ((n) << 2))
#define ROT_DST_BUF_ADDR(n)		(0x50 + ((n) << 2))

/* Buffer Size */
#define ROT_SRC_BUF_SIZE		0x3c
#define ROT_DST_BUF_SIZE		0x5c
#define ROT_SET_BUF_SIZE_H(x)		((x) << 16)
#define ROT_SET_BUF_SIZE_W(x)		((x) << 0)
#define ROT_GET_BUF_SIZE_H(x)		((x) >> 16)
#define ROT_GET_BUF_SIZE_W(x)		((x) & 0xffff)

/* Crop Position */
#define ROT_SRC_CROP_POS		0x40
#define ROT_DST_CROP_POS		0x60
#define ROT_CROP_POS_Y(x)		((x) << 16)
#define ROT_CROP_POS_X(x)		((x) << 0)

/* Source Crop Size */
#define ROT_SRC_CROP_SIZE		0x44
#define ROT_SRC_CROP_SIZE_H(x)		((x) << 16)
#define ROT_SRC_CROP_SIZE_W(x)		((x) << 0)

/* Round to nearest aligned value */
#define ROT_ALIGN(x, align, mask)	(((x) + (1 << ((align) - 1))) & (mask))
/* Minimum limit value */
#define ROT_MIN(min, mask)		(((min) + ~(mask)) & (mask))
/* Maximum limit value */
#define ROT_MAX(max, mask)		((max) & (mask))

#endif /* EXYNOS_REGS_ROTATOR_H */

