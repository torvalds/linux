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
#define IL_ACTIVE_DWELL_TIME_24    (30)       /* all times in msec */
#define IL_ACTIVE_DWELL_TIME_52    (20)

#define IL_ACTIVE_DWELL_FACTOR_24GHZ (3)
#define IL_ACTIVE_DWELL_FACTOR_52GHZ (2)

/* For passive scan, listen PASSIVE_DWELL_TIME (msec) on each channel.
 * Must be set longer than active dwell time.
 * For the most reliable scan, set > AP beacon interval (typically 100msec). */
#define IL_PASSIVE_DWELL_TIME_24   (20)       /* all times in msec */
#define IL_PASSIVE_DWELL_TIME_52   (10)
#define IL_PASSIVE_DWELL_BASE      (100)
#define IL_CHANNEL_TUNE_TIME       5

static int il_send_scan_abort(struct il_priv *il)
{
	int ret;
	struct il_rx_pkt *pkt;
	struct il_host_cmd cmd = {
		.id = REPLY_SCAN_ABORT_CMD,
		.flags = CMD_WANT_SKB,
	};

	/* Exit instantly with error when device is not ready
	 * to receive scan abort command or it does not perform
	 * hardware scan currently */
	if (!test_bit(STATUS_READY, &il->status) ||
	    !test_bit(STATUS_GEO_CONFIGURED, &il->status) ||
	    !test_bit(STATUS_SCAN_HW, &il->status) ||
	    test_bit(STATUS_FW_ERROR, &il->status) ||
	    test_bit(STATUS_EXIT_PENDING, &il->status))
		return -EIO;

	ret = il_send_cmd_sync(il, &cmd);
	if (ret)
		return ret;

	pkt = (struct il_rx_pkt *)cmd.reply_page;
	if (pkt->u.status != CAN_ABORT_STATUS) {
		/* The scan abort will return 1 for success or
		 * 2 for "failure".  A failure condition can be
		 * due to simply not being in an active scan which
		 * can occur if we send the scan abort before we
		 * the microcode has notified us that a scan is
		 * completed. */
		D_SCAN("SCAN_ABORT ret %d.\n", pkt->u.status);
		ret = -EIO;
	}

	il_free_pages(il, cmd.reply_page);
	return ret;
}

static void il_complete_scan(struct il_priv *il, bool aborted)
{
	/* check if scan was requested from mac80211 */
	if (il->scan_request) {
		D_SCAN("Complete scan in mac80211\n");
		ieee80211_scan_completed(il->hw, aborted);
	}

	il->scan_vif = NULL;
	il->scan_request = NULL;
}

void il_force_scan_end(struct il_priv *il)
{
	lockdep_assert_held(&il->mutex);

	if (!test_bit(STATUS_SCANNING, &il->status)) {
		D_SCAN("Forcing scan end while not scanning\n");
		return;
	}

	D_SCAN("Forcing scan end\n");
	clear_bit(STATUS_SCANNING, &il->status);
	clear_bit(STATUS_SCAN_HW, &il->status);
	clear_bit(STATUS_SCAN_ABORTING, &il->status);
	il_complete_scan(il, true);
}

static void il_do_scan_abort(struct il_priv *il)
{
	int ret;

	lockdep_assert_held(&il->mutex);

	if (!test_bit(STATUS_SCANNING, &il->status)) {
		D_SCAN("Not performing scan to abort\n");
		return;
	}

	if (test_and_set_bit(STATUS_SCAN_ABORTING, &il->status)) {
		D_SCAN("Scan abort in progress\n");
		return;
	}

	ret = il_send_scan_abort(il);
	if (ret) {
		D_SCAN("Send scan abort failed %d\n", ret);
		il_force_scan_end(il);
	} else
		D_SCAN("Successfully send scan abort\n");
}

/**
 * il_scan_cancel - Cancel any currently executing HW scan
 */
int il_scan_cancel(struct il_priv *il)
{
	D_SCAN("Queuing abort scan\n");
	queue_work(il->workqueue, &il->abort_scan);
	return 0;
}
EXPORT_SYMBOL(il_scan_cancel);

/**
 * il_scan_cancel_timeout - Cancel any currently executing HW scan
 * @ms: amount of time to wait (in milliseconds) for scan to abort
 *
 */
int il_scan_cancel_timeout(struct il_priv *il, unsigned long ms)
{
	unsigned long timeout = jiffies + msecs_to_jiffies(ms);

	lockdep_assert_held(&il->mutex);

	D_SCAN("Scan cancel timeout\n");

	il_do_scan_abort(il);

	while (time_before_eq(jiffies, timeout)) {
		if (!test_bit(STATUS_SCAN_HW, &il->status))
			break;
		msleep(20);
	}

	return test_bit(STATUS_SCAN_HW, &il->status);
}
EXPORT_SYMBOL(il_scan_cancel_timeout);

/* Service response to REPLY_SCAN_CMD (0x80) */
static void il_rx_reply_scan(struct il_priv *il,
			      struct il_rx_buf *rxb)
{
#ifdef CONFIG_IWLEGACY_DEBUG
	struct il_rx_pkt *pkt = rxb_addr(rxb);
	struct il_scanreq_notification *notif =
	    (struct il_scanreq_notification *)pkt->u.raw;

	D_SCAN("Scan request status = 0x%x\n", notif->status);
#endif
}

/* Service SCAN_START_NOTIFICATION (0x82) */
static void il_rx_scan_start_notif(struct il_priv *il,
				    struct il_rx_buf *rxb)
{
	struct il_rx_pkt *pkt = rxb_addr(rxb);
	struct il_scanstart_notification *notif =
	    (struct il_scanstart_notification *)pkt->u.raw;
	il->scan_start_tsf = le32_to_cpu(notif->tsf_low);
	D_SCAN("Scan start: "
		       "%d [802.11%s] "
		       "(TSF: 0x%08X:%08X) - %d (beacon timer %u)\n",
		       notif->channel,
		       notif->band ? "bg" : "a",
		       le32_to_cpu(notif->tsf_high),
		       le32_to_cpu(notif->tsf_low),
		       notif->status, notif->beacon_timer);
}

/* Service SCAN_RESULTS_NOTIFICATION (0x83) */
static void il_rx_scan_results_notif(struct il_priv *il,
				      struct il_rx_buf *rxb)
{
#ifdef CONFIG_IWLEGACY_DEBUG
	struct il_rx_pkt *pkt = rxb_addr(rxb);
	struct il_scanresults_notification *notif =
	    (struct il_scanresults_notification *)pkt->u.raw;

	D_SCAN("Scan ch.res: "
		       "%d [802.11%s] "
		       "(TSF: 0x%08X:%08X) - %d "
		       "elapsed=%lu usec\n",
		       notif->channel,
		       notif->band ? "bg" : "a",
		       le32_to_cpu(notif->tsf_high),
		       le32_to_cpu(notif->tsf_low),
		       le32_to_cpu(notif->stats[0]),
		       le32_to_cpu(notif->tsf_low) - il->scan_start_tsf);
#endif
}

/* Service SCAN_COMPLETE_NOTIFICATION (0x84) */
static void il_rx_scan_complete_notif(struct il_priv *il,
				       struct il_rx_buf *rxb)
{

#ifdef CONFIG_IWLEGACY_DEBUG
	struct il_rx_pkt *pkt = rxb_addr(rxb);
	struct il_scancomplete_notification *scan_notif = (void *)pkt->u.raw;
#endif

	D_SCAN(
			"Scan complete: %d channels (TSF 0x%08X:%08X) - %d\n",
		       scan_notif->scanned_channels,
		       scan_notif->tsf_low,
		       scan_notif->tsf_high, scan_notif->status);

	/* The HW is no longer scanning */
	clear_bit(STATUS_SCAN_HW, &il->status);

	D_SCAN("Scan on %sGHz took %dms\n",
		       (il->scan_band == IEEE80211_BAND_2GHZ) ? "2.4" : "5.2",
		       jiffies_to_msecs(jiffies - il->scan_start));

	queue_work(il->workqueue, &il->scan_completed);
}

void il_setup_rx_scan_handlers(struct il_priv *il)
{
	/* scan handlers */
	il->rx_handlers[REPLY_SCAN_CMD] = il_rx_reply_scan;
	il->rx_handlers[SCAN_START_NOTIFICATION] =
					il_rx_scan_start_notif;
	il->rx_handlers[SCAN_RESULTS_NOTIFICATION] =
					il_rx_scan_results_notif;
	il->rx_handlers[SCAN_COMPLETE_NOTIFICATION] =
					il_rx_scan_complete_notif;
}
EXPORT_SYMBOL(il_setup_rx_scan_handlers);

inline u16 il_get_active_dwell_time(struct il_priv *il,
				     enum ieee80211_band band,
				     u8 n_probes)
{
	if (band == IEEE80211_BAND_5GHZ)
		return IL_ACTIVE_DWELL_TIME_52 +
			IL_ACTIVE_DWELL_FACTOR_52GHZ * (n_probes + 1);
	else
		return IL_ACTIVE_DWELL_TIME_24 +
			IL_ACTIVE_DWELL_FACTOR_24GHZ * (n_probes + 1);
}
EXPORT_SYMBOL(il_get_active_dwell_time);

u16 il_get_passive_dwell_time(struct il_priv *il,
			       enum ieee80211_band band,
			       struct ieee80211_vif *vif)
{
	struct il_rxon_context *ctx = &il->ctx;
	u16 value;

	u16 passive = (band == IEEE80211_BAND_2GHZ) ?
	    IL_PASSIVE_DWELL_BASE + IL_PASSIVE_DWELL_TIME_24 :
	    IL_PASSIVE_DWELL_BASE + IL_PASSIVE_DWELL_TIME_52;

	if (il_is_any_associated(il)) {
		/*
		 * If we're associated, we clamp the maximum passive
		 * dwell time to be 98% of the smallest beacon interval
		 * (minus 2 * channel tune time)
		 */
		value = ctx->vif ? ctx->vif->bss_conf.beacon_int : 0;
		if (value > IL_PASSIVE_DWELL_BASE || !value)
			value = IL_PASSIVE_DWELL_BASE;
		value = (value * 98) / 100 - IL_CHANNEL_TUNE_TIME * 2;
		passive = min(value, passive);
	}

	return passive;
}
EXPORT_SYMBOL(il_get_passive_dwell_time);

void il_init_scan_params(struct il_priv *il)
{
	u8 ant_idx = fls(il->hw_params.valid_tx_ant) - 1;
	if (!il->scan_tx_ant[IEEE80211_BAND_5GHZ])
		il->scan_tx_ant[IEEE80211_BAND_5GHZ] = ant_idx;
	if (!il->scan_tx_ant[IEEE80211_BAND_2GHZ])
		il->scan_tx_ant[IEEE80211_BAND_2GHZ] = ant_idx;
}
EXPORT_SYMBOL(il_init_scan_params);

static int il_scan_initiate(struct il_priv *il,
				    struct ieee80211_vif *vif)
{
	int ret;

	lockdep_assert_held(&il->mutex);

	if (WARN_ON(!il->cfg->ops->utils->request_scan))
		return -EOPNOTSUPP;

	cancel_delayed_work(&il->scan_check);

	if (!il_is_ready_rf(il)) {
		IL_WARN("Request scan called when driver not ready.\n");
		return -EIO;
	}

	if (test_bit(STATUS_SCAN_HW, &il->status)) {
		D_SCAN(
			"Multiple concurrent scan requests in parallel.\n");
		return -EBUSY;
	}

	if (test_bit(STATUS_SCAN_ABORTING, &il->status)) {
		D_SCAN("Scan request while abort pending.\n");
		return -EBUSY;
	}

	D_SCAN("Starting scan...\n");

	set_bit(STATUS_SCANNING, &il->status);
	il->scan_start = jiffies;

	ret = il->cfg->ops->utils->request_scan(il, vif);
	if (ret) {
		clear_bit(STATUS_SCANNING, &il->status);
		return ret;
	}

	queue_delayed_work(il->workqueue, &il->scan_check,
			   IL_SCAN_CHECK_WATCHDOG);

	return 0;
}

int il_mac_hw_scan(struct ieee80211_hw *hw,
		    struct ieee80211_vif *vif,
		    struct cfg80211_scan_request *req)
{
	struct il_priv *il = hw->priv;
	int ret;

	D_MAC80211("enter\n");

	if (req->n_channels == 0)
		return -EINVAL;

	mutex_lock(&il->mutex);

	if (test_bit(STATUS_SCANNING, &il->status)) {
		D_SCAN("Scan already in progress.\n");
		ret = -EAGAIN;
		goto out_unlock;
	}

	/* mac80211 will only ask for one band at a time */
	il->scan_request = req;
	il->scan_vif = vif;
	il->scan_band = req->channels[0]->band;

	ret = il_scan_initiate(il, vif);

	D_MAC80211("leave\n");

out_unlock:
	mutex_unlock(&il->mutex);

	return ret;
}
EXPORT_SYMBOL(il_mac_hw_scan);

static void il_bg_scan_check(struct work_struct *data)
{
	struct il_priv *il =
	    container_of(data, struct il_priv, scan_check.work);

	D_SCAN("Scan check work\n");

	/* Since we are here firmware does not finish scan and
	 * most likely is in bad shape, so we don't bother to
	 * send abort command, just force scan complete to mac80211 */
	mutex_lock(&il->mutex);
	il_force_scan_end(il);
	mutex_unlock(&il->mutex);
}

/**
 * il_fill_probe_req - fill in all required fields and IE for probe request
 */

u16
il_fill_probe_req(struct il_priv *il, struct ieee80211_mgmt *frame,
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
	memcpy(frame->da, il_bcast_addr, ETH_ALEN);
	memcpy(frame->sa, ta, ETH_ALEN);
	memcpy(frame->bssid, il_bcast_addr, ETH_ALEN);
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
EXPORT_SYMBOL(il_fill_probe_req);

static void il_bg_abort_scan(struct work_struct *work)
{
	struct il_priv *il = container_of(work, struct il_priv, abort_scan);

	D_SCAN("Abort scan work\n");

	/* We keep scan_check work queued in case when firmware will not
	 * report back scan completed notification */
	mutex_lock(&il->mutex);
	il_scan_cancel_timeout(il, 200);
	mutex_unlock(&il->mutex);
}

static void il_bg_scan_completed(struct work_struct *work)
{
	struct il_priv *il =
	    container_of(work, struct il_priv, scan_completed);
	bool aborted;

	D_SCAN("Completed scan.\n");

	cancel_delayed_work(&il->scan_check);

	mutex_lock(&il->mutex);

	aborted = test_and_clear_bit(STATUS_SCAN_ABORTING, &il->status);
	if (aborted)
		D_SCAN("Aborted scan completed.\n");

	if (!test_and_clear_bit(STATUS_SCANNING, &il->status)) {
		D_SCAN("Scan already completed.\n");
		goto out_settings;
	}

	il_complete_scan(il, aborted);

out_settings:
	/* Can we still talk to firmware ? */
	if (!il_is_ready_rf(il))
		goto out;

	/*
	 * We do not commit power settings while scan is pending,
	 * do it now if the settings changed.
	 */
	il_power_set_mode(il, &il->power_data.sleep_cmd_next, false);
	il_set_tx_power(il, il->tx_power_next, false);

	il->cfg->ops->utils->post_scan(il);

out:
	mutex_unlock(&il->mutex);
}

void il_setup_scan_deferred_work(struct il_priv *il)
{
	INIT_WORK(&il->scan_completed, il_bg_scan_completed);
	INIT_WORK(&il->abort_scan, il_bg_abort_scan);
	INIT_DELAYED_WORK(&il->scan_check, il_bg_scan_check);
}
EXPORT_SYMBOL(il_setup_scan_deferred_work);

void il_cancel_scan_deferred_work(struct il_priv *il)
{
	cancel_work_sync(&il->abort_scan);
	cancel_work_sync(&il->scan_completed);

	if (cancel_delayed_work_sync(&il->scan_check)) {
		mutex_lock(&il->mutex);
		il_force_scan_end(il);
		mutex_unlock(&il->mutex);
	}
}
EXPORT_SYMBOL(il_cancel_scan_deferred_work);
