/******************************************************************************
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2008 Intel Corporation. All rights reserved.
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
 * Tomas Winkler <tomas.winkler@intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *****************************************************************************/
#include <net/mac80211.h>
#include <linux/etherdevice.h>

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

/* For faster active scanning, scan will move to the next channel if fewer than
 * PLCP_QUIET_THRESH packets are heard on this channel within
 * ACTIVE_QUIET_TIME after sending probe request.  This shortens the dwell
 * time if it's a quiet channel (nothing responded to our probe, and there's
 * no other traffic).
 * Disable "quiet" feature by setting PLCP_QUIET_THRESH to 0. */
#define IWL_PLCP_QUIET_THRESH       __constant_cpu_to_le16(1)  /* packets */
#define IWL_ACTIVE_QUIET_TIME       __constant_cpu_to_le16(10)  /* msec */

/* For passive scan, listen PASSIVE_DWELL_TIME (msec) on each channel.
 * Must be set longer than active dwell time.
 * For the most reliable scan, set > AP beacon interval (typically 100msec). */
#define IWL_PASSIVE_DWELL_TIME_24   (20)       /* all times in msec */
#define IWL_PASSIVE_DWELL_TIME_52   (10)
#define IWL_PASSIVE_DWELL_BASE      (100)
#define IWL_CHANNEL_TUNE_TIME       5

#define IWL_SCAN_PROBE_MASK(n) 	cpu_to_le32((BIT(n) | (BIT(n) - BIT(1))))


static int scan_tx_ant[3] = {
	RATE_MCS_ANT_A_MSK, RATE_MCS_ANT_B_MSK, RATE_MCS_ANT_C_MSK
};



static int iwl_is_empty_essid(const char *essid, int essid_len)
{
	/* Single white space is for Linksys APs */
	if (essid_len == 1 && essid[0] == ' ')
		return 1;

	/* Otherwise, if the entire essid is 0, we assume it is hidden */
	while (essid_len) {
		essid_len--;
		if (essid[essid_len] != '\0')
			return 0;
	}

	return 1;
}



static const char *iwl_escape_essid(const char *essid, u8 essid_len)
{
	static char escaped[IW_ESSID_MAX_SIZE * 2 + 1];
	const char *s = essid;
	char *d = escaped;

	if (iwl_is_empty_essid(essid, essid_len)) {
		memcpy(escaped, "<hidden>", sizeof("<hidden>"));
		return escaped;
	}

	essid_len = min(essid_len, (u8) IW_ESSID_MAX_SIZE);
	while (essid_len--) {
		if (*s == '\0') {
			*d++ = '\\';
			*d++ = '0';
			s++;
		} else
			*d++ = *s++;
	}
	*d = '\0';
	return escaped;
}

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
		if (!test_bit(STATUS_SCAN_ABORTING, &priv->status)) {
			IWL_DEBUG_SCAN("Queuing scan abort.\n");
			set_bit(STATUS_SCAN_ABORTING, &priv->status);
			queue_work(priv->workqueue, &priv->abort_scan);

		} else
			IWL_DEBUG_SCAN("Scan abort already in progress.\n");

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
	struct iwl_rx_packet *res;
	struct iwl_host_cmd cmd = {
		.id = REPLY_SCAN_ABORT_CMD,
		.meta.flags = CMD_WANT_SKB,
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

	res = (struct iwl_rx_packet *)cmd.meta.u.skb->data;
	if (res->u.status != CAN_ABORT_STATUS) {
		/* The scan abort will return 1 for success or
		 * 2 for "failure".  A failure condition can be
		 * due to simply not being in an active scan which
		 * can occur if we send the scan abort before we
		 * the microcode has notified us that a scan is
		 * completed. */
		IWL_DEBUG_INFO("SCAN_ABORT returned %d.\n", res->u.status);
		clear_bit(STATUS_SCAN_ABORTING, &priv->status);
		clear_bit(STATUS_SCAN_HW, &priv->status);
	}

	priv->alloc_rxb_skb--;
	dev_kfree_skb_any(cmd.meta.u.skb);

	return ret;
}


/* Service response to REPLY_SCAN_CMD (0x80) */
static void iwl_rx_reply_scan(struct iwl_priv *priv,
			      struct iwl_rx_mem_buffer *rxb)
{
#ifdef CONFIG_IWLWIFI_DEBUG
	struct iwl_rx_packet *pkt = (struct iwl_rx_packet *)rxb->skb->data;
	struct iwl_scanreq_notification *notif =
	    (struct iwl_scanreq_notification *)pkt->u.raw;

	IWL_DEBUG_RX("Scan request status = 0x%x\n", notif->status);
#endif
}

/* Service SCAN_START_NOTIFICATION (0x82) */
static void iwl_rx_scan_start_notif(struct iwl_priv *priv,
				    struct iwl_rx_mem_buffer *rxb)
{
	struct iwl_rx_packet *pkt = (struct iwl_rx_packet *)rxb->skb->data;
	struct iwl_scanstart_notification *notif =
	    (struct iwl_scanstart_notification *)pkt->u.raw;
	priv->scan_start_tsf = le32_to_cpu(notif->tsf_low);
	IWL_DEBUG_SCAN("Scan start: "
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
	struct iwl_rx_packet *pkt = (struct iwl_rx_packet *)rxb->skb->data;
	struct iwl_scanresults_notification *notif =
	    (struct iwl_scanresults_notification *)pkt->u.raw;

	IWL_DEBUG_SCAN("Scan ch.res: "
		       "%d [802.11%s] "
		       "(TSF: 0x%08X:%08X) - %d "
		       "elapsed=%lu usec (%dms since last)\n",
		       notif->channel,
		       notif->band ? "bg" : "a",
		       le32_to_cpu(notif->tsf_high),
		       le32_to_cpu(notif->tsf_low),
		       le32_to_cpu(notif->statistics[0]),
		       le32_to_cpu(notif->tsf_low) - priv->scan_start_tsf,
		       jiffies_to_msecs(elapsed_jiffies
					(priv->last_scan_jiffies, jiffies)));
#endif

	priv->last_scan_jiffies = jiffies;
	priv->next_scan_jiffies = 0;
}

/* Service SCAN_COMPLETE_NOTIFICATION (0x84) */
static void iwl_rx_scan_complete_notif(struct iwl_priv *priv,
				       struct iwl_rx_mem_buffer *rxb)
{
#ifdef CONFIG_IWLWIFI_DEBUG
	struct iwl_rx_packet *pkt = (struct iwl_rx_packet *)rxb->skb->data;
	struct iwl_scancomplete_notification *scan_notif = (void *)pkt->u.raw;

	IWL_DEBUG_SCAN("Scan complete: %d channels (TSF 0x%08X:%08X) - %d\n",
		       scan_notif->scanned_channels,
		       scan_notif->tsf_low,
		       scan_notif->tsf_high, scan_notif->status);
#endif

	/* The HW is no longer scanning */
	clear_bit(STATUS_SCAN_HW, &priv->status);

	/* The scan completion notification came in, so kill that timer... */
	cancel_delayed_work(&priv->scan_check);

	IWL_DEBUG_INFO("Scan pass on %sGHz took %dms\n",
		       (priv->scan_bands & BIT(IEEE80211_BAND_2GHZ)) ?
						"2.4" : "5.2",
		       jiffies_to_msecs(elapsed_jiffies
					(priv->scan_pass_start, jiffies)));

	/* Remove this scanned band from the list of pending
	 * bands to scan, band G precedes A in order of scanning
	 * as seen in iwl_bg_request_scan */
	if (priv->scan_bands & BIT(IEEE80211_BAND_2GHZ))
		priv->scan_bands &= ~BIT(IEEE80211_BAND_2GHZ);
	else if (priv->scan_bands &  BIT(IEEE80211_BAND_5GHZ))
		priv->scan_bands &= ~BIT(IEEE80211_BAND_5GHZ);

	/* If a request to abort was given, or the scan did not succeed
	 * then we reset the scan state machine and terminate,
	 * re-queuing another scan if one has been requested */
	if (test_bit(STATUS_SCAN_ABORTING, &priv->status)) {
		IWL_DEBUG_INFO("Aborted scan completed.\n");
		clear_bit(STATUS_SCAN_ABORTING, &priv->status);
	} else {
		/* If there are more bands on this scan pass reschedule */
		if (priv->scan_bands)
			goto reschedule;
	}

	priv->last_scan_jiffies = jiffies;
	priv->next_scan_jiffies = 0;
	IWL_DEBUG_INFO("Setting scan to off\n");

	clear_bit(STATUS_SCANNING, &priv->status);

	IWL_DEBUG_INFO("Scan took %dms\n",
		jiffies_to_msecs(elapsed_jiffies(priv->scan_start, jiffies)));

	queue_work(priv->workqueue, &priv->scan_completed);

	return;

reschedule:
	priv->scan_pass_start = jiffies;
	queue_work(priv->workqueue, &priv->request_scan);
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

static inline u16 iwl_get_active_dwell_time(struct iwl_priv *priv,
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

static u16 iwl_get_passive_dwell_time(struct iwl_priv *priv,
				      enum ieee80211_band band)
{
	u16 passive = (band == IEEE80211_BAND_2GHZ) ?
	    IWL_PASSIVE_DWELL_BASE + IWL_PASSIVE_DWELL_TIME_24 :
	    IWL_PASSIVE_DWELL_BASE + IWL_PASSIVE_DWELL_TIME_52;

	if (iwl_is_associated(priv)) {
		/* If we're associated, we clamp the maximum passive
		 * dwell time to be 98% of the beacon interval (minus
		 * 2 * channel tune time) */
		passive = priv->beacon_int;
		if ((passive > IWL_PASSIVE_DWELL_BASE) || !passive)
			passive = IWL_PASSIVE_DWELL_BASE;
		passive = (passive * 98) / 100 - IWL_CHANNEL_TUNE_TIME * 2;
	}

	return passive;
}

static int iwl_get_channels_for_scan(struct iwl_priv *priv,
				     enum ieee80211_band band,
				     u8 is_active, u8 n_probes,
				     struct iwl_scan_channel *scan_ch)
{
	const struct ieee80211_channel *channels = NULL;
	const struct ieee80211_supported_band *sband;
	const struct iwl_channel_info *ch_info;
	u16 passive_dwell = 0;
	u16 active_dwell = 0;
	int added, i;
	u16 channel;

	sband = iwl_get_hw_mode(priv, band);
	if (!sband)
		return 0;

	channels = sband->channels;

	active_dwell = iwl_get_active_dwell_time(priv, band, n_probes);
	passive_dwell = iwl_get_passive_dwell_time(priv, band);

	if (passive_dwell <= active_dwell)
		passive_dwell = active_dwell + 1;

	for (i = 0, added = 0; i < sband->n_channels; i++) {
		if (channels[i].flags & IEEE80211_CHAN_DISABLED)
			continue;

		channel =
			ieee80211_frequency_to_channel(channels[i].center_freq);
		scan_ch->channel = cpu_to_le16(channel);

		ch_info = iwl_get_channel_info(priv, band, channel);
		if (!is_channel_valid(ch_info)) {
			IWL_DEBUG_SCAN("Channel %d is INVALID for this band.\n",
					channel);
			continue;
		}

		if (!is_active || is_channel_passive(ch_info) ||
		    (channels[i].flags & IEEE80211_CHAN_PASSIVE_SCAN))
			scan_ch->type = SCAN_CHANNEL_TYPE_PASSIVE;
		else
			scan_ch->type = SCAN_CHANNEL_TYPE_ACTIVE;

		if (n_probes)
			scan_ch->type |= IWL_SCAN_PROBE_MASK(n_probes);

		scan_ch->active_dwell = cpu_to_le16(active_dwell);
		scan_ch->passive_dwell = cpu_to_le16(passive_dwell);

		/* Set txpower levels to defaults */
		scan_ch->dsp_atten = 110;

		/* NOTE: if we were doing 6Mb OFDM for scans we'd use
		 * power level:
		 * scan_ch->tx_gain = ((1 << 5) | (2 << 3)) | 3;
		 */
		if (band == IEEE80211_BAND_5GHZ)
			scan_ch->tx_gain = ((1 << 5) | (3 << 3)) | 3;
		else
			scan_ch->tx_gain = ((1 << 5) | (5 << 3));

		IWL_DEBUG_SCAN("Scanning ch=%d prob=0x%X [%s %d]\n",
			       channel, le32_to_cpu(scan_ch->type),
			       (scan_ch->type & SCAN_CHANNEL_TYPE_ACTIVE) ?
				"ACTIVE" : "PASSIVE",
			       (scan_ch->type & SCAN_CHANNEL_TYPE_ACTIVE) ?
			       active_dwell : passive_dwell);

		scan_ch++;
		added++;
	}

	IWL_DEBUG_SCAN("total channels to scan %d \n", added);
	return added;
}

void iwl_init_scan_params(struct iwl_priv *priv)
{
	if (!priv->scan_tx_ant[IEEE80211_BAND_5GHZ])
		priv->scan_tx_ant[IEEE80211_BAND_5GHZ] = RATE_MCS_ANT_INIT_IND;
	if (!priv->scan_tx_ant[IEEE80211_BAND_2GHZ])
		priv->scan_tx_ant[IEEE80211_BAND_2GHZ] = RATE_MCS_ANT_INIT_IND;
}

int iwl_scan_initiate(struct iwl_priv *priv)
{
	if (priv->iw_mode == IEEE80211_IF_TYPE_AP) {
		IWL_ERROR("APs don't scan.\n");
		return 0;
	}

	if (!iwl_is_ready_rf(priv)) {
		IWL_DEBUG_SCAN("Aborting scan due to not ready.\n");
		return -EIO;
	}

	if (test_bit(STATUS_SCANNING, &priv->status)) {
		IWL_DEBUG_SCAN("Scan already in progress.\n");
		return -EAGAIN;
	}

	if (test_bit(STATUS_SCAN_ABORTING, &priv->status)) {
		IWL_DEBUG_SCAN("Scan request while abort pending.  "
			       "Queuing.\n");
		return -EAGAIN;
	}

	IWL_DEBUG_INFO("Starting scan...\n");
	if (priv->cfg->sku & IWL_SKU_G)
		priv->scan_bands |= BIT(IEEE80211_BAND_2GHZ);
	if (priv->cfg->sku & IWL_SKU_A)
		priv->scan_bands |= BIT(IEEE80211_BAND_5GHZ);
	set_bit(STATUS_SCANNING, &priv->status);
	priv->scan_start = jiffies;
	priv->scan_pass_start = priv->scan_start;

	queue_work(priv->workqueue, &priv->request_scan);

	return 0;
}
EXPORT_SYMBOL(iwl_scan_initiate);

#define IWL_SCAN_CHECK_WATCHDOG (7 * HZ)

static void iwl_bg_scan_check(struct work_struct *data)
{
	struct iwl_priv *priv =
	    container_of(data, struct iwl_priv, scan_check.work);

	if (test_bit(STATUS_EXIT_PENDING, &priv->status))
		return;

	mutex_lock(&priv->mutex);
	if (test_bit(STATUS_SCANNING, &priv->status) ||
	    test_bit(STATUS_SCAN_ABORTING, &priv->status)) {
		IWL_DEBUG(IWL_DL_SCAN, "Scan completion watchdog resetting "
			"adapter (%dms)\n",
			jiffies_to_msecs(IWL_SCAN_CHECK_WATCHDOG));

		if (!test_bit(STATUS_EXIT_PENDING, &priv->status))
			iwl_send_scan_abort(priv);
	}
	mutex_unlock(&priv->mutex);
}
/**
 * iwl_supported_rate_to_ie - fill in the supported rate in IE field
 *
 * return : set the bit for each supported rate insert in ie
 */
static u16 iwl_supported_rate_to_ie(u8 *ie, u16 supported_rate,
				    u16 basic_rate, int *left)
{
	u16 ret_rates = 0, bit;
	int i;
	u8 *cnt = ie;
	u8 *rates = ie + 1;

	for (bit = 1, i = 0; i < IWL_RATE_COUNT; i++, bit <<= 1) {
		if (bit & supported_rate) {
			ret_rates |= bit;
			rates[*cnt] = iwl_rates[i].ieee |
				((bit & basic_rate) ? 0x80 : 0x00);
			(*cnt)++;
			(*left)--;
			if ((*left <= 0) ||
			    (*cnt >= IWL_SUPPORTED_RATES_IE_LEN))
				break;
		}
	}

	return ret_rates;
}


static void iwl_ht_cap_to_ie(const struct ieee80211_supported_band *sband,
			     u8 *pos, int *left)
{
	struct ieee80211_ht_cap *ht_cap;

	if (!sband || !sband->ht_info.ht_supported)
		return;

	if (*left < sizeof(struct ieee80211_ht_cap))
		return;

	*pos++ = sizeof(struct ieee80211_ht_cap);
	ht_cap = (struct ieee80211_ht_cap *) pos;

	ht_cap->cap_info = cpu_to_le16(sband->ht_info.cap);
	memcpy(ht_cap->supp_mcs_set, sband->ht_info.supp_mcs_set, 16);
	ht_cap->ampdu_params_info =
		(sband->ht_info.ampdu_factor & IEEE80211_HT_CAP_AMPDU_FACTOR) |
		((sband->ht_info.ampdu_density << 2) &
			IEEE80211_HT_CAP_AMPDU_DENSITY);
	*left -= sizeof(struct ieee80211_ht_cap);
}

/**
 * iwl_fill_probe_req - fill in all required fields and IE for probe request
 */

static u16 iwl_fill_probe_req(struct iwl_priv *priv,
				  enum ieee80211_band band,
				  struct ieee80211_mgmt *frame,
				  int left)
{
	int len = 0;
	u8 *pos = NULL;
	u16 active_rates, ret_rates, cck_rates, active_rate_basic;
	const struct ieee80211_supported_band *sband =
						iwl_get_hw_mode(priv, band);


	/* Make sure there is enough space for the probe request,
	 * two mandatory IEs and the data */
	left -= 24;
	if (left < 0)
		return 0;

	frame->frame_control = cpu_to_le16(IEEE80211_STYPE_PROBE_REQ);
	memcpy(frame->da, iwl_bcast_addr, ETH_ALEN);
	memcpy(frame->sa, priv->mac_addr, ETH_ALEN);
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

	/* fill in supported rate */
	left -= 2;
	if (left < 0)
		return 0;

	*pos++ = WLAN_EID_SUPP_RATES;
	*pos = 0;

	/* exclude 60M rate */
	active_rates = priv->rates_mask;
	active_rates &= ~IWL_RATE_60M_MASK;

	active_rate_basic = active_rates & IWL_BASIC_RATES_MASK;

	cck_rates = IWL_CCK_RATES_MASK & active_rates;
	ret_rates = iwl_supported_rate_to_ie(pos, cck_rates,
					     active_rate_basic, &left);
	active_rates &= ~ret_rates;

	ret_rates = iwl_supported_rate_to_ie(pos, active_rates,
					     active_rate_basic, &left);
	active_rates &= ~ret_rates;

	len += 2 + *pos;
	pos += (*pos) + 1;

	if (active_rates == 0)
		goto fill_end;

	/* fill in supported extended rate */
	/* ...next IE... */
	left -= 2;
	if (left < 0)
		return 0;
	/* ... fill it in... */
	*pos++ = WLAN_EID_EXT_SUPP_RATES;
	*pos = 0;
	iwl_supported_rate_to_ie(pos, active_rates, active_rate_basic, &left);
	if (*pos > 0) {
		len += 2 + *pos;
		pos += (*pos) + 1;
	} else {
		pos--;
	}

 fill_end:

	left -= 2;
	if (left < 0)
		return 0;

	*pos++ = WLAN_EID_HT_CAPABILITY;
	*pos = 0;
	iwl_ht_cap_to_ie(sband, pos, &left);
	if (*pos > 0)
		len += 2 + *pos;

	return (u16)len;
}

static u32 iwl_scan_tx_ant(struct iwl_priv *priv, enum ieee80211_band band)
{
	int i, ind;

	ind = priv->scan_tx_ant[band];
	for (i = 0; i < priv->hw_params.tx_chains_num; i++) {
		ind = (ind+1) >= priv->hw_params.tx_chains_num ? 0 : ind+1;
		if (priv->hw_params.valid_tx_ant & (1 << ind)) {
			priv->scan_tx_ant[band] = ind;
			break;
		}
	}
	IWL_DEBUG_SCAN("select TX ANT = %c\n", 'A' + ind);
	return scan_tx_ant[ind];
}


static void iwl_bg_request_scan(struct work_struct *data)
{
	struct iwl_priv *priv =
	    container_of(data, struct iwl_priv, request_scan);
	struct iwl_host_cmd cmd = {
		.id = REPLY_SCAN_CMD,
		.len = sizeof(struct iwl_scan_cmd),
		.meta.flags = CMD_SIZE_HUGE,
	};
	struct iwl_scan_cmd *scan;
	struct ieee80211_conf *conf = NULL;
	int ret = 0;
	u32 tx_ant;
	u16 cmd_len;
	enum ieee80211_band band;
	u8 n_probes = 2;
	u8 rx_chain = 0x7; /* bitmap: ABC chains */

	conf = ieee80211_get_hw_conf(priv->hw);

	mutex_lock(&priv->mutex);

	if (!iwl_is_ready(priv)) {
		IWL_WARNING("request scan called when driver not ready.\n");
		goto done;
	}

	/* Make sure the scan wasn't cancelled before this queued work
	 * was given the chance to run... */
	if (!test_bit(STATUS_SCANNING, &priv->status))
		goto done;

	/* This should never be called or scheduled if there is currently
	 * a scan active in the hardware. */
	if (test_bit(STATUS_SCAN_HW, &priv->status)) {
		IWL_DEBUG_INFO("Multiple concurrent scan requests in parallel. "
			       "Ignoring second request.\n");
		ret = -EIO;
		goto done;
	}

	if (test_bit(STATUS_EXIT_PENDING, &priv->status)) {
		IWL_DEBUG_SCAN("Aborting scan due to device shutdown\n");
		goto done;
	}

	if (test_bit(STATUS_SCAN_ABORTING, &priv->status)) {
		IWL_DEBUG_HC("Scan request while abort pending.  Queuing.\n");
		goto done;
	}

	if (iwl_is_rfkill(priv)) {
		IWL_DEBUG_HC("Aborting scan due to RF Kill activation\n");
		goto done;
	}

	if (!test_bit(STATUS_READY, &priv->status)) {
		IWL_DEBUG_HC("Scan request while uninitialized.  Queuing.\n");
		goto done;
	}

	if (!priv->scan_bands) {
		IWL_DEBUG_HC("Aborting scan due to no requested bands\n");
		goto done;
	}

	if (!priv->scan) {
		priv->scan = kmalloc(sizeof(struct iwl_scan_cmd) +
				     IWL_MAX_SCAN_SIZE, GFP_KERNEL);
		if (!priv->scan) {
			ret = -ENOMEM;
			goto done;
		}
	}
	scan = priv->scan;
	memset(scan, 0, sizeof(struct iwl_scan_cmd) + IWL_MAX_SCAN_SIZE);

	scan->quiet_plcp_th = IWL_PLCP_QUIET_THRESH;
	scan->quiet_time = IWL_ACTIVE_QUIET_TIME;

	if (iwl_is_associated(priv)) {
		u16 interval = 0;
		u32 extra;
		u32 suspend_time = 100;
		u32 scan_suspend_time = 100;
		unsigned long flags;

		IWL_DEBUG_INFO("Scanning while associated...\n");

		spin_lock_irqsave(&priv->lock, flags);
		interval = priv->beacon_int;
		spin_unlock_irqrestore(&priv->lock, flags);

		scan->suspend_time = 0;
		scan->max_out_time = cpu_to_le32(200 * 1024);
		if (!interval)
			interval = suspend_time;

		extra = (suspend_time / interval) << 22;
		scan_suspend_time = (extra |
		    ((suspend_time % interval) * 1024));
		scan->suspend_time = cpu_to_le32(scan_suspend_time);
		IWL_DEBUG_SCAN("suspend_time 0x%X beacon interval %d\n",
			       scan_suspend_time, interval);
	}

	/* We should add the ability for user to lock to PASSIVE ONLY */
	if (priv->one_direct_scan) {
		IWL_DEBUG_SCAN("Start direct scan for '%s'\n",
				iwl_escape_essid(priv->direct_ssid,
				priv->direct_ssid_len));
		scan->direct_scan[0].id = WLAN_EID_SSID;
		scan->direct_scan[0].len = priv->direct_ssid_len;
		memcpy(scan->direct_scan[0].ssid,
		       priv->direct_ssid, priv->direct_ssid_len);
		n_probes++;
	} else if (!iwl_is_associated(priv) && priv->essid_len) {
		IWL_DEBUG_SCAN("Start direct scan for '%s' (not associated)\n",
				iwl_escape_essid(priv->essid, priv->essid_len));
		scan->direct_scan[0].id = WLAN_EID_SSID;
		scan->direct_scan[0].len = priv->essid_len;
		memcpy(scan->direct_scan[0].ssid, priv->essid, priv->essid_len);
		n_probes++;
	} else {
		IWL_DEBUG_SCAN("Start indirect scan.\n");
	}

	scan->tx_cmd.tx_flags = TX_CMD_FLG_SEQ_CTL_MSK;
	scan->tx_cmd.sta_id = priv->hw_params.bcast_sta_id;
	scan->tx_cmd.stop_time.life_time = TX_CMD_LIFE_TIME_INFINITE;


	if (priv->scan_bands & BIT(IEEE80211_BAND_2GHZ)) {
		band = IEEE80211_BAND_2GHZ;
		scan->flags = RXON_FLG_BAND_24G_MSK | RXON_FLG_AUTO_DETECT_MSK;
		tx_ant = iwl_scan_tx_ant(priv, band);
		if (priv->active_rxon.flags & RXON_FLG_CHANNEL_MODE_PURE_40_MSK)
			scan->tx_cmd.rate_n_flags =
				iwl_hw_set_rate_n_flags(IWL_RATE_6M_PLCP,
							tx_ant);
		else
			scan->tx_cmd.rate_n_flags =
				iwl_hw_set_rate_n_flags(IWL_RATE_1M_PLCP,
							tx_ant |
							RATE_MCS_CCK_MSK);
		scan->good_CRC_th = 0;
	} else if (priv->scan_bands & BIT(IEEE80211_BAND_5GHZ)) {
		band = IEEE80211_BAND_5GHZ;
		tx_ant = iwl_scan_tx_ant(priv, band);
		scan->tx_cmd.rate_n_flags =
				iwl_hw_set_rate_n_flags(IWL_RATE_6M_PLCP,
							tx_ant);
		scan->good_CRC_th = IWL_GOOD_CRC_TH;

		/* Force use of chains B and C (0x6) for scan Rx for 4965
		 * Avoid A (0x1) because of its off-channel reception on A-band.
		 * MIMO is not used here, but value is required */
		if ((priv->hw_rev & CSR_HW_REV_TYPE_MSK) == CSR_HW_REV_TYPE_4965)
			rx_chain = 0x6;
	} else {
		IWL_WARNING("Invalid scan band count\n");
		goto done;
	}

	scan->rx_chain = RXON_RX_CHAIN_DRIVER_FORCE_MSK |
				cpu_to_le16((0x7 << RXON_RX_CHAIN_VALID_POS) |
				(rx_chain << RXON_RX_CHAIN_FORCE_SEL_POS) |
				(0x7 << RXON_RX_CHAIN_FORCE_MIMO_SEL_POS));

	cmd_len = iwl_fill_probe_req(priv, band,
				     (struct ieee80211_mgmt *)scan->data,
				     IWL_MAX_SCAN_SIZE - sizeof(*scan));

	scan->tx_cmd.len = cpu_to_le16(cmd_len);

	if (priv->iw_mode == IEEE80211_IF_TYPE_MNTR)
		scan->filter_flags = RXON_FILTER_PROMISC_MSK;

	scan->filter_flags |= (RXON_FILTER_ACCEPT_GRP_MSK |
			       RXON_FILTER_BCON_AWARE_MSK);

	scan->channel_count =
		iwl_get_channels_for_scan(priv, band, 1, /* active */
			n_probes,
			(void *)&scan->data[le16_to_cpu(scan->tx_cmd.len)]);

	if (scan->channel_count == 0) {
		IWL_DEBUG_SCAN("channel count %d\n", scan->channel_count);
		goto done;
	}

	cmd.len += le16_to_cpu(scan->tx_cmd.len) +
	    scan->channel_count * sizeof(struct iwl_scan_channel);
	cmd.data = scan;
	scan->len = cpu_to_le16(cmd.len);

	set_bit(STATUS_SCAN_HW, &priv->status);
	ret = iwl_send_cmd_sync(priv, &cmd);
	if (ret)
		goto done;

	queue_delayed_work(priv->workqueue, &priv->scan_check,
			   IWL_SCAN_CHECK_WATCHDOG);

	mutex_unlock(&priv->mutex);
	return;

 done:
	/* inform mac80211 scan aborted */
	queue_work(priv->workqueue, &priv->scan_completed);
	mutex_unlock(&priv->mutex);
}

static void iwl_bg_abort_scan(struct work_struct *work)
{
	struct iwl_priv *priv = container_of(work, struct iwl_priv, abort_scan);

	if (!iwl_is_ready(priv))
		return;

	mutex_lock(&priv->mutex);

	set_bit(STATUS_SCAN_ABORTING, &priv->status);
	iwl_send_scan_abort(priv);

	mutex_unlock(&priv->mutex);
}

void iwl_setup_scan_deferred_work(struct iwl_priv *priv)
{
	/*  FIXME: move here when resolved PENDING
	 *  INIT_WORK(&priv->scan_completed, iwl_bg_scan_completed); */
	INIT_WORK(&priv->request_scan, iwl_bg_request_scan);
	INIT_WORK(&priv->abort_scan, iwl_bg_abort_scan);
	INIT_DELAYED_WORK(&priv->scan_check, iwl_bg_scan_check);
}
EXPORT_SYMBOL(iwl_setup_scan_deferred_work);

