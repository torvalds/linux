/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  Copyright 2014-2015 Cisco Systems, Inc. and/or its affiliates.
 *  All rights reserved.
 */

#ifndef M00473_FREEWHEEL_MEMMAP_PACKAGE_H
#define M00473_FREEWHEEL_MEMMAP_PACKAGE_H

/*******************************************************************
 * Register Block
 * M00473_FREEWHEEL_MEMMAP_PACKAGE_VHD_REGMAP
 *******************************************************************/
struct m00473_freewheel_regmap {
	uint32_t ctrl;          /* Reg 0x0000, Default=0x0 */
	uint32_t status;        /* Reg 0x0004 */
	uint32_t active_length; /* Reg 0x0008, Default=0x1fa400 */
	uint32_t total_length;  /* Reg 0x000c, Default=0x31151b */
	uint32_t data_width;    /* Reg 0x0010 */
	uint32_t output_color;  /* Reg 0x0014, Default=0xffff */
	uint32_t clk_freq;      /* Reg 0x0018 */
};

#define M00473_FREEWHEEL_REG_CTRL_OFST 0
#define M00473_FREEWHEEL_REG_STATUS_OFST 4
#define M00473_FREEWHEEL_REG_ACTIVE_LENGTH_OFST 8
#define M00473_FREEWHEEL_REG_TOTAL_LENGTH_OFST 12
#define M00473_FREEWHEEL_REG_DATA_WIDTH_OFST 16
#define M00473_FREEWHEEL_REG_OUTPUT_COLOR_OFST 20
#define M00473_FREEWHEEL_REG_CLK_FREQ_OFST 24

/*******************************************************************
 * Bit Mask for register
 * M00473_FREEWHEEL_MEMMAP_PACKAGE_VHD_BITMAP
 *******************************************************************/
/* ctrl [1:0] */
#define M00473_CTRL_BITMAP_ENABLE_OFST               (0)
#define M00473_CTRL_BITMAP_ENABLE_MSK                (0x1 << M00473_CTRL_BITMAP_ENABLE_OFST)
#define M00473_CTRL_BITMAP_FORCE_FREEWHEEL_MODE_OFST (1)
#define M00473_CTRL_BITMAP_FORCE_FREEWHEEL_MODE_MSK  (0x1 << M00473_CTRL_BITMAP_FORCE_FREEWHEEL_MODE_OFST)
/* status [0:0] */
#define M00473_STATUS_BITMAP_FREEWHEEL_MODE_OFST     (0)
#define M00473_STATUS_BITMAP_FREEWHEEL_MODE_MSK      (0x1 << M00473_STATUS_BITMAP_FREEWHEEL_MODE_OFST)

#endif /*M00473_FREEWHEEL_MEMMAP_PACKAGE_H*/
