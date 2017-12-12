/*
 * Copyright (C) 2016 Felix Fietkau <nbd@nbd.name>
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

#include <linux/etherdevice.h>
#include "mt7603.h"
#include "mt7603_mac.h"

#define MT_PSE_PAGE_SIZE	128

static u32
mt7603_ac_queue_mask0(u32 mask)
{
	u32 ret = 0;

	ret |= GENMASK(3, 0) * !!(mask & BIT(0));
	ret |= GENMASK(8, 5) * !!(mask & BIT(1));
	ret |= GENMASK(13, 10) * !!(mask & BIT(2));
	ret |= GENMASK(19, 16) * !!(mask & BIT(3));
	return ret;
}

static void
mt76_stop_tx_ac(struct mt7603_dev *dev, u32 mask)
{
	mt76_set(dev, MT_WF_ARB_TX_STOP_0, mt7603_ac_queue_mask0(mask));
}

static void
mt76_start_tx_ac(struct mt7603_dev *dev, u32 mask)
{
	mt76_set(dev, MT_WF_ARB_TX_START_0, mt7603_ac_queue_mask0(mask));
}

void mt7603_mac_set_timing(struct mt7603_dev *dev)
{
	u32 cck = FIELD_PREP(MT_TIMEOUT_VAL_PLCP, 247) |
		  FIELD_PREP(MT_TIMEOUT_VAL_CCA, 64);
	u32 ofdm = FIELD_PREP(MT_TIMEOUT_VAL_PLCP, 113) |
		   FIELD_PREP(MT_TIMEOUT_VAL_CCA, 64);
	int offset = 3 * dev->coverage_class;
	u32 reg_offset = FIELD_PREP(MT_TIMEOUT_VAL_PLCP, offset) |
			 FIELD_PREP(MT_TIMEOUT_VAL_CCA, offset);
	int sifs;

	if (dev->mt76.chandef.chan->band == NL80211_BAND_5GHZ)
		sifs = 16;
	else
		sifs = 10;

	mt7603_mcu_set_timing(dev, dev->slottime + offset, sifs, 2, 360);
	mt76_wr(dev, MT_TIMEOUT_CCK, cck + reg_offset);
	mt76_wr(dev, MT_TIMEOUT_OFDM, ofdm + reg_offset);
}

static void
mt7603_wtbl_update(struct mt7603_dev *dev, int idx, u32 mask)
{
	mt76_rmw(dev, MT_WTBL_UPDATE, MT_WTBL_UPDATE_WLAN_IDX,
		 FIELD_PREP(MT_WTBL_UPDATE_WLAN_IDX, idx) | mask);

	mt76_poll(dev, MT_WTBL_UPDATE, MT_WTBL_UPDATE_BUSY, 0, 5000);
}

static u32
mt7603_wtbl1_addr(int idx)
{
	return MT_WTBL1_BASE + idx * MT_WTBL1_SIZE;
}

static u32
mt7603_wtbl2_addr(int idx)
{
	/* Mapped to WTBL2 */
	return MT_PCIE_REMAP_BASE_1 + idx * MT_WTBL2_SIZE;
}

static u32
mt7603_wtbl3_addr(int idx)
{
	u32 base = mt7603_wtbl2_addr(MT7603_WTBL_SIZE);
	return base + idx * MT_WTBL3_SIZE;
}

static u32
mt7603_wtbl4_addr(int idx)
{
	u32 base = mt7603_wtbl3_addr(MT7603_WTBL_SIZE);
	return base + idx * MT_WTBL4_SIZE;
}

void mt7603_wtbl_init(struct mt7603_dev *dev, int idx, int vif,
		      const u8 *mac_addr)
{
	const void *_mac = mac_addr;
	u32 addr = mt7603_wtbl1_addr(idx);
	u32 w0;
	int i;

	w0 = FIELD_PREP(MT_WTBL1_W0_ADDR_HI, get_unaligned_le16(_mac + 4));
	if (vif < 0)
		vif = 0;
	else
		w0 |= MT_WTBL1_W0_RX_CHECK_A1;
	w0 |= FIELD_PREP(MT_WTBL1_W0_MUAR_IDX, vif);

	mt76_poll(dev, MT_WTBL_UPDATE, MT_WTBL_UPDATE_BUSY, 0, 5000);

	mt76_set(dev, addr + 0 * 4, w0);
	mt76_set(dev, addr + 1 * 4,
		 FIELD_PREP(MT_WTBL1_W1_ADDR_LO, get_unaligned_le32(_mac)));
	mt76_set(dev, addr + 2 * 4, MT_WTBL1_W2_ADMISSION_CONTROL);

	mt76_stop_tx_ac(dev, GENMASK(3, 0));
	addr = mt7603_wtbl2_addr(idx);
	for (i = 0; i < MT_WTBL2_SIZE; i += 4)
		mt76_wr(dev, addr + i, 0);
	mt7603_wtbl_update(dev, idx, MT_WTBL_UPDATE_WTBL2);
	mt76_start_tx_ac(dev, GENMASK(3, 0));

	addr = mt7603_wtbl3_addr(idx);
	for (i = 0; i < MT_WTBL3_SIZE; i += 4)
		mt76_wr(dev, addr + i, 0);

	addr = mt7603_wtbl4_addr(idx);
	for (i = 0; i < MT_WTBL4_SIZE; i += 4)
		mt76_wr(dev, addr + i, 0);
}

void mt7603_wtbl_set_ps(struct mt7603_dev *dev, int idx, bool val)
{
	u32 addr = mt7603_wtbl1_addr(idx);

	mt76_set(dev, MT_WTBL1_OR, MT_WTBL1_OR_PSM_WRITE);
	mt76_rmw_field(dev, addr + 3 * 4, MT_WTBL1_W3_POWER_SAVE, val);
	mt76_clear(dev, MT_WTBL1_OR, MT_WTBL1_OR_PSM_WRITE);
}

void mt7603_wtbl_clear(struct mt7603_dev *dev, int idx)
{
	int wtbl2_frame_size = MT_PSE_PAGE_SIZE / MT_WTBL2_SIZE;
	int wtbl2_frame = idx / wtbl2_frame_size;
	int wtbl2_entry = idx % wtbl2_frame_size;

	int wtbl3_base_frame = MT_WTBL3_OFFSET / MT_PSE_PAGE_SIZE;
	int wtbl3_frame_size = MT_PSE_PAGE_SIZE / MT_WTBL3_SIZE;
	int wtbl3_frame = wtbl3_base_frame + idx / wtbl3_frame_size;
	int wtbl3_entry = (idx % wtbl3_frame_size) * 2;

	int wtbl4_base_frame = MT_WTBL4_OFFSET / MT_PSE_PAGE_SIZE;
	int wtbl4_frame_size = MT_PSE_PAGE_SIZE / MT_WTBL4_SIZE;
	int wtbl4_frame = wtbl4_base_frame + idx / wtbl4_frame_size;
	int wtbl4_entry = idx % wtbl4_frame_size;

	u32 addr = MT_WTBL1_BASE + idx * MT_WTBL1_SIZE;
	int i;

	mt76_poll(dev, MT_WTBL_UPDATE, MT_WTBL_UPDATE_BUSY, 0, 5000);

	mt76_wr(dev, addr + 0 * 4,
		MT_WTBL1_W0_RX_CHECK_A1 |
		MT_WTBL1_W0_RX_CHECK_A2 |
		MT_WTBL1_W0_RX_VALID);
	mt76_wr(dev, addr + 1 * 4, 0);
	mt76_wr(dev, addr + 2 * 4, 0);

	mt76_set(dev, MT_WTBL1_OR, MT_WTBL1_OR_PSM_WRITE);

	mt76_wr(dev, addr + 3 * 4,
		FIELD_PREP(MT_WTBL1_W3_WTBL2_FRAME_ID, wtbl2_frame) |
		FIELD_PREP(MT_WTBL1_W3_WTBL2_ENTRY_ID, wtbl2_entry) |
		FIELD_PREP(MT_WTBL1_W3_WTBL4_FRAME_ID, wtbl4_frame) |
		MT_WTBL1_W3_I_PSM | MT_WTBL1_W3_KEEP_I_PSM);
	mt76_wr(dev, addr + 4 * 4,
		FIELD_PREP(MT_WTBL1_W4_WTBL3_FRAME_ID, wtbl3_frame) |
		FIELD_PREP(MT_WTBL1_W4_WTBL3_ENTRY_ID, wtbl3_entry) |
		FIELD_PREP(MT_WTBL1_W4_WTBL4_ENTRY_ID, wtbl4_entry));

	mt76_clear(dev, MT_WTBL1_OR, MT_WTBL1_OR_PSM_WRITE);

	addr = mt7603_wtbl2_addr(idx);

	/* Clear BA information */
	mt76_wr(dev, addr + (15 * 4), 0);

	mt76_stop_tx_ac(dev, GENMASK(3, 0));
	for (i = 2; i <= 4; i++)
		mt76_wr(dev, addr + (i * 4), 0);
	mt7603_wtbl_update(dev, idx, MT_WTBL_UPDATE_WTBL2);
	mt76_start_tx_ac(dev, GENMASK(3, 0));

	mt7603_wtbl_update(dev, idx, MT_WTBL_UPDATE_RX_COUNT_CLEAR);
	mt7603_wtbl_update(dev, idx, MT_WTBL_UPDATE_TX_COUNT_CLEAR);
	mt7603_wtbl_update(dev, idx, MT_WTBL_UPDATE_ADM_COUNT_CLEAR);
}

void mt7603_wtbl_update_cap(struct mt7603_dev *dev, struct ieee80211_sta *sta)
{
	struct mt7603_sta *msta = (struct mt7603_sta *) sta->drv_priv;
	int idx = msta->wcid.idx;
	u32 addr;
	u32 val;

	addr = mt7603_wtbl1_addr(idx);

	val = mt76_rr(dev, addr + 2 * 4);
	val &= MT_WTBL1_W2_KEY_TYPE | MT_WTBL1_W2_ADMISSION_CONTROL;
	val |= FIELD_PREP(MT_WTBL1_W2_AMPDU_FACTOR, sta->ht_cap.ampdu_factor) |
	       FIELD_PREP(MT_WTBL1_W2_MPDU_DENSITY, sta->ht_cap.ampdu_density) |
	       MT_WTBL1_W2_TXS_BAF_REPORT;

	if (sta->ht_cap.cap)
		val |= MT_WTBL1_W2_HT;
	if (sta->vht_cap.cap)
		val |= MT_WTBL1_W2_VHT;

	mt76_wr(dev, addr + 2 * 4, val);

	addr = mt7603_wtbl2_addr(idx);
	val = mt76_rr(dev, addr + 9 * 4);
	val &= ~(MT_WTBL2_W9_SHORT_GI_20 | MT_WTBL2_W9_SHORT_GI_40 |
		 MT_WTBL2_W9_SHORT_GI_80);
	if (sta->ht_cap.cap & IEEE80211_HT_CAP_SGI_20)
		val |= MT_WTBL2_W9_SHORT_GI_20;
	if (sta->ht_cap.cap & IEEE80211_HT_CAP_SGI_40)
		val |= MT_WTBL2_W9_SHORT_GI_40;
	mt76_wr(dev, addr + 9 * 4, val);
}

void mt7603_mac_rx_ba_reset(struct mt7603_dev *dev, void *addr, u8 tid)
{
	mt76_wr(dev, MT_BA_CONTROL_0, get_unaligned_le32(addr));
	mt76_wr(dev, MT_BA_CONTROL_1,
		(get_unaligned_le16(addr + 4) |
		 FIELD_PREP(MT_BA_CONTROL_1_TID, tid) |
		 MT_BA_CONTROL_1_RESET));
}

void mt7603_mac_tx_ba_reset(struct mt7603_dev *dev, int wcid, int tid, int ssn,
			    int ba_size)
{
	u32 addr = mt7603_wtbl2_addr(wcid);
	u32 tid_mask = FIELD_PREP(MT_WTBL2_W15_BA_EN_TIDS, BIT(tid)) |
		       (MT_WTBL2_W15_BA_WIN_SIZE <<
			(tid * MT_WTBL2_W15_BA_WIN_SIZE_SHIFT));
	u32 tid_val;
	int i;

	if (ba_size < 0) {
		/* disable */
		mt76_clear(dev, addr + (15 * 4), tid_mask);
		return;
	}
	mt76_poll(dev, MT_WTBL_UPDATE, MT_WTBL_UPDATE_BUSY, 0, 5000);

	mt7603_mac_stop(dev);
	switch (tid) {
	case 0:
		mt76_rmw_field(dev, addr + (2 * 4), MT_WTBL2_W2_TID0_SN, ssn);
		break;
	case 1:
		mt76_rmw_field(dev, addr + (2 * 4), MT_WTBL2_W2_TID1_SN, ssn);
		break;
	case 2:
		mt76_rmw_field(dev, addr + (2 * 4), MT_WTBL2_W2_TID2_SN_LO,
			       ssn);
		mt76_rmw_field(dev, addr + (3 * 4), MT_WTBL2_W3_TID2_SN_HI,
			       ssn >> 8);
		break;
	case 3:
		mt76_rmw_field(dev, addr + (3 * 4), MT_WTBL2_W3_TID3_SN, ssn);
		break;
	case 4:
		mt76_rmw_field(dev, addr + (3 * 4), MT_WTBL2_W3_TID4_SN, ssn);
		break;
	case 5:
		mt76_rmw_field(dev, addr + (3 * 4), MT_WTBL2_W3_TID5_SN_LO,
			       ssn);
		mt76_rmw_field(dev, addr + (4 * 4), MT_WTBL2_W4_TID5_SN_HI,
			       ssn >> 4);
		break;
	case 6:
		mt76_rmw_field(dev, addr + (4 * 4), MT_WTBL2_W4_TID6_SN, ssn);
		break;
	case 7:
		mt76_rmw_field(dev, addr + (4 * 4), MT_WTBL2_W4_TID7_SN, ssn);
		break;
	}
	mt7603_wtbl_update(dev, wcid, MT_WTBL_UPDATE_WTBL2);
	mt7603_mac_start(dev);

	for (i = 7; i > 0; i--) {
		if (ba_size >= MT_AGG_SIZE_LIMIT(i))
			break;
	}

	tid_val = FIELD_PREP(MT_WTBL2_W15_BA_EN_TIDS, BIT(tid)) |
		  i << (tid * MT_WTBL2_W15_BA_WIN_SIZE_SHIFT);

	mt76_rmw(dev, addr + (15 * 4), tid_mask, tid_val);
}

static int
mt7603_get_rate(struct mt7603_dev *dev, struct ieee80211_supported_band *sband,
		int idx, bool cck)
{
	int offset = 0;
	int len = sband->n_bitrates;
	int i;

	if (cck) {
		if (WARN_ON_ONCE(sband == &dev->mt76.sband_5g.sband))
			return 0;

		idx &= ~BIT(2); /* short preamble */
	} else if (sband == &dev->mt76.sband_2g.sband) {
		offset = 4;
	}

	for (i = offset; i < len; i++) {
		if ((sband->bitrates[i].hw_value & GENMASK(7, 0)) == idx)
			return i;
	}

	WARN_ON_ONCE(1);
	return 0;
}

int
mt7603_mac_fill_rx(struct mt7603_dev *dev, struct sk_buff *skb)
{
	struct ieee80211_rx_status *status = IEEE80211_SKB_RXCB(skb);
	struct ieee80211_supported_band *sband;
	__le32 *rxd = (__le32 *) skb->data;
	u32 rxd0 = le32_to_cpu(rxd[0]);
	u32 rxd1 = le32_to_cpu(rxd[1]);
	u32 rxd2 = le32_to_cpu(rxd[2]);
	bool remove_pad;
	int i;

	memset(status, 0, sizeof(*status));

	i = FIELD_GET(MT_RXD1_NORMAL_CH_FREQ, rxd1);
	sband = (i & 1) ? &dev->mt76.sband_5g.sband : &dev->mt76.sband_2g.sband;
	i >>= 1;

	status->band = sband->band;
	if (i < sband->n_channels)
		status->freq = sband->channels[i].center_freq;

	if (rxd2 & MT_RXD2_NORMAL_FCS_ERR)
		status->flag |= RX_FLAG_FAILED_FCS_CRC;

	if (rxd2 & MT_RXD2_NORMAL_TKIP_MIC_ERR)
		status->flag |= RX_FLAG_MMIC_ERROR;

	if (FIELD_GET(MT_RXD2_NORMAL_SEC_MODE, rxd2) != 0 &&
	    !(rxd2 & (MT_RXD2_NORMAL_CLM | MT_RXD2_NORMAL_CM))) {
		status->flag |= RX_FLAG_DECRYPTED;
		status->flag |= RX_FLAG_IV_STRIPPED | RX_FLAG_MMIC_STRIPPED;
	}

	remove_pad = rxd1 & MT_RXD1_NORMAL_HDR_OFFSET;

	if (rxd2 & MT_RXD2_NORMAL_MAX_LEN_ERROR)
		return -EINVAL;

	if (WARN_ON_ONCE(!sband->channels))
		return -EINVAL;

	rxd += 4;
	if (rxd0 & MT_RXD0_NORMAL_GROUP_4) {
		rxd += 4;
		if ((u8 *) rxd - skb->data >= skb->len)
			return -EINVAL;
	}
	if (rxd0 & MT_RXD0_NORMAL_GROUP_1) {
		rxd += 4;
		if ((u8 *) rxd - skb->data >= skb->len)
			return -EINVAL;
	}
	if (rxd0 & MT_RXD0_NORMAL_GROUP_2) {
		rxd += 2;
		if ((u8 *) rxd - skb->data >= skb->len)
			return -EINVAL;
	}
	if (rxd0 & MT_RXD0_NORMAL_GROUP_3) {
		u32 rxdg0 = le32_to_cpu(rxd[0]);
		u32 rxdg3 = le32_to_cpu(rxd[3]);
		bool cck = false;

		i = FIELD_GET(MT_RXV1_TX_RATE, rxdg0);
		switch (FIELD_GET(MT_RXV1_TX_MODE, rxdg0)) {
		case MT_PHY_TYPE_CCK:
			cck = true;
			/* fall through */
		case MT_PHY_TYPE_OFDM:
			i = mt7603_get_rate(dev, sband, i, cck);
			break;
		case MT_PHY_TYPE_HT_GF:
		case MT_PHY_TYPE_HT:
			status->encoding = RX_ENC_HT;
			break;
		case MT_PHY_TYPE_VHT:
			status->encoding = RX_ENC_VHT;
			break;
		default:
			WARN_ON(1);
		}

		if (rxdg0 & MT_RXV1_HT_SHORT_GI)
			status->enc_flags |= RX_ENC_FLAG_SHORT_GI;

		status->enc_flags |= RX_ENC_FLAG_STBC_MASK *
				    FIELD_GET(MT_RXV1_HT_STBC, rxdg0);

		status->rate_idx = i;

		status->chains = BIT(0) | BIT(1);
		status->chain_signal[0] = FIELD_GET(MT_RXV4_IB_RSSI0, rxdg3) +
					  dev->rssi_offset[0];
		status->chain_signal[1] = FIELD_GET(MT_RXV4_IB_RSSI1, rxdg3) +
					  dev->rssi_offset[1];
		status->signal = max(status->chain_signal[0], status->chain_signal[1]);

		rxd += 6;
		if ((u8 *) rxd - skb->data >= skb->len)
			return -EINVAL;
	}

	skb_pull(skb, (u8 *) rxd - skb->data + 2 * remove_pad);

	return 0;
}

static u16
mt7603_mac_tx_rate_val(struct mt7603_dev *dev,
		       const struct ieee80211_tx_rate *rate, bool stbc, u8 *bw)
{
	u8 phy, nss, rate_idx;
	u16 rateval;

	*bw = 0;
	if (rate->flags & IEEE80211_TX_RC_MCS) {
		rate_idx = rate->idx;
		nss = 1 + (rate->idx >> 3);
		phy = MT_PHY_TYPE_HT;
		if (rate->flags & IEEE80211_TX_RC_GREEN_FIELD)
			phy = MT_PHY_TYPE_HT_GF;
		if (rate->flags & IEEE80211_TX_RC_40_MHZ_WIDTH)
			*bw = 1;
	} else {
		const struct ieee80211_rate *r;
		int band = dev->mt76.chandef.chan->band;
		u16 val;

		nss = 1;
		r = &mt76_hw(dev)->wiphy->bands[band]->bitrates[rate->idx];
		if (rate->flags & IEEE80211_TX_RC_USE_SHORT_PREAMBLE)
			val = r->hw_value_short;
		else
			val = r->hw_value;

		phy = val >> 8;
		rate_idx = val & 0xff;
	}

	rateval = (FIELD_PREP(MT_TX_RATE_IDX, rate_idx) |
		   FIELD_PREP(MT_TX_RATE_MODE, phy));

	if (stbc && nss == 1)
		rateval |= MT_TX_RATE_STBC;

	return rateval;
}

void mt7603_wtbl_set_rates(struct mt7603_dev *dev, struct mt7603_sta *sta,
			   struct ieee80211_tx_rate *probe_rate,
			   struct ieee80211_tx_rate *rates)
{
	int wcid = sta->wcid.idx;
	u32 addr = mt7603_wtbl2_addr(wcid);
	bool stbc = false;
	int n_rates = sta->n_rates;
	u8 bw, bw_prev, bw_idx = 0;
	u16 val[4];
	u16 probe_val;
	u32 w9 = mt76_rr(dev, addr + 9 * 4);
	int count;
	int i;

	if (!mt76_poll(dev, MT_WTBL_UPDATE, MT_WTBL_UPDATE_BUSY, 0, 5000))
		return;

	for (i = 0, count = 0; i < n_rates; i++)
		count += max_t(int, 2 * MT7603_RATE_RETRY, rates[i].count);
	for (i = n_rates; i < 4; i++)
		rates[i] = rates[n_rates - 1];

	w9 &= MT_WTBL2_W9_SHORT_GI_20 | MT_WTBL2_W9_SHORT_GI_40 |
	      MT_WTBL2_W9_SHORT_GI_80;

	val[0] = mt7603_mac_tx_rate_val(dev, &rates[0], stbc, &bw);
	bw_prev = bw;

	if (probe_rate) {
		probe_val = mt7603_mac_tx_rate_val(dev, probe_rate, stbc, &bw);
		if (bw)
			bw_idx = 1;
		else
			bw_prev = 0;
	} else {
		probe_val = val[0];
	}

	w9 |= FIELD_PREP(MT_WTBL2_W9_CC_BW_SEL, bw);
	w9 |= FIELD_PREP(MT_WTBL2_W9_BW_CAP, bw);

	val[1] = mt7603_mac_tx_rate_val(dev, &rates[1], stbc, &bw);
	if (bw_prev) {
		bw_idx = 3;
		bw_prev = bw;
	}

	val[2] = mt7603_mac_tx_rate_val(dev, &rates[2], stbc, &bw);
	if (bw_prev) {
		bw_idx = 5;
		bw_prev = bw;
	}

	val[3] = mt7603_mac_tx_rate_val(dev, &rates[3], stbc, &bw);
	if (bw_prev)
		bw_idx = 7;

	w9 |= FIELD_PREP(MT_WTBL2_W9_CHANGE_BW_RATE,
		       bw_idx ? bw_idx - 1 : 7);

	mt76_wr(dev, MT_WTBL_RIUCR0, w9);

	mt76_wr(dev, MT_WTBL_RIUCR1,
		FIELD_PREP(MT_WTBL_RIUCR1_RATE0, probe_val) |
		FIELD_PREP(MT_WTBL_RIUCR1_RATE1, val[0]) |
		FIELD_PREP(MT_WTBL_RIUCR1_RATE2_LO, val[0]));

	mt76_wr(dev, MT_WTBL_RIUCR2,
		FIELD_PREP(MT_WTBL_RIUCR2_RATE2_HI, val[0] >> 8) |
		FIELD_PREP(MT_WTBL_RIUCR2_RATE3, val[1]) |
		FIELD_PREP(MT_WTBL_RIUCR2_RATE4, val[1]) |
		FIELD_PREP(MT_WTBL_RIUCR2_RATE5_LO, val[2]));

	mt76_wr(dev, MT_WTBL_RIUCR3,
		FIELD_PREP(MT_WTBL_RIUCR3_RATE5_HI, val[2] >> 4) |
		FIELD_PREP(MT_WTBL_RIUCR3_RATE6, val[2]) |
		FIELD_PREP(MT_WTBL_RIUCR3_RATE7, val[3]));

	mt76_wr(dev, MT_WTBL_UPDATE,
		FIELD_PREP(MT_WTBL_UPDATE_WLAN_IDX, wcid) |
		MT_WTBL_UPDATE_RATE_UPDATE |
		MT_WTBL_UPDATE_TX_COUNT_CLEAR);

	if (!sta->wcid.tx_rate_set)
		mt76_poll(dev, MT_WTBL_UPDATE, MT_WTBL_UPDATE_BUSY, 0, 5000);

	sta->rate_count = count;
	sta->wcid.tx_rate_set = true;
}

static enum mt7603_cipher_type
mt7603_mac_get_key_info(struct ieee80211_key_conf *key, u8 *key_data)
{
	memset(key_data, 0, 32);
	if (!key)
		return MT_CIPHER_NONE;

	if (key->keylen > 32)
		return MT_CIPHER_NONE;

	memcpy(key_data, key->key, key->keylen);

	switch(key->cipher) {
	case WLAN_CIPHER_SUITE_WEP40:
		return MT_CIPHER_WEP40;
	case WLAN_CIPHER_SUITE_WEP104:
		return MT_CIPHER_WEP104;
	case WLAN_CIPHER_SUITE_TKIP:
		return MT_CIPHER_TKIP;
	case WLAN_CIPHER_SUITE_CCMP:
		return MT_CIPHER_AES_CCMP;
	default:
		return MT_CIPHER_NONE;
	}
}

int mt7603_wtbl_set_key(struct mt7603_dev *dev, int wcid,
			struct ieee80211_key_conf *key)
{
	enum mt7603_cipher_type cipher;
	u32 addr = mt7603_wtbl3_addr(wcid);
	u8 key_data[32];
	int key_len = sizeof(key_data);

	cipher = mt7603_mac_get_key_info(key, key_data);
	if (cipher == MT_CIPHER_NONE && key)
		return -EOPNOTSUPP;

	if (key && (cipher == MT_CIPHER_WEP40 || cipher == MT_CIPHER_WEP104)) {
		addr += key->keyidx * 16;
		key_len = 16;
	}

	mt76_wr_copy(dev, addr, key_data, key_len);

	addr = mt7603_wtbl1_addr(wcid);
	mt76_rmw_field(dev, addr + 2 * 4, MT_WTBL1_W2_KEY_TYPE, cipher);
	if (key)
		mt76_rmw_field(dev, addr, MT_WTBL1_W0_KEY_IDX, key->keyidx);
	mt76_rmw_field(dev, addr, MT_WTBL1_W0_RX_KEY_VALID, !!key);

	return 0;
}

static int
mt7603_mac_write_txwi(struct mt7603_dev *dev, __le32 *txwi,
		      struct sk_buff *skb, struct mt76_queue *q,
		      struct mt76_wcid *wcid, struct ieee80211_sta *sta,
		      int pid, struct ieee80211_key_conf *key)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_tx_rate *rate = &info->control.rates[0];
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
	struct ieee80211_vif *vif = info->control.vif;
	struct mt7603_vif *mvif;
	int wlan_idx;
	int hdr_len = ieee80211_get_hdrlen_from_skb(skb);
	int tx_count = 8;
	u8 frame_type, frame_subtype;
	u16 fc = le16_to_cpu(hdr->frame_control);
	u8 vif_idx = 0;
	u8 bw;

	if (vif) {
		mvif = (struct mt7603_vif *) vif->drv_priv;
		vif_idx = mvif->idx;
		if (vif_idx && q >= &dev->mt76.q_tx[MT_TXQ_BEACON])
			vif_idx += 0x10;
	}

	if (sta) {
		struct mt7603_sta *msta = (struct mt7603_sta *) sta->drv_priv;
		tx_count = msta->rate_count;
	}

	if (wcid)
		wlan_idx = wcid->idx;
	else
		wlan_idx = MT7603_WTBL_RESERVED;

	frame_type = (fc & IEEE80211_FCTL_FTYPE) >> 2;
	frame_subtype = (fc & IEEE80211_FCTL_STYPE) >> 4;

	txwi[0] = cpu_to_le32(
		FIELD_PREP(MT_TXD0_TX_BYTES, skb->len + MT_TXD_SIZE) |
		FIELD_PREP(MT_TXD0_Q_IDX, q->hw_idx)
	);
	txwi[1] = cpu_to_le32(
		MT_TXD1_LONG_FORMAT |
		FIELD_PREP(MT_TXD1_OWN_MAC, vif_idx) |
		FIELD_PREP(MT_TXD1_TID, skb->priority &
				      IEEE80211_QOS_CTL_TID_MASK) |
		FIELD_PREP(MT_TXD1_HDR_FORMAT, MT_HDR_FORMAT_802_11) |
		FIELD_PREP(MT_TXD1_HDR_INFO, hdr_len / 2) |
		FIELD_PREP(MT_TXD1_WLAN_IDX, wlan_idx) |
		FIELD_PREP(MT_TXD1_PROTECTED, !!key)
	);

	if (info->flags & IEEE80211_TX_CTL_NO_ACK) {
		txwi[1] |= cpu_to_le32(MT_TXD1_NO_ACK);
		pid = MT_PID_NOACK;
	}

	txwi[2] = cpu_to_le32(
		FIELD_PREP(MT_TXD2_FRAME_TYPE, frame_type) |
		FIELD_PREP(MT_TXD2_SUB_TYPE, frame_subtype) |
		FIELD_PREP(MT_TXD2_MULTICAST, is_multicast_ether_addr(hdr->addr1))
	);

	if (!(info->flags & IEEE80211_TX_CTL_AMPDU))
		txwi[2] |= cpu_to_le32(MT_TXD2_BA_DISABLE);

	txwi[4] = 0;
	txwi[5] = cpu_to_le32(
		MT_TXD5_TX_STATUS_HOST | MT_TXD5_SW_POWER_MGMT |
		FIELD_PREP(MT_TXD5_PID, pid)
	);
	txwi[6] = 0;

	if (rate->idx >= 0 && rate->count &&
	    !(info->flags & IEEE80211_TX_CTL_RATE_CTRL_PROBE)) {
		bool stbc = info->flags & IEEE80211_TX_CTL_STBC;
		u16 rateval = mt7603_mac_tx_rate_val(dev, rate, stbc, &bw);

		txwi[6] |= cpu_to_le32(
			MT_TXD6_FIXED_RATE |
			MT_TXD6_FIXED_BW |
			FIELD_PREP(MT_TXD6_BW, bw) |
			FIELD_PREP(MT_TXD6_TX_RATE, rateval)
		);

		if (rate->flags & IEEE80211_TX_RC_SHORT_GI)
			txwi[6] |= cpu_to_le32(MT_TXD6_SGI);

		if (!(rate->flags & IEEE80211_TX_RC_MCS))
			txwi[2] |= cpu_to_le32(MT_TXD2_BA_DISABLE);

		tx_count = rate->count;
	}

	/* use maximum tx count for beacons */
	if (q->hw_idx == MT_TX_HW_QUEUE_BCN)
		tx_count = 0x1f;

	txwi[3] = cpu_to_le32(
		FIELD_PREP(MT_TXD3_REM_TX_COUNT, tx_count) |
		FIELD_PREP(MT_TXD3_SEQ, le16_to_cpu(hdr->seq_ctrl))
	);

	if (key) {
		u64 pn = atomic64_inc_return(&key->tx_pn);
		txwi[3] |= cpu_to_le32(MT_TXD3_PN_VALID);
		txwi[4] = cpu_to_le32(pn & GENMASK(31, 0));
		txwi[5] |= cpu_to_le32(FIELD_PREP(MT_TXD5_PN_HIGH, pn >> 32));
	}

	txwi[7] = 0;

	return 0;
}

static int
mt7603_add_tx_status_skb(struct mt7603_dev *dev, struct mt7603_sta *msta,
			 struct sk_buff *skb)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct mt7603_cb *cb = mt7603_skb_cb(skb);
	int pid;

	if (!msta)
		return 0;

	if (info->flags & IEEE80211_TX_CTL_NO_ACK)
		return 0;

	if (!(info->flags & (IEEE80211_TX_CTL_REQ_TX_STATUS |
			  IEEE80211_TX_CTL_RATE_CTRL_PROBE)) &&
	    info->control.rates[0].idx < 0)
		return 0;

	spin_lock_bh(&dev->status_lock);

	memset(cb, 0, sizeof(*cb));
	msta->pid = (msta->pid + 1) & MT_PID_INDEX;
	if (!msta->pid || msta->pid == MT_PID_NOACK)
	    msta->pid = 1;

	pid = msta->pid;
	cb->wcid = msta->wcid.idx;
	cb->pktid = pid;
	cb->jiffies = jiffies;

	__skb_queue_tail(&dev->status_list, skb);

	spin_unlock_bh(&dev->status_lock);
	return pid;
}

int mt7603_tx_prepare_skb(struct mt76_dev *mdev, void *txwi_ptr,
			  struct sk_buff *skb, struct mt76_queue *q,
			  struct mt76_wcid *wcid, struct ieee80211_sta *sta,
			  u32 *tx_info)
{
	struct mt7603_dev *dev = container_of(mdev, struct mt7603_dev, mt76);
	struct mt7603_sta *msta = container_of(wcid, struct mt7603_sta, wcid);
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_key_conf *key = info->control.hw_key;
	int pid;

	if (!wcid)
		wcid = &dev->global_sta.wcid;

	pid = mt7603_add_tx_status_skb(dev, msta, skb);
	if (!pid)
		skb->next = skb->prev = NULL;

	if (info->flags & IEEE80211_TX_CTL_RATE_CTRL_PROBE) {
		spin_lock_bh(&dev->mt76.lock);
		msta->rate_probe = true;
		mt7603_wtbl_set_rates(dev, msta, &info->control.rates[0],
				      msta->rates);
		spin_unlock_bh(&dev->mt76.lock);
	}

	mt7603_mac_write_txwi(dev, txwi_ptr, skb, q, wcid, sta, pid, key);

	return 0;
}

static bool
mt7603_fill_txs(struct mt7603_dev *dev, struct mt7603_sta *sta,
		struct ieee80211_tx_info *info, __le32 *txs_data)
{
	bool final_mpdu;
	bool ack_timeout;
	bool fixed_rate;
	bool probe;
	bool ampdu;
	int count;
	u32 txs;
	u8 pid;
	int idx;
	int i;

	fixed_rate = info->status.rates[0].count;
	probe = !!(info->flags & IEEE80211_TX_CTL_RATE_CTRL_PROBE);

	txs = le32_to_cpu(txs_data[4]);
	final_mpdu = txs & MT_TXS4_ACKED_MPDU;
	ampdu = !fixed_rate && (txs & MT_TXS4_AMPDU);
	pid = FIELD_GET(MT_TXS4_PID, txs);
	count = FIELD_GET(MT_TXS4_TX_COUNT, txs);

	txs = le32_to_cpu(txs_data[0]);
	ack_timeout = txs & MT_TXS0_ACK_TIMEOUT;

	if (!(txs & MT_TXS0_ACK_ERROR_MASK)) {
		if (!fixed_rate && !ack_timeout)
			sta->ampdu_acked++;
		info->flags |= IEEE80211_TX_STAT_ACK;
	}

	if (!fixed_rate)
		sta->ampdu_count++;

	sta->ampdu_tx_count = max_t(int, sta->ampdu_tx_count, count);
	if (ampdu && !final_mpdu)
		return false;

	if (fixed_rate) {
		info->status.ampdu_len = 1;
		info->status.ampdu_ack_len = !!(info->flags & IEEE80211_TX_STAT_ACK);
	} else {
		info->status.ampdu_len = sta->ampdu_count;
		info->status.ampdu_ack_len = sta->ampdu_acked;
	}

	if (ampdu || (info->flags & IEEE80211_TX_CTL_AMPDU))
		info->flags |= IEEE80211_TX_STAT_AMPDU | IEEE80211_TX_CTL_AMPDU;

	count = sta->ampdu_tx_count;

	sta->ampdu_count = 0;
	sta->ampdu_acked = 0;
	sta->ampdu_tx_count = 0;

	if (fixed_rate && !probe) {
		info->status.rates[0].count = count;
		return true;
	}

	for (i = 0, idx = 0; i < ARRAY_SIZE(info->status.rates); i++) {
		int cur_count = min_t(int, count, 2 * MT7603_RATE_RETRY);

		if (!i && probe) {
			cur_count = 1;
		} else {
			info->status.rates[i] = sta->rates[idx];
			idx++;
		}

		if (i && info->status.rates[i].idx < 0) {
			info->status.rates[i - 1].count += count;
			break;
		}

		if (!count) {
			info->status.rates[i].idx = -1;
			break;
		}

		info->status.rates[i].count = cur_count;
		count -= cur_count;
	}

	return true;
}

static void
mt7603_skb_done(struct mt7603_dev *dev, struct sk_buff *skb, u8 flags)
{
	struct mt7603_cb *cb = mt7603_skb_cb(skb);
	u8 done = MT7603_CB_DMA_DONE | MT7603_CB_TXS_DONE;

	flags |= cb->flags;
	cb->flags = flags;

	if ((flags & done) != done)
		return;

	if (flags & MT7603_CB_TXS_FAILED)
		ieee80211_free_txskb(mt76_hw(dev), skb);
	else
		ieee80211_tx_status(mt76_hw(dev), skb);
}

struct sk_buff *
mt7603_mac_status_skb(struct mt7603_dev *dev, struct mt7603_sta *sta, int pktid)
{
	struct sk_buff *skb, *tmp;

	skb_queue_walk_safe(&dev->status_list, skb, tmp) {
		struct mt7603_cb *cb = mt7603_skb_cb(skb);
		if (sta && cb->wcid != sta->wcid.idx)
			continue;

		__skb_unlink(skb, &dev->status_list);
		if (cb->pktid == pktid)
			return skb;

		mt7603_skb_done(dev, skb,
				MT7603_CB_TXS_FAILED | MT7603_CB_TXS_DONE);
	}

	return NULL;
}

static bool
mt7603_mac_add_txs_skb(struct mt7603_dev *dev, struct mt7603_sta *sta, int pid,
		       __le32 *txs_data)
{
	struct sk_buff *skb;

	if (!pid)
		return false;

	spin_lock_bh(&dev->status_lock);
	skb = mt7603_mac_status_skb(dev, sta, pid);
	spin_unlock_bh(&dev->status_lock);

	if (skb) {
		struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);

		if (info->flags & IEEE80211_TX_CTL_RATE_CTRL_PROBE) {
			spin_lock_bh(&dev->mt76.lock);
			if (sta->rate_probe) {
				mt7603_wtbl_set_rates(dev, sta, NULL, sta->rates);
				sta->rate_probe = false;
			}
			spin_unlock_bh(&dev->mt76.lock);
		}
		mt7603_fill_txs(dev, sta, info, txs_data);
		mt7603_skb_done(dev, skb, MT7603_CB_TXS_DONE);
	}

	return !!skb;
}

void mt7603_mac_add_txs(struct mt7603_dev *dev, void *data)
{
	struct ieee80211_tx_info info = {};
	struct ieee80211_sta *sta = NULL;
	struct mt7603_sta *msta = NULL;
	struct mt76_wcid *wcid;
	__le32 *txs_data = data;
	void *priv;
	u32 txs;
	u8 wcidx;
	u8 pid;

	txs = le32_to_cpu(txs_data[4]);
	pid = FIELD_GET(MT_TXS4_PID, txs);
	txs = le32_to_cpu(txs_data[3]);
	wcidx = FIELD_GET(MT_TXS3_WCID, txs);

	if (pid == MT_PID_NOACK)
		return;

	if (wcidx >= ARRAY_SIZE(dev->wcid))
		return;

	rcu_read_lock();

	wcid = rcu_dereference(dev->wcid[wcidx]);
	if (!wcid)
		goto out;

	priv = msta = container_of(wcid, struct mt7603_sta, wcid);
	sta = container_of(priv, struct ieee80211_sta, drv_priv);

	pid &= MT_PID_INDEX;
	if (mt7603_mac_add_txs_skb(dev, msta, pid, txs_data))
		goto out;

	if (wcidx >= MT7603_WTBL_STA)
		goto out;

	if (mt7603_fill_txs(dev, msta, &info, txs_data))
		ieee80211_tx_status_noskb(mt76_hw(dev), sta, &info);

out:
	rcu_read_unlock();
}

void mt7603_tx_complete_skb(struct mt76_dev *mdev, struct mt76_queue *q,
			    struct mt76_queue_entry *e, bool flush)
{
	struct mt7603_dev *dev = container_of(mdev, struct mt7603_dev, mt76);
	struct sk_buff *skb = e->skb;
	bool free = true;

	if (!e->txwi) {
		dev_kfree_skb_any(skb);
		return;
	}

	if (q - dev->mt76.q_tx < 4)
		dev->tx_hang_check = 0;

	/* will be freed by tx status handling codepath */
	if (skb->prev) {
		spin_lock_bh(&dev->status_lock);
		if (!flush) {
			mt7603_skb_done(dev, skb, MT7603_CB_DMA_DONE);
			free = false;
		} else {
			__skb_unlink(skb, &dev->status_list);
		}
		spin_unlock_bh(&dev->status_lock);
	}

	if (free)
		ieee80211_free_txskb(mdev->hw, skb);
}

static bool
wait_for_wpdma(struct mt7603_dev *dev)
{
	return mt76_poll(dev, MT_WPDMA_GLO_CFG,
			 MT_WPDMA_GLO_CFG_TX_DMA_BUSY |
			 MT_WPDMA_GLO_CFG_RX_DMA_BUSY,
			 0, 1000);
}

void mt7603_mac_reset(struct mt7603_dev *dev)
{
	u32 addr = mt7603_reg_map(dev, MT_CLIENT_BASE_PHYS_ADDR);

	/* Reset PSE */
	mt76_clear(dev, MT_PSE_RESET, MT_PSE_RESET_SW_S);
	mt76_set(dev, MT_PSE_RESET, MT_PSE_RESET_SW);
	if (!mt76_poll_msec(dev, MT_PSE_RESET, MT_PSE_RESET_SW_S,
			    MT_PSE_RESET_SW_S, 500)) {
		mt76_clear(dev, MT_PSE_RESET, MT_PSE_RESET_SW);
		goto out;
	}
	mt76_clear(dev, MT_PSE_RESET, MT_PSE_RESET_SW_S);

	mt76_set(dev, addr + MT_CLIENT_RESET_TX, MT_CLIENT_RESET_TX_R_E_1);
	mt76_poll_msec(dev, addr + MT_CLIENT_RESET_TX,
		       MT_CLIENT_RESET_TX_R_E_1_S,
		       MT_CLIENT_RESET_TX_R_E_1_S, 500);

	mt76_set(dev, addr + MT_CLIENT_RESET_TX, MT_CLIENT_RESET_TX_R_E_2);
	mt76_set(dev, MT_WPDMA_GLO_CFG, MT_WPDMA_GLO_CFG_SW_RESET);

	mt76_poll_msec(dev, addr + MT_CLIENT_RESET_TX,
		       MT_CLIENT_RESET_TX_R_E_2_S,
		       MT_CLIENT_RESET_TX_R_E_2_S, 500);

	mt76_clear(dev, addr + MT_CLIENT_RESET_TX,
		   MT_CLIENT_RESET_TX_R_E_1 | MT_CLIENT_RESET_TX_R_E_2);
out:
	mt76_clear(dev, MT_PSE_RESET, MT_PSE_RESET_QUEUES);
}

void mt7603_mac_dma_start(struct mt7603_dev *dev)
{
	mt7603_mac_start(dev);

	wait_for_wpdma(dev);
	udelay(50);

	mt76_set(dev, MT_WPDMA_GLO_CFG,
		 (MT_WPDMA_GLO_CFG_TX_DMA_EN |
		  MT_WPDMA_GLO_CFG_RX_DMA_EN |
		  FIELD_PREP(MT_WPDMA_GLO_CFG_DMA_BURST_SIZE, 3) |
		  MT_WPDMA_GLO_CFG_TX_WRITEBACK_DONE));

	mt7603_irq_enable(dev, MT_INT_RX_DONE_ALL | MT_INT_TX_DONE_ALL);
}

void mt7603_mac_start(struct mt7603_dev *dev)
{
	mt76_clear(dev, MT_ARB_SCR, MT_ARB_SCR_TX_DISABLE | MT_ARB_SCR_RX_DISABLE);
	mt76_wr(dev, MT_WF_ARB_TX_START_0, ~0);
	mt76_set(dev, MT_WF_ARB_RQCR, MT_WF_ARB_RQCR_RX_START);
}

void mt7603_mac_stop(struct mt7603_dev *dev)
{
	mt76_set(dev, MT_ARB_SCR, MT_ARB_SCR_TX_DISABLE | MT_ARB_SCR_RX_DISABLE);
	mt76_wr(dev, MT_WF_ARB_TX_START_0, 0);
	mt76_clear(dev, MT_WF_ARB_RQCR, MT_WF_ARB_RQCR_RX_START);
}

void mt7603_mac_watchdog_reset(struct mt7603_dev *dev)
{
	int beacon_int = dev->beacon_int;
	u32 mask = dev->irqmask;
	int i;

	ieee80211_stop_queues(dev->mt76.hw);
	set_bit(MT76_RESET, &dev->mt76.state);

	tasklet_disable(&dev->tx_tasklet);
	tasklet_disable(&dev->pre_tbtt_tasklet);
	napi_disable(&dev->mt76.napi[0]);
	napi_disable(&dev->mt76.napi[1]);

	mutex_lock(&dev->mutex);
	mt76_clear(dev, MT_WPDMA_GLO_CFG,
		   MT_WPDMA_GLO_CFG_RX_DMA_EN | MT_WPDMA_GLO_CFG_TX_DMA_EN |
		   MT_WPDMA_GLO_CFG_TX_WRITEBACK_DONE);
	msleep(1);

	mt7603_irq_disable(dev, mask);

	mt7603_beacon_set_timer(dev, -1, 0);
	mt76_set(dev, MT_WPDMA_GLO_CFG, MT_WPDMA_GLO_CFG_FORCE_TX_EOF);

	mt7603_mac_reset(dev);

	for (i = 0; i < ARRAY_SIZE(dev->mt76.q_tx); i++)
		mt76_queue_tx_cleanup(dev, i, true);

	for (i = 0; i < ARRAY_SIZE(dev->mt76.q_rx); i++)
		mt76_queue_rx_reset(dev, i);

	mt7603_mac_dma_start(dev);

	mt7603_irq_enable(dev, mask);
	clear_bit(MT76_RESET, &dev->mt76.state);

	mt7603_beacon_set_timer(dev, -1, beacon_int);
	mutex_unlock(&dev->mutex);

	tasklet_enable(&dev->tx_tasklet);
	tasklet_enable(&dev->pre_tbtt_tasklet);
	napi_enable(&dev->mt76.napi[0]);
	napi_enable(&dev->mt76.napi[1]);
	ieee80211_wake_queues(dev->mt76.hw);
}

static bool mt7603_rx_dma_busy(struct mt7603_dev *dev)
{
	u32 val;

	if (!(mt76_rr(dev, MT_WPDMA_GLO_CFG) & MT_WPDMA_GLO_CFG_RX_DMA_BUSY))
		return false;

	mt76_wr(dev, 0x4244, 0x28000000);
	val = mt76_rr(dev, 0x4244);
	return !!(val & BIT(8));
}

static bool mt7603_tx_dma_busy(struct mt7603_dev *dev)
{
	u32 val;

	if (!(mt76_rr(dev, MT_WPDMA_GLO_CFG) & MT_WPDMA_GLO_CFG_TX_DMA_BUSY))
		return false;

	mt76_wr(dev, 0x4244, 0x98000000);
	val = mt76_rr(dev, 0x4244);
	return (val & BIT(8)) && (val & 0xf) != 0xf;
}

static bool mt7603_tx_hang(struct mt7603_dev *dev)
{
	struct mt76_queue *q;
	u32 dma_idx, prev_dma_idx;
	int i;

	for (i = 0; i < 4; i++) {
		q = &dev->mt76.q_tx[i];

		if (!q->queued)
			continue;

		prev_dma_idx = dev->tx_dma_idx[i];
		dev->tx_dma_idx[i] = dma_idx = ioread32(&q->regs->dma_idx);

		if (dma_idx == prev_dma_idx &&
		    dma_idx != ioread32(&q->regs->cpu_idx))
			break;
	}

	return i < 4;
}

static bool mt7603_rx_pse_busy(struct mt7603_dev *dev)
{
	u32 addr, val;

	mt76_wr(dev, 0x4244, 0x28000000);
	if (mt76_rr(dev, 0x4244) & BIT(8))
		return false;

	addr = mt7603_reg_map(dev, MT_CLIENT_BASE_PHYS_ADDR + MT_CLIENT_STATUS);
	mt76_wr(dev, addr, 3);
	val = mt76_rr(dev, addr) >> 16;

	return (val & 0x8001) == 0x8001 || (val & 0xe001) == 0xe001;
}

static bool
mt7603_watchdog_check(struct mt7603_dev *dev, u8 *counter,
		      enum mt7603_reset_cause cause,
		      bool (*check)(struct mt7603_dev *dev))
{
	if (check) {
		if (!check(dev) && *counter < MT7603_WATCHDOG_TIMEOUT) {
			*counter = 0;
			return false;
		}

		(*counter)++;
	}

	if (*counter < MT7603_WATCHDOG_TIMEOUT)
		return false;

	dev->reset_cause[cause]++;
	return true;
}

void mt7603_mac_work(struct work_struct *work)
{
	struct mt7603_dev *dev = container_of(work, struct mt7603_dev, mac_work.work);
	struct sk_buff *skb;
	bool reset = false;

	spin_lock_bh(&dev->status_lock);
	while ((skb = skb_peek(&dev->status_list)) != NULL) {
		struct mt7603_cb *cb = mt7603_skb_cb(skb);
		if (time_is_after_jiffies(cb->jiffies + MT7603_STATUS_TIMEOUT))
			break;

		__skb_unlink(skb, &dev->status_list);
		mt7603_skb_done(dev, skb,
				MT7603_CB_TXS_FAILED | MT7603_CB_TXS_DONE);
	}
	spin_unlock_bh(&dev->status_lock);

	mutex_lock(&dev->mutex);

	if (WARN_ON_ONCE(mt7603_watchdog_check(dev, &dev->tx_dma_check,
					       RESET_CAUSE_TX_BUSY,
					       mt7603_tx_dma_busy)) ||
	    WARN_ON_ONCE(mt7603_watchdog_check(dev, &dev->rx_dma_check,
					       RESET_CAUSE_RX_BUSY,
					       mt7603_rx_dma_busy)) ||
	    WARN_ON_ONCE(mt7603_watchdog_check(dev, &dev->tx_hang_check,
					       RESET_CAUSE_TX_HANG,
					       mt7603_tx_hang)) ||
	    WARN_ON_ONCE(mt7603_watchdog_check(dev, &dev->rx_pse_check,
					       RESET_CAUSE_RX_PSE_BUSY,
					       mt7603_rx_pse_busy)) ||
	    WARN_ON_ONCE(mt7603_watchdog_check(dev, &dev->beacon_check,
					       RESET_CAUSE_BEACON_STUCK,
					       NULL))) {
		dev->beacon_check = 0;
		dev->tx_dma_check = 0;
		dev->tx_hang_check = 0;
		dev->rx_dma_check = 0;
		dev->rx_pse_check = 0;
		dev->rx_dma_idx = ~0;
		memset(dev->tx_dma_idx, 0xff, sizeof(dev->tx_dma_idx));
		reset = true;
	}

	mutex_unlock(&dev->mutex);

	if (reset)
		mt7603_mac_watchdog_reset(dev);

	ieee80211_queue_delayed_work(mt76_hw(dev), &dev->mac_work,
				     msecs_to_jiffies(MT7603_WATCHDOG_TIME));
}
