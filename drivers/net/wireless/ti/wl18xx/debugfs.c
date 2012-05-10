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

WL18XX_DEBUGFS_FWSTATS_FILE(ring, tx_procs, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(ring, prepared_descs, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(ring, tx_xfr, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(ring, tx_dma, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(ring, tx_cmplt, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(ring, rx_procs, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(ring, rx_data, "%u");

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
WL18XX_DEBUGFS_FWSTATS_FILE(tx, tx_exch_mismatch, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(tx, tx_done_template, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(tx, tx_done_data, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(tx, tx_done_int_template, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(tx, tx_pre_xfr, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(tx, tx_xfr, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(tx, tx_xfr_out_of_mem, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(tx, tx_dma_programmed, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(tx, tx_dma_done, "%u");

WL18XX_DEBUGFS_FWSTATS_FILE(rx, rx_out_of_mem, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(rx, rx_hdr_overflow, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(rx, rx_hw_stuck, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(rx, rx_dropped_frame, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(rx, rx_complete_dropped_frame, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(rx, rx_alloc_frame, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(rx, rx_done_queue, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(rx, rx_done, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(rx, rx_defrag, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(rx, rx_defrag_end, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(rx, rx_mic, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(rx, rx_mic_end, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(rx, rx_xfr, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(rx, rx_xfr_end, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(rx, rx_cmplt, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(rx, rx_pre_complt, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(rx, rx_cmplt_task, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(rx, rx_phy_hdr, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(rx, rx_timeout, "%u");

WL18XX_DEBUGFS_FWSTATS_FILE(dma, rx_dma_errors, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(dma, tx_dma_errors, "%u");

WL18XX_DEBUGFS_FWSTATS_FILE(isr, irqs, "%u");

WL18XX_DEBUGFS_FWSTATS_FILE(wep, wep_add_key_count, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(wep, wep_default_key_count, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(wep, wep_key_not_found, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(wep, wep_decrypt_fail, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(wep, wep_encrypt_fail, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(wep, wep_dec_packets, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(wep, wep_dec_interrupt, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(wep, wep_enc_packets, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(wep, wep_enc_interrupts, "%u");

WL18XX_DEBUGFS_FWSTATS_FILE(pwr, missing_bcns_cnt, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(pwr, rcvd_bcns_cnt, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(pwr, connection_out_of_sync, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE_ARRAY(pwr, cont_miss_bcns_spread,
				  PWR_STAT_MAX_CONT_MISSED_BCNS_SPREAD);
WL18XX_DEBUGFS_FWSTATS_FILE(pwr, rcvd_awake_bcns_cnt, "%u");

WL18XX_DEBUGFS_FWSTATS_FILE(mic, mic_rx_pkts, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(mic, mic_calc_failure, "%u");

WL18XX_DEBUGFS_FWSTATS_FILE(aes, aes_encrypt_fail, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(aes, aes_decrypt_fail, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(aes, aes_encrypt_packets, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(aes, aes_decrypt_packets, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(aes, aes_encrypt_interrupt, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(aes, aes_decrypt_interrupt, "%u");

WL18XX_DEBUGFS_FWSTATS_FILE(gem, gem_encrypt_fail, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(gem, gem_decrypt_fail, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(gem, gem_encrypt_packets, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(gem, gem_decrypt_packets, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(gem, gem_encrypt_interrupt, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(gem, gem_decrypt_interrupt, "%u");

WL18XX_DEBUGFS_FWSTATS_FILE(event, calibration, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(event, rx_mismatch, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(event, rx_mem_empty, "%u");

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

WL18XX_DEBUGFS_FWSTATS_FILE(calibration, init_cal_total, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(calibration, init_radio_bands_fail, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(calibration, init_set_params, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(calibration, init_tx_clpc_fail, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(calibration, init_rx_iw_mm_fail, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(calibration, tune_cal_total, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(calibration, tune_drpw_rtrim_fail, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(calibration, tune_drpw_pd_buf_fail, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(calibration, tune_drpw_tx_mix_freq_fail, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(calibration, tune_drpw_ta_cal, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(calibration, tune_drpw_rx_if_2_gain, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(calibration, tune_drpw_rx_dac, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(calibration, tune_drpw_chan_tune, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(calibration, tune_drpw_rx_tx_lpf, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(calibration, tune_drpw_lna_tank, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(calibration, tune_tx_lo_leak_fail, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(calibration, tune_tx_iq_mm_fail, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(calibration, tune_tx_pdet_fail, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(calibration, tune_tx_ppa_fail, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(calibration, tune_tx_clpc_fail, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(calibration, tune_rx_ana_dc_fail, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(calibration, tune_rx_dig_dc_fail, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(calibration, tune_rx_iq_mm_fail, "%u");
WL18XX_DEBUGFS_FWSTATS_FILE(calibration, cal_state_fail, "%u");

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

	DEBUGFS_FWSTATS_ADD(debug, debug1);
	DEBUGFS_FWSTATS_ADD(debug, debug2);
	DEBUGFS_FWSTATS_ADD(debug, debug3);
	DEBUGFS_FWSTATS_ADD(debug, debug4);
	DEBUGFS_FWSTATS_ADD(debug, debug5);
	DEBUGFS_FWSTATS_ADD(debug, debug6);

	DEBUGFS_FWSTATS_ADD(ring, tx_procs);
	DEBUGFS_FWSTATS_ADD(ring, prepared_descs);
	DEBUGFS_FWSTATS_ADD(ring, tx_xfr);
	DEBUGFS_FWSTATS_ADD(ring, tx_dma);
	DEBUGFS_FWSTATS_ADD(ring, tx_cmplt);
	DEBUGFS_FWSTATS_ADD(ring, rx_procs);
	DEBUGFS_FWSTATS_ADD(ring, rx_data);

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
	DEBUGFS_FWSTATS_ADD(tx, tx_exch_mismatch);
	DEBUGFS_FWSTATS_ADD(tx, tx_done_template);
	DEBUGFS_FWSTATS_ADD(tx, tx_done_data);
	DEBUGFS_FWSTATS_ADD(tx, tx_done_int_template);
	DEBUGFS_FWSTATS_ADD(tx, tx_pre_xfr);
	DEBUGFS_FWSTATS_ADD(tx, tx_xfr);
	DEBUGFS_FWSTATS_ADD(tx, tx_xfr_out_of_mem);
	DEBUGFS_FWSTATS_ADD(tx, tx_dma_programmed);
	DEBUGFS_FWSTATS_ADD(tx, tx_dma_done);

	DEBUGFS_FWSTATS_ADD(rx, rx_out_of_mem);
	DEBUGFS_FWSTATS_ADD(rx, rx_hdr_overflow);
	DEBUGFS_FWSTATS_ADD(rx, rx_hw_stuck);
	DEBUGFS_FWSTATS_ADD(rx, rx_dropped_frame);
	DEBUGFS_FWSTATS_ADD(rx, rx_complete_dropped_frame);
	DEBUGFS_FWSTATS_ADD(rx, rx_alloc_frame);
	DEBUGFS_FWSTATS_ADD(rx, rx_done_queue);
	DEBUGFS_FWSTATS_ADD(rx, rx_done);
	DEBUGFS_FWSTATS_ADD(rx, rx_defrag);
	DEBUGFS_FWSTATS_ADD(rx, rx_defrag_end);
	DEBUGFS_FWSTATS_ADD(rx, rx_mic);
	DEBUGFS_FWSTATS_ADD(rx, rx_mic_end);
	DEBUGFS_FWSTATS_ADD(rx, rx_xfr);
	DEBUGFS_FWSTATS_ADD(rx, rx_xfr_end);
	DEBUGFS_FWSTATS_ADD(rx, rx_cmplt);
	DEBUGFS_FWSTATS_ADD(rx, rx_pre_complt);
	DEBUGFS_FWSTATS_ADD(rx, rx_cmplt_task);
	DEBUGFS_FWSTATS_ADD(rx, rx_phy_hdr);
	DEBUGFS_FWSTATS_ADD(rx, rx_timeout);

	DEBUGFS_FWSTATS_ADD(dma, rx_dma_errors);
	DEBUGFS_FWSTATS_ADD(dma, tx_dma_errors);

	DEBUGFS_FWSTATS_ADD(isr, irqs);

	DEBUGFS_FWSTATS_ADD(wep, wep_add_key_count);
	DEBUGFS_FWSTATS_ADD(wep, wep_default_key_count);
	DEBUGFS_FWSTATS_ADD(wep, wep_key_not_found);
	DEBUGFS_FWSTATS_ADD(wep, wep_decrypt_fail);
	DEBUGFS_FWSTATS_ADD(wep, wep_encrypt_fail);
	DEBUGFS_FWSTATS_ADD(wep, wep_dec_packets);
	DEBUGFS_FWSTATS_ADD(wep, wep_dec_interrupt);
	DEBUGFS_FWSTATS_ADD(wep, wep_enc_packets);
	DEBUGFS_FWSTATS_ADD(wep, wep_enc_interrupts);

	DEBUGFS_FWSTATS_ADD(pwr, missing_bcns_cnt);
	DEBUGFS_FWSTATS_ADD(pwr, rcvd_bcns_cnt);
	DEBUGFS_FWSTATS_ADD(pwr, connection_out_of_sync);
	DEBUGFS_FWSTATS_ADD(pwr, cont_miss_bcns_spread);
	DEBUGFS_FWSTATS_ADD(pwr, rcvd_awake_bcns_cnt);

	DEBUGFS_FWSTATS_ADD(mic, mic_rx_pkts);
	DEBUGFS_FWSTATS_ADD(mic, mic_calc_failure);

	DEBUGFS_FWSTATS_ADD(aes, aes_encrypt_fail);
	DEBUGFS_FWSTATS_ADD(aes, aes_decrypt_fail);
	DEBUGFS_FWSTATS_ADD(aes, aes_encrypt_packets);
	DEBUGFS_FWSTATS_ADD(aes, aes_decrypt_packets);
	DEBUGFS_FWSTATS_ADD(aes, aes_encrypt_interrupt);
	DEBUGFS_FWSTATS_ADD(aes, aes_decrypt_interrupt);

	DEBUGFS_FWSTATS_ADD(gem, gem_encrypt_fail);
	DEBUGFS_FWSTATS_ADD(gem, gem_decrypt_fail);
	DEBUGFS_FWSTATS_ADD(gem, gem_encrypt_packets);
	DEBUGFS_FWSTATS_ADD(gem, gem_decrypt_packets);
	DEBUGFS_FWSTATS_ADD(gem, gem_encrypt_interrupt);
	DEBUGFS_FWSTATS_ADD(gem, gem_decrypt_interrupt);

	DEBUGFS_FWSTATS_ADD(event, calibration);
	DEBUGFS_FWSTATS_ADD(event, rx_mismatch);
	DEBUGFS_FWSTATS_ADD(event, rx_mem_empty);

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

	DEBUGFS_FWSTATS_ADD(calibration, init_cal_total);
	DEBUGFS_FWSTATS_ADD(calibration, init_radio_bands_fail);
	DEBUGFS_FWSTATS_ADD(calibration, init_set_params);
	DEBUGFS_FWSTATS_ADD(calibration, init_tx_clpc_fail);
	DEBUGFS_FWSTATS_ADD(calibration, init_rx_iw_mm_fail);
	DEBUGFS_FWSTATS_ADD(calibration, tune_cal_total);
	DEBUGFS_FWSTATS_ADD(calibration, tune_drpw_rtrim_fail);
	DEBUGFS_FWSTATS_ADD(calibration, tune_drpw_pd_buf_fail);
	DEBUGFS_FWSTATS_ADD(calibration, tune_drpw_tx_mix_freq_fail);
	DEBUGFS_FWSTATS_ADD(calibration, tune_drpw_ta_cal);
	DEBUGFS_FWSTATS_ADD(calibration, tune_drpw_rx_if_2_gain);
	DEBUGFS_FWSTATS_ADD(calibration, tune_drpw_rx_dac);
	DEBUGFS_FWSTATS_ADD(calibration, tune_drpw_chan_tune);
	DEBUGFS_FWSTATS_ADD(calibration, tune_drpw_rx_tx_lpf);
	DEBUGFS_FWSTATS_ADD(calibration, tune_drpw_lna_tank);
	DEBUGFS_FWSTATS_ADD(calibration, tune_tx_lo_leak_fail);
	DEBUGFS_FWSTATS_ADD(calibration, tune_tx_iq_mm_fail);
	DEBUGFS_FWSTATS_ADD(calibration, tune_tx_pdet_fail);
	DEBUGFS_FWSTATS_ADD(calibration, tune_tx_ppa_fail);
	DEBUGFS_FWSTATS_ADD(calibration, tune_tx_clpc_fail);
	DEBUGFS_FWSTATS_ADD(calibration, tune_rx_ana_dc_fail);
	DEBUGFS_FWSTATS_ADD(calibration, tune_rx_dig_dc_fail);
	DEBUGFS_FWSTATS_ADD(calibration, tune_rx_iq_mm_fail);
	DEBUGFS_FWSTATS_ADD(calibration, cal_state_fail);

	return 0;

err:
	if (IS_ERR(entry))
		ret = PTR_ERR(entry);
	else
		ret = -ENOMEM;

	return ret;
}
