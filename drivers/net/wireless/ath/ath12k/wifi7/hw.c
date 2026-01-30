// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2018-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/types.h>
#include <linux/bitops.h>
#include <linux/bitfield.h>

#include "../debug.h"
#include "../core.h"
#include "../ce.h"
#include "ce.h"
#include "../hw.h"
#include "hw.h"
#include "../mhi.h"
#include "mhi.h"
#include "dp_rx.h"
#include "../peer.h"
#include "wmi.h"
#include "../wow.h"
#include "../debugfs.h"
#include "../debugfs_sta.h"
#include "../testmode.h"
#include "hal.h"
#include "dp_tx.h"

static const guid_t wcn7850_uuid = GUID_INIT(0xf634f534, 0x6147, 0x11ec,
					     0x90, 0xd6, 0x02, 0x42,
					     0xac, 0x12, 0x00, 0x03);

static u8 ath12k_wifi7_hw_qcn9274_mac_from_pdev_id(int pdev_idx)
{
	return pdev_idx;
}

static int
ath12k_wifi7_hw_mac_id_to_pdev_id_qcn9274(const struct ath12k_hw_params *hw,
					  int mac_id)
{
	return mac_id;
}

static int
ath12k_wifi7_hw_mac_id_to_srng_id_qcn9274(const struct ath12k_hw_params *hw,
					  int mac_id)
{
	return 0;
}

static u8 ath12k_wifi7_hw_get_ring_selector_qcn9274(struct sk_buff *skb)
{
	return smp_processor_id();
}

static bool ath12k_wifi7_dp_srng_is_comp_ring_qcn9274(int ring_num)
{
	if (ring_num < 3 || ring_num == 4)
		return true;

	return false;
}

static bool
ath12k_wifi7_is_frame_link_agnostic_qcn9274(struct ath12k_link_vif *arvif,
					    struct ieee80211_mgmt *mgmt)
{
	return ieee80211_is_action(mgmt->frame_control);
}

static int
ath12k_wifi7_hw_mac_id_to_pdev_id_wcn7850(const struct ath12k_hw_params *hw,
					  int mac_id)
{
	return 0;
}

static int
ath12k_wifi7_hw_mac_id_to_srng_id_wcn7850(const struct ath12k_hw_params *hw,
					  int mac_id)
{
	return mac_id;
}

static u8 ath12k_wifi7_hw_get_ring_selector_wcn7850(struct sk_buff *skb)
{
	return skb_get_queue_mapping(skb);
}

static bool ath12k_wifi7_dp_srng_is_comp_ring_wcn7850(int ring_num)
{
	if (ring_num == 0 || ring_num == 2 || ring_num == 4)
		return true;

	return false;
}

static bool ath12k_is_addba_resp_action_code(struct ieee80211_mgmt *mgmt)
{
	if (!ieee80211_is_action(mgmt->frame_control))
		return false;

	if (mgmt->u.action.category != WLAN_CATEGORY_BACK)
		return false;

	if (mgmt->u.action.u.addba_resp.action_code != WLAN_ACTION_ADDBA_RESP)
		return false;

	return true;
}

static bool
ath12k_wifi7_is_frame_link_agnostic_wcn7850(struct ath12k_link_vif *arvif,
					    struct ieee80211_mgmt *mgmt)
{
	struct ieee80211_vif *vif = ath12k_ahvif_to_vif(arvif->ahvif);
	struct ath12k_hw *ah = ath12k_ar_to_ah(arvif->ar);
	struct ath12k_base *ab = arvif->ar->ab;
	struct ath12k_dp *dp = ath12k_ab_to_dp(ab);
	struct ath12k_dp_peer *peer;
	__le16 fc = mgmt->frame_control;

	spin_lock_bh(&dp->dp_lock);
	if (!ath12k_dp_link_peer_find_by_addr(dp, mgmt->da)) {
		spin_lock_bh(&ah->dp_hw.peer_lock);
		peer = ath12k_dp_peer_find_by_addr(&ah->dp_hw, mgmt->da);
		if (!peer || (peer && !peer->is_mlo)) {
			spin_unlock_bh(&ah->dp_hw.peer_lock);
			spin_unlock_bh(&dp->dp_lock);
			return false;
		}
		spin_unlock_bh(&ah->dp_hw.peer_lock);
	}
	spin_unlock_bh(&dp->dp_lock);

	if (vif->type == NL80211_IFTYPE_STATION)
		return arvif->is_up &&
		       (vif->valid_links == vif->active_links) &&
		       !ieee80211_is_probe_req(fc) &&
		       !ieee80211_is_auth(fc) &&
		       !ieee80211_is_deauth(fc) &&
		       !ath12k_is_addba_resp_action_code(mgmt);

	if (vif->type == NL80211_IFTYPE_AP)
		return !(ieee80211_is_probe_resp(fc) || ieee80211_is_auth(fc) ||
			 ieee80211_is_assoc_resp(fc) || ieee80211_is_reassoc_resp(fc) ||
			 ath12k_is_addba_resp_action_code(mgmt));

	return false;
}

static const struct ath12k_hw_ops qcn9274_ops = {
	.get_hw_mac_from_pdev_id = ath12k_wifi7_hw_qcn9274_mac_from_pdev_id,
	.mac_id_to_pdev_id = ath12k_wifi7_hw_mac_id_to_pdev_id_qcn9274,
	.mac_id_to_srng_id = ath12k_wifi7_hw_mac_id_to_srng_id_qcn9274,
	.rxdma_ring_sel_config = ath12k_dp_rxdma_ring_sel_config_qcn9274,
	.get_ring_selector = ath12k_wifi7_hw_get_ring_selector_qcn9274,
	.dp_srng_is_tx_comp_ring = ath12k_wifi7_dp_srng_is_comp_ring_qcn9274,
	.is_frame_link_agnostic = ath12k_wifi7_is_frame_link_agnostic_qcn9274,
};

static const struct ath12k_hw_ops wcn7850_ops = {
	.get_hw_mac_from_pdev_id = ath12k_wifi7_hw_qcn9274_mac_from_pdev_id,
	.mac_id_to_pdev_id = ath12k_wifi7_hw_mac_id_to_pdev_id_wcn7850,
	.mac_id_to_srng_id = ath12k_wifi7_hw_mac_id_to_srng_id_wcn7850,
	.rxdma_ring_sel_config = ath12k_dp_rxdma_ring_sel_config_wcn7850,
	.get_ring_selector = ath12k_wifi7_hw_get_ring_selector_wcn7850,
	.dp_srng_is_tx_comp_ring = ath12k_wifi7_dp_srng_is_comp_ring_wcn7850,
	.is_frame_link_agnostic = ath12k_wifi7_is_frame_link_agnostic_wcn7850,
};

static const struct ath12k_hw_ops qcc2072_ops = {
	.get_hw_mac_from_pdev_id = ath12k_wifi7_hw_qcn9274_mac_from_pdev_id,
	.mac_id_to_pdev_id = ath12k_wifi7_hw_mac_id_to_pdev_id_wcn7850,
	.mac_id_to_srng_id = ath12k_wifi7_hw_mac_id_to_srng_id_wcn7850,
	.rxdma_ring_sel_config = ath12k_dp_rxdma_ring_sel_config_qcc2072,
	.get_ring_selector = ath12k_wifi7_hw_get_ring_selector_wcn7850,
	.dp_srng_is_tx_comp_ring = ath12k_wifi7_dp_srng_is_comp_ring_wcn7850,
	.is_frame_link_agnostic = ath12k_wifi7_is_frame_link_agnostic_wcn7850,
};

#define ATH12K_TX_RING_MASK_0 0x1
#define ATH12K_TX_RING_MASK_1 0x2
#define ATH12K_TX_RING_MASK_2 0x4
#define ATH12K_TX_RING_MASK_3 0x8
#define ATH12K_TX_RING_MASK_4 0x10

#define ATH12K_RX_RING_MASK_0 0x1
#define ATH12K_RX_RING_MASK_1 0x2
#define ATH12K_RX_RING_MASK_2 0x4
#define ATH12K_RX_RING_MASK_3 0x8

#define ATH12K_RX_ERR_RING_MASK_0 0x1

#define ATH12K_RX_WBM_REL_RING_MASK_0 0x1

#define ATH12K_REO_STATUS_RING_MASK_0 0x1

#define ATH12K_HOST2RXDMA_RING_MASK_0 0x1

#define ATH12K_RX_MON_RING_MASK_0 0x1
#define ATH12K_RX_MON_RING_MASK_1 0x2
#define ATH12K_RX_MON_RING_MASK_2 0x4

#define ATH12K_TX_MON_RING_MASK_0 0x1
#define ATH12K_TX_MON_RING_MASK_1 0x2

#define ATH12K_RX_MON_STATUS_RING_MASK_0 0x1
#define ATH12K_RX_MON_STATUS_RING_MASK_1 0x2
#define ATH12K_RX_MON_STATUS_RING_MASK_2 0x4

static const struct ath12k_hw_ring_mask ath12k_wifi7_hw_ring_mask_qcn9274 = {
	.tx  = {
		ATH12K_TX_RING_MASK_0,
		ATH12K_TX_RING_MASK_1,
		ATH12K_TX_RING_MASK_2,
		ATH12K_TX_RING_MASK_3,
	},
	.rx_mon_dest = {
		0, 0, 0, 0,
		0, 0, 0, 0,
		ATH12K_RX_MON_RING_MASK_0,
		ATH12K_RX_MON_RING_MASK_1,
		ATH12K_RX_MON_RING_MASK_2,
	},
	.rx = {
		0, 0, 0, 0,
		ATH12K_RX_RING_MASK_0,
		ATH12K_RX_RING_MASK_1,
		ATH12K_RX_RING_MASK_2,
		ATH12K_RX_RING_MASK_3,
	},
	.rx_err = {
		0, 0, 0,
		ATH12K_RX_ERR_RING_MASK_0,
	},
	.rx_wbm_rel = {
		0, 0, 0,
		ATH12K_RX_WBM_REL_RING_MASK_0,
	},
	.reo_status = {
		0, 0, 0,
		ATH12K_REO_STATUS_RING_MASK_0,
	},
	.host2rxdma = {
		0, 0, 0,
		ATH12K_HOST2RXDMA_RING_MASK_0,
	},
	.tx_mon_dest = {
		0, 0, 0,
	},
};

static const struct ath12k_hw_ring_mask ath12k_wifi7_hw_ring_mask_ipq5332 = {
	.tx  = {
		ATH12K_TX_RING_MASK_0,
		ATH12K_TX_RING_MASK_1,
		ATH12K_TX_RING_MASK_2,
		ATH12K_TX_RING_MASK_3,
	},
	.rx_mon_dest = {
		0, 0, 0, 0, 0, 0, 0, 0,
		ATH12K_RX_MON_RING_MASK_0,
	},
	.rx = {
		0, 0, 0, 0,
		ATH12K_RX_RING_MASK_0,
		ATH12K_RX_RING_MASK_1,
		ATH12K_RX_RING_MASK_2,
		ATH12K_RX_RING_MASK_3,
	},
	.rx_err = {
		0, 0, 0,
		ATH12K_RX_ERR_RING_MASK_0,
	},
	.rx_wbm_rel = {
		0, 0, 0,
		ATH12K_RX_WBM_REL_RING_MASK_0,
	},
	.reo_status = {
		0, 0, 0,
		ATH12K_REO_STATUS_RING_MASK_0,
	},
	.host2rxdma = {
		0, 0, 0,
		ATH12K_HOST2RXDMA_RING_MASK_0,
	},
	.tx_mon_dest = {
		ATH12K_TX_MON_RING_MASK_0,
		ATH12K_TX_MON_RING_MASK_1,
	},
};

static const struct ath12k_hw_ring_mask ath12k_wifi7_hw_ring_mask_wcn7850 = {
	.tx  = {
		ATH12K_TX_RING_MASK_0,
		ATH12K_TX_RING_MASK_1,
		ATH12K_TX_RING_MASK_2,
	},
	.rx_mon_dest = {
	},
	.rx_mon_status = {
		0, 0, 0, 0,
		ATH12K_RX_MON_STATUS_RING_MASK_0,
		ATH12K_RX_MON_STATUS_RING_MASK_1,
		ATH12K_RX_MON_STATUS_RING_MASK_2,
	},
	.rx = {
		0, 0, 0,
		ATH12K_RX_RING_MASK_0,
		ATH12K_RX_RING_MASK_1,
		ATH12K_RX_RING_MASK_2,
		ATH12K_RX_RING_MASK_3,
	},
	.rx_err = {
		ATH12K_RX_ERR_RING_MASK_0,
	},
	.rx_wbm_rel = {
		ATH12K_RX_WBM_REL_RING_MASK_0,
	},
	.reo_status = {
		ATH12K_REO_STATUS_RING_MASK_0,
	},
	.host2rxdma = {
	},
	.tx_mon_dest = {
	},
};

static const struct ce_ie_addr ath12k_wifi7_ce_ie_addr_ipq5332 = {
	.ie1_reg_addr = CE_HOST_IE_ADDRESS - HAL_IPQ5332_CE_WFSS_REG_BASE,
	.ie2_reg_addr = CE_HOST_IE_2_ADDRESS - HAL_IPQ5332_CE_WFSS_REG_BASE,
	.ie3_reg_addr = CE_HOST_IE_3_ADDRESS - HAL_IPQ5332_CE_WFSS_REG_BASE,
};

static const struct ce_remap ath12k_wifi7_ce_remap_ipq5332 = {
	.base = HAL_IPQ5332_CE_WFSS_REG_BASE,
	.size = HAL_IPQ5332_CE_SIZE,
	.cmem_offset = HAL_SEQ_WCSS_CMEM_OFFSET,
};

static const struct ath12k_hw_params ath12k_wifi7_hw_params[] = {
	{
		.name = "qcn9274 hw1.0",
		.hw_rev = ATH12K_HW_QCN9274_HW10,
		.fw = {
			.dir = "QCN9274/hw1.0",
			.board_size = 256 * 1024,
			.cal_offset = 128 * 1024,
			.m3_loader = ath12k_m3_fw_loader_driver,
			.download_aux_ucode = false,
		},
		.max_radios = 1,
		.single_pdev_only = false,
		.qmi_service_ins_id = ATH12K_QMI_WLFW_SERVICE_INS_ID_V01_QCN9274,
		.internal_sleep_clock = false,

		.hw_ops = &qcn9274_ops,
		.ring_mask = &ath12k_wifi7_hw_ring_mask_qcn9274,

		.host_ce_config = ath12k_wifi7_host_ce_config_qcn9274,
		.ce_count = 16,
		.target_ce_config = ath12k_wifi7_target_ce_config_wlan_qcn9274,
		.target_ce_count = 12,
		.svc_to_ce_map =
			ath12k_wifi7_target_service_to_ce_map_wlan_qcn9274,
		.svc_to_ce_map_len = 18,

		.rxdma1_enable = false,
		.num_rxdma_per_pdev = 1,
		.num_rxdma_dst_ring = 0,
		.rx_mac_buf_ring = false,
		.vdev_start_delay = false,

		.interface_modes = BIT(NL80211_IFTYPE_STATION) |
					BIT(NL80211_IFTYPE_AP) |
					BIT(NL80211_IFTYPE_MESH_POINT) |
					BIT(NL80211_IFTYPE_AP_VLAN),
		.supports_monitor = false,

		.idle_ps = false,
		.download_calib = true,
		.supports_suspend = false,
		.tcl_ring_retry = true,
		.reoq_lut_support = true,
		.supports_shadow_regs = false,

		.num_tcl_banks = 48,
		.max_tx_ring = 4,

		.mhi_config = &ath12k_wifi7_mhi_config_qcn9274,

		.wmi_init = ath12k_wifi7_wmi_init_qcn9274,

		.qmi_cnss_feature_bitmap = BIT(CNSS_QDSS_CFG_MISS_V01),

		.rfkill_pin = 0,
		.rfkill_cfg = 0,
		.rfkill_on_level = 0,

		.rddm_size = 0x600000,

		.def_num_link = 0,
		.max_mlo_peer = 256,

		.otp_board_id_register = QCN9274_QFPROM_RAW_RFA_PDET_ROW13_LSB,

		.supports_sta_ps = false,

		.acpi_guid = NULL,
		.supports_dynamic_smps_6ghz = true,

		.iova_mask = 0,

		.supports_aspm = false,

		.ce_ie_addr = NULL,
		.ce_remap = NULL,
		.bdf_addr_offset = 0,

		.current_cc_support = false,

		.dp_primary_link_only = true,
	},
	{
		.name = "wcn7850 hw2.0",
		.hw_rev = ATH12K_HW_WCN7850_HW20,

		.fw = {
			.dir = "WCN7850/hw2.0",
			.board_size = 256 * 1024,
			.cal_offset = 256 * 1024,
			.m3_loader = ath12k_m3_fw_loader_driver,
			.download_aux_ucode = false,
		},

		.max_radios = 1,
		.single_pdev_only = true,
		.qmi_service_ins_id = ATH12K_QMI_WLFW_SERVICE_INS_ID_V01_WCN7850,
		.internal_sleep_clock = true,

		.hw_ops = &wcn7850_ops,
		.ring_mask = &ath12k_wifi7_hw_ring_mask_wcn7850,

		.host_ce_config = ath12k_wifi7_host_ce_config_wcn7850,
		.ce_count = 9,
		.target_ce_config = ath12k_wifi7_target_ce_config_wlan_wcn7850,
		.target_ce_count = 9,
		.svc_to_ce_map =
			ath12k_wifi7_target_service_to_ce_map_wlan_wcn7850,
		.svc_to_ce_map_len = 14,

		.rxdma1_enable = false,
		.num_rxdma_per_pdev = 2,
		.num_rxdma_dst_ring = 1,
		.rx_mac_buf_ring = true,
		.vdev_start_delay = true,

		.interface_modes = BIT(NL80211_IFTYPE_STATION) |
				   BIT(NL80211_IFTYPE_AP) |
				   BIT(NL80211_IFTYPE_P2P_DEVICE) |
				   BIT(NL80211_IFTYPE_P2P_CLIENT) |
				   BIT(NL80211_IFTYPE_P2P_GO),
		.supports_monitor = true,

		.idle_ps = true,
		.download_calib = false,
		.supports_suspend = true,
		.tcl_ring_retry = false,
		.reoq_lut_support = false,
		.supports_shadow_regs = true,

		.num_tcl_banks = 7,
		.max_tx_ring = 3,

		.mhi_config = &ath12k_wifi7_mhi_config_wcn7850,

		.wmi_init = ath12k_wifi7_wmi_init_wcn7850,

		.qmi_cnss_feature_bitmap = BIT(CNSS_QDSS_CFG_MISS_V01) |
					   BIT(CNSS_PCIE_PERST_NO_PULL_V01),

		.rfkill_pin = 48,
		.rfkill_cfg = 0,
		.rfkill_on_level = 1,

		.rddm_size = 0x780000,

		.def_num_link = 2,
		.max_mlo_peer = 32,

		.otp_board_id_register = 0,

		.supports_sta_ps = true,

		.acpi_guid = &wcn7850_uuid,
		.supports_dynamic_smps_6ghz = false,

		.iova_mask = ATH12K_PCIE_MAX_PAYLOAD_SIZE - 1,

		.supports_aspm = true,

		.ce_ie_addr = NULL,
		.ce_remap = NULL,
		.bdf_addr_offset = 0,

		.current_cc_support = true,

		.dp_primary_link_only = false,
	},
	{
		.name = "qcn9274 hw2.0",
		.hw_rev = ATH12K_HW_QCN9274_HW20,
		.fw = {
			.dir = "QCN9274/hw2.0",
			.board_size = 256 * 1024,
			.cal_offset = 128 * 1024,
			.m3_loader = ath12k_m3_fw_loader_driver,
			.download_aux_ucode = false,
		},
		.max_radios = 2,
		.single_pdev_only = false,
		.qmi_service_ins_id = ATH12K_QMI_WLFW_SERVICE_INS_ID_V01_QCN9274,
		.internal_sleep_clock = false,

		.hw_ops = &qcn9274_ops,
		.ring_mask = &ath12k_wifi7_hw_ring_mask_qcn9274,

		.host_ce_config = ath12k_wifi7_host_ce_config_qcn9274,
		.ce_count = 16,
		.target_ce_config = ath12k_wifi7_target_ce_config_wlan_qcn9274,
		.target_ce_count = 12,
		.svc_to_ce_map =
			ath12k_wifi7_target_service_to_ce_map_wlan_qcn9274,
		.svc_to_ce_map_len = 18,

		.rxdma1_enable = true,
		.num_rxdma_per_pdev = 1,
		.num_rxdma_dst_ring = 0,
		.rx_mac_buf_ring = false,
		.vdev_start_delay = false,

		.interface_modes = BIT(NL80211_IFTYPE_STATION) |
					BIT(NL80211_IFTYPE_AP) |
					BIT(NL80211_IFTYPE_MESH_POINT) |
					BIT(NL80211_IFTYPE_AP_VLAN),
		.supports_monitor = true,

		.idle_ps = false,
		.download_calib = true,
		.supports_suspend = false,
		.tcl_ring_retry = true,
		.reoq_lut_support = true,
		.supports_shadow_regs = false,

		.num_tcl_banks = 48,
		.max_tx_ring = 4,

		.mhi_config = &ath12k_wifi7_mhi_config_qcn9274,

		.wmi_init = ath12k_wifi7_wmi_init_qcn9274,

		.qmi_cnss_feature_bitmap = BIT(CNSS_QDSS_CFG_MISS_V01),

		.rfkill_pin = 0,
		.rfkill_cfg = 0,
		.rfkill_on_level = 0,

		.rddm_size = 0x600000,

		.def_num_link = 0,
		.max_mlo_peer = 256,

		.otp_board_id_register = QCN9274_QFPROM_RAW_RFA_PDET_ROW13_LSB,

		.supports_sta_ps = false,

		.acpi_guid = NULL,
		.supports_dynamic_smps_6ghz = true,

		.iova_mask = 0,

		.supports_aspm = false,

		.ce_ie_addr = NULL,
		.ce_remap = NULL,
		.bdf_addr_offset = 0,

		.current_cc_support = false,

		.dp_primary_link_only = true,
	},
	{
		.name = "ipq5332 hw1.0",
		.hw_rev = ATH12K_HW_IPQ5332_HW10,
		.fw = {
			.dir = "IPQ5332/hw1.0",
			.board_size = 256 * 1024,
			.cal_offset = 128 * 1024,
			.m3_loader = ath12k_m3_fw_loader_remoteproc,
			.download_aux_ucode = false,
		},
		.max_radios = 1,
		.single_pdev_only = false,
		.qmi_service_ins_id = ATH12K_QMI_WLFW_SERVICE_INS_ID_V01_IPQ5332,
		.internal_sleep_clock = false,

		.hw_ops = &qcn9274_ops,
		.ring_mask = &ath12k_wifi7_hw_ring_mask_ipq5332,

		.host_ce_config = ath12k_wifi7_host_ce_config_ipq5332,
		.ce_count = 12,
		.target_ce_config = ath12k_wifi7_target_ce_config_wlan_ipq5332,
		.target_ce_count = 12,
		.svc_to_ce_map =
			ath12k_wifi7_target_service_to_ce_map_wlan_ipq5332,
		.svc_to_ce_map_len = 18,

		.rxdma1_enable = false,
		.num_rxdma_per_pdev = 1,
		.num_rxdma_dst_ring = 0,
		.rx_mac_buf_ring = false,
		.vdev_start_delay = false,

		.interface_modes = BIT(NL80211_IFTYPE_STATION) |
				   BIT(NL80211_IFTYPE_AP) |
				   BIT(NL80211_IFTYPE_MESH_POINT),
		.supports_monitor = false,

		.idle_ps = false,
		.download_calib = true,
		.supports_suspend = false,
		.tcl_ring_retry = true,
		.reoq_lut_support = false,
		.supports_shadow_regs = false,

		.num_tcl_banks = 48,
		.max_tx_ring = 4,

		.wmi_init = &ath12k_wifi7_wmi_init_qcn9274,

		.qmi_cnss_feature_bitmap = BIT(CNSS_QDSS_CFG_MISS_V01),

		.rfkill_pin = 0,
		.rfkill_cfg = 0,
		.rfkill_on_level = 0,

		.rddm_size = 0,

		.def_num_link = 0,
		.max_mlo_peer = 256,

		.otp_board_id_register = 0,

		.supports_sta_ps = false,

		.acpi_guid = NULL,
		.supports_dynamic_smps_6ghz = false,
		.iova_mask = 0,
		.supports_aspm = false,

		.ce_ie_addr = &ath12k_wifi7_ce_ie_addr_ipq5332,
		.ce_remap = &ath12k_wifi7_ce_remap_ipq5332,
		.bdf_addr_offset = 0xC00000,

		.dp_primary_link_only = true,
	},
	{
		.name = "qcc2072 hw1.0",
		.hw_rev = ATH12K_HW_QCC2072_HW10,

		.fw = {
			.dir = "QCC2072/hw1.0",
			.board_size = 256 * 1024,
			.cal_offset = 256 * 1024,
			.m3_loader = ath12k_m3_fw_loader_driver,
			.download_aux_ucode = true,
		},

		.max_radios = 1,
		.single_pdev_only = true,
		.qmi_service_ins_id = ATH12K_QMI_WLFW_SERVICE_INS_ID_V01_WCN7850,
		.internal_sleep_clock = true,

		.hw_ops = &qcc2072_ops,
		.ring_mask = &ath12k_wifi7_hw_ring_mask_wcn7850,

		.host_ce_config = ath12k_wifi7_host_ce_config_wcn7850,
		.ce_count = 9,
		.target_ce_config = ath12k_wifi7_target_ce_config_wlan_wcn7850,
		.target_ce_count = 9,
		.svc_to_ce_map =
			ath12k_wifi7_target_service_to_ce_map_wlan_wcn7850,
		.svc_to_ce_map_len = 14,

		.rxdma1_enable = false,
		.num_rxdma_per_pdev = 2,
		.num_rxdma_dst_ring = 1,
		.rx_mac_buf_ring = true,
		.vdev_start_delay = true,

		.interface_modes = BIT(NL80211_IFTYPE_STATION) |
				   BIT(NL80211_IFTYPE_AP) |
				   BIT(NL80211_IFTYPE_P2P_DEVICE) |
				   BIT(NL80211_IFTYPE_P2P_CLIENT) |
				   BIT(NL80211_IFTYPE_P2P_GO),
		.supports_monitor = true,

		.idle_ps = true,
		.download_calib = false,
		.supports_suspend = true,
		.tcl_ring_retry = false,
		.reoq_lut_support = false,
		.supports_shadow_regs = true,

		.num_tcl_banks = 7,
		.max_tx_ring = 3,

		.mhi_config = &ath12k_wifi7_mhi_config_wcn7850,

		.wmi_init = ath12k_wifi7_wmi_init_wcn7850,

		.qmi_cnss_feature_bitmap = BIT(CNSS_QDSS_CFG_MISS_V01) |
					   BIT(CNSS_PCIE_PERST_NO_PULL_V01) |
					   BIT(CNSS_AUX_UC_SUPPORT_V01),

		.rfkill_pin = 0,
		.rfkill_cfg = 0,
		.rfkill_on_level = 0,

		.rddm_size = 0x780000,

		.def_num_link = 2,
		.max_mlo_peer = 32,

		.otp_board_id_register = 0,

		.supports_sta_ps = true,

		.acpi_guid = &wcn7850_uuid,
		.supports_dynamic_smps_6ghz = false,

		.iova_mask = 0,

		.supports_aspm = true,

		.ce_ie_addr = NULL,
		.ce_remap = NULL,
		.bdf_addr_offset = 0,

		.current_cc_support = true,

		.dp_primary_link_only = false,
	},
};

/* Note: called under rcu_read_lock() */
static void ath12k_wifi7_mac_op_tx(struct ieee80211_hw *hw,
				   struct ieee80211_tx_control *control,
				   struct sk_buff *skb)
{
	struct ath12k_skb_cb *skb_cb = ATH12K_SKB_CB(skb);
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_vif *vif = info->control.vif;
	struct ath12k_vif *ahvif = ath12k_vif_to_ahvif(vif);
	struct ath12k_link_vif *arvif = &ahvif->deflink;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	struct ieee80211_key_conf *key = info->control.hw_key;
	struct ieee80211_sta *sta = control->sta;
	struct ath12k_link_vif *tmp_arvif;
	u32 info_flags = info->flags;
	struct sk_buff *msdu_copied;
	struct ath12k *ar, *tmp_ar;
	struct ath12k_pdev_dp *dp_pdev, *tmp_dp_pdev;
	struct ath12k_dp_link_peer *peer;
	unsigned long links_map;
	bool is_mcast = false;
	bool is_dvlan = false;
	struct ethhdr *eth;
	bool is_prb_rsp;
	u16 mcbc_gsn;
	u8 link_id;
	int ret;
	struct ath12k_dp *tmp_dp;

	if (ahvif->vdev_type == WMI_VDEV_TYPE_MONITOR) {
		ieee80211_free_txskb(hw, skb);
		return;
	}

	link_id = u32_get_bits(info->control.flags, IEEE80211_TX_CTRL_MLO_LINK);
	memset(skb_cb, 0, sizeof(*skb_cb));
	skb_cb->vif = vif;

	if (key) {
		skb_cb->cipher = key->cipher;
		skb_cb->flags |= ATH12K_SKB_CIPHER_SET;
	}

	/* handle only for MLO case, use deflink for non MLO case */
	if (ieee80211_vif_is_mld(vif)) {
		link_id = ath12k_mac_get_tx_link(sta, vif, link_id, skb, info_flags);
		if (link_id >= IEEE80211_MLD_MAX_NUM_LINKS) {
			ieee80211_free_txskb(hw, skb);
			return;
		}
	} else {
		if (vif->type == NL80211_IFTYPE_P2P_DEVICE)
			link_id = ATH12K_FIRST_SCAN_LINK;
		else
			link_id = 0;
	}

	arvif = rcu_dereference(ahvif->link[link_id]);
	if (!arvif || !arvif->ar) {
		ath12k_warn(ahvif->ah, "failed to find arvif link id %u for frame transmission",
			    link_id);
		ieee80211_free_txskb(hw, skb);
		return;
	}

	ar = arvif->ar;
	skb_cb->link_id = link_id;
	/*
	 * as skb_cb is common currently for dp and mgmt tx processing
	 * set this in the common mac op tx function.
	 */
	skb_cb->ar = ar;
	is_prb_rsp = ieee80211_is_probe_resp(hdr->frame_control);

	if (info_flags & IEEE80211_TX_CTL_HW_80211_ENCAP) {
		eth = (struct ethhdr *)skb->data;
		is_mcast = is_multicast_ether_addr(eth->h_dest);

		skb_cb->flags |= ATH12K_SKB_HW_80211_ENCAP;
	} else if (ieee80211_is_mgmt(hdr->frame_control)) {
		if (sta && sta->mlo)
			skb_cb->flags |= ATH12K_SKB_MLO_STA;

		ret = ath12k_mac_mgmt_tx(ar, skb, is_prb_rsp);
		if (ret) {
			ath12k_warn(ar->ab, "failed to queue management frame %d\n",
				    ret);
			ieee80211_free_txskb(hw, skb);
		}
		return;
	}

	if (!(info_flags & IEEE80211_TX_CTL_HW_80211_ENCAP))
		is_mcast = is_multicast_ether_addr(hdr->addr1);

	/* This is case only for P2P_GO */
	if (vif->type == NL80211_IFTYPE_AP && vif->p2p)
		ath12k_mac_add_p2p_noa_ie(ar, vif, skb, is_prb_rsp);

	dp_pdev = ath12k_dp_to_pdev_dp(ar->ab->dp, ar->pdev_idx);
	if (!dp_pdev) {
		ieee80211_free_txskb(hw, skb);
		return;
	}

	/* Checking if it is a DVLAN frame */
	if (!test_bit(ATH12K_FLAG_HW_CRYPTO_DISABLED, &ar->ab->dev_flags) &&
	    !(skb_cb->flags & ATH12K_SKB_HW_80211_ENCAP) &&
	    !(skb_cb->flags & ATH12K_SKB_CIPHER_SET) &&
	    ieee80211_has_protected(hdr->frame_control))
		is_dvlan = true;

	if (!vif->valid_links || !is_mcast || is_dvlan ||
	    (skb_cb->flags & ATH12K_SKB_HW_80211_ENCAP) ||
	    test_bit(ATH12K_FLAG_RAW_MODE, &ar->ab->dev_flags)) {
		ret = ath12k_wifi7_dp_tx(dp_pdev, arvif, skb, false, 0, is_mcast);
		if (unlikely(ret)) {
			ath12k_warn(ar->ab, "failed to transmit frame %d\n", ret);
			ieee80211_free_txskb(ar->ah->hw, skb);
			return;
		}
	} else {
		mcbc_gsn = atomic_inc_return(&ahvif->dp_vif.mcbc_gsn) & 0xfff;

		links_map = ahvif->links_map;
		for_each_set_bit(link_id, &links_map,
				 IEEE80211_MLD_MAX_NUM_LINKS) {
			tmp_arvif = rcu_dereference(ahvif->link[link_id]);
			if (!tmp_arvif || !tmp_arvif->is_up)
				continue;

			tmp_ar = tmp_arvif->ar;
			tmp_dp_pdev = ath12k_dp_to_pdev_dp(tmp_ar->ab->dp,
							   tmp_ar->pdev_idx);
			if (!tmp_dp_pdev)
				continue;
			msdu_copied = skb_copy(skb, GFP_ATOMIC);
			if (!msdu_copied) {
				ath12k_err(ar->ab,
					   "skb copy failure link_id 0x%X vdevid 0x%X\n",
					   link_id, tmp_arvif->vdev_id);
				continue;
			}

			ath12k_mlo_mcast_update_tx_link_address(vif, link_id,
								msdu_copied,
								info_flags);

			skb_cb = ATH12K_SKB_CB(msdu_copied);
			skb_cb->link_id = link_id;
			skb_cb->vif = vif;
			skb_cb->ar = tmp_ar;

			/* For open mode, skip peer find logic */
			if (unlikely(!ahvif->dp_vif.key_cipher))
				goto skip_peer_find;

			tmp_dp = ath12k_ab_to_dp(tmp_ar->ab);
			spin_lock_bh(&tmp_dp->dp_lock);
			peer = ath12k_dp_link_peer_find_by_addr(tmp_dp,
								tmp_arvif->bssid);
			if (!peer || !peer->dp_peer) {
				spin_unlock_bh(&tmp_dp->dp_lock);
				ath12k_warn(tmp_ar->ab,
					    "failed to find peer for vdev_id 0x%X addr %pM link_map 0x%X\n",
					    tmp_arvif->vdev_id, tmp_arvif->bssid,
					    ahvif->links_map);
				dev_kfree_skb_any(msdu_copied);
				continue;
			}

			key = peer->dp_peer->keys[peer->dp_peer->mcast_keyidx];
			if (key) {
				skb_cb->cipher = key->cipher;
				skb_cb->flags |= ATH12K_SKB_CIPHER_SET;

				hdr = (struct ieee80211_hdr *)msdu_copied->data;
				if (!ieee80211_has_protected(hdr->frame_control))
					hdr->frame_control |=
						cpu_to_le16(IEEE80211_FCTL_PROTECTED);
			}
			spin_unlock_bh(&tmp_dp->dp_lock);

skip_peer_find:
			ret = ath12k_wifi7_dp_tx(tmp_dp_pdev, tmp_arvif,
						 msdu_copied, true, mcbc_gsn, is_mcast);
			if (unlikely(ret)) {
				if (ret == -ENOMEM) {
					/* Drops are expected during heavy multicast
					 * frame flood. Print with debug log
					 * level to avoid lot of console prints
					 */
					ath12k_dbg(ar->ab, ATH12K_DBG_MAC,
						   "failed to transmit frame %d\n",
						   ret);
				} else {
					ath12k_warn(ar->ab,
						    "failed to transmit frame %d\n",
						    ret);
				}

				dev_kfree_skb_any(msdu_copied);
			}
		}
		ieee80211_free_txskb(ar->ah->hw, skb);
	}
}

static const struct ieee80211_ops ath12k_ops_wifi7 = {
	.tx				= ath12k_wifi7_mac_op_tx,
	.wake_tx_queue			= ieee80211_handle_wake_tx_queue,
	.start                          = ath12k_mac_op_start,
	.stop                           = ath12k_mac_op_stop,
	.reconfig_complete              = ath12k_mac_op_reconfig_complete,
	.add_interface                  = ath12k_mac_op_add_interface,
	.remove_interface		= ath12k_mac_op_remove_interface,
	.update_vif_offload		= ath12k_mac_op_update_vif_offload,
	.config                         = ath12k_mac_op_config,
	.link_info_changed              = ath12k_mac_op_link_info_changed,
	.vif_cfg_changed		= ath12k_mac_op_vif_cfg_changed,
	.change_vif_links               = ath12k_mac_op_change_vif_links,
	.configure_filter		= ath12k_mac_op_configure_filter,
	.hw_scan                        = ath12k_mac_op_hw_scan,
	.cancel_hw_scan                 = ath12k_mac_op_cancel_hw_scan,
	.set_key                        = ath12k_mac_op_set_key,
	.set_rekey_data	                = ath12k_mac_op_set_rekey_data,
	.sta_state                      = ath12k_mac_op_sta_state,
	.sta_set_txpwr			= ath12k_mac_op_sta_set_txpwr,
	.link_sta_rc_update		= ath12k_mac_op_link_sta_rc_update,
	.conf_tx                        = ath12k_mac_op_conf_tx,
	.set_antenna			= ath12k_mac_op_set_antenna,
	.get_antenna			= ath12k_mac_op_get_antenna,
	.ampdu_action			= ath12k_mac_op_ampdu_action,
	.add_chanctx			= ath12k_mac_op_add_chanctx,
	.remove_chanctx			= ath12k_mac_op_remove_chanctx,
	.change_chanctx			= ath12k_mac_op_change_chanctx,
	.assign_vif_chanctx		= ath12k_mac_op_assign_vif_chanctx,
	.unassign_vif_chanctx		= ath12k_mac_op_unassign_vif_chanctx,
	.switch_vif_chanctx		= ath12k_mac_op_switch_vif_chanctx,
	.get_txpower			= ath12k_mac_op_get_txpower,
	.set_rts_threshold		= ath12k_mac_op_set_rts_threshold,
	.set_frag_threshold		= ath12k_mac_op_set_frag_threshold,
	.set_bitrate_mask		= ath12k_mac_op_set_bitrate_mask,
	.get_survey			= ath12k_mac_op_get_survey,
	.flush				= ath12k_mac_op_flush,
	.sta_statistics			= ath12k_mac_op_sta_statistics,
	.link_sta_statistics		= ath12k_mac_op_link_sta_statistics,
	.remain_on_channel              = ath12k_mac_op_remain_on_channel,
	.cancel_remain_on_channel       = ath12k_mac_op_cancel_remain_on_channel,
	.change_sta_links               = ath12k_mac_op_change_sta_links,
	.can_activate_links             = ath12k_mac_op_can_activate_links,
#ifdef CONFIG_PM
	.suspend			= ath12k_wow_op_suspend,
	.resume				= ath12k_wow_op_resume,
	.set_wakeup			= ath12k_wow_op_set_wakeup,
#endif
#ifdef CONFIG_ATH12K_DEBUGFS
	.vif_add_debugfs                = ath12k_debugfs_op_vif_add,
#endif
	CFG80211_TESTMODE_CMD(ath12k_tm_cmd)
#ifdef CONFIG_ATH12K_DEBUGFS
	.link_sta_add_debugfs           = ath12k_debugfs_link_sta_op_add,
#endif
};

int ath12k_wifi7_hw_init(struct ath12k_base *ab)
{
	const struct ath12k_hw_params *hw_params = NULL;
	int i;

	for (i = 0; i < ARRAY_SIZE(ath12k_wifi7_hw_params); i++) {
		hw_params = &ath12k_wifi7_hw_params[i];

		if (hw_params->hw_rev == ab->hw_rev)
			break;
	}

	if (i == ARRAY_SIZE(ath12k_wifi7_hw_params)) {
		ath12k_err(ab, "Unsupported Wi-Fi 7 hardware version: 0x%x\n",
			   ab->hw_rev);
		return -EINVAL;
	}

	ab->hw_params = hw_params;
	ab->ath12k_ops = &ath12k_ops_wifi7;

	ath12k_wifi7_hal_init(ab);

	ath12k_info(ab, "Wi-Fi 7 Hardware name: %s\n", ab->hw_params->name);

	return 0;
}
