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
#define IWL_AX210_UCODE_API_MAX	89

/* Lowest firmware API version supported */
#define IWL_AX210_UCODE_API_MIN	77

/* Memory offsets and lengths */
#define IWL_AX210_SMEM_OFFSET		0x400000
#define IWL_AX210_SMEM_LEN		0xD0000

#define IWL_SO_A_JF_B_FW_PRE		"iwlwifi-so-a0-jf-b0"
#define IWL_SO_A_HR_B_FW_PRE		"iwlwifi-so-a0-hr-b0"
#define IWL_SO_A_GF_A_FW_PRE		"iwlwifi-so-a0-gf-a0"
#define IWL_TY_A_GF_A_FW_PRE		"iwlwifi-ty-a0-gf-a0"
#define IWL_SO_A_GF4_A_FW_PRE		"iwlwifi-so-a0-gf4-a0"
#define IWL_MA_A_HR_B_FW_PRE		"iwlwifi-ma-a0-hr-b0"
#define IWL_MA_A_GF_A_FW_PRE		"iwlwifi-ma-a0-gf-a0"
#define IWL_MA_A_GF4_A_FW_PRE		"iwlwifi-ma-a0-gf4-a0"
#define IWL_MA_B_HR_B_FW_PRE		"iwlwifi-ma-b0-hr-b0"
#define IWL_MA_B_GF_A_FW_PRE		"iwlwifi-ma-b0-gf-a0"
#define IWL_MA_B_GF4_A_FW_PRE		"iwlwifi-ma-b0-gf4-a0"

#define IWL_SO_A_JF_B_MODULE_FIRMWARE(api) \
	IWL_SO_A_JF_B_FW_PRE "-" __stringify(api) ".ucode"
#define IWL_SO_A_HR_B_MODULE_FIRMWARE(api) \
	IWL_SO_A_HR_B_FW_PRE "-" __stringify(api) ".ucode"
#define IWL_MA_A_HR_B_FW_MODULE_FIRMWARE(api)		\
	IWL_MA_A_HR_B_FW_PRE "-" __stringify(api) ".ucode"
#define IWL_MA_B_HR_B_FW_MODULE_FIRMWARE(api)		\
	IWL_MA_B_HR_B_FW_PRE "-" __stringify(api) ".ucode"

static const struct iwl_family_base_params iwl_ax210_base = {
	.num_of_queues = 512,
	.max_tfd_queue_size = 65536,
	.shadow_ram_support = true,
	.led_compensation = 57,
	.wd_timeout = IWL_LONG_WD_TIMEOUT,
	.max_event_log_size = 512,
	.shadow_reg_enable = true,
	.pcie_l1_allowed = true,
	.smem_offset = IWL_AX210_SMEM_OFFSET,
	.smem_len = IWL_AX210_SMEM_LEN,
	.features = IWL_TX_CSUM_NETIF_FLAGS | NETIF_F_RXCSUM,
	.apmg_not_supported = true,
	.mac_addr_from_csr = 0x380,
	.min_umac_error_event_table = 0x400000,
	.d3_debug_data_base_addr = 0x401000,
	.d3_debug_data_length = 60 * 1024,
	.mon_smem_regs = {
		.write_ptr = {
			.addr = LDBG_M2S_BUF_WPTR,
			.mask = LDBG_M2S_BUF_WPTR_VAL_MSK,
		},
		.cycle_cnt = {
			.addr = LDBG_M2S_BUF_WRAP_CNT,
			.mask = LDBG_M2S_BUF_WRAP_CNT_VAL_MSK,
		},
	},
	.min_txq_size = 128,
	.gp2_reg_addr = 0xd02c68,
	.min_ba_txq_size = IWL_DEFAULT_QUEUE_SIZE_HE,
	.mon_dram_regs = {
		.write_ptr = {
			.addr = DBGC_CUR_DBGBUF_STATUS,
			.mask = DBGC_CUR_DBGBUF_STATUS_OFFSET_MSK,
		},
		.cycle_cnt = {
			.addr = DBGC_DBGBUF_WRAP_AROUND,
			.mask = 0xffffffff,
		},
		.cur_frag = {
			.addr = DBGC_CUR_DBGBUF_STATUS,
			.mask = DBGC_CUR_DBGBUF_STATUS_IDX_MSK,
		},
	},
	.ucode_api_min = IWL_AX210_UCODE_API_MIN,
	.ucode_api_max = IWL_AX210_UCODE_API_MAX,
};

const struct iwl_mac_cfg iwl_so_mac_cfg = {
	.mq_rx_supported = true,
	.gen2 = true,
	.device_family = IWL_DEVICE_FAMILY_AX210,
	.base = &iwl_ax210_base,
	.umac_prph_offset = 0x300000,
	.integrated = true,
	/* TODO: the following values need to be checked */
	.xtal_latency = 500,
	.ltr_delay = IWL_CFG_TRANS_LTR_DELAY_200US,
};

const struct iwl_mac_cfg iwl_so_long_latency_mac_cfg = {
	.mq_rx_supported = true,
	.gen2 = true,
	.device_family = IWL_DEVICE_FAMILY_AX210,
	.base = &iwl_ax210_base,
	.umac_prph_offset = 0x300000,
	.integrated = true,
	.low_latency_xtal = true,
	.xtal_latency = 12000,
	.ltr_delay = IWL_CFG_TRANS_LTR_DELAY_2500US,
};

const struct iwl_mac_cfg iwl_so_long_latency_imr_mac_cfg = {
	.mq_rx_supported = true,
	.gen2 = true,
	.device_family = IWL_DEVICE_FAMILY_AX210,
	.base = &iwl_ax210_base,
	.umac_prph_offset = 0x300000,
	.integrated = true,
	.low_latency_xtal = true,
	.xtal_latency = 12000,
	.ltr_delay = IWL_CFG_TRANS_LTR_DELAY_2500US,
	.imr_enabled = true,
};

const struct iwl_mac_cfg iwl_ma_mac_cfg = {
	.device_family = IWL_DEVICE_FAMILY_AX210,
	.base = &iwl_ax210_base,
	.mq_rx_supported = true,
	.gen2 = true,
	.integrated = true,
	.umac_prph_offset = 0x300000
};

MODULE_FIRMWARE(IWL_SO_A_JF_B_MODULE_FIRMWARE(IWL_AX210_UCODE_API_MAX));
MODULE_FIRMWARE(IWL_SO_A_HR_B_MODULE_FIRMWARE(IWL_AX210_UCODE_API_MAX));
IWL_FW_AND_PNVM(IWL_SO_A_GF_A_FW_PRE, IWL_AX210_UCODE_API_MAX);
IWL_FW_AND_PNVM(IWL_TY_A_GF_A_FW_PRE, IWL_AX210_UCODE_API_MAX);
MODULE_FIRMWARE(IWL_MA_A_HR_B_FW_MODULE_FIRMWARE(IWL_AX210_UCODE_API_MAX));
IWL_FW_AND_PNVM(IWL_MA_A_GF_A_FW_PRE, IWL_AX210_UCODE_API_MAX);
IWL_FW_AND_PNVM(IWL_MA_A_GF4_A_FW_PRE, IWL_AX210_UCODE_API_MAX);
MODULE_FIRMWARE(IWL_MA_B_HR_B_FW_MODULE_FIRMWARE(IWL_AX210_UCODE_API_MAX));
IWL_FW_AND_PNVM(IWL_MA_B_GF_A_FW_PRE, IWL_AX210_UCODE_API_MAX);
IWL_FW_AND_PNVM(IWL_MA_B_GF4_A_FW_PRE, IWL_AX210_UCODE_API_MAX);
