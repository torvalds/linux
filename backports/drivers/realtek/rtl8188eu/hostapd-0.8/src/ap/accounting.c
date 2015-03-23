/*
 * hostapd / RADIUS Accounting
 * Copyright (c) 2002-2009, Jouni Malinen <j@w1.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Alternatively, this software may be distributed under the terms of BSD
 * license.
 *
 * See README and COPYING for more details.
 */

#include "utils/includes.h"

#include "utils/common.h"
#include "utils/eloop.h"
#include "drivers/driver.h"
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

static void accounting_sta_get_id(struct hostapd_data *hapd,
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

	msg = radius_msg_new(RADIUS_CODE_ACCOUNTING_REQUEST,
			     radius_client_get_id(hapd->radius));
	if (msg == NULL) {
		printf("Could not create net RADIUS packet\n");
		return NULL;
	}

	if (sta) {
		radius_msg_make_authenticator(msg, (u8 *) sta, sizeof(*sta));

		os_snprintf(buf, sizeof(buf), "%08X-%08X",
			    sta->acct_session_id_hi, sta->acct_session_id_lo);
		if (!radius_msg_add_attr(msg, RADIUS_ATTR_ACCT_SESSION_ID,
					 (u8 *) buf, os_strlen(buf))) {
			printf("Could not add Acct-Session-Id\n");
			goto fail;
		}
	} else {
		radius_msg_make_authenticator(msg, (u8 *) hapd, sizeof(*hapd));
	}

	if (!radius_msg_add_attr_int32(msg, RADIUS_ATTR_ACCT_STATUS_TYPE,
				       status_type)) {
		printf("Could not add Acct-Status-Type\n");
		goto fail;
	}

	if (!radius_msg_add_attr_int32(msg, RADIUS_ATTR_ACCT_AUTHENTIC,
				       hapd->conf->ieee802_1x ?
				       RADIUS_ACCT_AUTHENTIC_RADIUS :
				       RADIUS_ACCT_AUTHENTIC_LOCAL)) {
		printf("Could not add Acct-Authentic\n");
		goto fail;
	}

	if (sta) {
		val = ieee802_1x_get_identity(sta->eapol_sm, &len);
		if (!val) {
			os_snprintf(buf, sizeof(buf), RADIUS_ADDR_FORMAT,
				    MAC2STR(sta->addr));
			val = (u8 *) buf;
			len = os_strlen(buf);
		}

		if (!radius_msg_add_attr(msg, RADIUS_ATTR_USER_NAME, val,
					 len)) {
			printf("Could not add User-Name\n");
			goto fail;
		}
	}

	if (hapd->conf->own_ip_addr.af == AF_INET &&
	    !radius_msg_add_attr(msg, RADIUS_ATTR_NAS_IP_ADDRESS,
				 (u8 *) &hapd->conf->own_ip_addr.u.v4, 4)) {
		printf("Could not add NAS-IP-Address\n");
		goto fail;
	}

#ifdef CONFIG_IPV6
	if (hapd->conf->own_ip_addr.af == AF_INET6 &&
	    !radius_msg_add_attr(msg, RADIUS_ATTR_NAS_IPV6_ADDRESS,
				 (u8 *) &hapd->conf->own_ip_addr.u.v6, 16)) {
		printf("Could not add NAS-IPv6-Address\n");
		goto fail;
	}
#endif /* CONFIG_IPV6 */

	if (hapd->conf->nas_identifier &&
	    !radius_msg_add_attr(msg, RADIUS_ATTR_NAS_IDENTIFIER,
				 (u8 *) hapd->conf->nas_identifier,
				 os_strlen(hapd->conf->nas_identifier))) {
		printf("Could not add NAS-Identifier\n");
		goto fail;
	}

	if (sta &&
	    !radius_msg_add_attr_int32(msg, RADIUS_ATTR_NAS_PORT, sta->aid)) {
		printf("Could not add NAS-Port\n");
		goto fail;
	}

	os_snprintf(buf, sizeof(buf), RADIUS_802_1X_ADDR_FORMAT ":%s",
		    MAC2STR(hapd->own_addr), hapd->conf->ssid.ssid);
	if (!radius_msg_add_attr(msg, RADIUS_ATTR_CALLED_STATION_ID,
				 (u8 *) buf, os_strlen(buf))) {
		printf("Could not add Called-Station-Id\n");
		goto fail;
	}

	if (sta) {
		os_snprintf(buf, sizeof(buf), RADIUS_802_1X_ADDR_FORMAT,
			    MAC2STR(sta->addr));
		if (!radius_msg_add_attr(msg, RADIUS_ATTR_CALLING_STATION_ID,
					 (u8 *) buf, os_strlen(buf))) {
			printf("Could not add Calling-Station-Id\n");
			goto fail;
		}

		if (!radius_msg_add_attr_int32(
			    msg, RADIUS_ATTR_NAS_PORT_TYPE,
			    RADIUS_NAS_PORT_TYPE_IEEE_802_11)) {
			printf("Could not add NAS-Port-Type\n");
			goto fail;
		}

		os_snprintf(buf, sizeof(buf), "CONNECT %d%sMbps %s",
			    radius_sta_rate(hapd, sta) / 2,
			    (radius_sta_rate(hapd, sta) & 1) ? ".5" : "",
			    radius_mode_txt(hapd));
		if (!radius_msg_add_attr(msg, RADIUS_ATTR_CONNECT_INFO,
					 (u8 *) buf, os_strlen(buf))) {
			printf("Could not add Connect-Info\n");
			goto fail;
		}

		for (i = 0; ; i++) {
			val = ieee802_1x_get_radius_class(sta->eapol_sm, &len,
							  i);
			if (val == NULL)
				break;

			if (!radius_msg_add_attr(msg, RADIUS_ATTR_CLASS,
						 val, len)) {
				printf("Could not add Class\n");
				goto fail;
			}
		}
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

	if (sta->last_rx_bytes > data->rx_bytes)
		sta->acct_input_gigawords++;
	if (sta->last_tx_bytes > data->tx_bytes)
		sta->acct_output_gigawords++;
	sta->last_rx_bytes = data->rx_bytes;
	sta->last_tx_bytes = data->tx_bytes;

	hostapd_logger(hapd, sta->addr, HOSTAPD_MODULE_RADIUS,
		       HOSTAPD_LEVEL_DEBUG, "updated TX/RX stats: "
		       "Acct-Input-Octets=%lu Acct-Input-Gigawords=%u "
		       "Acct-Output-Octets=%lu Acct-Output-Gigawords=%u",
		       sta->last_rx_bytes, sta->acct_input_gigawords,
		       sta->last_tx_bytes, sta->acct_output_gigawords);

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

	accounting_sta_get_id(hapd, sta);
	hostapd_logger(hapd, sta->addr, HOSTAPD_MODULE_RADIUS,
		       HOSTAPD_LEVEL_INFO,
		       "starting accounting session %08X-%08X",
		       sta->acct_session_id_hi, sta->acct_session_id_lo);

	time(&sta->acct_session_start);
	sta->last_rx_bytes = sta->last_tx_bytes = 0;
	sta->acct_input_gigawords = sta->acct_output_gigawords = 0;
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
	if (msg)
		radius_client_send(hapd->radius, msg, RADIUS_ACCT, sta->addr);

	sta->acct_session_started = 1;
}


static void accounting_sta_report(struct hostapd_data *hapd,
				  struct sta_info *sta, int stop)
{
	struct radius_msg *msg;
	int cause = sta->acct_terminate_cause;
	struct hostap_sta_driver_data data;
	struct os_time now;
	u32 gigawords;

	if (!hapd->conf->radius->acct_server)
		return;

	msg = accounting_msg(hapd, sta,
			     stop ? RADIUS_ACCT_STATUS_TYPE_STOP :
			     RADIUS_ACCT_STATUS_TYPE_INTERIM_UPDATE);
	if (!msg) {
		printf("Could not create RADIUS Accounting message\n");
		return;
	}

	os_get_time(&now);
	if (!radius_msg_add_attr_int32(msg, RADIUS_ATTR_ACCT_SESSION_TIME,
				       now.sec - sta->acct_session_start)) {
		printf("Could not add Acct-Session-Time\n");
		goto fail;
	}

	if (accounting_sta_update_stats(hapd, sta, &data) == 0) {
		if (!radius_msg_add_attr_int32(msg,
					       RADIUS_ATTR_ACCT_INPUT_PACKETS,
					       data.rx_packets)) {
			printf("Could not add Acct-Input-Packets\n");
			goto fail;
		}
		if (!radius_msg_add_attr_int32(msg,
					       RADIUS_ATTR_ACCT_OUTPUT_PACKETS,
					       data.tx_packets)) {
			printf("Could not add Acct-Output-Packets\n");
			goto fail;
		}
		if (!radius_msg_add_attr_int32(msg,
					       RADIUS_ATTR_ACCT_INPUT_OCTETS,
					       data.rx_bytes)) {
			printf("Could not add Acct-Input-Octets\n");
			goto fail;
		}
		gigawords = sta->acct_input_gigawords;
#if __WORDSIZE == 64
		gigawords += data.rx_bytes >> 32;
#endif
		if (gigawords &&
		    !radius_msg_add_attr_int32(
			    msg, RADIUS_ATTR_ACCT_INPUT_GIGAWORDS,
			    gigawords)) {
			printf("Could not add Acct-Input-Gigawords\n");
			goto fail;
		}
		if (!radius_msg_add_attr_int32(msg,
					       RADIUS_ATTR_ACCT_OUTPUT_OCTETS,
					       data.tx_bytes)) {
			printf("Could not add Acct-Output-Octets\n");
			goto fail;
		}
		gigawords = sta->acct_output_gigawords;
#if __WORDSIZE == 64
		gigawords += data.tx_bytes >> 32;
#endif
		if (gigawords &&
		    !radius_msg_add_attr_int32(
			    msg, RADIUS_ATTR_ACCT_OUTPUT_GIGAWORDS,
			    gigawords)) {
			printf("Could not add Acct-Output-Gigawords\n");
			goto fail;
		}
	}

	if (!radius_msg_add_attr_int32(msg, RADIUS_ATTR_EVENT_TIMESTAMP,
				       now.sec)) {
		printf("Could not add Event-Timestamp\n");
		goto fail;
	}

	if (eloop_terminated())
		cause = RADIUS_ACCT_TERMINATE_CAUSE_ADMIN_REBOOT;

	if (stop && cause &&
	    !radius_msg_add_attr_int32(msg, RADIUS_ATTR_ACCT_TERMINATE_CAUSE,
				       cause)) {
		printf("Could not add Acct-Terminate-Cause\n");
		goto fail;
	}

	radius_client_send(hapd->radius, msg,
			   stop ? RADIUS_ACCT : RADIUS_ACCT_INTERIM,
			   sta->addr);
	return;

 fail:
	radius_msg_free(msg);
}


/**
 * accounting_sta_interim - Send a interim STA accounting report
 * @hapd: hostapd BSS data
 * @sta: The station
 */
void accounting_sta_interim(struct hostapd_data *hapd, struct sta_info *sta)
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
			       "stopped accounting session %08X-%08X",
			       sta->acct_session_id_hi,
			       sta->acct_session_id_lo);
		sta->acct_session_started = 0;
	}
}


static void accounting_sta_get_id(struct hostapd_data *hapd,
				  struct sta_info *sta)
{
	sta->acct_session_id_lo = hapd->acct_session_id_lo++;
	if (hapd->acct_session_id_lo == 0) {
		hapd->acct_session_id_hi++;
	}
	sta->acct_session_id_hi = hapd->acct_session_id_hi;
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
		printf("Unknown RADIUS message code\n");
		return RADIUS_RX_UNKNOWN;
	}

	if (radius_msg_verify(msg, shared_secret, shared_secret_len, req, 0)) {
		printf("Incoming RADIUS packet did not have correct "
		       "Authenticator - dropped\n");
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

	if (!radius_msg_add_attr_int32(msg, RADIUS_ATTR_ACCT_TERMINATE_CAUSE,
				       RADIUS_ACCT_TERMINATE_CAUSE_NAS_REBOOT))
	{
		printf("Could not add Acct-Terminate-Cause\n");
		radius_msg_free(msg);
		return;
	}

	radius_client_send(hapd->radius, msg, RADIUS_ACCT, NULL);
}


/**
 * accounting_init: Initialize accounting
 * @hapd: hostapd BSS data
 * Returns: 0 on success, -1 on failure
 */
int accounting_init(struct hostapd_data *hapd)
{
	struct os_time now;

	/* Acct-Session-Id should be unique over reboots. If reliable clock is
	 * not available, this could be replaced with reboot counter, etc. */
	os_get_time(&now);
	hapd->acct_session_id_hi = now.sec;

	if (radius_client_register(hapd->radius, RADIUS_ACCT,
				   accounting_receive, hapd))
		return -1;

	accounting_report_state(hapd, 1);

	return 0;
}


/**
 * accounting_deinit: Deinitilize accounting
 * @hapd: hostapd BSS data
 */
void accounting_deinit(struct hostapd_data *hapd)
{
	accounting_report_state(hapd, 0);
}
