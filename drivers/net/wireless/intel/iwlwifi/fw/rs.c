// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2021-2022 Intel Corporation
 */

#include <net/mac80211.h>
#include "fw/api/rs.h"
#include "iwl-drv.h"
#include "iwl-config.h"

#define IWL_DECLARE_RATE_INFO(r) \
	[IWL_RATE_##r##M_INDEX] = IWL_RATE_##r##M_PLCP

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

/* mbps, mcs */
static const struct iwl_rate_mcs_info rate_mcs[IWL_RATE_COUNT] = {
	{  "1", "BPSK DSSS"},
	{  "2", "QPSK DSSS"},
	{"5.5", "BPSK CCK"},
	{ "11", "QPSK CCK"},
	{  "6", "BPSK 1/2"},
	{  "9", "BPSK 1/2"},
	{ "12", "QPSK 1/2"},
	{ "18", "QPSK 3/4"},
	{ "24", "16QAM 1/2"},
	{ "36", "16QAM 3/4"},
	{ "48", "64QAM 2/3"},
	{ "54", "64QAM 3/4"},
	{ "60", "64QAM 5/6"},
};

static const char * const ant_name[] = {
	[ANT_NONE] = "None",
	[ANT_A]    = "A",
	[ANT_B]    = "B",
	[ANT_AB]   = "AB",
};

static const char * const pretty_bw[] = {
	"20Mhz",
	"40Mhz",
	"80Mhz",
	"160 Mhz",
	"320Mhz",
};

u8 iwl_fw_rate_idx_to_plcp(int idx)
{
	return fw_rate_idx_to_plcp[idx];
}
IWL_EXPORT_SYMBOL(iwl_fw_rate_idx_to_plcp);

const struct iwl_rate_mcs_info *iwl_rate_mcs(int idx)
{
	return &rate_mcs[idx];
}
IWL_EXPORT_SYMBOL(iwl_rate_mcs);

const char *iwl_rs_pretty_ant(u8 ant)
{
	if (ant >= ARRAY_SIZE(ant_name))
		return "UNKNOWN";

	return ant_name[ant];
}
IWL_EXPORT_SYMBOL(iwl_rs_pretty_ant);

const char *iwl_rs_pretty_bw(int bw)
{
	if (bw >= ARRAY_SIZE(pretty_bw))
		return "unknown bw";

	return pretty_bw[bw];
}
IWL_EXPORT_SYMBOL(iwl_rs_pretty_bw);

static u32 iwl_legacy_rate_to_fw_idx(u32 rate_n_flags)
{
	int rate = rate_n_flags & RATE_LEGACY_RATE_MSK_V1;
	int idx;
	bool ofdm = !(rate_n_flags & RATE_MCS_CCK_MSK_V1);
	int offset = ofdm ? IWL_FIRST_OFDM_RATE : 0;
	int last = ofdm ? IWL_RATE_COUNT_LEGACY : IWL_FIRST_OFDM_RATE;

	for (idx = offset; idx < last; idx++)
		if (iwl_fw_rate_idx_to_plcp(idx) == rate)
			return idx - offset;
	return IWL_RATE_INVALID;
}

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

		rate_v2 |= rate_v1 & RATE_MCS_NSS_MSK;

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

		if (WARN_ON_ONCE(legacy_rate == IWL_RATE_INVALID))
			legacy_rate = (rate_v1 & RATE_MCS_CCK_MSK_V1) ?
				IWL_FIRST_CCK_RATE : IWL_FIRST_OFDM_RATE;

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

int rs_pretty_print_rate(char *buf, int bufsz, const u32 rate)
{
	char *type;
	u8 mcs = 0, nss = 0;
	u8 ant = (rate & RATE_MCS_ANT_AB_MSK) >> RATE_MCS_ANT_POS;
	u32 bw = (rate & RATE_MCS_CHAN_WIDTH_MSK) >>
		RATE_MCS_CHAN_WIDTH_POS;
	u32 format = rate & RATE_MCS_MOD_TYPE_MSK;
	bool sgi;

	if (format == RATE_MCS_CCK_MSK ||
	    format == RATE_MCS_LEGACY_OFDM_MSK) {
		int legacy_rate = rate & RATE_LEGACY_RATE_MSK;
		int index = format == RATE_MCS_CCK_MSK ?
			legacy_rate :
			legacy_rate + IWL_FIRST_OFDM_RATE;

		return scnprintf(buf, bufsz, "Legacy | ANT: %s Rate: %s Mbps",
				 iwl_rs_pretty_ant(ant),
				 index == IWL_RATE_INVALID ? "BAD" :
				 iwl_rate_mcs(index)->mbps);
	}

	if (format ==  RATE_MCS_VHT_MSK)
		type = "VHT";
	else if (format ==  RATE_MCS_HT_MSK)
		type = "HT";
	else if (format == RATE_MCS_HE_MSK)
		type = "HE";
	else if (format == RATE_MCS_EHT_MSK)
		type = "EHT";
	else
		type = "Unknown"; /* shouldn't happen */

	mcs = format == RATE_MCS_HT_MSK ?
		RATE_HT_MCS_INDEX(rate) :
		rate & RATE_MCS_CODE_MSK;
	nss = ((rate & RATE_MCS_NSS_MSK)
	       >> RATE_MCS_NSS_POS) + 1;
	sgi = format == RATE_MCS_HE_MSK ?
		iwl_he_is_sgi(rate) :
		rate & RATE_MCS_SGI_MSK;

	return scnprintf(buf, bufsz,
			 "0x%x: %s | ANT: %s BW: %s MCS: %d NSS: %d %s%s%s%s%s",
			 rate, type, iwl_rs_pretty_ant(ant), iwl_rs_pretty_bw(bw), mcs, nss,
			 (sgi) ? "SGI " : "NGI ",
			 (rate & RATE_MCS_STBC_MSK) ? "STBC " : "",
			 (rate & RATE_MCS_LDPC_MSK) ? "LDPC " : "",
			 (rate & RATE_HE_DUAL_CARRIER_MODE_MSK) ? "DCM " : "",
			 (rate & RATE_MCS_BF_MSK) ? "BF " : "");
}
IWL_EXPORT_SYMBOL(rs_pretty_print_rate);

bool iwl_he_is_sgi(u32 rate_n_flags)
{
	u32 type = rate_n_flags & RATE_MCS_HE_TYPE_MSK;
	u32 ltf_gi = rate_n_flags & RATE_MCS_HE_GI_LTF_MSK;

	if (type == RATE_MCS_HE_TYPE_SU ||
	    type == RATE_MCS_HE_TYPE_EXT_SU)
		return ltf_gi == RATE_MCS_HE_SU_4_LTF_08_GI;
	return false;
}
IWL_EXPORT_SYMBOL(iwl_he_is_sgi);

