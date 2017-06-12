/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2013 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2015 Intel Mobile Communications GmbH
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
 *  Intel Linux Wireless <linuxwifi@intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
 * Copyright(c) 2013 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2015 Intel Mobile Communications GmbH
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
 *
 *****************************************************************************/

#include <linux/ieee80211.h>
#include <linux/etherdevice.h>
#include <net/mac80211.h>

#include "fw-api-coex.h"
#include "iwl-modparams.h"
#include "mvm.h"
#include "iwl-debug.h"

/* 20MHz / 40MHz below / 40Mhz above*/
static const __le64 iwl_ci_mask[][3] = {
	/* dummy entry for channel 0 */
	{cpu_to_le64(0), cpu_to_le64(0), cpu_to_le64(0)},
	{
		cpu_to_le64(0x0000001FFFULL),
		cpu_to_le64(0x0ULL),
		cpu_to_le64(0x00007FFFFFULL),
	},
	{
		cpu_to_le64(0x000000FFFFULL),
		cpu_to_le64(0x0ULL),
		cpu_to_le64(0x0003FFFFFFULL),
	},
	{
		cpu_to_le64(0x000003FFFCULL),
		cpu_to_le64(0x0ULL),
		cpu_to_le64(0x000FFFFFFCULL),
	},
	{
		cpu_to_le64(0x00001FFFE0ULL),
		cpu_to_le64(0x0ULL),
		cpu_to_le64(0x007FFFFFE0ULL),
	},
	{
		cpu_to_le64(0x00007FFF80ULL),
		cpu_to_le64(0x00007FFFFFULL),
		cpu_to_le64(0x01FFFFFF80ULL),
	},
	{
		cpu_to_le64(0x0003FFFC00ULL),
		cpu_to_le64(0x0003FFFFFFULL),
		cpu_to_le64(0x0FFFFFFC00ULL),
	},
	{
		cpu_to_le64(0x000FFFF000ULL),
		cpu_to_le64(0x000FFFFFFCULL),
		cpu_to_le64(0x3FFFFFF000ULL),
	},
	{
		cpu_to_le64(0x007FFF8000ULL),
		cpu_to_le64(0x007FFFFFE0ULL),
		cpu_to_le64(0xFFFFFF8000ULL),
	},
	{
		cpu_to_le64(0x01FFFE0000ULL),
		cpu_to_le64(0x01FFFFFF80ULL),
		cpu_to_le64(0xFFFFFE0000ULL),
	},
	{
		cpu_to_le64(0x0FFFF00000ULL),
		cpu_to_le64(0x0FFFFFFC00ULL),
		cpu_to_le64(0x0ULL),
	},
	{
		cpu_to_le64(0x3FFFC00000ULL),
		cpu_to_le64(0x3FFFFFF000ULL),
		cpu_to_le64(0x0)
	},
	{
		cpu_to_le64(0xFFFE000000ULL),
		cpu_to_le64(0xFFFFFF8000ULL),
		cpu_to_le64(0x0)
	},
	{
		cpu_to_le64(0xFFF8000000ULL),
		cpu_to_le64(0xFFFFFE0000ULL),
		cpu_to_le64(0x0)
	},
	{
		cpu_to_le64(0xFE00000000ULL),
		cpu_to_le64(0x0ULL),
		cpu_to_le64(0x0ULL)
	},
};

struct corunning_block_luts {
	u8 range;
	__le32 lut20[BT_COEX_CORUN_LUT_SIZE];
};

/*
 * Ranges for the antenna coupling calibration / co-running block LUT:
 *		LUT0: [ 0, 12[
 *		LUT1: [12, 20[
 *		LUT2: [20, 21[
 *		LUT3: [21, 23[
 *		LUT4: [23, 27[
 *		LUT5: [27, 30[
 *		LUT6: [30, 32[
 *		LUT7: [32, 33[
 *		LUT8: [33, - [
 */
static const struct corunning_block_luts antenna_coupling_ranges[] = {
	{
		.range = 0,
		.lut20 = {
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
		},
	},
	{
		.range = 12,
		.lut20 = {
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
		},
	},
	{
		.range = 20,
		.lut20 = {
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
		},
	},
	{
		.range = 21,
		.lut20 = {
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
		},
	},
	{
		.range = 23,
		.lut20 = {
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
		},
	},
	{
		.range = 27,
		.lut20 = {
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
		},
	},
	{
		.range = 30,
		.lut20 = {
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
		},
	},
	{
		.range = 32,
		.lut20 = {
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
		},
	},
	{
		.range = 33,
		.lut20 = {
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
			cpu_to_le32(0x00000000),  cpu_to_le32(0x00000000),
		},
	},
};

static enum iwl_bt_coex_lut_type
iwl_get_coex_type(struct iwl_mvm *mvm, const struct ieee80211_vif *vif)
{
	struct ieee80211_chanctx_conf *chanctx_conf;
	enum iwl_bt_coex_lut_type ret;
	u16 phy_ctx_id;
	u32 primary_ch_phy_id, secondary_ch_phy_id;

	/*
	 * Checking that we hold mvm->mutex is a good idea, but the rate
	 * control can't acquire the mutex since it runs in Tx path.
	 * So this is racy in that case, but in the worst case, the AMPDU
	 * size limit will be wrong for a short time which is not a big
	 * issue.
	 */

	rcu_read_lock();

	chanctx_conf = rcu_dereference(vif->chanctx_conf);

	if (!chanctx_conf ||
	     chanctx_conf->def.chan->band != NL80211_BAND_2GHZ) {
		rcu_read_unlock();
		return BT_COEX_INVALID_LUT;
	}

	ret = BT_COEX_TX_DIS_LUT;

	if (mvm->cfg->bt_shared_single_ant) {
		rcu_read_unlock();
		return ret;
	}

	phy_ctx_id = *((u16 *)chanctx_conf->drv_priv);
	primary_ch_phy_id = le32_to_cpu(mvm->last_bt_ci_cmd.primary_ch_phy_id);
	secondary_ch_phy_id =
		le32_to_cpu(mvm->last_bt_ci_cmd.secondary_ch_phy_id);

	if (primary_ch_phy_id == phy_ctx_id)
		ret = le32_to_cpu(mvm->last_bt_notif.primary_ch_lut);
	else if (secondary_ch_phy_id == phy_ctx_id)
		ret = le32_to_cpu(mvm->last_bt_notif.secondary_ch_lut);
	/* else - default = TX TX disallowed */

	rcu_read_unlock();

	return ret;
}

int iwl_send_bt_init_conf(struct iwl_mvm *mvm)
{
	struct iwl_bt_coex_cmd bt_cmd = {};
	u32 mode;

	lockdep_assert_held(&mvm->mutex);

	if (unlikely(mvm->bt_force_ant_mode != BT_FORCE_ANT_DIS)) {
		switch (mvm->bt_force_ant_mode) {
		case BT_FORCE_ANT_BT:
			mode = BT_COEX_BT;
			break;
		case BT_FORCE_ANT_WIFI:
			mode = BT_COEX_WIFI;
			break;
		default:
			WARN_ON(1);
			mode = 0;
		}

		bt_cmd.mode = cpu_to_le32(mode);
		goto send_cmd;
	}

	mode = iwlwifi_mod_params.bt_coex_active ? BT_COEX_NW : BT_COEX_DISABLE;
	bt_cmd.mode = cpu_to_le32(mode);

	if (IWL_MVM_BT_COEX_SYNC2SCO)
		bt_cmd.enabled_modules |=
			cpu_to_le32(BT_COEX_SYNC2SCO_ENABLED);

	if (iwl_mvm_bt_is_plcr_supported(mvm))
		bt_cmd.enabled_modules |= cpu_to_le32(BT_COEX_CORUN_ENABLED);

	if (iwl_mvm_is_mplut_supported(mvm))
		bt_cmd.enabled_modules |= cpu_to_le32(BT_COEX_MPLUT_ENABLED);

	bt_cmd.enabled_modules |= cpu_to_le32(BT_COEX_HIGH_BAND_RET);

send_cmd:
	memset(&mvm->last_bt_notif, 0, sizeof(mvm->last_bt_notif));
	memset(&mvm->last_bt_ci_cmd, 0, sizeof(mvm->last_bt_ci_cmd));

	return iwl_mvm_send_cmd_pdu(mvm, BT_CONFIG, 0, sizeof(bt_cmd), &bt_cmd);
}

static int iwl_mvm_bt_coex_reduced_txp(struct iwl_mvm *mvm, u8 sta_id,
				       bool enable)
{
	struct iwl_bt_coex_reduced_txp_update_cmd cmd = {};
	struct iwl_mvm_sta *mvmsta;
	u32 value;
	int ret;

	mvmsta = iwl_mvm_sta_from_staid_protected(mvm, sta_id);
	if (!mvmsta)
		return 0;

	/* nothing to do */
	if (mvmsta->bt_reduced_txpower == enable)
		return 0;

	value = mvmsta->sta_id;

	if (enable)
		value |= BT_REDUCED_TX_POWER_BIT;

	IWL_DEBUG_COEX(mvm, "%sable reduced Tx Power for sta %d\n",
		       enable ? "en" : "dis", sta_id);

	cmd.reduced_txp = cpu_to_le32(value);
	mvmsta->bt_reduced_txpower = enable;

	ret = iwl_mvm_send_cmd_pdu(mvm, BT_COEX_UPDATE_REDUCED_TXP, CMD_ASYNC,
				   sizeof(cmd), &cmd);

	return ret;
}

struct iwl_bt_iterator_data {
	struct iwl_bt_coex_profile_notif *notif;
	struct iwl_mvm *mvm;
	struct ieee80211_chanctx_conf *primary;
	struct ieee80211_chanctx_conf *secondary;
	bool primary_ll;
};

static inline
void iwl_mvm_bt_coex_enable_rssi_event(struct iwl_mvm *mvm,
				       struct ieee80211_vif *vif,
				       bool enable, int rssi)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);

	mvmvif->bf_data.last_bt_coex_event = rssi;
	mvmvif->bf_data.bt_coex_max_thold =
		enable ? -IWL_MVM_BT_COEX_EN_RED_TXP_THRESH : 0;
	mvmvif->bf_data.bt_coex_min_thold =
		enable ? -IWL_MVM_BT_COEX_DIS_RED_TXP_THRESH : 0;
}

/* must be called under rcu_read_lock */
static void iwl_mvm_bt_notif_iterator(void *_data, u8 *mac,
				      struct ieee80211_vif *vif)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_bt_iterator_data *data = _data;
	struct iwl_mvm *mvm = data->mvm;
	struct ieee80211_chanctx_conf *chanctx_conf;
	/* default smps_mode is AUTOMATIC - only used for client modes */
	enum ieee80211_smps_mode smps_mode = IEEE80211_SMPS_AUTOMATIC;
	u32 bt_activity_grading;
	int ave_rssi;

	lockdep_assert_held(&mvm->mutex);

	switch (vif->type) {
	case NL80211_IFTYPE_STATION:
		break;
	case NL80211_IFTYPE_AP:
		if (!mvmvif->ap_ibss_active)
			return;
		break;
	default:
		return;
	}

	chanctx_conf = rcu_dereference(vif->chanctx_conf);

	/* If channel context is invalid or not on 2.4GHz .. */
	if ((!chanctx_conf ||
	     chanctx_conf->def.chan->band != NL80211_BAND_2GHZ)) {
		if (vif->type == NL80211_IFTYPE_STATION) {
			/* ... relax constraints and disable rssi events */
			iwl_mvm_update_smps(mvm, vif, IWL_MVM_SMPS_REQ_BT_COEX,
					    smps_mode);
			iwl_mvm_bt_coex_reduced_txp(mvm, mvmvif->ap_sta_id,
						    false);
			iwl_mvm_bt_coex_enable_rssi_event(mvm, vif, false, 0);
		}
		return;
	}

	bt_activity_grading = le32_to_cpu(data->notif->bt_activity_grading);
	if (bt_activity_grading >= BT_HIGH_TRAFFIC)
		smps_mode = IEEE80211_SMPS_STATIC;
	else if (bt_activity_grading >= BT_LOW_TRAFFIC)
		smps_mode = IEEE80211_SMPS_DYNAMIC;

	/* relax SMPS constraints for next association */
	if (!vif->bss_conf.assoc)
		smps_mode = IEEE80211_SMPS_AUTOMATIC;

	if (mvmvif->phy_ctxt &&
	    IWL_COEX_IS_RRC_ON(mvm->last_bt_notif.ttc_rrc_status,
			       mvmvif->phy_ctxt->id))
		smps_mode = IEEE80211_SMPS_AUTOMATIC;

	IWL_DEBUG_COEX(data->mvm,
		       "mac %d: bt_activity_grading %d smps_req %d\n",
		       mvmvif->id, bt_activity_grading, smps_mode);

	if (vif->type == NL80211_IFTYPE_STATION)
		iwl_mvm_update_smps(mvm, vif, IWL_MVM_SMPS_REQ_BT_COEX,
				    smps_mode);

	/* low latency is always primary */
	if (iwl_mvm_vif_low_latency(mvmvif)) {
		data->primary_ll = true;

		data->secondary = data->primary;
		data->primary = chanctx_conf;
	}

	if (vif->type == NL80211_IFTYPE_AP) {
		if (!mvmvif->ap_ibss_active)
			return;

		if (chanctx_conf == data->primary)
			return;

		if (!data->primary_ll) {
			/*
			 * downgrade the current primary no matter what its
			 * type is.
			 */
			data->secondary = data->primary;
			data->primary = chanctx_conf;
		} else {
			/* there is low latency vif - we will be secondary */
			data->secondary = chanctx_conf;
		}
		return;
	}

	/*
	 * STA / P2P Client, try to be primary if first vif. If we are in low
	 * latency mode, we are already in primary and just don't do much
	 */
	if (!data->primary || data->primary == chanctx_conf)
		data->primary = chanctx_conf;
	else if (!data->secondary)
		/* if secondary is not NULL, it might be a GO */
		data->secondary = chanctx_conf;

	/*
	 * don't reduce the Tx power if one of these is true:
	 *  we are in LOOSE
	 *  single share antenna product
	 *  BT is inactive
	 *  we are not associated
	 */
	if (iwl_get_coex_type(mvm, vif) == BT_COEX_LOOSE_LUT ||
	    mvm->cfg->bt_shared_single_ant || !vif->bss_conf.assoc ||
	    le32_to_cpu(mvm->last_bt_notif.bt_activity_grading) == BT_OFF) {
		iwl_mvm_bt_coex_reduced_txp(mvm, mvmvif->ap_sta_id, false);
		iwl_mvm_bt_coex_enable_rssi_event(mvm, vif, false, 0);
		return;
	}

	/* try to get the avg rssi from fw */
	ave_rssi = mvmvif->bf_data.ave_beacon_signal;

	/* if the RSSI isn't valid, fake it is very low */
	if (!ave_rssi)
		ave_rssi = -100;
	if (ave_rssi > -IWL_MVM_BT_COEX_EN_RED_TXP_THRESH) {
		if (iwl_mvm_bt_coex_reduced_txp(mvm, mvmvif->ap_sta_id, true))
			IWL_ERR(mvm, "Couldn't send BT_CONFIG cmd\n");
	} else if (ave_rssi < -IWL_MVM_BT_COEX_DIS_RED_TXP_THRESH) {
		if (iwl_mvm_bt_coex_reduced_txp(mvm, mvmvif->ap_sta_id, false))
			IWL_ERR(mvm, "Couldn't send BT_CONFIG cmd\n");
	}

	/* Begin to monitor the RSSI: it may influence the reduced Tx power */
	iwl_mvm_bt_coex_enable_rssi_event(mvm, vif, true, ave_rssi);
}

static void iwl_mvm_bt_coex_notif_handle(struct iwl_mvm *mvm)
{
	struct iwl_bt_iterator_data data = {
		.mvm = mvm,
		.notif = &mvm->last_bt_notif,
	};
	struct iwl_bt_coex_ci_cmd cmd = {};
	u8 ci_bw_idx;

	/* Ignore updates if we are in force mode */
	if (unlikely(mvm->bt_force_ant_mode != BT_FORCE_ANT_DIS))
		return;

	rcu_read_lock();
	ieee80211_iterate_active_interfaces_atomic(
					mvm->hw, IEEE80211_IFACE_ITER_NORMAL,
					iwl_mvm_bt_notif_iterator, &data);

	if (data.primary) {
		struct ieee80211_chanctx_conf *chan = data.primary;
		if (WARN_ON(!chan->def.chan)) {
			rcu_read_unlock();
			return;
		}

		if (chan->def.width < NL80211_CHAN_WIDTH_40) {
			ci_bw_idx = 0;
		} else {
			if (chan->def.center_freq1 >
			    chan->def.chan->center_freq)
				ci_bw_idx = 2;
			else
				ci_bw_idx = 1;
		}

		cmd.bt_primary_ci =
			iwl_ci_mask[chan->def.chan->hw_value][ci_bw_idx];
		cmd.primary_ch_phy_id =
			cpu_to_le32(*((u16 *)data.primary->drv_priv));
	}

	if (data.secondary) {
		struct ieee80211_chanctx_conf *chan = data.secondary;
		if (WARN_ON(!data.secondary->def.chan)) {
			rcu_read_unlock();
			return;
		}

		if (chan->def.width < NL80211_CHAN_WIDTH_40) {
			ci_bw_idx = 0;
		} else {
			if (chan->def.center_freq1 >
			    chan->def.chan->center_freq)
				ci_bw_idx = 2;
			else
				ci_bw_idx = 1;
		}

		cmd.bt_secondary_ci =
			iwl_ci_mask[chan->def.chan->hw_value][ci_bw_idx];
		cmd.secondary_ch_phy_id =
			cpu_to_le32(*((u16 *)data.secondary->drv_priv));
	}

	rcu_read_unlock();

	/* Don't spam the fw with the same command over and over */
	if (memcmp(&cmd, &mvm->last_bt_ci_cmd, sizeof(cmd))) {
		if (iwl_mvm_send_cmd_pdu(mvm, BT_COEX_CI, 0,
					 sizeof(cmd), &cmd))
			IWL_ERR(mvm, "Failed to send BT_CI cmd\n");
		memcpy(&mvm->last_bt_ci_cmd, &cmd, sizeof(cmd));
	}
}

void iwl_mvm_rx_bt_coex_notif(struct iwl_mvm *mvm,
			      struct iwl_rx_cmd_buffer *rxb)
{
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	struct iwl_bt_coex_profile_notif *notif = (void *)pkt->data;

	IWL_DEBUG_COEX(mvm, "BT Coex Notification received\n");
	IWL_DEBUG_COEX(mvm, "\tBT ci compliance %d\n", notif->bt_ci_compliance);
	IWL_DEBUG_COEX(mvm, "\tBT primary_ch_lut %d\n",
		       le32_to_cpu(notif->primary_ch_lut));
	IWL_DEBUG_COEX(mvm, "\tBT secondary_ch_lut %d\n",
		       le32_to_cpu(notif->secondary_ch_lut));
	IWL_DEBUG_COEX(mvm, "\tBT activity grading %d\n",
		       le32_to_cpu(notif->bt_activity_grading));

	/* remember this notification for future use: rssi fluctuations */
	memcpy(&mvm->last_bt_notif, notif, sizeof(mvm->last_bt_notif));

	iwl_mvm_bt_coex_notif_handle(mvm);
}

void iwl_mvm_bt_rssi_event(struct iwl_mvm *mvm, struct ieee80211_vif *vif,
			   enum ieee80211_rssi_event_data rssi_event)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	int ret;

	lockdep_assert_held(&mvm->mutex);

	/* Ignore updates if we are in force mode */
	if (unlikely(mvm->bt_force_ant_mode != BT_FORCE_ANT_DIS))
		return;

	/*
	 * Rssi update while not associated - can happen since the statistics
	 * are handled asynchronously
	 */
	if (mvmvif->ap_sta_id == IWL_MVM_INVALID_STA)
		return;

	/* No BT - reports should be disabled */
	if (le32_to_cpu(mvm->last_bt_notif.bt_activity_grading) == BT_OFF)
		return;

	IWL_DEBUG_COEX(mvm, "RSSI for %pM is now %s\n", vif->bss_conf.bssid,
		       rssi_event == RSSI_EVENT_HIGH ? "HIGH" : "LOW");

	/*
	 * Check if rssi is good enough for reduced Tx power, but not in loose
	 * scheme.
	 */
	if (rssi_event == RSSI_EVENT_LOW || mvm->cfg->bt_shared_single_ant ||
	    iwl_get_coex_type(mvm, vif) == BT_COEX_LOOSE_LUT)
		ret = iwl_mvm_bt_coex_reduced_txp(mvm, mvmvif->ap_sta_id,
						  false);
	else
		ret = iwl_mvm_bt_coex_reduced_txp(mvm, mvmvif->ap_sta_id, true);

	if (ret)
		IWL_ERR(mvm, "couldn't send BT_CONFIG HCMD upon RSSI event\n");
}

#define LINK_QUAL_AGG_TIME_LIMIT_DEF	(4000)
#define LINK_QUAL_AGG_TIME_LIMIT_BT_ACT	(1200)

u16 iwl_mvm_coex_agg_time_limit(struct iwl_mvm *mvm,
				struct ieee80211_sta *sta)
{
	struct iwl_mvm_sta *mvmsta = iwl_mvm_sta_from_mac80211(sta);
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(mvmsta->vif);
	struct iwl_mvm_phy_ctxt *phy_ctxt = mvmvif->phy_ctxt;
	enum iwl_bt_coex_lut_type lut_type;

	if (IWL_COEX_IS_TTC_ON(mvm->last_bt_notif.ttc_rrc_status, phy_ctxt->id))
		return LINK_QUAL_AGG_TIME_LIMIT_DEF;

	if (le32_to_cpu(mvm->last_bt_notif.bt_activity_grading) <
	    BT_HIGH_TRAFFIC)
		return LINK_QUAL_AGG_TIME_LIMIT_DEF;

	lut_type = iwl_get_coex_type(mvm, mvmsta->vif);

	if (lut_type == BT_COEX_LOOSE_LUT || lut_type == BT_COEX_INVALID_LUT)
		return LINK_QUAL_AGG_TIME_LIMIT_DEF;

	/* tight coex, high bt traffic, reduce AGG time limit */
	return LINK_QUAL_AGG_TIME_LIMIT_BT_ACT;
}

bool iwl_mvm_bt_coex_is_mimo_allowed(struct iwl_mvm *mvm,
				     struct ieee80211_sta *sta)
{
	struct iwl_mvm_sta *mvmsta = iwl_mvm_sta_from_mac80211(sta);
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(mvmsta->vif);
	struct iwl_mvm_phy_ctxt *phy_ctxt = mvmvif->phy_ctxt;
	enum iwl_bt_coex_lut_type lut_type;

	if (IWL_COEX_IS_TTC_ON(mvm->last_bt_notif.ttc_rrc_status, phy_ctxt->id))
		return true;

	if (le32_to_cpu(mvm->last_bt_notif.bt_activity_grading) <
	    BT_HIGH_TRAFFIC)
		return true;

	/*
	 * In Tight / TxTxDis, BT can't Rx while we Tx, so use both antennas
	 * since BT is already killed.
	 * In Loose, BT can Rx while we Tx, so forbid MIMO to let BT Rx while
	 * we Tx.
	 * When we are in 5GHz, we'll get BT_COEX_INVALID_LUT allowing MIMO.
	 */
	lut_type = iwl_get_coex_type(mvm, mvmsta->vif);
	return lut_type != BT_COEX_LOOSE_LUT;
}

bool iwl_mvm_bt_coex_is_ant_avail(struct iwl_mvm *mvm, u8 ant)
{
	/* there is no other antenna, shared antenna is always available */
	if (mvm->cfg->bt_shared_single_ant)
		return true;

	if (ant & mvm->cfg->non_shared_ant)
		return true;

	return le32_to_cpu(mvm->last_bt_notif.bt_activity_grading) <
		BT_HIGH_TRAFFIC;
}

bool iwl_mvm_bt_coex_is_shared_ant_avail(struct iwl_mvm *mvm)
{
	/* there is no other antenna, shared antenna is always available */
	if (mvm->cfg->bt_shared_single_ant)
		return true;

	return le32_to_cpu(mvm->last_bt_notif.bt_activity_grading) < BT_HIGH_TRAFFIC;
}

bool iwl_mvm_bt_coex_is_tpc_allowed(struct iwl_mvm *mvm,
				    enum nl80211_band band)
{
	u32 bt_activity = le32_to_cpu(mvm->last_bt_notif.bt_activity_grading);

	if (band != NL80211_BAND_2GHZ)
		return false;

	return bt_activity >= BT_LOW_TRAFFIC;
}

u8 iwl_mvm_bt_coex_tx_prio(struct iwl_mvm *mvm, struct ieee80211_hdr *hdr,
			   struct ieee80211_tx_info *info, u8 ac)
{
	__le16 fc = hdr->frame_control;
	bool mplut_enabled = iwl_mvm_is_mplut_supported(mvm);

	if (info->band != NL80211_BAND_2GHZ)
		return 0;

	if (unlikely(mvm->bt_tx_prio))
		return mvm->bt_tx_prio - 1;

	if (likely(ieee80211_is_data(fc))) {
		if (likely(ieee80211_is_data_qos(fc))) {
			switch (ac) {
			case IEEE80211_AC_BE:
				return mplut_enabled ? 1 : 0;
			case IEEE80211_AC_VI:
				return mplut_enabled ? 2 : 3;
			case IEEE80211_AC_VO:
				return 3;
			default:
				return 0;
			}
		} else if (is_multicast_ether_addr(hdr->addr1)) {
			return 3;
		} else
			return 0;
	} else if (ieee80211_is_mgmt(fc)) {
		return ieee80211_is_disassoc(fc) ? 0 : 3;
	} else if (ieee80211_is_ctl(fc)) {
		/* ignore cfend and cfendack frames as we never send those */
		return 3;
	}

	return 0;
}

void iwl_mvm_bt_coex_vif_change(struct iwl_mvm *mvm)
{
	iwl_mvm_bt_coex_notif_handle(mvm);
}

void iwl_mvm_rx_ant_coupling_notif(struct iwl_mvm *mvm,
				   struct iwl_rx_cmd_buffer *rxb)
{
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	struct iwl_mvm_antenna_coupling_notif *notif = (void *)pkt->data;
	u32 ant_isolation = le32_to_cpu(notif->isolation);
	struct iwl_bt_coex_corun_lut_update_cmd cmd = {};
	u8 __maybe_unused lower_bound, upper_bound;
	u8 lut;

	if (!iwl_mvm_bt_is_plcr_supported(mvm))
		return;

	lockdep_assert_held(&mvm->mutex);

	/* Ignore updates if we are in force mode */
	if (unlikely(mvm->bt_force_ant_mode != BT_FORCE_ANT_DIS))
		return;

	if (ant_isolation ==  mvm->last_ant_isol)
		return;

	for (lut = 0; lut < ARRAY_SIZE(antenna_coupling_ranges) - 1; lut++)
		if (ant_isolation < antenna_coupling_ranges[lut + 1].range)
			break;

	lower_bound = antenna_coupling_ranges[lut].range;

	if (lut < ARRAY_SIZE(antenna_coupling_ranges) - 1)
		upper_bound = antenna_coupling_ranges[lut + 1].range;
	else
		upper_bound = antenna_coupling_ranges[lut].range;

	IWL_DEBUG_COEX(mvm, "Antenna isolation=%d in range [%d,%d[, lut=%d\n",
		       ant_isolation, lower_bound, upper_bound, lut);

	mvm->last_ant_isol = ant_isolation;

	if (mvm->last_corun_lut == lut)
		return;

	mvm->last_corun_lut = lut;

	/* For the moment, use the same LUT for 20GHz and 40GHz */
	memcpy(&cmd.corun_lut20, antenna_coupling_ranges[lut].lut20,
	       sizeof(cmd.corun_lut20));

	memcpy(&cmd.corun_lut40, antenna_coupling_ranges[lut].lut20,
	       sizeof(cmd.corun_lut40));

	if (iwl_mvm_send_cmd_pdu(mvm, BT_COEX_UPDATE_CORUN_LUT, 0,
				 sizeof(cmd), &cmd))
		IWL_ERR(mvm,
			"failed to send BT_COEX_UPDATE_CORUN_LUT command\n");
}
