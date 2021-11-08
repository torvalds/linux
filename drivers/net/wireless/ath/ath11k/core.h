/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) 2018-2019 The Linux Foundation. All rights reserved.
 */

#ifndef ATH11K_CORE_H
#define ATH11K_CORE_H

#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/bitfield.h>
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
#include "thermal.h"
#include "dbring.h"
#include "spectral.h"

#define SM(_v, _f) (((_v) << _f##_LSB) & _f##_MASK)

#define ATH11K_TX_MGMT_NUM_PENDING_MAX	512

#define ATH11K_TX_MGMT_TARGET_MAX_SUPPORT_WMI 64

/* Pending management packets threshold for dropping probe responses */
#define ATH11K_PRB_RSP_DROP_THRESHOLD ((ATH11K_TX_MGMT_TARGET_MAX_SUPPORT_WMI * 3) / 4)

#define ATH11K_INVALID_HW_MAC_ID	0xFF
#define ATH11K_CONNECTION_LOSS_HZ	(3 * HZ)

extern unsigned int ath11k_frame_mode;

#define ATH11K_MON_TIMER_INTERVAL  10

enum ath11k_supported_bw {
	ATH11K_BW_20	= 0,
	ATH11K_BW_40	= 1,
	ATH11K_BW_80	= 2,
	ATH11K_BW_160	= 3,
};

enum wme_ac {
	WME_AC_BE,
	WME_AC_BK,
	WME_AC_VI,
	WME_AC_VO,
	WME_NUM_AC
};

#define ATH11K_HT_MCS_MAX	7
#define ATH11K_VHT_MCS_MAX	9
#define ATH11K_HE_MCS_MAX	11

enum ath11k_crypt_mode {
	/* Only use hardware crypto engine */
	ATH11K_CRYPT_MODE_HW,
	/* Only use software crypto */
	ATH11K_CRYPT_MODE_SW,
};

static inline enum wme_ac ath11k_tid_to_ac(u32 tid)
{
	return (((tid == 0) || (tid == 3)) ? WME_AC_BE :
		((tid == 1) || (tid == 2)) ? WME_AC_BK :
		((tid == 4) || (tid == 5)) ? WME_AC_VI :
		WME_AC_VO);
}

enum ath11k_skb_flags {
	ATH11K_SKB_HW_80211_ENCAP = BIT(0),
	ATH11K_SKB_CIPHER_SET = BIT(1),
};

struct ath11k_skb_cb {
	dma_addr_t paddr;
	u8 eid;
	u8 flags;
	u32 cipher;
	struct ath11k *ar;
	struct ieee80211_vif *vif;
} __packed;

struct ath11k_skb_rxcb {
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
	u16 seq_no;
};

enum ath11k_hw_rev {
	ATH11K_HW_IPQ8074,
	ATH11K_HW_QCA6390_HW20,
	ATH11K_HW_IPQ6018_HW10,
	ATH11K_HW_QCN9074_HW10,
	ATH11K_HW_WCN6855_HW20,
};

enum ath11k_firmware_mode {
	/* the default mode, standard 802.11 functionality */
	ATH11K_FIRMWARE_MODE_NORMAL,

	/* factory tests etc */
	ATH11K_FIRMWARE_MODE_FTM,

	/* Cold boot calibration */
	ATH11K_FIRMWARE_MODE_COLD_BOOT = 7,
};

extern bool ath11k_cold_boot_cal;

#define ATH11K_IRQ_NUM_MAX 52
#define ATH11K_EXT_IRQ_NUM_MAX	16

struct ath11k_ext_irq_grp {
	struct ath11k_base *ab;
	u32 irqs[ATH11K_EXT_IRQ_NUM_MAX];
	u32 num_irq;
	u32 grp_id;
	u64 timestamp;
	struct napi_struct napi;
	struct net_device napi_ndev;
};

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

struct ath11k_he {
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

enum ath11k_scan_state {
	ATH11K_SCAN_IDLE,
	ATH11K_SCAN_STARTING,
	ATH11K_SCAN_RUNNING,
	ATH11K_SCAN_ABORTING,
};

enum ath11k_dev_flags {
	ATH11K_CAC_RUNNING,
	ATH11K_FLAG_CORE_REGISTERED,
	ATH11K_FLAG_CRASH_FLUSH,
	ATH11K_FLAG_RAW_MODE,
	ATH11K_FLAG_HW_CRYPTO_DISABLED,
	ATH11K_FLAG_BTCOEX,
	ATH11K_FLAG_RECOVERY,
	ATH11K_FLAG_UNREGISTERING,
	ATH11K_FLAG_REGISTERED,
	ATH11K_FLAG_QMI_FAIL,
	ATH11K_FLAG_HTC_SUSPEND_COMPLETE,
};

enum ath11k_monitor_flags {
	ATH11K_FLAG_MONITOR_CONF_ENABLED,
	ATH11K_FLAG_MONITOR_STARTED,
	ATH11K_FLAG_MONITOR_VDEV_CREATED,
};

struct ath11k_vif {
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

	struct ath11k *ar;
	struct ieee80211_vif *vif;

	u16 tx_seq_no;
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
	bool spectral_enabled;
	bool ps;
	u32 aid;
	u8 bssid[ETH_ALEN];
	struct cfg80211_bitrate_mask bitrate_mask;
	struct delayed_work connection_loss_work;
	int num_legacy_stations;
	int rtscts_prot_mode;
	int txpower;
	bool rsnie_present;
	bool wpaie_present;
	struct ieee80211_chanctx_conf chanctx;
};

struct ath11k_vif_iter {
	u32 vdev_id;
	struct ath11k_vif *arvif;
};

struct ath11k_rx_peer_stats {
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
};

#define ATH11K_HE_MCS_NUM       12
#define ATH11K_VHT_MCS_NUM      10
#define ATH11K_BW_NUM           4
#define ATH11K_NSS_NUM          4
#define ATH11K_LEGACY_NUM       12
#define ATH11K_GI_NUM           4
#define ATH11K_HT_MCS_NUM       32

enum ath11k_pkt_rx_err {
	ATH11K_PKT_RX_ERR_FCS,
	ATH11K_PKT_RX_ERR_TKIP,
	ATH11K_PKT_RX_ERR_CRYPT,
	ATH11K_PKT_RX_ERR_PEER_IDX_INVAL,
	ATH11K_PKT_RX_ERR_MAX,
};

enum ath11k_ampdu_subfrm_num {
	ATH11K_AMPDU_SUBFRM_NUM_10,
	ATH11K_AMPDU_SUBFRM_NUM_20,
	ATH11K_AMPDU_SUBFRM_NUM_30,
	ATH11K_AMPDU_SUBFRM_NUM_40,
	ATH11K_AMPDU_SUBFRM_NUM_50,
	ATH11K_AMPDU_SUBFRM_NUM_60,
	ATH11K_AMPDU_SUBFRM_NUM_MORE,
	ATH11K_AMPDU_SUBFRM_NUM_MAX,
};

enum ath11k_amsdu_subfrm_num {
	ATH11K_AMSDU_SUBFRM_NUM_1,
	ATH11K_AMSDU_SUBFRM_NUM_2,
	ATH11K_AMSDU_SUBFRM_NUM_3,
	ATH11K_AMSDU_SUBFRM_NUM_4,
	ATH11K_AMSDU_SUBFRM_NUM_MORE,
	ATH11K_AMSDU_SUBFRM_NUM_MAX,
};

enum ath11k_counter_type {
	ATH11K_COUNTER_TYPE_BYTES,
	ATH11K_COUNTER_TYPE_PKTS,
	ATH11K_COUNTER_TYPE_MAX,
};

enum ath11k_stats_type {
	ATH11K_STATS_TYPE_SUCC,
	ATH11K_STATS_TYPE_FAIL,
	ATH11K_STATS_TYPE_RETRY,
	ATH11K_STATS_TYPE_AMPDU,
	ATH11K_STATS_TYPE_MAX,
};

struct ath11k_htt_data_stats {
	u64 legacy[ATH11K_COUNTER_TYPE_MAX][ATH11K_LEGACY_NUM];
	u64 ht[ATH11K_COUNTER_TYPE_MAX][ATH11K_HT_MCS_NUM];
	u64 vht[ATH11K_COUNTER_TYPE_MAX][ATH11K_VHT_MCS_NUM];
	u64 he[ATH11K_COUNTER_TYPE_MAX][ATH11K_HE_MCS_NUM];
	u64 bw[ATH11K_COUNTER_TYPE_MAX][ATH11K_BW_NUM];
	u64 nss[ATH11K_COUNTER_TYPE_MAX][ATH11K_NSS_NUM];
	u64 gi[ATH11K_COUNTER_TYPE_MAX][ATH11K_GI_NUM];
};

struct ath11k_htt_tx_stats {
	struct ath11k_htt_data_stats stats[ATH11K_STATS_TYPE_MAX];
	u64 tx_duration;
	u64 ba_fails;
	u64 ack_fails;
};

struct ath11k_per_ppdu_tx_stats {
	u16 succ_pkts;
	u16 failed_pkts;
	u16 retry_pkts;
	u32 succ_bytes;
	u32 failed_bytes;
	u32 retry_bytes;
};

struct ath11k_sta {
	struct ath11k_vif *arvif;

	/* the following are protected by ar->data_lock */
	u32 changed; /* IEEE80211_RC_* */
	u32 bw;
	u32 nss;
	u32 smps;
	enum hal_pn_type pn_type;

	struct work_struct update_wk;
	struct work_struct set_4addr_wk;
	struct rate_info txrate;
	struct rate_info last_txrate;
	u64 rx_duration;
	u64 tx_duration;
	u8 rssi_comb;
	struct ath11k_htt_tx_stats *tx_stats;
	struct ath11k_rx_peer_stats *rx_stats;

#ifdef CONFIG_MAC80211_DEBUGFS
	/* protected by conf_mutex */
	bool aggr_mode;
#endif

	bool use_4addr_set;
	u16 tcl_metadata;
};

#define ATH11K_MIN_5G_FREQ 4150
#define ATH11K_MIN_6G_FREQ 5925
#define ATH11K_MAX_6G_FREQ 7115
#define ATH11K_NUM_CHANS 101
#define ATH11K_MAX_5G_CHAN 173

enum ath11k_state {
	ATH11K_STATE_OFF,
	ATH11K_STATE_ON,
	ATH11K_STATE_RESTARTING,
	ATH11K_STATE_RESTARTED,
	ATH11K_STATE_WEDGED,
	/* Add other states as required */
};

/* Antenna noise floor */
#define ATH11K_DEFAULT_NOISE_FLOOR -95

struct ath11k_fw_stats {
	struct dentry *debugfs_fwstats;
	u32 pdev_id;
	u32 stats_id;
	struct list_head pdevs;
	struct list_head vdevs;
	struct list_head bcn;
};

struct ath11k_dbg_htt_stats {
	u8 type;
	u8 reset;
	struct debug_htt_stats_req *stats_req;
	/* protects shared stats req buffer */
	spinlock_t lock;
};

struct ath11k_debug {
	struct dentry *debugfs_pdev;
	struct ath11k_dbg_htt_stats htt_stats;
	u32 extd_tx_stats;
	struct ath11k_fw_stats fw_stats;
	struct completion fw_stats_complete;
	bool fw_stats_done;
	u32 extd_rx_stats;
	u32 pktlog_filter;
	u32 pktlog_mode;
	u32 pktlog_peer_valid;
	u8 pktlog_peer_addr[ETH_ALEN];
	u32 rx_filter;
};

struct ath11k_per_peer_tx_stats {
	u32 succ_bytes;
	u32 retry_bytes;
	u32 failed_bytes;
	u16 succ_pkts;
	u16 retry_pkts;
	u16 failed_pkts;
	u32 duration;
	u8 ba_fails;
	bool is_ampdu;
};

#define ATH11K_FLUSH_TIMEOUT (5 * HZ)
#define ATH11K_VDEV_DELETE_TIMEOUT_HZ (5 * HZ)

struct ath11k {
	struct ath11k_base *ab;
	struct ath11k_pdev *pdev;
	struct ieee80211_hw *hw;
	struct ieee80211_ops *ops;
	struct ath11k_pdev_wmi *wmi;
	struct ath11k_pdev_dp dp;
	u8 mac_addr[ETH_ALEN];
	u32 ht_cap_info;
	u32 vht_cap_info;
	struct ath11k_he ar_he;
	enum ath11k_state state;
	bool supports_6ghz;
	struct {
		struct completion started;
		struct completion completed;
		struct completion on_channel;
		struct delayed_work timeout;
		enum ath11k_scan_state state;
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
	/* To synchronize concurrent synchronous mac80211 callback operations,
	 * concurrent debugfs configuration and concurrent FW statistics events.
	 */
	struct mutex conf_mutex;
	/* protects the radio specific data like debug stats, ppdu_stats_info stats,
	 * vdev_stop_status info, scan data, ath11k_sta info, ath11k_vif info,
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
	struct survey_info survey[ATH11K_NUM_CHANS];
	struct completion bss_survey_done;

	struct work_struct regd_update_work;

	struct work_struct wmi_mgmt_tx_work;
	struct sk_buff_head wmi_mgmt_tx_queue;

	struct ath11k_per_peer_tx_stats peer_tx_stats;
	struct list_head ppdu_stats_info;
	u32 ppdu_stat_list_depth;

	struct ath11k_per_peer_tx_stats cached_stats;
	u32 last_ppdu_id;
	u32 cached_ppdu_id;
	int monitor_vdev_id;
#ifdef CONFIG_ATH11K_DEBUGFS
	struct ath11k_debug debug;
#endif
#ifdef CONFIG_ATH11K_SPECTRAL
	struct ath11k_spectral spectral;
#endif
	bool dfs_block_radar_events;
	struct ath11k_thermal thermal;
};

struct ath11k_band_cap {
	u32 phy_id;
	u32 max_bw_supported;
	u32 ht_cap_info;
	u32 he_cap_info[2];
	u32 he_mcs;
	u32 he_cap_phy_info[PSOC_HOST_MAX_PHY_SIZE];
	struct ath11k_ppe_threshold he_ppet;
	u16 he_6ghz_capa;
};

struct ath11k_pdev_cap {
	u32 supported_bands;
	u32 ampdu_density;
	u32 vht_cap;
	u32 vht_mcs;
	u32 he_mcs;
	u32 tx_chain_mask;
	u32 rx_chain_mask;
	u32 tx_chain_mask_shift;
	u32 rx_chain_mask_shift;
	struct ath11k_band_cap band[NUM_NL80211_BANDS];
	bool nss_ratio_enabled;
	u8 nss_ratio_info;
};

struct ath11k_pdev {
	struct ath11k *ar;
	u32 pdev_id;
	struct ath11k_pdev_cap cap;
	u8 mac_addr[ETH_ALEN];
};

struct ath11k_board_data {
	const struct firmware *fw;
	const void *data;
	size_t len;
};

struct ath11k_bus_params {
	bool mhi_support;
	bool m3_fw_support;
	bool fixed_bdf_addr;
	bool fixed_mem_region;
	bool static_window_map;
};

/* IPQ8074 HW channel counters frequency value in hertz */
#define IPQ8074_CC_FREQ_HERTZ 320000

struct ath11k_bp_stats {
	/* Head Pointer reported by the last HTT Backpressure event for the ring */
	u16 hp;

	/* Tail Pointer reported by the last HTT Backpressure event for the ring */
	u16 tp;

	/* Number of Backpressure events received for the ring */
	u32 count;

	/* Last recorded event timestamp */
	unsigned long jiffies;
};

struct ath11k_dp_ring_bp_stats {
	struct ath11k_bp_stats umac_ring_bp_stats[HTT_SW_UMAC_RING_IDX_MAX];
	struct ath11k_bp_stats lmac_ring_bp_stats[HTT_SW_LMAC_RING_IDX_MAX][MAX_RADIOS];
};

struct ath11k_soc_dp_tx_err_stats {
	/* TCL Ring Descriptor unavailable */
	u32 desc_na[DP_TCL_NUM_RING_MAX];
	/* Other failures during dp_tx due to mem allocation failure
	 * idr unavailable etc.
	 */
	atomic_t misc_fail;
};

struct ath11k_soc_dp_stats {
	u32 err_ring_pkts;
	u32 invalid_rbm;
	u32 rxdma_error[HAL_REO_ENTR_RING_RXDMA_ECODE_MAX];
	u32 reo_error[HAL_REO_DEST_RING_ERROR_CODE_MAX];
	u32 hal_reo_error[DP_REO_DST_RING_MAX];
	struct ath11k_soc_dp_tx_err_stats tx_err;
	struct ath11k_dp_ring_bp_stats bp_stats;
};

/* Master structure to hold the hw data which may be used in core module */
struct ath11k_base {
	enum ath11k_hw_rev hw_rev;
	struct platform_device *pdev;
	struct device *dev;
	struct ath11k_qmi qmi;
	struct ath11k_wmi_base wmi_ab;
	struct completion fw_ready;
	int num_radios;
	/* HW channel counters frequency value in hertz common to all MACs */
	u32 cc_freq_hz;

	struct ath11k_htc htc;

	struct ath11k_dp dp;

	void __iomem *mem;
	unsigned long mem_len;

	struct {
		enum ath11k_bus bus;
		const struct ath11k_hif_ops *ops;
	} hif;

	struct {
		struct completion wakeup_completed;
	} wow;

	struct ath11k_ce ce;
	struct timer_list rx_replenish_retry;
	struct ath11k_hal hal;
	/* To synchronize core_start/core_stop */
	struct mutex core_lock;
	/* Protects data like peers */
	spinlock_t base_lock;
	struct ath11k_pdev pdevs[MAX_RADIOS];
	struct ath11k_pdev __rcu *pdevs_active[MAX_RADIOS];
	struct ath11k_hal_reg_capabilities_ext hal_reg_cap[MAX_RADIOS];
	unsigned long long free_vdev_map;
	struct list_head peers;
	wait_queue_head_t peer_mapping_wq;
	u8 mac_addr[ETH_ALEN];
	bool wmi_ready;
	u32 wlan_init_status;
	int irq_num[ATH11K_IRQ_NUM_MAX];
	struct ath11k_ext_irq_grp ext_irq_grp[ATH11K_EXT_IRQ_GRP_NUM_MAX];
	struct napi_struct *napi;
	struct ath11k_targ_cap target_caps;
	u32 ext_service_bitmap[WMI_SERVICE_EXT_BM_SIZE];
	bool pdevs_macaddr_valid;
	int bd_api;

	struct ath11k_hw_params hw_params;
	struct ath11k_bus_params bus_params;

	const struct firmware *cal_file;

	/* Below regd's are protected by ab->data_lock */
	/* This is the regd set for every radio
	 * by the firmware during initializatin
	 */
	struct ieee80211_regdomain *default_regd[MAX_RADIOS];
	/* This regd is set during dynamic country setting
	 * This may or may not be used during the runtime
	 */
	struct ieee80211_regdomain *new_regd[MAX_RADIOS];

	/* Current DFS Regulatory */
	enum ath11k_dfs_region dfs_region;
#ifdef CONFIG_ATH11K_DEBUGFS
	struct dentry *debugfs_soc;
	struct dentry *debugfs_ath11k;
#endif
	struct ath11k_soc_dp_stats soc_stats;

	unsigned long dev_flags;
	struct completion driver_recovery;
	struct workqueue_struct *workqueue;
	struct work_struct restart_work;
	struct {
		/* protected by data_lock */
		u32 fw_crash_counter;
	} stats;
	u32 pktlog_defs_checksum;

	struct ath11k_dbring_cap *db_caps;
	u32 num_db_cap;

	struct timer_list mon_reap_timer;

	struct completion htc_suspend;

	/* must be last */
	u8 drv_priv[0] __aligned(sizeof(void *));
};

struct ath11k_fw_stats_pdev {
	struct list_head list;

	/* PDEV stats */
	s32 ch_noise_floor;
	/* Cycles spent transmitting frames */
	u32 tx_frame_count;
	/* Cycles spent receiving frames */
	u32 rx_frame_count;
	/* Total channel busy time, evidently */
	u32 rx_clear_count;
	/* Total on-channel time */
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
	/* Num HTT cookies queued to dispatch list */
	s32 comp_queued;
	/* Num HTT cookies dispatched */
	s32 comp_delivered;
	/* Num MSDU queued to WAL */
	s32 msdu_enqued;
	/* Num MPDU queue to WAL */
	s32 mpdu_enqued;
	/* Num MSDUs dropped by WMM limit */
	s32 wmm_drop;
	/* Num Local frames queued */
	s32 local_enqued;
	/* Num Local frames done */
	s32 local_freed;
	/* Num queued to HW */
	s32 hw_queued;
	/* Num PPDU reaped from HW */
	s32 hw_reaped;
	/* Num underruns */
	s32 underrun;
	/* Num hw paused */
	u32 hw_paused;
	/* Num PPDUs cleaned up in TX abort */
	s32 tx_abort;
	/* Num MPDUs requeued by SW */
	s32 mpdus_requeued;
	/* excessive retries */
	u32 tx_ko;
	u32 tx_xretry;
	/* data hw rate code */
	u32 data_rc;
	/* Scheduler self triggers */
	u32 self_triggers;
	/* frames dropped due to excessive sw retries */
	u32 sw_retry_failure;
	/* illegal rate phy errors	*/
	u32 illgl_rate_phy_err;
	/* wal pdev continuous xretry */
	u32 pdev_cont_xretry;
	/* wal pdev tx timeouts */
	u32 pdev_tx_timeout;
	/* wal pdev resets */
	u32 pdev_resets;
	/* frames dropped due to non-availability of stateless TIDs */
	u32 stateless_tid_alloc_failure;
	/* PhY/BB underrun */
	u32 phy_underrun;
	/* MPDU is more than txop limit */
	u32 txop_ovf;
	/* Num sequences posted */
	u32 seq_posted;
	/* Num sequences failed in queueing */
	u32 seq_failed_queueing;
	/* Num sequences completed */
	u32 seq_completed;
	/* Num sequences restarted */
	u32 seq_restarted;
	/* Num of MU sequences posted */
	u32 mu_seq_posted;
	/* Num MPDUs flushed by SW, HWPAUSED, SW TXABORT
	 * (Reset,channel change)
	 */
	s32 mpdus_sw_flush;
	/* Num MPDUs filtered by HW, all filter condition (TTL expired) */
	s32 mpdus_hw_filter;
	/* Num MPDUs truncated by PDG (TXOP, TBTT,
	 * PPDU_duration based on rate, dyn_bw)
	 */
	s32 mpdus_truncated;
	/* Num MPDUs that was tried but didn't receive ACK or BA */
	s32 mpdus_ack_failed;
	/* Num MPDUs that was dropped du to expiry. */
	s32 mpdus_expired;

	/* PDEV RX stats */
	/* Cnts any change in ring routing mid-ppdu */
	s32 mid_ppdu_route_change;
	/* Total number of statuses processed */
	s32 status_rcvd;
	/* Extra frags on rings 0-3 */
	s32 r0_frags;
	s32 r1_frags;
	s32 r2_frags;
	s32 r3_frags;
	/* MSDUs / MPDUs delivered to HTT */
	s32 htt_msdus;
	s32 htt_mpdus;
	/* MSDUs / MPDUs delivered to local stack */
	s32 loc_msdus;
	s32 loc_mpdus;
	/* AMSDUs that have more MSDUs than the status ring size */
	s32 oversize_amsdu;
	/* Number of PHY errors */
	s32 phy_errs;
	/* Number of PHY errors drops */
	s32 phy_err_drop;
	/* Number of mpdu errors - FCS, MIC, ENC etc. */
	s32 mpdu_errs;
	/* Num overflow errors */
	s32 rx_ovfl_errs;
};

struct ath11k_fw_stats_vdev {
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

struct ath11k_fw_stats_bcn {
	struct list_head list;

	u32 vdev_id;
	u32 tx_bcn_succ_cnt;
	u32 tx_bcn_outage_cnt;
};

extern const struct ce_pipe_config ath11k_target_ce_config_wlan_ipq8074[];
extern const struct service_to_pipe ath11k_target_service_to_ce_map_wlan_ipq8074[];
extern const struct service_to_pipe ath11k_target_service_to_ce_map_wlan_ipq6018[];

extern const struct ce_pipe_config ath11k_target_ce_config_wlan_qca6390[];
extern const struct service_to_pipe ath11k_target_service_to_ce_map_wlan_qca6390[];

extern const struct ce_pipe_config ath11k_target_ce_config_wlan_qcn9074[];
extern const struct service_to_pipe ath11k_target_service_to_ce_map_wlan_qcn9074[];
int ath11k_core_qmi_firmware_ready(struct ath11k_base *ab);
int ath11k_core_pre_init(struct ath11k_base *ab);
int ath11k_core_init(struct ath11k_base *ath11k);
void ath11k_core_deinit(struct ath11k_base *ath11k);
struct ath11k_base *ath11k_core_alloc(struct device *dev, size_t priv_size,
				      enum ath11k_bus bus,
				      const struct ath11k_bus_params *bus_params);
void ath11k_core_free(struct ath11k_base *ath11k);
int ath11k_core_fetch_bdf(struct ath11k_base *ath11k,
			  struct ath11k_board_data *bd);
void ath11k_core_free_bdf(struct ath11k_base *ab, struct ath11k_board_data *bd);
int ath11k_core_check_dt(struct ath11k_base *ath11k);

void ath11k_core_halt(struct ath11k *ar);
int ath11k_core_resume(struct ath11k_base *ab);
int ath11k_core_suspend(struct ath11k_base *ab);

const struct firmware *ath11k_core_firmware_request(struct ath11k_base *ab,
						    const char *filename);

static inline const char *ath11k_scan_state_str(enum ath11k_scan_state state)
{
	switch (state) {
	case ATH11K_SCAN_IDLE:
		return "idle";
	case ATH11K_SCAN_STARTING:
		return "starting";
	case ATH11K_SCAN_RUNNING:
		return "running";
	case ATH11K_SCAN_ABORTING:
		return "aborting";
	}

	return "unknown";
}

static inline struct ath11k_skb_cb *ATH11K_SKB_CB(struct sk_buff *skb)
{
	BUILD_BUG_ON(sizeof(struct ath11k_skb_cb) >
		     IEEE80211_TX_INFO_DRIVER_DATA_SIZE);
	return (struct ath11k_skb_cb *)&IEEE80211_SKB_CB(skb)->driver_data;
}

static inline struct ath11k_skb_rxcb *ATH11K_SKB_RXCB(struct sk_buff *skb)
{
	BUILD_BUG_ON(sizeof(struct ath11k_skb_rxcb) > sizeof(skb->cb));
	return (struct ath11k_skb_rxcb *)skb->cb;
}

static inline struct ath11k_vif *ath11k_vif_to_arvif(struct ieee80211_vif *vif)
{
	return (struct ath11k_vif *)vif->drv_priv;
}

static inline struct ath11k *ath11k_ab_to_ar(struct ath11k_base *ab,
					     int mac_id)
{
	return ab->pdevs[ath11k_hw_mac_id_to_pdev_id(&ab->hw_params, mac_id)].ar;
}

static inline void ath11k_core_create_firmware_path(struct ath11k_base *ab,
						    const char *filename,
						    void *buf, size_t buf_len)
{
	snprintf(buf, buf_len, "%s/%s/%s", ATH11K_FW_DIR,
		 ab->hw_params.fw.dir, filename);
}

static inline const char *ath11k_bus_str(enum ath11k_bus bus)
{
	switch (bus) {
	case ATH11K_BUS_PCI:
		return "pci";
	case ATH11K_BUS_AHB:
		return "ahb";
	}

	return "unknown";
}

#endif /* _CORE_H_ */
