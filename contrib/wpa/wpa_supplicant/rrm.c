/*
 * wpa_supplicant - Radio Measurements
 * Copyright (c) 2003-2016, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"

#include "utils/common.h"
#include "utils/eloop.h"
#include "common/ieee802_11_common.h"
#include "wpa_supplicant_i.h"
#include "driver_i.h"
#include "bss.h"
#include "scan.h"
#include "p2p_supplicant.h"


static void wpas_rrm_neighbor_rep_timeout_handler(void *data, void *user_ctx)
{
	struct rrm_data *rrm = data;

	if (!rrm->notify_neighbor_rep) {
		wpa_printf(MSG_ERROR,
			   "RRM: Unexpected neighbor report timeout");
		return;
	}

	wpa_printf(MSG_DEBUG, "RRM: Notifying neighbor report - NONE");
	rrm->notify_neighbor_rep(rrm->neighbor_rep_cb_ctx, NULL);

	rrm->notify_neighbor_rep = NULL;
	rrm->neighbor_rep_cb_ctx = NULL;
}


/*
 * wpas_rrm_reset - Clear and reset all RRM data in wpa_supplicant
 * @wpa_s: Pointer to wpa_supplicant
 */
void wpas_rrm_reset(struct wpa_supplicant *wpa_s)
{
	wpa_s->rrm.rrm_used = 0;

	eloop_cancel_timeout(wpas_rrm_neighbor_rep_timeout_handler, &wpa_s->rrm,
			     NULL);
	if (wpa_s->rrm.notify_neighbor_rep)
		wpas_rrm_neighbor_rep_timeout_handler(&wpa_s->rrm, NULL);
	wpa_s->rrm.next_neighbor_rep_token = 1;
	wpas_clear_beacon_rep_data(wpa_s);
}


/*
 * wpas_rrm_process_neighbor_rep - Handle incoming neighbor report
 * @wpa_s: Pointer to wpa_supplicant
 * @report: Neighbor report buffer, prefixed by a 1-byte dialog token
 * @report_len: Length of neighbor report buffer
 */
void wpas_rrm_process_neighbor_rep(struct wpa_supplicant *wpa_s,
				   const u8 *report, size_t report_len)
{
	struct wpabuf *neighbor_rep;

	wpa_hexdump(MSG_DEBUG, "RRM: New Neighbor Report", report, report_len);
	if (report_len < 1)
		return;

	if (report[0] != wpa_s->rrm.next_neighbor_rep_token - 1) {
		wpa_printf(MSG_DEBUG,
			   "RRM: Discarding neighbor report with token %d (expected %d)",
			   report[0], wpa_s->rrm.next_neighbor_rep_token - 1);
		return;
	}

	eloop_cancel_timeout(wpas_rrm_neighbor_rep_timeout_handler, &wpa_s->rrm,
			     NULL);

	if (!wpa_s->rrm.notify_neighbor_rep) {
		wpa_printf(MSG_ERROR, "RRM: Unexpected neighbor report");
		return;
	}

	/* skipping the first byte, which is only an id (dialog token) */
	neighbor_rep = wpabuf_alloc(report_len - 1);
	if (!neighbor_rep) {
		wpas_rrm_neighbor_rep_timeout_handler(&wpa_s->rrm, NULL);
		return;
	}
	wpabuf_put_data(neighbor_rep, report + 1, report_len - 1);
	wpa_printf(MSG_DEBUG, "RRM: Notifying neighbor report (token = %d)",
		   report[0]);
	wpa_s->rrm.notify_neighbor_rep(wpa_s->rrm.neighbor_rep_cb_ctx,
				       neighbor_rep);
	wpa_s->rrm.notify_neighbor_rep = NULL;
	wpa_s->rrm.neighbor_rep_cb_ctx = NULL;
}


#if defined(__CYGWIN__) || defined(CONFIG_NATIVE_WINDOWS)
/* Workaround different, undefined for Windows, error codes used here */
#define ENOTCONN -1
#define EOPNOTSUPP -1
#define ECANCELED -1
#endif

/* Measurement Request element + Location Subject + Maximum Age subelement */
#define MEASURE_REQUEST_LCI_LEN (3 + 1 + 4)
/* Measurement Request element + Location Civic Request */
#define MEASURE_REQUEST_CIVIC_LEN (3 + 5)


/**
 * wpas_rrm_send_neighbor_rep_request - Request a neighbor report from our AP
 * @wpa_s: Pointer to wpa_supplicant
 * @ssid: if not null, this is sent in the request. Otherwise, no SSID IE
 *	  is sent in the request.
 * @lci: if set, neighbor request will include LCI request
 * @civic: if set, neighbor request will include civic location request
 * @cb: Callback function to be called once the requested report arrives, or
 *	timed out after RRM_NEIGHBOR_REPORT_TIMEOUT seconds.
 *	In the former case, 'neighbor_rep' is a newly allocated wpabuf, and it's
 *	the requester's responsibility to free it.
 *	In the latter case NULL will be sent in 'neighbor_rep'.
 * @cb_ctx: Context value to send the callback function
 * Returns: 0 in case of success, negative error code otherwise
 *
 * In case there is a previous request which has not been answered yet, the
 * new request fails. The caller may retry after RRM_NEIGHBOR_REPORT_TIMEOUT.
 * Request must contain a callback function.
 */
int wpas_rrm_send_neighbor_rep_request(struct wpa_supplicant *wpa_s,
				       const struct wpa_ssid_value *ssid,
				       int lci, int civic,
				       void (*cb)(void *ctx,
						  struct wpabuf *neighbor_rep),
				       void *cb_ctx)
{
	struct wpabuf *buf;
	const u8 *rrm_ie;

	if (wpa_s->wpa_state != WPA_COMPLETED || wpa_s->current_ssid == NULL) {
		wpa_printf(MSG_DEBUG, "RRM: No connection, no RRM.");
		return -ENOTCONN;
	}

	if (!wpa_s->rrm.rrm_used) {
		wpa_printf(MSG_DEBUG, "RRM: No RRM in current connection.");
		return -EOPNOTSUPP;
	}

	rrm_ie = wpa_bss_get_ie(wpa_s->current_bss,
				WLAN_EID_RRM_ENABLED_CAPABILITIES);
	if (!rrm_ie || !(wpa_s->current_bss->caps & IEEE80211_CAP_RRM) ||
	    !(rrm_ie[2] & WLAN_RRM_CAPS_NEIGHBOR_REPORT)) {
		wpa_printf(MSG_DEBUG,
			   "RRM: No network support for Neighbor Report.");
		return -EOPNOTSUPP;
	}

	/* Refuse if there's a live request */
	if (wpa_s->rrm.notify_neighbor_rep) {
		wpa_printf(MSG_DEBUG,
			   "RRM: Currently handling previous Neighbor Report.");
		return -EBUSY;
	}

	/* 3 = action category + action code + dialog token */
	buf = wpabuf_alloc(3 + (ssid ? 2 + ssid->ssid_len : 0) +
			   (lci ? 2 + MEASURE_REQUEST_LCI_LEN : 0) +
			   (civic ? 2 + MEASURE_REQUEST_CIVIC_LEN : 0));
	if (buf == NULL) {
		wpa_printf(MSG_DEBUG,
			   "RRM: Failed to allocate Neighbor Report Request");
		return -ENOMEM;
	}

	wpa_printf(MSG_DEBUG, "RRM: Neighbor report request (for %s), token=%d",
		   (ssid ? wpa_ssid_txt(ssid->ssid, ssid->ssid_len) : ""),
		   wpa_s->rrm.next_neighbor_rep_token);

	wpabuf_put_u8(buf, WLAN_ACTION_RADIO_MEASUREMENT);
	wpabuf_put_u8(buf, WLAN_RRM_NEIGHBOR_REPORT_REQUEST);
	wpabuf_put_u8(buf, wpa_s->rrm.next_neighbor_rep_token);
	if (ssid) {
		wpabuf_put_u8(buf, WLAN_EID_SSID);
		wpabuf_put_u8(buf, ssid->ssid_len);
		wpabuf_put_data(buf, ssid->ssid, ssid->ssid_len);
	}

	if (lci) {
		/* IEEE P802.11-REVmc/D5.0 9.4.2.21 */
		wpabuf_put_u8(buf, WLAN_EID_MEASURE_REQUEST);
		wpabuf_put_u8(buf, MEASURE_REQUEST_LCI_LEN);

		/*
		 * Measurement token; nonzero number that is unique among the
		 * Measurement Request elements in a particular frame.
		 */
		wpabuf_put_u8(buf, 1); /* Measurement Token */

		/*
		 * Parallel, Enable, Request, and Report bits are 0, Duration is
		 * reserved.
		 */
		wpabuf_put_u8(buf, 0); /* Measurement Request Mode */
		wpabuf_put_u8(buf, MEASURE_TYPE_LCI); /* Measurement Type */

		/* IEEE P802.11-REVmc/D5.0 9.4.2.21.10 - LCI request */
		/* Location Subject */
		wpabuf_put_u8(buf, LOCATION_SUBJECT_REMOTE);

		/* Optional Subelements */
		/*
		 * IEEE P802.11-REVmc/D5.0 Figure 9-170
		 * The Maximum Age subelement is required, otherwise the AP can
		 * send only data that was determined after receiving the
		 * request. Setting it here to unlimited age.
		 */
		wpabuf_put_u8(buf, LCI_REQ_SUBELEM_MAX_AGE);
		wpabuf_put_u8(buf, 2);
		wpabuf_put_le16(buf, 0xffff);
	}

	if (civic) {
		/* IEEE P802.11-REVmc/D5.0 9.4.2.21 */
		wpabuf_put_u8(buf, WLAN_EID_MEASURE_REQUEST);
		wpabuf_put_u8(buf, MEASURE_REQUEST_CIVIC_LEN);

		/*
		 * Measurement token; nonzero number that is unique among the
		 * Measurement Request elements in a particular frame.
		 */
		wpabuf_put_u8(buf, 2); /* Measurement Token */

		/*
		 * Parallel, Enable, Request, and Report bits are 0, Duration is
		 * reserved.
		 */
		wpabuf_put_u8(buf, 0); /* Measurement Request Mode */
		/* Measurement Type */
		wpabuf_put_u8(buf, MEASURE_TYPE_LOCATION_CIVIC);

		/* IEEE P802.11-REVmc/D5.0 9.4.2.21.14:
		 * Location Civic request */
		/* Location Subject */
		wpabuf_put_u8(buf, LOCATION_SUBJECT_REMOTE);
		wpabuf_put_u8(buf, 0); /* Civic Location Type: IETF RFC 4776 */
		/* Location Service Interval Units: Seconds */
		wpabuf_put_u8(buf, 0);
		/* Location Service Interval: 0 - Only one report is requested
		 */
		wpabuf_put_le16(buf, 0);
		/* No optional subelements */
	}

	wpa_s->rrm.next_neighbor_rep_token++;

	if (wpa_drv_send_action(wpa_s, wpa_s->assoc_freq, 0, wpa_s->bssid,
				wpa_s->own_addr, wpa_s->bssid,
				wpabuf_head(buf), wpabuf_len(buf), 0) < 0) {
		wpa_printf(MSG_DEBUG,
			   "RRM: Failed to send Neighbor Report Request");
		wpabuf_free(buf);
		return -ECANCELED;
	}

	wpa_s->rrm.neighbor_rep_cb_ctx = cb_ctx;
	wpa_s->rrm.notify_neighbor_rep = cb;
	eloop_register_timeout(RRM_NEIGHBOR_REPORT_TIMEOUT, 0,
			       wpas_rrm_neighbor_rep_timeout_handler,
			       &wpa_s->rrm, NULL);

	wpabuf_free(buf);
	return 0;
}


static int wpas_rrm_report_elem(struct wpabuf **buf, u8 token, u8 mode, u8 type,
				const u8 *data, size_t data_len)
{
	if (wpabuf_resize(buf, 5 + data_len))
		return -1;

	wpabuf_put_u8(*buf, WLAN_EID_MEASURE_REPORT);
	wpabuf_put_u8(*buf, 3 + data_len);
	wpabuf_put_u8(*buf, token);
	wpabuf_put_u8(*buf, mode);
	wpabuf_put_u8(*buf, type);

	if (data_len)
		wpabuf_put_data(*buf, data, data_len);

	return 0;
}


static int
wpas_rrm_build_lci_report(struct wpa_supplicant *wpa_s,
			  const struct rrm_measurement_request_element *req,
			  struct wpabuf **buf)
{
	u8 subject;
	u16 max_age = 0;
	struct os_reltime t, diff;
	unsigned long diff_l;
	const u8 *subelem;
	const u8 *request = req->variable;
	size_t len = req->len - 3;

	if (len < 1)
		return -1;

	if (!wpa_s->lci)
		goto reject;

	subject = *request++;
	len--;

	wpa_printf(MSG_DEBUG, "Measurement request location subject=%u",
		   subject);

	if (subject != LOCATION_SUBJECT_REMOTE) {
		wpa_printf(MSG_INFO,
			   "Not building LCI report - bad location subject");
		return 0;
	}

	/* Subelements are formatted exactly like elements */
	wpa_hexdump(MSG_DEBUG, "LCI request subelements", request, len);
	subelem = get_ie(request, len, LCI_REQ_SUBELEM_MAX_AGE);
	if (subelem && subelem[1] == 2)
		max_age = WPA_GET_LE16(subelem + 2);

	if (os_get_reltime(&t))
		goto reject;

	os_reltime_sub(&t, &wpa_s->lci_time, &diff);
	/* LCI age is calculated in 10th of a second units. */
	diff_l = diff.sec * 10 + diff.usec / 100000;

	if (max_age != 0xffff && max_age < diff_l)
		goto reject;

	if (wpas_rrm_report_elem(buf, req->token,
				 MEASUREMENT_REPORT_MODE_ACCEPT, req->type,
				 wpabuf_head_u8(wpa_s->lci),
				 wpabuf_len(wpa_s->lci)) < 0) {
		wpa_printf(MSG_DEBUG, "Failed to add LCI report element");
		return -1;
	}

	return 0;

reject:
	if (!is_multicast_ether_addr(wpa_s->rrm.dst_addr) &&
	    wpas_rrm_report_elem(buf, req->token,
				 MEASUREMENT_REPORT_MODE_REJECT_INCAPABLE,
				 req->type, NULL, 0) < 0) {
		wpa_printf(MSG_DEBUG, "RRM: Failed to add report element");
		return -1;
	}

	return 0;
}


static void wpas_rrm_send_msr_report_mpdu(struct wpa_supplicant *wpa_s,
					  const u8 *data, size_t len)
{
	struct wpabuf *report = wpabuf_alloc(len + 3);

	if (!report)
		return;

	wpabuf_put_u8(report, WLAN_ACTION_RADIO_MEASUREMENT);
	wpabuf_put_u8(report, WLAN_RRM_RADIO_MEASUREMENT_REPORT);
	wpabuf_put_u8(report, wpa_s->rrm.token);

	wpabuf_put_data(report, data, len);

	if (wpa_drv_send_action(wpa_s, wpa_s->assoc_freq, 0, wpa_s->bssid,
				wpa_s->own_addr, wpa_s->bssid,
				wpabuf_head(report), wpabuf_len(report), 0)) {
		wpa_printf(MSG_ERROR,
			   "RRM: Radio measurement report failed: Sending Action frame failed");
	}

	wpabuf_free(report);
}


static void wpas_rrm_send_msr_report(struct wpa_supplicant *wpa_s,
				     struct wpabuf *buf)
{
	int len = wpabuf_len(buf);
	const u8 *pos = wpabuf_head_u8(buf), *next = pos;

#define MPDU_REPORT_LEN (int) (IEEE80211_MAX_MMPDU_SIZE - IEEE80211_HDRLEN - 3)

	while (len) {
		int send_len = (len > MPDU_REPORT_LEN) ? next - pos : len;

		if (send_len == len ||
		    (send_len + next[1] + 2) > MPDU_REPORT_LEN) {
			wpas_rrm_send_msr_report_mpdu(wpa_s, pos, send_len);
			len -= send_len;
			pos = next;
		}

		if (len)
			next += next[1] + 2;
	}
#undef MPDU_REPORT_LEN
}


static int wpas_add_channel(u8 op_class, u8 chan, u8 num_primary_channels,
			    int *freqs)
{
	size_t i;

	for (i = 0; i < num_primary_channels; i++) {
		u8 primary_chan = chan - (2 * num_primary_channels - 2) + i * 4;

		freqs[i] = ieee80211_chan_to_freq(NULL, op_class, primary_chan);
		/* ieee80211_chan_to_freq() is not really meant for this
		 * conversion of 20 MHz primary channel numbers for wider VHT
		 * channels, so handle those as special cases here for now. */
		if (freqs[i] < 0 &&
		    (op_class == 128 || op_class == 129 || op_class == 130))
			freqs[i] = 5000 + 5 * primary_chan;
		if (freqs[i] < 0) {
			wpa_printf(MSG_DEBUG,
				   "Beacon Report: Invalid channel %u",
				   chan);
			return -1;
		}
	}

	return 0;
}


static int * wpas_add_channels(const struct oper_class_map *op,
			       struct hostapd_hw_modes *mode, int active,
			       const u8 *channels, const u8 size)
{
	int *freqs, *next_freq;
	u8 num_primary_channels, i;
	u8 num_chans;

	num_chans = channels ? size :
		(op->max_chan - op->min_chan) / op->inc + 1;

	if (op->bw == BW80 || op->bw == BW80P80)
		num_primary_channels = 4;
	else if (op->bw == BW160)
		num_primary_channels = 8;
	else
		num_primary_channels = 1;

	/* one extra place for the zero-terminator */
	freqs = os_calloc(num_chans * num_primary_channels + 1, sizeof(*freqs));
	if (!freqs) {
		wpa_printf(MSG_ERROR,
			   "Beacon Report: Failed to allocate freqs array");
		return NULL;
	}

	next_freq = freqs;
	for  (i = 0; i < num_chans; i++) {
		u8 chan = channels ? channels[i] : op->min_chan + i * op->inc;
		enum chan_allowed res = verify_channel(mode, chan, op->bw);

		if (res == NOT_ALLOWED || (res == NO_IR && active))
			continue;

		if (wpas_add_channel(op->op_class, chan, num_primary_channels,
				     next_freq) < 0) {
			os_free(freqs);
			return NULL;
		}

		next_freq += num_primary_channels;
	}

	if (!freqs[0]) {
		os_free(freqs);
		return NULL;
	}

	return freqs;
}


static int * wpas_op_class_freqs(const struct oper_class_map *op,
				 struct hostapd_hw_modes *mode, int active)
{
	u8 channels_80mhz[] = { 42, 58, 106, 122, 138, 155 };
	u8 channels_160mhz[] = { 50, 114 };

	/*
	 * When adding all channels in the operating class, 80 + 80 MHz
	 * operating classes are like 80 MHz channels because we add all valid
	 * channels anyway.
	 */
	if (op->bw == BW80 || op->bw == BW80P80)
		return wpas_add_channels(op, mode, active, channels_80mhz,
					 ARRAY_SIZE(channels_80mhz));

	if (op->bw == BW160)
		return wpas_add_channels(op, mode, active, channels_160mhz,
					 ARRAY_SIZE(channels_160mhz));

	return wpas_add_channels(op, mode, active, NULL, 0);
}


static int * wpas_channel_report_freqs(struct wpa_supplicant *wpa_s, int active,
				       const char *country, const u8 *subelems,
				       size_t len)
{
	int *freqs = NULL, *new_freqs;
	const u8 *end = subelems + len;

	while (end - subelems > 2) {
		const struct oper_class_map *op;
		const u8 *ap_chan_elem, *pos;
		u8 left;
		struct hostapd_hw_modes *mode;

		ap_chan_elem = get_ie(subelems, end - subelems,
				      WLAN_BEACON_REQUEST_SUBELEM_AP_CHANNEL);
		if (!ap_chan_elem)
			break;
		pos = ap_chan_elem + 2;
		left = ap_chan_elem[1];
		if (left < 1)
			break;
		subelems = ap_chan_elem + 2 + left;

		op = get_oper_class(country, *pos);
		if (!op) {
			wpa_printf(MSG_DEBUG,
				   "Beacon request: unknown operating class in AP Channel Report subelement %u",
				   *pos);
			goto out;
		}
		pos++;
		left--;

		mode = get_mode(wpa_s->hw.modes, wpa_s->hw.num_modes, op->mode);
		if (!mode)
			continue;

		/*
		 * For 80 + 80 MHz operating classes, this AP Channel Report
		 * element should be followed by another element specifying
		 * the second 80 MHz channel. For now just add this 80 MHz
		 * channel, the second 80 MHz channel will be added when the
		 * next element is parsed.
		 * TODO: Verify that this AP Channel Report element is followed
		 * by a corresponding AP Channel Report element as specified in
		 * IEEE Std 802.11-2016, 11.11.9.1.
		 */
		new_freqs = wpas_add_channels(op, mode, active, pos, left);
		if (new_freqs)
			int_array_concat(&freqs, new_freqs);

		os_free(new_freqs);
	}

	return freqs;
out:
	os_free(freqs);
	return NULL;
}


static int * wpas_beacon_request_freqs(struct wpa_supplicant *wpa_s,
				       u8 op_class, u8 chan, int active,
				       const u8 *subelems, size_t len)
{
	int *freqs = NULL, *ext_freqs = NULL;
	struct hostapd_hw_modes *mode;
	const char *country = NULL;
	const struct oper_class_map *op;
	const u8 *elem;

	if (!wpa_s->current_bss)
		return NULL;
	elem = wpa_bss_get_ie(wpa_s->current_bss, WLAN_EID_COUNTRY);
	if (elem && elem[1] >= 2)
		country = (const char *) (elem + 2);

	op = get_oper_class(country, op_class);
	if (!op) {
		wpa_printf(MSG_DEBUG,
			   "Beacon request: invalid operating class %d",
			   op_class);
		return NULL;
	}

	mode = get_mode(wpa_s->hw.modes, wpa_s->hw.num_modes, op->mode);
	if (!mode)
		return NULL;

	switch (chan) {
	case 0:
		freqs = wpas_op_class_freqs(op, mode, active);
		if (!freqs)
			return NULL;
		break;
	case 255:
		/* freqs will be added from AP channel subelements */
		break;
	default:
		freqs = wpas_add_channels(op, mode, active, &chan, 1);
		if (!freqs)
			return NULL;
		break;
	}

	ext_freqs = wpas_channel_report_freqs(wpa_s, active, country, subelems,
					      len);
	if (ext_freqs) {
		int_array_concat(&freqs, ext_freqs);
		os_free(ext_freqs);
		int_array_sort_unique(freqs);
	}

	return freqs;
}


static int wpas_get_op_chan_phy(int freq, const u8 *ies, size_t ies_len,
				u8 *op_class, u8 *chan, u8 *phy_type)
{
	const u8 *ie;
	int sec_chan = 0, vht = 0;
	struct ieee80211_ht_operation *ht_oper = NULL;
	struct ieee80211_vht_operation *vht_oper = NULL;
	u8 seg0, seg1;

	ie = get_ie(ies, ies_len, WLAN_EID_HT_OPERATION);
	if (ie && ie[1] >= sizeof(struct ieee80211_ht_operation)) {
		u8 sec_chan_offset;

		ht_oper = (struct ieee80211_ht_operation *) (ie + 2);
		sec_chan_offset = ht_oper->ht_param &
			HT_INFO_HT_PARAM_SECONDARY_CHNL_OFF_MASK;
		if (sec_chan_offset == HT_INFO_HT_PARAM_SECONDARY_CHNL_ABOVE)
			sec_chan = 1;
		else if (sec_chan_offset ==
			 HT_INFO_HT_PARAM_SECONDARY_CHNL_BELOW)
			sec_chan = -1;
	}

	ie = get_ie(ies, ies_len, WLAN_EID_VHT_OPERATION);
	if (ie && ie[1] >= sizeof(struct ieee80211_vht_operation)) {
		vht_oper = (struct ieee80211_vht_operation *) (ie + 2);

		switch (vht_oper->vht_op_info_chwidth) {
		case 1:
			seg0 = vht_oper->vht_op_info_chan_center_freq_seg0_idx;
			seg1 = vht_oper->vht_op_info_chan_center_freq_seg1_idx;
			if (seg1 && abs(seg1 - seg0) == 8)
				vht = VHT_CHANWIDTH_160MHZ;
			else if (seg1)
				vht = VHT_CHANWIDTH_80P80MHZ;
			else
				vht = VHT_CHANWIDTH_80MHZ;
			break;
		case 2:
			vht = VHT_CHANWIDTH_160MHZ;
			break;
		case 3:
			vht = VHT_CHANWIDTH_80P80MHZ;
			break;
		default:
			vht = VHT_CHANWIDTH_USE_HT;
			break;
		}
	}

	if (ieee80211_freq_to_channel_ext(freq, sec_chan, vht, op_class,
					  chan) == NUM_HOSTAPD_MODES) {
		wpa_printf(MSG_DEBUG,
			   "Cannot determine operating class and channel");
		return -1;
	}

	*phy_type = ieee80211_get_phy_type(freq, ht_oper != NULL,
					   vht_oper != NULL);
	if (*phy_type == PHY_TYPE_UNSPECIFIED) {
		wpa_printf(MSG_DEBUG, "Cannot determine phy type");
		return -1;
	}

	return 0;
}


static int wpas_beacon_rep_add_frame_body(struct bitfield *eids,
					  enum beacon_report_detail detail,
					  struct wpa_bss *bss, u8 *buf,
					  size_t buf_len)
{
	u8 *ies = (u8 *) (bss + 1);
	size_t ies_len = bss->ie_len ? bss->ie_len : bss->beacon_ie_len;
	u8 *pos = buf;
	int rem_len;

	rem_len = 255 - sizeof(struct rrm_measurement_beacon_report) -
		sizeof(struct rrm_measurement_report_element) - 2;

	if (detail > BEACON_REPORT_DETAIL_ALL_FIELDS_AND_ELEMENTS) {
		wpa_printf(MSG_DEBUG,
			   "Beacon Request: Invalid reporting detail: %d",
			   detail);
		return -1;
	}

	if (detail == BEACON_REPORT_DETAIL_NONE)
		return 0;

	/*
	 * Minimal frame body subelement size: EID(1) + length(1) + TSF(8) +
	 * beacon interval(2) + capabilities(2) = 14 bytes
	 */
	if (buf_len < 14)
		return 0;

	*pos++ = WLAN_BEACON_REPORT_SUBELEM_FRAME_BODY;
	/* The length will be filled later */
	pos++;
	WPA_PUT_LE64(pos, bss->tsf);
	pos += sizeof(bss->tsf);
	WPA_PUT_LE16(pos, bss->beacon_int);
	pos += 2;
	WPA_PUT_LE16(pos, bss->caps);
	pos += 2;

	rem_len -= pos - buf;

	/*
	 * According to IEEE Std 802.11-2016, 9.4.2.22.7, if the reported frame
	 * body subelement causes the element to exceed the maximum element
	 * size, the subelement is truncated so that the last IE is a complete
	 * IE. So even when required to report all IEs, add elements one after
	 * the other and stop once there is no more room in the measurement
	 * element.
	 */
	while (ies_len > 2 && 2U + ies[1] <= ies_len && rem_len > 0) {
		if (detail == BEACON_REPORT_DETAIL_ALL_FIELDS_AND_ELEMENTS ||
		    (eids && bitfield_is_set(eids, ies[0]))) {
			u8 eid = ies[0], elen = ies[1];

			if ((eid == WLAN_EID_TIM || eid == WLAN_EID_RSN) &&
			    elen > 4)
				elen = 4;
			/*
			 * TODO: Truncate IBSS DFS element as described in
			 * IEEE Std 802.11-2016, 9.4.2.22.7.
			 */

			if (2 + elen > buf + buf_len - pos ||
			    2 + elen > rem_len)
				break;

			*pos++ = ies[0];
			*pos++ = elen;
			os_memcpy(pos, ies + 2, elen);
			pos += elen;
			rem_len -= 2 + elen;
		}

		ies_len -= 2 + ies[1];
		ies += 2 + ies[1];
	}

	/* Now the length is known */
	buf[1] = pos - buf - 2;
	return pos - buf;
}


static int wpas_add_beacon_rep(struct wpa_supplicant *wpa_s,
			       struct wpabuf **wpa_buf, struct wpa_bss *bss,
			       u64 start, u64 parent_tsf)
{
	struct beacon_rep_data *data = &wpa_s->beacon_rep_data;
	u8 *ie = (u8 *) (bss + 1);
	size_t ie_len = bss->ie_len + bss->beacon_ie_len;
	int ret;
	u8 *buf;
	struct rrm_measurement_beacon_report *rep;

	if (os_memcmp(data->bssid, broadcast_ether_addr, ETH_ALEN) != 0 &&
	    os_memcmp(data->bssid, bss->bssid, ETH_ALEN) != 0)
		return 0;

	if (data->ssid_len &&
	    (data->ssid_len != bss->ssid_len ||
	     os_memcmp(data->ssid, bss->ssid, bss->ssid_len) != 0))
		return 0;

	/* Maximum element length: beacon report element + reported frame body
	 * subelement + all IEs of the reported beacon */
	buf = os_malloc(sizeof(*rep) + 14 + ie_len);
	if (!buf)
		return -1;

	rep = (struct rrm_measurement_beacon_report *) buf;
	if (wpas_get_op_chan_phy(bss->freq, ie, ie_len, &rep->op_class,
				 &rep->channel, &rep->report_info) < 0) {
		ret = 0;
		goto out;
	}

	rep->start_time = host_to_le64(start);
	rep->duration = host_to_le16(data->scan_params.duration);
	rep->rcpi = rssi_to_rcpi(bss->level);
	rep->rsni = 255; /* 255 indicates that RSNI is not available */
	os_memcpy(rep->bssid, bss->bssid, ETH_ALEN);
	rep->antenna_id = 0; /* unknown */
	rep->parent_tsf = host_to_le32(parent_tsf);

	ret = wpas_beacon_rep_add_frame_body(data->eids, data->report_detail,
					     bss, rep->variable, 14 + ie_len);
	if (ret < 0)
		goto out;

	ret = wpas_rrm_report_elem(wpa_buf, wpa_s->beacon_rep_data.token,
				   MEASUREMENT_REPORT_MODE_ACCEPT,
				   MEASURE_TYPE_BEACON, buf,
				   ret + sizeof(*rep));
out:
	os_free(buf);
	return ret;
}


static int wpas_beacon_rep_no_results(struct wpa_supplicant *wpa_s,
				      struct wpabuf **buf)
{
	return wpas_rrm_report_elem(buf, wpa_s->beacon_rep_data.token,
				    MEASUREMENT_REPORT_MODE_ACCEPT,
				    MEASURE_TYPE_BEACON, NULL, 0);
}


static void wpas_beacon_rep_table(struct wpa_supplicant *wpa_s,
				  struct wpabuf **buf)
{
	size_t i;

	for (i = 0; i < wpa_s->last_scan_res_used; i++) {
		if (wpas_add_beacon_rep(wpa_s, buf, wpa_s->last_scan_res[i],
					0, 0) < 0)
			break;
	}

	if (!(*buf))
		wpas_beacon_rep_no_results(wpa_s, buf);

	wpa_hexdump_buf(MSG_DEBUG, "RRM: Radio Measurement report", *buf);
}


void wpas_rrm_refuse_request(struct wpa_supplicant *wpa_s)
{
	if (!is_multicast_ether_addr(wpa_s->rrm.dst_addr)) {
		struct wpabuf *buf = NULL;

		if (wpas_rrm_report_elem(&buf, wpa_s->beacon_rep_data.token,
					 MEASUREMENT_REPORT_MODE_REJECT_REFUSED,
					 MEASURE_TYPE_BEACON, NULL, 0)) {
			wpa_printf(MSG_ERROR, "RRM: Memory allocation failed");
			wpabuf_free(buf);
			return;
		}

		wpas_rrm_send_msr_report(wpa_s, buf);
		wpabuf_free(buf);
	}

	wpas_clear_beacon_rep_data(wpa_s);
}


static void wpas_rrm_scan_timeout(void *eloop_ctx, void *timeout_ctx)
{
	struct wpa_supplicant *wpa_s = eloop_ctx;
	struct wpa_driver_scan_params *params =
		&wpa_s->beacon_rep_data.scan_params;
	u16 prev_duration = params->duration;

	if (!wpa_s->current_bss)
		return;

	if (!(wpa_s->drv_rrm_flags & WPA_DRIVER_FLAGS_SUPPORT_SET_SCAN_DWELL) &&
	    params->duration) {
		wpa_printf(MSG_DEBUG,
			   "RRM: Cannot set scan duration due to missing driver support");
		params->duration = 0;
	}
	os_get_reltime(&wpa_s->beacon_rep_scan);
	if (wpa_s->scanning || wpas_p2p_in_progress(wpa_s) ||
	    wpa_supplicant_trigger_scan(wpa_s, params))
		wpas_rrm_refuse_request(wpa_s);
	params->duration = prev_duration;
}


static int wpas_rm_handle_beacon_req_subelem(struct wpa_supplicant *wpa_s,
					     struct beacon_rep_data *data,
					     u8 sid, u8 slen, const u8 *subelem)
{
	u8 report_info, i;

	switch (sid) {
	case WLAN_BEACON_REQUEST_SUBELEM_SSID:
		if (!slen) {
			wpa_printf(MSG_DEBUG,
				   "SSID subelement with zero length - wildcard SSID");
			break;
		}

		if (slen > SSID_MAX_LEN) {
			wpa_printf(MSG_DEBUG,
				   "Invalid SSID subelement length: %u", slen);
			return -1;
		}

		data->ssid_len = slen;
		os_memcpy(data->ssid, subelem, data->ssid_len);
		break;
	case WLAN_BEACON_REQUEST_SUBELEM_INFO:
		if (slen != 2) {
			wpa_printf(MSG_DEBUG,
				   "Invalid reporting information subelement length: %u",
				   slen);
			return -1;
		}

		report_info = subelem[0];
		if (report_info != 0) {
			wpa_printf(MSG_DEBUG,
				   "reporting information=%u is not supported",
				   report_info);
			return 0;
		}
		break;
	case WLAN_BEACON_REQUEST_SUBELEM_DETAIL:
		if (slen != 1) {
			wpa_printf(MSG_DEBUG,
				   "Invalid reporting detail subelement length: %u",
				   slen);
			return -1;
		}

		data->report_detail = subelem[0];
		if (data->report_detail >
		    BEACON_REPORT_DETAIL_ALL_FIELDS_AND_ELEMENTS) {
			wpa_printf(MSG_DEBUG, "Invalid reporting detail: %u",
				   subelem[0]);
			return -1;
		}

		break;
	case WLAN_BEACON_REQUEST_SUBELEM_REQUEST:
		if (data->report_detail !=
		    BEACON_REPORT_DETAIL_REQUESTED_ONLY) {
			wpa_printf(MSG_DEBUG,
				   "Beacon request: request subelement is present but report detail is %u",
				   data->report_detail);
			return -1;
		}

		if (!slen) {
			wpa_printf(MSG_DEBUG,
				   "Invalid request subelement length: %u",
				   slen);
			return -1;
		}

		if (data->eids) {
			wpa_printf(MSG_DEBUG,
				   "Beacon Request: Request subelement appears more than once");
			return -1;
		}

		data->eids = bitfield_alloc(255);
		if (!data->eids) {
			wpa_printf(MSG_DEBUG, "Failed to allocate EIDs bitmap");
			return -1;
		}

		for (i = 0; i < slen; i++)
			bitfield_set(data->eids, subelem[i]);
		break;
	case WLAN_BEACON_REQUEST_SUBELEM_AP_CHANNEL:
		/* Skip - it will be processed when freqs are added */
		break;
	default:
		wpa_printf(MSG_DEBUG,
			   "Beacon request: Unknown subelement id %u", sid);
		break;
	}

	return 1;
}


/**
 * Returns 0 if the next element can be processed, 1 if some operation was
 * triggered, and -1 if processing failed (i.e., the element is in invalid
 * format or an internal error occurred).
 */
static int
wpas_rm_handle_beacon_req(struct wpa_supplicant *wpa_s,
			  u8 elem_token, int duration_mandatory,
			  const struct rrm_measurement_beacon_request *req,
			  size_t len, struct wpabuf **buf)
{
	struct beacon_rep_data *data = &wpa_s->beacon_rep_data;
	struct wpa_driver_scan_params *params = &data->scan_params;
	const u8 *subelems;
	size_t elems_len;
	u16 rand_interval;
	u32 interval_usec;
	u32 _rand;
	int ret = 0, res;
	u8 reject_mode;

	if (len < sizeof(*req))
		return -1;

	if (req->mode != BEACON_REPORT_MODE_PASSIVE &&
	    req->mode != BEACON_REPORT_MODE_ACTIVE &&
	    req->mode != BEACON_REPORT_MODE_TABLE)
		return 0;

	subelems = req->variable;
	elems_len = len - sizeof(*req);
	rand_interval = le_to_host16(req->rand_interval);

	os_free(params->freqs);
	os_memset(params, 0, sizeof(*params));

	data->token = elem_token;

	/* default reporting detail is all fixed length fields and all
	 * elements */
	data->report_detail = BEACON_REPORT_DETAIL_ALL_FIELDS_AND_ELEMENTS;
	os_memcpy(data->bssid, req->bssid, ETH_ALEN);

	while (elems_len >= 2) {
		if (subelems[1] > elems_len - 2) {
			wpa_printf(MSG_DEBUG,
				   "Beacon Request: Truncated subelement");
			ret = -1;
			goto out;
		}

		res = wpas_rm_handle_beacon_req_subelem(
			wpa_s, data, subelems[0], subelems[1], &subelems[2]);
		if (res < 0) {
			ret = res;
			goto out;
		} else if (!res) {
			reject_mode = MEASUREMENT_REPORT_MODE_REJECT_INCAPABLE;
			goto out_reject;
		}

		elems_len -= 2 + subelems[1];
		subelems += 2 + subelems[1];
	}

	if (req->mode == BEACON_REPORT_MODE_TABLE) {
		wpas_beacon_rep_table(wpa_s, buf);
		goto out;
	}

	params->freqs = wpas_beacon_request_freqs(
		wpa_s, req->oper_class, req->channel,
		req->mode == BEACON_REPORT_MODE_ACTIVE,
		req->variable, len - sizeof(*req));
	if (!params->freqs) {
		wpa_printf(MSG_DEBUG, "Beacon request: No valid channels");
		reject_mode = MEASUREMENT_REPORT_MODE_REJECT_REFUSED;
		goto out_reject;
	}

	params->duration = le_to_host16(req->duration);
	params->duration_mandatory = duration_mandatory;
	if (!params->duration) {
		wpa_printf(MSG_DEBUG, "Beacon request: Duration is 0");
		ret = -1;
		goto out;
	}

	params->only_new_results = 1;

	if (req->mode == BEACON_REPORT_MODE_ACTIVE) {
		params->ssids[params->num_ssids].ssid = data->ssid;
		params->ssids[params->num_ssids++].ssid_len = data->ssid_len;
	}

	if (os_get_random((u8 *) &_rand, sizeof(_rand)) < 0)
		_rand = os_random();
	interval_usec = (_rand % (rand_interval + 1)) * 1024;
	eloop_register_timeout(0, interval_usec, wpas_rrm_scan_timeout, wpa_s,
			       NULL);
	return 1;
out_reject:
	if (!is_multicast_ether_addr(wpa_s->rrm.dst_addr) &&
	    wpas_rrm_report_elem(buf, elem_token, reject_mode,
				 MEASURE_TYPE_BEACON, NULL, 0) < 0) {
		wpa_printf(MSG_DEBUG, "RRM: Failed to add report element");
		ret = -1;
	}
out:
	wpas_clear_beacon_rep_data(wpa_s);
	return ret;
}


static int
wpas_rrm_handle_msr_req_element(
	struct wpa_supplicant *wpa_s,
	const struct rrm_measurement_request_element *req,
	struct wpabuf **buf)
{
	int duration_mandatory;

	wpa_printf(MSG_DEBUG, "Measurement request type %d token %d",
		   req->type, req->token);

	if (req->mode & MEASUREMENT_REQUEST_MODE_ENABLE) {
		/* Enable bit is not supported for now */
		wpa_printf(MSG_DEBUG, "RRM: Enable bit not supported, ignore");
		return 0;
	}

	if ((req->mode & MEASUREMENT_REQUEST_MODE_PARALLEL) &&
	    req->type > MEASURE_TYPE_RPI_HIST) {
		/* Parallel measurements are not supported for now */
		wpa_printf(MSG_DEBUG,
			   "RRM: Parallel measurements are not supported, reject");
		goto reject;
	}

	duration_mandatory =
		!!(req->mode & MEASUREMENT_REQUEST_MODE_DURATION_MANDATORY);

	switch (req->type) {
	case MEASURE_TYPE_LCI:
		return wpas_rrm_build_lci_report(wpa_s, req, buf);
	case MEASURE_TYPE_BEACON:
		if (duration_mandatory &&
		    !(wpa_s->drv_rrm_flags &
		      WPA_DRIVER_FLAGS_SUPPORT_SET_SCAN_DWELL)) {
			wpa_printf(MSG_DEBUG,
				   "RRM: Driver does not support dwell time configuration - reject beacon report with mandatory duration");
			goto reject;
		}
		return wpas_rm_handle_beacon_req(wpa_s, req->token,
						 duration_mandatory,
						 (const void *) req->variable,
						 req->len - 3, buf);
	default:
		wpa_printf(MSG_INFO,
			   "RRM: Unsupported radio measurement type %u",
			   req->type);
		break;
	}

reject:
	if (!is_multicast_ether_addr(wpa_s->rrm.dst_addr) &&
	    wpas_rrm_report_elem(buf, req->token,
				 MEASUREMENT_REPORT_MODE_REJECT_INCAPABLE,
				 req->type, NULL, 0) < 0) {
		wpa_printf(MSG_DEBUG, "RRM: Failed to add report element");
		return -1;
	}

	return 0;
}


static struct wpabuf *
wpas_rrm_process_msr_req_elems(struct wpa_supplicant *wpa_s, const u8 *pos,
			       size_t len)
{
	struct wpabuf *buf = NULL;

	while (len) {
		const struct rrm_measurement_request_element *req;
		int res;

		if (len < 2) {
			wpa_printf(MSG_DEBUG, "RRM: Truncated element");
			goto out;
		}

		req = (const struct rrm_measurement_request_element *) pos;
		if (req->eid != WLAN_EID_MEASURE_REQUEST) {
			wpa_printf(MSG_DEBUG,
				   "RRM: Expected Measurement Request element, but EID is %u",
				   req->eid);
			goto out;
		}

		if (req->len < 3) {
			wpa_printf(MSG_DEBUG, "RRM: Element length too short");
			goto out;
		}

		if (req->len > len - 2) {
			wpa_printf(MSG_DEBUG, "RRM: Element length too long");
			goto out;
		}

		res = wpas_rrm_handle_msr_req_element(wpa_s, req, &buf);
		if (res < 0)
			goto out;

		pos += req->len + 2;
		len -= req->len + 2;
	}

	return buf;

out:
	wpabuf_free(buf);
	return NULL;
}


void wpas_rrm_handle_radio_measurement_request(struct wpa_supplicant *wpa_s,
					       const u8 *src, const u8 *dst,
					       const u8 *frame, size_t len)
{
	struct wpabuf *report;

	if (wpa_s->wpa_state != WPA_COMPLETED) {
		wpa_printf(MSG_INFO,
			   "RRM: Ignoring radio measurement request: Not associated");
		return;
	}

	if (!wpa_s->rrm.rrm_used) {
		wpa_printf(MSG_INFO,
			   "RRM: Ignoring radio measurement request: Not RRM network");
		return;
	}

	if (len < 3) {
		wpa_printf(MSG_INFO,
			   "RRM: Ignoring too short radio measurement request");
		return;
	}

	wpa_s->rrm.token = *frame;
	os_memcpy(wpa_s->rrm.dst_addr, dst, ETH_ALEN);

	/* Number of repetitions is not supported */

	report = wpas_rrm_process_msr_req_elems(wpa_s, frame + 3, len - 3);
	if (!report)
		return;

	wpas_rrm_send_msr_report(wpa_s, report);
	wpabuf_free(report);
}


void wpas_rrm_handle_link_measurement_request(struct wpa_supplicant *wpa_s,
					      const u8 *src,
					      const u8 *frame, size_t len,
					      int rssi)
{
	struct wpabuf *buf;
	const struct rrm_link_measurement_request *req;
	struct rrm_link_measurement_report report;

	if (wpa_s->wpa_state != WPA_COMPLETED) {
		wpa_printf(MSG_INFO,
			   "RRM: Ignoring link measurement request. Not associated");
		return;
	}

	if (!wpa_s->rrm.rrm_used) {
		wpa_printf(MSG_INFO,
			   "RRM: Ignoring link measurement request. Not RRM network");
		return;
	}

	if (!(wpa_s->drv_rrm_flags & WPA_DRIVER_FLAGS_TX_POWER_INSERTION)) {
		wpa_printf(MSG_INFO,
			   "RRM: Measurement report failed. TX power insertion not supported");
		return;
	}

	req = (const struct rrm_link_measurement_request *) frame;
	if (len < sizeof(*req)) {
		wpa_printf(MSG_INFO,
			   "RRM: Link measurement report failed. Request too short");
		return;
	}

	os_memset(&report, 0, sizeof(report));
	report.dialog_token = req->dialog_token;
	report.tpc.eid = WLAN_EID_TPC_REPORT;
	report.tpc.len = 2;
	/* Note: The driver is expected to update report.tpc.tx_power and
	 * report.tpc.link_margin subfields when sending out this frame.
	 * Similarly, the driver would need to update report.rx_ant_id and
	 * report.tx_ant_id subfields. */
	report.rsni = 255; /* 255 indicates that RSNI is not available */
	report.rcpi = rssi_to_rcpi(rssi);

	/* action_category + action_code */
	buf = wpabuf_alloc(2 + sizeof(report));
	if (buf == NULL) {
		wpa_printf(MSG_ERROR,
			   "RRM: Link measurement report failed. Buffer allocation failed");
		return;
	}

	wpabuf_put_u8(buf, WLAN_ACTION_RADIO_MEASUREMENT);
	wpabuf_put_u8(buf, WLAN_RRM_LINK_MEASUREMENT_REPORT);
	wpabuf_put_data(buf, &report, sizeof(report));
	wpa_hexdump_buf(MSG_DEBUG, "RRM: Link measurement report", buf);

	if (wpa_drv_send_action(wpa_s, wpa_s->assoc_freq, 0, src,
				wpa_s->own_addr, wpa_s->bssid,
				wpabuf_head(buf), wpabuf_len(buf), 0)) {
		wpa_printf(MSG_ERROR,
			   "RRM: Link measurement report failed. Send action failed");
	}
	wpabuf_free(buf);
}


int wpas_beacon_rep_scan_process(struct wpa_supplicant *wpa_s,
				 struct wpa_scan_results *scan_res,
				 struct scan_info *info)
{
	size_t i = 0;
	struct wpabuf *buf = NULL;

	if (!wpa_s->beacon_rep_data.token)
		return 0;

	if (!wpa_s->current_bss)
		goto out;

	/* If the measurement was aborted, don't report partial results */
	if (info->aborted)
		goto out;

	wpa_printf(MSG_DEBUG, "RRM: TSF BSSID: " MACSTR " current BSS: " MACSTR,
		   MAC2STR(info->scan_start_tsf_bssid),
		   MAC2STR(wpa_s->current_bss->bssid));
	if ((wpa_s->drv_rrm_flags & WPA_DRIVER_FLAGS_SUPPORT_BEACON_REPORT) &&
	    os_memcmp(info->scan_start_tsf_bssid, wpa_s->current_bss->bssid,
		      ETH_ALEN) != 0) {
		wpa_printf(MSG_DEBUG,
			   "RRM: Ignore scan results due to mismatching TSF BSSID");
		goto out;
	}

	for (i = 0; i < scan_res->num; i++) {
		struct wpa_bss *bss =
			wpa_bss_get_bssid(wpa_s, scan_res->res[i]->bssid);

		if (!bss)
			continue;

		if ((wpa_s->drv_rrm_flags &
		     WPA_DRIVER_FLAGS_SUPPORT_BEACON_REPORT) &&
		    os_memcmp(scan_res->res[i]->tsf_bssid,
			      wpa_s->current_bss->bssid, ETH_ALEN) != 0) {
			wpa_printf(MSG_DEBUG,
				   "RRM: Ignore scan result for " MACSTR
				   " due to mismatching TSF BSSID" MACSTR,
				   MAC2STR(scan_res->res[i]->bssid),
				   MAC2STR(scan_res->res[i]->tsf_bssid));
			continue;
		}

		/*
		 * Don't report results that were not received during the
		 * current measurement.
		 */
		if (!(wpa_s->drv_rrm_flags &
		      WPA_DRIVER_FLAGS_SUPPORT_BEACON_REPORT)) {
			struct os_reltime update_time, diff;

			/* For now, allow 8 ms older results due to some
			 * unknown issue with cfg80211 BSS table updates during
			 * a scan with the current BSS.
			 * TODO: Fix this more properly to avoid having to have
			 * this type of hacks in place. */
			calculate_update_time(&scan_res->fetch_time,
					      scan_res->res[i]->age,
					      &update_time);
			os_reltime_sub(&wpa_s->beacon_rep_scan,
				       &update_time, &diff);
			if (os_reltime_before(&update_time,
					      &wpa_s->beacon_rep_scan) &&
			    (diff.sec || diff.usec >= 8000)) {
				wpa_printf(MSG_DEBUG,
					   "RRM: Ignore scan result for " MACSTR
					   " due to old update (age(ms) %u, calculated age %u.%06u seconds)",
					   MAC2STR(scan_res->res[i]->bssid),
					   scan_res->res[i]->age,
					   (unsigned int) diff.sec,
					   (unsigned int) diff.usec);
				continue;
			}
		} else if (info->scan_start_tsf >
			   scan_res->res[i]->parent_tsf) {
			continue;
		}

		if (wpas_add_beacon_rep(wpa_s, &buf, bss, info->scan_start_tsf,
					scan_res->res[i]->parent_tsf) < 0)
			break;
	}

	if (!buf && wpas_beacon_rep_no_results(wpa_s, &buf))
		goto out;

	wpa_hexdump_buf(MSG_DEBUG, "RRM: Radio Measurement report", buf);

	wpas_rrm_send_msr_report(wpa_s, buf);
	wpabuf_free(buf);

out:
	wpas_clear_beacon_rep_data(wpa_s);
	return 1;
}


void wpas_clear_beacon_rep_data(struct wpa_supplicant *wpa_s)
{
	struct beacon_rep_data *data = &wpa_s->beacon_rep_data;

	eloop_cancel_timeout(wpas_rrm_scan_timeout, wpa_s, NULL);
	bitfield_free(data->eids);
	os_free(data->scan_params.freqs);
	os_memset(data, 0, sizeof(*data));
}
