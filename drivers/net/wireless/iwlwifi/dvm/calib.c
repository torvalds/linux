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
 * in the file called LICENSE.GPL.
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

#include <linux/slab.h>
#include <net/mac80211.h>

#include "iwl-trans.h"

#include "dev.h"
#include "calib.h"
#include "agn.h"

/*****************************************************************************
 * INIT calibrations framework
 *****************************************************************************/

/* Opaque calibration results */
struct iwl_calib_result {
	struct list_head list;
	size_t cmd_len;
	struct iwl_calib_hdr hdr;
	/* data follows */
};

struct statistics_general_data {
	u32 beacon_silence_rssi_a;
	u32 beacon_silence_rssi_b;
	u32 beacon_silence_rssi_c;
	u32 beacon_energy_a;
	u32 beacon_energy_b;
	u32 beacon_energy_c;
};

int iwl_send_calib_results(struct iwl_priv *priv)
{
	struct iwl_host_cmd hcmd = {
		.id = REPLY_PHY_CALIBRATION_CMD,
		.flags = CMD_SYNC,
	};
	struct iwl_calib_result *res;

	list_for_each_entry(res, &priv->calib_results, list) {
		int ret;

		hcmd.len[0] = res->cmd_len;
		hcmd.data[0] = &res->hdr;
		hcmd.dataflags[0] = IWL_HCMD_DFL_NOCOPY;
		ret = iwl_dvm_send_cmd(priv, &hcmd);
		if (ret) {
			IWL_ERR(priv, "Error %d on calib cmd %d\n",
				ret, res->hdr.op_code);
			return ret;
		}
	}

	return 0;
}

int iwl_calib_set(struct iwl_priv *priv,
		  const struct iwl_calib_hdr *cmd, int len)
{
	struct iwl_calib_result *res, *tmp;

	res = kmalloc(sizeof(*res) + len - sizeof(struct iwl_calib_hdr),
		      GFP_ATOMIC);
	if (!res)
		return -ENOMEM;
	memcpy(&res->hdr, cmd, len);
	res->cmd_len = len;

	list_for_each_entry(tmp, &priv->calib_results, list) {
		if (tmp->hdr.op_code == res->hdr.op_code) {
			list_replace(&tmp->list, &res->list);
			kfree(tmp);
			return 0;
		}
	}

	/* wasn't in list already */
	list_add_tail(&res->list, &priv->calib_results);

	return 0;
}

void iwl_calib_free_results(struct iwl_priv *priv)
{
	struct iwl_calib_result *res, *tmp;

	list_for_each_entry_safe(res, tmp, &priv->calib_results, list) {
		list_del(&res->list);
		kfree(res);
	}
}

/*****************************************************************************
 * RUNTIME calibrations framework
 *****************************************************************************/

/* "false alarms" are signals that our DSP tries to lock onto,
 *   but then determines that they are either noise, or transmissions
 *   from a distant wireless network (also "noise", really) that get
 *   "stepped on" by stronger transmissions within our own network.
 * This algorithm attempts to set a sensitivity level that is high
 *   enough to receive all of our own network traffic, but not so
 *   high that our DSP gets too busy trying to lock onto non-network
 *   activity/noise. */
static int iwl_sens_energy_cck(struct iwl_priv *priv,
				   u32 norm_fa,
				   u32 rx_enable_time,
				   struct statistics_general_data *rx_info)
{
	u32 max_nrg_cck = 0;
	int i = 0;
	u8 max_silence_rssi = 0;
	u32 silence_ref = 0;
	u8 silence_rssi_a = 0;
	u8 silence_rssi_b = 0;
	u8 silence_rssi_c = 0;
	u32 val;

	/* "false_alarms" values below are cross-multiplications to assess the
	 *   numbers of false alarms within the measured period of actual Rx
	 *   (Rx is off when we're txing), vs the min/max expected false alarms
	 *   (some should be expected if rx is sensitive enough) in a
	 *   hypothetical listening period of 200 time units (TU), 204.8 msec:
	 *
	 * MIN_FA/fixed-time < false_alarms/actual-rx-time < MAX_FA/beacon-time
	 *
	 * */
	u32 false_alarms = norm_fa * 200 * 1024;
	u32 max_false_alarms = MAX_FA_CCK * rx_enable_time;
	u32 min_false_alarms = MIN_FA_CCK * rx_enable_time;
	struct iwl_sensitivity_data *data = NULL;
	const struct iwl_sensitivity_ranges *ranges = priv->hw_params.sens;

	data = &(priv->sensitivity_data);

	data->nrg_auto_corr_silence_diff = 0;

	/* Find max silence rssi among all 3 receivers.
	 * This is background noise, which may include transmissions from other
	 *    networks, measured during silence before our network's beacon */
	silence_rssi_a = (u8)((rx_info->beacon_silence_rssi_a &
			    ALL_BAND_FILTER) >> 8);
	silence_rssi_b = (u8)((rx_info->beacon_silence_rssi_b &
			    ALL_BAND_FILTER) >> 8);
	silence_rssi_c = (u8)((rx_info->beacon_silence_rssi_c &
			    ALL_BAND_FILTER) >> 8);

	val = max(silence_rssi_b, silence_rssi_c);
	max_silence_rssi = max(silence_rssi_a, (u8) val);

	/* Store silence rssi in 20-beacon history table */
	data->nrg_silence_rssi[data->nrg_silence_idx] = max_silence_rssi;
	data->nrg_silence_idx++;
	if (data->nrg_silence_idx >= NRG_NUM_PREV_STAT_L)
		data->nrg_silence_idx = 0;

	/* Find max silence rssi across 20 beacon history */
	for (i = 0; i < NRG_NUM_PREV_STAT_L; i++) {
		val = data->nrg_silence_rssi[i];
		silence_ref = max(silence_ref, val);
	}
	IWL_DEBUG_CALIB(priv, "silence a %u, b %u, c %u, 20-bcn max %u\n",
			silence_rssi_a, silence_rssi_b, silence_rssi_c,
			silence_ref);

	/* Find max rx energy (min value!) among all 3 receivers,
	 *   measured during beacon frame.
	 * Save it in 10-beacon history table. */
	i = data->nrg_energy_idx;
	val = min(rx_info->beacon_energy_b, rx_info->beacon_energy_c);
	data->nrg_value[i] = min(rx_info->beacon_energy_a, val);

	data->nrg_energy_idx++;
	if (data->nrg_energy_idx >= 10)
		data->nrg_energy_idx = 0;

	/* Find min rx energy (max value) across 10 beacon history.
	 * This is the minimum signal level that we want to receive well.
	 * Add backoff (margin so we don't miss slightly lower energy frames).
	 * This establishes an upper bound (min value) for energy threshold. */
	max_nrg_cck = data->nrg_value[0];
	for (i = 1; i < 10; i++)
		max_nrg_cck = (u32) max(max_nrg_cck, (data->nrg_value[i]));
	max_nrg_cck += 6;

	IWL_DEBUG_CALIB(priv, "rx energy a %u, b %u, c %u, 10-bcn max/min %u\n",
			rx_info->beacon_energy_a, rx_info->beacon_energy_b,
			rx_info->beacon_energy_c, max_nrg_cck - 6);

	/* Count number of consecutive beacons with fewer-than-desired
	 *   false alarms. */
	if (false_alarms < min_false_alarms)
		data->num_in_cck_no_fa++;
	else
		data->num_in_cck_no_fa = 0;
	IWL_DEBUG_CALIB(priv, "consecutive bcns with few false alarms = %u\n",
			data->num_in_cck_no_fa);

	/* If we got too many false alarms this time, reduce sensitivity */
	if ((false_alarms > max_false_alarms) &&
		(data->auto_corr_cck > AUTO_CORR_MAX_TH_CCK)) {
		IWL_DEBUG_CALIB(priv, "norm FA %u > max FA %u\n",
		     false_alarms, max_false_alarms);
		IWL_DEBUG_CALIB(priv, "... reducing sensitivity\n");
		data->nrg_curr_state = IWL_FA_TOO_MANY;
		/* Store for "fewer than desired" on later beacon */
		data->nrg_silence_ref = silence_ref;

		/* increase energy threshold (reduce nrg value)
		 *   to decrease sensitivity */
		data->nrg_th_cck = data->nrg_th_cck - NRG_STEP_CCK;
	/* Else if we got fewer than desired, increase sensitivity */
	} else if (false_alarms < min_false_alarms) {
		data->nrg_curr_state = IWL_FA_TOO_FEW;

		/* Compare silence level with silence level for most recent
		 *   healthy number or too many false alarms */
		data->nrg_auto_corr_silence_diff = (s32)data->nrg_silence_ref -
						   (s32)silence_ref;

		IWL_DEBUG_CALIB(priv, "norm FA %u < min FA %u, silence diff %d\n",
			 false_alarms, min_false_alarms,
			 data->nrg_auto_corr_silence_diff);

		/* Increase value to increase sensitivity, but only if:
		 * 1a) previous beacon did *not* have *too many* false alarms
		 * 1b) AND there's a significant difference in Rx levels
		 *      from a previous beacon with too many, or healthy # FAs
		 * OR 2) We've seen a lot of beacons (100) with too few
		 *       false alarms */
		if ((data->nrg_prev_state != IWL_FA_TOO_MANY) &&
			((data->nrg_auto_corr_silence_diff > NRG_DIFF) ||
			(data->num_in_cck_no_fa > MAX_NUMBER_CCK_NO_FA))) {

			IWL_DEBUG_CALIB(priv, "... increasing sensitivity\n");
			/* Increase nrg value to increase sensitivity */
			val = data->nrg_th_cck + NRG_STEP_CCK;
			data->nrg_th_cck = min((u32)ranges->min_nrg_cck, val);
		} else {
			IWL_DEBUG_CALIB(priv, "... but not changing sensitivity\n");
		}

	/* Else we got a healthy number of false alarms, keep status quo */
	} else {
		IWL_DEBUG_CALIB(priv, " FA in safe zone\n");
		data->nrg_curr_state = IWL_FA_GOOD_RANGE;

		/* Store for use in "fewer than desired" with later beacon */
		data->nrg_silence_ref = silence_ref;

		/* If previous beacon had too many false alarms,
		 *   give it some extra margin by reducing sensitivity again
		 *   (but don't go below measured energy of desired Rx) */
		if (IWL_FA_TOO_MANY == data->nrg_prev_state) {
			IWL_DEBUG_CALIB(priv, "... increasing margin\n");
			if (data->nrg_th_cck > (max_nrg_cck + NRG_MARGIN))
				data->nrg_th_cck -= NRG_MARGIN;
			else
				data->nrg_th_cck = max_nrg_cck;
		}
	}

	/* Make sure the energy threshold does not go above the measured
	 * energy of the desired Rx signals (reduced by backoff margin),
	 * or else we might start missing Rx frames.
	 * Lower value is higher energy, so we use max()!
	 */
	data->nrg_th_cck = max(max_nrg_cck, data->nrg_th_cck);
	IWL_DEBUG_CALIB(priv, "new nrg_th_cck %u\n", data->nrg_th_cck);

	data->nrg_prev_state = data->nrg_curr_state;

	/* Auto-correlation CCK algorithm */
	if (false_alarms > min_false_alarms) {

		/* increase auto_corr values to decrease sensitivity
		 * so the DSP won't be disturbed by the noise
		 */
		if (data->auto_corr_cck < AUTO_CORR_MAX_TH_CCK)
			data->auto_corr_cck = AUTO_CORR_MAX_TH_CCK + 1;
		else {
			val = data->auto_corr_cck + AUTO_CORR_STEP_CCK;
			data->auto_corr_cck =
				min((u32)ranges->auto_corr_max_cck, val);
		}
		val = data->auto_corr_cck_mrc + AUTO_CORR_STEP_CCK;
		data->auto_corr_cck_mrc =
			min((u32)ranges->auto_corr_max_cck_mrc, val);
	} else if ((false_alarms < min_false_alarms) &&
	   ((data->nrg_auto_corr_silence_diff > NRG_DIFF) ||
	   (data->num_in_cck_no_fa > MAX_NUMBER_CCK_NO_FA))) {

		/* Decrease auto_corr values to increase sensitivity */
		val = data->auto_corr_cck - AUTO_CORR_STEP_CCK;
		data->auto_corr_cck =
			max((u32)ranges->auto_corr_min_cck, val);
		val = data->auto_corr_cck_mrc - AUTO_CORR_STEP_CCK;
		data->auto_corr_cck_mrc =
			max((u32)ranges->auto_corr_min_cck_mrc, val);
	}

	return 0;
}


static int iwl_sens_auto_corr_ofdm(struct iwl_priv *priv,
				       u32 norm_fa,
				       u32 rx_enable_time)
{
	u32 val;
	u32 false_alarms = norm_fa * 200 * 1024;
	u32 max_false_alarms = MAX_FA_OFDM * rx_enable_time;
	u32 min_false_alarms = MIN_FA_OFDM * rx_enable_time;
	struct iwl_sensitivity_data *data = NULL;
	const struct iwl_sensitivity_ranges *ranges = priv->hw_params.sens;

	data = &(priv->sensitivity_data);

	/* If we got too many false alarms this time, reduce sensitivity */
	if (false_alarms > max_false_alarms) {

		IWL_DEBUG_CALIB(priv, "norm FA %u > max FA %u)\n",
			     false_alarms, max_false_alarms);

		val = data->auto_corr_ofdm + AUTO_CORR_STEP_OFDM;
		data->auto_corr_ofdm =
			min((u32)ranges->auto_corr_max_ofdm, val);

		val = data->auto_corr_ofdm_mrc + AUTO_CORR_STEP_OFDM;
		data->auto_corr_ofdm_mrc =
			min((u32)ranges->auto_corr_max_ofdm_mrc, val);

		val = data->auto_corr_ofdm_x1 + AUTO_CORR_STEP_OFDM;
		data->auto_corr_ofdm_x1 =
			min((u32)ranges->auto_corr_max_ofdm_x1, val);

		val = data->auto_corr_ofdm_mrc_x1 + AUTO_CORR_STEP_OFDM;
		data->auto_corr_ofdm_mrc_x1 =
			min((u32)ranges->auto_corr_max_ofdm_mrc_x1, val);
	}

	/* Else if we got fewer than desired, increase sensitivity */
	else if (false_alarms < min_false_alarms) {

		IWL_DEBUG_CALIB(priv, "norm FA %u < min FA %u\n",
			     false_alarms, min_false_alarms);

		val = data->auto_corr_ofdm - AUTO_CORR_STEP_OFDM;
		data->auto_corr_ofdm =
			max((u32)ranges->auto_corr_min_ofdm, val);

		val = data->auto_corr_ofdm_mrc - AUTO_CORR_STEP_OFDM;
		data->auto_corr_ofdm_mrc =
			max((u32)ranges->auto_corr_min_ofdm_mrc, val);

		val = data->auto_corr_ofdm_x1 - AUTO_CORR_STEP_OFDM;
		data->auto_corr_ofdm_x1 =
			max((u32)ranges->auto_corr_min_ofdm_x1, val);

		val = data->auto_corr_ofdm_mrc_x1 - AUTO_CORR_STEP_OFDM;
		data->auto_corr_ofdm_mrc_x1 =
			max((u32)ranges->auto_corr_min_ofdm_mrc_x1, val);
	} else {
		IWL_DEBUG_CALIB(priv, "min FA %u < norm FA %u < max FA %u OK\n",
			 min_false_alarms, false_alarms, max_false_alarms);
	}
	return 0;
}

static void iwl_prepare_legacy_sensitivity_tbl(struct iwl_priv *priv,
				struct iwl_sensitivity_data *data,
				__le16 *tbl)
{
	tbl[HD_AUTO_CORR32_X4_TH_ADD_MIN_INDEX] =
				cpu_to_le16((u16)data->auto_corr_ofdm);
	tbl[HD_AUTO_CORR32_X4_TH_ADD_MIN_MRC_INDEX] =
				cpu_to_le16((u16)data->auto_corr_ofdm_mrc);
	tbl[HD_AUTO_CORR32_X1_TH_ADD_MIN_INDEX] =
				cpu_to_le16((u16)data->auto_corr_ofdm_x1);
	tbl[HD_AUTO_CORR32_X1_TH_ADD_MIN_MRC_INDEX] =
				cpu_to_le16((u16)data->auto_corr_ofdm_mrc_x1);

	tbl[HD_AUTO_CORR40_X4_TH_ADD_MIN_INDEX] =
				cpu_to_le16((u16)data->auto_corr_cck);
	tbl[HD_AUTO_CORR40_X4_TH_ADD_MIN_MRC_INDEX] =
				cpu_to_le16((u16)data->auto_corr_cck_mrc);

	tbl[HD_MIN_ENERGY_CCK_DET_INDEX] =
				cpu_to_le16((u16)data->nrg_th_cck);
	tbl[HD_MIN_ENERGY_OFDM_DET_INDEX] =
				cpu_to_le16((u16)data->nrg_th_ofdm);

	tbl[HD_BARKER_CORR_TH_ADD_MIN_INDEX] =
				cpu_to_le16(data->barker_corr_th_min);
	tbl[HD_BARKER_CORR_TH_ADD_MIN_MRC_INDEX] =
				cpu_to_le16(data->barker_corr_th_min_mrc);
	tbl[HD_OFDM_ENERGY_TH_IN_INDEX] =
				cpu_to_le16(data->nrg_th_cca);

	IWL_DEBUG_CALIB(priv, "ofdm: ac %u mrc %u x1 %u mrc_x1 %u thresh %u\n",
			data->auto_corr_ofdm, data->auto_corr_ofdm_mrc,
			data->auto_corr_ofdm_x1, data->auto_corr_ofdm_mrc_x1,
			data->nrg_th_ofdm);

	IWL_DEBUG_CALIB(priv, "cck: ac %u mrc %u thresh %u\n",
			data->auto_corr_cck, data->auto_corr_cck_mrc,
			data->nrg_th_cck);
}

/* Prepare a SENSITIVITY_CMD, send to uCode if values have changed */
static int iwl_sensitivity_write(struct iwl_priv *priv)
{
	struct iwl_sensitivity_cmd cmd;
	struct iwl_sensitivity_data *data = NULL;
	struct iwl_host_cmd cmd_out = {
		.id = SENSITIVITY_CMD,
		.len = { sizeof(struct iwl_sensitivity_cmd), },
		.flags = CMD_ASYNC,
		.data = { &cmd, },
	};

	data = &(priv->sensitivity_data);

	memset(&cmd, 0, sizeof(cmd));

	iwl_prepare_legacy_sensitivity_tbl(priv, data, &cmd.table[0]);

	/* Update uCode's "work" table, and copy it to DSP */
	cmd.control = SENSITIVITY_CMD_CONTROL_WORK_TABLE;

	/* Don't send command to uCode if nothing has changed */
	if (!memcmp(&cmd.table[0], &(priv->sensitivity_tbl[0]),
		    sizeof(u16)*HD_TABLE_SIZE)) {
		IWL_DEBUG_CALIB(priv, "No change in SENSITIVITY_CMD\n");
		return 0;
	}

	/* Copy table for comparison next time */
	memcpy(&(priv->sensitivity_tbl[0]), &(cmd.table[0]),
	       sizeof(u16)*HD_TABLE_SIZE);

	return iwl_dvm_send_cmd(priv, &cmd_out);
}

/* Prepare a SENSITIVITY_CMD, send to uCode if values have changed */
static int iwl_enhance_sensitivity_write(struct iwl_priv *priv)
{
	struct iwl_enhance_sensitivity_cmd cmd;
	struct iwl_sensitivity_data *data = NULL;
	struct iwl_host_cmd cmd_out = {
		.id = SENSITIVITY_CMD,
		.len = { sizeof(struct iwl_enhance_sensitivity_cmd), },
		.flags = CMD_ASYNC,
		.data = { &cmd, },
	};

	data = &(priv->sensitivity_data);

	memset(&cmd, 0, sizeof(cmd));

	iwl_prepare_legacy_sensitivity_tbl(priv, data, &cmd.enhance_table[0]);

	if (priv->cfg->base_params->hd_v2) {
		cmd.enhance_table[HD_INA_NON_SQUARE_DET_OFDM_INDEX] =
			HD_INA_NON_SQUARE_DET_OFDM_DATA_V2;
		cmd.enhance_table[HD_INA_NON_SQUARE_DET_CCK_INDEX] =
			HD_INA_NON_SQUARE_DET_CCK_DATA_V2;
		cmd.enhance_table[HD_CORR_11_INSTEAD_OF_CORR_9_EN_INDEX] =
			HD_CORR_11_INSTEAD_OF_CORR_9_EN_DATA_V2;
		cmd.enhance_table[HD_OFDM_NON_SQUARE_DET_SLOPE_MRC_INDEX] =
			HD_OFDM_NON_SQUARE_DET_SLOPE_MRC_DATA_V2;
		cmd.enhance_table[HD_OFDM_NON_SQUARE_DET_INTERCEPT_MRC_INDEX] =
			HD_OFDM_NON_SQUARE_DET_INTERCEPT_MRC_DATA_V2;
		cmd.enhance_table[HD_OFDM_NON_SQUARE_DET_SLOPE_INDEX] =
			HD_OFDM_NON_SQUARE_DET_SLOPE_DATA_V2;
		cmd.enhance_table[HD_OFDM_NON_SQUARE_DET_INTERCEPT_INDEX] =
			HD_OFDM_NON_SQUARE_DET_INTERCEPT_DATA_V2;
		cmd.enhance_table[HD_CCK_NON_SQUARE_DET_SLOPE_MRC_INDEX] =
			HD_CCK_NON_SQUARE_DET_SLOPE_MRC_DATA_V2;
		cmd.enhance_table[HD_CCK_NON_SQUARE_DET_INTERCEPT_MRC_INDEX] =
			HD_CCK_NON_SQUARE_DET_INTERCEPT_MRC_DATA_V2;
		cmd.enhance_table[HD_CCK_NON_SQUARE_DET_SLOPE_INDEX] =
			HD_CCK_NON_SQUARE_DET_SLOPE_DATA_V2;
		cmd.enhance_table[HD_CCK_NON_SQUARE_DET_INTERCEPT_INDEX] =
			HD_CCK_NON_SQUARE_DET_INTERCEPT_DATA_V2;
	} else {
		cmd.enhance_table[HD_INA_NON_SQUARE_DET_OFDM_INDEX] =
			HD_INA_NON_SQUARE_DET_OFDM_DATA_V1;
		cmd.enhance_table[HD_INA_NON_SQUARE_DET_CCK_INDEX] =
			HD_INA_NON_SQUARE_DET_CCK_DATA_V1;
		cmd.enhance_table[HD_CORR_11_INSTEAD_OF_CORR_9_EN_INDEX] =
			HD_CORR_11_INSTEAD_OF_CORR_9_EN_DATA_V1;
		cmd.enhance_table[HD_OFDM_NON_SQUARE_DET_SLOPE_MRC_INDEX] =
			HD_OFDM_NON_SQUARE_DET_SLOPE_MRC_DATA_V1;
		cmd.enhance_table[HD_OFDM_NON_SQUARE_DET_INTERCEPT_MRC_INDEX] =
			HD_OFDM_NON_SQUARE_DET_INTERCEPT_MRC_DATA_V1;
		cmd.enhance_table[HD_OFDM_NON_SQUARE_DET_SLOPE_INDEX] =
			HD_OFDM_NON_SQUARE_DET_SLOPE_DATA_V1;
		cmd.enhance_table[HD_OFDM_NON_SQUARE_DET_INTERCEPT_INDEX] =
			HD_OFDM_NON_SQUARE_DET_INTERCEPT_DATA_V1;
		cmd.enhance_table[HD_CCK_NON_SQUARE_DET_SLOPE_MRC_INDEX] =
			HD_CCK_NON_SQUARE_DET_SLOPE_MRC_DATA_V1;
		cmd.enhance_table[HD_CCK_NON_SQUARE_DET_INTERCEPT_MRC_INDEX] =
			HD_CCK_NON_SQUARE_DET_INTERCEPT_MRC_DATA_V1;
		cmd.enhance_table[HD_CCK_NON_SQUARE_DET_SLOPE_INDEX] =
			HD_CCK_NON_SQUARE_DET_SLOPE_DATA_V1;
		cmd.enhance_table[HD_CCK_NON_SQUARE_DET_INTERCEPT_INDEX] =
			HD_CCK_NON_SQUARE_DET_INTERCEPT_DATA_V1;
	}

	/* Update uCode's "work" table, and copy it to DSP */
	cmd.control = SENSITIVITY_CMD_CONTROL_WORK_TABLE;

	/* Don't send command to uCode if nothing has changed */
	if (!memcmp(&cmd.enhance_table[0], &(priv->sensitivity_tbl[0]),
		    sizeof(u16)*HD_TABLE_SIZE) &&
	    !memcmp(&cmd.enhance_table[HD_INA_NON_SQUARE_DET_OFDM_INDEX],
		    &(priv->enhance_sensitivity_tbl[0]),
		    sizeof(u16)*ENHANCE_HD_TABLE_ENTRIES)) {
		IWL_DEBUG_CALIB(priv, "No change in SENSITIVITY_CMD\n");
		return 0;
	}

	/* Copy table for comparison next time */
	memcpy(&(priv->sensitivity_tbl[0]), &(cmd.enhance_table[0]),
	       sizeof(u16)*HD_TABLE_SIZE);
	memcpy(&(priv->enhance_sensitivity_tbl[0]),
	       &(cmd.enhance_table[HD_INA_NON_SQUARE_DET_OFDM_INDEX]),
	       sizeof(u16)*ENHANCE_HD_TABLE_ENTRIES);

	return iwl_dvm_send_cmd(priv, &cmd_out);
}

void iwl_init_sensitivity(struct iwl_priv *priv)
{
	int ret = 0;
	int i;
	struct iwl_sensitivity_data *data = NULL;
	const struct iwl_sensitivity_ranges *ranges = priv->hw_params.sens;

	if (priv->calib_disabled & IWL_SENSITIVITY_CALIB_DISABLED)
		return;

	IWL_DEBUG_CALIB(priv, "Start iwl_init_sensitivity\n");

	/* Clear driver's sensitivity algo data */
	data = &(priv->sensitivity_data);

	if (ranges == NULL)
		return;

	memset(data, 0, sizeof(struct iwl_sensitivity_data));

	data->num_in_cck_no_fa = 0;
	data->nrg_curr_state = IWL_FA_TOO_MANY;
	data->nrg_prev_state = IWL_FA_TOO_MANY;
	data->nrg_silence_ref = 0;
	data->nrg_silence_idx = 0;
	data->nrg_energy_idx = 0;

	for (i = 0; i < 10; i++)
		data->nrg_value[i] = 0;

	for (i = 0; i < NRG_NUM_PREV_STAT_L; i++)
		data->nrg_silence_rssi[i] = 0;

	data->auto_corr_ofdm =  ranges->auto_corr_min_ofdm;
	data->auto_corr_ofdm_mrc = ranges->auto_corr_min_ofdm_mrc;
	data->auto_corr_ofdm_x1  = ranges->auto_corr_min_ofdm_x1;
	data->auto_corr_ofdm_mrc_x1 = ranges->auto_corr_min_ofdm_mrc_x1;
	data->auto_corr_cck = AUTO_CORR_CCK_MIN_VAL_DEF;
	data->auto_corr_cck_mrc = ranges->auto_corr_min_cck_mrc;
	data->nrg_th_cck = ranges->nrg_th_cck;
	data->nrg_th_ofdm = ranges->nrg_th_ofdm;
	data->barker_corr_th_min = ranges->barker_corr_th_min;
	data->barker_corr_th_min_mrc = ranges->barker_corr_th_min_mrc;
	data->nrg_th_cca = ranges->nrg_th_cca;

	data->last_bad_plcp_cnt_ofdm = 0;
	data->last_fa_cnt_ofdm = 0;
	data->last_bad_plcp_cnt_cck = 0;
	data->last_fa_cnt_cck = 0;

	if (priv->fw->enhance_sensitivity_table)
		ret |= iwl_enhance_sensitivity_write(priv);
	else
		ret |= iwl_sensitivity_write(priv);
	IWL_DEBUG_CALIB(priv, "<<return 0x%X\n", ret);
}

void iwl_sensitivity_calibration(struct iwl_priv *priv)
{
	u32 rx_enable_time;
	u32 fa_cck;
	u32 fa_ofdm;
	u32 bad_plcp_cck;
	u32 bad_plcp_ofdm;
	u32 norm_fa_ofdm;
	u32 norm_fa_cck;
	struct iwl_sensitivity_data *data = NULL;
	struct statistics_rx_non_phy *rx_info;
	struct statistics_rx_phy *ofdm, *cck;
	struct statistics_general_data statis;

	if (priv->calib_disabled & IWL_SENSITIVITY_CALIB_DISABLED)
		return;

	data = &(priv->sensitivity_data);

	if (!iwl_is_any_associated(priv)) {
		IWL_DEBUG_CALIB(priv, "<< - not associated\n");
		return;
	}

	spin_lock_bh(&priv->statistics.lock);
	rx_info = &priv->statistics.rx_non_phy;
	ofdm = &priv->statistics.rx_ofdm;
	cck = &priv->statistics.rx_cck;
	if (rx_info->interference_data_flag != INTERFERENCE_DATA_AVAILABLE) {
		IWL_DEBUG_CALIB(priv, "<< invalid data.\n");
		spin_unlock_bh(&priv->statistics.lock);
		return;
	}

	/* Extract Statistics: */
	rx_enable_time = le32_to_cpu(rx_info->channel_load);
	fa_cck = le32_to_cpu(cck->false_alarm_cnt);
	fa_ofdm = le32_to_cpu(ofdm->false_alarm_cnt);
	bad_plcp_cck = le32_to_cpu(cck->plcp_err);
	bad_plcp_ofdm = le32_to_cpu(ofdm->plcp_err);

	statis.beacon_silence_rssi_a =
			le32_to_cpu(rx_info->beacon_silence_rssi_a);
	statis.beacon_silence_rssi_b =
			le32_to_cpu(rx_info->beacon_silence_rssi_b);
	statis.beacon_silence_rssi_c =
			le32_to_cpu(rx_info->beacon_silence_rssi_c);
	statis.beacon_energy_a =
			le32_to_cpu(rx_info->beacon_energy_a);
	statis.beacon_energy_b =
			le32_to_cpu(rx_info->beacon_energy_b);
	statis.beacon_energy_c =
			le32_to_cpu(rx_info->beacon_energy_c);

	spin_unlock_bh(&priv->statistics.lock);

	IWL_DEBUG_CALIB(priv, "rx_enable_time = %u usecs\n", rx_enable_time);

	if (!rx_enable_time) {
		IWL_DEBUG_CALIB(priv, "<< RX Enable Time == 0!\n");
		return;
	}

	/* These statistics increase monotonically, and do not reset
	 *   at each beacon.  Calculate difference from last value, or just
	 *   use the new statistics value if it has reset or wrapped around. */
	if (data->last_bad_plcp_cnt_cck > bad_plcp_cck)
		data->last_bad_plcp_cnt_cck = bad_plcp_cck;
	else {
		bad_plcp_cck -= data->last_bad_plcp_cnt_cck;
		data->last_bad_plcp_cnt_cck += bad_plcp_cck;
	}

	if (data->last_bad_plcp_cnt_ofdm > bad_plcp_ofdm)
		data->last_bad_plcp_cnt_ofdm = bad_plcp_ofdm;
	else {
		bad_plcp_ofdm -= data->last_bad_plcp_cnt_ofdm;
		data->last_bad_plcp_cnt_ofdm += bad_plcp_ofdm;
	}

	if (data->last_fa_cnt_ofdm > fa_ofdm)
		data->last_fa_cnt_ofdm = fa_ofdm;
	else {
		fa_ofdm -= data->last_fa_cnt_ofdm;
		data->last_fa_cnt_ofdm += fa_ofdm;
	}

	if (data->last_fa_cnt_cck > fa_cck)
		data->last_fa_cnt_cck = fa_cck;
	else {
		fa_cck -= data->last_fa_cnt_cck;
		data->last_fa_cnt_cck += fa_cck;
	}

	/* Total aborted signal locks */
	norm_fa_ofdm = fa_ofdm + bad_plcp_ofdm;
	norm_fa_cck = fa_cck + bad_plcp_cck;

	IWL_DEBUG_CALIB(priv, "cck: fa %u badp %u  ofdm: fa %u badp %u\n", fa_cck,
			bad_plcp_cck, fa_ofdm, bad_plcp_ofdm);

	iwl_sens_auto_corr_ofdm(priv, norm_fa_ofdm, rx_enable_time);
	iwl_sens_energy_cck(priv, norm_fa_cck, rx_enable_time, &statis);
	if (priv->fw->enhance_sensitivity_table)
		iwl_enhance_sensitivity_write(priv);
	else
		iwl_sensitivity_write(priv);
}

static inline u8 find_first_chain(u8 mask)
{
	if (mask & ANT_A)
		return CHAIN_A;
	if (mask & ANT_B)
		return CHAIN_B;
	return CHAIN_C;
}

/**
 * Run disconnected antenna algorithm to find out which antennas are
 * disconnected.
 */
static void iwl_find_disconn_antenna(struct iwl_priv *priv, u32* average_sig,
				     struct iwl_chain_noise_data *data)
{
	u32 active_chains = 0;
	u32 max_average_sig;
	u16 max_average_sig_antenna_i;
	u8 num_tx_chains;
	u8 first_chain;
	u16 i = 0;

	average_sig[0] = data->chain_signal_a / IWL_CAL_NUM_BEACONS;
	average_sig[1] = data->chain_signal_b / IWL_CAL_NUM_BEACONS;
	average_sig[2] = data->chain_signal_c / IWL_CAL_NUM_BEACONS;

	if (average_sig[0] >= average_sig[1]) {
		max_average_sig = average_sig[0];
		max_average_sig_antenna_i = 0;
		active_chains = (1 << max_average_sig_antenna_i);
	} else {
		max_average_sig = average_sig[1];
		max_average_sig_antenna_i = 1;
		active_chains = (1 << max_average_sig_antenna_i);
	}

	if (average_sig[2] >= max_average_sig) {
		max_average_sig = average_sig[2];
		max_average_sig_antenna_i = 2;
		active_chains = (1 << max_average_sig_antenna_i);
	}

	IWL_DEBUG_CALIB(priv, "average_sig: a %d b %d c %d\n",
		     average_sig[0], average_sig[1], average_sig[2]);
	IWL_DEBUG_CALIB(priv, "max_average_sig = %d, antenna %d\n",
		     max_average_sig, max_average_sig_antenna_i);

	/* Compare signal strengths for all 3 receivers. */
	for (i = 0; i < NUM_RX_CHAINS; i++) {
		if (i != max_average_sig_antenna_i) {
			s32 rssi_delta = (max_average_sig - average_sig[i]);

			/* If signal is very weak, compared with
			 * strongest, mark it as disconnected. */
			if (rssi_delta > MAXIMUM_ALLOWED_PATHLOSS)
				data->disconn_array[i] = 1;
			else
				active_chains |= (1 << i);
			IWL_DEBUG_CALIB(priv, "i = %d  rssiDelta = %d  "
			     "disconn_array[i] = %d\n",
			     i, rssi_delta, data->disconn_array[i]);
		}
	}

	/*
	 * The above algorithm sometimes fails when the ucode
	 * reports 0 for all chains. It's not clear why that
	 * happens to start with, but it is then causing trouble
	 * because this can make us enable more chains than the
	 * hardware really has.
	 *
	 * To be safe, simply mask out any chains that we know
	 * are not on the device.
	 */
	active_chains &= priv->nvm_data->valid_rx_ant;

	num_tx_chains = 0;
	for (i = 0; i < NUM_RX_CHAINS; i++) {
		/* loops on all the bits of
		 * priv->hw_setting.valid_tx_ant */
		u8 ant_msk = (1 << i);
		if (!(priv->nvm_data->valid_tx_ant & ant_msk))
			continue;

		num_tx_chains++;
		if (data->disconn_array[i] == 0)
			/* there is a Tx antenna connected */
			break;
		if (num_tx_chains == priv->hw_params.tx_chains_num &&
		    data->disconn_array[i]) {
			/*
			 * If all chains are disconnected
			 * connect the first valid tx chain
			 */
			first_chain =
				find_first_chain(priv->nvm_data->valid_tx_ant);
			data->disconn_array[first_chain] = 0;
			active_chains |= BIT(first_chain);
			IWL_DEBUG_CALIB(priv,
					"All Tx chains are disconnected W/A - declare %d as connected\n",
					first_chain);
			break;
		}
	}

	if (active_chains != priv->nvm_data->valid_rx_ant &&
	    active_chains != priv->chain_noise_data.active_chains)
		IWL_DEBUG_CALIB(priv,
				"Detected that not all antennas are connected! "
				"Connected: %#x, valid: %#x.\n",
				active_chains,
				priv->nvm_data->valid_rx_ant);

	/* Save for use within RXON, TX, SCAN commands, etc. */
	data->active_chains = active_chains;
	IWL_DEBUG_CALIB(priv, "active_chains (bitwise) = 0x%x\n",
			active_chains);
}

static void iwlagn_gain_computation(struct iwl_priv *priv,
				    u32 average_noise[NUM_RX_CHAINS],
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
			min(abs(delta_g),
			(long) CHAIN_NOISE_MAX_DELTA_GAIN_CODE);

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
			priv->phy_calib_chain_noise_gain_cmd);
		cmd.delta_gain_1 = data->delta_gain_code[1];
		cmd.delta_gain_2 = data->delta_gain_code[2];
		iwl_dvm_send_cmd_pdu(priv, REPLY_PHY_CALIBRATION_CMD,
			CMD_ASYNC, sizeof(cmd), &cmd);

		data->radio_write = 1;
		data->state = IWL_CHAIN_NOISE_CALIBRATED;
	}
}

/*
 * Accumulate 16 beacons of signal and noise statistics for each of
 *   3 receivers/antennas/rx-chains, then figure out:
 * 1)  Which antennas are connected.
 * 2)  Differential rx gain settings to balance the 3 receivers.
 */
void iwl_chain_noise_calibration(struct iwl_priv *priv)
{
	struct iwl_chain_noise_data *data = NULL;

	u32 chain_noise_a;
	u32 chain_noise_b;
	u32 chain_noise_c;
	u32 chain_sig_a;
	u32 chain_sig_b;
	u32 chain_sig_c;
	u32 average_sig[NUM_RX_CHAINS] = {INITIALIZATION_VALUE};
	u32 average_noise[NUM_RX_CHAINS] = {INITIALIZATION_VALUE};
	u32 min_average_noise = MIN_AVERAGE_NOISE_MAX_VALUE;
	u16 min_average_noise_antenna_i = INITIALIZATION_VALUE;
	u16 i = 0;
	u16 rxon_chnum = INITIALIZATION_VALUE;
	u16 stat_chnum = INITIALIZATION_VALUE;
	u8 rxon_band24;
	u8 stat_band24;
	struct statistics_rx_non_phy *rx_info;

	/*
	 * MULTI-FIXME:
	 * When we support multiple interfaces on different channels,
	 * this must be modified/fixed.
	 */
	struct iwl_rxon_context *ctx = &priv->contexts[IWL_RXON_CTX_BSS];

	if (priv->calib_disabled & IWL_CHAIN_NOISE_CALIB_DISABLED)
		return;

	data = &(priv->chain_noise_data);

	/*
	 * Accumulate just the first "chain_noise_num_beacons" after
	 * the first association, then we're done forever.
	 */
	if (data->state != IWL_CHAIN_NOISE_ACCUMULATE) {
		if (data->state == IWL_CHAIN_NOISE_ALIVE)
			IWL_DEBUG_CALIB(priv, "Wait for noise calib reset\n");
		return;
	}

	spin_lock_bh(&priv->statistics.lock);

	rx_info = &priv->statistics.rx_non_phy;

	if (rx_info->interference_data_flag != INTERFERENCE_DATA_AVAILABLE) {
		IWL_DEBUG_CALIB(priv, " << Interference data unavailable\n");
		spin_unlock_bh(&priv->statistics.lock);
		return;
	}

	rxon_band24 = !!(ctx->staging.flags & RXON_FLG_BAND_24G_MSK);
	rxon_chnum = le16_to_cpu(ctx->staging.channel);
	stat_band24 =
		!!(priv->statistics.flag & STATISTICS_REPLY_FLG_BAND_24G_MSK);
	stat_chnum = le32_to_cpu(priv->statistics.flag) >> 16;

	/* Make sure we accumulate data for just the associated channel
	 *   (even if scanning). */
	if ((rxon_chnum != stat_chnum) || (rxon_band24 != stat_band24)) {
		IWL_DEBUG_CALIB(priv, "Stats not from chan=%d, band24=%d\n",
				rxon_chnum, rxon_band24);
		spin_unlock_bh(&priv->statistics.lock);
		return;
	}

	/*
	 *  Accumulate beacon statistics values across
	 * "chain_noise_num_beacons"
	 */
	chain_noise_a = le32_to_cpu(rx_info->beacon_silence_rssi_a) &
				IN_BAND_FILTER;
	chain_noise_b = le32_to_cpu(rx_info->beacon_silence_rssi_b) &
				IN_BAND_FILTER;
	chain_noise_c = le32_to_cpu(rx_info->beacon_silence_rssi_c) &
				IN_BAND_FILTER;

	chain_sig_a = le32_to_cpu(rx_info->beacon_rssi_a) & IN_BAND_FILTER;
	chain_sig_b = le32_to_cpu(rx_info->beacon_rssi_b) & IN_BAND_FILTER;
	chain_sig_c = le32_to_cpu(rx_info->beacon_rssi_c) & IN_BAND_FILTER;

	spin_unlock_bh(&priv->statistics.lock);

	data->beacon_count++;

	data->chain_noise_a = (chain_noise_a + data->chain_noise_a);
	data->chain_noise_b = (chain_noise_b + data->chain_noise_b);
	data->chain_noise_c = (chain_noise_c + data->chain_noise_c);

	data->chain_signal_a = (chain_sig_a + data->chain_signal_a);
	data->chain_signal_b = (chain_sig_b + data->chain_signal_b);
	data->chain_signal_c = (chain_sig_c + data->chain_signal_c);

	IWL_DEBUG_CALIB(priv, "chan=%d, band24=%d, beacon=%d\n",
			rxon_chnum, rxon_band24, data->beacon_count);
	IWL_DEBUG_CALIB(priv, "chain_sig: a %d b %d c %d\n",
			chain_sig_a, chain_sig_b, chain_sig_c);
	IWL_DEBUG_CALIB(priv, "chain_noise: a %d b %d c %d\n",
			chain_noise_a, chain_noise_b, chain_noise_c);

	/* If this is the "chain_noise_num_beacons", determine:
	 * 1)  Disconnected antennas (using signal strengths)
	 * 2)  Differential gain (using silence noise) to balance receivers */
	if (data->beacon_count != IWL_CAL_NUM_BEACONS)
		return;

	/* Analyze signal for disconnected antenna */
	if (priv->cfg->bt_params &&
	    priv->cfg->bt_params->advanced_bt_coexist) {
		/* Disable disconnected antenna algorithm for advanced
		   bt coex, assuming valid antennas are connected */
		data->active_chains = priv->nvm_data->valid_rx_ant;
		for (i = 0; i < NUM_RX_CHAINS; i++)
			if (!(data->active_chains & (1<<i)))
				data->disconn_array[i] = 1;
	} else
		iwl_find_disconn_antenna(priv, average_sig, data);

	/* Analyze noise for rx balance */
	average_noise[0] = data->chain_noise_a / IWL_CAL_NUM_BEACONS;
	average_noise[1] = data->chain_noise_b / IWL_CAL_NUM_BEACONS;
	average_noise[2] = data->chain_noise_c / IWL_CAL_NUM_BEACONS;

	for (i = 0; i < NUM_RX_CHAINS; i++) {
		if (!(data->disconn_array[i]) &&
		   (average_noise[i] <= min_average_noise)) {
			/* This means that chain i is active and has
			 * lower noise values so far: */
			min_average_noise = average_noise[i];
			min_average_noise_antenna_i = i;
		}
	}

	IWL_DEBUG_CALIB(priv, "average_noise: a %d b %d c %d\n",
			average_noise[0], average_noise[1],
			average_noise[2]);

	IWL_DEBUG_CALIB(priv, "min_average_noise = %d, antenna %d\n",
			min_average_noise, min_average_noise_antenna_i);

	iwlagn_gain_computation(
		priv, average_noise,
		find_first_chain(priv->nvm_data->valid_rx_ant));

	/* Some power changes may have been made during the calibration.
	 * Update and commit the RXON
	 */
	iwl_update_chain_flags(priv);

	data->state = IWL_CHAIN_NOISE_DONE;
	iwl_power_update_mode(priv, false);
}

void iwl_reset_run_time_calib(struct iwl_priv *priv)
{
	int i;
	memset(&(priv->sensitivity_data), 0,
	       sizeof(struct iwl_sensitivity_data));
	memset(&(priv->chain_noise_data), 0,
	       sizeof(struct iwl_chain_noise_data));
	for (i = 0; i < NUM_RX_CHAINS; i++)
		priv->chain_noise_data.delta_gain_code[i] =
				CHAIN_NOISE_DELTA_GAIN_INIT_VAL;

	/* Ask for statistics now, the uCode will send notification
	 * periodically after association */
	iwl_send_statistics_request(priv, CMD_ASYNC, true);
}
