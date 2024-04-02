/* SPDX-License-Identifier: ISC */
/* Copyright (C) 2020 MediaTek Inc. */

#ifndef __MT7921_H
#define __MT7921_H

#include "../mt792x.h"
#include "regs.h"

#define MT7921_TX_RING_SIZE		2048
#define MT7921_TX_MCU_RING_SIZE		256
#define MT7921_TX_FWDL_RING_SIZE	128

#define MT7921_RX_RING_SIZE		1536
#define MT7921_RX_MCU_RING_SIZE		8
#define MT7921_RX_MCU_WA_RING_SIZE	512

#define MT7921_EEPROM_SIZE		3584
#define MT7921_TOKEN_SIZE		8192

#define MT7921_EEPROM_BLOCK_SIZE	16

#define MT7921_SKU_RATE_NUM		161
#define MT7921_SKU_MAX_DELTA_IDX	MT7921_SKU_RATE_NUM
#define MT7921_SKU_TABLE_SIZE		(MT7921_SKU_RATE_NUM + 1)

#define MCU_UNI_EVENT_ROC  0x27
#define MCU_UNI_EVENT_CLC  0x80

enum {
	UNI_ROC_ACQUIRE,
	UNI_ROC_ABORT,
	UNI_ROC_NUM
};

enum mt7921_roc_req {
	MT7921_ROC_REQ_JOIN,
	MT7921_ROC_REQ_ROC,
	MT7921_ROC_REQ_NUM
};

enum {
	UNI_EVENT_ROC_GRANT = 0,
	UNI_EVENT_ROC_TAG_NUM
};

struct mt7921_realease_info {
	__le16 len;
	u8 pad_len;
	u8 tag;
} __packed;

struct mt7921_fw_features {
	u8 segment;
	u8 data;
	u8 rsv[14];
} __packed;

struct mt7921_roc_grant_tlv {
	__le16 tag;
	__le16 len;
	u8 bss_idx;
	u8 tokenid;
	u8 status;
	u8 primarychannel;
	u8 rfsco;
	u8 rfband;
	u8 channelwidth;
	u8 centerfreqseg1;
	u8 centerfreqseg2;
	u8 reqtype;
	u8 dbdcband;
	u8 rsv[1];
	__le32 max_interval;
} __packed;

enum mt7921_sdio_pkt_type {
	MT7921_SDIO_TXD,
	MT7921_SDIO_DATA,
	MT7921_SDIO_CMD,
	MT7921_SDIO_FWDL,
};

struct mt7921_sdio_intr {
	u32 isr;
	struct {
		u32 wtqcr[16];
	} tx;
	struct {
		u16 num[2];
		u16 len0[16];
		u16 len1[128];
	} rx;
	u32 rec_mb[2];
} __packed;

#define to_rssi(field, rxv)		((FIELD_GET(field, rxv) - 220) / 2)
#define to_rcpi(rssi)			(2 * (rssi) + 220)

enum mt7921_txq_id {
	MT7921_TXQ_BAND0,
	MT7921_TXQ_BAND1,
	MT7921_TXQ_FWDL = 16,
	MT7921_TXQ_MCU_WM,
};

enum mt7921_rxq_id {
	MT7921_RXQ_BAND0 = 0,
	MT7921_RXQ_BAND1,
	MT7921_RXQ_MCU_WM = 0,
};

enum {
	MT7921_CLC_POWER,
	MT7921_CLC_CHAN,
	MT7921_CLC_MAX_NUM,
};

struct mt7921_clc_rule {
	u8 alpha2[2];
	u8 type[2];
	__le16 len;
	u8 data[];
} __packed;

struct mt7921_clc {
	__le32 len;
	u8 idx;
	u8 ver;
	u8 nr_country;
	u8 type;
	u8 rsv[8];
	u8 data[];
} __packed;

enum mt7921_eeprom_field {
	MT_EE_CHIP_ID =		0x000,
	MT_EE_VERSION =		0x002,
	MT_EE_MAC_ADDR =	0x004,
	MT_EE_WIFI_CONF =	0x07c,
	MT_EE_HW_TYPE =		0x55b,
	__MT_EE_MAX =		0x9ff
};

#define MT_EE_HW_TYPE_ENCAP			BIT(0)

enum {
	TXPWR_USER,
	TXPWR_EEPROM,
	TXPWR_MAC,
	TXPWR_MAX_NUM,
};

struct mt7921_txpwr {
	u8 ch;
	u8 rsv[3];
	struct {
		u8 ch;
		u8 cck[4];
		u8 ofdm[8];
		u8 ht20[8];
		u8 ht40[9];
		u8 vht20[12];
		u8 vht40[12];
		u8 vht80[12];
		u8 vht160[12];
		u8 he26[12];
		u8 he52[12];
		u8 he106[12];
		u8 he242[12];
		u8 he484[12];
		u8 he996[12];
		u8 he996x2[12];
	} data[TXPWR_MAX_NUM];
};

extern const struct ieee80211_ops mt7921_ops;

u32 mt7921_reg_map(struct mt792x_dev *dev, u32 addr);

int __mt7921_start(struct mt792x_phy *phy);
int mt7921_register_device(struct mt792x_dev *dev);
void mt7921_unregister_device(struct mt792x_dev *dev);
int mt7921_run_firmware(struct mt792x_dev *dev);
int mt7921_mcu_set_bss_pm(struct mt792x_dev *dev, struct ieee80211_vif *vif,
			  bool enable);
int mt7921_mcu_sta_update(struct mt792x_dev *dev, struct ieee80211_sta *sta,
			  struct ieee80211_vif *vif, bool enable,
			  enum mt76_sta_info_state state);
int mt7921_mcu_set_chan_info(struct mt792x_phy *phy, int cmd);
int mt7921_mcu_set_tx(struct mt792x_dev *dev, struct ieee80211_vif *vif);
int mt7921_mcu_set_eeprom(struct mt792x_dev *dev);
int mt7921_mcu_get_rx_rate(struct mt792x_phy *phy, struct ieee80211_vif *vif,
			   struct ieee80211_sta *sta, struct rate_info *rate);
int mt7921_mcu_fw_log_2_host(struct mt792x_dev *dev, u8 ctrl);
void mt7921_mcu_rx_event(struct mt792x_dev *dev, struct sk_buff *skb);
int mt7921_mcu_set_rxfilter(struct mt792x_dev *dev, u32 fif,
			    u8 bit_op, u32 bit_map);

static inline u32
mt7921_reg_map_l1(struct mt792x_dev *dev, u32 addr)
{
	u32 offset = FIELD_GET(MT_HIF_REMAP_L1_OFFSET, addr);
	u32 base = FIELD_GET(MT_HIF_REMAP_L1_BASE, addr);

	mt76_rmw_field(dev, MT_HIF_REMAP_L1, MT_HIF_REMAP_L1_MASK, base);
	/* use read to push write */
	mt76_rr(dev, MT_HIF_REMAP_L1);

	return MT_HIF_REMAP_BASE_L1 + offset;
}

static inline u32
mt7921_l1_rr(struct mt792x_dev *dev, u32 addr)
{
	return mt76_rr(dev, mt7921_reg_map_l1(dev, addr));
}

static inline void
mt7921_l1_wr(struct mt792x_dev *dev, u32 addr, u32 val)
{
	mt76_wr(dev, mt7921_reg_map_l1(dev, addr), val);
}

static inline u32
mt7921_l1_rmw(struct mt792x_dev *dev, u32 addr, u32 mask, u32 val)
{
	val |= mt7921_l1_rr(dev, addr) & ~mask;
	mt7921_l1_wr(dev, addr, val);

	return val;
}

#define mt7921_l1_set(dev, addr, val)	mt7921_l1_rmw(dev, addr, 0, val)
#define mt7921_l1_clear(dev, addr, val)	mt7921_l1_rmw(dev, addr, val, 0)

void mt7921_regd_update(struct mt792x_dev *dev);
int mt7921_mac_init(struct mt792x_dev *dev);
bool mt7921_mac_wtbl_update(struct mt792x_dev *dev, int idx, u32 mask);
int mt7921_mac_sta_add(struct mt76_dev *mdev, struct ieee80211_vif *vif,
		       struct ieee80211_sta *sta);
void mt7921_mac_sta_assoc(struct mt76_dev *mdev, struct ieee80211_vif *vif,
			  struct ieee80211_sta *sta);
void mt7921_mac_sta_remove(struct mt76_dev *mdev, struct ieee80211_vif *vif,
			   struct ieee80211_sta *sta);
void mt7921_mac_reset_work(struct work_struct *work);
int mt7921e_tx_prepare_skb(struct mt76_dev *mdev, void *txwi_ptr,
			   enum mt76_txq_id qid, struct mt76_wcid *wcid,
			   struct ieee80211_sta *sta,
			   struct mt76_tx_info *tx_info);

bool mt7921_rx_check(struct mt76_dev *mdev, void *data, int len);
void mt7921_queue_rx_skb(struct mt76_dev *mdev, enum mt76_rxq_id q,
			 struct sk_buff *skb, u32 *info);
void mt7921_stats_work(struct work_struct *work);
void mt7921_set_stream_he_caps(struct mt792x_phy *phy);
int mt7921_init_debugfs(struct mt792x_dev *dev);

int mt7921_mcu_set_beacon_filter(struct mt792x_dev *dev,
				 struct ieee80211_vif *vif,
				 bool enable);
int mt7921_mcu_uni_tx_ba(struct mt792x_dev *dev,
			 struct ieee80211_ampdu_params *params,
			 bool enable);
int mt7921_mcu_uni_rx_ba(struct mt792x_dev *dev,
			 struct ieee80211_ampdu_params *params,
			 bool enable);
void mt7921_scan_work(struct work_struct *work);
void mt7921_roc_work(struct work_struct *work);
int mt7921_mcu_uni_bss_ps(struct mt792x_dev *dev, struct ieee80211_vif *vif);
void mt7921_coredump_work(struct work_struct *work);
int mt7921_get_txpwr_info(struct mt792x_dev *dev, struct mt7921_txpwr *txpwr);
int mt7921_testmode_cmd(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			void *data, int len);
int mt7921_testmode_dump(struct ieee80211_hw *hw, struct sk_buff *msg,
			 struct netlink_callback *cb, void *data, int len);
int mt7921_mcu_parse_response(struct mt76_dev *mdev, int cmd,
			      struct sk_buff *skb, int seq);

int mt7921e_driver_own(struct mt792x_dev *dev);
int mt7921e_mac_reset(struct mt792x_dev *dev);
int mt7921e_mcu_init(struct mt792x_dev *dev);
int mt7921s_wfsys_reset(struct mt792x_dev *dev);
int mt7921s_mac_reset(struct mt792x_dev *dev);
int mt7921s_init_reset(struct mt792x_dev *dev);

int mt7921s_mcu_init(struct mt792x_dev *dev);
int mt7921s_mcu_drv_pmctrl(struct mt792x_dev *dev);
int mt7921s_mcu_fw_pmctrl(struct mt792x_dev *dev);
void mt7921_mac_add_txs(struct mt792x_dev *dev, void *data);
void mt7921_set_runtime_pm(struct mt792x_dev *dev);
void mt7921_mcu_set_suspend_iter(void *priv, u8 *mac,
				 struct ieee80211_vif *vif);
void mt7921_set_ipv6_ns_work(struct work_struct *work);

int mt7921_mcu_set_sniffer(struct mt792x_dev *dev, struct ieee80211_vif *vif,
			   bool enable);
int mt7921_mcu_config_sniffer(struct mt792x_vif *vif,
			      struct ieee80211_chanctx_conf *ctx);
int mt7921_mcu_get_temperature(struct mt792x_phy *phy);

int mt7921_usb_sdio_tx_prepare_skb(struct mt76_dev *mdev, void *txwi_ptr,
				   enum mt76_txq_id qid, struct mt76_wcid *wcid,
				   struct ieee80211_sta *sta,
				   struct mt76_tx_info *tx_info);
void mt7921_usb_sdio_tx_complete_skb(struct mt76_dev *mdev,
				     struct mt76_queue_entry *e);
bool mt7921_usb_sdio_tx_status_data(struct mt76_dev *mdev, u8 *update);

/* usb */
int mt7921_mcu_uni_add_beacon_offload(struct mt792x_dev *dev,
				      struct ieee80211_hw *hw,
				      struct ieee80211_vif *vif,
				      bool enable);
int mt7921_set_tx_sar_pwr(struct ieee80211_hw *hw,
			  const struct cfg80211_sar_specs *sar);

int mt7921_mcu_set_clc(struct mt792x_dev *dev, u8 *alpha2,
		       enum environment_cap env_cap);
int mt7921_mcu_set_roc(struct mt792x_phy *phy, struct mt792x_vif *vif,
		       struct ieee80211_channel *chan, int duration,
		       enum mt7921_roc_req type, u8 token_id);
int mt7921_mcu_abort_roc(struct mt792x_phy *phy, struct mt792x_vif *vif,
			 u8 token_id);
void mt7921_roc_abort_sync(struct mt792x_dev *dev);
#endif
