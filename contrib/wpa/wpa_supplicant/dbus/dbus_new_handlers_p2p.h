/*
 * WPA Supplicant / dbus-based control interface for p2p
 * Copyright (c) 2011-2012, Intel Corporation
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef DBUS_NEW_HANDLERS_P2P_H
#define DBUS_NEW_HANDLERS_P2P_H

struct peer_handler_args {
	struct wpa_supplicant *wpa_s;
	u8 p2p_device_addr[ETH_ALEN];
};

/*
 * P2P Device methods
 */

DBusMessage *wpas_dbus_handler_p2p_find(
	DBusMessage *message, struct wpa_supplicant *wpa_s);

DBusMessage *wpas_dbus_handler_p2p_stop_find(
	DBusMessage *message, struct wpa_supplicant *wpa_s);

DBusMessage *wpas_dbus_handler_p2p_rejectpeer(
	DBusMessage *message, struct wpa_supplicant *wpa_s);

DBusMessage *wpas_dbus_handler_p2p_listen(
	DBusMessage *message, struct wpa_supplicant *wpa_s);

DBusMessage *wpas_dbus_handler_p2p_extendedlisten(
	DBusMessage *message, struct wpa_supplicant *wpa_s);

DBusMessage *wpas_dbus_handler_p2p_presence_request(
	DBusMessage *message, struct wpa_supplicant *wpa_s);

DBusMessage *wpas_dbus_handler_p2p_prov_disc_req(
	DBusMessage *message, struct wpa_supplicant *wpa_s);

DBusMessage *wpas_dbus_handler_p2p_group_add(
	DBusMessage *message, struct wpa_supplicant *wpa_s);

DBusMessage *wpas_dbus_handler_p2p_connect(
		DBusMessage *message,
		struct wpa_supplicant *wpa_s);

DBusMessage * wpas_dbus_handler_p2p_cancel(DBusMessage *message,
					   struct wpa_supplicant *wpa_s);

DBusMessage *wpas_dbus_handler_p2p_invite(
		DBusMessage *message,
		struct wpa_supplicant *wpa_s);

DBusMessage *wpas_dbus_handler_p2p_disconnect(
	DBusMessage *message, struct wpa_supplicant *wpa_s);

DBusMessage * wpas_dbus_handler_p2p_remove_client(
	DBusMessage *message, struct wpa_supplicant *wpa_s);

DBusMessage *wpas_dbus_handler_p2p_flush(
	DBusMessage *message, struct wpa_supplicant *wpa_s);

DBusMessage *wpas_dbus_handler_p2p_add_service(
	DBusMessage *message, struct wpa_supplicant *wpa_s);

DBusMessage *wpas_dbus_handler_p2p_delete_service(
	DBusMessage *message, struct wpa_supplicant *wpa_s);

DBusMessage *wpas_dbus_handler_p2p_flush_service(
	DBusMessage *message, struct wpa_supplicant *wpa_s);

DBusMessage *wpas_dbus_handler_p2p_service_sd_req(
	DBusMessage *message, struct wpa_supplicant *wpa_s);

DBusMessage *wpas_dbus_handler_p2p_service_sd_res(
	DBusMessage *message, struct wpa_supplicant *wpa_s);

DBusMessage *wpas_dbus_handler_p2p_service_sd_cancel_req(
	DBusMessage *message, struct wpa_supplicant *wpa_s);

DBusMessage *wpas_dbus_handler_p2p_service_update(
	DBusMessage *message, struct wpa_supplicant *wpa_s);

DBusMessage *wpas_dbus_handler_p2p_serv_disc_external(
	DBusMessage *message, struct wpa_supplicant *wpa_s);

/*
 * P2P Device property accessor methods.
 */
DECLARE_ACCESSOR(wpas_dbus_setter_p2p_device_config);
DECLARE_ACCESSOR(wpas_dbus_getter_p2p_device_config);
DECLARE_ACCESSOR(wpas_dbus_getter_p2p_peers);
DECLARE_ACCESSOR(wpas_dbus_getter_p2p_role);
DECLARE_ACCESSOR(wpas_dbus_getter_p2p_group);
DECLARE_ACCESSOR(wpas_dbus_getter_p2p_peergo);

/*
 * P2P Peer properties.
 */
DECLARE_ACCESSOR(wpas_dbus_getter_p2p_peer_device_name);
DECLARE_ACCESSOR(wpas_dbus_getter_p2p_peer_manufacturer);
DECLARE_ACCESSOR(wpas_dbus_getter_p2p_peer_modelname);
DECLARE_ACCESSOR(wpas_dbus_getter_p2p_peer_modelnumber);
DECLARE_ACCESSOR(wpas_dbus_getter_p2p_peer_serialnumber);
DECLARE_ACCESSOR(wpas_dbus_getter_p2p_peer_primary_device_type);
DECLARE_ACCESSOR(wpas_dbus_getter_p2p_peer_config_method);
DECLARE_ACCESSOR(wpas_dbus_getter_p2p_peer_level);
DECLARE_ACCESSOR(wpas_dbus_getter_p2p_peer_device_capability);
DECLARE_ACCESSOR(wpas_dbus_getter_p2p_peer_group_capability);
DECLARE_ACCESSOR(wpas_dbus_getter_p2p_peer_secondary_device_types);
DECLARE_ACCESSOR(wpas_dbus_getter_p2p_peer_vendor_extension);
DECLARE_ACCESSOR(wpas_dbus_getter_p2p_peer_ies);
DECLARE_ACCESSOR(wpas_dbus_getter_p2p_peer_device_address);
DECLARE_ACCESSOR(wpas_dbus_getter_p2p_peer_groups);

/*
 * P2P Group properties
 */
DECLARE_ACCESSOR(wpas_dbus_getter_p2p_group_members);
DECLARE_ACCESSOR(wpas_dbus_getter_p2p_group_ssid);
DECLARE_ACCESSOR(wpas_dbus_getter_p2p_group_bssid);
DECLARE_ACCESSOR(wpas_dbus_getter_p2p_group_frequency);
DECLARE_ACCESSOR(wpas_dbus_getter_p2p_group_passphrase);
DECLARE_ACCESSOR(wpas_dbus_getter_p2p_group_psk);
DECLARE_ACCESSOR(wpas_dbus_getter_p2p_group_vendor_ext);
DECLARE_ACCESSOR(wpas_dbus_setter_p2p_group_vendor_ext);

/*
 * P2P Persistent Groups and properties
 */
DECLARE_ACCESSOR(wpas_dbus_getter_persistent_groups);
DECLARE_ACCESSOR(wpas_dbus_getter_persistent_group_properties);
DECLARE_ACCESSOR(wpas_dbus_setter_persistent_group_properties);

DBusMessage * wpas_dbus_handler_add_persistent_group(
	DBusMessage *message, struct wpa_supplicant *wpa_s);

DBusMessage * wpas_dbus_handler_remove_persistent_group(
	DBusMessage *message, struct wpa_supplicant *wpa_s);

DBusMessage * wpas_dbus_handler_remove_all_persistent_groups(
	DBusMessage *message, struct wpa_supplicant *wpa_s);

#ifdef CONFIG_WIFI_DISPLAY
DECLARE_ACCESSOR(wpas_dbus_getter_global_wfd_ies);
DECLARE_ACCESSOR(wpas_dbus_setter_global_wfd_ies);
#endif /* CONFIG_WIFI_DISPLAY */

#endif /* DBUS_NEW_HANDLERS_P2P_H */
