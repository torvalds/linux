/*
 * WPA Supplicant / dbus-based control interface
 * Copyright (c) 2006, Dan Williams <dcbw@redhat.com> and Red Hat, Inc.
 * Copyright (c) 2009-2010, Witold Sowa <witold.sowa@gmail.com>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef CTRL_IFACE_DBUS_NEW_H
#define CTRL_IFACE_DBUS_NEW_H

#include "common/defs.h"
#include "p2p/p2p.h"

struct wpa_global;
struct wpa_supplicant;
struct wpa_ssid;
struct wps_event_m2d;
struct wps_event_fail;
struct wps_credential;

enum wpas_dbus_prop {
	WPAS_DBUS_PROP_AP_SCAN,
	WPAS_DBUS_PROP_SCANNING,
	WPAS_DBUS_PROP_STATE,
	WPAS_DBUS_PROP_CURRENT_BSS,
	WPAS_DBUS_PROP_CURRENT_NETWORK,
	WPAS_DBUS_PROP_CURRENT_AUTH_MODE,
	WPAS_DBUS_PROP_BSSS,
	WPAS_DBUS_PROP_DISCONNECT_REASON,
	WPAS_DBUS_PROP_ASSOC_STATUS_CODE,
};

enum wpas_dbus_bss_prop {
	WPAS_DBUS_BSS_PROP_SIGNAL,
	WPAS_DBUS_BSS_PROP_FREQ,
	WPAS_DBUS_BSS_PROP_MODE,
	WPAS_DBUS_BSS_PROP_PRIVACY,
	WPAS_DBUS_BSS_PROP_RATES,
	WPAS_DBUS_BSS_PROP_WPA,
	WPAS_DBUS_BSS_PROP_RSN,
	WPAS_DBUS_BSS_PROP_WPS,
	WPAS_DBUS_BSS_PROP_IES,
	WPAS_DBUS_BSS_PROP_AGE,
};

#define WPAS_DBUS_OBJECT_PATH_MAX 150

#define WPAS_DBUS_NEW_SERVICE		"fi.w1.wpa_supplicant1"
#define WPAS_DBUS_NEW_PATH		"/fi/w1/wpa_supplicant1"
#define WPAS_DBUS_NEW_INTERFACE		"fi.w1.wpa_supplicant1"

#define WPAS_DBUS_NEW_PATH_INTERFACES	WPAS_DBUS_NEW_PATH "/Interfaces"
#define WPAS_DBUS_NEW_IFACE_INTERFACE	WPAS_DBUS_NEW_INTERFACE ".Interface"
#define WPAS_DBUS_NEW_IFACE_WPS WPAS_DBUS_NEW_IFACE_INTERFACE ".WPS"

#define WPAS_DBUS_NEW_NETWORKS_PART "Networks"
#define WPAS_DBUS_NEW_IFACE_NETWORK WPAS_DBUS_NEW_INTERFACE ".Network"

#define WPAS_DBUS_NEW_BSSIDS_PART "BSSs"
#define WPAS_DBUS_NEW_IFACE_BSS	WPAS_DBUS_NEW_INTERFACE ".BSS"

#define WPAS_DBUS_NEW_IFACE_P2PDEVICE	\
		WPAS_DBUS_NEW_IFACE_INTERFACE ".P2PDevice"

#define WPAS_DBUS_NEW_IFACE_MESH WPAS_DBUS_NEW_IFACE_INTERFACE ".Mesh"

/*
 * Groups correspond to P2P groups where this device is a GO (owner)
 */
#define WPAS_DBUS_NEW_P2P_GROUPS_PART	"Groups"
#define	WPAS_DBUS_NEW_IFACE_P2P_GROUP WPAS_DBUS_NEW_INTERFACE ".Group"

/*
 * Different dbus object for persistent groups so they do not get confused
 * with regular (configured) network objects.
 */
#define WPAS_DBUS_NEW_PERSISTENT_GROUPS_PART "PersistentGroups"
#define WPAS_DBUS_NEW_IFACE_PERSISTENT_GROUP \
	WPAS_DBUS_NEW_INTERFACE ".PersistentGroup"

#define WPAS_DBUS_NEW_P2P_PEERS_PART	"Peers"
#define	WPAS_DBUS_NEW_IFACE_P2P_PEER WPAS_DBUS_NEW_INTERFACE ".Peer"

/* Top-level Errors */
#define WPAS_DBUS_ERROR_UNKNOWN_ERROR \
	WPAS_DBUS_NEW_INTERFACE ".UnknownError"
#define WPAS_DBUS_ERROR_INVALID_ARGS \
	WPAS_DBUS_NEW_INTERFACE ".InvalidArgs"

#define WPAS_DBUS_ERROR_IFACE_EXISTS \
	WPAS_DBUS_NEW_INTERFACE ".InterfaceExists"
#define WPAS_DBUS_ERROR_IFACE_DISABLED            \
	WPAS_DBUS_NEW_INTERFACE ".InterfaceDisabled"
#define WPAS_DBUS_ERROR_IFACE_UNKNOWN \
	WPAS_DBUS_NEW_INTERFACE ".InterfaceUnknown"

#define WPAS_DBUS_ERROR_NOT_CONNECTED \
	WPAS_DBUS_NEW_INTERFACE ".NotConnected"
#define WPAS_DBUS_ERROR_NETWORK_UNKNOWN \
	WPAS_DBUS_NEW_INTERFACE ".NetworkUnknown"

#define WPAS_DBUS_ERROR_CONNECT_CHANNEL_UNAVAILABLE \
	WPAS_DBUS_NEW_INTERFACE ".ConnectChannelUnavailable"
#define WPAS_DBUS_ERROR_CONNECT_CHANNEL_UNSUPPORTED \
	WPAS_DBUS_NEW_INTERFACE ".ConnectChannelUnsupported"
#define WPAS_DBUS_ERROR_CONNECT_UNSPECIFIED_ERROR \
	WPAS_DBUS_NEW_INTERFACE ".ConnectUnspecifiedError"

#define WPAS_DBUS_ERROR_BLOB_EXISTS \
	WPAS_DBUS_NEW_INTERFACE ".BlobExists"
#define WPAS_DBUS_ERROR_BLOB_UNKNOWN \
	WPAS_DBUS_NEW_INTERFACE ".BlobUnknown"

#define WPAS_DBUS_ERROR_SUBSCRIPTION_IN_USE \
	WPAS_DBUS_NEW_INTERFACE ".SubscriptionInUse"
#define WPAS_DBUS_ERROR_NO_SUBSCRIPTION \
	WPAS_DBUS_NEW_INTERFACE ".NoSubscription"
#define WPAS_DBUS_ERROR_SUBSCRIPTION_EPERM \
	WPAS_DBUS_NEW_INTERFACE ".SubscriptionNotYou"

/* Interface-level errors */
#define WPAS_DBUS_ERROR_IFACE_SCAN_ERROR \
	WPAS_DBUS_NEW_IFACE_INTERFACE ".ScanError"

void wpas_dbus_subscribe_noc(struct wpas_dbus_priv *priv);
void wpas_dbus_unsubscribe_noc(struct wpas_dbus_priv *priv);


#ifdef CONFIG_CTRL_IFACE_DBUS_NEW

int wpas_dbus_ctrl_iface_init(struct wpas_dbus_priv *priv);
void wpas_dbus_ctrl_iface_deinit(struct wpas_dbus_priv *iface);

int wpas_dbus_register_interface(struct wpa_supplicant *wpa_s);
int wpas_dbus_unregister_interface(struct wpa_supplicant *wpa_s);
void wpas_dbus_signal_prop_changed(struct wpa_supplicant *wpa_s,
				   enum wpas_dbus_prop property);
void wpas_dbus_bss_signal_prop_changed(struct wpa_supplicant *wpa_s,
				       enum wpas_dbus_bss_prop property,
				       unsigned int id);
void wpas_dbus_signal_network_enabled_changed(struct wpa_supplicant *wpa_s,
					      struct wpa_ssid *ssid);
void wpas_dbus_signal_network_selected(struct wpa_supplicant *wpa_s, int id);
void wpas_dbus_signal_network_request(struct wpa_supplicant *wpa_s,
				      struct wpa_ssid *ssid,
				      enum wpa_ctrl_req_type rtype,
				      const char *default_text);
void wpas_dbus_signal_scan_done(struct wpa_supplicant *wpa_s, int success);
void wpas_dbus_signal_wps_cred(struct wpa_supplicant *wpa_s,
			       const struct wps_credential *cred);
void wpas_dbus_signal_wps_event_m2d(struct wpa_supplicant *wpa_s,
				    struct wps_event_m2d *m2d);
void wpas_dbus_signal_wps_event_fail(struct wpa_supplicant *wpa_s,
				     struct wps_event_fail *fail);
void wpas_dbus_signal_wps_event_success(struct wpa_supplicant *wpa_s);
void wpas_dbus_signal_wps_event_pbc_overlap(struct wpa_supplicant *wpa_s);
int wpas_dbus_register_network(struct wpa_supplicant *wpa_s,
			       struct wpa_ssid *ssid);
int wpas_dbus_unregister_network(struct wpa_supplicant *wpa_s, int nid);
int wpas_dbus_unregister_bss(struct wpa_supplicant *wpa_s,
			     u8 bssid[ETH_ALEN], unsigned int id);
int wpas_dbus_register_bss(struct wpa_supplicant *wpa_s,
			   u8 bssid[ETH_ALEN], unsigned int id);
void wpas_dbus_signal_blob_added(struct wpa_supplicant *wpa_s,
				 const char *name);
void wpas_dbus_signal_blob_removed(struct wpa_supplicant *wpa_s,
				   const char *name);
void wpas_dbus_signal_debug_level_changed(struct wpa_global *global);
void wpas_dbus_signal_debug_timestamp_changed(struct wpa_global *global);
void wpas_dbus_signal_debug_show_keys_changed(struct wpa_global *global);

int wpas_dbus_register_peer(struct wpa_supplicant *wpa_s, const u8 *dev_addr);
void wpas_dbus_signal_p2p_find_stopped(struct wpa_supplicant *wpa_s);
void wpas_dbus_signal_peer_device_found(struct wpa_supplicant *wpa_s,
					   const u8 *dev_addr);
int wpas_dbus_unregister_peer(struct wpa_supplicant *wpa_s,
				  const u8 *dev_addr);
void wpas_dbus_signal_peer_device_lost(struct wpa_supplicant *wpa_s,
					   const u8 *dev_addr);
void wpas_dbus_signal_peer_groups_changed(struct wpa_supplicant *wpa_s,
					  const u8 *dev_addr);
void wpas_dbus_signal_p2p_group_removed(struct wpa_supplicant *wpa_s,
					const char *role);
void wpas_dbus_signal_p2p_provision_discovery(struct wpa_supplicant *wpa_s,
					      const u8 *dev_addr, int request,
					      enum p2p_prov_disc_status status,
					      u16 config_methods,
					      unsigned int generated_pin);
void wpas_dbus_signal_p2p_go_neg_req(struct wpa_supplicant *wpa_s,
				     const u8 *src, u16 dev_passwd_id,
				     u8 go_intent);
void wpas_dbus_signal_p2p_group_started(struct wpa_supplicant *wpa_s,
					int client, int persistent,
					const u8 *ip);
void wpas_dbus_signal_p2p_group_formation_failure(struct wpa_supplicant *wpa_s,
						  const char *reason);
void wpas_dbus_register_p2p_group(struct wpa_supplicant *wpa_s,
				  struct wpa_ssid *ssid);
void wpas_dbus_signal_p2p_go_neg_resp(struct wpa_supplicant *wpa_s,
				      struct p2p_go_neg_results *res);
void wpas_dbus_unregister_p2p_group(struct wpa_supplicant *wpa_s,
				    const struct wpa_ssid *ssid);
int wpas_dbus_register_persistent_group(struct wpa_supplicant *wpa_s,
					struct wpa_ssid *ssid);
int wpas_dbus_unregister_persistent_group(struct wpa_supplicant *wpa_s,
					  int nid);
void wpas_dbus_signal_p2p_invitation_result(struct wpa_supplicant *wpa_s,
					    int status, const u8 *bssid);
void wpas_dbus_signal_p2p_peer_disconnected(struct wpa_supplicant *wpa_s,
					    const u8 *member);
void wpas_dbus_signal_p2p_sd_request(struct wpa_supplicant *wpa_s,
				     int freq, const u8 *sa, u8 dialog_token,
				     u16 update_indic, const u8 *tlvs,
				     size_t tlvs_len);
void wpas_dbus_signal_p2p_sd_response(struct wpa_supplicant *wpa_s,
				      const u8 *sa, u16 update_indic,
				      const u8 *tlvs, size_t tlvs_len);
void wpas_dbus_signal_p2p_peer_joined(struct wpa_supplicant *wpa_s,
				const u8 *member);
void wpas_dbus_signal_p2p_wps_failed(struct wpa_supplicant *wpa_s,
				     struct wps_event_fail *fail);
void wpas_dbus_signal_certification(struct wpa_supplicant *wpa_s,
				    int depth, const char *subject,
				    const char *altsubject[],
				    int num_altsubject,
				    const char *cert_hash,
				    const struct wpabuf *cert);
void wpas_dbus_signal_preq(struct wpa_supplicant *wpa_s,
			   const u8 *addr, const u8 *dst, const u8 *bssid,
			   const u8 *ie, size_t ie_len, u32 ssi_signal);
void wpas_dbus_signal_eap_status(struct wpa_supplicant *wpa_s,
				 const char *status, const char *parameter);
void wpas_dbus_signal_sta_authorized(struct wpa_supplicant *wpa_s,
				     const u8 *sta);
void wpas_dbus_signal_sta_deauthorized(struct wpa_supplicant *wpa_s,
				       const u8 *sta);
void wpas_dbus_signal_p2p_invitation_received(struct wpa_supplicant *wpa_s,
					      const u8 *sa, const u8 *dev_addr,
					      const u8 *bssid, int id,
					      int op_freq);
void wpas_dbus_signal_mesh_group_started(struct wpa_supplicant *wpa_s,
					 struct wpa_ssid *ssid);
void wpas_dbus_signal_mesh_group_removed(struct wpa_supplicant *wpa_s,
					 const u8 *meshid, u8 meshid_len,
					 int reason);
void wpas_dbus_signal_mesh_peer_connected(struct wpa_supplicant *wpa_s,
					  const u8 *peer_addr);
void wpas_dbus_signal_mesh_peer_disconnected(struct wpa_supplicant *wpa_s,
					     const u8 *peer_addr, int reason);

#else /* CONFIG_CTRL_IFACE_DBUS_NEW */

static inline int wpas_dbus_register_interface(struct wpa_supplicant *wpa_s)
{
	return 0;
}

static inline int wpas_dbus_unregister_interface(struct wpa_supplicant *wpa_s)
{
	return 0;
}

#define wpas_dbus_signal_state_changed(w, n, o) do { } while (0)

static inline void wpas_dbus_signal_prop_changed(struct wpa_supplicant *wpa_s,
						 enum wpas_dbus_prop property)
{
}

static inline void wpas_dbus_bss_signal_prop_changed(
	struct wpa_supplicant *wpa_s, enum wpas_dbus_bss_prop property,
	unsigned int id)
{
}

static inline void wpas_dbus_signal_network_enabled_changed(
	struct wpa_supplicant *wpa_s, struct wpa_ssid *ssid)
{
}

static inline void wpas_dbus_signal_network_selected(
	struct wpa_supplicant *wpa_s, int id)
{
}

static inline void wpas_dbus_signal_network_request(
	struct wpa_supplicant *wpa_s, struct wpa_ssid *ssid,
	enum wpa_ctrl_req_type rtype, const char *default_txt)
{
}

static inline void wpas_dbus_signal_scan_done(struct wpa_supplicant *wpa_s,
					      int success)
{
}

static inline void wpas_dbus_signal_wps_cred(struct wpa_supplicant *wpa_s,
					     const struct wps_credential *cred)
{
}

static inline void wpas_dbus_signal_wps_event_m2d(struct wpa_supplicant *wpa_s,
						  struct wps_event_m2d *m2d)
{
}

static inline void wpas_dbus_signal_wps_event_fail(
	struct wpa_supplicant *wpa_s, struct wps_event_fail *fail)
{
}

static inline void wpas_dbus_signal_wps_event_success(
	struct wpa_supplicant *wpa_s)
{
}

static inline void wpas_dbus_signal_wps_event_pbc_overlap(
	struct wpa_supplicant *wpa_s)
{
}

static inline int wpas_dbus_register_network(struct wpa_supplicant *wpa_s,
					     struct wpa_ssid *ssid)
{
	return 0;
}

static inline int wpas_dbus_unregister_network(struct wpa_supplicant *wpa_s,
					       int nid)
{
	return 0;
}

static inline int wpas_dbus_unregister_bss(struct wpa_supplicant *wpa_s,
					   u8 bssid[ETH_ALEN], unsigned int id)
{
	return 0;
}

static inline int wpas_dbus_register_bss(struct wpa_supplicant *wpa_s,
					 u8 bssid[ETH_ALEN], unsigned int id)
{
	return 0;
}

static inline void wpas_dbus_signal_blob_added(struct wpa_supplicant *wpa_s,
					       const char *name)
{
}

static inline void wpas_dbus_signal_blob_removed(struct wpa_supplicant *wpa_s,
						 const char *name)
{
}

static inline void wpas_dbus_signal_debug_level_changed(
	struct wpa_global *global)
{
}

static inline void wpas_dbus_signal_debug_timestamp_changed(
	struct wpa_global *global)
{
}

static inline void wpas_dbus_signal_debug_show_keys_changed(
	struct wpa_global *global)
{
}

static inline int wpas_dbus_register_peer(struct wpa_supplicant *wpa_s,
					  const u8 *dev_addr)
{
	return 0;
}

static inline int wpas_dbus_unregister_peer(struct wpa_supplicant *wpa_s,
					    const u8 *dev_addr)
{
	return 0;
}

static inline void
wpas_dbus_signal_peer_groups_changed(struct wpa_supplicant *wpa_s,
				     const u8 *dev_addr)
{
}

static inline void
wpas_dbus_signal_p2p_group_removed(struct wpa_supplicant *wpa_s,
				   const char *role)
{
}

static inline void
wpas_dbus_signal_p2p_provision_discovery(struct wpa_supplicant *wpa_s,
					 const u8 *dev_addr, int request,
					 enum p2p_prov_disc_status status,
					 u16 config_methods,
					 unsigned int generated_pin)
{
}

static inline void wpas_dbus_signal_p2p_go_neg_req(struct wpa_supplicant *wpa_s,
						   const u8 *src,
						   u16 dev_passwd_id,
						   u8 go_intent)
{
}

static inline void
wpas_dbus_signal_p2p_group_started(struct wpa_supplicant *wpa_s,
				   int client, int persistent,
				   const u8 *ip)
{
}

static inline void
wpas_dbus_signal_p2p_group_formation_failure(struct wpa_supplicant *wpa_s,
					     const char *reason)
{
}

static inline void
wpas_dbus_register_p2p_group(struct wpa_supplicant *wpa_s,
			     struct wpa_ssid *ssid)
{
}

static inline int wpas_dbus_register_persistent_group(
	struct wpa_supplicant *wpa_s, struct wpa_ssid *ssid)
{
	return 0;
}

static inline int wpas_dbus_unregister_persistent_group(
	struct wpa_supplicant *wpa_s, int nid)
{
	return 0;
}

static inline void
wpas_dbus_signal_p2p_go_neg_resp(struct wpa_supplicant *wpa_s,
				 struct p2p_go_neg_results *res)
{
}

static inline void
wpas_dbus_unregister_p2p_group(struct wpa_supplicant *wpa_s,
			       const struct wpa_ssid *ssid)
{
}

static inline void wpas_dbus_signal_p2p_invitation_result(
				struct wpa_supplicant *wpa_s, int status,
				const u8 *bssid)
{
}

static inline void
wpas_dbus_register_p2p_groupmember(struct wpa_supplicant *wpa_s,
				   const u8 *p2p_if_addr)
{
}

static inline void
wpas_dbus_signal_p2p_sd_request(struct wpa_supplicant *wpa_s, int freq,
				const u8 *sa, u8 dialog_token, u16 update_indic,
				const u8 *tlvs, size_t tlvs_len)
{
}

static inline void
wpas_dbus_signal_p2p_sd_response(struct wpa_supplicant *wpa_s,
				 const u8 *sa, u16 update_indic,
				 const u8 *tlvs, size_t tlvs_len)
{
}

static inline void
wpas_dbus_unregister_p2p_groupmember(struct wpa_supplicant *wpa_s,
				     const u8 *p2p_if_addr)
{
}

static inline void
wpas_dbus_signal_p2p_peer_joined(struct wpa_supplicant *wpa_s,
				 const u8 *member)
{
}

static inline void
wpas_dbus_signal_p2p_find_stopped(struct wpa_supplicant *wpa_s)
{
}

static inline void
wpas_dbus_signal_peer_device_found(struct wpa_supplicant *wpa_s,
				   const u8 *dev_addr)
{
}

static inline void
wpas_dbus_signal_peer_device_lost(struct wpa_supplicant *wpa_s,
				  const u8 *dev_addr)
{
}

static inline void
wpas_dbus_signal_p2p_peer_disconnected(struct wpa_supplicant *wpa_s,
				       const u8 *member)
{
}

static inline void
wpas_dbus_signal_p2p_wps_failed(struct wpa_supplicant *wpa_s,
				struct wps_event_fail *fail)
{
}

static inline void wpas_dbus_signal_certification(struct wpa_supplicant *wpa_s,
						  int depth,
						  const char *subject,
						  const char *altsubject[],
						  int num_altsubject,
						  const char *cert_hash,
						  const struct wpabuf *cert)
{
}

static inline void wpas_dbus_signal_preq(struct wpa_supplicant *wpa_s,
					 const u8 *addr, const u8 *dst,
					 const u8 *bssid,
					 const u8 *ie, size_t ie_len,
					 u32 ssi_signal)
{
}

static inline void wpas_dbus_signal_eap_status(struct wpa_supplicant *wpa_s,
					       const char *status,
					       const char *parameter)
{
}

static inline
void wpas_dbus_signal_sta_authorized(struct wpa_supplicant *wpa_s,
				     const u8 *sta)
{
}

static inline
void wpas_dbus_signal_sta_deauthorized(struct wpa_supplicant *wpa_s,
				       const u8 *sta)
{
}

static inline
void wpas_dbus_signal_p2p_invitation_received(struct wpa_supplicant *wpa_s,
					      const u8 *sa, const u8 *dev_addr,
					      const u8 *bssid, int id,
					      int op_freq)
{
}

static inline
void wpas_dbus_signal_mesh_group_started(struct wpa_supplicant *wpa_s,
					 struct wpa_ssid *ssid)
{
}

static inline
void wpas_dbus_signal_mesh_group_removed(struct wpa_supplicant *wpa_s,
					 const u8 *meshid, u8 meshid_len,
					 int reason)
{
}

static inline
void wpas_dbus_signal_mesh_peer_connected(struct wpa_supplicant *wpa_s,
					  const u8 *peer_addr)
{
}

static inline
void wpas_dbus_signal_mesh_peer_disconnected(struct wpa_supplicant *wpa_s,
					     const u8 *peer_addr, int reason)
{
}

#endif /* CONFIG_CTRL_IFACE_DBUS_NEW */

#endif /* CTRL_IFACE_DBUS_H_NEW */
