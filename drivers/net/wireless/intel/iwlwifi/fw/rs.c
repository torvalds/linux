// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2021-2022, 2025 Intel Corporation
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

int rs_pretty_print_rate(char *buf, int bufsz, const u32 rate)
{
	char *type;
	u8 mcs = 0, nss = 0;
	u8 ant = (rate & RATE_MCS_ANT_AB_MSK) >> RATE_MCS_ANT_POS;
	u32 bw = (rate & RATE_MCS_CHAN_WIDTH_MSK) >>
		RATE_MCS_CHAN_WIDTH_POS;
	u32 format = rate & RATE_MCS_MOD_TYPE_MSK;
	int index = 0;
	bool sgi;

	switch (format) {
	case RATE_MCS_MOD_TYPE_LEGACY_OFDM:
		index = IWL_FIRST_OFDM_RATE;
		fallthrough;
	case RATE_MCS_MOD_TYPE_CCK:
		index += rate & RATE_LEGACY_RATE_MSK;

		return scnprintf(buf, bufsz, "Legacy | ANT: %s Rate: %s Mbps",
				 iwl_rs_pretty_ant(ant),
				 iwl_rate_mcs(index)->mbps);
	case RATE_MCS_MOD_TYPE_VHT:
		type = "VHT";
		break;
	case RATE_MCS_MOD_TYPE_HT:
		type = "HT";
		break;
	case RATE_MCS_MOD_TYPE_HE:
		type = "HE";
		break;
	case RATE_MCS_MOD_TYPE_EHT:
		type = "EHT";
		break;
	default:
		type = "Unknown"; /* shouldn't happen */
	}

	mcs = format == RATE_MCS_MOD_TYPE_HT ?
		RATE_HT_MCS_INDEX(rate) :
		rate & RATE_MCS_CODE_MSK;
	nss = u32_get_bits(rate, RATE_MCS_NSS_MSK);
	sgi = format == RATE_MCS_MOD_TYPE_HE ?
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

