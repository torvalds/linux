/******************************************************************************
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
 *****************************************************************************/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/sched.h>

#include "iwl-dev.h"
#include "iwl-core.h"
#include "iwl-io.h"
#include "iwl-agn.h"

int iwlagn_send_rxon_assoc(struct iwl_priv *priv)
{
	int ret = 0;
	struct iwl5000_rxon_assoc_cmd rxon_assoc;
	const struct iwl_rxon_cmd *rxon1 = &priv->staging_rxon;
	const struct iwl_rxon_cmd *rxon2 = &priv->active_rxon;

	if ((rxon1->flags == rxon2->flags) &&
	    (rxon1->filter_flags == rxon2->filter_flags) &&
	    (rxon1->cck_basic_rates == rxon2->cck_basic_rates) &&
	    (rxon1->ofdm_ht_single_stream_basic_rates ==
	     rxon2->ofdm_ht_single_stream_basic_rates) &&
	    (rxon1->ofdm_ht_dual_stream_basic_rates ==
	     rxon2->ofdm_ht_dual_stream_basic_rates) &&
	    (rxon1->ofdm_ht_triple_stream_basic_rates ==
	     rxon2->ofdm_ht_triple_stream_basic_rates) &&
	    (rxon1->acquisition_data == rxon2->acquisition_data) &&
	    (rxon1->rx_chain == rxon2->rx_chain) &&
	    (rxon1->ofdm_basic_rates == rxon2->ofdm_basic_rates)) {
		IWL_DEBUG_INFO(priv, "Using current RXON_ASSOC.  Not resending.\n");
		return 0;
	}

	rxon_assoc.flags = priv->staging_rxon.flags;
	rxon_assoc.filter_flags = priv->staging_rxon.filter_flags;
	rxon_assoc.ofdm_basic_rates = priv->staging_rxon.ofdm_basic_rates;
	rxon_assoc.cck_basic_rates = priv->staging_rxon.cck_basic_rates;
	rxon_assoc.reserved1 = 0;
	rxon_assoc.reserved2 = 0;
	rxon_assoc.reserved3 = 0;
	rxon_assoc.ofdm_ht_single_stream_basic_rates =
	    priv->staging_rxon.ofdm_ht_single_stream_basic_rates;
	rxon_assoc.ofdm_ht_dual_stream_basic_rates =
	    priv->staging_rxon.ofdm_ht_dual_stream_basic_rates;
	rxon_assoc.rx_chain_select_flags = priv->staging_rxon.rx_chain;
	rxon_assoc.ofdm_ht_triple_stream_basic_rates =
		 priv->staging_rxon.ofdm_ht_triple_stream_basic_rates;
	rxon_assoc.acquisition_data = priv->staging_rxon.acquisition_data;

	ret = iwl_send_cmd_pdu_async(priv, REPLY_RXON_ASSOC,
				     sizeof(rxon_assoc), &rxon_assoc, NULL);
	if (ret)
		return ret;

	return ret;
}

int iwlagn_send_tx_ant_config(struct iwl_priv *priv, u8 valid_tx_ant)
{
	struct iwl_tx_ant_config_cmd tx_ant_cmd = {
	  .valid = cpu_to_le32(valid_tx_ant),
	};

	if (IWL_UCODE_API(priv->ucode_ver) > 1) {
		IWL_DEBUG_HC(priv, "select valid tx ant: %u\n", valid_tx_ant);
		return iwl_send_cmd_pdu(priv, TX_ANT_CONFIGURATION_CMD,
					sizeof(struct iwl_tx_ant_config_cmd),
					&tx_ant_cmd);
	} else {
		IWL_DEBUG_HC(priv, "TX_ANT_CONFIGURATION_CMD not supported\n");
		return -EOPNOTSUPP;
	}
}

/* Currently this is the superset of everything */
static u16 iwlagn_get_hcmd_size(u8 cmd_id, u16 len)
{
	return len;
}

static u16 iwlagn_build_addsta_hcmd(const struct iwl_addsta_cmd *cmd, u8 *data)
{
	u16 size = (u16)sizeof(struct iwl_addsta_cmd);
	struct iwl_addsta_cmd *addsta = (struct iwl_addsta_cmd *)data;
	memcpy(addsta, cmd, size);
	/* resrved in 5000 */
	addsta->rate_n_flags = cpu_to_le16(0);
	return size;
}

static void iwlagn_gain_computation(struct iwl_priv *priv,
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

		delta_g = (priv->cfg->chain_noise_scale *
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

		cmd.hdr.op_code = priv->_agn.phy_calib_chain_noise_gain_cmd;
		cmd.hdr.first_group = 0;
		cmd.hdr.groups_num = 1;
		cmd.hdr.data_valid = 1;
		cmd.delta_gain_1 = data->delta_gain_code[1];
		cmd.delta_gain_2 = data->delta_gain_code[2];
		iwl_send_cmd_pdu_async(priv, REPLY_PHY_CALIBRATION_CMD,
			sizeof(cmd), &cmd, NULL);

		data->radio_write = 1;
		data->state = IWL_CHAIN_NOISE_CALIBRATED;
	}
}

static void iwlagn_chain_noise_reset(struct iwl_priv *priv)
{
	struct iwl_chain_noise_data *data = &priv->chain_noise_data;
	int ret;

	if ((data->state == IWL_CHAIN_NOISE_ALIVE) &&
	     iwl_is_associated(priv)) {
		struct iwl_calib_chain_noise_reset_cmd cmd;

		/* clear data for chain noise calibration algorithm */
		data->chain_noise_a = 0;
		data->chain_noise_b = 0;
		data->chain_noise_c = 0;
		data->chain_signal_a = 0;
		data->chain_signal_b = 0;
		data->chain_signal_c = 0;
		data->beacon_count = 0;

		memset(&cmd, 0, sizeof(cmd));
		cmd.hdr.op_code = priv->_agn.phy_calib_chain_noise_reset_cmd;
		cmd.hdr.first_group = 0;
		cmd.hdr.groups_num = 1;
		cmd.hdr.data_valid = 1;
		ret = iwl_send_cmd_pdu(priv, REPLY_PHY_CALIBRATION_CMD,
					sizeof(cmd), &cmd);
		if (ret)
			IWL_ERR(priv,
				"Could not send REPLY_PHY_CALIBRATION_CMD\n");
		data->state = IWL_CHAIN_NOISE_ACCUMULATE;
		IWL_DEBUG_CALIB(priv, "Run chain_noise_calibrate\n");
	}
}

static void iwlagn_tx_cmd_protection(struct iwl_priv *priv,
				     struct ieee80211_tx_info *info,
				     __le16 fc, __le32 *tx_flags)
{
	if (info->control.rates[0].flags & IEEE80211_TX_RC_USE_RTS_CTS ||
	    info->control.rates[0].flags & IEEE80211_TX_RC_USE_CTS_PROTECT) {
		*tx_flags |= TX_CMD_FLG_PROT_REQUIRE_MSK;
		return;
	}

	if (priv->cfg->use_rts_for_aggregation &&
	    info->flags & IEEE80211_TX_CTL_AMPDU) {
		*tx_flags |= TX_CMD_FLG_PROT_REQUIRE_MSK;
		return;
	}
}

/* Calc max signal level (dBm) among 3 possible receivers */
static int iwlagn_calc_rssi(struct iwl_priv *priv,
			     struct iwl_rx_phy_res *rx_resp)
{
	/* data from PHY/DSP regarding signal strength, etc.,
	 *   contents are always there, not configurable by host
	 */
	struct iwl5000_non_cfg_phy *ncphy =
		(struct iwl5000_non_cfg_phy *)rx_resp->non_cfg_phy_buf;
	u32 val, rssi_a, rssi_b, rssi_c, max_rssi;
	u8 agc;

	val  = le32_to_cpu(ncphy->non_cfg_phy[IWL50_RX_RES_AGC_IDX]);
	agc = (val & IWL50_OFDM_AGC_MSK) >> IWL50_OFDM_AGC_BIT_POS;

	/* Find max rssi among 3 possible receivers.
	 * These values are measured by the digital signal processor (DSP).
	 * They should stay fairly constant even as the signal strength varies,
	 *   if the radio's automatic gain control (AGC) is working right.
	 * AGC value (see below) will provide the "interesting" info.
	 */
	val = le32_to_cpu(ncphy->non_cfg_phy[IWL50_RX_RES_RSSI_AB_IDX]);
	rssi_a = (val & IWL50_OFDM_RSSI_A_MSK) >> IWL50_OFDM_RSSI_A_BIT_POS;
	rssi_b = (val & IWL50_OFDM_RSSI_B_MSK) >> IWL50_OFDM_RSSI_B_BIT_POS;
	val = le32_to_cpu(ncphy->non_cfg_phy[IWL50_RX_RES_RSSI_C_IDX]);
	rssi_c = (val & IWL50_OFDM_RSSI_C_MSK) >> IWL50_OFDM_RSSI_C_BIT_POS;

	max_rssi = max_t(u32, rssi_a, rssi_b);
	max_rssi = max_t(u32, max_rssi, rssi_c);

	IWL_DEBUG_STATS(priv, "Rssi In A %d B %d C %d Max %d AGC dB %d\n",
		rssi_a, rssi_b, rssi_c, max_rssi, agc);

	/* dBm = max_rssi dB - agc dB - constant.
	 * Higher AGC (higher radio gain) means lower signal. */
	return max_rssi - agc - IWLAGN_RSSI_OFFSET;
}

struct iwl_hcmd_ops iwlagn_hcmd = {
	.rxon_assoc = iwlagn_send_rxon_assoc,
	.commit_rxon = iwl_commit_rxon,
	.set_rxon_chain = iwl_set_rxon_chain,
	.set_tx_ant = iwlagn_send_tx_ant_config,
	.send_bt_config = iwl_send_bt_config,
};

struct iwl_hcmd_utils_ops iwlagn_hcmd_utils = {
	.get_hcmd_size = iwlagn_get_hcmd_size,
	.build_addsta_hcmd = iwlagn_build_addsta_hcmd,
	.gain_computation = iwlagn_gain_computation,
	.chain_noise_reset = iwlagn_chain_noise_reset,
	.tx_cmd_protection = iwlagn_tx_cmd_protection,
	.calc_rssi = iwlagn_calc_rssi,
	.request_scan = iwlagn_request_scan,
};
