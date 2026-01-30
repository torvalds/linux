/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) 2018-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef ATH12K_HW_H
#define ATH12K_HW_H

#include <linux/mhi.h>
#include <linux/uuid.h>

#include "wmi.h"
#include "hal.h"

/* Target configuration defines */

/* Num VDEVS per radio */
#define TARGET_NUM_VDEVS(ab)    ((ab)->profile_param->num_vdevs)

/* Max num of stations for Single Radio mode */
#define TARGET_NUM_STATIONS_SINGLE(ab) ((ab)->profile_param->max_client_single)

/* Max num of stations for DBS */
#define TARGET_NUM_STATIONS_DBS(ab)    ((ab)->profile_param->max_client_dbs)

/* Max num of stations for DBS_SBS */
#define TARGET_NUM_STATIONS_DBS_SBS(ab) \
	((ab)->profile_param->max_client_dbs_sbs)

#define TARGET_NUM_STATIONS(ab, x)     TARGET_NUM_STATIONS_##x(ab)

#define TARGET_NUM_PEER_KEYS	2

#define TARGET_AST_SKID_LIMIT	16
#define TARGET_NUM_OFFLD_PEERS	4
#define TARGET_NUM_OFFLD_REORDER_BUFFS 4

#define TARGET_TX_CHAIN_MASK	(BIT(0) | BIT(1) | BIT(2) | BIT(4))
#define TARGET_RX_CHAIN_MASK	(BIT(0) | BIT(1) | BIT(2) | BIT(4))
#define TARGET_RX_TIMEOUT_LO_PRI	100
#define TARGET_RX_TIMEOUT_HI_PRI	40

#define TARGET_DECAP_MODE_RAW		0
#define TARGET_DECAP_MODE_NATIVE_WIFI	1
#define TARGET_DECAP_MODE_ETH		2

#define TARGET_SCAN_MAX_PENDING_REQS	4
#define TARGET_BMISS_OFFLOAD_MAX_VDEV	3
#define TARGET_ROAM_OFFLOAD_MAX_VDEV	3
#define TARGET_ROAM_OFFLOAD_MAX_AP_PROFILES	8
#define TARGET_GTK_OFFLOAD_MAX_VDEV	3
#define TARGET_NUM_MCAST_GROUPS		12
#define TARGET_NUM_MCAST_TABLE_ELEMS	64
#define TARGET_MCAST2UCAST_MODE		2
#define TARGET_TX_DBG_LOG_SIZE		1024
#define TARGET_RX_SKIP_DEFRAG_TIMEOUT_DUP_DETECTION_CHECK 1
#define TARGET_VOW_CONFIG		0
#define TARGET_NUM_MSDU_DESC		(2500)
#define TARGET_MAX_FRAG_ENTRIES		6
#define TARGET_MAX_BCN_OFFLD		16
#define TARGET_NUM_WDS_ENTRIES		32
#define TARGET_DMA_BURST_SIZE		1
#define TARGET_RX_BATCHMODE		1
#define TARGET_EMA_MAX_PROFILE_PERIOD	8

#define ATH12K_HW_DEFAULT_QUEUE		0
#define ATH12K_HW_MAX_QUEUES		4
#define ATH12K_QUEUE_LEN		4096

#define ATH12K_HW_RATECODE_CCK_SHORT_PREAM_MASK  0x4

#define ATH12K_FW_DIR			"ath12k"

#define ATH12K_BOARD_MAGIC		"QCA-ATH12K-BOARD"
#define ATH12K_BOARD_API2_FILE		"board-2.bin"
#define ATH12K_DEFAULT_BOARD_FILE	"board.bin"
#define ATH12K_DEFAULT_CAL_FILE		"caldata.bin"
#define ATH12K_AMSS_FILE		"amss.bin"
#define ATH12K_M3_FILE			"m3.bin"
#define ATH12K_AUX_UC_FILE		"aux_ucode.bin"
#define ATH12K_REGDB_FILE_NAME		"regdb.bin"

#define ATH12K_PCIE_MAX_PAYLOAD_SIZE	128
#define ATH12K_IPQ5332_USERPD_ID	1

enum ath12k_hw_rate_cck {
	ATH12K_HW_RATE_CCK_LP_11M = 0,
	ATH12K_HW_RATE_CCK_LP_5_5M,
	ATH12K_HW_RATE_CCK_LP_2M,
	ATH12K_HW_RATE_CCK_LP_1M,
	ATH12K_HW_RATE_CCK_SP_11M,
	ATH12K_HW_RATE_CCK_SP_5_5M,
	ATH12K_HW_RATE_CCK_SP_2M,
};

enum ath12k_hw_rate_ofdm {
	ATH12K_HW_RATE_OFDM_48M = 0,
	ATH12K_HW_RATE_OFDM_24M,
	ATH12K_HW_RATE_OFDM_12M,
	ATH12K_HW_RATE_OFDM_6M,
	ATH12K_HW_RATE_OFDM_54M,
	ATH12K_HW_RATE_OFDM_36M,
	ATH12K_HW_RATE_OFDM_18M,
	ATH12K_HW_RATE_OFDM_9M,
};

enum ath12k_bus {
	ATH12K_BUS_PCI,
	ATH12K_BUS_AHB,
};

#define ATH12K_EXT_IRQ_GRP_NUM_MAX 11

struct hal_rx_desc;
struct hal_tcl_data_cmd;
struct htt_rx_ring_tlv_filter;
enum hal_encrypt_type;

struct ath12k_hw_ring_mask {
	u8 tx[ATH12K_EXT_IRQ_GRP_NUM_MAX];
	u8 rx_mon_dest[ATH12K_EXT_IRQ_GRP_NUM_MAX];
	u8 rx_mon_status[ATH12K_EXT_IRQ_GRP_NUM_MAX];
	u8 rx[ATH12K_EXT_IRQ_GRP_NUM_MAX];
	u8 rx_err[ATH12K_EXT_IRQ_GRP_NUM_MAX];
	u8 rx_wbm_rel[ATH12K_EXT_IRQ_GRP_NUM_MAX];
	u8 reo_status[ATH12K_EXT_IRQ_GRP_NUM_MAX];
	u8 host2rxdma[ATH12K_EXT_IRQ_GRP_NUM_MAX];
	u8 tx_mon_dest[ATH12K_EXT_IRQ_GRP_NUM_MAX];
};

enum ath12k_m3_fw_loaders {
	ath12k_m3_fw_loader_driver,
	ath12k_m3_fw_loader_remoteproc,
};

struct ath12k_hw_params {
	const char *name;
	u16 hw_rev;

	struct {
		const char *dir;
		size_t board_size;
		size_t cal_offset;
		enum ath12k_m3_fw_loaders m3_loader;
		bool download_aux_ucode:1;
	} fw;

	u8 max_radios;
	bool single_pdev_only:1;
	u32 qmi_service_ins_id;
	bool internal_sleep_clock:1;

	const struct ath12k_hw_ops *hw_ops;
	const struct ath12k_hw_ring_mask *ring_mask;

	const struct ce_attr *host_ce_config;
	u32 ce_count;
	const struct ce_pipe_config *target_ce_config;
	u32 target_ce_count;
	const struct service_to_pipe *svc_to_ce_map;
	u32 svc_to_ce_map_len;

	bool rxdma1_enable:1;
	int num_rxdma_per_pdev;
	int num_rxdma_dst_ring;
	bool rx_mac_buf_ring:1;
	bool vdev_start_delay:1;

	u16 interface_modes;
	bool supports_monitor:1;

	bool idle_ps:1;
	bool download_calib:1;
	bool supports_suspend:1;
	bool tcl_ring_retry:1;
	bool reoq_lut_support:1;
	bool supports_shadow_regs:1;
	bool supports_aspm:1;
	bool current_cc_support:1;

	u32 num_tcl_banks;
	u32 max_tx_ring;

	const struct mhi_controller_config *mhi_config;

	void (*wmi_init)(struct ath12k_base *ab,
			 struct ath12k_wmi_resource_config_arg *config);

	u64 qmi_cnss_feature_bitmap;

	u32 rfkill_pin;
	u32 rfkill_cfg;
	u32 rfkill_on_level;

	u32 rddm_size;

	u8 def_num_link;
	u16 max_mlo_peer;

	u32 otp_board_id_register;

	bool supports_sta_ps;

	const guid_t *acpi_guid;
	bool supports_dynamic_smps_6ghz;

	u32 iova_mask;

	const struct ce_ie_addr *ce_ie_addr;
	const struct ce_remap *ce_remap;
	u32 bdf_addr_offset;

	/* setup REO queue, frag etc only for primary link peer */
	bool dp_primary_link_only:1;
};

struct ath12k_hw_ops {
	u8 (*get_hw_mac_from_pdev_id)(int pdev_id);
	int (*mac_id_to_pdev_id)(const struct ath12k_hw_params *hw, int mac_id);
	int (*mac_id_to_srng_id)(const struct ath12k_hw_params *hw, int mac_id);
	int (*rxdma_ring_sel_config)(struct ath12k_base *ab);
	u8 (*get_ring_selector)(struct sk_buff *skb);
	bool (*dp_srng_is_tx_comp_ring)(int ring_num);
	bool (*is_frame_link_agnostic)(struct ath12k_link_vif *arvif,
				       struct ieee80211_mgmt *mgmt);
};

static inline
int ath12k_hw_get_mac_from_pdev_id(const struct ath12k_hw_params *hw,
				   int pdev_idx)
{
	if (hw->hw_ops->get_hw_mac_from_pdev_id)
		return hw->hw_ops->get_hw_mac_from_pdev_id(pdev_idx);

	return 0;
}

static inline int ath12k_hw_mac_id_to_pdev_id(const struct ath12k_hw_params *hw,
					      int mac_id)
{
	if (hw->hw_ops->mac_id_to_pdev_id)
		return hw->hw_ops->mac_id_to_pdev_id(hw, mac_id);

	return 0;
}

static inline int ath12k_hw_mac_id_to_srng_id(const struct ath12k_hw_params *hw,
					      int mac_id)
{
	if (hw->hw_ops->mac_id_to_srng_id)
		return hw->hw_ops->mac_id_to_srng_id(hw, mac_id);

	return 0;
}

struct ath12k_fw_ie {
	__le32 id;
	__le32 len;
	u8 data[];
};

enum ath12k_bd_ie_board_type {
	ATH12K_BD_IE_BOARD_NAME = 0,
	ATH12K_BD_IE_BOARD_DATA = 1,
};

enum ath12k_bd_ie_regdb_type {
	ATH12K_BD_IE_REGDB_NAME = 0,
	ATH12K_BD_IE_REGDB_DATA = 1,
};

enum ath12k_bd_ie_type {
	/* contains sub IEs of enum ath12k_bd_ie_board_type */
	ATH12K_BD_IE_BOARD = 0,
	/* contains sub IEs of enum ath12k_bd_ie_regdb_type */
	ATH12K_BD_IE_REGDB = 1,
};

static inline const char *ath12k_bd_ie_type_str(enum ath12k_bd_ie_type type)
{
	switch (type) {
	case ATH12K_BD_IE_BOARD:
		return "board data";
	case ATH12K_BD_IE_REGDB:
		return "regdb data";
	}

	return "unknown";
}

#endif
