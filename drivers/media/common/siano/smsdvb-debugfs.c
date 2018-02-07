// SPDX-License-Identifier: GPL-2.0+
//
// Copyright(c) 2013 Mauro Carvalho Chehab

#include "smscoreapi.h"

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/debugfs.h>
#include <linux/spinlock.h>
#include <linux/usb.h>

#include <media/dmxdev.h>
#include <media/dvbdev.h>
#include <media/dvb_demux.h>
#include <media/dvb_frontend.h>

#include "smsdvb.h"

static struct dentry *smsdvb_debugfs_usb_root;

struct smsdvb_debugfs {
	struct kref		refcount;
	spinlock_t		lock;

	char			stats_data[PAGE_SIZE];
	unsigned		stats_count;
	bool			stats_was_read;

	wait_queue_head_t	stats_queue;
};

static void smsdvb_print_dvb_stats(struct smsdvb_debugfs *debug_data,
			    struct sms_stats *p)
{
	int n = 0;
	char *buf;

	spin_lock(&debug_data->lock);
	if (debug_data->stats_count) {
		spin_unlock(&debug_data->lock);
		return;
	}

	buf = debug_data->stats_data;

	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "is_rf_locked = %d\n", p->is_rf_locked);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "is_demod_locked = %d\n", p->is_demod_locked);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "is_external_lna_on = %d\n", p->is_external_lna_on);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "SNR = %d\n", p->SNR);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "ber = %d\n", p->ber);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "FIB_CRC = %d\n", p->FIB_CRC);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "ts_per = %d\n", p->ts_per);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "MFER = %d\n", p->MFER);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "RSSI = %d\n", p->RSSI);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "in_band_pwr = %d\n", p->in_band_pwr);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "carrier_offset = %d\n", p->carrier_offset);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "modem_state = %d\n", p->modem_state);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "frequency = %d\n", p->frequency);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "bandwidth = %d\n", p->bandwidth);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "transmission_mode = %d\n", p->transmission_mode);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "modem_state = %d\n", p->modem_state);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "guard_interval = %d\n", p->guard_interval);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "code_rate = %d\n", p->code_rate);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "lp_code_rate = %d\n", p->lp_code_rate);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "hierarchy = %d\n", p->hierarchy);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "constellation = %d\n", p->constellation);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "burst_size = %d\n", p->burst_size);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "burst_duration = %d\n", p->burst_duration);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "burst_cycle_time = %d\n", p->burst_cycle_time);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "calc_burst_cycle_time = %d\n",
		      p->calc_burst_cycle_time);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "num_of_rows = %d\n", p->num_of_rows);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "num_of_padd_cols = %d\n", p->num_of_padd_cols);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "num_of_punct_cols = %d\n", p->num_of_punct_cols);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "error_ts_packets = %d\n", p->error_ts_packets);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "total_ts_packets = %d\n", p->total_ts_packets);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "num_of_valid_mpe_tlbs = %d\n", p->num_of_valid_mpe_tlbs);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "num_of_invalid_mpe_tlbs = %d\n", p->num_of_invalid_mpe_tlbs);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "num_of_corrected_mpe_tlbs = %d\n", p->num_of_corrected_mpe_tlbs);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "ber_error_count = %d\n", p->ber_error_count);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "ber_bit_count = %d\n", p->ber_bit_count);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "sms_to_host_tx_errors = %d\n", p->sms_to_host_tx_errors);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "pre_ber = %d\n", p->pre_ber);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "cell_id = %d\n", p->cell_id);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "dvbh_srv_ind_hp = %d\n", p->dvbh_srv_ind_hp);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "dvbh_srv_ind_lp = %d\n", p->dvbh_srv_ind_lp);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "num_mpe_received = %d\n", p->num_mpe_received);

	debug_data->stats_count = n;
	spin_unlock(&debug_data->lock);
	wake_up(&debug_data->stats_queue);
}

static void smsdvb_print_isdb_stats(struct smsdvb_debugfs *debug_data,
			     struct sms_isdbt_stats *p)
{
	int i, n = 0;
	char *buf;

	spin_lock(&debug_data->lock);
	if (debug_data->stats_count) {
		spin_unlock(&debug_data->lock);
		return;
	}

	buf = debug_data->stats_data;

	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "statistics_type = %d\t", p->statistics_type);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "full_size = %d\n", p->full_size);

	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "is_rf_locked = %d\t\t", p->is_rf_locked);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "is_demod_locked = %d\t", p->is_demod_locked);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "is_external_lna_on = %d\n", p->is_external_lna_on);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "SNR = %d dB\t\t", p->SNR);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "RSSI = %d dBm\t\t", p->RSSI);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "in_band_pwr = %d dBm\n", p->in_band_pwr);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "carrier_offset = %d\t", p->carrier_offset);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "bandwidth = %d\t\t", p->bandwidth);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "frequency = %d Hz\n", p->frequency);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "transmission_mode = %d\t", p->transmission_mode);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "modem_state = %d\t\t", p->modem_state);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "guard_interval = %d\n", p->guard_interval);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "system_type = %d\t\t", p->system_type);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "partial_reception = %d\t", p->partial_reception);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "num_of_layers = %d\n", p->num_of_layers);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "sms_to_host_tx_errors = %d\n", p->sms_to_host_tx_errors);

	for (i = 0; i < 3; i++) {
		if (p->layer_info[i].number_of_segments < 1 ||
		    p->layer_info[i].number_of_segments > 13)
			continue;

		n += snprintf(&buf[n], PAGE_SIZE - n, "\nLayer %d\n", i);
		n += snprintf(&buf[n], PAGE_SIZE - n, "\tcode_rate = %d\t",
			      p->layer_info[i].code_rate);
		n += snprintf(&buf[n], PAGE_SIZE - n, "constellation = %d\n",
			      p->layer_info[i].constellation);
		n += snprintf(&buf[n], PAGE_SIZE - n, "\tber = %-5d\t",
			      p->layer_info[i].ber);
		n += snprintf(&buf[n], PAGE_SIZE - n, "\tber_error_count = %-5d\t",
			      p->layer_info[i].ber_error_count);
		n += snprintf(&buf[n], PAGE_SIZE - n, "ber_bit_count = %-5d\n",
			      p->layer_info[i].ber_bit_count);
		n += snprintf(&buf[n], PAGE_SIZE - n, "\tpre_ber = %-5d\t",
			      p->layer_info[i].pre_ber);
		n += snprintf(&buf[n], PAGE_SIZE - n, "\tts_per = %-5d\n",
			      p->layer_info[i].ts_per);
		n += snprintf(&buf[n], PAGE_SIZE - n, "\terror_ts_packets = %-5d\t",
			      p->layer_info[i].error_ts_packets);
		n += snprintf(&buf[n], PAGE_SIZE - n, "total_ts_packets = %-5d\t",
			      p->layer_info[i].total_ts_packets);
		n += snprintf(&buf[n], PAGE_SIZE - n, "ti_ldepth_i = %d\n",
			      p->layer_info[i].ti_ldepth_i);
		n += snprintf(&buf[n], PAGE_SIZE - n,
			      "\tnumber_of_segments = %d\t",
			      p->layer_info[i].number_of_segments);
		n += snprintf(&buf[n], PAGE_SIZE - n, "tmcc_errors = %d\n",
			      p->layer_info[i].tmcc_errors);
	}

	debug_data->stats_count = n;
	spin_unlock(&debug_data->lock);
	wake_up(&debug_data->stats_queue);
}

static void smsdvb_print_isdb_stats_ex(struct smsdvb_debugfs *debug_data,
				struct sms_isdbt_stats_ex *p)
{
	int i, n = 0;
	char *buf;

	spin_lock(&debug_data->lock);
	if (debug_data->stats_count) {
		spin_unlock(&debug_data->lock);
		return;
	}

	buf = debug_data->stats_data;

	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "statistics_type = %d\t", p->statistics_type);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "full_size = %d\n", p->full_size);

	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "is_rf_locked = %d\t\t", p->is_rf_locked);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "is_demod_locked = %d\t", p->is_demod_locked);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "is_external_lna_on = %d\n", p->is_external_lna_on);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "SNR = %d dB\t\t", p->SNR);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "RSSI = %d dBm\t\t", p->RSSI);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "in_band_pwr = %d dBm\n", p->in_band_pwr);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "carrier_offset = %d\t", p->carrier_offset);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "bandwidth = %d\t\t", p->bandwidth);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "frequency = %d Hz\n", p->frequency);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "transmission_mode = %d\t", p->transmission_mode);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "modem_state = %d\t\t", p->modem_state);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "guard_interval = %d\n", p->guard_interval);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "system_type = %d\t\t", p->system_type);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "partial_reception = %d\t", p->partial_reception);
	n += snprintf(&buf[n], PAGE_SIZE - n,
		      "num_of_layers = %d\n", p->num_of_layers);
	n += snprintf(&buf[n], PAGE_SIZE - n, "segment_number = %d\t",
		      p->segment_number);
	n += snprintf(&buf[n], PAGE_SIZE - n, "tune_bw = %d\n",
		      p->tune_bw);

	for (i = 0; i < 3; i++) {
		if (p->layer_info[i].number_of_segments < 1 ||
		    p->layer_info[i].number_of_segments > 13)
			continue;

		n += snprintf(&buf[n], PAGE_SIZE - n, "\nLayer %d\n", i);
		n += snprintf(&buf[n], PAGE_SIZE - n, "\tcode_rate = %d\t",
			      p->layer_info[i].code_rate);
		n += snprintf(&buf[n], PAGE_SIZE - n, "constellation = %d\n",
			      p->layer_info[i].constellation);
		n += snprintf(&buf[n], PAGE_SIZE - n, "\tber = %-5d\t",
			      p->layer_info[i].ber);
		n += snprintf(&buf[n], PAGE_SIZE - n, "\tber_error_count = %-5d\t",
			      p->layer_info[i].ber_error_count);
		n += snprintf(&buf[n], PAGE_SIZE - n, "ber_bit_count = %-5d\n",
			      p->layer_info[i].ber_bit_count);
		n += snprintf(&buf[n], PAGE_SIZE - n, "\tpre_ber = %-5d\t",
			      p->layer_info[i].pre_ber);
		n += snprintf(&buf[n], PAGE_SIZE - n, "\tts_per = %-5d\n",
			      p->layer_info[i].ts_per);
		n += snprintf(&buf[n], PAGE_SIZE - n, "\terror_ts_packets = %-5d\t",
			      p->layer_info[i].error_ts_packets);
		n += snprintf(&buf[n], PAGE_SIZE - n, "total_ts_packets = %-5d\t",
			      p->layer_info[i].total_ts_packets);
		n += snprintf(&buf[n], PAGE_SIZE - n, "ti_ldepth_i = %d\n",
			      p->layer_info[i].ti_ldepth_i);
		n += snprintf(&buf[n], PAGE_SIZE - n,
			      "\tnumber_of_segments = %d\t",
			      p->layer_info[i].number_of_segments);
		n += snprintf(&buf[n], PAGE_SIZE - n, "tmcc_errors = %d\n",
			      p->layer_info[i].tmcc_errors);
	}


	debug_data->stats_count = n;
	spin_unlock(&debug_data->lock);

	wake_up(&debug_data->stats_queue);
}

static int smsdvb_stats_open(struct inode *inode, struct file *file)
{
	struct smsdvb_client_t *client = inode->i_private;
	struct smsdvb_debugfs *debug_data = client->debug_data;

	kref_get(&debug_data->refcount);

	spin_lock(&debug_data->lock);
	debug_data->stats_count = 0;
	debug_data->stats_was_read = false;
	spin_unlock(&debug_data->lock);

	file->private_data = debug_data;

	return 0;
}

static void smsdvb_debugfs_data_release(struct kref *ref)
{
	struct smsdvb_debugfs *debug_data;

	debug_data = container_of(ref, struct smsdvb_debugfs, refcount);
	kfree(debug_data);
}

static int smsdvb_stats_wait_read(struct smsdvb_debugfs *debug_data)
{
	int rc = 1;

	spin_lock(&debug_data->lock);

	if (debug_data->stats_was_read)
		goto exit;

	rc = debug_data->stats_count;

exit:
	spin_unlock(&debug_data->lock);
	return rc;
}

static __poll_t smsdvb_stats_poll(struct file *file, poll_table *wait)
{
	struct smsdvb_debugfs *debug_data = file->private_data;
	int rc;

	kref_get(&debug_data->refcount);

	poll_wait(file, &debug_data->stats_queue, wait);

	rc = smsdvb_stats_wait_read(debug_data);
	kref_put(&debug_data->refcount, smsdvb_debugfs_data_release);

	return rc > 0 ? POLLIN | POLLRDNORM : 0;
}

static ssize_t smsdvb_stats_read(struct file *file, char __user *user_buf,
				      size_t nbytes, loff_t *ppos)
{
	int rc = 0, len;
	struct smsdvb_debugfs *debug_data = file->private_data;

	kref_get(&debug_data->refcount);

	if (file->f_flags & O_NONBLOCK) {
		rc = smsdvb_stats_wait_read(debug_data);
		if (!rc) {
			rc = -EWOULDBLOCK;
			goto ret;
		}
	} else {
		rc = wait_event_interruptible(debug_data->stats_queue,
				      smsdvb_stats_wait_read(debug_data));
		if (rc < 0)
			goto ret;
	}

	if (debug_data->stats_was_read) {
		rc = 0;	/* EOF */
		goto ret;
	}

	len = debug_data->stats_count - *ppos;
	if (len >= 0)
		rc = simple_read_from_buffer(user_buf, nbytes, ppos,
					     debug_data->stats_data, len);
	else
		rc = 0;

	if (*ppos >= debug_data->stats_count) {
		spin_lock(&debug_data->lock);
		debug_data->stats_was_read = true;
		spin_unlock(&debug_data->lock);
	}
ret:
	kref_put(&debug_data->refcount, smsdvb_debugfs_data_release);
	return rc;
}

static int smsdvb_stats_release(struct inode *inode, struct file *file)
{
	struct smsdvb_debugfs *debug_data = file->private_data;

	spin_lock(&debug_data->lock);
	debug_data->stats_was_read = true;	/* return EOF to read() */
	spin_unlock(&debug_data->lock);
	wake_up_interruptible_sync(&debug_data->stats_queue);

	kref_put(&debug_data->refcount, smsdvb_debugfs_data_release);
	file->private_data = NULL;

	return 0;
}

static const struct file_operations debugfs_stats_ops = {
	.open = smsdvb_stats_open,
	.poll = smsdvb_stats_poll,
	.read = smsdvb_stats_read,
	.release = smsdvb_stats_release,
	.llseek = generic_file_llseek,
};

/*
 * Functions used by smsdvb, in order to create the interfaces
 */

int smsdvb_debugfs_create(struct smsdvb_client_t *client)
{
	struct smscore_device_t *coredev = client->coredev;
	struct dentry *d;
	struct smsdvb_debugfs *debug_data;

	if (!smsdvb_debugfs_usb_root || !coredev->is_usb_device)
		return -ENODEV;

	client->debugfs = debugfs_create_dir(coredev->devpath,
					     smsdvb_debugfs_usb_root);
	if (IS_ERR_OR_NULL(client->debugfs)) {
		pr_info("Unable to create debugfs %s directory.\n",
			coredev->devpath);
		return -ENODEV;
	}

	d = debugfs_create_file("stats", S_IRUGO | S_IWUSR, client->debugfs,
				client, &debugfs_stats_ops);
	if (!d) {
		debugfs_remove(client->debugfs);
		return -ENOMEM;
	}

	debug_data = kzalloc(sizeof(*client->debug_data), GFP_KERNEL);
	if (!debug_data)
		return -ENOMEM;

	client->debug_data        = debug_data;
	client->prt_dvb_stats     = smsdvb_print_dvb_stats;
	client->prt_isdb_stats    = smsdvb_print_isdb_stats;
	client->prt_isdb_stats_ex = smsdvb_print_isdb_stats_ex;

	init_waitqueue_head(&debug_data->stats_queue);
	spin_lock_init(&debug_data->lock);
	kref_init(&debug_data->refcount);

	return 0;
}

void smsdvb_debugfs_release(struct smsdvb_client_t *client)
{
	if (!client->debugfs)
		return;

	client->prt_dvb_stats     = NULL;
	client->prt_isdb_stats    = NULL;
	client->prt_isdb_stats_ex = NULL;

	debugfs_remove_recursive(client->debugfs);
	kref_put(&client->debug_data->refcount, smsdvb_debugfs_data_release);

	client->debug_data = NULL;
	client->debugfs = NULL;
}

int smsdvb_debugfs_register(void)
{
	struct dentry *d;

	/*
	 * FIXME: This was written to debug Siano USB devices. So, it creates
	 * the debugfs node under <debugfs>/usb.
	 * A similar logic would be needed for Siano sdio devices, but, in that
	 * case, usb_debug_root is not a good choice.
	 *
	 * Perhaps the right fix here would be to create another sysfs root
	 * node for sdio-based boards, but this may need some logic at sdio
	 * subsystem.
	 */
	d = debugfs_create_dir("smsdvb", usb_debug_root);
	if (IS_ERR_OR_NULL(d)) {
		pr_err("Couldn't create sysfs node for smsdvb\n");
		return PTR_ERR(d);
	} else {
		smsdvb_debugfs_usb_root = d;
	}
	return 0;
}

void smsdvb_debugfs_unregister(void)
{
	debugfs_remove_recursive(smsdvb_debugfs_usb_root);
	smsdvb_debugfs_usb_root = NULL;
}
