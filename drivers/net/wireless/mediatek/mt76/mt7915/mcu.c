// SPDX-License-Identifier: ISC
/* Copyright (C) 2020 MediaTek Inc. */

#include <linux/fs.h>
#include "mt7915.h"
#include "mcu.h"
#include "mac.h"
#include "eeprom.h"

#define fw_name(_dev, name, ...)	({			\
	char *_fw;						\
	switch (mt76_chip(&(_dev)->mt76)) {			\
	case 0x7915:						\
		_fw = MT7915_##name;				\
		break;						\
	case 0x7981:						\
		_fw = MT7981_##name;				\
		break;						\
	case 0x7986:						\
		_fw = MT7986_##name##__VA_ARGS__;		\
		break;						\
	default:						\
		_fw = MT7916_##name;				\
		break;						\
	}							\
	_fw;							\
})

#define fw_name_var(_dev, name)		(mt7915_check_adie(dev, false) ?	\
					 fw_name(_dev, name) :			\
					 fw_name(_dev, name, _MT7975))

#define MCU_PATCH_ADDRESS		0x200000

#define HE_PHY(p, c)			u8_get_bits(c, IEEE80211_HE_PHY_##p)
#define HE_MAC(m, c)			u8_get_bits(c, IEEE80211_HE_MAC_##m)

static bool sr_scene_detect = true;
module_param(sr_scene_detect, bool, 0644);
MODULE_PARM_DESC(sr_scene_detect, "Enable firmware scene detection algorithm");

static u8
mt7915_mcu_get_sta_nss(u16 mcs_map)
{
	u8 nss;

	for (nss = 8; nss > 0; nss--) {
		u8 nss_mcs = (mcs_map >> (2 * (nss - 1))) & 3;

		if (nss_mcs != IEEE80211_VHT_MCS_NOT_SUPPORTED)
			break;
	}

	return nss - 1;
}

static void
mt7915_mcu_set_sta_he_mcs(struct ieee80211_sta *sta, __le16 *he_mcs,
			  u16 mcs_map)
{
	struct mt7915_sta *msta = (struct mt7915_sta *)sta->drv_priv;
	struct mt7915_dev *dev = msta->vif->phy->dev;
	enum nl80211_band band = msta->vif->phy->mt76->chandef.chan->band;
	const u16 *mask = msta->vif->bitrate_mask.control[band].he_mcs;
	int nss, max_nss = sta->deflink.rx_nss > 3 ? 4 : sta->deflink.rx_nss;

	for (nss = 0; nss < max_nss; nss++) {
		int mcs;

		switch ((mcs_map >> (2 * nss)) & 0x3) {
		case IEEE80211_HE_MCS_SUPPORT_0_11:
			mcs = GENMASK(11, 0);
			break;
		case IEEE80211_HE_MCS_SUPPORT_0_9:
			mcs = GENMASK(9, 0);
			break;
		case IEEE80211_HE_MCS_SUPPORT_0_7:
			mcs = GENMASK(7, 0);
			break;
		default:
			mcs = 0;
		}

		mcs = mcs ? fls(mcs & mask[nss]) - 1 : -1;

		switch (mcs) {
		case 0 ... 7:
			mcs = IEEE80211_HE_MCS_SUPPORT_0_7;
			break;
		case 8 ... 9:
			mcs = IEEE80211_HE_MCS_SUPPORT_0_9;
			break;
		case 10 ... 11:
			mcs = IEEE80211_HE_MCS_SUPPORT_0_11;
			break;
		default:
			mcs = IEEE80211_HE_MCS_NOT_SUPPORTED;
			break;
		}
		mcs_map &= ~(0x3 << (nss * 2));
		mcs_map |= mcs << (nss * 2);

		/* only support 2ss on 160MHz for mt7915 */
		if (is_mt7915(&dev->mt76) && nss > 1 &&
		    sta->deflink.bandwidth == IEEE80211_STA_RX_BW_160)
			break;
	}

	*he_mcs = cpu_to_le16(mcs_map);
}

static void
mt7915_mcu_set_sta_vht_mcs(struct ieee80211_sta *sta, __le16 *vht_mcs,
			   const u16 *mask)
{
	struct mt7915_sta *msta = (struct mt7915_sta *)sta->drv_priv;
	struct mt7915_dev *dev = msta->vif->phy->dev;
	u16 mcs_map = le16_to_cpu(sta->deflink.vht_cap.vht_mcs.rx_mcs_map);
	int nss, max_nss = sta->deflink.rx_nss > 3 ? 4 : sta->deflink.rx_nss;
	u16 mcs;

	for (nss = 0; nss < max_nss; nss++, mcs_map >>= 2) {
		switch (mcs_map & 0x3) {
		case IEEE80211_VHT_MCS_SUPPORT_0_9:
			mcs = GENMASK(9, 0);
			break;
		case IEEE80211_VHT_MCS_SUPPORT_0_8:
			mcs = GENMASK(8, 0);
			break;
		case IEEE80211_VHT_MCS_SUPPORT_0_7:
			mcs = GENMASK(7, 0);
			break;
		default:
			mcs = 0;
		}

		vht_mcs[nss] = cpu_to_le16(mcs & mask[nss]);

		/* only support 2ss on 160MHz for mt7915 */
		if (is_mt7915(&dev->mt76) && nss > 1 &&
		    sta->deflink.bandwidth == IEEE80211_STA_RX_BW_160)
			break;
	}
}

static void
mt7915_mcu_set_sta_ht_mcs(struct ieee80211_sta *sta, u8 *ht_mcs,
			  const u8 *mask)
{
	int nss, max_nss = sta->deflink.rx_nss > 3 ? 4 : sta->deflink.rx_nss;

	for (nss = 0; nss < max_nss; nss++)
		ht_mcs[nss] = sta->deflink.ht_cap.mcs.rx_mask[nss] & mask[nss];
}

static int
mt7915_mcu_parse_response(struct mt76_dev *mdev, int cmd,
			  struct sk_buff *skb, int seq)
{
	struct mt7915_dev *dev = container_of(mdev, struct mt7915_dev, mt76);
	struct mt76_connac2_mcu_rxd *rxd;
	int ret = 0;

	if (!skb) {
		dev_err(mdev->dev, "Message %08x (seq %d) timeout\n",
			cmd, seq);

		if (!test_and_set_bit(MT76_MCU_RESET, &dev->mphy.state)) {
			dev->recovery.restart = true;
			wake_up(&dev->mt76.mcu.wait);
			queue_work(dev->mt76.wq, &dev->reset_work);
			wake_up(&dev->reset_wait);
		}

		return -ETIMEDOUT;
	}

	rxd = (struct mt76_connac2_mcu_rxd *)skb->data;
	if (seq != rxd->seq &&
	    !(rxd->eid == MCU_CMD_EXT_CID &&
	      rxd->ext_eid == MCU_EXT_EVENT_WA_TX_STAT))
		return -EAGAIN;

	if (cmd == MCU_CMD(PATCH_SEM_CONTROL)) {
		skb_pull(skb, sizeof(*rxd) - 4);
		ret = *skb->data;
	} else if (cmd == MCU_EXT_CMD(THERMAL_CTRL)) {
		skb_pull(skb, sizeof(*rxd) + 4);
		ret = le32_to_cpu(*(__le32 *)skb->data);
	} else {
		skb_pull(skb, sizeof(struct mt76_connac2_mcu_rxd));
	}

	return ret;
}

static void
mt7915_mcu_set_timeout(struct mt76_dev *mdev, int cmd)
{
	mdev->mcu.timeout = 5 * HZ;

	if ((cmd & __MCU_CMD_FIELD_ID) != MCU_CMD_EXT_CID)
		return;

	switch (FIELD_GET(__MCU_CMD_FIELD_EXT_ID, cmd)) {
	case MCU_EXT_CMD_THERMAL_CTRL:
	case MCU_EXT_CMD_GET_MIB_INFO:
	case MCU_EXT_CMD_PHY_STAT_INFO:
	case MCU_EXT_CMD_STA_REC_UPDATE:
	case MCU_EXT_CMD_BSS_INFO_UPDATE:
		mdev->mcu.timeout = 2 * HZ;
		return;
	case MCU_EXT_CMD_EFUSE_BUFFER_MODE:
		mdev->mcu.timeout = 10 * HZ;
		return;
	default:
		break;
	}
}

static int
mt7915_mcu_send_message(struct mt76_dev *mdev, struct sk_buff *skb,
			int cmd, int *wait_seq)
{
	struct mt7915_dev *dev = container_of(mdev, struct mt7915_dev, mt76);
	enum mt76_mcuq_id qid;

	if (cmd == MCU_CMD(FW_SCATTER))
		qid = MT_MCUQ_FWDL;
	else if (test_bit(MT76_STATE_MCU_RUNNING, &dev->mphy.state))
		qid = MT_MCUQ_WA;
	else
		qid = MT_MCUQ_WM;

	mt7915_mcu_set_timeout(mdev, cmd);

	return mt76_tx_queue_skb_raw(dev, mdev->q_mcu[qid], skb, 0);
}

int mt7915_mcu_wa_cmd(struct mt7915_dev *dev, int cmd, u32 a1, u32 a2, u32 a3)
{
	struct {
		__le32 args[3];
	} req = {
		.args = {
			cpu_to_le32(a1),
			cpu_to_le32(a2),
			cpu_to_le32(a3),
		},
	};

	return mt76_mcu_send_msg(&dev->mt76, cmd, &req, sizeof(req), false);
}

static void
mt7915_mcu_csa_finish(void *priv, u8 *mac, struct ieee80211_vif *vif)
{
	if (!vif->bss_conf.csa_active || vif->type == NL80211_IFTYPE_STATION)
		return;

	ieee80211_csa_finish(vif, 0);
}

static void
mt7915_mcu_rx_csa_notify(struct mt7915_dev *dev, struct sk_buff *skb)
{
	struct mt76_phy *mphy = &dev->mt76.phy;
	struct mt7915_mcu_csa_notify *c;

	c = (struct mt7915_mcu_csa_notify *)skb->data;

	if (c->band_idx > MT_BAND1)
		return;

	if ((c->band_idx && !dev->phy.mt76->band_idx) &&
	    dev->mt76.phys[MT_BAND1])
		mphy = dev->mt76.phys[MT_BAND1];

	ieee80211_iterate_active_interfaces_atomic(mphy->hw,
			IEEE80211_IFACE_ITER_RESUME_ALL,
			mt7915_mcu_csa_finish, mphy->hw);
}

static void
mt7915_mcu_rx_thermal_notify(struct mt7915_dev *dev, struct sk_buff *skb)
{
	struct mt76_phy *mphy = &dev->mt76.phy;
	struct mt7915_mcu_thermal_notify *t;
	struct mt7915_phy *phy;

	t = (struct mt7915_mcu_thermal_notify *)skb->data;
	if (t->ctrl.ctrl_id != THERMAL_PROTECT_ENABLE)
		return;

	if (t->ctrl.band_idx > MT_BAND1)
		return;

	if ((t->ctrl.band_idx && !dev->phy.mt76->band_idx) &&
	    dev->mt76.phys[MT_BAND1])
		mphy = dev->mt76.phys[MT_BAND1];

	phy = mphy->priv;
	phy->throttle_state = t->ctrl.duty.duty_cycle;
}

static void
mt7915_mcu_rx_radar_detected(struct mt7915_dev *dev, struct sk_buff *skb)
{
	struct mt76_phy *mphy = &dev->mt76.phy;
	struct mt7915_mcu_rdd_report *r;
	u32 sku;

	r = (struct mt7915_mcu_rdd_report *)skb->data;

	switch (r->rdd_idx) {
	case MT_RDD_IDX_BAND0:
		break;
	case MT_RDD_IDX_BAND1:
		sku = mt7915_check_adie(dev, true);
		/* the main phy is bound to band 1 for this sku */
		if (is_mt7986(&dev->mt76) &&
		    (sku == MT7975_ONE_ADIE || sku == MT7976_ONE_ADIE))
			break;
		mphy = dev->mt76.phys[MT_BAND1];
		break;
	case MT_RDD_IDX_BACKGROUND:
		if (!dev->rdd2_phy)
			return;
		mphy = dev->rdd2_phy->mt76;
		break;
	default:
		dev_err(dev->mt76.dev, "Unknown RDD idx %d\n", r->rdd_idx);
		return;
	}

	if (!mphy)
		return;

	if (r->rdd_idx == MT_RDD_IDX_BACKGROUND)
		cfg80211_background_radar_event(mphy->hw->wiphy,
						&dev->rdd2_chandef,
						GFP_ATOMIC);
	else
		ieee80211_radar_detected(mphy->hw, NULL);
	dev->hw_pattern++;
}

static void
mt7915_mcu_rx_log_message(struct mt7915_dev *dev, struct sk_buff *skb)
{
	struct mt76_connac2_mcu_rxd *rxd;
	int len = skb->len - sizeof(*rxd);
	const char *data, *type;

	rxd = (struct mt76_connac2_mcu_rxd *)skb->data;
	data = (char *)&rxd[1];

	switch (rxd->s2d_index) {
	case 0:
		if (mt7915_debugfs_rx_log(dev, data, len))
			return;

		type = "WM";
		break;
	case 2:
		type = "WA";
		break;
	default:
		type = "unknown";
		break;
	}

	wiphy_info(mt76_hw(dev)->wiphy, "%s: %.*s", type, len, data);
}

static void
mt7915_mcu_cca_finish(void *priv, u8 *mac, struct ieee80211_vif *vif)
{
	if (!vif->bss_conf.color_change_active || vif->type == NL80211_IFTYPE_STATION)
		return;

	ieee80211_color_change_finish(vif, 0);
}

static void
mt7915_mcu_rx_bcc_notify(struct mt7915_dev *dev, struct sk_buff *skb)
{
	struct mt76_phy *mphy = &dev->mt76.phy;
	struct mt7915_mcu_bcc_notify *b;

	b = (struct mt7915_mcu_bcc_notify *)skb->data;

	if (b->band_idx > MT_BAND1)
		return;

	if ((b->band_idx && !dev->phy.mt76->band_idx) &&
	    dev->mt76.phys[MT_BAND1])
		mphy = dev->mt76.phys[MT_BAND1];

	ieee80211_iterate_active_interfaces_atomic(mphy->hw,
			IEEE80211_IFACE_ITER_RESUME_ALL,
			mt7915_mcu_cca_finish, mphy->hw);
}

static void
mt7915_mcu_rx_ext_event(struct mt7915_dev *dev, struct sk_buff *skb)
{
	struct mt76_connac2_mcu_rxd *rxd;

	rxd = (struct mt76_connac2_mcu_rxd *)skb->data;
	switch (rxd->ext_eid) {
	case MCU_EXT_EVENT_THERMAL_PROTECT:
		mt7915_mcu_rx_thermal_notify(dev, skb);
		break;
	case MCU_EXT_EVENT_RDD_REPORT:
		mt7915_mcu_rx_radar_detected(dev, skb);
		break;
	case MCU_EXT_EVENT_CSA_NOTIFY:
		mt7915_mcu_rx_csa_notify(dev, skb);
		break;
	case MCU_EXT_EVENT_FW_LOG_2_HOST:
		mt7915_mcu_rx_log_message(dev, skb);
		break;
	case MCU_EXT_EVENT_BCC_NOTIFY:
		mt7915_mcu_rx_bcc_notify(dev, skb);
		break;
	default:
		break;
	}
}

static void
mt7915_mcu_rx_unsolicited_event(struct mt7915_dev *dev, struct sk_buff *skb)
{
	struct mt76_connac2_mcu_rxd *rxd;

	rxd = (struct mt76_connac2_mcu_rxd *)skb->data;
	switch (rxd->eid) {
	case MCU_EVENT_EXT:
		mt7915_mcu_rx_ext_event(dev, skb);
		break;
	default:
		break;
	}
	dev_kfree_skb(skb);
}

void mt7915_mcu_rx_event(struct mt7915_dev *dev, struct sk_buff *skb)
{
	struct mt76_connac2_mcu_rxd *rxd;

	rxd = (struct mt76_connac2_mcu_rxd *)skb->data;
	if ((rxd->ext_eid == MCU_EXT_EVENT_THERMAL_PROTECT ||
	     rxd->ext_eid == MCU_EXT_EVENT_FW_LOG_2_HOST ||
	     rxd->ext_eid == MCU_EXT_EVENT_ASSERT_DUMP ||
	     rxd->ext_eid == MCU_EXT_EVENT_PS_SYNC ||
	     rxd->ext_eid == MCU_EXT_EVENT_BCC_NOTIFY ||
	     !rxd->seq) &&
	     !(rxd->eid == MCU_CMD_EXT_CID &&
	       rxd->ext_eid == MCU_EXT_EVENT_WA_TX_STAT))
		mt7915_mcu_rx_unsolicited_event(dev, skb);
	else
		mt76_mcu_rx_event(&dev->mt76, skb);
}

static struct tlv *
mt7915_mcu_add_nested_subtlv(struct sk_buff *skb, int sub_tag, int sub_len,
			     __le16 *sub_ntlv, __le16 *len)
{
	struct tlv *ptlv, tlv = {
		.tag = cpu_to_le16(sub_tag),
		.len = cpu_to_le16(sub_len),
	};

	ptlv = skb_put_zero(skb, sub_len);
	memcpy(ptlv, &tlv, sizeof(tlv));

	le16_add_cpu(sub_ntlv, 1);
	le16_add_cpu(len, sub_len);

	return ptlv;
}

/** bss info **/
struct mt7915_he_obss_narrow_bw_ru_data {
	bool tolerated;
};

static void mt7915_check_he_obss_narrow_bw_ru_iter(struct wiphy *wiphy,
						   struct cfg80211_bss *bss,
						   void *_data)
{
	struct mt7915_he_obss_narrow_bw_ru_data *data = _data;
	const struct element *elem;

	rcu_read_lock();
	elem = ieee80211_bss_get_elem(bss, WLAN_EID_EXT_CAPABILITY);

	if (!elem || elem->datalen <= 10 ||
	    !(elem->data[10] &
	      WLAN_EXT_CAPA10_OBSS_NARROW_BW_RU_TOLERANCE_SUPPORT))
		data->tolerated = false;

	rcu_read_unlock();
}

static bool mt7915_check_he_obss_narrow_bw_ru(struct ieee80211_hw *hw,
					      struct ieee80211_vif *vif)
{
	struct mt7915_he_obss_narrow_bw_ru_data iter_data = {
		.tolerated = true,
	};

	if (!(vif->bss_conf.chanreq.oper.chan->flags & IEEE80211_CHAN_RADAR))
		return false;

	cfg80211_bss_iter(hw->wiphy, &vif->bss_conf.chanreq.oper,
			  mt7915_check_he_obss_narrow_bw_ru_iter,
			  &iter_data);

	/*
	 * If there is at least one AP on radar channel that cannot
	 * tolerate 26-tone RU UL OFDMA transmissions using HE TB PPDU.
	 */
	return !iter_data.tolerated;
}

static void
mt7915_mcu_bss_rfch_tlv(struct sk_buff *skb, struct ieee80211_vif *vif,
			struct mt7915_phy *phy)
{
	struct cfg80211_chan_def *chandef = &phy->mt76->chandef;
	struct bss_info_rf_ch *ch;
	struct tlv *tlv;
	int freq1 = chandef->center_freq1;

	tlv = mt76_connac_mcu_add_tlv(skb, BSS_INFO_RF_CH, sizeof(*ch));

	ch = (struct bss_info_rf_ch *)tlv;
	ch->pri_ch = chandef->chan->hw_value;
	ch->center_ch0 = ieee80211_frequency_to_channel(freq1);
	ch->bw = mt76_connac_chan_bw(chandef);

	if (chandef->width == NL80211_CHAN_WIDTH_80P80) {
		int freq2 = chandef->center_freq2;

		ch->center_ch1 = ieee80211_frequency_to_channel(freq2);
	}

	if (vif->bss_conf.he_support && vif->type == NL80211_IFTYPE_STATION) {
		struct mt76_phy *mphy = phy->mt76;

		ch->he_ru26_block =
			mt7915_check_he_obss_narrow_bw_ru(mphy->hw, vif);
		ch->he_all_disable = false;
	} else {
		ch->he_all_disable = true;
	}
}

static void
mt7915_mcu_bss_ra_tlv(struct sk_buff *skb, struct ieee80211_vif *vif,
		      struct mt7915_phy *phy)
{
	int max_nss = hweight8(phy->mt76->antenna_mask);
	struct bss_info_ra *ra;
	struct tlv *tlv;

	tlv = mt76_connac_mcu_add_tlv(skb, BSS_INFO_RA, sizeof(*ra));

	ra = (struct bss_info_ra *)tlv;
	ra->op_mode = vif->type == NL80211_IFTYPE_AP;
	ra->adhoc_en = vif->type == NL80211_IFTYPE_ADHOC;
	ra->short_preamble = true;
	ra->tx_streams = max_nss;
	ra->rx_streams = max_nss;
	ra->algo = 4;
	ra->train_up_rule = 2;
	ra->train_up_high_thres = 110;
	ra->train_up_rule_rssi = -70;
	ra->low_traffic_thres = 2;
	ra->phy_cap = cpu_to_le32(0xfdf);
	ra->interval = cpu_to_le32(500);
	ra->fast_interval = cpu_to_le32(100);
}

static void
mt7915_mcu_bss_he_tlv(struct sk_buff *skb, struct ieee80211_vif *vif,
		      struct mt7915_phy *phy)
{
#define DEFAULT_HE_PE_DURATION		4
#define DEFAULT_HE_DURATION_RTS_THRES	1023
	const struct ieee80211_sta_he_cap *cap;
	struct bss_info_he *he;
	struct tlv *tlv;

	cap = mt76_connac_get_he_phy_cap(phy->mt76, vif);

	tlv = mt76_connac_mcu_add_tlv(skb, BSS_INFO_HE_BASIC, sizeof(*he));

	he = (struct bss_info_he *)tlv;
	he->he_pe_duration = vif->bss_conf.htc_trig_based_pkt_ext;
	if (!he->he_pe_duration)
		he->he_pe_duration = DEFAULT_HE_PE_DURATION;

	he->he_rts_thres = cpu_to_le16(vif->bss_conf.frame_time_rts_th);
	if (!he->he_rts_thres)
		he->he_rts_thres = cpu_to_le16(DEFAULT_HE_DURATION_RTS_THRES);

	he->max_nss_mcs[CMD_HE_MCS_BW80] = cap->he_mcs_nss_supp.tx_mcs_80;
	he->max_nss_mcs[CMD_HE_MCS_BW160] = cap->he_mcs_nss_supp.tx_mcs_160;
	he->max_nss_mcs[CMD_HE_MCS_BW8080] = cap->he_mcs_nss_supp.tx_mcs_80p80;
}

static void
mt7915_mcu_bss_hw_amsdu_tlv(struct sk_buff *skb)
{
#define TXD_CMP_MAP1		GENMASK(15, 0)
#define TXD_CMP_MAP2		(GENMASK(31, 0) & ~BIT(23))
	struct bss_info_hw_amsdu *amsdu;
	struct tlv *tlv;

	tlv = mt76_connac_mcu_add_tlv(skb, BSS_INFO_HW_AMSDU, sizeof(*amsdu));

	amsdu = (struct bss_info_hw_amsdu *)tlv;
	amsdu->cmp_bitmap_0 = cpu_to_le32(TXD_CMP_MAP1);
	amsdu->cmp_bitmap_1 = cpu_to_le32(TXD_CMP_MAP2);
	amsdu->trig_thres = cpu_to_le16(2);
	amsdu->enable = true;
}

static void
mt7915_mcu_bss_bmc_tlv(struct sk_buff *skb, struct mt7915_phy *phy)
{
	struct bss_info_bmc_rate *bmc;
	struct cfg80211_chan_def *chandef = &phy->mt76->chandef;
	enum nl80211_band band = chandef->chan->band;
	struct tlv *tlv;

	tlv = mt76_connac_mcu_add_tlv(skb, BSS_INFO_BMC_RATE, sizeof(*bmc));

	bmc = (struct bss_info_bmc_rate *)tlv;
	if (band == NL80211_BAND_2GHZ) {
		bmc->short_preamble = true;
	} else {
		bmc->bc_trans = cpu_to_le16(0x2000);
		bmc->mc_trans = cpu_to_le16(0x2080);
	}
}

static int
mt7915_mcu_muar_config(struct mt7915_phy *phy, struct ieee80211_vif *vif,
		       bool bssid, bool enable)
{
	struct mt7915_dev *dev = phy->dev;
	struct mt7915_vif *mvif = (struct mt7915_vif *)vif->drv_priv;
	u32 idx = mvif->mt76.omac_idx - REPEATER_BSSID_START;
	u32 mask = phy->omac_mask >> 32 & ~BIT(idx);
	const u8 *addr = vif->addr;
	struct {
		u8 mode;
		u8 force_clear;
		u8 clear_bitmap[8];
		u8 entry_count;
		u8 write;
		u8 band;

		u8 index;
		u8 bssid;
		u8 addr[ETH_ALEN];
	} __packed req = {
		.mode = !!mask || enable,
		.entry_count = 1,
		.write = 1,
		.band = phy->mt76->band_idx,
		.index = idx * 2 + bssid,
	};

	if (bssid)
		addr = vif->bss_conf.bssid;

	if (enable)
		ether_addr_copy(req.addr, addr);

	return mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD(MUAR_UPDATE), &req,
				 sizeof(req), true);
}

int mt7915_mcu_add_bss_info(struct mt7915_phy *phy,
			    struct ieee80211_vif *vif, int enable)
{
	struct mt7915_vif *mvif = (struct mt7915_vif *)vif->drv_priv;
	struct mt7915_dev *dev = phy->dev;
	struct sk_buff *skb;

	if (mvif->mt76.omac_idx >= REPEATER_BSSID_START) {
		mt7915_mcu_muar_config(phy, vif, false, enable);
		mt7915_mcu_muar_config(phy, vif, true, enable);
	}

	skb = __mt76_connac_mcu_alloc_sta_req(&dev->mt76, &mvif->mt76, NULL,
					      MT7915_BSS_UPDATE_MAX_SIZE);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	/* bss_omac must be first */
	if (enable)
		mt76_connac_mcu_bss_omac_tlv(skb, vif);

	mt76_connac_mcu_bss_basic_tlv(skb, vif, NULL, phy->mt76,
				      mvif->sta.wcid.idx, enable);

	if (vif->type == NL80211_IFTYPE_MONITOR)
		goto out;

	if (enable) {
		mt7915_mcu_bss_rfch_tlv(skb, vif, phy);
		mt7915_mcu_bss_bmc_tlv(skb, phy);
		mt7915_mcu_bss_ra_tlv(skb, vif, phy);
		mt7915_mcu_bss_hw_amsdu_tlv(skb);

		if (vif->bss_conf.he_support)
			mt7915_mcu_bss_he_tlv(skb, vif, phy);

		if (mvif->mt76.omac_idx >= EXT_BSSID_START &&
		    mvif->mt76.omac_idx < REPEATER_BSSID_START)
			mt76_connac_mcu_bss_ext_tlv(skb, &mvif->mt76);
	}
out:
	return mt76_mcu_skb_send_msg(&dev->mt76, skb,
				     MCU_EXT_CMD(BSS_INFO_UPDATE), true);
}

/** starec & wtbl **/
int mt7915_mcu_add_tx_ba(struct mt7915_dev *dev,
			 struct ieee80211_ampdu_params *params,
			 bool enable)
{
	struct mt7915_sta *msta = (struct mt7915_sta *)params->sta->drv_priv;
	struct mt7915_vif *mvif = msta->vif;
	int ret;

	mt76_worker_disable(&dev->mt76.tx_worker);
	if (enable && !params->amsdu)
		msta->wcid.amsdu = false;
	ret = mt76_connac_mcu_sta_ba(&dev->mt76, &mvif->mt76, params,
				     MCU_EXT_CMD(STA_REC_UPDATE),
				     enable, true);
	mt76_worker_enable(&dev->mt76.tx_worker);

	return ret;
}

int mt7915_mcu_add_rx_ba(struct mt7915_dev *dev,
			 struct ieee80211_ampdu_params *params,
			 bool enable)
{
	struct mt7915_sta *msta = (struct mt7915_sta *)params->sta->drv_priv;
	struct mt7915_vif *mvif = msta->vif;

	return mt76_connac_mcu_sta_ba(&dev->mt76, &mvif->mt76, params,
				      MCU_EXT_CMD(STA_REC_UPDATE),
				      enable, false);
}

static void
mt7915_mcu_sta_he_tlv(struct sk_buff *skb, struct ieee80211_sta *sta,
		      struct ieee80211_vif *vif)
{
	struct mt7915_vif *mvif = (struct mt7915_vif *)vif->drv_priv;
	struct ieee80211_he_cap_elem *elem = &sta->deflink.he_cap.he_cap_elem;
	struct ieee80211_he_mcs_nss_supp mcs_map;
	struct sta_rec_he *he;
	struct tlv *tlv;
	u32 cap = 0;

	if (!sta->deflink.he_cap.has_he)
		return;

	tlv = mt76_connac_mcu_add_tlv(skb, STA_REC_HE, sizeof(*he));

	he = (struct sta_rec_he *)tlv;

	if (elem->mac_cap_info[0] & IEEE80211_HE_MAC_CAP0_HTC_HE)
		cap |= STA_REC_HE_CAP_HTC;

	if (elem->mac_cap_info[2] & IEEE80211_HE_MAC_CAP2_BSR)
		cap |= STA_REC_HE_CAP_BSR;

	if (elem->mac_cap_info[3] & IEEE80211_HE_MAC_CAP3_OMI_CONTROL)
		cap |= STA_REC_HE_CAP_OM;

	if (elem->mac_cap_info[4] & IEEE80211_HE_MAC_CAP4_AMSDU_IN_AMPDU)
		cap |= STA_REC_HE_CAP_AMSDU_IN_AMPDU;

	if (elem->mac_cap_info[4] & IEEE80211_HE_MAC_CAP4_BQR)
		cap |= STA_REC_HE_CAP_BQR;

	if (elem->phy_cap_info[0] &
	    (IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_RU_MAPPING_IN_2G |
	     IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_RU_MAPPING_IN_5G))
		cap |= STA_REC_HE_CAP_BW20_RU242_SUPPORT;

	if (mvif->cap.he_ldpc &&
	    (elem->phy_cap_info[1] &
	     IEEE80211_HE_PHY_CAP1_LDPC_CODING_IN_PAYLOAD))
		cap |= STA_REC_HE_CAP_LDPC;

	if (elem->phy_cap_info[1] &
	    IEEE80211_HE_PHY_CAP1_HE_LTF_AND_GI_FOR_HE_PPDUS_0_8US)
		cap |= STA_REC_HE_CAP_SU_PPDU_1LTF_8US_GI;

	if (elem->phy_cap_info[2] &
	    IEEE80211_HE_PHY_CAP2_NDP_4x_LTF_AND_3_2US)
		cap |= STA_REC_HE_CAP_NDP_4LTF_3DOT2MS_GI;

	if (elem->phy_cap_info[2] &
	    IEEE80211_HE_PHY_CAP2_STBC_TX_UNDER_80MHZ)
		cap |= STA_REC_HE_CAP_LE_EQ_80M_TX_STBC;

	if (elem->phy_cap_info[2] &
	    IEEE80211_HE_PHY_CAP2_STBC_RX_UNDER_80MHZ)
		cap |= STA_REC_HE_CAP_LE_EQ_80M_RX_STBC;

	if (elem->phy_cap_info[6] &
	    IEEE80211_HE_PHY_CAP6_TRIG_CQI_FB)
		cap |= STA_REC_HE_CAP_TRIG_CQI_FK;

	if (elem->phy_cap_info[6] &
	    IEEE80211_HE_PHY_CAP6_PARTIAL_BW_EXT_RANGE)
		cap |= STA_REC_HE_CAP_PARTIAL_BW_EXT_RANGE;

	if (elem->phy_cap_info[7] &
	    IEEE80211_HE_PHY_CAP7_HE_SU_MU_PPDU_4XLTF_AND_08_US_GI)
		cap |= STA_REC_HE_CAP_SU_MU_PPDU_4LTF_8US_GI;

	if (elem->phy_cap_info[7] &
	    IEEE80211_HE_PHY_CAP7_STBC_TX_ABOVE_80MHZ)
		cap |= STA_REC_HE_CAP_GT_80M_TX_STBC;

	if (elem->phy_cap_info[7] &
	    IEEE80211_HE_PHY_CAP7_STBC_RX_ABOVE_80MHZ)
		cap |= STA_REC_HE_CAP_GT_80M_RX_STBC;

	if (elem->phy_cap_info[8] &
	    IEEE80211_HE_PHY_CAP8_HE_ER_SU_PPDU_4XLTF_AND_08_US_GI)
		cap |= STA_REC_HE_CAP_ER_SU_PPDU_4LTF_8US_GI;

	if (elem->phy_cap_info[8] &
	    IEEE80211_HE_PHY_CAP8_HE_ER_SU_1XLTF_AND_08_US_GI)
		cap |= STA_REC_HE_CAP_ER_SU_PPDU_1LTF_8US_GI;

	if (elem->phy_cap_info[9] &
	    IEEE80211_HE_PHY_CAP9_TX_1024_QAM_LESS_THAN_242_TONE_RU)
		cap |= STA_REC_HE_CAP_TX_1024QAM_UNDER_RU242;

	if (elem->phy_cap_info[9] &
	    IEEE80211_HE_PHY_CAP9_RX_1024_QAM_LESS_THAN_242_TONE_RU)
		cap |= STA_REC_HE_CAP_RX_1024QAM_UNDER_RU242;

	he->he_cap = cpu_to_le32(cap);

	mcs_map = sta->deflink.he_cap.he_mcs_nss_supp;
	switch (sta->deflink.bandwidth) {
	case IEEE80211_STA_RX_BW_160:
		if (elem->phy_cap_info[0] &
		    IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_80PLUS80_MHZ_IN_5G)
			mt7915_mcu_set_sta_he_mcs(sta,
						  &he->max_nss_mcs[CMD_HE_MCS_BW8080],
						  le16_to_cpu(mcs_map.rx_mcs_80p80));

		mt7915_mcu_set_sta_he_mcs(sta,
					  &he->max_nss_mcs[CMD_HE_MCS_BW160],
					  le16_to_cpu(mcs_map.rx_mcs_160));
		fallthrough;
	default:
		mt7915_mcu_set_sta_he_mcs(sta,
					  &he->max_nss_mcs[CMD_HE_MCS_BW80],
					  le16_to_cpu(mcs_map.rx_mcs_80));
		break;
	}

	he->t_frame_dur =
		HE_MAC(CAP1_TF_MAC_PAD_DUR_MASK, elem->mac_cap_info[1]);
	he->max_ampdu_exp =
		HE_MAC(CAP3_MAX_AMPDU_LEN_EXP_MASK, elem->mac_cap_info[3]);

	he->bw_set =
		HE_PHY(CAP0_CHANNEL_WIDTH_SET_MASK, elem->phy_cap_info[0]);
	he->device_class =
		HE_PHY(CAP1_DEVICE_CLASS_A, elem->phy_cap_info[1]);
	he->punc_pream_rx =
		HE_PHY(CAP1_PREAMBLE_PUNC_RX_MASK, elem->phy_cap_info[1]);

	he->dcm_tx_mode =
		HE_PHY(CAP3_DCM_MAX_CONST_TX_MASK, elem->phy_cap_info[3]);
	he->dcm_tx_max_nss =
		HE_PHY(CAP3_DCM_MAX_TX_NSS_2, elem->phy_cap_info[3]);
	he->dcm_rx_mode =
		HE_PHY(CAP3_DCM_MAX_CONST_RX_MASK, elem->phy_cap_info[3]);
	he->dcm_rx_max_nss =
		HE_PHY(CAP3_DCM_MAX_RX_NSS_2, elem->phy_cap_info[3]);
	he->dcm_rx_max_nss =
		HE_PHY(CAP8_DCM_MAX_RU_MASK, elem->phy_cap_info[8]);

	he->pkt_ext = 2;
}

static void
mt7915_mcu_sta_muru_tlv(struct mt7915_dev *dev, struct sk_buff *skb,
			struct ieee80211_sta *sta, struct ieee80211_vif *vif)
{
	struct mt7915_vif *mvif = (struct mt7915_vif *)vif->drv_priv;
	struct ieee80211_he_cap_elem *elem = &sta->deflink.he_cap.he_cap_elem;
	struct sta_rec_muru *muru;
	struct tlv *tlv;

	if (vif->type != NL80211_IFTYPE_STATION &&
	    vif->type != NL80211_IFTYPE_AP)
		return;

	tlv = mt76_connac_mcu_add_tlv(skb, STA_REC_MURU, sizeof(*muru));

	muru = (struct sta_rec_muru *)tlv;

	muru->cfg.mimo_dl_en = mvif->cap.he_mu_ebfer ||
			       mvif->cap.vht_mu_ebfer ||
			       mvif->cap.vht_mu_ebfee;
	if (!is_mt7915(&dev->mt76))
		muru->cfg.mimo_ul_en = true;
	muru->cfg.ofdma_dl_en = true;

	if (sta->deflink.vht_cap.vht_supported)
		muru->mimo_dl.vht_mu_bfee =
			!!(sta->deflink.vht_cap.cap & IEEE80211_VHT_CAP_MU_BEAMFORMEE_CAPABLE);

	if (!sta->deflink.he_cap.has_he)
		return;

	muru->mimo_dl.partial_bw_dl_mimo =
		HE_PHY(CAP6_PARTIAL_BANDWIDTH_DL_MUMIMO, elem->phy_cap_info[6]);

	muru->mimo_ul.full_ul_mimo =
		HE_PHY(CAP2_UL_MU_FULL_MU_MIMO, elem->phy_cap_info[2]);
	muru->mimo_ul.partial_ul_mimo =
		HE_PHY(CAP2_UL_MU_PARTIAL_MU_MIMO, elem->phy_cap_info[2]);

	muru->ofdma_dl.punc_pream_rx =
		HE_PHY(CAP1_PREAMBLE_PUNC_RX_MASK, elem->phy_cap_info[1]);
	muru->ofdma_dl.he_20m_in_40m_2g =
		HE_PHY(CAP8_20MHZ_IN_40MHZ_HE_PPDU_IN_2G, elem->phy_cap_info[8]);
	muru->ofdma_dl.he_20m_in_160m =
		HE_PHY(CAP8_20MHZ_IN_160MHZ_HE_PPDU, elem->phy_cap_info[8]);
	muru->ofdma_dl.he_80m_in_160m =
		HE_PHY(CAP8_80MHZ_IN_160MHZ_HE_PPDU, elem->phy_cap_info[8]);

	muru->ofdma_ul.t_frame_dur =
		HE_MAC(CAP1_TF_MAC_PAD_DUR_MASK, elem->mac_cap_info[1]);
	muru->ofdma_ul.mu_cascading =
		HE_MAC(CAP2_MU_CASCADING, elem->mac_cap_info[2]);
	muru->ofdma_ul.uo_ra =
		HE_MAC(CAP3_OFDMA_RA, elem->mac_cap_info[3]);
	muru->ofdma_ul.rx_ctrl_frame_to_mbss =
		HE_MAC(CAP3_RX_CTRL_FRAME_TO_MULTIBSS, elem->mac_cap_info[3]);
}

static void
mt7915_mcu_sta_ht_tlv(struct sk_buff *skb, struct ieee80211_sta *sta)
{
	struct sta_rec_ht *ht;
	struct tlv *tlv;

	if (!sta->deflink.ht_cap.ht_supported)
		return;

	tlv = mt76_connac_mcu_add_tlv(skb, STA_REC_HT, sizeof(*ht));

	ht = (struct sta_rec_ht *)tlv;
	ht->ht_cap = cpu_to_le16(sta->deflink.ht_cap.cap);
}

static void
mt7915_mcu_sta_vht_tlv(struct sk_buff *skb, struct ieee80211_sta *sta)
{
	struct sta_rec_vht *vht;
	struct tlv *tlv;

	if (!sta->deflink.vht_cap.vht_supported)
		return;

	tlv = mt76_connac_mcu_add_tlv(skb, STA_REC_VHT, sizeof(*vht));

	vht = (struct sta_rec_vht *)tlv;
	vht->vht_cap = cpu_to_le32(sta->deflink.vht_cap.cap);
	vht->vht_rx_mcs_map = sta->deflink.vht_cap.vht_mcs.rx_mcs_map;
	vht->vht_tx_mcs_map = sta->deflink.vht_cap.vht_mcs.tx_mcs_map;
}

static void
mt7915_mcu_sta_amsdu_tlv(struct mt7915_dev *dev, struct sk_buff *skb,
			 struct ieee80211_vif *vif, struct ieee80211_sta *sta)
{
	struct mt7915_sta *msta = (struct mt7915_sta *)sta->drv_priv;
	struct sta_rec_amsdu *amsdu;
	struct tlv *tlv;

	if (vif->type != NL80211_IFTYPE_STATION &&
	    vif->type != NL80211_IFTYPE_AP)
		return;

	if (!sta->deflink.agg.max_amsdu_len)
	    return;

	tlv = mt76_connac_mcu_add_tlv(skb, STA_REC_HW_AMSDU, sizeof(*amsdu));
	amsdu = (struct sta_rec_amsdu *)tlv;
	amsdu->max_amsdu_num = 8;
	amsdu->amsdu_en = true;
	msta->wcid.amsdu = true;

	switch (sta->deflink.agg.max_amsdu_len) {
	case IEEE80211_MAX_MPDU_LEN_VHT_11454:
		if (!is_mt7915(&dev->mt76)) {
			amsdu->max_mpdu_size =
				IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_11454;
			return;
		}
		fallthrough;
	case IEEE80211_MAX_MPDU_LEN_HT_7935:
	case IEEE80211_MAX_MPDU_LEN_VHT_7991:
		amsdu->max_mpdu_size = IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_7991;
		return;
	default:
		amsdu->max_mpdu_size = IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_3895;
		return;
	}
}

static int
mt7915_mcu_sta_wtbl_tlv(struct mt7915_dev *dev, struct sk_buff *skb,
			struct ieee80211_vif *vif, struct ieee80211_sta *sta)
{
	struct mt7915_vif *mvif = (struct mt7915_vif *)vif->drv_priv;
	struct mt7915_sta *msta;
	struct wtbl_req_hdr *wtbl_hdr;
	struct mt76_wcid *wcid;
	struct tlv *tlv;

	msta = sta ? (struct mt7915_sta *)sta->drv_priv : &mvif->sta;
	wcid = sta ? &msta->wcid : NULL;

	tlv = mt76_connac_mcu_add_tlv(skb, STA_REC_WTBL, sizeof(struct tlv));
	wtbl_hdr = mt76_connac_mcu_alloc_wtbl_req(&dev->mt76, &msta->wcid,
						  WTBL_RESET_AND_SET, tlv,
						  &skb);
	if (IS_ERR(wtbl_hdr))
		return PTR_ERR(wtbl_hdr);

	mt76_connac_mcu_wtbl_generic_tlv(&dev->mt76, skb, vif, sta, tlv,
					 wtbl_hdr);
	mt76_connac_mcu_wtbl_hdr_trans_tlv(skb, vif, wcid, tlv, wtbl_hdr);
	if (sta)
		mt76_connac_mcu_wtbl_ht_tlv(&dev->mt76, skb, sta, tlv,
					    wtbl_hdr, mvif->cap.ht_ldpc,
					    mvif->cap.vht_ldpc);

	return 0;
}

static inline bool
mt7915_is_ebf_supported(struct mt7915_phy *phy, struct ieee80211_vif *vif,
			struct ieee80211_sta *sta, bool bfee)
{
	struct mt7915_vif *mvif = (struct mt7915_vif *)vif->drv_priv;
	int sts = hweight16(phy->mt76->chainmask);

	if (vif->type != NL80211_IFTYPE_STATION &&
	    vif->type != NL80211_IFTYPE_AP)
		return false;

	if (!bfee && sts < 2)
		return false;

	if (sta->deflink.he_cap.has_he) {
		struct ieee80211_he_cap_elem *pe = &sta->deflink.he_cap.he_cap_elem;

		if (bfee)
			return mvif->cap.he_su_ebfee &&
			       HE_PHY(CAP3_SU_BEAMFORMER, pe->phy_cap_info[3]);
		else
			return mvif->cap.he_su_ebfer &&
			       HE_PHY(CAP4_SU_BEAMFORMEE, pe->phy_cap_info[4]);
	}

	if (sta->deflink.vht_cap.vht_supported) {
		u32 cap = sta->deflink.vht_cap.cap;

		if (bfee)
			return mvif->cap.vht_su_ebfee &&
			       (cap & IEEE80211_VHT_CAP_SU_BEAMFORMER_CAPABLE);
		else
			return mvif->cap.vht_su_ebfer &&
			       (cap & IEEE80211_VHT_CAP_SU_BEAMFORMEE_CAPABLE);
	}

	return false;
}

static void
mt7915_mcu_sta_sounding_rate(struct sta_rec_bf *bf)
{
	bf->sounding_phy = MT_PHY_TYPE_OFDM;
	bf->ndp_rate = 0;				/* mcs0 */
	bf->ndpa_rate = MT7915_CFEND_RATE_DEFAULT;	/* ofdm 24m */
	bf->rept_poll_rate = MT7915_CFEND_RATE_DEFAULT;	/* ofdm 24m */
}

static void
mt7915_mcu_sta_bfer_ht(struct ieee80211_sta *sta, struct mt7915_phy *phy,
		       struct sta_rec_bf *bf)
{
	struct ieee80211_mcs_info *mcs = &sta->deflink.ht_cap.mcs;
	u8 n = 0;

	bf->tx_mode = MT_PHY_TYPE_HT;

	if ((mcs->tx_params & IEEE80211_HT_MCS_TX_RX_DIFF) &&
	    (mcs->tx_params & IEEE80211_HT_MCS_TX_DEFINED))
		n = FIELD_GET(IEEE80211_HT_MCS_TX_MAX_STREAMS_MASK,
			      mcs->tx_params);
	else if (mcs->rx_mask[3])
		n = 3;
	else if (mcs->rx_mask[2])
		n = 2;
	else if (mcs->rx_mask[1])
		n = 1;

	bf->nrow = hweight8(phy->mt76->chainmask) - 1;
	bf->ncol = min_t(u8, bf->nrow, n);
	bf->ibf_ncol = n;
}

static void
mt7915_mcu_sta_bfer_vht(struct ieee80211_sta *sta, struct mt7915_phy *phy,
			struct sta_rec_bf *bf, bool explicit)
{
	struct ieee80211_sta_vht_cap *pc = &sta->deflink.vht_cap;
	struct ieee80211_sta_vht_cap *vc = &phy->mt76->sband_5g.sband.vht_cap;
	u16 mcs_map = le16_to_cpu(pc->vht_mcs.rx_mcs_map);
	u8 nss_mcs = mt7915_mcu_get_sta_nss(mcs_map);
	u8 tx_ant = hweight8(phy->mt76->chainmask) - 1;

	bf->tx_mode = MT_PHY_TYPE_VHT;

	if (explicit) {
		u8 sts, snd_dim;

		mt7915_mcu_sta_sounding_rate(bf);

		sts = FIELD_GET(IEEE80211_VHT_CAP_BEAMFORMEE_STS_MASK,
				pc->cap);
		snd_dim = FIELD_GET(IEEE80211_VHT_CAP_SOUNDING_DIMENSIONS_MASK,
				    vc->cap);
		bf->nrow = min_t(u8, min_t(u8, snd_dim, sts), tx_ant);
		bf->ncol = min_t(u8, nss_mcs, bf->nrow);
		bf->ibf_ncol = bf->ncol;

		if (sta->deflink.bandwidth == IEEE80211_STA_RX_BW_160)
			bf->nrow = 1;
	} else {
		bf->nrow = tx_ant;
		bf->ncol = min_t(u8, nss_mcs, bf->nrow);
		bf->ibf_ncol = nss_mcs;

		if (sta->deflink.bandwidth == IEEE80211_STA_RX_BW_160)
			bf->ibf_nrow = 1;
	}
}

static void
mt7915_mcu_sta_bfer_he(struct ieee80211_sta *sta, struct ieee80211_vif *vif,
		       struct mt7915_phy *phy, struct sta_rec_bf *bf)
{
	struct ieee80211_sta_he_cap *pc = &sta->deflink.he_cap;
	struct ieee80211_he_cap_elem *pe = &pc->he_cap_elem;
	const struct ieee80211_sta_he_cap *vc =
		mt76_connac_get_he_phy_cap(phy->mt76, vif);
	const struct ieee80211_he_cap_elem *ve = &vc->he_cap_elem;
	u16 mcs_map = le16_to_cpu(pc->he_mcs_nss_supp.rx_mcs_80);
	u8 nss_mcs = mt7915_mcu_get_sta_nss(mcs_map);
	u8 snd_dim, sts;

	bf->tx_mode = MT_PHY_TYPE_HE_SU;

	mt7915_mcu_sta_sounding_rate(bf);

	bf->trigger_su = HE_PHY(CAP6_TRIG_SU_BEAMFORMING_FB,
				pe->phy_cap_info[6]);
	bf->trigger_mu = HE_PHY(CAP6_TRIG_MU_BEAMFORMING_PARTIAL_BW_FB,
				pe->phy_cap_info[6]);
	snd_dim = HE_PHY(CAP5_BEAMFORMEE_NUM_SND_DIM_UNDER_80MHZ_MASK,
			 ve->phy_cap_info[5]);
	sts = HE_PHY(CAP4_BEAMFORMEE_MAX_STS_UNDER_80MHZ_MASK,
		     pe->phy_cap_info[4]);
	bf->nrow = min_t(u8, snd_dim, sts);
	bf->ncol = min_t(u8, nss_mcs, bf->nrow);
	bf->ibf_ncol = bf->ncol;

	if (sta->deflink.bandwidth != IEEE80211_STA_RX_BW_160)
		return;

	/* go over for 160MHz and 80p80 */
	if (pe->phy_cap_info[0] &
	    IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_160MHZ_IN_5G) {
		mcs_map = le16_to_cpu(pc->he_mcs_nss_supp.rx_mcs_160);
		nss_mcs = mt7915_mcu_get_sta_nss(mcs_map);

		bf->ncol_gt_bw80 = nss_mcs;
	}

	if (pe->phy_cap_info[0] &
	    IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_80PLUS80_MHZ_IN_5G) {
		mcs_map = le16_to_cpu(pc->he_mcs_nss_supp.rx_mcs_80p80);
		nss_mcs = mt7915_mcu_get_sta_nss(mcs_map);

		if (bf->ncol_gt_bw80)
			bf->ncol_gt_bw80 = min_t(u8, bf->ncol_gt_bw80, nss_mcs);
		else
			bf->ncol_gt_bw80 = nss_mcs;
	}

	snd_dim = HE_PHY(CAP5_BEAMFORMEE_NUM_SND_DIM_ABOVE_80MHZ_MASK,
			 ve->phy_cap_info[5]);
	sts = HE_PHY(CAP4_BEAMFORMEE_MAX_STS_ABOVE_80MHZ_MASK,
		     pe->phy_cap_info[4]);

	bf->nrow_gt_bw80 = min_t(int, snd_dim, sts);
}

static void
mt7915_mcu_sta_bfer_tlv(struct mt7915_dev *dev, struct sk_buff *skb,
			struct ieee80211_vif *vif, struct ieee80211_sta *sta)
{
	struct mt7915_vif *mvif = (struct mt7915_vif *)vif->drv_priv;
	struct mt7915_phy *phy = mvif->phy;
	int tx_ant = hweight8(phy->mt76->chainmask) - 1;
	struct sta_rec_bf *bf;
	struct tlv *tlv;
	static const u8 matrix[4][4] = {
		{0, 0, 0, 0},
		{1, 1, 0, 0},	/* 2x1, 2x2, 2x3, 2x4 */
		{2, 4, 4, 0},	/* 3x1, 3x2, 3x3, 3x4 */
		{3, 5, 6, 0}	/* 4x1, 4x2, 4x3, 4x4 */
	};
	bool ebf;

	if (!(sta->deflink.ht_cap.ht_supported || sta->deflink.he_cap.has_he))
		return;

	ebf = mt7915_is_ebf_supported(phy, vif, sta, false);
	if (!ebf && !dev->ibf)
		return;

	tlv = mt76_connac_mcu_add_tlv(skb, STA_REC_BF, sizeof(*bf));
	bf = (struct sta_rec_bf *)tlv;

	/* he: eBF only, in accordance with spec
	 * vht: support eBF and iBF
	 * ht: iBF only, since mac80211 lacks of eBF support
	 */
	if (sta->deflink.he_cap.has_he && ebf)
		mt7915_mcu_sta_bfer_he(sta, vif, phy, bf);
	else if (sta->deflink.vht_cap.vht_supported)
		mt7915_mcu_sta_bfer_vht(sta, phy, bf, ebf);
	else if (sta->deflink.ht_cap.ht_supported)
		mt7915_mcu_sta_bfer_ht(sta, phy, bf);
	else
		return;

	bf->bf_cap = ebf ? ebf : dev->ibf << 1;
	bf->bw = sta->deflink.bandwidth;
	bf->ibf_dbw = sta->deflink.bandwidth;
	bf->ibf_nrow = tx_ant;

	if (!ebf && sta->deflink.bandwidth <= IEEE80211_STA_RX_BW_40 && !bf->ncol)
		bf->ibf_timeout = 0x48;
	else
		bf->ibf_timeout = 0x18;

	if (ebf && bf->nrow != tx_ant)
		bf->mem_20m = matrix[tx_ant][bf->ncol];
	else
		bf->mem_20m = matrix[bf->nrow][bf->ncol];

	switch (sta->deflink.bandwidth) {
	case IEEE80211_STA_RX_BW_160:
	case IEEE80211_STA_RX_BW_80:
		bf->mem_total = bf->mem_20m * 2;
		break;
	case IEEE80211_STA_RX_BW_40:
		bf->mem_total = bf->mem_20m;
		break;
	case IEEE80211_STA_RX_BW_20:
	default:
		break;
	}
}

static void
mt7915_mcu_sta_bfee_tlv(struct mt7915_dev *dev, struct sk_buff *skb,
			struct ieee80211_vif *vif, struct ieee80211_sta *sta)
{
	struct mt7915_vif *mvif = (struct mt7915_vif *)vif->drv_priv;
	struct mt7915_phy *phy = mvif->phy;
	int tx_ant = hweight8(phy->mt76->chainmask) - 1;
	struct sta_rec_bfee *bfee;
	struct tlv *tlv;
	u8 nrow = 0;

	if (!(sta->deflink.vht_cap.vht_supported || sta->deflink.he_cap.has_he))
		return;

	if (!mt7915_is_ebf_supported(phy, vif, sta, true))
		return;

	tlv = mt76_connac_mcu_add_tlv(skb, STA_REC_BFEE, sizeof(*bfee));
	bfee = (struct sta_rec_bfee *)tlv;

	if (sta->deflink.he_cap.has_he) {
		struct ieee80211_he_cap_elem *pe = &sta->deflink.he_cap.he_cap_elem;

		nrow = HE_PHY(CAP5_BEAMFORMEE_NUM_SND_DIM_UNDER_80MHZ_MASK,
			      pe->phy_cap_info[5]);
	} else if (sta->deflink.vht_cap.vht_supported) {
		struct ieee80211_sta_vht_cap *pc = &sta->deflink.vht_cap;

		nrow = FIELD_GET(IEEE80211_VHT_CAP_SOUNDING_DIMENSIONS_MASK,
				 pc->cap);
	}

	/* reply with identity matrix to avoid 2x2 BF negative gain */
	bfee->fb_identity_matrix = (nrow == 1 && tx_ant == 2);
}

static enum mcu_mmps_mode
mt7915_mcu_get_mmps_mode(enum ieee80211_smps_mode smps)
{
	switch (smps) {
	case IEEE80211_SMPS_OFF:
		return MCU_MMPS_DISABLE;
	case IEEE80211_SMPS_STATIC:
		return MCU_MMPS_STATIC;
	case IEEE80211_SMPS_DYNAMIC:
		return MCU_MMPS_DYNAMIC;
	default:
		return MCU_MMPS_DISABLE;
	}
}

int mt7915_mcu_set_fixed_rate_ctrl(struct mt7915_dev *dev,
				   struct ieee80211_vif *vif,
				   struct ieee80211_sta *sta,
				   void *data, u32 field)
{
	struct mt7915_vif *mvif = (struct mt7915_vif *)vif->drv_priv;
	struct mt7915_sta *msta = (struct mt7915_sta *)sta->drv_priv;
	struct sta_phy *phy = data;
	struct sta_rec_ra_fixed *ra;
	struct sk_buff *skb;
	struct tlv *tlv;

	skb = mt76_connac_mcu_alloc_sta_req(&dev->mt76, &mvif->mt76,
					    &msta->wcid);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	tlv = mt76_connac_mcu_add_tlv(skb, STA_REC_RA_UPDATE, sizeof(*ra));
	ra = (struct sta_rec_ra_fixed *)tlv;

	switch (field) {
	case RATE_PARAM_AUTO:
		break;
	case RATE_PARAM_FIXED:
	case RATE_PARAM_FIXED_MCS:
	case RATE_PARAM_FIXED_GI:
	case RATE_PARAM_FIXED_HE_LTF:
		if (phy)
			ra->phy = *phy;
		break;
	case RATE_PARAM_MMPS_UPDATE:
		ra->mmps_mode = mt7915_mcu_get_mmps_mode(sta->deflink.smps_mode);
		break;
	case RATE_PARAM_SPE_UPDATE:
		ra->spe_idx = *(u8 *)data;
		break;
	default:
		break;
	}
	ra->field = cpu_to_le32(field);

	return mt76_mcu_skb_send_msg(&dev->mt76, skb,
				     MCU_EXT_CMD(STA_REC_UPDATE), true);
}

int mt7915_mcu_add_smps(struct mt7915_dev *dev, struct ieee80211_vif *vif,
			struct ieee80211_sta *sta)
{
	struct mt7915_vif *mvif = (struct mt7915_vif *)vif->drv_priv;
	struct mt7915_sta *msta = (struct mt7915_sta *)sta->drv_priv;
	struct wtbl_req_hdr *wtbl_hdr;
	struct tlv *sta_wtbl;
	struct sk_buff *skb;
	int ret;

	skb = mt76_connac_mcu_alloc_sta_req(&dev->mt76, &mvif->mt76,
					    &msta->wcid);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	sta_wtbl = mt76_connac_mcu_add_tlv(skb, STA_REC_WTBL,
					   sizeof(struct tlv));
	wtbl_hdr = mt76_connac_mcu_alloc_wtbl_req(&dev->mt76, &msta->wcid,
						  WTBL_SET, sta_wtbl, &skb);
	if (IS_ERR(wtbl_hdr))
		return PTR_ERR(wtbl_hdr);

	mt76_connac_mcu_wtbl_smps_tlv(skb, sta, sta_wtbl, wtbl_hdr);

	ret = mt76_mcu_skb_send_msg(&dev->mt76, skb,
				    MCU_EXT_CMD(STA_REC_UPDATE), true);
	if (ret)
		return ret;

	return mt7915_mcu_set_fixed_rate_ctrl(dev, vif, sta, NULL,
					      RATE_PARAM_MMPS_UPDATE);
}

static int
mt7915_mcu_set_spe_idx(struct mt7915_dev *dev, struct ieee80211_vif *vif,
		       struct ieee80211_sta *sta)
{
	struct mt7915_vif *mvif = (struct mt7915_vif *)vif->drv_priv;
	struct mt76_phy *mphy = mvif->phy->mt76;
	u8 spe_idx = mt76_connac_spe_idx(mphy->antenna_mask);

	return mt7915_mcu_set_fixed_rate_ctrl(dev, vif, sta, &spe_idx,
					      RATE_PARAM_SPE_UPDATE);
}

static int
mt7915_mcu_add_rate_ctrl_fixed(struct mt7915_dev *dev,
			       struct ieee80211_vif *vif,
			       struct ieee80211_sta *sta)
{
	struct mt7915_vif *mvif = (struct mt7915_vif *)vif->drv_priv;
	struct cfg80211_chan_def *chandef = &mvif->phy->mt76->chandef;
	struct cfg80211_bitrate_mask *mask = &mvif->bitrate_mask;
	enum nl80211_band band = chandef->chan->band;
	struct sta_phy phy = {};
	int ret, nrates = 0;

#define __sta_phy_bitrate_mask_check(_mcs, _gi, _ht, _he)			\
	do {									\
		u8 i, gi = mask->control[band]._gi;				\
		gi = (_he) ? gi : gi == NL80211_TXRATE_FORCE_SGI;		\
		for (i = 0; i <= sta->deflink.bandwidth; i++) {			\
			phy.sgi |= gi << (i << (_he));				\
			phy.he_ltf |= mask->control[band].he_ltf << (i << (_he));\
		}								\
		for (i = 0; i < ARRAY_SIZE(mask->control[band]._mcs); i++) {	\
			if (!mask->control[band]._mcs[i])			\
				continue;					\
			nrates += hweight16(mask->control[band]._mcs[i]);	\
			phy.mcs = ffs(mask->control[band]._mcs[i]) - 1;		\
			if (_ht)						\
				phy.mcs += 8 * i;				\
		}								\
	} while (0)

	if (sta->deflink.he_cap.has_he) {
		__sta_phy_bitrate_mask_check(he_mcs, he_gi, 0, 1);
	} else if (sta->deflink.vht_cap.vht_supported) {
		__sta_phy_bitrate_mask_check(vht_mcs, gi, 0, 0);
	} else if (sta->deflink.ht_cap.ht_supported) {
		__sta_phy_bitrate_mask_check(ht_mcs, gi, 1, 0);
	} else {
		nrates = hweight32(mask->control[band].legacy);
		phy.mcs = ffs(mask->control[band].legacy) - 1;
	}
#undef __sta_phy_bitrate_mask_check

	/* fall back to auto rate control */
	if (mask->control[band].gi == NL80211_TXRATE_DEFAULT_GI &&
	    mask->control[band].he_gi == GENMASK(7, 0) &&
	    mask->control[band].he_ltf == GENMASK(7, 0) &&
	    nrates != 1)
		return 0;

	/* fixed single rate */
	if (nrates == 1) {
		ret = mt7915_mcu_set_fixed_rate_ctrl(dev, vif, sta, &phy,
						     RATE_PARAM_FIXED_MCS);
		if (ret)
			return ret;
	}

	/* fixed GI */
	if (mask->control[band].gi != NL80211_TXRATE_DEFAULT_GI ||
	    mask->control[band].he_gi != GENMASK(7, 0)) {
		struct mt7915_sta *msta = (struct mt7915_sta *)sta->drv_priv;
		u32 addr;

		/* firmware updates only TXCMD but doesn't take WTBL into
		 * account, so driver should update here to reflect the
		 * actual txrate hardware sends out.
		 */
		addr = mt7915_mac_wtbl_lmac_addr(dev, msta->wcid.idx, 7);
		if (sta->deflink.he_cap.has_he)
			mt76_rmw_field(dev, addr, GENMASK(31, 24), phy.sgi);
		else
			mt76_rmw_field(dev, addr, GENMASK(15, 12), phy.sgi);

		ret = mt7915_mcu_set_fixed_rate_ctrl(dev, vif, sta, &phy,
						     RATE_PARAM_FIXED_GI);
		if (ret)
			return ret;
	}

	/* fixed HE_LTF */
	if (mask->control[band].he_ltf != GENMASK(7, 0)) {
		ret = mt7915_mcu_set_fixed_rate_ctrl(dev, vif, sta, &phy,
						     RATE_PARAM_FIXED_HE_LTF);
		if (ret)
			return ret;
	}

	return mt7915_mcu_set_spe_idx(dev, vif, sta);
}

static void
mt7915_mcu_sta_rate_ctrl_tlv(struct sk_buff *skb, struct mt7915_dev *dev,
			     struct ieee80211_vif *vif, struct ieee80211_sta *sta)
{
	struct mt7915_vif *mvif = (struct mt7915_vif *)vif->drv_priv;
	struct mt76_phy *mphy = mvif->phy->mt76;
	struct cfg80211_chan_def *chandef = &mphy->chandef;
	struct cfg80211_bitrate_mask *mask = &mvif->bitrate_mask;
	enum nl80211_band band = chandef->chan->band;
	struct sta_rec_ra *ra;
	struct tlv *tlv;
	u32 supp_rate = sta->deflink.supp_rates[band];
	u32 cap = sta->wme ? STA_CAP_WMM : 0;

	tlv = mt76_connac_mcu_add_tlv(skb, STA_REC_RA, sizeof(*ra));
	ra = (struct sta_rec_ra *)tlv;

	ra->valid = true;
	ra->auto_rate = true;
	ra->phy_mode = mt76_connac_get_phy_mode(mphy, vif, band, &sta->deflink);
	ra->channel = chandef->chan->hw_value;
	ra->bw = sta->deflink.bandwidth;
	ra->phy.bw = sta->deflink.bandwidth;
	ra->mmps_mode = mt7915_mcu_get_mmps_mode(sta->deflink.smps_mode);

	if (supp_rate) {
		supp_rate &= mask->control[band].legacy;
		ra->rate_len = hweight32(supp_rate);

		if (band == NL80211_BAND_2GHZ) {
			ra->supp_mode = MODE_CCK;
			ra->supp_cck_rate = supp_rate & GENMASK(3, 0);

			if (ra->rate_len > 4) {
				ra->supp_mode |= MODE_OFDM;
				ra->supp_ofdm_rate = supp_rate >> 4;
			}
		} else {
			ra->supp_mode = MODE_OFDM;
			ra->supp_ofdm_rate = supp_rate;
		}
	}

	if (sta->deflink.ht_cap.ht_supported) {
		ra->supp_mode |= MODE_HT;
		ra->af = sta->deflink.ht_cap.ampdu_factor;
		ra->ht_gf = !!(sta->deflink.ht_cap.cap & IEEE80211_HT_CAP_GRN_FLD);

		cap |= STA_CAP_HT;
		if (sta->deflink.ht_cap.cap & IEEE80211_HT_CAP_SGI_20)
			cap |= STA_CAP_SGI_20;
		if (sta->deflink.ht_cap.cap & IEEE80211_HT_CAP_SGI_40)
			cap |= STA_CAP_SGI_40;
		if (sta->deflink.ht_cap.cap & IEEE80211_HT_CAP_TX_STBC)
			cap |= STA_CAP_TX_STBC;
		if (sta->deflink.ht_cap.cap & IEEE80211_HT_CAP_RX_STBC)
			cap |= STA_CAP_RX_STBC;
		if (mvif->cap.ht_ldpc &&
		    (sta->deflink.ht_cap.cap & IEEE80211_HT_CAP_LDPC_CODING))
			cap |= STA_CAP_LDPC;

		mt7915_mcu_set_sta_ht_mcs(sta, ra->ht_mcs,
					  mask->control[band].ht_mcs);
		ra->supp_ht_mcs = *(__le32 *)ra->ht_mcs;
	}

	if (sta->deflink.vht_cap.vht_supported) {
		u8 af;

		ra->supp_mode |= MODE_VHT;
		af = FIELD_GET(IEEE80211_VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_MASK,
			       sta->deflink.vht_cap.cap);
		ra->af = max_t(u8, ra->af, af);

		cap |= STA_CAP_VHT;
		if (sta->deflink.vht_cap.cap & IEEE80211_VHT_CAP_SHORT_GI_80)
			cap |= STA_CAP_VHT_SGI_80;
		if (sta->deflink.vht_cap.cap & IEEE80211_VHT_CAP_SHORT_GI_160)
			cap |= STA_CAP_VHT_SGI_160;
		if (sta->deflink.vht_cap.cap & IEEE80211_VHT_CAP_TXSTBC)
			cap |= STA_CAP_VHT_TX_STBC;
		if (sta->deflink.vht_cap.cap & IEEE80211_VHT_CAP_RXSTBC_1)
			cap |= STA_CAP_VHT_RX_STBC;
		if (mvif->cap.vht_ldpc &&
		    (sta->deflink.vht_cap.cap & IEEE80211_VHT_CAP_RXLDPC))
			cap |= STA_CAP_VHT_LDPC;

		mt7915_mcu_set_sta_vht_mcs(sta, ra->supp_vht_mcs,
					   mask->control[band].vht_mcs);
	}

	if (sta->deflink.he_cap.has_he) {
		ra->supp_mode |= MODE_HE;
		cap |= STA_CAP_HE;

		if (sta->deflink.he_6ghz_capa.capa)
			ra->af = le16_get_bits(sta->deflink.he_6ghz_capa.capa,
					       IEEE80211_HE_6GHZ_CAP_MAX_AMPDU_LEN_EXP);
	}

	ra->sta_cap = cpu_to_le32(cap);
}

int mt7915_mcu_add_rate_ctrl(struct mt7915_dev *dev, struct ieee80211_vif *vif,
			     struct ieee80211_sta *sta, bool changed)
{
	struct mt7915_vif *mvif = (struct mt7915_vif *)vif->drv_priv;
	struct mt7915_sta *msta = (struct mt7915_sta *)sta->drv_priv;
	struct sk_buff *skb;
	int ret;

	skb = mt76_connac_mcu_alloc_sta_req(&dev->mt76, &mvif->mt76,
					    &msta->wcid);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	/* firmware rc algorithm refers to sta_rec_he for HE control.
	 * once dev->rc_work changes the settings driver should also
	 * update sta_rec_he here.
	 */
	if (changed)
		mt7915_mcu_sta_he_tlv(skb, sta, vif);

	/* sta_rec_ra accommodates BW, NSS and only MCS range format
	 * i.e 0-{7,8,9} for VHT.
	 */
	mt7915_mcu_sta_rate_ctrl_tlv(skb, dev, vif, sta);

	ret = mt76_mcu_skb_send_msg(&dev->mt76, skb,
				    MCU_EXT_CMD(STA_REC_UPDATE), true);
	if (ret)
		return ret;

	/* sta_rec_ra_fixed accommodates single rate, (HE)GI and HE_LTE,
	 * and updates as peer fixed rate parameters, which overrides
	 * sta_rec_ra and firmware rate control algorithm.
	 */
	return mt7915_mcu_add_rate_ctrl_fixed(dev, vif, sta);
}

static int
mt7915_mcu_add_group(struct mt7915_dev *dev, struct ieee80211_vif *vif,
		     struct ieee80211_sta *sta)
{
#define MT_STA_BSS_GROUP		1
	struct mt7915_vif *mvif = (struct mt7915_vif *)vif->drv_priv;
	struct mt7915_sta *msta;
	struct {
		__le32 action;
		u8 wlan_idx_lo;
		u8 status;
		u8 wlan_idx_hi;
		u8 rsv0[5];
		__le32 val;
		u8 rsv1[8];
	} __packed req = {
		.action = cpu_to_le32(MT_STA_BSS_GROUP),
		.val = cpu_to_le32(mvif->mt76.idx % 16),
	};

	msta = sta ? (struct mt7915_sta *)sta->drv_priv : &mvif->sta;
	req.wlan_idx_lo = to_wcid_lo(msta->wcid.idx);
	req.wlan_idx_hi = to_wcid_hi(msta->wcid.idx);

	return mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD(SET_DRR_CTRL), &req,
				 sizeof(req), true);
}

int mt7915_mcu_add_sta(struct mt7915_dev *dev, struct ieee80211_vif *vif,
		       struct ieee80211_sta *sta, int conn_state, bool newly)
{
	struct mt7915_vif *mvif = (struct mt7915_vif *)vif->drv_priv;
	struct ieee80211_link_sta *link_sta;
	struct mt7915_sta *msta;
	struct sk_buff *skb;
	int ret;

	msta = sta ? (struct mt7915_sta *)sta->drv_priv : &mvif->sta;
	link_sta = sta ? &sta->deflink : NULL;

	skb = mt76_connac_mcu_alloc_sta_req(&dev->mt76, &mvif->mt76,
					    &msta->wcid);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	/* starec basic */
	mt76_connac_mcu_sta_basic_tlv(&dev->mt76, skb, &vif->bss_conf, link_sta,
				      conn_state, newly);
	/* tag order is in accordance with firmware dependency. */
	if (sta && conn_state != CONN_STATE_DISCONNECT) {
		/* starec bfer */
		mt7915_mcu_sta_bfer_tlv(dev, skb, vif, sta);
		/* starec ht */
		mt7915_mcu_sta_ht_tlv(skb, sta);
		/* starec vht */
		mt7915_mcu_sta_vht_tlv(skb, sta);
		/* starec uapsd */
		mt76_connac_mcu_sta_uapsd(skb, vif, sta);
	}

	if (newly || conn_state != CONN_STATE_DISCONNECT) {
		ret = mt7915_mcu_sta_wtbl_tlv(dev, skb, vif, sta);
		if (ret) {
			dev_kfree_skb(skb);
			return ret;
		}
	}

	if (conn_state == CONN_STATE_DISCONNECT)
		goto out;

	if (sta) {
		/* starec amsdu */
		mt7915_mcu_sta_amsdu_tlv(dev, skb, vif, sta);
		/* starec he */
		mt7915_mcu_sta_he_tlv(skb, sta, vif);
		/* starec muru */
		mt7915_mcu_sta_muru_tlv(dev, skb, sta, vif);
		/* starec bfee */
		mt7915_mcu_sta_bfee_tlv(dev, skb, vif, sta);
	}

	ret = mt7915_mcu_add_group(dev, vif, sta);
	if (ret) {
		dev_kfree_skb(skb);
		return ret;
	}
out:
	ret = mt76_connac_mcu_sta_wed_update(&dev->mt76, skb);
	if (ret)
		return ret;

	return mt76_mcu_skb_send_msg(&dev->mt76, skb,
				     MCU_EXT_CMD(STA_REC_UPDATE), true);
}

int mt7915_mcu_wed_enable_rx_stats(struct mt7915_dev *dev)
{
#ifdef CONFIG_NET_MEDIATEK_SOC_WED
	struct mtk_wed_device *wed = &dev->mt76.mmio.wed;
	struct {
		__le32 args[2];
	} req = {
		.args[0] = cpu_to_le32(1),
		.args[1] = cpu_to_le32(6),
	};

	return mtk_wed_device_update_msg(wed, MTK_WED_WO_CMD_RXCNT_CTRL,
					 &req, sizeof(req));
#else
	return 0;
#endif
}

int mt7915_mcu_add_dev_info(struct mt7915_phy *phy,
			    struct ieee80211_vif *vif, bool enable)
{
	struct mt7915_dev *dev = phy->dev;
	struct mt7915_vif *mvif = (struct mt7915_vif *)vif->drv_priv;
	struct {
		struct req_hdr {
			u8 omac_idx;
			u8 band_idx;
			__le16 tlv_num;
			u8 is_tlv_append;
			u8 rsv[3];
		} __packed hdr;
		struct req_tlv {
			__le16 tag;
			__le16 len;
			u8 active;
			u8 band_idx;
			u8 omac_addr[ETH_ALEN];
		} __packed tlv;
	} data = {
		.hdr = {
			.omac_idx = mvif->mt76.omac_idx,
			.band_idx = mvif->mt76.band_idx,
			.tlv_num = cpu_to_le16(1),
			.is_tlv_append = 1,
		},
		.tlv = {
			.tag = cpu_to_le16(DEV_INFO_ACTIVE),
			.len = cpu_to_le16(sizeof(struct req_tlv)),
			.active = enable,
			.band_idx = mvif->mt76.band_idx,
		},
	};

	if (mvif->mt76.omac_idx >= REPEATER_BSSID_START)
		return mt7915_mcu_muar_config(phy, vif, false, enable);

	memcpy(data.tlv.omac_addr, vif->addr, ETH_ALEN);
	return mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD(DEV_INFO_UPDATE),
				 &data, sizeof(data), true);
}

static void
mt7915_mcu_beacon_cntdwn(struct ieee80211_vif *vif, struct sk_buff *rskb,
			 struct sk_buff *skb, struct bss_info_bcn *bcn,
			 struct ieee80211_mutable_offsets *offs)
{
	struct bss_info_bcn_cntdwn *info;
	struct tlv *tlv;
	int sub_tag;

	if (!offs->cntdwn_counter_offs[0])
		return;

	sub_tag = vif->bss_conf.csa_active ? BSS_INFO_BCN_CSA : BSS_INFO_BCN_BCC;
	tlv = mt7915_mcu_add_nested_subtlv(rskb, sub_tag, sizeof(*info),
					   &bcn->sub_ntlv, &bcn->len);
	info = (struct bss_info_bcn_cntdwn *)tlv;
	info->cnt = skb->data[offs->cntdwn_counter_offs[0]];
}

static void
mt7915_mcu_beacon_mbss(struct sk_buff *rskb, struct sk_buff *skb,
		       struct ieee80211_vif *vif, struct bss_info_bcn *bcn,
		       struct ieee80211_mutable_offsets *offs)
{
	struct bss_info_bcn_mbss *mbss;
	const struct element *elem;
	struct tlv *tlv;

	if (!vif->bss_conf.bssid_indicator)
		return;

	tlv = mt7915_mcu_add_nested_subtlv(rskb, BSS_INFO_BCN_MBSSID,
					   sizeof(*mbss), &bcn->sub_ntlv,
					   &bcn->len);

	mbss = (struct bss_info_bcn_mbss *)tlv;
	mbss->offset[0] = cpu_to_le16(offs->tim_offset);
	mbss->bitmap = cpu_to_le32(1);

	for_each_element_id(elem, WLAN_EID_MULTIPLE_BSSID,
			    &skb->data[offs->mbssid_off],
			    skb->len - offs->mbssid_off) {
		const struct element *sub_elem;

		if (elem->datalen < 2)
			continue;

		for_each_element(sub_elem, elem->data + 1, elem->datalen - 1) {
			const struct ieee80211_bssid_index *idx;
			const u8 *idx_ie;

			if (sub_elem->id || sub_elem->datalen < 4)
				continue; /* not a valid BSS profile */

			/* Find WLAN_EID_MULTI_BSSID_IDX
			 * in the merged nontransmitted profile
			 */
			idx_ie = cfg80211_find_ie(WLAN_EID_MULTI_BSSID_IDX,
						  sub_elem->data,
						  sub_elem->datalen);
			if (!idx_ie || idx_ie[1] < sizeof(*idx))
				continue;

			idx = (void *)(idx_ie + 2);
			if (!idx->bssid_index || idx->bssid_index > 31)
				continue;

			mbss->offset[idx->bssid_index] =
				cpu_to_le16(idx_ie - skb->data);
			mbss->bitmap |= cpu_to_le32(BIT(idx->bssid_index));
		}
	}
}

static void
mt7915_mcu_beacon_cont(struct mt7915_dev *dev, struct ieee80211_vif *vif,
		       struct sk_buff *rskb, struct sk_buff *skb,
		       struct bss_info_bcn *bcn,
		       struct ieee80211_mutable_offsets *offs)
{
	struct mt76_wcid *wcid = &dev->mt76.global_wcid;
	struct bss_info_bcn_cont *cont;
	struct tlv *tlv;
	u8 *buf;
	int len = sizeof(*cont) + MT_TXD_SIZE + skb->len;

	len = (len & 0x3) ? ((len | 0x3) + 1) : len;
	tlv = mt7915_mcu_add_nested_subtlv(rskb, BSS_INFO_BCN_CONTENT,
					   len, &bcn->sub_ntlv, &bcn->len);

	cont = (struct bss_info_bcn_cont *)tlv;
	cont->pkt_len = cpu_to_le16(MT_TXD_SIZE + skb->len);
	cont->tim_ofs = cpu_to_le16(offs->tim_offset);

	if (offs->cntdwn_counter_offs[0]) {
		u16 offset = offs->cntdwn_counter_offs[0];

		if (vif->bss_conf.csa_active)
			cont->csa_ofs = cpu_to_le16(offset - 4);
		if (vif->bss_conf.color_change_active)
			cont->bcc_ofs = cpu_to_le16(offset - 3);
	}

	buf = (u8 *)tlv + sizeof(*cont);
	mt7915_mac_write_txwi(&dev->mt76, (__le32 *)buf, skb, wcid, 0, NULL,
			      0, BSS_CHANGED_BEACON);
	memcpy(buf + MT_TXD_SIZE, skb->data, skb->len);
}

int
mt7915_mcu_add_inband_discov(struct mt7915_dev *dev, struct ieee80211_vif *vif,
			     u32 changed)
{
#define OFFLOAD_TX_MODE_SU	BIT(0)
#define OFFLOAD_TX_MODE_MU	BIT(1)
	struct ieee80211_hw *hw = mt76_hw(dev);
	struct mt7915_phy *phy = mt7915_hw_phy(hw);
	struct mt7915_vif *mvif = (struct mt7915_vif *)vif->drv_priv;
	struct cfg80211_chan_def *chandef = &mvif->phy->mt76->chandef;
	enum nl80211_band band = chandef->chan->band;
	struct mt76_wcid *wcid = &dev->mt76.global_wcid;
	struct bss_info_bcn *bcn;
	struct bss_info_inband_discovery *discov;
	struct ieee80211_tx_info *info;
	struct sk_buff *rskb, *skb = NULL;
	struct tlv *tlv, *sub_tlv;
	bool ext_phy = phy != &dev->phy;
	u8 *buf, interval;
	int len;

	if (vif->bss_conf.nontransmitted)
		return 0;

	rskb = __mt76_connac_mcu_alloc_sta_req(&dev->mt76, &mvif->mt76, NULL,
					       MT7915_MAX_BSS_OFFLOAD_SIZE);
	if (IS_ERR(rskb))
		return PTR_ERR(rskb);

	tlv = mt76_connac_mcu_add_tlv(rskb, BSS_INFO_OFFLOAD, sizeof(*bcn));
	bcn = (struct bss_info_bcn *)tlv;
	bcn->enable = true;

	if (changed & BSS_CHANGED_FILS_DISCOVERY) {
		interval = vif->bss_conf.fils_discovery.max_interval;
		skb = ieee80211_get_fils_discovery_tmpl(hw, vif);
	} else if (changed & BSS_CHANGED_UNSOL_BCAST_PROBE_RESP &&
		   vif->bss_conf.unsol_bcast_probe_resp_interval) {
		interval = vif->bss_conf.unsol_bcast_probe_resp_interval;
		skb = ieee80211_get_unsol_bcast_probe_resp_tmpl(hw, vif);
	}

	if (!skb) {
		dev_kfree_skb(rskb);
		return -EINVAL;
	}

	info = IEEE80211_SKB_CB(skb);
	info->control.vif = vif;
	info->band = band;
	info->hw_queue |= FIELD_PREP(MT_TX_HW_QUEUE_PHY, ext_phy);

	len = sizeof(*discov) + MT_TXD_SIZE + skb->len;
	len = (len & 0x3) ? ((len | 0x3) + 1) : len;

	if (skb->len > MT7915_MAX_BEACON_SIZE) {
		dev_err(dev->mt76.dev, "inband discovery size limit exceed\n");
		dev_kfree_skb(rskb);
		dev_kfree_skb(skb);
		return -EINVAL;
	}

	sub_tlv = mt7915_mcu_add_nested_subtlv(rskb, BSS_INFO_BCN_DISCOV,
					       len, &bcn->sub_ntlv, &bcn->len);
	discov = (struct bss_info_inband_discovery *)sub_tlv;
	discov->tx_mode = OFFLOAD_TX_MODE_SU;
	/* 0: UNSOL PROBE RESP, 1: FILS DISCOV */
	discov->tx_type = !!(changed & BSS_CHANGED_FILS_DISCOVERY);
	discov->tx_interval = interval;
	discov->prob_rsp_len = cpu_to_le16(MT_TXD_SIZE + skb->len);
	discov->enable = !!interval;

	buf = (u8 *)sub_tlv + sizeof(*discov);

	mt7915_mac_write_txwi(&dev->mt76, (__le32 *)buf, skb, wcid, 0, NULL,
			      0, changed);
	memcpy(buf + MT_TXD_SIZE, skb->data, skb->len);

	dev_kfree_skb(skb);

	return mt76_mcu_skb_send_msg(&phy->dev->mt76, rskb,
				     MCU_EXT_CMD(BSS_INFO_UPDATE), true);
}

int mt7915_mcu_add_beacon(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			  int en, u32 changed)
{
	struct mt7915_dev *dev = mt7915_hw_dev(hw);
	struct mt7915_phy *phy = mt7915_hw_phy(hw);
	struct mt7915_vif *mvif = (struct mt7915_vif *)vif->drv_priv;
	struct ieee80211_mutable_offsets offs;
	struct ieee80211_tx_info *info;
	struct sk_buff *skb, *rskb;
	struct tlv *tlv;
	struct bss_info_bcn *bcn;
	int len = MT7915_MAX_BSS_OFFLOAD_SIZE;
	bool ext_phy = phy != &dev->phy;

	if (vif->bss_conf.nontransmitted)
		return 0;

	rskb = __mt76_connac_mcu_alloc_sta_req(&dev->mt76, &mvif->mt76,
					       NULL, len);
	if (IS_ERR(rskb))
		return PTR_ERR(rskb);

	tlv = mt76_connac_mcu_add_tlv(rskb, BSS_INFO_OFFLOAD, sizeof(*bcn));
	bcn = (struct bss_info_bcn *)tlv;
	bcn->enable = en;

	if (!en)
		goto out;

	skb = ieee80211_beacon_get_template(hw, vif, &offs, 0);
	if (!skb) {
		dev_kfree_skb(rskb);
		return -EINVAL;
	}

	if (skb->len > MT7915_MAX_BEACON_SIZE) {
		dev_err(dev->mt76.dev, "Bcn size limit exceed\n");
		dev_kfree_skb(rskb);
		dev_kfree_skb(skb);
		return -EINVAL;
	}

	info = IEEE80211_SKB_CB(skb);
	info->hw_queue = FIELD_PREP(MT_TX_HW_QUEUE_PHY, ext_phy);

	mt7915_mcu_beacon_cntdwn(vif, rskb, skb, bcn, &offs);
	mt7915_mcu_beacon_mbss(rskb, skb, vif, bcn, &offs);
	mt7915_mcu_beacon_cont(dev, vif, rskb, skb, bcn, &offs);
	dev_kfree_skb(skb);

out:
	return mt76_mcu_skb_send_msg(&phy->dev->mt76, rskb,
				     MCU_EXT_CMD(BSS_INFO_UPDATE), true);
}

static int mt7915_driver_own(struct mt7915_dev *dev, u8 band)
{
	mt76_wr(dev, MT_TOP_LPCR_HOST_BAND(band), MT_TOP_LPCR_HOST_DRV_OWN);
	if (!mt76_poll_msec(dev, MT_TOP_LPCR_HOST_BAND(band),
			    MT_TOP_LPCR_HOST_FW_OWN_STAT, 0, 500)) {
		dev_err(dev->mt76.dev, "Timeout for driver own\n");
		return -EIO;
	}

	/* clear irq when the driver own success */
	mt76_wr(dev, MT_TOP_LPCR_HOST_BAND_IRQ_STAT(band),
		MT_TOP_LPCR_HOST_BAND_STAT);

	return 0;
}

static int
mt7915_firmware_state(struct mt7915_dev *dev, bool wa)
{
	u32 state = FIELD_PREP(MT_TOP_MISC_FW_STATE,
			       wa ? FW_STATE_RDY : FW_STATE_FW_DOWNLOAD);

	if (!mt76_poll_msec(dev, MT_TOP_MISC, MT_TOP_MISC_FW_STATE,
			    state, 1000)) {
		dev_err(dev->mt76.dev, "Timeout for initializing firmware\n");
		return -EIO;
	}
	return 0;
}

static int mt7915_load_firmware(struct mt7915_dev *dev)
{
	int ret;

	/* make sure fw is download state */
	if (mt7915_firmware_state(dev, false)) {
		/* restart firmware once */
		mt76_connac_mcu_restart(&dev->mt76);
		ret = mt7915_firmware_state(dev, false);
		if (ret) {
			dev_err(dev->mt76.dev,
				"Firmware is not ready for download\n");
			return ret;
		}
	}

	ret = mt76_connac2_load_patch(&dev->mt76, fw_name_var(dev, ROM_PATCH));
	if (ret)
		return ret;

	ret = mt76_connac2_load_ram(&dev->mt76, fw_name_var(dev, FIRMWARE_WM),
				    fw_name(dev, FIRMWARE_WA));
	if (ret)
		return ret;

	ret = mt7915_firmware_state(dev, true);
	if (ret)
		return ret;

	mt76_queue_tx_cleanup(dev, dev->mt76.q_mcu[MT_MCUQ_FWDL], false);

	dev_dbg(dev->mt76.dev, "Firmware init done\n");

	return 0;
}

int mt7915_mcu_fw_log_2_host(struct mt7915_dev *dev, u8 type, u8 ctrl)
{
	struct {
		u8 ctrl_val;
		u8 pad[3];
	} data = {
		.ctrl_val = ctrl
	};

	if (type == MCU_FW_LOG_WA)
		return mt76_mcu_send_msg(&dev->mt76, MCU_WA_EXT_CMD(FW_LOG_2_HOST),
					 &data, sizeof(data), true);

	return mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD(FW_LOG_2_HOST), &data,
				 sizeof(data), true);
}

int mt7915_mcu_fw_dbg_ctrl(struct mt7915_dev *dev, u32 module, u8 level)
{
	struct {
		u8 ver;
		u8 pad;
		__le16 len;
		u8 level;
		u8 rsv[3];
		__le32 module_idx;
	} data = {
		.module_idx = cpu_to_le32(module),
		.level = level,
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD(FW_DBG_CTRL), &data,
				 sizeof(data), false);
}

int mt7915_mcu_muru_debug_set(struct mt7915_dev *dev, bool enabled)
{
	struct {
		__le32 cmd;
		u8 enable;
	} data = {
		.cmd = cpu_to_le32(MURU_SET_TXC_TX_STATS_EN),
		.enable = enabled,
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD(MURU_CTRL), &data,
				sizeof(data), false);
}

int mt7915_mcu_muru_debug_get(struct mt7915_phy *phy)
{
	struct mt7915_dev *dev = phy->dev;
	struct sk_buff *skb;
	struct mt7915_mcu_muru_stats *mu_stats;
	int ret;

	struct {
		__le32 cmd;
		u8 band_idx;
	} req = {
		.cmd = cpu_to_le32(MURU_GET_TXC_TX_STATS),
		.band_idx = phy->mt76->band_idx,
	};

	ret = mt76_mcu_send_and_get_msg(&dev->mt76, MCU_EXT_CMD(MURU_CTRL),
					&req, sizeof(req), true, &skb);
	if (ret)
		return ret;

	mu_stats = (struct mt7915_mcu_muru_stats *)(skb->data);

	/* accumulate stats, these are clear-on-read */
#define __dl_u32(s)	 phy->mib.dl_##s += le32_to_cpu(mu_stats->dl.s)
#define __ul_u32(s)	 phy->mib.ul_##s += le32_to_cpu(mu_stats->ul.s)
	__dl_u32(cck_cnt);
	__dl_u32(ofdm_cnt);
	__dl_u32(htmix_cnt);
	__dl_u32(htgf_cnt);
	__dl_u32(vht_su_cnt);
	__dl_u32(vht_2mu_cnt);
	__dl_u32(vht_3mu_cnt);
	__dl_u32(vht_4mu_cnt);
	__dl_u32(he_su_cnt);
	__dl_u32(he_2ru_cnt);
	__dl_u32(he_2mu_cnt);
	__dl_u32(he_3ru_cnt);
	__dl_u32(he_3mu_cnt);
	__dl_u32(he_4ru_cnt);
	__dl_u32(he_4mu_cnt);
	__dl_u32(he_5to8ru_cnt);
	__dl_u32(he_9to16ru_cnt);
	__dl_u32(he_gtr16ru_cnt);

	__ul_u32(hetrig_su_cnt);
	__ul_u32(hetrig_2ru_cnt);
	__ul_u32(hetrig_3ru_cnt);
	__ul_u32(hetrig_4ru_cnt);
	__ul_u32(hetrig_5to8ru_cnt);
	__ul_u32(hetrig_9to16ru_cnt);
	__ul_u32(hetrig_gtr16ru_cnt);
	__ul_u32(hetrig_2mu_cnt);
	__ul_u32(hetrig_3mu_cnt);
	__ul_u32(hetrig_4mu_cnt);
#undef __dl_u32
#undef __ul_u32

	dev_kfree_skb(skb);

	return 0;
}

static int mt7915_mcu_set_mwds(struct mt7915_dev *dev, bool enabled)
{
	struct {
		u8 enable;
		u8 _rsv[3];
	} __packed req = {
		.enable = enabled
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_WA_EXT_CMD(MWDS_SUPPORT), &req,
				 sizeof(req), false);
}

int mt7915_mcu_set_muru_ctrl(struct mt7915_dev *dev, u32 cmd, u32 val)
{
	struct {
		__le32 cmd;
		u8 val[4];
	} __packed req = {
		.cmd = cpu_to_le32(cmd),
	};

	put_unaligned_le32(val, req.val);

	return mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD(MURU_CTRL), &req,
				 sizeof(req), false);
}

static int
mt7915_mcu_init_rx_airtime(struct mt7915_dev *dev)
{
#define RX_AIRTIME_FEATURE_CTRL		1
#define RX_AIRTIME_BITWISE_CTRL		2
#define RX_AIRTIME_CLEAR_EN	1
	struct {
		__le16 field;
		__le16 sub_field;
		__le32 set_status;
		__le32 get_status;
		u8 _rsv[12];

		bool airtime_en;
		bool mibtime_en;
		bool earlyend_en;
		u8 _rsv1[9];

		bool airtime_clear;
		bool mibtime_clear;
		u8 _rsv2[98];
	} __packed req = {
		.field = cpu_to_le16(RX_AIRTIME_BITWISE_CTRL),
		.sub_field = cpu_to_le16(RX_AIRTIME_CLEAR_EN),
		.airtime_clear = true,
	};
	int ret;

	ret = mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD(RX_AIRTIME_CTRL), &req,
				sizeof(req), true);
	if (ret)
		return ret;

	req.field = cpu_to_le16(RX_AIRTIME_FEATURE_CTRL);
	req.sub_field = cpu_to_le16(RX_AIRTIME_CLEAR_EN);
	req.airtime_en = true;

	return mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD(RX_AIRTIME_CTRL), &req,
				 sizeof(req), true);
}

static int mt7915_red_set_watermark(struct mt7915_dev *dev)
{
#define RED_GLOBAL_TOKEN_WATERMARK 2
	struct {
		__le32 args[3];
		u8 cmd;
		u8 version;
		u8 __rsv1[4];
		__le16 len;
		__le16 high_mark;
		__le16 low_mark;
		u8 __rsv2[12];
	} __packed req = {
		.args[0] = cpu_to_le32(MCU_WA_PARAM_RED_SETTING),
		.cmd = RED_GLOBAL_TOKEN_WATERMARK,
		.len = cpu_to_le16(sizeof(req) - sizeof(req.args)),
		.high_mark = cpu_to_le16(MT7915_HW_TOKEN_SIZE - 256),
		.low_mark = cpu_to_le16(MT7915_HW_TOKEN_SIZE - 256 - 1536),
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_WA_PARAM_CMD(SET), &req,
				 sizeof(req), false);
}

static int mt7915_mcu_set_red(struct mt7915_dev *dev, bool enabled)
{
#define RED_DISABLE		0
#define RED_BY_WA_ENABLE	2
	int ret;
	u32 red_type = enabled ? RED_BY_WA_ENABLE : RED_DISABLE;
	__le32 req = cpu_to_le32(red_type);

	if (enabled) {
		ret = mt7915_red_set_watermark(dev);
		if (ret < 0)
			return ret;
	}

	ret = mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD(RED_ENABLE), &req,
				sizeof(req), false);
	if (ret < 0)
		return ret;

	return mt7915_mcu_wa_cmd(dev, MCU_WA_PARAM_CMD(SET),
				 MCU_WA_PARAM_RED, enabled, 0);
}

int mt7915_mcu_init_firmware(struct mt7915_dev *dev)
{
	int ret;

	/* force firmware operation mode into normal state,
	 * which should be set before firmware download stage.
	 */
	mt76_wr(dev, MT_SWDEF_MODE, MT_SWDEF_NORMAL_MODE);

	ret = mt7915_driver_own(dev, 0);
	if (ret)
		return ret;
	/* set driver own for band1 when two hif exist */
	if (dev->hif2) {
		ret = mt7915_driver_own(dev, 1);
		if (ret)
			return ret;
	}

	ret = mt7915_load_firmware(dev);
	if (ret)
		return ret;

	set_bit(MT76_STATE_MCU_RUNNING, &dev->mphy.state);
	ret = mt7915_mcu_fw_log_2_host(dev, MCU_FW_LOG_WM, 0);
	if (ret)
		return ret;

	ret = mt7915_mcu_fw_log_2_host(dev, MCU_FW_LOG_WA, 0);
	if (ret)
		return ret;

	mt76_connac_mcu_del_wtbl_all(&dev->mt76);

	if ((mtk_wed_device_active(&dev->mt76.mmio.wed) &&
	     is_mt7915(&dev->mt76)) ||
	    !mtk_wed_get_rx_capa(&dev->mt76.mmio.wed))
		mt7915_mcu_wa_cmd(dev, MCU_WA_PARAM_CMD(CAPABILITY), 0, 0, 0);

	ret = mt7915_mcu_set_mwds(dev, 1);
	if (ret)
		return ret;

	ret = mt7915_mcu_set_muru_ctrl(dev, MURU_SET_PLATFORM_TYPE,
				       MURU_PLATFORM_TYPE_PERF_LEVEL_2);
	if (ret)
		return ret;

	ret = mt7915_mcu_init_rx_airtime(dev);
	if (ret)
		return ret;

	return mt7915_mcu_set_red(dev, mtk_wed_device_active(&dev->mt76.mmio.wed));
}

int mt7915_mcu_init(struct mt7915_dev *dev)
{
	static const struct mt76_mcu_ops mt7915_mcu_ops = {
		.max_retry = 1,
		.headroom = sizeof(struct mt76_connac2_mcu_txd),
		.mcu_skb_prepare_msg = mt76_connac2_mcu_fill_message,
		.mcu_skb_send_msg = mt7915_mcu_send_message,
		.mcu_parse_response = mt7915_mcu_parse_response,
	};

	dev->mt76.mcu_ops = &mt7915_mcu_ops;

	return mt7915_mcu_init_firmware(dev);
}

void mt7915_mcu_exit(struct mt7915_dev *dev)
{
	mt76_connac_mcu_restart(&dev->mt76);
	if (mt7915_firmware_state(dev, false)) {
		dev_err(dev->mt76.dev, "Failed to exit mcu\n");
		goto out;
	}

	mt76_wr(dev, MT_TOP_LPCR_HOST_BAND(0), MT_TOP_LPCR_HOST_FW_OWN);
	if (dev->hif2)
		mt76_wr(dev, MT_TOP_LPCR_HOST_BAND(1),
			MT_TOP_LPCR_HOST_FW_OWN);
out:
	skb_queue_purge(&dev->mt76.mcu.res_q);
}

static int
mt7915_mcu_set_rx_hdr_trans_blacklist(struct mt7915_dev *dev, int band)
{
	struct {
		u8 operation;
		u8 count;
		u8 _rsv[2];
		u8 index;
		u8 enable;
		__le16 etype;
	} req = {
		.operation = 1,
		.count = 1,
		.enable = 1,
		.etype = cpu_to_le16(ETH_P_PAE),
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD(RX_HDR_TRANS),
				 &req, sizeof(req), false);
}

int mt7915_mcu_set_mac(struct mt7915_dev *dev, int band,
		       bool enable, bool hdr_trans)
{
	struct {
		u8 operation;
		u8 enable;
		u8 check_bssid;
		u8 insert_vlan;
		u8 remove_vlan;
		u8 tid;
		u8 mode;
		u8 rsv;
	} __packed req_trans = {
		.enable = hdr_trans,
	};
	struct {
		u8 enable;
		u8 band;
		u8 rsv[2];
	} __packed req_mac = {
		.enable = enable,
		.band = band,
	};
	int ret;

	ret = mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD(RX_HDR_TRANS),
				&req_trans, sizeof(req_trans), false);
	if (ret)
		return ret;

	if (hdr_trans)
		mt7915_mcu_set_rx_hdr_trans_blacklist(dev, band);

	return mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD(MAC_INIT_CTRL),
				 &req_mac, sizeof(req_mac), true);
}

int mt7915_mcu_update_edca(struct mt7915_dev *dev, void *param)
{
	struct mt7915_mcu_tx *req = (struct mt7915_mcu_tx *)param;
	u8 num = req->total;
	size_t len = sizeof(*req) -
		     (IEEE80211_NUM_ACS - num) * sizeof(struct edca);

	return mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD(EDCA_UPDATE), req,
				 len, true);
}

int mt7915_mcu_set_tx(struct mt7915_dev *dev, struct ieee80211_vif *vif)
{
#define TX_CMD_MODE		1
	struct mt7915_mcu_tx req = {
		.valid = true,
		.mode = TX_CMD_MODE,
		.total = IEEE80211_NUM_ACS,
	};
	struct mt7915_vif *mvif = (struct mt7915_vif *)vif->drv_priv;
	int ac;

	for (ac = 0; ac < IEEE80211_NUM_ACS; ac++) {
		struct ieee80211_tx_queue_params *q = &mvif->queue_params[ac];
		struct edca *e = &req.edca[ac];

		e->set = WMM_PARAM_SET;
		e->queue = ac + mvif->mt76.wmm_idx * MT76_CONNAC_MAX_WMM_SETS;
		e->aifs = q->aifs;
		e->txop = cpu_to_le16(q->txop);

		if (q->cw_min)
			e->cw_min = fls(q->cw_min);
		else
			e->cw_min = 5;

		if (q->cw_max)
			e->cw_max = cpu_to_le16(fls(q->cw_max));
		else
			e->cw_max = cpu_to_le16(10);
	}

	return mt7915_mcu_update_edca(dev, &req);
}

int mt7915_mcu_set_fcc5_lpn(struct mt7915_dev *dev, int val)
{
	struct {
		__le32 tag;
		__le16 min_lpn;
		u8 rsv[2];
	} __packed req = {
		.tag = cpu_to_le32(0x1),
		.min_lpn = cpu_to_le16(val),
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD(SET_RDD_TH), &req,
				 sizeof(req), true);
}

int mt7915_mcu_set_pulse_th(struct mt7915_dev *dev,
			    const struct mt7915_dfs_pulse *pulse)
{
	struct {
		__le32 tag;

		__le32 max_width;		/* us */
		__le32 max_pwr;			/* dbm */
		__le32 min_pwr;			/* dbm */
		__le32 min_stgr_pri;		/* us */
		__le32 max_stgr_pri;		/* us */
		__le32 min_cr_pri;		/* us */
		__le32 max_cr_pri;		/* us */
	} __packed req = {
		.tag = cpu_to_le32(0x3),

#define __req_field(field) .field = cpu_to_le32(pulse->field)
		__req_field(max_width),
		__req_field(max_pwr),
		__req_field(min_pwr),
		__req_field(min_stgr_pri),
		__req_field(max_stgr_pri),
		__req_field(min_cr_pri),
		__req_field(max_cr_pri),
#undef __req_field
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD(SET_RDD_TH), &req,
				 sizeof(req), true);
}

int mt7915_mcu_set_radar_th(struct mt7915_dev *dev, int index,
			    const struct mt7915_dfs_pattern *pattern)
{
	struct {
		__le32 tag;
		__le16 radar_type;

		u8 enb;
		u8 stgr;
		u8 min_crpn;
		u8 max_crpn;
		u8 min_crpr;
		u8 min_pw;
		__le32 min_pri;
		__le32 max_pri;
		u8 max_pw;
		u8 min_crbn;
		u8 max_crbn;
		u8 min_stgpn;
		u8 max_stgpn;
		u8 min_stgpr;
		u8 rsv[2];
		__le32 min_stgpr_diff;
	} __packed req = {
		.tag = cpu_to_le32(0x2),
		.radar_type = cpu_to_le16(index),

#define __req_field_u8(field) .field = pattern->field
#define __req_field_u32(field) .field = cpu_to_le32(pattern->field)
		__req_field_u8(enb),
		__req_field_u8(stgr),
		__req_field_u8(min_crpn),
		__req_field_u8(max_crpn),
		__req_field_u8(min_crpr),
		__req_field_u8(min_pw),
		__req_field_u32(min_pri),
		__req_field_u32(max_pri),
		__req_field_u8(max_pw),
		__req_field_u8(min_crbn),
		__req_field_u8(max_crbn),
		__req_field_u8(min_stgpn),
		__req_field_u8(max_stgpn),
		__req_field_u8(min_stgpr),
		__req_field_u32(min_stgpr_diff),
#undef __req_field_u8
#undef __req_field_u32
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD(SET_RDD_TH), &req,
				 sizeof(req), true);
}

static int
mt7915_mcu_background_chain_ctrl(struct mt7915_phy *phy,
				 struct cfg80211_chan_def *chandef,
				 int cmd)
{
	struct mt7915_dev *dev = phy->dev;
	struct mt76_phy *mphy = phy->mt76;
	struct ieee80211_channel *chan = mphy->chandef.chan;
	int freq = mphy->chandef.center_freq1;
	struct mt7915_mcu_background_chain_ctrl req = {
		.monitor_scan_type = 2, /* simple rx */
	};

	if (!chandef && cmd != CH_SWITCH_BACKGROUND_SCAN_STOP)
		return -EINVAL;

	if (!cfg80211_chandef_valid(&mphy->chandef))
		return -EINVAL;

	switch (cmd) {
	case CH_SWITCH_BACKGROUND_SCAN_START: {
		req.chan = chan->hw_value;
		req.central_chan = ieee80211_frequency_to_channel(freq);
		req.bw = mt76_connac_chan_bw(&mphy->chandef);
		req.monitor_chan = chandef->chan->hw_value;
		req.monitor_central_chan =
			ieee80211_frequency_to_channel(chandef->center_freq1);
		req.monitor_bw = mt76_connac_chan_bw(chandef);
		req.band_idx = phy->mt76->band_idx;
		req.scan_mode = 1;
		break;
	}
	case CH_SWITCH_BACKGROUND_SCAN_RUNNING:
		req.monitor_chan = chandef->chan->hw_value;
		req.monitor_central_chan =
			ieee80211_frequency_to_channel(chandef->center_freq1);
		req.band_idx = phy->mt76->band_idx;
		req.scan_mode = 2;
		break;
	case CH_SWITCH_BACKGROUND_SCAN_STOP:
		req.chan = chan->hw_value;
		req.central_chan = ieee80211_frequency_to_channel(freq);
		req.bw = mt76_connac_chan_bw(&mphy->chandef);
		req.tx_stream = hweight8(mphy->antenna_mask);
		req.rx_stream = mphy->antenna_mask;
		break;
	default:
		return -EINVAL;
	}
	req.band = chandef ? chandef->chan->band == NL80211_BAND_5GHZ : 1;

	return mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD(OFFCH_SCAN_CTRL),
				 &req, sizeof(req), false);
}

int mt7915_mcu_rdd_background_enable(struct mt7915_phy *phy,
				     struct cfg80211_chan_def *chandef)
{
	struct mt7915_dev *dev = phy->dev;
	int err, region, rdd_idx;

	rdd_idx = mt7915_get_rdd_idx(phy, true);
	if (rdd_idx < 0)
		return -EINVAL;

	if (!chandef) { /* disable offchain */
		err = mt76_connac_mcu_rdd_cmd(&dev->mt76, RDD_STOP, rdd_idx, 0, 0);
		if (err)
			return err;

		return mt7915_mcu_background_chain_ctrl(phy, NULL,
				CH_SWITCH_BACKGROUND_SCAN_STOP);
	}

	err = mt7915_mcu_background_chain_ctrl(phy, chandef,
					       CH_SWITCH_BACKGROUND_SCAN_START);
	if (err)
		return err;

	switch (dev->mt76.region) {
	case NL80211_DFS_ETSI:
		region = 0;
		break;
	case NL80211_DFS_JP:
		region = 2;
		break;
	case NL80211_DFS_FCC:
	default:
		region = 1;
		break;
	}

	return mt76_connac_mcu_rdd_cmd(&dev->mt76, RDD_START, rdd_idx, 0, region);
}

int mt7915_mcu_set_chan_info(struct mt7915_phy *phy, int cmd)
{
	static const u8 ch_band[] = {
		[NL80211_BAND_2GHZ] = 0,
		[NL80211_BAND_5GHZ] = 1,
		[NL80211_BAND_6GHZ] = 2,
	};
	struct mt7915_dev *dev = phy->dev;
	struct cfg80211_chan_def *chandef = &phy->mt76->chandef;
	int freq1 = chandef->center_freq1;
	u8 band = phy->mt76->band_idx;
	struct {
		u8 control_ch;
		u8 center_ch;
		u8 bw;
		u8 tx_path_num;
		u8 rx_path;	/* mask or num */
		u8 switch_reason;
		u8 band_idx;
		u8 center_ch2;	/* for 80+80 only */
		__le16 cac_case;
		u8 channel_band;
		u8 rsv0;
		__le32 outband_freq;
		u8 txpower_drop;
		u8 ap_bw;
		u8 ap_center_ch;
		u8 rsv1[57];
	} __packed req = {
		.control_ch = chandef->chan->hw_value,
		.center_ch = ieee80211_frequency_to_channel(freq1),
		.bw = mt76_connac_chan_bw(chandef),
		.tx_path_num = hweight16(phy->mt76->chainmask),
		.rx_path = phy->mt76->chainmask >> (dev->chainshift * band),
		.band_idx = band,
		.channel_band = ch_band[chandef->chan->band],
	};

#ifdef CONFIG_NL80211_TESTMODE
	if (phy->mt76->test.tx_antenna_mask &&
	    mt76_testmode_enabled(phy->mt76)) {
		req.tx_path_num = fls(phy->mt76->test.tx_antenna_mask);
		req.rx_path = phy->mt76->test.tx_antenna_mask;
	}
#endif

	if (mt76_connac_spe_idx(phy->mt76->antenna_mask))
		req.tx_path_num = fls(phy->mt76->antenna_mask);

	if (phy->mt76->hw->conf.flags & IEEE80211_CONF_MONITOR)
		req.switch_reason = CH_SWITCH_NORMAL;
	else if (phy->mt76->offchannel ||
		 phy->mt76->hw->conf.flags & IEEE80211_CONF_IDLE)
		req.switch_reason = CH_SWITCH_SCAN_BYPASS_DPD;
	else if (!cfg80211_reg_can_beacon(phy->mt76->hw->wiphy, chandef,
					  NL80211_IFTYPE_AP))
		req.switch_reason = CH_SWITCH_DFS;
	else
		req.switch_reason = CH_SWITCH_NORMAL;

	if (cmd == MCU_EXT_CMD(CHANNEL_SWITCH))
		req.rx_path = hweight8(req.rx_path);

	if (chandef->width == NL80211_CHAN_WIDTH_80P80) {
		int freq2 = chandef->center_freq2;

		req.center_ch2 = ieee80211_frequency_to_channel(freq2);
	}

	return mt76_mcu_send_msg(&dev->mt76, cmd, &req, sizeof(req), true);
}

static int mt7915_mcu_set_eeprom_flash(struct mt7915_dev *dev)
{
#define MAX_PAGE_IDX_MASK	GENMASK(7, 5)
#define PAGE_IDX_MASK		GENMASK(4, 2)
#define PER_PAGE_SIZE		0x400
	struct mt7915_mcu_eeprom req = { .buffer_mode = EE_MODE_BUFFER };
	u16 eeprom_size = mt7915_eeprom_size(dev);
	u8 total = DIV_ROUND_UP(eeprom_size, PER_PAGE_SIZE);
	u8 *eep = (u8 *)dev->mt76.eeprom.data;
	int eep_len;
	int i;

	for (i = 0; i < total; i++, eep += eep_len) {
		struct sk_buff *skb;
		int ret;

		if (i == total - 1 && !!(eeprom_size % PER_PAGE_SIZE))
			eep_len = eeprom_size % PER_PAGE_SIZE;
		else
			eep_len = PER_PAGE_SIZE;

		skb = mt76_mcu_msg_alloc(&dev->mt76, NULL,
					 sizeof(req) + eep_len);
		if (!skb)
			return -ENOMEM;

		req.format = FIELD_PREP(MAX_PAGE_IDX_MASK, total - 1) |
			     FIELD_PREP(PAGE_IDX_MASK, i) | EE_FORMAT_WHOLE;
		req.len = cpu_to_le16(eep_len);

		skb_put_data(skb, &req, sizeof(req));
		skb_put_data(skb, eep, eep_len);

		ret = mt76_mcu_skb_send_msg(&dev->mt76, skb,
					    MCU_EXT_CMD(EFUSE_BUFFER_MODE), true);
		if (ret)
			return ret;
	}

	return 0;
}

int mt7915_mcu_set_eeprom(struct mt7915_dev *dev)
{
	struct mt7915_mcu_eeprom req = {
		.buffer_mode = EE_MODE_EFUSE,
		.format = EE_FORMAT_WHOLE,
	};

	if (dev->flash_mode)
		return mt7915_mcu_set_eeprom_flash(dev);

	return mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD(EFUSE_BUFFER_MODE),
				 &req, sizeof(req), true);
}

int mt7915_mcu_get_eeprom(struct mt7915_dev *dev, u32 offset, u8 *read_buf)
{
	struct mt7915_mcu_eeprom_info req = {
		.addr = cpu_to_le32(round_down(offset,
				    MT7915_EEPROM_BLOCK_SIZE)),
	};
	struct mt7915_mcu_eeprom_info *res;
	struct sk_buff *skb;
	u8 *buf = read_buf;
	int ret;

	ret = mt76_mcu_send_and_get_msg(&dev->mt76,
					MCU_EXT_QUERY(EFUSE_ACCESS),
					&req, sizeof(req), true, &skb);
	if (ret)
		return ret;

	res = (struct mt7915_mcu_eeprom_info *)skb->data;
	if (!buf)
		buf = dev->mt76.eeprom.data + le32_to_cpu(res->addr);
	memcpy(buf, res->data, MT7915_EEPROM_BLOCK_SIZE);

	dev_kfree_skb(skb);

	return 0;
}

int mt7915_mcu_get_eeprom_free_block(struct mt7915_dev *dev, u8 *block_num)
{
	struct {
		u8 _rsv;
		u8 version;
		u8 die_idx;
		u8 _rsv2;
	} __packed req = {
		.version = 1,
	};
	struct sk_buff *skb;
	int ret;

	ret = mt76_mcu_send_and_get_msg(&dev->mt76,
					MCU_EXT_QUERY(EFUSE_FREE_BLOCK),
					&req, sizeof(req), true, &skb);
	if (ret)
		return ret;

	*block_num = *(u8 *)skb->data;
	dev_kfree_skb(skb);

	return 0;
}

static int mt7915_mcu_set_pre_cal(struct mt7915_dev *dev, u8 idx,
				  u8 *data, u32 len, int cmd)
{
	struct {
		u8 dir;
		u8 valid;
		__le16 bitmap;
		s8 precal;
		u8 action;
		u8 band;
		u8 idx;
		u8 rsv[4];
		__le32 len;
	} req = {};
	struct sk_buff *skb;

	skb = mt76_mcu_msg_alloc(&dev->mt76, NULL, sizeof(req) + len);
	if (!skb)
		return -ENOMEM;

	req.idx = idx;
	req.len = cpu_to_le32(len);
	skb_put_data(skb, &req, sizeof(req));
	skb_put_data(skb, data, len);

	return mt76_mcu_skb_send_msg(&dev->mt76, skb, cmd, false);
}

int mt7915_mcu_apply_group_cal(struct mt7915_dev *dev)
{
	u8 idx = 0, *cal = dev->cal, *eep = dev->mt76.eeprom.data;
	u32 total = mt7915_get_cal_group_size(dev);
	u32 offs = is_mt7915(&dev->mt76) ? MT_EE_DO_PRE_CAL : MT_EE_DO_PRE_CAL_V2;

	if (!(eep[offs] & MT_EE_WIFI_CAL_GROUP))
		return 0;

	/*
	 * Items: Rx DCOC, RSSI DCOC, Tx TSSI DCOC, Tx LPFG
	 * Tx FDIQ, Tx DCIQ, Rx FDIQ, Rx FIIQ, ADCDCOC
	 */
	while (total > 0) {
		int ret, len;

		len = min_t(u32, total, MT_EE_CAL_UNIT);

		ret = mt7915_mcu_set_pre_cal(dev, idx, cal, len,
					     MCU_EXT_CMD(GROUP_PRE_CAL_INFO));
		if (ret)
			return ret;

		total -= len;
		cal += len;
		idx++;
	}

	return 0;
}

static int mt7915_find_freq_idx(const u16 *freqs, int n_freqs, u16 cur)
{
	int i;

	for (i = 0; i < n_freqs; i++)
		if (cur == freqs[i])
			return i;

	return -1;
}

static int mt7915_dpd_freq_idx(struct mt7915_dev *dev, u16 freq, u8 bw)
{
	static const u16 freq_list_v1[] = {
		5180, 5200, 5220, 5240,
		5260, 5280, 5300, 5320,
		5500, 5520, 5540, 5560,
		5580, 5600, 5620, 5640,
		5660, 5680, 5700, 5745,
		5765, 5785, 5805, 5825
	};
	static const u16 freq_list_v2[] = {
		/* 6G BW20*/
		5955, 5975, 5995, 6015,
		6035, 6055, 6075, 6095,
		6115, 6135, 6155, 6175,
		6195, 6215, 6235, 6255,
		6275, 6295, 6315, 6335,
		6355, 6375, 6395, 6415,
		6435, 6455, 6475, 6495,
		6515, 6535, 6555, 6575,
		6595, 6615, 6635, 6655,
		6675, 6695, 6715, 6735,
		6755, 6775, 6795, 6815,
		6835, 6855, 6875, 6895,
		6915, 6935, 6955, 6975,
		6995, 7015, 7035, 7055,
		7075, 7095, 7115,
		/* 6G BW160 */
		6025, 6185, 6345, 6505,
		6665, 6825, 6985,
		/* 5G BW20 */
		5180, 5200, 5220, 5240,
		5260, 5280, 5300, 5320,
		5500, 5520, 5540, 5560,
		5580, 5600, 5620, 5640,
		5660, 5680, 5700, 5720,
		5745, 5765, 5785, 5805,
		5825, 5845, 5865, 5885,
		/* 5G BW160 */
		5250, 5570, 5815
	};
	static const u16 freq_list_v2_7981[] = {
		/* 5G BW20 */
		5180, 5200, 5220, 5240,
		5260, 5280, 5300, 5320,
		5500, 5520, 5540, 5560,
		5580, 5600, 5620, 5640,
		5660, 5680, 5700, 5720,
		5745, 5765, 5785, 5805,
		5825, 5845, 5865, 5885,
		/* 5G BW160 */
		5250, 5570, 5815
	};
	const u16 *freq_list = freq_list_v1;
	int n_freqs = ARRAY_SIZE(freq_list_v1);
	int idx;

	if (!is_mt7915(&dev->mt76)) {
		if (is_mt7981(&dev->mt76)) {
			freq_list = freq_list_v2_7981;
			n_freqs = ARRAY_SIZE(freq_list_v2_7981);
		} else {
			freq_list = freq_list_v2;
			n_freqs = ARRAY_SIZE(freq_list_v2);
		}
	}

	if (freq < 4000) {
		if (freq < 2432)
			return n_freqs;
		if (freq < 2457)
			return n_freqs + 1;

		return n_freqs + 2;
	}

	if (bw == NL80211_CHAN_WIDTH_80P80)
		return -1;

	if (bw != NL80211_CHAN_WIDTH_20) {
		idx = mt7915_find_freq_idx(freq_list, n_freqs, freq + 10);
		if (idx >= 0)
			return idx;

		idx = mt7915_find_freq_idx(freq_list, n_freqs, freq - 10);
		if (idx >= 0)
			return idx;
	}

	return mt7915_find_freq_idx(freq_list, n_freqs, freq);
}

int mt7915_mcu_apply_tx_dpd(struct mt7915_phy *phy)
{
	struct mt7915_dev *dev = phy->dev;
	struct cfg80211_chan_def *chandef = &phy->mt76->chandef;
	enum nl80211_band band = chandef->chan->band;
	u32 offs = is_mt7915(&dev->mt76) ? MT_EE_DO_PRE_CAL : MT_EE_DO_PRE_CAL_V2;
	u16 center_freq = chandef->center_freq1;
	u8 *cal = dev->cal, *eep = dev->mt76.eeprom.data;
	u8 dpd_mask, cal_num = is_mt7915(&dev->mt76) ? 2 : 3;
	int idx;

	switch (band) {
	case NL80211_BAND_2GHZ:
		dpd_mask = MT_EE_WIFI_CAL_DPD_2G;
		break;
	case NL80211_BAND_5GHZ:
		dpd_mask = MT_EE_WIFI_CAL_DPD_5G;
		break;
	case NL80211_BAND_6GHZ:
		dpd_mask = MT_EE_WIFI_CAL_DPD_6G;
		break;
	default:
		dpd_mask = 0;
		break;
	}

	if (!(eep[offs] & dpd_mask))
		return 0;

	idx = mt7915_dpd_freq_idx(dev, center_freq, chandef->width);
	if (idx < 0)
		return -EINVAL;

	/* Items: Tx DPD, Tx Flatness */
	idx = idx * cal_num;
	cal += mt7915_get_cal_group_size(dev) + (idx * MT_EE_CAL_UNIT);

	while (cal_num--) {
		int ret;

		ret = mt7915_mcu_set_pre_cal(dev, idx, cal, MT_EE_CAL_UNIT,
					     MCU_EXT_CMD(DPD_PRE_CAL_INFO));
		if (ret)
			return ret;

		idx++;
		cal += MT_EE_CAL_UNIT;
	}

	return 0;
}

int mt7915_mcu_get_chan_mib_info(struct mt7915_phy *phy, bool chan_switch)
{
	struct mt76_channel_state *state = phy->mt76->chan_state;
	struct mt76_channel_state *state_ts = &phy->state_ts;
	struct mt7915_dev *dev = phy->dev;
	struct mt7915_mcu_mib *res, req[5];
	struct sk_buff *skb;
	static const u32 *offs;
	int i, ret, len, offs_cc;
	u64 cc_tx;

	/* strict order */
	if (is_mt7915(&dev->mt76)) {
		static const u32 chip_offs[] = {
			MIB_NON_WIFI_TIME,
			MIB_TX_TIME,
			MIB_RX_TIME,
			MIB_OBSS_AIRTIME,
			MIB_TXOP_INIT_COUNT,
		};
		len = ARRAY_SIZE(chip_offs);
		offs = chip_offs;
		offs_cc = 20;
	} else {
		static const u32 chip_offs[] = {
			MIB_NON_WIFI_TIME_V2,
			MIB_TX_TIME_V2,
			MIB_RX_TIME_V2,
			MIB_OBSS_AIRTIME_V2
		};
		len = ARRAY_SIZE(chip_offs);
		offs = chip_offs;
		offs_cc = 0;
	}

	for (i = 0; i < len; i++) {
		req[i].band = cpu_to_le32(phy->mt76->band_idx);
		req[i].offs = cpu_to_le32(offs[i]);
	}

	ret = mt76_mcu_send_and_get_msg(&dev->mt76, MCU_EXT_CMD(GET_MIB_INFO),
					req, len * sizeof(req[0]), true, &skb);
	if (ret)
		return ret;

	res = (struct mt7915_mcu_mib *)(skb->data + offs_cc);

#define __res_u64(s) le64_to_cpu(res[s].data)
	/* subtract Tx backoff time from Tx duration for MT7915 */
	if (is_mt7915(&dev->mt76)) {
		u64 backoff = (__res_u64(4) & 0xffff) * 79;  /* 16us + 9us * 7 */
		cc_tx = __res_u64(1) - backoff;
	} else {
		cc_tx = __res_u64(1);
	}

	if (chan_switch)
		goto out;

	state->cc_tx += cc_tx - state_ts->cc_tx;
	state->cc_bss_rx += __res_u64(2) - state_ts->cc_bss_rx;
	state->cc_rx += __res_u64(2) + __res_u64(3) - state_ts->cc_rx;
	state->cc_busy += __res_u64(0) + cc_tx + __res_u64(2) + __res_u64(3) -
			  state_ts->cc_busy;

out:
	state_ts->cc_tx = cc_tx;
	state_ts->cc_bss_rx = __res_u64(2);
	state_ts->cc_rx = __res_u64(2) + __res_u64(3);
	state_ts->cc_busy = __res_u64(0) + cc_tx + __res_u64(2) + __res_u64(3);
#undef __res_u64

	dev_kfree_skb(skb);

	return 0;
}

int mt7915_mcu_get_temperature(struct mt7915_phy *phy)
{
	struct mt7915_dev *dev = phy->dev;
	struct {
		u8 ctrl_id;
		u8 action;
		u8 band_idx;
		u8 rsv[5];
	} req = {
		.ctrl_id = THERMAL_SENSOR_TEMP_QUERY,
		.band_idx = phy->mt76->band_idx,
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD(THERMAL_CTRL), &req,
				 sizeof(req), true);
}

int mt7915_mcu_set_thermal_throttling(struct mt7915_phy *phy, u8 state)
{
	struct mt7915_dev *dev = phy->dev;
	struct mt7915_mcu_thermal_ctrl req = {
		.band_idx = phy->mt76->band_idx,
		.ctrl_id = THERMAL_PROTECT_DUTY_CONFIG,
	};
	int level, ret;

	/* set duty cycle and level */
	for (level = 0; level < 4; level++) {
		req.duty.duty_level = level;
		req.duty.duty_cycle = state;
		state /= 2;

		ret = mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD(THERMAL_PROT),
					&req, sizeof(req), false);
		if (ret)
			return ret;
	}
	return 0;
}

int mt7915_mcu_set_thermal_protect(struct mt7915_phy *phy)
{
	struct mt7915_dev *dev = phy->dev;
	struct {
		struct mt7915_mcu_thermal_ctrl ctrl;

		__le32 trigger_temp;
		__le32 restore_temp;
		__le16 sustain_time;
		u8 rsv[2];
	} __packed req = {
		.ctrl = {
			.band_idx = phy->mt76->band_idx,
			.type.protect_type = 1,
			.type.trigger_type = 1,
		},
	};
	int ret;

	req.ctrl.ctrl_id = THERMAL_PROTECT_DISABLE;
	ret = mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD(THERMAL_PROT),
				&req, sizeof(req.ctrl), false);

	if (ret)
		return ret;

	/* set high-temperature trigger threshold */
	req.ctrl.ctrl_id = THERMAL_PROTECT_ENABLE;
	/* add a safety margin ~10 */
	req.restore_temp = cpu_to_le32(phy->throttle_temp[0] - 10);
	req.trigger_temp = cpu_to_le32(phy->throttle_temp[1]);
	req.sustain_time = cpu_to_le16(10);

	return mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD(THERMAL_PROT),
				 &req, sizeof(req), false);
}

int mt7915_mcu_set_txpower_frame_min(struct mt7915_phy *phy, s8 txpower)
{
	struct mt7915_dev *dev = phy->dev;
	struct {
		u8 format_id;
		u8 rsv;
		u8 band_idx;
		s8 txpower_min;
	} __packed req = {
		.format_id = TX_POWER_LIMIT_FRAME_MIN,
		.band_idx = phy->mt76->band_idx,
		.txpower_min = txpower * 2, /* 0.5db */
	};

	return mt76_mcu_send_msg(&dev->mt76,
				 MCU_EXT_CMD(TX_POWER_FEATURE_CTRL), &req,
				 sizeof(req), true);
}

int mt7915_mcu_set_txpower_frame(struct mt7915_phy *phy,
				 struct ieee80211_vif *vif,
				 struct ieee80211_sta *sta, s8 txpower)
{
	struct mt7915_sta *msta = (struct mt7915_sta *)sta->drv_priv;
	struct mt7915_dev *dev = phy->dev;
	struct mt76_phy *mphy = phy->mt76;
	struct {
		u8 format_id;
		u8 rsv[3];
		u8 band_idx;
		s8 txpower_max;
		__le16 wcid;
		s8 txpower_offs[48];
	} __packed req = {
		.format_id = TX_POWER_LIMIT_FRAME,
		.band_idx = phy->mt76->band_idx,
		.txpower_max = DIV_ROUND_UP(mphy->txpower_cur, 2),
		.wcid = cpu_to_le16(msta->wcid.idx),
	};
	int ret;
	s8 txpower_sku[MT7915_SKU_RATE_NUM];

	ret = mt7915_mcu_get_txpower_sku(phy, txpower_sku, sizeof(txpower_sku));
	if (ret)
		return ret;

	txpower = mt76_get_power_bound(mphy, txpower);
	if (txpower > mphy->txpower_cur || txpower < 0)
		return -EINVAL;

	if (txpower) {
		u32 offs, len, i;

		if (sta->deflink.ht_cap.ht_supported) {
			const u8 *sku_len = mt7915_sku_group_len;

			offs = sku_len[SKU_CCK] + sku_len[SKU_OFDM];
			len = sku_len[SKU_HT_BW20] + sku_len[SKU_HT_BW40];

			if (sta->deflink.vht_cap.vht_supported) {
				offs += len;
				len = sku_len[SKU_VHT_BW20] * 4;

				if (sta->deflink.he_cap.has_he) {
					offs += len + sku_len[SKU_HE_RU26] * 3;
					len = sku_len[SKU_HE_RU242] * 4;
				}
			}
		} else {
			return -EINVAL;
		}

		for (i = 0; i < len; i++, offs++)
			req.txpower_offs[i] =
				DIV_ROUND_UP(txpower - txpower_sku[offs], 2);
	}

	return mt76_mcu_send_msg(&dev->mt76,
				 MCU_EXT_CMD(TX_POWER_FEATURE_CTRL), &req,
				 sizeof(req), true);
}

int mt7915_mcu_set_txpower_sku(struct mt7915_phy *phy)
{
	struct mt7915_dev *dev = phy->dev;
	struct mt76_phy *mphy = phy->mt76;
	struct ieee80211_hw *hw = mphy->hw;
	struct mt7915_mcu_txpower_sku req = {
		.format_id = TX_POWER_LIMIT_TABLE,
		.band_idx = phy->mt76->band_idx,
	};
	struct mt76_power_limits limits_array;
	s8 *la = (s8 *)&limits_array;
	int i, idx;
	int tx_power;

	tx_power = mt76_get_power_bound(mphy, hw->conf.power_level);
	tx_power = mt76_get_rate_power_limits(mphy, mphy->chandef.chan,
					      &limits_array, tx_power);
	mphy->txpower_cur = tx_power;

	for (i = 0, idx = 0; i < ARRAY_SIZE(mt7915_sku_group_len); i++) {
		u8 mcs_num, len = mt7915_sku_group_len[i];
		int j;

		if (i >= SKU_HT_BW20 && i <= SKU_VHT_BW160) {
			mcs_num = 10;

			if (i == SKU_HT_BW20 || i == SKU_VHT_BW20)
				la = (s8 *)&limits_array + 12;
		} else {
			mcs_num = len;
		}

		for (j = 0; j < min_t(u8, mcs_num, len); j++)
			req.txpower_sku[idx + j] = la[j];

		la += mcs_num;
		idx += len;
	}

	return mt76_mcu_send_msg(&dev->mt76,
				 MCU_EXT_CMD(TX_POWER_FEATURE_CTRL), &req,
				 sizeof(req), true);
}

int mt7915_mcu_get_txpower_sku(struct mt7915_phy *phy, s8 *txpower, int len)
{
#define RATE_POWER_INFO	2
	struct mt7915_dev *dev = phy->dev;
	struct {
		u8 format_id;
		u8 category;
		u8 band_idx;
		u8 _rsv;
	} __packed req = {
		.format_id = TX_POWER_LIMIT_INFO,
		.category = RATE_POWER_INFO,
		.band_idx = phy->mt76->band_idx,
	};
	s8 txpower_sku[MT7915_SKU_RATE_NUM][2];
	struct sk_buff *skb;
	int ret, i;

	ret = mt76_mcu_send_and_get_msg(&dev->mt76,
					MCU_EXT_CMD(TX_POWER_FEATURE_CTRL),
					&req, sizeof(req), true, &skb);
	if (ret)
		return ret;

	memcpy(txpower_sku, skb->data + 4, sizeof(txpower_sku));
	for (i = 0; i < len; i++)
		txpower[i] = txpower_sku[i][req.band_idx];

	dev_kfree_skb(skb);

	return 0;
}

int mt7915_mcu_set_test_param(struct mt7915_dev *dev, u8 param, bool test_mode,
			      u8 en)
{
	struct {
		u8 test_mode_en;
		u8 param_idx;
		u8 _rsv[2];

		u8 enable;
		u8 _rsv2[3];

		u8 pad[8];
	} __packed req = {
		.test_mode_en = test_mode,
		.param_idx = param,
		.enable = en,
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD(ATE_CTRL), &req,
				 sizeof(req), false);
}

int mt7915_mcu_set_sku_en(struct mt7915_phy *phy, bool enable)
{
	struct mt7915_dev *dev = phy->dev;
	struct mt7915_sku {
		u8 format_id;
		u8 sku_enable;
		u8 band_idx;
		u8 rsv;
	} __packed req = {
		.format_id = TX_POWER_LIMIT_ENABLE,
		.band_idx = phy->mt76->band_idx,
		.sku_enable = enable,
	};

	return mt76_mcu_send_msg(&dev->mt76,
				 MCU_EXT_CMD(TX_POWER_FEATURE_CTRL), &req,
				 sizeof(req), true);
}

int mt7915_mcu_set_ser(struct mt7915_dev *dev, u8 action, u8 set, u8 band)
{
	struct {
		u8 action;
		u8 set;
		u8 band;
		u8 rsv;
	} req = {
		.action = action,
		.set = set,
		.band = band,
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD(SET_SER_TRIGGER),
				 &req, sizeof(req), false);
}

int mt7915_mcu_set_txbf(struct mt7915_dev *dev, u8 action)
{
	struct {
		u8 action;
		union {
			struct {
				u8 snd_mode;
				u8 sta_num;
				u8 rsv;
				u8 wlan_idx[4];
				__le32 snd_period;	/* ms */
			} __packed snd;
			struct {
				bool ebf;
				bool ibf;
				u8 rsv;
			} __packed type;
			struct {
				u8 bf_num;
				u8 bf_bitmap;
				u8 bf_sel[8];
				u8 rsv[5];
			} __packed mod;
		};
	} __packed req = {
		.action = action,
	};

#define MT_BF_PROCESSING	4
	switch (action) {
	case MT_BF_SOUNDING_ON:
		req.snd.snd_mode = MT_BF_PROCESSING;
		break;
	case MT_BF_TYPE_UPDATE:
		req.type.ebf = true;
		req.type.ibf = dev->ibf;
		break;
	case MT_BF_MODULE_UPDATE:
		req.mod.bf_num = 2;
		req.mod.bf_bitmap = GENMASK(1, 0);
		break;
	default:
		return -EINVAL;
	}

	return mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD(TXBF_ACTION), &req,
				 sizeof(req), true);
}

static int
mt7915_mcu_enable_obss_spr(struct mt7915_phy *phy, u8 action, u8 val)
{
	struct mt7915_dev *dev = phy->dev;
	struct mt7915_mcu_sr_ctrl req = {
		.action = action,
		.argnum = 1,
		.band_idx = phy->mt76->band_idx,
		.val = cpu_to_le32(val),
	};

	return mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD(SET_SPR), &req,
				 sizeof(req), true);
}

static int
mt7915_mcu_set_obss_spr_pd(struct mt7915_phy *phy,
			   struct ieee80211_he_obss_pd *he_obss_pd)
{
	struct mt7915_dev *dev = phy->dev;
	struct {
		struct mt7915_mcu_sr_ctrl ctrl;
		struct {
			u8 pd_th_non_srg;
			u8 pd_th_srg;
			u8 period_offs;
			u8 rcpi_src;
			__le16 obss_pd_min;
			__le16 obss_pd_min_srg;
			u8 resp_txpwr_mode;
			u8 txpwr_restrict_mode;
			u8 txpwr_ref;
			u8 rsv[3];
		} __packed param;
	} __packed req = {
		.ctrl = {
			.action = SPR_SET_PARAM,
			.argnum = 9,
			.band_idx = phy->mt76->band_idx,
		},
	};
	int ret;
	u8 max_th = 82, non_srg_max_th = 62;

	/* disable firmware dynamical PD asjustment */
	ret = mt7915_mcu_enable_obss_spr(phy, SPR_ENABLE_DPD, false);
	if (ret)
		return ret;

	if (he_obss_pd->sr_ctrl &
	    IEEE80211_HE_SPR_NON_SRG_OBSS_PD_SR_DISALLOWED)
		req.param.pd_th_non_srg = max_th;
	else if (he_obss_pd->sr_ctrl & IEEE80211_HE_SPR_NON_SRG_OFFSET_PRESENT)
		req.param.pd_th_non_srg  = max_th - he_obss_pd->non_srg_max_offset;
	else
		req.param.pd_th_non_srg  = non_srg_max_th;

	if (he_obss_pd->sr_ctrl & IEEE80211_HE_SPR_SRG_INFORMATION_PRESENT)
		req.param.pd_th_srg = max_th - he_obss_pd->max_offset;

	req.param.obss_pd_min = cpu_to_le16(82);
	req.param.obss_pd_min_srg = cpu_to_le16(82);
	req.param.txpwr_restrict_mode = 2;
	req.param.txpwr_ref = 21;

	return mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD(SET_SPR), &req,
				 sizeof(req), true);
}

static int
mt7915_mcu_set_obss_spr_siga(struct mt7915_phy *phy, struct ieee80211_vif *vif,
			     struct ieee80211_he_obss_pd *he_obss_pd)
{
	struct mt7915_vif *mvif = (struct mt7915_vif *)vif->drv_priv;
	struct mt7915_dev *dev = phy->dev;
	u8 omac = mvif->mt76.omac_idx;
	struct {
		struct mt7915_mcu_sr_ctrl ctrl;
		struct {
			u8 omac;
			u8 rsv[3];
			u8 flag[20];
		} __packed siga;
	} __packed req = {
		.ctrl = {
			.action = SPR_SET_SIGA,
			.argnum = 1,
			.band_idx = phy->mt76->band_idx,
		},
		.siga = {
			.omac = omac > HW_BSSID_MAX ? omac - 12 : omac,
		},
	};
	int ret;

	if (he_obss_pd->sr_ctrl & IEEE80211_HE_SPR_HESIGA_SR_VAL15_ALLOWED)
		req.siga.flag[req.siga.omac] = 0xf;
	else
		return 0;

	/* switch to normal AP mode */
	ret = mt7915_mcu_enable_obss_spr(phy, SPR_ENABLE_MODE, 0);
	if (ret)
		return ret;

	return mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD(SET_SPR), &req,
				 sizeof(req), true);
}

static int
mt7915_mcu_set_obss_spr_bitmap(struct mt7915_phy *phy,
			       struct ieee80211_he_obss_pd *he_obss_pd)
{
	struct mt7915_dev *dev = phy->dev;
	struct {
		struct mt7915_mcu_sr_ctrl ctrl;
		struct {
			__le32 color_l[2];
			__le32 color_h[2];
			__le32 bssid_l[2];
			__le32 bssid_h[2];
		} __packed bitmap;
	} __packed req = {
		.ctrl = {
			.action = SPR_SET_SRG_BITMAP,
			.argnum = 4,
			.band_idx = phy->mt76->band_idx,
		},
	};
	u32 bitmap;

	memcpy(&bitmap, he_obss_pd->bss_color_bitmap, sizeof(bitmap));
	req.bitmap.color_l[req.ctrl.band_idx] = cpu_to_le32(bitmap);

	memcpy(&bitmap, he_obss_pd->bss_color_bitmap + 4, sizeof(bitmap));
	req.bitmap.color_h[req.ctrl.band_idx] = cpu_to_le32(bitmap);

	memcpy(&bitmap, he_obss_pd->partial_bssid_bitmap, sizeof(bitmap));
	req.bitmap.bssid_l[req.ctrl.band_idx] = cpu_to_le32(bitmap);

	memcpy(&bitmap, he_obss_pd->partial_bssid_bitmap + 4, sizeof(bitmap));
	req.bitmap.bssid_h[req.ctrl.band_idx] = cpu_to_le32(bitmap);

	return mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD(SET_SPR), &req,
				 sizeof(req), true);
}

int mt7915_mcu_add_obss_spr(struct mt7915_phy *phy, struct ieee80211_vif *vif,
			    struct ieee80211_he_obss_pd *he_obss_pd)
{
	int ret;

	/* enable firmware scene detection algorithms */
	ret = mt7915_mcu_enable_obss_spr(phy, SPR_ENABLE_SD, sr_scene_detect);
	if (ret)
		return ret;

	/* firmware dynamically adjusts PD threshold so skip manual control */
	if (sr_scene_detect && !he_obss_pd->enable)
		return 0;

	/* enable spatial reuse */
	ret = mt7915_mcu_enable_obss_spr(phy, SPR_ENABLE, he_obss_pd->enable);
	if (ret)
		return ret;

	if (sr_scene_detect || !he_obss_pd->enable)
		return 0;

	ret = mt7915_mcu_enable_obss_spr(phy, SPR_ENABLE_TX, true);
	if (ret)
		return ret;

	/* set SRG/non-SRG OBSS PD threshold */
	ret = mt7915_mcu_set_obss_spr_pd(phy, he_obss_pd);
	if (ret)
		return ret;

	/* Set SR prohibit */
	ret = mt7915_mcu_set_obss_spr_siga(phy, vif, he_obss_pd);
	if (ret)
		return ret;

	/* set SRG BSS color/BSSID bitmap */
	return mt7915_mcu_set_obss_spr_bitmap(phy, he_obss_pd);
}

int mt7915_mcu_get_rx_rate(struct mt7915_phy *phy, struct ieee80211_vif *vif,
			   struct ieee80211_sta *sta, struct rate_info *rate)
{
	struct mt7915_vif *mvif = (struct mt7915_vif *)vif->drv_priv;
	struct mt7915_sta *msta = (struct mt7915_sta *)sta->drv_priv;
	struct mt7915_dev *dev = phy->dev;
	struct mt76_phy *mphy = phy->mt76;
	struct {
		u8 category;
		u8 band;
		__le16 wcid;
	} __packed req = {
		.category = MCU_PHY_STATE_CONTENTION_RX_RATE,
		.band = mvif->mt76.band_idx,
		.wcid = cpu_to_le16(msta->wcid.idx),
	};
	struct ieee80211_supported_band *sband;
	struct mt7915_mcu_phy_rx_info *res;
	struct sk_buff *skb;
	int ret;
	bool cck = false;

	ret = mt76_mcu_send_and_get_msg(&dev->mt76, MCU_EXT_CMD(PHY_STAT_INFO),
					&req, sizeof(req), true, &skb);
	if (ret)
		return ret;

	res = (struct mt7915_mcu_phy_rx_info *)skb->data;

	rate->mcs = res->rate;
	rate->nss = res->nsts + 1;

	switch (res->mode) {
	case MT_PHY_TYPE_CCK:
		cck = true;
		fallthrough;
	case MT_PHY_TYPE_OFDM:
		if (mphy->chandef.chan->band == NL80211_BAND_5GHZ)
			sband = &mphy->sband_5g.sband;
		else if (mphy->chandef.chan->band == NL80211_BAND_6GHZ)
			sband = &mphy->sband_6g.sband;
		else
			sband = &mphy->sband_2g.sband;

		rate->mcs = mt76_get_rate(&dev->mt76, sband, rate->mcs, cck);
		rate->legacy = sband->bitrates[rate->mcs].bitrate;
		break;
	case MT_PHY_TYPE_HT:
	case MT_PHY_TYPE_HT_GF:
		if (rate->mcs > 31) {
			ret = -EINVAL;
			goto out;
		}

		rate->flags = RATE_INFO_FLAGS_MCS;
		if (res->gi)
			rate->flags |= RATE_INFO_FLAGS_SHORT_GI;
		break;
	case MT_PHY_TYPE_VHT:
		if (rate->mcs > 9) {
			ret = -EINVAL;
			goto out;
		}

		rate->flags = RATE_INFO_FLAGS_VHT_MCS;
		if (res->gi)
			rate->flags |= RATE_INFO_FLAGS_SHORT_GI;
		break;
	case MT_PHY_TYPE_HE_SU:
	case MT_PHY_TYPE_HE_EXT_SU:
	case MT_PHY_TYPE_HE_TB:
	case MT_PHY_TYPE_HE_MU:
		if (res->gi > NL80211_RATE_INFO_HE_GI_3_2 || rate->mcs > 11) {
			ret = -EINVAL;
			goto out;
		}
		rate->he_gi = res->gi;
		rate->flags = RATE_INFO_FLAGS_HE_MCS;
		break;
	default:
		ret = -EINVAL;
		goto out;
	}

	switch (res->bw) {
	case IEEE80211_STA_RX_BW_160:
		rate->bw = RATE_INFO_BW_160;
		break;
	case IEEE80211_STA_RX_BW_80:
		rate->bw = RATE_INFO_BW_80;
		break;
	case IEEE80211_STA_RX_BW_40:
		rate->bw = RATE_INFO_BW_40;
		break;
	default:
		rate->bw = RATE_INFO_BW_20;
		break;
	}

out:
	dev_kfree_skb(skb);

	return ret;
}

int mt7915_mcu_update_bss_color(struct mt7915_dev *dev, struct ieee80211_vif *vif,
				struct cfg80211_he_bss_color *he_bss_color)
{
	int len = sizeof(struct sta_req_hdr) + sizeof(struct bss_info_color);
	struct mt7915_vif *mvif = (struct mt7915_vif *)vif->drv_priv;
	struct bss_info_color *bss_color;
	struct sk_buff *skb;
	struct tlv *tlv;

	skb = __mt76_connac_mcu_alloc_sta_req(&dev->mt76, &mvif->mt76,
					      NULL, len);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	tlv = mt76_connac_mcu_add_tlv(skb, BSS_INFO_BSS_COLOR,
				      sizeof(*bss_color));
	bss_color = (struct bss_info_color *)tlv;
	bss_color->disable = !he_bss_color->enabled;
	bss_color->color = he_bss_color->color;

	return mt76_mcu_skb_send_msg(&dev->mt76, skb,
				     MCU_EXT_CMD(BSS_INFO_UPDATE), true);
}

#define TWT_AGRT_TRIGGER	BIT(0)
#define TWT_AGRT_ANNOUNCE	BIT(1)
#define TWT_AGRT_PROTECT	BIT(2)

int mt7915_mcu_twt_agrt_update(struct mt7915_dev *dev,
			       struct mt7915_vif *mvif,
			       struct mt7915_twt_flow *flow,
			       int cmd)
{
	struct {
		u8 tbl_idx;
		u8 cmd;
		u8 own_mac_idx;
		u8 flowid; /* 0xff for group id */
		__le16 peer_id; /* specify the peer_id (msb=0)
				 * or group_id (msb=1)
				 */
		u8 duration; /* 256 us */
		u8 bss_idx;
		__le64 start_tsf;
		__le16 mantissa;
		u8 exponent;
		u8 is_ap;
		u8 agrt_params;
		u8 rsv[23];
	} __packed req = {
		.tbl_idx = flow->table_id,
		.cmd = cmd,
		.own_mac_idx = mvif->mt76.omac_idx,
		.flowid = flow->id,
		.peer_id = cpu_to_le16(flow->wcid),
		.duration = flow->duration,
		.bss_idx = mvif->mt76.idx,
		.start_tsf = cpu_to_le64(flow->tsf),
		.mantissa = flow->mantissa,
		.exponent = flow->exp,
		.is_ap = true,
	};

	if (flow->protection)
		req.agrt_params |= TWT_AGRT_PROTECT;
	if (!flow->flowtype)
		req.agrt_params |= TWT_AGRT_ANNOUNCE;
	if (flow->trigger)
		req.agrt_params |= TWT_AGRT_TRIGGER;

	return mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD(TWT_AGRT_UPDATE),
				 &req, sizeof(req), true);
}

int mt7915_mcu_wed_wa_tx_stats(struct mt7915_dev *dev, u16 wlan_idx)
{
	struct {
		__le32 cmd;
		__le32 arg0;
		__le32 arg1;
		__le16 arg2;
	} __packed req = {
		.cmd = cpu_to_le32(0x15),
	};
	struct mt7915_mcu_wa_tx_stat {
		__le16 wcid;
		u8 __rsv2[2];

		/* tx_bytes is deprecated since WA byte counter uses u32,
		 * which easily leads to overflow.
		 */
		__le32 tx_bytes;
		__le32 tx_packets;
	} __packed *res;
	struct mt76_wcid *wcid;
	struct sk_buff *skb;
	int ret, len;
	u16 ret_wcid;

	if (is_mt7915(&dev->mt76)) {
		req.arg0 = cpu_to_le32(wlan_idx);
		len = sizeof(req) - sizeof(req.arg2);
	} else {
		req.arg0 = cpu_to_le32(1);
		req.arg2 = cpu_to_le16(wlan_idx);
		len = sizeof(req);
	}

	ret = mt76_mcu_send_and_get_msg(&dev->mt76, MCU_WA_PARAM_CMD(QUERY),
					&req, len, true, &skb);
	if (ret)
		return ret;

	if (!is_mt7915(&dev->mt76))
		skb_pull(skb, 4);

	res = (struct mt7915_mcu_wa_tx_stat *)skb->data;

	ret_wcid = le16_to_cpu(res->wcid);
	if (is_mt7915(&dev->mt76))
		ret_wcid &= 0xff;

	if (ret_wcid != wlan_idx) {
		ret = -EINVAL;
		goto out;
	}

	rcu_read_lock();

	wcid = rcu_dereference(dev->mt76.wcid[wlan_idx]);
	if (wcid)
		wcid->stats.tx_packets += le32_to_cpu(res->tx_packets);
	else
		ret = -EINVAL;

	rcu_read_unlock();
out:
	dev_kfree_skb(skb);

	return ret;
}

int mt7915_mcu_rf_regval(struct mt7915_dev *dev, u32 regidx, u32 *val, bool set)
{
	struct {
		__le32 idx;
		__le32 ofs;
		__le32 data;
	} __packed req = {
		.idx = cpu_to_le32(u32_get_bits(regidx, GENMASK(31, 24))),
		.ofs = cpu_to_le32(u32_get_bits(regidx, GENMASK(23, 0))),
		.data = set ? cpu_to_le32(*val) : 0,
	};
	struct sk_buff *skb;
	int ret;

	if (set)
		return mt76_mcu_send_msg(&dev->mt76, MCU_EXT_CMD(RF_REG_ACCESS),
					 &req, sizeof(req), false);

	ret = mt76_mcu_send_and_get_msg(&dev->mt76, MCU_EXT_QUERY(RF_REG_ACCESS),
					&req, sizeof(req), true, &skb);
	if (ret)
		return ret;

	*val = le32_to_cpu(*(__le32 *)(skb->data + 8));
	dev_kfree_skb(skb);

	return 0;
}
