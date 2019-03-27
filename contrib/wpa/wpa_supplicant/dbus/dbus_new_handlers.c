/*
 * WPA Supplicant / dbus-based control interface
 * Copyright (c) 2006, Dan Williams <dcbw@redhat.com> and Red Hat, Inc.
 * Copyright (c) 2009-2010, Witold Sowa <witold.sowa@gmail.com>
 * Copyright (c) 2009-2015, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"

#include "common.h"
#include "common/ieee802_11_defs.h"
#include "eap_peer/eap_methods.h"
#include "eapol_supp/eapol_supp_sm.h"
#include "rsn_supp/wpa.h"
#include "../config.h"
#include "../wpa_supplicant_i.h"
#include "../driver_i.h"
#include "../notify.h"
#include "../bss.h"
#include "../scan.h"
#include "../autoscan.h"
#include "dbus_new_helpers.h"
#include "dbus_new.h"
#include "dbus_new_handlers.h"
#include "dbus_dict_helpers.h"
#include "dbus_common_i.h"
#include "drivers/driver.h"
#ifdef CONFIG_MESH
#include "ap/hostapd.h"
#include "ap/sta_info.h"
#endif /* CONFIG_MESH */

static const char * const debug_strings[] = {
	"excessive", "msgdump", "debug", "info", "warning", "error", NULL
};


/**
 * wpas_dbus_error_unknown_error - Return a new UnknownError error message
 * @message: Pointer to incoming dbus message this error refers to
 * @arg: Optional string appended to error message
 * Returns: a dbus error message
 *
 * Convenience function to create and return an UnknownError
 */
DBusMessage * wpas_dbus_error_unknown_error(DBusMessage *message,
					    const char *arg)
{
	return dbus_message_new_error(message, WPAS_DBUS_ERROR_UNKNOWN_ERROR,
				      arg);
}


/**
 * wpas_dbus_error_iface_unknown - Return a new invalid interface error message
 * @message: Pointer to incoming dbus message this error refers to
 * Returns: A dbus error message
 *
 * Convenience function to create and return an invalid interface error
 */
static DBusMessage * wpas_dbus_error_iface_unknown(DBusMessage *message)
{
	return dbus_message_new_error(
		message, WPAS_DBUS_ERROR_IFACE_UNKNOWN,
		"wpa_supplicant knows nothing about this interface.");
}


/**
 * wpas_dbus_error_network_unknown - Return a new NetworkUnknown error message
 * @message: Pointer to incoming dbus message this error refers to
 * Returns: a dbus error message
 *
 * Convenience function to create and return an invalid network error
 */
static DBusMessage * wpas_dbus_error_network_unknown(DBusMessage *message)
{
	return dbus_message_new_error(
		message, WPAS_DBUS_ERROR_NETWORK_UNKNOWN,
		"There is no such a network in this interface.");
}


/**
 * wpas_dbus_error_invalid_args - Return a new InvalidArgs error message
 * @message: Pointer to incoming dbus message this error refers to
 * Returns: a dbus error message
 *
 * Convenience function to create and return an invalid options error
 */
DBusMessage * wpas_dbus_error_invalid_args(DBusMessage *message,
					  const char *arg)
{
	DBusMessage *reply;

	reply = dbus_message_new_error(
		message, WPAS_DBUS_ERROR_INVALID_ARGS,
		"Did not receive correct message arguments.");
	if (arg != NULL)
		dbus_message_append_args(reply, DBUS_TYPE_STRING, &arg,
					 DBUS_TYPE_INVALID);

	return reply;
}


/**
 * wpas_dbus_error_scan_error - Return a new ScanError error message
 * @message: Pointer to incoming dbus message this error refers to
 * @error: Optional string to be used as the error message
 * Returns: a dbus error message
 *
 * Convenience function to create and return a scan error
 */
static DBusMessage * wpas_dbus_error_scan_error(DBusMessage *message,
						const char *error)
{
	return dbus_message_new_error(message,
				      WPAS_DBUS_ERROR_IFACE_SCAN_ERROR,
				      error);
}


DBusMessage * wpas_dbus_error_no_memory(DBusMessage *message)
{
	wpa_printf(MSG_DEBUG, "dbus: Failed to allocate memory");
	return dbus_message_new_error(message, DBUS_ERROR_NO_MEMORY, NULL);
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
 * get_iface_by_dbus_path - Get a new network interface
 * @global: Pointer to global data from wpa_supplicant_init()
 * @path: Pointer to a dbus object path representing an interface
 * Returns: Pointer to the interface or %NULL if not found
 */
static struct wpa_supplicant * get_iface_by_dbus_path(
	struct wpa_global *global, const char *path)
{
	struct wpa_supplicant *wpa_s;

	for (wpa_s = global->ifaces; wpa_s; wpa_s = wpa_s->next) {
		if (wpa_s->dbus_new_path &&
		    os_strcmp(wpa_s->dbus_new_path, path) == 0)
			return wpa_s;
	}
	return NULL;
}


/**
 * set_network_properties - Set properties of a configured network
 * @wpa_s: wpa_supplicant structure for a network interface
 * @ssid: wpa_ssid structure for a configured network
 * @iter: DBus message iterator containing dictionary of network
 * properties to set.
 * @error: On failure, an error describing the failure
 * Returns: TRUE if the request succeeds, FALSE if it failed
 *
 * Sets network configuration with parameters given id DBus dictionary
 */
dbus_bool_t set_network_properties(struct wpa_supplicant *wpa_s,
				   struct wpa_ssid *ssid,
				   DBusMessageIter *iter,
				   DBusError *error)
{
	struct wpa_dbus_dict_entry entry = { .type = DBUS_TYPE_STRING };
	DBusMessageIter	iter_dict;
	char *value = NULL;

	if (!wpa_dbus_dict_open_read(iter, &iter_dict, error))
		return FALSE;

	while (wpa_dbus_dict_has_dict_entry(&iter_dict)) {
		size_t size = 50;
		int ret;

		if (!wpa_dbus_dict_get_entry(&iter_dict, &entry))
			goto error;

		value = NULL;
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
				if (size == 0)
					goto error;

				size += 3;
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

		if (os_strcmp(entry.key, "bssid") != 0 &&
		    os_strcmp(entry.key, "priority") != 0)
			wpa_sm_pmksa_cache_flush(wpa_s->wpa, ssid);

		if (wpa_s->current_ssid == ssid ||
		    wpa_s->current_ssid == NULL) {
			/*
			 * Invalidate the EAP session cache if anything in the
			 * current or previously used configuration changes.
			 */
			eapol_sm_invalidate_cached_session(wpa_s->eapol);
		}

		if ((os_strcmp(entry.key, "psk") == 0 &&
		     value[0] == '"' && ssid->ssid_len) ||
		    (os_strcmp(entry.key, "ssid") == 0 && ssid->passphrase))
			wpa_config_update_psk(ssid);
		else if (os_strcmp(entry.key, "priority") == 0)
			wpa_config_update_prio_list(wpa_s->conf);

		os_free(value);
		value = NULL;
		wpa_dbus_dict_entry_clear(&entry);
	}

	return TRUE;

error:
	os_free(value);
	wpa_dbus_dict_entry_clear(&entry);
	dbus_set_error_const(error, DBUS_ERROR_INVALID_ARGS,
			     "invalid message format");
	return FALSE;
}


/**
 * wpas_dbus_simple_property_getter - Get basic type property
 * @iter: Message iter to use when appending arguments
 * @type: DBus type of property (must be basic type)
 * @val: pointer to place holding property value
 * @error: On failure an error describing the failure
 * Returns: TRUE if the request was successful, FALSE if it failed
 *
 * Generic getter for basic type properties. Type is required to be basic.
 */
dbus_bool_t wpas_dbus_simple_property_getter(DBusMessageIter *iter,
					     const int type,
					     const void *val,
					     DBusError *error)
{
	DBusMessageIter variant_iter;

	if (!dbus_type_is_basic(type)) {
		dbus_set_error(error, DBUS_ERROR_FAILED,
			       "%s: given type is not basic", __func__);
		return FALSE;
	}

	if (!dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT,
					      wpa_dbus_type_as_string(type),
					      &variant_iter) ||
	    !dbus_message_iter_append_basic(&variant_iter, type, val) ||
	    !dbus_message_iter_close_container(iter, &variant_iter)) {
		dbus_set_error(error, DBUS_ERROR_FAILED,
			       "%s: error constructing reply", __func__);
		return FALSE;
	}

	return TRUE;
}


/**
 * wpas_dbus_simple_property_setter - Set basic type property
 * @message: Pointer to incoming dbus message
 * @type: DBus type of property (must be basic type)
 * @val: pointer to place where value being set will be stored
 * Returns: TRUE if the request was successful, FALSE if it failed
 *
 * Generic setter for basic type properties. Type is required to be basic.
 */
dbus_bool_t wpas_dbus_simple_property_setter(DBusMessageIter *iter,
					     DBusError *error,
					     const int type, void *val)
{
	DBusMessageIter variant_iter;

	if (!dbus_type_is_basic(type)) {
		dbus_set_error(error, DBUS_ERROR_FAILED,
			       "%s: given type is not basic", __func__);
		return FALSE;
	}

	/* Look at the new value */
	dbus_message_iter_recurse(iter, &variant_iter);
	if (dbus_message_iter_get_arg_type(&variant_iter) != type) {
		dbus_set_error_const(error, DBUS_ERROR_FAILED,
				     "wrong property type");
		return FALSE;
	}
	dbus_message_iter_get_basic(&variant_iter, val);

	return TRUE;
}


/**
 * wpas_dbus_simple_array_property_getter - Get array type property
 * @iter: Pointer to incoming dbus message iterator
 * @type: DBus type of property array elements (must be basic type)
 * @array: pointer to array of elements to put into response message
 * @array_len: length of above array
 * @error: a pointer to an error to fill on failure
 * Returns: TRUE if the request succeeded, FALSE if it failed
 *
 * Generic getter for array type properties. Array elements type is
 * required to be basic.
 */
dbus_bool_t wpas_dbus_simple_array_property_getter(DBusMessageIter *iter,
						   const int type,
						   const void *array,
						   size_t array_len,
						   DBusError *error)
{
	DBusMessageIter variant_iter, array_iter;
	char type_str[] = "a?"; /* ? will be replaced with subtype letter; */
	const char *sub_type_str;
	size_t element_size, i;

	if (!dbus_type_is_basic(type)) {
		dbus_set_error(error, DBUS_ERROR_FAILED,
			       "%s: given type is not basic", __func__);
		return FALSE;
	}

	sub_type_str = wpa_dbus_type_as_string(type);
	type_str[1] = sub_type_str[0];

	if (!dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT,
					      type_str, &variant_iter) ||
	    !dbus_message_iter_open_container(&variant_iter, DBUS_TYPE_ARRAY,
					      sub_type_str, &array_iter)) {
		dbus_set_error(error, DBUS_ERROR_FAILED,
			       "%s: failed to construct message", __func__);
		return FALSE;
	}

	switch (type) {
	case DBUS_TYPE_BYTE:
	case DBUS_TYPE_BOOLEAN:
		element_size = 1;
		break;
	case DBUS_TYPE_INT16:
	case DBUS_TYPE_UINT16:
		element_size = sizeof(uint16_t);
		break;
	case DBUS_TYPE_INT32:
	case DBUS_TYPE_UINT32:
		element_size = sizeof(uint32_t);
		break;
	case DBUS_TYPE_INT64:
	case DBUS_TYPE_UINT64:
		element_size = sizeof(uint64_t);
		break;
	case DBUS_TYPE_DOUBLE:
		element_size = sizeof(double);
		break;
	case DBUS_TYPE_STRING:
	case DBUS_TYPE_OBJECT_PATH:
		element_size = sizeof(char *);
		break;
	default:
		dbus_set_error(error, DBUS_ERROR_FAILED,
			       "%s: unknown element type %d", __func__, type);
		return FALSE;
	}

	for (i = 0; i < array_len; i++) {
		if (!dbus_message_iter_append_basic(&array_iter, type,
						    (const char *) array +
						    i * element_size)) {
			dbus_set_error(error, DBUS_ERROR_FAILED,
				       "%s: failed to construct message 2.5",
				       __func__);
			return FALSE;
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


/**
 * wpas_dbus_simple_array_array_property_getter - Get array array type property
 * @iter: Pointer to incoming dbus message iterator
 * @type: DBus type of property array elements (must be basic type)
 * @array: pointer to array of elements to put into response message
 * @array_len: length of above array
 * @error: a pointer to an error to fill on failure
 * Returns: TRUE if the request succeeded, FALSE if it failed
 *
 * Generic getter for array type properties. Array elements type is
 * required to be basic.
 */
dbus_bool_t wpas_dbus_simple_array_array_property_getter(DBusMessageIter *iter,
							 const int type,
							 struct wpabuf **array,
							 size_t array_len,
							 DBusError *error)
{
	DBusMessageIter variant_iter, array_iter;
	char type_str[] = "aa?";
	char inner_type_str[] = "a?";
	const char *sub_type_str;
	size_t i;

	if (!dbus_type_is_basic(type)) {
		dbus_set_error(error, DBUS_ERROR_FAILED,
			       "%s: given type is not basic", __func__);
		return FALSE;
	}

	sub_type_str = wpa_dbus_type_as_string(type);
	type_str[2] = sub_type_str[0];
	inner_type_str[1] = sub_type_str[0];

	if (!dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT,
					      type_str, &variant_iter) ||
	    !dbus_message_iter_open_container(&variant_iter, DBUS_TYPE_ARRAY,
					      inner_type_str, &array_iter)) {
		dbus_set_error(error, DBUS_ERROR_FAILED,
			       "%s: failed to construct message", __func__);
		return FALSE;
	}

	for (i = 0; i < array_len && array[i]; i++) {
		wpa_dbus_dict_bin_array_add_element(&array_iter,
						    wpabuf_head(array[i]),
						    wpabuf_len(array[i]));

	}

	if (!dbus_message_iter_close_container(&variant_iter, &array_iter) ||
	    !dbus_message_iter_close_container(iter, &variant_iter)) {
		dbus_set_error(error, DBUS_ERROR_FAILED,
			       "%s: failed to close message", __func__);
		return FALSE;
	}

	return TRUE;
}


/**
 * wpas_dbus_string_property_getter - Get string type property
 * @iter: Message iter to use when appending arguments
 * @val: Pointer to place holding property value, can be %NULL
 * @error: On failure an error describing the failure
 * Returns: TRUE if the request was successful, FALSE if it failed
 *
 * Generic getter for string type properties. %NULL is converted to an empty
 * string.
 */
dbus_bool_t wpas_dbus_string_property_getter(DBusMessageIter *iter,
					     const void *val,
					     DBusError *error)
{
	if (!val)
		val = "";
	return wpas_dbus_simple_property_getter(iter, DBUS_TYPE_STRING,
						&val, error);
}


/**
 * wpas_dbus_handler_create_interface - Request registration of a network iface
 * @message: Pointer to incoming dbus message
 * @global: %wpa_supplicant global data structure
 * Returns: The object path of the new interface object,
 *          or a dbus error message with more information
 *
 * Handler function for "CreateInterface" method call. Handles requests
 * by dbus clients to register a network interface that wpa_supplicant
 * will manage.
 */
DBusMessage * wpas_dbus_handler_create_interface(DBusMessage *message,
						 struct wpa_global *global)
{
	DBusMessageIter iter_dict;
	DBusMessage *reply = NULL;
	DBusMessageIter iter;
	struct wpa_dbus_dict_entry entry;
	char *driver = NULL;
	char *ifname = NULL;
	char *confname = NULL;
	char *bridge_ifname = NULL;

	dbus_message_iter_init(message, &iter);

	if (!wpa_dbus_dict_open_read(&iter, &iter_dict, NULL))
		goto error;
	while (wpa_dbus_dict_has_dict_entry(&iter_dict)) {
		if (!wpa_dbus_dict_get_entry(&iter_dict, &entry))
			goto error;
		if (os_strcmp(entry.key, "Driver") == 0 &&
		    entry.type == DBUS_TYPE_STRING) {
			os_free(driver);
			driver = os_strdup(entry.str_value);
			wpa_dbus_dict_entry_clear(&entry);
			if (driver == NULL)
				goto oom;
		} else if (os_strcmp(entry.key, "Ifname") == 0 &&
			   entry.type == DBUS_TYPE_STRING) {
			os_free(ifname);
			ifname = os_strdup(entry.str_value);
			wpa_dbus_dict_entry_clear(&entry);
			if (ifname == NULL)
				goto oom;
		} else if (os_strcmp(entry.key, "ConfigFile") == 0 &&
			   entry.type == DBUS_TYPE_STRING) {
			os_free(confname);
			confname = os_strdup(entry.str_value);
			wpa_dbus_dict_entry_clear(&entry);
			if (confname == NULL)
				goto oom;
		} else if (os_strcmp(entry.key, "BridgeIfname") == 0 &&
			   entry.type == DBUS_TYPE_STRING) {
			os_free(bridge_ifname);
			bridge_ifname = os_strdup(entry.str_value);
			wpa_dbus_dict_entry_clear(&entry);
			if (bridge_ifname == NULL)
				goto oom;
		} else {
			wpa_dbus_dict_entry_clear(&entry);
			goto error;
		}
	}

	if (ifname == NULL)
		goto error; /* Required Ifname argument missing */

	/*
	 * Try to get the wpa_supplicant record for this iface, return
	 * an error if we already control it.
	 */
	if (wpa_supplicant_get_iface(global, ifname) != NULL) {
		reply = dbus_message_new_error(
			message, WPAS_DBUS_ERROR_IFACE_EXISTS,
			"wpa_supplicant already controls this interface.");
	} else {
		struct wpa_supplicant *wpa_s;
		struct wpa_interface iface;

		os_memset(&iface, 0, sizeof(iface));
		iface.driver = driver;
		iface.ifname = ifname;
		iface.confname = confname;
		iface.bridge_ifname = bridge_ifname;
		/* Otherwise, have wpa_supplicant attach to it. */
		wpa_s = wpa_supplicant_add_iface(global, &iface, NULL);
		if (wpa_s && wpa_s->dbus_new_path) {
			const char *path = wpa_s->dbus_new_path;

			reply = dbus_message_new_method_return(message);
			dbus_message_append_args(reply, DBUS_TYPE_OBJECT_PATH,
						 &path, DBUS_TYPE_INVALID);
		} else {
			reply = wpas_dbus_error_unknown_error(
				message,
				"wpa_supplicant couldn't grab this interface.");
		}
	}

out:
	os_free(driver);
	os_free(ifname);
	os_free(confname);
	os_free(bridge_ifname);
	return reply;

error:
	reply = wpas_dbus_error_invalid_args(message, NULL);
	goto out;
oom:
	reply = wpas_dbus_error_no_memory(message);
	goto out;
}


/**
 * wpas_dbus_handler_remove_interface - Request deregistration of an interface
 * @message: Pointer to incoming dbus message
 * @global: wpa_supplicant global data structure
 * Returns: a dbus message containing a UINT32 indicating success (1) or
 *          failure (0), or returns a dbus error message with more information
 *
 * Handler function for "removeInterface" method call.  Handles requests
 * by dbus clients to deregister a network interface that wpa_supplicant
 * currently manages.
 */
DBusMessage * wpas_dbus_handler_remove_interface(DBusMessage *message,
						 struct wpa_global *global)
{
	struct wpa_supplicant *wpa_s;
	char *path;
	DBusMessage *reply = NULL;

	dbus_message_get_args(message, NULL, DBUS_TYPE_OBJECT_PATH, &path,
			      DBUS_TYPE_INVALID);

	wpa_s = get_iface_by_dbus_path(global, path);
	if (wpa_s == NULL)
		reply = wpas_dbus_error_iface_unknown(message);
	else if (wpa_supplicant_remove_iface(global, wpa_s, 0)) {
		reply = wpas_dbus_error_unknown_error(
			message,
			"wpa_supplicant couldn't remove this interface.");
	}

	return reply;
}


/**
 * wpas_dbus_handler_get_interface - Get the object path for an interface name
 * @message: Pointer to incoming dbus message
 * @global: %wpa_supplicant global data structure
 * Returns: The object path of the interface object,
 *          or a dbus error message with more information
 *
 * Handler function for "getInterface" method call.
 */
DBusMessage * wpas_dbus_handler_get_interface(DBusMessage *message,
					      struct wpa_global *global)
{
	DBusMessage *reply = NULL;
	const char *ifname;
	const char *path;
	struct wpa_supplicant *wpa_s;

	dbus_message_get_args(message, NULL, DBUS_TYPE_STRING, &ifname,
			      DBUS_TYPE_INVALID);

	wpa_s = wpa_supplicant_get_iface(global, ifname);
	if (wpa_s == NULL || wpa_s->dbus_new_path == NULL)
		return wpas_dbus_error_iface_unknown(message);

	path = wpa_s->dbus_new_path;
	reply = dbus_message_new_method_return(message);
	if (reply == NULL)
		return wpas_dbus_error_no_memory(message);
	if (!dbus_message_append_args(reply, DBUS_TYPE_OBJECT_PATH, &path,
				      DBUS_TYPE_INVALID)) {
		dbus_message_unref(reply);
		return wpas_dbus_error_no_memory(message);
	}

	return reply;
}


/**
 * wpas_dbus_getter_debug_level - Get debug level
 * @iter: Pointer to incoming dbus message iter
 * @error: Location to store error on failure
 * @user_data: Function specific data
 * Returns: TRUE on success, FALSE on failure
 *
 * Getter for "DebugLevel" property.
 */
dbus_bool_t wpas_dbus_getter_debug_level(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	const char *str;
	int idx = wpa_debug_level;

	if (idx < 0)
		idx = 0;
	if (idx > 5)
		idx = 5;
	str = debug_strings[idx];
	return wpas_dbus_simple_property_getter(iter, DBUS_TYPE_STRING,
						&str, error);
}


/**
 * wpas_dbus_getter_debug_timestamp - Get debug timestamp
 * @iter: Pointer to incoming dbus message iter
 * @error: Location to store error on failure
 * @user_data: Function specific data
 * Returns: TRUE on success, FALSE on failure
 *
 * Getter for "DebugTimestamp" property.
 */
dbus_bool_t wpas_dbus_getter_debug_timestamp(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	return wpas_dbus_simple_property_getter(iter, DBUS_TYPE_BOOLEAN,
						&wpa_debug_timestamp, error);

}


/**
 * wpas_dbus_getter_debug_show_keys - Get debug show keys
 * @iter: Pointer to incoming dbus message iter
 * @error: Location to store error on failure
 * @user_data: Function specific data
 * Returns: TRUE on success, FALSE on failure
 *
 * Getter for "DebugShowKeys" property.
 */
dbus_bool_t wpas_dbus_getter_debug_show_keys(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	return wpas_dbus_simple_property_getter(iter, DBUS_TYPE_BOOLEAN,
						&wpa_debug_show_keys, error);

}

/**
 * wpas_dbus_setter_debug_level - Set debug level
 * @iter: Pointer to incoming dbus message iter
 * @error: Location to store error on failure
 * @user_data: Function specific data
 * Returns: TRUE on success, FALSE on failure
 *
 * Setter for "DebugLevel" property.
 */
dbus_bool_t wpas_dbus_setter_debug_level(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct wpa_global *global = user_data;
	const char *str = NULL;
	int i, val = -1;

	if (!wpas_dbus_simple_property_setter(iter, error, DBUS_TYPE_STRING,
					      &str))
		return FALSE;

	for (i = 0; debug_strings[i]; i++)
		if (os_strcmp(debug_strings[i], str) == 0) {
			val = i;
			break;
		}

	if (val < 0 ||
	    wpa_supplicant_set_debug_params(global, val, wpa_debug_timestamp,
					    wpa_debug_show_keys)) {
		dbus_set_error_const(error, DBUS_ERROR_FAILED,
				     "wrong debug level value");
		return FALSE;
	}

	return TRUE;
}


/**
 * wpas_dbus_setter_debug_timestamp - Set debug timestamp
 * @iter: Pointer to incoming dbus message iter
 * @error: Location to store error on failure
 * @user_data: Function specific data
 * Returns: TRUE on success, FALSE on failure
 *
 * Setter for "DebugTimestamp" property.
 */
dbus_bool_t wpas_dbus_setter_debug_timestamp(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct wpa_global *global = user_data;
	dbus_bool_t val;

	if (!wpas_dbus_simple_property_setter(iter, error, DBUS_TYPE_BOOLEAN,
					      &val))
		return FALSE;

	wpa_supplicant_set_debug_params(global, wpa_debug_level, val ? 1 : 0,
					wpa_debug_show_keys);
	return TRUE;
}


/**
 * wpas_dbus_setter_debug_show_keys - Set debug show keys
 * @iter: Pointer to incoming dbus message iter
 * @error: Location to store error on failure
 * @user_data: Function specific data
 * Returns: TRUE on success, FALSE on failure
 *
 * Setter for "DebugShowKeys" property.
 */
dbus_bool_t wpas_dbus_setter_debug_show_keys(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct wpa_global *global = user_data;
	dbus_bool_t val;

	if (!wpas_dbus_simple_property_setter(iter, error, DBUS_TYPE_BOOLEAN,
					      &val))
		return FALSE;

	wpa_supplicant_set_debug_params(global, wpa_debug_level,
					wpa_debug_timestamp,
					val ? 1 : 0);
	return TRUE;
}


/**
 * wpas_dbus_getter_interfaces - Request registered interfaces list
 * @iter: Pointer to incoming dbus message iter
 * @error: Location to store error on failure
 * @user_data: Function specific data
 * Returns: TRUE on success, FALSE on failure
 *
 * Getter for "Interfaces" property. Handles requests
 * by dbus clients to return list of registered interfaces objects
 * paths
 */
dbus_bool_t wpas_dbus_getter_interfaces(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct wpa_global *global = user_data;
	struct wpa_supplicant *wpa_s;
	const char **paths;
	unsigned int i = 0, num = 0;
	dbus_bool_t success;

	for (wpa_s = global->ifaces; wpa_s; wpa_s = wpa_s->next) {
		if (wpa_s->dbus_new_path)
			num++;
	}

	paths = os_calloc(num, sizeof(char *));
	if (!paths) {
		dbus_set_error_const(error, DBUS_ERROR_NO_MEMORY, "no memory");
		return FALSE;
	}

	for (wpa_s = global->ifaces; wpa_s; wpa_s = wpa_s->next) {
		if (wpa_s->dbus_new_path)
			paths[i++] = wpa_s->dbus_new_path;
	}

	success = wpas_dbus_simple_array_property_getter(iter,
							 DBUS_TYPE_OBJECT_PATH,
							 paths, num, error);

	os_free(paths);
	return success;
}


/**
 * wpas_dbus_getter_eap_methods - Request supported EAP methods list
 * @iter: Pointer to incoming dbus message iter
 * @error: Location to store error on failure
 * @user_data: Function specific data
 * Returns: TRUE on success, FALSE on failure
 *
 * Getter for "EapMethods" property. Handles requests
 * by dbus clients to return list of strings with supported EAP methods
 */
dbus_bool_t wpas_dbus_getter_eap_methods(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	char **eap_methods;
	size_t num_items = 0;
	dbus_bool_t success;

	eap_methods = eap_get_names_as_string_array(&num_items);
	if (!eap_methods) {
		dbus_set_error_const(error, DBUS_ERROR_NO_MEMORY, "no memory");
		return FALSE;
	}

	success = wpas_dbus_simple_array_property_getter(iter,
							 DBUS_TYPE_STRING,
							 eap_methods,
							 num_items, error);

	while (num_items)
		os_free(eap_methods[--num_items]);
	os_free(eap_methods);
	return success;
}


/**
 * wpas_dbus_getter_global_capabilities - Request supported global capabilities
 * @iter: Pointer to incoming dbus message iter
 * @error: Location to store error on failure
 * @user_data: Function specific data
 * Returns: TRUE on success, FALSE on failure
 *
 * Getter for "Capabilities" property. Handles requests by dbus clients to
 * return a list of strings with supported capabilities like AP, RSN IBSS,
 * and P2P that are determined at compile time.
 */
dbus_bool_t wpas_dbus_getter_global_capabilities(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	const char *capabilities[10] = { NULL, NULL, NULL, NULL, NULL, NULL,
					NULL, NULL, NULL, NULL };
	size_t num_items = 0;
#ifdef CONFIG_FILS
	struct wpa_global *global = user_data;
	struct wpa_supplicant *wpa_s;
	int fils_supported = 0, fils_sk_pfs_supported = 0;

	for (wpa_s = global->ifaces; wpa_s; wpa_s = wpa_s->next) {
		if (wpa_is_fils_supported(wpa_s))
			fils_supported = 1;
		if (wpa_is_fils_sk_pfs_supported(wpa_s))
			fils_sk_pfs_supported = 1;
	}
#endif /* CONFIG_FILS */

#ifdef CONFIG_AP
	capabilities[num_items++] = "ap";
#endif /* CONFIG_AP */
#ifdef CONFIG_IBSS_RSN
	capabilities[num_items++] = "ibss-rsn";
#endif /* CONFIG_IBSS_RSN */
#ifdef CONFIG_P2P
	capabilities[num_items++] = "p2p";
#endif /* CONFIG_P2P */
#ifdef CONFIG_INTERWORKING
	capabilities[num_items++] = "interworking";
#endif /* CONFIG_INTERWORKING */
#ifdef CONFIG_IEEE80211W
	capabilities[num_items++] = "pmf";
#endif /* CONFIG_IEEE80211W */
#ifdef CONFIG_MESH
	capabilities[num_items++] = "mesh";
#endif /* CONFIG_MESH */
#ifdef CONFIG_FILS
	if (fils_supported)
		capabilities[num_items++] = "fils";
	if (fils_sk_pfs_supported)
		capabilities[num_items++] = "fils_sk_pfs";
#endif /* CONFIG_FILS */
#ifdef CONFIG_IEEE80211R
	capabilities[num_items++] = "ft";
#endif /* CONFIG_IEEE80211R */
#ifdef CONFIG_SHA384
	capabilities[num_items++] = "sha384";
#endif /* CONFIG_SHA384 */

	return wpas_dbus_simple_array_property_getter(iter,
						      DBUS_TYPE_STRING,
						      capabilities,
						      num_items, error);
}


static int wpas_dbus_get_scan_type(DBusMessage *message, DBusMessageIter *var,
				   char **type, DBusMessage **reply)
{
	if (dbus_message_iter_get_arg_type(var) != DBUS_TYPE_STRING) {
		wpa_printf(MSG_DEBUG, "%s[dbus]: Type must be a string",
			   __func__);
		*reply = wpas_dbus_error_invalid_args(
			message, "Wrong Type value type. String required");
		return -1;
	}
	dbus_message_iter_get_basic(var, type);
	return 0;
}


static int wpas_dbus_get_scan_ssids(DBusMessage *message, DBusMessageIter *var,
				    struct wpa_driver_scan_params *params,
				    DBusMessage **reply)
{
	struct wpa_driver_scan_ssid *ssids = params->ssids;
	size_t ssids_num = 0;
	u8 *ssid;
	DBusMessageIter array_iter, sub_array_iter;
	char *val;
	int len;

	if (dbus_message_iter_get_arg_type(var) != DBUS_TYPE_ARRAY) {
		wpa_printf(MSG_DEBUG,
			   "%s[dbus]: ssids must be an array of arrays of bytes",
			   __func__);
		*reply = wpas_dbus_error_invalid_args(
			message,
			"Wrong SSIDs value type. Array of arrays of bytes required");
		return -1;
	}

	dbus_message_iter_recurse(var, &array_iter);

	if (dbus_message_iter_get_arg_type(&array_iter) != DBUS_TYPE_ARRAY ||
	    dbus_message_iter_get_element_type(&array_iter) != DBUS_TYPE_BYTE) {
		wpa_printf(MSG_DEBUG,
			   "%s[dbus]: ssids must be an array of arrays of bytes",
			   __func__);
		*reply = wpas_dbus_error_invalid_args(
			message,
			"Wrong SSIDs value type. Array of arrays of bytes required");
		return -1;
	}

	while (dbus_message_iter_get_arg_type(&array_iter) == DBUS_TYPE_ARRAY) {
		if (ssids_num >= WPAS_MAX_SCAN_SSIDS) {
			wpa_printf(MSG_DEBUG,
				   "%s[dbus]: Too many ssids specified on scan dbus call",
				   __func__);
			*reply = wpas_dbus_error_invalid_args(
				message,
				"Too many ssids specified. Specify at most four");
			return -1;
		}

		dbus_message_iter_recurse(&array_iter, &sub_array_iter);

		dbus_message_iter_get_fixed_array(&sub_array_iter, &val, &len);

		if (len > SSID_MAX_LEN) {
			wpa_printf(MSG_DEBUG,
				   "%s[dbus]: SSID too long (len=%d max_len=%d)",
				   __func__, len, SSID_MAX_LEN);
			*reply = wpas_dbus_error_invalid_args(
				message, "Invalid SSID: too long");
			return -1;
		}

		if (len != 0) {
			ssid = os_memdup(val, len);
			if (ssid == NULL) {
				*reply = wpas_dbus_error_no_memory(message);
				return -1;
			}
		} else {
			/* Allow zero-length SSIDs */
			ssid = NULL;
		}

		ssids[ssids_num].ssid = ssid;
		ssids[ssids_num].ssid_len = len;

		dbus_message_iter_next(&array_iter);
		ssids_num++;
	}

	params->num_ssids = ssids_num;
	return 0;
}


static int wpas_dbus_get_scan_ies(DBusMessage *message, DBusMessageIter *var,
				  struct wpa_driver_scan_params *params,
				  DBusMessage **reply)
{
	u8 *ies = NULL, *nies;
	int ies_len = 0;
	DBusMessageIter array_iter, sub_array_iter;
	char *val;
	int len;

	if (dbus_message_iter_get_arg_type(var) != DBUS_TYPE_ARRAY) {
		wpa_printf(MSG_DEBUG,
			   "%s[dbus]: ies must be an array of arrays of bytes",
			   __func__);
		*reply = wpas_dbus_error_invalid_args(
			message,
			"Wrong IEs value type. Array of arrays of bytes required");
		return -1;
	}

	dbus_message_iter_recurse(var, &array_iter);

	if (dbus_message_iter_get_arg_type(&array_iter) != DBUS_TYPE_ARRAY ||
	    dbus_message_iter_get_element_type(&array_iter) != DBUS_TYPE_BYTE) {
		wpa_printf(MSG_DEBUG,
			   "%s[dbus]: ies must be an array of arrays of bytes",
			   __func__);
		*reply = wpas_dbus_error_invalid_args(
			message, "Wrong IEs value type. Array required");
		return -1;
	}

	while (dbus_message_iter_get_arg_type(&array_iter) == DBUS_TYPE_ARRAY) {
		dbus_message_iter_recurse(&array_iter, &sub_array_iter);

		dbus_message_iter_get_fixed_array(&sub_array_iter, &val, &len);
		if (len == 0) {
			dbus_message_iter_next(&array_iter);
			continue;
		}

		nies = os_realloc(ies, ies_len + len);
		if (nies == NULL) {
			os_free(ies);
			*reply = wpas_dbus_error_no_memory(message);
			return -1;
		}
		ies = nies;
		os_memcpy(ies + ies_len, val, len);
		ies_len += len;

		dbus_message_iter_next(&array_iter);
	}

	params->extra_ies = ies;
	params->extra_ies_len = ies_len;
	return 0;
}


static int wpas_dbus_get_scan_channels(DBusMessage *message,
				       DBusMessageIter *var,
				       struct wpa_driver_scan_params *params,
				       DBusMessage **reply)
{
	DBusMessageIter array_iter, sub_array_iter;
	int *freqs = NULL, *nfreqs;
	int freqs_num = 0;

	if (dbus_message_iter_get_arg_type(var) != DBUS_TYPE_ARRAY) {
		wpa_printf(MSG_DEBUG,
			   "%s[dbus]: Channels must be an array of structs",
			   __func__);
		*reply = wpas_dbus_error_invalid_args(
			message,
			"Wrong Channels value type. Array of structs required");
		return -1;
	}

	dbus_message_iter_recurse(var, &array_iter);

	if (dbus_message_iter_get_arg_type(&array_iter) != DBUS_TYPE_STRUCT) {
		wpa_printf(MSG_DEBUG,
			   "%s[dbus]: Channels must be an array of structs",
			   __func__);
		*reply = wpas_dbus_error_invalid_args(
			message,
			"Wrong Channels value type. Array of structs required");
		return -1;
	}

	while (dbus_message_iter_get_arg_type(&array_iter) == DBUS_TYPE_STRUCT)
	{
		int freq, width;

		dbus_message_iter_recurse(&array_iter, &sub_array_iter);

		if (dbus_message_iter_get_arg_type(&sub_array_iter) !=
		    DBUS_TYPE_UINT32) {
			wpa_printf(MSG_DEBUG,
				   "%s[dbus]: Channel must by specified by struct of two UINT32s %c",
				   __func__,
				   dbus_message_iter_get_arg_type(
					   &sub_array_iter));
			*reply = wpas_dbus_error_invalid_args(
				message,
				"Wrong Channel struct. Two UINT32s required");
			os_free(freqs);
			return -1;
		}
		dbus_message_iter_get_basic(&sub_array_iter, &freq);

		if (!dbus_message_iter_next(&sub_array_iter) ||
		    dbus_message_iter_get_arg_type(&sub_array_iter) !=
		    DBUS_TYPE_UINT32) {
			wpa_printf(MSG_DEBUG,
				   "%s[dbus]: Channel must by specified by struct of two UINT32s",
				   __func__);
			*reply = wpas_dbus_error_invalid_args(
				message,
				"Wrong Channel struct. Two UINT32s required");
			os_free(freqs);
			return -1;
		}

		dbus_message_iter_get_basic(&sub_array_iter, &width);

#define FREQS_ALLOC_CHUNK 32
		if (freqs_num % FREQS_ALLOC_CHUNK == 0) {
			nfreqs = os_realloc_array(
				freqs, freqs_num + FREQS_ALLOC_CHUNK,
				sizeof(int));
			if (nfreqs == NULL)
				os_free(freqs);
			freqs = nfreqs;
		}
		if (freqs == NULL) {
			*reply = wpas_dbus_error_no_memory(message);
			return -1;
		}

		freqs[freqs_num] = freq;

		freqs_num++;
		dbus_message_iter_next(&array_iter);
	}

	nfreqs = os_realloc_array(freqs, freqs_num + 1, sizeof(int));
	if (nfreqs == NULL)
		os_free(freqs);
	freqs = nfreqs;
	if (freqs == NULL) {
		*reply = wpas_dbus_error_no_memory(message);
		return -1;
	}
	freqs[freqs_num] = 0;

	params->freqs = freqs;
	return 0;
}


static int wpas_dbus_get_scan_allow_roam(DBusMessage *message,
					 DBusMessageIter *var,
					 dbus_bool_t *allow,
					 DBusMessage **reply)
{
	if (dbus_message_iter_get_arg_type(var) != DBUS_TYPE_BOOLEAN) {
		wpa_printf(MSG_DEBUG, "%s[dbus]: Type must be a boolean",
			   __func__);
		*reply = wpas_dbus_error_invalid_args(
			message, "Wrong Type value type. Boolean required");
		return -1;
	}
	dbus_message_iter_get_basic(var, allow);
	return 0;
}


/**
 * wpas_dbus_handler_scan - Request a wireless scan on an interface
 * @message: Pointer to incoming dbus message
 * @wpa_s: wpa_supplicant structure for a network interface
 * Returns: NULL indicating success or DBus error message on failure
 *
 * Handler function for "Scan" method call of a network device. Requests
 * that wpa_supplicant perform a wireless scan as soon as possible
 * on a particular wireless interface.
 */
DBusMessage * wpas_dbus_handler_scan(DBusMessage *message,
				     struct wpa_supplicant *wpa_s)
{
	DBusMessage *reply = NULL;
	DBusMessageIter iter, dict_iter, entry_iter, variant_iter;
	char *key = NULL, *type = NULL;
	struct wpa_driver_scan_params params;
	size_t i;
	dbus_bool_t allow_roam = 1;

	os_memset(&params, 0, sizeof(params));

	dbus_message_iter_init(message, &iter);

	dbus_message_iter_recurse(&iter, &dict_iter);

	while (dbus_message_iter_get_arg_type(&dict_iter) ==
	       DBUS_TYPE_DICT_ENTRY) {
		dbus_message_iter_recurse(&dict_iter, &entry_iter);
		dbus_message_iter_get_basic(&entry_iter, &key);
		dbus_message_iter_next(&entry_iter);
		dbus_message_iter_recurse(&entry_iter, &variant_iter);

		if (os_strcmp(key, "Type") == 0) {
			if (wpas_dbus_get_scan_type(message, &variant_iter,
						    &type, &reply) < 0)
				goto out;
		} else if (os_strcmp(key, "SSIDs") == 0) {
			if (wpas_dbus_get_scan_ssids(message, &variant_iter,
						     &params, &reply) < 0)
				goto out;
		} else if (os_strcmp(key, "IEs") == 0) {
			if (wpas_dbus_get_scan_ies(message, &variant_iter,
						   &params, &reply) < 0)
				goto out;
		} else if (os_strcmp(key, "Channels") == 0) {
			if (wpas_dbus_get_scan_channels(message, &variant_iter,
							&params, &reply) < 0)
				goto out;
		} else if (os_strcmp(key, "AllowRoam") == 0) {
			if (wpas_dbus_get_scan_allow_roam(message,
							  &variant_iter,
							  &allow_roam,
							  &reply) < 0)
				goto out;
		} else {
			wpa_printf(MSG_DEBUG, "%s[dbus]: Unknown argument %s",
				   __func__, key);
			reply = wpas_dbus_error_invalid_args(message, key);
			goto out;
		}

		dbus_message_iter_next(&dict_iter);
	}

	if (!type) {
		wpa_printf(MSG_DEBUG, "%s[dbus]: Scan type not specified",
			   __func__);
		reply = wpas_dbus_error_invalid_args(message, key);
		goto out;
	}

	if (os_strcmp(type, "passive") == 0) {
		if (params.num_ssids || params.extra_ies_len) {
			wpa_printf(MSG_DEBUG,
				   "%s[dbus]: SSIDs or IEs specified for passive scan.",
				   __func__);
			reply = wpas_dbus_error_invalid_args(
				message,
				"You can specify only Channels in passive scan");
			goto out;
		} else {
			if (wpa_s->sched_scanning) {
				wpa_printf(MSG_DEBUG,
					   "%s[dbus]: Stop ongoing sched_scan to allow requested scan to proceed",
					   __func__);
				wpa_supplicant_cancel_sched_scan(wpa_s);
			}

			if (params.freqs && params.freqs[0]) {
				wpa_s->last_scan_req = MANUAL_SCAN_REQ;
				if (wpa_supplicant_trigger_scan(wpa_s,
								&params)) {
					reply = wpas_dbus_error_scan_error(
						message,
						"Scan request rejected");
				}
			} else {
				wpa_s->scan_req = MANUAL_SCAN_REQ;
				wpa_supplicant_req_scan(wpa_s, 0, 0);
			}
		}
	} else if (os_strcmp(type, "active") == 0) {
		if (!params.num_ssids) {
			/* Add wildcard ssid */
			params.num_ssids++;
		}
#ifdef CONFIG_AUTOSCAN
		autoscan_deinit(wpa_s);
#endif /* CONFIG_AUTOSCAN */
		if (wpa_s->sched_scanning) {
			wpa_printf(MSG_DEBUG,
				   "%s[dbus]: Stop ongoing sched_scan to allow requested scan to proceed",
				   __func__);
			wpa_supplicant_cancel_sched_scan(wpa_s);
		}

		wpa_s->last_scan_req = MANUAL_SCAN_REQ;
		if (wpa_supplicant_trigger_scan(wpa_s, &params)) {
			reply = wpas_dbus_error_scan_error(
				message, "Scan request rejected");
		}
	} else {
		wpa_printf(MSG_DEBUG, "%s[dbus]: Unknown scan type: %s",
			   __func__, type);
		reply = wpas_dbus_error_invalid_args(message,
						     "Wrong scan type");
		goto out;
	}

	if (!allow_roam)
		wpa_s->scan_res_handler = scan_only_handler;

out:
	for (i = 0; i < WPAS_MAX_SCAN_SSIDS; i++)
		os_free((u8 *) params.ssids[i].ssid);
	os_free((u8 *) params.extra_ies);
	os_free(params.freqs);
	return reply;
}


/*
 * wpas_dbus_handler_abort_scan - Request an ongoing scan to be aborted
 * @message: Pointer to incoming dbus message
 * @wpa_s: wpa_supplicant structure for a network interface
 * Returns: Abort failed or no scan in progress DBus error message on failure
 * or NULL otherwise.
 *
 * Handler function for "AbortScan" method call of network interface.
 */
DBusMessage * wpas_dbus_handler_abort_scan(DBusMessage *message,
					   struct wpa_supplicant *wpa_s)
{
	if (wpas_abort_ongoing_scan(wpa_s) < 0)
		return dbus_message_new_error(
			message, WPAS_DBUS_ERROR_IFACE_SCAN_ERROR,
			"Abort failed or no scan in progress");

	return NULL;
}


/**
 * wpas_dbus_handler_signal_poll - Request immediate signal properties
 * @message: Pointer to incoming dbus message
 * @wpa_s: wpa_supplicant structure for a network interface
 * Returns: NULL indicating success or DBus error message on failure
 *
 * Handler function for "SignalPoll" method call of a network device. Requests
 * that wpa_supplicant read signal properties like RSSI, noise, and link
 * speed and return them.
 */
DBusMessage * wpas_dbus_handler_signal_poll(DBusMessage *message,
					    struct wpa_supplicant *wpa_s)
{
	struct wpa_signal_info si;
	DBusMessage *reply = NULL;
	DBusMessageIter iter, iter_dict, variant_iter;
	int ret;

	ret = wpa_drv_signal_poll(wpa_s, &si);
	if (ret) {
		return dbus_message_new_error(message, DBUS_ERROR_FAILED,
					      "Failed to read signal");
	}

	reply = dbus_message_new_method_return(message);
	if (reply == NULL)
		goto nomem;

	dbus_message_iter_init_append(reply, &iter);

	if (!dbus_message_iter_open_container(&iter, DBUS_TYPE_VARIANT,
					      "a{sv}", &variant_iter) ||
	    !wpa_dbus_dict_open_write(&variant_iter, &iter_dict) ||
	    !wpa_dbus_dict_append_int32(&iter_dict, "rssi",
					si.current_signal) ||
	    !wpa_dbus_dict_append_int32(&iter_dict, "linkspeed",
					si.current_txrate / 1000) ||
	    !wpa_dbus_dict_append_int32(&iter_dict, "noise",
					si.current_noise) ||
	    !wpa_dbus_dict_append_uint32(&iter_dict, "frequency",
					 si.frequency) ||
	    (si.chanwidth != CHAN_WIDTH_UNKNOWN &&
	     !wpa_dbus_dict_append_string(
		     &iter_dict, "width",
		     channel_width_to_string(si.chanwidth))) ||
	    (si.center_frq1 > 0 && si.center_frq2 > 0 &&
	     (!wpa_dbus_dict_append_int32(&iter_dict, "center-frq1",
					  si.center_frq1) ||
	      !wpa_dbus_dict_append_int32(&iter_dict, "center-frq2",
					  si.center_frq2))) ||
	    (si.avg_signal &&
	     !wpa_dbus_dict_append_int32(&iter_dict, "avg-rssi",
					 si.avg_signal)) ||
	    !wpa_dbus_dict_close_write(&variant_iter, &iter_dict) ||
	    !dbus_message_iter_close_container(&iter, &variant_iter))
		goto nomem;

	return reply;

nomem:
	if (reply)
		dbus_message_unref(reply);
	return wpas_dbus_error_no_memory(message);
}


/*
 * wpas_dbus_handler_disconnect - Terminate the current connection
 * @message: Pointer to incoming dbus message
 * @wpa_s: wpa_supplicant structure for a network interface
 * Returns: NotConnected DBus error message if already not connected
 * or NULL otherwise.
 *
 * Handler function for "Disconnect" method call of network interface.
 */
DBusMessage * wpas_dbus_handler_disconnect(DBusMessage *message,
					   struct wpa_supplicant *wpa_s)
{
	if (wpa_s->current_ssid != NULL) {
		wpas_request_disconnection(wpa_s);
		return NULL;
	}

	return dbus_message_new_error(message, WPAS_DBUS_ERROR_NOT_CONNECTED,
				      "This interface is not connected");
}


/**
 * wpas_dbus_new_iface_add_network - Add a new configured network
 * @message: Pointer to incoming dbus message
 * @wpa_s: wpa_supplicant structure for a network interface
 * Returns: A dbus message containing the object path of the new network
 *
 * Handler function for "AddNetwork" method call of a network interface.
 */
DBusMessage * wpas_dbus_handler_add_network(DBusMessage *message,
					    struct wpa_supplicant *wpa_s)
{
	DBusMessage *reply = NULL;
	DBusMessageIter	iter;
	struct wpa_ssid *ssid = NULL;
	char path_buf[WPAS_DBUS_OBJECT_PATH_MAX], *path = path_buf;
	DBusError error;

	dbus_message_iter_init(message, &iter);

	if (wpa_s->dbus_new_path)
		ssid = wpa_supplicant_add_network(wpa_s);
	if (ssid == NULL) {
		wpa_printf(MSG_ERROR, "%s[dbus]: can't add new interface.",
			   __func__);
		reply = wpas_dbus_error_unknown_error(
			message,
			"wpa_supplicant could not add a network on this interface.");
		goto err;
	}

	dbus_error_init(&error);
	if (!set_network_properties(wpa_s, ssid, &iter, &error)) {
		wpa_printf(MSG_DEBUG,
			   "%s[dbus]: control interface couldn't set network properties",
			   __func__);
		reply = wpas_dbus_reply_new_from_error(message, &error,
						       DBUS_ERROR_INVALID_ARGS,
						       "Failed to add network");
		dbus_error_free(&error);
		goto err;
	}

	/* Construct the object path for this network. */
	os_snprintf(path, WPAS_DBUS_OBJECT_PATH_MAX,
		    "%s/" WPAS_DBUS_NEW_NETWORKS_PART "/%d",
		    wpa_s->dbus_new_path, ssid->id);

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
		wpas_notify_network_removed(wpa_s, ssid);
		wpa_config_remove_network(wpa_s->conf, ssid->id);
	}
	return reply;
}


/**
 * wpas_dbus_handler_reassociate - Reassociate
 * @message: Pointer to incoming dbus message
 * @wpa_s: wpa_supplicant structure for a network interface
 * Returns: InterfaceDisabled DBus error message if disabled
 * or NULL otherwise.
 *
 * Handler function for "Reassociate" method call of network interface.
 */
DBusMessage * wpas_dbus_handler_reassociate(DBusMessage *message,
					    struct wpa_supplicant *wpa_s)
{
	if (wpa_s->wpa_state != WPA_INTERFACE_DISABLED) {
		wpas_request_connection(wpa_s);
		return NULL;
	}

	return dbus_message_new_error(message, WPAS_DBUS_ERROR_IFACE_DISABLED,
				      "This interface is disabled");
}


/**
 * wpas_dbus_handler_expect_disconnect - ExpectDisconnect
 * @message: Pointer to incoming dbus message
 * @global: %wpa_supplicant global data structure
 * Returns: NULL
 *
 * Handler function for notifying system there will be a expected disconnect.
 * This will prevent wpa_supplicant from adding blacklists upon next disconnect..
 */
DBusMessage * wpas_dbus_handler_expect_disconnect(DBusMessage *message,
						  struct wpa_global *global)
{
	struct wpa_supplicant *wpa_s = global->ifaces;

	for (; wpa_s; wpa_s = wpa_s->next)
		if (wpa_s->wpa_state >= WPA_ASSOCIATED)
			wpa_s->own_disconnect_req = 1;
	return NULL;
}


/**
 * wpas_dbus_handler_reattach - Reattach to current AP
 * @message: Pointer to incoming dbus message
 * @wpa_s: wpa_supplicant structure for a network interface
 * Returns: NotConnected DBus error message if not connected
 * or NULL otherwise.
 *
 * Handler function for "Reattach" method call of network interface.
 */
DBusMessage * wpas_dbus_handler_reattach(DBusMessage *message,
					 struct wpa_supplicant *wpa_s)
{
	if (wpa_s->current_ssid != NULL) {
		wpa_s->reattach = 1;
		wpas_request_connection(wpa_s);
		return NULL;
	}

	return dbus_message_new_error(message, WPAS_DBUS_ERROR_NOT_CONNECTED,
				      "This interface is not connected");
}


/**
 * wpas_dbus_handler_reconnect - Reconnect if disconnected
 * @message: Pointer to incoming dbus message
 * @wpa_s: wpa_supplicant structure for a network interface
 * Returns: InterfaceDisabled DBus error message if disabled
 * or NULL otherwise.
 *
 * Handler function for "Reconnect" method call of network interface.
 */
DBusMessage * wpas_dbus_handler_reconnect(DBusMessage *message,
		struct wpa_supplicant *wpa_s)
{
	if (wpa_s->wpa_state == WPA_INTERFACE_DISABLED) {
		return dbus_message_new_error(message,
					      WPAS_DBUS_ERROR_IFACE_DISABLED,
					      "This interface is disabled");
	}

	if (wpa_s->disconnected)
		wpas_request_connection(wpa_s);
	return NULL;
}


/**
 * wpas_dbus_handler_remove_network - Remove a configured network
 * @message: Pointer to incoming dbus message
 * @wpa_s: wpa_supplicant structure for a network interface
 * Returns: NULL on success or dbus error on failure
 *
 * Handler function for "RemoveNetwork" method call of a network interface.
 */
DBusMessage * wpas_dbus_handler_remove_network(DBusMessage *message,
					       struct wpa_supplicant *wpa_s)
{
	DBusMessage *reply = NULL;
	const char *op;
	char *iface, *net_id;
	int id;
	int result;

	dbus_message_get_args(message, NULL, DBUS_TYPE_OBJECT_PATH, &op,
			      DBUS_TYPE_INVALID);

	/* Extract the network ID and ensure the network */
	/* is actually a child of this interface */
	iface = wpas_dbus_new_decompose_object_path(op,
						    WPAS_DBUS_NEW_NETWORKS_PART,
						    &net_id);
	if (iface == NULL || net_id == NULL || !wpa_s->dbus_new_path ||
	    os_strcmp(iface, wpa_s->dbus_new_path) != 0) {
		reply = wpas_dbus_error_invalid_args(message, op);
		goto out;
	}

	errno = 0;
	id = strtoul(net_id, NULL, 10);
	if (errno != 0) {
		reply = wpas_dbus_error_invalid_args(message, op);
		goto out;
	}

	result = wpa_supplicant_remove_network(wpa_s, id);
	if (result == -1) {
		reply = wpas_dbus_error_network_unknown(message);
		goto out;
	}
	if (result == -2) {
		wpa_printf(MSG_ERROR,
			   "%s[dbus]: error occurred when removing network %d",
			   __func__, id);
		reply = wpas_dbus_error_unknown_error(
			message,
			"error removing the specified network on is interface.");
		goto out;
	}

out:
	os_free(iface);
	return reply;
}


static void remove_network(void *arg, struct wpa_ssid *ssid)
{
	struct wpa_supplicant *wpa_s = arg;

	wpas_notify_network_removed(wpa_s, ssid);

	if (wpa_config_remove_network(wpa_s->conf, ssid->id) < 0) {
		wpa_printf(MSG_ERROR,
			   "%s[dbus]: error occurred when removing network %d",
			   __func__, ssid->id);
		return;
	}

	if (ssid == wpa_s->current_ssid)
		wpa_supplicant_deauthenticate(wpa_s,
					      WLAN_REASON_DEAUTH_LEAVING);
}


/**
 * wpas_dbus_handler_remove_all_networks - Remove all configured networks
 * @message: Pointer to incoming dbus message
 * @wpa_s: wpa_supplicant structure for a network interface
 * Returns: NULL on success or dbus error on failure
 *
 * Handler function for "RemoveAllNetworks" method call of a network interface.
 */
DBusMessage * wpas_dbus_handler_remove_all_networks(
	DBusMessage *message, struct wpa_supplicant *wpa_s)
{
	if (wpa_s->sched_scanning)
		wpa_supplicant_cancel_sched_scan(wpa_s);

	/* NB: could check for failure and return an error */
	wpa_config_foreach_network(wpa_s->conf, remove_network, wpa_s);
	return NULL;
}


/**
 * wpas_dbus_handler_select_network - Attempt association with a network
 * @message: Pointer to incoming dbus message
 * @wpa_s: wpa_supplicant structure for a network interface
 * Returns: NULL on success or dbus error on failure
 *
 * Handler function for "SelectNetwork" method call of network interface.
 */
DBusMessage * wpas_dbus_handler_select_network(DBusMessage *message,
					       struct wpa_supplicant *wpa_s)
{
	DBusMessage *reply = NULL;
	const char *op;
	char *iface, *net_id;
	int id;
	struct wpa_ssid *ssid;

	dbus_message_get_args(message, NULL, DBUS_TYPE_OBJECT_PATH, &op,
			      DBUS_TYPE_INVALID);

	/* Extract the network ID and ensure the network */
	/* is actually a child of this interface */
	iface = wpas_dbus_new_decompose_object_path(op,
						    WPAS_DBUS_NEW_NETWORKS_PART,
						    &net_id);
	if (iface == NULL || net_id == NULL || !wpa_s->dbus_new_path ||
	    os_strcmp(iface, wpa_s->dbus_new_path) != 0) {
		reply = wpas_dbus_error_invalid_args(message, op);
		goto out;
	}

	errno = 0;
	id = strtoul(net_id, NULL, 10);
	if (errno != 0) {
		reply = wpas_dbus_error_invalid_args(message, op);
		goto out;
	}

	ssid = wpa_config_get_network(wpa_s->conf, id);
	if (ssid == NULL) {
		reply = wpas_dbus_error_network_unknown(message);
		goto out;
	}

	/* Finally, associate with the network */
	wpa_supplicant_select_network(wpa_s, ssid);

out:
	os_free(iface);
	return reply;
}


/**
 * wpas_dbus_handler_network_reply - Reply to a NetworkRequest signal
 * @message: Pointer to incoming dbus message
 * @wpa_s: wpa_supplicant structure for a network interface
 * Returns: NULL on success or dbus error on failure
 *
 * Handler function for "NetworkReply" method call of network interface.
 */
DBusMessage * wpas_dbus_handler_network_reply(DBusMessage *message,
					      struct wpa_supplicant *wpa_s)
{
#ifdef IEEE8021X_EAPOL
	DBusMessage *reply = NULL;
	const char *op, *field, *value;
	char *iface, *net_id;
	int id;
	struct wpa_ssid *ssid;

	if (!dbus_message_get_args(message, NULL,
				   DBUS_TYPE_OBJECT_PATH, &op,
				   DBUS_TYPE_STRING, &field,
				   DBUS_TYPE_STRING, &value,
				   DBUS_TYPE_INVALID))
		return wpas_dbus_error_invalid_args(message, NULL);

	/* Extract the network ID and ensure the network */
	/* is actually a child of this interface */
	iface = wpas_dbus_new_decompose_object_path(op,
						    WPAS_DBUS_NEW_NETWORKS_PART,
						    &net_id);
	if (iface == NULL || net_id == NULL || !wpa_s->dbus_new_path ||
	    os_strcmp(iface, wpa_s->dbus_new_path) != 0) {
		reply = wpas_dbus_error_invalid_args(message, op);
		goto out;
	}

	errno = 0;
	id = strtoul(net_id, NULL, 10);
	if (errno != 0) {
		reply = wpas_dbus_error_invalid_args(message, net_id);
		goto out;
	}

	ssid = wpa_config_get_network(wpa_s->conf, id);
	if (ssid == NULL) {
		reply = wpas_dbus_error_network_unknown(message);
		goto out;
	}

	if (wpa_supplicant_ctrl_iface_ctrl_rsp_handle(wpa_s, ssid,
						      field, value) < 0)
		reply = wpas_dbus_error_invalid_args(message, field);
	else {
		/* Tell EAP to retry immediately */
		eapol_sm_notify_ctrl_response(wpa_s->eapol);
	}

out:
	os_free(iface);
	return reply;
#else /* IEEE8021X_EAPOL */
	wpa_printf(MSG_DEBUG, "dbus: 802.1X not included");
	return wpas_dbus_error_unknown_error(message, "802.1X not included");
#endif /* IEEE8021X_EAPOL */
}


#ifndef CONFIG_NO_CONFIG_BLOBS

/**
 * wpas_dbus_handler_add_blob - Store named binary blob (ie, for certificates)
 * @message: Pointer to incoming dbus message
 * @wpa_s: %wpa_supplicant data structure
 * Returns: A dbus message containing an error on failure or NULL on success
 *
 * Asks wpa_supplicant to internally store a binary blobs.
 */
DBusMessage * wpas_dbus_handler_add_blob(DBusMessage *message,
					 struct wpa_supplicant *wpa_s)
{
	DBusMessage *reply = NULL;
	DBusMessageIter	iter, array_iter;

	char *blob_name;
	u8 *blob_data;
	int blob_len;
	struct wpa_config_blob *blob = NULL;

	dbus_message_iter_init(message, &iter);
	dbus_message_iter_get_basic(&iter, &blob_name);

	if (wpa_config_get_blob(wpa_s->conf, blob_name)) {
		return dbus_message_new_error(message,
					      WPAS_DBUS_ERROR_BLOB_EXISTS,
					      NULL);
	}

	dbus_message_iter_next(&iter);
	dbus_message_iter_recurse(&iter, &array_iter);

	dbus_message_iter_get_fixed_array(&array_iter, &blob_data, &blob_len);

	blob = os_zalloc(sizeof(*blob));
	if (!blob) {
		reply = wpas_dbus_error_no_memory(message);
		goto err;
	}

	blob->data = os_memdup(blob_data, blob_len);
	blob->name = os_strdup(blob_name);
	if (!blob->data || !blob->name) {
		reply = wpas_dbus_error_no_memory(message);
		goto err;
	}
	blob->len = blob_len;

	wpa_config_set_blob(wpa_s->conf, blob);
	wpas_notify_blob_added(wpa_s, blob->name);

	return reply;

err:
	if (blob) {
		os_free(blob->name);
		os_free(blob->data);
		os_free(blob);
	}
	return reply;
}


/**
 * wpas_dbus_handler_get_blob - Get named binary blob (ie, for certificates)
 * @message: Pointer to incoming dbus message
 * @wpa_s: %wpa_supplicant data structure
 * Returns: A dbus message containing array of bytes (blob)
 *
 * Gets one wpa_supplicant's binary blobs.
 */
DBusMessage * wpas_dbus_handler_get_blob(DBusMessage *message,
					 struct wpa_supplicant *wpa_s)
{
	DBusMessage *reply = NULL;
	DBusMessageIter	iter, array_iter;

	char *blob_name;
	const struct wpa_config_blob *blob;

	dbus_message_get_args(message, NULL, DBUS_TYPE_STRING, &blob_name,
			      DBUS_TYPE_INVALID);

	blob = wpa_config_get_blob(wpa_s->conf, blob_name);
	if (!blob) {
		return dbus_message_new_error(message,
					      WPAS_DBUS_ERROR_BLOB_UNKNOWN,
					      "Blob id not set");
	}

	reply = dbus_message_new_method_return(message);
	if (!reply)
		return wpas_dbus_error_no_memory(message);

	dbus_message_iter_init_append(reply, &iter);

	if (!dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY,
					      DBUS_TYPE_BYTE_AS_STRING,
					      &array_iter) ||
	    !dbus_message_iter_append_fixed_array(&array_iter, DBUS_TYPE_BYTE,
						  &(blob->data), blob->len) ||
	    !dbus_message_iter_close_container(&iter, &array_iter)) {
		dbus_message_unref(reply);
		reply = wpas_dbus_error_no_memory(message);
	}

	return reply;
}


/**
 * wpas_remove_handler_remove_blob - Remove named binary blob
 * @message: Pointer to incoming dbus message
 * @wpa_s: %wpa_supplicant data structure
 * Returns: NULL on success or dbus error
 *
 * Asks wpa_supplicant to internally remove a binary blobs.
 */
DBusMessage * wpas_dbus_handler_remove_blob(DBusMessage *message,
					    struct wpa_supplicant *wpa_s)
{
	DBusMessage *reply = NULL;
	char *blob_name;

	dbus_message_get_args(message, NULL, DBUS_TYPE_STRING, &blob_name,
			      DBUS_TYPE_INVALID);

	if (wpa_config_remove_blob(wpa_s->conf, blob_name)) {
		return dbus_message_new_error(message,
					      WPAS_DBUS_ERROR_BLOB_UNKNOWN,
					      "Blob id not set");
	}
	wpas_notify_blob_removed(wpa_s, blob_name);

	return reply;

}

#endif /* CONFIG_NO_CONFIG_BLOBS */


/*
 * wpas_dbus_handler_flush_bss - Flush the BSS cache
 * @message: Pointer to incoming dbus message
 * @wpa_s: wpa_supplicant structure for a network interface
 * Returns: NULL
 *
 * Handler function for "FlushBSS" method call of network interface.
 */
DBusMessage * wpas_dbus_handler_flush_bss(DBusMessage *message,
					  struct wpa_supplicant *wpa_s)
{
	dbus_uint32_t age;

	dbus_message_get_args(message, NULL, DBUS_TYPE_UINT32, &age,
			      DBUS_TYPE_INVALID);

	if (age == 0)
		wpa_bss_flush(wpa_s);
	else
		wpa_bss_flush_by_age(wpa_s, age);

	return NULL;
}


#ifdef CONFIG_AUTOSCAN
/**
 * wpas_dbus_handler_autoscan - Set autoscan parameters for the interface
 * @message: Pointer to incoming dbus message
 * @wpa_s: wpa_supplicant structure for a network interface
 * Returns: NULL
 *
 * Handler function for "AutoScan" method call of network interface.
 */
DBusMessage * wpas_dbus_handler_autoscan(DBusMessage *message,
					 struct wpa_supplicant *wpa_s)
{
	DBusMessage *reply = NULL;
	enum wpa_states state = wpa_s->wpa_state;
	char *arg;

	dbus_message_get_args(message, NULL, DBUS_TYPE_STRING, &arg,
			      DBUS_TYPE_INVALID);

	if (arg != NULL && os_strlen(arg) > 0) {
		char *tmp;

		tmp = os_strdup(arg);
		if (tmp == NULL) {
			reply = wpas_dbus_error_no_memory(message);
		} else {
			os_free(wpa_s->conf->autoscan);
			wpa_s->conf->autoscan = tmp;
			if (state == WPA_DISCONNECTED || state == WPA_INACTIVE)
				autoscan_init(wpa_s, 1);
			else if (state == WPA_SCANNING)
				wpa_supplicant_reinit_autoscan(wpa_s);
		}
	} else if (arg != NULL && os_strlen(arg) == 0) {
		os_free(wpa_s->conf->autoscan);
		wpa_s->conf->autoscan = NULL;
		autoscan_deinit(wpa_s);
	} else
		reply = dbus_message_new_error(message,
					       DBUS_ERROR_INVALID_ARGS,
					       NULL);

	return reply;
}
#endif /* CONFIG_AUTOSCAN */


/*
 * wpas_dbus_handler_eap_logoff - IEEE 802.1X EAPOL state machine logoff
 * @message: Pointer to incoming dbus message
 * @wpa_s: wpa_supplicant structure for a network interface
 * Returns: NULL
 *
 * Handler function for "EAPLogoff" method call of network interface.
 */
DBusMessage * wpas_dbus_handler_eap_logoff(DBusMessage *message,
					   struct wpa_supplicant *wpa_s)
{
	eapol_sm_notify_logoff(wpa_s->eapol, TRUE);
	return NULL;
}


/*
 * wpas_dbus_handler_eap_logon - IEEE 802.1X EAPOL state machine logon
 * @message: Pointer to incoming dbus message
 * @wpa_s: wpa_supplicant structure for a network interface
 * Returns: NULL
 *
 * Handler function for "EAPLogin" method call of network interface.
 */
DBusMessage * wpas_dbus_handler_eap_logon(DBusMessage *message,
					  struct wpa_supplicant *wpa_s)
{
	eapol_sm_notify_logoff(wpa_s->eapol, FALSE);
	return NULL;
}


#ifdef CONFIG_TDLS

static int get_peer_hwaddr_helper(DBusMessage *message, const char *func_name,
				  u8 *peer_address, DBusMessage **error)
{
	const char *peer_string;

	*error = NULL;

	if (!dbus_message_get_args(message, NULL,
				   DBUS_TYPE_STRING, &peer_string,
				   DBUS_TYPE_INVALID)) {
		*error = wpas_dbus_error_invalid_args(message, NULL);
		return -1;
	}

	if (hwaddr_aton(peer_string, peer_address)) {
		wpa_printf(MSG_DEBUG, "%s: invalid address '%s'",
			   func_name, peer_string);
		*error = wpas_dbus_error_invalid_args(
			message, "Invalid hardware address format");
		return -1;
	}

	return 0;
}


/*
 * wpas_dbus_handler_tdls_discover - Discover TDLS peer
 * @message: Pointer to incoming dbus message
 * @wpa_s: wpa_supplicant structure for a network interface
 * Returns: NULL indicating success or DBus error message on failure
 *
 * Handler function for "TDLSDiscover" method call of network interface.
 */
DBusMessage * wpas_dbus_handler_tdls_discover(DBusMessage *message,
					      struct wpa_supplicant *wpa_s)
{
	u8 peer[ETH_ALEN];
	DBusMessage *error_reply;
	int ret;

	if (get_peer_hwaddr_helper(message, __func__, peer, &error_reply) < 0)
		return error_reply;

	wpa_printf(MSG_DEBUG, "DBUS TDLS_DISCOVER " MACSTR, MAC2STR(peer));

	if (wpa_tdls_is_external_setup(wpa_s->wpa))
		ret = wpa_tdls_send_discovery_request(wpa_s->wpa, peer);
	else
		ret = wpa_drv_tdls_oper(wpa_s, TDLS_DISCOVERY_REQ, peer);

	if (ret) {
		return wpas_dbus_error_unknown_error(
			message, "error performing TDLS discovery");
	}

	return NULL;
}


/*
 * wpas_dbus_handler_tdls_setup - Setup TDLS session
 * @message: Pointer to incoming dbus message
 * @wpa_s: wpa_supplicant structure for a network interface
 * Returns: NULL indicating success or DBus error message on failure
 *
 * Handler function for "TDLSSetup" method call of network interface.
 */
DBusMessage * wpas_dbus_handler_tdls_setup(DBusMessage *message,
					   struct wpa_supplicant *wpa_s)
{
	u8 peer[ETH_ALEN];
	DBusMessage *error_reply;
	int ret;

	if (get_peer_hwaddr_helper(message, __func__, peer, &error_reply) < 0)
		return error_reply;

	wpa_printf(MSG_DEBUG, "DBUS TDLS_SETUP " MACSTR, MAC2STR(peer));

	wpa_tdls_remove(wpa_s->wpa, peer);
	if (wpa_tdls_is_external_setup(wpa_s->wpa))
		ret = wpa_tdls_start(wpa_s->wpa, peer);
	else
		ret = wpa_drv_tdls_oper(wpa_s, TDLS_SETUP, peer);

	if (ret) {
		return wpas_dbus_error_unknown_error(
			message, "error performing TDLS setup");
	}

	return NULL;
}


/*
 * wpas_dbus_handler_tdls_status - Return TDLS session status
 * @message: Pointer to incoming dbus message
 * @wpa_s: wpa_supplicant structure for a network interface
 * Returns: A string representing the state of the link to this TDLS peer
 *
 * Handler function for "TDLSStatus" method call of network interface.
 */
DBusMessage * wpas_dbus_handler_tdls_status(DBusMessage *message,
					    struct wpa_supplicant *wpa_s)
{
	u8 peer[ETH_ALEN];
	DBusMessage *reply;
	const char *tdls_status;

	if (get_peer_hwaddr_helper(message, __func__, peer, &reply) < 0)
		return reply;

	wpa_printf(MSG_DEBUG, "DBUS TDLS_STATUS " MACSTR, MAC2STR(peer));

	tdls_status = wpa_tdls_get_link_status(wpa_s->wpa, peer);

	reply = dbus_message_new_method_return(message);
	dbus_message_append_args(reply, DBUS_TYPE_STRING,
				 &tdls_status, DBUS_TYPE_INVALID);
	return reply;
}


/*
 * wpas_dbus_handler_tdls_teardown - Teardown TDLS session
 * @message: Pointer to incoming dbus message
 * @wpa_s: wpa_supplicant structure for a network interface
 * Returns: NULL indicating success or DBus error message on failure
 *
 * Handler function for "TDLSTeardown" method call of network interface.
 */
DBusMessage * wpas_dbus_handler_tdls_teardown(DBusMessage *message,
					      struct wpa_supplicant *wpa_s)
{
	u8 peer[ETH_ALEN];
	DBusMessage *error_reply;
	int ret;

	if (get_peer_hwaddr_helper(message, __func__, peer, &error_reply) < 0)
		return error_reply;

	wpa_printf(MSG_DEBUG, "DBUS TDLS_TEARDOWN " MACSTR, MAC2STR(peer));

	if (wpa_tdls_is_external_setup(wpa_s->wpa))
		ret = wpa_tdls_teardown_link(
			wpa_s->wpa, peer,
			WLAN_REASON_TDLS_TEARDOWN_UNSPECIFIED);
	else
		ret = wpa_drv_tdls_oper(wpa_s, TDLS_TEARDOWN, peer);

	if (ret) {
		return wpas_dbus_error_unknown_error(
			message, "error performing TDLS teardown");
	}

	return NULL;
}

/*
 * wpas_dbus_handler_tdls_channel_switch - Enable channel switching with TDLS peer
 * @message: Pointer to incoming dbus message
 * @wpa_s: wpa_supplicant structure for a network interface
 * Returns: NULL indicating success or DBus error message on failure
 *
 * Handler function for "TDLSChannelSwitch" method call of network interface.
 */
DBusMessage *
wpas_dbus_handler_tdls_channel_switch(DBusMessage *message,
				      struct wpa_supplicant *wpa_s)
{
	DBusMessageIter	iter, iter_dict;
	struct wpa_dbus_dict_entry entry;
	u8 peer[ETH_ALEN];
	struct hostapd_freq_params freq_params;
	u8 oper_class = 0;
	int ret;
	int is_peer_present = 0;

	if (!wpa_tdls_is_external_setup(wpa_s->wpa)) {
		wpa_printf(MSG_INFO,
			   "tdls_chanswitch: Only supported with external setup");
		return wpas_dbus_error_unknown_error(message, "TDLS is not using external setup");
	}

	os_memset(&freq_params, 0, sizeof(freq_params));

	dbus_message_iter_init(message, &iter);

	if (!wpa_dbus_dict_open_read(&iter, &iter_dict, NULL))
		return wpas_dbus_error_invalid_args(message, NULL);

	while (wpa_dbus_dict_has_dict_entry(&iter_dict)) {
		if (!wpa_dbus_dict_get_entry(&iter_dict, &entry))
			return wpas_dbus_error_invalid_args(message, NULL);

		if (os_strcmp(entry.key, "PeerAddress") == 0 &&
		    entry.type == DBUS_TYPE_STRING) {
			if (hwaddr_aton(entry.str_value, peer)) {
				wpa_printf(MSG_DEBUG,
					   "tdls_chanswitch: Invalid address '%s'",
					   entry.str_value);
				wpa_dbus_dict_entry_clear(&entry);
				return wpas_dbus_error_invalid_args(message,
								    NULL);
			}

			is_peer_present = 1;
		} else if (os_strcmp(entry.key, "OperClass") == 0 &&
			   entry.type == DBUS_TYPE_BYTE) {
			oper_class = entry.byte_value;
		} else if (os_strcmp(entry.key, "Frequency") == 0 &&
			   entry.type == DBUS_TYPE_UINT32) {
			freq_params.freq = entry.uint32_value;
		} else if (os_strcmp(entry.key, "SecChannelOffset") == 0 &&
			   entry.type == DBUS_TYPE_UINT32) {
			freq_params.sec_channel_offset = entry.uint32_value;
		} else if (os_strcmp(entry.key, "CenterFrequency1") == 0 &&
			   entry.type == DBUS_TYPE_UINT32) {
			freq_params.center_freq1 = entry.uint32_value;
		} else if (os_strcmp(entry.key, "CenterFrequency2") == 0 &&
			   entry.type == DBUS_TYPE_UINT32) {
			freq_params.center_freq2 = entry.uint32_value;
		} else if (os_strcmp(entry.key, "Bandwidth") == 0 &&
			   entry.type == DBUS_TYPE_UINT32) {
			freq_params.bandwidth = entry.uint32_value;
		} else if (os_strcmp(entry.key, "HT") == 0 &&
			   entry.type == DBUS_TYPE_BOOLEAN) {
			freq_params.ht_enabled = entry.bool_value;
		} else if (os_strcmp(entry.key, "VHT") == 0 &&
			   entry.type == DBUS_TYPE_BOOLEAN) {
			freq_params.vht_enabled = entry.bool_value;
		} else {
			wpa_dbus_dict_entry_clear(&entry);
			return wpas_dbus_error_invalid_args(message, NULL);
		}

		wpa_dbus_dict_entry_clear(&entry);
	}

	if (oper_class == 0) {
		wpa_printf(MSG_INFO,
			   "tdls_chanswitch: Invalid op class provided");
		return wpas_dbus_error_invalid_args(
			message, "Invalid op class provided");
	}

	if (freq_params.freq == 0) {
		wpa_printf(MSG_INFO,
			   "tdls_chanswitch: Invalid freq provided");
		return wpas_dbus_error_invalid_args(message,
						    "Invalid freq provided");
	}

	if (is_peer_present == 0) {
		wpa_printf(MSG_DEBUG,
			   "tdls_chanswitch: peer address not provided");
		return wpas_dbus_error_invalid_args(
			message, "peer address not provided");
	}

	wpa_printf(MSG_DEBUG, "dbus: TDLS_CHAN_SWITCH " MACSTR
		   " OP CLASS %d FREQ %d CENTER1 %d CENTER2 %d BW %d SEC_OFFSET %d%s%s",
		   MAC2STR(peer), oper_class, freq_params.freq,
		   freq_params.center_freq1, freq_params.center_freq2,
		   freq_params.bandwidth, freq_params.sec_channel_offset,
		   freq_params.ht_enabled ? " HT" : "",
		   freq_params.vht_enabled ? " VHT" : "");

	ret = wpa_tdls_enable_chan_switch(wpa_s->wpa, peer, oper_class,
					  &freq_params);
	if (ret)
		return wpas_dbus_error_unknown_error(
			message, "error processing TDLS channel switch");

	return NULL;
}

/*
 * wpas_dbus_handler_tdls_cancel_channel_switch - Disable channel switching with TDLS peer
 * @message: Pointer to incoming dbus message
 * @wpa_s: wpa_supplicant structure for a network interface
 * Returns: NULL indicating success or DBus error message on failure
 *
 * Handler function for "TDLSCancelChannelSwitch" method call of network
 * interface.
 */
DBusMessage *
wpas_dbus_handler_tdls_cancel_channel_switch(DBusMessage *message,
					     struct wpa_supplicant *wpa_s)
{
	u8 peer[ETH_ALEN];
	DBusMessage *error_reply;
	int ret;

	if (get_peer_hwaddr_helper(message, __func__, peer, &error_reply) < 0)
		return error_reply;

	wpa_printf(MSG_DEBUG, "dbus: TDLS_CANCEL_CHAN_SWITCH " MACSTR,
		   MAC2STR(peer));

	ret = wpa_tdls_disable_chan_switch(wpa_s->wpa, peer);
	if (ret)
		return wpas_dbus_error_unknown_error(
			message, "error canceling TDLS channel switch");

	return NULL;
}

#endif /* CONFIG_TDLS */


#ifndef CONFIG_NO_CONFIG_WRITE
/**
 * wpas_dbus_handler_save_config - Save configuration to configuration file
 * @message: Pointer to incoming dbus message
 * @wpa_s: wpa_supplicant structure for a network interface
 * Returns: NULL on Success, Otherwise errror message
 *
 * Handler function for "SaveConfig" method call of network interface.
 */
DBusMessage * wpas_dbus_handler_save_config(DBusMessage *message,
					    struct wpa_supplicant *wpa_s)
{
	int ret;

	if (!wpa_s->conf->update_config) {
		return wpas_dbus_error_unknown_error(
			message,
			"Not allowed to update configuration (update_config=0)");
	}

	ret = wpa_config_write(wpa_s->confname, wpa_s->conf);
	if (ret)
		return wpas_dbus_error_unknown_error(
			message, "Failed to update configuration");
	return NULL;
}
#endif /* CONFIG_NO_CONFIG_WRITE */


/**
 * wpas_dbus_handler_set_pkcs11_engine_and_module_path - Set PKCS #11 engine and module path
 * @message: Pointer to incoming dbus message
 * @wpa_s: %wpa_supplicant data structure
 * Returns: A dbus message containing an error on failure or NULL on success
 *
 * Sets the PKCS #11 engine and module path.
 */
DBusMessage * wpas_dbus_handler_set_pkcs11_engine_and_module_path(
	DBusMessage *message, struct wpa_supplicant *wpa_s)
{
	DBusMessageIter iter;
	char *value = NULL;
	char *pkcs11_engine_path = NULL;
	char *pkcs11_module_path = NULL;

	dbus_message_iter_init(message, &iter);
	dbus_message_iter_get_basic(&iter, &value);
	if (value == NULL) {
		return dbus_message_new_error(
			message, DBUS_ERROR_INVALID_ARGS,
			"Invalid pkcs11_engine_path argument");
	}
	/* Empty path defaults to NULL */
	if (os_strlen(value))
		pkcs11_engine_path = value;

	dbus_message_iter_next(&iter);
	dbus_message_iter_get_basic(&iter, &value);
	if (value == NULL) {
		os_free(pkcs11_engine_path);
		return dbus_message_new_error(
			message, DBUS_ERROR_INVALID_ARGS,
			"Invalid pkcs11_module_path argument");
	}
	/* Empty path defaults to NULL */
	if (os_strlen(value))
		pkcs11_module_path = value;

	if (wpas_set_pkcs11_engine_and_module_path(wpa_s, pkcs11_engine_path,
						   pkcs11_module_path))
		return dbus_message_new_error(
			message, DBUS_ERROR_FAILED,
			"Reinit of the EAPOL state machine with the new PKCS #11 engine and module path failed.");

	if (wpa_s->dbus_new_path) {
		wpa_dbus_mark_property_changed(
			wpa_s->global->dbus, wpa_s->dbus_new_path,
			WPAS_DBUS_NEW_IFACE_INTERFACE, "PKCS11EnginePath");
		wpa_dbus_mark_property_changed(
			wpa_s->global->dbus, wpa_s->dbus_new_path,
			WPAS_DBUS_NEW_IFACE_INTERFACE, "PKCS11ModulePath");
	}

	return NULL;
}


/**
 * wpas_dbus_getter_capabilities - Return interface capabilities
 * @iter: Pointer to incoming dbus message iter
 * @error: Location to store error on failure
 * @user_data: Function specific data
 * Returns: TRUE on success, FALSE on failure
 *
 * Getter for "Capabilities" property of an interface.
 */
dbus_bool_t wpas_dbus_getter_capabilities(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct wpa_supplicant *wpa_s = user_data;
	struct wpa_driver_capa capa;
	int res;
	DBusMessageIter iter_dict, iter_dict_entry, iter_dict_val, iter_array,
		variant_iter;
	const char *scans[] = { "active", "passive", "ssid" };

	if (!dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT,
					      "a{sv}", &variant_iter) ||
	    !wpa_dbus_dict_open_write(&variant_iter, &iter_dict))
		goto nomem;

	res = wpa_drv_get_capa(wpa_s, &capa);

	/***** pairwise cipher */
	if (res < 0) {
		const char *args[] = {"ccmp", "tkip", "none"};

		if (!wpa_dbus_dict_append_string_array(
			    &iter_dict, "Pairwise", args,
			    ARRAY_SIZE(args)))
			goto nomem;
	} else {
		if (!wpa_dbus_dict_begin_string_array(&iter_dict, "Pairwise",
						      &iter_dict_entry,
						      &iter_dict_val,
						      &iter_array) ||
		    ((capa.enc & WPA_DRIVER_CAPA_ENC_CCMP_256) &&
		     !wpa_dbus_dict_string_array_add_element(
			     &iter_array, "ccmp-256")) ||
		    ((capa.enc & WPA_DRIVER_CAPA_ENC_GCMP_256) &&
		     !wpa_dbus_dict_string_array_add_element(
			     &iter_array, "gcmp-256")) ||
		    ((capa.enc & WPA_DRIVER_CAPA_ENC_CCMP) &&
		     !wpa_dbus_dict_string_array_add_element(
			     &iter_array, "ccmp")) ||
		    ((capa.enc & WPA_DRIVER_CAPA_ENC_GCMP) &&
		     !wpa_dbus_dict_string_array_add_element(
			     &iter_array, "gcmp")) ||
		    ((capa.enc & WPA_DRIVER_CAPA_ENC_TKIP) &&
		     !wpa_dbus_dict_string_array_add_element(
			     &iter_array, "tkip")) ||
		    ((capa.key_mgmt & WPA_DRIVER_CAPA_KEY_MGMT_WPA_NONE) &&
		     !wpa_dbus_dict_string_array_add_element(
			     &iter_array, "none")) ||
		    !wpa_dbus_dict_end_string_array(&iter_dict,
						    &iter_dict_entry,
						    &iter_dict_val,
						    &iter_array))
			goto nomem;
	}

	/***** group cipher */
	if (res < 0) {
		const char *args[] = {
			"ccmp", "tkip", "wep104", "wep40"
		};

		if (!wpa_dbus_dict_append_string_array(
			    &iter_dict, "Group", args,
			    ARRAY_SIZE(args)))
			goto nomem;
	} else {
		if (!wpa_dbus_dict_begin_string_array(&iter_dict, "Group",
						      &iter_dict_entry,
						      &iter_dict_val,
						      &iter_array) ||
		    ((capa.enc & WPA_DRIVER_CAPA_ENC_CCMP_256) &&
		     !wpa_dbus_dict_string_array_add_element(
			     &iter_array, "ccmp-256")) ||
		    ((capa.enc & WPA_DRIVER_CAPA_ENC_GCMP_256) &&
		     !wpa_dbus_dict_string_array_add_element(
			     &iter_array, "gcmp-256")) ||
		    ((capa.enc & WPA_DRIVER_CAPA_ENC_CCMP) &&
		     !wpa_dbus_dict_string_array_add_element(
			     &iter_array, "ccmp")) ||
		    ((capa.enc & WPA_DRIVER_CAPA_ENC_GCMP) &&
		     !wpa_dbus_dict_string_array_add_element(
			     &iter_array, "gcmp")) ||
		    ((capa.enc & WPA_DRIVER_CAPA_ENC_TKIP) &&
		     !wpa_dbus_dict_string_array_add_element(
			     &iter_array, "tkip")) ||
		    ((capa.enc & WPA_DRIVER_CAPA_ENC_WEP104) &&
		     !wpa_dbus_dict_string_array_add_element(
			     &iter_array, "wep104")) ||
		    ((capa.enc & WPA_DRIVER_CAPA_ENC_WEP40) &&
		     !wpa_dbus_dict_string_array_add_element(
			     &iter_array, "wep40")) ||
		    !wpa_dbus_dict_end_string_array(&iter_dict,
						    &iter_dict_entry,
						    &iter_dict_val,
						    &iter_array))
			goto nomem;
	}

	if (!wpa_dbus_dict_begin_string_array(&iter_dict, "GroupMgmt",
					      &iter_dict_entry,
					      &iter_dict_val,
					      &iter_array) ||
	    (res == 0 && (capa.enc & WPA_DRIVER_CAPA_ENC_BIP) &&
	     !wpa_dbus_dict_string_array_add_element(
		     &iter_array, "aes-128-cmac")) ||
	    (res == 0 && (capa.enc & WPA_DRIVER_CAPA_ENC_BIP_GMAC_128) &&
	     !wpa_dbus_dict_string_array_add_element(
		     &iter_array, "bip-gmac-128")) ||
	    (res == 0 && (capa.enc & WPA_DRIVER_CAPA_ENC_BIP_GMAC_256) &&
	     !wpa_dbus_dict_string_array_add_element(
		     &iter_array, "bip-gmac-256")) ||
	    (res == 0 && (capa.enc & WPA_DRIVER_CAPA_ENC_BIP_CMAC_256) &&
	     !wpa_dbus_dict_string_array_add_element(
		     &iter_array, "bip-cmac-256")) ||
	    !wpa_dbus_dict_end_string_array(&iter_dict,
					    &iter_dict_entry,
					    &iter_dict_val,
					    &iter_array))
		goto nomem;

	/***** key management */
	if (res < 0) {
		const char *args[] = {
			"wpa-psk", "wpa-eap", "ieee8021x", "wpa-none",
#ifdef CONFIG_WPS
			"wps",
#endif /* CONFIG_WPS */
			"none"
		};
		if (!wpa_dbus_dict_append_string_array(
			    &iter_dict, "KeyMgmt", args,
			    ARRAY_SIZE(args)))
			goto nomem;
	} else {
		if (!wpa_dbus_dict_begin_string_array(&iter_dict, "KeyMgmt",
						      &iter_dict_entry,
						      &iter_dict_val,
						      &iter_array) ||
		    !wpa_dbus_dict_string_array_add_element(&iter_array,
							    "none") ||
		    !wpa_dbus_dict_string_array_add_element(&iter_array,
							    "ieee8021x"))
			goto nomem;

		if (capa.key_mgmt & (WPA_DRIVER_CAPA_KEY_MGMT_WPA |
				     WPA_DRIVER_CAPA_KEY_MGMT_WPA2)) {
			if (!wpa_dbus_dict_string_array_add_element(
				    &iter_array, "wpa-eap") ||
			    ((capa.key_mgmt & WPA_DRIVER_CAPA_KEY_MGMT_FT) &&
			     !wpa_dbus_dict_string_array_add_element(
				     &iter_array, "wpa-ft-eap")))
				goto nomem;

/* TODO: Ensure that driver actually supports sha256 encryption. */
#ifdef CONFIG_IEEE80211W
			if (!wpa_dbus_dict_string_array_add_element(
				    &iter_array, "wpa-eap-sha256"))
				goto nomem;
#endif /* CONFIG_IEEE80211W */
		}

		if (capa.key_mgmt & (WPA_DRIVER_CAPA_KEY_MGMT_WPA_PSK |
				     WPA_DRIVER_CAPA_KEY_MGMT_WPA2_PSK)) {
			if (!wpa_dbus_dict_string_array_add_element(
				    &iter_array, "wpa-psk") ||
			    ((capa.key_mgmt &
			      WPA_DRIVER_CAPA_KEY_MGMT_FT_PSK) &&
			     !wpa_dbus_dict_string_array_add_element(
				     &iter_array, "wpa-ft-psk")))
				goto nomem;

/* TODO: Ensure that driver actually supports sha256 encryption. */
#ifdef CONFIG_IEEE80211W
			if (!wpa_dbus_dict_string_array_add_element(
				    &iter_array, "wpa-psk-sha256"))
				goto nomem;
#endif /* CONFIG_IEEE80211W */
		}

		if ((capa.key_mgmt & WPA_DRIVER_CAPA_KEY_MGMT_WPA_NONE) &&
		    !wpa_dbus_dict_string_array_add_element(&iter_array,
							    "wpa-none"))
			goto nomem;


#ifdef CONFIG_WPS
		if (!wpa_dbus_dict_string_array_add_element(&iter_array,
							    "wps"))
			goto nomem;
#endif /* CONFIG_WPS */

		if (!wpa_dbus_dict_end_string_array(&iter_dict,
						    &iter_dict_entry,
						    &iter_dict_val,
						    &iter_array))
			goto nomem;
	}

	/***** WPA protocol */
	if (res < 0) {
		const char *args[] = { "rsn", "wpa" };

		if (!wpa_dbus_dict_append_string_array(
			    &iter_dict, "Protocol", args,
			    ARRAY_SIZE(args)))
			goto nomem;
	} else {
		if (!wpa_dbus_dict_begin_string_array(&iter_dict, "Protocol",
						      &iter_dict_entry,
						      &iter_dict_val,
						      &iter_array) ||
		    ((capa.key_mgmt & (WPA_DRIVER_CAPA_KEY_MGMT_WPA2 |
				       WPA_DRIVER_CAPA_KEY_MGMT_WPA2_PSK)) &&
		     !wpa_dbus_dict_string_array_add_element(
			     &iter_array, "rsn")) ||
		    ((capa.key_mgmt & (WPA_DRIVER_CAPA_KEY_MGMT_WPA |
				       WPA_DRIVER_CAPA_KEY_MGMT_WPA_PSK)) &&
		     !wpa_dbus_dict_string_array_add_element(
			     &iter_array, "wpa")) ||
		    !wpa_dbus_dict_end_string_array(&iter_dict,
						    &iter_dict_entry,
						    &iter_dict_val,
						    &iter_array))
			goto nomem;
	}

	/***** auth alg */
	if (res < 0) {
		const char *args[] = { "open", "shared", "leap" };

		if (!wpa_dbus_dict_append_string_array(
			    &iter_dict, "AuthAlg", args,
			    ARRAY_SIZE(args)))
			goto nomem;
	} else {
		if (!wpa_dbus_dict_begin_string_array(&iter_dict, "AuthAlg",
						      &iter_dict_entry,
						      &iter_dict_val,
						      &iter_array))
			goto nomem;

		if (((capa.auth & WPA_DRIVER_AUTH_OPEN) &&
		     !wpa_dbus_dict_string_array_add_element(
			     &iter_array, "open")) ||
		    ((capa.auth & WPA_DRIVER_AUTH_SHARED) &&
		     !wpa_dbus_dict_string_array_add_element(
			     &iter_array, "shared")) ||
		    ((capa.auth & WPA_DRIVER_AUTH_LEAP) &&
		     !wpa_dbus_dict_string_array_add_element(
			     &iter_array, "leap")) ||
		    !wpa_dbus_dict_end_string_array(&iter_dict,
						    &iter_dict_entry,
						    &iter_dict_val,
						    &iter_array))
			goto nomem;
	}

	/***** Scan */
	if (!wpa_dbus_dict_append_string_array(&iter_dict, "Scan", scans,
					       ARRAY_SIZE(scans)))
		goto nomem;

	/***** Modes */
	if (!wpa_dbus_dict_begin_string_array(&iter_dict, "Modes",
					      &iter_dict_entry,
					      &iter_dict_val,
					      &iter_array) ||
	    !wpa_dbus_dict_string_array_add_element(
		    &iter_array, "infrastructure") ||
	    (res >= 0 && (capa.flags & WPA_DRIVER_FLAGS_IBSS) &&
	     !wpa_dbus_dict_string_array_add_element(
		     &iter_array, "ad-hoc")) ||
	    (res >= 0 && (capa.flags & WPA_DRIVER_FLAGS_AP) &&
	     !wpa_dbus_dict_string_array_add_element(
		     &iter_array, "ap")) ||
	    (res >= 0 && (capa.flags & WPA_DRIVER_FLAGS_P2P_CAPABLE) &&
	     !wpa_s->conf->p2p_disabled &&
	     !wpa_dbus_dict_string_array_add_element(
		     &iter_array, "p2p")) ||
#ifdef CONFIG_MESH
	    (res >= 0 && (capa.flags & WPA_DRIVER_FLAGS_MESH) &&
	     !wpa_dbus_dict_string_array_add_element(
		     &iter_array, "mesh")) ||
#endif /* CONFIG_MESH */
	    !wpa_dbus_dict_end_string_array(&iter_dict,
					    &iter_dict_entry,
					    &iter_dict_val,
					    &iter_array))
		goto nomem;
	/***** Modes end */

	if (res >= 0) {
		dbus_int32_t max_scan_ssid = capa.max_scan_ssids;

		if (!wpa_dbus_dict_append_int32(&iter_dict, "MaxScanSSID",
						max_scan_ssid))
			goto nomem;
	}

	if (!wpa_dbus_dict_close_write(&variant_iter, &iter_dict) ||
	    !dbus_message_iter_close_container(iter, &variant_iter))
		goto nomem;

	return TRUE;

nomem:
	dbus_set_error_const(error, DBUS_ERROR_NO_MEMORY, "no memory");
	return FALSE;
}


/**
 * wpas_dbus_getter_state - Get interface state
 * @iter: Pointer to incoming dbus message iter
 * @error: Location to store error on failure
 * @user_data: Function specific data
 * Returns: TRUE on success, FALSE on failure
 *
 * Getter for "State" property.
 */
dbus_bool_t wpas_dbus_getter_state(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct wpa_supplicant *wpa_s = user_data;
	const char *str_state;
	char *state_ls, *tmp;
	dbus_bool_t success = FALSE;

	str_state = wpa_supplicant_state_txt(wpa_s->wpa_state);

	/* make state string lowercase to fit new DBus API convention
	 */
	state_ls = tmp = os_strdup(str_state);
	if (!tmp) {
		dbus_set_error_const(error, DBUS_ERROR_NO_MEMORY, "no memory");
		return FALSE;
	}
	while (*tmp) {
		*tmp = tolower(*tmp);
		tmp++;
	}

	success = wpas_dbus_simple_property_getter(iter, DBUS_TYPE_STRING,
						   &state_ls, error);

	os_free(state_ls);

	return success;
}


/**
 * wpas_dbus_new_iface_get_scanning - Get interface scanning state
 * @iter: Pointer to incoming dbus message iter
 * @error: Location to store error on failure
 * @user_data: Function specific data
 * Returns: TRUE on success, FALSE on failure
 *
 * Getter for "scanning" property.
 */
dbus_bool_t wpas_dbus_getter_scanning(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct wpa_supplicant *wpa_s = user_data;
	dbus_bool_t scanning = wpa_s->scanning ? TRUE : FALSE;

	return wpas_dbus_simple_property_getter(iter, DBUS_TYPE_BOOLEAN,
						&scanning, error);
}


/**
 * wpas_dbus_getter_ap_scan - Control roaming mode
 * @iter: Pointer to incoming dbus message iter
 * @error: Location to store error on failure
 * @user_data: Function specific data
 * Returns: TRUE on success, FALSE on failure
 *
 * Getter function for "ApScan" property.
 */
dbus_bool_t wpas_dbus_getter_ap_scan(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct wpa_supplicant *wpa_s = user_data;
	dbus_uint32_t ap_scan = wpa_s->conf->ap_scan;

	return wpas_dbus_simple_property_getter(iter, DBUS_TYPE_UINT32,
						&ap_scan, error);
}


/**
 * wpas_dbus_setter_ap_scan - Control roaming mode
 * @iter: Pointer to incoming dbus message iter
 * @error: Location to store error on failure
 * @user_data: Function specific data
 * Returns: TRUE on success, FALSE on failure
 *
 * Setter function for "ApScan" property.
 */
dbus_bool_t wpas_dbus_setter_ap_scan(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct wpa_supplicant *wpa_s = user_data;
	dbus_uint32_t ap_scan;

	if (!wpas_dbus_simple_property_setter(iter, error, DBUS_TYPE_UINT32,
					      &ap_scan))
		return FALSE;

	if (wpa_supplicant_set_ap_scan(wpa_s, ap_scan)) {
		dbus_set_error_const(error, DBUS_ERROR_FAILED,
				     "ap_scan must be 0, 1, or 2");
		return FALSE;
	}
	return TRUE;
}


/**
 * wpas_dbus_getter_fast_reauth - Control fast
 * reauthentication (TLS session resumption)
 * @iter: Pointer to incoming dbus message iter
 * @error: Location to store error on failure
 * @user_data: Function specific data
 * Returns: TRUE on success, FALSE on failure
 *
 * Getter function for "FastReauth" property.
 */
dbus_bool_t wpas_dbus_getter_fast_reauth(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct wpa_supplicant *wpa_s = user_data;
	dbus_bool_t fast_reauth = wpa_s->conf->fast_reauth ? TRUE : FALSE;

	return wpas_dbus_simple_property_getter(iter, DBUS_TYPE_BOOLEAN,
						&fast_reauth, error);
}


/**
 * wpas_dbus_setter_fast_reauth - Control fast
 * reauthentication (TLS session resumption)
 * @iter: Pointer to incoming dbus message iter
 * @error: Location to store error on failure
 * @user_data: Function specific data
 * Returns: TRUE on success, FALSE on failure
 *
 * Setter function for "FastReauth" property.
 */
dbus_bool_t wpas_dbus_setter_fast_reauth(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct wpa_supplicant *wpa_s = user_data;
	dbus_bool_t fast_reauth;

	if (!wpas_dbus_simple_property_setter(iter, error, DBUS_TYPE_BOOLEAN,
					      &fast_reauth))
		return FALSE;

	wpa_s->conf->fast_reauth = fast_reauth;
	return TRUE;
}


/**
 * wpas_dbus_getter_disconnect_reason - Get most recent reason for disconnect
 * @iter: Pointer to incoming dbus message iter
 * @error: Location to store error on failure
 * @user_data: Function specific data
 * Returns: TRUE on success, FALSE on failure
 *
 * Getter for "DisconnectReason" property.  The reason is negative if it is
 * locally generated.
 */
dbus_bool_t wpas_dbus_getter_disconnect_reason(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct wpa_supplicant *wpa_s = user_data;
	dbus_int32_t reason = wpa_s->disconnect_reason;

	return wpas_dbus_simple_property_getter(iter, DBUS_TYPE_INT32,
						&reason, error);
}


/**
 * wpas_dbus_getter_assoc_status_code - Get most recent failed assoc status code
 * @iter: Pointer to incoming dbus message iter
 * @error: Location to store error on failure
 * @user_data: Function specific data
 * Returns: TRUE on success, FALSE on failure
 *
 * Getter for "AssocStatusCode" property.
 */
dbus_bool_t wpas_dbus_getter_assoc_status_code(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct wpa_supplicant *wpa_s = user_data;
	dbus_int32_t status_code = wpa_s->assoc_status_code;

	return wpas_dbus_simple_property_getter(iter, DBUS_TYPE_INT32,
						&status_code, error);
}


/**
 * wpas_dbus_getter_bss_expire_age - Get BSS entry expiration age
 * @iter: Pointer to incoming dbus message iter
 * @error: Location to store error on failure
 * @user_data: Function specific data
 * Returns: TRUE on success, FALSE on failure
 *
 * Getter function for "BSSExpireAge" property.
 */
dbus_bool_t wpas_dbus_getter_bss_expire_age(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct wpa_supplicant *wpa_s = user_data;
	dbus_uint32_t expire_age = wpa_s->conf->bss_expiration_age;

	return wpas_dbus_simple_property_getter(iter, DBUS_TYPE_UINT32,
						&expire_age, error);
}


/**
 * wpas_dbus_setter_bss_expire_age - Control BSS entry expiration age
 * @iter: Pointer to incoming dbus message iter
 * @error: Location to store error on failure
 * @user_data: Function specific data
 * Returns: TRUE on success, FALSE on failure
 *
 * Setter function for "BSSExpireAge" property.
 */
dbus_bool_t wpas_dbus_setter_bss_expire_age(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct wpa_supplicant *wpa_s = user_data;
	dbus_uint32_t expire_age;

	if (!wpas_dbus_simple_property_setter(iter, error, DBUS_TYPE_UINT32,
					      &expire_age))
		return FALSE;

	if (wpa_supplicant_set_bss_expiration_age(wpa_s, expire_age)) {
		dbus_set_error_const(error, DBUS_ERROR_FAILED,
				     "BSSExpireAge must be >= 10");
		return FALSE;
	}
	return TRUE;
}


/**
 * wpas_dbus_getter_bss_expire_count - Get BSS entry expiration scan count
 * @iter: Pointer to incoming dbus message iter
 * @error: Location to store error on failure
 * @user_data: Function specific data
 * Returns: TRUE on success, FALSE on failure
 *
 * Getter function for "BSSExpireCount" property.
 */
dbus_bool_t wpas_dbus_getter_bss_expire_count(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct wpa_supplicant *wpa_s = user_data;
	dbus_uint32_t expire_count = wpa_s->conf->bss_expiration_scan_count;

	return wpas_dbus_simple_property_getter(iter, DBUS_TYPE_UINT32,
						&expire_count, error);
}


/**
 * wpas_dbus_setter_bss_expire_count - Control BSS entry expiration scan count
 * @iter: Pointer to incoming dbus message iter
 * @error: Location to store error on failure
 * @user_data: Function specific data
 * Returns: TRUE on success, FALSE on failure
 *
 * Setter function for "BSSExpireCount" property.
 */
dbus_bool_t wpas_dbus_setter_bss_expire_count(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct wpa_supplicant *wpa_s = user_data;
	dbus_uint32_t expire_count;

	if (!wpas_dbus_simple_property_setter(iter, error, DBUS_TYPE_UINT32,
					      &expire_count))
		return FALSE;

	if (wpa_supplicant_set_bss_expiration_count(wpa_s, expire_count)) {
		dbus_set_error_const(error, DBUS_ERROR_FAILED,
				     "BSSExpireCount must be > 0");
		return FALSE;
	}
	return TRUE;
}


/**
 * wpas_dbus_getter_country - Control country code
 * @iter: Pointer to incoming dbus message iter
 * @error: Location to store error on failure
 * @user_data: Function specific data
 * Returns: TRUE on success, FALSE on failure
 *
 * Getter function for "Country" property.
 */
dbus_bool_t wpas_dbus_getter_country(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct wpa_supplicant *wpa_s = user_data;
	char country[3];
	char *str = country;

	country[0] = wpa_s->conf->country[0];
	country[1] = wpa_s->conf->country[1];
	country[2] = '\0';

	return wpas_dbus_simple_property_getter(iter, DBUS_TYPE_STRING,
						&str, error);
}


/**
 * wpas_dbus_setter_country - Control country code
 * @iter: Pointer to incoming dbus message iter
 * @error: Location to store error on failure
 * @user_data: Function specific data
 * Returns: TRUE on success, FALSE on failure
 *
 * Setter function for "Country" property.
 */
dbus_bool_t wpas_dbus_setter_country(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct wpa_supplicant *wpa_s = user_data;
	const char *country;

	if (!wpas_dbus_simple_property_setter(iter, error, DBUS_TYPE_STRING,
					      &country))
		return FALSE;

	if (!country[0] || !country[1]) {
		dbus_set_error_const(error, DBUS_ERROR_FAILED,
				     "invalid country code");
		return FALSE;
	}

	if (wpa_s->drv_priv != NULL && wpa_drv_set_country(wpa_s, country)) {
		wpa_printf(MSG_DEBUG, "Failed to set country");
		dbus_set_error_const(error, DBUS_ERROR_FAILED,
				     "failed to set country code");
		return FALSE;
	}

	wpa_s->conf->country[0] = country[0];
	wpa_s->conf->country[1] = country[1];
	return TRUE;
}


/**
 * wpas_dbus_getter_scan_interval - Get scan interval
 * @iter: Pointer to incoming dbus message iter
 * @error: Location to store error on failure
 * @user_data: Function specific data
 * Returns: TRUE on success, FALSE on failure
 *
 * Getter function for "ScanInterval" property.
 */
dbus_bool_t wpas_dbus_getter_scan_interval(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct wpa_supplicant *wpa_s = user_data;
	dbus_int32_t scan_interval = wpa_s->scan_interval;

	return wpas_dbus_simple_property_getter(iter, DBUS_TYPE_INT32,
						&scan_interval, error);
}


/**
 * wpas_dbus_setter_scan_interval - Control scan interval
 * @iter: Pointer to incoming dbus message iter
 * @error: Location to store error on failure
 * @user_data: Function specific data
 * Returns: TRUE on success, FALSE on failure
 *
 * Setter function for "ScanInterval" property.
 */
dbus_bool_t wpas_dbus_setter_scan_interval(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct wpa_supplicant *wpa_s = user_data;
	dbus_int32_t scan_interval;

	if (!wpas_dbus_simple_property_setter(iter, error, DBUS_TYPE_INT32,
					      &scan_interval))
		return FALSE;

	if (wpa_supplicant_set_scan_interval(wpa_s, scan_interval)) {
		dbus_set_error_const(error, DBUS_ERROR_FAILED,
				     "scan_interval must be >= 0");
		return FALSE;
	}
	return TRUE;
}


/**
 * wpas_dbus_getter_ifname - Get interface name
 * @iter: Pointer to incoming dbus message iter
 * @error: Location to store error on failure
 * @user_data: Function specific data
 * Returns: TRUE on success, FALSE on failure
 *
 * Getter for "Ifname" property.
 */
dbus_bool_t wpas_dbus_getter_ifname(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct wpa_supplicant *wpa_s = user_data;

	return wpas_dbus_string_property_getter(iter, wpa_s->ifname, error);
}


/**
 * wpas_dbus_getter_driver - Get interface name
 * @iter: Pointer to incoming dbus message iter
 * @error: Location to store error on failure
 * @user_data: Function specific data
 * Returns: TRUE on success, FALSE on failure
 *
 * Getter for "Driver" property.
 */
dbus_bool_t wpas_dbus_getter_driver(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct wpa_supplicant *wpa_s = user_data;

	if (wpa_s->driver == NULL || wpa_s->driver->name == NULL) {
		wpa_printf(MSG_DEBUG, "%s[dbus]: wpa_s has no driver set",
			   __func__);
		dbus_set_error(error, DBUS_ERROR_FAILED, "%s: no driver set",
			       __func__);
		return FALSE;
	}

	return wpas_dbus_string_property_getter(iter, wpa_s->driver->name,
						error);
}


/**
 * wpas_dbus_getter_current_bss - Get current bss object path
 * @iter: Pointer to incoming dbus message iter
 * @error: Location to store error on failure
 * @user_data: Function specific data
 * Returns: TRUE on success, FALSE on failure
 *
 * Getter for "CurrentBSS" property.
 */
dbus_bool_t wpas_dbus_getter_current_bss(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct wpa_supplicant *wpa_s = user_data;
	char path_buf[WPAS_DBUS_OBJECT_PATH_MAX], *bss_obj_path = path_buf;

	if (wpa_s->current_bss && wpa_s->dbus_new_path)
		os_snprintf(bss_obj_path, WPAS_DBUS_OBJECT_PATH_MAX,
			    "%s/" WPAS_DBUS_NEW_BSSIDS_PART "/%u",
			    wpa_s->dbus_new_path, wpa_s->current_bss->id);
	else
		os_snprintf(bss_obj_path, WPAS_DBUS_OBJECT_PATH_MAX, "/");

	return wpas_dbus_simple_property_getter(iter, DBUS_TYPE_OBJECT_PATH,
						&bss_obj_path, error);
}


/**
 * wpas_dbus_getter_current_network - Get current network object path
 * @iter: Pointer to incoming dbus message iter
 * @error: Location to store error on failure
 * @user_data: Function specific data
 * Returns: TRUE on success, FALSE on failure
 *
 * Getter for "CurrentNetwork" property.
 */
dbus_bool_t wpas_dbus_getter_current_network(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct wpa_supplicant *wpa_s = user_data;
	char path_buf[WPAS_DBUS_OBJECT_PATH_MAX], *net_obj_path = path_buf;

	if (wpa_s->current_ssid && wpa_s->dbus_new_path)
		os_snprintf(net_obj_path, WPAS_DBUS_OBJECT_PATH_MAX,
			    "%s/" WPAS_DBUS_NEW_NETWORKS_PART "/%u",
			    wpa_s->dbus_new_path, wpa_s->current_ssid->id);
	else
		os_snprintf(net_obj_path, WPAS_DBUS_OBJECT_PATH_MAX, "/");

	return wpas_dbus_simple_property_getter(iter, DBUS_TYPE_OBJECT_PATH,
						&net_obj_path, error);
}


/**
 * wpas_dbus_getter_current_auth_mode - Get current authentication type
 * @iter: Pointer to incoming dbus message iter
 * @error: Location to store error on failure
 * @user_data: Function specific data
 * Returns: TRUE on success, FALSE on failure
 *
 * Getter for "CurrentAuthMode" property.
 */
dbus_bool_t wpas_dbus_getter_current_auth_mode(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct wpa_supplicant *wpa_s = user_data;
	const char *eap_mode;
	const char *auth_mode;
	char eap_mode_buf[WPAS_DBUS_AUTH_MODE_MAX];

	if (wpa_s->wpa_state != WPA_COMPLETED) {
		auth_mode = "INACTIVE";
	} else if (wpa_s->key_mgmt == WPA_KEY_MGMT_IEEE8021X ||
	    wpa_s->key_mgmt == WPA_KEY_MGMT_IEEE8021X_NO_WPA) {
		eap_mode = wpa_supplicant_get_eap_mode(wpa_s);
		os_snprintf(eap_mode_buf, WPAS_DBUS_AUTH_MODE_MAX,
			    "EAP-%s", eap_mode);
		auth_mode = eap_mode_buf;

	} else if (wpa_s->current_ssid) {
		auth_mode = wpa_key_mgmt_txt(wpa_s->key_mgmt,
					     wpa_s->current_ssid->proto);
	} else {
		auth_mode = "UNKNOWN";
	}

	return wpas_dbus_simple_property_getter(iter, DBUS_TYPE_STRING,
						&auth_mode, error);
}


/**
 * wpas_dbus_getter_bridge_ifname - Get interface name
 * @iter: Pointer to incoming dbus message iter
 * @error: Location to store error on failure
 * @user_data: Function specific data
 * Returns: TRUE on success, FALSE on failure
 *
 * Getter for "BridgeIfname" property.
 */
dbus_bool_t wpas_dbus_getter_bridge_ifname(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct wpa_supplicant *wpa_s = user_data;

	return wpas_dbus_string_property_getter(iter, wpa_s->bridge_ifname,
						error);
}


/**
 * wpas_dbus_getter_config_file - Get interface configuration file path
 * @iter: Pointer to incoming dbus message iter
 * @error: Location to store error on failure
 * @user_data: Function specific data
 * Returns: TRUE on success, FALSE on failure
 *
 * Getter for "ConfigFile" property.
 */
dbus_bool_t wpas_dbus_getter_config_file(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct wpa_supplicant *wpa_s = user_data;

	return wpas_dbus_string_property_getter(iter, wpa_s->confname, error);
}


/**
 * wpas_dbus_getter_bsss - Get array of BSSs objects
 * @iter: Pointer to incoming dbus message iter
 * @error: Location to store error on failure
 * @user_data: Function specific data
 * Returns: TRUE on success, FALSE on failure
 *
 * Getter for "BSSs" property.
 */
dbus_bool_t wpas_dbus_getter_bsss(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct wpa_supplicant *wpa_s = user_data;
	struct wpa_bss *bss;
	char **paths;
	unsigned int i = 0;
	dbus_bool_t success = FALSE;

	if (!wpa_s->dbus_new_path) {
		dbus_set_error(error, DBUS_ERROR_FAILED,
			       "%s: no D-Bus interface", __func__);
		return FALSE;
	}

	paths = os_calloc(wpa_s->num_bss, sizeof(char *));
	if (!paths) {
		dbus_set_error_const(error, DBUS_ERROR_NO_MEMORY, "no memory");
		return FALSE;
	}

	/* Loop through scan results and append each result's object path */
	dl_list_for_each(bss, &wpa_s->bss_id, struct wpa_bss, list_id) {
		paths[i] = os_zalloc(WPAS_DBUS_OBJECT_PATH_MAX);
		if (paths[i] == NULL) {
			dbus_set_error_const(error, DBUS_ERROR_NO_MEMORY,
					     "no memory");
			goto out;
		}
		/* Construct the object path for this BSS. */
		os_snprintf(paths[i++], WPAS_DBUS_OBJECT_PATH_MAX,
			    "%s/" WPAS_DBUS_NEW_BSSIDS_PART "/%u",
			    wpa_s->dbus_new_path, bss->id);
	}

	success = wpas_dbus_simple_array_property_getter(iter,
							 DBUS_TYPE_OBJECT_PATH,
							 paths, wpa_s->num_bss,
							 error);

out:
	while (i)
		os_free(paths[--i]);
	os_free(paths);
	return success;
}


/**
 * wpas_dbus_getter_networks - Get array of networks objects
 * @iter: Pointer to incoming dbus message iter
 * @error: Location to store error on failure
 * @user_data: Function specific data
 * Returns: TRUE on success, FALSE on failure
 *
 * Getter for "Networks" property.
 */
dbus_bool_t wpas_dbus_getter_networks(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct wpa_supplicant *wpa_s = user_data;
	struct wpa_ssid *ssid;
	char **paths;
	unsigned int i = 0, num = 0;
	dbus_bool_t success = FALSE;

	if (!wpa_s->dbus_new_path) {
		dbus_set_error(error, DBUS_ERROR_FAILED,
			       "%s: no D-Bus interface", __func__);
		return FALSE;
	}

	for (ssid = wpa_s->conf->ssid; ssid; ssid = ssid->next)
		if (!network_is_persistent_group(ssid))
			num++;

	paths = os_calloc(num, sizeof(char *));
	if (!paths) {
		dbus_set_error(error, DBUS_ERROR_NO_MEMORY, "no memory");
		return FALSE;
	}

	/* Loop through configured networks and append object path of each */
	for (ssid = wpa_s->conf->ssid; ssid; ssid = ssid->next) {
		if (network_is_persistent_group(ssid))
			continue;
		paths[i] = os_zalloc(WPAS_DBUS_OBJECT_PATH_MAX);
		if (paths[i] == NULL) {
			dbus_set_error(error, DBUS_ERROR_NO_MEMORY,
				       "no memory");
			goto out;
		}

		/* Construct the object path for this network. */
		os_snprintf(paths[i++], WPAS_DBUS_OBJECT_PATH_MAX,
			    "%s/" WPAS_DBUS_NEW_NETWORKS_PART "/%d",
			    wpa_s->dbus_new_path, ssid->id);
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
 * wpas_dbus_getter_pkcs11_engine_path - Get PKCS #11 engine path
 * @iter: Pointer to incoming dbus message iter
 * @error: Location to store error on failure
 * @user_data: Function specific data
 * Returns: A dbus message containing the PKCS #11 engine path
 *
 * Getter for "PKCS11EnginePath" property.
 */
dbus_bool_t wpas_dbus_getter_pkcs11_engine_path(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct wpa_supplicant *wpa_s = user_data;

	return wpas_dbus_string_property_getter(iter,
						wpa_s->conf->pkcs11_engine_path,
						error);
}


/**
 * wpas_dbus_getter_pkcs11_module_path - Get PKCS #11 module path
 * @iter: Pointer to incoming dbus message iter
 * @error: Location to store error on failure
 * @user_data: Function specific data
 * Returns: A dbus message containing the PKCS #11 module path
 *
 * Getter for "PKCS11ModulePath" property.
 */
dbus_bool_t wpas_dbus_getter_pkcs11_module_path(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct wpa_supplicant *wpa_s = user_data;

	return wpas_dbus_string_property_getter(iter,
						wpa_s->conf->pkcs11_module_path,
						error);
}


/**
 * wpas_dbus_getter_blobs - Get all blobs defined for this interface
 * @iter: Pointer to incoming dbus message iter
 * @error: Location to store error on failure
 * @user_data: Function specific data
 * Returns: TRUE on success, FALSE on failure
 *
 * Getter for "Blobs" property.
 */
dbus_bool_t wpas_dbus_getter_blobs(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct wpa_supplicant *wpa_s = user_data;
	DBusMessageIter variant_iter, dict_iter, entry_iter, array_iter;
	struct wpa_config_blob *blob;

	if (!dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT,
					      "a{say}", &variant_iter) ||
	    !dbus_message_iter_open_container(&variant_iter, DBUS_TYPE_ARRAY,
					      "{say}", &dict_iter)) {
		dbus_set_error_const(error, DBUS_ERROR_NO_MEMORY, "no memory");
		return FALSE;
	}

	blob = wpa_s->conf->blobs;
	while (blob) {
		if (!dbus_message_iter_open_container(&dict_iter,
						      DBUS_TYPE_DICT_ENTRY,
						      NULL, &entry_iter) ||
		    !dbus_message_iter_append_basic(&entry_iter,
						    DBUS_TYPE_STRING,
						    &(blob->name)) ||
		    !dbus_message_iter_open_container(&entry_iter,
						      DBUS_TYPE_ARRAY,
						      DBUS_TYPE_BYTE_AS_STRING,
						      &array_iter) ||
		    !dbus_message_iter_append_fixed_array(&array_iter,
							  DBUS_TYPE_BYTE,
							  &(blob->data),
							  blob->len) ||
		    !dbus_message_iter_close_container(&entry_iter,
						       &array_iter) ||
		    !dbus_message_iter_close_container(&dict_iter,
						       &entry_iter)) {
			dbus_set_error_const(error, DBUS_ERROR_NO_MEMORY,
					     "no memory");
			return FALSE;
		}

		blob = blob->next;
	}

	if (!dbus_message_iter_close_container(&variant_iter, &dict_iter) ||
	    !dbus_message_iter_close_container(iter, &variant_iter)) {
		dbus_set_error_const(error, DBUS_ERROR_NO_MEMORY, "no memory");
		return FALSE;
	}

	return TRUE;
}


dbus_bool_t wpas_dbus_getter_iface_global(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct wpa_supplicant *wpa_s = user_data;
	int ret;
	char buf[250];
	char *p = buf;

	if (!property_desc->data) {
		dbus_set_error(error, DBUS_ERROR_INVALID_ARGS,
			       "Unhandled interface property %s",
			       property_desc->dbus_property);
		return FALSE;
	}

	ret = wpa_config_get_value(property_desc->data, wpa_s->conf, buf,
				   sizeof(buf));
	if (ret < 0)
		*p = '\0';

	return wpas_dbus_simple_property_getter(iter, DBUS_TYPE_STRING, &p,
						error);
}


dbus_bool_t wpas_dbus_setter_iface_global(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct wpa_supplicant *wpa_s = user_data;
	const char *new_value = NULL;
	char buf[250];
	size_t combined_len;
	int ret;

	if (!wpas_dbus_simple_property_setter(iter, error, DBUS_TYPE_STRING,
					      &new_value))
		return FALSE;

	combined_len = os_strlen(property_desc->data) + os_strlen(new_value) +
		3;
	if (combined_len >= sizeof(buf)) {
		dbus_set_error(error, DBUS_ERROR_INVALID_ARGS,
			       "Interface property %s value too large",
			       property_desc->dbus_property);
		return FALSE;
	}

	if (!new_value[0])
		new_value = "NULL";

	ret = os_snprintf(buf, combined_len, "%s=%s", property_desc->data,
			  new_value);
	if (os_snprintf_error(combined_len, ret)) {
		dbus_set_error(error,  WPAS_DBUS_ERROR_UNKNOWN_ERROR,
			       "Failed to construct new interface property %s",
			       property_desc->dbus_property);
		return FALSE;
	}

	if (wpa_config_process_global(wpa_s->conf, buf, -1)) {
		dbus_set_error(error, DBUS_ERROR_INVALID_ARGS,
			       "Failed to set interface property %s",
			       property_desc->dbus_property);
		return FALSE;
	}

	wpa_supplicant_update_config(wpa_s);
	return TRUE;
}


static struct wpa_bss * get_bss_helper(struct bss_handler_args *args,
				       DBusError *error, const char *func_name)
{
	struct wpa_bss *res = wpa_bss_get_id(args->wpa_s, args->id);

	if (!res) {
		wpa_printf(MSG_ERROR, "%s[dbus]: no bss with id %d found",
			   func_name, args->id);
		dbus_set_error(error, DBUS_ERROR_FAILED,
			       "%s: BSS %d not found",
			       func_name, args->id);
	}

	return res;
}


/**
 * wpas_dbus_getter_bss_bssid - Return the BSSID of a BSS
 * @iter: Pointer to incoming dbus message iter
 * @error: Location to store error on failure
 * @user_data: Function specific data
 * Returns: TRUE on success, FALSE on failure
 *
 * Getter for "BSSID" property.
 */
dbus_bool_t wpas_dbus_getter_bss_bssid(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct bss_handler_args *args = user_data;
	struct wpa_bss *res;

	res = get_bss_helper(args, error, __func__);
	if (!res)
		return FALSE;

	return wpas_dbus_simple_array_property_getter(iter, DBUS_TYPE_BYTE,
						      res->bssid, ETH_ALEN,
						      error);
}


/**
 * wpas_dbus_getter_bss_ssid - Return the SSID of a BSS
 * @iter: Pointer to incoming dbus message iter
 * @error: Location to store error on failure
 * @user_data: Function specific data
 * Returns: TRUE on success, FALSE on failure
 *
 * Getter for "SSID" property.
 */
dbus_bool_t wpas_dbus_getter_bss_ssid(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct bss_handler_args *args = user_data;
	struct wpa_bss *res;

	res = get_bss_helper(args, error, __func__);
	if (!res)
		return FALSE;

	return wpas_dbus_simple_array_property_getter(iter, DBUS_TYPE_BYTE,
						      res->ssid, res->ssid_len,
						      error);
}


/**
 * wpas_dbus_getter_bss_privacy - Return the privacy flag of a BSS
 * @iter: Pointer to incoming dbus message iter
 * @error: Location to store error on failure
 * @user_data: Function specific data
 * Returns: TRUE on success, FALSE on failure
 *
 * Getter for "Privacy" property.
 */
dbus_bool_t wpas_dbus_getter_bss_privacy(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct bss_handler_args *args = user_data;
	struct wpa_bss *res;
	dbus_bool_t privacy;

	res = get_bss_helper(args, error, __func__);
	if (!res)
		return FALSE;

	privacy = (res->caps & IEEE80211_CAP_PRIVACY) ? TRUE : FALSE;
	return wpas_dbus_simple_property_getter(iter, DBUS_TYPE_BOOLEAN,
						&privacy, error);
}


/**
 * wpas_dbus_getter_bss_mode - Return the mode of a BSS
 * @iter: Pointer to incoming dbus message iter
 * @error: Location to store error on failure
 * @user_data: Function specific data
 * Returns: TRUE on success, FALSE on failure
 *
 * Getter for "Mode" property.
 */
dbus_bool_t wpas_dbus_getter_bss_mode(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct bss_handler_args *args = user_data;
	struct wpa_bss *res;
	const char *mode;
	const u8 *mesh;

	res = get_bss_helper(args, error, __func__);
	if (!res)
		return FALSE;
	if (bss_is_dmg(res)) {
		switch (res->caps & IEEE80211_CAP_DMG_MASK) {
		case IEEE80211_CAP_DMG_PBSS:
		case IEEE80211_CAP_DMG_IBSS:
			mode = "ad-hoc";
			break;
		case IEEE80211_CAP_DMG_AP:
			mode = "infrastructure";
			break;
		default:
			mode = "";
			break;
		}
	} else {
		mesh = wpa_bss_get_ie(res, WLAN_EID_MESH_ID);
		if (mesh)
			mode = "mesh";
		else if (res->caps & IEEE80211_CAP_IBSS)
			mode = "ad-hoc";
		else
			mode = "infrastructure";
	}

	return wpas_dbus_simple_property_getter(iter, DBUS_TYPE_STRING,
						&mode, error);
}


/**
 * wpas_dbus_getter_bss_level - Return the signal strength of a BSS
 * @iter: Pointer to incoming dbus message iter
 * @error: Location to store error on failure
 * @user_data: Function specific data
 * Returns: TRUE on success, FALSE on failure
 *
 * Getter for "Level" property.
 */
dbus_bool_t wpas_dbus_getter_bss_signal(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct bss_handler_args *args = user_data;
	struct wpa_bss *res;
	s16 level;

	res = get_bss_helper(args, error, __func__);
	if (!res)
		return FALSE;

	level = (s16) res->level;
	return wpas_dbus_simple_property_getter(iter, DBUS_TYPE_INT16,
						&level, error);
}


/**
 * wpas_dbus_getter_bss_frequency - Return the frequency of a BSS
 * @iter: Pointer to incoming dbus message iter
 * @error: Location to store error on failure
 * @user_data: Function specific data
 * Returns: TRUE on success, FALSE on failure
 *
 * Getter for "Frequency" property.
 */
dbus_bool_t wpas_dbus_getter_bss_frequency(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct bss_handler_args *args = user_data;
	struct wpa_bss *res;
	u16 freq;

	res = get_bss_helper(args, error, __func__);
	if (!res)
		return FALSE;

	freq = (u16) res->freq;
	return wpas_dbus_simple_property_getter(iter, DBUS_TYPE_UINT16,
						&freq, error);
}


static int cmp_u8s_desc(const void *a, const void *b)
{
	return (*(u8 *) b - *(u8 *) a);
}


/**
 * wpas_dbus_getter_bss_rates - Return available bit rates of a BSS
 * @iter: Pointer to incoming dbus message iter
 * @error: Location to store error on failure
 * @user_data: Function specific data
 * Returns: TRUE on success, FALSE on failure
 *
 * Getter for "Rates" property.
 */
dbus_bool_t wpas_dbus_getter_bss_rates(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct bss_handler_args *args = user_data;
	struct wpa_bss *res;
	u8 *ie_rates = NULL;
	u32 *real_rates;
	int rates_num, i;
	dbus_bool_t success = FALSE;

	res = get_bss_helper(args, error, __func__);
	if (!res)
		return FALSE;

	rates_num = wpa_bss_get_bit_rates(res, &ie_rates);
	if (rates_num < 0)
		return FALSE;

	qsort(ie_rates, rates_num, 1, cmp_u8s_desc);

	real_rates = os_malloc(sizeof(u32) * rates_num);
	if (!real_rates) {
		os_free(ie_rates);
		dbus_set_error_const(error, DBUS_ERROR_NO_MEMORY, "no memory");
		return FALSE;
	}

	for (i = 0; i < rates_num; i++)
		real_rates[i] = ie_rates[i] * 500000;

	success = wpas_dbus_simple_array_property_getter(iter, DBUS_TYPE_UINT32,
							 real_rates, rates_num,
							 error);

	os_free(ie_rates);
	os_free(real_rates);
	return success;
}


static dbus_bool_t wpas_dbus_get_bss_security_prop(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, struct wpa_ie_data *ie_data, DBusError *error)
{
	DBusMessageIter iter_dict, variant_iter;
	const char *group;
	const char *pairwise[5]; /* max 5 pairwise ciphers is supported */
	const char *key_mgmt[13]; /* max 13 key managements may be supported */
	int n;

	if (!dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT,
					      "a{sv}", &variant_iter))
		goto nomem;

	if (!wpa_dbus_dict_open_write(&variant_iter, &iter_dict))
		goto nomem;

	/* KeyMgmt */
	n = 0;
	if (ie_data->key_mgmt & WPA_KEY_MGMT_PSK)
		key_mgmt[n++] = "wpa-psk";
	if (ie_data->key_mgmt & WPA_KEY_MGMT_FT_PSK)
		key_mgmt[n++] = "wpa-ft-psk";
	if (ie_data->key_mgmt & WPA_KEY_MGMT_PSK_SHA256)
		key_mgmt[n++] = "wpa-psk-sha256";
	if (ie_data->key_mgmt & WPA_KEY_MGMT_IEEE8021X)
		key_mgmt[n++] = "wpa-eap";
	if (ie_data->key_mgmt & WPA_KEY_MGMT_FT_IEEE8021X)
		key_mgmt[n++] = "wpa-ft-eap";
	if (ie_data->key_mgmt & WPA_KEY_MGMT_IEEE8021X_SHA256)
		key_mgmt[n++] = "wpa-eap-sha256";
#ifdef CONFIG_SUITEB
	if (ie_data->key_mgmt & WPA_KEY_MGMT_IEEE8021X_SUITE_B)
		key_mgmt[n++] = "wpa-eap-suite-b";
#endif /* CONFIG_SUITEB */
#ifdef CONFIG_SUITEB192
	if (ie_data->key_mgmt & WPA_KEY_MGMT_IEEE8021X_SUITE_B_192)
		key_mgmt[n++] = "wpa-eap-suite-b-192";
#endif /* CONFIG_SUITEB192 */
#ifdef CONFIG_FILS
	if (ie_data->key_mgmt & WPA_KEY_MGMT_FILS_SHA256)
		key_mgmt[n++] = "wpa-fils-sha256";
	if (ie_data->key_mgmt & WPA_KEY_MGMT_FILS_SHA384)
		key_mgmt[n++] = "wpa-fils-sha384";
	if (ie_data->key_mgmt & WPA_KEY_MGMT_FT_FILS_SHA256)
		key_mgmt[n++] = "wpa-ft-fils-sha256";
	if (ie_data->key_mgmt & WPA_KEY_MGMT_FT_FILS_SHA384)
		key_mgmt[n++] = "wpa-ft-fils-sha384";
#endif /* CONFIG_FILS */
	if (ie_data->key_mgmt & WPA_KEY_MGMT_NONE)
		key_mgmt[n++] = "wpa-none";

	if (!wpa_dbus_dict_append_string_array(&iter_dict, "KeyMgmt",
					       key_mgmt, n))
		goto nomem;

	/* Group */
	switch (ie_data->group_cipher) {
	case WPA_CIPHER_WEP40:
		group = "wep40";
		break;
	case WPA_CIPHER_TKIP:
		group = "tkip";
		break;
	case WPA_CIPHER_CCMP:
		group = "ccmp";
		break;
	case WPA_CIPHER_GCMP:
		group = "gcmp";
		break;
	case WPA_CIPHER_WEP104:
		group = "wep104";
		break;
	case WPA_CIPHER_CCMP_256:
		group = "ccmp-256";
		break;
	case WPA_CIPHER_GCMP_256:
		group = "gcmp-256";
		break;
	default:
		group = "";
		break;
	}

	if (!wpa_dbus_dict_append_string(&iter_dict, "Group", group))
		goto nomem;

	/* Pairwise */
	n = 0;
	if (ie_data->pairwise_cipher & WPA_CIPHER_TKIP)
		pairwise[n++] = "tkip";
	if (ie_data->pairwise_cipher & WPA_CIPHER_CCMP)
		pairwise[n++] = "ccmp";
	if (ie_data->pairwise_cipher & WPA_CIPHER_GCMP)
		pairwise[n++] = "gcmp";
	if (ie_data->pairwise_cipher & WPA_CIPHER_CCMP_256)
		pairwise[n++] = "ccmp-256";
	if (ie_data->pairwise_cipher & WPA_CIPHER_GCMP_256)
		pairwise[n++] = "gcmp-256";

	if (!wpa_dbus_dict_append_string_array(&iter_dict, "Pairwise",
					       pairwise, n))
		goto nomem;

	/* Management group (RSN only) */
	if (ie_data->proto == WPA_PROTO_RSN) {
		switch (ie_data->mgmt_group_cipher) {
#ifdef CONFIG_IEEE80211W
		case WPA_CIPHER_AES_128_CMAC:
			group = "aes128cmac";
			break;
#endif /* CONFIG_IEEE80211W */
		default:
			group = "";
			break;
		}

		if (!wpa_dbus_dict_append_string(&iter_dict, "MgmtGroup",
						 group))
			goto nomem;
	}

	if (!wpa_dbus_dict_close_write(&variant_iter, &iter_dict) ||
	    !dbus_message_iter_close_container(iter, &variant_iter))
		goto nomem;

	return TRUE;

nomem:
	dbus_set_error_const(error, DBUS_ERROR_NO_MEMORY, "no memory");
	return FALSE;
}


/**
 * wpas_dbus_getter_bss_wpa - Return the WPA options of a BSS
 * @iter: Pointer to incoming dbus message iter
 * @error: Location to store error on failure
 * @user_data: Function specific data
 * Returns: TRUE on success, FALSE on failure
 *
 * Getter for "WPA" property.
 */
dbus_bool_t wpas_dbus_getter_bss_wpa(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct bss_handler_args *args = user_data;
	struct wpa_bss *res;
	struct wpa_ie_data wpa_data;
	const u8 *ie;

	res = get_bss_helper(args, error, __func__);
	if (!res)
		return FALSE;

	os_memset(&wpa_data, 0, sizeof(wpa_data));
	ie = wpa_bss_get_vendor_ie(res, WPA_IE_VENDOR_TYPE);
	if (ie && wpa_parse_wpa_ie(ie, 2 + ie[1], &wpa_data) < 0) {
		dbus_set_error_const(error, DBUS_ERROR_FAILED,
				     "failed to parse WPA IE");
		return FALSE;
	}

	return wpas_dbus_get_bss_security_prop(property_desc, iter, &wpa_data, error);
}


/**
 * wpas_dbus_getter_bss_rsn - Return the RSN options of a BSS
 * @iter: Pointer to incoming dbus message iter
 * @error: Location to store error on failure
 * @user_data: Function specific data
 * Returns: TRUE on success, FALSE on failure
 *
 * Getter for "RSN" property.
 */
dbus_bool_t wpas_dbus_getter_bss_rsn(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct bss_handler_args *args = user_data;
	struct wpa_bss *res;
	struct wpa_ie_data wpa_data;
	const u8 *ie;

	res = get_bss_helper(args, error, __func__);
	if (!res)
		return FALSE;

	os_memset(&wpa_data, 0, sizeof(wpa_data));
	ie = wpa_bss_get_ie(res, WLAN_EID_RSN);
	if (ie && wpa_parse_wpa_ie(ie, 2 + ie[1], &wpa_data) < 0) {
		dbus_set_error_const(error, DBUS_ERROR_FAILED,
				     "failed to parse RSN IE");
		return FALSE;
	}

	return wpas_dbus_get_bss_security_prop(property_desc, iter, &wpa_data, error);
}


/**
 * wpas_dbus_getter_bss_wps - Return the WPS options of a BSS
 * @iter: Pointer to incoming dbus message iter
 * @error: Location to store error on failure
 * @user_data: Function specific data
 * Returns: TRUE on success, FALSE on failure
 *
 * Getter for "WPS" property.
 */
dbus_bool_t wpas_dbus_getter_bss_wps(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct bss_handler_args *args = user_data;
	struct wpa_bss *res;
#ifdef CONFIG_WPS
	struct wpabuf *wps_ie;
#endif /* CONFIG_WPS */
	DBusMessageIter iter_dict, variant_iter;
	int wps_support = 0;
	const char *type = "";

	res = get_bss_helper(args, error, __func__);
	if (!res)
		return FALSE;

	if (!dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT,
					      "a{sv}", &variant_iter) ||
	    !wpa_dbus_dict_open_write(&variant_iter, &iter_dict))
		goto nomem;

#ifdef CONFIG_WPS
	wps_ie = wpa_bss_get_vendor_ie_multi(res, WPS_IE_VENDOR_TYPE);
	if (wps_ie) {
		wps_support = 1;
		if (wps_is_selected_pbc_registrar(wps_ie))
			type = "pbc";
		else if (wps_is_selected_pin_registrar(wps_ie))
			type = "pin";

		wpabuf_free(wps_ie);
	}
#endif /* CONFIG_WPS */

	if ((wps_support && !wpa_dbus_dict_append_string(&iter_dict, "Type", type)) ||
	    !wpa_dbus_dict_close_write(&variant_iter, &iter_dict) ||
	    !dbus_message_iter_close_container(iter, &variant_iter))
		goto nomem;

	return TRUE;

nomem:
	dbus_set_error_const(error, DBUS_ERROR_NO_MEMORY, "no memory");
	return FALSE;
}


/**
 * wpas_dbus_getter_bss_ies - Return all IEs of a BSS
 * @iter: Pointer to incoming dbus message iter
 * @error: Location to store error on failure
 * @user_data: Function specific data
 * Returns: TRUE on success, FALSE on failure
 *
 * Getter for "IEs" property.
 */
dbus_bool_t wpas_dbus_getter_bss_ies(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct bss_handler_args *args = user_data;
	struct wpa_bss *res;

	res = get_bss_helper(args, error, __func__);
	if (!res)
		return FALSE;

	return wpas_dbus_simple_array_property_getter(iter, DBUS_TYPE_BYTE,
						      res + 1, res->ie_len,
						      error);
}


/**
 * wpas_dbus_getter_bss_age - Return time in seconds since BSS was last seen
 * @iter: Pointer to incoming dbus message iter
 * @error: Location to store error on failure
 * @user_data: Function specific data
 * Returns: TRUE on success, FALSE on failure
 *
 * Getter for BSS age
 */
dbus_bool_t wpas_dbus_getter_bss_age(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct bss_handler_args *args = user_data;
	struct wpa_bss *res;
	struct os_reltime now, diff = { 0, 0 };
	u32 age;

	res = get_bss_helper(args, error, __func__);
	if (!res)
		return FALSE;

	os_get_reltime(&now);
	os_reltime_sub(&now, &res->last_update, &diff);
	age = diff.sec > 0 ? diff.sec : 0;
	return wpas_dbus_simple_property_getter(iter, DBUS_TYPE_UINT32, &age,
						error);
}


/**
 * wpas_dbus_getter_enabled - Check whether network is enabled or disabled
 * @iter: Pointer to incoming dbus message iter
 * @error: Location to store error on failure
 * @user_data: Function specific data
 * Returns: TRUE on success, FALSE on failure
 *
 * Getter for "enabled" property of a configured network.
 */
dbus_bool_t wpas_dbus_getter_enabled(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct network_handler_args *net = user_data;
	dbus_bool_t enabled = net->ssid->disabled ? FALSE : TRUE;

	return wpas_dbus_simple_property_getter(iter, DBUS_TYPE_BOOLEAN,
						&enabled, error);
}


/**
 * wpas_dbus_setter_enabled - Mark a configured network as enabled or disabled
 * @iter: Pointer to incoming dbus message iter
 * @error: Location to store error on failure
 * @user_data: Function specific data
 * Returns: TRUE on success, FALSE on failure
 *
 * Setter for "Enabled" property of a configured network.
 */
dbus_bool_t wpas_dbus_setter_enabled(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct network_handler_args *net = user_data;
	struct wpa_supplicant *wpa_s;
	struct wpa_ssid *ssid;
	dbus_bool_t enable;

	if (!wpas_dbus_simple_property_setter(iter, error, DBUS_TYPE_BOOLEAN,
					      &enable))
		return FALSE;

	wpa_s = net->wpa_s;
	ssid = net->ssid;

	if (enable)
		wpa_supplicant_enable_network(wpa_s, ssid);
	else
		wpa_supplicant_disable_network(wpa_s, ssid);

	return TRUE;
}


/**
 * wpas_dbus_getter_network_properties - Get options for a configured network
 * @iter: Pointer to incoming dbus message iter
 * @error: Location to store error on failure
 * @user_data: Function specific data
 * Returns: TRUE on success, FALSE on failure
 *
 * Getter for "Properties" property of a configured network.
 */
dbus_bool_t wpas_dbus_getter_network_properties(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct network_handler_args *net = user_data;
	DBusMessageIter	variant_iter, dict_iter;
	char **iterator;
	char **props = wpa_config_get_all(net->ssid, 1);
	dbus_bool_t success = FALSE;

	if (!props) {
		dbus_set_error_const(error, DBUS_ERROR_NO_MEMORY, "no memory");
		return FALSE;
	}

	if (!dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT, "a{sv}",
					      &variant_iter) ||
	    !wpa_dbus_dict_open_write(&variant_iter, &dict_iter)) {
		dbus_set_error_const(error, DBUS_ERROR_NO_MEMORY, "no memory");
		goto out;
	}

	iterator = props;
	while (*iterator) {
		if (!wpa_dbus_dict_append_string(&dict_iter, *iterator,
						 *(iterator + 1))) {
			dbus_set_error_const(error, DBUS_ERROR_NO_MEMORY,
					     "no memory");
			goto out;
		}
		iterator += 2;
	}


	if (!wpa_dbus_dict_close_write(&variant_iter, &dict_iter) ||
	    !dbus_message_iter_close_container(iter, &variant_iter)) {
		dbus_set_error_const(error, DBUS_ERROR_NO_MEMORY, "no memory");
		goto out;
	}

	success = TRUE;

out:
	iterator = props;
	while (*iterator) {
		os_free(*iterator);
		iterator++;
	}
	os_free(props);
	return success;
}


/**
 * wpas_dbus_setter_network_properties - Set options for a configured network
 * @iter: Pointer to incoming dbus message iter
 * @error: Location to store error on failure
 * @user_data: Function specific data
 * Returns: TRUE on success, FALSE on failure
 *
 * Setter for "Properties" property of a configured network.
 */
dbus_bool_t wpas_dbus_setter_network_properties(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct network_handler_args *net = user_data;
	struct wpa_ssid *ssid = net->ssid;
	DBusMessageIter	variant_iter;

	dbus_message_iter_recurse(iter, &variant_iter);
	return set_network_properties(net->wpa_s, ssid, &variant_iter, error);
}


#ifdef CONFIG_AP

DBusMessage * wpas_dbus_handler_subscribe_preq(
	DBusMessage *message, struct wpa_supplicant *wpa_s)
{
	struct wpas_dbus_priv *priv = wpa_s->global->dbus;
	char *name;

	if (wpa_s->preq_notify_peer != NULL) {
		if (os_strcmp(dbus_message_get_sender(message),
			      wpa_s->preq_notify_peer) == 0)
			return NULL;

		return dbus_message_new_error(message,
			WPAS_DBUS_ERROR_SUBSCRIPTION_IN_USE,
			"Another application is already subscribed");
	}

	name = os_strdup(dbus_message_get_sender(message));
	if (!name)
		return wpas_dbus_error_no_memory(message);

	wpa_s->preq_notify_peer = name;

	/* Subscribe to clean up if application closes socket */
	wpas_dbus_subscribe_noc(priv);

	/*
	 * Double-check it's still alive to make sure that we didn't
	 * miss the NameOwnerChanged signal, e.g. while strdup'ing.
	 */
	if (!dbus_bus_name_has_owner(priv->con, name, NULL)) {
		/*
		 * Application no longer exists, clean up.
		 * The return value is irrelevant now.
		 *
		 * Need to check if the NameOwnerChanged handling
		 * already cleaned up because we have processed
		 * DBus messages while checking if the name still
		 * has an owner.
		 */
		if (!wpa_s->preq_notify_peer)
			return NULL;
		os_free(wpa_s->preq_notify_peer);
		wpa_s->preq_notify_peer = NULL;
		wpas_dbus_unsubscribe_noc(priv);
	}

	return NULL;
}


DBusMessage * wpas_dbus_handler_unsubscribe_preq(
	DBusMessage *message, struct wpa_supplicant *wpa_s)
{
	struct wpas_dbus_priv *priv = wpa_s->global->dbus;

	if (!wpa_s->preq_notify_peer)
		return dbus_message_new_error(message,
			WPAS_DBUS_ERROR_NO_SUBSCRIPTION,
			"Not subscribed");

	if (os_strcmp(wpa_s->preq_notify_peer,
		      dbus_message_get_sender(message)))
		return dbus_message_new_error(message,
			WPAS_DBUS_ERROR_SUBSCRIPTION_EPERM,
			"Can't unsubscribe others");

	os_free(wpa_s->preq_notify_peer);
	wpa_s->preq_notify_peer = NULL;
	wpas_dbus_unsubscribe_noc(priv);
	return NULL;
}


void wpas_dbus_signal_preq(struct wpa_supplicant *wpa_s,
			   const u8 *addr, const u8 *dst, const u8 *bssid,
			   const u8 *ie, size_t ie_len, u32 ssi_signal)
{
	DBusMessage *msg;
	DBusMessageIter iter, dict_iter;
	struct wpas_dbus_priv *priv = wpa_s->global->dbus;

	/* Do nothing if the control interface is not turned on */
	if (priv == NULL || !wpa_s->dbus_new_path)
		return;

	if (wpa_s->preq_notify_peer == NULL)
		return;

	msg = dbus_message_new_signal(wpa_s->dbus_new_path,
				      WPAS_DBUS_NEW_IFACE_INTERFACE,
				      "ProbeRequest");
	if (msg == NULL)
		return;

	dbus_message_set_destination(msg, wpa_s->preq_notify_peer);

	dbus_message_iter_init_append(msg, &iter);

	if (!wpa_dbus_dict_open_write(&iter, &dict_iter) ||
	    (addr && !wpa_dbus_dict_append_byte_array(&dict_iter, "addr",
						      (const char *) addr,
						      ETH_ALEN)) ||
	    (dst && !wpa_dbus_dict_append_byte_array(&dict_iter, "dst",
						     (const char *) dst,
						     ETH_ALEN)) ||
	    (bssid && !wpa_dbus_dict_append_byte_array(&dict_iter, "bssid",
						       (const char *) bssid,
						       ETH_ALEN)) ||
	    (ie && ie_len && !wpa_dbus_dict_append_byte_array(&dict_iter, "ies",
							      (const char *) ie,
							      ie_len)) ||
	    (ssi_signal && !wpa_dbus_dict_append_int32(&dict_iter, "signal",
						       ssi_signal)) ||
	    !wpa_dbus_dict_close_write(&iter, &dict_iter))
		goto fail;

	dbus_connection_send(priv->con, msg, NULL);
	goto out;
fail:
	wpa_printf(MSG_ERROR, "dbus: Failed to construct signal");
out:
	dbus_message_unref(msg);
}

#endif /* CONFIG_AP */


DBusMessage * wpas_dbus_handler_vendor_elem_add(DBusMessage *message,
						struct wpa_supplicant *wpa_s)
{
	u8 *ielems;
	int len;
	struct ieee802_11_elems elems;
	dbus_int32_t frame_id;
	DBusMessageIter	iter, array;

	dbus_message_iter_init(message, &iter);
	dbus_message_iter_get_basic(&iter, &frame_id);
	if (frame_id < 0 || frame_id >= NUM_VENDOR_ELEM_FRAMES) {
		return dbus_message_new_error(message, DBUS_ERROR_INVALID_ARGS,
					      "Invalid ID");
	}

	dbus_message_iter_next(&iter);
	dbus_message_iter_recurse(&iter, &array);
	dbus_message_iter_get_fixed_array(&array, &ielems, &len);
	if (!ielems || len == 0) {
		return dbus_message_new_error(
			message, DBUS_ERROR_INVALID_ARGS, "Invalid value");
	}

	if (ieee802_11_parse_elems(ielems, len, &elems, 0) == ParseFailed) {
		return dbus_message_new_error(message, DBUS_ERROR_INVALID_ARGS,
					      "Parse error");
	}

	wpa_s = wpas_vendor_elem(wpa_s, frame_id);
	if (!wpa_s->vendor_elem[frame_id]) {
		wpa_s->vendor_elem[frame_id] = wpabuf_alloc_copy(ielems, len);
		wpas_vendor_elem_update(wpa_s);
		return NULL;
	}

	if (wpabuf_resize(&wpa_s->vendor_elem[frame_id], len) < 0) {
		return dbus_message_new_error(message, DBUS_ERROR_INVALID_ARGS,
					      "Resize error");
	}

	wpabuf_put_data(wpa_s->vendor_elem[frame_id], ielems, len);
	wpas_vendor_elem_update(wpa_s);
	return NULL;
}


DBusMessage * wpas_dbus_handler_vendor_elem_get(DBusMessage *message,
						struct wpa_supplicant *wpa_s)
{
	DBusMessage *reply;
	DBusMessageIter	iter, array_iter;
	dbus_int32_t frame_id;
	const u8 *elem;
	size_t elem_len;

	dbus_message_iter_init(message, &iter);
	dbus_message_iter_get_basic(&iter, &frame_id);

	if (frame_id < 0 || frame_id >= NUM_VENDOR_ELEM_FRAMES) {
		return dbus_message_new_error(message, DBUS_ERROR_INVALID_ARGS,
					      "Invalid ID");
	}

	wpa_s = wpas_vendor_elem(wpa_s, frame_id);
	if (!wpa_s->vendor_elem[frame_id]) {
		return dbus_message_new_error(message, DBUS_ERROR_INVALID_ARGS,
					      "ID value does not exist");
	}

	reply = dbus_message_new_method_return(message);
	if (!reply)
		return wpas_dbus_error_no_memory(message);

	dbus_message_iter_init_append(reply, &iter);

	elem = wpabuf_head_u8(wpa_s->vendor_elem[frame_id]);
	elem_len = wpabuf_len(wpa_s->vendor_elem[frame_id]);

	if (!dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY,
					      DBUS_TYPE_BYTE_AS_STRING,
					      &array_iter) ||
	    !dbus_message_iter_append_fixed_array(&array_iter, DBUS_TYPE_BYTE,
						  &elem, elem_len) ||
	    !dbus_message_iter_close_container(&iter, &array_iter)) {
		dbus_message_unref(reply);
		reply = wpas_dbus_error_no_memory(message);
	}

	return reply;
}


DBusMessage * wpas_dbus_handler_vendor_elem_remove(DBusMessage *message,
						   struct wpa_supplicant *wpa_s)
{
	u8 *ielems;
	int len;
	struct ieee802_11_elems elems;
	DBusMessageIter	iter, array;
	dbus_int32_t frame_id;

	dbus_message_iter_init(message, &iter);
	dbus_message_iter_get_basic(&iter, &frame_id);
	if (frame_id < 0 || frame_id >= NUM_VENDOR_ELEM_FRAMES) {
		return dbus_message_new_error(message, DBUS_ERROR_INVALID_ARGS,
					      "Invalid ID");
	}

	dbus_message_iter_next(&iter);
	dbus_message_iter_recurse(&iter, &array);
	dbus_message_iter_get_fixed_array(&array, &ielems, &len);
	if (!ielems || len == 0) {
		return dbus_message_new_error(message, DBUS_ERROR_INVALID_ARGS,
					      "Invalid value");
	}

	wpa_s = wpas_vendor_elem(wpa_s, frame_id);

	if (len == 1 && *ielems == '*') {
		wpabuf_free(wpa_s->vendor_elem[frame_id]);
		wpa_s->vendor_elem[frame_id] = NULL;
		wpas_vendor_elem_update(wpa_s);
		return NULL;
	}

	if (!wpa_s->vendor_elem[frame_id]) {
		return dbus_message_new_error(message, DBUS_ERROR_INVALID_ARGS,
					      "ID value does not exist");
	}

	if (ieee802_11_parse_elems(ielems, len, &elems, 0) == ParseFailed) {
		return dbus_message_new_error(message, DBUS_ERROR_INVALID_ARGS,
					      "Parse error");
	}

	if (wpas_vendor_elem_remove(wpa_s, frame_id, ielems, len) == 0)
		return NULL;

	return dbus_message_new_error(message, DBUS_ERROR_INVALID_ARGS,
				      "Not found");
}


#ifdef CONFIG_MESH

/**
 * wpas_dbus_getter_mesh_peers - Get connected mesh peers
 * @iter: Pointer to incoming dbus message iter
 * @error: Location to store error on failure
 * @user_data: Function specific data
 * Returns: TRUE on success, FALSE on failure
 *
 * Getter for "MeshPeers" property.
 */
dbus_bool_t wpas_dbus_getter_mesh_peers(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct wpa_supplicant *wpa_s = user_data;
	struct hostapd_data *hapd;
	struct sta_info *sta;
	DBusMessageIter variant_iter, array_iter;
	int i;
	DBusMessageIter inner_array_iter;

	if (!wpa_s->ifmsh)
		return FALSE;
	hapd = wpa_s->ifmsh->bss[0];

	if (!dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT,
					      DBUS_TYPE_ARRAY_AS_STRING
					      DBUS_TYPE_ARRAY_AS_STRING
					      DBUS_TYPE_BYTE_AS_STRING,
					      &variant_iter) ||
	    !dbus_message_iter_open_container(&variant_iter, DBUS_TYPE_ARRAY,
					      DBUS_TYPE_ARRAY_AS_STRING
					      DBUS_TYPE_BYTE_AS_STRING,
					      &array_iter))
		return FALSE;

	for (sta = hapd->sta_list; sta; sta = sta->next) {
		if (!dbus_message_iter_open_container(
			    &array_iter, DBUS_TYPE_ARRAY,
			    DBUS_TYPE_BYTE_AS_STRING,
			    &inner_array_iter))
			return FALSE;

		for (i = 0; i < ETH_ALEN; i++) {
			if (!dbus_message_iter_append_basic(&inner_array_iter,
							    DBUS_TYPE_BYTE,
							    &(sta->addr[i])))
				return FALSE;
		}

		if (!dbus_message_iter_close_container(
			    &array_iter, &inner_array_iter))
			return FALSE;
	}

	if (!dbus_message_iter_close_container(&variant_iter, &array_iter) ||
	    !dbus_message_iter_close_container(iter, &variant_iter))
		return FALSE;

	return TRUE;
}


/**
 * wpas_dbus_getter_mesh_group - Get mesh group
 * @iter: Pointer to incoming dbus message iter
 * @error: Location to store error on failure
 * @user_data: Function specific data
 * Returns: TRUE on success, FALSE on failure
 *
 * Getter for "MeshGroup" property.
 */
dbus_bool_t wpas_dbus_getter_mesh_group(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct wpa_supplicant *wpa_s = user_data;
	struct wpa_ssid *ssid = wpa_s->current_ssid;

	if (!wpa_s->ifmsh || !ssid)
		return FALSE;

	if (!wpas_dbus_simple_array_property_getter(iter, DBUS_TYPE_BYTE,
						    (char *) ssid->ssid,
						    ssid->ssid_len, error)) {
		dbus_set_error(error, DBUS_ERROR_FAILED,
			       "%s: error constructing reply", __func__);
		return FALSE;
	}

	return TRUE;
}

#endif /* CONFIG_MESH */
