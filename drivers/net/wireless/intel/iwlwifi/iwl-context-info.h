/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (C) 2017 Intel Deutschland GmbH
 * Copyright (C) 2018-2020, 2022, 2024-2025 Intel Corporation
 */
#ifndef __iwl_context_info_file_h__
#define __iwl_context_info_file_h__

/* maximum number of DRAM map entries supported by FW */
#define IWL_MAX_DRAM_ENTRY	64
#define CSR_CTXT_INFO_BA	0x40

/**
 * enum iwl_context_info_flags - Context information control flags
 * @IWL_CTXT_INFO_AUTO_FUNC_INIT: If set, FW will not wait before interrupting
 *	the init done for driver command that configures several system modes
 * @IWL_CTXT_INFO_EARLY_DEBUG: enable early debug
 * @IWL_CTXT_INFO_ENABLE_CDMP: enable core dump
 * @IWL_CTXT_INFO_RB_CB_SIZE: mask of the RBD Cyclic Buffer Size
 *	exponent, the actual size is 2**value, valid sizes are 8-2048.
 *	The value is four bits long. Maximum valid exponent is 12
 * @IWL_CTXT_INFO_TFD_FORMAT_LONG: use long TFD Format (the
 *	default is short format - not supported by the driver)
 * @IWL_CTXT_INFO_RB_SIZE: RB size mask
 *	(values are IWL_CTXT_INFO_RB_SIZE_*K)
 * @IWL_CTXT_INFO_RB_SIZE_1K: Value for 1K RB size
 * @IWL_CTXT_INFO_RB_SIZE_2K: Value for 2K RB size
 * @IWL_CTXT_INFO_RB_SIZE_4K: Value for 4K RB size
 * @IWL_CTXT_INFO_RB_SIZE_8K: Value for 8K RB size
 * @IWL_CTXT_INFO_RB_SIZE_12K: Value for 12K RB size
 * @IWL_CTXT_INFO_RB_SIZE_16K: Value for 16K RB size
 * @IWL_CTXT_INFO_RB_SIZE_20K: Value for 20K RB size
 * @IWL_CTXT_INFO_RB_SIZE_24K: Value for 24K RB size
 * @IWL_CTXT_INFO_RB_SIZE_28K: Value for 28K RB size
 * @IWL_CTXT_INFO_RB_SIZE_32K: Value for 32K RB size
 */
enum iwl_context_info_flags {
	IWL_CTXT_INFO_AUTO_FUNC_INIT	= 0x0001,
	IWL_CTXT_INFO_EARLY_DEBUG	= 0x0002,
	IWL_CTXT_INFO_ENABLE_CDMP	= 0x0004,
	IWL_CTXT_INFO_RB_CB_SIZE	= 0x00f0,
	IWL_CTXT_INFO_TFD_FORMAT_LONG	= 0x0100,
	IWL_CTXT_INFO_RB_SIZE		= 0x1e00,
	IWL_CTXT_INFO_RB_SIZE_1K	= 0x1,
	IWL_CTXT_INFO_RB_SIZE_2K	= 0x2,
	IWL_CTXT_INFO_RB_SIZE_4K	= 0x4,
	IWL_CTXT_INFO_RB_SIZE_8K	= 0x8,
	IWL_CTXT_INFO_RB_SIZE_12K	= 0x9,
	IWL_CTXT_INFO_RB_SIZE_16K	= 0xa,
	IWL_CTXT_INFO_RB_SIZE_20K	= 0xb,
	IWL_CTXT_INFO_RB_SIZE_24K	= 0xc,
	IWL_CTXT_INFO_RB_SIZE_28K	= 0xd,
	IWL_CTXT_INFO_RB_SIZE_32K	= 0xe,
};

/**
 * struct iwl_context_info_version - version structure
 * @mac_id: SKU and revision id
 * @version: context information version id
 * @size: the size of the context information in DWs
 * @reserved: (reserved)
 */
struct iwl_context_info_version {
	__le16 mac_id;
	__le16 version;
	__le16 size;
	__le16 reserved;
} __packed;

/**
 * struct iwl_context_info_control - version structure
 * @control_flags: context information flags see &enum iwl_context_info_flags
 * @reserved: (reserved)
 */
struct iwl_context_info_control {
	__le32 control_flags;
	__le32 reserved;
} __packed;

/**
 * struct iwl_context_info_dram_nonfseq - images DRAM map
 * each entry in the map represents a DRAM chunk of up to 32 KB
 * @umac_img: UMAC image DRAM map
 * @lmac_img: LMAC image DRAM map
 * @virtual_img: paged image DRAM map
 */
struct iwl_context_info_dram_nonfseq {
	__le64 umac_img[IWL_MAX_DRAM_ENTRY];
	__le64 lmac_img[IWL_MAX_DRAM_ENTRY];
	__le64 virtual_img[IWL_MAX_DRAM_ENTRY];
} __packed;

/**
 * struct iwl_context_info_rbd_cfg - RBDs configuration
 * @free_rbd_addr: default queue free RB CB base address
 * @used_rbd_addr: default queue used RB CB base address
 * @status_wr_ptr: default queue used RB status write pointer
 */
struct iwl_context_info_rbd_cfg {
	__le64 free_rbd_addr;
	__le64 used_rbd_addr;
	__le64 status_wr_ptr;
} __packed;

/**
 * struct iwl_context_info_hcmd_cfg  - command queue configuration
 * @cmd_queue_addr: address of command queue
 * @cmd_queue_size: number of entries
 * @reserved: (reserved)
 */
struct iwl_context_info_hcmd_cfg {
	__le64 cmd_queue_addr;
	u8 cmd_queue_size;
	u8 reserved[7];
} __packed;

/**
 * struct iwl_context_info_dump_cfg - Core Dump configuration
 * @core_dump_addr: core dump (debug DRAM address) start address
 * @core_dump_size: size, in DWs
 * @reserved: (reserved)
 */
struct iwl_context_info_dump_cfg {
	__le64 core_dump_addr;
	__le32 core_dump_size;
	__le32 reserved;
} __packed;

/**
 * struct iwl_context_info_pnvm_cfg - platform NVM data configuration
 * @platform_nvm_addr: Platform NVM data start address
 * @platform_nvm_size: size in DWs
 * @reserved: (reserved)
 */
struct iwl_context_info_pnvm_cfg {
	__le64 platform_nvm_addr;
	__le32 platform_nvm_size;
	__le32 reserved;
} __packed;

/**
 * struct iwl_context_info_early_dbg_cfg - early debug configuration for
 *	dumping DRAM addresses
 * @early_debug_addr: early debug start address
 * @early_debug_size: size in DWs
 * @reserved: (reserved)
 */
struct iwl_context_info_early_dbg_cfg {
	__le64 early_debug_addr;
	__le32 early_debug_size;
	__le32 reserved;
} __packed;

/**
 * struct iwl_context_info - device INIT configuration
 * @version: version information of context info and HW
 * @control: control flags of FH configurations
 * @reserved0: (reserved)
 * @rbd_cfg: default RX queue configuration
 * @hcmd_cfg: command queue configuration
 * @reserved1: (reserved)
 * @dump_cfg: core dump data
 * @edbg_cfg: early debug configuration
 * @pnvm_cfg: platform nvm configuration
 * @reserved2: (reserved)
 * @dram: firmware image addresses in DRAM
 * @reserved3: (reserved)
 */
struct iwl_context_info {
	struct iwl_context_info_version version;
	struct iwl_context_info_control control;
	__le64 reserved0;
	struct iwl_context_info_rbd_cfg rbd_cfg;
	struct iwl_context_info_hcmd_cfg hcmd_cfg;
	__le32 reserved1[4];
	struct iwl_context_info_dump_cfg dump_cfg;
	struct iwl_context_info_early_dbg_cfg edbg_cfg;
	struct iwl_context_info_pnvm_cfg pnvm_cfg;
	__le32 reserved2[16];
	struct iwl_context_info_dram_nonfseq dram;
	__le32 reserved3[16];
} __packed;

int iwl_pcie_ctxt_info_init(struct iwl_trans *trans, const struct fw_img *img);
void iwl_pcie_ctxt_info_free(struct iwl_trans *trans);
void iwl_pcie_ctxt_info_free_paging(struct iwl_trans *trans);
int iwl_pcie_init_fw_sec(struct iwl_trans *trans,
			 const struct fw_img *fw,
			 struct iwl_context_info_dram_nonfseq *ctxt_dram);
void *iwl_pcie_ctxt_info_dma_alloc_coherent(struct iwl_trans *trans,
					    size_t size,
					    dma_addr_t *phys);
int iwl_pcie_ctxt_info_alloc_dma(struct iwl_trans *trans,
				 const void *data, u32 len,
				 struct iwl_dram_data *dram);

#endif /* __iwl_context_info_file_h__ */
