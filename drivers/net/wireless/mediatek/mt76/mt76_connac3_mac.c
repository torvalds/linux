// SPDX-License-Identifier: BSD-3-Clause-Clear
/* Copyright (C) 2023 MediaTek Inc. */

#include "mt76_connac.h"
#include "mt76_connac3_mac.h"
#include "dma.h"

#define HE_BITS(f)		cpu_to_le16(IEEE80211_RADIOTAP_HE_##f)
#define EHT_BITS(f)		cpu_to_le32(IEEE80211_RADIOTAP_EHT_##f)
#define HE_PREP(f, m, v)	le16_encode_bits(le32_get_bits(v, MT_CRXV_HE_##m),\
						 IEEE80211_RADIOTAP_HE_##f)
#define EHT_PREP(f, m, v)	le32_encode_bits(le32_get_bits(v, MT_CRXV_EHT_##m),\
						 IEEE80211_RADIOTAP_EHT_##f)

static void
mt76_connac3_mac_decode_he_radiotap_ru(struct mt76_rx_status *status,
				       struct ieee80211_radiotap_he *he,
				       __le32 *rxv)
{
	u32 ru = le32_get_bits(rxv[0], MT_PRXV_HE_RU_ALLOC), offs = 0;

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

#define MU_PREP(f, v)	le16_encode_bits(v, IEEE80211_RADIOTAP_HE_MU_##f)
static void
mt76_connac3_mac_decode_he_mu_radiotap(struct sk_buff *skb, __le32 *rxv)
{
	struct mt76_rx_status *status = (struct mt76_rx_status *)skb->cb;
	static const struct ieee80211_radiotap_he_mu mu_known = {
		.flags1 = HE_BITS(MU_FLAGS1_SIG_B_MCS_KNOWN) |
			  HE_BITS(MU_FLAGS1_SIG_B_DCM_KNOWN) |
			  HE_BITS(MU_FLAGS1_CH1_RU_KNOWN) |
			  HE_BITS(MU_FLAGS1_SIG_B_SYMS_USERS_KNOWN),
		.flags2 = HE_BITS(MU_FLAGS2_BW_FROM_SIG_A_BW_KNOWN),
	};
	struct ieee80211_radiotap_he_mu *he_mu;

	status->flag |= RX_FLAG_RADIOTAP_HE_MU;

	he_mu = skb_push(skb, sizeof(mu_known));
	memcpy(he_mu, &mu_known, sizeof(mu_known));

	he_mu->flags1 |= MU_PREP(FLAGS1_SIG_B_MCS, status->rate_idx);
	if (status->he_dcm)
		he_mu->flags1 |= MU_PREP(FLAGS1_SIG_B_DCM, status->he_dcm);

	he_mu->flags2 |= MU_PREP(FLAGS2_BW_FROM_SIG_A_BW, status->bw) |
			 MU_PREP(FLAGS2_SIG_B_SYMS_USERS,
				 le32_get_bits(rxv[4], MT_CRXV_HE_NUM_USER));

	he_mu->ru_ch1[0] = le32_get_bits(rxv[16], MT_CRXV_HE_RU0) & 0xff;

	if (status->bw >= RATE_INFO_BW_40) {
		he_mu->flags1 |= HE_BITS(MU_FLAGS1_CH2_RU_KNOWN);
		he_mu->ru_ch2[0] = le32_get_bits(rxv[16], MT_CRXV_HE_RU1) & 0xff;
	}

	if (status->bw >= RATE_INFO_BW_80) {
		u32 ru_h, ru_l;

		he_mu->ru_ch1[1] = le32_get_bits(rxv[16], MT_CRXV_HE_RU2) & 0xff;

		ru_l = le32_get_bits(rxv[16], MT_CRXV_HE_RU3_L);
		ru_h = le32_get_bits(rxv[17], MT_CRXV_HE_RU3_H) & 0x7;
		he_mu->ru_ch2[1] = (u8)(ru_l | ru_h << 4);
	}
}

void mt76_connac3_mac_decode_he_radiotap(struct sk_buff *skb, __le32 *rxv,
					 u8 mode)
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
	u32 ltf_size = le32_get_bits(rxv[4], MT_CRXV_HE_LTF_SIZE) + 1;
	struct ieee80211_radiotap_he *he;

	status->flag |= RX_FLAG_RADIOTAP_HE;

	he = skb_push(skb, sizeof(known));
	memcpy(he, &known, sizeof(known));

	he->data3 = HE_PREP(DATA3_BSS_COLOR, BSS_COLOR, rxv[9]) |
		    HE_PREP(DATA3_LDPC_XSYMSEG, LDPC_EXT_SYM, rxv[4]);
	he->data4 = HE_PREP(DATA4_SU_MU_SPTL_REUSE, SR_MASK, rxv[13]);
	he->data5 = HE_PREP(DATA5_PE_DISAMBIG, PE_DISAMBIG, rxv[5]) |
		    le16_encode_bits(ltf_size,
				     IEEE80211_RADIOTAP_HE_DATA5_LTF_SIZE);
	if (le32_to_cpu(rxv[0]) & MT_PRXV_TXBF)
		he->data5 |= HE_BITS(DATA5_TXBF);
	he->data6 = HE_PREP(DATA6_TXOP, TXOP_DUR, rxv[9]) |
		    HE_PREP(DATA6_DOPPLER, DOPPLER, rxv[9]);

	switch (mode) {
	case MT_PHY_TYPE_HE_SU:
		he->data1 |= HE_BITS(DATA1_FORMAT_SU) |
			     HE_BITS(DATA1_UL_DL_KNOWN) |
			     HE_BITS(DATA1_BEAM_CHANGE_KNOWN) |
			     HE_BITS(DATA1_BW_RU_ALLOC_KNOWN);

		he->data3 |= HE_PREP(DATA3_BEAM_CHANGE, BEAM_CHNG, rxv[8]) |
			     HE_PREP(DATA3_UL_DL, UPLINK, rxv[5]);
		break;
	case MT_PHY_TYPE_HE_EXT_SU:
		he->data1 |= HE_BITS(DATA1_FORMAT_EXT_SU) |
			     HE_BITS(DATA1_UL_DL_KNOWN) |
			     HE_BITS(DATA1_BW_RU_ALLOC_KNOWN);

		he->data3 |= HE_PREP(DATA3_UL_DL, UPLINK, rxv[5]);
		break;
	case MT_PHY_TYPE_HE_MU:
		he->data1 |= HE_BITS(DATA1_FORMAT_MU) |
			     HE_BITS(DATA1_UL_DL_KNOWN);

		he->data3 |= HE_PREP(DATA3_UL_DL, UPLINK, rxv[5]);
		he->data4 |= HE_PREP(DATA4_MU_STA_ID, MU_AID, rxv[8]);

		mt76_connac3_mac_decode_he_radiotap_ru(status, he, rxv);
		mt76_connac3_mac_decode_he_mu_radiotap(skb, rxv);
		break;
	case MT_PHY_TYPE_HE_TB:
		he->data1 |= HE_BITS(DATA1_FORMAT_TRIG) |
			     HE_BITS(DATA1_SPTL_REUSE2_KNOWN) |
			     HE_BITS(DATA1_SPTL_REUSE3_KNOWN) |
			     HE_BITS(DATA1_SPTL_REUSE4_KNOWN);

		he->data4 |= HE_PREP(DATA4_TB_SPTL_REUSE1, SR_MASK, rxv[13]) |
			     HE_PREP(DATA4_TB_SPTL_REUSE2, SR1_MASK, rxv[13]) |
			     HE_PREP(DATA4_TB_SPTL_REUSE3, SR2_MASK, rxv[13]) |
			     HE_PREP(DATA4_TB_SPTL_REUSE4, SR3_MASK, rxv[13]);

		mt76_connac3_mac_decode_he_radiotap_ru(status, he, rxv);
		break;
	default:
		break;
	}
}
EXPORT_SYMBOL_GPL(mt76_connac3_mac_decode_he_radiotap);

static void *
mt76_connac3_mac_radiotap_push_tlv(struct sk_buff *skb, u16 type, u16 len)
{
	struct ieee80211_radiotap_tlv *tlv;

	tlv = skb_push(skb, sizeof(*tlv) + len);
	tlv->type = cpu_to_le16(type);
	tlv->len = cpu_to_le16(len);
	memset(tlv->data, 0, len);

	return tlv->data;
}

void mt76_connac3_mac_decode_eht_radiotap(struct sk_buff *skb, __le32 *rxv,
					  u8 mode)
{
	struct mt76_rx_status *status = (struct mt76_rx_status *)skb->cb;
	struct ieee80211_radiotap_eht_usig *usig;
	struct ieee80211_radiotap_eht *eht;
	u32 ltf_size = le32_get_bits(rxv[4], MT_CRXV_HE_LTF_SIZE) + 1;
	u8 bw = FIELD_GET(MT_PRXV_FRAME_MODE, le32_to_cpu(rxv[2]));

	if (WARN_ONCE(skb_mac_header(skb) != skb->data,
		      "Should push tlv at the top of mac hdr"))
		return;

	eht = mt76_connac3_mac_radiotap_push_tlv(skb, IEEE80211_RADIOTAP_EHT,
						 sizeof(*eht) + sizeof(u32));
	usig = mt76_connac3_mac_radiotap_push_tlv(skb, IEEE80211_RADIOTAP_EHT_USIG,
						  sizeof(*usig));

	status->flag |= RX_FLAG_RADIOTAP_TLV_AT_END;

	eht->known |= EHT_BITS(KNOWN_SPATIAL_REUSE) |
		      EHT_BITS(KNOWN_GI) |
		      EHT_BITS(KNOWN_EHT_LTF) |
		      EHT_BITS(KNOWN_LDPC_EXTRA_SYM_OM) |
		      EHT_BITS(KNOWN_PE_DISAMBIGUITY_OM) |
		      EHT_BITS(KNOWN_NSS_S);

	eht->data[0] |=
		EHT_PREP(DATA0_SPATIAL_REUSE, SR_MASK, rxv[13]) |
		cpu_to_le32(FIELD_PREP(IEEE80211_RADIOTAP_EHT_DATA0_GI, status->eht.gi) |
			    FIELD_PREP(IEEE80211_RADIOTAP_EHT_DATA0_LTF, ltf_size)) |
		EHT_PREP(DATA0_PE_DISAMBIGUITY_OM, PE_DISAMBIG, rxv[5]) |
		EHT_PREP(DATA0_LDPC_EXTRA_SYM_OM, LDPC_EXT_SYM, rxv[4]);

	/* iwlwifi and wireshark expect radiotap to report zero-based NSS, so subtract 1. */
	eht->data[7] |= le32_encode_bits(status->nss - 1, IEEE80211_RADIOTAP_EHT_DATA7_NSS_S);

	eht->user_info[0] |=
		EHT_BITS(USER_INFO_MCS_KNOWN) |
		EHT_BITS(USER_INFO_CODING_KNOWN) |
		EHT_BITS(USER_INFO_NSS_KNOWN_O) |
		EHT_BITS(USER_INFO_BEAMFORMING_KNOWN_O) |
		EHT_BITS(USER_INFO_DATA_FOR_USER) |
		le32_encode_bits(status->rate_idx, IEEE80211_RADIOTAP_EHT_USER_INFO_MCS) |
		le32_encode_bits(status->nss - 1, IEEE80211_RADIOTAP_EHT_USER_INFO_NSS_O);

	if (le32_to_cpu(rxv[0]) & MT_PRXV_TXBF)
		eht->user_info[0] |= EHT_BITS(USER_INFO_BEAMFORMING_O);

	if (le32_to_cpu(rxv[0]) & MT_PRXV_HT_AD_CODE)
		eht->user_info[0] |= EHT_BITS(USER_INFO_CODING);

	if (mode == MT_PHY_TYPE_EHT_MU)
		eht->user_info[0] |= EHT_BITS(USER_INFO_STA_ID_KNOWN) |
				     EHT_PREP(USER_INFO_STA_ID, MU_AID, rxv[8]);

	usig->common |=
		EHT_BITS(USIG_COMMON_PHY_VER_KNOWN) |
		EHT_BITS(USIG_COMMON_BW_KNOWN) |
		EHT_BITS(USIG_COMMON_UL_DL_KNOWN) |
		EHT_BITS(USIG_COMMON_BSS_COLOR_KNOWN) |
		EHT_BITS(USIG_COMMON_TXOP_KNOWN) |
		le32_encode_bits(0, IEEE80211_RADIOTAP_EHT_USIG_COMMON_PHY_VER) |
		le32_encode_bits(bw, IEEE80211_RADIOTAP_EHT_USIG_COMMON_BW) |
		EHT_PREP(USIG_COMMON_UL_DL, UPLINK, rxv[5]) |
		EHT_PREP(USIG_COMMON_BSS_COLOR, BSS_COLOR, rxv[9]) |
		EHT_PREP(USIG_COMMON_TXOP, TXOP_DUR, rxv[9]);
}
EXPORT_SYMBOL_GPL(mt76_connac3_mac_decode_eht_radiotap);
