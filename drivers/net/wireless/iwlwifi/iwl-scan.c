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
 *****************************************************************************/
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/etherdevice.h>
#include <net/mac80211.h>

#include "iwl-eeprom.h"
#include "iwl-dev.h"
#include "iwl-core.h"
#include "iwl-sta.h"
#include "iwl-io.h"
#include "iwl-helpers.h"

/* For active scan, listen ACTIVE_DWELL_TIME (msec) on each channel after
 * sending probe req.  This should be set long enough to hear probe responses
 * from more than one AP.  */
#define IWL_ACTIVE_DWELL_TIME_24    (30)       /* all times in msec */
#define IWL_ACTIVE_DWELL_TIME_52    (20)

#define IWL_ACTIVE_DWELL_FACTOR_24GHZ (3)
#define IWL_ACTIVE_DWELL_FACTOR_52GHZ (2)

/* For passive scan, listen PASSIVE_DWELL_TIME (msec) on each channel.
 * Must be set longer than active dwell time.
 * For the most reliable scan, set > AP beacon interval (typically 100msec). */
#define IWL_PASSIVE_DWELL_TIME_24   (20)       /* all times in msec */
#define IWL_PASSIVE_DWELL_TIME_52   (10)
#define IWL_PASSIVE_DWELL_BASE      (100)
#define IWL_CHANNEL_TUNE_TIME       5

static int iwl_send_scan_abort(struct iwl_priv *priv)
{
	int ret;
	struct iwl_rx_packet *pkt;
	struct iwl_host_cmd cmd = {
		.id = REPLY_SCAN_ABORT_CMD,
		.flags = CMD_WANT_SKB,
	};

	/* Exit instantly with error when device is not ready
	 * to receive scan abort command or it does not perform
	 * hardware scan currently */
	if (!test_bit(STATUS_READY, &priv->status) ||
	    !test_bit(STATUS_GEO_CONFIGURED, &priv->status) ||
	    !test_bit(STATUS_SCAN_HW, &priv->status) ||
	    test_bit(STATUS_FW_ERROR, &priv->status) ||
	    test_bit(STATUS_EXIT_PENDING, &priv->status))
		return -EIO;

	ret = iwl_send_cmd_sync(priv, &cmd);
	if (ret)
		return ret;

	pkt = (struct iwl_rx_packet *)cmd.reply_page;
	if (pkt->u.status != CAN_ABORT_STATUS) {
		/* The scan abort will return 1 for success or
		 * 2 for "failure".  A failure condition can be
		 * due to simply not being in an active scan which
		 * can occur if we send the scan abort before we
		 * the microcode has notified us that a scan is
		 * completed. */
		IWL_DEBUG_SCAN(priv, "SCAN_ABORT ret %d.\n", pkt->u.status);
		ret = -EIO;
	}

	iwl_free_pages(priv, cmd.reply_page);
	return ret;
}

static void iwl_complete_scan(struct iwl_priv *priv, bool aborted)
{
	/* check if scan was requested from mac80211 */
	if (priv->scan_request) {
		IWL_DEBUG_SCAN(priv, "Complete scan in mac80211\n");
		ieee80211_scan_completed(priv->hw, aborted);
	}

	priv->scan_type = IWL_SCAN_NORMAL;
	priv->scan_vif = NULL;
	priv->scan_request = NULL;
}

void iwl_force_scan_end(struct iwl_priv *priv)
{
	lockdep_assert_held(&priv->mutex);

	if (!test_bit(STATUS_SCANNING, &priv->status)) {
		IWL_DEBUG_SCAN(priv, "Forcing scan end while not scanning\n");
		return;
	}

	IWL_DEBUG_SCAN(priv, "Forcing scan end\n");
	clear_bit(STATUS_SCANNING, &priv->status);
	clear_bit(STATUS_SCAN_HW, &priv->status);
	clear_bit(STATUS_SCAN_ABORTING, &priv->status);
	iwl_complete_scan(priv, true);
}

static void iwl_do_scan_abort(struct iwl_priv *priv)
{
	int ret;

	lockdep_assert_held(&priv->mutex);

	if (!test_bit(STATUS_SCANNING, &priv->status)) {
		IWL_DEBUG_SCAN(priv, "Not performing scan to abort\n");
		return;
	}

	if (test_and_set_bit(STATUS_SCAN_ABORTING, &priv->status)) {
		IWL_DEBUG_SCAN(priv, "Scan abort in progress\n");
		return;
	}

	ret = iwl_send_scan_abort(priv);
	if (ret) {
		IWL_DEBUG_SCAN(priv, "Send scan abort failed %d\n", ret);
		iwl_force_scan_end(priv);
	} else
		IWL_DEBUG_SCAN(priv, "Successfully send scan abort\n");
}

/**
 * iwl_scan_cancel - Cancel any currently executing HW scan
 */
int iwl_scan_cancel(struct iwl_priv *priv)
{
	IWL_DEBUG_SCAN(priv, "Queuing abort scan\n");
	queue_work(priv->workqueue, &priv->abort_scan);
	return 0;
}

/**
 * iwl_scan_cancel_timeout - Cancel any currently executing HW scan
 * @ms: amount of time to wait (in milliseconds) for scan to abort
 *
 */
int iwl_scan_cancel_timeout(struct iwl_priv *priv, unsigned long ms)
{
	unsigned long timeout = jiffies + msecs_to_jiffies(ms);

	lockdep_assert_held(&priv->mutex);

	IWL_DEBUG_SCAN(priv, "Scan cancel timeout\n");

	iwl_do_scan_abort(priv);

	while (time_before_eq(jiffies, timeout)) {
		if (!test_bit(STATUS_SCAN_HW, &priv->status))
			break;
		msleep(20);
	}

	return test_bit(STATUS_SCAN_HW, &priv->status);
}

/* Service response to REPLY_SCAN_CMD (0x80) */
static void iwl_rx_reply_scan(struct iwl_priv *priv,
			      struct iwl_rx_mem_buffer *rxb)
{
#ifdef CONFIG_IWLWIFI_DEBUG
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	struct iwl_scanreq_notification *notif =
	    (struct iwl_scanreq_notification *)pkt->u.raw;

	IWL_DEBUG_SCAN(priv, "Scan request status = 0x%x\n", notif->status);
#endif
}

/* Service SCAN_START_NOTIFICATION (0x82) */
static void iwl_rx_scan_start_notif(struct iwl_priv *priv,
				    struct iwl_rx_mem_buffer *rxb)
{
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	struct iwl_scanstart_notification *notif =
	    (struct iwl_scanstart_notification *)pkt->u.raw;
	priv->scan_start_tsf = le32_to_cpu(notif->tsf_low);
	IWL_DEBUG_SCAN(priv, "Scan start: "
		       "%d [802.11%s] "
		       "(TSF: 0x%08X:%08X) - %d (beacon timer %u)\n",
		       notif->channel,
		       notif->band ? "bg" : "a",
		       le32_to_cpu(notif->tsf_high),
		       le32_to_cpu(notif->tsf_low),
		       notif->status, notif->beacon_timer);
}

/* Service SCAN_RESULTS_NOTIFICATION (0x83) */
static void iwl_rx_scan_results_notif(struct iwl_priv *priv,
				      struct iwl_rx_mem_buffer *rxb)
{
#ifdef CONFIG_IWLWIFI_DEBUG
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	struct iwl_scanresults_notification *notif =
	    (struct iwl_scanresults_notification *)pkt->u.raw;

	IWL_DEBUG_SCAN(priv, "Scan ch.res: "
		       "%d [802.11%s] "
		       "(TSF: 0x%08X:%08X) - %d "
		       "elapsed=%lu usec\n",
		       notif->channel,
		       notif->band ? "bg" : "a",
		       le32_to_cpu(notif->tsf_high),
		       le32_to_cpu(notif->tsf_low),
		       le32_to_cpu(notif->statistics[0]),
		       le32_to_cpu(notif->tsf_low) - priv->scan_start_tsf);
#endif
}

/* Service SCAN_COMPLETE_NOTIFICATION (0x84) */
static void iwl_rx_scan_complete_notif(struct iwl_priv *priv,
				       struct iwl_rx_mem_buffer *rxb)
{
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	struct iwl_scancomplete_notification *scan_notif = (void *)pkt->u.raw;

	IWL_DEBUG_SCAN(priv, "Scan complete: %d channels (TSF 0x%08X:%08X) - %d\n",
		       scan_notif->scanned_channels,
		       scan_notif->tsf_low,
		       scan_notif->tsf_high, scan_notif->status);

	/* The HW is no longer scanning */
	clear_bit(STATUS_SCAN_HW, &priv->status);

	IWL_DEBUG_SCAN(priv, "Scan on %sGHz took %dms\n",
		       (priv->scan_band == IEEE80211_BAND_2GHZ) ? "2.4" : "5.2",
		       jiffies_to_msecs(jiffies - priv->scan_start));

	queue_work(priv->workqueue, &priv->scan_completed);

	if (priv->iw_mode != NL80211_IFTYPE_ADHOC &&
	    iwl_advanced_bt_coexist(priv) &&
	    priv->bt_status != scan_notif->bt_status) {
		if (scan_notif->bt_status) {
			/* BT on */
			if (!priv->bt_ch_announce)
				priv->bt_traffic_load =
					IWL_BT_COEX_TRAFFIC_LOAD_HIGH;
			/*
			 * otherwise, no traffic load information provided
			 * no changes made
			 */
		} else {
			/* BT off */
			priv->bt_traffic_load =
				IWL_BT_COEX_TRAFFIC_LOAD_NONE;
		}
		priv->bt_status = scan_notif->bt_status;
		queue_work(priv->workqueue, &priv->bt_traffic_change_work);
	}
}

void iwl_setup_rx_scan_handlers(struct iwl_priv *priv)
{
	/* scan handlers */
	priv->rx_handlers[REPLY_SCAN_CMD] = iwl_rx_reply_scan;
	priv->rx_handlers[SCAN_START_NOTIFICATION] = iwl_rx_scan_start_notif;
	priv->rx_handlers[SCAN_RESULTS_NOTIFICATION] =
					iwl_rx_scan_results_notif;
	priv->rx_handlers[SCAN_COMPLETE_NOTIFICATION] =
					iwl_rx_scan_complete_notif;
}

inline u16 iwl_get_active_dwell_time(struct iwl_priv *priv,
				     enum ieee80211_band band,
				     u8 n_probes)
{
	if (band == IEEE80211_BAND_5GHZ)
		return IWL_ACTIVE_DWELL_TIME_52 +
			IWL_ACTIVE_DWELL_FACTOR_52GHZ * (n_probes + 1);
	else
		return IWL_ACTIVE_DWELL_TIME_24 +
			IWL_ACTIVE_DWELL_FACTOR_24GHZ * (n_probes + 1);
}

u16 iwl_get_passive_dwell_time(struct iwl_priv *priv,
			       enum ieee80211_band band,
			       struct ieee80211_vif *vif)
{
	struct iwl_rxon_context *ctx;
	u16 passive = (band == IEEE80211_BAND_2GHZ) ?
	    IWL_PASSIVE_DWELL_BASE + IWL_PASSIVE_DWELL_TIME_24 :
	    IWL_PASSIVE_DWELL_BASE + IWL_PASSIVE_DWELL_TIME_52;

	if (iwl_is_any_associated(priv)) {
		/*
		 * If we're associated, we clamp the maximum passive
		 * dwell time to be 98% of the smallest beacon interval
		 * (minus 2 * channel tune time)
		 */
		for_each_context(priv, ctx) {
			u16 value;

			if (!iwl_is_associated_ctx(ctx))
				continue;
			value = ctx->vif ? ctx->vif->bss_conf.beacon_int : 0;
			if ((value > IWL_PASSIVE_DWELL_BASE) || !value)
				value = IWL_PASSIVE_DWELL_BASE;
			value = (value * 98) / 100 - IWL_CHANNEL_TUNE_TIME * 2;
			passive = min(value, passive);
		}
	}

	return passive;
}

void iwl_init_scan_params(struct iwl_priv *priv)
{
	u8 ant_idx = fls(priv->hw_params.valid_tx_ant) - 1;
	if (!priv->scan_tx_ant[IEEE80211_BAND_5GHZ])
		priv->scan_tx_ant[IEEE80211_BAND_5GHZ] = ant_idx;
	if (!priv->scan_tx_ant[IEEE80211_BAND_2GHZ])
		priv->scan_tx_ant[IEEE80211_BAND_2GHZ] = ant_idx;
}

int __must_check iwl_scan_initiate(struct iwl_priv *priv,
				   struct ieee80211_vif *vif,
				   enum iwl_scan_type scan_type,
				   enum ieee80211_band band)
{
	int ret;

	lockdep_assert_held(&priv->mutex);

	if (WARN_ON(!priv->cfg->ops->utils->request_scan))
		return -EOPNOTSUPP;

	cancel_delayed_work(&priv->scan_check);

	if (!iwl_is_ready_rf(priv)) {
		IWL_WARN(priv, "Request scan called when driver not ready.\n");
		return -EIO;
	}

	if (test_bit(STATUS_SCAN_HW, &priv->status)) {
		IWL_DEBUG_SCAN(priv,
			"Multiple concurrent scan requests in parallel.\n");
		return -EBUSY;
	}

	if (test_bit(STATUS_SCAN_ABORTING, &priv->status)) {
		IWL_DEBUG_SCAN(priv, "Scan request while abort pending.\n");
		return -EBUSY;
	}

	IWL_DEBUG_SCAN(priv, "Starting %sscan...\n",
			scan_type == IWL_SCAN_NORMAL ? "" :
			scan_type == IWL_SCAN_OFFCH_TX ? "offchan TX " :
			"internal short ");

	set_bit(STATUS_SCANNING, &priv->status);
	priv->scan_type = scan_type;
	priv->scan_start = jiffies;
	priv->scan_band = band;

	ret = priv->cfg->ops->utils->request_scan(priv, vif);
	if (ret) {
		clear_bit(STATUS_SCANNING, &priv->status);
		priv->scan_type = IWL_SCAN_NORMAL;
		return ret;
	}

	queue_delayed_work(priv->workqueue, &priv->scan_check,
			   IWL_SCAN_CHECK_WATCHDOG);

	return 0;
}

int iwl_mac_hw_scan(struct ieee80211_hw *hw,
		    struct ieee80211_vif *vif,
		    struct cfg80211_scan_request *req)
{
	struct iwl_priv *priv = hw->priv;
	int ret;

	IWL_DEBUG_MAC80211(priv, "enter\n");

	if (req->n_channels == 0)
		return -EINVAL;

	mutex_lock(&priv->mutex);

	if (test_bit(STATUS_SCANNING, &priv->status) &&
	    priv->scan_type != IWL_SCAN_NORMAL) {
		IWL_DEBUG_SCAN(priv, "Scan already in progress.\n");
		ret = -EAGAIN;
		goto out_unlock;
	}

	/* mac80211 will only ask for one band at a time */
	priv->scan_request = req;
	priv->scan_vif = vif;

	/*
	 * If an internal scan is in progress, just set
	 * up the scan_request as per above.
	 */
	if (priv->scan_type != IWL_SCAN_NORMAL) {
		IWL_DEBUG_SCAN(priv, "SCAN request during internal scan\n");
		ret = 0;
	} else
		ret = iwl_scan_initiate(priv, vif, IWL_SCAN_NORMAL,
					req->channels[0]->band);

	IWL_DEBUG_MAC80211(priv, "leave\n");

out_unlock:
	mutex_unlock(&priv->mutex);

	return ret;
}

/*
 * internal short scan, this function should only been called while associated.
 * It will reset and tune the radio to prevent possible RF related problem
 */
void iwl_internal_short_hw_scan(struct iwl_priv *priv)
{
	queue_work(priv->workqueue, &priv->start_internal_scan);
}

static void iwl_bg_start_internal_scan(struct work_struct *work)
{
	struct iwl_priv *priv =
		container_of(work, struct iwl_priv, start_internal_scan);

	IWL_DEBUG_SCAN(priv, "Start internal scan\n");

	mutex_lock(&priv->mutex);

	if (priv->scan_type == IWL_SCAN_RADIO_RESET) {
		IWL_DEBUG_SCAN(priv, "Internal scan already in progress\n");
		goto unlock;
	}

	if (test_bit(STATUS_SCANNING, &priv->status)) {
		IWL_DEBUG_SCAN(priv, "Scan already in progress.\n");
		goto unlock;
	}

	if (iwl_scan_initiate(priv, NULL, IWL_SCAN_RADIO_RESET, priv->band))
		IWL_DEBUG_SCAN(priv, "failed to start internal short scan\n");
 unlock:
	mutex_unlock(&priv->mutex);
}

static void iwl_bg_scan_check(struct work_struct *data)
{
	struct iwl_priv *priv =
	    container_of(data, struct iwl_priv, scan_check.work);

	IWL_DEBUG_SCAN(priv, "Scan check work\n");

	/* Since we are here firmware does not finish scan and
	 * most likely is in bad shape, so we don't bother to
	 * send abort command, just force scan complete to mac80211 */
	mutex_lock(&priv->mutex);
	iwl_force_scan_end(priv);
	mutex_unlock(&priv->mutex);
}

/**
 * iwl_fill_probe_req - fill in all required fields and IE for probe request
 */

u16 iwl_fill_probe_req(struct iwl_priv *priv, struct ieee80211_mgmt *frame,
		       const u8 *ta, const u8 *ies, int ie_len, int left)
{
	int len = 0;
	u8 *pos = NULL;

	/* Make sure there is enough space for the probe request,
	 * two mandatory IEs and the data */
	left -= 24;
	if (left < 0)
		return 0;

	frame->frame_control = cpu_to_le16(IEEE80211_STYPE_PROBE_REQ);
	memcpy(frame->da, iwl_bcast_addr, ETH_ALEN);
	memcpy(frame->sa, ta, ETH_ALEN);
	memcpy(frame->bssid, iwl_bcast_addr, ETH_ALEN);
	frame->seq_ctrl = 0;

	len += 24;

	/* ...next IE... */
	pos = &frame->u.probe_req.variable[0];

	/* fill in our indirect SSID IE */
	left -= 2;
	if (left < 0)
		return 0;
	*pos++ = WLAN_EID_SSID;
	*pos++ = 0;

	len += 2;

	if (WARN_ON(left < ie_len))
		return len;

	if (ies && ie_len) {
		memcpy(pos, ies, ie_len);
		len += ie_len;
	}

	return (u16)len;
}

static void iwl_bg_abort_scan(struct work_struct *work)
{
	struct iwl_priv *priv = container_of(work, struct iwl_priv, abort_scan);

	IWL_DEBUG_SCAN(priv, "Abort scan work\n");

	/* We keep scan_check work queued in case when firmware will not
	 * report back scan completed notification */
	mutex_lock(&priv->mutex);
	iwl_scan_cancel_timeout(priv, 200);
	mutex_unlock(&priv->mutex);
}

static void iwl_bg_scan_completed(struct work_struct *work)
{
	struct iwl_priv *priv =
	    container_of(work, struct iwl_priv, scan_completed);
	bool aborted;

	IWL_DEBUG_SCAN(priv, "Completed scan.\n");

	cancel_delayed_work(&priv->scan_check);

	mutex_lock(&priv->mutex);

	aborted = test_and_clear_bit(STATUS_SCAN_ABORTING, &priv->status);
	if (aborted)
		IWL_DEBUG_SCAN(priv, "Aborted scan completed.\n");

	if (!test_and_clear_bit(STATUS_SCANNING, &priv->status)) {
		IWL_DEBUG_SCAN(priv, "Scan already completed.\n");
		goto out_settings;
	}

	if (priv->scan_type == IWL_SCAN_OFFCH_TX && priv->_agn.offchan_tx_skb) {
		ieee80211_tx_status_irqsafe(priv->hw,
					    priv->_agn.offchan_tx_skb);
		priv->_agn.offchan_tx_skb = NULL;
	}

	if (priv->scan_type != IWL_SCAN_NORMAL && !aborted) {
		int err;

		/* Check if mac80211 requested scan during our internal scan */
		if (priv->scan_request == NULL)
			goto out_complete;

		/* If so request a new scan */
		err = iwl_scan_initiate(priv, priv->scan_vif, IWL_SCAN_NORMAL,
					priv->scan_request->channels[0]->band);
		if (err) {
			IWL_DEBUG_SCAN(priv,
				"failed to initiate pending scan: %d\n", err);
			aborted = true;
			goto out_complete;
		}

		goto out;
	}

out_complete:
	iwl_complete_scan(priv, aborted);

out_settings:
	/* Can we still talk to firmware ? */
	if (!iwl_is_ready_rf(priv))
		goto out;

	/*
	 * We do not commit power settings while scan is pending,
	 * do it now if the settings changed.
	 */
	iwl_power_set_mode(priv, &priv->power_data.sleep_cmd_next, false);
	iwl_set_tx_power(priv, priv->tx_power_next, false);

	priv->cfg->ops->utils->post_scan(priv);

out:
	mutex_unlock(&priv->mutex);
}

void iwl_setup_scan_deferred_work(struct iwl_priv *priv)
{
	INIT_WORK(&priv->scan_completed, iwl_bg_scan_completed);
	INIT_WORK(&priv->abort_scan, iwl_bg_abort_scan);
	INIT_WORK(&priv->start_internal_scan, iwl_bg_start_internal_scan);
	INIT_DELAYED_WORK(&priv->scan_check, iwl_bg_scan_check);
}

void iwl_cancel_scan_deferred_work(struct iwl_priv *priv)
{
	cancel_work_sync(&priv->start_internal_scan);
	cancel_work_sync(&priv->abort_scan);
	cancel_work_sync(&priv->scan_completed);

	if (cancel_delayed_work_sync(&priv->scan_check)) {
		mutex_lock(&priv->mutex);
		iwl_force_scan_end(priv);
		mutex_unlock(&priv->mutex);
	}
}
