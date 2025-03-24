// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2019-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2025 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "dp_mon.h"
#include "debug.h"
#include "dp_rx.h"
#include "dp_tx.h"
#include "peer.h"

#define ATH12K_LE32_DEC_ENC(value, dec_bits, enc_bits)	\
		u32_encode_bits(le32_get_bits(value, dec_bits), enc_bits)

#define ATH12K_LE64_DEC_ENC(value, dec_bits, enc_bits) \
		u32_encode_bits(le64_get_bits(value, dec_bits), enc_bits)

static void
ath12k_dp_mon_rx_handle_ofdma_info(const struct hal_rx_ppdu_end_user_stats *ppdu_end_user,
				   struct hal_rx_user_status *rx_user_status)
{
	rx_user_status->ul_ofdma_user_v0_word0 =
		__le32_to_cpu(ppdu_end_user->usr_resp_ref);
	rx_user_status->ul_ofdma_user_v0_word1 =
		__le32_to_cpu(ppdu_end_user->usr_resp_ref_ext);
}

static void
ath12k_dp_mon_rx_populate_byte_count(const struct hal_rx_ppdu_end_user_stats *stats,
				     void *ppduinfo,
				     struct hal_rx_user_status *rx_user_status)
{
	rx_user_status->mpdu_ok_byte_count =
		le32_get_bits(stats->info7,
			      HAL_RX_PPDU_END_USER_STATS_INFO7_MPDU_OK_BYTE_COUNT);
	rx_user_status->mpdu_err_byte_count =
		le32_get_bits(stats->info8,
			      HAL_RX_PPDU_END_USER_STATS_INFO8_MPDU_ERR_BYTE_COUNT);
}

static void
ath12k_dp_mon_rx_populate_mu_user_info(const struct hal_rx_ppdu_end_user_stats *rx_tlv,
				       struct hal_rx_mon_ppdu_info *ppdu_info,
				       struct hal_rx_user_status *rx_user_status)
{
	rx_user_status->ast_index = ppdu_info->ast_index;
	rx_user_status->tid = ppdu_info->tid;
	rx_user_status->tcp_ack_msdu_count =
		ppdu_info->tcp_ack_msdu_count;
	rx_user_status->tcp_msdu_count =
		ppdu_info->tcp_msdu_count;
	rx_user_status->udp_msdu_count =
		ppdu_info->udp_msdu_count;
	rx_user_status->other_msdu_count =
		ppdu_info->other_msdu_count;
	rx_user_status->frame_control = ppdu_info->frame_control;
	rx_user_status->frame_control_info_valid =
		ppdu_info->frame_control_info_valid;
	rx_user_status->data_sequence_control_info_valid =
		ppdu_info->data_sequence_control_info_valid;
	rx_user_status->first_data_seq_ctrl =
		ppdu_info->first_data_seq_ctrl;
	rx_user_status->preamble_type = ppdu_info->preamble_type;
	rx_user_status->ht_flags = ppdu_info->ht_flags;
	rx_user_status->vht_flags = ppdu_info->vht_flags;
	rx_user_status->he_flags = ppdu_info->he_flags;
	rx_user_status->rs_flags = ppdu_info->rs_flags;

	rx_user_status->mpdu_cnt_fcs_ok =
		ppdu_info->num_mpdu_fcs_ok;
	rx_user_status->mpdu_cnt_fcs_err =
		ppdu_info->num_mpdu_fcs_err;
	memcpy(&rx_user_status->mpdu_fcs_ok_bitmap[0], &ppdu_info->mpdu_fcs_ok_bitmap[0],
	       HAL_RX_NUM_WORDS_PER_PPDU_BITMAP *
	       sizeof(ppdu_info->mpdu_fcs_ok_bitmap[0]));

	ath12k_dp_mon_rx_populate_byte_count(rx_tlv, ppdu_info, rx_user_status);
}

static void ath12k_dp_mon_parse_vht_sig_a(const struct hal_rx_vht_sig_a_info *vht_sig,
					  struct hal_rx_mon_ppdu_info *ppdu_info)
{
	u32 nsts, info0, info1;
	u8 gi_setting;

	info0 = __le32_to_cpu(vht_sig->info0);
	info1 = __le32_to_cpu(vht_sig->info1);

	ppdu_info->ldpc = u32_get_bits(info1, HAL_RX_VHT_SIG_A_INFO_INFO1_SU_MU_CODING);
	ppdu_info->mcs = u32_get_bits(info1, HAL_RX_VHT_SIG_A_INFO_INFO1_MCS);
	gi_setting = u32_get_bits(info1, HAL_RX_VHT_SIG_A_INFO_INFO1_GI_SETTING);
	switch (gi_setting) {
	case HAL_RX_VHT_SIG_A_NORMAL_GI:
		ppdu_info->gi = HAL_RX_GI_0_8_US;
		break;
	case HAL_RX_VHT_SIG_A_SHORT_GI:
	case HAL_RX_VHT_SIG_A_SHORT_GI_AMBIGUITY:
		ppdu_info->gi = HAL_RX_GI_0_4_US;
		break;
	}

	ppdu_info->is_stbc = u32_get_bits(info0, HAL_RX_VHT_SIG_A_INFO_INFO0_STBC);
	nsts = u32_get_bits(info0, HAL_RX_VHT_SIG_A_INFO_INFO0_NSTS);
	if (ppdu_info->is_stbc && nsts > 0)
		nsts = ((nsts + 1) >> 1) - 1;

	ppdu_info->nss = u32_get_bits(nsts, VHT_SIG_SU_NSS_MASK);
	ppdu_info->bw = u32_get_bits(info0, HAL_RX_VHT_SIG_A_INFO_INFO0_BW);
	ppdu_info->beamformed = u32_get_bits(info1,
					     HAL_RX_VHT_SIG_A_INFO_INFO1_BEAMFORMED);
	ppdu_info->vht_flag_values5 = u32_get_bits(info0,
						   HAL_RX_VHT_SIG_A_INFO_INFO0_GROUP_ID);
	ppdu_info->vht_flag_values3[0] = (((ppdu_info->mcs) << 4) |
					    ppdu_info->nss);
	ppdu_info->vht_flag_values2 = ppdu_info->bw;
	ppdu_info->vht_flag_values4 =
		u32_get_bits(info1, HAL_RX_VHT_SIG_A_INFO_INFO1_SU_MU_CODING);
}

static void ath12k_dp_mon_parse_ht_sig(const struct hal_rx_ht_sig_info *ht_sig,
				       struct hal_rx_mon_ppdu_info *ppdu_info)
{
	u32 info0 = __le32_to_cpu(ht_sig->info0);
	u32 info1 = __le32_to_cpu(ht_sig->info1);

	ppdu_info->mcs = u32_get_bits(info0, HAL_RX_HT_SIG_INFO_INFO0_MCS);
	ppdu_info->bw = u32_get_bits(info0, HAL_RX_HT_SIG_INFO_INFO0_BW);
	ppdu_info->is_stbc = u32_get_bits(info1, HAL_RX_HT_SIG_INFO_INFO1_STBC);
	ppdu_info->ldpc = u32_get_bits(info1, HAL_RX_HT_SIG_INFO_INFO1_FEC_CODING);
	ppdu_info->gi = u32_get_bits(info1, HAL_RX_HT_SIG_INFO_INFO1_GI);
	ppdu_info->nss = (ppdu_info->mcs >> 3);
}

static void ath12k_dp_mon_parse_l_sig_b(const struct hal_rx_lsig_b_info *lsigb,
					struct hal_rx_mon_ppdu_info *ppdu_info)
{
	u32 info0 = __le32_to_cpu(lsigb->info0);
	u8 rate;

	rate = u32_get_bits(info0, HAL_RX_LSIG_B_INFO_INFO0_RATE);
	switch (rate) {
	case 1:
		rate = HAL_RX_LEGACY_RATE_1_MBPS;
		break;
	case 2:
	case 5:
		rate = HAL_RX_LEGACY_RATE_2_MBPS;
		break;
	case 3:
	case 6:
		rate = HAL_RX_LEGACY_RATE_5_5_MBPS;
		break;
	case 4:
	case 7:
		rate = HAL_RX_LEGACY_RATE_11_MBPS;
		break;
	default:
		rate = HAL_RX_LEGACY_RATE_INVALID;
	}

	ppdu_info->rate = rate;
	ppdu_info->cck_flag = 1;
}

static void ath12k_dp_mon_parse_l_sig_a(const struct hal_rx_lsig_a_info *lsiga,
					struct hal_rx_mon_ppdu_info *ppdu_info)
{
	u32 info0 = __le32_to_cpu(lsiga->info0);
	u8 rate;

	rate = u32_get_bits(info0, HAL_RX_LSIG_A_INFO_INFO0_RATE);
	switch (rate) {
	case 8:
		rate = HAL_RX_LEGACY_RATE_48_MBPS;
		break;
	case 9:
		rate = HAL_RX_LEGACY_RATE_24_MBPS;
		break;
	case 10:
		rate = HAL_RX_LEGACY_RATE_12_MBPS;
		break;
	case 11:
		rate = HAL_RX_LEGACY_RATE_6_MBPS;
		break;
	case 12:
		rate = HAL_RX_LEGACY_RATE_54_MBPS;
		break;
	case 13:
		rate = HAL_RX_LEGACY_RATE_36_MBPS;
		break;
	case 14:
		rate = HAL_RX_LEGACY_RATE_18_MBPS;
		break;
	case 15:
		rate = HAL_RX_LEGACY_RATE_9_MBPS;
		break;
	default:
		rate = HAL_RX_LEGACY_RATE_INVALID;
	}

	ppdu_info->rate = rate;
}

static void
ath12k_dp_mon_parse_he_sig_b2_ofdma(const struct hal_rx_he_sig_b2_ofdma_info *ofdma,
				    struct hal_rx_mon_ppdu_info *ppdu_info)
{
	u32 info0, value;

	info0 = __le32_to_cpu(ofdma->info0);

	ppdu_info->he_data1 |= HE_MCS_KNOWN | HE_DCM_KNOWN | HE_CODING_KNOWN;

	/* HE-data2 */
	ppdu_info->he_data2 |= HE_TXBF_KNOWN;

	ppdu_info->mcs = u32_get_bits(info0, HAL_RX_HE_SIG_B2_OFDMA_INFO_INFO0_STA_MCS);
	value = ppdu_info->mcs << HE_TRANSMIT_MCS_SHIFT;
	ppdu_info->he_data3 |= value;

	value = u32_get_bits(info0, HAL_RX_HE_SIG_B2_OFDMA_INFO_INFO0_STA_DCM);
	value = value << HE_DCM_SHIFT;
	ppdu_info->he_data3 |= value;

	value = u32_get_bits(info0, HAL_RX_HE_SIG_B2_OFDMA_INFO_INFO0_STA_CODING);
	ppdu_info->ldpc = value;
	value = value << HE_CODING_SHIFT;
	ppdu_info->he_data3 |= value;

	/* HE-data4 */
	value = u32_get_bits(info0, HAL_RX_HE_SIG_B2_OFDMA_INFO_INFO0_STA_ID);
	value = value << HE_STA_ID_SHIFT;
	ppdu_info->he_data4 |= value;

	ppdu_info->nss = u32_get_bits(info0, HAL_RX_HE_SIG_B2_OFDMA_INFO_INFO0_STA_NSTS);
	ppdu_info->beamformed = u32_get_bits(info0,
					     HAL_RX_HE_SIG_B2_OFDMA_INFO_INFO0_STA_TXBF);
}

static void
ath12k_dp_mon_parse_he_sig_b2_mu(const struct hal_rx_he_sig_b2_mu_info *he_sig_b2_mu,
				 struct hal_rx_mon_ppdu_info *ppdu_info)
{
	u32 info0, value;

	info0 = __le32_to_cpu(he_sig_b2_mu->info0);

	ppdu_info->he_data1 |= HE_MCS_KNOWN | HE_CODING_KNOWN;

	ppdu_info->mcs = u32_get_bits(info0, HAL_RX_HE_SIG_B2_MU_INFO_INFO0_STA_MCS);
	value = ppdu_info->mcs << HE_TRANSMIT_MCS_SHIFT;
	ppdu_info->he_data3 |= value;

	value = u32_get_bits(info0, HAL_RX_HE_SIG_B2_MU_INFO_INFO0_STA_CODING);
	ppdu_info->ldpc = value;
	value = value << HE_CODING_SHIFT;
	ppdu_info->he_data3 |= value;

	value = u32_get_bits(info0, HAL_RX_HE_SIG_B2_MU_INFO_INFO0_STA_ID);
	value = value << HE_STA_ID_SHIFT;
	ppdu_info->he_data4 |= value;

	ppdu_info->nss = u32_get_bits(info0, HAL_RX_HE_SIG_B2_MU_INFO_INFO0_STA_NSTS);
}

static void
ath12k_dp_mon_parse_he_sig_b1_mu(const struct hal_rx_he_sig_b1_mu_info *he_sig_b1_mu,
				 struct hal_rx_mon_ppdu_info *ppdu_info)
{
	u32 info0 = __le32_to_cpu(he_sig_b1_mu->info0);
	u16 ru_tones;

	ru_tones = u32_get_bits(info0,
				HAL_RX_HE_SIG_B1_MU_INFO_INFO0_RU_ALLOCATION);
	ppdu_info->ru_alloc = ath12k_he_ru_tones_to_nl80211_he_ru_alloc(ru_tones);
	ppdu_info->he_RU[0] = ru_tones;
}

static void
ath12k_dp_mon_parse_he_sig_mu(const struct hal_rx_he_sig_a_mu_dl_info *he_sig_a_mu_dl,
			      struct hal_rx_mon_ppdu_info *ppdu_info)
{
	u32 info0, info1, value;
	u16 he_gi = 0, he_ltf = 0;

	info0 = __le32_to_cpu(he_sig_a_mu_dl->info0);
	info1 = __le32_to_cpu(he_sig_a_mu_dl->info1);

	ppdu_info->he_mu_flags = 1;

	ppdu_info->he_data1 = HE_MU_FORMAT_TYPE;
	ppdu_info->he_data1 |=
			HE_BSS_COLOR_KNOWN |
			HE_DL_UL_KNOWN |
			HE_LDPC_EXTRA_SYMBOL_KNOWN |
			HE_STBC_KNOWN |
			HE_DATA_BW_RU_KNOWN |
			HE_DOPPLER_KNOWN;

	ppdu_info->he_data2 =
			HE_GI_KNOWN |
			HE_LTF_SYMBOLS_KNOWN |
			HE_PRE_FEC_PADDING_KNOWN |
			HE_PE_DISAMBIGUITY_KNOWN |
			HE_TXOP_KNOWN |
			HE_MIDABLE_PERIODICITY_KNOWN;

	/* data3 */
	ppdu_info->he_data3 = u32_get_bits(info0, HAL_RX_HE_SIG_A_MU_DL_INFO0_BSS_COLOR);
	value = u32_get_bits(info0, HAL_RX_HE_SIG_A_MU_DL_INFO0_UL_FLAG);
	value = value << HE_DL_UL_SHIFT;
	ppdu_info->he_data3 |= value;

	value = u32_get_bits(info1, HAL_RX_HE_SIG_A_MU_DL_INFO1_LDPC_EXTRA);
	value = value << HE_LDPC_EXTRA_SYMBOL_SHIFT;
	ppdu_info->he_data3 |= value;

	value = u32_get_bits(info1, HAL_RX_HE_SIG_A_MU_DL_INFO1_STBC);
	value = value << HE_STBC_SHIFT;
	ppdu_info->he_data3 |= value;

	/* data4 */
	ppdu_info->he_data4 = u32_get_bits(info0,
					   HAL_RX_HE_SIG_A_MU_DL_INFO0_SPATIAL_REUSE);
	ppdu_info->he_data4 = value;

	/* data5 */
	value = u32_get_bits(info0, HAL_RX_HE_SIG_A_MU_DL_INFO0_TRANSMIT_BW);
	ppdu_info->he_data5 = value;
	ppdu_info->bw = value;

	value = u32_get_bits(info0, HAL_RX_HE_SIG_A_MU_DL_INFO0_CP_LTF_SIZE);
	switch (value) {
	case 0:
		he_gi = HE_GI_0_8;
		he_ltf = HE_LTF_4_X;
		break;
	case 1:
		he_gi = HE_GI_0_8;
		he_ltf = HE_LTF_2_X;
		break;
	case 2:
		he_gi = HE_GI_1_6;
		he_ltf = HE_LTF_2_X;
		break;
	case 3:
		he_gi = HE_GI_3_2;
		he_ltf = HE_LTF_4_X;
		break;
	}

	ppdu_info->gi = he_gi;
	value = he_gi << HE_GI_SHIFT;
	ppdu_info->he_data5 |= value;

	value = he_ltf << HE_LTF_SIZE_SHIFT;
	ppdu_info->he_data5 |= value;

	value = u32_get_bits(info1, HAL_RX_HE_SIG_A_MU_DL_INFO1_NUM_LTF_SYMB);
	value = (value << HE_LTF_SYM_SHIFT);
	ppdu_info->he_data5 |= value;

	value = u32_get_bits(info1, HAL_RX_HE_SIG_A_MU_DL_INFO1_PKT_EXT_FACTOR);
	value = value << HE_PRE_FEC_PAD_SHIFT;
	ppdu_info->he_data5 |= value;

	value = u32_get_bits(info1, HAL_RX_HE_SIG_A_MU_DL_INFO1_PKT_EXT_PE_DISAM);
	value = value << HE_PE_DISAMBIGUITY_SHIFT;
	ppdu_info->he_data5 |= value;

	/*data6*/
	value = u32_get_bits(info0, HAL_RX_HE_SIG_A_MU_DL_INFO0_DOPPLER_INDICATION);
	value = value << HE_DOPPLER_SHIFT;
	ppdu_info->he_data6 |= value;

	value = u32_get_bits(info1, HAL_RX_HE_SIG_A_MU_DL_INFO1_TXOP_DURATION);
	value = value << HE_TXOP_SHIFT;
	ppdu_info->he_data6 |= value;

	/* HE-MU Flags */
	/* HE-MU-flags1 */
	ppdu_info->he_flags1 =
		HE_SIG_B_MCS_KNOWN |
		HE_SIG_B_DCM_KNOWN |
		HE_SIG_B_COMPRESSION_FLAG_1_KNOWN |
		HE_SIG_B_SYM_NUM_KNOWN |
		HE_RU_0_KNOWN;

	value = u32_get_bits(info0, HAL_RX_HE_SIG_A_MU_DL_INFO0_MCS_OF_SIGB);
	ppdu_info->he_flags1 |= value;
	value = u32_get_bits(info0, HAL_RX_HE_SIG_A_MU_DL_INFO0_DCM_OF_SIGB);
	value = value << HE_DCM_FLAG_1_SHIFT;
	ppdu_info->he_flags1 |= value;

	/* HE-MU-flags2 */
	ppdu_info->he_flags2 = HE_BW_KNOWN;

	value = u32_get_bits(info0, HAL_RX_HE_SIG_A_MU_DL_INFO0_TRANSMIT_BW);
	ppdu_info->he_flags2 |= value;
	value = u32_get_bits(info0, HAL_RX_HE_SIG_A_MU_DL_INFO0_COMP_MODE_SIGB);
	value = value << HE_SIG_B_COMPRESSION_FLAG_2_SHIFT;
	ppdu_info->he_flags2 |= value;
	value = u32_get_bits(info0, HAL_RX_HE_SIG_A_MU_DL_INFO0_NUM_SIGB_SYMB);
	value = value - 1;
	value = value << HE_NUM_SIG_B_SYMBOLS_SHIFT;
	ppdu_info->he_flags2 |= value;

	ppdu_info->is_stbc = info1 &
			     HAL_RX_HE_SIG_A_MU_DL_INFO1_STBC;
}

static void ath12k_dp_mon_parse_he_sig_su(const struct hal_rx_he_sig_a_su_info *he_sig_a,
					  struct hal_rx_mon_ppdu_info *ppdu_info)
{
	u32 info0, info1, value;
	u32 dcm;
	u8 he_dcm = 0, he_stbc = 0;
	u16 he_gi = 0, he_ltf = 0;

	ppdu_info->he_flags = 1;

	info0 = __le32_to_cpu(he_sig_a->info0);
	info1 = __le32_to_cpu(he_sig_a->info1);

	value = u32_get_bits(info0, HAL_RX_HE_SIG_A_SU_INFO_INFO0_FORMAT_IND);
	if (value == 0)
		ppdu_info->he_data1 = HE_TRIG_FORMAT_TYPE;
	else
		ppdu_info->he_data1 = HE_SU_FORMAT_TYPE;

	ppdu_info->he_data1 |=
			HE_BSS_COLOR_KNOWN |
			HE_BEAM_CHANGE_KNOWN |
			HE_DL_UL_KNOWN |
			HE_MCS_KNOWN |
			HE_DCM_KNOWN |
			HE_CODING_KNOWN |
			HE_LDPC_EXTRA_SYMBOL_KNOWN |
			HE_STBC_KNOWN |
			HE_DATA_BW_RU_KNOWN |
			HE_DOPPLER_KNOWN;

	ppdu_info->he_data2 |=
			HE_GI_KNOWN |
			HE_TXBF_KNOWN |
			HE_PE_DISAMBIGUITY_KNOWN |
			HE_TXOP_KNOWN |
			HE_LTF_SYMBOLS_KNOWN |
			HE_PRE_FEC_PADDING_KNOWN |
			HE_MIDABLE_PERIODICITY_KNOWN;

	ppdu_info->he_data3 = u32_get_bits(info0,
					   HAL_RX_HE_SIG_A_SU_INFO_INFO0_BSS_COLOR);
	value = u32_get_bits(info0, HAL_RX_HE_SIG_A_SU_INFO_INFO0_BEAM_CHANGE);
	value = value << HE_BEAM_CHANGE_SHIFT;
	ppdu_info->he_data3 |= value;
	value = u32_get_bits(info0, HAL_RX_HE_SIG_A_SU_INFO_INFO0_DL_UL_FLAG);
	value = value << HE_DL_UL_SHIFT;
	ppdu_info->he_data3 |= value;

	value = u32_get_bits(info0, HAL_RX_HE_SIG_A_SU_INFO_INFO0_TRANSMIT_MCS);
	ppdu_info->mcs = value;
	value = value << HE_TRANSMIT_MCS_SHIFT;
	ppdu_info->he_data3 |= value;

	value = u32_get_bits(info0, HAL_RX_HE_SIG_A_SU_INFO_INFO0_DCM);
	he_dcm = value;
	value = value << HE_DCM_SHIFT;
	ppdu_info->he_data3 |= value;
	value = u32_get_bits(info1, HAL_RX_HE_SIG_A_SU_INFO_INFO1_CODING);
	value = value << HE_CODING_SHIFT;
	ppdu_info->he_data3 |= value;
	value = u32_get_bits(info1, HAL_RX_HE_SIG_A_SU_INFO_INFO1_LDPC_EXTRA);
	value = value << HE_LDPC_EXTRA_SYMBOL_SHIFT;
	ppdu_info->he_data3 |= value;
	value = u32_get_bits(info1, HAL_RX_HE_SIG_A_SU_INFO_INFO1_STBC);
	he_stbc = value;
	value = value << HE_STBC_SHIFT;
	ppdu_info->he_data3 |= value;

	/* data4 */
	ppdu_info->he_data4 = u32_get_bits(info0,
					   HAL_RX_HE_SIG_A_SU_INFO_INFO0_SPATIAL_REUSE);

	/* data5 */
	value = u32_get_bits(info0,
			     HAL_RX_HE_SIG_A_SU_INFO_INFO0_TRANSMIT_BW);
	ppdu_info->he_data5 = value;
	ppdu_info->bw = value;
	value = u32_get_bits(info0, HAL_RX_HE_SIG_A_SU_INFO_INFO0_CP_LTF_SIZE);
	switch (value) {
	case 0:
		he_gi = HE_GI_0_8;
		he_ltf = HE_LTF_1_X;
		break;
	case 1:
		he_gi = HE_GI_0_8;
		he_ltf = HE_LTF_2_X;
		break;
	case 2:
		he_gi = HE_GI_1_6;
		he_ltf = HE_LTF_2_X;
		break;
	case 3:
		if (he_dcm && he_stbc) {
			he_gi = HE_GI_0_8;
			he_ltf = HE_LTF_4_X;
		} else {
			he_gi = HE_GI_3_2;
			he_ltf = HE_LTF_4_X;
		}
		break;
	}
	ppdu_info->gi = he_gi;
	value = he_gi << HE_GI_SHIFT;
	ppdu_info->he_data5 |= value;
	value = he_ltf << HE_LTF_SIZE_SHIFT;
	ppdu_info->ltf_size = he_ltf;
	ppdu_info->he_data5 |= value;

	value = u32_get_bits(info0, HAL_RX_HE_SIG_A_SU_INFO_INFO0_NSTS);
	value = (value << HE_LTF_SYM_SHIFT);
	ppdu_info->he_data5 |= value;

	value = u32_get_bits(info1, HAL_RX_HE_SIG_A_SU_INFO_INFO1_PKT_EXT_FACTOR);
	value = value << HE_PRE_FEC_PAD_SHIFT;
	ppdu_info->he_data5 |= value;

	value = u32_get_bits(info1, HAL_RX_HE_SIG_A_SU_INFO_INFO1_TXBF);
	value = value << HE_TXBF_SHIFT;
	ppdu_info->he_data5 |= value;
	value = u32_get_bits(info1, HAL_RX_HE_SIG_A_SU_INFO_INFO1_PKT_EXT_PE_DISAM);
	value = value << HE_PE_DISAMBIGUITY_SHIFT;
	ppdu_info->he_data5 |= value;

	/* data6 */
	value = u32_get_bits(info0, HAL_RX_HE_SIG_A_SU_INFO_INFO0_NSTS);
	value++;
	ppdu_info->he_data6 = value;
	value = u32_get_bits(info1, HAL_RX_HE_SIG_A_SU_INFO_INFO1_DOPPLER_IND);
	value = value << HE_DOPPLER_SHIFT;
	ppdu_info->he_data6 |= value;
	value = u32_get_bits(info1, HAL_RX_HE_SIG_A_SU_INFO_INFO1_TXOP_DURATION);
	value = value << HE_TXOP_SHIFT;
	ppdu_info->he_data6 |= value;

	ppdu_info->mcs =
		u32_get_bits(info0, HAL_RX_HE_SIG_A_SU_INFO_INFO0_TRANSMIT_MCS);
	ppdu_info->bw =
		u32_get_bits(info0, HAL_RX_HE_SIG_A_SU_INFO_INFO0_TRANSMIT_BW);
	ppdu_info->ldpc = u32_get_bits(info1, HAL_RX_HE_SIG_A_SU_INFO_INFO1_CODING);
	ppdu_info->is_stbc = u32_get_bits(info1, HAL_RX_HE_SIG_A_SU_INFO_INFO1_STBC);
	ppdu_info->beamformed = u32_get_bits(info1, HAL_RX_HE_SIG_A_SU_INFO_INFO1_TXBF);
	dcm = u32_get_bits(info0, HAL_RX_HE_SIG_A_SU_INFO_INFO0_DCM);
	ppdu_info->nss = u32_get_bits(info0, HAL_RX_HE_SIG_A_SU_INFO_INFO0_NSTS);
	ppdu_info->dcm = dcm;
}

static void
ath12k_dp_mon_hal_rx_parse_u_sig_cmn(const struct hal_mon_usig_cmn *cmn,
				     struct hal_rx_mon_ppdu_info *ppdu_info)
{
	u32 common;

	ppdu_info->u_sig_info.bw = le32_get_bits(cmn->info0,
						 HAL_RX_USIG_CMN_INFO0_BW);
	ppdu_info->u_sig_info.ul_dl = le32_get_bits(cmn->info0,
						    HAL_RX_USIG_CMN_INFO0_UL_DL);

	common = __le32_to_cpu(ppdu_info->u_sig_info.usig.common);
	common |= IEEE80211_RADIOTAP_EHT_USIG_COMMON_PHY_VER_KNOWN |
		  IEEE80211_RADIOTAP_EHT_USIG_COMMON_BW_KNOWN |
		  IEEE80211_RADIOTAP_EHT_USIG_COMMON_UL_DL_KNOWN |
		  IEEE80211_RADIOTAP_EHT_USIG_COMMON_BSS_COLOR_KNOWN |
		  IEEE80211_RADIOTAP_EHT_USIG_COMMON_TXOP_KNOWN |
		  ATH12K_LE32_DEC_ENC(cmn->info0,
				      HAL_RX_USIG_CMN_INFO0_PHY_VERSION,
				      IEEE80211_RADIOTAP_EHT_USIG_COMMON_PHY_VER) |
		  u32_encode_bits(ppdu_info->u_sig_info.bw,
				  IEEE80211_RADIOTAP_EHT_USIG_COMMON_BW) |
		  u32_encode_bits(ppdu_info->u_sig_info.ul_dl,
				  IEEE80211_RADIOTAP_EHT_USIG_COMMON_UL_DL) |
		  ATH12K_LE32_DEC_ENC(cmn->info0,
				      HAL_RX_USIG_CMN_INFO0_BSS_COLOR,
				      IEEE80211_RADIOTAP_EHT_USIG_COMMON_BSS_COLOR) |
		  ATH12K_LE32_DEC_ENC(cmn->info0,
				      HAL_RX_USIG_CMN_INFO0_TXOP,
				      IEEE80211_RADIOTAP_EHT_USIG_COMMON_TXOP);
	ppdu_info->u_sig_info.usig.common = cpu_to_le32(common);

	switch (ppdu_info->u_sig_info.bw) {
	default:
		fallthrough;
	case HAL_EHT_BW_20:
		ppdu_info->bw = HAL_RX_BW_20MHZ;
		break;
	case HAL_EHT_BW_40:
		ppdu_info->bw = HAL_RX_BW_40MHZ;
		break;
	case HAL_EHT_BW_80:
		ppdu_info->bw = HAL_RX_BW_80MHZ;
		break;
	case HAL_EHT_BW_160:
		ppdu_info->bw = HAL_RX_BW_160MHZ;
		break;
	case HAL_EHT_BW_320_1:
	case HAL_EHT_BW_320_2:
		ppdu_info->bw = HAL_RX_BW_320MHZ;
		break;
	}
}

static void
ath12k_dp_mon_hal_rx_parse_u_sig_tb(const struct hal_mon_usig_tb *usig_tb,
				    struct hal_rx_mon_ppdu_info *ppdu_info)
{
	struct ieee80211_radiotap_eht_usig *usig = &ppdu_info->u_sig_info.usig;
	enum ieee80211_radiotap_eht_usig_tb spatial_reuse1, spatial_reuse2;
	u32 common, value, mask;

	spatial_reuse1 = IEEE80211_RADIOTAP_EHT_USIG2_TB_B3_B6_SPATIAL_REUSE_1;
	spatial_reuse2 = IEEE80211_RADIOTAP_EHT_USIG2_TB_B7_B10_SPATIAL_REUSE_2;

	common = __le32_to_cpu(usig->common);
	value = __le32_to_cpu(usig->value);
	mask = __le32_to_cpu(usig->mask);

	ppdu_info->u_sig_info.ppdu_type_comp_mode =
				le32_get_bits(usig_tb->info0,
					      HAL_RX_USIG_TB_INFO0_PPDU_TYPE_COMP_MODE);

	common |= ATH12K_LE32_DEC_ENC(usig_tb->info0,
				      HAL_RX_USIG_TB_INFO0_RX_INTEG_CHECK_PASS,
				      IEEE80211_RADIOTAP_EHT_USIG_COMMON_BAD_USIG_CRC);

	value |= IEEE80211_RADIOTAP_EHT_USIG1_TB_B20_B25_DISREGARD |
		 u32_encode_bits(ppdu_info->u_sig_info.ppdu_type_comp_mode,
				 IEEE80211_RADIOTAP_EHT_USIG2_TB_B0_B1_PPDU_TYPE) |
		 IEEE80211_RADIOTAP_EHT_USIG2_TB_B2_VALIDATE |
		 ATH12K_LE32_DEC_ENC(usig_tb->info0,
				     HAL_RX_USIG_TB_INFO0_SPATIAL_REUSE_1,
				     spatial_reuse1) |
		 ATH12K_LE32_DEC_ENC(usig_tb->info0,
				     HAL_RX_USIG_TB_INFO0_SPATIAL_REUSE_2,
				     spatial_reuse2) |
		 IEEE80211_RADIOTAP_EHT_USIG2_TB_B11_B15_DISREGARD |
		 ATH12K_LE32_DEC_ENC(usig_tb->info0,
				     HAL_RX_USIG_TB_INFO0_CRC,
				     IEEE80211_RADIOTAP_EHT_USIG2_TB_B16_B19_CRC) |
		 ATH12K_LE32_DEC_ENC(usig_tb->info0,
				     HAL_RX_USIG_TB_INFO0_TAIL,
				     IEEE80211_RADIOTAP_EHT_USIG2_TB_B20_B25_TAIL);

	mask |= IEEE80211_RADIOTAP_EHT_USIG1_TB_B20_B25_DISREGARD |
		IEEE80211_RADIOTAP_EHT_USIG2_TB_B0_B1_PPDU_TYPE |
		IEEE80211_RADIOTAP_EHT_USIG2_TB_B2_VALIDATE |
		spatial_reuse1 | spatial_reuse2 |
		IEEE80211_RADIOTAP_EHT_USIG2_TB_B11_B15_DISREGARD |
		IEEE80211_RADIOTAP_EHT_USIG2_TB_B16_B19_CRC |
		IEEE80211_RADIOTAP_EHT_USIG2_TB_B20_B25_TAIL;

	usig->common = cpu_to_le32(common);
	usig->value = cpu_to_le32(value);
	usig->mask = cpu_to_le32(mask);
}

static void
ath12k_dp_mon_hal_rx_parse_u_sig_mu(const struct hal_mon_usig_mu *usig_mu,
				    struct hal_rx_mon_ppdu_info *ppdu_info)
{
	struct ieee80211_radiotap_eht_usig *usig = &ppdu_info->u_sig_info.usig;
	enum ieee80211_radiotap_eht_usig_mu sig_symb, punc;
	u32 common, value, mask;

	sig_symb = IEEE80211_RADIOTAP_EHT_USIG2_MU_B11_B15_EHT_SIG_SYMBOLS;
	punc = IEEE80211_RADIOTAP_EHT_USIG2_MU_B3_B7_PUNCTURED_INFO;

	common = __le32_to_cpu(usig->common);
	value = __le32_to_cpu(usig->value);
	mask = __le32_to_cpu(usig->mask);

	ppdu_info->u_sig_info.ppdu_type_comp_mode =
				le32_get_bits(usig_mu->info0,
					      HAL_RX_USIG_MU_INFO0_PPDU_TYPE_COMP_MODE);
	ppdu_info->u_sig_info.eht_sig_mcs =
				le32_get_bits(usig_mu->info0,
					      HAL_RX_USIG_MU_INFO0_EHT_SIG_MCS);
	ppdu_info->u_sig_info.num_eht_sig_sym =
				le32_get_bits(usig_mu->info0,
					      HAL_RX_USIG_MU_INFO0_NUM_EHT_SIG_SYM);

	common |= ATH12K_LE32_DEC_ENC(usig_mu->info0,
				      HAL_RX_USIG_MU_INFO0_RX_INTEG_CHECK_PASS,
				      IEEE80211_RADIOTAP_EHT_USIG_COMMON_BAD_USIG_CRC);

	value |= IEEE80211_RADIOTAP_EHT_USIG1_MU_B20_B24_DISREGARD |
		 IEEE80211_RADIOTAP_EHT_USIG1_MU_B25_VALIDATE |
		 u32_encode_bits(ppdu_info->u_sig_info.ppdu_type_comp_mode,
				 IEEE80211_RADIOTAP_EHT_USIG2_MU_B0_B1_PPDU_TYPE) |
		 IEEE80211_RADIOTAP_EHT_USIG2_MU_B2_VALIDATE |
		 ATH12K_LE32_DEC_ENC(usig_mu->info0,
				     HAL_RX_USIG_MU_INFO0_PUNC_CH_INFO,
				     punc) |
		 IEEE80211_RADIOTAP_EHT_USIG2_MU_B8_VALIDATE |
		 u32_encode_bits(ppdu_info->u_sig_info.eht_sig_mcs,
				 IEEE80211_RADIOTAP_EHT_USIG2_MU_B9_B10_SIG_MCS) |
		 u32_encode_bits(ppdu_info->u_sig_info.num_eht_sig_sym,
				 sig_symb) |
		 ATH12K_LE32_DEC_ENC(usig_mu->info0,
				     HAL_RX_USIG_MU_INFO0_CRC,
				     IEEE80211_RADIOTAP_EHT_USIG2_MU_B16_B19_CRC) |
		 ATH12K_LE32_DEC_ENC(usig_mu->info0,
				     HAL_RX_USIG_MU_INFO0_TAIL,
				     IEEE80211_RADIOTAP_EHT_USIG2_MU_B20_B25_TAIL);

	mask |= IEEE80211_RADIOTAP_EHT_USIG1_MU_B20_B24_DISREGARD |
		IEEE80211_RADIOTAP_EHT_USIG1_MU_B25_VALIDATE |
		IEEE80211_RADIOTAP_EHT_USIG2_MU_B0_B1_PPDU_TYPE |
		IEEE80211_RADIOTAP_EHT_USIG2_MU_B2_VALIDATE |
		punc |
		IEEE80211_RADIOTAP_EHT_USIG2_MU_B8_VALIDATE |
		IEEE80211_RADIOTAP_EHT_USIG2_MU_B9_B10_SIG_MCS |
		sig_symb |
		IEEE80211_RADIOTAP_EHT_USIG2_MU_B16_B19_CRC |
		IEEE80211_RADIOTAP_EHT_USIG2_MU_B20_B25_TAIL;

	usig->common = cpu_to_le32(common);
	usig->value = cpu_to_le32(value);
	usig->mask = cpu_to_le32(mask);
}

static void
ath12k_dp_mon_hal_rx_parse_u_sig_hdr(const struct hal_mon_usig_hdr *usig,
				     struct hal_rx_mon_ppdu_info *ppdu_info)
{
	u8 comp_mode;

	ppdu_info->eht_usig = true;

	ath12k_dp_mon_hal_rx_parse_u_sig_cmn(&usig->cmn, ppdu_info);

	comp_mode = le32_get_bits(usig->non_cmn.mu.info0,
				  HAL_RX_USIG_MU_INFO0_PPDU_TYPE_COMP_MODE);

	if (comp_mode == 0 && ppdu_info->u_sig_info.ul_dl)
		ath12k_dp_mon_hal_rx_parse_u_sig_tb(&usig->non_cmn.tb, ppdu_info);
	else
		ath12k_dp_mon_hal_rx_parse_u_sig_mu(&usig->non_cmn.mu, ppdu_info);
}

static void
ath12k_dp_mon_hal_aggr_tlv(struct hal_rx_mon_ppdu_info *ppdu_info,
			   u16 tlv_len, const void *tlv_data)
{
	if (tlv_len <= HAL_RX_MON_MAX_AGGR_SIZE - ppdu_info->tlv_aggr.cur_len) {
		memcpy(ppdu_info->tlv_aggr.buf + ppdu_info->tlv_aggr.cur_len,
		       tlv_data, tlv_len);
		ppdu_info->tlv_aggr.cur_len += tlv_len;
	}
}

static inline bool
ath12k_dp_mon_hal_rx_is_frame_type_ndp(const struct hal_rx_u_sig_info *usig_info)
{
	if (usig_info->ppdu_type_comp_mode == 1 &&
	    usig_info->eht_sig_mcs == 0 &&
	    usig_info->num_eht_sig_sym == 0)
		return true;

	return false;
}

static inline bool
ath12k_dp_mon_hal_rx_is_non_ofdma(const struct hal_rx_u_sig_info *usig_info)
{
	u32 ppdu_type_comp_mode = usig_info->ppdu_type_comp_mode;
	u32 ul_dl = usig_info->ul_dl;

	if ((ppdu_type_comp_mode == HAL_RX_RECEPTION_TYPE_MU_MIMO && ul_dl == 0) ||
	    (ppdu_type_comp_mode == HAL_RX_RECEPTION_TYPE_MU_OFDMA && ul_dl == 0) ||
	    (ppdu_type_comp_mode == HAL_RX_RECEPTION_TYPE_MU_MIMO  && ul_dl == 1))
		return true;

	return false;
}

static inline bool
ath12k_dp_mon_hal_rx_is_ofdma(const struct hal_rx_u_sig_info *usig_info)
{
	if (usig_info->ppdu_type_comp_mode == 0 && usig_info->ul_dl == 0)
		return true;

	return false;
}

static void
ath12k_dp_mon_hal_rx_parse_eht_sig_ndp(const struct hal_eht_sig_ndp_cmn_eb *eht_sig_ndp,
				       struct hal_rx_mon_ppdu_info *ppdu_info)
{
	struct hal_rx_radiotap_eht *eht = &ppdu_info->eht_info.eht;
	u32 known, data;

	known = __le32_to_cpu(eht->known);
	known |= IEEE80211_RADIOTAP_EHT_KNOWN_SPATIAL_REUSE |
		 IEEE80211_RADIOTAP_EHT_KNOWN_EHT_LTF |
		 IEEE80211_RADIOTAP_EHT_KNOWN_NSS_S |
		 IEEE80211_RADIOTAP_EHT_KNOWN_BEAMFORMED_S |
		 IEEE80211_RADIOTAP_EHT_KNOWN_DISREGARD_S |
		 IEEE80211_RADIOTAP_EHT_KNOWN_CRC1 |
		 IEEE80211_RADIOTAP_EHT_KNOWN_TAIL1;
	eht->known = cpu_to_le32(known);

	data = __le32_to_cpu(eht->data[0]);
	data |= ATH12K_LE32_DEC_ENC(eht_sig_ndp->info0,
				    HAL_RX_EHT_SIG_NDP_CMN_INFO0_SPATIAL_REUSE,
				    IEEE80211_RADIOTAP_EHT_DATA0_SPATIAL_REUSE);
	/* GI and LTF size are separately indicated in radiotap header
	 * and hence will be parsed from other TLV
	 */
	data |=	ATH12K_LE32_DEC_ENC(eht_sig_ndp->info0,
				    HAL_RX_EHT_SIG_NDP_CMN_INFO0_NUM_LTF_SYM,
				    IEEE80211_RADIOTAP_EHT_DATA0_EHT_LTF);

	data |=	ATH12K_LE32_DEC_ENC(eht_sig_ndp->info0,
				    HAL_RX_EHT_SIG_NDP_CMN_INFO0_CRC,
				    IEEE80211_RADIOTAP_EHT_DATA0_CRC1_O);

	data |= ATH12K_LE32_DEC_ENC(eht_sig_ndp->info0,
				    HAL_RX_EHT_SIG_NDP_CMN_INFO0_DISREGARD,
				    IEEE80211_RADIOTAP_EHT_DATA0_DISREGARD_S);
	eht->data[0] = cpu_to_le32(data);

	data = __le32_to_cpu(eht->data[7]);
	data |=	ATH12K_LE32_DEC_ENC(eht_sig_ndp->info0,
				    HAL_RX_EHT_SIG_NDP_CMN_INFO0_NSS,
				    IEEE80211_RADIOTAP_EHT_DATA7_NSS_S);

	data |=	ATH12K_LE32_DEC_ENC(eht_sig_ndp->info0,
				    HAL_RX_EHT_SIG_NDP_CMN_INFO0_BEAMFORMED,
				    IEEE80211_RADIOTAP_EHT_DATA7_BEAMFORMED_S);
	eht->data[7] = cpu_to_le32(data);
}

static void
ath12k_dp_mon_hal_rx_parse_usig_overflow(const struct hal_eht_sig_usig_overflow *ovflow,
					 struct hal_rx_mon_ppdu_info *ppdu_info)
{
	struct hal_rx_radiotap_eht *eht = &ppdu_info->eht_info.eht;
	u32 known, data;

	known = __le32_to_cpu(eht->known);
	known |= IEEE80211_RADIOTAP_EHT_KNOWN_SPATIAL_REUSE |
		 IEEE80211_RADIOTAP_EHT_KNOWN_EHT_LTF |
		 IEEE80211_RADIOTAP_EHT_KNOWN_LDPC_EXTRA_SYM_OM |
		 IEEE80211_RADIOTAP_EHT_KNOWN_PRE_PADD_FACOR_OM |
		 IEEE80211_RADIOTAP_EHT_KNOWN_PE_DISAMBIGUITY_OM |
		 IEEE80211_RADIOTAP_EHT_KNOWN_DISREGARD_O;
	eht->known = cpu_to_le32(known);

	data = __le32_to_cpu(eht->data[0]);
	data |=	ATH12K_LE32_DEC_ENC(ovflow->info0,
				    HAL_RX_EHT_SIG_OVERFLOW_INFO0_SPATIAL_REUSE,
				    IEEE80211_RADIOTAP_EHT_DATA0_SPATIAL_REUSE);

	/* GI and LTF size are separately indicated in radiotap header
	 * and hence will be parsed from other TLV
	 */
	data |=	ATH12K_LE32_DEC_ENC(ovflow->info0,
				    HAL_RX_EHT_SIG_OVERFLOW_INFO0_NUM_LTF_SYM,
				    IEEE80211_RADIOTAP_EHT_DATA0_EHT_LTF);

	data |=	ATH12K_LE32_DEC_ENC(ovflow->info0,
				    HAL_RX_EHT_SIG_OVERFLOW_INFO0_LDPC_EXTA_SYM,
				    IEEE80211_RADIOTAP_EHT_DATA0_LDPC_EXTRA_SYM_OM);

	data |=	ATH12K_LE32_DEC_ENC(ovflow->info0,
				    HAL_RX_EHT_SIG_OVERFLOW_INFO0_PRE_FEC_PAD_FACTOR,
				    IEEE80211_RADIOTAP_EHT_DATA0_PRE_PADD_FACOR_OM);

	data |=	ATH12K_LE32_DEC_ENC(ovflow->info0,
				    HAL_RX_EHT_SIG_OVERFLOW_INFO0_DISAMBIGUITY,
				    IEEE80211_RADIOTAP_EHT_DATA0_PE_DISAMBIGUITY_OM);

	data |=	ATH12K_LE32_DEC_ENC(ovflow->info0,
				    HAL_RX_EHT_SIG_OVERFLOW_INFO0_DISREGARD,
				    IEEE80211_RADIOTAP_EHT_DATA0_DISREGARD_O);
	eht->data[0] = cpu_to_le32(data);
}

static void
ath12k_dp_mon_hal_rx_parse_non_ofdma_users(const struct hal_eht_sig_non_ofdma_cmn_eb *eb,
					   struct hal_rx_mon_ppdu_info *ppdu_info)
{
	struct hal_rx_radiotap_eht *eht = &ppdu_info->eht_info.eht;
	u32 known, data;

	known = __le32_to_cpu(eht->known);
	known |= IEEE80211_RADIOTAP_EHT_KNOWN_NR_NON_OFDMA_USERS_M;
	eht->known = cpu_to_le32(known);

	data = __le32_to_cpu(eht->data[7]);
	data |=	ATH12K_LE32_DEC_ENC(eb->info0,
				    HAL_RX_EHT_SIG_NON_OFDMA_INFO0_NUM_USERS,
				    IEEE80211_RADIOTAP_EHT_DATA7_NUM_OF_NON_OFDMA_USERS);
	eht->data[7] = cpu_to_le32(data);
}

static void
ath12k_dp_mon_hal_rx_parse_eht_mumimo_user(const struct hal_eht_sig_mu_mimo *user,
					   struct hal_rx_mon_ppdu_info *ppdu_info)
{
	struct hal_rx_eht_info *eht_info = &ppdu_info->eht_info;
	u32 user_idx;

	if (eht_info->num_user_info >= ARRAY_SIZE(eht_info->user_info))
		return;

	user_idx = eht_info->num_user_info++;

	eht_info->user_info[user_idx] |=
		IEEE80211_RADIOTAP_EHT_USER_INFO_STA_ID_KNOWN |
		IEEE80211_RADIOTAP_EHT_USER_INFO_MCS_KNOWN |
		IEEE80211_RADIOTAP_EHT_USER_INFO_CODING_KNOWN |
		IEEE80211_RADIOTAP_EHT_USER_INFO_SPATIAL_CONFIG_KNOWN_M |
		ATH12K_LE32_DEC_ENC(user->info0,
				    HAL_RX_EHT_SIG_MUMIMO_USER_INFO0_STA_ID,
				    IEEE80211_RADIOTAP_EHT_USER_INFO_STA_ID) |
		ATH12K_LE32_DEC_ENC(user->info0,
				    HAL_RX_EHT_SIG_MUMIMO_USER_INFO0_CODING,
				    IEEE80211_RADIOTAP_EHT_USER_INFO_CODING) |
		ATH12K_LE32_DEC_ENC(user->info0,
				    HAL_RX_EHT_SIG_MUMIMO_USER_INFO0_MCS,
				    IEEE80211_RADIOTAP_EHT_USER_INFO_MCS) |
		ATH12K_LE32_DEC_ENC(user->info0,
				    HAL_RX_EHT_SIG_MUMIMO_USER_INFO0_SPATIAL_CODING,
				    IEEE80211_RADIOTAP_EHT_USER_INFO_SPATIAL_CONFIG_M);

	ppdu_info->mcs = le32_get_bits(user->info0,
				       HAL_RX_EHT_SIG_MUMIMO_USER_INFO0_MCS);
}

static void
ath12k_dp_mon_hal_rx_parse_eht_non_mumimo_user(const struct hal_eht_sig_non_mu_mimo *user,
					       struct hal_rx_mon_ppdu_info *ppdu_info)
{
	struct hal_rx_eht_info *eht_info = &ppdu_info->eht_info;
	u32 user_idx;

	if (eht_info->num_user_info >= ARRAY_SIZE(eht_info->user_info))
		return;

	user_idx = eht_info->num_user_info++;

	eht_info->user_info[user_idx] |=
		IEEE80211_RADIOTAP_EHT_USER_INFO_STA_ID_KNOWN |
		IEEE80211_RADIOTAP_EHT_USER_INFO_MCS_KNOWN |
		IEEE80211_RADIOTAP_EHT_USER_INFO_CODING_KNOWN |
		IEEE80211_RADIOTAP_EHT_USER_INFO_NSS_KNOWN_O |
		IEEE80211_RADIOTAP_EHT_USER_INFO_BEAMFORMING_KNOWN_O |
		ATH12K_LE32_DEC_ENC(user->info0,
				    HAL_RX_EHT_SIG_NON_MUMIMO_USER_INFO0_STA_ID,
				    IEEE80211_RADIOTAP_EHT_USER_INFO_STA_ID) |
		ATH12K_LE32_DEC_ENC(user->info0,
				    HAL_RX_EHT_SIG_NON_MUMIMO_USER_INFO0_CODING,
				    IEEE80211_RADIOTAP_EHT_USER_INFO_CODING) |
		ATH12K_LE32_DEC_ENC(user->info0,
				    HAL_RX_EHT_SIG_NON_MUMIMO_USER_INFO0_MCS,
				    IEEE80211_RADIOTAP_EHT_USER_INFO_MCS) |
		ATH12K_LE32_DEC_ENC(user->info0,
				    HAL_RX_EHT_SIG_NON_MUMIMO_USER_INFO0_NSS,
				    IEEE80211_RADIOTAP_EHT_USER_INFO_NSS_O) |
		ATH12K_LE32_DEC_ENC(user->info0,
				    HAL_RX_EHT_SIG_NON_MUMIMO_USER_INFO0_BEAMFORMED,
				    IEEE80211_RADIOTAP_EHT_USER_INFO_BEAMFORMING_O);

	ppdu_info->mcs = le32_get_bits(user->info0,
				       HAL_RX_EHT_SIG_NON_MUMIMO_USER_INFO0_MCS);

	ppdu_info->nss = le32_get_bits(user->info0,
				       HAL_RX_EHT_SIG_NON_MUMIMO_USER_INFO0_NSS) + 1;
}

static inline bool
ath12k_dp_mon_hal_rx_is_mu_mimo_user(const struct hal_rx_u_sig_info *usig_info)
{
	if (usig_info->ppdu_type_comp_mode == HAL_RX_RECEPTION_TYPE_SU &&
	    usig_info->ul_dl == 1)
		return true;

	return false;
}

static void
ath12k_dp_mon_hal_rx_parse_eht_sig_non_ofdma(const void *tlv,
					     struct hal_rx_mon_ppdu_info *ppdu_info)
{
	const struct hal_eht_sig_non_ofdma_cmn_eb *eb = tlv;

	ath12k_dp_mon_hal_rx_parse_usig_overflow(tlv, ppdu_info);
	ath12k_dp_mon_hal_rx_parse_non_ofdma_users(eb, ppdu_info);

	if (ath12k_dp_mon_hal_rx_is_mu_mimo_user(&ppdu_info->u_sig_info))
		ath12k_dp_mon_hal_rx_parse_eht_mumimo_user(&eb->user_field.mu_mimo,
							   ppdu_info);
	else
		ath12k_dp_mon_hal_rx_parse_eht_non_mumimo_user(&eb->user_field.n_mu_mimo,
							       ppdu_info);
}

static void
ath12k_dp_mon_hal_rx_parse_ru_allocation(const struct hal_eht_sig_ofdma_cmn_eb *eb,
					 struct hal_rx_mon_ppdu_info *ppdu_info)
{
	const struct hal_eht_sig_ofdma_cmn_eb1 *ofdma_cmn_eb1 = &eb->eb1;
	const struct hal_eht_sig_ofdma_cmn_eb2 *ofdma_cmn_eb2 = &eb->eb2;
	struct hal_rx_radiotap_eht *eht = &ppdu_info->eht_info.eht;
	enum ieee80211_radiotap_eht_data ru_123, ru_124, ru_125, ru_126;
	enum ieee80211_radiotap_eht_data ru_121, ru_122, ru_112, ru_111;
	u32 data;

	ru_123 = IEEE80211_RADIOTAP_EHT_DATA4_RU_ALLOC_CC_1_2_3;
	ru_124 = IEEE80211_RADIOTAP_EHT_DATA5_RU_ALLOC_CC_1_2_4;
	ru_125 = IEEE80211_RADIOTAP_EHT_DATA5_RU_ALLOC_CC_1_2_5;
	ru_126 = IEEE80211_RADIOTAP_EHT_DATA6_RU_ALLOC_CC_1_2_6;
	ru_121 = IEEE80211_RADIOTAP_EHT_DATA3_RU_ALLOC_CC_1_2_1;
	ru_122 = IEEE80211_RADIOTAP_EHT_DATA3_RU_ALLOC_CC_1_2_2;
	ru_112 = IEEE80211_RADIOTAP_EHT_DATA2_RU_ALLOC_CC_1_1_2;
	ru_111 = IEEE80211_RADIOTAP_EHT_DATA1_RU_ALLOC_CC_1_1_1;

	switch (ppdu_info->u_sig_info.bw) {
	case HAL_EHT_BW_320_2:
	case HAL_EHT_BW_320_1:
		data = __le32_to_cpu(eht->data[4]);
		/* CC1 2::3 */
		data |=	IEEE80211_RADIOTAP_EHT_DATA4_RU_ALLOC_CC_1_2_3_KNOWN |
			ATH12K_LE64_DEC_ENC(ofdma_cmn_eb2->info0,
					    HAL_RX_EHT_SIG_OFDMA_EB2_RU_ALLOC_2_3,
					    ru_123);
		eht->data[4] = cpu_to_le32(data);

		data = __le32_to_cpu(eht->data[5]);
		/* CC1 2::4 */
		data |=	IEEE80211_RADIOTAP_EHT_DATA5_RU_ALLOC_CC_1_2_4_KNOWN |
			ATH12K_LE64_DEC_ENC(ofdma_cmn_eb2->info0,
					    HAL_RX_EHT_SIG_OFDMA_EB2_RU_ALLOC_2_4,
					    ru_124);

		/* CC1 2::5 */
		data |=	IEEE80211_RADIOTAP_EHT_DATA5_RU_ALLOC_CC_1_2_5_KNOWN |
			ATH12K_LE64_DEC_ENC(ofdma_cmn_eb2->info0,
					    HAL_RX_EHT_SIG_OFDMA_EB2_RU_ALLOC_2_5,
					    ru_125);
		eht->data[5] = cpu_to_le32(data);

		data = __le32_to_cpu(eht->data[6]);
		/* CC1 2::6 */
		data |=	IEEE80211_RADIOTAP_EHT_DATA6_RU_ALLOC_CC_1_2_6_KNOWN |
			ATH12K_LE64_DEC_ENC(ofdma_cmn_eb2->info0,
					    HAL_RX_EHT_SIG_OFDMA_EB2_RU_ALLOC_2_6,
					    ru_126);
		eht->data[6] = cpu_to_le32(data);

		fallthrough;
	case HAL_EHT_BW_160:
		data = __le32_to_cpu(eht->data[3]);
		/* CC1 2::1 */
		data |=	IEEE80211_RADIOTAP_EHT_DATA3_RU_ALLOC_CC_1_2_1_KNOWN |
			ATH12K_LE64_DEC_ENC(ofdma_cmn_eb2->info0,
					    HAL_RX_EHT_SIG_OFDMA_EB2_RU_ALLOC_2_1,
					    ru_121);
		/* CC1 2::2 */
		data |=	IEEE80211_RADIOTAP_EHT_DATA3_RU_ALLOC_CC_1_2_2_KNOWN |
			ATH12K_LE64_DEC_ENC(ofdma_cmn_eb2->info0,
					    HAL_RX_EHT_SIG_OFDMA_EB2_RU_ALLOC_2_2,
					    ru_122);
		eht->data[3] = cpu_to_le32(data);

		fallthrough;
	case HAL_EHT_BW_80:
		data = __le32_to_cpu(eht->data[2]);
		/* CC1 1::2 */
		data |=	IEEE80211_RADIOTAP_EHT_DATA2_RU_ALLOC_CC_1_1_2_KNOWN |
			ATH12K_LE64_DEC_ENC(ofdma_cmn_eb1->info0,
					    HAL_RX_EHT_SIG_OFDMA_EB1_RU_ALLOC_1_2,
					    ru_112);
		eht->data[2] = cpu_to_le32(data);

		fallthrough;
	case HAL_EHT_BW_40:
		fallthrough;
	case HAL_EHT_BW_20:
		data = __le32_to_cpu(eht->data[1]);
		/* CC1 1::1 */
		data |=	IEEE80211_RADIOTAP_EHT_DATA1_RU_ALLOC_CC_1_1_1_KNOWN |
			ATH12K_LE64_DEC_ENC(ofdma_cmn_eb1->info0,
					    HAL_RX_EHT_SIG_OFDMA_EB1_RU_ALLOC_1_1,
					    ru_111);
		eht->data[1] = cpu_to_le32(data);
		break;
	default:
		break;
	}
}

static void
ath12k_dp_mon_hal_rx_parse_eht_sig_ofdma(const void *tlv,
					 struct hal_rx_mon_ppdu_info *ppdu_info)
{
	const struct hal_eht_sig_ofdma_cmn_eb *ofdma = tlv;

	ath12k_dp_mon_hal_rx_parse_usig_overflow(tlv, ppdu_info);
	ath12k_dp_mon_hal_rx_parse_ru_allocation(ofdma, ppdu_info);

	ath12k_dp_mon_hal_rx_parse_eht_non_mumimo_user(&ofdma->user_field.n_mu_mimo,
						       ppdu_info);
}

static void
ath12k_dp_mon_parse_eht_sig_hdr(struct hal_rx_mon_ppdu_info *ppdu_info,
				const void *tlv_data)
{
	ppdu_info->is_eht = true;

	if (ath12k_dp_mon_hal_rx_is_frame_type_ndp(&ppdu_info->u_sig_info))
		ath12k_dp_mon_hal_rx_parse_eht_sig_ndp(tlv_data, ppdu_info);
	else if (ath12k_dp_mon_hal_rx_is_non_ofdma(&ppdu_info->u_sig_info))
		ath12k_dp_mon_hal_rx_parse_eht_sig_non_ofdma(tlv_data, ppdu_info);
	else if (ath12k_dp_mon_hal_rx_is_ofdma(&ppdu_info->u_sig_info))
		ath12k_dp_mon_hal_rx_parse_eht_sig_ofdma(tlv_data, ppdu_info);
}

static inline enum ath12k_eht_ru_size
hal_rx_mon_hal_ru_size_to_ath12k_ru_size(u32 hal_ru_size)
{
	switch (hal_ru_size) {
	case HAL_EHT_RU_26:
		return ATH12K_EHT_RU_26;
	case HAL_EHT_RU_52:
		return ATH12K_EHT_RU_52;
	case HAL_EHT_RU_78:
		return ATH12K_EHT_RU_52_26;
	case HAL_EHT_RU_106:
		return ATH12K_EHT_RU_106;
	case HAL_EHT_RU_132:
		return ATH12K_EHT_RU_106_26;
	case HAL_EHT_RU_242:
		return ATH12K_EHT_RU_242;
	case HAL_EHT_RU_484:
		return ATH12K_EHT_RU_484;
	case HAL_EHT_RU_726:
		return ATH12K_EHT_RU_484_242;
	case HAL_EHT_RU_996:
		return ATH12K_EHT_RU_996;
	case HAL_EHT_RU_996x2:
		return ATH12K_EHT_RU_996x2;
	case HAL_EHT_RU_996x3:
		return ATH12K_EHT_RU_996x3;
	case HAL_EHT_RU_996x4:
		return ATH12K_EHT_RU_996x4;
	case HAL_EHT_RU_NONE:
		return ATH12K_EHT_RU_INVALID;
	case HAL_EHT_RU_996_484:
		return ATH12K_EHT_RU_996_484;
	case HAL_EHT_RU_996x2_484:
		return ATH12K_EHT_RU_996x2_484;
	case HAL_EHT_RU_996x3_484:
		return ATH12K_EHT_RU_996x3_484;
	case HAL_EHT_RU_996_484_242:
		return ATH12K_EHT_RU_996_484_242;
	default:
		return ATH12K_EHT_RU_INVALID;
	}
}

static inline u32
hal_rx_ul_ofdma_ru_size_to_width(enum ath12k_eht_ru_size ru_size)
{
	switch (ru_size) {
	case ATH12K_EHT_RU_26:
		return RU_26;
	case ATH12K_EHT_RU_52:
		return RU_52;
	case ATH12K_EHT_RU_52_26:
		return RU_52_26;
	case ATH12K_EHT_RU_106:
		return RU_106;
	case ATH12K_EHT_RU_106_26:
		return RU_106_26;
	case ATH12K_EHT_RU_242:
		return RU_242;
	case ATH12K_EHT_RU_484:
		return RU_484;
	case ATH12K_EHT_RU_484_242:
		return RU_484_242;
	case ATH12K_EHT_RU_996:
		return RU_996;
	case ATH12K_EHT_RU_996_484:
		return RU_996_484;
	case ATH12K_EHT_RU_996_484_242:
		return RU_996_484_242;
	case ATH12K_EHT_RU_996x2:
		return RU_2X996;
	case ATH12K_EHT_RU_996x2_484:
		return RU_2X996_484;
	case ATH12K_EHT_RU_996x3:
		return RU_3X996;
	case ATH12K_EHT_RU_996x3_484:
		return RU_3X996_484;
	case ATH12K_EHT_RU_996x4:
		return RU_4X996;
	default:
		return RU_INVALID;
	}
}

static void
ath12k_dp_mon_hal_rx_parse_user_info(const struct hal_receive_user_info *rx_usr_info,
				     u16 user_id,
				     struct hal_rx_mon_ppdu_info *ppdu_info)
{
	struct hal_rx_user_status *mon_rx_user_status = NULL;
	struct hal_rx_radiotap_eht *eht = &ppdu_info->eht_info.eht;
	enum ath12k_eht_ru_size rtap_ru_size = ATH12K_EHT_RU_INVALID;
	u32 ru_width, reception_type, ru_index = HAL_EHT_RU_INVALID;
	u32 ru_type_80_0, ru_start_index_80_0;
	u32 ru_type_80_1, ru_start_index_80_1;
	u32 ru_type_80_2, ru_start_index_80_2;
	u32 ru_type_80_3, ru_start_index_80_3;
	u32 ru_size = 0, num_80mhz_with_ru = 0;
	u64 ru_index_320mhz = 0;
	u32 ru_index_per80mhz;

	reception_type = le32_get_bits(rx_usr_info->info0,
				       HAL_RX_USR_INFO0_RECEPTION_TYPE);

	switch (reception_type) {
	case HAL_RECEPTION_TYPE_SU:
		ppdu_info->reception_type = HAL_RX_RECEPTION_TYPE_SU;
		break;
	case HAL_RECEPTION_TYPE_DL_MU_MIMO:
	case HAL_RECEPTION_TYPE_UL_MU_MIMO:
		ppdu_info->reception_type = HAL_RX_RECEPTION_TYPE_MU_MIMO;
		break;
	case HAL_RECEPTION_TYPE_DL_MU_OFMA:
	case HAL_RECEPTION_TYPE_UL_MU_OFDMA:
		ppdu_info->reception_type = HAL_RX_RECEPTION_TYPE_MU_OFDMA;
		break;
	case HAL_RECEPTION_TYPE_DL_MU_OFDMA_MIMO:
	case HAL_RECEPTION_TYPE_UL_MU_OFDMA_MIMO:
		ppdu_info->reception_type = HAL_RX_RECEPTION_TYPE_MU_OFDMA_MIMO;
	}

	ppdu_info->is_stbc = le32_get_bits(rx_usr_info->info0, HAL_RX_USR_INFO0_STBC);
	ppdu_info->ldpc = le32_get_bits(rx_usr_info->info2, HAL_RX_USR_INFO2_LDPC);
	ppdu_info->dcm = le32_get_bits(rx_usr_info->info2, HAL_RX_USR_INFO2_STA_DCM);
	ppdu_info->bw = le32_get_bits(rx_usr_info->info1, HAL_RX_USR_INFO1_RX_BW);
	ppdu_info->mcs = le32_get_bits(rx_usr_info->info1, HAL_RX_USR_INFO1_MCS);
	ppdu_info->nss = le32_get_bits(rx_usr_info->info2, HAL_RX_USR_INFO2_NSS) + 1;

	if (user_id < HAL_MAX_UL_MU_USERS) {
		mon_rx_user_status = &ppdu_info->userstats[user_id];
		mon_rx_user_status->mcs = ppdu_info->mcs;
		mon_rx_user_status->nss = ppdu_info->nss;
	}

	if (!(ppdu_info->reception_type == HAL_RX_RECEPTION_TYPE_MU_MIMO ||
	      ppdu_info->reception_type == HAL_RX_RECEPTION_TYPE_MU_OFDMA ||
	      ppdu_info->reception_type == HAL_RX_RECEPTION_TYPE_MU_OFDMA_MIMO))
		return;

	/* RU allocation present only for OFDMA reception */
	ru_type_80_0 = le32_get_bits(rx_usr_info->info2, HAL_RX_USR_INFO2_RU_TYPE_80_0);
	ru_start_index_80_0 = le32_get_bits(rx_usr_info->info3,
					    HAL_RX_USR_INFO3_RU_START_IDX_80_0);
	if (ru_type_80_0 != HAL_EHT_RU_NONE) {
		ru_size += ru_type_80_0;
		ru_index_per80mhz = ru_start_index_80_0;
		ru_index = ru_index_per80mhz;
		ru_index_320mhz |= HAL_RU_PER80(ru_type_80_0, 0, ru_index_per80mhz);
		num_80mhz_with_ru++;
	}

	ru_type_80_1 = le32_get_bits(rx_usr_info->info2, HAL_RX_USR_INFO2_RU_TYPE_80_1);
	ru_start_index_80_1 = le32_get_bits(rx_usr_info->info3,
					    HAL_RX_USR_INFO3_RU_START_IDX_80_1);
	if (ru_type_80_1 != HAL_EHT_RU_NONE) {
		ru_size += ru_type_80_1;
		ru_index_per80mhz = ru_start_index_80_1;
		ru_index = ru_index_per80mhz;
		ru_index_320mhz |= HAL_RU_PER80(ru_type_80_1, 1, ru_index_per80mhz);
		num_80mhz_with_ru++;
	}

	ru_type_80_2 = le32_get_bits(rx_usr_info->info2, HAL_RX_USR_INFO2_RU_TYPE_80_2);
	ru_start_index_80_2 = le32_get_bits(rx_usr_info->info3,
					    HAL_RX_USR_INFO3_RU_START_IDX_80_2);
	if (ru_type_80_2 != HAL_EHT_RU_NONE) {
		ru_size += ru_type_80_2;
		ru_index_per80mhz = ru_start_index_80_2;
		ru_index = ru_index_per80mhz;
		ru_index_320mhz |= HAL_RU_PER80(ru_type_80_2, 2, ru_index_per80mhz);
		num_80mhz_with_ru++;
	}

	ru_type_80_3 = le32_get_bits(rx_usr_info->info2, HAL_RX_USR_INFO2_RU_TYPE_80_3);
	ru_start_index_80_3 = le32_get_bits(rx_usr_info->info2,
					    HAL_RX_USR_INFO3_RU_START_IDX_80_3);
	if (ru_type_80_3 != HAL_EHT_RU_NONE) {
		ru_size += ru_type_80_3;
		ru_index_per80mhz = ru_start_index_80_3;
		ru_index = ru_index_per80mhz;
		ru_index_320mhz |= HAL_RU_PER80(ru_type_80_3, 3, ru_index_per80mhz);
		num_80mhz_with_ru++;
	}

	if (num_80mhz_with_ru > 1) {
		/* Calculate the MRU index */
		switch (ru_index_320mhz) {
		case HAL_EHT_RU_996_484_0:
		case HAL_EHT_RU_996x2_484_0:
		case HAL_EHT_RU_996x3_484_0:
			ru_index = 0;
			break;
		case HAL_EHT_RU_996_484_1:
		case HAL_EHT_RU_996x2_484_1:
		case HAL_EHT_RU_996x3_484_1:
			ru_index = 1;
			break;
		case HAL_EHT_RU_996_484_2:
		case HAL_EHT_RU_996x2_484_2:
		case HAL_EHT_RU_996x3_484_2:
			ru_index = 2;
			break;
		case HAL_EHT_RU_996_484_3:
		case HAL_EHT_RU_996x2_484_3:
		case HAL_EHT_RU_996x3_484_3:
			ru_index = 3;
			break;
		case HAL_EHT_RU_996_484_4:
		case HAL_EHT_RU_996x2_484_4:
		case HAL_EHT_RU_996x3_484_4:
			ru_index = 4;
			break;
		case HAL_EHT_RU_996_484_5:
		case HAL_EHT_RU_996x2_484_5:
		case HAL_EHT_RU_996x3_484_5:
			ru_index = 5;
			break;
		case HAL_EHT_RU_996_484_6:
		case HAL_EHT_RU_996x2_484_6:
		case HAL_EHT_RU_996x3_484_6:
			ru_index = 6;
			break;
		case HAL_EHT_RU_996_484_7:
		case HAL_EHT_RU_996x2_484_7:
		case HAL_EHT_RU_996x3_484_7:
			ru_index = 7;
			break;
		case HAL_EHT_RU_996x2_484_8:
			ru_index = 8;
			break;
		case HAL_EHT_RU_996x2_484_9:
			ru_index = 9;
			break;
		case HAL_EHT_RU_996x2_484_10:
			ru_index = 10;
			break;
		case HAL_EHT_RU_996x2_484_11:
			ru_index = 11;
			break;
		default:
			ru_index = HAL_EHT_RU_INVALID;
			break;
		}

		ru_size += 4;
	}

	rtap_ru_size = hal_rx_mon_hal_ru_size_to_ath12k_ru_size(ru_size);
	if (rtap_ru_size != ATH12K_EHT_RU_INVALID) {
		u32 known, data;

		known = __le32_to_cpu(eht->known);
		known |= IEEE80211_RADIOTAP_EHT_KNOWN_RU_MRU_SIZE_OM;
		eht->known = cpu_to_le32(known);

		data = __le32_to_cpu(eht->data[1]);
		data |=	u32_encode_bits(rtap_ru_size,
					IEEE80211_RADIOTAP_EHT_DATA1_RU_SIZE);
		eht->data[1] = cpu_to_le32(data);
	}

	if (ru_index != HAL_EHT_RU_INVALID) {
		u32 known, data;

		known = __le32_to_cpu(eht->known);
		known |= IEEE80211_RADIOTAP_EHT_KNOWN_RU_MRU_INDEX_OM;
		eht->known = cpu_to_le32(known);

		data = __le32_to_cpu(eht->data[1]);
		data |=	u32_encode_bits(rtap_ru_size,
					IEEE80211_RADIOTAP_EHT_DATA1_RU_INDEX);
		eht->data[1] = cpu_to_le32(data);
	}

	if (mon_rx_user_status && ru_index != HAL_EHT_RU_INVALID &&
	    rtap_ru_size != ATH12K_EHT_RU_INVALID) {
		mon_rx_user_status->ul_ofdma_ru_start_index = ru_index;
		mon_rx_user_status->ul_ofdma_ru_size = rtap_ru_size;

		ru_width = hal_rx_ul_ofdma_ru_size_to_width(rtap_ru_size);

		mon_rx_user_status->ul_ofdma_ru_width = ru_width;
		mon_rx_user_status->ofdma_info_valid = 1;
	}
}

static enum hal_rx_mon_status
ath12k_dp_mon_rx_parse_status_tlv(struct ath12k *ar,
				  struct ath12k_mon_data *pmon,
				  const struct hal_tlv_64_hdr *tlv)
{
	struct hal_rx_mon_ppdu_info *ppdu_info = &pmon->mon_ppdu_info;
	const void *tlv_data = tlv->value;
	u32 info[7], userid;
	u16 tlv_tag, tlv_len;

	tlv_tag = le64_get_bits(tlv->tl, HAL_TLV_64_HDR_TAG);
	tlv_len = le64_get_bits(tlv->tl, HAL_TLV_64_HDR_LEN);
	userid = le64_get_bits(tlv->tl, HAL_TLV_64_USR_ID);

	if (ppdu_info->tlv_aggr.in_progress && ppdu_info->tlv_aggr.tlv_tag != tlv_tag) {
		ath12k_dp_mon_parse_eht_sig_hdr(ppdu_info, ppdu_info->tlv_aggr.buf);

		ppdu_info->tlv_aggr.in_progress = false;
		ppdu_info->tlv_aggr.cur_len = 0;
	}

	switch (tlv_tag) {
	case HAL_RX_PPDU_START: {
		const struct hal_rx_ppdu_start *ppdu_start = tlv_data;

		u64 ppdu_ts = ath12k_le32hilo_to_u64(ppdu_start->ppdu_start_ts_63_32,
						     ppdu_start->ppdu_start_ts_31_0);

		info[0] = __le32_to_cpu(ppdu_start->info0);

		ppdu_info->ppdu_id = u32_get_bits(info[0],
						  HAL_RX_PPDU_START_INFO0_PPDU_ID);

		info[1] = __le32_to_cpu(ppdu_start->info1);
		ppdu_info->chan_num = u32_get_bits(info[1],
						   HAL_RX_PPDU_START_INFO1_CHAN_NUM);
		ppdu_info->freq = u32_get_bits(info[1],
					       HAL_RX_PPDU_START_INFO1_CHAN_FREQ);
		ppdu_info->ppdu_ts = ppdu_ts;

		if (ppdu_info->ppdu_id != ppdu_info->last_ppdu_id) {
			ppdu_info->last_ppdu_id = ppdu_info->ppdu_id;
			ppdu_info->num_users = 0;
			memset(&ppdu_info->mpdu_fcs_ok_bitmap, 0,
			       HAL_RX_NUM_WORDS_PER_PPDU_BITMAP *
			       sizeof(ppdu_info->mpdu_fcs_ok_bitmap[0]));
		}
		break;
	}
	case HAL_RX_PPDU_END_USER_STATS: {
		const struct hal_rx_ppdu_end_user_stats *eu_stats = tlv_data;
		u32 tid_bitmap;

		info[0] = __le32_to_cpu(eu_stats->info0);
		info[1] = __le32_to_cpu(eu_stats->info1);
		info[2] = __le32_to_cpu(eu_stats->info2);
		info[4] = __le32_to_cpu(eu_stats->info4);
		info[5] = __le32_to_cpu(eu_stats->info5);
		info[6] = __le32_to_cpu(eu_stats->info6);

		ppdu_info->ast_index =
			u32_get_bits(info[2], HAL_RX_PPDU_END_USER_STATS_INFO2_AST_INDEX);
		ppdu_info->fc_valid =
			u32_get_bits(info[1], HAL_RX_PPDU_END_USER_STATS_INFO1_FC_VALID);
		tid_bitmap = u32_get_bits(info[6],
					  HAL_RX_PPDU_END_USER_STATS_INFO6_TID_BITMAP);
		ppdu_info->tid = ffs(tid_bitmap) - 1;
		ppdu_info->tcp_msdu_count =
			u32_get_bits(info[4],
				     HAL_RX_PPDU_END_USER_STATS_INFO4_TCP_MSDU_CNT);
		ppdu_info->udp_msdu_count =
			u32_get_bits(info[4],
				     HAL_RX_PPDU_END_USER_STATS_INFO4_UDP_MSDU_CNT);
		ppdu_info->other_msdu_count =
			u32_get_bits(info[5],
				     HAL_RX_PPDU_END_USER_STATS_INFO5_OTHER_MSDU_CNT);
		ppdu_info->tcp_ack_msdu_count =
			u32_get_bits(info[5],
				     HAL_RX_PPDU_END_USER_STATS_INFO5_TCP_ACK_MSDU_CNT);
		ppdu_info->preamble_type =
			u32_get_bits(info[1],
				     HAL_RX_PPDU_END_USER_STATS_INFO1_PKT_TYPE);
		ppdu_info->num_mpdu_fcs_ok =
			u32_get_bits(info[1],
				     HAL_RX_PPDU_END_USER_STATS_INFO1_MPDU_CNT_FCS_OK);
		ppdu_info->num_mpdu_fcs_err =
			u32_get_bits(info[0],
				     HAL_RX_PPDU_END_USER_STATS_INFO0_MPDU_CNT_FCS_ERR);
		ppdu_info->peer_id =
			u32_get_bits(info[0], HAL_RX_PPDU_END_USER_STATS_INFO0_PEER_ID);

		switch (ppdu_info->preamble_type) {
		case HAL_RX_PREAMBLE_11N:
			ppdu_info->ht_flags = 1;
			break;
		case HAL_RX_PREAMBLE_11AC:
			ppdu_info->vht_flags = 1;
			break;
		case HAL_RX_PREAMBLE_11AX:
			ppdu_info->he_flags = 1;
			break;
		case HAL_RX_PREAMBLE_11BE:
			ppdu_info->is_eht = true;
			break;
		default:
			break;
		}

		if (userid < HAL_MAX_UL_MU_USERS) {
			struct hal_rx_user_status *rxuser_stats =
				&ppdu_info->userstats[userid];

			if (ppdu_info->num_mpdu_fcs_ok > 1 ||
			    ppdu_info->num_mpdu_fcs_err > 1)
				ppdu_info->userstats[userid].ampdu_present = true;

			ppdu_info->num_users += 1;

			ath12k_dp_mon_rx_handle_ofdma_info(eu_stats, rxuser_stats);
			ath12k_dp_mon_rx_populate_mu_user_info(eu_stats, ppdu_info,
							       rxuser_stats);
		}
		ppdu_info->mpdu_fcs_ok_bitmap[0] = __le32_to_cpu(eu_stats->rsvd1[0]);
		ppdu_info->mpdu_fcs_ok_bitmap[1] = __le32_to_cpu(eu_stats->rsvd1[1]);
		break;
	}
	case HAL_RX_PPDU_END_USER_STATS_EXT: {
		const struct hal_rx_ppdu_end_user_stats_ext *eu_stats = tlv_data;

		ppdu_info->mpdu_fcs_ok_bitmap[2] = __le32_to_cpu(eu_stats->info1);
		ppdu_info->mpdu_fcs_ok_bitmap[3] = __le32_to_cpu(eu_stats->info2);
		ppdu_info->mpdu_fcs_ok_bitmap[4] = __le32_to_cpu(eu_stats->info3);
		ppdu_info->mpdu_fcs_ok_bitmap[5] = __le32_to_cpu(eu_stats->info4);
		ppdu_info->mpdu_fcs_ok_bitmap[6] = __le32_to_cpu(eu_stats->info5);
		ppdu_info->mpdu_fcs_ok_bitmap[7] = __le32_to_cpu(eu_stats->info6);
		break;
	}
	case HAL_PHYRX_HT_SIG:
		ath12k_dp_mon_parse_ht_sig(tlv_data, ppdu_info);
		break;

	case HAL_PHYRX_L_SIG_B:
		ath12k_dp_mon_parse_l_sig_b(tlv_data, ppdu_info);
		break;

	case HAL_PHYRX_L_SIG_A:
		ath12k_dp_mon_parse_l_sig_a(tlv_data, ppdu_info);
		break;

	case HAL_PHYRX_VHT_SIG_A:
		ath12k_dp_mon_parse_vht_sig_a(tlv_data, ppdu_info);
		break;

	case HAL_PHYRX_HE_SIG_A_SU:
		ath12k_dp_mon_parse_he_sig_su(tlv_data, ppdu_info);
		break;

	case HAL_PHYRX_HE_SIG_A_MU_DL:
		ath12k_dp_mon_parse_he_sig_mu(tlv_data, ppdu_info);
		break;

	case HAL_PHYRX_HE_SIG_B1_MU:
		ath12k_dp_mon_parse_he_sig_b1_mu(tlv_data, ppdu_info);
		break;

	case HAL_PHYRX_HE_SIG_B2_MU:
		ath12k_dp_mon_parse_he_sig_b2_mu(tlv_data, ppdu_info);
		break;

	case HAL_PHYRX_HE_SIG_B2_OFDMA:
		ath12k_dp_mon_parse_he_sig_b2_ofdma(tlv_data, ppdu_info);
		break;

	case HAL_PHYRX_RSSI_LEGACY: {
		const struct hal_rx_phyrx_rssi_legacy_info *rssi = tlv_data;

		info[0] = __le32_to_cpu(rssi->info0);
		info[1] = __le32_to_cpu(rssi->info1);

		/* TODO: Please note that the combined rssi will not be accurate
		 * in MU case. Rssi in MU needs to be retrieved from
		 * PHYRX_OTHER_RECEIVE_INFO TLV.
		 */
		ppdu_info->rssi_comb =
			u32_get_bits(info[1],
				     HAL_RX_PHYRX_RSSI_LEGACY_INFO_INFO1_RSSI_COMB);

		ppdu_info->bw = u32_get_bits(info[0],
					     HAL_RX_PHYRX_RSSI_LEGACY_INFO_INFO0_RX_BW);
		break;
	}
	case HAL_PHYRX_OTHER_RECEIVE_INFO: {
		const struct hal_phyrx_common_user_info *cmn_usr_info = tlv_data;

		ppdu_info->gi = le32_get_bits(cmn_usr_info->info0,
					      HAL_RX_PHY_CMN_USER_INFO0_GI);
		break;
	}
	case HAL_RX_PPDU_START_USER_INFO:
		ath12k_dp_mon_hal_rx_parse_user_info(tlv_data, userid, ppdu_info);
		break;

	case HAL_RXPCU_PPDU_END_INFO: {
		const struct hal_rx_ppdu_end_duration *ppdu_rx_duration = tlv_data;

		info[0] = __le32_to_cpu(ppdu_rx_duration->info0);
		ppdu_info->rx_duration =
			u32_get_bits(info[0], HAL_RX_PPDU_END_DURATION);
		ppdu_info->tsft = __le32_to_cpu(ppdu_rx_duration->rsvd0[1]);
		ppdu_info->tsft = (ppdu_info->tsft << 32) |
				   __le32_to_cpu(ppdu_rx_duration->rsvd0[0]);
		break;
	}
	case HAL_RX_MPDU_START: {
		const struct hal_rx_mpdu_start *mpdu_start = tlv_data;
		u16 peer_id;

		info[1] = __le32_to_cpu(mpdu_start->info1);
		peer_id = u32_get_bits(info[1], HAL_RX_MPDU_START_INFO1_PEERID);
		if (peer_id)
			ppdu_info->peer_id = peer_id;

		ppdu_info->mpdu_len += u32_get_bits(info[1],
						    HAL_RX_MPDU_START_INFO2_MPDU_LEN);
		if (userid < HAL_MAX_UL_MU_USERS) {
			info[0] = __le32_to_cpu(mpdu_start->info0);
			ppdu_info->userid = userid;
			ppdu_info->userstats[userid].ampdu_id =
				u32_get_bits(info[0], HAL_RX_MPDU_START_INFO0_PPDU_ID);
		}

		return HAL_RX_MON_STATUS_MPDU_START;
	}
	case HAL_RX_MSDU_START:
		/* TODO: add msdu start parsing logic */
		break;
	case HAL_MON_BUF_ADDR:
		return HAL_RX_MON_STATUS_BUF_ADDR;
	case HAL_RX_MSDU_END:
		return HAL_RX_MON_STATUS_MSDU_END;
	case HAL_RX_MPDU_END:
		return HAL_RX_MON_STATUS_MPDU_END;
	case HAL_PHYRX_GENERIC_U_SIG:
		ath12k_dp_mon_hal_rx_parse_u_sig_hdr(tlv_data, ppdu_info);
		break;
	case HAL_PHYRX_GENERIC_EHT_SIG:
		/* Handle the case where aggregation is in progress
		 * or the current TLV is one of the TLVs which should be
		 * aggregated
		 */
		if (!ppdu_info->tlv_aggr.in_progress) {
			ppdu_info->tlv_aggr.in_progress = true;
			ppdu_info->tlv_aggr.tlv_tag = tlv_tag;
			ppdu_info->tlv_aggr.cur_len = 0;
		}

		ppdu_info->is_eht = true;

		ath12k_dp_mon_hal_aggr_tlv(ppdu_info, tlv_len, tlv_data);
		break;
	case HAL_DUMMY:
		return HAL_RX_MON_STATUS_BUF_DONE;
	case HAL_RX_PPDU_END_STATUS_DONE:
	case 0:
		return HAL_RX_MON_STATUS_PPDU_DONE;
	default:
		break;
	}

	return HAL_RX_MON_STATUS_PPDU_NOT_DONE;
}

static void ath12k_dp_mon_rx_msdus_set_payload(struct ath12k *ar,
					       struct sk_buff *head_msdu,
					       struct sk_buff *tail_msdu)
{
	u32 rx_pkt_offset, l2_hdr_offset;

	rx_pkt_offset = ar->ab->hal.hal_desc_sz;
	l2_hdr_offset =
		ath12k_dp_rx_h_l3pad(ar->ab, (struct hal_rx_desc *)tail_msdu->data);
	skb_pull(head_msdu, rx_pkt_offset + l2_hdr_offset);
}

static struct sk_buff *
ath12k_dp_mon_rx_merg_msdus(struct ath12k *ar,
			    struct sk_buff *head_msdu, struct sk_buff *tail_msdu,
			    struct ieee80211_rx_status *rxs, bool *fcs_err)
{
	struct ath12k_base *ab = ar->ab;
	struct sk_buff *msdu, *mpdu_buf, *prev_buf, *head_frag_list;
	struct hal_rx_desc *rx_desc, *tail_rx_desc;
	u8 *hdr_desc, *dest, decap_format;
	struct ieee80211_hdr_3addr *wh;
	u32 err_bitmap, frag_list_sum_len = 0;

	mpdu_buf = NULL;

	if (!head_msdu)
		goto err_merge_fail;

	rx_desc = (struct hal_rx_desc *)head_msdu->data;
	tail_rx_desc = (struct hal_rx_desc *)tail_msdu->data;

	err_bitmap = ath12k_dp_rx_h_mpdu_err(ab, tail_rx_desc);
	if (err_bitmap & HAL_RX_MPDU_ERR_FCS)
		*fcs_err = true;

	decap_format = ath12k_dp_rx_h_decap_type(ab, tail_rx_desc);

	ath12k_dp_rx_h_ppdu(ar, tail_rx_desc, rxs);

	if (decap_format == DP_RX_DECAP_TYPE_RAW) {
		ath12k_dp_mon_rx_msdus_set_payload(ar, head_msdu, tail_msdu);

		prev_buf = head_msdu;
		msdu = head_msdu->next;
		head_frag_list = NULL;

		while (msdu) {
			ath12k_dp_mon_rx_msdus_set_payload(ar, msdu, tail_msdu);

			if (!head_frag_list)
				head_frag_list = msdu;

			frag_list_sum_len += msdu->len;
			prev_buf = msdu;
			msdu = msdu->next;
		}

		prev_buf->next = NULL;

		skb_trim(prev_buf, prev_buf->len - HAL_RX_FCS_LEN);
		if (head_frag_list) {
			skb_shinfo(head_msdu)->frag_list = head_frag_list;
			head_msdu->data_len = frag_list_sum_len;
			head_msdu->len += head_msdu->data_len;
			head_msdu->next = NULL;
		}
	} else if (decap_format == DP_RX_DECAP_TYPE_NATIVE_WIFI) {
		u8 qos_pkt = 0;

		rx_desc = (struct hal_rx_desc *)head_msdu->data;
		hdr_desc =
			ab->hal_rx_ops->rx_desc_get_msdu_payload(rx_desc);

		/* Base size */
		wh = (struct ieee80211_hdr_3addr *)hdr_desc;

		if (ieee80211_is_data_qos(wh->frame_control))
			qos_pkt = 1;

		msdu = head_msdu;

		while (msdu) {
			ath12k_dp_mon_rx_msdus_set_payload(ar, msdu, tail_msdu);
			if (qos_pkt) {
				dest = skb_push(msdu, sizeof(__le16));
				if (!dest)
					goto err_merge_fail;
				memcpy(dest, hdr_desc, sizeof(struct ieee80211_qos_hdr));
			}
			prev_buf = msdu;
			msdu = msdu->next;
		}
		dest = skb_put(prev_buf, HAL_RX_FCS_LEN);
		if (!dest)
			goto err_merge_fail;

		ath12k_dbg(ab, ATH12K_DBG_DATA,
			   "mpdu_buf %p mpdu_buf->len %u",
			   prev_buf, prev_buf->len);
	} else {
		ath12k_dbg(ab, ATH12K_DBG_DATA,
			   "decap format %d is not supported!\n",
			   decap_format);
		goto err_merge_fail;
	}

	return head_msdu;

err_merge_fail:
	if (mpdu_buf && decap_format != DP_RX_DECAP_TYPE_RAW) {
		ath12k_dbg(ab, ATH12K_DBG_DATA,
			   "err_merge_fail mpdu_buf %p", mpdu_buf);
		/* Free the head buffer */
		dev_kfree_skb_any(mpdu_buf);
	}
	return NULL;
}

static void
ath12k_dp_mon_rx_update_radiotap_he(struct hal_rx_mon_ppdu_info *rx_status,
				    u8 *rtap_buf)
{
	u32 rtap_len = 0;

	put_unaligned_le16(rx_status->he_data1, &rtap_buf[rtap_len]);
	rtap_len += 2;

	put_unaligned_le16(rx_status->he_data2, &rtap_buf[rtap_len]);
	rtap_len += 2;

	put_unaligned_le16(rx_status->he_data3, &rtap_buf[rtap_len]);
	rtap_len += 2;

	put_unaligned_le16(rx_status->he_data4, &rtap_buf[rtap_len]);
	rtap_len += 2;

	put_unaligned_le16(rx_status->he_data5, &rtap_buf[rtap_len]);
	rtap_len += 2;

	put_unaligned_le16(rx_status->he_data6, &rtap_buf[rtap_len]);
}

static void
ath12k_dp_mon_rx_update_radiotap_he_mu(struct hal_rx_mon_ppdu_info *rx_status,
				       u8 *rtap_buf)
{
	u32 rtap_len = 0;

	put_unaligned_le16(rx_status->he_flags1, &rtap_buf[rtap_len]);
	rtap_len += 2;

	put_unaligned_le16(rx_status->he_flags2, &rtap_buf[rtap_len]);
	rtap_len += 2;

	rtap_buf[rtap_len] = rx_status->he_RU[0];
	rtap_len += 1;

	rtap_buf[rtap_len] = rx_status->he_RU[1];
	rtap_len += 1;

	rtap_buf[rtap_len] = rx_status->he_RU[2];
	rtap_len += 1;

	rtap_buf[rtap_len] = rx_status->he_RU[3];
}

static void ath12k_dp_mon_update_radiotap(struct ath12k *ar,
					  struct hal_rx_mon_ppdu_info *ppduinfo,
					  struct sk_buff *mon_skb,
					  struct ieee80211_rx_status *rxs)
{
	struct ieee80211_supported_band *sband;
	u8 *ptr = NULL;

	rxs->flag |= RX_FLAG_MACTIME_START;
	rxs->signal = ppduinfo->rssi_comb + ATH12K_DEFAULT_NOISE_FLOOR;
	rxs->nss = ppduinfo->nss + 1;

	if (ppduinfo->userstats[ppduinfo->userid].ampdu_present) {
		rxs->flag |= RX_FLAG_AMPDU_DETAILS;
		rxs->ampdu_reference = ppduinfo->userstats[ppduinfo->userid].ampdu_id;
	}

	if (ppduinfo->is_eht || ppduinfo->eht_usig) {
		struct ieee80211_radiotap_tlv *tlv;
		struct ieee80211_radiotap_eht *eht;
		struct ieee80211_radiotap_eht_usig *usig;
		u16 len = 0, i, eht_len, usig_len;
		u8 user;

		if (ppduinfo->is_eht) {
			eht_len = struct_size(eht,
					      user_info,
					      ppduinfo->eht_info.num_user_info);
			len += sizeof(*tlv) + eht_len;
		}

		if (ppduinfo->eht_usig) {
			usig_len = sizeof(*usig);
			len += sizeof(*tlv) + usig_len;
		}

		rxs->flag |= RX_FLAG_RADIOTAP_TLV_AT_END;
		rxs->encoding = RX_ENC_EHT;

		skb_reset_mac_header(mon_skb);

		tlv = skb_push(mon_skb, len);

		if (ppduinfo->is_eht) {
			tlv->type = cpu_to_le16(IEEE80211_RADIOTAP_EHT);
			tlv->len = cpu_to_le16(eht_len);

			eht = (struct ieee80211_radiotap_eht *)tlv->data;
			eht->known = ppduinfo->eht_info.eht.known;

			for (i = 0;
			     i < ARRAY_SIZE(eht->data) &&
			     i < ARRAY_SIZE(ppduinfo->eht_info.eht.data);
			     i++)
				eht->data[i] = ppduinfo->eht_info.eht.data[i];

			for (user = 0; user < ppduinfo->eht_info.num_user_info; user++)
				put_unaligned_le32(ppduinfo->eht_info.user_info[user],
						   &eht->user_info[user]);

			tlv = (struct ieee80211_radiotap_tlv *)&tlv->data[eht_len];
		}

		if (ppduinfo->eht_usig) {
			tlv->type = cpu_to_le16(IEEE80211_RADIOTAP_EHT_USIG);
			tlv->len = cpu_to_le16(usig_len);

			usig = (struct ieee80211_radiotap_eht_usig *)tlv->data;
			*usig = ppduinfo->u_sig_info.usig;
		}
	} else if (ppduinfo->he_mu_flags) {
		rxs->flag |= RX_FLAG_RADIOTAP_HE_MU;
		rxs->encoding = RX_ENC_HE;
		ptr = skb_push(mon_skb, sizeof(struct ieee80211_radiotap_he_mu));
		ath12k_dp_mon_rx_update_radiotap_he_mu(ppduinfo, ptr);
	} else if (ppduinfo->he_flags) {
		rxs->flag |= RX_FLAG_RADIOTAP_HE;
		rxs->encoding = RX_ENC_HE;
		ptr = skb_push(mon_skb, sizeof(struct ieee80211_radiotap_he));
		ath12k_dp_mon_rx_update_radiotap_he(ppduinfo, ptr);
		rxs->rate_idx = ppduinfo->rate;
	} else if (ppduinfo->vht_flags) {
		rxs->encoding = RX_ENC_VHT;
		rxs->rate_idx = ppduinfo->rate;
	} else if (ppduinfo->ht_flags) {
		rxs->encoding = RX_ENC_HT;
		rxs->rate_idx = ppduinfo->rate;
	} else {
		rxs->encoding = RX_ENC_LEGACY;
		sband = &ar->mac.sbands[rxs->band];
		rxs->rate_idx = ath12k_mac_hw_rate_to_idx(sband, ppduinfo->rate,
							  ppduinfo->cck_flag);
	}

	rxs->mactime = ppduinfo->tsft;
}

static void ath12k_dp_mon_rx_deliver_msdu(struct ath12k *ar, struct napi_struct *napi,
					  struct sk_buff *msdu,
					  struct ieee80211_rx_status *status)
{
	static const struct ieee80211_radiotap_he known = {
		.data1 = cpu_to_le16(IEEE80211_RADIOTAP_HE_DATA1_DATA_MCS_KNOWN |
				     IEEE80211_RADIOTAP_HE_DATA1_BW_RU_ALLOC_KNOWN),
		.data2 = cpu_to_le16(IEEE80211_RADIOTAP_HE_DATA2_GI_KNOWN),
	};
	struct ieee80211_rx_status *rx_status;
	struct ieee80211_radiotap_he *he = NULL;
	struct ieee80211_sta *pubsta = NULL;
	struct ath12k_peer *peer;
	struct ath12k_skb_rxcb *rxcb = ATH12K_SKB_RXCB(msdu);
	u8 decap = DP_RX_DECAP_TYPE_RAW;
	bool is_mcbc = rxcb->is_mcbc;
	bool is_eapol_tkip = rxcb->is_eapol;

	status->link_valid = 0;

	if ((status->encoding == RX_ENC_HE) && !(status->flag & RX_FLAG_RADIOTAP_HE) &&
	    !(status->flag & RX_FLAG_SKIP_MONITOR)) {
		he = skb_push(msdu, sizeof(known));
		memcpy(he, &known, sizeof(known));
		status->flag |= RX_FLAG_RADIOTAP_HE;
	}

	if (!(status->flag & RX_FLAG_ONLY_MONITOR))
		decap = ath12k_dp_rx_h_decap_type(ar->ab, rxcb->rx_desc);
	spin_lock_bh(&ar->ab->base_lock);
	peer = ath12k_dp_rx_h_find_peer(ar->ab, msdu);
	if (peer && peer->sta) {
		pubsta = peer->sta;
		if (pubsta->valid_links) {
			status->link_valid = 1;
			status->link_id = peer->link_id;
		}
	}

	spin_unlock_bh(&ar->ab->base_lock);

	ath12k_dbg(ar->ab, ATH12K_DBG_DATA,
		   "rx skb %p len %u peer %pM %u %s %s%s%s%s%s%s%s%s %srate_idx %u vht_nss %u freq %u band %u flag 0x%x fcs-err %i mic-err %i amsdu-more %i\n",
		   msdu,
		   msdu->len,
		   peer ? peer->addr : NULL,
		   rxcb->tid,
		   (is_mcbc) ? "mcast" : "ucast",
		   (status->encoding == RX_ENC_LEGACY) ? "legacy" : "",
		   (status->encoding == RX_ENC_HT) ? "ht" : "",
		   (status->encoding == RX_ENC_VHT) ? "vht" : "",
		   (status->encoding == RX_ENC_HE) ? "he" : "",
		   (status->bw == RATE_INFO_BW_40) ? "40" : "",
		   (status->bw == RATE_INFO_BW_80) ? "80" : "",
		   (status->bw == RATE_INFO_BW_160) ? "160" : "",
		   (status->bw == RATE_INFO_BW_320) ? "320" : "",
		   status->enc_flags & RX_ENC_FLAG_SHORT_GI ? "sgi " : "",
		   status->rate_idx,
		   status->nss,
		   status->freq,
		   status->band, status->flag,
		   !!(status->flag & RX_FLAG_FAILED_FCS_CRC),
		   !!(status->flag & RX_FLAG_MMIC_ERROR),
		   !!(status->flag & RX_FLAG_AMSDU_MORE));

	ath12k_dbg_dump(ar->ab, ATH12K_DBG_DP_RX, NULL, "dp rx msdu: ",
			msdu->data, msdu->len);
	rx_status = IEEE80211_SKB_RXCB(msdu);
	*rx_status = *status;

	/* TODO: trace rx packet */

	/* PN for multicast packets are not validate in HW,
	 * so skip 802.3 rx path
	 * Also, fast_rx expects the STA to be authorized, hence
	 * eapol packets are sent in slow path.
	 */
	if (decap == DP_RX_DECAP_TYPE_ETHERNET2_DIX && !is_eapol_tkip &&
	    !(is_mcbc && rx_status->flag & RX_FLAG_DECRYPTED))
		rx_status->flag |= RX_FLAG_8023;

	ieee80211_rx_napi(ath12k_ar_to_hw(ar), pubsta, msdu, napi);
}

static int ath12k_dp_mon_rx_deliver(struct ath12k *ar,
				    struct sk_buff *head_msdu, struct sk_buff *tail_msdu,
				    struct hal_rx_mon_ppdu_info *ppduinfo,
				    struct napi_struct *napi)
{
	struct ath12k_pdev_dp *dp = &ar->dp;
	struct sk_buff *mon_skb, *skb_next, *header;
	struct ieee80211_rx_status *rxs = &dp->rx_status;
	bool fcs_err = false;

	mon_skb = ath12k_dp_mon_rx_merg_msdus(ar,
					      head_msdu, tail_msdu,
					      rxs, &fcs_err);
	if (!mon_skb)
		goto mon_deliver_fail;

	header = mon_skb;
	rxs->flag = 0;

	if (fcs_err)
		rxs->flag = RX_FLAG_FAILED_FCS_CRC;

	do {
		skb_next = mon_skb->next;
		if (!skb_next)
			rxs->flag &= ~RX_FLAG_AMSDU_MORE;
		else
			rxs->flag |= RX_FLAG_AMSDU_MORE;

		if (mon_skb == header) {
			header = NULL;
			rxs->flag &= ~RX_FLAG_ALLOW_SAME_PN;
		} else {
			rxs->flag |= RX_FLAG_ALLOW_SAME_PN;
		}
		rxs->flag |= RX_FLAG_ONLY_MONITOR;
		ath12k_dp_mon_update_radiotap(ar, ppduinfo, mon_skb, rxs);
		ath12k_dp_mon_rx_deliver_msdu(ar, napi, mon_skb, rxs);
		mon_skb = skb_next;
	} while (mon_skb);
	rxs->flag = 0;

	return 0;

mon_deliver_fail:
	mon_skb = head_msdu;
	while (mon_skb) {
		skb_next = mon_skb->next;
		dev_kfree_skb_any(mon_skb);
		mon_skb = skb_next;
	}
	return -EINVAL;
}

static int ath12k_dp_pkt_set_pktlen(struct sk_buff *skb, u32 len)
{
	if (skb->len > len) {
		skb_trim(skb, len);
	} else {
		if (skb_tailroom(skb) < len - skb->len) {
			if ((pskb_expand_head(skb, 0,
					      len - skb->len - skb_tailroom(skb),
					      GFP_ATOMIC))) {
				return -ENOMEM;
			}
		}
		skb_put(skb, (len - skb->len));
	}

	return 0;
}

static void ath12k_dp_mon_parse_rx_msdu_end_err(u32 info, u32 *errmap)
{
	if (info & RX_MSDU_END_INFO13_FCS_ERR)
		*errmap |= HAL_RX_MPDU_ERR_FCS;

	if (info & RX_MSDU_END_INFO13_DECRYPT_ERR)
		*errmap |= HAL_RX_MPDU_ERR_DECRYPT;

	if (info & RX_MSDU_END_INFO13_TKIP_MIC_ERR)
		*errmap |= HAL_RX_MPDU_ERR_TKIP_MIC;

	if (info & RX_MSDU_END_INFO13_A_MSDU_ERROR)
		*errmap |= HAL_RX_MPDU_ERR_AMSDU_ERR;

	if (info & RX_MSDU_END_INFO13_OVERFLOW_ERR)
		*errmap |= HAL_RX_MPDU_ERR_OVERFLOW;

	if (info & RX_MSDU_END_INFO13_MSDU_LEN_ERR)
		*errmap |= HAL_RX_MPDU_ERR_MSDU_LEN;

	if (info & RX_MSDU_END_INFO13_MPDU_LEN_ERR)
		*errmap |= HAL_RX_MPDU_ERR_MPDU_LEN;
}

static int
ath12k_dp_mon_parse_status_msdu_end(struct ath12k_mon_data *pmon,
				    const struct hal_rx_msdu_end *msdu_end)
{
	struct dp_mon_mpdu *mon_mpdu = pmon->mon_mpdu;

	ath12k_dp_mon_parse_rx_msdu_end_err(__le32_to_cpu(msdu_end->info2),
					    &mon_mpdu->err_bitmap);

	mon_mpdu->decap_format = le32_get_bits(msdu_end->info1,
					       RX_MSDU_END_INFO11_DECAP_FORMAT);

	return 0;
}

static int
ath12k_dp_mon_parse_status_buf(struct ath12k *ar,
			       struct ath12k_mon_data *pmon,
			       const struct dp_mon_packet_info *packet_info)
{
	struct ath12k_base *ab = ar->ab;
	struct dp_rxdma_mon_ring *buf_ring = &ab->dp.rxdma_mon_buf_ring;
	struct sk_buff *msdu;
	int buf_id;
	u32 offset;

	buf_id = u32_get_bits(packet_info->cookie, DP_RXDMA_BUF_COOKIE_BUF_ID);

	spin_lock_bh(&buf_ring->idr_lock);
	msdu = idr_remove(&buf_ring->bufs_idr, buf_id);
	spin_unlock_bh(&buf_ring->idr_lock);

	if (unlikely(!msdu)) {
		ath12k_warn(ab, "mon dest desc with inval buf_id %d\n", buf_id);
		return 0;
	}

	dma_unmap_single(ab->dev, ATH12K_SKB_RXCB(msdu)->paddr,
			 msdu->len + skb_tailroom(msdu),
			 DMA_FROM_DEVICE);

	offset = packet_info->dma_length + ATH12K_MON_RX_DOT11_OFFSET;
	if (ath12k_dp_pkt_set_pktlen(msdu, offset)) {
		dev_kfree_skb_any(msdu);
		goto dest_replenish;
	}

	if (!pmon->mon_mpdu->head)
		pmon->mon_mpdu->head = msdu;
	else
		pmon->mon_mpdu->tail->next = msdu;

	pmon->mon_mpdu->tail = msdu;

dest_replenish:
	ath12k_dp_mon_buf_replenish(ab, buf_ring, 1);

	return 0;
}

static int
ath12k_dp_mon_parse_rx_dest_tlv(struct ath12k *ar,
				struct ath12k_mon_data *pmon,
				enum hal_rx_mon_status hal_status,
				const void *tlv_data)
{
	switch (hal_status) {
	case HAL_RX_MON_STATUS_MPDU_START:
		if (WARN_ON_ONCE(pmon->mon_mpdu))
			break;

		pmon->mon_mpdu = kzalloc(sizeof(*pmon->mon_mpdu), GFP_ATOMIC);
		if (!pmon->mon_mpdu)
			return -ENOMEM;
		break;
	case HAL_RX_MON_STATUS_BUF_ADDR:
		return ath12k_dp_mon_parse_status_buf(ar, pmon, tlv_data);
	case HAL_RX_MON_STATUS_MPDU_END:
		/* If no MSDU then free empty MPDU */
		if (pmon->mon_mpdu->tail) {
			pmon->mon_mpdu->tail->next = NULL;
			list_add_tail(&pmon->mon_mpdu->list, &pmon->dp_rx_mon_mpdu_list);
		} else {
			kfree(pmon->mon_mpdu);
		}
		pmon->mon_mpdu = NULL;
		break;
	case HAL_RX_MON_STATUS_MSDU_END:
		return ath12k_dp_mon_parse_status_msdu_end(pmon, tlv_data);
	default:
		break;
	}

	return 0;
}

static enum hal_rx_mon_status
ath12k_dp_mon_parse_rx_dest(struct ath12k *ar, struct ath12k_mon_data *pmon,
			    struct sk_buff *skb)
{
	struct hal_tlv_64_hdr *tlv;
	struct ath12k_skb_rxcb *rxcb;
	enum hal_rx_mon_status hal_status;
	u16 tlv_tag, tlv_len;
	u8 *ptr = skb->data;

	do {
		tlv = (struct hal_tlv_64_hdr *)ptr;
		tlv_tag = le64_get_bits(tlv->tl, HAL_TLV_64_HDR_TAG);

		/* The actual length of PPDU_END is the combined length of many PHY
		 * TLVs that follow. Skip the TLV header and
		 * rx_rxpcu_classification_overview that follows the header to get to
		 * next TLV.
		 */

		if (tlv_tag == HAL_RX_PPDU_END)
			tlv_len = sizeof(struct hal_rx_rxpcu_classification_overview);
		else
			tlv_len = le64_get_bits(tlv->tl, HAL_TLV_64_HDR_LEN);

		hal_status = ath12k_dp_mon_rx_parse_status_tlv(ar, pmon, tlv);

		if (ar->monitor_started &&
		    ath12k_dp_mon_parse_rx_dest_tlv(ar, pmon, hal_status, tlv->value))
			return HAL_RX_MON_STATUS_PPDU_DONE;

		ptr += sizeof(*tlv) + tlv_len;
		ptr = PTR_ALIGN(ptr, HAL_TLV_64_ALIGN);

		if ((ptr - skb->data) > skb->len)
			break;

	} while ((hal_status == HAL_RX_MON_STATUS_PPDU_NOT_DONE) ||
		 (hal_status == HAL_RX_MON_STATUS_BUF_ADDR) ||
		 (hal_status == HAL_RX_MON_STATUS_MPDU_START) ||
		 (hal_status == HAL_RX_MON_STATUS_MPDU_END) ||
		 (hal_status == HAL_RX_MON_STATUS_MSDU_END));

	rxcb = ATH12K_SKB_RXCB(skb);
	if (rxcb->is_end_of_ppdu)
		hal_status = HAL_RX_MON_STATUS_PPDU_DONE;

	return hal_status;
}

enum hal_rx_mon_status
ath12k_dp_mon_rx_parse_mon_status(struct ath12k *ar,
				  struct ath12k_mon_data *pmon,
				  struct sk_buff *skb,
				  struct napi_struct *napi)
{
	struct hal_rx_mon_ppdu_info *ppdu_info = &pmon->mon_ppdu_info;
	struct dp_mon_mpdu *tmp;
	struct dp_mon_mpdu *mon_mpdu = pmon->mon_mpdu;
	struct sk_buff *head_msdu, *tail_msdu;
	enum hal_rx_mon_status hal_status;

	hal_status = ath12k_dp_mon_parse_rx_dest(ar, pmon, skb);
	if (hal_status != HAL_RX_MON_STATUS_PPDU_DONE)
		return hal_status;

	list_for_each_entry_safe(mon_mpdu, tmp, &pmon->dp_rx_mon_mpdu_list, list) {
		list_del(&mon_mpdu->list);
		head_msdu = mon_mpdu->head;
		tail_msdu = mon_mpdu->tail;

		if (head_msdu && tail_msdu) {
			ath12k_dp_mon_rx_deliver(ar, head_msdu,
						 tail_msdu, ppdu_info, napi);
		}

		kfree(mon_mpdu);
	}

	return hal_status;
}

int ath12k_dp_mon_buf_replenish(struct ath12k_base *ab,
				struct dp_rxdma_mon_ring *buf_ring,
				int req_entries)
{
	struct hal_mon_buf_ring *mon_buf;
	struct sk_buff *skb;
	struct hal_srng *srng;
	dma_addr_t paddr;
	u32 cookie;
	int buf_id;

	srng = &ab->hal.srng_list[buf_ring->refill_buf_ring.ring_id];
	spin_lock_bh(&srng->lock);
	ath12k_hal_srng_access_begin(ab, srng);

	while (req_entries > 0) {
		skb = dev_alloc_skb(DP_RX_BUFFER_SIZE + DP_RX_BUFFER_ALIGN_SIZE);
		if (unlikely(!skb))
			goto fail_alloc_skb;

		if (!IS_ALIGNED((unsigned long)skb->data, DP_RX_BUFFER_ALIGN_SIZE)) {
			skb_pull(skb,
				 PTR_ALIGN(skb->data, DP_RX_BUFFER_ALIGN_SIZE) -
				 skb->data);
		}

		paddr = dma_map_single(ab->dev, skb->data,
				       skb->len + skb_tailroom(skb),
				       DMA_FROM_DEVICE);

		if (unlikely(dma_mapping_error(ab->dev, paddr)))
			goto fail_free_skb;

		spin_lock_bh(&buf_ring->idr_lock);
		buf_id = idr_alloc(&buf_ring->bufs_idr, skb, 0,
				   buf_ring->bufs_max * 3, GFP_ATOMIC);
		spin_unlock_bh(&buf_ring->idr_lock);

		if (unlikely(buf_id < 0))
			goto fail_dma_unmap;

		mon_buf = ath12k_hal_srng_src_get_next_entry(ab, srng);
		if (unlikely(!mon_buf))
			goto fail_idr_remove;

		ATH12K_SKB_RXCB(skb)->paddr = paddr;

		cookie = u32_encode_bits(buf_id, DP_RXDMA_BUF_COOKIE_BUF_ID);

		mon_buf->paddr_lo = cpu_to_le32(lower_32_bits(paddr));
		mon_buf->paddr_hi = cpu_to_le32(upper_32_bits(paddr));
		mon_buf->cookie = cpu_to_le64(cookie);

		req_entries--;
	}

	ath12k_hal_srng_access_end(ab, srng);
	spin_unlock_bh(&srng->lock);
	return 0;

fail_idr_remove:
	spin_lock_bh(&buf_ring->idr_lock);
	idr_remove(&buf_ring->bufs_idr, buf_id);
	spin_unlock_bh(&buf_ring->idr_lock);
fail_dma_unmap:
	dma_unmap_single(ab->dev, paddr, skb->len + skb_tailroom(skb),
			 DMA_FROM_DEVICE);
fail_free_skb:
	dev_kfree_skb_any(skb);
fail_alloc_skb:
	ath12k_hal_srng_access_end(ab, srng);
	spin_unlock_bh(&srng->lock);
	return -ENOMEM;
}

static struct dp_mon_tx_ppdu_info *
ath12k_dp_mon_tx_get_ppdu_info(struct ath12k_mon_data *pmon,
			       unsigned int ppdu_id,
			       enum dp_mon_tx_ppdu_info_type type)
{
	struct dp_mon_tx_ppdu_info *tx_ppdu_info;

	if (type == DP_MON_TX_PROT_PPDU_INFO) {
		tx_ppdu_info = pmon->tx_prot_ppdu_info;

		if (tx_ppdu_info && !tx_ppdu_info->is_used)
			return tx_ppdu_info;
		kfree(tx_ppdu_info);
	} else {
		tx_ppdu_info = pmon->tx_data_ppdu_info;

		if (tx_ppdu_info && !tx_ppdu_info->is_used)
			return tx_ppdu_info;
		kfree(tx_ppdu_info);
	}

	/* allocate new tx_ppdu_info */
	tx_ppdu_info = kzalloc(sizeof(*tx_ppdu_info), GFP_ATOMIC);
	if (!tx_ppdu_info)
		return NULL;

	tx_ppdu_info->is_used = 0;
	tx_ppdu_info->ppdu_id = ppdu_id;

	if (type == DP_MON_TX_PROT_PPDU_INFO)
		pmon->tx_prot_ppdu_info = tx_ppdu_info;
	else
		pmon->tx_data_ppdu_info = tx_ppdu_info;

	return tx_ppdu_info;
}

static struct dp_mon_tx_ppdu_info *
ath12k_dp_mon_hal_tx_ppdu_info(struct ath12k_mon_data *pmon,
			       u16 tlv_tag)
{
	switch (tlv_tag) {
	case HAL_TX_FES_SETUP:
	case HAL_TX_FLUSH:
	case HAL_PCU_PPDU_SETUP_INIT:
	case HAL_TX_PEER_ENTRY:
	case HAL_TX_QUEUE_EXTENSION:
	case HAL_TX_MPDU_START:
	case HAL_TX_MSDU_START:
	case HAL_TX_DATA:
	case HAL_MON_BUF_ADDR:
	case HAL_TX_MPDU_END:
	case HAL_TX_LAST_MPDU_FETCHED:
	case HAL_TX_LAST_MPDU_END:
	case HAL_COEX_TX_REQ:
	case HAL_TX_RAW_OR_NATIVE_FRAME_SETUP:
	case HAL_SCH_CRITICAL_TLV_REFERENCE:
	case HAL_TX_FES_SETUP_COMPLETE:
	case HAL_TQM_MPDU_GLOBAL_START:
	case HAL_SCHEDULER_END:
	case HAL_TX_FES_STATUS_USER_PPDU:
		break;
	case HAL_TX_FES_STATUS_PROT: {
		if (!pmon->tx_prot_ppdu_info->is_used)
			pmon->tx_prot_ppdu_info->is_used = true;

		return pmon->tx_prot_ppdu_info;
	}
	}

	if (!pmon->tx_data_ppdu_info->is_used)
		pmon->tx_data_ppdu_info->is_used = true;

	return pmon->tx_data_ppdu_info;
}

#define MAX_MONITOR_HEADER 512
#define MAX_DUMMY_FRM_BODY 128

struct sk_buff *ath12k_dp_mon_tx_alloc_skb(void)
{
	struct sk_buff *skb;

	skb = dev_alloc_skb(MAX_MONITOR_HEADER + MAX_DUMMY_FRM_BODY);
	if (!skb)
		return NULL;

	skb_reserve(skb, MAX_MONITOR_HEADER);

	if (!IS_ALIGNED((unsigned long)skb->data, 4))
		skb_pull(skb, PTR_ALIGN(skb->data, 4) - skb->data);

	return skb;
}

static int
ath12k_dp_mon_tx_gen_cts2self_frame(struct dp_mon_tx_ppdu_info *tx_ppdu_info)
{
	struct sk_buff *skb;
	struct ieee80211_cts *cts;

	skb = ath12k_dp_mon_tx_alloc_skb();
	if (!skb)
		return -ENOMEM;

	cts = (struct ieee80211_cts *)skb->data;
	memset(cts, 0, MAX_DUMMY_FRM_BODY);
	cts->frame_control =
		cpu_to_le16(IEEE80211_FTYPE_CTL | IEEE80211_STYPE_CTS);
	cts->duration = cpu_to_le16(tx_ppdu_info->rx_status.rx_duration);
	memcpy(cts->ra, tx_ppdu_info->rx_status.addr1, sizeof(cts->ra));

	skb_put(skb, sizeof(*cts));
	tx_ppdu_info->tx_mon_mpdu->head = skb;
	tx_ppdu_info->tx_mon_mpdu->tail = NULL;
	list_add_tail(&tx_ppdu_info->tx_mon_mpdu->list,
		      &tx_ppdu_info->dp_tx_mon_mpdu_list);

	return 0;
}

static int
ath12k_dp_mon_tx_gen_rts_frame(struct dp_mon_tx_ppdu_info *tx_ppdu_info)
{
	struct sk_buff *skb;
	struct ieee80211_rts *rts;

	skb = ath12k_dp_mon_tx_alloc_skb();
	if (!skb)
		return -ENOMEM;

	rts = (struct ieee80211_rts *)skb->data;
	memset(rts, 0, MAX_DUMMY_FRM_BODY);
	rts->frame_control =
		cpu_to_le16(IEEE80211_FTYPE_CTL | IEEE80211_STYPE_RTS);
	rts->duration = cpu_to_le16(tx_ppdu_info->rx_status.rx_duration);
	memcpy(rts->ra, tx_ppdu_info->rx_status.addr1, sizeof(rts->ra));
	memcpy(rts->ta, tx_ppdu_info->rx_status.addr2, sizeof(rts->ta));

	skb_put(skb, sizeof(*rts));
	tx_ppdu_info->tx_mon_mpdu->head = skb;
	tx_ppdu_info->tx_mon_mpdu->tail = NULL;
	list_add_tail(&tx_ppdu_info->tx_mon_mpdu->list,
		      &tx_ppdu_info->dp_tx_mon_mpdu_list);

	return 0;
}

static int
ath12k_dp_mon_tx_gen_3addr_qos_null_frame(struct dp_mon_tx_ppdu_info *tx_ppdu_info)
{
	struct sk_buff *skb;
	struct ieee80211_qos_hdr *qhdr;

	skb = ath12k_dp_mon_tx_alloc_skb();
	if (!skb)
		return -ENOMEM;

	qhdr = (struct ieee80211_qos_hdr *)skb->data;
	memset(qhdr, 0, MAX_DUMMY_FRM_BODY);
	qhdr->frame_control =
		cpu_to_le16(IEEE80211_FTYPE_DATA | IEEE80211_STYPE_QOS_NULLFUNC);
	qhdr->duration_id = cpu_to_le16(tx_ppdu_info->rx_status.rx_duration);
	memcpy(qhdr->addr1, tx_ppdu_info->rx_status.addr1, ETH_ALEN);
	memcpy(qhdr->addr2, tx_ppdu_info->rx_status.addr2, ETH_ALEN);
	memcpy(qhdr->addr3, tx_ppdu_info->rx_status.addr3, ETH_ALEN);

	skb_put(skb, sizeof(*qhdr));
	tx_ppdu_info->tx_mon_mpdu->head = skb;
	tx_ppdu_info->tx_mon_mpdu->tail = NULL;
	list_add_tail(&tx_ppdu_info->tx_mon_mpdu->list,
		      &tx_ppdu_info->dp_tx_mon_mpdu_list);

	return 0;
}

static int
ath12k_dp_mon_tx_gen_4addr_qos_null_frame(struct dp_mon_tx_ppdu_info *tx_ppdu_info)
{
	struct sk_buff *skb;
	struct dp_mon_qosframe_addr4 *qhdr;

	skb = ath12k_dp_mon_tx_alloc_skb();
	if (!skb)
		return -ENOMEM;

	qhdr = (struct dp_mon_qosframe_addr4 *)skb->data;
	memset(qhdr, 0, MAX_DUMMY_FRM_BODY);
	qhdr->frame_control =
		cpu_to_le16(IEEE80211_FTYPE_DATA | IEEE80211_STYPE_QOS_NULLFUNC);
	qhdr->duration = cpu_to_le16(tx_ppdu_info->rx_status.rx_duration);
	memcpy(qhdr->addr1, tx_ppdu_info->rx_status.addr1, ETH_ALEN);
	memcpy(qhdr->addr2, tx_ppdu_info->rx_status.addr2, ETH_ALEN);
	memcpy(qhdr->addr3, tx_ppdu_info->rx_status.addr3, ETH_ALEN);
	memcpy(qhdr->addr4, tx_ppdu_info->rx_status.addr4, ETH_ALEN);

	skb_put(skb, sizeof(*qhdr));
	tx_ppdu_info->tx_mon_mpdu->head = skb;
	tx_ppdu_info->tx_mon_mpdu->tail = NULL;
	list_add_tail(&tx_ppdu_info->tx_mon_mpdu->list,
		      &tx_ppdu_info->dp_tx_mon_mpdu_list);

	return 0;
}

static int
ath12k_dp_mon_tx_gen_ack_frame(struct dp_mon_tx_ppdu_info *tx_ppdu_info)
{
	struct sk_buff *skb;
	struct dp_mon_frame_min_one *fbmhdr;

	skb = ath12k_dp_mon_tx_alloc_skb();
	if (!skb)
		return -ENOMEM;

	fbmhdr = (struct dp_mon_frame_min_one *)skb->data;
	memset(fbmhdr, 0, MAX_DUMMY_FRM_BODY);
	fbmhdr->frame_control =
		cpu_to_le16(IEEE80211_FTYPE_DATA | IEEE80211_STYPE_QOS_CFACK);
	memcpy(fbmhdr->addr1, tx_ppdu_info->rx_status.addr1, ETH_ALEN);

	/* set duration zero for ack frame */
	fbmhdr->duration = 0;

	skb_put(skb, sizeof(*fbmhdr));
	tx_ppdu_info->tx_mon_mpdu->head = skb;
	tx_ppdu_info->tx_mon_mpdu->tail = NULL;
	list_add_tail(&tx_ppdu_info->tx_mon_mpdu->list,
		      &tx_ppdu_info->dp_tx_mon_mpdu_list);

	return 0;
}

static int
ath12k_dp_mon_tx_gen_prot_frame(struct dp_mon_tx_ppdu_info *tx_ppdu_info)
{
	int ret = 0;

	switch (tx_ppdu_info->rx_status.medium_prot_type) {
	case DP_MON_TX_MEDIUM_RTS_LEGACY:
	case DP_MON_TX_MEDIUM_RTS_11AC_STATIC_BW:
	case DP_MON_TX_MEDIUM_RTS_11AC_DYNAMIC_BW:
		ret = ath12k_dp_mon_tx_gen_rts_frame(tx_ppdu_info);
		break;
	case DP_MON_TX_MEDIUM_CTS2SELF:
		ret = ath12k_dp_mon_tx_gen_cts2self_frame(tx_ppdu_info);
		break;
	case DP_MON_TX_MEDIUM_QOS_NULL_NO_ACK_3ADDR:
		ret = ath12k_dp_mon_tx_gen_3addr_qos_null_frame(tx_ppdu_info);
		break;
	case DP_MON_TX_MEDIUM_QOS_NULL_NO_ACK_4ADDR:
		ret = ath12k_dp_mon_tx_gen_4addr_qos_null_frame(tx_ppdu_info);
		break;
	}

	return ret;
}

static enum dp_mon_tx_tlv_status
ath12k_dp_mon_tx_parse_status_tlv(struct ath12k_base *ab,
				  struct ath12k_mon_data *pmon,
				  u16 tlv_tag, const void *tlv_data, u32 userid)
{
	struct dp_mon_tx_ppdu_info *tx_ppdu_info;
	enum dp_mon_tx_tlv_status status = DP_MON_TX_STATUS_PPDU_NOT_DONE;
	u32 info[7];

	tx_ppdu_info = ath12k_dp_mon_hal_tx_ppdu_info(pmon, tlv_tag);

	switch (tlv_tag) {
	case HAL_TX_FES_SETUP: {
		const struct hal_tx_fes_setup *tx_fes_setup = tlv_data;

		info[0] = __le32_to_cpu(tx_fes_setup->info0);
		tx_ppdu_info->ppdu_id = __le32_to_cpu(tx_fes_setup->schedule_id);
		tx_ppdu_info->num_users =
			u32_get_bits(info[0], HAL_TX_FES_SETUP_INFO0_NUM_OF_USERS);
		status = DP_MON_TX_FES_SETUP;
		break;
	}

	case HAL_TX_FES_STATUS_END: {
		const struct hal_tx_fes_status_end *tx_fes_status_end = tlv_data;
		u32 tst_15_0, tst_31_16;

		info[0] = __le32_to_cpu(tx_fes_status_end->info0);
		tst_15_0 =
			u32_get_bits(info[0],
				     HAL_TX_FES_STATUS_END_INFO0_START_TIMESTAMP_15_0);
		tst_31_16 =
			u32_get_bits(info[0],
				     HAL_TX_FES_STATUS_END_INFO0_START_TIMESTAMP_31_16);

		tx_ppdu_info->rx_status.ppdu_ts = (tst_15_0 | (tst_31_16 << 16));
		status = DP_MON_TX_FES_STATUS_END;
		break;
	}

	case HAL_RX_RESPONSE_REQUIRED_INFO: {
		const struct hal_rx_resp_req_info *rx_resp_req_info = tlv_data;
		u32 addr_32;
		u16 addr_16;

		info[0] = __le32_to_cpu(rx_resp_req_info->info0);
		info[1] = __le32_to_cpu(rx_resp_req_info->info1);
		info[2] = __le32_to_cpu(rx_resp_req_info->info2);
		info[3] = __le32_to_cpu(rx_resp_req_info->info3);
		info[4] = __le32_to_cpu(rx_resp_req_info->info4);
		info[5] = __le32_to_cpu(rx_resp_req_info->info5);

		tx_ppdu_info->rx_status.ppdu_id =
			u32_get_bits(info[0], HAL_RX_RESP_REQ_INFO0_PPDU_ID);
		tx_ppdu_info->rx_status.reception_type =
			u32_get_bits(info[0], HAL_RX_RESP_REQ_INFO0_RECEPTION_TYPE);
		tx_ppdu_info->rx_status.rx_duration =
			u32_get_bits(info[1], HAL_RX_RESP_REQ_INFO1_DURATION);
		tx_ppdu_info->rx_status.mcs =
			u32_get_bits(info[1], HAL_RX_RESP_REQ_INFO1_RATE_MCS);
		tx_ppdu_info->rx_status.sgi =
			u32_get_bits(info[1], HAL_RX_RESP_REQ_INFO1_SGI);
		tx_ppdu_info->rx_status.is_stbc =
			u32_get_bits(info[1], HAL_RX_RESP_REQ_INFO1_STBC);
		tx_ppdu_info->rx_status.ldpc =
			u32_get_bits(info[1], HAL_RX_RESP_REQ_INFO1_LDPC);
		tx_ppdu_info->rx_status.is_ampdu =
			u32_get_bits(info[1], HAL_RX_RESP_REQ_INFO1_IS_AMPDU);
		tx_ppdu_info->rx_status.num_users =
			u32_get_bits(info[2], HAL_RX_RESP_REQ_INFO2_NUM_USER);

		addr_32 = u32_get_bits(info[3], HAL_RX_RESP_REQ_INFO3_ADDR1_31_0);
		addr_16 = u32_get_bits(info[3], HAL_RX_RESP_REQ_INFO4_ADDR1_47_32);
		ath12k_dp_get_mac_addr(addr_32, addr_16, tx_ppdu_info->rx_status.addr1);

		addr_16 = u32_get_bits(info[4], HAL_RX_RESP_REQ_INFO4_ADDR1_15_0);
		addr_32 = u32_get_bits(info[5], HAL_RX_RESP_REQ_INFO5_ADDR1_47_16);
		ath12k_dp_get_mac_addr(addr_32, addr_16, tx_ppdu_info->rx_status.addr2);

		if (tx_ppdu_info->rx_status.reception_type == 0)
			ath12k_dp_mon_tx_gen_cts2self_frame(tx_ppdu_info);
		status = DP_MON_RX_RESPONSE_REQUIRED_INFO;
		break;
	}

	case HAL_PCU_PPDU_SETUP_INIT: {
		const struct hal_tx_pcu_ppdu_setup_init *ppdu_setup = tlv_data;
		u32 addr_32;
		u16 addr_16;

		info[0] = __le32_to_cpu(ppdu_setup->info0);
		info[1] = __le32_to_cpu(ppdu_setup->info1);
		info[2] = __le32_to_cpu(ppdu_setup->info2);
		info[3] = __le32_to_cpu(ppdu_setup->info3);
		info[4] = __le32_to_cpu(ppdu_setup->info4);
		info[5] = __le32_to_cpu(ppdu_setup->info5);
		info[6] = __le32_to_cpu(ppdu_setup->info6);

		/* protection frame address 1 */
		addr_32 = u32_get_bits(info[1],
				       HAL_TX_PPDU_SETUP_INFO1_PROT_FRAME_ADDR1_31_0);
		addr_16 = u32_get_bits(info[2],
				       HAL_TX_PPDU_SETUP_INFO2_PROT_FRAME_ADDR1_47_32);
		ath12k_dp_get_mac_addr(addr_32, addr_16, tx_ppdu_info->rx_status.addr1);

		/* protection frame address 2 */
		addr_16 = u32_get_bits(info[2],
				       HAL_TX_PPDU_SETUP_INFO2_PROT_FRAME_ADDR2_15_0);
		addr_32 = u32_get_bits(info[3],
				       HAL_TX_PPDU_SETUP_INFO3_PROT_FRAME_ADDR2_47_16);
		ath12k_dp_get_mac_addr(addr_32, addr_16, tx_ppdu_info->rx_status.addr2);

		/* protection frame address 3 */
		addr_32 = u32_get_bits(info[4],
				       HAL_TX_PPDU_SETUP_INFO4_PROT_FRAME_ADDR3_31_0);
		addr_16 = u32_get_bits(info[5],
				       HAL_TX_PPDU_SETUP_INFO5_PROT_FRAME_ADDR3_47_32);
		ath12k_dp_get_mac_addr(addr_32, addr_16, tx_ppdu_info->rx_status.addr3);

		/* protection frame address 4 */
		addr_16 = u32_get_bits(info[5],
				       HAL_TX_PPDU_SETUP_INFO5_PROT_FRAME_ADDR4_15_0);
		addr_32 = u32_get_bits(info[6],
				       HAL_TX_PPDU_SETUP_INFO6_PROT_FRAME_ADDR4_47_16);
		ath12k_dp_get_mac_addr(addr_32, addr_16, tx_ppdu_info->rx_status.addr4);

		status = u32_get_bits(info[0],
				      HAL_TX_PPDU_SETUP_INFO0_MEDIUM_PROT_TYPE);
		break;
	}

	case HAL_TX_QUEUE_EXTENSION: {
		const struct hal_tx_queue_exten *tx_q_exten = tlv_data;

		info[0] = __le32_to_cpu(tx_q_exten->info0);

		tx_ppdu_info->rx_status.frame_control =
			u32_get_bits(info[0],
				     HAL_TX_Q_EXT_INFO0_FRAME_CTRL);
		tx_ppdu_info->rx_status.fc_valid = true;
		break;
	}

	case HAL_TX_FES_STATUS_START: {
		const struct hal_tx_fes_status_start *tx_fes_start = tlv_data;

		info[0] = __le32_to_cpu(tx_fes_start->info0);

		tx_ppdu_info->rx_status.medium_prot_type =
			u32_get_bits(info[0],
				     HAL_TX_FES_STATUS_START_INFO0_MEDIUM_PROT_TYPE);
		break;
	}

	case HAL_TX_FES_STATUS_PROT: {
		const struct hal_tx_fes_status_prot *tx_fes_status = tlv_data;
		u32 start_timestamp;
		u32 end_timestamp;

		info[0] = __le32_to_cpu(tx_fes_status->info0);
		info[1] = __le32_to_cpu(tx_fes_status->info1);

		start_timestamp =
			u32_get_bits(info[0],
				     HAL_TX_FES_STAT_PROT_INFO0_STRT_FRM_TS_15_0);
		start_timestamp |=
			u32_get_bits(info[0],
				     HAL_TX_FES_STAT_PROT_INFO0_STRT_FRM_TS_31_16) << 15;
		end_timestamp =
			u32_get_bits(info[1],
				     HAL_TX_FES_STAT_PROT_INFO1_END_FRM_TS_15_0);
		end_timestamp |=
			u32_get_bits(info[1],
				     HAL_TX_FES_STAT_PROT_INFO1_END_FRM_TS_31_16) << 15;
		tx_ppdu_info->rx_status.rx_duration = end_timestamp - start_timestamp;

		ath12k_dp_mon_tx_gen_prot_frame(tx_ppdu_info);
		break;
	}

	case HAL_TX_FES_STATUS_START_PPDU:
	case HAL_TX_FES_STATUS_START_PROT: {
		const struct hal_tx_fes_status_start_prot *tx_fes_stat_start = tlv_data;
		u64 ppdu_ts;

		info[0] = __le32_to_cpu(tx_fes_stat_start->info0);

		tx_ppdu_info->rx_status.ppdu_ts =
			u32_get_bits(info[0],
				     HAL_TX_FES_STAT_STRT_INFO0_PROT_TS_LOWER_32);
		ppdu_ts = (u32_get_bits(info[1],
					HAL_TX_FES_STAT_STRT_INFO1_PROT_TS_UPPER_32));
		tx_ppdu_info->rx_status.ppdu_ts |= ppdu_ts << 32;
		break;
	}

	case HAL_TX_FES_STATUS_USER_PPDU: {
		const struct hal_tx_fes_status_user_ppdu *tx_fes_usr_ppdu = tlv_data;

		info[0] = __le32_to_cpu(tx_fes_usr_ppdu->info0);

		tx_ppdu_info->rx_status.rx_duration =
			u32_get_bits(info[0],
				     HAL_TX_FES_STAT_USR_PPDU_INFO0_DURATION);
		break;
	}

	case HAL_MACTX_HE_SIG_A_SU:
		ath12k_dp_mon_parse_he_sig_su(tlv_data, &tx_ppdu_info->rx_status);
		break;

	case HAL_MACTX_HE_SIG_A_MU_DL:
		ath12k_dp_mon_parse_he_sig_mu(tlv_data, &tx_ppdu_info->rx_status);
		break;

	case HAL_MACTX_HE_SIG_B1_MU:
		ath12k_dp_mon_parse_he_sig_b1_mu(tlv_data, &tx_ppdu_info->rx_status);
		break;

	case HAL_MACTX_HE_SIG_B2_MU:
		ath12k_dp_mon_parse_he_sig_b2_mu(tlv_data, &tx_ppdu_info->rx_status);
		break;

	case HAL_MACTX_HE_SIG_B2_OFDMA:
		ath12k_dp_mon_parse_he_sig_b2_ofdma(tlv_data, &tx_ppdu_info->rx_status);
		break;

	case HAL_MACTX_VHT_SIG_A:
		ath12k_dp_mon_parse_vht_sig_a(tlv_data, &tx_ppdu_info->rx_status);
		break;

	case HAL_MACTX_L_SIG_A:
		ath12k_dp_mon_parse_l_sig_a(tlv_data, &tx_ppdu_info->rx_status);
		break;

	case HAL_MACTX_L_SIG_B:
		ath12k_dp_mon_parse_l_sig_b(tlv_data, &tx_ppdu_info->rx_status);
		break;

	case HAL_RX_FRAME_BITMAP_ACK: {
		const struct hal_rx_frame_bitmap_ack *fbm_ack = tlv_data;
		u32 addr_32;
		u16 addr_16;

		info[0] = __le32_to_cpu(fbm_ack->info0);
		info[1] = __le32_to_cpu(fbm_ack->info1);

		addr_32 = u32_get_bits(info[0],
				       HAL_RX_FBM_ACK_INFO0_ADDR1_31_0);
		addr_16 = u32_get_bits(info[1],
				       HAL_RX_FBM_ACK_INFO1_ADDR1_47_32);
		ath12k_dp_get_mac_addr(addr_32, addr_16, tx_ppdu_info->rx_status.addr1);

		ath12k_dp_mon_tx_gen_ack_frame(tx_ppdu_info);
		break;
	}

	case HAL_MACTX_PHY_DESC: {
		const struct hal_tx_phy_desc *tx_phy_desc = tlv_data;

		info[0] = __le32_to_cpu(tx_phy_desc->info0);
		info[1] = __le32_to_cpu(tx_phy_desc->info1);
		info[2] = __le32_to_cpu(tx_phy_desc->info2);
		info[3] = __le32_to_cpu(tx_phy_desc->info3);

		tx_ppdu_info->rx_status.beamformed =
			u32_get_bits(info[0],
				     HAL_TX_PHY_DESC_INFO0_BF_TYPE);
		tx_ppdu_info->rx_status.preamble_type =
			u32_get_bits(info[0],
				     HAL_TX_PHY_DESC_INFO0_PREAMBLE_11B);
		tx_ppdu_info->rx_status.mcs =
			u32_get_bits(info[1],
				     HAL_TX_PHY_DESC_INFO1_MCS);
		tx_ppdu_info->rx_status.ltf_size =
			u32_get_bits(info[3],
				     HAL_TX_PHY_DESC_INFO3_LTF_SIZE);
		tx_ppdu_info->rx_status.nss =
			u32_get_bits(info[2],
				     HAL_TX_PHY_DESC_INFO2_NSS);
		tx_ppdu_info->rx_status.chan_num =
			u32_get_bits(info[3],
				     HAL_TX_PHY_DESC_INFO3_ACTIVE_CHANNEL);
		tx_ppdu_info->rx_status.bw =
			u32_get_bits(info[0],
				     HAL_TX_PHY_DESC_INFO0_BANDWIDTH);
		break;
	}

	case HAL_TX_MPDU_START: {
		struct dp_mon_mpdu *mon_mpdu = tx_ppdu_info->tx_mon_mpdu;

		mon_mpdu = kzalloc(sizeof(*mon_mpdu), GFP_ATOMIC);
		if (!mon_mpdu)
			return DP_MON_TX_STATUS_PPDU_NOT_DONE;
		status = DP_MON_TX_MPDU_START;
		break;
	}

	case HAL_TX_MPDU_END:
		list_add_tail(&tx_ppdu_info->tx_mon_mpdu->list,
			      &tx_ppdu_info->dp_tx_mon_mpdu_list);
		break;
	}

	return status;
}

enum dp_mon_tx_tlv_status
ath12k_dp_mon_tx_status_get_num_user(u16 tlv_tag,
				     struct hal_tlv_hdr *tx_tlv,
				     u8 *num_users)
{
	u32 tlv_status = DP_MON_TX_STATUS_PPDU_NOT_DONE;
	u32 info0;

	switch (tlv_tag) {
	case HAL_TX_FES_SETUP: {
		struct hal_tx_fes_setup *tx_fes_setup =
				(struct hal_tx_fes_setup *)tx_tlv;

		info0 = __le32_to_cpu(tx_fes_setup->info0);

		*num_users = u32_get_bits(info0, HAL_TX_FES_SETUP_INFO0_NUM_OF_USERS);
		tlv_status = DP_MON_TX_FES_SETUP;
		break;
	}

	case HAL_RX_RESPONSE_REQUIRED_INFO: {
		/* TODO: need to update *num_users */
		tlv_status = DP_MON_RX_RESPONSE_REQUIRED_INFO;
		break;
	}
	}

	return tlv_status;
}

static void
ath12k_dp_mon_tx_process_ppdu_info(struct ath12k *ar,
				   struct napi_struct *napi,
				   struct dp_mon_tx_ppdu_info *tx_ppdu_info)
{
	struct dp_mon_mpdu *tmp, *mon_mpdu;
	struct sk_buff *head_msdu, *tail_msdu;

	list_for_each_entry_safe(mon_mpdu, tmp,
				 &tx_ppdu_info->dp_tx_mon_mpdu_list, list) {
		list_del(&mon_mpdu->list);
		head_msdu = mon_mpdu->head;
		tail_msdu = mon_mpdu->tail;

		if (head_msdu)
			ath12k_dp_mon_rx_deliver(ar, head_msdu, tail_msdu,
						 &tx_ppdu_info->rx_status, napi);

		kfree(mon_mpdu);
	}
}

enum hal_rx_mon_status
ath12k_dp_mon_tx_parse_mon_status(struct ath12k *ar,
				  struct ath12k_mon_data *pmon,
				  struct sk_buff *skb,
				  struct napi_struct *napi,
				  u32 ppdu_id)
{
	struct ath12k_base *ab = ar->ab;
	struct dp_mon_tx_ppdu_info *tx_prot_ppdu_info, *tx_data_ppdu_info;
	struct hal_tlv_hdr *tlv;
	u8 *ptr = skb->data;
	u16 tlv_tag;
	u16 tlv_len;
	u32 tlv_userid = 0;
	u8 num_user;
	u32 tlv_status = DP_MON_TX_STATUS_PPDU_NOT_DONE;

	tx_prot_ppdu_info = ath12k_dp_mon_tx_get_ppdu_info(pmon, ppdu_id,
							   DP_MON_TX_PROT_PPDU_INFO);
	if (!tx_prot_ppdu_info)
		return -ENOMEM;

	tlv = (struct hal_tlv_hdr *)ptr;
	tlv_tag = le32_get_bits(tlv->tl, HAL_TLV_HDR_TAG);

	tlv_status = ath12k_dp_mon_tx_status_get_num_user(tlv_tag, tlv, &num_user);
	if (tlv_status == DP_MON_TX_STATUS_PPDU_NOT_DONE || !num_user)
		return -EINVAL;

	tx_data_ppdu_info = ath12k_dp_mon_tx_get_ppdu_info(pmon, ppdu_id,
							   DP_MON_TX_DATA_PPDU_INFO);
	if (!tx_data_ppdu_info)
		return -ENOMEM;

	do {
		tlv = (struct hal_tlv_hdr *)ptr;
		tlv_tag = le32_get_bits(tlv->tl, HAL_TLV_HDR_TAG);
		tlv_len = le32_get_bits(tlv->tl, HAL_TLV_HDR_LEN);
		tlv_userid = le32_get_bits(tlv->tl, HAL_TLV_USR_ID);

		tlv_status = ath12k_dp_mon_tx_parse_status_tlv(ab, pmon,
							       tlv_tag, ptr,
							       tlv_userid);
		ptr += tlv_len;
		ptr = PTR_ALIGN(ptr, HAL_TLV_ALIGN);
		if ((ptr - skb->data) >= DP_TX_MONITOR_BUF_SIZE)
			break;
	} while (tlv_status != DP_MON_TX_FES_STATUS_END);

	ath12k_dp_mon_tx_process_ppdu_info(ar, napi, tx_data_ppdu_info);
	ath12k_dp_mon_tx_process_ppdu_info(ar, napi, tx_prot_ppdu_info);

	return tlv_status;
}

static void
ath12k_dp_mon_rx_update_peer_rate_table_stats(struct ath12k_rx_peer_stats *rx_stats,
					      struct hal_rx_mon_ppdu_info *ppdu_info,
					      struct hal_rx_user_status *user_stats,
					      u32 num_msdu)
{
	struct ath12k_rx_peer_rate_stats *stats;
	u32 mcs_idx = (user_stats) ? user_stats->mcs : ppdu_info->mcs;
	u32 nss_idx = (user_stats) ? user_stats->nss - 1 : ppdu_info->nss - 1;
	u32 bw_idx = ppdu_info->bw;
	u32 gi_idx = ppdu_info->gi;
	u32 len;

	if (mcs_idx > HAL_RX_MAX_MCS_HT || nss_idx >= HAL_RX_MAX_NSS ||
	    bw_idx >= HAL_RX_BW_MAX || gi_idx >= HAL_RX_GI_MAX) {
		return;
	}

	if (ppdu_info->preamble_type == HAL_RX_PREAMBLE_11AX ||
	    ppdu_info->preamble_type == HAL_RX_PREAMBLE_11BE)
		gi_idx = ath12k_he_gi_to_nl80211_he_gi(ppdu_info->gi);

	rx_stats->pkt_stats.rx_rate[bw_idx][gi_idx][nss_idx][mcs_idx] += num_msdu;
	stats = &rx_stats->byte_stats;

	if (user_stats)
		len = user_stats->mpdu_ok_byte_count;
	else
		len = ppdu_info->mpdu_len;

	stats->rx_rate[bw_idx][gi_idx][nss_idx][mcs_idx] += len;
}

static void ath12k_dp_mon_rx_update_peer_su_stats(struct ath12k *ar,
						  struct ath12k_link_sta *arsta,
						  struct hal_rx_mon_ppdu_info *ppdu_info)
{
	struct ath12k_rx_peer_stats *rx_stats = arsta->rx_stats;
	u32 num_msdu;

	if (!rx_stats)
		return;

	arsta->rssi_comb = ppdu_info->rssi_comb;
	ewma_avg_rssi_add(&arsta->avg_rssi, ppdu_info->rssi_comb);

	num_msdu = ppdu_info->tcp_msdu_count + ppdu_info->tcp_ack_msdu_count +
		   ppdu_info->udp_msdu_count + ppdu_info->other_msdu_count;

	rx_stats->num_msdu += num_msdu;
	rx_stats->tcp_msdu_count += ppdu_info->tcp_msdu_count +
				    ppdu_info->tcp_ack_msdu_count;
	rx_stats->udp_msdu_count += ppdu_info->udp_msdu_count;
	rx_stats->other_msdu_count += ppdu_info->other_msdu_count;

	if (ppdu_info->preamble_type == HAL_RX_PREAMBLE_11A ||
	    ppdu_info->preamble_type == HAL_RX_PREAMBLE_11B) {
		ppdu_info->nss = 1;
		ppdu_info->mcs = HAL_RX_MAX_MCS;
		ppdu_info->tid = IEEE80211_NUM_TIDS;
	}

	if (ppdu_info->ldpc < HAL_RX_SU_MU_CODING_MAX)
		rx_stats->coding_count[ppdu_info->ldpc] += num_msdu;

	if (ppdu_info->tid <= IEEE80211_NUM_TIDS)
		rx_stats->tid_count[ppdu_info->tid] += num_msdu;

	if (ppdu_info->preamble_type < HAL_RX_PREAMBLE_MAX)
		rx_stats->pream_cnt[ppdu_info->preamble_type] += num_msdu;

	if (ppdu_info->reception_type < HAL_RX_RECEPTION_TYPE_MAX)
		rx_stats->reception_type[ppdu_info->reception_type] += num_msdu;

	if (ppdu_info->is_stbc)
		rx_stats->stbc_count += num_msdu;

	if (ppdu_info->beamformed)
		rx_stats->beamformed_count += num_msdu;

	if (ppdu_info->num_mpdu_fcs_ok > 1)
		rx_stats->ampdu_msdu_count += num_msdu;
	else
		rx_stats->non_ampdu_msdu_count += num_msdu;

	rx_stats->num_mpdu_fcs_ok += ppdu_info->num_mpdu_fcs_ok;
	rx_stats->num_mpdu_fcs_err += ppdu_info->num_mpdu_fcs_err;
	rx_stats->dcm_count += ppdu_info->dcm;

	rx_stats->rx_duration += ppdu_info->rx_duration;
	arsta->rx_duration = rx_stats->rx_duration;

	if (ppdu_info->nss > 0 && ppdu_info->nss <= HAL_RX_MAX_NSS) {
		rx_stats->pkt_stats.nss_count[ppdu_info->nss - 1] += num_msdu;
		rx_stats->byte_stats.nss_count[ppdu_info->nss - 1] += ppdu_info->mpdu_len;
	}

	if (ppdu_info->preamble_type == HAL_RX_PREAMBLE_11N &&
	    ppdu_info->mcs <= HAL_RX_MAX_MCS_HT) {
		rx_stats->pkt_stats.ht_mcs_count[ppdu_info->mcs] += num_msdu;
		rx_stats->byte_stats.ht_mcs_count[ppdu_info->mcs] += ppdu_info->mpdu_len;
		/* To fit into rate table for HT packets */
		ppdu_info->mcs = ppdu_info->mcs % 8;
	}

	if (ppdu_info->preamble_type == HAL_RX_PREAMBLE_11AC &&
	    ppdu_info->mcs <= HAL_RX_MAX_MCS_VHT) {
		rx_stats->pkt_stats.vht_mcs_count[ppdu_info->mcs] += num_msdu;
		rx_stats->byte_stats.vht_mcs_count[ppdu_info->mcs] += ppdu_info->mpdu_len;
	}

	if (ppdu_info->preamble_type == HAL_RX_PREAMBLE_11AX &&
	    ppdu_info->mcs <= HAL_RX_MAX_MCS_HE) {
		rx_stats->pkt_stats.he_mcs_count[ppdu_info->mcs] += num_msdu;
		rx_stats->byte_stats.he_mcs_count[ppdu_info->mcs] += ppdu_info->mpdu_len;
	}

	if (ppdu_info->preamble_type == HAL_RX_PREAMBLE_11BE &&
	    ppdu_info->mcs <= HAL_RX_MAX_MCS_BE) {
		rx_stats->pkt_stats.be_mcs_count[ppdu_info->mcs] += num_msdu;
		rx_stats->byte_stats.be_mcs_count[ppdu_info->mcs] += ppdu_info->mpdu_len;
	}

	if ((ppdu_info->preamble_type == HAL_RX_PREAMBLE_11A ||
	     ppdu_info->preamble_type == HAL_RX_PREAMBLE_11B) &&
	     ppdu_info->rate < HAL_RX_LEGACY_RATE_INVALID) {
		rx_stats->pkt_stats.legacy_count[ppdu_info->rate] += num_msdu;
		rx_stats->byte_stats.legacy_count[ppdu_info->rate] += ppdu_info->mpdu_len;
	}

	if (ppdu_info->gi < HAL_RX_GI_MAX) {
		rx_stats->pkt_stats.gi_count[ppdu_info->gi] += num_msdu;
		rx_stats->byte_stats.gi_count[ppdu_info->gi] += ppdu_info->mpdu_len;
	}

	if (ppdu_info->bw < HAL_RX_BW_MAX) {
		rx_stats->pkt_stats.bw_count[ppdu_info->bw] += num_msdu;
		rx_stats->byte_stats.bw_count[ppdu_info->bw] += ppdu_info->mpdu_len;
	}

	ath12k_dp_mon_rx_update_peer_rate_table_stats(rx_stats, ppdu_info,
						      NULL, num_msdu);
}

void ath12k_dp_mon_rx_process_ulofdma(struct hal_rx_mon_ppdu_info *ppdu_info)
{
	struct hal_rx_user_status *rx_user_status;
	u32 num_users, i, mu_ul_user_v0_word0, mu_ul_user_v0_word1, ru_size;

	if (!(ppdu_info->reception_type == HAL_RX_RECEPTION_TYPE_MU_MIMO ||
	      ppdu_info->reception_type == HAL_RX_RECEPTION_TYPE_MU_OFDMA ||
	      ppdu_info->reception_type == HAL_RX_RECEPTION_TYPE_MU_OFDMA_MIMO))
		return;

	num_users = ppdu_info->num_users;
	if (num_users > HAL_MAX_UL_MU_USERS)
		num_users = HAL_MAX_UL_MU_USERS;

	for (i = 0; i < num_users; i++) {
		rx_user_status = &ppdu_info->userstats[i];
		mu_ul_user_v0_word0 =
			rx_user_status->ul_ofdma_user_v0_word0;
		mu_ul_user_v0_word1 =
			rx_user_status->ul_ofdma_user_v0_word1;

		if (u32_get_bits(mu_ul_user_v0_word0,
				 HAL_RX_UL_OFDMA_USER_INFO_V0_W0_VALID) &&
		    !u32_get_bits(mu_ul_user_v0_word0,
				  HAL_RX_UL_OFDMA_USER_INFO_V0_W0_VER)) {
			rx_user_status->mcs =
				u32_get_bits(mu_ul_user_v0_word1,
					     HAL_RX_UL_OFDMA_USER_INFO_V0_W1_MCS);
			rx_user_status->nss =
				u32_get_bits(mu_ul_user_v0_word1,
					     HAL_RX_UL_OFDMA_USER_INFO_V0_W1_NSS) + 1;

			rx_user_status->ofdma_info_valid = 1;
			rx_user_status->ul_ofdma_ru_start_index =
				u32_get_bits(mu_ul_user_v0_word1,
					     HAL_RX_UL_OFDMA_USER_INFO_V0_W1_RU_START);

			ru_size = u32_get_bits(mu_ul_user_v0_word1,
					       HAL_RX_UL_OFDMA_USER_INFO_V0_W1_RU_SIZE);
			rx_user_status->ul_ofdma_ru_width = ru_size;
			rx_user_status->ul_ofdma_ru_size = ru_size;
		}
		rx_user_status->ldpc = u32_get_bits(mu_ul_user_v0_word1,
						    HAL_RX_UL_OFDMA_USER_INFO_V0_W1_LDPC);
	}
	ppdu_info->ldpc = 1;
}

static void
ath12k_dp_mon_rx_update_user_stats(struct ath12k *ar,
				   struct hal_rx_mon_ppdu_info *ppdu_info,
				   u32 uid)
{
	struct ath12k_sta *ahsta;
	struct ath12k_link_sta *arsta;
	struct ath12k_rx_peer_stats *rx_stats = NULL;
	struct hal_rx_user_status *user_stats = &ppdu_info->userstats[uid];
	struct ath12k_peer *peer;
	u32 num_msdu;

	if (user_stats->ast_index == 0 || user_stats->ast_index == 0xFFFF)
		return;

	peer = ath12k_peer_find_by_ast(ar->ab, user_stats->ast_index);

	if (!peer) {
		ath12k_warn(ar->ab, "peer ast idx %d can't be found\n",
			    user_stats->ast_index);
		return;
	}

	ahsta = ath12k_sta_to_ahsta(peer->sta);
	arsta = &ahsta->deflink;
	rx_stats = arsta->rx_stats;

	if (!rx_stats)
		return;

	arsta->rssi_comb = ppdu_info->rssi_comb;
	ewma_avg_rssi_add(&arsta->avg_rssi, ppdu_info->rssi_comb);

	num_msdu = user_stats->tcp_msdu_count + user_stats->tcp_ack_msdu_count +
		   user_stats->udp_msdu_count + user_stats->other_msdu_count;

	rx_stats->num_msdu += num_msdu;
	rx_stats->tcp_msdu_count += user_stats->tcp_msdu_count +
				    user_stats->tcp_ack_msdu_count;
	rx_stats->udp_msdu_count += user_stats->udp_msdu_count;
	rx_stats->other_msdu_count += user_stats->other_msdu_count;

	if (ppdu_info->ldpc < HAL_RX_SU_MU_CODING_MAX)
		rx_stats->coding_count[ppdu_info->ldpc] += num_msdu;

	if (user_stats->tid <= IEEE80211_NUM_TIDS)
		rx_stats->tid_count[user_stats->tid] += num_msdu;

	if (user_stats->preamble_type < HAL_RX_PREAMBLE_MAX)
		rx_stats->pream_cnt[user_stats->preamble_type] += num_msdu;

	if (ppdu_info->reception_type < HAL_RX_RECEPTION_TYPE_MAX)
		rx_stats->reception_type[ppdu_info->reception_type] += num_msdu;

	if (ppdu_info->is_stbc)
		rx_stats->stbc_count += num_msdu;

	if (ppdu_info->beamformed)
		rx_stats->beamformed_count += num_msdu;

	if (user_stats->mpdu_cnt_fcs_ok > 1)
		rx_stats->ampdu_msdu_count += num_msdu;
	else
		rx_stats->non_ampdu_msdu_count += num_msdu;

	rx_stats->num_mpdu_fcs_ok += user_stats->mpdu_cnt_fcs_ok;
	rx_stats->num_mpdu_fcs_err += user_stats->mpdu_cnt_fcs_err;
	rx_stats->dcm_count += ppdu_info->dcm;
	if (ppdu_info->reception_type == HAL_RX_RECEPTION_TYPE_MU_OFDMA ||
	    ppdu_info->reception_type == HAL_RX_RECEPTION_TYPE_MU_OFDMA_MIMO)
		rx_stats->ru_alloc_cnt[user_stats->ul_ofdma_ru_size] += num_msdu;

	rx_stats->rx_duration += ppdu_info->rx_duration;
	arsta->rx_duration = rx_stats->rx_duration;

	if (user_stats->nss > 0 && user_stats->nss <= HAL_RX_MAX_NSS) {
		rx_stats->pkt_stats.nss_count[user_stats->nss - 1] += num_msdu;
		rx_stats->byte_stats.nss_count[user_stats->nss - 1] +=
						user_stats->mpdu_ok_byte_count;
	}

	if (user_stats->preamble_type == HAL_RX_PREAMBLE_11AX &&
	    user_stats->mcs <= HAL_RX_MAX_MCS_HE) {
		rx_stats->pkt_stats.he_mcs_count[user_stats->mcs] += num_msdu;
		rx_stats->byte_stats.he_mcs_count[user_stats->mcs] +=
						user_stats->mpdu_ok_byte_count;
	}

	if (ppdu_info->gi < HAL_RX_GI_MAX) {
		rx_stats->pkt_stats.gi_count[ppdu_info->gi] += num_msdu;
		rx_stats->byte_stats.gi_count[ppdu_info->gi] +=
						user_stats->mpdu_ok_byte_count;
	}

	if (ppdu_info->bw < HAL_RX_BW_MAX) {
		rx_stats->pkt_stats.bw_count[ppdu_info->bw] += num_msdu;
		rx_stats->byte_stats.bw_count[ppdu_info->bw] +=
						user_stats->mpdu_ok_byte_count;
	}

	ath12k_dp_mon_rx_update_peer_rate_table_stats(rx_stats, ppdu_info,
						      user_stats, num_msdu);
}

static void
ath12k_dp_mon_rx_update_peer_mu_stats(struct ath12k *ar,
				      struct hal_rx_mon_ppdu_info *ppdu_info)
{
	u32 num_users, i;

	num_users = ppdu_info->num_users;
	if (num_users > HAL_MAX_UL_MU_USERS)
		num_users = HAL_MAX_UL_MU_USERS;

	for (i = 0; i < num_users; i++)
		ath12k_dp_mon_rx_update_user_stats(ar, ppdu_info, i);
}

static void
ath12k_dp_mon_rx_memset_ppdu_info(struct hal_rx_mon_ppdu_info *ppdu_info)
{
	memset(ppdu_info, 0, sizeof(*ppdu_info));
	ppdu_info->peer_id = HAL_INVALID_PEERID;
}

int ath12k_dp_mon_srng_process(struct ath12k *ar, int *budget,
			       struct napi_struct *napi)
{
	struct ath12k_base *ab = ar->ab;
	struct ath12k_pdev_dp *pdev_dp = &ar->dp;
	struct ath12k_mon_data *pmon = (struct ath12k_mon_data *)&pdev_dp->mon_data;
	struct hal_rx_mon_ppdu_info *ppdu_info = &pmon->mon_ppdu_info;
	struct ath12k_dp *dp = &ab->dp;
	struct hal_mon_dest_desc *mon_dst_desc;
	struct sk_buff *skb;
	struct ath12k_skb_rxcb *rxcb;
	struct dp_srng *mon_dst_ring;
	struct hal_srng *srng;
	struct dp_rxdma_mon_ring *buf_ring;
	struct ath12k_sta *ahsta = NULL;
	struct ath12k_link_sta *arsta;
	struct ath12k_peer *peer;
	struct sk_buff_head skb_list;
	u64 cookie;
	int num_buffs_reaped = 0, srng_id, buf_id;
	u32 hal_status, end_offset, info0, end_reason;
	u8 pdev_idx = ath12k_hw_mac_id_to_pdev_id(ab->hw_params, ar->pdev_idx);

	__skb_queue_head_init(&skb_list);
	srng_id = ath12k_hw_mac_id_to_srng_id(ab->hw_params, pdev_idx);
	mon_dst_ring = &pdev_dp->rxdma_mon_dst_ring[srng_id];
	buf_ring = &dp->rxdma_mon_buf_ring;

	srng = &ab->hal.srng_list[mon_dst_ring->ring_id];
	spin_lock_bh(&srng->lock);
	ath12k_hal_srng_access_begin(ab, srng);

	while (likely(*budget)) {
		*budget -= 1;
		mon_dst_desc = ath12k_hal_srng_dst_peek(ab, srng);
		if (unlikely(!mon_dst_desc))
			break;

		/* In case of empty descriptor, the cookie in the ring descriptor
		 * is invalid. Therefore, this entry is skipped, and ring processing
		 * continues.
		 */
		info0 = le32_to_cpu(mon_dst_desc->info0);
		if (u32_get_bits(info0, HAL_MON_DEST_INFO0_EMPTY_DESC))
			goto move_next;

		cookie = le32_to_cpu(mon_dst_desc->cookie);
		buf_id = u32_get_bits(cookie, DP_RXDMA_BUF_COOKIE_BUF_ID);

		spin_lock_bh(&buf_ring->idr_lock);
		skb = idr_remove(&buf_ring->bufs_idr, buf_id);
		spin_unlock_bh(&buf_ring->idr_lock);

		if (unlikely(!skb)) {
			ath12k_warn(ab, "monitor destination with invalid buf_id %d\n",
				    buf_id);
			goto move_next;
		}

		rxcb = ATH12K_SKB_RXCB(skb);
		dma_unmap_single(ab->dev, rxcb->paddr,
				 skb->len + skb_tailroom(skb),
				 DMA_FROM_DEVICE);

		end_reason = u32_get_bits(info0, HAL_MON_DEST_INFO0_END_REASON);

		/* HAL_MON_FLUSH_DETECTED implies that an rx flush received at the end of
		 * rx PPDU and HAL_MON_PPDU_TRUNCATED implies that the PPDU got
		 * truncated due to a system level error. In both the cases, buffer data
		 * can be discarded
		 */
		if ((end_reason == HAL_MON_FLUSH_DETECTED) ||
		    (end_reason == HAL_MON_PPDU_TRUNCATED)) {
			ath12k_dbg(ab, ATH12K_DBG_DATA,
				   "Monitor dest descriptor end reason %d", end_reason);
			dev_kfree_skb_any(skb);
			goto move_next;
		}

		/* Calculate the budget when the ring descriptor with the
		 * HAL_MON_END_OF_PPDU to ensure that one PPDU worth of data is always
		 * reaped. This helps to efficiently utilize the NAPI budget.
		 */
		if (end_reason == HAL_MON_END_OF_PPDU) {
			*budget -= 1;
			rxcb->is_end_of_ppdu = true;
		}

		end_offset = u32_get_bits(info0, HAL_MON_DEST_INFO0_END_OFFSET);
		if (likely(end_offset <= DP_RX_BUFFER_SIZE)) {
			skb_put(skb, end_offset);
		} else {
			ath12k_warn(ab,
				    "invalid offset on mon stats destination %u\n",
				    end_offset);
			skb_put(skb, DP_RX_BUFFER_SIZE);
		}

		__skb_queue_tail(&skb_list, skb);

move_next:
		ath12k_dp_mon_buf_replenish(ab, buf_ring, 1);
		ath12k_hal_srng_dst_get_next_entry(ab, srng);
		num_buffs_reaped++;
	}

	ath12k_hal_srng_access_end(ab, srng);
	spin_unlock_bh(&srng->lock);

	if (!num_buffs_reaped)
		return 0;

	/* In some cases, one PPDU worth of data can be spread across multiple NAPI
	 * schedules, To avoid losing existing parsed ppdu_info information, skip
	 * the memset of the ppdu_info structure and continue processing it.
	 */
	if (!ppdu_info->ppdu_continuation)
		ath12k_dp_mon_rx_memset_ppdu_info(ppdu_info);

	while ((skb = __skb_dequeue(&skb_list))) {
		hal_status = ath12k_dp_mon_rx_parse_mon_status(ar, pmon, skb, napi);
		if (hal_status != HAL_RX_MON_STATUS_PPDU_DONE) {
			ppdu_info->ppdu_continuation = true;
			dev_kfree_skb_any(skb);
			continue;
		}

		if (ppdu_info->peer_id == HAL_INVALID_PEERID)
			goto free_skb;

		rcu_read_lock();
		spin_lock_bh(&ab->base_lock);
		peer = ath12k_peer_find_by_id(ab, ppdu_info->peer_id);
		if (!peer || !peer->sta) {
			ath12k_dbg(ab, ATH12K_DBG_DATA,
				   "failed to find the peer with monitor peer_id %d\n",
				   ppdu_info->peer_id);
			goto next_skb;
		}

		if (ppdu_info->reception_type == HAL_RX_RECEPTION_TYPE_SU) {
			ahsta = ath12k_sta_to_ahsta(peer->sta);
			arsta = &ahsta->deflink;
			ath12k_dp_mon_rx_update_peer_su_stats(ar, arsta,
							      ppdu_info);
		} else if ((ppdu_info->fc_valid) &&
			   (ppdu_info->ast_index != HAL_AST_IDX_INVALID)) {
			ath12k_dp_mon_rx_process_ulofdma(ppdu_info);
			ath12k_dp_mon_rx_update_peer_mu_stats(ar, ppdu_info);
		}

next_skb:
		spin_unlock_bh(&ab->base_lock);
		rcu_read_unlock();
free_skb:
		dev_kfree_skb_any(skb);
		ath12k_dp_mon_rx_memset_ppdu_info(ppdu_info);
	}

	return num_buffs_reaped;
}

int ath12k_dp_mon_process_ring(struct ath12k_base *ab, int mac_id,
			       struct napi_struct *napi, int budget,
			       enum dp_monitor_mode monitor_mode)
{
	struct ath12k *ar = ath12k_ab_to_ar(ab, mac_id);
	int num_buffs_reaped = 0;

	if (ab->hw_params->rxdma1_enable) {
		if (monitor_mode == ATH12K_DP_RX_MONITOR_MODE)
			num_buffs_reaped = ath12k_dp_mon_srng_process(ar, &budget, napi);
	}

	return num_buffs_reaped;
}
