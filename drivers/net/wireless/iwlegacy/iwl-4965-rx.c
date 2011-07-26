/******************************************************************************
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2008 - 2011 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110,
 * USA
 *
 * The full GNU General Public License is included in this distribution
 * in the file called LICENSE.GPL.
 *
 * Contact Information:
 *  Intel Linux Wireless <ilw@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 *****************************************************************************/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/sched.h>

#include "iwl-dev.h"
#include "iwl-core.h"
#include "iwl-4965-calib.h"
#include "iwl-sta.h"
#include "iwl-io.h"
#include "iwl-helpers.h"
#include "iwl-4965-hw.h"
#include "iwl-4965.h"

void iwl4965_rx_missed_beacon_notif(struct iwl_priv *priv,
				struct iwl_rx_mem_buffer *rxb)

{
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	struct iwl_missed_beacon_notif *missed_beacon;

	missed_beacon = &pkt->u.missed_beacon;
	if (le32_to_cpu(missed_beacon->consecutive_missed_beacons) >
	    priv->missed_beacon_threshold) {
		IWL_DEBUG_CALIB(priv,
		    "missed bcn cnsq %d totl %d rcd %d expctd %d\n",
		    le32_to_cpu(missed_beacon->consecutive_missed_beacons),
		    le32_to_cpu(missed_beacon->total_missed_becons),
		    le32_to_cpu(missed_beacon->num_recvd_beacons),
		    le32_to_cpu(missed_beacon->num_expected_beacons));
		if (!test_bit(STATUS_SCANNING, &priv->status))
			iwl4965_init_sensitivity(priv);
	}
}

/* Calculate noise level, based on measurements during network silence just
 *   before arriving beacon.  This measurement can be done only if we know
 *   exactly when to expect beacons, therefore only when we're associated. */
static void iwl4965_rx_calc_noise(struct iwl_priv *priv)
{
	struct statistics_rx_non_phy *rx_info;
	int num_active_rx = 0;
	int total_silence = 0;
	int bcn_silence_a, bcn_silence_b, bcn_silence_c;
	int last_rx_noise;

	rx_info = &(priv->_4965.statistics.rx.general);
	bcn_silence_a =
		le32_to_cpu(rx_info->beacon_silence_rssi_a) & IN_BAND_FILTER;
	bcn_silence_b =
		le32_to_cpu(rx_info->beacon_silence_rssi_b) & IN_BAND_FILTER;
	bcn_silence_c =
		le32_to_cpu(rx_info->beacon_silence_rssi_c) & IN_BAND_FILTER;

	if (bcn_silence_a) {
		total_silence += bcn_silence_a;
		num_active_rx++;
	}
	if (bcn_silence_b) {
		total_silence += bcn_silence_b;
		num_active_rx++;
	}
	if (bcn_silence_c) {
		total_silence += bcn_silence_c;
		num_active_rx++;
	}

	/* Average among active antennas */
	if (num_active_rx)
		last_rx_noise = (total_silence / num_active_rx) - 107;
	else
		last_rx_noise = IWL_NOISE_MEAS_NOT_AVAILABLE;

	IWL_DEBUG_CALIB(priv, "inband silence a %u, b %u, c %u, dBm %d\n",
			bcn_silence_a, bcn_silence_b, bcn_silence_c,
			last_rx_noise);
}

#ifdef CONFIG_IWLWIFI_LEGACY_DEBUGFS
/*
 *  based on the assumption of all statistics counter are in DWORD
 *  FIXME: This function is for debugging, do not deal with
 *  the case of counters roll-over.
 */
static void iwl4965_accumulative_statistics(struct iwl_priv *priv,
					__le32 *stats)
{
	int i, size;
	__le32 *prev_stats;
	u32 *accum_stats;
	u32 *delta, *max_delta;
	struct statistics_general_common *general, *accum_general;
	struct statistics_tx *tx, *accum_tx;

	prev_stats = (__le32 *)&priv->_4965.statistics;
	accum_stats = (u32 *)&priv->_4965.accum_statistics;
	size = sizeof(struct iwl_notif_statistics);
	general = &priv->_4965.statistics.general.common;
	accum_general = &priv->_4965.accum_statistics.general.common;
	tx = &priv->_4965.statistics.tx;
	accum_tx = &priv->_4965.accum_statistics.tx;
	delta = (u32 *)&priv->_4965.delta_statistics;
	max_delta = (u32 *)&priv->_4965.max_delta;

	for (i = sizeof(__le32); i < size;
	     i += sizeof(__le32), stats++, prev_stats++, delta++,
	     max_delta++, accum_stats++) {
		if (le32_to_cpu(*stats) > le32_to_cpu(*prev_stats)) {
			*delta = (le32_to_cpu(*stats) -
				le32_to_cpu(*prev_stats));
			*accum_stats += *delta;
			if (*delta > *max_delta)
				*max_delta = *delta;
		}
	}

	/* reset accumulative statistics for "no-counter" type statistics */
	accum_general->temperature = general->temperature;
	accum_general->ttl_timestamp = general->ttl_timestamp;
}
#endif

#define REG_RECALIB_PERIOD (60)

void iwl4965_rx_statistics(struct iwl_priv *priv,
			      struct iwl_rx_mem_buffer *rxb)
{
	int change;
	struct iwl_rx_packet *pkt = rxb_addr(rxb);

	IWL_DEBUG_RX(priv,
		     "Statistics notification received (%d vs %d).\n",
		     (int)sizeof(struct iwl_notif_statistics),
		     le32_to_cpu(pkt->len_n_flags) &
		     FH_RSCSR_FRAME_SIZE_MSK);

	change = ((priv->_4965.statistics.general.common.temperature !=
		   pkt->u.stats.general.common.temperature) ||
		   ((priv->_4965.statistics.flag &
		   STATISTICS_REPLY_FLG_HT40_MODE_MSK) !=
		   (pkt->u.stats.flag &
		   STATISTICS_REPLY_FLG_HT40_MODE_MSK)));
#ifdef CONFIG_IWLWIFI_LEGACY_DEBUGFS
	iwl4965_accumulative_statistics(priv, (__le32 *)&pkt->u.stats);
#endif

	/* TODO: reading some of statistics is unneeded */
	memcpy(&priv->_4965.statistics, &pkt->u.stats,
		sizeof(priv->_4965.statistics));

	set_bit(STATUS_STATISTICS, &priv->status);

	/* Reschedule the statistics timer to occur in
	 * REG_RECALIB_PERIOD seconds to ensure we get a
	 * thermal update even if the uCode doesn't give
	 * us one */
	mod_timer(&priv->statistics_periodic, jiffies +
		  msecs_to_jiffies(REG_RECALIB_PERIOD * 1000));

	if (unlikely(!test_bit(STATUS_SCANNING, &priv->status)) &&
	    (pkt->hdr.cmd == STATISTICS_NOTIFICATION)) {
		iwl4965_rx_calc_noise(priv);
		queue_work(priv->workqueue, &priv->run_time_calib_work);
	}
	if (priv->cfg->ops->lib->temp_ops.temperature && change)
		priv->cfg->ops->lib->temp_ops.temperature(priv);
}

void iwl4965_reply_statistics(struct iwl_priv *priv,
			      struct iwl_rx_mem_buffer *rxb)
{
	struct iwl_rx_packet *pkt = rxb_addr(rxb);

	if (le32_to_cpu(pkt->u.stats.flag) & UCODE_STATISTICS_CLEAR_MSK) {
#ifdef CONFIG_IWLWIFI_LEGACY_DEBUGFS
		memset(&priv->_4965.accum_statistics, 0,
			sizeof(struct iwl_notif_statistics));
		memset(&priv->_4965.delta_statistics, 0,
			sizeof(struct iwl_notif_statistics));
		memset(&priv->_4965.max_delta, 0,
			sizeof(struct iwl_notif_statistics));
#endif
		IWL_DEBUG_RX(priv, "Statistics have been cleared\n");
	}
	iwl4965_rx_statistics(priv, rxb);
}
