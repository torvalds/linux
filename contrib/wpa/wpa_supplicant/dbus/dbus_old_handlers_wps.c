/*
 * WPA Supplicant / dbus-based control interface (WPS)
 * Copyright (c) 2006, Dan Williams <dcbw@redhat.com> and Red Hat, Inc.
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"
#include <dbus/dbus.h>

#include "common.h"
#include "../config.h"
#include "../wpa_supplicant_i.h"
#include "../wps_supplicant.h"
#include "dbus_old.h"
#include "dbus_old_handlers.h"

/**
 * wpas_dbus_iface_wps_pbc - Request credentials using WPS PBC method
 * @message: Pointer to incoming dbus message
 * @wpa_s: %wpa_supplicant data structure
 * Returns: A dbus message containing a UINT32 indicating success (1) or
 *          failure (0)
 *
 * Handler function for "wpsPbc" method call
 */
DBusMessage * wpas_dbus_iface_wps_pbc(DBusMessage *message,
				      struct wpa_supplicant *wpa_s)
{
	char *arg_bssid = NULL;
	u8 bssid[ETH_ALEN];
	int ret = 0;

	if (!dbus_message_get_args(message, NULL, DBUS_TYPE_STRING, &arg_bssid,
				   DBUS_TYPE_INVALID))
		return wpas_dbus_new_invalid_opts_error(message, NULL);

	if (os_strcmp(arg_bssid, "any") == 0)
		ret = wpas_wps_start_pbc(wpa_s, NULL, 0);
	else if (!hwaddr_aton(arg_bssid, bssid))
		ret = wpas_wps_start_pbc(wpa_s, bssid, 0);
	else {
		return wpas_dbus_new_invalid_opts_error(message,
							"Invalid BSSID");
	}

	if (ret < 0) {
		return dbus_message_new_error(
			message, WPAS_ERROR_WPS_PBC_ERROR,
			"Could not start PBC negotiation");
	}

	return wpas_dbus_new_success_reply(message);
}


/**
 * wpas_dbus_iface_wps_pin - Establish the PIN number of the enrollee
 * @message: Pointer to incoming dbus message
 * @wpa_s: %wpa_supplicant data structure
 * Returns: A dbus message containing a UINT32 indicating success (1) or
 *          failure (0)
 *
 * Handler function for "wpsPin" method call
 */
DBusMessage * wpas_dbus_iface_wps_pin(DBusMessage *message,
				      struct wpa_supplicant *wpa_s)
{
	DBusMessage *reply = NULL;
	char *arg_bssid;
	char *pin = NULL;
	u8 bssid[ETH_ALEN], *_bssid = NULL;
	int ret = 0;
	char npin[9];

	if (!dbus_message_get_args(message, NULL, DBUS_TYPE_STRING, &arg_bssid,
				   DBUS_TYPE_STRING, &pin, DBUS_TYPE_INVALID))
		return wpas_dbus_new_invalid_opts_error(message, NULL);

	if (os_strcmp(arg_bssid, "any") == 0)
		_bssid = NULL;
	else if (!hwaddr_aton(arg_bssid, bssid))
		_bssid = bssid;
	else {
		return wpas_dbus_new_invalid_opts_error(message,
							"Invalid BSSID");
	}

	if (os_strlen(pin) > 0)
		ret = wpas_wps_start_pin(wpa_s, _bssid, pin, 0,
					 DEV_PW_DEFAULT);
	else
		ret = wpas_wps_start_pin(wpa_s, _bssid, NULL, 0,
					 DEV_PW_DEFAULT);

	if (ret < 0) {
		return dbus_message_new_error(message,
					      WPAS_ERROR_WPS_PIN_ERROR,
					      "Could not init PIN");
	}

	reply = dbus_message_new_method_return(message);
	if (reply == NULL)
		return NULL;

	if (ret > 0) {
		os_snprintf(npin, sizeof(npin), "%08d", ret);
		pin = npin;
	}
	dbus_message_append_args(reply, DBUS_TYPE_STRING, &pin,
				 DBUS_TYPE_INVALID);
	return reply;
}


/**
 * wpas_dbus_iface_wps_reg - Request credentials using the PIN of the AP
 * @message: Pointer to incoming dbus message
 * @wpa_s: %wpa_supplicant data structure
 * Returns: A dbus message containing a UINT32 indicating success (1) or
 *          failure (0)
 *
 * Handler function for "wpsReg" method call
 */
DBusMessage * wpas_dbus_iface_wps_reg(DBusMessage *message,
				      struct wpa_supplicant *wpa_s)
{
	char *arg_bssid;
	char *pin = NULL;
	u8 bssid[ETH_ALEN];
	int ret = 0;

	if (!dbus_message_get_args(message, NULL, DBUS_TYPE_STRING, &arg_bssid,
				   DBUS_TYPE_STRING, &pin, DBUS_TYPE_INVALID))
		return wpas_dbus_new_invalid_opts_error(message, NULL);

	if (!hwaddr_aton(arg_bssid, bssid))
		ret = wpas_wps_start_reg(wpa_s, bssid, pin, NULL);
	else {
		return wpas_dbus_new_invalid_opts_error(message,
							"Invalid BSSID");
	}

	if (ret < 0) {
		return dbus_message_new_error(message,
					      WPAS_ERROR_WPS_REG_ERROR,
					      "Could not request credentials");
	}

	return wpas_dbus_new_success_reply(message);
}
