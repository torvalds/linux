/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  Copyright 2014-2015 Cisco Systems, Inc. and/or its affiliates.
 *  All rights reserved.
 */

#ifndef M00235_FDMA_PACKER_MEMMAP_PACKAGE_H
#define M00235_FDMA_PACKER_MEMMAP_PACKAGE_H

/*******************************************************************
 * Register Block
 * M00235_FDMA_PACKER_MEMMAP_PACKAGE_VHD_REGMAP
 *******************************************************************/
struct m00235_fdma_packer_regmap {
	uint32_t control; /* Reg 0x0000, Default=0x0 */
};

#define M00235_FDMA_PACKER_REG_CONTROL_OFST 0

/*******************************************************************
 * Bit Mask for register
 * M00235_FDMA_PACKER_MEMMAP_PACKAGE_VHD_BITMAP
 *******************************************************************/
/* control [3:0] */
#define M00235_CONTROL_BITMAP_ENABLE_OFST        (0)
#define M00235_CONTROL_BITMAP_ENABLE_MSK         (0x1 << M00235_CONTROL_BITMAP_ENABLE_OFST)
#define M00235_CONTROL_BITMAP_PACK_FORMAT_OFST   (1)
#define M00235_CONTROL_BITMAP_PACK_FORMAT_MSK    (0x3 << M00235_CONTROL_BITMAP_PACK_FORMAT_OFST)
#define M00235_CONTROL_BITMAP_ENDIAN_FORMAT_OFST (3)
#define M00235_CONTROL_BITMAP_ENDIAN_FORMAT_MSK  (0x1 << M00235_CONTROL_BITMAP_ENDIAN_FORMAT_OFST)

#endif /*M00235_FDMA_PACKER_MEMMAP_PACKAGE_H*/
