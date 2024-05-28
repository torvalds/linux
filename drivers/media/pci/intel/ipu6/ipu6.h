/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2013 - 2024 Intel Corporation */

#ifndef IPU6_H
#define IPU6_H

#include <linux/list.h>
#include <linux/pci.h>
#include <linux/types.h>

#include "ipu6-buttress.h"

struct firmware;
struct pci_dev;
struct ipu6_bus_device;

#define IPU6_NAME			"intel-ipu6"
#define IPU6_MEDIA_DEV_MODEL_NAME	"ipu6"

#define IPU6SE_FIRMWARE_NAME		"intel/ipu/ipu6se_fw.bin"
#define IPU6EP_FIRMWARE_NAME		"intel/ipu/ipu6ep_fw.bin"
#define IPU6_FIRMWARE_NAME		"intel/ipu/ipu6_fw.bin"
#define IPU6EPMTL_FIRMWARE_NAME		"intel/ipu/ipu6epmtl_fw.bin"
#define IPU6EPADLN_FIRMWARE_NAME	"intel/ipu/ipu6epadln_fw.bin"

enum ipu6_version {
	IPU6_VER_INVALID = 0,
	IPU6_VER_6 = 1,
	IPU6_VER_6SE = 3,
	IPU6_VER_6EP = 5,
	IPU6_VER_6EP_MTL = 6,
};

/*
 * IPU6 - TGL
 * IPU6SE - JSL
 * IPU6EP - ADL/RPL
 * IPU6EP_MTL - MTL
 */
static inline bool is_ipu6se(u8 hw_ver)
{
	return hw_ver == IPU6_VER_6SE;
}

static inline bool is_ipu6ep(u8 hw_ver)
{
	return hw_ver == IPU6_VER_6EP;
}

static inline bool is_ipu6ep_mtl(u8 hw_ver)
{
	return hw_ver == IPU6_VER_6EP_MTL;
}

static inline bool is_ipu6_tgl(u8 hw_ver)
{
	return hw_ver == IPU6_VER_6;
}

/*
 * ISYS DMA can overshoot. For higher resolutions over allocation is one line
 * but it must be at minimum 1024 bytes. Value could be different in
 * different versions / generations thus provide it via platform data.
 */
#define IPU6_ISYS_OVERALLOC_MIN		1024

/* Physical pages in GDA is 128, page size is 2K for IPU6, 1K for others */
#define IPU6_DEVICE_GDA_NR_PAGES		128

/* Virtualization factor to calculate the available virtual pages */
#define IPU6_DEVICE_GDA_VIRT_FACTOR	32

struct ipu6_device {
	struct pci_dev *pdev;
	struct list_head devices;
	struct ipu6_bus_device *isys;
	struct ipu6_bus_device *psys;
	struct ipu6_buttress buttress;

	const struct firmware *cpd_fw;
	const char *cpd_fw_name;
	u32 cpd_metadata_cmpnt_size;

	void __iomem *base;
	bool need_ipc_reset;
	bool secure_mode;
	u8 hw_ver;
	bool bus_ready_to_probe;
};

#define IPU6_ISYS_NAME "isys"
#define IPU6_PSYS_NAME "psys"

#define IPU6_MMU_MAX_DEVICES		4
#define IPU6_MMU_ADDR_BITS		32
/* The firmware is accessible within the first 2 GiB only in non-secure mode. */
#define IPU6_MMU_ADDR_BITS_NON_SECURE	31

#define IPU6_MMU_MAX_TLB_L1_STREAMS	32
#define IPU6_MMU_MAX_TLB_L2_STREAMS	32
#define IPU6_MAX_LI_BLOCK_ADDR		128
#define IPU6_MAX_L2_BLOCK_ADDR		64

#define IPU6SE_ISYS_NUM_STREAMS          IPU6SE_NONSECURE_STREAM_ID_MAX
#define IPU6_ISYS_NUM_STREAMS            IPU6_NONSECURE_STREAM_ID_MAX

/*
 * To maximize the IOSF utlization, IPU6 need to send requests in bursts.
 * At the DMA interface with the buttress, there are CDC FIFOs with burst
 * collection capability. CDC FIFO burst collectors have a configurable
 * threshold and is configured based on the outcome of performance measurements.
 *
 * isys has 3 ports with IOSF interface for VC0, VC1 and VC2
 * psys has 4 ports with IOSF interface for VC0, VC1w, VC1r and VC2
 *
 * Threshold values are pre-defined and are arrived at after performance
 * evaluations on a type of IPU6
 */
#define IPU6_MAX_VC_IOSF_PORTS		4

/*
 * IPU6 must configure correct arbitration mechanism related to the IOSF VC
 * requests. There are two options per VC0 and VC1 - > 0 means rearbitrate on
 * stall and 1 means stall until the request is completed.
 */
#define IPU6_BTRS_ARB_MODE_TYPE_REARB	0
#define IPU6_BTRS_ARB_MODE_TYPE_STALL	1

/* Currently chosen arbitration mechanism for VC0 */
#define IPU6_BTRS_ARB_STALL_MODE_VC0	\
			IPU6_BTRS_ARB_MODE_TYPE_REARB

/* Currently chosen arbitration mechanism for VC1 */
#define IPU6_BTRS_ARB_STALL_MODE_VC1	\
			IPU6_BTRS_ARB_MODE_TYPE_REARB

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

struct ipu6_mmu_pdata {
	u32 nr_mmus;
	struct ipu6_mmu_hw mmu_hw[IPU6_MMU_MAX_DEVICES];
	int mmid;
};

struct ipu6_isys_csi2_pdata {
	void __iomem *base;
};

struct ipu6_isys_internal_csi2_pdata {
	u32 nports;
	u32 irq_mask;
	u32 ctrl0_irq_edge;
	u32 ctrl0_irq_clear;
	u32 ctrl0_irq_mask;
	u32 ctrl0_irq_enable;
	u32 ctrl0_irq_lnp;
	u32 ctrl0_irq_status;
	u32 fw_access_port_ofs;
};

struct ipu6_isys_internal_tpg_pdata {
	u32 ntpgs;
	u32 *offsets;
	u32 *sels;
};

struct ipu6_hw_variants {
	unsigned long offset;
	u32 nr_mmus;
	struct ipu6_mmu_hw mmu_hw[IPU6_MMU_MAX_DEVICES];
	u8 cdc_fifos;
	u8 cdc_fifo_threshold[IPU6_MAX_VC_IOSF_PORTS];
	u32 dmem_offset;
	u32 spc_offset;
};

struct ipu6_isys_internal_pdata {
	struct ipu6_isys_internal_csi2_pdata csi2;
	struct ipu6_hw_variants hw_variant;
	u32 num_parallel_streams;
	u32 isys_dma_overshoot;
	u32 sram_gran_shift;
	u32 sram_gran_size;
	u32 max_sram_size;
	u32 max_streams;
	u32 max_send_queues;
	u32 max_sram_blocks;
	u32 max_devq_size;
	u32 sensor_type_start;
	u32 sensor_type_end;
	u32 ltr;
	u32 memopen_threshold;
	bool enhanced_iwake;
};

struct ipu6_isys_pdata {
	void __iomem *base;
	const struct ipu6_isys_internal_pdata *ipdata;
};

struct ipu6_psys_internal_pdata {
	struct ipu6_hw_variants hw_variant;
};

struct ipu6_psys_pdata {
	void __iomem *base;
	const struct ipu6_psys_internal_pdata *ipdata;
};

int ipu6_fw_authenticate(void *data, u64 val);
void ipu6_configure_spc(struct ipu6_device *isp,
			const struct ipu6_hw_variants *hw_variant,
			int pkg_dir_idx, void __iomem *base, u64 *pkg_dir,
			dma_addr_t pkg_dir_dma_addr);
#endif /* IPU6_H */
