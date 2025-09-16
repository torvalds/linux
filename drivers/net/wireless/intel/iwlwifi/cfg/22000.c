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

/* Memory offsets and lengths */
#define IWL_22000_SMEM_OFFSET		0x400000
#define IWL_22000_SMEM_LEN		0xD0000

#define IWL_CC_A_FW_PRE			"iwlwifi-cc-a0"

#define IWL_CC_A_MODULE_FIRMWARE(api)			\
	IWL_CC_A_FW_PRE "-" __stringify(api) ".ucode"

static const struct iwl_family_base_params iwl_22000_base = {
	.num_of_queues = 512,
	.max_tfd_queue_size = 256,
	.shadow_ram_support = true,
	.led_compensation = 57,
	.wd_timeout = IWL_LONG_WD_TIMEOUT,
	.max_event_log_size = 512,
	.shadow_reg_enable = true,
	.pcie_l1_allowed = true,
	.smem_offset = IWL_22000_SMEM_OFFSET,
	.smem_len = IWL_22000_SMEM_LEN,
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
	.gp2_reg_addr = 0xa02c68,
	.mon_dram_regs = {
		.write_ptr = {
			.addr = MON_BUFF_WRPTR_VER2,
			.mask = 0xffffffff,
		},
		.cycle_cnt = {
			.addr = MON_BUFF_CYCLE_CNT_VER2,
			.mask = 0xffffffff,
		},
	},
	.ucode_api_min = IWL_22000_UCODE_API_MIN,
	.ucode_api_max = IWL_22000_UCODE_API_MAX,
};

const struct iwl_mac_cfg iwl_qu_mac_cfg = {
	.mq_rx_supported = true,
	.gen2 = true,
	.device_family = IWL_DEVICE_FAMILY_22000,
	.base = &iwl_22000_base,
	.integrated = true,
	.xtal_latency = 500,
	.ltr_delay = IWL_CFG_TRANS_LTR_DELAY_200US,
};

const struct iwl_mac_cfg iwl_qu_medium_latency_mac_cfg = {
	.mq_rx_supported = true,
	.gen2 = true,
	.device_family = IWL_DEVICE_FAMILY_22000,
	.base = &iwl_22000_base,
	.integrated = true,
	.xtal_latency = 1820,
	.ltr_delay = IWL_CFG_TRANS_LTR_DELAY_1820US,
};

const struct iwl_mac_cfg iwl_qu_long_latency_mac_cfg = {
	.mq_rx_supported = true,
	.gen2 = true,
	.device_family = IWL_DEVICE_FAMILY_22000,
	.base = &iwl_22000_base,
	.integrated = true,
	.xtal_latency = 12000,
	.low_latency_xtal = true,
	.ltr_delay = IWL_CFG_TRANS_LTR_DELAY_2500US,
};

const struct iwl_mac_cfg iwl_ax200_mac_cfg = {
	.device_family = IWL_DEVICE_FAMILY_22000,
	.base = &iwl_22000_base,
	.mq_rx_supported = true,
	.gen2 = true,
	.bisr_workaround = 1,
};

const char iwl_ax200_killer_1650w_name[] =
	"Killer(R) Wi-Fi 6 AX1650w 160MHz Wireless Network Adapter (200D2W)";
const char iwl_ax200_killer_1650x_name[] =
	"Killer(R) Wi-Fi 6 AX1650x 160MHz Wireless Network Adapter (200NGW)";
const char iwl_ax201_killer_1650s_name[] =
	"Killer(R) Wi-Fi 6 AX1650s 160MHz Wireless Network Adapter (201D2W)";
const char iwl_ax201_killer_1650i_name[] =
	"Killer(R) Wi-Fi 6 AX1650i 160MHz Wireless Network Adapter (201NGW)";

MODULE_FIRMWARE(IWL_CC_A_MODULE_FIRMWARE(IWL_22000_UCODE_API_MAX));
