// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2018-2019 The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/remoteproc.h>
#include <linux/firmware.h>
#include <linux/of.h>

#include "core.h"
#include "dp_tx.h"
#include "dp_rx.h"
#include "debug.h"
#include "hif.h"
#include "wow.h"

unsigned int ath11k_debug_mask;
EXPORT_SYMBOL(ath11k_debug_mask);
module_param_named(debug_mask, ath11k_debug_mask, uint, 0644);
MODULE_PARM_DESC(debug_mask, "Debugging mask");

static unsigned int ath11k_crypto_mode;
module_param_named(crypto_mode, ath11k_crypto_mode, uint, 0644);
MODULE_PARM_DESC(crypto_mode, "crypto mode: 0-hardware, 1-software");

/* frame mode values are mapped as per enum ath11k_hw_txrx_mode */
unsigned int ath11k_frame_mode = ATH11K_HW_TXRX_NATIVE_WIFI;
module_param_named(frame_mode, ath11k_frame_mode, uint, 0644);
MODULE_PARM_DESC(frame_mode,
		 "Datapath frame mode (0: raw, 1: native wifi (default), 2: ethernet)");

static const struct ath11k_hw_params ath11k_hw_params[] = {
	{
		.hw_rev = ATH11K_HW_IPQ8074,
		.name = "ipq8074 hw2.0",
		.fw = {
			.dir = "IPQ8074/hw2.0",
			.board_size = 256 * 1024,
			.cal_offset = 128 * 1024,
		},
		.max_radios = 3,
		.bdf_addr = 0x4B0C0000,
		.hw_ops = &ipq8074_ops,
		.ring_mask = &ath11k_hw_ring_mask_ipq8074,
		.internal_sleep_clock = false,
		.regs = &ipq8074_regs,
		.qmi_service_ins_id = ATH11K_QMI_WLFW_SERVICE_INS_ID_V01_IPQ8074,
		.host_ce_config = ath11k_host_ce_config_ipq8074,
		.ce_count = 12,
		.target_ce_config = ath11k_target_ce_config_wlan_ipq8074,
		.target_ce_count = 11,
		.svc_to_ce_map = ath11k_target_service_to_ce_map_wlan_ipq8074,
		.svc_to_ce_map_len = 21,
		.single_pdev_only = false,
		.rxdma1_enable = true,
		.num_rxmda_per_pdev = 1,
		.rx_mac_buf_ring = false,
		.vdev_start_delay = false,
		.htt_peer_map_v2 = true,

		.spectral = {
			.fft_sz = 2,
			/* HW bug, expected BIN size is 2 bytes but HW report as 4 bytes.
			 * so added pad size as 2 bytes to compensate the BIN size
			 */
			.fft_pad_sz = 2,
			.summary_pad_sz = 0,
			.fft_hdr_len = 16,
			.max_fft_bins = 512,
			.fragment_160mhz = true,
		},

		.interface_modes = BIT(NL80211_IFTYPE_STATION) |
					BIT(NL80211_IFTYPE_AP) |
					BIT(NL80211_IFTYPE_MESH_POINT),
		.supports_monitor = true,
		.full_monitor_mode = false,
		.supports_shadow_regs = false,
		.idle_ps = false,
		.supports_sta_ps = false,
		.cold_boot_calib = true,
		.cbcal_restart_fw = true,
		.fw_mem_mode = 0,
		.num_vdevs = 16 + 1,
		.num_peers = 512,
		.supports_suspend = false,
		.hal_desc_sz = sizeof(struct hal_rx_desc_ipq8074),
		.supports_regdb = false,
		.fix_l1ss = true,
		.credit_flow = false,
		.max_tx_ring = DP_TCL_NUM_RING_MAX,
		.hal_params = &ath11k_hw_hal_params_ipq8074,
		.supports_dynamic_smps_6ghz = false,
		.alloc_cacheable_memory = true,
		.supports_rssi_stats = false,
		.fw_wmi_diag_event = false,
		.current_cc_support = false,
		.dbr_debug_support = true,
		.global_reset = false,
		.bios_sar_capa = NULL,
		.m3_fw_support = false,
		.fixed_bdf_addr = true,
		.fixed_mem_region = true,
		.static_window_map = false,
		.hybrid_bus_type = false,
		.fixed_fw_mem = false,
		.support_off_channel_tx = false,
		.supports_multi_bssid = false,

		.sram_dump = {},

		.tcl_ring_retry = true,
		.tx_ring_size = DP_TCL_DATA_RING_SIZE,
		.smp2p_wow_exit = false,
	},
	{
		.hw_rev = ATH11K_HW_IPQ6018_HW10,
		.name = "ipq6018 hw1.0",
		.fw = {
			.dir = "IPQ6018/hw1.0",
			.board_size = 256 * 1024,
			.cal_offset = 128 * 1024,
		},
		.max_radios = 2,
		.bdf_addr = 0x4ABC0000,
		.hw_ops = &ipq6018_ops,
		.ring_mask = &ath11k_hw_ring_mask_ipq8074,
		.internal_sleep_clock = false,
		.regs = &ipq8074_regs,
		.qmi_service_ins_id = ATH11K_QMI_WLFW_SERVICE_INS_ID_V01_IPQ8074,
		.host_ce_config = ath11k_host_ce_config_ipq8074,
		.ce_count = 12,
		.target_ce_config = ath11k_target_ce_config_wlan_ipq8074,
		.target_ce_count = 11,
		.svc_to_ce_map = ath11k_target_service_to_ce_map_wlan_ipq6018,
		.svc_to_ce_map_len = 19,
		.single_pdev_only = false,
		.rxdma1_enable = true,
		.num_rxmda_per_pdev = 1,
		.rx_mac_buf_ring = false,
		.vdev_start_delay = false,
		.htt_peer_map_v2 = true,

		.spectral = {
			.fft_sz = 4,
			.fft_pad_sz = 0,
			.summary_pad_sz = 0,
			.fft_hdr_len = 16,
			.max_fft_bins = 512,
			.fragment_160mhz = true,
		},

		.interface_modes = BIT(NL80211_IFTYPE_STATION) |
					BIT(NL80211_IFTYPE_AP) |
					BIT(NL80211_IFTYPE_MESH_POINT),
		.supports_monitor = true,
		.full_monitor_mode = false,
		.supports_shadow_regs = false,
		.idle_ps = false,
		.supports_sta_ps = false,
		.cold_boot_calib = true,
		.cbcal_restart_fw = true,
		.fw_mem_mode = 0,
		.num_vdevs = 16 + 1,
		.num_peers = 512,
		.supports_suspend = false,
		.hal_desc_sz = sizeof(struct hal_rx_desc_ipq8074),
		.supports_regdb = false,
		.fix_l1ss = true,
		.credit_flow = false,
		.max_tx_ring = DP_TCL_NUM_RING_MAX,
		.hal_params = &ath11k_hw_hal_params_ipq8074,
		.supports_dynamic_smps_6ghz = false,
		.alloc_cacheable_memory = true,
		.supports_rssi_stats = false,
		.fw_wmi_diag_event = false,
		.current_cc_support = false,
		.dbr_debug_support = true,
		.global_reset = false,
		.bios_sar_capa = NULL,
		.m3_fw_support = false,
		.fixed_bdf_addr = true,
		.fixed_mem_region = true,
		.static_window_map = false,
		.hybrid_bus_type = false,
		.fixed_fw_mem = false,
		.support_off_channel_tx = false,
		.supports_multi_bssid = false,

		.sram_dump = {},

		.tcl_ring_retry = true,
		.tx_ring_size = DP_TCL_DATA_RING_SIZE,
		.smp2p_wow_exit = false,
	},
	{
		.name = "qca6390 hw2.0",
		.hw_rev = ATH11K_HW_QCA6390_HW20,
		.fw = {
			.dir = "QCA6390/hw2.0",
			.board_size = 256 * 1024,
			.cal_offset = 128 * 1024,
		},
		.max_radios = 3,
		.bdf_addr = 0x4B0C0000,
		.hw_ops = &qca6390_ops,
		.ring_mask = &ath11k_hw_ring_mask_qca6390,
		.internal_sleep_clock = true,
		.regs = &qca6390_regs,
		.qmi_service_ins_id = ATH11K_QMI_WLFW_SERVICE_INS_ID_V01_QCA6390,
		.host_ce_config = ath11k_host_ce_config_qca6390,
		.ce_count = 9,
		.target_ce_config = ath11k_target_ce_config_wlan_qca6390,
		.target_ce_count = 9,
		.svc_to_ce_map = ath11k_target_service_to_ce_map_wlan_qca6390,
		.svc_to_ce_map_len = 14,
		.single_pdev_only = true,
		.rxdma1_enable = false,
		.num_rxmda_per_pdev = 2,
		.rx_mac_buf_ring = true,
		.vdev_start_delay = true,
		.htt_peer_map_v2 = false,

		.spectral = {
			.fft_sz = 0,
			.fft_pad_sz = 0,
			.summary_pad_sz = 0,
			.fft_hdr_len = 0,
			.max_fft_bins = 0,
			.fragment_160mhz = false,
		},

		.interface_modes = BIT(NL80211_IFTYPE_STATION) |
					BIT(NL80211_IFTYPE_AP),
		.supports_monitor = false,
		.full_monitor_mode = false,
		.supports_shadow_regs = true,
		.idle_ps = true,
		.supports_sta_ps = true,
		.cold_boot_calib = false,
		.cbcal_restart_fw = false,
		.fw_mem_mode = 0,
		.num_vdevs = 16 + 1,
		.num_peers = 512,
		.supports_suspend = true,
		.hal_desc_sz = sizeof(struct hal_rx_desc_ipq8074),
		.supports_regdb = false,
		.fix_l1ss = true,
		.credit_flow = true,
		.max_tx_ring = DP_TCL_NUM_RING_MAX_QCA6390,
		.hal_params = &ath11k_hw_hal_params_qca6390,
		.supports_dynamic_smps_6ghz = false,
		.alloc_cacheable_memory = false,
		.supports_rssi_stats = true,
		.fw_wmi_diag_event = true,
		.current_cc_support = true,
		.dbr_debug_support = false,
		.global_reset = true,
		.bios_sar_capa = NULL,
		.m3_fw_support = true,
		.fixed_bdf_addr = false,
		.fixed_mem_region = false,
		.static_window_map = false,
		.hybrid_bus_type = false,
		.fixed_fw_mem = false,
		.support_off_channel_tx = true,
		.supports_multi_bssid = true,

		.sram_dump = {
			.start = 0x01400000,
			.end = 0x0171ffff,
		},

		.tcl_ring_retry = true,
		.tx_ring_size = DP_TCL_DATA_RING_SIZE,
		.smp2p_wow_exit = false,
	},
	{
		.name = "qcn9074 hw1.0",
		.hw_rev = ATH11K_HW_QCN9074_HW10,
		.fw = {
			.dir = "QCN9074/hw1.0",
			.board_size = 256 * 1024,
			.cal_offset = 128 * 1024,
		},
		.max_radios = 1,
		.single_pdev_only = false,
		.qmi_service_ins_id = ATH11K_QMI_WLFW_SERVICE_INS_ID_V01_QCN9074,
		.hw_ops = &qcn9074_ops,
		.ring_mask = &ath11k_hw_ring_mask_qcn9074,
		.internal_sleep_clock = false,
		.regs = &qcn9074_regs,
		.host_ce_config = ath11k_host_ce_config_qcn9074,
		.ce_count = 6,
		.target_ce_config = ath11k_target_ce_config_wlan_qcn9074,
		.target_ce_count = 9,
		.svc_to_ce_map = ath11k_target_service_to_ce_map_wlan_qcn9074,
		.svc_to_ce_map_len = 18,
		.rxdma1_enable = true,
		.num_rxmda_per_pdev = 1,
		.rx_mac_buf_ring = false,
		.vdev_start_delay = false,
		.htt_peer_map_v2 = true,

		.spectral = {
			.fft_sz = 2,
			.fft_pad_sz = 0,
			.summary_pad_sz = 16,
			.fft_hdr_len = 24,
			.max_fft_bins = 1024,
			.fragment_160mhz = false,
		},

		.interface_modes = BIT(NL80211_IFTYPE_STATION) |
					BIT(NL80211_IFTYPE_AP) |
					BIT(NL80211_IFTYPE_MESH_POINT),
		.supports_monitor = true,
		.full_monitor_mode = true,
		.supports_shadow_regs = false,
		.idle_ps = false,
		.supports_sta_ps = false,
		.cold_boot_calib = false,
		.cbcal_restart_fw = false,
		.fw_mem_mode = 2,
		.num_vdevs = 8,
		.num_peers = 128,
		.supports_suspend = false,
		.hal_desc_sz = sizeof(struct hal_rx_desc_qcn9074),
		.supports_regdb = false,
		.fix_l1ss = true,
		.credit_flow = false,
		.max_tx_ring = DP_TCL_NUM_RING_MAX,
		.hal_params = &ath11k_hw_hal_params_ipq8074,
		.supports_dynamic_smps_6ghz = true,
		.alloc_cacheable_memory = true,
		.supports_rssi_stats = false,
		.fw_wmi_diag_event = false,
		.current_cc_support = false,
		.dbr_debug_support = true,
		.global_reset = false,
		.bios_sar_capa = NULL,
		.m3_fw_support = true,
		.fixed_bdf_addr = false,
		.fixed_mem_region = false,
		.static_window_map = true,
		.hybrid_bus_type = false,
		.fixed_fw_mem = false,
		.support_off_channel_tx = false,
		.supports_multi_bssid = false,

		.sram_dump = {},

		.tcl_ring_retry = true,
		.tx_ring_size = DP_TCL_DATA_RING_SIZE,
		.smp2p_wow_exit = false,
	},
	{
		.name = "wcn6855 hw2.0",
		.hw_rev = ATH11K_HW_WCN6855_HW20,
		.fw = {
			.dir = "WCN6855/hw2.0",
			.board_size = 256 * 1024,
			.cal_offset = 128 * 1024,
		},
		.max_radios = 3,
		.bdf_addr = 0x4B0C0000,
		.hw_ops = &wcn6855_ops,
		.ring_mask = &ath11k_hw_ring_mask_qca6390,
		.internal_sleep_clock = true,
		.regs = &wcn6855_regs,
		.qmi_service_ins_id = ATH11K_QMI_WLFW_SERVICE_INS_ID_V01_QCA6390,
		.host_ce_config = ath11k_host_ce_config_qca6390,
		.ce_count = 9,
		.target_ce_config = ath11k_target_ce_config_wlan_qca6390,
		.target_ce_count = 9,
		.svc_to_ce_map = ath11k_target_service_to_ce_map_wlan_qca6390,
		.svc_to_ce_map_len = 14,
		.single_pdev_only = true,
		.rxdma1_enable = false,
		.num_rxmda_per_pdev = 2,
		.rx_mac_buf_ring = true,
		.vdev_start_delay = true,
		.htt_peer_map_v2 = false,

		.spectral = {
			.fft_sz = 0,
			.fft_pad_sz = 0,
			.summary_pad_sz = 0,
			.fft_hdr_len = 0,
			.max_fft_bins = 0,
			.fragment_160mhz = false,
		},

		.interface_modes = BIT(NL80211_IFTYPE_STATION) |
					BIT(NL80211_IFTYPE_AP),
		.supports_monitor = false,
		.full_monitor_mode = false,
		.supports_shadow_regs = true,
		.idle_ps = true,
		.supports_sta_ps = true,
		.cold_boot_calib = false,
		.cbcal_restart_fw = false,
		.fw_mem_mode = 0,
		.num_vdevs = 16 + 1,
		.num_peers = 512,
		.supports_suspend = true,
		.hal_desc_sz = sizeof(struct hal_rx_desc_wcn6855),
		.supports_regdb = true,
		.fix_l1ss = false,
		.credit_flow = true,
		.max_tx_ring = DP_TCL_NUM_RING_MAX_QCA6390,
		.hal_params = &ath11k_hw_hal_params_qca6390,
		.supports_dynamic_smps_6ghz = false,
		.alloc_cacheable_memory = false,
		.supports_rssi_stats = true,
		.fw_wmi_diag_event = true,
		.current_cc_support = true,
		.dbr_debug_support = false,
		.global_reset = true,
		.bios_sar_capa = &ath11k_hw_sar_capa_wcn6855,
		.m3_fw_support = true,
		.fixed_bdf_addr = false,
		.fixed_mem_region = false,
		.static_window_map = false,
		.hybrid_bus_type = false,
		.fixed_fw_mem = false,
		.support_off_channel_tx = true,
		.supports_multi_bssid = true,

		.sram_dump = {
			.start = 0x01400000,
			.end = 0x0177ffff,
		},

		.tcl_ring_retry = true,
		.tx_ring_size = DP_TCL_DATA_RING_SIZE,
		.smp2p_wow_exit = false,
	},
	{
		.name = "wcn6855 hw2.1",
		.hw_rev = ATH11K_HW_WCN6855_HW21,
		.fw = {
			.dir = "WCN6855/hw2.1",
			.board_size = 256 * 1024,
			.cal_offset = 128 * 1024,
		},
		.max_radios = 3,
		.bdf_addr = 0x4B0C0000,
		.hw_ops = &wcn6855_ops,
		.ring_mask = &ath11k_hw_ring_mask_qca6390,
		.internal_sleep_clock = true,
		.regs = &wcn6855_regs,
		.qmi_service_ins_id = ATH11K_QMI_WLFW_SERVICE_INS_ID_V01_QCA6390,
		.host_ce_config = ath11k_host_ce_config_qca6390,
		.ce_count = 9,
		.target_ce_config = ath11k_target_ce_config_wlan_qca6390,
		.target_ce_count = 9,
		.svc_to_ce_map = ath11k_target_service_to_ce_map_wlan_qca6390,
		.svc_to_ce_map_len = 14,
		.single_pdev_only = true,
		.rxdma1_enable = false,
		.num_rxmda_per_pdev = 2,
		.rx_mac_buf_ring = true,
		.vdev_start_delay = true,
		.htt_peer_map_v2 = false,

		.spectral = {
			.fft_sz = 0,
			.fft_pad_sz = 0,
			.summary_pad_sz = 0,
			.fft_hdr_len = 0,
			.max_fft_bins = 0,
			.fragment_160mhz = false,
		},

		.interface_modes = BIT(NL80211_IFTYPE_STATION) |
					BIT(NL80211_IFTYPE_AP),
		.supports_monitor = false,
		.supports_shadow_regs = true,
		.idle_ps = true,
		.supports_sta_ps = true,
		.cold_boot_calib = false,
		.cbcal_restart_fw = false,
		.fw_mem_mode = 0,
		.num_vdevs = 16 + 1,
		.num_peers = 512,
		.supports_suspend = true,
		.hal_desc_sz = sizeof(struct hal_rx_desc_wcn6855),
		.supports_regdb = true,
		.fix_l1ss = false,
		.credit_flow = true,
		.max_tx_ring = DP_TCL_NUM_RING_MAX_QCA6390,
		.hal_params = &ath11k_hw_hal_params_qca6390,
		.supports_dynamic_smps_6ghz = false,
		.alloc_cacheable_memory = false,
		.supports_rssi_stats = true,
		.fw_wmi_diag_event = true,
		.current_cc_support = true,
		.dbr_debug_support = false,
		.global_reset = true,
		.bios_sar_capa = &ath11k_hw_sar_capa_wcn6855,
		.m3_fw_support = true,
		.fixed_bdf_addr = false,
		.fixed_mem_region = false,
		.static_window_map = false,
		.hybrid_bus_type = false,
		.fixed_fw_mem = false,
		.support_off_channel_tx = true,
		.supports_multi_bssid = true,

		.sram_dump = {
			.start = 0x01400000,
			.end = 0x0177ffff,
		},

		.tcl_ring_retry = true,
		.tx_ring_size = DP_TCL_DATA_RING_SIZE,
		.smp2p_wow_exit = false,
	},
	{
		.name = "wcn6750 hw1.0",
		.hw_rev = ATH11K_HW_WCN6750_HW10,
		.fw = {
			.dir = "WCN6750/hw1.0",
			.board_size = 256 * 1024,
			.cal_offset = 128 * 1024,
		},
		.max_radios = 1,
		.bdf_addr = 0x4B0C0000,
		.hw_ops = &wcn6750_ops,
		.ring_mask = &ath11k_hw_ring_mask_wcn6750,
		.internal_sleep_clock = false,
		.regs = &wcn6750_regs,
		.qmi_service_ins_id = ATH11K_QMI_WLFW_SERVICE_INS_ID_V01_WCN6750,
		.host_ce_config = ath11k_host_ce_config_qca6390,
		.ce_count = 9,
		.target_ce_config = ath11k_target_ce_config_wlan_qca6390,
		.target_ce_count = 9,
		.svc_to_ce_map = ath11k_target_service_to_ce_map_wlan_qca6390,
		.svc_to_ce_map_len = 14,
		.single_pdev_only = true,
		.rxdma1_enable = false,
		.num_rxmda_per_pdev = 1,
		.rx_mac_buf_ring = true,
		.vdev_start_delay = true,
		.htt_peer_map_v2 = false,

		.spectral = {
			.fft_sz = 0,
			.fft_pad_sz = 0,
			.summary_pad_sz = 0,
			.fft_hdr_len = 0,
			.max_fft_bins = 0,
			.fragment_160mhz = false,
		},

		.interface_modes = BIT(NL80211_IFTYPE_STATION) |
					BIT(NL80211_IFTYPE_AP),
		.supports_monitor = false,
		.supports_shadow_regs = true,
		.idle_ps = true,
		.supports_sta_ps = true,
		.cold_boot_calib = true,
		.cbcal_restart_fw = false,
		.fw_mem_mode = 0,
		.num_vdevs = 16 + 1,
		.num_peers = 512,
		.supports_suspend = false,
		.hal_desc_sz = sizeof(struct hal_rx_desc_qcn9074),
		.supports_regdb = true,
		.fix_l1ss = false,
		.credit_flow = true,
		.max_tx_ring = DP_TCL_NUM_RING_MAX,
		.hal_params = &ath11k_hw_hal_params_wcn6750,
		.supports_dynamic_smps_6ghz = false,
		.alloc_cacheable_memory = false,
		.supports_rssi_stats = true,
		.fw_wmi_diag_event = false,
		.current_cc_support = true,
		.dbr_debug_support = false,
		.global_reset = false,
		.bios_sar_capa = NULL,
		.m3_fw_support = false,
		.fixed_bdf_addr = false,
		.fixed_mem_region = false,
		.static_window_map = true,
		.hybrid_bus_type = true,
		.fixed_fw_mem = true,
		.support_off_channel_tx = true,
		.supports_multi_bssid = true,

		.sram_dump = {},

		.tcl_ring_retry = false,
		.tx_ring_size = DP_TCL_DATA_RING_SIZE_WCN6750,
		.smp2p_wow_exit = true,
	},
};

static inline struct ath11k_pdev *ath11k_core_get_single_pdev(struct ath11k_base *ab)
{
	WARN_ON(!ab->hw_params.single_pdev_only);

	return &ab->pdevs[0];
}

void ath11k_fw_stats_pdevs_free(struct list_head *head)
{
	struct ath11k_fw_stats_pdev *i, *tmp;

	list_for_each_entry_safe(i, tmp, head, list) {
		list_del(&i->list);
		kfree(i);
	}
}

void ath11k_fw_stats_vdevs_free(struct list_head *head)
{
	struct ath11k_fw_stats_vdev *i, *tmp;

	list_for_each_entry_safe(i, tmp, head, list) {
		list_del(&i->list);
		kfree(i);
	}
}

void ath11k_fw_stats_bcn_free(struct list_head *head)
{
	struct ath11k_fw_stats_bcn *i, *tmp;

	list_for_each_entry_safe(i, tmp, head, list) {
		list_del(&i->list);
		kfree(i);
	}
}

void ath11k_fw_stats_init(struct ath11k *ar)
{
	INIT_LIST_HEAD(&ar->fw_stats.pdevs);
	INIT_LIST_HEAD(&ar->fw_stats.vdevs);
	INIT_LIST_HEAD(&ar->fw_stats.bcn);

	init_completion(&ar->fw_stats_complete);
}

void ath11k_fw_stats_free(struct ath11k_fw_stats *stats)
{
	ath11k_fw_stats_pdevs_free(&stats->pdevs);
	ath11k_fw_stats_vdevs_free(&stats->vdevs);
	ath11k_fw_stats_bcn_free(&stats->bcn);
}

int ath11k_core_suspend(struct ath11k_base *ab)
{
	int ret;
	struct ath11k_pdev *pdev;
	struct ath11k *ar;

	if (!ab->hw_params.supports_suspend)
		return -EOPNOTSUPP;

	/* so far single_pdev_only chips have supports_suspend as true
	 * and only the first pdev is valid.
	 */
	pdev = ath11k_core_get_single_pdev(ab);
	ar = pdev->ar;
	if (!ar || ar->state != ATH11K_STATE_OFF)
		return 0;

	ret = ath11k_dp_rx_pktlog_stop(ab, true);
	if (ret) {
		ath11k_warn(ab, "failed to stop dp rx (and timer) pktlog during suspend: %d\n",
			    ret);
		return ret;
	}

	ret = ath11k_mac_wait_tx_complete(ar);
	if (ret) {
		ath11k_warn(ab, "failed to wait tx complete: %d\n", ret);
		return ret;
	}

	ret = ath11k_wow_enable(ab);
	if (ret) {
		ath11k_warn(ab, "failed to enable wow during suspend: %d\n", ret);
		return ret;
	}

	ret = ath11k_dp_rx_pktlog_stop(ab, false);
	if (ret) {
		ath11k_warn(ab, "failed to stop dp rx pktlog during suspend: %d\n",
			    ret);
		return ret;
	}

	ath11k_ce_stop_shadow_timers(ab);
	ath11k_dp_stop_shadow_timers(ab);

	ath11k_hif_irq_disable(ab);
	ath11k_hif_ce_irq_disable(ab);

	ret = ath11k_hif_suspend(ab);
	if (ret) {
		ath11k_warn(ab, "failed to suspend hif: %d\n", ret);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL(ath11k_core_suspend);

int ath11k_core_resume(struct ath11k_base *ab)
{
	int ret;
	struct ath11k_pdev *pdev;
	struct ath11k *ar;

	if (!ab->hw_params.supports_suspend)
		return -EOPNOTSUPP;

	/* so far signle_pdev_only chips have supports_suspend as true
	 * and only the first pdev is valid.
	 */
	pdev = ath11k_core_get_single_pdev(ab);
	ar = pdev->ar;
	if (!ar || ar->state != ATH11K_STATE_OFF)
		return 0;

	ret = ath11k_hif_resume(ab);
	if (ret) {
		ath11k_warn(ab, "failed to resume hif during resume: %d\n", ret);
		return ret;
	}

	ath11k_hif_ce_irq_enable(ab);
	ath11k_hif_irq_enable(ab);

	ret = ath11k_dp_rx_pktlog_start(ab);
	if (ret) {
		ath11k_warn(ab, "failed to start rx pktlog during resume: %d\n",
			    ret);
		return ret;
	}

	ret = ath11k_wow_wakeup(ab);
	if (ret) {
		ath11k_warn(ab, "failed to wakeup wow during resume: %d\n", ret);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL(ath11k_core_resume);

static void ath11k_core_check_cc_code_bdfext(const struct dmi_header *hdr, void *data)
{
	struct ath11k_base *ab = data;
	const char *magic = ATH11K_SMBIOS_BDF_EXT_MAGIC;
	struct ath11k_smbios_bdf *smbios = (struct ath11k_smbios_bdf *)hdr;
	ssize_t copied;
	size_t len;
	int i;

	if (ab->qmi.target.bdf_ext[0] != '\0')
		return;

	if (hdr->type != ATH11K_SMBIOS_BDF_EXT_TYPE)
		return;

	if (hdr->length != ATH11K_SMBIOS_BDF_EXT_LENGTH) {
		ath11k_dbg(ab, ATH11K_DBG_BOOT,
			   "wrong smbios bdf ext type length (%d).\n",
			   hdr->length);
		return;
	}

	spin_lock_bh(&ab->base_lock);

	switch (smbios->country_code_flag) {
	case ATH11K_SMBIOS_CC_ISO:
		ab->new_alpha2[0] = (smbios->cc_code >> 8) & 0xff;
		ab->new_alpha2[1] = smbios->cc_code & 0xff;
		ath11k_dbg(ab, ATH11K_DBG_BOOT, "boot smbios cc_code %c%c\n",
			   ab->new_alpha2[0], ab->new_alpha2[1]);
		break;
	case ATH11K_SMBIOS_CC_WW:
		ab->new_alpha2[0] = '0';
		ab->new_alpha2[1] = '0';
		ath11k_dbg(ab, ATH11K_DBG_BOOT, "boot smbios worldwide regdomain\n");
		break;
	default:
		ath11k_dbg(ab, ATH11K_DBG_BOOT, "boot ignore smbios country code setting %d\n",
			   smbios->country_code_flag);
		break;
	}

	spin_unlock_bh(&ab->base_lock);

	if (!smbios->bdf_enabled) {
		ath11k_dbg(ab, ATH11K_DBG_BOOT, "bdf variant name not found.\n");
		return;
	}

	/* Only one string exists (per spec) */
	if (memcmp(smbios->bdf_ext, magic, strlen(magic)) != 0) {
		ath11k_dbg(ab, ATH11K_DBG_BOOT,
			   "bdf variant magic does not match.\n");
		return;
	}

	len = min_t(size_t,
		    strlen(smbios->bdf_ext), sizeof(ab->qmi.target.bdf_ext));
	for (i = 0; i < len; i++) {
		if (!isascii(smbios->bdf_ext[i]) || !isprint(smbios->bdf_ext[i])) {
			ath11k_dbg(ab, ATH11K_DBG_BOOT,
				   "bdf variant name contains non ascii chars.\n");
			return;
		}
	}

	/* Copy extension name without magic prefix */
	copied = strscpy(ab->qmi.target.bdf_ext, smbios->bdf_ext + strlen(magic),
			 sizeof(ab->qmi.target.bdf_ext));
	if (copied < 0) {
		ath11k_dbg(ab, ATH11K_DBG_BOOT,
			   "bdf variant string is longer than the buffer can accommodate\n");
		return;
	}

	ath11k_dbg(ab, ATH11K_DBG_BOOT,
		   "found and validated bdf variant smbios_type 0x%x bdf %s\n",
		   ATH11K_SMBIOS_BDF_EXT_TYPE, ab->qmi.target.bdf_ext);
}

int ath11k_core_check_smbios(struct ath11k_base *ab)
{
	ab->qmi.target.bdf_ext[0] = '\0';
	dmi_walk(ath11k_core_check_cc_code_bdfext, ab);

	if (ab->qmi.target.bdf_ext[0] == '\0')
		return -ENODATA;

	return 0;
}

int ath11k_core_check_dt(struct ath11k_base *ab)
{
	size_t max_len = sizeof(ab->qmi.target.bdf_ext);
	const char *variant = NULL;
	struct device_node *node;

	node = ab->dev->of_node;
	if (!node)
		return -ENOENT;

	of_property_read_string(node, "qcom,ath11k-calibration-variant",
				&variant);
	if (!variant)
		return -ENODATA;

	if (strscpy(ab->qmi.target.bdf_ext, variant, max_len) < 0)
		ath11k_dbg(ab, ATH11K_DBG_BOOT,
			   "bdf variant string is longer than the buffer can accommodate (variant: %s)\n",
			    variant);

	return 0;
}

static int __ath11k_core_create_board_name(struct ath11k_base *ab, char *name,
					   size_t name_len, bool with_variant)
{
	/* strlen(',variant=') + strlen(ab->qmi.target.bdf_ext) */
	char variant[9 + ATH11K_QMI_BDF_EXT_STR_LENGTH] = { 0 };

	if (with_variant && ab->qmi.target.bdf_ext[0] != '\0')
		scnprintf(variant, sizeof(variant), ",variant=%s",
			  ab->qmi.target.bdf_ext);

	switch (ab->id.bdf_search) {
	case ATH11K_BDF_SEARCH_BUS_AND_BOARD:
		scnprintf(name, name_len,
			  "bus=%s,vendor=%04x,device=%04x,subsystem-vendor=%04x,subsystem-device=%04x,qmi-chip-id=%d,qmi-board-id=%d%s",
			  ath11k_bus_str(ab->hif.bus),
			  ab->id.vendor, ab->id.device,
			  ab->id.subsystem_vendor,
			  ab->id.subsystem_device,
			  ab->qmi.target.chip_id,
			  ab->qmi.target.board_id,
			  variant);
		break;
	default:
		scnprintf(name, name_len,
			  "bus=%s,qmi-chip-id=%d,qmi-board-id=%d%s",
			  ath11k_bus_str(ab->hif.bus),
			  ab->qmi.target.chip_id,
			  ab->qmi.target.board_id, variant);
		break;
	}

	ath11k_dbg(ab, ATH11K_DBG_BOOT, "boot using board name '%s'\n", name);

	return 0;
}

static int ath11k_core_create_board_name(struct ath11k_base *ab, char *name,
					 size_t name_len)
{
	return __ath11k_core_create_board_name(ab, name, name_len, true);
}

static int ath11k_core_create_fallback_board_name(struct ath11k_base *ab, char *name,
						  size_t name_len)
{
	return __ath11k_core_create_board_name(ab, name, name_len, false);
}

const struct firmware *ath11k_core_firmware_request(struct ath11k_base *ab,
						    const char *file)
{
	const struct firmware *fw;
	char path[100];
	int ret;

	if (file == NULL)
		return ERR_PTR(-ENOENT);

	ath11k_core_create_firmware_path(ab, file, path, sizeof(path));

	ret = firmware_request_nowarn(&fw, path, ab->dev);
	if (ret)
		return ERR_PTR(ret);

	ath11k_dbg(ab, ATH11K_DBG_BOOT, "boot firmware request %s size %zu\n",
		   path, fw->size);

	return fw;
}

void ath11k_core_free_bdf(struct ath11k_base *ab, struct ath11k_board_data *bd)
{
	if (!IS_ERR(bd->fw))
		release_firmware(bd->fw);

	memset(bd, 0, sizeof(*bd));
}

static int ath11k_core_parse_bd_ie_board(struct ath11k_base *ab,
					 struct ath11k_board_data *bd,
					 const void *buf, size_t buf_len,
					 const char *boardname,
					 int ie_id,
					 int name_id,
					 int data_id)
{
	const struct ath11k_fw_ie *hdr;
	bool name_match_found;
	int ret, board_ie_id;
	size_t board_ie_len;
	const void *board_ie_data;

	name_match_found = false;

	/* go through ATH11K_BD_IE_BOARD_/ATH11K_BD_IE_REGDB_ elements */
	while (buf_len > sizeof(struct ath11k_fw_ie)) {
		hdr = buf;
		board_ie_id = le32_to_cpu(hdr->id);
		board_ie_len = le32_to_cpu(hdr->len);
		board_ie_data = hdr->data;

		buf_len -= sizeof(*hdr);
		buf += sizeof(*hdr);

		if (buf_len < ALIGN(board_ie_len, 4)) {
			ath11k_err(ab, "invalid %s length: %zu < %zu\n",
				   ath11k_bd_ie_type_str(ie_id),
				   buf_len, ALIGN(board_ie_len, 4));
			ret = -EINVAL;
			goto out;
		}

		if (board_ie_id == name_id) {
			ath11k_dbg_dump(ab, ATH11K_DBG_BOOT, "board name", "",
					board_ie_data, board_ie_len);

			if (board_ie_len != strlen(boardname))
				goto next;

			ret = memcmp(board_ie_data, boardname, strlen(boardname));
			if (ret)
				goto next;

			name_match_found = true;
			ath11k_dbg(ab, ATH11K_DBG_BOOT,
				   "boot found match %s for name '%s'",
				   ath11k_bd_ie_type_str(ie_id),
				   boardname);
		} else if (board_ie_id == data_id) {
			if (!name_match_found)
				/* no match found */
				goto next;

			ath11k_dbg(ab, ATH11K_DBG_BOOT,
				   "boot found %s for '%s'",
				   ath11k_bd_ie_type_str(ie_id),
				   boardname);

			bd->data = board_ie_data;
			bd->len = board_ie_len;

			ret = 0;
			goto out;
		} else {
			ath11k_warn(ab, "unknown %s id found: %d\n",
				    ath11k_bd_ie_type_str(ie_id),
				    board_ie_id);
		}
next:
		/* jump over the padding */
		board_ie_len = ALIGN(board_ie_len, 4);

		buf_len -= board_ie_len;
		buf += board_ie_len;
	}

	/* no match found */
	ret = -ENOENT;

out:
	return ret;
}

static int ath11k_core_fetch_board_data_api_n(struct ath11k_base *ab,
					      struct ath11k_board_data *bd,
					      const char *boardname,
					      int ie_id_match,
					      int name_id,
					      int data_id)
{
	size_t len, magic_len;
	const u8 *data;
	char *filename, filepath[100];
	size_t ie_len;
	struct ath11k_fw_ie *hdr;
	int ret, ie_id;

	filename = ATH11K_BOARD_API2_FILE;

	if (!bd->fw)
		bd->fw = ath11k_core_firmware_request(ab, filename);

	if (IS_ERR(bd->fw))
		return PTR_ERR(bd->fw);

	data = bd->fw->data;
	len = bd->fw->size;

	ath11k_core_create_firmware_path(ab, filename,
					 filepath, sizeof(filepath));

	/* magic has extra null byte padded */
	magic_len = strlen(ATH11K_BOARD_MAGIC) + 1;
	if (len < magic_len) {
		ath11k_err(ab, "failed to find magic value in %s, file too short: %zu\n",
			   filepath, len);
		ret = -EINVAL;
		goto err;
	}

	if (memcmp(data, ATH11K_BOARD_MAGIC, magic_len)) {
		ath11k_err(ab, "found invalid board magic\n");
		ret = -EINVAL;
		goto err;
	}

	/* magic is padded to 4 bytes */
	magic_len = ALIGN(magic_len, 4);
	if (len < magic_len) {
		ath11k_err(ab, "failed: %s too small to contain board data, len: %zu\n",
			   filepath, len);
		ret = -EINVAL;
		goto err;
	}

	data += magic_len;
	len -= magic_len;

	while (len > sizeof(struct ath11k_fw_ie)) {
		hdr = (struct ath11k_fw_ie *)data;
		ie_id = le32_to_cpu(hdr->id);
		ie_len = le32_to_cpu(hdr->len);

		len -= sizeof(*hdr);
		data = hdr->data;

		if (len < ALIGN(ie_len, 4)) {
			ath11k_err(ab, "invalid length for board ie_id %d ie_len %zu len %zu\n",
				   ie_id, ie_len, len);
			ret = -EINVAL;
			goto err;
		}

		if (ie_id == ie_id_match) {
			ret = ath11k_core_parse_bd_ie_board(ab, bd, data,
							    ie_len,
							    boardname,
							    ie_id_match,
							    name_id,
							    data_id);
			if (ret == -ENOENT)
				/* no match found, continue */
				goto next;
			else if (ret)
				/* there was an error, bail out */
				goto err;
			/* either found or error, so stop searching */
			goto out;
		}
next:
		/* jump over the padding */
		ie_len = ALIGN(ie_len, 4);

		len -= ie_len;
		data += ie_len;
	}

out:
	if (!bd->data || !bd->len) {
		ath11k_dbg(ab, ATH11K_DBG_BOOT,
			   "failed to fetch %s for %s from %s\n",
			   ath11k_bd_ie_type_str(ie_id_match),
			   boardname, filepath);
		ret = -ENODATA;
		goto err;
	}

	return 0;

err:
	ath11k_core_free_bdf(ab, bd);
	return ret;
}

int ath11k_core_fetch_board_data_api_1(struct ath11k_base *ab,
				       struct ath11k_board_data *bd,
				       const char *name)
{
	bd->fw = ath11k_core_firmware_request(ab, name);

	if (IS_ERR(bd->fw))
		return PTR_ERR(bd->fw);

	bd->data = bd->fw->data;
	bd->len = bd->fw->size;

	return 0;
}

#define BOARD_NAME_SIZE 200
int ath11k_core_fetch_bdf(struct ath11k_base *ab, struct ath11k_board_data *bd)
{
	char boardname[BOARD_NAME_SIZE], fallback_boardname[BOARD_NAME_SIZE];
	char *filename, filepath[100];
	int ret;

	filename = ATH11K_BOARD_API2_FILE;

	ret = ath11k_core_create_board_name(ab, boardname, sizeof(boardname));
	if (ret) {
		ath11k_err(ab, "failed to create board name: %d", ret);
		return ret;
	}

	ab->bd_api = 2;
	ret = ath11k_core_fetch_board_data_api_n(ab, bd, boardname,
						 ATH11K_BD_IE_BOARD,
						 ATH11K_BD_IE_BOARD_NAME,
						 ATH11K_BD_IE_BOARD_DATA);
	if (!ret)
		goto success;

	ret = ath11k_core_create_fallback_board_name(ab, fallback_boardname,
						     sizeof(fallback_boardname));
	if (ret) {
		ath11k_err(ab, "failed to create fallback board name: %d", ret);
		return ret;
	}

	ret = ath11k_core_fetch_board_data_api_n(ab, bd, fallback_boardname,
						 ATH11K_BD_IE_BOARD,
						 ATH11K_BD_IE_BOARD_NAME,
						 ATH11K_BD_IE_BOARD_DATA);
	if (!ret)
		goto success;

	ab->bd_api = 1;
	ret = ath11k_core_fetch_board_data_api_1(ab, bd, ATH11K_DEFAULT_BOARD_FILE);
	if (ret) {
		ath11k_core_create_firmware_path(ab, filename,
						 filepath, sizeof(filepath));
		ath11k_err(ab, "failed to fetch board data for %s from %s\n",
			   boardname, filepath);
		if (memcmp(boardname, fallback_boardname, strlen(boardname)))
			ath11k_err(ab, "failed to fetch board data for %s from %s\n",
				   fallback_boardname, filepath);

		ath11k_err(ab, "failed to fetch board.bin from %s\n",
			   ab->hw_params.fw.dir);
		return ret;
	}

success:
	ath11k_dbg(ab, ATH11K_DBG_BOOT, "using board api %d\n", ab->bd_api);
	return 0;
}

int ath11k_core_fetch_regdb(struct ath11k_base *ab, struct ath11k_board_data *bd)
{
	char boardname[BOARD_NAME_SIZE];
	int ret;

	ret = ath11k_core_create_board_name(ab, boardname, BOARD_NAME_SIZE);
	if (ret) {
		ath11k_dbg(ab, ATH11K_DBG_BOOT,
			   "failed to create board name for regdb: %d", ret);
		goto exit;
	}

	ret = ath11k_core_fetch_board_data_api_n(ab, bd, boardname,
						 ATH11K_BD_IE_REGDB,
						 ATH11K_BD_IE_REGDB_NAME,
						 ATH11K_BD_IE_REGDB_DATA);
	if (!ret)
		goto exit;

	ret = ath11k_core_fetch_board_data_api_1(ab, bd, ATH11K_REGDB_FILE_NAME);
	if (ret)
		ath11k_dbg(ab, ATH11K_DBG_BOOT, "failed to fetch %s from %s\n",
			   ATH11K_REGDB_FILE_NAME, ab->hw_params.fw.dir);

exit:
	if (!ret)
		ath11k_dbg(ab, ATH11K_DBG_BOOT, "fetched regdb\n");

	return ret;
}

static void ath11k_core_stop(struct ath11k_base *ab)
{
	if (!test_bit(ATH11K_FLAG_CRASH_FLUSH, &ab->dev_flags))
		ath11k_qmi_firmware_stop(ab);

	ath11k_hif_stop(ab);
	ath11k_wmi_detach(ab);
	ath11k_dp_pdev_reo_cleanup(ab);

	/* De-Init of components as needed */
}

static int ath11k_core_soc_create(struct ath11k_base *ab)
{
	int ret;

	ret = ath11k_qmi_init_service(ab);
	if (ret) {
		ath11k_err(ab, "failed to initialize qmi :%d\n", ret);
		return ret;
	}

	ret = ath11k_debugfs_soc_create(ab);
	if (ret) {
		ath11k_err(ab, "failed to create ath11k debugfs\n");
		goto err_qmi_deinit;
	}

	ret = ath11k_hif_power_up(ab);
	if (ret) {
		ath11k_err(ab, "failed to power up :%d\n", ret);
		goto err_debugfs_reg;
	}

	return 0;

err_debugfs_reg:
	ath11k_debugfs_soc_destroy(ab);
err_qmi_deinit:
	ath11k_qmi_deinit_service(ab);
	return ret;
}

static void ath11k_core_soc_destroy(struct ath11k_base *ab)
{
	ath11k_debugfs_soc_destroy(ab);
	ath11k_dp_free(ab);
	ath11k_reg_free(ab);
	ath11k_qmi_deinit_service(ab);
}

static int ath11k_core_pdev_create(struct ath11k_base *ab)
{
	int ret;

	ret = ath11k_debugfs_pdev_create(ab);
	if (ret) {
		ath11k_err(ab, "failed to create core pdev debugfs: %d\n", ret);
		return ret;
	}

	ret = ath11k_dp_pdev_alloc(ab);
	if (ret) {
		ath11k_err(ab, "failed to attach DP pdev: %d\n", ret);
		goto err_pdev_debug;
	}

	ret = ath11k_mac_register(ab);
	if (ret) {
		ath11k_err(ab, "failed register the radio with mac80211: %d\n", ret);
		goto err_dp_pdev_free;
	}

	ret = ath11k_thermal_register(ab);
	if (ret) {
		ath11k_err(ab, "could not register thermal device: %d\n",
			   ret);
		goto err_mac_unregister;
	}

	ret = ath11k_spectral_init(ab);
	if (ret) {
		ath11k_err(ab, "failed to init spectral %d\n", ret);
		goto err_thermal_unregister;
	}

	return 0;

err_thermal_unregister:
	ath11k_thermal_unregister(ab);
err_mac_unregister:
	ath11k_mac_unregister(ab);
err_dp_pdev_free:
	ath11k_dp_pdev_free(ab);
err_pdev_debug:
	ath11k_debugfs_pdev_destroy(ab);

	return ret;
}

static void ath11k_core_pdev_destroy(struct ath11k_base *ab)
{
	ath11k_spectral_deinit(ab);
	ath11k_thermal_unregister(ab);
	ath11k_mac_unregister(ab);
	ath11k_hif_irq_disable(ab);
	ath11k_dp_pdev_free(ab);
	ath11k_debugfs_pdev_destroy(ab);
}

static int ath11k_core_start(struct ath11k_base *ab)
{
	int ret;

	ret = ath11k_wmi_attach(ab);
	if (ret) {
		ath11k_err(ab, "failed to attach wmi: %d\n", ret);
		return ret;
	}

	ret = ath11k_htc_init(ab);
	if (ret) {
		ath11k_err(ab, "failed to init htc: %d\n", ret);
		goto err_wmi_detach;
	}

	ret = ath11k_hif_start(ab);
	if (ret) {
		ath11k_err(ab, "failed to start HIF: %d\n", ret);
		goto err_wmi_detach;
	}

	ret = ath11k_htc_wait_target(&ab->htc);
	if (ret) {
		ath11k_err(ab, "failed to connect to HTC: %d\n", ret);
		goto err_hif_stop;
	}

	ret = ath11k_dp_htt_connect(&ab->dp);
	if (ret) {
		ath11k_err(ab, "failed to connect to HTT: %d\n", ret);
		goto err_hif_stop;
	}

	ret = ath11k_wmi_connect(ab);
	if (ret) {
		ath11k_err(ab, "failed to connect wmi: %d\n", ret);
		goto err_hif_stop;
	}

	ret = ath11k_htc_start(&ab->htc);
	if (ret) {
		ath11k_err(ab, "failed to start HTC: %d\n", ret);
		goto err_hif_stop;
	}

	ret = ath11k_wmi_wait_for_service_ready(ab);
	if (ret) {
		ath11k_err(ab, "failed to receive wmi service ready event: %d\n",
			   ret);
		goto err_hif_stop;
	}

	ret = ath11k_mac_allocate(ab);
	if (ret) {
		ath11k_err(ab, "failed to create new hw device with mac80211 :%d\n",
			   ret);
		goto err_hif_stop;
	}

	ath11k_dp_pdev_pre_alloc(ab);

	ret = ath11k_dp_pdev_reo_setup(ab);
	if (ret) {
		ath11k_err(ab, "failed to initialize reo destination rings: %d\n", ret);
		goto err_mac_destroy;
	}

	ret = ath11k_wmi_cmd_init(ab);
	if (ret) {
		ath11k_err(ab, "failed to send wmi init cmd: %d\n", ret);
		goto err_reo_cleanup;
	}

	ret = ath11k_wmi_wait_for_unified_ready(ab);
	if (ret) {
		ath11k_err(ab, "failed to receive wmi unified ready event: %d\n",
			   ret);
		goto err_reo_cleanup;
	}

	/* put hardware to DBS mode */
	if (ab->hw_params.single_pdev_only && ab->hw_params.num_rxmda_per_pdev > 1) {
		ret = ath11k_wmi_set_hw_mode(ab, WMI_HOST_HW_MODE_DBS);
		if (ret) {
			ath11k_err(ab, "failed to send dbs mode: %d\n", ret);
			goto err_hif_stop;
		}
	}

	ret = ath11k_dp_tx_htt_h2t_ver_req_msg(ab);
	if (ret) {
		ath11k_err(ab, "failed to send htt version request message: %d\n",
			   ret);
		goto err_reo_cleanup;
	}

	return 0;

err_reo_cleanup:
	ath11k_dp_pdev_reo_cleanup(ab);
err_mac_destroy:
	ath11k_mac_destroy(ab);
err_hif_stop:
	ath11k_hif_stop(ab);
err_wmi_detach:
	ath11k_wmi_detach(ab);

	return ret;
}

static int ath11k_core_start_firmware(struct ath11k_base *ab,
				      enum ath11k_firmware_mode mode)
{
	int ret;

	ath11k_ce_get_shadow_config(ab, &ab->qmi.ce_cfg.shadow_reg_v2,
				    &ab->qmi.ce_cfg.shadow_reg_v2_len);

	ret = ath11k_qmi_firmware_start(ab, mode);
	if (ret) {
		ath11k_err(ab, "failed to send firmware start: %d\n", ret);
		return ret;
	}

	return ret;
}

int ath11k_core_qmi_firmware_ready(struct ath11k_base *ab)
{
	int ret;

	ret = ath11k_core_start_firmware(ab, ATH11K_FIRMWARE_MODE_NORMAL);
	if (ret) {
		ath11k_err(ab, "failed to start firmware: %d\n", ret);
		return ret;
	}

	ret = ath11k_ce_init_pipes(ab);
	if (ret) {
		ath11k_err(ab, "failed to initialize CE: %d\n", ret);
		goto err_firmware_stop;
	}

	ret = ath11k_dp_alloc(ab);
	if (ret) {
		ath11k_err(ab, "failed to init DP: %d\n", ret);
		goto err_firmware_stop;
	}

	switch (ath11k_crypto_mode) {
	case ATH11K_CRYPT_MODE_SW:
		set_bit(ATH11K_FLAG_HW_CRYPTO_DISABLED, &ab->dev_flags);
		set_bit(ATH11K_FLAG_RAW_MODE, &ab->dev_flags);
		break;
	case ATH11K_CRYPT_MODE_HW:
		clear_bit(ATH11K_FLAG_HW_CRYPTO_DISABLED, &ab->dev_flags);
		clear_bit(ATH11K_FLAG_RAW_MODE, &ab->dev_flags);
		break;
	default:
		ath11k_info(ab, "invalid crypto_mode: %d\n", ath11k_crypto_mode);
		return -EINVAL;
	}

	if (ath11k_frame_mode == ATH11K_HW_TXRX_RAW)
		set_bit(ATH11K_FLAG_RAW_MODE, &ab->dev_flags);

	mutex_lock(&ab->core_lock);
	ret = ath11k_core_start(ab);
	if (ret) {
		ath11k_err(ab, "failed to start core: %d\n", ret);
		goto err_dp_free;
	}

	ret = ath11k_core_pdev_create(ab);
	if (ret) {
		ath11k_err(ab, "failed to create pdev core: %d\n", ret);
		goto err_core_stop;
	}
	ath11k_hif_irq_enable(ab);
	mutex_unlock(&ab->core_lock);

	return 0;

err_core_stop:
	ath11k_core_stop(ab);
	ath11k_mac_destroy(ab);
err_dp_free:
	ath11k_dp_free(ab);
	mutex_unlock(&ab->core_lock);
err_firmware_stop:
	ath11k_qmi_firmware_stop(ab);

	return ret;
}

static int ath11k_core_reconfigure_on_crash(struct ath11k_base *ab)
{
	int ret;

	mutex_lock(&ab->core_lock);
	ath11k_thermal_unregister(ab);
	ath11k_hif_irq_disable(ab);
	ath11k_dp_pdev_free(ab);
	ath11k_spectral_deinit(ab);
	ath11k_hif_stop(ab);
	ath11k_wmi_detach(ab);
	ath11k_dp_pdev_reo_cleanup(ab);
	mutex_unlock(&ab->core_lock);

	ath11k_dp_free(ab);
	ath11k_hal_srng_deinit(ab);

	ab->free_vdev_map = (1LL << (ab->num_radios * TARGET_NUM_VDEVS(ab))) - 1;

	ret = ath11k_hal_srng_init(ab);
	if (ret)
		return ret;

	clear_bit(ATH11K_FLAG_CRASH_FLUSH, &ab->dev_flags);

	ret = ath11k_core_qmi_firmware_ready(ab);
	if (ret)
		goto err_hal_srng_deinit;

	clear_bit(ATH11K_FLAG_RECOVERY, &ab->dev_flags);

	return 0;

err_hal_srng_deinit:
	ath11k_hal_srng_deinit(ab);
	return ret;
}

void ath11k_core_halt(struct ath11k *ar)
{
	struct ath11k_base *ab = ar->ab;

	lockdep_assert_held(&ar->conf_mutex);

	ar->num_created_vdevs = 0;
	ar->allocated_vdev_map = 0;

	ath11k_mac_scan_finish(ar);
	ath11k_mac_peer_cleanup_all(ar);
	cancel_delayed_work_sync(&ar->scan.timeout);
	cancel_work_sync(&ar->regd_update_work);
	cancel_work_sync(&ab->update_11d_work);

	rcu_assign_pointer(ab->pdevs_active[ar->pdev_idx], NULL);
	synchronize_rcu();
	INIT_LIST_HEAD(&ar->arvifs);
	idr_init(&ar->txmgmt_idr);
}

static void ath11k_update_11d(struct work_struct *work)
{
	struct ath11k_base *ab = container_of(work, struct ath11k_base, update_11d_work);
	struct ath11k *ar;
	struct ath11k_pdev *pdev;
	struct wmi_set_current_country_params set_current_param = {};
	int ret, i;

	spin_lock_bh(&ab->base_lock);
	memcpy(&set_current_param.alpha2, &ab->new_alpha2, 2);
	spin_unlock_bh(&ab->base_lock);

	ath11k_dbg(ab, ATH11K_DBG_WMI, "update 11d new cc %c%c\n",
		   set_current_param.alpha2[0],
		   set_current_param.alpha2[1]);

	for (i = 0; i < ab->num_radios; i++) {
		pdev = &ab->pdevs[i];
		ar = pdev->ar;

		memcpy(&ar->alpha2, &set_current_param.alpha2, 2);
		ret = ath11k_wmi_send_set_current_country_cmd(ar, &set_current_param);
		if (ret)
			ath11k_warn(ar->ab,
				    "pdev id %d failed set current country code: %d\n",
				    i, ret);
	}
}

static void ath11k_core_pre_reconfigure_recovery(struct ath11k_base *ab)
{
	struct ath11k *ar;
	struct ath11k_pdev *pdev;
	int i;

	spin_lock_bh(&ab->base_lock);
	ab->stats.fw_crash_counter++;
	spin_unlock_bh(&ab->base_lock);

	for (i = 0; i < ab->num_radios; i++) {
		pdev = &ab->pdevs[i];
		ar = pdev->ar;
		if (!ar || ar->state == ATH11K_STATE_OFF)
			continue;

		ieee80211_stop_queues(ar->hw);
		ath11k_mac_drain_tx(ar);
		ar->state_11d = ATH11K_11D_IDLE;
		complete(&ar->completed_11d_scan);
		complete(&ar->scan.started);
		complete_all(&ar->scan.completed);
		complete(&ar->scan.on_channel);
		complete(&ar->peer_assoc_done);
		complete(&ar->peer_delete_done);
		complete(&ar->install_key_done);
		complete(&ar->vdev_setup_done);
		complete(&ar->vdev_delete_done);
		complete(&ar->bss_survey_done);
		complete(&ar->thermal.wmi_sync);

		wake_up(&ar->dp.tx_empty_waitq);
		idr_for_each(&ar->txmgmt_idr,
			     ath11k_mac_tx_mgmt_pending_free, ar);
		idr_destroy(&ar->txmgmt_idr);
		wake_up(&ar->txmgmt_empty_waitq);
	}

	wake_up(&ab->wmi_ab.tx_credits_wq);
	wake_up(&ab->peer_mapping_wq);

	reinit_completion(&ab->driver_recovery);
}

static void ath11k_core_post_reconfigure_recovery(struct ath11k_base *ab)
{
	struct ath11k *ar;
	struct ath11k_pdev *pdev;
	int i;

	for (i = 0; i < ab->num_radios; i++) {
		pdev = &ab->pdevs[i];
		ar = pdev->ar;
		if (!ar || ar->state == ATH11K_STATE_OFF)
			continue;

		mutex_lock(&ar->conf_mutex);

		switch (ar->state) {
		case ATH11K_STATE_ON:
			ar->state = ATH11K_STATE_RESTARTING;
			ath11k_core_halt(ar);
			ieee80211_restart_hw(ar->hw);
			break;
		case ATH11K_STATE_OFF:
			ath11k_warn(ab,
				    "cannot restart radio %d that hasn't been started\n",
				    i);
			break;
		case ATH11K_STATE_RESTARTING:
			break;
		case ATH11K_STATE_RESTARTED:
			ar->state = ATH11K_STATE_WEDGED;
			fallthrough;
		case ATH11K_STATE_WEDGED:
			ath11k_warn(ab,
				    "device is wedged, will not restart radio %d\n", i);
			break;
		}
		mutex_unlock(&ar->conf_mutex);
	}
	complete(&ab->driver_recovery);
}

static void ath11k_core_restart(struct work_struct *work)
{
	struct ath11k_base *ab = container_of(work, struct ath11k_base, restart_work);
	int ret;

	if (!ab->is_reset)
		ath11k_core_pre_reconfigure_recovery(ab);

	ret = ath11k_core_reconfigure_on_crash(ab);
	if (ret) {
		ath11k_err(ab, "failed to reconfigure driver on crash recovery\n");
		return;
	}

	if (ab->is_reset)
		complete_all(&ab->reconfigure_complete);

	if (!ab->is_reset)
		ath11k_core_post_reconfigure_recovery(ab);
}

static void ath11k_core_reset(struct work_struct *work)
{
	struct ath11k_base *ab = container_of(work, struct ath11k_base, reset_work);
	int reset_count, fail_cont_count;
	long time_left;

	if (!(test_bit(ATH11K_FLAG_REGISTERED, &ab->dev_flags))) {
		ath11k_warn(ab, "ignore reset dev flags 0x%lx\n", ab->dev_flags);
		return;
	}

	/* Sometimes the recovery will fail and then the next all recovery fail,
	 * this is to avoid infinite recovery since it can not recovery success.
	 */
	fail_cont_count = atomic_read(&ab->fail_cont_count);

	if (fail_cont_count >= ATH11K_RESET_MAX_FAIL_COUNT_FINAL)
		return;

	if (fail_cont_count >= ATH11K_RESET_MAX_FAIL_COUNT_FIRST &&
	    time_before(jiffies, ab->reset_fail_timeout))
		return;

	reset_count = atomic_inc_return(&ab->reset_count);

	if (reset_count > 1) {
		/* Sometimes it happened another reset worker before the previous one
		 * completed, then the second reset worker will destroy the previous one,
		 * thus below is to avoid that.
		 */
		ath11k_warn(ab, "already resetting count %d\n", reset_count);

		reinit_completion(&ab->reset_complete);
		time_left = wait_for_completion_timeout(&ab->reset_complete,
							ATH11K_RESET_TIMEOUT_HZ);

		if (time_left) {
			ath11k_dbg(ab, ATH11K_DBG_BOOT, "to skip reset\n");
			atomic_dec(&ab->reset_count);
			return;
		}

		ab->reset_fail_timeout = jiffies + ATH11K_RESET_FAIL_TIMEOUT_HZ;
		/* Record the continuous recovery fail count when recovery failed*/
		atomic_inc(&ab->fail_cont_count);
	}

	ath11k_dbg(ab, ATH11K_DBG_BOOT, "reset starting\n");

	ab->is_reset = true;
	atomic_set(&ab->recovery_count, 0);
	reinit_completion(&ab->recovery_start);
	atomic_set(&ab->recovery_start_count, 0);

	ath11k_core_pre_reconfigure_recovery(ab);

	reinit_completion(&ab->reconfigure_complete);
	ath11k_core_post_reconfigure_recovery(ab);

	ath11k_dbg(ab, ATH11K_DBG_BOOT, "waiting recovery start...\n");

	time_left = wait_for_completion_timeout(&ab->recovery_start,
						ATH11K_RECOVER_START_TIMEOUT_HZ);

	ath11k_hif_power_down(ab);
	ath11k_hif_power_up(ab);

	ath11k_dbg(ab, ATH11K_DBG_BOOT, "reset started\n");
}

static int ath11k_init_hw_params(struct ath11k_base *ab)
{
	const struct ath11k_hw_params *hw_params = NULL;
	int i;

	for (i = 0; i < ARRAY_SIZE(ath11k_hw_params); i++) {
		hw_params = &ath11k_hw_params[i];

		if (hw_params->hw_rev == ab->hw_rev)
			break;
	}

	if (i == ARRAY_SIZE(ath11k_hw_params)) {
		ath11k_err(ab, "Unsupported hardware version: 0x%x\n", ab->hw_rev);
		return -EINVAL;
	}

	ab->hw_params = *hw_params;

	ath11k_info(ab, "%s\n", ab->hw_params.name);

	return 0;
}

int ath11k_core_pre_init(struct ath11k_base *ab)
{
	int ret;

	ret = ath11k_init_hw_params(ab);
	if (ret) {
		ath11k_err(ab, "failed to get hw params: %d\n", ret);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL(ath11k_core_pre_init);

int ath11k_core_init(struct ath11k_base *ab)
{
	int ret;

	ret = ath11k_core_soc_create(ab);
	if (ret) {
		ath11k_err(ab, "failed to create soc core: %d\n", ret);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL(ath11k_core_init);

void ath11k_core_deinit(struct ath11k_base *ab)
{
	mutex_lock(&ab->core_lock);

	ath11k_core_pdev_destroy(ab);
	ath11k_core_stop(ab);

	mutex_unlock(&ab->core_lock);

	ath11k_hif_power_down(ab);
	ath11k_mac_destroy(ab);
	ath11k_core_soc_destroy(ab);
}
EXPORT_SYMBOL(ath11k_core_deinit);

void ath11k_core_free(struct ath11k_base *ab)
{
	destroy_workqueue(ab->workqueue_aux);
	destroy_workqueue(ab->workqueue);

	kfree(ab);
}
EXPORT_SYMBOL(ath11k_core_free);

struct ath11k_base *ath11k_core_alloc(struct device *dev, size_t priv_size,
				      enum ath11k_bus bus)
{
	struct ath11k_base *ab;

	ab = kzalloc(sizeof(*ab) + priv_size, GFP_KERNEL);
	if (!ab)
		return NULL;

	init_completion(&ab->driver_recovery);

	ab->workqueue = create_singlethread_workqueue("ath11k_wq");
	if (!ab->workqueue)
		goto err_sc_free;

	ab->workqueue_aux = create_singlethread_workqueue("ath11k_aux_wq");
	if (!ab->workqueue_aux)
		goto err_free_wq;

	mutex_init(&ab->core_lock);
	mutex_init(&ab->tbl_mtx_lock);
	spin_lock_init(&ab->base_lock);
	mutex_init(&ab->vdev_id_11d_lock);
	init_completion(&ab->reset_complete);
	init_completion(&ab->reconfigure_complete);
	init_completion(&ab->recovery_start);

	INIT_LIST_HEAD(&ab->peers);
	init_waitqueue_head(&ab->peer_mapping_wq);
	init_waitqueue_head(&ab->wmi_ab.tx_credits_wq);
	init_waitqueue_head(&ab->qmi.cold_boot_waitq);
	INIT_WORK(&ab->restart_work, ath11k_core_restart);
	INIT_WORK(&ab->update_11d_work, ath11k_update_11d);
	INIT_WORK(&ab->reset_work, ath11k_core_reset);
	timer_setup(&ab->rx_replenish_retry, ath11k_ce_rx_replenish_retry, 0);
	init_completion(&ab->htc_suspend);
	init_completion(&ab->wow.wakeup_completed);

	ab->dev = dev;
	ab->hif.bus = bus;

	return ab;

err_free_wq:
	destroy_workqueue(ab->workqueue);
err_sc_free:
	kfree(ab);
	return NULL;
}
EXPORT_SYMBOL(ath11k_core_alloc);

MODULE_DESCRIPTION("Core module for Qualcomm Atheros 802.11ax wireless LAN cards.");
MODULE_LICENSE("Dual BSD/GPL");
