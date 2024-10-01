// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2015-2017 Intel Deutschland GmbH
 * Copyright (C) 2018-2024 Intel Corporation
 */
#include <linux/module.h>
#include <linux/stringify.h>
#include "iwl-config.h"
#include "iwl-prph.h"
#include "fw/api/txq.h"

/* Highest firmware API version supported */
#define IWL_AX210_UCODE_API_MAX	89

/* Lowest firmware API version supported */
#define IWL_AX210_UCODE_API_MIN	77

/* NVM versions */
#define IWL_AX210_NVM_VERSION		0x0a1d

/* Memory offsets and lengths */
#define IWL_AX210_DCCM_OFFSET		0x800000 /* LMAC1 */
#define IWL_AX210_DCCM_LEN		0x10000 /* LMAC1 */
#define IWL_AX210_DCCM2_OFFSET		0x880000
#define IWL_AX210_DCCM2_LEN		0x8000
#define IWL_AX210_SMEM_OFFSET		0x400000
#define IWL_AX210_SMEM_LEN		0xD0000

#define IWL_SO_A_JF_B_FW_PRE		"iwlwifi-so-a0-jf-b0"
#define IWL_SO_A_HR_B_FW_PRE		"iwlwifi-so-a0-hr-b0"
#define IWL_SO_A_GF_A_FW_PRE		"iwlwifi-so-a0-gf-a0"
#define IWL_TY_A_GF_A_FW_PRE		"iwlwifi-ty-a0-gf-a0"
#define IWL_SO_A_GF4_A_FW_PRE		"iwlwifi-so-a0-gf4-a0"
#define IWL_SO_A_MR_A_FW_PRE		"iwlwifi-so-a0-mr-a0"
#define IWL_MA_A_HR_B_FW_PRE		"iwlwifi-ma-a0-hr-b0"
#define IWL_MA_A_GF_A_FW_PRE		"iwlwifi-ma-a0-gf-a0"
#define IWL_MA_A_GF4_A_FW_PRE		"iwlwifi-ma-a0-gf4-a0"
#define IWL_MA_A_MR_A_FW_PRE		"iwlwifi-ma-a0-mr-a0"
#define IWL_MA_B_HR_B_FW_PRE		"iwlwifi-ma-b0-hr-b0"
#define IWL_MA_B_GF_A_FW_PRE		"iwlwifi-ma-b0-gf-a0"
#define IWL_MA_B_GF4_A_FW_PRE		"iwlwifi-ma-b0-gf4-a0"
#define IWL_MA_B_MR_A_FW_PRE		"iwlwifi-ma-b0-mr-a0"

#define IWL_SO_A_JF_B_MODULE_FIRMWARE(api) \
	IWL_SO_A_JF_B_FW_PRE "-" __stringify(api) ".ucode"
#define IWL_SO_A_HR_B_MODULE_FIRMWARE(api) \
	IWL_SO_A_HR_B_FW_PRE "-" __stringify(api) ".ucode"
#define IWL_SO_A_GF_A_MODULE_FIRMWARE(api) \
	IWL_SO_A_GF_A_FW_PRE "-" __stringify(api) ".ucode"
#define IWL_TY_A_GF_A_MODULE_FIRMWARE(api) \
	IWL_TY_A_GF_A_FW_PRE "-" __stringify(api) ".ucode"
#define IWL_MA_A_HR_B_FW_MODULE_FIRMWARE(api)		\
	IWL_MA_A_HR_B_FW_PRE "-" __stringify(api) ".ucode"
#define IWL_MA_A_GF_A_FW_MODULE_FIRMWARE(api)		\
	IWL_MA_A_GF_A_FW_PRE "-" __stringify(api) ".ucode"
#define IWL_MA_A_GF4_A_FW_MODULE_FIRMWARE(api)		\
	IWL_MA_A_GF4_A_FW_PRE "-" __stringify(api) ".ucode"
#define IWL_MA_A_MR_A_FW_MODULE_FIRMWARE(api) \
	IWL_MA_A_MR_A_FW_PRE "-" __stringify(api) ".ucode"
#define IWL_MA_B_HR_B_FW_MODULE_FIRMWARE(api)		\
	IWL_MA_B_HR_B_FW_PRE "-" __stringify(api) ".ucode"
#define IWL_MA_B_GF_A_FW_MODULE_FIRMWARE(api)		\
	IWL_MA_B_GF_A_FW_PRE "-" __stringify(api) ".ucode"
#define IWL_MA_B_GF4_A_FW_MODULE_FIRMWARE(api)		\
	IWL_MA_B_GF4_A_FW_PRE "-" __stringify(api) ".ucode"
#define IWL_MA_B_MR_A_FW_MODULE_FIRMWARE(api) \
	IWL_MA_B_MR_A_FW_PRE "-" __stringify(api) ".ucode"

static const struct iwl_base_params iwl_ax210_base_params = {
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

#define IWL_DEVICE_AX210_COMMON						\
	.ucode_api_min = IWL_AX210_UCODE_API_MIN,			\
	.led_mode = IWL_LED_RF_STATE,					\
	.nvm_hw_section_num = 10,					\
	.non_shared_ant = ANT_B,					\
	.dccm_offset = IWL_AX210_DCCM_OFFSET,				\
	.dccm_len = IWL_AX210_DCCM_LEN,					\
	.dccm2_offset = IWL_AX210_DCCM2_OFFSET,				\
	.dccm2_len = IWL_AX210_DCCM2_LEN,				\
	.smem_offset = IWL_AX210_SMEM_OFFSET,				\
	.smem_len = IWL_AX210_SMEM_LEN,					\
	.features = IWL_TX_CSUM_NETIF_FLAGS | NETIF_F_RXCSUM,		\
	.apmg_not_supported = true,					\
	.trans.mq_rx_supported = true,					\
	.vht_mu_mimo_supported = true,					\
	.mac_addr_from_csr = 0x380,					\
	.ht_params = &iwl_22000_ht_params,				\
	.nvm_ver = IWL_AX210_NVM_VERSION,				\
	.trans.rf_id = true,						\
	.trans.gen2 = true,						\
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

#define IWL_DEVICE_AX210						\
	IWL_DEVICE_AX210_COMMON,					\
	.ucode_api_max = IWL_AX210_UCODE_API_MAX,			\
	.trans.umac_prph_offset = 0x300000,				\
	.trans.device_family = IWL_DEVICE_FAMILY_AX210,			\
	.trans.base_params = &iwl_ax210_base_params,			\
	.min_txq_size = 128,						\
	.gp2_reg_addr = 0xd02c68,					\
	.min_ba_txq_size = IWL_DEFAULT_QUEUE_SIZE_HE,		\
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
	}

const struct iwl_cfg_trans_params iwl_so_trans_cfg = {
	.mq_rx_supported = true,
	.rf_id = true,
	.gen2 = true,
	.device_family = IWL_DEVICE_FAMILY_AX210,
	.base_params = &iwl_ax210_base_params,
	.umac_prph_offset = 0x300000,
	.integrated = true,
	/* TODO: the following values need to be checked */
	.xtal_latency = 500,
	.ltr_delay = IWL_CFG_TRANS_LTR_DELAY_200US,
};

const struct iwl_cfg_trans_params iwl_so_long_latency_trans_cfg = {
	.mq_rx_supported = true,
	.rf_id = true,
	.gen2 = true,
	.device_family = IWL_DEVICE_FAMILY_AX210,
	.base_params = &iwl_ax210_base_params,
	.umac_prph_offset = 0x300000,
	.integrated = true,
	.low_latency_xtal = true,
	.xtal_latency = 12000,
	.ltr_delay = IWL_CFG_TRANS_LTR_DELAY_2500US,
};

const struct iwl_cfg_trans_params iwl_so_long_latency_imr_trans_cfg = {
	.mq_rx_supported = true,
	.rf_id = true,
	.gen2 = true,
	.device_family = IWL_DEVICE_FAMILY_AX210,
	.base_params = &iwl_ax210_base_params,
	.umac_prph_offset = 0x300000,
	.integrated = true,
	.low_latency_xtal = true,
	.xtal_latency = 12000,
	.ltr_delay = IWL_CFG_TRANS_LTR_DELAY_2500US,
	.imr_enabled = true,
};

/*
 * If the device doesn't support HE, no need to have that many buffers.
 * AX210 devices can split multiple frames into a single RB, so fewer are
 * needed; AX210 cannot (but use smaller RBs by default) - these sizes
 * were picked according to 8 MSDUs inside 256 A-MSDUs in an A-MPDU, with
 * additional overhead to account for processing time.
 */
#define IWL_NUM_RBDS_NON_HE		512
#define IWL_NUM_RBDS_AX210_HE		4096

const struct iwl_cfg_trans_params iwl_ma_trans_cfg = {
	.device_family = IWL_DEVICE_FAMILY_AX210,
	.base_params = &iwl_ax210_base_params,
	.mq_rx_supported = true,
	.rf_id = true,
	.gen2 = true,
	.integrated = true,
	.umac_prph_offset = 0x300000
};

const char iwl_ax211_name[] = "Intel(R) Wi-Fi 6E AX211 160MHz";
const char iwl_ax221_name[] = "Intel(R) Wi-Fi 6E AX221 160MHz";
const char iwl_ax231_name[] = "Intel(R) Wi-Fi 6E AX231 160MHz";
const char iwl_ax411_name[] = "Intel(R) Wi-Fi 6E AX411 160MHz";

const char iwl_ax210_killer_1675w_name[] =
	"Killer(R) Wi-Fi 6E AX1675w 160MHz Wireless Network Adapter (210D2W)";
const char iwl_ax210_killer_1675x_name[] =
	"Killer(R) Wi-Fi 6E AX1675x 160MHz Wireless Network Adapter (210NGW)";
const char iwl_ax211_killer_1675s_name[] =
	"Killer(R) Wi-Fi 6E AX1675s 160MHz Wireless Network Adapter (211NGW)";
const char iwl_ax211_killer_1675i_name[] =
	"Killer(R) Wi-Fi 6E AX1675i 160MHz Wireless Network Adapter (211NGW)";
const char iwl_ax411_killer_1690s_name[] =
	"Killer(R) Wi-Fi 6E AX1690s 160MHz Wireless Network Adapter (411D2W)";
const char iwl_ax411_killer_1690i_name[] =
	"Killer(R) Wi-Fi 6E AX1690i 160MHz Wireless Network Adapter (411NGW)";

const struct iwl_cfg iwlax210_2ax_cfg_so_jf_b0 = {
	.name = "Intel(R) Wireless-AC 9560 160MHz",
	.fw_name_pre = IWL_SO_A_JF_B_FW_PRE,
	IWL_DEVICE_AX210,
	.num_rbds = IWL_NUM_RBDS_NON_HE,
};

const struct iwl_cfg iwlax211_2ax_cfg_so_gf_a0 = {
	.name = iwl_ax211_name,
	.fw_name_pre = IWL_SO_A_GF_A_FW_PRE,
	.uhb_supported = true,
	IWL_DEVICE_AX210,
	.num_rbds = IWL_NUM_RBDS_AX210_HE,
};

const struct iwl_cfg iwlax211_2ax_cfg_so_gf_a0_long = {
	.name = iwl_ax211_name,
	.fw_name_pre = IWL_SO_A_GF_A_FW_PRE,
	.uhb_supported = true,
	IWL_DEVICE_AX210,
	.num_rbds = IWL_NUM_RBDS_AX210_HE,
	.trans.xtal_latency = 12000,
	.trans.low_latency_xtal = true,
};

const struct iwl_cfg iwlax210_2ax_cfg_ty_gf_a0 = {
	.name = "Intel(R) Wi-Fi 6 AX210 160MHz",
	.fw_name_pre = IWL_TY_A_GF_A_FW_PRE,
	.uhb_supported = true,
	IWL_DEVICE_AX210,
	.num_rbds = IWL_NUM_RBDS_AX210_HE,
};

const struct iwl_cfg iwlax411_2ax_cfg_so_gf4_a0 = {
	.name = iwl_ax411_name,
	.fw_name_pre = IWL_SO_A_GF4_A_FW_PRE,
	.uhb_supported = true,
	IWL_DEVICE_AX210,
	.num_rbds = IWL_NUM_RBDS_AX210_HE,
};

const struct iwl_cfg iwlax411_2ax_cfg_so_gf4_a0_long = {
	.name = iwl_ax411_name,
	.fw_name_pre = IWL_SO_A_GF4_A_FW_PRE,
	.uhb_supported = true,
	IWL_DEVICE_AX210,
	.num_rbds = IWL_NUM_RBDS_AX210_HE,
	.trans.xtal_latency = 12000,
	.trans.low_latency_xtal = true,
};

const struct iwl_cfg iwl_cfg_so_a0_ms_a0 = {
	.fw_name_pre = IWL_SO_A_MR_A_FW_PRE,
	.uhb_supported = false,
	IWL_DEVICE_AX210,
	.num_rbds = IWL_NUM_RBDS_AX210_HE,
};

const struct iwl_cfg iwl_cfg_ma = {
	.fw_name_mac = "ma",
	.uhb_supported = true,
	IWL_DEVICE_AX210,
	.num_rbds = IWL_NUM_RBDS_AX210_HE,
};

const struct iwl_cfg iwl_cfg_so_a0_hr_a0 = {
	.fw_name_pre = IWL_SO_A_HR_B_FW_PRE,
	IWL_DEVICE_AX210,
	.num_rbds = IWL_NUM_RBDS_AX210_HE,
};

MODULE_FIRMWARE(IWL_SO_A_JF_B_MODULE_FIRMWARE(IWL_AX210_UCODE_API_MAX));
MODULE_FIRMWARE(IWL_SO_A_HR_B_MODULE_FIRMWARE(IWL_AX210_UCODE_API_MAX));
MODULE_FIRMWARE(IWL_SO_A_GF_A_MODULE_FIRMWARE(IWL_AX210_UCODE_API_MAX));
MODULE_FIRMWARE(IWL_TY_A_GF_A_MODULE_FIRMWARE(IWL_AX210_UCODE_API_MAX));
MODULE_FIRMWARE(IWL_MA_A_HR_B_FW_MODULE_FIRMWARE(IWL_AX210_UCODE_API_MAX));
MODULE_FIRMWARE(IWL_MA_A_GF_A_FW_MODULE_FIRMWARE(IWL_AX210_UCODE_API_MAX));
MODULE_FIRMWARE(IWL_MA_A_GF4_A_FW_MODULE_FIRMWARE(IWL_AX210_UCODE_API_MAX));
MODULE_FIRMWARE(IWL_MA_A_MR_A_FW_MODULE_FIRMWARE(IWL_AX210_UCODE_API_MAX));
MODULE_FIRMWARE(IWL_MA_B_HR_B_FW_MODULE_FIRMWARE(IWL_AX210_UCODE_API_MAX));
MODULE_FIRMWARE(IWL_MA_B_GF_A_FW_MODULE_FIRMWARE(IWL_AX210_UCODE_API_MAX));
MODULE_FIRMWARE(IWL_MA_B_GF4_A_FW_MODULE_FIRMWARE(IWL_AX210_UCODE_API_MAX));
MODULE_FIRMWARE(IWL_MA_B_MR_A_FW_MODULE_FIRMWARE(IWL_AX210_UCODE_API_MAX));

MODULE_FIRMWARE("iwlwifi-so-a0-gf-a0.pnvm");
MODULE_FIRMWARE("iwlwifi-so-a0-gf4-a0.pnvm");
MODULE_FIRMWARE("iwlwifi-ty-a0-gf-a0.pnvm");
MODULE_FIRMWARE("iwlwifi-ma-b0-gf-a0.pnvm");
MODULE_FIRMWARE("iwlwifi-ma-b0-gf4-a0.pnvm");
