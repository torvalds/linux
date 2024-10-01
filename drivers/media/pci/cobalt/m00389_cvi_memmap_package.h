/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  Copyright 2014-2015 Cisco Systems, Inc. and/or its affiliates.
 *  All rights reserved.
 */

#ifndef M00389_CVI_MEMMAP_PACKAGE_H
#define M00389_CVI_MEMMAP_PACKAGE_H

/*******************************************************************
 * Register Block
 * M00389_CVI_MEMMAP_PACKAGE_VHD_REGMAP
 *******************************************************************/
struct m00389_cvi_regmap {
	uint32_t control;          /* Reg 0x0000, Default=0x0 */
	uint32_t frame_width;      /* Reg 0x0004, Default=0x10 */
	uint32_t frame_height;     /* Reg 0x0008, Default=0xc */
	uint32_t freewheel_period; /* Reg 0x000c, Default=0x0 */
	uint32_t error_color;      /* Reg 0x0010, Default=0x0 */
	uint32_t status;           /* Reg 0x0014 */
};

#define M00389_CVI_REG_CONTROL_OFST 0
#define M00389_CVI_REG_FRAME_WIDTH_OFST 4
#define M00389_CVI_REG_FRAME_HEIGHT_OFST 8
#define M00389_CVI_REG_FREEWHEEL_PERIOD_OFST 12
#define M00389_CVI_REG_ERROR_COLOR_OFST 16
#define M00389_CVI_REG_STATUS_OFST 20

/*******************************************************************
 * Bit Mask for register
 * M00389_CVI_MEMMAP_PACKAGE_VHD_BITMAP
 *******************************************************************/
/* control [2:0] */
#define M00389_CONTROL_BITMAP_ENABLE_OFST             (0)
#define M00389_CONTROL_BITMAP_ENABLE_MSK              (0x1 << M00389_CONTROL_BITMAP_ENABLE_OFST)
#define M00389_CONTROL_BITMAP_HSYNC_POLARITY_LOW_OFST (1)
#define M00389_CONTROL_BITMAP_HSYNC_POLARITY_LOW_MSK  (0x1 << M00389_CONTROL_BITMAP_HSYNC_POLARITY_LOW_OFST)
#define M00389_CONTROL_BITMAP_VSYNC_POLARITY_LOW_OFST (2)
#define M00389_CONTROL_BITMAP_VSYNC_POLARITY_LOW_MSK  (0x1 << M00389_CONTROL_BITMAP_VSYNC_POLARITY_LOW_OFST)
/* status [1:0] */
#define M00389_STATUS_BITMAP_LOCK_OFST                (0)
#define M00389_STATUS_BITMAP_LOCK_MSK                 (0x1 << M00389_STATUS_BITMAP_LOCK_OFST)
#define M00389_STATUS_BITMAP_ERROR_OFST               (1)
#define M00389_STATUS_BITMAP_ERROR_MSK                (0x1 << M00389_STATUS_BITMAP_ERROR_OFST)

#endif /*M00389_CVI_MEMMAP_PACKAGE_H*/
