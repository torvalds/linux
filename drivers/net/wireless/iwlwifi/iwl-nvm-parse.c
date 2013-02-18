/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2008 - 2013 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110,
 * USA
 *
 * The full GNU General Public License is included in this distribution
 * in the file called COPYING.
 *
 * Contact Information:
 *  Intel Linux Wireless <ilw@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
 * Copyright(c) 2005 - 2013 Intel Corporation. All rights reserved.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *****************************************************************************/
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/export.h>
#include "iwl-modparams.h"
#include "iwl-nvm-parse.h"

/* NVM offsets (in words) definitions */
enum wkp_nvm_offsets {
	/* NVM HW-Section offset (in words) definitions */
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
	XTAL_CALIB = 0x316 - NVM_CALIB_SECTION
};

/* SKU Capabilities (actual values from NVM definition) */
enum nvm_sku_bits {
	NVM_SKU_CAP_BAND_24GHZ	= BIT(0),
	NVM_SKU_CAP_BAND_52GHZ	= BIT(1),
	NVM_SKU_CAP_11N_ENABLE	= BIT(2),
};

/* radio config bits (actual values from NVM definition) */
#define NVM_RF_CFG_DASH_MSK(x)   (x & 0x3)         /* bits 0-1   */
#define NVM_RF_CFG_STEP_MSK(x)   ((x >> 2)  & 0x3) /* bits 2-3   */
#define NVM_RF_CFG_TYPE_MSK(x)   ((x >> 4)  & 0x3) /* bits 4-5   */
#define NVM_RF_CFG_PNUM_MSK(x)   ((x >> 6)  & 0x3) /* bits 6-7   */
#define NVM_RF_CFG_TX_ANT_MSK(x) ((x >> 8)  & 0xF) /* bits 8-11  */
#define NVM_RF_CFG_RX_ANT_MSK(x) ((x >> 12) & 0xF) /* bits 12-15 */

/*
 * These are the channel numbers in the order that they are stored in the NVM
 */
static const u8 iwl_nvm_channels[] = {
	/* 2.4 GHz */
	1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
	/* 5 GHz */
	36, 40, 44 , 48, 52, 56, 60, 64,
	100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 144,
	149, 153, 157, 161, 165
};

#define IWL_NUM_CHANNELS	ARRAY_SIZE(iwl_nvm_channels)
#define NUM_2GHZ_CHANNELS	14
#define FIRST_2GHZ_HT_MINUS	5
#define LAST_2GHZ_HT_PLUS	9
#define LAST_5GHZ_HT		161


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
 * @NVM_CHANNEL_DFS: dynamic freq selection candidate
 * @NVM_CHANNEL_WIDE: 20 MHz channel okay (?)
 * @NVM_CHANNEL_40MHZ: 40 MHz channel okay (?)
 */
enum iwl_nvm_channel_flags {
	NVM_CHANNEL_VALID = BIT(0),
	NVM_CHANNEL_IBSS = BIT(1),
	NVM_CHANNEL_ACTIVE = BIT(3),
	NVM_CHANNEL_RADAR = BIT(4),
	NVM_CHANNEL_DFS = BIT(7),
	NVM_CHANNEL_WIDE = BIT(8),
	NVM_CHANNEL_40MHZ = BIT(9),
};

#define CHECK_AND_PRINT_I(x)	\
	((ch_flags & NVM_CHANNEL_##x) ? # x " " : "")

static int iwl_init_channel_map(struct device *dev, const struct iwl_cfg *cfg,
				struct iwl_nvm_data *data,
				const __le16 * const nvm_ch_flags)
{
	int ch_idx;
	int n_channels = 0;
	struct ieee80211_channel *channel;
	u16 ch_flags;
	bool is_5ghz;

	for (ch_idx = 0; ch_idx < IWL_NUM_CHANNELS; ch_idx++) {
		ch_flags = __le16_to_cpup(nvm_ch_flags + ch_idx);
		if (!(ch_flags & NVM_CHANNEL_VALID)) {
			IWL_DEBUG_EEPROM(dev,
					 "Ch. %d Flags %x [%sGHz] - No traffic\n",
					 iwl_nvm_channels[ch_idx],
					 ch_flags,
					 (ch_idx >= NUM_2GHZ_CHANNELS) ?
					 "5.2" : "2.4");
			continue;
		}

		channel = &data->channels[n_channels];
		n_channels++;

		channel->hw_value = iwl_nvm_channels[ch_idx];
		channel->band = (ch_idx < NUM_2GHZ_CHANNELS) ?
				IEEE80211_BAND_2GHZ : IEEE80211_BAND_5GHZ;
		channel->center_freq =
			ieee80211_channel_to_frequency(
				channel->hw_value, channel->band);

		/* TODO: Need to be dependent to the NVM */
		channel->flags = IEEE80211_CHAN_NO_HT40;
		if (ch_idx < NUM_2GHZ_CHANNELS &&
		    (ch_flags & NVM_CHANNEL_40MHZ)) {
			if (iwl_nvm_channels[ch_idx] <= LAST_2GHZ_HT_PLUS)
				channel->flags &= ~IEEE80211_CHAN_NO_HT40PLUS;
			if (iwl_nvm_channels[ch_idx] >= FIRST_2GHZ_HT_MINUS)
				channel->flags &= ~IEEE80211_CHAN_NO_HT40MINUS;
		} else if (iwl_nvm_channels[ch_idx] <= LAST_5GHZ_HT &&
			   (ch_flags & NVM_CHANNEL_40MHZ)) {
			if ((ch_idx - NUM_2GHZ_CHANNELS) % 2 == 0)
				channel->flags &= ~IEEE80211_CHAN_NO_HT40PLUS;
			else
				channel->flags &= ~IEEE80211_CHAN_NO_HT40MINUS;
		}

		if (!(ch_flags & NVM_CHANNEL_IBSS))
			channel->flags |= IEEE80211_CHAN_NO_IBSS;

		if (!(ch_flags & NVM_CHANNEL_ACTIVE))
			channel->flags |= IEEE80211_CHAN_PASSIVE_SCAN;

		if (ch_flags & NVM_CHANNEL_RADAR)
			channel->flags |= IEEE80211_CHAN_RADAR;

		/* Initialize regulatory-based run-time data */

		/* TODO: read the real value from the NVM */
		channel->max_power = 0;
		is_5ghz = channel->band == IEEE80211_BAND_5GHZ;
		IWL_DEBUG_EEPROM(dev,
				 "Ch. %d [%sGHz] %s%s%s%s%s%s(0x%02x %ddBm): Ad-Hoc %ssupported\n",
				 channel->hw_value,
				 is_5ghz ? "5.2" : "2.4",
				 CHECK_AND_PRINT_I(VALID),
				 CHECK_AND_PRINT_I(IBSS),
				 CHECK_AND_PRINT_I(ACTIVE),
				 CHECK_AND_PRINT_I(RADAR),
				 CHECK_AND_PRINT_I(WIDE),
				 CHECK_AND_PRINT_I(DFS),
				 ch_flags,
				 channel->max_power,
				 ((ch_flags & NVM_CHANNEL_IBSS) &&
				  !(ch_flags & NVM_CHANNEL_RADAR))
					? "" : "not ");
	}

	return n_channels;
}

static void iwl_init_sbands(struct device *dev, const struct iwl_cfg *cfg,
			    struct iwl_nvm_data *data, const __le16 *nvm_sw)
{
	int n_channels = iwl_init_channel_map(dev, cfg, data,
			&nvm_sw[NVM_CHANNELS]);
	int n_used = 0;
	struct ieee80211_supported_band *sband;

	sband = &data->bands[IEEE80211_BAND_2GHZ];
	sband->band = IEEE80211_BAND_2GHZ;
	sband->bitrates = &iwl_cfg80211_rates[RATES_24_OFFS];
	sband->n_bitrates = N_RATES_24;
	n_used += iwl_init_sband_channels(data, sband, n_channels,
					  IEEE80211_BAND_2GHZ);
	iwl_init_ht_hw_capab(cfg, data, &sband->ht_cap, IEEE80211_BAND_2GHZ);

	sband = &data->bands[IEEE80211_BAND_5GHZ];
	sband->band = IEEE80211_BAND_5GHZ;
	sband->bitrates = &iwl_cfg80211_rates[RATES_52_OFFS];
	sband->n_bitrates = N_RATES_52;
	n_used += iwl_init_sband_channels(data, sband, n_channels,
					  IEEE80211_BAND_5GHZ);
	iwl_init_ht_hw_capab(cfg, data, &sband->ht_cap, IEEE80211_BAND_5GHZ);

	if (n_channels != n_used)
		IWL_ERR_DEV(dev, "NVM: used only %d of %d channels\n",
			    n_used, n_channels);
}

struct iwl_nvm_data *
iwl_parse_nvm_data(struct device *dev, const struct iwl_cfg *cfg,
		   const __le16 *nvm_hw, const __le16 *nvm_sw,
		   const __le16 *nvm_calib)
{
	struct iwl_nvm_data *data;
	u8 hw_addr[ETH_ALEN];
	u16 radio_cfg, sku;

	data = kzalloc(sizeof(*data) +
		       sizeof(struct ieee80211_channel) * IWL_NUM_CHANNELS,
		       GFP_KERNEL);
	if (!data)
		return NULL;

	data->nvm_version = le16_to_cpup(nvm_sw + NVM_VERSION);

	radio_cfg = le16_to_cpup(nvm_sw + RADIO_CFG);
	data->radio_cfg_type = NVM_RF_CFG_TYPE_MSK(radio_cfg);
	data->radio_cfg_step = NVM_RF_CFG_STEP_MSK(radio_cfg);
	data->radio_cfg_dash = NVM_RF_CFG_DASH_MSK(radio_cfg);
	data->radio_cfg_pnum = NVM_RF_CFG_PNUM_MSK(radio_cfg);
	data->valid_tx_ant = NVM_RF_CFG_TX_ANT_MSK(radio_cfg);
	data->valid_rx_ant = NVM_RF_CFG_RX_ANT_MSK(radio_cfg);

	sku = le16_to_cpup(nvm_sw + SKU);
	data->sku_cap_band_24GHz_enable = sku & NVM_SKU_CAP_BAND_24GHZ;
	data->sku_cap_band_52GHz_enable = sku & NVM_SKU_CAP_BAND_52GHZ;
	data->sku_cap_11n_enable = sku & NVM_SKU_CAP_11N_ENABLE;
	if (iwlwifi_mod_params.disable_11n & IWL_DISABLE_HT_ALL)
		data->sku_cap_11n_enable = false;

	/* check overrides (some devices have wrong NVM) */
	if (cfg->valid_tx_ant)
		data->valid_tx_ant = cfg->valid_tx_ant;
	if (cfg->valid_rx_ant)
		data->valid_rx_ant = cfg->valid_rx_ant;

	if (!data->valid_tx_ant || !data->valid_rx_ant) {
		IWL_ERR_DEV(dev, "invalid antennas (0x%x, 0x%x)\n",
			    data->valid_tx_ant, data->valid_rx_ant);
		kfree(data);
		return NULL;
	}

	data->n_hw_addrs = le16_to_cpup(nvm_sw + N_HW_ADDRS);

	data->xtal_calib[0] = *(nvm_calib + XTAL_CALIB);
	data->xtal_calib[1] = *(nvm_calib + XTAL_CALIB + 1);

	/* The byte order is little endian 16 bit, meaning 214365 */
	memcpy(hw_addr, nvm_hw + HW_ADDR, ETH_ALEN);
	data->hw_addr[0] = hw_addr[1];
	data->hw_addr[1] = hw_addr[0];
	data->hw_addr[2] = hw_addr[3];
	data->hw_addr[3] = hw_addr[2];
	data->hw_addr[4] = hw_addr[5];
	data->hw_addr[5] = hw_addr[4];

	iwl_init_sbands(dev, cfg, data, nvm_sw);

	data->calib_version = 255;   /* TODO:
					this value will prevent some checks from
					failing, we need to check if this
					field is still needed, and if it does,
					where is it in the NVM*/

	return data;
}
EXPORT_SYMBOL_GPL(iwl_parse_nvm_data);
