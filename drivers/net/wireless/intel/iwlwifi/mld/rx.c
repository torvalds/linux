// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2024-2025 Intel Corporation
 */

#include <net/mac80211.h>
#include <kunit/static_stub.h>

#include "mld.h"
#include "sta.h"
#include "agg.h"
#include "rx.h"
#include "hcmd.h"
#include "iface.h"
#include "time_sync.h"
#include "fw/dbg.h"
#include "fw/api/rx.h"

/* stores relevant PHY data fields extracted from iwl_rx_mpdu_desc */
struct iwl_mld_rx_phy_data {
	struct iwl_rx_phy_air_sniffer_ntfy *ntfy;
	bool first_subframe;
	bool with_data;
	u32 rate_n_flags;
	u32 gp2_on_air_rise;
	/* phy_info is only valid when we have a frame, i.e. with_data=true */
	u16 phy_info;
	u8 energy_a, energy_b;
};

static void
iwl_mld_fill_phy_data_from_mpdu(struct iwl_mld *mld,
				struct iwl_rx_mpdu_desc *desc,
				struct iwl_mld_rx_phy_data *phy_data)
{
	if (unlikely(mld->monitor.phy.valid)) {
		mld->monitor.phy.used = true;
		phy_data->ntfy = &mld->monitor.phy.data;
	}

	phy_data->phy_info = le16_to_cpu(desc->phy_info);
	phy_data->rate_n_flags = iwl_v3_rate_from_v2_v3(desc->v3.rate_n_flags,
							mld->fw_rates_ver_3);
	phy_data->gp2_on_air_rise = le32_to_cpu(desc->v3.gp2_on_air_rise);
	phy_data->energy_a = desc->v3.energy_a;
	phy_data->energy_b = desc->v3.energy_b;
	phy_data->with_data = true;
}

static inline int iwl_mld_check_pn(struct iwl_mld *mld, struct sk_buff *skb,
				   int queue, struct ieee80211_sta *sta)
{
	struct ieee80211_hdr *hdr = (void *)skb_mac_header(skb);
	struct ieee80211_rx_status *stats = IEEE80211_SKB_RXCB(skb);
	struct iwl_mld_sta *mld_sta;
	struct iwl_mld_ptk_pn *ptk_pn;
	int res;
	u8 tid, keyidx;
	u8 pn[IEEE80211_CCMP_PN_LEN];
	u8 *extiv;

	/* multicast and non-data only arrives on default queue; avoid checking
	 * for default queue - we don't want to replicate all the logic that's
	 * necessary for checking the PN on fragmented frames, leave that
	 * to mac80211
	 */
	if (queue == 0 || !ieee80211_is_data(hdr->frame_control) ||
	    is_multicast_ether_addr(hdr->addr1))
		return 0;

	if (!(stats->flag & RX_FLAG_DECRYPTED))
		return 0;

	/* if we are here - this for sure is either CCMP or GCMP */
	if (!sta) {
		IWL_DEBUG_DROP(mld,
			       "expected hw-decrypted unicast frame for station\n");
		return -1;
	}

	mld_sta = iwl_mld_sta_from_mac80211(sta);

	extiv = (u8 *)hdr + ieee80211_hdrlen(hdr->frame_control);
	keyidx = extiv[3] >> 6;

	ptk_pn = rcu_dereference(mld_sta->ptk_pn[keyidx]);
	if (!ptk_pn)
		return -1;

	if (ieee80211_is_data_qos(hdr->frame_control))
		tid = ieee80211_get_tid(hdr);
	else
		tid = 0;

	/* we don't use HCCA/802.11 QoS TSPECs, so drop such frames */
	if (tid >= IWL_MAX_TID_COUNT)
		return -1;

	/* load pn */
	pn[0] = extiv[7];
	pn[1] = extiv[6];
	pn[2] = extiv[5];
	pn[3] = extiv[4];
	pn[4] = extiv[1];
	pn[5] = extiv[0];

	res = memcmp(pn, ptk_pn->q[queue].pn[tid], IEEE80211_CCMP_PN_LEN);
	if (res < 0)
		return -1;
	if (!res && !(stats->flag & RX_FLAG_ALLOW_SAME_PN))
		return -1;

	memcpy(ptk_pn->q[queue].pn[tid], pn, IEEE80211_CCMP_PN_LEN);
	stats->flag |= RX_FLAG_PN_VALIDATED;

	return 0;
}

/* iwl_mld_pass_packet_to_mac80211 - passes the packet for mac80211 */
void iwl_mld_pass_packet_to_mac80211(struct iwl_mld *mld,
				     struct napi_struct *napi,
				     struct sk_buff *skb, int queue,
				     struct ieee80211_sta *sta)
{
	KUNIT_STATIC_STUB_REDIRECT(iwl_mld_pass_packet_to_mac80211,
				   mld, napi, skb, queue, sta);

	if (unlikely(iwl_mld_check_pn(mld, skb, queue, sta))) {
		kfree_skb(skb);
		return;
	}

	ieee80211_rx_napi(mld->hw, sta, skb, napi);
}
EXPORT_SYMBOL_IF_IWLWIFI_KUNIT(iwl_mld_pass_packet_to_mac80211);

static bool iwl_mld_used_average_energy(struct iwl_mld *mld, int link_id,
					struct ieee80211_hdr *hdr,
					struct ieee80211_rx_status *rx_status)
{
	struct ieee80211_bss_conf *link_conf;
	struct iwl_mld_link *mld_link;

	if (unlikely(!hdr || link_id < 0))
		return false;

	if (likely(!ieee80211_is_beacon(hdr->frame_control)))
		return false;

	/*
	 * if link ID is >= valid ones then that means the RX
	 * was on the AUX link and no correction is needed
	 */
	if (link_id >= mld->fw->ucode_capa.num_links)
		return false;

	/* for the link conf lookup */
	guard(rcu)();

	link_conf = rcu_dereference(mld->fw_id_to_bss_conf[link_id]);
	if (!link_conf)
		return false;

	mld_link = iwl_mld_link_from_mac80211(link_conf);
	if (!mld_link)
		return false;

	/*
	 * If we know the link by link ID then the frame was
	 * received for the link, so by filtering it means it
	 * was from the AP the link is connected to.
	 */

	/* skip also in case we don't have it (yet) */
	if (!mld_link->average_beacon_energy)
		return false;

	IWL_DEBUG_STATS(mld, "energy override by average %d\n",
			mld_link->average_beacon_energy);
	rx_status->signal = -mld_link->average_beacon_energy;
	return true;
}

static void iwl_mld_fill_signal(struct iwl_mld *mld, int link_id,
				struct ieee80211_hdr *hdr,
				struct ieee80211_rx_status *rx_status,
				struct iwl_mld_rx_phy_data *phy_data)
{
	u32 rate_n_flags = phy_data->rate_n_flags;
	int energy_a = phy_data->energy_a;
	int energy_b = phy_data->energy_b;
	int max_energy;

	energy_a = energy_a ? -energy_a : S8_MIN;
	energy_b = energy_b ? -energy_b : S8_MIN;
	max_energy = max(energy_a, energy_b);

	IWL_DEBUG_STATS(mld, "energy in A %d B %d, and max %d\n",
			energy_a, energy_b, max_energy);

	if (iwl_mld_used_average_energy(mld, link_id, hdr, rx_status))
		return;

	rx_status->signal = max_energy;
	rx_status->chains = u32_get_bits(rate_n_flags, RATE_MCS_ANT_AB_MSK);
	rx_status->chain_signal[0] = energy_a;
	rx_status->chain_signal[1] = energy_b;
}

static void
iwl_mld_he_set_ru_alloc(struct ieee80211_rx_status *rx_status,
			struct ieee80211_radiotap_he *he,
			u8 ru_with_p80)
{
	u8 ru = ru_with_p80 >> 1;
	u8 p80 = ru_with_p80 & 1;
	u8 offs = 0;

	rx_status->bw = RATE_INFO_BW_HE_RU;

	he->data1 |= cpu_to_le16(IEEE80211_RADIOTAP_HE_DATA1_BW_RU_ALLOC_KNOWN);
	he->data2 |= cpu_to_le16(IEEE80211_RADIOTAP_HE_DATA2_PRISEC_80_KNOWN |
				 IEEE80211_RADIOTAP_HE_DATA2_RU_OFFSET_KNOWN);

	switch (ru) {
	case 0 ... 36:
		rx_status->he_ru = NL80211_RATE_INFO_HE_RU_ALLOC_26;
		offs = ru;
		break;
	case 37 ... 52:
		rx_status->he_ru = NL80211_RATE_INFO_HE_RU_ALLOC_52;
		offs = ru - 37;
		break;
	case 53 ... 60:
		rx_status->he_ru = NL80211_RATE_INFO_HE_RU_ALLOC_106;
		offs = ru - 53;
		break;
	case 61 ... 64:
		rx_status->he_ru = NL80211_RATE_INFO_HE_RU_ALLOC_242;
		offs = ru - 61;
		break;
	case 65 ... 66:
		rx_status->he_ru = NL80211_RATE_INFO_HE_RU_ALLOC_484;
		offs = ru - 65;
		break;
	case 67:
		rx_status->he_ru = NL80211_RATE_INFO_HE_RU_ALLOC_996;
		break;
	case 68:
		rx_status->he_ru = NL80211_RATE_INFO_HE_RU_ALLOC_2x996;
		break;
	}

	he->data2 |= le16_encode_bits(offs,
				      IEEE80211_RADIOTAP_HE_DATA2_RU_OFFSET);

	he->data2 |= le16_encode_bits(p80, IEEE80211_RADIOTAP_HE_DATA2_PRISEC_80_SEC);
}

#define RTAP_ENC_HE(src, src_msk, dst_msk)			\
	le16_encode_bits(le32_get_bits(src, src_msk), dst_msk)

static void
iwl_mld_decode_he_mu(struct iwl_mld_rx_phy_data *phy_data,
		     struct ieee80211_radiotap_he *he,
		     struct ieee80211_radiotap_he_mu *he_mu,
		     struct ieee80211_rx_status *rx_status)
{
	u32 rate_n_flags = phy_data->rate_n_flags;

	he_mu->flags1 |= RTAP_ENC_HE(phy_data->ntfy->sigs.he.b,
				     OFDM_RX_FRAME_HE_SIGB_DCM,
				     IEEE80211_RADIOTAP_HE_MU_FLAGS1_SIG_B_DCM);
	he_mu->flags1 |= RTAP_ENC_HE(phy_data->ntfy->sigs.he.b,
				     OFDM_RX_FRAME_HE_SIGB_MCS,
				     IEEE80211_RADIOTAP_HE_MU_FLAGS1_SIG_B_MCS);
	he_mu->flags2 |= RTAP_ENC_HE(phy_data->ntfy->sigs.he.a1,
				     OFDM_RX_FRAME_HE_PRMBL_PUNC_TYPE,
				     IEEE80211_RADIOTAP_HE_MU_FLAGS2_PUNC_FROM_SIG_A_BW);
	he_mu->flags2 |= RTAP_ENC_HE(phy_data->ntfy->sigs.he.a2,
				     OFDM_RX_FRAME_HE_MU_NUM_OF_SIGB_SYM_OR_USER_NUM,
				     IEEE80211_RADIOTAP_HE_MU_FLAGS2_SIG_B_SYMS_USERS);
	he_mu->flags2 |= RTAP_ENC_HE(phy_data->ntfy->sigs.he.b,
				     OFDM_RX_FRAME_HE_MU_SIGB_COMP,
				     IEEE80211_RADIOTAP_HE_MU_FLAGS2_SIG_B_COMP);

	if (phy_data->ntfy->flags & IWL_SNIF_FLAG_VALID_RU &&
	    le32_get_bits(phy_data->ntfy->sigs.he.cmn[2],
			  OFDM_RX_FRAME_HE_COMMON_CC1_CRC_OK)) {
		he_mu->flags1 |=
			cpu_to_le16(IEEE80211_RADIOTAP_HE_MU_FLAGS1_CH1_RU_KNOWN |
				    IEEE80211_RADIOTAP_HE_MU_FLAGS1_CH1_CTR_26T_RU_KNOWN);

		he_mu->flags1 |=
			RTAP_ENC_HE(phy_data->ntfy->sigs.he.cmn[2],
				    OFDM_RX_FRAME_HE_CENTER_RU_CC1,
				    IEEE80211_RADIOTAP_HE_MU_FLAGS1_CH1_CTR_26T_RU);

		he_mu->ru_ch1[0] = le32_get_bits(phy_data->ntfy->sigs.he.cmn[0],
						 OFDM_RX_FRAME_HE_RU_ALLOC_0_A1);
		he_mu->ru_ch1[1] = le32_get_bits(phy_data->ntfy->sigs.he.cmn[1],
						 OFDM_RX_FRAME_HE_RU_ALLOC_1_C1);
		he_mu->ru_ch1[2] = le32_get_bits(phy_data->ntfy->sigs.he.cmn[0],
						 OFDM_RX_FRAME_HE_RU_ALLOC_0_A2);
		he_mu->ru_ch1[3] = le32_get_bits(phy_data->ntfy->sigs.he.cmn[1],
						 OFDM_RX_FRAME_HE_RU_ALLOC_1_C2);
	}

	if (phy_data->ntfy->flags & IWL_SNIF_FLAG_VALID_RU &&
	    le32_get_bits(phy_data->ntfy->sigs.he.cmn[2],
			  OFDM_RX_FRAME_HE_COMMON_CC2_CRC_OK) &&
	    (rate_n_flags & RATE_MCS_CHAN_WIDTH_MSK) != RATE_MCS_CHAN_WIDTH_20) {
		he_mu->flags1 |=
			cpu_to_le16(IEEE80211_RADIOTAP_HE_MU_FLAGS1_CH2_RU_KNOWN |
				    IEEE80211_RADIOTAP_HE_MU_FLAGS1_CH2_CTR_26T_RU_KNOWN);

		he_mu->flags2 |=
			RTAP_ENC_HE(phy_data->ntfy->sigs.he.cmn[2],
				    OFDM_RX_FRAME_HE_CENTER_RU_CC2,
				    IEEE80211_RADIOTAP_HE_MU_FLAGS2_CH2_CTR_26T_RU);

		he_mu->ru_ch2[0] = le32_get_bits(phy_data->ntfy->sigs.he.cmn[0],
						 OFDM_RX_FRAME_HE_RU_ALLOC_0_B1);
		he_mu->ru_ch2[1] = le32_get_bits(phy_data->ntfy->sigs.he.cmn[1],
						 OFDM_RX_FRAME_HE_RU_ALLOC_1_D1);
		he_mu->ru_ch2[2] = le32_get_bits(phy_data->ntfy->sigs.he.cmn[0],
						 OFDM_RX_FRAME_HE_RU_ALLOC_0_B2);
		he_mu->ru_ch2[3] = le32_get_bits(phy_data->ntfy->sigs.he.cmn[1],
						 OFDM_RX_FRAME_HE_RU_ALLOC_1_D2);
	}

#define CHECK_BW(bw) \
	BUILD_BUG_ON(IEEE80211_RADIOTAP_HE_MU_FLAGS2_BW_FROM_SIG_A_BW_ ## bw ## MHZ != \
		     RATE_MCS_CHAN_WIDTH_##bw >> RATE_MCS_CHAN_WIDTH_POS)
	CHECK_BW(20);
	CHECK_BW(40);
	CHECK_BW(80);
	CHECK_BW(160);
#undef CHECK_BW

	he_mu->flags2 |=
		le16_encode_bits(u32_get_bits(rate_n_flags, RATE_MCS_CHAN_WIDTH_MSK),
				 IEEE80211_RADIOTAP_HE_MU_FLAGS2_BW_FROM_SIG_A_BW);

	iwl_mld_he_set_ru_alloc(rx_status, he,
				le32_get_bits(phy_data->ntfy->sigs.he.b,
					      OFDM_RX_FRAME_HE_SIGB_STA_RU));
}

static void
iwl_mld_decode_he_tb_phy_data(struct iwl_mld_rx_phy_data *phy_data,
			      struct ieee80211_radiotap_he *he,
			      struct ieee80211_rx_status *rx_status)
{
	u32 rate_n_flags = phy_data->rate_n_flags;
	u32 nsts;

	he->data1 |= cpu_to_le16(IEEE80211_RADIOTAP_HE_DATA1_BSS_COLOR_KNOWN |
				 IEEE80211_RADIOTAP_HE_DATA1_SPTL_REUSE_KNOWN |
				 IEEE80211_RADIOTAP_HE_DATA1_SPTL_REUSE2_KNOWN |
				 IEEE80211_RADIOTAP_HE_DATA1_SPTL_REUSE3_KNOWN |
				 IEEE80211_RADIOTAP_HE_DATA1_SPTL_REUSE4_KNOWN);

	he->data4 |= RTAP_ENC_HE(phy_data->ntfy->sigs.he_tb.a1,
				 OFDM_RX_HE_TRIG_SPATIAL_REUSE_1,
				 IEEE80211_RADIOTAP_HE_DATA4_TB_SPTL_REUSE1);
	he->data4 |= RTAP_ENC_HE(phy_data->ntfy->sigs.he_tb.a1,
				 OFDM_RX_HE_TRIG_SPATIAL_REUSE_2,
				 IEEE80211_RADIOTAP_HE_DATA4_TB_SPTL_REUSE2);
	he->data4 |= RTAP_ENC_HE(phy_data->ntfy->sigs.he_tb.a1,
				 OFDM_RX_HE_TRIG_SPATIAL_REUSE_3,
				 IEEE80211_RADIOTAP_HE_DATA4_TB_SPTL_REUSE3);
	he->data4 |= RTAP_ENC_HE(phy_data->ntfy->sigs.he_tb.a1,
				 OFDM_RX_HE_TRIG_SPATIAL_REUSE_4,
				 IEEE80211_RADIOTAP_HE_DATA4_TB_SPTL_REUSE4);
	he->data3 |= RTAP_ENC_HE(phy_data->ntfy->sigs.he_tb.a1,
				 OFDM_RX_HE_TRIG_BSS_COLOR,
				 IEEE80211_RADIOTAP_HE_DATA3_BSS_COLOR);

#define CHECK_BW(bw) \
	BUILD_BUG_ON(IEEE80211_RADIOTAP_HE_DATA6_TB_PPDU_BW_ ## bw ## MHZ != \
		     RATE_MCS_CHAN_WIDTH_##bw >> RATE_MCS_CHAN_WIDTH_POS)
	CHECK_BW(20);
	CHECK_BW(40);
	CHECK_BW(80);
	CHECK_BW(160);
#undef CHECK_BW

	he->data6 |= cpu_to_le16(IEEE80211_RADIOTAP_HE_DATA6_TB_PPDU_BW_KNOWN) |
		     le16_encode_bits(u32_get_bits(rate_n_flags, RATE_MCS_CHAN_WIDTH_MSK),
				      IEEE80211_RADIOTAP_HE_DATA6_TB_PPDU_BW);

	if (!(phy_data->ntfy->flags & IWL_SNIF_FLAG_VALID_TB_RX))
		return;

	he->data1 |= cpu_to_le16(IEEE80211_RADIOTAP_HE_DATA1_LDPC_XSYMSEG_KNOWN |
				 IEEE80211_RADIOTAP_HE_DATA1_DOPPLER_KNOWN);
	he->data2 |= cpu_to_le16(IEEE80211_RADIOTAP_HE_DATA2_PRE_FEC_PAD_KNOWN |
				 IEEE80211_RADIOTAP_HE_DATA2_PE_DISAMBIG_KNOWN |
				 IEEE80211_RADIOTAP_HE_DATA2_TXOP_KNOWN |
				 IEEE80211_RADIOTAP_HE_DATA2_NUM_LTF_SYMS_KNOWN);

	he->data3 |= RTAP_ENC_HE(phy_data->ntfy->sigs.he_tb.tb_rx1,
				 OFDM_UCODE_TRIG_BASE_RX_CODING_EXTRA_SYM,
				 IEEE80211_RADIOTAP_HE_DATA3_LDPC_XSYMSEG);
	he->data6 |= RTAP_ENC_HE(phy_data->ntfy->sigs.he_tb.tb_rx1,
				 OFDM_UCODE_TRIG_BASE_RX_DOPPLER,
				 IEEE80211_RADIOTAP_HE_DATA6_DOPPLER);
	he->data5 |= RTAP_ENC_HE(phy_data->ntfy->sigs.he_tb.tb_rx1,
				 OFDM_UCODE_TRIG_BASE_RX_PRE_FEC_PAD_FACTOR,
				 IEEE80211_RADIOTAP_HE_DATA5_PRE_FEC_PAD);
	he->data5 |= RTAP_ENC_HE(phy_data->ntfy->sigs.he_tb.tb_rx1,
				 OFDM_UCODE_TRIG_BASE_RX_PE_DISAMBIG,
				 IEEE80211_RADIOTAP_HE_DATA5_PE_DISAMBIG);
	he->data5 |= RTAP_ENC_HE(phy_data->ntfy->sigs.he_tb.tb_rx1,
				 OFDM_UCODE_TRIG_BASE_RX_NUM_OF_LTF_SYM,
				 IEEE80211_RADIOTAP_HE_DATA5_NUM_LTF_SYMS);
	he->data6 |= RTAP_ENC_HE(phy_data->ntfy->sigs.he_tb.a2,
				 OFDM_RX_HE_TRIG_TXOP_DURATION,
				 IEEE80211_RADIOTAP_HE_DATA6_TXOP);

	iwl_mld_he_set_ru_alloc(rx_status, he,
				le32_get_bits(phy_data->ntfy->sigs.he_tb.tb_rx1,
					      OFDM_UCODE_TRIG_BASE_RX_RU));

	nsts = le32_get_bits(phy_data->ntfy->sigs.he_tb.tb_rx1,
			     OFDM_UCODE_TRIG_BASE_RX_NSTS) + 1;
	rx_status->nss = nsts >> !!(rate_n_flags & RATE_MCS_STBC_MSK);
}

static void
iwl_mld_decode_he_phy_data(struct iwl_mld_rx_phy_data *phy_data,
			   struct ieee80211_radiotap_he *he,
			   struct ieee80211_radiotap_he_mu *he_mu,
			   struct ieee80211_rx_status *rx_status)
{
	u32 rate_n_flags = phy_data->rate_n_flags;
	u32 he_type = rate_n_flags & RATE_MCS_HE_TYPE_MSK;
	u32 nsts;

	switch (he_type) {
	case RATE_MCS_HE_TYPE_TRIG:
		iwl_mld_decode_he_tb_phy_data(phy_data, he, rx_status);
		/* that's it, below is only for SU/MU */
		return;
	case RATE_MCS_HE_TYPE_MU:
		iwl_mld_decode_he_mu(phy_data, he, he_mu, rx_status);

		nsts = le32_get_bits(phy_data->ntfy->sigs.he.b,
				     OFDM_RX_FRAME_HE_SIGB_NSTS) + 1;
		break;
	case RATE_MCS_HE_TYPE_SU:
	case RATE_MCS_HE_TYPE_EXT_SU:
		he->data1 |= cpu_to_le16(IEEE80211_RADIOTAP_HE_DATA1_BEAM_CHANGE_KNOWN);
		he->data3 |= RTAP_ENC_HE(phy_data->ntfy->sigs.he.a1,
					 OFDM_RX_FRAME_HE_BEAM_CHANGE,
					 IEEE80211_RADIOTAP_HE_DATA3_BEAM_CHANGE);

		nsts = le32_get_bits(phy_data->ntfy->sigs.he.a1,
				     OFDM_RX_FRAME_HE_NSTS) + 1;
		break;
	}

	rx_status->nss = nsts >> !!(rate_n_flags & RATE_MCS_STBC_MSK);

	he->data1 |= cpu_to_le16(IEEE80211_RADIOTAP_HE_DATA1_LDPC_XSYMSEG_KNOWN |
				 IEEE80211_RADIOTAP_HE_DATA1_DOPPLER_KNOWN);
	he->data2 |= cpu_to_le16(IEEE80211_RADIOTAP_HE_DATA2_PRE_FEC_PAD_KNOWN |
				 IEEE80211_RADIOTAP_HE_DATA2_PE_DISAMBIG_KNOWN |
				 IEEE80211_RADIOTAP_HE_DATA2_TXOP_KNOWN |
				 IEEE80211_RADIOTAP_HE_DATA2_NUM_LTF_SYMS_KNOWN);

	he->data3 |= RTAP_ENC_HE(phy_data->ntfy->sigs.he.a2,
				 OFDM_RX_FRAME_HE_CODING_EXTRA_SYM,
				 IEEE80211_RADIOTAP_HE_DATA3_LDPC_XSYMSEG);
	he->data5 |= RTAP_ENC_HE(phy_data->ntfy->sigs.he.a2,
				 OFDM_RX_FRAME_HE_PRE_FEC_PAD_FACTOR,
				 IEEE80211_RADIOTAP_HE_DATA5_PRE_FEC_PAD);
	he->data5 |= RTAP_ENC_HE(phy_data->ntfy->sigs.he.a2,
				 OFDM_RX_FRAME_HE_PE_DISAMBIG,
				 IEEE80211_RADIOTAP_HE_DATA5_PE_DISAMBIG);
	he->data5 |= RTAP_ENC_HE(phy_data->ntfy->sigs.he.a2,
				 OFDM_RX_FRAME_HE_MU_NUM_OF_LTF_SYM,
				 IEEE80211_RADIOTAP_HE_DATA5_NUM_LTF_SYMS);
	he->data6 |= RTAP_ENC_HE(phy_data->ntfy->sigs.he.a2,
				 OFDM_RX_FRAME_HE_TXOP_DURATION,
				 IEEE80211_RADIOTAP_HE_DATA6_TXOP);
	he->data6 |= RTAP_ENC_HE(phy_data->ntfy->sigs.he.a2,
				 OFDM_RX_FRAME_HE_DOPPLER,
				 IEEE80211_RADIOTAP_HE_DATA6_DOPPLER);

	he->data1 |= cpu_to_le16(IEEE80211_RADIOTAP_HE_DATA1_UL_DL_KNOWN |
				 IEEE80211_RADIOTAP_HE_DATA1_BSS_COLOR_KNOWN |
				 IEEE80211_RADIOTAP_HE_DATA1_SPTL_REUSE_KNOWN);

	he->data3 |= RTAP_ENC_HE(phy_data->ntfy->sigs.he.a1,
				 OFDM_RX_FRAME_HE_BSS_COLOR,
				 IEEE80211_RADIOTAP_HE_DATA3_BSS_COLOR);
	he->data3 |= RTAP_ENC_HE(phy_data->ntfy->sigs.he.a1,
				 OFDM_RX_FRAME_HE_UL_FLAG,
				 IEEE80211_RADIOTAP_HE_DATA3_UL_DL);
	he->data4 |= RTAP_ENC_HE(phy_data->ntfy->sigs.he.a1,
				 OFDM_RX_FRAME_HE_SPATIAL_REUSE,
				 IEEE80211_RADIOTAP_HE_DATA4_SU_MU_SPTL_REUSE);
}

static void iwl_mld_rx_he(struct sk_buff *skb,
			  struct iwl_mld_rx_phy_data *phy_data)
{
	struct ieee80211_rx_status *rx_status = IEEE80211_SKB_RXCB(skb);
	struct ieee80211_radiotap_he *he = NULL;
	struct ieee80211_radiotap_he_mu *he_mu = NULL;
	u32 rate_n_flags = phy_data->rate_n_flags;
	u32 he_type = rate_n_flags & RATE_MCS_HE_TYPE_MSK;
	u8 ltf;
	static const struct ieee80211_radiotap_he known = {
		.data1 = cpu_to_le16(IEEE80211_RADIOTAP_HE_DATA1_DATA_MCS_KNOWN |
				     IEEE80211_RADIOTAP_HE_DATA1_DATA_DCM_KNOWN |
				     IEEE80211_RADIOTAP_HE_DATA1_STBC_KNOWN	|
				     IEEE80211_RADIOTAP_HE_DATA1_CODING_KNOWN),
		.data2 = cpu_to_le16(IEEE80211_RADIOTAP_HE_DATA2_GI_KNOWN |
				     IEEE80211_RADIOTAP_HE_DATA2_TXBF_KNOWN),
	};
	static const struct ieee80211_radiotap_he_mu mu_known = {
		.flags1 = cpu_to_le16(IEEE80211_RADIOTAP_HE_MU_FLAGS1_SIG_B_MCS_KNOWN |
				      IEEE80211_RADIOTAP_HE_MU_FLAGS1_SIG_B_DCM_KNOWN |
				      IEEE80211_RADIOTAP_HE_MU_FLAGS1_SIG_B_SYMS_USERS_KNOWN |
				      IEEE80211_RADIOTAP_HE_MU_FLAGS1_SIG_B_COMP_KNOWN),
		.flags2 = cpu_to_le16(IEEE80211_RADIOTAP_HE_MU_FLAGS2_PUNC_FROM_SIG_A_BW_KNOWN |
				      IEEE80211_RADIOTAP_HE_MU_FLAGS2_BW_FROM_SIG_A_BW_KNOWN),
	};

	he = skb_put_data(skb, &known, sizeof(known));
	rx_status->flag |= RX_FLAG_RADIOTAP_HE;

	switch (he_type) {
	case RATE_MCS_HE_TYPE_EXT_SU:
		/*
		 * Except for this special case we won't have
		 * HE RU allocation info outside of monitor mode
		 * since we don't get the PHY notif.
		 */
		if (rate_n_flags & RATE_MCS_HE_106T_MSK) {
			rx_status->bw = RATE_INFO_BW_HE_RU;
			rx_status->he_ru = NL80211_RATE_INFO_HE_RU_ALLOC_106;
		}
		fallthrough;
	case RATE_MCS_HE_TYPE_SU:
		/* actual data is filled in mac80211 */
		he->data1 |=
			cpu_to_le16(IEEE80211_RADIOTAP_HE_DATA1_BW_RU_ALLOC_KNOWN);
		break;
	}

#define CHECK_TYPE(F)							\
	BUILD_BUG_ON(IEEE80211_RADIOTAP_HE_DATA1_FORMAT_ ## F !=	\
		     (RATE_MCS_HE_TYPE_ ## F >> RATE_MCS_HE_TYPE_POS))

	CHECK_TYPE(SU);
	CHECK_TYPE(EXT_SU);
	CHECK_TYPE(MU);
	CHECK_TYPE(TRIG);

	he->data1 |= cpu_to_le16(he_type >> RATE_MCS_HE_TYPE_POS);

	if (rate_n_flags & RATE_MCS_BF_MSK)
		he->data5 |= cpu_to_le16(IEEE80211_RADIOTAP_HE_DATA5_TXBF);

	switch (u32_get_bits(rate_n_flags, RATE_MCS_HE_GI_LTF_MSK)) {
	case 0:
		if (he_type == RATE_MCS_HE_TYPE_TRIG)
			rx_status->he_gi = NL80211_RATE_INFO_HE_GI_1_6;
		else
			rx_status->he_gi = NL80211_RATE_INFO_HE_GI_0_8;
		if (he_type == RATE_MCS_HE_TYPE_MU)
			ltf = IEEE80211_RADIOTAP_HE_DATA5_LTF_SIZE_4X;
		else
			ltf = IEEE80211_RADIOTAP_HE_DATA5_LTF_SIZE_1X;
		break;
	case 1:
		if (he_type == RATE_MCS_HE_TYPE_TRIG)
			rx_status->he_gi = NL80211_RATE_INFO_HE_GI_1_6;
		else
			rx_status->he_gi = NL80211_RATE_INFO_HE_GI_0_8;
		ltf = IEEE80211_RADIOTAP_HE_DATA5_LTF_SIZE_2X;
		break;
	case 2:
		if (he_type == RATE_MCS_HE_TYPE_TRIG) {
			rx_status->he_gi = NL80211_RATE_INFO_HE_GI_3_2;
			ltf = IEEE80211_RADIOTAP_HE_DATA5_LTF_SIZE_4X;
		} else {
			rx_status->he_gi = NL80211_RATE_INFO_HE_GI_1_6;
			ltf = IEEE80211_RADIOTAP_HE_DATA5_LTF_SIZE_2X;
		}
		break;
	case 3:
		rx_status->he_gi = NL80211_RATE_INFO_HE_GI_3_2;
		ltf = IEEE80211_RADIOTAP_HE_DATA5_LTF_SIZE_4X;
		break;
	case 4:
		rx_status->he_gi = NL80211_RATE_INFO_HE_GI_0_8;
		ltf = IEEE80211_RADIOTAP_HE_DATA5_LTF_SIZE_4X;
		break;
	default:
		ltf = IEEE80211_RADIOTAP_HE_DATA5_LTF_SIZE_UNKNOWN;
	}

	he->data5 |= le16_encode_bits(ltf,
				      IEEE80211_RADIOTAP_HE_DATA5_LTF_SIZE);

	if (likely(!phy_data->ntfy))
		return;

	if (he_type == RATE_MCS_HE_TYPE_MU) {
		he_mu = skb_put_data(skb, &mu_known, sizeof(mu_known));
		rx_status->flag |= RX_FLAG_RADIOTAP_HE_MU;
	}

	iwl_mld_decode_he_phy_data(phy_data, he, he_mu, rx_status);
}

static void iwl_mld_decode_lsig(struct sk_buff *skb,
				struct iwl_mld_rx_phy_data *phy_data)
{
	struct ieee80211_rx_status *rx_status = IEEE80211_SKB_RXCB(skb);
	u32 format = phy_data->rate_n_flags & RATE_MCS_MOD_TYPE_MSK;
	struct ieee80211_radiotap_lsig *lsig;
	u32 lsig_len, rate;

	if (likely(!phy_data->ntfy))
		return;

	/*
	 * Technically legacy CCK/OFDM frames don't have an L-SIG
	 * since that's the compat format for HT (non-greenfield)
	 * and up. However, it's meant to be compatible with the
	 * LENGTH and RATE fields in Clause 17 and 18 OFDM frames
	 * so include the field for any non-CCK frame. For CCK it
	 * cannot work, since the LENGTH field for them is 16-bit
	 * and the radiotap field only has 12 bits.
	 */
	if (format == RATE_MCS_MOD_TYPE_CCK)
		return;

	lsig_len = le32_get_bits(phy_data->ntfy->legacy_sig.ofdm,
				 OFDM_RX_LEGACY_LENGTH);
	rate = le32_get_bits(phy_data->ntfy->legacy_sig.ofdm, OFDM_RX_RATE);

	lsig = skb_put(skb, sizeof(*lsig));
	lsig->data1 = cpu_to_le16(IEEE80211_RADIOTAP_LSIG_DATA1_LENGTH_KNOWN) |
		      cpu_to_le16(IEEE80211_RADIOTAP_LSIG_DATA1_RATE_KNOWN);
	lsig->data2 = le16_encode_bits(lsig_len,
				       IEEE80211_RADIOTAP_LSIG_DATA2_LENGTH) |
		      le16_encode_bits(rate, IEEE80211_RADIOTAP_LSIG_DATA2_RATE);
	rx_status->flag |= RX_FLAG_RADIOTAP_LSIG;
}

/* Put a TLV on the skb and return data pointer
 *
 * Also pad the len to 4 and zero out all data part
 */
static void *
iwl_mld_radiotap_put_tlv(struct sk_buff *skb, u16 type, u16 len)
{
	struct ieee80211_radiotap_tlv *tlv;

	tlv = skb_put(skb, sizeof(*tlv));
	tlv->type = cpu_to_le16(type);
	tlv->len = cpu_to_le16(len);
	return skb_put_zero(skb, ALIGN(len, 4));
}

#define LE32_DEC_ENC(value, dec_bits, enc_bits) \
	le32_encode_bits(le32_get_bits(value, dec_bits), enc_bits)

#define IWL_MLD_ENC_USIG_VALUE_MASK(usig, in_value, dec_bits, enc_bits) do { \
	typeof(enc_bits) _enc_bits = enc_bits; \
	typeof(usig) _usig = usig; \
	(_usig)->mask |= cpu_to_le32(_enc_bits); \
	(_usig)->value |= LE32_DEC_ENC(in_value, dec_bits, _enc_bits); \
} while (0)

static void iwl_mld_decode_eht_usig_tb(struct iwl_mld_rx_phy_data *phy_data,
				       struct ieee80211_radiotap_eht_usig *usig)
{
	__le32 usig_a1 = phy_data->ntfy->sigs.eht_tb.usig_a1;
	__le32 usig_a2 = phy_data->ntfy->sigs.eht_tb.usig_a2_eht;

	IWL_MLD_ENC_USIG_VALUE_MASK(usig, usig_a1,
				    OFDM_RX_FRAME_EHT_USIG1_DISREGARD,
				    IEEE80211_RADIOTAP_EHT_USIG1_TB_B20_B25_DISREGARD);
	IWL_MLD_ENC_USIG_VALUE_MASK(usig, usig_a2,
				    OFDM_RX_FRAME_EHT_PPDU_TYPE,
				    IEEE80211_RADIOTAP_EHT_USIG2_TB_B0_B1_PPDU_TYPE);
	IWL_MLD_ENC_USIG_VALUE_MASK(usig, usig_a2,
				    OFDM_RX_FRAME_EHT_USIG2_VALIDATE_B2,
				    IEEE80211_RADIOTAP_EHT_USIG2_TB_B2_VALIDATE);
	IWL_MLD_ENC_USIG_VALUE_MASK(usig, usig_a2,
				    OFDM_RX_FRAME_EHT_TRIG_SPATIAL_REUSE_1,
				    IEEE80211_RADIOTAP_EHT_USIG2_TB_B3_B6_SPATIAL_REUSE_1);
	IWL_MLD_ENC_USIG_VALUE_MASK(usig, usig_a2,
				    OFDM_RX_FRAME_EHT_TRIG_SPATIAL_REUSE_2,
				    IEEE80211_RADIOTAP_EHT_USIG2_TB_B7_B10_SPATIAL_REUSE_2);
	IWL_MLD_ENC_USIG_VALUE_MASK(usig, usig_a2,
				    OFDM_RX_FRAME_EHT_TRIG_USIG2_DISREGARD,
				    IEEE80211_RADIOTAP_EHT_USIG2_TB_B11_B15_DISREGARD);
}

static void iwl_mld_decode_eht_usig_non_tb(struct iwl_mld_rx_phy_data *phy_data,
					   struct ieee80211_radiotap_eht_usig *usig)
{
	__le32 usig_a1 = phy_data->ntfy->sigs.eht.usig_a1;
	__le32 usig_a2 = phy_data->ntfy->sigs.eht.usig_a2_eht;

	IWL_MLD_ENC_USIG_VALUE_MASK(usig, usig_a1,
				    OFDM_RX_FRAME_EHT_USIG1_DISREGARD,
				    IEEE80211_RADIOTAP_EHT_USIG1_MU_B20_B24_DISREGARD);
	IWL_MLD_ENC_USIG_VALUE_MASK(usig, usig_a1,
				    OFDM_RX_FRAME_EHT_USIG1_VALIDATE,
				    IEEE80211_RADIOTAP_EHT_USIG1_MU_B25_VALIDATE);
	IWL_MLD_ENC_USIG_VALUE_MASK(usig, usig_a2,
				    OFDM_RX_FRAME_EHT_PPDU_TYPE,
				    IEEE80211_RADIOTAP_EHT_USIG2_MU_B0_B1_PPDU_TYPE);
	IWL_MLD_ENC_USIG_VALUE_MASK(usig, usig_a2,
				    OFDM_RX_FRAME_EHT_USIG2_VALIDATE_B2,
				    IEEE80211_RADIOTAP_EHT_USIG2_MU_B2_VALIDATE);
	IWL_MLD_ENC_USIG_VALUE_MASK(usig, usig_a2,
				    OFDM_RX_FRAME_EHT_PUNC_CHANNEL,
				    IEEE80211_RADIOTAP_EHT_USIG2_MU_B3_B7_PUNCTURED_INFO);
	IWL_MLD_ENC_USIG_VALUE_MASK(usig, usig_a2,
				    OFDM_RX_FRAME_EHT_USIG2_VALIDATE_B8,
				    IEEE80211_RADIOTAP_EHT_USIG2_MU_B8_VALIDATE);
	IWL_MLD_ENC_USIG_VALUE_MASK(usig, usig_a2,
				    OFDM_RX_FRAME_EHT_SIG_MCS,
				    IEEE80211_RADIOTAP_EHT_USIG2_MU_B9_B10_SIG_MCS);
	IWL_MLD_ENC_USIG_VALUE_MASK(usig, usig_a2,
				    OFDM_RX_FRAME_EHT_SIG_SYM_NUM,
				    IEEE80211_RADIOTAP_EHT_USIG2_MU_B11_B15_EHT_SIG_SYMBOLS);
}

static void iwl_mld_decode_eht_usig(struct iwl_mld_rx_phy_data *phy_data,
				    struct sk_buff *skb)
{
	u32 he_type = phy_data->rate_n_flags & RATE_MCS_HE_TYPE_MSK;
	__le32 usig_a1 = phy_data->ntfy->sigs.eht.usig_a1;
	__le32 usig_a2 = phy_data->ntfy->sigs.eht.usig_a2_eht;
	struct ieee80211_radiotap_eht_usig *usig;
	u32 bw;

	usig = iwl_mld_radiotap_put_tlv(skb, IEEE80211_RADIOTAP_EHT_USIG,
					sizeof(*usig));

	BUILD_BUG_ON(offsetof(union iwl_sigs, eht.usig_a1) !=
		     offsetof(union iwl_sigs, eht_tb.usig_a1));
	BUILD_BUG_ON(offsetof(union iwl_sigs, eht.usig_a2_eht) !=
		     offsetof(union iwl_sigs, eht_tb.usig_a2_eht));

	usig->common |= cpu_to_le32(IEEE80211_RADIOTAP_EHT_USIG_COMMON_UL_DL_KNOWN |
				    IEEE80211_RADIOTAP_EHT_USIG_COMMON_BSS_COLOR_KNOWN |
				    IEEE80211_RADIOTAP_EHT_USIG_COMMON_VALIDATE_BITS_CHECKED |
				    IEEE80211_RADIOTAP_EHT_USIG_COMMON_BW_KNOWN |
				    IEEE80211_RADIOTAP_EHT_USIG_COMMON_TXOP_KNOWN);

#define CHECK_BW(bw) \
	BUILD_BUG_ON(IEEE80211_RADIOTAP_EHT_USIG_COMMON_BW_ ## bw ## MHZ != \
		     RATE_MCS_CHAN_WIDTH_ ## bw ## _VAL)
	CHECK_BW(20);
	CHECK_BW(40);
	CHECK_BW(80);
	CHECK_BW(160);
#undef CHECK_BW
	BUILD_BUG_ON(IEEE80211_RADIOTAP_EHT_USIG_COMMON_BW_320MHZ_1 !=
		     RATE_MCS_CHAN_WIDTH_320_VAL);
	bw = u32_get_bits(phy_data->rate_n_flags, RATE_MCS_CHAN_WIDTH_MSK);
	/* specific handling for 320MHz-1/320MHz-2 */
	if (bw == RATE_MCS_CHAN_WIDTH_320_VAL)
		bw += le32_get_bits(usig_a1, OFDM_RX_FRAME_EHT_BW320_SLOT);
	usig->common |= le32_encode_bits(bw,
					 IEEE80211_RADIOTAP_EHT_USIG_COMMON_BW);

	usig->common |= LE32_DEC_ENC(usig_a1, OFDM_RX_FRAME_ENHANCED_WIFI_UL_FLAG,
				     IEEE80211_RADIOTAP_EHT_USIG_COMMON_UL_DL);
	usig->common |= LE32_DEC_ENC(usig_a1, OFDM_RX_FRAME_ENHANCED_WIFI_BSS_COLOR,
				     IEEE80211_RADIOTAP_EHT_USIG_COMMON_BSS_COLOR);

	if (le32_get_bits(usig_a1, OFDM_RX_FRAME_EHT_USIG1_VALIDATE) &&
	    le32_get_bits(usig_a2, OFDM_RX_FRAME_EHT_USIG2_VALIDATE_B2) &&
	    le32_get_bits(usig_a2, OFDM_RX_FRAME_EHT_USIG2_VALIDATE_B8))
		usig->common |= cpu_to_le32(IEEE80211_RADIOTAP_EHT_USIG_COMMON_VALIDATE_BITS_OK);

	usig->common |= LE32_DEC_ENC(usig_a1,
				     OFDM_RX_FRAME_ENHANCED_WIFI_TXOP_DURATION,
				     IEEE80211_RADIOTAP_EHT_USIG_COMMON_TXOP);

	if (!le32_get_bits(usig_a2, OFDM_RX_USIG_CRC_OK))
		usig->common |= cpu_to_le32(IEEE80211_RADIOTAP_EHT_USIG_COMMON_BAD_USIG_CRC);

	usig->common |= cpu_to_le32(IEEE80211_RADIOTAP_EHT_USIG_COMMON_PHY_VER_KNOWN);
	usig->common |= LE32_DEC_ENC(usig_a1,
				     OFDM_RX_FRAME_ENHANCED_WIFI_VER_ID,
				     IEEE80211_RADIOTAP_EHT_USIG_COMMON_PHY_VER);

	if (he_type == RATE_MCS_HE_TYPE_TRIG)
		iwl_mld_decode_eht_usig_tb(phy_data, usig);
	else
		iwl_mld_decode_eht_usig_non_tb(phy_data, usig);
}

static void
iwl_mld_eht_set_ru_alloc(struct ieee80211_rx_status *rx_status,
			 u32 ru_with_p80)
{
	enum nl80211_eht_ru_alloc nl_ru;
	u32 ru = ru_with_p80 >> 1;

	/*
	 * HW always uses trigger frame format:
	 *
	 * Draft PIEEE802.11be D7.0 Table 9-46l - Encoding of the PS160 and
	 * RU Allocation subfields in an EHT variant User Info field
	 */

	switch (ru) {
	case 0 ... 36:
		nl_ru = NL80211_RATE_INFO_EHT_RU_ALLOC_26;
		break;
	case 37 ... 52:
		nl_ru = NL80211_RATE_INFO_EHT_RU_ALLOC_52;
		break;
	case 53 ... 60:
		nl_ru = NL80211_RATE_INFO_EHT_RU_ALLOC_106;
		break;
	case 61 ... 64:
		nl_ru = NL80211_RATE_INFO_EHT_RU_ALLOC_242;
		break;
	case 65 ... 66:
		nl_ru = NL80211_RATE_INFO_EHT_RU_ALLOC_484;
		break;
	case 67:
		nl_ru = NL80211_RATE_INFO_EHT_RU_ALLOC_996;
		break;
	case 68:
		nl_ru = NL80211_RATE_INFO_EHT_RU_ALLOC_2x996;
		break;
	case 69:
		nl_ru = NL80211_RATE_INFO_EHT_RU_ALLOC_4x996;
		break;
	case 70 ... 81:
		nl_ru = NL80211_RATE_INFO_EHT_RU_ALLOC_52P26;
		break;
	case 82 ... 89:
		nl_ru = NL80211_RATE_INFO_EHT_RU_ALLOC_106P26;
		break;
	case 90 ... 93:
		nl_ru = NL80211_RATE_INFO_EHT_RU_ALLOC_484P242;
		break;
	case 94 ... 95:
		nl_ru = NL80211_RATE_INFO_EHT_RU_ALLOC_996P484;
		break;
	case 96 ... 99:
		nl_ru = NL80211_RATE_INFO_EHT_RU_ALLOC_996P484P242;
		break;
	case 100 ... 103:
		nl_ru = NL80211_RATE_INFO_EHT_RU_ALLOC_2x996P484;
		break;
	case 104:
		nl_ru = NL80211_RATE_INFO_EHT_RU_ALLOC_3x996;
		break;
	case 105 ... 106:
		nl_ru = NL80211_RATE_INFO_EHT_RU_ALLOC_3x996P484;
		break;
	default:
		return;
	}

	rx_status->bw = RATE_INFO_BW_EHT_RU;
	rx_status->eht.ru = nl_ru;
}

static void iwl_mld_decode_eht_tb(struct iwl_mld_rx_phy_data *phy_data,
				  struct ieee80211_rx_status *rx_status,
				  struct ieee80211_radiotap_eht *eht)
{
	if (!(phy_data->ntfy->flags & IWL_SNIF_FLAG_VALID_TB_RX))
		return;

	eht->known |= cpu_to_le32(IEEE80211_RADIOTAP_EHT_KNOWN_RU_ALLOC_TB_FMT |
				  IEEE80211_RADIOTAP_EHT_KNOWN_LDPC_EXTRA_SYM_OM |
				  IEEE80211_RADIOTAP_EHT_KNOWN_PRE_PADD_FACOR_OM |
				  IEEE80211_RADIOTAP_EHT_KNOWN_PE_DISAMBIGUITY_OM |
				  IEEE80211_RADIOTAP_EHT_KNOWN_EHT_LTF |
				  IEEE80211_RADIOTAP_EHT_KNOWN_PRIMARY_80);

	eht->data[8] |= LE32_DEC_ENC(phy_data->ntfy->sigs.eht_tb.tb_rx0,
				     OFDM_UCODE_TRIG_BASE_PS160,
				     IEEE80211_RADIOTAP_EHT_DATA8_RU_ALLOC_TB_FMT_PS_160);
	eht->data[8] |= LE32_DEC_ENC(phy_data->ntfy->sigs.eht_tb.tb_rx1,
				     OFDM_UCODE_TRIG_BASE_RX_RU,
				     IEEE80211_RADIOTAP_EHT_DATA8_RU_ALLOC_TB_FMT_B0 |
				     IEEE80211_RADIOTAP_EHT_DATA8_RU_ALLOC_TB_FMT_B7_B1);
	eht->data[0] |= LE32_DEC_ENC(phy_data->ntfy->sigs.eht_tb.tb_rx1,
				     OFDM_UCODE_TRIG_BASE_RX_CODING_EXTRA_SYM,
				     IEEE80211_RADIOTAP_EHT_DATA0_LDPC_EXTRA_SYM_OM);
	eht->data[0] |= LE32_DEC_ENC(phy_data->ntfy->sigs.eht_tb.tb_rx1,
				     OFDM_UCODE_TRIG_BASE_RX_PRE_FEC_PAD_FACTOR,
				     IEEE80211_RADIOTAP_EHT_DATA0_PRE_PADD_FACOR_OM);
	eht->data[0] |= LE32_DEC_ENC(phy_data->ntfy->sigs.eht_tb.tb_rx1,
				     OFDM_UCODE_TRIG_BASE_RX_PE_DISAMBIG,
				     IEEE80211_RADIOTAP_EHT_DATA0_PE_DISAMBIGUITY_OM);
	eht->data[0] |= LE32_DEC_ENC(phy_data->ntfy->sigs.eht_tb.tb_rx1,
				     OFDM_UCODE_TRIG_BASE_RX_NUM_OF_LTF_SYM,
				     IEEE80211_RADIOTAP_EHT_DATA0_EHT_LTF);
	eht->data[1] |= LE32_DEC_ENC(phy_data->ntfy->sigs.eht_tb.tb_rx0,
				     OFDM_UCODE_TRIG_BASE_RX_RU_P80,
				     IEEE80211_RADIOTAP_EHT_DATA1_PRIMARY_80);

	iwl_mld_eht_set_ru_alloc(rx_status,
				 le32_get_bits(phy_data->ntfy->sigs.eht_tb.tb_rx1,
					       OFDM_UCODE_TRIG_BASE_RX_RU));
}

static void iwl_mld_eht_decode_user_ru(struct iwl_mld_rx_phy_data *phy_data,
				       struct ieee80211_radiotap_eht *eht)
{
	u32 phy_bw = phy_data->rate_n_flags & RATE_MCS_CHAN_WIDTH_MSK;

	if (!(phy_data->ntfy->flags & IWL_SNIF_FLAG_VALID_RU))
		return;

#define __IWL_MLD_ENC_EHT_RU(rt_data, rt_ru, fw_data, fw_ru) \
	eht->data[(rt_data)] |= \
		(cpu_to_le32(IEEE80211_RADIOTAP_EHT_DATA ## rt_data ## _RU_ALLOC_CC_ ## rt_ru ## _KNOWN) | \
		 LE32_DEC_ENC(phy_data->ntfy->sigs.eht.cmn[fw_data], \
			      OFDM_RX_FRAME_EHT_RU_ALLOC_ ## fw_data ## _ ## fw_ru, \
			      IEEE80211_RADIOTAP_EHT_DATA ## rt_data ## _RU_ALLOC_CC_ ## rt_ru))

#define _IWL_MLD_ENC_EHT_RU(rt_data, rt_ru, fw_data, fw_ru)	\
	__IWL_MLD_ENC_EHT_RU(rt_data, rt_ru, fw_data, fw_ru)

#define IEEE80211_RADIOTAP_RU_DATA_1_1_1	1
#define IEEE80211_RADIOTAP_RU_DATA_2_1_1	2
#define IEEE80211_RADIOTAP_RU_DATA_1_1_2	2
#define IEEE80211_RADIOTAP_RU_DATA_2_1_2	2
#define IEEE80211_RADIOTAP_RU_DATA_1_2_1	3
#define IEEE80211_RADIOTAP_RU_DATA_2_2_1	3
#define IEEE80211_RADIOTAP_RU_DATA_1_2_2	3
#define IEEE80211_RADIOTAP_RU_DATA_2_2_2	4
#define IEEE80211_RADIOTAP_RU_DATA_1_2_3	4
#define IEEE80211_RADIOTAP_RU_DATA_2_2_3	4
#define IEEE80211_RADIOTAP_RU_DATA_1_2_4	5
#define IEEE80211_RADIOTAP_RU_DATA_2_2_4	5
#define IEEE80211_RADIOTAP_RU_DATA_1_2_5	5
#define IEEE80211_RADIOTAP_RU_DATA_2_2_5	6
#define IEEE80211_RADIOTAP_RU_DATA_1_2_6	6
#define IEEE80211_RADIOTAP_RU_DATA_2_2_6	6

#define IWL_RX_RU_DATA_A1			0
#define IWL_RX_RU_DATA_A2			0
#define IWL_RX_RU_DATA_A3			0
#define IWL_RX_RU_DATA_A4			4
#define IWL_RX_RU_DATA_B1			1
#define IWL_RX_RU_DATA_B2			1
#define IWL_RX_RU_DATA_B3			1
#define IWL_RX_RU_DATA_B4			4
#define IWL_RX_RU_DATA_C1			2
#define IWL_RX_RU_DATA_C2			2
#define IWL_RX_RU_DATA_C3			2
#define IWL_RX_RU_DATA_C4			5
#define IWL_RX_RU_DATA_D1			3
#define IWL_RX_RU_DATA_D2			3
#define IWL_RX_RU_DATA_D3			3
#define IWL_RX_RU_DATA_D4			5

#define IWL_MLD_ENC_EHT_RU(rt_ru, fw_ru)				\
	_IWL_MLD_ENC_EHT_RU(IEEE80211_RADIOTAP_RU_DATA_ ## rt_ru,	\
			    rt_ru,					\
			    IWL_RX_RU_DATA_ ## fw_ru,			\
			    fw_ru)

	/*
	 * Hardware labels the content channels/RU allocation values
	 * as follows:
	 *
	 *           Content Channel 1		Content Channel 2
	 *   20 MHz: A1
	 *   40 MHz: A1				B1
	 *   80 MHz: A1 C1			B1 D1
	 *  160 MHz: A1 C1 A2 C2		B1 D1 B2 D2
	 *  320 MHz: A1 C1 A2 C2 A3 C3 A4 C4	B1 D1 B2 D2 B3 D3 B4 D4
	 */

	switch (phy_bw) {
	case RATE_MCS_CHAN_WIDTH_320:
		/* content channel 1 */
		IWL_MLD_ENC_EHT_RU(1_2_3, A3);
		IWL_MLD_ENC_EHT_RU(1_2_4, C3);
		IWL_MLD_ENC_EHT_RU(1_2_5, A4);
		IWL_MLD_ENC_EHT_RU(1_2_6, C4);
		/* content channel 2 */
		IWL_MLD_ENC_EHT_RU(2_2_3, B3);
		IWL_MLD_ENC_EHT_RU(2_2_4, D3);
		IWL_MLD_ENC_EHT_RU(2_2_5, B4);
		IWL_MLD_ENC_EHT_RU(2_2_6, D4);
		fallthrough;
	case RATE_MCS_CHAN_WIDTH_160:
		/* content channel 1 */
		IWL_MLD_ENC_EHT_RU(1_2_1, A2);
		IWL_MLD_ENC_EHT_RU(1_2_2, C2);
		/* content channel 2 */
		IWL_MLD_ENC_EHT_RU(2_2_1, B2);
		IWL_MLD_ENC_EHT_RU(2_2_2, D2);
		fallthrough;
	case RATE_MCS_CHAN_WIDTH_80:
		/* content channel 1 */
		IWL_MLD_ENC_EHT_RU(1_1_2, C1);
		/* content channel 2 */
		IWL_MLD_ENC_EHT_RU(2_1_2, D1);
		fallthrough;
	case RATE_MCS_CHAN_WIDTH_40:
		/* content channel 2 */
		IWL_MLD_ENC_EHT_RU(2_1_1, B1);
		fallthrough;
	case RATE_MCS_CHAN_WIDTH_20:
		/* content channel 1 */
		IWL_MLD_ENC_EHT_RU(1_1_1, A1);
		break;
	}
}

static void iwl_mld_decode_eht_non_tb(struct iwl_mld_rx_phy_data *phy_data,
				      struct ieee80211_rx_status *rx_status,
				      struct ieee80211_radiotap_eht *eht)
{
	eht->known |= cpu_to_le32(IEEE80211_RADIOTAP_EHT_KNOWN_SPATIAL_REUSE |
				  /* All RU allocating size/index is in TB format */
				  IEEE80211_RADIOTAP_EHT_KNOWN_RU_ALLOC_TB_FMT |
				  IEEE80211_RADIOTAP_EHT_KNOWN_LDPC_EXTRA_SYM_OM |
				  IEEE80211_RADIOTAP_EHT_KNOWN_PRE_PADD_FACOR_OM |
				  IEEE80211_RADIOTAP_EHT_KNOWN_PE_DISAMBIGUITY_OM |
				  IEEE80211_RADIOTAP_EHT_KNOWN_EHT_LTF |
				  IEEE80211_RADIOTAP_EHT_KNOWN_PRIMARY_80 |
				  IEEE80211_RADIOTAP_EHT_KNOWN_NR_NON_OFDMA_USERS_M);

	eht->data[0] |= LE32_DEC_ENC(phy_data->ntfy->sigs.eht.b1,
				     OFDM_RX_FRAME_EHT_SPATIAL_REUSE,
				     IEEE80211_RADIOTAP_EHT_DATA0_SPATIAL_REUSE);
	eht->data[8] |= LE32_DEC_ENC(phy_data->ntfy->sigs.eht.b2,
				     OFDM_RX_FRAME_EHT_STA_RU_PS160,
				     IEEE80211_RADIOTAP_EHT_DATA8_RU_ALLOC_TB_FMT_PS_160);
	eht->data[8] |= LE32_DEC_ENC(phy_data->ntfy->sigs.eht.b2,
				     OFDM_RX_FRAME_EHT_STA_RU,
				     IEEE80211_RADIOTAP_EHT_DATA8_RU_ALLOC_TB_FMT_B0 |
				     IEEE80211_RADIOTAP_EHT_DATA8_RU_ALLOC_TB_FMT_B7_B1);
	eht->data[0] |= LE32_DEC_ENC(phy_data->ntfy->sigs.eht.b1,
				     OFDM_RX_FRAME_EHT_CODING_EXTRA_SYM,
				     IEEE80211_RADIOTAP_EHT_DATA0_LDPC_EXTRA_SYM_OM);
	eht->data[0] |= LE32_DEC_ENC(phy_data->ntfy->sigs.eht.b1,
				     OFDM_RX_FRAME_EHT_PRE_FEC_PAD_FACTOR,
				     IEEE80211_RADIOTAP_EHT_DATA0_PRE_PADD_FACOR_OM);
	eht->data[0] |= LE32_DEC_ENC(phy_data->ntfy->sigs.eht.b1,
				     OFDM_RX_FRAME_EHT_PE_DISAMBIG,
				     IEEE80211_RADIOTAP_EHT_DATA0_PE_DISAMBIGUITY_OM);
	eht->data[0] |= LE32_DEC_ENC(phy_data->ntfy->sigs.eht.b1,
				     OFDM_RX_FRAME_EHT_NUM_OF_LTF_SYM,
				     IEEE80211_RADIOTAP_EHT_DATA0_EHT_LTF);
	eht->data[1] |= LE32_DEC_ENC(phy_data->ntfy->sigs.eht.b2,
				     OFDM_RX_FRAME_EHT_STA_RU_P80,
				     IEEE80211_RADIOTAP_EHT_DATA1_PRIMARY_80);
	eht->data[7] |= LE32_DEC_ENC(phy_data->ntfy->sigs.eht.b1,
				     OFDM_RX_FRAME_EHT_NUM_OF_USERS,
				     IEEE80211_RADIOTAP_EHT_DATA7_NUM_OF_NON_OFDMA_USERS);

	iwl_mld_eht_decode_user_ru(phy_data, eht);

	iwl_mld_eht_set_ru_alloc(rx_status,
				 le32_get_bits(phy_data->ntfy->sigs.eht.b2,
					       OFDM_RX_FRAME_EHT_STA_RU));
}

static void iwl_mld_decode_eht_phy_data(struct iwl_mld_rx_phy_data *phy_data,
					struct ieee80211_rx_status *rx_status,
					struct ieee80211_radiotap_eht *eht)
{
	u32 he_type = phy_data->rate_n_flags & RATE_MCS_HE_TYPE_MSK;

	if (he_type == RATE_MCS_HE_TYPE_TRIG)
		iwl_mld_decode_eht_tb(phy_data, rx_status, eht);
	else
		iwl_mld_decode_eht_non_tb(phy_data, rx_status, eht);
}

static void iwl_mld_rx_eht(struct iwl_mld *mld, struct sk_buff *skb,
			   struct iwl_mld_rx_phy_data *phy_data)
{
	struct ieee80211_rx_status *rx_status = IEEE80211_SKB_RXCB(skb);
	struct ieee80211_radiotap_eht *eht;
	size_t eht_len = sizeof(*eht);
	u32 rate_n_flags = phy_data->rate_n_flags;
	u32 he_type = rate_n_flags & RATE_MCS_HE_TYPE_MSK;
	/* EHT and HE have the same values for LTF */
	u8 ltf = IEEE80211_RADIOTAP_HE_DATA5_LTF_SIZE_UNKNOWN;

	/* u32 for 1 user_info */
	if (phy_data->with_data)
		eht_len += sizeof(u32);

	eht = iwl_mld_radiotap_put_tlv(skb, IEEE80211_RADIOTAP_EHT, eht_len);

	rx_status->flag |= RX_FLAG_RADIOTAP_TLV_AT_END;

	switch (u32_get_bits(rate_n_flags, RATE_MCS_HE_GI_LTF_MSK)) {
	case 0:
		if (he_type == RATE_MCS_HE_TYPE_TRIG) {
			rx_status->eht.gi = NL80211_RATE_INFO_EHT_GI_1_6;
			ltf = IEEE80211_RADIOTAP_HE_DATA5_LTF_SIZE_1X;
		} else {
			rx_status->eht.gi = NL80211_RATE_INFO_EHT_GI_0_8;
			ltf = IEEE80211_RADIOTAP_HE_DATA5_LTF_SIZE_2X;
		}
		break;
	case 1:
		rx_status->eht.gi = NL80211_RATE_INFO_EHT_GI_1_6;
		ltf = IEEE80211_RADIOTAP_HE_DATA5_LTF_SIZE_2X;
		break;
	case 2:
		ltf = IEEE80211_RADIOTAP_HE_DATA5_LTF_SIZE_4X;
		if (he_type == RATE_MCS_HE_TYPE_TRIG)
			rx_status->eht.gi = NL80211_RATE_INFO_EHT_GI_3_2;
		else
			rx_status->eht.gi = NL80211_RATE_INFO_EHT_GI_0_8;
		break;
	case 3:
		if (he_type != RATE_MCS_HE_TYPE_TRIG) {
			ltf = IEEE80211_RADIOTAP_HE_DATA5_LTF_SIZE_4X;
			rx_status->eht.gi = NL80211_RATE_INFO_EHT_GI_3_2;
		}
		break;
	default:
		/* nothing here */
		break;
	}

	if (ltf != IEEE80211_RADIOTAP_HE_DATA5_LTF_SIZE_UNKNOWN) {
		eht->known |= cpu_to_le32(IEEE80211_RADIOTAP_EHT_KNOWN_GI);
		eht->data[0] |= le32_encode_bits(ltf,
						 IEEE80211_RADIOTAP_EHT_DATA0_LTF) |
				le32_encode_bits(rx_status->eht.gi,
						 IEEE80211_RADIOTAP_EHT_DATA0_GI);
	}

	if (!phy_data->with_data) {
		eht->known |= cpu_to_le32(IEEE80211_RADIOTAP_EHT_KNOWN_NSS_S |
					  IEEE80211_RADIOTAP_EHT_KNOWN_BEAMFORMED_S);
		eht->data[7] |= LE32_DEC_ENC(phy_data->ntfy->sigs.eht.b1,
					     OFDM_RX_FRAME_EHT_NSTS,
					     IEEE80211_RADIOTAP_EHT_DATA7_NSS_S);
		if (rate_n_flags & RATE_MCS_BF_MSK)
			eht->data[7] |=
				cpu_to_le32(IEEE80211_RADIOTAP_EHT_DATA7_BEAMFORMED_S);
	} else {
		eht->user_info[0] |=
			cpu_to_le32(IEEE80211_RADIOTAP_EHT_USER_INFO_MCS_KNOWN |
				    IEEE80211_RADIOTAP_EHT_USER_INFO_CODING_KNOWN |
				    IEEE80211_RADIOTAP_EHT_USER_INFO_NSS_KNOWN_O |
				    IEEE80211_RADIOTAP_EHT_USER_INFO_BEAMFORMING_KNOWN_O |
				    IEEE80211_RADIOTAP_EHT_USER_INFO_DATA_FOR_USER);

		if (rate_n_flags & RATE_MCS_BF_MSK)
			eht->user_info[0] |=
				cpu_to_le32(IEEE80211_RADIOTAP_EHT_USER_INFO_BEAMFORMING_O);

		if (rate_n_flags & RATE_MCS_LDPC_MSK)
			eht->user_info[0] |=
				cpu_to_le32(IEEE80211_RADIOTAP_EHT_USER_INFO_CODING);

		eht->user_info[0] |=
			le32_encode_bits(u32_get_bits(rate_n_flags,
						      RATE_VHT_MCS_RATE_CODE_MSK),
					 IEEE80211_RADIOTAP_EHT_USER_INFO_MCS) |
			le32_encode_bits(u32_get_bits(rate_n_flags,
						      RATE_MCS_NSS_MSK),
					 IEEE80211_RADIOTAP_EHT_USER_INFO_NSS_O);
	}

	if (likely(!phy_data->ntfy))
		return;

	if (phy_data->with_data) {
		eht->user_info[0] |=
			cpu_to_le32(IEEE80211_RADIOTAP_EHT_USER_INFO_STA_ID_KNOWN) |
			LE32_DEC_ENC(phy_data->ntfy->sigs.eht.user_id,
				     OFDM_RX_FRAME_EHT_USER_FIELD_ID,
				     IEEE80211_RADIOTAP_EHT_USER_INFO_STA_ID);
	}

	iwl_mld_decode_eht_usig(phy_data, skb);
	iwl_mld_decode_eht_phy_data(phy_data, rx_status, eht);
}

#ifdef CONFIG_IWLWIFI_DEBUGFS
static void iwl_mld_add_rtap_sniffer_config(struct iwl_mld *mld,
					    struct sk_buff *skb)
{
	struct ieee80211_rx_status *rx_status = IEEE80211_SKB_RXCB(skb);
	struct ieee80211_radiotap_vendor_content *radiotap;
	const u16 vendor_data_len = sizeof(mld->monitor.cur_aid);

	if (!mld->monitor.cur_aid)
		return;

	radiotap =
		iwl_mld_radiotap_put_tlv(skb,
					 IEEE80211_RADIOTAP_VENDOR_NAMESPACE,
					 sizeof(*radiotap) + vendor_data_len);

	/* Intel OUI */
	radiotap->oui[0] = 0xf6;
	radiotap->oui[1] = 0x54;
	radiotap->oui[2] = 0x25;
	/* Intel OUI default radiotap subtype */
	radiotap->oui_subtype = 1;
	/* Sniffer config element type */
	radiotap->vendor_type = 0;

	/* fill the data now */
	memcpy(radiotap->data, &mld->monitor.cur_aid,
	       sizeof(mld->monitor.cur_aid));

	rx_status->flag |= RX_FLAG_RADIOTAP_TLV_AT_END;
}
#endif

static void iwl_mld_add_rtap_sniffer_phy_data(struct iwl_mld *mld,
					      struct sk_buff *skb,
					      struct iwl_rx_phy_air_sniffer_ntfy *ntfy)
{
	struct ieee80211_rx_status *rx_status = IEEE80211_SKB_RXCB(skb);
	struct ieee80211_radiotap_vendor_content *radiotap;
	const u16 vendor_data_len = sizeof(*ntfy);

	radiotap =
		iwl_mld_radiotap_put_tlv(skb,
					 IEEE80211_RADIOTAP_VENDOR_NAMESPACE,
					 sizeof(*radiotap) + vendor_data_len);

	/* Intel OUI */
	radiotap->oui[0] = 0xf6;
	radiotap->oui[1] = 0x54;
	radiotap->oui[2] = 0x25;
	/* Intel OUI default radiotap subtype */
	radiotap->oui_subtype = 1;
	/* PHY data element type */
	radiotap->vendor_type = cpu_to_le16(1);

	/* fill the data now */
	memcpy(radiotap->data, ntfy, vendor_data_len);

	rx_status->flag |= RX_FLAG_RADIOTAP_TLV_AT_END;
}

static void
iwl_mld_set_rx_nonlegacy_rate_info(u32 rate_n_flags,
				   struct ieee80211_rx_status *rx_status)
{
	u8 stbc = u32_get_bits(rate_n_flags, RATE_MCS_STBC_MSK);

	/* NSS may be overridden by PHY ntfy with full value */
	rx_status->nss = u32_get_bits(rate_n_flags, RATE_MCS_NSS_MSK) + 1;
	rx_status->rate_idx = rate_n_flags & RATE_MCS_CODE_MSK;
	rx_status->enc_flags |= stbc << RX_ENC_FLAG_STBC_SHIFT;
	if (rate_n_flags & RATE_MCS_LDPC_MSK)
		rx_status->enc_flags |= RX_ENC_FLAG_LDPC;
}

static void iwl_mld_set_rx_rate(struct iwl_mld *mld,
				struct iwl_mld_rx_phy_data *phy_data,
				struct ieee80211_rx_status *rx_status)
{
	u32 rate_n_flags = phy_data->rate_n_flags;
	u8 stbc = u32_get_bits(rate_n_flags, RATE_MCS_STBC_MSK);
	u32 format = rate_n_flags & RATE_MCS_MOD_TYPE_MSK;
	bool is_sgi = rate_n_flags & RATE_MCS_SGI_MSK;

	/* bandwidth may be overridden to RU by PHY ntfy */
	switch (rate_n_flags & RATE_MCS_CHAN_WIDTH_MSK) {
	case RATE_MCS_CHAN_WIDTH_20:
		break;
	case RATE_MCS_CHAN_WIDTH_40:
		rx_status->bw = RATE_INFO_BW_40;
		break;
	case RATE_MCS_CHAN_WIDTH_80:
		rx_status->bw = RATE_INFO_BW_80;
		break;
	case RATE_MCS_CHAN_WIDTH_160:
		rx_status->bw = RATE_INFO_BW_160;
		break;
	case RATE_MCS_CHAN_WIDTH_320:
		rx_status->bw = RATE_INFO_BW_320;
		break;
	}

	switch (format) {
	case RATE_MCS_MOD_TYPE_CCK:
		if (phy_data->phy_info & IWL_RX_MPDU_PHY_SHORT_PREAMBLE)
			rx_status->enc_flags |= RX_ENC_FLAG_SHORTPRE;
		fallthrough;
	case RATE_MCS_MOD_TYPE_LEGACY_OFDM: {
		int rate =
		    iwl_mld_legacy_hw_idx_to_mac80211_idx(rate_n_flags,
							  rx_status->band);

		/* override BW - it could be DUP and indicate the wrong BW */
		rx_status->bw = RATE_INFO_BW_20;

		/* valid rate */
		if (rate >= 0 && rate <= 0xFF) {
			rx_status->rate_idx = rate;
			break;
		}

		/* invalid rate */
		rx_status->rate_idx = 0;

		/*
		 * In monitor mode we can see CCK frames on 5 or 6 GHz, usually
		 * just the (possibly malformed) PHY header by accident, since
		 * the decoder doesn't seem to turn off CCK. We cannot correctly
		 * encode the rate to mac80211 (and therefore not in radiotap)
		 * since we give the per-band index which doesn't cover those
		 * rates.
		 */
		if (!mld->monitor.on && net_ratelimit())
			IWL_ERR(mld, "invalid rate_n_flags=0x%x, band=%d\n",
				rate_n_flags, rx_status->band);
		break;
		}
	case RATE_MCS_MOD_TYPE_HT:
		rx_status->encoding = RX_ENC_HT;
		rx_status->rate_idx = RATE_HT_MCS_INDEX(rate_n_flags);
		rx_status->enc_flags |= stbc << RX_ENC_FLAG_STBC_SHIFT;
		break;
	case RATE_MCS_MOD_TYPE_VHT:
		rx_status->encoding = RX_ENC_VHT;
		iwl_mld_set_rx_nonlegacy_rate_info(rate_n_flags, rx_status);
		break;
	case RATE_MCS_MOD_TYPE_HE:
		rx_status->encoding = RX_ENC_HE;
		rx_status->he_dcm =
			!!(rate_n_flags & RATE_HE_DUAL_CARRIER_MODE_MSK);
		iwl_mld_set_rx_nonlegacy_rate_info(rate_n_flags, rx_status);
		break;
	case RATE_MCS_MOD_TYPE_EHT:
		rx_status->encoding = RX_ENC_EHT;
		iwl_mld_set_rx_nonlegacy_rate_info(rate_n_flags, rx_status);
		break;
	default:
		WARN_ON_ONCE(1);
	}

	if (format != RATE_MCS_MOD_TYPE_CCK && is_sgi)
		rx_status->enc_flags |= RX_ENC_FLAG_SHORT_GI;
}

/* Note: hdr can be NULL */
static void iwl_mld_rx_fill_status(struct iwl_mld *mld, int link_id,
				   struct ieee80211_hdr *hdr,
				   struct sk_buff *skb,
				   struct iwl_mld_rx_phy_data *phy_data)
{
	struct ieee80211_rx_status *rx_status = IEEE80211_SKB_RXCB(skb);
	u32 rate_n_flags = phy_data->rate_n_flags;
	u32 format = rate_n_flags & RATE_MCS_MOD_TYPE_MSK;

	iwl_mld_fill_signal(mld, link_id, hdr, rx_status, phy_data);

	rx_status->device_timestamp = phy_data->gp2_on_air_rise;

	iwl_mld_set_rx_rate(mld, phy_data, rx_status);

	/* must be before L-SIG data (radiotap field order) */
	if (format == RATE_MCS_MOD_TYPE_HE)
		iwl_mld_rx_he(skb, phy_data);

	iwl_mld_decode_lsig(skb, phy_data);

	/* TLVs - must be after radiotap fixed fields */
	if (format == RATE_MCS_MOD_TYPE_EHT)
		iwl_mld_rx_eht(mld, skb, phy_data);

#ifdef CONFIG_IWLWIFI_DEBUGFS
	if (unlikely(mld->monitor.on)) {
		iwl_mld_add_rtap_sniffer_config(mld, skb);

		if (mld->monitor.ptp_time) {
			u64 adj_time =
				iwl_mld_ptp_get_adj_time(mld,
							 phy_data->gp2_on_air_rise *
							 NSEC_PER_USEC);

			rx_status->mactime = div64_u64(adj_time, NSEC_PER_USEC);
			rx_status->flag |= RX_FLAG_MACTIME_IS_RTAP_TS64;
			rx_status->flag &= ~RX_FLAG_MACTIME;
		}
	}
#endif

	if (phy_data->ntfy)
		iwl_mld_add_rtap_sniffer_phy_data(mld, skb, phy_data->ntfy);
}

/* iwl_mld_create_skb adds the rxb to a new skb */
static int iwl_mld_build_rx_skb(struct iwl_mld *mld, struct sk_buff *skb,
				struct ieee80211_hdr *hdr, u16 len,
				u8 crypt_len, struct iwl_rx_cmd_buffer *rxb)
{
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	struct iwl_rx_mpdu_desc *desc = (void *)pkt->data;
	unsigned int headlen, fraglen, pad_len = 0;
	unsigned int hdrlen = ieee80211_hdrlen(hdr->frame_control);
	u8 mic_crc_len = u8_get_bits(desc->mac_flags1,
				     IWL_RX_MPDU_MFLG1_MIC_CRC_LEN_MASK) << 1;

	if (desc->mac_flags2 & IWL_RX_MPDU_MFLG2_PAD) {
		len -= 2;
		pad_len = 2;
	}

	/* For non monitor interface strip the bytes the RADA might not have
	 * removed (it might be disabled, e.g. for mgmt frames). As a monitor
	 * interface cannot exist with other interfaces, this removal is safe
	 * and sufficient, in monitor mode there's no decryption being done.
	 */
	if (len > mic_crc_len && !ieee80211_hw_check(mld->hw, RX_INCLUDES_FCS))
		len -= mic_crc_len;

	/* If frame is small enough to fit in skb->head, pull it completely.
	 * If not, only pull ieee80211_hdr (including crypto if present, and
	 * an additional 8 bytes for SNAP/ethertype, see below) so that
	 * splice() or TCP coalesce are more efficient.
	 *
	 * Since, in addition, ieee80211_data_to_8023() always pull in at
	 * least 8 bytes (possibly more for mesh) we can do the same here
	 * to save the cost of doing it later. That still doesn't pull in
	 * the actual IP header since the typical case has a SNAP header.
	 * If the latter changes (there are efforts in the standards group
	 * to do so) we should revisit this and ieee80211_data_to_8023().
	 */
	headlen = (len <= skb_tailroom(skb)) ? len : hdrlen + crypt_len + 8;

	/* The firmware may align the packet to DWORD.
	 * The padding is inserted after the IV.
	 * After copying the header + IV skip the padding if
	 * present before copying packet data.
	 */
	hdrlen += crypt_len;

	if (unlikely(headlen < hdrlen))
		return -EINVAL;

	/* Since data doesn't move data while putting data on skb and that is
	 * the only way we use, data + len is the next place that hdr would
	 * be put
	 */
	skb_set_mac_header(skb, skb->len);
	skb_put_data(skb, hdr, hdrlen);
	skb_put_data(skb, (u8 *)hdr + hdrlen + pad_len, headlen - hdrlen);

	if (skb->ip_summed == CHECKSUM_COMPLETE) {
		struct {
			u8 hdr[6];
			__be16 type;
		} __packed *shdr = (void *)((u8 *)hdr + hdrlen + pad_len);

		if (unlikely(headlen - hdrlen < sizeof(*shdr) ||
			     !ether_addr_equal(shdr->hdr, rfc1042_header) ||
			     (shdr->type != htons(ETH_P_IP) &&
			      shdr->type != htons(ETH_P_ARP) &&
			      shdr->type != htons(ETH_P_IPV6) &&
			      shdr->type != htons(ETH_P_8021Q) &&
			      shdr->type != htons(ETH_P_PAE) &&
			      shdr->type != htons(ETH_P_TDLS))))
			skb->ip_summed = CHECKSUM_NONE;
	}

	fraglen = len - headlen;

	if (fraglen) {
		int offset = (u8 *)hdr + headlen + pad_len -
			     (u8 *)rxb_addr(rxb) + rxb_offset(rxb);

		skb_add_rx_frag(skb, 0, rxb_steal_page(rxb), offset,
				fraglen, rxb->truesize);
	}

	return 0;
}

/* returns true if a packet is a duplicate or invalid tid and
 * should be dropped. Updates AMSDU PN tracking info
 */
VISIBLE_IF_IWLWIFI_KUNIT
bool
iwl_mld_is_dup(struct iwl_mld *mld, struct ieee80211_sta *sta,
	       struct ieee80211_hdr *hdr,
	       const struct iwl_rx_mpdu_desc *mpdu_desc,
	       struct ieee80211_rx_status *rx_status, int queue)
{
	struct iwl_mld_sta *mld_sta;
	struct iwl_mld_rxq_dup_data *dup_data;
	u8 tid, sub_frame_idx;

	if (WARN_ON(!sta))
		return false;

	mld_sta = iwl_mld_sta_from_mac80211(sta);

	if (WARN_ON_ONCE(!mld_sta->dup_data))
		return false;

	dup_data = &mld_sta->dup_data[queue];

	/* Drop duplicate 802.11 retransmissions
	 * (IEEE 802.11-2020: 10.3.2.14 "Duplicate detection and recovery")
	 */
	if (ieee80211_is_ctl(hdr->frame_control) ||
	    ieee80211_is_any_nullfunc(hdr->frame_control) ||
	    is_multicast_ether_addr(hdr->addr1))
		return false;

	if (ieee80211_is_data_qos(hdr->frame_control)) {
		/* frame has qos control */
		tid = ieee80211_get_tid(hdr);
		if (tid >= IWL_MAX_TID_COUNT)
			return true;
	} else {
		tid = IWL_MAX_TID_COUNT;
	}

	/* If this wasn't a part of an A-MSDU the sub-frame index will be 0 */
	sub_frame_idx = mpdu_desc->amsdu_info &
		IWL_RX_MPDU_AMSDU_SUBFRAME_IDX_MASK;

	if (IWL_FW_CHECK(mld,
			 sub_frame_idx > 0 &&
			 !(mpdu_desc->mac_flags2 & IWL_RX_MPDU_MFLG2_AMSDU),
			 "got sub_frame_idx=%d but A-MSDU flag is not set\n",
			 sub_frame_idx))
		return true;

	if (unlikely(ieee80211_has_retry(hdr->frame_control) &&
		     dup_data->last_seq[tid] == hdr->seq_ctrl &&
		     dup_data->last_sub_frame_idx[tid] >= sub_frame_idx))
		return true;

	/* Allow same PN as the first subframe for following sub frames */
	if (dup_data->last_seq[tid] == hdr->seq_ctrl &&
	    sub_frame_idx > dup_data->last_sub_frame_idx[tid])
		rx_status->flag |= RX_FLAG_ALLOW_SAME_PN;

	dup_data->last_seq[tid] = hdr->seq_ctrl;
	dup_data->last_sub_frame_idx[tid] = sub_frame_idx;

	rx_status->flag |= RX_FLAG_DUP_VALIDATED;

	return false;
}
EXPORT_SYMBOL_IF_IWLWIFI_KUNIT(iwl_mld_is_dup);

static void iwl_mld_update_last_rx_timestamp(struct iwl_mld *mld, u8 baid)
{
	unsigned long now = jiffies;
	unsigned long timeout;
	struct iwl_mld_baid_data *ba_data;

	ba_data = rcu_dereference(mld->fw_id_to_ba[baid]);
	if (!ba_data) {
		IWL_DEBUG_HT(mld, "BAID %d not found in map\n", baid);
		return;
	}

	if (!ba_data->timeout)
		return;

	/* To minimize cache bouncing between RX queues, avoid frequent updates
	 * to last_rx_timestamp. update it only when the timeout period has
	 * passed. The worst-case scenario is the session expiring after
	 * approximately 2 * timeout, which is negligible (the update is
	 * atomic).
	 */
	timeout = TU_TO_JIFFIES(ba_data->timeout);
	if (time_is_before_jiffies(ba_data->last_rx_timestamp + timeout))
		ba_data->last_rx_timestamp = now;
}

/* Processes received packets for a station.
 * Sets *drop to true if the packet should be dropped.
 * Returns the station if found, or NULL otherwise.
 */
static struct ieee80211_sta *
iwl_mld_rx_with_sta(struct iwl_mld *mld, struct ieee80211_hdr *hdr,
		    struct sk_buff *skb,
		    const struct iwl_rx_mpdu_desc *mpdu_desc,
		    const struct iwl_rx_packet *pkt, int queue, bool *drop)
{
	struct ieee80211_sta *sta = NULL;
	struct ieee80211_link_sta *link_sta = NULL;
	struct ieee80211_rx_status *rx_status;
	u8 baid;

	if (mpdu_desc->status & cpu_to_le32(IWL_RX_MPDU_STATUS_SRC_STA_FOUND)) {
		u8 sta_id = le32_get_bits(mpdu_desc->status,
					  IWL_RX_MPDU_STATUS_STA_ID);

		if (IWL_FW_CHECK(mld,
				 sta_id >= mld->fw->ucode_capa.num_stations,
				 "rx_mpdu: invalid sta_id %d\n", sta_id))
			return NULL;

		link_sta = rcu_dereference(mld->fw_id_to_link_sta[sta_id]);
		if (!IS_ERR_OR_NULL(link_sta))
			sta = link_sta->sta;
	} else if (!is_multicast_ether_addr(hdr->addr2)) {
		/* Passing NULL is fine since we prevent two stations with the
		 * same address from being added.
		 */
		sta = ieee80211_find_sta_by_ifaddr(mld->hw, hdr->addr2, NULL);
	}

	/* we may not have any station yet */
	if (!sta)
		return NULL;

	rx_status = IEEE80211_SKB_RXCB(skb);

	if (link_sta && sta->valid_links) {
		rx_status->link_valid = true;
		rx_status->link_id = link_sta->link_id;
	}

	/* fill checksum */
	if (ieee80211_is_data(hdr->frame_control) &&
	    pkt->len_n_flags & cpu_to_le32(FH_RSCSR_RPA_EN)) {
		u16 hwsum = be16_to_cpu(mpdu_desc->v3.raw_xsum);

		skb->ip_summed = CHECKSUM_COMPLETE;
		skb->csum = csum_unfold(~(__force __sum16)hwsum);
	}

	if (iwl_mld_is_dup(mld, sta, hdr, mpdu_desc, rx_status, queue)) {
		IWL_DEBUG_DROP(mld, "Dropping duplicate packet 0x%x\n",
			       le16_to_cpu(hdr->seq_ctrl));
		*drop = true;
		return NULL;
	}

	baid = le32_get_bits(mpdu_desc->reorder_data,
			     IWL_RX_MPDU_REORDER_BAID_MASK);
	if (baid != IWL_RX_REORDER_DATA_INVALID_BAID)
		iwl_mld_update_last_rx_timestamp(mld, baid);

	if (link_sta && ieee80211_is_data(hdr->frame_control)) {
		u8 sub_frame_idx = mpdu_desc->amsdu_info &
			IWL_RX_MPDU_AMSDU_SUBFRAME_IDX_MASK;

		/* 0 means not an A-MSDU, and 1 means a new A-MSDU */
		if (!sub_frame_idx || sub_frame_idx == 1)
			iwl_mld_count_mpdu_rx(link_sta, queue, 1);

		if (!is_multicast_ether_addr(hdr->addr1))
			iwl_mld_low_latency_update_counters(mld, hdr, sta,
							    queue);
	}

	return sta;
}

static int iwl_mld_rx_mgmt_prot(struct ieee80211_sta *sta,
				struct ieee80211_hdr *hdr,
				struct ieee80211_rx_status *rx_status,
				u32 mpdu_status,
				u32 mpdu_len)
{
	struct iwl_mld_link *link;
	struct wireless_dev *wdev;
	struct iwl_mld_sta *mld_sta;
	struct iwl_mld_vif *mld_vif;
	u8 keyidx;
	struct ieee80211_key_conf *key;
	const u8 *frame = (void *)hdr;
	const u8 *mmie;
	u8 link_id;

	if ((mpdu_status & IWL_RX_MPDU_STATUS_SEC_MASK) ==
	     IWL_RX_MPDU_STATUS_SEC_NONE)
		return 0;

	/* For non-beacon, we don't really care. But beacons may
	 * be filtered out, and we thus need the firmware's replay
	 * detection, otherwise beacons the firmware previously
	 * filtered could be replayed, or something like that, and
	 * it can filter a lot - though usually only if nothing has
	 * changed.
	 */
	if (!ieee80211_is_beacon(hdr->frame_control))
		return 0;

	if (!sta)
		return -1;

	mld_sta = iwl_mld_sta_from_mac80211(sta);
	mld_vif = iwl_mld_vif_from_mac80211(mld_sta->vif);

	/* key mismatch - will also report !MIC_OK but we shouldn't count it */
	if (!(mpdu_status & IWL_RX_MPDU_STATUS_KEY_VALID))
		goto report;

	/* good cases */
	if (likely(mpdu_status & IWL_RX_MPDU_STATUS_MIC_OK &&
		   !(mpdu_status & IWL_RX_MPDU_STATUS_REPLAY_ERROR))) {
		rx_status->flag |= RX_FLAG_DECRYPTED;
		return 0;
	}

	link_id = rx_status->link_valid ? rx_status->link_id : 0;
	link = rcu_dereference(mld_vif->link[link_id]);
	if (WARN_ON_ONCE(!link))
		return -1;

	/* both keys will have the same cipher and MIC length, use
	 * whichever one is available
	 */
	key = rcu_dereference(link->bigtks[0]);
	if (!key) {
		key = rcu_dereference(link->bigtks[1]);
		if (!key)
			goto report;
	}

	/* get the real key ID */
	if (mpdu_len < key->icv_len)
		goto report;

	mmie = frame + (mpdu_len - key->icv_len);

	/* the position of the key_id in ieee80211_mmie_16 is the same */
	keyidx = le16_to_cpu(((const struct ieee80211_mmie *) mmie)->key_id);

	/* and if that's the other key, look it up */
	if (keyidx != key->keyidx) {
		/* shouldn't happen since firmware checked, but be safe
		 * in case the MIC length is wrong too, for example
		 */
		if (keyidx != 6 && keyidx != 7)
			return -1;

		key = rcu_dereference(link->bigtks[keyidx - 6]);
		if (!key)
			goto report;
	}

	/* Report status to mac80211 */
	if (!(mpdu_status & IWL_RX_MPDU_STATUS_MIC_OK))
		ieee80211_key_mic_failure(key);
	else if (mpdu_status & IWL_RX_MPDU_STATUS_REPLAY_ERROR)
		ieee80211_key_replay(key);
report:
	wdev = ieee80211_vif_to_wdev(mld_sta->vif);
	if (wdev->netdev)
		cfg80211_rx_unprot_mlme_mgmt(wdev->netdev, (void *)hdr,
					     mpdu_len);

	return -1;
}

static int iwl_mld_rx_crypto(struct iwl_mld *mld,
			     struct ieee80211_sta *sta,
			     struct ieee80211_hdr *hdr,
			     struct ieee80211_rx_status *rx_status,
			     struct iwl_rx_mpdu_desc *desc, int queue,
			     u32 pkt_flags, u8 *crypto_len)
{
	u32 status = le32_to_cpu(desc->status);

	if (unlikely(ieee80211_is_mgmt(hdr->frame_control) &&
		     !ieee80211_has_protected(hdr->frame_control)))
		return iwl_mld_rx_mgmt_prot(sta, hdr, rx_status, status,
					    le16_to_cpu(desc->mpdu_len));

	if (!ieee80211_has_protected(hdr->frame_control) ||
	    (status & IWL_RX_MPDU_STATUS_SEC_MASK) ==
	    IWL_RX_MPDU_STATUS_SEC_NONE)
		return 0;

	switch (status & IWL_RX_MPDU_STATUS_SEC_MASK) {
	case IWL_RX_MPDU_STATUS_SEC_CCM:
	case IWL_RX_MPDU_STATUS_SEC_GCM:
		BUILD_BUG_ON(IEEE80211_CCMP_PN_LEN != IEEE80211_GCMP_PN_LEN);
		if (!(status & IWL_RX_MPDU_STATUS_MIC_OK)) {
			IWL_DEBUG_DROP(mld,
				       "Dropping packet, bad MIC (CCM/GCM)\n");
			return -1;
		}

		rx_status->flag |= RX_FLAG_DECRYPTED | RX_FLAG_MIC_STRIPPED;
		*crypto_len = IEEE80211_CCMP_HDR_LEN;
		return 0;
	case IWL_RX_MPDU_STATUS_SEC_TKIP:
		if (!(status & IWL_RX_MPDU_STATUS_ICV_OK))
			return -1;

		if (!(status & RX_MPDU_RES_STATUS_MIC_OK))
			rx_status->flag |= RX_FLAG_MMIC_ERROR;

		if (pkt_flags & FH_RSCSR_RADA_EN) {
			rx_status->flag |= RX_FLAG_ICV_STRIPPED;
			rx_status->flag |= RX_FLAG_MMIC_STRIPPED;
		}

		*crypto_len = IEEE80211_TKIP_IV_LEN;
		rx_status->flag |= RX_FLAG_DECRYPTED;
		return 0;
	default:
		break;
	}

	return 0;
}

static void iwl_mld_rx_update_ampdu_data(struct iwl_mld *mld,
					 struct iwl_mld_rx_phy_data *phy_data,
					 struct ieee80211_rx_status *rx_status)
{
	u32 format = phy_data->rate_n_flags & RATE_MCS_MOD_TYPE_MSK;
	bool toggle_bit =
		phy_data->phy_info & IWL_RX_MPDU_PHY_AMPDU_TOGGLE;

	switch (format) {
	case RATE_MCS_MOD_TYPE_CCK:
	case RATE_MCS_MOD_TYPE_LEGACY_OFDM:
		/* no aggregation possible */
		return;
	case RATE_MCS_MOD_TYPE_HT:
	case RATE_MCS_MOD_TYPE_VHT:
		/* single frames are not A-MPDU format */
		if (!(phy_data->phy_info & IWL_RX_MPDU_PHY_AMPDU))
			return;
		break;
	default:
		/* HE/EHT/UHR have A-MPDU format for single frames */
		if (!(phy_data->phy_info & IWL_RX_MPDU_PHY_AMPDU)) {
			rx_status->flag |= RX_FLAG_AMPDU_DETAILS;
			rx_status->flag |= RX_FLAG_AMPDU_EOF_BIT_KNOWN;
			if (phy_data->phy_info & IWL_RX_MPDU_PHY_EOF_INDICATION)
				rx_status->flag |= RX_FLAG_AMPDU_EOF_BIT;
			return;
		}
	}

	rx_status->flag |= RX_FLAG_AMPDU_DETAILS;
	/* Toggle is switched whenever new aggregation starts. Make
	 * sure ampdu_reference is never 0 so we can later use it to
	 * see if the frame was really part of an A-MPDU or not.
	 */
	if (toggle_bit != mld->monitor.ampdu_toggle) {
		mld->monitor.ampdu_ref++;
		if (mld->monitor.ampdu_ref == 0)
			mld->monitor.ampdu_ref++;
		mld->monitor.ampdu_toggle = toggle_bit;
		phy_data->first_subframe = true;

		/* report EOF bit on the first subframe */
		rx_status->flag |= RX_FLAG_AMPDU_EOF_BIT_KNOWN;
		if (phy_data->phy_info & IWL_RX_MPDU_PHY_EOF_INDICATION)
			rx_status->flag |= RX_FLAG_AMPDU_EOF_BIT;
	}
	rx_status->ampdu_reference = mld->monitor.ampdu_ref;
}

static void
iwl_mld_fill_rx_status_band_freq(struct ieee80211_rx_status *rx_status,
				 u8 band, u8 channel)
{
	rx_status->band = iwl_mld_phy_band_to_nl80211(band);
	rx_status->freq = ieee80211_channel_to_frequency(channel,
							 rx_status->band);
}

void iwl_mld_rx_mpdu(struct iwl_mld *mld, struct napi_struct *napi,
		     struct iwl_rx_cmd_buffer *rxb, int queue)
{
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	struct iwl_mld_rx_phy_data phy_data = {};
	struct iwl_rx_mpdu_desc *mpdu_desc = (void *)pkt->data;
	struct ieee80211_sta *sta;
	struct ieee80211_hdr *hdr;
	struct sk_buff *skb;
	size_t mpdu_desc_size = sizeof(*mpdu_desc);
	bool drop = false;
	u8 crypto_len = 0, band, link_id;
	u32 pkt_len = iwl_rx_packet_payload_len(pkt);
	u32 mpdu_len;
	enum iwl_mld_reorder_result reorder_res;
	struct ieee80211_rx_status *rx_status;
	unsigned int alloc_size = 128;

	if (unlikely(mld->fw_status.in_hw_restart))
		return;

	if (IWL_FW_CHECK(mld, pkt_len < mpdu_desc_size,
			 "Bad REPLY_RX_MPDU_CMD size (%d)\n", pkt_len))
		return;

	mpdu_len = le16_to_cpu(mpdu_desc->mpdu_len);

	if (IWL_FW_CHECK(mld, mpdu_len + mpdu_desc_size > pkt_len,
			 "FW lied about packet len (%d)\n", pkt_len))
		return;

	iwl_mld_fill_phy_data_from_mpdu(mld, mpdu_desc, &phy_data);

	/* Don't use dev_alloc_skb(), we'll have enough headroom once
	 * ieee80211_hdr pulled.
	 *
	 * For monitor mode we need more space to include the full PHY
	 * notification data.
	 */
	if (unlikely(mld->monitor.on) && phy_data.ntfy)
		alloc_size += sizeof(struct iwl_rx_phy_air_sniffer_ntfy);
	skb = alloc_skb(alloc_size, GFP_ATOMIC);
	if (!skb) {
		IWL_ERR(mld, "alloc_skb failed\n");
		return;
	}

	hdr = (void *)(pkt->data + mpdu_desc_size);

	if (mpdu_desc->mac_flags2 & IWL_RX_MPDU_MFLG2_PAD) {
		/* If the device inserted padding it means that (it thought)
		 * the 802.11 header wasn't a multiple of 4 bytes long. In
		 * this case, reserve two bytes at the start of the SKB to
		 * align the payload properly in case we end up copying it.
		 */
		skb_reserve(skb, 2);
	}

	rx_status = IEEE80211_SKB_RXCB(skb);

	/* this is needed early */
	band = u8_get_bits(mpdu_desc->mac_phy_band,
			   IWL_RX_MPDU_MAC_PHY_BAND_BAND_MASK);
	iwl_mld_fill_rx_status_band_freq(rx_status, band,
					 mpdu_desc->v3.channel);


	rcu_read_lock();

	sta = iwl_mld_rx_with_sta(mld, hdr, skb, mpdu_desc, pkt, queue, &drop);
	if (drop)
		goto drop;

	if (unlikely(mld->monitor.on))
		iwl_mld_rx_update_ampdu_data(mld, &phy_data, rx_status);

	/* Keep packets with CRC errors (and with overrun) for monitor mode
	 * (otherwise the firmware discards them) but mark them as bad.
	 */
	if (!(mpdu_desc->status & cpu_to_le32(IWL_RX_MPDU_STATUS_CRC_OK)) ||
	    !(mpdu_desc->status & cpu_to_le32(IWL_RX_MPDU_STATUS_OVERRUN_OK))) {
		IWL_DEBUG_RX(mld, "Bad CRC or FIFO: 0x%08X.\n",
			     le32_to_cpu(mpdu_desc->status));
		rx_status->flag |= RX_FLAG_FAILED_FCS_CRC;
	}

	if (likely(!(phy_data.phy_info & IWL_RX_MPDU_PHY_TSF_OVERLOAD))) {
		rx_status->mactime =
			le64_to_cpu(mpdu_desc->v3.tsf_on_air_rise);

		/* TSF as indicated by the firmware is at INA time */
		rx_status->flag |= RX_FLAG_MACTIME_PLCP_START;
	}

	/* management stuff on default queue */
	if (!queue && unlikely(ieee80211_is_beacon(hdr->frame_control) ||
			       ieee80211_is_probe_resp(hdr->frame_control))) {
		rx_status->boottime_ns = ktime_get_boottime_ns();

		if (mld->scan.pass_all_sched_res ==
				SCHED_SCAN_PASS_ALL_STATE_ENABLED)
			mld->scan.pass_all_sched_res =
				SCHED_SCAN_PASS_ALL_STATE_FOUND;
	}

	link_id = u8_get_bits(mpdu_desc->mac_phy_band,
			      IWL_RX_MPDU_MAC_PHY_BAND_LINK_MASK);

	iwl_mld_rx_fill_status(mld, link_id, hdr, skb, &phy_data);

	if (iwl_mld_rx_crypto(mld, sta, hdr, rx_status, mpdu_desc, queue,
			      le32_to_cpu(pkt->len_n_flags), &crypto_len))
		goto drop;

	if (iwl_mld_build_rx_skb(mld, skb, hdr, mpdu_len, crypto_len, rxb))
		goto drop;

	/* time sync frame is saved and will be released later when the
	 * notification with the timestamps arrives.
	 */
	if (iwl_mld_time_sync_frame(mld, skb, hdr->addr2))
		goto out;

	reorder_res = iwl_mld_reorder(mld, napi, queue, sta, skb, mpdu_desc);
	switch (reorder_res) {
	case IWL_MLD_PASS_SKB:
		break;
	case IWL_MLD_DROP_SKB:
		goto drop;
	case IWL_MLD_BUFFERED_SKB:
		goto out;
	default:
		WARN_ON(1);
		goto drop;
	}

	iwl_mld_pass_packet_to_mac80211(mld, napi, skb, queue, sta);

	goto out;

drop:
	kfree_skb(skb);
out:
	rcu_read_unlock();
}

#define SYNC_RX_QUEUE_TIMEOUT (HZ)
void iwl_mld_sync_rx_queues(struct iwl_mld *mld,
			    enum iwl_mld_internal_rxq_notif_type type,
			    const void *notif_payload, u32 notif_payload_size)
{
	u8 num_rx_queues = mld->trans->info.num_rxqs;
	struct {
		struct iwl_rxq_sync_cmd sync_cmd;
		struct iwl_mld_internal_rxq_notif notif;
	} __packed cmd = {
		.sync_cmd.rxq_mask = cpu_to_le32(BIT(num_rx_queues) - 1),
		.sync_cmd.count =
			cpu_to_le32(sizeof(struct iwl_mld_internal_rxq_notif) +
				    notif_payload_size),
		.notif.type = type,
		.notif.cookie = mld->rxq_sync.cookie,
	};
	struct iwl_host_cmd hcmd = {
		.id = WIDE_ID(DATA_PATH_GROUP, TRIGGER_RX_QUEUES_NOTIF_CMD),
		.data[0] = &cmd,
		.len[0] = sizeof(cmd),
		.data[1] = notif_payload,
		.len[1] = notif_payload_size,
	};
	int ret;

	/* size must be a multiple of DWORD */
	if (WARN_ON(cmd.sync_cmd.count & cpu_to_le32(3)))
		return;

	mld->rxq_sync.state = (1 << num_rx_queues) - 1;

	ret = iwl_mld_send_cmd(mld, &hcmd);
	if (ret) {
		IWL_ERR(mld, "Failed to trigger RX queues sync (%d)\n", ret);
		goto out;
	}

	ret = wait_event_timeout(mld->rxq_sync.waitq,
				 READ_ONCE(mld->rxq_sync.state) == 0,
				 SYNC_RX_QUEUE_TIMEOUT);
	WARN_ONCE(!ret, "RXQ sync failed: state=0x%lx, cookie=%d\n",
		  mld->rxq_sync.state, mld->rxq_sync.cookie);

out:
	mld->rxq_sync.state = 0;
	mld->rxq_sync.cookie++;
}

void iwl_mld_handle_rx_queues_sync_notif(struct iwl_mld *mld,
					 struct napi_struct *napi,
					 struct iwl_rx_packet *pkt, int queue)
{
	struct iwl_rxq_sync_notification *notif;
	struct iwl_mld_internal_rxq_notif *internal_notif;
	u32 len = iwl_rx_packet_payload_len(pkt);
	size_t combined_notif_len = sizeof(*notif) + sizeof(*internal_notif);

	notif = (void *)pkt->data;
	internal_notif = (void *)notif->payload;

	if (IWL_FW_CHECK(mld, len < combined_notif_len,
			 "invalid notification size %u (%zu)\n",
			 len, combined_notif_len))
		return;

	len -= combined_notif_len;

	if (IWL_FW_CHECK(mld, mld->rxq_sync.cookie != internal_notif->cookie,
			 "received expired RX queue sync message (cookie=%d expected=%d q[%d])\n",
			 internal_notif->cookie, mld->rxq_sync.cookie, queue))
		return;

	switch (internal_notif->type) {
	case IWL_MLD_RXQ_EMPTY:
		IWL_FW_CHECK(mld, len,
			     "invalid empty notification size %d\n", len);
		break;
	case IWL_MLD_RXQ_NOTIF_DEL_BA:
		if (IWL_FW_CHECK(mld, len != sizeof(struct iwl_mld_delba_data),
				 "invalid delba notification size %u (%zu)\n",
				 len, sizeof(struct iwl_mld_delba_data)))
			break;
		iwl_mld_del_ba(mld, queue, (void *)internal_notif->payload);
		break;
	default:
		WARN_ON_ONCE(1);
	}

	IWL_FW_CHECK(mld, !test_and_clear_bit(queue, &mld->rxq_sync.state),
		     "RXQ sync: queue %d responded a second time!\n", queue);

	if (READ_ONCE(mld->rxq_sync.state) == 0)
		wake_up(&mld->rxq_sync.waitq);
}

static void iwl_mld_no_data_rx(struct iwl_mld *mld,
			       struct napi_struct *napi,
			       struct iwl_rx_phy_air_sniffer_ntfy *ntfy)
{
	struct ieee80211_rx_status *rx_status;
	struct iwl_mld_rx_phy_data phy_data = {
		.ntfy = ntfy,
		.phy_info = 0, /* short preamble set below */
		.rate_n_flags = le32_to_cpu(ntfy->rate),
		.gp2_on_air_rise = le32_to_cpu(ntfy->on_air_rise_time),
		.energy_a = ntfy->rssi_a,
		.energy_b = ntfy->rssi_b,
	};
	u32 format = phy_data.rate_n_flags & RATE_MCS_MOD_TYPE_MSK;
	struct sk_buff *skb;

	skb = alloc_skb(128 + sizeof(struct iwl_rx_phy_air_sniffer_ntfy),
			GFP_ATOMIC);
	if (!skb)
		return;

	rx_status = IEEE80211_SKB_RXCB(skb);

	/* 0-length PSDU */
	rx_status->flag |= RX_FLAG_NO_PSDU;

	switch (ntfy->status) {
	case IWL_SNIF_STAT_PLCP_RX_OK:
		/* we only get here with sounding PPDUs */
		rx_status->zero_length_psdu_type =
			IEEE80211_RADIOTAP_ZERO_LEN_PSDU_SOUNDING;
		break;
	case IWL_SNIF_STAT_AID_NOT_FOR_US:
		rx_status->zero_length_psdu_type =
			IEEE80211_RADIOTAP_ZERO_LEN_PSDU_NOT_CAPTURED;
		break;
	case IWL_SNIF_STAT_PLCP_RX_LSIG_ERR:
	case IWL_SNIF_STAT_PLCP_RX_SIGA_ERR:
	case IWL_SNIF_STAT_PLCP_RX_SIGB_ERR:
	case IWL_SNIF_STAT_UNKNOWN_ERROR:
	default:
		rx_status->flag |= RX_FLAG_FAILED_PLCP_CRC;
		fallthrough;
	case IWL_SNIF_STAT_UNEXPECTED_TB:
	case IWL_SNIF_STAT_UNSUPPORTED_RATE:
		rx_status->zero_length_psdu_type =
			IEEE80211_RADIOTAP_ZERO_LEN_PSDU_VENDOR;
		/* we could include the real reason in a vendor TLV */
	}

	if (format == RATE_MCS_MOD_TYPE_CCK &&
	    ntfy->legacy_sig.cck & cpu_to_le32(CCK_CRFR_SHORT_PREAMBLE))
		phy_data.phy_info |= IWL_RX_MPDU_PHY_SHORT_PREAMBLE;

	iwl_mld_fill_rx_status_band_freq(IEEE80211_SKB_RXCB(skb),
					 ntfy->band, ntfy->channel);

	/* link ID is ignored for NULL header */
	iwl_mld_rx_fill_status(mld, -1, NULL, skb, &phy_data);

	/* No more radiotap info should be added after this point.
	 * Mark it as mac header for upper layers to know where
	 * the radiotap header ends.
	 */
	skb_set_mac_header(skb, skb->len);

	/* pass the packet to mac80211 */
	rcu_read_lock();
	ieee80211_rx_napi(mld->hw, NULL, skb, napi);
	rcu_read_unlock();
}

void iwl_mld_handle_phy_air_sniffer_notif(struct iwl_mld *mld,
					  struct napi_struct *napi,
					  struct iwl_rx_packet *pkt)
{
	struct iwl_rx_phy_air_sniffer_ntfy *ntfy = (void *)pkt->data;
	bool is_ndp = false;
	u32 he_type;

	if (IWL_FW_CHECK(mld, iwl_rx_packet_payload_len(pkt) < sizeof(*ntfy),
			 "invalid air sniffer notification size\n"))
		return;

	/* check if there's an old one to release as errored */
	if (mld->monitor.phy.valid && !mld->monitor.phy.used) {
		/* didn't capture data, so override status */
		mld->monitor.phy.data.status = IWL_SNIF_STAT_AID_NOT_FOR_US;
		iwl_mld_no_data_rx(mld, napi, &mld->monitor.phy.data);
	}

	/* old data is no longer valid now */
	mld->monitor.phy.valid = false;

	he_type = le32_to_cpu(ntfy->rate) & RATE_MCS_HE_TYPE_MSK;

	switch (le32_to_cpu(ntfy->rate) & RATE_MCS_MOD_TYPE_MSK) {
	case RATE_MCS_MOD_TYPE_HT:
		is_ndp = !le32_get_bits(ntfy->sigs.ht.a1,
					OFDM_RX_FRAME_HT_LENGTH);
		break;
	case RATE_MCS_MOD_TYPE_VHT:
		is_ndp = le32_get_bits(ntfy->sigs.vht.a0,
				       OFDM_RX_FRAME_VHT_NUM_OF_DATA_SYM_VALID) &&
			 !le32_get_bits(ntfy->sigs.vht.a0,
					OFDM_RX_FRAME_VHT_NUM_OF_DATA_SYM);
		break;
	case RATE_MCS_MOD_TYPE_HE:
		if (he_type == RATE_MCS_HE_TYPE_TRIG)
			break;
		is_ndp = le32_get_bits(ntfy->sigs.he.a3,
				       OFDM_RX_FRAME_HE_NUM_OF_DATA_SYM_VALID) &&
			 !le32_get_bits(ntfy->sigs.he.a3,
					OFDM_RX_FRAME_HE_NUM_OF_DATA_SYM);
		break;
	case RATE_MCS_MOD_TYPE_EHT:
		if (he_type == RATE_MCS_HE_TYPE_TRIG)
			break;
		is_ndp = le32_get_bits(ntfy->sigs.eht.sig2,
				       OFDM_RX_FRAME_EHT_NUM_OF_DATA_SYM_VALID) &&
			 !le32_get_bits(ntfy->sigs.eht.sig2,
					OFDM_RX_FRAME_EHT_NUM_OF_DATA_SYM);
		break;
	}

	if (ntfy->status != IWL_SNIF_STAT_PLCP_RX_OK || is_ndp) {
		iwl_mld_no_data_rx(mld, napi, ntfy);
		return;
	}

	/* hang on to it for the RX_MPDU data packet(s) */
	mld->monitor.phy.data = *ntfy;
	mld->monitor.phy.valid = true;
	mld->monitor.phy.used = false;
}
