// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2005-2014, 2018-2023 Intel Corporation
 * Copyright (C) 2013-2015 Intel Mobile Communications GmbH
 * Copyright (C) 2016-2017 Intel Deutschland GmbH
 */
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/export.h>
#include <linux/etherdevice.h>
#include <linux/pci.h>
#include <linux/firmware.h>

#include "iwl-drv.h"
#include "iwl-modparams.h"
#include "iwl-nvm-parse.h"
#include "iwl-prph.h"
#include "iwl-io.h"
#include "iwl-csr.h"
#include "fw/acpi.h"
#include "fw/api/nvm-reg.h"
#include "fw/api/commands.h"
#include "fw/api/cmdhdr.h"
#include "fw/img.h"
#include "mei/iwl-mei.h"

/* NVM offsets (in words) definitions */
enum nvm_offsets {
	/* NVM HW-Section offset (in words) definitions */
	SUBSYSTEM_ID = 0x0A,
	HW_ADDR = 0x15,

	/* NVM SW-Section offset (in words) definitions */
	NVM_SW_SECTION = 0x1C0,
	NVM_VERSION = 0,
	RADIO_CFG = 1,
	SKU = 2,
	N_HW_ADDRS = 3,
	NVM_CHANNELS = 0x1E0 - NVM_SW_SECTION,

	/* NVM calibration section offset (in words) definitions */
	NVM_CALIB_SECTION = 0x2B8,
	XTAL_CALIB = 0x316 - NVM_CALIB_SECTION,

	/* NVM REGULATORY -Section offset (in words) definitions */
	NVM_CHANNELS_SDP = 0,
};

enum ext_nvm_offsets {
	/* NVM HW-Section offset (in words) definitions */
	MAC_ADDRESS_OVERRIDE_EXT_NVM = 1,

	/* NVM SW-Section offset (in words) definitions */
	NVM_VERSION_EXT_NVM = 0,
	N_HW_ADDRS_FAMILY_8000 = 3,

	/* NVM PHY_SKU-Section offset (in words) definitions */
	RADIO_CFG_FAMILY_EXT_NVM = 0,
	SKU_FAMILY_8000 = 2,

	/* NVM REGULATORY -Section offset (in words) definitions */
	NVM_CHANNELS_EXTENDED = 0,
	NVM_LAR_OFFSET_OLD = 0x4C7,
	NVM_LAR_OFFSET = 0x507,
	NVM_LAR_ENABLED = 0x7,
};

/* SKU Capabilities (actual values from NVM definition) */
enum nvm_sku_bits {
	NVM_SKU_CAP_BAND_24GHZ		= BIT(0),
	NVM_SKU_CAP_BAND_52GHZ		= BIT(1),
	NVM_SKU_CAP_11N_ENABLE		= BIT(2),
	NVM_SKU_CAP_11AC_ENABLE		= BIT(3),
	NVM_SKU_CAP_MIMO_DISABLE	= BIT(5),
};

/*
 * These are the channel numbers in the order that they are stored in the NVM
 */
static const u16 iwl_nvm_channels[] = {
	/* 2.4 GHz */
	1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
	/* 5 GHz */
	36, 40, 44, 48, 52, 56, 60, 64,
	100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 144,
	149, 153, 157, 161, 165
};

static const u16 iwl_ext_nvm_channels[] = {
	/* 2.4 GHz */
	1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
	/* 5 GHz */
	36, 40, 44, 48, 52, 56, 60, 64, 68, 72, 76, 80, 84, 88, 92,
	96, 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 144,
	149, 153, 157, 161, 165, 169, 173, 177, 181
};

static const u16 iwl_uhb_nvm_channels[] = {
	/* 2.4 GHz */
	1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
	/* 5 GHz */
	36, 40, 44, 48, 52, 56, 60, 64, 68, 72, 76, 80, 84, 88, 92,
	96, 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 144,
	149, 153, 157, 161, 165, 169, 173, 177, 181,
	/* 6-7 GHz */
	1, 5, 9, 13, 17, 21, 25, 29, 33, 37, 41, 45, 49, 53, 57, 61, 65, 69,
	73, 77, 81, 85, 89, 93, 97, 101, 105, 109, 113, 117, 121, 125, 129,
	133, 137, 141, 145, 149, 153, 157, 161, 165, 169, 173, 177, 181, 185,
	189, 193, 197, 201, 205, 209, 213, 217, 221, 225, 229, 233
};

#define IWL_NVM_NUM_CHANNELS		ARRAY_SIZE(iwl_nvm_channels)
#define IWL_NVM_NUM_CHANNELS_EXT	ARRAY_SIZE(iwl_ext_nvm_channels)
#define IWL_NVM_NUM_CHANNELS_UHB	ARRAY_SIZE(iwl_uhb_nvm_channels)
#define NUM_2GHZ_CHANNELS		14
#define NUM_5GHZ_CHANNELS		37
#define FIRST_2GHZ_HT_MINUS		5
#define LAST_2GHZ_HT_PLUS		9
#define N_HW_ADDR_MASK			0xF

/* rate data (static) */
static struct ieee80211_rate iwl_cfg80211_rates[] = {
	{ .bitrate = 1 * 10, .hw_value = 0, .hw_value_short = 0, },
	{ .bitrate = 2 * 10, .hw_value = 1, .hw_value_short = 1,
	  .flags = IEEE80211_RATE_SHORT_PREAMBLE, },
	{ .bitrate = 5.5 * 10, .hw_value = 2, .hw_value_short = 2,
	  .flags = IEEE80211_RATE_SHORT_PREAMBLE, },
	{ .bitrate = 11 * 10, .hw_value = 3, .hw_value_short = 3,
	  .flags = IEEE80211_RATE_SHORT_PREAMBLE, },
	{ .bitrate = 6 * 10, .hw_value = 4, .hw_value_short = 4, },
	{ .bitrate = 9 * 10, .hw_value = 5, .hw_value_short = 5, },
	{ .bitrate = 12 * 10, .hw_value = 6, .hw_value_short = 6, },
	{ .bitrate = 18 * 10, .hw_value = 7, .hw_value_short = 7, },
	{ .bitrate = 24 * 10, .hw_value = 8, .hw_value_short = 8, },
	{ .bitrate = 36 * 10, .hw_value = 9, .hw_value_short = 9, },
	{ .bitrate = 48 * 10, .hw_value = 10, .hw_value_short = 10, },
	{ .bitrate = 54 * 10, .hw_value = 11, .hw_value_short = 11, },
};
#define RATES_24_OFFS	0
#define N_RATES_24	ARRAY_SIZE(iwl_cfg80211_rates)
#define RATES_52_OFFS	4
#define N_RATES_52	(N_RATES_24 - RATES_52_OFFS)

/**
 * enum iwl_nvm_channel_flags - channel flags in NVM
 * @NVM_CHANNEL_VALID: channel is usable for this SKU/geo
 * @NVM_CHANNEL_IBSS: usable as an IBSS channel
 * @NVM_CHANNEL_ACTIVE: active scanning allowed
 * @NVM_CHANNEL_RADAR: radar detection required
 * @NVM_CHANNEL_INDOOR_ONLY: only indoor use is allowed
 * @NVM_CHANNEL_GO_CONCURRENT: GO operation is allowed when connected to BSS
 *	on same channel on 2.4 or same UNII band on 5.2
 * @NVM_CHANNEL_UNIFORM: uniform spreading required
 * @NVM_CHANNEL_20MHZ: 20 MHz channel okay
 * @NVM_CHANNEL_40MHZ: 40 MHz channel okay
 * @NVM_CHANNEL_80MHZ: 80 MHz channel okay
 * @NVM_CHANNEL_160MHZ: 160 MHz channel okay
 * @NVM_CHANNEL_DC_HIGH: DC HIGH required/allowed (?)
 */
enum iwl_nvm_channel_flags {
	NVM_CHANNEL_VALID		= BIT(0),
	NVM_CHANNEL_IBSS		= BIT(1),
	NVM_CHANNEL_ACTIVE		= BIT(3),
	NVM_CHANNEL_RADAR		= BIT(4),
	NVM_CHANNEL_INDOOR_ONLY		= BIT(5),
	NVM_CHANNEL_GO_CONCURRENT	= BIT(6),
	NVM_CHANNEL_UNIFORM		= BIT(7),
	NVM_CHANNEL_20MHZ		= BIT(8),
	NVM_CHANNEL_40MHZ		= BIT(9),
	NVM_CHANNEL_80MHZ		= BIT(10),
	NVM_CHANNEL_160MHZ		= BIT(11),
	NVM_CHANNEL_DC_HIGH		= BIT(12),
};

/**
 * enum iwl_reg_capa_flags_v1 - global flags applied for the whole regulatory
 * domain.
 * @REG_CAPA_V1_BF_CCD_LOW_BAND: Beam-forming or Cyclic Delay Diversity in the
 *	2.4Ghz band is allowed.
 * @REG_CAPA_V1_BF_CCD_HIGH_BAND: Beam-forming or Cyclic Delay Diversity in the
 *	5Ghz band is allowed.
 * @REG_CAPA_V1_160MHZ_ALLOWED: 11ac channel with a width of 160Mhz is allowed
 *	for this regulatory domain (valid only in 5Ghz).
 * @REG_CAPA_V1_80MHZ_ALLOWED: 11ac channel with a width of 80Mhz is allowed
 *	for this regulatory domain (valid only in 5Ghz).
 * @REG_CAPA_V1_MCS_8_ALLOWED: 11ac with MCS 8 is allowed.
 * @REG_CAPA_V1_MCS_9_ALLOWED: 11ac with MCS 9 is allowed.
 * @REG_CAPA_V1_40MHZ_FORBIDDEN: 11n channel with a width of 40Mhz is forbidden
 *	for this regulatory domain (valid only in 5Ghz).
 * @REG_CAPA_V1_DC_HIGH_ENABLED: DC HIGH allowed.
 * @REG_CAPA_V1_11AX_DISABLED: 11ax is forbidden for this regulatory domain.
 */
enum iwl_reg_capa_flags_v1 {
	REG_CAPA_V1_BF_CCD_LOW_BAND	= BIT(0),
	REG_CAPA_V1_BF_CCD_HIGH_BAND	= BIT(1),
	REG_CAPA_V1_160MHZ_ALLOWED	= BIT(2),
	REG_CAPA_V1_80MHZ_ALLOWED	= BIT(3),
	REG_CAPA_V1_MCS_8_ALLOWED	= BIT(4),
	REG_CAPA_V1_MCS_9_ALLOWED	= BIT(5),
	REG_CAPA_V1_40MHZ_FORBIDDEN	= BIT(7),
	REG_CAPA_V1_DC_HIGH_ENABLED	= BIT(9),
	REG_CAPA_V1_11AX_DISABLED	= BIT(10),
}; /* GEO_CHANNEL_CAPABILITIES_API_S_VER_1 */

/**
 * enum iwl_reg_capa_flags_v2 - global flags applied for the whole regulatory
 * domain (version 2).
 * @REG_CAPA_V2_STRADDLE_DISABLED: Straddle channels (144, 142, 138) are
 *	disabled.
 * @REG_CAPA_V2_BF_CCD_LOW_BAND: Beam-forming or Cyclic Delay Diversity in the
 *	2.4Ghz band is allowed.
 * @REG_CAPA_V2_BF_CCD_HIGH_BAND: Beam-forming or Cyclic Delay Diversity in the
 *	5Ghz band is allowed.
 * @REG_CAPA_V2_160MHZ_ALLOWED: 11ac channel with a width of 160Mhz is allowed
 *	for this regulatory domain (valid only in 5Ghz).
 * @REG_CAPA_V2_80MHZ_ALLOWED: 11ac channel with a width of 80Mhz is allowed
 *	for this regulatory domain (valid only in 5Ghz).
 * @REG_CAPA_V2_MCS_8_ALLOWED: 11ac with MCS 8 is allowed.
 * @REG_CAPA_V2_MCS_9_ALLOWED: 11ac with MCS 9 is allowed.
 * @REG_CAPA_V2_WEATHER_DISABLED: Weather radar channels (120, 124, 128, 118,
 *	126, 122) are disabled.
 * @REG_CAPA_V2_40MHZ_ALLOWED: 11n channel with a width of 40Mhz is allowed
 *	for this regulatory domain (uvalid only in 5Ghz).
 * @REG_CAPA_V2_11AX_DISABLED: 11ax is forbidden for this regulatory domain.
 */
enum iwl_reg_capa_flags_v2 {
	REG_CAPA_V2_STRADDLE_DISABLED	= BIT(0),
	REG_CAPA_V2_BF_CCD_LOW_BAND	= BIT(1),
	REG_CAPA_V2_BF_CCD_HIGH_BAND	= BIT(2),
	REG_CAPA_V2_160MHZ_ALLOWED	= BIT(3),
	REG_CAPA_V2_80MHZ_ALLOWED	= BIT(4),
	REG_CAPA_V2_MCS_8_ALLOWED	= BIT(5),
	REG_CAPA_V2_MCS_9_ALLOWED	= BIT(6),
	REG_CAPA_V2_WEATHER_DISABLED	= BIT(7),
	REG_CAPA_V2_40MHZ_ALLOWED	= BIT(8),
	REG_CAPA_V2_11AX_DISABLED	= BIT(10),
}; /* GEO_CHANNEL_CAPABILITIES_API_S_VER_2 */

/**
 * enum iwl_reg_capa_flags_v4 - global flags applied for the whole regulatory
 * domain.
 * @REG_CAPA_V4_160MHZ_ALLOWED: 11ac channel with a width of 160Mhz is allowed
 *	for this regulatory domain (valid only in 5Ghz).
 * @REG_CAPA_V4_80MHZ_ALLOWED: 11ac channel with a width of 80Mhz is allowed
 *	for this regulatory domain (valid only in 5Ghz).
 * @REG_CAPA_V4_MCS_12_ALLOWED: 11ac with MCS 12 is allowed.
 * @REG_CAPA_V4_MCS_13_ALLOWED: 11ac with MCS 13 is allowed.
 * @REG_CAPA_V4_11BE_DISABLED: 11be is forbidden for this regulatory domain.
 * @REG_CAPA_V4_11AX_DISABLED: 11ax is forbidden for this regulatory domain.
 * @REG_CAPA_V4_320MHZ_ALLOWED: 11be channel with a width of 320Mhz is allowed
 *	for this regulatory domain (valid only in 5GHz).
 */
enum iwl_reg_capa_flags_v4 {
	REG_CAPA_V4_160MHZ_ALLOWED		= BIT(3),
	REG_CAPA_V4_80MHZ_ALLOWED		= BIT(4),
	REG_CAPA_V4_MCS_12_ALLOWED		= BIT(5),
	REG_CAPA_V4_MCS_13_ALLOWED		= BIT(6),
	REG_CAPA_V4_11BE_DISABLED		= BIT(8),
	REG_CAPA_V4_11AX_DISABLED		= BIT(13),
	REG_CAPA_V4_320MHZ_ALLOWED		= BIT(16),
}; /* GEO_CHANNEL_CAPABILITIES_API_S_VER_4 */

/*
* API v2 for reg_capa_flags is relevant from version 6 and onwards of the
* MCC update command response.
*/
#define REG_CAPA_V2_RESP_VER	6

/* API v4 for reg_capa_flags is relevant from version 8 and onwards of the
 * MCC update command response.
 */
#define REG_CAPA_V4_RESP_VER	8

/**
 * struct iwl_reg_capa - struct for global regulatory capabilities, Used for
 * handling the different APIs of reg_capa_flags.
 *
 * @allow_40mhz: 11n channel with a width of 40Mhz is allowed
 *	for this regulatory domain.
 * @allow_80mhz: 11ac channel with a width of 80Mhz is allowed
 *	for this regulatory domain (valid only in 5 and 6 Ghz).
 * @allow_160mhz: 11ac channel with a width of 160Mhz is allowed
 *	for this regulatory domain (valid only in 5 and 6 Ghz).
 * @allow_320mhz: 11be channel with a width of 320Mhz is allowed
 *	for this regulatory domain (valid only in 6 Ghz).
 * @disable_11ax: 11ax is forbidden for this regulatory domain.
 * @disable_11be: 11be is forbidden for this regulatory domain.
 */
struct iwl_reg_capa {
	bool allow_40mhz;
	bool allow_80mhz;
	bool allow_160mhz;
	bool allow_320mhz;
	bool disable_11ax;
	bool disable_11be;
};

static inline void iwl_nvm_print_channel_flags(struct device *dev, u32 level,
					       int chan, u32 flags)
{
#define CHECK_AND_PRINT_I(x)	\
	((flags & NVM_CHANNEL_##x) ? " " #x : "")

	if (!(flags & NVM_CHANNEL_VALID)) {
		IWL_DEBUG_DEV(dev, level, "Ch. %d: 0x%x: No traffic\n",
			      chan, flags);
		return;
	}

	/* Note: already can print up to 101 characters, 110 is the limit! */
	IWL_DEBUG_DEV(dev, level,
		      "Ch. %d: 0x%x:%s%s%s%s%s%s%s%s%s%s%s%s\n",
		      chan, flags,
		      CHECK_AND_PRINT_I(VALID),
		      CHECK_AND_PRINT_I(IBSS),
		      CHECK_AND_PRINT_I(ACTIVE),
		      CHECK_AND_PRINT_I(RADAR),
		      CHECK_AND_PRINT_I(INDOOR_ONLY),
		      CHECK_AND_PRINT_I(GO_CONCURRENT),
		      CHECK_AND_PRINT_I(UNIFORM),
		      CHECK_AND_PRINT_I(20MHZ),
		      CHECK_AND_PRINT_I(40MHZ),
		      CHECK_AND_PRINT_I(80MHZ),
		      CHECK_AND_PRINT_I(160MHZ),
		      CHECK_AND_PRINT_I(DC_HIGH));
#undef CHECK_AND_PRINT_I
}

static u32 iwl_get_channel_flags(u8 ch_num, int ch_idx, enum nl80211_band band,
				 u32 nvm_flags, const struct iwl_cfg *cfg)
{
	u32 flags = IEEE80211_CHAN_NO_HT40;

	if (band == NL80211_BAND_2GHZ && (nvm_flags & NVM_CHANNEL_40MHZ)) {
		if (ch_num <= LAST_2GHZ_HT_PLUS)
			flags &= ~IEEE80211_CHAN_NO_HT40PLUS;
		if (ch_num >= FIRST_2GHZ_HT_MINUS)
			flags &= ~IEEE80211_CHAN_NO_HT40MINUS;
	} else if (nvm_flags & NVM_CHANNEL_40MHZ) {
		if ((ch_idx - NUM_2GHZ_CHANNELS) % 2 == 0)
			flags &= ~IEEE80211_CHAN_NO_HT40PLUS;
		else
			flags &= ~IEEE80211_CHAN_NO_HT40MINUS;
	}
	if (!(nvm_flags & NVM_CHANNEL_80MHZ))
		flags |= IEEE80211_CHAN_NO_80MHZ;
	if (!(nvm_flags & NVM_CHANNEL_160MHZ))
		flags |= IEEE80211_CHAN_NO_160MHZ;

	if (!(nvm_flags & NVM_CHANNEL_IBSS))
		flags |= IEEE80211_CHAN_NO_IR;

	if (!(nvm_flags & NVM_CHANNEL_ACTIVE))
		flags |= IEEE80211_CHAN_NO_IR;

	if (nvm_flags & NVM_CHANNEL_RADAR)
		flags |= IEEE80211_CHAN_RADAR;

	if (nvm_flags & NVM_CHANNEL_INDOOR_ONLY)
		flags |= IEEE80211_CHAN_INDOOR_ONLY;

	/* Set the GO concurrent flag only in case that NO_IR is set.
	 * Otherwise it is meaningless
	 */
	if ((nvm_flags & NVM_CHANNEL_GO_CONCURRENT) &&
	    (flags & IEEE80211_CHAN_NO_IR))
		flags |= IEEE80211_CHAN_IR_CONCURRENT;

	return flags;
}

static enum nl80211_band iwl_nl80211_band_from_channel_idx(int ch_idx)
{
	if (ch_idx >= NUM_2GHZ_CHANNELS + NUM_5GHZ_CHANNELS) {
		return NL80211_BAND_6GHZ;
	}

	if (ch_idx >= NUM_2GHZ_CHANNELS)
		return NL80211_BAND_5GHZ;
	return NL80211_BAND_2GHZ;
}

static int iwl_init_channel_map(struct device *dev, const struct iwl_cfg *cfg,
				struct iwl_nvm_data *data,
				const void * const nvm_ch_flags,
				u32 sbands_flags, bool v4)
{
	int ch_idx;
	int n_channels = 0;
	struct ieee80211_channel *channel;
	u32 ch_flags;
	int num_of_ch;
	const u16 *nvm_chan;

	if (cfg->uhb_supported) {
		num_of_ch = IWL_NVM_NUM_CHANNELS_UHB;
		nvm_chan = iwl_uhb_nvm_channels;
	} else if (cfg->nvm_type == IWL_NVM_EXT) {
		num_of_ch = IWL_NVM_NUM_CHANNELS_EXT;
		nvm_chan = iwl_ext_nvm_channels;
	} else {
		num_of_ch = IWL_NVM_NUM_CHANNELS;
		nvm_chan = iwl_nvm_channels;
	}

	for (ch_idx = 0; ch_idx < num_of_ch; ch_idx++) {
		enum nl80211_band band =
			iwl_nl80211_band_from_channel_idx(ch_idx);

		if (v4)
			ch_flags =
				__le32_to_cpup((const __le32 *)nvm_ch_flags + ch_idx);
		else
			ch_flags =
				__le16_to_cpup((const __le16 *)nvm_ch_flags + ch_idx);

		if (band == NL80211_BAND_5GHZ &&
		    !data->sku_cap_band_52ghz_enable)
			continue;

		/* workaround to disable wide channels in 5GHz */
		if ((sbands_flags & IWL_NVM_SBANDS_FLAGS_NO_WIDE_IN_5GHZ) &&
		    band == NL80211_BAND_5GHZ) {
			ch_flags &= ~(NVM_CHANNEL_40MHZ |
				     NVM_CHANNEL_80MHZ |
				     NVM_CHANNEL_160MHZ);
		}

		if (ch_flags & NVM_CHANNEL_160MHZ)
			data->vht160_supported = true;

		if (!(sbands_flags & IWL_NVM_SBANDS_FLAGS_LAR) &&
		    !(ch_flags & NVM_CHANNEL_VALID)) {
			/*
			 * Channels might become valid later if lar is
			 * supported, hence we still want to add them to
			 * the list of supported channels to cfg80211.
			 */
			iwl_nvm_print_channel_flags(dev, IWL_DL_EEPROM,
						    nvm_chan[ch_idx], ch_flags);
			continue;
		}

		channel = &data->channels[n_channels];
		n_channels++;

		channel->hw_value = nvm_chan[ch_idx];
		channel->band = band;
		channel->center_freq =
			ieee80211_channel_to_frequency(
				channel->hw_value, channel->band);

		/* Initialize regulatory-based run-time data */

		/*
		 * Default value - highest tx power value.  max_power
		 * is not used in mvm, and is used for backwards compatibility
		 */
		channel->max_power = IWL_DEFAULT_MAX_TX_POWER;

		/* don't put limitations in case we're using LAR */
		if (!(sbands_flags & IWL_NVM_SBANDS_FLAGS_LAR))
			channel->flags = iwl_get_channel_flags(nvm_chan[ch_idx],
							       ch_idx, band,
							       ch_flags, cfg);
		else
			channel->flags = 0;

		/* TODO: Don't put limitations on UHB devices as we still don't
		 * have NVM for them
		 */
		if (cfg->uhb_supported)
			channel->flags = 0;
		iwl_nvm_print_channel_flags(dev, IWL_DL_EEPROM,
					    channel->hw_value, ch_flags);
		IWL_DEBUG_EEPROM(dev, "Ch. %d: %ddBm\n",
				 channel->hw_value, channel->max_power);
	}

	return n_channels;
}

static void iwl_init_vht_hw_capab(struct iwl_trans *trans,
				  struct iwl_nvm_data *data,
				  struct ieee80211_sta_vht_cap *vht_cap,
				  u8 tx_chains, u8 rx_chains)
{
	const struct iwl_cfg *cfg = trans->cfg;
	int num_rx_ants = num_of_ant(rx_chains);
	int num_tx_ants = num_of_ant(tx_chains);

	vht_cap->vht_supported = true;

	vht_cap->cap = IEEE80211_VHT_CAP_SHORT_GI_80 |
		       IEEE80211_VHT_CAP_RXSTBC_1 |
		       IEEE80211_VHT_CAP_SU_BEAMFORMEE_CAPABLE |
		       3 << IEEE80211_VHT_CAP_BEAMFORMEE_STS_SHIFT |
		       IEEE80211_VHT_MAX_AMPDU_1024K <<
		       IEEE80211_VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_SHIFT;

	if (!trans->cfg->ht_params->stbc)
		vht_cap->cap &= ~IEEE80211_VHT_CAP_RXSTBC_MASK;

	if (data->vht160_supported)
		vht_cap->cap |= IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_160MHZ |
				IEEE80211_VHT_CAP_SHORT_GI_160;

	if (cfg->vht_mu_mimo_supported)
		vht_cap->cap |= IEEE80211_VHT_CAP_MU_BEAMFORMEE_CAPABLE;

	if (cfg->ht_params->ldpc)
		vht_cap->cap |= IEEE80211_VHT_CAP_RXLDPC;

	if (data->sku_cap_mimo_disabled) {
		num_rx_ants = 1;
		num_tx_ants = 1;
	}

	if (trans->cfg->ht_params->stbc && num_tx_ants > 1)
		vht_cap->cap |= IEEE80211_VHT_CAP_TXSTBC;
	else
		vht_cap->cap |= IEEE80211_VHT_CAP_TX_ANTENNA_PATTERN;

	switch (iwlwifi_mod_params.amsdu_size) {
	case IWL_AMSDU_DEF:
		if (trans->trans_cfg->mq_rx_supported)
			vht_cap->cap |=
				IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_11454;
		else
			vht_cap->cap |= IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_3895;
		break;
	case IWL_AMSDU_2K:
		if (trans->trans_cfg->mq_rx_supported)
			vht_cap->cap |=
				IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_11454;
		else
			WARN(1, "RB size of 2K is not supported by this device\n");
		break;
	case IWL_AMSDU_4K:
		vht_cap->cap |= IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_3895;
		break;
	case IWL_AMSDU_8K:
		vht_cap->cap |= IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_7991;
		break;
	case IWL_AMSDU_12K:
		vht_cap->cap |= IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_11454;
		break;
	default:
		break;
	}

	vht_cap->vht_mcs.rx_mcs_map =
		cpu_to_le16(IEEE80211_VHT_MCS_SUPPORT_0_9 << 0 |
			    IEEE80211_VHT_MCS_SUPPORT_0_9 << 2 |
			    IEEE80211_VHT_MCS_NOT_SUPPORTED << 4 |
			    IEEE80211_VHT_MCS_NOT_SUPPORTED << 6 |
			    IEEE80211_VHT_MCS_NOT_SUPPORTED << 8 |
			    IEEE80211_VHT_MCS_NOT_SUPPORTED << 10 |
			    IEEE80211_VHT_MCS_NOT_SUPPORTED << 12 |
			    IEEE80211_VHT_MCS_NOT_SUPPORTED << 14);

	if (num_rx_ants == 1 || cfg->rx_with_siso_diversity) {
		vht_cap->cap |= IEEE80211_VHT_CAP_RX_ANTENNA_PATTERN;
		/* this works because NOT_SUPPORTED == 3 */
		vht_cap->vht_mcs.rx_mcs_map |=
			cpu_to_le16(IEEE80211_VHT_MCS_NOT_SUPPORTED << 2);
	}

	vht_cap->vht_mcs.tx_mcs_map = vht_cap->vht_mcs.rx_mcs_map;

	vht_cap->vht_mcs.tx_highest |=
		cpu_to_le16(IEEE80211_VHT_EXT_NSS_BW_CAPABLE);
}

static const u8 iwl_vendor_caps[] = {
	0xdd,			/* vendor element */
	0x06,			/* length */
	0x00, 0x17, 0x35,	/* Intel OUI */
	0x08,			/* type (Intel Capabilities) */
	/* followed by 16 bits of capabilities */
#define IWL_VENDOR_CAP_IMPROVED_BF_FDBK_HE	BIT(0)
	IWL_VENDOR_CAP_IMPROVED_BF_FDBK_HE,
	0x00
};

static const struct ieee80211_sband_iftype_data iwl_he_eht_capa[] = {
	{
		.types_mask = BIT(NL80211_IFTYPE_STATION),
		.he_cap = {
			.has_he = true,
			.he_cap_elem = {
				.mac_cap_info[0] =
					IEEE80211_HE_MAC_CAP0_HTC_HE,
				.mac_cap_info[1] =
					IEEE80211_HE_MAC_CAP1_TF_MAC_PAD_DUR_16US |
					IEEE80211_HE_MAC_CAP1_MULTI_TID_AGG_RX_QOS_8,
				.mac_cap_info[2] =
					IEEE80211_HE_MAC_CAP2_32BIT_BA_BITMAP,
				.mac_cap_info[3] =
					IEEE80211_HE_MAC_CAP3_OMI_CONTROL |
					IEEE80211_HE_MAC_CAP3_RX_CTRL_FRAME_TO_MULTIBSS,
				.mac_cap_info[4] =
					IEEE80211_HE_MAC_CAP4_AMSDU_IN_AMPDU |
					IEEE80211_HE_MAC_CAP4_MULTI_TID_AGG_TX_QOS_B39,
				.mac_cap_info[5] =
					IEEE80211_HE_MAC_CAP5_MULTI_TID_AGG_TX_QOS_B40 |
					IEEE80211_HE_MAC_CAP5_MULTI_TID_AGG_TX_QOS_B41 |
					IEEE80211_HE_MAC_CAP5_UL_2x996_TONE_RU |
					IEEE80211_HE_MAC_CAP5_HE_DYNAMIC_SM_PS |
					IEEE80211_HE_MAC_CAP5_HT_VHT_TRIG_FRAME_RX,
				.phy_cap_info[1] =
					IEEE80211_HE_PHY_CAP1_PREAMBLE_PUNC_RX_MASK |
					IEEE80211_HE_PHY_CAP1_DEVICE_CLASS_A |
					IEEE80211_HE_PHY_CAP1_LDPC_CODING_IN_PAYLOAD,
				.phy_cap_info[2] =
					IEEE80211_HE_PHY_CAP2_NDP_4x_LTF_AND_3_2US |
					IEEE80211_HE_PHY_CAP2_STBC_RX_UNDER_80MHZ,
				.phy_cap_info[3] =
					IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_TX_BPSK |
					IEEE80211_HE_PHY_CAP3_DCM_MAX_TX_NSS_1 |
					IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_RX_BPSK |
					IEEE80211_HE_PHY_CAP3_DCM_MAX_RX_NSS_1,
				.phy_cap_info[4] =
					IEEE80211_HE_PHY_CAP4_SU_BEAMFORMEE |
					IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_ABOVE_80MHZ_8 |
					IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_UNDER_80MHZ_8,
				.phy_cap_info[6] =
					IEEE80211_HE_PHY_CAP6_TRIG_SU_BEAMFORMING_FB |
					IEEE80211_HE_PHY_CAP6_TRIG_MU_BEAMFORMING_PARTIAL_BW_FB |
					IEEE80211_HE_PHY_CAP6_PPE_THRESHOLD_PRESENT,
				.phy_cap_info[7] =
					IEEE80211_HE_PHY_CAP7_POWER_BOOST_FACTOR_SUPP |
					IEEE80211_HE_PHY_CAP7_HE_SU_MU_PPDU_4XLTF_AND_08_US_GI,
				.phy_cap_info[8] =
					IEEE80211_HE_PHY_CAP8_HE_ER_SU_PPDU_4XLTF_AND_08_US_GI |
					IEEE80211_HE_PHY_CAP8_20MHZ_IN_40MHZ_HE_PPDU_IN_2G |
					IEEE80211_HE_PHY_CAP8_20MHZ_IN_160MHZ_HE_PPDU |
					IEEE80211_HE_PHY_CAP8_80MHZ_IN_160MHZ_HE_PPDU |
					IEEE80211_HE_PHY_CAP8_DCM_MAX_RU_242,
				.phy_cap_info[9] =
					IEEE80211_HE_PHY_CAP9_RX_FULL_BW_SU_USING_MU_WITH_COMP_SIGB |
					IEEE80211_HE_PHY_CAP9_RX_FULL_BW_SU_USING_MU_WITH_NON_COMP_SIGB |
					(IEEE80211_HE_PHY_CAP9_NOMINAL_PKT_PADDING_RESERVED <<
					IEEE80211_HE_PHY_CAP9_NOMINAL_PKT_PADDING_POS),
				.phy_cap_info[10] =
					IEEE80211_HE_PHY_CAP10_HE_MU_M1RU_MAX_LTF,
			},
			/*
			 * Set default Tx/Rx HE MCS NSS Support field.
			 * Indicate support for up to 2 spatial streams and all
			 * MCS, without any special cases
			 */
			.he_mcs_nss_supp = {
				.rx_mcs_80 = cpu_to_le16(0xfffa),
				.tx_mcs_80 = cpu_to_le16(0xfffa),
				.rx_mcs_160 = cpu_to_le16(0xfffa),
				.tx_mcs_160 = cpu_to_le16(0xfffa),
				.rx_mcs_80p80 = cpu_to_le16(0xffff),
				.tx_mcs_80p80 = cpu_to_le16(0xffff),
			},
			/*
			 * Set default PPE thresholds, with PPET16 set to 0,
			 * PPET8 set to 7
			 */
			.ppe_thres = {0x61, 0x1c, 0xc7, 0x71},
		},
		.eht_cap = {
			.has_eht = true,
			.eht_cap_elem = {
				.mac_cap_info[0] =
					IEEE80211_EHT_MAC_CAP0_EPCS_PRIO_ACCESS |
					IEEE80211_EHT_MAC_CAP0_OM_CONTROL |
					IEEE80211_EHT_MAC_CAP0_TRIG_TXOP_SHARING_MODE1 |
					IEEE80211_EHT_MAC_CAP0_TRIG_TXOP_SHARING_MODE2 |
					IEEE80211_EHT_MAC_CAP0_SCS_TRAFFIC_DESC,
				.phy_cap_info[0] =
					IEEE80211_EHT_PHY_CAP0_242_TONE_RU_GT20MHZ |
					IEEE80211_EHT_PHY_CAP0_NDP_4_EHT_LFT_32_GI |
					IEEE80211_EHT_PHY_CAP0_PARTIAL_BW_UL_MU_MIMO |
					IEEE80211_EHT_PHY_CAP0_SU_BEAMFORMEE |
					IEEE80211_EHT_PHY_CAP0_BEAMFORMEE_SS_80MHZ_MASK,
				.phy_cap_info[1] =
					IEEE80211_EHT_PHY_CAP1_BEAMFORMEE_SS_80MHZ_MASK  |
					IEEE80211_EHT_PHY_CAP1_BEAMFORMEE_SS_160MHZ_MASK,
				.phy_cap_info[3] =
					IEEE80211_EHT_PHY_CAP3_NG_16_SU_FEEDBACK |
					IEEE80211_EHT_PHY_CAP3_NG_16_MU_FEEDBACK |
					IEEE80211_EHT_PHY_CAP3_CODEBOOK_4_2_SU_FDBK |
					IEEE80211_EHT_PHY_CAP3_CODEBOOK_7_5_MU_FDBK |
					IEEE80211_EHT_PHY_CAP3_TRIG_SU_BF_FDBK |
					IEEE80211_EHT_PHY_CAP3_TRIG_MU_BF_PART_BW_FDBK |
					IEEE80211_EHT_PHY_CAP3_TRIG_CQI_FDBK,

				.phy_cap_info[4] =
					IEEE80211_EHT_PHY_CAP4_PART_BW_DL_MU_MIMO |
					IEEE80211_EHT_PHY_CAP4_POWER_BOOST_FACT_SUPP |
					IEEE80211_EHT_PHY_CAP4_EHT_MU_PPDU_4_EHT_LTF_08_GI,
				.phy_cap_info[5] =
					IEEE80211_EHT_PHY_CAP5_NON_TRIG_CQI_FEEDBACK |
					IEEE80211_EHT_PHY_CAP5_TX_LESS_242_TONE_RU_SUPP |
					IEEE80211_EHT_PHY_CAP5_RX_LESS_242_TONE_RU_SUPP |
					IEEE80211_EHT_PHY_CAP5_PPE_THRESHOLD_PRESENT,
				.phy_cap_info[6] =
					IEEE80211_EHT_PHY_CAP6_MCS15_SUPP_MASK |
					IEEE80211_EHT_PHY_CAP6_EHT_DUP_6GHZ_SUPP,
				.phy_cap_info[8] =
					IEEE80211_EHT_PHY_CAP8_RX_1024QAM_WIDER_BW_DL_OFDMA |
					IEEE80211_EHT_PHY_CAP8_RX_4096QAM_WIDER_BW_DL_OFDMA,
			},

			/* For all MCS and bandwidth, set 2 NSS for both Tx and
			 * Rx - note we don't set the only_20mhz, but due to this
			 * being a union, it gets set correctly anyway.
			 */
			.eht_mcs_nss_supp = {
				.bw._80 = {
					.rx_tx_mcs9_max_nss = 0x22,
					.rx_tx_mcs11_max_nss = 0x22,
					.rx_tx_mcs13_max_nss = 0x22,
				},
				.bw._160 = {
					.rx_tx_mcs9_max_nss = 0x22,
					.rx_tx_mcs11_max_nss = 0x22,
					.rx_tx_mcs13_max_nss = 0x22,
				},
				.bw._320 = {
					.rx_tx_mcs9_max_nss = 0x22,
					.rx_tx_mcs11_max_nss = 0x22,
					.rx_tx_mcs13_max_nss = 0x22,
				},
			},

			/*
			 * PPE thresholds for NSS = 2, and RU index bitmap set
			 * to 0xc.
			 */
			.eht_ppe_thres = {0xc1, 0x0e, 0xe0 }
		},
	},
	{
		.types_mask = BIT(NL80211_IFTYPE_AP),
		.he_cap = {
			.has_he = true,
			.he_cap_elem = {
				.mac_cap_info[0] =
					IEEE80211_HE_MAC_CAP0_HTC_HE,
				.mac_cap_info[1] =
					IEEE80211_HE_MAC_CAP1_TF_MAC_PAD_DUR_16US |
					IEEE80211_HE_MAC_CAP1_MULTI_TID_AGG_RX_QOS_8,
				.mac_cap_info[3] =
					IEEE80211_HE_MAC_CAP3_OMI_CONTROL,
				.phy_cap_info[1] =
					IEEE80211_HE_PHY_CAP1_LDPC_CODING_IN_PAYLOAD,
				.phy_cap_info[2] =
					IEEE80211_HE_PHY_CAP2_STBC_RX_UNDER_80MHZ |
					IEEE80211_HE_PHY_CAP2_NDP_4x_LTF_AND_3_2US,
				.phy_cap_info[3] =
					IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_TX_BPSK |
					IEEE80211_HE_PHY_CAP3_DCM_MAX_TX_NSS_1 |
					IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_RX_BPSK |
					IEEE80211_HE_PHY_CAP3_DCM_MAX_RX_NSS_1,
				.phy_cap_info[6] =
					IEEE80211_HE_PHY_CAP6_PPE_THRESHOLD_PRESENT,
				.phy_cap_info[7] =
					IEEE80211_HE_PHY_CAP7_HE_SU_MU_PPDU_4XLTF_AND_08_US_GI,
				.phy_cap_info[8] =
					IEEE80211_HE_PHY_CAP8_HE_ER_SU_PPDU_4XLTF_AND_08_US_GI |
					IEEE80211_HE_PHY_CAP8_DCM_MAX_RU_242,
				.phy_cap_info[9] =
					IEEE80211_HE_PHY_CAP9_NOMINAL_PKT_PADDING_RESERVED
					<< IEEE80211_HE_PHY_CAP9_NOMINAL_PKT_PADDING_POS,
			},
			/*
			 * Set default Tx/Rx HE MCS NSS Support field.
			 * Indicate support for up to 2 spatial streams and all
			 * MCS, without any special cases
			 */
			.he_mcs_nss_supp = {
				.rx_mcs_80 = cpu_to_le16(0xfffa),
				.tx_mcs_80 = cpu_to_le16(0xfffa),
				.rx_mcs_160 = cpu_to_le16(0xfffa),
				.tx_mcs_160 = cpu_to_le16(0xfffa),
				.rx_mcs_80p80 = cpu_to_le16(0xffff),
				.tx_mcs_80p80 = cpu_to_le16(0xffff),
			},
			/*
			 * Set default PPE thresholds, with PPET16 set to 0,
			 * PPET8 set to 7
			 */
			.ppe_thres = {0x61, 0x1c, 0xc7, 0x71},
		},
		.eht_cap = {
			.has_eht = true,
			.eht_cap_elem = {
				.mac_cap_info[0] =
					IEEE80211_EHT_MAC_CAP0_EPCS_PRIO_ACCESS |
					IEEE80211_EHT_MAC_CAP0_OM_CONTROL |
					IEEE80211_EHT_MAC_CAP0_TRIG_TXOP_SHARING_MODE1 |
					IEEE80211_EHT_MAC_CAP0_TRIG_TXOP_SHARING_MODE2,
				.phy_cap_info[0] =
					IEEE80211_EHT_PHY_CAP0_242_TONE_RU_GT20MHZ |
					IEEE80211_EHT_PHY_CAP0_NDP_4_EHT_LFT_32_GI,
				.phy_cap_info[5] =
					IEEE80211_EHT_PHY_CAP5_PPE_THRESHOLD_PRESENT,
			},

			/* For all MCS and bandwidth, set 2 NSS for both Tx and
			 * Rx - note we don't set the only_20mhz, but due to this
			 * being a union, it gets set correctly anyway.
			 */
			.eht_mcs_nss_supp = {
				.bw._80 = {
					.rx_tx_mcs9_max_nss = 0x22,
					.rx_tx_mcs11_max_nss = 0x22,
					.rx_tx_mcs13_max_nss = 0x22,
				},
				.bw._160 = {
					.rx_tx_mcs9_max_nss = 0x22,
					.rx_tx_mcs11_max_nss = 0x22,
					.rx_tx_mcs13_max_nss = 0x22,
				},
				.bw._320 = {
					.rx_tx_mcs9_max_nss = 0x22,
					.rx_tx_mcs11_max_nss = 0x22,
					.rx_tx_mcs13_max_nss = 0x22,
				},
			},

			/*
			 * PPE thresholds for NSS = 2, and RU index bitmap set
			 * to 0xc.
			 */
			.eht_ppe_thres = {0xc1, 0x0e, 0xe0 }
		},
	},
};

static void iwl_init_he_6ghz_capa(struct iwl_trans *trans,
				  struct iwl_nvm_data *data,
				  struct ieee80211_supported_band *sband,
				  u8 tx_chains, u8 rx_chains)
{
	struct ieee80211_sta_ht_cap ht_cap;
	struct ieee80211_sta_vht_cap vht_cap = {};
	struct ieee80211_sband_iftype_data *iftype_data;
	u16 he_6ghz_capa = 0;
	u32 exp;
	int i;

	if (sband->band != NL80211_BAND_6GHZ)
		return;

	/* grab HT/VHT capabilities and calculate HE 6 GHz capabilities */
	iwl_init_ht_hw_capab(trans, data, &ht_cap, NL80211_BAND_5GHZ,
			     tx_chains, rx_chains);
	WARN_ON(!ht_cap.ht_supported);
	iwl_init_vht_hw_capab(trans, data, &vht_cap, tx_chains, rx_chains);
	WARN_ON(!vht_cap.vht_supported);

	he_6ghz_capa |=
		u16_encode_bits(ht_cap.ampdu_density,
				IEEE80211_HE_6GHZ_CAP_MIN_MPDU_START);
	exp = u32_get_bits(vht_cap.cap,
			   IEEE80211_VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_MASK);
	he_6ghz_capa |=
		u16_encode_bits(exp, IEEE80211_HE_6GHZ_CAP_MAX_AMPDU_LEN_EXP);
	exp = u32_get_bits(vht_cap.cap, IEEE80211_VHT_CAP_MAX_MPDU_MASK);
	he_6ghz_capa |=
		u16_encode_bits(exp, IEEE80211_HE_6GHZ_CAP_MAX_MPDU_LEN);
	/* we don't support extended_ht_cap_info anywhere, so no RD_RESPONDER */
	if (vht_cap.cap & IEEE80211_VHT_CAP_TX_ANTENNA_PATTERN)
		he_6ghz_capa |= IEEE80211_HE_6GHZ_CAP_TX_ANTPAT_CONS;
	if (vht_cap.cap & IEEE80211_VHT_CAP_RX_ANTENNA_PATTERN)
		he_6ghz_capa |= IEEE80211_HE_6GHZ_CAP_RX_ANTPAT_CONS;

	IWL_DEBUG_EEPROM(trans->dev, "he_6ghz_capa=0x%x\n", he_6ghz_capa);

	/* we know it's writable - we set it before ourselves */
	iftype_data = (void *)(uintptr_t)sband->iftype_data;
	for (i = 0; i < sband->n_iftype_data; i++)
		iftype_data[i].he_6ghz_capa.capa = cpu_to_le16(he_6ghz_capa);
}

static void
iwl_nvm_fixup_sband_iftd(struct iwl_trans *trans,
			 struct iwl_nvm_data *data,
			 struct ieee80211_supported_band *sband,
			 struct ieee80211_sband_iftype_data *iftype_data,
			 u8 tx_chains, u8 rx_chains,
			 const struct iwl_fw *fw)
{
	bool is_ap = iftype_data->types_mask & BIT(NL80211_IFTYPE_AP);
	bool no_320;

	no_320 = !trans->trans_cfg->integrated &&
		 trans->pcie_link_speed < PCI_EXP_LNKSTA_CLS_8_0GB;

	if (!data->sku_cap_11be_enable || iwlwifi_mod_params.disable_11be)
		iftype_data->eht_cap.has_eht = false;

	/* Advertise an A-MPDU exponent extension based on
	 * operating band
	 */
	if (sband->band == NL80211_BAND_6GHZ && iftype_data->eht_cap.has_eht)
		iftype_data->he_cap.he_cap_elem.mac_cap_info[3] |=
			IEEE80211_HE_MAC_CAP3_MAX_AMPDU_LEN_EXP_EXT_2;
	else if (sband->band != NL80211_BAND_2GHZ)
		iftype_data->he_cap.he_cap_elem.mac_cap_info[3] |=
			IEEE80211_HE_MAC_CAP3_MAX_AMPDU_LEN_EXP_EXT_1;
	else
		iftype_data->he_cap.he_cap_elem.mac_cap_info[3] |=
			IEEE80211_HE_MAC_CAP3_MAX_AMPDU_LEN_EXP_EXT_3;

	switch (sband->band) {
	case NL80211_BAND_2GHZ:
		iftype_data->he_cap.he_cap_elem.phy_cap_info[0] |=
			IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_IN_2G;
		iftype_data->eht_cap.eht_cap_elem.mac_cap_info[0] |=
			u8_encode_bits(IEEE80211_EHT_MAC_CAP0_MAX_MPDU_LEN_11454,
				       IEEE80211_EHT_MAC_CAP0_MAX_MPDU_LEN_MASK);
		break;
	case NL80211_BAND_6GHZ:
		if (!no_320) {
			iftype_data->eht_cap.eht_cap_elem.phy_cap_info[0] |=
				IEEE80211_EHT_PHY_CAP0_320MHZ_IN_6GHZ;
			iftype_data->eht_cap.eht_cap_elem.phy_cap_info[1] |=
				IEEE80211_EHT_PHY_CAP1_BEAMFORMEE_SS_320MHZ_MASK;
		}
		fallthrough;
	case NL80211_BAND_5GHZ:
		iftype_data->he_cap.he_cap_elem.phy_cap_info[0] |=
			IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_80MHZ_IN_5G |
			IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_160MHZ_IN_5G;
		break;
	default:
		WARN_ON(1);
		break;
	}

	if ((tx_chains & rx_chains) == ANT_AB) {
		iftype_data->he_cap.he_cap_elem.phy_cap_info[2] |=
			IEEE80211_HE_PHY_CAP2_STBC_TX_UNDER_80MHZ;
		iftype_data->he_cap.he_cap_elem.phy_cap_info[5] |=
			IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_UNDER_80MHZ_2 |
			IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_ABOVE_80MHZ_2;
		if (!is_ap) {
			iftype_data->he_cap.he_cap_elem.phy_cap_info[7] |=
				IEEE80211_HE_PHY_CAP7_MAX_NC_2;

			if (iftype_data->eht_cap.has_eht) {
				/*
				 * Set the number of sounding dimensions for each
				 * bandwidth to 1 to indicate the maximal supported
				 * value of TXVECTOR parameter NUM_STS of 2
				 */
				iftype_data->eht_cap.eht_cap_elem.phy_cap_info[2] |= 0x49;

				/*
				 * Set the MAX NC to 1 to indicate sounding feedback of
				 * 2 supported by the beamfomee.
				 */
				iftype_data->eht_cap.eht_cap_elem.phy_cap_info[4] |= 0x10;
			}
		}
	} else {
		struct ieee80211_he_mcs_nss_supp *he_mcs_nss_supp =
			&iftype_data->he_cap.he_mcs_nss_supp;

		if (iftype_data->eht_cap.has_eht) {
			struct ieee80211_eht_mcs_nss_supp *mcs_nss =
				&iftype_data->eht_cap.eht_mcs_nss_supp;

			memset(mcs_nss, 0x11, sizeof(*mcs_nss));
		}

		if (!is_ap) {
			/* If not 2x2, we need to indicate 1x1 in the
			 * Midamble RX Max NSTS - but not for AP mode
			 */
			iftype_data->he_cap.he_cap_elem.phy_cap_info[1] &=
				~IEEE80211_HE_PHY_CAP1_MIDAMBLE_RX_TX_MAX_NSTS;
			iftype_data->he_cap.he_cap_elem.phy_cap_info[2] &=
				~IEEE80211_HE_PHY_CAP2_MIDAMBLE_RX_TX_MAX_NSTS;
			iftype_data->he_cap.he_cap_elem.phy_cap_info[7] |=
				IEEE80211_HE_PHY_CAP7_MAX_NC_1;
		}

		he_mcs_nss_supp->rx_mcs_80 |=
			cpu_to_le16(IEEE80211_HE_MCS_NOT_SUPPORTED << 2);
		he_mcs_nss_supp->tx_mcs_80 |=
			cpu_to_le16(IEEE80211_HE_MCS_NOT_SUPPORTED << 2);
		he_mcs_nss_supp->rx_mcs_160 |=
			cpu_to_le16(IEEE80211_HE_MCS_NOT_SUPPORTED << 2);
		he_mcs_nss_supp->tx_mcs_160 |=
			cpu_to_le16(IEEE80211_HE_MCS_NOT_SUPPORTED << 2);
		he_mcs_nss_supp->rx_mcs_80p80 |=
			cpu_to_le16(IEEE80211_HE_MCS_NOT_SUPPORTED << 2);
		he_mcs_nss_supp->tx_mcs_80p80 |=
			cpu_to_le16(IEEE80211_HE_MCS_NOT_SUPPORTED << 2);
	}

	if (trans->trans_cfg->device_family >= IWL_DEVICE_FAMILY_AX210 && !is_ap)
		iftype_data->he_cap.he_cap_elem.phy_cap_info[2] |=
			IEEE80211_HE_PHY_CAP2_UL_MU_FULL_MU_MIMO;

	switch (CSR_HW_RFID_TYPE(trans->hw_rf_id)) {
	case IWL_CFG_RF_TYPE_GF:
	case IWL_CFG_RF_TYPE_MR:
	case IWL_CFG_RF_TYPE_MS:
	case IWL_CFG_RF_TYPE_FM:
	case IWL_CFG_RF_TYPE_WH:
		iftype_data->he_cap.he_cap_elem.phy_cap_info[9] |=
			IEEE80211_HE_PHY_CAP9_TX_1024_QAM_LESS_THAN_242_TONE_RU;
		if (!is_ap)
			iftype_data->he_cap.he_cap_elem.phy_cap_info[9] |=
				IEEE80211_HE_PHY_CAP9_RX_1024_QAM_LESS_THAN_242_TONE_RU;
		break;
	}

	if (CSR_HW_REV_TYPE(trans->hw_rev) == IWL_CFG_MAC_TYPE_GL &&
	    iftype_data->eht_cap.has_eht) {
		iftype_data->eht_cap.eht_cap_elem.mac_cap_info[0] &=
			~(IEEE80211_EHT_MAC_CAP0_EPCS_PRIO_ACCESS |
			  IEEE80211_EHT_MAC_CAP0_TRIG_TXOP_SHARING_MODE1 |
			  IEEE80211_EHT_MAC_CAP0_TRIG_TXOP_SHARING_MODE2);
		iftype_data->eht_cap.eht_cap_elem.phy_cap_info[3] &=
			~(IEEE80211_EHT_PHY_CAP0_PARTIAL_BW_UL_MU_MIMO |
			  IEEE80211_EHT_PHY_CAP3_NG_16_SU_FEEDBACK |
			  IEEE80211_EHT_PHY_CAP3_NG_16_MU_FEEDBACK |
			  IEEE80211_EHT_PHY_CAP3_CODEBOOK_4_2_SU_FDBK |
			  IEEE80211_EHT_PHY_CAP3_CODEBOOK_7_5_MU_FDBK |
			  IEEE80211_EHT_PHY_CAP3_TRIG_MU_BF_PART_BW_FDBK |
			  IEEE80211_EHT_PHY_CAP3_TRIG_CQI_FDBK);
		iftype_data->eht_cap.eht_cap_elem.phy_cap_info[4] &=
			~(IEEE80211_EHT_PHY_CAP4_PART_BW_DL_MU_MIMO |
			  IEEE80211_EHT_PHY_CAP4_POWER_BOOST_FACT_SUPP);
		iftype_data->eht_cap.eht_cap_elem.phy_cap_info[5] &=
			~IEEE80211_EHT_PHY_CAP5_NON_TRIG_CQI_FEEDBACK;
		iftype_data->eht_cap.eht_cap_elem.phy_cap_info[6] &=
			~(IEEE80211_EHT_PHY_CAP6_MCS15_SUPP_MASK |
			  IEEE80211_EHT_PHY_CAP6_EHT_DUP_6GHZ_SUPP);
		iftype_data->eht_cap.eht_cap_elem.phy_cap_info[5] |=
			IEEE80211_EHT_PHY_CAP5_SUPP_EXTRA_EHT_LTF;
	}

	if (fw_has_capa(&fw->ucode_capa, IWL_UCODE_TLV_CAPA_BROADCAST_TWT))
		iftype_data->he_cap.he_cap_elem.mac_cap_info[2] |=
			IEEE80211_HE_MAC_CAP2_BCAST_TWT;

	if (trans->trans_cfg->device_family == IWL_DEVICE_FAMILY_22000 &&
	    !is_ap) {
		iftype_data->vendor_elems.data = iwl_vendor_caps;
		iftype_data->vendor_elems.len = ARRAY_SIZE(iwl_vendor_caps);
	}

	if (!trans->cfg->ht_params->stbc) {
		iftype_data->he_cap.he_cap_elem.phy_cap_info[2] &=
			~IEEE80211_HE_PHY_CAP2_STBC_RX_UNDER_80MHZ;
		iftype_data->he_cap.he_cap_elem.phy_cap_info[7] &=
			~IEEE80211_HE_PHY_CAP7_STBC_RX_ABOVE_80MHZ;
	}
}

static void iwl_init_he_hw_capab(struct iwl_trans *trans,
				 struct iwl_nvm_data *data,
				 struct ieee80211_supported_band *sband,
				 u8 tx_chains, u8 rx_chains,
				 const struct iwl_fw *fw)
{
	struct ieee80211_sband_iftype_data *iftype_data;
	int i;

	BUILD_BUG_ON(sizeof(data->iftd.low) != sizeof(iwl_he_eht_capa));
	BUILD_BUG_ON(sizeof(data->iftd.high) != sizeof(iwl_he_eht_capa));
	BUILD_BUG_ON(sizeof(data->iftd.uhb) != sizeof(iwl_he_eht_capa));

	switch (sband->band) {
	case NL80211_BAND_2GHZ:
		iftype_data = data->iftd.low;
		break;
	case NL80211_BAND_5GHZ:
		iftype_data = data->iftd.high;
		break;
	case NL80211_BAND_6GHZ:
		iftype_data = data->iftd.uhb;
		break;
	default:
		WARN_ON(1);
		return;
	}

	memcpy(iftype_data, iwl_he_eht_capa, sizeof(iwl_he_eht_capa));

	_ieee80211_set_sband_iftype_data(sband, iftype_data,
					 ARRAY_SIZE(iwl_he_eht_capa));

	for (i = 0; i < sband->n_iftype_data; i++)
		iwl_nvm_fixup_sband_iftd(trans, data, sband, &iftype_data[i],
					 tx_chains, rx_chains, fw);

	iwl_init_he_6ghz_capa(trans, data, sband, tx_chains, rx_chains);
}

void iwl_reinit_cab(struct iwl_trans *trans, struct iwl_nvm_data *data,
		    u8 tx_chains, u8 rx_chains, const struct iwl_fw *fw)
{
	struct ieee80211_supported_band *sband;

	sband = &data->bands[NL80211_BAND_2GHZ];
	iwl_init_ht_hw_capab(trans, data, &sband->ht_cap, NL80211_BAND_2GHZ,
			     tx_chains, rx_chains);

	if (data->sku_cap_11ax_enable && !iwlwifi_mod_params.disable_11ax)
		iwl_init_he_hw_capab(trans, data, sband, tx_chains, rx_chains,
				     fw);

	sband = &data->bands[NL80211_BAND_5GHZ];
	iwl_init_ht_hw_capab(trans, data, &sband->ht_cap, NL80211_BAND_5GHZ,
			     tx_chains, rx_chains);
	if (data->sku_cap_11ac_enable && !iwlwifi_mod_params.disable_11ac)
		iwl_init_vht_hw_capab(trans, data, &sband->vht_cap,
				      tx_chains, rx_chains);

	if (data->sku_cap_11ax_enable && !iwlwifi_mod_params.disable_11ax)
		iwl_init_he_hw_capab(trans, data, sband, tx_chains, rx_chains,
				     fw);

	sband = &data->bands[NL80211_BAND_6GHZ];
	if (data->sku_cap_11ax_enable && !iwlwifi_mod_params.disable_11ax)
		iwl_init_he_hw_capab(trans, data, sband, tx_chains, rx_chains,
				     fw);
}
IWL_EXPORT_SYMBOL(iwl_reinit_cab);

static void iwl_init_sbands(struct iwl_trans *trans,
			    struct iwl_nvm_data *data,
			    const void *nvm_ch_flags, u8 tx_chains,
			    u8 rx_chains, u32 sbands_flags, bool v4,
			    const struct iwl_fw *fw)
{
	struct device *dev = trans->dev;
	const struct iwl_cfg *cfg = trans->cfg;
	int n_channels;
	int n_used = 0;
	struct ieee80211_supported_band *sband;

	n_channels = iwl_init_channel_map(dev, cfg, data, nvm_ch_flags,
					  sbands_flags, v4);
	sband = &data->bands[NL80211_BAND_2GHZ];
	sband->band = NL80211_BAND_2GHZ;
	sband->bitrates = &iwl_cfg80211_rates[RATES_24_OFFS];
	sband->n_bitrates = N_RATES_24;
	n_used += iwl_init_sband_channels(data, sband, n_channels,
					  NL80211_BAND_2GHZ);
	iwl_init_ht_hw_capab(trans, data, &sband->ht_cap, NL80211_BAND_2GHZ,
			     tx_chains, rx_chains);

	if (data->sku_cap_11ax_enable && !iwlwifi_mod_params.disable_11ax)
		iwl_init_he_hw_capab(trans, data, sband, tx_chains, rx_chains,
				     fw);

	sband = &data->bands[NL80211_BAND_5GHZ];
	sband->band = NL80211_BAND_5GHZ;
	sband->bitrates = &iwl_cfg80211_rates[RATES_52_OFFS];
	sband->n_bitrates = N_RATES_52;
	n_used += iwl_init_sband_channels(data, sband, n_channels,
					  NL80211_BAND_5GHZ);
	iwl_init_ht_hw_capab(trans, data, &sband->ht_cap, NL80211_BAND_5GHZ,
			     tx_chains, rx_chains);
	if (data->sku_cap_11ac_enable && !iwlwifi_mod_params.disable_11ac)
		iwl_init_vht_hw_capab(trans, data, &sband->vht_cap,
				      tx_chains, rx_chains);

	if (data->sku_cap_11ax_enable && !iwlwifi_mod_params.disable_11ax)
		iwl_init_he_hw_capab(trans, data, sband, tx_chains, rx_chains,
				     fw);

	/* 6GHz band. */
	sband = &data->bands[NL80211_BAND_6GHZ];
	sband->band = NL80211_BAND_6GHZ;
	/* use the same rates as 5GHz band */
	sband->bitrates = &iwl_cfg80211_rates[RATES_52_OFFS];
	sband->n_bitrates = N_RATES_52;
	n_used += iwl_init_sband_channels(data, sband, n_channels,
					  NL80211_BAND_6GHZ);

	if (data->sku_cap_11ax_enable && !iwlwifi_mod_params.disable_11ax)
		iwl_init_he_hw_capab(trans, data, sband, tx_chains, rx_chains,
				     fw);
	else
		sband->n_channels = 0;
	if (n_channels != n_used)
		IWL_ERR_DEV(dev, "NVM: used only %d of %d channels\n",
			    n_used, n_channels);
}

static int iwl_get_sku(const struct iwl_cfg *cfg, const __le16 *nvm_sw,
		       const __le16 *phy_sku)
{
	if (cfg->nvm_type != IWL_NVM_EXT)
		return le16_to_cpup(nvm_sw + SKU);

	return le32_to_cpup((const __le32 *)(phy_sku + SKU_FAMILY_8000));
}

static int iwl_get_nvm_version(const struct iwl_cfg *cfg, const __le16 *nvm_sw)
{
	if (cfg->nvm_type != IWL_NVM_EXT)
		return le16_to_cpup(nvm_sw + NVM_VERSION);
	else
		return le32_to_cpup((const __le32 *)(nvm_sw +
						     NVM_VERSION_EXT_NVM));
}

static int iwl_get_radio_cfg(const struct iwl_cfg *cfg, const __le16 *nvm_sw,
			     const __le16 *phy_sku)
{
	if (cfg->nvm_type != IWL_NVM_EXT)
		return le16_to_cpup(nvm_sw + RADIO_CFG);

	return le32_to_cpup((const __le32 *)(phy_sku + RADIO_CFG_FAMILY_EXT_NVM));

}

static int iwl_get_n_hw_addrs(const struct iwl_cfg *cfg, const __le16 *nvm_sw)
{
	int n_hw_addr;

	if (cfg->nvm_type != IWL_NVM_EXT)
		return le16_to_cpup(nvm_sw + N_HW_ADDRS);

	n_hw_addr = le32_to_cpup((const __le32 *)(nvm_sw + N_HW_ADDRS_FAMILY_8000));

	return n_hw_addr & N_HW_ADDR_MASK;
}

static void iwl_set_radio_cfg(const struct iwl_cfg *cfg,
			      struct iwl_nvm_data *data,
			      u32 radio_cfg)
{
	if (cfg->nvm_type != IWL_NVM_EXT) {
		data->radio_cfg_type = NVM_RF_CFG_TYPE_MSK(radio_cfg);
		data->radio_cfg_step = NVM_RF_CFG_STEP_MSK(radio_cfg);
		data->radio_cfg_dash = NVM_RF_CFG_DASH_MSK(radio_cfg);
		data->radio_cfg_pnum = NVM_RF_CFG_PNUM_MSK(radio_cfg);
		return;
	}

	/* set the radio configuration for family 8000 */
	data->radio_cfg_type = EXT_NVM_RF_CFG_TYPE_MSK(radio_cfg);
	data->radio_cfg_step = EXT_NVM_RF_CFG_STEP_MSK(radio_cfg);
	data->radio_cfg_dash = EXT_NVM_RF_CFG_DASH_MSK(radio_cfg);
	data->radio_cfg_pnum = EXT_NVM_RF_CFG_FLAVOR_MSK(radio_cfg);
	data->valid_tx_ant = EXT_NVM_RF_CFG_TX_ANT_MSK(radio_cfg);
	data->valid_rx_ant = EXT_NVM_RF_CFG_RX_ANT_MSK(radio_cfg);
}

static void iwl_flip_hw_address(__le32 mac_addr0, __le32 mac_addr1, u8 *dest)
{
	const u8 *hw_addr;

	hw_addr = (const u8 *)&mac_addr0;
	dest[0] = hw_addr[3];
	dest[1] = hw_addr[2];
	dest[2] = hw_addr[1];
	dest[3] = hw_addr[0];

	hw_addr = (const u8 *)&mac_addr1;
	dest[4] = hw_addr[1];
	dest[5] = hw_addr[0];
}

static void iwl_set_hw_address_from_csr(struct iwl_trans *trans,
					struct iwl_nvm_data *data)
{
	__le32 mac_addr0 = cpu_to_le32(iwl_read32(trans,
						  CSR_MAC_ADDR0_STRAP(trans)));
	__le32 mac_addr1 = cpu_to_le32(iwl_read32(trans,
						  CSR_MAC_ADDR1_STRAP(trans)));

	iwl_flip_hw_address(mac_addr0, mac_addr1, data->hw_addr);
	/*
	 * If the OEM fused a valid address, use it instead of the one in the
	 * OTP
	 */
	if (is_valid_ether_addr(data->hw_addr))
		return;

	mac_addr0 = cpu_to_le32(iwl_read32(trans, CSR_MAC_ADDR0_OTP(trans)));
	mac_addr1 = cpu_to_le32(iwl_read32(trans, CSR_MAC_ADDR1_OTP(trans)));

	iwl_flip_hw_address(mac_addr0, mac_addr1, data->hw_addr);
}

static void iwl_set_hw_address_family_8000(struct iwl_trans *trans,
					   const struct iwl_cfg *cfg,
					   struct iwl_nvm_data *data,
					   const __le16 *mac_override,
					   const __be16 *nvm_hw)
{
	const u8 *hw_addr;

	if (mac_override) {
		static const u8 reserved_mac[] = {
			0x02, 0xcc, 0xaa, 0xff, 0xee, 0x00
		};

		hw_addr = (const u8 *)(mac_override +
				 MAC_ADDRESS_OVERRIDE_EXT_NVM);

		/*
		 * Store the MAC address from MAO section.
		 * No byte swapping is required in MAO section
		 */
		memcpy(data->hw_addr, hw_addr, ETH_ALEN);

		/*
		 * Force the use of the OTP MAC address in case of reserved MAC
		 * address in the NVM, or if address is given but invalid.
		 */
		if (is_valid_ether_addr(data->hw_addr) &&
		    memcmp(reserved_mac, hw_addr, ETH_ALEN) != 0)
			return;

		IWL_ERR(trans,
			"mac address from nvm override section is not valid\n");
	}

	if (nvm_hw) {
		/* read the mac address from WFMP registers */
		__le32 mac_addr0 = cpu_to_le32(iwl_trans_read_prph(trans,
						WFMP_MAC_ADDR_0));
		__le32 mac_addr1 = cpu_to_le32(iwl_trans_read_prph(trans,
						WFMP_MAC_ADDR_1));

		iwl_flip_hw_address(mac_addr0, mac_addr1, data->hw_addr);

		return;
	}

	IWL_ERR(trans, "mac address is not found\n");
}

static int iwl_set_hw_address(struct iwl_trans *trans,
			      const struct iwl_cfg *cfg,
			      struct iwl_nvm_data *data, const __be16 *nvm_hw,
			      const __le16 *mac_override)
{
	if (cfg->mac_addr_from_csr) {
		iwl_set_hw_address_from_csr(trans, data);
	} else if (cfg->nvm_type != IWL_NVM_EXT) {
		const u8 *hw_addr = (const u8 *)(nvm_hw + HW_ADDR);

		/* The byte order is little endian 16 bit, meaning 214365 */
		data->hw_addr[0] = hw_addr[1];
		data->hw_addr[1] = hw_addr[0];
		data->hw_addr[2] = hw_addr[3];
		data->hw_addr[3] = hw_addr[2];
		data->hw_addr[4] = hw_addr[5];
		data->hw_addr[5] = hw_addr[4];
	} else {
		iwl_set_hw_address_family_8000(trans, cfg, data,
					       mac_override, nvm_hw);
	}

	if (!is_valid_ether_addr(data->hw_addr)) {
		IWL_ERR(trans, "no valid mac address was found\n");
		return -EINVAL;
	}

	if (!trans->csme_own)
		IWL_INFO(trans, "base HW address: %pM, OTP minor version: 0x%x\n",
			 data->hw_addr, iwl_read_prph(trans, REG_OTP_MINOR));

	return 0;
}

static bool
iwl_nvm_no_wide_in_5ghz(struct iwl_trans *trans, const struct iwl_cfg *cfg,
			const __be16 *nvm_hw)
{
	/*
	 * Workaround a bug in Indonesia SKUs where the regulatory in
	 * some 7000-family OTPs erroneously allow wide channels in
	 * 5GHz.  To check for Indonesia, we take the SKU value from
	 * bits 1-4 in the subsystem ID and check if it is either 5 or
	 * 9.  In those cases, we need to force-disable wide channels
	 * in 5GHz otherwise the FW will throw a sysassert when we try
	 * to use them.
	 */
	if (trans->trans_cfg->device_family == IWL_DEVICE_FAMILY_7000) {
		/*
		 * Unlike the other sections in the NVM, the hw
		 * section uses big-endian.
		 */
		u16 subsystem_id = be16_to_cpup(nvm_hw + SUBSYSTEM_ID);
		u8 sku = (subsystem_id & 0x1e) >> 1;

		if (sku == 5 || sku == 9) {
			IWL_DEBUG_EEPROM(trans->dev,
					 "disabling wide channels in 5GHz (0x%0x %d)\n",
					 subsystem_id, sku);
			return true;
		}
	}

	return false;
}

struct iwl_nvm_data *
iwl_parse_mei_nvm_data(struct iwl_trans *trans, const struct iwl_cfg *cfg,
		       const struct iwl_mei_nvm *mei_nvm,
		       const struct iwl_fw *fw, u8 tx_ant, u8 rx_ant)
{
	struct iwl_nvm_data *data;
	u32 sbands_flags = 0;
	u8 rx_chains = fw->valid_rx_ant;
	u8 tx_chains = fw->valid_rx_ant;

	if (cfg->uhb_supported)
		data = kzalloc(struct_size(data, channels,
					   IWL_NVM_NUM_CHANNELS_UHB),
					   GFP_KERNEL);
	else
		data = kzalloc(struct_size(data, channels,
					   IWL_NVM_NUM_CHANNELS_EXT),
					   GFP_KERNEL);
	if (!data)
		return NULL;

	BUILD_BUG_ON(ARRAY_SIZE(mei_nvm->channels) !=
		     IWL_NVM_NUM_CHANNELS_UHB);
	data->nvm_version = mei_nvm->nvm_version;

	iwl_set_radio_cfg(cfg, data, mei_nvm->radio_cfg);
	if (data->valid_tx_ant)
		tx_chains &= data->valid_tx_ant;
	if (data->valid_rx_ant)
		rx_chains &= data->valid_rx_ant;
	if (tx_ant)
		tx_chains &= tx_ant;
	if (rx_ant)
		rx_chains &= rx_ant;

	data->sku_cap_mimo_disabled = false;
	data->sku_cap_band_24ghz_enable = true;
	data->sku_cap_band_52ghz_enable = true;
	data->sku_cap_11n_enable =
		!(iwlwifi_mod_params.disable_11n & IWL_DISABLE_HT_ALL);
	data->sku_cap_11ac_enable = true;
	data->sku_cap_11ax_enable =
		mei_nvm->caps & MEI_NVM_CAPS_11AX_SUPPORT;

	data->lar_enabled = mei_nvm->caps & MEI_NVM_CAPS_LARI_SUPPORT;

	data->n_hw_addrs = mei_nvm->n_hw_addrs;
	/* If no valid mac address was found - bail out */
	if (iwl_set_hw_address(trans, cfg, data, NULL, NULL)) {
		kfree(data);
		return NULL;
	}

	if (data->lar_enabled &&
	    fw_has_capa(&fw->ucode_capa, IWL_UCODE_TLV_CAPA_LAR_SUPPORT))
		sbands_flags |= IWL_NVM_SBANDS_FLAGS_LAR;

	iwl_init_sbands(trans, data, mei_nvm->channels, tx_chains, rx_chains,
			sbands_flags, true, fw);

	return data;
}
IWL_EXPORT_SYMBOL(iwl_parse_mei_nvm_data);

struct iwl_nvm_data *
iwl_parse_nvm_data(struct iwl_trans *trans, const struct iwl_cfg *cfg,
		   const struct iwl_fw *fw,
		   const __be16 *nvm_hw, const __le16 *nvm_sw,
		   const __le16 *nvm_calib, const __le16 *regulatory,
		   const __le16 *mac_override, const __le16 *phy_sku,
		   u8 tx_chains, u8 rx_chains)
{
	struct iwl_nvm_data *data;
	bool lar_enabled;
	u32 sku, radio_cfg;
	u32 sbands_flags = 0;
	u16 lar_config;
	const __le16 *ch_section;

	if (cfg->uhb_supported)
		data = kzalloc(struct_size(data, channels,
					   IWL_NVM_NUM_CHANNELS_UHB),
					   GFP_KERNEL);
	else if (cfg->nvm_type != IWL_NVM_EXT)
		data = kzalloc(struct_size(data, channels,
					   IWL_NVM_NUM_CHANNELS),
					   GFP_KERNEL);
	else
		data = kzalloc(struct_size(data, channels,
					   IWL_NVM_NUM_CHANNELS_EXT),
					   GFP_KERNEL);
	if (!data)
		return NULL;

	data->nvm_version = iwl_get_nvm_version(cfg, nvm_sw);

	radio_cfg = iwl_get_radio_cfg(cfg, nvm_sw, phy_sku);
	iwl_set_radio_cfg(cfg, data, radio_cfg);
	if (data->valid_tx_ant)
		tx_chains &= data->valid_tx_ant;
	if (data->valid_rx_ant)
		rx_chains &= data->valid_rx_ant;

	sku = iwl_get_sku(cfg, nvm_sw, phy_sku);
	data->sku_cap_band_24ghz_enable = sku & NVM_SKU_CAP_BAND_24GHZ;
	data->sku_cap_band_52ghz_enable = sku & NVM_SKU_CAP_BAND_52GHZ;
	data->sku_cap_11n_enable = sku & NVM_SKU_CAP_11N_ENABLE;
	if (iwlwifi_mod_params.disable_11n & IWL_DISABLE_HT_ALL)
		data->sku_cap_11n_enable = false;
	data->sku_cap_11ac_enable = data->sku_cap_11n_enable &&
				    (sku & NVM_SKU_CAP_11AC_ENABLE);
	data->sku_cap_mimo_disabled = sku & NVM_SKU_CAP_MIMO_DISABLE;

	data->n_hw_addrs = iwl_get_n_hw_addrs(cfg, nvm_sw);

	if (cfg->nvm_type != IWL_NVM_EXT) {
		/* Checking for required sections */
		if (!nvm_calib) {
			IWL_ERR(trans,
				"Can't parse empty Calib NVM sections\n");
			kfree(data);
			return NULL;
		}

		ch_section = cfg->nvm_type == IWL_NVM_SDP ?
			     &regulatory[NVM_CHANNELS_SDP] :
			     &nvm_sw[NVM_CHANNELS];

		/* in family 8000 Xtal calibration values moved to OTP */
		data->xtal_calib[0] = *(nvm_calib + XTAL_CALIB);
		data->xtal_calib[1] = *(nvm_calib + XTAL_CALIB + 1);
		lar_enabled = true;
	} else {
		u16 lar_offset = data->nvm_version < 0xE39 ?
				 NVM_LAR_OFFSET_OLD :
				 NVM_LAR_OFFSET;

		lar_config = le16_to_cpup(regulatory + lar_offset);
		data->lar_enabled = !!(lar_config &
				       NVM_LAR_ENABLED);
		lar_enabled = data->lar_enabled;
		ch_section = &regulatory[NVM_CHANNELS_EXTENDED];
	}

	/* If no valid mac address was found - bail out */
	if (iwl_set_hw_address(trans, cfg, data, nvm_hw, mac_override)) {
		kfree(data);
		return NULL;
	}

	if (lar_enabled &&
	    fw_has_capa(&fw->ucode_capa, IWL_UCODE_TLV_CAPA_LAR_SUPPORT))
		sbands_flags |= IWL_NVM_SBANDS_FLAGS_LAR;

	if (iwl_nvm_no_wide_in_5ghz(trans, cfg, nvm_hw))
		sbands_flags |= IWL_NVM_SBANDS_FLAGS_NO_WIDE_IN_5GHZ;

	iwl_init_sbands(trans, data, ch_section, tx_chains, rx_chains,
			sbands_flags, false, fw);
	data->calib_version = 255;

	return data;
}
IWL_EXPORT_SYMBOL(iwl_parse_nvm_data);

static u32 iwl_nvm_get_regdom_bw_flags(const u16 *nvm_chan,
				       int ch_idx, u16 nvm_flags,
				       struct iwl_reg_capa reg_capa,
				       const struct iwl_cfg *cfg)
{
	u32 flags = NL80211_RRF_NO_HT40;

	if (ch_idx < NUM_2GHZ_CHANNELS &&
	    (nvm_flags & NVM_CHANNEL_40MHZ)) {
		if (nvm_chan[ch_idx] <= LAST_2GHZ_HT_PLUS)
			flags &= ~NL80211_RRF_NO_HT40PLUS;
		if (nvm_chan[ch_idx] >= FIRST_2GHZ_HT_MINUS)
			flags &= ~NL80211_RRF_NO_HT40MINUS;
	} else if (nvm_flags & NVM_CHANNEL_40MHZ) {
		if ((ch_idx - NUM_2GHZ_CHANNELS) % 2 == 0)
			flags &= ~NL80211_RRF_NO_HT40PLUS;
		else
			flags &= ~NL80211_RRF_NO_HT40MINUS;
	}

	if (!(nvm_flags & NVM_CHANNEL_80MHZ))
		flags |= NL80211_RRF_NO_80MHZ;
	if (!(nvm_flags & NVM_CHANNEL_160MHZ))
		flags |= NL80211_RRF_NO_160MHZ;

	if (!(nvm_flags & NVM_CHANNEL_ACTIVE))
		flags |= NL80211_RRF_NO_IR;

	if (nvm_flags & NVM_CHANNEL_RADAR)
		flags |= NL80211_RRF_DFS;

	if (nvm_flags & NVM_CHANNEL_INDOOR_ONLY)
		flags |= NL80211_RRF_NO_OUTDOOR;

	/* Set the GO concurrent flag only in case that NO_IR is set.
	 * Otherwise it is meaningless
	 */
	if ((nvm_flags & NVM_CHANNEL_GO_CONCURRENT)) {
		if (flags & NL80211_RRF_NO_IR)
			flags |= NL80211_RRF_GO_CONCURRENT;
		if (flags & NL80211_RRF_DFS) {
			flags |= NL80211_RRF_DFS_CONCURRENT;
			/* Our device doesn't set active bit for DFS channels
			 * however, once marked as DFS no-ir is not needed.
			 */
			flags &= ~NL80211_RRF_NO_IR;
		}
	}
	/*
	 * reg_capa is per regulatory domain so apply it for every channel
	 */
	if (ch_idx >= NUM_2GHZ_CHANNELS) {
		if (!reg_capa.allow_40mhz)
			flags |= NL80211_RRF_NO_HT40;

		if (!reg_capa.allow_80mhz)
			flags |= NL80211_RRF_NO_80MHZ;

		if (!reg_capa.allow_160mhz)
			flags |= NL80211_RRF_NO_160MHZ;

		if (!reg_capa.allow_320mhz)
			flags |= NL80211_RRF_NO_320MHZ;
	}

	if (reg_capa.disable_11ax)
		flags |= NL80211_RRF_NO_HE;

	if (reg_capa.disable_11be)
		flags |= NL80211_RRF_NO_EHT;

	return flags;
}

static struct iwl_reg_capa iwl_get_reg_capa(u32 flags, u8 resp_ver)
{
	struct iwl_reg_capa reg_capa = {};

	if (resp_ver >= REG_CAPA_V4_RESP_VER) {
		reg_capa.allow_40mhz = true;
		reg_capa.allow_80mhz = flags & REG_CAPA_V4_80MHZ_ALLOWED;
		reg_capa.allow_160mhz = flags & REG_CAPA_V4_160MHZ_ALLOWED;
		reg_capa.allow_320mhz = flags & REG_CAPA_V4_320MHZ_ALLOWED;
		reg_capa.disable_11ax = flags & REG_CAPA_V4_11AX_DISABLED;
		reg_capa.disable_11be = flags & REG_CAPA_V4_11BE_DISABLED;
	} else if (resp_ver >= REG_CAPA_V2_RESP_VER) {
		reg_capa.allow_40mhz = flags & REG_CAPA_V2_40MHZ_ALLOWED;
		reg_capa.allow_80mhz = flags & REG_CAPA_V2_80MHZ_ALLOWED;
		reg_capa.allow_160mhz = flags & REG_CAPA_V2_160MHZ_ALLOWED;
		reg_capa.disable_11ax = flags & REG_CAPA_V2_11AX_DISABLED;
	} else {
		reg_capa.allow_40mhz = !(flags & REG_CAPA_V1_40MHZ_FORBIDDEN);
		reg_capa.allow_80mhz = flags & REG_CAPA_V1_80MHZ_ALLOWED;
		reg_capa.allow_160mhz = flags & REG_CAPA_V1_160MHZ_ALLOWED;
		reg_capa.disable_11ax = flags & REG_CAPA_V1_11AX_DISABLED;
	}
	return reg_capa;
}

struct ieee80211_regdomain *
iwl_parse_nvm_mcc_info(struct device *dev, const struct iwl_cfg *cfg,
		       int num_of_ch, __le32 *channels, u16 fw_mcc,
		       u16 geo_info, u32 cap, u8 resp_ver)
{
	int ch_idx;
	u16 ch_flags;
	u32 reg_rule_flags, prev_reg_rule_flags = 0;
	const u16 *nvm_chan;
	struct ieee80211_regdomain *regd, *copy_rd;
	struct ieee80211_reg_rule *rule;
	enum nl80211_band band;
	int center_freq, prev_center_freq = 0;
	int valid_rules = 0;
	bool new_rule;
	int max_num_ch;
	struct iwl_reg_capa reg_capa;

	if (cfg->uhb_supported) {
		max_num_ch = IWL_NVM_NUM_CHANNELS_UHB;
		nvm_chan = iwl_uhb_nvm_channels;
	} else if (cfg->nvm_type == IWL_NVM_EXT) {
		max_num_ch = IWL_NVM_NUM_CHANNELS_EXT;
		nvm_chan = iwl_ext_nvm_channels;
	} else {
		max_num_ch = IWL_NVM_NUM_CHANNELS;
		nvm_chan = iwl_nvm_channels;
	}

	if (num_of_ch > max_num_ch) {
		IWL_DEBUG_DEV(dev, IWL_DL_LAR,
			      "Num of channels (%d) is greater than expected. Truncating to %d\n",
			      num_of_ch, max_num_ch);
		num_of_ch = max_num_ch;
	}

	if (WARN_ON_ONCE(num_of_ch > NL80211_MAX_SUPP_REG_RULES))
		return ERR_PTR(-EINVAL);

	IWL_DEBUG_DEV(dev, IWL_DL_LAR, "building regdom for %d channels\n",
		      num_of_ch);

	/* build a regdomain rule for every valid channel */
	regd = kzalloc(struct_size(regd, reg_rules, num_of_ch), GFP_KERNEL);
	if (!regd)
		return ERR_PTR(-ENOMEM);

	/* set alpha2 from FW. */
	regd->alpha2[0] = fw_mcc >> 8;
	regd->alpha2[1] = fw_mcc & 0xff;

	/* parse regulatory capability flags */
	reg_capa = iwl_get_reg_capa(cap, resp_ver);

	for (ch_idx = 0; ch_idx < num_of_ch; ch_idx++) {
		ch_flags = (u16)__le32_to_cpup(channels + ch_idx);
		band = iwl_nl80211_band_from_channel_idx(ch_idx);
		center_freq = ieee80211_channel_to_frequency(nvm_chan[ch_idx],
							     band);
		new_rule = false;

		if (!(ch_flags & NVM_CHANNEL_VALID)) {
			iwl_nvm_print_channel_flags(dev, IWL_DL_LAR,
						    nvm_chan[ch_idx], ch_flags);
			continue;
		}

		reg_rule_flags = iwl_nvm_get_regdom_bw_flags(nvm_chan, ch_idx,
							     ch_flags, reg_capa,
							     cfg);

		/* we can't continue the same rule */
		if (ch_idx == 0 || prev_reg_rule_flags != reg_rule_flags ||
		    center_freq - prev_center_freq > 20) {
			valid_rules++;
			new_rule = true;
		}

		rule = &regd->reg_rules[valid_rules - 1];

		if (new_rule)
			rule->freq_range.start_freq_khz =
						MHZ_TO_KHZ(center_freq - 10);

		rule->freq_range.end_freq_khz = MHZ_TO_KHZ(center_freq + 10);

		/* this doesn't matter - not used by FW */
		rule->power_rule.max_antenna_gain = DBI_TO_MBI(6);
		rule->power_rule.max_eirp =
			DBM_TO_MBM(IWL_DEFAULT_MAX_TX_POWER);

		rule->flags = reg_rule_flags;

		/* rely on auto-calculation to merge BW of contiguous chans */
		rule->flags |= NL80211_RRF_AUTO_BW;
		rule->freq_range.max_bandwidth_khz = 0;

		prev_center_freq = center_freq;
		prev_reg_rule_flags = reg_rule_flags;

		iwl_nvm_print_channel_flags(dev, IWL_DL_LAR,
					    nvm_chan[ch_idx], ch_flags);

		if (!(geo_info & GEO_WMM_ETSI_5GHZ_INFO) ||
		    band == NL80211_BAND_2GHZ)
			continue;

		reg_query_regdb_wmm(regd->alpha2, center_freq, rule);
	}

	/*
	 * Certain firmware versions might report no valid channels
	 * if booted in RF-kill, i.e. not all calibrations etc. are
	 * running. We'll get out of this situation later when the
	 * rfkill is removed and we update the regdomain again, but
	 * since cfg80211 doesn't accept an empty regdomain, add a
	 * dummy (unusable) rule here in this case so we can init.
	 */
	if (!valid_rules) {
		valid_rules = 1;
		rule = &regd->reg_rules[valid_rules - 1];
		rule->freq_range.start_freq_khz = MHZ_TO_KHZ(2412);
		rule->freq_range.end_freq_khz = MHZ_TO_KHZ(2413);
		rule->freq_range.max_bandwidth_khz = MHZ_TO_KHZ(1);
		rule->power_rule.max_antenna_gain = DBI_TO_MBI(6);
		rule->power_rule.max_eirp =
			DBM_TO_MBM(IWL_DEFAULT_MAX_TX_POWER);
	}

	regd->n_reg_rules = valid_rules;

	/*
	 * Narrow down regdom for unused regulatory rules to prevent hole
	 * between reg rules to wmm rules.
	 */
	copy_rd = kmemdup(regd, struct_size(regd, reg_rules, valid_rules),
			  GFP_KERNEL);
	if (!copy_rd)
		copy_rd = ERR_PTR(-ENOMEM);

	kfree(regd);
	return copy_rd;
}
IWL_EXPORT_SYMBOL(iwl_parse_nvm_mcc_info);

#define IWL_MAX_NVM_SECTION_SIZE	0x1b58
#define IWL_MAX_EXT_NVM_SECTION_SIZE	0x1ffc
#define MAX_NVM_FILE_LEN	16384

void iwl_nvm_fixups(u32 hw_id, unsigned int section, u8 *data,
		    unsigned int len)
{
#define IWL_4165_DEVICE_ID	0x5501
#define NVM_SKU_CAP_MIMO_DISABLE BIT(5)

	if (section == NVM_SECTION_TYPE_PHY_SKU &&
	    hw_id == IWL_4165_DEVICE_ID && data && len >= 5 &&
	    (data[4] & NVM_SKU_CAP_MIMO_DISABLE))
		/* OTP 0x52 bug work around: it's a 1x1 device */
		data[3] = ANT_B | (ANT_B << 4);
}
IWL_EXPORT_SYMBOL(iwl_nvm_fixups);

/*
 * Reads external NVM from a file into mvm->nvm_sections
 *
 * HOW TO CREATE THE NVM FILE FORMAT:
 * ------------------------------
 * 1. create hex file, format:
 *      3800 -> header
 *      0000 -> header
 *      5a40 -> data
 *
 *   rev - 6 bit (word1)
 *   len - 10 bit (word1)
 *   id - 4 bit (word2)
 *   rsv - 12 bit (word2)
 *
 * 2. flip 8bits with 8 bits per line to get the right NVM file format
 *
 * 3. create binary file from the hex file
 *
 * 4. save as "iNVM_xxx.bin" under /lib/firmware
 */
int iwl_read_external_nvm(struct iwl_trans *trans,
			  const char *nvm_file_name,
			  struct iwl_nvm_section *nvm_sections)
{
	int ret, section_size;
	u16 section_id;
	const struct firmware *fw_entry;
	const struct {
		__le16 word1;
		__le16 word2;
		u8 data[];
	} *file_sec;
	const u8 *eof;
	u8 *temp;
	int max_section_size;
	const __le32 *dword_buff;

#define NVM_WORD1_LEN(x) (8 * (x & 0x03FF))
#define NVM_WORD2_ID(x) (x >> 12)
#define EXT_NVM_WORD2_LEN(x) (2 * (((x) & 0xFF) << 8 | (x) >> 8))
#define EXT_NVM_WORD1_ID(x) ((x) >> 4)
#define NVM_HEADER_0	(0x2A504C54)
#define NVM_HEADER_1	(0x4E564D2A)
#define NVM_HEADER_SIZE	(4 * sizeof(u32))

	IWL_DEBUG_EEPROM(trans->dev, "Read from external NVM\n");

	/* Maximal size depends on NVM version */
	if (trans->cfg->nvm_type != IWL_NVM_EXT)
		max_section_size = IWL_MAX_NVM_SECTION_SIZE;
	else
		max_section_size = IWL_MAX_EXT_NVM_SECTION_SIZE;

	/*
	 * Obtain NVM image via request_firmware. Since we already used
	 * request_firmware_nowait() for the firmware binary load and only
	 * get here after that we assume the NVM request can be satisfied
	 * synchronously.
	 */
	ret = request_firmware(&fw_entry, nvm_file_name, trans->dev);
	if (ret) {
		IWL_ERR(trans, "ERROR: %s isn't available %d\n",
			nvm_file_name, ret);
		return ret;
	}

	IWL_INFO(trans, "Loaded NVM file %s (%zu bytes)\n",
		 nvm_file_name, fw_entry->size);

	if (fw_entry->size > MAX_NVM_FILE_LEN) {
		IWL_ERR(trans, "NVM file too large\n");
		ret = -EINVAL;
		goto out;
	}

	eof = fw_entry->data + fw_entry->size;
	dword_buff = (const __le32 *)fw_entry->data;

	/* some NVM file will contain a header.
	 * The header is identified by 2 dwords header as follow:
	 * dword[0] = 0x2A504C54
	 * dword[1] = 0x4E564D2A
	 *
	 * This header must be skipped when providing the NVM data to the FW.
	 */
	if (fw_entry->size > NVM_HEADER_SIZE &&
	    dword_buff[0] == cpu_to_le32(NVM_HEADER_0) &&
	    dword_buff[1] == cpu_to_le32(NVM_HEADER_1)) {
		file_sec = (const void *)(fw_entry->data + NVM_HEADER_SIZE);
		IWL_INFO(trans, "NVM Version %08X\n", le32_to_cpu(dword_buff[2]));
		IWL_INFO(trans, "NVM Manufacturing date %08X\n",
			 le32_to_cpu(dword_buff[3]));

		/* nvm file validation, dword_buff[2] holds the file version */
		if (trans->trans_cfg->device_family == IWL_DEVICE_FAMILY_8000 &&
		    trans->hw_rev_step == SILICON_C_STEP &&
		    le32_to_cpu(dword_buff[2]) < 0xE4A) {
			ret = -EFAULT;
			goto out;
		}
	} else {
		file_sec = (const void *)fw_entry->data;
	}

	while (true) {
		if (file_sec->data > eof) {
			IWL_ERR(trans,
				"ERROR - NVM file too short for section header\n");
			ret = -EINVAL;
			break;
		}

		/* check for EOF marker */
		if (!file_sec->word1 && !file_sec->word2) {
			ret = 0;
			break;
		}

		if (trans->cfg->nvm_type != IWL_NVM_EXT) {
			section_size =
				2 * NVM_WORD1_LEN(le16_to_cpu(file_sec->word1));
			section_id = NVM_WORD2_ID(le16_to_cpu(file_sec->word2));
		} else {
			section_size = 2 * EXT_NVM_WORD2_LEN(
						le16_to_cpu(file_sec->word2));
			section_id = EXT_NVM_WORD1_ID(
						le16_to_cpu(file_sec->word1));
		}

		if (section_size > max_section_size) {
			IWL_ERR(trans, "ERROR - section too large (%d)\n",
				section_size);
			ret = -EINVAL;
			break;
		}

		if (!section_size) {
			IWL_ERR(trans, "ERROR - section empty\n");
			ret = -EINVAL;
			break;
		}

		if (file_sec->data + section_size > eof) {
			IWL_ERR(trans,
				"ERROR - NVM file too short for section (%d bytes)\n",
				section_size);
			ret = -EINVAL;
			break;
		}

		if (WARN(section_id >= NVM_MAX_NUM_SECTIONS,
			 "Invalid NVM section ID %d\n", section_id)) {
			ret = -EINVAL;
			break;
		}

		temp = kmemdup(file_sec->data, section_size, GFP_KERNEL);
		if (!temp) {
			ret = -ENOMEM;
			break;
		}

		iwl_nvm_fixups(trans->hw_id, section_id, temp, section_size);

		kfree(nvm_sections[section_id].data);
		nvm_sections[section_id].data = temp;
		nvm_sections[section_id].length = section_size;

		/* advance to the next section */
		file_sec = (const void *)(file_sec->data + section_size);
	}
out:
	release_firmware(fw_entry);
	return ret;
}
IWL_EXPORT_SYMBOL(iwl_read_external_nvm);

struct iwl_nvm_data *iwl_get_nvm(struct iwl_trans *trans,
				 const struct iwl_fw *fw,
				 u8 set_tx_ant, u8 set_rx_ant)
{
	struct iwl_nvm_get_info cmd = {};
	struct iwl_nvm_data *nvm;
	struct iwl_host_cmd hcmd = {
		.flags = CMD_WANT_SKB | CMD_SEND_IN_RFKILL,
		.data = { &cmd, },
		.len = { sizeof(cmd) },
		.id = WIDE_ID(REGULATORY_AND_NVM_GROUP, NVM_GET_INFO)
	};
	int  ret;
	bool empty_otp;
	u32 mac_flags;
	u32 sbands_flags = 0;
	u8 tx_ant;
	u8 rx_ant;

	/*
	 * All the values in iwl_nvm_get_info_rsp v4 are the same as
	 * in v3, except for the channel profile part of the
	 * regulatory.  So we can just access the new struct, with the
	 * exception of the latter.
	 */
	struct iwl_nvm_get_info_rsp *rsp;
	struct iwl_nvm_get_info_rsp_v3 *rsp_v3;
	bool v4 = fw_has_api(&fw->ucode_capa,
			     IWL_UCODE_TLV_API_REGULATORY_NVM_INFO);
	size_t rsp_size = v4 ? sizeof(*rsp) : sizeof(*rsp_v3);
	void *channel_profile;

	ret = iwl_trans_send_cmd(trans, &hcmd);
	if (ret)
		return ERR_PTR(ret);

	if (WARN(iwl_rx_packet_payload_len(hcmd.resp_pkt) != rsp_size,
		 "Invalid payload len in NVM response from FW %d",
		 iwl_rx_packet_payload_len(hcmd.resp_pkt))) {
		ret = -EINVAL;
		goto out;
	}

	rsp = (void *)hcmd.resp_pkt->data;
	empty_otp = !!(le32_to_cpu(rsp->general.flags) &
		       NVM_GENERAL_FLAGS_EMPTY_OTP);
	if (empty_otp)
		IWL_INFO(trans, "OTP is empty\n");

	nvm = kzalloc(struct_size(nvm, channels, IWL_NUM_CHANNELS), GFP_KERNEL);
	if (!nvm) {
		ret = -ENOMEM;
		goto out;
	}

	iwl_set_hw_address_from_csr(trans, nvm);
	/* TODO: if platform NVM has MAC address - override it here */

	if (!is_valid_ether_addr(nvm->hw_addr)) {
		IWL_ERR(trans, "no valid mac address was found\n");
		ret = -EINVAL;
		goto err_free;
	}

	IWL_INFO(trans, "base HW address: %pM\n", nvm->hw_addr);

	/* Initialize general data */
	nvm->nvm_version = le16_to_cpu(rsp->general.nvm_version);
	nvm->n_hw_addrs = rsp->general.n_hw_addrs;
	if (nvm->n_hw_addrs == 0)
		IWL_WARN(trans,
			 "Firmware declares no reserved mac addresses. OTP is empty: %d\n",
			 empty_otp);

	/* Initialize MAC sku data */
	mac_flags = le32_to_cpu(rsp->mac_sku.mac_sku_flags);
	nvm->sku_cap_11ac_enable =
		!!(mac_flags & NVM_MAC_SKU_FLAGS_802_11AC_ENABLED);
	nvm->sku_cap_11n_enable =
		!!(mac_flags & NVM_MAC_SKU_FLAGS_802_11N_ENABLED);
	nvm->sku_cap_11ax_enable =
		!!(mac_flags & NVM_MAC_SKU_FLAGS_802_11AX_ENABLED);
	nvm->sku_cap_band_24ghz_enable =
		!!(mac_flags & NVM_MAC_SKU_FLAGS_BAND_2_4_ENABLED);
	nvm->sku_cap_band_52ghz_enable =
		!!(mac_flags & NVM_MAC_SKU_FLAGS_BAND_5_2_ENABLED);
	nvm->sku_cap_mimo_disabled =
		!!(mac_flags & NVM_MAC_SKU_FLAGS_MIMO_DISABLED);
	if (CSR_HW_RFID_TYPE(trans->hw_rf_id) == IWL_CFG_RF_TYPE_FM)
		nvm->sku_cap_11be_enable = true;

	/* Initialize PHY sku data */
	nvm->valid_tx_ant = (u8)le32_to_cpu(rsp->phy_sku.tx_chains);
	nvm->valid_rx_ant = (u8)le32_to_cpu(rsp->phy_sku.rx_chains);

	if (le32_to_cpu(rsp->regulatory.lar_enabled) &&
	    fw_has_capa(&fw->ucode_capa,
			IWL_UCODE_TLV_CAPA_LAR_SUPPORT)) {
		nvm->lar_enabled = true;
		sbands_flags |= IWL_NVM_SBANDS_FLAGS_LAR;
	}

	rsp_v3 = (void *)rsp;
	channel_profile = v4 ? (void *)rsp->regulatory.channel_profile :
			  (void *)rsp_v3->regulatory.channel_profile;

	tx_ant = nvm->valid_tx_ant & fw->valid_tx_ant;
	rx_ant = nvm->valid_rx_ant & fw->valid_rx_ant;

	if (set_tx_ant)
		tx_ant &= set_tx_ant;
	if (set_rx_ant)
		rx_ant &= set_rx_ant;

	iwl_init_sbands(trans, nvm, channel_profile, tx_ant, rx_ant,
			sbands_flags, v4, fw);

	iwl_free_resp(&hcmd);
	return nvm;

err_free:
	kfree(nvm);
out:
	iwl_free_resp(&hcmd);
	return ERR_PTR(ret);
}
IWL_EXPORT_SYMBOL(iwl_get_nvm);
