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

int iwl_eeprom_check_sku(struct iwl_priv *priv)
{
	u16 eeprom_sku;
	u16 radio_cfg;

	eeprom_sku = iwl_eeprom_query16(priv, EEPROM_SKU_CAP);

	priv->cfg->sku = ((eeprom_sku & EEPROM_SKU_CAP_BAND_SELECTION) >>
			EEPROM_SKU_CAP_BAND_POS);
	if (eeprom_sku & EEPROM_SKU_CAP_11N_ENABLE)
		priv->cfg->sku |= IWL_SKU_N;

	if (!priv->cfg->sku) {
		IWL_ERR(priv, "Invalid device sku\n");
		return -EINVAL;
	}

	IWL_INFO(priv, "Device SKU: 0X%x\n", priv->cfg->sku);

	if (!priv->cfg->valid_tx_ant && !priv->cfg->valid_rx_ant) {
		/* not using .cfg overwrite */
		radio_cfg = iwl_eeprom_query16(priv, EEPROM_RADIO_CONFIG);
		priv->cfg->valid_tx_ant = EEPROM_RF_CFG_TX_ANT_MSK(radio_cfg);
		priv->cfg->valid_rx_ant = EEPROM_RF_CFG_TX_ANT_MSK(radio_cfg);
		if (!priv->cfg->valid_tx_ant || !priv->cfg->valid_rx_ant) {
			IWL_ERR(priv, "Invalid chain (0X%x, 0X%x)\n",
				priv->cfg->valid_tx_ant,
				priv->cfg->valid_rx_ant);
			return -EINVAL;
		}
		IWL_INFO(priv, "Valid Tx ant: 0X%x, Valid Rx ant: 0X%x\n",
			 priv->cfg->valid_tx_ant, priv->cfg->valid_rx_ant);
	}
	/*
	 * for some special cases,
	 * EEPROM did not reflect the correct antenna setting
	 * so overwrite the valid tx/rx antenna from .cfg
	 */
	return 0;
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

	IWL_DEBUG_EEPROM(priv, "%d - "
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

void iwlcore_eeprom_enhanced_txpower(struct iwl_priv *priv)
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
