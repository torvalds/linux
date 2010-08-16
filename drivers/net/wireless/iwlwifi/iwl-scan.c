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



/**
 * iwl_scan_cancel - Cancel any currently executing HW scan
 *
 * NOTE: priv->mutex is not required before calling this function
 */
int iwl_scan_cancel(struct iwl_priv *priv)
{
	if (!test_bit(STATUS_SCAN_HW, &priv->status)) {
		clear_bit(STATUS_SCANNING, &priv->status);
		return 0;
	}

	if (test_bit(STATUS_SCANNING, &priv->status)) {
		if (!test_and_set_bit(STATUS_SCAN_ABORTING, &priv->status)) {
			IWL_DEBUG_SCAN(priv, "Queuing scan abort.\n");
			queue_work(priv->workqueue, &priv->abort_scan);

		} else
			IWL_DEBUG_SCAN(priv, "Scan abort already in progress.\n");

		return test_bit(STATUS_SCANNING, &priv->status);
	}

	return 0;
}
EXPORT_SYMBOL(iwl_scan_cancel);
/**
 * iwl_scan_cancel_timeout - Cancel any currently executing HW scan
 * @ms: amount of time to wait (in milliseconds) for scan to abort
 *
 * NOTE: priv->mutex must be held before calling this function
 */
int iwl_scan_cancel_timeout(struct iwl_priv *priv, unsigned long ms)
{
	unsigned long now = jiffies;
	int ret;

	ret = iwl_scan_cancel(priv);
	if (ret && ms) {
		mutex_unlock(&priv->mutex);
		while (!time_after(jiffies, now + msecs_to_jiffies(ms)) &&
				test_bit(STATUS_SCANNING, &priv->status))
			msleep(1);
		mutex_lock(&priv->mutex);

		return test_bit(STATUS_SCANNING, &priv->status);
	}

	return ret;
}
EXPORT_SYMBOL(iwl_scan_cancel_timeout);

static int iwl_send_scan_abort(struct iwl_priv *priv)
{
	int ret = 0;
	struct iwl_rx_packet *pkt;
	struct iwl_host_cmd cmd = {
		.id = REPLY_SCAN_ABORT_CMD,
		.flags = CMD_WANT_SKB,
	};

	/* If there isn't a scan actively going on in the hardware
	 * then we are in between scan bands and not actually
	 * actively scanning, so don't send the abort command */
	if (!test_bit(STATUS_SCAN_HW, &priv->status)) {
		clear_bit(STATUS_SCAN_ABORTING, &priv->status);
		return 0;
	}

	ret = iwl_send_cmd_sync(priv, &cmd);
	if (ret) {
		clear_bit(STATUS_SCAN_ABORTING, &priv->status);
		return ret;
	}

	pkt = (struct iwl_rx_packet *)cmd.reply_page;
	if (pkt->u.status != CAN_ABORT_STATUS) {
		/* The scan abort will return 1 for success or
		 * 2 for "failure".  A failure condition can be
		 * due to simply not being in an active scan which
		 * can occur if we send the scan abort before we
		 * the microcode has notified us that a scan is
		 * completed. */
		IWL_DEBUG_INFO(priv, "SCAN_ABORT returned %d.\n", pkt->u.status);
		clear_bit(STATUS_SCAN_ABORTING, &priv->status);
		clear_bit(STATUS_SCAN_HW, &priv->status);
	}

	iwl_free_pages(priv, cmd.reply_page);

	return ret;
}

/* Service response to REPLY_SCAN_CMD (0x80) */
static void iwl_rx_reply_scan(struct iwl_priv *priv,
			      struct iwl_rx_mem_buffer *rxb)
{
#ifdef CONFIG_IWLWIFI_DEBUG
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	struct iwl_scanreq_notification *notif =
	    (struct iwl_scanreq_notification *)pkt->u.raw;

	IWL_DEBUG_RX(priv, "Scan request status = 0x%x\n", notif->status);
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
#ifdef CONFIG_IWLWIFI_DEBUG
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	struct iwl_scancomplete_notification *scan_notif = (void *)pkt->u.raw;

	IWL_DEBUG_SCAN(priv, "Scan complete: %d channels (TSF 0x%08X:%08X) - %d\n",
		       scan_notif->scanned_channels,
		       scan_notif->tsf_low,
		       scan_notif->tsf_high, scan_notif->status);
#endif

	/* The HW is no longer scanning */
	clear_bit(STATUS_SCAN_HW, &priv->status);

	IWL_DEBUG_INFO(priv, "Scan on %sGHz took %dms\n",
		       (priv->scan_band == IEEE80211_BAND_2GHZ) ? "2.4" : "5.2",
		       jiffies_to_msecs(elapsed_jiffies
					(priv->scan_start, jiffies)));

	/*
	 * If a request to abort was given, or the scan did not succeed
	 * then we reset the scan state machine and terminate,
	 * re-queuing another scan if one has been requested
	 */
	if (test_and_clear_bit(STATUS_SCAN_ABORTING, &priv->status))
		IWL_DEBUG_INFO(priv, "Aborted scan completed.\n");

	IWL_DEBUG_INFO(priv, "Setting scan to off\n");

	clear_bit(STATUS_SCANNING, &priv->status);

	queue_work(priv->workqueue, &priv->scan_completed);
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
EXPORT_SYMBOL(iwl_setup_rx_scan_handlers);

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
EXPORT_SYMBOL(iwl_get_active_dwell_time);

u16 iwl_get_passive_dwell_time(struct iwl_priv *priv,
			       enum ieee80211_band band,
			       struct ieee80211_vif *vif)
{
	u16 passive = (band == IEEE80211_BAND_2GHZ) ?
	    IWL_PASSIVE_DWELL_BASE + IWL_PASSIVE_DWELL_TIME_24 :
	    IWL_PASSIVE_DWELL_BASE + IWL_PASSIVE_DWELL_TIME_52;

	if (iwl_is_associated(priv)) {
		/* If we're associated, we clamp the maximum passive
		 * dwell time to be 98% of the beacon interval (minus
		 * 2 * channel tune time) */
		passive = vif ? vif->bss_conf.beacon_int : 0;
		if ((passive > IWL_PASSIVE_DWELL_BASE) || !passive)
			passive = IWL_PASSIVE_DWELL_BASE;
		passive = (passive * 98) / 100 - IWL_CHANNEL_TUNE_TIME * 2;
	}

	return passive;
}
EXPORT_SYMBOL(iwl_get_passive_dwell_time);

void iwl_init_scan_params(struct iwl_priv *priv)
{
	u8 ant_idx = fls(priv->hw_params.valid_tx_ant) - 1;
	if (!priv->scan_tx_ant[IEEE80211_BAND_5GHZ])
		priv->scan_tx_ant[IEEE80211_BAND_5GHZ] = ant_idx;
	if (!priv->scan_tx_ant[IEEE80211_BAND_2GHZ])
		priv->scan_tx_ant[IEEE80211_BAND_2GHZ] = ant_idx;
}
EXPORT_SYMBOL(iwl_init_scan_params);

static int iwl_scan_initiate(struct iwl_priv *priv, struct ieee80211_vif *vif)
{
	lockdep_assert_held(&priv->mutex);

	IWL_DEBUG_INFO(priv, "Starting scan...\n");
	set_bit(STATUS_SCANNING, &priv->status);
	priv->is_internal_short_scan = false;
	priv->scan_start = jiffies;

	if (WARN_ON(!priv->cfg->ops->utils->request_scan))
		return -EOPNOTSUPP;

	priv->cfg->ops->utils->request_scan(priv, vif);

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

	if (!iwl_is_ready_rf(priv)) {
		ret = -EIO;
		IWL_DEBUG_MAC80211(priv, "leave - not ready or exit pending\n");
		goto out_unlock;
	}

	if (test_bit(STATUS_SCANNING, &priv->status) &&
	    !priv->is_internal_short_scan) {
		IWL_DEBUG_SCAN(priv, "Scan already in progress.\n");
		ret = -EAGAIN;
		goto out_unlock;
	}

	if (test_bit(STATUS_SCAN_ABORTING, &priv->status)) {
		IWL_DEBUG_SCAN(priv, "Scan request while abort pending\n");
		ret = -EAGAIN;
		goto out_unlock;
	}

	/* mac80211 will only ask for one band at a time */
	priv->scan_band = req->channels[0]->band;
	priv->scan_request = req;
	priv->scan_vif = vif;

	/*
	 * If an internal scan is in progress, just set
	 * up the scan_request as per above.
	 */
	if (priv->is_internal_short_scan)
		ret = 0;
	else
		ret = iwl_scan_initiate(priv, vif);

	IWL_DEBUG_MAC80211(priv, "leave\n");

out_unlock:
	mutex_unlock(&priv->mutex);

	return ret;
}
EXPORT_SYMBOL(iwl_mac_hw_scan);

/*
 * internal short scan, this function should only been called while associated.
 * It will reset and tune the radio to prevent possible RF related problem
 */
void iwl_internal_short_hw_scan(struct iwl_priv *priv)
{
	queue_work(priv->workqueue, &priv->start_internal_scan);
}

void iwl_bg_start_internal_scan(struct work_struct *work)
{
	struct iwl_priv *priv =
		container_of(work, struct iwl_priv, start_internal_scan);

	mutex_lock(&priv->mutex);

	if (priv->is_internal_short_scan == true) {
		IWL_DEBUG_SCAN(priv, "Internal scan already in progress\n");
		goto unlock;
	}

	if (!iwl_is_ready_rf(priv)) {
		IWL_DEBUG_SCAN(priv, "not ready or exit pending\n");
		goto unlock;
	}

	if (test_bit(STATUS_SCANNING, &priv->status)) {
		IWL_DEBUG_SCAN(priv, "Scan already in progress.\n");
		goto unlock;
	}

	if (test_bit(STATUS_SCAN_ABORTING, &priv->status)) {
		IWL_DEBUG_SCAN(priv, "Scan request while abort pending\n");
		goto unlock;
	}

	priv->scan_band = priv->band;

	IWL_DEBUG_SCAN(priv, "Start internal short scan...\n");
	set_bit(STATUS_SCANNING, &priv->status);
	priv->is_internal_short_scan = true;

	if (WARN_ON(!priv->cfg->ops->utils->request_scan))
		goto unlock;

	priv->cfg->ops->utils->request_scan(priv, NULL);
 unlock:
	mutex_unlock(&priv->mutex);
}
EXPORT_SYMBOL(iwl_bg_start_internal_scan);

void iwl_bg_scan_check(struct work_struct *data)
{
	struct iwl_priv *priv =
	    container_of(data, struct iwl_priv, scan_check.work);

	if (test_bit(STATUS_EXIT_PENDING, &priv->status))
		return;

	mutex_lock(&priv->mutex);
	if (test_bit(STATUS_SCANNING, &priv->status) &&
	    !test_bit(STATUS_SCAN_ABORTING, &priv->status)) {
		IWL_DEBUG_SCAN(priv, "Scan completion watchdog (%dms)\n",
			       jiffies_to_msecs(IWL_SCAN_CHECK_WATCHDOG));

		if (!test_bit(STATUS_EXIT_PENDING, &priv->status))
			iwl_send_scan_abort(priv);
	}
	mutex_unlock(&priv->mutex);
}
EXPORT_SYMBOL(iwl_bg_scan_check);

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
EXPORT_SYMBOL(iwl_fill_probe_req);

void iwl_bg_abort_scan(struct work_struct *work)
{
	struct iwl_priv *priv = container_of(work, struct iwl_priv, abort_scan);

	if (!test_bit(STATUS_READY, &priv->status) ||
	    !test_bit(STATUS_GEO_CONFIGURED, &priv->status))
		return;

	cancel_delayed_work(&priv->scan_check);

	mutex_lock(&priv->mutex);
	if (test_bit(STATUS_SCAN_ABORTING, &priv->status))
		iwl_send_scan_abort(priv);
	mutex_unlock(&priv->mutex);
}
EXPORT_SYMBOL(iwl_bg_abort_scan);

void iwl_bg_scan_completed(struct work_struct *work)
{
	struct iwl_priv *priv =
	    container_of(work, struct iwl_priv, scan_completed);
	bool internal = false;

	IWL_DEBUG_SCAN(priv, "SCAN complete scan\n");

	cancel_delayed_work(&priv->scan_check);

	mutex_lock(&priv->mutex);
	if (priv->is_internal_short_scan) {
		priv->is_internal_short_scan = false;
		IWL_DEBUG_SCAN(priv, "internal short scan completed\n");
		internal = true;
	} else {
		priv->scan_request = NULL;
		priv->scan_vif = NULL;
	}

	if (test_bit(STATUS_EXIT_PENDING, &priv->status))
		goto out;

	if (internal && priv->scan_request)
		iwl_scan_initiate(priv, priv->scan_vif);

	/* Since setting the TXPOWER may have been deferred while
	 * performing the scan, fire one off */
	iwl_set_tx_power(priv, priv->tx_power_user_lmt, true);

	/*
	 * Since setting the RXON may have been deferred while
	 * performing the scan, fire one off if needed
	 */
	if (memcmp(&priv->active_rxon,
		   &priv->staging_rxon, sizeof(priv->staging_rxon)))
		iwlcore_commit_rxon(priv);

 out:
	mutex_unlock(&priv->mutex);

	/*
	 * Do not hold mutex here since this will cause mac80211 to call
	 * into driver again into functions that will attempt to take
	 * mutex.
	 */
	if (!internal)
		ieee80211_scan_completed(priv->hw, false);
}
EXPORT_SYMBOL(iwl_bg_scan_completed);

void iwl_setup_scan_deferred_work(struct iwl_priv *priv)
{
	INIT_WORK(&priv->scan_completed, iwl_bg_scan_completed);
	INIT_WORK(&priv->abort_scan, iwl_bg_abort_scan);
	INIT_WORK(&priv->start_internal_scan, iwl_bg_start_internal_scan);
	INIT_DELAYED_WORK(&priv->scan_check, iwl_bg_scan_check);
}
EXPORT_SYMBOL(iwl_setup_scan_deferred_work);

