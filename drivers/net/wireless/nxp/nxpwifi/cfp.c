// SPDX-License-Identifier: GPL-2.0-only
/*
 * nxpwifi: Channel, Frequency and Power
 *
 * Copyright 2011-2024 NXP
 */

#include "cfg.h"
#include "util.h"
#include "fw.h"
#include "main.h"
#include "cfg80211.h"

/* 100mW */
#define NXPWIFI_TX_PWR_DEFAULT         20
/* 100mW */
#define NXPWIFI_TX_PWR_US_DEFAULT      20
/* 50mW */
#define NXPWIFI_TX_PWR_JP_DEFAULT      16
/* 100mW */
#define NXPWIFI_TX_PWR_FR_100MW        20
/* 10mW */
#define NXPWIFI_TX_PWR_FR_10MW         10
/* 100mW */
#define NXPWIFI_TX_PWR_EMEA_DEFAULT    20

static u8 supported_rates_a[A_SUPPORTED_RATES] = { 0x0c, 0x12, 0x18, 0x24,
					0xb0, 0x48, 0x60, 0x6c, 0 };
static u16 nxpwifi_data_rates[NXPWIFI_SUPPORTED_RATES_EXT] = { 0x02, 0x04,
					0x0B, 0x16, 0x00, 0x0C, 0x12, 0x18,
					0x24, 0x30, 0x48, 0x60, 0x6C, 0x90,
					0x0D, 0x1A, 0x27, 0x34, 0x4E, 0x68,
					0x75, 0x82, 0x0C, 0x1B, 0x36, 0x51,
					0x6C, 0xA2, 0xD8, 0xF3, 0x10E, 0x00 };

static u8 supported_rates_b[B_SUPPORTED_RATES] = { 0x02, 0x04, 0x0b, 0x16, 0 };

static u8 supported_rates_g[G_SUPPORTED_RATES] = { 0x0c, 0x12, 0x18, 0x24,
					0x30, 0x48, 0x60, 0x6c, 0 };

static u8 supported_rates_bg[BG_SUPPORTED_RATES] = { 0x02, 0x04, 0x0b, 0x0c,
					0x12, 0x16, 0x18, 0x24, 0x30, 0x48,
					0x60, 0x6c, 0 };

/* mcs_rate: first 8 entries for 1x1; all 16 for 2x2. */
static const u16 mcs_rate[4][16] = {
	/* LGI 40M */
	{ 0x1b, 0x36, 0x51, 0x6c, 0xa2, 0xd8, 0xf3, 0x10e,
	  0x36, 0x6c, 0xa2, 0xd8, 0x144, 0x1b0, 0x1e6, 0x21c },

	/* SGI 40M */
	{ 0x1e, 0x3c, 0x5a, 0x78, 0xb4, 0xf0, 0x10e, 0x12c,
	  0x3c, 0x78, 0xb4, 0xf0, 0x168, 0x1e0, 0x21c, 0x258 },

	/* LGI 20M */
	{ 0x0d, 0x1a, 0x27, 0x34, 0x4e, 0x68, 0x75, 0x82,
	  0x1a, 0x34, 0x4e, 0x68, 0x9c, 0xd0, 0xea, 0x104 },

	/* SGI 20M */
	{ 0x0e, 0x1c, 0x2b, 0x39, 0x56, 0x73, 0x82, 0x90,
	  0x1c, 0x39, 0x56, 0x73, 0xad, 0xe7, 0x104, 0x120 }
};

/* AC rates */
static const u16 ac_mcs_rate_nss1[8][10] = {
	/* LG 160M */
	{ 0x75, 0xEA, 0x15F, 0x1D4, 0x2BE, 0x3A8, 0x41D,
	  0x492, 0x57C, 0x618 },

	/* SG 160M */
	{ 0x82, 0x104, 0x186, 0x208, 0x30C, 0x410, 0x492,
	  0x514, 0x618, 0x6C6 },

	/* LG 80M */
	{ 0x3B, 0x75, 0xB0, 0xEA, 0x15F, 0x1D4, 0x20F,
	  0x249, 0x2BE, 0x30C },

	/* SG 80M */
	{ 0x41, 0x82, 0xC3, 0x104, 0x186, 0x208, 0x249,
	  0x28A, 0x30C, 0x363 },

	/* LG 40M */
	{ 0x1B, 0x36, 0x51, 0x6C, 0xA2, 0xD8, 0xF3,
	  0x10E, 0x144, 0x168 },

	/* SG 40M */
	{ 0x1E, 0x3C, 0x5A, 0x78, 0xB4, 0xF0, 0x10E,
	  0x12C, 0x168, 0x190 },

	/* LG 20M */
	{ 0xD, 0x1A, 0x27, 0x34, 0x4E, 0x68, 0x75, 0x82, 0x9C, 0x00 },

	/* SG 20M */
	{ 0xF, 0x1D, 0x2C, 0x3A, 0x57, 0x74, 0x82, 0x91, 0xAE, 0x00 },
};

/* NSS2 note: the value in the table is 2 multiplier of the actual rate */
static const u16 ac_mcs_rate_nss2[8][10] = {
	/* LG 160M */
	{ 0xEA, 0x1D4, 0x2BE, 0x3A8, 0x57C, 0x750, 0x83A,
	  0x924, 0xAF8, 0xC30 },

	/* SG 160M */
	{ 0x104, 0x208, 0x30C, 0x410, 0x618, 0x820, 0x924,
	  0xA28, 0xC30, 0xD8B },

	/* LG 80M */
	{ 0x75, 0xEA, 0x15F, 0x1D4, 0x2BE, 0x3A8, 0x41D,
	  0x492, 0x57C, 0x618 },

	/* SG 80M */
	{ 0x82, 0x104, 0x186, 0x208, 0x30C, 0x410, 0x492,
	  0x514, 0x618, 0x6C6 },

	/* LG 40M */
	{ 0x36, 0x6C, 0xA2, 0xD8, 0x144, 0x1B0, 0x1E6,
	  0x21C, 0x288, 0x2D0 },

	/* SG 40M */
	{ 0x3C, 0x78, 0xB4, 0xF0, 0x168, 0x1E0, 0x21C,
	  0x258, 0x2D0, 0x320 },

	/* LG 20M */
	{ 0x1A, 0x34, 0x4A, 0x68, 0x9C, 0xD0, 0xEA, 0x104,
	  0x138, 0x00 },

	/* SG 20M */
	{ 0x1D, 0x3A, 0x57, 0x74, 0xAE, 0xE6, 0x104, 0x121,
	  0x15B, 0x00 },
};

struct region_code_mapping {
	u8 code;
	u8 region[IEEE80211_COUNTRY_STRING_LEN];
};

static struct region_code_mapping region_code_mapping_t[] = {
	{ 0x10, "US " }, /* US FCC */
	{ 0x20, "CA " }, /* IC Canada */
	{ 0x30, "FR " }, /* France */
	{ 0x31, "ES " }, /* Spain */
	{ 0x32, "FR " }, /* France */
	{ 0x40, "JP " }, /* Japan */
	{ 0x41, "JP " }, /* Japan */
	{ 0x50, "CN " }, /* China */
};

/* Convert 11d country code to region string. */
u8 *nxpwifi_11d_code_2_region(u8 code)
{
	u8 i;

	/* Look for code in mapping table */
	for (i = 0; i < ARRAY_SIZE(region_code_mapping_t); i++)
		if (region_code_mapping_t[i].code == code)
			return region_code_mapping_t[i].region;

	return NULL;
}

/* Map supported rate index to AC/VHT data rate. */
u32 nxpwifi_index_to_acs_data_rate(struct nxpwifi_private *priv,
				   u8 index, u8 ht_info)
{
	u32 rate = 0;
	u8 mcs_index = 0;
	u8 bw = 0;
	u8 gi = 0;

	if ((ht_info & 0x3) == NXPWIFI_RATE_FORMAT_VHT) {
		mcs_index = min(index & 0xF, 9);

		/* 20M: bw=0, 40M: bw=1, 80M: bw=2, 160M: bw=3 */
		bw = (ht_info & 0xC) >> 2;

		/* LGI: gi =0, SGI: gi = 1 */
		gi = (ht_info & 0x10) >> 4;

		if ((index >> 4) == 1)	/* NSS = 2 */
			rate = ac_mcs_rate_nss2[2 * (3 - bw) + gi][mcs_index];
		else			/* NSS = 1 */
			rate = ac_mcs_rate_nss1[2 * (3 - bw) + gi][mcs_index];
	} else if ((ht_info & 0x3) == NXPWIFI_RATE_FORMAT_HT) {
		/* 20M: bw=0, 40M: bw=1 */
		bw = (ht_info & 0xC) >> 2;

		/* LGI: gi =0, SGI: gi = 1 */
		gi = (ht_info & 0x10) >> 4;

		if (index == NXPWIFI_RATE_BITMAP_MCS0) {
			if (gi == 1)
				rate = 0x0D;    /* MCS 32 SGI rate */
			else
				rate = 0x0C;    /* MCS 32 LGI rate */
		} else if (index < 16) {
			if (bw == 1 || bw == 0)
				rate = mcs_rate[2 * (1 - bw) + gi][index];
			else
				rate = nxpwifi_data_rates[0];
		} else {
			rate = nxpwifi_data_rates[0];
		}
	} else {
		/* 11n non-HT rates */
		if (index >= NXPWIFI_SUPPORTED_RATES_EXT)
			index = 0;
		rate = nxpwifi_data_rates[index];
	}

	return rate;
}

/* Map supported rate index to data rate. */
u32 nxpwifi_index_to_data_rate(struct nxpwifi_private *priv,
			       u8 index, u8 ht_info)
{
	u32 mcs_num_supp =
		(priv->adapter->user_dev_mcs_support == HT_STREAM_2X2) ? 16 : 8;
	u32 rate;

	if (priv->adapter->is_hw_11ac_capable)
		return nxpwifi_index_to_acs_data_rate(priv, index, ht_info);

	if (ht_info & BIT(0)) {
		if (index == NXPWIFI_RATE_BITMAP_MCS0) {
			if (ht_info & BIT(2))
				rate = 0x0D;	/* MCS 32 SGI rate */
			else
				rate = 0x0C;	/* MCS 32 LGI rate */
		} else if (index < mcs_num_supp) {
			if (ht_info & BIT(1)) {
				if (ht_info & BIT(2))
					/* SGI, 40M */
					rate = mcs_rate[1][index];
				else
					/* LGI, 40M */
					rate = mcs_rate[0][index];
			} else {
				if (ht_info & BIT(2))
					/* SGI, 20M */
					rate = mcs_rate[3][index];
				else
					/* LGI, 20M */
					rate = mcs_rate[2][index];
			}
		} else {
			rate = nxpwifi_data_rates[0];
		}
	} else {
		if (index >= NXPWIFI_SUPPORTED_RATES_EXT)
			index = 0;
		rate = nxpwifi_data_rates[index];
	}
	return rate;
}

/* Return current active data rates (depends on connection). */
u32 nxpwifi_get_active_data_rates(struct nxpwifi_private *priv, u8 *rates)
{
	if (!priv->media_connected)
		return nxpwifi_get_supported_rates(priv, rates);
	else
		return nxpwifi_copy_rates(rates, 0,
					  priv->curr_bss_params.data_rates,
					  priv->curr_bss_params.num_of_rates);
}

/* Find Channel/Frequency/Power by band and channel or frequency. */
struct nxpwifi_chan_freq_power *
nxpwifi_get_cfp(struct nxpwifi_private *priv, u8 band, u16 channel, u32 freq)
{
	struct nxpwifi_chan_freq_power *cfp = NULL;
	struct ieee80211_supported_band *sband;
	struct ieee80211_channel *ch = NULL;
	int i;

	if (!channel && !freq)
		return cfp;

	if (nxpwifi_band_to_radio_type(band) == HOST_SCAN_RADIO_TYPE_BG)
		sband = priv->wdev.wiphy->bands[NL80211_BAND_2GHZ];
	else
		sband = priv->wdev.wiphy->bands[NL80211_BAND_5GHZ];

	if (!sband) {
		nxpwifi_dbg(priv->adapter, ERROR,
			    "%s: cannot find cfp by band %d\n",
			    __func__, band);
		return cfp;
	}

	for (i = 0; i < sband->n_channels; i++) {
		ch = &sband->channels[i];

		if (ch->flags & IEEE80211_CHAN_DISABLED)
			continue;

		if (freq) {
			if (ch->center_freq == freq)
				break;
		} else {
			/* Find by valid channel. */
			if (ch->hw_value == channel ||
			    channel == FIRST_VALID_CHANNEL)
				break;
		}
	}
	if (i == sband->n_channels) {
		nxpwifi_dbg(priv->adapter, WARN,
			    "%s: cannot find cfp by band %d\t"
			    "& channel=%d freq=%d\n",
			    __func__, band, channel, freq);
	} else {
		if (!ch)
			return cfp;

		priv->cfp.channel = ch->hw_value;
		priv->cfp.freq = ch->center_freq;
		priv->cfp.max_tx_power = ch->max_power;
		cfp = &priv->cfp;
	}

	return cfp;
}

/* Return true if data rate is set to auto. */
u8
nxpwifi_is_rate_auto(struct nxpwifi_private *priv)
{
	u32 i;
	int rate_num = 0;

	for (i = 0; i < ARRAY_SIZE(priv->bitmap_rates); i++)
		if (priv->bitmap_rates[i])
			rate_num++;

	if (rate_num > 1)
		return true;
	else
		return false;
}

/* Extract supported rates from cfg80211_scan_request bitmask. */
u32 nxpwifi_get_rates_from_cfg80211(struct nxpwifi_private *priv,
				    u8 *rates, u8 radio_type)
{
	struct wiphy *wiphy = priv->adapter->wiphy;
	struct cfg80211_scan_request *request = priv->scan_request;
	u32 num_rates, rate_mask;
	struct ieee80211_supported_band *sband;
	int i;

	if (radio_type) {
		sband = wiphy->bands[NL80211_BAND_5GHZ];
		if (WARN_ON_ONCE(!sband))
			return 0;
		rate_mask = request->rates[NL80211_BAND_5GHZ];
	} else {
		sband = wiphy->bands[NL80211_BAND_2GHZ];
		if (WARN_ON_ONCE(!sband))
			return 0;
		rate_mask = request->rates[NL80211_BAND_2GHZ];
	}

	num_rates = 0;
	for (i = 0; i < sband->n_bitrates; i++) {
		if ((BIT(i) & rate_mask) == 0)
			continue; /* skip rate */
		rates[num_rates++] = (u8)(sband->bitrates[i].bitrate / 5);
	}

	return num_rates;
}

/* Convert config_bands to B/G/A band */
static u16 nxpwifi_convert_config_bands(u16 config_bands)
{
	u16 bands = 0;

	if (config_bands & BAND_B)
		bands |= BAND_B;
	if (config_bands & BAND_G || config_bands & BAND_GN ||
	    config_bands & BAND_GAC || config_bands & BAND_GAX)
		bands |= BAND_G;
	if (config_bands & BAND_A || config_bands & BAND_AN ||
	    config_bands & BAND_AAC || config_bands & BAND_AAX)
		bands |= BAND_A;

	return bands;
}

/* Get supported rates in infrastructure (STA/P2P client) mode. */
u32 nxpwifi_get_supported_rates(struct nxpwifi_private *priv, u8 *rates)
{
	struct nxpwifi_adapter *adapter = priv->adapter;
	u32 k = 0;
	u16 bands = 0;

	bands = nxpwifi_convert_config_bands(adapter->fw_bands);

	if (priv->bss_mode == NL80211_IFTYPE_STATION) {
		if (bands == BAND_B) {
			/* B only */
			nxpwifi_dbg(adapter, INFO, "info: infra band=%d\t"
				    "supported_rates_b\n",
				    priv->config_bands);
			k = nxpwifi_copy_rates(rates, k, supported_rates_b,
					       sizeof(supported_rates_b));
		} else if (bands == BAND_G) {
			/* G only */
			nxpwifi_dbg(adapter, INFO, "info: infra band=%d\t"
				    "supported_rates_g\n",
				    priv->config_bands);
			k = nxpwifi_copy_rates(rates, k, supported_rates_g,
					       sizeof(supported_rates_g));
		} else if (bands & (BAND_B | BAND_G)) {
			/* BG only */
			nxpwifi_dbg(adapter, INFO, "info: infra band=%d\t"
				    "supported_rates_bg\n",
				    priv->config_bands);
			k = nxpwifi_copy_rates(rates, k, supported_rates_bg,
					       sizeof(supported_rates_bg));
		} else if (bands & BAND_A) {
			/* support A */
			nxpwifi_dbg(adapter, INFO, "info: infra band=%d\t"
				    "supported_rates_a\n",
				    priv->config_bands);
			k = nxpwifi_copy_rates(rates, k, supported_rates_a,
					       sizeof(supported_rates_a));
		}
	}

	return k;
}

u8 nxpwifi_adjust_data_rate(struct nxpwifi_private *priv,
			    u8 rx_rate, u8 rate_info)
{
	u8 rate_index = 0;

	/* HT40 */
	if ((rate_info & BIT(0)) && (rate_info & BIT(1)))
		rate_index = NXPWIFI_RATE_INDEX_MCS0 +
			     NXPWIFI_BW20_MCS_NUM + rx_rate;
	else if (rate_info & BIT(0)) /* HT20 */
		rate_index = NXPWIFI_RATE_INDEX_MCS0 + rx_rate;
	else
		rate_index = (rx_rate > NXPWIFI_RATE_INDEX_OFDM0) ?
			      rx_rate - 1 : rx_rate;

	if (rate_index >= NXPWIFI_MAX_AC_RX_RATES)
		rate_index = NXPWIFI_MAX_AC_RX_RATES - 1;

	return rate_index;
}

/* Check if the given region code is a valid NXP-defined region code.
 * Valid codes are defined by the FW v18 region enum in IEEE_types.h:
 *   0x00 (World), 0x10 (US/FCC), 0x20 (Canada/IC), 0x30 (ETSI),
 *   0x31 (Spain), 0x32 (France), 0x40 (Japan), 0x41 (Japan1), 0x50 (China)
 */
bool nxpwifi_is_valid_region_code(enum nxpwifi_region_code code)
{
	switch (code) {
	case NXPWIFI_REGION_WORLD:
	case NXPWIFI_REGION_FCC:
	case NXPWIFI_REGION_IC:
	case NXPWIFI_REGION_ETSI:
	case NXPWIFI_REGION_SPAIN:
	case NXPWIFI_REGION_FRANCE:
	case NXPWIFI_REGION_JAPAN:
	case NXPWIFI_REGION_JAPAN1:
	case NXPWIFI_REGION_CHINA:
		return true;
	default:
		return false;
	}
}
