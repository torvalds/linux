// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2015-2017 Intel Deutschland GmbH
 * Copyright (C) 2018-2025 Intel Corporation
 */
#include <linux/module.h>
#include <linux/stringify.h>
#include "iwl-config.h"
#include "iwl-prph.h"
#include "fw/api/txq.h"

/* Highest firmware API version supported */
#define IWL_SC_UCODE_API_MAX	98

/* Lowest firmware API version supported */
#define IWL_SC_UCODE_API_MIN	93

/* NVM versions */
#define IWL_SC_NVM_VERSION		0x0a1d

/* Memory offsets and lengths */
#define IWL_SC_DCCM_OFFSET		0x800000 /* LMAC1 */
#define IWL_SC_DCCM_LEN			0x10000 /* LMAC1 */
#define IWL_SC_DCCM2_OFFSET		0x880000
#define IWL_SC_DCCM2_LEN		0x8000
#define IWL_SC_SMEM_OFFSET		0x400000
#define IWL_SC_SMEM_LEN			0xD0000

#define IWL_SC_A_FM_B_FW_PRE		"iwlwifi-sc-a0-fm-b0"
#define IWL_SC_A_FM_C_FW_PRE		"iwlwifi-sc-a0-fm-c0"
#define IWL_SC_A_HR_A_FW_PRE		"iwlwifi-sc-a0-hr-b0"
#define IWL_SC_A_HR_B_FW_PRE		"iwlwifi-sc-a0-hr-b0"
#define IWL_SC_A_GF_A_FW_PRE		"iwlwifi-sc-a0-gf-a0"
#define IWL_SC_A_GF4_A_FW_PRE		"iwlwifi-sc-a0-gf4-a0"
#define IWL_SC_A_WH_A_FW_PRE		"iwlwifi-sc-a0-wh-a0"
#define IWL_SC2_A_FM_C_FW_PRE		"iwlwifi-sc2-a0-fm-c0"
#define IWL_SC2_A_WH_A_FW_PRE		"iwlwifi-sc2-a0-wh-a0"
#define IWL_SC2F_A_FM_C_FW_PRE		"iwlwifi-sc2f-a0-fm-c0"
#define IWL_SC2F_A_WH_A_FW_PRE		"iwlwifi-sc2f-a0-wh-a0"

#define IWL_SC_A_HR_A_FW_MODULE_FIRMWARE(api) \
	IWL_SC_A_HR_A_FW_PRE "-" __stringify(api) ".ucode"
#define IWL_SC_A_HR_B_FW_MODULE_FIRMWARE(api) \
	IWL_SC_A_HR_B_FW_PRE "-" __stringify(api) ".ucode"

static const struct iwl_base_params iwl_sc_base_params = {
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

#define IWL_DEVICE_BZ_COMMON						\
	.ucode_api_max = IWL_SC_UCODE_API_MAX,			\
	.ucode_api_min = IWL_SC_UCODE_API_MIN,			\
	.led_mode = IWL_LED_RF_STATE,					\
	.nvm_hw_section_num = 10,					\
	.non_shared_ant = ANT_B,					\
	.dccm_offset = IWL_SC_DCCM_OFFSET,				\
	.dccm_len = IWL_SC_DCCM_LEN,					\
	.dccm2_offset = IWL_SC_DCCM2_OFFSET,				\
	.dccm2_len = IWL_SC_DCCM2_LEN,				\
	.smem_offset = IWL_SC_SMEM_OFFSET,				\
	.smem_len = IWL_SC_SMEM_LEN,					\
	.apmg_not_supported = true,					\
	.trans.mq_rx_supported = true,					\
	.vht_mu_mimo_supported = true,					\
	.mac_addr_from_csr = 0x30,					\
	.nvm_ver = IWL_SC_NVM_VERSION,				\
	.trans.rf_id = true,						\
	.trans.gen2 = true,						\
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
	.trans.umac_prph_offset = 0x300000,				\
	.trans.device_family = IWL_DEVICE_FAMILY_SC,			\
	.trans.base_params = &iwl_sc_base_params,			\
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

#define IWL_DEVICE_SC							\
	IWL_DEVICE_BZ_COMMON,						\
	.uhb_supported = true,						\
	.features = IWL_TX_CSUM_NETIF_FLAGS | NETIF_F_RXCSUM,		\
	.num_rbds = IWL_NUM_RBDS_SC_EHT,				\
	.ht_params = &iwl_bz_ht_params

/*
 * This size was picked according to 8 MSDUs inside 512 A-MSDUs in an
 * A-MPDU, with additional overhead to account for processing time.
 */
#define IWL_NUM_RBDS_SC_EHT		(512 * 16)

const struct iwl_cfg_trans_params iwl_sc_trans_cfg = {
	.device_family = IWL_DEVICE_FAMILY_SC,
	.base_params = &iwl_sc_base_params,
	.mq_rx_supported = true,
	.rf_id = true,
	.gen2 = true,
	.integrated = true,
	.umac_prph_offset = 0x300000,
	.xtal_latency = 12000,
	.low_latency_xtal = true,
	.ltr_delay = IWL_CFG_TRANS_LTR_DELAY_2500US,
};

const char iwl_sp_name[] = "Intel(R) Wi-Fi 7 BE213 160MHz";

const struct iwl_cfg iwl_cfg_sc = {
	.fw_name_mac = "sc",
	IWL_DEVICE_SC,
};

const struct iwl_cfg iwl_cfg_sc2 = {
	.fw_name_mac = "sc2",
	IWL_DEVICE_SC,
};

const struct iwl_cfg iwl_cfg_sc2f = {
	.fw_name_mac = "sc2f",
	IWL_DEVICE_SC,
};

IWL_FW_AND_PNVM(IWL_SC_A_FM_B_FW_PRE, IWL_SC_UCODE_API_MAX);
IWL_FW_AND_PNVM(IWL_SC_A_FM_C_FW_PRE, IWL_SC_UCODE_API_MAX);
MODULE_FIRMWARE(IWL_SC_A_HR_A_FW_MODULE_FIRMWARE(IWL_SC_UCODE_API_MAX));
MODULE_FIRMWARE(IWL_SC_A_HR_B_FW_MODULE_FIRMWARE(IWL_SC_UCODE_API_MAX));
IWL_FW_AND_PNVM(IWL_SC_A_GF_A_FW_PRE, IWL_SC_UCODE_API_MAX);
IWL_FW_AND_PNVM(IWL_SC_A_GF4_A_FW_PRE, IWL_SC_UCODE_API_MAX);
IWL_FW_AND_PNVM(IWL_SC_A_WH_A_FW_PRE, IWL_SC_UCODE_API_MAX);
IWL_FW_AND_PNVM(IWL_SC2_A_FM_C_FW_PRE, IWL_SC_UCODE_API_MAX);
IWL_FW_AND_PNVM(IWL_SC2_A_WH_A_FW_PRE, IWL_SC_UCODE_API_MAX);
IWL_FW_AND_PNVM(IWL_SC2F_A_FM_C_FW_PRE, IWL_SC_UCODE_API_MAX);
IWL_FW_AND_PNVM(IWL_SC2F_A_WH_A_FW_PRE, IWL_SC_UCODE_API_MAX);
