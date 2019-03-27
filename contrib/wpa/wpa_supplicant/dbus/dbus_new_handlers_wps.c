/*
 * WPA Supplicant / dbus-based control interface (WPS)
 * Copyright (c) 2006, Dan Williams <dcbw@redhat.com> and Red Hat, Inc.
 * Copyright (c) 2009, Witold Sowa <witold.sowa@gmail.com>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"

#include "common.h"
#include "../config.h"
#include "../wpa_supplicant_i.h"
#include "../wps_supplicant.h"
#include "../driver_i.h"
#include "../ap.h"
#include "dbus_new_helpers.h"
#include "dbus_new.h"
#include "dbus_new_handlers.h"
#include "dbus_dict_helpers.h"


struct wps_start_params {
	int role; /* 0 - not set, 1 - enrollee, 2 - registrar */
	int type; /* 0 - not set, 1 - pin,      2 - pbc       */
	u8 *bssid;
	char *pin;
	u8 *p2p_dev_addr;
};


static int wpas_dbus_handler_wps_role(DBusMessage *message,
				      DBusMessageIter *entry_iter,
				      struct wps_start_params *params,
				      DBusMessage **reply)
{
	DBusMessageIter variant_iter;
	char *val;

	dbus_message_iter_recurse(entry_iter, &variant_iter);
	if (dbus_message_iter_get_arg_type(&variant_iter) !=
	    DBUS_TYPE_STRING) {
		wpa_printf(MSG_DEBUG,
			   "dbus: WPS.Start - Wrong Role type, string required");
		*reply = wpas_dbus_error_invalid_args(message,
						      "Role must be a string");
		return -1;
	}
	dbus_message_iter_get_basic(&variant_iter, &val);
	if (os_strcmp(val, "enrollee") == 0)
		params->role = 1;
	else if (os_strcmp(val, "registrar") == 0)
		params->role = 2;
	else {
		wpa_printf(MSG_DEBUG, "dbus: WPS.Start - Unknown role %s", val);
		*reply = wpas_dbus_error_invalid_args(message, val);
		return -1;
	}
	return 0;
}


static int wpas_dbus_handler_wps_type(DBusMessage *message,
				      DBusMessageIter *entry_iter,
				      struct wps_start_params *params,
				      DBusMessage **reply)
{
	DBusMessageIter variant_iter;
	char *val;

	dbus_message_iter_recurse(entry_iter, &variant_iter);
	if (dbus_message_iter_get_arg_type(&variant_iter) != DBUS_TYPE_STRING) {
		wpa_printf(MSG_DEBUG,
			   "dbus: WPS.Start - Wrong Type type, string required");
		*reply = wpas_dbus_error_invalid_args(message,
						      "Type must be a string");
		return -1;
	}
	dbus_message_iter_get_basic(&variant_iter, &val);
	if (os_strcmp(val, "pin") == 0)
		params->type = 1;
	else if (os_strcmp(val, "pbc") == 0)
		params->type = 2;
	else {
		wpa_printf(MSG_DEBUG, "dbus: WPS.Start - Unknown type %s",
			   val);
		*reply = wpas_dbus_error_invalid_args(message, val);
		return -1;
	}
	return 0;
}


static int wpas_dbus_handler_wps_bssid(DBusMessage *message,
				       DBusMessageIter *entry_iter,
				       struct wps_start_params *params,
				       DBusMessage **reply)
{
	DBusMessageIter variant_iter, array_iter;
	int len;

	dbus_message_iter_recurse(entry_iter, &variant_iter);
	if (dbus_message_iter_get_arg_type(&variant_iter) != DBUS_TYPE_ARRAY ||
	    dbus_message_iter_get_element_type(&variant_iter) !=
	    DBUS_TYPE_BYTE) {
		wpa_printf(MSG_DEBUG,
			   "dbus: WPS.Start - Wrong Bssid type, byte array required");
		*reply = wpas_dbus_error_invalid_args(
			message, "Bssid must be a byte array");
		return -1;
	}
	dbus_message_iter_recurse(&variant_iter, &array_iter);
	dbus_message_iter_get_fixed_array(&array_iter, &params->bssid, &len);
	if (len != ETH_ALEN) {
		wpa_printf(MSG_DEBUG, "dbus: WPS.Start - Wrong Bssid length %d",
			   len);
		*reply = wpas_dbus_error_invalid_args(message,
						      "Bssid is wrong length");
		return -1;
	}
	return 0;
}


static int wpas_dbus_handler_wps_pin(DBusMessage *message,
				     DBusMessageIter *entry_iter,
				     struct wps_start_params *params,
				     DBusMessage **reply)
{
	DBusMessageIter variant_iter;

	dbus_message_iter_recurse(entry_iter, &variant_iter);
	if (dbus_message_iter_get_arg_type(&variant_iter) != DBUS_TYPE_STRING) {
		wpa_printf(MSG_DEBUG,
			   "dbus: WPS.Start - Wrong Pin type, string required");
		*reply = wpas_dbus_error_invalid_args(message,
						      "Pin must be a string");
		return -1;
	}
	dbus_message_iter_get_basic(&variant_iter, &params->pin);
	return 0;
}


#ifdef CONFIG_P2P
static int wpas_dbus_handler_wps_p2p_dev_addr(DBusMessage *message,
					      DBusMessageIter *entry_iter,
					      struct wps_start_params *params,
					      DBusMessage **reply)
{
	DBusMessageIter variant_iter, array_iter;
	int len;

	dbus_message_iter_recurse(entry_iter, &variant_iter);
	if (dbus_message_iter_get_arg_type(&variant_iter) != DBUS_TYPE_ARRAY ||
	    dbus_message_iter_get_element_type(&variant_iter) !=
	    DBUS_TYPE_BYTE) {
		wpa_printf(MSG_DEBUG,
			   "dbus: WPS.Start - Wrong P2PDeviceAddress type, byte array required");
		*reply = wpas_dbus_error_invalid_args(
			message, "P2PDeviceAddress must be a byte array");
		return -1;
	}
	dbus_message_iter_recurse(&variant_iter, &array_iter);
	dbus_message_iter_get_fixed_array(&array_iter, &params->p2p_dev_addr,
					  &len);
	if (len != ETH_ALEN) {
		wpa_printf(MSG_DEBUG,
			   "dbus: WPS.Start - Wrong P2PDeviceAddress length %d",
			   len);
		*reply = wpas_dbus_error_invalid_args(
			message, "P2PDeviceAddress has wrong length");
		return -1;
	}
	return 0;
}
#endif /* CONFIG_P2P */


static int wpas_dbus_handler_wps_start_entry(DBusMessage *message, char *key,
					     DBusMessageIter *entry_iter,
					     struct wps_start_params *params,
					     DBusMessage **reply)
{
	if (os_strcmp(key, "Role") == 0)
		return wpas_dbus_handler_wps_role(message, entry_iter,
						  params, reply);
	else if (os_strcmp(key, "Type") == 0)
		return wpas_dbus_handler_wps_type(message, entry_iter,
						  params, reply);
	else if (os_strcmp(key, "Bssid") == 0)
		return wpas_dbus_handler_wps_bssid(message, entry_iter,
						   params, reply);
	else if (os_strcmp(key, "Pin") == 0)
		return wpas_dbus_handler_wps_pin(message, entry_iter,
						 params, reply);
#ifdef CONFIG_P2P
	else if (os_strcmp(key, "P2PDeviceAddress") == 0)
		return wpas_dbus_handler_wps_p2p_dev_addr(message, entry_iter,
							  params, reply);
#endif /* CONFIG_P2P */

	wpa_printf(MSG_DEBUG, "dbus: WPS.Start - unknown key %s", key);
	*reply = wpas_dbus_error_invalid_args(message, key);
	return -1;
}


/**
 * wpas_dbus_handler_wps_start - Start WPS configuration
 * @message: Pointer to incoming dbus message
 * @wpa_s: %wpa_supplicant data structure
 * Returns: DBus message dictionary on success or DBus error on failure
 *
 * Handler for "Start" method call. DBus dictionary argument contains
 * information about role (enrollee or registrar), authorization method
 * (pin or push button) and optionally pin and bssid. Returned message
 * has a dictionary argument which may contain newly generated pin (optional).
 */
DBusMessage * wpas_dbus_handler_wps_start(DBusMessage *message,
					  struct wpa_supplicant *wpa_s)
{
	DBusMessage *reply = NULL;
	DBusMessageIter iter, dict_iter, entry_iter;
	struct wps_start_params params;
	char *key;
	char npin[9] = { '\0' };
	int ret;

	os_memset(&params, 0, sizeof(params));
	dbus_message_iter_init(message, &iter);

	dbus_message_iter_recurse(&iter, &dict_iter);
	while (dbus_message_iter_get_arg_type(&dict_iter) ==
	       DBUS_TYPE_DICT_ENTRY) {
		dbus_message_iter_recurse(&dict_iter, &entry_iter);

		dbus_message_iter_get_basic(&entry_iter, &key);
		dbus_message_iter_next(&entry_iter);

		if (wpas_dbus_handler_wps_start_entry(message, key,
						      &entry_iter,
						      &params, &reply))
			return reply;

		dbus_message_iter_next(&dict_iter);
	}

#ifdef CONFIG_AP
	if (wpa_s->ap_iface && params.type == 1) {
		if (params.pin == NULL) {
			wpa_printf(MSG_DEBUG,
				   "dbus: WPS.Start - Pin required for registrar role");
			return wpas_dbus_error_invalid_args(
				message, "Pin required for registrar role.");
		}
		ret = wpa_supplicant_ap_wps_pin(wpa_s,
						params.bssid,
						params.pin,
						npin, sizeof(npin), 0);
	} else if (wpa_s->ap_iface) {
		ret = wpa_supplicant_ap_wps_pbc(wpa_s,
						params.bssid,
						params.p2p_dev_addr);
	} else
#endif /* CONFIG_AP */
	if (params.role == 0) {
		wpa_printf(MSG_DEBUG, "dbus: WPS.Start - Role not specified");
		return wpas_dbus_error_invalid_args(message,
						    "Role not specified");
	} else if (params.role == 2) {
		if (params.pin == NULL) {
			wpa_printf(MSG_DEBUG,
				   "dbus: WPS.Start - Pin required for registrar role");
			return wpas_dbus_error_invalid_args(
				message, "Pin required for registrar role.");
		}
		ret = wpas_wps_start_reg(wpa_s, params.bssid, params.pin,
					 NULL);
	} else if (params.type == 0) {
		wpa_printf(MSG_DEBUG, "dbus: WPS.Start - Type not specified");
		return wpas_dbus_error_invalid_args(message,
						    "Type not specified");
	} else if (params.type == 1) {
		ret = wpas_wps_start_pin(wpa_s, params.bssid,
					 params.pin, 0,
					 DEV_PW_DEFAULT);
		if (ret > 0)
			os_snprintf(npin, sizeof(npin), "%08d", ret);
	} else {
		ret = wpas_wps_start_pbc(wpa_s, params.bssid, 0);
	}

	if (ret < 0) {
		wpa_printf(MSG_DEBUG,
			   "dbus: WPS.Start wpas_wps_failed in role %s and key %s",
			   (params.role == 1 ? "enrollee" : "registrar"),
			   (params.type == 0 ? "" :
			    (params.type == 1 ? "pin" : "pbc")));
		return wpas_dbus_error_unknown_error(message,
						     "WPS start failed");
	}

	reply = dbus_message_new_method_return(message);
	if (!reply)
		return wpas_dbus_error_no_memory(message);

	dbus_message_iter_init_append(reply, &iter);
	if (!wpa_dbus_dict_open_write(&iter, &dict_iter) ||
	    (os_strlen(npin) > 0 &&
	     !wpa_dbus_dict_append_string(&dict_iter, "Pin", npin)) ||
	    !wpa_dbus_dict_close_write(&iter, &dict_iter)) {
		dbus_message_unref(reply);
		return wpas_dbus_error_no_memory(message);
	}

	return reply;
}


/**
 * wpas_dbus_handler_wps_cancel - Cancel ongoing WPS configuration
 * @message: Pointer to incoming dbus message
 * @wpa_s: %wpa_supplicant data structure
 * Returns: NULL on success or DBus error on failure
 *
 * Handler for "Cancel" method call. Returns NULL if WPS cancel successful
 * or DBus error on WPS cancel failure
 */
DBusMessage * wpas_dbus_handler_wps_cancel(DBusMessage *message,
					   struct wpa_supplicant *wpa_s)
{
	if (wpas_wps_cancel(wpa_s))
		return wpas_dbus_error_unknown_error(message,
						     "WPS cancel failed");

	return NULL;
}


/**
 * wpas_dbus_getter_process_credentials - Check if credentials are processed
 * @message: Pointer to incoming dbus message
 * @wpa_s: %wpa_supplicant data structure
 * Returns: TRUE on success, FALSE on failure
 *
 * Getter for "ProcessCredentials" property. Returns returned boolean will be
 * true if wps_cred_processing configuration field is not equal to 1 or false
 * if otherwise.
 */
dbus_bool_t wpas_dbus_getter_process_credentials(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct wpa_supplicant *wpa_s = user_data;
	dbus_bool_t process = wpa_s->conf->wps_cred_processing != 1;

	return wpas_dbus_simple_property_getter(iter, DBUS_TYPE_BOOLEAN,
						&process, error);
}


/**
 * wpas_dbus_setter_process_credentials - Set credentials_processed conf param
 * @iter: Pointer to incoming dbus message iter
 * @error: Location to store error on failure
 * @user_data: Function specific data
 * Returns: TRUE on success, FALSE on failure
 *
 * Setter for "ProcessCredentials" property. Sets credentials_processed on 2
 * if boolean argument is true or on 1 if otherwise.
 */
dbus_bool_t wpas_dbus_setter_process_credentials(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct wpa_supplicant *wpa_s = user_data;
	dbus_bool_t process_credentials, old_pc;

	if (!wpa_s->dbus_new_path)
		return FALSE;
	if (!wpas_dbus_simple_property_setter(iter, error, DBUS_TYPE_BOOLEAN,
					      &process_credentials))
		return FALSE;

	old_pc = wpa_s->conf->wps_cred_processing != 1;
	wpa_s->conf->wps_cred_processing = (process_credentials ? 2 : 1);

	if ((wpa_s->conf->wps_cred_processing != 1) != old_pc)
		wpa_dbus_mark_property_changed(wpa_s->global->dbus,
					       wpa_s->dbus_new_path,
					       WPAS_DBUS_NEW_IFACE_WPS,
					       "ProcessCredentials");

	return TRUE;
}


/**
 * wpas_dbus_getter_config_methods - Get current WPS configuration methods
 * @iter: Pointer to incoming dbus message iter
 * @error: Location to store error on failure
 * @user_data: Function specific data
 * Returns: TRUE on success, FALSE on failure
 *
 * Getter for "ConfigMethods" property. Returned boolean will be true if
 * providing the relevant string worked, or false otherwise.
 */
dbus_bool_t wpas_dbus_getter_config_methods(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct wpa_supplicant *wpa_s = user_data;

	return wpas_dbus_string_property_getter(iter,
						wpa_s->conf->config_methods,
						error);
}


/**
 * wpas_dbus_setter_config_methods - Set WPS configuration methods
 * @iter: Pointer to incoming dbus message iter
 * @error: Location to store error on failure
 * @user_data: Function specific data
 * Returns: TRUE on success, FALSE on failure
 *
 * Setter for "ConfigMethods" property. Sets the methods string, apply such
 * change and returns true on success. Returns false otherwise.
 */
dbus_bool_t wpas_dbus_setter_config_methods(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct wpa_supplicant *wpa_s = user_data;
	char *methods, *new_methods;

	if (!wpas_dbus_simple_property_setter(iter, error, DBUS_TYPE_STRING,
					      &methods))
		return FALSE;

	new_methods = os_strdup(methods);
	if (!new_methods)
		return FALSE;

	os_free(wpa_s->conf->config_methods);
	wpa_s->conf->config_methods = new_methods;

	wpa_s->conf->changed_parameters |= CFG_CHANGED_CONFIG_METHODS;
	wpa_supplicant_update_config(wpa_s);

	return TRUE;
}


/**
 * wpas_dbus_getter_wps_device_name - Get current WPS device name
 * @iter: Pointer to incoming dbus message iter
 * @error: Location to store error on failure
 * @user_data: Function specific data
 * Returns: TRUE on success, FALSE on failure
 *
 * Getter for "DeviceName" property.
 */
dbus_bool_t wpas_dbus_getter_wps_device_name(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct wpa_supplicant *wpa_s = user_data;

	return wpas_dbus_string_property_getter(iter, wpa_s->conf->device_name,
						error);
}


/**
 * wpas_dbus_setter_wps_device_name - Set current WPS device name
 * @iter: Pointer to incoming dbus message iter
 * @error: Location to store error on failure
 * @user_data: Function specific data
 * Returns: TRUE on success, FALSE on failure
 *
 * Setter for "DeviceName" property.
 */
dbus_bool_t wpas_dbus_setter_wps_device_name(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct wpa_supplicant *wpa_s = user_data;
	char *methods, *devname;

	if (!wpas_dbus_simple_property_setter(iter, error, DBUS_TYPE_STRING,
					      &methods))
		return FALSE;

	if (os_strlen(methods) > WPS_DEV_NAME_MAX_LEN)
		return FALSE;

	devname = os_strdup(methods);
	if (!devname)
		return FALSE;

	os_free(wpa_s->conf->device_name);
	wpa_s->conf->device_name = devname;
	wpa_s->conf->changed_parameters |= CFG_CHANGED_DEVICE_NAME;
	wpa_supplicant_update_config(wpa_s);

	return TRUE;
}


/**
 * wpas_dbus_getter_wps_manufacturer - Get current manufacturer name
 * @iter: Pointer to incoming dbus message iter
 * @error: Location to store error on failure
 * @user_data: Function specific data
 * Returns: TRUE on success, FALSE on failure
 *
 * Getter for "Manufacturer" property.
 */
dbus_bool_t wpas_dbus_getter_wps_manufacturer(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct wpa_supplicant *wpa_s = user_data;

	return wpas_dbus_string_property_getter(iter, wpa_s->conf->manufacturer,
						error);
}


/**
 * wpas_dbus_setter_wps_manufacturer - Set current manufacturer name
 * @iter: Pointer to incoming dbus message iter
 * @error: Location to store error on failure
 * @user_data: Function specific data
 * Returns: TRUE on success, FALSE on failure
 *
 * Setter for "Manufacturer" property.
 */
dbus_bool_t wpas_dbus_setter_wps_manufacturer(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct wpa_supplicant *wpa_s = user_data;
	char *methods, *manufacturer;

	if (!wpas_dbus_simple_property_setter(iter, error, DBUS_TYPE_STRING,
					      &methods))
		return FALSE;

	if (os_strlen(methods) > WPS_MANUFACTURER_MAX_LEN)
		return FALSE;

	manufacturer = os_strdup(methods);
	if (!manufacturer)
		return FALSE;

	os_free(wpa_s->conf->manufacturer);
	wpa_s->conf->manufacturer = manufacturer;
	wpa_s->conf->changed_parameters |= CFG_CHANGED_WPS_STRING;
	wpa_supplicant_update_config(wpa_s);

	return TRUE;
}


/**
 * wpas_dbus_getter_wps_device_model_name - Get current device model name
 * @iter: Pointer to incoming dbus message iter
 * @error: Location to store error on failure
 * @user_data: Function specific data
 * Returns: TRUE on success, FALSE on failure
 *
 * Getter for "ModelName" property.
 */
dbus_bool_t wpas_dbus_getter_wps_device_model_name(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct wpa_supplicant *wpa_s = user_data;

	return wpas_dbus_string_property_getter(iter, wpa_s->conf->model_name,
						error);
}


/**
 * wpas_dbus_setter_wps_device_model_name - Set current device model name
 * @iter: Pointer to incoming dbus message iter
 * @error: Location to store error on failure
 * @user_data: Function specific data
 * Returns: TRUE on success, FALSE on failure
 *
 * Setter for "ModelName" property.
 */
dbus_bool_t wpas_dbus_setter_wps_device_model_name(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct wpa_supplicant *wpa_s = user_data;
	char *methods, *model_name;

	if (!wpas_dbus_simple_property_setter(iter, error, DBUS_TYPE_STRING,
					      &methods))
		return FALSE;

	if (os_strlen(methods) > WPS_MODEL_NAME_MAX_LEN)
		return FALSE;

	model_name = os_strdup(methods);
	if (!model_name)
		return FALSE;
	os_free(wpa_s->conf->model_name);
	wpa_s->conf->model_name = model_name;
	wpa_s->conf->changed_parameters |= CFG_CHANGED_WPS_STRING;
	wpa_supplicant_update_config(wpa_s);

	return TRUE;
}


/**
 * wpas_dbus_getter_wps_device_model_number - Get current device model number
 * @iter: Pointer to incoming dbus message iter
 * @error: Location to store error on failure
 * @user_data: Function specific data
 * Returns: TRUE on success, FALSE on failure
 *
 * Getter for "ModelNumber" property.
 */
dbus_bool_t wpas_dbus_getter_wps_device_model_number(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct wpa_supplicant *wpa_s = user_data;

	return wpas_dbus_string_property_getter(iter, wpa_s->conf->model_number,
						error);
}


/**
 * wpas_dbus_setter_wps_device_model_number - Set current device model number
 * @iter: Pointer to incoming dbus message iter
 * @error: Location to store error on failure
 * @user_data: Function specific data
 * Returns: TRUE on success, FALSE on failure
 *
 * Setter for "ModelNumber" property.
 */
dbus_bool_t wpas_dbus_setter_wps_device_model_number(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct wpa_supplicant *wpa_s = user_data;
	char *methods, *model_number;

	if (!wpas_dbus_simple_property_setter(iter, error, DBUS_TYPE_STRING,
					      &methods))
		return FALSE;

	if (os_strlen(methods) > WPS_MODEL_NUMBER_MAX_LEN)
		return FALSE;

	model_number = os_strdup(methods);
	if (!model_number)
		return FALSE;

	os_free(wpa_s->conf->model_number);
	wpa_s->conf->model_number = model_number;
	wpa_s->conf->changed_parameters |= CFG_CHANGED_WPS_STRING;
	wpa_supplicant_update_config(wpa_s);

	return TRUE;
}


/**
 * wpas_dbus_getter_wps_device_serial_number - Get current device serial number
 * @iter: Pointer to incoming dbus message iter
 * @error: Location to store error on failure
 * @user_data: Function specific data
 * Returns: TRUE on success, FALSE on failure
 *
 * Getter for "SerialNumber" property.
 */
dbus_bool_t wpas_dbus_getter_wps_device_serial_number(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct wpa_supplicant *wpa_s = user_data;

	return wpas_dbus_string_property_getter(iter,
						wpa_s->conf->serial_number,
						error);
}


/**
 * wpas_dbus_setter_wps_device_serial_number - Set current device serial number
 * @iter: Pointer to incoming dbus message iter
 * @error: Location to store error on failure
 * @user_data: Function specific data
 * Returns: TRUE on success, FALSE on failure
 *
 * Setter for "SerialNumber" property.
 */
dbus_bool_t wpas_dbus_setter_wps_device_serial_number(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct wpa_supplicant *wpa_s = user_data;
	char *methods, *serial_number;

	if (!wpas_dbus_simple_property_setter(iter, error, DBUS_TYPE_STRING,
					      &methods))
		return FALSE;

	if (os_strlen(methods) > WPS_SERIAL_NUMBER_MAX_LEN)
		return FALSE;

	serial_number = os_strdup(methods);
	if (!serial_number)
		return FALSE;
	os_free(wpa_s->conf->serial_number);
	wpa_s->conf->serial_number = serial_number;
	wpa_s->conf->changed_parameters |= CFG_CHANGED_WPS_STRING;
	wpa_supplicant_update_config(wpa_s);

	return TRUE;
}


/**
 * wpas_dbus_getter_wps_device_device_type - Get current device type
 * @iter: Pointer to incoming dbus message iter
 * @error: Location to store error on failure
 * @user_data: Function specific data
 * Returns: TRUE on success, FALSE on failure
 *
 * Getter for "DeviceType" property.
 */
dbus_bool_t wpas_dbus_getter_wps_device_device_type(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct wpa_supplicant *wpa_s = user_data;

	if (!wpas_dbus_simple_array_property_getter(iter, DBUS_TYPE_BYTE,
						    (char *)
						    wpa_s->conf->device_type,
						    WPS_DEV_TYPE_LEN, error)) {
		dbus_set_error(error, DBUS_ERROR_FAILED,
			       "%s: error constructing reply", __func__);
		return FALSE;
	}

	return TRUE;
}


/**
 * wpas_dbus_setter_wps_device_device_type - Set current device type
 * @iter: Pointer to incoming dbus message iter
 * @error: Location to store error on failure
 * @user_data: Function specific data
 * Returns: TRUE on success, FALSE on failure
 *
 * Setter for "DeviceType" property.
 */
dbus_bool_t wpas_dbus_setter_wps_device_device_type(
	const struct wpa_dbus_property_desc *property_desc,
	DBusMessageIter *iter, DBusError *error, void *user_data)
{
	struct wpa_supplicant *wpa_s = user_data;
	u8 *dev_type;
	int dev_len;
	DBusMessageIter variant, array_iter;

	if (dbus_message_iter_get_arg_type(iter) != DBUS_TYPE_VARIANT)
		return FALSE;

	dbus_message_iter_recurse(iter, &variant);
	if (dbus_message_iter_get_arg_type(&variant) != DBUS_TYPE_ARRAY)
		return FALSE;

	dbus_message_iter_recurse(&variant, &array_iter);
	dbus_message_iter_get_fixed_array(&array_iter, &dev_type, &dev_len);

	if (dev_len != WPS_DEV_TYPE_LEN)
		return FALSE;

	os_memcpy(wpa_s->conf->device_type, dev_type, WPS_DEV_TYPE_LEN);
	wpa_s->conf->changed_parameters |= CFG_CHANGED_DEVICE_TYPE;
	wpa_supplicant_update_config(wpa_s);

	return TRUE;
}
