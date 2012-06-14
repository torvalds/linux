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

#include "wl18xx.h"
#include "acx.h"
#include "debugfs.h"

#define WL18XX_DEBUGFS_FWSTATS_FILE(a, b, c) \
	DEBUGFS_FWSTATS_FILE(a, b, c, wl18xx_acx_statistics)
#define WL18XX_DEBUGFS_FWSTATS_FILE_ARRAY(a, b, c) \
	DEBUGFS_FWSTATS_FILE_ARRAY(a, b, c, wl18xx_acx_statistics)


WL18XX_DEBUGFS_FWSTATS_FILE(debug, debug1, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(debug, debug2, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(debug, debug3, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(debug, debug4, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(debug, debug5, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(debug, debug6, "%u");

WL18XX_DEBUGFS_FWSTATS_FILE(error, error_frame, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(error, error_null_Frame_tx_start, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(error, error_numll_frame_cts_start, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(error, error_bar_retry, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(error, error_frame_cts_nul_flid, "%u");

WL18XX_DEBUGFS_FWSTATS_FILE(tx, tx_prepared_descs, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(tx, tx_cmplt, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(tx, tx_template_prepared, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(tx, tx_data_prepared, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(tx, tx_template_programmed, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(tx, tx_data_programmed, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(tx, tx_burst_programmed, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(tx, tx_starts, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(tx, tx_imm_resp, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(tx, tx_start_templates, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(tx, tx_start_int_templates, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(tx, tx_start_fw_gen, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(tx, tx_start_data, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(tx, tx_start_null_frame, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(tx, tx_exch, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(tx, tx_retry_template, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(tx, tx_retry_data, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(tx, tx_exch_pending, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(tx, tx_exch_expiry, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(tx, tx_done_template, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(tx, tx_done_data, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(tx, tx_done_int_template, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(tx, tx_frame_checksum, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(tx, tx_checksum_result, "%u");
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
WL18XX_DEBUGFS_FWSTATS_FILE(rx, rx_timeout_wa, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(rx, rx_wa_density_dropped_frame, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(rx, rx_wa_ba_not_expected, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(rx, rx_frame_checksum, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(rx, rx_checksum_result, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(rx, defrag_called, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(rx, defrag_init_called, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(rx, defrag_in_process_called, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(rx, defrag_tkip_called, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(rx, defrag_need_defrag, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(rx, defrag_decrypt_failed, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(rx, decrypt_key_not_found, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(rx, defrag_need_decrypt, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(rx, rx_tkip_replays, "%u");

WL18XX_DEBUGFS_FWSTATS_FILE(isr, irqs, "%u");

WL18XX_DEBUGFS_FWSTATS_FILE(pwr, missing_bcns_cnt, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(pwr, rcvd_bcns_cnt, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(pwr, connection_out_of_sync, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE_ARRAY(pwr, cont_miss_bcns_spread,
				  PWR_STAT_MAX_CONT_MISSED_BCNS_SPREAD);
WL18XX_DEBUGFS_FWSTATS_FILE(pwr, rcvd_awake_bcns_cnt, "%u");


WL18XX_DEBUGFS_FWSTATS_FILE(ps_poll, ps_poll_timeouts, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(ps_poll, upsd_timeouts, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(ps_poll, upsd_max_ap_turn, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(ps_poll, ps_poll_max_ap_turn, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(ps_poll, ps_poll_utilization, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(ps_poll, upsd_utilization, "%u");

WL18XX_DEBUGFS_FWSTATS_FILE(rx_filter, beacon_filter, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(rx_filter, arp_filter, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(rx_filter, mc_filter, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(rx_filter, dup_filter, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(rx_filter, data_filter, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(rx_filter, ibss_filter, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(rx_filter, protection_filter, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(rx_filter, accum_arp_pend_requests, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(rx_filter, max_arp_queue_dep, "%u");

WL18XX_DEBUGFS_FWSTATS_FILE(rx_rate, rx_frames_per_rates, "%u");

WL18XX_DEBUGFS_FWSTATS_FILE_ARRAY(aggr_size, tx_agg_vs_rate,
				  AGGR_STATS_TX_AGG*AGGR_STATS_TX_RATE);
WL18XX_DEBUGFS_FWSTATS_FILE_ARRAY(aggr_size, rx_size,
				  AGGR_STATS_RX_SIZE_LEN);

WL18XX_DEBUGFS_FWSTATS_FILE(pipeline, hs_tx_stat_fifo_int, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(pipeline, tcp_tx_stat_fifo_int, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(pipeline, tcp_rx_stat_fifo_int, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(pipeline, enc_tx_stat_fifo_int, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(pipeline, enc_rx_stat_fifo_int, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(pipeline, rx_complete_stat_fifo_int, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(pipeline, pre_proc_swi, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(pipeline, post_proc_swi, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(pipeline, sec_frag_swi, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(pipeline, pre_to_defrag_swi, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(pipeline, defrag_to_csum_swi, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(pipeline, csum_to_rx_xfer_swi, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(pipeline, dec_packet_in, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(pipeline, dec_packet_in_fifo_full, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(pipeline, dec_packet_out, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(pipeline, cs_rx_packet_in, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(pipeline, cs_rx_packet_out, "%u");

WL18XX_DEBUGFS_FWSTATS_FILE_ARRAY(pipeline, pipeline_fifo_full,
				  PIPE_STATS_HW_FIFO);

WL18XX_DEBUGFS_FWSTATS_FILE(mem, rx_free_mem_blks, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(mem, tx_free_mem_blks, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(mem, fwlog_free_mem_blks, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(mem, fw_gen_free_mem_blks, "%u");

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

	if (wl->state == WL1271_STATE_OFF)
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

	DEBUGFS_FWSTATS_ADD(debug, debug1);
	DEBUGFS_FWSTATS_ADD(debug, debug2);
	DEBUGFS_FWSTATS_ADD(debug, debug3);
	DEBUGFS_FWSTATS_ADD(debug, debug4);
	DEBUGFS_FWSTATS_ADD(debug, debug5);
	DEBUGFS_FWSTATS_ADD(debug, debug6);

	DEBUGFS_FWSTATS_ADD(error, error_frame);
	DEBUGFS_FWSTATS_ADD(error, error_null_Frame_tx_start);
	DEBUGFS_FWSTATS_ADD(error, error_numll_frame_cts_start);
	DEBUGFS_FWSTATS_ADD(error, error_bar_retry);
	DEBUGFS_FWSTATS_ADD(error, error_frame_cts_nul_flid);

	DEBUGFS_FWSTATS_ADD(tx, tx_prepared_descs);
	DEBUGFS_FWSTATS_ADD(tx, tx_cmplt);
	DEBUGFS_FWSTATS_ADD(tx, tx_template_prepared);
	DEBUGFS_FWSTATS_ADD(tx, tx_data_prepared);
	DEBUGFS_FWSTATS_ADD(tx, tx_template_programmed);
	DEBUGFS_FWSTATS_ADD(tx, tx_data_programmed);
	DEBUGFS_FWSTATS_ADD(tx, tx_burst_programmed);
	DEBUGFS_FWSTATS_ADD(tx, tx_starts);
	DEBUGFS_FWSTATS_ADD(tx, tx_imm_resp);
	DEBUGFS_FWSTATS_ADD(tx, tx_start_templates);
	DEBUGFS_FWSTATS_ADD(tx, tx_start_int_templates);
	DEBUGFS_FWSTATS_ADD(tx, tx_start_fw_gen);
	DEBUGFS_FWSTATS_ADD(tx, tx_start_data);
	DEBUGFS_FWSTATS_ADD(tx, tx_start_null_frame);
	DEBUGFS_FWSTATS_ADD(tx, tx_exch);
	DEBUGFS_FWSTATS_ADD(tx, tx_retry_template);
	DEBUGFS_FWSTATS_ADD(tx, tx_retry_data);
	DEBUGFS_FWSTATS_ADD(tx, tx_exch_pending);
	DEBUGFS_FWSTATS_ADD(tx, tx_exch_expiry);
	DEBUGFS_FWSTATS_ADD(tx, tx_done_template);
	DEBUGFS_FWSTATS_ADD(tx, tx_done_data);
	DEBUGFS_FWSTATS_ADD(tx, tx_done_int_template);
	DEBUGFS_FWSTATS_ADD(tx, tx_frame_checksum);
	DEBUGFS_FWSTATS_ADD(tx, tx_checksum_result);
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
	DEBUGFS_FWSTATS_ADD(rx, rx_timeout_wa);
	DEBUGFS_FWSTATS_ADD(rx, rx_wa_density_dropped_frame);
	DEBUGFS_FWSTATS_ADD(rx, rx_wa_ba_not_expected);
	DEBUGFS_FWSTATS_ADD(rx, rx_frame_checksum);
	DEBUGFS_FWSTATS_ADD(rx, rx_checksum_result);
	DEBUGFS_FWSTATS_ADD(rx, defrag_called);
	DEBUGFS_FWSTATS_ADD(rx, defrag_init_called);
	DEBUGFS_FWSTATS_ADD(rx, defrag_in_process_called);
	DEBUGFS_FWSTATS_ADD(rx, defrag_tkip_called);
	DEBUGFS_FWSTATS_ADD(rx, defrag_need_defrag);
	DEBUGFS_FWSTATS_ADD(rx, defrag_decrypt_failed);
	DEBUGFS_FWSTATS_ADD(rx, decrypt_key_not_found);
	DEBUGFS_FWSTATS_ADD(rx, defrag_need_decrypt);
	DEBUGFS_FWSTATS_ADD(rx, rx_tkip_replays);

	DEBUGFS_FWSTATS_ADD(isr, irqs);

	DEBUGFS_FWSTATS_ADD(pwr, missing_bcns_cnt);
	DEBUGFS_FWSTATS_ADD(pwr, rcvd_bcns_cnt);
	DEBUGFS_FWSTATS_ADD(pwr, connection_out_of_sync);
	DEBUGFS_FWSTATS_ADD(pwr, cont_miss_bcns_spread);
	DEBUGFS_FWSTATS_ADD(pwr, rcvd_awake_bcns_cnt);

	DEBUGFS_FWSTATS_ADD(ps_poll, ps_poll_timeouts);
	DEBUGFS_FWSTATS_ADD(ps_poll, upsd_timeouts);
	DEBUGFS_FWSTATS_ADD(ps_poll, upsd_max_ap_turn);
	DEBUGFS_FWSTATS_ADD(ps_poll, ps_poll_max_ap_turn);
	DEBUGFS_FWSTATS_ADD(ps_poll, ps_poll_utilization);
	DEBUGFS_FWSTATS_ADD(ps_poll, upsd_utilization);

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

	DEBUGFS_FWSTATS_ADD(aggr_size, tx_agg_vs_rate);
	DEBUGFS_FWSTATS_ADD(aggr_size, rx_size);

	DEBUGFS_FWSTATS_ADD(pipeline, hs_tx_stat_fifo_int);
	DEBUGFS_FWSTATS_ADD(pipeline, tcp_tx_stat_fifo_int);
	DEBUGFS_FWSTATS_ADD(pipeline, tcp_rx_stat_fifo_int);
	DEBUGFS_FWSTATS_ADD(pipeline, enc_tx_stat_fifo_int);
	DEBUGFS_FWSTATS_ADD(pipeline, enc_rx_stat_fifo_int);
	DEBUGFS_FWSTATS_ADD(pipeline, rx_complete_stat_fifo_int);
	DEBUGFS_FWSTATS_ADD(pipeline, pre_proc_swi);
	DEBUGFS_FWSTATS_ADD(pipeline, post_proc_swi);
	DEBUGFS_FWSTATS_ADD(pipeline, sec_frag_swi);
	DEBUGFS_FWSTATS_ADD(pipeline, pre_to_defrag_swi);
	DEBUGFS_FWSTATS_ADD(pipeline, defrag_to_csum_swi);
	DEBUGFS_FWSTATS_ADD(pipeline, csum_to_rx_xfer_swi);
	DEBUGFS_FWSTATS_ADD(pipeline, dec_packet_in);
	DEBUGFS_FWSTATS_ADD(pipeline, dec_packet_in_fifo_full);
	DEBUGFS_FWSTATS_ADD(pipeline, dec_packet_out);
	DEBUGFS_FWSTATS_ADD(pipeline, cs_rx_packet_in);
	DEBUGFS_FWSTATS_ADD(pipeline, cs_rx_packet_out);
	DEBUGFS_FWSTATS_ADD(pipeline, pipeline_fifo_full);

	DEBUGFS_FWSTATS_ADD(mem, rx_free_mem_blks);
	DEBUGFS_FWSTATS_ADD(mem, tx_free_mem_blks);
	DEBUGFS_FWSTATS_ADD(mem, fwlog_free_mem_blks);
	DEBUGFS_FWSTATS_ADD(mem, fw_gen_free_mem_blks);

	DEBUGFS_ADD(conf, moddir);

	return 0;

err:
	if (IS_ERR(entry))
		ret = PTR_ERR(entry);
	else
		ret = -ENOMEM;

	return ret;
}
