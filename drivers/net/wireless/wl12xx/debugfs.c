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
#include "tx.h"

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

#define DEBUGFS_ADD_PREFIX(prefix, name, parent)			\
	do {								\
		entry = debugfs_create_file(#name, 0400, parent,	\
				    wl, &prefix## _## name## _ops);	\
		if (!entry || IS_ERR(entry))				\
			goto err;					\
	} while (0);

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

	ret = wl1271_ps_elp_wakeup(wl);
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

	queue_len = wl1271_tx_total_queue_count(wl);

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

	len = min(count, sizeof(buf) - 1);
	if (copy_from_user(buf, user_buf, len)) {
		return -EFAULT;
	}
	buf[len] = '\0';

	ret = kstrtoul(buf, 0, &value);
	if (ret < 0) {
		wl1271_warning("illegal value in gpio_power");
		return -EINVAL;
	}

	mutex_lock(&wl->mutex);

	if (value)
		wl1271_power_on(wl);
	else
		wl1271_power_off(wl);

	mutex_unlock(&wl->mutex);
	return count;
}

static const struct file_operations gpio_power_ops = {
	.read = gpio_power_read,
	.write = gpio_power_write,
	.open = wl1271_open_file_generic,
	.llseek = default_llseek,
};

static ssize_t start_recovery_write(struct file *file,
				    const char __user *user_buf,
				    size_t count, loff_t *ppos)
{
	struct wl1271 *wl = file->private_data;

	mutex_lock(&wl->mutex);
	wl12xx_queue_recovery_work(wl);
	mutex_unlock(&wl->mutex);

	return count;
}

static const struct file_operations start_recovery_ops = {
	.write = start_recovery_write,
	.open = wl1271_open_file_generic,
	.llseek = default_llseek,
};

static ssize_t driver_state_read(struct file *file, char __user *user_buf,
				 size_t count, loff_t *ppos)
{
	struct wl1271 *wl = file->private_data;
	int res = 0;
	char buf[1024];

	mutex_lock(&wl->mutex);

#define DRIVER_STATE_PRINT(x, fmt)   \
	(res += scnprintf(buf + res, sizeof(buf) - res,\
			  #x " = " fmt "\n", wl->x))

#define DRIVER_STATE_PRINT_LONG(x) DRIVER_STATE_PRINT(x, "%ld")
#define DRIVER_STATE_PRINT_INT(x)  DRIVER_STATE_PRINT(x, "%d")
#define DRIVER_STATE_PRINT_STR(x)  DRIVER_STATE_PRINT(x, "%s")
#define DRIVER_STATE_PRINT_LHEX(x) DRIVER_STATE_PRINT(x, "0x%lx")
#define DRIVER_STATE_PRINT_HEX(x)  DRIVER_STATE_PRINT(x, "0x%x")

	DRIVER_STATE_PRINT_INT(tx_blocks_available);
	DRIVER_STATE_PRINT_INT(tx_allocated_blocks[0]);
	DRIVER_STATE_PRINT_INT(tx_allocated_blocks[1]);
	DRIVER_STATE_PRINT_INT(tx_allocated_blocks[2]);
	DRIVER_STATE_PRINT_INT(tx_allocated_blocks[3]);
	DRIVER_STATE_PRINT_INT(tx_frames_cnt);
	DRIVER_STATE_PRINT_LHEX(tx_frames_map[0]);
	DRIVER_STATE_PRINT_INT(tx_queue_count[0]);
	DRIVER_STATE_PRINT_INT(tx_queue_count[1]);
	DRIVER_STATE_PRINT_INT(tx_queue_count[2]);
	DRIVER_STATE_PRINT_INT(tx_queue_count[3]);
	DRIVER_STATE_PRINT_INT(tx_packets_count);
	DRIVER_STATE_PRINT_INT(tx_results_count);
	DRIVER_STATE_PRINT_LHEX(flags);
	DRIVER_STATE_PRINT_INT(tx_blocks_freed[0]);
	DRIVER_STATE_PRINT_INT(tx_blocks_freed[1]);
	DRIVER_STATE_PRINT_INT(tx_blocks_freed[2]);
	DRIVER_STATE_PRINT_INT(tx_blocks_freed[3]);
	DRIVER_STATE_PRINT_INT(tx_security_last_seq_lsb);
	DRIVER_STATE_PRINT_INT(rx_counter);
	DRIVER_STATE_PRINT_INT(session_counter);
	DRIVER_STATE_PRINT_INT(state);
	DRIVER_STATE_PRINT_INT(bss_type);
	DRIVER_STATE_PRINT_INT(channel);
	DRIVER_STATE_PRINT_HEX(rate_set);
	DRIVER_STATE_PRINT_HEX(basic_rate_set);
	DRIVER_STATE_PRINT_HEX(basic_rate);
	DRIVER_STATE_PRINT_INT(band);
	DRIVER_STATE_PRINT_INT(beacon_int);
	DRIVER_STATE_PRINT_INT(psm_entry_retry);
	DRIVER_STATE_PRINT_INT(ps_poll_failures);
	DRIVER_STATE_PRINT_HEX(filters);
	DRIVER_STATE_PRINT_HEX(rx_config);
	DRIVER_STATE_PRINT_HEX(rx_filter);
	DRIVER_STATE_PRINT_INT(power_level);
	DRIVER_STATE_PRINT_INT(rssi_thold);
	DRIVER_STATE_PRINT_INT(last_rssi_event);
	DRIVER_STATE_PRINT_INT(sg_enabled);
	DRIVER_STATE_PRINT_INT(enable_11a);
	DRIVER_STATE_PRINT_INT(noise);
	DRIVER_STATE_PRINT_LHEX(ap_hlid_map[0]);
	DRIVER_STATE_PRINT_INT(last_tx_hlid);
	DRIVER_STATE_PRINT_INT(ba_support);
	DRIVER_STATE_PRINT_HEX(ba_rx_bitmap);
	DRIVER_STATE_PRINT_HEX(ap_fw_ps_map);
	DRIVER_STATE_PRINT_LHEX(ap_ps_map);
	DRIVER_STATE_PRINT_HEX(quirks);
	DRIVER_STATE_PRINT_HEX(irq);
	DRIVER_STATE_PRINT_HEX(ref_clock);
	DRIVER_STATE_PRINT_HEX(tcxo_clock);
	DRIVER_STATE_PRINT_HEX(hw_pg_ver);
	DRIVER_STATE_PRINT_HEX(platform_quirks);
	DRIVER_STATE_PRINT_HEX(chip.id);
	DRIVER_STATE_PRINT_STR(chip.fw_ver_str);
	DRIVER_STATE_PRINT_INT(sched_scanning);

#undef DRIVER_STATE_PRINT_INT
#undef DRIVER_STATE_PRINT_LONG
#undef DRIVER_STATE_PRINT_HEX
#undef DRIVER_STATE_PRINT_LHEX
#undef DRIVER_STATE_PRINT_STR
#undef DRIVER_STATE_PRINT

	mutex_unlock(&wl->mutex);

	return simple_read_from_buffer(user_buf, count, ppos, buf, res);
}

static const struct file_operations driver_state_ops = {
	.read = driver_state_read,
	.open = wl1271_open_file_generic,
	.llseek = default_llseek,
};

static ssize_t dtim_interval_read(struct file *file, char __user *user_buf,
				  size_t count, loff_t *ppos)
{
	struct wl1271 *wl = file->private_data;
	u8 value;

	if (wl->conf.conn.wake_up_event == CONF_WAKE_UP_EVENT_DTIM ||
	    wl->conf.conn.wake_up_event == CONF_WAKE_UP_EVENT_N_DTIM)
		value = wl->conf.conn.listen_interval;
	else
		value = 0;

	return wl1271_format_buffer(user_buf, count, ppos, "%d\n", value);
}

static ssize_t dtim_interval_write(struct file *file,
				   const char __user *user_buf,
				   size_t count, loff_t *ppos)
{
	struct wl1271 *wl = file->private_data;
	char buf[10];
	size_t len;
	unsigned long value;
	int ret;

	len = min(count, sizeof(buf) - 1);
	if (copy_from_user(buf, user_buf, len))
		return -EFAULT;
	buf[len] = '\0';

	ret = kstrtoul(buf, 0, &value);
	if (ret < 0) {
		wl1271_warning("illegal value for dtim_interval");
		return -EINVAL;
	}

	if (value < 1 || value > 10) {
		wl1271_warning("dtim value is not in valid range");
		return -ERANGE;
	}

	mutex_lock(&wl->mutex);

	wl->conf.conn.listen_interval = value;
	/* for some reason there are different event types for 1 and >1 */
	if (value == 1)
		wl->conf.conn.wake_up_event = CONF_WAKE_UP_EVENT_DTIM;
	else
		wl->conf.conn.wake_up_event = CONF_WAKE_UP_EVENT_N_DTIM;

	/*
	 * we don't reconfigure ACX_WAKE_UP_CONDITIONS now, so it will only
	 * take effect on the next time we enter psm.
	 */
	mutex_unlock(&wl->mutex);
	return count;
}

static const struct file_operations dtim_interval_ops = {
	.read = dtim_interval_read,
	.write = dtim_interval_write,
	.open = wl1271_open_file_generic,
	.llseek = default_llseek,
};

static ssize_t beacon_interval_read(struct file *file, char __user *user_buf,
				    size_t count, loff_t *ppos)
{
	struct wl1271 *wl = file->private_data;
	u8 value;

	if (wl->conf.conn.wake_up_event == CONF_WAKE_UP_EVENT_BEACON ||
	    wl->conf.conn.wake_up_event == CONF_WAKE_UP_EVENT_N_BEACONS)
		value = wl->conf.conn.listen_interval;
	else
		value = 0;

	return wl1271_format_buffer(user_buf, count, ppos, "%d\n", value);
}

static ssize_t beacon_interval_write(struct file *file,
				     const char __user *user_buf,
				     size_t count, loff_t *ppos)
{
	struct wl1271 *wl = file->private_data;
	char buf[10];
	size_t len;
	unsigned long value;
	int ret;

	len = min(count, sizeof(buf) - 1);
	if (copy_from_user(buf, user_buf, len))
		return -EFAULT;
	buf[len] = '\0';

	ret = kstrtoul(buf, 0, &value);
	if (ret < 0) {
		wl1271_warning("illegal value for beacon_interval");
		return -EINVAL;
	}

	if (value < 1 || value > 255) {
		wl1271_warning("beacon interval value is not in valid range");
		return -ERANGE;
	}

	mutex_lock(&wl->mutex);

	wl->conf.conn.listen_interval = value;
	/* for some reason there are different event types for 1 and >1 */
	if (value == 1)
		wl->conf.conn.wake_up_event = CONF_WAKE_UP_EVENT_BEACON;
	else
		wl->conf.conn.wake_up_event = CONF_WAKE_UP_EVENT_N_BEACONS;

	/*
	 * we don't reconfigure ACX_WAKE_UP_CONDITIONS now, so it will only
	 * take effect on the next time we enter psm.
	 */
	mutex_unlock(&wl->mutex);
	return count;
}

static const struct file_operations beacon_interval_ops = {
	.read = beacon_interval_read,
	.write = beacon_interval_write,
	.open = wl1271_open_file_generic,
	.llseek = default_llseek,
};

static ssize_t rx_streaming_interval_write(struct file *file,
			   const char __user *user_buf,
			   size_t count, loff_t *ppos)
{
	struct wl1271 *wl = file->private_data;
	char buf[10];
	size_t len;
	unsigned long value;
	int ret;

	len = min(count, sizeof(buf) - 1);
	if (copy_from_user(buf, user_buf, len))
		return -EFAULT;
	buf[len] = '\0';

	ret = kstrtoul(buf, 0, &value);
	if (ret < 0) {
		wl1271_warning("illegal value in rx_streaming_interval!");
		return -EINVAL;
	}

	/* valid values: 0, 10-100 */
	if (value && (value < 10 || value > 100)) {
		wl1271_warning("value is not in range!");
		return -ERANGE;
	}

	mutex_lock(&wl->mutex);

	wl->conf.rx_streaming.interval = value;

	ret = wl1271_ps_elp_wakeup(wl);
	if (ret < 0)
		goto out;

	wl1271_recalc_rx_streaming(wl);

	wl1271_ps_elp_sleep(wl);
out:
	mutex_unlock(&wl->mutex);
	return count;
}

static ssize_t rx_streaming_interval_read(struct file *file,
			    char __user *userbuf,
			    size_t count, loff_t *ppos)
{
	struct wl1271 *wl = file->private_data;
	return wl1271_format_buffer(userbuf, count, ppos,
				    "%d\n", wl->conf.rx_streaming.interval);
}

static const struct file_operations rx_streaming_interval_ops = {
	.read = rx_streaming_interval_read,
	.write = rx_streaming_interval_write,
	.open = wl1271_open_file_generic,
	.llseek = default_llseek,
};

static ssize_t rx_streaming_always_write(struct file *file,
			   const char __user *user_buf,
			   size_t count, loff_t *ppos)
{
	struct wl1271 *wl = file->private_data;
	char buf[10];
	size_t len;
	unsigned long value;
	int ret;

	len = min(count, sizeof(buf) - 1);
	if (copy_from_user(buf, user_buf, len))
		return -EFAULT;
	buf[len] = '\0';

	ret = kstrtoul(buf, 0, &value);
	if (ret < 0) {
		wl1271_warning("illegal value in rx_streaming_write!");
		return -EINVAL;
	}

	/* valid values: 0, 10-100 */
	if (!(value == 0 || value == 1)) {
		wl1271_warning("value is not in valid!");
		return -EINVAL;
	}

	mutex_lock(&wl->mutex);

	wl->conf.rx_streaming.always = value;

	ret = wl1271_ps_elp_wakeup(wl);
	if (ret < 0)
		goto out;

	wl1271_recalc_rx_streaming(wl);

	wl1271_ps_elp_sleep(wl);
out:
	mutex_unlock(&wl->mutex);
	return count;
}

static ssize_t rx_streaming_always_read(struct file *file,
			    char __user *userbuf,
			    size_t count, loff_t *ppos)
{
	struct wl1271 *wl = file->private_data;
	return wl1271_format_buffer(userbuf, count, ppos,
				    "%d\n", wl->conf.rx_streaming.always);
}

static const struct file_operations rx_streaming_always_ops = {
	.read = rx_streaming_always_read,
	.write = rx_streaming_always_write,
	.open = wl1271_open_file_generic,
	.llseek = default_llseek,
};

static int wl1271_debugfs_add_files(struct wl1271 *wl,
				     struct dentry *rootdir)
{
	int ret = 0;
	struct dentry *entry, *stats, *streaming;

	stats = debugfs_create_dir("fw-statistics", rootdir);
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

	DEBUGFS_ADD(tx_queue_len, rootdir);
	DEBUGFS_ADD(retry_count, rootdir);
	DEBUGFS_ADD(excessive_retries, rootdir);

	DEBUGFS_ADD(gpio_power, rootdir);
	DEBUGFS_ADD(start_recovery, rootdir);
	DEBUGFS_ADD(driver_state, rootdir);
	DEBUGFS_ADD(dtim_interval, rootdir);
	DEBUGFS_ADD(beacon_interval, rootdir);

	streaming = debugfs_create_dir("rx_streaming", rootdir);
	if (!streaming || IS_ERR(streaming))
		goto err;

	DEBUGFS_ADD_PREFIX(rx_streaming, interval, streaming);
	DEBUGFS_ADD_PREFIX(rx_streaming, always, streaming);


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
	if (!wl->stats.fw_stats)
		return;

	memset(wl->stats.fw_stats, 0, sizeof(*wl->stats.fw_stats));
	wl->stats.retry_count = 0;
	wl->stats.excessive_retries = 0;
}

int wl1271_debugfs_init(struct wl1271 *wl)
{
	int ret;
	struct dentry *rootdir;

	rootdir = debugfs_create_dir(KBUILD_MODNAME,
				     wl->hw->wiphy->debugfsdir);

	if (IS_ERR(rootdir)) {
		ret = PTR_ERR(rootdir);
		goto err;
	}

	wl->stats.fw_stats = kzalloc(sizeof(*wl->stats.fw_stats),
				      GFP_KERNEL);

	if (!wl->stats.fw_stats) {
		ret = -ENOMEM;
		goto err_fw;
	}

	wl->stats.fw_stats_update = jiffies;

	ret = wl1271_debugfs_add_files(wl, rootdir);

	if (ret < 0)
		goto err_file;

	return 0;

err_file:
	kfree(wl->stats.fw_stats);
	wl->stats.fw_stats = NULL;

err_fw:
	debugfs_remove_recursive(rootdir);

err:
	return ret;
}

void wl1271_debugfs_exit(struct wl1271 *wl)
{
	kfree(wl->stats.fw_stats);
	wl->stats.fw_stats = NULL;
}
