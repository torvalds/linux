// SPDX-License-Identifier: ISC
/* Copyright (C) 2019 MediaTek Inc.
 *
 * Author: Ryder Lee <ryder.lee@mediatek.com>
 *         Roy Luo <royluo@google.com>
 *         Felix Fietkau <nbd@nbd.name>
 *         Lorenzo Bianconi <lorenzo@kernel.org>
 */

#include <linux/devcoredump.h>
#include <linux/etherdevice.h>
#include <linux/timekeeping.h>
#include "mt7615.h"
#include "../trace.h"
#include "../dma.h"
#include "mt7615_trace.h"
#include "mac.h"
#include "mcu.h"

#define to_rssi(field, rxv)		((FIELD_GET(field, rxv) - 220) / 2)

static const struct mt7615_dfs_radar_spec etsi_radar_specs = {
	.pulse_th = { 110, -10, -80, 40, 5200, 128, 5200 },
	.radar_pattern = {
		[5] =  { 1, 0,  6, 32, 28, 0, 17,  990, 5010, 1, 1 },
		[6] =  { 1, 0,  9, 32, 28, 0, 27,  615, 5010, 1, 1 },
		[7] =  { 1, 0, 15, 32, 28, 0, 27,  240,  445, 1, 1 },
		[8] =  { 1, 0, 12, 32, 28, 0, 42,  240,  510, 1, 1 },
		[9] =  { 1, 1,  0,  0,  0, 0, 14, 2490, 3343, 0, 0, 12, 32, 28 },
		[10] = { 1, 1,  0,  0,  0, 0, 14, 2490, 3343, 0, 0, 15, 32, 24 },
		[11] = { 1, 1,  0,  0,  0, 0, 14,  823, 2510, 0, 0, 18, 32, 28 },
		[12] = { 1, 1,  0,  0,  0, 0, 14,  823, 2510, 0, 0, 27, 32, 24 },
	},
};

static const struct mt7615_dfs_radar_spec fcc_radar_specs = {
	.pulse_th = { 110, -10, -80, 40, 5200, 128, 5200 },
	.radar_pattern = {
		[0] = { 1, 0,  9,  32, 28, 0, 13, 508, 3076, 1,  1 },
		[1] = { 1, 0, 12,  32, 28, 0, 17, 140,  240, 1,  1 },
		[2] = { 1, 0,  8,  32, 28, 0, 22, 190,  510, 1,  1 },
		[3] = { 1, 0,  6,  32, 28, 0, 32, 190,  510, 1,  1 },
		[4] = { 1, 0,  9, 255, 28, 0, 13, 323,  343, 1, 32 },
	},
};

static const struct mt7615_dfs_radar_spec jp_radar_specs = {
	.pulse_th = { 110, -10, -80, 40, 5200, 128, 5200 },
	.radar_pattern = {
		[0] =  { 1, 0,  8, 32, 28, 0, 13,  508, 3076, 1,  1 },
		[1] =  { 1, 0, 12, 32, 28, 0, 17,  140,  240, 1,  1 },
		[2] =  { 1, 0,  8, 32, 28, 0, 22,  190,  510, 1,  1 },
		[3] =  { 1, 0,  6, 32, 28, 0, 32,  190,  510, 1,  1 },
		[4] =  { 1, 0,  9, 32, 28, 0, 13,  323,  343, 1, 32 },
		[13] = { 1, 0, 8,  32, 28, 0, 14, 3836, 3856, 1,  1 },
		[14] = { 1, 0, 8,  32, 28, 0, 14, 3990, 4010, 1,  1 },
	},
};

static enum mt76_cipher_type
mt7615_mac_get_cipher(int cipher)
{
	switch (cipher) {
	case WLAN_CIPHER_SUITE_WEP40:
		return MT_CIPHER_WEP40;
	case WLAN_CIPHER_SUITE_WEP104:
		return MT_CIPHER_WEP104;
	case WLAN_CIPHER_SUITE_TKIP:
		return MT_CIPHER_TKIP;
	case WLAN_CIPHER_SUITE_AES_CMAC:
		return MT_CIPHER_BIP_CMAC_128;
	case WLAN_CIPHER_SUITE_CCMP:
		return MT_CIPHER_AES_CCMP;
	case WLAN_CIPHER_SUITE_CCMP_256:
		return MT_CIPHER_CCMP_256;
	case WLAN_CIPHER_SUITE_GCMP:
		return MT_CIPHER_GCMP;
	case WLAN_CIPHER_SUITE_GCMP_256:
		return MT_CIPHER_GCMP_256;
	case WLAN_CIPHER_SUITE_SMS4:
		return MT_CIPHER_WAPI;
	default:
		return MT_CIPHER_NONE;
	}
}

static struct mt76_wcid *mt7615_rx_get_wcid(struct mt7615_dev *dev,
					    u8 idx, bool unicast)
{
	struct mt7615_sta *sta;
	struct mt76_wcid *wcid;

	if (idx >= MT7615_WTBL_SIZE)
		return NULL;

	wcid = rcu_dereference(dev->mt76.wcid[idx]);
	if (unicast || !wcid)
		return wcid;

	if (!wcid->sta)
		return NULL;

	sta = container_of(wcid, struct mt7615_sta, wcid);
	if (!sta->vif)
		return NULL;

	return &sta->vif->sta.wcid;
}

void mt7615_mac_reset_counters(struct mt7615_dev *dev)
{
	struct mt76_phy *mphy_ext = dev->mt76.phys[MT_BAND1];
	int i;

	for (i = 0; i < 4; i++) {
		mt76_rr(dev, MT_TX_AGG_CNT(0, i));
		mt76_rr(dev, MT_TX_AGG_CNT(1, i));
	}

	memset(dev->mt76.aggr_stats, 0, sizeof(dev->mt76.aggr_stats));
	dev->mt76.phy.survey_time = ktime_get_boottime();
	if (mphy_ext)
		mphy_ext->survey_time = ktime_get_boottime();

	/* reset airtime counters */
	mt76_rr(dev, MT_MIB_SDR9(0));
	mt76_rr(dev, MT_MIB_SDR9(1));

	mt76_rr(dev, MT_MIB_SDR36(0));
	mt76_rr(dev, MT_MIB_SDR36(1));

	mt76_rr(dev, MT_MIB_SDR37(0));
	mt76_rr(dev, MT_MIB_SDR37(1));

	mt76_set(dev, MT_WF_RMAC_MIB_TIME0, MT_WF_RMAC_MIB_RXTIME_CLR);
	mt76_set(dev, MT_WF_RMAC_MIB_AIRTIME0, MT_WF_RMAC_MIB_RXTIME_CLR);
}

void mt7615_mac_set_timing(struct mt7615_phy *phy)
{
	s16 coverage_class = phy->coverage_class;
	struct mt7615_dev *dev = phy->dev;
	bool ext_phy = phy != &dev->phy;
	u32 val, reg_offset;
	u32 cck = FIELD_PREP(MT_TIMEOUT_VAL_PLCP, 231) |
		  FIELD_PREP(MT_TIMEOUT_VAL_CCA, 48);
	u32 ofdm = FIELD_PREP(MT_TIMEOUT_VAL_PLCP, 60) |
		   FIELD_PREP(MT_TIMEOUT_VAL_CCA, 28);
	int sifs, offset;
	bool is_5ghz = phy->mt76->chandef.chan->band == NL80211_BAND_5GHZ;

	if (!test_bit(MT76_STATE_RUNNING, &phy->mt76->state))
		return;

	if (is_5ghz)
		sifs = 16;
	else
		sifs = 10;

	if (ext_phy) {
		coverage_class = max_t(s16, dev->phy.coverage_class,
				       coverage_class);
		mt76_set(dev, MT_ARB_SCR,
			 MT_ARB_SCR_TX1_DISABLE | MT_ARB_SCR_RX1_DISABLE);
	} else {
		struct mt7615_phy *phy_ext = mt7615_ext_phy(dev);

		if (phy_ext)
			coverage_class = max_t(s16, phy_ext->coverage_class,
					       coverage_class);
		mt76_set(dev, MT_ARB_SCR,
			 MT_ARB_SCR_TX0_DISABLE | MT_ARB_SCR_RX0_DISABLE);
	}
	udelay(1);

	offset = 3 * coverage_class;
	reg_offset = FIELD_PREP(MT_TIMEOUT_VAL_PLCP, offset) |
		     FIELD_PREP(MT_TIMEOUT_VAL_CCA, offset);
	mt76_wr(dev, MT_TMAC_CDTR, cck + reg_offset);
	mt76_wr(dev, MT_TMAC_ODTR, ofdm + reg_offset);

	mt76_wr(dev, MT_TMAC_ICR(ext_phy),
		FIELD_PREP(MT_IFS_EIFS, 360) |
		FIELD_PREP(MT_IFS_RIFS, 2) |
		FIELD_PREP(MT_IFS_SIFS, sifs) |
		FIELD_PREP(MT_IFS_SLOT, phy->slottime));

	if (phy->slottime < 20 || is_5ghz)
		val = MT7615_CFEND_RATE_DEFAULT;
	else
		val = MT7615_CFEND_RATE_11B;

	mt76_rmw_field(dev, MT_AGG_ACR(ext_phy), MT_AGG_ACR_CFEND_RATE, val);
	if (ext_phy)
		mt76_clear(dev, MT_ARB_SCR,
			   MT_ARB_SCR_TX1_DISABLE | MT_ARB_SCR_RX1_DISABLE);
	else
		mt76_clear(dev, MT_ARB_SCR,
			   MT_ARB_SCR_TX0_DISABLE | MT_ARB_SCR_RX0_DISABLE);

}

static void
mt7615_get_status_freq_info(struct mt7615_dev *dev, struct mt76_phy *mphy,
			    struct mt76_rx_status *status, u8 chfreq)
{
	if (!test_bit(MT76_HW_SCANNING, &mphy->state) &&
	    !test_bit(MT76_HW_SCHED_SCANNING, &mphy->state) &&
	    !test_bit(MT76_STATE_ROC, &mphy->state)) {
		status->freq = mphy->chandef.chan->center_freq;
		status->band = mphy->chandef.chan->band;
		return;
	}

	status->band = chfreq <= 14 ? NL80211_BAND_2GHZ : NL80211_BAND_5GHZ;
	status->freq = ieee80211_channel_to_frequency(chfreq, status->band);
}

static void mt7615_mac_fill_tm_rx(struct mt7615_phy *phy, __le32 *rxv)
{
#ifdef CONFIG_NL80211_TESTMODE
	u32 rxv1 = le32_to_cpu(rxv[0]);
	u32 rxv3 = le32_to_cpu(rxv[2]);
	u32 rxv4 = le32_to_cpu(rxv[3]);
	u32 rxv5 = le32_to_cpu(rxv[4]);
	u8 cbw = FIELD_GET(MT_RXV1_FRAME_MODE, rxv1);
	u8 mode = FIELD_GET(MT_RXV1_TX_MODE, rxv1);
	s16 foe = FIELD_GET(MT_RXV5_FOE, rxv5);
	u32 foe_const = (BIT(cbw + 1) & 0xf) * 10000;

	if (!mode) {
		/* CCK */
		foe &= ~BIT(11);
		foe *= 1000;
		foe >>= 11;
	} else {
		if (foe > 2048)
			foe -= 4096;

		foe = (foe * foe_const) >> 15;
	}

	phy->test.last_freq_offset = foe;
	phy->test.last_rcpi[0] = FIELD_GET(MT_RXV4_RCPI0, rxv4);
	phy->test.last_rcpi[1] = FIELD_GET(MT_RXV4_RCPI1, rxv4);
	phy->test.last_rcpi[2] = FIELD_GET(MT_RXV4_RCPI2, rxv4);
	phy->test.last_rcpi[3] = FIELD_GET(MT_RXV4_RCPI3, rxv4);
	phy->test.last_ib_rssi[0] = FIELD_GET(MT_RXV3_IB_RSSI, rxv3);
	phy->test.last_wb_rssi[0] = FIELD_GET(MT_RXV3_WB_RSSI, rxv3);
#endif
}

/* The HW does not translate the mac header to 802.3 for mesh point */
static int mt7615_reverse_frag0_hdr_trans(struct sk_buff *skb, u16 hdr_gap)
{
	struct mt76_rx_status *status = (struct mt76_rx_status *)skb->cb;
	struct ethhdr *eth_hdr = (struct ethhdr *)(skb->data + hdr_gap);
	struct mt7615_sta *msta = (struct mt7615_sta *)status->wcid;
	__le32 *rxd = (__le32 *)skb->data;
	struct ieee80211_sta *sta;
	struct ieee80211_vif *vif;
	struct ieee80211_hdr hdr;
	u16 frame_control;

	if (le32_get_bits(rxd[1], MT_RXD1_NORMAL_ADDR_TYPE) !=
	    MT_RXD1_NORMAL_U2M)
		return -EINVAL;

	if (!(le32_to_cpu(rxd[0]) & MT_RXD0_NORMAL_GROUP_4))
		return -EINVAL;

	if (!msta || !msta->vif)
		return -EINVAL;

	sta = container_of((void *)msta, struct ieee80211_sta, drv_priv);
	vif = container_of((void *)msta->vif, struct ieee80211_vif, drv_priv);

	/* store the info from RXD and ethhdr to avoid being overridden */
	frame_control = le32_get_bits(rxd[4], MT_RXD4_FRAME_CONTROL);
	hdr.frame_control = cpu_to_le16(frame_control);
	hdr.seq_ctrl = cpu_to_le16(le32_get_bits(rxd[6], MT_RXD6_SEQ_CTRL));
	hdr.duration_id = 0;

	ether_addr_copy(hdr.addr1, vif->addr);
	ether_addr_copy(hdr.addr2, sta->addr);
	switch (frame_control & (IEEE80211_FCTL_TODS |
				 IEEE80211_FCTL_FROMDS)) {
	case 0:
		ether_addr_copy(hdr.addr3, vif->bss_conf.bssid);
		break;
	case IEEE80211_FCTL_FROMDS:
		ether_addr_copy(hdr.addr3, eth_hdr->h_source);
		break;
	case IEEE80211_FCTL_TODS:
		ether_addr_copy(hdr.addr3, eth_hdr->h_dest);
		break;
	case IEEE80211_FCTL_TODS | IEEE80211_FCTL_FROMDS:
		ether_addr_copy(hdr.addr3, eth_hdr->h_dest);
		ether_addr_copy(hdr.addr4, eth_hdr->h_source);
		break;
	default:
		break;
	}

	skb_pull(skb, hdr_gap + sizeof(struct ethhdr) - 2);
	if (eth_hdr->h_proto == cpu_to_be16(ETH_P_AARP) ||
	    eth_hdr->h_proto == cpu_to_be16(ETH_P_IPX))
		ether_addr_copy(skb_push(skb, ETH_ALEN), bridge_tunnel_header);
	else if (be16_to_cpu(eth_hdr->h_proto) >= ETH_P_802_3_MIN)
		ether_addr_copy(skb_push(skb, ETH_ALEN), rfc1042_header);
	else
		skb_pull(skb, 2);

	if (ieee80211_has_order(hdr.frame_control))
		memcpy(skb_push(skb, IEEE80211_HT_CTL_LEN), &rxd[7],
		       IEEE80211_HT_CTL_LEN);

	if (ieee80211_is_data_qos(hdr.frame_control)) {
		__le16 qos_ctrl;

		qos_ctrl = cpu_to_le16(le32_get_bits(rxd[6], MT_RXD6_QOS_CTL));
		memcpy(skb_push(skb, IEEE80211_QOS_CTL_LEN), &qos_ctrl,
		       IEEE80211_QOS_CTL_LEN);
	}

	if (ieee80211_has_a4(hdr.frame_control))
		memcpy(skb_push(skb, sizeof(hdr)), &hdr, sizeof(hdr));
	else
		memcpy(skb_push(skb, sizeof(hdr) - 6), &hdr, sizeof(hdr) - 6);

	status->flag &= ~(RX_FLAG_RADIOTAP_HE | RX_FLAG_RADIOTAP_HE_MU);
	return 0;
}

static int mt7615_mac_fill_rx(struct mt7615_dev *dev, struct sk_buff *skb)
{
	struct mt76_rx_status *status = (struct mt76_rx_status *)skb->cb;
	struct mt76_phy *mphy = &dev->mt76.phy;
	struct mt7615_phy *phy = &dev->phy;
	struct ieee80211_supported_band *sband;
	struct ieee80211_hdr *hdr;
	struct mt7615_phy *phy2;
	__le32 *rxd = (__le32 *)skb->data;
	u32 rxd0 = le32_to_cpu(rxd[0]);
	u32 rxd1 = le32_to_cpu(rxd[1]);
	u32 rxd2 = le32_to_cpu(rxd[2]);
	u32 csum_mask = MT_RXD0_NORMAL_IP_SUM | MT_RXD0_NORMAL_UDP_TCP_SUM;
	u32 csum_status = *(u32 *)skb->cb;
	bool unicast, hdr_trans, remove_pad, insert_ccmp_hdr = false;
	u16 hdr_gap;
	int phy_idx;
	int i, idx;
	u8 chfreq, amsdu_info, qos_ctl = 0;
	u16 seq_ctrl = 0;
	__le16 fc = 0;

	memset(status, 0, sizeof(*status));

	chfreq = FIELD_GET(MT_RXD1_NORMAL_CH_FREQ, rxd1);

	phy2 = dev->mt76.phys[MT_BAND1] ? dev->mt76.phys[MT_BAND1]->priv : NULL;
	if (!phy2)
		phy_idx = 0;
	else if (phy2->chfreq == phy->chfreq)
		phy_idx = -1;
	else if (phy->chfreq == chfreq)
		phy_idx = 0;
	else if (phy2->chfreq == chfreq)
		phy_idx = 1;
	else
		phy_idx = -1;

	if (rxd2 & MT_RXD2_NORMAL_AMSDU_ERR)
		return -EINVAL;

	hdr_trans = rxd1 & MT_RXD1_NORMAL_HDR_TRANS;
	if (hdr_trans && (rxd2 & MT_RXD2_NORMAL_CM))
		return -EINVAL;

	/* ICV error or CCMP/BIP/WPI MIC error */
	if (rxd2 & MT_RXD2_NORMAL_ICV_ERR)
		status->flag |= RX_FLAG_ONLY_MONITOR;

	unicast = (rxd1 & MT_RXD1_NORMAL_ADDR_TYPE) == MT_RXD1_NORMAL_U2M;
	idx = FIELD_GET(MT_RXD2_NORMAL_WLAN_IDX, rxd2);
	status->wcid = mt7615_rx_get_wcid(dev, idx, unicast);

	if (status->wcid) {
		struct mt7615_sta *msta;

		msta = container_of(status->wcid, struct mt7615_sta, wcid);
		spin_lock_bh(&dev->sta_poll_lock);
		if (list_empty(&msta->poll_list))
			list_add_tail(&msta->poll_list, &dev->sta_poll_list);
		spin_unlock_bh(&dev->sta_poll_lock);
	}

	if (mt76_is_mmio(&dev->mt76) && (rxd0 & csum_mask) == csum_mask &&
	    !(csum_status & (BIT(0) | BIT(2) | BIT(3))))
		skb->ip_summed = CHECKSUM_UNNECESSARY;

	if (rxd2 & MT_RXD2_NORMAL_FCS_ERR)
		status->flag |= RX_FLAG_FAILED_FCS_CRC;

	if (rxd2 & MT_RXD2_NORMAL_TKIP_MIC_ERR)
		status->flag |= RX_FLAG_MMIC_ERROR;

	if (FIELD_GET(MT_RXD2_NORMAL_SEC_MODE, rxd2) != 0 &&
	    !(rxd2 & (MT_RXD2_NORMAL_CLM | MT_RXD2_NORMAL_CM))) {
		status->flag |= RX_FLAG_DECRYPTED;
		status->flag |= RX_FLAG_IV_STRIPPED;
		status->flag |= RX_FLAG_MMIC_STRIPPED | RX_FLAG_MIC_STRIPPED;
	}

	remove_pad = rxd1 & MT_RXD1_NORMAL_HDR_OFFSET;

	if (rxd2 & MT_RXD2_NORMAL_MAX_LEN_ERROR)
		return -EINVAL;

	rxd += 4;
	if (rxd0 & MT_RXD0_NORMAL_GROUP_4) {
		u32 v0 = le32_to_cpu(rxd[0]);
		u32 v2 = le32_to_cpu(rxd[2]);

		fc = cpu_to_le16(FIELD_GET(MT_RXD4_FRAME_CONTROL, v0));
		qos_ctl = FIELD_GET(MT_RXD6_QOS_CTL, v2);
		seq_ctrl = FIELD_GET(MT_RXD6_SEQ_CTRL, v2);

		rxd += 4;
		if ((u8 *)rxd - skb->data >= skb->len)
			return -EINVAL;
	}

	if (rxd0 & MT_RXD0_NORMAL_GROUP_1) {
		u8 *data = (u8 *)rxd;

		if (status->flag & RX_FLAG_DECRYPTED) {
			switch (FIELD_GET(MT_RXD2_NORMAL_SEC_MODE, rxd2)) {
			case MT_CIPHER_AES_CCMP:
			case MT_CIPHER_CCMP_CCX:
			case MT_CIPHER_CCMP_256:
				insert_ccmp_hdr =
					FIELD_GET(MT_RXD2_NORMAL_FRAG, rxd2);
				fallthrough;
			case MT_CIPHER_TKIP:
			case MT_CIPHER_TKIP_NO_MIC:
			case MT_CIPHER_GCMP:
			case MT_CIPHER_GCMP_256:
				status->iv[0] = data[5];
				status->iv[1] = data[4];
				status->iv[2] = data[3];
				status->iv[3] = data[2];
				status->iv[4] = data[1];
				status->iv[5] = data[0];
				break;
			default:
				break;
			}
		}
		rxd += 4;
		if ((u8 *)rxd - skb->data >= skb->len)
			return -EINVAL;
	}

	if (rxd0 & MT_RXD0_NORMAL_GROUP_2) {
		status->timestamp = le32_to_cpu(rxd[0]);
		status->flag |= RX_FLAG_MACTIME_START;

		if (!(rxd2 & (MT_RXD2_NORMAL_NON_AMPDU_SUB |
			      MT_RXD2_NORMAL_NON_AMPDU))) {
			status->flag |= RX_FLAG_AMPDU_DETAILS;

			/* all subframes of an A-MPDU have the same timestamp */
			if (phy->rx_ampdu_ts != status->timestamp) {
				if (!++phy->ampdu_ref)
					phy->ampdu_ref++;
			}
			phy->rx_ampdu_ts = status->timestamp;

			status->ampdu_ref = phy->ampdu_ref;
		}

		rxd += 2;
		if ((u8 *)rxd - skb->data >= skb->len)
			return -EINVAL;
	}

	if (rxd0 & MT_RXD0_NORMAL_GROUP_3) {
		u32 rxdg5 = le32_to_cpu(rxd[5]);

		/*
		 * If both PHYs are on the same channel and we don't have a WCID,
		 * we need to figure out which PHY this packet was received on.
		 * On the primary PHY, the noise value for the chains belonging to the
		 * second PHY will be set to the noise value of the last packet from
		 * that PHY.
		 */
		if (phy_idx < 0) {
			int first_chain = ffs(phy2->mt76->chainmask) - 1;

			phy_idx = ((rxdg5 >> (first_chain * 8)) & 0xff) == 0;
		}
	}

	if (phy_idx == 1 && phy2) {
		mphy = dev->mt76.phys[MT_BAND1];
		phy = phy2;
		status->phy_idx = phy_idx;
	}

	if (!mt7615_firmware_offload(dev) && chfreq != phy->chfreq)
		return -EINVAL;

	mt7615_get_status_freq_info(dev, mphy, status, chfreq);
	if (status->band == NL80211_BAND_5GHZ)
		sband = &mphy->sband_5g.sband;
	else
		sband = &mphy->sband_2g.sband;

	if (!test_bit(MT76_STATE_RUNNING, &mphy->state))
		return -EINVAL;

	if (!sband->channels)
		return -EINVAL;

	if (rxd0 & MT_RXD0_NORMAL_GROUP_3) {
		u32 rxdg0 = le32_to_cpu(rxd[0]);
		u32 rxdg1 = le32_to_cpu(rxd[1]);
		u32 rxdg3 = le32_to_cpu(rxd[3]);
		u8 stbc = FIELD_GET(MT_RXV1_HT_STBC, rxdg0);
		bool cck = false;

		i = FIELD_GET(MT_RXV1_TX_RATE, rxdg0);
		switch (FIELD_GET(MT_RXV1_TX_MODE, rxdg0)) {
		case MT_PHY_TYPE_CCK:
			cck = true;
			fallthrough;
		case MT_PHY_TYPE_OFDM:
			i = mt76_get_rate(&dev->mt76, sband, i, cck);
			break;
		case MT_PHY_TYPE_HT_GF:
		case MT_PHY_TYPE_HT:
			status->encoding = RX_ENC_HT;
			if (i > 31)
				return -EINVAL;
			break;
		case MT_PHY_TYPE_VHT:
			status->nss = FIELD_GET(MT_RXV2_NSTS, rxdg1) + 1;
			status->encoding = RX_ENC_VHT;
			break;
		default:
			return -EINVAL;
		}
		status->rate_idx = i;

		switch (FIELD_GET(MT_RXV1_FRAME_MODE, rxdg0)) {
		case MT_PHY_BW_20:
			break;
		case MT_PHY_BW_40:
			status->bw = RATE_INFO_BW_40;
			break;
		case MT_PHY_BW_80:
			status->bw = RATE_INFO_BW_80;
			break;
		case MT_PHY_BW_160:
			status->bw = RATE_INFO_BW_160;
			break;
		default:
			return -EINVAL;
		}

		if (rxdg0 & MT_RXV1_HT_SHORT_GI)
			status->enc_flags |= RX_ENC_FLAG_SHORT_GI;
		if (rxdg0 & MT_RXV1_HT_AD_CODE)
			status->enc_flags |= RX_ENC_FLAG_LDPC;

		status->enc_flags |= RX_ENC_FLAG_STBC_MASK * stbc;

		status->chains = mphy->antenna_mask;
		status->chain_signal[0] = to_rssi(MT_RXV4_RCPI0, rxdg3);
		status->chain_signal[1] = to_rssi(MT_RXV4_RCPI1, rxdg3);
		status->chain_signal[2] = to_rssi(MT_RXV4_RCPI2, rxdg3);
		status->chain_signal[3] = to_rssi(MT_RXV4_RCPI3, rxdg3);

		mt7615_mac_fill_tm_rx(mphy->priv, rxd);

		rxd += 6;
		if ((u8 *)rxd - skb->data >= skb->len)
			return -EINVAL;
	}

	amsdu_info = FIELD_GET(MT_RXD1_NORMAL_PAYLOAD_FORMAT, rxd1);
	status->amsdu = !!amsdu_info;
	if (status->amsdu) {
		status->first_amsdu = amsdu_info == MT_RXD1_FIRST_AMSDU_FRAME;
		status->last_amsdu = amsdu_info == MT_RXD1_LAST_AMSDU_FRAME;
	}

	hdr_gap = (u8 *)rxd - skb->data + 2 * remove_pad;
	if (hdr_trans && ieee80211_has_morefrags(fc)) {
		if (mt7615_reverse_frag0_hdr_trans(skb, hdr_gap))
			return -EINVAL;
		hdr_trans = false;
	} else {
		int pad_start = 0;

		skb_pull(skb, hdr_gap);
		if (!hdr_trans && status->amsdu) {
			pad_start = ieee80211_get_hdrlen_from_skb(skb);
		} else if (hdr_trans && (rxd2 & MT_RXD2_NORMAL_HDR_TRANS_ERROR)) {
			/*
			 * When header translation failure is indicated,
			 * the hardware will insert an extra 2-byte field
			 * containing the data length after the protocol
			 * type field. This happens either when the LLC-SNAP
			 * pattern did not match, or if a VLAN header was
			 * detected.
			 */
			pad_start = 12;
			if (get_unaligned_be16(skb->data + pad_start) == ETH_P_8021Q)
				pad_start += 4;
			else
				pad_start = 0;
		}

		if (pad_start) {
			memmove(skb->data + 2, skb->data, pad_start);
			skb_pull(skb, 2);
		}
	}

	if (insert_ccmp_hdr && !hdr_trans) {
		u8 key_id = FIELD_GET(MT_RXD1_NORMAL_KEY_ID, rxd1);

		mt76_insert_ccmp_hdr(skb, key_id);
	}

	if (!hdr_trans) {
		hdr = (struct ieee80211_hdr *)skb->data;
		fc = hdr->frame_control;
		if (ieee80211_is_data_qos(fc)) {
			seq_ctrl = le16_to_cpu(hdr->seq_ctrl);
			qos_ctl = *ieee80211_get_qos_ctl(hdr);
		}
	} else {
		status->flag |= RX_FLAG_8023;
	}

	if (!status->wcid || !ieee80211_is_data_qos(fc))
		return 0;

	status->aggr = unicast &&
		       !ieee80211_is_qos_nullfunc(fc);
	status->qos_ctl = qos_ctl;
	status->seqno = IEEE80211_SEQ_TO_SN(seq_ctrl);

	return 0;
}

void mt7615_sta_ps(struct mt76_dev *mdev, struct ieee80211_sta *sta, bool ps)
{
}
EXPORT_SYMBOL_GPL(mt7615_sta_ps);

static u16
mt7615_mac_tx_rate_val(struct mt7615_dev *dev,
		       struct mt76_phy *mphy,
		       const struct ieee80211_tx_rate *rate,
		       bool stbc, u8 *bw)
{
	u8 phy, nss, rate_idx;
	u16 rateval = 0;

	*bw = 0;

	if (rate->flags & IEEE80211_TX_RC_VHT_MCS) {
		rate_idx = ieee80211_rate_get_vht_mcs(rate);
		nss = ieee80211_rate_get_vht_nss(rate);
		phy = MT_PHY_TYPE_VHT;
		if (rate->flags & IEEE80211_TX_RC_40_MHZ_WIDTH)
			*bw = 1;
		else if (rate->flags & IEEE80211_TX_RC_80_MHZ_WIDTH)
			*bw = 2;
		else if (rate->flags & IEEE80211_TX_RC_160_MHZ_WIDTH)
			*bw = 3;
	} else if (rate->flags & IEEE80211_TX_RC_MCS) {
		rate_idx = rate->idx;
		nss = 1 + (rate->idx >> 3);
		phy = MT_PHY_TYPE_HT;
		if (rate->flags & IEEE80211_TX_RC_GREEN_FIELD)
			phy = MT_PHY_TYPE_HT_GF;
		if (rate->flags & IEEE80211_TX_RC_40_MHZ_WIDTH)
			*bw = 1;
	} else {
		const struct ieee80211_rate *r;
		int band = mphy->chandef.chan->band;
		u16 val;

		nss = 1;
		r = &mphy->hw->wiphy->bands[band]->bitrates[rate->idx];
		if (rate->flags & IEEE80211_TX_RC_USE_SHORT_PREAMBLE)
			val = r->hw_value_short;
		else
			val = r->hw_value;

		phy = val >> 8;
		rate_idx = val & 0xff;
	}

	if (stbc && nss == 1) {
		nss++;
		rateval |= MT_TX_RATE_STBC;
	}

	rateval |= (FIELD_PREP(MT_TX_RATE_IDX, rate_idx) |
		    FIELD_PREP(MT_TX_RATE_MODE, phy) |
		    FIELD_PREP(MT_TX_RATE_NSS, nss - 1));

	return rateval;
}

int mt7615_mac_write_txwi(struct mt7615_dev *dev, __le32 *txwi,
			  struct sk_buff *skb, struct mt76_wcid *wcid,
			  struct ieee80211_sta *sta, int pid,
			  struct ieee80211_key_conf *key,
			  enum mt76_txq_id qid, bool beacon)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	u8 fc_type, fc_stype, p_fmt, q_idx, omac_idx = 0, wmm_idx = 0;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_tx_rate *rate = &info->control.rates[0];
	u8 phy_idx = (info->hw_queue & MT_TX_HW_QUEUE_PHY) >> 2;
	bool multicast = is_multicast_ether_addr(hdr->addr1);
	struct ieee80211_vif *vif = info->control.vif;
	bool is_mmio = mt76_is_mmio(&dev->mt76);
	u32 val, sz_txd = is_mmio ? MT_TXD_SIZE : MT_USB_TXD_SIZE;
	struct mt76_phy *mphy = &dev->mphy;
	__le16 fc = hdr->frame_control;
	int tx_count = 8;
	u16 seqno = 0;

	if (vif) {
		struct mt76_vif *mvif = (struct mt76_vif *)vif->drv_priv;

		omac_idx = mvif->omac_idx;
		wmm_idx = mvif->wmm_idx;
	}

	if (sta) {
		struct mt7615_sta *msta = (struct mt7615_sta *)sta->drv_priv;

		tx_count = msta->rate_count;
	}

	if (phy_idx && dev->mt76.phys[MT_BAND1])
		mphy = dev->mt76.phys[MT_BAND1];

	fc_type = (le16_to_cpu(fc) & IEEE80211_FCTL_FTYPE) >> 2;
	fc_stype = (le16_to_cpu(fc) & IEEE80211_FCTL_STYPE) >> 4;

	if (beacon) {
		p_fmt = MT_TX_TYPE_FW;
		q_idx = phy_idx ? MT_LMAC_BCN1 : MT_LMAC_BCN0;
	} else if (qid >= MT_TXQ_PSD) {
		p_fmt = is_mmio ? MT_TX_TYPE_CT : MT_TX_TYPE_SF;
		q_idx = phy_idx ? MT_LMAC_ALTX1 : MT_LMAC_ALTX0;
	} else {
		p_fmt = is_mmio ? MT_TX_TYPE_CT : MT_TX_TYPE_SF;
		q_idx = wmm_idx * MT7615_MAX_WMM_SETS +
			mt7615_lmac_mapping(dev, skb_get_queue_mapping(skb));
	}

	val = FIELD_PREP(MT_TXD0_TX_BYTES, skb->len + sz_txd) |
	      FIELD_PREP(MT_TXD0_P_IDX, MT_TX_PORT_IDX_LMAC) |
	      FIELD_PREP(MT_TXD0_Q_IDX, q_idx);
	txwi[0] = cpu_to_le32(val);

	val = MT_TXD1_LONG_FORMAT |
	      FIELD_PREP(MT_TXD1_WLAN_IDX, wcid->idx) |
	      FIELD_PREP(MT_TXD1_HDR_FORMAT, MT_HDR_FORMAT_802_11) |
	      FIELD_PREP(MT_TXD1_HDR_INFO,
			 ieee80211_get_hdrlen_from_skb(skb) / 2) |
	      FIELD_PREP(MT_TXD1_TID,
			 skb->priority & IEEE80211_QOS_CTL_TID_MASK) |
	      FIELD_PREP(MT_TXD1_PKT_FMT, p_fmt) |
	      FIELD_PREP(MT_TXD1_OWN_MAC, omac_idx);
	txwi[1] = cpu_to_le32(val);

	val = FIELD_PREP(MT_TXD2_FRAME_TYPE, fc_type) |
	      FIELD_PREP(MT_TXD2_SUB_TYPE, fc_stype) |
	      FIELD_PREP(MT_TXD2_MULTICAST, multicast);
	if (key) {
		if (multicast && ieee80211_is_robust_mgmt_frame(skb) &&
		    key->cipher == WLAN_CIPHER_SUITE_AES_CMAC) {
			val |= MT_TXD2_BIP;
			txwi[3] = 0;
		} else {
			txwi[3] = cpu_to_le32(MT_TXD3_PROTECT_FRAME);
		}
	} else {
		txwi[3] = 0;
	}
	txwi[2] = cpu_to_le32(val);

	if (!(info->flags & IEEE80211_TX_CTL_AMPDU))
		txwi[2] |= cpu_to_le32(MT_TXD2_BA_DISABLE);

	txwi[4] = 0;
	txwi[6] = 0;

	if (rate->idx >= 0 && rate->count &&
	    !(info->flags & IEEE80211_TX_CTL_RATE_CTRL_PROBE)) {
		bool stbc = info->flags & IEEE80211_TX_CTL_STBC;
		u8 bw;
		u16 rateval = mt7615_mac_tx_rate_val(dev, mphy, rate, stbc,
						     &bw);

		txwi[2] |= cpu_to_le32(MT_TXD2_FIX_RATE);

		val = MT_TXD6_FIXED_BW |
		      FIELD_PREP(MT_TXD6_BW, bw) |
		      FIELD_PREP(MT_TXD6_TX_RATE, rateval);
		txwi[6] |= cpu_to_le32(val);

		if (rate->flags & IEEE80211_TX_RC_SHORT_GI)
			txwi[6] |= cpu_to_le32(MT_TXD6_SGI);

		if (info->flags & IEEE80211_TX_CTL_LDPC)
			txwi[6] |= cpu_to_le32(MT_TXD6_LDPC);

		if (!(rate->flags & (IEEE80211_TX_RC_MCS |
				     IEEE80211_TX_RC_VHT_MCS)))
			txwi[2] |= cpu_to_le32(MT_TXD2_BA_DISABLE);

		tx_count = rate->count;
	}

	if (!ieee80211_is_beacon(fc)) {
		struct ieee80211_hw *hw = mt76_hw(dev);

		val = MT_TXD5_TX_STATUS_HOST | FIELD_PREP(MT_TXD5_PID, pid);
		if (!ieee80211_hw_check(hw, SUPPORTS_PS))
			val |= MT_TXD5_SW_POWER_MGMT;
		txwi[5] = cpu_to_le32(val);
	} else {
		txwi[5] = 0;
		/* use maximum tx count for beacons */
		tx_count = 0x1f;
	}

	val = FIELD_PREP(MT_TXD3_REM_TX_COUNT, tx_count);
	if (info->flags & IEEE80211_TX_CTL_INJECTED) {
		seqno = le16_to_cpu(hdr->seq_ctrl);

		if (ieee80211_is_back_req(hdr->frame_control)) {
			struct ieee80211_bar *bar;

			bar = (struct ieee80211_bar *)skb->data;
			seqno = le16_to_cpu(bar->start_seq_num);
		}

		val |= MT_TXD3_SN_VALID |
		       FIELD_PREP(MT_TXD3_SEQ, IEEE80211_SEQ_TO_SN(seqno));
	}

	txwi[3] |= cpu_to_le32(val);

	if (info->flags & IEEE80211_TX_CTL_NO_ACK)
		txwi[3] |= cpu_to_le32(MT_TXD3_NO_ACK);

	val = FIELD_PREP(MT_TXD7_TYPE, fc_type) |
	      FIELD_PREP(MT_TXD7_SUB_TYPE, fc_stype) |
	      FIELD_PREP(MT_TXD7_SPE_IDX, 0x18);
	txwi[7] = cpu_to_le32(val);
	if (!is_mmio) {
		val = FIELD_PREP(MT_TXD8_L_TYPE, fc_type) |
		      FIELD_PREP(MT_TXD8_L_SUB_TYPE, fc_stype);
		txwi[8] = cpu_to_le32(val);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(mt7615_mac_write_txwi);

bool mt7615_mac_wtbl_update(struct mt7615_dev *dev, int idx, u32 mask)
{
	mt76_rmw(dev, MT_WTBL_UPDATE, MT_WTBL_UPDATE_WLAN_IDX,
		 FIELD_PREP(MT_WTBL_UPDATE_WLAN_IDX, idx) | mask);

	return mt76_poll(dev, MT_WTBL_UPDATE, MT_WTBL_UPDATE_BUSY,
			 0, 5000);
}

void mt7615_mac_sta_poll(struct mt7615_dev *dev)
{
	static const u8 ac_to_tid[4] = {
		[IEEE80211_AC_BE] = 0,
		[IEEE80211_AC_BK] = 1,
		[IEEE80211_AC_VI] = 4,
		[IEEE80211_AC_VO] = 6
	};
	static const u8 hw_queue_map[] = {
		[IEEE80211_AC_BK] = 0,
		[IEEE80211_AC_BE] = 1,
		[IEEE80211_AC_VI] = 2,
		[IEEE80211_AC_VO] = 3,
	};
	struct ieee80211_sta *sta;
	struct mt7615_sta *msta;
	u32 addr, tx_time[4], rx_time[4];
	struct list_head sta_poll_list;
	int i;

	INIT_LIST_HEAD(&sta_poll_list);
	spin_lock_bh(&dev->sta_poll_lock);
	list_splice_init(&dev->sta_poll_list, &sta_poll_list);
	spin_unlock_bh(&dev->sta_poll_lock);

	while (!list_empty(&sta_poll_list)) {
		bool clear = false;

		msta = list_first_entry(&sta_poll_list, struct mt7615_sta,
					poll_list);
		list_del_init(&msta->poll_list);

		addr = mt7615_mac_wtbl_addr(dev, msta->wcid.idx) + 19 * 4;

		for (i = 0; i < 4; i++, addr += 8) {
			u32 tx_last = msta->airtime_ac[i];
			u32 rx_last = msta->airtime_ac[i + 4];

			msta->airtime_ac[i] = mt76_rr(dev, addr);
			msta->airtime_ac[i + 4] = mt76_rr(dev, addr + 4);
			tx_time[i] = msta->airtime_ac[i] - tx_last;
			rx_time[i] = msta->airtime_ac[i + 4] - rx_last;

			if ((tx_last | rx_last) & BIT(30))
				clear = true;
		}

		if (clear) {
			mt7615_mac_wtbl_update(dev, msta->wcid.idx,
					       MT_WTBL_UPDATE_ADM_COUNT_CLEAR);
			memset(msta->airtime_ac, 0, sizeof(msta->airtime_ac));
		}

		if (!msta->wcid.sta)
			continue;

		sta = container_of((void *)msta, struct ieee80211_sta,
				   drv_priv);
		for (i = 0; i < 4; i++) {
			u32 tx_cur = tx_time[i];
			u32 rx_cur = rx_time[hw_queue_map[i]];
			u8 tid = ac_to_tid[i];

			if (!tx_cur && !rx_cur)
				continue;

			ieee80211_sta_register_airtime(sta, tid, tx_cur,
						       rx_cur);
		}
	}
}
EXPORT_SYMBOL_GPL(mt7615_mac_sta_poll);

static void
mt7615_mac_update_rate_desc(struct mt7615_phy *phy, struct mt7615_sta *sta,
			    struct ieee80211_tx_rate *probe_rate,
			    struct ieee80211_tx_rate *rates,
			    struct mt7615_rate_desc *rd)
{
	struct mt7615_dev *dev = phy->dev;
	struct mt76_phy *mphy = phy->mt76;
	struct ieee80211_tx_rate *ref;
	bool rateset, stbc = false;
	int n_rates = sta->n_rates;
	u8 bw, bw_prev;
	int i, j;

	for (i = n_rates; i < 4; i++)
		rates[i] = rates[n_rates - 1];

	rateset = !(sta->rate_set_tsf & BIT(0));
	memcpy(sta->rateset[rateset].rates, rates,
	       sizeof(sta->rateset[rateset].rates));
	if (probe_rate) {
		sta->rateset[rateset].probe_rate = *probe_rate;
		ref = &sta->rateset[rateset].probe_rate;
	} else {
		sta->rateset[rateset].probe_rate.idx = -1;
		ref = &sta->rateset[rateset].rates[0];
	}

	rates = sta->rateset[rateset].rates;
	for (i = 0; i < ARRAY_SIZE(sta->rateset[rateset].rates); i++) {
		/*
		 * We don't support switching between short and long GI
		 * within the rate set. For accurate tx status reporting, we
		 * need to make sure that flags match.
		 * For improved performance, avoid duplicate entries by
		 * decrementing the MCS index if necessary
		 */
		if ((ref->flags ^ rates[i].flags) & IEEE80211_TX_RC_SHORT_GI)
			rates[i].flags ^= IEEE80211_TX_RC_SHORT_GI;

		for (j = 0; j < i; j++) {
			if (rates[i].idx != rates[j].idx)
				continue;
			if ((rates[i].flags ^ rates[j].flags) &
			    (IEEE80211_TX_RC_40_MHZ_WIDTH |
			     IEEE80211_TX_RC_80_MHZ_WIDTH |
			     IEEE80211_TX_RC_160_MHZ_WIDTH))
				continue;

			if (!rates[i].idx)
				continue;

			rates[i].idx--;
		}
	}

	rd->val[0] = mt7615_mac_tx_rate_val(dev, mphy, &rates[0], stbc, &bw);
	bw_prev = bw;

	if (probe_rate) {
		rd->probe_val = mt7615_mac_tx_rate_val(dev, mphy, probe_rate,
						       stbc, &bw);
		if (bw)
			rd->bw_idx = 1;
		else
			bw_prev = 0;
	} else {
		rd->probe_val = rd->val[0];
	}

	rd->val[1] = mt7615_mac_tx_rate_val(dev, mphy, &rates[1], stbc, &bw);
	if (bw_prev) {
		rd->bw_idx = 3;
		bw_prev = bw;
	}

	rd->val[2] = mt7615_mac_tx_rate_val(dev, mphy, &rates[2], stbc, &bw);
	if (bw_prev) {
		rd->bw_idx = 5;
		bw_prev = bw;
	}

	rd->val[3] = mt7615_mac_tx_rate_val(dev, mphy, &rates[3], stbc, &bw);
	if (bw_prev)
		rd->bw_idx = 7;

	rd->rateset = rateset;
	rd->bw = bw;
}

static int
mt7615_mac_queue_rate_update(struct mt7615_phy *phy, struct mt7615_sta *sta,
			     struct ieee80211_tx_rate *probe_rate,
			     struct ieee80211_tx_rate *rates)
{
	struct mt7615_dev *dev = phy->dev;
	struct mt7615_wtbl_rate_desc *wrd;

	if (work_pending(&dev->rate_work))
		return -EBUSY;

	wrd = kzalloc(sizeof(*wrd), GFP_ATOMIC);
	if (!wrd)
		return -ENOMEM;

	wrd->sta = sta;
	mt7615_mac_update_rate_desc(phy, sta, probe_rate, rates,
				    &wrd->rate);
	list_add_tail(&wrd->node, &dev->wrd_head);
	queue_work(dev->mt76.wq, &dev->rate_work);

	return 0;
}

u32 mt7615_mac_get_sta_tid_sn(struct mt7615_dev *dev, int wcid, u8 tid)
{
	u32 addr, val, val2;
	u8 offset;

	addr = mt7615_mac_wtbl_addr(dev, wcid) + 11 * 4;

	offset = tid * 12;
	addr += 4 * (offset / 32);
	offset %= 32;

	val = mt76_rr(dev, addr);
	val >>= offset;

	if (offset > 20) {
		addr += 4;
		val2 = mt76_rr(dev, addr);
		val |= val2 << (32 - offset);
	}

	return val & GENMASK(11, 0);
}

void mt7615_mac_set_rates(struct mt7615_phy *phy, struct mt7615_sta *sta,
			  struct ieee80211_tx_rate *probe_rate,
			  struct ieee80211_tx_rate *rates)
{
	int wcid = sta->wcid.idx, n_rates = sta->n_rates;
	struct mt7615_dev *dev = phy->dev;
	struct mt7615_rate_desc rd;
	u32 w5, w27, addr;
	u16 idx = sta->vif->mt76.omac_idx;

	if (!mt76_is_mmio(&dev->mt76)) {
		mt7615_mac_queue_rate_update(phy, sta, probe_rate, rates);
		return;
	}

	if (!mt76_poll(dev, MT_WTBL_UPDATE, MT_WTBL_UPDATE_BUSY, 0, 5000))
		return;

	memset(&rd, 0, sizeof(struct mt7615_rate_desc));
	mt7615_mac_update_rate_desc(phy, sta, probe_rate, rates, &rd);

	addr = mt7615_mac_wtbl_addr(dev, wcid);
	w27 = mt76_rr(dev, addr + 27 * 4);
	w27 &= ~MT_WTBL_W27_CC_BW_SEL;
	w27 |= FIELD_PREP(MT_WTBL_W27_CC_BW_SEL, rd.bw);

	w5 = mt76_rr(dev, addr + 5 * 4);
	w5 &= ~(MT_WTBL_W5_BW_CAP | MT_WTBL_W5_CHANGE_BW_RATE |
		MT_WTBL_W5_MPDU_OK_COUNT |
		MT_WTBL_W5_MPDU_FAIL_COUNT |
		MT_WTBL_W5_RATE_IDX);
	w5 |= FIELD_PREP(MT_WTBL_W5_BW_CAP, rd.bw) |
	      FIELD_PREP(MT_WTBL_W5_CHANGE_BW_RATE,
			 rd.bw_idx ? rd.bw_idx - 1 : 7);

	mt76_wr(dev, MT_WTBL_RIUCR0, w5);

	mt76_wr(dev, MT_WTBL_RIUCR1,
		FIELD_PREP(MT_WTBL_RIUCR1_RATE0, rd.probe_val) |
		FIELD_PREP(MT_WTBL_RIUCR1_RATE1, rd.val[0]) |
		FIELD_PREP(MT_WTBL_RIUCR1_RATE2_LO, rd.val[1]));

	mt76_wr(dev, MT_WTBL_RIUCR2,
		FIELD_PREP(MT_WTBL_RIUCR2_RATE2_HI, rd.val[1] >> 8) |
		FIELD_PREP(MT_WTBL_RIUCR2_RATE3, rd.val[1]) |
		FIELD_PREP(MT_WTBL_RIUCR2_RATE4, rd.val[2]) |
		FIELD_PREP(MT_WTBL_RIUCR2_RATE5_LO, rd.val[2]));

	mt76_wr(dev, MT_WTBL_RIUCR3,
		FIELD_PREP(MT_WTBL_RIUCR3_RATE5_HI, rd.val[2] >> 4) |
		FIELD_PREP(MT_WTBL_RIUCR3_RATE6, rd.val[3]) |
		FIELD_PREP(MT_WTBL_RIUCR3_RATE7, rd.val[3]));

	mt76_wr(dev, MT_WTBL_UPDATE,
		FIELD_PREP(MT_WTBL_UPDATE_WLAN_IDX, wcid) |
		MT_WTBL_UPDATE_RATE_UPDATE |
		MT_WTBL_UPDATE_TX_COUNT_CLEAR);

	mt76_wr(dev, addr + 27 * 4, w27);

	idx = idx > HW_BSSID_MAX ? HW_BSSID_0 : idx;
	addr = idx > 1 ? MT_LPON_TCR2(idx): MT_LPON_TCR0(idx);

	mt76_rmw(dev, addr, MT_LPON_TCR_MODE, MT_LPON_TCR_READ); /* TSF read */
	sta->rate_set_tsf = mt76_rr(dev, MT_LPON_UTTR0) & ~BIT(0);
	sta->rate_set_tsf |= rd.rateset;

	if (!(sta->wcid.tx_info & MT_WCID_TX_INFO_SET))
		mt76_poll(dev, MT_WTBL_UPDATE, MT_WTBL_UPDATE_BUSY, 0, 5000);

	sta->rate_count = 2 * MT7615_RATE_RETRY * n_rates;
	sta->wcid.tx_info |= MT_WCID_TX_INFO_SET;
	sta->rate_probe = !!probe_rate;
}
EXPORT_SYMBOL_GPL(mt7615_mac_set_rates);

static int
mt7615_mac_wtbl_update_key(struct mt7615_dev *dev, struct mt76_wcid *wcid,
			   struct ieee80211_key_conf *key,
			   enum mt76_cipher_type cipher, u16 cipher_mask)
{
	u32 addr = mt7615_mac_wtbl_addr(dev, wcid->idx) + 30 * 4;
	u8 data[32] = {};

	if (key->keylen > sizeof(data))
		return -EINVAL;

	mt76_rr_copy(dev, addr, data, sizeof(data));
	if (cipher == MT_CIPHER_TKIP) {
		/* Rx/Tx MIC keys are swapped */
		memcpy(data, key->key, 16);
		memcpy(data + 16, key->key + 24, 8);
		memcpy(data + 24, key->key + 16, 8);
	} else {
		if (cipher_mask == BIT(cipher))
			memcpy(data, key->key, key->keylen);
		else if (cipher != MT_CIPHER_BIP_CMAC_128)
			memcpy(data, key->key, 16);
		if (cipher == MT_CIPHER_BIP_CMAC_128)
			memcpy(data + 16, key->key, 16);
	}

	mt76_wr_copy(dev, addr, data, sizeof(data));

	return 0;
}

static int
mt7615_mac_wtbl_update_pk(struct mt7615_dev *dev, struct mt76_wcid *wcid,
			  enum mt76_cipher_type cipher, u16 cipher_mask,
			  int keyidx)
{
	u32 addr = mt7615_mac_wtbl_addr(dev, wcid->idx), w0, w1;

	if (!mt76_poll(dev, MT_WTBL_UPDATE, MT_WTBL_UPDATE_BUSY, 0, 5000))
		return -ETIMEDOUT;

	w0 = mt76_rr(dev, addr);
	w1 = mt76_rr(dev, addr + 4);

	if (cipher_mask)
		w0 |= MT_WTBL_W0_RX_KEY_VALID;
	else
		w0 &= ~(MT_WTBL_W0_RX_KEY_VALID | MT_WTBL_W0_KEY_IDX);
	if (cipher_mask & BIT(MT_CIPHER_BIP_CMAC_128))
		w0 |= MT_WTBL_W0_RX_IK_VALID;
	else
		w0 &= ~MT_WTBL_W0_RX_IK_VALID;

	if (cipher != MT_CIPHER_BIP_CMAC_128 || cipher_mask == BIT(cipher)) {
		w0 &= ~MT_WTBL_W0_KEY_IDX;
		w0 |= FIELD_PREP(MT_WTBL_W0_KEY_IDX, keyidx);
	}

	mt76_wr(dev, MT_WTBL_RICR0, w0);
	mt76_wr(dev, MT_WTBL_RICR1, w1);

	if (!mt7615_mac_wtbl_update(dev, wcid->idx,
				    MT_WTBL_UPDATE_RXINFO_UPDATE))
		return -ETIMEDOUT;

	return 0;
}

static void
mt7615_mac_wtbl_update_cipher(struct mt7615_dev *dev, struct mt76_wcid *wcid,
			      enum mt76_cipher_type cipher, u16 cipher_mask)
{
	u32 addr = mt7615_mac_wtbl_addr(dev, wcid->idx);

	if (cipher == MT_CIPHER_BIP_CMAC_128 &&
	    cipher_mask & ~BIT(MT_CIPHER_BIP_CMAC_128))
		return;

	mt76_rmw(dev, addr + 2 * 4, MT_WTBL_W2_KEY_TYPE,
		 FIELD_PREP(MT_WTBL_W2_KEY_TYPE, cipher));
}

int __mt7615_mac_wtbl_set_key(struct mt7615_dev *dev,
			      struct mt76_wcid *wcid,
			      struct ieee80211_key_conf *key)
{
	enum mt76_cipher_type cipher;
	u16 cipher_mask = wcid->cipher;
	int err;

	cipher = mt7615_mac_get_cipher(key->cipher);
	if (cipher == MT_CIPHER_NONE)
		return -EOPNOTSUPP;

	cipher_mask |= BIT(cipher);
	mt7615_mac_wtbl_update_cipher(dev, wcid, cipher, cipher_mask);
	err = mt7615_mac_wtbl_update_key(dev, wcid, key, cipher, cipher_mask);
	if (err < 0)
		return err;

	err = mt7615_mac_wtbl_update_pk(dev, wcid, cipher, cipher_mask,
					key->keyidx);
	if (err < 0)
		return err;

	wcid->cipher = cipher_mask;

	return 0;
}

int mt7615_mac_wtbl_set_key(struct mt7615_dev *dev,
			    struct mt76_wcid *wcid,
			    struct ieee80211_key_conf *key)
{
	int err;

	spin_lock_bh(&dev->mt76.lock);
	err = __mt7615_mac_wtbl_set_key(dev, wcid, key);
	spin_unlock_bh(&dev->mt76.lock);

	return err;
}

static bool mt7615_fill_txs(struct mt7615_dev *dev, struct mt7615_sta *sta,
			    struct ieee80211_tx_info *info, __le32 *txs_data)
{
	struct ieee80211_supported_band *sband;
	struct mt7615_rate_set *rs;
	struct mt76_phy *mphy;
	int first_idx = 0, last_idx;
	int i, idx, count;
	bool fixed_rate, ack_timeout;
	bool ampdu, cck = false;
	bool rs_idx;
	u32 rate_set_tsf;
	u32 final_rate, final_rate_flags, final_nss, txs;

	txs = le32_to_cpu(txs_data[1]);
	ampdu = txs & MT_TXS1_AMPDU;

	txs = le32_to_cpu(txs_data[3]);
	count = FIELD_GET(MT_TXS3_TX_COUNT, txs);
	last_idx = FIELD_GET(MT_TXS3_LAST_TX_RATE, txs);

	txs = le32_to_cpu(txs_data[0]);
	fixed_rate = txs & MT_TXS0_FIXED_RATE;
	final_rate = FIELD_GET(MT_TXS0_TX_RATE, txs);
	ack_timeout = txs & MT_TXS0_ACK_TIMEOUT;

	if (!ampdu && (txs & MT_TXS0_RTS_TIMEOUT))
		return false;

	if (txs & MT_TXS0_QUEUE_TIMEOUT)
		return false;

	if (!ack_timeout)
		info->flags |= IEEE80211_TX_STAT_ACK;

	info->status.ampdu_len = 1;
	info->status.ampdu_ack_len = !!(info->flags &
					IEEE80211_TX_STAT_ACK);

	if (ampdu || (info->flags & IEEE80211_TX_CTL_AMPDU))
		info->flags |= IEEE80211_TX_STAT_AMPDU | IEEE80211_TX_CTL_AMPDU;

	first_idx = max_t(int, 0, last_idx - (count - 1) / MT7615_RATE_RETRY);

	if (fixed_rate) {
		info->status.rates[0].count = count;
		i = 0;
		goto out;
	}

	rate_set_tsf = READ_ONCE(sta->rate_set_tsf);
	rs_idx = !((u32)(le32_get_bits(txs_data[4], MT_TXS4_F0_TIMESTAMP) -
			 rate_set_tsf) < 1000000);
	rs_idx ^= rate_set_tsf & BIT(0);
	rs = &sta->rateset[rs_idx];

	if (!first_idx && rs->probe_rate.idx >= 0) {
		info->status.rates[0] = rs->probe_rate;

		spin_lock_bh(&dev->mt76.lock);
		if (sta->rate_probe) {
			struct mt7615_phy *phy = &dev->phy;

			if (sta->wcid.phy_idx && dev->mt76.phys[MT_BAND1])
				phy = dev->mt76.phys[MT_BAND1]->priv;

			mt7615_mac_set_rates(phy, sta, NULL, sta->rates);
		}
		spin_unlock_bh(&dev->mt76.lock);
	} else {
		info->status.rates[0] = rs->rates[first_idx / 2];
	}
	info->status.rates[0].count = 0;

	for (i = 0, idx = first_idx; count && idx <= last_idx; idx++) {
		struct ieee80211_tx_rate *cur_rate;
		int cur_count;

		cur_rate = &rs->rates[idx / 2];
		cur_count = min_t(int, MT7615_RATE_RETRY, count);
		count -= cur_count;

		if (idx && (cur_rate->idx != info->status.rates[i].idx ||
			    cur_rate->flags != info->status.rates[i].flags)) {
			i++;
			if (i == ARRAY_SIZE(info->status.rates)) {
				i--;
				break;
			}

			info->status.rates[i] = *cur_rate;
			info->status.rates[i].count = 0;
		}

		info->status.rates[i].count += cur_count;
	}

out:
	final_rate_flags = info->status.rates[i].flags;

	switch (FIELD_GET(MT_TX_RATE_MODE, final_rate)) {
	case MT_PHY_TYPE_CCK:
		cck = true;
		fallthrough;
	case MT_PHY_TYPE_OFDM:
		mphy = &dev->mphy;
		if (sta->wcid.phy_idx && dev->mt76.phys[MT_BAND1])
			mphy = dev->mt76.phys[MT_BAND1];

		if (mphy->chandef.chan->band == NL80211_BAND_5GHZ)
			sband = &mphy->sband_5g.sband;
		else
			sband = &mphy->sband_2g.sband;
		final_rate &= MT_TX_RATE_IDX;
		final_rate = mt76_get_rate(&dev->mt76, sband, final_rate,
					   cck);
		final_rate_flags = 0;
		break;
	case MT_PHY_TYPE_HT_GF:
	case MT_PHY_TYPE_HT:
		final_rate_flags |= IEEE80211_TX_RC_MCS;
		final_rate &= MT_TX_RATE_IDX;
		if (final_rate > 31)
			return false;
		break;
	case MT_PHY_TYPE_VHT:
		final_nss = FIELD_GET(MT_TX_RATE_NSS, final_rate);

		if ((final_rate & MT_TX_RATE_STBC) && final_nss)
			final_nss--;

		final_rate_flags |= IEEE80211_TX_RC_VHT_MCS;
		final_rate = (final_rate & MT_TX_RATE_IDX) | (final_nss << 4);
		break;
	default:
		return false;
	}

	info->status.rates[i].idx = final_rate;
	info->status.rates[i].flags = final_rate_flags;

	return true;
}

static bool mt7615_mac_add_txs_skb(struct mt7615_dev *dev,
				   struct mt7615_sta *sta, int pid,
				   __le32 *txs_data)
{
	struct mt76_dev *mdev = &dev->mt76;
	struct sk_buff_head list;
	struct sk_buff *skb;

	if (pid < MT_PACKET_ID_FIRST)
		return false;

	trace_mac_txdone(mdev, sta->wcid.idx, pid);

	mt76_tx_status_lock(mdev, &list);
	skb = mt76_tx_status_skb_get(mdev, &sta->wcid, pid, &list);
	if (skb) {
		struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);

		if (!mt7615_fill_txs(dev, sta, info, txs_data)) {
			info->status.rates[0].count = 0;
			info->status.rates[0].idx = -1;
		}

		mt76_tx_status_skb_done(mdev, skb, &list);
	}
	mt76_tx_status_unlock(mdev, &list);

	return !!skb;
}

static void mt7615_mac_add_txs(struct mt7615_dev *dev, void *data)
{
	struct ieee80211_tx_info info = {};
	struct ieee80211_sta *sta = NULL;
	struct mt7615_sta *msta = NULL;
	struct mt76_wcid *wcid;
	struct mt76_phy *mphy = &dev->mt76.phy;
	__le32 *txs_data = data;
	u8 wcidx;
	u8 pid;

	pid = le32_get_bits(txs_data[0], MT_TXS0_PID);
	wcidx = le32_get_bits(txs_data[2], MT_TXS2_WCID);

	if (pid == MT_PACKET_ID_NO_ACK)
		return;

	if (wcidx >= MT7615_WTBL_SIZE)
		return;

	rcu_read_lock();

	wcid = rcu_dereference(dev->mt76.wcid[wcidx]);
	if (!wcid)
		goto out;

	msta = container_of(wcid, struct mt7615_sta, wcid);
	sta = wcid_to_sta(wcid);

	spin_lock_bh(&dev->sta_poll_lock);
	if (list_empty(&msta->poll_list))
		list_add_tail(&msta->poll_list, &dev->sta_poll_list);
	spin_unlock_bh(&dev->sta_poll_lock);

	if (mt7615_mac_add_txs_skb(dev, msta, pid, txs_data))
		goto out;

	if (wcidx >= MT7615_WTBL_STA || !sta)
		goto out;

	if (wcid->phy_idx && dev->mt76.phys[MT_BAND1])
		mphy = dev->mt76.phys[MT_BAND1];

	if (mt7615_fill_txs(dev, msta, &info, txs_data))
		ieee80211_tx_status_noskb(mphy->hw, sta, &info);

out:
	rcu_read_unlock();
}

static void
mt7615_txwi_free(struct mt7615_dev *dev, struct mt76_txwi_cache *txwi)
{
	struct mt76_dev *mdev = &dev->mt76;
	__le32 *txwi_data;
	u32 val;
	u8 wcid;

	mt76_connac_txp_skb_unmap(mdev, txwi);
	if (!txwi->skb)
		goto out;

	txwi_data = (__le32 *)mt76_get_txwi_ptr(mdev, txwi);
	val = le32_to_cpu(txwi_data[1]);
	wcid = FIELD_GET(MT_TXD1_WLAN_IDX, val);
	mt76_tx_complete_skb(mdev, wcid, txwi->skb);

out:
	txwi->skb = NULL;
	mt76_put_txwi(mdev, txwi);
}

static void
mt7615_mac_tx_free_token(struct mt7615_dev *dev, u16 token)
{
	struct mt76_dev *mdev = &dev->mt76;
	struct mt76_txwi_cache *txwi;

	trace_mac_tx_free(dev, token);
	txwi = mt76_token_put(mdev, token);
	if (!txwi)
		return;

	mt7615_txwi_free(dev, txwi);
}

static void mt7615_mac_tx_free(struct mt7615_dev *dev, void *data, int len)
{
	struct mt76_connac_tx_free *free = data;
	void *tx_token = data + sizeof(*free);
	void *end = data + len;
	u8 i, count;

	mt76_queue_tx_cleanup(dev, dev->mphy.q_tx[MT_TXQ_PSD], false);
	if (is_mt7615(&dev->mt76)) {
		mt76_queue_tx_cleanup(dev, dev->mphy.q_tx[MT_TXQ_BE], false);
	} else {
		for (i = 0; i < IEEE80211_NUM_ACS; i++)
			mt76_queue_tx_cleanup(dev, dev->mphy.q_tx[i], false);
	}

	count = le16_get_bits(free->ctrl, MT_TX_FREE_MSDU_ID_CNT);
	if (is_mt7615(&dev->mt76)) {
		__le16 *token = tx_token;

		if (WARN_ON_ONCE((void *)&token[count] > end))
			return;

		for (i = 0; i < count; i++)
			mt7615_mac_tx_free_token(dev, le16_to_cpu(token[i]));
	} else {
		__le32 *token = tx_token;

		if (WARN_ON_ONCE((void *)&token[count] > end))
			return;

		for (i = 0; i < count; i++)
			mt7615_mac_tx_free_token(dev, le32_to_cpu(token[i]));
	}

	rcu_read_lock();
	mt7615_mac_sta_poll(dev);
	rcu_read_unlock();

	mt76_worker_schedule(&dev->mt76.tx_worker);
}

bool mt7615_rx_check(struct mt76_dev *mdev, void *data, int len)
{
	struct mt7615_dev *dev = container_of(mdev, struct mt7615_dev, mt76);
	__le32 *rxd = (__le32 *)data;
	__le32 *end = (__le32 *)&rxd[len / 4];
	enum rx_pkt_type type;

	type = le32_get_bits(rxd[0], MT_RXD0_PKT_TYPE);

	switch (type) {
	case PKT_TYPE_TXRX_NOTIFY:
		mt7615_mac_tx_free(dev, data, len);
		return false;
	case PKT_TYPE_TXS:
		for (rxd++; rxd + 7 <= end; rxd += 7)
			mt7615_mac_add_txs(dev, rxd);
		return false;
	default:
		return true;
	}
}
EXPORT_SYMBOL_GPL(mt7615_rx_check);

void mt7615_queue_rx_skb(struct mt76_dev *mdev, enum mt76_rxq_id q,
			 struct sk_buff *skb)
{
	struct mt7615_dev *dev = container_of(mdev, struct mt7615_dev, mt76);
	__le32 *rxd = (__le32 *)skb->data;
	__le32 *end = (__le32 *)&skb->data[skb->len];
	enum rx_pkt_type type;
	u16 flag;

	type = le32_get_bits(rxd[0], MT_RXD0_PKT_TYPE);
	flag = le32_get_bits(rxd[0], MT_RXD0_PKT_FLAG);
	if (type == PKT_TYPE_RX_EVENT && flag == 0x1)
		type = PKT_TYPE_NORMAL_MCU;

	switch (type) {
	case PKT_TYPE_TXS:
		for (rxd++; rxd + 7 <= end; rxd += 7)
			mt7615_mac_add_txs(dev, rxd);
		dev_kfree_skb(skb);
		break;
	case PKT_TYPE_TXRX_NOTIFY:
		mt7615_mac_tx_free(dev, skb->data, skb->len);
		dev_kfree_skb(skb);
		break;
	case PKT_TYPE_RX_EVENT:
		mt7615_mcu_rx_event(dev, skb);
		break;
	case PKT_TYPE_NORMAL_MCU:
	case PKT_TYPE_NORMAL:
		if (!mt7615_mac_fill_rx(dev, skb)) {
			mt76_rx(&dev->mt76, q, skb);
			return;
		}
		fallthrough;
	default:
		dev_kfree_skb(skb);
		break;
	}
}
EXPORT_SYMBOL_GPL(mt7615_queue_rx_skb);

static void
mt7615_mac_set_sensitivity(struct mt7615_phy *phy, int val, bool ofdm)
{
	struct mt7615_dev *dev = phy->dev;
	bool ext_phy = phy != &dev->phy;

	if (is_mt7663(&dev->mt76)) {
		if (ofdm)
			mt76_rmw(dev, MT7663_WF_PHY_MIN_PRI_PWR(ext_phy),
				 MT_WF_PHY_PD_OFDM_MASK(0),
				 MT_WF_PHY_PD_OFDM(0, val));
		else
			mt76_rmw(dev, MT7663_WF_PHY_RXTD_CCK_PD(ext_phy),
				 MT_WF_PHY_PD_CCK_MASK(ext_phy),
				 MT_WF_PHY_PD_CCK(ext_phy, val));
		return;
	}

	if (ofdm)
		mt76_rmw(dev, MT_WF_PHY_MIN_PRI_PWR(ext_phy),
			 MT_WF_PHY_PD_OFDM_MASK(ext_phy),
			 MT_WF_PHY_PD_OFDM(ext_phy, val));
	else
		mt76_rmw(dev, MT_WF_PHY_RXTD_CCK_PD(ext_phy),
			 MT_WF_PHY_PD_CCK_MASK(ext_phy),
			 MT_WF_PHY_PD_CCK(ext_phy, val));
}

static void
mt7615_mac_set_default_sensitivity(struct mt7615_phy *phy)
{
	/* ofdm */
	mt7615_mac_set_sensitivity(phy, 0x13c, true);
	/* cck */
	mt7615_mac_set_sensitivity(phy, 0x92, false);

	phy->ofdm_sensitivity = -98;
	phy->cck_sensitivity = -110;
	phy->last_cca_adj = jiffies;
}

void mt7615_mac_set_scs(struct mt7615_phy *phy, bool enable)
{
	struct mt7615_dev *dev = phy->dev;
	bool ext_phy = phy != &dev->phy;
	u32 reg, mask;

	mt7615_mutex_acquire(dev);

	if (phy->scs_en == enable)
		goto out;

	if (is_mt7663(&dev->mt76)) {
		reg = MT7663_WF_PHY_MIN_PRI_PWR(ext_phy);
		mask = MT_WF_PHY_PD_BLK(0);
	} else {
		reg = MT_WF_PHY_MIN_PRI_PWR(ext_phy);
		mask = MT_WF_PHY_PD_BLK(ext_phy);
	}

	if (enable) {
		mt76_set(dev, reg, mask);
		if (is_mt7622(&dev->mt76)) {
			mt76_set(dev, MT_MIB_M0_MISC_CR(0), 0x7 << 8);
			mt76_set(dev, MT_MIB_M0_MISC_CR(0), 0x7);
		}
	} else {
		mt76_clear(dev, reg, mask);
	}

	mt7615_mac_set_default_sensitivity(phy);
	phy->scs_en = enable;

out:
	mt7615_mutex_release(dev);
}

void mt7615_mac_enable_nf(struct mt7615_dev *dev, bool ext_phy)
{
	u32 rxtd, reg;

	if (is_mt7663(&dev->mt76))
		reg = MT7663_WF_PHY_R0_PHYMUX_5;
	else
		reg = MT_WF_PHY_R0_PHYMUX_5(ext_phy);

	if (ext_phy)
		rxtd = MT_WF_PHY_RXTD2(10);
	else
		rxtd = MT_WF_PHY_RXTD(12);

	mt76_set(dev, rxtd, BIT(18) | BIT(29));
	mt76_set(dev, reg, 0x5 << 12);
}

void mt7615_mac_cca_stats_reset(struct mt7615_phy *phy)
{
	struct mt7615_dev *dev = phy->dev;
	bool ext_phy = phy != &dev->phy;
	u32 reg;

	if (is_mt7663(&dev->mt76))
		reg = MT7663_WF_PHY_R0_PHYMUX_5;
	else
		reg = MT_WF_PHY_R0_PHYMUX_5(ext_phy);

	/* reset PD and MDRDY counters */
	mt76_clear(dev, reg, GENMASK(22, 20));
	mt76_set(dev, reg, BIT(22) | BIT(20));
}

static void
mt7615_mac_adjust_sensitivity(struct mt7615_phy *phy,
			      u32 rts_err_rate, bool ofdm)
{
	struct mt7615_dev *dev = phy->dev;
	int false_cca = ofdm ? phy->false_cca_ofdm : phy->false_cca_cck;
	bool ext_phy = phy != &dev->phy;
	s16 def_th = ofdm ? -98 : -110;
	bool update = false;
	s8 *sensitivity;
	int signal;

	sensitivity = ofdm ? &phy->ofdm_sensitivity : &phy->cck_sensitivity;
	signal = mt76_get_min_avg_rssi(&dev->mt76, ext_phy);
	if (!signal) {
		mt7615_mac_set_default_sensitivity(phy);
		return;
	}

	signal = min(signal, -72);
	if (false_cca > 500) {
		if (rts_err_rate > MT_FRAC(40, 100))
			return;

		/* decrease coverage */
		if (*sensitivity == def_th && signal > -90) {
			*sensitivity = -90;
			update = true;
		} else if (*sensitivity + 2 < signal) {
			*sensitivity += 2;
			update = true;
		}
	} else if ((false_cca > 0 && false_cca < 50) ||
		   rts_err_rate > MT_FRAC(60, 100)) {
		/* increase coverage */
		if (*sensitivity - 2 >= def_th) {
			*sensitivity -= 2;
			update = true;
		}
	}

	if (*sensitivity > signal) {
		*sensitivity = signal;
		update = true;
	}

	if (update) {
		u16 val = ofdm ? *sensitivity * 2 + 512 : *sensitivity + 256;

		mt7615_mac_set_sensitivity(phy, val, ofdm);
		phy->last_cca_adj = jiffies;
	}
}

static void
mt7615_mac_scs_check(struct mt7615_phy *phy)
{
	struct mt7615_dev *dev = phy->dev;
	struct mib_stats *mib = &phy->mib;
	u32 val, rts_err_rate = 0;
	u32 mdrdy_cck, mdrdy_ofdm, pd_cck, pd_ofdm;
	bool ext_phy = phy != &dev->phy;

	if (!phy->scs_en)
		return;

	if (is_mt7663(&dev->mt76))
		val = mt76_rr(dev, MT7663_WF_PHY_R0_PHYCTRL_STS0(ext_phy));
	else
		val = mt76_rr(dev, MT_WF_PHY_R0_PHYCTRL_STS0(ext_phy));
	pd_cck = FIELD_GET(MT_WF_PHYCTRL_STAT_PD_CCK, val);
	pd_ofdm = FIELD_GET(MT_WF_PHYCTRL_STAT_PD_OFDM, val);

	if (is_mt7663(&dev->mt76))
		val = mt76_rr(dev, MT7663_WF_PHY_R0_PHYCTRL_STS5(ext_phy));
	else
		val = mt76_rr(dev, MT_WF_PHY_R0_PHYCTRL_STS5(ext_phy));
	mdrdy_cck = FIELD_GET(MT_WF_PHYCTRL_STAT_MDRDY_CCK, val);
	mdrdy_ofdm = FIELD_GET(MT_WF_PHYCTRL_STAT_MDRDY_OFDM, val);

	phy->false_cca_ofdm = pd_ofdm - mdrdy_ofdm;
	phy->false_cca_cck = pd_cck - mdrdy_cck;
	mt7615_mac_cca_stats_reset(phy);

	if (mib->rts_cnt + mib->rts_retries_cnt)
		rts_err_rate = MT_FRAC(mib->rts_retries_cnt,
				       mib->rts_cnt + mib->rts_retries_cnt);

	/* cck */
	mt7615_mac_adjust_sensitivity(phy, rts_err_rate, false);
	/* ofdm */
	mt7615_mac_adjust_sensitivity(phy, rts_err_rate, true);

	if (time_after(jiffies, phy->last_cca_adj + 10 * HZ))
		mt7615_mac_set_default_sensitivity(phy);
}

static u8
mt7615_phy_get_nf(struct mt7615_dev *dev, int idx)
{
	static const u8 nf_power[] = { 92, 89, 86, 83, 80, 75, 70, 65, 60, 55, 52 };
	u32 reg, val, sum = 0, n = 0;
	int i;

	if (is_mt7663(&dev->mt76))
		reg = MT7663_WF_PHY_RXTD(20);
	else
		reg = idx ? MT_WF_PHY_RXTD2(17) : MT_WF_PHY_RXTD(20);

	for (i = 0; i < ARRAY_SIZE(nf_power); i++, reg += 4) {
		val = mt76_rr(dev, reg);
		sum += val * nf_power[i];
		n += val;
	}

	if (!n)
		return 0;

	return sum / n;
}

static void
mt7615_phy_update_channel(struct mt76_phy *mphy, int idx)
{
	struct mt7615_dev *dev = container_of(mphy->dev, struct mt7615_dev, mt76);
	struct mt7615_phy *phy = mphy->priv;
	struct mt76_channel_state *state;
	u64 busy_time, tx_time, rx_time, obss_time;
	u32 obss_reg = idx ? MT_WF_RMAC_MIB_TIME6 : MT_WF_RMAC_MIB_TIME5;
	int nf;

	busy_time = mt76_get_field(dev, MT_MIB_SDR9(idx),
				   MT_MIB_SDR9_BUSY_MASK);
	tx_time = mt76_get_field(dev, MT_MIB_SDR36(idx),
				 MT_MIB_SDR36_TXTIME_MASK);
	rx_time = mt76_get_field(dev, MT_MIB_SDR37(idx),
				 MT_MIB_SDR37_RXTIME_MASK);
	obss_time = mt76_get_field(dev, obss_reg, MT_MIB_OBSSTIME_MASK);

	nf = mt7615_phy_get_nf(dev, idx);
	if (!phy->noise)
		phy->noise = nf << 4;
	else if (nf)
		phy->noise += nf - (phy->noise >> 4);

	state = mphy->chan_state;
	state->cc_busy += busy_time;
	state->cc_tx += tx_time;
	state->cc_rx += rx_time + obss_time;
	state->cc_bss_rx += rx_time;
	state->noise = -(phy->noise >> 4);
}

static void mt7615_update_survey(struct mt7615_dev *dev)
{
	struct mt76_dev *mdev = &dev->mt76;
	struct mt76_phy *mphy_ext = mdev->phys[MT_BAND1];
	ktime_t cur_time;

	/* MT7615 can only update both phys simultaneously
	 * since some reisters are shared across bands.
	 */

	mt7615_phy_update_channel(&mdev->phy, 0);
	if (mphy_ext)
		mt7615_phy_update_channel(mphy_ext, 1);

	cur_time = ktime_get_boottime();

	mt76_update_survey_active_time(&mdev->phy, cur_time);
	if (mphy_ext)
		mt76_update_survey_active_time(mphy_ext, cur_time);

	/* reset obss airtime */
	mt76_set(dev, MT_WF_RMAC_MIB_TIME0, MT_WF_RMAC_MIB_RXTIME_CLR);
}

void mt7615_update_channel(struct mt76_phy *mphy)
{
	struct mt7615_dev *dev = container_of(mphy->dev, struct mt7615_dev, mt76);

	if (mt76_connac_pm_wake(&dev->mphy, &dev->pm))
		return;

	mt7615_update_survey(dev);
	mt76_connac_power_save_sched(&dev->mphy, &dev->pm);
}
EXPORT_SYMBOL_GPL(mt7615_update_channel);

static void
mt7615_mac_update_mib_stats(struct mt7615_phy *phy)
{
	struct mt7615_dev *dev = phy->dev;
	struct mib_stats *mib = &phy->mib;
	bool ext_phy = phy != &dev->phy;
	int i, aggr;
	u32 val, val2;

	mib->fcs_err_cnt += mt76_get_field(dev, MT_MIB_SDR3(ext_phy),
					   MT_MIB_SDR3_FCS_ERR_MASK);

	val = mt76_get_field(dev, MT_MIB_SDR14(ext_phy),
			     MT_MIB_AMPDU_MPDU_COUNT);
	if (val) {
		val2 = mt76_get_field(dev, MT_MIB_SDR15(ext_phy),
				      MT_MIB_AMPDU_ACK_COUNT);
		mib->aggr_per = 1000 * (val - val2) / val;
	}

	aggr = ext_phy ? ARRAY_SIZE(dev->mt76.aggr_stats) / 2 : 0;
	for (i = 0; i < 4; i++) {
		val = mt76_rr(dev, MT_MIB_MB_SDR1(ext_phy, i));
		mib->ba_miss_cnt += FIELD_GET(MT_MIB_BA_MISS_COUNT_MASK, val);
		mib->ack_fail_cnt += FIELD_GET(MT_MIB_ACK_FAIL_COUNT_MASK,
					       val);

		val = mt76_rr(dev, MT_MIB_MB_SDR0(ext_phy, i));
		mib->rts_cnt += FIELD_GET(MT_MIB_RTS_COUNT_MASK, val);
		mib->rts_retries_cnt += FIELD_GET(MT_MIB_RTS_RETRIES_COUNT_MASK,
						  val);

		val = mt76_rr(dev, MT_TX_AGG_CNT(ext_phy, i));
		dev->mt76.aggr_stats[aggr++] += val & 0xffff;
		dev->mt76.aggr_stats[aggr++] += val >> 16;
	}
}

void mt7615_pm_wake_work(struct work_struct *work)
{
	struct mt7615_dev *dev;
	struct mt76_phy *mphy;

	dev = (struct mt7615_dev *)container_of(work, struct mt7615_dev,
						pm.wake_work);
	mphy = dev->phy.mt76;

	if (!mt7615_mcu_set_drv_ctrl(dev)) {
		struct mt76_dev *mdev = &dev->mt76;
		int i;

		if (mt76_is_sdio(mdev)) {
			mt76_connac_pm_dequeue_skbs(mphy, &dev->pm);
			mt76_worker_schedule(&mdev->sdio.txrx_worker);
		} else {
			local_bh_disable();
			mt76_for_each_q_rx(mdev, i)
				napi_schedule(&mdev->napi[i]);
			local_bh_enable();
			mt76_connac_pm_dequeue_skbs(mphy, &dev->pm);
			mt76_queue_tx_cleanup(dev, mdev->q_mcu[MT_MCUQ_WM],
					      false);
		}

		if (test_bit(MT76_STATE_RUNNING, &mphy->state)) {
			unsigned long timeout;

			timeout = mt7615_get_macwork_timeout(dev);
			ieee80211_queue_delayed_work(mphy->hw, &mphy->mac_work,
						     timeout);
		}
	}

	ieee80211_wake_queues(mphy->hw);
	wake_up(&dev->pm.wait);
}

void mt7615_pm_power_save_work(struct work_struct *work)
{
	struct mt7615_dev *dev;
	unsigned long delta;

	dev = (struct mt7615_dev *)container_of(work, struct mt7615_dev,
						pm.ps_work.work);

	delta = dev->pm.idle_timeout;
	if (test_bit(MT76_HW_SCANNING, &dev->mphy.state) ||
	    test_bit(MT76_HW_SCHED_SCANNING, &dev->mphy.state))
		goto out;

	if (mutex_is_locked(&dev->mt76.mutex))
		/* if mt76 mutex is held we should not put the device
		 * to sleep since we are currently accessing device
		 * register map. We need to wait for the next power_save
		 * trigger.
		 */
		goto out;

	if (time_is_after_jiffies(dev->pm.last_activity + delta)) {
		delta = dev->pm.last_activity + delta - jiffies;
		goto out;
	}

	if (!mt7615_mcu_set_fw_ctrl(dev))
		return;
out:
	queue_delayed_work(dev->mt76.wq, &dev->pm.ps_work, delta);
}

void mt7615_mac_work(struct work_struct *work)
{
	struct mt7615_phy *phy;
	struct mt76_phy *mphy;
	unsigned long timeout;

	mphy = (struct mt76_phy *)container_of(work, struct mt76_phy,
					       mac_work.work);
	phy = mphy->priv;

	mt7615_mutex_acquire(phy->dev);

	mt7615_update_survey(phy->dev);
	if (++mphy->mac_work_count == 5) {
		mphy->mac_work_count = 0;

		mt7615_mac_update_mib_stats(phy);
		mt7615_mac_scs_check(phy);
	}

	mt7615_mutex_release(phy->dev);

	mt76_tx_status_check(mphy->dev, false);

	timeout = mt7615_get_macwork_timeout(phy->dev);
	ieee80211_queue_delayed_work(mphy->hw, &mphy->mac_work, timeout);
}

void mt7615_tx_token_put(struct mt7615_dev *dev)
{
	struct mt76_txwi_cache *txwi;
	int id;

	spin_lock_bh(&dev->mt76.token_lock);
	idr_for_each_entry(&dev->mt76.token, txwi, id)
		mt7615_txwi_free(dev, txwi);
	spin_unlock_bh(&dev->mt76.token_lock);
	idr_destroy(&dev->mt76.token);
}
EXPORT_SYMBOL_GPL(mt7615_tx_token_put);

static void mt7615_dfs_stop_radar_detector(struct mt7615_phy *phy)
{
	struct mt7615_dev *dev = phy->dev;

	if (phy->rdd_state & BIT(0))
		mt76_connac_mcu_rdd_cmd(&dev->mt76, RDD_STOP, 0,
					MT_RX_SEL0, 0);
	if (phy->rdd_state & BIT(1))
		mt76_connac_mcu_rdd_cmd(&dev->mt76, RDD_STOP, 1,
					MT_RX_SEL0, 0);
}

static int mt7615_dfs_start_rdd(struct mt7615_dev *dev, int chain)
{
	int err;

	err = mt76_connac_mcu_rdd_cmd(&dev->mt76, RDD_START, chain,
				      MT_RX_SEL0, 0);
	if (err < 0)
		return err;

	return mt76_connac_mcu_rdd_cmd(&dev->mt76, RDD_DET_MODE, chain,
				       MT_RX_SEL0, 1);
}

static int mt7615_dfs_start_radar_detector(struct mt7615_phy *phy)
{
	struct cfg80211_chan_def *chandef = &phy->mt76->chandef;
	struct mt7615_dev *dev = phy->dev;
	bool ext_phy = phy != &dev->phy;
	int err;

	/* start CAC */
	err = mt76_connac_mcu_rdd_cmd(&dev->mt76, RDD_CAC_START, ext_phy,
				      MT_RX_SEL0, 0);
	if (err < 0)
		return err;

	err = mt7615_dfs_start_rdd(dev, ext_phy);
	if (err < 0)
		return err;

	phy->rdd_state |= BIT(ext_phy);

	if (chandef->width == NL80211_CHAN_WIDTH_160 ||
	    chandef->width == NL80211_CHAN_WIDTH_80P80) {
		err = mt7615_dfs_start_rdd(dev, 1);
		if (err < 0)
			return err;

		phy->rdd_state |= BIT(1);
	}

	return 0;
}

static int
mt7615_dfs_init_radar_specs(struct mt7615_phy *phy)
{
	const struct mt7615_dfs_radar_spec *radar_specs;
	struct mt7615_dev *dev = phy->dev;
	int err, i, lpn = 500;

	switch (dev->mt76.region) {
	case NL80211_DFS_FCC:
		radar_specs = &fcc_radar_specs;
		lpn = 8;
		break;
	case NL80211_DFS_ETSI:
		radar_specs = &etsi_radar_specs;
		break;
	case NL80211_DFS_JP:
		radar_specs = &jp_radar_specs;
		break;
	default:
		return -EINVAL;
	}

	/* avoid FCC radar detection in non-FCC region */
	err = mt7615_mcu_set_fcc5_lpn(dev, lpn);
	if (err < 0)
		return err;

	for (i = 0; i < ARRAY_SIZE(radar_specs->radar_pattern); i++) {
		err = mt7615_mcu_set_radar_th(dev, i,
					      &radar_specs->radar_pattern[i]);
		if (err < 0)
			return err;
	}

	return mt7615_mcu_set_pulse_th(dev, &radar_specs->pulse_th);
}

int mt7615_dfs_init_radar_detector(struct mt7615_phy *phy)
{
	struct cfg80211_chan_def *chandef = &phy->mt76->chandef;
	struct mt7615_dev *dev = phy->dev;
	bool ext_phy = phy != &dev->phy;
	enum mt76_dfs_state dfs_state, prev_state;
	int err;

	if (is_mt7663(&dev->mt76))
		return 0;

	prev_state = phy->mt76->dfs_state;
	dfs_state = mt76_phy_dfs_state(phy->mt76);
	if ((chandef->chan->flags & IEEE80211_CHAN_RADAR) &&
	    dfs_state < MT_DFS_STATE_CAC)
		dfs_state = MT_DFS_STATE_ACTIVE;

	if (prev_state == dfs_state)
		return 0;

	if (dfs_state == MT_DFS_STATE_DISABLED)
		goto stop;

	if (prev_state <= MT_DFS_STATE_DISABLED) {
		err = mt7615_dfs_init_radar_specs(phy);
		if (err < 0)
			return err;

		err = mt7615_dfs_start_radar_detector(phy);
		if (err < 0)
			return err;

		phy->mt76->dfs_state = MT_DFS_STATE_CAC;
	}

	if (dfs_state == MT_DFS_STATE_CAC)
		return 0;

	err = mt76_connac_mcu_rdd_cmd(&dev->mt76, RDD_CAC_END,
				      ext_phy, MT_RX_SEL0, 0);
	if (err < 0) {
		phy->mt76->dfs_state = MT_DFS_STATE_UNKNOWN;
		return err;
	}

	phy->mt76->dfs_state = MT_DFS_STATE_ACTIVE;
	return 0;

stop:
	err = mt76_connac_mcu_rdd_cmd(&dev->mt76, RDD_NORMAL_START, ext_phy,
				      MT_RX_SEL0, 0);
	if (err < 0)
		return err;

	mt7615_dfs_stop_radar_detector(phy);
	phy->mt76->dfs_state = MT_DFS_STATE_DISABLED;

	return 0;
}

int mt7615_mac_set_beacon_filter(struct mt7615_phy *phy,
				 struct ieee80211_vif *vif,
				 bool enable)
{
	struct mt7615_dev *dev = phy->dev;
	bool ext_phy = phy != &dev->phy;
	int err;

	if (!mt7615_firmware_offload(dev))
		return -EOPNOTSUPP;

	switch (vif->type) {
	case NL80211_IFTYPE_MONITOR:
		return 0;
	case NL80211_IFTYPE_MESH_POINT:
	case NL80211_IFTYPE_ADHOC:
	case NL80211_IFTYPE_AP:
		if (enable)
			phy->n_beacon_vif++;
		else
			phy->n_beacon_vif--;
		fallthrough;
	default:
		break;
	}

	err = mt7615_mcu_set_bss_pm(dev, vif, !phy->n_beacon_vif);
	if (err)
		return err;

	if (phy->n_beacon_vif) {
		vif->driver_flags &= ~IEEE80211_VIF_BEACON_FILTER;
		mt76_clear(dev, MT_WF_RFCR(ext_phy),
			   MT_WF_RFCR_DROP_OTHER_BEACON);
	} else {
		vif->driver_flags |= IEEE80211_VIF_BEACON_FILTER;
		mt76_set(dev, MT_WF_RFCR(ext_phy),
			 MT_WF_RFCR_DROP_OTHER_BEACON);
	}

	return 0;
}

void mt7615_coredump_work(struct work_struct *work)
{
	struct mt7615_dev *dev;
	char *dump, *data;

	dev = (struct mt7615_dev *)container_of(work, struct mt7615_dev,
						coredump.work.work);

	if (time_is_after_jiffies(dev->coredump.last_activity +
				  4 * MT76_CONNAC_COREDUMP_TIMEOUT)) {
		queue_delayed_work(dev->mt76.wq, &dev->coredump.work,
				   MT76_CONNAC_COREDUMP_TIMEOUT);
		return;
	}

	dump = vzalloc(MT76_CONNAC_COREDUMP_SZ);
	data = dump;

	while (true) {
		struct sk_buff *skb;

		spin_lock_bh(&dev->mt76.lock);
		skb = __skb_dequeue(&dev->coredump.msg_list);
		spin_unlock_bh(&dev->mt76.lock);

		if (!skb)
			break;

		skb_pull(skb, sizeof(struct mt7615_mcu_rxd));
		if (data + skb->len - dump > MT76_CONNAC_COREDUMP_SZ) {
			dev_kfree_skb(skb);
			continue;
		}

		memcpy(data, skb->data, skb->len);
		data += skb->len;

		dev_kfree_skb(skb);
	}
	dev_coredumpv(dev->mt76.dev, dump, MT76_CONNAC_COREDUMP_SZ,
		      GFP_KERNEL);
}
