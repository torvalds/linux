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

	/* default is to dump the entire data segment */
	if (!mvm->dbgfs_sram_offset && !mvm->dbgfs_sram_len) {
		mvm->dbgfs_sram_offset = 0x800000;
		if (!mvm->ucode_loaded)
			return -EINVAL;
		img = &mvm->fw->img[mvm->cur_ucode];
		mvm->dbgfs_sram_len = img->sec[IWL_UCODE_SECTION_DATA].len;
	}
	len = mvm->dbgfs_sram_len;

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
	pos += scnprintf(buf + pos, bufsz - pos, "sram_offset: 0x%x\n",
			 mvm->dbgfs_sram_offset);

	iwl_trans_read_mem_bytes(mvm->trans,
				 mvm->dbgfs_sram_offset,
				 ptr, len);
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

static ssize_t iwl_dbgfs_power_down_allow_write(struct file *file,
						const char __user *user_buf,
						size_t count, loff_t *ppos)
{
	struct iwl_mvm *mvm = file->private_data;
	char buf[8] = {};
	int allow;

	if (!mvm->ucode_loaded)
		return -EIO;

	if (copy_from_user(buf, user_buf, sizeof(buf)))
		return -EFAULT;

	if (sscanf(buf, "%d", &allow) != 1)
		return -EINVAL;

	IWL_DEBUG_POWER(mvm, "%s device power down\n",
			allow ? "allow" : "prevent");

	/*
	 * TODO: Send REPLY_DEBUG_CMD (0xf0) when FW support it
	 */

	return count;
}

static ssize_t iwl_dbgfs_power_down_d3_allow_write(struct file *file,
						   const char __user *user_buf,
						   size_t count, loff_t *ppos)
{
	struct iwl_mvm *mvm = file->private_data;
	char buf[8] = {};
	int allow;

	if (copy_from_user(buf, user_buf, sizeof(buf)))
		return -EFAULT;

	if (sscanf(buf, "%d", &allow) != 1)
		return -EINVAL;

	IWL_DEBUG_POWER(mvm, "%s device power down in d3\n",
			allow ? "allow" : "prevent");

	/*
	 * TODO: When WoWLAN FW alive notification happens, driver will send
	 * REPLY_DEBUG_CMD setting power_down_allow flag according to
	 * mvm->prevent_power_down_d3
	 */
	mvm->prevent_power_down_d3 = !allow;

	return count;
}

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
MVM_DEBUGFS_WRITE_FILE_OPS(power_down_allow);
MVM_DEBUGFS_WRITE_FILE_OPS(power_down_d3_allow);

int iwl_mvm_dbgfs_register(struct iwl_mvm *mvm, struct dentry *dbgfs_dir)
{
	char buf[100];

	mvm->debugfs_dir = dbgfs_dir;

	MVM_DEBUGFS_ADD_FILE(tx_flush, mvm->debugfs_dir, S_IWUSR);
	MVM_DEBUGFS_ADD_FILE(sta_drain, mvm->debugfs_dir, S_IWUSR);
	MVM_DEBUGFS_ADD_FILE(sram, mvm->debugfs_dir, S_IWUSR | S_IRUSR);
	MVM_DEBUGFS_ADD_FILE(stations, dbgfs_dir, S_IRUSR);
	MVM_DEBUGFS_ADD_FILE(power_down_allow, mvm->debugfs_dir, S_IWUSR);
	MVM_DEBUGFS_ADD_FILE(power_down_d3_allow, mvm->debugfs_dir, S_IWUSR);

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
