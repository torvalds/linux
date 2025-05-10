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
#define IWL_BZ_UCODE_API_MAX	99

/* Lowest firmware API version supported */
#define IWL_BZ_UCODE_API_MIN	93

/* Memory offsets and lengths */
#define IWL_BZ_SMEM_OFFSET		0x400000
#define IWL_BZ_SMEM_LEN			0xD0000

#define IWL_BZ_A_HR_B_FW_PRE		"iwlwifi-bz-a0-hr-b0"
#define IWL_BZ_A_GF_A_FW_PRE		"iwlwifi-bz-a0-gf-a0"
#define IWL_BZ_A_GF4_A_FW_PRE		"iwlwifi-bz-a0-gf4-a0"
#define IWL_BZ_A_FM_B_FW_PRE		"iwlwifi-bz-a0-fm-b0"
#define IWL_BZ_A_FM_C_FW_PRE		"iwlwifi-bz-a0-fm-c0"
#define IWL_BZ_A_FM4_B_FW_PRE		"iwlwifi-bz-a0-fm4-b0"
#define IWL_GL_B_FM_B_FW_PRE		"iwlwifi-gl-b0-fm-b0"
#define IWL_GL_C_FM_C_FW_PRE		"iwlwifi-gl-c0-fm-c0"

#define IWL_BZ_A_HR_B_MODULE_FIRMWARE(api) \
	IWL_BZ_A_HR_B_FW_PRE "-" __stringify(api) ".ucode"

#if !IS_ENABLED(CONFIG_IWLMVM)
const char iwl_ax211_name[] = "Intel(R) Wi-Fi 6E AX211 160MHz";
#endif

static const struct iwl_family_base_params iwl_bz_base = {
	.num_of_queues = 512,
	.max_tfd_queue_size = 65536,
	.shadow_ram_support = true,
	.led_compensation = 57,
	.wd_timeout = IWL_LONG_WD_TIMEOUT,
	.max_event_log_size = 512,
	.shadow_reg_enable = true,
	.pcie_l1_allowed = true,
	.smem_offset = IWL_BZ_SMEM_OFFSET,
	.smem_len = IWL_BZ_SMEM_LEN,
	.apmg_not_supported = true,
	.mac_addr_from_csr = 0x30,
	.min_umac_error_event_table = 0xD0000,
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
	.min_ba_txq_size = IWL_DEFAULT_QUEUE_SIZE_EHT,
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
	.mon_dbgi_regs = {
		.write_ptr = {
			.addr = DBGI_SRAM_FIFO_POINTERS,
			.mask = DBGI_SRAM_FIFO_POINTERS_WR_PTR_MSK,
		},
	},
	.features = IWL_TX_CSUM_NETIF_FLAGS | NETIF_F_RXCSUM,
	.ucode_api_max = IWL_BZ_UCODE_API_MAX,
	.ucode_api_min = IWL_BZ_UCODE_API_MIN,
};

const struct iwl_mac_cfg iwl_bz_mac_cfg = {
	.device_family = IWL_DEVICE_FAMILY_BZ,
	.base = &iwl_bz_base,
	.mq_rx_supported = true,
	.gen2 = true,
	.integrated = true,
	.umac_prph_offset = 0x300000,
	.xtal_latency = 12000,
	.low_latency_xtal = true,
	.ltr_delay = IWL_CFG_TRANS_LTR_DELAY_2500US,
};

const struct iwl_mac_cfg iwl_gl_mac_cfg = {
	.device_family = IWL_DEVICE_FAMILY_BZ,
	.base = &iwl_bz_base,
	.mq_rx_supported = true,
	.gen2 = true,
	.umac_prph_offset = 0x300000,
	.xtal_latency = 12000,
	.low_latency_xtal = true,
};

const char iwl_fm_name[] = "Intel(R) Wi-Fi 7 BE201 320MHz";
const char iwl_wh_name[] = "Intel(R) Wi-Fi 7 BE211 320MHz";
const char iwl_gl_name[] = "Intel(R) Wi-Fi 7 BE200 320MHz";
const char iwl_mtp_name[] = "Intel(R) Wi-Fi 7 BE202 160MHz";

MODULE_FIRMWARE(IWL_BZ_A_HR_B_MODULE_FIRMWARE(IWL_BZ_UCODE_API_MAX));
IWL_FW_AND_PNVM(IWL_BZ_A_GF_A_FW_PRE, IWL_BZ_UCODE_API_MAX);
IWL_FW_AND_PNVM(IWL_BZ_A_GF4_A_FW_PRE, IWL_BZ_UCODE_API_MAX);
IWL_FW_AND_PNVM(IWL_BZ_A_FM_B_FW_PRE, IWL_BZ_UCODE_API_MAX);
IWL_FW_AND_PNVM(IWL_BZ_A_FM_C_FW_PRE, IWL_BZ_UCODE_API_MAX);
IWL_FW_AND_PNVM(IWL_BZ_A_FM4_B_FW_PRE, IWL_BZ_UCODE_API_MAX);
IWL_FW_AND_PNVM(IWL_GL_B_FM_B_FW_PRE, IWL_BZ_UCODE_API_MAX);
IWL_FW_AND_PNVM(IWL_GL_C_FM_C_FW_PRE, IWL_BZ_UCODE_API_MAX);
