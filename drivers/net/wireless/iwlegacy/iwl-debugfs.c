/******************************************************************************
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2008 - 2011 Intel Corporation. All rights reserved.
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
#include <linux/ieee80211.h>
#include <net/mac80211.h>


#include "iwl-dev.h"
#include "iwl-debug.h"
#include "iwl-core.h"
#include "iwl-io.h"

/* create and remove of files */
#define DEBUGFS_ADD_FILE(name, parent, mode) do {			\
	if (!debugfs_create_file(#name, mode, parent, priv,		\
			 &iwl_legacy_dbgfs_##name##_ops))		\
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

/* file operation */
#define DEBUGFS_READ_FUNC(name)                                         \
static ssize_t iwl_legacy_dbgfs_##name##_read(struct file *file,               \
					char __user *user_buf,          \
					size_t count, loff_t *ppos);

#define DEBUGFS_WRITE_FUNC(name)                                        \
static ssize_t iwl_legacy_dbgfs_##name##_write(struct file *file,              \
					const char __user *user_buf,    \
					size_t count, loff_t *ppos);


static int
iwl_legacy_dbgfs_open_file_generic(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private;
	return 0;
}

#define DEBUGFS_READ_FILE_OPS(name) 				\
	DEBUGFS_READ_FUNC(name);                                        \
static const struct file_operations iwl_legacy_dbgfs_##name##_ops = {	\
	.read = iwl_legacy_dbgfs_##name##_read,				\
	.open = iwl_legacy_dbgfs_open_file_generic,                    	\
	.llseek = generic_file_llseek,					\
};

#define DEBUGFS_WRITE_FILE_OPS(name) 				\
	DEBUGFS_WRITE_FUNC(name);                                       \
static const struct file_operations iwl_legacy_dbgfs_##name##_ops = {	\
	.write = iwl_legacy_dbgfs_##name##_write,			\
	.open = iwl_legacy_dbgfs_open_file_generic,                    	\
	.llseek = generic_file_llseek,					\
};

#define DEBUGFS_READ_WRITE_FILE_OPS(name)                           \
	DEBUGFS_READ_FUNC(name);                                        \
	DEBUGFS_WRITE_FUNC(name);                                       \
static const struct file_operations iwl_legacy_dbgfs_##name##_ops = {	\
	.write = iwl_legacy_dbgfs_##name##_write,			\
	.read = iwl_legacy_dbgfs_##name##_read,				\
	.open = iwl_legacy_dbgfs_open_file_generic,			\
	.llseek = generic_file_llseek,					\
};

static ssize_t iwl_legacy_dbgfs_tx_statistics_read(struct file *file,
						char __user *user_buf,
						size_t count, loff_t *ppos) {

	struct iwl_priv *priv = file->private_data;
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
				 iwl_legacy_get_mgmt_string(cnt),
				 priv->tx_stats.mgmt[cnt]);
	}
	pos += scnprintf(buf + pos, bufsz - pos, "Control\n");
	for (cnt = 0; cnt < CONTROL_MAX; cnt++) {
		pos += scnprintf(buf + pos, bufsz - pos,
				 "\t%25s\t\t: %u\n",
				 iwl_legacy_get_ctrl_string(cnt),
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

static ssize_t
iwl_legacy_dbgfs_clear_traffic_statistics_write(struct file *file,
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
	iwl_legacy_clear_traffic_stats(priv);

	return count;
}

static ssize_t iwl_legacy_dbgfs_rx_statistics_read(struct file *file,
						char __user *user_buf,
						size_t count, loff_t *ppos) {

	struct iwl_priv *priv = file->private_data;
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
				 iwl_legacy_get_mgmt_string(cnt),
				 priv->rx_stats.mgmt[cnt]);
	}
	pos += scnprintf(buf + pos, bufsz - pos, "Control:\n");
	for (cnt = 0; cnt < CONTROL_MAX; cnt++) {
		pos += scnprintf(buf + pos, bufsz - pos,
				 "\t%25s\t\t: %u\n",
				 iwl_legacy_get_ctrl_string(cnt),
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
static ssize_t iwl_legacy_dbgfs_sram_read(struct file *file,
					char __user *user_buf,
					size_t count, loff_t *ppos)
{
	u32 val;
	char *buf;
	ssize_t ret;
	int i;
	int pos = 0;
	struct iwl_priv *priv = file->private_data;
	size_t bufsz;

	/* default is to dump the entire data segment */
	if (!priv->dbgfs_sram_offset && !priv->dbgfs_sram_len) {
		priv->dbgfs_sram_offset = 0x800000;
		if (priv->ucode_type == UCODE_INIT)
			priv->dbgfs_sram_len = priv->ucode_init_data.len;
		else
			priv->dbgfs_sram_len = priv->ucode_data.len;
	}
	bufsz =  30 + priv->dbgfs_sram_len * sizeof(char) * 10;
	buf = kmalloc(bufsz, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;
	pos += scnprintf(buf + pos, bufsz - pos, "sram_len: 0x%x\n",
			priv->dbgfs_sram_len);
	pos += scnprintf(buf + pos, bufsz - pos, "sram_offset: 0x%x\n",
			priv->dbgfs_sram_offset);
	for (i = priv->dbgfs_sram_len; i > 0; i -= 4) {
		val = iwl_legacy_read_targ_mem(priv, priv->dbgfs_sram_offset + \
					priv->dbgfs_sram_len - i);
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

static ssize_t iwl_legacy_dbgfs_sram_write(struct file *file,
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
	} else {
		priv->dbgfs_sram_offset = 0;
		priv->dbgfs_sram_len = 0;
	}

	return count;
}

static ssize_t
iwl_legacy_dbgfs_stations_read(struct file *file, char __user *user_buf,
					size_t count, loff_t *ppos)
{
	struct iwl_priv *priv = file->private_data;
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
		if (!station->used)
			continue;
		pos += scnprintf(buf + pos, bufsz - pos,
				 "station %d - addr: %pM, flags: %#x\n",
				 i, station->sta.sta.addr,
				 station->sta.station_flags_msk);
		pos += scnprintf(buf + pos, bufsz - pos,
				"TID\tseq_num\ttxq_id\tframes\ttfds\t");
		pos += scnprintf(buf + pos, bufsz - pos,
				"start_idx\tbitmap\t\t\trate_n_flags\n");

		for (j = 0; j < MAX_TID_COUNT; j++) {
			pos += scnprintf(buf + pos, bufsz - pos,
				"%d:\t%#x\t%#x\t%u\t%u\t%u\t\t%#.16llx\t%#x",
				j, station->tid[j].seq_number,
				station->tid[j].agg.txq_id,
				station->tid[j].agg.frame_count,
				station->tid[j].tfds_in_queue,
				station->tid[j].agg.start_idx,
				station->tid[j].agg.bitmap,
				station->tid[j].agg.rate_n_flags);

			if (station->tid[j].agg.wait_for_ba)
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

static ssize_t iwl_legacy_dbgfs_nvm_read(struct file *file,
				       char __user *user_buf,
				       size_t count,
				       loff_t *ppos)
{
	ssize_t ret;
	struct iwl_priv *priv = file->private_data;
	int pos = 0, ofs = 0, buf_size = 0;
	const u8 *ptr;
	char *buf;
	u16 eeprom_ver;
	size_t eeprom_len = priv->cfg->base_params->eeprom_size;
	buf_size = 4 * eeprom_len + 256;

	if (eeprom_len % 16) {
		IWL_ERR(priv, "NVM size is not multiple of 16.\n");
		return -ENODATA;
	}

	ptr = priv->eeprom;
	if (!ptr) {
		IWL_ERR(priv, "Invalid EEPROM memory\n");
		return -ENOMEM;
	}

	/* 4 characters for byte 0xYY */
	buf = kzalloc(buf_size, GFP_KERNEL);
	if (!buf) {
		IWL_ERR(priv, "Can not allocate Buffer\n");
		return -ENOMEM;
	}
	eeprom_ver = iwl_legacy_eeprom_query16(priv, EEPROM_VERSION);
	pos += scnprintf(buf + pos, buf_size - pos, "EEPROM "
			"version: 0x%x\n", eeprom_ver);
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

static ssize_t
iwl_legacy_dbgfs_channels_read(struct file *file, char __user *user_buf,
				       size_t count, loff_t *ppos)
{
	struct iwl_priv *priv = file->private_data;
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

static ssize_t iwl_legacy_dbgfs_status_read(struct file *file,
						char __user *user_buf,
						size_t count, loff_t *ppos) {

	struct iwl_priv *priv = file->private_data;
	char buf[512];
	int pos = 0;
	const size_t bufsz = sizeof(buf);

	pos += scnprintf(buf + pos, bufsz - pos, "STATUS_HCMD_ACTIVE:\t %d\n",
		test_bit(STATUS_HCMD_ACTIVE, &priv->status));
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
	return simple_read_from_buffer(user_buf, count, ppos, buf, pos);
}

static ssize_t iwl_legacy_dbgfs_interrupt_read(struct file *file,
					char __user *user_buf,
					size_t count, loff_t *ppos) {

	struct iwl_priv *priv = file->private_data;
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
	if (priv->isr_stats.sw || priv->isr_stats.hw) {
		pos += scnprintf(buf + pos, bufsz - pos,
			"\tLast Restarting Code:  0x%X\n",
			priv->isr_stats.err_code);
	}
#ifdef CONFIG_IWLWIFI_LEGACY_DEBUG
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
				iwl_legacy_get_cmd_string(cnt),
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

static ssize_t iwl_legacy_dbgfs_interrupt_write(struct file *file,
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
		iwl_legacy_clear_isr_stats(priv);

	return count;
}

static ssize_t
iwl_legacy_dbgfs_qos_read(struct file *file, char __user *user_buf,
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

static ssize_t iwl_legacy_dbgfs_disable_ht40_write(struct file *file,
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
	if (!iwl_legacy_is_any_associated(priv))
		priv->disable_ht40 = ht40 ? true : false;
	else {
		IWL_ERR(priv, "Sta associated with AP - "
			"Change to 40MHz channel support is not allowed\n");
		return -EINVAL;
	}

	return count;
}

static ssize_t iwl_legacy_dbgfs_disable_ht40_read(struct file *file,
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

DEBUGFS_READ_WRITE_FILE_OPS(sram);
DEBUGFS_READ_FILE_OPS(nvm);
DEBUGFS_READ_FILE_OPS(stations);
DEBUGFS_READ_FILE_OPS(channels);
DEBUGFS_READ_FILE_OPS(status);
DEBUGFS_READ_WRITE_FILE_OPS(interrupt);
DEBUGFS_READ_FILE_OPS(qos);
DEBUGFS_READ_WRITE_FILE_OPS(disable_ht40);

static ssize_t iwl_legacy_dbgfs_traffic_log_read(struct file *file,
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
		(priv->cfg->base_params->num_of_queues * 32 * 8) + 400;
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
	if (priv->tx_traffic && (iwlegacy_debug_level & IWL_DL_TX)) {
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

	if (priv->rx_traffic && (iwlegacy_debug_level & IWL_DL_RX)) {
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

static ssize_t iwl_legacy_dbgfs_traffic_log_write(struct file *file,
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
		iwl_legacy_reset_traffic_log(priv);

	return count;
}

static ssize_t iwl_legacy_dbgfs_tx_queue_read(struct file *file,
						char __user *user_buf,
						size_t count, loff_t *ppos) {

	struct iwl_priv *priv = file->private_data;
	struct iwl_tx_queue *txq;
	struct iwl_queue *q;
	char *buf;
	int pos = 0;
	int cnt;
	int ret;
	const size_t bufsz = sizeof(char) * 64 *
				priv->cfg->base_params->num_of_queues;

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
				txq->swq_id, txq->swq_id & 3,
				(txq->swq_id >> 2) & 0x1f);
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

static ssize_t iwl_legacy_dbgfs_rx_queue_read(struct file *file,
						char __user *user_buf,
						size_t count, loff_t *ppos) {

	struct iwl_priv *priv = file->private_data;
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
	if (rxq->rb_stts) {
		pos += scnprintf(buf + pos, bufsz - pos, "closed_rb_num: %u\n",
			 le16_to_cpu(rxq->rb_stts->closed_rb_num) &  0x0FFF);
	} else {
		pos += scnprintf(buf + pos, bufsz - pos,
					"closed_rb_num: Not Allocated\n");
	}
	return simple_read_from_buffer(user_buf, count, ppos, buf, pos);
}

static ssize_t iwl_legacy_dbgfs_ucode_rx_stats_read(struct file *file,
					char __user *user_buf,
					size_t count, loff_t *ppos)
{
	struct iwl_priv *priv = file->private_data;
	return priv->cfg->ops->lib->debugfs_ops.rx_stats_read(file,
			user_buf, count, ppos);
}

static ssize_t iwl_legacy_dbgfs_ucode_tx_stats_read(struct file *file,
					char __user *user_buf,
					size_t count, loff_t *ppos)
{
	struct iwl_priv *priv = file->private_data;
	return priv->cfg->ops->lib->debugfs_ops.tx_stats_read(file,
			user_buf, count, ppos);
}

static ssize_t iwl_legacy_dbgfs_ucode_general_stats_read(struct file *file,
					char __user *user_buf,
					size_t count, loff_t *ppos)
{
	struct iwl_priv *priv = file->private_data;
	return priv->cfg->ops->lib->debugfs_ops.general_stats_read(file,
			user_buf, count, ppos);
}

static ssize_t iwl_legacy_dbgfs_sensitivity_read(struct file *file,
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


static ssize_t iwl_legacy_dbgfs_chain_noise_read(struct file *file,
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

static ssize_t iwl_legacy_dbgfs_power_save_status_read(struct file *file,
						    char __user *user_buf,
						    size_t count, loff_t *ppos)
{
	struct iwl_priv *priv = file->private_data;
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

static ssize_t iwl_legacy_dbgfs_clear_ucode_statistics_write(struct file *file,
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
	iwl_legacy_send_statistics_request(priv, CMD_SYNC, true);
	mutex_unlock(&priv->mutex);

	return count;
}

static ssize_t iwl_legacy_dbgfs_rxon_flags_read(struct file *file,
					 char __user *user_buf,
					 size_t count, loff_t *ppos) {

	struct iwl_priv *priv = file->private_data;
	int len = 0;
	char buf[20];

	len = sprintf(buf, "0x%04X\n",
		le32_to_cpu(priv->contexts[IWL_RXON_CTX_BSS].active.flags));
	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static ssize_t iwl_legacy_dbgfs_rxon_filter_flags_read(struct file *file,
						char __user *user_buf,
						size_t count, loff_t *ppos) {

	struct iwl_priv *priv = file->private_data;
	int len = 0;
	char buf[20];

	len = sprintf(buf, "0x%04X\n",
	le32_to_cpu(priv->contexts[IWL_RXON_CTX_BSS].active.filter_flags));
	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static ssize_t iwl_legacy_dbgfs_fh_reg_read(struct file *file,
					 char __user *user_buf,
					 size_t count, loff_t *ppos)
{
	struct iwl_priv *priv = file->private_data;
	char *buf;
	int pos = 0;
	ssize_t ret = -EFAULT;

	if (priv->cfg->ops->lib->dump_fh) {
		ret = pos = priv->cfg->ops->lib->dump_fh(priv, &buf, true);
		if (buf) {
			ret = simple_read_from_buffer(user_buf,
						      count, ppos, buf, pos);
			kfree(buf);
		}
	}

	return ret;
}

static ssize_t iwl_legacy_dbgfs_missed_beacon_read(struct file *file,
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

static ssize_t iwl_legacy_dbgfs_missed_beacon_write(struct file *file,
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

static ssize_t iwl_legacy_dbgfs_force_reset_read(struct file *file,
					char __user *user_buf,
					size_t count, loff_t *ppos) {

	struct iwl_priv *priv = file->private_data;
	int pos = 0;
	char buf[300];
	const size_t bufsz = sizeof(buf);
	struct iwl_force_reset *force_reset;

	force_reset = &priv->force_reset;

	pos += scnprintf(buf + pos, bufsz - pos,
			"\tnumber of reset request: %d\n",
			force_reset->reset_request_count);
	pos += scnprintf(buf + pos, bufsz - pos,
			"\tnumber of reset request success: %d\n",
			force_reset->reset_success_count);
	pos += scnprintf(buf + pos, bufsz - pos,
			"\tnumber of reset request reject: %d\n",
			force_reset->reset_reject_count);
	pos += scnprintf(buf + pos, bufsz - pos,
			"\treset duration: %lu\n",
			force_reset->reset_duration);

	return simple_read_from_buffer(user_buf, count, ppos, buf, pos);
}

static ssize_t iwl_legacy_dbgfs_force_reset_write(struct file *file,
					const char __user *user_buf,
					size_t count, loff_t *ppos) {

	int ret;
	struct iwl_priv *priv = file->private_data;

	ret = iwl_legacy_force_reset(priv, true);

	return ret ? ret : count;
}

static ssize_t iwl_legacy_dbgfs_wd_timeout_write(struct file *file,
					const char __user *user_buf,
					size_t count, loff_t *ppos) {

	struct iwl_priv *priv = file->private_data;
	char buf[8];
	int buf_size;
	int timeout;

	memset(buf, 0, sizeof(buf));
	buf_size = min(count, sizeof(buf) -  1);
	if (copy_from_user(buf, user_buf, buf_size))
		return -EFAULT;
	if (sscanf(buf, "%d", &timeout) != 1)
		return -EINVAL;
	if (timeout < 0 || timeout > IWL_MAX_WD_TIMEOUT)
		timeout = IWL_DEF_WD_TIMEOUT;

	priv->cfg->base_params->wd_timeout = timeout;
	iwl_legacy_setup_watchdog(priv);
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
DEBUGFS_READ_FILE_OPS(power_save_status);
DEBUGFS_WRITE_FILE_OPS(clear_ucode_statistics);
DEBUGFS_WRITE_FILE_OPS(clear_traffic_statistics);
DEBUGFS_READ_FILE_OPS(fh_reg);
DEBUGFS_READ_WRITE_FILE_OPS(missed_beacon);
DEBUGFS_READ_WRITE_FILE_OPS(force_reset);
DEBUGFS_READ_FILE_OPS(rxon_flags);
DEBUGFS_READ_FILE_OPS(rxon_filter_flags);
DEBUGFS_WRITE_FILE_OPS(wd_timeout);

/*
 * Create the debugfs files and directories
 *
 */
int iwl_legacy_dbgfs_register(struct iwl_priv *priv, const char *name)
{
	struct dentry *phyd = priv->hw->wiphy->debugfsdir;
	struct dentry *dir_drv, *dir_data, *dir_rf, *dir_debug;

	dir_drv = debugfs_create_dir(name, phyd);
	if (!dir_drv)
		return -ENOMEM;

	priv->debugfs_dir = dir_drv;

	dir_data = debugfs_create_dir("data", dir_drv);
	if (!dir_data)
		goto err;
	dir_rf = debugfs_create_dir("rf", dir_drv);
	if (!dir_rf)
		goto err;
	dir_debug = debugfs_create_dir("debug", dir_drv);
	if (!dir_debug)
		goto err;

	DEBUGFS_ADD_FILE(nvm, dir_data, S_IRUSR);
	DEBUGFS_ADD_FILE(sram, dir_data, S_IWUSR | S_IRUSR);
	DEBUGFS_ADD_FILE(stations, dir_data, S_IRUSR);
	DEBUGFS_ADD_FILE(channels, dir_data, S_IRUSR);
	DEBUGFS_ADD_FILE(status, dir_data, S_IRUSR);
	DEBUGFS_ADD_FILE(interrupt, dir_data, S_IWUSR | S_IRUSR);
	DEBUGFS_ADD_FILE(qos, dir_data, S_IRUSR);
	DEBUGFS_ADD_FILE(disable_ht40, dir_data, S_IWUSR | S_IRUSR);
	DEBUGFS_ADD_FILE(rx_statistics, dir_debug, S_IRUSR);
	DEBUGFS_ADD_FILE(tx_statistics, dir_debug, S_IRUSR);
	DEBUGFS_ADD_FILE(traffic_log, dir_debug, S_IWUSR | S_IRUSR);
	DEBUGFS_ADD_FILE(rx_queue, dir_debug, S_IRUSR);
	DEBUGFS_ADD_FILE(tx_queue, dir_debug, S_IRUSR);
	DEBUGFS_ADD_FILE(power_save_status, dir_debug, S_IRUSR);
	DEBUGFS_ADD_FILE(clear_ucode_statistics, dir_debug, S_IWUSR);
	DEBUGFS_ADD_FILE(clear_traffic_statistics, dir_debug, S_IWUSR);
	DEBUGFS_ADD_FILE(fh_reg, dir_debug, S_IRUSR);
	DEBUGFS_ADD_FILE(missed_beacon, dir_debug, S_IWUSR);
	DEBUGFS_ADD_FILE(force_reset, dir_debug, S_IWUSR | S_IRUSR);
	DEBUGFS_ADD_FILE(ucode_rx_stats, dir_debug, S_IRUSR);
	DEBUGFS_ADD_FILE(ucode_tx_stats, dir_debug, S_IRUSR);
	DEBUGFS_ADD_FILE(ucode_general_stats, dir_debug, S_IRUSR);

	if (priv->cfg->base_params->sensitivity_calib_by_driver)
		DEBUGFS_ADD_FILE(sensitivity, dir_debug, S_IRUSR);
	if (priv->cfg->base_params->chain_noise_calib_by_driver)
		DEBUGFS_ADD_FILE(chain_noise, dir_debug, S_IRUSR);
	DEBUGFS_ADD_FILE(rxon_flags, dir_debug, S_IWUSR);
	DEBUGFS_ADD_FILE(rxon_filter_flags, dir_debug, S_IWUSR);
	DEBUGFS_ADD_FILE(wd_timeout, dir_debug, S_IWUSR);
	if (priv->cfg->base_params->sensitivity_calib_by_driver)
		DEBUGFS_ADD_BOOL(disable_sensitivity, dir_rf,
				 &priv->disable_sens_cal);
	if (priv->cfg->base_params->chain_noise_calib_by_driver)
		DEBUGFS_ADD_BOOL(disable_chain_noise, dir_rf,
				 &priv->disable_chain_noise_cal);
	DEBUGFS_ADD_BOOL(disable_tx_power, dir_rf,
				&priv->disable_tx_power_cal);
	return 0;

err:
	IWL_ERR(priv, "Can't create the debugfs directory\n");
	iwl_legacy_dbgfs_unregister(priv);
	return -ENOMEM;
}
EXPORT_SYMBOL(iwl_legacy_dbgfs_register);

/**
 * Remove the debugfs files and directories
 *
 */
void iwl_legacy_dbgfs_unregister(struct iwl_priv *priv)
{
	if (!priv->debugfs_dir)
		return;

	debugfs_remove_recursive(priv->debugfs_dir);
	priv->debugfs_dir = NULL;
}
EXPORT_SYMBOL(iwl_legacy_dbgfs_unregister);
