/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2012 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2015 Intel Mobile Communications GmbH
 * Copyright(c) 2016        Intel Deutschland GmbH
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
 * Copyright(c) 2012 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2015 Intel Mobile Communications GmbH
 * Copyright(c) 2016        Intel Deutschland GmbH
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
#include "fw-api-tof.h"
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

static ssize_t iwl_dbgfs_tx_pwr_lmt_read(struct file *file,
					 char __user *user_buf,
					 size_t count, loff_t *ppos)
{
	struct ieee80211_vif *vif = file->private_data;
	char buf[64];
	int bufsz = sizeof(buf);
	int pos;

	pos = scnprintf(buf, bufsz, "bss limit = %d\n",
			vif->bss_conf.txpower);

	return simple_read_from_buffer(user_buf, count, ppos, buf, pos);
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
			struct iwl_mvm_sta *mvm_sta = iwl_mvm_sta_from_mac80211(sta);

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

static inline char *iwl_dbgfs_is_match(char *name, char *buf)
{
	int len = strlen(name);

	return !strncmp(name, buf, len) ? buf + len : NULL;
}

static ssize_t iwl_dbgfs_tof_enable_write(struct ieee80211_vif *vif,
					  char *buf,
					  size_t count, loff_t *ppos)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_mvm *mvm = mvmvif->mvm;
	u32 value;
	int ret = -EINVAL;
	char *data;

	mutex_lock(&mvm->mutex);

	data = iwl_dbgfs_is_match("tof_disabled=", buf);
	if (data) {
		ret = kstrtou32(data, 10, &value);
		if (ret == 0)
			mvm->tof_data.tof_cfg.tof_disabled = value;
		goto out;
	}

	data = iwl_dbgfs_is_match("one_sided_disabled=", buf);
	if (data) {
		ret = kstrtou32(data, 10, &value);
		if (ret == 0)
			mvm->tof_data.tof_cfg.one_sided_disabled = value;
		goto out;
	}

	data = iwl_dbgfs_is_match("is_debug_mode=", buf);
	if (data) {
		ret = kstrtou32(data, 10, &value);
		if (ret == 0)
			mvm->tof_data.tof_cfg.is_debug_mode = value;
		goto out;
	}

	data = iwl_dbgfs_is_match("is_buf=", buf);
	if (data) {
		ret = kstrtou32(data, 10, &value);
		if (ret == 0)
			mvm->tof_data.tof_cfg.is_buf_required = value;
		goto out;
	}

	data = iwl_dbgfs_is_match("send_tof_cfg=", buf);
	if (data) {
		ret = kstrtou32(data, 10, &value);
		if (ret == 0 && value) {
			ret = iwl_mvm_tof_config_cmd(mvm);
			goto out;
		}
	}

out:
	mutex_unlock(&mvm->mutex);

	return ret ?: count;
}

static ssize_t iwl_dbgfs_tof_enable_read(struct file *file,
					 char __user *user_buf,
					 size_t count, loff_t *ppos)
{
	struct ieee80211_vif *vif = file->private_data;
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_mvm *mvm = mvmvif->mvm;
	char buf[256];
	int pos = 0;
	const size_t bufsz = sizeof(buf);
	struct iwl_tof_config_cmd *cmd;

	cmd = &mvm->tof_data.tof_cfg;

	mutex_lock(&mvm->mutex);

	pos += scnprintf(buf + pos, bufsz - pos, "tof_disabled = %d\n",
			 cmd->tof_disabled);
	pos += scnprintf(buf + pos, bufsz - pos, "one_sided_disabled = %d\n",
			 cmd->one_sided_disabled);
	pos += scnprintf(buf + pos, bufsz - pos, "is_debug_mode = %d\n",
			 cmd->is_debug_mode);
	pos += scnprintf(buf + pos, bufsz - pos, "is_buf_required = %d\n",
			 cmd->is_buf_required);

	mutex_unlock(&mvm->mutex);

	return simple_read_from_buffer(user_buf, count, ppos, buf, pos);
}

static ssize_t iwl_dbgfs_tof_responder_params_write(struct ieee80211_vif *vif,
						    char *buf,
						    size_t count, loff_t *ppos)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_mvm *mvm = mvmvif->mvm;
	u32 value;
	int ret = 0;
	char *data;

	mutex_lock(&mvm->mutex);

	data = iwl_dbgfs_is_match("burst_period=", buf);
	if (data) {
		ret = kstrtou32(data, 10, &value);
		if (!ret)
			mvm->tof_data.responder_cfg.burst_period =
							cpu_to_le16(value);
		goto out;
	}

	data = iwl_dbgfs_is_match("min_delta_ftm=", buf);
	if (data) {
		ret = kstrtou32(data, 10, &value);
		if (ret == 0)
			mvm->tof_data.responder_cfg.min_delta_ftm = value;
		goto out;
	}

	data = iwl_dbgfs_is_match("burst_duration=", buf);
	if (data) {
		ret = kstrtou32(data, 10, &value);
		if (ret == 0)
			mvm->tof_data.responder_cfg.burst_duration = value;
		goto out;
	}

	data = iwl_dbgfs_is_match("num_of_burst_exp=", buf);
	if (data) {
		ret = kstrtou32(data, 10, &value);
		if (ret == 0)
			mvm->tof_data.responder_cfg.num_of_burst_exp = value;
		goto out;
	}

	data = iwl_dbgfs_is_match("abort_responder=", buf);
	if (data) {
		ret = kstrtou32(data, 10, &value);
		if (ret == 0)
			mvm->tof_data.responder_cfg.abort_responder = value;
		goto out;
	}

	data = iwl_dbgfs_is_match("get_ch_est=", buf);
	if (data) {
		ret = kstrtou32(data, 10, &value);
		if (ret == 0)
			mvm->tof_data.responder_cfg.get_ch_est = value;
		goto out;
	}

	data = iwl_dbgfs_is_match("recv_sta_req_params=", buf);
	if (data) {
		ret = kstrtou32(data, 10, &value);
		if (ret == 0)
			mvm->tof_data.responder_cfg.recv_sta_req_params = value;
		goto out;
	}

	data = iwl_dbgfs_is_match("channel_num=", buf);
	if (data) {
		ret = kstrtou32(data, 10, &value);
		if (ret == 0)
			mvm->tof_data.responder_cfg.channel_num = value;
		goto out;
	}

	data = iwl_dbgfs_is_match("bandwidth=", buf);
	if (data) {
		ret = kstrtou32(data, 10, &value);
		if (ret == 0)
			mvm->tof_data.responder_cfg.bandwidth = value;
		goto out;
	}

	data = iwl_dbgfs_is_match("rate=", buf);
	if (data) {
		ret = kstrtou32(data, 10, &value);
		if (ret == 0)
			mvm->tof_data.responder_cfg.rate = value;
		goto out;
	}

	data = iwl_dbgfs_is_match("bssid=", buf);
	if (data) {
		u8 *mac = mvm->tof_data.responder_cfg.bssid;

		if (!mac_pton(data, mac)) {
			ret = -EINVAL;
			goto out;
		}
	}

	data = iwl_dbgfs_is_match("tsf_timer_offset_msecs=", buf);
	if (data) {
		ret = kstrtou32(data, 10, &value);
		if (ret == 0)
			mvm->tof_data.responder_cfg.tsf_timer_offset_msecs =
							cpu_to_le16(value);
		goto out;
	}

	data = iwl_dbgfs_is_match("toa_offset=", buf);
	if (data) {
		ret = kstrtou32(data, 10, &value);
		if (ret == 0)
			mvm->tof_data.responder_cfg.toa_offset =
							cpu_to_le16(value);
		goto out;
	}

	data = iwl_dbgfs_is_match("center_freq=", buf);
	if (data) {
		struct iwl_tof_responder_config_cmd *cmd =
			&mvm->tof_data.responder_cfg;

		ret = kstrtou32(data, 10, &value);
		if (ret == 0 && value) {
			enum nl80211_band band = (cmd->channel_num <= 14) ?
						   NL80211_BAND_2GHZ :
						   NL80211_BAND_5GHZ;
			struct ieee80211_channel chn = {
				.band = band,
				.center_freq = ieee80211_channel_to_frequency(
					cmd->channel_num, band),
				};
			struct cfg80211_chan_def chandef = {
				.chan =  &chn,
				.center_freq1 =
					ieee80211_channel_to_frequency(value,
								       band),
			};

			cmd->ctrl_ch_position = iwl_mvm_get_ctrl_pos(&chandef);
		}
		goto out;
	}

	data = iwl_dbgfs_is_match("ftm_per_burst=", buf);
	if (data) {
		ret = kstrtou32(data, 10, &value);
		if (ret == 0)
			mvm->tof_data.responder_cfg.ftm_per_burst = value;
		goto out;
	}

	data = iwl_dbgfs_is_match("ftm_resp_ts_avail=", buf);
	if (data) {
		ret = kstrtou32(data, 10, &value);
		if (ret == 0)
			mvm->tof_data.responder_cfg.ftm_resp_ts_avail = value;
		goto out;
	}

	data = iwl_dbgfs_is_match("asap_mode=", buf);
	if (data) {
		ret = kstrtou32(data, 10, &value);
		if (ret == 0)
			mvm->tof_data.responder_cfg.asap_mode = value;
		goto out;
	}

	data = iwl_dbgfs_is_match("send_responder_cfg=", buf);
	if (data) {
		ret = kstrtou32(data, 10, &value);
		if (ret == 0 && value) {
			ret = iwl_mvm_tof_responder_cmd(mvm, vif);
			goto out;
		}
	}

out:
	mutex_unlock(&mvm->mutex);

	return ret ?: count;
}

static ssize_t iwl_dbgfs_tof_responder_params_read(struct file *file,
						   char __user *user_buf,
						   size_t count, loff_t *ppos)
{
	struct ieee80211_vif *vif = file->private_data;
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_mvm *mvm = mvmvif->mvm;
	char buf[256];
	int pos = 0;
	const size_t bufsz = sizeof(buf);
	struct iwl_tof_responder_config_cmd *cmd;

	cmd = &mvm->tof_data.responder_cfg;

	mutex_lock(&mvm->mutex);

	pos += scnprintf(buf + pos, bufsz - pos, "burst_period = %d\n",
			 le16_to_cpu(cmd->burst_period));
	pos += scnprintf(buf + pos, bufsz - pos, "burst_duration = %d\n",
			 cmd->burst_duration);
	pos += scnprintf(buf + pos, bufsz - pos, "bandwidth = %d\n",
			 cmd->bandwidth);
	pos += scnprintf(buf + pos, bufsz - pos, "channel_num = %d\n",
			 cmd->channel_num);
	pos += scnprintf(buf + pos, bufsz - pos, "ctrl_ch_position = 0x%x\n",
			 cmd->ctrl_ch_position);
	pos += scnprintf(buf + pos, bufsz - pos, "bssid = %pM\n",
			 cmd->bssid);
	pos += scnprintf(buf + pos, bufsz - pos, "min_delta_ftm = %d\n",
			 cmd->min_delta_ftm);
	pos += scnprintf(buf + pos, bufsz - pos, "num_of_burst_exp = %d\n",
			 cmd->num_of_burst_exp);
	pos += scnprintf(buf + pos, bufsz - pos, "rate = %d\n", cmd->rate);
	pos += scnprintf(buf + pos, bufsz - pos, "abort_responder = %d\n",
			 cmd->abort_responder);
	pos += scnprintf(buf + pos, bufsz - pos, "get_ch_est = %d\n",
			 cmd->get_ch_est);
	pos += scnprintf(buf + pos, bufsz - pos, "recv_sta_req_params = %d\n",
			 cmd->recv_sta_req_params);
	pos += scnprintf(buf + pos, bufsz - pos, "ftm_per_burst = %d\n",
			 cmd->ftm_per_burst);
	pos += scnprintf(buf + pos, bufsz - pos, "ftm_resp_ts_avail = %d\n",
			 cmd->ftm_resp_ts_avail);
	pos += scnprintf(buf + pos, bufsz - pos, "asap_mode = %d\n",
			 cmd->asap_mode);
	pos += scnprintf(buf + pos, bufsz - pos,
			 "tsf_timer_offset_msecs = %d\n",
			 le16_to_cpu(cmd->tsf_timer_offset_msecs));
	pos += scnprintf(buf + pos, bufsz - pos, "toa_offset = %d\n",
			 le16_to_cpu(cmd->toa_offset));

	mutex_unlock(&mvm->mutex);

	return simple_read_from_buffer(user_buf, count, ppos, buf, pos);
}

static ssize_t iwl_dbgfs_tof_range_request_write(struct ieee80211_vif *vif,
						 char *buf, size_t count,
						 loff_t *ppos)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_mvm *mvm = mvmvif->mvm;
	u32 value;
	int ret = 0;
	char *data;

	mutex_lock(&mvm->mutex);

	data = iwl_dbgfs_is_match("request_id=", buf);
	if (data) {
		ret = kstrtou32(data, 10, &value);
		if (ret == 0)
			mvm->tof_data.range_req.request_id = value;
		goto out;
	}

	data = iwl_dbgfs_is_match("initiator=", buf);
	if (data) {
		ret = kstrtou32(data, 10, &value);
		if (ret == 0)
			mvm->tof_data.range_req.initiator = value;
		goto out;
	}

	data = iwl_dbgfs_is_match("one_sided_los_disable=", buf);
	if (data) {
		ret = kstrtou32(data, 10, &value);
		if (ret == 0)
			mvm->tof_data.range_req.one_sided_los_disable = value;
		goto out;
	}

	data = iwl_dbgfs_is_match("req_timeout=", buf);
	if (data) {
		ret = kstrtou32(data, 10, &value);
		if (ret == 0)
			mvm->tof_data.range_req.req_timeout = value;
		goto out;
	}

	data = iwl_dbgfs_is_match("report_policy=", buf);
	if (data) {
		ret = kstrtou32(data, 10, &value);
		if (ret == 0)
			mvm->tof_data.range_req.report_policy = value;
		goto out;
	}

	data = iwl_dbgfs_is_match("macaddr_random=", buf);
	if (data) {
		ret = kstrtou32(data, 10, &value);
		if (ret == 0)
			mvm->tof_data.range_req.macaddr_random = value;
		goto out;
	}

	data = iwl_dbgfs_is_match("num_of_ap=", buf);
	if (data) {
		ret = kstrtou32(data, 10, &value);
		if (ret == 0)
			mvm->tof_data.range_req.num_of_ap = value;
		goto out;
	}

	data = iwl_dbgfs_is_match("macaddr_template=", buf);
	if (data) {
		u8 mac[ETH_ALEN];

		if (!mac_pton(data, mac)) {
			ret = -EINVAL;
			goto out;
		}
		memcpy(mvm->tof_data.range_req.macaddr_template, mac, ETH_ALEN);
		goto out;
	}

	data = iwl_dbgfs_is_match("macaddr_mask=", buf);
	if (data) {
		u8 mac[ETH_ALEN];

		if (!mac_pton(data, mac)) {
			ret = -EINVAL;
			goto out;
		}
		memcpy(mvm->tof_data.range_req.macaddr_mask, mac, ETH_ALEN);
		goto out;
	}

	data = iwl_dbgfs_is_match("ap=", buf);
	if (data) {
		struct iwl_tof_range_req_ap_entry ap = {};
		int size = sizeof(struct iwl_tof_range_req_ap_entry);
		u16 burst_period;
		u8 *mac = ap.bssid;
		unsigned int i;

		if (sscanf(data, "%u %hhd %hhd %hhd"
			   "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx"
			   "%hhd %hhd %hd"
			   "%hhd %hhd %d"
			   "%hhx %hhd %hhd %hhd",
			   &i, &ap.channel_num, &ap.bandwidth,
			   &ap.ctrl_ch_position,
			   mac, mac + 1, mac + 2, mac + 3, mac + 4, mac + 5,
			   &ap.measure_type, &ap.num_of_bursts,
			   &burst_period,
			   &ap.samples_per_burst, &ap.retries_per_sample,
			   &ap.tsf_delta, &ap.location_req, &ap.asap_mode,
			   &ap.enable_dyn_ack, &ap.rssi) != 20) {
			ret = -EINVAL;
			goto out;
		}
		if (i >= IWL_MVM_TOF_MAX_APS) {
			IWL_ERR(mvm, "Invalid AP index %d\n", i);
			ret = -EINVAL;
			goto out;
		}

		ap.burst_period = cpu_to_le16(burst_period);

		memcpy(&mvm->tof_data.range_req.ap[i], &ap, size);
		goto out;
	}

	data = iwl_dbgfs_is_match("send_range_request=", buf);
	if (data) {
		ret = kstrtou32(data, 10, &value);
		if (ret == 0 && value)
			ret = iwl_mvm_tof_range_request_cmd(mvm, vif);
		goto out;
	}

	ret = -EINVAL;
out:
	mutex_unlock(&mvm->mutex);
	return ret ?: count;
}

static ssize_t iwl_dbgfs_tof_range_request_read(struct file *file,
						char __user *user_buf,
						size_t count, loff_t *ppos)
{
	struct ieee80211_vif *vif = file->private_data;
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_mvm *mvm = mvmvif->mvm;
	char buf[512];
	int pos = 0;
	const size_t bufsz = sizeof(buf);
	struct iwl_tof_range_req_cmd *cmd;
	int i;

	cmd = &mvm->tof_data.range_req;

	mutex_lock(&mvm->mutex);

	pos += scnprintf(buf + pos, bufsz - pos, "request_id= %d\n",
			 cmd->request_id);
	pos += scnprintf(buf + pos, bufsz - pos, "initiator= %d\n",
			 cmd->initiator);
	pos += scnprintf(buf + pos, bufsz - pos, "one_sided_los_disable = %d\n",
			 cmd->one_sided_los_disable);
	pos += scnprintf(buf + pos, bufsz - pos, "req_timeout= %d\n",
			 cmd->req_timeout);
	pos += scnprintf(buf + pos, bufsz - pos, "report_policy= %d\n",
			 cmd->report_policy);
	pos += scnprintf(buf + pos, bufsz - pos, "macaddr_random= %d\n",
			 cmd->macaddr_random);
	pos += scnprintf(buf + pos, bufsz - pos, "macaddr_template= %pM\n",
			 cmd->macaddr_template);
	pos += scnprintf(buf + pos, bufsz - pos, "macaddr_mask= %pM\n",
			 cmd->macaddr_mask);
	pos += scnprintf(buf + pos, bufsz - pos, "num_of_ap= %d\n",
			 cmd->num_of_ap);
	for (i = 0; i < cmd->num_of_ap; i++) {
		struct iwl_tof_range_req_ap_entry *ap = &cmd->ap[i];

		pos += scnprintf(buf + pos, bufsz - pos,
				"ap %.2d: channel_num=%hhd bw=%hhd"
				" control=%hhd bssid=%pM type=%hhd"
				" num_of_bursts=%hhd burst_period=%hd ftm=%hhd"
				" retries=%hhd tsf_delta=%d"
				" tsf_delta_direction=%hhd location_req=0x%hhx "
				" asap=%hhd enable=%hhd rssi=%hhd\n",
				i, ap->channel_num, ap->bandwidth,
				ap->ctrl_ch_position, ap->bssid,
				ap->measure_type, ap->num_of_bursts,
				ap->burst_period, ap->samples_per_burst,
				ap->retries_per_sample, ap->tsf_delta,
				ap->tsf_delta_direction,
				ap->location_req, ap->asap_mode,
				ap->enable_dyn_ack, ap->rssi);
	}

	mutex_unlock(&mvm->mutex);

	return simple_read_from_buffer(user_buf, count, ppos, buf, pos);
}

static ssize_t iwl_dbgfs_tof_range_req_ext_write(struct ieee80211_vif *vif,
						 char *buf,
						 size_t count, loff_t *ppos)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_mvm *mvm = mvmvif->mvm;
	u32 value;
	int ret = 0;
	char *data;

	mutex_lock(&mvm->mutex);

	data = iwl_dbgfs_is_match("tsf_timer_offset_msec=", buf);
	if (data) {
		ret = kstrtou32(data, 10, &value);
		if (ret == 0)
			mvm->tof_data.range_req_ext.tsf_timer_offset_msec =
							cpu_to_le16(value);
		goto out;
	}

	data = iwl_dbgfs_is_match("min_delta_ftm=", buf);
	if (data) {
		ret = kstrtou32(data, 10, &value);
		if (ret == 0)
			mvm->tof_data.range_req_ext.min_delta_ftm = value;
		goto out;
	}

	data = iwl_dbgfs_is_match("ftm_format_and_bw20M=", buf);
	if (data) {
		ret = kstrtou32(data, 10, &value);
		if (ret == 0)
			mvm->tof_data.range_req_ext.ftm_format_and_bw20M =
									value;
		goto out;
	}

	data = iwl_dbgfs_is_match("ftm_format_and_bw40M=", buf);
	if (data) {
		ret = kstrtou32(data, 10, &value);
		if (ret == 0)
			mvm->tof_data.range_req_ext.ftm_format_and_bw40M =
									value;
		goto out;
	}

	data = iwl_dbgfs_is_match("ftm_format_and_bw80M=", buf);
	if (data) {
		ret = kstrtou32(data, 10, &value);
		if (ret == 0)
			mvm->tof_data.range_req_ext.ftm_format_and_bw80M =
									value;
		goto out;
	}

	data = iwl_dbgfs_is_match("send_range_req_ext=", buf);
	if (data) {
		ret = kstrtou32(data, 10, &value);
		if (ret == 0 && value)
			ret = iwl_mvm_tof_range_request_ext_cmd(mvm, vif);
		goto out;
	}

	ret = -EINVAL;
out:
	mutex_unlock(&mvm->mutex);
	return ret ?: count;
}

static ssize_t iwl_dbgfs_tof_range_req_ext_read(struct file *file,
						char __user *user_buf,
						size_t count, loff_t *ppos)
{
	struct ieee80211_vif *vif = file->private_data;
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_mvm *mvm = mvmvif->mvm;
	char buf[256];
	int pos = 0;
	const size_t bufsz = sizeof(buf);
	struct iwl_tof_range_req_ext_cmd *cmd;

	cmd = &mvm->tof_data.range_req_ext;

	mutex_lock(&mvm->mutex);

	pos += scnprintf(buf + pos, bufsz - pos,
			 "tsf_timer_offset_msec = %hd\n",
			 cmd->tsf_timer_offset_msec);
	pos += scnprintf(buf + pos, bufsz - pos, "min_delta_ftm = %hhd\n",
			 cmd->min_delta_ftm);
	pos += scnprintf(buf + pos, bufsz - pos,
			 "ftm_format_and_bw20M = %hhd\n",
			 cmd->ftm_format_and_bw20M);
	pos += scnprintf(buf + pos, bufsz - pos,
			 "ftm_format_and_bw40M = %hhd\n",
			 cmd->ftm_format_and_bw40M);
	pos += scnprintf(buf + pos, bufsz - pos,
			 "ftm_format_and_bw80M = %hhd\n",
			 cmd->ftm_format_and_bw80M);

	mutex_unlock(&mvm->mutex);
	return simple_read_from_buffer(user_buf, count, ppos, buf, pos);
}

static ssize_t iwl_dbgfs_tof_range_abort_write(struct ieee80211_vif *vif,
					       char *buf,
					       size_t count, loff_t *ppos)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_mvm *mvm = mvmvif->mvm;
	u32 value;
	int abort_id, ret = 0;
	char *data;

	mutex_lock(&mvm->mutex);

	data = iwl_dbgfs_is_match("abort_id=", buf);
	if (data) {
		ret = kstrtou32(data, 10, &value);
		if (ret == 0)
			mvm->tof_data.last_abort_id = value;
		goto out;
	}

	data = iwl_dbgfs_is_match("send_range_abort=", buf);
	if (data) {
		ret = kstrtou32(data, 10, &value);
		if (ret == 0 && value) {
			abort_id = mvm->tof_data.last_abort_id;
			ret = iwl_mvm_tof_range_abort_cmd(mvm, abort_id);
			goto out;
		}
	}

out:
	mutex_unlock(&mvm->mutex);
	return ret ?: count;
}

static ssize_t iwl_dbgfs_tof_range_abort_read(struct file *file,
					      char __user *user_buf,
					      size_t count, loff_t *ppos)
{
	struct ieee80211_vif *vif = file->private_data;
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_mvm *mvm = mvmvif->mvm;
	char buf[32];
	int pos = 0;
	const size_t bufsz = sizeof(buf);
	int last_abort_id;

	mutex_lock(&mvm->mutex);
	last_abort_id = mvm->tof_data.last_abort_id;
	mutex_unlock(&mvm->mutex);

	pos += scnprintf(buf + pos, bufsz - pos, "last_abort_id = %d\n",
			 last_abort_id);
	return simple_read_from_buffer(user_buf, count, ppos, buf, pos);
}

static ssize_t iwl_dbgfs_tof_range_response_read(struct file *file,
						 char __user *user_buf,
						 size_t count, loff_t *ppos)
{
	struct ieee80211_vif *vif = file->private_data;
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_mvm *mvm = mvmvif->mvm;
	char *buf;
	int pos = 0;
	const size_t bufsz = sizeof(struct iwl_tof_range_rsp_ntfy) + 256;
	struct iwl_tof_range_rsp_ntfy *cmd;
	int i, ret;

	buf = kzalloc(bufsz, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	mutex_lock(&mvm->mutex);
	cmd = &mvm->tof_data.range_resp;

	pos += scnprintf(buf + pos, bufsz - pos, "request_id = %d\n",
			 cmd->request_id);
	pos += scnprintf(buf + pos, bufsz - pos, "status = %d\n",
			 cmd->request_status);
	pos += scnprintf(buf + pos, bufsz - pos, "last_in_batch = %d\n",
			 cmd->last_in_batch);
	pos += scnprintf(buf + pos, bufsz - pos, "num_of_aps = %d\n",
			 cmd->num_of_aps);
	for (i = 0; i < cmd->num_of_aps; i++) {
		struct iwl_tof_range_rsp_ap_entry_ntfy *ap = &cmd->ap[i];

		pos += scnprintf(buf + pos, bufsz - pos,
				"ap %.2d: bssid=%pM status=%hhd bw=%hhd"
				" rtt=%d rtt_var=%d rtt_spread=%d"
				" rssi=%hhd  rssi_spread=%hhd"
				" range=%d range_var=%d"
				" time_stamp=%d\n",
				i, ap->bssid, ap->measure_status,
				ap->measure_bw,
				ap->rtt, ap->rtt_variance, ap->rtt_spread,
				ap->rssi, ap->rssi_spread, ap->range,
				ap->range_variance, ap->timestamp);
	}
	mutex_unlock(&mvm->mutex);

	ret = simple_read_from_buffer(user_buf, count, ppos, buf, pos);
	kfree(buf);
	return ret;
}

static ssize_t iwl_dbgfs_low_latency_write(struct ieee80211_vif *vif, char *buf,
					   size_t count, loff_t *ppos)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_mvm *mvm = mvmvif->mvm;
	bool prev;
	u8 value;
	int ret;

	ret = kstrtou8(buf, 0, &value);
	if (ret)
		return ret;
	if (value > 1)
		return -EINVAL;

	mutex_lock(&mvm->mutex);
	prev = iwl_mvm_vif_low_latency(mvmvif);
	mvmvif->low_latency_dbgfs = value;
	iwl_mvm_update_low_latency(mvm, vif, prev);
	mutex_unlock(&mvm->mutex);

	return count;
}

static ssize_t iwl_dbgfs_low_latency_read(struct file *file,
					  char __user *user_buf,
					  size_t count, loff_t *ppos)
{
	struct ieee80211_vif *vif = file->private_data;
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	char buf[30] = {};
	int len;

	len = snprintf(buf, sizeof(buf) - 1,
		       "traffic=%d\ndbgfs=%d\nvcmd=%d\n",
		       mvmvif->low_latency_traffic,
		       mvmvif->low_latency_dbgfs,
		       mvmvif->low_latency_vcmd);
	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static ssize_t iwl_dbgfs_uapsd_misbehaving_read(struct file *file,
						char __user *user_buf,
						size_t count, loff_t *ppos)
{
	struct ieee80211_vif *vif = file->private_data;
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	char buf[20];
	int len;

	len = sprintf(buf, "%pM\n", mvmvif->uapsd_misbehaving_bssid);
	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static ssize_t iwl_dbgfs_uapsd_misbehaving_write(struct ieee80211_vif *vif,
						 char *buf, size_t count,
						 loff_t *ppos)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_mvm *mvm = mvmvif->mvm;
	bool ret;

	mutex_lock(&mvm->mutex);
	ret = mac_pton(buf, mvmvif->uapsd_misbehaving_bssid);
	mutex_unlock(&mvm->mutex);

	return ret ? count : -EINVAL;
}

static ssize_t iwl_dbgfs_rx_phyinfo_write(struct ieee80211_vif *vif, char *buf,
					  size_t count, loff_t *ppos)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_mvm *mvm = mvmvif->mvm;
	struct ieee80211_chanctx_conf *chanctx_conf;
	struct iwl_mvm_phy_ctxt *phy_ctxt;
	u16 value;
	int ret;

	ret = kstrtou16(buf, 0, &value);
	if (ret)
		return ret;

	mutex_lock(&mvm->mutex);
	rcu_read_lock();

	chanctx_conf = rcu_dereference(vif->chanctx_conf);
	/* make sure the channel context is assigned */
	if (!chanctx_conf) {
		rcu_read_unlock();
		mutex_unlock(&mvm->mutex);
		return -EINVAL;
	}

	phy_ctxt = &mvm->phy_ctxts[*(u16 *)chanctx_conf->drv_priv];
	rcu_read_unlock();

	mvm->dbgfs_rx_phyinfo = value;

	ret = iwl_mvm_phy_ctxt_changed(mvm, phy_ctxt, &chanctx_conf->min_def,
				       chanctx_conf->rx_chains_static,
				       chanctx_conf->rx_chains_dynamic);
	mutex_unlock(&mvm->mutex);

	return ret ?: count;
}

static ssize_t iwl_dbgfs_rx_phyinfo_read(struct file *file,
					 char __user *user_buf,
					 size_t count, loff_t *ppos)
{
	struct ieee80211_vif *vif = file->private_data;
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	char buf[8];

	snprintf(buf, sizeof(buf), "0x%04x\n", mvmvif->mvm->dbgfs_rx_phyinfo);

	return simple_read_from_buffer(user_buf, count, ppos, buf, sizeof(buf));
}

static void iwl_dbgfs_quota_check(void *data, u8 *mac,
				  struct ieee80211_vif *vif)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	int *ret = data;

	if (mvmvif->dbgfs_quota_min)
		*ret = -EINVAL;
}

static ssize_t iwl_dbgfs_quota_min_write(struct ieee80211_vif *vif, char *buf,
					 size_t count, loff_t *ppos)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_mvm *mvm = mvmvif->mvm;
	u16 value;
	int ret;

	ret = kstrtou16(buf, 0, &value);
	if (ret)
		return ret;

	if (value > 95)
		return -EINVAL;

	mutex_lock(&mvm->mutex);

	mvmvif->dbgfs_quota_min = 0;
	ieee80211_iterate_interfaces(mvm->hw, IEEE80211_IFACE_ITER_NORMAL,
				     iwl_dbgfs_quota_check, &ret);
	if (ret == 0) {
		mvmvif->dbgfs_quota_min = value;
		iwl_mvm_update_quotas(mvm, false, NULL);
	}
	mutex_unlock(&mvm->mutex);

	return ret ?: count;
}

static ssize_t iwl_dbgfs_quota_min_read(struct file *file,
					char __user *user_buf,
					size_t count, loff_t *ppos)
{
	struct ieee80211_vif *vif = file->private_data;
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	char buf[10];
	int len;

	len = snprintf(buf, sizeof(buf), "%d\n", mvmvif->dbgfs_quota_min);

	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static const char * const chanwidths[] = {
	[NL80211_CHAN_WIDTH_20_NOHT] = "noht",
	[NL80211_CHAN_WIDTH_20] = "ht20",
	[NL80211_CHAN_WIDTH_40] = "ht40",
	[NL80211_CHAN_WIDTH_80] = "vht80",
	[NL80211_CHAN_WIDTH_80P80] = "vht80p80",
	[NL80211_CHAN_WIDTH_160] = "vht160",
};

static bool iwl_mvm_lqm_notif_wait(struct iwl_notif_wait_data *notif_wait,
				   struct iwl_rx_packet *pkt, void *data)
{
	struct ieee80211_vif *vif = data;
	struct iwl_mvm *mvm =
		container_of(notif_wait, struct iwl_mvm, notif_wait);
	struct iwl_link_qual_msrmnt_notif *report = (void *)pkt->data;
	u32 num_of_stations = le32_to_cpu(report->number_of_stations);
	int i;

	IWL_INFO(mvm, "LQM report:\n");
	IWL_INFO(mvm, "\tstatus: %d\n", report->status);
	IWL_INFO(mvm, "\tmacID: %d\n", le32_to_cpu(report->mac_id));
	IWL_INFO(mvm, "\ttx_frame_dropped: %d\n",
		 le32_to_cpu(report->tx_frame_dropped));
	IWL_INFO(mvm, "\ttime_in_measurement_window: %d us\n",
		 le32_to_cpu(report->time_in_measurement_window));
	IWL_INFO(mvm, "\ttotal_air_time_other_stations: %d\n",
		 le32_to_cpu(report->total_air_time_other_stations));
	IWL_INFO(mvm, "\tchannel_freq: %d\n",
		 vif->bss_conf.chandef.center_freq1);
	IWL_INFO(mvm, "\tchannel_width: %s\n",
		 chanwidths[vif->bss_conf.chandef.width]);
	IWL_INFO(mvm, "\tnumber_of_stations: %d\n", num_of_stations);
	for (i = 0; i < num_of_stations; i++)
		IWL_INFO(mvm, "\t\tsta[%d]: %d\n", i,
			 report->frequent_stations_air_time[i]);

	return true;
}

static ssize_t iwl_dbgfs_lqm_send_cmd_write(struct ieee80211_vif *vif,
					    char *buf, size_t count,
					    loff_t *ppos)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_mvm *mvm = mvmvif->mvm;
	struct iwl_notification_wait wait_lqm_notif;
	static u16 lqm_notif[] = {
		WIDE_ID(MAC_CONF_GROUP,
			LINK_QUALITY_MEASUREMENT_COMPLETE_NOTIF)
	};
	int err;
	u32 duration;
	u32 timeout;

	if (sscanf(buf, "%d,%d", &duration, &timeout) != 2)
		return -EINVAL;

	iwl_init_notification_wait(&mvm->notif_wait, &wait_lqm_notif,
				   lqm_notif, ARRAY_SIZE(lqm_notif),
				   iwl_mvm_lqm_notif_wait, vif);
	mutex_lock(&mvm->mutex);
	err = iwl_mvm_send_lqm_cmd(vif, LQM_CMD_OPERATION_START_MEASUREMENT,
				   duration, timeout);
	mutex_unlock(&mvm->mutex);

	if (err) {
		IWL_ERR(mvm, "Failed to send lqm cmdf(err=%d)\n", err);
		iwl_remove_notification(&mvm->notif_wait, &wait_lqm_notif);
		return err;
	}

	/* wait for 2 * timeout (safety guard) and convert to jiffies*/
	timeout = msecs_to_jiffies((timeout * 2) / 1000);

	err = iwl_wait_notification(&mvm->notif_wait, &wait_lqm_notif,
				    timeout);
	if (err)
		IWL_ERR(mvm, "Getting lqm notif timed out\n");

	return count;
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
MVM_DEBUGFS_READ_FILE_OPS(tx_pwr_lmt);
MVM_DEBUGFS_READ_WRITE_FILE_OPS(pm_params, 32);
MVM_DEBUGFS_READ_WRITE_FILE_OPS(bf_params, 256);
MVM_DEBUGFS_READ_WRITE_FILE_OPS(low_latency, 10);
MVM_DEBUGFS_READ_WRITE_FILE_OPS(uapsd_misbehaving, 20);
MVM_DEBUGFS_READ_WRITE_FILE_OPS(rx_phyinfo, 10);
MVM_DEBUGFS_READ_WRITE_FILE_OPS(tof_enable, 32);
MVM_DEBUGFS_READ_WRITE_FILE_OPS(tof_range_request, 512);
MVM_DEBUGFS_READ_WRITE_FILE_OPS(tof_range_req_ext, 32);
MVM_DEBUGFS_READ_WRITE_FILE_OPS(tof_range_abort, 32);
MVM_DEBUGFS_READ_FILE_OPS(tof_range_response);
MVM_DEBUGFS_READ_WRITE_FILE_OPS(tof_responder_params, 32);
MVM_DEBUGFS_READ_WRITE_FILE_OPS(quota_min, 32);
MVM_DEBUGFS_WRITE_FILE_OPS(lqm_send_cmd, 64);

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

	MVM_DEBUGFS_ADD_FILE_VIF(tx_pwr_lmt, mvmvif->dbgfs_dir, S_IRUSR);
	MVM_DEBUGFS_ADD_FILE_VIF(mac_params, mvmvif->dbgfs_dir, S_IRUSR);
	MVM_DEBUGFS_ADD_FILE_VIF(low_latency, mvmvif->dbgfs_dir,
				 S_IRUSR | S_IWUSR);
	MVM_DEBUGFS_ADD_FILE_VIF(uapsd_misbehaving, mvmvif->dbgfs_dir,
				 S_IRUSR | S_IWUSR);
	MVM_DEBUGFS_ADD_FILE_VIF(rx_phyinfo, mvmvif->dbgfs_dir,
				 S_IRUSR | S_IWUSR);
	MVM_DEBUGFS_ADD_FILE_VIF(quota_min, mvmvif->dbgfs_dir,
				 S_IRUSR | S_IWUSR);
	MVM_DEBUGFS_ADD_FILE_VIF(lqm_send_cmd, mvmvif->dbgfs_dir, S_IWUSR);

	if (vif->type == NL80211_IFTYPE_STATION && !vif->p2p &&
	    mvmvif == mvm->bf_allowed_vif)
		MVM_DEBUGFS_ADD_FILE_VIF(bf_params, mvmvif->dbgfs_dir,
					 S_IRUSR | S_IWUSR);

	if (fw_has_capa(&mvm->fw->ucode_capa, IWL_UCODE_TLV_CAPA_TOF_SUPPORT) &&
	    !vif->p2p && (vif->type != NL80211_IFTYPE_P2P_DEVICE)) {
		if (IWL_MVM_TOF_IS_RESPONDER && vif->type == NL80211_IFTYPE_AP)
			MVM_DEBUGFS_ADD_FILE_VIF(tof_responder_params,
						 mvmvif->dbgfs_dir,
						 S_IRUSR | S_IWUSR);

		MVM_DEBUGFS_ADD_FILE_VIF(tof_range_request, mvmvif->dbgfs_dir,
					 S_IRUSR | S_IWUSR);
		MVM_DEBUGFS_ADD_FILE_VIF(tof_range_req_ext, mvmvif->dbgfs_dir,
					 S_IRUSR | S_IWUSR);
		MVM_DEBUGFS_ADD_FILE_VIF(tof_enable, mvmvif->dbgfs_dir,
					 S_IRUSR | S_IWUSR);
		MVM_DEBUGFS_ADD_FILE_VIF(tof_range_abort, mvmvif->dbgfs_dir,
					 S_IRUSR | S_IWUSR);
		MVM_DEBUGFS_ADD_FILE_VIF(tof_range_response, mvmvif->dbgfs_dir,
					 S_IRUSR);
	}

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
