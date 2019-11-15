/* SPDX-License-Identifier: ISC */
/*
 * Copyright (c) 2005-2011 Atheros Communications Inc.
 * Copyright (c) 2011-2017 Qualcomm Atheros, Inc.
 * Copyright (c) 2018-2019, The Linux Foundation. All rights reserved.
 */

#ifndef _CORE_H_
#define _CORE_H_

#include <linux/completion.h>
#include <linux/if_ether.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/uuid.h>
#include <linux/time.h>

#include "htt.h"
#include "htc.h"
#include "hw.h"
#include "targaddrs.h"
#include "wmi.h"
#include "../ath.h"
#include "../regd.h"
#include "../dfs_pattern_detector.h"
#include "spectral.h"
#include "thermal.h"
#include "wow.h"
#include "swap.h"

#define MS(_v, _f) (((_v) & _f##_MASK) >> _f##_LSB)
#define SM(_v, _f) (((_v) << _f##_LSB) & _f##_MASK)
#define WO(_f)      ((_f##_OFFSET) >> 2)

#define ATH10K_SCAN_ID 0
#define ATH10K_SCAN_CHANNEL_SWITCH_WMI_EVT_OVERHEAD 10 /* msec */
#define WMI_READY_TIMEOUT (5 * HZ)
#define ATH10K_FLUSH_TIMEOUT_HZ (5 * HZ)
#define ATH10K_CONNECTION_LOSS_HZ (3 * HZ)
#define ATH10K_NUM_CHANS 41
#define ATH10K_MAX_5G_CHAN 173

/* Antenna noise floor */
#define ATH10K_DEFAULT_NOISE_FLOOR -95

#define ATH10K_INVALID_RSSI 128

#define ATH10K_MAX_NUM_MGMT_PENDING 128

/* number of failed packets (20 packets with 16 sw reties each) */
#define ATH10K_KICKOUT_THRESHOLD (20 * 16)

/*
 * Use insanely high numbers to make sure that the firmware implementation
 * won't start, we have the same functionality already in hostapd. Unit
 * is seconds.
 */
#define ATH10K_KEEPALIVE_MIN_IDLE 3747
#define ATH10K_KEEPALIVE_MAX_IDLE 3895
#define ATH10K_KEEPALIVE_MAX_UNRESPONSIVE 3900

/* NAPI poll budget */
#define ATH10K_NAPI_BUDGET      64

/* SMBIOS type containing Board Data File Name Extension */
#define ATH10K_SMBIOS_BDF_EXT_TYPE 0xF8

/* SMBIOS type structure length (excluding strings-set) */
#define ATH10K_SMBIOS_BDF_EXT_LENGTH 0x9

/* Offset pointing to Board Data File Name Extension */
#define ATH10K_SMBIOS_BDF_EXT_OFFSET 0x8

/* Board Data File Name Extension string length.
 * String format: BDF_<Customer ID>_<Extension>\0
 */
#define ATH10K_SMBIOS_BDF_EXT_STR_LENGTH 0x20

/* The magic used by QCA spec */
#define ATH10K_SMBIOS_BDF_EXT_MAGIC "BDF_"

/* Default Airtime weight multipler (Tuned for multiclient performance) */
#define ATH10K_AIRTIME_WEIGHT_MULTIPLIER  4

struct ath10k;

static inline const char *ath10k_bus_str(enum ath10k_bus bus)
{
	switch (bus) {
	case ATH10K_BUS_PCI:
		return "pci";
	case ATH10K_BUS_AHB:
		return "ahb";
	case ATH10K_BUS_SDIO:
		return "sdio";
	case ATH10K_BUS_USB:
		return "usb";
	case ATH10K_BUS_SNOC:
		return "snoc";
	}

	return "unknown";
}

enum ath10k_skb_flags {
	ATH10K_SKB_F_NO_HWCRYPT = BIT(0),
	ATH10K_SKB_F_DTIM_ZERO = BIT(1),
	ATH10K_SKB_F_DELIVER_CAB = BIT(2),
	ATH10K_SKB_F_MGMT = BIT(3),
	ATH10K_SKB_F_QOS = BIT(4),
	ATH10K_SKB_F_RAW_TX = BIT(5),
};

struct ath10k_skb_cb {
	dma_addr_t paddr;
	u8 flags;
	u8 eid;
	u16 msdu_id;
	u16 airtime_est;
	struct ieee80211_vif *vif;
	struct ieee80211_txq *txq;
} __packed;

struct ath10k_skb_rxcb {
	dma_addr_t paddr;
	struct hlist_node hlist;
	u8 eid;
};

static inline struct ath10k_skb_cb *ATH10K_SKB_CB(struct sk_buff *skb)
{
	BUILD_BUG_ON(sizeof(struct ath10k_skb_cb) >
		     IEEE80211_TX_INFO_DRIVER_DATA_SIZE);
	return (struct ath10k_skb_cb *)&IEEE80211_SKB_CB(skb)->driver_data;
}

static inline struct ath10k_skb_rxcb *ATH10K_SKB_RXCB(struct sk_buff *skb)
{
	BUILD_BUG_ON(sizeof(struct ath10k_skb_rxcb) > sizeof(skb->cb));
	return (struct ath10k_skb_rxcb *)skb->cb;
}

#define ATH10K_RXCB_SKB(rxcb) \
		container_of((void *)rxcb, struct sk_buff, cb)

static inline u32 host_interest_item_address(u32 item_offset)
{
	return QCA988X_HOST_INTEREST_ADDRESS + item_offset;
}

struct ath10k_bmi {
	bool done_sent;
};

struct ath10k_mem_chunk {
	void *vaddr;
	dma_addr_t paddr;
	u32 len;
	u32 req_id;
};

struct ath10k_wmi {
	enum ath10k_htc_ep_id eid;
	struct completion service_ready;
	struct completion unified_ready;
	struct completion barrier;
	struct completion radar_confirm;
	wait_queue_head_t tx_credits_wq;
	DECLARE_BITMAP(svc_map, WMI_SERVICE_MAX);
	struct wmi_cmd_map *cmd;
	struct wmi_vdev_param_map *vdev_param;
	struct wmi_pdev_param_map *pdev_param;
	struct wmi_peer_param_map *peer_param;
	const struct wmi_ops *ops;
	const struct wmi_peer_flags_map *peer_flags;

	u32 mgmt_max_num_pending_tx;

	/* Protected by data_lock */
	struct idr mgmt_pending_tx;

	u32 num_mem_chunks;
	u32 rx_decap_mode;
	struct ath10k_mem_chunk mem_chunks[WMI_MAX_MEM_REQS];
};

struct ath10k_fw_stats_peer {
	struct list_head list;

	u8 peer_macaddr[ETH_ALEN];
	u32 peer_rssi;
	u32 peer_tx_rate;
	u32 peer_rx_rate; /* 10x only */
	u64 rx_duration;
};

struct ath10k_fw_extd_stats_peer {
	struct list_head list;

	u8 peer_macaddr[ETH_ALEN];
	u64 rx_duration;
};

struct ath10k_fw_stats_vdev {
	struct list_head list;

	u32 vdev_id;
	u32 beacon_snr;
	u32 data_snr;
	u32 num_tx_frames[4];
	u32 num_rx_frames;
	u32 num_tx_frames_retries[4];
	u32 num_tx_frames_failures[4];
	u32 num_rts_fail;
	u32 num_rts_success;
	u32 num_rx_err;
	u32 num_rx_discard;
	u32 num_tx_not_acked;
	u32 tx_rate_history[10];
	u32 beacon_rssi_history[10];
};

struct ath10k_fw_stats_vdev_extd {
	struct list_head list;

	u32 vdev_id;
	u32 ppdu_aggr_cnt;
	u32 ppdu_noack;
	u32 mpdu_queued;
	u32 ppdu_nonaggr_cnt;
	u32 mpdu_sw_requeued;
	u32 mpdu_suc_retry;
	u32 mpdu_suc_multitry;
	u32 mpdu_fail_retry;
	u32 tx_ftm_suc;
	u32 tx_ftm_suc_retry;
	u32 tx_ftm_fail;
	u32 rx_ftmr_cnt;
	u32 rx_ftmr_dup_cnt;
	u32 rx_iftmr_cnt;
	u32 rx_iftmr_dup_cnt;
};

struct ath10k_fw_stats_pdev {
	struct list_head list;

	/* PDEV stats */
	s32 ch_noise_floor;
	u32 tx_frame_count; /* Cycles spent transmitting frames */
	u32 rx_frame_count; /* Cycles spent receiving frames */
	u32 rx_clear_count; /* Total channel busy time, evidently */
	u32 cycle_count; /* Total on-channel time */
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
	u32 hw_paused;
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
	u32 phy_underrun;
	u32 txop_ovf;
	u32 seq_posted;
	u32 seq_failed_queueing;
	u32 seq_completed;
	u32 seq_restarted;
	u32 mu_seq_posted;
	u32 mpdus_sw_flush;
	u32 mpdus_hw_filter;
	u32 mpdus_truncated;
	u32 mpdus_ack_failed;
	u32 mpdus_expired;

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
	s32 rx_ovfl_errs;
};

struct ath10k_fw_stats {
	bool extended;
	struct list_head pdevs;
	struct list_head vdevs;
	struct list_head peers;
	struct list_head peers_extd;
};

#define ATH10K_TPC_TABLE_TYPE_FLAG	1
#define ATH10K_TPC_PREAM_TABLE_END	0xFFFF

struct ath10k_tpc_table {
	u32 pream_idx[WMI_TPC_RATE_MAX];
	u8 rate_code[WMI_TPC_RATE_MAX];
	char tpc_value[WMI_TPC_RATE_MAX][WMI_TPC_TX_N_CHAIN * WMI_TPC_BUF_SIZE];
};

struct ath10k_tpc_stats {
	u32 reg_domain;
	u32 chan_freq;
	u32 phy_mode;
	u32 twice_antenna_reduction;
	u32 twice_max_rd_power;
	s32 twice_antenna_gain;
	u32 power_limit;
	u32 num_tx_chain;
	u32 ctl;
	u32 rate_max;
	u8 flag[WMI_TPC_FLAG];
	struct ath10k_tpc_table tpc_table[WMI_TPC_FLAG];
};

struct ath10k_tpc_table_final {
	u32 pream_idx[WMI_TPC_FINAL_RATE_MAX];
	u8 rate_code[WMI_TPC_FINAL_RATE_MAX];
	char tpc_value[WMI_TPC_FINAL_RATE_MAX][WMI_TPC_TX_N_CHAIN * WMI_TPC_BUF_SIZE];
};

struct ath10k_tpc_stats_final {
	u32 reg_domain;
	u32 chan_freq;
	u32 phy_mode;
	u32 twice_antenna_reduction;
	u32 twice_max_rd_power;
	s32 twice_antenna_gain;
	u32 power_limit;
	u32 num_tx_chain;
	u32 ctl;
	u32 rate_max;
	u8 flag[WMI_TPC_FLAG];
	struct ath10k_tpc_table_final tpc_table_final[WMI_TPC_FLAG];
};

struct ath10k_dfs_stats {
	u32 phy_errors;
	u32 pulses_total;
	u32 pulses_detected;
	u32 pulses_discarded;
	u32 radar_detected;
};

enum ath10k_radar_confirmation_state {
	ATH10K_RADAR_CONFIRMATION_IDLE = 0,
	ATH10K_RADAR_CONFIRMATION_INPROGRESS,
	ATH10K_RADAR_CONFIRMATION_STOPPED,
};

struct ath10k_radar_found_info {
	u32 pri_min;
	u32 pri_max;
	u32 width_min;
	u32 width_max;
	u32 sidx_min;
	u32 sidx_max;
};

#define ATH10K_MAX_NUM_PEER_IDS (1 << 11) /* htt rx_desc limit */

struct ath10k_peer {
	struct list_head list;
	struct ieee80211_vif *vif;
	struct ieee80211_sta *sta;

	bool removed;
	int vdev_id;
	u8 addr[ETH_ALEN];
	DECLARE_BITMAP(peer_ids, ATH10K_MAX_NUM_PEER_IDS);

	/* protected by ar->data_lock */
	struct ieee80211_key_conf *keys[WMI_MAX_KEY_INDEX + 1];
	union htt_rx_pn_t tids_last_pn[ATH10K_TXRX_NUM_EXT_TIDS];
	bool tids_last_pn_valid[ATH10K_TXRX_NUM_EXT_TIDS];
	union htt_rx_pn_t frag_tids_last_pn[ATH10K_TXRX_NUM_EXT_TIDS];
	u32 frag_tids_seq[ATH10K_TXRX_NUM_EXT_TIDS];
	struct {
		enum htt_security_types sec_type;
		int pn_len;
	} rx_pn[ATH10K_HTT_TXRX_PEER_SECURITY_MAX];
};

struct ath10k_txq {
	struct list_head list;
	unsigned long num_fw_queued;
	unsigned long num_push_allowed;
};

enum ath10k_pkt_rx_err {
	ATH10K_PKT_RX_ERR_FCS,
	ATH10K_PKT_RX_ERR_TKIP,
	ATH10K_PKT_RX_ERR_CRYPT,
	ATH10K_PKT_RX_ERR_PEER_IDX_INVAL,
	ATH10K_PKT_RX_ERR_MAX,
};

enum ath10k_ampdu_subfrm_num {
	ATH10K_AMPDU_SUBFRM_NUM_10,
	ATH10K_AMPDU_SUBFRM_NUM_20,
	ATH10K_AMPDU_SUBFRM_NUM_30,
	ATH10K_AMPDU_SUBFRM_NUM_40,
	ATH10K_AMPDU_SUBFRM_NUM_50,
	ATH10K_AMPDU_SUBFRM_NUM_60,
	ATH10K_AMPDU_SUBFRM_NUM_MORE,
	ATH10K_AMPDU_SUBFRM_NUM_MAX,
};

enum ath10k_amsdu_subfrm_num {
	ATH10K_AMSDU_SUBFRM_NUM_1,
	ATH10K_AMSDU_SUBFRM_NUM_2,
	ATH10K_AMSDU_SUBFRM_NUM_3,
	ATH10K_AMSDU_SUBFRM_NUM_4,
	ATH10K_AMSDU_SUBFRM_NUM_MORE,
	ATH10K_AMSDU_SUBFRM_NUM_MAX,
};

struct ath10k_sta_tid_stats {
	unsigned long rx_pkt_from_fw;
	unsigned long rx_pkt_unchained;
	unsigned long rx_pkt_drop_chained;
	unsigned long rx_pkt_drop_filter;
	unsigned long rx_pkt_err[ATH10K_PKT_RX_ERR_MAX];
	unsigned long rx_pkt_queued_for_mac;
	unsigned long rx_pkt_ampdu[ATH10K_AMPDU_SUBFRM_NUM_MAX];
	unsigned long rx_pkt_amsdu[ATH10K_AMSDU_SUBFRM_NUM_MAX];
};

enum ath10k_counter_type {
	ATH10K_COUNTER_TYPE_BYTES,
	ATH10K_COUNTER_TYPE_PKTS,
	ATH10K_COUNTER_TYPE_MAX,
};

enum ath10k_stats_type {
	ATH10K_STATS_TYPE_SUCC,
	ATH10K_STATS_TYPE_FAIL,
	ATH10K_STATS_TYPE_RETRY,
	ATH10K_STATS_TYPE_AMPDU,
	ATH10K_STATS_TYPE_MAX,
};

struct ath10k_htt_data_stats {
	u64 legacy[ATH10K_COUNTER_TYPE_MAX][ATH10K_LEGACY_NUM];
	u64 ht[ATH10K_COUNTER_TYPE_MAX][ATH10K_HT_MCS_NUM];
	u64 vht[ATH10K_COUNTER_TYPE_MAX][ATH10K_VHT_MCS_NUM];
	u64 bw[ATH10K_COUNTER_TYPE_MAX][ATH10K_BW_NUM];
	u64 nss[ATH10K_COUNTER_TYPE_MAX][ATH10K_NSS_NUM];
	u64 gi[ATH10K_COUNTER_TYPE_MAX][ATH10K_GI_NUM];
	u64 rate_table[ATH10K_COUNTER_TYPE_MAX][ATH10K_RATE_TABLE_NUM];
};

struct ath10k_htt_tx_stats {
	struct ath10k_htt_data_stats stats[ATH10K_STATS_TYPE_MAX];
	u64 tx_duration;
	u64 ba_fails;
	u64 ack_fails;
};

struct ath10k_sta {
	struct ath10k_vif *arvif;

	/* the following are protected by ar->data_lock */
	u32 changed; /* IEEE80211_RC_* */
	u32 bw;
	u32 nss;
	u32 smps;
	u16 peer_id;
	struct rate_info txrate;
	struct ieee80211_tx_info tx_info;
	u32 last_tx_bitrate;

	struct work_struct update_wk;
	u64 rx_duration;
	struct ath10k_htt_tx_stats *tx_stats;

#ifdef CONFIG_MAC80211_DEBUGFS
	/* protected by conf_mutex */
	bool aggr_mode;

	/* Protected with ar->data_lock */
	struct ath10k_sta_tid_stats tid_stats[IEEE80211_NUM_TIDS + 1];
#endif
	/* Protected with ar->data_lock */
	u32 peer_ps_state;
};

#define ATH10K_VDEV_SETUP_TIMEOUT_HZ	(5 * HZ)
#define ATH10K_VDEV_DELETE_TIMEOUT_HZ	(5 * HZ)

enum ath10k_beacon_state {
	ATH10K_BEACON_SCHEDULED = 0,
	ATH10K_BEACON_SENDING,
	ATH10K_BEACON_SENT,
};

struct ath10k_vif {
	struct list_head list;

	u32 vdev_id;
	u16 peer_id;
	enum wmi_vdev_type vdev_type;
	enum wmi_vdev_subtype vdev_subtype;
	u32 beacon_interval;
	u32 dtim_period;
	struct sk_buff *beacon;
	/* protected by data_lock */
	enum ath10k_beacon_state beacon_state;
	void *beacon_buf;
	dma_addr_t beacon_paddr;
	unsigned long tx_paused; /* arbitrary values defined by target */

	struct ath10k *ar;
	struct ieee80211_vif *vif;

	bool is_started;
	bool is_up;
	bool spectral_enabled;
	bool ps;
	u32 aid;
	u8 bssid[ETH_ALEN];

	struct ieee80211_key_conf *wep_keys[WMI_MAX_KEY_INDEX + 1];
	s8 def_wep_key_idx;

	u16 tx_seq_no;

	union {
		struct {
			u32 uapsd;
		} sta;
		struct {
			/* 512 stations */
			u8 tim_bitmap[64];
			u8 tim_len;
			u32 ssid_len;
			u8 ssid[IEEE80211_MAX_SSID_LEN];
			bool hidden_ssid;
			/* P2P_IE with NoA attribute for P2P_GO case */
			u32 noa_len;
			u8 *noa_data;
		} ap;
	} u;

	bool use_cts_prot;
	bool nohwcrypt;
	int num_legacy_stations;
	int txpower;
	bool ftm_responder;
	struct wmi_wmm_params_all_arg wmm_params;
	struct work_struct ap_csa_work;
	struct delayed_work connection_loss_work;
	struct cfg80211_bitrate_mask bitrate_mask;

	/* For setting VHT peer fixed rate, protected by conf_mutex */
	int vht_num_rates;
	u8 vht_pfr;
};

struct ath10k_vif_iter {
	u32 vdev_id;
	struct ath10k_vif *arvif;
};

/* Copy Engine register dump, protected by ce-lock */
struct ath10k_ce_crash_data {
	__le32 base_addr;
	__le32 src_wr_idx;
	__le32 src_r_idx;
	__le32 dst_wr_idx;
	__le32 dst_r_idx;
};

struct ath10k_ce_crash_hdr {
	__le32 ce_count;
	__le32 reserved[3]; /* for future use */
	struct ath10k_ce_crash_data entries[];
};

#define MAX_MEM_DUMP_TYPE	5

/* used for crash-dump storage, protected by data-lock */
struct ath10k_fw_crash_data {
	guid_t guid;
	struct timespec64 timestamp;
	__le32 registers[REG_DUMP_COUNT_QCA988X];
	struct ath10k_ce_crash_data ce_crash_data[CE_COUNT_MAX];

	u8 *ramdump_buf;
	size_t ramdump_buf_len;
};

struct ath10k_debug {
	struct dentry *debugfs_phy;

	struct ath10k_fw_stats fw_stats;
	struct completion fw_stats_complete;
	bool fw_stats_done;

	unsigned long htt_stats_mask;
	unsigned long reset_htt_stats;
	struct delayed_work htt_stats_dwork;
	struct ath10k_dfs_stats dfs_stats;
	struct ath_dfs_pool_stats dfs_pool_stats;

	/* used for tpc-dump storage, protected by data-lock */
	struct ath10k_tpc_stats *tpc_stats;
	struct ath10k_tpc_stats_final *tpc_stats_final;

	struct completion tpc_complete;

	/* protected by conf_mutex */
	u64 fw_dbglog_mask;
	u32 fw_dbglog_level;
	u32 reg_addr;
	u32 nf_cal_period;
	void *cal_data;
	u32 enable_extd_tx_stats;
	u8 fw_dbglog_mode;
};

enum ath10k_state {
	ATH10K_STATE_OFF = 0,
	ATH10K_STATE_ON,

	/* When doing firmware recovery the device is first powered down.
	 * mac80211 is supposed to call in to start() hook later on. It is
	 * however possible that driver unloading and firmware crash overlap.
	 * mac80211 can wait on conf_mutex in stop() while the device is
	 * stopped in ath10k_core_restart() work holding conf_mutex. The state
	 * RESTARTED means that the device is up and mac80211 has started hw
	 * reconfiguration. Once mac80211 is done with the reconfiguration we
	 * set the state to STATE_ON in reconfig_complete().
	 */
	ATH10K_STATE_RESTARTING,
	ATH10K_STATE_RESTARTED,

	/* The device has crashed while restarting hw. This state is like ON
	 * but commands are blocked in HTC and -ECOMM response is given. This
	 * prevents completion timeouts and makes the driver more responsive to
	 * userspace commands. This is also prevents recursive recovery.
	 */
	ATH10K_STATE_WEDGED,

	/* factory tests */
	ATH10K_STATE_UTF,
};

enum ath10k_firmware_mode {
	/* the default mode, standard 802.11 functionality */
	ATH10K_FIRMWARE_MODE_NORMAL,

	/* factory tests etc */
	ATH10K_FIRMWARE_MODE_UTF,
};

enum ath10k_fw_features {
	/* wmi_mgmt_rx_hdr contains extra RSSI information */
	ATH10K_FW_FEATURE_EXT_WMI_MGMT_RX = 0,

	/* Firmware from 10X branch. Deprecated, don't use in new code. */
	ATH10K_FW_FEATURE_WMI_10X = 1,

	/* firmware support tx frame management over WMI, otherwise it's HTT */
	ATH10K_FW_FEATURE_HAS_WMI_MGMT_TX = 2,

	/* Firmware does not support P2P */
	ATH10K_FW_FEATURE_NO_P2P = 3,

	/* Firmware 10.2 feature bit. The ATH10K_FW_FEATURE_WMI_10X feature
	 * bit is required to be set as well. Deprecated, don't use in new
	 * code.
	 */
	ATH10K_FW_FEATURE_WMI_10_2 = 4,

	/* Some firmware revisions lack proper multi-interface client powersave
	 * implementation. Enabling PS could result in connection drops,
	 * traffic stalls, etc.
	 */
	ATH10K_FW_FEATURE_MULTI_VIF_PS_SUPPORT = 5,

	/* Some firmware revisions have an incomplete WoWLAN implementation
	 * despite WMI service bit being advertised. This feature flag is used
	 * to distinguish whether WoWLAN is really supported or not.
	 */
	ATH10K_FW_FEATURE_WOWLAN_SUPPORT = 6,

	/* Don't trust error code from otp.bin */
	ATH10K_FW_FEATURE_IGNORE_OTP_RESULT = 7,

	/* Some firmware revisions pad 4th hw address to 4 byte boundary making
	 * it 8 bytes long in Native Wifi Rx decap.
	 */
	ATH10K_FW_FEATURE_NO_NWIFI_DECAP_4ADDR_PADDING = 8,

	/* Firmware supports bypassing PLL setting on init. */
	ATH10K_FW_FEATURE_SUPPORTS_SKIP_CLOCK_INIT = 9,

	/* Raw mode support. If supported, FW supports receiving and trasmitting
	 * frames in raw mode.
	 */
	ATH10K_FW_FEATURE_RAW_MODE_SUPPORT = 10,

	/* Firmware Supports Adaptive CCA*/
	ATH10K_FW_FEATURE_SUPPORTS_ADAPTIVE_CCA = 11,

	/* Firmware supports management frame protection */
	ATH10K_FW_FEATURE_MFP_SUPPORT = 12,

	/* Firmware supports pull-push model where host shares it's software
	 * queue state with firmware and firmware generates fetch requests
	 * telling host which queues to dequeue tx from.
	 *
	 * Primary function of this is improved MU-MIMO performance with
	 * multiple clients.
	 */
	ATH10K_FW_FEATURE_PEER_FLOW_CONTROL = 13,

	/* Firmware supports BT-Coex without reloading firmware via pdev param.
	 * To support Bluetooth coexistence pdev param, WMI_COEX_GPIO_SUPPORT of
	 * extended resource config should be enabled always. This firmware IE
	 * is used to configure WMI_COEX_GPIO_SUPPORT.
	 */
	ATH10K_FW_FEATURE_BTCOEX_PARAM = 14,

	/* Unused flag and proven to be not working, enable this if you want
	 * to experiment sending NULL func data frames in HTT TX
	 */
	ATH10K_FW_FEATURE_SKIP_NULL_FUNC_WAR = 15,

	/* Firmware allow other BSS mesh broadcast/multicast frames without
	 * creating monitor interface. Appropriate rxfilters are programmed for
	 * mesh vdev by firmware itself. This feature flags will be used for
	 * not creating monitor vdev while configuring mesh node.
	 */
	ATH10K_FW_FEATURE_ALLOWS_MESH_BCAST = 16,

	/* Firmware does not support power save in station mode. */
	ATH10K_FW_FEATURE_NO_PS = 17,

	/* Firmware allows management tx by reference instead of by value. */
	ATH10K_FW_FEATURE_MGMT_TX_BY_REF = 18,

	/* Firmware load is done externally, not by bmi */
	ATH10K_FW_FEATURE_NON_BMI = 19,

	/* Firmware sends only one chan_info event per channel */
	ATH10K_FW_FEATURE_SINGLE_CHAN_INFO_PER_CHANNEL = 20,

	/* Firmware allows setting peer fixed rate */
	ATH10K_FW_FEATURE_PEER_FIXED_RATE = 21,

	/* keep last */
	ATH10K_FW_FEATURE_COUNT,
};

enum ath10k_dev_flags {
	/* Indicates that ath10k device is during CAC phase of DFS */
	ATH10K_CAC_RUNNING,
	ATH10K_FLAG_CORE_REGISTERED,

	/* Device has crashed and needs to restart. This indicates any pending
	 * waiters should immediately cancel instead of waiting for a time out.
	 */
	ATH10K_FLAG_CRASH_FLUSH,

	/* Use Raw mode instead of native WiFi Tx/Rx encap mode.
	 * Raw mode supports both hardware and software crypto. Native WiFi only
	 * supports hardware crypto.
	 */
	ATH10K_FLAG_RAW_MODE,

	/* Disable HW crypto engine */
	ATH10K_FLAG_HW_CRYPTO_DISABLED,

	/* Bluetooth coexistance enabled */
	ATH10K_FLAG_BTCOEX,

	/* Per Station statistics service */
	ATH10K_FLAG_PEER_STATS,
};

enum ath10k_cal_mode {
	ATH10K_CAL_MODE_FILE,
	ATH10K_CAL_MODE_OTP,
	ATH10K_CAL_MODE_DT,
	ATH10K_PRE_CAL_MODE_FILE,
	ATH10K_PRE_CAL_MODE_DT,
	ATH10K_CAL_MODE_EEPROM,
};

enum ath10k_crypt_mode {
	/* Only use hardware crypto engine */
	ATH10K_CRYPT_MODE_HW,
	/* Only use software crypto engine */
	ATH10K_CRYPT_MODE_SW,
};

static inline const char *ath10k_cal_mode_str(enum ath10k_cal_mode mode)
{
	switch (mode) {
	case ATH10K_CAL_MODE_FILE:
		return "file";
	case ATH10K_CAL_MODE_OTP:
		return "otp";
	case ATH10K_CAL_MODE_DT:
		return "dt";
	case ATH10K_PRE_CAL_MODE_FILE:
		return "pre-cal-file";
	case ATH10K_PRE_CAL_MODE_DT:
		return "pre-cal-dt";
	case ATH10K_CAL_MODE_EEPROM:
		return "eeprom";
	}

	return "unknown";
}

enum ath10k_scan_state {
	ATH10K_SCAN_IDLE,
	ATH10K_SCAN_STARTING,
	ATH10K_SCAN_RUNNING,
	ATH10K_SCAN_ABORTING,
};

static inline const char *ath10k_scan_state_str(enum ath10k_scan_state state)
{
	switch (state) {
	case ATH10K_SCAN_IDLE:
		return "idle";
	case ATH10K_SCAN_STARTING:
		return "starting";
	case ATH10K_SCAN_RUNNING:
		return "running";
	case ATH10K_SCAN_ABORTING:
		return "aborting";
	}

	return "unknown";
}

enum ath10k_tx_pause_reason {
	ATH10K_TX_PAUSE_Q_FULL,
	ATH10K_TX_PAUSE_MAX,
};

struct ath10k_fw_file {
	const struct firmware *firmware;

	char fw_version[ETHTOOL_FWVERS_LEN];

	DECLARE_BITMAP(fw_features, ATH10K_FW_FEATURE_COUNT);

	enum ath10k_fw_wmi_op_version wmi_op_version;
	enum ath10k_fw_htt_op_version htt_op_version;

	const void *firmware_data;
	size_t firmware_len;

	const void *otp_data;
	size_t otp_len;

	const void *codeswap_data;
	size_t codeswap_len;

	/* The original idea of struct ath10k_fw_file was that it only
	 * contains struct firmware and pointers to various parts (actual
	 * firmware binary, otp, metadata etc) of the file. This seg_info
	 * is actually created separate but as this is used similarly as
	 * the other firmware components it's more convenient to have it
	 * here.
	 */
	struct ath10k_swap_code_seg_info *firmware_swap_code_seg_info;
};

struct ath10k_fw_components {
	const struct firmware *board;
	const void *board_data;
	size_t board_len;
	const struct firmware *ext_board;
	const void *ext_board_data;
	size_t ext_board_len;

	struct ath10k_fw_file fw_file;
};

struct ath10k_per_peer_tx_stats {
	u32	succ_bytes;
	u32	retry_bytes;
	u32	failed_bytes;
	u8	ratecode;
	u8	flags;
	u16	peer_id;
	u16	succ_pkts;
	u16	retry_pkts;
	u16	failed_pkts;
	u16	duration;
	u32	reserved1;
	u32	reserved2;
};

enum ath10k_dev_type {
	ATH10K_DEV_TYPE_LL,
	ATH10K_DEV_TYPE_HL,
};

struct ath10k_bus_params {
	u32 chip_id;
	enum ath10k_dev_type dev_type;
	bool link_can_suspend;
	bool hl_msdu_ids;
};

struct ath10k {
	struct ath_common ath_common;
	struct ieee80211_hw *hw;
	struct ieee80211_ops *ops;
	struct device *dev;
	u8 mac_addr[ETH_ALEN];

	enum ath10k_hw_rev hw_rev;
	u16 dev_id;
	u32 chip_id;
	enum ath10k_dev_type dev_type;
	u32 target_version;
	u8 fw_version_major;
	u32 fw_version_minor;
	u16 fw_version_release;
	u16 fw_version_build;
	u32 fw_stats_req_mask;
	u32 phy_capability;
	u32 hw_min_tx_power;
	u32 hw_max_tx_power;
	u32 hw_eeprom_rd;
	u32 ht_cap_info;
	u32 vht_cap_info;
	u32 vht_supp_mcs;
	u32 num_rf_chains;
	u32 max_spatial_stream;
	/* protected by conf_mutex */
	u32 low_2ghz_chan;
	u32 high_2ghz_chan;
	u32 low_5ghz_chan;
	u32 high_5ghz_chan;
	bool ani_enabled;
	u32 sys_cap_info;

	/* protected by data_lock */
	bool hw_rfkill_on;

	/* protected by conf_mutex */
	u8 ps_state_enable;

	bool nlo_enabled;
	bool p2p;

	struct {
		enum ath10k_bus bus;
		const struct ath10k_hif_ops *ops;
	} hif;

	struct completion target_suspend;
	struct completion driver_recovery;

	const struct ath10k_hw_regs *regs;
	const struct ath10k_hw_ce_regs *hw_ce_regs;
	const struct ath10k_hw_values *hw_values;
	struct ath10k_bmi bmi;
	struct ath10k_wmi wmi;
	struct ath10k_htc htc;
	struct ath10k_htt htt;

	struct ath10k_hw_params hw_params;

	/* contains the firmware images used with ATH10K_FIRMWARE_MODE_NORMAL */
	struct ath10k_fw_components normal_mode_fw;

	/* READ-ONLY images of the running firmware, which can be either
	 * normal or UTF. Do not modify, release etc!
	 */
	const struct ath10k_fw_components *running_fw;

	const struct firmware *pre_cal_file;
	const struct firmware *cal_file;

	struct {
		u32 vendor;
		u32 device;
		u32 subsystem_vendor;
		u32 subsystem_device;

		bool bmi_ids_valid;
		bool qmi_ids_valid;
		u32 qmi_board_id;
		u8 bmi_board_id;
		u8 bmi_eboard_id;
		u8 bmi_chip_id;
		bool ext_bid_supported;

		char bdf_ext[ATH10K_SMBIOS_BDF_EXT_STR_LENGTH];
	} id;

	int fw_api;
	int bd_api;
	enum ath10k_cal_mode cal_mode;

	struct {
		struct completion started;
		struct completion completed;
		struct completion on_channel;
		struct delayed_work timeout;
		enum ath10k_scan_state state;
		bool is_roc;
		int vdev_id;
		int roc_freq;
		bool roc_notify;
	} scan;

	struct {
		struct ieee80211_supported_band sbands[NUM_NL80211_BANDS];
	} mac;

	/* should never be NULL; needed for regular htt rx */
	struct ieee80211_channel *rx_channel;

	/* valid during scan; needed for mgmt rx during scan */
	struct ieee80211_channel *scan_channel;

	/* current operating channel definition */
	struct cfg80211_chan_def chandef;

	/* currently configured operating channel in firmware */
	struct ieee80211_channel *tgt_oper_chan;

	unsigned long long free_vdev_map;
	struct ath10k_vif *monitor_arvif;
	bool monitor;
	int monitor_vdev_id;
	bool monitor_started;
	unsigned int filter_flags;
	unsigned long dev_flags;
	bool dfs_block_radar_events;

	/* protected by conf_mutex */
	bool radar_enabled;
	int num_started_vdevs;

	/* Protected by conf-mutex */
	u8 cfg_tx_chainmask;
	u8 cfg_rx_chainmask;

	struct completion install_key_done;

	int last_wmi_vdev_start_status;
	struct completion vdev_setup_done;
	struct completion vdev_delete_done;

	struct workqueue_struct *workqueue;
	/* Auxiliary workqueue */
	struct workqueue_struct *workqueue_aux;

	/* prevents concurrent FW reconfiguration */
	struct mutex conf_mutex;

	/* protects coredump data */
	struct mutex dump_mutex;

	/* protects shared structure data */
	spinlock_t data_lock;

	struct list_head arvifs;
	struct list_head peers;
	struct ath10k_peer *peer_map[ATH10K_MAX_NUM_PEER_IDS];
	wait_queue_head_t peer_mapping_wq;

	/* protected by conf_mutex */
	int num_peers;
	int num_stations;

	int max_num_peers;
	int max_num_stations;
	int max_num_vdevs;
	int max_num_tdls_vdevs;
	int num_active_peers;
	int num_tids;

	struct work_struct svc_rdy_work;
	struct sk_buff *svc_rdy_skb;

	struct work_struct offchan_tx_work;
	struct sk_buff_head offchan_tx_queue;
	struct completion offchan_tx_completed;
	struct sk_buff *offchan_tx_skb;

	struct work_struct wmi_mgmt_tx_work;
	struct sk_buff_head wmi_mgmt_tx_queue;

	enum ath10k_state state;

	struct work_struct register_work;
	struct work_struct restart_work;

	/* cycle count is reported twice for each visited channel during scan.
	 * access protected by data_lock
	 */
	u32 survey_last_rx_clear_count;
	u32 survey_last_cycle_count;
	struct survey_info survey[ATH10K_NUM_CHANS];

	/* Channel info events are expected to come in pairs without and with
	 * COMPLETE flag set respectively for each channel visit during scan.
	 *
	 * However there are deviations from this rule. This flag is used to
	 * avoid reporting garbage data.
	 */
	bool ch_info_can_report_survey;
	struct completion bss_survey_done;

	struct dfs_pattern_detector *dfs_detector;

	unsigned long tx_paused; /* see ATH10K_TX_PAUSE_ */

#ifdef CONFIG_ATH10K_DEBUGFS
	struct ath10k_debug debug;
	struct {
		/* relay(fs) channel for spectral scan */
		struct rchan *rfs_chan_spec_scan;

		/* spectral_mode and spec_config are protected by conf_mutex */
		enum ath10k_spectral_mode mode;
		struct ath10k_spec_scan config;
	} spectral;
#endif

	u32 pktlog_filter;

#ifdef CONFIG_DEV_COREDUMP
	struct {
		struct ath10k_fw_crash_data *fw_crash_data;
	} coredump;
#endif

	struct {
		/* protected by conf_mutex */
		struct ath10k_fw_components utf_mode_fw;

		/* protected by data_lock */
		bool utf_monitor;
	} testmode;

	struct {
		/* protected by data_lock */
		u32 rx_crc_err_drop;
		u32 fw_crash_counter;
		u32 fw_warm_reset_counter;
		u32 fw_cold_reset_counter;
	} stats;

	struct ath10k_thermal thermal;
	struct ath10k_wow wow;
	struct ath10k_per_peer_tx_stats peer_tx_stats;

	/* NAPI */
	struct net_device napi_dev;
	struct napi_struct napi;

	struct work_struct set_coverage_class_work;
	/* protected by conf_mutex */
	struct {
		/* writing also protected by data_lock */
		s16 coverage_class;

		u32 reg_phyclk;
		u32 reg_slottime_conf;
		u32 reg_slottime_orig;
		u32 reg_ack_cts_timeout_conf;
		u32 reg_ack_cts_timeout_orig;
	} fw_coverage;

	u32 ampdu_reference;

	const u8 *wmi_key_cipher;
	void *ce_priv;

	u32 sta_tid_stats_mask;

	/* protected by data_lock */
	enum ath10k_radar_confirmation_state radar_conf_state;
	struct ath10k_radar_found_info last_radar_info;
	struct work_struct radar_confirmation_work;
	struct ath10k_bus_params bus_param;
	struct completion peer_delete_done;

	/* must be last */
	u8 drv_priv[0] __aligned(sizeof(void *));
};

static inline bool ath10k_peer_stats_enabled(struct ath10k *ar)
{
	if (test_bit(ATH10K_FLAG_PEER_STATS, &ar->dev_flags) &&
	    test_bit(WMI_SERVICE_PEER_STATS, ar->wmi.svc_map))
		return true;

	return false;
}

extern unsigned long ath10k_coredump_mask;

struct ath10k *ath10k_core_create(size_t priv_size, struct device *dev,
				  enum ath10k_bus bus,
				  enum ath10k_hw_rev hw_rev,
				  const struct ath10k_hif_ops *hif_ops);
void ath10k_core_destroy(struct ath10k *ar);
void ath10k_core_get_fw_features_str(struct ath10k *ar,
				     char *buf,
				     size_t max_len);
int ath10k_core_fetch_firmware_api_n(struct ath10k *ar, const char *name,
				     struct ath10k_fw_file *fw_file);

int ath10k_core_start(struct ath10k *ar, enum ath10k_firmware_mode mode,
		      const struct ath10k_fw_components *fw_components);
int ath10k_wait_for_suspend(struct ath10k *ar, u32 suspend_opt);
void ath10k_core_stop(struct ath10k *ar);
int ath10k_core_register(struct ath10k *ar,
			 const struct ath10k_bus_params *bus_params);
void ath10k_core_unregister(struct ath10k *ar);
int ath10k_core_fetch_board_file(struct ath10k *ar, int bd_ie_type);
void ath10k_core_free_board_files(struct ath10k *ar);

#endif /* _CORE_H_ */
