/*
 * hostapd - IEEE 802.11i-2004 / WPA Authenticator: Internal definitions
 * Copyright (c) 2004-2015, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef WPA_AUTH_I_H
#define WPA_AUTH_I_H

#include "utils/list.h"

/* max(dot11RSNAConfigGroupUpdateCount,dot11RSNAConfigPairwiseUpdateCount) */
#define RSNA_MAX_EAPOL_RETRIES 4

struct wpa_group;

struct wpa_state_machine {
	struct wpa_authenticator *wpa_auth;
	struct wpa_group *group;

	u8 addr[ETH_ALEN];
	u8 p2p_dev_addr[ETH_ALEN];

	enum {
		WPA_PTK_INITIALIZE, WPA_PTK_DISCONNECT, WPA_PTK_DISCONNECTED,
		WPA_PTK_AUTHENTICATION, WPA_PTK_AUTHENTICATION2,
		WPA_PTK_INITPMK, WPA_PTK_INITPSK, WPA_PTK_PTKSTART,
		WPA_PTK_PTKCALCNEGOTIATING, WPA_PTK_PTKCALCNEGOTIATING2,
		WPA_PTK_PTKINITNEGOTIATING, WPA_PTK_PTKINITDONE
	} wpa_ptk_state;

	enum {
		WPA_PTK_GROUP_IDLE = 0,
		WPA_PTK_GROUP_REKEYNEGOTIATING,
		WPA_PTK_GROUP_REKEYESTABLISHED,
		WPA_PTK_GROUP_KEYERROR
	} wpa_ptk_group_state;

	Boolean Init;
	Boolean DeauthenticationRequest;
	Boolean AuthenticationRequest;
	Boolean ReAuthenticationRequest;
	Boolean Disconnect;
	u16 disconnect_reason; /* specific reason code to use with Disconnect */
	u32 TimeoutCtr;
	u32 GTimeoutCtr;
	Boolean TimeoutEvt;
	Boolean EAPOLKeyReceived;
	Boolean EAPOLKeyPairwise;
	Boolean EAPOLKeyRequest;
	Boolean MICVerified;
	Boolean GUpdateStationKeys;
	u8 ANonce[WPA_NONCE_LEN];
	u8 SNonce[WPA_NONCE_LEN];
	u8 alt_SNonce[WPA_NONCE_LEN];
	u8 alt_replay_counter[WPA_REPLAY_COUNTER_LEN];
	u8 PMK[PMK_LEN_MAX];
	unsigned int pmk_len;
	u8 pmkid[PMKID_LEN]; /* valid if pmkid_set == 1 */
	struct wpa_ptk PTK;
	Boolean PTK_valid;
	Boolean pairwise_set;
	Boolean tk_already_set;
	int keycount;
	Boolean Pair;
	struct wpa_key_replay_counter {
		u8 counter[WPA_REPLAY_COUNTER_LEN];
		Boolean valid;
	} key_replay[RSNA_MAX_EAPOL_RETRIES],
		prev_key_replay[RSNA_MAX_EAPOL_RETRIES];
	Boolean PInitAKeys; /* WPA only, not in IEEE 802.11i */
	Boolean PTKRequest; /* not in IEEE 802.11i state machine */
	Boolean has_GTK;
	Boolean PtkGroupInit; /* init request for PTK Group state machine */

	u8 *last_rx_eapol_key; /* starting from IEEE 802.1X header */
	size_t last_rx_eapol_key_len;

	unsigned int changed:1;
	unsigned int in_step_loop:1;
	unsigned int pending_deinit:1;
	unsigned int started:1;
	unsigned int mgmt_frame_prot:1;
	unsigned int rx_eapol_key_secure:1;
	unsigned int update_snonce:1;
	unsigned int alt_snonce_valid:1;
#ifdef CONFIG_IEEE80211R_AP
	unsigned int ft_completed:1;
	unsigned int pmk_r1_name_valid:1;
#endif /* CONFIG_IEEE80211R_AP */
	unsigned int is_wnmsleep:1;
	unsigned int pmkid_set:1;

	u8 req_replay_counter[WPA_REPLAY_COUNTER_LEN];
	int req_replay_counter_used;

	u8 *wpa_ie;
	size_t wpa_ie_len;

	enum {
		WPA_VERSION_NO_WPA = 0 /* WPA not used */,
		WPA_VERSION_WPA = 1 /* WPA / IEEE 802.11i/D3.0 */,
		WPA_VERSION_WPA2 = 2 /* WPA2 / IEEE 802.11i */
	} wpa;
	int pairwise; /* Pairwise cipher suite, WPA_CIPHER_* */
	int wpa_key_mgmt; /* the selected WPA_KEY_MGMT_* */
	struct rsn_pmksa_cache_entry *pmksa;

	u32 dot11RSNAStatsTKIPLocalMICFailures;
	u32 dot11RSNAStatsTKIPRemoteMICFailures;

#ifdef CONFIG_IEEE80211R_AP
	u8 xxkey[PMK_LEN_MAX]; /* PSK or the second 256 bits of MSK, or the
				* first 384 bits of MSK */
	size_t xxkey_len;
	u8 pmk_r1_name[WPA_PMK_NAME_LEN]; /* PMKR1Name derived from FT Auth
					   * Request */
	u8 r0kh_id[FT_R0KH_ID_MAX_LEN]; /* R0KH-ID from FT Auth Request */
	size_t r0kh_id_len;
	u8 sup_pmk_r1_name[WPA_PMK_NAME_LEN]; /* PMKR1Name from EAPOL-Key
					       * message 2/4 */
	u8 *assoc_resp_ftie;

	void (*ft_pending_cb)(void *ctx, const u8 *dst, const u8 *bssid,
			      u16 auth_transaction, u16 status,
			      const u8 *ies, size_t ies_len);
	void *ft_pending_cb_ctx;
	struct wpabuf *ft_pending_req_ies;
	u8 ft_pending_pull_nonce[FT_RRB_NONCE_LEN];
	u8 ft_pending_auth_transaction;
	u8 ft_pending_current_ap[ETH_ALEN];
	int ft_pending_pull_left_retries;
#endif /* CONFIG_IEEE80211R_AP */

	int pending_1_of_4_timeout;

#ifdef CONFIG_P2P
	u8 ip_addr[4];
#endif /* CONFIG_P2P */

#ifdef CONFIG_FILS
	u8 fils_key_auth_sta[FILS_MAX_KEY_AUTH_LEN];
	u8 fils_key_auth_ap[FILS_MAX_KEY_AUTH_LEN];
	size_t fils_key_auth_len;
	unsigned int fils_completed:1;
#endif /* CONFIG_FILS */

#ifdef CONFIG_TESTING_OPTIONS
	void (*eapol_status_cb)(void *ctx1, void *ctx2);
	void *eapol_status_cb_ctx1;
	void *eapol_status_cb_ctx2;
#endif /* CONFIG_TESTING_OPTIONS */
};


/* per group key state machine data */
struct wpa_group {
	struct wpa_group *next;
	int vlan_id;

	Boolean GInit;
	int GKeyDoneStations;
	Boolean GTKReKey;
	int GTK_len;
	int GN, GM;
	Boolean GTKAuthenticator;
	u8 Counter[WPA_NONCE_LEN];

	enum {
		WPA_GROUP_GTK_INIT = 0,
		WPA_GROUP_SETKEYS, WPA_GROUP_SETKEYSDONE,
		WPA_GROUP_FATAL_FAILURE
	} wpa_group_state;

	u8 GMK[WPA_GMK_LEN];
	u8 GTK[2][WPA_GTK_MAX_LEN];
	u8 GNonce[WPA_NONCE_LEN];
	Boolean changed;
	Boolean first_sta_seen;
	Boolean reject_4way_hs_for_entropy;
#ifdef CONFIG_IEEE80211W
	u8 IGTK[2][WPA_IGTK_MAX_LEN];
	int GN_igtk, GM_igtk;
#endif /* CONFIG_IEEE80211W */
	/* Number of references except those in struct wpa_group->next */
	unsigned int references;
	unsigned int num_setup_iface;
};


struct wpa_ft_pmk_cache;

/* per authenticator data */
struct wpa_authenticator {
	struct wpa_group *group;

	unsigned int dot11RSNAStatsTKIPRemoteMICFailures;
	u32 dot11RSNAAuthenticationSuiteSelected;
	u32 dot11RSNAPairwiseCipherSelected;
	u32 dot11RSNAGroupCipherSelected;
	u8 dot11RSNAPMKIDUsed[PMKID_LEN];
	u32 dot11RSNAAuthenticationSuiteRequested; /* FIX: update */
	u32 dot11RSNAPairwiseCipherRequested; /* FIX: update */
	u32 dot11RSNAGroupCipherRequested; /* FIX: update */
	unsigned int dot11RSNATKIPCounterMeasuresInvoked;
	unsigned int dot11RSNA4WayHandshakeFailures;

	struct wpa_auth_config conf;
	const struct wpa_auth_callbacks *cb;
	void *cb_ctx;

	u8 *wpa_ie;
	size_t wpa_ie_len;

	u8 addr[ETH_ALEN];

	struct rsn_pmksa_cache *pmksa;
	struct wpa_ft_pmk_cache *ft_pmk_cache;

#ifdef CONFIG_P2P
	struct bitfield *ip_pool;
#endif /* CONFIG_P2P */
};


#ifdef CONFIG_IEEE80211R_AP

#define FT_REMOTE_SEQ_BACKLOG 16
struct ft_remote_seq_rx {
	u32 dom;
	struct os_reltime time_offset; /* local time - offset = remote time */

	/* accepted sequence numbers: (offset ... offset + 0x40000000]
	 *   (except those in last)
	 * dropped sequence numbers: (offset - 0x40000000 ... offset]
	 * all others trigger SEQ_REQ message (except first message)
	 */
	u32 last[FT_REMOTE_SEQ_BACKLOG];
	unsigned int num_last;
	u32 offsetidx;

	struct dl_list queue; /* send nonces + rrb msgs awaiting seq resp */
};

struct ft_remote_seq_tx {
	u32 dom; /* non zero if initialized */
	u32 seq;
};

struct ft_remote_seq {
	struct ft_remote_seq_rx rx;
	struct ft_remote_seq_tx tx;
};

#endif /* CONFIG_IEEE80211R_AP */


int wpa_write_rsn_ie(struct wpa_auth_config *conf, u8 *buf, size_t len,
		     const u8 *pmkid);
void wpa_auth_logger(struct wpa_authenticator *wpa_auth, const u8 *addr,
		     logger_level level, const char *txt);
void wpa_auth_vlogger(struct wpa_authenticator *wpa_auth, const u8 *addr,
		      logger_level level, const char *fmt, ...);
void __wpa_send_eapol(struct wpa_authenticator *wpa_auth,
		      struct wpa_state_machine *sm, int key_info,
		      const u8 *key_rsc, const u8 *nonce,
		      const u8 *kde, size_t kde_len,
		      int keyidx, int encr, int force_version);
int wpa_auth_for_each_sta(struct wpa_authenticator *wpa_auth,
			  int (*cb)(struct wpa_state_machine *sm, void *ctx),
			  void *cb_ctx);
int wpa_auth_for_each_auth(struct wpa_authenticator *wpa_auth,
			   int (*cb)(struct wpa_authenticator *a, void *ctx),
			   void *cb_ctx);

#ifdef CONFIG_IEEE80211R_AP
int wpa_write_mdie(struct wpa_auth_config *conf, u8 *buf, size_t len);
int wpa_write_ftie(struct wpa_auth_config *conf, int use_sha384,
		   const u8 *r0kh_id, size_t r0kh_id_len,
		   const u8 *anonce, const u8 *snonce,
		   u8 *buf, size_t len, const u8 *subelem,
		   size_t subelem_len);
int wpa_auth_derive_ptk_ft(struct wpa_state_machine *sm, const u8 *pmk,
			   struct wpa_ptk *ptk);
struct wpa_ft_pmk_cache * wpa_ft_pmk_cache_init(void);
void wpa_ft_pmk_cache_deinit(struct wpa_ft_pmk_cache *cache);
void wpa_ft_install_ptk(struct wpa_state_machine *sm);
int wpa_ft_store_pmk_fils(struct wpa_state_machine *sm, const u8 *pmk_r0,
			  const u8 *pmk_r0_name);
#endif /* CONFIG_IEEE80211R_AP */

#endif /* WPA_AUTH_I_H */
