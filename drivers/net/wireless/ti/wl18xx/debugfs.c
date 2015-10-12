/*
 * This file is part of wl18xx
 *
 * Copyright (C) 2009 Nokia Corporation
 * Copyright (C) 2011-2012 Texas Instruments
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

#include "../wlcore/debugfs.h"
#include "../wlcore/wlcore.h"
#include "../wlcore/debug.h"
#include "../wlcore/ps.h"

#include "wl18xx.h"
#include "acx.h"
#include "cmd.h"
#include "debugfs.h"

#define WL18XX_DEBUGFS_FWSTATS_FILE(a, b, c) \
	DEBUGFS_FWSTATS_FILE(a, b, c, wl18xx_acx_statistics)
#define WL18XX_DEBUGFS_FWSTATS_FILE_ARRAY(a, b, c) \
	DEBUGFS_FWSTATS_FILE_ARRAY(a, b, c, wl18xx_acx_statistics)


WL18XX_DEBUGFS_FWSTATS_FILE(error, error_frame_non_ctrl, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(error, error_frame_ctrl, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(error, error_frame_during_protection, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(error, null_frame_tx_start, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(error, null_frame_cts_start, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(error, bar_retry, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(error, num_frame_cts_nul_flid, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(error, tx_abort_failure, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(error, tx_resume_failure, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(error, rx_cmplt_db_overflow_cnt, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(error, elp_while_rx_exch, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(error, elp_while_tx_exch, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(error, elp_while_tx, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(error, elp_while_nvic_pending, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(error, rx_excessive_frame_len, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(error, burst_mismatch, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(error, tbc_exch_mismatch, "%u");

WL18XX_DEBUGFS_FWSTATS_FILE(tx, tx_prepared_descs, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(tx, tx_cmplt, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(tx, tx_template_prepared, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(tx, tx_data_prepared, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(tx, tx_template_programmed, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(tx, tx_data_programmed, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(tx, tx_burst_programmed, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(tx, tx_starts, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(tx, tx_stop, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(tx, tx_start_templates, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(tx, tx_start_int_templates, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(tx, tx_start_fw_gen, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(tx, tx_start_data, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(tx, tx_start_null_frame, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(tx, tx_exch, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(tx, tx_retry_template, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(tx, tx_retry_data, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE_ARRAY(tx, tx_retry_per_rate,
				  NUM_OF_RATES_INDEXES);
WL18XX_DEBUGFS_FWSTATS_FILE(tx, tx_exch_pending, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(tx, tx_exch_expiry, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(tx, tx_done_template, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(tx, tx_done_data, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(tx, tx_done_int_template, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(tx, tx_cfe1, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(tx, tx_cfe2, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(tx, frag_called, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(tx, frag_mpdu_alloc_failed, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(tx, frag_init_called, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(tx, frag_in_process_called, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(tx, frag_tkip_called, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(tx, frag_key_not_found, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(tx, frag_need_fragmentation, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(tx, frag_bad_mblk_num, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(tx, frag_failed, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(tx, frag_cache_hit, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(tx, frag_cache_miss, "%u");

WL18XX_DEBUGFS_FWSTATS_FILE(rx, rx_beacon_early_term, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(rx, rx_out_of_mpdu_nodes, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(rx, rx_hdr_overflow, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(rx, rx_dropped_frame, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(rx, rx_done, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(rx, rx_defrag, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(rx, rx_defrag_end, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(rx, rx_cmplt, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(rx, rx_pre_complt, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(rx, rx_cmplt_task, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(rx, rx_phy_hdr, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(rx, rx_timeout, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(rx, rx_rts_timeout, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(rx, rx_timeout_wa, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(rx, defrag_called, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(rx, defrag_init_called, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(rx, defrag_in_process_called, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(rx, defrag_tkip_called, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(rx, defrag_need_defrag, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(rx, defrag_decrypt_failed, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(rx, decrypt_key_not_found, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(rx, defrag_need_decrypt, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(rx, rx_tkip_replays, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(rx, rx_xfr, "%u");

WL18XX_DEBUGFS_FWSTATS_FILE(isr, irqs, "%u");

WL18XX_DEBUGFS_FWSTATS_FILE(pwr, missing_bcns_cnt, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(pwr, rcvd_bcns_cnt, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(pwr, connection_out_of_sync, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE_ARRAY(pwr, cont_miss_bcns_spread,
				  PWR_STAT_MAX_CONT_MISSED_BCNS_SPREAD);
WL18XX_DEBUGFS_FWSTATS_FILE(pwr, rcvd_awake_bcns_cnt, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(pwr, sleep_time_count, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(pwr, sleep_time_avg, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(pwr, sleep_cycle_avg, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(pwr, sleep_percent, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(pwr, ap_sleep_active_conf, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(pwr, ap_sleep_user_conf, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(pwr, ap_sleep_counter, "%u");

WL18XX_DEBUGFS_FWSTATS_FILE(rx_filter, beacon_filter, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(rx_filter, arp_filter, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(rx_filter, mc_filter, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(rx_filter, dup_filter, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(rx_filter, data_filter, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(rx_filter, ibss_filter, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(rx_filter, protection_filter, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(rx_filter, accum_arp_pend_requests, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(rx_filter, max_arp_queue_dep, "%u");

WL18XX_DEBUGFS_FWSTATS_FILE_ARRAY(rx_rate, rx_frames_per_rates, 50);

WL18XX_DEBUGFS_FWSTATS_FILE_ARRAY(aggr_size, tx_agg_rate,
				  AGGR_STATS_TX_AGG);
WL18XX_DEBUGFS_FWSTATS_FILE_ARRAY(aggr_size, tx_agg_len,
				  AGGR_STATS_TX_AGG);
WL18XX_DEBUGFS_FWSTATS_FILE_ARRAY(aggr_size, rx_size,
				  AGGR_STATS_RX_SIZE_LEN);

WL18XX_DEBUGFS_FWSTATS_FILE(pipeline, hs_tx_stat_fifo_int, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(pipeline, enc_tx_stat_fifo_int, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(pipeline, enc_rx_stat_fifo_int, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(pipeline, rx_complete_stat_fifo_int, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(pipeline, pre_proc_swi, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(pipeline, post_proc_swi, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(pipeline, sec_frag_swi, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(pipeline, pre_to_defrag_swi, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(pipeline, defrag_to_rx_xfer_swi, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(pipeline, dec_packet_in, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(pipeline, dec_packet_in_fifo_full, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(pipeline, dec_packet_out, "%u");

WL18XX_DEBUGFS_FWSTATS_FILE_ARRAY(pipeline, pipeline_fifo_full,
				  PIPE_STATS_HW_FIFO);

WL18XX_DEBUGFS_FWSTATS_FILE_ARRAY(diversity, num_of_packets_per_ant,
				  DIVERSITY_STATS_NUM_OF_ANT);
WL18XX_DEBUGFS_FWSTATS_FILE(diversity, total_num_of_toggles, "%u");

WL18XX_DEBUGFS_FWSTATS_FILE(thermal, irq_thr_low, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(thermal, irq_thr_high, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(thermal, tx_stop, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(thermal, tx_resume, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(thermal, false_irq, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(thermal, adc_source_unexpected, "%u");

WL18XX_DEBUGFS_FWSTATS_FILE_ARRAY(calib, fail_count,
				  WL18XX_NUM_OF_CALIBRATIONS_ERRORS);
WL18XX_DEBUGFS_FWSTATS_FILE(calib, calib_count, "%u");

WL18XX_DEBUGFS_FWSTATS_FILE(roaming, rssi_level, "%d");

WL18XX_DEBUGFS_FWSTATS_FILE(dfs, num_of_radar_detections, "%d");

static ssize_t conf_read(struct file *file, char __user *user_buf,
			 size_t count, loff_t *ppos)
{
	struct wl1271 *wl = file->private_data;
	struct wl18xx_priv *priv = wl->priv;
	struct wlcore_conf_header header;
	char *buf, *pos;
	size_t len;
	int ret;

	len = WL18XX_CONF_SIZE;
	buf = kmalloc(len, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	header.magic	= cpu_to_le32(WL18XX_CONF_MAGIC);
	header.version	= cpu_to_le32(WL18XX_CONF_VERSION);
	header.checksum	= 0;

	mutex_lock(&wl->mutex);

	pos = buf;
	memcpy(pos, &header, sizeof(header));
	pos += sizeof(header);
	memcpy(pos, &wl->conf, sizeof(wl->conf));
	pos += sizeof(wl->conf);
	memcpy(pos, &priv->conf, sizeof(priv->conf));

	mutex_unlock(&wl->mutex);

	ret = simple_read_from_buffer(user_buf, count, ppos, buf, len);

	kfree(buf);
	return ret;
}

static const struct file_operations conf_ops = {
	.read = conf_read,
	.open = simple_open,
	.llseek = default_llseek,
};

static ssize_t clear_fw_stats_write(struct file *file,
			      const char __user *user_buf,
			      size_t count, loff_t *ppos)
{
	struct wl1271 *wl = file->private_data;
	int ret;

	mutex_lock(&wl->mutex);

	if (unlikely(wl->state != WLCORE_STATE_ON))
		goto out;

	ret = wl18xx_acx_clear_statistics(wl);
	if (ret < 0) {
		count = ret;
		goto out;
	}
out:
	mutex_unlock(&wl->mutex);
	return count;
}

static const struct file_operations clear_fw_stats_ops = {
	.write = clear_fw_stats_write,
	.open = simple_open,
	.llseek = default_llseek,
};

static ssize_t radar_detection_write(struct file *file,
				     const char __user *user_buf,
				     size_t count, loff_t *ppos)
{
	struct wl1271 *wl = file->private_data;
	int ret;
	u8 channel;

	ret = kstrtou8_from_user(user_buf, count, 10, &channel);
	if (ret < 0) {
		wl1271_warning("illegal channel");
		return -EINVAL;
	}

	mutex_lock(&wl->mutex);

	if (unlikely(wl->state != WLCORE_STATE_ON))
		goto out;

	ret = wl1271_ps_elp_wakeup(wl);
	if (ret < 0)
		goto out;

	ret = wl18xx_cmd_radar_detection_debug(wl, channel);
	if (ret < 0)
		count = ret;

	wl1271_ps_elp_sleep(wl);
out:
	mutex_unlock(&wl->mutex);
	return count;
}

static const struct file_operations radar_detection_ops = {
	.write = radar_detection_write,
	.open = simple_open,
	.llseek = default_llseek,
};

static ssize_t dynamic_fw_traces_write(struct file *file,
					const char __user *user_buf,
					size_t count, loff_t *ppos)
{
	struct wl1271 *wl = file->private_data;
	unsigned long value;
	int ret;

	ret = kstrtoul_from_user(user_buf, count, 0, &value);
	if (ret < 0)
		return ret;

	mutex_lock(&wl->mutex);

	wl->dynamic_fw_traces = value;

	if (unlikely(wl->state != WLCORE_STATE_ON))
		goto out;

	ret = wl1271_ps_elp_wakeup(wl);
	if (ret < 0)
		goto out;

	ret = wl18xx_acx_dynamic_fw_traces(wl);
	if (ret < 0)
		count = ret;

	wl1271_ps_elp_sleep(wl);
out:
	mutex_unlock(&wl->mutex);
	return count;
}

static ssize_t dynamic_fw_traces_read(struct file *file,
					char __user *userbuf,
					size_t count, loff_t *ppos)
{
	struct wl1271 *wl = file->private_data;
	return wl1271_format_buffer(userbuf, count, ppos,
				    "%d\n", wl->dynamic_fw_traces);
}

static const struct file_operations dynamic_fw_traces_ops = {
	.read = dynamic_fw_traces_read,
	.write = dynamic_fw_traces_write,
	.open = simple_open,
	.llseek = default_llseek,
};

int wl18xx_debugfs_add_files(struct wl1271 *wl,
			     struct dentry *rootdir)
{
	int ret = 0;
	struct dentry *entry, *stats, *moddir;

	moddir = debugfs_create_dir(KBUILD_MODNAME, rootdir);
	if (!moddir || IS_ERR(moddir)) {
		entry = moddir;
		goto err;
	}

	stats = debugfs_create_dir("fw_stats", moddir);
	if (!stats || IS_ERR(stats)) {
		entry = stats;
		goto err;
	}

	DEBUGFS_ADD(clear_fw_stats, stats);

	DEBUGFS_FWSTATS_ADD(error, error_frame_non_ctrl);
	DEBUGFS_FWSTATS_ADD(error, error_frame_ctrl);
	DEBUGFS_FWSTATS_ADD(error, error_frame_during_protection);
	DEBUGFS_FWSTATS_ADD(error, null_frame_tx_start);
	DEBUGFS_FWSTATS_ADD(error, null_frame_cts_start);
	DEBUGFS_FWSTATS_ADD(error, bar_retry);
	DEBUGFS_FWSTATS_ADD(error, num_frame_cts_nul_flid);
	DEBUGFS_FWSTATS_ADD(error, tx_abort_failure);
	DEBUGFS_FWSTATS_ADD(error, tx_resume_failure);
	DEBUGFS_FWSTATS_ADD(error, rx_cmplt_db_overflow_cnt);
	DEBUGFS_FWSTATS_ADD(error, elp_while_rx_exch);
	DEBUGFS_FWSTATS_ADD(error, elp_while_tx_exch);
	DEBUGFS_FWSTATS_ADD(error, elp_while_tx);
	DEBUGFS_FWSTATS_ADD(error, elp_while_nvic_pending);
	DEBUGFS_FWSTATS_ADD(error, rx_excessive_frame_len);
	DEBUGFS_FWSTATS_ADD(error, burst_mismatch);
	DEBUGFS_FWSTATS_ADD(error, tbc_exch_mismatch);

	DEBUGFS_FWSTATS_ADD(tx, tx_prepared_descs);
	DEBUGFS_FWSTATS_ADD(tx, tx_cmplt);
	DEBUGFS_FWSTATS_ADD(tx, tx_template_prepared);
	DEBUGFS_FWSTATS_ADD(tx, tx_data_prepared);
	DEBUGFS_FWSTATS_ADD(tx, tx_template_programmed);
	DEBUGFS_FWSTATS_ADD(tx, tx_data_programmed);
	DEBUGFS_FWSTATS_ADD(tx, tx_burst_programmed);
	DEBUGFS_FWSTATS_ADD(tx, tx_starts);
	DEBUGFS_FWSTATS_ADD(tx, tx_stop);
	DEBUGFS_FWSTATS_ADD(tx, tx_start_templates);
	DEBUGFS_FWSTATS_ADD(tx, tx_start_int_templates);
	DEBUGFS_FWSTATS_ADD(tx, tx_start_fw_gen);
	DEBUGFS_FWSTATS_ADD(tx, tx_start_data);
	DEBUGFS_FWSTATS_ADD(tx, tx_start_null_frame);
	DEBUGFS_FWSTATS_ADD(tx, tx_exch);
	DEBUGFS_FWSTATS_ADD(tx, tx_retry_template);
	DEBUGFS_FWSTATS_ADD(tx, tx_retry_data);
	DEBUGFS_FWSTATS_ADD(tx, tx_retry_per_rate);
	DEBUGFS_FWSTATS_ADD(tx, tx_exch_pending);
	DEBUGFS_FWSTATS_ADD(tx, tx_exch_expiry);
	DEBUGFS_FWSTATS_ADD(tx, tx_done_template);
	DEBUGFS_FWSTATS_ADD(tx, tx_done_data);
	DEBUGFS_FWSTATS_ADD(tx, tx_done_int_template);
	DEBUGFS_FWSTATS_ADD(tx, tx_cfe1);
	DEBUGFS_FWSTATS_ADD(tx, tx_cfe2);
	DEBUGFS_FWSTATS_ADD(tx, frag_called);
	DEBUGFS_FWSTATS_ADD(tx, frag_mpdu_alloc_failed);
	DEBUGFS_FWSTATS_ADD(tx, frag_init_called);
	DEBUGFS_FWSTATS_ADD(tx, frag_in_process_called);
	DEBUGFS_FWSTATS_ADD(tx, frag_tkip_called);
	DEBUGFS_FWSTATS_ADD(tx, frag_key_not_found);
	DEBUGFS_FWSTATS_ADD(tx, frag_need_fragmentation);
	DEBUGFS_FWSTATS_ADD(tx, frag_bad_mblk_num);
	DEBUGFS_FWSTATS_ADD(tx, frag_failed);
	DEBUGFS_FWSTATS_ADD(tx, frag_cache_hit);
	DEBUGFS_FWSTATS_ADD(tx, frag_cache_miss);

	DEBUGFS_FWSTATS_ADD(rx, rx_beacon_early_term);
	DEBUGFS_FWSTATS_ADD(rx, rx_out_of_mpdu_nodes);
	DEBUGFS_FWSTATS_ADD(rx, rx_hdr_overflow);
	DEBUGFS_FWSTATS_ADD(rx, rx_dropped_frame);
	DEBUGFS_FWSTATS_ADD(rx, rx_done);
	DEBUGFS_FWSTATS_ADD(rx, rx_defrag);
	DEBUGFS_FWSTATS_ADD(rx, rx_defrag_end);
	DEBUGFS_FWSTATS_ADD(rx, rx_cmplt);
	DEBUGFS_FWSTATS_ADD(rx, rx_pre_complt);
	DEBUGFS_FWSTATS_ADD(rx, rx_cmplt_task);
	DEBUGFS_FWSTATS_ADD(rx, rx_phy_hdr);
	DEBUGFS_FWSTATS_ADD(rx, rx_timeout);
	DEBUGFS_FWSTATS_ADD(rx, rx_rts_timeout);
	DEBUGFS_FWSTATS_ADD(rx, rx_timeout_wa);
	DEBUGFS_FWSTATS_ADD(rx, defrag_called);
	DEBUGFS_FWSTATS_ADD(rx, defrag_init_called);
	DEBUGFS_FWSTATS_ADD(rx, defrag_in_process_called);
	DEBUGFS_FWSTATS_ADD(rx, defrag_tkip_called);
	DEBUGFS_FWSTATS_ADD(rx, defrag_need_defrag);
	DEBUGFS_FWSTATS_ADD(rx, defrag_decrypt_failed);
	DEBUGFS_FWSTATS_ADD(rx, decrypt_key_not_found);
	DEBUGFS_FWSTATS_ADD(rx, defrag_need_decrypt);
	DEBUGFS_FWSTATS_ADD(rx, rx_tkip_replays);
	DEBUGFS_FWSTATS_ADD(rx, rx_xfr);

	DEBUGFS_FWSTATS_ADD(isr, irqs);

	DEBUGFS_FWSTATS_ADD(pwr, missing_bcns_cnt);
	DEBUGFS_FWSTATS_ADD(pwr, rcvd_bcns_cnt);
	DEBUGFS_FWSTATS_ADD(pwr, connection_out_of_sync);
	DEBUGFS_FWSTATS_ADD(pwr, cont_miss_bcns_spread);
	DEBUGFS_FWSTATS_ADD(pwr, rcvd_awake_bcns_cnt);
	DEBUGFS_FWSTATS_ADD(pwr, sleep_time_count);
	DEBUGFS_FWSTATS_ADD(pwr, sleep_time_avg);
	DEBUGFS_FWSTATS_ADD(pwr, sleep_cycle_avg);
	DEBUGFS_FWSTATS_ADD(pwr, sleep_percent);
	DEBUGFS_FWSTATS_ADD(pwr, ap_sleep_active_conf);
	DEBUGFS_FWSTATS_ADD(pwr, ap_sleep_user_conf);
	DEBUGFS_FWSTATS_ADD(pwr, ap_sleep_counter);

	DEBUGFS_FWSTATS_ADD(rx_filter, beacon_filter);
	DEBUGFS_FWSTATS_ADD(rx_filter, arp_filter);
	DEBUGFS_FWSTATS_ADD(rx_filter, mc_filter);
	DEBUGFS_FWSTATS_ADD(rx_filter, dup_filter);
	DEBUGFS_FWSTATS_ADD(rx_filter, data_filter);
	DEBUGFS_FWSTATS_ADD(rx_filter, ibss_filter);
	DEBUGFS_FWSTATS_ADD(rx_filter, protection_filter);
	DEBUGFS_FWSTATS_ADD(rx_filter, accum_arp_pend_requests);
	DEBUGFS_FWSTATS_ADD(rx_filter, max_arp_queue_dep);

	DEBUGFS_FWSTATS_ADD(rx_rate, rx_frames_per_rates);

	DEBUGFS_FWSTATS_ADD(aggr_size, tx_agg_rate);
	DEBUGFS_FWSTATS_ADD(aggr_size, tx_agg_len);
	DEBUGFS_FWSTATS_ADD(aggr_size, rx_size);

	DEBUGFS_FWSTATS_ADD(pipeline, hs_tx_stat_fifo_int);
	DEBUGFS_FWSTATS_ADD(pipeline, enc_tx_stat_fifo_int);
	DEBUGFS_FWSTATS_ADD(pipeline, enc_rx_stat_fifo_int);
	DEBUGFS_FWSTATS_ADD(pipeline, rx_complete_stat_fifo_int);
	DEBUGFS_FWSTATS_ADD(pipeline, pre_proc_swi);
	DEBUGFS_FWSTATS_ADD(pipeline, post_proc_swi);
	DEBUGFS_FWSTATS_ADD(pipeline, sec_frag_swi);
	DEBUGFS_FWSTATS_ADD(pipeline, pre_to_defrag_swi);
	DEBUGFS_FWSTATS_ADD(pipeline, defrag_to_rx_xfer_swi);
	DEBUGFS_FWSTATS_ADD(pipeline, dec_packet_in);
	DEBUGFS_FWSTATS_ADD(pipeline, dec_packet_in_fifo_full);
	DEBUGFS_FWSTATS_ADD(pipeline, dec_packet_out);
	DEBUGFS_FWSTATS_ADD(pipeline, pipeline_fifo_full);

	DEBUGFS_FWSTATS_ADD(diversity, num_of_packets_per_ant);
	DEBUGFS_FWSTATS_ADD(diversity, total_num_of_toggles);

	DEBUGFS_FWSTATS_ADD(thermal, irq_thr_low);
	DEBUGFS_FWSTATS_ADD(thermal, irq_thr_high);
	DEBUGFS_FWSTATS_ADD(thermal, tx_stop);
	DEBUGFS_FWSTATS_ADD(thermal, tx_resume);
	DEBUGFS_FWSTATS_ADD(thermal, false_irq);
	DEBUGFS_FWSTATS_ADD(thermal, adc_source_unexpected);

	DEBUGFS_FWSTATS_ADD(calib, fail_count);

	DEBUGFS_FWSTATS_ADD(calib, calib_count);

	DEBUGFS_FWSTATS_ADD(roaming, rssi_level);

	DEBUGFS_FWSTATS_ADD(dfs, num_of_radar_detections);

	DEBUGFS_ADD(conf, moddir);
	DEBUGFS_ADD(radar_detection, moddir);
	DEBUGFS_ADD(dynamic_fw_traces, moddir);

	return 0;

err:
	if (IS_ERR(entry))
		ret = PTR_ERR(entry);
	else
		ret = -ENOMEM;

	return ret;
}
