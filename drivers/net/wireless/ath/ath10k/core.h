/*
 * Copyright (c) 2005-2011 Atheros Communications Inc.
 * Copyright (c) 2011-2013 Qualcomm Atheros, Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _CORE_H_
#define _CORE_H_

#include <linux/completion.h>
#include <linux/if_ether.h>
#include <linux/types.h>
#include <linux/pci.h>

#include "htt.h"
#include "htc.h"
#include "hw.h"
#include "targaddrs.h"
#include "wmi.h"
#include "../ath.h"
#include "../regd.h"

#define MS(_v, _f) (((_v) & _f##_MASK) >> _f##_LSB)
#define SM(_v, _f) (((_v) << _f##_LSB) & _f##_MASK)
#define WO(_f)      ((_f##_OFFSET) >> 2)

#define ATH10K_SCAN_ID 0
#define WMI_READY_TIMEOUT (5 * HZ)
#define ATH10K_FLUSH_TIMEOUT_HZ (5*HZ)
#define ATH10K_NUM_CHANS 38

/* Antenna noise floor */
#define ATH10K_DEFAULT_NOISE_FLOOR -95

struct ath10k;

struct ath10k_skb_cb {
	dma_addr_t paddr;
	bool is_mapped;
	bool is_aborted;

	struct {
		u8 vdev_id;
		u8 tid;
		bool is_offchan;

		u8 frag_len;
		u8 pad_len;
	} __packed htt;
} __packed;

static inline struct ath10k_skb_cb *ATH10K_SKB_CB(struct sk_buff *skb)
{
	BUILD_BUG_ON(sizeof(struct ath10k_skb_cb) >
		     IEEE80211_TX_INFO_DRIVER_DATA_SIZE);
	return (struct ath10k_skb_cb *)&IEEE80211_SKB_CB(skb)->driver_data;
}

static inline int ath10k_skb_map(struct device *dev, struct sk_buff *skb)
{
	if (ATH10K_SKB_CB(skb)->is_mapped)
		return -EINVAL;

	ATH10K_SKB_CB(skb)->paddr = dma_map_single(dev, skb->data, skb->len,
						   DMA_TO_DEVICE);

	if (unlikely(dma_mapping_error(dev, ATH10K_SKB_CB(skb)->paddr)))
		return -EIO;

	ATH10K_SKB_CB(skb)->is_mapped = true;
	return 0;
}

static inline int ath10k_skb_unmap(struct device *dev, struct sk_buff *skb)
{
	if (!ATH10K_SKB_CB(skb)->is_mapped)
		return -EINVAL;

	dma_unmap_single(dev, ATH10K_SKB_CB(skb)->paddr, skb->len,
			 DMA_TO_DEVICE);
	ATH10K_SKB_CB(skb)->is_mapped = false;
	return 0;
}

static inline u32 host_interest_item_address(u32 item_offset)
{
	return QCA988X_HOST_INTEREST_ADDRESS + item_offset;
}

struct ath10k_bmi {
	bool done_sent;
};

struct ath10k_wmi {
	enum ath10k_htc_ep_id eid;
	struct completion service_ready;
	struct completion unified_ready;
	wait_queue_head_t tx_credits_wq;
};

struct ath10k_peer_stat {
	u8 peer_macaddr[ETH_ALEN];
	u32 peer_rssi;
	u32 peer_tx_rate;
};

struct ath10k_target_stats {
	/* PDEV stats */
	s32 ch_noise_floor;
	u32 tx_frame_count;
	u32 rx_frame_count;
	u32 rx_clear_count;
	u32 cycle_count;
	u32 phy_err_count;
	u32 chan_tx_power;

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

	/* VDEV STATS */

	/* PEER STATS */
	u8 peers;
	struct ath10k_peer_stat peer_stat[TARGET_NUM_PEERS];

	/* TODO: Beacon filter stats */

};

#define ATH10K_MAX_NUM_PEER_IDS (1 << 11) /* htt rx_desc limit */

struct ath10k_peer {
	struct list_head list;
	int vdev_id;
	u8 addr[ETH_ALEN];
	DECLARE_BITMAP(peer_ids, ATH10K_MAX_NUM_PEER_IDS);
	struct ieee80211_key_conf *keys[WMI_MAX_KEY_INDEX + 1];
};

#define ATH10K_VDEV_SETUP_TIMEOUT_HZ (5*HZ)

struct ath10k_vif {
	u32 vdev_id;
	enum wmi_vdev_type vdev_type;
	enum wmi_vdev_subtype vdev_subtype;
	u32 beacon_interval;
	u32 dtim_period;
	struct sk_buff *beacon;

	struct ath10k *ar;
	struct ieee80211_vif *vif;

	struct ieee80211_key_conf *wep_keys[WMI_MAX_KEY_INDEX + 1];
	u8 def_wep_key_index;

	u16 tx_seq_no;

	union {
		struct {
			u8 bssid[ETH_ALEN];
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
		struct {
			u8 bssid[ETH_ALEN];
		} ibss;
	} u;
};

struct ath10k_vif_iter {
	u32 vdev_id;
	struct ath10k_vif *arvif;
};

struct ath10k_debug {
	struct dentry *debugfs_phy;

	struct ath10k_target_stats target_stats;
	u32 wmi_service_bitmap[WMI_SERVICE_BM_SIZE];

	struct completion event_stats_compl;

	unsigned long htt_stats_mask;
	struct delayed_work htt_stats_dwork;
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
	 * set the state to STATE_ON in restart_complete(). */
	ATH10K_STATE_RESTARTING,
	ATH10K_STATE_RESTARTED,

	/* The device has crashed while restarting hw. This state is like ON
	 * but commands are blocked in HTC and -ECOMM response is given. This
	 * prevents completion timeouts and makes the driver more responsive to
	 * userspace commands. This is also prevents recursive recovery. */
	ATH10K_STATE_WEDGED,
};

enum ath10k_fw_features {
	/* wmi_mgmt_rx_hdr contains extra RSSI information */
	ATH10K_FW_FEATURE_EXT_WMI_MGMT_RX = 0,

	/* keep last */
	ATH10K_FW_FEATURE_COUNT,
};

struct ath10k {
	struct ath_common ath_common;
	struct ieee80211_hw *hw;
	struct device *dev;
	u8 mac_addr[ETH_ALEN];

	u32 chip_id;
	u32 target_version;
	u8 fw_version_major;
	u32 fw_version_minor;
	u16 fw_version_release;
	u16 fw_version_build;
	u32 phy_capability;
	u32 hw_min_tx_power;
	u32 hw_max_tx_power;
	u32 ht_cap_info;
	u32 vht_cap_info;
	u32 num_rf_chains;

	DECLARE_BITMAP(fw_features, ATH10K_FW_FEATURE_COUNT);

	struct targetdef *targetdef;
	struct hostdef *hostdef;

	bool p2p;

	struct {
		void *priv;
		const struct ath10k_hif_ops *ops;
	} hif;

	wait_queue_head_t event_queue;
	bool is_target_paused;

	struct ath10k_bmi bmi;
	struct ath10k_wmi wmi;
	struct ath10k_htc htc;
	struct ath10k_htt htt;

	struct ath10k_hw_params {
		u32 id;
		const char *name;
		u32 patch_load_addr;

		struct ath10k_hw_params_fw {
			const char *dir;
			const char *fw;
			const char *otp;
			const char *board;
		} fw;
	} hw_params;

	const struct firmware *board_data;
	const struct firmware *otp;
	const struct firmware *firmware;

	struct {
		struct completion started;
		struct completion completed;
		struct completion on_channel;
		struct timer_list timeout;
		bool is_roc;
		bool in_progress;
		bool aborting;
		int vdev_id;
		int roc_freq;
	} scan;

	struct {
		struct ieee80211_supported_band sbands[IEEE80211_NUM_BANDS];
	} mac;

	/* should never be NULL; needed for regular htt rx */
	struct ieee80211_channel *rx_channel;

	/* valid during scan; needed for mgmt rx during scan */
	struct ieee80211_channel *scan_channel;

	int free_vdev_map;
	int monitor_vdev_id;
	bool monitor_enabled;
	bool monitor_present;
	unsigned int filter_flags;

	struct wmi_pdev_set_wmm_params_arg wmm_params;
	struct completion install_key_done;

	struct completion vdev_setup_done;

	struct workqueue_struct *workqueue;

	/* prevents concurrent FW reconfiguration */
	struct mutex conf_mutex;

	/* protects shared structure data */
	spinlock_t data_lock;

	struct list_head peers;
	wait_queue_head_t peer_mapping_wq;

	struct work_struct offchan_tx_work;
	struct sk_buff_head offchan_tx_queue;
	struct completion offchan_tx_completed;
	struct sk_buff *offchan_tx_skb;

	enum ath10k_state state;

	struct work_struct restart_work;

	/* cycle count is reported twice for each visited channel during scan.
	 * access protected by data_lock */
	u32 survey_last_rx_clear_count;
	u32 survey_last_cycle_count;
	struct survey_info survey[ATH10K_NUM_CHANS];

#ifdef CONFIG_ATH10K_DEBUGFS
	struct ath10k_debug debug;
#endif
};

struct ath10k *ath10k_core_create(void *hif_priv, struct device *dev,
				  const struct ath10k_hif_ops *hif_ops);
void ath10k_core_destroy(struct ath10k *ar);

int ath10k_core_start(struct ath10k *ar);
void ath10k_core_stop(struct ath10k *ar);
int ath10k_core_register(struct ath10k *ar, u32 chip_id);
void ath10k_core_unregister(struct ath10k *ar);

#endif /* _CORE_H_ */
