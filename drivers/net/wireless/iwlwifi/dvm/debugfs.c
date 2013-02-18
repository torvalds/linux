/******************************************************************************
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
 * in the file called COPYING.
 *
 * Contact Information:
 *  Intel Linux Wireless <ilw@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *****************************************************************************/

#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/debugfs.h>
#include <linux/ieee80211.h>
#include <net/mac80211.h>
#include "iwl-debug.h"
#include "iwl-io.h"
#include "dev.h"
#include "agn.h"

/* create and remove of files */
#define DEBUGFS_ADD_FILE(name, parent, mode) do {			\
	if (!debugfs_create_file(#name, mode, parent, priv,		\
				 &iwl_dbgfs_##name##_ops))		\
		goto err;						\
} while (0)

#define DEBUGFS_ADD_BOOL(name, parent, ptr) do {			\
	struct dentry *__tmp;						\
	__tmp = debugfs_create_bool(#name, S_IWUSR | S_IRUSR,		\
				    parent, ptr);			\
	if (IS_ERR(__tmp) || !__tmp)					\
		goto err;						\
} while (0)

#define DEBUGFS_ADD_X32(name, parent, ptr) do {				\
	struct dentry *__tmp;						\
	__tmp = debugfs_create_x32(#name, S_IWUSR | S_IRUSR,		\
				   parent, ptr);			\
	if (IS_ERR(__tmp) || !__tmp)					\
		goto err;						\
} while (0)

#define DEBUGFS_ADD_U32(name, parent, ptr, mode) do {			\
	struct dentry *__tmp;						\
	__tmp = debugfs_create_u32(#name, mode,				\
				   parent, ptr);			\
	if (IS_ERR(__tmp) || !__tmp)					\
		goto err;						\
} while (0)

/* file operation */
#define DEBUGFS_READ_FUNC(name)                                         \
static ssize_t iwl_dbgfs_##name##_read(struct file *file,               \
					char __user *user_buf,          \
					size_t count, loff_t *ppos);

#define DEBUGFS_WRITE_FUNC(name)                                        \
static ssize_t iwl_dbgfs_##name##_write(struct file *file,              \
					const char __user *user_buf,    \
					size_t count, loff_t *ppos);


#define DEBUGFS_READ_FILE_OPS(name)                                     \
	DEBUGFS_READ_FUNC(name);                                        \
static const struct file_operations iwl_dbgfs_##name##_ops = {          \
	.read = iwl_dbgfs_##name##_read,				\
	.open = simple_open,						\
	.llseek = generic_file_llseek,					\
};

#define DEBUGFS_WRITE_FILE_OPS(name)                                    \
	DEBUGFS_WRITE_FUNC(name);                                       \
static const struct file_operations iwl_dbgfs_##name##_ops = {          \
	.write = iwl_dbgfs_##name##_write,                              \
	.open = simple_open,						\
	.llseek = generic_file_llseek,					\
};


#define DEBUGFS_READ_WRITE_FILE_OPS(name)                               \
	DEBUGFS_READ_FUNC(name);                                        \
	DEBUGFS_WRITE_FUNC(name);                                       \
static const struct file_operations iwl_dbgfs_##name##_ops = {          \
	.write = iwl_dbgfs_##name##_write,                              \
	.read = iwl_dbgfs_##name##_read,                                \
	.open = simple_open,						\
	.llseek = generic_file_llseek,					\
};

static ssize_t iwl_dbgfs_sram_read(struct file *file,
					char __user *user_buf,
					size_t count, loff_t *ppos)
{
	u32 val = 0;
	char *buf;
	ssize_t ret;
	int i = 0;
	bool device_format = false;
	int offset = 0;
	int len = 0;
	int pos = 0;
	int sram;
	struct iwl_priv *priv = file->private_data;
	const struct fw_img *img;
	size_t bufsz;

	if (!iwl_is_ready_rf(priv))
		return -EAGAIN;

	/* default is to dump the entire data segment */
	if (!priv->dbgfs_sram_offset && !priv->dbgfs_sram_len) {
		priv->dbgfs_sram_offset = 0x800000;
		if (!priv->ucode_loaded)
			return -EINVAL;
		img = &priv->fw->img[priv->cur_ucode];
		priv->dbgfs_sram_len = img->sec[IWL_UCODE_SECTION_DATA].len;
	}
	len = priv->dbgfs_sram_len;

	if (len == -4) {
		device_format = true;
		len = 4;
	}

	bufsz =  50 + len * 4;
	buf = kmalloc(bufsz, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	pos += scnprintf(buf + pos, bufsz - pos, "sram_len: 0x%x\n",
			 len);
	pos += scnprintf(buf + pos, bufsz - pos, "sram_offset: 0x%x\n",
			priv->dbgfs_sram_offset);

	/* adjust sram address since reads are only on even u32 boundaries */
	offset = priv->dbgfs_sram_offset & 0x3;
	sram = priv->dbgfs_sram_offset & ~0x3;

	/* read the first u32 from sram */
	val = iwl_trans_read_mem32(priv->trans, sram);

	for (; len; len--) {
		/* put the address at the start of every line */
		if (i == 0)
			pos += scnprintf(buf + pos, bufsz - pos,
				"%08X: ", sram + offset);

		if (device_format)
			pos += scnprintf(buf + pos, bufsz - pos,
				"%02x", (val >> (8 * (3 - offset))) & 0xff);
		else
			pos += scnprintf(buf + pos, bufsz - pos,
				"%02x ", (val >> (8 * offset)) & 0xff);

		/* if all bytes processed, read the next u32 from sram */
		if (++offset == 4) {
			sram += 4;
			offset = 0;
			val = iwl_trans_read_mem32(priv->trans, sram);
		}

		/* put in extra spaces and split lines for human readability */
		if (++i == 16) {
			i = 0;
			pos += scnprintf(buf + pos, bufsz - pos, "\n");
		} else if (!(i & 7)) {
			pos += scnprintf(buf + pos, bufsz - pos, "   ");
		} else if (!(i & 3)) {
			pos += scnprintf(buf + pos, bufsz - pos, " ");
		}
	}
	if (i)
		pos += scnprintf(buf + pos, bufsz - pos, "\n");

	ret = simple_read_from_buffer(user_buf, count, ppos, buf, pos);
	kfree(buf);
	return ret;
}

static ssize_t iwl_dbgfs_sram_write(struct file *file,
					const char __user *user_buf,
					size_t count, loff_t *ppos)
{
	struct iwl_priv *priv = file->private_data;
	char buf[64];
	int buf_size;
	u32 offset, len;

	memset(buf, 0, sizeof(buf));
	buf_size = min(count, sizeof(buf) -  1);
	if (copy_from_user(buf, user_buf, buf_size))
		return -EFAULT;

	if (sscanf(buf, "%x,%x", &offset, &len) == 2) {
		priv->dbgfs_sram_offset = offset;
		priv->dbgfs_sram_len = len;
	} else if (sscanf(buf, "%x", &offset) == 1) {
		priv->dbgfs_sram_offset = offset;
		priv->dbgfs_sram_len = -4;
	} else {
		priv->dbgfs_sram_offset = 0;
		priv->dbgfs_sram_len = 0;
	}

	return count;
}

static ssize_t iwl_dbgfs_wowlan_sram_read(struct file *file,
					  char __user *user_buf,
					  size_t count, loff_t *ppos)
{
	struct iwl_priv *priv = file->private_data;
	const struct fw_img *img = &priv->fw->img[IWL_UCODE_WOWLAN];

	if (!priv->wowlan_sram)
		return -ENODATA;

	return simple_read_from_buffer(user_buf, count, ppos,
				       priv->wowlan_sram,
				       img->sec[IWL_UCODE_SECTION_DATA].len);
}
static ssize_t iwl_dbgfs_stations_read(struct file *file, char __user *user_buf,
					size_t count, loff_t *ppos)
{
	struct iwl_priv *priv = file->private_data;
	struct iwl_station_entry *station;
	struct iwl_tid_data *tid_data;
	char *buf;
	int i, j, pos = 0;
	ssize_t ret;
	/* Add 30 for initial string */
	const size_t bufsz = 30 + sizeof(char) * 500 * (priv->num_stations);

	buf = kmalloc(bufsz, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	pos += scnprintf(buf + pos, bufsz - pos, "num of stations: %d\n\n",
			priv->num_stations);

	for (i = 0; i < IWLAGN_STATION_COUNT; i++) {
		station = &priv->stations[i];
		if (!station->used)
			continue;
		pos += scnprintf(buf + pos, bufsz - pos,
				 "station %d - addr: %pM, flags: %#x\n",
				 i, station->sta.sta.addr,
				 station->sta.station_flags_msk);
		pos += scnprintf(buf + pos, bufsz - pos,
				"TID seqno  next_rclmd "
				"rate_n_flags state txq\n");

		for (j = 0; j < IWL_MAX_TID_COUNT; j++) {
			tid_data = &priv->tid_data[i][j];
			pos += scnprintf(buf + pos, bufsz - pos,
				"%d:  0x%.4x 0x%.4x     0x%.8x   "
				"%d     %.2d",
				j, tid_data->seq_number,
				tid_data->next_reclaimed,
				tid_data->agg.rate_n_flags,
				tid_data->agg.state,
				tid_data->agg.txq_id);

			if (tid_data->agg.wait_for_ba)
				pos += scnprintf(buf + pos, bufsz - pos,
						 " - waitforba");
			pos += scnprintf(buf + pos, bufsz - pos, "\n");
		}

		pos += scnprintf(buf + pos, bufsz - pos, "\n");
	}

	ret = simple_read_from_buffer(user_buf, count, ppos, buf, pos);
	kfree(buf);
	return ret;
}

static ssize_t iwl_dbgfs_nvm_read(struct file *file,
				       char __user *user_buf,
				       size_t count,
				       loff_t *ppos)
{
	ssize_t ret;
	struct iwl_priv *priv = file->private_data;
	int pos = 0, ofs = 0, buf_size = 0;
	const u8 *ptr;
	char *buf;
	u16 nvm_ver;
	size_t eeprom_len = priv->eeprom_blob_size;
	buf_size = 4 * eeprom_len + 256;

	if (eeprom_len % 16)
		return -ENODATA;

	ptr = priv->eeprom_blob;
	if (!ptr)
		return -ENOMEM;

	/* 4 characters for byte 0xYY */
	buf = kzalloc(buf_size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	nvm_ver = priv->nvm_data->nvm_version;
	pos += scnprintf(buf + pos, buf_size - pos,
			 "NVM version: 0x%x\n", nvm_ver);
	for (ofs = 0 ; ofs < eeprom_len ; ofs += 16) {
		pos += scnprintf(buf + pos, buf_size - pos, "0x%.4x ", ofs);
		hex_dump_to_buffer(ptr + ofs, 16 , 16, 2, buf + pos,
				   buf_size - pos, 0);
		pos += strlen(buf + pos);
		if (buf_size - pos > 0)
			buf[pos++] = '\n';
	}

	ret = simple_read_from_buffer(user_buf, count, ppos, buf, pos);
	kfree(buf);
	return ret;
}

static ssize_t iwl_dbgfs_channels_read(struct file *file, char __user *user_buf,
				       size_t count, loff_t *ppos)
{
	struct iwl_priv *priv = file->private_data;
	struct ieee80211_channel *channels = NULL;
	const struct ieee80211_supported_band *supp_band = NULL;
	int pos = 0, i, bufsz = PAGE_SIZE;
	char *buf;
	ssize_t ret;

	buf = kzalloc(bufsz, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	supp_band = iwl_get_hw_mode(priv, IEEE80211_BAND_2GHZ);
	if (supp_band) {
		channels = supp_band->channels;

		pos += scnprintf(buf + pos, bufsz - pos,
				"Displaying %d channels in 2.4GHz band 802.11bg):\n",
				supp_band->n_channels);

		for (i = 0; i < supp_band->n_channels; i++)
			pos += scnprintf(buf + pos, bufsz - pos,
					"%d: %ddBm: BSS%s%s, %s.\n",
					channels[i].hw_value,
					channels[i].max_power,
					channels[i].flags & IEEE80211_CHAN_RADAR ?
					" (IEEE 802.11h required)" : "",
					((channels[i].flags & IEEE80211_CHAN_NO_IBSS)
					|| (channels[i].flags &
					IEEE80211_CHAN_RADAR)) ? "" :
					", IBSS",
					channels[i].flags &
					IEEE80211_CHAN_PASSIVE_SCAN ?
					"passive only" : "active/passive");
	}
	supp_band = iwl_get_hw_mode(priv, IEEE80211_BAND_5GHZ);
	if (supp_band) {
		channels = supp_band->channels;

		pos += scnprintf(buf + pos, bufsz - pos,
				"Displaying %d channels in 5.2GHz band (802.11a)\n",
				supp_band->n_channels);

		for (i = 0; i < supp_band->n_channels; i++)
			pos += scnprintf(buf + pos, bufsz - pos,
					"%d: %ddBm: BSS%s%s, %s.\n",
					channels[i].hw_value,
					channels[i].max_power,
					channels[i].flags & IEEE80211_CHAN_RADAR ?
					" (IEEE 802.11h required)" : "",
					((channels[i].flags & IEEE80211_CHAN_NO_IBSS)
					|| (channels[i].flags &
					IEEE80211_CHAN_RADAR)) ? "" :
					", IBSS",
					channels[i].flags &
					IEEE80211_CHAN_PASSIVE_SCAN ?
					"passive only" : "active/passive");
	}
	ret = simple_read_from_buffer(user_buf, count, ppos, buf, pos);
	kfree(buf);
	return ret;
}

static ssize_t iwl_dbgfs_status_read(struct file *file,
						char __user *user_buf,
						size_t count, loff_t *ppos) {

	struct iwl_priv *priv = file->private_data;
	char buf[512];
	int pos = 0;
	const size_t bufsz = sizeof(buf);

	pos += scnprintf(buf + pos, bufsz - pos, "STATUS_RF_KILL_HW:\t %d\n",
		test_bit(STATUS_RF_KILL_HW, &priv->status));
	pos += scnprintf(buf + pos, bufsz - pos, "STATUS_CT_KILL:\t\t %d\n",
		test_bit(STATUS_CT_KILL, &priv->status));
	pos += scnprintf(buf + pos, bufsz - pos, "STATUS_ALIVE:\t\t %d\n",
		test_bit(STATUS_ALIVE, &priv->status));
	pos += scnprintf(buf + pos, bufsz - pos, "STATUS_READY:\t\t %d\n",
		test_bit(STATUS_READY, &priv->status));
	pos += scnprintf(buf + pos, bufsz - pos, "STATUS_EXIT_PENDING:\t %d\n",
		test_bit(STATUS_EXIT_PENDING, &priv->status));
	pos += scnprintf(buf + pos, bufsz - pos, "STATUS_STATISTICS:\t %d\n",
		test_bit(STATUS_STATISTICS, &priv->status));
	pos += scnprintf(buf + pos, bufsz - pos, "STATUS_SCANNING:\t %d\n",
		test_bit(STATUS_SCANNING, &priv->status));
	pos += scnprintf(buf + pos, bufsz - pos, "STATUS_SCAN_ABORTING:\t %d\n",
		test_bit(STATUS_SCAN_ABORTING, &priv->status));
	pos += scnprintf(buf + pos, bufsz - pos, "STATUS_SCAN_HW:\t\t %d\n",
		test_bit(STATUS_SCAN_HW, &priv->status));
	pos += scnprintf(buf + pos, bufsz - pos, "STATUS_POWER_PMI:\t %d\n",
		test_bit(STATUS_POWER_PMI, &priv->status));
	pos += scnprintf(buf + pos, bufsz - pos, "STATUS_FW_ERROR:\t %d\n",
		test_bit(STATUS_FW_ERROR, &priv->status));
	return simple_read_from_buffer(user_buf, count, ppos, buf, pos);
}

static ssize_t iwl_dbgfs_rx_handlers_read(struct file *file,
					char __user *user_buf,
					size_t count, loff_t *ppos) {

	struct iwl_priv *priv = file->private_data;

	int pos = 0;
	int cnt = 0;
	char *buf;
	int bufsz = 24 * 64; /* 24 items * 64 char per item */
	ssize_t ret;

	buf = kzalloc(bufsz, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	for (cnt = 0; cnt < REPLY_MAX; cnt++) {
		if (priv->rx_handlers_stats[cnt] > 0)
			pos += scnprintf(buf + pos, bufsz - pos,
				"\tRx handler[%36s]:\t\t %u\n",
				iwl_dvm_get_cmd_string(cnt),
				priv->rx_handlers_stats[cnt]);
	}

	ret = simple_read_from_buffer(user_buf, count, ppos, buf, pos);
	kfree(buf);
	return ret;
}

static ssize_t iwl_dbgfs_rx_handlers_write(struct file *file,
					 const char __user *user_buf,
					 size_t count, loff_t *ppos)
{
	struct iwl_priv *priv = file->private_data;

	char buf[8];
	int buf_size;
	u32 reset_flag;

	memset(buf, 0, sizeof(buf));
	buf_size = min(count, sizeof(buf) -  1);
	if (copy_from_user(buf, user_buf, buf_size))
		return -EFAULT;
	if (sscanf(buf, "%x", &reset_flag) != 1)
		return -EFAULT;
	if (reset_flag == 0)
		memset(&priv->rx_handlers_stats[0], 0,
			sizeof(priv->rx_handlers_stats));

	return count;
}

static ssize_t iwl_dbgfs_qos_read(struct file *file, char __user *user_buf,
				       size_t count, loff_t *ppos)
{
	struct iwl_priv *priv = file->private_data;
	struct iwl_rxon_context *ctx;
	int pos = 0, i;
	char buf[256 * NUM_IWL_RXON_CTX];
	const size_t bufsz = sizeof(buf);

	for_each_context(priv, ctx) {
		pos += scnprintf(buf + pos, bufsz - pos, "context %d:\n",
				 ctx->ctxid);
		for (i = 0; i < AC_NUM; i++) {
			pos += scnprintf(buf + pos, bufsz - pos,
				"\tcw_min\tcw_max\taifsn\ttxop\n");
			pos += scnprintf(buf + pos, bufsz - pos,
				"AC[%d]\t%u\t%u\t%u\t%u\n", i,
				ctx->qos_data.def_qos_parm.ac[i].cw_min,
				ctx->qos_data.def_qos_parm.ac[i].cw_max,
				ctx->qos_data.def_qos_parm.ac[i].aifsn,
				ctx->qos_data.def_qos_parm.ac[i].edca_txop);
		}
		pos += scnprintf(buf + pos, bufsz - pos, "\n");
	}
	return simple_read_from_buffer(user_buf, count, ppos, buf, pos);
}

static ssize_t iwl_dbgfs_thermal_throttling_read(struct file *file,
				char __user *user_buf,
				size_t count, loff_t *ppos)
{
	struct iwl_priv *priv = file->private_data;
	struct iwl_tt_mgmt *tt = &priv->thermal_throttle;
	struct iwl_tt_restriction *restriction;
	char buf[100];
	int pos = 0;
	const size_t bufsz = sizeof(buf);

	pos += scnprintf(buf + pos, bufsz - pos,
			"Thermal Throttling Mode: %s\n",
			tt->advanced_tt ? "Advance" : "Legacy");
	pos += scnprintf(buf + pos, bufsz - pos,
			"Thermal Throttling State: %d\n",
			tt->state);
	if (tt->advanced_tt) {
		restriction = tt->restriction + tt->state;
		pos += scnprintf(buf + pos, bufsz - pos,
				"Tx mode: %d\n",
				restriction->tx_stream);
		pos += scnprintf(buf + pos, bufsz - pos,
				"Rx mode: %d\n",
				restriction->rx_stream);
		pos += scnprintf(buf + pos, bufsz - pos,
				"HT mode: %d\n",
				restriction->is_ht);
	}
	return simple_read_from_buffer(user_buf, count, ppos, buf, pos);
}

static ssize_t iwl_dbgfs_disable_ht40_write(struct file *file,
					 const char __user *user_buf,
					 size_t count, loff_t *ppos)
{
	struct iwl_priv *priv = file->private_data;
	char buf[8];
	int buf_size;
	int ht40;

	memset(buf, 0, sizeof(buf));
	buf_size = min(count, sizeof(buf) -  1);
	if (copy_from_user(buf, user_buf, buf_size))
		return -EFAULT;
	if (sscanf(buf, "%d", &ht40) != 1)
		return -EFAULT;
	if (!iwl_is_any_associated(priv))
		priv->disable_ht40 = ht40 ? true : false;
	else
		return -EINVAL;

	return count;
}

static ssize_t iwl_dbgfs_disable_ht40_read(struct file *file,
					 char __user *user_buf,
					 size_t count, loff_t *ppos)
{
	struct iwl_priv *priv = file->private_data;
	char buf[100];
	int pos = 0;
	const size_t bufsz = sizeof(buf);

	pos += scnprintf(buf + pos, bufsz - pos,
			"11n 40MHz Mode: %s\n",
			priv->disable_ht40 ? "Disabled" : "Enabled");
	return simple_read_from_buffer(user_buf, count, ppos, buf, pos);
}

static ssize_t iwl_dbgfs_temperature_read(struct file *file,
					 char __user *user_buf,
					 size_t count, loff_t *ppos)
{
	struct iwl_priv *priv = file->private_data;
	char buf[8];
	int pos = 0;
	const size_t bufsz = sizeof(buf);

	pos += scnprintf(buf + pos, bufsz - pos, "%d\n", priv->temperature);
	return simple_read_from_buffer(user_buf, count, ppos, buf, pos);
}


static ssize_t iwl_dbgfs_sleep_level_override_write(struct file *file,
						    const char __user *user_buf,
						    size_t count, loff_t *ppos)
{
	struct iwl_priv *priv = file->private_data;
	char buf[8];
	int buf_size;
	int value;

	memset(buf, 0, sizeof(buf));
	buf_size = min(count, sizeof(buf) -  1);
	if (copy_from_user(buf, user_buf, buf_size))
		return -EFAULT;

	if (sscanf(buf, "%d", &value) != 1)
		return -EINVAL;

	/*
	 * Our users expect 0 to be "CAM", but 0 isn't actually
	 * valid here. However, let's not confuse them and present
	 * IWL_POWER_INDEX_1 as "1", not "0".
	 */
	if (value == 0)
		return -EINVAL;
	else if (value > 0)
		value -= 1;

	if (value != -1 && (value < 0 || value >= IWL_POWER_NUM))
		return -EINVAL;

	if (!iwl_is_ready_rf(priv))
		return -EAGAIN;

	priv->power_data.debug_sleep_level_override = value;

	mutex_lock(&priv->mutex);
	iwl_power_update_mode(priv, true);
	mutex_unlock(&priv->mutex);

	return count;
}

static ssize_t iwl_dbgfs_sleep_level_override_read(struct file *file,
						   char __user *user_buf,
						   size_t count, loff_t *ppos)
{
	struct iwl_priv *priv = file->private_data;
	char buf[10];
	int pos, value;
	const size_t bufsz = sizeof(buf);

	/* see the write function */
	value = priv->power_data.debug_sleep_level_override;
	if (value >= 0)
		value += 1;

	pos = scnprintf(buf, bufsz, "%d\n", value);
	return simple_read_from_buffer(user_buf, count, ppos, buf, pos);
}

static ssize_t iwl_dbgfs_current_sleep_command_read(struct file *file,
						    char __user *user_buf,
						    size_t count, loff_t *ppos)
{
	struct iwl_priv *priv = file->private_data;
	char buf[200];
	int pos = 0, i;
	const size_t bufsz = sizeof(buf);
	struct iwl_powertable_cmd *cmd = &priv->power_data.sleep_cmd;

	pos += scnprintf(buf + pos, bufsz - pos,
			 "flags: %#.2x\n", le16_to_cpu(cmd->flags));
	pos += scnprintf(buf + pos, bufsz - pos,
			 "RX/TX timeout: %d/%d usec\n",
			 le32_to_cpu(cmd->rx_data_timeout),
			 le32_to_cpu(cmd->tx_data_timeout));
	for (i = 0; i < IWL_POWER_VEC_SIZE; i++)
		pos += scnprintf(buf + pos, bufsz - pos,
				 "sleep_interval[%d]: %d\n", i,
				 le32_to_cpu(cmd->sleep_interval[i]));

	return simple_read_from_buffer(user_buf, count, ppos, buf, pos);
}

DEBUGFS_READ_WRITE_FILE_OPS(sram);
DEBUGFS_READ_FILE_OPS(wowlan_sram);
DEBUGFS_READ_FILE_OPS(nvm);
DEBUGFS_READ_FILE_OPS(stations);
DEBUGFS_READ_FILE_OPS(channels);
DEBUGFS_READ_FILE_OPS(status);
DEBUGFS_READ_WRITE_FILE_OPS(rx_handlers);
DEBUGFS_READ_FILE_OPS(qos);
DEBUGFS_READ_FILE_OPS(thermal_throttling);
DEBUGFS_READ_WRITE_FILE_OPS(disable_ht40);
DEBUGFS_READ_FILE_OPS(temperature);
DEBUGFS_READ_WRITE_FILE_OPS(sleep_level_override);
DEBUGFS_READ_FILE_OPS(current_sleep_command);

static const char *fmt_value = "  %-30s %10u\n";
static const char *fmt_hex   = "  %-30s       0x%02X\n";
static const char *fmt_table = "  %-30s %10u  %10u  %10u  %10u\n";
static const char *fmt_header =
	"%-32s    current  cumulative       delta         max\n";

static int iwl_statistics_flag(struct iwl_priv *priv, char *buf, int bufsz)
{
	int p = 0;
	u32 flag;

	lockdep_assert_held(&priv->statistics.lock);

	flag = le32_to_cpu(priv->statistics.flag);

	p += scnprintf(buf + p, bufsz - p, "Statistics Flag(0x%X):\n", flag);
	if (flag & UCODE_STATISTICS_CLEAR_MSK)
		p += scnprintf(buf + p, bufsz - p,
		"\tStatistics have been cleared\n");
	p += scnprintf(buf + p, bufsz - p, "\tOperational Frequency: %s\n",
		(flag & UCODE_STATISTICS_FREQUENCY_MSK)
		? "2.4 GHz" : "5.2 GHz");
	p += scnprintf(buf + p, bufsz - p, "\tTGj Narrow Band: %s\n",
		(flag & UCODE_STATISTICS_NARROW_BAND_MSK)
		 ? "enabled" : "disabled");

	return p;
}

static ssize_t iwl_dbgfs_ucode_rx_stats_read(struct file *file,
					char __user *user_buf,
					size_t count, loff_t *ppos)
{
	struct iwl_priv *priv = file->private_data;
	int pos = 0;
	char *buf;
	int bufsz = sizeof(struct statistics_rx_phy) * 40 +
		    sizeof(struct statistics_rx_non_phy) * 40 +
		    sizeof(struct statistics_rx_ht_phy) * 40 + 400;
	ssize_t ret;
	struct statistics_rx_phy *ofdm, *accum_ofdm, *delta_ofdm, *max_ofdm;
	struct statistics_rx_phy *cck, *accum_cck, *delta_cck, *max_cck;
	struct statistics_rx_non_phy *general, *accum_general;
	struct statistics_rx_non_phy *delta_general, *max_general;
	struct statistics_rx_ht_phy *ht, *accum_ht, *delta_ht, *max_ht;

	if (!iwl_is_alive(priv))
		return -EAGAIN;

	buf = kzalloc(bufsz, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	/*
	 * the statistic information display here is based on
	 * the last statistics notification from uCode
	 * might not reflect the current uCode activity
	 */
	spin_lock_bh(&priv->statistics.lock);
	ofdm = &priv->statistics.rx_ofdm;
	cck = &priv->statistics.rx_cck;
	general = &priv->statistics.rx_non_phy;
	ht = &priv->statistics.rx_ofdm_ht;
	accum_ofdm = &priv->accum_stats.rx_ofdm;
	accum_cck = &priv->accum_stats.rx_cck;
	accum_general = &priv->accum_stats.rx_non_phy;
	accum_ht = &priv->accum_stats.rx_ofdm_ht;
	delta_ofdm = &priv->delta_stats.rx_ofdm;
	delta_cck = &priv->delta_stats.rx_cck;
	delta_general = &priv->delta_stats.rx_non_phy;
	delta_ht = &priv->delta_stats.rx_ofdm_ht;
	max_ofdm = &priv->max_delta_stats.rx_ofdm;
	max_cck = &priv->max_delta_stats.rx_cck;
	max_general = &priv->max_delta_stats.rx_non_phy;
	max_ht = &priv->max_delta_stats.rx_ofdm_ht;

	pos += iwl_statistics_flag(priv, buf, bufsz);
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_header, "Statistics_Rx - OFDM:");
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "ina_cnt:",
			 le32_to_cpu(ofdm->ina_cnt),
			 accum_ofdm->ina_cnt,
			 delta_ofdm->ina_cnt, max_ofdm->ina_cnt);
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "fina_cnt:",
			 le32_to_cpu(ofdm->fina_cnt), accum_ofdm->fina_cnt,
			 delta_ofdm->fina_cnt, max_ofdm->fina_cnt);
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "plcp_err:",
			 le32_to_cpu(ofdm->plcp_err), accum_ofdm->plcp_err,
			 delta_ofdm->plcp_err, max_ofdm->plcp_err);
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "crc32_err:",
			 le32_to_cpu(ofdm->crc32_err), accum_ofdm->crc32_err,
			 delta_ofdm->crc32_err, max_ofdm->crc32_err);
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "overrun_err:",
			 le32_to_cpu(ofdm->overrun_err),
			 accum_ofdm->overrun_err, delta_ofdm->overrun_err,
			 max_ofdm->overrun_err);
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "early_overrun_err:",
			 le32_to_cpu(ofdm->early_overrun_err),
			 accum_ofdm->early_overrun_err,
			 delta_ofdm->early_overrun_err,
			 max_ofdm->early_overrun_err);
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "crc32_good:",
			 le32_to_cpu(ofdm->crc32_good),
			 accum_ofdm->crc32_good, delta_ofdm->crc32_good,
			 max_ofdm->crc32_good);
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "false_alarm_cnt:",
			 le32_to_cpu(ofdm->false_alarm_cnt),
			 accum_ofdm->false_alarm_cnt,
			 delta_ofdm->false_alarm_cnt,
			 max_ofdm->false_alarm_cnt);
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "fina_sync_err_cnt:",
			 le32_to_cpu(ofdm->fina_sync_err_cnt),
			 accum_ofdm->fina_sync_err_cnt,
			 delta_ofdm->fina_sync_err_cnt,
			 max_ofdm->fina_sync_err_cnt);
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "sfd_timeout:",
			 le32_to_cpu(ofdm->sfd_timeout),
			 accum_ofdm->sfd_timeout, delta_ofdm->sfd_timeout,
			 max_ofdm->sfd_timeout);
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "fina_timeout:",
			 le32_to_cpu(ofdm->fina_timeout),
			 accum_ofdm->fina_timeout, delta_ofdm->fina_timeout,
			 max_ofdm->fina_timeout);
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "unresponded_rts:",
			 le32_to_cpu(ofdm->unresponded_rts),
			 accum_ofdm->unresponded_rts,
			 delta_ofdm->unresponded_rts,
			 max_ofdm->unresponded_rts);
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "rxe_frame_lmt_ovrun:",
			 le32_to_cpu(ofdm->rxe_frame_limit_overrun),
			 accum_ofdm->rxe_frame_limit_overrun,
			 delta_ofdm->rxe_frame_limit_overrun,
			 max_ofdm->rxe_frame_limit_overrun);
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "sent_ack_cnt:",
			 le32_to_cpu(ofdm->sent_ack_cnt),
			 accum_ofdm->sent_ack_cnt, delta_ofdm->sent_ack_cnt,
			 max_ofdm->sent_ack_cnt);
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "sent_cts_cnt:",
			 le32_to_cpu(ofdm->sent_cts_cnt),
			 accum_ofdm->sent_cts_cnt, delta_ofdm->sent_cts_cnt,
			 max_ofdm->sent_cts_cnt);
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "sent_ba_rsp_cnt:",
			 le32_to_cpu(ofdm->sent_ba_rsp_cnt),
			 accum_ofdm->sent_ba_rsp_cnt,
			 delta_ofdm->sent_ba_rsp_cnt,
			 max_ofdm->sent_ba_rsp_cnt);
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "dsp_self_kill:",
			 le32_to_cpu(ofdm->dsp_self_kill),
			 accum_ofdm->dsp_self_kill,
			 delta_ofdm->dsp_self_kill,
			 max_ofdm->dsp_self_kill);
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "mh_format_err:",
			 le32_to_cpu(ofdm->mh_format_err),
			 accum_ofdm->mh_format_err,
			 delta_ofdm->mh_format_err,
			 max_ofdm->mh_format_err);
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "re_acq_main_rssi_sum:",
			 le32_to_cpu(ofdm->re_acq_main_rssi_sum),
			 accum_ofdm->re_acq_main_rssi_sum,
			 delta_ofdm->re_acq_main_rssi_sum,
			 max_ofdm->re_acq_main_rssi_sum);

	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_header, "Statistics_Rx - CCK:");
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "ina_cnt:",
			 le32_to_cpu(cck->ina_cnt), accum_cck->ina_cnt,
			 delta_cck->ina_cnt, max_cck->ina_cnt);
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "fina_cnt:",
			 le32_to_cpu(cck->fina_cnt), accum_cck->fina_cnt,
			 delta_cck->fina_cnt, max_cck->fina_cnt);
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "plcp_err:",
			 le32_to_cpu(cck->plcp_err), accum_cck->plcp_err,
			 delta_cck->plcp_err, max_cck->plcp_err);
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "crc32_err:",
			 le32_to_cpu(cck->crc32_err), accum_cck->crc32_err,
			 delta_cck->crc32_err, max_cck->crc32_err);
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "overrun_err:",
			 le32_to_cpu(cck->overrun_err),
			 accum_cck->overrun_err, delta_cck->overrun_err,
			 max_cck->overrun_err);
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "early_overrun_err:",
			 le32_to_cpu(cck->early_overrun_err),
			 accum_cck->early_overrun_err,
			 delta_cck->early_overrun_err,
			 max_cck->early_overrun_err);
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "crc32_good:",
			 le32_to_cpu(cck->crc32_good), accum_cck->crc32_good,
			 delta_cck->crc32_good, max_cck->crc32_good);
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "false_alarm_cnt:",
			 le32_to_cpu(cck->false_alarm_cnt),
			 accum_cck->false_alarm_cnt,
			 delta_cck->false_alarm_cnt, max_cck->false_alarm_cnt);
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "fina_sync_err_cnt:",
			 le32_to_cpu(cck->fina_sync_err_cnt),
			 accum_cck->fina_sync_err_cnt,
			 delta_cck->fina_sync_err_cnt,
			 max_cck->fina_sync_err_cnt);
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "sfd_timeout:",
			 le32_to_cpu(cck->sfd_timeout),
			 accum_cck->sfd_timeout, delta_cck->sfd_timeout,
			 max_cck->sfd_timeout);
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "fina_timeout:",
			 le32_to_cpu(cck->fina_timeout),
			 accum_cck->fina_timeout, delta_cck->fina_timeout,
			 max_cck->fina_timeout);
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "unresponded_rts:",
			 le32_to_cpu(cck->unresponded_rts),
			 accum_cck->unresponded_rts, delta_cck->unresponded_rts,
			 max_cck->unresponded_rts);
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "rxe_frame_lmt_ovrun:",
			 le32_to_cpu(cck->rxe_frame_limit_overrun),
			 accum_cck->rxe_frame_limit_overrun,
			 delta_cck->rxe_frame_limit_overrun,
			 max_cck->rxe_frame_limit_overrun);
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "sent_ack_cnt:",
			 le32_to_cpu(cck->sent_ack_cnt),
			 accum_cck->sent_ack_cnt, delta_cck->sent_ack_cnt,
			 max_cck->sent_ack_cnt);
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "sent_cts_cnt:",
			 le32_to_cpu(cck->sent_cts_cnt),
			 accum_cck->sent_cts_cnt, delta_cck->sent_cts_cnt,
			 max_cck->sent_cts_cnt);
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "sent_ba_rsp_cnt:",
			 le32_to_cpu(cck->sent_ba_rsp_cnt),
			 accum_cck->sent_ba_rsp_cnt,
			 delta_cck->sent_ba_rsp_cnt,
			 max_cck->sent_ba_rsp_cnt);
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "dsp_self_kill:",
			 le32_to_cpu(cck->dsp_self_kill),
			 accum_cck->dsp_self_kill, delta_cck->dsp_self_kill,
			 max_cck->dsp_self_kill);
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "mh_format_err:",
			 le32_to_cpu(cck->mh_format_err),
			 accum_cck->mh_format_err, delta_cck->mh_format_err,
			 max_cck->mh_format_err);
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "re_acq_main_rssi_sum:",
			 le32_to_cpu(cck->re_acq_main_rssi_sum),
			 accum_cck->re_acq_main_rssi_sum,
			 delta_cck->re_acq_main_rssi_sum,
			 max_cck->re_acq_main_rssi_sum);

	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_header, "Statistics_Rx - GENERAL:");
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "bogus_cts:",
			 le32_to_cpu(general->bogus_cts),
			 accum_general->bogus_cts, delta_general->bogus_cts,
			 max_general->bogus_cts);
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "bogus_ack:",
			 le32_to_cpu(general->bogus_ack),
			 accum_general->bogus_ack, delta_general->bogus_ack,
			 max_general->bogus_ack);
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "non_bssid_frames:",
			 le32_to_cpu(general->non_bssid_frames),
			 accum_general->non_bssid_frames,
			 delta_general->non_bssid_frames,
			 max_general->non_bssid_frames);
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "filtered_frames:",
			 le32_to_cpu(general->filtered_frames),
			 accum_general->filtered_frames,
			 delta_general->filtered_frames,
			 max_general->filtered_frames);
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "non_channel_beacons:",
			 le32_to_cpu(general->non_channel_beacons),
			 accum_general->non_channel_beacons,
			 delta_general->non_channel_beacons,
			 max_general->non_channel_beacons);
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "channel_beacons:",
			 le32_to_cpu(general->channel_beacons),
			 accum_general->channel_beacons,
			 delta_general->channel_beacons,
			 max_general->channel_beacons);
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "num_missed_bcon:",
			 le32_to_cpu(general->num_missed_bcon),
			 accum_general->num_missed_bcon,
			 delta_general->num_missed_bcon,
			 max_general->num_missed_bcon);
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "adc_rx_saturation_time:",
			 le32_to_cpu(general->adc_rx_saturation_time),
			 accum_general->adc_rx_saturation_time,
			 delta_general->adc_rx_saturation_time,
			 max_general->adc_rx_saturation_time);
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "ina_detect_search_tm:",
			 le32_to_cpu(general->ina_detection_search_time),
			 accum_general->ina_detection_search_time,
			 delta_general->ina_detection_search_time,
			 max_general->ina_detection_search_time);
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "beacon_silence_rssi_a:",
			 le32_to_cpu(general->beacon_silence_rssi_a),
			 accum_general->beacon_silence_rssi_a,
			 delta_general->beacon_silence_rssi_a,
			 max_general->beacon_silence_rssi_a);
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "beacon_silence_rssi_b:",
			 le32_to_cpu(general->beacon_silence_rssi_b),
			 accum_general->beacon_silence_rssi_b,
			 delta_general->beacon_silence_rssi_b,
			 max_general->beacon_silence_rssi_b);
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "beacon_silence_rssi_c:",
			 le32_to_cpu(general->beacon_silence_rssi_c),
			 accum_general->beacon_silence_rssi_c,
			 delta_general->beacon_silence_rssi_c,
			 max_general->beacon_silence_rssi_c);
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "interference_data_flag:",
			 le32_to_cpu(general->interference_data_flag),
			 accum_general->interference_data_flag,
			 delta_general->interference_data_flag,
			 max_general->interference_data_flag);
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "channel_load:",
			 le32_to_cpu(general->channel_load),
			 accum_general->channel_load,
			 delta_general->channel_load,
			 max_general->channel_load);
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "dsp_false_alarms:",
			 le32_to_cpu(general->dsp_false_alarms),
			 accum_general->dsp_false_alarms,
			 delta_general->dsp_false_alarms,
			 max_general->dsp_false_alarms);
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "beacon_rssi_a:",
			 le32_to_cpu(general->beacon_rssi_a),
			 accum_general->beacon_rssi_a,
			 delta_general->beacon_rssi_a,
			 max_general->beacon_rssi_a);
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "beacon_rssi_b:",
			 le32_to_cpu(general->beacon_rssi_b),
			 accum_general->beacon_rssi_b,
			 delta_general->beacon_rssi_b,
			 max_general->beacon_rssi_b);
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "beacon_rssi_c:",
			 le32_to_cpu(general->beacon_rssi_c),
			 accum_general->beacon_rssi_c,
			 delta_general->beacon_rssi_c,
			 max_general->beacon_rssi_c);
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "beacon_energy_a:",
			 le32_to_cpu(general->beacon_energy_a),
			 accum_general->beacon_energy_a,
			 delta_general->beacon_energy_a,
			 max_general->beacon_energy_a);
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "beacon_energy_b:",
			 le32_to_cpu(general->beacon_energy_b),
			 accum_general->beacon_energy_b,
			 delta_general->beacon_energy_b,
			 max_general->beacon_energy_b);
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "beacon_energy_c:",
			 le32_to_cpu(general->beacon_energy_c),
			 accum_general->beacon_energy_c,
			 delta_general->beacon_energy_c,
			 max_general->beacon_energy_c);

	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_header, "Statistics_Rx - OFDM_HT:");
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "plcp_err:",
			 le32_to_cpu(ht->plcp_err), accum_ht->plcp_err,
			 delta_ht->plcp_err, max_ht->plcp_err);
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "overrun_err:",
			 le32_to_cpu(ht->overrun_err), accum_ht->overrun_err,
			 delta_ht->overrun_err, max_ht->overrun_err);
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "early_overrun_err:",
			 le32_to_cpu(ht->early_overrun_err),
			 accum_ht->early_overrun_err,
			 delta_ht->early_overrun_err,
			 max_ht->early_overrun_err);
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "crc32_good:",
			 le32_to_cpu(ht->crc32_good), accum_ht->crc32_good,
			 delta_ht->crc32_good, max_ht->crc32_good);
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "crc32_err:",
			 le32_to_cpu(ht->crc32_err), accum_ht->crc32_err,
			 delta_ht->crc32_err, max_ht->crc32_err);
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "mh_format_err:",
			 le32_to_cpu(ht->mh_format_err),
			 accum_ht->mh_format_err,
			 delta_ht->mh_format_err, max_ht->mh_format_err);
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "agg_crc32_good:",
			 le32_to_cpu(ht->agg_crc32_good),
			 accum_ht->agg_crc32_good,
			 delta_ht->agg_crc32_good, max_ht->agg_crc32_good);
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "agg_mpdu_cnt:",
			 le32_to_cpu(ht->agg_mpdu_cnt),
			 accum_ht->agg_mpdu_cnt,
			 delta_ht->agg_mpdu_cnt, max_ht->agg_mpdu_cnt);
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "agg_cnt:",
			 le32_to_cpu(ht->agg_cnt), accum_ht->agg_cnt,
			 delta_ht->agg_cnt, max_ht->agg_cnt);
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "unsupport_mcs:",
			 le32_to_cpu(ht->unsupport_mcs),
			 accum_ht->unsupport_mcs,
			 delta_ht->unsupport_mcs, max_ht->unsupport_mcs);

	spin_unlock_bh(&priv->statistics.lock);

	ret = simple_read_from_buffer(user_buf, count, ppos, buf, pos);
	kfree(buf);
	return ret;
}

static ssize_t iwl_dbgfs_ucode_tx_stats_read(struct file *file,
					char __user *user_buf,
					size_t count, loff_t *ppos)
{
	struct iwl_priv *priv = file->private_data;
	int pos = 0;
	char *buf;
	int bufsz = (sizeof(struct statistics_tx) * 48) + 250;
	ssize_t ret;
	struct statistics_tx *tx, *accum_tx, *delta_tx, *max_tx;

	if (!iwl_is_alive(priv))
		return -EAGAIN;

	buf = kzalloc(bufsz, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	/* the statistic information display here is based on
	 * the last statistics notification from uCode
	 * might not reflect the current uCode activity
	 */
	spin_lock_bh(&priv->statistics.lock);

	tx = &priv->statistics.tx;
	accum_tx = &priv->accum_stats.tx;
	delta_tx = &priv->delta_stats.tx;
	max_tx = &priv->max_delta_stats.tx;

	pos += iwl_statistics_flag(priv, buf, bufsz);
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_header, "Statistics_Tx:");
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "preamble:",
			 le32_to_cpu(tx->preamble_cnt),
			 accum_tx->preamble_cnt,
			 delta_tx->preamble_cnt, max_tx->preamble_cnt);
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "rx_detected_cnt:",
			 le32_to_cpu(tx->rx_detected_cnt),
			 accum_tx->rx_detected_cnt,
			 delta_tx->rx_detected_cnt, max_tx->rx_detected_cnt);
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "bt_prio_defer_cnt:",
			 le32_to_cpu(tx->bt_prio_defer_cnt),
			 accum_tx->bt_prio_defer_cnt,
			 delta_tx->bt_prio_defer_cnt,
			 max_tx->bt_prio_defer_cnt);
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "bt_prio_kill_cnt:",
			 le32_to_cpu(tx->bt_prio_kill_cnt),
			 accum_tx->bt_prio_kill_cnt,
			 delta_tx->bt_prio_kill_cnt,
			 max_tx->bt_prio_kill_cnt);
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "few_bytes_cnt:",
			 le32_to_cpu(tx->few_bytes_cnt),
			 accum_tx->few_bytes_cnt,
			 delta_tx->few_bytes_cnt, max_tx->few_bytes_cnt);
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "cts_timeout:",
			 le32_to_cpu(tx->cts_timeout), accum_tx->cts_timeout,
			 delta_tx->cts_timeout, max_tx->cts_timeout);
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "ack_timeout:",
			 le32_to_cpu(tx->ack_timeout),
			 accum_tx->ack_timeout,
			 delta_tx->ack_timeout, max_tx->ack_timeout);
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "expected_ack_cnt:",
			 le32_to_cpu(tx->expected_ack_cnt),
			 accum_tx->expected_ack_cnt,
			 delta_tx->expected_ack_cnt,
			 max_tx->expected_ack_cnt);
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "actual_ack_cnt:",
			 le32_to_cpu(tx->actual_ack_cnt),
			 accum_tx->actual_ack_cnt,
			 delta_tx->actual_ack_cnt,
			 max_tx->actual_ack_cnt);
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "dump_msdu_cnt:",
			 le32_to_cpu(tx->dump_msdu_cnt),
			 accum_tx->dump_msdu_cnt,
			 delta_tx->dump_msdu_cnt,
			 max_tx->dump_msdu_cnt);
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "abort_nxt_frame_mismatch:",
			 le32_to_cpu(tx->burst_abort_next_frame_mismatch_cnt),
			 accum_tx->burst_abort_next_frame_mismatch_cnt,
			 delta_tx->burst_abort_next_frame_mismatch_cnt,
			 max_tx->burst_abort_next_frame_mismatch_cnt);
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "abort_missing_nxt_frame:",
			 le32_to_cpu(tx->burst_abort_missing_next_frame_cnt),
			 accum_tx->burst_abort_missing_next_frame_cnt,
			 delta_tx->burst_abort_missing_next_frame_cnt,
			 max_tx->burst_abort_missing_next_frame_cnt);
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "cts_timeout_collision:",
			 le32_to_cpu(tx->cts_timeout_collision),
			 accum_tx->cts_timeout_collision,
			 delta_tx->cts_timeout_collision,
			 max_tx->cts_timeout_collision);
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "ack_ba_timeout_collision:",
			 le32_to_cpu(tx->ack_or_ba_timeout_collision),
			 accum_tx->ack_or_ba_timeout_collision,
			 delta_tx->ack_or_ba_timeout_collision,
			 max_tx->ack_or_ba_timeout_collision);
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "agg ba_timeout:",
			 le32_to_cpu(tx->agg.ba_timeout),
			 accum_tx->agg.ba_timeout,
			 delta_tx->agg.ba_timeout,
			 max_tx->agg.ba_timeout);
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "agg ba_resched_frames:",
			 le32_to_cpu(tx->agg.ba_reschedule_frames),
			 accum_tx->agg.ba_reschedule_frames,
			 delta_tx->agg.ba_reschedule_frames,
			 max_tx->agg.ba_reschedule_frames);
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "agg scd_query_agg_frame:",
			 le32_to_cpu(tx->agg.scd_query_agg_frame_cnt),
			 accum_tx->agg.scd_query_agg_frame_cnt,
			 delta_tx->agg.scd_query_agg_frame_cnt,
			 max_tx->agg.scd_query_agg_frame_cnt);
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "agg scd_query_no_agg:",
			 le32_to_cpu(tx->agg.scd_query_no_agg),
			 accum_tx->agg.scd_query_no_agg,
			 delta_tx->agg.scd_query_no_agg,
			 max_tx->agg.scd_query_no_agg);
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "agg scd_query_agg:",
			 le32_to_cpu(tx->agg.scd_query_agg),
			 accum_tx->agg.scd_query_agg,
			 delta_tx->agg.scd_query_agg,
			 max_tx->agg.scd_query_agg);
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "agg scd_query_mismatch:",
			 le32_to_cpu(tx->agg.scd_query_mismatch),
			 accum_tx->agg.scd_query_mismatch,
			 delta_tx->agg.scd_query_mismatch,
			 max_tx->agg.scd_query_mismatch);
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "agg frame_not_ready:",
			 le32_to_cpu(tx->agg.frame_not_ready),
			 accum_tx->agg.frame_not_ready,
			 delta_tx->agg.frame_not_ready,
			 max_tx->agg.frame_not_ready);
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "agg underrun:",
			 le32_to_cpu(tx->agg.underrun),
			 accum_tx->agg.underrun,
			 delta_tx->agg.underrun, max_tx->agg.underrun);
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "agg bt_prio_kill:",
			 le32_to_cpu(tx->agg.bt_prio_kill),
			 accum_tx->agg.bt_prio_kill,
			 delta_tx->agg.bt_prio_kill,
			 max_tx->agg.bt_prio_kill);
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "agg rx_ba_rsp_cnt:",
			 le32_to_cpu(tx->agg.rx_ba_rsp_cnt),
			 accum_tx->agg.rx_ba_rsp_cnt,
			 delta_tx->agg.rx_ba_rsp_cnt,
			 max_tx->agg.rx_ba_rsp_cnt);

	if (tx->tx_power.ant_a || tx->tx_power.ant_b || tx->tx_power.ant_c) {
		pos += scnprintf(buf + pos, bufsz - pos,
			"tx power: (1/2 dB step)\n");
		if ((priv->nvm_data->valid_tx_ant & ANT_A) &&
		    tx->tx_power.ant_a)
			pos += scnprintf(buf + pos, bufsz - pos,
					fmt_hex, "antenna A:",
					tx->tx_power.ant_a);
		if ((priv->nvm_data->valid_tx_ant & ANT_B) &&
		    tx->tx_power.ant_b)
			pos += scnprintf(buf + pos, bufsz - pos,
					fmt_hex, "antenna B:",
					tx->tx_power.ant_b);
		if ((priv->nvm_data->valid_tx_ant & ANT_C) &&
		    tx->tx_power.ant_c)
			pos += scnprintf(buf + pos, bufsz - pos,
					fmt_hex, "antenna C:",
					tx->tx_power.ant_c);
	}

	spin_unlock_bh(&priv->statistics.lock);

	ret = simple_read_from_buffer(user_buf, count, ppos, buf, pos);
	kfree(buf);
	return ret;
}

static ssize_t iwl_dbgfs_ucode_general_stats_read(struct file *file,
					char __user *user_buf,
					size_t count, loff_t *ppos)
{
	struct iwl_priv *priv = file->private_data;
	int pos = 0;
	char *buf;
	int bufsz = sizeof(struct statistics_general) * 10 + 300;
	ssize_t ret;
	struct statistics_general_common *general, *accum_general;
	struct statistics_general_common *delta_general, *max_general;
	struct statistics_dbg *dbg, *accum_dbg, *delta_dbg, *max_dbg;
	struct statistics_div *div, *accum_div, *delta_div, *max_div;

	if (!iwl_is_alive(priv))
		return -EAGAIN;

	buf = kzalloc(bufsz, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	/* the statistic information display here is based on
	 * the last statistics notification from uCode
	 * might not reflect the current uCode activity
	 */

	spin_lock_bh(&priv->statistics.lock);

	general = &priv->statistics.common;
	dbg = &priv->statistics.common.dbg;
	div = &priv->statistics.common.div;
	accum_general = &priv->accum_stats.common;
	accum_dbg = &priv->accum_stats.common.dbg;
	accum_div = &priv->accum_stats.common.div;
	delta_general = &priv->delta_stats.common;
	max_general = &priv->max_delta_stats.common;
	delta_dbg = &priv->delta_stats.common.dbg;
	max_dbg = &priv->max_delta_stats.common.dbg;
	delta_div = &priv->delta_stats.common.div;
	max_div = &priv->max_delta_stats.common.div;

	pos += iwl_statistics_flag(priv, buf, bufsz);
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_header, "Statistics_General:");
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_value, "temperature:",
			 le32_to_cpu(general->temperature));
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_value, "temperature_m:",
			 le32_to_cpu(general->temperature_m));
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_value, "ttl_timestamp:",
			 le32_to_cpu(general->ttl_timestamp));
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "burst_check:",
			 le32_to_cpu(dbg->burst_check),
			 accum_dbg->burst_check,
			 delta_dbg->burst_check, max_dbg->burst_check);
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "burst_count:",
			 le32_to_cpu(dbg->burst_count),
			 accum_dbg->burst_count,
			 delta_dbg->burst_count, max_dbg->burst_count);
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "wait_for_silence_timeout_count:",
			 le32_to_cpu(dbg->wait_for_silence_timeout_cnt),
			 accum_dbg->wait_for_silence_timeout_cnt,
			 delta_dbg->wait_for_silence_timeout_cnt,
			 max_dbg->wait_for_silence_timeout_cnt);
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "sleep_time:",
			 le32_to_cpu(general->sleep_time),
			 accum_general->sleep_time,
			 delta_general->sleep_time, max_general->sleep_time);
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "slots_out:",
			 le32_to_cpu(general->slots_out),
			 accum_general->slots_out,
			 delta_general->slots_out, max_general->slots_out);
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "slots_idle:",
			 le32_to_cpu(general->slots_idle),
			 accum_general->slots_idle,
			 delta_general->slots_idle, max_general->slots_idle);
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "tx_on_a:",
			 le32_to_cpu(div->tx_on_a), accum_div->tx_on_a,
			 delta_div->tx_on_a, max_div->tx_on_a);
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "tx_on_b:",
			 le32_to_cpu(div->tx_on_b), accum_div->tx_on_b,
			 delta_div->tx_on_b, max_div->tx_on_b);
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "exec_time:",
			 le32_to_cpu(div->exec_time), accum_div->exec_time,
			 delta_div->exec_time, max_div->exec_time);
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "probe_time:",
			 le32_to_cpu(div->probe_time), accum_div->probe_time,
			 delta_div->probe_time, max_div->probe_time);
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "rx_enable_counter:",
			 le32_to_cpu(general->rx_enable_counter),
			 accum_general->rx_enable_counter,
			 delta_general->rx_enable_counter,
			 max_general->rx_enable_counter);
	pos += scnprintf(buf + pos, bufsz - pos,
			 fmt_table, "num_of_sos_states:",
			 le32_to_cpu(general->num_of_sos_states),
			 accum_general->num_of_sos_states,
			 delta_general->num_of_sos_states,
			 max_general->num_of_sos_states);

	spin_unlock_bh(&priv->statistics.lock);

	ret = simple_read_from_buffer(user_buf, count, ppos, buf, pos);
	kfree(buf);
	return ret;
}

static ssize_t iwl_dbgfs_ucode_bt_stats_read(struct file *file,
					char __user *user_buf,
					size_t count, loff_t *ppos)
{
	struct iwl_priv *priv = (struct iwl_priv *)file->private_data;
	int pos = 0;
	char *buf;
	int bufsz = (sizeof(struct statistics_bt_activity) * 24) + 200;
	ssize_t ret;
	struct statistics_bt_activity *bt, *accum_bt;

	if (!iwl_is_alive(priv))
		return -EAGAIN;

	if (!priv->bt_enable_flag)
		return -EINVAL;

	/* make request to uCode to retrieve statistics information */
	mutex_lock(&priv->mutex);
	ret = iwl_send_statistics_request(priv, CMD_SYNC, false);
	mutex_unlock(&priv->mutex);

	if (ret)
		return -EAGAIN;
	buf = kzalloc(bufsz, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	/*
	 * the statistic information display here is based on
	 * the last statistics notification from uCode
	 * might not reflect the current uCode activity
	 */

	spin_lock_bh(&priv->statistics.lock);

	bt = &priv->statistics.bt_activity;
	accum_bt = &priv->accum_stats.bt_activity;

	pos += iwl_statistics_flag(priv, buf, bufsz);
	pos += scnprintf(buf + pos, bufsz - pos, "Statistics_BT:\n");
	pos += scnprintf(buf + pos, bufsz - pos,
			"\t\t\tcurrent\t\t\taccumulative\n");
	pos += scnprintf(buf + pos, bufsz - pos,
			 "hi_priority_tx_req_cnt:\t\t%u\t\t\t%u\n",
			 le32_to_cpu(bt->hi_priority_tx_req_cnt),
			 accum_bt->hi_priority_tx_req_cnt);
	pos += scnprintf(buf + pos, bufsz - pos,
			 "hi_priority_tx_denied_cnt:\t%u\t\t\t%u\n",
			 le32_to_cpu(bt->hi_priority_tx_denied_cnt),
			 accum_bt->hi_priority_tx_denied_cnt);
	pos += scnprintf(buf + pos, bufsz - pos,
			 "lo_priority_tx_req_cnt:\t\t%u\t\t\t%u\n",
			 le32_to_cpu(bt->lo_priority_tx_req_cnt),
			 accum_bt->lo_priority_tx_req_cnt);
	pos += scnprintf(buf + pos, bufsz - pos,
			 "lo_priority_tx_denied_cnt:\t%u\t\t\t%u\n",
			 le32_to_cpu(bt->lo_priority_tx_denied_cnt),
			 accum_bt->lo_priority_tx_denied_cnt);
	pos += scnprintf(buf + pos, bufsz - pos,
			 "hi_priority_rx_req_cnt:\t\t%u\t\t\t%u\n",
			 le32_to_cpu(bt->hi_priority_rx_req_cnt),
			 accum_bt->hi_priority_rx_req_cnt);
	pos += scnprintf(buf + pos, bufsz - pos,
			 "hi_priority_rx_denied_cnt:\t%u\t\t\t%u\n",
			 le32_to_cpu(bt->hi_priority_rx_denied_cnt),
			 accum_bt->hi_priority_rx_denied_cnt);
	pos += scnprintf(buf + pos, bufsz - pos,
			 "lo_priority_rx_req_cnt:\t\t%u\t\t\t%u\n",
			 le32_to_cpu(bt->lo_priority_rx_req_cnt),
			 accum_bt->lo_priority_rx_req_cnt);
	pos += scnprintf(buf + pos, bufsz - pos,
			 "lo_priority_rx_denied_cnt:\t%u\t\t\t%u\n",
			 le32_to_cpu(bt->lo_priority_rx_denied_cnt),
			 accum_bt->lo_priority_rx_denied_cnt);

	pos += scnprintf(buf + pos, bufsz - pos,
			 "(rx)num_bt_kills:\t\t%u\t\t\t%u\n",
			 le32_to_cpu(priv->statistics.num_bt_kills),
			 priv->statistics.accum_num_bt_kills);

	spin_unlock_bh(&priv->statistics.lock);

	ret = simple_read_from_buffer(user_buf, count, ppos, buf, pos);
	kfree(buf);
	return ret;
}

static ssize_t iwl_dbgfs_reply_tx_error_read(struct file *file,
					char __user *user_buf,
					size_t count, loff_t *ppos)
{
	struct iwl_priv *priv = (struct iwl_priv *)file->private_data;
	int pos = 0;
	char *buf;
	int bufsz = (sizeof(struct reply_tx_error_statistics) * 24) +
		(sizeof(struct reply_agg_tx_error_statistics) * 24) + 200;
	ssize_t ret;

	if (!iwl_is_alive(priv))
		return -EAGAIN;

	buf = kzalloc(bufsz, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	pos += scnprintf(buf + pos, bufsz - pos, "Statistics_TX_Error:\n");
	pos += scnprintf(buf + pos, bufsz - pos, "%s:\t\t\t\t%u\n",
			 iwl_get_tx_fail_reason(TX_STATUS_POSTPONE_DELAY),
			 priv->reply_tx_stats.pp_delay);
	pos += scnprintf(buf + pos, bufsz - pos, "%s:\t\t\t%u\n",
			 iwl_get_tx_fail_reason(TX_STATUS_POSTPONE_FEW_BYTES),
			 priv->reply_tx_stats.pp_few_bytes);
	pos += scnprintf(buf + pos, bufsz - pos, "%s:\t\t\t%u\n",
			 iwl_get_tx_fail_reason(TX_STATUS_POSTPONE_BT_PRIO),
			 priv->reply_tx_stats.pp_bt_prio);
	pos += scnprintf(buf + pos, bufsz - pos, "%s:\t\t\t%u\n",
			 iwl_get_tx_fail_reason(TX_STATUS_POSTPONE_QUIET_PERIOD),
			 priv->reply_tx_stats.pp_quiet_period);
	pos += scnprintf(buf + pos, bufsz - pos, "%s:\t\t\t%u\n",
			 iwl_get_tx_fail_reason(TX_STATUS_POSTPONE_CALC_TTAK),
			 priv->reply_tx_stats.pp_calc_ttak);
	pos += scnprintf(buf + pos, bufsz - pos, "%s:\t\t%u\n",
			 iwl_get_tx_fail_reason(
				TX_STATUS_FAIL_INTERNAL_CROSSED_RETRY),
			 priv->reply_tx_stats.int_crossed_retry);
	pos += scnprintf(buf + pos, bufsz - pos, "%s:\t\t\t%u\n",
			 iwl_get_tx_fail_reason(TX_STATUS_FAIL_SHORT_LIMIT),
			 priv->reply_tx_stats.short_limit);
	pos += scnprintf(buf + pos, bufsz - pos, "%s:\t\t\t%u\n",
			 iwl_get_tx_fail_reason(TX_STATUS_FAIL_LONG_LIMIT),
			 priv->reply_tx_stats.long_limit);
	pos += scnprintf(buf + pos, bufsz - pos, "%s:\t\t\t%u\n",
			 iwl_get_tx_fail_reason(TX_STATUS_FAIL_FIFO_UNDERRUN),
			 priv->reply_tx_stats.fifo_underrun);
	pos += scnprintf(buf + pos, bufsz - pos, "%s:\t\t\t%u\n",
			 iwl_get_tx_fail_reason(TX_STATUS_FAIL_DRAIN_FLOW),
			 priv->reply_tx_stats.drain_flow);
	pos += scnprintf(buf + pos, bufsz - pos, "%s:\t\t\t%u\n",
			 iwl_get_tx_fail_reason(TX_STATUS_FAIL_RFKILL_FLUSH),
			 priv->reply_tx_stats.rfkill_flush);
	pos += scnprintf(buf + pos, bufsz - pos, "%s:\t\t\t%u\n",
			 iwl_get_tx_fail_reason(TX_STATUS_FAIL_LIFE_EXPIRE),
			 priv->reply_tx_stats.life_expire);
	pos += scnprintf(buf + pos, bufsz - pos, "%s:\t\t\t%u\n",
			 iwl_get_tx_fail_reason(TX_STATUS_FAIL_DEST_PS),
			 priv->reply_tx_stats.dest_ps);
	pos += scnprintf(buf + pos, bufsz - pos, "%s:\t\t\t%u\n",
			 iwl_get_tx_fail_reason(TX_STATUS_FAIL_HOST_ABORTED),
			 priv->reply_tx_stats.host_abort);
	pos += scnprintf(buf + pos, bufsz - pos, "%s:\t\t\t%u\n",
			 iwl_get_tx_fail_reason(TX_STATUS_FAIL_BT_RETRY),
			 priv->reply_tx_stats.pp_delay);
	pos += scnprintf(buf + pos, bufsz - pos, "%s:\t\t\t%u\n",
			 iwl_get_tx_fail_reason(TX_STATUS_FAIL_STA_INVALID),
			 priv->reply_tx_stats.sta_invalid);
	pos += scnprintf(buf + pos, bufsz - pos, "%s:\t\t\t%u\n",
			 iwl_get_tx_fail_reason(TX_STATUS_FAIL_FRAG_DROPPED),
			 priv->reply_tx_stats.frag_drop);
	pos += scnprintf(buf + pos, bufsz - pos, "%s:\t\t\t%u\n",
			 iwl_get_tx_fail_reason(TX_STATUS_FAIL_TID_DISABLE),
			 priv->reply_tx_stats.tid_disable);
	pos += scnprintf(buf + pos, bufsz - pos, "%s:\t\t\t%u\n",
			 iwl_get_tx_fail_reason(TX_STATUS_FAIL_FIFO_FLUSHED),
			 priv->reply_tx_stats.fifo_flush);
	pos += scnprintf(buf + pos, bufsz - pos, "%s:\t\t%u\n",
			 iwl_get_tx_fail_reason(
				TX_STATUS_FAIL_INSUFFICIENT_CF_POLL),
			 priv->reply_tx_stats.insuff_cf_poll);
	pos += scnprintf(buf + pos, bufsz - pos, "%s:\t\t\t%u\n",
			 iwl_get_tx_fail_reason(TX_STATUS_FAIL_PASSIVE_NO_RX),
			 priv->reply_tx_stats.fail_hw_drop);
	pos += scnprintf(buf + pos, bufsz - pos, "%s:\t\t%u\n",
			 iwl_get_tx_fail_reason(
				TX_STATUS_FAIL_NO_BEACON_ON_RADAR),
			 priv->reply_tx_stats.sta_color_mismatch);
	pos += scnprintf(buf + pos, bufsz - pos, "UNKNOWN:\t\t\t%u\n",
			 priv->reply_tx_stats.unknown);

	pos += scnprintf(buf + pos, bufsz - pos,
			 "\nStatistics_Agg_TX_Error:\n");

	pos += scnprintf(buf + pos, bufsz - pos, "%s:\t\t\t%u\n",
			 iwl_get_agg_tx_fail_reason(AGG_TX_STATE_UNDERRUN_MSK),
			 priv->reply_agg_tx_stats.underrun);
	pos += scnprintf(buf + pos, bufsz - pos, "%s:\t\t\t%u\n",
			 iwl_get_agg_tx_fail_reason(AGG_TX_STATE_BT_PRIO_MSK),
			 priv->reply_agg_tx_stats.bt_prio);
	pos += scnprintf(buf + pos, bufsz - pos, "%s:\t\t\t%u\n",
			 iwl_get_agg_tx_fail_reason(AGG_TX_STATE_FEW_BYTES_MSK),
			 priv->reply_agg_tx_stats.few_bytes);
	pos += scnprintf(buf + pos, bufsz - pos, "%s:\t\t\t%u\n",
			 iwl_get_agg_tx_fail_reason(AGG_TX_STATE_ABORT_MSK),
			 priv->reply_agg_tx_stats.abort);
	pos += scnprintf(buf + pos, bufsz - pos, "%s:\t\t%u\n",
			 iwl_get_agg_tx_fail_reason(
				AGG_TX_STATE_LAST_SENT_TTL_MSK),
			 priv->reply_agg_tx_stats.last_sent_ttl);
	pos += scnprintf(buf + pos, bufsz - pos, "%s:\t\t%u\n",
			 iwl_get_agg_tx_fail_reason(
				AGG_TX_STATE_LAST_SENT_TRY_CNT_MSK),
			 priv->reply_agg_tx_stats.last_sent_try);
	pos += scnprintf(buf + pos, bufsz - pos, "%s:\t\t%u\n",
			 iwl_get_agg_tx_fail_reason(
				AGG_TX_STATE_LAST_SENT_BT_KILL_MSK),
			 priv->reply_agg_tx_stats.last_sent_bt_kill);
	pos += scnprintf(buf + pos, bufsz - pos, "%s:\t\t\t%u\n",
			 iwl_get_agg_tx_fail_reason(AGG_TX_STATE_SCD_QUERY_MSK),
			 priv->reply_agg_tx_stats.scd_query);
	pos += scnprintf(buf + pos, bufsz - pos, "%s:\t\t%u\n",
			 iwl_get_agg_tx_fail_reason(
				AGG_TX_STATE_TEST_BAD_CRC32_MSK),
			 priv->reply_agg_tx_stats.bad_crc32);
	pos += scnprintf(buf + pos, bufsz - pos, "%s:\t\t\t%u\n",
			 iwl_get_agg_tx_fail_reason(AGG_TX_STATE_RESPONSE_MSK),
			 priv->reply_agg_tx_stats.response);
	pos += scnprintf(buf + pos, bufsz - pos, "%s:\t\t\t%u\n",
			 iwl_get_agg_tx_fail_reason(AGG_TX_STATE_DUMP_TX_MSK),
			 priv->reply_agg_tx_stats.dump_tx);
	pos += scnprintf(buf + pos, bufsz - pos, "%s:\t\t\t%u\n",
			 iwl_get_agg_tx_fail_reason(AGG_TX_STATE_DELAY_TX_MSK),
			 priv->reply_agg_tx_stats.delay_tx);
	pos += scnprintf(buf + pos, bufsz - pos, "UNKNOWN:\t\t\t%u\n",
			 priv->reply_agg_tx_stats.unknown);

	ret = simple_read_from_buffer(user_buf, count, ppos, buf, pos);
	kfree(buf);
	return ret;
}

static ssize_t iwl_dbgfs_sensitivity_read(struct file *file,
					char __user *user_buf,
					size_t count, loff_t *ppos) {

	struct iwl_priv *priv = file->private_data;
	int pos = 0;
	int cnt = 0;
	char *buf;
	int bufsz = sizeof(struct iwl_sensitivity_data) * 4 + 100;
	ssize_t ret;
	struct iwl_sensitivity_data *data;

	data = &priv->sensitivity_data;
	buf = kzalloc(bufsz, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	pos += scnprintf(buf + pos, bufsz - pos, "auto_corr_ofdm:\t\t\t %u\n",
			data->auto_corr_ofdm);
	pos += scnprintf(buf + pos, bufsz - pos,
			"auto_corr_ofdm_mrc:\t\t %u\n",
			data->auto_corr_ofdm_mrc);
	pos += scnprintf(buf + pos, bufsz - pos, "auto_corr_ofdm_x1:\t\t %u\n",
			data->auto_corr_ofdm_x1);
	pos += scnprintf(buf + pos, bufsz - pos,
			"auto_corr_ofdm_mrc_x1:\t\t %u\n",
			data->auto_corr_ofdm_mrc_x1);
	pos += scnprintf(buf + pos, bufsz - pos, "auto_corr_cck:\t\t\t %u\n",
			data->auto_corr_cck);
	pos += scnprintf(buf + pos, bufsz - pos, "auto_corr_cck_mrc:\t\t %u\n",
			data->auto_corr_cck_mrc);
	pos += scnprintf(buf + pos, bufsz - pos,
			"last_bad_plcp_cnt_ofdm:\t\t %u\n",
			data->last_bad_plcp_cnt_ofdm);
	pos += scnprintf(buf + pos, bufsz - pos, "last_fa_cnt_ofdm:\t\t %u\n",
			data->last_fa_cnt_ofdm);
	pos += scnprintf(buf + pos, bufsz - pos,
			"last_bad_plcp_cnt_cck:\t\t %u\n",
			data->last_bad_plcp_cnt_cck);
	pos += scnprintf(buf + pos, bufsz - pos, "last_fa_cnt_cck:\t\t %u\n",
			data->last_fa_cnt_cck);
	pos += scnprintf(buf + pos, bufsz - pos, "nrg_curr_state:\t\t\t %u\n",
			data->nrg_curr_state);
	pos += scnprintf(buf + pos, bufsz - pos, "nrg_prev_state:\t\t\t %u\n",
			data->nrg_prev_state);
	pos += scnprintf(buf + pos, bufsz - pos, "nrg_value:\t\t\t");
	for (cnt = 0; cnt < 10; cnt++) {
		pos += scnprintf(buf + pos, bufsz - pos, " %u",
				data->nrg_value[cnt]);
	}
	pos += scnprintf(buf + pos, bufsz - pos, "\n");
	pos += scnprintf(buf + pos, bufsz - pos, "nrg_silence_rssi:\t\t");
	for (cnt = 0; cnt < NRG_NUM_PREV_STAT_L; cnt++) {
		pos += scnprintf(buf + pos, bufsz - pos, " %u",
				data->nrg_silence_rssi[cnt]);
	}
	pos += scnprintf(buf + pos, bufsz - pos, "\n");
	pos += scnprintf(buf + pos, bufsz - pos, "nrg_silence_ref:\t\t %u\n",
			data->nrg_silence_ref);
	pos += scnprintf(buf + pos, bufsz - pos, "nrg_energy_idx:\t\t\t %u\n",
			data->nrg_energy_idx);
	pos += scnprintf(buf + pos, bufsz - pos, "nrg_silence_idx:\t\t %u\n",
			data->nrg_silence_idx);
	pos += scnprintf(buf + pos, bufsz - pos, "nrg_th_cck:\t\t\t %u\n",
			data->nrg_th_cck);
	pos += scnprintf(buf + pos, bufsz - pos,
			"nrg_auto_corr_silence_diff:\t %u\n",
			data->nrg_auto_corr_silence_diff);
	pos += scnprintf(buf + pos, bufsz - pos, "num_in_cck_no_fa:\t\t %u\n",
			data->num_in_cck_no_fa);
	pos += scnprintf(buf + pos, bufsz - pos, "nrg_th_ofdm:\t\t\t %u\n",
			data->nrg_th_ofdm);

	ret = simple_read_from_buffer(user_buf, count, ppos, buf, pos);
	kfree(buf);
	return ret;
}


static ssize_t iwl_dbgfs_chain_noise_read(struct file *file,
					char __user *user_buf,
					size_t count, loff_t *ppos) {

	struct iwl_priv *priv = file->private_data;
	int pos = 0;
	int cnt = 0;
	char *buf;
	int bufsz = sizeof(struct iwl_chain_noise_data) * 4 + 100;
	ssize_t ret;
	struct iwl_chain_noise_data *data;

	data = &priv->chain_noise_data;
	buf = kzalloc(bufsz, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	pos += scnprintf(buf + pos, bufsz - pos, "active_chains:\t\t\t %u\n",
			data->active_chains);
	pos += scnprintf(buf + pos, bufsz - pos, "chain_noise_a:\t\t\t %u\n",
			data->chain_noise_a);
	pos += scnprintf(buf + pos, bufsz - pos, "chain_noise_b:\t\t\t %u\n",
			data->chain_noise_b);
	pos += scnprintf(buf + pos, bufsz - pos, "chain_noise_c:\t\t\t %u\n",
			data->chain_noise_c);
	pos += scnprintf(buf + pos, bufsz - pos, "chain_signal_a:\t\t\t %u\n",
			data->chain_signal_a);
	pos += scnprintf(buf + pos, bufsz - pos, "chain_signal_b:\t\t\t %u\n",
			data->chain_signal_b);
	pos += scnprintf(buf + pos, bufsz - pos, "chain_signal_c:\t\t\t %u\n",
			data->chain_signal_c);
	pos += scnprintf(buf + pos, bufsz - pos, "beacon_count:\t\t\t %u\n",
			data->beacon_count);

	pos += scnprintf(buf + pos, bufsz - pos, "disconn_array:\t\t\t");
	for (cnt = 0; cnt < NUM_RX_CHAINS; cnt++) {
		pos += scnprintf(buf + pos, bufsz - pos, " %u",
				data->disconn_array[cnt]);
	}
	pos += scnprintf(buf + pos, bufsz - pos, "\n");
	pos += scnprintf(buf + pos, bufsz - pos, "delta_gain_code:\t\t");
	for (cnt = 0; cnt < NUM_RX_CHAINS; cnt++) {
		pos += scnprintf(buf + pos, bufsz - pos, " %u",
				data->delta_gain_code[cnt]);
	}
	pos += scnprintf(buf + pos, bufsz - pos, "\n");
	pos += scnprintf(buf + pos, bufsz - pos, "radio_write:\t\t\t %u\n",
			data->radio_write);
	pos += scnprintf(buf + pos, bufsz - pos, "state:\t\t\t\t %u\n",
			data->state);

	ret = simple_read_from_buffer(user_buf, count, ppos, buf, pos);
	kfree(buf);
	return ret;
}

static ssize_t iwl_dbgfs_power_save_status_read(struct file *file,
						    char __user *user_buf,
						    size_t count, loff_t *ppos)
{
	struct iwl_priv *priv = file->private_data;
	char buf[60];
	int pos = 0;
	const size_t bufsz = sizeof(buf);
	u32 pwrsave_status;

	pwrsave_status = iwl_read32(priv->trans, CSR_GP_CNTRL) &
			CSR_GP_REG_POWER_SAVE_STATUS_MSK;

	pos += scnprintf(buf + pos, bufsz - pos, "Power Save Status: ");
	pos += scnprintf(buf + pos, bufsz - pos, "%s\n",
		(pwrsave_status == CSR_GP_REG_NO_POWER_SAVE) ? "none" :
		(pwrsave_status == CSR_GP_REG_MAC_POWER_SAVE) ? "MAC" :
		(pwrsave_status == CSR_GP_REG_PHY_POWER_SAVE) ? "PHY" :
		"error");

	return simple_read_from_buffer(user_buf, count, ppos, buf, pos);
}

static ssize_t iwl_dbgfs_clear_ucode_statistics_write(struct file *file,
					 const char __user *user_buf,
					 size_t count, loff_t *ppos)
{
	struct iwl_priv *priv = file->private_data;
	char buf[8];
	int buf_size;
	int clear;

	memset(buf, 0, sizeof(buf));
	buf_size = min(count, sizeof(buf) -  1);
	if (copy_from_user(buf, user_buf, buf_size))
		return -EFAULT;
	if (sscanf(buf, "%d", &clear) != 1)
		return -EFAULT;

	/* make request to uCode to retrieve statistics information */
	mutex_lock(&priv->mutex);
	iwl_send_statistics_request(priv, CMD_SYNC, true);
	mutex_unlock(&priv->mutex);

	return count;
}

static ssize_t iwl_dbgfs_ucode_tracing_read(struct file *file,
					char __user *user_buf,
					size_t count, loff_t *ppos) {

	struct iwl_priv *priv = file->private_data;
	int pos = 0;
	char buf[128];
	const size_t bufsz = sizeof(buf);

	pos += scnprintf(buf + pos, bufsz - pos, "ucode trace timer is %s\n",
			priv->event_log.ucode_trace ? "On" : "Off");
	pos += scnprintf(buf + pos, bufsz - pos, "non_wraps_count:\t\t %u\n",
			priv->event_log.non_wraps_count);
	pos += scnprintf(buf + pos, bufsz - pos, "wraps_once_count:\t\t %u\n",
			priv->event_log.wraps_once_count);
	pos += scnprintf(buf + pos, bufsz - pos, "wraps_more_count:\t\t %u\n",
			priv->event_log.wraps_more_count);

	return simple_read_from_buffer(user_buf, count, ppos, buf, pos);
}

static ssize_t iwl_dbgfs_ucode_tracing_write(struct file *file,
					 const char __user *user_buf,
					 size_t count, loff_t *ppos)
{
	struct iwl_priv *priv = file->private_data;
	char buf[8];
	int buf_size;
	int trace;

	memset(buf, 0, sizeof(buf));
	buf_size = min(count, sizeof(buf) -  1);
	if (copy_from_user(buf, user_buf, buf_size))
		return -EFAULT;
	if (sscanf(buf, "%d", &trace) != 1)
		return -EFAULT;

	if (trace) {
		priv->event_log.ucode_trace = true;
		if (iwl_is_alive(priv)) {
			/* start collecting data now */
			mod_timer(&priv->ucode_trace, jiffies);
		}
	} else {
		priv->event_log.ucode_trace = false;
		del_timer_sync(&priv->ucode_trace);
	}

	return count;
}

static ssize_t iwl_dbgfs_rxon_flags_read(struct file *file,
					 char __user *user_buf,
					 size_t count, loff_t *ppos) {

	struct iwl_priv *priv = file->private_data;
	int len = 0;
	char buf[20];

	len = sprintf(buf, "0x%04X\n",
		le32_to_cpu(priv->contexts[IWL_RXON_CTX_BSS].active.flags));
	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static ssize_t iwl_dbgfs_rxon_filter_flags_read(struct file *file,
						char __user *user_buf,
						size_t count, loff_t *ppos) {

	struct iwl_priv *priv = file->private_data;
	int len = 0;
	char buf[20];

	len = sprintf(buf, "0x%04X\n",
		le32_to_cpu(priv->contexts[IWL_RXON_CTX_BSS].active.filter_flags));
	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static ssize_t iwl_dbgfs_missed_beacon_read(struct file *file,
					char __user *user_buf,
					size_t count, loff_t *ppos) {

	struct iwl_priv *priv = file->private_data;
	int pos = 0;
	char buf[12];
	const size_t bufsz = sizeof(buf);

	pos += scnprintf(buf + pos, bufsz - pos, "%d\n",
			priv->missed_beacon_threshold);

	return simple_read_from_buffer(user_buf, count, ppos, buf, pos);
}

static ssize_t iwl_dbgfs_missed_beacon_write(struct file *file,
					 const char __user *user_buf,
					 size_t count, loff_t *ppos)
{
	struct iwl_priv *priv = file->private_data;
	char buf[8];
	int buf_size;
	int missed;

	memset(buf, 0, sizeof(buf));
	buf_size = min(count, sizeof(buf) -  1);
	if (copy_from_user(buf, user_buf, buf_size))
		return -EFAULT;
	if (sscanf(buf, "%d", &missed) != 1)
		return -EINVAL;

	if (missed < IWL_MISSED_BEACON_THRESHOLD_MIN ||
	    missed > IWL_MISSED_BEACON_THRESHOLD_MAX)
		priv->missed_beacon_threshold =
			IWL_MISSED_BEACON_THRESHOLD_DEF;
	else
		priv->missed_beacon_threshold = missed;

	return count;
}

static ssize_t iwl_dbgfs_plcp_delta_read(struct file *file,
					char __user *user_buf,
					size_t count, loff_t *ppos) {

	struct iwl_priv *priv = file->private_data;
	int pos = 0;
	char buf[12];
	const size_t bufsz = sizeof(buf);

	pos += scnprintf(buf + pos, bufsz - pos, "%u\n",
			priv->plcp_delta_threshold);

	return simple_read_from_buffer(user_buf, count, ppos, buf, pos);
}

static ssize_t iwl_dbgfs_plcp_delta_write(struct file *file,
					const char __user *user_buf,
					size_t count, loff_t *ppos) {

	struct iwl_priv *priv = file->private_data;
	char buf[8];
	int buf_size;
	int plcp;

	memset(buf, 0, sizeof(buf));
	buf_size = min(count, sizeof(buf) -  1);
	if (copy_from_user(buf, user_buf, buf_size))
		return -EFAULT;
	if (sscanf(buf, "%d", &plcp) != 1)
		return -EINVAL;
	if ((plcp < IWL_MAX_PLCP_ERR_THRESHOLD_MIN) ||
		(plcp > IWL_MAX_PLCP_ERR_THRESHOLD_MAX))
		priv->plcp_delta_threshold =
			IWL_MAX_PLCP_ERR_THRESHOLD_DISABLE;
	else
		priv->plcp_delta_threshold = plcp;
	return count;
}

static ssize_t iwl_dbgfs_rf_reset_read(struct file *file,
				       char __user *user_buf,
				       size_t count, loff_t *ppos)
{
	struct iwl_priv *priv = file->private_data;
	int pos = 0;
	char buf[300];
	const size_t bufsz = sizeof(buf);
	struct iwl_rf_reset *rf_reset = &priv->rf_reset;

	pos += scnprintf(buf + pos, bufsz - pos,
			"RF reset statistics\n");
	pos += scnprintf(buf + pos, bufsz - pos,
			"\tnumber of reset request: %d\n",
			rf_reset->reset_request_count);
	pos += scnprintf(buf + pos, bufsz - pos,
			"\tnumber of reset request success: %d\n",
			rf_reset->reset_success_count);
	pos += scnprintf(buf + pos, bufsz - pos,
			"\tnumber of reset request reject: %d\n",
			rf_reset->reset_reject_count);

	return simple_read_from_buffer(user_buf, count, ppos, buf, pos);
}

static ssize_t iwl_dbgfs_rf_reset_write(struct file *file,
					const char __user *user_buf,
					size_t count, loff_t *ppos) {

	struct iwl_priv *priv = file->private_data;
	int ret;

	ret = iwl_force_rf_reset(priv, true);
	return ret ? ret : count;
}

static ssize_t iwl_dbgfs_txfifo_flush_write(struct file *file,
					const char __user *user_buf,
					size_t count, loff_t *ppos) {

	struct iwl_priv *priv = file->private_data;
	char buf[8];
	int buf_size;
	int flush;

	memset(buf, 0, sizeof(buf));
	buf_size = min(count, sizeof(buf) -  1);
	if (copy_from_user(buf, user_buf, buf_size))
		return -EFAULT;
	if (sscanf(buf, "%d", &flush) != 1)
		return -EINVAL;

	if (iwl_is_rfkill(priv))
		return -EFAULT;

	iwlagn_dev_txfifo_flush(priv);

	return count;
}

static ssize_t iwl_dbgfs_bt_traffic_read(struct file *file,
					char __user *user_buf,
					size_t count, loff_t *ppos) {

	struct iwl_priv *priv = (struct iwl_priv *)file->private_data;
	int pos = 0;
	char buf[200];
	const size_t bufsz = sizeof(buf);

	if (!priv->bt_enable_flag) {
		pos += scnprintf(buf + pos, bufsz - pos, "BT coex disabled\n");
		return simple_read_from_buffer(user_buf, count, ppos, buf, pos);
	}
	pos += scnprintf(buf + pos, bufsz - pos, "BT enable flag: 0x%x\n",
		priv->bt_enable_flag);
	pos += scnprintf(buf + pos, bufsz - pos, "BT in %s mode\n",
		priv->bt_full_concurrent ? "full concurrency" : "3-wire");
	pos += scnprintf(buf + pos, bufsz - pos, "BT status: %s, "
			 "last traffic notif: %d\n",
		priv->bt_status ? "On" : "Off", priv->last_bt_traffic_load);
	pos += scnprintf(buf + pos, bufsz - pos, "ch_announcement: %d, "
			 "kill_ack_mask: %x, kill_cts_mask: %x\n",
		priv->bt_ch_announce, priv->kill_ack_mask,
		priv->kill_cts_mask);

	pos += scnprintf(buf + pos, bufsz - pos, "bluetooth traffic load: ");
	switch (priv->bt_traffic_load) {
	case IWL_BT_COEX_TRAFFIC_LOAD_CONTINUOUS:
		pos += scnprintf(buf + pos, bufsz - pos, "Continuous\n");
		break;
	case IWL_BT_COEX_TRAFFIC_LOAD_HIGH:
		pos += scnprintf(buf + pos, bufsz - pos, "High\n");
		break;
	case IWL_BT_COEX_TRAFFIC_LOAD_LOW:
		pos += scnprintf(buf + pos, bufsz - pos, "Low\n");
		break;
	case IWL_BT_COEX_TRAFFIC_LOAD_NONE:
	default:
		pos += scnprintf(buf + pos, bufsz - pos, "None\n");
		break;
	}

	return simple_read_from_buffer(user_buf, count, ppos, buf, pos);
}

static ssize_t iwl_dbgfs_protection_mode_read(struct file *file,
					char __user *user_buf,
					size_t count, loff_t *ppos)
{
	struct iwl_priv *priv = (struct iwl_priv *)file->private_data;

	int pos = 0;
	char buf[40];
	const size_t bufsz = sizeof(buf);

	if (priv->cfg->ht_params)
		pos += scnprintf(buf + pos, bufsz - pos,
			 "use %s for aggregation\n",
			 (priv->hw_params.use_rts_for_aggregation) ?
				"rts/cts" : "cts-to-self");
	else
		pos += scnprintf(buf + pos, bufsz - pos, "N/A");

	return simple_read_from_buffer(user_buf, count, ppos, buf, pos);
}

static ssize_t iwl_dbgfs_protection_mode_write(struct file *file,
					const char __user *user_buf,
					size_t count, loff_t *ppos) {

	struct iwl_priv *priv = file->private_data;
	char buf[8];
	int buf_size;
	int rts;

	if (!priv->cfg->ht_params)
		return -EINVAL;

	memset(buf, 0, sizeof(buf));
	buf_size = min(count, sizeof(buf) -  1);
	if (copy_from_user(buf, user_buf, buf_size))
		return -EFAULT;
	if (sscanf(buf, "%d", &rts) != 1)
		return -EINVAL;
	if (rts)
		priv->hw_params.use_rts_for_aggregation = true;
	else
		priv->hw_params.use_rts_for_aggregation = false;
	return count;
}

static int iwl_cmd_echo_test(struct iwl_priv *priv)
{
	int ret;
	struct iwl_host_cmd cmd = {
		.id = REPLY_ECHO,
		.len = { 0 },
		.flags = CMD_SYNC,
	};

	ret = iwl_dvm_send_cmd(priv, &cmd);
	if (ret)
		IWL_ERR(priv, "echo testing fail: 0X%x\n", ret);
	else
		IWL_DEBUG_INFO(priv, "echo testing pass\n");
	return ret;
}

static ssize_t iwl_dbgfs_echo_test_write(struct file *file,
					const char __user *user_buf,
					size_t count, loff_t *ppos)
{
	struct iwl_priv *priv = file->private_data;
	char buf[8];
	int buf_size;

	memset(buf, 0, sizeof(buf));
	buf_size = min(count, sizeof(buf) -  1);
	if (copy_from_user(buf, user_buf, buf_size))
		return -EFAULT;

	iwl_cmd_echo_test(priv);
	return count;
}

#ifdef CONFIG_IWLWIFI_DEBUG
static ssize_t iwl_dbgfs_log_event_read(struct file *file,
					 char __user *user_buf,
					 size_t count, loff_t *ppos)
{
	struct iwl_priv *priv = file->private_data;
	char *buf;
	int pos = 0;
	ssize_t ret = -ENOMEM;

	ret = pos = iwl_dump_nic_event_log(priv, true, &buf, true);
	if (buf) {
		ret = simple_read_from_buffer(user_buf, count, ppos, buf, pos);
		kfree(buf);
	}
	return ret;
}

static ssize_t iwl_dbgfs_log_event_write(struct file *file,
					const char __user *user_buf,
					size_t count, loff_t *ppos)
{
	struct iwl_priv *priv = file->private_data;
	u32 event_log_flag;
	char buf[8];
	int buf_size;

	/* check that the interface is up */
	if (!iwl_is_ready(priv))
		return -EAGAIN;

	memset(buf, 0, sizeof(buf));
	buf_size = min(count, sizeof(buf) -  1);
	if (copy_from_user(buf, user_buf, buf_size))
		return -EFAULT;
	if (sscanf(buf, "%d", &event_log_flag) != 1)
		return -EFAULT;
	if (event_log_flag == 1)
		iwl_dump_nic_event_log(priv, true, NULL, false);

	return count;
}
#endif

static ssize_t iwl_dbgfs_calib_disabled_read(struct file *file,
					 char __user *user_buf,
					 size_t count, loff_t *ppos)
{
	struct iwl_priv *priv = file->private_data;
	char buf[120];
	int pos = 0;
	const size_t bufsz = sizeof(buf);

	pos += scnprintf(buf + pos, bufsz - pos,
			 "Sensitivity calibrations %s\n",
			 (priv->calib_disabled &
					IWL_SENSITIVITY_CALIB_DISABLED) ?
			 "DISABLED" : "ENABLED");
	pos += scnprintf(buf + pos, bufsz - pos,
			 "Chain noise calibrations %s\n",
			 (priv->calib_disabled &
					IWL_CHAIN_NOISE_CALIB_DISABLED) ?
			 "DISABLED" : "ENABLED");
	pos += scnprintf(buf + pos, bufsz - pos,
			 "Tx power calibrations %s\n",
			 (priv->calib_disabled &
					IWL_TX_POWER_CALIB_DISABLED) ?
			 "DISABLED" : "ENABLED");

	return simple_read_from_buffer(user_buf, count, ppos, buf, pos);
}

static ssize_t iwl_dbgfs_calib_disabled_write(struct file *file,
					      const char __user *user_buf,
					      size_t count, loff_t *ppos)
{
	struct iwl_priv *priv = file->private_data;
	char buf[8];
	u32 calib_disabled;
	int buf_size;

	memset(buf, 0, sizeof(buf));
	buf_size = min(count, sizeof(buf) - 1);
	if (copy_from_user(buf, user_buf, buf_size))
		return -EFAULT;
	if (sscanf(buf, "%x", &calib_disabled) != 1)
		return -EFAULT;

	priv->calib_disabled = calib_disabled;

	return count;
}

DEBUGFS_READ_FILE_OPS(ucode_rx_stats);
DEBUGFS_READ_FILE_OPS(ucode_tx_stats);
DEBUGFS_READ_FILE_OPS(ucode_general_stats);
DEBUGFS_READ_FILE_OPS(sensitivity);
DEBUGFS_READ_FILE_OPS(chain_noise);
DEBUGFS_READ_FILE_OPS(power_save_status);
DEBUGFS_WRITE_FILE_OPS(clear_ucode_statistics);
DEBUGFS_READ_WRITE_FILE_OPS(ucode_tracing);
DEBUGFS_READ_WRITE_FILE_OPS(missed_beacon);
DEBUGFS_READ_WRITE_FILE_OPS(plcp_delta);
DEBUGFS_READ_WRITE_FILE_OPS(rf_reset);
DEBUGFS_READ_FILE_OPS(rxon_flags);
DEBUGFS_READ_FILE_OPS(rxon_filter_flags);
DEBUGFS_WRITE_FILE_OPS(txfifo_flush);
DEBUGFS_READ_FILE_OPS(ucode_bt_stats);
DEBUGFS_READ_FILE_OPS(bt_traffic);
DEBUGFS_READ_WRITE_FILE_OPS(protection_mode);
DEBUGFS_READ_FILE_OPS(reply_tx_error);
DEBUGFS_WRITE_FILE_OPS(echo_test);
#ifdef CONFIG_IWLWIFI_DEBUG
DEBUGFS_READ_WRITE_FILE_OPS(log_event);
#endif
DEBUGFS_READ_WRITE_FILE_OPS(calib_disabled);

/*
 * Create the debugfs files and directories
 *
 */
int iwl_dbgfs_register(struct iwl_priv *priv, struct dentry *dbgfs_dir)
{
	struct dentry *dir_data, *dir_rf, *dir_debug;

	priv->debugfs_dir = dbgfs_dir;

	dir_data = debugfs_create_dir("data", dbgfs_dir);
	if (!dir_data)
		goto err;
	dir_rf = debugfs_create_dir("rf", dbgfs_dir);
	if (!dir_rf)
		goto err;
	dir_debug = debugfs_create_dir("debug", dbgfs_dir);
	if (!dir_debug)
		goto err;

	DEBUGFS_ADD_FILE(nvm, dir_data, S_IRUSR);
	DEBUGFS_ADD_FILE(sram, dir_data, S_IWUSR | S_IRUSR);
	DEBUGFS_ADD_FILE(wowlan_sram, dir_data, S_IRUSR);
	DEBUGFS_ADD_FILE(stations, dir_data, S_IRUSR);
	DEBUGFS_ADD_FILE(channels, dir_data, S_IRUSR);
	DEBUGFS_ADD_FILE(status, dir_data, S_IRUSR);
	DEBUGFS_ADD_FILE(rx_handlers, dir_data, S_IWUSR | S_IRUSR);
	DEBUGFS_ADD_FILE(qos, dir_data, S_IRUSR);
	DEBUGFS_ADD_FILE(sleep_level_override, dir_data, S_IWUSR | S_IRUSR);
	DEBUGFS_ADD_FILE(current_sleep_command, dir_data, S_IRUSR);
	DEBUGFS_ADD_FILE(thermal_throttling, dir_data, S_IRUSR);
	DEBUGFS_ADD_FILE(disable_ht40, dir_data, S_IWUSR | S_IRUSR);
	DEBUGFS_ADD_FILE(temperature, dir_data, S_IRUSR);

	DEBUGFS_ADD_FILE(power_save_status, dir_debug, S_IRUSR);
	DEBUGFS_ADD_FILE(clear_ucode_statistics, dir_debug, S_IWUSR);
	DEBUGFS_ADD_FILE(missed_beacon, dir_debug, S_IWUSR);
	DEBUGFS_ADD_FILE(plcp_delta, dir_debug, S_IWUSR | S_IRUSR);
	DEBUGFS_ADD_FILE(rf_reset, dir_debug, S_IWUSR | S_IRUSR);
	DEBUGFS_ADD_FILE(ucode_rx_stats, dir_debug, S_IRUSR);
	DEBUGFS_ADD_FILE(ucode_tx_stats, dir_debug, S_IRUSR);
	DEBUGFS_ADD_FILE(ucode_general_stats, dir_debug, S_IRUSR);
	DEBUGFS_ADD_FILE(txfifo_flush, dir_debug, S_IWUSR);
	DEBUGFS_ADD_FILE(protection_mode, dir_debug, S_IWUSR | S_IRUSR);
	DEBUGFS_ADD_FILE(sensitivity, dir_debug, S_IRUSR);
	DEBUGFS_ADD_FILE(chain_noise, dir_debug, S_IRUSR);
	DEBUGFS_ADD_FILE(ucode_tracing, dir_debug, S_IWUSR | S_IRUSR);
	DEBUGFS_ADD_FILE(ucode_bt_stats, dir_debug, S_IRUSR);
	DEBUGFS_ADD_FILE(reply_tx_error, dir_debug, S_IRUSR);
	DEBUGFS_ADD_FILE(rxon_flags, dir_debug, S_IWUSR);
	DEBUGFS_ADD_FILE(rxon_filter_flags, dir_debug, S_IWUSR);
	DEBUGFS_ADD_FILE(echo_test, dir_debug, S_IWUSR);
#ifdef CONFIG_IWLWIFI_DEBUG
	DEBUGFS_ADD_FILE(log_event, dir_debug, S_IWUSR | S_IRUSR);
#endif

	if (iwl_advanced_bt_coexist(priv))
		DEBUGFS_ADD_FILE(bt_traffic, dir_debug, S_IRUSR);

	/* Calibrations disabled/enabled status*/
	DEBUGFS_ADD_FILE(calib_disabled, dir_rf, S_IWUSR | S_IRUSR);

	/*
	 * Create a symlink with mac80211. This is not very robust, as it does
	 * not remove the symlink created. The implicit assumption is that
	 * when the opmode exits, mac80211 will also exit, and will remove
	 * this symlink as part of its cleanup.
	 */
	if (priv->mac80211_registered) {
		char buf[100];
		struct dentry *mac80211_dir, *dev_dir, *root_dir;

		dev_dir = dbgfs_dir->d_parent;
		root_dir = dev_dir->d_parent;
		mac80211_dir = priv->hw->wiphy->debugfsdir;

		snprintf(buf, 100, "../../%s/%s", root_dir->d_name.name,
			 dev_dir->d_name.name);

		if (!debugfs_create_symlink("iwlwifi", mac80211_dir, buf))
			goto err;
	}

	return 0;

err:
	IWL_ERR(priv, "failed to create the dvm debugfs entries\n");
	return -ENOMEM;
}
