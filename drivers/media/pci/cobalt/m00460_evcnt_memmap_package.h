/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  Copyright 2014-2015 Cisco Systems, Inc. and/or its affiliates.
 *  All rights reserved.
 */

#ifndef M00460_EVCNT_MEMMAP_PACKAGE_H
#define M00460_EVCNT_MEMMAP_PACKAGE_H

/*******************************************************************
 * Register Block
 * M00460_EVCNT_MEMMAP_PACKAGE_VHD_REGMAP
 *******************************************************************/
struct m00460_evcnt_regmap {
	uint32_t control; /* Reg 0x0000, Default=0x0 */
	uint32_t count;   /* Reg 0x0004 */
};

#define M00460_EVCNT_REG_CONTROL_OFST 0
#define M00460_EVCNT_REG_COUNT_OFST 4

/*******************************************************************
 * Bit Mask for register
 * M00460_EVCNT_MEMMAP_PACKAGE_VHD_BITMAP
 *******************************************************************/
/* control [1:0] */
#define M00460_CONTROL_BITMAP_ENABLE_OFST (0)
#define M00460_CONTROL_BITMAP_ENABLE_MSK  (0x1 << M00460_CONTROL_BITMAP_ENABLE_OFST)
#define M00460_CONTROL_BITMAP_CLEAR_OFST  (1)
#define M00460_CONTROL_BITMAP_CLEAR_MSK   (0x1 << M00460_CONTROL_BITMAP_CLEAR_OFST)

#endif /*M00460_EVCNT_MEMMAP_PACKAGE_H*/
