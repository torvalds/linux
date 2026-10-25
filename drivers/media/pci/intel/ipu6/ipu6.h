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
#define IPU7_MEDIA_DEV_MODEL_NAME	"ipu7"
#define IPU7P5_MEDIA_DEV_MODEL_NAME	"ipu7.5"
#define IPU8_MEDIA_DEV_MODEL_NAME	"ipu8"

#define IPU6SE_FIRMWARE_NAME		"intel/ipu/ipu6se_fw.bin"
#define IPU6EP_FIRMWARE_NAME		"intel/ipu/ipu6ep_fw.bin"
#define IPU6_FIRMWARE_NAME		"intel/ipu/ipu6_fw.bin"
#define IPU6EPMTL_FIRMWARE_NAME		"intel/ipu/ipu6epmtl_fw.bin"
#define IPU6EPADLN_FIRMWARE_NAME	"intel/ipu/ipu6epadln_fw.bin"
#define IPU7_FIRMWARE_NAME		"intel/ipu/ipu7_fw.bin"
#define IPU7P5_FIRMWARE_NAME		"intel/ipu/ipu7ptl_fw.bin"
#define IPU8_FIRMWARE_NAME		"intel/ipu/ipu8_fw.bin"

#define IPU_VERSION_6		BIT(0) /* TGL */
#define IPU_VERSION_6SE		BIT(1) /* JSL */
#define IPU_VERSION_6EP		BIT(2) /* ADL/RPL */
#define IPU_VERSION_6EP_MTL	BIT(3) /* MTL */
#define IPU_VERSION_7		BIT(4) /* LNL */
#define IPU_VERSION_7P5		BIT(5) /* PTL */
#define IPU_VERSION_8		BIT(6) /* NVL */

#define IS_IPU6_TGL(isp)	((isp)->hw_ver & IPU_VERSION_6)
#define IS_IPU6SE(isp)		((isp)->hw_ver & IPU_VERSION_6SE)
#define IS_IPU6EP(isp)		((isp)->hw_ver & IPU_VERSION_6EP)
#define IS_IPU6EP_MTL(isp)	((isp)->hw_ver & IPU_VERSION_6EP_MTL)
#define IS_IPU7(isp)		((isp)->hw_ver & \
				 (IPU_VERSION_7 | IPU_VERSION_7P5))
#define IS_IPU7_MTL(isp)	((isp)->hw_ver & IPU_VERSION_7)
#define IS_IPU7P5(isp)		((isp)->hw_ver & IPU_VERSION_7P5)
#define IS_IPU8(isp)		((isp)->hw_ver & IPU_VERSION_8)

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
	const char *model_name;

	void __iomem *base;
	void __iomem *pb_base;
	bool need_ipc_reset;
	bool secure_mode;
	u8 hw_ver;
	bool bus_ready_to_probe;
	u32 *fw_code_region;
};

#define IPU_PSYS	0
#define IPU_ISYS	1
#define IPU_SUBSYS_NUM	2

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
	/* IPU7-specific field */
	u32 gpreg;
	u32 gpreg_stride;
};

struct ipu6_isys_internal_tpg_pdata {
	u32 ntpgs;
	u32 *offsets;
	u32 *sels;
};

struct ipu6_hw_variants {
	unsigned long offset;
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
