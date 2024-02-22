/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) 2018-2019 The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef ATH11K_HW_H
#define ATH11K_HW_H

#include "hal.h"
#include "wmi.h"

/* Target configuration defines */

/* Num VDEVS per radio */
#define TARGET_NUM_VDEVS(ab)	(ab->hw_params.num_vdevs)

#define TARGET_NUM_PEERS_PDEV(ab) (ab->hw_params.num_peers + TARGET_NUM_VDEVS(ab))

/* Num of peers for Single Radio mode */
#define TARGET_NUM_PEERS_SINGLE(ab) (TARGET_NUM_PEERS_PDEV(ab))

/* Num of peers for DBS */
#define TARGET_NUM_PEERS_DBS(ab) (2 * TARGET_NUM_PEERS_PDEV(ab))

/* Num of peers for DBS_SBS */
#define TARGET_NUM_PEERS_DBS_SBS(ab)	(3 * TARGET_NUM_PEERS_PDEV(ab))

/* Max num of stations (per radio) */
#define TARGET_NUM_STATIONS(ab)	(ab->hw_params.num_peers)

#define TARGET_NUM_PEERS(ab, x)	TARGET_NUM_PEERS_##x(ab)
#define TARGET_NUM_PEER_KEYS	2
#define TARGET_NUM_TIDS(ab, x)	(2 * TARGET_NUM_PEERS(ab, x) +	\
				 4 * TARGET_NUM_VDEVS(ab) + 8)

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

#define ATH11K_HW_MAX_QUEUES		4
#define ATH11K_QUEUE_LEN		4096

#define ATH11k_HW_RATECODE_CCK_SHORT_PREAM_MASK  0x4

#define ATH11K_FW_DIR			"ath11k"

#define ATH11K_BOARD_MAGIC		"QCA-ATH11K-BOARD"
#define ATH11K_BOARD_API2_FILE		"board-2.bin"
#define ATH11K_DEFAULT_BOARD_FILE	"board.bin"
#define ATH11K_DEFAULT_CAL_FILE		"caldata.bin"
#define ATH11K_AMSS_FILE		"amss.bin"
#define ATH11K_M3_FILE			"m3.bin"
#define ATH11K_REGDB_FILE_NAME		"regdb.bin"

#define ATH11K_CE_OFFSET(ab)	(ab->mem_ce - ab->mem)

enum ath11k_hw_rate_cck {
	ATH11K_HW_RATE_CCK_LP_11M = 0,
	ATH11K_HW_RATE_CCK_LP_5_5M,
	ATH11K_HW_RATE_CCK_LP_2M,
	ATH11K_HW_RATE_CCK_LP_1M,
	ATH11K_HW_RATE_CCK_SP_11M,
	ATH11K_HW_RATE_CCK_SP_5_5M,
	ATH11K_HW_RATE_CCK_SP_2M,
};

enum ath11k_hw_rate_ofdm {
	ATH11K_HW_RATE_OFDM_48M = 0,
	ATH11K_HW_RATE_OFDM_24M,
	ATH11K_HW_RATE_OFDM_12M,
	ATH11K_HW_RATE_OFDM_6M,
	ATH11K_HW_RATE_OFDM_54M,
	ATH11K_HW_RATE_OFDM_36M,
	ATH11K_HW_RATE_OFDM_18M,
	ATH11K_HW_RATE_OFDM_9M,
};

enum ath11k_bus {
	ATH11K_BUS_AHB,
	ATH11K_BUS_PCI,
};

#define ATH11K_EXT_IRQ_GRP_NUM_MAX 11

struct hal_rx_desc;
struct hal_tcl_data_cmd;

struct ath11k_hw_ring_mask {
	u8 tx[ATH11K_EXT_IRQ_GRP_NUM_MAX];
	u8 rx_mon_status[ATH11K_EXT_IRQ_GRP_NUM_MAX];
	u8 rx[ATH11K_EXT_IRQ_GRP_NUM_MAX];
	u8 rx_err[ATH11K_EXT_IRQ_GRP_NUM_MAX];
	u8 rx_wbm_rel[ATH11K_EXT_IRQ_GRP_NUM_MAX];
	u8 reo_status[ATH11K_EXT_IRQ_GRP_NUM_MAX];
	u8 rxdma2host[ATH11K_EXT_IRQ_GRP_NUM_MAX];
	u8 host2rxdma[ATH11K_EXT_IRQ_GRP_NUM_MAX];
};

struct ath11k_hw_tcl2wbm_rbm_map {
	u8 tcl_ring_num;
	u8 wbm_ring_num;
	u8 rbm_id;
};

struct ath11k_hw_hal_params {
	enum hal_rx_buf_return_buf_manager rx_buf_rbm;
	const struct ath11k_hw_tcl2wbm_rbm_map *tcl2wbm_rbm_map;
};

struct ath11k_hw_params {
	const char *name;
	u16 hw_rev;
	u8 max_radios;
	u32 bdf_addr;

	struct {
		const char *dir;
		size_t board_size;
		size_t cal_offset;
	} fw;

	const struct ath11k_hw_ops *hw_ops;
	const struct ath11k_hw_ring_mask *ring_mask;

	bool internal_sleep_clock;

	const struct ath11k_hw_regs *regs;
	u32 qmi_service_ins_id;
	const struct ce_attr *host_ce_config;
	u32 ce_count;
	const struct ce_pipe_config *target_ce_config;
	u32 target_ce_count;
	const struct service_to_pipe *svc_to_ce_map;
	u32 svc_to_ce_map_len;
	const struct ce_ie_addr *ce_ie_addr;
	const struct ce_remap *ce_remap;

	bool single_pdev_only;

	bool rxdma1_enable;
	int num_rxmda_per_pdev;
	bool rx_mac_buf_ring;
	bool vdev_start_delay;
	bool htt_peer_map_v2;

	struct {
		u8 fft_sz;
		u8 fft_pad_sz;
		u8 summary_pad_sz;
		u8 fft_hdr_len;
		u16 max_fft_bins;
		bool fragment_160mhz;
	} spectral;

	u16 interface_modes;
	bool supports_monitor;
	bool full_monitor_mode;
	bool supports_shadow_regs;
	bool idle_ps;
	bool supports_sta_ps;
	bool coldboot_cal_mm;
	bool coldboot_cal_ftm;
	bool cbcal_restart_fw;
	int fw_mem_mode;
	u32 num_vdevs;
	u32 num_peers;
	bool supports_suspend;
	u32 hal_desc_sz;
	bool supports_regdb;
	bool fix_l1ss;
	bool credit_flow;
	u8 max_tx_ring;
	const struct ath11k_hw_hal_params *hal_params;
	bool supports_dynamic_smps_6ghz;
	bool alloc_cacheable_memory;
	bool supports_rssi_stats;
	bool fw_wmi_diag_event;
	bool current_cc_support;
	bool dbr_debug_support;
	bool global_reset;
	const struct cfg80211_sar_capa *bios_sar_capa;
	bool m3_fw_support;
	bool fixed_bdf_addr;
	bool fixed_mem_region;
	bool static_window_map;
	bool hybrid_bus_type;
	bool fixed_fw_mem;
	bool support_off_channel_tx;
	bool supports_multi_bssid;

	struct {
		u32 start;
		u32 end;
	} sram_dump;

	bool tcl_ring_retry;
	u32 tx_ring_size;
	bool smp2p_wow_exit;
	bool support_fw_mac_sequence;
	bool support_dual_stations;
};

struct ath11k_hw_ops {
	u8 (*get_hw_mac_from_pdev_id)(int pdev_id);
	void (*wmi_init_config)(struct ath11k_base *ab,
				struct target_resource_config *config);
	int (*mac_id_to_pdev_id)(struct ath11k_hw_params *hw, int mac_id);
	int (*mac_id_to_srng_id)(struct ath11k_hw_params *hw, int mac_id);
	void (*tx_mesh_enable)(struct ath11k_base *ab,
			       struct hal_tcl_data_cmd *tcl_cmd);
	bool (*rx_desc_get_first_msdu)(struct hal_rx_desc *desc);
	bool (*rx_desc_get_last_msdu)(struct hal_rx_desc *desc);
	u8 (*rx_desc_get_l3_pad_bytes)(struct hal_rx_desc *desc);
	u8 *(*rx_desc_get_hdr_status)(struct hal_rx_desc *desc);
	bool (*rx_desc_encrypt_valid)(struct hal_rx_desc *desc);
	u32 (*rx_desc_get_encrypt_type)(struct hal_rx_desc *desc);
	u8 (*rx_desc_get_decap_type)(struct hal_rx_desc *desc);
	u8 (*rx_desc_get_mesh_ctl)(struct hal_rx_desc *desc);
	bool (*rx_desc_get_ldpc_support)(struct hal_rx_desc *desc);
	bool (*rx_desc_get_mpdu_seq_ctl_vld)(struct hal_rx_desc *desc);
	bool (*rx_desc_get_mpdu_fc_valid)(struct hal_rx_desc *desc);
	u16 (*rx_desc_get_mpdu_start_seq_no)(struct hal_rx_desc *desc);
	u16 (*rx_desc_get_msdu_len)(struct hal_rx_desc *desc);
	u8 (*rx_desc_get_msdu_sgi)(struct hal_rx_desc *desc);
	u8 (*rx_desc_get_msdu_rate_mcs)(struct hal_rx_desc *desc);
	u8 (*rx_desc_get_msdu_rx_bw)(struct hal_rx_desc *desc);
	u32 (*rx_desc_get_msdu_freq)(struct hal_rx_desc *desc);
	u8 (*rx_desc_get_msdu_pkt_type)(struct hal_rx_desc *desc);
	u8 (*rx_desc_get_msdu_nss)(struct hal_rx_desc *desc);
	u8 (*rx_desc_get_mpdu_tid)(struct hal_rx_desc *desc);
	u16 (*rx_desc_get_mpdu_peer_id)(struct hal_rx_desc *desc);
	void (*rx_desc_copy_attn_end_tlv)(struct hal_rx_desc *fdesc,
					  struct hal_rx_desc *ldesc);
	u32 (*rx_desc_get_mpdu_start_tag)(struct hal_rx_desc *desc);
	u32 (*rx_desc_get_mpdu_ppdu_id)(struct hal_rx_desc *desc);
	void (*rx_desc_set_msdu_len)(struct hal_rx_desc *desc, u16 len);
	struct rx_attention *(*rx_desc_get_attention)(struct hal_rx_desc *desc);
	u8 *(*rx_desc_get_msdu_payload)(struct hal_rx_desc *desc);
	void (*reo_setup)(struct ath11k_base *ab);
	u16 (*mpdu_info_get_peerid)(struct hal_rx_mpdu_info *mpdu_info);
	bool (*rx_desc_mac_addr2_valid)(struct hal_rx_desc *desc);
	u8* (*rx_desc_mpdu_start_addr2)(struct hal_rx_desc *desc);
	u32 (*get_ring_selector)(struct sk_buff *skb);
};

extern const struct ath11k_hw_ops ipq8074_ops;
extern const struct ath11k_hw_ops ipq6018_ops;
extern const struct ath11k_hw_ops qca6390_ops;
extern const struct ath11k_hw_ops qcn9074_ops;
extern const struct ath11k_hw_ops wcn6855_ops;
extern const struct ath11k_hw_ops wcn6750_ops;
extern const struct ath11k_hw_ops ipq5018_ops;

extern const struct ath11k_hw_ring_mask ath11k_hw_ring_mask_ipq8074;
extern const struct ath11k_hw_ring_mask ath11k_hw_ring_mask_qca6390;
extern const struct ath11k_hw_ring_mask ath11k_hw_ring_mask_qcn9074;
extern const struct ath11k_hw_ring_mask ath11k_hw_ring_mask_wcn6750;

extern const struct ce_ie_addr ath11k_ce_ie_addr_ipq8074;
extern const struct ce_ie_addr ath11k_ce_ie_addr_ipq5018;

extern const struct ce_remap ath11k_ce_remap_ipq5018;

extern const struct ath11k_hw_hal_params ath11k_hw_hal_params_ipq8074;
extern const struct ath11k_hw_hal_params ath11k_hw_hal_params_qca6390;
extern const struct ath11k_hw_hal_params ath11k_hw_hal_params_wcn6750;

static inline
int ath11k_hw_get_mac_from_pdev_id(struct ath11k_hw_params *hw,
				   int pdev_idx)
{
	if (hw->hw_ops->get_hw_mac_from_pdev_id)
		return hw->hw_ops->get_hw_mac_from_pdev_id(pdev_idx);

	return 0;
}

static inline int ath11k_hw_mac_id_to_pdev_id(struct ath11k_hw_params *hw,
					      int mac_id)
{
	if (hw->hw_ops->mac_id_to_pdev_id)
		return hw->hw_ops->mac_id_to_pdev_id(hw, mac_id);

	return 0;
}

static inline int ath11k_hw_mac_id_to_srng_id(struct ath11k_hw_params *hw,
					      int mac_id)
{
	if (hw->hw_ops->mac_id_to_srng_id)
		return hw->hw_ops->mac_id_to_srng_id(hw, mac_id);

	return 0;
}

struct ath11k_fw_ie {
	__le32 id;
	__le32 len;
	u8 data[];
};

enum ath11k_bd_ie_board_type {
	ATH11K_BD_IE_BOARD_NAME = 0,
	ATH11K_BD_IE_BOARD_DATA = 1,
};

enum ath11k_bd_ie_regdb_type {
	ATH11K_BD_IE_REGDB_NAME = 0,
	ATH11K_BD_IE_REGDB_DATA = 1,
};

enum ath11k_bd_ie_type {
	/* contains sub IEs of enum ath11k_bd_ie_board_type */
	ATH11K_BD_IE_BOARD = 0,
	/* contains sub IEs of enum ath11k_bd_ie_regdb_type */
	ATH11K_BD_IE_REGDB = 1,
};

struct ath11k_hw_regs {
	u32 hal_tcl1_ring_base_lsb;
	u32 hal_tcl1_ring_base_msb;
	u32 hal_tcl1_ring_id;
	u32 hal_tcl1_ring_misc;
	u32 hal_tcl1_ring_tp_addr_lsb;
	u32 hal_tcl1_ring_tp_addr_msb;
	u32 hal_tcl1_ring_consumer_int_setup_ix0;
	u32 hal_tcl1_ring_consumer_int_setup_ix1;
	u32 hal_tcl1_ring_msi1_base_lsb;
	u32 hal_tcl1_ring_msi1_base_msb;
	u32 hal_tcl1_ring_msi1_data;
	u32 hal_tcl2_ring_base_lsb;
	u32 hal_tcl_ring_base_lsb;

	u32 hal_tcl_status_ring_base_lsb;

	u32 hal_reo1_ring_base_lsb;
	u32 hal_reo1_ring_base_msb;
	u32 hal_reo1_ring_id;
	u32 hal_reo1_ring_misc;
	u32 hal_reo1_ring_hp_addr_lsb;
	u32 hal_reo1_ring_hp_addr_msb;
	u32 hal_reo1_ring_producer_int_setup;
	u32 hal_reo1_ring_msi1_base_lsb;
	u32 hal_reo1_ring_msi1_base_msb;
	u32 hal_reo1_ring_msi1_data;
	u32 hal_reo2_ring_base_lsb;
	u32 hal_reo1_aging_thresh_ix_0;
	u32 hal_reo1_aging_thresh_ix_1;
	u32 hal_reo1_aging_thresh_ix_2;
	u32 hal_reo1_aging_thresh_ix_3;

	u32 hal_reo1_ring_hp;
	u32 hal_reo1_ring_tp;
	u32 hal_reo2_ring_hp;

	u32 hal_reo_tcl_ring_base_lsb;
	u32 hal_reo_tcl_ring_hp;

	u32 hal_reo_status_ring_base_lsb;
	u32 hal_reo_status_hp;

	u32 hal_reo_cmd_ring_base_lsb;
	u32 hal_reo_cmd_ring_hp;

	u32 hal_sw2reo_ring_base_lsb;
	u32 hal_sw2reo_ring_hp;

	u32 hal_seq_wcss_umac_ce0_src_reg;
	u32 hal_seq_wcss_umac_ce0_dst_reg;
	u32 hal_seq_wcss_umac_ce1_src_reg;
	u32 hal_seq_wcss_umac_ce1_dst_reg;

	u32 hal_wbm_idle_link_ring_base_lsb;
	u32 hal_wbm_idle_link_ring_misc;

	u32 hal_wbm_release_ring_base_lsb;

	u32 hal_wbm0_release_ring_base_lsb;
	u32 hal_wbm1_release_ring_base_lsb;

	u32 pcie_qserdes_sysclk_en_sel;
	u32 pcie_pcs_osc_dtct_config_base;

	u32 hal_shadow_base_addr;
	u32 hal_reo1_misc_ctl;
};

extern const struct ath11k_hw_regs ipq8074_regs;
extern const struct ath11k_hw_regs qca6390_regs;
extern const struct ath11k_hw_regs qcn9074_regs;
extern const struct ath11k_hw_regs wcn6855_regs;
extern const struct ath11k_hw_regs wcn6750_regs;
extern const struct ath11k_hw_regs ipq5018_regs;

static inline const char *ath11k_bd_ie_type_str(enum ath11k_bd_ie_type type)
{
	switch (type) {
	case ATH11K_BD_IE_BOARD:
		return "board data";
	case ATH11K_BD_IE_REGDB:
		return "regdb data";
	}

	return "unknown";
}

extern const struct cfg80211_sar_capa ath11k_hw_sar_capa_wcn6855;

#endif
