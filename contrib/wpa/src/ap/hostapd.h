/*
 * hostapd / Initialization and configuration
 * Copyright (c) 2002-2014, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef HOSTAPD_H
#define HOSTAPD_H

#include "common/defs.h"
#include "utils/list.h"
#include "ap_config.h"
#include "drivers/driver.h"

#define OCE_STA_CFON_ENABLED(hapd) \
	((hapd->conf->oce & OCE_STA_CFON) && \
	 (hapd->iface->drv_flags & WPA_DRIVER_FLAGS_OCE_STA_CFON))
#define OCE_AP_ENABLED(hapd) \
	((hapd->conf->oce & OCE_AP) && \
	 (hapd->iface->drv_flags & WPA_DRIVER_FLAGS_OCE_AP))

struct wpa_ctrl_dst;
struct radius_server_data;
struct upnp_wps_device_sm;
struct hostapd_data;
struct sta_info;
struct ieee80211_ht_capabilities;
struct full_dynamic_vlan;
enum wps_event;
union wps_event_data;
#ifdef CONFIG_MESH
struct mesh_conf;
#endif /* CONFIG_MESH */

struct hostapd_iface;

struct hapd_interfaces {
	int (*reload_config)(struct hostapd_iface *iface);
	struct hostapd_config * (*config_read_cb)(const char *config_fname);
	int (*ctrl_iface_init)(struct hostapd_data *hapd);
	void (*ctrl_iface_deinit)(struct hostapd_data *hapd);
	int (*for_each_interface)(struct hapd_interfaces *interfaces,
				  int (*cb)(struct hostapd_iface *iface,
					    void *ctx), void *ctx);
	int (*driver_init)(struct hostapd_iface *iface);

	size_t count;
	int global_ctrl_sock;
	struct dl_list global_ctrl_dst;
	char *global_iface_path;
	char *global_iface_name;
#ifndef CONFIG_NATIVE_WINDOWS
	gid_t ctrl_iface_group;
#endif /* CONFIG_NATIVE_WINDOWS */
	struct hostapd_iface **iface;

	size_t terminate_on_error;
#ifndef CONFIG_NO_VLAN
	struct dynamic_iface *vlan_priv;
#endif /* CONFIG_NO_VLAN */
#ifdef CONFIG_ETH_P_OUI
	struct dl_list eth_p_oui; /* OUI Extended EtherType handlers */
#endif /* CONFIG_ETH_P_OUI */
	int eloop_initialized;

#ifdef CONFIG_DPP
	int dpp_init_done;
	struct dl_list dpp_bootstrap; /* struct dpp_bootstrap_info */
	struct dl_list dpp_configurator; /* struct dpp_configurator */
#endif /* CONFIG_DPP */
};

enum hostapd_chan_status {
	HOSTAPD_CHAN_VALID = 0, /* channel is ready */
	HOSTAPD_CHAN_INVALID = 1, /* no usable channel found */
	HOSTAPD_CHAN_ACS = 2, /* ACS work being performed */
};

struct hostapd_probereq_cb {
	int (*cb)(void *ctx, const u8 *sa, const u8 *da, const u8 *bssid,
		  const u8 *ie, size_t ie_len, int ssi_signal);
	void *ctx;
};

#define HOSTAPD_RATE_BASIC 0x00000001

struct hostapd_rate_data {
	int rate; /* rate in 100 kbps */
	int flags; /* HOSTAPD_RATE_ flags */
};

struct hostapd_frame_info {
	unsigned int freq;
	u32 channel;
	u32 datarate;
	int ssi_signal; /* dBm */
};

enum wps_status {
	WPS_STATUS_SUCCESS = 1,
	WPS_STATUS_FAILURE
};

enum pbc_status {
	WPS_PBC_STATUS_DISABLE,
	WPS_PBC_STATUS_ACTIVE,
	WPS_PBC_STATUS_TIMEOUT,
	WPS_PBC_STATUS_OVERLAP
};

struct wps_stat {
	enum wps_status status;
	enum wps_error_indication failure_reason;
	enum pbc_status pbc_status;
	u8 peer_addr[ETH_ALEN];
};

struct hostapd_neighbor_entry {
	struct dl_list list;
	u8 bssid[ETH_ALEN];
	struct wpa_ssid_value ssid;
	struct wpabuf *nr;
	struct wpabuf *lci;
	struct wpabuf *civic;
	/* LCI update time */
	struct os_time lci_date;
	int stationary;
};

/**
 * struct hostapd_data - hostapd per-BSS data structure
 */
struct hostapd_data {
	struct hostapd_iface *iface;
	struct hostapd_config *iconf;
	struct hostapd_bss_config *conf;
	int interface_added; /* virtual interface added for this BSS */
	unsigned int started:1;
	unsigned int disabled:1;
	unsigned int reenable_beacon:1;

	u8 own_addr[ETH_ALEN];

	int num_sta; /* number of entries in sta_list */
	struct sta_info *sta_list; /* STA info list head */
#define STA_HASH_SIZE 256
#define STA_HASH(sta) (sta[5])
	struct sta_info *sta_hash[STA_HASH_SIZE];

	/*
	 * Bitfield for indicating which AIDs are allocated. Only AID values
	 * 1-2007 are used and as such, the bit at index 0 corresponds to AID
	 * 1.
	 */
#define AID_WORDS ((2008 + 31) / 32)
	u32 sta_aid[AID_WORDS];

	const struct wpa_driver_ops *driver;
	void *drv_priv;

	void (*new_assoc_sta_cb)(struct hostapd_data *hapd,
				 struct sta_info *sta, int reassoc);

	void *msg_ctx; /* ctx for wpa_msg() calls */
	void *msg_ctx_parent; /* parent interface ctx for wpa_msg() calls */

	struct radius_client_data *radius;
	u64 acct_session_id;
	struct radius_das_data *radius_das;

	struct iapp_data *iapp;

	struct hostapd_cached_radius_acl *acl_cache;
	struct hostapd_acl_query_data *acl_queries;

	struct wpa_authenticator *wpa_auth;
	struct eapol_authenticator *eapol_auth;

	struct rsn_preauth_interface *preauth_iface;
	struct os_reltime michael_mic_failure;
	int michael_mic_failures;
	int tkip_countermeasures;

	int ctrl_sock;
	struct dl_list ctrl_dst;

	void *ssl_ctx;
	void *eap_sim_db_priv;
	struct radius_server_data *radius_srv;
	struct dl_list erp_keys; /* struct eap_server_erp_key */

	int parameter_set_count;

	/* Time Advertisement */
	u8 time_update_counter;
	struct wpabuf *time_adv;

#ifdef CONFIG_FULL_DYNAMIC_VLAN
	struct full_dynamic_vlan *full_dynamic_vlan;
#endif /* CONFIG_FULL_DYNAMIC_VLAN */

	struct l2_packet_data *l2;

#ifdef CONFIG_IEEE80211R_AP
	struct dl_list l2_queue;
	struct dl_list l2_oui_queue;
	struct eth_p_oui_ctx *oui_pull;
	struct eth_p_oui_ctx *oui_resp;
	struct eth_p_oui_ctx *oui_push;
	struct eth_p_oui_ctx *oui_sreq;
	struct eth_p_oui_ctx *oui_sresp;
#endif /* CONFIG_IEEE80211R_AP */

	struct wps_context *wps;

	int beacon_set_done;
	struct wpabuf *wps_beacon_ie;
	struct wpabuf *wps_probe_resp_ie;
#ifdef CONFIG_WPS
	unsigned int ap_pin_failures;
	unsigned int ap_pin_failures_consecutive;
	struct upnp_wps_device_sm *wps_upnp;
	unsigned int ap_pin_lockout_time;

	struct wps_stat wps_stats;
#endif /* CONFIG_WPS */

	struct hostapd_probereq_cb *probereq_cb;
	size_t num_probereq_cb;

	void (*public_action_cb)(void *ctx, const u8 *buf, size_t len,
				 int freq);
	void *public_action_cb_ctx;
	void (*public_action_cb2)(void *ctx, const u8 *buf, size_t len,
				  int freq);
	void *public_action_cb2_ctx;

	int (*vendor_action_cb)(void *ctx, const u8 *buf, size_t len,
				int freq);
	void *vendor_action_cb_ctx;

	void (*wps_reg_success_cb)(void *ctx, const u8 *mac_addr,
				   const u8 *uuid_e);
	void *wps_reg_success_cb_ctx;

	void (*wps_event_cb)(void *ctx, enum wps_event event,
			     union wps_event_data *data);
	void *wps_event_cb_ctx;

	void (*sta_authorized_cb)(void *ctx, const u8 *mac_addr,
				  int authorized, const u8 *p2p_dev_addr);
	void *sta_authorized_cb_ctx;

	void (*setup_complete_cb)(void *ctx);
	void *setup_complete_cb_ctx;

	void (*new_psk_cb)(void *ctx, const u8 *mac_addr,
			   const u8 *p2p_dev_addr, const u8 *psk,
			   size_t psk_len);
	void *new_psk_cb_ctx;

	/* channel switch parameters */
	struct hostapd_freq_params cs_freq_params;
	u8 cs_count;
	int cs_block_tx;
	unsigned int cs_c_off_beacon;
	unsigned int cs_c_off_proberesp;
	int csa_in_progress;
	unsigned int cs_c_off_ecsa_beacon;
	unsigned int cs_c_off_ecsa_proberesp;

#ifdef CONFIG_P2P
	struct p2p_data *p2p;
	struct p2p_group *p2p_group;
	struct wpabuf *p2p_beacon_ie;
	struct wpabuf *p2p_probe_resp_ie;

	/* Number of non-P2P association stations */
	int num_sta_no_p2p;

	/* Periodic NoA (used only when no non-P2P clients in the group) */
	int noa_enabled;
	int noa_start;
	int noa_duration;
#endif /* CONFIG_P2P */
#ifdef CONFIG_PROXYARP
	struct l2_packet_data *sock_dhcp;
	struct l2_packet_data *sock_ndisc;
#endif /* CONFIG_PROXYARP */
#ifdef CONFIG_MESH
	int num_plinks;
	int max_plinks;
	void (*mesh_sta_free_cb)(struct hostapd_data *hapd,
				 struct sta_info *sta);
	struct wpabuf *mesh_pending_auth;
	struct os_reltime mesh_pending_auth_time;
	u8 mesh_required_peer[ETH_ALEN];
#endif /* CONFIG_MESH */

#ifdef CONFIG_SQLITE
	struct hostapd_eap_user tmp_eap_user;
#endif /* CONFIG_SQLITE */

#ifdef CONFIG_SAE
	/** Key used for generating SAE anti-clogging tokens */
	u8 sae_token_key[8];
	struct os_reltime last_sae_token_key_update;
	int dot11RSNASAERetransPeriod; /* msec */
#endif /* CONFIG_SAE */

#ifdef CONFIG_TESTING_OPTIONS
	unsigned int ext_mgmt_frame_handling:1;
	unsigned int ext_eapol_frame_io:1;

	struct l2_packet_data *l2_test;

	enum wpa_alg last_gtk_alg;
	int last_gtk_key_idx;
	u8 last_gtk[WPA_GTK_MAX_LEN];
	size_t last_gtk_len;

#ifdef CONFIG_IEEE80211W
	enum wpa_alg last_igtk_alg;
	int last_igtk_key_idx;
	u8 last_igtk[WPA_IGTK_MAX_LEN];
	size_t last_igtk_len;
#endif /* CONFIG_IEEE80211W */
#endif /* CONFIG_TESTING_OPTIONS */

#ifdef CONFIG_MBO
	unsigned int mbo_assoc_disallow;
#endif /* CONFIG_MBO */

	struct dl_list nr_db;

	u8 beacon_req_token;
	u8 lci_req_token;
	u8 range_req_token;
	unsigned int lci_req_active:1;
	unsigned int range_req_active:1;

	int dhcp_sock; /* UDP socket used with the DHCP server */

#ifdef CONFIG_DPP
	int dpp_init_done;
	struct dpp_authentication *dpp_auth;
	u8 dpp_allowed_roles;
	int dpp_qr_mutual;
	int dpp_auth_ok_on_ack;
	int dpp_in_response_listen;
	struct gas_query_ap *gas;
	struct dpp_pkex *dpp_pkex;
	struct dpp_bootstrap_info *dpp_pkex_bi;
	char *dpp_pkex_code;
	char *dpp_pkex_identifier;
	char *dpp_pkex_auth_cmd;
	char *dpp_configurator_params;
	struct os_reltime dpp_last_init;
	struct os_reltime dpp_init_iter_start;
	unsigned int dpp_init_max_tries;
	unsigned int dpp_init_retry_time;
	unsigned int dpp_resp_wait_time;
	unsigned int dpp_resp_max_tries;
	unsigned int dpp_resp_retry_time;
#ifdef CONFIG_TESTING_OPTIONS
	char *dpp_config_obj_override;
	char *dpp_discovery_override;
	char *dpp_groups_override;
	unsigned int dpp_ignore_netaccesskey_mismatch:1;
#endif /* CONFIG_TESTING_OPTIONS */
#endif /* CONFIG_DPP */
};


struct hostapd_sta_info {
	struct dl_list list;
	u8 addr[ETH_ALEN];
	struct os_reltime last_seen;
	int ssi_signal;
#ifdef CONFIG_TAXONOMY
	struct wpabuf *probe_ie_taxonomy;
#endif /* CONFIG_TAXONOMY */
};

/**
 * struct hostapd_iface - hostapd per-interface data structure
 */
struct hostapd_iface {
	struct hapd_interfaces *interfaces;
	void *owner;
	char *config_fname;
	struct hostapd_config *conf;
	char phy[16]; /* Name of the PHY (radio) */

	enum hostapd_iface_state {
		HAPD_IFACE_UNINITIALIZED,
		HAPD_IFACE_DISABLED,
		HAPD_IFACE_COUNTRY_UPDATE,
		HAPD_IFACE_ACS,
		HAPD_IFACE_HT_SCAN,
		HAPD_IFACE_DFS,
		HAPD_IFACE_ENABLED
	} state;

#ifdef CONFIG_MESH
	struct mesh_conf *mconf;
#endif /* CONFIG_MESH */

	size_t num_bss;
	struct hostapd_data **bss;

	unsigned int wait_channel_update:1;
	unsigned int cac_started:1;
#ifdef CONFIG_FST
	struct fst_iface *fst;
	const struct wpabuf *fst_ies;
#endif /* CONFIG_FST */

	/*
	 * When set, indicates that the driver will handle the AP
	 * teardown: delete global keys, station keys, and stations.
	 */
	unsigned int driver_ap_teardown:1;

	/*
	 * When set, indicates that this interface is part of list of
	 * interfaces that need to be started together (synchronously).
	 */
	unsigned int need_to_start_in_sync:1;

	/* Ready to start but waiting for other interfaces to become ready. */
	unsigned int ready_to_start_in_sync:1;

	int num_ap; /* number of entries in ap_list */
	struct ap_info *ap_list; /* AP info list head */
	struct ap_info *ap_hash[STA_HASH_SIZE];

	u64 drv_flags;

	/* SMPS modes supported by the driver (WPA_DRIVER_SMPS_MODE_*) */
	unsigned int smps_modes;

	/*
	 * A bitmap of supported protocols for probe response offload. See
	 * struct wpa_driver_capa in driver.h
	 */
	unsigned int probe_resp_offloads;

	/* extended capabilities supported by the driver */
	const u8 *extended_capa, *extended_capa_mask;
	unsigned int extended_capa_len;

	unsigned int drv_max_acl_mac_addrs;

	struct hostapd_hw_modes *hw_features;
	int num_hw_features;
	struct hostapd_hw_modes *current_mode;
	/* Rates that are currently used (i.e., filtered copy of
	 * current_mode->channels */
	int num_rates;
	struct hostapd_rate_data *current_rates;
	int *basic_rates;
	int freq;

	u16 hw_flags;

	/* Number of associated Non-ERP stations (i.e., stations using 802.11b
	 * in 802.11g BSS) */
	int num_sta_non_erp;

	/* Number of associated stations that do not support Short Slot Time */
	int num_sta_no_short_slot_time;

	/* Number of associated stations that do not support Short Preamble */
	int num_sta_no_short_preamble;

	int olbc; /* Overlapping Legacy BSS Condition */

	/* Number of HT associated stations that do not support greenfield */
	int num_sta_ht_no_gf;

	/* Number of associated non-HT stations */
	int num_sta_no_ht;

	/* Number of HT associated stations 20 MHz */
	int num_sta_ht_20mhz;

	/* Number of HT40 intolerant stations */
	int num_sta_ht40_intolerant;

	/* Overlapping BSS information */
	int olbc_ht;

	u16 ht_op_mode;

	/* surveying helpers */

	/* number of channels surveyed */
	unsigned int chans_surveyed;

	/* lowest observed noise floor in dBm */
	s8 lowest_nf;

	/* channel utilization calculation */
	u64 last_channel_time;
	u64 last_channel_time_busy;
	u8 channel_utilization;

	unsigned int chan_util_samples_sum;
	unsigned int chan_util_num_sample_periods;
	unsigned int chan_util_average;

	/* eCSA IE will be added only if operating class is specified */
	u8 cs_oper_class;

	unsigned int dfs_cac_ms;
	struct os_reltime dfs_cac_start;

	/* Latched with the actual secondary channel information and will be
	 * used while juggling between HT20 and HT40 modes. */
	int secondary_ch;

#ifdef CONFIG_ACS
	unsigned int acs_num_completed_scans;
#endif /* CONFIG_ACS */

	void (*scan_cb)(struct hostapd_iface *iface);
	int num_ht40_scan_tries;

	struct dl_list sta_seen; /* struct hostapd_sta_info */
	unsigned int num_sta_seen;

	u8 dfs_domain;
};

/* hostapd.c */
int hostapd_for_each_interface(struct hapd_interfaces *interfaces,
			       int (*cb)(struct hostapd_iface *iface,
					 void *ctx), void *ctx);
int hostapd_reload_config(struct hostapd_iface *iface);
void hostapd_reconfig_encryption(struct hostapd_data *hapd);
struct hostapd_data *
hostapd_alloc_bss_data(struct hostapd_iface *hapd_iface,
		       struct hostapd_config *conf,
		       struct hostapd_bss_config *bss);
int hostapd_setup_interface(struct hostapd_iface *iface);
int hostapd_setup_interface_complete(struct hostapd_iface *iface, int err);
void hostapd_interface_deinit(struct hostapd_iface *iface);
void hostapd_interface_free(struct hostapd_iface *iface);
struct hostapd_iface * hostapd_alloc_iface(void);
struct hostapd_iface * hostapd_init(struct hapd_interfaces *interfaces,
				    const char *config_file);
struct hostapd_iface *
hostapd_interface_init_bss(struct hapd_interfaces *interfaces, const char *phy,
			   const char *config_fname, int debug);
void hostapd_new_assoc_sta(struct hostapd_data *hapd, struct sta_info *sta,
			   int reassoc);
void hostapd_interface_deinit_free(struct hostapd_iface *iface);
int hostapd_enable_iface(struct hostapd_iface *hapd_iface);
int hostapd_reload_iface(struct hostapd_iface *hapd_iface);
int hostapd_disable_iface(struct hostapd_iface *hapd_iface);
int hostapd_add_iface(struct hapd_interfaces *ifaces, char *buf);
int hostapd_remove_iface(struct hapd_interfaces *ifaces, char *buf);
void hostapd_channel_list_updated(struct hostapd_iface *iface, int initiator);
void hostapd_set_state(struct hostapd_iface *iface, enum hostapd_iface_state s);
const char * hostapd_state_text(enum hostapd_iface_state s);
int hostapd_csa_in_progress(struct hostapd_iface *iface);
void hostapd_chan_switch_vht_config(struct hostapd_data *hapd, int vht_enabled);
int hostapd_switch_channel(struct hostapd_data *hapd,
			   struct csa_settings *settings);
void
hostapd_switch_channel_fallback(struct hostapd_iface *iface,
				const struct hostapd_freq_params *freq_params);
void hostapd_cleanup_cs_params(struct hostapd_data *hapd);
void hostapd_periodic_iface(struct hostapd_iface *iface);
int hostapd_owe_trans_get_info(struct hostapd_data *hapd);

/* utils.c */
int hostapd_register_probereq_cb(struct hostapd_data *hapd,
				 int (*cb)(void *ctx, const u8 *sa,
					   const u8 *da, const u8 *bssid,
					   const u8 *ie, size_t ie_len,
					   int ssi_signal),
				 void *ctx);
void hostapd_prune_associations(struct hostapd_data *hapd, const u8 *addr);

/* drv_callbacks.c (TODO: move to somewhere else?) */
void hostapd_notify_assoc_fils_finish(struct hostapd_data *hapd,
				      struct sta_info *sta);
int hostapd_notif_assoc(struct hostapd_data *hapd, const u8 *addr,
			const u8 *ie, size_t ielen, int reassoc);
void hostapd_notif_disassoc(struct hostapd_data *hapd, const u8 *addr);
void hostapd_event_sta_low_ack(struct hostapd_data *hapd, const u8 *addr);
void hostapd_event_connect_failed_reason(struct hostapd_data *hapd,
					 const u8 *addr, int reason_code);
int hostapd_probe_req_rx(struct hostapd_data *hapd, const u8 *sa, const u8 *da,
			 const u8 *bssid, const u8 *ie, size_t ie_len,
			 int ssi_signal);
void hostapd_event_ch_switch(struct hostapd_data *hapd, int freq, int ht,
			     int offset, int width, int cf1, int cf2);
struct survey_results;
void hostapd_event_get_survey(struct hostapd_iface *iface,
			      struct survey_results *survey_results);
void hostapd_acs_channel_selected(struct hostapd_data *hapd,
				  struct acs_selected_channels *acs_res);

const struct hostapd_eap_user *
hostapd_get_eap_user(struct hostapd_data *hapd, const u8 *identity,
		     size_t identity_len, int phase2);

struct hostapd_data * hostapd_get_iface(struct hapd_interfaces *interfaces,
					const char *ifname);
void hostapd_event_sta_opmode_changed(struct hostapd_data *hapd, const u8 *addr,
				      enum smps_mode smps_mode,
				      enum chan_width chan_width, u8 rx_nss);

#ifdef CONFIG_FST
void fst_hostapd_fill_iface_obj(struct hostapd_data *hapd,
				struct fst_wpa_obj *iface_obj);
#endif /* CONFIG_FST */

#endif /* HOSTAPD_H */
