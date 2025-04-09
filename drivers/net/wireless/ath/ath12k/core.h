/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) 2018-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2025 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef ATH12K_CORE_H
#define ATH12K_CORE_H

#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/bitfield.h>
#include <linux/dmi.h>
#include <linux/ctype.h>
#include <linux/firmware.h>
#include <linux/of_reserved_mem.h>
#include <linux/panic_notifier.h>
#include <linux/average.h>
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
#include "fw.h"
#include "acpi.h"
#include "wow.h"
#include "debugfs_htt_stats.h"
#include "coredump.h"

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
#define ATH12K_CONNECTION_LOSS_HZ	(3 * HZ)

#define ATH12K_MON_TIMER_INTERVAL  10
#define ATH12K_RESET_TIMEOUT_HZ			(20 * HZ)
#define ATH12K_RESET_MAX_FAIL_COUNT_FIRST	3
#define ATH12K_RESET_MAX_FAIL_COUNT_FINAL	5
#define ATH12K_RESET_FAIL_TIMEOUT_HZ		(20 * HZ)
#define ATH12K_RECONFIGURE_TIMEOUT_HZ		(10 * HZ)
#define ATH12K_RECOVER_START_TIMEOUT_HZ		(20 * HZ)

#define ATH12K_MAX_SOCS 3
#define ATH12K_GROUP_MAX_RADIO (ATH12K_MAX_SOCS * MAX_RADIOS)
#define ATH12K_INVALID_GROUP_ID  0xFF
#define ATH12K_INVALID_DEVICE_ID 0xFF

#define ATH12K_MAX_MLO_PEERS            256
#define ATH12K_MLO_PEER_ID_INVALID      0xFFFF

enum ath12k_bdf_search {
	ATH12K_BDF_SEARCH_DEFAULT,
	ATH12K_BDF_SEARCH_BUS_AND_BOARD,
};

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
#define ATH12K_EHT_MCS_MAX	15

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

static inline u64 ath12k_le32hilo_to_u64(__le32 hi, __le32 lo)
{
	u64 hi64 = le32_to_cpu(hi);
	u64 lo64 = le32_to_cpu(lo);

	return (hi64 << 32) | lo64;
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
	u8 link_id;
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
	u8 hw_link_id;
	u8 unmapped;
	u8 is_frag;
	u8 tid;
	u16 peer_id;
	bool is_end_of_ppdu;
};

enum ath12k_hw_rev {
	ATH12K_HW_QCN9274_HW10,
	ATH12K_HW_QCN9274_HW20,
	ATH12K_HW_WCN7850_HW20,
	ATH12K_HW_IPQ5332_HW10,
};

enum ath12k_firmware_mode {
	/* the default mode, standard 802.11 functionality */
	ATH12K_FIRMWARE_MODE_NORMAL,

	/* factory tests etc */
	ATH12K_FIRMWARE_MODE_FTM,
};

#define ATH12K_IRQ_NUM_MAX 57
#define ATH12K_EXT_IRQ_NUM_MAX	16
#define ATH12K_MAX_TCL_RING_NUM	3

struct ath12k_ext_irq_grp {
	struct ath12k_base *ab;
	u32 irqs[ATH12K_EXT_IRQ_NUM_MAX];
	u32 num_irq;
	u32 grp_id;
	u64 timestamp;
	bool napi_enabled;
	struct napi_struct napi;
	struct net_device *napi_ndev;
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

enum ath12k_11d_state {
	ATH12K_11D_IDLE,
	ATH12K_11D_PREPARING,
	ATH12K_11D_RUNNING,
};

enum ath12k_hw_group_flags {
	ATH12K_GROUP_FLAG_REGISTERED,
	ATH12K_GROUP_FLAG_UNREGISTER,
};

enum ath12k_dev_flags {
	ATH12K_FLAG_CAC_RUNNING,
	ATH12K_FLAG_CRASH_FLUSH,
	ATH12K_FLAG_RAW_MODE,
	ATH12K_FLAG_HW_CRYPTO_DISABLED,
	ATH12K_FLAG_RECOVERY,
	ATH12K_FLAG_UNREGISTERING,
	ATH12K_FLAG_REGISTERED,
	ATH12K_FLAG_QMI_FAIL,
	ATH12K_FLAG_HTC_SUSPEND_COMPLETE,
	ATH12K_FLAG_CE_IRQ_ENABLED,
	ATH12K_FLAG_EXT_IRQ_ENABLED,
	ATH12K_FLAG_QMI_FW_READY_COMPLETE,
	ATH12K_FLAG_FTM_SEGMENTED,
	ATH12K_FLAG_FIXED_MEM_REGION,
};

struct ath12k_tx_conf {
	bool changed;
	u16 ac;
	struct ieee80211_tx_queue_params tx_queue_params;
};

struct ath12k_key_conf {
	enum set_key_cmd cmd;
	struct list_head list;
	struct ieee80211_sta *sta;
	struct ieee80211_key_conf *key;
};

struct ath12k_vif_cache {
	struct ath12k_tx_conf tx_conf;
	struct ath12k_key_conf key_conf;
	u32 bss_conf_changed;
};

struct ath12k_rekey_data {
	u8 kck[NL80211_KCK_LEN];
	u8 kek[NL80211_KCK_LEN];
	u64 replay_ctr;
	bool enable_offload;
};

struct ath12k_link_vif {
	u32 vdev_id;
	u32 beacon_interval;
	u32 dtim_period;
	u16 ast_hash;
	u16 ast_idx;
	u16 tcl_metadata;
	u8 hal_addr_search_flags;
	u8 search_type;

	struct ath12k *ar;

	int bank_id;
	u8 vdev_id_check_en;

	struct wmi_wmm_params_all_arg wmm_params;
	struct list_head list;

	bool is_created;
	bool is_started;
	bool is_up;
	u8 bssid[ETH_ALEN];
	struct cfg80211_bitrate_mask bitrate_mask;
	struct delayed_work connection_loss_work;
	int num_legacy_stations;
	int rtscts_prot_mode;
	int txpower;
	bool rsnie_present;
	bool wpaie_present;
	u8 vdev_stats_id;
	u32 punct_bitmap;
	u8 link_id;
	struct ath12k_vif *ahvif;
	struct ath12k_rekey_data rekey_data;
	struct ath12k_link_stats link_stats;
	spinlock_t link_stats_lock; /* Protects updates to link_stats */

	u8 current_cntdown_counter;
};

struct ath12k_vif {
	enum wmi_vdev_type vdev_type;
	enum wmi_vdev_subtype vdev_subtype;
	struct ieee80211_vif *vif;
	struct ath12k_hw *ah;

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

	u32 aid;
	u32 key_cipher;
	u8 tx_encap_type;
	bool ps;
	atomic_t mcbc_gsn;

	struct ath12k_link_vif deflink;
	struct ath12k_link_vif __rcu *link[ATH12K_NUM_MAX_LINKS];
	struct ath12k_vif_cache *cache[IEEE80211_MLD_MAX_NUM_LINKS];
	/* indicates bitmap of link vif created in FW */
	u16 links_map;
	u8 last_scan_link;

	/* Must be last - ends in a flexible-array member.
	 *
	 * FIXME: Driver should not copy struct ieee80211_chanctx_conf,
	 * especially because it has a flexible array. Find a better way.
	 */
	struct ieee80211_chanctx_conf chanctx;
};

struct ath12k_vif_iter {
	u32 vdev_id;
	struct ath12k *ar;
	struct ath12k_link_vif *arvif;
};

#define HAL_AST_IDX_INVALID	0xFFFF
#define HAL_RX_MAX_MCS		12
#define HAL_RX_MAX_MCS_HT	31
#define HAL_RX_MAX_MCS_VHT	9
#define HAL_RX_MAX_MCS_HE	11
#define HAL_RX_MAX_MCS_BE	15
#define HAL_RX_MAX_NSS		8
#define HAL_RX_MAX_NUM_LEGACY_RATES 12

#define ATH12K_SCAN_TIMEOUT_HZ (20 * HZ)

struct ath12k_rx_peer_rate_stats {
	u64 ht_mcs_count[HAL_RX_MAX_MCS_HT + 1];
	u64 vht_mcs_count[HAL_RX_MAX_MCS_VHT + 1];
	u64 he_mcs_count[HAL_RX_MAX_MCS_HE + 1];
	u64 be_mcs_count[HAL_RX_MAX_MCS_BE + 1];
	u64 nss_count[HAL_RX_MAX_NSS];
	u64 bw_count[HAL_RX_BW_MAX];
	u64 gi_count[HAL_RX_GI_MAX];
	u64 legacy_count[HAL_RX_MAX_NUM_LEGACY_RATES];
	u64 rx_rate[HAL_RX_BW_MAX][HAL_RX_GI_MAX][HAL_RX_MAX_NSS][HAL_RX_MAX_MCS_HT + 1];
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

DECLARE_EWMA(avg_rssi, 10, 8)

struct ath12k_link_sta {
	struct ath12k_link_vif *arvif;
	struct ath12k_sta *ahsta;

	/* link address similar to ieee80211_link_sta */
	u8 addr[ETH_ALEN];

	/* the following are protected by ar->data_lock */
	u32 changed; /* IEEE80211_RC_* */
	u32 bw;
	u32 nss;
	u32 smps;

	struct wiphy_work update_wk;
	struct rate_info txrate;
	struct rate_info last_txrate;
	u64 rx_duration;
	u64 tx_duration;
	u8 rssi_comb;
	struct ewma_avg_rssi avg_rssi;
	u8 link_id;
	struct ath12k_rx_peer_stats *rx_stats;
	struct ath12k_wbm_tx_stats *wbm_tx_stats;
	u32 bw_prev;
	u32 peer_nss;
	s8 rssi_beacon;

	/* For now the assoc link will be considered primary */
	bool is_assoc_link;

	 /* for firmware use only */
	u8 link_idx;
};

struct ath12k_sta {
	struct ath12k_vif *ahvif;
	enum hal_pn_type pn_type;
	struct ath12k_link_sta deflink;
	struct ath12k_link_sta __rcu *link[IEEE80211_MLD_MAX_NUM_LINKS];
	/* indicates bitmap of link sta created in FW */
	u16 links_map;
	u8 assoc_link_id;
	u16 ml_peer_id;
	u8 num_peer;

	enum ieee80211_sta_state state;
};

#define ATH12K_HALF_20MHZ_BW	10
#define ATH12K_2GHZ_MIN_CENTER	2412
#define ATH12K_2GHZ_MAX_CENTER	2484
#define ATH12K_5GHZ_MIN_CENTER	4900
#define ATH12K_5GHZ_MAX_CENTER	5920
#define ATH12K_6GHZ_MIN_CENTER	5935
#define ATH12K_6GHZ_MAX_CENTER	7115
#define ATH12K_MIN_2GHZ_FREQ	(ATH12K_2GHZ_MIN_CENTER - ATH12K_HALF_20MHZ_BW - 1)
#define ATH12K_MAX_2GHZ_FREQ	(ATH12K_2GHZ_MAX_CENTER + ATH12K_HALF_20MHZ_BW + 1)
#define ATH12K_MIN_5GHZ_FREQ	(ATH12K_5GHZ_MIN_CENTER - ATH12K_HALF_20MHZ_BW)
#define ATH12K_MAX_5GHZ_FREQ	(ATH12K_5GHZ_MAX_CENTER + ATH12K_HALF_20MHZ_BW)
#define ATH12K_MIN_6GHZ_FREQ	(ATH12K_6GHZ_MIN_CENTER - ATH12K_HALF_20MHZ_BW)
#define ATH12K_MAX_6GHZ_FREQ	(ATH12K_6GHZ_MAX_CENTER + ATH12K_HALF_20MHZ_BW)
#define ATH12K_NUM_CHANS 101
#define ATH12K_MAX_5GHZ_CHAN 173

enum ath12k_hw_state {
	ATH12K_HW_STATE_OFF,
	ATH12K_HW_STATE_ON,
	ATH12K_HW_STATE_RESTARTING,
	ATH12K_HW_STATE_RESTARTED,
	ATH12K_HW_STATE_WEDGED,
	ATH12K_HW_STATE_TM,
	/* Add other states as required */
};

/* Antenna noise floor */
#define ATH12K_DEFAULT_NOISE_FLOOR -95

struct ath12k_ftm_event_obj {
	u32 data_pos;
	u32 expected_seq;
	u8 *eventdata;
};

struct ath12k_fw_stats {
	u32 pdev_id;
	u32 stats_id;
	struct list_head pdevs;
	struct list_head vdevs;
	struct list_head bcn;
	bool fw_stats_done;
};

struct ath12k_dbg_htt_stats {
	enum ath12k_dbg_htt_ext_stats_type type;
	u32 cfg_param[4];
	u8 reset;
	struct debug_htt_stats_req *stats_req;
};

struct ath12k_debug {
	struct dentry *debugfs_pdev;
	struct dentry *debugfs_pdev_symlink;
	struct ath12k_dbg_htt_stats htt_stats;
	enum wmi_halphy_ctrl_path_stats_id tpc_stats_type;
	bool tpc_request;
	struct completion tpc_complete;
	struct wmi_tpc_stats_arg *tpc_stats;
	u32 rx_filter;
	bool extd_rx_stats;
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
	struct ath12k_hw *ah;
	struct ath12k_wmi_pdev *wmi;
	struct ath12k_pdev_dp dp;
	u8 mac_addr[ETH_ALEN];
	u32 ht_cap_info;
	u32 vht_cap_info;
	struct ath12k_he ar_he;
	bool supports_6ghz;
	struct {
		struct completion started;
		struct completion completed;
		struct completion on_channel;
		struct delayed_work timeout;
		enum ath12k_scan_state state;
		bool is_roc;
		int roc_freq;
		bool roc_notify;
		struct wiphy_work vdev_clean_wk;
		struct ath12k_link_vif *arvif;
	} scan;

	struct {
		struct ieee80211_supported_band sbands[NUM_NL80211_BANDS];
		struct ieee80211_sband_iftype_data
			iftype[NUM_NL80211_BANDS][NUM_NL80211_IFTYPES];
	} mac;

	unsigned long dev_flags;
	unsigned int filter_flags;
	u32 min_tx_power;
	u32 max_tx_power;
	u32 txpower_limit_2g;
	u32 txpower_limit_5g;
	u32 txpower_scale;
	u32 power_scale;
	u32 chan_tx_pwr;
	u32 num_stations;
	u32 max_num_stations;

	/* protects the radio specific data like debug stats, ppdu_stats_info stats,
	 * vdev_stop_status info, scan data, ath12k_sta info, ath12k_link_vif info,
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
	u8 hw_link_id;

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

	struct wiphy_work wmi_mgmt_tx_work;
	struct sk_buff_head wmi_mgmt_tx_queue;

	struct ath12k_wow wow;
	struct completion target_suspend;
	bool target_suspend_ack;
	struct ath12k_per_peer_tx_stats peer_tx_stats;
	struct list_head ppdu_stats_info;
	u32 ppdu_stat_list_depth;

	struct ath12k_per_peer_tx_stats cached_stats;
	u32 last_ppdu_id;
	u32 cached_ppdu_id;
#ifdef CONFIG_ATH12K_DEBUGFS
	struct ath12k_debug debug;
#endif

	bool dfs_block_radar_events;
	bool monitor_vdev_created;
	bool monitor_started;
	int monitor_vdev_id;

	struct wiphy_radio_freq_range freq_range;

	bool nlo_enabled;

	/* Protected by wiphy::mtx lock. */
	u32 vdev_id_11d_scan;
	struct completion completed_11d_scan;
	enum ath12k_11d_state state_11d;
	u8 alpha2[REG_ALPHA2_LEN];
	bool regdom_set_by_user;

	struct completion fw_stats_complete;

	struct completion mlo_setup_done;
	u32 mlo_setup_status;
	u8 ftm_msgref;
	struct ath12k_fw_stats fw_stats;
	unsigned long last_tx_power_update;
};

struct ath12k_hw {
	struct ieee80211_hw *hw;
	struct device *dev;

	/* Protect the write operation of the hardware state ath12k_hw::state
	 * between hardware start<=>reconfigure<=>stop transitions.
	 */
	struct mutex hw_mutex;
	enum ath12k_hw_state state;
	bool regd_updated;
	bool use_6ghz_regd;

	u8 num_radio;

	DECLARE_BITMAP(free_ml_peer_id_map, ATH12K_MAX_MLO_PEERS);

	/* protected by wiphy_lock() */
	struct list_head ml_peers;

	/* Keep last */
	struct ath12k radio[] __aligned(sizeof(void *));
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
	u32 eml_cap;
	u32 mld_cap;
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
	u32 hw_link_id;
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

struct ath12k_mlo_memory {
	struct target_mem_chunk chunk[ATH12K_QMI_WLANFW_MAX_NUM_MEM_SEG_V01];
	int mlo_mem_size;
	bool init_done;
};

struct ath12k_hw_link {
	u8 device_id;
	u8 pdev_idx;
};

/* Holds info on the group of devices that are registered as a single
 * wiphy, protected with struct ath12k_hw_group::mutex.
 */
struct ath12k_hw_group {
	struct list_head list;
	u8 id;
	u8 num_devices;
	u8 num_probed;
	u8 num_started;
	unsigned long flags;
	struct ath12k_base *ab[ATH12K_MAX_SOCS];

	/* protects access to this struct */
	struct mutex mutex;

	/* Holds information of wiphy (hw) registration.
	 *
	 * In Multi/Single Link Operation case, all pdevs are registered as
	 * a single wiphy. In other (legacy/Non-MLO) cases, each pdev is
	 * registered as separate wiphys.
	 */
	struct ath12k_hw *ah[ATH12K_GROUP_MAX_RADIO];
	u8 num_hw;
	bool mlo_capable;
	struct device_node *wsi_node[ATH12K_MAX_SOCS];
	struct ath12k_mlo_memory mlo_mem;
	struct ath12k_hw_link hw_links[ATH12K_GROUP_MAX_RADIO];
	bool hw_link_id_init_done;
};

/* Holds WSI info specific to each device, excluding WSI group info */
struct ath12k_wsi_info {
	u32 index;
	u32 hw_link_id_base;
};

/* Master structure to hold the hw data which may be used in core module */
struct ath12k_base {
	enum ath12k_hw_rev hw_rev;
	struct platform_device *pdev;
	struct device *dev;
	struct ath12k_qmi qmi;
	struct ath12k_wmi_base wmi_ab;
	struct completion fw_ready;
	u8 device_id;
	int num_radios;
	/* HW channel counters frequency value in hertz common to all MACs */
	u32 cc_freq_hz;

	struct ath12k_dump_file_data *dump_data;
	size_t ath12k_coredump_len;
	struct work_struct dump_work;

	struct ath12k_htc htc;

	struct ath12k_dp dp;

	void __iomem *mem;
	unsigned long mem_len;

	void __iomem *mem_ce;
	u32 ce_remap_base_addr;
	bool ce_remap;

	struct {
		enum ath12k_bus bus;
		const struct ath12k_hif_ops *ops;
	} hif;

	struct {
		struct completion wakeup_completed;
		u32 wmi_conf_rx_decap_mode;
	} wow;

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
#ifdef CONFIG_ATH12K_DEBUGFS
	struct dentry *debugfs_soc;
#endif

	unsigned long dev_flags;
	struct completion driver_recovery;
	struct workqueue_struct *workqueue;
	struct work_struct restart_work;
	struct workqueue_struct *workqueue_aux;
	struct work_struct reset_work;
	atomic_t reset_count;
	atomic_t recovery_count;
	bool is_reset;
	struct completion reset_complete;
	/* continuous recovery fail count */
	atomic_t fail_cont_count;
	unsigned long reset_fail_timeout;
	struct work_struct update_11d_work;
	u8 new_alpha2[2];
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

	struct {
		enum ath12k_bdf_search bdf_search;
		u32 vendor;
		u32 device;
		u32 subsystem_vendor;
		u32 subsystem_device;
	} id;

	struct {
		u32 api_version;

		const struct firmware *fw;
		const u8 *amss_data;
		size_t amss_len;
		const u8 *amss_dualmac_data;
		size_t amss_dualmac_len;
		const u8 *m3_data;
		size_t m3_len;

		DECLARE_BITMAP(fw_features, ATH12K_FW_FEATURE_COUNT);
	} fw;

	const struct hal_rx_ops *hal_rx_ops;

	struct completion restart_completed;

#ifdef CONFIG_ACPI

	struct {
		bool started;
		u32 func_bit;
		bool acpi_tas_enable;
		bool acpi_bios_sar_enable;
		bool acpi_disable_11be;
		bool acpi_disable_rfkill;
		bool acpi_cca_enable;
		bool acpi_band_edge_enable;
		bool acpi_enable_bdf;
		u32 bit_flag;
		char bdf_string[ATH12K_ACPI_BDF_MAX_LEN];
		u8 tas_cfg[ATH12K_ACPI_DSM_TAS_CFG_SIZE];
		u8 tas_sar_power_table[ATH12K_ACPI_DSM_TAS_DATA_SIZE];
		u8 bios_sar_data[ATH12K_ACPI_DSM_BIOS_SAR_DATA_SIZE];
		u8 geo_offset_data[ATH12K_ACPI_DSM_GEO_OFFSET_DATA_SIZE];
		u8 cca_data[ATH12K_ACPI_DSM_CCA_DATA_SIZE];
		u8 band_edge_power[ATH12K_ACPI_DSM_BAND_EDGE_DATA_SIZE];
	} acpi;

#endif /* CONFIG_ACPI */

	struct notifier_block panic_nb;

	struct ath12k_hw_group *ag;
	struct ath12k_wsi_info wsi_info;
	enum ath12k_firmware_mode fw_mode;
	struct ath12k_ftm_event_obj ftm_event_obj;

	/* must be last */
	u8 drv_priv[] __aligned(sizeof(void *));
};

struct ath12k_pdev_map {
	struct ath12k_base *ab;
	u8 pdev_idx;
};

struct ath12k_fw_stats_vdev {
	struct list_head list;

	u32 vdev_id;
	u32 beacon_snr;
	u32 data_snr;
	u32 num_tx_frames[WLAN_MAX_AC];
	u32 num_rx_frames;
	u32 num_tx_frames_retries[WLAN_MAX_AC];
	u32 num_tx_frames_failures[WLAN_MAX_AC];
	u32 num_rts_fail;
	u32 num_rts_success;
	u32 num_rx_err;
	u32 num_rx_discard;
	u32 num_tx_not_acked;
	u32 tx_rate_history[MAX_TX_RATE_VALUES];
	u32 beacon_rssi_history[MAX_TX_RATE_VALUES];
};

struct ath12k_fw_stats_bcn {
	struct list_head list;

	u32 vdev_id;
	u32 tx_bcn_succ_cnt;
	u32 tx_bcn_outage_cnt;
};

struct ath12k_fw_stats_pdev {
	struct list_head list;

	/* PDEV stats */
	s32 ch_noise_floor;
	u32 tx_frame_count;
	u32 rx_frame_count;
	u32 rx_clear_count;
	u32 cycle_count;
	u32 phy_err_count;
	u32 chan_tx_power;
	u32 ack_rx_bad;
	u32 rts_bad;
	u32 rts_good;
	u32 fcs_bad;
	u32 no_beacons;
	u32 mib_int_count;

	/* PDEV TX stats */
	s32 comp_queued;
	s32 comp_delivered;
	s32 msdu_enqued;
	s32 mpdu_enqued;
	s32 wmm_drop;
	s32 local_enqued;
	s32 local_freed;
	s32 hw_queued;
	s32 hw_reaped;
	s32 underrun;
	s32 tx_abort;
	s32 mpdus_requed;
	u32 tx_ko;
	u32 data_rc;
	u32 self_triggers;
	u32 sw_retry_failure;
	u32 illgl_rate_phy_err;
	u32 pdev_cont_xretry;
	u32 pdev_tx_timeout;
	u32 pdev_resets;
	u32 stateless_tid_alloc_failure;
	u32 phy_underrun;
	u32 txop_ovf;

	/* PDEV RX stats */
	s32 mid_ppdu_route_change;
	s32 status_rcvd;
	s32 r0_frags;
	s32 r1_frags;
	s32 r2_frags;
	s32 r3_frags;
	s32 htt_msdus;
	s32 htt_mpdus;
	s32 loc_msdus;
	s32 loc_mpdus;
	s32 oversize_amsdu;
	s32 phy_errs;
	s32 phy_err_drop;
	s32 mpdu_errs;
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
int ath12k_core_fetch_regdb(struct ath12k_base *ab, struct ath12k_board_data *bd);
int ath12k_core_check_dt(struct ath12k_base *ath12k);
int ath12k_core_check_smbios(struct ath12k_base *ab);
void ath12k_core_halt(struct ath12k *ar);
int ath12k_core_resume_early(struct ath12k_base *ab);
int ath12k_core_resume(struct ath12k_base *ab);
int ath12k_core_suspend(struct ath12k_base *ab);
int ath12k_core_suspend_late(struct ath12k_base *ab);
void ath12k_core_hw_group_unassign(struct ath12k_base *ab);
u8 ath12k_get_num_partner_link(struct ath12k *ar);

const struct firmware *ath12k_core_firmware_request(struct ath12k_base *ab,
						    const char *filename);
u32 ath12k_core_get_max_station_per_radio(struct ath12k_base *ab);
u32 ath12k_core_get_max_peers_per_radio(struct ath12k_base *ab);
u32 ath12k_core_get_max_num_tids(struct ath12k_base *ab);

void ath12k_core_hw_group_set_mlo_capable(struct ath12k_hw_group *ag);
void ath12k_fw_stats_init(struct ath12k *ar);
void ath12k_fw_stats_bcn_free(struct list_head *head);
void ath12k_fw_stats_free(struct ath12k_fw_stats *stats);
void ath12k_fw_stats_reset(struct ath12k *ar);
struct reserved_mem *ath12k_core_get_reserved_mem(struct ath12k_base *ab,
						  int index);

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

static inline struct ath12k_vif *ath12k_vif_to_ahvif(struct ieee80211_vif *vif)
{
	return (struct ath12k_vif *)vif->drv_priv;
}

static inline struct ath12k_sta *ath12k_sta_to_ahsta(struct ieee80211_sta *sta)
{
	return (struct ath12k_sta *)sta->drv_priv;
}

static inline struct ieee80211_sta *ath12k_ahsta_to_sta(struct ath12k_sta *ahsta)
{
	return container_of((void *)ahsta, struct ieee80211_sta, drv_priv);
}

static inline struct ieee80211_vif *ath12k_ahvif_to_vif(struct ath12k_vif *ahvif)
{
	return container_of((void *)ahvif, struct ieee80211_vif, drv_priv);
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
	case ATH12K_BUS_AHB:
		return "ahb";
	}

	return "unknown";
}

static inline struct ath12k_hw *ath12k_hw_to_ah(struct ieee80211_hw  *hw)
{
	return hw->priv;
}

static inline struct ath12k *ath12k_ah_to_ar(struct ath12k_hw *ah, u8 hw_link_id)
{
	if (WARN(hw_link_id >= ah->num_radio,
		 "bad hw link id %d, so switch to default link\n", hw_link_id))
		hw_link_id = 0;

	return &ah->radio[hw_link_id];
}

static inline struct ath12k_hw *ath12k_ar_to_ah(struct ath12k *ar)
{
	return ar->ah;
}

static inline struct ieee80211_hw *ath12k_ar_to_hw(struct ath12k *ar)
{
	return ar->ah->hw;
}

#define for_each_ar(ah, ar, index) \
	for ((index) = 0; ((index) < (ah)->num_radio && \
	     ((ar) = &(ah)->radio[(index)])); (index)++)

static inline struct ath12k_hw *ath12k_ag_to_ah(struct ath12k_hw_group *ag, int idx)
{
	return ag->ah[idx];
}

static inline void ath12k_ag_set_ah(struct ath12k_hw_group *ag, int idx,
				    struct ath12k_hw *ah)
{
	ag->ah[idx] = ah;
}

static inline struct ath12k_hw_group *ath12k_ab_to_ag(struct ath12k_base *ab)
{
	return ab->ag;
}

static inline void ath12k_core_started(struct ath12k_base *ab)
{
	lockdep_assert_held(&ab->ag->mutex);

	ab->ag->num_started++;
}

static inline void ath12k_core_stopped(struct ath12k_base *ab)
{
	lockdep_assert_held(&ab->ag->mutex);

	ab->ag->num_started--;
}

static inline struct ath12k_base *ath12k_ag_to_ab(struct ath12k_hw_group *ag,
						  u8 device_id)
{
	return ag->ab[device_id];
}

#endif /* _CORE_H_ */
