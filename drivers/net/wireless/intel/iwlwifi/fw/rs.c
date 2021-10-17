// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2021 Intel Corporation
 */

#include <net/mac80211.h>
#include "fw/api/rs.h"
#include "iwl-drv.h"

#define IWL_DECLARE_RATE_INFO(r) \
	[IWL_RATE_##r##M_INDEX] = IWL_RATE_##r##M_PLCP

u8 iwl_fw_rate_idx_to_plcp(int idx)
{
/*
 * Translate from fw_rate_index (IWL_RATE_XXM_INDEX) to PLCP
 * */
static const u8 fw_rate_idx_to_plcp[IWL_RATE_COUNT] = {
	IWL_DECLARE_RATE_INFO(1),
	IWL_DECLARE_RATE_INFO(2),
	IWL_DECLARE_RATE_INFO(5),
	IWL_DECLARE_RATE_INFO(11),
	IWL_DECLARE_RATE_INFO(6),
	IWL_DECLARE_RATE_INFO(9),
	IWL_DECLARE_RATE_INFO(12),
	IWL_DECLARE_RATE_INFO(18),
	IWL_DECLARE_RATE_INFO(24),
	IWL_DECLARE_RATE_INFO(36),
	IWL_DECLARE_RATE_INFO(48),
	IWL_DECLARE_RATE_INFO(54),
};

	return fw_rate_idx_to_plcp[idx];
}
IWL_EXPORT_SYMBOL(iwl_fw_rate_idx_to_plcp);

u32 iwl_new_rate_from_v1(u32 rate_v1)
{
	u32 rate_v2 = 0;
	u32 dup = 0;

	if (rate_v1 == 0)
		return rate_v1;
	/* convert rate */
	if (rate_v1 & RATE_MCS_HT_MSK_V1) {
		u32 nss = 0;

		rate_v2 |= RATE_MCS_HT_MSK;
		rate_v2 |=
			rate_v1 & RATE_HT_MCS_RATE_CODE_MSK_V1;
		nss = (rate_v1 & RATE_HT_MCS_MIMO2_MSK) >>
			RATE_HT_MCS_NSS_POS_V1;
		rate_v2 |= nss << RATE_MCS_NSS_POS;
	} else if (rate_v1 & RATE_MCS_VHT_MSK_V1 ||
		   rate_v1 & RATE_MCS_HE_MSK_V1) {
		rate_v2 |= rate_v1 & RATE_VHT_MCS_RATE_CODE_MSK;

		rate_v2 |= rate_v1 & RATE_VHT_MCS_MIMO2_MSK;

		if (rate_v1 & RATE_MCS_HE_MSK_V1) {
			u32 he_type_bits = rate_v1 & RATE_MCS_HE_TYPE_MSK_V1;
			u32 he_type = he_type_bits >> RATE_MCS_HE_TYPE_POS_V1;
			u32 he_106t = (rate_v1 & RATE_MCS_HE_106T_MSK_V1) >>
				RATE_MCS_HE_106T_POS_V1;
			u32 he_gi_ltf = (rate_v1 & RATE_MCS_HE_GI_LTF_MSK_V1) >>
				RATE_MCS_HE_GI_LTF_POS;

			if ((he_type_bits == RATE_MCS_HE_TYPE_SU ||
			     he_type_bits == RATE_MCS_HE_TYPE_EXT_SU) &&
			    he_gi_ltf == RATE_MCS_HE_SU_4_LTF)
				/* the new rate have an additional bit to
				 * represent the value 4 rather then using SGI
				 * bit for this purpose - as it was done in the old
				 * rate */
				he_gi_ltf += (rate_v1 & RATE_MCS_SGI_MSK_V1) >>
					RATE_MCS_SGI_POS_V1;

			rate_v2 |= he_gi_ltf << RATE_MCS_HE_GI_LTF_POS;
			rate_v2 |= he_type << RATE_MCS_HE_TYPE_POS;
			rate_v2 |= he_106t << RATE_MCS_HE_106T_POS;
			rate_v2 |= rate_v1 & RATE_HE_DUAL_CARRIER_MODE_MSK;
			rate_v2 |= RATE_MCS_HE_MSK;
		} else {
			rate_v2 |= RATE_MCS_VHT_MSK;
		}
	/* if legacy format */
	} else {
		u32 legacy_rate = iwl_legacy_rate_to_fw_idx(rate_v1);

		WARN_ON(legacy_rate < 0);
		rate_v2 |= legacy_rate;
		if (!(rate_v1 & RATE_MCS_CCK_MSK_V1))
			rate_v2 |= RATE_MCS_LEGACY_OFDM_MSK;
	}

	/* convert flags */
	if (rate_v1 & RATE_MCS_LDPC_MSK_V1)
		rate_v2 |= RATE_MCS_LDPC_MSK;
	rate_v2 |= (rate_v1 & RATE_MCS_CHAN_WIDTH_MSK_V1) |
		(rate_v1 & RATE_MCS_ANT_AB_MSK) |
		(rate_v1 & RATE_MCS_STBC_MSK) |
		(rate_v1 & RATE_MCS_BF_MSK);

	dup = (rate_v1 & RATE_MCS_DUP_MSK_V1) >> RATE_MCS_DUP_POS_V1;
	if (dup) {
		rate_v2 |= RATE_MCS_DUP_MSK;
		rate_v2 |= dup << RATE_MCS_CHAN_WIDTH_POS;
	}

	if ((!(rate_v1 & RATE_MCS_HE_MSK_V1)) &&
	    (rate_v1 & RATE_MCS_SGI_MSK_V1))
		rate_v2 |= RATE_MCS_SGI_MSK;

	return rate_v2;
}
IWL_EXPORT_SYMBOL(iwl_new_rate_from_v1);

u32 iwl_legacy_rate_to_fw_idx(u32 rate_n_flags)
{
	int rate = rate_n_flags & RATE_LEGACY_RATE_MSK_V1;
	int idx;
	bool ofdm = !(rate_n_flags & RATE_MCS_CCK_MSK_V1);
	int offset = ofdm ? IWL_FIRST_OFDM_RATE : 0;
	int last = ofdm ? IWL_RATE_COUNT_LEGACY : IWL_FIRST_OFDM_RATE;

	for (idx = offset; idx < last; idx++)
		if (iwl_fw_rate_idx_to_plcp(idx) == rate)
			return idx - offset;
	return -1;
}

