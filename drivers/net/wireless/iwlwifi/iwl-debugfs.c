/******************************************************************************
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2008 - 2009 Intel Corporation. All rights reserved.
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
 *****************************************************************************/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/debugfs.h>

#include <linux/ieee80211.h>
#include <net/mac80211.h>


#include "iwl-dev.h"
#include "iwl-debug.h"
#include "iwl-core.h"
#include "iwl-io.h"
#include "iwl-calib.h"

/* create and remove of files */
#define DEBUGFS_ADD_DIR(name, parent) do {                              \
	dbgfs->dir_##name = debugfs_create_dir(#name, parent);          \
	if (!(dbgfs->dir_##name))                                       \
		goto err; 						\
} while (0)

#define DEBUGFS_ADD_FILE(name, parent, mode) do {                       \
	dbgfs->dbgfs_##parent##_files.file_##name =                     \
	debugfs_create_file(#name, mode,                                \
				dbgfs->dir_##parent, priv,              \
				&iwl_dbgfs_##name##_ops);               \
	if (!(dbgfs->dbgfs_##parent##_files.file_##name))               \
		goto err;                                               \
} while (0)

#define DEBUGFS_ADD_BOOL(name, parent, ptr) do {                        \
	dbgfs->dbgfs_##parent##_files.file_##name =                     \
	debugfs_create_bool(#name, S_IWUSR | S_IRUSR,                   \
			    dbgfs->dir_##parent, ptr);                  \
	if (IS_ERR(dbgfs->dbgfs_##parent##_files.file_##name)		\
			|| !dbgfs->dbgfs_##parent##_files.file_##name)	\
		goto err;                                               \
} while (0)

#define DEBUGFS_ADD_X32(name, parent, ptr) do {                        \
	dbgfs->dbgfs_##parent##_files.file_##name =                     \
	debugfs_create_x32(#name, S_IRUSR, dbgfs->dir_##parent, ptr);   \
	if (IS_ERR(dbgfs->dbgfs_##parent##_files.file_##name)		\
			|| !dbgfs->dbgfs_##parent##_files.file_##name)	\
		goto err;                                               \
} while (0)

#define DEBUGFS_REMOVE(name)  do {              \
	debugfs_remove(name);                   \
	name = NULL;                            \
} while (0);

/* file operation */
#define DEBUGFS_READ_FUNC(name)                                         \
static ssize_t iwl_dbgfs_##name##_read(struct file *file,               \
					char __user *user_buf,          \
					size_t count, loff_t *ppos);

#define DEBUGFS_WRITE_FUNC(name)                                        \
static ssize_t iwl_dbgfs_##name##_write(struct file *file,              \
					const char __user *user_buf,    \
					size_t count, loff_t *ppos);


static int iwl_dbgfs_open_file_generic(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private;
	return 0;
}

#define DEBUGFS_READ_FILE_OPS(name)                                     \
	DEBUGFS_READ_FUNC(name);                                        \
static const struct file_operations iwl_dbgfs_##name##_ops = {          \
	.read = iwl_dbgfs_##name##_read,                       		\
	.open = iwl_dbgfs_open_file_generic,                    	\
};

#define DEBUGFS_WRITE_FILE_OPS(name)                                    \
	DEBUGFS_WRITE_FUNC(name);                                       \
static const struct file_operations iwl_dbgfs_##name##_ops = {          \
	.write = iwl_dbgfs_##name##_write,                              \
	.open = iwl_dbgfs_open_file_generic,                    	\
};


#define DEBUGFS_READ_WRITE_FILE_OPS(name)                               \
	DEBUGFS_READ_FUNC(name);                                        \
	DEBUGFS_WRITE_FUNC(name);                                       \
static const struct file_operations iwl_dbgfs_##name##_ops = {          \
	.write = iwl_dbgfs_##name##_write,                              \
	.read = iwl_dbgfs_##name##_read,                                \
	.open = iwl_dbgfs_open_file_generic,                            \
};


static ssize_t iwl_dbgfs_tx_statistics_read(struct file *file,
						char __user *user_buf,
						size_t count, loff_t *ppos) {

	struct iwl_priv *priv = (struct iwl_priv *)file->private_data;
	char *buf;
	int pos = 0;

	int cnt;
	ssize_t ret;
	const size_t bufsz = 100 +
		sizeof(char) * 50 * (MANAGEMENT_MAX + CONTROL_MAX);
	buf = kzalloc(bufsz, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;
	pos += scnprintf(buf + pos, bufsz - pos, "Management:\n");
	for (cnt = 0; cnt < MANAGEMENT_MAX; cnt++) {
		pos += scnprintf(buf + pos, bufsz - pos,
				 "\t%25s\t\t: %u\n",
				 get_mgmt_string(cnt),
				 priv->tx_stats.mgmt[cnt]);
	}
	pos += scnprintf(buf + pos, bufsz - pos, "Control\n");
	for (cnt = 0; cnt < CONTROL_MAX; cnt++) {
		pos += scnprintf(buf + pos, bufsz - pos,
				 "\t%25s\t\t: %u\n",
				 get_ctrl_string(cnt),
				 priv->tx_stats.ctrl[cnt]);
	}
	pos += scnprintf(buf + pos, bufsz - pos, "Data:\n");
	pos += scnprintf(buf + pos, bufsz - pos, "\tcnt: %u\n",
			 priv->tx_stats.data_cnt);
	pos += scnprintf(buf + pos, bufsz - pos, "\tbytes: %llu\n",
			 priv->tx_stats.data_bytes);
	ret = simple_read_from_buffer(user_buf, count, ppos, buf, pos);
	kfree(buf);
	return ret;
}

static ssize_t iwl_dbgfs_clear_traffic_statistics_write(struct file *file,
					const char __user *user_buf,
					size_t count, loff_t *ppos)
{
	struct iwl_priv *priv = file->private_data;
	u32 clear_flag;
	char buf[8];
	int buf_size;

	memset(buf, 0, sizeof(buf));
	buf_size = min(count, sizeof(buf) -  1);
	if (copy_from_user(buf, user_buf, buf_size))
		return -EFAULT;
	if (sscanf(buf, "%x", &clear_flag) != 1)
		return -EFAULT;
	iwl_clear_traffic_stats(priv);

	return count;
}

static ssize_t iwl_dbgfs_rx_statistics_read(struct file *file,
						char __user *user_buf,
						size_t count, loff_t *ppos) {

	struct iwl_priv *priv = (struct iwl_priv *)file->private_data;
	char *buf;
	int pos = 0;
	int cnt;
	ssize_t ret;
	const size_t bufsz = 100 +
		sizeof(char) * 50 * (MANAGEMENT_MAX + CONTROL_MAX);
	buf = kzalloc(bufsz, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	pos += scnprintf(buf + pos, bufsz - pos, "Management:\n");
	for (cnt = 0; cnt < MANAGEMENT_MAX; cnt++) {
		pos += scnprintf(buf + pos, bufsz - pos,
				 "\t%25s\t\t: %u\n",
				 get_mgmt_string(cnt),
				 priv->rx_stats.mgmt[cnt]);
	}
	pos += scnprintf(buf + pos, bufsz - pos, "Control:\n");
	for (cnt = 0; cnt < CONTROL_MAX; cnt++) {
		pos += scnprintf(buf + pos, bufsz - pos,
				 "\t%25s\t\t: %u\n",
				 get_ctrl_string(cnt),
				 priv->rx_stats.ctrl[cnt]);
	}
	pos += scnprintf(buf + pos, bufsz - pos, "Data:\n");
	pos += scnprintf(buf + pos, bufsz - pos, "\tcnt: %u\n",
			 priv->rx_stats.data_cnt);
	pos += scnprintf(buf + pos, bufsz - pos, "\tbytes: %llu\n",
			 priv->rx_stats.data_bytes);

	ret = simple_read_from_buffer(user_buf, count, ppos, buf, pos);
	kfree(buf);
	return ret;
}

#define BYTE1_MASK 0x000000ff;
#define BYTE2_MASK 0x0000ffff;
#define BYTE3_MASK 0x00ffffff;
static ssize_t iwl_dbgfs_sram_read(struct file *file,
					char __user *user_buf,
					size_t count, loff_t *ppos)
{
	u32 val;
	char *buf;
	ssize_t ret;
	int i;
	int pos = 0;
	struct iwl_priv *priv = (struct iwl_priv *)file->private_data;
	size_t bufsz;

	/* default is to dump the entire data segment */
	if (!priv->dbgfs->sram_offset && !priv->dbgfs->sram_len) {
		priv->dbgfs->sram_offset = 0x800000;
		if (priv->ucode_type == UCODE_INIT)
			priv->dbgfs->sram_len = priv->ucode_init_data.len;
		else
			priv->dbgfs->sram_len = priv->ucode_data.len;
	}
	bufsz =  30 + priv->dbgfs->sram_len * sizeof(char) * 10;
	buf = kmalloc(bufsz, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;
	pos += scnprintf(buf + pos, bufsz - pos, "sram_len: 0x%x\n",
			priv->dbgfs->sram_len);
	pos += scnprintf(buf + pos, bufsz - pos, "sram_offset: 0x%x\n",
			priv->dbgfs->sram_offset);
	for (i = priv->dbgfs->sram_len; i > 0; i -= 4) {
		val = iwl_read_targ_mem(priv, priv->dbgfs->sram_offset + \
					priv->dbgfs->sram_len - i);
		if (i < 4) {
			switch (i) {
			case 1:
				val &= BYTE1_MASK;
				break;
			case 2:
				val &= BYTE2_MASK;
				break;
			case 3:
				val &= BYTE3_MASK;
				break;
			}
		}
		if (!(i % 16))
			pos += scnprintf(buf + pos, bufsz - pos, "\n");
		pos += scnprintf(buf + pos, bufsz - pos, "0x%08x ", val);
	}
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
		priv->dbgfs->sram_offset = offset;
		priv->dbgfs->sram_len = len;
	} else {
		priv->dbgfs->sram_offset = 0;
		priv->dbgfs->sram_len = 0;
	}

	return count;
}

static ssize_t iwl_dbgfs_stations_read(struct file *file, char __user *user_buf,
					size_t count, loff_t *ppos)
{
	struct iwl_priv *priv = (struct iwl_priv *)file->private_data;
	struct iwl_station_entry *station;
	int max_sta = priv->hw_params.max_stations;
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

	for (i = 0; i < max_sta; i++) {
		station = &priv->stations[i];
		if (station->used) {
			pos += scnprintf(buf + pos, bufsz - pos,
					"station %d:\ngeneral data:\n", i+1);
			pos += scnprintf(buf + pos, bufsz - pos, "id: %u\n",
					station->sta.sta.sta_id);
			pos += scnprintf(buf + pos, bufsz - pos, "mode: %u\n",
					station->sta.mode);
			pos += scnprintf(buf + pos, bufsz - pos,
					"flags: 0x%x\n",
					station->sta.station_flags_msk);
			pos += scnprintf(buf + pos, bufsz - pos, "tid data:\n");
			pos += scnprintf(buf + pos, bufsz - pos,
					"seq_num\t\ttxq_id");
			pos += scnprintf(buf + pos, bufsz - pos,
					"\tframe_count\twait_for_ba\t");
			pos += scnprintf(buf + pos, bufsz - pos,
					"start_idx\tbitmap0\t");
			pos += scnprintf(buf + pos, bufsz - pos,
					"bitmap1\trate_n_flags");
			pos += scnprintf(buf + pos, bufsz - pos, "\n");

			for (j = 0; j < MAX_TID_COUNT; j++) {
				pos += scnprintf(buf + pos, bufsz - pos,
						"[%d]:\t\t%u", j,
						station->tid[j].seq_number);
				pos += scnprintf(buf + pos, bufsz - pos,
						"\t%u\t\t%u\t\t%u\t\t",
						station->tid[j].agg.txq_id,
						station->tid[j].agg.frame_count,
						station->tid[j].agg.wait_for_ba);
				pos += scnprintf(buf + pos, bufsz - pos,
						"%u\t%llu\t%u",
						station->tid[j].agg.start_idx,
						(unsigned long long)station->tid[j].agg.bitmap,
						station->tid[j].agg.rate_n_flags);
				pos += scnprintf(buf + pos, bufsz - pos, "\n");
			}
			pos += scnprintf(buf + pos, bufsz - pos, "\n");
		}
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
	struct iwl_priv *priv = (struct iwl_priv *)file->private_data;
	int pos = 0, ofs = 0, buf_size = 0;
	const u8 *ptr;
	char *buf;
	u16 eeprom_ver;
	size_t eeprom_len = priv->cfg->eeprom_size;
	buf_size = 4 * eeprom_len + 256;

	if (eeprom_len % 16) {
		IWL_ERR(priv, "NVM size is not multiple of 16.\n");
		return -ENODATA;
	}

	ptr = priv->eeprom;
	if (!ptr) {
		IWL_ERR(priv, "Invalid EEPROM/OTP memory\n");
		return -ENOMEM;
	}

	/* 4 characters for byte 0xYY */
	buf = kzalloc(buf_size, GFP_KERNEL);
	if (!buf) {
		IWL_ERR(priv, "Can not allocate Buffer\n");
		return -ENOMEM;
	}
	eeprom_ver = iwl_eeprom_query16(priv, EEPROM_VERSION);
	pos += scnprintf(buf + pos, buf_size - pos, "NVM Type: %s, "
			"version: 0x%x\n",
			(priv->nvm_device_type == NVM_DEVICE_TYPE_OTP)
			 ? "OTP" : "EEPROM", eeprom_ver);
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

static ssize_t iwl_dbgfs_log_event_write(struct file *file,
					const char __user *user_buf,
					size_t count, loff_t *ppos)
{
	struct iwl_priv *priv = file->private_data;
	u32 event_log_flag;
	char buf[8];
	int buf_size;

	memset(buf, 0, sizeof(buf));
	buf_size = min(count, sizeof(buf) -  1);
	if (copy_from_user(buf, user_buf, buf_size))
		return -EFAULT;
	if (sscanf(buf, "%d", &event_log_flag) != 1)
		return -EFAULT;
	if (event_log_flag == 1)
		priv->cfg->ops->lib->dump_nic_event_log(priv, true);

	return count;
}



static ssize_t iwl_dbgfs_channels_read(struct file *file, char __user *user_buf,
				       size_t count, loff_t *ppos)
{
	struct iwl_priv *priv = (struct iwl_priv *)file->private_data;
	struct ieee80211_channel *channels = NULL;
	const struct ieee80211_supported_band *supp_band = NULL;
	int pos = 0, i, bufsz = PAGE_SIZE;
	char *buf;
	ssize_t ret;

	if (!test_bit(STATUS_GEO_CONFIGURED, &priv->status))
		return -EAGAIN;

	buf = kzalloc(bufsz, GFP_KERNEL);
	if (!buf) {
		IWL_ERR(priv, "Can not allocate Buffer\n");
		return -ENOMEM;
	}

	supp_band = iwl_get_hw_mode(priv, IEEE80211_BAND_2GHZ);
	if (supp_band) {
		channels = supp_band->channels;

		pos += scnprintf(buf + pos, bufsz - pos,
				"Displaying %d channels in 2.4GHz band 802.11bg):\n",
				supp_band->n_channels);

		for (i = 0; i < supp_band->n_channels; i++)
			pos += scnprintf(buf + pos, bufsz - pos,
					"%d: %ddBm: BSS%s%s, %s.\n",
					ieee80211_frequency_to_channel(
					channels[i].center_freq),
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
					ieee80211_frequency_to_channel(
					channels[i].center_freq),
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

	struct iwl_priv *priv = (struct iwl_priv *)file->private_data;
	char buf[512];
	int pos = 0;
	const size_t bufsz = sizeof(buf);

	pos += scnprintf(buf + pos, bufsz - pos, "STATUS_HCMD_ACTIVE:\t %d\n",
		test_bit(STATUS_HCMD_ACTIVE, &priv->status));
	pos += scnprintf(buf + pos, bufsz - pos, "STATUS_HCMD_SYNC_ACTIVE: %d\n",
		test_bit(STATUS_HCMD_SYNC_ACTIVE, &priv->status));
	pos += scnprintf(buf + pos, bufsz - pos, "STATUS_INT_ENABLED:\t %d\n",
		test_bit(STATUS_INT_ENABLED, &priv->status));
	pos += scnprintf(buf + pos, bufsz - pos, "STATUS_RF_KILL_HW:\t %d\n",
		test_bit(STATUS_RF_KILL_HW, &priv->status));
	pos += scnprintf(buf + pos, bufsz - pos, "STATUS_CT_KILL:\t\t %d\n",
		test_bit(STATUS_CT_KILL, &priv->status));
	pos += scnprintf(buf + pos, bufsz - pos, "STATUS_INIT:\t\t %d\n",
		test_bit(STATUS_INIT, &priv->status));
	pos += scnprintf(buf + pos, bufsz - pos, "STATUS_ALIVE:\t\t %d\n",
		test_bit(STATUS_ALIVE, &priv->status));
	pos += scnprintf(buf + pos, bufsz - pos, "STATUS_READY:\t\t %d\n",
		test_bit(STATUS_READY, &priv->status));
	pos += scnprintf(buf + pos, bufsz - pos, "STATUS_TEMPERATURE:\t %d\n",
		test_bit(STATUS_TEMPERATURE, &priv->status));
	pos += scnprintf(buf + pos, bufsz - pos, "STATUS_GEO_CONFIGURED:\t %d\n",
		test_bit(STATUS_GEO_CONFIGURED, &priv->status));
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
	pos += scnprintf(buf + pos, bufsz - pos, "STATUS_MODE_PENDING:\t %d\n",
		test_bit(STATUS_MODE_PENDING, &priv->status));
	return simple_read_from_buffer(user_buf, count, ppos, buf, pos);
}

static ssize_t iwl_dbgfs_interrupt_read(struct file *file,
					char __user *user_buf,
					size_t count, loff_t *ppos) {

	struct iwl_priv *priv = (struct iwl_priv *)file->private_data;
	int pos = 0;
	int cnt = 0;
	char *buf;
	int bufsz = 24 * 64; /* 24 items * 64 char per item */
	ssize_t ret;

	buf = kzalloc(bufsz, GFP_KERNEL);
	if (!buf) {
		IWL_ERR(priv, "Can not allocate Buffer\n");
		return -ENOMEM;
	}

	pos += scnprintf(buf + pos, bufsz - pos,
			"Interrupt Statistics Report:\n");

	pos += scnprintf(buf + pos, bufsz - pos, "HW Error:\t\t\t %u\n",
		priv->isr_stats.hw);
	pos += scnprintf(buf + pos, bufsz - pos, "SW Error:\t\t\t %u\n",
		priv->isr_stats.sw);
	if (priv->isr_stats.sw > 0) {
		pos += scnprintf(buf + pos, bufsz - pos,
			"\tLast Restarting Code:  0x%X\n",
			priv->isr_stats.sw_err);
	}
#ifdef CONFIG_IWLWIFI_DEBUG
	pos += scnprintf(buf + pos, bufsz - pos, "Frame transmitted:\t\t %u\n",
		priv->isr_stats.sch);
	pos += scnprintf(buf + pos, bufsz - pos, "Alive interrupt:\t\t %u\n",
		priv->isr_stats.alive);
#endif
	pos += scnprintf(buf + pos, bufsz - pos,
		"HW RF KILL switch toggled:\t %u\n",
		priv->isr_stats.rfkill);

	pos += scnprintf(buf + pos, bufsz - pos, "CT KILL:\t\t\t %u\n",
		priv->isr_stats.ctkill);

	pos += scnprintf(buf + pos, bufsz - pos, "Wakeup Interrupt:\t\t %u\n",
		priv->isr_stats.wakeup);

	pos += scnprintf(buf + pos, bufsz - pos,
		"Rx command responses:\t\t %u\n",
		priv->isr_stats.rx);
	for (cnt = 0; cnt < REPLY_MAX; cnt++) {
		if (priv->isr_stats.rx_handlers[cnt] > 0)
			pos += scnprintf(buf + pos, bufsz - pos,
				"\tRx handler[%36s]:\t\t %u\n",
				get_cmd_string(cnt),
				priv->isr_stats.rx_handlers[cnt]);
	}

	pos += scnprintf(buf + pos, bufsz - pos, "Tx/FH interrupt:\t\t %u\n",
		priv->isr_stats.tx);

	pos += scnprintf(buf + pos, bufsz - pos, "Unexpected INTA:\t\t %u\n",
		priv->isr_stats.unhandled);

	ret = simple_read_from_buffer(user_buf, count, ppos, buf, pos);
	kfree(buf);
	return ret;
}

static ssize_t iwl_dbgfs_interrupt_write(struct file *file,
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
		iwl_clear_isr_stats(priv);

	return count;
}

static ssize_t iwl_dbgfs_qos_read(struct file *file, char __user *user_buf,
				       size_t count, loff_t *ppos)
{
	struct iwl_priv *priv = (struct iwl_priv *)file->private_data;
	int pos = 0, i;
	char buf[256];
	const size_t bufsz = sizeof(buf);
	ssize_t ret;

	for (i = 0; i < AC_NUM; i++) {
		pos += scnprintf(buf + pos, bufsz - pos,
			"\tcw_min\tcw_max\taifsn\ttxop\n");
		pos += scnprintf(buf + pos, bufsz - pos,
				"AC[%d]\t%u\t%u\t%u\t%u\n", i,
				priv->qos_data.def_qos_parm.ac[i].cw_min,
				priv->qos_data.def_qos_parm.ac[i].cw_max,
				priv->qos_data.def_qos_parm.ac[i].aifsn,
				priv->qos_data.def_qos_parm.ac[i].edca_txop);
	}
	ret = simple_read_from_buffer(user_buf, count, ppos, buf, pos);
	return ret;
}

static ssize_t iwl_dbgfs_led_read(struct file *file, char __user *user_buf,
				  size_t count, loff_t *ppos)
{
	struct iwl_priv *priv = (struct iwl_priv *)file->private_data;
	int pos = 0;
	char buf[256];
	const size_t bufsz = sizeof(buf);
	ssize_t ret;

	pos += scnprintf(buf + pos, bufsz - pos,
			 "allow blinking: %s\n",
			 (priv->allow_blinking) ? "True" : "False");
	if (priv->allow_blinking) {
		pos += scnprintf(buf + pos, bufsz - pos,
				 "Led blinking rate: %u\n",
				 priv->last_blink_rate);
		pos += scnprintf(buf + pos, bufsz - pos,
				 "Last blink time: %lu\n",
				 priv->last_blink_time);
	}

	ret = simple_read_from_buffer(user_buf, count, ppos, buf, pos);
	return ret;
}

static ssize_t iwl_dbgfs_thermal_throttling_read(struct file *file,
				char __user *user_buf,
				size_t count, loff_t *ppos)
{
	struct iwl_priv *priv = (struct iwl_priv *)file->private_data;
	struct iwl_tt_mgmt *tt = &priv->thermal_throttle;
	struct iwl_tt_restriction *restriction;
	char buf[100];
	int pos = 0;
	const size_t bufsz = sizeof(buf);
	ssize_t ret;

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
	ret = simple_read_from_buffer(user_buf, count, ppos, buf, pos);
	return ret;
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
	if (!iwl_is_associated(priv))
		priv->disable_ht40 = ht40 ? true : false;
	else {
		IWL_ERR(priv, "Sta associated with AP - "
			"Change to 40MHz channel support is not allowed\n");
		return -EINVAL;
	}

	return count;
}

static ssize_t iwl_dbgfs_disable_ht40_read(struct file *file,
					 char __user *user_buf,
					 size_t count, loff_t *ppos)
{
	struct iwl_priv *priv = (struct iwl_priv *)file->private_data;
	char buf[100];
	int pos = 0;
	const size_t bufsz = sizeof(buf);
	ssize_t ret;

	pos += scnprintf(buf + pos, bufsz - pos,
			"11n 40MHz Mode: %s\n",
			priv->disable_ht40 ? "Disabled" : "Enabled");
	ret = simple_read_from_buffer(user_buf, count, ppos, buf, pos);
	return ret;
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

	iwl_power_update_mode(priv, true);

	return count;
}

static ssize_t iwl_dbgfs_sleep_level_override_read(struct file *file,
						   char __user *user_buf,
						   size_t count, loff_t *ppos)
{
	struct iwl_priv *priv = (struct iwl_priv *)file->private_data;
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
	struct iwl_priv *priv = (struct iwl_priv *)file->private_data;
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
DEBUGFS_WRITE_FILE_OPS(log_event);
DEBUGFS_READ_FILE_OPS(nvm);
DEBUGFS_READ_FILE_OPS(stations);
DEBUGFS_READ_FILE_OPS(channels);
DEBUGFS_READ_FILE_OPS(status);
DEBUGFS_READ_WRITE_FILE_OPS(interrupt);
DEBUGFS_READ_FILE_OPS(qos);
DEBUGFS_READ_FILE_OPS(led);
DEBUGFS_READ_FILE_OPS(thermal_throttling);
DEBUGFS_READ_WRITE_FILE_OPS(disable_ht40);
DEBUGFS_READ_WRITE_FILE_OPS(sleep_level_override);
DEBUGFS_READ_FILE_OPS(current_sleep_command);

static ssize_t iwl_dbgfs_traffic_log_read(struct file *file,
					 char __user *user_buf,
					 size_t count, loff_t *ppos)
{
	struct iwl_priv *priv = file->private_data;
	int pos = 0, ofs = 0;
	int cnt = 0, entry;
	struct iwl_tx_queue *txq;
	struct iwl_queue *q;
	struct iwl_rx_queue *rxq = &priv->rxq;
	char *buf;
	int bufsz = ((IWL_TRAFFIC_ENTRIES * IWL_TRAFFIC_ENTRY_SIZE * 64) * 2) +
		(priv->cfg->num_of_queues * 32 * 8) + 400;
	const u8 *ptr;
	ssize_t ret;

	if (!priv->txq) {
		IWL_ERR(priv, "txq not ready\n");
		return -EAGAIN;
	}
	buf = kzalloc(bufsz, GFP_KERNEL);
	if (!buf) {
		IWL_ERR(priv, "Can not allocate buffer\n");
		return -ENOMEM;
	}
	pos += scnprintf(buf + pos, bufsz - pos, "Tx Queue\n");
	for (cnt = 0; cnt < priv->hw_params.max_txq_num; cnt++) {
		txq = &priv->txq[cnt];
		q = &txq->q;
		pos += scnprintf(buf + pos, bufsz - pos,
				"q[%d]: read_ptr: %u, write_ptr: %u\n",
				cnt, q->read_ptr, q->write_ptr);
	}
	if (priv->tx_traffic && (iwl_debug_level & IWL_DL_TX)) {
		ptr = priv->tx_traffic;
		pos += scnprintf(buf + pos, bufsz - pos,
				"Tx Traffic idx: %u\n",	priv->tx_traffic_idx);
		for (cnt = 0, ofs = 0; cnt < IWL_TRAFFIC_ENTRIES; cnt++) {
			for (entry = 0; entry < IWL_TRAFFIC_ENTRY_SIZE / 16;
			     entry++,  ofs += 16) {
				pos += scnprintf(buf + pos, bufsz - pos,
						"0x%.4x ", ofs);
				hex_dump_to_buffer(ptr + ofs, 16, 16, 2,
						   buf + pos, bufsz - pos, 0);
				pos += strlen(buf + pos);
				if (bufsz - pos > 0)
					buf[pos++] = '\n';
			}
		}
	}

	pos += scnprintf(buf + pos, bufsz - pos, "Rx Queue\n");
	pos += scnprintf(buf + pos, bufsz - pos,
			"read: %u, write: %u\n",
			 rxq->read, rxq->write);

	if (priv->rx_traffic && (iwl_debug_level & IWL_DL_RX)) {
		ptr = priv->rx_traffic;
		pos += scnprintf(buf + pos, bufsz - pos,
				"Rx Traffic idx: %u\n",	priv->rx_traffic_idx);
		for (cnt = 0, ofs = 0; cnt < IWL_TRAFFIC_ENTRIES; cnt++) {
			for (entry = 0; entry < IWL_TRAFFIC_ENTRY_SIZE / 16;
			     entry++,  ofs += 16) {
				pos += scnprintf(buf + pos, bufsz - pos,
						"0x%.4x ", ofs);
				hex_dump_to_buffer(ptr + ofs, 16, 16, 2,
						   buf + pos, bufsz - pos, 0);
				pos += strlen(buf + pos);
				if (bufsz - pos > 0)
					buf[pos++] = '\n';
			}
		}
	}

	ret = simple_read_from_buffer(user_buf, count, ppos, buf, pos);
	kfree(buf);
	return ret;
}

static ssize_t iwl_dbgfs_traffic_log_write(struct file *file,
					 const char __user *user_buf,
					 size_t count, loff_t *ppos)
{
	struct iwl_priv *priv = file->private_data;
	char buf[8];
	int buf_size;
	int traffic_log;

	memset(buf, 0, sizeof(buf));
	buf_size = min(count, sizeof(buf) -  1);
	if (copy_from_user(buf, user_buf, buf_size))
		return -EFAULT;
	if (sscanf(buf, "%d", &traffic_log) != 1)
		return -EFAULT;
	if (traffic_log == 0)
		iwl_reset_traffic_log(priv);

	return count;
}

static ssize_t iwl_dbgfs_tx_queue_read(struct file *file,
						char __user *user_buf,
						size_t count, loff_t *ppos) {

	struct iwl_priv *priv = (struct iwl_priv *)file->private_data;
	struct iwl_tx_queue *txq;
	struct iwl_queue *q;
	char *buf;
	int pos = 0;
	int cnt;
	int ret;
	const size_t bufsz = sizeof(char) * 64 * priv->cfg->num_of_queues;

	if (!priv->txq) {
		IWL_ERR(priv, "txq not ready\n");
		return -EAGAIN;
	}
	buf = kzalloc(bufsz, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	for (cnt = 0; cnt < priv->hw_params.max_txq_num; cnt++) {
		txq = &priv->txq[cnt];
		q = &txq->q;
		pos += scnprintf(buf + pos, bufsz - pos,
				"hwq %.2d: read=%u write=%u stop=%d"
				" swq_id=%#.2x (ac %d/hwq %d)\n",
				cnt, q->read_ptr, q->write_ptr,
				!!test_bit(cnt, priv->queue_stopped),
				txq->swq_id,
				txq->swq_id & 0x80 ? txq->swq_id & 3 :
				txq->swq_id,
				txq->swq_id & 0x80 ? (txq->swq_id >> 2) &
				0x1f : txq->swq_id);
		if (cnt >= 4)
			continue;
		/* for the ACs, display the stop count too */
		pos += scnprintf(buf + pos, bufsz - pos,
				"        stop-count: %d\n",
				atomic_read(&priv->queue_stop_count[cnt]));
	}
	ret = simple_read_from_buffer(user_buf, count, ppos, buf, pos);
	kfree(buf);
	return ret;
}

static ssize_t iwl_dbgfs_rx_queue_read(struct file *file,
						char __user *user_buf,
						size_t count, loff_t *ppos) {

	struct iwl_priv *priv = (struct iwl_priv *)file->private_data;
	struct iwl_rx_queue *rxq = &priv->rxq;
	char buf[256];
	int pos = 0;
	const size_t bufsz = sizeof(buf);

	pos += scnprintf(buf + pos, bufsz - pos, "read: %u\n",
						rxq->read);
	pos += scnprintf(buf + pos, bufsz - pos, "write: %u\n",
						rxq->write);
	pos += scnprintf(buf + pos, bufsz - pos, "free_count: %u\n",
						rxq->free_count);
	pos += scnprintf(buf + pos, bufsz - pos, "closed_rb_num: %u\n",
			 le16_to_cpu(rxq->rb_stts->closed_rb_num) &  0x0FFF);
	return simple_read_from_buffer(user_buf, count, ppos, buf, pos);
}

static int iwl_dbgfs_statistics_flag(struct iwl_priv *priv, char *buf,
				     int bufsz)
{
	int p = 0;

	p += scnprintf(buf + p, bufsz - p,
		"Statistics Flag(0x%X):\n",
		le32_to_cpu(priv->statistics.flag));
	if (le32_to_cpu(priv->statistics.flag) & UCODE_STATISTICS_CLEAR_MSK)
		p += scnprintf(buf + p, bufsz - p,
		"\tStatistics have been cleared\n");
	p += scnprintf(buf + p, bufsz - p,
		"\tOperational Frequency: %s\n",
		(le32_to_cpu(priv->statistics.flag) &
		UCODE_STATISTICS_FREQUENCY_MSK)
		 ? "2.4 GHz" : "5.2 GHz");
	p += scnprintf(buf + p, bufsz - p,
		"\tTGj Narrow Band: %s\n",
		(le32_to_cpu(priv->statistics.flag) &
		UCODE_STATISTICS_NARROW_BAND_MSK)
		 ? "enabled" : "disabled");
	return p;
}


static ssize_t iwl_dbgfs_ucode_rx_stats_read(struct file *file,
					char __user *user_buf,
					size_t count, loff_t *ppos)
{
	struct iwl_priv *priv = (struct iwl_priv *)file->private_data;
	int pos = 0;
	char *buf;
	int bufsz = sizeof(struct statistics_rx_phy) * 20 +
		sizeof(struct statistics_rx_non_phy) * 20 +
		sizeof(struct statistics_rx_ht_phy) * 20 + 400;
	ssize_t ret;
	struct statistics_rx_phy *ofdm, *accum_ofdm;
	struct statistics_rx_phy *cck, *accum_cck;
	struct statistics_rx_non_phy *general, *accum_general;
	struct statistics_rx_ht_phy *ht, *accum_ht;

	if (!iwl_is_alive(priv))
		return -EAGAIN;

	/* make request to uCode to retrieve statistics information */
	mutex_lock(&priv->mutex);
	ret = iwl_send_statistics_request(priv, CMD_SYNC, false);
	mutex_unlock(&priv->mutex);

	if (ret) {
		IWL_ERR(priv,
			"Error sending statistics request: %zd\n", ret);
		return -EAGAIN;
	}
	buf = kzalloc(bufsz, GFP_KERNEL);
	if (!buf) {
		IWL_ERR(priv, "Can not allocate Buffer\n");
		return -ENOMEM;
	}

	/* the statistic information display here is based on
	 * the last statistics notification from uCode
	 * might not reflect the current uCode activity
	 */
	ofdm = &priv->statistics.rx.ofdm;
	cck = &priv->statistics.rx.cck;
	general = &priv->statistics.rx.general;
	ht = &priv->statistics.rx.ofdm_ht;
	accum_ofdm = &priv->accum_statistics.rx.ofdm;
	accum_cck = &priv->accum_statistics.rx.cck;
	accum_general = &priv->accum_statistics.rx.general;
	accum_ht = &priv->accum_statistics.rx.ofdm_ht;
	pos += iwl_dbgfs_statistics_flag(priv, buf, bufsz);
	pos += scnprintf(buf + pos, bufsz - pos, "Statistics_Rx - OFDM:\n");
	pos += scnprintf(buf + pos, bufsz - pos,
			"\t\t\tcurrent\t\t\taccumulative\n");
	pos += scnprintf(buf + pos, bufsz - pos, "ina_cnt:\t\t%u\t\t\t%u\n",
			 le32_to_cpu(ofdm->ina_cnt), accum_ofdm->ina_cnt);
	pos += scnprintf(buf + pos, bufsz - pos, "fina_cnt:\t\t%u\t\t\t%u\n",
			 le32_to_cpu(ofdm->fina_cnt), accum_ofdm->fina_cnt);
	pos += scnprintf(buf + pos, bufsz - pos, "plcp_err:\t\t%u\t\t\t%u\n",
			 le32_to_cpu(ofdm->plcp_err), accum_ofdm->plcp_err);
	pos += scnprintf(buf + pos, bufsz - pos, "crc32_err:\t\t%u\t\t\t%u\n",
			 le32_to_cpu(ofdm->crc32_err), accum_ofdm->crc32_err);
	pos += scnprintf(buf + pos, bufsz - pos,
			 "overrun_err:\t\t%u\t\t\t%u\n",
			 le32_to_cpu(ofdm->overrun_err),
			 accum_ofdm->overrun_err);
	pos += scnprintf(buf + pos, bufsz - pos,
			 "early_overrun_err:\t%u\t\t\t%u\n",
			 le32_to_cpu(ofdm->early_overrun_err),
			 accum_ofdm->early_overrun_err);
	pos += scnprintf(buf + pos, bufsz - pos, "crc32_good:\t\t%u\t\t\t%u\n",
			 le32_to_cpu(ofdm->crc32_good),
			 accum_ofdm->crc32_good);
	pos += scnprintf(buf + pos, bufsz - pos,
			 "false_alarm_cnt:\t%u\t\t\t%u\n",
			 le32_to_cpu(ofdm->false_alarm_cnt),
			 accum_ofdm->false_alarm_cnt);
	pos += scnprintf(buf + pos, bufsz - pos,
			 "fina_sync_err_cnt:\t%u\t\t\t%u\n",
			 le32_to_cpu(ofdm->fina_sync_err_cnt),
			 accum_ofdm->fina_sync_err_cnt);
	pos += scnprintf(buf + pos, bufsz - pos,
			 "sfd_timeout:\t\t%u\t\t\t%u\n",
			 le32_to_cpu(ofdm->sfd_timeout),
			 accum_ofdm->sfd_timeout);
	pos += scnprintf(buf + pos, bufsz - pos,
			 "fina_timeout:\t\t%u\t\t\t%u\n",
			 le32_to_cpu(ofdm->fina_timeout),
			 accum_ofdm->fina_timeout);
	pos += scnprintf(buf + pos, bufsz - pos,
			 "unresponded_rts:\t%u\t\t\t%u\n",
			 le32_to_cpu(ofdm->unresponded_rts),
			 accum_ofdm->unresponded_rts);
	pos += scnprintf(buf + pos, bufsz - pos,
			"rxe_frame_lmt_ovrun:\t%u\t\t\t%u\n",
			 le32_to_cpu(ofdm->rxe_frame_limit_overrun),
			 accum_ofdm->rxe_frame_limit_overrun);
	pos += scnprintf(buf + pos, bufsz - pos,
			 "sent_ack_cnt:\t\t%u\t\t\t%u\n",
			 le32_to_cpu(ofdm->sent_ack_cnt),
			 accum_ofdm->sent_ack_cnt);
	pos += scnprintf(buf + pos, bufsz - pos,
			 "sent_cts_cnt:\t\t%u\t\t\t%u\n",
			 le32_to_cpu(ofdm->sent_cts_cnt),
			 accum_ofdm->sent_cts_cnt);
	pos += scnprintf(buf + pos, bufsz - pos,
			 "sent_ba_rsp_cnt:\t%u\t\t\t%u\n",
			 le32_to_cpu(ofdm->sent_ba_rsp_cnt),
			 accum_ofdm->sent_ba_rsp_cnt);
	pos += scnprintf(buf + pos, bufsz - pos,
			 "dsp_self_kill:\t\t%u\t\t\t%u\n",
			 le32_to_cpu(ofdm->dsp_self_kill),
			 accum_ofdm->dsp_self_kill);
	pos += scnprintf(buf + pos, bufsz - pos,
			 "mh_format_err:\t\t%u\t\t\t%u\n",
			 le32_to_cpu(ofdm->mh_format_err),
			 accum_ofdm->mh_format_err);
	pos += scnprintf(buf + pos, bufsz - pos,
			 "re_acq_main_rssi_sum:\t%u\t\t\t%u\n",
			 le32_to_cpu(ofdm->re_acq_main_rssi_sum),
			 accum_ofdm->re_acq_main_rssi_sum);

	pos += scnprintf(buf + pos, bufsz - pos, "Statistics_Rx - CCK:\n");
	pos += scnprintf(buf + pos, bufsz - pos,
			"\t\t\tcurrent\t\t\taccumulative\n");
	pos += scnprintf(buf + pos, bufsz - pos, "ina_cnt:\t\t%u\t\t\t%u\n",
			 le32_to_cpu(cck->ina_cnt), accum_cck->ina_cnt);
	pos += scnprintf(buf + pos, bufsz - pos, "fina_cnt:\t\t%u\t\t\t%u\n",
			 le32_to_cpu(cck->fina_cnt), accum_cck->fina_cnt);
	pos += scnprintf(buf + pos, bufsz - pos, "plcp_err:\t\t%u\t\t\t%u\n",
			 le32_to_cpu(cck->plcp_err), accum_cck->plcp_err);
	pos += scnprintf(buf + pos, bufsz - pos, "crc32_err:\t\t%u\t\t\t%u\n",
			 le32_to_cpu(cck->crc32_err), accum_cck->crc32_err);
	pos += scnprintf(buf + pos, bufsz - pos,
			 "overrun_err:\t\t%u\t\t\t%u\n",
			 le32_to_cpu(cck->overrun_err),
			 accum_cck->overrun_err);
	pos += scnprintf(buf + pos, bufsz - pos,
			 "early_overrun_err:\t%u\t\t\t%u\n",
			 le32_to_cpu(cck->early_overrun_err),
			 accum_cck->early_overrun_err);
	pos += scnprintf(buf + pos, bufsz - pos, "crc32_good:\t\t%u\t\t\t%u\n",
			 le32_to_cpu(cck->crc32_good), accum_cck->crc32_good);
	pos += scnprintf(buf + pos, bufsz - pos,
			 "false_alarm_cnt:\t%u\t\t\t%u\n",
			 le32_to_cpu(cck->false_alarm_cnt),
			 accum_cck->false_alarm_cnt);
	pos += scnprintf(buf + pos, bufsz - pos,
			 "fina_sync_err_cnt:\t%u\t\t\t%u\n",
			 le32_to_cpu(cck->fina_sync_err_cnt),
			 accum_cck->fina_sync_err_cnt);
	pos += scnprintf(buf + pos, bufsz - pos,
			 "sfd_timeout:\t\t%u\t\t\t%u\n",
			 le32_to_cpu(cck->sfd_timeout),
			 accum_cck->sfd_timeout);
	pos += scnprintf(buf + pos, bufsz - pos,
			 "fina_timeout:\t\t%u\t\t\t%u\n",
			 le32_to_cpu(cck->fina_timeout),
			 accum_cck->fina_timeout);
	pos += scnprintf(buf + pos, bufsz - pos,
			 "unresponded_rts:\t%u\t\t\t%u\n",
			 le32_to_cpu(cck->unresponded_rts),
			 accum_cck->unresponded_rts);
	pos += scnprintf(buf + pos, bufsz - pos,
			"rxe_frame_lmt_ovrun:\t%u\t\t\t%u\n",
			 le32_to_cpu(cck->rxe_frame_limit_overrun),
			 accum_cck->rxe_frame_limit_overrun);
	pos += scnprintf(buf + pos, bufsz - pos,
			 "sent_ack_cnt:\t\t%u\t\t\t%u\n",
			 le32_to_cpu(cck->sent_ack_cnt),
			 accum_cck->sent_ack_cnt);
	pos += scnprintf(buf + pos, bufsz - pos,
			 "sent_cts_cnt:\t\t%u\t\t\t%u\n",
			 le32_to_cpu(cck->sent_cts_cnt),
			 accum_cck->sent_cts_cnt);
	pos += scnprintf(buf + pos, bufsz - pos,
			 "sent_ba_rsp_cnt:\t%u\t\t\t%u\n",
			 le32_to_cpu(cck->sent_ba_rsp_cnt),
			 accum_cck->sent_ba_rsp_cnt);
	pos += scnprintf(buf + pos, bufsz - pos,
			 "dsp_self_kill:\t\t%u\t\t\t%u\n",
			 le32_to_cpu(cck->dsp_self_kill),
			 accum_cck->dsp_self_kill);
	pos += scnprintf(buf + pos, bufsz - pos,
			 "mh_format_err:\t\t%u\t\t\t%u\n",
			 le32_to_cpu(cck->mh_format_err),
			 accum_cck->mh_format_err);
	pos += scnprintf(buf + pos, bufsz - pos,
			 "re_acq_main_rssi_sum:\t%u\t\t\t%u\n",
			 le32_to_cpu(cck->re_acq_main_rssi_sum),
			 accum_cck->re_acq_main_rssi_sum);

	pos += scnprintf(buf + pos, bufsz - pos, "Statistics_Rx - GENERAL:\n");
	pos += scnprintf(buf + pos, bufsz - pos,
			"\t\t\tcurrent\t\t\taccumulative\n");
	pos += scnprintf(buf + pos, bufsz - pos, "bogus_cts:\t\t%u\t\t\t%u\n",
			 le32_to_cpu(general->bogus_cts),
			 accum_general->bogus_cts);
	pos += scnprintf(buf + pos, bufsz - pos, "bogus_ack:\t\t%u\t\t\t%u\n",
			 le32_to_cpu(general->bogus_ack),
			 accum_general->bogus_ack);
	pos += scnprintf(buf + pos, bufsz - pos,
			 "non_bssid_frames:\t%u\t\t\t%u\n",
			 le32_to_cpu(general->non_bssid_frames),
			 accum_general->non_bssid_frames);
	pos += scnprintf(buf + pos, bufsz - pos,
			 "filtered_frames:\t%u\t\t\t%u\n",
			 le32_to_cpu(general->filtered_frames),
			 accum_general->filtered_frames);
	pos += scnprintf(buf + pos, bufsz - pos,
			 "non_channel_beacons:\t%u\t\t\t%u\n",
			 le32_to_cpu(general->non_channel_beacons),
			 accum_general->non_channel_beacons);
	pos += scnprintf(buf + pos, bufsz - pos,
			 "channel_beacons:\t%u\t\t\t%u\n",
			 le32_to_cpu(general->channel_beacons),
			 accum_general->channel_beacons);
	pos += scnprintf(buf + pos, bufsz - pos,
			 "num_missed_bcon:\t%u\t\t\t%u\n",
			 le32_to_cpu(general->num_missed_bcon),
			 accum_general->num_missed_bcon);
	pos += scnprintf(buf + pos, bufsz - pos,
			"adc_rx_saturation_time:\t%u\t\t\t%u\n",
			 le32_to_cpu(general->adc_rx_saturation_time),
			 accum_general->adc_rx_saturation_time);
	pos += scnprintf(buf + pos, bufsz - pos,
			"ina_detect_search_tm:\t%u\t\t\t%u\n",
			 le32_to_cpu(general->ina_detection_search_time),
			 accum_general->ina_detection_search_time);
	pos += scnprintf(buf + pos, bufsz - pos,
			 "beacon_silence_rssi_a:\t%u\t\t\t%u\n",
			 le32_to_cpu(general->beacon_silence_rssi_a),
			 accum_general->beacon_silence_rssi_a);
	pos += scnprintf(buf + pos, bufsz - pos,
			 "beacon_silence_rssi_b:\t%u\t\t\t%u\n",
			 le32_to_cpu(general->beacon_silence_rssi_b),
			 accum_general->beacon_silence_rssi_b);
	pos += scnprintf(buf + pos, bufsz - pos,
			 "beacon_silence_rssi_c:\t%u\t\t\t%u\n",
			 le32_to_cpu(general->beacon_silence_rssi_c),
			 accum_general->beacon_silence_rssi_c);
	pos += scnprintf(buf + pos, bufsz - pos,
			"interference_data_flag:\t%u\t\t\t%u\n",
			 le32_to_cpu(general->interference_data_flag),
			 accum_general->interference_data_flag);
	pos += scnprintf(buf + pos, bufsz - pos,
			 "channel_load:\t\t%u\t\t\t%u\n",
			 le32_to_cpu(general->channel_load),
			 accum_general->channel_load);
	pos += scnprintf(buf + pos, bufsz - pos,
			 "dsp_false_alarms:\t%u\t\t\t%u\n",
			 le32_to_cpu(general->dsp_false_alarms),
			 accum_general->dsp_false_alarms);
	pos += scnprintf(buf + pos, bufsz - pos,
			 "beacon_rssi_a:\t\t%u\t\t\t%u\n",
			 le32_to_cpu(general->beacon_rssi_a),
			 accum_general->beacon_rssi_a);
	pos += scnprintf(buf + pos, bufsz - pos,
			 "beacon_rssi_b:\t\t%u\t\t\t%u\n",
			 le32_to_cpu(general->beacon_rssi_b),
			 accum_general->beacon_rssi_b);
	pos += scnprintf(buf + pos, bufsz - pos,
			 "beacon_rssi_c:\t\t%u\t\t\t%u\n",
			 le32_to_cpu(general->beacon_rssi_c),
			 accum_general->beacon_rssi_c);
	pos += scnprintf(buf + pos, bufsz - pos,
			 "beacon_energy_a:\t%u\t\t\t%u\n",
			 le32_to_cpu(general->beacon_energy_a),
			 accum_general->beacon_energy_a);
	pos += scnprintf(buf + pos, bufsz - pos,
			 "beacon_energy_b:\t%u\t\t\t%u\n",
			 le32_to_cpu(general->beacon_energy_b),
			 accum_general->beacon_energy_b);
	pos += scnprintf(buf + pos, bufsz - pos,
			 "beacon_energy_c:\t%u\t\t\t%u\n",
			 le32_to_cpu(general->beacon_energy_c),
			 accum_general->beacon_energy_c);

	pos += scnprintf(buf + pos, bufsz - pos, "Statistics_Rx - OFDM_HT:\n");
	pos += scnprintf(buf + pos, bufsz - pos,
			"\t\t\tcurrent\t\t\taccumulative\n");
	pos += scnprintf(buf + pos, bufsz - pos, "plcp_err:\t\t%u\t\t\t%u\n",
			 le32_to_cpu(ht->plcp_err), accum_ht->plcp_err);
	pos += scnprintf(buf + pos, bufsz - pos,
			 "overrun_err:\t\t%u\t\t\t%u\n",
			 le32_to_cpu(ht->overrun_err), accum_ht->overrun_err);
	pos += scnprintf(buf + pos, bufsz - pos,
			 "early_overrun_err:\t%u\t\t\t%u\n",
			 le32_to_cpu(ht->early_overrun_err),
			 accum_ht->early_overrun_err);
	pos += scnprintf(buf + pos, bufsz - pos, "crc32_good:\t\t%u\t\t\t%u\n",
			 le32_to_cpu(ht->crc32_good), accum_ht->crc32_good);
	pos += scnprintf(buf + pos, bufsz - pos, "crc32_err:\t\t%u\t\t\t%u\n",
			 le32_to_cpu(ht->crc32_err), accum_ht->crc32_err);
	pos += scnprintf(buf + pos, bufsz - pos,
			 "mh_format_err:\t\t%u\t\t\t%u\n",
			 le32_to_cpu(ht->mh_format_err),
			 accum_ht->mh_format_err);
	pos += scnprintf(buf + pos, bufsz - pos,
			 "agg_crc32_good:\t\t%u\t\t\t%u\n",
			 le32_to_cpu(ht->agg_crc32_good),
			 accum_ht->agg_crc32_good);
	pos += scnprintf(buf + pos, bufsz - pos,
			 "agg_mpdu_cnt:\t\t%u\t\t\t%u\n",
			 le32_to_cpu(ht->agg_mpdu_cnt),
			 accum_ht->agg_mpdu_cnt);
	pos += scnprintf(buf + pos, bufsz - pos, "agg_cnt:\t\t%u\t\t\t%u\n",
			 le32_to_cpu(ht->agg_cnt), accum_ht->agg_cnt);

	ret = simple_read_from_buffer(user_buf, count, ppos, buf, pos);
	kfree(buf);
	return ret;
}

static ssize_t iwl_dbgfs_ucode_tx_stats_read(struct file *file,
					char __user *user_buf,
					size_t count, loff_t *ppos)
{
	struct iwl_priv *priv = (struct iwl_priv *)file->private_data;
	int pos = 0;
	char *buf;
	int bufsz = (sizeof(struct statistics_tx) * 24) + 250;
	ssize_t ret;
	struct statistics_tx *tx, *accum_tx;

	if (!iwl_is_alive(priv))
		return -EAGAIN;

	/* make request to uCode to retrieve statistics information */
	mutex_lock(&priv->mutex);
	ret = iwl_send_statistics_request(priv, CMD_SYNC, false);
	mutex_unlock(&priv->mutex);

	if (ret) {
		IWL_ERR(priv,
			"Error sending statistics request: %zd\n", ret);
		return -EAGAIN;
	}
	buf = kzalloc(bufsz, GFP_KERNEL);
	if (!buf) {
		IWL_ERR(priv, "Can not allocate Buffer\n");
		return -ENOMEM;
	}

	/* the statistic information display here is based on
	 * the last statistics notification from uCode
	 * might not reflect the current uCode activity
	 */
	tx = &priv->statistics.tx;
	accum_tx = &priv->accum_statistics.tx;
	pos += iwl_dbgfs_statistics_flag(priv, buf, bufsz);
	pos += scnprintf(buf + pos, bufsz - pos, "Statistics_Tx:\n");
	pos += scnprintf(buf + pos, bufsz - pos,
			"\t\t\tcurrent\t\t\taccumulative\n");
	pos += scnprintf(buf + pos, bufsz - pos, "preamble:\t\t\t%u\t\t\t%u\n",
			 le32_to_cpu(tx->preamble_cnt),
			 accum_tx->preamble_cnt);
	pos += scnprintf(buf + pos, bufsz - pos,
			 "rx_detected_cnt:\t\t%u\t\t\t%u\n",
			 le32_to_cpu(tx->rx_detected_cnt),
			 accum_tx->rx_detected_cnt);
	pos += scnprintf(buf + pos, bufsz - pos,
			 "bt_prio_defer_cnt:\t\t%u\t\t\t%u\n",
			 le32_to_cpu(tx->bt_prio_defer_cnt),
			 accum_tx->bt_prio_defer_cnt);
	pos += scnprintf(buf + pos, bufsz - pos,
			 "bt_prio_kill_cnt:\t\t%u\t\t\t%u\n",
			 le32_to_cpu(tx->bt_prio_kill_cnt),
			 accum_tx->bt_prio_kill_cnt);
	pos += scnprintf(buf + pos, bufsz - pos,
			 "few_bytes_cnt:\t\t\t%u\t\t\t%u\n",
			 le32_to_cpu(tx->few_bytes_cnt),
			 accum_tx->few_bytes_cnt);
	pos += scnprintf(buf + pos, bufsz - pos,
			 "cts_timeout:\t\t\t%u\t\t\t%u\n",
			 le32_to_cpu(tx->cts_timeout), accum_tx->cts_timeout);
	pos += scnprintf(buf + pos, bufsz - pos,
			 "ack_timeout:\t\t\t%u\t\t\t%u\n",
			 le32_to_cpu(tx->ack_timeout),
			 accum_tx->ack_timeout);
	pos += scnprintf(buf + pos, bufsz - pos,
			 "expected_ack_cnt:\t\t%u\t\t\t%u\n",
			 le32_to_cpu(tx->expected_ack_cnt),
			 accum_tx->expected_ack_cnt);
	pos += scnprintf(buf + pos, bufsz - pos,
			 "actual_ack_cnt:\t\t\t%u\t\t\t%u\n",
			 le32_to_cpu(tx->actual_ack_cnt),
			 accum_tx->actual_ack_cnt);
	pos += scnprintf(buf + pos, bufsz - pos,
			 "dump_msdu_cnt:\t\t\t%u\t\t\t%u\n",
			 le32_to_cpu(tx->dump_msdu_cnt),
			 accum_tx->dump_msdu_cnt);
	pos += scnprintf(buf + pos, bufsz - pos,
			 "abort_nxt_frame_mismatch:"
			 "\t%u\t\t\t%u\n",
			 le32_to_cpu(tx->burst_abort_next_frame_mismatch_cnt),
			 accum_tx->burst_abort_next_frame_mismatch_cnt);
	pos += scnprintf(buf + pos, bufsz - pos,
			 "abort_missing_nxt_frame:"
			 "\t%u\t\t\t%u\n",
			 le32_to_cpu(tx->burst_abort_missing_next_frame_cnt),
			 accum_tx->burst_abort_missing_next_frame_cnt);
	pos += scnprintf(buf + pos, bufsz - pos,
			 "cts_timeout_collision:\t\t%u\t\t\t%u\n",
			 le32_to_cpu(tx->cts_timeout_collision),
			 accum_tx->cts_timeout_collision);
	pos += scnprintf(buf + pos, bufsz - pos,
			"ack_ba_timeout_collision:\t%u\t\t\t%u\n",
			 le32_to_cpu(tx->ack_or_ba_timeout_collision),
			 accum_tx->ack_or_ba_timeout_collision);
	pos += scnprintf(buf + pos, bufsz - pos,
			 "agg ba_timeout:\t\t\t%u\t\t\t%u\n",
			 le32_to_cpu(tx->agg.ba_timeout),
			 accum_tx->agg.ba_timeout);
	pos += scnprintf(buf + pos, bufsz - pos,
			"agg ba_resched_frames:\t\t%u\t\t\t%u\n",
			 le32_to_cpu(tx->agg.ba_reschedule_frames),
			 accum_tx->agg.ba_reschedule_frames);
	pos += scnprintf(buf + pos, bufsz - pos,
			"agg scd_query_agg_frame:\t%u\t\t\t%u\n",
			 le32_to_cpu(tx->agg.scd_query_agg_frame_cnt),
			 accum_tx->agg.scd_query_agg_frame_cnt);
	pos += scnprintf(buf + pos, bufsz - pos,
			 "agg scd_query_no_agg:\t\t%u\t\t\t%u\n",
			 le32_to_cpu(tx->agg.scd_query_no_agg),
			 accum_tx->agg.scd_query_no_agg);
	pos += scnprintf(buf + pos, bufsz - pos,
			 "agg scd_query_agg:\t\t%u\t\t\t%u\n",
			 le32_to_cpu(tx->agg.scd_query_agg),
			 accum_tx->agg.scd_query_agg);
	pos += scnprintf(buf + pos, bufsz - pos,
			"agg scd_query_mismatch:\t\t%u\t\t\t%u\n",
			 le32_to_cpu(tx->agg.scd_query_mismatch),
			 accum_tx->agg.scd_query_mismatch);
	pos += scnprintf(buf + pos, bufsz - pos,
			 "agg frame_not_ready:\t\t%u\t\t\t%u\n",
			 le32_to_cpu(tx->agg.frame_not_ready),
			 accum_tx->agg.frame_not_ready);
	pos += scnprintf(buf + pos, bufsz - pos,
			 "agg underrun:\t\t\t%u\t\t\t%u\n",
			 le32_to_cpu(tx->agg.underrun),
			 accum_tx->agg.underrun);
	pos += scnprintf(buf + pos, bufsz - pos,
			 "agg bt_prio_kill:\t\t%u\t\t\t%u\n",
			 le32_to_cpu(tx->agg.bt_prio_kill),
			 accum_tx->agg.bt_prio_kill);
	pos += scnprintf(buf + pos, bufsz - pos,
			 "agg rx_ba_rsp_cnt:\t\t%u\t\t\t%u\n",
			 le32_to_cpu(tx->agg.rx_ba_rsp_cnt),
			 accum_tx->agg.rx_ba_rsp_cnt);

	ret = simple_read_from_buffer(user_buf, count, ppos, buf, pos);
	kfree(buf);
	return ret;
}

static ssize_t iwl_dbgfs_ucode_general_stats_read(struct file *file,
					char __user *user_buf,
					size_t count, loff_t *ppos)
{
	struct iwl_priv *priv = (struct iwl_priv *)file->private_data;
	int pos = 0;
	char *buf;
	int bufsz = sizeof(struct statistics_general) * 4 + 250;
	ssize_t ret;
	struct statistics_general *general, *accum_general;
	struct statistics_dbg *dbg, *accum_dbg;
	struct statistics_div *div, *accum_div;

	if (!iwl_is_alive(priv))
		return -EAGAIN;

	/* make request to uCode to retrieve statistics information */
	mutex_lock(&priv->mutex);
	ret = iwl_send_statistics_request(priv, CMD_SYNC, false);
	mutex_unlock(&priv->mutex);

	if (ret) {
		IWL_ERR(priv,
			"Error sending statistics request: %zd\n", ret);
		return -EAGAIN;
	}
	buf = kzalloc(bufsz, GFP_KERNEL);
	if (!buf) {
		IWL_ERR(priv, "Can not allocate Buffer\n");
		return -ENOMEM;
	}

	/* the statistic information display here is based on
	 * the last statistics notification from uCode
	 * might not reflect the current uCode activity
	 */
	general = &priv->statistics.general;
	dbg = &priv->statistics.general.dbg;
	div = &priv->statistics.general.div;
	accum_general = &priv->accum_statistics.general;
	accum_dbg = &priv->accum_statistics.general.dbg;
	accum_div = &priv->accum_statistics.general.div;
	pos += iwl_dbgfs_statistics_flag(priv, buf, bufsz);
	pos += scnprintf(buf + pos, bufsz - pos, "Statistics_General:\n");
	pos += scnprintf(buf + pos, bufsz - pos,
			"\t\t\tcurrent\t\t\taccumulative\n");
	pos += scnprintf(buf + pos, bufsz - pos, "temperature:\t\t\t%u\n",
			 le32_to_cpu(general->temperature));
	pos += scnprintf(buf + pos, bufsz - pos, "temperature_m:\t\t\t%u\n",
			 le32_to_cpu(general->temperature_m));
	pos += scnprintf(buf + pos, bufsz - pos,
			 "burst_check:\t\t\t%u\t\t\t%u\n",
			 le32_to_cpu(dbg->burst_check),
			 accum_dbg->burst_check);
	pos += scnprintf(buf + pos, bufsz - pos,
			 "burst_count:\t\t\t%u\t\t\t%u\n",
			 le32_to_cpu(dbg->burst_count),
			 accum_dbg->burst_count);
	pos += scnprintf(buf + pos, bufsz - pos,
			 "sleep_time:\t\t\t%u\t\t\t%u\n",
			 le32_to_cpu(general->sleep_time),
			 accum_general->sleep_time);
	pos += scnprintf(buf + pos, bufsz - pos,
			 "slots_out:\t\t\t%u\t\t\t%u\n",
			 le32_to_cpu(general->slots_out),
			 accum_general->slots_out);
	pos += scnprintf(buf + pos, bufsz - pos,
			 "slots_idle:\t\t\t%u\t\t\t%u\n",
			 le32_to_cpu(general->slots_idle),
			 accum_general->slots_idle);
	pos += scnprintf(buf + pos, bufsz - pos, "ttl_timestamp:\t\t\t%u\n",
			 le32_to_cpu(general->ttl_timestamp));
	pos += scnprintf(buf + pos, bufsz - pos, "tx_on_a:\t\t\t%u\t\t\t%u\n",
			 le32_to_cpu(div->tx_on_a), accum_div->tx_on_a);
	pos += scnprintf(buf + pos, bufsz - pos, "tx_on_b:\t\t\t%u\t\t\t%u\n",
			 le32_to_cpu(div->tx_on_b), accum_div->tx_on_b);
	pos += scnprintf(buf + pos, bufsz - pos,
			 "exec_time:\t\t\t%u\t\t\t%u\n",
			 le32_to_cpu(div->exec_time), accum_div->exec_time);
	pos += scnprintf(buf + pos, bufsz - pos,
			 "probe_time:\t\t\t%u\t\t\t%u\n",
			 le32_to_cpu(div->probe_time), accum_div->probe_time);
	pos += scnprintf(buf + pos, bufsz - pos,
			 "rx_enable_counter:\t\t%u\t\t\t%u\n",
			 le32_to_cpu(general->rx_enable_counter),
			 accum_general->rx_enable_counter);
	ret = simple_read_from_buffer(user_buf, count, ppos, buf, pos);
	kfree(buf);
	return ret;
}

static ssize_t iwl_dbgfs_sensitivity_read(struct file *file,
					char __user *user_buf,
					size_t count, loff_t *ppos) {

	struct iwl_priv *priv = (struct iwl_priv *)file->private_data;
	int pos = 0;
	int cnt = 0;
	char *buf;
	int bufsz = sizeof(struct iwl_sensitivity_data) * 4 + 100;
	ssize_t ret;
	struct iwl_sensitivity_data *data;

	data = &priv->sensitivity_data;
	buf = kzalloc(bufsz, GFP_KERNEL);
	if (!buf) {
		IWL_ERR(priv, "Can not allocate Buffer\n");
		return -ENOMEM;
	}

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

	struct iwl_priv *priv = (struct iwl_priv *)file->private_data;
	int pos = 0;
	int cnt = 0;
	char *buf;
	int bufsz = sizeof(struct iwl_chain_noise_data) * 4 + 100;
	ssize_t ret;
	struct iwl_chain_noise_data *data;

	data = &priv->chain_noise_data;
	buf = kzalloc(bufsz, GFP_KERNEL);
	if (!buf) {
		IWL_ERR(priv, "Can not allocate Buffer\n");
		return -ENOMEM;
	}

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

static ssize_t iwl_dbgfs_tx_power_read(struct file *file,
					char __user *user_buf,
					size_t count, loff_t *ppos) {

	struct iwl_priv *priv = (struct iwl_priv *)file->private_data;
	char buf[128];
	int pos = 0;
	ssize_t ret;
	const size_t bufsz = sizeof(buf);
	struct statistics_tx *tx;

	if (!iwl_is_alive(priv))
		pos += scnprintf(buf + pos, bufsz - pos, "N/A\n");
	else {
		/* make request to uCode to retrieve statistics information */
		mutex_lock(&priv->mutex);
		ret = iwl_send_statistics_request(priv, CMD_SYNC, false);
		mutex_unlock(&priv->mutex);

		if (ret) {
			IWL_ERR(priv, "Error sending statistics request: %zd\n",
				ret);
			return -EAGAIN;
		}
		tx = &priv->statistics.tx;
		if (tx->tx_power.ant_a ||
		    tx->tx_power.ant_b ||
		    tx->tx_power.ant_c) {
			pos += scnprintf(buf + pos, bufsz - pos,
				"tx power: (1/2 dB step)\n");
			if ((priv->cfg->valid_tx_ant & ANT_A) &&
			    tx->tx_power.ant_a)
				pos += scnprintf(buf + pos, bufsz - pos,
						"\tantenna A: 0x%X\n",
						tx->tx_power.ant_a);
			if ((priv->cfg->valid_tx_ant & ANT_B) &&
			    tx->tx_power.ant_b)
				pos += scnprintf(buf + pos, bufsz - pos,
						"\tantenna B: 0x%X\n",
						tx->tx_power.ant_b);
			if ((priv->cfg->valid_tx_ant & ANT_C) &&
			    tx->tx_power.ant_c)
				pos += scnprintf(buf + pos, bufsz - pos,
						"\tantenna C: 0x%X\n",
						tx->tx_power.ant_c);
		} else
			pos += scnprintf(buf + pos, bufsz - pos, "N/A\n");
	}
	return simple_read_from_buffer(user_buf, count, ppos, buf, pos);
}

static ssize_t iwl_dbgfs_power_save_status_read(struct file *file,
						    char __user *user_buf,
						    size_t count, loff_t *ppos)
{
	struct iwl_priv *priv = (struct iwl_priv *)file->private_data;
	char buf[60];
	int pos = 0;
	const size_t bufsz = sizeof(buf);
	u32 pwrsave_status;

	pwrsave_status = iwl_read32(priv, CSR_GP_CNTRL) &
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

DEBUGFS_READ_FILE_OPS(rx_statistics);
DEBUGFS_READ_FILE_OPS(tx_statistics);
DEBUGFS_READ_WRITE_FILE_OPS(traffic_log);
DEBUGFS_READ_FILE_OPS(rx_queue);
DEBUGFS_READ_FILE_OPS(tx_queue);
DEBUGFS_READ_FILE_OPS(ucode_rx_stats);
DEBUGFS_READ_FILE_OPS(ucode_tx_stats);
DEBUGFS_READ_FILE_OPS(ucode_general_stats);
DEBUGFS_READ_FILE_OPS(sensitivity);
DEBUGFS_READ_FILE_OPS(chain_noise);
DEBUGFS_READ_FILE_OPS(tx_power);
DEBUGFS_READ_FILE_OPS(power_save_status);
DEBUGFS_WRITE_FILE_OPS(clear_ucode_statistics);
DEBUGFS_WRITE_FILE_OPS(clear_traffic_statistics);

/*
 * Create the debugfs files and directories
 *
 */
int iwl_dbgfs_register(struct iwl_priv *priv, const char *name)
{
	struct iwl_debugfs *dbgfs;
	struct dentry *phyd = priv->hw->wiphy->debugfsdir;
	int ret = 0;

	dbgfs = kzalloc(sizeof(struct iwl_debugfs), GFP_KERNEL);
	if (!dbgfs) {
		ret = -ENOMEM;
		goto err;
	}

	priv->dbgfs = dbgfs;
	dbgfs->name = name;
	dbgfs->dir_drv = debugfs_create_dir(name, phyd);
	if (!dbgfs->dir_drv || IS_ERR(dbgfs->dir_drv)) {
		ret = -ENOENT;
		goto err;
	}

	DEBUGFS_ADD_DIR(data, dbgfs->dir_drv);
	DEBUGFS_ADD_DIR(rf, dbgfs->dir_drv);
	DEBUGFS_ADD_DIR(debug, dbgfs->dir_drv);
	DEBUGFS_ADD_FILE(nvm, data, S_IRUSR);
	DEBUGFS_ADD_FILE(sram, data, S_IWUSR | S_IRUSR);
	DEBUGFS_ADD_FILE(log_event, data, S_IWUSR);
	DEBUGFS_ADD_FILE(stations, data, S_IRUSR);
	DEBUGFS_ADD_FILE(channels, data, S_IRUSR);
	DEBUGFS_ADD_FILE(status, data, S_IRUSR);
	DEBUGFS_ADD_FILE(interrupt, data, S_IWUSR | S_IRUSR);
	DEBUGFS_ADD_FILE(qos, data, S_IRUSR);
	DEBUGFS_ADD_FILE(led, data, S_IRUSR);
	DEBUGFS_ADD_FILE(sleep_level_override, data, S_IWUSR | S_IRUSR);
	DEBUGFS_ADD_FILE(current_sleep_command, data, S_IRUSR);
	DEBUGFS_ADD_FILE(thermal_throttling, data, S_IRUSR);
	DEBUGFS_ADD_FILE(disable_ht40, data, S_IWUSR | S_IRUSR);
	DEBUGFS_ADD_FILE(rx_statistics, debug, S_IRUSR);
	DEBUGFS_ADD_FILE(tx_statistics, debug, S_IRUSR);
	DEBUGFS_ADD_FILE(traffic_log, debug, S_IWUSR | S_IRUSR);
	DEBUGFS_ADD_FILE(rx_queue, debug, S_IRUSR);
	DEBUGFS_ADD_FILE(tx_queue, debug, S_IRUSR);
	DEBUGFS_ADD_FILE(tx_power, debug, S_IRUSR);
	DEBUGFS_ADD_FILE(power_save_status, debug, S_IRUSR);
	DEBUGFS_ADD_FILE(clear_ucode_statistics, debug, S_IWUSR);
	DEBUGFS_ADD_FILE(clear_traffic_statistics, debug, S_IWUSR);
	if ((priv->hw_rev & CSR_HW_REV_TYPE_MSK) != CSR_HW_REV_TYPE_3945) {
		DEBUGFS_ADD_FILE(ucode_rx_stats, debug, S_IRUSR);
		DEBUGFS_ADD_FILE(ucode_tx_stats, debug, S_IRUSR);
		DEBUGFS_ADD_FILE(ucode_general_stats, debug, S_IRUSR);
		DEBUGFS_ADD_FILE(sensitivity, debug, S_IRUSR);
		DEBUGFS_ADD_FILE(chain_noise, debug, S_IRUSR);
	}
	DEBUGFS_ADD_BOOL(disable_sensitivity, rf, &priv->disable_sens_cal);
	DEBUGFS_ADD_BOOL(disable_chain_noise, rf,
			 &priv->disable_chain_noise_cal);
	if (((priv->hw_rev & CSR_HW_REV_TYPE_MSK) == CSR_HW_REV_TYPE_4965) ||
	    ((priv->hw_rev & CSR_HW_REV_TYPE_MSK) == CSR_HW_REV_TYPE_3945))
		DEBUGFS_ADD_BOOL(disable_tx_power, rf,
				&priv->disable_tx_power_cal);
	return 0;

err:
	IWL_ERR(priv, "Can't open the debugfs directory\n");
	iwl_dbgfs_unregister(priv);
	return ret;
}
EXPORT_SYMBOL(iwl_dbgfs_register);

/**
 * Remove the debugfs files and directories
 *
 */
void iwl_dbgfs_unregister(struct iwl_priv *priv)
{
	if (!priv->dbgfs)
		return;

	DEBUGFS_REMOVE(priv->dbgfs->dbgfs_data_files.file_sleep_level_override);
	DEBUGFS_REMOVE(priv->dbgfs->dbgfs_data_files.file_current_sleep_command);
	DEBUGFS_REMOVE(priv->dbgfs->dbgfs_data_files.file_nvm);
	DEBUGFS_REMOVE(priv->dbgfs->dbgfs_data_files.file_sram);
	DEBUGFS_REMOVE(priv->dbgfs->dbgfs_data_files.file_log_event);
	DEBUGFS_REMOVE(priv->dbgfs->dbgfs_data_files.file_stations);
	DEBUGFS_REMOVE(priv->dbgfs->dbgfs_data_files.file_channels);
	DEBUGFS_REMOVE(priv->dbgfs->dbgfs_data_files.file_status);
	DEBUGFS_REMOVE(priv->dbgfs->dbgfs_data_files.file_interrupt);
	DEBUGFS_REMOVE(priv->dbgfs->dbgfs_data_files.file_qos);
	DEBUGFS_REMOVE(priv->dbgfs->dbgfs_data_files.file_led);
	DEBUGFS_REMOVE(priv->dbgfs->dbgfs_data_files.file_thermal_throttling);
	DEBUGFS_REMOVE(priv->dbgfs->dbgfs_data_files.file_disable_ht40);
	DEBUGFS_REMOVE(priv->dbgfs->dir_data);
	DEBUGFS_REMOVE(priv->dbgfs->dbgfs_debug_files.file_rx_statistics);
	DEBUGFS_REMOVE(priv->dbgfs->dbgfs_debug_files.file_tx_statistics);
	DEBUGFS_REMOVE(priv->dbgfs->dbgfs_debug_files.file_traffic_log);
	DEBUGFS_REMOVE(priv->dbgfs->dbgfs_debug_files.file_rx_queue);
	DEBUGFS_REMOVE(priv->dbgfs->dbgfs_debug_files.file_tx_queue);
	DEBUGFS_REMOVE(priv->dbgfs->dbgfs_debug_files.file_tx_power);
	DEBUGFS_REMOVE(priv->dbgfs->dbgfs_debug_files.file_power_save_status);
	DEBUGFS_REMOVE(priv->dbgfs->dbgfs_debug_files.
			file_clear_ucode_statistics);
	DEBUGFS_REMOVE(priv->dbgfs->dbgfs_debug_files.
			file_clear_traffic_statistics);
	if ((priv->hw_rev & CSR_HW_REV_TYPE_MSK) != CSR_HW_REV_TYPE_3945) {
		DEBUGFS_REMOVE(priv->dbgfs->dbgfs_debug_files.
			file_ucode_rx_stats);
		DEBUGFS_REMOVE(priv->dbgfs->dbgfs_debug_files.
			file_ucode_tx_stats);
		DEBUGFS_REMOVE(priv->dbgfs->dbgfs_debug_files.
			file_ucode_general_stats);
		DEBUGFS_REMOVE(priv->dbgfs->dbgfs_debug_files.
			file_sensitivity);
		DEBUGFS_REMOVE(priv->dbgfs->dbgfs_debug_files.
			file_chain_noise);
	}
	DEBUGFS_REMOVE(priv->dbgfs->dir_debug);
	DEBUGFS_REMOVE(priv->dbgfs->dbgfs_rf_files.file_disable_sensitivity);
	DEBUGFS_REMOVE(priv->dbgfs->dbgfs_rf_files.file_disable_chain_noise);
	if (((priv->hw_rev & CSR_HW_REV_TYPE_MSK) == CSR_HW_REV_TYPE_4965) ||
	    ((priv->hw_rev & CSR_HW_REV_TYPE_MSK) == CSR_HW_REV_TYPE_3945))
		DEBUGFS_REMOVE(priv->dbgfs->dbgfs_rf_files.file_disable_tx_power);
	DEBUGFS_REMOVE(priv->dbgfs->dir_rf);
	DEBUGFS_REMOVE(priv->dbgfs->dir_drv);
	kfree(priv->dbgfs);
	priv->dbgfs = NULL;
}
EXPORT_SYMBOL(iwl_dbgfs_unregister);



