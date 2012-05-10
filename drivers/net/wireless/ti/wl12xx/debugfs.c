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

#include "../wlcore/debugfs.h"
#include "../wlcore/wlcore.h"

#include "wl12xx.h"
#include "acx.h"
#include "debugfs.h"

#define WL12XX_DEBUGFS_FWSTATS_FILE(a, b, c) \
	DEBUGFS_FWSTATS_FILE(a, b, c, wl12xx_acx_statistics)

WL12XX_DEBUGFS_FWSTATS_FILE(tx, internal_desc_overflow, "%u");

WL12XX_DEBUGFS_FWSTATS_FILE(rx, out_of_mem, "%u");
WL12XX_DEBUGFS_FWSTATS_FILE(rx, hdr_overflow, "%u");
WL12XX_DEBUGFS_FWSTATS_FILE(rx, hw_stuck, "%u");
WL12XX_DEBUGFS_FWSTATS_FILE(rx, dropped, "%u");
WL12XX_DEBUGFS_FWSTATS_FILE(rx, fcs_err, "%u");
WL12XX_DEBUGFS_FWSTATS_FILE(rx, xfr_hint_trig, "%u");
WL12XX_DEBUGFS_FWSTATS_FILE(rx, path_reset, "%u");
WL12XX_DEBUGFS_FWSTATS_FILE(rx, reset_counter, "%u");

WL12XX_DEBUGFS_FWSTATS_FILE(dma, rx_requested, "%u");
WL12XX_DEBUGFS_FWSTATS_FILE(dma, rx_errors, "%u");
WL12XX_DEBUGFS_FWSTATS_FILE(dma, tx_requested, "%u");
WL12XX_DEBUGFS_FWSTATS_FILE(dma, tx_errors, "%u");

WL12XX_DEBUGFS_FWSTATS_FILE(isr, cmd_cmplt, "%u");
WL12XX_DEBUGFS_FWSTATS_FILE(isr, fiqs, "%u");
WL12XX_DEBUGFS_FWSTATS_FILE(isr, rx_headers, "%u");
WL12XX_DEBUGFS_FWSTATS_FILE(isr, rx_mem_overflow, "%u");
WL12XX_DEBUGFS_FWSTATS_FILE(isr, rx_rdys, "%u");
WL12XX_DEBUGFS_FWSTATS_FILE(isr, irqs, "%u");
WL12XX_DEBUGFS_FWSTATS_FILE(isr, tx_procs, "%u");
WL12XX_DEBUGFS_FWSTATS_FILE(isr, decrypt_done, "%u");
WL12XX_DEBUGFS_FWSTATS_FILE(isr, dma0_done, "%u");
WL12XX_DEBUGFS_FWSTATS_FILE(isr, dma1_done, "%u");
WL12XX_DEBUGFS_FWSTATS_FILE(isr, tx_exch_complete, "%u");
WL12XX_DEBUGFS_FWSTATS_FILE(isr, commands, "%u");
WL12XX_DEBUGFS_FWSTATS_FILE(isr, rx_procs, "%u");
WL12XX_DEBUGFS_FWSTATS_FILE(isr, hw_pm_mode_changes, "%u");
WL12XX_DEBUGFS_FWSTATS_FILE(isr, host_acknowledges, "%u");
WL12XX_DEBUGFS_FWSTATS_FILE(isr, pci_pm, "%u");
WL12XX_DEBUGFS_FWSTATS_FILE(isr, wakeups, "%u");
WL12XX_DEBUGFS_FWSTATS_FILE(isr, low_rssi, "%u");

WL12XX_DEBUGFS_FWSTATS_FILE(wep, addr_key_count, "%u");
WL12XX_DEBUGFS_FWSTATS_FILE(wep, default_key_count, "%u");
/* skipping wep.reserved */
WL12XX_DEBUGFS_FWSTATS_FILE(wep, key_not_found, "%u");
WL12XX_DEBUGFS_FWSTATS_FILE(wep, decrypt_fail, "%u");
WL12XX_DEBUGFS_FWSTATS_FILE(wep, packets, "%u");
WL12XX_DEBUGFS_FWSTATS_FILE(wep, interrupt, "%u");

WL12XX_DEBUGFS_FWSTATS_FILE(pwr, ps_enter, "%u");
WL12XX_DEBUGFS_FWSTATS_FILE(pwr, elp_enter, "%u");
WL12XX_DEBUGFS_FWSTATS_FILE(pwr, missing_bcns, "%u");
WL12XX_DEBUGFS_FWSTATS_FILE(pwr, wake_on_host, "%u");
WL12XX_DEBUGFS_FWSTATS_FILE(pwr, wake_on_timer_exp, "%u");
WL12XX_DEBUGFS_FWSTATS_FILE(pwr, tx_with_ps, "%u");
WL12XX_DEBUGFS_FWSTATS_FILE(pwr, tx_without_ps, "%u");
WL12XX_DEBUGFS_FWSTATS_FILE(pwr, rcvd_beacons, "%u");
WL12XX_DEBUGFS_FWSTATS_FILE(pwr, power_save_off, "%u");
WL12XX_DEBUGFS_FWSTATS_FILE(pwr, enable_ps, "%u");
WL12XX_DEBUGFS_FWSTATS_FILE(pwr, disable_ps, "%u");
WL12XX_DEBUGFS_FWSTATS_FILE(pwr, fix_tsf_ps, "%u");
/* skipping cont_miss_bcns_spread for now */
WL12XX_DEBUGFS_FWSTATS_FILE(pwr, rcvd_awake_beacons, "%u");

WL12XX_DEBUGFS_FWSTATS_FILE(mic, rx_pkts, "%u");
WL12XX_DEBUGFS_FWSTATS_FILE(mic, calc_failure, "%u");

WL12XX_DEBUGFS_FWSTATS_FILE(aes, encrypt_fail, "%u");
WL12XX_DEBUGFS_FWSTATS_FILE(aes, decrypt_fail, "%u");
WL12XX_DEBUGFS_FWSTATS_FILE(aes, encrypt_packets, "%u");
WL12XX_DEBUGFS_FWSTATS_FILE(aes, decrypt_packets, "%u");
WL12XX_DEBUGFS_FWSTATS_FILE(aes, encrypt_interrupt, "%u");
WL12XX_DEBUGFS_FWSTATS_FILE(aes, decrypt_interrupt, "%u");

WL12XX_DEBUGFS_FWSTATS_FILE(event, heart_beat, "%u");
WL12XX_DEBUGFS_FWSTATS_FILE(event, calibration, "%u");
WL12XX_DEBUGFS_FWSTATS_FILE(event, rx_mismatch, "%u");
WL12XX_DEBUGFS_FWSTATS_FILE(event, rx_mem_empty, "%u");
WL12XX_DEBUGFS_FWSTATS_FILE(event, rx_pool, "%u");
WL12XX_DEBUGFS_FWSTATS_FILE(event, oom_late, "%u");
WL12XX_DEBUGFS_FWSTATS_FILE(event, phy_transmit_error, "%u");
WL12XX_DEBUGFS_FWSTATS_FILE(event, tx_stuck, "%u");

WL12XX_DEBUGFS_FWSTATS_FILE(ps, pspoll_timeouts, "%u");
WL12XX_DEBUGFS_FWSTATS_FILE(ps, upsd_timeouts, "%u");
WL12XX_DEBUGFS_FWSTATS_FILE(ps, upsd_max_sptime, "%u");
WL12XX_DEBUGFS_FWSTATS_FILE(ps, upsd_max_apturn, "%u");
WL12XX_DEBUGFS_FWSTATS_FILE(ps, pspoll_max_apturn, "%u");
WL12XX_DEBUGFS_FWSTATS_FILE(ps, pspoll_utilization, "%u");
WL12XX_DEBUGFS_FWSTATS_FILE(ps, upsd_utilization, "%u");

WL12XX_DEBUGFS_FWSTATS_FILE(rxpipe, rx_prep_beacon_drop, "%u");
WL12XX_DEBUGFS_FWSTATS_FILE(rxpipe, descr_host_int_trig_rx_data, "%u");
WL12XX_DEBUGFS_FWSTATS_FILE(rxpipe, beacon_buffer_thres_host_int_trig_rx_data,
			    "%u");
WL12XX_DEBUGFS_FWSTATS_FILE(rxpipe, missed_beacon_host_int_trig_rx_data, "%u");
WL12XX_DEBUGFS_FWSTATS_FILE(rxpipe, tx_xfr_host_int_trig_rx_data, "%u");

int wl12xx_debugfs_add_files(struct wl1271 *wl,
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

	return 0;

err:
	if (IS_ERR(entry))
		ret = PTR_ERR(entry);
	else
		ret = -ENOMEM;

	return ret;
}
