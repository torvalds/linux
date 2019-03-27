/*
 * WPA Supplicant / dbus-based control interface (P2P)
 * Copyright (c) 2011-2012, Intel Corporation
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"

#include "utils/includes.h"
#include "common.h"
#include "../config.h"
#include "../wpa_supplicant_i.h"
#include "../wps_supplicant.h"
#include "../notify.h"
#include "dbus_new_helpers.h"
#include "dbus_new.h"
#include "dbus_new_handlers.h"
#include "dbus_new_handlers_p2p.h"
#include "dbus_dict_helpers.h"
#include "p2p/p2p.h"
#include "common/ieee802_11_defs.h"
#include "ap/hostapd.h"
#include "ap/ap_config.h"
#include "ap/wps_hostapd.h"

#include "../p2p_supplicant.h"
#include "../wifi_display.h"


static int wpas_dbus_validate_dbus_ipaddr(struct wpa_dbus_dict_entry entry)
{
	if (entry.type != DBUS_TYPE_ARRAY ||
	    entry.array_type != DBUS_TYPE_BYTE ||
	    entry.array_len != 4)
		return 0;

	return 1;
}


/**
 * Parses out the mac address from the peer object path.
 * @peer_path - object path of the form
 *	/fi/w1/wpa_supplicant1/Interfaces/n/Peers/00112233445566 (no colons)
 * @addr - out param must be of ETH_ALEN size
 * Returns 0 if valid (including MAC), -1 otherwise
 */
static int parse_peer_object_path(const char *peer_path, u8 addr[ETH_ALEN])
{
	const char *p;

	if (!peer_path)
		return -1;
	p = os_strrchr(peer_path, '/');
	if (!p)
		return -1;
	p++;
	return hwaddr_compact_aton(p, addr);
}


/**
 * wpas_dbus_error_persistent_group_unknown - Return a new PersistentGroupUnknown
 * error message
 * @message: Pointer to incoming dbus message this error refers to
 * Returns: a dbus error message
 *
 * Convenience function to create and return an invalid persistent group error.
 */
static DBusMessage *
wpas_dbus_error_persistent_group_unknown(DBusMessage *message)
{
	return dbus_message_new_error(
		message, WPAS_DBUS_ERROR_NETWORK_UNKNOWN,
		"There is no such persistent group in this P2P device.");
}


DBusMessage * wpas_dbus_handler_p2p_find(DBusMessage *message,
					 struct wpa_supplicant *wpa_s)
{
	struct wpa_dbus_dict_entry entry;
	DBusMessage *reply = NULL;
	DBusMessageIter iter;
	DBusMessageIter iter_dict;
	unsigned int timeout = 0;
	enum p2p_discovery_type type = P2P_FIND_START_WITH_FULL;
	int num_req_dev_types = 0;
	unsigned int i;
	u8 *req_dev_types = NULL;
	unsigned int freq = 0;

	dbus_message_iter_init(message, &iter);
	entry.key = NULL;

	if (!wpa_dbus_dict_open_read(&iter, &iter_dict, NULL))
		goto error;

	while (wpa_dbus_dict_has_dict_entry(&iter_dict)) {
		if (!wpa_dbus_dict_get_entry(&iter_dict, &entry))
			goto error;

		if (os_strcmp(entry.key, "Timeout") == 0 &&
		    entry.type == DBUS_TYPE_INT32) {
			timeout = entry.uint32_value;
		} else if (os_strcmp(entry.key, "RequestedDeviceTypes") == 0) {
			if (entry.type != DBUS_TYPE_ARRAY ||
			    entry.array_type != WPAS_DBUS_TYPE_BINARRAY)
				goto error_clear;

			os_free(req_dev_types);
			req_dev_types =
				os_malloc(WPS_DEV_TYPE_LEN * entry.array_len);
			if (!req_dev_types)
				goto error_clear;

			for (i = 0; i < entry.array_len; i++) {
				if (wpabuf_len(entry.binarray_value[i]) !=
				    WPS_DEV_TYPE_LEN)
					goto error_clear;
				os_memcpy(req_dev_types + i * WPS_DEV_TYPE_LEN,
					  wpabuf_head(entry.binarray_value[i]),
					  WPS_DEV_TYPE_LEN);
			}
			num_req_dev_types = entry.array_len;
		} else if (os_strcmp(entry.key, "DiscoveryType") == 0 &&
			   entry.type == DBUS_TYPE_STRING) {
			if (os_strcmp(entry.str_value, "start_with_full") == 0)
				type = P2P_FIND_START_WITH_FULL;
			else if (os_strcmp(entry.str_value, "social") == 0)
				type = P2P_FIND_ONLY_SOCIAL;
			else if (os_strcmp(entry.str_value, "progressive") == 0)
				type = P2P_FIND_PROGRESSIVE;
			else
				goto error_clear;
		} else if (os_strcmp(entry.key, "freq") == 0 &&
			   (entry.type == DBUS_TYPE_INT32 ||
			    entry.type == DBUS_TYPE_UINT32)) {
			freq = entry.uint32_value;
		} else
			goto error_clear;
		wpa_dbus_dict_entry_clear(&entry);
	}

	wpa_s = wpa_s->global->p2p_init_wpa_s;

	if (wpas_p2p_find(wpa_s, timeout, type, num_req_dev_types,
			  req_dev_types, NULL, 0, 0, NULL, freq))
		reply = wpas_dbus_error_unknown_error(
			message, "Could not start P2P find");

	os_free(req_dev_types);
	return reply;

error_clear:
	wpa_dbus_dict_entry_clear(&entry);
error:
	os_free(req_dev_types);
	reply = wpas_dbus_error_invalid_args(message, entry.key);
	return reply;
}


DBusMessage * wpas_dbus_handler_p2p_stop_find(DBusMessage *message,
					      struct wpa_supplicant *wpa_s)
{
	wpas_p2p_stop_find(wpa_s->global->p2p_init_wpa_s);
	return NULL;
}


DBusMessage * wpas_dbus_handler_p2p_rejectpeer(DBusMessage *message,
					       struct wpa_supplicant *wpa_s)
{
	DBusMessageIter iter;
	char *peer_object_path = NULL;
	u8 peer_addr[ETH_ALEN];

	dbus_message_iter_init(message, &iter);
	dbus_message_iter_get_basic(&iter, &peer_object_path);

	if (parse_peer_object_path(peer_object_path, peer_addr) < 0)
		return wpas_dbus_error_invalid_args(message, NULL);

	wpa_s = wpa_s->global->p2p_init_wpa_s;

	if (wpas_p2p_reject(wpa_s, peer_addr) < 0)
		return wpas_dbus_error_unknown_error(message,
				"Failed to call wpas_p2p_reject method.");

	return NULL;
}


DBusMessage * wpas_dbus_handler_p2p_listen(DBusMessage *message,
					   struct wpa_supplicant *wpa_s)
{
	dbus_int32_t timeout = 0;

	if (!dbus_message_get_args(message, NULL, DBUS_TYPE_INT32, &timeout,
				   DBUS_TYPE_INVALID))
		return wpas_dbus_error_no_memory(message);

	wpa_s = wpa_s->global->p2p_init_wpa_s;

	if (wpas_p2p_listen(wpa_s, (unsigned int) timeout)) {
		return dbus_message_new_error(message,
					      WPAS_DBUS_ERROR_UNKNOWN_ERROR,
					      "Could not start P2P listen");
	}

	return NULL;
}


DBusMessage * wpas_dbus_handler_p2p_extendedlisten(
	DBusMessage *message, struct wpa_supplicant *wpa_s)
{
	unsigned int period = 0, interval = 0;
	struct wpa_dbus_dict_entry entry;
	DBusMessageIter iter;
	DBusMessageIter iter_dict;

	dbus_message_iter_init(message, &iter);
	entry.key = NULL;

	if (!wpa_dbus_dict_open_read(&iter, &iter_dict, NULL))
		goto error;

	while (wpa_dbus_dict_has_dict_entry(&iter_dict)) {
		if (!wpa_dbus_dict_get_entry(&iter_dict, &entry))
			goto error;

		if (os_strcmp(entry.key, "period") == 0 &&
		    entry.type == DBUS_TYPE_INT32)
			period = entry.uint32_value;
		else if (os_strcmp(entry.key, "interval") == 0 &&
			 entry.type == DBUS_TYPE_INT32)
			interval = entry.uint32_value;
		else
			goto error_clear;
		wpa_dbus_dict_entry_clear(&entry);
	}

	wpa_s = wpa_s->global->p2p_init_wpa_s;

	if (wpas_p2p_ext_listen(wpa_s, period, interval))
		return wpas_dbus_error_unknown_error(
			message, "failed to initiate a p2p_ext_listen.");

	return NULL;

error_clear:
	wpa_dbus_dict_entry_clear(&entry);
error:
	return wpas_dbus_error_invalid_args(message, entry.key);
}


DBusMessage * wpas_dbus_handler_p2p_presence_request(
	DBusMessage *message, struct wpa_supplicant *wpa_s)
{
	unsigned int dur1 = 0, int1 = 0, dur2 = 0, int2 = 0;
	struct wpa_dbus_dict_entry entry;
	DBusMessageIter iter;
	DBusMessageIter iter_dict;

	dbus_message_iter_init(message, &iter);
	entry.key = NULL;

	if (!wpa_dbus_dict_open_read(&iter, &iter_dict, NULL))
		goto error;

	while (wpa_dbus_dict_has_dict_entry(&iter_dict)) {
		if (!wpa_dbus_dict_get_entry(&iter_dict, &entry))
			goto error;

		if (os_strcmp(entry.key, "duration1") == 0 &&
		    entry.type == DBUS_TYPE_INT32)
			dur1 = entry.uint32_value;
		else if (os_strcmp(entry.key, "interval1") == 0 &&
			 entry.type == DBUS_TYPE_INT32)
			int1 = entry.uint32_value;
		else if (os_strcmp(entry.key, "duration2") == 0 &&
			 entry.type == DBUS_TYPE_INT32)
			dur2 = entry.uint32_value;
		else if (os_strcmp(entry.key, "interval2") == 0 &&
			 entry.type == DBUS_TYPE_INT32)
			int2 = entry.uint32_value;
		else
			goto error_clear;

		wpa_dbus_dict_entry_clear(&entry);
	}

	if (wpas_p2p_presence_req(wpa_s, dur1, int1, dur2, int2) < 0)
		return wpas_dbus_error_unknown_error(message,
				"Failed to invoke presence request.");

	return NULL;

error_clear:
	wpa_dbus_dict_entry_clear(&entry);
error:
	return wpas_dbus_error_invalid_args(message, entry.key);
}


DBusMessage * wpas_dbus_handler_p2p_group_add(DBusMessage *message,
					      struct wpa_supplicant *wpa_s)
{
	DBusMessageIter iter_dict;
	DBusMessage *reply = NULL;
	DBusMessageIter iter;
	struct wpa_dbus_dict_entry entry;
	char *pg_object_path = NULL;
	int persistent_group = 0;
	int freq = 0;
	char *iface = NULL;
	unsigned int group_id = 0;
	struct wpa_ssid *ssid;

	dbus_message_iter_init(message, &iter);

	if (!wpa_dbus_dict_open_read(&iter, &iter_dict, NULL))
		goto inv_args;

	while (wpa_dbus_dict_has_dict_entry(&iter_dict)) {
		if (!wpa_dbus_dict_get_entry(&iter_dict, &entry))
			goto inv_args;

		if (os_strcmp(entry.key, "persistent") == 0 &&
		    entry.type == DBUS_TYPE_BOOLEAN) {
			persistent_group = entry.bool_value;
		} else if (os_strcmp(entry.key, "frequency") == 0 &&
			   entry.type == DBUS_TYPE_INT32) {
			freq = entry.int32_value;
			if (freq <= 0)
				goto inv_args_clear;
		} else if (os_strcmp(entry.key, "persistent_group_object") ==
			   0 &&
			   entry.type == DBUS_TYPE_OBJECT_PATH)
			pg_object_path = os_strdup(entry.str_value);
		else
			goto inv_args_clear;

		wpa_dbus_dict_entry_clear(&entry);
	}

	wpa_s = wpa_s->global->p2p_init_wpa_s;

	if (pg_object_path != NULL) {
		char *net_id_str;

		/*
		 * A persistent group Object Path is defined meaning we want
		 * to re-invoke a persistent group.
		 */

		iface = wpas_dbus_new_decompose_object_path(
			pg_object_path, WPAS_DBUS_NEW_PERSISTENT_GROUPS_PART,
			&net_id_str);
		if (iface == NULL || net_id_str == NULL ||
		    !wpa_s->parent->dbus_new_path ||
		    os_strcmp(iface, wpa_s->parent->dbus_new_path) != 0) {
			reply =
			    wpas_dbus_error_invalid_args(message,
							 pg_object_path);
			goto out;
		}

		group_id = strtoul(net_id_str, NULL, 10);
		if (errno == EINVAL) {
			reply = wpas_dbus_error_invalid_args(
						message, pg_object_path);
			goto out;
		}

		/* Get the SSID structure from the persistent group id */
		ssid = wpa_config_get_network(wpa_s->conf, group_id);
		if (ssid == NULL || ssid->disabled != 2)
			goto inv_args;

		if (wpas_p2p_group_add_persistent(wpa_s, ssid, 0, freq, 0, 0, 0,
						  0, 0, NULL, 0, 0)) {
			reply = wpas_dbus_error_unknown_error(
				message,
				"Failed to reinvoke a persistent group");
			goto out;
		}
	} else if (wpas_p2p_group_add(wpa_s, persistent_group, freq, 0, 0, 0,
				      0))
		goto inv_args;

out:
	os_free(pg_object_path);
	os_free(iface);
	return reply;
inv_args_clear:
	wpa_dbus_dict_entry_clear(&entry);
inv_args:
	reply = wpas_dbus_error_invalid_args(message, NULL);
	goto out;
}


DBusMessage * wpas_dbus_handler_p2p_disconnect(DBusMessage *message,
					       struct wpa_supplicant *wpa_s)
{
	if (wpas_p2p_disconnect(wpa_s))
		return wpas_dbus_error_unknown_error(message,
						"failed to disconnect");

	return NULL;
}


static dbus_bool_t wpa_dbus_p2p_check_enabled(struct wpa_supplicant *wpa_s,
					      DBusMessage *message,
					      DBusMessage **out_reply,
					      DBusError *error)
{
	/* Return an error message or an error if P2P isn't available */
	if (wpa_s->global->p2p_disabled || wpa_s->global->p2p == NULL) {
		if (out_reply) {
			*out_reply = dbus_message_new_error(
				message, DBUS_ERROR_FAILED,
				"P2P is not available for this interface");
		}
		dbus_set_error_const(error, DBUS_ERROR_FAILED,
				     "P2P is not available for this interface");
		return FALSE;
	}
	return TRUE;
}


DBusMessage * wpas_dbus_handler_p2p_remove_client(DBusMessage *message,
						  struct wpa_supplicant *wpa_s)
{
	DBusMessageIter iter_dict;
	DBusMessage *reply = NULL;
	DBusMessageIter iter;
	struct wpa_dbus_dict_entry entry;
	char *peer_object_path = NULL;
	char *interface_addr = NULL;
	u8 peer_addr[ETH_ALEN];

	if (!wpa_dbus_p2p_check_enabled(wpa_s, message, &reply, NULL))
		return reply;

	dbus_message_iter_init(message, &iter);

	if (!wpa_dbus_dict_open_read(&iter, &iter_dict, NULL))
		goto err;

	while (wpa_dbus_dict_has_dict_entry(&iter_dict)) {
		if (!wpa_dbus_dict_get_entry(&iter_dict, &entry))
			goto err;

		if (os_strcmp(entry.key, "peer") == 0 &&
		    entry.type == DBUS_TYPE_OBJECT_PATH) {
			os_free(peer_object_path);
			peer_object_path = os_strdup(entry.str_value);
			wpa_dbus_dict_entry_clear(&entry);
		} else if (os_strcmp(entry.key, "iface") == 0 &&
			   entry.type == DBUS_TYPE_STRING) {
			os_free(interface_addr);
			interface_addr = os_strdup(entry.str_value);
			wpa_dbus_dict_entry_clear(&entry);
		} else {
			wpa_dbus_dict_entry_clear(&entry);
			goto err;
		}
	}

	if ((!peer_object_path && !interface_addr) ||
	    (peer_object_path &&
	     (parse_peer_object_path(peer_object_path, peer_addr) < 0 ||
	      !p2p_peer_known(wpa_s->global->p2p, peer_addr))) ||
	    (interface_addr && hwaddr_aton(interface_addr, peer_addr) < 0))
		goto err;

	wpas_p2p_remove_client(wpa_s, peer_addr, interface_addr != NULL);
	reply = NULL;
out:
	os_free(peer_object_path);
	os_free(interface_addr);
	return reply;
err:
	reply = wpas_dbus_error_invalid_args(message, "Invalid address format");
	goto out;
}


DBusMessage * wpas_dbus_handler_p2p_flush(DBusMessage *message,
					  struct wpa_supplicant *wpa_s)
{
	DBusMessage *reply = NULL;

	if (!wpa_dbus_p2p_check_enabled(wpa_s, message, &reply, NULL))
		return reply;

	wpa_s = wpa_s->global->p2p_init_wpa_s;

	os_memset(wpa_s->p2p_auth_invite, 0, ETH_ALEN);
	wpa_s->force_long_sd = 0;
	p2p_flush(wpa_s->global->p2p);

	return NULL;
}


DBusMessage * wpas_dbus_handler_p2p_connect(DBusMessage *message,
					    struct wpa_supplicant *wpa_s)
{
	DBusMessageIter iter_dict;
	DBusMessage *reply = NULL;
	DBusMessageIter iter;
	struct wpa_dbus_dict_entry entry;
	char *peer_object_path = NULL;
	int persistent_group = 0;
	int join = 0;
	int authorize_only = 0;
	int go_intent = -1;
	int freq = 0;
	u8 addr[ETH_ALEN];
	char *pin = NULL;
	enum p2p_wps_method wps_method = WPS_NOT_READY;
	int new_pin;
	char *err_msg = NULL;
	char *iface = NULL;

	if (!wpa_dbus_p2p_check_enabled(wpa_s, message, &reply, NULL))
		return reply;

	dbus_message_iter_init(message, &iter);

	if (!wpa_dbus_dict_open_read(&iter, &iter_dict, NULL))
		goto inv_args;

	while (wpa_dbus_dict_has_dict_entry(&iter_dict)) {
		if (!wpa_dbus_dict_get_entry(&iter_dict, &entry))
			goto inv_args;

		if (os_strcmp(entry.key, "peer") == 0 &&
		    entry.type == DBUS_TYPE_OBJECT_PATH) {
			peer_object_path = os_strdup(entry.str_value);
		} else if (os_strcmp(entry.key, "persistent") == 0 &&
			   entry.type == DBUS_TYPE_BOOLEAN) {
			persistent_group = entry.bool_value;
		} else if (os_strcmp(entry.key, "join") == 0 &&
			   entry.type == DBUS_TYPE_BOOLEAN) {
			join = entry.bool_value;
		} else if (os_strcmp(entry.key, "authorize_only") == 0 &&
			   entry.type == DBUS_TYPE_BOOLEAN) {
			authorize_only = entry.bool_value;
		} else if (os_strcmp(entry.key, "frequency") == 0 &&
			   entry.type == DBUS_TYPE_INT32) {
			freq = entry.int32_value;
			if (freq <= 0)
				goto inv_args_clear;
		} else if (os_strcmp(entry.key, "go_intent") == 0 &&
			   entry.type == DBUS_TYPE_INT32) {
			go_intent = entry.int32_value;
			if ((go_intent < 0) || (go_intent > 15))
				goto inv_args_clear;
		} else if (os_strcmp(entry.key, "wps_method") == 0 &&
			   entry.type == DBUS_TYPE_STRING) {
			if (os_strcmp(entry.str_value, "pbc") == 0)
				wps_method = WPS_PBC;
			else if (os_strcmp(entry.str_value, "pin") == 0)
				wps_method = WPS_PIN_DISPLAY;
			else if (os_strcmp(entry.str_value, "display") == 0)
				wps_method = WPS_PIN_DISPLAY;
			else if (os_strcmp(entry.str_value, "keypad") == 0)
				wps_method = WPS_PIN_KEYPAD;
			else
				goto inv_args_clear;
		} else if (os_strcmp(entry.key, "pin") == 0 &&
			   entry.type == DBUS_TYPE_STRING) {
			pin = os_strdup(entry.str_value);
		} else
			goto inv_args_clear;

		wpa_dbus_dict_entry_clear(&entry);
	}

	if (wps_method == WPS_NOT_READY ||
	    parse_peer_object_path(peer_object_path, addr) < 0 ||
	    !p2p_peer_known(wpa_s->global->p2p, addr))
		goto inv_args;

	/*
	 * Validate the wps_method specified and the pin value.
	 */
	if ((!pin || !pin[0]) && wps_method == WPS_PIN_KEYPAD)
		goto inv_args;

	wpa_s = wpa_s->global->p2p_init_wpa_s;

	new_pin = wpas_p2p_connect(wpa_s, addr, pin, wps_method,
				   persistent_group, 0, join, authorize_only,
				   go_intent, freq, 0, -1, 0, 0, 0, 0, NULL, 0);

	if (new_pin >= 0) {
		char npin[9];
		char *generated_pin;

		os_snprintf(npin, sizeof(npin), "%08d", new_pin);
		generated_pin = npin;
		reply = dbus_message_new_method_return(message);
		dbus_message_append_args(reply, DBUS_TYPE_STRING,
					 &generated_pin, DBUS_TYPE_INVALID);
	} else {
		switch (new_pin) {
		case -2:
			err_msg =
				"connect failed due to channel unavailability.";
			iface = WPAS_DBUS_ERROR_CONNECT_CHANNEL_UNAVAILABLE;
			break;

		case -3:
			err_msg = "connect failed due to unsupported channel.";
			iface = WPAS_DBUS_ERROR_CONNECT_CHANNEL_UNSUPPORTED;
			break;

		default:
			err_msg = "connect failed due to unspecified error.";
			iface = WPAS_DBUS_ERROR_CONNECT_UNSPECIFIED_ERROR;
			break;
		}

		/*
		 * TODO:
		 * Do we need specialized errors corresponding to above
		 * error conditions as against just returning a different
		 * error message?
		 */
		reply = dbus_message_new_error(message, iface, err_msg);
	}

out:
	os_free(peer_object_path);
	os_free(pin);
	return reply;
inv_args_clear:
	wpa_dbus_dict_entry_clear(&entry);
inv_args:
	reply = wpas_dbus_error_invalid_args(message, NULL);
	goto out;
}


/**
 * wpas_dbus_handler_p2p_cancel - Cancel P2P group formation
 * @message: Pointer to incoming dbus message
 * @wpa_s: %wpa_supplicant data structure
 * Returns: NULL on success or DBus error on failure
 *
 * Handler for "Cancel" method call. Returns NULL if P2P cancel succeeds or DBus
 * error on P2P cancel failure
 */
DBusMessage * wpas_dbus_handler_p2p_cancel(DBusMessage *message,
					   struct wpa_supplicant *wpa_s)
{
	if (wpas_p2p_cancel(wpa_s))
		return wpas_dbus_error_unknown_error(message,
						     "P2P cancel failed");

	return NULL;
}


DBusMessage * wpas_dbus_handler_p2p_invite(DBusMessage *message,
					   struct wpa_supplicant *wpa_s)
{
	DBusMessageIter iter_dict;
	DBusMessage *reply = NULL;
	DBusMessageIter iter;
	struct wpa_dbus_dict_entry entry;
	char *peer_object_path = NULL;
	char *pg_object_path = NULL;
	char *iface = NULL;
	u8 peer_addr[ETH_ALEN];
	unsigned int group_id = 0;
	int persistent = 0;
	struct wpa_ssid *ssid;

	if (!wpa_dbus_p2p_check_enabled(wpa_s, message, &reply, NULL))
		return reply;

	dbus_message_iter_init(message, &iter);

	if (!wpa_dbus_dict_open_read(&iter, &iter_dict, NULL))
		goto err;

	while (wpa_dbus_dict_has_dict_entry(&iter_dict)) {
		if (!wpa_dbus_dict_get_entry(&iter_dict, &entry))
			goto err;

		if (os_strcmp(entry.key, "peer") == 0 &&
		    entry.type == DBUS_TYPE_OBJECT_PATH) {
			peer_object_path = os_strdup(entry.str_value);
			wpa_dbus_dict_entry_clear(&entry);
		} else if (os_strcmp(entry.key, "persistent_group_object") ==
			   0 &&
			   entry.type == DBUS_TYPE_OBJECT_PATH) {
			pg_object_path = os_strdup(entry.str_value);
			persistent = 1;
			wpa_dbus_dict_entry_clear(&entry);
		} else {
			wpa_dbus_dict_entry_clear(&entry);
			goto err;
		}
	}

	if (parse_peer_object_path(peer_object_path, peer_addr) < 0 ||
	    !p2p_peer_known(wpa_s->global->p2p, peer_addr))
		goto err;

	wpa_s = wpa_s->global->p2p_init_wpa_s;

	if (persistent) {
		char *net_id_str;
		/*
		 * A group ID is defined meaning we want to re-invoke a
		 * persistent group
		 */

		iface = wpas_dbus_new_decompose_object_path(
			pg_object_path,
			WPAS_DBUS_NEW_PERSISTENT_GROUPS_PART,
			&net_id_str);
		if (iface == NULL || net_id_str == NULL ||
		    !wpa_s->parent->dbus_new_path ||
		    os_strcmp(iface, wpa_s->parent->dbus_new_path) != 0) {
			reply = wpas_dbus_error_invalid_args(message,
							     pg_object_path);
			goto out;
		}

		group_id = strtoul(net_id_str, NULL, 10);
		if (errno == EINVAL) {
			reply = wpas_dbus_error_invalid_args(
				message, pg_object_path);
			goto out;
		}

		/* Get the SSID structure from the persistent group id */
		ssid = wpa_config_get_network(wpa_s->conf, group_id);
		if (ssid == NULL || ssid->disabled != 2)
			goto err;

		if (wpas_p2p_invite(wpa_s, peer_addr, ssid, NULL, 0, 0, 0, 0, 0,
				    0) < 0) {
			reply = wpas_dbus_error_unknown_error(
				message,
				"Failed to reinvoke a persistent group");
			goto out;
		}
	} else {
		/*
		 * No group ID means propose to a peer to join my active group
		 */
		if (wpas_p2p_invite_group(wpa_s, wpa_s->ifname,
					  peer_addr, NULL)) {
			reply = wpas_dbus_error_unknown_error(
				message, "Failed to join to an active group");
			goto out;
		}
	}

out:
	os_free(iface);
	os_free(pg_object_path);
	os_free(peer_object_path);
	return reply;

err:
	reply = wpas_dbus_error_invalid_args(message, NULL);
	goto out;
}


DBusMessage * wpas_dbus_handler_p2p_prov_disc_req(DBusMessage *message,
						  struct wpa_supplicant *wpa_s)
{
	DBusMessageIter iter;
	char *peer_object_path = NULL;
	char *config_method = NULL;
	u8 peer_addr[ETH_ALEN];

	dbus_message_iter_init(message, &iter);
	dbus_message_iter_get_basic(&iter, &peer_object_path);

	if (parse_peer_object_path(peer_object_path, peer_addr) < 0)
		return wpas_dbus_error_invalid_args(message, NULL);

	dbus_message_iter_next(&iter);
	dbus_message_iter_get_basic(&iter, &config_method);

	/*
	 * Validation checks on config_method are being duplicated here
	 * to be able to return invalid args reply since the error code
	 * from p2p module are not granular enough (yet).
	 */
	if (os_strcmp(config_method, "display") &&
	    os_strcmp(config_method, "keypad") &&
	    os_strcmp(config_method, "pbc") &&
	    os_strcmp(config_method, "pushbutton"))
		return wpas_dbus_error_invalid_args(message, NULL);

	wpa_s = wpa_s->global->p2p_init_wpa_s;

	if (wpas_p2p_prov_disc(wpa_s, peer_addr, config_method,
			       WPAS_P2P_PD_FOR_GO_NEG, NULL) < 0)
		return wpas_dbus_error_unknown_error(message,
				"Failed to send provision discovery request");

	return NULL;
}


/*
 * P2P Device property accessor methods.
 */

dbus_bool_t wpas_dbus_getter_p2p_device_config(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct wpa_supplicant *wpa_s = user_data;
	DBusMessageIter variant_iter, dict_iter;
	DBusMessageIter iter_secdev_dict_entry, iter_secdev_dict_val,
		iter_secdev_dict_array;
	const char *dev_name;
	int num_vendor_extensions = 0;
	int i;
	const struct wpabuf *vendor_ext[P2P_MAX_WPS_VENDOR_EXT];

	if (!wpa_dbus_p2p_check_enabled(wpa_s, NULL, NULL, error))
		return FALSE;

	wpa_s = wpa_s->global->p2p_init_wpa_s;

	if (!dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT,
					      "a{sv}", &variant_iter) ||
	    !wpa_dbus_dict_open_write(&variant_iter, &dict_iter))
		goto err_no_mem;

	/* DeviceName */
	dev_name = wpa_s->conf->device_name;
	if (dev_name &&
	    !wpa_dbus_dict_append_string(&dict_iter, "DeviceName", dev_name))
		goto err_no_mem;

	/* Primary device type */
	if (!wpa_dbus_dict_append_byte_array(&dict_iter, "PrimaryDeviceType",
					     (char *) wpa_s->conf->device_type,
					     WPS_DEV_TYPE_LEN))
		goto err_no_mem;

	/* Secondary device types */
	if (wpa_s->conf->num_sec_device_types) {
		if (!wpa_dbus_dict_begin_array(&dict_iter,
					       "SecondaryDeviceTypes",
					       DBUS_TYPE_ARRAY_AS_STRING
					       DBUS_TYPE_BYTE_AS_STRING,
					       &iter_secdev_dict_entry,
					       &iter_secdev_dict_val,
					       &iter_secdev_dict_array))
			goto err_no_mem;

		for (i = 0; i < wpa_s->conf->num_sec_device_types; i++)
			wpa_dbus_dict_bin_array_add_element(
				&iter_secdev_dict_array,
				wpa_s->conf->sec_device_type[i],
				WPS_DEV_TYPE_LEN);

		if (!wpa_dbus_dict_end_array(&dict_iter,
					     &iter_secdev_dict_entry,
					     &iter_secdev_dict_val,
					     &iter_secdev_dict_array))
			goto err_no_mem;
	}

	/* GO IP address */
	if (WPA_GET_BE32(wpa_s->conf->ip_addr_go) &&
	    !wpa_dbus_dict_append_byte_array(&dict_iter, "IpAddrGo",
					     (char *) wpa_s->conf->ip_addr_go,
					     4))
		goto err_no_mem;

	/* IP address mask */
	if (WPA_GET_BE32(wpa_s->conf->ip_addr_mask) &&
	    !wpa_dbus_dict_append_byte_array(&dict_iter, "IpAddrMask",
					     (char *) wpa_s->conf->ip_addr_mask,
					     4))
		goto err_no_mem;

	/* IP address start */
	if (WPA_GET_BE32(wpa_s->conf->ip_addr_start) &&
	    !wpa_dbus_dict_append_byte_array(&dict_iter, "IpAddrStart",
					     (char *)
					     wpa_s->conf->ip_addr_start,
					     4))
		goto err_no_mem;

	/* IP address end */
	if (WPA_GET_BE32(wpa_s->conf->ip_addr_end) &&
	    !wpa_dbus_dict_append_byte_array(&dict_iter, "IpAddrEnd",
					     (char *) wpa_s->conf->ip_addr_end,
					     4))
		goto err_no_mem;

	/* Vendor Extensions */
	for (i = 0; i < P2P_MAX_WPS_VENDOR_EXT; i++) {
		if (wpa_s->conf->wps_vendor_ext[i] == NULL)
			continue;
		vendor_ext[num_vendor_extensions++] =
			wpa_s->conf->wps_vendor_ext[i];
	}

	if ((num_vendor_extensions &&
	     !wpa_dbus_dict_append_wpabuf_array(&dict_iter,
						"VendorExtension",
						vendor_ext,
						num_vendor_extensions)) ||
	    !wpa_dbus_dict_append_uint32(&dict_iter, "GOIntent",
					 wpa_s->conf->p2p_go_intent) ||
	    !wpa_dbus_dict_append_bool(&dict_iter, "PersistentReconnect",
				       wpa_s->conf->persistent_reconnect) ||
	    !wpa_dbus_dict_append_uint32(&dict_iter, "ListenRegClass",
					 wpa_s->conf->p2p_listen_reg_class) ||
	    !wpa_dbus_dict_append_uint32(&dict_iter, "ListenChannel",
					 wpa_s->conf->p2p_listen_channel) ||
	    !wpa_dbus_dict_append_uint32(&dict_iter, "OperRegClass",
					 wpa_s->conf->p2p_oper_reg_class) ||
	    !wpa_dbus_dict_append_uint32(&dict_iter, "OperChannel",
					 wpa_s->conf->p2p_oper_channel) ||
	    (wpa_s->conf->p2p_ssid_postfix &&
	     !wpa_dbus_dict_append_string(&dict_iter, "SsidPostfix",
					  wpa_s->conf->p2p_ssid_postfix)) ||
	    !wpa_dbus_dict_append_bool(&dict_iter, "IntraBss",
				       wpa_s->conf->p2p_intra_bss) ||
	    !wpa_dbus_dict_append_uint32(&dict_iter, "GroupIdle",
					 wpa_s->conf->p2p_group_idle) ||
	    !wpa_dbus_dict_append_uint32(&dict_iter, "disassoc_low_ack",
					 wpa_s->conf->disassoc_low_ack) ||
	    !wpa_dbus_dict_append_bool(&dict_iter, "NoGroupIface",
				       wpa_s->conf->p2p_no_group_iface) ||
	    !wpa_dbus_dict_append_uint32(&dict_iter, "p2p_search_delay",
					 wpa_s->conf->p2p_search_delay) ||
	    !wpa_dbus_dict_close_write(&variant_iter, &dict_iter) ||
	    !dbus_message_iter_close_container(iter, &variant_iter))
		goto err_no_mem;

	return TRUE;

err_no_mem:
	dbus_set_error_const(error, DBUS_ERROR_NO_MEMORY, "no memory");
	return FALSE;
}


dbus_bool_t wpas_dbus_setter_p2p_device_config(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct wpa_supplicant *wpa_s = user_data;
	DBusMessageIter variant_iter, iter_dict;
	struct wpa_dbus_dict_entry entry = {.type = DBUS_TYPE_STRING };
	unsigned int i;

	if (!wpa_dbus_p2p_check_enabled(wpa_s, NULL, NULL, error))
		return FALSE;

	wpa_s = wpa_s->global->p2p_init_wpa_s;

	dbus_message_iter_recurse(iter, &variant_iter);
	if (!wpa_dbus_dict_open_read(&variant_iter, &iter_dict, error))
		return FALSE;

	while (wpa_dbus_dict_has_dict_entry(&iter_dict)) {
		if (!wpa_dbus_dict_get_entry(&iter_dict, &entry)) {
			dbus_set_error_const(error, DBUS_ERROR_INVALID_ARGS,
					     "invalid message format");
			return FALSE;
		}

		if (os_strcmp(entry.key, "DeviceName") == 0) {
			char *devname;

			if (entry.type != DBUS_TYPE_STRING ||
			    os_strlen(entry.str_value) > WPS_DEV_NAME_MAX_LEN)
				goto error;

			devname = os_strdup(entry.str_value);
			if (devname == NULL)
				goto err_no_mem_clear;

			os_free(wpa_s->conf->device_name);
			wpa_s->conf->device_name = devname;

			wpa_s->conf->changed_parameters |=
				CFG_CHANGED_DEVICE_NAME;
		} else if (os_strcmp(entry.key, "PrimaryDeviceType") == 0) {
			if (entry.type != DBUS_TYPE_ARRAY ||
			    entry.array_type != DBUS_TYPE_BYTE ||
			    entry.array_len != WPS_DEV_TYPE_LEN)
				goto error;

			os_memcpy(wpa_s->conf->device_type,
				  entry.bytearray_value,
				  WPS_DEV_TYPE_LEN);
			wpa_s->conf->changed_parameters |=
				CFG_CHANGED_DEVICE_TYPE;
		} else if (os_strcmp(entry.key, "SecondaryDeviceTypes") == 0) {
			if (entry.type != DBUS_TYPE_ARRAY ||
			    entry.array_type != WPAS_DBUS_TYPE_BINARRAY ||
			    entry.array_len > MAX_SEC_DEVICE_TYPES)
				goto error;

			for (i = 0; i < entry.array_len; i++)
				if (wpabuf_len(entry.binarray_value[i]) !=
				    WPS_DEV_TYPE_LEN)
					goto err_no_mem_clear;
			for (i = 0; i < entry.array_len; i++)
				os_memcpy(wpa_s->conf->sec_device_type[i],
					  wpabuf_head(entry.binarray_value[i]),
					  WPS_DEV_TYPE_LEN);
			wpa_s->conf->num_sec_device_types = entry.array_len;
			wpa_s->conf->changed_parameters |=
					CFG_CHANGED_SEC_DEVICE_TYPE;
		} else if (os_strcmp(entry.key, "VendorExtension") == 0) {
			if (entry.type != DBUS_TYPE_ARRAY ||
			    entry.array_type != WPAS_DBUS_TYPE_BINARRAY ||
			    (entry.array_len > P2P_MAX_WPS_VENDOR_EXT))
				goto error;

			wpa_s->conf->changed_parameters |=
				CFG_CHANGED_VENDOR_EXTENSION;

			for (i = 0; i < P2P_MAX_WPS_VENDOR_EXT; i++) {
				wpabuf_free(wpa_s->conf->wps_vendor_ext[i]);
				if (i < entry.array_len) {
					wpa_s->conf->wps_vendor_ext[i] =
						entry.binarray_value[i];
					entry.binarray_value[i] = NULL;
				} else
					wpa_s->conf->wps_vendor_ext[i] = NULL;
			}
		} else if (os_strcmp(entry.key, "GOIntent") == 0 &&
			   entry.type == DBUS_TYPE_UINT32 &&
			   (entry.uint32_value <= 15))
			wpa_s->conf->p2p_go_intent = entry.uint32_value;
		else if (os_strcmp(entry.key, "PersistentReconnect") == 0 &&
			 entry.type == DBUS_TYPE_BOOLEAN)
			wpa_s->conf->persistent_reconnect = entry.bool_value;
		else if (os_strcmp(entry.key, "ListenRegClass") == 0 &&
			 entry.type == DBUS_TYPE_UINT32) {
			wpa_s->conf->p2p_listen_reg_class = entry.uint32_value;
			wpa_s->conf->changed_parameters |=
				CFG_CHANGED_P2P_LISTEN_CHANNEL;
		} else if (os_strcmp(entry.key, "ListenChannel") == 0 &&
			   entry.type == DBUS_TYPE_UINT32) {
			wpa_s->conf->p2p_listen_channel = entry.uint32_value;
			wpa_s->conf->changed_parameters |=
				CFG_CHANGED_P2P_LISTEN_CHANNEL;
		} else if (os_strcmp(entry.key, "OperRegClass") == 0 &&
			   entry.type == DBUS_TYPE_UINT32) {
			wpa_s->conf->p2p_oper_reg_class = entry.uint32_value;
			wpa_s->conf->changed_parameters |=
				CFG_CHANGED_P2P_OPER_CHANNEL;
		} else if (os_strcmp(entry.key, "OperChannel") == 0 &&
			   entry.type == DBUS_TYPE_UINT32) {
			wpa_s->conf->p2p_oper_channel = entry.uint32_value;
			wpa_s->conf->changed_parameters |=
				CFG_CHANGED_P2P_OPER_CHANNEL;
		} else if (os_strcmp(entry.key, "SsidPostfix") == 0) {
			char *postfix;

			if (entry.type != DBUS_TYPE_STRING)
				goto error;

			postfix = os_strdup(entry.str_value);
			if (!postfix)
				goto err_no_mem_clear;

			os_free(wpa_s->conf->p2p_ssid_postfix);
			wpa_s->conf->p2p_ssid_postfix = postfix;

			wpa_s->conf->changed_parameters |=
					CFG_CHANGED_P2P_SSID_POSTFIX;
		} else if (os_strcmp(entry.key, "IntraBss") == 0 &&
			   entry.type == DBUS_TYPE_BOOLEAN) {
			wpa_s->conf->p2p_intra_bss = entry.bool_value;
			wpa_s->conf->changed_parameters |=
				CFG_CHANGED_P2P_INTRA_BSS;
		} else if (os_strcmp(entry.key, "IpAddrGo") == 0) {
			if (!wpas_dbus_validate_dbus_ipaddr(entry))
				goto error;
			os_memcpy(wpa_s->conf->ip_addr_go,
				  entry.bytearray_value, 4);
		} else if (os_strcmp(entry.key, "IpAddrMask") == 0) {
			if (!wpas_dbus_validate_dbus_ipaddr(entry))
				goto error;
			os_memcpy(wpa_s->conf->ip_addr_mask,
				  entry.bytearray_value, 4);
		} else if (os_strcmp(entry.key, "IpAddrStart") == 0) {
			if (!wpas_dbus_validate_dbus_ipaddr(entry))
				goto error;
			os_memcpy(wpa_s->conf->ip_addr_start,
				  entry.bytearray_value, 4);
		} else if (os_strcmp(entry.key, "IpAddrEnd") == 0) {
			if (!wpas_dbus_validate_dbus_ipaddr(entry))
				goto error;
			os_memcpy(wpa_s->conf->ip_addr_end,
				  entry.bytearray_value, 4);
		} else if (os_strcmp(entry.key, "GroupIdle") == 0 &&
			   entry.type == DBUS_TYPE_UINT32)
			wpa_s->conf->p2p_group_idle = entry.uint32_value;
		else if (os_strcmp(entry.key, "disassoc_low_ack") == 0 &&
			 entry.type == DBUS_TYPE_UINT32)
			wpa_s->conf->disassoc_low_ack = entry.uint32_value;
		else if (os_strcmp(entry.key, "NoGroupIface") == 0 &&
			 entry.type == DBUS_TYPE_BOOLEAN)
			wpa_s->conf->p2p_no_group_iface = entry.bool_value;
		else if (os_strcmp(entry.key, "p2p_search_delay") == 0 &&
			 entry.type == DBUS_TYPE_UINT32)
			wpa_s->conf->p2p_search_delay = entry.uint32_value;
		else
			goto error;

		wpa_dbus_dict_entry_clear(&entry);
	}

	if (wpa_s->conf->changed_parameters) {
		/* Some changed parameters requires to update config*/
		wpa_supplicant_update_config(wpa_s);
	}

	return TRUE;

 error:
	dbus_set_error_const(error, DBUS_ERROR_INVALID_ARGS,
			     "invalid message format");
	wpa_dbus_dict_entry_clear(&entry);
	return FALSE;

 err_no_mem_clear:
	dbus_set_error_const(error, DBUS_ERROR_NO_MEMORY, "no memory");
	wpa_dbus_dict_entry_clear(&entry);
	return FALSE;
}


dbus_bool_t wpas_dbus_getter_p2p_peers(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct wpa_supplicant *wpa_s = user_data;
	struct p2p_data *p2p = wpa_s->global->p2p;
	int next = 0, i = 0;
	int num = 0, out_of_mem = 0;
	const u8 *addr;
	const struct p2p_peer_info *peer_info = NULL;
	dbus_bool_t success = FALSE;

	struct dl_list peer_objpath_list;
	struct peer_objpath_node {
		struct dl_list list;
		char path[WPAS_DBUS_OBJECT_PATH_MAX];
	} *node, *tmp;

	char **peer_obj_paths = NULL;

	if (!wpa_dbus_p2p_check_enabled(wpa_s, NULL, NULL, error) ||
	    !wpa_s->parent->parent->dbus_new_path)
		return FALSE;

	dl_list_init(&peer_objpath_list);

	/* Get the first peer info */
	peer_info = p2p_get_peer_found(p2p, NULL, next);

	/* Get next and accumulate them */
	next = 1;
	while (peer_info != NULL) {
		node = os_zalloc(sizeof(struct peer_objpath_node));
		if (!node) {
			out_of_mem = 1;
			goto error;
		}

		addr = peer_info->p2p_device_addr;
		os_snprintf(node->path, WPAS_DBUS_OBJECT_PATH_MAX,
			    "%s/" WPAS_DBUS_NEW_P2P_PEERS_PART
			    "/" COMPACT_MACSTR,
			    wpa_s->parent->parent->dbus_new_path,
			    MAC2STR(addr));
		dl_list_add_tail(&peer_objpath_list, &node->list);
		num++;

		peer_info = p2p_get_peer_found(p2p, addr, next);
	}

	/*
	 * Now construct the peer object paths in a form suitable for
	 * array_property_getter helper below.
	 */
	peer_obj_paths = os_calloc(num, sizeof(char *));

	if (!peer_obj_paths) {
		out_of_mem = 1;
		goto error;
	}

	dl_list_for_each_safe(node, tmp, &peer_objpath_list,
			      struct peer_objpath_node, list)
		peer_obj_paths[i++] = node->path;

	success = wpas_dbus_simple_array_property_getter(iter,
							 DBUS_TYPE_OBJECT_PATH,
							 peer_obj_paths, num,
							 error);

error:
	if (peer_obj_paths)
		os_free(peer_obj_paths);

	dl_list_for_each_safe(node, tmp, &peer_objpath_list,
			      struct peer_objpath_node, list) {
		dl_list_del(&node->list);
		os_free(node);
	}
	if (out_of_mem)
		dbus_set_error_const(error, DBUS_ERROR_NO_MEMORY, "no memory");

	return success;
}


enum wpas_p2p_role {
	WPAS_P2P_ROLE_DEVICE,
	WPAS_P2P_ROLE_GO,
	WPAS_P2P_ROLE_CLIENT,
};

static enum wpas_p2p_role wpas_get_p2p_role(struct wpa_supplicant *wpa_s)
{
	struct wpa_ssid *ssid = wpa_s->current_ssid;

	if (!ssid)
		return WPAS_P2P_ROLE_DEVICE;
	if (wpa_s->wpa_state != WPA_COMPLETED)
		return WPAS_P2P_ROLE_DEVICE;

	switch (ssid->mode) {
	case WPAS_MODE_P2P_GO:
	case WPAS_MODE_P2P_GROUP_FORMATION:
		return WPAS_P2P_ROLE_GO;
	case WPAS_MODE_INFRA:
		if (ssid->p2p_group)
			return WPAS_P2P_ROLE_CLIENT;
		return WPAS_P2P_ROLE_DEVICE;
	default:
		return WPAS_P2P_ROLE_DEVICE;
	}
}


dbus_bool_t wpas_dbus_getter_p2p_role(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct wpa_supplicant *wpa_s = user_data;
	char *str;

	switch (wpas_get_p2p_role(wpa_s)) {
	case WPAS_P2P_ROLE_GO:
		str = "GO";
		break;
	case WPAS_P2P_ROLE_CLIENT:
		str = "client";
		break;
	default:
		str = "device";
		break;
	}

	return wpas_dbus_simple_property_getter(iter, DBUS_TYPE_STRING, &str,
						error);
}


dbus_bool_t wpas_dbus_getter_p2p_group(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct wpa_supplicant *wpa_s = user_data;
	char path_buf[WPAS_DBUS_OBJECT_PATH_MAX];
	char *dbus_groupobj_path = path_buf;

	if (wpa_s->dbus_groupobj_path == NULL)
		os_snprintf(dbus_groupobj_path, WPAS_DBUS_OBJECT_PATH_MAX,
			    "/");
	else
		os_snprintf(dbus_groupobj_path, WPAS_DBUS_OBJECT_PATH_MAX,
			    "%s", wpa_s->dbus_groupobj_path);

	return wpas_dbus_simple_property_getter(iter, DBUS_TYPE_OBJECT_PATH,
						&dbus_groupobj_path, error);
}


dbus_bool_t wpas_dbus_getter_p2p_peergo(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct wpa_supplicant *wpa_s = user_data;
	char go_peer_obj_path[WPAS_DBUS_OBJECT_PATH_MAX], *path;

	if (!wpa_s->parent->parent->dbus_new_path)
		return FALSE;

	if (wpas_get_p2p_role(wpa_s) != WPAS_P2P_ROLE_CLIENT)
		os_snprintf(go_peer_obj_path, WPAS_DBUS_OBJECT_PATH_MAX, "/");
	else
		os_snprintf(go_peer_obj_path, WPAS_DBUS_OBJECT_PATH_MAX,
			    "%s/" WPAS_DBUS_NEW_P2P_PEERS_PART "/"
			    COMPACT_MACSTR,
			    wpa_s->parent->parent->dbus_new_path,
			    MAC2STR(wpa_s->go_dev_addr));

	path = go_peer_obj_path;
	return wpas_dbus_simple_property_getter(iter, DBUS_TYPE_OBJECT_PATH,
						&path, error);
}


/*
 * Peer object properties accessor methods
 */

dbus_bool_t wpas_dbus_getter_p2p_peer_device_name(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct peer_handler_args *peer_args = user_data;
	const struct p2p_peer_info *info;
	char *tmp;

	if (!wpa_dbus_p2p_check_enabled(peer_args->wpa_s, NULL, NULL, error))
		return FALSE;

	/* get the peer info */
	info = p2p_get_peer_found(peer_args->wpa_s->global->p2p,
				  peer_args->p2p_device_addr, 0);
	if (info == NULL) {
		dbus_set_error(error, DBUS_ERROR_FAILED,
			       "failed to find peer");
		return FALSE;
	}

	tmp = os_strdup(info->device_name);
	if (!tmp) {
		dbus_set_error_const(error, DBUS_ERROR_NO_MEMORY, "no memory");
		return FALSE;
	}

	if (!wpas_dbus_simple_property_getter(iter, DBUS_TYPE_STRING, &tmp,
					      error)) {
		dbus_set_error_const(error, DBUS_ERROR_NO_MEMORY, "no memory");
		os_free(tmp);
		return FALSE;
	}

	os_free(tmp);
	return TRUE;
}


dbus_bool_t wpas_dbus_getter_p2p_peer_manufacturer(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct peer_handler_args *peer_args = user_data;
	const struct p2p_peer_info *info;
	char *tmp;

	if (!wpa_dbus_p2p_check_enabled(peer_args->wpa_s, NULL, NULL, error))
		return FALSE;

	/* get the peer info */
	info = p2p_get_peer_found(peer_args->wpa_s->global->p2p,
				  peer_args->p2p_device_addr, 0);
	if (info == NULL) {
		dbus_set_error(error, DBUS_ERROR_FAILED, "failed to find peer");
		return FALSE;
	}

	tmp = os_strdup(info->manufacturer);
	if (!tmp) {
		dbus_set_error_const(error, DBUS_ERROR_NO_MEMORY, "no memory");
		return FALSE;
	}

	if (!wpas_dbus_simple_property_getter(iter, DBUS_TYPE_STRING, &tmp,
					      error)) {
		dbus_set_error_const(error, DBUS_ERROR_NO_MEMORY, "no memory");
		os_free(tmp);
		return FALSE;
	}

	os_free(tmp);
	return TRUE;
}


dbus_bool_t wpas_dbus_getter_p2p_peer_modelname(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct peer_handler_args *peer_args = user_data;
	const struct p2p_peer_info *info;
	char *tmp;

	if (!wpa_dbus_p2p_check_enabled(peer_args->wpa_s, NULL, NULL, error))
		return FALSE;

	/* get the peer info */
	info = p2p_get_peer_found(peer_args->wpa_s->global->p2p,
				  peer_args->p2p_device_addr, 0);
	if (info == NULL) {
		dbus_set_error(error, DBUS_ERROR_FAILED, "failed to find peer");
		return FALSE;
	}

	tmp = os_strdup(info->model_name);
	if (!tmp) {
		dbus_set_error_const(error, DBUS_ERROR_NO_MEMORY, "no memory");
		return FALSE;
	}

	if (!wpas_dbus_simple_property_getter(iter, DBUS_TYPE_STRING, &tmp,
					      error)) {
		dbus_set_error_const(error, DBUS_ERROR_NO_MEMORY, "no memory");
		os_free(tmp);
		return FALSE;
	}

	os_free(tmp);
	return TRUE;
}


dbus_bool_t wpas_dbus_getter_p2p_peer_modelnumber(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct peer_handler_args *peer_args = user_data;
	const struct p2p_peer_info *info;
	char *tmp;

	if (!wpa_dbus_p2p_check_enabled(peer_args->wpa_s, NULL, NULL, error))
		return FALSE;

	/* get the peer info */
	info = p2p_get_peer_found(peer_args->wpa_s->global->p2p,
				  peer_args->p2p_device_addr, 0);
	if (info == NULL) {
		dbus_set_error(error, DBUS_ERROR_FAILED, "failed to find peer");
		return FALSE;
	}

	tmp = os_strdup(info->model_number);
	if (!tmp) {
		dbus_set_error_const(error, DBUS_ERROR_NO_MEMORY, "no memory");
		return FALSE;
	}

	if (!wpas_dbus_simple_property_getter(iter, DBUS_TYPE_STRING, &tmp,
					      error)) {
		dbus_set_error_const(error, DBUS_ERROR_NO_MEMORY, "no memory");
		os_free(tmp);
		return FALSE;
	}

	os_free(tmp);
	return TRUE;
}


dbus_bool_t wpas_dbus_getter_p2p_peer_serialnumber(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct peer_handler_args *peer_args = user_data;
	const struct p2p_peer_info *info;
	char *tmp;

	if (!wpa_dbus_p2p_check_enabled(peer_args->wpa_s, NULL, NULL, error))
		return FALSE;

	/* get the peer info */
	info = p2p_get_peer_found(peer_args->wpa_s->global->p2p,
				  peer_args->p2p_device_addr, 0);
	if (info == NULL) {
		dbus_set_error(error, DBUS_ERROR_FAILED, "failed to find peer");
		return FALSE;
	}

	tmp = os_strdup(info->serial_number);
	if (!tmp) {
		dbus_set_error_const(error, DBUS_ERROR_NO_MEMORY, "no memory");
		return FALSE;
	}

	if (!wpas_dbus_simple_property_getter(iter, DBUS_TYPE_STRING, &tmp,
					      error)) {
		dbus_set_error_const(error, DBUS_ERROR_NO_MEMORY, "no memory");
		os_free(tmp);
		return FALSE;
	}

	os_free(tmp);
	return TRUE;
}


dbus_bool_t wpas_dbus_getter_p2p_peer_primary_device_type(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct peer_handler_args *peer_args = user_data;
	const struct p2p_peer_info *info;

	info = p2p_get_peer_found(peer_args->wpa_s->global->p2p,
				  peer_args->p2p_device_addr, 0);
	if (info == NULL) {
		dbus_set_error(error, DBUS_ERROR_FAILED,
			       "failed to find peer");
		return FALSE;
	}

	if (!wpas_dbus_simple_array_property_getter(iter, DBUS_TYPE_BYTE,
						    (char *)
						    info->pri_dev_type,
						    WPS_DEV_TYPE_LEN, error)) {
		dbus_set_error_const(error, DBUS_ERROR_NO_MEMORY, "no memory");
		return FALSE;
	}

	return TRUE;
}


dbus_bool_t wpas_dbus_getter_p2p_peer_config_method(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct peer_handler_args *peer_args = user_data;
	const struct p2p_peer_info *info;

	info = p2p_get_peer_found(peer_args->wpa_s->global->p2p,
				  peer_args->p2p_device_addr, 0);
	if (info == NULL) {
		dbus_set_error(error, DBUS_ERROR_FAILED,
			       "failed to find peer");
		return FALSE;
	}

	if (!wpas_dbus_simple_property_getter(iter, DBUS_TYPE_UINT16,
					      &info->config_methods, error)) {
		dbus_set_error_const(error, DBUS_ERROR_NO_MEMORY, "no memory");
		return FALSE;
	}

	return TRUE;
}


dbus_bool_t wpas_dbus_getter_p2p_peer_level(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct peer_handler_args *peer_args = user_data;
	const struct p2p_peer_info *info;

	info = p2p_get_peer_found(peer_args->wpa_s->global->p2p,
				  peer_args->p2p_device_addr, 0);
	if (info == NULL) {
		dbus_set_error(error, DBUS_ERROR_FAILED,
			       "failed to find peer");
		return FALSE;
	}

	if (!wpas_dbus_simple_property_getter(iter, DBUS_TYPE_INT32,
					      &info->level, error)) {
		dbus_set_error_const(error, DBUS_ERROR_NO_MEMORY, "no memory");
		return FALSE;
	}

	return TRUE;
}


dbus_bool_t wpas_dbus_getter_p2p_peer_device_capability(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct peer_handler_args *peer_args = user_data;
	const struct p2p_peer_info *info;

	info = p2p_get_peer_found(peer_args->wpa_s->global->p2p,
				  peer_args->p2p_device_addr, 0);
	if (info == NULL) {
		dbus_set_error(error, DBUS_ERROR_FAILED,
			       "failed to find peer");
		return FALSE;
	}

	if (!wpas_dbus_simple_property_getter(iter, DBUS_TYPE_BYTE,
					      &info->dev_capab, error)) {
		dbus_set_error_const(error, DBUS_ERROR_NO_MEMORY, "no memory");
		return FALSE;
	}

	return TRUE;
}


dbus_bool_t wpas_dbus_getter_p2p_peer_group_capability(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct peer_handler_args *peer_args = user_data;
	const struct p2p_peer_info *info;

	info = p2p_get_peer_found(peer_args->wpa_s->global->p2p,
				  peer_args->p2p_device_addr, 0);
	if (info == NULL) {
		dbus_set_error(error, DBUS_ERROR_FAILED,
			       "failed to find peer");
		return FALSE;
	}

	if (!wpas_dbus_simple_property_getter(iter, DBUS_TYPE_BYTE,
					      &info->group_capab, error)) {
		dbus_set_error_const(error, DBUS_ERROR_NO_MEMORY, "no memory");
		return FALSE;
	}

	return TRUE;
}


dbus_bool_t wpas_dbus_getter_p2p_peer_secondary_device_types(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct peer_handler_args *peer_args = user_data;
	const struct p2p_peer_info *info;
	DBusMessageIter variant_iter, array_iter;

	info = p2p_get_peer_found(peer_args->wpa_s->global->p2p,
				  peer_args->p2p_device_addr, 0);
	if (info == NULL) {
		dbus_set_error(error, DBUS_ERROR_FAILED, "failed to find peer");
		return FALSE;
	}

	if (!dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT,
					      DBUS_TYPE_ARRAY_AS_STRING
					      DBUS_TYPE_ARRAY_AS_STRING
					      DBUS_TYPE_BYTE_AS_STRING,
					      &variant_iter) ||
	    !dbus_message_iter_open_container(&variant_iter, DBUS_TYPE_ARRAY,
					      DBUS_TYPE_ARRAY_AS_STRING
					      DBUS_TYPE_BYTE_AS_STRING,
					      &array_iter)) {
		dbus_set_error(error, DBUS_ERROR_FAILED,
			       "%s: failed to construct message 1", __func__);
		return FALSE;
	}

	if (info->wps_sec_dev_type_list_len) {
		const u8 *sec_dev_type_list = info->wps_sec_dev_type_list;
		int num_sec_device_types =
			info->wps_sec_dev_type_list_len / WPS_DEV_TYPE_LEN;
		int i;
		DBusMessageIter inner_array_iter;

		for (i = 0; i < num_sec_device_types; i++) {
			if (!dbus_message_iter_open_container(
				    &array_iter, DBUS_TYPE_ARRAY,
				    DBUS_TYPE_BYTE_AS_STRING,
				    &inner_array_iter) ||
			    !dbus_message_iter_append_fixed_array(
				    &inner_array_iter, DBUS_TYPE_BYTE,
				    &sec_dev_type_list, WPS_DEV_TYPE_LEN) ||
			    !dbus_message_iter_close_container(
				    &array_iter, &inner_array_iter)) {
				dbus_set_error(error, DBUS_ERROR_FAILED,
					       "%s: failed to construct message 2 (%d)",
					       __func__, i);
				return FALSE;
			}

			sec_dev_type_list += WPS_DEV_TYPE_LEN;
		}
	}

	if (!dbus_message_iter_close_container(&variant_iter, &array_iter) ||
	    !dbus_message_iter_close_container(iter, &variant_iter)) {
		dbus_set_error(error, DBUS_ERROR_FAILED,
			       "%s: failed to construct message 3", __func__);
		return FALSE;
	}

	return TRUE;
}


dbus_bool_t wpas_dbus_getter_p2p_peer_vendor_extension(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct wpabuf *vendor_extension[P2P_MAX_WPS_VENDOR_EXT];
	unsigned int i, num = 0;
	struct peer_handler_args *peer_args = user_data;
	const struct p2p_peer_info *info;

	info = p2p_get_peer_found(peer_args->wpa_s->global->p2p,
				  peer_args->p2p_device_addr, 0);
	if (info == NULL) {
		dbus_set_error(error, DBUS_ERROR_FAILED,
			       "failed to find peer");
		return FALSE;
	}

	/* Add WPS vendor extensions attribute */
	os_memset(vendor_extension, 0, sizeof(vendor_extension));
	for (i = 0; i < P2P_MAX_WPS_VENDOR_EXT; i++) {
		if (info->wps_vendor_ext[i] == NULL)
			continue;
		vendor_extension[num] = info->wps_vendor_ext[i];
		num++;
	}

	if (!wpas_dbus_simple_array_array_property_getter(iter, DBUS_TYPE_BYTE,
							  vendor_extension,
							  num, error))
		return FALSE;

	return TRUE;
}


dbus_bool_t wpas_dbus_getter_p2p_peer_ies(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct peer_handler_args *peer_args = user_data;
	const struct p2p_peer_info *info;

	info = p2p_get_peer_found(peer_args->wpa_s->global->p2p,
				  peer_args->p2p_device_addr, 0);
	if (info == NULL) {
		dbus_set_error(error, DBUS_ERROR_FAILED,
			       "failed to find peer");
		return FALSE;
	}

	if (info->wfd_subelems == NULL)
		return wpas_dbus_simple_array_property_getter(iter,
							      DBUS_TYPE_BYTE,
							      NULL, 0, error);

	return wpas_dbus_simple_array_property_getter(
		iter, DBUS_TYPE_BYTE, (char *) info->wfd_subelems->buf,
		info->wfd_subelems->used, error);
}


dbus_bool_t wpas_dbus_getter_p2p_peer_device_address(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct peer_handler_args *peer_args = user_data;
	const struct p2p_peer_info *info;

	info = p2p_get_peer_found(peer_args->wpa_s->global->p2p,
				  peer_args->p2p_device_addr, 0);
	if (info == NULL) {
		dbus_set_error(error, DBUS_ERROR_FAILED,
			       "failed to find peer");
		return FALSE;
	}

	return wpas_dbus_simple_array_property_getter(
		iter, DBUS_TYPE_BYTE, (char *) info->p2p_device_addr,
		ETH_ALEN, error);
}


struct peer_group_data {
	struct wpa_supplicant *wpa_s;
	const struct p2p_peer_info *info;
	char **paths;
	unsigned int nb_paths;
	int error;
};


static int match_group_where_peer_is_client(struct p2p_group *group,
					    void *user_data)
{
	struct peer_group_data *data = user_data;
	const struct p2p_group_config *cfg;
	struct wpa_supplicant *wpa_s_go;
	char **paths;

	if (!p2p_group_is_client_connected(group, data->info->p2p_device_addr))
		return 1;

	cfg = p2p_group_get_config(group);

	wpa_s_go = wpas_get_p2p_go_iface(data->wpa_s, cfg->ssid,
					 cfg->ssid_len);
	if (wpa_s_go == NULL)
		return 1;

	paths = os_realloc_array(data->paths, data->nb_paths + 1,
				 sizeof(char *));
	if (paths == NULL)
		goto out_of_memory;

	data->paths = paths;
	data->paths[data->nb_paths] = wpa_s_go->dbus_groupobj_path;
	data->nb_paths++;

	return 1;

out_of_memory:
	data->error = ENOMEM;
	return 0;
}


dbus_bool_t wpas_dbus_getter_p2p_peer_groups(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct peer_handler_args *peer_args = user_data;
	const struct p2p_peer_info *info;
	struct peer_group_data data;
	struct wpa_supplicant *wpa_s, *wpa_s_go;
	dbus_bool_t success = FALSE;

	info = p2p_get_peer_found(peer_args->wpa_s->global->p2p,
				  peer_args->p2p_device_addr, 0);
	if (info == NULL) {
		dbus_set_error(error, DBUS_ERROR_FAILED,
			       "failed to find peer");
		return FALSE;
	}

	os_memset(&data, 0, sizeof(data));

	wpa_s = peer_args->wpa_s;
	wpa_s = wpa_s->global->p2p_init_wpa_s;

	wpa_s_go = wpas_get_p2p_client_iface(wpa_s, info->p2p_device_addr);
	if (wpa_s_go) {
		data.paths = os_calloc(1, sizeof(char *));
		if (data.paths == NULL)
			goto out_of_memory;
		data.paths[0] = wpa_s_go->dbus_groupobj_path;
		data.nb_paths = 1;
	}

	data.wpa_s = peer_args->wpa_s;
	data.info = info;

	p2p_loop_on_all_groups(peer_args->wpa_s->global->p2p,
			       match_group_where_peer_is_client, &data);
	if (data.error)
		goto out_of_memory;

	if (data.paths == NULL) {
		return wpas_dbus_simple_array_property_getter(
			iter, DBUS_TYPE_OBJECT_PATH, NULL, 0, error);
	}

	success = wpas_dbus_simple_array_property_getter(iter,
							 DBUS_TYPE_OBJECT_PATH,
							 data.paths,
							 data.nb_paths, error);
	goto out;

out_of_memory:
	dbus_set_error_const(error, DBUS_ERROR_NO_MEMORY, "no memory");
out:
	os_free(data.paths);
	return success;
}


/**
 * wpas_dbus_getter_persistent_groups - Get array of persistent group objects
 * @iter: Pointer to incoming dbus message iter
 * @error: Location to store error on failure
 * @user_data: Function specific data
 * Returns: TRUE on success, FALSE on failure
 *
 * Getter for "PersistentGroups" property.
 */
dbus_bool_t wpas_dbus_getter_persistent_groups(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct wpa_supplicant *wpa_s = user_data;
	struct wpa_ssid *ssid;
	char **paths;
	unsigned int i = 0, num = 0;
	dbus_bool_t success = FALSE;

	wpa_s = wpa_s->global->p2p_init_wpa_s;
	if (!wpa_s->parent->dbus_new_path)
		return FALSE;

	for (ssid = wpa_s->conf->ssid; ssid; ssid = ssid->next)
		if (network_is_persistent_group(ssid))
			num++;

	paths = os_calloc(num, sizeof(char *));
	if (!paths) {
		dbus_set_error_const(error, DBUS_ERROR_NO_MEMORY, "no memory");
		return FALSE;
	}

	/* Loop through configured networks and append object path of each */
	for (ssid = wpa_s->conf->ssid; ssid; ssid = ssid->next) {
		if (!network_is_persistent_group(ssid))
			continue;
		paths[i] = os_zalloc(WPAS_DBUS_OBJECT_PATH_MAX);
		if (paths[i] == NULL) {
			dbus_set_error_const(error, DBUS_ERROR_NO_MEMORY,
					     "no memory");
			goto out;
		}
		/* Construct the object path for this network. */
		os_snprintf(paths[i++], WPAS_DBUS_OBJECT_PATH_MAX,
			    "%s/" WPAS_DBUS_NEW_PERSISTENT_GROUPS_PART "/%d",
			    wpa_s->parent->dbus_new_path, ssid->id);
	}

	success = wpas_dbus_simple_array_property_getter(iter,
							 DBUS_TYPE_OBJECT_PATH,
							 paths, num, error);

out:
	while (i)
		os_free(paths[--i]);
	os_free(paths);
	return success;
}


/**
 * wpas_dbus_getter_persistent_group_properties - Get options for a persistent
 *	group
 * @iter: Pointer to incoming dbus message iter
 * @error: Location to store error on failure
 * @user_data: Function specific data
 * Returns: TRUE on success, FALSE on failure
 *
 * Getter for "Properties" property of a persistent group.
 */
dbus_bool_t wpas_dbus_getter_persistent_group_properties(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct network_handler_args *net = user_data;

	/* Leveraging the fact that persistent group object is still
	 * represented in same manner as network within.
	 */
	return wpas_dbus_getter_network_properties(property_desc, iter, error, net);
}


/**
 * wpas_dbus_setter_persistent_group_properties - Set options for a persistent
 *	group
 * @iter: Pointer to incoming dbus message iter
 * @error: Location to store error on failure
 * @user_data: Function specific data
 * Returns: TRUE on success, FALSE on failure
 *
 * Setter for "Properties" property of a persistent group.
 */
dbus_bool_t wpas_dbus_setter_persistent_group_properties(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct network_handler_args *net = user_data;
	struct wpa_ssid *ssid = net->ssid;
	DBusMessageIter	variant_iter;

	/*
	 * Leveraging the fact that persistent group object is still
	 * represented in same manner as network within.
	 */
	dbus_message_iter_recurse(iter, &variant_iter);
	return set_network_properties(net->wpa_s, ssid, &variant_iter, error);
}


/**
 * wpas_dbus_new_iface_add_persistent_group - Add a new configured
 *	persistent_group
 * @message: Pointer to incoming dbus message
 * @wpa_s: wpa_supplicant structure for a network interface
 * Returns: A dbus message containing the object path of the new
 * persistent group
 *
 * Handler function for "AddPersistentGroup" method call of a P2P Device
 * interface.
 */
DBusMessage * wpas_dbus_handler_add_persistent_group(
	DBusMessage *message, struct wpa_supplicant *wpa_s)
{
	DBusMessage *reply = NULL;
	DBusMessageIter	iter;
	struct wpa_ssid *ssid = NULL;
	char path_buf[WPAS_DBUS_OBJECT_PATH_MAX], *path = path_buf;
	DBusError error;

	dbus_message_iter_init(message, &iter);

	wpa_s = wpa_s->global->p2p_init_wpa_s;
	if (wpa_s->parent->dbus_new_path)
		ssid = wpa_config_add_network(wpa_s->conf);
	if (ssid == NULL) {
		wpa_printf(MSG_ERROR,
			   "dbus: %s: Cannot add new persistent group",
			   __func__);
		reply = wpas_dbus_error_unknown_error(
			message,
			"wpa_supplicant could not add a persistent group on this interface.");
		goto err;
	}

	/* Mark the ssid as being a persistent group before the notification */
	ssid->disabled = 2;
	ssid->p2p_persistent_group = 1;
	wpas_notify_persistent_group_added(wpa_s, ssid);

	wpa_config_set_network_defaults(ssid);

	dbus_error_init(&error);
	if (!set_network_properties(wpa_s, ssid, &iter, &error)) {
		wpa_printf(MSG_DEBUG,
			   "dbus: %s: Control interface could not set persistent group properties",
			   __func__);
		reply = wpas_dbus_reply_new_from_error(
			message, &error, DBUS_ERROR_INVALID_ARGS,
			"Failed to set network properties");
		dbus_error_free(&error);
		goto err;
	}

	/* Construct the object path for this network. */
	os_snprintf(path, WPAS_DBUS_OBJECT_PATH_MAX,
		    "%s/" WPAS_DBUS_NEW_PERSISTENT_GROUPS_PART "/%d",
		    wpa_s->parent->dbus_new_path, ssid->id);

	reply = dbus_message_new_method_return(message);
	if (reply == NULL) {
		reply = wpas_dbus_error_no_memory(message);
		goto err;
	}
	if (!dbus_message_append_args(reply, DBUS_TYPE_OBJECT_PATH, &path,
				      DBUS_TYPE_INVALID)) {
		dbus_message_unref(reply);
		reply = wpas_dbus_error_no_memory(message);
		goto err;
	}

	return reply;

err:
	if (ssid) {
		wpas_notify_persistent_group_removed(wpa_s, ssid);
		wpa_config_remove_network(wpa_s->conf, ssid->id);
	}
	return reply;
}


/**
 * wpas_dbus_handler_remove_persistent_group - Remove a configured persistent
 *	group
 * @message: Pointer to incoming dbus message
 * @wpa_s: wpa_supplicant structure for a network interface
 * Returns: NULL on success or dbus error on failure
 *
 * Handler function for "RemovePersistentGroup" method call of a P2P Device
 * interface.
 */
DBusMessage * wpas_dbus_handler_remove_persistent_group(
	DBusMessage *message, struct wpa_supplicant *wpa_s)
{
	DBusMessage *reply = NULL;
	const char *op;
	char *iface = NULL, *persistent_group_id;
	int id;
	struct wpa_ssid *ssid;

	dbus_message_get_args(message, NULL, DBUS_TYPE_OBJECT_PATH, &op,
			      DBUS_TYPE_INVALID);

	wpa_s = wpa_s->global->p2p_init_wpa_s;

	/*
	 * Extract the network ID and ensure the network is actually a child of
	 * this interface.
	 */
	iface = wpas_dbus_new_decompose_object_path(
		op, WPAS_DBUS_NEW_PERSISTENT_GROUPS_PART,
		&persistent_group_id);
	if (iface == NULL || persistent_group_id == NULL ||
	    !wpa_s->parent->dbus_new_path ||
	    os_strcmp(iface, wpa_s->parent->dbus_new_path) != 0) {
		reply = wpas_dbus_error_invalid_args(message, op);
		goto out;
	}

	id = strtoul(persistent_group_id, NULL, 10);
	if (errno == EINVAL) {
		reply = wpas_dbus_error_invalid_args(message, op);
		goto out;
	}

	ssid = wpa_config_get_network(wpa_s->conf, id);
	if (ssid == NULL) {
		reply = wpas_dbus_error_persistent_group_unknown(message);
		goto out;
	}

	wpas_notify_persistent_group_removed(wpa_s, ssid);

	if (wpa_config_remove_network(wpa_s->conf, id) < 0) {
		wpa_printf(MSG_ERROR,
			   "dbus: %s: error occurred when removing persistent group %d",
			   __func__, id);
		reply = wpas_dbus_error_unknown_error(
			message,
			"error removing the specified persistent group on this interface.");
		goto out;
	}

out:
	os_free(iface);
	return reply;
}


static void remove_persistent_group(struct wpa_supplicant *wpa_s,
				    struct wpa_ssid *ssid)
{
	wpas_notify_persistent_group_removed(wpa_s, ssid);

	if (wpa_config_remove_network(wpa_s->conf, ssid->id) < 0) {
		wpa_printf(MSG_ERROR,
			   "dbus: %s: error occurred when removing persistent group %d",
			   __func__, ssid->id);
		return;
	}
}


/**
 * wpas_dbus_handler_remove_all_persistent_groups - Remove all configured
 * persistent groups
 * @message: Pointer to incoming dbus message
 * @wpa_s: wpa_supplicant structure for a network interface
 * Returns: NULL on success or dbus error on failure
 *
 * Handler function for "RemoveAllPersistentGroups" method call of a
 * P2P Device interface.
 */
DBusMessage * wpas_dbus_handler_remove_all_persistent_groups(
	DBusMessage *message, struct wpa_supplicant *wpa_s)
{
	struct wpa_ssid *ssid, *next;
	struct wpa_config *config;

	wpa_s = wpa_s->global->p2p_init_wpa_s;

	config = wpa_s->conf;
	ssid = config->ssid;
	while (ssid) {
		next = ssid->next;
		if (network_is_persistent_group(ssid))
			remove_persistent_group(wpa_s, ssid);
		ssid = next;
	}
	return NULL;
}


/*
 * Group object properties accessor methods
 */

dbus_bool_t wpas_dbus_getter_p2p_group_members(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct wpa_supplicant *wpa_s = user_data;
	struct wpa_ssid *ssid;
	unsigned int num_members;
	char **paths;
	unsigned int i;
	void *next = NULL;
	const u8 *addr;
	dbus_bool_t success = FALSE;

	if (!wpa_s->parent->parent->dbus_new_path)
		return FALSE;

	/* Verify correct role for this property */
	if (wpas_get_p2p_role(wpa_s) != WPAS_P2P_ROLE_GO) {
		return wpas_dbus_simple_array_property_getter(
			iter, DBUS_TYPE_OBJECT_PATH, NULL, 0, error);
	}

	ssid = wpa_s->conf->ssid;
	/* At present WPAS P2P_GO mode only applicable for p2p_go */
	if (ssid->mode != WPAS_MODE_P2P_GO &&
	    ssid->mode != WPAS_MODE_AP &&
	    ssid->mode != WPAS_MODE_P2P_GROUP_FORMATION)
		return FALSE;

	num_members = p2p_get_group_num_members(wpa_s->p2p_group);

	paths = os_calloc(num_members, sizeof(char *));
	if (!paths)
		goto out_of_memory;

	i = 0;
	while ((addr = p2p_iterate_group_members(wpa_s->p2p_group, &next))) {
		paths[i] = os_zalloc(WPAS_DBUS_OBJECT_PATH_MAX);
		if (!paths[i])
			goto out_of_memory;
		os_snprintf(paths[i], WPAS_DBUS_OBJECT_PATH_MAX,
			    "%s/" WPAS_DBUS_NEW_P2P_PEERS_PART
			    "/" COMPACT_MACSTR,
			    wpa_s->parent->parent->dbus_new_path,
			    MAC2STR(addr));
		i++;
	}

	success = wpas_dbus_simple_array_property_getter(iter,
							 DBUS_TYPE_OBJECT_PATH,
							 paths, num_members,
							 error);

	for (i = 0; i < num_members; i++)
		os_free(paths[i]);
	os_free(paths);
	return success;

out_of_memory:
	dbus_set_error_const(error, DBUS_ERROR_NO_MEMORY, "no memory");
	if (paths) {
		for (i = 0; i < num_members; i++)
			os_free(paths[i]);
		os_free(paths);
	}
	return FALSE;
}


dbus_bool_t wpas_dbus_getter_p2p_group_ssid(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct wpa_supplicant *wpa_s = user_data;

	if (wpa_s->current_ssid == NULL)
		return FALSE;
	return wpas_dbus_simple_array_property_getter(
		iter, DBUS_TYPE_BYTE, wpa_s->current_ssid->ssid,
		wpa_s->current_ssid->ssid_len, error);
}


dbus_bool_t wpas_dbus_getter_p2p_group_bssid(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct wpa_supplicant *wpa_s = user_data;
	u8 role = wpas_get_p2p_role(wpa_s);
	u8 *p_bssid;

	if (role == WPAS_P2P_ROLE_CLIENT) {
		if (wpa_s->current_ssid == NULL)
			return FALSE;
		p_bssid = wpa_s->current_ssid->bssid;
	} else {
		if (wpa_s->ap_iface == NULL)
			return FALSE;
		p_bssid = wpa_s->ap_iface->bss[0]->own_addr;
	}

	return wpas_dbus_simple_array_property_getter(iter, DBUS_TYPE_BYTE,
						      p_bssid, ETH_ALEN,
						      error);
}


dbus_bool_t wpas_dbus_getter_p2p_group_frequency(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct wpa_supplicant *wpa_s = user_data;
	u16 op_freq;
	u8 role = wpas_get_p2p_role(wpa_s);

	if (role == WPAS_P2P_ROLE_CLIENT) {
		if (wpa_s->go_params == NULL)
			return FALSE;
		op_freq = wpa_s->go_params->freq;
	} else {
		if (wpa_s->ap_iface == NULL)
			return FALSE;
		op_freq = wpa_s->ap_iface->freq;
	}

	return wpas_dbus_simple_property_getter(iter, DBUS_TYPE_UINT16,
						&op_freq, error);
}


dbus_bool_t wpas_dbus_getter_p2p_group_passphrase(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct wpa_supplicant *wpa_s = user_data;
	struct wpa_ssid *ssid = wpa_s->current_ssid;

	if (ssid == NULL)
		return FALSE;

	return wpas_dbus_string_property_getter(iter, ssid->passphrase, error);
}


dbus_bool_t wpas_dbus_getter_p2p_group_psk(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct wpa_supplicant *wpa_s = user_data;
	u8 *p_psk = NULL;
	u8 psk_len = 0;
	struct wpa_ssid *ssid = wpa_s->current_ssid;

	if (ssid == NULL)
		return FALSE;

	if (ssid->psk_set) {
		p_psk = ssid->psk;
		psk_len = sizeof(ssid->psk);
	}

	return wpas_dbus_simple_array_property_getter(iter, DBUS_TYPE_BYTE,
						      p_psk, psk_len, error);
}


dbus_bool_t wpas_dbus_getter_p2p_group_vendor_ext(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct wpa_supplicant *wpa_s = user_data;
	struct hostapd_data *hapd;
	struct wpabuf *vendor_ext[MAX_WPS_VENDOR_EXTENSIONS];
	unsigned int i, num_vendor_ext = 0;

	os_memset(vendor_ext, 0, sizeof(vendor_ext));

	/* Verify correct role for this property */
	if (wpas_get_p2p_role(wpa_s) == WPAS_P2P_ROLE_GO) {
		if (wpa_s->ap_iface == NULL)
			return FALSE;
		hapd = wpa_s->ap_iface->bss[0];

		/* Parse WPS Vendor Extensions sent in Beacon/Probe Response */
		for (i = 0; i < MAX_WPS_VENDOR_EXTENSIONS; i++) {
			if (hapd->conf->wps_vendor_ext[i] == NULL)
				continue;
			vendor_ext[num_vendor_ext++] =
				hapd->conf->wps_vendor_ext[i];
		}
	}

	/* Return vendor extensions or no data */
	return wpas_dbus_simple_array_array_property_getter(iter,
							    DBUS_TYPE_BYTE,
							    vendor_ext,
							    num_vendor_ext,
							    error);
}


dbus_bool_t wpas_dbus_setter_p2p_group_vendor_ext(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct wpa_supplicant *wpa_s = user_data;
	DBusMessageIter variant_iter, iter_dict, array_iter, sub;
	struct wpa_dbus_dict_entry entry = { .type = DBUS_TYPE_STRING };
	unsigned int i;
	struct hostapd_data *hapd = NULL;

	if (wpas_get_p2p_role(wpa_s) == WPAS_P2P_ROLE_GO &&
	    wpa_s->ap_iface != NULL)
		hapd = wpa_s->ap_iface->bss[0];
	else
		return FALSE;

	dbus_message_iter_recurse(iter, &variant_iter);
	if (dbus_message_iter_get_arg_type(&variant_iter) != DBUS_TYPE_ARRAY)
		return FALSE;

	/*
	 * This is supposed to be array of bytearrays (aay), but the earlier
	 * implementation used a dict with "WPSVendorExtensions" as the key in
	 * this setter function which does not match the format used by the
	 * getter function. For backwards compatibility, allow both formats to
	 * be used in the setter.
	 */
	if (dbus_message_iter_get_element_type(&variant_iter) ==
	    DBUS_TYPE_ARRAY) {
		/* This is the proper format matching the getter */
		struct wpabuf *vals[MAX_WPS_VENDOR_EXTENSIONS];

		dbus_message_iter_recurse(&variant_iter, &array_iter);

		if (dbus_message_iter_get_arg_type(&array_iter) !=
		    DBUS_TYPE_ARRAY ||
		    dbus_message_iter_get_element_type(&array_iter) !=
		    DBUS_TYPE_BYTE) {
			wpa_printf(MSG_DEBUG,
				   "dbus: Not an array of array of bytes");
			return FALSE;
		}

		i = 0;
		os_memset(vals, 0, sizeof(vals));

		while (dbus_message_iter_get_arg_type(&array_iter) ==
		       DBUS_TYPE_ARRAY) {
			char *val;
			int len;

			if (i == MAX_WPS_VENDOR_EXTENSIONS) {
				wpa_printf(MSG_DEBUG,
					   "dbus: Too many WPSVendorExtensions values");
				i = MAX_WPS_VENDOR_EXTENSIONS + 1;
				break;
			}

			dbus_message_iter_recurse(&array_iter, &sub);
			dbus_message_iter_get_fixed_array(&sub, &val, &len);
			wpa_hexdump(MSG_DEBUG, "dbus: WPSVendorExtentions[]",
				    val, len);
			vals[i] = wpabuf_alloc_copy(val, len);
			if (vals[i] == NULL) {
				i = MAX_WPS_VENDOR_EXTENSIONS + 1;
				break;
			}
			i++;
			dbus_message_iter_next(&array_iter);
		}

		if (i > MAX_WPS_VENDOR_EXTENSIONS) {
			for (i = 0; i < MAX_WPS_VENDOR_EXTENSIONS; i++)
				wpabuf_free(vals[i]);
			return FALSE;
		}

		for (i = 0; i < MAX_WPS_VENDOR_EXTENSIONS; i++) {
			wpabuf_free(hapd->conf->wps_vendor_ext[i]);
			hapd->conf->wps_vendor_ext[i] = vals[i];
		}

		hostapd_update_wps(hapd);

		return TRUE;
	}

	if (dbus_message_iter_get_element_type(&variant_iter) !=
	    DBUS_TYPE_DICT_ENTRY)
		return FALSE;

	wpa_printf(MSG_DEBUG,
		   "dbus: Try to use backwards compatibility version of WPSVendorExtensions setter");
	if (!wpa_dbus_dict_open_read(&variant_iter, &iter_dict, error))
		return FALSE;

	while (wpa_dbus_dict_has_dict_entry(&iter_dict)) {
		if (!wpa_dbus_dict_get_entry(&iter_dict, &entry)) {
			dbus_set_error_const(error, DBUS_ERROR_INVALID_ARGS,
					     "invalid message format");
			return FALSE;
		}

		if (os_strcmp(entry.key, "WPSVendorExtensions") == 0) {
			if (entry.type != DBUS_TYPE_ARRAY ||
			    entry.array_type != WPAS_DBUS_TYPE_BINARRAY ||
			    entry.array_len > MAX_WPS_VENDOR_EXTENSIONS)
				goto error;

			for (i = 0; i < MAX_WPS_VENDOR_EXTENSIONS; i++) {
				wpabuf_free(hapd->conf->wps_vendor_ext[i]);
				if (i < entry.array_len) {
					hapd->conf->wps_vendor_ext[i] =
						entry.binarray_value[i];
					entry.binarray_value[i] = NULL;
				} else
					hapd->conf->wps_vendor_ext[i] = NULL;
			}

			hostapd_update_wps(hapd);
		} else
			goto error;

		wpa_dbus_dict_entry_clear(&entry);
	}

	return TRUE;

error:
	wpa_dbus_dict_entry_clear(&entry);
	dbus_set_error_const(error, DBUS_ERROR_INVALID_ARGS,
			     "invalid message format");
	return FALSE;
}


DBusMessage * wpas_dbus_handler_p2p_add_service(DBusMessage *message,
						struct wpa_supplicant *wpa_s)
{
	DBusMessageIter iter_dict;
	DBusMessage *reply = NULL;
	DBusMessageIter iter;
	struct wpa_dbus_dict_entry entry;
	int upnp = 0;
	int bonjour = 0;
	char *service = NULL;
	struct wpabuf *query = NULL;
	struct wpabuf *resp = NULL;
	u8 version = 0;

	dbus_message_iter_init(message, &iter);

	if (!wpa_dbus_dict_open_read(&iter, &iter_dict, NULL))
		goto error;

	while (wpa_dbus_dict_has_dict_entry(&iter_dict)) {
		if (!wpa_dbus_dict_get_entry(&iter_dict, &entry))
			goto error;

		if (os_strcmp(entry.key, "service_type") == 0 &&
		    entry.type == DBUS_TYPE_STRING) {
			if (os_strcmp(entry.str_value, "upnp") == 0)
				upnp = 1;
			else if (os_strcmp(entry.str_value, "bonjour") == 0)
				bonjour = 1;
			else
				goto error_clear;
		} else if (os_strcmp(entry.key, "version") == 0 &&
			   entry.type == DBUS_TYPE_INT32) {
			version = entry.uint32_value;
		} else if (os_strcmp(entry.key, "service") == 0 &&
			   entry.type == DBUS_TYPE_STRING) {
			os_free(service);
			service = os_strdup(entry.str_value);
		} else if (os_strcmp(entry.key, "query") == 0) {
			if (entry.type != DBUS_TYPE_ARRAY ||
			    entry.array_type != DBUS_TYPE_BYTE)
				goto error_clear;
			query = wpabuf_alloc_copy(
				entry.bytearray_value,
				entry.array_len);
		} else if (os_strcmp(entry.key, "response") == 0) {
			if (entry.type != DBUS_TYPE_ARRAY ||
			    entry.array_type != DBUS_TYPE_BYTE)
				goto error_clear;
			resp = wpabuf_alloc_copy(entry.bytearray_value,
						 entry.array_len);
		}
		wpa_dbus_dict_entry_clear(&entry);
	}

	if (upnp == 1) {
		if (version <= 0 || service == NULL)
			goto error;

		if (wpas_p2p_service_add_upnp(wpa_s, version, service) != 0)
			goto error;

	} else if (bonjour == 1) {
		if (query == NULL || resp == NULL)
			goto error;

		if (wpas_p2p_service_add_bonjour(wpa_s, query, resp) < 0)
			goto error;
		query = NULL;
		resp = NULL;
	} else
		goto error;

	os_free(service);
	return reply;
error_clear:
	wpa_dbus_dict_entry_clear(&entry);
error:
	os_free(service);
	wpabuf_free(query);
	wpabuf_free(resp);
	return wpas_dbus_error_invalid_args(message, NULL);
}


DBusMessage * wpas_dbus_handler_p2p_delete_service(
	DBusMessage *message, struct wpa_supplicant *wpa_s)
{
	DBusMessageIter iter_dict;
	DBusMessage *reply = NULL;
	DBusMessageIter iter;
	struct wpa_dbus_dict_entry entry;
	int upnp = 0;
	int bonjour = 0;
	int ret = 0;
	char *service = NULL;
	struct wpabuf *query = NULL;
	u8 version = 0;

	dbus_message_iter_init(message, &iter);

	if (!wpa_dbus_dict_open_read(&iter, &iter_dict, NULL))
		goto error;

	if (wpa_dbus_dict_has_dict_entry(&iter_dict)) {
		if (!wpa_dbus_dict_get_entry(&iter_dict, &entry))
			goto error;

		if (os_strcmp(entry.key, "service_type") == 0 &&
		    entry.type == DBUS_TYPE_STRING) {
			if (os_strcmp(entry.str_value, "upnp") == 0)
				upnp = 1;
			else if (os_strcmp(entry.str_value, "bonjour") == 0)
				bonjour = 1;
			else
				goto error_clear;
			wpa_dbus_dict_entry_clear(&entry);
		}
	}
	if (upnp == 1) {
		while (wpa_dbus_dict_has_dict_entry(&iter_dict)) {
			if (!wpa_dbus_dict_get_entry(&iter_dict, &entry))
				goto error;
			if (os_strcmp(entry.key, "version") == 0 &&
			    entry.type == DBUS_TYPE_INT32)
				version = entry.uint32_value;
			else if (os_strcmp(entry.key, "service") == 0 &&
				 entry.type == DBUS_TYPE_STRING) {
				os_free(service);
				service = os_strdup(entry.str_value);
			} else
				goto error_clear;

			wpa_dbus_dict_entry_clear(&entry);
		}

		if (version <= 0 || service == NULL)
			goto error;

		ret = wpas_p2p_service_del_upnp(wpa_s, version, service);
		if (ret != 0)
			goto error;
	} else if (bonjour == 1) {
		while (wpa_dbus_dict_has_dict_entry(&iter_dict)) {
			if (!wpa_dbus_dict_get_entry(&iter_dict, &entry))
				goto error;

			if (os_strcmp(entry.key, "query") == 0) {
				if (entry.type != DBUS_TYPE_ARRAY ||
				    entry.array_type != DBUS_TYPE_BYTE)
					goto error_clear;
				wpabuf_free(query);
				query = wpabuf_alloc_copy(
					entry.bytearray_value,
					entry.array_len);
			} else
				goto error_clear;

			wpa_dbus_dict_entry_clear(&entry);
		}

		if (query == NULL)
			goto error;

		ret = wpas_p2p_service_del_bonjour(wpa_s, query);
		if (ret != 0)
			goto error;
	} else
		goto error;

	wpabuf_free(query);
	os_free(service);
	return reply;
error_clear:
	wpa_dbus_dict_entry_clear(&entry);
error:
	wpabuf_free(query);
	os_free(service);
	return wpas_dbus_error_invalid_args(message, NULL);
}


DBusMessage * wpas_dbus_handler_p2p_flush_service(DBusMessage *message,
						  struct wpa_supplicant *wpa_s)
{
	wpas_p2p_service_flush(wpa_s);
	return NULL;
}


DBusMessage * wpas_dbus_handler_p2p_service_sd_req(
	DBusMessage *message, struct wpa_supplicant *wpa_s)
{
	DBusMessageIter iter_dict;
	DBusMessage *reply = NULL;
	DBusMessageIter iter;
	struct wpa_dbus_dict_entry entry;
	int upnp = 0;
	char *service = NULL;
	char *peer_object_path = NULL;
	struct wpabuf *tlv = NULL;
	u8 version = 0;
	u64 ref = 0;
	u8 addr_buf[ETH_ALEN], *addr;

	dbus_message_iter_init(message, &iter);

	if (!wpa_dbus_dict_open_read(&iter, &iter_dict, NULL))
		goto error;

	while (wpa_dbus_dict_has_dict_entry(&iter_dict)) {
		if (!wpa_dbus_dict_get_entry(&iter_dict, &entry))
			goto error;
		if (os_strcmp(entry.key, "peer_object") == 0 &&
		    entry.type == DBUS_TYPE_OBJECT_PATH) {
			peer_object_path = os_strdup(entry.str_value);
		} else if (os_strcmp(entry.key, "service_type") == 0 &&
			   entry.type == DBUS_TYPE_STRING) {
			if (os_strcmp(entry.str_value, "upnp") == 0)
				upnp = 1;
			else
				goto error_clear;
		} else if (os_strcmp(entry.key, "version") == 0 &&
			   entry.type == DBUS_TYPE_INT32) {
			version = entry.uint32_value;
		} else if (os_strcmp(entry.key, "service") == 0 &&
			   entry.type == DBUS_TYPE_STRING) {
			service = os_strdup(entry.str_value);
		} else if (os_strcmp(entry.key, "tlv") == 0) {
			if (entry.type != DBUS_TYPE_ARRAY ||
			    entry.array_type != DBUS_TYPE_BYTE)
				goto error_clear;
			tlv = wpabuf_alloc_copy(entry.bytearray_value,
						entry.array_len);
		} else
			goto error_clear;

		wpa_dbus_dict_entry_clear(&entry);
	}

	if (!peer_object_path) {
		addr = NULL;
	} else {
		if (parse_peer_object_path(peer_object_path, addr_buf) < 0 ||
		    !p2p_peer_known(wpa_s->global->p2p, addr_buf))
			goto error;

		addr = addr_buf;
	}

	if (upnp == 1) {
		if (version <= 0 || service == NULL)
			goto error;

		ref = wpas_p2p_sd_request_upnp(wpa_s, addr, version, service);
	} else {
		if (tlv == NULL)
			goto error;
		ref = wpas_p2p_sd_request(wpa_s, addr, tlv);
		wpabuf_free(tlv);
	}

	if (ref != 0) {
		reply = dbus_message_new_method_return(message);
		dbus_message_append_args(reply, DBUS_TYPE_UINT64,
					 &ref, DBUS_TYPE_INVALID);
	} else {
		reply = wpas_dbus_error_unknown_error(
			message, "Unable to send SD request");
	}
out:
	os_free(service);
	os_free(peer_object_path);
	return reply;
error_clear:
	wpa_dbus_dict_entry_clear(&entry);
error:
	if (tlv)
		wpabuf_free(tlv);
	reply = wpas_dbus_error_invalid_args(message, NULL);
	goto out;
}


DBusMessage * wpas_dbus_handler_p2p_service_sd_res(
	DBusMessage *message, struct wpa_supplicant *wpa_s)
{
	DBusMessageIter iter_dict;
	DBusMessage *reply = NULL;
	DBusMessageIter iter;
	struct wpa_dbus_dict_entry entry;
	char *peer_object_path = NULL;
	struct wpabuf *tlv = NULL;
	int freq = 0;
	int dlg_tok = 0;
	u8 addr[ETH_ALEN];

	dbus_message_iter_init(message, &iter);

	if (!wpa_dbus_dict_open_read(&iter, &iter_dict, NULL))
		goto error;

	while (wpa_dbus_dict_has_dict_entry(&iter_dict)) {
		if (!wpa_dbus_dict_get_entry(&iter_dict, &entry))
			goto error;

		if (os_strcmp(entry.key, "peer_object") == 0 &&
		    entry.type == DBUS_TYPE_OBJECT_PATH) {
			peer_object_path = os_strdup(entry.str_value);
		} else if (os_strcmp(entry.key, "frequency") == 0 &&
			   entry.type == DBUS_TYPE_INT32) {
			freq = entry.uint32_value;
		} else if (os_strcmp(entry.key, "dialog_token") == 0 &&
			   (entry.type == DBUS_TYPE_UINT32 ||
			    entry.type == DBUS_TYPE_INT32)) {
			dlg_tok = entry.uint32_value;
		} else if (os_strcmp(entry.key, "tlvs") == 0) {
			if (entry.type != DBUS_TYPE_ARRAY ||
			    entry.array_type != DBUS_TYPE_BYTE)
				goto error_clear;
			tlv = wpabuf_alloc_copy(entry.bytearray_value,
						entry.array_len);
		} else
			goto error_clear;

		wpa_dbus_dict_entry_clear(&entry);
	}
	if (parse_peer_object_path(peer_object_path, addr) < 0 ||
	    !p2p_peer_known(wpa_s->global->p2p, addr) ||
	    tlv == NULL)
		goto error;

	wpas_p2p_sd_response(wpa_s, freq, addr, (u8) dlg_tok, tlv);
	wpabuf_free(tlv);
out:
	os_free(peer_object_path);
	return reply;
error_clear:
	wpa_dbus_dict_entry_clear(&entry);
error:
	reply = wpas_dbus_error_invalid_args(message, NULL);
	goto out;
}


DBusMessage * wpas_dbus_handler_p2p_service_sd_cancel_req(
	DBusMessage *message, struct wpa_supplicant *wpa_s)
{
	DBusMessageIter iter;
	u64 req = 0;

	dbus_message_iter_init(message, &iter);
	dbus_message_iter_get_basic(&iter, &req);

	if (req == 0)
		goto error;

	if (wpas_p2p_sd_cancel_request(wpa_s, req) < 0)
		goto error;

	return NULL;
error:
	return wpas_dbus_error_invalid_args(message, NULL);
}


DBusMessage * wpas_dbus_handler_p2p_service_update(
	DBusMessage *message, struct wpa_supplicant *wpa_s)
{
	wpas_p2p_sd_service_update(wpa_s);
	return NULL;
}


DBusMessage * wpas_dbus_handler_p2p_serv_disc_external(
	DBusMessage *message, struct wpa_supplicant *wpa_s)
{
	DBusMessageIter iter;
	int ext = 0;

	dbus_message_iter_init(message, &iter);
	dbus_message_iter_get_basic(&iter, &ext);

	wpa_s->p2p_sd_over_ctrl_iface = ext;

	return NULL;

}


#ifdef CONFIG_WIFI_DISPLAY

dbus_bool_t wpas_dbus_getter_global_wfd_ies(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct wpa_global *global = user_data;
	struct wpabuf *ie;
	dbus_bool_t ret;

	ie = wifi_display_get_wfd_ie(global);
	if (ie == NULL)
		return wpas_dbus_simple_array_property_getter(iter,
							      DBUS_TYPE_BYTE,
							      NULL, 0, error);

	ret = wpas_dbus_simple_array_property_getter(iter, DBUS_TYPE_BYTE,
						     wpabuf_head(ie),
						     wpabuf_len(ie), error);
	wpabuf_free(ie);

	return ret;
}


dbus_bool_t wpas_dbus_setter_global_wfd_ies(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct wpa_global *global = user_data;
	DBusMessageIter variant, array;
	struct wpabuf *ie = NULL;
	const u8 *data;
	int len;

	if (dbus_message_iter_get_arg_type(iter) != DBUS_TYPE_VARIANT)
		goto err;

	dbus_message_iter_recurse(iter, &variant);
	if (dbus_message_iter_get_arg_type(&variant) != DBUS_TYPE_ARRAY)
		goto err;

	dbus_message_iter_recurse(&variant, &array);
	dbus_message_iter_get_fixed_array(&array, &data, &len);
	if (len == 0) {
		wifi_display_enable(global, 0);
		wifi_display_deinit(global);

		return TRUE;
	}

	ie = wpabuf_alloc(len);
	if (ie == NULL)
		goto err;

	wpabuf_put_data(ie, data, len);
	if (wifi_display_subelem_set_from_ies(global, ie) != 0)
		goto err;

	if (global->wifi_display == 0)
		wifi_display_enable(global, 1);

	wpabuf_free(ie);

	return TRUE;
err:
	wpabuf_free(ie);

	dbus_set_error_const(error, DBUS_ERROR_INVALID_ARGS,
			     "invalid message format");
	return FALSE;
}

#endif /* CONFIG_WIFI_DISPLAY */
