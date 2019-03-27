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
#include "eloop.h"
#include "wps/wps.h"
#include "../config.h"
#include "../wpa_supplicant_i.h"
#include "../bss.h"
#include "dbus_old.h"
#include "dbus_old_handlers.h"
#include "dbus_common_i.h"


/**
 * wpas_dbus_decompose_object_path - Decompose an interface object path into parts
 * @path: The dbus object path
 * @network: (out) the configured network this object path refers to, if any
 * @bssid: (out) the scanned bssid this object path refers to, if any
 * Returns: The object path of the network interface this path refers to
 *
 * For a given object path, decomposes the object path into object id, network,
 * and BSSID parts, if those parts exist.
 */
char * wpas_dbus_decompose_object_path(const char *path, char **network,
				       char **bssid)
{
	const unsigned int dev_path_prefix_len =
		strlen(WPAS_DBUS_PATH_INTERFACES "/");
	char *obj_path_only;
	char *next_sep;

	/* Be a bit paranoid about path */
	if (!path || strncmp(path, WPAS_DBUS_PATH_INTERFACES "/",
			     dev_path_prefix_len))
		return NULL;

	/* Ensure there's something at the end of the path */
	if ((path + dev_path_prefix_len)[0] == '\0')
		return NULL;

	obj_path_only = os_strdup(path);
	if (obj_path_only == NULL)
		return NULL;

	next_sep = strchr(obj_path_only + dev_path_prefix_len, '/');
	if (next_sep != NULL) {
		const char *net_part = strstr(next_sep,
					      WPAS_DBUS_NETWORKS_PART "/");
		const char *bssid_part = strstr(next_sep,
						WPAS_DBUS_BSSIDS_PART "/");

		if (network && net_part) {
			/* Deal with a request for a configured network */
			const char *net_name = net_part +
				strlen(WPAS_DBUS_NETWORKS_PART "/");
			*network = NULL;
			if (strlen(net_name))
				*network = os_strdup(net_name);
		} else if (bssid && bssid_part) {
			/* Deal with a request for a scanned BSSID */
			const char *bssid_name = bssid_part +
				strlen(WPAS_DBUS_BSSIDS_PART "/");
			if (strlen(bssid_name))
				*bssid = os_strdup(bssid_name);
			else
				*bssid = NULL;
		}

		/* Cut off interface object path before "/" */
		*next_sep = '\0';
	}

	return obj_path_only;
}


/**
 * wpas_dbus_new_invalid_iface_error - Return a new invalid interface error message
 * @message: Pointer to incoming dbus message this error refers to
 * Returns: A dbus error message
 *
 * Convenience function to create and return an invalid interface error
 */
DBusMessage * wpas_dbus_new_invalid_iface_error(DBusMessage *message)
{
	return dbus_message_new_error(
		message, WPAS_ERROR_INVALID_IFACE,
		"wpa_supplicant knows nothing about this interface.");
}


/**
 * wpas_dbus_new_invalid_network_error - Return a new invalid network error message
 * @message: Pointer to incoming dbus message this error refers to
 * Returns: a dbus error message
 *
 * Convenience function to create and return an invalid network error
 */
DBusMessage * wpas_dbus_new_invalid_network_error(DBusMessage *message)
{
	return dbus_message_new_error(message, WPAS_ERROR_INVALID_NETWORK,
				      "The requested network does not exist.");
}


/**
 * wpas_dbus_new_invalid_bssid_error - Return a new invalid bssid error message
 * @message: Pointer to incoming dbus message this error refers to
 * Returns: a dbus error message
 *
 * Convenience function to create and return an invalid bssid error
 */
static DBusMessage * wpas_dbus_new_invalid_bssid_error(DBusMessage *message)
{
	return dbus_message_new_error(message, WPAS_ERROR_INVALID_BSSID,
				      "The BSSID requested was invalid.");
}


/**
 * wpas_dispatch_network_method - dispatch messages for configured networks
 * @message: the incoming dbus message
 * @wpa_s: a network interface's data
 * @network_id: id of the configured network we're interested in
 * Returns: a reply dbus message, or a dbus error message
 *
 * This function dispatches all incoming dbus messages for configured networks.
 */
static DBusMessage * wpas_dispatch_network_method(DBusMessage *message,
						  struct wpa_supplicant *wpa_s,
						  int network_id)
{
	DBusMessage *reply = NULL;
	const char *method = dbus_message_get_member(message);
	struct wpa_ssid *ssid;

	ssid = wpa_config_get_network(wpa_s->conf, network_id);
	if (ssid == NULL)
		return wpas_dbus_new_invalid_network_error(message);

	if (!strcmp(method, "set"))
		reply = wpas_dbus_iface_set_network(message, wpa_s, ssid);
	else if (!strcmp(method, "enable"))
		reply = wpas_dbus_iface_enable_network(message, wpa_s, ssid);
	else if (!strcmp(method, "disable"))
		reply = wpas_dbus_iface_disable_network(message, wpa_s, ssid);

	return reply;
}


/**
 * wpas_dispatch_bssid_method - dispatch messages for scanned networks
 * @message: the incoming dbus message
 * @wpa_s: a network interface's data
 * @bssid: bssid of the scanned network we're interested in
 * Returns: a reply dbus message, or a dbus error message
 *
 * This function dispatches all incoming dbus messages for scanned networks.
 */
static DBusMessage * wpas_dispatch_bssid_method(DBusMessage *message,
						struct wpa_supplicant *wpa_s,
						const char *bssid_txt)
{
	u8 bssid[ETH_ALEN];
	struct wpa_bss *bss;

	if (hexstr2bin(bssid_txt, bssid, ETH_ALEN) < 0)
		return wpas_dbus_new_invalid_bssid_error(message);

	bss = wpa_bss_get_bssid(wpa_s, bssid);
	if (bss == NULL)
		return wpas_dbus_new_invalid_bssid_error(message);

	/* Dispatch the method call against the scanned bssid */
	if (os_strcmp(dbus_message_get_member(message), "properties") == 0)
		return wpas_dbus_bssid_properties(message, wpa_s, bss);

	return NULL;
}


/**
 * wpas_iface_message_handler - Dispatch messages for interfaces or networks
 * @connection: Connection to the system message bus
 * @message: An incoming dbus message
 * @user_data: A pointer to a dbus control interface data structure
 * Returns: Whether or not the message was handled
 *
 * This function dispatches all incoming dbus messages for network interfaces,
 * or objects owned by them, such as scanned BSSIDs and configured networks.
 */
static DBusHandlerResult wpas_iface_message_handler(DBusConnection *connection,
						    DBusMessage *message,
						    void *user_data)
{
	struct wpa_supplicant *wpa_s = user_data;
	const char *method = dbus_message_get_member(message);
	const char *path = dbus_message_get_path(message);
	const char *msg_interface = dbus_message_get_interface(message);
	char *iface_obj_path = NULL;
	char *network = NULL;
	char *bssid = NULL;
	DBusMessage *reply = NULL;

	/* Caller must specify a message interface */
	if (!msg_interface)
		goto out;

	wpa_printf(MSG_MSGDUMP, "dbus[old/iface]: %s.%s (%s) [%s]",
		   msg_interface, method, path,
		   dbus_message_get_signature(message));

	iface_obj_path = wpas_dbus_decompose_object_path(path, &network,
							 &bssid);
	if (iface_obj_path == NULL) {
		reply = wpas_dbus_new_invalid_iface_error(message);
		goto out;
	}

	/* Make sure the message's object path actually refers to the
	 * wpa_supplicant structure it's supposed to (which is wpa_s)
	 */
	if (wpa_supplicant_get_iface_by_dbus_path(wpa_s->global,
						  iface_obj_path) != wpa_s) {
		reply = wpas_dbus_new_invalid_iface_error(message);
		goto out;
	}

	if (network && !strcmp(msg_interface, WPAS_DBUS_IFACE_NETWORK)) {
		/* A method for one of this interface's configured networks */
		int nid = strtoul(network, NULL, 10);

		if (errno != EINVAL)
			reply = wpas_dispatch_network_method(message, wpa_s,
							     nid);
		else
			reply = wpas_dbus_new_invalid_network_error(message);
	} else if (bssid && !strcmp(msg_interface, WPAS_DBUS_IFACE_BSSID)) {
		/* A method for one of this interface's scanned BSSIDs */
		reply = wpas_dispatch_bssid_method(message, wpa_s, bssid);
	} else if (!strcmp(msg_interface, WPAS_DBUS_IFACE_INTERFACE)) {
		/* A method for an interface only. */
		if (!strcmp(method, "scan"))
			reply = wpas_dbus_iface_scan(message, wpa_s);
		else if (!strcmp(method, "scanResults"))
			reply = wpas_dbus_iface_scan_results(message, wpa_s);
		else if (!strcmp(method, "addNetwork"))
			reply = wpas_dbus_iface_add_network(message, wpa_s);
		else if (!strcmp(method, "removeNetwork"))
			reply = wpas_dbus_iface_remove_network(message, wpa_s);
		else if (!strcmp(method, "selectNetwork"))
			reply = wpas_dbus_iface_select_network(message, wpa_s);
		else if (!strcmp(method, "capabilities"))
			reply = wpas_dbus_iface_capabilities(message, wpa_s);
		else if (!strcmp(method, "disconnect"))
			reply = wpas_dbus_iface_disconnect(message, wpa_s);
		else if (!strcmp(method, "setAPScan"))
			reply = wpas_dbus_iface_set_ap_scan(message, wpa_s);
		else if (!strcmp(method, "setSmartcardModules"))
			reply = wpas_dbus_iface_set_smartcard_modules(message,
								      wpa_s);
		else if (!strcmp(method, "state"))
			reply = wpas_dbus_iface_get_state(message, wpa_s);
		else if (!strcmp(method, "scanning"))
			reply = wpas_dbus_iface_get_scanning(message, wpa_s);
#ifndef CONFIG_NO_CONFIG_BLOBS
		else if (!strcmp(method, "setBlobs"))
			reply = wpas_dbus_iface_set_blobs(message, wpa_s);
		else if (!strcmp(method, "removeBlobs"))
			reply = wpas_dbus_iface_remove_blobs(message, wpa_s);
#endif /* CONFIG_NO_CONFIG_BLOBS */
#ifdef CONFIG_WPS
		else if (os_strcmp(method, "wpsPbc") == 0)
			reply = wpas_dbus_iface_wps_pbc(message, wpa_s);
		else if (os_strcmp(method, "wpsPin") == 0)
			reply = wpas_dbus_iface_wps_pin(message, wpa_s);
		else if (os_strcmp(method, "wpsReg") == 0)
			reply = wpas_dbus_iface_wps_reg(message, wpa_s);
#endif /* CONFIG_WPS */
		else if (os_strcmp(method, "flush") == 0)
			reply = wpas_dbus_iface_flush(message, wpa_s);
	}

	/* If the message was handled, send back the reply */
out:
	if (reply) {
		if (!dbus_message_get_no_reply(message))
			dbus_connection_send(connection, reply, NULL);
		dbus_message_unref(reply);
	}

	os_free(iface_obj_path);
	os_free(network);
	os_free(bssid);
	return reply ? DBUS_HANDLER_RESULT_HANDLED :
		DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}


/**
 * wpas_message_handler - dispatch incoming dbus messages
 * @connection: connection to the system message bus
 * @message: an incoming dbus message
 * @user_data: a pointer to a dbus control interface data structure
 * Returns: whether or not the message was handled
 *
 * This function dispatches all incoming dbus messages to the correct
 * handlers, depending on what the message's target object path is,
 * and what the method call is.
 */
static DBusHandlerResult wpas_message_handler(DBusConnection *connection,
	DBusMessage *message, void *user_data)
{
	struct wpas_dbus_priv *ctrl_iface = user_data;
	const char *method;
	const char *path;
	const char *msg_interface;
	DBusMessage *reply = NULL;

	method = dbus_message_get_member(message);
	path = dbus_message_get_path(message);
	msg_interface = dbus_message_get_interface(message);
	if (!method || !path || !ctrl_iface || !msg_interface)
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

	wpa_printf(MSG_MSGDUMP, "dbus[old]: %s.%s (%s) [%s]",
		   msg_interface, method, path,
		   dbus_message_get_signature(message));

	/* Validate the method interface */
	if (strcmp(msg_interface, WPAS_DBUS_INTERFACE) != 0)
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

	if (!strcmp(path, WPAS_DBUS_PATH)) {
		/* dispatch methods against our global dbus interface here */
		if (!strcmp(method, "addInterface")) {
			reply = wpas_dbus_global_add_interface(
				message, ctrl_iface->global);
		} else if (!strcmp(method, "removeInterface")) {
			reply = wpas_dbus_global_remove_interface(
				message, ctrl_iface->global);
		} else if (!strcmp(method, "getInterface")) {
			reply = wpas_dbus_global_get_interface(
				message, ctrl_iface->global);
		} else if (!strcmp(method, "setDebugParams")) {
			reply = wpas_dbus_global_set_debugparams(
				message, ctrl_iface->global);
		}
	}

	/* If the message was handled, send back the reply */
	if (reply) {
		if (!dbus_message_get_no_reply(message))
			dbus_connection_send(connection, reply, NULL);
		dbus_message_unref(reply);
	}

	return reply ? DBUS_HANDLER_RESULT_HANDLED :
		DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}


/**
 * wpa_supplicant_dbus_notify_scan_results - Send a scan results signal
 * @wpa_s: %wpa_supplicant network interface data
 * Returns: 0 on success, -1 on failure
 *
 * Notify listeners that this interface has updated scan results.
 */
void wpa_supplicant_dbus_notify_scan_results(struct wpa_supplicant *wpa_s)
{
	struct wpas_dbus_priv *iface = wpa_s->global->dbus;
	DBusMessage *_signal;

	/* Do nothing if the control interface is not turned on */
	if (iface == NULL || !wpa_s->dbus_path)
		return;

	_signal = dbus_message_new_signal(wpa_s->dbus_path,
					  WPAS_DBUS_IFACE_INTERFACE,
					  "ScanResultsAvailable");
	if (_signal == NULL) {
		wpa_printf(MSG_ERROR,
			   "dbus: Not enough memory to send scan results signal");
		return;
	}
	dbus_connection_send(iface->con, _signal, NULL);
	dbus_message_unref(_signal);
}


/**
 * wpa_supplicant_dbus_notify_state_change - Send a state change signal
 * @wpa_s: %wpa_supplicant network interface data
 * @new_state: new state wpa_supplicant is entering
 * @old_state: old state wpa_supplicant is leaving
 * Returns: 0 on success, -1 on failure
 *
 * Notify listeners that wpa_supplicant has changed state
 */
void wpa_supplicant_dbus_notify_state_change(struct wpa_supplicant *wpa_s,
					     enum wpa_states new_state,
					     enum wpa_states old_state)
{
	struct wpas_dbus_priv *iface;
	DBusMessage *_signal = NULL;
	const char *new_state_str, *old_state_str;

	if (wpa_s->dbus_path == NULL)
		return; /* Skip signal since D-Bus setup is not yet ready */

	/* Do nothing if the control interface is not turned on */
	if (wpa_s->global == NULL)
		return;
	iface = wpa_s->global->dbus;
	if (iface == NULL)
		return;

	/* Only send signal if state really changed */
	if (new_state == old_state)
		return;

	_signal = dbus_message_new_signal(wpa_s->dbus_path,
					  WPAS_DBUS_IFACE_INTERFACE,
					  "StateChange");
	if (_signal == NULL) {
		wpa_printf(MSG_ERROR,
			   "dbus: %s: could not create dbus signal; likely out of memory",
			   __func__);
		return;
	}

	new_state_str = wpa_supplicant_state_txt(new_state);
	old_state_str = wpa_supplicant_state_txt(old_state);

	if (!dbus_message_append_args(_signal,
				      DBUS_TYPE_STRING, &new_state_str,
				      DBUS_TYPE_STRING, &old_state_str,
				      DBUS_TYPE_INVALID)) {
		wpa_printf(MSG_ERROR,
			   "dbus: %s: Not enough memory to construct state change signal",
			   __func__);
		goto out;
	}

	dbus_connection_send(iface->con, _signal, NULL);

out:
	dbus_message_unref(_signal);
}


/**
 * wpa_supplicant_dbus_notify_scanning - send scanning status
 * @wpa_s: %wpa_supplicant network interface data
 * Returns: 0 on success, -1 on failure
 *
 * Notify listeners of interface scanning state changes
 */
void wpa_supplicant_dbus_notify_scanning(struct wpa_supplicant *wpa_s)
{
	struct wpas_dbus_priv *iface = wpa_s->global->dbus;
	DBusMessage *_signal;
	dbus_bool_t scanning = wpa_s->scanning ? TRUE : FALSE;

	/* Do nothing if the control interface is not turned on */
	if (iface == NULL || !wpa_s->dbus_path)
		return;

	_signal = dbus_message_new_signal(wpa_s->dbus_path,
					  WPAS_DBUS_IFACE_INTERFACE,
					  "Scanning");
	if (_signal == NULL) {
		wpa_printf(MSG_ERROR,
			   "dbus: Not enough memory to send scan results signal");
		return;
	}

	if (dbus_message_append_args(_signal,
				     DBUS_TYPE_BOOLEAN, &scanning,
				     DBUS_TYPE_INVALID)) {
		dbus_connection_send(iface->con, _signal, NULL);
	} else {
		wpa_printf(MSG_ERROR,
			   "dbus: Not enough memory to construct signal");
	}
	dbus_message_unref(_signal);
}


#ifdef CONFIG_WPS
void wpa_supplicant_dbus_notify_wps_cred(struct wpa_supplicant *wpa_s,
					 const struct wps_credential *cred)
{
	struct wpas_dbus_priv *iface;
	DBusMessage *_signal = NULL;

	/* Do nothing if the control interface is not turned on */
	if (wpa_s->global == NULL)
		return;
	iface = wpa_s->global->dbus;
	if (iface == NULL || !wpa_s->dbus_path)
		return;

	_signal = dbus_message_new_signal(wpa_s->dbus_path,
					  WPAS_DBUS_IFACE_INTERFACE,
					  "WpsCred");
	if (_signal == NULL) {
		wpa_printf(MSG_ERROR,
			   "dbus: %s: Could not create dbus signal; likely out of memory",
			   __func__);
		return;
	}

	if (!dbus_message_append_args(_signal,
				      DBUS_TYPE_ARRAY, DBUS_TYPE_BYTE,
				      &cred->cred_attr, cred->cred_attr_len,
				      DBUS_TYPE_INVALID)) {
		wpa_printf(MSG_ERROR,
			   "dbus: %s: Not enough memory to construct signal",
			   __func__);
		goto out;
	}

	dbus_connection_send(iface->con, _signal, NULL);

out:
	dbus_message_unref(_signal);
}
#else /* CONFIG_WPS */
void wpa_supplicant_dbus_notify_wps_cred(struct wpa_supplicant *wpa_s,
					 const struct wps_credential *cred)
{
}
#endif /* CONFIG_WPS */

void wpa_supplicant_dbus_notify_certification(struct wpa_supplicant *wpa_s,
					      int depth, const char *subject,
					      const char *cert_hash,
					      const struct wpabuf *cert)
{
	struct wpas_dbus_priv *iface;
	DBusMessage *_signal = NULL;
	const char *hash;
	const char *cert_hex;
	int cert_hex_len;

	/* Do nothing if the control interface is not turned on */
	if (wpa_s->global == NULL)
		return;
	iface = wpa_s->global->dbus;
	if (iface == NULL || !wpa_s->dbus_path)
		return;

	_signal = dbus_message_new_signal(wpa_s->dbus_path,
					  WPAS_DBUS_IFACE_INTERFACE,
					  "Certification");
	if (_signal == NULL) {
		wpa_printf(MSG_ERROR,
			   "dbus: %s: Could not create dbus signal; likely out of memory",
			   __func__);
		return;
	}

	hash = cert_hash ? cert_hash : "";
	cert_hex = cert ? wpabuf_head(cert) : "";
	cert_hex_len = cert ? wpabuf_len(cert) : 0;

	if (!dbus_message_append_args(_signal,
				      DBUS_TYPE_INT32, &depth,
				      DBUS_TYPE_STRING, &subject,
				      DBUS_TYPE_STRING, &hash,
				      DBUS_TYPE_ARRAY, DBUS_TYPE_BYTE,
				      &cert_hex, cert_hex_len,
				      DBUS_TYPE_INVALID)) {
		wpa_printf(MSG_ERROR,
			   "dbus: %s: Not enough memory to construct signal",
			   __func__);
		goto out;
	}

	dbus_connection_send(iface->con, _signal, NULL);

out:
	dbus_message_unref(_signal);

}


/**
 * wpa_supplicant_dbus_ctrl_iface_init - Initialize dbus control interface
 * @global: Pointer to global data from wpa_supplicant_init()
 * Returns: 0 on success, -1 on failure
 *
 * Initialize the dbus control interface and start receiving commands from
 * external programs over the bus.
 */
int wpa_supplicant_dbus_ctrl_iface_init(struct wpas_dbus_priv *iface)
{
	DBusError error;
	int ret = -1;
	DBusObjectPathVTable wpas_vtable = {
		NULL, &wpas_message_handler, NULL, NULL, NULL, NULL
	};

	/* Register the message handler for the global dbus interface */
	if (!dbus_connection_register_object_path(iface->con,
						  WPAS_DBUS_PATH, &wpas_vtable,
						  iface)) {
		wpa_printf(MSG_ERROR, "dbus: Could not set up message handler");
		return -1;
	}

	/* Register our service with the message bus */
	dbus_error_init(&error);
	switch (dbus_bus_request_name(iface->con, WPAS_DBUS_SERVICE,
				      0, &error)) {
	case DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER:
		ret = 0;
		break;
	case DBUS_REQUEST_NAME_REPLY_EXISTS:
	case DBUS_REQUEST_NAME_REPLY_IN_QUEUE:
	case DBUS_REQUEST_NAME_REPLY_ALREADY_OWNER:
		wpa_printf(MSG_ERROR,
			   "dbus: Could not request service name: already registered");
		break;
	default:
		wpa_printf(MSG_ERROR,
			   "dbus: Could not request service name: %s %s",
			   error.name, error.message);
		break;
	}
	dbus_error_free(&error);

	if (ret != 0)
		return -1;

	wpa_printf(MSG_DEBUG, "Providing DBus service '" WPAS_DBUS_SERVICE
		   "'.");

	return 0;
}


/**
 * wpas_dbus_register_new_iface - Register a new interface with dbus
 * @wpa_s: %wpa_supplicant interface description structure to register
 * Returns: 0 on success, -1 on error
 *
 * Registers a new interface with dbus and assigns it a dbus object path.
 */
int wpas_dbus_register_iface(struct wpa_supplicant *wpa_s)
{
	struct wpas_dbus_priv *ctrl_iface = wpa_s->global->dbus;
	DBusConnection * con;
	u32 next;
	DBusObjectPathVTable vtable = {
		NULL, &wpas_iface_message_handler, NULL, NULL, NULL, NULL
	};

	/* Do nothing if the control interface is not turned on */
	if (ctrl_iface == NULL)
		return 0;

	con = ctrl_iface->con;
	next = ctrl_iface->next_objid++;

	/* Create and set the interface's object path */
	wpa_s->dbus_path = os_zalloc(WPAS_DBUS_OBJECT_PATH_MAX);
	if (wpa_s->dbus_path == NULL)
		return -1;
	os_snprintf(wpa_s->dbus_path, WPAS_DBUS_OBJECT_PATH_MAX,
		    WPAS_DBUS_PATH_INTERFACES "/%u",
		    next);

	/* Register the message handler for the interface functions */
	if (!dbus_connection_register_fallback(con, wpa_s->dbus_path, &vtable,
					       wpa_s)) {
		wpa_printf(MSG_ERROR,
			   "dbus: Could not set up message handler for interface %s",
			   wpa_s->ifname);
		return -1;
	}

	return 0;
}


/**
 * wpas_dbus_unregister_iface - Unregister an interface from dbus
 * @wpa_s: wpa_supplicant interface structure
 * Returns: 0 on success, -1 on failure
 *
 * Unregisters the interface with dbus
 */
int wpas_dbus_unregister_iface(struct wpa_supplicant *wpa_s)
{
	struct wpas_dbus_priv *ctrl_iface;
	DBusConnection *con;

	/* Do nothing if the control interface is not turned on */
	if (wpa_s == NULL || wpa_s->global == NULL)
		return 0;
	ctrl_iface = wpa_s->global->dbus;
	if (ctrl_iface == NULL || wpa_s->dbus_path == NULL)
		return 0;

	con = ctrl_iface->con;
	if (!dbus_connection_unregister_object_path(con, wpa_s->dbus_path))
		return -1;

	os_free(wpa_s->dbus_path);
	wpa_s->dbus_path = NULL;

	return 0;
}


/**
 * wpa_supplicant_get_iface_by_dbus_path - Get a new network interface
 * @global: Pointer to global data from wpa_supplicant_init()
 * @path: Pointer to a dbus object path representing an interface
 * Returns: Pointer to the interface or %NULL if not found
 */
struct wpa_supplicant * wpa_supplicant_get_iface_by_dbus_path(
	struct wpa_global *global, const char *path)
{
	struct wpa_supplicant *wpa_s;

	for (wpa_s = global->ifaces; wpa_s; wpa_s = wpa_s->next) {
		if (wpa_s->dbus_path && strcmp(wpa_s->dbus_path, path) == 0)
			return wpa_s;
	}
	return NULL;
}
