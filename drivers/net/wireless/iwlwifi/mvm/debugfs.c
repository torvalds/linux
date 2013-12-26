/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2012 - 2013 Intel Corporation. All rights reserved.
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
 * Copyright(c) 2012 - 2013 Intel Corporation. All rights reserved.
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
#include "sta.h"
#include "iwl-io.h"

struct iwl_dbgfs_mvm_ctx {
	struct iwl_mvm *mvm;
	struct ieee80211_vif *vif;
};

static ssize_t iwl_dbgfs_tx_flush_write(struct file *file,
					const char __user *user_buf,
					size_t count, loff_t *ppos)
{
	struct iwl_mvm *mvm = file->private_data;

	char buf[16];
	int buf_size, ret;
	u32 scd_q_msk;

	if (!mvm->ucode_loaded || mvm->cur_ucode != IWL_UCODE_REGULAR)
		return -EIO;

	memset(buf, 0, sizeof(buf));
	buf_size = min(count, sizeof(buf) - 1);
	if (copy_from_user(buf, user_buf, buf_size))
		return -EFAULT;

	if (sscanf(buf, "%x", &scd_q_msk) != 1)
		return -EINVAL;

	IWL_ERR(mvm, "FLUSHING queues: scd_q_msk = 0x%x\n", scd_q_msk);

	mutex_lock(&mvm->mutex);
	ret =  iwl_mvm_flush_tx_path(mvm, scd_q_msk, true) ? : count;
	mutex_unlock(&mvm->mutex);

	return ret;
}

static ssize_t iwl_dbgfs_sta_drain_write(struct file *file,
					 const char __user *user_buf,
					 size_t count, loff_t *ppos)
{
	struct iwl_mvm *mvm = file->private_data;
	struct ieee80211_sta *sta;

	char buf[8];
	int buf_size, sta_id, drain, ret;

	if (!mvm->ucode_loaded || mvm->cur_ucode != IWL_UCODE_REGULAR)
		return -EIO;

	memset(buf, 0, sizeof(buf));
	buf_size = min(count, sizeof(buf) - 1);
	if (copy_from_user(buf, user_buf, buf_size))
		return -EFAULT;

	if (sscanf(buf, "%d %d", &sta_id, &drain) != 2)
		return -EINVAL;
	if (sta_id < 0 || sta_id >= IWL_MVM_STATION_COUNT)
		return -EINVAL;
	if (drain < 0 || drain > 1)
		return -EINVAL;

	mutex_lock(&mvm->mutex);

	sta = rcu_dereference_protected(mvm->fw_id_to_mac_id[sta_id],
					lockdep_is_held(&mvm->mutex));
	if (IS_ERR_OR_NULL(sta))
		ret = -ENOENT;
	else
		ret = iwl_mvm_drain_sta(mvm, (void *)sta->drv_priv, drain) ? :
			count;

	mutex_unlock(&mvm->mutex);

	return ret;
}

static ssize_t iwl_dbgfs_sram_read(struct file *file, char __user *user_buf,
				   size_t count, loff_t *ppos)
{
	struct iwl_mvm *mvm = file->private_data;
	const struct fw_img *img;
	int ofs, len, pos = 0;
	size_t bufsz, ret;
	char *buf;
	u8 *ptr;

	if (!mvm->ucode_loaded)
		return -EINVAL;

	/* default is to dump the entire data segment */
	if (!mvm->dbgfs_sram_offset && !mvm->dbgfs_sram_len) {
		img = &mvm->fw->img[mvm->cur_ucode];
		ofs = img->sec[IWL_UCODE_SECTION_DATA].offset;
		len = img->sec[IWL_UCODE_SECTION_DATA].len;
	} else {
		ofs = mvm->dbgfs_sram_offset;
		len = mvm->dbgfs_sram_len;
	}

	bufsz = len * 4 + 256;
	buf = kzalloc(bufsz, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	ptr = kzalloc(len, GFP_KERNEL);
	if (!ptr) {
		kfree(buf);
		return -ENOMEM;
	}

	pos += scnprintf(buf + pos, bufsz - pos, "sram_len: 0x%x\n", len);
	pos += scnprintf(buf + pos, bufsz - pos, "sram_offset: 0x%x\n", ofs);

	iwl_trans_read_mem_bytes(mvm->trans, ofs, ptr, len);
	for (ofs = 0; ofs < len; ofs += 16) {
		pos += scnprintf(buf + pos, bufsz - pos, "0x%.4x ", ofs);
		hex_dump_to_buffer(ptr + ofs, 16, 16, 1, buf + pos,
				   bufsz - pos, false);
		pos += strlen(buf + pos);
		if (bufsz - pos > 0)
			buf[pos++] = '\n';
	}

	ret = simple_read_from_buffer(user_buf, count, ppos, buf, pos);

	kfree(buf);
	kfree(ptr);

	return ret;
}

static ssize_t iwl_dbgfs_sram_write(struct file *file,
				    const char __user *user_buf, size_t count,
				    loff_t *ppos)
{
	struct iwl_mvm *mvm = file->private_data;
	char buf[64];
	int buf_size;
	u32 offset, len;

	memset(buf, 0, sizeof(buf));
	buf_size = min(count, sizeof(buf) -  1);
	if (copy_from_user(buf, user_buf, buf_size))
		return -EFAULT;

	if (sscanf(buf, "%x,%x", &offset, &len) == 2) {
		if ((offset & 0x3) || (len & 0x3))
			return -EINVAL;
		mvm->dbgfs_sram_offset = offset;
		mvm->dbgfs_sram_len = len;
	} else {
		mvm->dbgfs_sram_offset = 0;
		mvm->dbgfs_sram_len = 0;
	}

	return count;
}

static ssize_t iwl_dbgfs_stations_read(struct file *file, char __user *user_buf,
				       size_t count, loff_t *ppos)
{
	struct iwl_mvm *mvm = file->private_data;
	struct ieee80211_sta *sta;
	char buf[400];
	int i, pos = 0, bufsz = sizeof(buf);

	mutex_lock(&mvm->mutex);

	for (i = 0; i < IWL_MVM_STATION_COUNT; i++) {
		pos += scnprintf(buf + pos, bufsz - pos, "%.2d: ", i);
		sta = rcu_dereference_protected(mvm->fw_id_to_mac_id[i],
						lockdep_is_held(&mvm->mutex));
		if (!sta)
			pos += scnprintf(buf + pos, bufsz - pos, "N/A\n");
		else if (IS_ERR(sta))
			pos += scnprintf(buf + pos, bufsz - pos, "%ld\n",
					 PTR_ERR(sta));
		else
			pos += scnprintf(buf + pos, bufsz - pos, "%pM\n",
					 sta->addr);
	}

	mutex_unlock(&mvm->mutex);

	return simple_read_from_buffer(user_buf, count, ppos, buf, pos);
}

static ssize_t iwl_dbgfs_disable_power_off_read(struct file *file,
						char __user *user_buf,
						size_t count, loff_t *ppos)
{
	struct iwl_mvm *mvm = file->private_data;
	char buf[64];
	int bufsz = sizeof(buf);
	int pos = 0;

	pos += scnprintf(buf+pos, bufsz-pos, "disable_power_off_d0=%d\n",
			 mvm->disable_power_off);
	pos += scnprintf(buf+pos, bufsz-pos, "disable_power_off_d3=%d\n",
			 mvm->disable_power_off_d3);

	return simple_read_from_buffer(user_buf, count, ppos, buf, pos);
}

static ssize_t iwl_dbgfs_disable_power_off_write(struct file *file,
						 const char __user *user_buf,
						 size_t count, loff_t *ppos)
{
	struct iwl_mvm *mvm = file->private_data;
	char buf[64] = {};
	int ret;
	int val;

	if (!mvm->ucode_loaded)
		return -EIO;

	count = min_t(size_t, count, sizeof(buf) - 1);
	if (copy_from_user(buf, user_buf, count))
		return -EFAULT;

	if (!strncmp("disable_power_off_d0=", buf, 21)) {
		if (sscanf(buf + 21, "%d", &val) != 1)
			return -EINVAL;
		mvm->disable_power_off = val;
	} else if (!strncmp("disable_power_off_d3=", buf, 21)) {
		if (sscanf(buf + 21, "%d", &val) != 1)
			return -EINVAL;
		mvm->disable_power_off_d3 = val;
	} else {
		return -EINVAL;
	}

	mutex_lock(&mvm->mutex);
	ret = iwl_mvm_power_update_device_mode(mvm);
	mutex_unlock(&mvm->mutex);

	return ret ?: count;
}

static void iwl_dbgfs_update_pm(struct iwl_mvm *mvm,
				 struct ieee80211_vif *vif,
				 enum iwl_dbgfs_pm_mask param, int val)
{
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_dbgfs_pm *dbgfs_pm = &mvmvif->dbgfs_pm;

	dbgfs_pm->mask |= param;

	switch (param) {
	case MVM_DEBUGFS_PM_KEEP_ALIVE: {
		struct ieee80211_hw *hw = mvm->hw;
		int dtimper = hw->conf.ps_dtim_period ?: 1;
		int dtimper_msec = dtimper * vif->bss_conf.beacon_int;

		IWL_DEBUG_POWER(mvm, "debugfs: set keep_alive= %d sec\n", val);
		if (val * MSEC_PER_SEC < 3 * dtimper_msec) {
			IWL_WARN(mvm,
				 "debugfs: keep alive period (%ld msec) is less than minimum required (%d msec)\n",
				 val * MSEC_PER_SEC, 3 * dtimper_msec);
		}
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
	case MVM_DEBUGFS_PM_DISABLE_POWER_OFF:
		IWL_DEBUG_POWER(mvm, "disable_power_off=%d\n", val);
		dbgfs_pm->disable_power_off = val;
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
	}
}

static ssize_t iwl_dbgfs_pm_params_write(struct file *file,
					 const char __user *user_buf,
					 size_t count, loff_t *ppos)
{
	struct ieee80211_vif *vif = file->private_data;
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_mvm *mvm = mvmvif->dbgfs_data;
	enum iwl_dbgfs_pm_mask param;
	char buf[32] = {};
	int val;
	int ret;

	count = min_t(size_t, count, sizeof(buf) - 1);
	if (copy_from_user(buf, user_buf, count))
		return -EFAULT;

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
	} else if (!strncmp("disable_power_off=", buf, 18) &&
		   !(mvm->fw->ucode_capa.flags &
		     IWL_UCODE_TLV_FLAGS_DEVICE_PS_CMD)) {
		if (sscanf(buf + 18, "%d", &val) != 1)
			return -EINVAL;
		param = MVM_DEBUGFS_PM_DISABLE_POWER_OFF;
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
	} else {
		return -EINVAL;
	}

	mutex_lock(&mvm->mutex);
	iwl_dbgfs_update_pm(mvm, vif, param, val);
	ret = iwl_mvm_power_update_mode(mvm, vif);
	mutex_unlock(&mvm->mutex);

	return ret ?: count;
}

static ssize_t iwl_dbgfs_pm_params_read(struct file *file,
					char __user *user_buf,
					size_t count, loff_t *ppos)
{
	struct ieee80211_vif *vif = file->private_data;
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_mvm *mvm = mvmvif->dbgfs_data;
	char buf[512];
	int bufsz = sizeof(buf);
	int pos;

	pos = iwl_mvm_power_dbgfs_read(mvm, vif, buf, bufsz);

	return simple_read_from_buffer(user_buf, count, ppos, buf, pos);
}

static ssize_t iwl_dbgfs_mac_params_read(struct file *file,
					 char __user *user_buf,
					 size_t count, loff_t *ppos)
{
	struct ieee80211_vif *vif = file->private_data;
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_mvm *mvm = mvmvif->dbgfs_data;
	u8 ap_sta_id;
	struct ieee80211_chanctx_conf *chanctx_conf;
	char buf[512];
	int bufsz = sizeof(buf);
	int pos = 0;
	int i;

	mutex_lock(&mvm->mutex);

	ap_sta_id = mvmvif->ap_sta_id;

	pos += scnprintf(buf+pos, bufsz-pos, "mac id/color: %d / %d\n",
			 mvmvif->id, mvmvif->color);
	pos += scnprintf(buf+pos, bufsz-pos, "bssid: %pM\n",
			 vif->bss_conf.bssid);
	pos += scnprintf(buf+pos, bufsz-pos, "QoS:\n");
	for (i = 0; i < ARRAY_SIZE(mvmvif->queue_params); i++) {
		pos += scnprintf(buf+pos, bufsz-pos,
				 "\t%d: txop:%d - cw_min:%d - cw_max = %d - aifs = %d upasd = %d\n",
				 i, mvmvif->queue_params[i].txop,
				 mvmvif->queue_params[i].cw_min,
				 mvmvif->queue_params[i].cw_max,
				 mvmvif->queue_params[i].aifs,
				 mvmvif->queue_params[i].uapsd);
	}

	if (vif->type == NL80211_IFTYPE_STATION &&
	    ap_sta_id != IWL_MVM_STATION_COUNT) {
		struct ieee80211_sta *sta;
		struct iwl_mvm_sta *mvm_sta;

		sta = rcu_dereference_protected(mvm->fw_id_to_mac_id[ap_sta_id],
						lockdep_is_held(&mvm->mutex));
		mvm_sta = (void *)sta->drv_priv;
		pos += scnprintf(buf+pos, bufsz-pos,
				 "ap_sta_id %d - reduced Tx power %d\n",
				 ap_sta_id, mvm_sta->bt_reduced_txpower);
	}

	rcu_read_lock();
	chanctx_conf = rcu_dereference(vif->chanctx_conf);
	if (chanctx_conf) {
		pos += scnprintf(buf+pos, bufsz-pos,
				 "idle rx chains %d, active rx chains: %d\n",
				 chanctx_conf->rx_chains_static,
				 chanctx_conf->rx_chains_dynamic);
	}
	rcu_read_unlock();

	mutex_unlock(&mvm->mutex);

	return simple_read_from_buffer(user_buf, count, ppos, buf, pos);
}

#define BT_MBOX_MSG(_notif, _num, _field)				     \
	((le32_to_cpu((_notif)->mbox_msg[(_num)]) & BT_MBOX##_num##_##_field)\
	>> BT_MBOX##_num##_##_field##_POS)


#define BT_MBOX_PRINT(_num, _field, _end)				    \
			pos += scnprintf(buf + pos, bufsz - pos,	    \
					 "\t%s: %d%s",			    \
					 #_field,			    \
					 BT_MBOX_MSG(notif, _num, _field),  \
					 true ? "\n" : ", ");

static ssize_t iwl_dbgfs_bt_notif_read(struct file *file, char __user *user_buf,
				       size_t count, loff_t *ppos)
{
	struct iwl_mvm *mvm = file->private_data;
	struct iwl_bt_coex_profile_notif *notif = &mvm->last_bt_notif;
	char *buf;
	int ret, pos = 0, bufsz = sizeof(char) * 1024;

	buf = kmalloc(bufsz, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	mutex_lock(&mvm->mutex);

	pos += scnprintf(buf+pos, bufsz-pos, "MBOX dw0:\n");

	BT_MBOX_PRINT(0, LE_SLAVE_LAT, false);
	BT_MBOX_PRINT(0, LE_PROF1, false);
	BT_MBOX_PRINT(0, LE_PROF2, false);
	BT_MBOX_PRINT(0, LE_PROF_OTHER, false);
	BT_MBOX_PRINT(0, CHL_SEQ_N, false);
	BT_MBOX_PRINT(0, INBAND_S, false);
	BT_MBOX_PRINT(0, LE_MIN_RSSI, false);
	BT_MBOX_PRINT(0, LE_SCAN, false);
	BT_MBOX_PRINT(0, LE_ADV, false);
	BT_MBOX_PRINT(0, LE_MAX_TX_POWER, false);
	BT_MBOX_PRINT(0, OPEN_CON_1, true);

	pos += scnprintf(buf+pos, bufsz-pos, "MBOX dw1:\n");

	BT_MBOX_PRINT(1, BR_MAX_TX_POWER, false);
	BT_MBOX_PRINT(1, IP_SR, false);
	BT_MBOX_PRINT(1, LE_MSTR, false);
	BT_MBOX_PRINT(1, AGGR_TRFC_LD, false);
	BT_MBOX_PRINT(1, MSG_TYPE, false);
	BT_MBOX_PRINT(1, SSN, true);

	pos += scnprintf(buf+pos, bufsz-pos, "MBOX dw2:\n");

	BT_MBOX_PRINT(2, SNIFF_ACT, false);
	BT_MBOX_PRINT(2, PAG, false);
	BT_MBOX_PRINT(2, INQUIRY, false);
	BT_MBOX_PRINT(2, CONN, false);
	BT_MBOX_PRINT(2, SNIFF_INTERVAL, false);
	BT_MBOX_PRINT(2, DISC, false);
	BT_MBOX_PRINT(2, SCO_TX_ACT, false);
	BT_MBOX_PRINT(2, SCO_RX_ACT, false);
	BT_MBOX_PRINT(2, ESCO_RE_TX, false);
	BT_MBOX_PRINT(2, SCO_DURATION, true);

	pos += scnprintf(buf+pos, bufsz-pos, "MBOX dw3:\n");

	BT_MBOX_PRINT(3, SCO_STATE, false);
	BT_MBOX_PRINT(3, SNIFF_STATE, false);
	BT_MBOX_PRINT(3, A2DP_STATE, false);
	BT_MBOX_PRINT(3, ACL_STATE, false);
	BT_MBOX_PRINT(3, MSTR_STATE, false);
	BT_MBOX_PRINT(3, OBX_STATE, false);
	BT_MBOX_PRINT(3, OPEN_CON_2, false);
	BT_MBOX_PRINT(3, TRAFFIC_LOAD, false);
	BT_MBOX_PRINT(3, CHL_SEQN_LSB, false);
	BT_MBOX_PRINT(3, INBAND_P, false);
	BT_MBOX_PRINT(3, MSG_TYPE_2, false);
	BT_MBOX_PRINT(3, SSN_2, false);
	BT_MBOX_PRINT(3, UPDATE_REQUEST, true);

	pos += scnprintf(buf+pos, bufsz-pos, "bt_status = %d\n",
			 notif->bt_status);
	pos += scnprintf(buf+pos, bufsz-pos, "bt_open_conn = %d\n",
			 notif->bt_open_conn);
	pos += scnprintf(buf+pos, bufsz-pos, "bt_traffic_load = %d\n",
			 notif->bt_traffic_load);
	pos += scnprintf(buf+pos, bufsz-pos, "bt_agg_traffic_load = %d\n",
			 notif->bt_agg_traffic_load);
	pos += scnprintf(buf+pos, bufsz-pos, "bt_ci_compliance = %d\n",
			 notif->bt_ci_compliance);
	pos += scnprintf(buf+pos, bufsz-pos, "primary_ch_lut = %d\n",
			 le32_to_cpu(notif->primary_ch_lut));
	pos += scnprintf(buf+pos, bufsz-pos, "secondary_ch_lut = %d\n",
			 le32_to_cpu(notif->secondary_ch_lut));
	pos += scnprintf(buf+pos, bufsz-pos, "bt_activity_grading = %d\n",
			 le32_to_cpu(notif->bt_activity_grading));

	mutex_unlock(&mvm->mutex);

	ret = simple_read_from_buffer(user_buf, count, ppos, buf, pos);
	kfree(buf);

	return ret;
}
#undef BT_MBOX_PRINT

static ssize_t iwl_dbgfs_bt_cmd_read(struct file *file, char __user *user_buf,
				     size_t count, loff_t *ppos)
{
	struct iwl_mvm *mvm = file->private_data;
	struct iwl_bt_coex_ci_cmd *cmd = &mvm->last_bt_ci_cmd;
	char buf[256];
	int bufsz = sizeof(buf);
	int pos = 0;

	mutex_lock(&mvm->mutex);

	pos += scnprintf(buf+pos, bufsz-pos, "Channel inhibition CMD\n");
	pos += scnprintf(buf+pos, bufsz-pos,
		       "\tPrimary Channel Bitmap 0x%016llx Fat: %d\n",
		       le64_to_cpu(cmd->bt_primary_ci),
		       !!cmd->co_run_bw_primary);
	pos += scnprintf(buf+pos, bufsz-pos,
		       "\tSecondary Channel Bitmap 0x%016llx Fat: %d\n",
		       le64_to_cpu(cmd->bt_secondary_ci),
		       !!cmd->co_run_bw_secondary);

	pos += scnprintf(buf+pos, bufsz-pos, "BT Configuration CMD\n");
	pos += scnprintf(buf+pos, bufsz-pos, "\tACK Kill Mask 0x%08x\n",
			 iwl_bt_ack_kill_msk[mvm->bt_kill_msk]);
	pos += scnprintf(buf+pos, bufsz-pos, "\tCTS Kill Mask 0x%08x\n",
			 iwl_bt_cts_kill_msk[mvm->bt_kill_msk]);

	mutex_unlock(&mvm->mutex);

	return simple_read_from_buffer(user_buf, count, ppos, buf, pos);
}

#define PRINT_STATS_LE32(_str, _val)					\
			 pos += scnprintf(buf + pos, bufsz - pos,	\
					  fmt_table, _str,		\
					  le32_to_cpu(_val))

static ssize_t iwl_dbgfs_fw_rx_stats_read(struct file *file,
					  char __user *user_buf, size_t count,
					  loff_t *ppos)
{
	struct iwl_mvm *mvm = file->private_data;
	static const char *fmt_table = "\t%-30s %10u\n";
	static const char *fmt_header = "%-32s\n";
	int pos = 0;
	char *buf;
	int ret;
	/* 43 is the size of each data line, 33 is the size of each header */
	size_t bufsz =
		((sizeof(struct mvm_statistics_rx) / sizeof(__le32)) * 43) +
		(4 * 33) + 1;

	struct mvm_statistics_rx_phy *ofdm;
	struct mvm_statistics_rx_phy *cck;
	struct mvm_statistics_rx_non_phy *general;
	struct mvm_statistics_rx_ht_phy *ht;

	buf = kzalloc(bufsz, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	mutex_lock(&mvm->mutex);

	ofdm = &mvm->rx_stats.ofdm;
	cck = &mvm->rx_stats.cck;
	general = &mvm->rx_stats.general;
	ht = &mvm->rx_stats.ofdm_ht;

	pos += scnprintf(buf + pos, bufsz - pos, fmt_header,
			 "Statistics_Rx - OFDM");
	PRINT_STATS_LE32("ina_cnt", ofdm->ina_cnt);
	PRINT_STATS_LE32("fina_cnt", ofdm->fina_cnt);
	PRINT_STATS_LE32("plcp_err", ofdm->plcp_err);
	PRINT_STATS_LE32("crc32_err", ofdm->crc32_err);
	PRINT_STATS_LE32("overrun_err", ofdm->overrun_err);
	PRINT_STATS_LE32("early_overrun_err", ofdm->early_overrun_err);
	PRINT_STATS_LE32("crc32_good", ofdm->crc32_good);
	PRINT_STATS_LE32("false_alarm_cnt", ofdm->false_alarm_cnt);
	PRINT_STATS_LE32("fina_sync_err_cnt", ofdm->fina_sync_err_cnt);
	PRINT_STATS_LE32("sfd_timeout", ofdm->sfd_timeout);
	PRINT_STATS_LE32("fina_timeout", ofdm->fina_timeout);
	PRINT_STATS_LE32("unresponded_rts", ofdm->unresponded_rts);
	PRINT_STATS_LE32("rxe_frame_lmt_overrun",
			 ofdm->rxe_frame_limit_overrun);
	PRINT_STATS_LE32("sent_ack_cnt", ofdm->sent_ack_cnt);
	PRINT_STATS_LE32("sent_cts_cnt", ofdm->sent_cts_cnt);
	PRINT_STATS_LE32("sent_ba_rsp_cnt", ofdm->sent_ba_rsp_cnt);
	PRINT_STATS_LE32("dsp_self_kill", ofdm->dsp_self_kill);
	PRINT_STATS_LE32("mh_format_err", ofdm->mh_format_err);
	PRINT_STATS_LE32("re_acq_main_rssi_sum", ofdm->re_acq_main_rssi_sum);
	PRINT_STATS_LE32("reserved", ofdm->reserved);

	pos += scnprintf(buf + pos, bufsz - pos, fmt_header,
			 "Statistics_Rx - CCK");
	PRINT_STATS_LE32("ina_cnt", cck->ina_cnt);
	PRINT_STATS_LE32("fina_cnt", cck->fina_cnt);
	PRINT_STATS_LE32("plcp_err", cck->plcp_err);
	PRINT_STATS_LE32("crc32_err", cck->crc32_err);
	PRINT_STATS_LE32("overrun_err", cck->overrun_err);
	PRINT_STATS_LE32("early_overrun_err", cck->early_overrun_err);
	PRINT_STATS_LE32("crc32_good", cck->crc32_good);
	PRINT_STATS_LE32("false_alarm_cnt", cck->false_alarm_cnt);
	PRINT_STATS_LE32("fina_sync_err_cnt", cck->fina_sync_err_cnt);
	PRINT_STATS_LE32("sfd_timeout", cck->sfd_timeout);
	PRINT_STATS_LE32("fina_timeout", cck->fina_timeout);
	PRINT_STATS_LE32("unresponded_rts", cck->unresponded_rts);
	PRINT_STATS_LE32("rxe_frame_lmt_overrun",
			 cck->rxe_frame_limit_overrun);
	PRINT_STATS_LE32("sent_ack_cnt", cck->sent_ack_cnt);
	PRINT_STATS_LE32("sent_cts_cnt", cck->sent_cts_cnt);
	PRINT_STATS_LE32("sent_ba_rsp_cnt", cck->sent_ba_rsp_cnt);
	PRINT_STATS_LE32("dsp_self_kill", cck->dsp_self_kill);
	PRINT_STATS_LE32("mh_format_err", cck->mh_format_err);
	PRINT_STATS_LE32("re_acq_main_rssi_sum", cck->re_acq_main_rssi_sum);
	PRINT_STATS_LE32("reserved", cck->reserved);

	pos += scnprintf(buf + pos, bufsz - pos, fmt_header,
			 "Statistics_Rx - GENERAL");
	PRINT_STATS_LE32("bogus_cts", general->bogus_cts);
	PRINT_STATS_LE32("bogus_ack", general->bogus_ack);
	PRINT_STATS_LE32("non_bssid_frames", general->non_bssid_frames);
	PRINT_STATS_LE32("filtered_frames", general->filtered_frames);
	PRINT_STATS_LE32("non_channel_beacons", general->non_channel_beacons);
	PRINT_STATS_LE32("channel_beacons", general->channel_beacons);
	PRINT_STATS_LE32("num_missed_bcon", general->num_missed_bcon);
	PRINT_STATS_LE32("adc_rx_saturation_time",
			 general->adc_rx_saturation_time);
	PRINT_STATS_LE32("ina_detection_search_time",
			 general->ina_detection_search_time);
	PRINT_STATS_LE32("beacon_silence_rssi_a",
			 general->beacon_silence_rssi_a);
	PRINT_STATS_LE32("beacon_silence_rssi_b",
			 general->beacon_silence_rssi_b);
	PRINT_STATS_LE32("beacon_silence_rssi_c",
			 general->beacon_silence_rssi_c);
	PRINT_STATS_LE32("interference_data_flag",
			 general->interference_data_flag);
	PRINT_STATS_LE32("channel_load", general->channel_load);
	PRINT_STATS_LE32("dsp_false_alarms", general->dsp_false_alarms);
	PRINT_STATS_LE32("beacon_rssi_a", general->beacon_rssi_a);
	PRINT_STATS_LE32("beacon_rssi_b", general->beacon_rssi_b);
	PRINT_STATS_LE32("beacon_rssi_c", general->beacon_rssi_c);
	PRINT_STATS_LE32("beacon_energy_a", general->beacon_energy_a);
	PRINT_STATS_LE32("beacon_energy_b", general->beacon_energy_b);
	PRINT_STATS_LE32("beacon_energy_c", general->beacon_energy_c);
	PRINT_STATS_LE32("num_bt_kills", general->num_bt_kills);
	PRINT_STATS_LE32("mac_id", general->mac_id);
	PRINT_STATS_LE32("directed_data_mpdu", general->directed_data_mpdu);

	pos += scnprintf(buf + pos, bufsz - pos, fmt_header,
			 "Statistics_Rx - HT");
	PRINT_STATS_LE32("plcp_err", ht->plcp_err);
	PRINT_STATS_LE32("overrun_err", ht->overrun_err);
	PRINT_STATS_LE32("early_overrun_err", ht->early_overrun_err);
	PRINT_STATS_LE32("crc32_good", ht->crc32_good);
	PRINT_STATS_LE32("crc32_err", ht->crc32_err);
	PRINT_STATS_LE32("mh_format_err", ht->mh_format_err);
	PRINT_STATS_LE32("agg_crc32_good", ht->agg_crc32_good);
	PRINT_STATS_LE32("agg_mpdu_cnt", ht->agg_mpdu_cnt);
	PRINT_STATS_LE32("agg_cnt", ht->agg_cnt);
	PRINT_STATS_LE32("unsupport_mcs", ht->unsupport_mcs);

	mutex_unlock(&mvm->mutex);

	ret = simple_read_from_buffer(user_buf, count, ppos, buf, pos);
	kfree(buf);

	return ret;
}
#undef PRINT_STAT_LE32

static ssize_t iwl_dbgfs_fw_restart_write(struct file *file,
					  const char __user *user_buf,
					  size_t count, loff_t *ppos)
{
	struct iwl_mvm *mvm = file->private_data;
	int ret;

	mutex_lock(&mvm->mutex);

	/* allow one more restart that we're provoking here */
	if (mvm->restart_fw >= 0)
		mvm->restart_fw++;

	/* take the return value to make compiler happy - it will fail anyway */
	ret = iwl_mvm_send_cmd_pdu(mvm, REPLY_ERROR, CMD_SYNC, 0, NULL);

	mutex_unlock(&mvm->mutex);

	return count;
}

static ssize_t
iwl_dbgfs_scan_ant_rxchain_read(struct file *file,
				char __user *user_buf,
				size_t count, loff_t *ppos)
{
	struct iwl_mvm *mvm = file->private_data;
	int pos = 0;
	char buf[32];
	const size_t bufsz = sizeof(buf);

	/* print which antennas were set for the scan command by the user */
	pos += scnprintf(buf + pos, bufsz - pos, "Antennas for scan: ");
	if (mvm->scan_rx_ant & ANT_A)
		pos += scnprintf(buf + pos, bufsz - pos, "A");
	if (mvm->scan_rx_ant & ANT_B)
		pos += scnprintf(buf + pos, bufsz - pos, "B");
	if (mvm->scan_rx_ant & ANT_C)
		pos += scnprintf(buf + pos, bufsz - pos, "C");
	pos += scnprintf(buf + pos, bufsz - pos, " (%hhx)\n", mvm->scan_rx_ant);

	return simple_read_from_buffer(user_buf, count, ppos, buf, pos);
}

static ssize_t
iwl_dbgfs_scan_ant_rxchain_write(struct file *file,
				 const char __user *user_buf,
				 size_t count, loff_t *ppos)
{
	struct iwl_mvm *mvm = file->private_data;
	char buf[8];
	int buf_size;
	u8 scan_rx_ant;

	memset(buf, 0, sizeof(buf));
	buf_size = min(count, sizeof(buf) - 1);

	/* get the argument from the user and check if it is valid */
	if (copy_from_user(buf, user_buf, buf_size))
		return -EFAULT;
	if (sscanf(buf, "%hhx", &scan_rx_ant) != 1)
		return -EINVAL;
	if (scan_rx_ant > ANT_ABC)
		return -EINVAL;
	if (scan_rx_ant & ~iwl_fw_valid_rx_ant(mvm->fw))
		return -EINVAL;

	/* change the rx antennas for scan command */
	mvm->scan_rx_ant = scan_rx_ant;

	return count;
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

static ssize_t iwl_dbgfs_bf_params_write(struct file *file,
					 const char __user *user_buf,
					 size_t count, loff_t *ppos)
{
	struct ieee80211_vif *vif = file->private_data;
	struct iwl_mvm_vif *mvmvif = iwl_mvm_vif_from_mac80211(vif);
	struct iwl_mvm *mvm = mvmvif->dbgfs_data;
	enum iwl_dbgfs_bf_mask param;
	char buf[256];
	int buf_size;
	int value;
	int ret = 0;

	memset(buf, 0, sizeof(buf));
	buf_size = min(count, sizeof(buf) - 1);
	if (copy_from_user(buf, user_buf, buf_size))
		return -EFAULT;

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
	if (param == MVM_DEBUGFS_BF_ENABLE_BEACON_FILTER && !value) {
		ret = iwl_mvm_disable_beacon_filter(mvm, vif);
	} else {
		ret = iwl_mvm_enable_beacon_filter(mvm, vif);
	}
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

#ifdef CONFIG_PM_SLEEP
static ssize_t iwl_dbgfs_d3_sram_write(struct file *file,
				       const char __user *user_buf,
				       size_t count, loff_t *ppos)
{
	struct iwl_mvm *mvm = file->private_data;
	char buf[8] = {};
	int store;

	count = min_t(size_t, count, sizeof(buf) - 1);
	if (copy_from_user(buf, user_buf, count))
		return -EFAULT;

	if (sscanf(buf, "%d", &store) != 1)
		return -EINVAL;

	mvm->store_d3_resume_sram = store;

	return count;
}

static ssize_t iwl_dbgfs_d3_sram_read(struct file *file, char __user *user_buf,
				      size_t count, loff_t *ppos)
{
	struct iwl_mvm *mvm = file->private_data;
	const struct fw_img *img;
	int ofs, len, pos = 0;
	size_t bufsz, ret;
	char *buf;
	u8 *ptr = mvm->d3_resume_sram;

	img = &mvm->fw->img[IWL_UCODE_WOWLAN];
	len = img->sec[IWL_UCODE_SECTION_DATA].len;

	bufsz = len * 4 + 256;
	buf = kzalloc(bufsz, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	pos += scnprintf(buf, bufsz, "D3 SRAM capture: %sabled\n",
			 mvm->store_d3_resume_sram ? "en" : "dis");

	if (ptr) {
		for (ofs = 0; ofs < len; ofs += 16) {
			pos += scnprintf(buf + pos, bufsz - pos,
					 "0x%.4x ", ofs);
			hex_dump_to_buffer(ptr + ofs, 16, 16, 1, buf + pos,
					   bufsz - pos, false);
			pos += strlen(buf + pos);
			if (bufsz - pos > 0)
				buf[pos++] = '\n';
		}
	} else {
		pos += scnprintf(buf + pos, bufsz - pos,
				 "(no data captured)\n");
	}

	ret = simple_read_from_buffer(user_buf, count, ppos, buf, pos);

	kfree(buf);

	return ret;
}
#endif

#define MVM_DEBUGFS_READ_FILE_OPS(name)					\
static const struct file_operations iwl_dbgfs_##name##_ops = {	\
	.read = iwl_dbgfs_##name##_read,				\
	.open = simple_open,						\
	.llseek = generic_file_llseek,					\
}

#define MVM_DEBUGFS_READ_WRITE_FILE_OPS(name)				\
static const struct file_operations iwl_dbgfs_##name##_ops = {	\
	.write = iwl_dbgfs_##name##_write,				\
	.read = iwl_dbgfs_##name##_read,				\
	.open = simple_open,						\
	.llseek = generic_file_llseek,					\
};

#define MVM_DEBUGFS_WRITE_FILE_OPS(name)				\
static const struct file_operations iwl_dbgfs_##name##_ops = {	\
	.write = iwl_dbgfs_##name##_write,				\
	.open = simple_open,						\
	.llseek = generic_file_llseek,					\
};

#define MVM_DEBUGFS_ADD_FILE(name, parent, mode) do {			\
		if (!debugfs_create_file(#name, mode, parent, mvm,	\
					 &iwl_dbgfs_##name##_ops))	\
			goto err;					\
	} while (0)

#define MVM_DEBUGFS_ADD_FILE_VIF(name, parent, mode) do {		\
		if (!debugfs_create_file(#name, mode, parent, vif,	\
					 &iwl_dbgfs_##name##_ops))	\
			goto err;					\
	} while (0)

/* Device wide debugfs entries */
MVM_DEBUGFS_WRITE_FILE_OPS(tx_flush);
MVM_DEBUGFS_WRITE_FILE_OPS(sta_drain);
MVM_DEBUGFS_READ_WRITE_FILE_OPS(sram);
MVM_DEBUGFS_READ_FILE_OPS(stations);
MVM_DEBUGFS_READ_FILE_OPS(bt_notif);
MVM_DEBUGFS_READ_FILE_OPS(bt_cmd);
MVM_DEBUGFS_READ_WRITE_FILE_OPS(disable_power_off);
MVM_DEBUGFS_READ_FILE_OPS(fw_rx_stats);
MVM_DEBUGFS_WRITE_FILE_OPS(fw_restart);
MVM_DEBUGFS_READ_WRITE_FILE_OPS(scan_ant_rxchain);

#ifdef CONFIG_PM_SLEEP
MVM_DEBUGFS_READ_WRITE_FILE_OPS(d3_sram);
#endif

/* Interface specific debugfs entries */
MVM_DEBUGFS_READ_FILE_OPS(mac_params);
MVM_DEBUGFS_READ_WRITE_FILE_OPS(pm_params);
MVM_DEBUGFS_READ_WRITE_FILE_OPS(bf_params);

int iwl_mvm_dbgfs_register(struct iwl_mvm *mvm, struct dentry *dbgfs_dir)
{
	char buf[100];

	mvm->debugfs_dir = dbgfs_dir;

	MVM_DEBUGFS_ADD_FILE(tx_flush, mvm->debugfs_dir, S_IWUSR);
	MVM_DEBUGFS_ADD_FILE(sta_drain, mvm->debugfs_dir, S_IWUSR);
	MVM_DEBUGFS_ADD_FILE(sram, mvm->debugfs_dir, S_IWUSR | S_IRUSR);
	MVM_DEBUGFS_ADD_FILE(stations, dbgfs_dir, S_IRUSR);
	MVM_DEBUGFS_ADD_FILE(bt_notif, dbgfs_dir, S_IRUSR);
	MVM_DEBUGFS_ADD_FILE(bt_cmd, dbgfs_dir, S_IRUSR);
	if (mvm->fw->ucode_capa.flags & IWL_UCODE_TLV_FLAGS_DEVICE_PS_CMD)
		MVM_DEBUGFS_ADD_FILE(disable_power_off, mvm->debugfs_dir,
				     S_IRUSR | S_IWUSR);
	MVM_DEBUGFS_ADD_FILE(fw_rx_stats, mvm->debugfs_dir, S_IRUSR);
	MVM_DEBUGFS_ADD_FILE(fw_restart, mvm->debugfs_dir, S_IWUSR);
	MVM_DEBUGFS_ADD_FILE(scan_ant_rxchain, mvm->debugfs_dir,
			     S_IWUSR | S_IRUSR);
#ifdef CONFIG_PM_SLEEP
	MVM_DEBUGFS_ADD_FILE(d3_sram, mvm->debugfs_dir, S_IRUSR | S_IWUSR);
	MVM_DEBUGFS_ADD_FILE(d3_test, mvm->debugfs_dir, S_IRUSR);
	if (!debugfs_create_bool("d3_wake_sysassert", S_IRUSR | S_IWUSR,
				 mvm->debugfs_dir, &mvm->d3_wake_sysassert))
		goto err;
#endif

	/*
	 * Create a symlink with mac80211. It will be removed when mac80211
	 * exists (before the opmode exists which removes the target.)
	 */
	snprintf(buf, 100, "../../%s/%s",
		 dbgfs_dir->d_parent->d_parent->d_name.name,
		 dbgfs_dir->d_parent->d_name.name);
	if (!debugfs_create_symlink("iwlwifi", mvm->hw->wiphy->debugfsdir, buf))
		goto err;

	return 0;
err:
	IWL_ERR(mvm, "Can't create the mvm debugfs directory\n");
	return -ENOMEM;
}

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
	mvmvif->dbgfs_data = mvm;

	if (!mvmvif->dbgfs_dir) {
		IWL_ERR(mvm, "Failed to create debugfs directory under %s\n",
			dbgfs_dir->d_name.name);
		return;
	}

	if (iwlmvm_mod_params.power_scheme != IWL_POWER_SCHEME_CAM &&
	    vif->type == NL80211_IFTYPE_STATION && !vif->p2p)
		MVM_DEBUGFS_ADD_FILE_VIF(pm_params, mvmvif->dbgfs_dir, S_IWUSR |
					 S_IRUSR);

	MVM_DEBUGFS_ADD_FILE_VIF(mac_params, mvmvif->dbgfs_dir,
				 S_IRUSR);

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
