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
#define IWL_22000_UCODE_API_MAX	77

/* Lowest firmware API version supported */
#define IWL_22000_UCODE_API_MIN	77

/* NVM versions */
#define IWL_22000_NVM_VERSION		0x0a1d

/* Memory offsets and lengths */
#define IWL_22000_DCCM_OFFSET		0x800000 /* LMAC1 */
#define IWL_22000_DCCM_LEN		0x10000 /* LMAC1 */
#define IWL_22000_DCCM2_OFFSET		0x880000
#define IWL_22000_DCCM2_LEN		0x8000
#define IWL_22000_SMEM_OFFSET		0x400000
#define IWL_22000_SMEM_LEN		0xD0000

#define IWL_QU_B_HR_B_FW_PRE		"iwlwifi-Qu-b0-hr-b0"
#define IWL_QU_C_HR_B_FW_PRE		"iwlwifi-Qu-c0-hr-b0"
#define IWL_QU_B_JF_B_FW_PRE		"iwlwifi-Qu-b0-jf-b0"
#define IWL_QU_C_JF_B_FW_PRE		"iwlwifi-Qu-c0-jf-b0"
#define IWL_QUZ_A_HR_B_FW_PRE		"iwlwifi-QuZ-a0-hr-b0"
#define IWL_QUZ_A_JF_B_FW_PRE		"iwlwifi-QuZ-a0-jf-b0"
#define IWL_CC_A_FW_PRE			"iwlwifi-cc-a0"

#define IWL_QU_B_HR_B_MODULE_FIRMWARE(api) \
	IWL_QU_B_HR_B_FW_PRE "-" __stringify(api) ".ucode"
#define IWL_QUZ_A_HR_B_MODULE_FIRMWARE(api) \
	IWL_QUZ_A_HR_B_FW_PRE "-" __stringify(api) ".ucode"
#define IWL_QUZ_A_JF_B_MODULE_FIRMWARE(api) \
	IWL_QUZ_A_JF_B_FW_PRE "-" __stringify(api) ".ucode"
#define IWL_QU_C_HR_B_MODULE_FIRMWARE(api) \
	IWL_QU_C_HR_B_FW_PRE "-" __stringify(api) ".ucode"
#define IWL_QU_B_JF_B_MODULE_FIRMWARE(api) \
	IWL_QU_B_JF_B_FW_PRE "-" __stringify(api) ".ucode"
#define IWL_QU_C_JF_B_MODULE_FIRMWARE(api) \
	IWL_QU_C_JF_B_FW_PRE "-" __stringify(api) ".ucode"
#define IWL_CC_A_MODULE_FIRMWARE(api)			\
	IWL_CC_A_FW_PRE "-" __stringify(api) ".ucode"

static const struct iwl_base_params iwl_22000_base_params = {
	.eeprom_size = OTP_LOW_IMAGE_SIZE_32K,
	.num_of_queues = 512,
	.max_tfd_queue_size = 256,
	.shadow_ram_support = true,
	.led_compensation = 57,
	.wd_timeout = IWL_LONG_WD_TIMEOUT,
	.max_event_log_size = 512,
	.shadow_reg_enable = true,
	.pcie_l1_allowed = true,
};

const struct iwl_ht_params iwl_22000_ht_params = {
	.stbc = true,
	.ldpc = true,
	.ht40_bands = BIT(NL80211_BAND_2GHZ) | BIT(NL80211_BAND_5GHZ) |
		      BIT(NL80211_BAND_6GHZ),
};

#define IWL_DEVICE_22000_COMMON						\
	.ucode_api_min = IWL_22000_UCODE_API_MIN,			\
	.led_mode = IWL_LED_RF_STATE,					\
	.nvm_hw_section_num = 10,					\
	.non_shared_ant = ANT_B,					\
	.dccm_offset = IWL_22000_DCCM_OFFSET,				\
	.dccm_len = IWL_22000_DCCM_LEN,					\
	.dccm2_offset = IWL_22000_DCCM2_OFFSET,				\
	.dccm2_len = IWL_22000_DCCM2_LEN,				\
	.smem_offset = IWL_22000_SMEM_OFFSET,				\
	.smem_len = IWL_22000_SMEM_LEN,					\
	.features = IWL_TX_CSUM_NETIF_FLAGS | NETIF_F_RXCSUM,		\
	.apmg_not_supported = true,					\
	.vht_mu_mimo_supported = true,					\
	.mac_addr_from_csr = 0x380,					\
	.ht_params = &iwl_22000_ht_params,				\
	.nvm_ver = IWL_22000_NVM_VERSION,				\
	.nvm_type = IWL_NVM_EXT,					\
	.dbgc_supported = true,						\
	.min_umac_error_event_table = 0x400000,				\
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
	}

#define IWL_DEVICE_22500						\
	IWL_DEVICE_22000_COMMON,					\
	.ucode_api_max = IWL_22000_UCODE_API_MAX,			\
	.gp2_reg_addr = 0xa02c68,					\
	.mon_dram_regs = {						\
		.write_ptr = {						\
			.addr = MON_BUFF_WRPTR_VER2,			\
			.mask = 0xffffffff,				\
		},							\
		.cycle_cnt = {						\
			.addr = MON_BUFF_CYCLE_CNT_VER2,		\
			.mask = 0xffffffff,				\
		},							\
	}

const struct iwl_cfg_trans_params iwl_qu_trans_cfg = {
	.mq_rx_supported = true,
	.rf_id = true,
	.gen2 = true,
	.device_family = IWL_DEVICE_FAMILY_22000,
	.base_params = &iwl_22000_base_params,
	.integrated = true,
	.xtal_latency = 500,
	.ltr_delay = IWL_CFG_TRANS_LTR_DELAY_200US,
};

const struct iwl_cfg_trans_params iwl_qu_medium_latency_trans_cfg = {
	.mq_rx_supported = true,
	.rf_id = true,
	.gen2 = true,
	.device_family = IWL_DEVICE_FAMILY_22000,
	.base_params = &iwl_22000_base_params,
	.integrated = true,
	.xtal_latency = 1820,
	.ltr_delay = IWL_CFG_TRANS_LTR_DELAY_1820US,
};

const struct iwl_cfg_trans_params iwl_qu_long_latency_trans_cfg = {
	.mq_rx_supported = true,
	.rf_id = true,
	.gen2 = true,
	.device_family = IWL_DEVICE_FAMILY_22000,
	.base_params = &iwl_22000_base_params,
	.integrated = true,
	.xtal_latency = 12000,
	.low_latency_xtal = true,
	.ltr_delay = IWL_CFG_TRANS_LTR_DELAY_2500US,
};

/*
 * If the device doesn't support HE, no need to have that many buffers.
 * 22000 devices can split multiple frames into a single RB, so fewer are
 * needed; AX210 cannot (but use smaller RBs by default) - these sizes
 * were picked according to 8 MSDUs inside 256 A-MSDUs in an A-MPDU, with
 * additional overhead to account for processing time.
 */
#define IWL_NUM_RBDS_NON_HE		512
#define IWL_NUM_RBDS_22000_HE		2048

/*
 * All JF radio modules are part of the 9000 series, but the MAC part
 * looks more like 22000.  That's why this device is here, but called
 * 9560 nevertheless.
 */
const struct iwl_cfg iwl9560_qu_jf_cfg = {
	IWL_DEVICE_22500,
	.num_rbds = IWL_NUM_RBDS_NON_HE,
};

const struct iwl_cfg iwl9560_qu_jf_cfg_80mhz = {
	IWL_DEVICE_22500,
	.num_rbds = IWL_NUM_RBDS_NON_HE,
	.bw_limit = 80,
};

const struct iwl_cfg iwl9560_quz_a0_jf_b0_cfg = {
	.fw_name_pre = IWL_QUZ_A_JF_B_FW_PRE,
	IWL_DEVICE_22500,
	.num_rbds = IWL_NUM_RBDS_NON_HE,
};

const struct iwl_cfg iwl9560_quz_a0_jf_b0_cfg_80mhz = {
	.fw_name_pre = IWL_QUZ_A_JF_B_FW_PRE,
	IWL_DEVICE_22500,
	.num_rbds = IWL_NUM_RBDS_NON_HE,
	.bw_limit = 80,
};

const struct iwl_cfg_trans_params iwl_ax200_trans_cfg = {
	.device_family = IWL_DEVICE_FAMILY_22000,
	.base_params = &iwl_22000_base_params,
	.mq_rx_supported = true,
	.rf_id = true,
	.gen2 = true,
	.bisr_workaround = 1,
};

const char iwl_ax101_name[] = "Intel(R) Wi-Fi 6 AX101";
const char iwl_ax200_name[] = "Intel(R) Wi-Fi 6 AX200 160MHz";
const char iwl_ax201_name[] = "Intel(R) Wi-Fi 6 AX201 160MHz";
const char iwl_ax203_name[] = "Intel(R) Wi-Fi 6 AX203";

const char iwl_ax200_killer_1650w_name[] =
	"Killer(R) Wi-Fi 6 AX1650w 160MHz Wireless Network Adapter (200D2W)";
const char iwl_ax200_killer_1650x_name[] =
	"Killer(R) Wi-Fi 6 AX1650x 160MHz Wireless Network Adapter (200NGW)";
const char iwl_ax201_killer_1650s_name[] =
	"Killer(R) Wi-Fi 6 AX1650s 160MHz Wireless Network Adapter (201D2W)";
const char iwl_ax201_killer_1650i_name[] =
	"Killer(R) Wi-Fi 6 AX1650i 160MHz Wireless Network Adapter (201NGW)";

const struct iwl_cfg iwl_qu_hr1 = {
	IWL_DEVICE_22500,
	.tx_with_siso_diversity = true,
	.num_rbds = IWL_NUM_RBDS_22000_HE,
};

const struct iwl_cfg iwl_qu_hr = {
	IWL_DEVICE_22500,
	.num_rbds = IWL_NUM_RBDS_22000_HE,
};

const struct iwl_cfg iwl_qu_hr_80mhz = {
	IWL_DEVICE_22500,
	.num_rbds = IWL_NUM_RBDS_22000_HE,
	.bw_limit = 80,
};

const struct iwl_cfg iwl_ax200_cfg_cc = {
	.fw_name_pre = IWL_CC_A_FW_PRE,
	IWL_DEVICE_22500,
	.num_rbds = IWL_NUM_RBDS_22000_HE,
};

MODULE_FIRMWARE(IWL_QU_B_HR_B_MODULE_FIRMWARE(IWL_22000_UCODE_API_MAX));
MODULE_FIRMWARE(IWL_QU_C_HR_B_MODULE_FIRMWARE(IWL_22000_UCODE_API_MAX));
MODULE_FIRMWARE(IWL_QU_B_JF_B_MODULE_FIRMWARE(IWL_22000_UCODE_API_MAX));
MODULE_FIRMWARE(IWL_QU_C_JF_B_MODULE_FIRMWARE(IWL_22000_UCODE_API_MAX));
MODULE_FIRMWARE(IWL_QUZ_A_HR_B_MODULE_FIRMWARE(IWL_22000_UCODE_API_MAX));
MODULE_FIRMWARE(IWL_QUZ_A_JF_B_MODULE_FIRMWARE(IWL_22000_UCODE_API_MAX));
MODULE_FIRMWARE(IWL_CC_A_MODULE_FIRMWARE(IWL_22000_UCODE_API_MAX));
