/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2013--2026 Intel Corporation */

#ifndef IPU6_MMU_H
#define IPU6_MMU_H

#include <linux/list.h>
#include <linux/spinlock_types.h>
#include <linux/types.h>

#include "ipu7-mmu-hw.h"

struct device;
struct ipu6_mmu;
struct page;

struct ipu6_mmu_info {
	struct device *dev;

	u32 *l1_pt;
	u32 l1_pt_dma;
	u32 **l2_pts;

	u32 *dummy_l2_pt;
	u32 dummy_l2_pteval;
	void *dummy_page;
	u32 dummy_page_pteval;

	dma_addr_t aperture_start;
	dma_addr_t aperture_end;
	unsigned long pgsize_bitmap;

	spinlock_t lock;	/* Serialize access to users */
	struct ipu6_dma_mapping *dmap;
};

/*
 * MMU Invalidation HW bug workaround by ZLW mechanism
 *
 * Old IPU6 MMUV2 has a bug in the invalidation mechanism which might result in
 * wrong translation or replication of the translation. This will cause data
 * corruption. So we cannot directly use the MMU V2 invalidation registers
 * to invalidate the MMU. Instead, whenever an invalidate is called, we need to
 * clear the TLB by evicting all the valid translations by filling it with trash
 * buffer (which is guaranteed not to be used by any other processes). ZLW is
 * used to fill the L1 and L2 caches with the trash buffer translations. ZLW
 * or Zero length write, is pre-fetch mechanism to pre-fetch the pages in
 * advance to the L1 and L2 caches without triggering any memory operations.
 *
 * In MMU V2, L1 -> 16 streams and 64 blocks, maximum 16 blocks per stream
 * One L1 block has 16 entries, hence points to 16 * 4K pages
 * L2 -> 16 streams and 32 blocks. 2 blocks per streams
 * One L2 block maps to 1024 L1 entries, hence points to 4MB address range
 * 2 blocks per L2 stream means, 1 stream points to 8MB range
 *
 * As we need to clear the caches and 8MB being the biggest cache size, we need
 * to have trash buffer which points to 8MB address range. As these trash
 * buffers are not used for any memory transactions, we need only the least
 * amount of physical memory. So we reserve 8MB IOVA address range but only
 * one page is reserved from physical memory. Each of this 8MB IOVA address
 * range is then mapped to the same physical memory page.
 */
/* One L2 entry maps 1024 L1 entries and one L1 entry per page */
#define IPU6_MMUV2_L2_RANGE		(1024 * PAGE_SIZE)
/* Max L2 blocks per stream */
#define IPU6_MMUV2_MAX_L2_BLOCKS	2
/* Max L1 blocks per stream */
#define IPU6_MMUV2_MAX_L1_BLOCKS	16
#define IPU6_MMUV2_TRASH_RANGE	(IPU6_MMUV2_L2_RANGE * IPU6_MMUV2_MAX_L2_BLOCKS)
/* Entries per L1 block */
#define MMUV2_ENTRIES_PER_L1_BLOCK	16
#define MMUV2_TRASH_L1_BLOCK_OFFSET	(MMUV2_ENTRIES_PER_L1_BLOCK * PAGE_SIZE)
#define MMUV2_TRASH_L2_BLOCK_OFFSET	IPU6_MMUV2_L2_RANGE

/*
 * In some of the IPU6 MMUs, there is provision to configure L1 and L2 page
 * table caches. Both these L1 and L2 caches are divided into multiple sections
 * called streams. There is maximum 16 streams for both caches. Each of these
 * sections are subdivided into multiple blocks. When nr_l1streams = 0 and
 * nr_l2streams = 0, means the MMU is of type MMU_V1 and do not support
 * L1/L2 page table caches.
 *
 * L1 stream per block sizes are configurable and varies per usecase.
 * L2 has constant block sizes - 2 blocks per stream.
 *
 * MMU1 support pre-fetching of the pages to have less cache lookup misses. To
 * enable the pre-fetching, MMU1 AT (Address Translator) device registers
 * need to be configured.
 *
 * There are four types of memory accesses which requires ZLW configuration.
 * ZLW(Zero Length Write) is a mechanism to enable VT-d pre-fetching on IOMMU.
 *
 * 1. Sequential Access or 1D mode
 *	Set ZLW_EN -> 1
 *	set ZLW_PAGE_CROSS_1D -> 1
 *	Set ZLW_N to "N" pages so that ZLW will be inserte N pages ahead where
 *		  N is pre-defined and hardcoded in the platform data
 *	Set ZLW_2D -> 0
 *
 * 2. ZLW 2D mode
 *	Set ZLW_EN -> 1
 *	set ZLW_PAGE_CROSS_1D -> 1,
 *	Set ZLW_N -> 0
 *	Set ZLW_2D -> 1
 *
 * 3. ZLW Enable (no 1D or 2D mode)
 *	Set ZLW_EN -> 1
 *	set ZLW_PAGE_CROSS_1D -> 0,
 *	Set ZLW_N -> 0
 *	Set ZLW_2D -> 0
 *
 * 4. ZLW disable
 *	Set ZLW_EN -> 0
 *	set ZLW_PAGE_CROSS_1D -> 0,
 *	Set ZLW_N -> 0
 *	Set ZLW_2D -> 0
 *
 * To configure the ZLW for the above memory access, four registers are
 * available. Hence to track these four settings, we have the following entries
 * in the struct ipu6_mmu_hw. Each of these entries are per stream and
 * available only for the L1 streams.
 *
 * a. l1_zlw_en -> To track zlw enabled per stream (ZLW_EN)
 * b. l1_zlw_1d_mode -> Track 1D mode per stream. ZLW inserted at page boundary
 * c. l1_ins_zlw_ahead_pages -> to track how advance the ZLW need to be inserted
 *			Insert ZLW request N pages ahead address.
 * d. l1_zlw_2d_mode -> To track 2D mode per stream (ZLW_2D)
 *
 *
 * Currently L1/L2 streams, blocks, AT ZLW configurations etc. are pre-defined
 * as per the usecase specific calculations. Any change to this pre-defined
 * table has to happen in sync with IPU6 FW.
 */

struct ipu6_mmu_hw {
	union {
		unsigned long offset;
		void __iomem *base;
	};
	u32 info_bits;
	u8 nr_l1streams;
	/*
	 * L1 has variable blocks per stream - total of 64 blocks and maximum of
	 * 16 blocks per stream. Configurable by using the block start address
	 * per stream. Block start address is calculated from the block size
	 */
	u8 l1_block_sz[IPU6_MMU_MAX_TLB_L1_STREAMS];
	/* Is ZLW is enabled in each stream */
	bool l1_zlw_en[IPU6_MMU_MAX_TLB_L1_STREAMS];
	bool l1_zlw_1d_mode[IPU6_MMU_MAX_TLB_L1_STREAMS];
	u8 l1_ins_zlw_ahead_pages[IPU6_MMU_MAX_TLB_L1_STREAMS];
	bool l1_zlw_2d_mode[IPU6_MMU_MAX_TLB_L1_STREAMS];

	u32 l1_stream_id_reg_offset;
	u32 l2_stream_id_reg_offset;

	u8 nr_l2streams;
	/*
	 * L2 has fixed 2 blocks per stream. Block address is calculated
	 * from the block size
	 */
	u8 l2_block_sz[IPU6_MMU_MAX_TLB_L2_STREAMS];
	/* flag to track if WA is needed for successive invalidate HW bug */
	bool insert_read_before_invalidate;
};

struct ipu6_mmu_hw_ops {
	int (*init_hw_data)(struct ipu6_mmu *mmu, struct device *dev,
			    void __iomem *base);
	int (*hw_init)(struct ipu6_mmu *mmu);
	void (*tlb_invalidate)(struct ipu6_mmu *mmu);
};

struct ipu6_mmu {
	struct list_head node;

	union {
		struct ipu6_mmu_hw *ipu6_mmu_hw;
		struct ipu7_mmu_hw *ipu7_mmu_hw;
	};
	unsigned int nr_mmus;
	unsigned int mmid;

	phys_addr_t pgtbl;
	struct device *dev;

	struct ipu6_dma_mapping *dmap;
	struct list_head vma_list;

	struct page *trash_page;
	dma_addr_t pci_trash_page; /* IOVA from PCI DMA services (parent) */
	dma_addr_t iova_trash_page; /* IOVA for IPU6 child nodes to use */

	bool ready;
	spinlock_t ready_lock;	/* Serialize access to bool ready */

	const struct ipu6_mmu_hw_ops *ops;
};

extern const struct ipu6_mmu_hw_ops ipu6_mmu_ops;
extern const struct ipu6_mmu_hw_ops ipu7_mmu_ops;

struct ipu6_mmu *ipu6_mmu_init(struct device *dev,
			       void __iomem *base, int mmid);
void ipu6_mmu_cleanup(struct ipu6_mmu *mmu);
int ipu6_mmu_hw_init(struct ipu6_mmu *mmu);
void ipu6_mmu_hw_cleanup(struct ipu6_mmu *mmu);
int ipu6_mmu_map(struct ipu6_mmu_info *mmu_info, unsigned long iova,
		 phys_addr_t paddr, size_t size);
void ipu6_mmu_unmap(struct ipu6_mmu_info *mmu_info, unsigned long iova,
		    size_t size);
phys_addr_t ipu6_mmu_iova_to_phys(struct ipu6_mmu_info *mmu_info,
				  dma_addr_t iova);
#endif
