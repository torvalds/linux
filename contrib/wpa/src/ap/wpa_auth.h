/*
 * hostapd - IEEE 802.11i-2004 / WPA Authenticator
 * Copyright (c) 2004-2017, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef WPA_AUTH_H
#define WPA_AUTH_H

#include "common/defs.h"
#include "common/eapol_common.h"
#include "common/wpa_common.h"
#include "common/ieee802_11_defs.h"

struct vlan_description;

#define MAX_OWN_IE_OVERRIDE 256

#ifdef _MSC_VER
#pragma pack(push, 1)
#endif /* _MSC_VER */

/* IEEE Std 802.11r-2008, 11A.10.3 - Remote request/response frame definition
 */
struct ft_rrb_frame {
	u8 frame_type; /* RSN_REMOTE_FRAME_TYPE_FT_RRB */
	u8 packet_type; /* FT_PACKET_REQUEST/FT_PACKET_RESPONSE */
	le16 action_length; /* little endian length of action_frame */
	u8 ap_address[ETH_ALEN];
	/*
	 * Followed by action_length bytes of FT Action frame (from Category
	 * field to the end of Action Frame body.
	 */
} STRUCT_PACKED;

#define RSN_REMOTE_FRAME_TYPE_FT_RRB 1

#define FT_PACKET_REQUEST 0
#define FT_PACKET_RESPONSE 1

/* Vendor-specific types for R0KH-R1KH protocol; not defined in 802.11r. These
 * use OUI Extended EtherType as the encapsulating format. */
#define FT_PACKET_R0KH_R1KH_PULL 0x01
#define FT_PACKET_R0KH_R1KH_RESP 0x02
#define FT_PACKET_R0KH_R1KH_PUSH 0x03
#define FT_PACKET_R0KH_R1KH_SEQ_REQ 0x04
#define FT_PACKET_R0KH_R1KH_SEQ_RESP 0x05

/* packet layout
 *  IEEE 802 extended OUI ethertype frame header
 *  u16 authlen (little endian)
 *  multiple of struct ft_rrb_tlv (authenticated only, length = authlen)
 *  multiple of struct ft_rrb_tlv (AES-SIV encrypted, AES-SIV needs an extra
 *                                 blocksize length)
 *
 * AES-SIV AAD;
 *  source MAC address (6)
 *  authenticated-only TLVs (authlen)
 *  subtype (1; FT_PACKET_*)
 */

#define FT_RRB_NONCE_LEN 16

#define FT_RRB_LAST_EMPTY     0 /* placeholder or padding */

#define FT_RRB_SEQ            1 /* struct ft_rrb_seq */
#define FT_RRB_NONCE          2 /* size FT_RRB_NONCE_LEN */
#define FT_RRB_TIMESTAMP      3 /* le32 unix seconds */

#define FT_RRB_R0KH_ID        4 /* FT_R0KH_ID_MAX_LEN */
#define FT_RRB_R1KH_ID        5 /* FT_R1KH_ID_LEN */
#define FT_RRB_S1KH_ID        6 /* ETH_ALEN */

#define FT_RRB_PMK_R0_NAME    7 /* WPA_PMK_NAME_LEN */
#define FT_RRB_PMK_R0         8 /* PMK_LEN */
#define FT_RRB_PMK_R1_NAME    9 /* WPA_PMK_NAME_LEN */
#define FT_RRB_PMK_R1        10 /* PMK_LEN */

#define FT_RRB_PAIRWISE      11 /* le16 */
#define FT_RRB_EXPIRES_IN    12 /* le16 seconds */

#define FT_RRB_VLAN_UNTAGGED 13 /* le16 */
#define FT_RRB_VLAN_TAGGED   14 /* n times le16 */

#define FT_RRB_IDENTITY      15
#define FT_RRB_RADIUS_CUI    16
#define FT_RRB_SESSION_TIMEOUT  17 /* le32 seconds */

struct ft_rrb_tlv {
	le16 type;
	le16 len;
	/* followed by data of length len */
} STRUCT_PACKED;

struct ft_rrb_seq {
	le32 dom;
	le32 seq;
	le32 ts;
} STRUCT_PACKED;

/* session TLVs:
 *   required: PMK_R1, PMK_R1_NAME, PAIRWISE
 *   optional: VLAN_UNTAGGED, VLAN_TAGGED, EXPIRES_IN, IDENTITY, RADIUS_CUI,
 *		 SESSION_TIMEOUT
 *
 * pull frame TLVs:
 *   auth:
 *     required: SEQ, NONCE, R0KH_ID, R1KH_ID
 *   encrypted:
 *     required: PMK_R0_NAME, S1KH_ID
 *
 * response frame TLVs:
 *   auth:
 *     required: SEQ, NONCE, R0KH_ID, R1KH_ID
 *   encrypted:
 *     required: S1KH_ID
 *     optional: session TLVs
 *
 * push frame TLVs:
 *   auth:
 *     required: SEQ, R0KH_ID, R1KH_ID
 *   encrypted:
 *     required: S1KH_ID, PMK_R0_NAME, session TLVs
 *
 * sequence number request frame TLVs:
 *   auth:
 *     required: R0KH_ID, R1KH_ID, NONCE
 *
 * sequence number response frame TLVs:
 *   auth:
 *     required: SEQ, NONCE, R0KH_ID, R1KH_ID
 */

#ifdef _MSC_VER
#pragma pack(pop)
#endif /* _MSC_VER */


/* per STA state machine data */

struct wpa_authenticator;
struct wpa_state_machine;
struct rsn_pmksa_cache_entry;
struct eapol_state_machine;
struct ft_remote_seq;


struct ft_remote_r0kh {
	struct ft_remote_r0kh *next;
	u8 addr[ETH_ALEN];
	u8 id[FT_R0KH_ID_MAX_LEN];
	size_t id_len;
	u8 key[32];
	struct ft_remote_seq *seq;
};


struct ft_remote_r1kh {
	struct ft_remote_r1kh *next;
	u8 addr[ETH_ALEN];
	u8 id[FT_R1KH_ID_LEN];
	u8 key[32];
	struct ft_remote_seq *seq;
};


struct wpa_auth_config {
	int wpa;
	int wpa_key_mgmt;
	int wpa_pairwise;
	int wpa_group;
	int wpa_group_rekey;
	int wpa_strict_rekey;
	int wpa_gmk_rekey;
	int wpa_ptk_rekey;
	u32 wpa_group_update_count;
	u32 wpa_pairwise_update_count;
	int wpa_disable_eapol_key_retries;
	int rsn_pairwise;
	int rsn_preauth;
	int eapol_version;
	int wmm_enabled;
	int wmm_uapsd;
	int disable_pmksa_caching;
	int okc;
	int tx_status;
#ifdef CONFIG_IEEE80211W
	enum mfp_options ieee80211w;
	int group_mgmt_cipher;
	int sae_require_mfp;
#endif /* CONFIG_IEEE80211W */
#ifdef CONFIG_IEEE80211R_AP
	u8 ssid[SSID_MAX_LEN];
	size_t ssid_len;
	u8 mobility_domain[MOBILITY_DOMAIN_ID_LEN];
	u8 r0_key_holder[FT_R0KH_ID_MAX_LEN];
	size_t r0_key_holder_len;
	u8 r1_key_holder[FT_R1KH_ID_LEN];
	u32 r0_key_lifetime; /* PMK-R0 lifetime seconds */
	int rkh_pos_timeout;
	int rkh_neg_timeout;
	int rkh_pull_timeout; /* ms */
	int rkh_pull_retries;
	int r1_max_key_lifetime;
	u32 reassociation_deadline;
	struct ft_remote_r0kh **r0kh_list;
	struct ft_remote_r1kh **r1kh_list;
	int pmk_r1_push;
	int ft_over_ds;
	int ft_psk_generate_local;
#endif /* CONFIG_IEEE80211R_AP */
	int disable_gtk;
	int ap_mlme;
#ifdef CONFIG_TESTING_OPTIONS
	double corrupt_gtk_rekey_mic_probability;
	u8 own_ie_override[MAX_OWN_IE_OVERRIDE];
	size_t own_ie_override_len;
#endif /* CONFIG_TESTING_OPTIONS */
#ifdef CONFIG_P2P
	u8 ip_addr_go[4];
	u8 ip_addr_mask[4];
	u8 ip_addr_start[4];
	u8 ip_addr_end[4];
#endif /* CONFIG_P2P */
#ifdef CONFIG_FILS
	unsigned int fils_cache_id_set:1;
	u8 fils_cache_id[FILS_CACHE_ID_LEN];
#endif /* CONFIG_FILS */
};

typedef enum {
	LOGGER_DEBUG, LOGGER_INFO, LOGGER_WARNING
} logger_level;

typedef enum {
	WPA_EAPOL_portEnabled, WPA_EAPOL_portValid, WPA_EAPOL_authorized,
	WPA_EAPOL_portControl_Auto, WPA_EAPOL_keyRun, WPA_EAPOL_keyAvailable,
	WPA_EAPOL_keyDone, WPA_EAPOL_inc_EapolFramesTx
} wpa_eapol_variable;

struct wpa_auth_callbacks {
	void (*logger)(void *ctx, const u8 *addr, logger_level level,
		       const char *txt);
	void (*disconnect)(void *ctx, const u8 *addr, u16 reason);
	int (*mic_failure_report)(void *ctx, const u8 *addr);
	void (*psk_failure_report)(void *ctx, const u8 *addr);
	void (*set_eapol)(void *ctx, const u8 *addr, wpa_eapol_variable var,
			  int value);
	int (*get_eapol)(void *ctx, const u8 *addr, wpa_eapol_variable var);
	const u8 * (*get_psk)(void *ctx, const u8 *addr, const u8 *p2p_dev_addr,
			      const u8 *prev_psk, size_t *psk_len);
	int (*get_msk)(void *ctx, const u8 *addr, u8 *msk, size_t *len);
	int (*set_key)(void *ctx, int vlan_id, enum wpa_alg alg,
		       const u8 *addr, int idx, u8 *key, size_t key_len);
	int (*get_seqnum)(void *ctx, const u8 *addr, int idx, u8 *seq);
	int (*send_eapol)(void *ctx, const u8 *addr, const u8 *data,
			  size_t data_len, int encrypt);
	int (*for_each_sta)(void *ctx, int (*cb)(struct wpa_state_machine *sm,
						 void *ctx), void *cb_ctx);
	int (*for_each_auth)(void *ctx, int (*cb)(struct wpa_authenticator *a,
						  void *ctx), void *cb_ctx);
	int (*send_ether)(void *ctx, const u8 *dst, u16 proto, const u8 *data,
			  size_t data_len);
	int (*send_oui)(void *ctx, const u8 *dst, u8 oui_suffix, const u8 *data,
			size_t data_len);
#ifdef CONFIG_IEEE80211R_AP
	struct wpa_state_machine * (*add_sta)(void *ctx, const u8 *sta_addr);
	int (*set_vlan)(void *ctx, const u8 *sta_addr,
			struct vlan_description *vlan);
	int (*get_vlan)(void *ctx, const u8 *sta_addr,
			struct vlan_description *vlan);
	int (*set_identity)(void *ctx, const u8 *sta_addr,
			    const u8 *identity, size_t identity_len);
	size_t (*get_identity)(void *ctx, const u8 *sta_addr, const u8 **buf);
	int (*set_radius_cui)(void *ctx, const u8 *sta_addr,
			      const u8 *radius_cui, size_t radius_cui_len);
	size_t (*get_radius_cui)(void *ctx, const u8 *sta_addr, const u8 **buf);
	void (*set_session_timeout)(void *ctx, const u8 *sta_addr,
				    int session_timeout);
	int (*get_session_timeout)(void *ctx, const u8 *sta_addr);

	int (*send_ft_action)(void *ctx, const u8 *dst,
			      const u8 *data, size_t data_len);
	int (*add_tspec)(void *ctx, const u8 *sta_addr, u8 *tspec_ie,
			 size_t tspec_ielen);
#endif /* CONFIG_IEEE80211R_AP */
#ifdef CONFIG_MESH
	int (*start_ampe)(void *ctx, const u8 *sta_addr);
#endif /* CONFIG_MESH */
};

struct wpa_authenticator * wpa_init(const u8 *addr,
				    struct wpa_auth_config *conf,
				    const struct wpa_auth_callbacks *cb,
				    void *cb_ctx);
int wpa_init_keys(struct wpa_authenticator *wpa_auth);
void wpa_deinit(struct wpa_authenticator *wpa_auth);
int wpa_reconfig(struct wpa_authenticator *wpa_auth,
		 struct wpa_auth_config *conf);

enum {
	WPA_IE_OK, WPA_INVALID_IE, WPA_INVALID_GROUP, WPA_INVALID_PAIRWISE,
	WPA_INVALID_AKMP, WPA_NOT_ENABLED, WPA_ALLOC_FAIL,
	WPA_MGMT_FRAME_PROTECTION_VIOLATION, WPA_INVALID_MGMT_GROUP_CIPHER,
	WPA_INVALID_MDIE, WPA_INVALID_PROTO, WPA_INVALID_PMKID
};

int wpa_validate_wpa_ie(struct wpa_authenticator *wpa_auth,
			struct wpa_state_machine *sm,
			const u8 *wpa_ie, size_t wpa_ie_len,
			const u8 *mdie, size_t mdie_len,
			const u8 *owe_dh, size_t owe_dh_len);
int wpa_validate_osen(struct wpa_authenticator *wpa_auth,
		      struct wpa_state_machine *sm,
		      const u8 *osen_ie, size_t osen_ie_len);
int wpa_auth_uses_mfp(struct wpa_state_machine *sm);
struct wpa_state_machine *
wpa_auth_sta_init(struct wpa_authenticator *wpa_auth, const u8 *addr,
		  const u8 *p2p_dev_addr);
int wpa_auth_sta_associated(struct wpa_authenticator *wpa_auth,
			    struct wpa_state_machine *sm);
void wpa_auth_sta_no_wpa(struct wpa_state_machine *sm);
void wpa_auth_sta_deinit(struct wpa_state_machine *sm);
void wpa_receive(struct wpa_authenticator *wpa_auth,
		 struct wpa_state_machine *sm,
		 u8 *data, size_t data_len);
enum wpa_event {
	WPA_AUTH, WPA_ASSOC, WPA_DISASSOC, WPA_DEAUTH, WPA_REAUTH,
	WPA_REAUTH_EAPOL, WPA_ASSOC_FT, WPA_ASSOC_FILS, WPA_DRV_STA_REMOVED
};
void wpa_remove_ptk(struct wpa_state_machine *sm);
int wpa_auth_sm_event(struct wpa_state_machine *sm, enum wpa_event event);
void wpa_auth_sm_notify(struct wpa_state_machine *sm);
void wpa_gtk_rekey(struct wpa_authenticator *wpa_auth);
int wpa_get_mib(struct wpa_authenticator *wpa_auth, char *buf, size_t buflen);
int wpa_get_mib_sta(struct wpa_state_machine *sm, char *buf, size_t buflen);
void wpa_auth_countermeasures_start(struct wpa_authenticator *wpa_auth);
int wpa_auth_pairwise_set(struct wpa_state_machine *sm);
int wpa_auth_get_pairwise(struct wpa_state_machine *sm);
int wpa_auth_sta_key_mgmt(struct wpa_state_machine *sm);
int wpa_auth_sta_wpa_version(struct wpa_state_machine *sm);
int wpa_auth_sta_ft_tk_already_set(struct wpa_state_machine *sm);
int wpa_auth_sta_fils_tk_already_set(struct wpa_state_machine *sm);
int wpa_auth_sta_clear_pmksa(struct wpa_state_machine *sm,
			     struct rsn_pmksa_cache_entry *entry);
struct rsn_pmksa_cache_entry *
wpa_auth_sta_get_pmksa(struct wpa_state_machine *sm);
void wpa_auth_sta_local_mic_failure_report(struct wpa_state_machine *sm);
const u8 * wpa_auth_get_wpa_ie(struct wpa_authenticator *wpa_auth,
			       size_t *len);
int wpa_auth_pmksa_add(struct wpa_state_machine *sm, const u8 *pmk,
		       unsigned int pmk_len,
		       int session_timeout, struct eapol_state_machine *eapol);
int wpa_auth_pmksa_add_preauth(struct wpa_authenticator *wpa_auth,
			       const u8 *pmk, size_t len, const u8 *sta_addr,
			       int session_timeout,
			       struct eapol_state_machine *eapol);
int wpa_auth_pmksa_add_sae(struct wpa_authenticator *wpa_auth, const u8 *addr,
			   const u8 *pmk, const u8 *pmkid);
void wpa_auth_add_sae_pmkid(struct wpa_state_machine *sm, const u8 *pmkid);
int wpa_auth_pmksa_add2(struct wpa_authenticator *wpa_auth, const u8 *addr,
			const u8 *pmk, size_t pmk_len, const u8 *pmkid,
			int session_timeout, int akmp);
void wpa_auth_pmksa_remove(struct wpa_authenticator *wpa_auth,
			   const u8 *sta_addr);
int wpa_auth_pmksa_list(struct wpa_authenticator *wpa_auth, char *buf,
			size_t len);
void wpa_auth_pmksa_flush(struct wpa_authenticator *wpa_auth);
int wpa_auth_pmksa_list_mesh(struct wpa_authenticator *wpa_auth, const u8 *addr,
			     char *buf, size_t len);
struct rsn_pmksa_cache_entry *
wpa_auth_pmksa_create_entry(const u8 *aa, const u8 *spa, const u8 *pmk,
			    const u8 *pmkid, int expiration);
int wpa_auth_pmksa_add_entry(struct wpa_authenticator *wpa_auth,
			     struct rsn_pmksa_cache_entry *entry);
struct rsn_pmksa_cache_entry *
wpa_auth_pmksa_get(struct wpa_authenticator *wpa_auth, const u8 *sta_addr,
		   const u8 *pmkid);
struct rsn_pmksa_cache_entry *
wpa_auth_pmksa_get_fils_cache_id(struct wpa_authenticator *wpa_auth,
				 const u8 *sta_addr, const u8 *pmkid);
void wpa_auth_pmksa_set_to_sm(struct rsn_pmksa_cache_entry *pmksa,
			      struct wpa_state_machine *sm,
			      struct wpa_authenticator *wpa_auth,
			      u8 *pmkid, u8 *pmk);
int wpa_auth_sta_set_vlan(struct wpa_state_machine *sm, int vlan_id);
void wpa_auth_eapol_key_tx_status(struct wpa_authenticator *wpa_auth,
				  struct wpa_state_machine *sm, int ack);

#ifdef CONFIG_IEEE80211R_AP
u8 * wpa_sm_write_assoc_resp_ies(struct wpa_state_machine *sm, u8 *pos,
				 size_t max_len, int auth_alg,
				 const u8 *req_ies, size_t req_ies_len);
void wpa_ft_process_auth(struct wpa_state_machine *sm, const u8 *bssid,
			 u16 auth_transaction, const u8 *ies, size_t ies_len,
			 void (*cb)(void *ctx, const u8 *dst, const u8 *bssid,
				    u16 auth_transaction, u16 resp,
				    const u8 *ies, size_t ies_len),
			 void *ctx);
u16 wpa_ft_validate_reassoc(struct wpa_state_machine *sm, const u8 *ies,
			    size_t ies_len);
int wpa_ft_action_rx(struct wpa_state_machine *sm, const u8 *data, size_t len);
int wpa_ft_rrb_rx(struct wpa_authenticator *wpa_auth, const u8 *src_addr,
		  const u8 *data, size_t data_len);
void wpa_ft_rrb_oui_rx(struct wpa_authenticator *wpa_auth, const u8 *src_addr,
		       const u8 *dst_addr, u8 oui_suffix, const u8 *data,
		       size_t data_len);
void wpa_ft_push_pmk_r1(struct wpa_authenticator *wpa_auth, const u8 *addr);
void wpa_ft_deinit(struct wpa_authenticator *wpa_auth);
void wpa_ft_sta_deinit(struct wpa_state_machine *sm);
#endif /* CONFIG_IEEE80211R_AP */

void wpa_wnmsleep_rekey_gtk(struct wpa_state_machine *sm);
void wpa_set_wnmsleep(struct wpa_state_machine *sm, int flag);
int wpa_wnmsleep_gtk_subelem(struct wpa_state_machine *sm, u8 *pos);
int wpa_wnmsleep_igtk_subelem(struct wpa_state_machine *sm, u8 *pos);

int wpa_auth_uses_sae(struct wpa_state_machine *sm);
int wpa_auth_uses_ft_sae(struct wpa_state_machine *sm);

int wpa_auth_get_ip_addr(struct wpa_state_machine *sm, u8 *addr);

struct radius_das_attrs;
int wpa_auth_radius_das_disconnect_pmksa(struct wpa_authenticator *wpa_auth,
					 struct radius_das_attrs *attr);
void wpa_auth_reconfig_group_keys(struct wpa_authenticator *wpa_auth);

int wpa_auth_ensure_group(struct wpa_authenticator *wpa_auth, int vlan_id);
int wpa_auth_release_group(struct wpa_authenticator *wpa_auth, int vlan_id);
int fils_auth_pmk_to_ptk(struct wpa_state_machine *sm, const u8 *pmk,
			 size_t pmk_len, const u8 *snonce, const u8 *anonce,
			 const u8 *dhss, size_t dhss_len,
			 struct wpabuf *g_sta, struct wpabuf *g_ap);
int fils_decrypt_assoc(struct wpa_state_machine *sm, const u8 *fils_session,
		       const struct ieee80211_mgmt *mgmt, size_t frame_len,
		       u8 *pos, size_t left);
int fils_encrypt_assoc(struct wpa_state_machine *sm, u8 *buf,
		       size_t current_len, size_t max_len,
		       const struct wpabuf *hlp);
int fils_set_tk(struct wpa_state_machine *sm);
u8 * hostapd_eid_assoc_fils_session(struct wpa_state_machine *sm, u8 *eid,
				    const u8 *fils_session,
				    struct wpabuf *fils_hlp_resp);
const u8 *  wpa_fils_validate_fils_session(struct wpa_state_machine *sm,
					   const u8 *ies, size_t ies_len,
					   const u8 *fils_session);
int wpa_fils_validate_key_confirm(struct wpa_state_machine *sm, const u8 *ies,
				  size_t ies_len);

int wpa_auth_write_fte(struct wpa_authenticator *wpa_auth, int use_sha384,
		       u8 *buf, size_t len);
void wpa_auth_get_fils_aead_params(struct wpa_state_machine *sm,
				   u8 *fils_anonce, u8 *fils_snonce,
				   u8 *fils_kek, size_t *fils_kek_len);
u8 * wpa_auth_write_assoc_resp_owe(struct wpa_state_machine *sm,
				   u8 *pos, size_t max_len,
				   const u8 *req_ies, size_t req_ies_len);

int wpa_auth_resend_m1(struct wpa_state_machine *sm, int change_anonce,
		       void (*cb)(void *ctx1, void *ctx2),
		       void *ctx1, void *ctx2);
int wpa_auth_resend_m3(struct wpa_state_machine *sm,
		       void (*cb)(void *ctx1, void *ctx2),
		       void *ctx1, void *ctx2);
int wpa_auth_resend_group_m1(struct wpa_state_machine *sm,
			     void (*cb)(void *ctx1, void *ctx2),
			     void *ctx1, void *ctx2);
int wpa_auth_rekey_gtk(struct wpa_authenticator *wpa_auth);

#endif /* WPA_AUTH_H */
