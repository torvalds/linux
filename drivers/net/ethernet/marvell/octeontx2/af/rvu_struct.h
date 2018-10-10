/* SPDX-License-Identifier: GPL-2.0
 * Marvell OcteonTx2 RVU Admin Function driver
 *
 * Copyright (C) 2018 Marvell International Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef RVU_STRUCT_H
#define RVU_STRUCT_H

/* RVU Block Address Enumeration */
enum rvu_block_addr_e {
	BLKADDR_RVUM    = 0x0ULL,
	BLKADDR_LMT     = 0x1ULL,
	BLKADDR_MSIX    = 0x2ULL,
	BLKADDR_NPA     = 0x3ULL,
	BLKADDR_NIX0    = 0x4ULL,
	BLKADDR_NIX1    = 0x5ULL,
	BLKADDR_NPC     = 0x6ULL,
	BLKADDR_SSO     = 0x7ULL,
	BLKADDR_SSOW    = 0x8ULL,
	BLKADDR_TIM     = 0x9ULL,
	BLKADDR_CPT0    = 0xaULL,
	BLKADDR_CPT1    = 0xbULL,
	BLKADDR_NDC0    = 0xcULL,
	BLKADDR_NDC1    = 0xdULL,
	BLKADDR_NDC2    = 0xeULL,
	BLK_COUNT	= 0xfULL,
};

#endif /* RVU_STRUCT_H */
