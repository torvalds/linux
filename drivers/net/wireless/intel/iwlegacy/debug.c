// SPDX-License-Identifier: GPL-2.0-only
/******************************************************************************
 *
 * Copyright(c) 2008 - 2011 Intel Corporation. All rights reserved.
 *
 * Contact Information:
 *  Intel Linux Wireless <ilw@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *****************************************************************************/
#include <linux/ieee80211.h>
#include <linux/export.h>
#include <net/mac80211.h>

#include "common.h"

static void
il_clear_traffic_stats(struct il_priv *il)
{
	memset(&il->tx_stats, 0, sizeof(struct traffic_stats));
	memset(&il->rx_stats, 0, sizeof(struct traffic_stats));
}

/*
 * il_update_stats function record all the MGMT, CTRL and DATA pkt for
 * both TX and Rx . Use debugfs to display the rx/rx_stats
 */
void
il_update_stats(struct il_priv *il, bool is_tx, __le16 fc, u16 len)
{
	struct traffic_stats *stats;

	if (is_tx)
		stats = &il->tx_stats;
	else
		stats = &il->rx_stats;

	if (ieee80211_is_mgmt(fc)) {
		switch (fc & cpu_to_le16(IEEE80211_FCTL_STYPE)) {
		case cpu_to_le16(IEEE80211_STYPE_ASSOC_REQ):
			stats->mgmt[MANAGEMENT_ASSOC_REQ]++;
			break;
		case cpu_to_le16(IEEE80211_STYPE_ASSOC_RESP):
			stats->mgmt[MANAGEMENT_ASSOC_RESP]++;
			break;
		case cpu_to_le16(IEEE80211_STYPE_REASSOC_REQ):
			stats->mgmt[MANAGEMENT_REASSOC_REQ]++;
			break;
		case cpu_to_le16(IEEE80211_STYPE_REASSOC_RESP):
			stats->mgmt[MANAGEMENT_REASSOC_RESP]++;
			break;
		case cpu_to_le16(IEEE80211_STYPE_PROBE_REQ):
			stats->mgmt[MANAGEMENT_PROBE_REQ]++;
			break;
		case cpu_to_le16(IEEE80211_STYPE_PROBE_RESP):
			stats->mgmt[MANAGEMENT_PROBE_RESP]++;
			break;
		case cpu_to_le16(IEEE80211_STYPE_BEACON):
			stats->mgmt[MANAGEMENT_BEACON]++;
			break;
		case cpu_to_le16(IEEE80211_STYPE_ATIM):
			stats->mgmt[MANAGEMENT_ATIM]++;
			break;
		case cpu_to_le16(IEEE80211_STYPE_DISASSOC):
			stats->mgmt[MANAGEMENT_DISASSOC]++;
			break;
		case cpu_to_le16(IEEE80211_STYPE_AUTH):
			stats->mgmt[MANAGEMENT_AUTH]++;
			break;
		case cpu_to_le16(IEEE80211_STYPE_DEAUTH):
			stats->mgmt[MANAGEMENT_DEAUTH]++;
			break;
		case cpu_to_le16(IEEE80211_STYPE_ACTION):
			stats->mgmt[MANAGEMENT_ACTION]++;
			break;
		}
	} else if (ieee80211_is_ctl(fc)) {
		switch (fc & cpu_to_le16(IEEE80211_FCTL_STYPE)) {
		case cpu_to_le16(IEEE80211_STYPE_BACK_REQ):
			stats->ctrl[CONTROL_BACK_REQ]++;
			break;
		case cpu_to_le16(IEEE80211_STYPE_BACK):
			stats->ctrl[CONTROL_BACK]++;
			break;
		case cpu_to_le16(IEEE80211_STYPE_PSPOLL):
			stats->ctrl[CONTROL_PSPOLL]++;
			break;
		case cpu_to_le16(IEEE80211_STYPE_RTS):
			stats->ctrl[CONTROL_RTS]++;
			break;
		case cpu_to_le16(IEEE80211_STYPE_CTS):
			stats->ctrl[CONTROL_CTS]++;
			break;
		case cpu_to_le16(IEEE80211_STYPE_ACK):
			stats->ctrl[CONTROL_ACK]++;
			break;
		case cpu_to_le16(IEEE80211_STYPE_CFEND):
			stats->ctrl[CONTROL_CFEND]++;
			break;
		case cpu_to_le16(IEEE80211_STYPE_CFENDACK):
			stats->ctrl[CONTROL_CFENDACK]++;
			break;
		}
	} else {
		/* data */
		stats->data_cnt++;
		stats->data_bytes += len;
	}
}
EXPORT_SYMBOL(il_update_stats);

/* create and remove of files */
#define DEBUGFS_ADD_FILE(name, parent, mode) do {			\
	debugfs_create_file(#name, mode, parent, il,			\
			    &il_dbgfs_##name##_ops);			\
} while (0)

#define DEBUGFS_ADD_BOOL(name, parent, ptr) do {			\
	debugfs_create_bool(#name, 0600, parent, ptr);			\
} while (0)

/* file operation */
#define DEBUGFS_READ_FUNC(name)                                         \
static ssize_t il_dbgfs_##name##_read(struct file *file,               \
					char __user *user_buf,          \
					size_t count, loff_t *ppos);

#define DEBUGFS_WRITE_FUNC(name)                                        \
static ssize_t il_dbgfs_##name##_write(struct file *file,              \
					const char __user *user_buf,    \
					size_t count, loff_t *ppos);


#define DEBUGFS_READ_FILE_OPS(name)				\
	DEBUGFS_READ_FUNC(name);				\
static const struct file_operations il_dbgfs_##name##_ops = {	\
	.read = il_dbgfs_##name##_read,				\
	.open = simple_open,					\
	.llseek = generic_file_llseek,				\
};

#define DEBUGFS_WRITE_FILE_OPS(name)				\
	DEBUGFS_WRITE_FUNC(name);				\
static const struct file_operations il_dbgfs_##name##_ops = {	\
	.write = il_dbgfs_##name##_write,			\
	.open = simple_open,					\
	.llseek = generic_file_llseek,				\
};

#define DEBUGFS_READ_WRITE_FILE_OPS(name)			\
	DEBUGFS_READ_FUNC(name);				\
	DEBUGFS_WRITE_FUNC(name);				\
static const struct file_operations il_dbgfs_##name##_ops = {	\
	.write = il_dbgfs_##name##_write,			\
	.read = il_dbgfs_##name##_read,				\
	.open = simple_open,					\
	.llseek = generic_file_llseek,				\
};

static const char *
il_get_mgmt_string(int cmd)
{
	switch (cmd) {
	IL_CMD(MANAGEMENT_ASSOC_REQ);
	IL_CMD(MANAGEMENT_ASSOC_RESP);
	IL_CMD(MANAGEMENT_REASSOC_REQ);
	IL_CMD(MANAGEMENT_REASSOC_RESP);
	IL_CMD(MANAGEMENT_PROBE_REQ);
	IL_CMD(MANAGEMENT_PROBE_RESP);
	IL_CMD(MANAGEMENT_BEACON);
	IL_CMD(MANAGEMENT_ATIM);
	IL_CMD(MANAGEMENT_DISASSOC);
	IL_CMD(MANAGEMENT_AUTH);
	IL_CMD(MANAGEMENT_DEAUTH);
	IL_CMD(MANAGEMENT_ACTION);
	default:
		return "UNKNOWN";

	}
}

static const char *
il_get_ctrl_string(int cmd)
{
	switch (cmd) {
	IL_CMD(CONTROL_BACK_REQ);
	IL_CMD(CONTROL_BACK);
	IL_CMD(CONTROL_PSPOLL);
	IL_CMD(CONTROL_RTS);
	IL_CMD(CONTROL_CTS);
	IL_CMD(CONTROL_ACK);
	IL_CMD(CONTROL_CFEND);
	IL_CMD(CONTROL_CFENDACK);
	default:
		return "UNKNOWN";

	}
}

static ssize_t
il_dbgfs_tx_stats_read(struct file *file, char __user *user_buf, size_t count,
		       loff_t *ppos)
{

	struct il_priv *il = file->private_data;
	char *buf;
	int pos = 0;

	int cnt;
	ssize_t ret;
	const size_t bufsz =
	    100 + sizeof(char) * 50 * (MANAGEMENT_MAX + CONTROL_MAX);
	buf = kzalloc(bufsz, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;
	pos += scnprintf(buf + pos, bufsz - pos, "Management:\n");
	for (cnt = 0; cnt < MANAGEMENT_MAX; cnt++) {
		pos +=
		    scnprintf(buf + pos, bufsz - pos, "\t%25s\t\t: %u\n",
			      il_get_mgmt_string(cnt), il->tx_stats.mgmt[cnt]);
	}
	pos += scnprintf(buf + pos, bufsz - pos, "Control\n");
	for (cnt = 0; cnt < CONTROL_MAX; cnt++) {
		pos +=
		    scnprintf(buf + pos, bufsz - pos, "\t%25s\t\t: %u\n",
			      il_get_ctrl_string(cnt), il->tx_stats.ctrl[cnt]);
	}
	pos += scnprintf(buf + pos, bufsz - pos, "Data:\n");
	pos +=
	    scnprintf(buf + pos, bufsz - pos, "\tcnt: %u\n",
		      il->tx_stats.data_cnt);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, "\tbytes: %llu\n",
		      il->tx_stats.data_bytes);
	ret = simple_read_from_buffer(user_buf, count, ppos, buf, pos);
	kfree(buf);
	return ret;
}

static ssize_t
il_dbgfs_clear_traffic_stats_write(struct file *file,
				   const char __user *user_buf, size_t count,
				   loff_t *ppos)
{
	struct il_priv *il = file->private_data;
	u32 clear_flag;
	char buf[8];
	int buf_size;

	memset(buf, 0, sizeof(buf));
	buf_size = min(count, sizeof(buf) - 1);
	if (copy_from_user(buf, user_buf, buf_size))
		return -EFAULT;
	if (sscanf(buf, "%x", &clear_flag) != 1)
		return -EFAULT;
	il_clear_traffic_stats(il);

	return count;
}

static ssize_t
il_dbgfs_rx_stats_read(struct file *file, char __user *user_buf, size_t count,
		       loff_t *ppos)
{

	struct il_priv *il = file->private_data;
	char *buf;
	int pos = 0;
	int cnt;
	ssize_t ret;
	const size_t bufsz =
	    100 + sizeof(char) * 50 * (MANAGEMENT_MAX + CONTROL_MAX);
	buf = kzalloc(bufsz, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	pos += scnprintf(buf + pos, bufsz - pos, "Management:\n");
	for (cnt = 0; cnt < MANAGEMENT_MAX; cnt++) {
		pos +=
		    scnprintf(buf + pos, bufsz - pos, "\t%25s\t\t: %u\n",
			      il_get_mgmt_string(cnt), il->rx_stats.mgmt[cnt]);
	}
	pos += scnprintf(buf + pos, bufsz - pos, "Control:\n");
	for (cnt = 0; cnt < CONTROL_MAX; cnt++) {
		pos +=
		    scnprintf(buf + pos, bufsz - pos, "\t%25s\t\t: %u\n",
			      il_get_ctrl_string(cnt), il->rx_stats.ctrl[cnt]);
	}
	pos += scnprintf(buf + pos, bufsz - pos, "Data:\n");
	pos +=
	    scnprintf(buf + pos, bufsz - pos, "\tcnt: %u\n",
		      il->rx_stats.data_cnt);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, "\tbytes: %llu\n",
		      il->rx_stats.data_bytes);

	ret = simple_read_from_buffer(user_buf, count, ppos, buf, pos);
	kfree(buf);
	return ret;
}

#define BYTE1_MASK 0x000000ff;
#define BYTE2_MASK 0x0000ffff;
#define BYTE3_MASK 0x00ffffff;
static ssize_t
il_dbgfs_sram_read(struct file *file, char __user *user_buf, size_t count,
		   loff_t *ppos)
{
	u32 val;
	char *buf;
	ssize_t ret;
	int i;
	int pos = 0;
	struct il_priv *il = file->private_data;
	size_t bufsz;

	/* default is to dump the entire data segment */
	if (!il->dbgfs_sram_offset && !il->dbgfs_sram_len) {
		il->dbgfs_sram_offset = 0x800000;
		if (il->ucode_type == UCODE_INIT)
			il->dbgfs_sram_len = il->ucode_init_data.len;
		else
			il->dbgfs_sram_len = il->ucode_data.len;
	}
	bufsz = 30 + il->dbgfs_sram_len * sizeof(char) * 10;
	buf = kmalloc(bufsz, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;
	pos +=
	    scnprintf(buf + pos, bufsz - pos, "sram_len: 0x%x\n",
		      il->dbgfs_sram_len);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, "sram_offset: 0x%x\n",
		      il->dbgfs_sram_offset);
	for (i = il->dbgfs_sram_len; i > 0; i -= 4) {
		val =
		    il_read_targ_mem(il,
				     il->dbgfs_sram_offset +
				     il->dbgfs_sram_len - i);
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

static ssize_t
il_dbgfs_sram_write(struct file *file, const char __user *user_buf,
		    size_t count, loff_t *ppos)
{
	struct il_priv *il = file->private_data;
	char buf[64];
	int buf_size;
	u32 offset, len;

	memset(buf, 0, sizeof(buf));
	buf_size = min(count, sizeof(buf) - 1);
	if (copy_from_user(buf, user_buf, buf_size))
		return -EFAULT;

	if (sscanf(buf, "%x,%x", &offset, &len) == 2) {
		il->dbgfs_sram_offset = offset;
		il->dbgfs_sram_len = len;
	} else {
		il->dbgfs_sram_offset = 0;
		il->dbgfs_sram_len = 0;
	}

	return count;
}

static ssize_t
il_dbgfs_stations_read(struct file *file, char __user *user_buf, size_t count,
		       loff_t *ppos)
{
	struct il_priv *il = file->private_data;
	struct il_station_entry *station;
	int max_sta = il->hw_params.max_stations;
	char *buf;
	int i, j, pos = 0;
	ssize_t ret;
	/* Add 30 for initial string */
	const size_t bufsz = 30 + sizeof(char) * 500 * (il->num_stations);

	buf = kmalloc(bufsz, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	pos +=
	    scnprintf(buf + pos, bufsz - pos, "num of stations: %d\n\n",
		      il->num_stations);

	for (i = 0; i < max_sta; i++) {
		station = &il->stations[i];
		if (!station->used)
			continue;
		pos +=
		    scnprintf(buf + pos, bufsz - pos,
			      "station %d - addr: %pM, flags: %#x\n", i,
			      station->sta.sta.addr,
			      station->sta.station_flags_msk);
		pos +=
		    scnprintf(buf + pos, bufsz - pos,
			      "TID\tseq_num\ttxq_id\tframes\ttfds\t");
		pos +=
		    scnprintf(buf + pos, bufsz - pos,
			      "start_idx\tbitmap\t\t\trate_n_flags\n");

		for (j = 0; j < MAX_TID_COUNT; j++) {
			pos +=
			    scnprintf(buf + pos, bufsz - pos,
				      "%d:\t%#x\t%#x\t%u\t%u\t%u\t\t%#.16llx\t%#x",
				      j, station->tid[j].seq_number,
				      station->tid[j].agg.txq_id,
				      station->tid[j].agg.frame_count,
				      station->tid[j].tfds_in_queue,
				      station->tid[j].agg.start_idx,
				      station->tid[j].agg.bitmap,
				      station->tid[j].agg.rate_n_flags);

			if (station->tid[j].agg.wait_for_ba)
				pos +=
				    scnprintf(buf + pos, bufsz - pos,
					      " - waitforba");
			pos += scnprintf(buf + pos, bufsz - pos, "\n");
		}

		pos += scnprintf(buf + pos, bufsz - pos, "\n");
	}

	ret = simple_read_from_buffer(user_buf, count, ppos, buf, pos);
	kfree(buf);
	return ret;
}

static ssize_t
il_dbgfs_nvm_read(struct file *file, char __user *user_buf, size_t count,
		  loff_t *ppos)
{
	ssize_t ret;
	struct il_priv *il = file->private_data;
	int pos = 0, ofs = 0, buf_size = 0;
	const u8 *ptr;
	char *buf;
	u16 eeprom_ver;
	size_t eeprom_len = il->cfg->eeprom_size;
	buf_size = 4 * eeprom_len + 256;

	if (eeprom_len % 16) {
		IL_ERR("NVM size is not multiple of 16.\n");
		return -ENODATA;
	}

	ptr = il->eeprom;
	if (!ptr) {
		IL_ERR("Invalid EEPROM memory\n");
		return -ENOMEM;
	}

	/* 4 characters for byte 0xYY */
	buf = kzalloc(buf_size, GFP_KERNEL);
	if (!buf) {
		IL_ERR("Can not allocate Buffer\n");
		return -ENOMEM;
	}
	eeprom_ver = il_eeprom_query16(il, EEPROM_VERSION);
	pos +=
	    scnprintf(buf + pos, buf_size - pos, "EEPROM " "version: 0x%x\n",
		      eeprom_ver);
	for (ofs = 0; ofs < eeprom_len; ofs += 16) {
		pos += scnprintf(buf + pos, buf_size - pos, "0x%.4x %16ph\n",
				 ofs, ptr + ofs);
	}

	ret = simple_read_from_buffer(user_buf, count, ppos, buf, pos);
	kfree(buf);
	return ret;
}

static ssize_t
il_dbgfs_channels_read(struct file *file, char __user *user_buf, size_t count,
		       loff_t *ppos)
{
	struct il_priv *il = file->private_data;
	struct ieee80211_channel *channels = NULL;
	const struct ieee80211_supported_band *supp_band = NULL;
	int pos = 0, i, bufsz = PAGE_SIZE;
	char *buf;
	ssize_t ret;

	if (!test_bit(S_GEO_CONFIGURED, &il->status))
		return -EAGAIN;

	buf = kzalloc(bufsz, GFP_KERNEL);
	if (!buf) {
		IL_ERR("Can not allocate Buffer\n");
		return -ENOMEM;
	}

	supp_band = il_get_hw_mode(il, NL80211_BAND_2GHZ);
	if (supp_band) {
		channels = supp_band->channels;

		pos +=
		    scnprintf(buf + pos, bufsz - pos,
			      "Displaying %d channels in 2.4GHz band 802.11bg):\n",
			      supp_band->n_channels);

		for (i = 0; i < supp_band->n_channels; i++)
			pos +=
			    scnprintf(buf + pos, bufsz - pos,
				      "%d: %ddBm: BSS%s%s, %s.\n",
				      channels[i].hw_value,
				      channels[i].max_power,
				      channels[i].
				      flags & IEEE80211_CHAN_RADAR ?
				      " (IEEE 802.11h required)" : "",
				      ((channels[i].
					flags & IEEE80211_CHAN_NO_IR) ||
				       (channels[i].
					flags & IEEE80211_CHAN_RADAR)) ? "" :
				      ", IBSS",
				      channels[i].
				      flags & IEEE80211_CHAN_NO_IR ?
				      "passive only" : "active/passive");
	}
	supp_band = il_get_hw_mode(il, NL80211_BAND_5GHZ);
	if (supp_band) {
		channels = supp_band->channels;

		pos +=
		    scnprintf(buf + pos, bufsz - pos,
			      "Displaying %d channels in 5.2GHz band (802.11a)\n",
			      supp_band->n_channels);

		for (i = 0; i < supp_band->n_channels; i++)
			pos +=
			    scnprintf(buf + pos, bufsz - pos,
				      "%d: %ddBm: BSS%s%s, %s.\n",
				      channels[i].hw_value,
				      channels[i].max_power,
				      channels[i].
				      flags & IEEE80211_CHAN_RADAR ?
				      " (IEEE 802.11h required)" : "",
				      ((channels[i].
					flags & IEEE80211_CHAN_NO_IR) ||
				       (channels[i].
					flags & IEEE80211_CHAN_RADAR)) ? "" :
				      ", IBSS",
				      channels[i].
				      flags & IEEE80211_CHAN_NO_IR ?
				      "passive only" : "active/passive");
	}
	ret = simple_read_from_buffer(user_buf, count, ppos, buf, pos);
	kfree(buf);
	return ret;
}

static ssize_t
il_dbgfs_status_read(struct file *file, char __user *user_buf, size_t count,
		     loff_t *ppos)
{

	struct il_priv *il = file->private_data;
	char buf[512];
	int pos = 0;
	const size_t bufsz = sizeof(buf);

	pos +=
	    scnprintf(buf + pos, bufsz - pos, "S_HCMD_ACTIVE:\t %d\n",
		      test_bit(S_HCMD_ACTIVE, &il->status));
	pos +=
	    scnprintf(buf + pos, bufsz - pos, "S_INT_ENABLED:\t %d\n",
		      test_bit(S_INT_ENABLED, &il->status));
	pos +=
	    scnprintf(buf + pos, bufsz - pos, "S_RFKILL:\t %d\n",
		      test_bit(S_RFKILL, &il->status));
	pos +=
	    scnprintf(buf + pos, bufsz - pos, "S_CT_KILL:\t\t %d\n",
		      test_bit(S_CT_KILL, &il->status));
	pos +=
	    scnprintf(buf + pos, bufsz - pos, "S_INIT:\t\t %d\n",
		      test_bit(S_INIT, &il->status));
	pos +=
	    scnprintf(buf + pos, bufsz - pos, "S_ALIVE:\t\t %d\n",
		      test_bit(S_ALIVE, &il->status));
	pos +=
	    scnprintf(buf + pos, bufsz - pos, "S_READY:\t\t %d\n",
		      test_bit(S_READY, &il->status));
	pos +=
	    scnprintf(buf + pos, bufsz - pos, "S_TEMPERATURE:\t %d\n",
		      test_bit(S_TEMPERATURE, &il->status));
	pos +=
	    scnprintf(buf + pos, bufsz - pos, "S_GEO_CONFIGURED:\t %d\n",
		      test_bit(S_GEO_CONFIGURED, &il->status));
	pos +=
	    scnprintf(buf + pos, bufsz - pos, "S_EXIT_PENDING:\t %d\n",
		      test_bit(S_EXIT_PENDING, &il->status));
	pos +=
	    scnprintf(buf + pos, bufsz - pos, "S_STATS:\t %d\n",
		      test_bit(S_STATS, &il->status));
	pos +=
	    scnprintf(buf + pos, bufsz - pos, "S_SCANNING:\t %d\n",
		      test_bit(S_SCANNING, &il->status));
	pos +=
	    scnprintf(buf + pos, bufsz - pos, "S_SCAN_ABORTING:\t %d\n",
		      test_bit(S_SCAN_ABORTING, &il->status));
	pos +=
	    scnprintf(buf + pos, bufsz - pos, "S_SCAN_HW:\t\t %d\n",
		      test_bit(S_SCAN_HW, &il->status));
	pos +=
	    scnprintf(buf + pos, bufsz - pos, "S_POWER_PMI:\t %d\n",
		      test_bit(S_POWER_PMI, &il->status));
	pos +=
	    scnprintf(buf + pos, bufsz - pos, "S_FW_ERROR:\t %d\n",
		      test_bit(S_FW_ERROR, &il->status));
	return simple_read_from_buffer(user_buf, count, ppos, buf, pos);
}

static ssize_t
il_dbgfs_interrupt_read(struct file *file, char __user *user_buf, size_t count,
			loff_t *ppos)
{

	struct il_priv *il = file->private_data;
	int pos = 0;
	int cnt = 0;
	char *buf;
	int bufsz = 24 * 64;	/* 24 items * 64 char per item */
	ssize_t ret;

	buf = kzalloc(bufsz, GFP_KERNEL);
	if (!buf) {
		IL_ERR("Can not allocate Buffer\n");
		return -ENOMEM;
	}

	pos +=
	    scnprintf(buf + pos, bufsz - pos, "Interrupt Statistics Report:\n");

	pos +=
	    scnprintf(buf + pos, bufsz - pos, "HW Error:\t\t\t %u\n",
		      il->isr_stats.hw);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, "SW Error:\t\t\t %u\n",
		      il->isr_stats.sw);
	if (il->isr_stats.sw || il->isr_stats.hw) {
		pos +=
		    scnprintf(buf + pos, bufsz - pos,
			      "\tLast Restarting Code:  0x%X\n",
			      il->isr_stats.err_code);
	}
#ifdef CONFIG_IWLEGACY_DEBUG
	pos +=
	    scnprintf(buf + pos, bufsz - pos, "Frame transmitted:\t\t %u\n",
		      il->isr_stats.sch);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, "Alive interrupt:\t\t %u\n",
		      il->isr_stats.alive);
#endif
	pos +=
	    scnprintf(buf + pos, bufsz - pos,
		      "HW RF KILL switch toggled:\t %u\n",
		      il->isr_stats.rfkill);

	pos +=
	    scnprintf(buf + pos, bufsz - pos, "CT KILL:\t\t\t %u\n",
		      il->isr_stats.ctkill);

	pos +=
	    scnprintf(buf + pos, bufsz - pos, "Wakeup Interrupt:\t\t %u\n",
		      il->isr_stats.wakeup);

	pos +=
	    scnprintf(buf + pos, bufsz - pos, "Rx command responses:\t\t %u\n",
		      il->isr_stats.rx);
	for (cnt = 0; cnt < IL_CN_MAX; cnt++) {
		if (il->isr_stats.handlers[cnt] > 0)
			pos +=
			    scnprintf(buf + pos, bufsz - pos,
				      "\tRx handler[%36s]:\t\t %u\n",
				      il_get_cmd_string(cnt),
				      il->isr_stats.handlers[cnt]);
	}

	pos +=
	    scnprintf(buf + pos, bufsz - pos, "Tx/FH interrupt:\t\t %u\n",
		      il->isr_stats.tx);

	pos +=
	    scnprintf(buf + pos, bufsz - pos, "Unexpected INTA:\t\t %u\n",
		      il->isr_stats.unhandled);

	ret = simple_read_from_buffer(user_buf, count, ppos, buf, pos);
	kfree(buf);
	return ret;
}

static ssize_t
il_dbgfs_interrupt_write(struct file *file, const char __user *user_buf,
			 size_t count, loff_t *ppos)
{
	struct il_priv *il = file->private_data;
	char buf[8];
	int buf_size;
	u32 reset_flag;

	memset(buf, 0, sizeof(buf));
	buf_size = min(count, sizeof(buf) - 1);
	if (copy_from_user(buf, user_buf, buf_size))
		return -EFAULT;
	if (sscanf(buf, "%x", &reset_flag) != 1)
		return -EFAULT;
	if (reset_flag == 0)
		il_clear_isr_stats(il);

	return count;
}

static ssize_t
il_dbgfs_qos_read(struct file *file, char __user *user_buf, size_t count,
		  loff_t *ppos)
{
	struct il_priv *il = file->private_data;
	int pos = 0, i;
	char buf[256];
	const size_t bufsz = sizeof(buf);

	for (i = 0; i < AC_NUM; i++) {
		pos +=
		    scnprintf(buf + pos, bufsz - pos,
			      "\tcw_min\tcw_max\taifsn\ttxop\n");
		pos +=
		    scnprintf(buf + pos, bufsz - pos,
			      "AC[%d]\t%u\t%u\t%u\t%u\n", i,
			      il->qos_data.def_qos_parm.ac[i].cw_min,
			      il->qos_data.def_qos_parm.ac[i].cw_max,
			      il->qos_data.def_qos_parm.ac[i].aifsn,
			      il->qos_data.def_qos_parm.ac[i].edca_txop);
	}

	return simple_read_from_buffer(user_buf, count, ppos, buf, pos);
}

static ssize_t
il_dbgfs_disable_ht40_write(struct file *file, const char __user *user_buf,
			    size_t count, loff_t *ppos)
{
	struct il_priv *il = file->private_data;
	char buf[8];
	int buf_size;
	int ht40;

	memset(buf, 0, sizeof(buf));
	buf_size = min(count, sizeof(buf) - 1);
	if (copy_from_user(buf, user_buf, buf_size))
		return -EFAULT;
	if (sscanf(buf, "%d", &ht40) != 1)
		return -EFAULT;
	if (!il_is_any_associated(il))
		il->disable_ht40 = ht40 ? true : false;
	else {
		IL_ERR("Sta associated with AP - "
		       "Change to 40MHz channel support is not allowed\n");
		return -EINVAL;
	}

	return count;
}

static ssize_t
il_dbgfs_disable_ht40_read(struct file *file, char __user *user_buf,
			   size_t count, loff_t *ppos)
{
	struct il_priv *il = file->private_data;
	char buf[100];
	int pos = 0;
	const size_t bufsz = sizeof(buf);

	pos +=
	    scnprintf(buf + pos, bufsz - pos, "11n 40MHz Mode: %s\n",
		      il->disable_ht40 ? "Disabled" : "Enabled");
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

static ssize_t
il_dbgfs_tx_queue_read(struct file *file, char __user *user_buf, size_t count,
		       loff_t *ppos)
{

	struct il_priv *il = file->private_data;
	struct il_tx_queue *txq;
	struct il_queue *q;
	char *buf;
	int pos = 0;
	int cnt;
	int ret;
	const size_t bufsz =
	    sizeof(char) * 64 * il->cfg->num_of_queues;

	if (!il->txq) {
		IL_ERR("txq not ready\n");
		return -EAGAIN;
	}
	buf = kzalloc(bufsz, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	for (cnt = 0; cnt < il->hw_params.max_txq_num; cnt++) {
		txq = &il->txq[cnt];
		q = &txq->q;
		pos +=
		    scnprintf(buf + pos, bufsz - pos,
			      "hwq %.2d: read=%u write=%u stop=%d"
			      " swq_id=%#.2x (ac %d/hwq %d)\n", cnt,
			      q->read_ptr, q->write_ptr,
			      !!test_bit(cnt, il->queue_stopped),
			      txq->swq_id, txq->swq_id & 3,
			      (txq->swq_id >> 2) & 0x1f);
		if (cnt >= 4)
			continue;
		/* for the ACs, display the stop count too */
		pos +=
		    scnprintf(buf + pos, bufsz - pos,
			      "        stop-count: %d\n",
			      atomic_read(&il->queue_stop_count[cnt]));
	}
	ret = simple_read_from_buffer(user_buf, count, ppos, buf, pos);
	kfree(buf);
	return ret;
}

static ssize_t
il_dbgfs_rx_queue_read(struct file *file, char __user *user_buf, size_t count,
		       loff_t *ppos)
{

	struct il_priv *il = file->private_data;
	struct il_rx_queue *rxq = &il->rxq;
	char buf[256];
	int pos = 0;
	const size_t bufsz = sizeof(buf);

	pos += scnprintf(buf + pos, bufsz - pos, "read: %u\n", rxq->read);
	pos += scnprintf(buf + pos, bufsz - pos, "write: %u\n", rxq->write);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, "free_count: %u\n",
		      rxq->free_count);
	if (rxq->rb_stts) {
		pos +=
		    scnprintf(buf + pos, bufsz - pos, "closed_rb_num: %u\n",
			      le16_to_cpu(rxq->rb_stts->
					  closed_rb_num) & 0x0FFF);
	} else {
		pos +=
		    scnprintf(buf + pos, bufsz - pos,
			      "closed_rb_num: Not Allocated\n");
	}
	return simple_read_from_buffer(user_buf, count, ppos, buf, pos);
}

static ssize_t
il_dbgfs_ucode_rx_stats_read(struct file *file, char __user *user_buf,
			     size_t count, loff_t *ppos)
{
	struct il_priv *il = file->private_data;

	return il->debugfs_ops->rx_stats_read(file, user_buf, count, ppos);
}

static ssize_t
il_dbgfs_ucode_tx_stats_read(struct file *file, char __user *user_buf,
			     size_t count, loff_t *ppos)
{
	struct il_priv *il = file->private_data;

	return il->debugfs_ops->tx_stats_read(file, user_buf, count, ppos);
}

static ssize_t
il_dbgfs_ucode_general_stats_read(struct file *file, char __user *user_buf,
				  size_t count, loff_t *ppos)
{
	struct il_priv *il = file->private_data;

	return il->debugfs_ops->general_stats_read(file, user_buf, count, ppos);
}

static ssize_t
il_dbgfs_sensitivity_read(struct file *file, char __user *user_buf,
			  size_t count, loff_t *ppos)
{

	struct il_priv *il = file->private_data;
	int pos = 0;
	int cnt = 0;
	char *buf;
	int bufsz = sizeof(struct il_sensitivity_data) * 4 + 100;
	ssize_t ret;
	struct il_sensitivity_data *data;

	data = &il->sensitivity_data;
	buf = kzalloc(bufsz, GFP_KERNEL);
	if (!buf) {
		IL_ERR("Can not allocate Buffer\n");
		return -ENOMEM;
	}

	pos +=
	    scnprintf(buf + pos, bufsz - pos, "auto_corr_ofdm:\t\t\t %u\n",
		      data->auto_corr_ofdm);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, "auto_corr_ofdm_mrc:\t\t %u\n",
		      data->auto_corr_ofdm_mrc);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, "auto_corr_ofdm_x1:\t\t %u\n",
		      data->auto_corr_ofdm_x1);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, "auto_corr_ofdm_mrc_x1:\t\t %u\n",
		      data->auto_corr_ofdm_mrc_x1);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, "auto_corr_cck:\t\t\t %u\n",
		      data->auto_corr_cck);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, "auto_corr_cck_mrc:\t\t %u\n",
		      data->auto_corr_cck_mrc);
	pos +=
	    scnprintf(buf + pos, bufsz - pos,
		      "last_bad_plcp_cnt_ofdm:\t\t %u\n",
		      data->last_bad_plcp_cnt_ofdm);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, "last_fa_cnt_ofdm:\t\t %u\n",
		      data->last_fa_cnt_ofdm);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, "last_bad_plcp_cnt_cck:\t\t %u\n",
		      data->last_bad_plcp_cnt_cck);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, "last_fa_cnt_cck:\t\t %u\n",
		      data->last_fa_cnt_cck);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, "nrg_curr_state:\t\t\t %u\n",
		      data->nrg_curr_state);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, "nrg_prev_state:\t\t\t %u\n",
		      data->nrg_prev_state);
	pos += scnprintf(buf + pos, bufsz - pos, "nrg_value:\t\t\t");
	for (cnt = 0; cnt < 10; cnt++) {
		pos +=
		    scnprintf(buf + pos, bufsz - pos, " %u",
			      data->nrg_value[cnt]);
	}
	pos += scnprintf(buf + pos, bufsz - pos, "\n");
	pos += scnprintf(buf + pos, bufsz - pos, "nrg_silence_rssi:\t\t");
	for (cnt = 0; cnt < NRG_NUM_PREV_STAT_L; cnt++) {
		pos +=
		    scnprintf(buf + pos, bufsz - pos, " %u",
			      data->nrg_silence_rssi[cnt]);
	}
	pos += scnprintf(buf + pos, bufsz - pos, "\n");
	pos +=
	    scnprintf(buf + pos, bufsz - pos, "nrg_silence_ref:\t\t %u\n",
		      data->nrg_silence_ref);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, "nrg_energy_idx:\t\t\t %u\n",
		      data->nrg_energy_idx);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, "nrg_silence_idx:\t\t %u\n",
		      data->nrg_silence_idx);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, "nrg_th_cck:\t\t\t %u\n",
		      data->nrg_th_cck);
	pos +=
	    scnprintf(buf + pos, bufsz - pos,
		      "nrg_auto_corr_silence_diff:\t %u\n",
		      data->nrg_auto_corr_silence_diff);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, "num_in_cck_no_fa:\t\t %u\n",
		      data->num_in_cck_no_fa);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, "nrg_th_ofdm:\t\t\t %u\n",
		      data->nrg_th_ofdm);

	ret = simple_read_from_buffer(user_buf, count, ppos, buf, pos);
	kfree(buf);
	return ret;
}

static ssize_t
il_dbgfs_chain_noise_read(struct file *file, char __user *user_buf,
			  size_t count, loff_t *ppos)
{

	struct il_priv *il = file->private_data;
	int pos = 0;
	int cnt = 0;
	char *buf;
	int bufsz = sizeof(struct il_chain_noise_data) * 4 + 100;
	ssize_t ret;
	struct il_chain_noise_data *data;

	data = &il->chain_noise_data;
	buf = kzalloc(bufsz, GFP_KERNEL);
	if (!buf) {
		IL_ERR("Can not allocate Buffer\n");
		return -ENOMEM;
	}

	pos +=
	    scnprintf(buf + pos, bufsz - pos, "active_chains:\t\t\t %u\n",
		      data->active_chains);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, "chain_noise_a:\t\t\t %u\n",
		      data->chain_noise_a);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, "chain_noise_b:\t\t\t %u\n",
		      data->chain_noise_b);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, "chain_noise_c:\t\t\t %u\n",
		      data->chain_noise_c);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, "chain_signal_a:\t\t\t %u\n",
		      data->chain_signal_a);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, "chain_signal_b:\t\t\t %u\n",
		      data->chain_signal_b);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, "chain_signal_c:\t\t\t %u\n",
		      data->chain_signal_c);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, "beacon_count:\t\t\t %u\n",
		      data->beacon_count);

	pos += scnprintf(buf + pos, bufsz - pos, "disconn_array:\t\t\t");
	for (cnt = 0; cnt < NUM_RX_CHAINS; cnt++) {
		pos +=
		    scnprintf(buf + pos, bufsz - pos, " %u",
			      data->disconn_array[cnt]);
	}
	pos += scnprintf(buf + pos, bufsz - pos, "\n");
	pos += scnprintf(buf + pos, bufsz - pos, "delta_gain_code:\t\t");
	for (cnt = 0; cnt < NUM_RX_CHAINS; cnt++) {
		pos +=
		    scnprintf(buf + pos, bufsz - pos, " %u",
			      data->delta_gain_code[cnt]);
	}
	pos += scnprintf(buf + pos, bufsz - pos, "\n");
	pos +=
	    scnprintf(buf + pos, bufsz - pos, "radio_write:\t\t\t %u\n",
		      data->radio_write);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, "state:\t\t\t\t %u\n",
		      data->state);

	ret = simple_read_from_buffer(user_buf, count, ppos, buf, pos);
	kfree(buf);
	return ret;
}

static ssize_t
il_dbgfs_power_save_status_read(struct file *file, char __user *user_buf,
				size_t count, loff_t *ppos)
{
	struct il_priv *il = file->private_data;
	char buf[60];
	int pos = 0;
	const size_t bufsz = sizeof(buf);
	u32 pwrsave_status;

	pwrsave_status =
	    _il_rd(il, CSR_GP_CNTRL) & CSR_GP_REG_POWER_SAVE_STATUS_MSK;

	pos += scnprintf(buf + pos, bufsz - pos, "Power Save Status: ");
	pos +=
	    scnprintf(buf + pos, bufsz - pos, "%s\n",
		      (pwrsave_status == CSR_GP_REG_NO_POWER_SAVE) ? "none" :
		      (pwrsave_status == CSR_GP_REG_MAC_POWER_SAVE) ? "MAC" :
		      (pwrsave_status == CSR_GP_REG_PHY_POWER_SAVE) ? "PHY" :
		      "error");

	return simple_read_from_buffer(user_buf, count, ppos, buf, pos);
}

static ssize_t
il_dbgfs_clear_ucode_stats_write(struct file *file,
				 const char __user *user_buf, size_t count,
				 loff_t *ppos)
{
	struct il_priv *il = file->private_data;
	char buf[8];
	int buf_size;
	int clear;

	memset(buf, 0, sizeof(buf));
	buf_size = min(count, sizeof(buf) - 1);
	if (copy_from_user(buf, user_buf, buf_size))
		return -EFAULT;
	if (sscanf(buf, "%d", &clear) != 1)
		return -EFAULT;

	/* make request to uCode to retrieve stats information */
	mutex_lock(&il->mutex);
	il_send_stats_request(il, CMD_SYNC, true);
	mutex_unlock(&il->mutex);

	return count;
}

static ssize_t
il_dbgfs_rxon_flags_read(struct file *file, char __user *user_buf,
			 size_t count, loff_t *ppos)
{

	struct il_priv *il = file->private_data;
	int len = 0;
	char buf[20];

	len = sprintf(buf, "0x%04X\n", le32_to_cpu(il->active.flags));
	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static ssize_t
il_dbgfs_rxon_filter_flags_read(struct file *file, char __user *user_buf,
				size_t count, loff_t *ppos)
{

	struct il_priv *il = file->private_data;
	int len = 0;
	char buf[20];

	len =
	    sprintf(buf, "0x%04X\n", le32_to_cpu(il->active.filter_flags));
	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static ssize_t
il_dbgfs_fh_reg_read(struct file *file, char __user *user_buf, size_t count,
		     loff_t *ppos)
{
	struct il_priv *il = file->private_data;
	char *buf;
	int pos = 0;
	ssize_t ret = -EFAULT;

	if (il->ops->dump_fh) {
		ret = pos = il->ops->dump_fh(il, &buf, true);
		if (buf) {
			ret =
			    simple_read_from_buffer(user_buf, count, ppos, buf,
						    pos);
			kfree(buf);
		}
	}

	return ret;
}

static ssize_t
il_dbgfs_missed_beacon_read(struct file *file, char __user *user_buf,
			    size_t count, loff_t *ppos)
{

	struct il_priv *il = file->private_data;
	int pos = 0;
	char buf[12];
	const size_t bufsz = sizeof(buf);

	pos +=
	    scnprintf(buf + pos, bufsz - pos, "%d\n",
		      il->missed_beacon_threshold);

	return simple_read_from_buffer(user_buf, count, ppos, buf, pos);
}

static ssize_t
il_dbgfs_missed_beacon_write(struct file *file, const char __user *user_buf,
			     size_t count, loff_t *ppos)
{
	struct il_priv *il = file->private_data;
	char buf[8];
	int buf_size;
	int missed;

	memset(buf, 0, sizeof(buf));
	buf_size = min(count, sizeof(buf) - 1);
	if (copy_from_user(buf, user_buf, buf_size))
		return -EFAULT;
	if (sscanf(buf, "%d", &missed) != 1)
		return -EINVAL;

	if (missed < IL_MISSED_BEACON_THRESHOLD_MIN ||
	    missed > IL_MISSED_BEACON_THRESHOLD_MAX)
		il->missed_beacon_threshold = IL_MISSED_BEACON_THRESHOLD_DEF;
	else
		il->missed_beacon_threshold = missed;

	return count;
}

static ssize_t
il_dbgfs_force_reset_read(struct file *file, char __user *user_buf,
			  size_t count, loff_t *ppos)
{

	struct il_priv *il = file->private_data;
	int pos = 0;
	char buf[300];
	const size_t bufsz = sizeof(buf);
	struct il_force_reset *force_reset;

	force_reset = &il->force_reset;

	pos +=
	    scnprintf(buf + pos, bufsz - pos, "\tnumber of reset request: %d\n",
		      force_reset->reset_request_count);
	pos +=
	    scnprintf(buf + pos, bufsz - pos,
		      "\tnumber of reset request success: %d\n",
		      force_reset->reset_success_count);
	pos +=
	    scnprintf(buf + pos, bufsz - pos,
		      "\tnumber of reset request reject: %d\n",
		      force_reset->reset_reject_count);
	pos +=
	    scnprintf(buf + pos, bufsz - pos, "\treset duration: %lu\n",
		      force_reset->reset_duration);

	return simple_read_from_buffer(user_buf, count, ppos, buf, pos);
}

static ssize_t
il_dbgfs_force_reset_write(struct file *file, const char __user *user_buf,
			   size_t count, loff_t *ppos)
{

	int ret;
	struct il_priv *il = file->private_data;

	ret = il_force_reset(il, true);

	return ret ? ret : count;
}

static ssize_t
il_dbgfs_wd_timeout_write(struct file *file, const char __user *user_buf,
			  size_t count, loff_t *ppos)
{

	struct il_priv *il = file->private_data;
	char buf[8];
	int buf_size;
	int timeout;

	memset(buf, 0, sizeof(buf));
	buf_size = min(count, sizeof(buf) - 1);
	if (copy_from_user(buf, user_buf, buf_size))
		return -EFAULT;
	if (sscanf(buf, "%d", &timeout) != 1)
		return -EINVAL;
	if (timeout < 0 || timeout > IL_MAX_WD_TIMEOUT)
		timeout = IL_DEF_WD_TIMEOUT;

	il->cfg->wd_timeout = timeout;
	il_setup_watchdog(il);
	return count;
}

DEBUGFS_READ_FILE_OPS(rx_stats);
DEBUGFS_READ_FILE_OPS(tx_stats);
DEBUGFS_READ_FILE_OPS(rx_queue);
DEBUGFS_READ_FILE_OPS(tx_queue);
DEBUGFS_READ_FILE_OPS(ucode_rx_stats);
DEBUGFS_READ_FILE_OPS(ucode_tx_stats);
DEBUGFS_READ_FILE_OPS(ucode_general_stats);
DEBUGFS_READ_FILE_OPS(sensitivity);
DEBUGFS_READ_FILE_OPS(chain_noise);
DEBUGFS_READ_FILE_OPS(power_save_status);
DEBUGFS_WRITE_FILE_OPS(clear_ucode_stats);
DEBUGFS_WRITE_FILE_OPS(clear_traffic_stats);
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
void
il_dbgfs_register(struct il_priv *il, const char *name)
{
	struct dentry *phyd = il->hw->wiphy->debugfsdir;
	struct dentry *dir_drv, *dir_data, *dir_rf, *dir_debug;

	dir_drv = debugfs_create_dir(name, phyd);
	il->debugfs_dir = dir_drv;

	dir_data = debugfs_create_dir("data", dir_drv);
	dir_rf = debugfs_create_dir("rf", dir_drv);
	dir_debug = debugfs_create_dir("debug", dir_drv);

	DEBUGFS_ADD_FILE(nvm, dir_data, 0400);
	DEBUGFS_ADD_FILE(sram, dir_data, 0600);
	DEBUGFS_ADD_FILE(stations, dir_data, 0400);
	DEBUGFS_ADD_FILE(channels, dir_data, 0400);
	DEBUGFS_ADD_FILE(status, dir_data, 0400);
	DEBUGFS_ADD_FILE(interrupt, dir_data, 0600);
	DEBUGFS_ADD_FILE(qos, dir_data, 0400);
	DEBUGFS_ADD_FILE(disable_ht40, dir_data, 0600);
	DEBUGFS_ADD_FILE(rx_stats, dir_debug, 0400);
	DEBUGFS_ADD_FILE(tx_stats, dir_debug, 0400);
	DEBUGFS_ADD_FILE(rx_queue, dir_debug, 0400);
	DEBUGFS_ADD_FILE(tx_queue, dir_debug, 0400);
	DEBUGFS_ADD_FILE(power_save_status, dir_debug, 0400);
	DEBUGFS_ADD_FILE(clear_ucode_stats, dir_debug, 0200);
	DEBUGFS_ADD_FILE(clear_traffic_stats, dir_debug, 0200);
	DEBUGFS_ADD_FILE(fh_reg, dir_debug, 0400);
	DEBUGFS_ADD_FILE(missed_beacon, dir_debug, 0200);
	DEBUGFS_ADD_FILE(force_reset, dir_debug, 0600);
	DEBUGFS_ADD_FILE(ucode_rx_stats, dir_debug, 0400);
	DEBUGFS_ADD_FILE(ucode_tx_stats, dir_debug, 0400);
	DEBUGFS_ADD_FILE(ucode_general_stats, dir_debug, 0400);

	if (il->cfg->sensitivity_calib_by_driver)
		DEBUGFS_ADD_FILE(sensitivity, dir_debug, 0400);
	if (il->cfg->chain_noise_calib_by_driver)
		DEBUGFS_ADD_FILE(chain_noise, dir_debug, 0400);
	DEBUGFS_ADD_FILE(rxon_flags, dir_debug, 0200);
	DEBUGFS_ADD_FILE(rxon_filter_flags, dir_debug, 0200);
	DEBUGFS_ADD_FILE(wd_timeout, dir_debug, 0200);
	if (il->cfg->sensitivity_calib_by_driver)
		DEBUGFS_ADD_BOOL(disable_sensitivity, dir_rf,
				 &il->disable_sens_cal);
	if (il->cfg->chain_noise_calib_by_driver)
		DEBUGFS_ADD_BOOL(disable_chain_noise, dir_rf,
				 &il->disable_chain_noise_cal);
	DEBUGFS_ADD_BOOL(disable_tx_power, dir_rf, &il->disable_tx_power_cal);
}
EXPORT_SYMBOL(il_dbgfs_register);

/**
 * Remove the debugfs files and directories
 *
 */
void
il_dbgfs_unregister(struct il_priv *il)
{
	if (!il->debugfs_dir)
		return;

	debugfs_remove_recursive(il->debugfs_dir);
	il->debugfs_dir = NULL;
}
EXPORT_SYMBOL(il_dbgfs_unregister);
