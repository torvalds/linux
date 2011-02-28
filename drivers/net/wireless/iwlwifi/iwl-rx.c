/******************************************************************************
 *
 * Copyright(c) 2003 - 2010 Intel Corporation. All rights reserved.
 *
 * Portions of this file are derived from the ipw3945 project, as well
 * as portions of the ieee80211 subsystem header files.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 *  Intel Linux Wireless <ilw@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 *****************************************************************************/

#include <linux/etherdevice.h>
#include <linux/slab.h>
#include <net/mac80211.h>
#include <asm/unaligned.h>
#include "iwl-eeprom.h"
#include "iwl-dev.h"
#include "iwl-core.h"
#include "iwl-sta.h"
#include "iwl-io.h"
#include "iwl-helpers.h"
#include "iwl-agn-calib.h"
/************************** RX-FUNCTIONS ****************************/
/*
 * Rx theory of operation
 *
 * Driver allocates a circular buffer of Receive Buffer Descriptors (RBDs),
 * each of which point to Receive Buffers to be filled by the NIC.  These get
 * used not only for Rx frames, but for any command response or notification
 * from the NIC.  The driver and NIC manage the Rx buffers by means
 * of indexes into the circular buffer.
 *
 * Rx Queue Indexes
 * The host/firmware share two index registers for managing the Rx buffers.
 *
 * The READ index maps to the first position that the firmware may be writing
 * to -- the driver can read up to (but not including) this position and get
 * good data.
 * The READ index is managed by the firmware once the card is enabled.
 *
 * The WRITE index maps to the last position the driver has read from -- the
 * position preceding WRITE is the last slot the firmware can place a packet.
 *
 * The queue is empty (no good data) if WRITE = READ - 1, and is full if
 * WRITE = READ.
 *
 * During initialization, the host sets up the READ queue position to the first
 * INDEX position, and WRITE to the last (READ - 1 wrapped)
 *
 * When the firmware places a packet in a buffer, it will advance the READ index
 * and fire the RX interrupt.  The driver can then query the READ index and
 * process as many packets as possible, moving the WRITE index forward as it
 * resets the Rx queue buffers with new memory.
 *
 * The management in the driver is as follows:
 * + A list of pre-allocated SKBs is stored in iwl->rxq->rx_free.  When
 *   iwl->rxq->free_count drops to or below RX_LOW_WATERMARK, work is scheduled
 *   to replenish the iwl->rxq->rx_free.
 * + In iwl_rx_replenish (scheduled) if 'processed' != 'read' then the
 *   iwl->rxq is replenished and the READ INDEX is updated (updating the
 *   'processed' and 'read' driver indexes as well)
 * + A received packet is processed and handed to the kernel network stack,
 *   detached from the iwl->rxq.  The driver 'processed' index is updated.
 * + The Host/Firmware iwl->rxq is replenished at tasklet time from the rx_free
 *   list. If there are no allocated buffers in iwl->rxq->rx_free, the READ
 *   INDEX is not incremented and iwl->status(RX_STALLED) is set.  If there
 *   were enough free buffers and RX_STALLED is set it is cleared.
 *
 *
 * Driver sequence:
 *
 * iwl_rx_queue_alloc()   Allocates rx_free
 * iwl_rx_replenish()     Replenishes rx_free list from rx_used, and calls
 *                            iwl_rx_queue_restock
 * iwl_rx_queue_restock() Moves available buffers from rx_free into Rx
 *                            queue, updates firmware pointers, and updates
 *                            the WRITE index.  If insufficient rx_free buffers
 *                            are available, schedules iwl_rx_replenish
 *
 * -- enable interrupts --
 * ISR - iwl_rx()         Detach iwl_rx_mem_buffers from pool up to the
 *                            READ INDEX, detaching the SKB from the pool.
 *                            Moves the packet buffer from queue to rx_used.
 *                            Calls iwl_rx_queue_restock to refill any empty
 *                            slots.
 * ...
 *
 */

/**
 * iwl_rx_queue_space - Return number of free slots available in queue.
 */
int iwl_rx_queue_space(const struct iwl_rx_queue *q)
{
	int s = q->read - q->write;
	if (s <= 0)
		s += RX_QUEUE_SIZE;
	/* keep some buffer to not confuse full and empty queue */
	s -= 2;
	if (s < 0)
		s = 0;
	return s;
}

/**
 * iwl_rx_queue_update_write_ptr - Update the write pointer for the RX queue
 */
void iwl_rx_queue_update_write_ptr(struct iwl_priv *priv, struct iwl_rx_queue *q)
{
	unsigned long flags;
	u32 rx_wrt_ptr_reg = priv->hw_params.rx_wrt_ptr_reg;
	u32 reg;

	spin_lock_irqsave(&q->lock, flags);

	if (q->need_update == 0)
		goto exit_unlock;

	if (priv->cfg->base_params->shadow_reg_enable) {
		/* shadow register enabled */
		/* Device expects a multiple of 8 */
		q->write_actual = (q->write & ~0x7);
		iwl_write32(priv, rx_wrt_ptr_reg, q->write_actual);
	} else {
		/* If power-saving is in use, make sure device is awake */
		if (test_bit(STATUS_POWER_PMI, &priv->status)) {
			reg = iwl_read32(priv, CSR_UCODE_DRV_GP1);

			if (reg & CSR_UCODE_DRV_GP1_BIT_MAC_SLEEP) {
				IWL_DEBUG_INFO(priv,
					"Rx queue requesting wakeup,"
					" GP1 = 0x%x\n", reg);
				iwl_set_bit(priv, CSR_GP_CNTRL,
					CSR_GP_CNTRL_REG_FLAG_MAC_ACCESS_REQ);
				goto exit_unlock;
			}

			q->write_actual = (q->write & ~0x7);
			iwl_write_direct32(priv, rx_wrt_ptr_reg,
					q->write_actual);

		/* Else device is assumed to be awake */
		} else {
			/* Device expects a multiple of 8 */
			q->write_actual = (q->write & ~0x7);
			iwl_write_direct32(priv, rx_wrt_ptr_reg,
				q->write_actual);
		}
	}
	q->need_update = 0;

 exit_unlock:
	spin_unlock_irqrestore(&q->lock, flags);
}

int iwl_rx_queue_alloc(struct iwl_priv *priv)
{
	struct iwl_rx_queue *rxq = &priv->rxq;
	struct device *dev = &priv->pci_dev->dev;
	int i;

	spin_lock_init(&rxq->lock);
	INIT_LIST_HEAD(&rxq->rx_free);
	INIT_LIST_HEAD(&rxq->rx_used);

	/* Alloc the circular buffer of Read Buffer Descriptors (RBDs) */
	rxq->bd = dma_alloc_coherent(dev, 4 * RX_QUEUE_SIZE, &rxq->bd_dma,
				     GFP_KERNEL);
	if (!rxq->bd)
		goto err_bd;

	rxq->rb_stts = dma_alloc_coherent(dev, sizeof(struct iwl_rb_status),
					  &rxq->rb_stts_dma, GFP_KERNEL);
	if (!rxq->rb_stts)
		goto err_rb;

	/* Fill the rx_used queue with _all_ of the Rx buffers */
	for (i = 0; i < RX_FREE_BUFFERS + RX_QUEUE_SIZE; i++)
		list_add_tail(&rxq->pool[i].list, &rxq->rx_used);

	/* Set us so that we have processed and used all buffers, but have
	 * not restocked the Rx queue with fresh buffers */
	rxq->read = rxq->write = 0;
	rxq->write_actual = 0;
	rxq->free_count = 0;
	rxq->need_update = 0;
	return 0;

err_rb:
	dma_free_coherent(&priv->pci_dev->dev, 4 * RX_QUEUE_SIZE, rxq->bd,
			  rxq->bd_dma);
err_bd:
	return -ENOMEM;
}

void iwl_rx_spectrum_measure_notif(struct iwl_priv *priv,
					  struct iwl_rx_mem_buffer *rxb)
{
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	struct iwl_spectrum_notification *report = &(pkt->u.spectrum_notif);

	if (!report->state) {
		IWL_DEBUG_11H(priv,
			"Spectrum Measure Notification: Start\n");
		return;
	}

	memcpy(&priv->measure_report, report, sizeof(*report));
	priv->measurement_status |= MEASUREMENT_READY;
}

/* the threshold ratio of actual_ack_cnt to expected_ack_cnt in percent */
#define ACK_CNT_RATIO (50)
#define BA_TIMEOUT_CNT (5)
#define BA_TIMEOUT_MAX (16)

/**
 * iwl_good_ack_health - checks for ACK count ratios, BA timeout retries.
 *
 * When the ACK count ratio is low and aggregated BA timeout retries exceeding
 * the BA_TIMEOUT_MAX, reload firmware and bring system back to normal
 * operation state.
 */
static bool iwl_good_ack_health(struct iwl_priv *priv, struct iwl_rx_packet *pkt)
{
	int actual_delta, expected_delta, ba_timeout_delta;
	struct statistics_tx *cur, *old;

	if (priv->_agn.agg_tids_count)
		return true;

	if (iwl_bt_statistics(priv)) {
		cur = &pkt->u.stats_bt.tx;
		old = &priv->_agn.statistics_bt.tx;
	} else {
		cur = &pkt->u.stats.tx;
		old = &priv->_agn.statistics.tx;
	}

	actual_delta = le32_to_cpu(cur->actual_ack_cnt) -
		       le32_to_cpu(old->actual_ack_cnt);
	expected_delta = le32_to_cpu(cur->expected_ack_cnt) -
			 le32_to_cpu(old->expected_ack_cnt);

	/* Values should not be negative, but we do not trust the firmware */
	if (actual_delta <= 0 || expected_delta <= 0)
		return true;

	ba_timeout_delta = le32_to_cpu(cur->agg.ba_timeout) -
			   le32_to_cpu(old->agg.ba_timeout);

	if ((actual_delta * 100 / expected_delta) < ACK_CNT_RATIO &&
	    ba_timeout_delta > BA_TIMEOUT_CNT) {
		IWL_DEBUG_RADIO(priv, "deltas: actual %d expected %d ba_timeout %d\n",
				actual_delta, expected_delta, ba_timeout_delta);

#ifdef CONFIG_IWLWIFI_DEBUGFS
		/*
		 * This is ifdef'ed on DEBUGFS because otherwise the
		 * statistics aren't available. If DEBUGFS is set but
		 * DEBUG is not, these will just compile out.
		 */
		IWL_DEBUG_RADIO(priv, "rx_detected_cnt delta %d\n",
				priv->_agn.delta_statistics.tx.rx_detected_cnt);
		IWL_DEBUG_RADIO(priv,
				"ack_or_ba_timeout_collision delta %d\n",
				priv->_agn.delta_statistics.tx.ack_or_ba_timeout_collision);
#endif

		if (ba_timeout_delta >= BA_TIMEOUT_MAX)
			return false;
	}

	return true;
}

/**
 * iwl_good_plcp_health - checks for plcp error.
 *
 * When the plcp error is exceeding the thresholds, reset the radio
 * to improve the throughput.
 */
static bool iwl_good_plcp_health(struct iwl_priv *priv, struct iwl_rx_packet *pkt)
{
	bool rc = true;
	int combined_plcp_delta;
	unsigned int plcp_msec;
	unsigned long plcp_received_jiffies;

	if (priv->cfg->base_params->plcp_delta_threshold ==
	    IWL_MAX_PLCP_ERR_THRESHOLD_DISABLE) {
		IWL_DEBUG_RADIO(priv, "plcp_err check disabled\n");
		return rc;
	}

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
		struct statistics_rx_phy *ofdm;
		struct statistics_rx_ht_phy *ofdm_ht;

		if (iwl_bt_statistics(priv)) {
			ofdm = &pkt->u.stats_bt.rx.ofdm;
			ofdm_ht = &pkt->u.stats_bt.rx.ofdm_ht;
			combined_plcp_delta =
			   (le32_to_cpu(ofdm->plcp_err) -
			   le32_to_cpu(priv->_agn.statistics_bt.
				       rx.ofdm.plcp_err)) +
			   (le32_to_cpu(ofdm_ht->plcp_err) -
			   le32_to_cpu(priv->_agn.statistics_bt.
				       rx.ofdm_ht.plcp_err));
		} else {
			ofdm = &pkt->u.stats.rx.ofdm;
			ofdm_ht = &pkt->u.stats.rx.ofdm_ht;
			combined_plcp_delta =
			    (le32_to_cpu(ofdm->plcp_err) -
			    le32_to_cpu(priv->_agn.statistics.
					rx.ofdm.plcp_err)) +
			    (le32_to_cpu(ofdm_ht->plcp_err) -
			    le32_to_cpu(priv->_agn.statistics.
					rx.ofdm_ht.plcp_err));
		}

		if ((combined_plcp_delta > 0) &&
		    ((combined_plcp_delta * 100) / plcp_msec) >
			priv->cfg->base_params->plcp_delta_threshold) {
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
				priv->cfg->base_params->plcp_delta_threshold,
				le32_to_cpu(ofdm->plcp_err),
				le32_to_cpu(ofdm->plcp_err),
				le32_to_cpu(ofdm_ht->plcp_err),
				le32_to_cpu(ofdm_ht->plcp_err),
				combined_plcp_delta, plcp_msec);

			rc = false;
		}
	}
	return rc;
}

static void iwl_recover_from_statistics(struct iwl_priv *priv, struct iwl_rx_packet *pkt)
{
	const struct iwl_mod_params *mod_params = priv->cfg->mod_params;

	if (test_bit(STATUS_EXIT_PENDING, &priv->status) ||
	    !iwl_is_any_associated(priv))
		return;

	if (mod_params->ack_check && !iwl_good_ack_health(priv, pkt)) {
		IWL_ERR(priv, "low ack count detected, restart firmware\n");
		if (!iwl_force_reset(priv, IWL_FW_RESET, false))
			return;
	}

	if (mod_params->plcp_check && !iwl_good_plcp_health(priv, pkt))
		iwl_force_reset(priv, IWL_RF_RESET, false);
}

/* Calculate noise level, based on measurements during network silence just
 *   before arriving beacon.  This measurement can be done only if we know
 *   exactly when to expect beacons, therefore only when we're associated. */
static void iwl_rx_calc_noise(struct iwl_priv *priv)
{
	struct statistics_rx_non_phy *rx_info;
	int num_active_rx = 0;
	int total_silence = 0;
	int bcn_silence_a, bcn_silence_b, bcn_silence_c;
	int last_rx_noise;

	if (iwl_bt_statistics(priv))
		rx_info = &(priv->_agn.statistics_bt.rx.general.common);
	else
		rx_info = &(priv->_agn.statistics.rx.general);
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

#ifdef CONFIG_IWLWIFI_DEBUGFS
/*
 *  based on the assumption of all statistics counter are in DWORD
 *  FIXME: This function is for debugging, do not deal with
 *  the case of counters roll-over.
 */
static void iwl_accumulative_statistics(struct iwl_priv *priv,
					__le32 *stats)
{
	int i, size;
	__le32 *prev_stats;
	u32 *accum_stats;
	u32 *delta, *max_delta;
	struct statistics_general_common *general, *accum_general;
	struct statistics_tx *tx, *accum_tx;

	if (iwl_bt_statistics(priv)) {
		prev_stats = (__le32 *)&priv->_agn.statistics_bt;
		accum_stats = (u32 *)&priv->_agn.accum_statistics_bt;
		size = sizeof(struct iwl_bt_notif_statistics);
		general = &priv->_agn.statistics_bt.general.common;
		accum_general = &priv->_agn.accum_statistics_bt.general.common;
		tx = &priv->_agn.statistics_bt.tx;
		accum_tx = &priv->_agn.accum_statistics_bt.tx;
		delta = (u32 *)&priv->_agn.delta_statistics_bt;
		max_delta = (u32 *)&priv->_agn.max_delta_bt;
	} else {
		prev_stats = (__le32 *)&priv->_agn.statistics;
		accum_stats = (u32 *)&priv->_agn.accum_statistics;
		size = sizeof(struct iwl_notif_statistics);
		general = &priv->_agn.statistics.general.common;
		accum_general = &priv->_agn.accum_statistics.general.common;
		tx = &priv->_agn.statistics.tx;
		accum_tx = &priv->_agn.accum_statistics.tx;
		delta = (u32 *)&priv->_agn.delta_statistics;
		max_delta = (u32 *)&priv->_agn.max_delta;
	}
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
	accum_general->temperature_m = general->temperature_m;
	accum_general->ttl_timestamp = general->ttl_timestamp;
	accum_tx->tx_power.ant_a = tx->tx_power.ant_a;
	accum_tx->tx_power.ant_b = tx->tx_power.ant_b;
	accum_tx->tx_power.ant_c = tx->tx_power.ant_c;
}
#endif

#define REG_RECALIB_PERIOD (60)

void iwl_rx_statistics(struct iwl_priv *priv,
			      struct iwl_rx_mem_buffer *rxb)
{
	int change;
	struct iwl_rx_packet *pkt = rxb_addr(rxb);

	if (iwl_bt_statistics(priv)) {
		IWL_DEBUG_RX(priv,
			     "Statistics notification received (%d vs %d).\n",
			     (int)sizeof(struct iwl_bt_notif_statistics),
			     le32_to_cpu(pkt->len_n_flags) &
			     FH_RSCSR_FRAME_SIZE_MSK);

		change = ((priv->_agn.statistics_bt.general.common.temperature !=
			   pkt->u.stats_bt.general.common.temperature) ||
			   ((priv->_agn.statistics_bt.flag &
			   STATISTICS_REPLY_FLG_HT40_MODE_MSK) !=
			   (pkt->u.stats_bt.flag &
			   STATISTICS_REPLY_FLG_HT40_MODE_MSK)));
#ifdef CONFIG_IWLWIFI_DEBUGFS
		iwl_accumulative_statistics(priv, (__le32 *)&pkt->u.stats_bt);
#endif

	} else {
		IWL_DEBUG_RX(priv,
			     "Statistics notification received (%d vs %d).\n",
			     (int)sizeof(struct iwl_notif_statistics),
			     le32_to_cpu(pkt->len_n_flags) &
			     FH_RSCSR_FRAME_SIZE_MSK);

		change = ((priv->_agn.statistics.general.common.temperature !=
			   pkt->u.stats.general.common.temperature) ||
			   ((priv->_agn.statistics.flag &
			   STATISTICS_REPLY_FLG_HT40_MODE_MSK) !=
			   (pkt->u.stats.flag &
			   STATISTICS_REPLY_FLG_HT40_MODE_MSK)));
#ifdef CONFIG_IWLWIFI_DEBUGFS
		iwl_accumulative_statistics(priv, (__le32 *)&pkt->u.stats);
#endif

	}

	iwl_recover_from_statistics(priv, pkt);

	if (iwl_bt_statistics(priv))
		memcpy(&priv->_agn.statistics_bt, &pkt->u.stats_bt,
			sizeof(priv->_agn.statistics_bt));
	else
		memcpy(&priv->_agn.statistics, &pkt->u.stats,
			sizeof(priv->_agn.statistics));

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
		memset(&priv->_agn.accum_statistics, 0,
			sizeof(struct iwl_notif_statistics));
		memset(&priv->_agn.delta_statistics, 0,
			sizeof(struct iwl_notif_statistics));
		memset(&priv->_agn.max_delta, 0,
			sizeof(struct iwl_notif_statistics));
		memset(&priv->_agn.accum_statistics_bt, 0,
			sizeof(struct iwl_bt_notif_statistics));
		memset(&priv->_agn.delta_statistics_bt, 0,
			sizeof(struct iwl_bt_notif_statistics));
		memset(&priv->_agn.max_delta_bt, 0,
			sizeof(struct iwl_bt_notif_statistics));
#endif
		IWL_DEBUG_RX(priv, "Statistics have been cleared\n");
	}
	iwl_rx_statistics(priv, rxb);
}

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

/*
 * returns non-zero if packet should be dropped
 */
int iwl_set_decrypted_flag(struct iwl_priv *priv,
			   struct ieee80211_hdr *hdr,
			   u32 decrypt_res,
			   struct ieee80211_rx_status *stats)
{
	u16 fc = le16_to_cpu(hdr->frame_control);

	/*
	 * All contexts have the same setting here due to it being
	 * a module parameter, so OK to check any context.
	 */
	if (priv->contexts[IWL_RXON_CTX_BSS].active.filter_flags &
						RXON_FILTER_DIS_DECRYPT_MSK)
		return 0;

	if (!(fc & IEEE80211_FCTL_PROTECTED))
		return 0;

	IWL_DEBUG_RX(priv, "decrypt_res:0x%x\n", decrypt_res);
	switch (decrypt_res & RX_RES_STATUS_SEC_TYPE_MSK) {
	case RX_RES_STATUS_SEC_TYPE_TKIP:
		/* The uCode has got a bad phase 1 Key, pushes the packet.
		 * Decryption will be done in SW. */
		if ((decrypt_res & RX_RES_STATUS_DECRYPT_TYPE_MSK) ==
		    RX_RES_STATUS_BAD_KEY_TTAK)
			break;

	case RX_RES_STATUS_SEC_TYPE_WEP:
		if ((decrypt_res & RX_RES_STATUS_DECRYPT_TYPE_MSK) ==
		    RX_RES_STATUS_BAD_ICV_MIC) {
			/* bad ICV, the packet is destroyed since the
			 * decryption is inplace, drop it */
			IWL_DEBUG_RX(priv, "Packet destroyed\n");
			return -1;
		}
	case RX_RES_STATUS_SEC_TYPE_CCMP:
		if ((decrypt_res & RX_RES_STATUS_DECRYPT_TYPE_MSK) ==
		    RX_RES_STATUS_DECRYPT_OK) {
			IWL_DEBUG_RX(priv, "hw decrypt successfully!!!\n");
			stats->flag |= RX_FLAG_DECRYPTED;
		}
		break;

	default:
		break;
	}
	return 0;
}
