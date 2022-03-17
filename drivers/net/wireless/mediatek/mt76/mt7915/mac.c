// SPDX-License-Identifier: ISC
/* Copyright (C) 2020 MediaTek Inc. */

#include <linux/etherdevice.h>
#include <linux/timekeeping.h>
#include "mt7915.h"
#include "../dma.h"
#include "mac.h"
#include "mcu.h"

#define to_rssi(field, rxv)	((FIELD_GET(field, rxv) - 220) / 2)

#define HE_BITS(f)		cpu_to_le16(IEEE80211_RADIOTAP_HE_##f)
#define HE_PREP(f, m, v)	le16_encode_bits(le32_get_bits(v, MT_CRXV_HE_##m),\
						 IEEE80211_RADIOTAP_HE_##f)

static const struct mt7915_dfs_radar_spec etsi_radar_specs = {
	.pulse_th = { 110, -10, -80, 40, 5200, 128, 5200 },
	.radar_pattern = {
		[5] =  { 1, 0,  6, 32, 28, 0,  990, 5010, 17, 1, 1 },
		[6] =  { 1, 0,  9, 32, 28, 0,  615, 5010, 27, 1, 1 },
		[7] =  { 1, 0, 15, 32, 28, 0,  240,  445, 27, 1, 1 },
		[8] =  { 1, 0, 12, 32, 28, 0,  240,  510, 42, 1, 1 },
		[9] =  { 1, 1,  0,  0,  0, 0, 2490, 3343, 14, 0, 0, 12, 32, 28, { }, 126 },
		[10] = { 1, 1,  0,  0,  0, 0, 2490, 3343, 14, 0, 0, 15, 32, 24, { }, 126 },
		[11] = { 1, 1,  0,  0,  0, 0,  823, 2510, 14, 0, 0, 18, 32, 28, { },  54 },
		[12] = { 1, 1,  0,  0,  0, 0,  823, 2510, 14, 0, 0, 27, 32, 24, { },  54 },
	},
};

static const struct mt7915_dfs_radar_spec fcc_radar_specs = {
	.pulse_th = { 110, -10, -80, 40, 5200, 128, 5200 },
	.radar_pattern = {
		[0] = { 1, 0,  8,  32, 28, 0, 508, 3076, 13, 1,  1 },
		[1] = { 1, 0, 12,  32, 28, 0, 140,  240, 17, 1,  1 },
		[2] = { 1, 0,  8,  32, 28, 0, 190,  510, 22, 1,  1 },
		[3] = { 1, 0,  6,  32, 28, 0, 190,  510, 32, 1,  1 },
		[4] = { 1, 0,  9, 255, 28, 0, 323,  343, 13, 1, 32 },
	},
};

static const struct mt7915_dfs_radar_spec jp_radar_specs = {
	.pulse_th = { 110, -10, -80, 40, 5200, 128, 5200 },
	.radar_pattern = {
		[0] =  { 1, 0,  8,  32, 28, 0,  508, 3076,  13, 1,  1 },
		[1] =  { 1, 0, 12,  32, 28, 0,  140,  240,  17, 1,  1 },
		[2] =  { 1, 0,  8,  32, 28, 0,  190,  510,  22, 1,  1 },
		[3] =  { 1, 0,  6,  32, 28, 0,  190,  510,  32, 1,  1 },
		[4] =  { 1, 0,  9, 255, 28, 0,  323,  343,  13, 1, 32 },
		[13] = { 1, 0,  7,  32, 28, 0, 3836, 3856,  14, 1,  1 },
		[14] = { 1, 0,  6,  32, 28, 0,  615, 5010, 110, 1,  1 },
		[15] = { 1, 1,  0,   0,  0, 0,   15, 5010, 110, 0,  0, 12, 32, 28 },
	},
};

static struct mt76_wcid *mt7915_rx_get_wcid(struct mt7915_dev *dev,
					    u16 idx, bool unicast)
{
	struct mt7915_sta *sta;
	struct mt76_wcid *wcid;

	if (idx >= ARRAY_SIZE(dev->mt76.wcid))
		return NULL;

	wcid = rcu_dereference(dev->mt76.wcid[idx]);
	if (unicast || !wcid)
		return wcid;

	if (!wcid->sta)
		return NULL;

	sta = container_of(wcid, struct mt7915_sta, wcid);
	if (!sta->vif)
		return NULL;

	return &sta->vif->sta.wcid;
}

void mt7915_sta_ps(struct mt76_dev *mdev, struct ieee80211_sta *sta, bool ps)
{
}

bool mt7915_mac_wtbl_update(struct mt7915_dev *dev, int idx, u32 mask)
{
	mt76_rmw(dev, MT_WTBL_UPDATE, MT_WTBL_UPDATE_WLAN_IDX,
		 FIELD_PREP(MT_WTBL_UPDATE_WLAN_IDX, idx) | mask);

	return mt76_poll(dev, MT_WTBL_UPDATE, MT_WTBL_UPDATE_BUSY,
			 0, 5000);
}

u32 mt7915_mac_wtbl_lmac_addr(struct mt7915_dev *dev, u16 wcid, u8 dw)
{
	mt76_wr(dev, MT_WTBLON_TOP_WDUCR,
		FIELD_PREP(MT_WTBLON_TOP_WDUCR_GROUP, (wcid >> 7)));

	return MT_WTBL_LMAC_OFFS(wcid, dw);
}

static void mt7915_mac_sta_poll(struct mt7915_dev *dev)
{
	static const u8 ac_to_tid[] = {
		[IEEE80211_AC_BE] = 0,
		[IEEE80211_AC_BK] = 1,
		[IEEE80211_AC_VI] = 4,
		[IEEE80211_AC_VO] = 6
	};
	struct ieee80211_sta *sta;
	struct mt7915_sta *msta;
	struct rate_info *rate;
	u32 tx_time[IEEE80211_NUM_ACS], rx_time[IEEE80211_NUM_ACS];
	LIST_HEAD(sta_poll_list);
	int i;

	spin_lock_bh(&dev->sta_poll_lock);
	list_splice_init(&dev->sta_poll_list, &sta_poll_list);
	spin_unlock_bh(&dev->sta_poll_lock);

	rcu_read_lock();

	while (true) {
		bool clear = false;
		u32 addr, val;
		u16 idx;
		u8 bw;

		spin_lock_bh(&dev->sta_poll_lock);
		if (list_empty(&sta_poll_list)) {
			spin_unlock_bh(&dev->sta_poll_lock);
			break;
		}
		msta = list_first_entry(&sta_poll_list,
					struct mt7915_sta, poll_list);
		list_del_init(&msta->poll_list);
		spin_unlock_bh(&dev->sta_poll_lock);

		idx = msta->wcid.idx;
		addr = mt7915_mac_wtbl_lmac_addr(dev, idx, 20);

		for (i = 0; i < IEEE80211_NUM_ACS; i++) {
			u32 tx_last = msta->airtime_ac[i];
			u32 rx_last = msta->airtime_ac[i + 4];

			msta->airtime_ac[i] = mt76_rr(dev, addr);
			msta->airtime_ac[i + 4] = mt76_rr(dev, addr + 4);

			tx_time[i] = msta->airtime_ac[i] - tx_last;
			rx_time[i] = msta->airtime_ac[i + 4] - rx_last;

			if ((tx_last | rx_last) & BIT(30))
				clear = true;

			addr += 8;
		}

		if (clear) {
			mt7915_mac_wtbl_update(dev, idx,
					       MT_WTBL_UPDATE_ADM_COUNT_CLEAR);
			memset(msta->airtime_ac, 0, sizeof(msta->airtime_ac));
		}

		if (!msta->wcid.sta)
			continue;

		sta = container_of((void *)msta, struct ieee80211_sta,
				   drv_priv);
		for (i = 0; i < IEEE80211_NUM_ACS; i++) {
			u8 q = mt76_connac_lmac_mapping(i);
			u32 tx_cur = tx_time[q];
			u32 rx_cur = rx_time[q];
			u8 tid = ac_to_tid[i];

			if (!tx_cur && !rx_cur)
				continue;

			ieee80211_sta_register_airtime(sta, tid, tx_cur,
						       rx_cur);
		}

		/*
		 * We don't support reading GI info from txs packets.
		 * For accurate tx status reporting and AQL improvement,
		 * we need to make sure that flags match so polling GI
		 * from per-sta counters directly.
		 */
		rate = &msta->wcid.rate;
		addr = mt7915_mac_wtbl_lmac_addr(dev, idx, 7);
		val = mt76_rr(dev, addr);

		switch (rate->bw) {
		case RATE_INFO_BW_160:
			bw = IEEE80211_STA_RX_BW_160;
			break;
		case RATE_INFO_BW_80:
			bw = IEEE80211_STA_RX_BW_80;
			break;
		case RATE_INFO_BW_40:
			bw = IEEE80211_STA_RX_BW_40;
			break;
		default:
			bw = IEEE80211_STA_RX_BW_20;
			break;
		}

		if (rate->flags & RATE_INFO_FLAGS_HE_MCS) {
			u8 offs = 24 + 2 * bw;

			rate->he_gi = (val & (0x3 << offs)) >> offs;
		} else if (rate->flags &
			   (RATE_INFO_FLAGS_VHT_MCS | RATE_INFO_FLAGS_MCS)) {
			if (val & BIT(12 + bw))
				rate->flags |= RATE_INFO_FLAGS_SHORT_GI;
			else
				rate->flags &= ~RATE_INFO_FLAGS_SHORT_GI;
		}
	}

	rcu_read_unlock();
}

static void
mt7915_mac_decode_he_radiotap_ru(struct mt76_rx_status *status,
				 struct ieee80211_radiotap_he *he,
				 __le32 *rxv)
{
	u32 ru_h, ru_l;
	u8 ru, offs = 0;

	ru_l = le32_get_bits(rxv[0], MT_PRXV_HE_RU_ALLOC_L);
	ru_h = le32_get_bits(rxv[1], MT_PRXV_HE_RU_ALLOC_H);
	ru = (u8)(ru_l | ru_h << 4);

	status->bw = RATE_INFO_BW_HE_RU;

	switch (ru) {
	case 0 ... 36:
		status->he_ru = NL80211_RATE_INFO_HE_RU_ALLOC_26;
		offs = ru;
		break;
	case 37 ... 52:
		status->he_ru = NL80211_RATE_INFO_HE_RU_ALLOC_52;
		offs = ru - 37;
		break;
	case 53 ... 60:
		status->he_ru = NL80211_RATE_INFO_HE_RU_ALLOC_106;
		offs = ru - 53;
		break;
	case 61 ... 64:
		status->he_ru = NL80211_RATE_INFO_HE_RU_ALLOC_242;
		offs = ru - 61;
		break;
	case 65 ... 66:
		status->he_ru = NL80211_RATE_INFO_HE_RU_ALLOC_484;
		offs = ru - 65;
		break;
	case 67:
		status->he_ru = NL80211_RATE_INFO_HE_RU_ALLOC_996;
		break;
	case 68:
		status->he_ru = NL80211_RATE_INFO_HE_RU_ALLOC_2x996;
		break;
	}

	he->data1 |= HE_BITS(DATA1_BW_RU_ALLOC_KNOWN);
	he->data2 |= HE_BITS(DATA2_RU_OFFSET_KNOWN) |
		     le16_encode_bits(offs,
				      IEEE80211_RADIOTAP_HE_DATA2_RU_OFFSET);
}

static void
mt7915_mac_decode_he_mu_radiotap(struct sk_buff *skb, __le32 *rxv)
{
	struct mt76_rx_status *status = (struct mt76_rx_status *)skb->cb;
	static const struct ieee80211_radiotap_he_mu mu_known = {
		.flags1 = HE_BITS(MU_FLAGS1_SIG_B_MCS_KNOWN) |
			  HE_BITS(MU_FLAGS1_SIG_B_DCM_KNOWN) |
			  HE_BITS(MU_FLAGS1_CH1_RU_KNOWN) |
			  HE_BITS(MU_FLAGS1_SIG_B_SYMS_USERS_KNOWN),
		.flags2 = HE_BITS(MU_FLAGS2_BW_FROM_SIG_A_BW_KNOWN),
	};
	struct ieee80211_radiotap_he_mu *he_mu = NULL;

	status->flag |= RX_FLAG_RADIOTAP_HE_MU;

	he_mu = skb_push(skb, sizeof(mu_known));
	memcpy(he_mu, &mu_known, sizeof(mu_known));

#define MU_PREP(f, v)	le16_encode_bits(v, IEEE80211_RADIOTAP_HE_MU_##f)

	he_mu->flags1 |= MU_PREP(FLAGS1_SIG_B_MCS, status->rate_idx);
	if (status->he_dcm)
		he_mu->flags1 |= MU_PREP(FLAGS1_SIG_B_DCM, status->he_dcm);

	he_mu->flags2 |= MU_PREP(FLAGS2_BW_FROM_SIG_A_BW, status->bw) |
			 MU_PREP(FLAGS2_SIG_B_SYMS_USERS,
				 le32_get_bits(rxv[2], MT_CRXV_HE_NUM_USER));

	he_mu->ru_ch1[0] = le32_get_bits(rxv[3], MT_CRXV_HE_RU0);

	if (status->bw >= RATE_INFO_BW_40) {
		he_mu->flags1 |= HE_BITS(MU_FLAGS1_CH2_RU_KNOWN);
		he_mu->ru_ch2[0] = le32_get_bits(rxv[3], MT_CRXV_HE_RU1);
	}

	if (status->bw >= RATE_INFO_BW_80) {
		he_mu->ru_ch1[1] = le32_get_bits(rxv[3], MT_CRXV_HE_RU2);
		he_mu->ru_ch2[1] = le32_get_bits(rxv[3], MT_CRXV_HE_RU3);
	}
}

static void
mt7915_mac_decode_he_radiotap(struct sk_buff *skb, __le32 *rxv, u32 mode)
{
	struct mt76_rx_status *status = (struct mt76_rx_status *)skb->cb;
	static const struct ieee80211_radiotap_he known = {
		.data1 = HE_BITS(DATA1_DATA_MCS_KNOWN) |
			 HE_BITS(DATA1_DATA_DCM_KNOWN) |
			 HE_BITS(DATA1_STBC_KNOWN) |
			 HE_BITS(DATA1_CODING_KNOWN) |
			 HE_BITS(DATA1_LDPC_XSYMSEG_KNOWN) |
			 HE_BITS(DATA1_DOPPLER_KNOWN) |
			 HE_BITS(DATA1_SPTL_REUSE_KNOWN) |
			 HE_BITS(DATA1_BSS_COLOR_KNOWN),
		.data2 = HE_BITS(DATA2_GI_KNOWN) |
			 HE_BITS(DATA2_TXBF_KNOWN) |
			 HE_BITS(DATA2_PE_DISAMBIG_KNOWN) |
			 HE_BITS(DATA2_TXOP_KNOWN),
	};
	struct ieee80211_radiotap_he *he = NULL;
	u32 ltf_size = le32_get_bits(rxv[2], MT_CRXV_HE_LTF_SIZE) + 1;

	status->flag |= RX_FLAG_RADIOTAP_HE;

	he = skb_push(skb, sizeof(known));
	memcpy(he, &known, sizeof(known));

	he->data3 = HE_PREP(DATA3_BSS_COLOR, BSS_COLOR, rxv[14]) |
		    HE_PREP(DATA3_LDPC_XSYMSEG, LDPC_EXT_SYM, rxv[2]);
	he->data4 = HE_PREP(DATA4_SU_MU_SPTL_REUSE, SR_MASK, rxv[11]);
	he->data5 = HE_PREP(DATA5_PE_DISAMBIG, PE_DISAMBIG, rxv[2]) |
		    le16_encode_bits(ltf_size,
				     IEEE80211_RADIOTAP_HE_DATA5_LTF_SIZE);
	if (le32_to_cpu(rxv[0]) & MT_PRXV_TXBF)
		he->data5 |= HE_BITS(DATA5_TXBF);
	he->data6 = HE_PREP(DATA6_TXOP, TXOP_DUR, rxv[14]) |
		    HE_PREP(DATA6_DOPPLER, DOPPLER, rxv[14]);

	switch (mode) {
	case MT_PHY_TYPE_HE_SU:
		he->data1 |= HE_BITS(DATA1_FORMAT_SU) |
			     HE_BITS(DATA1_UL_DL_KNOWN) |
			     HE_BITS(DATA1_BEAM_CHANGE_KNOWN) |
			     HE_BITS(DATA1_BW_RU_ALLOC_KNOWN);

		he->data3 |= HE_PREP(DATA3_BEAM_CHANGE, BEAM_CHNG, rxv[14]) |
			     HE_PREP(DATA3_UL_DL, UPLINK, rxv[2]);
		break;
	case MT_PHY_TYPE_HE_EXT_SU:
		he->data1 |= HE_BITS(DATA1_FORMAT_EXT_SU) |
			     HE_BITS(DATA1_UL_DL_KNOWN) |
			     HE_BITS(DATA1_BW_RU_ALLOC_KNOWN);

		he->data3 |= HE_PREP(DATA3_UL_DL, UPLINK, rxv[2]);
		break;
	case MT_PHY_TYPE_HE_MU:
		he->data1 |= HE_BITS(DATA1_FORMAT_MU) |
			     HE_BITS(DATA1_UL_DL_KNOWN);

		he->data3 |= HE_PREP(DATA3_UL_DL, UPLINK, rxv[2]);
		he->data4 |= HE_PREP(DATA4_MU_STA_ID, MU_AID, rxv[7]);

		mt7915_mac_decode_he_radiotap_ru(status, he, rxv);
		mt7915_mac_decode_he_mu_radiotap(skb, rxv);
		break;
	case MT_PHY_TYPE_HE_TB:
		he->data1 |= HE_BITS(DATA1_FORMAT_TRIG) |
			     HE_BITS(DATA1_SPTL_REUSE2_KNOWN) |
			     HE_BITS(DATA1_SPTL_REUSE3_KNOWN) |
			     HE_BITS(DATA1_SPTL_REUSE4_KNOWN);

		he->data4 |= HE_PREP(DATA4_TB_SPTL_REUSE1, SR_MASK, rxv[11]) |
			     HE_PREP(DATA4_TB_SPTL_REUSE2, SR1_MASK, rxv[11]) |
			     HE_PREP(DATA4_TB_SPTL_REUSE3, SR2_MASK, rxv[11]) |
			     HE_PREP(DATA4_TB_SPTL_REUSE4, SR3_MASK, rxv[11]);

		mt7915_mac_decode_he_radiotap_ru(status, he, rxv);
		break;
	default:
		break;
	}
}

/* The HW does not translate the mac header to 802.3 for mesh point */
static int mt7915_reverse_frag0_hdr_trans(struct sk_buff *skb, u16 hdr_gap)
{
	struct mt76_rx_status *status = (struct mt76_rx_status *)skb->cb;
	struct ethhdr *eth_hdr = (struct ethhdr *)(skb->data + hdr_gap);
	struct mt7915_sta *msta = (struct mt7915_sta *)status->wcid;
	__le32 *rxd = (__le32 *)skb->data;
	struct ieee80211_sta *sta;
	struct ieee80211_vif *vif;
	struct ieee80211_hdr hdr;
	u16 frame_control;

	if (le32_get_bits(rxd[3], MT_RXD3_NORMAL_ADDR_TYPE) !=
	    MT_RXD3_NORMAL_U2M)
		return -EINVAL;

	if (!(le32_to_cpu(rxd[1]) & MT_RXD1_NORMAL_GROUP_4))
		return -EINVAL;

	if (!msta || !msta->vif)
		return -EINVAL;

	sta = container_of((void *)msta, struct ieee80211_sta, drv_priv);
	vif = container_of((void *)msta->vif, struct ieee80211_vif, drv_priv);

	/* store the info from RXD and ethhdr to avoid being overridden */
	frame_control = le32_get_bits(rxd[6], MT_RXD6_FRAME_CONTROL);
	hdr.frame_control = cpu_to_le16(frame_control);
	hdr.seq_ctrl = cpu_to_le16(le32_get_bits(rxd[8], MT_RXD8_SEQ_CTRL));
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
		memcpy(skb_push(skb, IEEE80211_HT_CTL_LEN), &rxd[9],
		       IEEE80211_HT_CTL_LEN);
	if (ieee80211_is_data_qos(hdr.frame_control)) {
		__le16 qos_ctrl;

		qos_ctrl = cpu_to_le16(le32_get_bits(rxd[8], MT_RXD8_QOS_CTL));
		memcpy(skb_push(skb, IEEE80211_QOS_CTL_LEN), &qos_ctrl,
		       IEEE80211_QOS_CTL_LEN);
	}

	if (ieee80211_has_a4(hdr.frame_control))
		memcpy(skb_push(skb, sizeof(hdr)), &hdr, sizeof(hdr));
	else
		memcpy(skb_push(skb, sizeof(hdr) - 6), &hdr, sizeof(hdr) - 6);

	return 0;
}

static int
mt7915_mac_fill_rx_rate(struct mt7915_dev *dev,
			struct mt76_rx_status *status,
			struct ieee80211_supported_band *sband,
			__le32 *rxv)
{
	u32 v0, v2;
	u8 stbc, gi, bw, dcm, mode, nss;
	int i, idx;
	bool cck = false;

	v0 = le32_to_cpu(rxv[0]);
	v2 = le32_to_cpu(rxv[2]);

	idx = i = FIELD_GET(MT_PRXV_TX_RATE, v0);
	nss = FIELD_GET(MT_PRXV_NSTS, v0) + 1;

	if (!is_mt7915(&dev->mt76)) {
		stbc = FIELD_GET(MT_PRXV_HT_STBC, v0);
		gi = FIELD_GET(MT_PRXV_HT_SHORT_GI, v0);
		mode = FIELD_GET(MT_PRXV_TX_MODE, v0);
		dcm = FIELD_GET(MT_PRXV_DCM, v0);
		bw = FIELD_GET(MT_PRXV_FRAME_MODE, v0);
	} else {
		stbc = FIELD_GET(MT_CRXV_HT_STBC, v2);
		gi = FIELD_GET(MT_CRXV_HT_SHORT_GI, v2);
		mode = FIELD_GET(MT_CRXV_TX_MODE, v2);
		dcm = !!(idx & GENMASK(3, 0) & MT_PRXV_TX_DCM);
		bw = FIELD_GET(MT_CRXV_FRAME_MODE, v2);
	}

	switch (mode) {
	case MT_PHY_TYPE_CCK:
		cck = true;
		fallthrough;
	case MT_PHY_TYPE_OFDM:
		i = mt76_get_rate(&dev->mt76, sband, i, cck);
		break;
	case MT_PHY_TYPE_HT_GF:
	case MT_PHY_TYPE_HT:
		status->encoding = RX_ENC_HT;
		if (gi)
			status->enc_flags |= RX_ENC_FLAG_SHORT_GI;
		if (i > 31)
			return -EINVAL;
		break;
	case MT_PHY_TYPE_VHT:
		status->nss = nss;
		status->encoding = RX_ENC_VHT;
		if (gi)
			status->enc_flags |= RX_ENC_FLAG_SHORT_GI;
		if (i > 9)
			return -EINVAL;
		break;
	case MT_PHY_TYPE_HE_MU:
	case MT_PHY_TYPE_HE_SU:
	case MT_PHY_TYPE_HE_EXT_SU:
	case MT_PHY_TYPE_HE_TB:
		status->nss = nss;
		status->encoding = RX_ENC_HE;
		i &= GENMASK(3, 0);

		if (gi <= NL80211_RATE_INFO_HE_GI_3_2)
			status->he_gi = gi;

		status->he_dcm = dcm;
		break;
	default:
		return -EINVAL;
	}
	status->rate_idx = i;

	switch (bw) {
	case IEEE80211_STA_RX_BW_20:
		break;
	case IEEE80211_STA_RX_BW_40:
		if (mode & MT_PHY_TYPE_HE_EXT_SU &&
		    (idx & MT_PRXV_TX_ER_SU_106T)) {
			status->bw = RATE_INFO_BW_HE_RU;
			status->he_ru =
				NL80211_RATE_INFO_HE_RU_ALLOC_106;
		} else {
			status->bw = RATE_INFO_BW_40;
		}
		break;
	case IEEE80211_STA_RX_BW_80:
		status->bw = RATE_INFO_BW_80;
		break;
	case IEEE80211_STA_RX_BW_160:
		status->bw = RATE_INFO_BW_160;
		break;
	default:
		return -EINVAL;
	}

	status->enc_flags |= RX_ENC_FLAG_STBC_MASK * stbc;
	if (mode < MT_PHY_TYPE_HE_SU && gi)
		status->enc_flags |= RX_ENC_FLAG_SHORT_GI;

	return 0;
}

static int
mt7915_mac_fill_rx(struct mt7915_dev *dev, struct sk_buff *skb)
{
	struct mt76_rx_status *status = (struct mt76_rx_status *)skb->cb;
	struct mt76_phy *mphy = &dev->mt76.phy;
	struct mt7915_phy *phy = &dev->phy;
	struct ieee80211_supported_band *sband;
	__le32 *rxd = (__le32 *)skb->data;
	__le32 *rxv = NULL;
	u32 mode = 0;
	u32 rxd0 = le32_to_cpu(rxd[0]);
	u32 rxd1 = le32_to_cpu(rxd[1]);
	u32 rxd2 = le32_to_cpu(rxd[2]);
	u32 rxd3 = le32_to_cpu(rxd[3]);
	u32 rxd4 = le32_to_cpu(rxd[4]);
	u32 csum_mask = MT_RXD0_NORMAL_IP_SUM | MT_RXD0_NORMAL_UDP_TCP_SUM;
	bool unicast, insert_ccmp_hdr = false;
	u8 remove_pad, amsdu_info;
	bool hdr_trans;
	u16 hdr_gap;
	u16 seq_ctrl = 0;
	u8 qos_ctl = 0;
	__le16 fc = 0;
	int idx;

	memset(status, 0, sizeof(*status));

	if ((rxd1 & MT_RXD1_NORMAL_BAND_IDX) && !phy->band_idx) {
		mphy = dev->mt76.phy2;
		if (!mphy)
			return -EINVAL;

		phy = mphy->priv;
		status->ext_phy = true;
	}

	if (!test_bit(MT76_STATE_RUNNING, &mphy->state))
		return -EINVAL;

	if (rxd2 & MT_RXD2_NORMAL_AMSDU_ERR)
		return -EINVAL;

	hdr_trans = rxd2 & MT_RXD2_NORMAL_HDR_TRANS;
	if (hdr_trans && (rxd1 & MT_RXD1_NORMAL_CM))
		return -EINVAL;

	/* ICV error or CCMP/BIP/WPI MIC error */
	if (rxd1 & MT_RXD1_NORMAL_ICV_ERR)
		status->flag |= RX_FLAG_ONLY_MONITOR;

	unicast = FIELD_GET(MT_RXD3_NORMAL_ADDR_TYPE, rxd3) == MT_RXD3_NORMAL_U2M;
	idx = FIELD_GET(MT_RXD1_NORMAL_WLAN_IDX, rxd1);
	status->wcid = mt7915_rx_get_wcid(dev, idx, unicast);

	if (status->wcid) {
		struct mt7915_sta *msta;

		msta = container_of(status->wcid, struct mt7915_sta, wcid);
		spin_lock_bh(&dev->sta_poll_lock);
		if (list_empty(&msta->poll_list))
			list_add_tail(&msta->poll_list, &dev->sta_poll_list);
		spin_unlock_bh(&dev->sta_poll_lock);
	}

	status->freq = mphy->chandef.chan->center_freq;
	status->band = mphy->chandef.chan->band;
	if (status->band == NL80211_BAND_5GHZ)
		sband = &mphy->sband_5g.sband;
	else if (status->band == NL80211_BAND_6GHZ)
		sband = &mphy->sband_6g.sband;
	else
		sband = &mphy->sband_2g.sband;

	if (!sband->channels)
		return -EINVAL;

	if ((rxd0 & csum_mask) == csum_mask)
		skb->ip_summed = CHECKSUM_UNNECESSARY;

	if (rxd1 & MT_RXD1_NORMAL_FCS_ERR)
		status->flag |= RX_FLAG_FAILED_FCS_CRC;

	if (rxd1 & MT_RXD1_NORMAL_TKIP_MIC_ERR)
		status->flag |= RX_FLAG_MMIC_ERROR;

	if (FIELD_GET(MT_RXD1_NORMAL_SEC_MODE, rxd1) != 0 &&
	    !(rxd1 & (MT_RXD1_NORMAL_CLM | MT_RXD1_NORMAL_CM))) {
		status->flag |= RX_FLAG_DECRYPTED;
		status->flag |= RX_FLAG_IV_STRIPPED;
		status->flag |= RX_FLAG_MMIC_STRIPPED | RX_FLAG_MIC_STRIPPED;
	}

	remove_pad = FIELD_GET(MT_RXD2_NORMAL_HDR_OFFSET, rxd2);

	if (rxd2 & MT_RXD2_NORMAL_MAX_LEN_ERROR)
		return -EINVAL;

	rxd += 6;
	if (rxd1 & MT_RXD1_NORMAL_GROUP_4) {
		u32 v0 = le32_to_cpu(rxd[0]);
		u32 v2 = le32_to_cpu(rxd[2]);

		fc = cpu_to_le16(FIELD_GET(MT_RXD6_FRAME_CONTROL, v0));
		qos_ctl = FIELD_GET(MT_RXD8_QOS_CTL, v2);
		seq_ctrl = FIELD_GET(MT_RXD8_SEQ_CTRL, v2);

		rxd += 4;
		if ((u8 *)rxd - skb->data >= skb->len)
			return -EINVAL;
	}

	if (rxd1 & MT_RXD1_NORMAL_GROUP_1) {
		u8 *data = (u8 *)rxd;

		if (status->flag & RX_FLAG_DECRYPTED) {
			switch (FIELD_GET(MT_RXD1_NORMAL_SEC_MODE, rxd1)) {
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

	if (rxd1 & MT_RXD1_NORMAL_GROUP_2) {
		status->timestamp = le32_to_cpu(rxd[0]);
		status->flag |= RX_FLAG_MACTIME_START;

		if (!(rxd2 & MT_RXD2_NORMAL_NON_AMPDU)) {
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

	/* RXD Group 3 - P-RXV */
	if (rxd1 & MT_RXD1_NORMAL_GROUP_3) {
		u32 v0, v1;
		int ret;

		rxv = rxd;
		rxd += 2;
		if ((u8 *)rxd - skb->data >= skb->len)
			return -EINVAL;

		v0 = le32_to_cpu(rxv[0]);
		v1 = le32_to_cpu(rxv[1]);

		if (v0 & MT_PRXV_HT_AD_CODE)
			status->enc_flags |= RX_ENC_FLAG_LDPC;

		status->chains = mphy->antenna_mask;
		status->chain_signal[0] = to_rssi(MT_PRXV_RCPI0, v1);
		status->chain_signal[1] = to_rssi(MT_PRXV_RCPI1, v1);
		status->chain_signal[2] = to_rssi(MT_PRXV_RCPI2, v1);
		status->chain_signal[3] = to_rssi(MT_PRXV_RCPI3, v1);

		/* RXD Group 5 - C-RXV */
		if (rxd1 & MT_RXD1_NORMAL_GROUP_5) {
			rxd += 18;
			if ((u8 *)rxd - skb->data >= skb->len)
				return -EINVAL;
		}

		if (!is_mt7915(&dev->mt76) || (rxd1 & MT_RXD1_NORMAL_GROUP_5)) {
			ret = mt7915_mac_fill_rx_rate(dev, status, sband, rxv);
			if (ret < 0)
				return ret;
		}
	}

	amsdu_info = FIELD_GET(MT_RXD4_NORMAL_PAYLOAD_FORMAT, rxd4);
	status->amsdu = !!amsdu_info;
	if (status->amsdu) {
		status->first_amsdu = amsdu_info == MT_RXD4_FIRST_AMSDU_FRAME;
		status->last_amsdu = amsdu_info == MT_RXD4_LAST_AMSDU_FRAME;
	}

	hdr_gap = (u8 *)rxd - skb->data + 2 * remove_pad;
	if (hdr_trans && ieee80211_has_morefrags(fc)) {
		if (mt7915_reverse_frag0_hdr_trans(skb, hdr_gap))
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
			 * type field.
			 */
			pad_start = 12;
			if (get_unaligned_be16(skb->data + pad_start) == ETH_P_8021Q)
				pad_start += 4;

			if (get_unaligned_be16(skb->data + pad_start) !=
			    skb->len - pad_start - 2)
				pad_start = 0;
		}

		if (pad_start) {
			memmove(skb->data + 2, skb->data, pad_start);
			skb_pull(skb, 2);
		}
	}

	if (!hdr_trans) {
		struct ieee80211_hdr *hdr;

		if (insert_ccmp_hdr) {
			u8 key_id = FIELD_GET(MT_RXD1_NORMAL_KEY_ID, rxd1);

			mt76_insert_ccmp_hdr(skb, key_id);
		}

		hdr = mt76_skb_get_hdr(skb);
		fc = hdr->frame_control;
		if (ieee80211_is_data_qos(fc)) {
			seq_ctrl = le16_to_cpu(hdr->seq_ctrl);
			qos_ctl = *ieee80211_get_qos_ctl(hdr);
		}
	} else {
		status->flag |= RX_FLAG_8023;
	}

	if (rxv && mode >= MT_PHY_TYPE_HE_SU && !(status->flag & RX_FLAG_8023))
		mt7915_mac_decode_he_radiotap(skb, rxv, mode);

	if (!status->wcid || !ieee80211_is_data_qos(fc))
		return 0;

	/* drop no data frame */
	if (fc & cpu_to_le16(IEEE80211_STYPE_NULLFUNC))
		return -EINVAL;

	status->aggr = unicast &&
		       !ieee80211_is_qos_nullfunc(fc);
	status->qos_ctl = qos_ctl;
	status->seqno = IEEE80211_SEQ_TO_SN(seq_ctrl);

	return 0;
}

static void
mt7915_mac_fill_rx_vector(struct mt7915_dev *dev, struct sk_buff *skb)
{
#ifdef CONFIG_NL80211_TESTMODE
	struct mt7915_phy *phy = &dev->phy;
	__le32 *rxd = (__le32 *)skb->data;
	__le32 *rxv_hdr = rxd + 2;
	__le32 *rxv = rxd + 4;
	u32 rcpi, ib_rssi, wb_rssi, v20, v21;
	u8 band_idx;
	s32 foe;
	u8 snr;
	int i;

	band_idx = le32_get_bits(rxv_hdr[1], MT_RXV_HDR_BAND_IDX);
	if (band_idx && !phy->band_idx)
		phy = mt7915_ext_phy(dev);

	rcpi = le32_to_cpu(rxv[6]);
	ib_rssi = le32_to_cpu(rxv[7]);
	wb_rssi = le32_to_cpu(rxv[8]) >> 5;

	for (i = 0; i < 4; i++, rcpi >>= 8, ib_rssi >>= 8, wb_rssi >>= 9) {
		if (i == 3)
			wb_rssi = le32_to_cpu(rxv[9]);

		phy->test.last_rcpi[i] = rcpi & 0xff;
		phy->test.last_ib_rssi[i] = ib_rssi & 0xff;
		phy->test.last_wb_rssi[i] = wb_rssi & 0xff;
	}

	v20 = le32_to_cpu(rxv[20]);
	v21 = le32_to_cpu(rxv[21]);

	foe = FIELD_GET(MT_CRXV_FOE_LO, v20) |
	      (FIELD_GET(MT_CRXV_FOE_HI, v21) << MT_CRXV_FOE_SHIFT);

	snr = FIELD_GET(MT_CRXV_SNR, v20) - 16;

	phy->test.last_freq_offset = foe;
	phy->test.last_snr = snr;
#endif

	dev_kfree_skb(skb);
}

static void
mt7915_mac_write_txwi_tm(struct mt7915_phy *phy, __le32 *txwi,
			 struct sk_buff *skb)
{
#ifdef CONFIG_NL80211_TESTMODE
	struct mt76_testmode_data *td = &phy->mt76->test;
	const struct ieee80211_rate *r;
	u8 bw, mode, nss = td->tx_rate_nss;
	u8 rate_idx = td->tx_rate_idx;
	u16 rateval = 0;
	u32 val;
	bool cck = false;
	int band;

	if (skb != phy->mt76->test.tx_skb)
		return;

	switch (td->tx_rate_mode) {
	case MT76_TM_TX_MODE_HT:
		nss = 1 + (rate_idx >> 3);
		mode = MT_PHY_TYPE_HT;
		break;
	case MT76_TM_TX_MODE_VHT:
		mode = MT_PHY_TYPE_VHT;
		break;
	case MT76_TM_TX_MODE_HE_SU:
		mode = MT_PHY_TYPE_HE_SU;
		break;
	case MT76_TM_TX_MODE_HE_EXT_SU:
		mode = MT_PHY_TYPE_HE_EXT_SU;
		break;
	case MT76_TM_TX_MODE_HE_TB:
		mode = MT_PHY_TYPE_HE_TB;
		break;
	case MT76_TM_TX_MODE_HE_MU:
		mode = MT_PHY_TYPE_HE_MU;
		break;
	case MT76_TM_TX_MODE_CCK:
		cck = true;
		fallthrough;
	case MT76_TM_TX_MODE_OFDM:
		band = phy->mt76->chandef.chan->band;
		if (band == NL80211_BAND_2GHZ && !cck)
			rate_idx += 4;

		r = &phy->mt76->hw->wiphy->bands[band]->bitrates[rate_idx];
		val = cck ? r->hw_value_short : r->hw_value;

		mode = val >> 8;
		rate_idx = val & 0xff;
		break;
	default:
		mode = MT_PHY_TYPE_OFDM;
		break;
	}

	switch (phy->mt76->chandef.width) {
	case NL80211_CHAN_WIDTH_40:
		bw = 1;
		break;
	case NL80211_CHAN_WIDTH_80:
		bw = 2;
		break;
	case NL80211_CHAN_WIDTH_80P80:
	case NL80211_CHAN_WIDTH_160:
		bw = 3;
		break;
	default:
		bw = 0;
		break;
	}

	if (td->tx_rate_stbc && nss == 1) {
		nss++;
		rateval |= MT_TX_RATE_STBC;
	}

	rateval |= FIELD_PREP(MT_TX_RATE_IDX, rate_idx) |
		   FIELD_PREP(MT_TX_RATE_MODE, mode) |
		   FIELD_PREP(MT_TX_RATE_NSS, nss - 1);

	txwi[2] |= cpu_to_le32(MT_TXD2_FIX_RATE);

	le32p_replace_bits(&txwi[3], 1, MT_TXD3_REM_TX_COUNT);
	if (td->tx_rate_mode < MT76_TM_TX_MODE_HT)
		txwi[3] |= cpu_to_le32(MT_TXD3_BA_DISABLE);

	val = MT_TXD6_FIXED_BW |
	      FIELD_PREP(MT_TXD6_BW, bw) |
	      FIELD_PREP(MT_TXD6_TX_RATE, rateval) |
	      FIELD_PREP(MT_TXD6_SGI, td->tx_rate_sgi);

	/* for HE_SU/HE_EXT_SU PPDU
	 * - 1x, 2x, 4x LTF + 0.8us GI
	 * - 2x LTF + 1.6us GI, 4x LTF + 3.2us GI
	 * for HE_MU PPDU
	 * - 2x, 4x LTF + 0.8us GI
	 * - 2x LTF + 1.6us GI, 4x LTF + 3.2us GI
	 * for HE_TB PPDU
	 * - 1x, 2x LTF + 1.6us GI
	 * - 4x LTF + 3.2us GI
	 */
	if (mode >= MT_PHY_TYPE_HE_SU)
		val |= FIELD_PREP(MT_TXD6_HELTF, td->tx_ltf);

	if (td->tx_rate_ldpc || (bw > 0 && mode >= MT_PHY_TYPE_HE_SU))
		val |= MT_TXD6_LDPC;

	txwi[3] &= ~cpu_to_le32(MT_TXD3_SN_VALID);
	txwi[6] |= cpu_to_le32(val);
	txwi[7] |= cpu_to_le32(FIELD_PREP(MT_TXD7_SPE_IDX,
					  phy->test.spe_idx));
#endif
}

static void
mt7915_mac_write_txwi_8023(struct mt7915_dev *dev, __le32 *txwi,
			   struct sk_buff *skb, struct mt76_wcid *wcid)
{

	u8 tid = skb->priority & IEEE80211_QOS_CTL_TID_MASK;
	u8 fc_type, fc_stype;
	bool wmm = false;
	u32 val;

	if (wcid->sta) {
		struct ieee80211_sta *sta;

		sta = container_of((void *)wcid, struct ieee80211_sta, drv_priv);
		wmm = sta->wme;
	}

	val = FIELD_PREP(MT_TXD1_HDR_FORMAT, MT_HDR_FORMAT_802_3) |
	      FIELD_PREP(MT_TXD1_TID, tid);

	if (be16_to_cpu(skb->protocol) >= ETH_P_802_3_MIN)
		val |= MT_TXD1_ETH_802_3;

	txwi[1] |= cpu_to_le32(val);

	fc_type = IEEE80211_FTYPE_DATA >> 2;
	fc_stype = wmm ? IEEE80211_STYPE_QOS_DATA >> 4 : 0;

	val = FIELD_PREP(MT_TXD2_FRAME_TYPE, fc_type) |
	      FIELD_PREP(MT_TXD2_SUB_TYPE, fc_stype);

	txwi[2] |= cpu_to_le32(val);

	val = FIELD_PREP(MT_TXD7_TYPE, fc_type) |
	      FIELD_PREP(MT_TXD7_SUB_TYPE, fc_stype);
	txwi[7] |= cpu_to_le32(val);
}

static void
mt7915_mac_write_txwi_80211(struct mt7915_dev *dev, __le32 *txwi,
			    struct sk_buff *skb, struct ieee80211_key_conf *key,
			    bool *mcast)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	struct ieee80211_mgmt *mgmt = (struct ieee80211_mgmt *)skb->data;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	u8 tid = skb->priority & IEEE80211_QOS_CTL_TID_MASK;
	__le16 fc = hdr->frame_control;
	u8 fc_type, fc_stype;
	u32 val;

	*mcast = is_multicast_ether_addr(hdr->addr1);

	if (ieee80211_is_action(fc) &&
	    mgmt->u.action.category == WLAN_CATEGORY_BACK &&
	    mgmt->u.action.u.addba_req.action_code == WLAN_ACTION_ADDBA_REQ) {
		u16 capab = le16_to_cpu(mgmt->u.action.u.addba_req.capab);

		txwi[5] |= cpu_to_le32(MT_TXD5_ADD_BA);
		tid = (capab >> 2) & IEEE80211_QOS_CTL_TID_MASK;
	} else if (ieee80211_is_back_req(hdr->frame_control)) {
		struct ieee80211_bar *bar = (struct ieee80211_bar *)hdr;
		u16 control = le16_to_cpu(bar->control);

		tid = FIELD_GET(IEEE80211_BAR_CTRL_TID_INFO_MASK, control);
	}

	val = FIELD_PREP(MT_TXD1_HDR_FORMAT, MT_HDR_FORMAT_802_11) |
	      FIELD_PREP(MT_TXD1_HDR_INFO,
			 ieee80211_get_hdrlen_from_skb(skb) / 2) |
	      FIELD_PREP(MT_TXD1_TID, tid);
	txwi[1] |= cpu_to_le32(val);

	fc_type = (le16_to_cpu(fc) & IEEE80211_FCTL_FTYPE) >> 2;
	fc_stype = (le16_to_cpu(fc) & IEEE80211_FCTL_STYPE) >> 4;

	val = FIELD_PREP(MT_TXD2_FRAME_TYPE, fc_type) |
	      FIELD_PREP(MT_TXD2_SUB_TYPE, fc_stype) |
	      FIELD_PREP(MT_TXD2_MULTICAST, *mcast);

	if (key && *mcast && ieee80211_is_robust_mgmt_frame(skb) &&
	    key->cipher == WLAN_CIPHER_SUITE_AES_CMAC) {
		val |= MT_TXD2_BIP;
		txwi[3] &= ~cpu_to_le32(MT_TXD3_PROTECT_FRAME);
	}

	if (!ieee80211_is_data(fc) || *mcast ||
	    info->flags & IEEE80211_TX_CTL_USE_MINRATE)
		val |= MT_TXD2_FIX_RATE;

	txwi[2] |= cpu_to_le32(val);

	if (ieee80211_is_beacon(fc)) {
		txwi[3] &= ~cpu_to_le32(MT_TXD3_SW_POWER_MGMT);
		txwi[3] |= cpu_to_le32(MT_TXD3_REM_TX_COUNT);
		txwi[7] |= cpu_to_le32(FIELD_PREP(MT_TXD7_SPE_IDX, 0x18));
	}

	if (info->flags & IEEE80211_TX_CTL_INJECTED) {
		u16 seqno = le16_to_cpu(hdr->seq_ctrl);

		if (ieee80211_is_back_req(hdr->frame_control)) {
			struct ieee80211_bar *bar;

			bar = (struct ieee80211_bar *)skb->data;
			seqno = le16_to_cpu(bar->start_seq_num);
		}

		val = MT_TXD3_SN_VALID |
		      FIELD_PREP(MT_TXD3_SEQ, IEEE80211_SEQ_TO_SN(seqno));
		txwi[3] |= cpu_to_le32(val);
		txwi[7] &= ~cpu_to_le32(MT_TXD7_HW_AMSDU);
	}

	val = FIELD_PREP(MT_TXD7_TYPE, fc_type) |
	      FIELD_PREP(MT_TXD7_SUB_TYPE, fc_stype);
	txwi[7] |= cpu_to_le32(val);
}

static u16
mt7915_mac_tx_rate_val(struct mt76_phy *mphy, struct ieee80211_vif *vif,
		       bool beacon, bool mcast)
{
	u8 mode = 0, band = mphy->chandef.chan->band;
	int rateidx = 0, mcast_rate;

	if (beacon) {
		struct cfg80211_bitrate_mask *mask;

		mask = &vif->bss_conf.beacon_tx_rate;
		if (hweight16(mask->control[band].he_mcs[0]) == 1) {
			rateidx = ffs(mask->control[band].he_mcs[0]) - 1;
			mode = MT_PHY_TYPE_HE_SU;
			goto out;
		} else if (hweight16(mask->control[band].vht_mcs[0]) == 1) {
			rateidx = ffs(mask->control[band].vht_mcs[0]) - 1;
			mode = MT_PHY_TYPE_VHT;
			goto out;
		} else if (hweight8(mask->control[band].ht_mcs[0]) == 1) {
			rateidx = ffs(mask->control[band].ht_mcs[0]) - 1;
			mode = MT_PHY_TYPE_HT;
			goto out;
		} else if (hweight32(mask->control[band].legacy) == 1) {
			rateidx = ffs(mask->control[band].legacy) - 1;
			goto legacy;
		}
	}

	mcast_rate = vif->bss_conf.mcast_rate[band];
	if (mcast && mcast_rate > 0)
		rateidx = mcast_rate - 1;
	else
		rateidx = ffs(vif->bss_conf.basic_rates) - 1;

legacy:
	rateidx = mt76_calculate_default_rate(mphy, rateidx);
	mode = rateidx >> 8;
	rateidx &= GENMASK(7, 0);

out:
	return FIELD_PREP(MT_TX_RATE_IDX, rateidx) |
	       FIELD_PREP(MT_TX_RATE_MODE, mode);
}

void mt7915_mac_write_txwi(struct mt7915_dev *dev, __le32 *txwi,
			   struct sk_buff *skb, struct mt76_wcid *wcid, int pid,
			   struct ieee80211_key_conf *key, bool beacon)
{
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ieee80211_vif *vif = info->control.vif;
	struct mt76_phy *mphy = &dev->mphy;
	bool ext_phy = info->hw_queue & MT_TX_HW_QUEUE_EXT_PHY;
	u8 p_fmt, q_idx, omac_idx = 0, wmm_idx = 0, band_idx = 0;
	bool is_8023 = info->flags & IEEE80211_TX_CTL_HW_80211_ENCAP;
	bool mcast = false;
	u16 tx_count = 15;
	u32 val;

	if (vif) {
		struct mt7915_vif *mvif = (struct mt7915_vif *)vif->drv_priv;

		omac_idx = mvif->mt76.omac_idx;
		wmm_idx = mvif->mt76.wmm_idx;
		band_idx = mvif->mt76.band_idx;
	}

	if (ext_phy && dev->mt76.phy2)
		mphy = dev->mt76.phy2;

	if (beacon) {
		p_fmt = MT_TX_TYPE_FW;
		q_idx = MT_LMAC_BCN0;
	} else if (skb_get_queue_mapping(skb) >= MT_TXQ_PSD) {
		p_fmt = MT_TX_TYPE_CT;
		q_idx = MT_LMAC_ALTX0;
	} else {
		p_fmt = MT_TX_TYPE_CT;
		q_idx = wmm_idx * MT7915_MAX_WMM_SETS +
			mt76_connac_lmac_mapping(skb_get_queue_mapping(skb));
	}

	val = FIELD_PREP(MT_TXD0_TX_BYTES, skb->len + MT_TXD_SIZE) |
	      FIELD_PREP(MT_TXD0_PKT_FMT, p_fmt) |
	      FIELD_PREP(MT_TXD0_Q_IDX, q_idx);
	txwi[0] = cpu_to_le32(val);

	val = MT_TXD1_LONG_FORMAT | MT_TXD1_VTA |
	      FIELD_PREP(MT_TXD1_WLAN_IDX, wcid->idx) |
	      FIELD_PREP(MT_TXD1_OWN_MAC, omac_idx);

	if (ext_phy || band_idx)
		val |= MT_TXD1_TGID;

	txwi[1] = cpu_to_le32(val);

	txwi[2] = 0;

	val = MT_TXD3_SW_POWER_MGMT |
	      FIELD_PREP(MT_TXD3_REM_TX_COUNT, tx_count);
	if (key)
		val |= MT_TXD3_PROTECT_FRAME;
	if (info->flags & IEEE80211_TX_CTL_NO_ACK)
		val |= MT_TXD3_NO_ACK;

	txwi[3] = cpu_to_le32(val);
	txwi[4] = 0;

	val = FIELD_PREP(MT_TXD5_PID, pid);
	if (pid >= MT_PACKET_ID_FIRST)
		val |= MT_TXD5_TX_STATUS_HOST;
	txwi[5] = cpu_to_le32(val);

	txwi[6] = 0;
	txwi[7] = wcid->amsdu ? cpu_to_le32(MT_TXD7_HW_AMSDU) : 0;

	if (is_8023)
		mt7915_mac_write_txwi_8023(dev, txwi, skb, wcid);
	else
		mt7915_mac_write_txwi_80211(dev, txwi, skb, key, &mcast);

	if (txwi[2] & cpu_to_le32(MT_TXD2_FIX_RATE)) {
		u16 rate = mt7915_mac_tx_rate_val(mphy, vif, beacon, mcast);

		/* hardware won't add HTC for mgmt/ctrl frame */
		txwi[2] |= cpu_to_le32(MT_TXD2_HTC_VLD);

		val = MT_TXD6_FIXED_BW |
		      FIELD_PREP(MT_TXD6_TX_RATE, rate);
		txwi[6] |= cpu_to_le32(val);
		txwi[3] |= cpu_to_le32(MT_TXD3_BA_DISABLE);
	}

	if (mt76_testmode_enabled(mphy))
		mt7915_mac_write_txwi_tm(mphy->priv, txwi, skb);
}

int mt7915_tx_prepare_skb(struct mt76_dev *mdev, void *txwi_ptr,
			  enum mt76_txq_id qid, struct mt76_wcid *wcid,
			  struct ieee80211_sta *sta,
			  struct mt76_tx_info *tx_info)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)tx_info->skb->data;
	struct mt7915_dev *dev = container_of(mdev, struct mt7915_dev, mt76);
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(tx_info->skb);
	struct ieee80211_key_conf *key = info->control.hw_key;
	struct ieee80211_vif *vif = info->control.vif;
	struct mt76_txwi_cache *t;
	struct mt7915_txp *txp;
	int id, i, nbuf = tx_info->nbuf - 1;
	u8 *txwi = (u8 *)txwi_ptr;
	int pid;

	if (unlikely(tx_info->skb->len <= ETH_HLEN))
		return -EINVAL;

	if (!wcid)
		wcid = &dev->mt76.global_wcid;

	if (sta) {
		struct mt7915_sta *msta;

		msta = (struct mt7915_sta *)sta->drv_priv;

		if (time_after(jiffies, msta->jiffies + HZ / 4)) {
			info->flags |= IEEE80211_TX_CTL_REQ_TX_STATUS;
			msta->jiffies = jiffies;
		}
	}

	t = (struct mt76_txwi_cache *)(txwi + mdev->drv->txwi_size);
	t->skb = tx_info->skb;

	id = mt76_token_consume(mdev, &t);
	if (id < 0)
		return id;

	pid = mt76_tx_status_skb_add(mdev, wcid, tx_info->skb);
	mt7915_mac_write_txwi(dev, txwi_ptr, tx_info->skb, wcid, pid, key,
			      false);

	txp = (struct mt7915_txp *)(txwi + MT_TXD_SIZE);
	for (i = 0; i < nbuf; i++) {
		txp->buf[i] = cpu_to_le32(tx_info->buf[i + 1].addr);
		txp->len[i] = cpu_to_le16(tx_info->buf[i + 1].len);
	}
	txp->nbuf = nbuf;

	txp->flags = cpu_to_le16(MT_CT_INFO_APPLY_TXD | MT_CT_INFO_FROM_HOST);

	if (!key)
		txp->flags |= cpu_to_le16(MT_CT_INFO_NONE_CIPHER_FRAME);

	if (!(info->flags & IEEE80211_TX_CTL_HW_80211_ENCAP) &&
	    ieee80211_is_mgmt(hdr->frame_control))
		txp->flags |= cpu_to_le16(MT_CT_INFO_MGMT_FRAME);

	if (vif) {
		struct mt7915_vif *mvif = (struct mt7915_vif *)vif->drv_priv;

		txp->bss_idx = mvif->mt76.idx;
	}

	txp->token = cpu_to_le16(id);
	if (test_bit(MT_WCID_FLAG_4ADDR, &wcid->flags))
		txp->rept_wds_wcid = cpu_to_le16(wcid->idx);
	else
		txp->rept_wds_wcid = cpu_to_le16(0x3ff);
	tx_info->skb = DMA_DUMMY_DATA;

	/* pass partial skb header to fw */
	tx_info->buf[1].len = MT_CT_PARSE_LEN;
	tx_info->buf[1].skip_unmap = true;
	tx_info->nbuf = MT_CT_DMA_BUF_NUM;

	return 0;
}

static void
mt7915_tx_check_aggr(struct ieee80211_sta *sta, __le32 *txwi)
{
	struct mt7915_sta *msta;
	u16 fc, tid;
	u32 val;

	if (!sta || !(sta->ht_cap.ht_supported || sta->he_cap.has_he))
		return;

	tid = le32_get_bits(txwi[1], MT_TXD1_TID);
	if (tid >= 6) /* skip VO queue */
		return;

	val = le32_to_cpu(txwi[2]);
	fc = FIELD_GET(MT_TXD2_FRAME_TYPE, val) << 2 |
	     FIELD_GET(MT_TXD2_SUB_TYPE, val) << 4;
	if (unlikely(fc != (IEEE80211_FTYPE_DATA | IEEE80211_STYPE_QOS_DATA)))
		return;

	msta = (struct mt7915_sta *)sta->drv_priv;
	if (!test_and_set_bit(tid, &msta->ampdu_state))
		ieee80211_start_tx_ba_session(sta, tid, 0);
}

static void
mt7915_txp_skb_unmap(struct mt76_dev *dev, struct mt76_txwi_cache *t)
{
	struct mt7915_txp *txp;
	int i;

	txp = mt7915_txwi_to_txp(dev, t);
	for (i = 0; i < txp->nbuf; i++)
		dma_unmap_single(dev->dev, le32_to_cpu(txp->buf[i]),
				 le16_to_cpu(txp->len[i]), DMA_TO_DEVICE);
}

static void
mt7915_txwi_free(struct mt7915_dev *dev, struct mt76_txwi_cache *t,
		 struct ieee80211_sta *sta, struct list_head *free_list)
{
	struct mt76_dev *mdev = &dev->mt76;
	struct mt76_wcid *wcid;
	__le32 *txwi;
	u16 wcid_idx;

	mt7915_txp_skb_unmap(mdev, t);
	if (!t->skb)
		goto out;

	txwi = (__le32 *)mt76_get_txwi_ptr(mdev, t);
	if (sta) {
		wcid = (struct mt76_wcid *)sta->drv_priv;
		wcid_idx = wcid->idx;

		if (likely(t->skb->protocol != cpu_to_be16(ETH_P_PAE)))
			mt7915_tx_check_aggr(sta, txwi);
	} else {
		wcid_idx = le32_get_bits(txwi[1], MT_TXD1_WLAN_IDX);
	}

	__mt76_tx_complete_skb(mdev, wcid_idx, t->skb, free_list);

out:
	t->skb = NULL;
	mt76_put_txwi(mdev, t);
}

static void
mt7915_mac_tx_free(struct mt7915_dev *dev, void *data, int len)
{
	struct mt7915_tx_free *free = (struct mt7915_tx_free *)data;
	struct mt76_dev *mdev = &dev->mt76;
	struct mt76_phy *mphy_ext = mdev->phy2;
	struct mt76_txwi_cache *txwi;
	struct ieee80211_sta *sta = NULL;
	LIST_HEAD(free_list);
	struct sk_buff *skb, *tmp;
	void *end = data + len;
	bool v3, wake = false;
	u16 total, count = 0;
	u32 txd = le32_to_cpu(free->txd);
	__le32 *cur_info;

	/* clean DMA queues and unmap buffers first */
	mt76_queue_tx_cleanup(dev, dev->mphy.q_tx[MT_TXQ_PSD], false);
	mt76_queue_tx_cleanup(dev, dev->mphy.q_tx[MT_TXQ_BE], false);
	if (mphy_ext) {
		mt76_queue_tx_cleanup(dev, mphy_ext->q_tx[MT_TXQ_PSD], false);
		mt76_queue_tx_cleanup(dev, mphy_ext->q_tx[MT_TXQ_BE], false);
	}

	total = le16_get_bits(free->ctrl, MT_TX_FREE_MSDU_CNT);
	v3 = (FIELD_GET(MT_TX_FREE_VER, txd) == 0x4);
	if (WARN_ON_ONCE((void *)&free->info[total >> v3] > end))
		return;

	for (cur_info = &free->info[0]; count < total; cur_info++) {
		u32 msdu, info = le32_to_cpu(*cur_info);
		u8 i;

		/*
		 * 1'b1: new wcid pair.
		 * 1'b0: msdu_id with the same 'wcid pair' as above.
		 */
		if (info & MT_TX_FREE_PAIR) {
			struct mt7915_sta *msta;
			struct mt76_wcid *wcid;
			u16 idx;

			idx = FIELD_GET(MT_TX_FREE_WLAN_ID, info);
			wcid = rcu_dereference(dev->mt76.wcid[idx]);
			sta = wcid_to_sta(wcid);
			if (!sta)
				continue;

			msta = container_of(wcid, struct mt7915_sta, wcid);
			spin_lock_bh(&dev->sta_poll_lock);
			if (list_empty(&msta->poll_list))
				list_add_tail(&msta->poll_list, &dev->sta_poll_list);
			spin_unlock_bh(&dev->sta_poll_lock);
			continue;
		}

		if (v3 && (info & MT_TX_FREE_MPDU_HEADER))
			continue;

		for (i = 0; i < 1 + v3; i++) {
			if (v3) {
				msdu = (info >> (15 * i)) & MT_TX_FREE_MSDU_ID_V3;
				if (msdu == MT_TX_FREE_MSDU_ID_V3)
					continue;
			} else {
				msdu = FIELD_GET(MT_TX_FREE_MSDU_ID, info);
			}
			count++;
			txwi = mt76_token_release(mdev, msdu, &wake);
			if (!txwi)
				continue;

			mt7915_txwi_free(dev, txwi, sta, &free_list);
		}
	}

	mt7915_mac_sta_poll(dev);

	if (wake)
		mt76_set_tx_blocked(&dev->mt76, false);

	mt76_worker_schedule(&dev->mt76.tx_worker);

	list_for_each_entry_safe(skb, tmp, &free_list, list) {
		skb_list_del_init(skb);
		napi_consume_skb(skb, 1);
	}
}

static bool
mt7915_mac_add_txs_skb(struct mt7915_dev *dev, struct mt76_wcid *wcid, int pid,
		       __le32 *txs_data, struct mt76_sta_stats *stats)
{
	struct ieee80211_supported_band *sband;
	struct mt76_dev *mdev = &dev->mt76;
	struct mt76_phy *mphy;
	struct ieee80211_tx_info *info;
	struct sk_buff_head list;
	struct rate_info rate = {};
	struct sk_buff *skb;
	bool cck = false;
	u32 txrate, txs, mode;

	mt76_tx_status_lock(mdev, &list);
	skb = mt76_tx_status_skb_get(mdev, wcid, pid, &list);
	if (!skb)
		goto out_no_skb;

	txs = le32_to_cpu(txs_data[0]);

	info = IEEE80211_SKB_CB(skb);
	if (!(txs & MT_TXS0_ACK_ERROR_MASK))
		info->flags |= IEEE80211_TX_STAT_ACK;

	info->status.ampdu_len = 1;
	info->status.ampdu_ack_len = !!(info->flags &
					IEEE80211_TX_STAT_ACK);

	info->status.rates[0].idx = -1;

	txrate = FIELD_GET(MT_TXS0_TX_RATE, txs);

	rate.mcs = FIELD_GET(MT_TX_RATE_IDX, txrate);
	rate.nss = FIELD_GET(MT_TX_RATE_NSS, txrate) + 1;

	if (rate.nss - 1 < ARRAY_SIZE(stats->tx_nss))
		stats->tx_nss[rate.nss - 1]++;
	if (rate.mcs < ARRAY_SIZE(stats->tx_mcs))
		stats->tx_mcs[rate.mcs]++;

	mode = FIELD_GET(MT_TX_RATE_MODE, txrate);
	switch (mode) {
	case MT_PHY_TYPE_CCK:
		cck = true;
		fallthrough;
	case MT_PHY_TYPE_OFDM:
		mphy = &dev->mphy;
		if (wcid->ext_phy && dev->mt76.phy2)
			mphy = dev->mt76.phy2;

		if (mphy->chandef.chan->band == NL80211_BAND_5GHZ)
			sband = &mphy->sband_5g.sband;
		else if (mphy->chandef.chan->band == NL80211_BAND_6GHZ)
			sband = &mphy->sband_6g.sband;
		else
			sband = &mphy->sband_2g.sband;

		rate.mcs = mt76_get_rate(mphy->dev, sband, rate.mcs, cck);
		rate.legacy = sband->bitrates[rate.mcs].bitrate;
		break;
	case MT_PHY_TYPE_HT:
	case MT_PHY_TYPE_HT_GF:
		if (rate.mcs > 31)
			goto out;

		rate.flags = RATE_INFO_FLAGS_MCS;
		if (wcid->rate.flags & RATE_INFO_FLAGS_SHORT_GI)
			rate.flags |= RATE_INFO_FLAGS_SHORT_GI;
		break;
	case MT_PHY_TYPE_VHT:
		if (rate.mcs > 9)
			goto out;

		rate.flags = RATE_INFO_FLAGS_VHT_MCS;
		break;
	case MT_PHY_TYPE_HE_SU:
	case MT_PHY_TYPE_HE_EXT_SU:
	case MT_PHY_TYPE_HE_TB:
	case MT_PHY_TYPE_HE_MU:
		if (rate.mcs > 11)
			goto out;

		rate.he_gi = wcid->rate.he_gi;
		rate.he_dcm = FIELD_GET(MT_TX_RATE_DCM, txrate);
		rate.flags = RATE_INFO_FLAGS_HE_MCS;
		break;
	default:
		goto out;
	}

	stats->tx_mode[mode]++;

	switch (FIELD_GET(MT_TXS0_BW, txs)) {
	case IEEE80211_STA_RX_BW_160:
		rate.bw = RATE_INFO_BW_160;
		stats->tx_bw[3]++;
		break;
	case IEEE80211_STA_RX_BW_80:
		rate.bw = RATE_INFO_BW_80;
		stats->tx_bw[2]++;
		break;
	case IEEE80211_STA_RX_BW_40:
		rate.bw = RATE_INFO_BW_40;
		stats->tx_bw[1]++;
		break;
	default:
		rate.bw = RATE_INFO_BW_20;
		stats->tx_bw[0]++;
		break;
	}
	wcid->rate = rate;

out:
	mt76_tx_status_skb_done(mdev, skb, &list);

out_no_skb:
	mt76_tx_status_unlock(mdev, &list);

	return !!skb;
}

static void mt7915_mac_add_txs(struct mt7915_dev *dev, void *data)
{
	struct mt7915_sta *msta = NULL;
	struct mt76_wcid *wcid;
	__le32 *txs_data = data;
	u16 wcidx;
	u8 pid;

	if (le32_get_bits(txs_data[0], MT_TXS0_TXS_FORMAT) > 1)
		return;

	wcidx = le32_get_bits(txs_data[2], MT_TXS2_WCID);
	pid = le32_get_bits(txs_data[3], MT_TXS3_PID);

	if (pid < MT_PACKET_ID_FIRST)
		return;

	if (wcidx >= mt7915_wtbl_size(dev))
		return;

	rcu_read_lock();

	wcid = rcu_dereference(dev->mt76.wcid[wcidx]);
	if (!wcid)
		goto out;

	msta = container_of(wcid, struct mt7915_sta, wcid);

	mt7915_mac_add_txs_skb(dev, wcid, pid, txs_data, &msta->stats);

	if (!wcid->sta)
		goto out;

	spin_lock_bh(&dev->sta_poll_lock);
	if (list_empty(&msta->poll_list))
		list_add_tail(&msta->poll_list, &dev->sta_poll_list);
	spin_unlock_bh(&dev->sta_poll_lock);

out:
	rcu_read_unlock();
}

bool mt7915_rx_check(struct mt76_dev *mdev, void *data, int len)
{
	struct mt7915_dev *dev = container_of(mdev, struct mt7915_dev, mt76);
	__le32 *rxd = (__le32 *)data;
	__le32 *end = (__le32 *)&rxd[len / 4];
	enum rx_pkt_type type;

	type = le32_get_bits(rxd[0], MT_RXD0_PKT_TYPE);

	switch (type) {
	case PKT_TYPE_TXRX_NOTIFY:
		mt7915_mac_tx_free(dev, data, len);
		return false;
	case PKT_TYPE_TXS:
		for (rxd += 2; rxd + 8 <= end; rxd += 8)
		    mt7915_mac_add_txs(dev, rxd);
		return false;
	case PKT_TYPE_RX_FW_MONITOR:
		mt7915_debugfs_rx_fw_monitor(dev, data, len);
		return false;
	default:
		return true;
	}
}

void mt7915_queue_rx_skb(struct mt76_dev *mdev, enum mt76_rxq_id q,
			 struct sk_buff *skb)
{
	struct mt7915_dev *dev = container_of(mdev, struct mt7915_dev, mt76);
	__le32 *rxd = (__le32 *)skb->data;
	__le32 *end = (__le32 *)&skb->data[skb->len];
	enum rx_pkt_type type;

	type = le32_get_bits(rxd[0], MT_RXD0_PKT_TYPE);

	switch (type) {
	case PKT_TYPE_TXRX_NOTIFY:
		mt7915_mac_tx_free(dev, skb->data, skb->len);
		napi_consume_skb(skb, 1);
		break;
	case PKT_TYPE_RX_EVENT:
		mt7915_mcu_rx_event(dev, skb);
		break;
	case PKT_TYPE_TXRXV:
		mt7915_mac_fill_rx_vector(dev, skb);
		break;
	case PKT_TYPE_TXS:
		for (rxd += 2; rxd + 8 <= end; rxd += 8)
		    mt7915_mac_add_txs(dev, rxd);
		dev_kfree_skb(skb);
		break;
	case PKT_TYPE_RX_FW_MONITOR:
		mt7915_debugfs_rx_fw_monitor(dev, skb->data, skb->len);
		dev_kfree_skb(skb);
		break;
	case PKT_TYPE_NORMAL:
		if (!mt7915_mac_fill_rx(dev, skb)) {
			mt76_rx(&dev->mt76, q, skb);
			return;
		}
		fallthrough;
	default:
		dev_kfree_skb(skb);
		break;
	}
}

void mt7915_tx_complete_skb(struct mt76_dev *mdev, struct mt76_queue_entry *e)
{
	if (!e->txwi) {
		dev_kfree_skb_any(e->skb);
		return;
	}

	/* error path */
	if (e->skb == DMA_DUMMY_DATA) {
		struct mt76_txwi_cache *t;
		struct mt7915_txp *txp;

		txp = mt7915_txwi_to_txp(mdev, e->txwi);
		t = mt76_token_put(mdev, le16_to_cpu(txp->token));
		e->skb = t ? t->skb : NULL;
	}

	if (e->skb)
		mt76_tx_complete_skb(mdev, e->wcid, e->skb);
}

void mt7915_mac_cca_stats_reset(struct mt7915_phy *phy)
{
	struct mt7915_dev *dev = phy->dev;
	u32 reg = MT_WF_PHY_RX_CTRL1(phy->band_idx);

	mt76_clear(dev, reg, MT_WF_PHY_RX_CTRL1_STSCNT_EN);
	mt76_set(dev, reg, BIT(11) | BIT(9));
}

void mt7915_mac_reset_counters(struct mt7915_phy *phy)
{
	struct mt7915_dev *dev = phy->dev;
	int i;

	for (i = 0; i < 4; i++) {
		mt76_rr(dev, MT_TX_AGG_CNT(phy->band_idx, i));
		mt76_rr(dev, MT_TX_AGG_CNT2(phy->band_idx, i));
	}

	i = 0;
	phy->mt76->survey_time = ktime_get_boottime();
	if (phy->band_idx)
		i = ARRAY_SIZE(dev->mt76.aggr_stats) / 2;

	memset(&dev->mt76.aggr_stats[i], 0, sizeof(dev->mt76.aggr_stats) / 2);

	/* reset airtime counters */
	mt76_set(dev, MT_WF_RMAC_MIB_AIRTIME0(phy->band_idx),
		 MT_WF_RMAC_MIB_RXTIME_CLR);

	mt7915_mcu_get_chan_mib_info(phy, true);
}

void mt7915_mac_set_timing(struct mt7915_phy *phy)
{
	s16 coverage_class = phy->coverage_class;
	struct mt7915_dev *dev = phy->dev;
	struct mt7915_phy *ext_phy = mt7915_ext_phy(dev);
	u32 val, reg_offset;
	u32 cck = FIELD_PREP(MT_TIMEOUT_VAL_PLCP, 231) |
		  FIELD_PREP(MT_TIMEOUT_VAL_CCA, 48);
	u32 ofdm = FIELD_PREP(MT_TIMEOUT_VAL_PLCP, 60) |
		   FIELD_PREP(MT_TIMEOUT_VAL_CCA, 28);
	int offset;
	bool a_band = !(phy->mt76->chandef.chan->band == NL80211_BAND_2GHZ);

	if (!test_bit(MT76_STATE_RUNNING, &phy->mt76->state))
		return;

	if (ext_phy)
		coverage_class = max_t(s16, dev->phy.coverage_class,
				       ext_phy->coverage_class);

	mt76_set(dev, MT_ARB_SCR(phy->band_idx),
		 MT_ARB_SCR_TX_DISABLE | MT_ARB_SCR_RX_DISABLE);
	udelay(1);

	offset = 3 * coverage_class;
	reg_offset = FIELD_PREP(MT_TIMEOUT_VAL_PLCP, offset) |
		     FIELD_PREP(MT_TIMEOUT_VAL_CCA, offset);

	mt76_wr(dev, MT_TMAC_CDTR(phy->band_idx), cck + reg_offset);
	mt76_wr(dev, MT_TMAC_ODTR(phy->band_idx), ofdm + reg_offset);
	mt76_wr(dev, MT_TMAC_ICR0(phy->band_idx),
		FIELD_PREP(MT_IFS_EIFS_OFDM, a_band ? 84 : 78) |
		FIELD_PREP(MT_IFS_RIFS, 2) |
		FIELD_PREP(MT_IFS_SIFS, 10) |
		FIELD_PREP(MT_IFS_SLOT, phy->slottime));

	mt76_wr(dev, MT_TMAC_ICR1(phy->band_idx),
		FIELD_PREP(MT_IFS_EIFS_CCK, 314));

	if (phy->slottime < 20 || a_band)
		val = MT7915_CFEND_RATE_DEFAULT;
	else
		val = MT7915_CFEND_RATE_11B;

	mt76_rmw_field(dev, MT_AGG_ACR0(phy->band_idx), MT_AGG_ACR_CFEND_RATE, val);
	mt76_clear(dev, MT_ARB_SCR(phy->band_idx),
		   MT_ARB_SCR_TX_DISABLE | MT_ARB_SCR_RX_DISABLE);
}

void mt7915_mac_enable_nf(struct mt7915_dev *dev, bool ext_phy)
{
	u32 reg;

	reg = is_mt7915(&dev->mt76) ? MT_WF_PHY_RXTD12(ext_phy) :
		MT_WF_PHY_RXTD12_MT7916(ext_phy);
	mt76_set(dev, reg,
		 MT_WF_PHY_RXTD12_IRPI_SW_CLR_ONLY |
		 MT_WF_PHY_RXTD12_IRPI_SW_CLR);

	reg = is_mt7915(&dev->mt76) ? MT_WF_PHY_RX_CTRL1(ext_phy) :
		MT_WF_PHY_RX_CTRL1_MT7916(ext_phy);
	mt76_set(dev, reg, FIELD_PREP(MT_WF_PHY_RX_CTRL1_IPI_EN, 0x5));
}

static u8
mt7915_phy_get_nf(struct mt7915_phy *phy, int idx)
{
	static const u8 nf_power[] = { 92, 89, 86, 83, 80, 75, 70, 65, 60, 55, 52 };
	struct mt7915_dev *dev = phy->dev;
	u32 val, sum = 0, n = 0;
	int nss, i;

	for (nss = 0; nss < hweight8(phy->mt76->chainmask); nss++) {
		u32 reg = is_mt7915(&dev->mt76) ?
			MT_WF_IRPI_NSS(0, nss + (idx << dev->dbdc_support)) :
			MT_WF_IRPI_NSS_MT7916(idx, nss);

		for (i = 0; i < ARRAY_SIZE(nf_power); i++, reg += 4) {
			val = mt76_rr(dev, reg);
			sum += val * nf_power[i];
			n += val;
		}
	}

	if (!n)
		return 0;

	return sum / n;
}

void mt7915_update_channel(struct mt76_phy *mphy)
{
	struct mt7915_phy *phy = (struct mt7915_phy *)mphy->priv;
	struct mt76_channel_state *state = mphy->chan_state;
	int nf;

	mt7915_mcu_get_chan_mib_info(phy, false);

	nf = mt7915_phy_get_nf(phy, phy->band_idx);
	if (!phy->noise)
		phy->noise = nf << 4;
	else if (nf)
		phy->noise += nf - (phy->noise >> 4);

	state->noise = -(phy->noise >> 4);
}

static bool
mt7915_wait_reset_state(struct mt7915_dev *dev, u32 state)
{
	bool ret;

	ret = wait_event_timeout(dev->reset_wait,
				 (READ_ONCE(dev->reset_state) & state),
				 MT7915_RESET_TIMEOUT);

	WARN(!ret, "Timeout waiting for MCU reset state %x\n", state);
	return ret;
}

static void
mt7915_update_vif_beacon(void *priv, u8 *mac, struct ieee80211_vif *vif)
{
	struct ieee80211_hw *hw = priv;

	switch (vif->type) {
	case NL80211_IFTYPE_MESH_POINT:
	case NL80211_IFTYPE_ADHOC:
	case NL80211_IFTYPE_AP:
		mt7915_mcu_add_beacon(hw, vif, vif->bss_conf.enable_beacon);
		break;
	default:
		break;
	}
}

static void
mt7915_update_beacons(struct mt7915_dev *dev)
{
	ieee80211_iterate_active_interfaces(dev->mt76.hw,
		IEEE80211_IFACE_ITER_RESUME_ALL,
		mt7915_update_vif_beacon, dev->mt76.hw);

	if (!dev->mt76.phy2)
		return;

	ieee80211_iterate_active_interfaces(dev->mt76.phy2->hw,
		IEEE80211_IFACE_ITER_RESUME_ALL,
		mt7915_update_vif_beacon, dev->mt76.phy2->hw);
}

static void
mt7915_dma_reset(struct mt7915_dev *dev)
{
	struct mt76_phy *mphy_ext = dev->mt76.phy2;
	u32 hif1_ofs = MT_WFDMA0_PCIE1(0) - MT_WFDMA0(0);
	int i;

	mt76_clear(dev, MT_WFDMA0_GLO_CFG,
		   MT_WFDMA0_GLO_CFG_TX_DMA_EN |
		   MT_WFDMA0_GLO_CFG_RX_DMA_EN);

	if (is_mt7915(&dev->mt76))
		mt76_clear(dev, MT_WFDMA1_GLO_CFG,
			   MT_WFDMA1_GLO_CFG_TX_DMA_EN |
			   MT_WFDMA1_GLO_CFG_RX_DMA_EN);
	if (dev->hif2) {
		mt76_clear(dev, MT_WFDMA0_GLO_CFG + hif1_ofs,
			   MT_WFDMA0_GLO_CFG_TX_DMA_EN |
			   MT_WFDMA0_GLO_CFG_RX_DMA_EN);

		if (is_mt7915(&dev->mt76))
			mt76_clear(dev, MT_WFDMA1_GLO_CFG + hif1_ofs,
				   MT_WFDMA1_GLO_CFG_TX_DMA_EN |
				   MT_WFDMA1_GLO_CFG_RX_DMA_EN);
	}

	usleep_range(1000, 2000);

	for (i = 0; i < __MT_TXQ_MAX; i++) {
		mt76_queue_tx_cleanup(dev, dev->mphy.q_tx[i], true);
		if (mphy_ext)
			mt76_queue_tx_cleanup(dev, mphy_ext->q_tx[i], true);
	}

	for (i = 0; i < __MT_MCUQ_MAX; i++)
		mt76_queue_tx_cleanup(dev, dev->mt76.q_mcu[i], true);

	mt76_for_each_q_rx(&dev->mt76, i)
		mt76_queue_rx_reset(dev, i);

	mt76_tx_status_check(&dev->mt76, true);

	/* re-init prefetch settings after reset */
	mt7915_dma_prefetch(dev);

	mt76_set(dev, MT_WFDMA0_GLO_CFG,
		 MT_WFDMA0_GLO_CFG_TX_DMA_EN | MT_WFDMA0_GLO_CFG_RX_DMA_EN);
	if (is_mt7915(&dev->mt76))
		mt76_set(dev, MT_WFDMA1_GLO_CFG,
			 MT_WFDMA1_GLO_CFG_TX_DMA_EN |
			 MT_WFDMA1_GLO_CFG_RX_DMA_EN |
			 MT_WFDMA1_GLO_CFG_OMIT_TX_INFO |
			 MT_WFDMA1_GLO_CFG_OMIT_RX_INFO);
	if (dev->hif2) {
		mt76_set(dev, MT_WFDMA0_GLO_CFG + hif1_ofs,
			 MT_WFDMA0_GLO_CFG_TX_DMA_EN |
			 MT_WFDMA0_GLO_CFG_RX_DMA_EN);

		if (is_mt7915(&dev->mt76))
			mt76_set(dev, MT_WFDMA1_GLO_CFG + hif1_ofs,
				 MT_WFDMA1_GLO_CFG_TX_DMA_EN |
				 MT_WFDMA1_GLO_CFG_RX_DMA_EN |
				 MT_WFDMA1_GLO_CFG_OMIT_TX_INFO |
				 MT_WFDMA1_GLO_CFG_OMIT_RX_INFO);
	}
}

void mt7915_tx_token_put(struct mt7915_dev *dev)
{
	struct mt76_txwi_cache *txwi;
	int id;

	spin_lock_bh(&dev->mt76.token_lock);
	idr_for_each_entry(&dev->mt76.token, txwi, id) {
		mt7915_txwi_free(dev, txwi, NULL, NULL);
		dev->mt76.token_count--;
	}
	spin_unlock_bh(&dev->mt76.token_lock);
	idr_destroy(&dev->mt76.token);
}

/* system error recovery */
void mt7915_mac_reset_work(struct work_struct *work)
{
	struct mt7915_phy *phy2;
	struct mt76_phy *ext_phy;
	struct mt7915_dev *dev;

	dev = container_of(work, struct mt7915_dev, reset_work);
	ext_phy = dev->mt76.phy2;
	phy2 = ext_phy ? ext_phy->priv : NULL;

	if (!(READ_ONCE(dev->reset_state) & MT_MCU_CMD_STOP_DMA))
		return;

	ieee80211_stop_queues(mt76_hw(dev));
	if (ext_phy)
		ieee80211_stop_queues(ext_phy->hw);

	set_bit(MT76_RESET, &dev->mphy.state);
	set_bit(MT76_MCU_RESET, &dev->mphy.state);
	wake_up(&dev->mt76.mcu.wait);
	cancel_delayed_work_sync(&dev->mphy.mac_work);
	if (phy2) {
		set_bit(MT76_RESET, &phy2->mt76->state);
		cancel_delayed_work_sync(&phy2->mt76->mac_work);
	}
	mt76_worker_disable(&dev->mt76.tx_worker);
	napi_disable(&dev->mt76.napi[0]);
	napi_disable(&dev->mt76.napi[1]);
	napi_disable(&dev->mt76.napi[2]);
	napi_disable(&dev->mt76.tx_napi);

	mutex_lock(&dev->mt76.mutex);

	mt76_wr(dev, MT_MCU_INT_EVENT, MT_MCU_INT_EVENT_DMA_STOPPED);

	if (mt7915_wait_reset_state(dev, MT_MCU_CMD_RESET_DONE)) {
		mt7915_dma_reset(dev);

		mt7915_tx_token_put(dev);
		idr_init(&dev->mt76.token);

		mt76_wr(dev, MT_MCU_INT_EVENT, MT_MCU_INT_EVENT_DMA_INIT);
		mt7915_wait_reset_state(dev, MT_MCU_CMD_RECOVERY_DONE);
	}

	clear_bit(MT76_MCU_RESET, &dev->mphy.state);
	clear_bit(MT76_RESET, &dev->mphy.state);
	if (phy2)
		clear_bit(MT76_RESET, &phy2->mt76->state);

	local_bh_disable();
	napi_enable(&dev->mt76.napi[0]);
	napi_schedule(&dev->mt76.napi[0]);

	napi_enable(&dev->mt76.napi[1]);
	napi_schedule(&dev->mt76.napi[1]);

	napi_enable(&dev->mt76.napi[2]);
	napi_schedule(&dev->mt76.napi[2]);
	local_bh_enable();

	tasklet_schedule(&dev->irq_tasklet);

	mt76_wr(dev, MT_MCU_INT_EVENT, MT_MCU_INT_EVENT_RESET_DONE);
	mt7915_wait_reset_state(dev, MT_MCU_CMD_NORMAL_STATE);

	mt76_worker_enable(&dev->mt76.tx_worker);

	napi_enable(&dev->mt76.tx_napi);
	napi_schedule(&dev->mt76.tx_napi);

	ieee80211_wake_queues(mt76_hw(dev));
	if (ext_phy)
		ieee80211_wake_queues(ext_phy->hw);

	mutex_unlock(&dev->mt76.mutex);

	mt7915_update_beacons(dev);

	ieee80211_queue_delayed_work(mt76_hw(dev), &dev->mphy.mac_work,
				     MT7915_WATCHDOG_TIME);
	if (phy2)
		ieee80211_queue_delayed_work(ext_phy->hw,
					     &phy2->mt76->mac_work,
					     MT7915_WATCHDOG_TIME);
}

void mt7915_mac_update_stats(struct mt7915_phy *phy)
{
	struct mt7915_dev *dev = phy->dev;
	struct mib_stats *mib = &phy->mib;
	int i, aggr0, aggr1, cnt;
	u32 val;

	cnt = mt76_rr(dev, MT_MIB_SDR3(phy->band_idx));
	mib->fcs_err_cnt += is_mt7915(&dev->mt76) ? FIELD_GET(MT_MIB_SDR3_FCS_ERR_MASK, cnt) :
		FIELD_GET(MT_MIB_SDR3_FCS_ERR_MASK_MT7916, cnt);

	cnt = mt76_rr(dev, MT_MIB_SDR4(phy->band_idx));
	mib->rx_fifo_full_cnt += FIELD_GET(MT_MIB_SDR4_RX_FIFO_FULL_MASK, cnt);

	cnt = mt76_rr(dev, MT_MIB_SDR5(phy->band_idx));
	mib->rx_mpdu_cnt += cnt;

	cnt = mt76_rr(dev, MT_MIB_SDR6(phy->band_idx));
	mib->channel_idle_cnt += FIELD_GET(MT_MIB_SDR6_CHANNEL_IDL_CNT_MASK, cnt);

	cnt = mt76_rr(dev, MT_MIB_SDR7(phy->band_idx));
	mib->rx_vector_mismatch_cnt += FIELD_GET(MT_MIB_SDR7_RX_VECTOR_MISMATCH_CNT_MASK, cnt);

	cnt = mt76_rr(dev, MT_MIB_SDR8(phy->band_idx));
	mib->rx_delimiter_fail_cnt += FIELD_GET(MT_MIB_SDR8_RX_DELIMITER_FAIL_CNT_MASK, cnt);

	cnt = mt76_rr(dev, MT_MIB_SDR11(phy->band_idx));
	mib->rx_len_mismatch_cnt += FIELD_GET(MT_MIB_SDR11_RX_LEN_MISMATCH_CNT_MASK, cnt);

	cnt = mt76_rr(dev, MT_MIB_SDR12(phy->band_idx));
	mib->tx_ampdu_cnt += cnt;

	cnt = mt76_rr(dev, MT_MIB_SDR13(phy->band_idx));
	mib->tx_stop_q_empty_cnt += FIELD_GET(MT_MIB_SDR13_TX_STOP_Q_EMPTY_CNT_MASK, cnt);

	cnt = mt76_rr(dev, MT_MIB_SDR14(phy->band_idx));
	mib->tx_mpdu_attempts_cnt += is_mt7915(&dev->mt76) ?
		FIELD_GET(MT_MIB_SDR14_TX_MPDU_ATTEMPTS_CNT_MASK, cnt) :
		FIELD_GET(MT_MIB_SDR14_TX_MPDU_ATTEMPTS_CNT_MASK_MT7916, cnt);

	cnt = mt76_rr(dev, MT_MIB_SDR15(phy->band_idx));
	mib->tx_mpdu_success_cnt += is_mt7915(&dev->mt76) ?
		FIELD_GET(MT_MIB_SDR15_TX_MPDU_SUCCESS_CNT_MASK, cnt) :
		FIELD_GET(MT_MIB_SDR15_TX_MPDU_SUCCESS_CNT_MASK_MT7916, cnt);

	cnt = mt76_rr(dev, MT_MIB_SDR22(phy->band_idx));
	mib->rx_ampdu_cnt += cnt;

	cnt = mt76_rr(dev, MT_MIB_SDR23(phy->band_idx));
	mib->rx_ampdu_bytes_cnt += cnt;

	cnt = mt76_rr(dev, MT_MIB_SDR24(phy->band_idx));
	mib->rx_ampdu_valid_subframe_cnt += is_mt7915(&dev->mt76) ?
		FIELD_GET(MT_MIB_SDR24_RX_AMPDU_SF_CNT_MASK, cnt) :
		FIELD_GET(MT_MIB_SDR24_RX_AMPDU_SF_CNT_MASK_MT7916, cnt);

	cnt = mt76_rr(dev, MT_MIB_SDR25(phy->band_idx));
	mib->rx_ampdu_valid_subframe_bytes_cnt += cnt;

	cnt = mt76_rr(dev, MT_MIB_SDR27(phy->band_idx));
	mib->tx_rwp_fail_cnt += FIELD_GET(MT_MIB_SDR27_TX_RWP_FAIL_CNT_MASK, cnt);

	cnt = mt76_rr(dev, MT_MIB_SDR28(phy->band_idx));
	mib->tx_rwp_need_cnt += FIELD_GET(MT_MIB_SDR28_TX_RWP_NEED_CNT_MASK, cnt);

	cnt = mt76_rr(dev, MT_MIB_SDR29(phy->band_idx));
	mib->rx_pfdrop_cnt += is_mt7915(&dev->mt76) ?
		FIELD_GET(MT_MIB_SDR29_RX_PFDROP_CNT_MASK, cnt) :
		FIELD_GET(MT_MIB_SDR29_RX_PFDROP_CNT_MASK_MT7916, cnt);

	cnt = mt76_rr(dev, MT_MIB_SDRVEC(phy->band_idx));
	mib->rx_vec_queue_overflow_drop_cnt += is_mt7915(&dev->mt76) ?
		FIELD_GET(MT_MIB_SDR30_RX_VEC_QUEUE_OVERFLOW_DROP_CNT_MASK, cnt) :
		FIELD_GET(MT_MIB_SDR30_RX_VEC_QUEUE_OVERFLOW_DROP_CNT_MASK_MT7916, cnt);

	cnt = mt76_rr(dev, MT_MIB_SDR31(phy->band_idx));
	mib->rx_ba_cnt += cnt;

	cnt = mt76_rr(dev, MT_MIB_SDRMUBF(phy->band_idx));
	mib->tx_bf_cnt += FIELD_GET(MT_MIB_MU_BF_TX_CNT, cnt);

	cnt = mt76_rr(dev, MT_MIB_DR8(phy->band_idx));
	mib->tx_mu_mpdu_cnt += cnt;

	cnt = mt76_rr(dev, MT_MIB_DR9(phy->band_idx));
	mib->tx_mu_acked_mpdu_cnt += cnt;

	cnt = mt76_rr(dev, MT_MIB_DR11(phy->band_idx));
	mib->tx_su_acked_mpdu_cnt += cnt;

	cnt = mt76_rr(dev, MT_ETBF_PAR_RPT0(phy->band_idx));
	mib->tx_bf_rx_fb_bw = FIELD_GET(MT_ETBF_PAR_RPT0_FB_BW, cnt);
	mib->tx_bf_rx_fb_nc_cnt += FIELD_GET(MT_ETBF_PAR_RPT0_FB_NC, cnt);
	mib->tx_bf_rx_fb_nr_cnt += FIELD_GET(MT_ETBF_PAR_RPT0_FB_NR, cnt);

	for (i = 0; i < ARRAY_SIZE(mib->tx_amsdu); i++) {
		cnt = mt76_rr(dev, MT_PLE_AMSDU_PACK_MSDU_CNT(i));
		mib->tx_amsdu[i] += cnt;
		mib->tx_amsdu_cnt += cnt;
	}

	aggr0 = phy->band_idx ? ARRAY_SIZE(dev->mt76.aggr_stats) / 2 : 0;
	if (is_mt7915(&dev->mt76)) {
		for (i = 0, aggr1 = aggr0 + 4; i < 4; i++) {
			val = mt76_rr(dev, MT_MIB_MB_SDR1(phy->band_idx, (i << 4)));
			mib->ba_miss_cnt += FIELD_GET(MT_MIB_BA_MISS_COUNT_MASK, val);
			mib->ack_fail_cnt +=
				FIELD_GET(MT_MIB_ACK_FAIL_COUNT_MASK, val);

			val = mt76_rr(dev, MT_MIB_MB_SDR0(phy->band_idx, (i << 4)));
			mib->rts_cnt += FIELD_GET(MT_MIB_RTS_COUNT_MASK, val);
			mib->rts_retries_cnt +=
				FIELD_GET(MT_MIB_RTS_RETRIES_COUNT_MASK, val);

			val = mt76_rr(dev, MT_TX_AGG_CNT(phy->band_idx, i));
			dev->mt76.aggr_stats[aggr0++] += val & 0xffff;
			dev->mt76.aggr_stats[aggr0++] += val >> 16;

			val = mt76_rr(dev, MT_TX_AGG_CNT2(phy->band_idx, i));
			dev->mt76.aggr_stats[aggr1++] += val & 0xffff;
			dev->mt76.aggr_stats[aggr1++] += val >> 16;
		}

		cnt = mt76_rr(dev, MT_MIB_SDR32(phy->band_idx));
		mib->tx_pkt_ebf_cnt += FIELD_GET(MT_MIB_SDR32_TX_PKT_EBF_CNT, cnt);

		cnt = mt76_rr(dev, MT_MIB_SDR33(phy->band_idx));
		mib->tx_pkt_ibf_cnt += FIELD_GET(MT_MIB_SDR33_TX_PKT_IBF_CNT, cnt);

		cnt = mt76_rr(dev, MT_ETBF_TX_APP_CNT(phy->band_idx));
		mib->tx_bf_ibf_ppdu_cnt += FIELD_GET(MT_ETBF_TX_IBF_CNT, cnt);
		mib->tx_bf_ebf_ppdu_cnt += FIELD_GET(MT_ETBF_TX_EBF_CNT, cnt);

		cnt = mt76_rr(dev, MT_ETBF_TX_NDP_BFRP(phy->band_idx));
		mib->tx_bf_fb_cpl_cnt += FIELD_GET(MT_ETBF_TX_FB_CPL, cnt);
		mib->tx_bf_fb_trig_cnt += FIELD_GET(MT_ETBF_TX_FB_TRI, cnt);

		cnt = mt76_rr(dev, MT_ETBF_RX_FB_CNT(phy->band_idx));
		mib->tx_bf_rx_fb_all_cnt += FIELD_GET(MT_ETBF_RX_FB_ALL, cnt);
		mib->tx_bf_rx_fb_he_cnt += FIELD_GET(MT_ETBF_RX_FB_HE, cnt);
		mib->tx_bf_rx_fb_vht_cnt += FIELD_GET(MT_ETBF_RX_FB_VHT, cnt);
		mib->tx_bf_rx_fb_ht_cnt += FIELD_GET(MT_ETBF_RX_FB_HT, cnt);
	} else {
		for (i = 0; i < 2; i++) {
			/* rts count */
			val = mt76_rr(dev, MT_MIB_MB_SDR0(phy->band_idx, (i << 2)));
			mib->rts_cnt += FIELD_GET(GENMASK(15, 0), val);
			mib->rts_cnt += FIELD_GET(GENMASK(31, 16), val);

			/* rts retry count */
			val = mt76_rr(dev, MT_MIB_MB_SDR1(phy->band_idx, (i << 2)));
			mib->rts_retries_cnt += FIELD_GET(GENMASK(15, 0), val);
			mib->rts_retries_cnt += FIELD_GET(GENMASK(31, 16), val);

			/* ba miss count */
			val = mt76_rr(dev, MT_MIB_MB_SDR2(phy->band_idx, (i << 2)));
			mib->ba_miss_cnt += FIELD_GET(GENMASK(15, 0), val);
			mib->ba_miss_cnt += FIELD_GET(GENMASK(31, 16), val);

			/* ack fail count */
			val = mt76_rr(dev, MT_MIB_MB_BFTF(phy->band_idx, (i << 2)));
			mib->ack_fail_cnt += FIELD_GET(GENMASK(15, 0), val);
			mib->ack_fail_cnt += FIELD_GET(GENMASK(31, 16), val);
		}

		for (i = 0; i < 8; i++) {
			val = mt76_rr(dev, MT_TX_AGG_CNT(phy->band_idx, i));
			dev->mt76.aggr_stats[aggr0++] += FIELD_GET(GENMASK(15, 0), val);
			dev->mt76.aggr_stats[aggr0++] += FIELD_GET(GENMASK(31, 16), val);
		}

		cnt = mt76_rr(dev, MT_MIB_SDR32(phy->band_idx));
		mib->tx_pkt_ibf_cnt += FIELD_GET(MT_MIB_SDR32_TX_PKT_IBF_CNT, cnt);
		mib->tx_bf_ibf_ppdu_cnt += FIELD_GET(MT_MIB_SDR32_TX_PKT_IBF_CNT, cnt);
		mib->tx_pkt_ebf_cnt += FIELD_GET(MT_MIB_SDR32_TX_PKT_EBF_CNT, cnt);
		mib->tx_bf_ebf_ppdu_cnt += FIELD_GET(MT_MIB_SDR32_TX_PKT_EBF_CNT, cnt);

		cnt = mt76_rr(dev, MT_MIB_BFCR7(phy->band_idx));
		mib->tx_bf_fb_cpl_cnt += FIELD_GET(MT_MIB_BFCR7_BFEE_TX_FB_CPL, cnt);

		cnt = mt76_rr(dev, MT_MIB_BFCR2(phy->band_idx));
		mib->tx_bf_fb_trig_cnt += FIELD_GET(MT_MIB_BFCR2_BFEE_TX_FB_TRIG, cnt);

		cnt = mt76_rr(dev, MT_MIB_BFCR0(phy->band_idx));
		mib->tx_bf_rx_fb_vht_cnt += FIELD_GET(MT_MIB_BFCR0_RX_FB_VHT, cnt);
		mib->tx_bf_rx_fb_all_cnt += FIELD_GET(MT_MIB_BFCR0_RX_FB_VHT, cnt);
		mib->tx_bf_rx_fb_ht_cnt += FIELD_GET(MT_MIB_BFCR0_RX_FB_HT, cnt);
		mib->tx_bf_rx_fb_all_cnt += FIELD_GET(MT_MIB_BFCR0_RX_FB_HT, cnt);

		cnt = mt76_rr(dev, MT_MIB_BFCR1(phy->band_idx));
		mib->tx_bf_rx_fb_he_cnt += FIELD_GET(MT_MIB_BFCR1_RX_FB_HE, cnt);
		mib->tx_bf_rx_fb_all_cnt += FIELD_GET(MT_MIB_BFCR1_RX_FB_HE, cnt);
	}
}

void mt7915_mac_sta_rc_work(struct work_struct *work)
{
	struct mt7915_dev *dev = container_of(work, struct mt7915_dev, rc_work);
	struct ieee80211_sta *sta;
	struct ieee80211_vif *vif;
	struct mt7915_sta *msta;
	u32 changed;
	LIST_HEAD(list);

	spin_lock_bh(&dev->sta_poll_lock);
	list_splice_init(&dev->sta_rc_list, &list);

	while (!list_empty(&list)) {
		msta = list_first_entry(&list, struct mt7915_sta, rc_list);
		list_del_init(&msta->rc_list);
		changed = msta->changed;
		msta->changed = 0;
		spin_unlock_bh(&dev->sta_poll_lock);

		sta = container_of((void *)msta, struct ieee80211_sta, drv_priv);
		vif = container_of((void *)msta->vif, struct ieee80211_vif, drv_priv);

		if (changed & (IEEE80211_RC_SUPP_RATES_CHANGED |
			       IEEE80211_RC_NSS_CHANGED |
			       IEEE80211_RC_BW_CHANGED))
			mt7915_mcu_add_rate_ctrl(dev, vif, sta, true);

		if (changed & IEEE80211_RC_SMPS_CHANGED)
			mt7915_mcu_add_smps(dev, vif, sta);

		spin_lock_bh(&dev->sta_poll_lock);
	}

	spin_unlock_bh(&dev->sta_poll_lock);
}

void mt7915_mac_work(struct work_struct *work)
{
	struct mt7915_phy *phy;
	struct mt76_phy *mphy;

	mphy = (struct mt76_phy *)container_of(work, struct mt76_phy,
					       mac_work.work);
	phy = mphy->priv;

	mutex_lock(&mphy->dev->mutex);

	mt76_update_survey(mphy);
	if (++mphy->mac_work_count == 5) {
		mphy->mac_work_count = 0;

		mt7915_mac_update_stats(phy);
	}

	mutex_unlock(&mphy->dev->mutex);

	mt76_tx_status_check(mphy->dev, false);

	ieee80211_queue_delayed_work(mphy->hw, &mphy->mac_work,
				     MT7915_WATCHDOG_TIME);
}

static void mt7915_dfs_stop_radar_detector(struct mt7915_phy *phy)
{
	struct mt7915_dev *dev = phy->dev;

	if (phy->rdd_state & BIT(0))
		mt76_connac_mcu_rdd_cmd(&dev->mt76, RDD_STOP, 0,
					MT_RX_SEL0, 0);
	if (phy->rdd_state & BIT(1))
		mt76_connac_mcu_rdd_cmd(&dev->mt76, RDD_STOP, 1,
					MT_RX_SEL0, 0);
}

static int mt7915_dfs_start_rdd(struct mt7915_dev *dev, int chain)
{
	int err, region;

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

	err = mt76_connac_mcu_rdd_cmd(&dev->mt76, RDD_START, chain,
				      MT_RX_SEL0, region);
	if (err < 0)
		return err;

	return mt76_connac_mcu_rdd_cmd(&dev->mt76, RDD_DET_MODE, chain,
				       MT_RX_SEL0, 1);
}

static int mt7915_dfs_start_radar_detector(struct mt7915_phy *phy)
{
	struct cfg80211_chan_def *chandef = &phy->mt76->chandef;
	struct mt7915_dev *dev = phy->dev;
	int err;

	/* start CAC */
	err = mt76_connac_mcu_rdd_cmd(&dev->mt76, RDD_CAC_START, phy->band_idx,
				      MT_RX_SEL0, 0);
	if (err < 0)
		return err;

	err = mt7915_dfs_start_rdd(dev, phy->band_idx);
	if (err < 0)
		return err;

	phy->rdd_state |= BIT(phy->band_idx);

	if (!is_mt7915(&dev->mt76))
		return 0;

	if (chandef->width == NL80211_CHAN_WIDTH_160 ||
	    chandef->width == NL80211_CHAN_WIDTH_80P80) {
		err = mt7915_dfs_start_rdd(dev, 1);
		if (err < 0)
			return err;

		phy->rdd_state |= BIT(1);
	}

	return 0;
}

static int
mt7915_dfs_init_radar_specs(struct mt7915_phy *phy)
{
	const struct mt7915_dfs_radar_spec *radar_specs;
	struct mt7915_dev *dev = phy->dev;
	int err, i;

	switch (dev->mt76.region) {
	case NL80211_DFS_FCC:
		radar_specs = &fcc_radar_specs;
		err = mt7915_mcu_set_fcc5_lpn(dev, 8);
		if (err < 0)
			return err;
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

	for (i = 0; i < ARRAY_SIZE(radar_specs->radar_pattern); i++) {
		err = mt7915_mcu_set_radar_th(dev, i,
					      &radar_specs->radar_pattern[i]);
		if (err < 0)
			return err;
	}

	return mt7915_mcu_set_pulse_th(dev, &radar_specs->pulse_th);
}

int mt7915_dfs_init_radar_detector(struct mt7915_phy *phy)
{
	struct mt7915_dev *dev = phy->dev;
	enum mt76_dfs_state dfs_state, prev_state;
	int err;

	prev_state = phy->mt76->dfs_state;
	dfs_state = mt76_phy_dfs_state(phy->mt76);

	if (prev_state == dfs_state)
		return 0;

	if (prev_state == MT_DFS_STATE_UNKNOWN)
		mt7915_dfs_stop_radar_detector(phy);

	if (dfs_state == MT_DFS_STATE_DISABLED)
		goto stop;

	if (prev_state <= MT_DFS_STATE_DISABLED) {
		err = mt7915_dfs_init_radar_specs(phy);
		if (err < 0)
			return err;

		err = mt7915_dfs_start_radar_detector(phy);
		if (err < 0)
			return err;

		phy->mt76->dfs_state = MT_DFS_STATE_CAC;
	}

	if (dfs_state == MT_DFS_STATE_CAC)
		return 0;

	err = mt76_connac_mcu_rdd_cmd(&dev->mt76, RDD_CAC_END,
				      phy->band_idx, MT_RX_SEL0, 0);
	if (err < 0) {
		phy->mt76->dfs_state = MT_DFS_STATE_UNKNOWN;
		return err;
	}

	phy->mt76->dfs_state = MT_DFS_STATE_ACTIVE;
	return 0;

stop:
	err = mt76_connac_mcu_rdd_cmd(&dev->mt76, RDD_NORMAL_START,
				      phy->band_idx, MT_RX_SEL0, 0);
	if (err < 0)
		return err;

	mt7915_dfs_stop_radar_detector(phy);
	phy->mt76->dfs_state = MT_DFS_STATE_DISABLED;

	return 0;
}

static int
mt7915_mac_twt_duration_align(int duration)
{
	return duration << 8;
}

static u64
mt7915_mac_twt_sched_list_add(struct mt7915_dev *dev,
			      struct mt7915_twt_flow *flow)
{
	struct mt7915_twt_flow *iter, *iter_next;
	u32 duration = flow->duration << 8;
	u64 start_tsf;

	iter = list_first_entry_or_null(&dev->twt_list,
					struct mt7915_twt_flow, list);
	if (!iter || !iter->sched || iter->start_tsf > duration) {
		/* add flow as first entry in the list */
		list_add(&flow->list, &dev->twt_list);
		return 0;
	}

	list_for_each_entry_safe(iter, iter_next, &dev->twt_list, list) {
		start_tsf = iter->start_tsf +
			    mt7915_mac_twt_duration_align(iter->duration);
		if (list_is_last(&iter->list, &dev->twt_list))
			break;

		if (!iter_next->sched ||
		    iter_next->start_tsf > start_tsf + duration) {
			list_add(&flow->list, &iter->list);
			goto out;
		}
	}

	/* add flow as last entry in the list */
	list_add_tail(&flow->list, &dev->twt_list);
out:
	return start_tsf;
}

static int mt7915_mac_check_twt_req(struct ieee80211_twt_setup *twt)
{
	struct ieee80211_twt_params *twt_agrt;
	u64 interval, duration;
	u16 mantissa;
	u8 exp;

	/* only individual agreement supported */
	if (twt->control & IEEE80211_TWT_CONTROL_NEG_TYPE_BROADCAST)
		return -EOPNOTSUPP;

	/* only 256us unit supported */
	if (twt->control & IEEE80211_TWT_CONTROL_WAKE_DUR_UNIT)
		return -EOPNOTSUPP;

	twt_agrt = (struct ieee80211_twt_params *)twt->params;

	/* explicit agreement not supported */
	if (!(twt_agrt->req_type & cpu_to_le16(IEEE80211_TWT_REQTYPE_IMPLICIT)))
		return -EOPNOTSUPP;

	exp = FIELD_GET(IEEE80211_TWT_REQTYPE_WAKE_INT_EXP,
			le16_to_cpu(twt_agrt->req_type));
	mantissa = le16_to_cpu(twt_agrt->mantissa);
	duration = twt_agrt->min_twt_dur << 8;

	interval = (u64)mantissa << exp;
	if (interval < duration)
		return -EOPNOTSUPP;

	return 0;
}

void mt7915_mac_add_twt_setup(struct ieee80211_hw *hw,
			      struct ieee80211_sta *sta,
			      struct ieee80211_twt_setup *twt)
{
	enum ieee80211_twt_setup_cmd setup_cmd = TWT_SETUP_CMD_REJECT;
	struct mt7915_sta *msta = (struct mt7915_sta *)sta->drv_priv;
	struct ieee80211_twt_params *twt_agrt = (void *)twt->params;
	u16 req_type = le16_to_cpu(twt_agrt->req_type);
	enum ieee80211_twt_setup_cmd sta_setup_cmd;
	struct mt7915_dev *dev = mt7915_hw_dev(hw);
	struct mt7915_twt_flow *flow;
	int flowid, table_id;
	u8 exp;

	if (mt7915_mac_check_twt_req(twt))
		goto out;

	mutex_lock(&dev->mt76.mutex);

	if (dev->twt.n_agrt == MT7915_MAX_TWT_AGRT)
		goto unlock;

	if (hweight8(msta->twt.flowid_mask) == ARRAY_SIZE(msta->twt.flow))
		goto unlock;

	flowid = ffs(~msta->twt.flowid_mask) - 1;
	le16p_replace_bits(&twt_agrt->req_type, flowid,
			   IEEE80211_TWT_REQTYPE_FLOWID);

	table_id = ffs(~dev->twt.table_mask) - 1;
	exp = FIELD_GET(IEEE80211_TWT_REQTYPE_WAKE_INT_EXP, req_type);
	sta_setup_cmd = FIELD_GET(IEEE80211_TWT_REQTYPE_SETUP_CMD, req_type);

	flow = &msta->twt.flow[flowid];
	memset(flow, 0, sizeof(*flow));
	INIT_LIST_HEAD(&flow->list);
	flow->wcid = msta->wcid.idx;
	flow->table_id = table_id;
	flow->id = flowid;
	flow->duration = twt_agrt->min_twt_dur;
	flow->mantissa = twt_agrt->mantissa;
	flow->exp = exp;
	flow->protection = !!(req_type & IEEE80211_TWT_REQTYPE_PROTECTION);
	flow->flowtype = !!(req_type & IEEE80211_TWT_REQTYPE_FLOWTYPE);
	flow->trigger = !!(req_type & IEEE80211_TWT_REQTYPE_TRIGGER);

	if (sta_setup_cmd == TWT_SETUP_CMD_REQUEST ||
	    sta_setup_cmd == TWT_SETUP_CMD_SUGGEST) {
		u64 interval = (u64)le16_to_cpu(twt_agrt->mantissa) << exp;
		u64 flow_tsf, curr_tsf;
		u32 rem;

		flow->sched = true;
		flow->start_tsf = mt7915_mac_twt_sched_list_add(dev, flow);
		curr_tsf = __mt7915_get_tsf(hw, msta->vif);
		div_u64_rem(curr_tsf - flow->start_tsf, interval, &rem);
		flow_tsf = curr_tsf + interval - rem;
		twt_agrt->twt = cpu_to_le64(flow_tsf);
	} else {
		list_add_tail(&flow->list, &dev->twt_list);
	}
	flow->tsf = le64_to_cpu(twt_agrt->twt);

	if (mt7915_mcu_twt_agrt_update(dev, msta->vif, flow, MCU_TWT_AGRT_ADD))
		goto unlock;

	setup_cmd = TWT_SETUP_CMD_ACCEPT;
	dev->twt.table_mask |= BIT(table_id);
	msta->twt.flowid_mask |= BIT(flowid);
	dev->twt.n_agrt++;

unlock:
	mutex_unlock(&dev->mt76.mutex);
out:
	le16p_replace_bits(&twt_agrt->req_type, setup_cmd,
			   IEEE80211_TWT_REQTYPE_SETUP_CMD);
	twt->control = (twt->control & IEEE80211_TWT_CONTROL_WAKE_DUR_UNIT) |
		       (twt->control & IEEE80211_TWT_CONTROL_RX_DISABLED);
}

void mt7915_mac_twt_teardown_flow(struct mt7915_dev *dev,
				  struct mt7915_sta *msta,
				  u8 flowid)
{
	struct mt7915_twt_flow *flow;

	lockdep_assert_held(&dev->mt76.mutex);

	if (flowid >= ARRAY_SIZE(msta->twt.flow))
		return;

	if (!(msta->twt.flowid_mask & BIT(flowid)))
		return;

	flow = &msta->twt.flow[flowid];
	if (mt7915_mcu_twt_agrt_update(dev, msta->vif, flow,
				       MCU_TWT_AGRT_DELETE))
		return;

	list_del_init(&flow->list);
	msta->twt.flowid_mask &= ~BIT(flowid);
	dev->twt.table_mask &= ~BIT(flow->table_id);
	dev->twt.n_agrt--;
}
