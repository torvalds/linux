/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) 2018-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef ATH12K_CORE_H
#define ATH12K_CORE_H

#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/bitfield.h>
#include <linux/dmi.h>
#include <linux/ctype.h>
#include "qmi.h"
#include "htc.h"
#include "wmi.h"
#include "hal.h"
#include "dp.h"
#include "ce.h"
#include "mac.h"
#include "hw.h"
#include "hal_rx.h"
#include "reg.h"
#include "dbring.h"

#define SM(_v, _f) (((_v) << _f##_LSB) & _f##_MASK)

#define ATH12K_TX_MGMT_NUM_PENDING_MAX	512

#define ATH12K_TX_MGMT_TARGET_MAX_SUPPORT_WMI 64

/* Pending management packets threshold for dropping probe responses */
#define ATH12K_PRB_RSP_DROP_THRESHOLD ((ATH12K_TX_MGMT_TARGET_MAX_SUPPORT_WMI * 3) / 4)

/* SMBIOS type containing Board Data File Name Extension */
#define ATH12K_SMBIOS_BDF_EXT_TYPE 0xF8

/* SMBIOS type structure length (excluding strings-set) */
#define ATH12K_SMBIOS_BDF_EXT_LENGTH 0x9

/* The magic used by QCA spec */
#define ATH12K_SMBIOS_BDF_EXT_MAGIC "BDF_"

#define ATH12K_INVALID_HW_MAC_ID	0xFF
#define	ATH12K_RX_RATE_TABLE_NUM	320
#define	ATH12K_RX_RATE_TABLE_11AX_NUM	576

#define ATH12K_MON_TIMER_INTERVAL  10
#define ATH12K_RESET_TIMEOUT_HZ			(20 * HZ)
#define ATH12K_RESET_MAX_FAIL_COUNT_FIRST	3
#define ATH12K_RESET_MAX_FAIL_COUNT_FINAL	5
#define ATH12K_RESET_FAIL_TIMEOUT_HZ		(20 * HZ)
#define ATH12K_RECONFIGURE_TIMEOUT_HZ		(10 * HZ)
#define ATH12K_RECOVER_START_TIMEOUT_HZ		(20 * HZ)

enum wme_ac {
	WME_AC_BE,
	WME_AC_BK,
	WME_AC_VI,
	WME_AC_VO,
	WME_NUM_AC
};

#define ATH12K_HT_MCS_MAX	7
#define ATH12K_VHT_MCS_MAX	9
#define ATH12K_HE_MCS_MAX	11

enum ath12k_crypt_mode {
	/* Only use hardware crypto engine */
	ATH12K_CRYPT_MODE_HW,
	/* Only use software crypto */
	ATH12K_CRYPT_MODE_SW,
};

static inline enum wme_ac ath12k_tid_to_ac(u32 tid)
{
	return (((tid == 0) || (tid == 3)) ? WME_AC_BE :
		((tid == 1) || (tid == 2)) ? WME_AC_BK :
		((tid == 4) || (tid == 5)) ? WME_AC_VI :
		WME_AC_VO);
}

enum ath12k_skb_flags {
	ATH12K_SKB_HW_80211_ENCAP = BIT(0),
	ATH12K_SKB_CIPHER_SET = BIT(1),
};

struct ath12k_skb_cb {
	dma_addr_t paddr;
	struct ath12k *ar;
	struct ieee80211_vif *vif;
	dma_addr_t paddr_ext_desc;
	u32 cipher;
	u8 flags;
};

struct ath12k_skb_rxcb {
	dma_addr_t paddr;
	bool is_first_msdu;
	bool is_last_msdu;
	bool is_continuation;
	bool is_mcbc;
	bool is_eapol;
	struct hal_rx_desc *rx_desc;
	u8 err_rel_src;
	u8 err_code;
	u8 mac_id;
	u8 unmapped;
	u8 is_frag;
	u8 tid;
	u16 peer_id;
};

enum ath12k_hw_rev {
	ATH12K_HW_QCN9274_HW10,
	ATH12K_HW_QCN9274_HW20,
	ATH12K_HW_WCN7850_HW20
};

enum ath12k_firmware_mode {
	/* the default mode, standard 802.11 functionality */
	ATH12K_FIRMWARE_MODE_NORMAL,

	/* factory tests etc */
	ATH12K_FIRMWARE_MODE_FTM,
};

#define ATH12K_IRQ_NUM_MAX 57
#define ATH12K_EXT_IRQ_NUM_MAX	16

struct ath12k_ext_irq_grp {
	struct ath12k_base *ab;
	u32 irqs[ATH12K_EXT_IRQ_NUM_MAX];
	u32 num_irq;
	u32 grp_id;
	u64 timestamp;
	struct napi_struct napi;
	struct net_device napi_ndev;
};

struct ath12k_smbios_bdf {
	struct dmi_header hdr;
	u32 padding;
	u8 bdf_enabled;
	u8 bdf_ext[];
} __packed;

#define HEHANDLE_CAP_PHYINFO_SIZE       3
#define HECAP_PHYINFO_SIZE              9
#define HECAP_MACINFO_SIZE              5
#define HECAP_TXRX_MCS_NSS_SIZE         2
#define HECAP_PPET16_PPET8_MAX_SIZE     25

#define HE_PPET16_PPET8_SIZE            8

/* 802.11ax PPE (PPDU packet Extension) threshold */
struct he_ppe_threshold {
	u32 numss_m1;
	u32 ru_mask;
	u32 ppet16_ppet8_ru3_ru0[HE_PPET16_PPET8_SIZE];
};

struct ath12k_he {
	u8 hecap_macinfo[HECAP_MACINFO_SIZE];
	u32 hecap_rxmcsnssmap;
	u32 hecap_txmcsnssmap;
	u32 hecap_phyinfo[HEHANDLE_CAP_PHYINFO_SIZE];
	struct he_ppe_threshold   hecap_ppet;
	u32 heop_param;
};

#define MAX_RADIOS 3

enum {
	WMI_HOST_TP_SCALE_MAX   = 0,
	WMI_HOST_TP_SCALE_50    = 1,
	WMI_HOST_TP_SCALE_25    = 2,
	WMI_HOST_TP_SCALE_12    = 3,
	WMI_HOST_TP_SCALE_MIN   = 4,
	WMI_HOST_TP_SCALE_SIZE   = 5,
};

enum ath12k_scan_state {
	ATH12K_SCAN_IDLE,
	ATH12K_SCAN_STARTING,
	ATH12K_SCAN_RUNNING,
	ATH12K_SCAN_ABORTING,
};

enum ath12k_dev_flags {
	ATH12K_CAC_RUNNING,
	ATH12K_FLAG_CRASH_FLUSH,
	ATH12K_FLAG_RAW_MODE,
	ATH12K_FLAG_HW_CRYPTO_DISABLED,
	ATH12K_FLAG_RECOVERY,
	ATH12K_FLAG_UNREGISTERING,
	ATH12K_FLAG_REGISTERED,
	ATH12K_FLAG_QMI_FAIL,
	ATH12K_FLAG_HTC_SUSPEND_COMPLETE,
};

enum ath12k_monitor_flags {
	ATH12K_FLAG_MONITOR_ENABLED,
};

struct ath12k_vif {
	u32 vdev_id;
	enum wmi_vdev_type vdev_type;
	enum wmi_vdev_subtype vdev_subtype;
	u32 beacon_interval;
	u32 dtim_period;
	u16 ast_hash;
	u16 ast_idx;
	u16 tcl_metadata;
	u8 hal_addr_search_flags;
	u8 search_type;

	struct ath12k *ar;
	struct ieee80211_vif *vif;

	int bank_id;
	u8 vdev_id_check_en;

	struct wmi_wmm_params_all_arg wmm_params;
	struct list_head list;
	union {
		struct {
			u32 uapsd;
		} sta;
		struct {
			/* 127 stations; wmi limit */
			u8 tim_bitmap[16];
			u8 tim_len;
			u32 ssid_len;
			u8 ssid[IEEE80211_MAX_SSID_LEN];
			bool hidden_ssid;
			/* P2P_IE with NoA attribute for P2P_GO case */
			u32 noa_len;
			u8 *noa_data;
		} ap;
	} u;

	bool is_started;
	bool is_up;
	u32 aid;
	u8 bssid[ETH_ALEN];
	struct cfg80211_bitrate_mask bitrate_mask;
	int num_legacy_stations;
	int rtscts_prot_mode;
	int txpower;
	bool rsnie_present;
	bool wpaie_present;
	struct ieee80211_chanctx_conf chanctx;
	u32 key_cipher;
	u8 tx_encap_type;
	u8 vdev_stats_id;
	u32 punct_bitmap;
};

struct ath12k_vif_iter {
	u32 vdev_id;
	struct ath12k_vif *arvif;
};

#define HAL_AST_IDX_INVALID	0xFFFF
#define HAL_RX_MAX_MCS		12
#define HAL_RX_MAX_MCS_HT	31
#define HAL_RX_MAX_MCS_VHT	9
#define HAL_RX_MAX_MCS_HE	11
#define HAL_RX_MAX_NSS		8
#define HAL_RX_MAX_NUM_LEGACY_RATES 12
#define ATH12K_RX_RATE_TABLE_11AX_NUM	576
#define ATH12K_RX_RATE_TABLE_NUM 320

struct ath12k_rx_peer_rate_stats {
	u64 ht_mcs_count[HAL_RX_MAX_MCS_HT + 1];
	u64 vht_mcs_count[HAL_RX_MAX_MCS_VHT + 1];
	u64 he_mcs_count[HAL_RX_MAX_MCS_HE + 1];
	u64 nss_count[HAL_RX_MAX_NSS];
	u64 bw_count[HAL_RX_BW_MAX];
	u64 gi_count[HAL_RX_GI_MAX];
	u64 legacy_count[HAL_RX_MAX_NUM_LEGACY_RATES];
	u64 rx_rate[ATH12K_RX_RATE_TABLE_11AX_NUM];
};

struct ath12k_rx_peer_stats {
	u64 num_msdu;
	u64 num_mpdu_fcs_ok;
	u64 num_mpdu_fcs_err;
	u64 tcp_msdu_count;
	u64 udp_msdu_count;
	u64 other_msdu_count;
	u64 ampdu_msdu_count;
	u64 non_ampdu_msdu_count;
	u64 stbc_count;
	u64 beamformed_count;
	u64 mcs_count[HAL_RX_MAX_MCS + 1];
	u64 nss_count[HAL_RX_MAX_NSS];
	u64 bw_count[HAL_RX_BW_MAX];
	u64 gi_count[HAL_RX_GI_MAX];
	u64 coding_count[HAL_RX_SU_MU_CODING_MAX];
	u64 tid_count[IEEE80211_NUM_TIDS + 1];
	u64 pream_cnt[HAL_RX_PREAMBLE_MAX];
	u64 reception_type[HAL_RX_RECEPTION_TYPE_MAX];
	u64 rx_duration;
	u64 dcm_count;
	u64 ru_alloc_cnt[HAL_RX_RU_ALLOC_TYPE_MAX];
	struct ath12k_rx_peer_rate_stats pkt_stats;
	struct ath12k_rx_peer_rate_stats byte_stats;
};

#define ATH12K_HE_MCS_NUM       12
#define ATH12K_VHT_MCS_NUM      10
#define ATH12K_BW_NUM           5
#define ATH12K_NSS_NUM          4
#define ATH12K_LEGACY_NUM       12
#define ATH12K_GI_NUM           4
#define ATH12K_HT_MCS_NUM       32

enum ath12k_pkt_rx_err {
	ATH12K_PKT_RX_ERR_FCS,
	ATH12K_PKT_RX_ERR_TKIP,
	ATH12K_PKT_RX_ERR_CRYPT,
	ATH12K_PKT_RX_ERR_PEER_IDX_INVAL,
	ATH12K_PKT_RX_ERR_MAX,
};

enum ath12k_ampdu_subfrm_num {
	ATH12K_AMPDU_SUBFRM_NUM_10,
	ATH12K_AMPDU_SUBFRM_NUM_20,
	ATH12K_AMPDU_SUBFRM_NUM_30,
	ATH12K_AMPDU_SUBFRM_NUM_40,
	ATH12K_AMPDU_SUBFRM_NUM_50,
	ATH12K_AMPDU_SUBFRM_NUM_60,
	ATH12K_AMPDU_SUBFRM_NUM_MORE,
	ATH12K_AMPDU_SUBFRM_NUM_MAX,
};

enum ath12k_amsdu_subfrm_num {
	ATH12K_AMSDU_SUBFRM_NUM_1,
	ATH12K_AMSDU_SUBFRM_NUM_2,
	ATH12K_AMSDU_SUBFRM_NUM_3,
	ATH12K_AMSDU_SUBFRM_NUM_4,
	ATH12K_AMSDU_SUBFRM_NUM_MORE,
	ATH12K_AMSDU_SUBFRM_NUM_MAX,
};

enum ath12k_counter_type {
	ATH12K_COUNTER_TYPE_BYTES,
	ATH12K_COUNTER_TYPE_PKTS,
	ATH12K_COUNTER_TYPE_MAX,
};

enum ath12k_stats_type {
	ATH12K_STATS_TYPE_SUCC,
	ATH12K_STATS_TYPE_FAIL,
	ATH12K_STATS_TYPE_RETRY,
	ATH12K_STATS_TYPE_AMPDU,
	ATH12K_STATS_TYPE_MAX,
};

struct ath12k_htt_data_stats {
	u64 legacy[ATH12K_COUNTER_TYPE_MAX][ATH12K_LEGACY_NUM];
	u64 ht[ATH12K_COUNTER_TYPE_MAX][ATH12K_HT_MCS_NUM];
	u64 vht[ATH12K_COUNTER_TYPE_MAX][ATH12K_VHT_MCS_NUM];
	u64 he[ATH12K_COUNTER_TYPE_MAX][ATH12K_HE_MCS_NUM];
	u64 bw[ATH12K_COUNTER_TYPE_MAX][ATH12K_BW_NUM];
	u64 nss[ATH12K_COUNTER_TYPE_MAX][ATH12K_NSS_NUM];
	u64 gi[ATH12K_COUNTER_TYPE_MAX][ATH12K_GI_NUM];
	u64 transmit_type[ATH12K_COUNTER_TYPE_MAX][HAL_RX_RECEPTION_TYPE_MAX];
	u64 ru_loc[ATH12K_COUNTER_TYPE_MAX][HAL_RX_RU_ALLOC_TYPE_MAX];
};

struct ath12k_htt_tx_stats {
	struct ath12k_htt_data_stats stats[ATH12K_STATS_TYPE_MAX];
	u64 tx_duration;
	u64 ba_fails;
	u64 ack_fails;
	u16 ru_start;
	u16 ru_tones;
	u32 mu_group[MAX_MU_GROUP_ID];
};

struct ath12k_per_ppdu_tx_stats {
	u16 succ_pkts;
	u16 failed_pkts;
	u16 retry_pkts;
	u32 succ_bytes;
	u32 failed_bytes;
	u32 retry_bytes;
};

struct ath12k_wbm_tx_stats {
	u64 wbm_tx_comp_stats[HAL_WBM_REL_HTT_TX_COMP_STATUS_MAX];
};

struct ath12k_sta {
	struct ath12k_vif *arvif;

	/* the following are protected by ar->data_lock */
	u32 changed; /* IEEE80211_RC_* */
	u32 bw;
	u32 nss;
	u32 smps;
	enum hal_pn_type pn_type;

	struct work_struct update_wk;
	struct rate_info txrate;
	struct rate_info last_txrate;
	u64 rx_duration;
	u64 tx_duration;
	u8 rssi_comb;
	struct ath12k_rx_peer_stats *rx_stats;
	struct ath12k_wbm_tx_stats *wbm_tx_stats;
	u32 bw_prev;
};

#define ATH12K_MIN_5G_FREQ 4150
#define ATH12K_MIN_6G_FREQ 5945
#define ATH12K_MAX_6G_FREQ 7115
#define ATH12K_NUM_CHANS 100
#define ATH12K_MAX_5G_CHAN 173

enum ath12k_state {
	ATH12K_STATE_OFF,
	ATH12K_STATE_ON,
	ATH12K_STATE_RESTARTING,
	ATH12K_STATE_RESTARTED,
	ATH12K_STATE_WEDGED,
	/* Add other states as required */
};

/* Antenna noise floor */
#define ATH12K_DEFAULT_NOISE_FLOOR -95

struct ath12k_fw_stats {
	u32 pdev_id;
	u32 stats_id;
	struct list_head pdevs;
	struct list_head vdevs;
	struct list_head bcn;
};

struct ath12k_per_peer_tx_stats {
	u32 succ_bytes;
	u32 retry_bytes;
	u32 failed_bytes;
	u32 duration;
	u16 succ_pkts;
	u16 retry_pkts;
	u16 failed_pkts;
	u16 ru_start;
	u16 ru_tones;
	u8 ba_fails;
	u8 ppdu_type;
	u32 mu_grpid;
	u32 mu_pos;
	bool is_ampdu;
};

#define ATH12K_FLUSH_TIMEOUT (5 * HZ)
#define ATH12K_VDEV_DELETE_TIMEOUT_HZ (5 * HZ)

struct ath12k {
	struct ath12k_base *ab;
	struct ath12k_pdev *pdev;
	struct ieee80211_hw *hw;
	struct ieee80211_ops *ops;
	struct ath12k_wmi_pdev *wmi;
	struct ath12k_pdev_dp dp;
	u8 mac_addr[ETH_ALEN];
	u32 ht_cap_info;
	u32 vht_cap_info;
	struct ath12k_he ar_he;
	enum ath12k_state state;
	bool supports_6ghz;
	struct {
		struct completion started;
		struct completion completed;
		struct completion on_channel;
		struct delayed_work timeout;
		enum ath12k_scan_state state;
		bool is_roc;
		int vdev_id;
		int roc_freq;
		bool roc_notify;
	} scan;

	struct {
		struct ieee80211_supported_band sbands[NUM_NL80211_BANDS];
		struct ieee80211_sband_iftype_data
			iftype[NUM_NL80211_BANDS][NUM_NL80211_IFTYPES];
	} mac;

	unsigned long dev_flags;
	unsigned int filter_flags;
	unsigned long monitor_flags;
	u32 min_tx_power;
	u32 max_tx_power;
	u32 txpower_limit_2g;
	u32 txpower_limit_5g;
	u32 txpower_scale;
	u32 power_scale;
	u32 chan_tx_pwr;
	u32 num_stations;
	u32 max_num_stations;
	bool monitor_present;
	/* To synchronize concurrent synchronous mac80211 callback operations,
	 * concurrent debugfs configuration and concurrent FW statistics events.
	 */
	struct mutex conf_mutex;
	/* protects the radio specific data like debug stats, ppdu_stats_info stats,
	 * vdev_stop_status info, scan data, ath12k_sta info, ath12k_vif info,
	 * channel context data, survey info, test mode data.
	 */
	spinlock_t data_lock;

	struct list_head arvifs;
	/* should never be NULL; needed for regular htt rx */
	struct ieee80211_channel *rx_channel;

	/* valid during scan; needed for mgmt rx during scan */
	struct ieee80211_channel *scan_channel;

	u8 cfg_tx_chainmask;
	u8 cfg_rx_chainmask;
	u8 num_rx_chains;
	u8 num_tx_chains;
	/* pdev_idx starts from 0 whereas pdev->pdev_id starts with 1 */
	u8 pdev_idx;
	u8 lmac_id;

	struct completion peer_assoc_done;
	struct completion peer_delete_done;

	int install_key_status;
	struct completion install_key_done;

	int last_wmi_vdev_start_status;
	struct completion vdev_setup_done;
	struct completion vdev_delete_done;

	int num_peers;
	int max_num_peers;
	u32 num_started_vdevs;
	u32 num_created_vdevs;
	unsigned long long allocated_vdev_map;

	struct idr txmgmt_idr;
	/* protects txmgmt_idr data */
	spinlock_t txmgmt_idr_lock;
	atomic_t num_pending_mgmt_tx;
	wait_queue_head_t txmgmt_empty_waitq;

	/* cycle count is reported twice for each visited channel during scan.
	 * access protected by data_lock
	 */
	u32 survey_last_rx_clear_count;
	u32 survey_last_cycle_count;

	/* Channel info events are expected to come in pairs without and with
	 * COMPLETE flag set respectively for each channel visit during scan.
	 *
	 * However there are deviations from this rule. This flag is used to
	 * avoid reporting garbage data.
	 */
	bool ch_info_can_report_survey;
	struct survey_info survey[ATH12K_NUM_CHANS];
	struct completion bss_survey_done;

	struct work_struct regd_update_work;

	struct work_struct wmi_mgmt_tx_work;
	struct sk_buff_head wmi_mgmt_tx_queue;

	struct ath12k_per_peer_tx_stats peer_tx_stats;
	struct list_head ppdu_stats_info;
	u32 ppdu_stat_list_depth;

	struct ath12k_per_peer_tx_stats cached_stats;
	u32 last_ppdu_id;
	u32 cached_ppdu_id;

	bool dfs_block_radar_events;
	bool monitor_conf_enabled;
	bool monitor_vdev_created;
	bool monitor_started;
	int monitor_vdev_id;
};

struct ath12k_band_cap {
	u32 phy_id;
	u32 max_bw_supported;
	u32 ht_cap_info;
	u32 he_cap_info[2];
	u32 he_mcs;
	u32 he_cap_phy_info[PSOC_HOST_MAX_PHY_SIZE];
	struct ath12k_wmi_ppe_threshold_arg he_ppet;
	u16 he_6ghz_capa;
	u32 eht_cap_mac_info[WMI_MAX_EHTCAP_MAC_SIZE];
	u32 eht_cap_phy_info[WMI_MAX_EHTCAP_PHY_SIZE];
	u32 eht_mcs_20_only;
	u32 eht_mcs_80;
	u32 eht_mcs_160;
	u32 eht_mcs_320;
	struct ath12k_wmi_ppe_threshold_arg eht_ppet;
	u32 eht_cap_info_internal;
};

struct ath12k_pdev_cap {
	u32 supported_bands;
	u32 ampdu_density;
	u32 vht_cap;
	u32 vht_mcs;
	u32 he_mcs;
	u32 tx_chain_mask;
	u32 rx_chain_mask;
	u32 tx_chain_mask_shift;
	u32 rx_chain_mask_shift;
	struct ath12k_band_cap band[NUM_NL80211_BANDS];
};

struct mlo_timestamp {
	u32 info;
	u32 sync_timestamp_lo_us;
	u32 sync_timestamp_hi_us;
	u32 mlo_offset_lo;
	u32 mlo_offset_hi;
	u32 mlo_offset_clks;
	u32 mlo_comp_clks;
	u32 mlo_comp_timer;
};

struct ath12k_pdev {
	struct ath12k *ar;
	u32 pdev_id;
	struct ath12k_pdev_cap cap;
	u8 mac_addr[ETH_ALEN];
	struct mlo_timestamp timestamp;
};

struct ath12k_fw_pdev {
	u32 pdev_id;
	u32 phy_id;
	u32 supported_bands;
};

struct ath12k_board_data {
	const struct firmware *fw;
	const void *data;
	size_t len;
};

struct ath12k_soc_dp_tx_err_stats {
	/* TCL Ring Descriptor unavailable */
	u32 desc_na[DP_TCL_NUM_RING_MAX];
	/* Other failures during dp_tx due to mem allocation failure
	 * idr unavailable etc.
	 */
	atomic_t misc_fail;
};

struct ath12k_soc_dp_stats {
	u32 err_ring_pkts;
	u32 invalid_rbm;
	u32 rxdma_error[HAL_REO_ENTR_RING_RXDMA_ECODE_MAX];
	u32 reo_error[HAL_REO_DEST_RING_ERROR_CODE_MAX];
	u32 hal_reo_error[DP_REO_DST_RING_MAX];
	struct ath12k_soc_dp_tx_err_stats tx_err;
};

/* Master structure to hold the hw data which may be used in core module */
struct ath12k_base {
	enum ath12k_hw_rev hw_rev;
	struct platform_device *pdev;
	struct device *dev;
	struct ath12k_qmi qmi;
	struct ath12k_wmi_base wmi_ab;
	struct completion fw_ready;
	int num_radios;
	/* HW channel counters frequency value in hertz common to all MACs */
	u32 cc_freq_hz;

	struct ath12k_htc htc;

	struct ath12k_dp dp;

	void __iomem *mem;
	unsigned long mem_len;

	struct {
		enum ath12k_bus bus;
		const struct ath12k_hif_ops *ops;
	} hif;

	struct ath12k_ce ce;
	struct timer_list rx_replenish_retry;
	struct ath12k_hal hal;
	/* To synchronize core_start/core_stop */
	struct mutex core_lock;
	/* Protects data like peers */
	spinlock_t base_lock;

	/* Single pdev device (struct ath12k_hw_params::single_pdev_only):
	 *
	 * Firmware maintains data for all bands but advertises a single
	 * phy to the host which is stored as a single element in this
	 * array.
	 *
	 * Other devices:
	 *
	 * This array will contain as many elements as the number of
	 * radios.
	 */
	struct ath12k_pdev pdevs[MAX_RADIOS];

	/* struct ath12k_hw_params::single_pdev_only devices use this to
	 * store phy specific data
	 */
	struct ath12k_fw_pdev fw_pdev[MAX_RADIOS];
	u8 fw_pdev_count;

	struct ath12k_pdev __rcu *pdevs_active[MAX_RADIOS];
	struct ath12k_wmi_hal_reg_capabilities_ext_arg hal_reg_cap[MAX_RADIOS];
	unsigned long long free_vdev_map;
	unsigned long long free_vdev_stats_id_map;
	struct list_head peers;
	wait_queue_head_t peer_mapping_wq;
	u8 mac_addr[ETH_ALEN];
	bool wmi_ready;
	u32 wlan_init_status;
	int irq_num[ATH12K_IRQ_NUM_MAX];
	struct ath12k_ext_irq_grp ext_irq_grp[ATH12K_EXT_IRQ_GRP_NUM_MAX];
	struct napi_struct *napi;
	struct ath12k_wmi_target_cap_arg target_caps;
	u32 ext_service_bitmap[WMI_SERVICE_EXT_BM_SIZE];
	bool pdevs_macaddr_valid;
	int bd_api;

	const struct ath12k_hw_params *hw_params;

	const struct firmware *cal_file;

	/* Below regd's are protected by ab->data_lock */
	/* This is the regd set for every radio
	 * by the firmware during initialization
	 */
	struct ieee80211_regdomain *default_regd[MAX_RADIOS];
	/* This regd is set during dynamic country setting
	 * This may or may not be used during the runtime
	 */
	struct ieee80211_regdomain *new_regd[MAX_RADIOS];

	/* Current DFS Regulatory */
	enum ath12k_dfs_region dfs_region;
	struct ath12k_soc_dp_stats soc_stats;

	unsigned long dev_flags;
	struct completion driver_recovery;
	struct workqueue_struct *workqueue;
	struct work_struct restart_work;
	struct workqueue_struct *workqueue_aux;
	struct work_struct reset_work;
	atomic_t reset_count;
	atomic_t recovery_count;
	atomic_t recovery_start_count;
	bool is_reset;
	struct completion reset_complete;
	struct completion reconfigure_complete;
	struct completion recovery_start;
	/* continuous recovery fail count */
	atomic_t fail_cont_count;
	unsigned long reset_fail_timeout;
	struct {
		/* protected by data_lock */
		u32 fw_crash_counter;
	} stats;
	u32 pktlog_defs_checksum;

	struct ath12k_dbring_cap *db_caps;
	u32 num_db_cap;

	struct timer_list mon_reap_timer;

	struct completion htc_suspend;

	u64 fw_soc_drop_count;
	bool static_window_map;

	struct work_struct rfkill_work;
	/* true means radio is on */
	bool rfkill_radio_on;

	/* must be last */
	u8 drv_priv[] __aligned(sizeof(void *));
};

int ath12k_core_qmi_firmware_ready(struct ath12k_base *ab);
int ath12k_core_pre_init(struct ath12k_base *ab);
int ath12k_core_init(struct ath12k_base *ath12k);
void ath12k_core_deinit(struct ath12k_base *ath12k);
struct ath12k_base *ath12k_core_alloc(struct device *dev, size_t priv_size,
				      enum ath12k_bus bus);
void ath12k_core_free(struct ath12k_base *ath12k);
int ath12k_core_fetch_board_data_api_1(struct ath12k_base *ab,
				       struct ath12k_board_data *bd,
				       char *filename);
int ath12k_core_fetch_bdf(struct ath12k_base *ath12k,
			  struct ath12k_board_data *bd);
void ath12k_core_free_bdf(struct ath12k_base *ab, struct ath12k_board_data *bd);
int ath12k_core_check_dt(struct ath12k_base *ath12k);
int ath12k_core_check_smbios(struct ath12k_base *ab);
void ath12k_core_halt(struct ath12k *ar);
int ath12k_core_resume(struct ath12k_base *ab);
int ath12k_core_suspend(struct ath12k_base *ab);

const struct firmware *ath12k_core_firmware_request(struct ath12k_base *ab,
						    const char *filename);

static inline const char *ath12k_scan_state_str(enum ath12k_scan_state state)
{
	switch (state) {
	case ATH12K_SCAN_IDLE:
		return "idle";
	case ATH12K_SCAN_STARTING:
		return "starting";
	case ATH12K_SCAN_RUNNING:
		return "running";
	case ATH12K_SCAN_ABORTING:
		return "aborting";
	}

	return "unknown";
}

static inline struct ath12k_skb_cb *ATH12K_SKB_CB(struct sk_buff *skb)
{
	BUILD_BUG_ON(sizeof(struct ath12k_skb_cb) >
		     IEEE80211_TX_INFO_DRIVER_DATA_SIZE);
	return (struct ath12k_skb_cb *)&IEEE80211_SKB_CB(skb)->driver_data;
}

static inline struct ath12k_skb_rxcb *ATH12K_SKB_RXCB(struct sk_buff *skb)
{
	BUILD_BUG_ON(sizeof(struct ath12k_skb_rxcb) > sizeof(skb->cb));
	return (struct ath12k_skb_rxcb *)skb->cb;
}

static inline struct ath12k_vif *ath12k_vif_to_arvif(struct ieee80211_vif *vif)
{
	return (struct ath12k_vif *)vif->drv_priv;
}

static inline struct ath12k *ath12k_ab_to_ar(struct ath12k_base *ab,
					     int mac_id)
{
	return ab->pdevs[ath12k_hw_mac_id_to_pdev_id(ab->hw_params, mac_id)].ar;
}

static inline void ath12k_core_create_firmware_path(struct ath12k_base *ab,
						    const char *filename,
						    void *buf, size_t buf_len)
{
	snprintf(buf, buf_len, "%s/%s/%s", ATH12K_FW_DIR,
		 ab->hw_params->fw.dir, filename);
}

static inline const char *ath12k_bus_str(enum ath12k_bus bus)
{
	switch (bus) {
	case ATH12K_BUS_PCI:
		return "pci";
	}

	return "unknown";
}

#endif /* _CORE_H_ */
