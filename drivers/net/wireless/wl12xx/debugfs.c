/*
 * This file is part of wl1271
 *
 * Copyright (C) 2009 Nokia Corporation
 *
 * Contact: Luciano Coelho <luciano.coelho@nokia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#include "debugfs.h"

#include <linux/skbuff.h>
#include <linux/slab.h>

#include "wl12xx.h"
#include "acx.h"
#include "ps.h"
#include "io.h"

/* ms */
#define WL1271_DEBUGFS_STATS_LIFETIME 1000

/* debugfs macros idea from mac80211 */
#define DEBUGFS_FORMAT_BUFFER_SIZE 100
static int wl1271_format_buffer(char __user *userbuf, size_t count,
				    loff_t *ppos, char *fmt, ...)
{
	va_list args;
	char buf[DEBUGFS_FORMAT_BUFFER_SIZE];
	int res;

	va_start(args, fmt);
	res = vscnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);

	return simple_read_from_buffer(userbuf, count, ppos, buf, res);
}

#define DEBUGFS_READONLY_FILE(name, fmt, value...)			\
static ssize_t name## _read(struct file *file, char __user *userbuf,	\
			    size_t count, loff_t *ppos)			\
{									\
	struct wl1271 *wl = file->private_data;				\
	return wl1271_format_buffer(userbuf, count, ppos,		\
				    fmt "\n", ##value);			\
}									\
									\
static const struct file_operations name## _ops = {			\
	.read = name## _read,						\
	.open = wl1271_open_file_generic,				\
	.llseek	= generic_file_llseek,					\
};

#define DEBUGFS_ADD(name, parent)					\
	entry = debugfs_create_file(#name, 0400, parent,		\
				    wl, &name## _ops);			\
	if (!entry || IS_ERR(entry))					\
		goto err;						\

#define DEBUGFS_FWSTATS_FILE(sub, name, fmt)				\
static ssize_t sub## _ ##name## _read(struct file *file,		\
				      char __user *userbuf,		\
				      size_t count, loff_t *ppos)	\
{									\
	struct wl1271 *wl = file->private_data;				\
									\
	wl1271_debugfs_update_stats(wl);				\
									\
	return wl1271_format_buffer(userbuf, count, ppos, fmt "\n",	\
				    wl->stats.fw_stats->sub.name);	\
}									\
									\
static const struct file_operations sub## _ ##name## _ops = {		\
	.read = sub## _ ##name## _read,					\
	.open = wl1271_open_file_generic,				\
	.llseek	= generic_file_llseek,					\
};

#define DEBUGFS_FWSTATS_ADD(sub, name)				\
	DEBUGFS_ADD(sub## _ ##name, stats)

static void wl1271_debugfs_update_stats(struct wl1271 *wl)
{
	int ret;

	mutex_lock(&wl->mutex);

	ret = wl1271_ps_elp_wakeup(wl, false);
	if (ret < 0)
		goto out;

	if (wl->state == WL1271_STATE_ON &&
	    time_after(jiffies, wl->stats.fw_stats_update +
		       msecs_to_jiffies(WL1271_DEBUGFS_STATS_LIFETIME))) {
		wl1271_acx_statistics(wl, wl->stats.fw_stats);
		wl->stats.fw_stats_update = jiffies;
	}

	wl1271_ps_elp_sleep(wl);

out:
	mutex_unlock(&wl->mutex);
}

static int wl1271_open_file_generic(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private;
	return 0;
}

DEBUGFS_FWSTATS_FILE(tx, internal_desc_overflow, "%u");

DEBUGFS_FWSTATS_FILE(rx, out_of_mem, "%u");
DEBUGFS_FWSTATS_FILE(rx, hdr_overflow, "%u");
DEBUGFS_FWSTATS_FILE(rx, hw_stuck, "%u");
DEBUGFS_FWSTATS_FILE(rx, dropped, "%u");
DEBUGFS_FWSTATS_FILE(rx, fcs_err, "%u");
DEBUGFS_FWSTATS_FILE(rx, xfr_hint_trig, "%u");
DEBUGFS_FWSTATS_FILE(rx, path_reset, "%u");
DEBUGFS_FWSTATS_FILE(rx, reset_counter, "%u");

DEBUGFS_FWSTATS_FILE(dma, rx_requested, "%u");
DEBUGFS_FWSTATS_FILE(dma, rx_errors, "%u");
DEBUGFS_FWSTATS_FILE(dma, tx_requested, "%u");
DEBUGFS_FWSTATS_FILE(dma, tx_errors, "%u");

DEBUGFS_FWSTATS_FILE(isr, cmd_cmplt, "%u");
DEBUGFS_FWSTATS_FILE(isr, fiqs, "%u");
DEBUGFS_FWSTATS_FILE(isr, rx_headers, "%u");
DEBUGFS_FWSTATS_FILE(isr, rx_mem_overflow, "%u");
DEBUGFS_FWSTATS_FILE(isr, rx_rdys, "%u");
DEBUGFS_FWSTATS_FILE(isr, irqs, "%u");
DEBUGFS_FWSTATS_FILE(isr, tx_procs, "%u");
DEBUGFS_FWSTATS_FILE(isr, decrypt_done, "%u");
DEBUGFS_FWSTATS_FILE(isr, dma0_done, "%u");
DEBUGFS_FWSTATS_FILE(isr, dma1_done, "%u");
DEBUGFS_FWSTATS_FILE(isr, tx_exch_complete, "%u");
DEBUGFS_FWSTATS_FILE(isr, commands, "%u");
DEBUGFS_FWSTATS_FILE(isr, rx_procs, "%u");
DEBUGFS_FWSTATS_FILE(isr, hw_pm_mode_changes, "%u");
DEBUGFS_FWSTATS_FILE(isr, host_acknowledges, "%u");
DEBUGFS_FWSTATS_FILE(isr, pci_pm, "%u");
DEBUGFS_FWSTATS_FILE(isr, wakeups, "%u");
DEBUGFS_FWSTATS_FILE(isr, low_rssi, "%u");

DEBUGFS_FWSTATS_FILE(wep, addr_key_count, "%u");
DEBUGFS_FWSTATS_FILE(wep, default_key_count, "%u");
/* skipping wep.reserved */
DEBUGFS_FWSTATS_FILE(wep, key_not_found, "%u");
DEBUGFS_FWSTATS_FILE(wep, decrypt_fail, "%u");
DEBUGFS_FWSTATS_FILE(wep, packets, "%u");
DEBUGFS_FWSTATS_FILE(wep, interrupt, "%u");

DEBUGFS_FWSTATS_FILE(pwr, ps_enter, "%u");
DEBUGFS_FWSTATS_FILE(pwr, elp_enter, "%u");
DEBUGFS_FWSTATS_FILE(pwr, missing_bcns, "%u");
DEBUGFS_FWSTATS_FILE(pwr, wake_on_host, "%u");
DEBUGFS_FWSTATS_FILE(pwr, wake_on_timer_exp, "%u");
DEBUGFS_FWSTATS_FILE(pwr, tx_with_ps, "%u");
DEBUGFS_FWSTATS_FILE(pwr, tx_without_ps, "%u");
DEBUGFS_FWSTATS_FILE(pwr, rcvd_beacons, "%u");
DEBUGFS_FWSTATS_FILE(pwr, power_save_off, "%u");
DEBUGFS_FWSTATS_FILE(pwr, enable_ps, "%u");
DEBUGFS_FWSTATS_FILE(pwr, disable_ps, "%u");
DEBUGFS_FWSTATS_FILE(pwr, fix_tsf_ps, "%u");
/* skipping cont_miss_bcns_spread for now */
DEBUGFS_FWSTATS_FILE(pwr, rcvd_awake_beacons, "%u");

DEBUGFS_FWSTATS_FILE(mic, rx_pkts, "%u");
DEBUGFS_FWSTATS_FILE(mic, calc_failure, "%u");

DEBUGFS_FWSTATS_FILE(aes, encrypt_fail, "%u");
DEBUGFS_FWSTATS_FILE(aes, decrypt_fail, "%u");
DEBUGFS_FWSTATS_FILE(aes, encrypt_packets, "%u");
DEBUGFS_FWSTATS_FILE(aes, decrypt_packets, "%u");
DEBUGFS_FWSTATS_FILE(aes, encrypt_interrupt, "%u");
DEBUGFS_FWSTATS_FILE(aes, decrypt_interrupt, "%u");

DEBUGFS_FWSTATS_FILE(event, heart_beat, "%u");
DEBUGFS_FWSTATS_FILE(event, calibration, "%u");
DEBUGFS_FWSTATS_FILE(event, rx_mismatch, "%u");
DEBUGFS_FWSTATS_FILE(event, rx_mem_empty, "%u");
DEBUGFS_FWSTATS_FILE(event, rx_pool, "%u");
DEBUGFS_FWSTATS_FILE(event, oom_late, "%u");
DEBUGFS_FWSTATS_FILE(event, phy_transmit_error, "%u");
DEBUGFS_FWSTATS_FILE(event, tx_stuck, "%u");

DEBUGFS_FWSTATS_FILE(ps, pspoll_timeouts, "%u");
DEBUGFS_FWSTATS_FILE(ps, upsd_timeouts, "%u");
DEBUGFS_FWSTATS_FILE(ps, upsd_max_sptime, "%u");
DEBUGFS_FWSTATS_FILE(ps, upsd_max_apturn, "%u");
DEBUGFS_FWSTATS_FILE(ps, pspoll_max_apturn, "%u");
DEBUGFS_FWSTATS_FILE(ps, pspoll_utilization, "%u");
DEBUGFS_FWSTATS_FILE(ps, upsd_utilization, "%u");

DEBUGFS_FWSTATS_FILE(rxpipe, rx_prep_beacon_drop, "%u");
DEBUGFS_FWSTATS_FILE(rxpipe, descr_host_int_trig_rx_data, "%u");
DEBUGFS_FWSTATS_FILE(rxpipe, beacon_buffer_thres_host_int_trig_rx_data, "%u");
DEBUGFS_FWSTATS_FILE(rxpipe, missed_beacon_host_int_trig_rx_data, "%u");
DEBUGFS_FWSTATS_FILE(rxpipe, tx_xfr_host_int_trig_rx_data, "%u");

DEBUGFS_READONLY_FILE(retry_count, "%u", wl->stats.retry_count);
DEBUGFS_READONLY_FILE(excessive_retries, "%u",
		      wl->stats.excessive_retries);

static ssize_t tx_queue_len_read(struct file *file, char __user *userbuf,
				 size_t count, loff_t *ppos)
{
	struct wl1271 *wl = file->private_data;
	u32 queue_len;
	char buf[20];
	int res;

	queue_len = skb_queue_len(&wl->tx_queue);

	res = scnprintf(buf, sizeof(buf), "%u\n", queue_len);
	return simple_read_from_buffer(userbuf, count, ppos, buf, res);
}

static const struct file_operations tx_queue_len_ops = {
	.read = tx_queue_len_read,
	.open = wl1271_open_file_generic,
	.llseek = default_llseek,
};

static ssize_t gpio_power_read(struct file *file, char __user *user_buf,
			  size_t count, loff_t *ppos)
{
	struct wl1271 *wl = file->private_data;
	bool state = test_bit(WL1271_FLAG_GPIO_POWER, &wl->flags);

	int res;
	char buf[10];

	res = scnprintf(buf, sizeof(buf), "%d\n", state);

	return simple_read_from_buffer(user_buf, count, ppos, buf, res);
}

static ssize_t gpio_power_write(struct file *file,
			   const char __user *user_buf,
			   size_t count, loff_t *ppos)
{
	struct wl1271 *wl = file->private_data;
	char buf[10];
	size_t len;
	unsigned long value;
	int ret;

	mutex_lock(&wl->mutex);

	len = min(count, sizeof(buf) - 1);
	if (copy_from_user(buf, user_buf, len)) {
		ret = -EFAULT;
		goto out;
	}
	buf[len] = '\0';

	ret = strict_strtoul(buf, 0, &value);
	if (ret < 0) {
		wl1271_warning("illegal value in gpio_power");
		goto out;
	}

	if (value)
		wl1271_power_on(wl);
	else
		wl1271_power_off(wl);

out:
	mutex_unlock(&wl->mutex);
	return count;
}

static const struct file_operations gpio_power_ops = {
	.read = gpio_power_read,
	.write = gpio_power_write,
	.open = wl1271_open_file_generic,
	.llseek = default_llseek,
};

static int wl1271_debugfs_add_files(struct wl1271 *wl)
{
	int ret = 0;
	struct dentry *entry, *stats;

	stats = debugfs_create_dir("fw-statistics", wl->rootdir);
	if (!stats || IS_ERR(stats)) {
		entry = stats;
		goto err;
	}

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

	DEBUGFS_ADD(tx_queue_len, wl->rootdir);
	DEBUGFS_ADD(retry_count, wl->rootdir);
	DEBUGFS_ADD(excessive_retries, wl->rootdir);

	DEBUGFS_ADD(gpio_power, wl->rootdir);

	return 0;

err:
	if (IS_ERR(entry))
		ret = PTR_ERR(entry);
	else
		ret = -ENOMEM;

	return ret;
}

void wl1271_debugfs_reset(struct wl1271 *wl)
{
	if (!wl->rootdir)
		return;

	memset(wl->stats.fw_stats, 0, sizeof(*wl->stats.fw_stats));
	wl->stats.retry_count = 0;
	wl->stats.excessive_retries = 0;
}

int wl1271_debugfs_init(struct wl1271 *wl)
{
	int ret;

	wl->rootdir = debugfs_create_dir(KBUILD_MODNAME,
					 wl->hw->wiphy->debugfsdir);

	if (IS_ERR(wl->rootdir)) {
		ret = PTR_ERR(wl->rootdir);
		wl->rootdir = NULL;
		goto err;
	}

	wl->stats.fw_stats = kzalloc(sizeof(*wl->stats.fw_stats),
				      GFP_KERNEL);

	if (!wl->stats.fw_stats) {
		ret = -ENOMEM;
		goto err_fw;
	}

	wl->stats.fw_stats_update = jiffies;

	ret = wl1271_debugfs_add_files(wl);

	if (ret < 0)
		goto err_file;

	return 0;

err_file:
	kfree(wl->stats.fw_stats);
	wl->stats.fw_stats = NULL;

err_fw:
	debugfs_remove_recursive(wl->rootdir);
	wl->rootdir = NULL;

err:
	return ret;
}

void wl1271_debugfs_exit(struct wl1271 *wl)
{
	kfree(wl->stats.fw_stats);
	wl->stats.fw_stats = NULL;

	debugfs_remove_recursive(wl->rootdir);
	wl->rootdir = NULL;

}
