/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2012 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2015 Intel Mobile Communications GmbH
 * Copyright(c) 2016 - 2017 Intel Deutschland GmbH
 * Copyright(c) 2018 - 2019 Intel Corporation
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
 * Copyright(c) 2016 - 2017 Intel Deutschland GmbH
 * Copyright(c) 2018 - 2019 Intel Corporation
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
#include <linux/vmalloc.h>
#include <linux/ieee80211.h>
#include <linux/netdevice.h>

#include "mvm.h"
#include "sta.h"
#include "iwl-io.h"
#include "debugfs.h"
#include "iwl-modparams.h"
#include "fw/error-dump.h"

static ssize_t iwl_dbgfs_ctdp_budget_read(struct file *file,
					  char __user *user_buf,
					  size_t count, loff_t *ppos)
{
	struct iwl_mvm *mvm = file->private_data;
	char buf[16];
	int pos, budget;

	if (!iwl_mvm_is_ctdp_supported(mvm))
		return -EOPNOTSUPP;

	if (!iwl_mvm_firmware_running(mvm) ||
	    mvm->fwrt.cur_fw_img != IWL_UCODE_REGULAR)
		return -EIO;

	mutex_lock(&mvm->mutex);
	budget = iwl_mvm_ctdp_command(mvm, CTDP_CMD_OPERATION_REPORT, 0);
	mutex_unlock(&mvm->mutex);

	if (budget < 0)
		return budget;

	pos = scnprintf(buf, sizeof(buf), "%d\n", budget);

	return simple_read_from_buffer(user_buf, count, ppos, buf, pos);
}

static ssize_t iwl_dbgfs_stop_ctdp_write(struct iwl_mvm *mvm, char *buf,
					 size_t count, loff_t *ppos)
{
	int ret;

	if (!iwl_mvm_is_ctdp_supported(mvm))
		return -EOPNOTSUPP;

	if (!iwl_mvm_firmware_running(mvm) ||
	    mvm->fwrt.cur_fw_img != IWL_UCODE_REGULAR)
		return -EIO;

	mutex_lock(&mvm->mutex);
	ret = iwl_mvm_ctdp_command(mvm, CTDP_CMD_OPERATION_STOP, 0);
	mutex_unlock(&mvm->mutex);

	return ret ?: count;
}

static ssize_t iwl_dbgfs_force_ctkill_write(struct iwl_mvm *mvm, char *buf,
					    size_t count, loff_t *ppos)
{
	if (!iwl_mvm_firmware_running(mvm) ||
	    mvm->fwrt.cur_fw_img != IWL_UCODE_REGULAR)
		return -EIO;

	iwl_mvm_enter_ctkill(mvm);

	return count;
}

static ssize_t iwl_dbgfs_tx_flush_write(struct iwl_mvm *mvm, char *buf,
					size_t count, loff_t *ppos)
{
	int ret;
	u32 flush_arg;

	if (!iwl_mvm_firmware_running(mvm) ||
	    mvm->fwrt.cur_fw_img != IWL_UCODE_REGULAR)
		return -EIO;

	if (kstrtou32(buf, 0, &flush_arg))
		return -EINVAL;

	if (iwl_mvm_has_new_tx_api(mvm)) {
		IWL_DEBUG_TX_QUEUES(mvm,
				    "FLUSHING all tids queues on sta_id = %d\n",
				    flush_arg);
		mutex_lock(&mvm->mutex);
		ret = iwl_mvm_flush_sta_tids(mvm, flush_arg, 0xFFFF, 0)
			? : count;
		mutex_unlock(&mvm->mutex);
		return ret;
	}

	IWL_DEBUG_TX_QUEUES(mvm, "FLUSHING queues mask to flush = 0x%x\n",
			    flush_arg);

	mutex_lock(&mvm->mutex);
	ret =  iwl_mvm_flush_tx_path(mvm, flush_arg, 0) ? : count;
	mutex_unlock(&mvm->mutex);

	return ret;
}

static ssize_t iwl_dbgfs_sta_drain_write(struct iwl_mvm *mvm, char *buf,
					 size_t count, loff_t *ppos)
{
	struct iwl_mvm_sta *mvmsta;
	int sta_id, drain, ret;

	if (!iwl_mvm_firmware_running(mvm) ||
	    mvm->fwrt.cur_fw_img != IWL_UCODE_REGULAR)
		return -EIO;

	if (sscanf(buf, "%d %d", &sta_id, &drain) != 2)
		return -EINVAL;
	if (sta_id < 0 || sta_id >= IWL_MVM_STATION_COUNT)
		return -EINVAL;
	if (drain < 0 || drain > 1)
		return -EINVAL;

	mutex_lock(&mvm->mutex);

	mvmsta = iwl_mvm_sta_from_staid_protected(mvm, sta_id);

	if (!mvmsta)
		ret = -ENOENT;
	else
		ret = iwl_mvm_drain_sta(mvm, mvmsta, drain) ? : count;

	mutex_unlock(&mvm->mutex);

	return ret;
}

static ssize_t iwl_dbgfs_sram_read(struct file *file, char __user *user_buf,
				   size_t count, loff_t *ppos)
{
	struct iwl_mvm *mvm = file->private_data;
	const struct fw_img *img;
	unsigned int ofs, len;
	size_t ret;
	u8 *ptr;

	if (!iwl_mvm_firmware_running(mvm))
		return -EINVAL;

	/* default is to dump the entire data segment */
	img = &mvm->fw->img[mvm->fwrt.cur_fw_img];
	ofs = img->sec[IWL_UCODE_SECTION_DATA].offset;
	len = img->sec[IWL_UCODE_SECTION_DATA].len;

	if (mvm->dbgfs_sram_len) {
		ofs = mvm->dbgfs_sram_offset;
		len = mvm->dbgfs_sram_len;
	}

	ptr = kzalloc(len, GFP_KERNEL);
	if (!ptr)
		return -ENOMEM;

	iwl_trans_read_mem_bytes(mvm->trans, ofs, ptr, len);

	ret = simple_read_from_buffer(user_buf, count, ppos, ptr, len);

	kfree(ptr);

	return ret;
}

static ssize_t iwl_dbgfs_sram_write(struct iwl_mvm *mvm, char *buf,
				    size_t count, loff_t *ppos)
{
	const struct fw_img *img;
	u32 offset, len;
	u32 img_offset, img_len;

	if (!iwl_mvm_firmware_running(mvm))
		return -EINVAL;

	img = &mvm->fw->img[mvm->fwrt.cur_fw_img];
	img_offset = img->sec[IWL_UCODE_SECTION_DATA].offset;
	img_len = img->sec[IWL_UCODE_SECTION_DATA].len;

	if (sscanf(buf, "%x,%x", &offset, &len) == 2) {
		if ((offset & 0x3) || (len & 0x3))
			return -EINVAL;

		if (offset + len > img_offset + img_len)
			return -EINVAL;

		mvm->dbgfs_sram_offset = offset;
		mvm->dbgfs_sram_len = len;
	} else {
		mvm->dbgfs_sram_offset = 0;
		mvm->dbgfs_sram_len = 0;
	}

	return count;
}

static ssize_t iwl_dbgfs_set_nic_temperature_read(struct file *file,
						  char __user *user_buf,
						  size_t count, loff_t *ppos)
{
	struct iwl_mvm *mvm = file->private_data;
	char buf[16];
	int pos;

	if (!mvm->temperature_test)
		pos = scnprintf(buf , sizeof(buf), "disabled\n");
	else
		pos = scnprintf(buf , sizeof(buf), "%d\n", mvm->temperature);

	return simple_read_from_buffer(user_buf, count, ppos, buf, pos);
}

/*
 * Set NIC Temperature
 * Cause the driver to ignore the actual NIC temperature reported by the FW
 * Enable: any value between IWL_MVM_DEBUG_SET_TEMPERATURE_MIN -
 * IWL_MVM_DEBUG_SET_TEMPERATURE_MAX
 * Disable: IWL_MVM_DEBUG_SET_TEMPERATURE_DISABLE
 */
static ssize_t iwl_dbgfs_set_nic_temperature_write(struct iwl_mvm *mvm,
						   char *buf, size_t count,
						   loff_t *ppos)
{
	int temperature;

	if (!iwl_mvm_firmware_running(mvm) && !mvm->temperature_test)
		return -EIO;

	if (kstrtoint(buf, 10, &temperature))
		return -EINVAL;
	/* not a legal temperature */
	if ((temperature > IWL_MVM_DEBUG_SET_TEMPERATURE_MAX &&
	     temperature != IWL_MVM_DEBUG_SET_TEMPERATURE_DISABLE) ||
	    temperature < IWL_MVM_DEBUG_SET_TEMPERATURE_MIN)
		return -EINVAL;

	mutex_lock(&mvm->mutex);
	if (temperature == IWL_MVM_DEBUG_SET_TEMPERATURE_DISABLE) {
		if (!mvm->temperature_test)
			goto out;

		mvm->temperature_test = false;
		/* Since we can't read the temp while awake, just set
		 * it to zero until we get the next RX stats from the
		 * firmware.
		 */
		mvm->temperature = 0;
	} else {
		mvm->temperature_test = true;
		mvm->temperature = temperature;
	}
	IWL_DEBUG_TEMP(mvm, "%sabling debug set temperature (temp = %d)\n",
		       mvm->temperature_test ? "En" : "Dis" ,
		       mvm->temperature);
	/* handle the temperature change */
	iwl_mvm_tt_handler(mvm);

out:
	mutex_unlock(&mvm->mutex);

	return count;
}

static ssize_t iwl_dbgfs_nic_temp_read(struct file *file,
				       char __user *user_buf,
				       size_t count, loff_t *ppos)
{
	struct iwl_mvm *mvm = file->private_data;
	char buf[16];
	int pos, ret;
	s32 temp;

	if (!iwl_mvm_firmware_running(mvm))
		return -EIO;

	mutex_lock(&mvm->mutex);
	ret = iwl_mvm_get_temp(mvm, &temp);
	mutex_unlock(&mvm->mutex);

	if (ret)
		return -EIO;

	pos = scnprintf(buf , sizeof(buf), "%d\n", temp);

	return simple_read_from_buffer(user_buf, count, ppos, buf, pos);
}

#ifdef CONFIG_ACPI
static ssize_t iwl_dbgfs_sar_geo_profile_read(struct file *file,
					      char __user *user_buf,
					      size_t count, loff_t *ppos)
{
	struct iwl_mvm *mvm = file->private_data;
	char buf[256];
	int pos = 0;
	int bufsz = sizeof(buf);
	int tbl_idx;
	u8 *value;

	if (!iwl_mvm_firmware_running(mvm))
		return -EIO;

	mutex_lock(&mvm->mutex);
	tbl_idx = iwl_mvm_get_sar_geo_profile(mvm);
	if (tbl_idx < 0) {
		mutex_unlock(&mvm->mutex);
		return tbl_idx;
	}

	if (!tbl_idx) {
		pos = scnprintf(buf, bufsz,
				"SAR geographic profile disabled\n");
	} else {
		value = &mvm->fwrt.geo_profiles[tbl_idx - 1].values[0];

		pos += scnprintf(buf + pos, bufsz - pos,
				 "Use geographic profile %d\n", tbl_idx);
		pos += scnprintf(buf + pos, bufsz - pos,
				 "2.4GHz:\n\tChain A offset: %hhd dBm\n\tChain B offset: %hhd dBm\n\tmax tx power: %hhd dBm\n",
				 value[1], value[2], value[0]);
		pos += scnprintf(buf + pos, bufsz - pos,
				 "5.2GHz:\n\tChain A offset: %hhd dBm\n\tChain B offset: %hhd dBm\n\tmax tx power: %hhd dBm\n",
				 value[4], value[5], value[3]);
	}
	mutex_unlock(&mvm->mutex);

	return simple_read_from_buffer(user_buf, count, ppos, buf, pos);
}
#endif

static ssize_t iwl_dbgfs_stations_read(struct file *file, char __user *user_buf,
				       size_t count, loff_t *ppos)
{
	struct iwl_mvm *mvm = file->private_data;
	struct ieee80211_sta *sta;
	char buf[400];
	int i, pos = 0, bufsz = sizeof(buf);

	mutex_lock(&mvm->mutex);

	for (i = 0; i < ARRAY_SIZE(mvm->fw_id_to_mac_id); i++) {
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

static ssize_t iwl_dbgfs_rs_data_read(struct file *file, char __user *user_buf,
				      size_t count, loff_t *ppos)
{
	struct ieee80211_sta *sta = file->private_data;
	struct iwl_mvm_sta *mvmsta = iwl_mvm_sta_from_mac80211(sta);
	struct iwl_lq_sta_rs_fw *lq_sta = &mvmsta->lq_sta.rs_fw;
	struct iwl_mvm *mvm = lq_sta->pers.drv;
	static const size_t bufsz = 2048;
	char *buff;
	int desc = 0;
	ssize_t ret;

	buff = kmalloc(bufsz, GFP_KERNEL);
	if (!buff)
		return -ENOMEM;

	mutex_lock(&mvm->mutex);

	desc += scnprintf(buff + desc, bufsz - desc, "sta_id %d\n",
			  lq_sta->pers.sta_id);
	desc += scnprintf(buff + desc, bufsz - desc,
			  "fixed rate 0x%X\n",
			  lq_sta->pers.dbg_fixed_rate);
	desc += scnprintf(buff + desc, bufsz - desc,
			  "A-MPDU size limit %d\n",
			  lq_sta->pers.dbg_agg_frame_count_lim);
	desc += scnprintf(buff + desc, bufsz - desc,
			  "valid_tx_ant %s%s%s\n",
		(iwl_mvm_get_valid_tx_ant(mvm) & ANT_A) ? "ANT_A," : "",
		(iwl_mvm_get_valid_tx_ant(mvm) & ANT_B) ? "ANT_B," : "",
		(iwl_mvm_get_valid_tx_ant(mvm) & ANT_C) ? "ANT_C" : "");
	desc += scnprintf(buff + desc, bufsz - desc,
			  "last tx rate=0x%X ",
			  lq_sta->last_rate_n_flags);

	desc += rs_pretty_print_rate(buff + desc, bufsz - desc,
				     lq_sta->last_rate_n_flags);
	mutex_unlock(&mvm->mutex);

	ret = simple_read_from_buffer(user_buf, count, ppos, buff, desc);
	kfree(buff);
	return ret;
}

static ssize_t iwl_dbgfs_amsdu_len_write(struct ieee80211_sta *sta,
					 char *buf, size_t count,
					 loff_t *ppos)
{
	struct iwl_mvm_sta *mvmsta = iwl_mvm_sta_from_mac80211(sta);
	int i;
	u16 amsdu_len;

	if (kstrtou16(buf, 0, &amsdu_len))
		return -EINVAL;

	if (amsdu_len) {
		mvmsta->orig_amsdu_len = sta->max_amsdu_len;
		sta->max_amsdu_len = amsdu_len;
		for (i = 0; i < ARRAY_SIZE(sta->max_tid_amsdu_len); i++)
			sta->max_tid_amsdu_len[i] = amsdu_len;
	} else {
		sta->max_amsdu_len = mvmsta->orig_amsdu_len;
		mvmsta->orig_amsdu_len = 0;
	}
	return count;
}

static ssize_t iwl_dbgfs_amsdu_len_read(struct file *file,
					char __user *user_buf,
					size_t count, loff_t *ppos)
{
	struct ieee80211_sta *sta = file->private_data;
	struct iwl_mvm_sta *mvmsta = iwl_mvm_sta_from_mac80211(sta);

	char buf[32];
	int pos;

	pos = scnprintf(buf, sizeof(buf), "current %d ", sta->max_amsdu_len);
	pos += scnprintf(buf + pos, sizeof(buf) - pos, "stored %d\n",
			 mvmsta->orig_amsdu_len);

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

static ssize_t iwl_dbgfs_disable_power_off_write(struct iwl_mvm *mvm, char *buf,
						 size_t count, loff_t *ppos)
{
	int ret, val;

	if (!iwl_mvm_firmware_running(mvm))
		return -EIO;

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
	ret = iwl_mvm_power_update_device(mvm);
	mutex_unlock(&mvm->mutex);

	return ret ?: count;
}

static
int iwl_mvm_coex_dump_mbox(struct iwl_bt_coex_profile_notif *notif, char *buf,
			   int pos, int bufsz)
{
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
	BT_MBOX_PRINT(3, A2DP_SRC, false);
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

	return pos;
}

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

	pos += iwl_mvm_coex_dump_mbox(notif, buf, pos, bufsz);

	pos += scnprintf(buf + pos, bufsz - pos, "bt_ci_compliance = %d\n",
			 notif->bt_ci_compliance);
	pos += scnprintf(buf + pos, bufsz - pos, "primary_ch_lut = %d\n",
			 le32_to_cpu(notif->primary_ch_lut));
	pos += scnprintf(buf + pos, bufsz - pos, "secondary_ch_lut = %d\n",
			 le32_to_cpu(notif->secondary_ch_lut));
	pos += scnprintf(buf + pos,
			 bufsz - pos, "bt_activity_grading = %d\n",
			 le32_to_cpu(notif->bt_activity_grading));
	pos += scnprintf(buf + pos, bufsz - pos, "bt_rrc = %d\n",
			 notif->rrc_status & 0xF);
	pos += scnprintf(buf + pos, bufsz - pos, "bt_ttc = %d\n",
			 notif->ttc_status & 0xF);

	pos += scnprintf(buf + pos, bufsz - pos, "sync_sco = %d\n",
			 IWL_MVM_BT_COEX_SYNC2SCO);
	pos += scnprintf(buf + pos, bufsz - pos, "mplut = %d\n",
			 IWL_MVM_BT_COEX_MPLUT);

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

	pos += scnprintf(buf + pos, bufsz - pos, "Channel inhibition CMD\n");
	pos += scnprintf(buf + pos, bufsz - pos,
			 "\tPrimary Channel Bitmap 0x%016llx\n",
			 le64_to_cpu(cmd->bt_primary_ci));
	pos += scnprintf(buf + pos, bufsz - pos,
			 "\tSecondary Channel Bitmap 0x%016llx\n",
			 le64_to_cpu(cmd->bt_secondary_ci));

	mutex_unlock(&mvm->mutex);

	return simple_read_from_buffer(user_buf, count, ppos, buf, pos);
}

static ssize_t
iwl_dbgfs_bt_tx_prio_write(struct iwl_mvm *mvm, char *buf,
			   size_t count, loff_t *ppos)
{
	u32 bt_tx_prio;

	if (sscanf(buf, "%u", &bt_tx_prio) != 1)
		return -EINVAL;
	if (bt_tx_prio > 4)
		return -EINVAL;

	mvm->bt_tx_prio = bt_tx_prio;

	return count;
}

static ssize_t
iwl_dbgfs_bt_force_ant_write(struct iwl_mvm *mvm, char *buf,
			     size_t count, loff_t *ppos)
{
	static const char * const modes_str[BT_FORCE_ANT_MAX] = {
		[BT_FORCE_ANT_DIS] = "dis",
		[BT_FORCE_ANT_AUTO] = "auto",
		[BT_FORCE_ANT_BT] = "bt",
		[BT_FORCE_ANT_WIFI] = "wifi",
	};
	int ret, bt_force_ant_mode;

	ret = match_string(modes_str, ARRAY_SIZE(modes_str), buf);
	if (ret < 0)
		return ret;

	bt_force_ant_mode = ret;
	ret = 0;
	mutex_lock(&mvm->mutex);
	if (mvm->bt_force_ant_mode == bt_force_ant_mode)
		goto out;

	mvm->bt_force_ant_mode = bt_force_ant_mode;
	IWL_DEBUG_COEX(mvm, "Force mode: %s\n",
		       modes_str[mvm->bt_force_ant_mode]);

	if (iwl_mvm_firmware_running(mvm))
		ret = iwl_mvm_send_bt_init_conf(mvm);
	else
		ret = 0;

out:
	mutex_unlock(&mvm->mutex);
	return ret ?: count;
}

static ssize_t iwl_dbgfs_fw_ver_read(struct file *file, char __user *user_buf,
				     size_t count, loff_t *ppos)
{
	struct iwl_mvm *mvm = file->private_data;
	char *buff, *pos, *endpos;
	static const size_t bufsz = 1024;
	int ret;

	buff = kmalloc(bufsz, GFP_KERNEL);
	if (!buff)
		return -ENOMEM;

	pos = buff;
	endpos = pos + bufsz;

	pos += scnprintf(pos, endpos - pos, "FW prefix: %s\n",
			 mvm->trans->cfg->fw_name_pre);
	pos += scnprintf(pos, endpos - pos, "FW: %s\n",
			 mvm->fwrt.fw->human_readable);
	pos += scnprintf(pos, endpos - pos, "Device: %s\n",
			 mvm->fwrt.trans->cfg->name);
	pos += scnprintf(pos, endpos - pos, "Bus: %s\n",
			 mvm->fwrt.dev->bus->name);

	ret = simple_read_from_buffer(user_buf, count, ppos, buff, pos - buff);
	kfree(buff);

	return ret;
}

#define PRINT_STATS_LE32(_struct, _memb)				\
			 pos += scnprintf(buf + pos, bufsz - pos,	\
					  fmt_table, #_memb,		\
					  le32_to_cpu(_struct->_memb))

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
	size_t bufsz;

	if (iwl_mvm_has_new_rx_stats_api(mvm))
		bufsz = ((sizeof(struct mvm_statistics_rx) /
			  sizeof(__le32)) * 43) + (4 * 33) + 1;
	else
		/* 43 = size of each data line; 33 = size of each header */
		bufsz = ((sizeof(struct mvm_statistics_rx_v3) /
			  sizeof(__le32)) * 43) + (4 * 33) + 1;

	buf = kzalloc(bufsz, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	mutex_lock(&mvm->mutex);

	if (iwl_mvm_firmware_running(mvm))
		iwl_mvm_request_statistics(mvm, false);

	pos += scnprintf(buf + pos, bufsz - pos, fmt_header,
			 "Statistics_Rx - OFDM");
	if (!iwl_mvm_has_new_rx_stats_api(mvm)) {
		struct mvm_statistics_rx_phy_v2 *ofdm = &mvm->rx_stats_v3.ofdm;

		PRINT_STATS_LE32(ofdm, ina_cnt);
		PRINT_STATS_LE32(ofdm, fina_cnt);
		PRINT_STATS_LE32(ofdm, plcp_err);
		PRINT_STATS_LE32(ofdm, crc32_err);
		PRINT_STATS_LE32(ofdm, overrun_err);
		PRINT_STATS_LE32(ofdm, early_overrun_err);
		PRINT_STATS_LE32(ofdm, crc32_good);
		PRINT_STATS_LE32(ofdm, false_alarm_cnt);
		PRINT_STATS_LE32(ofdm, fina_sync_err_cnt);
		PRINT_STATS_LE32(ofdm, sfd_timeout);
		PRINT_STATS_LE32(ofdm, fina_timeout);
		PRINT_STATS_LE32(ofdm, unresponded_rts);
		PRINT_STATS_LE32(ofdm, rxe_frame_lmt_overrun);
		PRINT_STATS_LE32(ofdm, sent_ack_cnt);
		PRINT_STATS_LE32(ofdm, sent_cts_cnt);
		PRINT_STATS_LE32(ofdm, sent_ba_rsp_cnt);
		PRINT_STATS_LE32(ofdm, dsp_self_kill);
		PRINT_STATS_LE32(ofdm, mh_format_err);
		PRINT_STATS_LE32(ofdm, re_acq_main_rssi_sum);
		PRINT_STATS_LE32(ofdm, reserved);
	} else {
		struct mvm_statistics_rx_phy *ofdm = &mvm->rx_stats.ofdm;

		PRINT_STATS_LE32(ofdm, unresponded_rts);
		PRINT_STATS_LE32(ofdm, rxe_frame_lmt_overrun);
		PRINT_STATS_LE32(ofdm, sent_ba_rsp_cnt);
		PRINT_STATS_LE32(ofdm, dsp_self_kill);
		PRINT_STATS_LE32(ofdm, reserved);
	}

	pos += scnprintf(buf + pos, bufsz - pos, fmt_header,
			 "Statistics_Rx - CCK");
	if (!iwl_mvm_has_new_rx_stats_api(mvm)) {
		struct mvm_statistics_rx_phy_v2 *cck = &mvm->rx_stats_v3.cck;

		PRINT_STATS_LE32(cck, ina_cnt);
		PRINT_STATS_LE32(cck, fina_cnt);
		PRINT_STATS_LE32(cck, plcp_err);
		PRINT_STATS_LE32(cck, crc32_err);
		PRINT_STATS_LE32(cck, overrun_err);
		PRINT_STATS_LE32(cck, early_overrun_err);
		PRINT_STATS_LE32(cck, crc32_good);
		PRINT_STATS_LE32(cck, false_alarm_cnt);
		PRINT_STATS_LE32(cck, fina_sync_err_cnt);
		PRINT_STATS_LE32(cck, sfd_timeout);
		PRINT_STATS_LE32(cck, fina_timeout);
		PRINT_STATS_LE32(cck, unresponded_rts);
		PRINT_STATS_LE32(cck, rxe_frame_lmt_overrun);
		PRINT_STATS_LE32(cck, sent_ack_cnt);
		PRINT_STATS_LE32(cck, sent_cts_cnt);
		PRINT_STATS_LE32(cck, sent_ba_rsp_cnt);
		PRINT_STATS_LE32(cck, dsp_self_kill);
		PRINT_STATS_LE32(cck, mh_format_err);
		PRINT_STATS_LE32(cck, re_acq_main_rssi_sum);
		PRINT_STATS_LE32(cck, reserved);
	} else {
		struct mvm_statistics_rx_phy *cck = &mvm->rx_stats.cck;

		PRINT_STATS_LE32(cck, unresponded_rts);
		PRINT_STATS_LE32(cck, rxe_frame_lmt_overrun);
		PRINT_STATS_LE32(cck, sent_ba_rsp_cnt);
		PRINT_STATS_LE32(cck, dsp_self_kill);
		PRINT_STATS_LE32(cck, reserved);
	}

	pos += scnprintf(buf + pos, bufsz - pos, fmt_header,
			 "Statistics_Rx - GENERAL");
	if (!iwl_mvm_has_new_rx_stats_api(mvm)) {
		struct mvm_statistics_rx_non_phy_v3 *general =
			&mvm->rx_stats_v3.general;

		PRINT_STATS_LE32(general, bogus_cts);
		PRINT_STATS_LE32(general, bogus_ack);
		PRINT_STATS_LE32(general, non_bssid_frames);
		PRINT_STATS_LE32(general, filtered_frames);
		PRINT_STATS_LE32(general, non_channel_beacons);
		PRINT_STATS_LE32(general, channel_beacons);
		PRINT_STATS_LE32(general, num_missed_bcon);
		PRINT_STATS_LE32(general, adc_rx_saturation_time);
		PRINT_STATS_LE32(general, ina_detection_search_time);
		PRINT_STATS_LE32(general, beacon_silence_rssi_a);
		PRINT_STATS_LE32(general, beacon_silence_rssi_b);
		PRINT_STATS_LE32(general, beacon_silence_rssi_c);
		PRINT_STATS_LE32(general, interference_data_flag);
		PRINT_STATS_LE32(general, channel_load);
		PRINT_STATS_LE32(general, dsp_false_alarms);
		PRINT_STATS_LE32(general, beacon_rssi_a);
		PRINT_STATS_LE32(general, beacon_rssi_b);
		PRINT_STATS_LE32(general, beacon_rssi_c);
		PRINT_STATS_LE32(general, beacon_energy_a);
		PRINT_STATS_LE32(general, beacon_energy_b);
		PRINT_STATS_LE32(general, beacon_energy_c);
		PRINT_STATS_LE32(general, num_bt_kills);
		PRINT_STATS_LE32(general, mac_id);
		PRINT_STATS_LE32(general, directed_data_mpdu);
	} else {
		struct mvm_statistics_rx_non_phy *general =
			&mvm->rx_stats.general;

		PRINT_STATS_LE32(general, bogus_cts);
		PRINT_STATS_LE32(general, bogus_ack);
		PRINT_STATS_LE32(general, non_channel_beacons);
		PRINT_STATS_LE32(general, channel_beacons);
		PRINT_STATS_LE32(general, num_missed_bcon);
		PRINT_STATS_LE32(general, adc_rx_saturation_time);
		PRINT_STATS_LE32(general, ina_detection_search_time);
		PRINT_STATS_LE32(general, beacon_silence_rssi_a);
		PRINT_STATS_LE32(general, beacon_silence_rssi_b);
		PRINT_STATS_LE32(general, beacon_silence_rssi_c);
		PRINT_STATS_LE32(general, interference_data_flag);
		PRINT_STATS_LE32(general, channel_load);
		PRINT_STATS_LE32(general, beacon_rssi_a);
		PRINT_STATS_LE32(general, beacon_rssi_b);
		PRINT_STATS_LE32(general, beacon_rssi_c);
		PRINT_STATS_LE32(general, beacon_energy_a);
		PRINT_STATS_LE32(general, beacon_energy_b);
		PRINT_STATS_LE32(general, beacon_energy_c);
		PRINT_STATS_LE32(general, num_bt_kills);
		PRINT_STATS_LE32(general, mac_id);
	}

	pos += scnprintf(buf + pos, bufsz - pos, fmt_header,
			 "Statistics_Rx - HT");
	if (!iwl_mvm_has_new_rx_stats_api(mvm)) {
		struct mvm_statistics_rx_ht_phy_v1 *ht =
			&mvm->rx_stats_v3.ofdm_ht;

		PRINT_STATS_LE32(ht, plcp_err);
		PRINT_STATS_LE32(ht, overrun_err);
		PRINT_STATS_LE32(ht, early_overrun_err);
		PRINT_STATS_LE32(ht, crc32_good);
		PRINT_STATS_LE32(ht, crc32_err);
		PRINT_STATS_LE32(ht, mh_format_err);
		PRINT_STATS_LE32(ht, agg_crc32_good);
		PRINT_STATS_LE32(ht, agg_mpdu_cnt);
		PRINT_STATS_LE32(ht, agg_cnt);
		PRINT_STATS_LE32(ht, unsupport_mcs);
	} else {
		struct mvm_statistics_rx_ht_phy *ht =
			&mvm->rx_stats.ofdm_ht;

		PRINT_STATS_LE32(ht, mh_format_err);
		PRINT_STATS_LE32(ht, agg_mpdu_cnt);
		PRINT_STATS_LE32(ht, agg_cnt);
		PRINT_STATS_LE32(ht, unsupport_mcs);
	}

	mutex_unlock(&mvm->mutex);

	ret = simple_read_from_buffer(user_buf, count, ppos, buf, pos);
	kfree(buf);

	return ret;
}
#undef PRINT_STAT_LE32

static ssize_t iwl_dbgfs_frame_stats_read(struct iwl_mvm *mvm,
					  char __user *user_buf, size_t count,
					  loff_t *ppos,
					  struct iwl_mvm_frame_stats *stats)
{
	char *buff, *pos, *endpos;
	int idx, i;
	int ret;
	static const size_t bufsz = 1024;

	buff = kmalloc(bufsz, GFP_KERNEL);
	if (!buff)
		return -ENOMEM;

	spin_lock_bh(&mvm->drv_stats_lock);

	pos = buff;
	endpos = pos + bufsz;

	pos += scnprintf(pos, endpos - pos,
			 "Legacy/HT/VHT\t:\t%d/%d/%d\n",
			 stats->legacy_frames,
			 stats->ht_frames,
			 stats->vht_frames);
	pos += scnprintf(pos, endpos - pos, "20/40/80\t:\t%d/%d/%d\n",
			 stats->bw_20_frames,
			 stats->bw_40_frames,
			 stats->bw_80_frames);
	pos += scnprintf(pos, endpos - pos, "NGI/SGI\t\t:\t%d/%d\n",
			 stats->ngi_frames,
			 stats->sgi_frames);
	pos += scnprintf(pos, endpos - pos, "SISO/MIMO2\t:\t%d/%d\n",
			 stats->siso_frames,
			 stats->mimo2_frames);
	pos += scnprintf(pos, endpos - pos, "FAIL/SCSS\t:\t%d/%d\n",
			 stats->fail_frames,
			 stats->success_frames);
	pos += scnprintf(pos, endpos - pos, "MPDUs agg\t:\t%d\n",
			 stats->agg_frames);
	pos += scnprintf(pos, endpos - pos, "A-MPDUs\t\t:\t%d\n",
			 stats->ampdu_count);
	pos += scnprintf(pos, endpos - pos, "Avg MPDUs/A-MPDU:\t%d\n",
			 stats->ampdu_count > 0 ?
			 (stats->agg_frames / stats->ampdu_count) : 0);

	pos += scnprintf(pos, endpos - pos, "Last Rates\n");

	idx = stats->last_frame_idx - 1;
	for (i = 0; i < ARRAY_SIZE(stats->last_rates); i++) {
		idx = (idx + 1) % ARRAY_SIZE(stats->last_rates);
		if (stats->last_rates[idx] == 0)
			continue;
		pos += scnprintf(pos, endpos - pos, "Rate[%d]: ",
				 (int)(ARRAY_SIZE(stats->last_rates) - i));
		pos += rs_pretty_print_rate(pos, endpos - pos,
					    stats->last_rates[idx]);
	}
	spin_unlock_bh(&mvm->drv_stats_lock);

	ret = simple_read_from_buffer(user_buf, count, ppos, buff, pos - buff);
	kfree(buff);

	return ret;
}

static ssize_t iwl_dbgfs_drv_rx_stats_read(struct file *file,
					   char __user *user_buf, size_t count,
					   loff_t *ppos)
{
	struct iwl_mvm *mvm = file->private_data;

	return iwl_dbgfs_frame_stats_read(mvm, user_buf, count, ppos,
					  &mvm->drv_rx_stats);
}

static ssize_t iwl_dbgfs_fw_restart_write(struct iwl_mvm *mvm, char *buf,
					  size_t count, loff_t *ppos)
{
	int __maybe_unused ret;

	if (!iwl_mvm_firmware_running(mvm))
		return -EIO;

	mutex_lock(&mvm->mutex);

	/* allow one more restart that we're provoking here */
	if (mvm->fw_restart >= 0)
		mvm->fw_restart++;

	/* take the return value to make compiler happy - it will fail anyway */
	ret = iwl_mvm_send_cmd_pdu(mvm, REPLY_ERROR, 0, 0, NULL);

	mutex_unlock(&mvm->mutex);

	return count;
}

static ssize_t iwl_dbgfs_fw_nmi_write(struct iwl_mvm *mvm, char *buf,
				      size_t count, loff_t *ppos)
{
	if (!iwl_mvm_firmware_running(mvm))
		return -EIO;

	iwl_force_nmi(mvm->trans);

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
iwl_dbgfs_scan_ant_rxchain_write(struct iwl_mvm *mvm, char *buf,
				 size_t count, loff_t *ppos)
{
	u8 scan_rx_ant;

	if (!iwl_mvm_firmware_running(mvm))
		return -EIO;

	if (sscanf(buf, "%hhx", &scan_rx_ant) != 1)
		return -EINVAL;
	if (scan_rx_ant > ANT_ABC)
		return -EINVAL;
	if (scan_rx_ant & ~(iwl_mvm_get_valid_rx_ant(mvm)))
		return -EINVAL;

	if (mvm->scan_rx_ant != scan_rx_ant) {
		mvm->scan_rx_ant = scan_rx_ant;
		if (fw_has_capa(&mvm->fw->ucode_capa,
				IWL_UCODE_TLV_CAPA_UMAC_SCAN))
			iwl_mvm_config_scan(mvm);
	}

	return count;
}

static ssize_t iwl_dbgfs_indirection_tbl_write(struct iwl_mvm *mvm,
					       char *buf, size_t count,
					       loff_t *ppos)
{
	struct iwl_rss_config_cmd cmd = {
		.flags = cpu_to_le32(IWL_RSS_ENABLE),
		.hash_mask = IWL_RSS_HASH_TYPE_IPV4_TCP |
			     IWL_RSS_HASH_TYPE_IPV4_UDP |
			     IWL_RSS_HASH_TYPE_IPV4_PAYLOAD |
			     IWL_RSS_HASH_TYPE_IPV6_TCP |
			     IWL_RSS_HASH_TYPE_IPV6_UDP |
			     IWL_RSS_HASH_TYPE_IPV6_PAYLOAD,
	};
	int ret, i, num_repeats, nbytes = count / 2;

	ret = hex2bin(cmd.indirection_table, buf, nbytes);
	if (ret)
		return ret;

	/*
	 * The input is the redirection table, partial or full.
	 * Repeat the pattern if needed.
	 * For example, input of 01020F will be repeated 42 times,
	 * indirecting RSS hash results to queues 1, 2, 15 (skipping
	 * queues 3 - 14).
	 */
	num_repeats = ARRAY_SIZE(cmd.indirection_table) / nbytes;
	for (i = 1; i < num_repeats; i++)
		memcpy(&cmd.indirection_table[i * nbytes],
		       cmd.indirection_table, nbytes);
	/* handle cut in the middle pattern for the last places */
	memcpy(&cmd.indirection_table[i * nbytes], cmd.indirection_table,
	       ARRAY_SIZE(cmd.indirection_table) % nbytes);

	netdev_rss_key_fill(cmd.secret_key, sizeof(cmd.secret_key));

	mutex_lock(&mvm->mutex);
	if (iwl_mvm_firmware_running(mvm))
		ret = iwl_mvm_send_cmd_pdu(mvm, RSS_CONFIG_CMD, 0,
					   sizeof(cmd), &cmd);
	else
		ret = 0;
	mutex_unlock(&mvm->mutex);

	return ret ?: count;
}

static ssize_t iwl_dbgfs_inject_packet_write(struct iwl_mvm *mvm,
					     char *buf, size_t count,
					     loff_t *ppos)
{
	struct iwl_rx_cmd_buffer rxb = {
		._rx_page_order = 0,
		.truesize = 0, /* not used */
		._offset = 0,
	};
	struct iwl_rx_packet *pkt;
	struct iwl_rx_mpdu_desc *desc;
	int bin_len = count / 2;
	int ret = -EINVAL;
	size_t mpdu_cmd_hdr_size = (mvm->trans->trans_cfg->device_family >=
				    IWL_DEVICE_FAMILY_AX210) ?
		sizeof(struct iwl_rx_mpdu_desc) :
		IWL_RX_DESC_SIZE_V1;

	if (!iwl_mvm_firmware_running(mvm))
		return -EIO;

	/* supporting only 9000 descriptor */
	if (!mvm->trans->trans_cfg->mq_rx_supported)
		return -ENOTSUPP;

	rxb._page = alloc_pages(GFP_ATOMIC, 0);
	if (!rxb._page)
		return -ENOMEM;
	pkt = rxb_addr(&rxb);

	ret = hex2bin(page_address(rxb._page), buf, bin_len);
	if (ret)
		goto out;

	/* avoid invalid memory access */
	if (bin_len < sizeof(*pkt) + mpdu_cmd_hdr_size)
		goto out;

	/* check this is RX packet */
	if (WIDE_ID(pkt->hdr.group_id, pkt->hdr.cmd) !=
	    WIDE_ID(LEGACY_GROUP, REPLY_RX_MPDU_CMD))
		goto out;

	/* check the length in metadata matches actual received length */
	desc = (void *)pkt->data;
	if (le16_to_cpu(desc->mpdu_len) !=
	    (bin_len - mpdu_cmd_hdr_size - sizeof(*pkt)))
		goto out;

	local_bh_disable();
	iwl_mvm_rx_mpdu_mq(mvm, NULL, &rxb, 0);
	local_bh_enable();
	ret = 0;

out:
	iwl_free_rxb(&rxb);

	return ret ?: count;
}

static int _iwl_dbgfs_inject_beacon_ie(struct iwl_mvm *mvm, char *bin, int len)
{
	struct ieee80211_vif *vif;
	struct iwl_mvm_vif *mvmvif;
	struct sk_buff *beacon;
	struct ieee80211_tx_info *info;
	struct iwl_mac_beacon_cmd beacon_cmd = {};
	u8 rate;
	u16 flags;
	int i;

	len /= 2;

	/* Element len should be represented by u8 */
	if (len >= U8_MAX)
		return -EINVAL;

	if (!iwl_mvm_firmware_running(mvm))
		return -EIO;

	if (!iwl_mvm_has_new_tx_api(mvm) &&
	    !fw_has_api(&mvm->fw->ucode_capa,
			IWL_UCODE_TLV_API_NEW_BEACON_TEMPLATE))
		return -EINVAL;

	rcu_read_lock();

	for (i = 0; i < NUM_MAC_INDEX_DRIVER; i++) {
		vif = iwl_mvm_rcu_dereference_vif_id(mvm, i, true);
		if (!vif)
			continue;

		if (vif->type == NL80211_IFTYPE_AP)
			break;
	}

	if (i == NUM_MAC_INDEX_DRIVER || !vif)
		goto out_err;

	mvm->hw->extra_beacon_tailroom = len;

	beacon = ieee80211_beacon_get_template(mvm->hw, vif, NULL);
	if (!beacon)
		goto out_err;

	if (len && hex2bin(skb_put_zero(beacon, len), bin, len)) {
		dev_kfree_skb(beacon);
		goto out_err;
	}

	mvm->beacon_inject_active = true;

	mvmvif = iwl_mvm_vif_from_mac80211(vif);
	info = IEEE80211_SKB_CB(beacon);
	rate = iwl_mvm_mac_ctxt_get_lowest_rate(info, vif);
	flags = iwl_mvm_mac80211_idx_to_hwrate(rate);

	if (rate == IWL_FIRST_CCK_RATE)
		flags |= IWL_MAC_BEACON_CCK;

	beacon_cmd.flags = cpu_to_le16(flags);
	beacon_cmd.byte_cnt = cpu_to_le16((u16)beacon->len);
	beacon_cmd.template_id = cpu_to_le32((u32)mvmvif->id);

	iwl_mvm_mac_ctxt_set_tim(mvm, &beacon_cmd.tim_idx,
				 &beacon_cmd.tim_size,
				 beacon->data, beacon->len);

	mutex_lock(&mvm->mutex);
	iwl_mvm_mac_ctxt_send_beacon_cmd(mvm, beacon, &beacon_cmd,
					 sizeof(beacon_cmd));
	mutex_unlock(&mvm->mutex);

	dev_kfree_skb(beacon);

	rcu_read_unlock();
	return 0;

out_err:
	rcu_read_unlock();
	return -EINVAL;
}

static ssize_t iwl_dbgfs_inject_beacon_ie_write(struct iwl_mvm *mvm,
						char *buf, size_t count,
						loff_t *ppos)
{
	int ret = _iwl_dbgfs_inject_beacon_ie(mvm, buf, count);

	mvm->hw->extra_beacon_tailroom = 0;
	return ret ?: count;
}

static ssize_t iwl_dbgfs_inject_beacon_ie_restore_write(struct iwl_mvm *mvm,
							char *buf,
							size_t count,
							loff_t *ppos)
{
	int ret = _iwl_dbgfs_inject_beacon_ie(mvm, NULL, 0);

	mvm->hw->extra_beacon_tailroom = 0;
	mvm->beacon_inject_active = false;
	return ret ?: count;
}

static ssize_t iwl_dbgfs_fw_dbg_conf_read(struct file *file,
					  char __user *user_buf,
					  size_t count, loff_t *ppos)
{
	struct iwl_mvm *mvm = file->private_data;
	int conf;
	char buf[8];
	const size_t bufsz = sizeof(buf);
	int pos = 0;

	mutex_lock(&mvm->mutex);
	conf = mvm->fwrt.dump.conf;
	mutex_unlock(&mvm->mutex);

	pos += scnprintf(buf + pos, bufsz - pos, "%d\n", conf);

	return simple_read_from_buffer(user_buf, count, ppos, buf, pos);
}

static ssize_t iwl_dbgfs_fw_dbg_conf_write(struct iwl_mvm *mvm,
					   char *buf, size_t count,
					   loff_t *ppos)
{
	unsigned int conf_id;
	int ret;

	if (!iwl_mvm_firmware_running(mvm))
		return -EIO;

	ret = kstrtouint(buf, 0, &conf_id);
	if (ret)
		return ret;

	if (WARN_ON(conf_id >= FW_DBG_CONF_MAX))
		return -EINVAL;

	mutex_lock(&mvm->mutex);
	ret = iwl_fw_start_dbg_conf(&mvm->fwrt, conf_id);
	mutex_unlock(&mvm->mutex);

	return ret ?: count;
}

static ssize_t iwl_dbgfs_fw_dbg_collect_write(struct iwl_mvm *mvm,
					      char *buf, size_t count,
					      loff_t *ppos)
{
	if (count == 0)
		return 0;

	iwl_dbg_tlv_time_point(&mvm->fwrt, IWL_FW_INI_TIME_POINT_USER_TRIGGER,
			       NULL);

	iwl_fw_dbg_collect(&mvm->fwrt, FW_DBG_TRIGGER_USER, buf,
			   (count - 1), NULL);

	return count;
}

#define ADD_TEXT(...) pos += scnprintf(buf + pos, bufsz - pos, __VA_ARGS__)
#ifdef CONFIG_IWLWIFI_BCAST_FILTERING
static ssize_t iwl_dbgfs_bcast_filters_read(struct file *file,
					    char __user *user_buf,
					    size_t count, loff_t *ppos)
{
	struct iwl_mvm *mvm = file->private_data;
	struct iwl_bcast_filter_cmd cmd;
	const struct iwl_fw_bcast_filter *filter;
	char *buf;
	int bufsz = 1024;
	int i, j, pos = 0;
	ssize_t ret;

	buf = kzalloc(bufsz, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	mutex_lock(&mvm->mutex);
	if (!iwl_mvm_bcast_filter_build_cmd(mvm, &cmd)) {
		ADD_TEXT("None\n");
		mutex_unlock(&mvm->mutex);
		goto out;
	}
	mutex_unlock(&mvm->mutex);

	for (i = 0; cmd.filters[i].attrs[0].mask; i++) {
		filter = &cmd.filters[i];

		ADD_TEXT("Filter [%d]:\n", i);
		ADD_TEXT("\tDiscard=%d\n", filter->discard);
		ADD_TEXT("\tFrame Type: %s\n",
			 filter->frame_type ? "IPv4" : "Generic");

		for (j = 0; j < ARRAY_SIZE(filter->attrs); j++) {
			const struct iwl_fw_bcast_filter_attr *attr;

			attr = &filter->attrs[j];
			if (!attr->mask)
				break;

			ADD_TEXT("\tAttr [%d]: offset=%d (from %s), mask=0x%x, value=0x%x reserved=0x%x\n",
				 j, attr->offset,
				 attr->offset_type ? "IP End" :
						     "Payload Start",
				 be32_to_cpu(attr->mask),
				 be32_to_cpu(attr->val),
				 le16_to_cpu(attr->reserved1));
		}
	}
out:
	ret = simple_read_from_buffer(user_buf, count, ppos, buf, pos);
	kfree(buf);
	return ret;
}

static ssize_t iwl_dbgfs_bcast_filters_write(struct iwl_mvm *mvm, char *buf,
					     size_t count, loff_t *ppos)
{
	int pos, next_pos;
	struct iwl_fw_bcast_filter filter = {};
	struct iwl_bcast_filter_cmd cmd;
	u32 filter_id, attr_id, mask, value;
	int err = 0;

	if (sscanf(buf, "%d %hhi %hhi %n", &filter_id, &filter.discard,
		   &filter.frame_type, &pos) != 3)
		return -EINVAL;

	if (filter_id >= ARRAY_SIZE(mvm->dbgfs_bcast_filtering.cmd.filters) ||
	    filter.frame_type > BCAST_FILTER_FRAME_TYPE_IPV4)
		return -EINVAL;

	for (attr_id = 0; attr_id < ARRAY_SIZE(filter.attrs);
	     attr_id++) {
		struct iwl_fw_bcast_filter_attr *attr =
				&filter.attrs[attr_id];

		if (pos >= count)
			break;

		if (sscanf(&buf[pos], "%hhi %hhi %i %i %n",
			   &attr->offset, &attr->offset_type,
			   &mask, &value, &next_pos) != 4)
			return -EINVAL;

		attr->mask = cpu_to_be32(mask);
		attr->val = cpu_to_be32(value);
		if (mask)
			filter.num_attrs++;

		pos += next_pos;
	}

	mutex_lock(&mvm->mutex);
	memcpy(&mvm->dbgfs_bcast_filtering.cmd.filters[filter_id],
	       &filter, sizeof(filter));

	/* send updated bcast filtering configuration */
	if (iwl_mvm_firmware_running(mvm) &&
	    mvm->dbgfs_bcast_filtering.override &&
	    iwl_mvm_bcast_filter_build_cmd(mvm, &cmd))
		err = iwl_mvm_send_cmd_pdu(mvm, BCAST_FILTER_CMD, 0,
					   sizeof(cmd), &cmd);
	mutex_unlock(&mvm->mutex);

	return err ?: count;
}

static ssize_t iwl_dbgfs_bcast_filters_macs_read(struct file *file,
						 char __user *user_buf,
						 size_t count, loff_t *ppos)
{
	struct iwl_mvm *mvm = file->private_data;
	struct iwl_bcast_filter_cmd cmd;
	char *buf;
	int bufsz = 1024;
	int i, pos = 0;
	ssize_t ret;

	buf = kzalloc(bufsz, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	mutex_lock(&mvm->mutex);
	if (!iwl_mvm_bcast_filter_build_cmd(mvm, &cmd)) {
		ADD_TEXT("None\n");
		mutex_unlock(&mvm->mutex);
		goto out;
	}
	mutex_unlock(&mvm->mutex);

	for (i = 0; i < ARRAY_SIZE(cmd.macs); i++) {
		const struct iwl_fw_bcast_mac *mac = &cmd.macs[i];

		ADD_TEXT("Mac [%d]: discard=%d attached_filters=0x%x\n",
			 i, mac->default_discard, mac->attached_filters);
	}
out:
	ret = simple_read_from_buffer(user_buf, count, ppos, buf, pos);
	kfree(buf);
	return ret;
}

static ssize_t iwl_dbgfs_bcast_filters_macs_write(struct iwl_mvm *mvm,
						  char *buf, size_t count,
						  loff_t *ppos)
{
	struct iwl_bcast_filter_cmd cmd;
	struct iwl_fw_bcast_mac mac = {};
	u32 mac_id, attached_filters;
	int err = 0;

	if (!mvm->bcast_filters)
		return -ENOENT;

	if (sscanf(buf, "%d %hhi %i", &mac_id, &mac.default_discard,
		   &attached_filters) != 3)
		return -EINVAL;

	if (mac_id >= ARRAY_SIZE(cmd.macs) ||
	    mac.default_discard > 1 ||
	    attached_filters >= BIT(ARRAY_SIZE(cmd.filters)))
		return -EINVAL;

	mac.attached_filters = cpu_to_le16(attached_filters);

	mutex_lock(&mvm->mutex);
	memcpy(&mvm->dbgfs_bcast_filtering.cmd.macs[mac_id],
	       &mac, sizeof(mac));

	/* send updated bcast filtering configuration */
	if (iwl_mvm_firmware_running(mvm) &&
	    mvm->dbgfs_bcast_filtering.override &&
	    iwl_mvm_bcast_filter_build_cmd(mvm, &cmd))
		err = iwl_mvm_send_cmd_pdu(mvm, BCAST_FILTER_CMD, 0,
					   sizeof(cmd), &cmd);
	mutex_unlock(&mvm->mutex);

	return err ?: count;
}
#endif

#define MVM_DEBUGFS_WRITE_FILE_OPS(name, bufsz) \
	_MVM_DEBUGFS_WRITE_FILE_OPS(name, bufsz, struct iwl_mvm)
#define MVM_DEBUGFS_READ_WRITE_FILE_OPS(name, bufsz) \
	_MVM_DEBUGFS_READ_WRITE_FILE_OPS(name, bufsz, struct iwl_mvm)
#define MVM_DEBUGFS_ADD_FILE_ALIAS(alias, name, parent, mode) do {	\
		debugfs_create_file(alias, mode, parent, mvm,		\
				    &iwl_dbgfs_##name##_ops);		\
	} while (0)
#define MVM_DEBUGFS_ADD_FILE(name, parent, mode) \
	MVM_DEBUGFS_ADD_FILE_ALIAS(#name, name, parent, mode)

#define MVM_DEBUGFS_WRITE_STA_FILE_OPS(name, bufsz) \
	_MVM_DEBUGFS_WRITE_FILE_OPS(name, bufsz, struct ieee80211_sta)
#define MVM_DEBUGFS_READ_WRITE_STA_FILE_OPS(name, bufsz) \
	_MVM_DEBUGFS_READ_WRITE_FILE_OPS(name, bufsz, struct ieee80211_sta)

#define MVM_DEBUGFS_ADD_STA_FILE_ALIAS(alias, name, parent, mode) do {	\
		debugfs_create_file(alias, mode, parent, sta,		\
				    &iwl_dbgfs_##name##_ops);		\
	} while (0)
#define MVM_DEBUGFS_ADD_STA_FILE(name, parent, mode) \
	MVM_DEBUGFS_ADD_STA_FILE_ALIAS(#name, name, parent, mode)

static ssize_t
iwl_dbgfs_prph_reg_read(struct file *file,
			char __user *user_buf,
			size_t count, loff_t *ppos)
{
	struct iwl_mvm *mvm = file->private_data;
	int pos = 0;
	char buf[32];
	const size_t bufsz = sizeof(buf);

	if (!mvm->dbgfs_prph_reg_addr)
		return -EINVAL;

	pos += scnprintf(buf + pos, bufsz - pos, "Reg 0x%x: (0x%x)\n",
		mvm->dbgfs_prph_reg_addr,
		iwl_read_prph(mvm->trans, mvm->dbgfs_prph_reg_addr));

	return simple_read_from_buffer(user_buf, count, ppos, buf, pos);
}

static ssize_t
iwl_dbgfs_prph_reg_write(struct iwl_mvm *mvm, char *buf,
			 size_t count, loff_t *ppos)
{
	u8 args;
	u32 value;

	args = sscanf(buf, "%i %i", &mvm->dbgfs_prph_reg_addr, &value);
	/* if we only want to set the reg address - nothing more to do */
	if (args == 1)
		goto out;

	/* otherwise, make sure we have both address and value */
	if (args != 2)
		return -EINVAL;

	iwl_write_prph(mvm->trans, mvm->dbgfs_prph_reg_addr, value);

out:
	return count;
}

static ssize_t
iwl_dbgfs_send_echo_cmd_write(struct iwl_mvm *mvm, char *buf,
			      size_t count, loff_t *ppos)
{
	int ret;

	if (!iwl_mvm_firmware_running(mvm))
		return -EIO;

	mutex_lock(&mvm->mutex);
	ret = iwl_mvm_send_cmd_pdu(mvm, ECHO_CMD, 0, 0, NULL);
	mutex_unlock(&mvm->mutex);

	return ret ?: count;
}

struct iwl_mvm_sniffer_apply {
	struct iwl_mvm *mvm;
	u8 *bssid;
	u16 aid;
};

static bool iwl_mvm_sniffer_apply(struct iwl_notif_wait_data *notif_data,
				  struct iwl_rx_packet *pkt, void *data)
{
	struct iwl_mvm_sniffer_apply *apply = data;

	apply->mvm->cur_aid = cpu_to_le16(apply->aid);
	memcpy(apply->mvm->cur_bssid, apply->bssid,
	       sizeof(apply->mvm->cur_bssid));

	return true;
}

static ssize_t
iwl_dbgfs_he_sniffer_params_write(struct iwl_mvm *mvm, char *buf,
				  size_t count, loff_t *ppos)
{
	struct iwl_notification_wait wait;
	struct iwl_he_monitor_cmd he_mon_cmd = {};
	struct iwl_mvm_sniffer_apply apply = {
		.mvm = mvm,
	};
	u16 wait_cmds[] = {
		iwl_cmd_id(HE_AIR_SNIFFER_CONFIG_CMD, DATA_PATH_GROUP, 0),
	};
	u32 aid;
	int ret;

	if (!iwl_mvm_firmware_running(mvm))
		return -EIO;

	ret = sscanf(buf, "%x %2hhx:%2hhx:%2hhx:%2hhx:%2hhx:%2hhx", &aid,
		     &he_mon_cmd.bssid[0], &he_mon_cmd.bssid[1],
		     &he_mon_cmd.bssid[2], &he_mon_cmd.bssid[3],
		     &he_mon_cmd.bssid[4], &he_mon_cmd.bssid[5]);
	if (ret != 7)
		return -EINVAL;

	he_mon_cmd.aid = cpu_to_le16(aid);

	apply.aid = aid;
	apply.bssid = (void *)he_mon_cmd.bssid;

	mutex_lock(&mvm->mutex);

	/*
	 * Use the notification waiter to get our function triggered
	 * in sequence with other RX. This ensures that frames we get
	 * on the RX queue _before_ the new configuration is applied
	 * still have mvm->cur_aid pointing to the old AID, and that
	 * frames on the RX queue _after_ the firmware processed the
	 * new configuration (and sent the response, synchronously)
	 * get mvm->cur_aid correctly set to the new AID.
	 */
	iwl_init_notification_wait(&mvm->notif_wait, &wait,
				   wait_cmds, ARRAY_SIZE(wait_cmds),
				   iwl_mvm_sniffer_apply, &apply);

	ret = iwl_mvm_send_cmd_pdu(mvm, iwl_cmd_id(HE_AIR_SNIFFER_CONFIG_CMD,
						   DATA_PATH_GROUP, 0), 0,
				   sizeof(he_mon_cmd), &he_mon_cmd);

	/* no need to really wait, we already did anyway */
	iwl_remove_notification(&mvm->notif_wait, &wait);

	mutex_unlock(&mvm->mutex);

	return ret ?: count;
}

static ssize_t
iwl_dbgfs_he_sniffer_params_read(struct file *file, char __user *user_buf,
				 size_t count, loff_t *ppos)
{
	struct iwl_mvm *mvm = file->private_data;
	u8 buf[32];
	int len;

	len = scnprintf(buf, sizeof(buf),
			"%d %02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx\n",
			le16_to_cpu(mvm->cur_aid), mvm->cur_bssid[0],
			mvm->cur_bssid[1], mvm->cur_bssid[2], mvm->cur_bssid[3],
			mvm->cur_bssid[4], mvm->cur_bssid[5]);

	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static ssize_t
iwl_dbgfs_uapsd_noagg_bssids_read(struct file *file, char __user *user_buf,
				  size_t count, loff_t *ppos)
{
	struct iwl_mvm *mvm = file->private_data;
	u8 buf[IWL_MVM_UAPSD_NOAGG_BSSIDS_NUM * ETH_ALEN * 3 + 1];
	unsigned int pos = 0;
	size_t bufsz = sizeof(buf);
	int i;

	mutex_lock(&mvm->mutex);

	for (i = 0; i < IWL_MVM_UAPSD_NOAGG_LIST_LEN; i++)
		pos += scnprintf(buf + pos, bufsz - pos, "%pM\n",
				 mvm->uapsd_noagg_bssids[i].addr);

	mutex_unlock(&mvm->mutex);

	return simple_read_from_buffer(user_buf, count, ppos, buf, pos);
}

static ssize_t
iwl_dbgfs_ltr_config_write(struct iwl_mvm *mvm,
			   char *buf, size_t count, loff_t *ppos)
{
	int ret;
	struct iwl_ltr_config_cmd ltr_config = {0};

	if (!iwl_mvm_firmware_running(mvm))
		return -EIO;

	if (sscanf(buf, "%x,%x,%x,%x,%x,%x,%x",
		   &ltr_config.flags,
		   &ltr_config.static_long,
		   &ltr_config.static_short,
		   &ltr_config.ltr_cfg_values[0],
		   &ltr_config.ltr_cfg_values[1],
		   &ltr_config.ltr_cfg_values[2],
		   &ltr_config.ltr_cfg_values[3]) != 7) {
		return -EINVAL;
	}

	mutex_lock(&mvm->mutex);
	ret = iwl_mvm_send_cmd_pdu(mvm, LTR_CONFIG, 0, sizeof(ltr_config),
				   &ltr_config);
	mutex_unlock(&mvm->mutex);

	if (ret)
		IWL_ERR(mvm, "failed to send ltr configuration cmd\n");

	return ret ?: count;
}

MVM_DEBUGFS_READ_WRITE_FILE_OPS(prph_reg, 64);

/* Device wide debugfs entries */
MVM_DEBUGFS_READ_FILE_OPS(ctdp_budget);
MVM_DEBUGFS_WRITE_FILE_OPS(stop_ctdp, 8);
MVM_DEBUGFS_WRITE_FILE_OPS(force_ctkill, 8);
MVM_DEBUGFS_WRITE_FILE_OPS(tx_flush, 16);
MVM_DEBUGFS_WRITE_FILE_OPS(sta_drain, 8);
MVM_DEBUGFS_WRITE_FILE_OPS(send_echo_cmd, 8);
MVM_DEBUGFS_READ_WRITE_FILE_OPS(sram, 64);
MVM_DEBUGFS_READ_WRITE_FILE_OPS(set_nic_temperature, 64);
MVM_DEBUGFS_READ_FILE_OPS(nic_temp);
MVM_DEBUGFS_READ_FILE_OPS(stations);
MVM_DEBUGFS_READ_FILE_OPS(rs_data);
MVM_DEBUGFS_READ_FILE_OPS(bt_notif);
MVM_DEBUGFS_READ_FILE_OPS(bt_cmd);
MVM_DEBUGFS_READ_WRITE_FILE_OPS(disable_power_off, 64);
MVM_DEBUGFS_READ_FILE_OPS(fw_rx_stats);
MVM_DEBUGFS_READ_FILE_OPS(drv_rx_stats);
MVM_DEBUGFS_READ_FILE_OPS(fw_ver);
MVM_DEBUGFS_WRITE_FILE_OPS(fw_restart, 10);
MVM_DEBUGFS_WRITE_FILE_OPS(fw_nmi, 10);
MVM_DEBUGFS_WRITE_FILE_OPS(bt_tx_prio, 10);
MVM_DEBUGFS_WRITE_FILE_OPS(bt_force_ant, 10);
MVM_DEBUGFS_READ_WRITE_FILE_OPS(scan_ant_rxchain, 8);
MVM_DEBUGFS_READ_WRITE_FILE_OPS(fw_dbg_conf, 8);
MVM_DEBUGFS_WRITE_FILE_OPS(fw_dbg_collect, 64);
MVM_DEBUGFS_WRITE_FILE_OPS(indirection_tbl,
			   (IWL_RSS_INDIRECTION_TABLE_SIZE * 2));
MVM_DEBUGFS_WRITE_FILE_OPS(inject_packet, 512);
MVM_DEBUGFS_WRITE_FILE_OPS(inject_beacon_ie, 512);
MVM_DEBUGFS_WRITE_FILE_OPS(inject_beacon_ie_restore, 512);

MVM_DEBUGFS_READ_FILE_OPS(uapsd_noagg_bssids);

#ifdef CONFIG_IWLWIFI_BCAST_FILTERING
MVM_DEBUGFS_READ_WRITE_FILE_OPS(bcast_filters, 256);
MVM_DEBUGFS_READ_WRITE_FILE_OPS(bcast_filters_macs, 256);
#endif

#ifdef CONFIG_ACPI
MVM_DEBUGFS_READ_FILE_OPS(sar_geo_profile);
#endif

MVM_DEBUGFS_READ_WRITE_STA_FILE_OPS(amsdu_len, 16);

MVM_DEBUGFS_READ_WRITE_FILE_OPS(he_sniffer_params, 32);

MVM_DEBUGFS_WRITE_FILE_OPS(ltr_config, 512);

static ssize_t iwl_dbgfs_mem_read(struct file *file, char __user *user_buf,
				  size_t count, loff_t *ppos)
{
	struct iwl_mvm *mvm = file->private_data;
	struct iwl_dbg_mem_access_cmd cmd = {};
	struct iwl_dbg_mem_access_rsp *rsp;
	struct iwl_host_cmd hcmd = {
		.flags = CMD_WANT_SKB | CMD_SEND_IN_RFKILL,
		.data = { &cmd, },
		.len = { sizeof(cmd) },
	};
	size_t delta;
	ssize_t ret, len;

	if (!iwl_mvm_firmware_running(mvm))
		return -EIO;

	hcmd.id = iwl_cmd_id(*ppos >> 24 ? UMAC_RD_WR : LMAC_RD_WR,
			     DEBUG_GROUP, 0);
	cmd.op = cpu_to_le32(DEBUG_MEM_OP_READ);

	/* Take care of alignment of both the position and the length */
	delta = *ppos & 0x3;
	cmd.addr = cpu_to_le32(*ppos - delta);
	cmd.len = cpu_to_le32(min(ALIGN(count + delta, 4) / 4,
				  (size_t)DEBUG_MEM_MAX_SIZE_DWORDS));

	mutex_lock(&mvm->mutex);
	ret = iwl_mvm_send_cmd(mvm, &hcmd);
	mutex_unlock(&mvm->mutex);

	if (ret < 0)
		return ret;

	rsp = (void *)hcmd.resp_pkt->data;
	if (le32_to_cpu(rsp->status) != DEBUG_MEM_STATUS_SUCCESS) {
		ret = -ENXIO;
		goto out;
	}

	len = min((size_t)le32_to_cpu(rsp->len) << 2,
		  iwl_rx_packet_payload_len(hcmd.resp_pkt) - sizeof(*rsp));
	len = min(len - delta, count);
	if (len < 0) {
		ret = -EFAULT;
		goto out;
	}

	ret = len - copy_to_user(user_buf, (void *)rsp->data + delta, len);
	*ppos += ret;

out:
	iwl_free_resp(&hcmd);
	return ret;
}

static ssize_t iwl_dbgfs_mem_write(struct file *file,
				   const char __user *user_buf, size_t count,
				   loff_t *ppos)
{
	struct iwl_mvm *mvm = file->private_data;
	struct iwl_dbg_mem_access_cmd *cmd;
	struct iwl_dbg_mem_access_rsp *rsp;
	struct iwl_host_cmd hcmd = {};
	size_t cmd_size;
	size_t data_size;
	u32 op, len;
	ssize_t ret;

	if (!iwl_mvm_firmware_running(mvm))
		return -EIO;

	hcmd.id = iwl_cmd_id(*ppos >> 24 ? UMAC_RD_WR : LMAC_RD_WR,
			     DEBUG_GROUP, 0);

	if (*ppos & 0x3 || count < 4) {
		op = DEBUG_MEM_OP_WRITE_BYTES;
		len = min(count, (size_t)(4 - (*ppos & 0x3)));
		data_size = len;
	} else {
		op = DEBUG_MEM_OP_WRITE;
		len = min(count >> 2, (size_t)DEBUG_MEM_MAX_SIZE_DWORDS);
		data_size = len << 2;
	}

	cmd_size = sizeof(*cmd) + ALIGN(data_size, 4);
	cmd = kzalloc(cmd_size, GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	cmd->op = cpu_to_le32(op);
	cmd->len = cpu_to_le32(len);
	cmd->addr = cpu_to_le32(*ppos);
	if (copy_from_user((void *)cmd->data, user_buf, data_size)) {
		kfree(cmd);
		return -EFAULT;
	}

	hcmd.flags = CMD_WANT_SKB | CMD_SEND_IN_RFKILL,
	hcmd.data[0] = (void *)cmd;
	hcmd.len[0] = cmd_size;

	mutex_lock(&mvm->mutex);
	ret = iwl_mvm_send_cmd(mvm, &hcmd);
	mutex_unlock(&mvm->mutex);

	kfree(cmd);

	if (ret < 0)
		return ret;

	rsp = (void *)hcmd.resp_pkt->data;
	if (rsp->status != DEBUG_MEM_STATUS_SUCCESS) {
		ret = -ENXIO;
		goto out;
	}

	ret = data_size;
	*ppos += ret;

out:
	iwl_free_resp(&hcmd);
	return ret;
}

static const struct file_operations iwl_dbgfs_mem_ops = {
	.read = iwl_dbgfs_mem_read,
	.write = iwl_dbgfs_mem_write,
	.open = simple_open,
	.llseek = default_llseek,
};

void iwl_mvm_sta_add_debugfs(struct ieee80211_hw *hw,
			     struct ieee80211_vif *vif,
			     struct ieee80211_sta *sta,
			     struct dentry *dir)
{
	struct iwl_mvm *mvm = IWL_MAC80211_GET_MVM(hw);

	if (iwl_mvm_has_tlc_offload(mvm)) {
		MVM_DEBUGFS_ADD_STA_FILE(rs_data, dir, 0400);
	}
	MVM_DEBUGFS_ADD_STA_FILE(amsdu_len, dir, 0600);
}

void iwl_mvm_dbgfs_register(struct iwl_mvm *mvm, struct dentry *dbgfs_dir)
{
	struct dentry *bcast_dir __maybe_unused;
	char buf[100];

	spin_lock_init(&mvm->drv_stats_lock);

	mvm->debugfs_dir = dbgfs_dir;

	MVM_DEBUGFS_ADD_FILE(tx_flush, mvm->debugfs_dir, 0200);
	MVM_DEBUGFS_ADD_FILE(sta_drain, mvm->debugfs_dir, 0200);
	MVM_DEBUGFS_ADD_FILE(sram, mvm->debugfs_dir, 0600);
	MVM_DEBUGFS_ADD_FILE(set_nic_temperature, mvm->debugfs_dir, 0600);
	MVM_DEBUGFS_ADD_FILE(nic_temp, dbgfs_dir, 0400);
	MVM_DEBUGFS_ADD_FILE(ctdp_budget, dbgfs_dir, 0400);
	MVM_DEBUGFS_ADD_FILE(stop_ctdp, dbgfs_dir, 0200);
	MVM_DEBUGFS_ADD_FILE(force_ctkill, dbgfs_dir, 0200);
	MVM_DEBUGFS_ADD_FILE(stations, dbgfs_dir, 0400);
	MVM_DEBUGFS_ADD_FILE(bt_notif, dbgfs_dir, 0400);
	MVM_DEBUGFS_ADD_FILE(bt_cmd, dbgfs_dir, 0400);
	MVM_DEBUGFS_ADD_FILE(disable_power_off, mvm->debugfs_dir, 0600);
	MVM_DEBUGFS_ADD_FILE(fw_ver, mvm->debugfs_dir, 0400);
	MVM_DEBUGFS_ADD_FILE(fw_rx_stats, mvm->debugfs_dir, 0400);
	MVM_DEBUGFS_ADD_FILE(drv_rx_stats, mvm->debugfs_dir, 0400);
	MVM_DEBUGFS_ADD_FILE(fw_restart, mvm->debugfs_dir, 0200);
	MVM_DEBUGFS_ADD_FILE(fw_nmi, mvm->debugfs_dir, 0200);
	MVM_DEBUGFS_ADD_FILE(bt_tx_prio, mvm->debugfs_dir, 0200);
	MVM_DEBUGFS_ADD_FILE(bt_force_ant, mvm->debugfs_dir, 0200);
	MVM_DEBUGFS_ADD_FILE(scan_ant_rxchain, mvm->debugfs_dir, 0600);
	MVM_DEBUGFS_ADD_FILE(prph_reg, mvm->debugfs_dir, 0600);
	MVM_DEBUGFS_ADD_FILE(fw_dbg_conf, mvm->debugfs_dir, 0600);
	MVM_DEBUGFS_ADD_FILE(fw_dbg_collect, mvm->debugfs_dir, 0200);
	MVM_DEBUGFS_ADD_FILE(send_echo_cmd, mvm->debugfs_dir, 0200);
	MVM_DEBUGFS_ADD_FILE(indirection_tbl, mvm->debugfs_dir, 0200);
	MVM_DEBUGFS_ADD_FILE(inject_packet, mvm->debugfs_dir, 0200);
	MVM_DEBUGFS_ADD_FILE(inject_beacon_ie, mvm->debugfs_dir, 0200);
	MVM_DEBUGFS_ADD_FILE(inject_beacon_ie_restore, mvm->debugfs_dir, 0200);
#ifdef CONFIG_ACPI
	MVM_DEBUGFS_ADD_FILE(sar_geo_profile, dbgfs_dir, 0400);
#endif
	MVM_DEBUGFS_ADD_FILE(he_sniffer_params, mvm->debugfs_dir, 0600);

	if (fw_has_capa(&mvm->fw->ucode_capa, IWL_UCODE_TLV_CAPA_SET_LTR_GEN2))
		MVM_DEBUGFS_ADD_FILE(ltr_config, mvm->debugfs_dir, 0200);

	debugfs_create_bool("enable_scan_iteration_notif", 0600,
			    mvm->debugfs_dir, &mvm->scan_iter_notif_enabled);
	debugfs_create_bool("drop_bcn_ap_mode", 0600, mvm->debugfs_dir,
			    &mvm->drop_bcn_ap_mode);

	MVM_DEBUGFS_ADD_FILE(uapsd_noagg_bssids, mvm->debugfs_dir, S_IRUSR);

#ifdef CONFIG_IWLWIFI_BCAST_FILTERING
	if (mvm->fw->ucode_capa.flags & IWL_UCODE_TLV_FLAGS_BCAST_FILTERING) {
		bcast_dir = debugfs_create_dir("bcast_filtering",
					       mvm->debugfs_dir);

		debugfs_create_bool("override", 0600, bcast_dir,
				    &mvm->dbgfs_bcast_filtering.override);

		MVM_DEBUGFS_ADD_FILE_ALIAS("filters", bcast_filters,
					   bcast_dir, 0600);
		MVM_DEBUGFS_ADD_FILE_ALIAS("macs", bcast_filters_macs,
					   bcast_dir, 0600);
	}
#endif

#ifdef CONFIG_PM_SLEEP
	MVM_DEBUGFS_ADD_FILE(d3_test, mvm->debugfs_dir, 0400);
	debugfs_create_bool("d3_wake_sysassert", 0600, mvm->debugfs_dir,
			    &mvm->d3_wake_sysassert);
	debugfs_create_u32("last_netdetect_scans", 0400, mvm->debugfs_dir,
			   &mvm->last_netdetect_scans);
#endif

	debugfs_create_u8("ps_disabled", 0400, mvm->debugfs_dir,
			  &mvm->ps_disabled);
	debugfs_create_blob("nvm_hw", 0400, mvm->debugfs_dir,
			    &mvm->nvm_hw_blob);
	debugfs_create_blob("nvm_sw", 0400, mvm->debugfs_dir,
			    &mvm->nvm_sw_blob);
	debugfs_create_blob("nvm_calib", 0400, mvm->debugfs_dir,
			    &mvm->nvm_calib_blob);
	debugfs_create_blob("nvm_prod", 0400, mvm->debugfs_dir,
			    &mvm->nvm_prod_blob);
	debugfs_create_blob("nvm_phy_sku", 0400, mvm->debugfs_dir,
			    &mvm->nvm_phy_sku_blob);
	debugfs_create_blob("nvm_reg", S_IRUSR,
			    mvm->debugfs_dir, &mvm->nvm_reg_blob);

	debugfs_create_file("mem", 0600, dbgfs_dir, mvm, &iwl_dbgfs_mem_ops);

	/*
	 * Create a symlink with mac80211. It will be removed when mac80211
	 * exists (before the opmode exists which removes the target.)
	 */
	snprintf(buf, 100, "../../%pd2", dbgfs_dir->d_parent);
	debugfs_create_symlink("iwlwifi", mvm->hw->wiphy->debugfsdir, buf);
}
