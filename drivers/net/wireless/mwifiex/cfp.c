/*
 * Marvell Wireless LAN device driver: Channel, Frequence and Power
 *
 * Copyright (C) 2011, Marvell International Ltd.
 *
 * This software file (the "File") is distributed by Marvell International
 * Ltd. under the terms of the GNU General Public License Version 2, June 1991
 * (the "License").  You may use, redistribute and/or modify this File in
 * accordance with the terms and conditions of the License, a copy of which
 * is available by writing to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA or on the
 * worldwide web at http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt.
 *
 * THE FILE IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND, AND THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE
 * ARE EXPRESSLY DISCLAIMED.  The License provides additional details about
 * this warranty disclaimer.
 */

#include "decl.h"
#include "ioctl.h"
#include "util.h"
#include "fw.h"
#include "main.h"
#include "cfg80211.h"

/* 100mW */
#define MWIFIEX_TX_PWR_DEFAULT     20
/* 100mW */
#define MWIFIEX_TX_PWR_US_DEFAULT      20
/* 50mW */
#define MWIFIEX_TX_PWR_JP_DEFAULT      16
/* 100mW */
#define MWIFIEX_TX_PWR_FR_100MW        20
/* 10mW */
#define MWIFIEX_TX_PWR_FR_10MW         10
/* 100mW */
#define MWIFIEX_TX_PWR_EMEA_DEFAULT    20

static u8 adhoc_rates_b[B_SUPPORTED_RATES] = { 0x82, 0x84, 0x8b, 0x96, 0 };

static u8 adhoc_rates_g[G_SUPPORTED_RATES] = { 0x8c, 0x12, 0x98, 0x24,
					       0xb0, 0x48, 0x60, 0x6c, 0 };

static u8 adhoc_rates_bg[BG_SUPPORTED_RATES] = { 0x82, 0x84, 0x8b, 0x96,
						 0x0c, 0x12, 0x18, 0x24,
						 0x30, 0x48, 0x60, 0x6c, 0 };

static u8 adhoc_rates_a[A_SUPPORTED_RATES] = { 0x8c, 0x12, 0x98, 0x24,
					       0xb0, 0x48, 0x60, 0x6c, 0 };
static u8 supported_rates_a[A_SUPPORTED_RATES] = { 0x0c, 0x12, 0x18, 0x24,
					0xb0, 0x48, 0x60, 0x6c, 0 };
static u16 mwifiex_data_rates[MWIFIEX_SUPPORTED_RATES_EXT] = { 0x02, 0x04,
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

u16 region_code_index[MWIFIEX_MAX_REGION_CODE] = { 0x10, 0x20, 0x30,
						0x32, 0x40, 0x41, 0xff };

static u8 supported_rates_n[N_SUPPORTED_RATES] = { 0x02, 0x04, 0 };

struct region_code_mapping {
	u8 code;
	u8 region[IEEE80211_COUNTRY_STRING_LEN];
};

static struct region_code_mapping region_code_mapping_t[] = {
	{ 0x10, "US " }, /* US FCC */
	{ 0x20, "CA " }, /* IC Canada */
	{ 0x30, "EU " }, /* ETSI */
	{ 0x31, "ES " }, /* Spain */
	{ 0x32, "FR " }, /* France */
	{ 0x40, "JP " }, /* Japan */
	{ 0x41, "JP " }, /* Japan */
	{ 0x50, "CN " }, /* China */
};

/* This function converts integer code to region string */
u8 *mwifiex_11d_code_2_region(u8 code)
{
	u8 i;
	u8 size = sizeof(region_code_mapping_t)/
				sizeof(struct region_code_mapping);

	/* Look for code in mapping table */
	for (i = 0; i < size; i++)
		if (region_code_mapping_t[i].code == code)
			return region_code_mapping_t[i].region;

	return NULL;
}

/*
 * This function maps an index in supported rates table into
 * the corresponding data rate.
 */
u32 mwifiex_index_to_acs_data_rate(struct mwifiex_private *priv,
				   u8 index, u8 ht_info)
{
	/*
	 * For every mcs_rate line, the first 8 bytes are for stream 1x1,
	 * and all 16 bytes are for stream 2x2.
	 */
	u16  mcs_rate[4][16] = {
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
	u16 ac_mcs_rate_nss1[8][10] = {
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
	/* NSS2 note: the value in the table is 2 multiplier of the actual
	 * rate
	 */
	u16 ac_mcs_rate_nss2[8][10] = {
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
	u32 rate = 0;
	u8 mcs_index = 0;
	u8 bw = 0;
	u8 gi = 0;

	if ((ht_info & 0x3) == MWIFIEX_RATE_FORMAT_VHT) {
		mcs_index = min(index & 0xF, 9);

		/* 20M: bw=0, 40M: bw=1, 80M: bw=2, 160M: bw=3 */
		bw = (ht_info & 0xC) >> 2;

		/* LGI: gi =0, SGI: gi = 1 */
		gi = (ht_info & 0x10) >> 4;

		if ((index >> 4) == 1)	/* NSS = 2 */
			rate = ac_mcs_rate_nss2[2 * (3 - bw) + gi][mcs_index];
		else			/* NSS = 1 */
			rate = ac_mcs_rate_nss1[2 * (3 - bw) + gi][mcs_index];
	} else if ((ht_info & 0x3) == MWIFIEX_RATE_FORMAT_HT) {
		/* 20M: bw=0, 40M: bw=1 */
		bw = (ht_info & 0xC) >> 2;

		/* LGI: gi =0, SGI: gi = 1 */
		gi = (ht_info & 0x10) >> 4;

		if (index == MWIFIEX_RATE_BITMAP_MCS0) {
			if (gi == 1)
				rate = 0x0D;    /* MCS 32 SGI rate */
			else
				rate = 0x0C;    /* MCS 32 LGI rate */
		} else if (index < 16) {
			if ((bw == 1) || (bw == 0))
				rate = mcs_rate[2 * (1 - bw) + gi][index];
			else
				rate = mwifiex_data_rates[0];
		} else {
			rate = mwifiex_data_rates[0];
		}
	} else {
		/* 11n non-HT rates */
		if (index >= MWIFIEX_SUPPORTED_RATES_EXT)
			index = 0;
		rate = mwifiex_data_rates[index];
	}

	return rate;
}

/* This function maps an index in supported rates table into
 * the corresponding data rate.
 */
u32 mwifiex_index_to_data_rate(struct mwifiex_private *priv,
			       u8 index, u8 ht_info)
{
	/* For every mcs_rate line, the first 8 bytes are for stream 1x1,
	 * and all 16 bytes are for stream 2x2.
	 */
	u16  mcs_rate[4][16] = {
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
	u32 mcs_num_supp =
		(priv->adapter->hw_dev_mcs_support == HT_STREAM_2X2) ? 16 : 8;
	u32 rate;

	if (priv->adapter->is_hw_11ac_capable)
		return mwifiex_index_to_acs_data_rate(priv, index, ht_info);

	if (ht_info & BIT(0)) {
		if (index == MWIFIEX_RATE_BITMAP_MCS0) {
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
		} else
			rate = mwifiex_data_rates[0];
	} else {
		if (index >= MWIFIEX_SUPPORTED_RATES_EXT)
			index = 0;
		rate = mwifiex_data_rates[index];
	}
	return rate;
}

/*
 * This function returns the current active data rates.
 *
 * The result may vary depending upon connection status.
 */
u32 mwifiex_get_active_data_rates(struct mwifiex_private *priv, u8 *rates)
{
	if (!priv->media_connected)
		return mwifiex_get_supported_rates(priv, rates);
	else
		return mwifiex_copy_rates(rates, 0,
					  priv->curr_bss_params.data_rates,
					  priv->curr_bss_params.num_of_rates);
}

/*
 * This function locates the Channel-Frequency-Power triplet based upon
 * band and channel/frequency parameters.
 */
struct mwifiex_chan_freq_power *
mwifiex_get_cfp(struct mwifiex_private *priv, u8 band, u16 channel, u32 freq)
{
	struct mwifiex_chan_freq_power *cfp = NULL;
	struct ieee80211_supported_band *sband;
	struct ieee80211_channel *ch = NULL;
	int i;

	if (!channel && !freq)
		return cfp;

	if (mwifiex_band_to_radio_type(band) == HostCmd_SCAN_RADIO_TYPE_BG)
		sband = priv->wdev->wiphy->bands[IEEE80211_BAND_2GHZ];
	else
		sband = priv->wdev->wiphy->bands[IEEE80211_BAND_5GHZ];

	if (!sband) {
		dev_err(priv->adapter->dev, "%s: cannot find cfp by band %d\n",
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
			/* find by valid channel*/
			if (ch->hw_value == channel ||
			    channel == FIRST_VALID_CHANNEL)
				break;
		}
	}
	if (i == sband->n_channels) {
		dev_err(priv->adapter->dev, "%s: cannot find cfp by band %d"
			" & channel=%d freq=%d\n", __func__, band, channel,
			freq);
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

/*
 * This function checks if the data rate is set to auto.
 */
u8
mwifiex_is_rate_auto(struct mwifiex_private *priv)
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

/*
 * This function gets the supported data rates.
 *
 * The function works in both Ad-Hoc and infra mode by printing the
 * band and returning the data rates.
 */
u32 mwifiex_get_supported_rates(struct mwifiex_private *priv, u8 *rates)
{
	u32 k = 0;
	struct mwifiex_adapter *adapter = priv->adapter;

	if (priv->bss_mode == NL80211_IFTYPE_STATION) {
		switch (adapter->config_bands) {
		case BAND_B:
			dev_dbg(adapter->dev, "info: infra band=%d "
				"supported_rates_b\n", adapter->config_bands);
			k = mwifiex_copy_rates(rates, k, supported_rates_b,
					       sizeof(supported_rates_b));
			break;
		case BAND_G:
		case BAND_G | BAND_GN:
		case BAND_G | BAND_GN | BAND_GAC:
			dev_dbg(adapter->dev, "info: infra band=%d "
				"supported_rates_g\n", adapter->config_bands);
			k = mwifiex_copy_rates(rates, k, supported_rates_g,
					       sizeof(supported_rates_g));
			break;
		case BAND_B | BAND_G:
		case BAND_A | BAND_B | BAND_G:
		case BAND_A | BAND_B:
		case BAND_A | BAND_B | BAND_G | BAND_GN | BAND_AN:
		case BAND_A | BAND_B | BAND_G | BAND_GN | BAND_AN | BAND_AAC:
		case BAND_A | BAND_B | BAND_G | BAND_GN | BAND_AN |
		     BAND_AAC | BAND_GAC:
		case BAND_B | BAND_G | BAND_GN:
		case BAND_B | BAND_G | BAND_GN | BAND_GAC:
			dev_dbg(adapter->dev, "info: infra band=%d "
				"supported_rates_bg\n", adapter->config_bands);
			k = mwifiex_copy_rates(rates, k, supported_rates_bg,
					       sizeof(supported_rates_bg));
			break;
		case BAND_A:
		case BAND_A | BAND_G:
			dev_dbg(adapter->dev, "info: infra band=%d "
				"supported_rates_a\n", adapter->config_bands);
			k = mwifiex_copy_rates(rates, k, supported_rates_a,
					       sizeof(supported_rates_a));
			break;
		case BAND_AN:
		case BAND_A | BAND_AN:
		case BAND_A | BAND_AN | BAND_AAC:
		case BAND_A | BAND_G | BAND_AN | BAND_GN:
		case BAND_A | BAND_G | BAND_AN | BAND_GN | BAND_AAC:
			dev_dbg(adapter->dev, "info: infra band=%d "
				"supported_rates_a\n", adapter->config_bands);
			k = mwifiex_copy_rates(rates, k, supported_rates_a,
					       sizeof(supported_rates_a));
			break;
		case BAND_GN:
		case BAND_GN | BAND_GAC:
			dev_dbg(adapter->dev, "info: infra band=%d "
				"supported_rates_n\n", adapter->config_bands);
			k = mwifiex_copy_rates(rates, k, supported_rates_n,
					       sizeof(supported_rates_n));
			break;
		}
	} else {
		/* Ad-hoc mode */
		switch (adapter->adhoc_start_band) {
		case BAND_B:
			dev_dbg(adapter->dev, "info: adhoc B\n");
			k = mwifiex_copy_rates(rates, k, adhoc_rates_b,
					       sizeof(adhoc_rates_b));
			break;
		case BAND_G:
		case BAND_G | BAND_GN:
			dev_dbg(adapter->dev, "info: adhoc G only\n");
			k = mwifiex_copy_rates(rates, k, adhoc_rates_g,
					       sizeof(adhoc_rates_g));
			break;
		case BAND_B | BAND_G:
		case BAND_B | BAND_G | BAND_GN:
			dev_dbg(adapter->dev, "info: adhoc BG\n");
			k = mwifiex_copy_rates(rates, k, adhoc_rates_bg,
					       sizeof(adhoc_rates_bg));
			break;
		case BAND_A:
		case BAND_A | BAND_AN:
			dev_dbg(adapter->dev, "info: adhoc A\n");
			k = mwifiex_copy_rates(rates, k, adhoc_rates_a,
					       sizeof(adhoc_rates_a));
			break;
		}
	}

	return k;
}
