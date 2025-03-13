/* SPDX-License-Identifier: ISC */
/* Copyright (C) 2020 MediaTek Inc. */

#ifndef __MT76_CONNAC_H
#define __MT76_CONNAC_H

#include "mt76.h"

enum rx_pkt_type {
	PKT_TYPE_TXS,
	PKT_TYPE_TXRXV,
	PKT_TYPE_NORMAL,
	PKT_TYPE_RX_DUP_RFB,
	PKT_TYPE_RX_TMR,
	PKT_TYPE_RETRIEVE,
	PKT_TYPE_TXRX_NOTIFY,
	PKT_TYPE_RX_EVENT,
	PKT_TYPE_NORMAL_MCU,
	PKT_TYPE_RX_FW_MONITOR	= 0x0c,
	PKT_TYPE_TXRX_NOTIFY_V0	= 0x18,
};

#define MT76_CONNAC_SCAN_IE_LEN			600
#define MT76_CONNAC_MAX_NUM_SCHED_SCAN_INTERVAL	 10
#define MT76_CONNAC_MAX_TIME_SCHED_SCAN_INTERVAL U16_MAX
#define MT76_CONNAC_MAX_SCHED_SCAN_SSID		10
#define MT76_CONNAC_MAX_SCAN_MATCH		16

#define MT76_CONNAC_MAX_WMM_SETS		4

#define MT76_CONNAC_COREDUMP_TIMEOUT		(HZ / 20)
#define MT76_CONNAC_COREDUMP_SZ			(1300 * 1024)

#define MT_TXD_SIZE				(8 * 4)

#define MT_USB_TXD_SIZE				(MT_TXD_SIZE + 8 * 4)
#define MT_USB_HDR_SIZE				4
#define MT_USB_TAIL_SIZE			4

#define MT_SDIO_TXD_SIZE			(MT_TXD_SIZE + 8 * 4)
#define MT_SDIO_TAIL_SIZE			8
#define MT_SDIO_HDR_SIZE			4

#define MT_MSDU_ID_VALID		BIT(15)

#define MT_TXD_LEN_LAST			BIT(15)
#define MT_TXD_LEN_MASK			GENMASK(11, 0)
#define MT_TXD_LEN_MSDU_LAST		BIT(14)
#define MT_TXD_LEN_AMSDU_LAST		BIT(15)

enum {
	CMD_CBW_20MHZ = IEEE80211_STA_RX_BW_20,
	CMD_CBW_40MHZ = IEEE80211_STA_RX_BW_40,
	CMD_CBW_80MHZ = IEEE80211_STA_RX_BW_80,
	CMD_CBW_160MHZ = IEEE80211_STA_RX_BW_160,
	CMD_CBW_10MHZ,
	CMD_CBW_5MHZ,
	CMD_CBW_8080MHZ,
	CMD_CBW_320MHZ,

	CMD_HE_MCS_BW80 = 0,
	CMD_HE_MCS_BW160,
	CMD_HE_MCS_BW8080,
	CMD_HE_MCS_BW_NUM
};

enum {
	HW_BSSID_0 = 0x0,
	HW_BSSID_1,
	HW_BSSID_2,
	HW_BSSID_3,
	HW_BSSID_MAX = HW_BSSID_3,
	EXT_BSSID_START = 0x10,
	EXT_BSSID_1,
	EXT_BSSID_15 = 0x1f,
	EXT_BSSID_MAX = EXT_BSSID_15,
	REPEATER_BSSID_START = 0x20,
	REPEATER_BSSID_MAX = 0x3f,
};

struct mt76_connac_reg_map {
	u32 phys;
	u32 maps;
	u32 size;
};

struct mt76_connac_pm {
	bool enable:1;
	bool enable_user:1;
	bool ds_enable:1;
	bool ds_enable_user:1;
	bool suspended:1;

	spinlock_t txq_lock;
	struct {
		struct mt76_wcid *wcid;
		struct sk_buff *skb;
	} tx_q[IEEE80211_NUM_ACS];

	struct work_struct wake_work;
	wait_queue_head_t wait;

	struct {
		spinlock_t lock;
		u32 count;
	} wake;
	struct mutex mutex;

	struct delayed_work ps_work;
	unsigned long last_activity;
	unsigned long idle_timeout;

	struct {
		unsigned long last_wake_event;
		unsigned long awake_time;
		unsigned long last_doze_event;
		unsigned long doze_time;
		unsigned int lp_wake;
	} stats;
};

struct mt76_connac_coredump {
	struct sk_buff_head msg_list;
	struct delayed_work work;
	unsigned long last_activity;
};

struct mt76_connac_sta_key_conf {
	s8 keyidx;
	u8 key[16];
};

#define MT_TXP_MAX_BUF_NUM		6

struct mt76_connac_fw_txp {
	__le16 flags;
	__le16 token;
	u8 bss_idx;
	__le16 rept_wds_wcid;
	u8 nbuf;
	__le32 buf[MT_TXP_MAX_BUF_NUM];
	__le16 len[MT_TXP_MAX_BUF_NUM];
} __packed __aligned(4);

#define MT_HW_TXP_MAX_MSDU_NUM		4
#define MT_HW_TXP_MAX_BUF_NUM		4

struct mt76_connac_txp_ptr {
	__le32 buf0;
	__le16 len0;
	__le16 len1;
	__le32 buf1;
} __packed __aligned(4);

struct mt76_connac_hw_txp {
	__le16 msdu_id[MT_HW_TXP_MAX_MSDU_NUM];
	struct mt76_connac_txp_ptr ptr[MT_HW_TXP_MAX_BUF_NUM / 2];
} __packed __aligned(4);

struct mt76_connac_txp_common {
	union {
		struct mt76_connac_fw_txp fw;
		struct mt76_connac_hw_txp hw;
	};
};

struct mt76_connac_tx_free {
	__le16 rx_byte_cnt;
	__le16 ctrl;
	__le32 txd;
} __packed __aligned(4);

extern const struct wiphy_wowlan_support mt76_connac_wowlan_support;

static inline bool is_mt7925(struct mt76_dev *dev)
{
	return mt76_chip(dev) == 0x7925;
}

static inline bool is_mt7920(struct mt76_dev *dev)
{
	return mt76_chip(dev) == 0x7920;
}

static inline bool is_mt7922(struct mt76_dev *dev)
{
	return mt76_chip(dev) == 0x7922;
}

static inline bool is_mt7921(struct mt76_dev *dev)
{
	return mt76_chip(dev) == 0x7961 || is_mt7922(dev) || is_mt7920(dev);
}

static inline bool is_mt7663(struct mt76_dev *dev)
{
	return mt76_chip(dev) == 0x7663;
}

static inline bool is_mt7915(struct mt76_dev *dev)
{
	return mt76_chip(dev) == 0x7915;
}

static inline bool is_mt7916(struct mt76_dev *dev)
{
	return mt76_chip(dev) == 0x7906;
}

static inline bool is_mt7981(struct mt76_dev *dev)
{
	return mt76_chip(dev) == 0x7981;
}

static inline bool is_mt7986(struct mt76_dev *dev)
{
	return mt76_chip(dev) == 0x7986;
}

static inline bool is_mt798x(struct mt76_dev *dev)
{
	return is_mt7981(dev) || is_mt7986(dev);
}

static inline bool is_mt7996(struct mt76_dev *dev)
{
	return mt76_chip(dev) == 0x7990;
}

static inline bool is_mt7992(struct mt76_dev *dev)
{
	return mt76_chip(dev) == 0x7992;
}

static inline bool is_mt799x(struct mt76_dev *dev)
{
	return is_mt7996(dev) || is_mt7992(dev);
}

static inline bool is_mt7622(struct mt76_dev *dev)
{
	if (!IS_ENABLED(CONFIG_MT7622_WMAC))
		return false;

	return mt76_chip(dev) == 0x7622;
}

static inline bool is_mt7615(struct mt76_dev *dev)
{
	return mt76_chip(dev) == 0x7615 || mt76_chip(dev) == 0x7611;
}

static inline bool is_mt7611(struct mt76_dev *dev)
{
	return mt76_chip(dev) == 0x7611;
}

static inline bool is_connac_v1(struct mt76_dev *dev)
{
	return is_mt7615(dev) || is_mt7663(dev) || is_mt7622(dev);
}

static inline bool is_mt76_fw_txp(struct mt76_dev *dev)
{
	switch (mt76_chip(dev)) {
	case 0x7961:
	case 0x7920:
	case 0x7922:
	case 0x7925:
	case 0x7663:
	case 0x7622:
		return false;
	default:
		return true;
	}
}

static inline u8 mt76_connac_chan_bw(struct cfg80211_chan_def *chandef)
{
	static const u8 width_to_bw[] = {
		[NL80211_CHAN_WIDTH_40] = CMD_CBW_40MHZ,
		[NL80211_CHAN_WIDTH_80] = CMD_CBW_80MHZ,
		[NL80211_CHAN_WIDTH_80P80] = CMD_CBW_8080MHZ,
		[NL80211_CHAN_WIDTH_160] = CMD_CBW_160MHZ,
		[NL80211_CHAN_WIDTH_5] = CMD_CBW_5MHZ,
		[NL80211_CHAN_WIDTH_10] = CMD_CBW_10MHZ,
		[NL80211_CHAN_WIDTH_20] = CMD_CBW_20MHZ,
		[NL80211_CHAN_WIDTH_20_NOHT] = CMD_CBW_20MHZ,
		[NL80211_CHAN_WIDTH_320] = CMD_CBW_320MHZ,
	};

	if (chandef->width >= ARRAY_SIZE(width_to_bw))
		return 0;

	return width_to_bw[chandef->width];
}

static inline u8 mt76_connac_lmac_mapping(u8 ac)
{
	/* LMAC uses the reverse order of mac80211 AC indexes */
	return 3 - ac;
}

static inline void *
mt76_connac_txwi_to_txp(struct mt76_dev *dev, struct mt76_txwi_cache *t)
{
	u8 *txwi;

	if (!t)
		return NULL;

	txwi = mt76_get_txwi_ptr(dev, t);

	return (void *)(txwi + MT_TXD_SIZE);
}

static inline u8 mt76_connac_spe_idx(u8 antenna_mask)
{
	static const u8 ant_to_spe[] = {0, 0, 1, 0, 3, 2, 4, 0,
					9, 8, 6, 10, 16, 12, 18, 0};

	if (antenna_mask >= sizeof(ant_to_spe))
		return 0;

	return ant_to_spe[antenna_mask];
}

static inline void mt76_connac_irq_enable(struct mt76_dev *dev, u32 mask)
{
	mt76_set_irq_mask(dev, 0, 0, mask);
	tasklet_schedule(&dev->irq_tasklet);
}

int mt76_connac_pm_wake(struct mt76_phy *phy, struct mt76_connac_pm *pm);
void mt76_connac_power_save_sched(struct mt76_phy *phy,
				  struct mt76_connac_pm *pm);
void mt76_connac_free_pending_tx_skbs(struct mt76_connac_pm *pm,
				      struct mt76_wcid *wcid);

static inline void mt76_connac_tx_cleanup(struct mt76_dev *dev)
{
	dev->queue_ops->tx_cleanup(dev, dev->q_mcu[MT_MCUQ_WM], false);
	dev->queue_ops->tx_cleanup(dev, dev->q_mcu[MT_MCUQ_WA], false);
}

static inline bool
mt76_connac_pm_ref(struct mt76_phy *phy, struct mt76_connac_pm *pm)
{
	bool ret = false;

	spin_lock_bh(&pm->wake.lock);
	if (test_bit(MT76_STATE_PM, &phy->state))
		goto out;

	pm->wake.count++;
	ret = true;
out:
	spin_unlock_bh(&pm->wake.lock);

	return ret;
}

static inline void
mt76_connac_pm_unref(struct mt76_phy *phy, struct mt76_connac_pm *pm)
{
	spin_lock_bh(&pm->wake.lock);

	pm->last_activity = jiffies;
	if (--pm->wake.count == 0 &&
	    test_bit(MT76_STATE_MCU_RUNNING, &phy->state))
		mt76_connac_power_save_sched(phy, pm);

	spin_unlock_bh(&pm->wake.lock);
}

static inline bool
mt76_connac_skip_fw_pmctrl(struct mt76_phy *phy, struct mt76_connac_pm *pm)
{
	struct mt76_dev *dev = phy->dev;
	bool ret;

	if (dev->token_count)
		return true;

	spin_lock_bh(&pm->wake.lock);
	ret = pm->wake.count || test_and_set_bit(MT76_STATE_PM, &phy->state);
	spin_unlock_bh(&pm->wake.lock);

	return ret;
}

static inline void
mt76_connac_mutex_acquire(struct mt76_dev *dev, struct mt76_connac_pm *pm)
	__acquires(&dev->mutex)
{
	mutex_lock(&dev->mutex);
	mt76_connac_pm_wake(&dev->phy, pm);
}

static inline void
mt76_connac_mutex_release(struct mt76_dev *dev, struct mt76_connac_pm *pm)
	__releases(&dev->mutex)
{
	mt76_connac_power_save_sched(&dev->phy, pm);
	mutex_unlock(&dev->mutex);
}

void mt76_connac_gen_ppe_thresh(u8 *he_ppet, int nss, enum nl80211_band band);
int mt76_connac_init_tx_queues(struct mt76_phy *phy, int idx, int n_desc,
			       int ring_base, void *wed, u32 flags);

void mt76_connac_write_hw_txp(struct mt76_dev *dev,
			      struct mt76_tx_info *tx_info,
			      void *txp_ptr, u32 id);
void mt76_connac_txp_skb_unmap(struct mt76_dev *dev,
			       struct mt76_txwi_cache *txwi);
void mt76_connac_tx_complete_skb(struct mt76_dev *mdev,
				 struct mt76_queue_entry *e);
void mt76_connac_pm_queue_skb(struct ieee80211_hw *hw,
			      struct mt76_connac_pm *pm,
			      struct mt76_wcid *wcid,
			      struct sk_buff *skb);
void mt76_connac_pm_dequeue_skbs(struct mt76_phy *phy,
				 struct mt76_connac_pm *pm);
void mt76_connac2_mac_write_txwi(struct mt76_dev *dev, __le32 *txwi,
				 struct sk_buff *skb, struct mt76_wcid *wcid,
				 struct ieee80211_key_conf *key, int pid,
				 enum mt76_txq_id qid, u32 changed);
u16 mt76_connac2_mac_tx_rate_val(struct mt76_phy *mphy,
				 struct ieee80211_bss_conf *conf,
				 bool beacon, bool mcast);
bool mt76_connac2_mac_fill_txs(struct mt76_dev *dev, struct mt76_wcid *wcid,
			       __le32 *txs_data);
bool mt76_connac2_mac_add_txs_skb(struct mt76_dev *dev, struct mt76_wcid *wcid,
				  int pid, __le32 *txs_data);
void mt76_connac2_mac_decode_he_radiotap(struct mt76_dev *dev,
					 struct sk_buff *skb,
					 __le32 *rxv, u32 mode);
int mt76_connac2_reverse_frag0_hdr_trans(struct ieee80211_vif *vif,
					 struct sk_buff *skb, u16 hdr_offset);
int mt76_connac2_mac_fill_rx_rate(struct mt76_dev *dev,
				  struct mt76_rx_status *status,
				  struct ieee80211_supported_band *sband,
				  __le32 *rxv, u8 *mode);
void mt76_connac2_tx_check_aggr(struct ieee80211_sta *sta, __le32 *txwi);
void mt76_connac2_txwi_free(struct mt76_dev *dev, struct mt76_txwi_cache *t,
			    struct ieee80211_sta *sta,
			    struct list_head *free_list);
void mt76_connac2_tx_token_put(struct mt76_dev *dev);

/* connac3 */
void mt76_connac3_mac_decode_he_radiotap(struct sk_buff *skb, __le32 *rxv,
					 u8 mode);
void mt76_connac3_mac_decode_eht_radiotap(struct sk_buff *skb, __le32 *rxv,
					  u8 mode);
#endif /* __MT76_CONNAC_H */
