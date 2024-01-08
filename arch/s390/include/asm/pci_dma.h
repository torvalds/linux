/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_S390_PCI_DMA_H
#define _ASM_S390_PCI_DMA_H

/* I/O Translation Anchor (IOTA) */
enum zpci_ioat_dtype {
	ZPCI_IOTA_STO = 0,
	ZPCI_IOTA_RTTO = 1,
	ZPCI_IOTA_RSTO = 2,
	ZPCI_IOTA_RFTO = 3,
	ZPCI_IOTA_PFAA = 4,
	ZPCI_IOTA_IOPFAA = 5,
	ZPCI_IOTA_IOPTO = 7
};

#define ZPCI_IOTA_IOT_ENABLED		0x800UL
#define ZPCI_IOTA_DT_ST			(ZPCI_IOTA_STO	<< 2)
#define ZPCI_IOTA_DT_RT			(ZPCI_IOTA_RTTO << 2)
#define ZPCI_IOTA_DT_RS			(ZPCI_IOTA_RSTO << 2)
#define ZPCI_IOTA_DT_RF			(ZPCI_IOTA_RFTO << 2)
#define ZPCI_IOTA_DT_PF			(ZPCI_IOTA_PFAA << 2)
#define ZPCI_IOTA_FS_4K			0
#define ZPCI_IOTA_FS_1M			1
#define ZPCI_IOTA_FS_2G			2
#define ZPCI_KEY			(PAGE_DEFAULT_KEY << 5)

#define ZPCI_TABLE_SIZE_RT	(1UL << 42)

#define ZPCI_IOTA_STO_FLAG	(ZPCI_IOTA_IOT_ENABLED | ZPCI_KEY | ZPCI_IOTA_DT_ST)
#define ZPCI_IOTA_RTTO_FLAG	(ZPCI_IOTA_IOT_ENABLED | ZPCI_KEY | ZPCI_IOTA_DT_RT)
#define ZPCI_IOTA_RSTO_FLAG	(ZPCI_IOTA_IOT_ENABLED | ZPCI_KEY | ZPCI_IOTA_DT_RS)
#define ZPCI_IOTA_RFTO_FLAG	(ZPCI_IOTA_IOT_ENABLED | ZPCI_KEY | ZPCI_IOTA_DT_RF)
#define ZPCI_IOTA_RFAA_FLAG	(ZPCI_IOTA_IOT_ENABLED | ZPCI_KEY | ZPCI_IOTA_DT_PF | ZPCI_IOTA_FS_2G)

/* I/O Region and segment tables */
#define ZPCI_INDEX_MASK			0x7ffUL

#define ZPCI_TABLE_TYPE_MASK		0xc
#define ZPCI_TABLE_TYPE_RFX		0xc
#define ZPCI_TABLE_TYPE_RSX		0x8
#define ZPCI_TABLE_TYPE_RTX		0x4
#define ZPCI_TABLE_TYPE_SX		0x0

#define ZPCI_TABLE_LEN_RFX		0x3
#define ZPCI_TABLE_LEN_RSX		0x3
#define ZPCI_TABLE_LEN_RTX		0x3

#define ZPCI_TABLE_OFFSET_MASK		0xc0
#define ZPCI_TABLE_SIZE			0x4000
#define ZPCI_TABLE_ALIGN		ZPCI_TABLE_SIZE
#define ZPCI_TABLE_ENTRY_SIZE		(sizeof(unsigned long))
#define ZPCI_TABLE_ENTRIES		(ZPCI_TABLE_SIZE / ZPCI_TABLE_ENTRY_SIZE)

#define ZPCI_TABLE_BITS			11
#define ZPCI_PT_BITS			8
#define ZPCI_ST_SHIFT			(ZPCI_PT_BITS + PAGE_SHIFT)
#define ZPCI_RT_SHIFT			(ZPCI_ST_SHIFT + ZPCI_TABLE_BITS)

#define ZPCI_RTE_FLAG_MASK		0x3fffUL
#define ZPCI_RTE_ADDR_MASK		(~ZPCI_RTE_FLAG_MASK)
#define ZPCI_STE_FLAG_MASK		0x7ffUL
#define ZPCI_STE_ADDR_MASK		(~ZPCI_STE_FLAG_MASK)

/* I/O Page tables */
#define ZPCI_PTE_VALID_MASK		0x400
#define ZPCI_PTE_INVALID		0x400
#define ZPCI_PTE_VALID			0x000
#define ZPCI_PT_SIZE			0x800
#define ZPCI_PT_ALIGN			ZPCI_PT_SIZE
#define ZPCI_PT_ENTRIES			(ZPCI_PT_SIZE / ZPCI_TABLE_ENTRY_SIZE)
#define ZPCI_PT_MASK			(ZPCI_PT_ENTRIES - 1)

#define ZPCI_PTE_FLAG_MASK		0xfffUL
#define ZPCI_PTE_ADDR_MASK		(~ZPCI_PTE_FLAG_MASK)

/* Shared bits */
#define ZPCI_TABLE_VALID		0x00
#define ZPCI_TABLE_INVALID		0x20
#define ZPCI_TABLE_PROTECTED		0x200
#define ZPCI_TABLE_UNPROTECTED		0x000

#define ZPCI_TABLE_VALID_MASK		0x20
#define ZPCI_TABLE_PROT_MASK		0x200

struct zpci_iommu_ctrs {
	atomic64_t		mapped_pages;
	atomic64_t		unmapped_pages;
	atomic64_t		global_rpcits;
	atomic64_t		sync_map_rpcits;
	atomic64_t		sync_rpcits;
};

struct zpci_dev;

struct zpci_iommu_ctrs *zpci_get_iommu_ctrs(struct zpci_dev *zdev);

#endif
