/*
 * hostapd / RADIUS Accounting
 * Copyright (c) 2002-2009, 2012-2015, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "utils/includes.h"

#include "utils/common.h"
#include "utils/eloop.h"
#include "eapol_auth/eapol_auth_sm.h"
#include "eapol_auth/eapol_auth_sm_i.h"
#include "radius/radius.h"
#include "radius/radius_client.h"
#include "hostapd.h"
#include "ieee802_1x.h"
#include "ap_config.h"
#include "sta_info.h"
#include "ap_drv_ops.h"
#include "accounting.h"


/* Default interval in seconds for polling TX/RX octets from the driver if
 * STA is not using interim accounting. This detects wrap arounds for
 * input/output octets and updates Acct-{Input,Output}-Gigawords. */
#define ACCT_DEFAULT_UPDATE_INTERVAL 300

static void accounting_sta_interim(struct hostapd_data *hapd,
				   struct sta_info *sta);


static struct radius_msg * accounting_msg(struct hostapd_data *hapd,
					  struct sta_info *sta,
					  int status_type)
{
	struct radius_msg *msg;
	char buf[128];
	u8 *val;
	size_t len;
	int i;
	struct wpabuf *b;
	struct os_time now;

	msg = radius_msg_new(RADIUS_CODE_ACCOUNTING_REQUEST,
			     radius_client_get_id(hapd->radius));
	if (msg == NULL) {
		wpa_printf(MSG_INFO, "Could not create new RADIUS packet");
		return NULL;
	}

	if (!radius_msg_add_attr_int32(msg, RADIUS_ATTR_ACCT_STATUS_TYPE,
				       status_type)) {
		wpa_printf(MSG_INFO, "Could not add Acct-Status-Type");
		goto fail;
	}

	if (sta) {
		if (!hostapd_config_get_radius_attr(
			    hapd->conf->radius_acct_req_attr,
			    RADIUS_ATTR_ACCT_AUTHENTIC) &&
		    !radius_msg_add_attr_int32(msg, RADIUS_ATTR_ACCT_AUTHENTIC,
					       hapd->conf->ieee802_1x ?
					       RADIUS_ACCT_AUTHENTIC_RADIUS :
					       RADIUS_ACCT_AUTHENTIC_LOCAL)) {
			wpa_printf(MSG_INFO, "Could not add Acct-Authentic");
			goto fail;
		}

		/* Use 802.1X identity if available */
		val = ieee802_1x_get_identity(sta->eapol_sm, &len);

		/* Use RADIUS ACL identity if 802.1X provides no identity */
		if (!val && sta->identity) {
			val = (u8 *) sta->identity;
			len = os_strlen(sta->identity);
		}

		/* Use STA MAC if neither 802.1X nor RADIUS ACL provided
		 * identity */
		if (!val) {
			os_snprintf(buf, sizeof(buf), RADIUS_ADDR_FORMAT,
				    MAC2STR(sta->addr));
			val = (u8 *) buf;
			len = os_strlen(buf);
		}

		if (!radius_msg_add_attr(msg, RADIUS_ATTR_USER_NAME, val,
					 len)) {
			wpa_printf(MSG_INFO, "Could not add User-Name");
			goto fail;
		}
	}

	if (add_common_radius_attr(hapd, hapd->conf->radius_acct_req_attr, sta,
				   msg) < 0)
		goto fail;

	if (sta) {
		for (i = 0; ; i++) {
			val = ieee802_1x_get_radius_class(sta->eapol_sm, &len,
							  i);
			if (val == NULL)
				break;

			if (!radius_msg_add_attr(msg, RADIUS_ATTR_CLASS,
						 val, len)) {
				wpa_printf(MSG_INFO, "Could not add Class");
				goto fail;
			}
		}

		b = ieee802_1x_get_radius_cui(sta->eapol_sm);
		if (b &&
		    !radius_msg_add_attr(msg,
					 RADIUS_ATTR_CHARGEABLE_USER_IDENTITY,
					 wpabuf_head(b), wpabuf_len(b))) {
			wpa_printf(MSG_ERROR, "Could not add CUI");
			goto fail;
		}

		if (!b && sta->radius_cui &&
		    !radius_msg_add_attr(msg,
					 RADIUS_ATTR_CHARGEABLE_USER_IDENTITY,
					 (u8 *) sta->radius_cui,
					 os_strlen(sta->radius_cui))) {
			wpa_printf(MSG_ERROR, "Could not add CUI from ACL");
			goto fail;
		}

		if (sta->ipaddr &&
		    !radius_msg_add_attr_int32(msg,
					       RADIUS_ATTR_FRAMED_IP_ADDRESS,
					       be_to_host32(sta->ipaddr))) {
			wpa_printf(MSG_ERROR,
				   "Could not add Framed-IP-Address");
			goto fail;
		}
	}

	os_get_time(&now);
	if (now.sec > 1000000000 &&
	    !radius_msg_add_attr_int32(msg, RADIUS_ATTR_EVENT_TIMESTAMP,
				       now.sec)) {
		wpa_printf(MSG_INFO, "Could not add Event-Timestamp");
		goto fail;
	}

	/*
	 * Add Acct-Delay-Time with zero value for the first transmission. This
	 * will be updated within radius_client.c when retransmitting the frame.
	 */
	if (!radius_msg_add_attr_int32(msg, RADIUS_ATTR_ACCT_DELAY_TIME, 0)) {
		wpa_printf(MSG_INFO, "Could not add Acct-Delay-Time");
		goto fail;
	}

	return msg;

 fail:
	radius_msg_free(msg);
	return NULL;
}


static int accounting_sta_update_stats(struct hostapd_data *hapd,
				       struct sta_info *sta,
				       struct hostap_sta_driver_data *data)
{
	if (hostapd_drv_read_sta_data(hapd, data, sta->addr))
		return -1;

	if (!data->bytes_64bit) {
		/* Extend 32-bit counters from the driver to 64-bit counters */
		if (sta->last_rx_bytes_lo > data->rx_bytes)
			sta->last_rx_bytes_hi++;
		sta->last_rx_bytes_lo = data->rx_bytes;

		if (sta->last_tx_bytes_lo > data->tx_bytes)
			sta->last_tx_bytes_hi++;
		sta->last_tx_bytes_lo = data->tx_bytes;
	}

	hostapd_logger(hapd, sta->addr, HOSTAPD_MODULE_RADIUS,
		       HOSTAPD_LEVEL_DEBUG,
		       "updated TX/RX stats: rx_bytes=%llu [%u:%u] tx_bytes=%llu [%u:%u] bytes_64bit=%d",
		       data->rx_bytes, sta->last_rx_bytes_hi,
		       sta->last_rx_bytes_lo,
		       data->tx_bytes, sta->last_tx_bytes_hi,
		       sta->last_tx_bytes_lo,
		       data->bytes_64bit);

	return 0;
}


static void accounting_interim_update(void *eloop_ctx, void *timeout_ctx)
{
	struct hostapd_data *hapd = eloop_ctx;
	struct sta_info *sta = timeout_ctx;
	int interval;

	if (sta->acct_interim_interval) {
		accounting_sta_interim(hapd, sta);
		interval = sta->acct_interim_interval;
	} else {
		struct hostap_sta_driver_data data;
		accounting_sta_update_stats(hapd, sta, &data);
		interval = ACCT_DEFAULT_UPDATE_INTERVAL;
	}

	eloop_register_timeout(interval, 0, accounting_interim_update,
			       hapd, sta);
}


/**
 * accounting_sta_start - Start STA accounting
 * @hapd: hostapd BSS data
 * @sta: The station
 */
void accounting_sta_start(struct hostapd_data *hapd, struct sta_info *sta)
{
	struct radius_msg *msg;
	int interval;

	if (sta->acct_session_started)
		return;

	hostapd_logger(hapd, sta->addr, HOSTAPD_MODULE_RADIUS,
		       HOSTAPD_LEVEL_INFO,
		       "starting accounting session %016llX",
		       (unsigned long long) sta->acct_session_id);

	os_get_reltime(&sta->acct_session_start);
	sta->last_rx_bytes_hi = 0;
	sta->last_rx_bytes_lo = 0;
	sta->last_tx_bytes_hi = 0;
	sta->last_tx_bytes_lo = 0;
	hostapd_drv_sta_clear_stats(hapd, sta->addr);

	if (!hapd->conf->radius->acct_server)
		return;

	if (sta->acct_interim_interval)
		interval = sta->acct_interim_interval;
	else
		interval = ACCT_DEFAULT_UPDATE_INTERVAL;
	eloop_register_timeout(interval, 0, accounting_interim_update,
			       hapd, sta);

	msg = accounting_msg(hapd, sta, RADIUS_ACCT_STATUS_TYPE_START);
	if (msg &&
	    radius_client_send(hapd->radius, msg, RADIUS_ACCT, sta->addr) < 0)
		radius_msg_free(msg);

	sta->acct_session_started = 1;
}


static void accounting_sta_report(struct hostapd_data *hapd,
				  struct sta_info *sta, int stop)
{
	struct radius_msg *msg;
	int cause = sta->acct_terminate_cause;
	struct hostap_sta_driver_data data;
	struct os_reltime now_r, diff;
	u64 bytes;

	if (!hapd->conf->radius->acct_server)
		return;

	msg = accounting_msg(hapd, sta,
			     stop ? RADIUS_ACCT_STATUS_TYPE_STOP :
			     RADIUS_ACCT_STATUS_TYPE_INTERIM_UPDATE);
	if (!msg) {
		wpa_printf(MSG_INFO, "Could not create RADIUS Accounting message");
		return;
	}

	os_get_reltime(&now_r);
	os_reltime_sub(&now_r, &sta->acct_session_start, &diff);
	if (!radius_msg_add_attr_int32(msg, RADIUS_ATTR_ACCT_SESSION_TIME,
				       diff.sec)) {
		wpa_printf(MSG_INFO, "Could not add Acct-Session-Time");
		goto fail;
	}

	if (accounting_sta_update_stats(hapd, sta, &data) == 0) {
		if (!radius_msg_add_attr_int32(msg,
					       RADIUS_ATTR_ACCT_INPUT_PACKETS,
					       data.rx_packets)) {
			wpa_printf(MSG_INFO, "Could not add Acct-Input-Packets");
			goto fail;
		}
		if (!radius_msg_add_attr_int32(msg,
					       RADIUS_ATTR_ACCT_OUTPUT_PACKETS,
					       data.tx_packets)) {
			wpa_printf(MSG_INFO, "Could not add Acct-Output-Packets");
			goto fail;
		}
		if (data.bytes_64bit)
			bytes = data.rx_bytes;
		else
			bytes = ((u64) sta->last_rx_bytes_hi << 32) |
				sta->last_rx_bytes_lo;
		if (!radius_msg_add_attr_int32(msg,
					       RADIUS_ATTR_ACCT_INPUT_OCTETS,
					       (u32) bytes)) {
			wpa_printf(MSG_INFO, "Could not add Acct-Input-Octets");
			goto fail;
		}
		if (!radius_msg_add_attr_int32(msg,
					       RADIUS_ATTR_ACCT_INPUT_GIGAWORDS,
					       (u32) (bytes >> 32))) {
			wpa_printf(MSG_INFO, "Could not add Acct-Input-Gigawords");
			goto fail;
		}
		if (data.bytes_64bit)
			bytes = data.tx_bytes;
		else
			bytes = ((u64) sta->last_tx_bytes_hi << 32) |
				sta->last_tx_bytes_lo;
		if (!radius_msg_add_attr_int32(msg,
					       RADIUS_ATTR_ACCT_OUTPUT_OCTETS,
					       (u32) bytes)) {
			wpa_printf(MSG_INFO, "Could not add Acct-Output-Octets");
			goto fail;
		}
		if (!radius_msg_add_attr_int32(msg,
					       RADIUS_ATTR_ACCT_OUTPUT_GIGAWORDS,
					       (u32) (bytes >> 32))) {
			wpa_printf(MSG_INFO, "Could not add Acct-Output-Gigawords");
			goto fail;
		}
	}

	if (eloop_terminated())
		cause = RADIUS_ACCT_TERMINATE_CAUSE_ADMIN_REBOOT;

	if (stop && cause &&
	    !radius_msg_add_attr_int32(msg, RADIUS_ATTR_ACCT_TERMINATE_CAUSE,
				       cause)) {
		wpa_printf(MSG_INFO, "Could not add Acct-Terminate-Cause");
		goto fail;
	}

	if (radius_client_send(hapd->radius, msg,
			       stop ? RADIUS_ACCT : RADIUS_ACCT_INTERIM,
			       sta->addr) < 0)
		goto fail;
	return;

 fail:
	radius_msg_free(msg);
}


/**
 * accounting_sta_interim - Send a interim STA accounting report
 * @hapd: hostapd BSS data
 * @sta: The station
 */
static void accounting_sta_interim(struct hostapd_data *hapd,
				   struct sta_info *sta)
{
	if (sta->acct_session_started)
		accounting_sta_report(hapd, sta, 0);
}


/**
 * accounting_sta_stop - Stop STA accounting
 * @hapd: hostapd BSS data
 * @sta: The station
 */
void accounting_sta_stop(struct hostapd_data *hapd, struct sta_info *sta)
{
	if (sta->acct_session_started) {
		accounting_sta_report(hapd, sta, 1);
		eloop_cancel_timeout(accounting_interim_update, hapd, sta);
		hostapd_logger(hapd, sta->addr, HOSTAPD_MODULE_RADIUS,
			       HOSTAPD_LEVEL_INFO,
			       "stopped accounting session %016llX",
			       (unsigned long long) sta->acct_session_id);
		sta->acct_session_started = 0;
	}
}


int accounting_sta_get_id(struct hostapd_data *hapd, struct sta_info *sta)
{
	return radius_gen_session_id((u8 *) &sta->acct_session_id,
				     sizeof(sta->acct_session_id));
}


/**
 * accounting_receive - Process the RADIUS frames from Accounting Server
 * @msg: RADIUS response message
 * @req: RADIUS request message
 * @shared_secret: RADIUS shared secret
 * @shared_secret_len: Length of shared_secret in octets
 * @data: Context data (struct hostapd_data *)
 * Returns: Processing status
 */
static RadiusRxResult
accounting_receive(struct radius_msg *msg, struct radius_msg *req,
		   const u8 *shared_secret, size_t shared_secret_len,
		   void *data)
{
	if (radius_msg_get_hdr(msg)->code != RADIUS_CODE_ACCOUNTING_RESPONSE) {
		wpa_printf(MSG_INFO, "Unknown RADIUS message code");
		return RADIUS_RX_UNKNOWN;
	}

	if (radius_msg_verify(msg, shared_secret, shared_secret_len, req, 0)) {
		wpa_printf(MSG_INFO, "Incoming RADIUS packet did not have correct Authenticator - dropped");
		return RADIUS_RX_INVALID_AUTHENTICATOR;
	}

	return RADIUS_RX_PROCESSED;
}


static void accounting_report_state(struct hostapd_data *hapd, int on)
{
	struct radius_msg *msg;

	if (!hapd->conf->radius->acct_server || hapd->radius == NULL)
		return;

	/* Inform RADIUS server that accounting will start/stop so that the
	 * server can close old accounting sessions. */
	msg = accounting_msg(hapd, NULL,
			     on ? RADIUS_ACCT_STATUS_TYPE_ACCOUNTING_ON :
			     RADIUS_ACCT_STATUS_TYPE_ACCOUNTING_OFF);
	if (!msg)
		return;

	if (hapd->acct_session_id) {
		char buf[20];

		os_snprintf(buf, sizeof(buf), "%016llX",
			    (unsigned long long) hapd->acct_session_id);
		if (!radius_msg_add_attr(msg, RADIUS_ATTR_ACCT_SESSION_ID,
					 (u8 *) buf, os_strlen(buf)))
			wpa_printf(MSG_ERROR, "Could not add Acct-Session-Id");
	}

	if (radius_client_send(hapd->radius, msg, RADIUS_ACCT, NULL) < 0)
		radius_msg_free(msg);
}


static void accounting_interim_error_cb(const u8 *addr, void *ctx)
{
	struct hostapd_data *hapd = ctx;
	struct sta_info *sta;
	unsigned int i, wait_time;
	int res;

	sta = ap_get_sta(hapd, addr);
	if (!sta)
		return;
	sta->acct_interim_errors++;
	if (sta->acct_interim_errors > 10 /* RADIUS_CLIENT_MAX_RETRIES */) {
		wpa_printf(MSG_DEBUG,
			   "Interim RADIUS accounting update failed for " MACSTR
			   " - too many errors, abandon this interim accounting update",
			   MAC2STR(addr));
		sta->acct_interim_errors = 0;
		/* Next update will be tried after normal update interval */
		return;
	}

	/*
	 * Use a shorter update interval as an improved retransmission mechanism
	 * for failed interim accounting updates. This allows the statistics to
	 * be updated for each retransmission.
	 *
	 * RADIUS client code has already waited RADIUS_CLIENT_FIRST_WAIT.
	 * Schedule the first retry attempt immediately and every following one
	 * with exponential backoff.
	 */
	if (sta->acct_interim_errors == 1) {
		wait_time = 0;
	} else {
		wait_time = 3; /* RADIUS_CLIENT_FIRST_WAIT */
		for (i = 1; i < sta->acct_interim_errors; i++)
			wait_time *= 2;
	}
	res = eloop_deplete_timeout(wait_time, 0, accounting_interim_update,
				    hapd, sta);
	if (res == 1)
		wpa_printf(MSG_DEBUG,
			   "Interim RADIUS accounting update failed for " MACSTR
			   " (error count: %u) - schedule next update in %u seconds",
			   MAC2STR(addr), sta->acct_interim_errors, wait_time);
	else if (res == 0)
		wpa_printf(MSG_DEBUG,
			   "Interim RADIUS accounting update failed for " MACSTR
			   " (error count: %u)", MAC2STR(addr),
			   sta->acct_interim_errors);
	else
		wpa_printf(MSG_DEBUG,
			   "Interim RADIUS accounting update failed for " MACSTR
			   " (error count: %u) - no timer found", MAC2STR(addr),
			   sta->acct_interim_errors);
}


/**
 * accounting_init: Initialize accounting
 * @hapd: hostapd BSS data
 * Returns: 0 on success, -1 on failure
 */
int accounting_init(struct hostapd_data *hapd)
{
	if (radius_gen_session_id((u8 *) &hapd->acct_session_id,
				  sizeof(hapd->acct_session_id)) < 0)
		return -1;

	if (radius_client_register(hapd->radius, RADIUS_ACCT,
				   accounting_receive, hapd))
		return -1;
	radius_client_set_interim_error_cb(hapd->radius,
					   accounting_interim_error_cb, hapd);

	accounting_report_state(hapd, 1);

	return 0;
}


/**
 * accounting_deinit: Deinitialize accounting
 * @hapd: hostapd BSS data
 */
void accounting_deinit(struct hostapd_data *hapd)
{
	accounting_report_state(hapd, 0);
}
