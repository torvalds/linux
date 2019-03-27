/*
 * WPA Supplicant / dbus-based control interface
 * Copyright (c) 2006, Dan Williams <dcbw@redhat.com> and Red Hat, Inc.
 * Copyright (c) 2009-2010, Witold Sowa <witold.sowa@gmail.com>
 * Copyright (c) 2009, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "includes.h"

#include "common.h"
#include "common/ieee802_11_defs.h"
#include "wps/wps.h"
#include "../config.h"
#include "../wpa_supplicant_i.h"
#include "../bss.h"
#include "../wpas_glue.h"
#include "dbus_new_helpers.h"
#include "dbus_dict_helpers.h"
#include "dbus_new.h"
#include "dbus_new_handlers.h"
#include "dbus_common_i.h"
#include "dbus_new_handlers_p2p.h"
#include "p2p/p2p.h"
#include "../p2p_supplicant.h"

#ifdef CONFIG_AP /* until needed by something else */

/*
 * NameOwnerChanged handling
 *
 * Some services we provide allow an application to register for
 * a signal that it needs. While it can also unregister, we must
 * be prepared for the case where the application simply crashes
 * and thus doesn't clean up properly. The way to handle this in
 * DBus is to register for the NameOwnerChanged signal which will
 * signal an owner change to NULL if the peer closes the socket
 * for whatever reason.
 *
 * Handle this signal via a filter function whenever necessary.
 * The code below also handles refcounting in case in the future
 * there will be multiple instances of this subscription scheme.
 */
static const char wpas_dbus_noc_filter_str[] =
	"interface=org.freedesktop.DBus,member=NameOwnerChanged";


static DBusHandlerResult noc_filter(DBusConnection *conn,
				    DBusMessage *message, void *data)
{
	struct wpas_dbus_priv *priv = data;

	if (dbus_message_get_type(message) != DBUS_MESSAGE_TYPE_SIGNAL)
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

	if (dbus_message_is_signal(message, DBUS_INTERFACE_DBUS,
				   "NameOwnerChanged")) {
		const char *name;
		const char *prev_owner;
		const char *new_owner;
		DBusError derr;
		struct wpa_supplicant *wpa_s;

		dbus_error_init(&derr);

		if (!dbus_message_get_args(message, &derr,
					   DBUS_TYPE_STRING, &name,
					   DBUS_TYPE_STRING, &prev_owner,
					   DBUS_TYPE_STRING, &new_owner,
					   DBUS_TYPE_INVALID)) {
			/* Ignore this error */
			dbus_error_free(&derr);
			return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
		}

		for (wpa_s = priv->global->ifaces; wpa_s; wpa_s = wpa_s->next) {
			if (wpa_s->preq_notify_peer != NULL &&
			    os_strcmp(name, wpa_s->preq_notify_peer) == 0 &&
			    (new_owner == NULL || os_strlen(new_owner) == 0)) {
				/* probe request owner disconnected */
				os_free(wpa_s->preq_notify_peer);
				wpa_s->preq_notify_peer = NULL;
				wpas_dbus_unsubscribe_noc(priv);
			}
		}
	}

	return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}


void wpas_dbus_subscribe_noc(struct wpas_dbus_priv *priv)
{
	priv->dbus_noc_refcnt++;
	if (priv->dbus_noc_refcnt > 1)
		return;

	if (!dbus_connection_add_filter(priv->con, noc_filter, priv, NULL)) {
		wpa_printf(MSG_ERROR, "dbus: failed to add filter");
		return;
	}

	dbus_bus_add_match(priv->con, wpas_dbus_noc_filter_str, NULL);
}


void wpas_dbus_unsubscribe_noc(struct wpas_dbus_priv *priv)
{
	priv->dbus_noc_refcnt--;
	if (priv->dbus_noc_refcnt > 0)
		return;

	dbus_bus_remove_match(priv->con, wpas_dbus_noc_filter_str, NULL);
	dbus_connection_remove_filter(priv->con, noc_filter, priv);
}

#endif /* CONFIG_AP */


/**
 * wpas_dbus_signal_interface - Send a interface related event signal
 * @wpa_s: %wpa_supplicant network interface data
 * @sig_name: signal name - InterfaceAdded or InterfaceRemoved
 * @properties: Whether to add second argument with object properties
 *
 * Notify listeners about event related with interface
 */
static void wpas_dbus_signal_interface(struct wpa_supplicant *wpa_s,
				       const char *sig_name, int properties)
{
	struct wpas_dbus_priv *iface;
	DBusMessage *msg;
	DBusMessageIter iter;

	iface = wpa_s->global->dbus;

	/* Do nothing if the control interface is not turned on */
	if (iface == NULL || !wpa_s->dbus_new_path)
		return;

	msg = dbus_message_new_signal(WPAS_DBUS_NEW_PATH,
				      WPAS_DBUS_NEW_INTERFACE, sig_name);
	if (msg == NULL)
		return;

	dbus_message_iter_init_append(msg, &iter);
	if (!dbus_message_iter_append_basic(&iter, DBUS_TYPE_OBJECT_PATH,
					    &wpa_s->dbus_new_path) ||
	    (properties &&
	     !wpa_dbus_get_object_properties(
		     iface, wpa_s->dbus_new_path,
		     WPAS_DBUS_NEW_IFACE_INTERFACE, &iter)))
		wpa_printf(MSG_ERROR, "dbus: Failed to construct signal");
	else
		dbus_connection_send(iface->con, msg, NULL);
	dbus_message_unref(msg);
}


/**
 * wpas_dbus_signal_interface_added - Send a interface created signal
 * @wpa_s: %wpa_supplicant network interface data
 *
 * Notify listeners about creating new interface
 */
static void wpas_dbus_signal_interface_added(struct wpa_supplicant *wpa_s)
{
	wpas_dbus_signal_interface(wpa_s, "InterfaceAdded", TRUE);
}


/**
 * wpas_dbus_signal_interface_removed - Send a interface removed signal
 * @wpa_s: %wpa_supplicant network interface data
 *
 * Notify listeners about removing interface
 */
static void wpas_dbus_signal_interface_removed(struct wpa_supplicant *wpa_s)
{
	wpas_dbus_signal_interface(wpa_s, "InterfaceRemoved", FALSE);

}


/**
 * wpas_dbus_signal_scan_done - send scan done signal
 * @wpa_s: %wpa_supplicant network interface data
 * @success: indicates if scanning succeed or failed
 *
 * Notify listeners about finishing a scan
 */
void wpas_dbus_signal_scan_done(struct wpa_supplicant *wpa_s, int success)
{
	struct wpas_dbus_priv *iface;
	DBusMessage *msg;
	dbus_bool_t succ;

	iface = wpa_s->global->dbus;

	/* Do nothing if the control interface is not turned on */
	if (iface == NULL || !wpa_s->dbus_new_path)
		return;

	msg = dbus_message_new_signal(wpa_s->dbus_new_path,
				      WPAS_DBUS_NEW_IFACE_INTERFACE,
				      "ScanDone");
	if (msg == NULL)
		return;

	succ = success ? TRUE : FALSE;
	if (dbus_message_append_args(msg, DBUS_TYPE_BOOLEAN, &succ,
				     DBUS_TYPE_INVALID))
		dbus_connection_send(iface->con, msg, NULL);
	else
		wpa_printf(MSG_ERROR, "dbus: Failed to construct signal");
	dbus_message_unref(msg);
}


/**
 * wpas_dbus_signal_bss - Send a BSS related event signal
 * @wpa_s: %wpa_supplicant network interface data
 * @bss_obj_path: BSS object path
 * @sig_name: signal name - BSSAdded or BSSRemoved
 * @properties: Whether to add second argument with object properties
 *
 * Notify listeners about event related with BSS
 */
static void wpas_dbus_signal_bss(struct wpa_supplicant *wpa_s,
				 const char *bss_obj_path,
				 const char *sig_name, int properties)
{
	struct wpas_dbus_priv *iface;
	DBusMessage *msg;
	DBusMessageIter iter;

	iface = wpa_s->global->dbus;

	/* Do nothing if the control interface is not turned on */
	if (iface == NULL || !wpa_s->dbus_new_path)
		return;

	msg = dbus_message_new_signal(wpa_s->dbus_new_path,
				      WPAS_DBUS_NEW_IFACE_INTERFACE,
				      sig_name);
	if (msg == NULL)
		return;

	dbus_message_iter_init_append(msg, &iter);
	if (!dbus_message_iter_append_basic(&iter, DBUS_TYPE_OBJECT_PATH,
					    &bss_obj_path) ||
	    (properties &&
	     !wpa_dbus_get_object_properties(iface, bss_obj_path,
					     WPAS_DBUS_NEW_IFACE_BSS,
					     &iter)))
		wpa_printf(MSG_ERROR, "dbus: Failed to construct signal");
	else
		dbus_connection_send(iface->con, msg, NULL);
	dbus_message_unref(msg);
}


/**
 * wpas_dbus_signal_bss_added - Send a BSS added signal
 * @wpa_s: %wpa_supplicant network interface data
 * @bss_obj_path: new BSS object path
 *
 * Notify listeners about adding new BSS
 */
static void wpas_dbus_signal_bss_added(struct wpa_supplicant *wpa_s,
				       const char *bss_obj_path)
{
	wpas_dbus_signal_bss(wpa_s, bss_obj_path, "BSSAdded", TRUE);
}


/**
 * wpas_dbus_signal_bss_removed - Send a BSS removed signal
 * @wpa_s: %wpa_supplicant network interface data
 * @bss_obj_path: BSS object path
 *
 * Notify listeners about removing BSS
 */
static void wpas_dbus_signal_bss_removed(struct wpa_supplicant *wpa_s,
					 const char *bss_obj_path)
{
	wpas_dbus_signal_bss(wpa_s, bss_obj_path, "BSSRemoved", FALSE);
}


/**
 * wpas_dbus_signal_blob - Send a blob related event signal
 * @wpa_s: %wpa_supplicant network interface data
 * @name: blob name
 * @sig_name: signal name - BlobAdded or BlobRemoved
 *
 * Notify listeners about event related with blob
 */
static void wpas_dbus_signal_blob(struct wpa_supplicant *wpa_s,
				  const char *name, const char *sig_name)
{
	struct wpas_dbus_priv *iface;
	DBusMessage *msg;

	iface = wpa_s->global->dbus;

	/* Do nothing if the control interface is not turned on */
	if (iface == NULL || !wpa_s->dbus_new_path)
		return;

	msg = dbus_message_new_signal(wpa_s->dbus_new_path,
				      WPAS_DBUS_NEW_IFACE_INTERFACE,
				      sig_name);
	if (msg == NULL)
		return;

	if (dbus_message_append_args(msg, DBUS_TYPE_STRING, &name,
				     DBUS_TYPE_INVALID))
		dbus_connection_send(iface->con, msg, NULL);
	else
		wpa_printf(MSG_ERROR, "dbus: Failed to construct signal");
	dbus_message_unref(msg);
}


/**
 * wpas_dbus_signal_blob_added - Send a blob added signal
 * @wpa_s: %wpa_supplicant network interface data
 * @name: blob name
 *
 * Notify listeners about adding a new blob
 */
void wpas_dbus_signal_blob_added(struct wpa_supplicant *wpa_s,
				 const char *name)
{
	wpas_dbus_signal_blob(wpa_s, name, "BlobAdded");
}


/**
 * wpas_dbus_signal_blob_removed - Send a blob removed signal
 * @wpa_s: %wpa_supplicant network interface data
 * @name: blob name
 *
 * Notify listeners about removing blob
 */
void wpas_dbus_signal_blob_removed(struct wpa_supplicant *wpa_s,
				   const char *name)
{
	wpas_dbus_signal_blob(wpa_s, name, "BlobRemoved");
}


/**
 * wpas_dbus_signal_network - Send a network related event signal
 * @wpa_s: %wpa_supplicant network interface data
 * @id: new network id
 * @sig_name: signal name - NetworkAdded, NetworkRemoved or NetworkSelected
 * @properties: determines if add second argument with object properties
 *
 * Notify listeners about event related with configured network
 */
static void wpas_dbus_signal_network(struct wpa_supplicant *wpa_s,
				     int id, const char *sig_name,
				     int properties)
{
	struct wpas_dbus_priv *iface;
	DBusMessage *msg;
	DBusMessageIter iter;
	char net_obj_path[WPAS_DBUS_OBJECT_PATH_MAX], *path;

	iface = wpa_s->global->dbus;

	/* Do nothing if the control interface is not turned on */
	if (iface == NULL || !wpa_s->dbus_new_path)
		return;

	os_snprintf(net_obj_path, WPAS_DBUS_OBJECT_PATH_MAX,
		    "%s/" WPAS_DBUS_NEW_NETWORKS_PART "/%u",
		    wpa_s->dbus_new_path, id);

	msg = dbus_message_new_signal(wpa_s->dbus_new_path,
				      WPAS_DBUS_NEW_IFACE_INTERFACE,
				      sig_name);
	if (msg == NULL)
		return;

	dbus_message_iter_init_append(msg, &iter);
	path = net_obj_path;
	if (!dbus_message_iter_append_basic(&iter, DBUS_TYPE_OBJECT_PATH,
					    &path) ||
	    (properties &&
	     !wpa_dbus_get_object_properties(
		     iface, net_obj_path, WPAS_DBUS_NEW_IFACE_NETWORK,
		     &iter)))
		wpa_printf(MSG_ERROR, "dbus: Failed to construct signal");
	else
		dbus_connection_send(iface->con, msg, NULL);
	dbus_message_unref(msg);
}


/**
 * wpas_dbus_signal_network_added - Send a network added signal
 * @wpa_s: %wpa_supplicant network interface data
 * @id: new network id
 *
 * Notify listeners about adding new network
 */
static void wpas_dbus_signal_network_added(struct wpa_supplicant *wpa_s,
					   int id)
{
	wpas_dbus_signal_network(wpa_s, id, "NetworkAdded", TRUE);
}


/**
 * wpas_dbus_signal_network_removed - Send a network removed signal
 * @wpa_s: %wpa_supplicant network interface data
 * @id: network id
 *
 * Notify listeners about removing a network
 */
static void wpas_dbus_signal_network_removed(struct wpa_supplicant *wpa_s,
					     int id)
{
	wpas_dbus_signal_network(wpa_s, id, "NetworkRemoved", FALSE);
}


/**
 * wpas_dbus_signal_network_selected - Send a network selected signal
 * @wpa_s: %wpa_supplicant network interface data
 * @id: network id
 *
 * Notify listeners about selecting a network
 */
void wpas_dbus_signal_network_selected(struct wpa_supplicant *wpa_s, int id)
{
	wpas_dbus_signal_network(wpa_s, id, "NetworkSelected", FALSE);
}


/**
 * wpas_dbus_signal_network_request - Indicate that additional information
 * (EAP password, etc.) is required to complete the association to this SSID
 * @wpa_s: %wpa_supplicant network interface data
 * @rtype: The specific additional information required
 * @default_text: Optional description of required information
 *
 * Request additional information or passwords to complete an association
 * request.
 */
void wpas_dbus_signal_network_request(struct wpa_supplicant *wpa_s,
				      struct wpa_ssid *ssid,
				      enum wpa_ctrl_req_type rtype,
				      const char *default_txt)
{
	struct wpas_dbus_priv *iface;
	DBusMessage *msg;
	DBusMessageIter iter;
	char net_obj_path[WPAS_DBUS_OBJECT_PATH_MAX];
	const char *field, *txt = NULL, *net_ptr;

	iface = wpa_s->global->dbus;

	/* Do nothing if the control interface is not turned on */
	if (iface == NULL || !wpa_s->dbus_new_path)
		return;

	field = wpa_supplicant_ctrl_req_to_string(rtype, default_txt, &txt);
	if (field == NULL)
		return;

	msg = dbus_message_new_signal(wpa_s->dbus_new_path,
				      WPAS_DBUS_NEW_IFACE_INTERFACE,
				      "NetworkRequest");
	if (msg == NULL)
		return;

	os_snprintf(net_obj_path, WPAS_DBUS_OBJECT_PATH_MAX,
		    "%s/" WPAS_DBUS_NEW_NETWORKS_PART "/%u",
		    wpa_s->dbus_new_path, ssid->id);
	net_ptr = &net_obj_path[0];

	dbus_message_iter_init_append(msg, &iter);
	if (!dbus_message_iter_append_basic(&iter, DBUS_TYPE_OBJECT_PATH,
					    &net_ptr) ||
	    !dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &field) ||
	    !dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &txt))
		wpa_printf(MSG_ERROR, "dbus: Failed to construct signal");
	else
		dbus_connection_send(iface->con, msg, NULL);
	dbus_message_unref(msg);
}


/**
 * wpas_dbus_signal_network_enabled_changed - Signals Enabled property changes
 * @wpa_s: %wpa_supplicant network interface data
 * @ssid: configured network which Enabled property has changed
 *
 * Sends PropertyChanged signals containing new value of Enabled property
 * for specified network
 */
void wpas_dbus_signal_network_enabled_changed(struct wpa_supplicant *wpa_s,
					      struct wpa_ssid *ssid)
{

	char path[WPAS_DBUS_OBJECT_PATH_MAX];

	if (!wpa_s->dbus_new_path)
		return;
	os_snprintf(path, WPAS_DBUS_OBJECT_PATH_MAX,
		    "%s/" WPAS_DBUS_NEW_NETWORKS_PART "/%d",
		    wpa_s->dbus_new_path, ssid->id);

	wpa_dbus_mark_property_changed(wpa_s->global->dbus, path,
				       WPAS_DBUS_NEW_IFACE_NETWORK, "Enabled");
}


#ifdef CONFIG_WPS

/**
 * wpas_dbus_signal_wps_event_pbc_overlap - Signals PBC overlap WPS event
 * @wpa_s: %wpa_supplicant network interface data
 *
 * Sends Event dbus signal with name "pbc-overlap" and empty dict as arguments
 */
void wpas_dbus_signal_wps_event_pbc_overlap(struct wpa_supplicant *wpa_s)
{

	DBusMessage *msg;
	DBusMessageIter iter, dict_iter;
	struct wpas_dbus_priv *iface;
	char *key = "pbc-overlap";

	iface = wpa_s->global->dbus;

	/* Do nothing if the control interface is not turned on */
	if (iface == NULL || !wpa_s->dbus_new_path)
		return;

	msg = dbus_message_new_signal(wpa_s->dbus_new_path,
				      WPAS_DBUS_NEW_IFACE_WPS, "Event");
	if (msg == NULL)
		return;

	dbus_message_iter_init_append(msg, &iter);

	if (!dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &key) ||
	    !wpa_dbus_dict_open_write(&iter, &dict_iter) ||
	    !wpa_dbus_dict_close_write(&iter, &dict_iter))
		wpa_printf(MSG_ERROR, "dbus: Failed to construct signal");
	else
		dbus_connection_send(iface->con, msg, NULL);

	dbus_message_unref(msg);
}


/**
 * wpas_dbus_signal_wps_event_success - Signals Success WPS event
 * @wpa_s: %wpa_supplicant network interface data
 *
 * Sends Event dbus signal with name "success" and empty dict as arguments
 */
void wpas_dbus_signal_wps_event_success(struct wpa_supplicant *wpa_s)
{

	DBusMessage *msg;
	DBusMessageIter iter, dict_iter;
	struct wpas_dbus_priv *iface;
	char *key = "success";

	iface = wpa_s->global->dbus;

	/* Do nothing if the control interface is not turned on */
	if (iface == NULL || !wpa_s->dbus_new_path)
		return;

	msg = dbus_message_new_signal(wpa_s->dbus_new_path,
				      WPAS_DBUS_NEW_IFACE_WPS, "Event");
	if (msg == NULL)
		return;

	dbus_message_iter_init_append(msg, &iter);

	if (!dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &key) ||
	    !wpa_dbus_dict_open_write(&iter, &dict_iter) ||
	    !wpa_dbus_dict_close_write(&iter, &dict_iter))
		wpa_printf(MSG_ERROR, "dbus: Failed to construct signal");
	else
		dbus_connection_send(iface->con, msg, NULL);

	dbus_message_unref(msg);
}


/**
 * wpas_dbus_signal_wps_event_fail - Signals Fail WPS event
 * @wpa_s: %wpa_supplicant network interface data
 * @fail: WPS failure information
 *
 * Sends Event dbus signal with name "fail" and dictionary containing
 * "msg field with fail message number (int32) as arguments
 */
void wpas_dbus_signal_wps_event_fail(struct wpa_supplicant *wpa_s,
				     struct wps_event_fail *fail)
{

	DBusMessage *msg;
	DBusMessageIter iter, dict_iter;
	struct wpas_dbus_priv *iface;
	char *key = "fail";

	iface = wpa_s->global->dbus;

	/* Do nothing if the control interface is not turned on */
	if (iface == NULL || !wpa_s->dbus_new_path)
		return;

	msg = dbus_message_new_signal(wpa_s->dbus_new_path,
				      WPAS_DBUS_NEW_IFACE_WPS, "Event");
	if (msg == NULL)
		return;

	dbus_message_iter_init_append(msg, &iter);

	if (!dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &key) ||
	    !wpa_dbus_dict_open_write(&iter, &dict_iter) ||
	    !wpa_dbus_dict_append_int32(&dict_iter, "msg", fail->msg) ||
	    !wpa_dbus_dict_append_int32(&dict_iter, "config_error",
					fail->config_error) ||
	    !wpa_dbus_dict_append_int32(&dict_iter, "error_indication",
					fail->error_indication) ||
	    !wpa_dbus_dict_close_write(&iter, &dict_iter))
		wpa_printf(MSG_ERROR, "dbus: Failed to construct signal");
	else
		dbus_connection_send(iface->con, msg, NULL);

	dbus_message_unref(msg);
}


/**
 * wpas_dbus_signal_wps_event_m2d - Signals M2D WPS event
 * @wpa_s: %wpa_supplicant network interface data
 * @m2d: M2D event data information
 *
 * Sends Event dbus signal with name "m2d" and dictionary containing
 * fields of wps_event_m2d structure.
 */
void wpas_dbus_signal_wps_event_m2d(struct wpa_supplicant *wpa_s,
				    struct wps_event_m2d *m2d)
{

	DBusMessage *msg;
	DBusMessageIter iter, dict_iter;
	struct wpas_dbus_priv *iface;
	char *key = "m2d";

	iface = wpa_s->global->dbus;

	/* Do nothing if the control interface is not turned on */
	if (iface == NULL || !wpa_s->dbus_new_path)
		return;

	msg = dbus_message_new_signal(wpa_s->dbus_new_path,
				      WPAS_DBUS_NEW_IFACE_WPS, "Event");
	if (msg == NULL)
		return;

	dbus_message_iter_init_append(msg, &iter);

	if (!dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &key) ||
	    !wpa_dbus_dict_open_write(&iter, &dict_iter) ||
	    !wpa_dbus_dict_append_uint16(&dict_iter, "config_methods",
					 m2d->config_methods) ||
	    !wpa_dbus_dict_append_byte_array(&dict_iter, "manufacturer",
					     (const char *) m2d->manufacturer,
					     m2d->manufacturer_len) ||
	    !wpa_dbus_dict_append_byte_array(&dict_iter, "model_name",
					     (const char *) m2d->model_name,
					     m2d->model_name_len) ||
	    !wpa_dbus_dict_append_byte_array(&dict_iter, "model_number",
					     (const char *) m2d->model_number,
					     m2d->model_number_len) ||
	    !wpa_dbus_dict_append_byte_array(&dict_iter, "serial_number",
					     (const char *)
					     m2d->serial_number,
					     m2d->serial_number_len) ||
	    !wpa_dbus_dict_append_byte_array(&dict_iter, "dev_name",
					     (const char *) m2d->dev_name,
					     m2d->dev_name_len) ||
	    !wpa_dbus_dict_append_byte_array(&dict_iter, "primary_dev_type",
					     (const char *)
					     m2d->primary_dev_type, 8) ||
	    !wpa_dbus_dict_append_uint16(&dict_iter, "config_error",
					 m2d->config_error) ||
	    !wpa_dbus_dict_append_uint16(&dict_iter, "dev_password_id",
					 m2d->dev_password_id) ||
	    !wpa_dbus_dict_close_write(&iter, &dict_iter))
		wpa_printf(MSG_ERROR, "dbus: Failed to construct signal");
	else
		dbus_connection_send(iface->con, msg, NULL);

	dbus_message_unref(msg);
}


/**
 * wpas_dbus_signal_wps_cred - Signals new credentials
 * @wpa_s: %wpa_supplicant network interface data
 * @cred: WPS Credential information
 *
 * Sends signal with credentials in directory argument
 */
void wpas_dbus_signal_wps_cred(struct wpa_supplicant *wpa_s,
			       const struct wps_credential *cred)
{
	DBusMessage *msg;
	DBusMessageIter iter, dict_iter;
	struct wpas_dbus_priv *iface;
	char *auth_type[5]; /* we have five possible authentication types */
	int at_num = 0;
	char *encr_type[3]; /* we have three possible encryption types */
	int et_num = 0;

	iface = wpa_s->global->dbus;

	/* Do nothing if the control interface is not turned on */
	if (iface == NULL || !wpa_s->dbus_new_path)
		return;

	msg = dbus_message_new_signal(wpa_s->dbus_new_path,
				      WPAS_DBUS_NEW_IFACE_WPS,
				      "Credentials");
	if (msg == NULL)
		return;

	dbus_message_iter_init_append(msg, &iter);
	if (!wpa_dbus_dict_open_write(&iter, &dict_iter))
		goto nomem;

	if (cred->auth_type & WPS_AUTH_OPEN)
		auth_type[at_num++] = "open";
	if (cred->auth_type & WPS_AUTH_WPAPSK)
		auth_type[at_num++] = "wpa-psk";
	if (cred->auth_type & WPS_AUTH_WPA)
		auth_type[at_num++] = "wpa-eap";
	if (cred->auth_type & WPS_AUTH_WPA2)
		auth_type[at_num++] = "wpa2-eap";
	if (cred->auth_type & WPS_AUTH_WPA2PSK)
		auth_type[at_num++] = "wpa2-psk";

	if (cred->encr_type & WPS_ENCR_NONE)
		encr_type[et_num++] = "none";
	if (cred->encr_type & WPS_ENCR_TKIP)
		encr_type[et_num++] = "tkip";
	if (cred->encr_type & WPS_ENCR_AES)
		encr_type[et_num++] = "aes";

	if ((wpa_s->current_ssid &&
	     !wpa_dbus_dict_append_byte_array(
		     &dict_iter, "BSSID",
		     (const char *) wpa_s->current_ssid->bssid, ETH_ALEN)) ||
	    !wpa_dbus_dict_append_byte_array(&dict_iter, "SSID",
					     (const char *) cred->ssid,
					     cred->ssid_len) ||
	    !wpa_dbus_dict_append_string_array(&dict_iter, "AuthType",
					       (const char **) auth_type,
					       at_num) ||
	    !wpa_dbus_dict_append_string_array(&dict_iter, "EncrType",
					       (const char **) encr_type,
					       et_num) ||
	    !wpa_dbus_dict_append_byte_array(&dict_iter, "Key",
					     (const char *) cred->key,
					     cred->key_len) ||
	    !wpa_dbus_dict_append_uint32(&dict_iter, "KeyIndex",
					 cred->key_idx) ||
	    !wpa_dbus_dict_close_write(&iter, &dict_iter))
		goto nomem;

	dbus_connection_send(iface->con, msg, NULL);

nomem:
	dbus_message_unref(msg);
}

#endif /* CONFIG_WPS */


#ifdef CONFIG_MESH

void wpas_dbus_signal_mesh_group_started(struct wpa_supplicant *wpa_s,
					 struct wpa_ssid *ssid)
{
	struct wpas_dbus_priv *iface;
	DBusMessage *msg;
	DBusMessageIter iter, dict_iter;

	iface = wpa_s->global->dbus;

	/* Do nothing if the control interface is not turned on */
	if (!iface || !wpa_s->dbus_new_path)
		return;

	msg = dbus_message_new_signal(wpa_s->dbus_new_path,
				      WPAS_DBUS_NEW_IFACE_MESH,
				      "MeshGroupStarted");
	if (!msg)
		return;

	dbus_message_iter_init_append(msg, &iter);
	if (!wpa_dbus_dict_open_write(&iter, &dict_iter) ||
	    !wpa_dbus_dict_append_byte_array(&dict_iter, "SSID",
					     (const char *) ssid->ssid,
					     ssid->ssid_len) ||
	    !wpa_dbus_dict_close_write(&iter, &dict_iter))
		wpa_printf(MSG_ERROR, "dbus: Failed to construct signal");
	else
		dbus_connection_send(iface->con, msg, NULL);
	dbus_message_unref(msg);
}


void wpas_dbus_signal_mesh_group_removed(struct wpa_supplicant *wpa_s,
					 const u8 *meshid, u8 meshid_len,
					 int reason)
{
	struct wpas_dbus_priv *iface;
	DBusMessage *msg;
	DBusMessageIter iter, dict_iter;

	iface = wpa_s->global->dbus;

	/* Do nothing if the control interface is not turned on */
	if (!iface || !wpa_s->dbus_new_path)
		return;

	msg = dbus_message_new_signal(wpa_s->dbus_new_path,
				      WPAS_DBUS_NEW_IFACE_MESH,
				      "MeshGroupRemoved");
	if (!msg)
		return;

	dbus_message_iter_init_append(msg, &iter);
	if (!wpa_dbus_dict_open_write(&iter, &dict_iter) ||
	    !wpa_dbus_dict_append_byte_array(&dict_iter, "SSID",
					     (const char *) meshid,
					     meshid_len) ||
	    !wpa_dbus_dict_append_int32(&dict_iter, "DisconnectReason",
					reason) ||
	    !wpa_dbus_dict_close_write(&iter, &dict_iter))
		wpa_printf(MSG_ERROR, "dbus: Failed to construct signal");
	else
		dbus_connection_send(iface->con, msg, NULL);
	dbus_message_unref(msg);
}


void wpas_dbus_signal_mesh_peer_connected(struct wpa_supplicant *wpa_s,
					  const u8 *peer_addr)
{
	struct wpas_dbus_priv *iface;
	DBusMessage *msg;
	DBusMessageIter iter, dict_iter;

	iface = wpa_s->global->dbus;

	/* Do nothing if the control interface is not turned on */
	if (!iface || !wpa_s->dbus_new_path)
		return;

	msg = dbus_message_new_signal(wpa_s->dbus_new_path,
				      WPAS_DBUS_NEW_IFACE_MESH,
				      "MeshPeerConnected");
	if (!msg)
		return;

	dbus_message_iter_init_append(msg, &iter);
	if (!wpa_dbus_dict_open_write(&iter, &dict_iter) ||
	    !wpa_dbus_dict_append_byte_array(&dict_iter, "PeerAddress",
					     (const char *) peer_addr,
					     ETH_ALEN) ||
	    !wpa_dbus_dict_close_write(&iter, &dict_iter))
		wpa_printf(MSG_ERROR, "dbus: Failed to construct signal");
	else
		dbus_connection_send(iface->con, msg, NULL);
	dbus_message_unref(msg);
}


void wpas_dbus_signal_mesh_peer_disconnected(struct wpa_supplicant *wpa_s,
					     const u8 *peer_addr, int reason)
{
	struct wpas_dbus_priv *iface;
	DBusMessage *msg;
	DBusMessageIter iter, dict_iter;

	iface = wpa_s->global->dbus;

	/* Do nothing if the control interface is not turned on */
	if (!iface || !wpa_s->dbus_new_path)
		return;

	msg = dbus_message_new_signal(wpa_s->dbus_new_path,
				      WPAS_DBUS_NEW_IFACE_MESH,
				      "MeshPeerDisconnected");
	if (!msg)
		return;

	dbus_message_iter_init_append(msg, &iter);
	if (!wpa_dbus_dict_open_write(&iter, &dict_iter) ||
	    !wpa_dbus_dict_append_byte_array(&dict_iter, "PeerAddress",
					     (const char *) peer_addr,
					     ETH_ALEN) ||
	    !wpa_dbus_dict_append_int32(&dict_iter, "DisconnectReason",
					reason) ||
	    !wpa_dbus_dict_close_write(&iter, &dict_iter))
		wpa_printf(MSG_ERROR, "dbus: Failed to construct signal");
	else
		dbus_connection_send(iface->con, msg, NULL);
	dbus_message_unref(msg);
}

#endif /* CONFIG_MESH */


void wpas_dbus_signal_certification(struct wpa_supplicant *wpa_s,
				    int depth, const char *subject,
				    const char *altsubject[],
				    int num_altsubject,
				    const char *cert_hash,
				    const struct wpabuf *cert)
{
	struct wpas_dbus_priv *iface;
	DBusMessage *msg;
	DBusMessageIter iter, dict_iter;

	iface = wpa_s->global->dbus;

	/* Do nothing if the control interface is not turned on */
	if (iface == NULL || !wpa_s->dbus_new_path)
		return;

	msg = dbus_message_new_signal(wpa_s->dbus_new_path,
				      WPAS_DBUS_NEW_IFACE_INTERFACE,
				      "Certification");
	if (msg == NULL)
		return;

	dbus_message_iter_init_append(msg, &iter);
	if (!wpa_dbus_dict_open_write(&iter, &dict_iter) ||
	    !wpa_dbus_dict_append_uint32(&dict_iter, "depth", depth) ||
	    !wpa_dbus_dict_append_string(&dict_iter, "subject", subject) ||
	    (altsubject && num_altsubject &&
	     !wpa_dbus_dict_append_string_array(&dict_iter, "altsubject",
						altsubject, num_altsubject)) ||
	    (cert_hash &&
	     !wpa_dbus_dict_append_string(&dict_iter, "cert_hash",
					  cert_hash)) ||
	    (cert &&
	     !wpa_dbus_dict_append_byte_array(&dict_iter, "cert",
					      wpabuf_head(cert),
					      wpabuf_len(cert))) ||
	    !wpa_dbus_dict_close_write(&iter, &dict_iter))
		wpa_printf(MSG_ERROR, "dbus: Failed to construct signal");
	else
		dbus_connection_send(iface->con, msg, NULL);
	dbus_message_unref(msg);
}


void wpas_dbus_signal_eap_status(struct wpa_supplicant *wpa_s,
				 const char *status, const char *parameter)
{
	struct wpas_dbus_priv *iface;
	DBusMessage *msg;
	DBusMessageIter iter;

	iface = wpa_s->global->dbus;

	/* Do nothing if the control interface is not turned on */
	if (iface == NULL || !wpa_s->dbus_new_path)
		return;

	msg = dbus_message_new_signal(wpa_s->dbus_new_path,
				      WPAS_DBUS_NEW_IFACE_INTERFACE,
				      "EAP");
	if (msg == NULL)
		return;

	dbus_message_iter_init_append(msg, &iter);

	if (!dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &status) ||
	    !dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING,
					    &parameter))
		wpa_printf(MSG_ERROR, "dbus: Failed to construct signal");
	else
		dbus_connection_send(iface->con, msg, NULL);
	dbus_message_unref(msg);
}


/**
 * wpas_dbus_signal_sta - Send a station related event signal
 * @wpa_s: %wpa_supplicant network interface data
 * @sta: station mac address
 * @sig_name: signal name - StaAuthorized or StaDeauthorized
 *
 * Notify listeners about event related with station
 */
static void wpas_dbus_signal_sta(struct wpa_supplicant *wpa_s,
				 const u8 *sta, const char *sig_name)
{
	struct wpas_dbus_priv *iface;
	DBusMessage *msg;
	char sta_mac[WPAS_DBUS_OBJECT_PATH_MAX];
	char *dev_mac;

	os_snprintf(sta_mac, WPAS_DBUS_OBJECT_PATH_MAX, MACSTR, MAC2STR(sta));
	dev_mac = sta_mac;

	iface = wpa_s->global->dbus;

	/* Do nothing if the control interface is not turned on */
	if (iface == NULL || !wpa_s->dbus_new_path)
		return;

	msg = dbus_message_new_signal(wpa_s->dbus_new_path,
				      WPAS_DBUS_NEW_IFACE_INTERFACE, sig_name);
	if (msg == NULL)
		return;

	if (dbus_message_append_args(msg, DBUS_TYPE_STRING, &dev_mac,
				     DBUS_TYPE_INVALID))
		dbus_connection_send(iface->con, msg, NULL);
	else
		wpa_printf(MSG_ERROR, "dbus: Failed to construct signal");
	dbus_message_unref(msg);

	wpa_printf(MSG_DEBUG, "dbus: Station MAC address '%s' '%s'",
		   sta_mac, sig_name);
}


/**
 * wpas_dbus_signal_sta_authorized - Send a STA authorized signal
 * @wpa_s: %wpa_supplicant network interface data
 * @sta: station mac address
 *
 * Notify listeners a new station has been authorized
 */
void wpas_dbus_signal_sta_authorized(struct wpa_supplicant *wpa_s,
				     const u8 *sta)
{
	wpas_dbus_signal_sta(wpa_s, sta, "StaAuthorized");
}


/**
 * wpas_dbus_signal_sta_deauthorized - Send a STA deauthorized signal
 * @wpa_s: %wpa_supplicant network interface data
 * @sta: station mac address
 *
 * Notify listeners a station has been deauthorized
 */
void wpas_dbus_signal_sta_deauthorized(struct wpa_supplicant *wpa_s,
				       const u8 *sta)
{
	wpas_dbus_signal_sta(wpa_s, sta, "StaDeauthorized");
}


#ifdef CONFIG_P2P

/**
 * wpas_dbus_signal_p2p_group_removed - Signals P2P group was removed
 * @wpa_s: %wpa_supplicant network interface data
 * @role: role of this device (client or GO)
 * Sends signal with i/f name and role as string arguments
 */
void wpas_dbus_signal_p2p_group_removed(struct wpa_supplicant *wpa_s,
					const char *role)
{
	DBusMessage *msg;
	DBusMessageIter iter, dict_iter;
	struct wpas_dbus_priv *iface = wpa_s->global->dbus;
	struct wpa_supplicant *parent;

	/* Do nothing if the control interface is not turned on */
	if (iface == NULL)
		return;

	parent = wpa_s->parent;
	if (parent->p2p_mgmt)
		parent = parent->parent;

	if (!wpa_s->dbus_groupobj_path || !wpa_s->dbus_new_path ||
	    !parent->dbus_new_path)
		return;

	msg = dbus_message_new_signal(parent->dbus_new_path,
				      WPAS_DBUS_NEW_IFACE_P2PDEVICE,
				      "GroupFinished");
	if (msg == NULL)
		return;

	dbus_message_iter_init_append(msg, &iter);
	if (!wpa_dbus_dict_open_write(&iter, &dict_iter) ||
	    !wpa_dbus_dict_append_object_path(&dict_iter,
					      "interface_object",
					      wpa_s->dbus_new_path) ||
	    !wpa_dbus_dict_append_string(&dict_iter, "role", role) ||
	    !wpa_dbus_dict_append_object_path(&dict_iter, "group_object",
					      wpa_s->dbus_groupobj_path) ||
	    !wpa_dbus_dict_close_write(&iter, &dict_iter))
		wpa_printf(MSG_ERROR, "dbus: Failed to construct signal");
	else
		dbus_connection_send(iface->con, msg, NULL);
	dbus_message_unref(msg);
}


/**
 * wpas_dbus_signal_p2p_provision_discovery - Signals various PD events
 *
 * @dev_addr - who sent the request or responded to our request.
 * @request - Will be 1 if request, 0 for response.
 * @status - valid only in case of response
 * @config_methods - wps config methods
 * @generated_pin - pin to be displayed in case of WPS_CONFIG_DISPLAY method
 *
 * Sends following provision discovery related events:
 *	ProvisionDiscoveryRequestDisplayPin
 *	ProvisionDiscoveryResponseDisplayPin
 *	ProvisionDiscoveryRequestEnterPin
 *	ProvisionDiscoveryResponseEnterPin
 *	ProvisionDiscoveryPBCRequest
 *	ProvisionDiscoveryPBCResponse
 *
 *	TODO::
 *	ProvisionDiscoveryFailure (timeout case)
 */
void wpas_dbus_signal_p2p_provision_discovery(struct wpa_supplicant *wpa_s,
					      const u8 *dev_addr, int request,
					      enum p2p_prov_disc_status status,
					      u16 config_methods,
					      unsigned int generated_pin)
{
	DBusMessage *msg;
	DBusMessageIter iter;
	struct wpas_dbus_priv *iface;
	char *_signal;
	int add_pin = 0;
	char peer_obj_path[WPAS_DBUS_OBJECT_PATH_MAX], *path;
	int error_ret = 1;
	char pin[9], *p_pin = NULL;

	iface = wpa_s->global->dbus;

	/* Do nothing if the control interface is not turned on */
	if (iface == NULL)
		return;

	if (wpa_s->p2p_mgmt)
		wpa_s = wpa_s->parent;
	if (!wpa_s->dbus_new_path)
		return;

	if (request || !status) {
		if (config_methods & WPS_CONFIG_DISPLAY)
			_signal = request ?
				 "ProvisionDiscoveryRequestDisplayPin" :
				 "ProvisionDiscoveryResponseEnterPin";
		else if (config_methods & WPS_CONFIG_KEYPAD)
			_signal = request ?
				 "ProvisionDiscoveryRequestEnterPin" :
				 "ProvisionDiscoveryResponseDisplayPin";
		else if (config_methods & WPS_CONFIG_PUSHBUTTON)
			_signal = request ? "ProvisionDiscoveryPBCRequest" :
				   "ProvisionDiscoveryPBCResponse";
		else
			return; /* Unknown or un-supported method */
	} else {
		/* Explicit check for failure response */
		_signal = "ProvisionDiscoveryFailure";
	}

	add_pin = ((request && (config_methods & WPS_CONFIG_DISPLAY)) ||
		   (!request && !status &&
			(config_methods & WPS_CONFIG_KEYPAD)));

	if (add_pin) {
		os_snprintf(pin, sizeof(pin), "%08d", generated_pin);
		p_pin = pin;
	}

	msg = dbus_message_new_signal(wpa_s->dbus_new_path,
				      WPAS_DBUS_NEW_IFACE_P2PDEVICE, _signal);
	if (msg == NULL)
		return;

	/* Check if this is a known peer */
	if (!p2p_peer_known(wpa_s->global->p2p, dev_addr))
		goto error;

	os_snprintf(peer_obj_path, WPAS_DBUS_OBJECT_PATH_MAX,
			"%s/" WPAS_DBUS_NEW_P2P_PEERS_PART "/"
			COMPACT_MACSTR,
			wpa_s->dbus_new_path, MAC2STR(dev_addr));

	path = peer_obj_path;

	dbus_message_iter_init_append(msg, &iter);

	if (!dbus_message_iter_append_basic(&iter,
					    DBUS_TYPE_OBJECT_PATH,
					    &path))
			goto error;

	if (!request && status)
		/* Attach status to ProvisionDiscoveryFailure */
		error_ret = !dbus_message_iter_append_basic(&iter,
						    DBUS_TYPE_INT32,
						    &status);
	else
		error_ret = (add_pin &&
				 !dbus_message_iter_append_basic(&iter,
							DBUS_TYPE_STRING,
							&p_pin));

error:
	if (!error_ret)
		dbus_connection_send(iface->con, msg, NULL);
	else
		wpa_printf(MSG_ERROR, "dbus: Failed to construct signal");

	dbus_message_unref(msg);
}


/**
 * wpas_dbus_signal_p2p_go_neg_req - Signal P2P GO Negotiation Request RX
 * @wpa_s: %wpa_supplicant network interface data
 * @src: Source address of the message triggering this notification
 * @dev_passwd_id: WPS Device Password Id
 * @go_intent: Peer's GO Intent value
 *
 * Sends signal to notify that a peer P2P Device is requesting group owner
 * negotiation with us.
 */
void wpas_dbus_signal_p2p_go_neg_req(struct wpa_supplicant *wpa_s,
				     const u8 *src, u16 dev_passwd_id,
				     u8 go_intent)
{
	DBusMessage *msg;
	DBusMessageIter iter;
	struct wpas_dbus_priv *iface;
	char peer_obj_path[WPAS_DBUS_OBJECT_PATH_MAX], *path;

	iface = wpa_s->global->dbus;

	/* Do nothing if the control interface is not turned on */
	if (iface == NULL)
		return;

	if (wpa_s->p2p_mgmt)
		wpa_s = wpa_s->parent;
	if (!wpa_s->dbus_new_path)
		return;

	os_snprintf(peer_obj_path, WPAS_DBUS_OBJECT_PATH_MAX,
		    "%s/" WPAS_DBUS_NEW_P2P_PEERS_PART "/" COMPACT_MACSTR,
		    wpa_s->dbus_new_path, MAC2STR(src));
	path = peer_obj_path;

	msg = dbus_message_new_signal(wpa_s->dbus_new_path,
				      WPAS_DBUS_NEW_IFACE_P2PDEVICE,
				      "GONegotiationRequest");
	if (msg == NULL)
		return;

	dbus_message_iter_init_append(msg, &iter);

	if (!dbus_message_iter_append_basic(&iter, DBUS_TYPE_OBJECT_PATH,
					    &path) ||
	    !dbus_message_iter_append_basic(&iter, DBUS_TYPE_UINT16,
					    &dev_passwd_id) ||
	    !dbus_message_iter_append_basic(&iter, DBUS_TYPE_BYTE,
					    &go_intent))
		wpa_printf(MSG_ERROR, "dbus: Failed to construct signal");
	else
		dbus_connection_send(iface->con, msg, NULL);

	dbus_message_unref(msg);
}


static int wpas_dbus_get_group_obj_path(struct wpa_supplicant *wpa_s,
					const struct wpa_ssid *ssid,
					char *group_obj_path)
{
	char group_name[3];

	if (!wpa_s->dbus_new_path ||
	    os_memcmp(ssid->ssid, P2P_WILDCARD_SSID, P2P_WILDCARD_SSID_LEN))
		return -1;

	os_memcpy(group_name, ssid->ssid + P2P_WILDCARD_SSID_LEN, 2);
	group_name[2] = '\0';

	os_snprintf(group_obj_path, WPAS_DBUS_OBJECT_PATH_MAX,
		    "%s/" WPAS_DBUS_NEW_P2P_GROUPS_PART "/%s",
		    wpa_s->dbus_new_path, group_name);

	return 0;
}


struct group_changed_data {
	struct wpa_supplicant *wpa_s;
	struct p2p_peer_info *info;
};


static int match_group_where_peer_is_client(struct p2p_group *group,
					    void *user_data)
{
	struct group_changed_data *data = user_data;
	const struct p2p_group_config *cfg;
	struct wpa_supplicant *wpa_s_go;

	if (!p2p_group_is_client_connected(group, data->info->p2p_device_addr))
		return 1;

	cfg = p2p_group_get_config(group);

	wpa_s_go = wpas_get_p2p_go_iface(data->wpa_s, cfg->ssid,
					 cfg->ssid_len);
	if (wpa_s_go != NULL && wpa_s_go == data->wpa_s) {
		wpas_dbus_signal_peer_groups_changed(
			data->wpa_s->p2pdev, data->info->p2p_device_addr);
		return 0;
	}

	return 1;
}


static void signal_peer_groups_changed(struct p2p_peer_info *info,
				       void *user_data)
{
	struct group_changed_data *data = user_data;
	struct wpa_supplicant *wpa_s_go;

	wpa_s_go = wpas_get_p2p_client_iface(data->wpa_s,
					     info->p2p_device_addr);
	if (wpa_s_go != NULL && wpa_s_go == data->wpa_s) {
		wpas_dbus_signal_peer_groups_changed(data->wpa_s->p2pdev,
						     info->p2p_device_addr);
		return;
	}

	data->info = info;
	p2p_loop_on_all_groups(data->wpa_s->global->p2p,
			       match_group_where_peer_is_client, data);
	data->info = NULL;
}


static void peer_groups_changed(struct wpa_supplicant *wpa_s)
{
	struct group_changed_data data;

	os_memset(&data, 0, sizeof(data));
	data.wpa_s = wpa_s;

	p2p_loop_on_known_peers(wpa_s->global->p2p,
				signal_peer_groups_changed, &data);
}


/**
 * wpas_dbus_signal_p2p_group_started - Signals P2P group has
 * started. Emitted when a group is successfully started
 * irrespective of the role (client/GO) of the current device
 *
 * @wpa_s: %wpa_supplicant network interface data
 * @client: this device is P2P client
 * @persistent: 0 - non persistent group, 1 - persistent group
 * @ip: When group role is client, it contains local IP address, netmask, and
 *	GO's IP address, if assigned; otherwise, NULL
 */
void wpas_dbus_signal_p2p_group_started(struct wpa_supplicant *wpa_s,
					int client, int persistent,
					const u8 *ip)
{
	DBusMessage *msg;
	DBusMessageIter iter, dict_iter;
	struct wpas_dbus_priv *iface;
	struct wpa_supplicant *parent;

	parent = wpa_s->parent;
	if (parent->p2p_mgmt)
		parent = parent->parent;

	iface = parent->global->dbus;

	/* Do nothing if the control interface is not turned on */
	if (iface == NULL || !parent->dbus_new_path || !wpa_s->dbus_new_path)
		return;

	if (wpa_s->dbus_groupobj_path == NULL)
		return;

	/* New interface has been created for this group */
	msg = dbus_message_new_signal(parent->dbus_new_path,
				      WPAS_DBUS_NEW_IFACE_P2PDEVICE,
				      "GroupStarted");
	if (msg == NULL)
		return;

	dbus_message_iter_init_append(msg, &iter);
	/*
	 * In case the device supports creating a separate interface the
	 * DBus client will need to know the object path for the interface
	 * object this group was created on, so include it here.
	 */
	if (!wpa_dbus_dict_open_write(&iter, &dict_iter) ||
	    !wpa_dbus_dict_append_object_path(&dict_iter,
					      "interface_object",
					      wpa_s->dbus_new_path) ||
	    !wpa_dbus_dict_append_string(&dict_iter, "role",
					 client ? "client" : "GO") ||
	    !wpa_dbus_dict_append_bool(&dict_iter, "persistent", persistent) ||
	    !wpa_dbus_dict_append_object_path(&dict_iter, "group_object",
					      wpa_s->dbus_groupobj_path) ||
	    (ip &&
	     (!wpa_dbus_dict_append_byte_array(&dict_iter, "IpAddr",
					       (char *) ip, 4) ||
	      !wpa_dbus_dict_append_byte_array(&dict_iter, "IpAddrMask",
					       (char *) ip + 4, 4) ||
	      !wpa_dbus_dict_append_byte_array(&dict_iter, "IpAddrGo",
					       (char *) ip + 8, 4))) ||
	    !wpa_dbus_dict_close_write(&iter, &dict_iter)) {
		wpa_printf(MSG_ERROR, "dbus: Failed to construct signal");
	} else {
		dbus_connection_send(iface->con, msg, NULL);
		if (client)
			peer_groups_changed(wpa_s);
	}
	dbus_message_unref(msg);
}


/**
 * wpas_dbus_signal_p2p_go_neg_resp - Emit GONegotiation Success/Failure signal
 * @wpa_s: %wpa_supplicant network interface data
 * @res: Result of the GO Neg Request
 */
void wpas_dbus_signal_p2p_go_neg_resp(struct wpa_supplicant *wpa_s,
				      struct p2p_go_neg_results *res)
{
	DBusMessage *msg;
	DBusMessageIter iter, dict_iter;
	DBusMessageIter iter_dict_entry, iter_dict_val, iter_dict_array;
	struct wpas_dbus_priv *iface;
	char peer_obj_path[WPAS_DBUS_OBJECT_PATH_MAX], *path;
	dbus_int32_t freqs[P2P_MAX_CHANNELS];
	dbus_int32_t *f_array = freqs;


	iface = wpa_s->global->dbus;

	if (wpa_s->p2p_mgmt)
		wpa_s = wpa_s->parent;

	os_memset(freqs, 0, sizeof(freqs));
	/* Do nothing if the control interface is not turned on */
	if (iface == NULL || !wpa_s->dbus_new_path)
		return;

	os_snprintf(peer_obj_path, WPAS_DBUS_OBJECT_PATH_MAX,
		    "%s/" WPAS_DBUS_NEW_P2P_PEERS_PART "/" COMPACT_MACSTR,
		    wpa_s->dbus_new_path, MAC2STR(res->peer_device_addr));
	path = peer_obj_path;

	msg = dbus_message_new_signal(wpa_s->dbus_new_path,
				      WPAS_DBUS_NEW_IFACE_P2PDEVICE,
				      res->status ? "GONegotiationFailure" :
						    "GONegotiationSuccess");
	if (msg == NULL)
		return;

	dbus_message_iter_init_append(msg, &iter);
	if (!wpa_dbus_dict_open_write(&iter, &dict_iter) ||
	    !wpa_dbus_dict_append_object_path(&dict_iter, "peer_object",
					      path) ||
	    !wpa_dbus_dict_append_int32(&dict_iter, "status", res->status))
		goto err;

	if (!res->status) {
		int i = 0;
		int freq_list_num = 0;

		if ((res->role_go &&
		     !wpa_dbus_dict_append_string(&dict_iter, "passphrase",
						  res->passphrase)) ||
		    !wpa_dbus_dict_append_string(&dict_iter, "role_go",
						 res->role_go ? "GO" :
						 "client") ||
		    !wpa_dbus_dict_append_int32(&dict_iter, "frequency",
						res->freq) ||
		    !wpa_dbus_dict_append_byte_array(&dict_iter, "ssid",
						     (const char *) res->ssid,
						     res->ssid_len) ||
		    !wpa_dbus_dict_append_byte_array(&dict_iter,
						     "peer_device_addr",
						     (const char *)
						     res->peer_device_addr,
						     ETH_ALEN) ||
		    !wpa_dbus_dict_append_byte_array(&dict_iter,
						     "peer_interface_addr",
						     (const char *)
						     res->peer_interface_addr,
						     ETH_ALEN) ||
		    !wpa_dbus_dict_append_string(&dict_iter, "wps_method",
						 p2p_wps_method_text(
							 res->wps_method)))
			goto err;

		for (i = 0; i < P2P_MAX_CHANNELS; i++) {
			if (res->freq_list[i]) {
				freqs[i] = res->freq_list[i];
				freq_list_num++;
			}
		}

		if (!wpa_dbus_dict_begin_array(&dict_iter,
					       "frequency_list",
					       DBUS_TYPE_INT32_AS_STRING,
					       &iter_dict_entry,
					       &iter_dict_val,
					       &iter_dict_array) ||
		    !dbus_message_iter_append_fixed_array(&iter_dict_array,
							  DBUS_TYPE_INT32,
							  &f_array,
							  freq_list_num) ||
		    !wpa_dbus_dict_end_array(&dict_iter,
					     &iter_dict_entry,
					     &iter_dict_val,
					     &iter_dict_array) ||
		    !wpa_dbus_dict_append_int32(&dict_iter, "persistent_group",
						res->persistent_group) ||
		    !wpa_dbus_dict_append_uint32(&dict_iter,
						 "peer_config_timeout",
						 res->peer_config_timeout))
			goto err;
	}

	if (!wpa_dbus_dict_close_write(&iter, &dict_iter))
		goto err;

	dbus_connection_send(iface->con, msg, NULL);
err:
	dbus_message_unref(msg);
}


/**
 * wpas_dbus_signal_p2p_invitation_result - Emit InvitationResult signal
 * @wpa_s: %wpa_supplicant network interface data
 * @status: Status of invitation process
 * @bssid: Basic Service Set Identifier
 */
void wpas_dbus_signal_p2p_invitation_result(struct wpa_supplicant *wpa_s,
					    int status, const u8 *bssid)
{
	DBusMessage *msg;
	DBusMessageIter iter, dict_iter;
	struct wpas_dbus_priv *iface;

	wpa_printf(MSG_DEBUG, "%s", __func__);

	iface = wpa_s->global->dbus;
	/* Do nothing if the control interface is not turned on */
	if (iface == NULL)
		return;

	if (wpa_s->p2p_mgmt)
		wpa_s = wpa_s->parent;
	if (!wpa_s->dbus_new_path)
		return;

	msg = dbus_message_new_signal(wpa_s->dbus_new_path,
				      WPAS_DBUS_NEW_IFACE_P2PDEVICE,
				      "InvitationResult");

	if (msg == NULL)
		return;

	dbus_message_iter_init_append(msg, &iter);
	if (!wpa_dbus_dict_open_write(&iter, &dict_iter) ||
	    !wpa_dbus_dict_append_int32(&dict_iter, "status", status) ||
	    (bssid &&
	     !wpa_dbus_dict_append_byte_array(&dict_iter, "BSSID",
					      (const char *) bssid,
					      ETH_ALEN)) ||
	    !wpa_dbus_dict_close_write(&iter, &dict_iter))
		wpa_printf(MSG_ERROR, "dbus: Failed to construct signal");
	else
		dbus_connection_send(iface->con, msg, NULL);
	dbus_message_unref(msg);
}


/**
 *
 * Method to emit a signal for a peer joining the group.
 * The signal will carry path to the group member object
 * constructed using p2p i/f addr used for connecting.
 *
 * @wpa_s: %wpa_supplicant network interface data
 * @peer_addr: P2P Device Address of the peer joining the group
 */
void wpas_dbus_signal_p2p_peer_joined(struct wpa_supplicant *wpa_s,
				      const u8 *peer_addr)
{
	struct wpas_dbus_priv *iface;
	DBusMessage *msg;
	DBusMessageIter iter;
	char peer_obj_path[WPAS_DBUS_OBJECT_PATH_MAX], *path;
	struct wpa_supplicant *parent;

	iface = wpa_s->global->dbus;

	/* Do nothing if the control interface is not turned on */
	if (iface == NULL)
		return;

	if (!wpa_s->dbus_groupobj_path)
		return;

	parent = wpa_s->parent;
	if (parent->p2p_mgmt)
		parent = parent->parent;
	if (!parent->dbus_new_path)
		return;

	os_snprintf(peer_obj_path, WPAS_DBUS_OBJECT_PATH_MAX,
			"%s/" WPAS_DBUS_NEW_P2P_PEERS_PART "/"
			COMPACT_MACSTR,
			parent->dbus_new_path, MAC2STR(peer_addr));

	msg = dbus_message_new_signal(wpa_s->dbus_groupobj_path,
				      WPAS_DBUS_NEW_IFACE_P2P_GROUP,
				      "PeerJoined");
	if (msg == NULL)
		return;

	dbus_message_iter_init_append(msg, &iter);
	path = peer_obj_path;
	if (!dbus_message_iter_append_basic(&iter, DBUS_TYPE_OBJECT_PATH,
					    &path)) {
		wpa_printf(MSG_ERROR, "dbus: Failed to construct signal");
	} else {
		dbus_connection_send(iface->con, msg, NULL);
		wpas_dbus_signal_peer_groups_changed(parent, peer_addr);
	}
	dbus_message_unref(msg);
}


/**
 *
 * Method to emit a signal for a peer disconnecting the group.
 * The signal will carry path to the group member object
 * constructed using the P2P Device Address of the peer.
 *
 * @wpa_s: %wpa_supplicant network interface data
 * @peer_addr: P2P Device Address of the peer joining the group
 */
void wpas_dbus_signal_p2p_peer_disconnected(struct wpa_supplicant *wpa_s,
					    const u8 *peer_addr)
{
	struct wpas_dbus_priv *iface;
	DBusMessage *msg;
	DBusMessageIter iter;
	char peer_obj_path[WPAS_DBUS_OBJECT_PATH_MAX], *path;
	struct wpa_supplicant *parent;

	iface = wpa_s->global->dbus;

	/* Do nothing if the control interface is not turned on */
	if (iface == NULL)
		return;

	if (!wpa_s->dbus_groupobj_path)
		return;

	parent = wpa_s->parent;
	if (parent->p2p_mgmt)
		parent = parent->parent;
	if (!parent->dbus_new_path)
		return;

	os_snprintf(peer_obj_path, WPAS_DBUS_OBJECT_PATH_MAX,
			"%s/" WPAS_DBUS_NEW_P2P_PEERS_PART "/"
			COMPACT_MACSTR,
			parent->dbus_new_path, MAC2STR(peer_addr));

	msg = dbus_message_new_signal(wpa_s->dbus_groupobj_path,
				      WPAS_DBUS_NEW_IFACE_P2P_GROUP,
				      "PeerDisconnected");
	if (msg == NULL)
		return;

	dbus_message_iter_init_append(msg, &iter);
	path = peer_obj_path;
	if (!dbus_message_iter_append_basic(&iter, DBUS_TYPE_OBJECT_PATH,
					    &path)) {
		wpa_printf(MSG_ERROR,
			   "dbus: Failed to construct PeerDisconnected signal");
	} else {
		dbus_connection_send(iface->con, msg, NULL);
		wpas_dbus_signal_peer_groups_changed(parent, peer_addr);
	}
	dbus_message_unref(msg);
}


/**
 *
 * Method to emit a signal for a service discovery request.
 * The signal will carry station address, frequency, dialog token,
 * update indicator and it tlvs
 *
 * @wpa_s: %wpa_supplicant network interface data
 * @sa: station addr (p2p i/f) of the peer
 * @dialog_token: service discovery request dialog token
 * @update_indic: service discovery request update indicator
 * @tlvs: service discovery request genrated byte array of tlvs
 * @tlvs_len: service discovery request tlvs length
 */
void wpas_dbus_signal_p2p_sd_request(struct wpa_supplicant *wpa_s,
				     int freq, const u8 *sa, u8 dialog_token,
				     u16 update_indic, const u8 *tlvs,
				     size_t tlvs_len)
{
	DBusMessage *msg;
	DBusMessageIter iter, dict_iter;
	struct wpas_dbus_priv *iface;
	char peer_obj_path[WPAS_DBUS_OBJECT_PATH_MAX], *path;

	iface = wpa_s->global->dbus;

	/* Do nothing if the control interface is not turned on */
	if (iface == NULL)
		return;

	if (wpa_s->p2p_mgmt)
		wpa_s = wpa_s->parent;
	if (!wpa_s->dbus_new_path)
		return;

	/* Check if this is a known peer */
	if (!p2p_peer_known(wpa_s->global->p2p, sa))
		return;

	msg = dbus_message_new_signal(wpa_s->dbus_new_path,
				      WPAS_DBUS_NEW_IFACE_P2PDEVICE,
				      "ServiceDiscoveryRequest");
	if (msg == NULL)
		return;

	os_snprintf(peer_obj_path, WPAS_DBUS_OBJECT_PATH_MAX,
		    "%s/" WPAS_DBUS_NEW_P2P_PEERS_PART "/"
		    COMPACT_MACSTR, wpa_s->dbus_new_path, MAC2STR(sa));

	path = peer_obj_path;

	dbus_message_iter_init_append(msg, &iter);
	if (!wpa_dbus_dict_open_write(&iter, &dict_iter) ||
	    !wpa_dbus_dict_append_object_path(&dict_iter, "peer_object",
					      path) ||
	    !wpa_dbus_dict_append_int32(&dict_iter, "frequency", freq) ||
	    !wpa_dbus_dict_append_int32(&dict_iter, "dialog_token",
					dialog_token) ||
	    !wpa_dbus_dict_append_uint16(&dict_iter, "update_indicator",
					 update_indic) ||
	    !wpa_dbus_dict_append_byte_array(&dict_iter, "tlvs",
					     (const char *) tlvs,
					     tlvs_len) ||
	    !wpa_dbus_dict_close_write(&iter, &dict_iter))
		wpa_printf(MSG_ERROR, "dbus: Failed to construct signal");
	else
		dbus_connection_send(iface->con, msg, NULL);
	dbus_message_unref(msg);
}


/**
 *
 * Method to emit a signal for a service discovery response.
 * The signal will carry station address, update indicator and it
 * tlvs
 *
 * @wpa_s: %wpa_supplicant network interface data
 * @sa: station addr (p2p i/f) of the peer
 * @update_indic: service discovery request update indicator
 * @tlvs: service discovery request genrated byte array of tlvs
 * @tlvs_len: service discovery request tlvs length
 */
void wpas_dbus_signal_p2p_sd_response(struct wpa_supplicant *wpa_s,
				      const u8 *sa, u16 update_indic,
				      const u8 *tlvs, size_t tlvs_len)
{
	DBusMessage *msg;
	DBusMessageIter iter, dict_iter;
	struct wpas_dbus_priv *iface;
	char peer_obj_path[WPAS_DBUS_OBJECT_PATH_MAX], *path;

	iface = wpa_s->global->dbus;

	/* Do nothing if the control interface is not turned on */
	if (iface == NULL)
		return;

	if (wpa_s->p2p_mgmt)
		wpa_s = wpa_s->parent;
	if (!wpa_s->dbus_new_path)
		return;

	/* Check if this is a known peer */
	if (!p2p_peer_known(wpa_s->global->p2p, sa))
		return;

	msg = dbus_message_new_signal(wpa_s->dbus_new_path,
				      WPAS_DBUS_NEW_IFACE_P2PDEVICE,
				      "ServiceDiscoveryResponse");
	if (msg == NULL)
		return;

	os_snprintf(peer_obj_path, WPAS_DBUS_OBJECT_PATH_MAX,
		    "%s/" WPAS_DBUS_NEW_P2P_PEERS_PART "/"
		    COMPACT_MACSTR, wpa_s->dbus_new_path, MAC2STR(sa));

	path = peer_obj_path;

	dbus_message_iter_init_append(msg, &iter);
	if (!wpa_dbus_dict_open_write(&iter, &dict_iter) ||
	    !wpa_dbus_dict_append_object_path(&dict_iter, "peer_object",
					      path) ||
	    !wpa_dbus_dict_append_uint16(&dict_iter, "update_indicator",
					 update_indic) ||
	    !wpa_dbus_dict_append_byte_array(&dict_iter, "tlvs",
					     (const char *) tlvs,
					     tlvs_len) ||
	    !wpa_dbus_dict_close_write(&iter, &dict_iter))
		wpa_printf(MSG_ERROR, "dbus: Failed to construct signal");
	else
		dbus_connection_send(iface->con, msg, NULL);
	dbus_message_unref(msg);
}


/**
 * wpas_dbus_signal_persistent_group - Send a persistent group related
 *	event signal
 * @wpa_s: %wpa_supplicant network interface data
 * @id: new persistent group id
 * @sig_name: signal name - PersistentGroupAdded, PersistentGroupRemoved
 * @properties: determines if add second argument with object properties
 *
 * Notify listeners about an event related to persistent groups.
 */
static void wpas_dbus_signal_persistent_group(struct wpa_supplicant *wpa_s,
					      int id, const char *sig_name,
					      int properties)
{
	struct wpas_dbus_priv *iface;
	DBusMessage *msg;
	DBusMessageIter iter;
	char pgrp_obj_path[WPAS_DBUS_OBJECT_PATH_MAX], *path;

	iface = wpa_s->global->dbus;

	/* Do nothing if the control interface is not turned on */
	if (iface == NULL)
		return;

	if (wpa_s->p2p_mgmt)
		wpa_s = wpa_s->parent;
	if (!wpa_s->dbus_new_path)
		return;

	os_snprintf(pgrp_obj_path, WPAS_DBUS_OBJECT_PATH_MAX,
		    "%s/" WPAS_DBUS_NEW_PERSISTENT_GROUPS_PART "/%u",
		    wpa_s->dbus_new_path, id);

	msg = dbus_message_new_signal(wpa_s->dbus_new_path,
				      WPAS_DBUS_NEW_IFACE_P2PDEVICE,
				      sig_name);
	if (msg == NULL)
		return;

	dbus_message_iter_init_append(msg, &iter);
	path = pgrp_obj_path;
	if (!dbus_message_iter_append_basic(&iter, DBUS_TYPE_OBJECT_PATH,
					    &path) ||
	    (properties &&
	     !wpa_dbus_get_object_properties(
		     iface, pgrp_obj_path,
		     WPAS_DBUS_NEW_IFACE_PERSISTENT_GROUP, &iter)))
		wpa_printf(MSG_ERROR, "dbus: Failed to construct signal");
	else
		dbus_connection_send(iface->con, msg, NULL);

	dbus_message_unref(msg);
}


/**
 * wpas_dbus_signal_persistent_group_added - Send a persistent_group
 *	added signal
 * @wpa_s: %wpa_supplicant network interface data
 * @id: new persistent group id
 *
 * Notify listeners about addition of a new persistent group.
 */
static void wpas_dbus_signal_persistent_group_added(
	struct wpa_supplicant *wpa_s, int id)
{
	wpas_dbus_signal_persistent_group(wpa_s, id, "PersistentGroupAdded",
					  TRUE);
}


/**
 * wpas_dbus_signal_persistent_group_removed - Send a persistent_group
 *	removed signal
 * @wpa_s: %wpa_supplicant network interface data
 * @id: persistent group id
 *
 * Notify listeners about removal of a persistent group.
 */
static void wpas_dbus_signal_persistent_group_removed(
	struct wpa_supplicant *wpa_s, int id)
{
	wpas_dbus_signal_persistent_group(wpa_s, id, "PersistentGroupRemoved",
					  FALSE);
}


/**
 * wpas_dbus_signal_p2p_wps_failed - Signals WpsFailed event
 * @wpa_s: %wpa_supplicant network interface data
 * @fail: WPS failure information
 *
 * Sends Event dbus signal with name "fail" and dictionary containing
 * "msg" field with fail message number (int32) as arguments
 */
void wpas_dbus_signal_p2p_wps_failed(struct wpa_supplicant *wpa_s,
				     struct wps_event_fail *fail)
{

	DBusMessage *msg;
	DBusMessageIter iter, dict_iter;
	struct wpas_dbus_priv *iface;
	char *key = "fail";

	iface = wpa_s->global->dbus;

	/* Do nothing if the control interface is not turned on */
	if (iface == NULL)
		return;

	if (wpa_s->p2p_mgmt)
		wpa_s = wpa_s->parent;

	if (!wpa_s->dbus_new_path)
		return;
	msg = dbus_message_new_signal(wpa_s->dbus_new_path,
				      WPAS_DBUS_NEW_IFACE_P2PDEVICE,
				      "WpsFailed");
	if (msg == NULL)
		return;

	dbus_message_iter_init_append(msg, &iter);

	if (!dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &key) ||
	    !wpa_dbus_dict_open_write(&iter, &dict_iter) ||
	    !wpa_dbus_dict_append_int32(&dict_iter, "msg", fail->msg) ||
	    !wpa_dbus_dict_append_int16(&dict_iter, "config_error",
					fail->config_error) ||
	    !wpa_dbus_dict_close_write(&iter, &dict_iter))
		wpa_printf(MSG_ERROR, "dbus: Failed to construct signal");
	else
		dbus_connection_send(iface->con, msg, NULL);

	dbus_message_unref(msg);
}


/**
 * wpas_dbus_signal_p2p_group_formation_failure - Signals GroupFormationFailure event
 * @wpa_s: %wpa_supplicant network interface data
 * @reason: indicates the reason code for group formation failure
 *
 * Sends Event dbus signal and string reason code when available.
 */
void wpas_dbus_signal_p2p_group_formation_failure(struct wpa_supplicant *wpa_s,
						  const char *reason)
{
	DBusMessage *msg;
	struct wpas_dbus_priv *iface;

	iface = wpa_s->global->dbus;

	/* Do nothing if the control interface is not turned on */
	if (iface == NULL)
		return;

	if (wpa_s->p2p_mgmt)
		wpa_s = wpa_s->parent;

	msg = dbus_message_new_signal(wpa_s->dbus_new_path,
				      WPAS_DBUS_NEW_IFACE_P2PDEVICE,
				      "GroupFormationFailure");
	if (msg == NULL)
		return;

	if (dbus_message_append_args(msg, DBUS_TYPE_STRING, &reason,
				     DBUS_TYPE_INVALID))
		dbus_connection_send(iface->con, msg, NULL);
	else
		wpa_printf(MSG_ERROR, "dbus: Failed to construct signal");

	dbus_message_unref(msg);
}


/**
 * wpas_dbus_signal_p2p_invitation_received - Emit InvitationReceived signal
 * @wpa_s: %wpa_supplicant network interface data
 * @sa: Source address of the Invitation Request
 * @dev_add: GO Device Address
 * @bssid: P2P Group BSSID or %NULL if not received
 * @id: Persistent group id or %0 if not persistent group
 * @op_freq: Operating frequency for the group
 */

void wpas_dbus_signal_p2p_invitation_received(struct wpa_supplicant *wpa_s,
					      const u8 *sa, const u8 *dev_addr,
					      const u8 *bssid, int id,
					      int op_freq)
{
	DBusMessage *msg;
	DBusMessageIter iter, dict_iter;
	struct wpas_dbus_priv *iface;

	iface = wpa_s->global->dbus;

	/* Do nothing if the control interface is not turned on */
	if (iface == NULL)
		return;

	if (wpa_s->p2p_mgmt)
		wpa_s = wpa_s->parent;

	msg = dbus_message_new_signal(wpa_s->dbus_new_path,
				      WPAS_DBUS_NEW_IFACE_P2PDEVICE,
				      "InvitationReceived");
	if (msg == NULL)
		return;

	dbus_message_iter_init_append(msg, &iter);
	if (!wpa_dbus_dict_open_write(&iter, &dict_iter) ||
	    (sa &&
	     !wpa_dbus_dict_append_byte_array(&dict_iter, "sa",
					      (const char *) sa, ETH_ALEN)) ||
	    (dev_addr &&
	     !wpa_dbus_dict_append_byte_array(&dict_iter, "go_dev_addr",
					      (const char *) dev_addr,
					      ETH_ALEN)) ||
	    (bssid &&
	     !wpa_dbus_dict_append_byte_array(&dict_iter, "bssid",
					      (const char *) bssid,
					      ETH_ALEN)) ||
	    (id &&
	     !wpa_dbus_dict_append_int32(&dict_iter, "persistent_id", id)) ||
	    !wpa_dbus_dict_append_int32(&dict_iter, "op_freq", op_freq) ||
	    !wpa_dbus_dict_close_write(&iter, &dict_iter)) {
		dbus_message_unref(msg);
		return;
	}

	dbus_connection_send(iface->con, msg, NULL);
	dbus_message_unref(msg);
}


#endif /* CONFIG_P2P */


/**
 * wpas_dbus_signal_prop_changed - Signals change of property
 * @wpa_s: %wpa_supplicant network interface data
 * @property: indicates which property has changed
 *
 * Sends PropertyChanged signals with path, interface and arguments
 * depending on which property has changed.
 */
void wpas_dbus_signal_prop_changed(struct wpa_supplicant *wpa_s,
				   enum wpas_dbus_prop property)
{
	char *prop;
	dbus_bool_t flush;

	if (wpa_s->dbus_new_path == NULL)
		return; /* Skip signal since D-Bus setup is not yet ready */

	flush = FALSE;
	switch (property) {
	case WPAS_DBUS_PROP_AP_SCAN:
		prop = "ApScan";
		break;
	case WPAS_DBUS_PROP_SCANNING:
		prop = "Scanning";
		break;
	case WPAS_DBUS_PROP_STATE:
		prop = "State";
		break;
	case WPAS_DBUS_PROP_CURRENT_BSS:
		prop = "CurrentBSS";
		break;
	case WPAS_DBUS_PROP_CURRENT_NETWORK:
		prop = "CurrentNetwork";
		break;
	case WPAS_DBUS_PROP_BSSS:
		prop = "BSSs";
		break;
	case WPAS_DBUS_PROP_CURRENT_AUTH_MODE:
		prop = "CurrentAuthMode";
		break;
	case WPAS_DBUS_PROP_DISCONNECT_REASON:
		prop = "DisconnectReason";
		flush = TRUE;
		break;
	case WPAS_DBUS_PROP_ASSOC_STATUS_CODE:
		prop = "AssocStatusCode";
		flush = TRUE;
		break;
	default:
		wpa_printf(MSG_ERROR, "dbus: %s: Unknown Property value %d",
			   __func__, property);
		return;
	}

	wpa_dbus_mark_property_changed(wpa_s->global->dbus,
				       wpa_s->dbus_new_path,
				       WPAS_DBUS_NEW_IFACE_INTERFACE, prop);
	if (flush) {
		wpa_dbus_flush_object_changed_properties(
			wpa_s->global->dbus->con, wpa_s->dbus_new_path);
	}
}


/**
 * wpas_dbus_bss_signal_prop_changed - Signals change of BSS property
 * @wpa_s: %wpa_supplicant network interface data
 * @property: indicates which property has changed
 * @id: unique BSS identifier
 *
 * Sends PropertyChanged signals with path, interface, and arguments depending
 * on which property has changed.
 */
void wpas_dbus_bss_signal_prop_changed(struct wpa_supplicant *wpa_s,
				       enum wpas_dbus_bss_prop property,
				       unsigned int id)
{
	char path[WPAS_DBUS_OBJECT_PATH_MAX];
	char *prop;

	if (!wpa_s->dbus_new_path)
		return;

	switch (property) {
	case WPAS_DBUS_BSS_PROP_SIGNAL:
		prop = "Signal";
		break;
	case WPAS_DBUS_BSS_PROP_FREQ:
		prop = "Frequency";
		break;
	case WPAS_DBUS_BSS_PROP_MODE:
		prop = "Mode";
		break;
	case WPAS_DBUS_BSS_PROP_PRIVACY:
		prop = "Privacy";
		break;
	case WPAS_DBUS_BSS_PROP_RATES:
		prop = "Rates";
		break;
	case WPAS_DBUS_BSS_PROP_WPA:
		prop = "WPA";
		break;
	case WPAS_DBUS_BSS_PROP_RSN:
		prop = "RSN";
		break;
	case WPAS_DBUS_BSS_PROP_WPS:
		prop = "WPS";
		break;
	case WPAS_DBUS_BSS_PROP_IES:
		prop = "IEs";
		break;
	case WPAS_DBUS_BSS_PROP_AGE:
		prop = "Age";
		break;
	default:
		wpa_printf(MSG_ERROR, "dbus: %s: Unknown Property value %d",
			   __func__, property);
		return;
	}

	os_snprintf(path, WPAS_DBUS_OBJECT_PATH_MAX,
		    "%s/" WPAS_DBUS_NEW_BSSIDS_PART "/%u",
		    wpa_s->dbus_new_path, id);

	wpa_dbus_mark_property_changed(wpa_s->global->dbus, path,
				       WPAS_DBUS_NEW_IFACE_BSS, prop);
}


/**
 * wpas_dbus_signal_debug_level_changed - Signals change of debug param
 * @global: wpa_global structure
 *
 * Sends PropertyChanged signals informing that debug level has changed.
 */
void wpas_dbus_signal_debug_level_changed(struct wpa_global *global)
{
	wpa_dbus_mark_property_changed(global->dbus, WPAS_DBUS_NEW_PATH,
				       WPAS_DBUS_NEW_INTERFACE,
				       "DebugLevel");
}


/**
 * wpas_dbus_signal_debug_timestamp_changed - Signals change of debug param
 * @global: wpa_global structure
 *
 * Sends PropertyChanged signals informing that debug timestamp has changed.
 */
void wpas_dbus_signal_debug_timestamp_changed(struct wpa_global *global)
{
	wpa_dbus_mark_property_changed(global->dbus, WPAS_DBUS_NEW_PATH,
				       WPAS_DBUS_NEW_INTERFACE,
				       "DebugTimestamp");
}


/**
 * wpas_dbus_signal_debug_show_keys_changed - Signals change of debug param
 * @global: wpa_global structure
 *
 * Sends PropertyChanged signals informing that debug show_keys has changed.
 */
void wpas_dbus_signal_debug_show_keys_changed(struct wpa_global *global)
{
	wpa_dbus_mark_property_changed(global->dbus, WPAS_DBUS_NEW_PATH,
				       WPAS_DBUS_NEW_INTERFACE,
				       "DebugShowKeys");
}


static void wpas_dbus_register(struct wpa_dbus_object_desc *obj_desc,
			       void *priv,
			       WPADBusArgumentFreeFunction priv_free,
			       const struct wpa_dbus_method_desc *methods,
			       const struct wpa_dbus_property_desc *properties,
			       const struct wpa_dbus_signal_desc *signals)
{
	int n;

	obj_desc->user_data = priv;
	obj_desc->user_data_free_func = priv_free;
	obj_desc->methods = methods;
	obj_desc->properties = properties;
	obj_desc->signals = signals;

	for (n = 0; properties && properties->dbus_property; properties++)
		n++;

	obj_desc->prop_changed_flags = os_zalloc(n);
	if (!obj_desc->prop_changed_flags)
		wpa_printf(MSG_DEBUG, "dbus: %s: can't register handlers",
			   __func__);
}


static const struct wpa_dbus_method_desc wpas_dbus_global_methods[] = {
	{ "CreateInterface", WPAS_DBUS_NEW_INTERFACE,
	  (WPADBusMethodHandler) wpas_dbus_handler_create_interface,
	  {
		  { "args", "a{sv}", ARG_IN },
		  { "path", "o", ARG_OUT },
		  END_ARGS
	  }
	},
	{ "RemoveInterface", WPAS_DBUS_NEW_INTERFACE,
	  (WPADBusMethodHandler) wpas_dbus_handler_remove_interface,
	  {
		  { "path", "o", ARG_IN },
		  END_ARGS
	  }
	},
	{ "GetInterface", WPAS_DBUS_NEW_INTERFACE,
	  (WPADBusMethodHandler) wpas_dbus_handler_get_interface,
	  {
		  { "ifname", "s", ARG_IN },
		  { "path", "o", ARG_OUT },
		  END_ARGS
	  }
	},
	{ "ExpectDisconnect", WPAS_DBUS_NEW_INTERFACE,
	  (WPADBusMethodHandler) wpas_dbus_handler_expect_disconnect,
	  {
		END_ARGS
	  }
	},
	{ NULL, NULL, NULL, { END_ARGS } }
};

static const struct wpa_dbus_property_desc wpas_dbus_global_properties[] = {
	{ "DebugLevel", WPAS_DBUS_NEW_INTERFACE, "s",
	  wpas_dbus_getter_debug_level,
	  wpas_dbus_setter_debug_level,
	  NULL
	},
	{ "DebugTimestamp", WPAS_DBUS_NEW_INTERFACE, "b",
	  wpas_dbus_getter_debug_timestamp,
	  wpas_dbus_setter_debug_timestamp,
	  NULL
	},
	{ "DebugShowKeys", WPAS_DBUS_NEW_INTERFACE, "b",
	  wpas_dbus_getter_debug_show_keys,
	  wpas_dbus_setter_debug_show_keys,
	  NULL
	},
	{ "Interfaces", WPAS_DBUS_NEW_INTERFACE, "ao",
	  wpas_dbus_getter_interfaces,
	  NULL,
	  NULL
	},
	{ "EapMethods", WPAS_DBUS_NEW_INTERFACE, "as",
	  wpas_dbus_getter_eap_methods,
	  NULL,
	  NULL
	},
	{ "Capabilities", WPAS_DBUS_NEW_INTERFACE, "as",
	  wpas_dbus_getter_global_capabilities,
	  NULL,
	  NULL
	},
#ifdef CONFIG_WIFI_DISPLAY
	{ "WFDIEs", WPAS_DBUS_NEW_INTERFACE, "ay",
	  wpas_dbus_getter_global_wfd_ies,
	  wpas_dbus_setter_global_wfd_ies,
	  NULL
	},
#endif /* CONFIG_WIFI_DISPLAY */
	{ NULL, NULL, NULL, NULL, NULL, NULL }
};

static const struct wpa_dbus_signal_desc wpas_dbus_global_signals[] = {
	{ "InterfaceAdded", WPAS_DBUS_NEW_INTERFACE,
	  {
		  { "path", "o", ARG_OUT },
		  { "properties", "a{sv}", ARG_OUT },
		  END_ARGS
	  }
	},
	{ "InterfaceRemoved", WPAS_DBUS_NEW_INTERFACE,
	  {
		  { "path", "o", ARG_OUT },
		  END_ARGS
	  }
	},
	/* Deprecated: use org.freedesktop.DBus.Properties.PropertiesChanged */
	{ "PropertiesChanged", WPAS_DBUS_NEW_INTERFACE,
	  {
		  { "properties", "a{sv}", ARG_OUT },
		  END_ARGS
	  }
	},
	{ NULL, NULL, { END_ARGS } }
};


static char * uscore_to_dbus(const char *uscore)
{
	const char *p = uscore;
	char *str, *s;
	dbus_bool_t last_was_uscore = TRUE;

	s = str = os_zalloc(os_strlen(uscore) + 1);
	if (!str)
		return NULL;
	while (p && *p) {
		if (*p == '_') {
			last_was_uscore = TRUE;
		} else {
			*s++ = last_was_uscore ? toupper(*p) : *p;
			last_was_uscore = FALSE;
		}
		p++;
	}

	return str;
}


static int wpa_dbus_ctrl_iface_props_init(struct wpas_dbus_priv *priv);


static void wpa_dbus_ctrl_iface_props_deinit(struct wpas_dbus_priv *priv)
{
	int idx = priv->globals_start;

	/* Free all allocated property values */
	while (priv->all_interface_properties[idx].dbus_property)
		os_free((char *)
			priv->all_interface_properties[idx++].dbus_property);
	os_free((char *) priv->all_interface_properties);
}


/**
 * wpas_dbus_ctrl_iface_init - Initialize dbus control interface
 * @global: Pointer to global data from wpa_supplicant_init()
 * Returns: 0 on success or -1 on failure
 *
 * Initialize the dbus control interface for wpa_supplicant and start
 * receiving commands from external programs over the bus.
 */
int wpas_dbus_ctrl_iface_init(struct wpas_dbus_priv *priv)
{
	struct wpa_dbus_object_desc *obj_desc;
	int ret;

	ret = wpa_dbus_ctrl_iface_props_init(priv);
	if (ret < 0) {
		wpa_printf(MSG_ERROR,
			   "dbus: Not enough memory to init interface properties");
		return -1;
	}

	obj_desc = os_zalloc(sizeof(struct wpa_dbus_object_desc));
	if (!obj_desc) {
		wpa_printf(MSG_ERROR,
			   "Not enough memory to create object description");
		goto error;
	}

	wpas_dbus_register(obj_desc, priv->global, NULL,
			   wpas_dbus_global_methods,
			   wpas_dbus_global_properties,
			   wpas_dbus_global_signals);

	wpa_printf(MSG_DEBUG, "dbus: Register D-Bus object '%s'",
		   WPAS_DBUS_NEW_PATH);
	ret = wpa_dbus_ctrl_iface_init(priv, WPAS_DBUS_NEW_PATH,
				       WPAS_DBUS_NEW_SERVICE,
				       obj_desc);
	if (ret < 0) {
		free_dbus_object_desc(obj_desc);
		goto error;
	}

	priv->dbus_new_initialized = 1;
	return 0;

error:
	wpa_dbus_ctrl_iface_props_deinit(priv);
	return -1;
}


/**
 * wpas_dbus_ctrl_iface_deinit - Deinitialize dbus ctrl interface for
 * wpa_supplicant
 * @priv: Pointer to dbus private data from wpas_dbus_init()
 *
 * Deinitialize the dbus control interface that was initialized with
 * wpas_dbus_ctrl_iface_init().
 */
void wpas_dbus_ctrl_iface_deinit(struct wpas_dbus_priv *priv)
{
	if (!priv->dbus_new_initialized)
		return;
	wpa_printf(MSG_DEBUG, "dbus: Unregister D-Bus object '%s'",
		   WPAS_DBUS_NEW_PATH);
	dbus_connection_unregister_object_path(priv->con, WPAS_DBUS_NEW_PATH);
	wpa_dbus_ctrl_iface_props_deinit(priv);
}


static void wpa_dbus_free(void *ptr)
{
	os_free(ptr);
}


static const struct wpa_dbus_property_desc wpas_dbus_network_properties[] = {
	{ "Properties", WPAS_DBUS_NEW_IFACE_NETWORK, "a{sv}",
	  wpas_dbus_getter_network_properties,
	  wpas_dbus_setter_network_properties,
	  NULL
	},
	{ "Enabled", WPAS_DBUS_NEW_IFACE_NETWORK, "b",
	  wpas_dbus_getter_enabled,
	  wpas_dbus_setter_enabled,
	  NULL
	},
	{ NULL, NULL, NULL, NULL, NULL, NULL }
};


static const struct wpa_dbus_signal_desc wpas_dbus_network_signals[] = {
	/* Deprecated: use org.freedesktop.DBus.Properties.PropertiesChanged */
	{ "PropertiesChanged", WPAS_DBUS_NEW_IFACE_NETWORK,
	  {
		  { "properties", "a{sv}", ARG_OUT },
		  END_ARGS
	  }
	},
	{ NULL, NULL, { END_ARGS } }
};


/**
 * wpas_dbus_register_network - Register a configured network with dbus
 * @wpa_s: wpa_supplicant interface structure
 * @ssid: network configuration data
 * Returns: 0 on success, -1 on failure
 *
 * Registers network representing object with dbus
 */
int wpas_dbus_register_network(struct wpa_supplicant *wpa_s,
			       struct wpa_ssid *ssid)
{
	struct wpas_dbus_priv *ctrl_iface;
	struct wpa_dbus_object_desc *obj_desc;
	struct network_handler_args *arg;
	char net_obj_path[WPAS_DBUS_OBJECT_PATH_MAX];

#ifdef CONFIG_P2P
	/*
	 * If it is a persistent group register it as such.
	 * This is to handle cases where an interface is being initialized
	 * with a list of networks read from config.
	 */
	if (network_is_persistent_group(ssid))
		return wpas_dbus_register_persistent_group(wpa_s, ssid);
#endif /* CONFIG_P2P */

	/* Do nothing if the control interface is not turned on */
	if (wpa_s == NULL || wpa_s->global == NULL || !wpa_s->dbus_new_path)
		return 0;
	ctrl_iface = wpa_s->global->dbus;
	if (ctrl_iface == NULL)
		return 0;

	os_snprintf(net_obj_path, WPAS_DBUS_OBJECT_PATH_MAX,
		    "%s/" WPAS_DBUS_NEW_NETWORKS_PART "/%u",
		    wpa_s->dbus_new_path, ssid->id);

	wpa_printf(MSG_DEBUG, "dbus: Register network object '%s'",
		   net_obj_path);
	obj_desc = os_zalloc(sizeof(struct wpa_dbus_object_desc));
	if (!obj_desc) {
		wpa_printf(MSG_ERROR,
			   "Not enough memory to create object description");
		goto err;
	}

	/* allocate memory for handlers arguments */
	arg = os_zalloc(sizeof(struct network_handler_args));
	if (!arg) {
		wpa_printf(MSG_ERROR,
			   "Not enough memory to create arguments for method");
		goto err;
	}

	arg->wpa_s = wpa_s;
	arg->ssid = ssid;

	wpas_dbus_register(obj_desc, arg, wpa_dbus_free, NULL,
			   wpas_dbus_network_properties,
			   wpas_dbus_network_signals);

	if (wpa_dbus_register_object_per_iface(ctrl_iface, net_obj_path,
					       wpa_s->ifname, obj_desc))
		goto err;

	wpas_dbus_signal_network_added(wpa_s, ssid->id);

	return 0;

err:
	free_dbus_object_desc(obj_desc);
	return -1;
}


/**
 * wpas_dbus_unregister_network - Unregister a configured network from dbus
 * @wpa_s: wpa_supplicant interface structure
 * @nid: network id
 * Returns: 0 on success, -1 on failure
 *
 * Unregisters network representing object from dbus
 */
int wpas_dbus_unregister_network(struct wpa_supplicant *wpa_s, int nid)
{
	struct wpas_dbus_priv *ctrl_iface;
	char net_obj_path[WPAS_DBUS_OBJECT_PATH_MAX];
	int ret;
#ifdef CONFIG_P2P
	struct wpa_ssid *ssid;

	ssid = wpa_config_get_network(wpa_s->conf, nid);

	/* If it is a persistent group unregister it as such */
	if (ssid && network_is_persistent_group(ssid))
		return wpas_dbus_unregister_persistent_group(wpa_s, nid);
#endif /* CONFIG_P2P */

	/* Do nothing if the control interface is not turned on */
	if (wpa_s->global == NULL || wpa_s->dbus_new_path == NULL)
		return 0;
	ctrl_iface = wpa_s->global->dbus;
	if (ctrl_iface == NULL)
		return 0;

	os_snprintf(net_obj_path, WPAS_DBUS_OBJECT_PATH_MAX,
		    "%s/" WPAS_DBUS_NEW_NETWORKS_PART "/%u",
		    wpa_s->dbus_new_path, nid);

	wpa_printf(MSG_DEBUG, "dbus: Unregister network object '%s'",
		   net_obj_path);
	ret = wpa_dbus_unregister_object_per_iface(ctrl_iface, net_obj_path);

	if (!ret)
		wpas_dbus_signal_network_removed(wpa_s, nid);

	return ret;
}


static const struct wpa_dbus_property_desc wpas_dbus_bss_properties[] = {
	{ "SSID", WPAS_DBUS_NEW_IFACE_BSS, "ay",
	  wpas_dbus_getter_bss_ssid,
	  NULL,
	  NULL
	},
	{ "BSSID", WPAS_DBUS_NEW_IFACE_BSS, "ay",
	  wpas_dbus_getter_bss_bssid,
	  NULL,
	  NULL
	},
	{ "Privacy", WPAS_DBUS_NEW_IFACE_BSS, "b",
	  wpas_dbus_getter_bss_privacy,
	  NULL,
	  NULL
	},
	{ "Mode", WPAS_DBUS_NEW_IFACE_BSS, "s",
	  wpas_dbus_getter_bss_mode,
	  NULL,
	  NULL
	},
	{ "Signal", WPAS_DBUS_NEW_IFACE_BSS, "n",
	  wpas_dbus_getter_bss_signal,
	  NULL,
	  NULL
	},
	{ "Frequency", WPAS_DBUS_NEW_IFACE_BSS, "q",
	  wpas_dbus_getter_bss_frequency,
	  NULL,
	  NULL
	},
	{ "Rates", WPAS_DBUS_NEW_IFACE_BSS, "au",
	  wpas_dbus_getter_bss_rates,
	  NULL,
	  NULL
	},
	{ "WPA", WPAS_DBUS_NEW_IFACE_BSS, "a{sv}",
	  wpas_dbus_getter_bss_wpa,
	  NULL,
	  NULL
	},
	{ "RSN", WPAS_DBUS_NEW_IFACE_BSS, "a{sv}",
	  wpas_dbus_getter_bss_rsn,
	  NULL,
	  NULL
	},
	{ "WPS", WPAS_DBUS_NEW_IFACE_BSS, "a{sv}",
	  wpas_dbus_getter_bss_wps,
	  NULL,
	  NULL
	},
	{ "IEs", WPAS_DBUS_NEW_IFACE_BSS, "ay",
	  wpas_dbus_getter_bss_ies,
	  NULL,
	  NULL
	},
	{ "Age", WPAS_DBUS_NEW_IFACE_BSS, "u",
	  wpas_dbus_getter_bss_age,
	  NULL,
	  NULL
	},
	{ NULL, NULL, NULL, NULL, NULL, NULL }
};


static const struct wpa_dbus_signal_desc wpas_dbus_bss_signals[] = {
	/* Deprecated: use org.freedesktop.DBus.Properties.PropertiesChanged */
	{ "PropertiesChanged", WPAS_DBUS_NEW_IFACE_BSS,
	  {
		  { "properties", "a{sv}", ARG_OUT },
		  END_ARGS
	  }
	},
	{ NULL, NULL, { END_ARGS } }
};


/**
 * wpas_dbus_unregister_bss - Unregister a scanned BSS from dbus
 * @wpa_s: wpa_supplicant interface structure
 * @bssid: scanned network bssid
 * @id: unique BSS identifier
 * Returns: 0 on success, -1 on failure
 *
 * Unregisters BSS representing object from dbus
 */
int wpas_dbus_unregister_bss(struct wpa_supplicant *wpa_s,
			     u8 bssid[ETH_ALEN], unsigned int id)
{
	struct wpas_dbus_priv *ctrl_iface;
	char bss_obj_path[WPAS_DBUS_OBJECT_PATH_MAX];

	/* Do nothing if the control interface is not turned on */
	if (wpa_s == NULL || wpa_s->global == NULL || !wpa_s->dbus_new_path)
		return 0;
	ctrl_iface = wpa_s->global->dbus;
	if (ctrl_iface == NULL)
		return 0;

	os_snprintf(bss_obj_path, WPAS_DBUS_OBJECT_PATH_MAX,
		    "%s/" WPAS_DBUS_NEW_BSSIDS_PART "/%u",
		    wpa_s->dbus_new_path, id);

	wpa_printf(MSG_DEBUG, "dbus: Unregister BSS object '%s'",
		   bss_obj_path);
	if (wpa_dbus_unregister_object_per_iface(ctrl_iface, bss_obj_path)) {
		wpa_printf(MSG_ERROR, "dbus: Cannot unregister BSS object %s",
			   bss_obj_path);
		return -1;
	}

	wpas_dbus_signal_bss_removed(wpa_s, bss_obj_path);
	wpas_dbus_signal_prop_changed(wpa_s, WPAS_DBUS_PROP_BSSS);

	return 0;
}


/**
 * wpas_dbus_register_bss - Register a scanned BSS with dbus
 * @wpa_s: wpa_supplicant interface structure
 * @bssid: scanned network bssid
 * @id: unique BSS identifier
 * Returns: 0 on success, -1 on failure
 *
 * Registers BSS representing object with dbus
 */
int wpas_dbus_register_bss(struct wpa_supplicant *wpa_s,
			   u8 bssid[ETH_ALEN], unsigned int id)
{
	struct wpas_dbus_priv *ctrl_iface;
	struct wpa_dbus_object_desc *obj_desc;
	char bss_obj_path[WPAS_DBUS_OBJECT_PATH_MAX];
	struct bss_handler_args *arg;

	/* Do nothing if the control interface is not turned on */
	if (wpa_s == NULL || wpa_s->global == NULL || !wpa_s->dbus_new_path)
		return 0;
	ctrl_iface = wpa_s->global->dbus;
	if (ctrl_iface == NULL)
		return 0;

	os_snprintf(bss_obj_path, WPAS_DBUS_OBJECT_PATH_MAX,
		    "%s/" WPAS_DBUS_NEW_BSSIDS_PART "/%u",
		    wpa_s->dbus_new_path, id);

	obj_desc = os_zalloc(sizeof(struct wpa_dbus_object_desc));
	if (!obj_desc) {
		wpa_printf(MSG_ERROR,
			   "Not enough memory to create object description");
		goto err;
	}

	arg = os_zalloc(sizeof(struct bss_handler_args));
	if (!arg) {
		wpa_printf(MSG_ERROR,
			   "Not enough memory to create arguments for handler");
		goto err;
	}
	arg->wpa_s = wpa_s;
	arg->id = id;

	wpas_dbus_register(obj_desc, arg, wpa_dbus_free, NULL,
			   wpas_dbus_bss_properties,
			   wpas_dbus_bss_signals);

	wpa_printf(MSG_DEBUG, "dbus: Register BSS object '%s'",
		   bss_obj_path);
	if (wpa_dbus_register_object_per_iface(ctrl_iface, bss_obj_path,
					       wpa_s->ifname, obj_desc)) {
		wpa_printf(MSG_ERROR,
			   "Cannot register BSSID dbus object %s.",
			   bss_obj_path);
		goto err;
	}

	wpas_dbus_signal_bss_added(wpa_s, bss_obj_path);
	wpas_dbus_signal_prop_changed(wpa_s, WPAS_DBUS_PROP_BSSS);

	return 0;

err:
	free_dbus_object_desc(obj_desc);
	return -1;
}


static const struct wpa_dbus_method_desc wpas_dbus_interface_methods[] = {
	{ "Scan", WPAS_DBUS_NEW_IFACE_INTERFACE,
	  (WPADBusMethodHandler) wpas_dbus_handler_scan,
	  {
		  { "args", "a{sv}", ARG_IN },
		  END_ARGS
	  }
	},
	{ "SignalPoll", WPAS_DBUS_NEW_IFACE_INTERFACE,
	  (WPADBusMethodHandler) wpas_dbus_handler_signal_poll,
	  {
		  { "args", "a{sv}", ARG_OUT },
		  END_ARGS
	  }
	},
	{ "Disconnect", WPAS_DBUS_NEW_IFACE_INTERFACE,
	  (WPADBusMethodHandler) wpas_dbus_handler_disconnect,
	  {
		  END_ARGS
	  }
	},
	{ "AddNetwork", WPAS_DBUS_NEW_IFACE_INTERFACE,
	  (WPADBusMethodHandler) wpas_dbus_handler_add_network,
	  {
		  { "args", "a{sv}", ARG_IN },
		  { "path", "o", ARG_OUT },
		  END_ARGS
	  }
	},
	{ "Reassociate", WPAS_DBUS_NEW_IFACE_INTERFACE,
	  (WPADBusMethodHandler) wpas_dbus_handler_reassociate,
	  {
		  END_ARGS
	  }
	},
	{ "Reattach", WPAS_DBUS_NEW_IFACE_INTERFACE,
	  (WPADBusMethodHandler) wpas_dbus_handler_reattach,
	  {
		  END_ARGS
	  }
	},
	{ "Reconnect", WPAS_DBUS_NEW_IFACE_INTERFACE,
	  (WPADBusMethodHandler) wpas_dbus_handler_reconnect,
	  {
		  END_ARGS
	  }
	},
	{ "RemoveNetwork", WPAS_DBUS_NEW_IFACE_INTERFACE,
	  (WPADBusMethodHandler) wpas_dbus_handler_remove_network,
	  {
		  { "path", "o", ARG_IN },
		  END_ARGS
	  }
	},
	{ "RemoveAllNetworks", WPAS_DBUS_NEW_IFACE_INTERFACE,
	  (WPADBusMethodHandler) wpas_dbus_handler_remove_all_networks,
	  {
		  END_ARGS
	  }
	},
	{ "SelectNetwork", WPAS_DBUS_NEW_IFACE_INTERFACE,
	  (WPADBusMethodHandler) wpas_dbus_handler_select_network,
	  {
		  { "path", "o", ARG_IN },
		  END_ARGS
	  }
	},
	{ "NetworkReply", WPAS_DBUS_NEW_IFACE_INTERFACE,
	  (WPADBusMethodHandler) wpas_dbus_handler_network_reply,
	  {
		  { "path", "o", ARG_IN },
		  { "field", "s", ARG_IN },
		  { "value", "s", ARG_IN },
		  END_ARGS
	  }
	},
#ifndef CONFIG_NO_CONFIG_BLOBS
	{ "AddBlob", WPAS_DBUS_NEW_IFACE_INTERFACE,
	  (WPADBusMethodHandler) wpas_dbus_handler_add_blob,
	  {
		  { "name", "s", ARG_IN },
		  { "data", "ay", ARG_IN },
		  END_ARGS
	  }
	},
	{ "GetBlob", WPAS_DBUS_NEW_IFACE_INTERFACE,
	  (WPADBusMethodHandler) wpas_dbus_handler_get_blob,
	  {
		  { "name", "s", ARG_IN },
		  { "data", "ay", ARG_OUT },
		  END_ARGS
	  }
	},
	{ "RemoveBlob", WPAS_DBUS_NEW_IFACE_INTERFACE,
	  (WPADBusMethodHandler) wpas_dbus_handler_remove_blob,
	  {
		  { "name", "s", ARG_IN },
		  END_ARGS
	  }
	},
#endif /* CONFIG_NO_CONFIG_BLOBS */
	{ "SetPKCS11EngineAndModulePath", WPAS_DBUS_NEW_IFACE_INTERFACE,
	  (WPADBusMethodHandler)
	  wpas_dbus_handler_set_pkcs11_engine_and_module_path,
	  {
		  { "pkcs11_engine_path", "s", ARG_IN },
		  { "pkcs11_module_path", "s", ARG_IN },
		  END_ARGS
	  }
	},
#ifdef CONFIG_WPS
	{ "Start", WPAS_DBUS_NEW_IFACE_WPS,
	  (WPADBusMethodHandler) wpas_dbus_handler_wps_start,
	  {
		  { "args", "a{sv}", ARG_IN },
		  { "output", "a{sv}", ARG_OUT },
		  END_ARGS
	  }
	},
	{ "Cancel", WPAS_DBUS_NEW_IFACE_WPS,
	  (WPADBusMethodHandler) wpas_dbus_handler_wps_cancel,
	  {
		  END_ARGS
	  }
	},
#endif /* CONFIG_WPS */
#ifdef CONFIG_P2P
	{ "Find", WPAS_DBUS_NEW_IFACE_P2PDEVICE,
	  (WPADBusMethodHandler) wpas_dbus_handler_p2p_find,
	  {
		  { "args", "a{sv}", ARG_IN },
		  END_ARGS
	  }
	},
	{ "StopFind", WPAS_DBUS_NEW_IFACE_P2PDEVICE,
	  (WPADBusMethodHandler) wpas_dbus_handler_p2p_stop_find,
	  {
		  END_ARGS
	  }
	},
	{ "Listen", WPAS_DBUS_NEW_IFACE_P2PDEVICE,
	  (WPADBusMethodHandler) wpas_dbus_handler_p2p_listen,
	  {
		  { "timeout", "i", ARG_IN },
		  END_ARGS
	  }
	},
	{ "ExtendedListen", WPAS_DBUS_NEW_IFACE_P2PDEVICE,
	  (WPADBusMethodHandler) wpas_dbus_handler_p2p_extendedlisten,
	  {
		  { "args", "a{sv}", ARG_IN },
		  END_ARGS
	  }
	},
	{ "PresenceRequest", WPAS_DBUS_NEW_IFACE_P2PDEVICE,
	  (WPADBusMethodHandler) wpas_dbus_handler_p2p_presence_request,
	  {
		  { "args", "a{sv}", ARG_IN },
		  END_ARGS
	  }
	},
	{ "ProvisionDiscoveryRequest", WPAS_DBUS_NEW_IFACE_P2PDEVICE,
	  (WPADBusMethodHandler) wpas_dbus_handler_p2p_prov_disc_req,
	  {
		  { "peer", "o", ARG_IN },
		  { "config_method", "s", ARG_IN },
		  END_ARGS
	  }
	},
	{ "Connect", WPAS_DBUS_NEW_IFACE_P2PDEVICE,
	  (WPADBusMethodHandler) wpas_dbus_handler_p2p_connect,
	  {
		  { "args", "a{sv}", ARG_IN },
		  { "generated_pin", "s", ARG_OUT },
		  END_ARGS
	  }
	},
	{ "GroupAdd", WPAS_DBUS_NEW_IFACE_P2PDEVICE,
	  (WPADBusMethodHandler) wpas_dbus_handler_p2p_group_add,
	  {
		  { "args", "a{sv}", ARG_IN },
		  END_ARGS
	  }
	},
	{ "Cancel", WPAS_DBUS_NEW_IFACE_P2PDEVICE,
	  (WPADBusMethodHandler) wpas_dbus_handler_p2p_cancel,
	  {
		  END_ARGS
	  }
	},
	{ "Invite", WPAS_DBUS_NEW_IFACE_P2PDEVICE,
	  (WPADBusMethodHandler) wpas_dbus_handler_p2p_invite,
	  {
		  { "args", "a{sv}", ARG_IN },
		  END_ARGS
	  }
	},
	{ "Disconnect", WPAS_DBUS_NEW_IFACE_P2PDEVICE,
	  (WPADBusMethodHandler) wpas_dbus_handler_p2p_disconnect,
	  {
		  END_ARGS
	  }
	},
	{ "RejectPeer", WPAS_DBUS_NEW_IFACE_P2PDEVICE,
	  (WPADBusMethodHandler) wpas_dbus_handler_p2p_rejectpeer,
	  {
		  { "peer", "o", ARG_IN },
		  END_ARGS
	  }
	},
	{ "RemoveClient", WPAS_DBUS_NEW_IFACE_P2PDEVICE,
	  (WPADBusMethodHandler) wpas_dbus_handler_p2p_remove_client,
	  {
		  { "args", "a{sv}", ARG_IN },
		  END_ARGS
	  }
	},
	{ "Flush", WPAS_DBUS_NEW_IFACE_P2PDEVICE,
	  (WPADBusMethodHandler) wpas_dbus_handler_p2p_flush,
	  {
		  END_ARGS
	  }
	},
	{ "AddService", WPAS_DBUS_NEW_IFACE_P2PDEVICE,
	  (WPADBusMethodHandler) wpas_dbus_handler_p2p_add_service,
	  {
		  { "args", "a{sv}", ARG_IN },
		  END_ARGS
	  }
	},
	{ "DeleteService", WPAS_DBUS_NEW_IFACE_P2PDEVICE,
	  (WPADBusMethodHandler) wpas_dbus_handler_p2p_delete_service,
	  {
		  { "args", "a{sv}", ARG_IN },
		  END_ARGS
	  }
	},
	{ "FlushService", WPAS_DBUS_NEW_IFACE_P2PDEVICE,
	  (WPADBusMethodHandler) wpas_dbus_handler_p2p_flush_service,
	  {
		  END_ARGS
	  }
	},
	{ "ServiceDiscoveryRequest", WPAS_DBUS_NEW_IFACE_P2PDEVICE,
	  (WPADBusMethodHandler) wpas_dbus_handler_p2p_service_sd_req,
	  {
		  { "args", "a{sv}", ARG_IN },
		  { "ref", "t", ARG_OUT },
		  END_ARGS
	  }
	},
	{ "ServiceDiscoveryResponse", WPAS_DBUS_NEW_IFACE_P2PDEVICE,
	  (WPADBusMethodHandler) wpas_dbus_handler_p2p_service_sd_res,
	  {
		  { "args", "a{sv}", ARG_IN },
		  END_ARGS
	  }
	},
	{ "ServiceDiscoveryCancelRequest", WPAS_DBUS_NEW_IFACE_P2PDEVICE,
	  (WPADBusMethodHandler) wpas_dbus_handler_p2p_service_sd_cancel_req,
	  {
		  { "args", "t", ARG_IN },
		  END_ARGS
	  }
	},
	{ "ServiceUpdate", WPAS_DBUS_NEW_IFACE_P2PDEVICE,
	  (WPADBusMethodHandler) wpas_dbus_handler_p2p_service_update,
	  {
		  END_ARGS
	  }
	},
	{ "ServiceDiscoveryExternal", WPAS_DBUS_NEW_IFACE_P2PDEVICE,
	  (WPADBusMethodHandler) wpas_dbus_handler_p2p_serv_disc_external,
	  {
		  { "arg", "i", ARG_IN },
		  END_ARGS
	  }
	},
	{ "AddPersistentGroup", WPAS_DBUS_NEW_IFACE_P2PDEVICE,
	  (WPADBusMethodHandler) wpas_dbus_handler_add_persistent_group,
	  {
		  { "args", "a{sv}", ARG_IN },
		  { "path", "o", ARG_OUT },
		  END_ARGS
	  }
	},
	{ "RemovePersistentGroup", WPAS_DBUS_NEW_IFACE_P2PDEVICE,
	  (WPADBusMethodHandler) wpas_dbus_handler_remove_persistent_group,
	  {
		  { "path", "o", ARG_IN },
		  END_ARGS
	  }
	},
	{ "RemoveAllPersistentGroups", WPAS_DBUS_NEW_IFACE_P2PDEVICE,
	  (WPADBusMethodHandler)
	  wpas_dbus_handler_remove_all_persistent_groups,
	  {
		  END_ARGS
	  }
	},
#endif /* CONFIG_P2P */
	{ "FlushBSS", WPAS_DBUS_NEW_IFACE_INTERFACE,
	  (WPADBusMethodHandler) wpas_dbus_handler_flush_bss,
	  {
		  { "age", "u", ARG_IN },
		  END_ARGS
	  }
	},
#ifdef CONFIG_AP
	{ "SubscribeProbeReq", WPAS_DBUS_NEW_IFACE_INTERFACE,
	  (WPADBusMethodHandler) wpas_dbus_handler_subscribe_preq,
	  {
		  END_ARGS
	  }
	},
	{ "UnsubscribeProbeReq", WPAS_DBUS_NEW_IFACE_INTERFACE,
	  (WPADBusMethodHandler) wpas_dbus_handler_unsubscribe_preq,
	  {
		  END_ARGS
	  }
	},
#endif /* CONFIG_AP */
	{ "EAPLogoff", WPAS_DBUS_NEW_IFACE_INTERFACE,
	  (WPADBusMethodHandler) wpas_dbus_handler_eap_logoff,
	  {
		  END_ARGS
	  }
	},
	{ "EAPLogon", WPAS_DBUS_NEW_IFACE_INTERFACE,
	  (WPADBusMethodHandler) wpas_dbus_handler_eap_logon,
	  {
		  END_ARGS
	  }
	},
#ifdef CONFIG_AUTOSCAN
	{ "AutoScan", WPAS_DBUS_NEW_IFACE_INTERFACE,
	  (WPADBusMethodHandler) wpas_dbus_handler_autoscan,
	  {
		  { "arg", "s", ARG_IN },
		  END_ARGS
	  }
	},
#endif /* CONFIG_AUTOSCAN */
#ifdef CONFIG_TDLS
	{ "TDLSDiscover", WPAS_DBUS_NEW_IFACE_INTERFACE,
	  (WPADBusMethodHandler) wpas_dbus_handler_tdls_discover,
	  {
		  { "peer_address", "s", ARG_IN },
		  END_ARGS
	  }
	},
	{ "TDLSSetup", WPAS_DBUS_NEW_IFACE_INTERFACE,
	  (WPADBusMethodHandler) wpas_dbus_handler_tdls_setup,
	  {
		  { "peer_address", "s", ARG_IN },
		  END_ARGS
	  }
	},
	{ "TDLSStatus", WPAS_DBUS_NEW_IFACE_INTERFACE,
	  (WPADBusMethodHandler) wpas_dbus_handler_tdls_status,
	  {
		  { "peer_address", "s", ARG_IN },
		  { "status", "s", ARG_OUT },
		  END_ARGS
	  }
	},
	{ "TDLSTeardown", WPAS_DBUS_NEW_IFACE_INTERFACE,
	  (WPADBusMethodHandler) wpas_dbus_handler_tdls_teardown,
	  {
		  { "peer_address", "s", ARG_IN },
		  END_ARGS
	  }
	},
	{ "TDLSChannelSwitch", WPAS_DBUS_NEW_IFACE_INTERFACE,
	  (WPADBusMethodHandler) wpas_dbus_handler_tdls_channel_switch,
	  {
		  { "args", "a{sv}", ARG_IN },
		  END_ARGS
	  }
	},
	{ "TDLSCancelChannelSwitch", WPAS_DBUS_NEW_IFACE_INTERFACE,
	  (WPADBusMethodHandler) wpas_dbus_handler_tdls_cancel_channel_switch,
	  {
		  { "peer_address", "s", ARG_IN },
		  END_ARGS
	  }
	},
#endif /* CONFIG_TDLS */
	{ "VendorElemAdd", WPAS_DBUS_NEW_IFACE_INTERFACE,
	  (WPADBusMethodHandler) wpas_dbus_handler_vendor_elem_add,
	  {
		  { "frame_id", "i", ARG_IN },
		  { "ielems", "ay", ARG_IN },
		  END_ARGS
	  }
	},
	{ "VendorElemGet", WPAS_DBUS_NEW_IFACE_INTERFACE,
	  (WPADBusMethodHandler) wpas_dbus_handler_vendor_elem_get,
	  {
		  { "frame_id", "i", ARG_IN },
		  { "ielems", "ay", ARG_OUT },
		  END_ARGS
	  }
	},
	{ "VendorElemRem", WPAS_DBUS_NEW_IFACE_INTERFACE,
	  (WPADBusMethodHandler) wpas_dbus_handler_vendor_elem_remove,
	  {
		  { "frame_id", "i", ARG_IN },
		  { "ielems", "ay", ARG_IN },
		  END_ARGS
	  }
	},
#ifndef CONFIG_NO_CONFIG_WRITE
	{ "SaveConfig", WPAS_DBUS_NEW_IFACE_INTERFACE,
	  (WPADBusMethodHandler) wpas_dbus_handler_save_config,
	  {
		  END_ARGS
	  }
	},
#endif /* CONFIG_NO_CONFIG_WRITE */
	{ "AbortScan", WPAS_DBUS_NEW_IFACE_INTERFACE,
	  (WPADBusMethodHandler) wpas_dbus_handler_abort_scan,
	  {
		  END_ARGS
	  }
	},
	{ NULL, NULL, NULL, { END_ARGS } }
};

static const struct wpa_dbus_property_desc wpas_dbus_interface_properties[] = {
	{ "Capabilities", WPAS_DBUS_NEW_IFACE_INTERFACE, "a{sv}",
	  wpas_dbus_getter_capabilities,
	  NULL,
	  NULL
	},
	{ "State", WPAS_DBUS_NEW_IFACE_INTERFACE, "s",
	  wpas_dbus_getter_state,
	  NULL,
	  NULL
	},
	{ "Scanning", WPAS_DBUS_NEW_IFACE_INTERFACE, "b",
	  wpas_dbus_getter_scanning,
	  NULL,
	  NULL
	},
	{ "ApScan", WPAS_DBUS_NEW_IFACE_INTERFACE, "u",
	  wpas_dbus_getter_ap_scan,
	  wpas_dbus_setter_ap_scan,
	  NULL
	},
	{ "BSSExpireAge", WPAS_DBUS_NEW_IFACE_INTERFACE, "u",
	  wpas_dbus_getter_bss_expire_age,
	  wpas_dbus_setter_bss_expire_age,
	  NULL
	},
	{ "BSSExpireCount", WPAS_DBUS_NEW_IFACE_INTERFACE, "u",
	  wpas_dbus_getter_bss_expire_count,
	  wpas_dbus_setter_bss_expire_count,
	  NULL
	},
	{ "Country", WPAS_DBUS_NEW_IFACE_INTERFACE, "s",
	  wpas_dbus_getter_country,
	  wpas_dbus_setter_country,
	  NULL
	},
	{ "Ifname", WPAS_DBUS_NEW_IFACE_INTERFACE, "s",
	  wpas_dbus_getter_ifname,
	  NULL,
	  NULL
	},
	{ "Driver", WPAS_DBUS_NEW_IFACE_INTERFACE, "s",
	  wpas_dbus_getter_driver,
	  NULL,
	  NULL
	},
	{ "BridgeIfname", WPAS_DBUS_NEW_IFACE_INTERFACE, "s",
	  wpas_dbus_getter_bridge_ifname,
	  NULL,
	  NULL
	},
	{ "ConfigFile", WPAS_DBUS_NEW_IFACE_INTERFACE, "s",
	  wpas_dbus_getter_config_file,
	  NULL,
	  NULL
	},
	{ "CurrentBSS", WPAS_DBUS_NEW_IFACE_INTERFACE, "o",
	  wpas_dbus_getter_current_bss,
	  NULL,
	  NULL
	},
	{ "CurrentNetwork", WPAS_DBUS_NEW_IFACE_INTERFACE, "o",
	  wpas_dbus_getter_current_network,
	  NULL,
	  NULL
	},
	{ "CurrentAuthMode", WPAS_DBUS_NEW_IFACE_INTERFACE, "s",
	  wpas_dbus_getter_current_auth_mode,
	  NULL,
	  NULL
	},
	{ "Blobs", WPAS_DBUS_NEW_IFACE_INTERFACE, "a{say}",
	  wpas_dbus_getter_blobs,
	  NULL,
	  NULL
	},
	{ "BSSs", WPAS_DBUS_NEW_IFACE_INTERFACE, "ao",
	  wpas_dbus_getter_bsss,
	  NULL,
	  NULL
	},
	{ "Networks", WPAS_DBUS_NEW_IFACE_INTERFACE, "ao",
	  wpas_dbus_getter_networks,
	  NULL,
	  NULL
	},
	{ "FastReauth", WPAS_DBUS_NEW_IFACE_INTERFACE, "b",
	  wpas_dbus_getter_fast_reauth,
	  wpas_dbus_setter_fast_reauth,
	  NULL
	},
	{ "ScanInterval", WPAS_DBUS_NEW_IFACE_INTERFACE, "i",
	  wpas_dbus_getter_scan_interval,
	  wpas_dbus_setter_scan_interval,
	  NULL
	},
	{ "PKCS11EnginePath", WPAS_DBUS_NEW_IFACE_INTERFACE, "s",
	  wpas_dbus_getter_pkcs11_engine_path,
	  NULL,
	  NULL
	},
	{ "PKCS11ModulePath", WPAS_DBUS_NEW_IFACE_INTERFACE, "s",
	  wpas_dbus_getter_pkcs11_module_path,
	  NULL,
	  NULL
	},
#ifdef CONFIG_WPS
	{ "ProcessCredentials", WPAS_DBUS_NEW_IFACE_WPS, "b",
	  wpas_dbus_getter_process_credentials,
	  wpas_dbus_setter_process_credentials,
	  NULL
	},
	{ "ConfigMethods", WPAS_DBUS_NEW_IFACE_WPS, "s",
	  wpas_dbus_getter_config_methods,
	  wpas_dbus_setter_config_methods,
	  NULL
	},
	{
	  "DeviceName", WPAS_DBUS_NEW_IFACE_WPS, "s",
	  wpas_dbus_getter_wps_device_name,
	  wpas_dbus_setter_wps_device_name,
	  NULL
	},
	{
	  "Manufacturer", WPAS_DBUS_NEW_IFACE_WPS, "s",
	  wpas_dbus_getter_wps_manufacturer,
	  wpas_dbus_setter_wps_manufacturer,
	  NULL
	},
	{
	  "ModelName", WPAS_DBUS_NEW_IFACE_WPS, "s",
	  wpas_dbus_getter_wps_device_model_name,
	  wpas_dbus_setter_wps_device_model_name,
	  NULL
	},
	{
	  "ModelNumber", WPAS_DBUS_NEW_IFACE_WPS, "s",
	  wpas_dbus_getter_wps_device_model_number,
	  wpas_dbus_setter_wps_device_model_number,
	  NULL
	},
	{
	  "SerialNumber", WPAS_DBUS_NEW_IFACE_WPS, "s",
	  wpas_dbus_getter_wps_device_serial_number,
	  wpas_dbus_setter_wps_device_serial_number,
	  NULL
	},
	{
	  "DeviceType", WPAS_DBUS_NEW_IFACE_WPS, "ay",
	  wpas_dbus_getter_wps_device_device_type,
	  wpas_dbus_setter_wps_device_device_type,
	  NULL
	},
#endif /* CONFIG_WPS */
#ifdef CONFIG_P2P
	{ "P2PDeviceConfig", WPAS_DBUS_NEW_IFACE_P2PDEVICE, "a{sv}",
	  wpas_dbus_getter_p2p_device_config,
	  wpas_dbus_setter_p2p_device_config,
	  NULL
	},
	{ "Peers", WPAS_DBUS_NEW_IFACE_P2PDEVICE, "ao",
	  wpas_dbus_getter_p2p_peers,
	  NULL,
	  NULL
	},
	{ "Role", WPAS_DBUS_NEW_IFACE_P2PDEVICE, "s",
	  wpas_dbus_getter_p2p_role,
	  NULL,
	  NULL
	},
	{ "Group", WPAS_DBUS_NEW_IFACE_P2PDEVICE, "o",
	  wpas_dbus_getter_p2p_group,
	  NULL,
	  NULL
	},
	{ "PeerGO", WPAS_DBUS_NEW_IFACE_P2PDEVICE, "o",
	  wpas_dbus_getter_p2p_peergo,
	  NULL,
	  NULL
	},
	{ "PersistentGroups", WPAS_DBUS_NEW_IFACE_P2PDEVICE, "ao",
	  wpas_dbus_getter_persistent_groups,
	  NULL,
	  NULL
	},
#endif /* CONFIG_P2P */
	{ "DisconnectReason", WPAS_DBUS_NEW_IFACE_INTERFACE, "i",
	  wpas_dbus_getter_disconnect_reason,
	  NULL,
	  NULL
	},
	{ "AssocStatusCode", WPAS_DBUS_NEW_IFACE_INTERFACE, "i",
	  wpas_dbus_getter_assoc_status_code,
	  NULL,
	  NULL
	},
#ifdef CONFIG_MESH
	{ "MeshPeers", WPAS_DBUS_NEW_IFACE_MESH, "aay",
	  wpas_dbus_getter_mesh_peers,
	  NULL,
	  NULL
	},
	{ "MeshGroup", WPAS_DBUS_NEW_IFACE_MESH, "ay",
	  wpas_dbus_getter_mesh_group,
	  NULL,
	  NULL
	},
#endif /* CONFIG_MESH */
	{ NULL, NULL, NULL, NULL, NULL, NULL }
};

static const struct wpa_dbus_signal_desc wpas_dbus_interface_signals[] = {
	{ "ScanDone", WPAS_DBUS_NEW_IFACE_INTERFACE,
	  {
		  { "success", "b", ARG_OUT },
		  END_ARGS
	  }
	},
	{ "BSSAdded", WPAS_DBUS_NEW_IFACE_INTERFACE,
	  {
		  { "path", "o", ARG_OUT },
		  { "properties", "a{sv}", ARG_OUT },
		  END_ARGS
	  }
	},
	{ "BSSRemoved", WPAS_DBUS_NEW_IFACE_INTERFACE,
	  {
		  { "path", "o", ARG_OUT },
		  END_ARGS
	  }
	},
	{ "BlobAdded", WPAS_DBUS_NEW_IFACE_INTERFACE,
	  {
		  { "name", "s", ARG_OUT },
		  END_ARGS
	  }
	},
	{ "BlobRemoved", WPAS_DBUS_NEW_IFACE_INTERFACE,
	  {
		  { "name", "s", ARG_OUT },
		  END_ARGS
	  }
	},
	{ "NetworkAdded", WPAS_DBUS_NEW_IFACE_INTERFACE,
	  {
		  { "path", "o", ARG_OUT },
		  { "properties", "a{sv}", ARG_OUT },
		  END_ARGS
	  }
	},
	{ "NetworkRemoved", WPAS_DBUS_NEW_IFACE_INTERFACE,
	  {
		  { "path", "o", ARG_OUT },
		  END_ARGS
	  }
	},
	{ "NetworkSelected", WPAS_DBUS_NEW_IFACE_INTERFACE,
	  {
		  { "path", "o", ARG_OUT },
		  END_ARGS
	  }
	},
	/* Deprecated: use org.freedesktop.DBus.Properties.PropertiesChanged */
	{ "PropertiesChanged", WPAS_DBUS_NEW_IFACE_INTERFACE,
	  {
		  { "properties", "a{sv}", ARG_OUT },
		  END_ARGS
	  }
	},
#ifdef CONFIG_WPS
	{ "Event", WPAS_DBUS_NEW_IFACE_WPS,
	  {
		  { "name", "s", ARG_OUT },
		  { "args", "a{sv}", ARG_OUT },
		  END_ARGS
	  }
	},
	{ "Credentials", WPAS_DBUS_NEW_IFACE_WPS,
	  {
		  { "credentials", "a{sv}", ARG_OUT },
		  END_ARGS
	  }
	},
	/* Deprecated: use org.freedesktop.DBus.Properties.PropertiesChanged */
	{ "PropertiesChanged", WPAS_DBUS_NEW_IFACE_WPS,
	  {
		  { "properties", "a{sv}", ARG_OUT },
		  END_ARGS
	  }
	},
#endif /* CONFIG_WPS */
#ifdef CONFIG_P2P
	{ "DeviceFound", WPAS_DBUS_NEW_IFACE_P2PDEVICE,
	  {
		  { "path", "o", ARG_OUT },
		  END_ARGS
	  }
	},
	{ "DeviceFoundProperties", WPAS_DBUS_NEW_IFACE_P2PDEVICE,
	  {
		  { "path", "o", ARG_OUT },
		  { "properties", "a{sv}", ARG_OUT },
		  END_ARGS
	  }
	},
	{ "DeviceLost", WPAS_DBUS_NEW_IFACE_P2PDEVICE,
	  {
		  { "path", "o", ARG_OUT },
		  END_ARGS
	  }
	},
	{ "FindStopped", WPAS_DBUS_NEW_IFACE_P2PDEVICE,
	  {
		  END_ARGS
	  }
	},
	{ "ProvisionDiscoveryRequestDisplayPin", WPAS_DBUS_NEW_IFACE_P2PDEVICE,
	  {
		  { "peer_object", "o", ARG_OUT },
		  { "pin", "s", ARG_OUT },
		  END_ARGS
	  }
	},
	{ "ProvisionDiscoveryResponseDisplayPin", WPAS_DBUS_NEW_IFACE_P2PDEVICE,
	  {
		  { "peer_object", "o", ARG_OUT },
		  { "pin", "s", ARG_OUT },
		  END_ARGS
	  }
	},
	{ "ProvisionDiscoveryRequestEnterPin", WPAS_DBUS_NEW_IFACE_P2PDEVICE,
	  {
		  { "peer_object", "o", ARG_OUT },
		  END_ARGS
	  }
	},
	{ "ProvisionDiscoveryResponseEnterPin", WPAS_DBUS_NEW_IFACE_P2PDEVICE,
	  {
		  { "peer_object", "o", ARG_OUT },
		  END_ARGS
	  }
	},
	{ "ProvisionDiscoveryPBCRequest", WPAS_DBUS_NEW_IFACE_P2PDEVICE,
	  {
		  { "peer_object", "o", ARG_OUT },
		  END_ARGS
	  }
	},
	{ "ProvisionDiscoveryPBCResponse", WPAS_DBUS_NEW_IFACE_P2PDEVICE,
	  {
		  { "peer_object", "o", ARG_OUT },
		  END_ARGS
	  }
	},
	{ "ProvisionDiscoveryFailure", WPAS_DBUS_NEW_IFACE_P2PDEVICE,
	  {
		  { "peer_object", "o", ARG_OUT },
		  { "status", "i", ARG_OUT },
		  END_ARGS
	  }
	},
	{ "GroupStarted", WPAS_DBUS_NEW_IFACE_P2PDEVICE,
	  {
		  { "properties", "a{sv}", ARG_OUT },
		  END_ARGS
	  }
	},
	{ "GroupFormationFailure", WPAS_DBUS_NEW_IFACE_P2PDEVICE,
	  {
		  { "reason", "s", ARG_OUT },
		  END_ARGS
	  }
	},
	{ "GONegotiationSuccess", WPAS_DBUS_NEW_IFACE_P2PDEVICE,
	  {
		  { "properties", "a{sv}", ARG_OUT },
		  END_ARGS
	  }
	},
	{ "GONegotiationFailure", WPAS_DBUS_NEW_IFACE_P2PDEVICE,
	  {
		  { "properties", "a{sv}", ARG_OUT },
		  END_ARGS
	  }
	},
	{ "GONegotiationRequest", WPAS_DBUS_NEW_IFACE_P2PDEVICE,
	  {
		  { "path", "o", ARG_OUT },
		  { "dev_passwd_id", "q", ARG_OUT },
		  { "device_go_intent", "y", ARG_OUT },
		  END_ARGS
	  }
	},
	{ "InvitationResult", WPAS_DBUS_NEW_IFACE_P2PDEVICE,
	  {
		  { "invite_result", "a{sv}", ARG_OUT },
		  END_ARGS
	  }
	},
	{ "GroupFinished", WPAS_DBUS_NEW_IFACE_P2PDEVICE,
	  {
		  { "properties", "a{sv}", ARG_OUT },
		  END_ARGS
	  }
	},
	{ "ServiceDiscoveryRequest", WPAS_DBUS_NEW_IFACE_P2PDEVICE,
	  {
		  { "sd_request", "a{sv}", ARG_OUT },
		  END_ARGS
	  }
	},
	{ "ServiceDiscoveryResponse", WPAS_DBUS_NEW_IFACE_P2PDEVICE,
	  {
		  { "sd_response", "a{sv}", ARG_OUT },
		  END_ARGS
	  }
	},
	{ "PersistentGroupAdded", WPAS_DBUS_NEW_IFACE_P2PDEVICE,
	  {
		  { "path", "o", ARG_OUT },
		  { "properties", "a{sv}", ARG_OUT },
		  END_ARGS
	  }
	},
	{ "PersistentGroupRemoved", WPAS_DBUS_NEW_IFACE_P2PDEVICE,
	  {
		  { "path", "o", ARG_OUT },
		  END_ARGS
	  }
	},
	{ "WpsFailed", WPAS_DBUS_NEW_IFACE_P2PDEVICE,
	  {
		  { "name", "s", ARG_OUT },
		  { "args", "a{sv}", ARG_OUT },
		  END_ARGS
	  }
	},
	{ "InvitationReceived", WPAS_DBUS_NEW_IFACE_P2PDEVICE,
	  {
		  { "properties", "a{sv}", ARG_OUT },
		  END_ARGS
	  }
	},
#endif /* CONFIG_P2P */
#ifdef CONFIG_AP
	{ "ProbeRequest", WPAS_DBUS_NEW_IFACE_INTERFACE,
	  {
		  { "args", "a{sv}", ARG_OUT },
		  END_ARGS
	  }
	},
#endif /* CONFIG_AP */
	{ "Certification", WPAS_DBUS_NEW_IFACE_INTERFACE,
	  {
		  { "certification", "a{sv}", ARG_OUT },
		  END_ARGS
	  }
	},
	{ "EAP", WPAS_DBUS_NEW_IFACE_INTERFACE,
	  {
		  { "status", "s", ARG_OUT },
		  { "parameter", "s", ARG_OUT },
		  END_ARGS
	  }
	},
	{ "StaAuthorized", WPAS_DBUS_NEW_IFACE_INTERFACE,
	  {
		  { "name", "s", ARG_OUT },
		  END_ARGS
	  }
	},
	{ "StaDeauthorized", WPAS_DBUS_NEW_IFACE_INTERFACE,
	  {
		  { "name", "s", ARG_OUT },
		  END_ARGS
	  }
	},
	{ "NetworkRequest", WPAS_DBUS_NEW_IFACE_INTERFACE,
	  {
		  { "path", "o", ARG_OUT },
		  { "field", "s", ARG_OUT },
		  { "text", "s", ARG_OUT },
		  END_ARGS
	  }
	},
#ifdef CONFIG_MESH
	{ "MeshGroupStarted", WPAS_DBUS_NEW_IFACE_MESH,
	  {
		  { "args", "a{sv}", ARG_OUT },
		  END_ARGS
	  }
	},
	{ "MeshGroupRemoved", WPAS_DBUS_NEW_IFACE_MESH,
	  {
		  { "args", "a{sv}", ARG_OUT },
		  END_ARGS
	  }
	},
	{ "MeshPeerConnected", WPAS_DBUS_NEW_IFACE_MESH,
	  {
		  { "args", "a{sv}", ARG_OUT },
		  END_ARGS
	  }
	},
	{ "MeshPeerDisconnected", WPAS_DBUS_NEW_IFACE_MESH,
	  {
		  { "args", "a{sv}", ARG_OUT },
		  END_ARGS
	  }
	},
#endif /* CONFIG_MESH */
	{ NULL, NULL, { END_ARGS } }
};


static int wpa_dbus_ctrl_iface_props_init(struct wpas_dbus_priv *priv)
{
	size_t all_size;
	unsigned int i, j, count, num_const, num_globals;
	const char *global_name;
	static const char * const ignored_globals[] = {
		"bss_expiration_age", "bss_expiration_scan_count",
		"ap_scan", "country", "fast_reauth",
		"pkcs11_engine_path", "pkcs11_module_path"
	};

	/* wpas_dbus_interface_properties terminates with a NULL element */
	num_const = ARRAY_SIZE(wpas_dbus_interface_properties) - 1;

	num_globals = wpa_config_get_num_global_field_names();
	priv->globals_start = num_const;

	/* allocate enough for all properties + terminating NULL element */
	all_size = (num_globals + num_const + 1) *
		sizeof(wpas_dbus_interface_properties[0]);
	priv->all_interface_properties = os_zalloc(all_size);
	if (!priv->all_interface_properties) {
		wpa_printf(MSG_ERROR,
			   "dbus: Not enough memory for interface properties");
		return -1;
	}

	/* Copy constant interface properties to the start of the array */
	os_memcpy(priv->all_interface_properties,
		  wpas_dbus_interface_properties,
		  sizeof(wpas_dbus_interface_properties));

	/* Dynamically construct interface global properties */
	for (i = 0, count = num_const; i < num_globals; i++) {
		struct wpa_dbus_property_desc *desc;
		int no_var = 0;

		/* ignore globals that are actually just methods */
		global_name = wpa_config_get_global_field_name(i, &no_var);
		if (no_var)
			continue;
		/* Ignore fields already explicitly exposed */
		for (j = 0; j < ARRAY_SIZE(ignored_globals); j++) {
			if (os_strcmp(global_name, ignored_globals[j]) == 0)
				break;
		}
		if (j < ARRAY_SIZE(ignored_globals))
			continue;

		desc = &priv->all_interface_properties[count++];
		desc->dbus_property = uscore_to_dbus(global_name);
		if (!desc->dbus_property) {
			wpa_printf(MSG_ERROR,
				   "dbus: Not enough memory for D-Bus property name");
			goto error;
		}
		desc->dbus_interface = WPAS_DBUS_NEW_IFACE_INTERFACE;
		desc->type = "s";
		desc->getter = wpas_dbus_getter_iface_global;
		desc->setter = wpas_dbus_setter_iface_global;
		desc->data = global_name;
	}

	return 0;

error:
	wpa_dbus_ctrl_iface_props_deinit(priv);
	return -1;
}


/**
 * wpas_dbus_register_interface - Register an interface with D-Bus
 * @wpa_s: wpa_supplicant interface structure
 * Returns: 0 on success, -1 on failure
 */
int wpas_dbus_register_interface(struct wpa_supplicant *wpa_s)
{
	struct wpa_dbus_object_desc *obj_desc = NULL;
	struct wpas_dbus_priv *ctrl_iface = wpa_s->global->dbus;
	int next;

	/* Do nothing if the control interface is not turned on */
	if (ctrl_iface == NULL)
		return 0;

	/* Create and set the interface's object path */
	wpa_s->dbus_new_path = os_zalloc(WPAS_DBUS_OBJECT_PATH_MAX);
	if (wpa_s->dbus_new_path == NULL)
		return -1;
	next = ctrl_iface->next_objid++;
	os_snprintf(wpa_s->dbus_new_path, WPAS_DBUS_OBJECT_PATH_MAX,
		    WPAS_DBUS_NEW_PATH_INTERFACES "/%u",
		    next);

	obj_desc = os_zalloc(sizeof(struct wpa_dbus_object_desc));
	if (!obj_desc) {
		wpa_printf(MSG_ERROR,
			   "Not enough memory to create object description");
		goto err;
	}

	wpas_dbus_register(obj_desc, wpa_s, NULL, wpas_dbus_interface_methods,
			   ctrl_iface->all_interface_properties,
			   wpas_dbus_interface_signals);

	wpa_printf(MSG_DEBUG, "dbus: Register interface object '%s'",
		   wpa_s->dbus_new_path);
	if (wpa_dbus_register_object_per_iface(ctrl_iface,
					       wpa_s->dbus_new_path,
					       wpa_s->ifname, obj_desc))
		goto err;

	wpas_dbus_signal_interface_added(wpa_s);

	return 0;

err:
	os_free(wpa_s->dbus_new_path);
	wpa_s->dbus_new_path = NULL;
	free_dbus_object_desc(obj_desc);
	return -1;
}


/**
 * wpas_dbus_unregister_interface - Unregister the interface from D-Bus
 * @wpa_s: wpa_supplicant interface structure
 * Returns: 0 on success, -1 on failure
 */
int wpas_dbus_unregister_interface(struct wpa_supplicant *wpa_s)
{
	struct wpas_dbus_priv *ctrl_iface;

	/* Do nothing if the control interface is not turned on */
	if (wpa_s == NULL || wpa_s->global == NULL)
		return 0;
	ctrl_iface = wpa_s->global->dbus;
	if (ctrl_iface == NULL || wpa_s->dbus_new_path == NULL)
		return 0;

	wpa_printf(MSG_DEBUG, "dbus: Unregister interface object '%s'",
		   wpa_s->dbus_new_path);

#ifdef CONFIG_AP
	if (wpa_s->preq_notify_peer) {
		wpas_dbus_unsubscribe_noc(ctrl_iface);
		os_free(wpa_s->preq_notify_peer);
		wpa_s->preq_notify_peer = NULL;
	}
#endif /* CONFIG_AP */

	if (wpa_dbus_unregister_object_per_iface(ctrl_iface,
						 wpa_s->dbus_new_path))
		return -1;

	wpas_dbus_signal_interface_removed(wpa_s);

	os_free(wpa_s->dbus_new_path);
	wpa_s->dbus_new_path = NULL;

	return 0;
}

#ifdef CONFIG_P2P

static const struct wpa_dbus_property_desc wpas_dbus_p2p_peer_properties[] = {
	{ "DeviceName", WPAS_DBUS_NEW_IFACE_P2P_PEER, "s",
	  wpas_dbus_getter_p2p_peer_device_name,
	  NULL,
	  NULL
	},
	{ "Manufacturer", WPAS_DBUS_NEW_IFACE_P2P_PEER, "s",
	  wpas_dbus_getter_p2p_peer_manufacturer,
	  NULL,
	  NULL
	},
	{ "ModelName", WPAS_DBUS_NEW_IFACE_P2P_PEER, "s",
	  wpas_dbus_getter_p2p_peer_modelname,
	  NULL,
	  NULL
	},
	{ "ModelNumber", WPAS_DBUS_NEW_IFACE_P2P_PEER, "s",
	  wpas_dbus_getter_p2p_peer_modelnumber,
	  NULL,
	  NULL
	},
	{ "SerialNumber", WPAS_DBUS_NEW_IFACE_P2P_PEER, "s",
	  wpas_dbus_getter_p2p_peer_serialnumber,
	  NULL,
	  NULL
	},
	{ "PrimaryDeviceType", WPAS_DBUS_NEW_IFACE_P2P_PEER, "ay",
	  wpas_dbus_getter_p2p_peer_primary_device_type,
	  NULL,
	  NULL
	},
	{ "config_method", WPAS_DBUS_NEW_IFACE_P2P_PEER, "q",
	  wpas_dbus_getter_p2p_peer_config_method,
	  NULL,
	  NULL
	},
	{ "level", WPAS_DBUS_NEW_IFACE_P2P_PEER, "i",
	  wpas_dbus_getter_p2p_peer_level,
	  NULL,
	  NULL
	},
	{ "devicecapability", WPAS_DBUS_NEW_IFACE_P2P_PEER, "y",
	  wpas_dbus_getter_p2p_peer_device_capability,
	  NULL,
	  NULL
	},
	{ "groupcapability", WPAS_DBUS_NEW_IFACE_P2P_PEER, "y",
	  wpas_dbus_getter_p2p_peer_group_capability,
	  NULL,
	  NULL
	},
	{ "SecondaryDeviceTypes", WPAS_DBUS_NEW_IFACE_P2P_PEER, "aay",
	  wpas_dbus_getter_p2p_peer_secondary_device_types,
	  NULL,
	  NULL
	},
	{ "VendorExtension", WPAS_DBUS_NEW_IFACE_P2P_PEER, "aay",
	  wpas_dbus_getter_p2p_peer_vendor_extension,
	  NULL,
	  NULL
	},
	{ "IEs", WPAS_DBUS_NEW_IFACE_P2P_PEER, "ay",
	  wpas_dbus_getter_p2p_peer_ies,
	  NULL,
	  NULL
	},
	{ "DeviceAddress", WPAS_DBUS_NEW_IFACE_P2P_PEER, "ay",
	  wpas_dbus_getter_p2p_peer_device_address,
	  NULL,
	  NULL
	},
	{ "Groups", WPAS_DBUS_NEW_IFACE_P2P_PEER, "ao",
	  wpas_dbus_getter_p2p_peer_groups,
	  NULL,
	  NULL
	},
	{ NULL, NULL, NULL, NULL, NULL, NULL }
};

static const struct wpa_dbus_signal_desc wpas_dbus_p2p_peer_signals[] = {
	/* Deprecated: use org.freedesktop.DBus.Properties.PropertiesChanged */
	{ "PropertiesChanged", WPAS_DBUS_NEW_IFACE_P2P_PEER,
	  {
		  { "properties", "a{sv}", ARG_OUT },
		  END_ARGS
	  }
	},
	{ NULL, NULL, { END_ARGS } }
};

/**
 * wpas_dbus_signal_peer - Send a peer related event signal
 * @wpa_s: %wpa_supplicant network interface data
 * @dev: peer device object
 * @interface: name of the interface emitting this signal.
 *	In case of peer objects, it would be emitted by either
 *	the "interface object" or by "peer objects"
 * @sig_name: signal name - DeviceFound
 * @properties: Whether to add a second argument with object properties
 *
 * Notify listeners about event related with p2p peer device
 */
static void wpas_dbus_signal_peer(struct wpa_supplicant *wpa_s,
				  const u8 *dev_addr, const char *interface,
				  const char *sig_name, int properties)
{
	struct wpas_dbus_priv *iface;
	DBusMessage *msg;
	DBusMessageIter iter;
	char peer_obj_path[WPAS_DBUS_OBJECT_PATH_MAX], *path;

	if (wpa_s->p2p_mgmt)
		wpa_s = wpa_s->parent;

	iface = wpa_s->global->dbus;

	/* Do nothing if the control interface is not turned on */
	if (iface == NULL || !wpa_s->dbus_new_path)
		return;

	os_snprintf(peer_obj_path, WPAS_DBUS_OBJECT_PATH_MAX,
		    "%s/" WPAS_DBUS_NEW_P2P_PEERS_PART "/" COMPACT_MACSTR,
		    wpa_s->dbus_new_path, MAC2STR(dev_addr));

	msg = dbus_message_new_signal(wpa_s->dbus_new_path, interface,
				      sig_name);
	if (msg == NULL)
		return;

	dbus_message_iter_init_append(msg, &iter);
	path = peer_obj_path;
	if (!dbus_message_iter_append_basic(&iter, DBUS_TYPE_OBJECT_PATH,
					    &path) ||
	    (properties && !wpa_dbus_get_object_properties(
		    iface, peer_obj_path, WPAS_DBUS_NEW_IFACE_P2P_PEER,
		    &iter)))
		wpa_printf(MSG_ERROR, "dbus: Failed to construct signal");
	else
		dbus_connection_send(iface->con, msg, NULL);

	dbus_message_unref(msg);
}


/**
 * wpas_dbus_signal_peer_found - Send a peer found signal
 * @wpa_s: %wpa_supplicant network interface data
 * @dev_addr: Peer P2P Device Address
 *
 * Notify listeners about find a p2p peer device found
 */
void wpas_dbus_signal_peer_device_found(struct wpa_supplicant *wpa_s,
					const u8 *dev_addr)
{
	wpas_dbus_signal_peer(wpa_s, dev_addr,
			      WPAS_DBUS_NEW_IFACE_P2PDEVICE,
			      "DeviceFound", FALSE);

	wpas_dbus_signal_peer(wpa_s, dev_addr,
			      WPAS_DBUS_NEW_IFACE_P2PDEVICE,
			      "DeviceFoundProperties", TRUE);
}

/**
 * wpas_dbus_signal_peer_lost - Send a peer lost signal
 * @wpa_s: %wpa_supplicant network interface data
 * @dev_addr: Peer P2P Device Address
 *
 * Notify listeners about lost a p2p peer device
 */
void wpas_dbus_signal_peer_device_lost(struct wpa_supplicant *wpa_s,
				       const u8 *dev_addr)
{
	wpas_dbus_signal_peer(wpa_s, dev_addr,
			      WPAS_DBUS_NEW_IFACE_P2PDEVICE,
			      "DeviceLost", FALSE);
}

/**
 * wpas_dbus_register_peer - Register a discovered peer object with dbus
 * @wpa_s: wpa_supplicant interface structure
 * @dev_addr: P2P Device Address of the peer
 * Returns: 0 on success, -1 on failure
 *
 * Registers network representing object with dbus
 */
int wpas_dbus_register_peer(struct wpa_supplicant *wpa_s, const u8 *dev_addr)
{
	struct wpas_dbus_priv *ctrl_iface;
	struct wpa_dbus_object_desc *obj_desc;
	struct peer_handler_args *arg;
	char peer_obj_path[WPAS_DBUS_OBJECT_PATH_MAX];

	/* Do nothing if the control interface is not turned on */
	if (wpa_s == NULL || wpa_s->global == NULL)
		return 0;

	ctrl_iface = wpa_s->global->dbus;
	if (ctrl_iface == NULL)
		return 0;

	wpa_s = wpa_s->parent->parent;
	if (!wpa_s->dbus_new_path)
		return 0;

	os_snprintf(peer_obj_path, WPAS_DBUS_OBJECT_PATH_MAX,
		    "%s/" WPAS_DBUS_NEW_P2P_PEERS_PART "/" COMPACT_MACSTR,
		    wpa_s->dbus_new_path, MAC2STR(dev_addr));

	wpa_printf(MSG_INFO, "dbus: Register peer object '%s'",
		   peer_obj_path);
	obj_desc = os_zalloc(sizeof(struct wpa_dbus_object_desc));
	if (!obj_desc) {
		wpa_printf(MSG_ERROR,
			   "Not enough memory to create object description");
		goto err;
	}

	/* allocate memory for handlers arguments */
	arg = os_zalloc(sizeof(struct peer_handler_args));
	if (!arg) {
		wpa_printf(MSG_ERROR,
			   "Not enough memory to create arguments for method");
		goto err;
	}

	arg->wpa_s = wpa_s;
	os_memcpy(arg->p2p_device_addr, dev_addr, ETH_ALEN);

	wpas_dbus_register(obj_desc, arg, wpa_dbus_free,
			   NULL,
			   wpas_dbus_p2p_peer_properties,
			   wpas_dbus_p2p_peer_signals);

	if (wpa_dbus_register_object_per_iface(ctrl_iface, peer_obj_path,
					       wpa_s->ifname, obj_desc))
		goto err;

	return 0;

err:
	free_dbus_object_desc(obj_desc);
	return -1;
}

/**
 * wpas_dbus_unregister_peer - Unregister a peer object with dbus
 * @wpa_s: wpa_supplicant interface structure
 * @dev_addr: p2p device addr
 * Returns: 0 on success, -1 on failure
 *
 * Registers network representing object with dbus
 */
int wpas_dbus_unregister_peer(struct wpa_supplicant *wpa_s,
				  const u8 *dev_addr)
{
	struct wpas_dbus_priv *ctrl_iface;
	char peer_obj_path[WPAS_DBUS_OBJECT_PATH_MAX];
	int ret;

	/* Do nothing if the control interface is not turned on */
	if (wpa_s == NULL || wpa_s->global == NULL)
		return 0;

	wpa_s = wpa_s->parent->parent;
	if (!wpa_s->dbus_new_path)
		return 0;

	ctrl_iface = wpa_s->global->dbus;
	if (ctrl_iface == NULL)
		return 0;

	os_snprintf(peer_obj_path, WPAS_DBUS_OBJECT_PATH_MAX,
		    "%s/" WPAS_DBUS_NEW_P2P_PEERS_PART "/" COMPACT_MACSTR,
		    wpa_s->dbus_new_path, MAC2STR(dev_addr));

	wpa_printf(MSG_INFO, "dbus: Unregister peer object '%s'",
		   peer_obj_path);
	ret = wpa_dbus_unregister_object_per_iface(ctrl_iface, peer_obj_path);

	return ret;
}


/**
 * wpas_dbus_signal_p2p_find_stopped - Send P2P Find stopped signal
 * @wpa_s: %wpa_supplicant network interface data
 *
 * Notify listeners about P2P Find stopped
 */
void wpas_dbus_signal_p2p_find_stopped(struct wpa_supplicant *wpa_s)
{
	struct wpas_dbus_priv *iface;
	DBusMessage *msg;

	iface = wpa_s->global->dbus;

	/* Do nothing if the control interface is not turned on */
	if (iface == NULL)
		return;

	if (wpa_s->p2p_mgmt)
		wpa_s = wpa_s->parent;

	if (!wpa_s->dbus_new_path)
		return;

	msg = dbus_message_new_signal(wpa_s->dbus_new_path,
				      WPAS_DBUS_NEW_IFACE_P2PDEVICE,
				      "FindStopped");
	if (msg == NULL)
		return;

	dbus_connection_send(iface->con, msg, NULL);

	dbus_message_unref(msg);
}


/**
 * wpas_dbus_signal_peer_groups_changed - Send peer group change property signal
 * @wpa_s: %wpa_supplicant network interface data
 * @dev_addr: P2P Device Address
 *
 * Notify listeners about peer Groups property changes.
 */
void wpas_dbus_signal_peer_groups_changed(struct wpa_supplicant *wpa_s,
					  const u8 *dev_addr)
{
	char peer_obj_path[WPAS_DBUS_OBJECT_PATH_MAX];

	if (wpa_s->p2p_mgmt)
		wpa_s = wpa_s->parent;

	if (!wpa_s->dbus_new_path)
		return;
	os_snprintf(peer_obj_path, WPAS_DBUS_OBJECT_PATH_MAX,
		    "%s/" WPAS_DBUS_NEW_P2P_PEERS_PART "/" COMPACT_MACSTR,
		    wpa_s->dbus_new_path, MAC2STR(dev_addr));

	wpa_dbus_mark_property_changed(wpa_s->global->dbus, peer_obj_path,
				       WPAS_DBUS_NEW_IFACE_P2P_PEER, "Groups");
}


static const struct wpa_dbus_property_desc wpas_dbus_p2p_group_properties[] = {
	{ "Members", WPAS_DBUS_NEW_IFACE_P2P_GROUP, "ao",
	  wpas_dbus_getter_p2p_group_members,
	  NULL,
	  NULL
	},
	{ "Group", WPAS_DBUS_NEW_IFACE_P2P_GROUP, "o",
	  wpas_dbus_getter_p2p_group,
	  NULL,
	  NULL
	},
	{ "Role", WPAS_DBUS_NEW_IFACE_P2P_GROUP, "s",
	  wpas_dbus_getter_p2p_role,
	  NULL,
	  NULL
	},
	{ "SSID", WPAS_DBUS_NEW_IFACE_P2P_GROUP, "ay",
	  wpas_dbus_getter_p2p_group_ssid,
	  NULL,
	  NULL
	},
	{ "BSSID", WPAS_DBUS_NEW_IFACE_P2P_GROUP, "ay",
	  wpas_dbus_getter_p2p_group_bssid,
	  NULL,
	  NULL
	},
	{ "Frequency", WPAS_DBUS_NEW_IFACE_P2P_GROUP, "q",
	  wpas_dbus_getter_p2p_group_frequency,
	  NULL,
	  NULL
	},
	{ "Passphrase", WPAS_DBUS_NEW_IFACE_P2P_GROUP, "s",
	  wpas_dbus_getter_p2p_group_passphrase,
	  NULL,
	  NULL
	},
	{ "PSK", WPAS_DBUS_NEW_IFACE_P2P_GROUP, "ay",
	  wpas_dbus_getter_p2p_group_psk,
	  NULL,
	  NULL
	},
	{ "WPSVendorExtensions", WPAS_DBUS_NEW_IFACE_P2P_GROUP, "aay",
	  wpas_dbus_getter_p2p_group_vendor_ext,
	  wpas_dbus_setter_p2p_group_vendor_ext,
	  NULL
	},
	{ NULL, NULL, NULL, NULL, NULL, NULL }
};

static const struct wpa_dbus_signal_desc wpas_dbus_p2p_group_signals[] = {
	{ "PeerJoined", WPAS_DBUS_NEW_IFACE_P2P_GROUP,
	  {
		  { "peer", "o", ARG_OUT },
		  END_ARGS
	  }
	},
	{ "PeerDisconnected", WPAS_DBUS_NEW_IFACE_P2P_GROUP,
	  {
		  { "peer", "o", ARG_OUT },
		  END_ARGS
	  }
	},
	{ NULL, NULL, { END_ARGS } }
};

/**
 * wpas_dbus_register_p2p_group - Register a p2p group object with dbus
 * @wpa_s: wpa_supplicant interface structure
 * @ssid: SSID struct
 * Returns: 0 on success, -1 on failure
 *
 * Registers p2p group representing object with dbus
 */
void wpas_dbus_register_p2p_group(struct wpa_supplicant *wpa_s,
				  struct wpa_ssid *ssid)
{
	struct wpas_dbus_priv *ctrl_iface;
	struct wpa_dbus_object_desc *obj_desc;
	char group_obj_path[WPAS_DBUS_OBJECT_PATH_MAX];

	/* Do nothing if the control interface is not turned on */
	if (wpa_s == NULL || wpa_s->global == NULL)
		return;

	ctrl_iface = wpa_s->global->dbus;
	if (ctrl_iface == NULL)
		return;

	if (wpa_s->dbus_groupobj_path) {
		wpa_printf(MSG_INFO, "%s: Group object '%s' already exists",
			   __func__, wpa_s->dbus_groupobj_path);
		return;
	}

	if (wpas_dbus_get_group_obj_path(wpa_s, ssid, group_obj_path) < 0)
		return;

	wpa_s->dbus_groupobj_path = os_strdup(group_obj_path);
	if (wpa_s->dbus_groupobj_path == NULL)
		return;

	wpa_printf(MSG_INFO, "dbus: Register group object '%s'",
		   group_obj_path);
	obj_desc = os_zalloc(sizeof(struct wpa_dbus_object_desc));
	if (!obj_desc) {
		wpa_printf(MSG_ERROR,
			   "Not enough memory to create object description");
		goto err;
	}

	wpas_dbus_register(obj_desc, wpa_s, NULL, NULL,
			   wpas_dbus_p2p_group_properties,
			   wpas_dbus_p2p_group_signals);

	if (wpa_dbus_register_object_per_iface(ctrl_iface, group_obj_path,
					       wpa_s->ifname, obj_desc))
		goto err;

	return;

err:
	if (wpa_s->dbus_groupobj_path) {
		os_free(wpa_s->dbus_groupobj_path);
		wpa_s->dbus_groupobj_path = NULL;
	}

	free_dbus_object_desc(obj_desc);
}

/**
 * wpas_dbus_unregister_p2p_group - Unregister a p2p group object from dbus
 * @wpa_s: wpa_supplicant interface structure
 * @ssid: network name of the p2p group started
 */
void wpas_dbus_unregister_p2p_group(struct wpa_supplicant *wpa_s,
				    const struct wpa_ssid *ssid)
{
	struct wpas_dbus_priv *ctrl_iface;

	/* Do nothing if the control interface is not turned on */
	if (wpa_s == NULL || wpa_s->global == NULL)
		return;

	if (wpa_s->p2p_mgmt)
		wpa_s = wpa_s->parent;

	ctrl_iface = wpa_s->global->dbus;
	if (ctrl_iface == NULL)
		return;

	if (!wpa_s->dbus_groupobj_path) {
		wpa_printf(MSG_DEBUG,
			   "%s: Group object '%s' already unregistered",
			   __func__, wpa_s->dbus_groupobj_path);
		return;
	}

	peer_groups_changed(wpa_s);

	wpa_printf(MSG_DEBUG, "dbus: Unregister group object '%s'",
		   wpa_s->dbus_groupobj_path);

	wpa_dbus_unregister_object_per_iface(ctrl_iface,
					     wpa_s->dbus_groupobj_path);

	os_free(wpa_s->dbus_groupobj_path);
	wpa_s->dbus_groupobj_path = NULL;
}

static const struct wpa_dbus_property_desc
	wpas_dbus_persistent_group_properties[] = {
	{ "Properties", WPAS_DBUS_NEW_IFACE_PERSISTENT_GROUP, "a{sv}",
	  wpas_dbus_getter_persistent_group_properties,
	  wpas_dbus_setter_persistent_group_properties,
	  NULL
	},
	{ NULL, NULL, NULL, NULL, NULL, NULL }
};

/* No signals intended for persistent group objects */

/**
 * wpas_dbus_register_persistent_group - Register a configured(saved)
 *	persistent group with dbus
 * @wpa_s: wpa_supplicant interface structure
 * @ssid: persistent group (still represented as a network within wpa)
 *	  configuration data
 * Returns: 0 on success, -1 on failure
 *
 * Registers a persistent group representing object with dbus.
 */
int wpas_dbus_register_persistent_group(struct wpa_supplicant *wpa_s,
					struct wpa_ssid *ssid)
{
	struct wpas_dbus_priv *ctrl_iface;
	struct wpa_dbus_object_desc *obj_desc;
	struct network_handler_args *arg;
	char pgrp_obj_path[WPAS_DBUS_OBJECT_PATH_MAX];

	/* Do nothing if the control interface is not turned on */
	if (wpa_s == NULL || wpa_s->global == NULL)
		return 0;
	wpa_s = wpa_s->parent->parent;
	if (!wpa_s->dbus_new_path)
		return 0;

	/* Make sure ssid is a persistent group */
	if (ssid->disabled != 2 && !ssid->p2p_persistent_group)
		return -1; /* should we return w/o complaining? */

	if (wpa_s->p2p_mgmt)
		wpa_s = wpa_s->parent;

	ctrl_iface = wpa_s->global->dbus;
	if (ctrl_iface == NULL)
		return 0;

	/*
	 * Intentionally not coming up with different numbering scheme
	 * for persistent groups.
	 */
	os_snprintf(pgrp_obj_path, WPAS_DBUS_OBJECT_PATH_MAX,
		    "%s/" WPAS_DBUS_NEW_PERSISTENT_GROUPS_PART "/%u",
		    wpa_s->dbus_new_path, ssid->id);

	wpa_printf(MSG_DEBUG, "dbus: Register persistent group object '%s'",
		   pgrp_obj_path);
	obj_desc = os_zalloc(sizeof(struct wpa_dbus_object_desc));
	if (!obj_desc) {
		wpa_printf(MSG_ERROR,
			   "dbus: Not enough memory to create object description");
		goto err;
	}

	/*
	 * Reusing the same context structure as that for networks
	 * since these are represented using same data structure.
	 */
	/* allocate memory for handlers arguments */
	arg = os_zalloc(sizeof(struct network_handler_args));
	if (!arg) {
		wpa_printf(MSG_ERROR,
			   "dbus: Not enough memory to create arguments for method");
		goto err;
	}

	arg->wpa_s = wpa_s;
	arg->ssid = ssid;

	wpas_dbus_register(obj_desc, arg, wpa_dbus_free, NULL,
			   wpas_dbus_persistent_group_properties,
			   NULL);

	if (wpa_dbus_register_object_per_iface(ctrl_iface, pgrp_obj_path,
					       wpa_s->ifname, obj_desc))
		goto err;

	wpas_dbus_signal_persistent_group_added(wpa_s, ssid->id);

	return 0;

err:
	free_dbus_object_desc(obj_desc);
	return -1;
}


/**
 * wpas_dbus_unregister_persistent_group - Unregister a persistent_group
 *	from dbus
 * @wpa_s: wpa_supplicant interface structure
 * @nid: network id
 * Returns: 0 on success, -1 on failure
 *
 * Unregisters persistent group representing object from dbus
 *
 * NOTE: There is a slight issue with the semantics here. While the
 * implementation simply means the persistent group is unloaded from memory,
 * it should not get interpreted as the group is actually being erased/removed
 * from persistent storage as well.
 */
int wpas_dbus_unregister_persistent_group(struct wpa_supplicant *wpa_s,
					  int nid)
{
	struct wpas_dbus_priv *ctrl_iface;
	char pgrp_obj_path[WPAS_DBUS_OBJECT_PATH_MAX];
	int ret;

	/* Do nothing if the control interface is not turned on */
	if (wpa_s == NULL || wpa_s->global == NULL)
		return 0;

	wpa_s = wpa_s->parent->parent;

	ctrl_iface = wpa_s->global->dbus;
	if (ctrl_iface == NULL || !wpa_s->dbus_new_path)
		return 0;

	os_snprintf(pgrp_obj_path, WPAS_DBUS_OBJECT_PATH_MAX,
		    "%s/" WPAS_DBUS_NEW_PERSISTENT_GROUPS_PART "/%u",
		    wpa_s->dbus_new_path, nid);

	wpa_printf(MSG_DEBUG, "dbus: Unregister persistent group object '%s'",
		   pgrp_obj_path);
	ret = wpa_dbus_unregister_object_per_iface(ctrl_iface, pgrp_obj_path);

	if (!ret)
		wpas_dbus_signal_persistent_group_removed(wpa_s, nid);

	return ret;
}

#endif /* CONFIG_P2P */
