/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2013 - 2025 Intel Corporation
 */

#ifndef IPU7_H
#define IPU7_H

#include <linux/list.h>
#include <linux/pci.h>
#include <linux/types.h>

#include "ipu7-buttress.h"

struct ipu7_bus_device;
struct pci_dev;
struct firmware;

#define IPU_NAME			"intel-ipu7"
#define IPU_MEDIA_DEV_MODEL_NAME	"ipu7"

#define IPU7_FIRMWARE_NAME		"intel/ipu/ipu7_fw.bin"
#define IPU7P5_FIRMWARE_NAME		"intel/ipu/ipu7ptl_fw.bin"
#define IPU8_FIRMWARE_NAME		"intel/ipu/ipu8_fw.bin"

#define IPU7_ISYS_NUM_STREAMS		12

#define IPU7_PCI_ID			0x645d
#define IPU7P5_PCI_ID			0xb05d
#define IPU8_PCI_ID			0xd719

#define FW_LOG_BUF_SIZE			(2 * 1024 * 1024)

enum ipu_version {
	IPU_VER_INVALID = 0,
	IPU_VER_7 = 1,
	IPU_VER_7P5 = 2,
	IPU_VER_8 = 3,
};

static inline bool is_ipu7p5(u8 hw_ver)
{
	return hw_ver == IPU_VER_7P5;
}

static inline bool is_ipu7(u8 hw_ver)
{
	return hw_ver == IPU_VER_7;
}

static inline bool is_ipu8(u8 hw_ver)
{
	return hw_ver == IPU_VER_8;
}

#define IPU_UNIFIED_OFFSET		0

/*
 * ISYS DMA can overshoot. For higher resolutions over allocation is one line
 * but it must be at minimum 1024 bytes. Value could be different in
 * different versions / generations thus provide it via platform data.
 */
#define IPU_ISYS_OVERALLOC_MIN		1024

#define IPU_FW_CODE_REGION_SIZE		0x1000000 /* 16MB */
#define IPU_FW_CODE_REGION_START	0x4000000 /* 64MB */
#define IPU_FW_CODE_REGION_END		(IPU_FW_CODE_REGION_START +	\
					 IPU_FW_CODE_REGION_SIZE) /* 80MB */

struct ipu7_device {
	struct pci_dev *pdev;
	struct list_head devices;
	struct ipu7_bus_device *isys;
	struct ipu7_bus_device *psys;
	struct ipu_buttress buttress;

	const struct firmware *cpd_fw;
	const char *cpd_fw_name;
	/* Only for non-secure mode. */
	void *fw_code_region;

	void __iomem *base;
	void __iomem *pb_base;
	u8 hw_ver;
	bool ipc_reinit;
	bool secure_mode;
	bool ipu7_bus_ready_to_probe;
};

#define IPU_DMA_MASK			39
#define IPU_LIB_CALL_TIMEOUT_MS		2000
#define IPU_PSYS_CMD_TIMEOUT_MS		2000
#define IPU_PSYS_OPEN_CLOSE_TIMEOUT_US	50
#define IPU_PSYS_OPEN_CLOSE_RETRY	(10000 / IPU_PSYS_OPEN_CLOSE_TIMEOUT_US)

#define IPU_ISYS_NAME "isys"
#define IPU_PSYS_NAME "psys"

#define IPU_MMU_ADDR_BITS		32
/* FW is accessible within the first 2 GiB only in non-secure mode. */
#define IPU_MMU_ADDR_BITS_NON_SECURE	31

#define IPU7_IS_MMU_NUM			4U
#define IPU7_PS_MMU_NUM			4U
#define IPU7P5_IS_MMU_NUM		4U
#define IPU7P5_PS_MMU_NUM		4U
#define IPU8_IS_MMU_NUM			5U
#define IPU8_PS_MMU_NUM			4U
#define IPU_MMU_MAX_NUM			5U /* max(IS, PS) */
#define IPU_MMU_MAX_TLB_L1_STREAMS	40U
#define IPU_MMU_MAX_TLB_L2_STREAMS	40U
#define IPU_ZLX_MAX_NUM			32U
#define IPU_ZLX_POOL_NUM		8U
#define IPU_UAO_PLANE_MAX_NUM		64U

/*
 * To maximize the IOSF utlization, IPU need to send requests in bursts.
 * At the DMA interface with the buttress, there are CDC FIFOs with burst
 * collection capability. CDC FIFO burst collectors have a configurable
 * threshold and is configured based on the outcome of performance measurements.
 *
 * isys has 3 ports with IOSF interface for VC0, VC1 and VC2
 * psys has 4 ports with IOSF interface for VC0, VC1w, VC1r and VC2
 *
 * Threshold values are pre-defined and are arrived at after performance
 * evaluations on a type of IPU
 */
#define IPU_MAX_VC_IOSF_PORTS		4

/*
 * IPU must configure correct arbitration mechanism related to the IOSF VC
 * requests. There are two options per VC0 and VC1 - > 0 means rearbitrate on
 * stall and 1 means stall until the request is completed.
 */
#define IPU_BTRS_ARB_MODE_TYPE_REARB	0
#define IPU_BTRS_ARB_MODE_TYPE_STALL	1

/* Currently chosen arbitration mechanism for VC0 */
#define IPU_BTRS_ARB_STALL_MODE_VC0	IPU_BTRS_ARB_MODE_TYPE_REARB

/* Currently chosen arbitration mechanism for VC1 */
#define IPU_BTRS_ARB_STALL_MODE_VC1	IPU_BTRS_ARB_MODE_TYPE_REARB

/* One L2 entry maps 1024 L1 entries and one L1 entry per page */
#define IPU_MMUV2_L2_RANGE		(1024 * PAGE_SIZE)
/* Max L2 blocks per stream */
#define IPU_MMUV2_MAX_L2_BLOCKS		2
/* Max L1 blocks per stream */
#define IPU_MMUV2_MAX_L1_BLOCKS		16
#define IPU_MMUV2_TRASH_RANGE		(IPU_MMUV2_L2_RANGE *	\
					 IPU_MMUV2_MAX_L2_BLOCKS)
/* Entries per L1 block */
#define MMUV2_ENTRIES_PER_L1_BLOCK	16
#define MMUV2_TRASH_L1_BLOCK_OFFSET	(MMUV2_ENTRIES_PER_L1_BLOCK * PAGE_SIZE)
#define MMUV2_TRASH_L2_BLOCK_OFFSET	IPU_MMUV2_L2_RANGE

struct ipu7_mmu_hw {
	char name[32];

	void __iomem *base;
	void __iomem *zlx_base;
	void __iomem *uao_base;

	u32 offset;
	u32 zlx_offset;
	u32 uao_offset;

	u32 info_bits;
	u32 refill;
	u32 collapse_en_bitmap;
	u32 at_sp_arb_cfg;

	u32 l1_block;
	u32 l2_block;

	u8 nr_l1streams;
	u8 nr_l2streams;
	u32 l1_block_sz[IPU_MMU_MAX_TLB_L1_STREAMS];
	u32 l2_block_sz[IPU_MMU_MAX_TLB_L2_STREAMS];

	u8 zlx_nr;
	u32 zlx_axi_pool[IPU_ZLX_POOL_NUM];
	u32 zlx_en[IPU_ZLX_MAX_NUM];
	u32 zlx_conf[IPU_ZLX_MAX_NUM];

	u32 uao_p_num;
	u32 uao_p2tlb[IPU_UAO_PLANE_MAX_NUM];
};

struct ipu7_mmu_pdata {
	u32 nr_mmus;
	struct ipu7_mmu_hw mmu_hw[IPU_MMU_MAX_NUM];
	int mmid;
};

struct ipu7_isys_csi2_pdata {
	void __iomem *base;
};

struct ipu7_isys_internal_csi2_pdata {
	u32 nports;
	u32 const *offsets;
	u32 gpreg;
};

struct ipu7_hw_variants {
	unsigned long offset;
	u32 nr_mmus;
	struct ipu7_mmu_hw mmu_hw[IPU_MMU_MAX_NUM];
	u8 cdc_fifos;
	u8 cdc_fifo_threshold[IPU_MAX_VC_IOSF_PORTS];
	u32 dmem_offset;
	u32 spc_offset;	/* SPC offset from psys base */
};

struct ipu_isys_internal_pdata {
	struct ipu7_isys_internal_csi2_pdata csi2;
	struct ipu7_hw_variants hw_variant;
	u32 num_parallel_streams;
	u32 isys_dma_overshoot;
};

struct ipu7_isys_pdata {
	void __iomem *base;
	const struct ipu_isys_internal_pdata *ipdata;
};

struct ipu_psys_internal_pdata {
	struct ipu7_hw_variants hw_variant;
};

struct ipu7_psys_pdata {
	void __iomem *base;
	const struct ipu_psys_internal_pdata *ipdata;
};

int request_cpd_fw(const struct firmware **firmware_p, const char *name,
		   struct device *device);
void ipu_internal_pdata_init(struct ipu_isys_internal_pdata *isys_ipdata,
			     struct ipu_psys_internal_pdata *psys_ipdata);
void ipu7_dump_fw_error_log(const struct ipu7_bus_device *adev);
#endif /* IPU7_H */
