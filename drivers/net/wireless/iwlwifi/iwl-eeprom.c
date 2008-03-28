/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2008 Intel Corporation. All rights reserved.
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
 * Tomas Winkler <tomas.winkler@intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
 * Copyright(c) 2005 - 2008 Intel Corporation. All rights reserved.
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
#include <linux/version.h>
#include <linux/init.h>

#include <net/mac80211.h>

#include "iwl-4965-commands.h"
#include "iwl-4965.h"
#include "iwl-core.h"
#include "iwl-debug.h"
#include "iwl-eeprom.h"
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

/* 2.4 GHz */
const u8 iwl_eeprom_band_1[14] = {
	1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14
};

/* 5.2 GHz bands */
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

static const u8 iwl_eeprom_band_6[] = {       /* 2.4 FAT channel */
	1, 2, 3, 4, 5, 6, 7
};

static const u8 iwl_eeprom_band_7[] = {       /* 5.2 FAT channel */
	36, 44, 52, 60, 100, 108, 116, 124, 132, 149, 157
};

/******************************************************************************
 *
 * EEPROM related functions
 *
******************************************************************************/

int iwlcore_eeprom_verify_signature(struct iwl_priv *priv)
{
	u32 gp = iwl_read32(priv, CSR_EEPROM_GP);
	if ((gp & CSR_EEPROM_GP_VALID_MSK) == CSR_EEPROM_GP_BAD_SIGNATURE) {
		IWL_ERROR("EEPROM not found, EEPROM_GP=0x%08x", gp);
		return -ENOENT;
	}
	return 0;
}
EXPORT_SYMBOL(iwlcore_eeprom_verify_signature);

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
			IWL_DEBUG_IO("Acquired semaphore after %d tries.\n",
				count+1);
			return ret;
		}
	}

	return ret;
}
EXPORT_SYMBOL(iwlcore_eeprom_acquire_semaphore);

void iwlcore_eeprom_release_semaphore(struct iwl_priv *priv)
{
	iwl_clear_bit(priv, CSR_HW_IF_CONFIG_REG,
		CSR_HW_IF_CONFIG_REG_BIT_EEPROM_OWN_SEM);

}
EXPORT_SYMBOL(iwlcore_eeprom_release_semaphore);


/**
 * iwl_eeprom_init - read EEPROM contents
 *
 * Load the EEPROM contents from adapter into priv->eeprom
 *
 * NOTE:  This routine uses the non-debug IO access functions.
 */
int iwl_eeprom_init(struct iwl_priv *priv)
{
	u16 *e = (u16 *)&priv->eeprom;
	u32 gp = iwl_read32(priv, CSR_EEPROM_GP);
	u32 r;
	int sz = sizeof(priv->eeprom);
	int ret;
	int i;
	u16 addr;

	/* The EEPROM structure has several padding buffers within it
	 * and when adding new EEPROM maps is subject to programmer errors
	 * which may be very difficult to identify without explicitly
	 * checking the resulting size of the eeprom map. */
	BUILD_BUG_ON(sizeof(priv->eeprom) != IWL_EEPROM_IMAGE_SIZE);

	if ((gp & CSR_EEPROM_GP_VALID_MSK) == CSR_EEPROM_GP_BAD_SIGNATURE) {
		IWL_ERROR("EEPROM not found, EEPROM_GP=0x%08x", gp);
		return -ENOENT;
	}

	/* Make sure driver (instead of uCode) is allowed to read EEPROM */
	ret = priv->cfg->ops->lib->eeprom_ops.acquire_semaphore(priv);
	if (ret < 0) {
		IWL_ERROR("Failed to acquire EEPROM semaphore.\n");
		return -ENOENT;
	}

	/* eeprom is an array of 16bit values */
	for (addr = 0; addr < sz; addr += sizeof(u16)) {
		_iwl_write32(priv, CSR_EEPROM_REG, addr << 1);
		_iwl_clear_bit(priv, CSR_EEPROM_REG, CSR_EEPROM_REG_BIT_CMD);

		for (i = 0; i < IWL_EEPROM_ACCESS_TIMEOUT;
					i += IWL_EEPROM_ACCESS_DELAY) {
			r = _iwl_read_direct32(priv, CSR_EEPROM_REG);
			if (r & CSR_EEPROM_REG_READ_VALID_MSK)
				break;
			udelay(IWL_EEPROM_ACCESS_DELAY);
		}

		if (!(r & CSR_EEPROM_REG_READ_VALID_MSK)) {
			IWL_ERROR("Time out reading EEPROM[%d]", addr);
			ret = -ETIMEDOUT;
			goto done;
		}
		e[addr / 2] = le16_to_cpu((__force __le16)(r >> 16));
	}
	ret = 0;

done:
	priv->cfg->ops->lib->eeprom_ops.release_semaphore(priv);
	return ret;
}
EXPORT_SYMBOL(iwl_eeprom_init);


void iwl_eeprom_get_mac(const struct iwl_priv *priv, u8 *mac)
{
	memcpy(mac, priv->eeprom.mac_address, 6);
}
EXPORT_SYMBOL(iwl_eeprom_get_mac);

static void iwl_init_band_reference(const struct iwl_priv *priv,
				    int band,
				    int *eeprom_ch_count,
				    const struct iwl4965_eeprom_channel
				    **eeprom_ch_info,
				    const u8 **eeprom_ch_index)
{
	switch (band) {
	case 1:		/* 2.4GHz band */
		*eeprom_ch_count = ARRAY_SIZE(iwl_eeprom_band_1);
		*eeprom_ch_info = priv->eeprom.band_1_channels;
		*eeprom_ch_index = iwl_eeprom_band_1;
		break;
	case 2:		/* 4.9GHz band */
		*eeprom_ch_count = ARRAY_SIZE(iwl_eeprom_band_2);
		*eeprom_ch_info = priv->eeprom.band_2_channels;
		*eeprom_ch_index = iwl_eeprom_band_2;
		break;
	case 3:		/* 5.2GHz band */
		*eeprom_ch_count = ARRAY_SIZE(iwl_eeprom_band_3);
		*eeprom_ch_info = priv->eeprom.band_3_channels;
		*eeprom_ch_index = iwl_eeprom_band_3;
		break;
	case 4:		/* 5.5GHz band */
		*eeprom_ch_count = ARRAY_SIZE(iwl_eeprom_band_4);
		*eeprom_ch_info = priv->eeprom.band_4_channels;
		*eeprom_ch_index = iwl_eeprom_band_4;
		break;
	case 5:		/* 5.7GHz band */
		*eeprom_ch_count = ARRAY_SIZE(iwl_eeprom_band_5);
		*eeprom_ch_info = priv->eeprom.band_5_channels;
		*eeprom_ch_index = iwl_eeprom_band_5;
		break;
	case 6:		/* 2.4GHz FAT channels */
		*eeprom_ch_count = ARRAY_SIZE(iwl_eeprom_band_6);
		*eeprom_ch_info = priv->eeprom.band_24_channels;
		*eeprom_ch_index = iwl_eeprom_band_6;
		break;
	case 7:		/* 5 GHz FAT channels */
		*eeprom_ch_count = ARRAY_SIZE(iwl_eeprom_band_7);
		*eeprom_ch_info = priv->eeprom.band_52_channels;
		*eeprom_ch_index = iwl_eeprom_band_7;
		break;
	default:
		BUG();
		return;
	}
}

#define CHECK_AND_PRINT(x) ((eeprom_ch->flags & EEPROM_CHANNEL_##x) \
			    ? # x " " : "")

/**
 * iwl4965_set_fat_chan_info - Copy fat channel info into driver's priv.
 *
 * Does not set up a command, or touch hardware.
 */
static int iwl4965_set_fat_chan_info(struct iwl_priv *priv,
			      enum ieee80211_band band, u16 channel,
			      const struct iwl4965_eeprom_channel *eeprom_ch,
			      u8 fat_extension_channel)
{
	struct iwl_channel_info *ch_info;

	ch_info = (struct iwl_channel_info *)
			iwl_get_channel_info(priv, band, channel);

	if (!is_channel_valid(ch_info))
		return -1;

	IWL_DEBUG_INFO("FAT Ch. %d [%sGHz] %s%s%s%s%s%s(0x%02x"
			" %ddBm): Ad-Hoc %ssupported\n",
			ch_info->channel,
			is_channel_a_band(ch_info) ?
			"5.2" : "2.4",
			CHECK_AND_PRINT(IBSS),
			CHECK_AND_PRINT(ACTIVE),
			CHECK_AND_PRINT(RADAR),
			CHECK_AND_PRINT(WIDE),
			CHECK_AND_PRINT(NARROW),
			CHECK_AND_PRINT(DFS),
			eeprom_ch->flags,
			eeprom_ch->max_power_avg,
			((eeprom_ch->flags & EEPROM_CHANNEL_IBSS)
			 && !(eeprom_ch->flags & EEPROM_CHANNEL_RADAR)) ?
			"" : "not ");

	ch_info->fat_eeprom = *eeprom_ch;
	ch_info->fat_max_power_avg = eeprom_ch->max_power_avg;
	ch_info->fat_curr_txpow = eeprom_ch->max_power_avg;
	ch_info->fat_min_power = 0;
	ch_info->fat_scan_power = eeprom_ch->max_power_avg;
	ch_info->fat_flags = eeprom_ch->flags;
	ch_info->fat_extension_channel = fat_extension_channel;

	return 0;
}

#define CHECK_AND_PRINT_I(x) ((eeprom_ch_info[ch].flags & EEPROM_CHANNEL_##x) \
			    ? # x " " : "")

/**
 * iwl_init_channel_map - Set up driver's info for all possible channels
 */
int iwl_init_channel_map(struct iwl_priv *priv)
{
	int eeprom_ch_count = 0;
	const u8 *eeprom_ch_index = NULL;
	const struct iwl4965_eeprom_channel *eeprom_ch_info = NULL;
	int band, ch;
	struct iwl_channel_info *ch_info;

	if (priv->channel_count) {
		IWL_DEBUG_INFO("Channel map already initialized.\n");
		return 0;
	}

	if (priv->eeprom.version < 0x2f) {
		IWL_WARNING("Unsupported EEPROM version: 0x%04X\n",
			    priv->eeprom.version);
		return -EINVAL;
	}

	IWL_DEBUG_INFO("Initializing regulatory info from EEPROM\n");

	priv->channel_count =
	    ARRAY_SIZE(iwl_eeprom_band_1) +
	    ARRAY_SIZE(iwl_eeprom_band_2) +
	    ARRAY_SIZE(iwl_eeprom_band_3) +
	    ARRAY_SIZE(iwl_eeprom_band_4) +
	    ARRAY_SIZE(iwl_eeprom_band_5);

	IWL_DEBUG_INFO("Parsing data for %d channels.\n", priv->channel_count);

	priv->channel_info = kzalloc(sizeof(struct iwl_channel_info) *
				     priv->channel_count, GFP_KERNEL);
	if (!priv->channel_info) {
		IWL_ERROR("Could not allocate channel_info\n");
		priv->channel_count = 0;
		return -ENOMEM;
	}

	ch_info = priv->channel_info;

	/* Loop through the 5 EEPROM bands adding them in order to the
	 * channel map we maintain (that contains additional information than
	 * what just in the EEPROM) */
	for (band = 1; band <= 5; band++) {

		iwl_init_band_reference(priv, band, &eeprom_ch_count,
					&eeprom_ch_info, &eeprom_ch_index);

		/* Loop through each band adding each of the channels */
		for (ch = 0; ch < eeprom_ch_count; ch++) {
			ch_info->channel = eeprom_ch_index[ch];
			ch_info->band = (band == 1) ? IEEE80211_BAND_2GHZ :
			    IEEE80211_BAND_5GHZ;

			/* permanently store EEPROM's channel regulatory flags
			 *   and max power in channel info database. */
			ch_info->eeprom = eeprom_ch_info[ch];

			/* Copy the run-time flags so they are there even on
			 * invalid channels */
			ch_info->flags = eeprom_ch_info[ch].flags;

			if (!(is_channel_valid(ch_info))) {
				IWL_DEBUG_INFO("Ch. %d Flags %x [%sGHz] - "
					       "No traffic\n",
					       ch_info->channel,
					       ch_info->flags,
					       is_channel_a_band(ch_info) ?
					       "5.2" : "2.4");
				ch_info++;
				continue;
			}

			/* Initialize regulatory-based run-time data */
			ch_info->max_power_avg = ch_info->curr_txpow =
			    eeprom_ch_info[ch].max_power_avg;
			ch_info->scan_power = eeprom_ch_info[ch].max_power_avg;
			ch_info->min_power = 0;

			IWL_DEBUG_INFO("Ch. %d [%sGHz] %s%s%s%s%s%s%s(0x%02x"
				       " %ddBm): Ad-Hoc %ssupported\n",
				       ch_info->channel,
				       is_channel_a_band(ch_info) ?
				       "5.2" : "2.4",
				       CHECK_AND_PRINT_I(VALID),
				       CHECK_AND_PRINT_I(IBSS),
				       CHECK_AND_PRINT_I(ACTIVE),
				       CHECK_AND_PRINT_I(RADAR),
				       CHECK_AND_PRINT_I(WIDE),
				       CHECK_AND_PRINT_I(NARROW),
				       CHECK_AND_PRINT_I(DFS),
				       eeprom_ch_info[ch].flags,
				       eeprom_ch_info[ch].max_power_avg,
				       ((eeprom_ch_info[ch].
					 flags & EEPROM_CHANNEL_IBSS)
					&& !(eeprom_ch_info[ch].
					     flags & EEPROM_CHANNEL_RADAR))
				       ? "" : "not ");

			/* Set the user_txpower_limit to the highest power
			 * supported by any channel */
			if (eeprom_ch_info[ch].max_power_avg >
			    priv->user_txpower_limit)
				priv->user_txpower_limit =
				    eeprom_ch_info[ch].max_power_avg;

			ch_info++;
		}
	}

	/* Two additional EEPROM bands for 2.4 and 5 GHz FAT channels */
	for (band = 6; band <= 7; band++) {
		enum ieee80211_band ieeeband;
		u8 fat_extension_chan;

		iwl_init_band_reference(priv, band, &eeprom_ch_count,
					&eeprom_ch_info, &eeprom_ch_index);

		/* EEPROM band 6 is 2.4, band 7 is 5 GHz */
		ieeeband =
			(band == 6) ? IEEE80211_BAND_2GHZ : IEEE80211_BAND_5GHZ;

		/* Loop through each band adding each of the channels */
		for (ch = 0; ch < eeprom_ch_count; ch++) {

			if ((band == 6) &&
			    ((eeprom_ch_index[ch] == 5) ||
			    (eeprom_ch_index[ch] == 6) ||
			    (eeprom_ch_index[ch] == 7)))
			       fat_extension_chan = HT_IE_EXT_CHANNEL_MAX;
			else
				fat_extension_chan = HT_IE_EXT_CHANNEL_ABOVE;

			/* Set up driver's info for lower half */
			iwl4965_set_fat_chan_info(priv, ieeeband,
						  eeprom_ch_index[ch],
						  &(eeprom_ch_info[ch]),
						  fat_extension_chan);

			/* Set up driver's info for upper half */
			iwl4965_set_fat_chan_info(priv, ieeeband,
						  (eeprom_ch_index[ch] + 4),
						  &(eeprom_ch_info[ch]),
						  HT_IE_EXT_CHANNEL_BELOW);
		}
	}

	return 0;
}
EXPORT_SYMBOL(iwl_init_channel_map);

/*
 * iwl_free_channel_map - undo allocations in iwl4965_init_channel_map
 */
void iwl_free_channel_map(struct iwl_priv *priv)
{
	kfree(priv->channel_info);
	priv->channel_count = 0;
}
EXPORT_SYMBOL(iwl_free_channel_map);

/**
 * iwl_get_channel_info - Find driver's private channel info
 *
 * Based on band and channel number.
 */
const struct iwl_channel_info *iwl_get_channel_info(
		const struct iwl_priv *priv,
		enum ieee80211_band band, u16 channel)
{
	int i;

	switch (band) {
	case IEEE80211_BAND_5GHZ:
		for (i = 14; i < priv->channel_count; i++) {
			if (priv->channel_info[i].channel == channel)
				return &priv->channel_info[i];
		}
		break;
	case IEEE80211_BAND_2GHZ:
		if (channel >= 1 && channel <= 14)
			return &priv->channel_info[channel - 1];
		break;
	default:
		BUG();
	}

	return NULL;
}
EXPORT_SYMBOL(iwl_get_channel_info);

