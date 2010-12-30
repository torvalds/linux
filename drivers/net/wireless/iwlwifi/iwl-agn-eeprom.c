/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2008 - 2010 Intel Corporation. All rights reserved.
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
 * in the file called LICENSE.GPL.
 *
 * Contact Information:
 *  Intel Linux Wireless <ilw@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
 * Copyright(c) 2005 - 2010 Intel Corporation. All rights reserved.
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


#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>

#include <net/mac80211.h>

#include "iwl-commands.h"
#include "iwl-dev.h"
#include "iwl-core.h"
#include "iwl-debug.h"
#include "iwl-agn.h"
#include "iwl-io.h"

/************************** EEPROM BANDS ****************************
 *
 * The iwl_eeprom_band definitions below provide the mapping from the
 * EEPROM contents to the specific channel number supported for each
 * band.
 *
 * For example, iwl_priv->eeprom.band_3_channels[4] from the band_3
 * definition below maps to physical channel 42 in the 5.2GHz spectrum.
 * The specific geography and calibration information for that channel
 * is contained in the eeprom map itself.
 *
 * During init, we copy the eeprom information and channel map
 * information into priv->channel_info_24/52 and priv->channel_map_24/52
 *
 * channel_map_24/52 provides the index in the channel_info array for a
 * given channel.  We have to have two separate maps as there is channel
 * overlap with the 2.4GHz and 5.2GHz spectrum as seen in band_1 and
 * band_2
 *
 * A value of 0xff stored in the channel_map indicates that the channel
 * is not supported by the hardware at all.
 *
 * A value of 0xfe in the channel_map indicates that the channel is not
 * valid for Tx with the current hardware.  This means that
 * while the system can tune and receive on a given channel, it may not
 * be able to associate or transmit any frames on that
 * channel.  There is no corresponding channel information for that
 * entry.
 *
 *********************************************************************/

/**
 * struct iwl_txpwr_section: eeprom section information
 * @offset: indirect address into eeprom image
 * @count: number of "struct iwl_eeprom_enhanced_txpwr" in this section
 * @band: band type for the section
 * @is_common - true: common section, false: channel section
 * @is_cck - true: cck section, false: not cck section
 * @is_ht_40 - true: all channel in the section are HT40 channel,
 *	       false: legacy or HT 20 MHz
 *	       ignore if it is common section
 * @iwl_eeprom_section_channel: channel array in the section,
 *	       ignore if common section
 */
struct iwl_txpwr_section {
	u32 offset;
	u8 count;
	enum ieee80211_band band;
	bool is_common;
	bool is_cck;
	bool is_ht40;
	u8 iwl_eeprom_section_channel[EEPROM_MAX_TXPOWER_SECTION_ELEMENTS];
};

/**
 * section 1 - 3 are regulatory tx power apply to all channels based on
 *    modulation: CCK, OFDM
 *    Band: 2.4GHz, 5.2GHz
 * section 4 - 10 are regulatory tx power apply to specified channels
 *    For example:
 *	1L - Channel 1 Legacy
 *	1HT - Channel 1 HT
 *	(1,+1) - Channel 1 HT40 "_above_"
 *
 * Section 1: all CCK channels
 * Section 2: all 2.4 GHz OFDM (Legacy, HT and HT40) channels
 * Section 3: all 5.2 GHz OFDM (Legacy, HT and HT40) channels
 * Section 4: 2.4 GHz 20MHz channels: 1L, 1HT, 2L, 2HT, 10L, 10HT, 11L, 11HT
 * Section 5: 2.4 GHz 40MHz channels: (1,+1) (2,+1) (6,+1) (7,+1) (9,+1)
 * Section 6: 5.2 GHz 20MHz channels: 36L, 64L, 100L, 36HT, 64HT, 100HT
 * Section 7: 5.2 GHz 40MHz channels: (36,+1) (60,+1) (100,+1)
 * Section 8: 2.4 GHz channel: 13L, 13HT
 * Section 9: 2.4 GHz channel: 140L, 140HT
 * Section 10: 2.4 GHz 40MHz channels: (132,+1)  (44,+1)
 *
 */
static const struct iwl_txpwr_section enhinfo[] = {
	{ EEPROM_LB_CCK_20_COMMON, 1, IEEE80211_BAND_2GHZ, true, true, false },
	{ EEPROM_LB_OFDM_COMMON, 3, IEEE80211_BAND_2GHZ, true, false, false },
	{ EEPROM_HB_OFDM_COMMON, 3, IEEE80211_BAND_5GHZ, true, false, false },
	{ EEPROM_LB_OFDM_20_BAND, 8, IEEE80211_BAND_2GHZ,
		false, false, false,
		{1, 1, 2, 2, 10, 10, 11, 11 } },
	{ EEPROM_LB_OFDM_HT40_BAND, 5, IEEE80211_BAND_2GHZ,
		false, false, true,
		{ 1, 2, 6, 7, 9 } },
	{ EEPROM_HB_OFDM_20_BAND, 6, IEEE80211_BAND_5GHZ,
		false, false, false,
		{ 36, 64, 100, 36, 64, 100 } },
	{ EEPROM_HB_OFDM_HT40_BAND, 3, IEEE80211_BAND_5GHZ,
		false, false, true,
		{ 36, 60, 100 } },
	{ EEPROM_LB_OFDM_20_CHANNEL_13, 2, IEEE80211_BAND_2GHZ,
		false, false, false,
		{ 13, 13 } },
	{ EEPROM_HB_OFDM_20_CHANNEL_140, 2, IEEE80211_BAND_5GHZ,
		false, false, false,
		{ 140, 140 } },
	{ EEPROM_HB_OFDM_HT40_BAND_1, 2, IEEE80211_BAND_5GHZ,
		false, false, true,
		{ 132, 44 } },
};

/******************************************************************************
 *
 * EEPROM related functions
 *
******************************************************************************/

/*
 * The device's EEPROM semaphore prevents conflicts between driver and uCode
 * when accessing the EEPROM; each access is a series of pulses to/from the
 * EEPROM chip, not a single event, so even reads could conflict if they
 * weren't arbitrated by the semaphore.
 */
int iwlcore_eeprom_acquire_semaphore(struct iwl_priv *priv)
{
	u16 count;
	int ret;

	for (count = 0; count < EEPROM_SEM_RETRY_LIMIT; count++) {
		/* Request semaphore */
		iwl_set_bit(priv, CSR_HW_IF_CONFIG_REG,
			    CSR_HW_IF_CONFIG_REG_BIT_EEPROM_OWN_SEM);

		/* See if we got it */
		ret = iwl_poll_bit(priv, CSR_HW_IF_CONFIG_REG,
				CSR_HW_IF_CONFIG_REG_BIT_EEPROM_OWN_SEM,
				CSR_HW_IF_CONFIG_REG_BIT_EEPROM_OWN_SEM,
				EEPROM_SEM_TIMEOUT);
		if (ret >= 0) {
			IWL_DEBUG_IO(priv,
				"Acquired semaphore after %d tries.\n",
				count+1);
			return ret;
		}
	}

	return ret;
}

void iwlcore_eeprom_release_semaphore(struct iwl_priv *priv)
{
	iwl_clear_bit(priv, CSR_HW_IF_CONFIG_REG,
		CSR_HW_IF_CONFIG_REG_BIT_EEPROM_OWN_SEM);

}

int iwl_eeprom_check_version(struct iwl_priv *priv)
{
	u16 eeprom_ver;
	u16 calib_ver;

	eeprom_ver = iwl_eeprom_query16(priv, EEPROM_VERSION);
	calib_ver = priv->cfg->ops->lib->eeprom_ops.calib_version(priv);

	if (eeprom_ver < priv->cfg->eeprom_ver ||
	    calib_ver < priv->cfg->eeprom_calib_ver)
		goto err;

	IWL_INFO(priv, "device EEPROM VER=0x%x, CALIB=0x%x\n",
		 eeprom_ver, calib_ver);

	return 0;
err:
	IWL_ERR(priv, "Unsupported (too old) EEPROM VER=0x%x < 0x%x "
		  "CALIB=0x%x < 0x%x\n",
		  eeprom_ver, priv->cfg->eeprom_ver,
		  calib_ver,  priv->cfg->eeprom_calib_ver);
	return -EINVAL;

}

void iwl_eeprom_get_mac(const struct iwl_priv *priv, u8 *mac)
{
	const u8 *addr = priv->cfg->ops->lib->eeprom_ops.query_addr(priv,
					EEPROM_MAC_ADDRESS);
	memcpy(mac, addr, ETH_ALEN);
}

/**
 * iwl_get_max_txpower_avg - get the highest tx power from all chains.
 *     find the highest tx power from all chains for the channel
 */
static s8 iwl_get_max_txpower_avg(struct iwl_priv *priv,
		struct iwl_eeprom_enhanced_txpwr *enhanced_txpower,
		int element, s8 *max_txpower_in_half_dbm)
{
	s8 max_txpower_avg = 0; /* (dBm) */

	IWL_DEBUG_INFO(priv, "%d - "
			"chain_a: %d dB chain_b: %d dB "
			"chain_c: %d dB mimo2: %d dB mimo3: %d dB\n",
			element,
			enhanced_txpower[element].chain_a_max >> 1,
			enhanced_txpower[element].chain_b_max >> 1,
			enhanced_txpower[element].chain_c_max >> 1,
			enhanced_txpower[element].mimo2_max >> 1,
			enhanced_txpower[element].mimo3_max >> 1);
	/* Take the highest tx power from any valid chains */
	if ((priv->cfg->valid_tx_ant & ANT_A) &&
	    (enhanced_txpower[element].chain_a_max > max_txpower_avg))
		max_txpower_avg = enhanced_txpower[element].chain_a_max;
	if ((priv->cfg->valid_tx_ant & ANT_B) &&
	    (enhanced_txpower[element].chain_b_max > max_txpower_avg))
		max_txpower_avg = enhanced_txpower[element].chain_b_max;
	if ((priv->cfg->valid_tx_ant & ANT_C) &&
	    (enhanced_txpower[element].chain_c_max > max_txpower_avg))
		max_txpower_avg = enhanced_txpower[element].chain_c_max;
	if (((priv->cfg->valid_tx_ant == ANT_AB) |
	    (priv->cfg->valid_tx_ant == ANT_BC) |
	    (priv->cfg->valid_tx_ant == ANT_AC)) &&
	    (enhanced_txpower[element].mimo2_max > max_txpower_avg))
		max_txpower_avg =  enhanced_txpower[element].mimo2_max;
	if ((priv->cfg->valid_tx_ant == ANT_ABC) &&
	    (enhanced_txpower[element].mimo3_max > max_txpower_avg))
		max_txpower_avg = enhanced_txpower[element].mimo3_max;

	/*
	 * max. tx power in EEPROM is in 1/2 dBm format
	 * convert from 1/2 dBm to dBm (round-up convert)
	 * but we also do not want to loss 1/2 dBm resolution which
	 * will impact performance
	 */
	*max_txpower_in_half_dbm = max_txpower_avg;
	return (max_txpower_avg & 0x01) + (max_txpower_avg >> 1);
}

/**
 * iwl_update_common_txpower: update channel tx power
 *     update tx power per band based on EEPROM enhanced tx power info.
 */
static s8 iwl_update_common_txpower(struct iwl_priv *priv,
		struct iwl_eeprom_enhanced_txpwr *enhanced_txpower,
		int section, int element, s8 *max_txpower_in_half_dbm)
{
	struct iwl_channel_info *ch_info;
	int ch;
	bool is_ht40 = false;
	s8 max_txpower_avg; /* (dBm) */

	/* it is common section, contain all type (Legacy, HT and HT40)
	 * based on the element in the section to determine
	 * is it HT 40 or not
	 */
	if (element == EEPROM_TXPOWER_COMMON_HT40_INDEX)
		is_ht40 = true;
	max_txpower_avg =
		iwl_get_max_txpower_avg(priv, enhanced_txpower,
					element, max_txpower_in_half_dbm);

	ch_info = priv->channel_info;

	for (ch = 0; ch < priv->channel_count; ch++) {
		/* find matching band and update tx power if needed */
		if ((ch_info->band == enhinfo[section].band) &&
		    (ch_info->max_power_avg < max_txpower_avg) &&
		    (!is_ht40)) {
			/* Update regulatory-based run-time data */
			ch_info->max_power_avg = ch_info->curr_txpow =
				max_txpower_avg;
			ch_info->scan_power = max_txpower_avg;
		}
		if ((ch_info->band == enhinfo[section].band) && is_ht40 &&
		    (ch_info->ht40_max_power_avg < max_txpower_avg)) {
			/* Update regulatory-based run-time data */
			ch_info->ht40_max_power_avg = max_txpower_avg;
		}
		ch_info++;
	}
	return max_txpower_avg;
}

/**
 * iwl_update_channel_txpower: update channel tx power
 *      update channel tx power based on EEPROM enhanced tx power info.
 */
static s8 iwl_update_channel_txpower(struct iwl_priv *priv,
		struct iwl_eeprom_enhanced_txpwr *enhanced_txpower,
		int section, int element, s8 *max_txpower_in_half_dbm)
{
	struct iwl_channel_info *ch_info;
	int ch;
	u8 channel;
	s8 max_txpower_avg; /* (dBm) */

	channel = enhinfo[section].iwl_eeprom_section_channel[element];
	max_txpower_avg =
		iwl_get_max_txpower_avg(priv, enhanced_txpower,
					element, max_txpower_in_half_dbm);

	ch_info = priv->channel_info;
	for (ch = 0; ch < priv->channel_count; ch++) {
		/* find matching channel and update tx power if needed */
		if (ch_info->channel == channel) {
			if ((ch_info->max_power_avg < max_txpower_avg) &&
			    (!enhinfo[section].is_ht40)) {
				/* Update regulatory-based run-time data */
				ch_info->max_power_avg = max_txpower_avg;
				ch_info->curr_txpow = max_txpower_avg;
				ch_info->scan_power = max_txpower_avg;
			}
			if ((enhinfo[section].is_ht40) &&
			    (ch_info->ht40_max_power_avg < max_txpower_avg)) {
				/* Update regulatory-based run-time data */
				ch_info->ht40_max_power_avg = max_txpower_avg;
			}
			break;
		}
		ch_info++;
	}
	return max_txpower_avg;
}

/**
 * iwlcore_eeprom_enhanced_txpower: process enhanced tx power info
 */
static void iwlcore_eeprom_enhanced_txpower_old(struct iwl_priv *priv)
{
	int eeprom_section_count = 0;
	int section, element;
	struct iwl_eeprom_enhanced_txpwr *enhanced_txpower;
	u32 offset;
	s8 max_txpower_avg; /* (dBm) */
	s8 max_txpower_in_half_dbm; /* (half-dBm) */

	/* Loop through all the sections
	 * adjust bands and channel's max tx power
	 * Set the tx_power_user_lmt to the highest power
	 * supported by any channels and chains
	 */
	for (section = 0; section < ARRAY_SIZE(enhinfo); section++) {
		eeprom_section_count = enhinfo[section].count;
		offset = enhinfo[section].offset;
		enhanced_txpower = (struct iwl_eeprom_enhanced_txpwr *)
				iwl_eeprom_query_addr(priv, offset);

		/*
		 * check for valid entry -
		 * different version of EEPROM might contain different set
		 * of enhanced tx power table
		 * always check for valid entry before process
		 * the information
		 */
		if (!(enhanced_txpower->flags || enhanced_txpower->channel) ||
		    enhanced_txpower->delta_20_in_40)
			continue;

		for (element = 0; element < eeprom_section_count; element++) {
			if (enhinfo[section].is_common)
				max_txpower_avg =
					iwl_update_common_txpower(priv,
						enhanced_txpower, section,
						element,
						&max_txpower_in_half_dbm);
			else
				max_txpower_avg =
					iwl_update_channel_txpower(priv,
						enhanced_txpower, section,
						element,
						&max_txpower_in_half_dbm);

			/* Update the tx_power_user_lmt to the highest power
			 * supported by any channel */
			if (max_txpower_avg > priv->tx_power_user_lmt)
				priv->tx_power_user_lmt = max_txpower_avg;

			/*
			 * Update the tx_power_lmt_in_half_dbm to
			 * the highest power supported by any channel
			 */
			if (max_txpower_in_half_dbm >
			    priv->tx_power_lmt_in_half_dbm)
				priv->tx_power_lmt_in_half_dbm =
					max_txpower_in_half_dbm;
		}
	}
}

static void
iwlcore_eeprom_enh_txp_read_element(struct iwl_priv *priv,
				    struct iwl_eeprom_enhanced_txpwr *txp,
				    s8 max_txpower_avg)
{
	int ch_idx;
	bool is_ht40 = txp->flags & IWL_EEPROM_ENH_TXP_FL_40MHZ;
	enum ieee80211_band band;

	band = txp->flags & IWL_EEPROM_ENH_TXP_FL_BAND_52G ?
		IEEE80211_BAND_5GHZ : IEEE80211_BAND_2GHZ;

	for (ch_idx = 0; ch_idx < priv->channel_count; ch_idx++) {
		struct iwl_channel_info *ch_info = &priv->channel_info[ch_idx];

		/* update matching channel or from common data only */
		if (txp->channel != 0 && ch_info->channel != txp->channel)
			continue;

		/* update matching band only */
		if (band != ch_info->band)
			continue;

		if (ch_info->max_power_avg < max_txpower_avg && !is_ht40) {
			ch_info->max_power_avg = max_txpower_avg;
			ch_info->curr_txpow = max_txpower_avg;
			ch_info->scan_power = max_txpower_avg;
		}

		if (is_ht40 && ch_info->ht40_max_power_avg < max_txpower_avg)
			ch_info->ht40_max_power_avg = max_txpower_avg;
	}
}

#define EEPROM_TXP_OFFS	(0x00 | INDIRECT_ADDRESS | INDIRECT_TXP_LIMIT)
#define EEPROM_TXP_ENTRY_LEN sizeof(struct iwl_eeprom_enhanced_txpwr)
#define EEPROM_TXP_SZ_OFFS (0x00 | INDIRECT_ADDRESS | INDIRECT_TXP_LIMIT_SIZE)

static void iwlcore_eeprom_enhanced_txpower_new(struct iwl_priv *priv)
{
	struct iwl_eeprom_enhanced_txpwr *txp_array, *txp;
	int idx, entries;
	__le16 *txp_len;
	s8 max_txp_avg, max_txp_avg_halfdbm;

	BUILD_BUG_ON(sizeof(struct iwl_eeprom_enhanced_txpwr) != 8);

	/* the length is in 16-bit words, but we want entries */
	txp_len = (__le16 *) iwlagn_eeprom_query_addr(priv, EEPROM_TXP_SZ_OFFS);
	entries = le16_to_cpup(txp_len) * 2 / EEPROM_TXP_ENTRY_LEN;

	txp_array = (void *) iwlagn_eeprom_query_addr(priv, EEPROM_TXP_OFFS);
	for (idx = 0; idx < entries; idx++) {
		txp = &txp_array[idx];

		/* skip invalid entries */
		if (!(txp->flags & IWL_EEPROM_ENH_TXP_FL_VALID))
			continue;

		max_txp_avg = iwl_get_max_txpower_avg(priv, txp_array, idx,
						      &max_txp_avg_halfdbm);

		/*
		 * Update the user limit values values to the highest
		 * power supported by any channel
		 */
		if (max_txp_avg > priv->tx_power_user_lmt)
			priv->tx_power_user_lmt = max_txp_avg;
		if (max_txp_avg_halfdbm > priv->tx_power_lmt_in_half_dbm)
			priv->tx_power_lmt_in_half_dbm = max_txp_avg_halfdbm;

		iwlcore_eeprom_enh_txp_read_element(priv, txp, max_txp_avg);
	}
}

void iwlcore_eeprom_enhanced_txpower(struct iwl_priv *priv)
{
	if (priv->cfg->use_new_eeprom_reading)
		iwlcore_eeprom_enhanced_txpower_new(priv);
	else
		iwlcore_eeprom_enhanced_txpower_old(priv);
}
