// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2005-2014, 2018-2019, 2021, 2024 Intel Corporation
 */
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/export.h>

#include "iwl-drv.h"
#include "iwl-debug.h"
#include "iwl-io.h"
#include "iwl-prph.h"
#include "iwl-csr.h"
#include "agn.h"

/* EEPROM offset definitions */

/* indirect access definitions */
#define ADDRESS_MSK                 0x0000FFFF
#define INDIRECT_TYPE_MSK           0x000F0000
#define INDIRECT_HOST               0x00010000
#define INDIRECT_GENERAL            0x00020000
#define INDIRECT_REGULATORY         0x00030000
#define INDIRECT_CALIBRATION        0x00040000
#define INDIRECT_PROCESS_ADJST      0x00050000
#define INDIRECT_OTHERS             0x00060000
#define INDIRECT_TXP_LIMIT          0x00070000
#define INDIRECT_TXP_LIMIT_SIZE     0x00080000
#define INDIRECT_ADDRESS            0x00100000

/* corresponding link offsets in EEPROM */
#define EEPROM_LINK_HOST             (2*0x64)
#define EEPROM_LINK_GENERAL          (2*0x65)
#define EEPROM_LINK_REGULATORY       (2*0x66)
#define EEPROM_LINK_CALIBRATION      (2*0x67)
#define EEPROM_LINK_PROCESS_ADJST    (2*0x68)
#define EEPROM_LINK_OTHERS           (2*0x69)
#define EEPROM_LINK_TXP_LIMIT        (2*0x6a)
#define EEPROM_LINK_TXP_LIMIT_SIZE   (2*0x6b)

/* General */
#define EEPROM_DEVICE_ID                    (2*0x08)	/* 2 bytes */
#define EEPROM_SUBSYSTEM_ID		    (2*0x0A)	/* 2 bytes */
#define EEPROM_MAC_ADDRESS                  (2*0x15)	/* 6  bytes */
#define EEPROM_BOARD_REVISION               (2*0x35)	/* 2  bytes */
#define EEPROM_BOARD_PBA_NUMBER             (2*0x3B+1)	/* 9  bytes */
#define EEPROM_VERSION                      (2*0x44)	/* 2  bytes */
#define EEPROM_SKU_CAP                      (2*0x45)	/* 2  bytes */
#define EEPROM_OEM_MODE                     (2*0x46)	/* 2  bytes */
#define EEPROM_RADIO_CONFIG                 (2*0x48)	/* 2  bytes */
#define EEPROM_NUM_MAC_ADDRESS              (2*0x4C)	/* 2  bytes */

/* calibration */
struct iwl_eeprom_calib_hdr {
	u8 version;
	u8 pa_type;
	__le16 voltage;
} __packed;

#define EEPROM_CALIB_ALL	(INDIRECT_ADDRESS | INDIRECT_CALIBRATION)
#define EEPROM_XTAL		((2*0x128) | EEPROM_CALIB_ALL)

/* temperature */
#define EEPROM_KELVIN_TEMPERATURE	((2*0x12A) | EEPROM_CALIB_ALL)
#define EEPROM_RAW_TEMPERATURE		((2*0x12B) | EEPROM_CALIB_ALL)

/* SKU Capabilities (actual values from EEPROM definition) */
enum eeprom_sku_bits {
	EEPROM_SKU_CAP_BAND_24GHZ	= BIT(4),
	EEPROM_SKU_CAP_BAND_52GHZ	= BIT(5),
	EEPROM_SKU_CAP_11N_ENABLE	= BIT(6),
	EEPROM_SKU_CAP_AMT_ENABLE	= BIT(7),
	EEPROM_SKU_CAP_IPAN_ENABLE	= BIT(8)
};

/* radio config bits (actual values from EEPROM definition) */
#define EEPROM_RF_CFG_TYPE_MSK(x)   (x & 0x3)         /* bits 0-1   */
#define EEPROM_RF_CFG_STEP_MSK(x)   ((x >> 2)  & 0x3) /* bits 2-3   */
#define EEPROM_RF_CFG_DASH_MSK(x)   ((x >> 4)  & 0x3) /* bits 4-5   */
#define EEPROM_RF_CFG_PNUM_MSK(x)   ((x >> 6)  & 0x3) /* bits 6-7   */
#define EEPROM_RF_CFG_TX_ANT_MSK(x) ((x >> 8)  & 0xF) /* bits 8-11  */
#define EEPROM_RF_CFG_RX_ANT_MSK(x) ((x >> 12) & 0xF) /* bits 12-15 */

/*
 * EEPROM bands
 * These are the channel numbers from each band in the order
 * that they are stored in the EEPROM band information. Note
 * that EEPROM bands aren't the same as mac80211 bands, and
 * there are even special "ht40 bands" in the EEPROM.
 */
static const u8 iwl_eeprom_band_1[14] = { /* 2.4 GHz */
	1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14
};

static const u8 iwl_eeprom_band_2[] = {	/* 4915-5080MHz */
	183, 184, 185, 187, 188, 189, 192, 196, 7, 8, 11, 12, 16
};

static const u8 iwl_eeprom_band_3[] = {	/* 5170-5320MHz */
	34, 36, 38, 40, 42, 44, 46, 48, 52, 56, 60, 64
};

static const u8 iwl_eeprom_band_4[] = {	/* 5500-5700MHz */
	100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140
};

static const u8 iwl_eeprom_band_5[] = {	/* 5725-5825MHz */
	145, 149, 153, 157, 161, 165
};

static const u8 iwl_eeprom_band_6[] = {	/* 2.4 ht40 channel */
	1, 2, 3, 4, 5, 6, 7
};

static const u8 iwl_eeprom_band_7[] = {	/* 5.2 ht40 channel */
	36, 44, 52, 60, 100, 108, 116, 124, 132, 149, 157
};

#define IWL_NUM_CHANNELS	(ARRAY_SIZE(iwl_eeprom_band_1) + \
				 ARRAY_SIZE(iwl_eeprom_band_2) + \
				 ARRAY_SIZE(iwl_eeprom_band_3) + \
				 ARRAY_SIZE(iwl_eeprom_band_4) + \
				 ARRAY_SIZE(iwl_eeprom_band_5))

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

/* EEPROM reading functions */

static u16 iwl_eeprom_query16(const u8 *eeprom, size_t eeprom_size, int offset)
{
	if (WARN_ON(offset + sizeof(u16) > eeprom_size))
		return 0;
	return le16_to_cpup((__le16 *)(eeprom + offset));
}

static u32 eeprom_indirect_address(const u8 *eeprom, size_t eeprom_size,
				   u32 address)
{
	u16 offset = 0;

	if ((address & INDIRECT_ADDRESS) == 0)
		return address;

	switch (address & INDIRECT_TYPE_MSK) {
	case INDIRECT_HOST:
		offset = iwl_eeprom_query16(eeprom, eeprom_size,
					    EEPROM_LINK_HOST);
		break;
	case INDIRECT_GENERAL:
		offset = iwl_eeprom_query16(eeprom, eeprom_size,
					    EEPROM_LINK_GENERAL);
		break;
	case INDIRECT_REGULATORY:
		offset = iwl_eeprom_query16(eeprom, eeprom_size,
					    EEPROM_LINK_REGULATORY);
		break;
	case INDIRECT_TXP_LIMIT:
		offset = iwl_eeprom_query16(eeprom, eeprom_size,
					    EEPROM_LINK_TXP_LIMIT);
		break;
	case INDIRECT_TXP_LIMIT_SIZE:
		offset = iwl_eeprom_query16(eeprom, eeprom_size,
					    EEPROM_LINK_TXP_LIMIT_SIZE);
		break;
	case INDIRECT_CALIBRATION:
		offset = iwl_eeprom_query16(eeprom, eeprom_size,
					    EEPROM_LINK_CALIBRATION);
		break;
	case INDIRECT_PROCESS_ADJST:
		offset = iwl_eeprom_query16(eeprom, eeprom_size,
					    EEPROM_LINK_PROCESS_ADJST);
		break;
	case INDIRECT_OTHERS:
		offset = iwl_eeprom_query16(eeprom, eeprom_size,
					    EEPROM_LINK_OTHERS);
		break;
	default:
		WARN_ON(1);
		break;
	}

	/* translate the offset from words to byte */
	return (address & ADDRESS_MSK) + (offset << 1);
}

static const u8 *iwl_eeprom_query_addr(const u8 *eeprom, size_t eeprom_size,
				       u32 offset)
{
	u32 address = eeprom_indirect_address(eeprom, eeprom_size, offset);

	if (WARN_ON(address >= eeprom_size))
		return NULL;

	return &eeprom[address];
}

static int iwl_eeprom_read_calib(const u8 *eeprom, size_t eeprom_size,
				 struct iwl_nvm_data *data)
{
	struct iwl_eeprom_calib_hdr *hdr;

	hdr = (void *)iwl_eeprom_query_addr(eeprom, eeprom_size,
					    EEPROM_CALIB_ALL);
	if (!hdr)
		return -ENODATA;
	data->calib_version = hdr->version;
	data->calib_voltage = hdr->voltage;

	return 0;
}

/**
 * enum iwl_eeprom_channel_flags - channel flags in EEPROM
 * @EEPROM_CHANNEL_VALID: channel is usable for this SKU/geo
 * @EEPROM_CHANNEL_IBSS: usable as an IBSS channel
 * @EEPROM_CHANNEL_ACTIVE: active scanning allowed
 * @EEPROM_CHANNEL_RADAR: radar detection required
 * @EEPROM_CHANNEL_WIDE: 20 MHz channel okay (?)
 * @EEPROM_CHANNEL_DFS: dynamic freq selection candidate
 */
enum iwl_eeprom_channel_flags {
	EEPROM_CHANNEL_VALID = BIT(0),
	EEPROM_CHANNEL_IBSS = BIT(1),
	EEPROM_CHANNEL_ACTIVE = BIT(3),
	EEPROM_CHANNEL_RADAR = BIT(4),
	EEPROM_CHANNEL_WIDE = BIT(5),
	EEPROM_CHANNEL_DFS = BIT(7),
};

/**
 * struct iwl_eeprom_channel - EEPROM channel data
 * @flags: %EEPROM_CHANNEL_* flags
 * @max_power_avg: max power (in dBm) on this channel, at most 31 dBm
 */
struct iwl_eeprom_channel {
	u8 flags;
	s8 max_power_avg;
} __packed;

enum iwl_eeprom_enhanced_txpwr_flags {
	IWL_EEPROM_ENH_TXP_FL_VALID = BIT(0),
	IWL_EEPROM_ENH_TXP_FL_BAND_52G = BIT(1),
	IWL_EEPROM_ENH_TXP_FL_OFDM = BIT(2),
	IWL_EEPROM_ENH_TXP_FL_40MHZ = BIT(3),
	IWL_EEPROM_ENH_TXP_FL_HT_AP = BIT(4),
	IWL_EEPROM_ENH_TXP_FL_RES1 = BIT(5),
	IWL_EEPROM_ENH_TXP_FL_RES2 = BIT(6),
	IWL_EEPROM_ENH_TXP_FL_COMMON_TYPE = BIT(7),
};

/**
 * struct iwl_eeprom_enhanced_txpwr - enhanced regulatory TX power limits
 * @flags: entry flags
 * @channel: channel number
 * @chain_a_max: chain a max power in 1/2 dBm
 * @chain_b_max: chain b max power in 1/2 dBm
 * @chain_c_max: chain c max power in 1/2 dBm
 * @delta_20_in_40: 20-in-40 deltas (hi/lo)
 * @mimo2_max: mimo2 max power in 1/2 dBm
 * @mimo3_max: mimo3 max power in 1/2 dBm
 *
 * This structure presents the enhanced regulatory tx power limit layout
 * in an EEPROM image.
 */
struct iwl_eeprom_enhanced_txpwr {
	u8 flags;
	u8 channel;
	s8 chain_a_max;
	s8 chain_b_max;
	s8 chain_c_max;
	u8 delta_20_in_40;
	s8 mimo2_max;
	s8 mimo3_max;
} __packed;

static s8 iwl_get_max_txpwr_half_dbm(const struct iwl_nvm_data *data,
				     struct iwl_eeprom_enhanced_txpwr *txp)
{
	s8 result = 0; /* (.5 dBm) */

	/* Take the highest tx power from any valid chains */
	if (data->valid_tx_ant & ANT_A && txp->chain_a_max > result)
		result = txp->chain_a_max;

	if (data->valid_tx_ant & ANT_B && txp->chain_b_max > result)
		result = txp->chain_b_max;

	if (data->valid_tx_ant & ANT_C && txp->chain_c_max > result)
		result = txp->chain_c_max;

	if ((data->valid_tx_ant == ANT_AB ||
	     data->valid_tx_ant == ANT_BC ||
	     data->valid_tx_ant == ANT_AC) && txp->mimo2_max > result)
		result = txp->mimo2_max;

	if (data->valid_tx_ant == ANT_ABC && txp->mimo3_max > result)
		result = txp->mimo3_max;

	return result;
}

#define EEPROM_TXP_OFFS	(0x00 | INDIRECT_ADDRESS | INDIRECT_TXP_LIMIT)
#define EEPROM_TXP_ENTRY_LEN sizeof(struct iwl_eeprom_enhanced_txpwr)
#define EEPROM_TXP_SZ_OFFS (0x00 | INDIRECT_ADDRESS | INDIRECT_TXP_LIMIT_SIZE)

#define TXP_CHECK_AND_PRINT(x) \
	((txp->flags & IWL_EEPROM_ENH_TXP_FL_##x) ? # x " " : "")

static void
iwl_eeprom_enh_txp_read_element(struct iwl_nvm_data *data,
				struct iwl_eeprom_enhanced_txpwr *txp,
				int n_channels, s8 max_txpower_avg)
{
	int ch_idx;
	enum nl80211_band band;

	band = txp->flags & IWL_EEPROM_ENH_TXP_FL_BAND_52G ?
		NL80211_BAND_5GHZ : NL80211_BAND_2GHZ;

	for (ch_idx = 0; ch_idx < n_channels; ch_idx++) {
		struct ieee80211_channel *chan = &data->channels[ch_idx];

		/* update matching channel or from common data only */
		if (txp->channel != 0 && chan->hw_value != txp->channel)
			continue;

		/* update matching band only */
		if (band != chan->band)
			continue;

		if (chan->max_power < max_txpower_avg &&
		    !(txp->flags & IWL_EEPROM_ENH_TXP_FL_40MHZ))
			chan->max_power = max_txpower_avg;
	}
}

static void iwl_eeprom_enhanced_txpower(struct device *dev,
					struct iwl_nvm_data *data,
					const u8 *eeprom, size_t eeprom_size,
					int n_channels)
{
	struct iwl_eeprom_enhanced_txpwr *txp_array, *txp;
	int idx, entries;
	__le16 *txp_len;
	s8 max_txp_avg_halfdbm;

	BUILD_BUG_ON(sizeof(struct iwl_eeprom_enhanced_txpwr) != 8);

	/* the length is in 16-bit words, but we want entries */
	txp_len = (__le16 *)iwl_eeprom_query_addr(eeprom, eeprom_size,
						  EEPROM_TXP_SZ_OFFS);
	entries = le16_to_cpup(txp_len) * 2 / EEPROM_TXP_ENTRY_LEN;

	txp_array = (void *)iwl_eeprom_query_addr(eeprom, eeprom_size,
						  EEPROM_TXP_OFFS);

	for (idx = 0; idx < entries; idx++) {
		txp = &txp_array[idx];
		/* skip invalid entries */
		if (!(txp->flags & IWL_EEPROM_ENH_TXP_FL_VALID))
			continue;

		IWL_DEBUG_EEPROM(dev, "%s %d:\t %s%s%s%s%s%s%s%s (0x%02x)\n",
				 (txp->channel && (txp->flags &
					IWL_EEPROM_ENH_TXP_FL_COMMON_TYPE)) ?
					"Common " : (txp->channel) ?
					"Channel" : "Common",
				 (txp->channel),
				 TXP_CHECK_AND_PRINT(VALID),
				 TXP_CHECK_AND_PRINT(BAND_52G),
				 TXP_CHECK_AND_PRINT(OFDM),
				 TXP_CHECK_AND_PRINT(40MHZ),
				 TXP_CHECK_AND_PRINT(HT_AP),
				 TXP_CHECK_AND_PRINT(RES1),
				 TXP_CHECK_AND_PRINT(RES2),
				 TXP_CHECK_AND_PRINT(COMMON_TYPE),
				 txp->flags);
		IWL_DEBUG_EEPROM(dev,
				 "\t\t chain_A: %d chain_B: %d chain_C: %d\n",
				 txp->chain_a_max, txp->chain_b_max,
				 txp->chain_c_max);
		IWL_DEBUG_EEPROM(dev,
				 "\t\t MIMO2: %d MIMO3: %d High 20_on_40: 0x%02x Low 20_on_40: 0x%02x\n",
				 txp->mimo2_max, txp->mimo3_max,
				 ((txp->delta_20_in_40 & 0xf0) >> 4),
				 (txp->delta_20_in_40 & 0x0f));

		max_txp_avg_halfdbm = iwl_get_max_txpwr_half_dbm(data, txp);

		iwl_eeprom_enh_txp_read_element(data, txp, n_channels,
				DIV_ROUND_UP(max_txp_avg_halfdbm, 2));

		if (max_txp_avg_halfdbm > data->max_tx_pwr_half_dbm)
			data->max_tx_pwr_half_dbm = max_txp_avg_halfdbm;
	}
}

static void iwl_init_band_reference(const struct iwl_cfg *cfg,
				    const u8 *eeprom, size_t eeprom_size,
				    int eeprom_band, int *eeprom_ch_count,
				    const struct iwl_eeprom_channel **ch_info,
				    const u8 **eeprom_ch_array)
{
	u32 offset = cfg->eeprom_params->regulatory_bands[eeprom_band - 1];

	offset |= INDIRECT_ADDRESS | INDIRECT_REGULATORY;

	*ch_info = (void *)iwl_eeprom_query_addr(eeprom, eeprom_size, offset);

	switch (eeprom_band) {
	case 1:		/* 2.4GHz band */
		*eeprom_ch_count = ARRAY_SIZE(iwl_eeprom_band_1);
		*eeprom_ch_array = iwl_eeprom_band_1;
		break;
	case 2:		/* 4.9GHz band */
		*eeprom_ch_count = ARRAY_SIZE(iwl_eeprom_band_2);
		*eeprom_ch_array = iwl_eeprom_band_2;
		break;
	case 3:		/* 5.2GHz band */
		*eeprom_ch_count = ARRAY_SIZE(iwl_eeprom_band_3);
		*eeprom_ch_array = iwl_eeprom_band_3;
		break;
	case 4:		/* 5.5GHz band */
		*eeprom_ch_count = ARRAY_SIZE(iwl_eeprom_band_4);
		*eeprom_ch_array = iwl_eeprom_band_4;
		break;
	case 5:		/* 5.7GHz band */
		*eeprom_ch_count = ARRAY_SIZE(iwl_eeprom_band_5);
		*eeprom_ch_array = iwl_eeprom_band_5;
		break;
	case 6:		/* 2.4GHz ht40 channels */
		*eeprom_ch_count = ARRAY_SIZE(iwl_eeprom_band_6);
		*eeprom_ch_array = iwl_eeprom_band_6;
		break;
	case 7:		/* 5 GHz ht40 channels */
		*eeprom_ch_count = ARRAY_SIZE(iwl_eeprom_band_7);
		*eeprom_ch_array = iwl_eeprom_band_7;
		break;
	default:
		*eeprom_ch_count = 0;
		*eeprom_ch_array = NULL;
		WARN_ON(1);
	}
}

#define CHECK_AND_PRINT(x) \
	((eeprom_ch->flags & EEPROM_CHANNEL_##x) ? # x " " : "")

static void iwl_mod_ht40_chan_info(struct device *dev,
				   struct iwl_nvm_data *data, int n_channels,
				   enum nl80211_band band, u16 channel,
				   const struct iwl_eeprom_channel *eeprom_ch,
				   u8 clear_ht40_extension_channel)
{
	struct ieee80211_channel *chan = NULL;
	int i;

	for (i = 0; i < n_channels; i++) {
		if (data->channels[i].band != band)
			continue;
		if (data->channels[i].hw_value != channel)
			continue;
		chan = &data->channels[i];
		break;
	}

	if (!chan)
		return;

	IWL_DEBUG_EEPROM(dev,
			 "HT40 Ch. %d [%sGHz] %s%s%s%s%s(0x%02x %ddBm): Ad-Hoc %ssupported\n",
			 channel,
			 band == NL80211_BAND_5GHZ ? "5.2" : "2.4",
			 CHECK_AND_PRINT(IBSS),
			 CHECK_AND_PRINT(ACTIVE),
			 CHECK_AND_PRINT(RADAR),
			 CHECK_AND_PRINT(WIDE),
			 CHECK_AND_PRINT(DFS),
			 eeprom_ch->flags,
			 eeprom_ch->max_power_avg,
			 ((eeprom_ch->flags & EEPROM_CHANNEL_IBSS) &&
			  !(eeprom_ch->flags & EEPROM_CHANNEL_RADAR)) ? ""
								      : "not ");

	if (eeprom_ch->flags & EEPROM_CHANNEL_VALID)
		chan->flags &= ~clear_ht40_extension_channel;
}

#define CHECK_AND_PRINT_I(x)	\
	((eeprom_ch_info[ch_idx].flags & EEPROM_CHANNEL_##x) ? # x " " : "")

static int iwl_init_channel_map(struct device *dev, const struct iwl_cfg *cfg,
				struct iwl_nvm_data *data,
				const u8 *eeprom, size_t eeprom_size)
{
	int band, ch_idx;
	const struct iwl_eeprom_channel *eeprom_ch_info;
	const u8 *eeprom_ch_array;
	int eeprom_ch_count;
	int n_channels = 0;

	/*
	 * Loop through the 5 EEPROM bands and add them to the parse list
	 */
	for (band = 1; band <= 5; band++) {
		struct ieee80211_channel *channel;

		iwl_init_band_reference(cfg, eeprom, eeprom_size, band,
					&eeprom_ch_count, &eeprom_ch_info,
					&eeprom_ch_array);

		/* Loop through each band adding each of the channels */
		for (ch_idx = 0; ch_idx < eeprom_ch_count; ch_idx++) {
			const struct iwl_eeprom_channel *eeprom_ch;

			eeprom_ch = &eeprom_ch_info[ch_idx];

			if (!(eeprom_ch->flags & EEPROM_CHANNEL_VALID)) {
				IWL_DEBUG_EEPROM(dev,
						 "Ch. %d Flags %x [%sGHz] - No traffic\n",
						 eeprom_ch_array[ch_idx],
						 eeprom_ch_info[ch_idx].flags,
						 (band != 1) ? "5.2" : "2.4");
				continue;
			}

			channel = &data->channels[n_channels];
			n_channels++;

			channel->hw_value = eeprom_ch_array[ch_idx];
			channel->band = (band == 1) ? NL80211_BAND_2GHZ
						    : NL80211_BAND_5GHZ;
			channel->center_freq =
				ieee80211_channel_to_frequency(
					channel->hw_value, channel->band);

			/* set no-HT40, will enable as appropriate later */
			channel->flags = IEEE80211_CHAN_NO_HT40;

			if (!(eeprom_ch->flags & EEPROM_CHANNEL_IBSS))
				channel->flags |= IEEE80211_CHAN_NO_IR;

			if (!(eeprom_ch->flags & EEPROM_CHANNEL_ACTIVE))
				channel->flags |= IEEE80211_CHAN_NO_IR;

			if (eeprom_ch->flags & EEPROM_CHANNEL_RADAR)
				channel->flags |= IEEE80211_CHAN_RADAR;

			/* Initialize regulatory-based run-time data */
			channel->max_power =
				eeprom_ch_info[ch_idx].max_power_avg;
			IWL_DEBUG_EEPROM(dev,
					 "Ch. %d [%sGHz] %s%s%s%s%s%s(0x%02x %ddBm): Ad-Hoc %ssupported\n",
					 channel->hw_value,
					 (band != 1) ? "5.2" : "2.4",
					 CHECK_AND_PRINT_I(VALID),
					 CHECK_AND_PRINT_I(IBSS),
					 CHECK_AND_PRINT_I(ACTIVE),
					 CHECK_AND_PRINT_I(RADAR),
					 CHECK_AND_PRINT_I(WIDE),
					 CHECK_AND_PRINT_I(DFS),
					 eeprom_ch_info[ch_idx].flags,
					 eeprom_ch_info[ch_idx].max_power_avg,
					 ((eeprom_ch_info[ch_idx].flags &
							EEPROM_CHANNEL_IBSS) &&
					  !(eeprom_ch_info[ch_idx].flags &
							EEPROM_CHANNEL_RADAR))
						? "" : "not ");
		}
	}

	if (cfg->eeprom_params->enhanced_txpower) {
		/*
		 * for newer device (6000 series and up)
		 * EEPROM contain enhanced tx power information
		 * driver need to process addition information
		 * to determine the max channel tx power limits
		 */
		iwl_eeprom_enhanced_txpower(dev, data, eeprom, eeprom_size,
					    n_channels);
	} else {
		/* All others use data from channel map */
		int i;

		data->max_tx_pwr_half_dbm = -128;

		for (i = 0; i < n_channels; i++)
			data->max_tx_pwr_half_dbm =
				max_t(s8, data->max_tx_pwr_half_dbm,
				      data->channels[i].max_power * 2);
	}

	/* Check if we do have HT40 channels */
	if (cfg->eeprom_params->regulatory_bands[5] ==
				EEPROM_REGULATORY_BAND_NO_HT40 &&
	    cfg->eeprom_params->regulatory_bands[6] ==
				EEPROM_REGULATORY_BAND_NO_HT40)
		return n_channels;

	/* Two additional EEPROM bands for 2.4 and 5 GHz HT40 channels */
	for (band = 6; band <= 7; band++) {
		enum nl80211_band ieeeband;

		iwl_init_band_reference(cfg, eeprom, eeprom_size, band,
					&eeprom_ch_count, &eeprom_ch_info,
					&eeprom_ch_array);

		/* EEPROM band 6 is 2.4, band 7 is 5 GHz */
		ieeeband = (band == 6) ? NL80211_BAND_2GHZ
				       : NL80211_BAND_5GHZ;

		/* Loop through each band adding each of the channels */
		for (ch_idx = 0; ch_idx < eeprom_ch_count; ch_idx++) {
			/* Set up driver's info for lower half */
			iwl_mod_ht40_chan_info(dev, data, n_channels, ieeeband,
					       eeprom_ch_array[ch_idx],
					       &eeprom_ch_info[ch_idx],
					       IEEE80211_CHAN_NO_HT40PLUS);

			/* Set up driver's info for upper half */
			iwl_mod_ht40_chan_info(dev, data, n_channels, ieeeband,
					       eeprom_ch_array[ch_idx] + 4,
					       &eeprom_ch_info[ch_idx],
					       IEEE80211_CHAN_NO_HT40MINUS);
		}
	}

	return n_channels;
}
/*
 * EEPROM access time values:
 *
 * Driver initiates EEPROM read by writing byte address << 1 to CSR_EEPROM_REG.
 * Driver then polls CSR_EEPROM_REG for CSR_EEPROM_REG_READ_VALID_MSK (0x1).
 * When polling, wait 10 uSec between polling loops, up to a maximum 5000 uSec.
 * Driver reads 16-bit value from bits 31-16 of CSR_EEPROM_REG.
 */
#define IWL_EEPROM_ACCESS_TIMEOUT	5000 /* uSec */

/*
 * The device's EEPROM semaphore prevents conflicts between driver and uCode
 * when accessing the EEPROM; each access is a series of pulses to/from the
 * EEPROM chip, not a single event, so even reads could conflict if they
 * weren't arbitrated by the semaphore.
 */
#define IWL_EEPROM_SEM_TIMEOUT		10   /* microseconds */
#define IWL_EEPROM_SEM_RETRY_LIMIT	1000 /* number of attempts (not time) */


static int iwl_eeprom_acquire_semaphore(struct iwl_trans *trans)
{
	u16 count;
	int ret;

	for (count = 0; count < IWL_EEPROM_SEM_RETRY_LIMIT; count++) {
		/* Request semaphore */
		iwl_set_bit(trans, CSR_HW_IF_CONFIG_REG,
			    CSR_HW_IF_CONFIG_REG_EEPROM_OWN_SEM);

		/* See if we got it */
		ret = iwl_poll_bit(trans, CSR_HW_IF_CONFIG_REG,
				CSR_HW_IF_CONFIG_REG_EEPROM_OWN_SEM,
				CSR_HW_IF_CONFIG_REG_EEPROM_OWN_SEM,
				IWL_EEPROM_SEM_TIMEOUT);
		if (ret >= 0) {
			IWL_DEBUG_EEPROM(trans->dev,
					 "Acquired semaphore after %d tries.\n",
					 count+1);
			return ret;
		}
	}

	return ret;
}

static void iwl_eeprom_release_semaphore(struct iwl_trans *trans)
{
	iwl_clear_bit(trans, CSR_HW_IF_CONFIG_REG,
		      CSR_HW_IF_CONFIG_REG_EEPROM_OWN_SEM);
}

static int iwl_eeprom_verify_signature(struct iwl_trans *trans, bool nvm_is_otp)
{
	u32 gp = iwl_read32(trans, CSR_EEPROM_GP) & CSR_EEPROM_GP_VALID_MSK;

	IWL_DEBUG_EEPROM(trans->dev, "EEPROM signature=0x%08x\n", gp);

	switch (gp) {
	case CSR_EEPROM_GP_BAD_SIG_EEP_GOOD_SIG_OTP:
		if (!nvm_is_otp) {
			IWL_ERR(trans, "EEPROM with bad signature: 0x%08x\n",
				gp);
			return -ENOENT;
		}
		return 0;
	case CSR_EEPROM_GP_GOOD_SIG_EEP_LESS_THAN_4K:
	case CSR_EEPROM_GP_GOOD_SIG_EEP_MORE_THAN_4K:
		if (nvm_is_otp) {
			IWL_ERR(trans, "OTP with bad signature: 0x%08x\n", gp);
			return -ENOENT;
		}
		return 0;
	case CSR_EEPROM_GP_BAD_SIGNATURE_BOTH_EEP_AND_OTP:
	default:
		IWL_ERR(trans,
			"bad EEPROM/OTP signature, type=%s, EEPROM_GP=0x%08x\n",
			nvm_is_otp ? "OTP" : "EEPROM", gp);
		return -ENOENT;
	}
}

/******************************************************************************
 *
 * OTP related functions
 *
******************************************************************************/

static void iwl_set_otp_access_absolute(struct iwl_trans *trans)
{
	iwl_read32(trans, CSR_OTP_GP_REG);

	iwl_clear_bit(trans, CSR_OTP_GP_REG,
		      CSR_OTP_GP_REG_OTP_ACCESS_MODE);
}

static int iwl_nvm_is_otp(struct iwl_trans *trans)
{
	u32 otpgp;

	/* OTP only valid for CP/PP and after */
	switch (trans->hw_rev & CSR_HW_REV_TYPE_MSK) {
	case CSR_HW_REV_TYPE_NONE:
		IWL_ERR(trans, "Unknown hardware type\n");
		return -EIO;
	case CSR_HW_REV_TYPE_5300:
	case CSR_HW_REV_TYPE_5350:
	case CSR_HW_REV_TYPE_5100:
	case CSR_HW_REV_TYPE_5150:
		return 0;
	default:
		otpgp = iwl_read32(trans, CSR_OTP_GP_REG);
		if (otpgp & CSR_OTP_GP_REG_DEVICE_SELECT)
			return 1;
		return 0;
	}
}

static int iwl_init_otp_access(struct iwl_trans *trans)
{
	int ret;

	ret = iwl_finish_nic_init(trans);
	if (ret)
		return ret;

	iwl_set_bits_prph(trans, APMG_PS_CTRL_REG,
			  APMG_PS_CTRL_VAL_RESET_REQ);
	udelay(5);
	iwl_clear_bits_prph(trans, APMG_PS_CTRL_REG,
			    APMG_PS_CTRL_VAL_RESET_REQ);

	/*
	 * CSR auto clock gate disable bit -
	 * this is only applicable for HW with OTP shadow RAM
	 */
	if (trans->trans_cfg->base_params->shadow_ram_support)
		iwl_set_bit(trans, CSR_DBG_LINK_PWR_MGMT_REG,
			    CSR_RESET_LINK_PWR_MGMT_DISABLED);

	return 0;
}

static int iwl_read_otp_word(struct iwl_trans *trans, u16 addr,
			     __le16 *eeprom_data)
{
	int ret = 0;
	u32 r;
	u32 otpgp;

	iwl_write32(trans, CSR_EEPROM_REG,
		    CSR_EEPROM_REG_MSK_ADDR & (addr << 1));
	ret = iwl_poll_bit(trans, CSR_EEPROM_REG,
				 CSR_EEPROM_REG_READ_VALID_MSK,
				 CSR_EEPROM_REG_READ_VALID_MSK,
				 IWL_EEPROM_ACCESS_TIMEOUT);
	if (ret < 0) {
		IWL_ERR(trans, "Time out reading OTP[%d]\n", addr);
		return ret;
	}
	r = iwl_read32(trans, CSR_EEPROM_REG);
	/* check for ECC errors: */
	otpgp = iwl_read32(trans, CSR_OTP_GP_REG);
	if (otpgp & CSR_OTP_GP_REG_ECC_UNCORR_STATUS_MSK) {
		/* stop in this case */
		/* set the uncorrectable OTP ECC bit for acknowledgment */
		iwl_set_bit(trans, CSR_OTP_GP_REG,
			    CSR_OTP_GP_REG_ECC_UNCORR_STATUS_MSK);
		IWL_ERR(trans, "Uncorrectable OTP ECC error, abort OTP read\n");
		return -EINVAL;
	}
	if (otpgp & CSR_OTP_GP_REG_ECC_CORR_STATUS_MSK) {
		/* continue in this case */
		/* set the correctable OTP ECC bit for acknowledgment */
		iwl_set_bit(trans, CSR_OTP_GP_REG,
			    CSR_OTP_GP_REG_ECC_CORR_STATUS_MSK);
		IWL_ERR(trans, "Correctable OTP ECC error, continue read\n");
	}
	*eeprom_data = cpu_to_le16(r >> 16);
	return 0;
}

/*
 * iwl_is_otp_empty: check for empty OTP
 */
static bool iwl_is_otp_empty(struct iwl_trans *trans)
{
	u16 next_link_addr = 0;
	__le16 link_value;
	bool is_empty = false;

	/* locate the beginning of OTP link list */
	if (!iwl_read_otp_word(trans, next_link_addr, &link_value)) {
		if (!link_value) {
			IWL_ERR(trans, "OTP is empty\n");
			is_empty = true;
		}
	} else {
		IWL_ERR(trans, "Unable to read first block of OTP list.\n");
		is_empty = true;
	}

	return is_empty;
}


/*
 * iwl_find_otp_image: find EEPROM image in OTP
 *   finding the OTP block that contains the EEPROM image.
 *   the last valid block on the link list (the block _before_ the last block)
 *   is the block we should read and used to configure the device.
 *   If all the available OTP blocks are full, the last block will be the block
 *   we should read and used to configure the device.
 *   only perform this operation if shadow RAM is disabled
 */
static int iwl_find_otp_image(struct iwl_trans *trans,
					u16 *validblockaddr)
{
	u16 next_link_addr = 0, valid_addr;
	__le16 link_value = 0;
	int usedblocks = 0;

	/* set addressing mode to absolute to traverse the link list */
	iwl_set_otp_access_absolute(trans);

	/* checking for empty OTP or error */
	if (iwl_is_otp_empty(trans))
		return -EINVAL;

	/*
	 * start traverse link list
	 * until reach the max number of OTP blocks
	 * different devices have different number of OTP blocks
	 */
	do {
		/* save current valid block address
		 * check for more block on the link list
		 */
		valid_addr = next_link_addr;
		next_link_addr = le16_to_cpu(link_value) * sizeof(u16);
		IWL_DEBUG_EEPROM(trans->dev, "OTP blocks %d addr 0x%x\n",
				 usedblocks, next_link_addr);
		if (iwl_read_otp_word(trans, next_link_addr, &link_value))
			return -EINVAL;
		if (!link_value) {
			/*
			 * reach the end of link list, return success and
			 * set address point to the starting address
			 * of the image
			 */
			*validblockaddr = valid_addr;
			/* skip first 2 bytes (link list pointer) */
			*validblockaddr += 2;
			return 0;
		}
		/* more in the link list, continue */
		usedblocks++;
	} while (usedblocks <= trans->trans_cfg->base_params->max_ll_items);

	/* OTP has no valid blocks */
	IWL_DEBUG_EEPROM(trans->dev, "OTP has no valid blocks\n");
	return -EINVAL;
}

/*
 * iwl_read_eeprom - read EEPROM contents
 *
 * Load the EEPROM contents from adapter and return it
 * and its size.
 *
 * NOTE:  This routine uses the non-debug IO access functions.
 */
int iwl_read_eeprom(struct iwl_trans *trans, u8 **eeprom, size_t *eeprom_size)
{
	__le16 *e;
	u32 gp = iwl_read32(trans, CSR_EEPROM_GP);
	int sz;
	int ret;
	u16 addr;
	u16 validblockaddr = 0;
	u16 cache_addr = 0;
	int nvm_is_otp;

	if (!eeprom || !eeprom_size)
		return -EINVAL;

	nvm_is_otp = iwl_nvm_is_otp(trans);
	if (nvm_is_otp < 0)
		return nvm_is_otp;

	sz = trans->trans_cfg->base_params->eeprom_size;
	IWL_DEBUG_EEPROM(trans->dev, "NVM size = %d\n", sz);

	e = kmalloc(sz, GFP_KERNEL);
	if (!e)
		return -ENOMEM;

	ret = iwl_eeprom_verify_signature(trans, nvm_is_otp);
	if (ret < 0) {
		IWL_ERR(trans, "EEPROM not found, EEPROM_GP=0x%08x\n", gp);
		goto err_free;
	}

	/* Make sure driver (instead of uCode) is allowed to read EEPROM */
	ret = iwl_eeprom_acquire_semaphore(trans);
	if (ret < 0) {
		IWL_ERR(trans, "Failed to acquire EEPROM semaphore.\n");
		goto err_free;
	}

	if (nvm_is_otp) {
		ret = iwl_init_otp_access(trans);
		if (ret) {
			IWL_ERR(trans, "Failed to initialize OTP access.\n");
			goto err_unlock;
		}

		iwl_write32(trans, CSR_EEPROM_GP,
			    iwl_read32(trans, CSR_EEPROM_GP) &
			    ~CSR_EEPROM_GP_IF_OWNER_MSK);

		iwl_set_bit(trans, CSR_OTP_GP_REG,
			    CSR_OTP_GP_REG_ECC_CORR_STATUS_MSK |
			    CSR_OTP_GP_REG_ECC_UNCORR_STATUS_MSK);
		/* traversing the linked list if no shadow ram supported */
		if (!trans->trans_cfg->base_params->shadow_ram_support) {
			ret = iwl_find_otp_image(trans, &validblockaddr);
			if (ret)
				goto err_unlock;
		}
		for (addr = validblockaddr; addr < validblockaddr + sz;
		     addr += sizeof(u16)) {
			__le16 eeprom_data;

			ret = iwl_read_otp_word(trans, addr, &eeprom_data);
			if (ret)
				goto err_unlock;
			e[cache_addr / 2] = eeprom_data;
			cache_addr += sizeof(u16);
		}
	} else {
		/* eeprom is an array of 16bit values */
		for (addr = 0; addr < sz; addr += sizeof(u16)) {
			u32 r;

			iwl_write32(trans, CSR_EEPROM_REG,
				    CSR_EEPROM_REG_MSK_ADDR & (addr << 1));

			ret = iwl_poll_bit(trans, CSR_EEPROM_REG,
					   CSR_EEPROM_REG_READ_VALID_MSK,
					   CSR_EEPROM_REG_READ_VALID_MSK,
					   IWL_EEPROM_ACCESS_TIMEOUT);
			if (ret < 0) {
				IWL_ERR(trans,
					"Time out reading EEPROM[%d]\n", addr);
				goto err_unlock;
			}
			r = iwl_read32(trans, CSR_EEPROM_REG);
			e[addr / 2] = cpu_to_le16(r >> 16);
		}
	}

	IWL_DEBUG_EEPROM(trans->dev, "NVM Type: %s\n",
			 nvm_is_otp ? "OTP" : "EEPROM");

	iwl_eeprom_release_semaphore(trans);

	*eeprom_size = sz;
	*eeprom = (u8 *)e;
	return 0;

 err_unlock:
	iwl_eeprom_release_semaphore(trans);
 err_free:
	kfree(e);

	return ret;
}

static void iwl_init_sbands(struct iwl_trans *trans, const struct iwl_cfg *cfg,
			    struct iwl_nvm_data *data,
			    const u8 *eeprom, size_t eeprom_size)
{
	struct device *dev = trans->dev;
	int n_channels = iwl_init_channel_map(dev, cfg, data,
					      eeprom, eeprom_size);
	int n_used = 0;
	struct ieee80211_supported_band *sband;

	sband = &data->bands[NL80211_BAND_2GHZ];
	sband->band = NL80211_BAND_2GHZ;
	sband->bitrates = &iwl_cfg80211_rates[RATES_24_OFFS];
	sband->n_bitrates = N_RATES_24;
	n_used += iwl_init_sband_channels(data, sband, n_channels,
					  NL80211_BAND_2GHZ);
	iwl_init_ht_hw_capab(trans, data, &sband->ht_cap, NL80211_BAND_2GHZ,
			     data->valid_tx_ant, data->valid_rx_ant);

	sband = &data->bands[NL80211_BAND_5GHZ];
	sband->band = NL80211_BAND_5GHZ;
	sband->bitrates = &iwl_cfg80211_rates[RATES_52_OFFS];
	sband->n_bitrates = N_RATES_52;
	n_used += iwl_init_sband_channels(data, sband, n_channels,
					  NL80211_BAND_5GHZ);
	iwl_init_ht_hw_capab(trans, data, &sband->ht_cap, NL80211_BAND_5GHZ,
			     data->valid_tx_ant, data->valid_rx_ant);

	if (n_channels != n_used)
		IWL_ERR_DEV(dev, "EEPROM: used only %d of %d channels\n",
			    n_used, n_channels);
}

/* EEPROM data functions */
struct iwl_nvm_data *
iwl_parse_eeprom_data(struct iwl_trans *trans, const struct iwl_cfg *cfg,
		      const u8 *eeprom, size_t eeprom_size)
{
	struct iwl_nvm_data *data;
	struct device *dev = trans->dev;
	const void *tmp;
	u16 radio_cfg, sku;

	if (WARN_ON(!cfg || !cfg->eeprom_params))
		return NULL;

	data = kzalloc(struct_size(data, channels, IWL_NUM_CHANNELS),
		       GFP_KERNEL);
	if (!data)
		return NULL;

	/* get MAC address(es) */
	tmp = iwl_eeprom_query_addr(eeprom, eeprom_size, EEPROM_MAC_ADDRESS);
	if (!tmp)
		goto err_free;
	memcpy(data->hw_addr, tmp, ETH_ALEN);
	data->n_hw_addrs = iwl_eeprom_query16(eeprom, eeprom_size,
					      EEPROM_NUM_MAC_ADDRESS);

	if (iwl_eeprom_read_calib(eeprom, eeprom_size, data))
		goto err_free;

	tmp = iwl_eeprom_query_addr(eeprom, eeprom_size, EEPROM_XTAL);
	if (!tmp)
		goto err_free;
	memcpy(data->xtal_calib, tmp, sizeof(data->xtal_calib));

	tmp = iwl_eeprom_query_addr(eeprom, eeprom_size,
				    EEPROM_RAW_TEMPERATURE);
	if (!tmp)
		goto err_free;
	data->raw_temperature = *(__le16 *)tmp;

	tmp = iwl_eeprom_query_addr(eeprom, eeprom_size,
				    EEPROM_KELVIN_TEMPERATURE);
	if (!tmp)
		goto err_free;
	data->kelvin_temperature = *(__le16 *)tmp;
	data->kelvin_voltage = *((__le16 *)tmp + 1);

	radio_cfg =
		iwl_eeprom_query16(eeprom, eeprom_size, EEPROM_RADIO_CONFIG);
	data->radio_cfg_dash = EEPROM_RF_CFG_DASH_MSK(radio_cfg);
	data->radio_cfg_pnum = EEPROM_RF_CFG_PNUM_MSK(radio_cfg);
	data->radio_cfg_step = EEPROM_RF_CFG_STEP_MSK(radio_cfg);
	data->radio_cfg_type = EEPROM_RF_CFG_TYPE_MSK(radio_cfg);
	data->valid_rx_ant = EEPROM_RF_CFG_RX_ANT_MSK(radio_cfg);
	data->valid_tx_ant = EEPROM_RF_CFG_TX_ANT_MSK(radio_cfg);

	sku = iwl_eeprom_query16(eeprom, eeprom_size,
				 EEPROM_SKU_CAP);
	data->sku_cap_11n_enable = sku & EEPROM_SKU_CAP_11N_ENABLE;
	data->sku_cap_amt_enable = sku & EEPROM_SKU_CAP_AMT_ENABLE;
	data->sku_cap_band_24ghz_enable = sku & EEPROM_SKU_CAP_BAND_24GHZ;
	data->sku_cap_band_52ghz_enable = sku & EEPROM_SKU_CAP_BAND_52GHZ;
	data->sku_cap_ipan_enable = sku & EEPROM_SKU_CAP_IPAN_ENABLE;
	if (iwlwifi_mod_params.disable_11n & IWL_DISABLE_HT_ALL)
		data->sku_cap_11n_enable = false;

	data->nvm_version = iwl_eeprom_query16(eeprom, eeprom_size,
					       EEPROM_VERSION);

	/* check overrides (some devices have wrong EEPROM) */
	if (cfg->valid_tx_ant)
		data->valid_tx_ant = cfg->valid_tx_ant;
	if (cfg->valid_rx_ant)
		data->valid_rx_ant = cfg->valid_rx_ant;

	if (!data->valid_tx_ant || !data->valid_rx_ant) {
		IWL_ERR_DEV(dev, "invalid antennas (0x%x, 0x%x)\n",
			    data->valid_tx_ant, data->valid_rx_ant);
		goto err_free;
	}

	iwl_init_sbands(trans, cfg, data, eeprom, eeprom_size);

	return data;
 err_free:
	kfree(data);
	return NULL;
}
