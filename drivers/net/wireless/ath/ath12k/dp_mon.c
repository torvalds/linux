// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2019-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "dp_mon.h"
#include "debug.h"
#include "dp_rx.h"
#include "dp_tx.h"
#include "peer.h"

static void ath12k_dp_mon_rx_handle_ofdma_info(void *rx_tlv,
					       struct hal_rx_user_status *rx_user_status)
{
	struct hal_rx_ppdu_end_user_stats *ppdu_end_user =
				(struct hal_rx_ppdu_end_user_stats *)rx_tlv;

	rx_user_status->ul_ofdma_user_v0_word0 =
		__le32_to_cpu(ppdu_end_user->usr_resp_ref);
	rx_user_status->ul_ofdma_user_v0_word1 =
		__le32_to_cpu(ppdu_end_user->usr_resp_ref_ext);
}

static void
ath12k_dp_mon_rx_populate_byte_count(void *rx_tlv, void *ppduinfo,
				     struct hal_rx_user_status *rx_user_status)
{
	struct hal_rx_ppdu_end_user_stats *ppdu_end_user =
		(struct hal_rx_ppdu_end_user_stats *)rx_tlv;
	u32 mpdu_ok_byte_count = __le32_to_cpu(ppdu_end_user->mpdu_ok_cnt);
	u32 mpdu_err_byte_count = __le32_to_cpu(ppdu_end_user->mpdu_err_cnt);

	rx_user_status->mpdu_ok_byte_count =
		u32_get_bits(mpdu_ok_byte_count,
			     HAL_RX_PPDU_END_USER_STATS_MPDU_DELIM_OK_BYTE_COUNT);
	rx_user_status->mpdu_err_byte_count =
		u32_get_bits(mpdu_err_byte_count,
			     HAL_RX_PPDU_END_USER_STATS_MPDU_DELIM_ERR_BYTE_COUNT);
}

static void
ath12k_dp_mon_rx_populate_mu_user_info(void *rx_tlv,
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

static void ath12k_dp_mon_parse_vht_sig_a(u8 *tlv_data,
					  struct hal_rx_mon_ppdu_info *ppdu_info)
{
	struct hal_rx_vht_sig_a_info *vht_sig =
			(struct hal_rx_vht_sig_a_info *)tlv_data;
	u32 nsts, group_id, info0, info1;
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
	group_id = u32_get_bits(info0, HAL_RX_VHT_SIG_A_INFO_INFO0_GROUP_ID);
	if (group_id == 0 || group_id == 63)
		ppdu_info->reception_type = HAL_RX_RECEPTION_TYPE_SU;
	else
		ppdu_info->reception_type = HAL_RX_RECEPTION_TYPE_MU_MIMO;
	ppdu_info->vht_flag_values5 = group_id;
	ppdu_info->vht_flag_values3[0] = (((ppdu_info->mcs) << 4) |
					    ppdu_info->nss);
	ppdu_info->vht_flag_values2 = ppdu_info->bw;
	ppdu_info->vht_flag_values4 =
		u32_get_bits(info1, HAL_RX_VHT_SIG_A_INFO_INFO1_SU_MU_CODING);
}

static void ath12k_dp_mon_parse_ht_sig(u8 *tlv_data,
				       struct hal_rx_mon_ppdu_info *ppdu_info)
{
	struct hal_rx_ht_sig_info *ht_sig =
			(struct hal_rx_ht_sig_info *)tlv_data;
	u32 info0 = __le32_to_cpu(ht_sig->info0);
	u32 info1 = __le32_to_cpu(ht_sig->info1);

	ppdu_info->mcs = u32_get_bits(info0, HAL_RX_HT_SIG_INFO_INFO0_MCS);
	ppdu_info->bw = u32_get_bits(info0, HAL_RX_HT_SIG_INFO_INFO0_BW);
	ppdu_info->is_stbc = u32_get_bits(info1, HAL_RX_HT_SIG_INFO_INFO1_STBC);
	ppdu_info->ldpc = u32_get_bits(info1, HAL_RX_HT_SIG_INFO_INFO1_FEC_CODING);
	ppdu_info->gi = u32_get_bits(info1, HAL_RX_HT_SIG_INFO_INFO1_GI);
	ppdu_info->nss = (ppdu_info->mcs >> 3);
	ppdu_info->reception_type = HAL_RX_RECEPTION_TYPE_SU;
}

static void ath12k_dp_mon_parse_l_sig_b(u8 *tlv_data,
					struct hal_rx_mon_ppdu_info *ppdu_info)
{
	struct hal_rx_lsig_b_info *lsigb =
			(struct hal_rx_lsig_b_info *)tlv_data;
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
	ppdu_info->reception_type = HAL_RX_RECEPTION_TYPE_SU;
}

static void ath12k_dp_mon_parse_l_sig_a(u8 *tlv_data,
					struct hal_rx_mon_ppdu_info *ppdu_info)
{
	struct hal_rx_lsig_a_info *lsiga =
			(struct hal_rx_lsig_a_info *)tlv_data;
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
	ppdu_info->reception_type = HAL_RX_RECEPTION_TYPE_SU;
}

static void ath12k_dp_mon_parse_he_sig_b2_ofdma(u8 *tlv_data,
						struct hal_rx_mon_ppdu_info *ppdu_info)
{
	struct hal_rx_he_sig_b2_ofdma_info *he_sig_b2_ofdma =
			(struct hal_rx_he_sig_b2_ofdma_info *)tlv_data;
	u32 info0, value;

	info0 = __le32_to_cpu(he_sig_b2_ofdma->info0);

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
	ppdu_info->reception_type = HAL_RX_RECEPTION_TYPE_MU_OFDMA;
}

static void ath12k_dp_mon_parse_he_sig_b2_mu(u8 *tlv_data,
					     struct hal_rx_mon_ppdu_info *ppdu_info)
{
	struct hal_rx_he_sig_b2_mu_info *he_sig_b2_mu =
			(struct hal_rx_he_sig_b2_mu_info *)tlv_data;
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

static void ath12k_dp_mon_parse_he_sig_b1_mu(u8 *tlv_data,
					     struct hal_rx_mon_ppdu_info *ppdu_info)
{
	struct hal_rx_he_sig_b1_mu_info *he_sig_b1_mu =
			(struct hal_rx_he_sig_b1_mu_info *)tlv_data;
	u32 info0 = __le32_to_cpu(he_sig_b1_mu->info0);
	u16 ru_tones;

	ru_tones = u32_get_bits(info0,
				HAL_RX_HE_SIG_B1_MU_INFO_INFO0_RU_ALLOCATION);
	ppdu_info->ru_alloc = ath12k_he_ru_tones_to_nl80211_he_ru_alloc(ru_tones);
	ppdu_info->he_RU[0] = ru_tones;
	ppdu_info->reception_type = HAL_RX_RECEPTION_TYPE_MU_MIMO;
}

static void ath12k_dp_mon_parse_he_sig_mu(u8 *tlv_data,
					  struct hal_rx_mon_ppdu_info *ppdu_info)
{
	struct hal_rx_he_sig_a_mu_dl_info *he_sig_a_mu_dl =
			(struct hal_rx_he_sig_a_mu_dl_info *)tlv_data;
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
	ppdu_info->reception_type = HAL_RX_RECEPTION_TYPE_MU_MIMO;
}

static void ath12k_dp_mon_parse_he_sig_su(u8 *tlv_data,
					  struct hal_rx_mon_ppdu_info *ppdu_info)
{
	struct hal_rx_he_sig_a_su_info *he_sig_a =
			(struct hal_rx_he_sig_a_su_info *)tlv_data;
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
	ppdu_info->reception_type = HAL_RX_RECEPTION_TYPE_SU;
}

static enum hal_rx_mon_status
ath12k_dp_mon_rx_parse_status_tlv(struct ath12k_base *ab,
				  struct ath12k_mon_data *pmon,
				  u32 tlv_tag, u8 *tlv_data, u32 userid)
{
	struct hal_rx_mon_ppdu_info *ppdu_info = &pmon->mon_ppdu_info;
	u32 info[7];

	switch (tlv_tag) {
	case HAL_RX_PPDU_START: {
		struct hal_rx_ppdu_start *ppdu_start =
			(struct hal_rx_ppdu_start *)tlv_data;

		info[0] = __le32_to_cpu(ppdu_start->info0);

		ppdu_info->ppdu_id =
			u32_get_bits(info[0], HAL_RX_PPDU_START_INFO0_PPDU_ID);
		ppdu_info->chan_num = __le32_to_cpu(ppdu_start->chan_num);
		ppdu_info->ppdu_ts = __le32_to_cpu(ppdu_start->ppdu_start_ts);

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
		struct hal_rx_ppdu_end_user_stats *eu_stats =
			(struct hal_rx_ppdu_end_user_stats *)tlv_data;

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
		ppdu_info->tid =
			ffs(u32_get_bits(info[6],
					 HAL_RX_PPDU_END_USER_STATS_INFO6_TID_BITMAP)
					 - 1);
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
		default:
			break;
		}

		if (userid < HAL_MAX_UL_MU_USERS) {
			struct hal_rx_user_status *rxuser_stats =
				&ppdu_info->userstats[userid];
			ppdu_info->num_users += 1;

			ath12k_dp_mon_rx_handle_ofdma_info(tlv_data, rxuser_stats);
			ath12k_dp_mon_rx_populate_mu_user_info(tlv_data, ppdu_info,
							       rxuser_stats);
		}
		ppdu_info->mpdu_fcs_ok_bitmap[0] = __le32_to_cpu(eu_stats->rsvd1[0]);
		ppdu_info->mpdu_fcs_ok_bitmap[1] = __le32_to_cpu(eu_stats->rsvd1[1]);
		break;
	}
	case HAL_RX_PPDU_END_USER_STATS_EXT: {
		struct hal_rx_ppdu_end_user_stats_ext *eu_stats =
			(struct hal_rx_ppdu_end_user_stats_ext *)tlv_data;
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
		struct hal_rx_phyrx_rssi_legacy_info *rssi =
			(struct hal_rx_phyrx_rssi_legacy_info *)tlv_data;
		u32 reception_type = 0;
		u32 rssi_legacy_info = __le32_to_cpu(rssi->rsvd[0]);

		info[0] = __le32_to_cpu(rssi->info0);

		/* TODO: Please note that the combined rssi will not be accurate
		 * in MU case. Rssi in MU needs to be retrieved from
		 * PHYRX_OTHER_RECEIVE_INFO TLV.
		 */
		ppdu_info->rssi_comb =
			u32_get_bits(info[0],
				     HAL_RX_PHYRX_RSSI_LEGACY_INFO_INFO0_RSSI_COMB);
		reception_type =
			u32_get_bits(rssi_legacy_info,
				     HAL_RX_PHYRX_RSSI_LEGACY_INFO_RSVD1_RECEPTION);

		switch (reception_type) {
		case HAL_RECEPTION_TYPE_ULOFMDA:
			ppdu_info->reception_type = HAL_RX_RECEPTION_TYPE_MU_OFDMA;
			break;
		case HAL_RECEPTION_TYPE_ULMIMO:
			ppdu_info->reception_type = HAL_RX_RECEPTION_TYPE_MU_MIMO;
			break;
		default:
			ppdu_info->reception_type = HAL_RX_RECEPTION_TYPE_SU;
			break;
		}
		break;
	}
	case HAL_RXPCU_PPDU_END_INFO: {
		struct hal_rx_ppdu_end_duration *ppdu_rx_duration =
			(struct hal_rx_ppdu_end_duration *)tlv_data;

		info[0] = __le32_to_cpu(ppdu_rx_duration->info0);
		ppdu_info->rx_duration =
			u32_get_bits(info[0], HAL_RX_PPDU_END_DURATION);
		ppdu_info->tsft = __le32_to_cpu(ppdu_rx_duration->rsvd0[1]);
		ppdu_info->tsft = (ppdu_info->tsft << 32) |
				   __le32_to_cpu(ppdu_rx_duration->rsvd0[0]);
		break;
	}
	case HAL_RX_MPDU_START: {
		struct hal_rx_mpdu_start *mpdu_start =
			(struct hal_rx_mpdu_start *)tlv_data;
		struct dp_mon_mpdu *mon_mpdu = pmon->mon_mpdu;
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
			ppdu_info->ampdu_id[userid] =
				u32_get_bits(info[0], HAL_RX_MPDU_START_INFO1_PEERID);
		}

		mon_mpdu = kzalloc(sizeof(*mon_mpdu), GFP_ATOMIC);
		if (!mon_mpdu)
			return HAL_RX_MON_STATUS_PPDU_NOT_DONE;

		break;
	}
	case HAL_RX_MSDU_START:
		/* TODO: add msdu start parsing logic */
		break;
	case HAL_MON_BUF_ADDR: {
		struct dp_rxdma_ring *buf_ring = &ab->dp.rxdma_mon_buf_ring;
		struct dp_mon_packet_info *packet_info =
			(struct dp_mon_packet_info *)tlv_data;
		int buf_id = u32_get_bits(packet_info->cookie,
					  DP_RXDMA_BUF_COOKIE_BUF_ID);
		struct sk_buff *msdu;
		struct dp_mon_mpdu *mon_mpdu = pmon->mon_mpdu;
		struct ath12k_skb_rxcb *rxcb;

		spin_lock_bh(&buf_ring->idr_lock);
		msdu = idr_remove(&buf_ring->bufs_idr, buf_id);
		spin_unlock_bh(&buf_ring->idr_lock);

		if (unlikely(!msdu)) {
			ath12k_warn(ab, "montior destination with invalid buf_id %d\n",
				    buf_id);
			return HAL_RX_MON_STATUS_PPDU_NOT_DONE;
		}

		rxcb = ATH12K_SKB_RXCB(msdu);
		dma_unmap_single(ab->dev, rxcb->paddr,
				 msdu->len + skb_tailroom(msdu),
				 DMA_FROM_DEVICE);

		if (mon_mpdu->tail)
			mon_mpdu->tail->next = msdu;
		else
			mon_mpdu->tail = msdu;

		ath12k_dp_mon_buf_replenish(ab, buf_ring, 1);

		break;
	}
	case HAL_RX_MSDU_END: {
		struct rx_msdu_end_qcn9274 *msdu_end =
			(struct rx_msdu_end_qcn9274 *)tlv_data;
		bool is_first_msdu_in_mpdu;
		u16 msdu_end_info;

		msdu_end_info = __le16_to_cpu(msdu_end->info5);
		is_first_msdu_in_mpdu = u32_get_bits(msdu_end_info,
						     RX_MSDU_END_INFO5_FIRST_MSDU);
		if (is_first_msdu_in_mpdu) {
			pmon->mon_mpdu->head = pmon->mon_mpdu->tail;
			pmon->mon_mpdu->tail = NULL;
		}
		break;
	}
	case HAL_RX_MPDU_END:
		list_add_tail(&pmon->mon_mpdu->list, &pmon->dp_rx_mon_mpdu_list);
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

static void ath12k_dp_mon_rx_msdus_set_payload(struct ath12k *ar, struct sk_buff *msdu)
{
	u32 rx_pkt_offset, l2_hdr_offset;

	rx_pkt_offset = ar->ab->hw_params->hal_desc_sz;
	l2_hdr_offset = ath12k_dp_rx_h_l3pad(ar->ab,
					     (struct hal_rx_desc *)msdu->data);
	skb_pull(msdu, rx_pkt_offset + l2_hdr_offset);
}

static struct sk_buff *
ath12k_dp_mon_rx_merg_msdus(struct ath12k *ar,
			    u32 mac_id, struct sk_buff *head_msdu,
			    struct ieee80211_rx_status *rxs, bool *fcs_err)
{
	struct ath12k_base *ab = ar->ab;
	struct sk_buff *msdu, *mpdu_buf, *prev_buf;
	struct hal_rx_desc *rx_desc;
	u8 *hdr_desc, *dest, decap_format;
	struct ieee80211_hdr_3addr *wh;
	u32 err_bitmap;

	mpdu_buf = NULL;

	if (!head_msdu)
		goto err_merge_fail;

	rx_desc = (struct hal_rx_desc *)head_msdu->data;
	err_bitmap = ath12k_dp_rx_h_mpdu_err(ab, rx_desc);

	if (err_bitmap & HAL_RX_MPDU_ERR_FCS)
		*fcs_err = true;

	decap_format = ath12k_dp_rx_h_decap_type(ab, rx_desc);

	ath12k_dp_rx_h_ppdu(ar, rx_desc, rxs);

	if (decap_format == DP_RX_DECAP_TYPE_RAW) {
		ath12k_dp_mon_rx_msdus_set_payload(ar, head_msdu);

		prev_buf = head_msdu;
		msdu = head_msdu->next;

		while (msdu) {
			ath12k_dp_mon_rx_msdus_set_payload(ar, msdu);

			prev_buf = msdu;
			msdu = msdu->next;
		}

		prev_buf->next = NULL;

		skb_trim(prev_buf, prev_buf->len - HAL_RX_FCS_LEN);
	} else if (decap_format == DP_RX_DECAP_TYPE_NATIVE_WIFI) {
		u8 qos_pkt = 0;

		rx_desc = (struct hal_rx_desc *)head_msdu->data;
		hdr_desc = ab->hw_params->hal_ops->rx_desc_get_msdu_payload(rx_desc);

		/* Base size */
		wh = (struct ieee80211_hdr_3addr *)hdr_desc;

		if (ieee80211_is_data_qos(wh->frame_control))
			qos_pkt = 1;

		msdu = head_msdu;

		while (msdu) {
			ath12k_dp_mon_rx_msdus_set_payload(ar, msdu);
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
			   "mpdu_buf %pK mpdu_buf->len %u",
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
			   "err_merge_fail mpdu_buf %pK", mpdu_buf);
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
	u16 ampdu_id = ppduinfo->ampdu_id[ppduinfo->userid];

	rxs->flag |= RX_FLAG_MACTIME_START;
	rxs->signal = ppduinfo->rssi_comb + ATH12K_DEFAULT_NOISE_FLOOR;
	rxs->nss = ppduinfo->nss + 1;

	if (ampdu_id) {
		rxs->flag |= RX_FLAG_AMPDU_DETAILS;
		rxs->ampdu_reference = ampdu_id;
	}

	if (ppduinfo->he_mu_flags) {
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
	if (peer && peer->sta)
		pubsta = peer->sta;
	spin_unlock_bh(&ar->ab->base_lock);

	ath12k_dbg(ar->ab, ATH12K_DBG_DATA,
		   "rx skb %pK len %u peer %pM %u %s %s%s%s%s%s%s%s %srate_idx %u vht_nss %u freq %u band %u flag 0x%x fcs-err %i mic-err %i amsdu-more %i\n",
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
	 * Also, fast_rx expectes the STA to be authorized, hence
	 * eapol packets are sent in slow path.
	 */
	if (decap == DP_RX_DECAP_TYPE_ETHERNET2_DIX && !is_eapol_tkip &&
	    !(is_mcbc && rx_status->flag & RX_FLAG_DECRYPTED))
		rx_status->flag |= RX_FLAG_8023;

	ieee80211_rx_napi(ar->hw, pubsta, msdu, napi);
}

static int ath12k_dp_mon_rx_deliver(struct ath12k *ar, u32 mac_id,
				    struct sk_buff *head_msdu,
				    struct hal_rx_mon_ppdu_info *ppduinfo,
				    struct napi_struct *napi)
{
	struct ath12k_pdev_dp *dp = &ar->dp;
	struct sk_buff *mon_skb, *skb_next, *header;
	struct ieee80211_rx_status *rxs = &dp->rx_status;
	bool fcs_err = false;

	mon_skb = ath12k_dp_mon_rx_merg_msdus(ar, mac_id, head_msdu,
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

static enum hal_rx_mon_status
ath12k_dp_mon_parse_rx_dest(struct ath12k_base *ab, struct ath12k_mon_data *pmon,
			    struct sk_buff *skb)
{
	struct hal_rx_mon_ppdu_info *ppdu_info = &pmon->mon_ppdu_info;
	struct hal_tlv_hdr *tlv;
	enum hal_rx_mon_status hal_status;
	u32 tlv_userid = 0;
	u16 tlv_tag, tlv_len;
	u8 *ptr = skb->data;

	memset(ppdu_info, 0, sizeof(struct hal_rx_mon_ppdu_info));

	do {
		tlv = (struct hal_tlv_hdr *)ptr;
		tlv_tag = le32_get_bits(tlv->tl, HAL_TLV_HDR_TAG);
		tlv_len = le32_get_bits(tlv->tl, HAL_TLV_HDR_LEN);
		tlv_userid = le32_get_bits(tlv->tl, HAL_TLV_USR_ID);
		ptr += sizeof(*tlv);

		/* The actual length of PPDU_END is the combined length of many PHY
		 * TLVs that follow. Skip the TLV header and
		 * rx_rxpcu_classification_overview that follows the header to get to
		 * next TLV.
		 */

		if (tlv_tag == HAL_RX_PPDU_END)
			tlv_len = sizeof(struct hal_rx_rxpcu_classification_overview);

		hal_status = ath12k_dp_mon_rx_parse_status_tlv(ab, pmon,
							       tlv_tag, ptr, tlv_userid);
		ptr += tlv_len;
		ptr = PTR_ALIGN(ptr, HAL_TLV_ALIGN);

		if ((ptr - skb->data) >= DP_RX_BUFFER_SIZE)
			break;

	} while (hal_status == HAL_RX_MON_STATUS_PPDU_NOT_DONE);

	return hal_status;
}

enum hal_rx_mon_status
ath12k_dp_mon_rx_parse_mon_status(struct ath12k *ar,
				  struct ath12k_mon_data *pmon,
				  int mac_id,
				  struct sk_buff *skb,
				  struct napi_struct *napi)
{
	struct ath12k_base *ab = ar->ab;
	struct hal_rx_mon_ppdu_info *ppdu_info = &pmon->mon_ppdu_info;
	struct dp_mon_mpdu *tmp;
	struct dp_mon_mpdu *mon_mpdu = pmon->mon_mpdu;
	struct sk_buff *head_msdu, *tail_msdu;
	enum hal_rx_mon_status hal_status = HAL_RX_MON_STATUS_BUF_DONE;

	ath12k_dp_mon_parse_rx_dest(ab, pmon, skb);

	list_for_each_entry_safe(mon_mpdu, tmp, &pmon->dp_rx_mon_mpdu_list, list) {
		list_del(&mon_mpdu->list);
		head_msdu = mon_mpdu->head;
		tail_msdu = mon_mpdu->tail;

		if (head_msdu && tail_msdu) {
			ath12k_dp_mon_rx_deliver(ar, mac_id, head_msdu,
						 ppdu_info, napi);
		}

		kfree(mon_mpdu);
	}
	return hal_status;
}

int ath12k_dp_mon_buf_replenish(struct ath12k_base *ab,
				struct dp_rxdma_ring *buf_ring,
				int req_entries)
{
	struct hal_mon_buf_ring *mon_buf;
	struct sk_buff *skb;
	struct hal_srng *srng;
	dma_addr_t paddr;
	u32 cookie, buf_id;

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
				  u16 tlv_tag, u8 *tlv_data, u32 userid)
{
	struct dp_mon_tx_ppdu_info *tx_ppdu_info;
	enum dp_mon_tx_tlv_status status = DP_MON_TX_STATUS_PPDU_NOT_DONE;
	u32 info[7];

	tx_ppdu_info = ath12k_dp_mon_hal_tx_ppdu_info(pmon, tlv_tag);

	switch (tlv_tag) {
	case HAL_TX_FES_SETUP: {
		struct hal_tx_fes_setup *tx_fes_setup =
					(struct hal_tx_fes_setup *)tlv_data;

		info[0] = __le32_to_cpu(tx_fes_setup->info0);
		tx_ppdu_info->ppdu_id = __le32_to_cpu(tx_fes_setup->schedule_id);
		tx_ppdu_info->num_users =
			u32_get_bits(info[0], HAL_TX_FES_SETUP_INFO0_NUM_OF_USERS);
		status = DP_MON_TX_FES_SETUP;
		break;
	}

	case HAL_TX_FES_STATUS_END: {
		struct hal_tx_fes_status_end *tx_fes_status_end =
			(struct hal_tx_fes_status_end *)tlv_data;
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
		struct hal_rx_resp_req_info *rx_resp_req_info =
			(struct hal_rx_resp_req_info *)tlv_data;
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
		struct hal_tx_pcu_ppdu_setup_init *ppdu_setup =
			(struct hal_tx_pcu_ppdu_setup_init *)tlv_data;
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
		struct hal_tx_queue_exten *tx_q_exten =
			(struct hal_tx_queue_exten *)tlv_data;

		info[0] = __le32_to_cpu(tx_q_exten->info0);

		tx_ppdu_info->rx_status.frame_control =
			u32_get_bits(info[0],
				     HAL_TX_Q_EXT_INFO0_FRAME_CTRL);
		tx_ppdu_info->rx_status.fc_valid = true;
		break;
	}

	case HAL_TX_FES_STATUS_START: {
		struct hal_tx_fes_status_start *tx_fes_start =
			(struct hal_tx_fes_status_start *)tlv_data;

		info[0] = __le32_to_cpu(tx_fes_start->info0);

		tx_ppdu_info->rx_status.medium_prot_type =
			u32_get_bits(info[0],
				     HAL_TX_FES_STATUS_START_INFO0_MEDIUM_PROT_TYPE);
		break;
	}

	case HAL_TX_FES_STATUS_PROT: {
		struct hal_tx_fes_status_prot *tx_fes_status =
			(struct hal_tx_fes_status_prot *)tlv_data;
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
		struct hal_tx_fes_status_start_prot *tx_fes_stat_start =
			(struct hal_tx_fes_status_start_prot *)tlv_data;
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
		struct hal_tx_fes_status_user_ppdu *tx_fes_usr_ppdu =
			(struct hal_tx_fes_status_user_ppdu *)tlv_data;

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
		struct hal_rx_frame_bitmap_ack *fbm_ack =
			(struct hal_rx_frame_bitmap_ack *)tlv_data;
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
		struct hal_tx_phy_desc *tx_phy_desc =
			(struct hal_tx_phy_desc *)tlv_data;

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

	case HAL_MON_BUF_ADDR: {
		struct dp_rxdma_ring *buf_ring = &ab->dp.tx_mon_buf_ring;
		struct dp_mon_packet_info *packet_info =
			(struct dp_mon_packet_info *)tlv_data;
		int buf_id = u32_get_bits(packet_info->cookie,
					  DP_RXDMA_BUF_COOKIE_BUF_ID);
		struct sk_buff *msdu;
		struct dp_mon_mpdu *mon_mpdu = tx_ppdu_info->tx_mon_mpdu;
		struct ath12k_skb_rxcb *rxcb;

		spin_lock_bh(&buf_ring->idr_lock);
		msdu = idr_remove(&buf_ring->bufs_idr, buf_id);
		spin_unlock_bh(&buf_ring->idr_lock);

		if (unlikely(!msdu)) {
			ath12k_warn(ab, "montior destination with invalid buf_id %d\n",
				    buf_id);
			return DP_MON_TX_STATUS_PPDU_NOT_DONE;
		}

		rxcb = ATH12K_SKB_RXCB(msdu);
		dma_unmap_single(ab->dev, rxcb->paddr,
				 msdu->len + skb_tailroom(msdu),
				 DMA_FROM_DEVICE);

		if (!mon_mpdu->head)
			mon_mpdu->head = msdu;
		else if (mon_mpdu->tail)
			mon_mpdu->tail->next = msdu;

		mon_mpdu->tail = msdu;

		ath12k_dp_mon_buf_replenish(ab, buf_ring, 1);
		status = DP_MON_TX_BUFFER_ADDR;
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
ath12k_dp_mon_tx_process_ppdu_info(struct ath12k *ar, int mac_id,
				   struct napi_struct *napi,
				   struct dp_mon_tx_ppdu_info *tx_ppdu_info)
{
	struct dp_mon_mpdu *tmp, *mon_mpdu;
	struct sk_buff *head_msdu;

	list_for_each_entry_safe(mon_mpdu, tmp,
				 &tx_ppdu_info->dp_tx_mon_mpdu_list, list) {
		list_del(&mon_mpdu->list);
		head_msdu = mon_mpdu->head;

		if (head_msdu)
			ath12k_dp_mon_rx_deliver(ar, mac_id, head_msdu,
						 &tx_ppdu_info->rx_status, napi);

		kfree(mon_mpdu);
	}
}

enum hal_rx_mon_status
ath12k_dp_mon_tx_parse_mon_status(struct ath12k *ar,
				  struct ath12k_mon_data *pmon,
				  int mac_id,
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

	ath12k_dp_mon_tx_process_ppdu_info(ar, mac_id, napi, tx_data_ppdu_info);
	ath12k_dp_mon_tx_process_ppdu_info(ar, mac_id, napi, tx_prot_ppdu_info);

	return tlv_status;
}

int ath12k_dp_mon_srng_process(struct ath12k *ar, int mac_id, int *budget,
			       enum dp_monitor_mode monitor_mode,
			       struct napi_struct *napi)
{
	struct hal_mon_dest_desc *mon_dst_desc;
	struct ath12k_pdev_dp *pdev_dp = &ar->dp;
	struct ath12k_mon_data *pmon = (struct ath12k_mon_data *)&pdev_dp->mon_data;
	struct ath12k_base *ab = ar->ab;
	struct ath12k_dp *dp = &ab->dp;
	struct sk_buff *skb;
	struct ath12k_skb_rxcb *rxcb;
	struct dp_srng *mon_dst_ring;
	struct hal_srng *srng;
	struct dp_rxdma_ring *buf_ring;
	u64 cookie;
	u32 ppdu_id;
	int num_buffs_reaped = 0, srng_id, buf_id;
	u8 dest_idx = 0, i;
	bool end_of_ppdu;
	struct hal_rx_mon_ppdu_info *ppdu_info;
	struct ath12k_peer *peer = NULL;

	ppdu_info = &pmon->mon_ppdu_info;
	memset(ppdu_info, 0, sizeof(*ppdu_info));
	ppdu_info->peer_id = HAL_INVALID_PEERID;

	srng_id = ath12k_hw_mac_id_to_srng_id(ab->hw_params, mac_id);

	if (monitor_mode == ATH12K_DP_RX_MONITOR_MODE) {
		mon_dst_ring = &pdev_dp->rxdma_mon_dst_ring[srng_id];
		buf_ring = &dp->rxdma_mon_buf_ring;
	} else {
		mon_dst_ring = &pdev_dp->tx_mon_dst_ring[srng_id];
		buf_ring = &dp->tx_mon_buf_ring;
	}

	srng = &ab->hal.srng_list[mon_dst_ring->ring_id];

	spin_lock_bh(&srng->lock);
	ath12k_hal_srng_access_begin(ab, srng);

	while (likely(*budget)) {
		*budget -= 1;
		mon_dst_desc = ath12k_hal_srng_dst_peek(ab, srng);
		if (unlikely(!mon_dst_desc))
			break;

		cookie = le32_to_cpu(mon_dst_desc->cookie);
		buf_id = u32_get_bits(cookie, DP_RXDMA_BUF_COOKIE_BUF_ID);

		spin_lock_bh(&buf_ring->idr_lock);
		skb = idr_remove(&buf_ring->bufs_idr, buf_id);
		spin_unlock_bh(&buf_ring->idr_lock);

		if (unlikely(!skb)) {
			ath12k_warn(ab, "montior destination with invalid buf_id %d\n",
				    buf_id);
			goto move_next;
		}

		rxcb = ATH12K_SKB_RXCB(skb);
		dma_unmap_single(ab->dev, rxcb->paddr,
				 skb->len + skb_tailroom(skb),
				 DMA_FROM_DEVICE);

		pmon->dest_skb_q[dest_idx] = skb;
		dest_idx++;
		ppdu_id = le32_to_cpu(mon_dst_desc->ppdu_id);
		end_of_ppdu = le32_get_bits(mon_dst_desc->info0,
					    HAL_MON_DEST_INFO0_END_OF_PPDU);
		if (!end_of_ppdu)
			continue;

		for (i = 0; i < dest_idx; i++) {
			skb = pmon->dest_skb_q[i];

			if (monitor_mode == ATH12K_DP_RX_MONITOR_MODE)
				ath12k_dp_mon_rx_parse_mon_status(ar, pmon, mac_id,
								  skb, napi);
			else
				ath12k_dp_mon_tx_parse_mon_status(ar, pmon, mac_id,
								  skb, napi, ppdu_id);

			peer = ath12k_peer_find_by_id(ab, ppdu_info->peer_id);

			if (!peer || !peer->sta) {
				ath12k_dbg(ab, ATH12K_DBG_DATA,
					   "failed to find the peer with peer_id %d\n",
					   ppdu_info->peer_id);
				dev_kfree_skb_any(skb);
				continue;
			}

			dev_kfree_skb_any(skb);
			pmon->dest_skb_q[i] = NULL;
		}

		dest_idx = 0;
move_next:
		ath12k_dp_mon_buf_replenish(ab, buf_ring, 1);
		ath12k_hal_srng_src_get_next_entry(ab, srng);
		num_buffs_reaped++;
	}

	ath12k_hal_srng_access_end(ab, srng);
	spin_unlock_bh(&srng->lock);

	return num_buffs_reaped;
}

static void
ath12k_dp_mon_rx_update_peer_rate_table_stats(struct ath12k_rx_peer_stats *rx_stats,
					      struct hal_rx_mon_ppdu_info *ppdu_info,
					      struct hal_rx_user_status *user_stats,
					      u32 num_msdu)
{
	u32 rate_idx = 0;
	u32 mcs_idx = (user_stats) ? user_stats->mcs : ppdu_info->mcs;
	u32 nss_idx = (user_stats) ? user_stats->nss - 1 : ppdu_info->nss - 1;
	u32 bw_idx = ppdu_info->bw;
	u32 gi_idx = ppdu_info->gi;

	if ((mcs_idx > HAL_RX_MAX_MCS_HE) || (nss_idx >= HAL_RX_MAX_NSS) ||
	    (bw_idx >= HAL_RX_BW_MAX) || (gi_idx >= HAL_RX_GI_MAX)) {
		return;
	}

	if (ppdu_info->preamble_type == HAL_RX_PREAMBLE_11N ||
	    ppdu_info->preamble_type == HAL_RX_PREAMBLE_11AC) {
		rate_idx = mcs_idx * 8 + 8 * 10 * nss_idx;
		rate_idx += bw_idx * 2 + gi_idx;
	} else if (ppdu_info->preamble_type == HAL_RX_PREAMBLE_11AX) {
		gi_idx = ath12k_he_gi_to_nl80211_he_gi(ppdu_info->gi);
		rate_idx = mcs_idx * 12 + 12 * 12 * nss_idx;
		rate_idx += bw_idx * 3 + gi_idx;
	} else {
		return;
	}

	rx_stats->pkt_stats.rx_rate[rate_idx] += num_msdu;
	if (user_stats)
		rx_stats->byte_stats.rx_rate[rate_idx] += user_stats->mpdu_ok_byte_count;
	else
		rx_stats->byte_stats.rx_rate[rate_idx] += ppdu_info->mpdu_len;
}

static void ath12k_dp_mon_rx_update_peer_su_stats(struct ath12k *ar,
						  struct ath12k_sta *arsta,
						  struct hal_rx_mon_ppdu_info *ppdu_info)
{
	struct ath12k_rx_peer_stats *rx_stats = arsta->rx_stats;
	u32 num_msdu;

	if (!rx_stats)
		return;

	arsta->rssi_comb = ppdu_info->rssi_comb;

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
	struct ath12k_sta *arsta = NULL;
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

	arsta = (struct ath12k_sta *)peer->sta->drv_priv;
	rx_stats = arsta->rx_stats;

	if (!rx_stats)
		return;

	arsta->rssi_comb = ppdu_info->rssi_comb;

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

int ath12k_dp_mon_rx_process_stats(struct ath12k *ar, int mac_id,
				   struct napi_struct *napi, int *budget)
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
	struct dp_rxdma_ring *buf_ring;
	struct ath12k_sta *arsta = NULL;
	struct ath12k_peer *peer;
	u64 cookie;
	int num_buffs_reaped = 0, srng_id, buf_id;
	u8 dest_idx = 0, i;
	bool end_of_ppdu;
	u32 hal_status;

	srng_id = ath12k_hw_mac_id_to_srng_id(ab->hw_params, mac_id);
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
		cookie = le32_to_cpu(mon_dst_desc->cookie);
		buf_id = u32_get_bits(cookie, DP_RXDMA_BUF_COOKIE_BUF_ID);

		spin_lock_bh(&buf_ring->idr_lock);
		skb = idr_remove(&buf_ring->bufs_idr, buf_id);
		spin_unlock_bh(&buf_ring->idr_lock);

		if (unlikely(!skb)) {
			ath12k_warn(ab, "montior destination with invalid buf_id %d\n",
				    buf_id);
			goto move_next;
		}

		rxcb = ATH12K_SKB_RXCB(skb);
		dma_unmap_single(ab->dev, rxcb->paddr,
				 skb->len + skb_tailroom(skb),
				 DMA_FROM_DEVICE);
		pmon->dest_skb_q[dest_idx] = skb;
		dest_idx++;
		end_of_ppdu = le32_get_bits(mon_dst_desc->info0,
					    HAL_MON_DEST_INFO0_END_OF_PPDU);
		if (!end_of_ppdu)
			continue;

		for (i = 0; i < dest_idx; i++) {
			skb = pmon->dest_skb_q[i];
			hal_status = ath12k_dp_mon_parse_rx_dest(ab, pmon, skb);

			if (ppdu_info->peer_id == HAL_INVALID_PEERID ||
			    hal_status != HAL_RX_MON_STATUS_PPDU_DONE) {
				dev_kfree_skb_any(skb);
				continue;
			}

			rcu_read_lock();
			spin_lock_bh(&ab->base_lock);
			peer = ath12k_peer_find_by_id(ab, ppdu_info->peer_id);
			if (!peer || !peer->sta) {
				ath12k_dbg(ab, ATH12K_DBG_DATA,
					   "failed to find the peer with peer_id %d\n",
					   ppdu_info->peer_id);
				spin_unlock_bh(&ab->base_lock);
				rcu_read_unlock();
				dev_kfree_skb_any(skb);
				continue;
			}

			if (ppdu_info->reception_type == HAL_RX_RECEPTION_TYPE_SU) {
				arsta = (struct ath12k_sta *)peer->sta->drv_priv;
				ath12k_dp_mon_rx_update_peer_su_stats(ar, arsta,
								      ppdu_info);
			} else if ((ppdu_info->fc_valid) &&
				   (ppdu_info->ast_index != HAL_AST_IDX_INVALID)) {
				ath12k_dp_mon_rx_process_ulofdma(ppdu_info);
				ath12k_dp_mon_rx_update_peer_mu_stats(ar, ppdu_info);
			}

			spin_unlock_bh(&ab->base_lock);
			rcu_read_unlock();
			dev_kfree_skb_any(skb);
			memset(ppdu_info, 0, sizeof(*ppdu_info));
			ppdu_info->peer_id = HAL_INVALID_PEERID;
		}

		dest_idx = 0;
move_next:
		ath12k_dp_mon_buf_replenish(ab, buf_ring, 1);
		ath12k_hal_srng_src_get_next_entry(ab, srng);
		num_buffs_reaped++;
	}

	ath12k_hal_srng_access_end(ab, srng);
	spin_unlock_bh(&srng->lock);
	return num_buffs_reaped;
}

int ath12k_dp_mon_process_ring(struct ath12k_base *ab, int mac_id,
			       struct napi_struct *napi, int budget,
			       enum dp_monitor_mode monitor_mode)
{
	struct ath12k *ar = ath12k_ab_to_ar(ab, mac_id);
	int num_buffs_reaped = 0;

	if (!ar->monitor_started)
		ath12k_dp_mon_rx_process_stats(ar, mac_id, napi, &budget);
	else
		num_buffs_reaped = ath12k_dp_mon_srng_process(ar, mac_id, &budget,
							      monitor_mode, napi);

	return num_buffs_reaped;
}
