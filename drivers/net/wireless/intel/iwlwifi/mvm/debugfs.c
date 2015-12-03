/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2012 - 2014 Intel Corporation. All rights reserved.
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
 *  Intel Linux Wireless <ilw@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
 * Copyright(c) 2012 - 2014 Intel Corporation. All rights reserved.
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
#include <linux/vmalloc.h>

#include "mvm.h"
#include "fw-dbg.h"
#include "sta.h"
#include "iwl-io.h"
#include "debugfs.h"
#include "iwl-fw-error-dump.h"

static ssize_t iwl_dbgfs_tx_flush_write(struct iwl_mvm *mvm, char *buf,
					size_t count, loff_t *ppos)
{
	int ret;
	u32 scd_q_msk;

	if (!mvm->ucode_loaded || mvm->cur_ucode != IWL_UCODE_REGULAR)
		return -EIO;

	if (sscanf(buf, "%x", &scd_q_msk) != 1)
		return -EINVAL;

	IWL_ERR(mvm, "FLUSHING queues: scd_q_msk = 0x%x\n", scd_q_msk);

	mutex_lock(&mvm->mutex);
	ret =  iwl_mvm_flush_tx_path(mvm, scd_q_msk, 0) ? : count;
	mutex_unlock(&mvm->mutex);

	return ret;
}

static ssize_t iwl_dbgfs_sta_drain_write(struct iwl_mvm *mvm, char *buf,
					 size_t count, loff_t *ppos)
{
	struct iwl_mvm_sta *mvmsta;
	int sta_id, drain, ret;

	if (!mvm->ucode_loaded || mvm->cur_ucode != IWL_UCODE_REGULAR)
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

	if (!mvm->ucode_loaded)
		return -EINVAL;

	/* default is to dump the entire data segment */
	img = &mvm->fw->img[mvm->cur_ucode];
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

	if (!mvm->ucode_loaded)
		return -EINVAL;

	img = &mvm->fw->img[mvm->cur_ucode];
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

	if (!mvm->ucode_loaded && !mvm->temperature_test)
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
	int pos, temp;

	if (!mvm->ucode_loaded)
		return -EIO;

	mutex_lock(&mvm->mutex);
	temp = iwl_mvm_get_temp(mvm);
	mutex_unlock(&mvm->mutex);

	if (temp < 0)
		return temp;

	pos = scnprintf(buf , sizeof(buf), "%d\n", temp);

	return simple_read_from_buffer(user_buf, count, ppos, buf, pos);
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

static ssize_t iwl_dbgfs_disable_power_off_write(struct iwl_mvm *mvm, char *buf,
						 size_t count, loff_t *ppos)
{
	int ret, val;

	if (!mvm->ucode_loaded)
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

#define BT_MBOX_MSG(_notif, _num, _field)				     \
	((le32_to_cpu((_notif)->mbox_msg[(_num)]) & BT_MBOX##_num##_##_field)\
	>> BT_MBOX##_num##_##_field##_POS)


#define BT_MBOX_PRINT(_num, _field, _end)				    \
			pos += scnprintf(buf + pos, bufsz - pos,	    \
					 "\t%s: %d%s",			    \
					 #_field,			    \
					 BT_MBOX_MSG(notif, _num, _field),  \
					 true ? "\n" : ", ");

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

static
int iwl_mvm_coex_dump_mbox_old(struct iwl_bt_coex_profile_notif_old *notif,
			       char *buf, int pos, int bufsz)
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
	char *buf;
	int ret, pos = 0, bufsz = sizeof(char) * 1024;

	buf = kmalloc(bufsz, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	mutex_lock(&mvm->mutex);

	if (!fw_has_api(&mvm->fw->ucode_capa,
			IWL_UCODE_TLV_API_BT_COEX_SPLIT)) {
		struct iwl_bt_coex_profile_notif_old *notif =
			&mvm->last_bt_notif_old;

		pos += iwl_mvm_coex_dump_mbox_old(notif, buf, pos, bufsz);

		pos += scnprintf(buf+pos, bufsz-pos, "bt_ci_compliance = %d\n",
				 notif->bt_ci_compliance);
		pos += scnprintf(buf+pos, bufsz-pos, "primary_ch_lut = %d\n",
				 le32_to_cpu(notif->primary_ch_lut));
		pos += scnprintf(buf+pos, bufsz-pos, "secondary_ch_lut = %d\n",
				 le32_to_cpu(notif->secondary_ch_lut));
		pos += scnprintf(buf+pos,
				 bufsz-pos, "bt_activity_grading = %d\n",
				 le32_to_cpu(notif->bt_activity_grading));
		pos += scnprintf(buf+pos, bufsz-pos,
				 "antenna isolation = %d CORUN LUT index = %d\n",
				 mvm->last_ant_isol, mvm->last_corun_lut);
		pos += scnprintf(buf + pos, bufsz - pos, "bt_rrc = %d\n",
				 notif->rrc_enabled);
		pos += scnprintf(buf + pos, bufsz - pos, "bt_ttc = %d\n",
				 notif->ttc_enabled);
	} else {
		struct iwl_bt_coex_profile_notif *notif =
			&mvm->last_bt_notif;

		pos += iwl_mvm_coex_dump_mbox(notif, buf, pos, bufsz);

		pos += scnprintf(buf+pos, bufsz-pos, "bt_ci_compliance = %d\n",
				 notif->bt_ci_compliance);
		pos += scnprintf(buf+pos, bufsz-pos, "primary_ch_lut = %d\n",
				 le32_to_cpu(notif->primary_ch_lut));
		pos += scnprintf(buf+pos, bufsz-pos, "secondary_ch_lut = %d\n",
				 le32_to_cpu(notif->secondary_ch_lut));
		pos += scnprintf(buf+pos,
				 bufsz-pos, "bt_activity_grading = %d\n",
				 le32_to_cpu(notif->bt_activity_grading));
		pos += scnprintf(buf+pos, bufsz-pos,
				 "antenna isolation = %d CORUN LUT index = %d\n",
				 mvm->last_ant_isol, mvm->last_corun_lut);
		pos += scnprintf(buf + pos, bufsz - pos, "bt_rrc = %d\n",
				 (notif->ttc_rrc_status >> 4) & 0xF);
		pos += scnprintf(buf + pos, bufsz - pos, "bt_ttc = %d\n",
				 notif->ttc_rrc_status & 0xF);
	}

	pos += scnprintf(buf + pos, bufsz - pos, "sync_sco = %d\n",
			 IWL_MVM_BT_COEX_SYNC2SCO);
	pos += scnprintf(buf + pos, bufsz - pos, "mplut = %d\n",
			 IWL_MVM_BT_COEX_MPLUT);
	pos += scnprintf(buf + pos, bufsz - pos, "corunning = %d\n",
			 IWL_MVM_BT_COEX_CORUNNING);

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
	char buf[256];
	int bufsz = sizeof(buf);
	int pos = 0;

	mutex_lock(&mvm->mutex);

	if (!fw_has_api(&mvm->fw->ucode_capa,
			IWL_UCODE_TLV_API_BT_COEX_SPLIT)) {
		struct iwl_bt_coex_ci_cmd_old *cmd = &mvm->last_bt_ci_cmd_old;

		pos += scnprintf(buf+pos, bufsz-pos,
				 "Channel inhibition CMD\n");
		pos += scnprintf(buf+pos, bufsz-pos,
			       "\tPrimary Channel Bitmap 0x%016llx\n",
			       le64_to_cpu(cmd->bt_primary_ci));
		pos += scnprintf(buf+pos, bufsz-pos,
			       "\tSecondary Channel Bitmap 0x%016llx\n",
			       le64_to_cpu(cmd->bt_secondary_ci));

		pos += scnprintf(buf+pos, bufsz-pos,
				 "BT Configuration CMD - 0=default, 1=never, 2=always\n");
		pos += scnprintf(buf+pos, bufsz-pos, "\tACK Kill msk idx %d\n",
				 mvm->bt_ack_kill_msk[0]);
		pos += scnprintf(buf+pos, bufsz-pos, "\tCTS Kill msk idx %d\n",
				 mvm->bt_cts_kill_msk[0]);

	} else {
		struct iwl_bt_coex_ci_cmd *cmd = &mvm->last_bt_ci_cmd;

		pos += scnprintf(buf+pos, bufsz-pos,
				 "Channel inhibition CMD\n");
		pos += scnprintf(buf+pos, bufsz-pos,
			       "\tPrimary Channel Bitmap 0x%016llx\n",
			       le64_to_cpu(cmd->bt_primary_ci));
		pos += scnprintf(buf+pos, bufsz-pos,
			       "\tSecondary Channel Bitmap 0x%016llx\n",
			       le64_to_cpu(cmd->bt_secondary_ci));
	}

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

	for (bt_force_ant_mode = 0;
	     bt_force_ant_mode < ARRAY_SIZE(modes_str);
	     bt_force_ant_mode++) {
		if (!strcmp(buf, modes_str[bt_force_ant_mode]))
			break;
	}

	if (bt_force_ant_mode >= ARRAY_SIZE(modes_str))
		return -EINVAL;

	ret = 0;
	mutex_lock(&mvm->mutex);
	if (mvm->bt_force_ant_mode == bt_force_ant_mode)
		goto out;

	mvm->bt_force_ant_mode = bt_force_ant_mode;
	IWL_DEBUG_COEX(mvm, "Force mode: %s\n",
		       modes_str[mvm->bt_force_ant_mode]);
	ret = iwl_send_bt_init_conf(mvm);

out:
	mutex_unlock(&mvm->mutex);
	return ret ?: count;
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

	pos += scnprintf(buf + pos, bufsz - pos, fmt_header,
			 "Statistics_Rx - CCK");
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

	pos += scnprintf(buf + pos, bufsz - pos, fmt_header,
			 "Statistics_Rx - GENERAL");
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

	pos += scnprintf(buf + pos, bufsz - pos, fmt_header,
			 "Statistics_Rx - HT");
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
		pos += rs_pretty_print_rate(pos, stats->last_rates[idx]);
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
	int ret;

	mutex_lock(&mvm->mutex);

	/* allow one more restart that we're provoking here */
	if (mvm->restart_fw >= 0)
		mvm->restart_fw++;

	/* take the return value to make compiler happy - it will fail anyway */
	ret = iwl_mvm_send_cmd_pdu(mvm, REPLY_ERROR, 0, 0, NULL);

	mutex_unlock(&mvm->mutex);

	return count;
}

static ssize_t iwl_dbgfs_fw_nmi_write(struct iwl_mvm *mvm, char *buf,
				      size_t count, loff_t *ppos)
{
	int ret = iwl_mvm_ref_sync(mvm, IWL_MVM_REF_NMI);
	if (ret)
		return ret;

	iwl_force_nmi(mvm->trans);

	iwl_mvm_unref(mvm, IWL_MVM_REF_NMI);

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
	conf = mvm->fw_dbg_conf;
	mutex_unlock(&mvm->mutex);

	pos += scnprintf(buf + pos, bufsz - pos, "%d\n", conf);

	return simple_read_from_buffer(user_buf, count, ppos, buf, pos);
}

/*
 * Enable / Disable continuous recording.
 * Cause the FW to start continuous recording, by sending the relevant hcmd.
 * Enable: input of every integer larger than 0, ENABLE_CONT_RECORDING.
 * Disable: for 0 as input, DISABLE_CONT_RECORDING.
 */
static ssize_t iwl_dbgfs_cont_recording_write(struct iwl_mvm *mvm,
					      char *buf, size_t count,
					      loff_t *ppos)
{
	struct iwl_trans *trans = mvm->trans;
	const struct iwl_fw_dbg_dest_tlv *dest = trans->dbg_dest_tlv;
	struct iwl_continuous_record_cmd cont_rec = {};
	int ret, rec_mode;

	if (!dest)
		return -EOPNOTSUPP;

	if (dest->monitor_mode != SMEM_MODE ||
	    trans->cfg->device_family != IWL_DEVICE_FAMILY_8000)
		return -EOPNOTSUPP;

	ret = kstrtouint(buf, 0, &rec_mode);
	if (ret)
		return ret;

	cont_rec.record_mode.enable_recording = rec_mode ?
		cpu_to_le16(ENABLE_CONT_RECORDING) :
		cpu_to_le16(DISABLE_CONT_RECORDING);

	mutex_lock(&mvm->mutex);
	ret = iwl_mvm_send_cmd_pdu(mvm, LDBG_CONFIG_CMD, 0,
				   sizeof(cont_rec), &cont_rec);
	mutex_unlock(&mvm->mutex);

	return ret ?: count;
}

static ssize_t iwl_dbgfs_fw_dbg_conf_write(struct iwl_mvm *mvm,
					   char *buf, size_t count,
					   loff_t *ppos)
{
	unsigned int conf_id;
	int ret;

	ret = kstrtouint(buf, 0, &conf_id);
	if (ret)
		return ret;

	if (WARN_ON(conf_id >= FW_DBG_CONF_MAX))
		return -EINVAL;

	mutex_lock(&mvm->mutex);
	ret = iwl_mvm_start_fw_dbg_conf(mvm, conf_id);
	mutex_unlock(&mvm->mutex);

	return ret ?: count;
}

static ssize_t iwl_dbgfs_fw_dbg_collect_write(struct iwl_mvm *mvm,
					      char *buf, size_t count,
					      loff_t *ppos)
{
	int ret = iwl_mvm_ref_sync(mvm, IWL_MVM_REF_PRPH_WRITE);

	if (ret)
		return ret;

	iwl_mvm_fw_dbg_collect(mvm, FW_DBG_TRIGGER_USER, NULL, 0, NULL);

	iwl_mvm_unref(mvm, IWL_MVM_REF_PRPH_WRITE);

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
	if (mvm->dbgfs_bcast_filtering.override &&
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
	if (mvm->dbgfs_bcast_filtering.override &&
	    iwl_mvm_bcast_filter_build_cmd(mvm, &cmd))
		err = iwl_mvm_send_cmd_pdu(mvm, BCAST_FILTER_CMD, 0,
					   sizeof(cmd), &cmd);
	mutex_unlock(&mvm->mutex);

	return err ?: count;
}
#endif

#ifdef CONFIG_PM_SLEEP
static ssize_t iwl_dbgfs_d3_sram_write(struct iwl_mvm *mvm, char *buf,
				       size_t count, loff_t *ppos)
{
	int store;

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
					 "0x%.4x %16ph\n", ofs, ptr + ofs);
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

#define PRINT_MVM_REF(ref) do {						\
	if (mvm->refs[ref])						\
		pos += scnprintf(buf + pos, bufsz - pos,		\
				 "\t(0x%lx): %d %s\n",			\
				 BIT(ref), mvm->refs[ref], #ref);	\
} while (0)

static ssize_t iwl_dbgfs_d0i3_refs_read(struct file *file,
					char __user *user_buf,
					size_t count, loff_t *ppos)
{
	struct iwl_mvm *mvm = file->private_data;
	int i, pos = 0;
	char buf[256];
	const size_t bufsz = sizeof(buf);
	u32 refs = 0;

	for (i = 0; i < IWL_MVM_REF_COUNT; i++)
		if (mvm->refs[i])
			refs |= BIT(i);

	pos += scnprintf(buf + pos, bufsz - pos, "taken mvm refs: 0x%x\n",
			 refs);

	PRINT_MVM_REF(IWL_MVM_REF_UCODE_DOWN);
	PRINT_MVM_REF(IWL_MVM_REF_SCAN);
	PRINT_MVM_REF(IWL_MVM_REF_ROC);
	PRINT_MVM_REF(IWL_MVM_REF_ROC_AUX);
	PRINT_MVM_REF(IWL_MVM_REF_P2P_CLIENT);
	PRINT_MVM_REF(IWL_MVM_REF_AP_IBSS);
	PRINT_MVM_REF(IWL_MVM_REF_USER);
	PRINT_MVM_REF(IWL_MVM_REF_TX);
	PRINT_MVM_REF(IWL_MVM_REF_TX_AGG);
	PRINT_MVM_REF(IWL_MVM_REF_ADD_IF);
	PRINT_MVM_REF(IWL_MVM_REF_START_AP);
	PRINT_MVM_REF(IWL_MVM_REF_BSS_CHANGED);
	PRINT_MVM_REF(IWL_MVM_REF_PREPARE_TX);
	PRINT_MVM_REF(IWL_MVM_REF_PROTECT_TDLS);
	PRINT_MVM_REF(IWL_MVM_REF_CHECK_CTKILL);
	PRINT_MVM_REF(IWL_MVM_REF_PRPH_READ);
	PRINT_MVM_REF(IWL_MVM_REF_PRPH_WRITE);
	PRINT_MVM_REF(IWL_MVM_REF_NMI);
	PRINT_MVM_REF(IWL_MVM_REF_TM_CMD);
	PRINT_MVM_REF(IWL_MVM_REF_EXIT_WORK);
	PRINT_MVM_REF(IWL_MVM_REF_PROTECT_CSA);
	PRINT_MVM_REF(IWL_MVM_REF_FW_DBG_COLLECT);

	return simple_read_from_buffer(user_buf, count, ppos, buf, pos);
}

static ssize_t iwl_dbgfs_d0i3_refs_write(struct iwl_mvm *mvm, char *buf,
					 size_t count, loff_t *ppos)
{
	unsigned long value;
	int ret;
	bool taken;

	ret = kstrtoul(buf, 10, &value);
	if (ret < 0)
		return ret;

	mutex_lock(&mvm->mutex);

	taken = mvm->refs[IWL_MVM_REF_USER];
	if (value == 1 && !taken)
		iwl_mvm_ref(mvm, IWL_MVM_REF_USER);
	else if (value == 0 && taken)
		iwl_mvm_unref(mvm, IWL_MVM_REF_USER);
	else
		ret = -EINVAL;

	mutex_unlock(&mvm->mutex);

	if (ret < 0)
		return ret;
	return count;
}

#define MVM_DEBUGFS_WRITE_FILE_OPS(name, bufsz) \
	_MVM_DEBUGFS_WRITE_FILE_OPS(name, bufsz, struct iwl_mvm)
#define MVM_DEBUGFS_READ_WRITE_FILE_OPS(name, bufsz) \
	_MVM_DEBUGFS_READ_WRITE_FILE_OPS(name, bufsz, struct iwl_mvm)
#define MVM_DEBUGFS_ADD_FILE_ALIAS(alias, name, parent, mode) do {	\
		if (!debugfs_create_file(alias, mode, parent, mvm,	\
					 &iwl_dbgfs_##name##_ops))	\
			goto err;					\
	} while (0)
#define MVM_DEBUGFS_ADD_FILE(name, parent, mode) \
	MVM_DEBUGFS_ADD_FILE_ALIAS(#name, name, parent, mode)

static ssize_t
iwl_dbgfs_prph_reg_read(struct file *file,
			char __user *user_buf,
			size_t count, loff_t *ppos)
{
	struct iwl_mvm *mvm = file->private_data;
	int pos = 0;
	char buf[32];
	const size_t bufsz = sizeof(buf);
	int ret;

	if (!mvm->dbgfs_prph_reg_addr)
		return -EINVAL;

	ret = iwl_mvm_ref_sync(mvm, IWL_MVM_REF_PRPH_READ);
	if (ret)
		return ret;

	pos += scnprintf(buf + pos, bufsz - pos, "Reg 0x%x: (0x%x)\n",
		mvm->dbgfs_prph_reg_addr,
		iwl_read_prph(mvm->trans, mvm->dbgfs_prph_reg_addr));

	iwl_mvm_unref(mvm, IWL_MVM_REF_PRPH_READ);

	return simple_read_from_buffer(user_buf, count, ppos, buf, pos);
}

static ssize_t
iwl_dbgfs_prph_reg_write(struct iwl_mvm *mvm, char *buf,
			 size_t count, loff_t *ppos)
{
	u8 args;
	u32 value;
	int ret;

	args = sscanf(buf, "%i %i", &mvm->dbgfs_prph_reg_addr, &value);
	/* if we only want to set the reg address - nothing more to do */
	if (args == 1)
		goto out;

	/* otherwise, make sure we have both address and value */
	if (args != 2)
		return -EINVAL;

	ret = iwl_mvm_ref_sync(mvm, IWL_MVM_REF_PRPH_WRITE);
	if (ret)
		return ret;

	iwl_write_prph(mvm->trans, mvm->dbgfs_prph_reg_addr, value);

	iwl_mvm_unref(mvm, IWL_MVM_REF_PRPH_WRITE);
out:
	return count;
}

static ssize_t
iwl_dbgfs_send_echo_cmd_write(struct iwl_mvm *mvm, char *buf,
			      size_t count, loff_t *ppos)
{
	int ret;

	mutex_lock(&mvm->mutex);
	ret = iwl_mvm_send_cmd_pdu(mvm, ECHO_CMD, 0, 0, NULL);
	mutex_unlock(&mvm->mutex);

	return ret ?: count;
}

MVM_DEBUGFS_READ_WRITE_FILE_OPS(prph_reg, 64);

/* Device wide debugfs entries */
MVM_DEBUGFS_WRITE_FILE_OPS(tx_flush, 16);
MVM_DEBUGFS_WRITE_FILE_OPS(sta_drain, 8);
MVM_DEBUGFS_WRITE_FILE_OPS(send_echo_cmd, 8);
MVM_DEBUGFS_READ_WRITE_FILE_OPS(sram, 64);
MVM_DEBUGFS_READ_WRITE_FILE_OPS(set_nic_temperature, 64);
MVM_DEBUGFS_READ_FILE_OPS(nic_temp);
MVM_DEBUGFS_READ_FILE_OPS(stations);
MVM_DEBUGFS_READ_FILE_OPS(bt_notif);
MVM_DEBUGFS_READ_FILE_OPS(bt_cmd);
MVM_DEBUGFS_READ_WRITE_FILE_OPS(disable_power_off, 64);
MVM_DEBUGFS_READ_FILE_OPS(fw_rx_stats);
MVM_DEBUGFS_READ_FILE_OPS(drv_rx_stats);
MVM_DEBUGFS_WRITE_FILE_OPS(fw_restart, 10);
MVM_DEBUGFS_WRITE_FILE_OPS(fw_nmi, 10);
MVM_DEBUGFS_WRITE_FILE_OPS(bt_tx_prio, 10);
MVM_DEBUGFS_WRITE_FILE_OPS(bt_force_ant, 10);
MVM_DEBUGFS_READ_WRITE_FILE_OPS(scan_ant_rxchain, 8);
MVM_DEBUGFS_READ_WRITE_FILE_OPS(d0i3_refs, 8);
MVM_DEBUGFS_READ_WRITE_FILE_OPS(fw_dbg_conf, 8);
MVM_DEBUGFS_WRITE_FILE_OPS(fw_dbg_collect, 8);
MVM_DEBUGFS_WRITE_FILE_OPS(cont_recording, 8);

#ifdef CONFIG_IWLWIFI_BCAST_FILTERING
MVM_DEBUGFS_READ_WRITE_FILE_OPS(bcast_filters, 256);
MVM_DEBUGFS_READ_WRITE_FILE_OPS(bcast_filters_macs, 256);
#endif

#ifdef CONFIG_PM_SLEEP
MVM_DEBUGFS_READ_WRITE_FILE_OPS(d3_sram, 8);
#endif

int iwl_mvm_dbgfs_register(struct iwl_mvm *mvm, struct dentry *dbgfs_dir)
{
	struct dentry *bcast_dir __maybe_unused;
	char buf[100];

	spin_lock_init(&mvm->drv_stats_lock);

	mvm->debugfs_dir = dbgfs_dir;

	MVM_DEBUGFS_ADD_FILE(tx_flush, mvm->debugfs_dir, S_IWUSR);
	MVM_DEBUGFS_ADD_FILE(sta_drain, mvm->debugfs_dir, S_IWUSR);
	MVM_DEBUGFS_ADD_FILE(sram, mvm->debugfs_dir, S_IWUSR | S_IRUSR);
	MVM_DEBUGFS_ADD_FILE(set_nic_temperature, mvm->debugfs_dir,
			     S_IWUSR | S_IRUSR);
	MVM_DEBUGFS_ADD_FILE(nic_temp, dbgfs_dir, S_IRUSR);
	MVM_DEBUGFS_ADD_FILE(stations, dbgfs_dir, S_IRUSR);
	MVM_DEBUGFS_ADD_FILE(bt_notif, dbgfs_dir, S_IRUSR);
	MVM_DEBUGFS_ADD_FILE(bt_cmd, dbgfs_dir, S_IRUSR);
	MVM_DEBUGFS_ADD_FILE(disable_power_off, mvm->debugfs_dir,
			     S_IRUSR | S_IWUSR);
	MVM_DEBUGFS_ADD_FILE(fw_rx_stats, mvm->debugfs_dir, S_IRUSR);
	MVM_DEBUGFS_ADD_FILE(drv_rx_stats, mvm->debugfs_dir, S_IRUSR);
	MVM_DEBUGFS_ADD_FILE(fw_restart, mvm->debugfs_dir, S_IWUSR);
	MVM_DEBUGFS_ADD_FILE(fw_nmi, mvm->debugfs_dir, S_IWUSR);
	MVM_DEBUGFS_ADD_FILE(bt_tx_prio, mvm->debugfs_dir, S_IWUSR);
	MVM_DEBUGFS_ADD_FILE(bt_force_ant, mvm->debugfs_dir, S_IWUSR);
	MVM_DEBUGFS_ADD_FILE(scan_ant_rxchain, mvm->debugfs_dir,
			     S_IWUSR | S_IRUSR);
	MVM_DEBUGFS_ADD_FILE(prph_reg, mvm->debugfs_dir, S_IWUSR | S_IRUSR);
	MVM_DEBUGFS_ADD_FILE(d0i3_refs, mvm->debugfs_dir, S_IRUSR | S_IWUSR);
	MVM_DEBUGFS_ADD_FILE(fw_dbg_conf, mvm->debugfs_dir, S_IRUSR | S_IWUSR);
	MVM_DEBUGFS_ADD_FILE(fw_dbg_collect, mvm->debugfs_dir, S_IWUSR);
	MVM_DEBUGFS_ADD_FILE(send_echo_cmd, mvm->debugfs_dir, S_IWUSR);
	MVM_DEBUGFS_ADD_FILE(cont_recording, mvm->debugfs_dir, S_IWUSR);
	if (!debugfs_create_bool("enable_scan_iteration_notif",
				 S_IRUSR | S_IWUSR,
				 mvm->debugfs_dir,
				 &mvm->scan_iter_notif_enabled))
		goto err;

#ifdef CONFIG_IWLWIFI_BCAST_FILTERING
	if (mvm->fw->ucode_capa.flags & IWL_UCODE_TLV_FLAGS_BCAST_FILTERING) {
		bcast_dir = debugfs_create_dir("bcast_filtering",
					       mvm->debugfs_dir);
		if (!bcast_dir)
			goto err;

		if (!debugfs_create_bool("override", S_IRUSR | S_IWUSR,
				bcast_dir,
				&mvm->dbgfs_bcast_filtering.override))
			goto err;

		MVM_DEBUGFS_ADD_FILE_ALIAS("filters", bcast_filters,
					   bcast_dir, S_IWUSR | S_IRUSR);
		MVM_DEBUGFS_ADD_FILE_ALIAS("macs", bcast_filters_macs,
					   bcast_dir, S_IWUSR | S_IRUSR);
	}
#endif

#ifdef CONFIG_PM_SLEEP
	MVM_DEBUGFS_ADD_FILE(d3_sram, mvm->debugfs_dir, S_IRUSR | S_IWUSR);
	MVM_DEBUGFS_ADD_FILE(d3_test, mvm->debugfs_dir, S_IRUSR);
	if (!debugfs_create_bool("d3_wake_sysassert", S_IRUSR | S_IWUSR,
				 mvm->debugfs_dir, &mvm->d3_wake_sysassert))
		goto err;
	if (!debugfs_create_u32("last_netdetect_scans", S_IRUSR,
				mvm->debugfs_dir, &mvm->last_netdetect_scans))
		goto err;
#endif

	if (!debugfs_create_u8("ps_disabled", S_IRUSR,
			       mvm->debugfs_dir, &mvm->ps_disabled))
		goto err;
	if (!debugfs_create_blob("nvm_hw", S_IRUSR,
				  mvm->debugfs_dir, &mvm->nvm_hw_blob))
		goto err;
	if (!debugfs_create_blob("nvm_sw", S_IRUSR,
				  mvm->debugfs_dir, &mvm->nvm_sw_blob))
		goto err;
	if (!debugfs_create_blob("nvm_calib", S_IRUSR,
				  mvm->debugfs_dir, &mvm->nvm_calib_blob))
		goto err;
	if (!debugfs_create_blob("nvm_prod", S_IRUSR,
				  mvm->debugfs_dir, &mvm->nvm_prod_blob))
		goto err;
	if (!debugfs_create_blob("nvm_phy_sku", S_IRUSR,
				 mvm->debugfs_dir, &mvm->nvm_phy_sku_blob))
		goto err;

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
