/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2012 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2014 Intel Mobile Communications GmbH
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
 * Copyright(c) 2012 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2014 Intel Mobile Communications GmbH
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
#include "mvm.h"
#include "debugfs.h"

static void iwl_dbgfs_update_pm(struct iwl_mvm *mvm,
				 struct ieee80211_vif *vif,
				 enum iwl_dbgfs_pm_mask param, int val)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_dbgfs_pm *dbgfs_pm = &mvmvif->dbgfs_pm;

	dbgfs_pm->mask |= param;

	switch (param) {
	case MVM_DEBUGFS_PM_KEEP_ALIVE: {
		int dtimper = vif->bss_conf.dtim_period ?: 1;
		int dtimper_msec = dtimper * vif->bss_conf.beacon_int;

		IWL_DEBUG_POWER(mvm, "debugfs: set keep_alive= %d sec\n", val);
		if (val * MSEC_PER_SEC < 3 * dtimper_msec)
			IWL_WARN(mvm,
				 "debugfs: keep alive period (%ld msec) is less than minimum required (%d msec)\n",
				 val * MSEC_PER_SEC, 3 * dtimper_msec);
		dbgfs_pm->keep_alive_seconds = val;
		break;
	}
	case MVM_DEBUGFS_PM_SKIP_OVER_DTIM:
		IWL_DEBUG_POWER(mvm, "skip_over_dtim %s\n",
				val ? "enabled" : "disabled");
		dbgfs_pm->skip_over_dtim = val;
		break;
	case MVM_DEBUGFS_PM_SKIP_DTIM_PERIODS:
		IWL_DEBUG_POWER(mvm, "skip_dtim_periods=%d\n", val);
		dbgfs_pm->skip_dtim_periods = val;
		break;
	case MVM_DEBUGFS_PM_RX_DATA_TIMEOUT:
		IWL_DEBUG_POWER(mvm, "rx_data_timeout=%d\n", val);
		dbgfs_pm->rx_data_timeout = val;
		break;
	case MVM_DEBUGFS_PM_TX_DATA_TIMEOUT:
		IWL_DEBUG_POWER(mvm, "tx_data_timeout=%d\n", val);
		dbgfs_pm->tx_data_timeout = val;
		break;
	case MVM_DEBUGFS_PM_LPRX_ENA:
		IWL_DEBUG_POWER(mvm, "lprx %s\n", val ? "enabled" : "disabled");
		dbgfs_pm->lprx_ena = val;
		break;
	case MVM_DEBUGFS_PM_LPRX_RSSI_THRESHOLD:
		IWL_DEBUG_POWER(mvm, "lprx_rssi_threshold=%d\n", val);
		dbgfs_pm->lprx_rssi_threshold = val;
		break;
	case MVM_DEBUGFS_PM_SNOOZE_ENABLE:
		IWL_DEBUG_POWER(mvm, "snooze_enable=%d\n", val);
		dbgfs_pm->snooze_ena = val;
		break;
	case MVM_DEBUGFS_PM_UAPSD_MISBEHAVING:
		IWL_DEBUG_POWER(mvm, "uapsd_misbehaving_enable=%d\n", val);
		dbgfs_pm->uapsd_misbehaving = val;
		break;
	case MVM_DEBUGFS_PM_USE_PS_POLL:
		IWL_DEBUG_POWER(mvm, "use_ps_poll=%d\n", val);
		dbgfs_pm->use_ps_poll = val;
		break;
	}
}

static ssize_t iwl_dbgfs_pm_params_write(struct ieee80211_vif *vif, char *buf,
					 size_t count, loff_t *ppos)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_mvm *mvm = mvmvif->mvm;
	enum iwl_dbgfs_pm_mask param;
	int val, ret;

	if (!strncmp("keep_alive=", buf, 11)) {
		if (sscanf(buf + 11, "%d", &val) != 1)
			return -EINVAL;
		param = MVM_DEBUGFS_PM_KEEP_ALIVE;
	} else if (!strncmp("skip_over_dtim=", buf, 15)) {
		if (sscanf(buf + 15, "%d", &val) != 1)
			return -EINVAL;
		param = MVM_DEBUGFS_PM_SKIP_OVER_DTIM;
	} else if (!strncmp("skip_dtim_periods=", buf, 18)) {
		if (sscanf(buf + 18, "%d", &val) != 1)
			return -EINVAL;
		param = MVM_DEBUGFS_PM_SKIP_DTIM_PERIODS;
	} else if (!strncmp("rx_data_timeout=", buf, 16)) {
		if (sscanf(buf + 16, "%d", &val) != 1)
			return -EINVAL;
		param = MVM_DEBUGFS_PM_RX_DATA_TIMEOUT;
	} else if (!strncmp("tx_data_timeout=", buf, 16)) {
		if (sscanf(buf + 16, "%d", &val) != 1)
			return -EINVAL;
		param = MVM_DEBUGFS_PM_TX_DATA_TIMEOUT;
	} else if (!strncmp("lprx=", buf, 5)) {
		if (sscanf(buf + 5, "%d", &val) != 1)
			return -EINVAL;
		param = MVM_DEBUGFS_PM_LPRX_ENA;
	} else if (!strncmp("lprx_rssi_threshold=", buf, 20)) {
		if (sscanf(buf + 20, "%d", &val) != 1)
			return -EINVAL;
		if (val > POWER_LPRX_RSSI_THRESHOLD_MAX || val <
		    POWER_LPRX_RSSI_THRESHOLD_MIN)
			return -EINVAL;
		param = MVM_DEBUGFS_PM_LPRX_RSSI_THRESHOLD;
	} else if (!strncmp("snooze_enable=", buf, 14)) {
		if (sscanf(buf + 14, "%d", &val) != 1)
			return -EINVAL;
		param = MVM_DEBUGFS_PM_SNOOZE_ENABLE;
	} else if (!strncmp("uapsd_misbehaving=", buf, 18)) {
		if (sscanf(buf + 18, "%d", &val) != 1)
			return -EINVAL;
		param = MVM_DEBUGFS_PM_UAPSD_MISBEHAVING;
	} else if (!strncmp("use_ps_poll=", buf, 12)) {
		if (sscanf(buf + 12, "%d", &val) != 1)
			return -EINVAL;
		param = MVM_DEBUGFS_PM_USE_PS_POLL;
	} else {
		return -EINVAL;
	}

	mutex_lock(&mvm->mutex);
	iwl_dbgfs_update_pm(mvm, vif, param, val);
	ret = iwl_mvm_power_update_mac(mvm);
	mutex_unlock(&mvm->mutex);

	return ret ?: count;
}

static ssize_t iwl_dbgfs_pm_params_read(struct file *file,
					char __user *user_buf,
					size_t count, loff_t *ppos)
{
	struct ieee80211_vif *vif = file->private_data;
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_mvm *mvm = mvmvif->mvm;
	char buf[512];
	int bufsz = sizeof(buf);
	int pos;

	pos = iwl_mvm_power_mac_dbgfs_read(mvm, vif, buf, bufsz);

	return simple_read_from_buffer(user_buf, count, ppos, buf, pos);
}

static ssize_t iwl_dbgfs_mac_params_read(struct file *file,
					 char __user *user_buf,
					 size_t count, loff_t *ppos)
{
	struct ieee80211_vif *vif = file->private_data;
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_mvm *mvm = mvmvif->mvm;
	u8 ap_sta_id;
	struct ieee80211_chanctx_conf *chanctx_conf;
	char buf[512];
	int bufsz = sizeof(buf);
	int pos = 0;
	int i;

	mutex_lock(&mvm->mutex);

	ap_sta_id = mvmvif->ap_sta_id;

	switch (ieee80211_vif_type_p2p(vif)) {
	case NL80211_IFTYPE_ADHOC:
		pos += scnprintf(buf+pos, bufsz-pos, "type: ibss\n");
		break;
	case NL80211_IFTYPE_STATION:
		pos += scnprintf(buf+pos, bufsz-pos, "type: bss\n");
		break;
	case NL80211_IFTYPE_AP:
		pos += scnprintf(buf+pos, bufsz-pos, "type: ap\n");
		break;
	case NL80211_IFTYPE_P2P_CLIENT:
		pos += scnprintf(buf+pos, bufsz-pos, "type: p2p client\n");
		break;
	case NL80211_IFTYPE_P2P_GO:
		pos += scnprintf(buf+pos, bufsz-pos, "type: p2p go\n");
		break;
	case NL80211_IFTYPE_P2P_DEVICE:
		pos += scnprintf(buf+pos, bufsz-pos, "type: p2p dev\n");
		break;
	default:
		break;
	}

	pos += scnprintf(buf+pos, bufsz-pos, "mac id/color: %d / %d\n",
			 mvmvif->id, mvmvif->color);
	pos += scnprintf(buf+pos, bufsz-pos, "bssid: %pM\n",
			 vif->bss_conf.bssid);
	pos += scnprintf(buf+pos, bufsz-pos, "QoS:\n");
	for (i = 0; i < ARRAY_SIZE(mvmvif->queue_params); i++)
		pos += scnprintf(buf+pos, bufsz-pos,
				 "\t%d: txop:%d - cw_min:%d - cw_max = %d - aifs = %d upasd = %d\n",
				 i, mvmvif->queue_params[i].txop,
				 mvmvif->queue_params[i].cw_min,
				 mvmvif->queue_params[i].cw_max,
				 mvmvif->queue_params[i].aifs,
				 mvmvif->queue_params[i].uapsd);

	if (vif->type == NL80211_IFTYPE_STATION &&
	    ap_sta_id != IWL_MVM_STATION_COUNT) {
		struct ieee80211_sta *sta;

		sta = rcu_dereference_protected(mvm->fw_id_to_mac_id[ap_sta_id],
						lockdep_is_held(&mvm->mutex));
		if (!IS_ERR_OR_NULL(sta)) {
			struct iwl_mvm_sta *mvm_sta = (void *)sta->drv_priv;

			pos += scnprintf(buf+pos, bufsz-pos,
					 "ap_sta_id %d - reduced Tx power %d\n",
					 ap_sta_id,
					 mvm_sta->bt_reduced_txpower);
		}
	}

	rcu_read_lock();
	chanctx_conf = rcu_dereference(vif->chanctx_conf);
	if (chanctx_conf)
		pos += scnprintf(buf+pos, bufsz-pos,
				 "idle rx chains %d, active rx chains: %d\n",
				 chanctx_conf->rx_chains_static,
				 chanctx_conf->rx_chains_dynamic);
	rcu_read_unlock();

	mutex_unlock(&mvm->mutex);

	return simple_read_from_buffer(user_buf, count, ppos, buf, pos);
}

static void iwl_dbgfs_update_bf(struct ieee80211_vif *vif,
				enum iwl_dbgfs_bf_mask param, int value)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_dbgfs_bf *dbgfs_bf = &mvmvif->dbgfs_bf;

	dbgfs_bf->mask |= param;

	switch (param) {
	case MVM_DEBUGFS_BF_ENERGY_DELTA:
		dbgfs_bf->bf_energy_delta = value;
		break;
	case MVM_DEBUGFS_BF_ROAMING_ENERGY_DELTA:
		dbgfs_bf->bf_roaming_energy_delta = value;
		break;
	case MVM_DEBUGFS_BF_ROAMING_STATE:
		dbgfs_bf->bf_roaming_state = value;
		break;
	case MVM_DEBUGFS_BF_TEMP_THRESHOLD:
		dbgfs_bf->bf_temp_threshold = value;
		break;
	case MVM_DEBUGFS_BF_TEMP_FAST_FILTER:
		dbgfs_bf->bf_temp_fast_filter = value;
		break;
	case MVM_DEBUGFS_BF_TEMP_SLOW_FILTER:
		dbgfs_bf->bf_temp_slow_filter = value;
		break;
	case MVM_DEBUGFS_BF_ENABLE_BEACON_FILTER:
		dbgfs_bf->bf_enable_beacon_filter = value;
		break;
	case MVM_DEBUGFS_BF_DEBUG_FLAG:
		dbgfs_bf->bf_debug_flag = value;
		break;
	case MVM_DEBUGFS_BF_ESCAPE_TIMER:
		dbgfs_bf->bf_escape_timer = value;
		break;
	case MVM_DEBUGFS_BA_ENABLE_BEACON_ABORT:
		dbgfs_bf->ba_enable_beacon_abort = value;
		break;
	case MVM_DEBUGFS_BA_ESCAPE_TIMER:
		dbgfs_bf->ba_escape_timer = value;
		break;
	}
}

static ssize_t iwl_dbgfs_bf_params_write(struct ieee80211_vif *vif, char *buf,
					 size_t count, loff_t *ppos)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_mvm *mvm = mvmvif->mvm;
	enum iwl_dbgfs_bf_mask param;
	int value, ret = 0;

	if (!strncmp("bf_energy_delta=", buf, 16)) {
		if (sscanf(buf+16, "%d", &value) != 1)
			return -EINVAL;
		if (value < IWL_BF_ENERGY_DELTA_MIN ||
		    value > IWL_BF_ENERGY_DELTA_MAX)
			return -EINVAL;
		param = MVM_DEBUGFS_BF_ENERGY_DELTA;
	} else if (!strncmp("bf_roaming_energy_delta=", buf, 24)) {
		if (sscanf(buf+24, "%d", &value) != 1)
			return -EINVAL;
		if (value < IWL_BF_ROAMING_ENERGY_DELTA_MIN ||
		    value > IWL_BF_ROAMING_ENERGY_DELTA_MAX)
			return -EINVAL;
		param = MVM_DEBUGFS_BF_ROAMING_ENERGY_DELTA;
	} else if (!strncmp("bf_roaming_state=", buf, 17)) {
		if (sscanf(buf+17, "%d", &value) != 1)
			return -EINVAL;
		if (value < IWL_BF_ROAMING_STATE_MIN ||
		    value > IWL_BF_ROAMING_STATE_MAX)
			return -EINVAL;
		param = MVM_DEBUGFS_BF_ROAMING_STATE;
	} else if (!strncmp("bf_temp_threshold=", buf, 18)) {
		if (sscanf(buf+18, "%d", &value) != 1)
			return -EINVAL;
		if (value < IWL_BF_TEMP_THRESHOLD_MIN ||
		    value > IWL_BF_TEMP_THRESHOLD_MAX)
			return -EINVAL;
		param = MVM_DEBUGFS_BF_TEMP_THRESHOLD;
	} else if (!strncmp("bf_temp_fast_filter=", buf, 20)) {
		if (sscanf(buf+20, "%d", &value) != 1)
			return -EINVAL;
		if (value < IWL_BF_TEMP_FAST_FILTER_MIN ||
		    value > IWL_BF_TEMP_FAST_FILTER_MAX)
			return -EINVAL;
		param = MVM_DEBUGFS_BF_TEMP_FAST_FILTER;
	} else if (!strncmp("bf_temp_slow_filter=", buf, 20)) {
		if (sscanf(buf+20, "%d", &value) != 1)
			return -EINVAL;
		if (value < IWL_BF_TEMP_SLOW_FILTER_MIN ||
		    value > IWL_BF_TEMP_SLOW_FILTER_MAX)
			return -EINVAL;
		param = MVM_DEBUGFS_BF_TEMP_SLOW_FILTER;
	} else if (!strncmp("bf_enable_beacon_filter=", buf, 24)) {
		if (sscanf(buf+24, "%d", &value) != 1)
			return -EINVAL;
		if (value < 0 || value > 1)
			return -EINVAL;
		param = MVM_DEBUGFS_BF_ENABLE_BEACON_FILTER;
	} else if (!strncmp("bf_debug_flag=", buf, 14)) {
		if (sscanf(buf+14, "%d", &value) != 1)
			return -EINVAL;
		if (value < 0 || value > 1)
			return -EINVAL;
		param = MVM_DEBUGFS_BF_DEBUG_FLAG;
	} else if (!strncmp("bf_escape_timer=", buf, 16)) {
		if (sscanf(buf+16, "%d", &value) != 1)
			return -EINVAL;
		if (value < IWL_BF_ESCAPE_TIMER_MIN ||
		    value > IWL_BF_ESCAPE_TIMER_MAX)
			return -EINVAL;
		param = MVM_DEBUGFS_BF_ESCAPE_TIMER;
	} else if (!strncmp("ba_escape_timer=", buf, 16)) {
		if (sscanf(buf+16, "%d", &value) != 1)
			return -EINVAL;
		if (value < IWL_BA_ESCAPE_TIMER_MIN ||
		    value > IWL_BA_ESCAPE_TIMER_MAX)
			return -EINVAL;
		param = MVM_DEBUGFS_BA_ESCAPE_TIMER;
	} else if (!strncmp("ba_enable_beacon_abort=", buf, 23)) {
		if (sscanf(buf+23, "%d", &value) != 1)
			return -EINVAL;
		if (value < 0 || value > 1)
			return -EINVAL;
		param = MVM_DEBUGFS_BA_ENABLE_BEACON_ABORT;
	} else {
		return -EINVAL;
	}

	mutex_lock(&mvm->mutex);
	iwl_dbgfs_update_bf(vif, param, value);
	if (param == MVM_DEBUGFS_BF_ENABLE_BEACON_FILTER && !value)
		ret = iwl_mvm_disable_beacon_filter(mvm, vif, 0);
	else
		ret = iwl_mvm_enable_beacon_filter(mvm, vif, 0);
	mutex_unlock(&mvm->mutex);

	return ret ?: count;
}

static ssize_t iwl_dbgfs_bf_params_read(struct file *file,
					char __user *user_buf,
					size_t count, loff_t *ppos)
{
	struct ieee80211_vif *vif = file->private_data;
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	char buf[256];
	int pos = 0;
	const size_t bufsz = sizeof(buf);
	struct iwl_beacon_filter_cmd cmd = {
		IWL_BF_CMD_CONFIG_DEFAULTS,
		.bf_enable_beacon_filter =
			cpu_to_le32(IWL_BF_ENABLE_BEACON_FILTER_DEFAULT),
		.ba_enable_beacon_abort =
			cpu_to_le32(IWL_BA_ENABLE_BEACON_ABORT_DEFAULT),
	};

	iwl_mvm_beacon_filter_debugfs_parameters(vif, &cmd);
	if (mvmvif->bf_data.bf_enabled)
		cmd.bf_enable_beacon_filter = cpu_to_le32(1);
	else
		cmd.bf_enable_beacon_filter = 0;

	pos += scnprintf(buf+pos, bufsz-pos, "bf_energy_delta = %d\n",
			 le32_to_cpu(cmd.bf_energy_delta));
	pos += scnprintf(buf+pos, bufsz-pos, "bf_roaming_energy_delta = %d\n",
			 le32_to_cpu(cmd.bf_roaming_energy_delta));
	pos += scnprintf(buf+pos, bufsz-pos, "bf_roaming_state = %d\n",
			 le32_to_cpu(cmd.bf_roaming_state));
	pos += scnprintf(buf+pos, bufsz-pos, "bf_temp_threshold = %d\n",
			 le32_to_cpu(cmd.bf_temp_threshold));
	pos += scnprintf(buf+pos, bufsz-pos, "bf_temp_fast_filter = %d\n",
			 le32_to_cpu(cmd.bf_temp_fast_filter));
	pos += scnprintf(buf+pos, bufsz-pos, "bf_temp_slow_filter = %d\n",
			 le32_to_cpu(cmd.bf_temp_slow_filter));
	pos += scnprintf(buf+pos, bufsz-pos, "bf_enable_beacon_filter = %d\n",
			 le32_to_cpu(cmd.bf_enable_beacon_filter));
	pos += scnprintf(buf+pos, bufsz-pos, "bf_debug_flag = %d\n",
			 le32_to_cpu(cmd.bf_debug_flag));
	pos += scnprintf(buf+pos, bufsz-pos, "bf_escape_timer = %d\n",
			 le32_to_cpu(cmd.bf_escape_timer));
	pos += scnprintf(buf+pos, bufsz-pos, "ba_escape_timer = %d\n",
			 le32_to_cpu(cmd.ba_escape_timer));
	pos += scnprintf(buf+pos, bufsz-pos, "ba_enable_beacon_abort = %d\n",
			 le32_to_cpu(cmd.ba_enable_beacon_abort));

	return simple_read_from_buffer(user_buf, count, ppos, buf, pos);
}

static ssize_t iwl_dbgfs_low_latency_write(struct ieee80211_vif *vif, char *buf,
					   size_t count, loff_t *ppos)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_mvm *mvm = mvmvif->mvm;
	u8 value;
	int ret;

	ret = kstrtou8(buf, 0, &value);
	if (ret)
		return ret;
	if (value > 1)
		return -EINVAL;

	mutex_lock(&mvm->mutex);
	iwl_mvm_update_low_latency(mvm, vif, value);
	mutex_unlock(&mvm->mutex);

	return count;
}

static ssize_t iwl_dbgfs_low_latency_read(struct file *file,
					  char __user *user_buf,
					  size_t count, loff_t *ppos)
{
	struct ieee80211_vif *vif = file->private_data;
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	char buf[3];

	buf[0] = mvmvif->low_latency ? '1' : '0';
	buf[1] = '\n';
	buf[2] = '\0';
	return simple_read_from_buffer(user_buf, count, ppos, buf, sizeof(buf));
}

#define MVM_DEBUGFS_WRITE_FILE_OPS(name, bufsz) \
	_MVM_DEBUGFS_WRITE_FILE_OPS(name, bufsz, struct ieee80211_vif)
#define MVM_DEBUGFS_READ_WRITE_FILE_OPS(name, bufsz) \
	_MVM_DEBUGFS_READ_WRITE_FILE_OPS(name, bufsz, struct ieee80211_vif)
#define MVM_DEBUGFS_ADD_FILE_VIF(name, parent, mode) do {		\
		if (!debugfs_create_file(#name, mode, parent, vif,	\
					 &iwl_dbgfs_##name##_ops))	\
			goto err;					\
	} while (0)

MVM_DEBUGFS_READ_FILE_OPS(mac_params);
MVM_DEBUGFS_READ_WRITE_FILE_OPS(pm_params, 32);
MVM_DEBUGFS_READ_WRITE_FILE_OPS(bf_params, 256);
MVM_DEBUGFS_READ_WRITE_FILE_OPS(low_latency, 10);

void iwl_mvm_vif_dbgfs_register(struct iwl_mvm *mvm, struct ieee80211_vif *vif)
{
	struct dentry *dbgfs_dir = vif->debugfs_dir;
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	char buf[100];

	/*
	 * Check if debugfs directory already exist before creating it.
	 * This may happen when, for example, resetting hw or suspend-resume
	 */
	if (!dbgfs_dir || mvmvif->dbgfs_dir)
		return;

	mvmvif->dbgfs_dir = debugfs_create_dir("iwlmvm", dbgfs_dir);
	mvmvif->mvm = mvm;

	if (!mvmvif->dbgfs_dir) {
		IWL_ERR(mvm, "Failed to create debugfs directory under %s\n",
			dbgfs_dir->d_name.name);
		return;
	}

	if (iwlmvm_mod_params.power_scheme != IWL_POWER_SCHEME_CAM &&
	    ((vif->type == NL80211_IFTYPE_STATION && !vif->p2p) ||
	     (vif->type == NL80211_IFTYPE_STATION && vif->p2p &&
	      mvm->fw->ucode_capa.flags & IWL_UCODE_TLV_FLAGS_BSS_P2P_PS_DCM)))
		MVM_DEBUGFS_ADD_FILE_VIF(pm_params, mvmvif->dbgfs_dir, S_IWUSR |
					 S_IRUSR);

	MVM_DEBUGFS_ADD_FILE_VIF(mac_params, mvmvif->dbgfs_dir, S_IRUSR);
	MVM_DEBUGFS_ADD_FILE_VIF(low_latency, mvmvif->dbgfs_dir,
				 S_IRUSR | S_IWUSR);

	if (vif->type == NL80211_IFTYPE_STATION && !vif->p2p &&
	    mvmvif == mvm->bf_allowed_vif)
		MVM_DEBUGFS_ADD_FILE_VIF(bf_params, mvmvif->dbgfs_dir,
					 S_IRUSR | S_IWUSR);

	/*
	 * Create symlink for convenience pointing to interface specific
	 * debugfs entries for the driver. For example, under
	 * /sys/kernel/debug/iwlwifi/0000\:02\:00.0/iwlmvm/
	 * find
	 * netdev:wlan0 -> ../../../ieee80211/phy0/netdev:wlan0/iwlmvm/
	 */
	snprintf(buf, 100, "../../../%s/%s/%s/%s",
		 dbgfs_dir->d_parent->d_parent->d_name.name,
		 dbgfs_dir->d_parent->d_name.name,
		 dbgfs_dir->d_name.name,
		 mvmvif->dbgfs_dir->d_name.name);

	mvmvif->dbgfs_slink = debugfs_create_symlink(dbgfs_dir->d_name.name,
						     mvm->debugfs_dir, buf);
	if (!mvmvif->dbgfs_slink)
		IWL_ERR(mvm, "Can't create debugfs symbolic link under %s\n",
			dbgfs_dir->d_name.name);
	return;
err:
	IWL_ERR(mvm, "Can't create debugfs entity\n");
}

void iwl_mvm_vif_dbgfs_clean(struct iwl_mvm *mvm, struct ieee80211_vif *vif)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);

	debugfs_remove(mvmvif->dbgfs_slink);
	mvmvif->dbgfs_slink = NULL;

	debugfs_remove_recursive(mvmvif->dbgfs_dir);
	mvmvif->dbgfs_dir = NULL;
}
