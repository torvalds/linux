// SPDX-License-Identifier: GPL-2.0-only
/*
 * Debugfs interface.
 *
 * Copyright (c) 2017-2019, Silicon Laboratories, Inc.
 * Copyright (c) 2010, ST-Ericsson
 */
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/crc32.h>

#include "debug.h"
#include "wfx.h"
#include "sta.h"
#include "main.h"
#include "hif_tx.h"
#include "hif_tx_mib.h"

#define CREATE_TRACE_POINTS
#include "traces.h"

static const struct trace_print_flags hif_msg_print_map[] = {
	hif_msg_list,
};

static const struct trace_print_flags hif_mib_print_map[] = {
	hif_mib_list,
};

static const struct trace_print_flags wfx_reg_print_map[] = {
	wfx_reg_list,
};

static const char *get_symbol(unsigned long val,
		const struct trace_print_flags *symbol_array)
{
	int i;

	for (i = 0; symbol_array[i].mask != -1; i++) {
		if (val == symbol_array[i].mask)
			return symbol_array[i].name;
	}

	return "unknown";
}

const char *get_hif_name(unsigned long id)
{
	return get_symbol(id, hif_msg_print_map);
}

const char *get_mib_name(unsigned long id)
{
	return get_symbol(id, hif_mib_print_map);
}

const char *get_reg_name(unsigned long id)
{
	return get_symbol(id, wfx_reg_print_map);
}

static int wfx_counters_show(struct seq_file *seq, void *v)
{
	int ret, i;
	struct wfx_dev *wdev = seq->private;
	struct hif_mib_extended_count_table counters[3];

	for (i = 0; i < ARRAY_SIZE(counters); i++) {
		ret = hif_get_counters_table(wdev, i, counters + i);
		if (ret < 0)
			return ret;
		if (ret > 0)
			return -EIO;
	}

	seq_printf(seq, "%-24s %12s %12s %12s\n",
		   "", "global", "iface 0", "iface 1");

#define PUT_COUNTER(name) \
	seq_printf(seq, "%-24s %12d %12d %12d\n", #name, \
		   le32_to_cpu(counters[2].count_##name), \
		   le32_to_cpu(counters[0].count_##name), \
		   le32_to_cpu(counters[1].count_##name))

	PUT_COUNTER(tx_packets);
	PUT_COUNTER(tx_multicast_frames);
	PUT_COUNTER(tx_frames_success);
	PUT_COUNTER(tx_frame_failures);
	PUT_COUNTER(tx_frames_retried);
	PUT_COUNTER(tx_frames_multi_retried);

	PUT_COUNTER(rts_success);
	PUT_COUNTER(rts_failures);
	PUT_COUNTER(ack_failures);

	PUT_COUNTER(rx_packets);
	PUT_COUNTER(rx_frames_success);
	PUT_COUNTER(rx_packet_errors);
	PUT_COUNTER(plcp_errors);
	PUT_COUNTER(fcs_errors);
	PUT_COUNTER(rx_decryption_failures);
	PUT_COUNTER(rx_mic_failures);
	PUT_COUNTER(rx_no_key_failures);
	PUT_COUNTER(rx_frame_duplicates);
	PUT_COUNTER(rx_multicast_frames);
	PUT_COUNTER(rx_cmacicv_errors);
	PUT_COUNTER(rx_cmac_replays);
	PUT_COUNTER(rx_mgmt_ccmp_replays);

	PUT_COUNTER(rx_beacon);
	PUT_COUNTER(miss_beacon);

#undef PUT_COUNTER

	for (i = 0; i < ARRAY_SIZE(counters[0].reserved); i++)
		seq_printf(seq, "reserved[%02d]%12s %12d %12d %12d\n", i, "",
			   le32_to_cpu(counters[2].reserved[i]),
			   le32_to_cpu(counters[0].reserved[i]),
			   le32_to_cpu(counters[1].reserved[i]));

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(wfx_counters);

static const char * const channel_names[] = {
	[0] = "1M",
	[1] = "2M",
	[2] = "5.5M",
	[3] = "11M",
	/* Entries 4 and 5 does not exist */
	[6] = "6M",
	[7] = "9M",
	[8] = "12M",
	[9] = "18M",
	[10] = "24M",
	[11] = "36M",
	[12] = "48M",
	[13] = "54M",
	[14] = "MCS0",
	[15] = "MCS1",
	[16] = "MCS2",
	[17] = "MCS3",
	[18] = "MCS4",
	[19] = "MCS5",
	[20] = "MCS6",
	[21] = "MCS7",
};

static int wfx_rx_stats_show(struct seq_file *seq, void *v)
{
	struct wfx_dev *wdev = seq->private;
	struct hif_rx_stats *st = &wdev->rx_stats;
	int i;

	mutex_lock(&wdev->rx_stats_lock);
	seq_printf(seq, "Timestamp: %dus\n", st->date);
	seq_printf(seq, "Low power clock: frequency %uHz, external %s\n",
		   le32_to_cpu(st->pwr_clk_freq),
		   st->is_ext_pwr_clk ? "yes" : "no");
	seq_printf(seq,
		   "Num. of frames: %d, PER (x10e4): %d, Throughput: %dKbps/s\n",
		   st->nb_rx_frame, st->per_total, st->throughput);
	seq_puts(seq, "       Num. of      PER     RSSI      SNR      CFO\n");
	seq_puts(seq, "        frames  (x10e4)    (dBm)     (dB)    (kHz)\n");
	for (i = 0; i < ARRAY_SIZE(channel_names); i++) {
		if (channel_names[i])
			seq_printf(seq, "%5s %8d %8d %8d %8d %8d\n",
				   channel_names[i],
				   le32_to_cpu(st->nb_rx_by_rate[i]),
				   le16_to_cpu(st->per[i]),
				   (s16)le16_to_cpu(st->rssi[i]) / 100,
				   (s16)le16_to_cpu(st->snr[i]) / 100,
				   (s16)le16_to_cpu(st->cfo[i]));
	}
	mutex_unlock(&wdev->rx_stats_lock);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(wfx_rx_stats);

static int wfx_tx_power_loop_show(struct seq_file *seq, void *v)
{
	struct wfx_dev *wdev = seq->private;
	struct hif_tx_power_loop_info *st = &wdev->tx_power_loop_info;
	int tmp;

	mutex_lock(&wdev->tx_power_loop_info_lock);
	tmp = le16_to_cpu(st->tx_gain_dig);
	seq_printf(seq, "Tx gain digital: %d\n", tmp);
	tmp = le16_to_cpu(st->tx_gain_pa);
	seq_printf(seq, "Tx gain PA: %d\n", tmp);
	tmp = (s16)le16_to_cpu(st->target_pout);
	seq_printf(seq, "Target Pout: %d.%02d dBm\n", tmp / 4, (tmp % 4) * 25);
	tmp = (s16)le16_to_cpu(st->p_estimation);
	seq_printf(seq, "FEM Pout: %d.%02d dBm\n", tmp / 4, (tmp % 4) * 25);
	tmp = le16_to_cpu(st->vpdet);
	seq_printf(seq, "Vpdet: %d mV\n", tmp);
	seq_printf(seq, "Measure index: %d\n", st->measurement_index);
	mutex_unlock(&wdev->tx_power_loop_info_lock);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(wfx_tx_power_loop);

static ssize_t wfx_send_pds_write(struct file *file,
				  const char __user *user_buf,
				  size_t count, loff_t *ppos)
{
	struct wfx_dev *wdev = file->private_data;
	char *buf;
	int ret;

	if (*ppos != 0) {
		dev_dbg(wdev->dev, "PDS data must be written in one transaction");
		return -EBUSY;
	}
	buf = memdup_user(user_buf, count);
	if (IS_ERR(buf))
		return PTR_ERR(buf);
	*ppos = *ppos + count;
	ret = wfx_send_pds(wdev, buf, count);
	kfree(buf);
	if (ret < 0)
		return ret;
	return count;
}

static const struct file_operations wfx_send_pds_fops = {
	.open = simple_open,
	.write = wfx_send_pds_write,
};

static ssize_t wfx_burn_slk_key_write(struct file *file,
				      const char __user *user_buf,
				      size_t count, loff_t *ppos)
{
	struct wfx_dev *wdev = file->private_data;

	dev_info(wdev->dev, "this driver does not support secure link\n");
	return -EINVAL;
}

static const struct file_operations wfx_burn_slk_key_fops = {
	.open = simple_open,
	.write = wfx_burn_slk_key_write,
};

struct dbgfs_hif_msg {
	struct wfx_dev *wdev;
	struct completion complete;
	u8 reply[1024];
	int ret;
};

static ssize_t wfx_send_hif_msg_write(struct file *file,
				      const char __user *user_buf,
				      size_t count, loff_t *ppos)
{
	struct dbgfs_hif_msg *context = file->private_data;
	struct wfx_dev *wdev = context->wdev;
	struct hif_msg *request;

	if (completion_done(&context->complete)) {
		dev_dbg(wdev->dev, "read previous result before start a new one\n");
		return -EBUSY;
	}
	if (count < sizeof(struct hif_msg))
		return -EINVAL;

	// wfx_cmd_send() chekc that reply buffer is wide enough, but do not
	// return precise length read. User have to know how many bytes should
	// be read. Filling reply buffer with a memory pattern may help user.
	memset(context->reply, 0xFF, sizeof(context->reply));
	request = memdup_user(user_buf, count);
	if (IS_ERR(request))
		return PTR_ERR(request);
	if (le16_to_cpu(request->len) != count) {
		kfree(request);
		return -EINVAL;
	}
	context->ret = wfx_cmd_send(wdev, request, context->reply,
				    sizeof(context->reply), false);

	kfree(request);
	complete(&context->complete);
	return count;
}

static ssize_t wfx_send_hif_msg_read(struct file *file, char __user *user_buf,
				     size_t count, loff_t *ppos)
{
	struct dbgfs_hif_msg *context = file->private_data;
	int ret;

	if (count > sizeof(context->reply))
		return -EINVAL;
	ret = wait_for_completion_interruptible(&context->complete);
	if (ret)
		return ret;
	if (context->ret < 0)
		return context->ret;
	// Be carefull, write() is waiting for a full message while read()
	// only return a payload
	if (copy_to_user(user_buf, context->reply, count))
		return -EFAULT;

	return count;
}

static int wfx_send_hif_msg_open(struct inode *inode, struct file *file)
{
	struct dbgfs_hif_msg *context = kzalloc(sizeof(*context), GFP_KERNEL);

	if (!context)
		return -ENOMEM;
	context->wdev = inode->i_private;
	init_completion(&context->complete);
	file->private_data = context;
	return 0;
}

static int wfx_send_hif_msg_release(struct inode *inode, struct file *file)
{
	struct dbgfs_hif_msg *context = file->private_data;

	kfree(context);
	return 0;
}

static const struct file_operations wfx_send_hif_msg_fops = {
	.open = wfx_send_hif_msg_open,
	.release = wfx_send_hif_msg_release,
	.write = wfx_send_hif_msg_write,
	.read = wfx_send_hif_msg_read,
};

int wfx_debug_init(struct wfx_dev *wdev)
{
	struct dentry *d;

	d = debugfs_create_dir("wfx", wdev->hw->wiphy->debugfsdir);
	debugfs_create_file("counters", 0444, d, wdev, &wfx_counters_fops);
	debugfs_create_file("rx_stats", 0444, d, wdev, &wfx_rx_stats_fops);
	debugfs_create_file("tx_power_loop", 0444, d, wdev,
			    &wfx_tx_power_loop_fops);
	debugfs_create_file("send_pds", 0200, d, wdev, &wfx_send_pds_fops);
	debugfs_create_file("burn_slk_key", 0200, d, wdev,
			    &wfx_burn_slk_key_fops);
	debugfs_create_file("send_hif_msg", 0600, d, wdev,
			    &wfx_send_hif_msg_fops);

	return 0;
}
