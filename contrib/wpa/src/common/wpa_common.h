/*
 * WPA definitions shared between hostapd and wpa_supplicant
 * Copyright (c) 2002-2018, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef WPA_COMMON_H
#define WPA_COMMON_H

/* IEEE 802.11i */
#define PMKID_LEN 16
#define PMK_LEN 32
#define PMK_LEN_SUITE_B_192 48
#define PMK_LEN_MAX 64
#define WPA_REPLAY_COUNTER_LEN 8
#define WPA_NONCE_LEN 32
#define WPA_KEY_RSC_LEN 8
#define WPA_GMK_LEN 32
#define WPA_GTK_MAX_LEN 32

#define OWE_DH_GROUP 19

#define WPA_ALLOWED_PAIRWISE_CIPHERS \
(WPA_CIPHER_CCMP | WPA_CIPHER_GCMP | WPA_CIPHER_TKIP | WPA_CIPHER_NONE | \
WPA_CIPHER_GCMP_256 | WPA_CIPHER_CCMP_256)
#define WPA_ALLOWED_GROUP_CIPHERS \
(WPA_CIPHER_CCMP | WPA_CIPHER_GCMP | WPA_CIPHER_TKIP | \
WPA_CIPHER_GCMP_256 | WPA_CIPHER_CCMP_256 | \
WPA_CIPHER_GTK_NOT_USED)
#define WPA_ALLOWED_GROUP_MGMT_CIPHERS \
(WPA_CIPHER_AES_128_CMAC | WPA_CIPHER_BIP_GMAC_128 | WPA_CIPHER_BIP_GMAC_256 | \
WPA_CIPHER_BIP_CMAC_256)

#define WPA_SELECTOR_LEN 4
#define WPA_VERSION 1
#define RSN_SELECTOR_LEN 4
#define RSN_VERSION 1

#define RSN_SELECTOR(a, b, c, d) \
	((((u32) (a)) << 24) | (((u32) (b)) << 16) | (((u32) (c)) << 8) | \
	 (u32) (d))

#define WPA_AUTH_KEY_MGMT_NONE RSN_SELECTOR(0x00, 0x50, 0xf2, 0)
#define WPA_AUTH_KEY_MGMT_UNSPEC_802_1X RSN_SELECTOR(0x00, 0x50, 0xf2, 1)
#define WPA_AUTH_KEY_MGMT_PSK_OVER_802_1X RSN_SELECTOR(0x00, 0x50, 0xf2, 2)
#define WPA_AUTH_KEY_MGMT_CCKM RSN_SELECTOR(0x00, 0x40, 0x96, 0)
#define WPA_CIPHER_SUITE_NONE RSN_SELECTOR(0x00, 0x50, 0xf2, 0)
#define WPA_CIPHER_SUITE_TKIP RSN_SELECTOR(0x00, 0x50, 0xf2, 2)
#define WPA_CIPHER_SUITE_CCMP RSN_SELECTOR(0x00, 0x50, 0xf2, 4)


#define RSN_AUTH_KEY_MGMT_UNSPEC_802_1X RSN_SELECTOR(0x00, 0x0f, 0xac, 1)
#define RSN_AUTH_KEY_MGMT_PSK_OVER_802_1X RSN_SELECTOR(0x00, 0x0f, 0xac, 2)
#define RSN_AUTH_KEY_MGMT_FT_802_1X RSN_SELECTOR(0x00, 0x0f, 0xac, 3)
#define RSN_AUTH_KEY_MGMT_FT_PSK RSN_SELECTOR(0x00, 0x0f, 0xac, 4)
#define RSN_AUTH_KEY_MGMT_802_1X_SHA256 RSN_SELECTOR(0x00, 0x0f, 0xac, 5)
#define RSN_AUTH_KEY_MGMT_PSK_SHA256 RSN_SELECTOR(0x00, 0x0f, 0xac, 6)
#define RSN_AUTH_KEY_MGMT_TPK_HANDSHAKE RSN_SELECTOR(0x00, 0x0f, 0xac, 7)
#define RSN_AUTH_KEY_MGMT_SAE RSN_SELECTOR(0x00, 0x0f, 0xac, 8)
#define RSN_AUTH_KEY_MGMT_FT_SAE RSN_SELECTOR(0x00, 0x0f, 0xac, 9)
#define RSN_AUTH_KEY_MGMT_802_1X_SUITE_B RSN_SELECTOR(0x00, 0x0f, 0xac, 11)
#define RSN_AUTH_KEY_MGMT_802_1X_SUITE_B_192 RSN_SELECTOR(0x00, 0x0f, 0xac, 12)
#define RSN_AUTH_KEY_MGMT_FT_802_1X_SHA384 RSN_SELECTOR(0x00, 0x0f, 0xac, 13)
#define RSN_AUTH_KEY_MGMT_FILS_SHA256 RSN_SELECTOR(0x00, 0x0f, 0xac, 14)
#define RSN_AUTH_KEY_MGMT_FILS_SHA384 RSN_SELECTOR(0x00, 0x0f, 0xac, 15)
#define RSN_AUTH_KEY_MGMT_FT_FILS_SHA256 RSN_SELECTOR(0x00, 0x0f, 0xac, 16)
#define RSN_AUTH_KEY_MGMT_FT_FILS_SHA384 RSN_SELECTOR(0x00, 0x0f, 0xac, 17)
#define RSN_AUTH_KEY_MGMT_OWE RSN_SELECTOR(0x00, 0x0f, 0xac, 18)
#define RSN_AUTH_KEY_MGMT_CCKM RSN_SELECTOR(0x00, 0x40, 0x96, 0x00)
#define RSN_AUTH_KEY_MGMT_OSEN RSN_SELECTOR(0x50, 0x6f, 0x9a, 0x01)
#define RSN_AUTH_KEY_MGMT_DPP RSN_SELECTOR(0x50, 0x6f, 0x9a, 0x02)

#define RSN_CIPHER_SUITE_NONE RSN_SELECTOR(0x00, 0x0f, 0xac, 0)
#define RSN_CIPHER_SUITE_WEP40 RSN_SELECTOR(0x00, 0x0f, 0xac, 1)
#define RSN_CIPHER_SUITE_TKIP RSN_SELECTOR(0x00, 0x0f, 0xac, 2)
#if 0
#define RSN_CIPHER_SUITE_WRAP RSN_SELECTOR(0x00, 0x0f, 0xac, 3)
#endif
#define RSN_CIPHER_SUITE_CCMP RSN_SELECTOR(0x00, 0x0f, 0xac, 4)
#define RSN_CIPHER_SUITE_WEP104 RSN_SELECTOR(0x00, 0x0f, 0xac, 5)
#define RSN_CIPHER_SUITE_AES_128_CMAC RSN_SELECTOR(0x00, 0x0f, 0xac, 6)
#define RSN_CIPHER_SUITE_NO_GROUP_ADDRESSED RSN_SELECTOR(0x00, 0x0f, 0xac, 7)
#define RSN_CIPHER_SUITE_GCMP RSN_SELECTOR(0x00, 0x0f, 0xac, 8)
#define RSN_CIPHER_SUITE_GCMP_256 RSN_SELECTOR(0x00, 0x0f, 0xac, 9)
#define RSN_CIPHER_SUITE_CCMP_256 RSN_SELECTOR(0x00, 0x0f, 0xac, 10)
#define RSN_CIPHER_SUITE_BIP_GMAC_128 RSN_SELECTOR(0x00, 0x0f, 0xac, 11)
#define RSN_CIPHER_SUITE_BIP_GMAC_256 RSN_SELECTOR(0x00, 0x0f, 0xac, 12)
#define RSN_CIPHER_SUITE_BIP_CMAC_256 RSN_SELECTOR(0x00, 0x0f, 0xac, 13)
#define RSN_CIPHER_SUITE_SMS4 RSN_SELECTOR(0x00, 0x14, 0x72, 1)
#define RSN_CIPHER_SUITE_CKIP RSN_SELECTOR(0x00, 0x40, 0x96, 0)
#define RSN_CIPHER_SUITE_CKIP_CMIC RSN_SELECTOR(0x00, 0x40, 0x96, 1)
#define RSN_CIPHER_SUITE_CMIC RSN_SELECTOR(0x00, 0x40, 0x96, 2)
/* KRK is defined for nl80211 use only */
#define RSN_CIPHER_SUITE_KRK RSN_SELECTOR(0x00, 0x40, 0x96, 255)

/* EAPOL-Key Key Data Encapsulation
 * GroupKey and PeerKey require encryption, otherwise, encryption is optional.
 */
#define RSN_KEY_DATA_GROUPKEY RSN_SELECTOR(0x00, 0x0f, 0xac, 1)
#if 0
#define RSN_KEY_DATA_STAKEY RSN_SELECTOR(0x00, 0x0f, 0xac, 2)
#endif
#define RSN_KEY_DATA_MAC_ADDR RSN_SELECTOR(0x00, 0x0f, 0xac, 3)
#define RSN_KEY_DATA_PMKID RSN_SELECTOR(0x00, 0x0f, 0xac, 4)
#ifdef CONFIG_IEEE80211W
#define RSN_KEY_DATA_IGTK RSN_SELECTOR(0x00, 0x0f, 0xac, 9)
#endif /* CONFIG_IEEE80211W */
#define RSN_KEY_DATA_KEYID RSN_SELECTOR(0x00, 0x0f, 0xac, 10)
#define RSN_KEY_DATA_MULTIBAND_GTK RSN_SELECTOR(0x00, 0x0f, 0xac, 11)
#define RSN_KEY_DATA_MULTIBAND_KEYID RSN_SELECTOR(0x00, 0x0f, 0xac, 12)

#define WFA_KEY_DATA_IP_ADDR_REQ RSN_SELECTOR(0x50, 0x6f, 0x9a, 4)
#define WFA_KEY_DATA_IP_ADDR_ALLOC RSN_SELECTOR(0x50, 0x6f, 0x9a, 5)

#define WPA_OUI_TYPE RSN_SELECTOR(0x00, 0x50, 0xf2, 1)

#define RSN_SELECTOR_PUT(a, val) WPA_PUT_BE32((u8 *) (a), (val))
#define RSN_SELECTOR_GET(a) WPA_GET_BE32((const u8 *) (a))

#define RSN_NUM_REPLAY_COUNTERS_1 0
#define RSN_NUM_REPLAY_COUNTERS_2 1
#define RSN_NUM_REPLAY_COUNTERS_4 2
#define RSN_NUM_REPLAY_COUNTERS_16 3


#ifdef _MSC_VER
#pragma pack(push, 1)
#endif /* _MSC_VER */

#ifdef CONFIG_IEEE80211W
#define WPA_IGTK_LEN 16
#define WPA_IGTK_MAX_LEN 32
#endif /* CONFIG_IEEE80211W */


/* IEEE 802.11, 7.3.2.25.3 RSN Capabilities */
#define WPA_CAPABILITY_PREAUTH BIT(0)
#define WPA_CAPABILITY_NO_PAIRWISE BIT(1)
/* B2-B3: PTKSA Replay Counter */
/* B4-B5: GTKSA Replay Counter */
#define WPA_CAPABILITY_MFPR BIT(6)
#define WPA_CAPABILITY_MFPC BIT(7)
/* B8: Reserved */
#define WPA_CAPABILITY_PEERKEY_ENABLED BIT(9)
#define WPA_CAPABILITY_SPP_A_MSDU_CAPABLE BIT(10)
#define WPA_CAPABILITY_SPP_A_MSDU_REQUIRED BIT(11)
#define WPA_CAPABILITY_PBAC BIT(12)
#define WPA_CAPABILITY_EXT_KEY_ID_FOR_UNICAST BIT(13)
/* B14-B15: Reserved */


/* IEEE 802.11r */
#define MOBILITY_DOMAIN_ID_LEN 2
#define FT_R0KH_ID_MAX_LEN 48
#define FT_R1KH_ID_LEN 6
#define WPA_PMK_NAME_LEN 16


/* IEEE 802.11, 8.5.2 EAPOL-Key frames */
#define WPA_KEY_INFO_TYPE_MASK ((u16) (BIT(0) | BIT(1) | BIT(2)))
#define WPA_KEY_INFO_TYPE_AKM_DEFINED 0
#define WPA_KEY_INFO_TYPE_HMAC_MD5_RC4 BIT(0)
#define WPA_KEY_INFO_TYPE_HMAC_SHA1_AES BIT(1)
#define WPA_KEY_INFO_TYPE_AES_128_CMAC 3
#define WPA_KEY_INFO_KEY_TYPE BIT(3) /* 1 = Pairwise, 0 = Group key */
/* bit4..5 is used in WPA, but is reserved in IEEE 802.11i/RSN */
#define WPA_KEY_INFO_KEY_INDEX_MASK (BIT(4) | BIT(5))
#define WPA_KEY_INFO_KEY_INDEX_SHIFT 4
#define WPA_KEY_INFO_INSTALL BIT(6) /* pairwise */
#define WPA_KEY_INFO_TXRX BIT(6) /* group */
#define WPA_KEY_INFO_ACK BIT(7)
#define WPA_KEY_INFO_MIC BIT(8)
#define WPA_KEY_INFO_SECURE BIT(9)
#define WPA_KEY_INFO_ERROR BIT(10)
#define WPA_KEY_INFO_REQUEST BIT(11)
#define WPA_KEY_INFO_ENCR_KEY_DATA BIT(12) /* IEEE 802.11i/RSN only */
#define WPA_KEY_INFO_SMK_MESSAGE BIT(13)


struct wpa_eapol_key {
	u8 type;
	/* Note: key_info, key_length, and key_data_length are unaligned */
	u8 key_info[2]; /* big endian */
	u8 key_length[2]; /* big endian */
	u8 replay_counter[WPA_REPLAY_COUNTER_LEN];
	u8 key_nonce[WPA_NONCE_LEN];
	u8 key_iv[16];
	u8 key_rsc[WPA_KEY_RSC_LEN];
	u8 key_id[8]; /* Reserved in IEEE 802.11i/RSN */
	/* variable length Key MIC field */
	/* big endian 2-octet Key Data Length field */
	/* followed by Key Data Length bytes of Key Data */
} STRUCT_PACKED;

#define WPA_EAPOL_KEY_MIC_MAX_LEN 32
#define WPA_KCK_MAX_LEN 32
#define WPA_KEK_MAX_LEN 64
#define WPA_TK_MAX_LEN 32
#define FILS_ICK_MAX_LEN 48
#define FILS_FT_MAX_LEN 48

/**
 * struct wpa_ptk - WPA Pairwise Transient Key
 * IEEE Std 802.11i-2004 - 8.5.1.2 Pairwise key hierarchy
 */
struct wpa_ptk {
	u8 kck[WPA_KCK_MAX_LEN]; /* EAPOL-Key Key Confirmation Key (KCK) */
	u8 kek[WPA_KEK_MAX_LEN]; /* EAPOL-Key Key Encryption Key (KEK) */
	u8 tk[WPA_TK_MAX_LEN]; /* Temporal Key (TK) */
	u8 kck2[WPA_KCK_MAX_LEN]; /* FT reasoc Key Confirmation Key (KCK2) */
	u8 kek2[WPA_KEK_MAX_LEN]; /* FT reassoc Key Encryption Key (KEK2) */
	size_t kck_len;
	size_t kek_len;
	size_t tk_len;
	size_t kck2_len;
	size_t kek2_len;
	int installed; /* 1 if key has already been installed to driver */
};

struct wpa_gtk {
	u8 gtk[WPA_GTK_MAX_LEN];
	size_t gtk_len;
};

#ifdef CONFIG_IEEE80211W
struct wpa_igtk {
	u8 igtk[WPA_IGTK_MAX_LEN];
	size_t igtk_len;
};
#endif /* CONFIG_IEEE80211W */

/* WPA IE version 1
 * 00-50-f2:1 (OUI:OUI type)
 * 0x01 0x00 (version; little endian)
 * (all following fields are optional:)
 * Group Suite Selector (4 octets) (default: TKIP)
 * Pairwise Suite Count (2 octets, little endian) (default: 1)
 * Pairwise Suite List (4 * n octets) (default: TKIP)
 * Authenticated Key Management Suite Count (2 octets, little endian)
 *    (default: 1)
 * Authenticated Key Management Suite List (4 * n octets)
 *    (default: unspec 802.1X)
 * WPA Capabilities (2 octets, little endian) (default: 0)
 */

struct wpa_ie_hdr {
	u8 elem_id;
	u8 len;
	u8 oui[4]; /* 24-bit OUI followed by 8-bit OUI type */
	u8 version[2]; /* little endian */
} STRUCT_PACKED;


/* 1/4: PMKID
 * 2/4: RSN IE
 * 3/4: one or two RSN IEs + GTK IE (encrypted)
 * 4/4: empty
 * 1/2: GTK IE (encrypted)
 * 2/2: empty
 */

/* RSN IE version 1
 * 0x01 0x00 (version; little endian)
 * (all following fields are optional:)
 * Group Suite Selector (4 octets) (default: CCMP)
 * Pairwise Suite Count (2 octets, little endian) (default: 1)
 * Pairwise Suite List (4 * n octets) (default: CCMP)
 * Authenticated Key Management Suite Count (2 octets, little endian)
 *    (default: 1)
 * Authenticated Key Management Suite List (4 * n octets)
 *    (default: unspec 802.1X)
 * RSN Capabilities (2 octets, little endian) (default: 0)
 * PMKID Count (2 octets) (default: 0)
 * PMKID List (16 * n octets)
 * Management Group Cipher Suite (4 octets) (default: AES-128-CMAC)
 */

struct rsn_ie_hdr {
	u8 elem_id; /* WLAN_EID_RSN */
	u8 len;
	u8 version[2]; /* little endian */
} STRUCT_PACKED;


struct rsn_error_kde {
	be16 mui;
	be16 error_type;
} STRUCT_PACKED;

#ifdef CONFIG_IEEE80211W
#define WPA_IGTK_KDE_PREFIX_LEN (2 + 6)
struct wpa_igtk_kde {
	u8 keyid[2];
	u8 pn[6];
	u8 igtk[WPA_IGTK_MAX_LEN];
} STRUCT_PACKED;
#endif /* CONFIG_IEEE80211W */

struct rsn_mdie {
	u8 mobility_domain[MOBILITY_DOMAIN_ID_LEN];
	u8 ft_capab;
} STRUCT_PACKED;

#define RSN_FT_CAPAB_FT_OVER_DS BIT(0)
#define RSN_FT_CAPAB_FT_RESOURCE_REQ_SUPP BIT(1)

struct rsn_ftie {
	u8 mic_control[2];
	u8 mic[16];
	u8 anonce[WPA_NONCE_LEN];
	u8 snonce[WPA_NONCE_LEN];
	/* followed by optional parameters */
} STRUCT_PACKED;

struct rsn_ftie_sha384 {
	u8 mic_control[2];
	u8 mic[24];
	u8 anonce[WPA_NONCE_LEN];
	u8 snonce[WPA_NONCE_LEN];
	/* followed by optional parameters */
} STRUCT_PACKED;

#define FTIE_SUBELEM_R1KH_ID 1
#define FTIE_SUBELEM_GTK 2
#define FTIE_SUBELEM_R0KH_ID 3
#define FTIE_SUBELEM_IGTK 4

struct rsn_rdie {
	u8 id;
	u8 descr_count;
	le16 status_code;
} STRUCT_PACKED;


#ifdef _MSC_VER
#pragma pack(pop)
#endif /* _MSC_VER */


int wpa_eapol_key_mic(const u8 *key, size_t key_len, int akmp, int ver,
		      const u8 *buf, size_t len, u8 *mic);
int wpa_pmk_to_ptk(const u8 *pmk, size_t pmk_len, const char *label,
		   const u8 *addr1, const u8 *addr2,
		   const u8 *nonce1, const u8 *nonce2,
		   struct wpa_ptk *ptk, int akmp, int cipher);
int fils_rmsk_to_pmk(int akmp, const u8 *rmsk, size_t rmsk_len,
		     const u8 *snonce, const u8 *anonce, const u8 *dh_ss,
		     size_t dh_ss_len, u8 *pmk, size_t *pmk_len);
int fils_pmkid_erp(int akmp, const u8 *reauth, size_t reauth_len,
		   u8 *pmkid);
int fils_pmk_to_ptk(const u8 *pmk, size_t pmk_len, const u8 *spa, const u8 *aa,
		    const u8 *snonce, const u8 *anonce, const u8 *dhss,
		    size_t dhss_len, struct wpa_ptk *ptk,
		    u8 *ick, size_t *ick_len, int akmp, int cipher,
		    u8 *fils_ft, size_t *fils_ft_len);
int fils_key_auth_sk(const u8 *ick, size_t ick_len, const u8 *snonce,
		     const u8 *anonce, const u8 *sta_addr, const u8 *bssid,
		     const u8 *g_sta, size_t g_sta_len,
		     const u8 *g_ap, size_t g_ap_len,
		     int akmp, u8 *key_auth_sta, u8 *key_auth_ap,
		     size_t *key_auth_len);

#ifdef CONFIG_IEEE80211R
int wpa_ft_mic(const u8 *kck, size_t kck_len, const u8 *sta_addr,
	       const u8 *ap_addr, u8 transaction_seqnum,
	       const u8 *mdie, size_t mdie_len,
	       const u8 *ftie, size_t ftie_len,
	       const u8 *rsnie, size_t rsnie_len,
	       const u8 *ric, size_t ric_len, u8 *mic);
int wpa_derive_pmk_r0(const u8 *xxkey, size_t xxkey_len,
		      const u8 *ssid, size_t ssid_len,
		      const u8 *mdid, const u8 *r0kh_id, size_t r0kh_id_len,
		      const u8 *s0kh_id, u8 *pmk_r0, u8 *pmk_r0_name,
		      int use_sha384);
int wpa_derive_pmk_r1_name(const u8 *pmk_r0_name, const u8 *r1kh_id,
			   const u8 *s1kh_id, u8 *pmk_r1_name, int use_sha384);
int wpa_derive_pmk_r1(const u8 *pmk_r0, size_t pmk_r0_len,
		      const u8 *pmk_r0_name,
		      const u8 *r1kh_id, const u8 *s1kh_id,
		      u8 *pmk_r1, u8 *pmk_r1_name);
int wpa_pmk_r1_to_ptk(const u8 *pmk_r1, size_t pmk_r1_len, const u8 *snonce,
		      const u8 *anonce, const u8 *sta_addr, const u8 *bssid,
		      const u8 *pmk_r1_name,
		      struct wpa_ptk *ptk, u8 *ptk_name, int akmp, int cipher);
#endif /* CONFIG_IEEE80211R */

struct wpa_ie_data {
	int proto;
	int pairwise_cipher;
	int group_cipher;
	int key_mgmt;
	int capabilities;
	size_t num_pmkid;
	const u8 *pmkid;
	int mgmt_group_cipher;
};


int wpa_parse_wpa_ie_rsn(const u8 *rsn_ie, size_t rsn_ie_len,
			 struct wpa_ie_data *data);
int wpa_parse_wpa_ie_wpa(const u8 *wpa_ie, size_t wpa_ie_len,
			 struct wpa_ie_data *data);

void rsn_pmkid(const u8 *pmk, size_t pmk_len, const u8 *aa, const u8 *spa,
	       u8 *pmkid, int akmp);
#ifdef CONFIG_SUITEB
int rsn_pmkid_suite_b(const u8 *kck, size_t kck_len, const u8 *aa,
		       const u8 *spa, u8 *pmkid);
#else /* CONFIG_SUITEB */
static inline int rsn_pmkid_suite_b(const u8 *kck, size_t kck_len, const u8 *aa,
				    const u8 *spa, u8 *pmkid)
{
	return -1;
}
#endif /* CONFIG_SUITEB */
#ifdef CONFIG_SUITEB192
int rsn_pmkid_suite_b_192(const u8 *kck, size_t kck_len, const u8 *aa,
			  const u8 *spa, u8 *pmkid);
#else /* CONFIG_SUITEB192 */
static inline int rsn_pmkid_suite_b_192(const u8 *kck, size_t kck_len,
					const u8 *aa, const u8 *spa, u8 *pmkid)
{
	return -1;
}
#endif /* CONFIG_SUITEB192 */

const char * wpa_cipher_txt(int cipher);
const char * wpa_key_mgmt_txt(int key_mgmt, int proto);
u32 wpa_akm_to_suite(int akm);
int wpa_compare_rsn_ie(int ft_initial_assoc,
		       const u8 *ie1, size_t ie1len,
		       const u8 *ie2, size_t ie2len);
int wpa_insert_pmkid(u8 *ies, size_t *ies_len, const u8 *pmkid);

struct wpa_ft_ies {
	const u8 *mdie;
	size_t mdie_len;
	const u8 *ftie;
	size_t ftie_len;
	const u8 *r1kh_id;
	const u8 *gtk;
	size_t gtk_len;
	const u8 *r0kh_id;
	size_t r0kh_id_len;
	const u8 *rsn;
	size_t rsn_len;
	const u8 *rsn_pmkid;
	const u8 *tie;
	size_t tie_len;
	const u8 *igtk;
	size_t igtk_len;
	const u8 *ric;
	size_t ric_len;
	int key_mgmt;
	int pairwise_cipher;
};

int wpa_ft_parse_ies(const u8 *ies, size_t ies_len, struct wpa_ft_ies *parse,
		     int use_sha384);

int wpa_cipher_key_len(int cipher);
int wpa_cipher_rsc_len(int cipher);
enum wpa_alg wpa_cipher_to_alg(int cipher);
int wpa_cipher_valid_group(int cipher);
int wpa_cipher_valid_pairwise(int cipher);
int wpa_cipher_valid_mgmt_group(int cipher);
u32 wpa_cipher_to_suite(int proto, int cipher);
int rsn_cipher_put_suites(u8 *pos, int ciphers);
int wpa_cipher_put_suites(u8 *pos, int ciphers);
int wpa_pick_pairwise_cipher(int ciphers, int none_allowed);
int wpa_pick_group_cipher(int ciphers);
int wpa_parse_cipher(const char *value);
int wpa_write_ciphers(char *start, char *end, int ciphers, const char *delim);
int wpa_select_ap_group_cipher(int wpa, int wpa_pairwise, int rsn_pairwise);
unsigned int wpa_mic_len(int akmp, size_t pmk_len);
int wpa_use_akm_defined(int akmp);
int wpa_use_cmac(int akmp);
int wpa_use_aes_key_wrap(int akmp);
int fils_domain_name_hash(const char *domain, u8 *hash);

#endif /* WPA_COMMON_H */
