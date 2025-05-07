/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (C) 2018, 2020-2025 Intel Corporation
 */
#ifndef __iwl_context_info_file_gen3_h__
#define __iwl_context_info_file_gen3_h__

#include "iwl-context-info.h"

#define CSR_CTXT_INFO_BOOT_CTRL         0x0
#define CSR_CTXT_INFO_ADDR              0x118
#define CSR_IML_DATA_ADDR               0x120
#define CSR_IML_SIZE_ADDR               0x128
#define CSR_IML_RESP_ADDR               0x12c

#define UNFRAGMENTED_PNVM_PAYLOADS_NUMBER 2

/* Set bit for enabling automatic function boot */
#define CSR_AUTO_FUNC_BOOT_ENA          BIT(1)
/* Set bit for initiating function boot */
#define CSR_AUTO_FUNC_INIT              BIT(7)

/**
 * enum iwl_prph_scratch_mtr_format - tfd size configuration
 * @IWL_PRPH_MTR_FORMAT_16B: 16 bit tfd
 * @IWL_PRPH_MTR_FORMAT_32B: 32 bit tfd
 * @IWL_PRPH_MTR_FORMAT_64B: 64 bit tfd
 * @IWL_PRPH_MTR_FORMAT_256B: 256 bit tfd
 */
enum iwl_prph_scratch_mtr_format {
	IWL_PRPH_MTR_FORMAT_16B = 0x0,
	IWL_PRPH_MTR_FORMAT_32B = 0x40000,
	IWL_PRPH_MTR_FORMAT_64B = 0x80000,
	IWL_PRPH_MTR_FORMAT_256B = 0xC0000,
};

/**
 * enum iwl_prph_scratch_flags - PRPH scratch control flags
 * @IWL_PRPH_SCRATCH_IMR_DEBUG_EN: IMR support for debug
 * @IWL_PRPH_SCRATCH_EARLY_DEBUG_EN: enable early debug conf
 * @IWL_PRPH_SCRATCH_EDBG_DEST_DRAM: use DRAM, with size allocated
 *	in hwm config.
 * @IWL_PRPH_SCRATCH_EDBG_DEST_INTERNAL: use buffer on SRAM
 * @IWL_PRPH_SCRATCH_EDBG_DEST_ST_ARBITER: use st arbiter, mainly for
 *	multicomm.
 * @IWL_PRPH_SCRATCH_EDBG_DEST_TB22DTF: route debug data to SoC HW
 * @IWL_PRPH_SCRATCH_RB_SIZE_4K: Use 4K RB size (the default is 2K)
 * @IWL_PRPH_SCRATCH_MTR_MODE: format used for completion - 0: for
 *	completion descriptor, 1 for responses (legacy)
 * @IWL_PRPH_SCRATCH_MTR_FORMAT: a mask for the size of the tfd.
 *	There are 4 optional values: 0: 16 bit, 1: 32 bit, 2: 64 bit,
 *	3: 256 bit.
 * @IWL_PRPH_SCRATCH_RB_SIZE_EXT_MASK: RB size full information, ignored
 *	by older firmware versions, so set IWL_PRPH_SCRATCH_RB_SIZE_4K
 *	appropriately; use the below values for this.
 * @IWL_PRPH_SCRATCH_RB_SIZE_EXT_8K: 8kB RB size
 * @IWL_PRPH_SCRATCH_RB_SIZE_EXT_12K: 12kB RB size
 * @IWL_PRPH_SCRATCH_RB_SIZE_EXT_16K: 16kB RB size
 * @IWL_PRPH_SCRATCH_SCU_FORCE_ACTIVE: Indicate fw to set SCU_FORCE_ACTIVE
 *	upon reset.
 */
enum iwl_prph_scratch_flags {
	IWL_PRPH_SCRATCH_IMR_DEBUG_EN		= BIT(1),
	IWL_PRPH_SCRATCH_EARLY_DEBUG_EN		= BIT(4),
	IWL_PRPH_SCRATCH_EDBG_DEST_DRAM		= BIT(8),
	IWL_PRPH_SCRATCH_EDBG_DEST_INTERNAL	= BIT(9),
	IWL_PRPH_SCRATCH_EDBG_DEST_ST_ARBITER	= BIT(10),
	IWL_PRPH_SCRATCH_EDBG_DEST_TB22DTF	= BIT(11),
	IWL_PRPH_SCRATCH_RB_SIZE_4K		= BIT(16),
	IWL_PRPH_SCRATCH_MTR_MODE		= BIT(17),
	IWL_PRPH_SCRATCH_MTR_FORMAT		= BIT(18) | BIT(19),
	IWL_PRPH_SCRATCH_RB_SIZE_EXT_MASK	= 0xf << 20,
	IWL_PRPH_SCRATCH_RB_SIZE_EXT_8K		= 8 << 20,
	IWL_PRPH_SCRATCH_RB_SIZE_EXT_12K	= 9 << 20,
	IWL_PRPH_SCRATCH_RB_SIZE_EXT_16K	= 10 << 20,
	IWL_PRPH_SCRATCH_SCU_FORCE_ACTIVE	= BIT(29),
};

/**
 * enum iwl_prph_scratch_ext_flags - PRPH scratch control ext flags
 * @IWL_PRPH_SCRATCH_EXT_EXT_FSEQ: external FSEQ image provided
 * @IWL_PRPH_SCRATCH_EXT_URM_FW: switch to URM mode based on fw setting
 * @IWL_PRPH_SCRATCH_EXT_URM_PERM: switch to permanent URM mode
 * @IWL_PRPH_SCRATCH_EXT_32KHZ_CLK_VALID: use external 32 KHz clock
 */
enum iwl_prph_scratch_ext_flags {
	IWL_PRPH_SCRATCH_EXT_EXT_FSEQ		= BIT(0),
	IWL_PRPH_SCRATCH_EXT_URM_FW		= BIT(4),
	IWL_PRPH_SCRATCH_EXT_URM_PERM		= BIT(5),
	IWL_PRPH_SCRATCH_EXT_32KHZ_CLK_VALID	= BIT(8),
};

/**
 * struct iwl_prph_scratch_version - version structure
 * @mac_id: SKU and revision id
 * @version: prph scratch information version id
 * @size: the size of the context information in DWs
 * @reserved: reserved
 */
struct iwl_prph_scratch_version {
	__le16 mac_id;
	__le16 version;
	__le16 size;
	__le16 reserved;
} __packed; /* PERIPH_SCRATCH_VERSION_S */

/**
 * struct iwl_prph_scratch_control - control structure
 * @control_flags: context information flags see &enum iwl_prph_scratch_flags
 * @control_flags_ext: context information for extended flags,
 *	see &enum iwl_prph_scratch_ext_flags
 */
struct iwl_prph_scratch_control {
	__le32 control_flags;
	__le32 control_flags_ext;
} __packed; /* PERIPH_SCRATCH_CONTROL_S */

/**
 * struct iwl_prph_scratch_pnvm_cfg - PNVM scratch
 * @pnvm_base_addr: PNVM start address
 * @pnvm_size: the size of the PNVM image in bytes
 * @reserved: reserved
 */
struct iwl_prph_scratch_pnvm_cfg {
	__le64 pnvm_base_addr;
	__le32 pnvm_size;
	__le32 reserved;
} __packed; /* PERIPH_SCRATCH_PNVM_CFG_S */

/**
 * struct iwl_prph_scrath_mem_desc_addr_array
 * @mem_descs: array of dram addresses.
 * Each address is the beggining of a pnvm payload.
 */
struct iwl_prph_scrath_mem_desc_addr_array {
	__le64 mem_descs[IPC_DRAM_MAP_ENTRY_NUM_MAX];
} __packed; /* PERIPH_SCRATCH_MEM_DESC_ADDR_ARRAY_S_VER_1 */

/**
 * struct iwl_prph_scratch_hwm_cfg - hwm config
 * @hwm_base_addr: hwm start address
 * @hwm_size: hwm size in DWs
 * @debug_token_config: debug preset
 */
struct iwl_prph_scratch_hwm_cfg {
	__le64 hwm_base_addr;
	__le32 hwm_size;
	__le32 debug_token_config;
} __packed; /* PERIPH_SCRATCH_HWM_CFG_S */

/**
 * struct iwl_prph_scratch_rbd_cfg - RBDs configuration
 * @free_rbd_addr: default queue free RB CB base address
 * @reserved: reserved
 */
struct iwl_prph_scratch_rbd_cfg {
	__le64 free_rbd_addr;
	__le32 reserved;
} __packed; /* PERIPH_SCRATCH_RBD_CFG_S */

/**
 * struct iwl_prph_scratch_uefi_cfg - prph scratch reduce power table
 * @base_addr: reduce power table address
 * @size: the size of the entire power table image
 * @reserved: (reserved)
 */
struct iwl_prph_scratch_uefi_cfg {
	__le64 base_addr;
	__le32 size;
	__le32 reserved;
} __packed; /* PERIPH_SCRATCH_UEFI_CFG_S */

/**
 * struct iwl_prph_scratch_step_cfg - prph scratch step configuration
 * @mbx_addr_0: [0:7] revision,
 *		[8:15] cnvi_to_cnvr length,
 *		[16:23] cnvr_to_cnvi channel length,
 *		[24:31] radio1 reserved
 * @mbx_addr_1: [0:7] radio2 reserved
 */

struct iwl_prph_scratch_step_cfg {
	__le32 mbx_addr_0;
	__le32 mbx_addr_1;
} __packed;

/**
 * struct iwl_prph_scratch_ctrl_cfg - prph scratch ctrl and config
 * @version: version information of context info and HW
 * @control: control flags of FH configurations
 * @pnvm_cfg: ror configuration
 * @hwm_cfg: hwm configuration
 * @rbd_cfg: default RX queue configuration
 * @reduce_power_cfg: UEFI power reduction table
 * @step_cfg: step configuration
 */
struct iwl_prph_scratch_ctrl_cfg {
	struct iwl_prph_scratch_version version;
	struct iwl_prph_scratch_control control;
	struct iwl_prph_scratch_pnvm_cfg pnvm_cfg;
	struct iwl_prph_scratch_hwm_cfg hwm_cfg;
	struct iwl_prph_scratch_rbd_cfg rbd_cfg;
	struct iwl_prph_scratch_uefi_cfg reduce_power_cfg;
	struct iwl_prph_scratch_step_cfg step_cfg;
} __packed; /* PERIPH_SCRATCH_CTRL_CFG_S */

#define IWL_NUM_DRAM_FSEQ_ENTRIES	8

/**
 * struct iwl_context_info_dram_fseq - images DRAM map (with fseq)
 * each entry in the map represents a DRAM chunk of up to 32 KB
 * @common: UMAC/LMAC/virtual images
 * @fseq_img: FSEQ image DRAM map
 */
struct iwl_context_info_dram_fseq {
	struct iwl_context_info_dram_nonfseq common;
	__le64 fseq_img[IWL_NUM_DRAM_FSEQ_ENTRIES];
} __packed; /* PERIPH_SCRATCH_DRAM_MAP_S */

/**
 * struct iwl_prph_scratch - peripheral scratch mapping
 * @ctrl_cfg: control and configuration of prph scratch
 * @dram: firmware images addresses in DRAM
 * @fseq_override: FSEQ override parameters
 * @step_analog_params: STEP analog calibration values
 * @reserved: reserved
 */
struct iwl_prph_scratch {
	struct iwl_prph_scratch_ctrl_cfg ctrl_cfg;
	__le32 fseq_override;
	__le32 step_analog_params;
	__le32 reserved[8];
	struct iwl_context_info_dram_fseq dram;
} __packed; /* PERIPH_SCRATCH_S */

/**
 * struct iwl_prph_info - peripheral information
 * @boot_stage_mirror: reflects the value in the Boot Stage CSR register
 * @ipc_status_mirror: reflects the value in the IPC Status CSR register
 * @sleep_notif: indicates the peripheral sleep status
 * @reserved: reserved
 */
struct iwl_prph_info {
	__le32 boot_stage_mirror;
	__le32 ipc_status_mirror;
	__le32 sleep_notif;
	__le32 reserved;
} __packed; /* PERIPH_INFO_S */

/**
 * struct iwl_context_info_gen3 - device INIT configuration
 * @version: version of the context information
 * @size: size of context information in DWs
 * @config: context in which the peripheral would execute - a subset of
 *	capability csr register published by the peripheral
 * @prph_info_base_addr: the peripheral information structure start address
 * @cr_head_idx_arr_base_addr: the completion ring head index array
 *	start address
 * @tr_tail_idx_arr_base_addr: the transfer ring tail index array
 *	start address
 * @cr_tail_idx_arr_base_addr: the completion ring tail index array
 *	start address
 * @tr_head_idx_arr_base_addr: the transfer ring head index array
 *	start address
 * @cr_idx_arr_size: number of entries in the completion ring index array
 * @tr_idx_arr_size: number of entries in the transfer ring index array
 * @mtr_base_addr: the message transfer ring start address
 * @mcr_base_addr: the message completion ring start address
 * @mtr_size: number of entries which the message transfer ring can hold
 * @mcr_size: number of entries which the message completion ring can hold
 * @mtr_doorbell_vec: the doorbell vector associated with the message
 *	transfer ring
 * @mcr_doorbell_vec: the doorbell vector associated with the message
 *	completion ring
 * @mtr_msi_vec: the MSI which shall be generated by the peripheral after
 *	completing a transfer descriptor in the message transfer ring
 * @mcr_msi_vec: the MSI which shall be generated by the peripheral after
 *	completing a completion descriptor in the message completion ring
 * @mtr_opt_header_size: the size of the optional header in the transfer
 *	descriptor associated with the message transfer ring in DWs
 * @mtr_opt_footer_size: the size of the optional footer in the transfer
 *	descriptor associated with the message transfer ring in DWs
 * @mcr_opt_header_size: the size of the optional header in the completion
 *	descriptor associated with the message completion ring in DWs
 * @mcr_opt_footer_size: the size of the optional footer in the completion
 *	descriptor associated with the message completion ring in DWs
 * @msg_rings_ctrl_flags: message rings control flags
 * @prph_info_msi_vec: the MSI which shall be generated by the peripheral
 *	after updating the Peripheral Information structure
 * @prph_scratch_base_addr: the peripheral scratch structure start address
 * @prph_scratch_size: the size of the peripheral scratch structure in DWs
 * @reserved: reserved
 */
struct iwl_context_info_gen3 {
	__le16 version;
	__le16 size;
	__le32 config;
	__le64 prph_info_base_addr;
	__le64 cr_head_idx_arr_base_addr;
	__le64 tr_tail_idx_arr_base_addr;
	__le64 cr_tail_idx_arr_base_addr;
	__le64 tr_head_idx_arr_base_addr;
	__le16 cr_idx_arr_size;
	__le16 tr_idx_arr_size;
	__le64 mtr_base_addr;
	__le64 mcr_base_addr;
	__le16 mtr_size;
	__le16 mcr_size;
	__le16 mtr_doorbell_vec;
	__le16 mcr_doorbell_vec;
	__le16 mtr_msi_vec;
	__le16 mcr_msi_vec;
	u8 mtr_opt_header_size;
	u8 mtr_opt_footer_size;
	u8 mcr_opt_header_size;
	u8 mcr_opt_footer_size;
	__le16 msg_rings_ctrl_flags;
	__le16 prph_info_msi_vec;
	__le64 prph_scratch_base_addr;
	__le32 prph_scratch_size;
	__le32 reserved;
} __packed; /* IPC_CONTEXT_INFO_S */

int iwl_pcie_ctxt_info_gen3_init(struct iwl_trans *trans,
				 const struct fw_img *fw);
void iwl_pcie_ctxt_info_gen3_free(struct iwl_trans *trans, bool alive);

int iwl_trans_pcie_ctx_info_gen3_load_pnvm(struct iwl_trans *trans,
					   const struct iwl_pnvm_image *pnvm_payloads,
					   const struct iwl_ucode_capabilities *capa);
void iwl_trans_pcie_ctx_info_gen3_set_pnvm(struct iwl_trans *trans,
					   const struct iwl_ucode_capabilities *capa);
int
iwl_trans_pcie_ctx_info_gen3_load_reduce_power(struct iwl_trans *trans,
					       const struct iwl_pnvm_image *payloads,
					       const struct iwl_ucode_capabilities *capa);
void
iwl_trans_pcie_ctx_info_gen3_set_reduce_power(struct iwl_trans *trans,
					      const struct iwl_ucode_capabilities *capa);
int iwl_trans_pcie_ctx_info_gen3_set_step(struct iwl_trans *trans,
					  u32 mbx_addr_0_step, u32 mbx_addr_1_step);
#endif /* __iwl_context_info_file_gen3_h__ */
