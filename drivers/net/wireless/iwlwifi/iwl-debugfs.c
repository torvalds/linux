/******************************************************************************
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2008 Intel Corporation. All rights reserved.
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
 * Tomas Winkler <tomas.winkler@intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *****************************************************************************/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/debugfs.h>

#include <linux/ieee80211.h>
#include <net/mac80211.h>


#include "iwl-4965.h"
#include "iwl-debug.h"
#include "iwl-core.h"
#include "iwl-io.h"


/* create and remove of files */
#define DEBUGFS_ADD_DIR(name, parent) do {                              \
	dbgfs->dir_##name = debugfs_create_dir(#name, parent);          \
	if (!(dbgfs->dir_##name))                                       \
		goto err; 						\
} while (0)

#define DEBUGFS_ADD_FILE(name, parent) do {                             \
	dbgfs->dbgfs_##parent##_files.file_##name =                     \
	debugfs_create_file(#name, 0644, dbgfs->dir_##parent, priv,     \
				&iwl_dbgfs_##name##_ops);               \
	if (!(dbgfs->dbgfs_##parent##_files.file_##name))               \
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
	char buf[256];
	int pos = 0;
	const size_t bufsz = sizeof(buf);

	pos += scnprintf(buf + pos, bufsz - pos, "mgmt: %u\n",
						priv->tx_stats[0].cnt);
	pos += scnprintf(buf + pos, bufsz - pos, "ctrl: %u\n",
						priv->tx_stats[1].cnt);
	pos += scnprintf(buf + pos, bufsz - pos, "data: %u\n",
						priv->tx_stats[2].cnt);

	return simple_read_from_buffer(user_buf, count, ppos, buf, pos);
}

static ssize_t iwl_dbgfs_rx_statistics_read(struct file *file,
						char __user *user_buf,
						size_t count, loff_t *ppos) {

	struct iwl_priv *priv = (struct iwl_priv *)file->private_data;
	char buf[256];
	int pos = 0;
	const size_t bufsz = sizeof(buf);

	pos += scnprintf(buf + pos, bufsz - pos, "mgmt: %u\n",
						priv->rx_stats[0].cnt);
	pos += scnprintf(buf + pos, bufsz - pos, "ctrl: %u\n",
						priv->rx_stats[1].cnt);
	pos += scnprintf(buf + pos, bufsz - pos, "data: %u\n",
						priv->rx_stats[2].cnt);

	return simple_read_from_buffer(user_buf, count, ppos, buf, pos);
}

#define BYTE1_MASK 0x000000ff;
#define BYTE2_MASK 0x0000ffff;
#define BYTE3_MASK 0x00ffffff;
static ssize_t iwl_dbgfs_sram_read(struct file *file,
					char __user *user_buf,
					size_t count, loff_t *ppos)
{
	u32 val;
	char buf[1024];
	ssize_t ret;
	int i;
	int pos = 0;
	struct iwl_priv *priv = (struct iwl_priv *)file->private_data;
	const size_t bufsz = sizeof(buf);

	printk(KERN_DEBUG "offset is: 0x%x\tlen is: 0x%x\n",
	priv->dbgfs->sram_offset, priv->dbgfs->sram_len);

	iwl_grab_nic_access(priv);
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
		pos += scnprintf(buf + pos, bufsz - pos, "0x%08x ", val);
	}
	pos += scnprintf(buf + pos, bufsz - pos, "\n");
	iwl_release_nic_access(priv);

	ret = simple_read_from_buffer(user_buf, count, ppos, buf, pos);
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
	struct iwl4965_station_entry *station;
	int max_sta = priv->hw_params.max_stations;
	char *buf;
	int i, j, pos = 0;
	ssize_t ret;
	/* Add 30 for initial string */
	const size_t bufsz = 30 + sizeof(char) * 500 * (priv->num_stations);
	DECLARE_MAC_BUF(mac);

	buf = kmalloc(bufsz, GFP_KERNEL);
	if(!buf)
		return -ENOMEM;

	pos += scnprintf(buf + pos, bufsz - pos, "num of stations: %d\n\n",
			priv->num_stations);

	for (i = 0; i < max_sta; i++) {
		station = &priv->stations[i];
		if (station->used) {
			pos += scnprintf(buf + pos, bufsz - pos,
					"station %d:\ngeneral data:\n", i+1);
			print_mac(mac, station->sta.sta.addr);
			pos += scnprintf(buf + pos, bufsz - pos, "id: %u\n",
					station->sta.sta.sta_id);
			pos += scnprintf(buf + pos, bufsz - pos, "mode: %u\n",
					station->sta.mode);
			pos += scnprintf(buf + pos, bufsz - pos,
					"flags: 0x%x\n",
					station->sta.station_flags_msk);
			pos += scnprintf(buf + pos, bufsz - pos,
					"ps_status: %u\n", station->ps_status);
			pos += scnprintf(buf + pos, bufsz - pos, "tid data:\n");
			pos += scnprintf(buf + pos, bufsz - pos,
					"seq_num\t\ttxq_id\t");
			pos += scnprintf(buf + pos, bufsz - pos,
					"frame_count\twait_for_ba\t");
			pos += scnprintf(buf + pos, bufsz - pos,
					"start_idx\tbitmap0\t");
			pos += scnprintf(buf + pos, bufsz - pos,
					"bitmap1\trate_n_flags\n");

			for (j = 0; j < MAX_TID_COUNT; j++) {
				pos += scnprintf(buf + pos, bufsz - pos,
						"[%d]:\t\t%u\t", j,
						station->tid[j].seq_number);
				pos += scnprintf(buf + pos, bufsz - pos,
						"%u\t\t%u\t\t%u\t\t",
						station->tid[j].agg.txq_id,
						station->tid[j].agg.frame_count,
						station->tid[j].agg.wait_for_ba);
				pos += scnprintf(buf + pos, bufsz - pos,
						"%u\t%llu\t%u\n",
						station->tid[j].agg.start_idx,
						(unsigned long long)station->tid[j].agg.bitmap,
						station->tid[j].agg.rate_n_flags);
			}
			pos += scnprintf(buf + pos, bufsz - pos, "\n");
		}
	}

	ret = simple_read_from_buffer(user_buf, count, ppos, buf, pos);
	kfree(buf);
	return ret;
}


DEBUGFS_READ_WRITE_FILE_OPS(sram);
DEBUGFS_READ_FILE_OPS(stations);
DEBUGFS_READ_FILE_OPS(rx_statistics);
DEBUGFS_READ_FILE_OPS(tx_statistics);

/*
 * Create the debugfs files and directories
 *
 */
int iwl_dbgfs_register(struct iwl_priv *priv, const char *name)
{
	struct iwl_debugfs *dbgfs;

	dbgfs = kzalloc(sizeof(struct iwl_debugfs), GFP_KERNEL);
	if (!dbgfs) {
		goto err;
	}

	priv->dbgfs = dbgfs;
	dbgfs->name = name;
	dbgfs->dir_drv = debugfs_create_dir(name, NULL);
	if (!dbgfs->dir_drv || IS_ERR(dbgfs->dir_drv)){
		goto err;
	}

	DEBUGFS_ADD_DIR(data, dbgfs->dir_drv);
	DEBUGFS_ADD_FILE(sram, data);
	DEBUGFS_ADD_FILE(stations, data);
	DEBUGFS_ADD_FILE(rx_statistics, data);
	DEBUGFS_ADD_FILE(tx_statistics, data);

	return 0;

err:
	IWL_ERROR("Can't open the debugfs directory\n");
	iwl_dbgfs_unregister(priv);
	return -ENOENT;
}
EXPORT_SYMBOL(iwl_dbgfs_register);

/**
 * Remove the debugfs files and directories
 *
 */
void iwl_dbgfs_unregister(struct iwl_priv *priv)
{
	if (!(priv->dbgfs))
		return;

	DEBUGFS_REMOVE(priv->dbgfs->dbgfs_data_files.file_rx_statistics);
	DEBUGFS_REMOVE(priv->dbgfs->dbgfs_data_files.file_tx_statistics);
	DEBUGFS_REMOVE(priv->dbgfs->dbgfs_data_files.file_sram);
	DEBUGFS_REMOVE(priv->dbgfs->dbgfs_data_files.file_stations);
	DEBUGFS_REMOVE(priv->dbgfs->dir_data);
	DEBUGFS_REMOVE(priv->dbgfs->dir_drv);
	kfree(priv->dbgfs);
	priv->dbgfs = NULL;
}
EXPORT_SYMBOL(iwl_dbgfs_unregister);


