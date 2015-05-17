/*
 * Fundamental types and constants relating to WPA
 *
 * $Copyright Open Broadcom Corporation$
 *
 * $Id: wpa.h 492853 2014-07-23 17:20:34Z $
 */

#ifndef _proto_wpa_h_
#define _proto_wpa_h_

#include <typedefs.h>
#include <proto/ethernet.h>


/* This marks the start of a packed structure section. */
#include <packed_section_start.h>

/* Reason Codes */

/* 13 through 23 taken from IEEE Std 802.11i-2004 */
#define DOT11_RC_INVALID_WPA_IE		13	/* Invalid info. element */
#define DOT11_RC_MIC_FAILURE		14	/* Michael failure */
#define DOT11_RC_4WH_TIMEOUT		15	/* 4-way handshake timeout */
#define DOT11_RC_GTK_UPDATE_TIMEOUT	16	/* Group key update timeout */
#define DOT11_RC_WPA_IE_MISMATCH	17	/* WPA IE in 4-way handshake differs from
						 * (re-)assoc. request/probe response
						 */
#define DOT11_RC_INVALID_MC_CIPHER	18	/* Invalid multicast cipher */
#define DOT11_RC_INVALID_UC_CIPHER	19	/* Invalid unicast cipher */
#define DOT11_RC_INVALID_AKMP		20	/* Invalid authenticated key management protocol */
#define DOT11_RC_BAD_WPA_VERSION	21	/* Unsupported WPA version */
#define DOT11_RC_INVALID_WPA_CAP	22	/* Invalid WPA IE capabilities */
#define DOT11_RC_8021X_AUTH_FAIL	23	/* 802.1X authentication failure */

#define WPA2_PMKID_LEN	16

/* WPA IE fixed portion */
typedef BWL_PRE_PACKED_STRUCT struct
{
	uint8 tag;	/* TAG */
	uint8 length;	/* TAG length */
	uint8 oui[3];	/* IE OUI */
	uint8 oui_type;	/* OUI type */
	BWL_PRE_PACKED_STRUCT struct {
		uint8 low;
		uint8 high;
	} BWL_POST_PACKED_STRUCT version;	/* IE version */
} BWL_POST_PACKED_STRUCT wpa_ie_fixed_t;
#define WPA_IE_OUITYPE_LEN	4
#define WPA_IE_FIXED_LEN	8
#define WPA_IE_TAG_FIXED_LEN	6

typedef BWL_PRE_PACKED_STRUCT struct {
	uint8 tag;	/* TAG */
	uint8 length;	/* TAG length */
	BWL_PRE_PACKED_STRUCT struct {
		uint8 low;
		uint8 high;
	} BWL_POST_PACKED_STRUCT version;	/* IE version */
} BWL_POST_PACKED_STRUCT wpa_rsn_ie_fixed_t;
#define WPA_RSN_IE_FIXED_LEN	4
#define WPA_RSN_IE_TAG_FIXED_LEN	2
typedef uint8 wpa_pmkid_t[WPA2_PMKID_LEN];

#define WFA_OSEN_IE_FIXED_LEN	6

/* WPA suite/multicast suite */
typedef BWL_PRE_PACKED_STRUCT struct
{
	uint8 oui[3];
	uint8 type;
} BWL_POST_PACKED_STRUCT wpa_suite_t, wpa_suite_mcast_t;
#define WPA_SUITE_LEN	4

/* WPA unicast suite list/key management suite list */
typedef BWL_PRE_PACKED_STRUCT struct
{
	BWL_PRE_PACKED_STRUCT struct {
		uint8 low;
		uint8 high;
	} BWL_POST_PACKED_STRUCT count;
	wpa_suite_t list[1];
} BWL_POST_PACKED_STRUCT wpa_suite_ucast_t, wpa_suite_auth_key_mgmt_t;
#define WPA_IE_SUITE_COUNT_LEN	2
typedef BWL_PRE_PACKED_STRUCT struct
{
	BWL_PRE_PACKED_STRUCT struct {
		uint8 low;
		uint8 high;
	} BWL_POST_PACKED_STRUCT count;
	wpa_pmkid_t list[1];
} BWL_POST_PACKED_STRUCT wpa_pmkid_list_t;

/* WPA cipher suites */
#define WPA_CIPHER_NONE		0	/* None */
#define WPA_CIPHER_WEP_40	1	/* WEP (40-bit) */
#define WPA_CIPHER_TKIP		2	/* TKIP: default for WPA */
#define WPA_CIPHER_AES_OCB	3	/* AES (OCB) */
#define WPA_CIPHER_AES_CCM	4	/* AES (CCM) */
#define WPA_CIPHER_WEP_104	5	/* WEP (104-bit) */
#define WPA_CIPHER_BIP		6	/* WEP (104-bit) */
#define WPA_CIPHER_TPK		7	/* Group addressed traffic not allowed */
#ifdef BCMCCX
#define WPA_CIPHER_CKIP		8	/* KP with no MIC */
#define WPA_CIPHER_CKIP_MMH	9	/* KP with MIC ("CKIP/MMH", "CKIP+CMIC") */
#define WPA_CIPHER_WEP_MMH	10	/* MIC with no KP ("WEP/MMH", "CMIC") */

#define IS_CCX_CIPHER(cipher)	((cipher) == WPA_CIPHER_CKIP || \
				 (cipher) == WPA_CIPHER_CKIP_MMH || \
				 (cipher) == WPA_CIPHER_WEP_MMH)
#endif

#ifdef BCMWAPI_WAI
#define WAPI_CIPHER_NONE	WPA_CIPHER_NONE
#define WAPI_CIPHER_SMS4	11

#define WAPI_CSE_WPI_SMS4	1
#endif /* BCMWAPI_WAI */

#define IS_WPA_CIPHER(cipher)	((cipher) == WPA_CIPHER_NONE || \
				 (cipher) == WPA_CIPHER_WEP_40 || \
				 (cipher) == WPA_CIPHER_WEP_104 || \
				 (cipher) == WPA_CIPHER_TKIP || \
				 (cipher) == WPA_CIPHER_AES_OCB || \
				 (cipher) == WPA_CIPHER_AES_CCM || \
				 (cipher) == WPA_CIPHER_TPK)

#ifdef BCMWAPI_WAI
#define IS_WAPI_CIPHER(cipher)	((cipher) == WAPI_CIPHER_NONE || \
				 (cipher) == WAPI_CSE_WPI_SMS4)

/* convert WAPI_CSE_WPI_XXX to WAPI_CIPHER_XXX */
#define WAPI_CSE_WPI_2_CIPHER(cse) ((cse) == WAPI_CSE_WPI_SMS4 ? \
				WAPI_CIPHER_SMS4 : WAPI_CIPHER_NONE)

#define WAPI_CIPHER_2_CSE_WPI(cipher) ((cipher) == WAPI_CIPHER_SMS4 ? \
				WAPI_CSE_WPI_SMS4 : WAPI_CIPHER_NONE)
#endif /* BCMWAPI_WAI */

/* WPA TKIP countermeasures parameters */
#define WPA_TKIP_CM_DETECT	60	/* multiple MIC failure window (seconds) */
#define WPA_TKIP_CM_BLOCK	60	/* countermeasures active window (seconds) */

/* RSN IE defines */
#define RSN_CAP_LEN		2	/* Length of RSN capabilities field (2 octets) */

/* RSN Capabilities defined in 802.11i */
#define RSN_CAP_PREAUTH			0x0001
#define RSN_CAP_NOPAIRWISE		0x0002
#define RSN_CAP_PTK_REPLAY_CNTR_MASK	0x000C
#define RSN_CAP_PTK_REPLAY_CNTR_SHIFT	2
#define RSN_CAP_GTK_REPLAY_CNTR_MASK	0x0030
#define RSN_CAP_GTK_REPLAY_CNTR_SHIFT	4
#define RSN_CAP_1_REPLAY_CNTR		0
#define RSN_CAP_2_REPLAY_CNTRS		1
#define RSN_CAP_4_REPLAY_CNTRS		2
#define RSN_CAP_16_REPLAY_CNTRS		3
#define RSN_CAP_MFPR			0x0040
#define RSN_CAP_MFPC			0x0080
#define RSN_CAP_SPPC			0x0400
#define RSN_CAP_SPPR			0x0800

/* WPA capabilities defined in 802.11i */
#define WPA_CAP_4_REPLAY_CNTRS		RSN_CAP_4_REPLAY_CNTRS
#define WPA_CAP_16_REPLAY_CNTRS		RSN_CAP_16_REPLAY_CNTRS
#define WPA_CAP_REPLAY_CNTR_SHIFT	RSN_CAP_PTK_REPLAY_CNTR_SHIFT
#define WPA_CAP_REPLAY_CNTR_MASK	RSN_CAP_PTK_REPLAY_CNTR_MASK

/* WPA capabilities defined in 802.11zD9.0 */
#define WPA_CAP_PEER_KEY_ENABLE		(0x1 << 1)	/* bit 9 */

/* WPA Specific defines */
#define WPA_CAP_LEN	RSN_CAP_LEN	/* Length of RSN capabilities in RSN IE (2 octets) */
#define WPA_PMKID_CNT_LEN	2 	/* Length of RSN PMKID count (2 octests) */

#define	WPA_CAP_WPA2_PREAUTH		RSN_CAP_PREAUTH

#define WPA2_PMKID_COUNT_LEN	2
#define RSN_GROUPMANAGE_CIPHER_LEN 4

#ifdef BCMWAPI_WAI
#define WAPI_CAP_PREAUTH		RSN_CAP_PREAUTH

/* Other WAI definition */
#define WAPI_WAI_REQUEST		0x00F1
#define WAPI_UNICAST_REKEY		0x00F2
#define WAPI_STA_AGING			0x00F3
#define WAPI_MUTIL_REKEY		0x00F4
#define WAPI_STA_STATS			0x00F5

#define WAPI_USK_REKEY_COUNT		0x4000000 /* 0xA00000 */
#define WAPI_MSK_REKEY_COUNT		0x4000000 /* 0xA00000 */
#endif /* BCMWAPI_WAI */

/* This marks the end of a packed structure section. */
#include <packed_section_end.h>

#endif /* _proto_wpa_h_ */
