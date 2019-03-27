/*
 * WPA Supplicant / dbus-based control interface
 * Copyright (c) 2006, Dan Williams <dcbw@redhat.com> and Red Hat, Inc.
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef CTRL_IFACE_DBUS_H
#define CTRL_IFACE_DBUS_H

struct wps_credential;

#ifdef CONFIG_CTRL_IFACE_DBUS

#define WPAS_DBUS_OBJECT_PATH_MAX 150

#define WPAS_DBUS_SERVICE	"fi.epitest.hostap.WPASupplicant"
#define WPAS_DBUS_PATH		"/fi/epitest/hostap/WPASupplicant"
#define WPAS_DBUS_INTERFACE	"fi.epitest.hostap.WPASupplicant"

#define WPAS_DBUS_PATH_INTERFACES	WPAS_DBUS_PATH "/Interfaces"
#define WPAS_DBUS_IFACE_INTERFACE	WPAS_DBUS_INTERFACE ".Interface"

#define WPAS_DBUS_NETWORKS_PART "Networks"
#define WPAS_DBUS_IFACE_NETWORK	WPAS_DBUS_INTERFACE ".Network"

#define WPAS_DBUS_BSSIDS_PART	"BSSIDs"
#define WPAS_DBUS_IFACE_BSSID	WPAS_DBUS_INTERFACE ".BSSID"


/* Errors */
#define WPAS_ERROR_INVALID_NETWORK \
	WPAS_DBUS_IFACE_INTERFACE ".InvalidNetwork"
#define WPAS_ERROR_INVALID_BSSID \
	WPAS_DBUS_IFACE_INTERFACE ".InvalidBSSID"

#define WPAS_ERROR_INVALID_OPTS \
	WPAS_DBUS_INTERFACE ".InvalidOptions"
#define WPAS_ERROR_INVALID_IFACE \
	WPAS_DBUS_INTERFACE ".InvalidInterface"

#define WPAS_ERROR_ADD_ERROR \
	WPAS_DBUS_INTERFACE ".AddError"
#define WPAS_ERROR_EXISTS_ERROR \
	WPAS_DBUS_INTERFACE ".ExistsError"
#define WPAS_ERROR_REMOVE_ERROR \
	WPAS_DBUS_INTERFACE ".RemoveError"

#define WPAS_ERROR_SCAN_ERROR \
	WPAS_DBUS_IFACE_INTERFACE ".ScanError"
#define WPAS_ERROR_ADD_NETWORK_ERROR \
	WPAS_DBUS_IFACE_INTERFACE ".AddNetworkError"
#define WPAS_ERROR_INTERNAL_ERROR \
	WPAS_DBUS_IFACE_INTERFACE ".InternalError"
#define WPAS_ERROR_REMOVE_NETWORK_ERROR \
	WPAS_DBUS_IFACE_INTERFACE ".RemoveNetworkError"

#define WPAS_ERROR_WPS_PBC_ERROR \
	WPAS_DBUS_IFACE_INTERFACE ".WpsPbcError"
#define WPAS_ERROR_WPS_PIN_ERROR \
	WPAS_DBUS_IFACE_INTERFACE ".WpsPinError"
#define WPAS_ERROR_WPS_REG_ERROR \
	WPAS_DBUS_IFACE_INTERFACE ".WpsRegError"

#define WPAS_DBUS_BSSID_FORMAT "%02x%02x%02x%02x%02x%02x"

struct wpa_global;
struct wpa_supplicant;

int wpa_supplicant_dbus_ctrl_iface_init(struct wpas_dbus_priv *iface);
void wpa_supplicant_dbus_notify_scan_results(struct wpa_supplicant *wpa_s);
void wpa_supplicant_dbus_notify_scanning(struct wpa_supplicant *wpa_s);
void wpa_supplicant_dbus_notify_state_change(struct wpa_supplicant *wpa_s,
					     enum wpa_states new_state,
					     enum wpa_states old_state);
void wpa_supplicant_dbus_notify_wps_cred(struct wpa_supplicant *wpa_s,
					 const struct wps_credential *cred);
void wpa_supplicant_dbus_notify_certification(struct wpa_supplicant *wpa_s,
					      int depth, const char *subject,
					      const char *cert_hash,
					      const struct wpabuf *cert);

char * wpas_dbus_decompose_object_path(const char *path, char **network,
				       char **bssid);

int wpas_dbus_register_iface(struct wpa_supplicant *wpa_s);
int wpas_dbus_unregister_iface(struct wpa_supplicant *wpa_s);


/* Methods internal to the dbus control interface */
struct wpa_supplicant * wpa_supplicant_get_iface_by_dbus_path(
	struct wpa_global *global, const char *path);

#else /* CONFIG_CTRL_IFACE_DBUS */

static inline void
wpa_supplicant_dbus_notify_scan_results(struct wpa_supplicant *wpa_s)
{
}

static inline void
wpa_supplicant_dbus_notify_scanning(struct wpa_supplicant *wpa_s)
{
}

static inline void
wpa_supplicant_dbus_notify_state_change(struct wpa_supplicant *wpa_s,
					enum wpa_states new_state,
					enum wpa_states old_state)
{
}

static inline void
wpa_supplicant_dbus_notify_wps_cred(struct wpa_supplicant *wpa_s,
				    const struct wps_credential *cred)
{
}

static inline void
wpa_supplicant_dbus_notify_certification(struct wpa_supplicant *wpa_s,
					      int depth, const char *subject,
					      const char *cert_hash,
					      const struct wpabuf *cert)
{
}

static inline int
wpas_dbus_register_iface(struct wpa_supplicant *wpa_s)
{
	return 0;
}

static inline int
wpas_dbus_unregister_iface(struct wpa_supplicant *wpa_s)
{
	return 0;
}

#endif /* CONFIG_CTRL_IFACE_DBUS */

#endif /* CTRL_IFACE_DBUS_H */
