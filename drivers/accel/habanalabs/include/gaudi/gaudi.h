/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright 2018-2020 HabanaLabs, Ltd.
 * All Rights Reserved.
 *
 */

#ifndef GAUDI_H
#define GAUDI_H

#define SRAM_BAR_ID		0
#define CFG_BAR_ID		2
#define HBM_BAR_ID		4

#define SRAM_BAR_SIZE		0x4000000ull		/* 64MB */
#define CFG_BAR_SIZE		0x8000000ull		/* 128MB */

#define CFG_BASE		0x7FFC000000ull
#define CFG_SIZE		0x4000000		/* 32MB CFG + 32MB DBG*/

#define SRAM_BASE_ADDR		0x7FF0000000ull
#define SRAM_SIZE		0x1400000		/* 20MB */

#define SPI_FLASH_BASE_ADDR	0x7FF8000000ull

#define PSOC_SCRATCHPAD_ADDR	0x7FFBFE0000ull
#define PSOC_SCRATCHPAD_SIZE	0x10000			/* 64KB */

#define PCIE_FW_SRAM_ADDR	0x7FFBFF0000ull
#define PCIE_FW_SRAM_SIZE	0x8000			/* 32KB */

#define DRAM_PHYS_BASE		0x0ull

#define HOST_PHYS_BASE		0x8000000000ull		/* 0.5TB */
#define HOST_PHYS_SIZE		0x1000000000000ull	/* 0.25PB (48 bits) */

#define GAUDI_MSI_ENTRIES	32

#define QMAN_PQ_ENTRY_SIZE	16			/* Bytes */

#define MAX_ASID		2

#define PROT_BITS_OFFS		0xF80

#define MME_NUMBER_OF_MASTER_ENGINES	2

#define MME_NUMBER_OF_SLAVE_ENGINES	2

#define TPC_NUMBER_OF_ENGINES	8

#define DMA_NUMBER_OF_CHANNELS	8

#define NIC_NUMBER_OF_MACROS	5

#define NIC_NUMBER_OF_ENGINES	(NIC_NUMBER_OF_MACROS * 2)

#define NUMBER_OF_IF		8

#define DEVICE_CACHE_LINE_SIZE	128

#endif /* GAUDI_H */
