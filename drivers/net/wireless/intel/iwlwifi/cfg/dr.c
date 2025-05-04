// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2024-2025 Intel Corporation
 */
#include <linux/module.h>
#include <linux/stringify.h>
#include "iwl-config.h"
#include "iwl-prph.h"
#include "fw/api/txq.h"

/* Highest firmware API version supported */
#define IWL_DR_UCODE_API_MAX	99

/* Lowest firmware API version supported */
#define IWL_DR_UCODE_API_MIN	97

/* NVM versions */
#define IWL_DR_NVM_VERSION		0x0a1d

/* Memory offsets and lengths */
#define IWL_DR_DCCM_OFFSET		0x800000 /* LMAC1 */
#define IWL_DR_DCCM_LEN			0x10000 /* LMAC1 */
#define IWL_DR_DCCM2_OFFSET		0x880000
#define IWL_DR_DCCM2_LEN		0x8000
#define IWL_DR_SMEM_OFFSET		0x400000
#define IWL_DR_SMEM_LEN			0xD0000

#define IWL_DR_A_PE_A_FW_PRE		"iwlwifi-dr-a0-pe-a0"
#define IWL_BR_A_PET_A_FW_PRE		"iwlwifi-br-a0-petc-a0"
#define IWL_BR_A_PE_A_FW_PRE		"iwlwifi-br-a0-pe-a0"

#define IWL_DR_A_PE_A_FW_MODULE_FIRMWARE(api) \
	IWL_DR_A_PE_A_FW_PRE "-" __stringify(api) ".ucode"
#define IWL_BR_A_PET_A_FW_MODULE_FIRMWARE(api) \
	IWL_BR_A_PET_A_FW_PRE "-" __stringify(api) ".ucode"
#define IWL_BR_A_PE_A_FW_MODULE_FIRMWARE(api) \
	IWL_BR_A_PE_A_FW_PRE "-" __stringify(api) ".ucode"

static const struct iwl_base_params iwl_dr_base_params = {
	.eeprom_size = OTP_LOW_IMAGE_SIZE_32K,
	.num_of_queues = 512,
	.max_tfd_queue_size = 65536,
	.shadow_ram_support = true,
	.led_compensation = 57,
	.wd_timeout = IWL_LONG_WD_TIMEOUT,
	.max_event_log_size = 512,
	.shadow_reg_enable = true,
	.pcie_l1_allowed = true,
};

#define IWL_DEVICE_DR_COMMON						\
	.ucode_api_max = IWL_DR_UCODE_API_MAX,			\
	.ucode_api_min = IWL_DR_UCODE_API_MIN,			\
	.led_mode = IWL_LED_RF_STATE,					\
	.nvm_hw_section_num = 10,					\
	.non_shared_ant = ANT_B,					\
	.dccm_offset = IWL_DR_DCCM_OFFSET,				\
	.dccm_len = IWL_DR_DCCM_LEN,					\
	.dccm2_offset = IWL_DR_DCCM2_OFFSET,				\
	.dccm2_len = IWL_DR_DCCM2_LEN,				\
	.smem_offset = IWL_DR_SMEM_OFFSET,				\
	.smem_len = IWL_DR_SMEM_LEN,					\
	.apmg_not_supported = true,					\
	.vht_mu_mimo_supported = true,					\
	.mac_addr_from_csr = 0x30,					\
	.nvm_ver = IWL_DR_NVM_VERSION,				\
	.nvm_type = IWL_NVM_EXT,					\
	.dbgc_supported = true,						\
	.min_umac_error_event_table = 0xD0000,				\
	.d3_debug_data_base_addr = 0x401000,				\
	.d3_debug_data_length = 60 * 1024,				\
	.mon_smem_regs = {						\
		.write_ptr = {						\
			.addr = LDBG_M2S_BUF_WPTR,			\
			.mask = LDBG_M2S_BUF_WPTR_VAL_MSK,		\
	},								\
		.cycle_cnt = {						\
			.addr = LDBG_M2S_BUF_WRAP_CNT,			\
			.mask = LDBG_M2S_BUF_WRAP_CNT_VAL_MSK,		\
		},							\
	},								\
	.min_txq_size = 128,						\
	.gp2_reg_addr = 0xd02c68,					\
	.min_ba_txq_size = IWL_DEFAULT_QUEUE_SIZE_EHT,			\
	.mon_dram_regs = {						\
		.write_ptr = {						\
			.addr = DBGC_CUR_DBGBUF_STATUS,			\
			.mask = DBGC_CUR_DBGBUF_STATUS_OFFSET_MSK,	\
		},							\
		.cycle_cnt = {						\
			.addr = DBGC_DBGBUF_WRAP_AROUND,		\
			.mask = 0xffffffff,				\
		},							\
		.cur_frag = {						\
			.addr = DBGC_CUR_DBGBUF_STATUS,			\
			.mask = DBGC_CUR_DBGBUF_STATUS_IDX_MSK,		\
		},							\
	},								\
	.mon_dbgi_regs = {						\
		.write_ptr = {						\
			.addr = DBGI_SRAM_FIFO_POINTERS,		\
			.mask = DBGI_SRAM_FIFO_POINTERS_WR_PTR_MSK,	\
		},							\
	}

#define IWL_DEVICE_DR							\
	IWL_DEVICE_DR_COMMON,						\
	.uhb_supported = true,						\
	.features = IWL_TX_CSUM_NETIF_FLAGS | NETIF_F_RXCSUM,		\
	.num_rbds = IWL_NUM_RBDS_DR_EHT,				\
	.ht_params = &iwl_bz_ht_params

/*
 * This size was picked according to 8 MSDUs inside 512 A-MSDUs in an
 * A-MPDU, with additional overhead to account for processing time.
 */
#define IWL_NUM_RBDS_DR_EHT		(512 * 16)

const struct iwl_cfg_trans_params iwl_dr_trans_cfg = {
	.device_family = IWL_DEVICE_FAMILY_DR,
	.base_params = &iwl_dr_base_params,
	.mq_rx_supported = true,
	.rf_id = true,
	.gen2 = true,
	.integrated = true,
	.umac_prph_offset = 0x300000,
	.xtal_latency = 12000,
	.low_latency_xtal = true,
	.ltr_delay = IWL_CFG_TRANS_LTR_DELAY_2500US,
};

const char iwl_dr_name[] = "Intel(R) TBD Dr device";

const struct iwl_cfg iwl_cfg_dr = {
	IWL_DEVICE_DR,
};

const struct iwl_cfg_trans_params iwl_br_trans_cfg = {
	.device_family = IWL_DEVICE_FAMILY_DR,
	.base_params = &iwl_dr_base_params,
	.mq_rx_supported = true,
	.rf_id = true,
	.gen2 = true,
	.umac_prph_offset = 0x300000,
	.xtal_latency = 12000,
	.low_latency_xtal = true,
};

const char iwl_br_name[] = "Intel(R) TBD Br device";

MODULE_FIRMWARE(IWL_DR_A_PE_A_FW_MODULE_FIRMWARE(IWL_DR_UCODE_API_MAX));
MODULE_FIRMWARE(IWL_BR_A_PET_A_FW_MODULE_FIRMWARE(IWL_DR_UCODE_API_MAX));
MODULE_FIRMWARE(IWL_BR_A_PE_A_FW_MODULE_FIRMWARE(IWL_DR_UCODE_API_MAX));
