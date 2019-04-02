/*
 * This file is part of wl12xx
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

#include "../wlcore/defs.h"
#include "../wlcore/wlcore.h"

#include "wl12xx.h"
#include "acx.h"
#include "defs.h"

#define WL12XX_DEFS_FWSTATS_FILE(a, b, c) \
	DEFS_FWSTATS_FILE(a, b, c, wl12xx_acx_statistics)

WL12XX_DEFS_FWSTATS_FILE(tx, internal_desc_overflow, "%u");

WL12XX_DEFS_FWSTATS_FILE(rx, out_of_mem, "%u");
WL12XX_DEFS_FWSTATS_FILE(rx, hdr_overflow, "%u");
WL12XX_DEFS_FWSTATS_FILE(rx, hw_stuck, "%u");
WL12XX_DEFS_FWSTATS_FILE(rx, dropped, "%u");
WL12XX_DEFS_FWSTATS_FILE(rx, fcs_err, "%u");
WL12XX_DEFS_FWSTATS_FILE(rx, xfr_hint_trig, "%u");
WL12XX_DEFS_FWSTATS_FILE(rx, path_reset, "%u");
WL12XX_DEFS_FWSTATS_FILE(rx, reset_counter, "%u");

WL12XX_DEFS_FWSTATS_FILE(dma, rx_requested, "%u");
WL12XX_DEFS_FWSTATS_FILE(dma, rx_errors, "%u");
WL12XX_DEFS_FWSTATS_FILE(dma, tx_requested, "%u");
WL12XX_DEFS_FWSTATS_FILE(dma, tx_errors, "%u");

WL12XX_DEFS_FWSTATS_FILE(isr, cmd_cmplt, "%u");
WL12XX_DEFS_FWSTATS_FILE(isr, fiqs, "%u");
WL12XX_DEFS_FWSTATS_FILE(isr, rx_headers, "%u");
WL12XX_DEFS_FWSTATS_FILE(isr, rx_mem_overflow, "%u");
WL12XX_DEFS_FWSTATS_FILE(isr, rx_rdys, "%u");
WL12XX_DEFS_FWSTATS_FILE(isr, irqs, "%u");
WL12XX_DEFS_FWSTATS_FILE(isr, tx_procs, "%u");
WL12XX_DEFS_FWSTATS_FILE(isr, decrypt_done, "%u");
WL12XX_DEFS_FWSTATS_FILE(isr, dma0_done, "%u");
WL12XX_DEFS_FWSTATS_FILE(isr, dma1_done, "%u");
WL12XX_DEFS_FWSTATS_FILE(isr, tx_exch_complete, "%u");
WL12XX_DEFS_FWSTATS_FILE(isr, commands, "%u");
WL12XX_DEFS_FWSTATS_FILE(isr, rx_procs, "%u");
WL12XX_DEFS_FWSTATS_FILE(isr, hw_pm_mode_changes, "%u");
WL12XX_DEFS_FWSTATS_FILE(isr, host_acknowledges, "%u");
WL12XX_DEFS_FWSTATS_FILE(isr, pci_pm, "%u");
WL12XX_DEFS_FWSTATS_FILE(isr, wakeups, "%u");
WL12XX_DEFS_FWSTATS_FILE(isr, low_rssi, "%u");

WL12XX_DEFS_FWSTATS_FILE(wep, addr_key_count, "%u");
WL12XX_DEFS_FWSTATS_FILE(wep, default_key_count, "%u");
/* skipping wep.reserved */
WL12XX_DEFS_FWSTATS_FILE(wep, key_not_found, "%u");
WL12XX_DEFS_FWSTATS_FILE(wep, decrypt_fail, "%u");
WL12XX_DEFS_FWSTATS_FILE(wep, packets, "%u");
WL12XX_DEFS_FWSTATS_FILE(wep, interrupt, "%u");

WL12XX_DEFS_FWSTATS_FILE(pwr, ps_enter, "%u");
WL12XX_DEFS_FWSTATS_FILE(pwr, elp_enter, "%u");
WL12XX_DEFS_FWSTATS_FILE(pwr, missing_bcns, "%u");
WL12XX_DEFS_FWSTATS_FILE(pwr, wake_on_host, "%u");
WL12XX_DEFS_FWSTATS_FILE(pwr, wake_on_timer_exp, "%u");
WL12XX_DEFS_FWSTATS_FILE(pwr, tx_with_ps, "%u");
WL12XX_DEFS_FWSTATS_FILE(pwr, tx_without_ps, "%u");
WL12XX_DEFS_FWSTATS_FILE(pwr, rcvd_beacons, "%u");
WL12XX_DEFS_FWSTATS_FILE(pwr, power_save_off, "%u");
WL12XX_DEFS_FWSTATS_FILE(pwr, enable_ps, "%u");
WL12XX_DEFS_FWSTATS_FILE(pwr, disable_ps, "%u");
WL12XX_DEFS_FWSTATS_FILE(pwr, fix_tsf_ps, "%u");
/* skipping cont_miss_bcns_spread for now */
WL12XX_DEFS_FWSTATS_FILE(pwr, rcvd_awake_beacons, "%u");

WL12XX_DEFS_FWSTATS_FILE(mic, rx_pkts, "%u");
WL12XX_DEFS_FWSTATS_FILE(mic, calc_failure, "%u");

WL12XX_DEFS_FWSTATS_FILE(aes, encrypt_fail, "%u");
WL12XX_DEFS_FWSTATS_FILE(aes, decrypt_fail, "%u");
WL12XX_DEFS_FWSTATS_FILE(aes, encrypt_packets, "%u");
WL12XX_DEFS_FWSTATS_FILE(aes, decrypt_packets, "%u");
WL12XX_DEFS_FWSTATS_FILE(aes, encrypt_interrupt, "%u");
WL12XX_DEFS_FWSTATS_FILE(aes, decrypt_interrupt, "%u");

WL12XX_DEFS_FWSTATS_FILE(event, heart_beat, "%u");
WL12XX_DEFS_FWSTATS_FILE(event, calibration, "%u");
WL12XX_DEFS_FWSTATS_FILE(event, rx_mismatch, "%u");
WL12XX_DEFS_FWSTATS_FILE(event, rx_mem_empty, "%u");
WL12XX_DEFS_FWSTATS_FILE(event, rx_pool, "%u");
WL12XX_DEFS_FWSTATS_FILE(event, oom_late, "%u");
WL12XX_DEFS_FWSTATS_FILE(event, phy_transmit_error, "%u");
WL12XX_DEFS_FWSTATS_FILE(event, tx_stuck, "%u");

WL12XX_DEFS_FWSTATS_FILE(ps, pspoll_timeouts, "%u");
WL12XX_DEFS_FWSTATS_FILE(ps, upsd_timeouts, "%u");
WL12XX_DEFS_FWSTATS_FILE(ps, upsd_max_sptime, "%u");
WL12XX_DEFS_FWSTATS_FILE(ps, upsd_max_apturn, "%u");
WL12XX_DEFS_FWSTATS_FILE(ps, pspoll_max_apturn, "%u");
WL12XX_DEFS_FWSTATS_FILE(ps, pspoll_utilization, "%u");
WL12XX_DEFS_FWSTATS_FILE(ps, upsd_utilization, "%u");

WL12XX_DEFS_FWSTATS_FILE(rxpipe, rx_prep_beacon_drop, "%u");
WL12XX_DEFS_FWSTATS_FILE(rxpipe, descr_host_int_trig_rx_data, "%u");
WL12XX_DEFS_FWSTATS_FILE(rxpipe, beacon_buffer_thres_host_int_trig_rx_data,
			    "%u");
WL12XX_DEFS_FWSTATS_FILE(rxpipe, missed_beacon_host_int_trig_rx_data, "%u");
WL12XX_DEFS_FWSTATS_FILE(rxpipe, tx_xfr_host_int_trig_rx_data, "%u");

int wl12xx_defs_add_files(struct wl1271 *wl,
			     struct dentry *rootdir)
{
	struct dentry *stats, *moddir;

	moddir = defs_create_dir(KBUILD_MODNAME, rootdir);
	stats = defs_create_dir("fw_stats", moddir);

	DEFS_FWSTATS_ADD(tx, internal_desc_overflow);

	DEFS_FWSTATS_ADD(rx, out_of_mem);
	DEFS_FWSTATS_ADD(rx, hdr_overflow);
	DEFS_FWSTATS_ADD(rx, hw_stuck);
	DEFS_FWSTATS_ADD(rx, dropped);
	DEFS_FWSTATS_ADD(rx, fcs_err);
	DEFS_FWSTATS_ADD(rx, xfr_hint_trig);
	DEFS_FWSTATS_ADD(rx, path_reset);
	DEFS_FWSTATS_ADD(rx, reset_counter);

	DEFS_FWSTATS_ADD(dma, rx_requested);
	DEFS_FWSTATS_ADD(dma, rx_errors);
	DEFS_FWSTATS_ADD(dma, tx_requested);
	DEFS_FWSTATS_ADD(dma, tx_errors);

	DEFS_FWSTATS_ADD(isr, cmd_cmplt);
	DEFS_FWSTATS_ADD(isr, fiqs);
	DEFS_FWSTATS_ADD(isr, rx_headers);
	DEFS_FWSTATS_ADD(isr, rx_mem_overflow);
	DEFS_FWSTATS_ADD(isr, rx_rdys);
	DEFS_FWSTATS_ADD(isr, irqs);
	DEFS_FWSTATS_ADD(isr, tx_procs);
	DEFS_FWSTATS_ADD(isr, decrypt_done);
	DEFS_FWSTATS_ADD(isr, dma0_done);
	DEFS_FWSTATS_ADD(isr, dma1_done);
	DEFS_FWSTATS_ADD(isr, tx_exch_complete);
	DEFS_FWSTATS_ADD(isr, commands);
	DEFS_FWSTATS_ADD(isr, rx_procs);
	DEFS_FWSTATS_ADD(isr, hw_pm_mode_changes);
	DEFS_FWSTATS_ADD(isr, host_acknowledges);
	DEFS_FWSTATS_ADD(isr, pci_pm);
	DEFS_FWSTATS_ADD(isr, wakeups);
	DEFS_FWSTATS_ADD(isr, low_rssi);

	DEFS_FWSTATS_ADD(wep, addr_key_count);
	DEFS_FWSTATS_ADD(wep, default_key_count);
	/* skipping wep.reserved */
	DEFS_FWSTATS_ADD(wep, key_not_found);
	DEFS_FWSTATS_ADD(wep, decrypt_fail);
	DEFS_FWSTATS_ADD(wep, packets);
	DEFS_FWSTATS_ADD(wep, interrupt);

	DEFS_FWSTATS_ADD(pwr, ps_enter);
	DEFS_FWSTATS_ADD(pwr, elp_enter);
	DEFS_FWSTATS_ADD(pwr, missing_bcns);
	DEFS_FWSTATS_ADD(pwr, wake_on_host);
	DEFS_FWSTATS_ADD(pwr, wake_on_timer_exp);
	DEFS_FWSTATS_ADD(pwr, tx_with_ps);
	DEFS_FWSTATS_ADD(pwr, tx_without_ps);
	DEFS_FWSTATS_ADD(pwr, rcvd_beacons);
	DEFS_FWSTATS_ADD(pwr, power_save_off);
	DEFS_FWSTATS_ADD(pwr, enable_ps);
	DEFS_FWSTATS_ADD(pwr, disable_ps);
	DEFS_FWSTATS_ADD(pwr, fix_tsf_ps);
	/* skipping cont_miss_bcns_spread for now */
	DEFS_FWSTATS_ADD(pwr, rcvd_awake_beacons);

	DEFS_FWSTATS_ADD(mic, rx_pkts);
	DEFS_FWSTATS_ADD(mic, calc_failure);

	DEFS_FWSTATS_ADD(aes, encrypt_fail);
	DEFS_FWSTATS_ADD(aes, decrypt_fail);
	DEFS_FWSTATS_ADD(aes, encrypt_packets);
	DEFS_FWSTATS_ADD(aes, decrypt_packets);
	DEFS_FWSTATS_ADD(aes, encrypt_interrupt);
	DEFS_FWSTATS_ADD(aes, decrypt_interrupt);

	DEFS_FWSTATS_ADD(event, heart_beat);
	DEFS_FWSTATS_ADD(event, calibration);
	DEFS_FWSTATS_ADD(event, rx_mismatch);
	DEFS_FWSTATS_ADD(event, rx_mem_empty);
	DEFS_FWSTATS_ADD(event, rx_pool);
	DEFS_FWSTATS_ADD(event, oom_late);
	DEFS_FWSTATS_ADD(event, phy_transmit_error);
	DEFS_FWSTATS_ADD(event, tx_stuck);

	DEFS_FWSTATS_ADD(ps, pspoll_timeouts);
	DEFS_FWSTATS_ADD(ps, upsd_timeouts);
	DEFS_FWSTATS_ADD(ps, upsd_max_sptime);
	DEFS_FWSTATS_ADD(ps, upsd_max_apturn);
	DEFS_FWSTATS_ADD(ps, pspoll_max_apturn);
	DEFS_FWSTATS_ADD(ps, pspoll_utilization);
	DEFS_FWSTATS_ADD(ps, upsd_utilization);

	DEFS_FWSTATS_ADD(rxpipe, rx_prep_beacon_drop);
	DEFS_FWSTATS_ADD(rxpipe, descr_host_int_trig_rx_data);
	DEFS_FWSTATS_ADD(rxpipe, beacon_buffer_thres_host_int_trig_rx_data);
	DEFS_FWSTATS_ADD(rxpipe, missed_beacon_host_int_trig_rx_data);
	DEFS_FWSTATS_ADD(rxpipe, tx_xfr_host_int_trig_rx_data);

	return 0;
}
