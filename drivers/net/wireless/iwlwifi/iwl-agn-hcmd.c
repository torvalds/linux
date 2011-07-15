/******************************************************************************
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2008 - 2011 Intel Corporation. All rights reserved.
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
 *****************************************************************************/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/sched.h>

#include "iwl-dev.h"
#include "iwl-core.h"
#include "iwl-io.h"
#include "iwl-agn.h"
#include "iwl-trans.h"

int iwlagn_send_tx_ant_config(struct iwl_priv *priv, u8 valid_tx_ant)
{
	struct iwl_tx_ant_config_cmd tx_ant_cmd = {
	  .valid = cpu_to_le32(valid_tx_ant),
	};

	if (IWL_UCODE_API(priv->ucode_ver) > 1) {
		IWL_DEBUG_HC(priv, "select valid tx ant: %u\n", valid_tx_ant);
		return trans_send_cmd_pdu(priv,
					TX_ANT_CONFIGURATION_CMD,
					CMD_SYNC,
					sizeof(struct iwl_tx_ant_config_cmd),
					&tx_ant_cmd);
	} else {
		IWL_DEBUG_HC(priv, "TX_ANT_CONFIGURATION_CMD not supported\n");
		return -EOPNOTSUPP;
	}
}

void iwlagn_gain_computation(struct iwl_priv *priv,
		u32 average_noise[NUM_RX_CHAINS],
		u16 min_average_noise_antenna_i,
		u32 min_average_noise,
		u8 default_chain)
{
	int i;
	s32 delta_g;
	struct iwl_chain_noise_data *data = &priv->chain_noise_data;

	/*
	 * Find Gain Code for the chains based on "default chain"
	 */
	for (i = default_chain + 1; i < NUM_RX_CHAINS; i++) {
		if ((data->disconn_array[i])) {
			data->delta_gain_code[i] = 0;
			continue;
		}

		delta_g = (priv->cfg->base_params->chain_noise_scale *
			((s32)average_noise[default_chain] -
			(s32)average_noise[i])) / 1500;

		/* bound gain by 2 bits value max, 3rd bit is sign */
		data->delta_gain_code[i] =
			min(abs(delta_g), (long) CHAIN_NOISE_MAX_DELTA_GAIN_CODE);

		if (delta_g < 0)
			/*
			 * set negative sign ...
			 * note to Intel developers:  This is uCode API format,
			 *   not the format of any internal device registers.
			 *   Do not change this format for e.g. 6050 or similar
			 *   devices.  Change format only if more resolution
			 *   (i.e. more than 2 bits magnitude) is needed.
			 */
			data->delta_gain_code[i] |= (1 << 2);
	}

	IWL_DEBUG_CALIB(priv, "Delta gains: ANT_B = %d  ANT_C = %d\n",
			data->delta_gain_code[1], data->delta_gain_code[2]);

	if (!data->radio_write) {
		struct iwl_calib_chain_noise_gain_cmd cmd;

		memset(&cmd, 0, sizeof(cmd));

		iwl_set_calib_hdr(&cmd.hdr,
			priv->_agn.phy_calib_chain_noise_gain_cmd);
		cmd.delta_gain_1 = data->delta_gain_code[1];
		cmd.delta_gain_2 = data->delta_gain_code[2];
		trans_send_cmd_pdu(priv, REPLY_PHY_CALIBRATION_CMD,
			CMD_ASYNC, sizeof(cmd), &cmd);

		data->radio_write = 1;
		data->state = IWL_CHAIN_NOISE_CALIBRATED;
	}
}

int iwlagn_set_pan_params(struct iwl_priv *priv)
{
	struct iwl_wipan_params_cmd cmd;
	struct iwl_rxon_context *ctx_bss, *ctx_pan;
	int slot0 = 300, slot1 = 0;
	int ret;

	if (priv->valid_contexts == BIT(IWL_RXON_CTX_BSS))
		return 0;

	BUILD_BUG_ON(NUM_IWL_RXON_CTX != 2);

	lockdep_assert_held(&priv->mutex);

	ctx_bss = &priv->contexts[IWL_RXON_CTX_BSS];
	ctx_pan = &priv->contexts[IWL_RXON_CTX_PAN];

	/*
	 * If the PAN context is inactive, then we don't need
	 * to update the PAN parameters, the last thing we'll
	 * have done before it goes inactive is making the PAN
	 * parameters be WLAN-only.
	 */
	if (!ctx_pan->is_active)
		return 0;

	memset(&cmd, 0, sizeof(cmd));

	/* only 2 slots are currently allowed */
	cmd.num_slots = 2;

	cmd.slots[0].type = 0; /* BSS */
	cmd.slots[1].type = 1; /* PAN */

	if (priv->_agn.hw_roc_channel) {
		/* both contexts must be used for this to happen */
		slot1 = priv->_agn.hw_roc_duration;
		slot0 = IWL_MIN_SLOT_TIME;
	} else if (ctx_bss->vif && ctx_pan->vif) {
		int bcnint = ctx_pan->vif->bss_conf.beacon_int;
		int dtim = ctx_pan->vif->bss_conf.dtim_period ?: 1;

		/* should be set, but seems unused?? */
		cmd.flags |= cpu_to_le16(IWL_WIPAN_PARAMS_FLG_SLOTTED_MODE);

		if (ctx_pan->vif->type == NL80211_IFTYPE_AP &&
		    bcnint &&
		    bcnint != ctx_bss->vif->bss_conf.beacon_int) {
			IWL_ERR(priv,
				"beacon intervals don't match (%d, %d)\n",
				ctx_bss->vif->bss_conf.beacon_int,
				ctx_pan->vif->bss_conf.beacon_int);
		} else
			bcnint = max_t(int, bcnint,
				       ctx_bss->vif->bss_conf.beacon_int);
		if (!bcnint)
			bcnint = DEFAULT_BEACON_INTERVAL;
		slot0 = bcnint / 2;
		slot1 = bcnint - slot0;

		if (test_bit(STATUS_SCAN_HW, &priv->status) ||
		    (!ctx_bss->vif->bss_conf.idle &&
		     !ctx_bss->vif->bss_conf.assoc)) {
			slot0 = dtim * bcnint * 3 - IWL_MIN_SLOT_TIME;
			slot1 = IWL_MIN_SLOT_TIME;
		} else if (!ctx_pan->vif->bss_conf.idle &&
			   !ctx_pan->vif->bss_conf.assoc) {
			slot1 = bcnint * 3 - IWL_MIN_SLOT_TIME;
			slot0 = IWL_MIN_SLOT_TIME;
		}
	} else if (ctx_pan->vif) {
		slot0 = 0;
		slot1 = max_t(int, 1, ctx_pan->vif->bss_conf.dtim_period) *
					ctx_pan->vif->bss_conf.beacon_int;
		slot1 = max_t(int, DEFAULT_BEACON_INTERVAL, slot1);

		if (test_bit(STATUS_SCAN_HW, &priv->status)) {
			slot0 = slot1 * 3 - IWL_MIN_SLOT_TIME;
			slot1 = IWL_MIN_SLOT_TIME;
		}
	}

	cmd.slots[0].width = cpu_to_le16(slot0);
	cmd.slots[1].width = cpu_to_le16(slot1);

	ret = trans_send_cmd_pdu(priv, REPLY_WIPAN_PARAMS, CMD_SYNC,
			sizeof(cmd), &cmd);
	if (ret)
		IWL_ERR(priv, "Error setting PAN parameters (%d)\n", ret);

	return ret;
}
