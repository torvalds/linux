/******************************************************************************
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2008 - 2010 Intel Corporation. All rights reserved.
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
#include "iwl-calib.h"
#include "iwl-sta.h"
#include "iwl-io.h"
#include "iwl-helpers.h"
#include "iwl-agn-hw.h"
#include "iwl-agn.h"

void iwl_rx_missed_beacon_notif(struct iwl_priv *priv,
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
			iwl_init_sensitivity(priv);
	}
}

/* Calculate noise level, based on measurements during network silence just
 *   before arriving beacon.  This measurement can be done only if we know
 *   exactly when to expect beacons, therefore only when we're associated. */
static void iwl_rx_calc_noise(struct iwl_priv *priv)
{
	struct statistics_rx_non_phy *rx_info
				= &(priv->statistics.rx.general);
	int num_active_rx = 0;
	int total_silence = 0;
	int bcn_silence_a =
		le32_to_cpu(rx_info->beacon_silence_rssi_a) & IN_BAND_FILTER;
	int bcn_silence_b =
		le32_to_cpu(rx_info->beacon_silence_rssi_b) & IN_BAND_FILTER;
	int bcn_silence_c =
		le32_to_cpu(rx_info->beacon_silence_rssi_c) & IN_BAND_FILTER;
	int last_rx_noise;

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

#ifdef CONFIG_IWLWIFI_DEBUGFS
/*
 *  based on the assumption of all statistics counter are in DWORD
 *  FIXME: This function is for debugging, do not deal with
 *  the case of counters roll-over.
 */
static void iwl_accumulative_statistics(struct iwl_priv *priv,
					__le32 *stats)
{
	int i;
	__le32 *prev_stats;
	u32 *accum_stats;
	u32 *delta, *max_delta;

	prev_stats = (__le32 *)&priv->statistics;
	accum_stats = (u32 *)&priv->accum_statistics;
	delta = (u32 *)&priv->delta_statistics;
	max_delta = (u32 *)&priv->max_delta;

	for (i = sizeof(__le32); i < sizeof(struct iwl_notif_statistics);
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
	priv->accum_statistics.general.temperature =
		priv->statistics.general.temperature;
	priv->accum_statistics.general.temperature_m =
		priv->statistics.general.temperature_m;
	priv->accum_statistics.general.ttl_timestamp =
		priv->statistics.general.ttl_timestamp;
	priv->accum_statistics.tx.tx_power.ant_a =
		priv->statistics.tx.tx_power.ant_a;
	priv->accum_statistics.tx.tx_power.ant_b =
		priv->statistics.tx.tx_power.ant_b;
	priv->accum_statistics.tx.tx_power.ant_c =
		priv->statistics.tx.tx_power.ant_c;
}
#endif

#define REG_RECALIB_PERIOD (60)

/**
 * iwl_good_plcp_health - checks for plcp error.
 *
 * When the plcp error is exceeding the thresholds, reset the radio
 * to improve the throughput.
 */
bool iwl_good_plcp_health(struct iwl_priv *priv,
				struct iwl_rx_packet *pkt)
{
	bool rc = true;
	int combined_plcp_delta;
	unsigned int plcp_msec;
	unsigned long plcp_received_jiffies;

	/*
	 * check for plcp_err and trigger radio reset if it exceeds
	 * the plcp error threshold plcp_delta.
	 */
	plcp_received_jiffies = jiffies;
	plcp_msec = jiffies_to_msecs((long) plcp_received_jiffies -
					(long) priv->plcp_jiffies);
	priv->plcp_jiffies = plcp_received_jiffies;
	/*
	 * check to make sure plcp_msec is not 0 to prevent division
	 * by zero.
	 */
	if (plcp_msec) {
		combined_plcp_delta =
			(le32_to_cpu(pkt->u.stats.rx.ofdm.plcp_err) -
			le32_to_cpu(priv->statistics.rx.ofdm.plcp_err)) +
			(le32_to_cpu(pkt->u.stats.rx.ofdm_ht.plcp_err) -
			le32_to_cpu(priv->statistics.rx.ofdm_ht.plcp_err));

		if ((combined_plcp_delta > 0) &&
		    ((combined_plcp_delta * 100) / plcp_msec) >
			priv->cfg->plcp_delta_threshold) {
			/*
			 * if plcp_err exceed the threshold,
			 * the following data is printed in csv format:
			 *    Text: plcp_err exceeded %d,
			 *    Received ofdm.plcp_err,
			 *    Current ofdm.plcp_err,
			 *    Received ofdm_ht.plcp_err,
			 *    Current ofdm_ht.plcp_err,
			 *    combined_plcp_delta,
			 *    plcp_msec
			 */
			IWL_DEBUG_RADIO(priv, "plcp_err exceeded %u, "
				"%u, %u, %u, %u, %d, %u mSecs\n",
				priv->cfg->plcp_delta_threshold,
				le32_to_cpu(pkt->u.stats.rx.ofdm.plcp_err),
				le32_to_cpu(
				       priv->statistics.rx.ofdm.plcp_err),
				le32_to_cpu(pkt->u.stats.rx.ofdm_ht.plcp_err),
				le32_to_cpu(
				  priv->statistics.rx.ofdm_ht.plcp_err),
				combined_plcp_delta, plcp_msec);
			rc = false;
		}
	}
	return rc;
}

void iwl_rx_statistics(struct iwl_priv *priv,
			      struct iwl_rx_mem_buffer *rxb)
{
	int change;
	struct iwl_rx_packet *pkt = rxb_addr(rxb);


	IWL_DEBUG_RX(priv, "Statistics notification received (%d vs %d).\n",
		     (int)sizeof(priv->statistics),
		     le32_to_cpu(pkt->len_n_flags) & FH_RSCSR_FRAME_SIZE_MSK);

	change = ((priv->statistics.general.temperature !=
		   pkt->u.stats.general.temperature) ||
		  ((priv->statistics.flag &
		    STATISTICS_REPLY_FLG_HT40_MODE_MSK) !=
		   (pkt->u.stats.flag & STATISTICS_REPLY_FLG_HT40_MODE_MSK)));

#ifdef CONFIG_IWLWIFI_DEBUGFS
	iwl_accumulative_statistics(priv, (__le32 *)&pkt->u.stats);
#endif
	iwl_recover_from_statistics(priv, pkt);

	memcpy(&priv->statistics, &pkt->u.stats,
	       sizeof(priv->statistics));

	set_bit(STATUS_STATISTICS, &priv->status);

	/* Reschedule the statistics timer to occur in
	 * REG_RECALIB_PERIOD seconds to ensure we get a
	 * thermal update even if the uCode doesn't give
	 * us one */
	mod_timer(&priv->statistics_periodic, jiffies +
		  msecs_to_jiffies(REG_RECALIB_PERIOD * 1000));

	if (unlikely(!test_bit(STATUS_SCANNING, &priv->status)) &&
	    (pkt->hdr.cmd == STATISTICS_NOTIFICATION)) {
		iwl_rx_calc_noise(priv);
		queue_work(priv->workqueue, &priv->run_time_calib_work);
	}
	if (priv->cfg->ops->lib->temp_ops.temperature && change)
		priv->cfg->ops->lib->temp_ops.temperature(priv);
}

void iwl_reply_statistics(struct iwl_priv *priv,
			      struct iwl_rx_mem_buffer *rxb)
{
	struct iwl_rx_packet *pkt = rxb_addr(rxb);

	if (le32_to_cpu(pkt->u.stats.flag) & UCODE_STATISTICS_CLEAR_MSK) {
#ifdef CONFIG_IWLWIFI_DEBUGFS
		memset(&priv->accum_statistics, 0,
			sizeof(struct iwl_notif_statistics));
		memset(&priv->delta_statistics, 0,
			sizeof(struct iwl_notif_statistics));
		memset(&priv->max_delta, 0,
			sizeof(struct iwl_notif_statistics));
#endif
		IWL_DEBUG_RX(priv, "Statistics have been cleared\n");
	}
	iwl_rx_statistics(priv, rxb);
}
