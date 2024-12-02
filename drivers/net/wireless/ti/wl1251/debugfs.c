// SPDX-License-Identifier: GPL-2.0-only
/*
 * This file is part of wl1251
 *
 * Copyright (C) 2009 Nokia Corporation
 */

#include "debugfs.h"

#include <linux/skbuff.h>
#include <linux/slab.h>

#include "wl1251.h"
#include "acx.h"
#include "ps.h"

/* ms */
#define WL1251_DEBUGFS_STATS_LIFETIME 1000

/* debugfs macros idea from mac80211 */

#define DEBUGFS_READONLY_FILE(name, buflen, fmt, value...)		\
static ssize_t name## _read(struct file *file, char __user *userbuf,	\
			    size_t count, loff_t *ppos)			\
{									\
	struct wl1251 *wl = file->private_data;				\
	char buf[buflen];						\
	int res;							\
									\
	res = scnprintf(buf, buflen, fmt "\n", ##value);		\
	return simple_read_from_buffer(userbuf, count, ppos, buf, res);	\
}									\
									\
static const struct file_operations name## _ops = {			\
	.read = name## _read,						\
	.open = simple_open,						\
	.llseek	= generic_file_llseek,					\
};

#define DEBUGFS_ADD(name, parent)					\
	wl->debugfs.name = debugfs_create_file(#name, 0400, parent,	\
					       wl, &name## _ops)	\

#define DEBUGFS_DEL(name)						\
	do {								\
		debugfs_remove(wl->debugfs.name);			\
		wl->debugfs.name = NULL;				\
	} while (0)

#define DEBUGFS_FWSTATS_FILE(sub, name, buflen, fmt)			\
static ssize_t sub## _ ##name## _read(struct file *file,		\
				      char __user *userbuf,		\
				      size_t count, loff_t *ppos)	\
{									\
	struct wl1251 *wl = file->private_data;				\
	char buf[buflen];						\
	int res;							\
									\
	wl1251_debugfs_update_stats(wl);				\
									\
	res = scnprintf(buf, buflen, fmt "\n",				\
			wl->stats.fw_stats->sub.name);			\
	return simple_read_from_buffer(userbuf, count, ppos, buf, res);	\
}									\
									\
static const struct file_operations sub## _ ##name## _ops = {		\
	.read = sub## _ ##name## _read,					\
	.open = simple_open,						\
	.llseek	= generic_file_llseek,					\
};

#define DEBUGFS_FWSTATS_ADD(sub, name)				\
	DEBUGFS_ADD(sub## _ ##name, wl->debugfs.fw_statistics)

#define DEBUGFS_FWSTATS_DEL(sub, name)				\
	DEBUGFS_DEL(sub## _ ##name)

static void wl1251_debugfs_update_stats(struct wl1251 *wl)
{
	int ret;

	mutex_lock(&wl->mutex);

	ret = wl1251_ps_elp_wakeup(wl);
	if (ret < 0)
		goto out;

	if (wl->state == WL1251_STATE_ON &&
	    time_after(jiffies, wl->stats.fw_stats_update +
		       msecs_to_jiffies(WL1251_DEBUGFS_STATS_LIFETIME))) {
		wl1251_acx_statistics(wl, wl->stats.fw_stats);
		wl->stats.fw_stats_update = jiffies;
	}

	wl1251_ps_elp_sleep(wl);

out:
	mutex_unlock(&wl->mutex);
}

DEBUGFS_FWSTATS_FILE(tx, internal_desc_overflow, 20, "%u");

DEBUGFS_FWSTATS_FILE(rx, out_of_mem, 20, "%u");
DEBUGFS_FWSTATS_FILE(rx, hdr_overflow, 20, "%u");
DEBUGFS_FWSTATS_FILE(rx, hw_stuck, 20, "%u");
DEBUGFS_FWSTATS_FILE(rx, dropped, 20, "%u");
DEBUGFS_FWSTATS_FILE(rx, fcs_err, 20, "%u");
DEBUGFS_FWSTATS_FILE(rx, xfr_hint_trig, 20, "%u");
DEBUGFS_FWSTATS_FILE(rx, path_reset, 20, "%u");
DEBUGFS_FWSTATS_FILE(rx, reset_counter, 20, "%u");

DEBUGFS_FWSTATS_FILE(dma, rx_requested, 20, "%u");
DEBUGFS_FWSTATS_FILE(dma, rx_errors, 20, "%u");
DEBUGFS_FWSTATS_FILE(dma, tx_requested, 20, "%u");
DEBUGFS_FWSTATS_FILE(dma, tx_errors, 20, "%u");

DEBUGFS_FWSTATS_FILE(isr, cmd_cmplt, 20, "%u");
DEBUGFS_FWSTATS_FILE(isr, fiqs, 20, "%u");
DEBUGFS_FWSTATS_FILE(isr, rx_headers, 20, "%u");
DEBUGFS_FWSTATS_FILE(isr, rx_mem_overflow, 20, "%u");
DEBUGFS_FWSTATS_FILE(isr, rx_rdys, 20, "%u");
DEBUGFS_FWSTATS_FILE(isr, irqs, 20, "%u");
DEBUGFS_FWSTATS_FILE(isr, tx_procs, 20, "%u");
DEBUGFS_FWSTATS_FILE(isr, decrypt_done, 20, "%u");
DEBUGFS_FWSTATS_FILE(isr, dma0_done, 20, "%u");
DEBUGFS_FWSTATS_FILE(isr, dma1_done, 20, "%u");
DEBUGFS_FWSTATS_FILE(isr, tx_exch_complete, 20, "%u");
DEBUGFS_FWSTATS_FILE(isr, commands, 20, "%u");
DEBUGFS_FWSTATS_FILE(isr, rx_procs, 20, "%u");
DEBUGFS_FWSTATS_FILE(isr, hw_pm_mode_changes, 20, "%u");
DEBUGFS_FWSTATS_FILE(isr, host_acknowledges, 20, "%u");
DEBUGFS_FWSTATS_FILE(isr, pci_pm, 20, "%u");
DEBUGFS_FWSTATS_FILE(isr, wakeups, 20, "%u");
DEBUGFS_FWSTATS_FILE(isr, low_rssi, 20, "%u");

DEBUGFS_FWSTATS_FILE(wep, addr_key_count, 20, "%u");
DEBUGFS_FWSTATS_FILE(wep, default_key_count, 20, "%u");
/* skipping wep.reserved */
DEBUGFS_FWSTATS_FILE(wep, key_not_found, 20, "%u");
DEBUGFS_FWSTATS_FILE(wep, decrypt_fail, 20, "%u");
DEBUGFS_FWSTATS_FILE(wep, packets, 20, "%u");
DEBUGFS_FWSTATS_FILE(wep, interrupt, 20, "%u");

DEBUGFS_FWSTATS_FILE(pwr, ps_enter, 20, "%u");
DEBUGFS_FWSTATS_FILE(pwr, elp_enter, 20, "%u");
DEBUGFS_FWSTATS_FILE(pwr, missing_bcns, 20, "%u");
DEBUGFS_FWSTATS_FILE(pwr, wake_on_host, 20, "%u");
DEBUGFS_FWSTATS_FILE(pwr, wake_on_timer_exp, 20, "%u");
DEBUGFS_FWSTATS_FILE(pwr, tx_with_ps, 20, "%u");
DEBUGFS_FWSTATS_FILE(pwr, tx_without_ps, 20, "%u");
DEBUGFS_FWSTATS_FILE(pwr, rcvd_beacons, 20, "%u");
DEBUGFS_FWSTATS_FILE(pwr, power_save_off, 20, "%u");
DEBUGFS_FWSTATS_FILE(pwr, enable_ps, 20, "%u");
DEBUGFS_FWSTATS_FILE(pwr, disable_ps, 20, "%u");
DEBUGFS_FWSTATS_FILE(pwr, fix_tsf_ps, 20, "%u");
/* skipping cont_miss_bcns_spread for now */
DEBUGFS_FWSTATS_FILE(pwr, rcvd_awake_beacons, 20, "%u");

DEBUGFS_FWSTATS_FILE(mic, rx_pkts, 20, "%u");
DEBUGFS_FWSTATS_FILE(mic, calc_failure, 20, "%u");

DEBUGFS_FWSTATS_FILE(aes, encrypt_fail, 20, "%u");
DEBUGFS_FWSTATS_FILE(aes, decrypt_fail, 20, "%u");
DEBUGFS_FWSTATS_FILE(aes, encrypt_packets, 20, "%u");
DEBUGFS_FWSTATS_FILE(aes, decrypt_packets, 20, "%u");
DEBUGFS_FWSTATS_FILE(aes, encrypt_interrupt, 20, "%u");
DEBUGFS_FWSTATS_FILE(aes, decrypt_interrupt, 20, "%u");

DEBUGFS_FWSTATS_FILE(event, heart_beat, 20, "%u");
DEBUGFS_FWSTATS_FILE(event, calibration, 20, "%u");
DEBUGFS_FWSTATS_FILE(event, rx_mismatch, 20, "%u");
DEBUGFS_FWSTATS_FILE(event, rx_mem_empty, 20, "%u");
DEBUGFS_FWSTATS_FILE(event, rx_pool, 20, "%u");
DEBUGFS_FWSTATS_FILE(event, oom_late, 20, "%u");
DEBUGFS_FWSTATS_FILE(event, phy_transmit_error, 20, "%u");
DEBUGFS_FWSTATS_FILE(event, tx_stuck, 20, "%u");

DEBUGFS_FWSTATS_FILE(ps, pspoll_timeouts, 20, "%u");
DEBUGFS_FWSTATS_FILE(ps, upsd_timeouts, 20, "%u");
DEBUGFS_FWSTATS_FILE(ps, upsd_max_sptime, 20, "%u");
DEBUGFS_FWSTATS_FILE(ps, upsd_max_apturn, 20, "%u");
DEBUGFS_FWSTATS_FILE(ps, pspoll_max_apturn, 20, "%u");
DEBUGFS_FWSTATS_FILE(ps, pspoll_utilization, 20, "%u");
DEBUGFS_FWSTATS_FILE(ps, upsd_utilization, 20, "%u");

DEBUGFS_FWSTATS_FILE(rxpipe, rx_prep_beacon_drop, 20, "%u");
DEBUGFS_FWSTATS_FILE(rxpipe, descr_host_int_trig_rx_data, 20, "%u");
DEBUGFS_FWSTATS_FILE(rxpipe, beacon_buffer_thres_host_int_trig_rx_data,
		     20, "%u");
DEBUGFS_FWSTATS_FILE(rxpipe, missed_beacon_host_int_trig_rx_data, 20, "%u");
DEBUGFS_FWSTATS_FILE(rxpipe, tx_xfr_host_int_trig_rx_data, 20, "%u");

DEBUGFS_READONLY_FILE(retry_count, 20, "%u", wl->stats.retry_count);
DEBUGFS_READONLY_FILE(excessive_retries, 20, "%u",
		      wl->stats.excessive_retries);

static ssize_t tx_queue_len_read(struct file *file, char __user *userbuf,
				 size_t count, loff_t *ppos)
{
	struct wl1251 *wl = file->private_data;
	u32 queue_len;
	char buf[20];
	int res;

	queue_len = skb_queue_len(&wl->tx_queue);

	res = scnprintf(buf, sizeof(buf), "%u\n", queue_len);
	return simple_read_from_buffer(userbuf, count, ppos, buf, res);
}

static const struct file_operations tx_queue_len_ops = {
	.read = tx_queue_len_read,
	.open = simple_open,
	.llseek = generic_file_llseek,
};

static ssize_t tx_queue_status_read(struct file *file, char __user *userbuf,
				    size_t count, loff_t *ppos)
{
	struct wl1251 *wl = file->private_data;
	char buf[3], status;
	int len;

	if (wl->tx_queue_stopped)
		status = 's';
	else
		status = 'r';

	len = scnprintf(buf, sizeof(buf), "%c\n", status);
	return simple_read_from_buffer(userbuf, count, ppos, buf, len);
}

static const struct file_operations tx_queue_status_ops = {
	.read = tx_queue_status_read,
	.open = simple_open,
	.llseek = generic_file_llseek,
};

static void wl1251_debugfs_delete_files(struct wl1251 *wl)
{
	DEBUGFS_FWSTATS_DEL(tx, internal_desc_overflow);

	DEBUGFS_FWSTATS_DEL(rx, out_of_mem);
	DEBUGFS_FWSTATS_DEL(rx, hdr_overflow);
	DEBUGFS_FWSTATS_DEL(rx, hw_stuck);
	DEBUGFS_FWSTATS_DEL(rx, dropped);
	DEBUGFS_FWSTATS_DEL(rx, fcs_err);
	DEBUGFS_FWSTATS_DEL(rx, xfr_hint_trig);
	DEBUGFS_FWSTATS_DEL(rx, path_reset);
	DEBUGFS_FWSTATS_DEL(rx, reset_counter);

	DEBUGFS_FWSTATS_DEL(dma, rx_requested);
	DEBUGFS_FWSTATS_DEL(dma, rx_errors);
	DEBUGFS_FWSTATS_DEL(dma, tx_requested);
	DEBUGFS_FWSTATS_DEL(dma, tx_errors);

	DEBUGFS_FWSTATS_DEL(isr, cmd_cmplt);
	DEBUGFS_FWSTATS_DEL(isr, fiqs);
	DEBUGFS_FWSTATS_DEL(isr, rx_headers);
	DEBUGFS_FWSTATS_DEL(isr, rx_mem_overflow);
	DEBUGFS_FWSTATS_DEL(isr, rx_rdys);
	DEBUGFS_FWSTATS_DEL(isr, irqs);
	DEBUGFS_FWSTATS_DEL(isr, tx_procs);
	DEBUGFS_FWSTATS_DEL(isr, decrypt_done);
	DEBUGFS_FWSTATS_DEL(isr, dma0_done);
	DEBUGFS_FWSTATS_DEL(isr, dma1_done);
	DEBUGFS_FWSTATS_DEL(isr, tx_exch_complete);
	DEBUGFS_FWSTATS_DEL(isr, commands);
	DEBUGFS_FWSTATS_DEL(isr, rx_procs);
	DEBUGFS_FWSTATS_DEL(isr, hw_pm_mode_changes);
	DEBUGFS_FWSTATS_DEL(isr, host_acknowledges);
	DEBUGFS_FWSTATS_DEL(isr, pci_pm);
	DEBUGFS_FWSTATS_DEL(isr, wakeups);
	DEBUGFS_FWSTATS_DEL(isr, low_rssi);

	DEBUGFS_FWSTATS_DEL(wep, addr_key_count);
	DEBUGFS_FWSTATS_DEL(wep, default_key_count);
	/* skipping wep.reserved */
	DEBUGFS_FWSTATS_DEL(wep, key_not_found);
	DEBUGFS_FWSTATS_DEL(wep, decrypt_fail);
	DEBUGFS_FWSTATS_DEL(wep, packets);
	DEBUGFS_FWSTATS_DEL(wep, interrupt);

	DEBUGFS_FWSTATS_DEL(pwr, ps_enter);
	DEBUGFS_FWSTATS_DEL(pwr, elp_enter);
	DEBUGFS_FWSTATS_DEL(pwr, missing_bcns);
	DEBUGFS_FWSTATS_DEL(pwr, wake_on_host);
	DEBUGFS_FWSTATS_DEL(pwr, wake_on_timer_exp);
	DEBUGFS_FWSTATS_DEL(pwr, tx_with_ps);
	DEBUGFS_FWSTATS_DEL(pwr, tx_without_ps);
	DEBUGFS_FWSTATS_DEL(pwr, rcvd_beacons);
	DEBUGFS_FWSTATS_DEL(pwr, power_save_off);
	DEBUGFS_FWSTATS_DEL(pwr, enable_ps);
	DEBUGFS_FWSTATS_DEL(pwr, disable_ps);
	DEBUGFS_FWSTATS_DEL(pwr, fix_tsf_ps);
	/* skipping cont_miss_bcns_spread for now */
	DEBUGFS_FWSTATS_DEL(pwr, rcvd_awake_beacons);

	DEBUGFS_FWSTATS_DEL(mic, rx_pkts);
	DEBUGFS_FWSTATS_DEL(mic, calc_failure);

	DEBUGFS_FWSTATS_DEL(aes, encrypt_fail);
	DEBUGFS_FWSTATS_DEL(aes, decrypt_fail);
	DEBUGFS_FWSTATS_DEL(aes, encrypt_packets);
	DEBUGFS_FWSTATS_DEL(aes, decrypt_packets);
	DEBUGFS_FWSTATS_DEL(aes, encrypt_interrupt);
	DEBUGFS_FWSTATS_DEL(aes, decrypt_interrupt);

	DEBUGFS_FWSTATS_DEL(event, heart_beat);
	DEBUGFS_FWSTATS_DEL(event, calibration);
	DEBUGFS_FWSTATS_DEL(event, rx_mismatch);
	DEBUGFS_FWSTATS_DEL(event, rx_mem_empty);
	DEBUGFS_FWSTATS_DEL(event, rx_pool);
	DEBUGFS_FWSTATS_DEL(event, oom_late);
	DEBUGFS_FWSTATS_DEL(event, phy_transmit_error);
	DEBUGFS_FWSTATS_DEL(event, tx_stuck);

	DEBUGFS_FWSTATS_DEL(ps, pspoll_timeouts);
	DEBUGFS_FWSTATS_DEL(ps, upsd_timeouts);
	DEBUGFS_FWSTATS_DEL(ps, upsd_max_sptime);
	DEBUGFS_FWSTATS_DEL(ps, upsd_max_apturn);
	DEBUGFS_FWSTATS_DEL(ps, pspoll_max_apturn);
	DEBUGFS_FWSTATS_DEL(ps, pspoll_utilization);
	DEBUGFS_FWSTATS_DEL(ps, upsd_utilization);

	DEBUGFS_FWSTATS_DEL(rxpipe, rx_prep_beacon_drop);
	DEBUGFS_FWSTATS_DEL(rxpipe, descr_host_int_trig_rx_data);
	DEBUGFS_FWSTATS_DEL(rxpipe, beacon_buffer_thres_host_int_trig_rx_data);
	DEBUGFS_FWSTATS_DEL(rxpipe, missed_beacon_host_int_trig_rx_data);
	DEBUGFS_FWSTATS_DEL(rxpipe, tx_xfr_host_int_trig_rx_data);

	DEBUGFS_DEL(tx_queue_len);
	DEBUGFS_DEL(tx_queue_status);
	DEBUGFS_DEL(retry_count);
	DEBUGFS_DEL(excessive_retries);
}

static void wl1251_debugfs_add_files(struct wl1251 *wl)
{
	DEBUGFS_FWSTATS_ADD(tx, internal_desc_overflow);

	DEBUGFS_FWSTATS_ADD(rx, out_of_mem);
	DEBUGFS_FWSTATS_ADD(rx, hdr_overflow);
	DEBUGFS_FWSTATS_ADD(rx, hw_stuck);
	DEBUGFS_FWSTATS_ADD(rx, dropped);
	DEBUGFS_FWSTATS_ADD(rx, fcs_err);
	DEBUGFS_FWSTATS_ADD(rx, xfr_hint_trig);
	DEBUGFS_FWSTATS_ADD(rx, path_reset);
	DEBUGFS_FWSTATS_ADD(rx, reset_counter);

	DEBUGFS_FWSTATS_ADD(dma, rx_requested);
	DEBUGFS_FWSTATS_ADD(dma, rx_errors);
	DEBUGFS_FWSTATS_ADD(dma, tx_requested);
	DEBUGFS_FWSTATS_ADD(dma, tx_errors);

	DEBUGFS_FWSTATS_ADD(isr, cmd_cmplt);
	DEBUGFS_FWSTATS_ADD(isr, fiqs);
	DEBUGFS_FWSTATS_ADD(isr, rx_headers);
	DEBUGFS_FWSTATS_ADD(isr, rx_mem_overflow);
	DEBUGFS_FWSTATS_ADD(isr, rx_rdys);
	DEBUGFS_FWSTATS_ADD(isr, irqs);
	DEBUGFS_FWSTATS_ADD(isr, tx_procs);
	DEBUGFS_FWSTATS_ADD(isr, decrypt_done);
	DEBUGFS_FWSTATS_ADD(isr, dma0_done);
	DEBUGFS_FWSTATS_ADD(isr, dma1_done);
	DEBUGFS_FWSTATS_ADD(isr, tx_exch_complete);
	DEBUGFS_FWSTATS_ADD(isr, commands);
	DEBUGFS_FWSTATS_ADD(isr, rx_procs);
	DEBUGFS_FWSTATS_ADD(isr, hw_pm_mode_changes);
	DEBUGFS_FWSTATS_ADD(isr, host_acknowledges);
	DEBUGFS_FWSTATS_ADD(isr, pci_pm);
	DEBUGFS_FWSTATS_ADD(isr, wakeups);
	DEBUGFS_FWSTATS_ADD(isr, low_rssi);

	DEBUGFS_FWSTATS_ADD(wep, addr_key_count);
	DEBUGFS_FWSTATS_ADD(wep, default_key_count);
	/* skipping wep.reserved */
	DEBUGFS_FWSTATS_ADD(wep, key_not_found);
	DEBUGFS_FWSTATS_ADD(wep, decrypt_fail);
	DEBUGFS_FWSTATS_ADD(wep, packets);
	DEBUGFS_FWSTATS_ADD(wep, interrupt);

	DEBUGFS_FWSTATS_ADD(pwr, ps_enter);
	DEBUGFS_FWSTATS_ADD(pwr, elp_enter);
	DEBUGFS_FWSTATS_ADD(pwr, missing_bcns);
	DEBUGFS_FWSTATS_ADD(pwr, wake_on_host);
	DEBUGFS_FWSTATS_ADD(pwr, wake_on_timer_exp);
	DEBUGFS_FWSTATS_ADD(pwr, tx_with_ps);
	DEBUGFS_FWSTATS_ADD(pwr, tx_without_ps);
	DEBUGFS_FWSTATS_ADD(pwr, rcvd_beacons);
	DEBUGFS_FWSTATS_ADD(pwr, power_save_off);
	DEBUGFS_FWSTATS_ADD(pwr, enable_ps);
	DEBUGFS_FWSTATS_ADD(pwr, disable_ps);
	DEBUGFS_FWSTATS_ADD(pwr, fix_tsf_ps);
	/* skipping cont_miss_bcns_spread for now */
	DEBUGFS_FWSTATS_ADD(pwr, rcvd_awake_beacons);

	DEBUGFS_FWSTATS_ADD(mic, rx_pkts);
	DEBUGFS_FWSTATS_ADD(mic, calc_failure);

	DEBUGFS_FWSTATS_ADD(aes, encrypt_fail);
	DEBUGFS_FWSTATS_ADD(aes, decrypt_fail);
	DEBUGFS_FWSTATS_ADD(aes, encrypt_packets);
	DEBUGFS_FWSTATS_ADD(aes, decrypt_packets);
	DEBUGFS_FWSTATS_ADD(aes, encrypt_interrupt);
	DEBUGFS_FWSTATS_ADD(aes, decrypt_interrupt);

	DEBUGFS_FWSTATS_ADD(event, heart_beat);
	DEBUGFS_FWSTATS_ADD(event, calibration);
	DEBUGFS_FWSTATS_ADD(event, rx_mismatch);
	DEBUGFS_FWSTATS_ADD(event, rx_mem_empty);
	DEBUGFS_FWSTATS_ADD(event, rx_pool);
	DEBUGFS_FWSTATS_ADD(event, oom_late);
	DEBUGFS_FWSTATS_ADD(event, phy_transmit_error);
	DEBUGFS_FWSTATS_ADD(event, tx_stuck);

	DEBUGFS_FWSTATS_ADD(ps, pspoll_timeouts);
	DEBUGFS_FWSTATS_ADD(ps, upsd_timeouts);
	DEBUGFS_FWSTATS_ADD(ps, upsd_max_sptime);
	DEBUGFS_FWSTATS_ADD(ps, upsd_max_apturn);
	DEBUGFS_FWSTATS_ADD(ps, pspoll_max_apturn);
	DEBUGFS_FWSTATS_ADD(ps, pspoll_utilization);
	DEBUGFS_FWSTATS_ADD(ps, upsd_utilization);

	DEBUGFS_FWSTATS_ADD(rxpipe, rx_prep_beacon_drop);
	DEBUGFS_FWSTATS_ADD(rxpipe, descr_host_int_trig_rx_data);
	DEBUGFS_FWSTATS_ADD(rxpipe, beacon_buffer_thres_host_int_trig_rx_data);
	DEBUGFS_FWSTATS_ADD(rxpipe, missed_beacon_host_int_trig_rx_data);
	DEBUGFS_FWSTATS_ADD(rxpipe, tx_xfr_host_int_trig_rx_data);

	DEBUGFS_ADD(tx_queue_len, wl->debugfs.rootdir);
	DEBUGFS_ADD(tx_queue_status, wl->debugfs.rootdir);
	DEBUGFS_ADD(retry_count, wl->debugfs.rootdir);
	DEBUGFS_ADD(excessive_retries, wl->debugfs.rootdir);
}

void wl1251_debugfs_reset(struct wl1251 *wl)
{
	if (wl->stats.fw_stats != NULL)
		memset(wl->stats.fw_stats, 0, sizeof(*wl->stats.fw_stats));
	wl->stats.retry_count = 0;
	wl->stats.excessive_retries = 0;
}

int wl1251_debugfs_init(struct wl1251 *wl)
{
	wl->stats.fw_stats = kzalloc(sizeof(*wl->stats.fw_stats), GFP_KERNEL);
	if (!wl->stats.fw_stats)
		return -ENOMEM;

	wl->debugfs.rootdir = debugfs_create_dir(KBUILD_MODNAME, NULL);

	wl->debugfs.fw_statistics = debugfs_create_dir("fw-statistics",
						       wl->debugfs.rootdir);

	wl->stats.fw_stats_update = jiffies;

	wl1251_debugfs_add_files(wl);

	return 0;
}

void wl1251_debugfs_exit(struct wl1251 *wl)
{
	wl1251_debugfs_delete_files(wl);

	kfree(wl->stats.fw_stats);
	wl->stats.fw_stats = NULL;

	debugfs_remove(wl->debugfs.fw_statistics);
	wl->debugfs.fw_statistics = NULL;

	debugfs_remove(wl->debugfs.rootdir);
	wl->debugfs.rootdir = NULL;

}
