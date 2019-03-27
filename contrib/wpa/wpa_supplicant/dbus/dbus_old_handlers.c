/*
 * WPA Supplicant / dbus-based control interface
 * Copyright (c) 2006, Dan Williams <dcbw@redhat.com> and Red Hat, Inc.
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"
#include <dbus/dbus.h>

#include "common.h"
#include "eap_peer/eap_methods.h"
#include "common/ieee802_11_defs.h"
#include "eapol_supp/eapol_supp_sm.h"
#include "rsn_supp/wpa.h"
#include "../config.h"
#include "../wpa_supplicant_i.h"
#include "../driver_i.h"
#include "../notify.h"
#include "../wpas_glue.h"
#include "../bss.h"
#include "../scan.h"
#include "dbus_old.h"
#include "dbus_old_handlers.h"
#include "dbus_dict_helpers.h"

/**
 * wpas_dbus_new_invalid_opts_error - Return a new invalid options error message
 * @message: Pointer to incoming dbus message this error refers to
 * Returns: a dbus error message
 *
 * Convenience function to create and return an invalid options error
 */
DBusMessage * wpas_dbus_new_invalid_opts_error(DBusMessage *message,
					       const char *arg)
{
	DBusMessage *reply;

	reply = dbus_message_new_error(
		message, WPAS_ERROR_INVALID_OPTS,
		"Did not receive correct message arguments.");
	if (arg != NULL)
		dbus_message_append_args(reply, DBUS_TYPE_STRING, &arg,
					 DBUS_TYPE_INVALID);

	return reply;
}


/**
 * wpas_dbus_new_success_reply - Return a new success reply message
 * @message: Pointer to incoming dbus message this reply refers to
 * Returns: a dbus message containing a single UINT32 that indicates
 *          success (ie, a value of 1)
 *
 * Convenience function to create and return a success reply message
 */
DBusMessage * wpas_dbus_new_success_reply(DBusMessage *message)
{
	DBusMessage *reply;
	unsigned int success = 1;

	reply = dbus_message_new_method_return(message);
	dbus_message_append_args(reply, DBUS_TYPE_UINT32, &success,
				 DBUS_TYPE_INVALID);
	return reply;
}


/**
 * wpas_dbus_global_add_interface - Request registration of a network interface
 * @message: Pointer to incoming dbus message
 * @global: %wpa_supplicant global data structure
 * Returns: The object path of the new interface object,
 *          or a dbus error message with more information
 *
 * Handler function for "addInterface" method call. Handles requests
 * by dbus clients to register a network interface that wpa_supplicant
 * will manage.
 */
DBusMessage * wpas_dbus_global_add_interface(DBusMessage *message,
					     struct wpa_global *global)
{
	char *ifname = NULL;
	char *driver = NULL;
	char *driver_param = NULL;
	char *confname = NULL;
	char *bridge_ifname = NULL;
	DBusMessage *reply = NULL;
	DBusMessageIter iter;

	dbus_message_iter_init(message, &iter);

	/* First argument: interface name (DBUS_TYPE_STRING)
	 *    Required; must be non-zero length
	 */
	if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_STRING)
		goto error;
	dbus_message_iter_get_basic(&iter, &ifname);
	if (!os_strlen(ifname))
		goto error;

	/* Second argument: dict of options */
	if (dbus_message_iter_next(&iter)) {
		DBusMessageIter iter_dict;
		struct wpa_dbus_dict_entry entry;

		if (!wpa_dbus_dict_open_read(&iter, &iter_dict, NULL))
			goto error;
		while (wpa_dbus_dict_has_dict_entry(&iter_dict)) {
			if (!wpa_dbus_dict_get_entry(&iter_dict, &entry))
				goto error;
			if (!strcmp(entry.key, "driver") &&
			    entry.type == DBUS_TYPE_STRING) {
				os_free(driver);
				driver = os_strdup(entry.str_value);
				wpa_dbus_dict_entry_clear(&entry);
				if (driver == NULL)
					goto error;
			} else if (!strcmp(entry.key, "driver-params") &&
				   entry.type == DBUS_TYPE_STRING) {
				os_free(driver_param);
				driver_param = os_strdup(entry.str_value);
				wpa_dbus_dict_entry_clear(&entry);
				if (driver_param == NULL)
					goto error;
			} else if (!strcmp(entry.key, "config-file") &&
				   entry.type == DBUS_TYPE_STRING) {
				os_free(confname);
				confname = os_strdup(entry.str_value);
				wpa_dbus_dict_entry_clear(&entry);
				if (confname == NULL)
					goto error;
			} else if (!strcmp(entry.key, "bridge-ifname") &&
				   entry.type == DBUS_TYPE_STRING) {
				os_free(bridge_ifname);
				bridge_ifname = os_strdup(entry.str_value);
				wpa_dbus_dict_entry_clear(&entry);
				if (bridge_ifname == NULL)
					goto error;
			} else {
				wpa_dbus_dict_entry_clear(&entry);
				goto error;
			}
		}
	}

	/*
	 * Try to get the wpa_supplicant record for this iface, return
	 * an error if we already control it.
	 */
	if (wpa_supplicant_get_iface(global, ifname) != NULL) {
		reply = dbus_message_new_error(
			message, WPAS_ERROR_EXISTS_ERROR,
			"wpa_supplicant already controls this interface.");
	} else {
		struct wpa_supplicant *wpa_s;
		struct wpa_interface iface;

		os_memset(&iface, 0, sizeof(iface));
		iface.ifname = ifname;
		iface.driver = driver;
		iface.driver_param = driver_param;
		iface.confname = confname;
		iface.bridge_ifname = bridge_ifname;
		/* Otherwise, have wpa_supplicant attach to it. */
		wpa_s = wpa_supplicant_add_iface(global, &iface, NULL);
		if (wpa_s && wpa_s->dbus_path) {
			const char *path = wpa_s->dbus_path;

			reply = dbus_message_new_method_return(message);
			dbus_message_append_args(reply, DBUS_TYPE_OBJECT_PATH,
						 &path, DBUS_TYPE_INVALID);
		} else {
			reply = dbus_message_new_error(
				message, WPAS_ERROR_ADD_ERROR,
				"wpa_supplicant couldn't grab this interface.");
		}
	}

out:
	os_free(driver);
	os_free(driver_param);
	os_free(confname);
	os_free(bridge_ifname);
	return reply;

error:
	reply = wpas_dbus_new_invalid_opts_error(message, NULL);
	goto out;
}


/**
 * wpas_dbus_global_remove_interface - Request deregistration of an interface
 * @message: Pointer to incoming dbus message
 * @global: wpa_supplicant global data structure
 * Returns: a dbus message containing a UINT32 indicating success (1) or
 *          failure (0), or returns a dbus error message with more information
 *
 * Handler function for "removeInterface" method call.  Handles requests
 * by dbus clients to deregister a network interface that wpa_supplicant
 * currently manages.
 */
DBusMessage * wpas_dbus_global_remove_interface(DBusMessage *message,
						struct wpa_global *global)
{
	struct wpa_supplicant *wpa_s;
	char *path;
	DBusMessage *reply = NULL;

	if (!dbus_message_get_args(message, NULL,
				   DBUS_TYPE_OBJECT_PATH, &path,
				   DBUS_TYPE_INVALID)) {
		reply = wpas_dbus_new_invalid_opts_error(message, NULL);
		goto out;
	}

	wpa_s = wpa_supplicant_get_iface_by_dbus_path(global, path);
	if (wpa_s == NULL) {
		reply = wpas_dbus_new_invalid_iface_error(message);
		goto out;
	}

	if (!wpa_supplicant_remove_iface(global, wpa_s, 0)) {
		reply = wpas_dbus_new_success_reply(message);
	} else {
		reply = dbus_message_new_error(
			message, WPAS_ERROR_REMOVE_ERROR,
			"wpa_supplicant couldn't remove this interface.");
	}

out:
	return reply;
}


/**
 * wpas_dbus_global_get_interface - Get the object path for an interface name
 * @message: Pointer to incoming dbus message
 * @global: %wpa_supplicant global data structure
 * Returns: The object path of the interface object,
 *          or a dbus error message with more information
 *
 * Handler function for "getInterface" method call. Handles requests
 * by dbus clients for the object path of an specific network interface.
 */
DBusMessage * wpas_dbus_global_get_interface(DBusMessage *message,
					     struct wpa_global *global)
{
	DBusMessage *reply = NULL;
	const char *ifname;
	const char *path;
	struct wpa_supplicant *wpa_s;

	if (!dbus_message_get_args(message, NULL,
				   DBUS_TYPE_STRING, &ifname,
				   DBUS_TYPE_INVALID)) {
		reply = wpas_dbus_new_invalid_opts_error(message, NULL);
		goto out;
	}

	wpa_s = wpa_supplicant_get_iface(global, ifname);
	if (wpa_s == NULL || !wpa_s->dbus_path) {
		reply = wpas_dbus_new_invalid_iface_error(message);
		goto out;
	}

	path = wpa_s->dbus_path;
	reply = dbus_message_new_method_return(message);
	dbus_message_append_args(reply,
				 DBUS_TYPE_OBJECT_PATH, &path,
				 DBUS_TYPE_INVALID);

out:
	return reply;
}


/**
 * wpas_dbus_global_set_debugparams- Set the debug params
 * @message: Pointer to incoming dbus message
 * @global: %wpa_supplicant global data structure
 * Returns: a dbus message containing a UINT32 indicating success (1) or
 *          failure (0), or returns a dbus error message with more information
 *
 * Handler function for "setDebugParams" method call. Handles requests
 * by dbus clients for the object path of an specific network interface.
 */
DBusMessage * wpas_dbus_global_set_debugparams(DBusMessage *message,
					       struct wpa_global *global)
{
	DBusMessage *reply = NULL;
	int debug_level;
	dbus_bool_t debug_timestamp;
	dbus_bool_t debug_show_keys;

	if (!dbus_message_get_args(message, NULL,
				   DBUS_TYPE_INT32, &debug_level,
				   DBUS_TYPE_BOOLEAN, &debug_timestamp,
				   DBUS_TYPE_BOOLEAN, &debug_show_keys,
				   DBUS_TYPE_INVALID)) {
		return wpas_dbus_new_invalid_opts_error(message, NULL);
	}

	if (wpa_supplicant_set_debug_params(global, debug_level,
					    debug_timestamp ? 1 : 0,
					    debug_show_keys ? 1 : 0)) {
		return wpas_dbus_new_invalid_opts_error(message, NULL);
	}

	reply = wpas_dbus_new_success_reply(message);

	return reply;
}


/**
 * wpas_dbus_iface_scan - Request a wireless scan on an interface
 * @message: Pointer to incoming dbus message
 * @wpa_s: wpa_supplicant structure for a network interface
 * Returns: a dbus message containing a UINT32 indicating success (1) or
 *          failure (0)
 *
 * Handler function for "scan" method call of a network device. Requests
 * that wpa_supplicant perform a wireless scan as soon as possible
 * on a particular wireless interface.
 */
DBusMessage * wpas_dbus_iface_scan(DBusMessage *message,
				   struct wpa_supplicant *wpa_s)
{
	wpa_s->scan_req = MANUAL_SCAN_REQ;
	wpa_supplicant_req_scan(wpa_s, 0, 0);
	return wpas_dbus_new_success_reply(message);
}


/**
 * wpas_dbus_iface_scan_results - Get the results of a recent scan request
 * @message: Pointer to incoming dbus message
 * @wpa_s: wpa_supplicant structure for a network interface
 * Returns: a dbus message containing a dbus array of objects paths, or returns
 *          a dbus error message if not scan results could be found
 *
 * Handler function for "scanResults" method call of a network device. Returns
 * a dbus message containing the object paths of wireless networks found.
 */
DBusMessage * wpas_dbus_iface_scan_results(DBusMessage *message,
					   struct wpa_supplicant *wpa_s)
{
	DBusMessage *reply;
	DBusMessageIter iter;
	DBusMessageIter sub_iter;
	struct wpa_bss *bss;

	if (!wpa_s->dbus_path)
		return dbus_message_new_error(message,
					      WPAS_ERROR_INTERNAL_ERROR,
					      "no D-Bus interface available");

	/* Create and initialize the return message */
	reply = dbus_message_new_method_return(message);
	dbus_message_iter_init_append(reply, &iter);
	if (!dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY,
					      DBUS_TYPE_OBJECT_PATH_AS_STRING,
					      &sub_iter))
		goto error;

	/* Loop through scan results and append each result's object path */
	dl_list_for_each(bss, &wpa_s->bss_id, struct wpa_bss, list_id) {
		char path_buf[WPAS_DBUS_OBJECT_PATH_MAX];
		char *path = path_buf;

		/* Construct the object path for this network.  Note that ':'
		 * is not a valid character in dbus object paths.
		 */
		os_snprintf(path, WPAS_DBUS_OBJECT_PATH_MAX,
			    "%s/" WPAS_DBUS_BSSIDS_PART "/"
			    WPAS_DBUS_BSSID_FORMAT,
			    wpa_s->dbus_path, MAC2STR(bss->bssid));
		if (!dbus_message_iter_append_basic(&sub_iter,
						    DBUS_TYPE_OBJECT_PATH,
						    &path))
			goto error;
	}

	if (!dbus_message_iter_close_container(&iter, &sub_iter))
		goto error;

	return reply;

error:
	dbus_message_unref(reply);
	return dbus_message_new_error(message, WPAS_ERROR_INTERNAL_ERROR,
				      "an internal error occurred returning scan results");
}


/**
 * wpas_dbus_bssid_properties - Return the properties of a scanned network
 * @message: Pointer to incoming dbus message
 * @wpa_s: wpa_supplicant structure for a network interface
 * @res: wpa_supplicant scan result for which to get properties
 * Returns: a dbus message containing the properties for the requested network
 *
 * Handler function for "properties" method call of a scanned network.
 * Returns a dbus message containing the the properties.
 */
DBusMessage * wpas_dbus_bssid_properties(DBusMessage *message,
					 struct wpa_supplicant *wpa_s,
					 struct wpa_bss *bss)
{
	DBusMessage *reply;
	DBusMessageIter iter, iter_dict;
	const u8 *wpa_ie, *rsn_ie, *wps_ie;

	/* Dump the properties into a dbus message */
	reply = dbus_message_new_method_return(message);

	wpa_ie = wpa_bss_get_vendor_ie(bss, WPA_IE_VENDOR_TYPE);
	rsn_ie = wpa_bss_get_ie(bss, WLAN_EID_RSN);
	wps_ie = wpa_bss_get_vendor_ie(bss, WPS_IE_VENDOR_TYPE);

	dbus_message_iter_init_append(reply, &iter);
	if (!wpa_dbus_dict_open_write(&iter, &iter_dict) ||
	    !wpa_dbus_dict_append_byte_array(&iter_dict, "bssid",
					     (const char *) bss->bssid,
					     ETH_ALEN) ||
	    !wpa_dbus_dict_append_byte_array(&iter_dict, "ssid",
					     (const char *) bss->ssid,
					     bss->ssid_len) ||
	    (wpa_ie &&
	     !wpa_dbus_dict_append_byte_array(&iter_dict, "wpaie",
					      (const char *) wpa_ie,
					      wpa_ie[1] + 2)) ||
	    (rsn_ie &&
	     !wpa_dbus_dict_append_byte_array(&iter_dict, "rsnie",
					      (const char *) rsn_ie,
					      rsn_ie[1] + 2)) ||
	    (wps_ie &&
	     !wpa_dbus_dict_append_byte_array(&iter_dict, "wpsie",
					     (const char *) wps_ie,
					      wps_ie[1] + 2)) ||
	    (bss->freq &&
	     !wpa_dbus_dict_append_int32(&iter_dict, "frequency", bss->freq)) ||
	    !wpa_dbus_dict_append_uint16(&iter_dict, "capabilities",
					 bss->caps) ||
	    (!(bss->flags & WPA_BSS_QUAL_INVALID) &&
	     !wpa_dbus_dict_append_int32(&iter_dict, "quality", bss->qual)) ||
	    (!(bss->flags & WPA_BSS_NOISE_INVALID) &&
	     !wpa_dbus_dict_append_int32(&iter_dict, "noise", bss->noise)) ||
	    (!(bss->flags & WPA_BSS_LEVEL_INVALID) &&
	     !wpa_dbus_dict_append_int32(&iter_dict, "level", bss->level)) ||
	    !wpa_dbus_dict_append_int32(&iter_dict, "maxrate",
					wpa_bss_get_max_rate(bss) * 500000) ||
	    !wpa_dbus_dict_close_write(&iter, &iter_dict)) {
		if (reply)
			dbus_message_unref(reply);
		reply = dbus_message_new_error(
			message, WPAS_ERROR_INTERNAL_ERROR,
			"an internal error occurred returning BSSID properties.");
	}

	return reply;
}


/**
 * wpas_dbus_iface_capabilities - Return interface capabilities
 * @message: Pointer to incoming dbus message
 * @wpa_s: wpa_supplicant structure for a network interface
 * Returns: A dbus message containing a dict of strings
 *
 * Handler function for "capabilities" method call of an interface.
 */
DBusMessage * wpas_dbus_iface_capabilities(DBusMessage *message,
					   struct wpa_supplicant *wpa_s)
{
	DBusMessage *reply = NULL;
	struct wpa_driver_capa capa;
	int res;
	DBusMessageIter iter, iter_dict;
	char **eap_methods;
	size_t num_items;
	dbus_bool_t strict = FALSE;
	DBusMessageIter iter_dict_entry, iter_dict_val, iter_array;

	if (!dbus_message_get_args(message, NULL,
				   DBUS_TYPE_BOOLEAN, &strict,
				   DBUS_TYPE_INVALID))
		strict = FALSE;

	reply = dbus_message_new_method_return(message);

	dbus_message_iter_init_append(reply, &iter);
	if (!wpa_dbus_dict_open_write(&iter, &iter_dict))
		goto error;

	/* EAP methods */
	eap_methods = eap_get_names_as_string_array(&num_items);
	if (eap_methods) {
		dbus_bool_t success;
		size_t i = 0;

		success = wpa_dbus_dict_append_string_array(
			&iter_dict, "eap", (const char **) eap_methods,
			num_items);

		/* free returned method array */
		while (eap_methods[i])
			os_free(eap_methods[i++]);
		os_free(eap_methods);

		if (!success)
			goto error;
	}

	res = wpa_drv_get_capa(wpa_s, &capa);

	/***** pairwise cipher */
	if (res < 0) {
		if (!strict) {
			const char *args[] = {"CCMP", "TKIP", "NONE"};

			if (!wpa_dbus_dict_append_string_array(
				    &iter_dict, "pairwise", args,
				    ARRAY_SIZE(args)))
				goto error;
		}
	} else {
		if (!wpa_dbus_dict_begin_string_array(&iter_dict, "pairwise",
						      &iter_dict_entry,
						      &iter_dict_val,
						      &iter_array) ||
		    ((capa.enc & WPA_DRIVER_CAPA_ENC_CCMP) &&
		     !wpa_dbus_dict_string_array_add_element(
			     &iter_array, "CCMP")) ||
		    ((capa.enc & WPA_DRIVER_CAPA_ENC_TKIP) &&
		     !wpa_dbus_dict_string_array_add_element(
			     &iter_array, "TKIP")) ||
		    ((capa.key_mgmt & WPA_DRIVER_CAPA_KEY_MGMT_WPA_NONE) &&
		     !wpa_dbus_dict_string_array_add_element(
			     &iter_array, "NONE")) ||
		    !wpa_dbus_dict_end_string_array(&iter_dict,
						    &iter_dict_entry,
						    &iter_dict_val,
						    &iter_array))
			goto error;
	}

	/***** group cipher */
	if (res < 0) {
		if (!strict) {
			const char *args[] = {
				"CCMP", "TKIP", "WEP104", "WEP40"
			};

			if (!wpa_dbus_dict_append_string_array(
				    &iter_dict, "group", args,
				    ARRAY_SIZE(args)))
				goto error;
		}
	} else {
		if (!wpa_dbus_dict_begin_string_array(&iter_dict, "group",
						      &iter_dict_entry,
						      &iter_dict_val,
						      &iter_array))
			goto error;

		if (((capa.enc & WPA_DRIVER_CAPA_ENC_CCMP) &&
		     !wpa_dbus_dict_string_array_add_element(
			     &iter_array, "CCMP")) ||
		    ((capa.enc & WPA_DRIVER_CAPA_ENC_TKIP) &&
		     !wpa_dbus_dict_string_array_add_element(
			     &iter_array, "TKIP")) ||
		    ((capa.enc & WPA_DRIVER_CAPA_ENC_WEP104) &&
		     !wpa_dbus_dict_string_array_add_element(
			     &iter_array, "WEP104")) ||
		    ((capa.enc & WPA_DRIVER_CAPA_ENC_WEP40) &&
		     !wpa_dbus_dict_string_array_add_element(
			     &iter_array, "WEP40")) ||
		    !wpa_dbus_dict_end_string_array(&iter_dict,
						    &iter_dict_entry,
						    &iter_dict_val,
						    &iter_array))
			goto error;
	}

	/***** key management */
	if (res < 0) {
		if (!strict) {
			const char *args[] = {
				"WPA-PSK", "WPA-EAP", "IEEE8021X", "WPA-NONE",
				"NONE"
			};
			if (!wpa_dbus_dict_append_string_array(
				    &iter_dict, "key_mgmt", args,
				    ARRAY_SIZE(args)))
				goto error;
		}
	} else {
		if (!wpa_dbus_dict_begin_string_array(&iter_dict, "key_mgmt",
						      &iter_dict_entry,
						      &iter_dict_val,
						      &iter_array) ||
		    !wpa_dbus_dict_string_array_add_element(&iter_array,
							    "NONE") ||
		    !wpa_dbus_dict_string_array_add_element(&iter_array,
							    "IEEE8021X") ||
		    ((capa.key_mgmt & (WPA_DRIVER_CAPA_KEY_MGMT_WPA |
				       WPA_DRIVER_CAPA_KEY_MGMT_WPA2)) &&
		     !wpa_dbus_dict_string_array_add_element(
			     &iter_array, "WPA-EAP")) ||
		    ((capa.key_mgmt & (WPA_DRIVER_CAPA_KEY_MGMT_WPA_PSK |
				       WPA_DRIVER_CAPA_KEY_MGMT_WPA2_PSK)) &&
		     !wpa_dbus_dict_string_array_add_element(
			     &iter_array, "WPA-PSK")) ||
		    ((capa.key_mgmt & WPA_DRIVER_CAPA_KEY_MGMT_WPA_NONE) &&
		     !wpa_dbus_dict_string_array_add_element(
			     &iter_array, "WPA-NONE")) ||
		    !wpa_dbus_dict_end_string_array(&iter_dict,
						    &iter_dict_entry,
						    &iter_dict_val,
						    &iter_array))
			goto error;
	}

	/***** WPA protocol */
	if (res < 0) {
		if (!strict) {
			const char *args[] = { "RSN", "WPA" };

			if (!wpa_dbus_dict_append_string_array(
				    &iter_dict, "proto", args,
				    ARRAY_SIZE(args)))
				goto error;
		}
	} else {
		if (!wpa_dbus_dict_begin_string_array(&iter_dict, "proto",
						      &iter_dict_entry,
						      &iter_dict_val,
						      &iter_array) ||
		    ((capa.key_mgmt & (WPA_DRIVER_CAPA_KEY_MGMT_WPA2 |
				       WPA_DRIVER_CAPA_KEY_MGMT_WPA2_PSK)) &&
		     !wpa_dbus_dict_string_array_add_element(
			     &iter_array, "RSN")) ||
		    ((capa.key_mgmt & (WPA_DRIVER_CAPA_KEY_MGMT_WPA |
				       WPA_DRIVER_CAPA_KEY_MGMT_WPA_PSK)) &&
		     !wpa_dbus_dict_string_array_add_element(
			     &iter_array, "WPA")) ||
		    !wpa_dbus_dict_end_string_array(&iter_dict,
						    &iter_dict_entry,
						    &iter_dict_val,
						    &iter_array))
			goto error;
	}

	/***** auth alg */
	if (res < 0) {
		if (!strict) {
			const char *args[] = { "OPEN", "SHARED", "LEAP" };

			if (!wpa_dbus_dict_append_string_array(
				    &iter_dict, "auth_alg", args,
				    ARRAY_SIZE(args)))
				goto error;
		}
	} else {
		if (!wpa_dbus_dict_begin_string_array(&iter_dict, "auth_alg",
						      &iter_dict_entry,
						      &iter_dict_val,
						      &iter_array) ||
		    ((capa.auth & WPA_DRIVER_AUTH_OPEN) &&
		     !wpa_dbus_dict_string_array_add_element(
			     &iter_array, "OPEN")) ||
		    ((capa.auth & WPA_DRIVER_AUTH_SHARED) &&
		     !wpa_dbus_dict_string_array_add_element(
			     &iter_array, "SHARED")) ||
		    ((capa.auth & WPA_DRIVER_AUTH_LEAP) &&
		     !wpa_dbus_dict_string_array_add_element(
			     &iter_array, "LEAP")) ||
		    !wpa_dbus_dict_end_string_array(&iter_dict,
						    &iter_dict_entry,
						    &iter_dict_val,
						    &iter_array))
			goto error;
	}

	if (!wpa_dbus_dict_close_write(&iter, &iter_dict))
		goto error;

	return reply;

error:
	if (reply)
		dbus_message_unref(reply);
	return dbus_message_new_error(
		message, WPAS_ERROR_INTERNAL_ERROR,
		"an internal error occurred returning interface capabilities.");
}


/**
 * wpas_dbus_iface_add_network - Add a new configured network
 * @message: Pointer to incoming dbus message
 * @wpa_s: wpa_supplicant structure for a network interface
 * Returns: A dbus message containing the object path of the new network
 *
 * Handler function for "addNetwork" method call of a network interface.
 */
DBusMessage * wpas_dbus_iface_add_network(DBusMessage *message,
					  struct wpa_supplicant *wpa_s)
{
	DBusMessage *reply = NULL;
	struct wpa_ssid *ssid = NULL;
	char path_buf[WPAS_DBUS_OBJECT_PATH_MAX], *path = path_buf;

	if (wpa_s->dbus_path)
		ssid = wpa_supplicant_add_network(wpa_s);
	if (ssid == NULL) {
		reply = dbus_message_new_error(
			message, WPAS_ERROR_ADD_NETWORK_ERROR,
			"wpa_supplicant could not add a network on this interface.");
		goto out;
	}

	/* Construct the object path for this network. */
	os_snprintf(path, WPAS_DBUS_OBJECT_PATH_MAX,
		    "%s/" WPAS_DBUS_NETWORKS_PART "/%d",
		    wpa_s->dbus_path, ssid->id);

	reply = dbus_message_new_method_return(message);
	dbus_message_append_args(reply, DBUS_TYPE_OBJECT_PATH,
				 &path, DBUS_TYPE_INVALID);

out:
	return reply;
}


/**
 * wpas_dbus_iface_remove_network - Remove a configured network
 * @message: Pointer to incoming dbus message
 * @wpa_s: wpa_supplicant structure for a network interface
 * Returns: A dbus message containing a UINT32 indicating success (1) or
 *          failure (0)
 *
 * Handler function for "removeNetwork" method call of a network interface.
 */
DBusMessage * wpas_dbus_iface_remove_network(DBusMessage *message,
					     struct wpa_supplicant *wpa_s)
{
	DBusMessage *reply = NULL;
	const char *op;
	char *iface = NULL, *net_id = NULL;
	int id;
	int result;

	if (!dbus_message_get_args(message, NULL,
				   DBUS_TYPE_OBJECT_PATH, &op,
				   DBUS_TYPE_INVALID)) {
		reply = wpas_dbus_new_invalid_opts_error(message, NULL);
		goto out;
	}

	/* Extract the network ID */
	iface = wpas_dbus_decompose_object_path(op, &net_id, NULL);
	if (iface == NULL || net_id == NULL) {
		reply = wpas_dbus_new_invalid_network_error(message);
		goto out;
	}

	/* Ensure the network is actually a child of this interface */
	if (!wpa_s->dbus_path || os_strcmp(iface, wpa_s->dbus_path) != 0) {
		reply = wpas_dbus_new_invalid_network_error(message);
		goto out;
	}

	id = strtoul(net_id, NULL, 10);
	result = wpa_supplicant_remove_network(wpa_s, id);
	if (result == -1) {
		reply = wpas_dbus_new_invalid_network_error(message);
		goto out;
	}
	if (result == -2) {
		reply = dbus_message_new_error(
			message, WPAS_ERROR_REMOVE_NETWORK_ERROR,
			"error removing the specified on this interface.");
		goto out;
	}

	reply = wpas_dbus_new_success_reply(message);

out:
	os_free(iface);
	os_free(net_id);
	return reply;
}


static const char * const dont_quote[] = {
	"key_mgmt", "proto", "pairwise", "auth_alg", "group", "eap",
	"opensc_engine_path", "pkcs11_engine_path", "pkcs11_module_path",
	"bssid", "scan_freq", "freq_list", NULL
};


static dbus_bool_t should_quote_opt(const char *key)
{
	int i = 0;

	while (dont_quote[i] != NULL) {
		if (os_strcmp(key, dont_quote[i]) == 0)
			return FALSE;
		i++;
	}
	return TRUE;
}


/**
 * wpas_dbus_iface_set_network - Set options for a configured network
 * @message: Pointer to incoming dbus message
 * @wpa_s: wpa_supplicant structure for a network interface
 * @ssid: wpa_ssid structure for a configured network
 * Returns: a dbus message containing a UINT32 indicating success (1) or
 *          failure (0)
 *
 * Handler function for "set" method call of a configured network.
 */
DBusMessage * wpas_dbus_iface_set_network(DBusMessage *message,
					  struct wpa_supplicant *wpa_s,
					  struct wpa_ssid *ssid)
{
	DBusMessage *reply = NULL;
	struct wpa_dbus_dict_entry entry = { .type = DBUS_TYPE_STRING };
	DBusMessageIter	iter, iter_dict;

	dbus_message_iter_init(message, &iter);

	if (!wpa_dbus_dict_open_read(&iter, &iter_dict, NULL)) {
		reply = wpas_dbus_new_invalid_opts_error(message, NULL);
		goto out;
	}

	while (wpa_dbus_dict_has_dict_entry(&iter_dict)) {
		char *value = NULL;
		size_t size = 50;
		int ret;

		if (!wpa_dbus_dict_get_entry(&iter_dict, &entry)) {
			reply = wpas_dbus_new_invalid_opts_error(message,
								 NULL);
			goto out;
		}

		/* Type conversions, since wpa_supplicant wants strings */
		if (entry.type == DBUS_TYPE_ARRAY &&
		    entry.array_type == DBUS_TYPE_BYTE) {
			if (entry.array_len <= 0)
				goto error;

			size = entry.array_len * 2 + 1;
			value = os_zalloc(size);
			if (value == NULL)
				goto error;
			ret = wpa_snprintf_hex(value, size,
					       (u8 *) entry.bytearray_value,
					       entry.array_len);
			if (ret <= 0)
				goto error;
		} else if (entry.type == DBUS_TYPE_STRING) {
			if (should_quote_opt(entry.key)) {
				size = os_strlen(entry.str_value);
				/* Zero-length option check */
				if (size == 0)
					goto error;
				size += 3;  /* For quotes and terminator */
				value = os_zalloc(size);
				if (value == NULL)
					goto error;
				ret = os_snprintf(value, size, "\"%s\"",
						  entry.str_value);
				if (os_snprintf_error(size, ret))
					goto error;
			} else {
				value = os_strdup(entry.str_value);
				if (value == NULL)
					goto error;
			}
		} else if (entry.type == DBUS_TYPE_UINT32) {
			value = os_zalloc(size);
			if (value == NULL)
				goto error;
			ret = os_snprintf(value, size, "%u",
					  entry.uint32_value);
			if (os_snprintf_error(size, ret))
				goto error;
		} else if (entry.type == DBUS_TYPE_INT32) {
			value = os_zalloc(size);
			if (value == NULL)
				goto error;
			ret = os_snprintf(value, size, "%d",
					  entry.int32_value);
			if (os_snprintf_error(size, ret))
				goto error;
		} else
			goto error;

		if (wpa_config_set(ssid, entry.key, value, 0) < 0)
			goto error;

		if ((os_strcmp(entry.key, "psk") == 0 &&
		     value[0] == '"' && ssid->ssid_len) ||
		    (os_strcmp(entry.key, "ssid") == 0 && ssid->passphrase))
			wpa_config_update_psk(ssid);
		else if (os_strcmp(entry.key, "priority") == 0)
			wpa_config_update_prio_list(wpa_s->conf);

		os_free(value);
		wpa_dbus_dict_entry_clear(&entry);
		continue;

	error:
		os_free(value);
		reply = wpas_dbus_new_invalid_opts_error(message, entry.key);
		wpa_dbus_dict_entry_clear(&entry);
		break;
	}

	if (!reply)
		reply = wpas_dbus_new_success_reply(message);

out:
	return reply;
}


/**
 * wpas_dbus_iface_enable_network - Mark a configured network as enabled
 * @message: Pointer to incoming dbus message
 * @wpa_s: wpa_supplicant structure for a network interface
 * @ssid: wpa_ssid structure for a configured network
 * Returns: A dbus message containing a UINT32 indicating success (1) or
 *          failure (0)
 *
 * Handler function for "enable" method call of a configured network.
 */
DBusMessage * wpas_dbus_iface_enable_network(DBusMessage *message,
					     struct wpa_supplicant *wpa_s,
					     struct wpa_ssid *ssid)
{
	wpa_supplicant_enable_network(wpa_s, ssid);
	return wpas_dbus_new_success_reply(message);
}


/**
 * wpas_dbus_iface_disable_network - Mark a configured network as disabled
 * @message: Pointer to incoming dbus message
 * @wpa_s: wpa_supplicant structure for a network interface
 * @ssid: wpa_ssid structure for a configured network
 * Returns: A dbus message containing a UINT32 indicating success (1) or
 *          failure (0)
 *
 * Handler function for "disable" method call of a configured network.
 */
DBusMessage * wpas_dbus_iface_disable_network(DBusMessage *message,
					      struct wpa_supplicant *wpa_s,
					      struct wpa_ssid *ssid)
{
	wpa_supplicant_disable_network(wpa_s, ssid);
	return wpas_dbus_new_success_reply(message);
}


/**
 * wpas_dbus_iface_select_network - Attempt association with a configured network
 * @message: Pointer to incoming dbus message
 * @wpa_s: wpa_supplicant structure for a network interface
 * Returns: A dbus message containing a UINT32 indicating success (1) or
 *          failure (0)
 *
 * Handler function for "selectNetwork" method call of network interface.
 */
DBusMessage * wpas_dbus_iface_select_network(DBusMessage *message,
					     struct wpa_supplicant *wpa_s)
{
	DBusMessage *reply = NULL;
	const char *op;
	struct wpa_ssid *ssid;
	char *iface_obj_path = NULL;
	char *network = NULL;

	if (os_strlen(dbus_message_get_signature(message)) == 0) {
		/* Any network */
		ssid = NULL;
	} else {
		int nid;

		if (!dbus_message_get_args(message, NULL,
					   DBUS_TYPE_OBJECT_PATH, &op,
					   DBUS_TYPE_INVALID)) {
			reply = wpas_dbus_new_invalid_opts_error(message,
								 NULL);
			goto out;
		}

		/* Extract the network number */
		iface_obj_path = wpas_dbus_decompose_object_path(op,
								 &network,
								 NULL);
		if (iface_obj_path == NULL) {
			reply = wpas_dbus_new_invalid_iface_error(message);
			goto out;
		}
		/* Ensure the object path really points to this interface */
		if (network == NULL || !wpa_s->dbus_path ||
		    os_strcmp(iface_obj_path, wpa_s->dbus_path) != 0) {
			reply = wpas_dbus_new_invalid_network_error(message);
			goto out;
		}

		nid = strtoul(network, NULL, 10);
		if (errno == EINVAL) {
			reply = wpas_dbus_new_invalid_network_error(message);
			goto out;
		}

		ssid = wpa_config_get_network(wpa_s->conf, nid);
		if (ssid == NULL) {
			reply = wpas_dbus_new_invalid_network_error(message);
			goto out;
		}
	}

	/* Finally, associate with the network */
	wpa_supplicant_select_network(wpa_s, ssid);

	reply = wpas_dbus_new_success_reply(message);

out:
	os_free(iface_obj_path);
	os_free(network);
	return reply;
}


/**
 * wpas_dbus_iface_disconnect - Terminate the current connection
 * @message: Pointer to incoming dbus message
 * @wpa_s: wpa_supplicant structure for a network interface
 * Returns: A dbus message containing a UINT32 indicating success (1) or
 *          failure (0)
 *
 * Handler function for "disconnect" method call of network interface.
 */
DBusMessage * wpas_dbus_iface_disconnect(DBusMessage *message,
					 struct wpa_supplicant *wpa_s)
{
	wpas_request_disconnection(wpa_s);

	return wpas_dbus_new_success_reply(message);
}


/**
 * wpas_dbus_iface_set_ap_scan - Control roaming mode
 * @message: Pointer to incoming dbus message
 * @wpa_s: wpa_supplicant structure for a network interface
 * Returns: A dbus message containing a UINT32 indicating success (1) or
 *          failure (0)
 *
 * Handler function for "setAPScan" method call.
 */
DBusMessage * wpas_dbus_iface_set_ap_scan(DBusMessage *message,
					  struct wpa_supplicant *wpa_s)
{
	DBusMessage *reply = NULL;
	dbus_uint32_t ap_scan = 1;

	if (!dbus_message_get_args(message, NULL, DBUS_TYPE_UINT32, &ap_scan,
				   DBUS_TYPE_INVALID)) {
		reply = wpas_dbus_new_invalid_opts_error(message, NULL);
		goto out;
	}

	if (wpa_supplicant_set_ap_scan(wpa_s, ap_scan)) {
		reply = wpas_dbus_new_invalid_opts_error(message, NULL);
		goto out;
	}

	reply = wpas_dbus_new_success_reply(message);

out:
	return reply;
}


/**
 * wpas_dbus_iface_set_smartcard_modules - Set smartcard related module paths
 * @message: Pointer to incoming dbus message
 * @wpa_s: wpa_supplicant structure for a network interface
 * Returns: A dbus message containing a UINT32 indicating success (1) or
 *          failure (0)
 *
 * Handler function for "setSmartcardModules" method call.
 */
DBusMessage * wpas_dbus_iface_set_smartcard_modules(
	DBusMessage *message, struct wpa_supplicant *wpa_s)
{
	DBusMessageIter iter, iter_dict;
	char *opensc_engine_path = NULL;
	char *pkcs11_engine_path = NULL;
	char *pkcs11_module_path = NULL;
	struct wpa_dbus_dict_entry entry;

	if (!dbus_message_iter_init(message, &iter))
		goto error;

	if (!wpa_dbus_dict_open_read(&iter, &iter_dict, NULL))
		goto error;

	while (wpa_dbus_dict_has_dict_entry(&iter_dict)) {
		if (!wpa_dbus_dict_get_entry(&iter_dict, &entry))
			goto error;
		if (!strcmp(entry.key, "opensc_engine_path") &&
		    entry.type == DBUS_TYPE_STRING) {
			os_free(opensc_engine_path);
			opensc_engine_path = os_strdup(entry.str_value);
			wpa_dbus_dict_entry_clear(&entry);
			if (opensc_engine_path == NULL)
				goto error;
		} else if (!strcmp(entry.key, "pkcs11_engine_path") &&
			   entry.type == DBUS_TYPE_STRING) {
			os_free(pkcs11_engine_path);
			pkcs11_engine_path = os_strdup(entry.str_value);
			wpa_dbus_dict_entry_clear(&entry);
			if (pkcs11_engine_path == NULL)
				goto error;
		} else if (!strcmp(entry.key, "pkcs11_module_path") &&
				 entry.type == DBUS_TYPE_STRING) {
			os_free(pkcs11_module_path);
			pkcs11_module_path = os_strdup(entry.str_value);
			wpa_dbus_dict_entry_clear(&entry);
			if (pkcs11_module_path == NULL)
				goto error;
		} else {
			wpa_dbus_dict_entry_clear(&entry);
			goto error;
		}
	}

	os_free(wpa_s->conf->opensc_engine_path);
	wpa_s->conf->opensc_engine_path = opensc_engine_path;
	os_free(wpa_s->conf->pkcs11_engine_path);
	wpa_s->conf->pkcs11_engine_path = pkcs11_engine_path;
	os_free(wpa_s->conf->pkcs11_module_path);
	wpa_s->conf->pkcs11_module_path = pkcs11_module_path;

	wpa_sm_set_eapol(wpa_s->wpa, NULL);
	eapol_sm_deinit(wpa_s->eapol);
	wpa_s->eapol = NULL;
	wpa_supplicant_init_eapol(wpa_s);
	wpa_sm_set_eapol(wpa_s->wpa, wpa_s->eapol);

	return wpas_dbus_new_success_reply(message);

error:
	os_free(opensc_engine_path);
	os_free(pkcs11_engine_path);
	os_free(pkcs11_module_path);
	return wpas_dbus_new_invalid_opts_error(message, NULL);
}


/**
 * wpas_dbus_iface_get_state - Get interface state
 * @message: Pointer to incoming dbus message
 * @wpa_s: wpa_supplicant structure for a network interface
 * Returns: A dbus message containing a STRING representing the current
 *          interface state
 *
 * Handler function for "state" method call.
 */
DBusMessage * wpas_dbus_iface_get_state(DBusMessage *message,
					struct wpa_supplicant *wpa_s)
{
	DBusMessage *reply = NULL;
	const char *str_state;

	reply = dbus_message_new_method_return(message);
	if (reply != NULL) {
		str_state = wpa_supplicant_state_txt(wpa_s->wpa_state);
		dbus_message_append_args(reply, DBUS_TYPE_STRING, &str_state,
					 DBUS_TYPE_INVALID);
	}

	return reply;
}


/**
 * wpas_dbus_iface_get_scanning - Get interface scanning state
 * @message: Pointer to incoming dbus message
 * @wpa_s: wpa_supplicant structure for a network interface
 * Returns: A dbus message containing whether the interface is scanning
 *
 * Handler function for "scanning" method call.
 */
DBusMessage * wpas_dbus_iface_get_scanning(DBusMessage *message,
					   struct wpa_supplicant *wpa_s)
{
	DBusMessage *reply = NULL;
	dbus_bool_t scanning = wpa_s->scanning ? TRUE : FALSE;

	reply = dbus_message_new_method_return(message);
	if (reply != NULL) {
		dbus_message_append_args(reply, DBUS_TYPE_BOOLEAN, &scanning,
					 DBUS_TYPE_INVALID);
	} else {
		wpa_printf(MSG_ERROR,
			   "dbus: Not enough memory to return scanning state");
	}

	return reply;
}


#ifndef CONFIG_NO_CONFIG_BLOBS

/**
 * wpas_dbus_iface_set_blobs - Store named binary blobs (ie, for certificates)
 * @message: Pointer to incoming dbus message
 * @wpa_s: %wpa_supplicant data structure
 * Returns: A dbus message containing a UINT32 indicating success (1) or
 *          failure (0)
 *
 * Asks wpa_supplicant to internally store a one or more binary blobs.
 */
DBusMessage * wpas_dbus_iface_set_blobs(DBusMessage *message,
					struct wpa_supplicant *wpa_s)
{
	DBusMessage *reply = NULL;
	struct wpa_dbus_dict_entry entry = { .type = DBUS_TYPE_STRING };
	DBusMessageIter	iter, iter_dict;

	dbus_message_iter_init(message, &iter);

	if (!wpa_dbus_dict_open_read(&iter, &iter_dict, NULL))
		return wpas_dbus_new_invalid_opts_error(message, NULL);

	while (wpa_dbus_dict_has_dict_entry(&iter_dict)) {
		struct wpa_config_blob *blob;

		if (!wpa_dbus_dict_get_entry(&iter_dict, &entry)) {
			reply = wpas_dbus_new_invalid_opts_error(message,
								 NULL);
			break;
		}

		if (entry.type != DBUS_TYPE_ARRAY ||
		    entry.array_type != DBUS_TYPE_BYTE) {
			reply = wpas_dbus_new_invalid_opts_error(
				message, "Byte array expected.");
			break;
		}

		if ((entry.array_len <= 0) || (entry.array_len > 65536) ||
		    !strlen(entry.key)) {
			reply = wpas_dbus_new_invalid_opts_error(
				message, "Invalid array size.");
			break;
		}

		blob = os_zalloc(sizeof(*blob));
		if (blob == NULL) {
			reply = dbus_message_new_error(
				message, WPAS_ERROR_ADD_ERROR,
				"Not enough memory to add blob.");
			break;
		}
		blob->data = os_zalloc(entry.array_len);
		if (blob->data == NULL) {
			reply = dbus_message_new_error(
				message, WPAS_ERROR_ADD_ERROR,
				"Not enough memory to add blob data.");
			os_free(blob);
			break;
		}

		blob->name = os_strdup(entry.key);
		blob->len = entry.array_len;
		os_memcpy(blob->data, (u8 *) entry.bytearray_value,
				entry.array_len);
		if (blob->name == NULL) {
			wpa_config_free_blob(blob);
			reply = dbus_message_new_error(
				message, WPAS_ERROR_ADD_ERROR,
				"Error adding blob.");
			break;
		}

		/* Success */
		if (!wpa_config_remove_blob(wpa_s->conf, blob->name))
			wpas_notify_blob_removed(wpa_s, blob->name);
		wpa_config_set_blob(wpa_s->conf, blob);
		wpas_notify_blob_added(wpa_s, blob->name);

		wpa_dbus_dict_entry_clear(&entry);
	}
	wpa_dbus_dict_entry_clear(&entry);

	return reply ? reply : wpas_dbus_new_success_reply(message);
}


/**
 * wpas_dbus_iface_remove_blob - Remove named binary blobs
 * @message: Pointer to incoming dbus message
 * @wpa_s: %wpa_supplicant data structure
 * Returns: A dbus message containing a UINT32 indicating success (1) or
 *          failure (0)
 *
 * Asks wpa_supplicant to remove one or more previously stored binary blobs.
 */
DBusMessage * wpas_dbus_iface_remove_blobs(DBusMessage *message,
					   struct wpa_supplicant *wpa_s)
{
	DBusMessageIter iter, array;
	char *err_msg = NULL;

	dbus_message_iter_init(message, &iter);

	if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_ARRAY ||
	    dbus_message_iter_get_element_type(&iter) != DBUS_TYPE_STRING)
		return wpas_dbus_new_invalid_opts_error(message, NULL);

	dbus_message_iter_recurse(&iter, &array);
	while (dbus_message_iter_get_arg_type(&array) == DBUS_TYPE_STRING) {
		const char *name;

		dbus_message_iter_get_basic(&array, &name);
		if (!os_strlen(name))
			err_msg = "Invalid blob name.";
		else if (wpa_config_remove_blob(wpa_s->conf, name) != 0)
			err_msg = "Error removing blob.";
		else
			wpas_notify_blob_removed(wpa_s, name);
		dbus_message_iter_next(&array);
	}

	if (err_msg)
		return dbus_message_new_error(message, WPAS_ERROR_REMOVE_ERROR,
					      err_msg);

	return wpas_dbus_new_success_reply(message);
}

#endif /* CONFIG_NO_CONFIG_BLOBS */


/**
 * wpas_dbus_iface_flush - Clear BSS of old or all inactive entries
 * @message: Pointer to incoming dbus message
 * @wpa_s: %wpa_supplicant data structure
 * Returns: a dbus message containing a UINT32 indicating success (1) or
 *          failure (0), or returns a dbus error message with more information
 *
 * Handler function for "flush" method call. Handles requests for an
 * interface with an optional "age" parameter that specifies the minimum
 * age of a BSS to be flushed.
 */
DBusMessage * wpas_dbus_iface_flush(DBusMessage *message,
				    struct wpa_supplicant *wpa_s)
{
	int flush_age = 0;

	if (os_strlen(dbus_message_get_signature(message)) != 0 &&
	    !dbus_message_get_args(message, NULL,
				   DBUS_TYPE_INT32, &flush_age,
				   DBUS_TYPE_INVALID)) {
		return wpas_dbus_new_invalid_opts_error(message, NULL);
	}

	if (flush_age == 0)
		wpa_bss_flush(wpa_s);
	else
		wpa_bss_flush_by_age(wpa_s, flush_age);

	return wpas_dbus_new_success_reply(message);
}
