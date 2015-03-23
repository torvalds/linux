/*
 * WPA definitions shared between hostapd and wpa_supplicant
 * Copyright (c) 2002-2008, Jouni Malinen <j@w1.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Alternatively, this software may be distributed under the terms of BSD
 * license.
 *
 * See README and COPYING for more details.
 */

#ifndef WPA_COMMON_H
#define WPA_COMMON_H

#define WPA_MAX_SSID_LEN 32

/* IEEE 802.11i */
#define PMKID_LEN 16
#define PMK_LEN 32
#define WPA_REPLAY_COUNTER_LEN 8
#define WPA_NONCE_LEN 32
#define WPA_KEY_RSC_LEN 8
#define WPA_GMK_LEN 32
#define WPA_GTK_MAX_LEN 32

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
#define WPA_CIPHER_SUITE_NONE RSN_SELECTOR(0x00, 0x50, 0xf2, 0)
#define WPA_CIPHER_SUITE_WEP40 RSN_SELECTOR(0x00, 0x50, 0xf2, 1)
#define WPA_CIPHER_SUITE_TKIP RSN_SELECTOR(0x00, 0x50, 0xf2, 2)
#if 0
#define WPA_CIPHER_SUITE_WRAP RSN_SELECTOR(0x00, 0x50, 0xf2, 3)
#endif
#define WPA_CIPHER_SUITE_CCMP RSN_SELECTOR(0x00, 0x50, 0xf2, 4)
#define WPA_CIPHER_SUITE_WEP104 RSN_SELECTOR(0x00, 0x50, 0xf2, 5)


#define RSN_AUTH_KEY_MGMT_UNSPEC_802_1X RSN_SELECTOR(0x00, 0x0f, 0xac, 1)
#define RSN_AUTH_KEY_MGMT_PSK_OVER_802_1X RSN_SELECTOR(0x00, 0x0f, 0xac, 2)
#ifdef CONFIG_IEEE80211R
#define RSN_AUTH_KEY_MGMT_FT_802_1X RSN_SELECTOR(0x00, 0x0f, 0xac, 3)
#define RSN_AUTH_KEY_MGMT_FT_PSK RSN_SELECTOR(0x00, 0x0f, 0xac, 4)
#endif /* CONFIG_IEEE80211R */
#define RSN_AUTH_KEY_MGMT_802_1X_SHA256 RSN_SELECTOR(0x00, 0x0f, 0xac, 5)
#define RSN_AUTH_KEY_MGMT_PSK_SHA256 RSN_SELECTOR(0x00, 0x0f, 0xac, 6)
#define RSN_AUTH_KEY_MGMT_TPK_HANDSHAKE RSN_SELECTOR(0x00, 0x0f, 0xac, 7)

#define RSN_CIPHER_SUITE_NONE RSN_SELECTOR(0x00, 0x0f, 0xac, 0)
#define RSN_CIPHER_SUITE_WEP40 RSN_SELECTOR(0x00, 0x0f, 0xac, 1)
#define RSN_CIPHER_SUITE_TKIP RSN_SELECTOR(0x00, 0x0f, 0xac, 2)
#if 0
#define RSN_CIPHER_SUITE_WRAP RSN_SELECTOR(0x00, 0x0f, 0xac, 3)
#endif
#define RSN_CIPHER_SUITE_CCMP RSN_SELECTOR(0x00, 0x0f, 0xac, 4)
#define RSN_CIPHER_SUITE_WEP104 RSN_SELECTOR(0x00, 0x0f, 0xac, 5)
#ifdef CONFIG_IEEE80211W
#define RSN_CIPHER_SUITE_AES_128_CMAC RSN_SELECTOR(0x00, 0x0f, 0xac, 6)
#endif /* CONFIG_IEEE80211W */
#define RSN_CIPHER_SUITE_NO_GROUP_ADDRESSED RSN_SELECTOR(0x00, 0x0f, 0xac, 7)

/* EAPOL-Key Key Data Encapsulation
 * GroupKey and PeerKey require encryption, otherwise, encryption is optional.
 */
#define RSN_KEY_DATA_GROUPKEY RSN_SELECTOR(0x00, 0x0f, 0xac, 1)
#if 0
#define RSN_KEY_DATA_STAKEY RSN_SELECTOR(0x00, 0x0f, 0xac, 2)
#endif
#define RSN_KEY_DATA_MAC_ADDR RSN_SELECTOR(0x00, 0x0f, 0xac, 3)
#define RSN_KEY_DATA_PMKID RSN_SELECTOR(0x00, 0x0f, 0xac, 4)
#ifdef CONFIG_PEERKEY
#define RSN_KEY_DATA_SMK RSN_SELECTOR(0x00, 0x0f, 0xac, 5)
#define RSN_KEY_DATA_NONCE RSN_SELECTOR(0x00, 0x0f, 0xac, 6)
#define RSN_KEY_DATA_LIFETIME RSN_SELECTOR(0x00, 0x0f, 0xac, 7)
#define RSN_KEY_DATA_ERROR RSN_SELECTOR(0x00, 0x0f, 0xac, 8)
#endif /* CONFIG_PEERKEY */
#ifdef CONFIG_IEEE80211W
#define RSN_KEY_DATA_IGTK RSN_SELECTOR(0x00, 0x0f, 0xac, 9)
#endif /* CONFIG_IEEE80211W */

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
	u8 key_mic[16];
	u8 key_data_length[2]; /* big endian */
	/* followed by key_data_length bytes of key_data */
} STRUCT_PACKED;

/**
 * struct wpa_ptk - WPA Pairwise Transient Key
 * IEEE Std 802.11i-2004 - 8.5.1.2 Pairwise key hierarchy
 */
struct wpa_ptk {
	u8 kck[16]; /* EAPOL-Key Key Confirmation Key (KCK) */
	u8 kek[16]; /* EAPOL-Key Key Encryption Key (KEK) */
	u8 tk1[16]; /* Temporal Key 1 (TK1) */
	union {
		u8 tk2[16]; /* Temporal Key 2 (TK2) */
		struct {
			u8 tx_mic_key[8];
			u8 rx_mic_key[8];
		} auth;
	} u;
} STRUCT_PACKED;


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


#ifdef CONFIG_PEERKEY
enum {
	STK_MUI_4WAY_STA_AP = 1,
	STK_MUI_4WAY_STAT_STA = 2,
	STK_MUI_GTK = 3,
	STK_MUI_SMK = 4
};

enum {
	STK_ERR_STA_NR = 1,
	STK_ERR_STA_NRSN = 2,
	STK_ERR_CPHR_NS = 3,
	STK_ERR_NO_STSL = 4
};
#endif /* CONFIG_PEERKEY */

struct rsn_error_kde {
	be16 mui;
	be16 error_type;
} STRUCT_PACKED;

#ifdef CONFIG_IEEE80211W
struct wpa_igtk_kde {
	u8 keyid[2];
	u8 pn[6];
	u8 igtk[WPA_IGTK_LEN];
} STRUCT_PACKED;
#endif /* CONFIG_IEEE80211W */

#ifdef CONFIG_IEEE80211R
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

#define FTIE_SUBELEM_R1KH_ID 1
#define FTIE_SUBELEM_GTK 2
#define FTIE_SUBELEM_R0KH_ID 3
#define FTIE_SUBELEM_IGTK 4

struct rsn_rdie {
	u8 id;
	u8 descr_count;
	le16 status_code;
} STRUCT_PACKED;

#endif /* CONFIG_IEEE80211R */

#ifdef _MSC_VER
#pragma pack(pop)
#endif /* _MSC_VER */


int wpa_eapol_key_mic(const u8 *key, int ver, const u8 *buf, size_t len,
		      u8 *mic);
void wpa_pmk_to_ptk(const u8 *pmk, size_t pmk_len, const char *label,
		    const u8 *addr1, const u8 *addr2,
		    const u8 *nonce1, const u8 *nonce2,
		    u8 *ptk, size_t ptk_len, int use_sha256);

#ifdef CONFIG_IEEE80211R
int wpa_ft_mic(const u8 *kck, const u8 *sta_addr, const u8 *ap_addr,
	       u8 transaction_seqnum, const u8 *mdie, size_t mdie_len,
	       const u8 *ftie, size_t ftie_len,
	       const u8 *rsnie, size_t rsnie_len,
	       const u8 *ric, size_t ric_len, u8 *mic);
void wpa_derive_pmk_r0(const u8 *xxkey, size_t xxkey_len,
		       const u8 *ssid, size_t ssid_len,
		       const u8 *mdid, const u8 *r0kh_id, size_t r0kh_id_len,
		       const u8 *s0kh_id, u8 *pmk_r0, u8 *pmk_r0_name);
void wpa_derive_pmk_r1_name(const u8 *pmk_r0_name, const u8 *r1kh_id,
			    const u8 *s1kh_id, u8 *pmk_r1_name);
void wpa_derive_pmk_r1(const u8 *pmk_r0, const u8 *pmk_r0_name,
		       const u8 *r1kh_id, const u8 *s1kh_id,
		       u8 *pmk_r1, u8 *pmk_r1_name);
void wpa_pmk_r1_to_ptk(const u8 *pmk_r1, const u8 *snonce, const u8 *anonce,
		       const u8 *sta_addr, const u8 *bssid,
		       const u8 *pmk_r1_name,
		       u8 *ptk, size_t ptk_len, u8 *ptk_name);
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
	       u8 *pmkid, int use_sha256);

const char * wpa_cipher_txt(int cipher);
const char * wpa_key_mgmt_txt(int key_mgmt, int proto);
int wpa_compare_rsn_ie(int ft_initial_assoc,
		       const u8 *ie1, size_t ie1len,
		       const u8 *ie2, size_t ie2len);
int wpa_insert_pmkid(u8 *ies, size_t ies_len, const u8 *pmkid);

#endif /* WPA_COMMON_H */
