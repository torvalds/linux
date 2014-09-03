/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2008 - 2014 Intel Corporation. All rights reserved.
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
 * Copyright(c) 2005 - 2014 Intel Corporation. All rights reserved.
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
#include <linux/etherdevice.h>
#include <linux/pci.h>
#include "iwl-drv.h"
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

enum family_8000_nvm_offsets {
	/* NVM HW-Section offset (in words) definitions */
	HW_ADDR0_WFPM_FAMILY_8000 = 0x12,
	HW_ADDR1_WFPM_FAMILY_8000 = 0x16,
	HW_ADDR0_PCIE_FAMILY_8000 = 0x8A,
	HW_ADDR1_PCIE_FAMILY_8000 = 0x8E,
	MAC_ADDRESS_OVERRIDE_FAMILY_8000 = 1,

	/* NVM SW-Section offset (in words) definitions */
	NVM_SW_SECTION_FAMILY_8000 = 0x1C0,
	NVM_VERSION_FAMILY_8000 = 0,
	RADIO_CFG_FAMILY_8000 = 2,
	SKU_FAMILY_8000 = 4,
	N_HW_ADDRS_FAMILY_8000 = 5,

	/* NVM REGULATORY -Section offset (in words) definitions */
	NVM_CHANNELS_FAMILY_8000 = 0,

	/* NVM calibration section offset (in words) definitions */
	NVM_CALIB_SECTION_FAMILY_8000 = 0x2B8,
	XTAL_CALIB_FAMILY_8000 = 0x316 - NVM_CALIB_SECTION_FAMILY_8000
};

/* SKU Capabilities (actual values from NVM definition) */
enum nvm_sku_bits {
	NVM_SKU_CAP_BAND_24GHZ	= BIT(0),
	NVM_SKU_CAP_BAND_52GHZ	= BIT(1),
	NVM_SKU_CAP_11N_ENABLE	= BIT(2),
	NVM_SKU_CAP_11AC_ENABLE	= BIT(3),
};

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

static const u8 iwl_nvm_channels_family_8000[] = {
	/* 2.4 GHz */
	1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
	/* 5 GHz */
	36, 40, 44, 48, 52, 56, 60, 64, 68, 72, 76, 80, 84, 88, 92,
	96, 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 144,
	149, 153, 157, 161, 165, 169, 173, 177, 181
};

#define IWL_NUM_CHANNELS		ARRAY_SIZE(iwl_nvm_channels)
#define IWL_NUM_CHANNELS_FAMILY_8000	ARRAY_SIZE(iwl_nvm_channels_family_8000)
#define NUM_2GHZ_CHANNELS		14
#define NUM_2GHZ_CHANNELS_FAMILY_8000	14
#define FIRST_2GHZ_HT_MINUS		5
#define LAST_2GHZ_HT_PLUS		9
#define LAST_5GHZ_HT			161

#define DEFAULT_MAX_TX_POWER 16

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
 * @NVM_CHANNEL_WIDE: 20 MHz channel okay (?)
 * @NVM_CHANNEL_40MHZ: 40 MHz channel okay (?)
 * @NVM_CHANNEL_80MHZ: 80 MHz channel okay (?)
 * @NVM_CHANNEL_160MHZ: 160 MHz channel okay (?)
 */
enum iwl_nvm_channel_flags {
	NVM_CHANNEL_VALID = BIT(0),
	NVM_CHANNEL_IBSS = BIT(1),
	NVM_CHANNEL_ACTIVE = BIT(3),
	NVM_CHANNEL_RADAR = BIT(4),
	NVM_CHANNEL_INDOOR_ONLY = BIT(5),
	NVM_CHANNEL_GO_CONCURRENT = BIT(6),
	NVM_CHANNEL_WIDE = BIT(8),
	NVM_CHANNEL_40MHZ = BIT(9),
	NVM_CHANNEL_80MHZ = BIT(10),
	NVM_CHANNEL_160MHZ = BIT(11),
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
	int num_of_ch, num_2ghz_channels;
	const u8 *nvm_chan;

	if (cfg->device_family != IWL_DEVICE_FAMILY_8000) {
		num_of_ch = IWL_NUM_CHANNELS;
		nvm_chan = &iwl_nvm_channels[0];
		num_2ghz_channels = NUM_2GHZ_CHANNELS;
	} else {
		num_of_ch = IWL_NUM_CHANNELS_FAMILY_8000;
		nvm_chan = &iwl_nvm_channels_family_8000[0];
		num_2ghz_channels = NUM_2GHZ_CHANNELS_FAMILY_8000;
	}

	for (ch_idx = 0; ch_idx < num_of_ch; ch_idx++) {
		ch_flags = __le16_to_cpup(nvm_ch_flags + ch_idx);

		if (ch_idx >= num_2ghz_channels &&
		    !data->sku_cap_band_52GHz_enable)
			ch_flags &= ~NVM_CHANNEL_VALID;

		if (!(ch_flags & NVM_CHANNEL_VALID)) {
			IWL_DEBUG_EEPROM(dev,
					 "Ch. %d Flags %x [%sGHz] - No traffic\n",
					 nvm_chan[ch_idx],
					 ch_flags,
					 (ch_idx >= num_2ghz_channels) ?
					 "5.2" : "2.4");
			continue;
		}

		channel = &data->channels[n_channels];
		n_channels++;

		channel->hw_value = nvm_chan[ch_idx];
		channel->band = (ch_idx < num_2ghz_channels) ?
				IEEE80211_BAND_2GHZ : IEEE80211_BAND_5GHZ;
		channel->center_freq =
			ieee80211_channel_to_frequency(
				channel->hw_value, channel->band);

		/* TODO: Need to be dependent to the NVM */
		channel->flags = IEEE80211_CHAN_NO_HT40;
		if (ch_idx < num_2ghz_channels &&
		    (ch_flags & NVM_CHANNEL_40MHZ)) {
			if (nvm_chan[ch_idx] <= LAST_2GHZ_HT_PLUS)
				channel->flags &= ~IEEE80211_CHAN_NO_HT40PLUS;
			if (nvm_chan[ch_idx] >= FIRST_2GHZ_HT_MINUS)
				channel->flags &= ~IEEE80211_CHAN_NO_HT40MINUS;
		} else if (nvm_chan[ch_idx] <= LAST_5GHZ_HT &&
			   (ch_flags & NVM_CHANNEL_40MHZ)) {
			if ((ch_idx - num_2ghz_channels) % 2 == 0)
				channel->flags &= ~IEEE80211_CHAN_NO_HT40PLUS;
			else
				channel->flags &= ~IEEE80211_CHAN_NO_HT40MINUS;
		}
		if (!(ch_flags & NVM_CHANNEL_80MHZ))
			channel->flags |= IEEE80211_CHAN_NO_80MHZ;
		if (!(ch_flags & NVM_CHANNEL_160MHZ))
			channel->flags |= IEEE80211_CHAN_NO_160MHZ;

		if (!(ch_flags & NVM_CHANNEL_IBSS))
			channel->flags |= IEEE80211_CHAN_NO_IR;

		if (!(ch_flags & NVM_CHANNEL_ACTIVE))
			channel->flags |= IEEE80211_CHAN_NO_IR;

		if (ch_flags & NVM_CHANNEL_RADAR)
			channel->flags |= IEEE80211_CHAN_RADAR;

		if (ch_flags & NVM_CHANNEL_INDOOR_ONLY)
			channel->flags |= IEEE80211_CHAN_INDOOR_ONLY;

		/* Set the GO concurrent flag only in case that NO_IR is set.
		 * Otherwise it is meaningless
		 */
		if ((ch_flags & NVM_CHANNEL_GO_CONCURRENT) &&
		    (channel->flags & IEEE80211_CHAN_NO_IR))
			channel->flags |= IEEE80211_CHAN_GO_CONCURRENT;

		/* Initialize regulatory-based run-time data */

		/*
		 * Default value - highest tx power value.  max_power
		 * is not used in mvm, and is used for backwards compatibility
		 */
		channel->max_power = DEFAULT_MAX_TX_POWER;
		is_5ghz = channel->band == IEEE80211_BAND_5GHZ;
		IWL_DEBUG_EEPROM(dev,
				 "Ch. %d [%sGHz] %s%s%s%s%s%s%s(0x%02x %ddBm): Ad-Hoc %ssupported\n",
				 channel->hw_value,
				 is_5ghz ? "5.2" : "2.4",
				 CHECK_AND_PRINT_I(VALID),
				 CHECK_AND_PRINT_I(IBSS),
				 CHECK_AND_PRINT_I(ACTIVE),
				 CHECK_AND_PRINT_I(RADAR),
				 CHECK_AND_PRINT_I(WIDE),
				 CHECK_AND_PRINT_I(INDOOR_ONLY),
				 CHECK_AND_PRINT_I(GO_CONCURRENT),
				 ch_flags,
				 channel->max_power,
				 ((ch_flags & NVM_CHANNEL_IBSS) &&
				  !(ch_flags & NVM_CHANNEL_RADAR))
					? "" : "not ");
	}

	return n_channels;
}

static void iwl_init_vht_hw_capab(const struct iwl_cfg *cfg,
				  struct iwl_nvm_data *data,
				  struct ieee80211_sta_vht_cap *vht_cap,
				  u8 tx_chains, u8 rx_chains)
{
	int num_rx_ants = num_of_ant(rx_chains);
	int num_tx_ants = num_of_ant(tx_chains);

	vht_cap->vht_supported = true;

	vht_cap->cap = IEEE80211_VHT_CAP_SHORT_GI_80 |
		       IEEE80211_VHT_CAP_RXSTBC_1 |
		       IEEE80211_VHT_CAP_SU_BEAMFORMEE_CAPABLE |
		       3 << IEEE80211_VHT_CAP_BEAMFORMEE_STS_SHIFT |
		       7 << IEEE80211_VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_SHIFT;

	if (num_tx_ants > 1)
		vht_cap->cap |= IEEE80211_VHT_CAP_TXSTBC;
	else
		vht_cap->cap |= IEEE80211_VHT_CAP_TX_ANTENNA_PATTERN;

	if (iwlwifi_mod_params.amsdu_size_8K)
		vht_cap->cap |= IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_7991;

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
}

static void iwl_init_sbands(struct device *dev, const struct iwl_cfg *cfg,
			    struct iwl_nvm_data *data,
			    const __le16 *ch_section, bool enable_vht,
			    u8 tx_chains, u8 rx_chains)
{
	int n_channels;
	int n_used = 0;
	struct ieee80211_supported_band *sband;

	if (cfg->device_family != IWL_DEVICE_FAMILY_8000)
		n_channels = iwl_init_channel_map(
				dev, cfg, data,
				&ch_section[NVM_CHANNELS]);
	else
		n_channels = iwl_init_channel_map(
				dev, cfg, data,
				&ch_section[NVM_CHANNELS_FAMILY_8000]);

	sband = &data->bands[IEEE80211_BAND_2GHZ];
	sband->band = IEEE80211_BAND_2GHZ;
	sband->bitrates = &iwl_cfg80211_rates[RATES_24_OFFS];
	sband->n_bitrates = N_RATES_24;
	n_used += iwl_init_sband_channels(data, sband, n_channels,
					  IEEE80211_BAND_2GHZ);
	iwl_init_ht_hw_capab(cfg, data, &sband->ht_cap, IEEE80211_BAND_2GHZ,
			     tx_chains, rx_chains);

	sband = &data->bands[IEEE80211_BAND_5GHZ];
	sband->band = IEEE80211_BAND_5GHZ;
	sband->bitrates = &iwl_cfg80211_rates[RATES_52_OFFS];
	sband->n_bitrates = N_RATES_52;
	n_used += iwl_init_sband_channels(data, sband, n_channels,
					  IEEE80211_BAND_5GHZ);
	iwl_init_ht_hw_capab(cfg, data, &sband->ht_cap, IEEE80211_BAND_5GHZ,
			     tx_chains, rx_chains);
	if (enable_vht)
		iwl_init_vht_hw_capab(cfg, data, &sband->vht_cap,
				      tx_chains, rx_chains);

	if (n_channels != n_used)
		IWL_ERR_DEV(dev, "NVM: used only %d of %d channels\n",
			    n_used, n_channels);
}

static int iwl_get_sku(const struct iwl_cfg *cfg,
		       const __le16 *nvm_sw)
{
	if (cfg->device_family != IWL_DEVICE_FAMILY_8000)
		return le16_to_cpup(nvm_sw + SKU);
	else
		return le32_to_cpup((__le32 *)(nvm_sw + SKU_FAMILY_8000));
}

static int iwl_get_nvm_version(const struct iwl_cfg *cfg,
			       const __le16 *nvm_sw)
{
	if (cfg->device_family != IWL_DEVICE_FAMILY_8000)
		return le16_to_cpup(nvm_sw + NVM_VERSION);
	else
		return le32_to_cpup((__le32 *)(nvm_sw +
					       NVM_VERSION_FAMILY_8000));
}

static int iwl_get_radio_cfg(const struct iwl_cfg *cfg,
			     const __le16 *nvm_sw)
{
	if (cfg->device_family != IWL_DEVICE_FAMILY_8000)
		return le16_to_cpup(nvm_sw + RADIO_CFG);
	else
		return le32_to_cpup((__le32 *)(nvm_sw + RADIO_CFG_FAMILY_8000));
}

#define N_HW_ADDRS_MASK_FAMILY_8000	0xF
static int iwl_get_n_hw_addrs(const struct iwl_cfg *cfg,
			      const __le16 *nvm_sw)
{
	if (cfg->device_family != IWL_DEVICE_FAMILY_8000)
		return le16_to_cpup(nvm_sw + N_HW_ADDRS);
	else
		return le32_to_cpup((__le32 *)(nvm_sw + N_HW_ADDRS_FAMILY_8000))
		       & N_HW_ADDRS_MASK_FAMILY_8000;
}

static void iwl_set_radio_cfg(const struct iwl_cfg *cfg,
			      struct iwl_nvm_data *data,
			      u32 radio_cfg)
{
	if (cfg->device_family != IWL_DEVICE_FAMILY_8000) {
		data->radio_cfg_type = NVM_RF_CFG_TYPE_MSK(radio_cfg);
		data->radio_cfg_step = NVM_RF_CFG_STEP_MSK(radio_cfg);
		data->radio_cfg_dash = NVM_RF_CFG_DASH_MSK(radio_cfg);
		data->radio_cfg_pnum = NVM_RF_CFG_PNUM_MSK(radio_cfg);
		return;
	}

	/* set the radio configuration for family 8000 */
	data->radio_cfg_type = NVM_RF_CFG_TYPE_MSK_FAMILY_8000(radio_cfg);
	data->radio_cfg_step = NVM_RF_CFG_STEP_MSK_FAMILY_8000(radio_cfg);
	data->radio_cfg_dash = NVM_RF_CFG_DASH_MSK_FAMILY_8000(radio_cfg);
	data->radio_cfg_pnum = NVM_RF_CFG_FLAVOR_MSK_FAMILY_8000(radio_cfg);
}

static void iwl_set_hw_address(const struct iwl_cfg *cfg,
			       struct iwl_nvm_data *data,
			       const __le16 *nvm_sec)
{
	const u8 *hw_addr = (const u8 *)(nvm_sec + HW_ADDR);

	/* The byte order is little endian 16 bit, meaning 214365 */
	data->hw_addr[0] = hw_addr[1];
	data->hw_addr[1] = hw_addr[0];
	data->hw_addr[2] = hw_addr[3];
	data->hw_addr[3] = hw_addr[2];
	data->hw_addr[4] = hw_addr[5];
	data->hw_addr[5] = hw_addr[4];
}

static void iwl_set_hw_address_family_8000(struct device *dev,
					   const struct iwl_cfg *cfg,
					   struct iwl_nvm_data *data,
					   const __le16 *mac_override,
					   const __le16 *nvm_hw)
{
	const u8 *hw_addr;

	if (mac_override) {
		hw_addr = (const u8 *)(mac_override +
				 MAC_ADDRESS_OVERRIDE_FAMILY_8000);

		/* The byte order is little endian 16 bit, meaning 214365 */
		data->hw_addr[0] = hw_addr[1];
		data->hw_addr[1] = hw_addr[0];
		data->hw_addr[2] = hw_addr[3];
		data->hw_addr[3] = hw_addr[2];
		data->hw_addr[4] = hw_addr[5];
		data->hw_addr[5] = hw_addr[4];

		if (is_valid_ether_addr(data->hw_addr))
			return;

		IWL_ERR_DEV(dev,
			    "mac address from nvm override section is not valid\n");
	}

	if (nvm_hw) {
		/* read the MAC address from OTP */
		if (!dev_is_pci(dev) || (data->nvm_version < 0xE08)) {
			/* read the mac address from the WFPM location */
			hw_addr = (const u8 *)(nvm_hw +
					       HW_ADDR0_WFPM_FAMILY_8000);
			data->hw_addr[0] = hw_addr[3];
			data->hw_addr[1] = hw_addr[2];
			data->hw_addr[2] = hw_addr[1];
			data->hw_addr[3] = hw_addr[0];

			hw_addr = (const u8 *)(nvm_hw +
					       HW_ADDR1_WFPM_FAMILY_8000);
			data->hw_addr[4] = hw_addr[1];
			data->hw_addr[5] = hw_addr[0];
		} else if ((data->nvm_version >= 0xE08) &&
			   (data->nvm_version < 0xE0B)) {
			/* read "reverse order"  from the PCIe location */
			hw_addr = (const u8 *)(nvm_hw +
					       HW_ADDR0_PCIE_FAMILY_8000);
			data->hw_addr[5] = hw_addr[2];
			data->hw_addr[4] = hw_addr[1];
			data->hw_addr[3] = hw_addr[0];

			hw_addr = (const u8 *)(nvm_hw +
					       HW_ADDR1_PCIE_FAMILY_8000);
			data->hw_addr[2] = hw_addr[3];
			data->hw_addr[1] = hw_addr[2];
			data->hw_addr[0] = hw_addr[1];
		} else {
			/* read from the PCIe location */
			hw_addr = (const u8 *)(nvm_hw +
					       HW_ADDR0_PCIE_FAMILY_8000);
			data->hw_addr[5] = hw_addr[0];
			data->hw_addr[4] = hw_addr[1];
			data->hw_addr[3] = hw_addr[2];

			hw_addr = (const u8 *)(nvm_hw +
					       HW_ADDR1_PCIE_FAMILY_8000);
			data->hw_addr[2] = hw_addr[1];
			data->hw_addr[1] = hw_addr[2];
			data->hw_addr[0] = hw_addr[3];
		}
		if (!is_valid_ether_addr(data->hw_addr))
			IWL_ERR_DEV(dev,
				    "mac address from hw section is not valid\n");

		return;
	}

	IWL_ERR_DEV(dev, "mac address is not found\n");
}

struct iwl_nvm_data *
iwl_parse_nvm_data(struct device *dev, const struct iwl_cfg *cfg,
		   const __le16 *nvm_hw, const __le16 *nvm_sw,
		   const __le16 *nvm_calib, const __le16 *regulatory,
		   const __le16 *mac_override, u8 tx_chains, u8 rx_chains)
{
	struct iwl_nvm_data *data;
	u32 sku;
	u32 radio_cfg;

	if (cfg->device_family != IWL_DEVICE_FAMILY_8000)
		data = kzalloc(sizeof(*data) +
			       sizeof(struct ieee80211_channel) *
			       IWL_NUM_CHANNELS,
			       GFP_KERNEL);
	else
		data = kzalloc(sizeof(*data) +
			       sizeof(struct ieee80211_channel) *
			       IWL_NUM_CHANNELS_FAMILY_8000,
			       GFP_KERNEL);
	if (!data)
		return NULL;

	data->nvm_version = iwl_get_nvm_version(cfg, nvm_sw);

	radio_cfg = iwl_get_radio_cfg(cfg, nvm_sw);
	iwl_set_radio_cfg(cfg, data, radio_cfg);

	sku = iwl_get_sku(cfg, nvm_sw);
	data->sku_cap_band_24GHz_enable = sku & NVM_SKU_CAP_BAND_24GHZ;
	data->sku_cap_band_52GHz_enable = sku & NVM_SKU_CAP_BAND_52GHZ;
	data->sku_cap_11n_enable = sku & NVM_SKU_CAP_11N_ENABLE;
	data->sku_cap_11ac_enable = sku & NVM_SKU_CAP_11AC_ENABLE;
	if (iwlwifi_mod_params.disable_11n & IWL_DISABLE_HT_ALL)
		data->sku_cap_11n_enable = false;

	data->n_hw_addrs = iwl_get_n_hw_addrs(cfg, nvm_sw);

	if (cfg->device_family != IWL_DEVICE_FAMILY_8000) {
		/* Checking for required sections */
		if (!nvm_calib) {
			IWL_ERR_DEV(dev,
				    "Can't parse empty Calib NVM sections\n");
			kfree(data);
			return NULL;
		}
		/* in family 8000 Xtal calibration values moved to OTP */
		data->xtal_calib[0] = *(nvm_calib + XTAL_CALIB);
		data->xtal_calib[1] = *(nvm_calib + XTAL_CALIB + 1);
	}

	if (cfg->device_family != IWL_DEVICE_FAMILY_8000) {
		iwl_set_hw_address(cfg, data, nvm_hw);

		iwl_init_sbands(dev, cfg, data, nvm_sw,
				sku & NVM_SKU_CAP_11AC_ENABLE, tx_chains,
				rx_chains);
	} else {
		/* MAC address in family 8000 */
		iwl_set_hw_address_family_8000(dev, cfg, data, mac_override,
					       nvm_hw);

		iwl_init_sbands(dev, cfg, data, regulatory,
				sku & NVM_SKU_CAP_11AC_ENABLE, tx_chains,
				rx_chains);
	}

	data->calib_version = 255;

	return data;
}
IWL_EXPORT_SYMBOL(iwl_parse_nvm_data);
