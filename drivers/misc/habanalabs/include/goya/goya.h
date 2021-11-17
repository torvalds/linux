/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright 2016-2019 HabanaLabs, Ltd.
 * All Rights Reserved.
 *
 */

#ifndef GOYA_H
#define GOYA_H

#define SRAM_CFG_BAR_ID		0
#define MSIX_BAR_ID		2
#define DDR_BAR_ID		4

#define CFG_BAR_SIZE		0x10000000ull		/* 256MB */
#define MSIX_BAR_SIZE		0x1000ull		/* 4KB */

#define CFG_BASE		0x7FFC000000ull
#define CFG_SIZE		0x4000000		/* 32MB CFG + 32MB DBG*/

#define SRAM_BASE_ADDR		0x7FF0000000ull
#define SRAM_SIZE		0x32A0000		/* 50.625MB */

#define DRAM_PHYS_BASE		0x0ull

#define HOST_PHYS_BASE		0x8000000000ull		/* 0.5TB */
#define HOST_PHYS_SIZE		0x1000000000000ull	/* 0.25PB (48 bits) */

#define GOYA_MSIX_ENTRIES	8

#define QMAN_PQ_ENTRY_SIZE	16			/* Bytes */

#define MAX_ASID		2

#define PROT_BITS_OFFS		0xF80

#define DMA_MAX_NUM		5

#define TPC_MAX_NUM		8

#define MME_MAX_NUM		1

#endif /* GOYA_H */
