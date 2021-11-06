/*
 * bcmwpa.h - interface definitions of shared WPA-related functions
 *
 * Broadcom Proprietary and Confidential. Copyright (C) 2020,
 * All Rights Reserved.
 *
 * This is UNPUBLISHED PROPRIETARY SOURCE CODE of Broadcom;
 * the contents of this file may not be disclosed to third parties,
 * copied or duplicated in any form, in whole or in part, without
 * the prior written permission of Broadcom.
 *
 *
 * <<Broadcom-WL-IPTag/Proprietary:>>
 */

#ifndef _BCMWPA_H_
#define _BCMWPA_H_
#ifdef BCM_EXTERNAL_APP
typedef int osl_t;
#endif
#include <wpa.h>
#if defined(BCMSUP_PSK) || defined(BCMSUPPL) || \
	defined(MFP) || defined(BCMAUTH_PSK) || defined(WLFBT) || \
    defined(WL_OKC) || defined(GTKOE) || defined(WL_FILS)
#include <eapol.h>
#endif
#include <802.11.h>
#ifdef WLP2P
#include <p2p.h>
#endif
#include <rc4.h>
#include <bcmutils.h>
#include <wlioctl.h>
#include <sha2.h>
#ifdef WL_OCV
#include <bcm_ocv.h>
#endif /* WL_OCV */

/* Field sizes for WPA key hierarchy */
#define WPA_TEMP_TX_KEY_LEN		8u
#define WPA_TEMP_RX_KEY_LEN		8u

#define PMK_LEN				32u
#define TKIP_PTK_LEN			64u
#define TKIP_TK_LEN			32u
#define AES_PTK_LEN			48u
#define AES_TK_LEN			16u
#define AES_GCM_PTK_LEN			48u
#define AES_GCM_TK_LEN			16u
#define AES_GCM256_PTK_LEN		64u
#define AES_GCM256_TK_LEN		32u

/* limits for pre-shared key lengths */
#define WPA_MIN_PSK_LEN			8u
#define WPA_MAX_PSK_LEN			64u

#define WPA_KEY_DATA_LEN_256		256u	/* allocation size of 256 for temp data pointer. */
#define WPA_KEY_DATA_LEN_128		128u	/* allocation size of 128 for temp data pointer. */

/* Minimum length of WPA2 GTK encapsulation in EAPOL */
#define EAPOL_WPA2_GTK_ENCAP_MIN_LEN  (EAPOL_WPA2_ENCAP_DATA_HDR_LEN - \
	TLV_HDR_LEN + EAPOL_WPA2_KEY_GTK_ENCAP_HDR_LEN)

/* Minimum length of WPA2 IGTK encapsulation in EAPOL */
#define EAPOL_WPA2_IGTK_ENCAP_MIN_LEN  (EAPOL_WPA2_ENCAP_DATA_HDR_LEN - \
	TLV_HDR_LEN + EAPOL_WPA2_KEY_IGTK_ENCAP_HDR_LEN)

/* Minimum length of BIGTK encapsulation in EAPOL */
#define EAPOL_WPA2_BIGTK_ENCAP_MIN_LEN  (EAPOL_WPA2_ENCAP_DATA_HDR_LEN - \
	TLV_HDR_LEN + EAPOL_WPA2_KEY_BIGTK_ENCAP_HDR_LEN)

#ifdef WL_OCV
/* Size of the OCI element */
#define WPA_OCV_OCI_IE_SIZE \
	(bcm_ocv_get_oci_len() + BCM_TLV_EXT_HDR_SIZE)

/* Size of the OCI KDE */
#define WPA_OCV_OCI_KDE_SIZE \
	(bcm_ocv_get_oci_len() + EAPOL_WPA2_ENCAP_DATA_HDR_LEN)

/* Size of the OCI subelement */
#define WPA_OCV_OCI_SUBELEM_SIZE \
	(bcm_ocv_get_oci_len() + TLV_HDR_LEN)

/* Minimum length of WPA2 OCI encapsulation in EAPOL */
#define EAPOL_WPA2_OCI_ENCAP_MIN_LEN \
	(WPA_OCV_OCI_KDE_SIZE - TLV_HDR_LEN)
#endif /* WL_OCV */

#ifdef WLFIPS
#define WLC_SW_KEYS(wlc, bsscfg) ((((wlc)->wsec_swkeys) || \
	((bsscfg)->wsec & (WSEC_SWFLAG | FIPS_ENABLED))))
#else
#define WLC_SW_KEYS(wlc, bsscfg) ((((wlc)->wsec_swkeys) || \
	((bsscfg)->wsec & WSEC_SWFLAG)))
#endif /* WLFIPS */

/* This doesn't really belong here, but neither does WSEC_CKIP* */
/* per-packet encryption exemption policy */
/* no exemption...follow whatever standard rules apply */
#define WSEC_EXEMPT_NO			0
/* send unencrypted */
#define WSEC_EXEMPT_ALWAYS		1
/* send unencrypted if no pairwise key */
#define WSEC_EXEMPT_NO_PAIRWISE		2

#define WPA_CIPHER_UNSPECIFIED 0xff
#define WPA_P_CIPHERS_UNSPECIFIED 0x80000000

#ifdef RSN_IE_INFO_STRUCT_RELOCATED
#define WPA_AKMS_UNSPECIFIED 0x80000000
#else
#define WPA_AKMS_UNSPECIFIED 0
#endif

#ifdef BCMWAPI_WAI
#define IS_WAPI_AUTH(auth) ((auth) == WAPI_AUTH_UNSPECIFIED || \
			    (auth) == WAPI_AUTH_PSK)
#define INCLUDES_WAPI_AUTH(auth) \
				((auth) & (WAPI_AUTH_UNSPECIFIED | \
					   WAPI_AUTH_PSK))
#endif /* BCMWAPI_WAI */

#define IS_WPA_AKM(akm)	((akm) == RSN_AKM_NONE || \
			 (akm) == RSN_AKM_UNSPECIFIED || \
			 (akm) == RSN_AKM_PSK)

#define IS_WPA2_AKM(akm) ((akm) == RSN_AKM_UNSPECIFIED || \
			  (akm) == RSN_AKM_PSK || \
			  (akm) == RSN_AKM_FILS_SHA256 || \
			  (akm) == RSN_AKM_FILS_SHA384)

/* this doesn't mean much. A WPA (not RSN) akm type would match this */
#define RSN_AKM_MASK (\
	BCM_BIT(RSN_AKM_UNSPECIFIED) | \
	BCM_BIT(RSN_AKM_PSK) | \
	BCM_BIT(RSN_AKM_SAE_PSK) | \
	BCM_BIT(RSN_AKM_FILS_SHA256) | \
	BCM_BIT(RSN_AKM_FILS_SHA384) | \
	BCM_BIT(RSN_AKM_OWE) | \
	BCM_BIT(RSN_AKM_SUITEB_SHA256_1X) | \
	BCM_BIT(RSN_AKM_SUITEB_SHA384_1X))

/* verify less than 32 before shifting bits */
#define VALID_AKM_BIT(akm)	((akm) < 32u ? BCM_BIT((akm)) : 0u)

#define IS_RSN_AKM(akm)	(VALID_AKM_BIT((akm)) & RSN_AKM_MASK)

#define FBT_AKM_MASK (BCM_BIT(RSN_AKM_FBT_1X) | \
		BCM_BIT(RSN_AKM_FBT_PSK) | \
		BCM_BIT(RSN_AKM_SAE_FBT) | \
		BCM_BIT(RSN_AKM_FBT_SHA256_FILS) | \
		BCM_BIT(RSN_AKM_FBT_SHA384_FILS) | \
		BCM_BIT(RSN_AKM_FBT_SHA384_1X) | \
		BCM_BIT(RSN_AKM_FBT_SHA384_PSK))

#define IS_FBT_AKM(akm) (VALID_AKM_BIT((akm)) & FBT_AKM_MASK)

#define FILS_AKM_MASK	(\
		BCM_BIT(RSN_AKM_FILS_SHA256) | \
	    BCM_BIT(RSN_AKM_FILS_SHA384))

#define IS_FILS_AKM(akm) (VALID_AKM_BIT((akm)) & FILS_AKM_MASK)

#define MFP_AKM_MASK (\
		BCM_BIT(RSN_AKM_SHA256_1X) | \
	    BCM_BIT(RSN_AKM_SHA256_PSK))

#define IS_MFP_AKM(akm)	(MFP_AKM_MASK & VALID_AKM_BIT((akm)))

#ifdef BCMWAPI_WAI
#define IS_WAPI_AKM(akm) ((akm) == RSN_AKM_NONE || \
			  (akm) == RSN_AKM_UNSPECIFIED || \
			  (akm) == RSN_AKM_PSK)
#endif /* BCMWAPI_WAI */

#define IS_TDLS_AKM(akm) ((akm) == RSN_AKM_TPK)

/* Broadcom(OUI) authenticated key managment suite */
#define BRCM_AKM_NONE 0
#define BRCM_AKM_PSK 1u /* Proprietary PSK AKM */

#define IS_BRCM_AKM(akm) ((akm) == BRCM_AKM_PSK)

#define ONE_X_AKM_MASK (BCM_BIT(RSN_AKM_FBT_1X) | \
		BCM_BIT(RSN_AKM_MFP_1X) | \
		BCM_BIT(RSN_AKM_SHA256_1X) | \
		BCM_BIT(RSN_AKM_SUITEB_SHA256_1X) | \
		BCM_BIT(RSN_AKM_SUITEB_SHA384_1X) | \
		BCM_BIT(RSN_AKM_FBT_SHA384_1X) | \
		BCM_BIT(RSN_AKM_UNSPECIFIED))

#define IS_1X_AKM(akm)  (VALID_AKM_BIT((akm)) & ONE_X_AKM_MASK)

#define SUITEB_AKM_MASK (BCM_BIT(RSN_AKM_SUITEB_SHA256_1X) | \
		BCM_BIT(RSN_AKM_SUITEB_SHA384_1X))
#define IS_1X_SUITEB_AKM(akm) (VALID_AKM_BIT((akm)) & SUITEB_AKM_MASK)

#define SAE_AKM_MASK (BCM_BIT(RSN_AKM_SAE_PSK) | BCM_BIT(RSN_AKM_SAE_FBT))
#define IS_SAE_AKM(akm) (VALID_AKM_BIT((akm)) & SAE_AKM_MASK)

#define SHA256_AKM_MASK (BCM_BIT(RSN_AKM_SHA256_1X) | \
			 BCM_BIT(RSN_AKM_SHA256_PSK) | \
			 BCM_BIT(RSN_AKM_SAE_PSK) | \
			 BCM_BIT(RSN_AKM_SAE_FBT) | \
			 BCM_BIT(RSN_AKM_SUITEB_SHA256_1X) | \
			 BCM_BIT(RSN_AKM_FILS_SHA256) | \
			 BCM_BIT(RSN_AKM_FBT_SHA256_FILS) | \
			 BCM_BIT(RSN_AKM_OWE))
#define IS_SHA256_AKM(akm) (VALID_AKM_BIT((akm)) & SHA256_AKM_MASK)

#define SHA384_AKM_MASK (BCM_BIT(RSN_AKM_SUITEB_SHA384_1X) | \
			 BCM_BIT(RSN_AKM_FBT_SHA384_1X) | \
			 BCM_BIT(RSN_AKM_FILS_SHA384) | \
			 BCM_BIT(RSN_AKM_FBT_SHA384_FILS) | \
			 BCM_BIT(RSN_AKM_PSK_SHA384))
#define IS_SHA384_AKM(akm) (VALID_AKM_BIT((akm)) & SHA384_AKM_MASK)

#define OPEN_AUTH_AKM_MASK (\
	BCM_BIT(RSN_AKM_UNSPECIFIED) | \
	BCM_BIT(RSN_AKM_PSK) | \
	BCM_BIT(RSN_AKM_SHA256_1X) | \
	BCM_BIT(RSN_AKM_SHA256_PSK) | \
	BCM_BIT(RSN_AKM_SUITEB_SHA256_1X) | \
	BCM_BIT(RSN_AKM_SUITEB_SHA384_1X) | \
	BCM_BIT(RSN_AKM_PSK_SHA384))
#define IS_OPEN_AUTH_AKM(akm) (VALID_AKM_BIT((akm)) & OPEN_AUTH_AKM_MASK)

typedef enum akm_type {
	WPA_AUTH_IE = 0x01,
	RSN_AUTH_IE = 0x02,
	OSEN_AUTH_IE = 0x04
} akm_type_t;

#define MAX_ARRAY 1
#define MIN_ARRAY 0

#define WPS_ATID_SEL_REGISTRAR		0x1041

/* move these to appropriate file(s) */
#define WPS_IE_FIXED_LEN	6

/* GTK indices we use - 0-3 valid per IEEE/802.11 2012 */
#define GTK_INDEX_1       1
#define GTK_INDEX_2       2

/* IGTK indices we use - 4-5 are valid per IEEE 802.11 2012 */
#define IGTK_INDEX_1      4
#define IGTK_INDEX_2      5

/* following needed for compatibility for router code because it automerges */
#define IGTK_ID_TO_WSEC_INDEX(_id) (_id)
#define WPA_AES_CMAC_CALC aes_cmac_calc

#define IS_IGTK_INDEX(x) ((x) == IGTK_INDEX_1 || (x) == IGTK_INDEX_2)

#ifdef RSN_IE_INFO_STRUCT_RELOCATED
typedef struct rsn_ie_info {
	uint8 version;
	int parse_status;
	device_type_t dev_type;			/* AP or STA */
	auth_ie_type_mask_t auth_ie_type;	/* bit field of WPA, WPA2 and (not yet) WAPI */
	rsn_cipher_t g_cipher;
	rsn_akm_t sta_akm;			/* single STA akm */
	uint16 caps;
	rsn_ciphers_t rsn_p_ciphers;
	rsn_ciphers_t wpa_p_ciphers;
	rsn_akm_mask_t rsn_akms;
	rsn_akm_mask_t wpa_akms;
	uint8 pmkid_count;
	uint8 pmkids_offset;			/* offset into the IE */
	rsn_cipher_t g_mgmt_cipher;
	rsn_cipher_t sta_cipher;		/* single STA cipher */
	uint16 key_desc;			/* key descriptor version as STA */
	uint16 mic_len;				/* unused. keep for ROM compatibility. */
	uint8 pmk_len;				/* EAPOL PMK */
	uint8 kck_mic_len;			/* EAPOL MIC (by KCK) */
	uint8 kck_len;				/* EAPOL KCK */
	uint8 kek_len;				/* EAPOL KEK */
	uint8 tk_len;				/* EAPOL TK */
	uint8 ptk_len;				/* EAPOL PTK */
	uint8 kck2_len;				/* EAPOL KCK2 */
	uint8 kek2_len;				/* EAPOL KEK2 */
	uint8* rsn_ie;		/* RSN IE from beacon or assoc request */
	uint16 rsn_ie_len;	/* RSN IE length */
	uint8* wpa_ie;		/* WPA IE */
	uint16 wpa_ie_len;	/* WPA IE length (is it fixed ? */
	/* the following are helpers in the AP rsn info to be filled in by the STA
	 * after determination of which IE is being used.in wsec_filter.
	 */
	uint32 p_ciphers;	/* current ciphers for the chosen auth IE */
	uint32 akms;		/* current ciphers for the chosen auth IE */
	uint8 *auth_ie;		/* pointer to current chosen auth IE */
	uint16 auth_ie_len;
	uint8 ref_count;	/* external reference count to decide if structure must be freed */
	uint8 rsnxe_len;	/* RSNXE IE length */
	uint8 PAD[3];
	uint8* rsnxe;		/* RSNXE IE TLV buffer */
	uint32 rsnxe_cap;	/* RSNXE IE cap flag, refer to 802.11.h */
} rsn_ie_info_t;
#endif /* RSN_IE_INFO_STRUCT_RELOCATED */

/* WiFi WPS Attribute fixed portion */
typedef struct wps_at_fixed {
	uint8 at[2];
	uint8 len[2];
	uint8 data[1];
} wps_at_fixed_t;

typedef const struct oui_akm_wpa_tbl {
	const char *oui;  /* WPA auth category */
	uint16 rsn_akm;
	uint32 wpa_auth;
} oui_akm_wpa_tbl_t;

#define WPS_AT_FIXED_LEN	4

#define wps_ie_fixed_t wpa_ie_fixed_t

/* What should be the multicast mask for AES ? */
#define WPA_UNICAST_AES_MASK (\
		BCM_BIT(WPA_CIPHER_AES_CCM) | \
		BCM_BIT(WPA_CIPHER_AES_GCM) | \
		BCM_BIT(WPA_CIPHER_AES_GCM256))

#define WPA_CIPHER_WEP_MASK (\
		BCM_BIT(WPA_CIPHER_WEP_104) | \
		BCM_BIT(WPA_CIPHER_WEP_40))

/* temporary to pass pre-commit */
#ifdef TMP_USE_RSN_INFO
/* wsec macros */
#ifdef EXT_STA
#define UCAST_NONE(rsn_info)	(((rsn_info)->p_ciphers == (1 << WPA_CIPHER_NONE)) && \
		(!WLEXTSTA_ENAB(wlc->pub) || wlc->use_group_enabled))
#else
#define UCAST_NONE(rsn_info)   (rsn_info->p_ciphers == (1 << WPA_CIPHER_NONE))
#endif /* EXT_STA */

#define UCAST_AES(rsn_info) (rsn_info->p_ciphers & WPA_UNICAST_AES_MASK)
#define UCAST_TKIP(rsn_info)	(rsn_info->p_ciphers & (1 << WPA_CIPHER_TKIP))
#define UCAST_WEP(rsn_info) (rsn_info->p_ciphers & WPA_CIPHER_WEP_MASK)

#define MCAST_NONE(rsn_info)	((rsn_info)->g_cipher == WPA_CIPHER_NONE)
#define MCAST_AES(rsn_info) ((1 << rsn_info->g_cipher) & WPA_UNICAST_AES_MASK)
#define MCAST_TKIP(rsn_info) (rsn_info->g_cipher == WPA_CIPHER_TKIP)
#define MCAST_WEP(rsn_info) ((1 << rsn_info->g_cipher) & WPA_CIPHER_WEP_MASK)

#endif /* TMP_USE_RSN_INFO */

#define AKM_SHA256_MASK (\
	BCM_BIT(RSN_AKM_SHA256_1X) |	\
	BCM_BIT(RSN_AKM_SHA256_PSK) |	\
	BCM_BIT(RSN_AKM_SAE_PSK) |		\
	BCM_BIT(RSN_AKM_OWE) |			  \
	BCM_BIT(RSN_AKM_SUITEB_SHA256_1X) | \
	BCM_BIT(RSN_AKM_FILS_SHA256) |		\
	BCM_BIT(RSN_AKM_FBT_SHA256_FILS) |	\
	BCM_BIT(RSN_AKM_SAE_FBT))

#define AKM_SHA384_MASK (\
	BCM_BIT(RSN_AKM_SUITEB_SHA384_1X) |  \
	BCM_BIT(RSN_AKM_FBT_SHA384_1X) | \
	BCM_BIT(RSN_AKM_FILS_SHA384) |	\
	BCM_BIT(RSN_AKM_FBT_SHA384_FILS) | \
	BCM_BIT(RSN_AKM_FBT_SHA384_PSK) |	\
	BCM_BIT(RSN_AKM_PSK_SHA384))

/* these AKMs require MFP capable set in their IE */
#define RSN_MFPC_AKM_MASK (\
	BCM_BIT(RSN_AKM_SAE_PSK) |	\
	BCM_BIT(RSN_AKM_OWE) | \
	BCM_BIT(RSN_AKM_SAE_FBT))

/* AKMs that supported by in-driver supplicant.
 * TODO: have to redesign this to include 1x and other PSK AKMs.
 */
#define IS_BCMSUP_AKM(akm) \
	((akm == RSN_AKM_PSK) | \
	 (akm == RSN_AKM_SAE_PSK) | \
	 (akm == RSN_AKM_OWE) | \
	 (akm == RSN_AKM_FBT_PSK) | \
	 (akm == RSN_AKM_SAE_FBT) | \
	 (akm == RSN_AKM_FBT_SHA384_1X) | \
	 (akm == RSN_AKM_FBT_SHA384_PSK))

/* AKMs use common PSK which identified by broadcast addr */
#define IS_SHARED_PMK_AKM(akm) \
	((akm == RSN_AKM_PSK) | \
	 (akm == RSN_AKM_FBT_PSK) | \
	 (akm == RSN_AKM_SHA256_PSK) | \
	 (akm == RSN_AKM_FBT_SHA384_PSK) | \
	 (akm == RSN_AKM_PSK_SHA384))

#define RSN_AKM_USE_KDF(akm) (akm >= RSN_AKM_FBT_1X ? 1u : 0)

/* Macro to abstract access to the rsn_ie_info strucuture in case
 * we want to move it to a cubby or something else.
 * Gives the rsn_info pointer
 */

#define RSN_INFO_GET(s) (s->rsn_info)
/* where the rsn_info resides */
#define RSN_INFO_GET_PTR(s) (&s->rsn_info)

#define AUTH_AKM_INCLUDED(s) (s->rsn_info != NULL && s->rsn_info->parse_status == BCME_OK && \
		s->rsn_info->akms != WPA_AKMS_UNSPECIFIED)

#define AKM_IS_MEMBER(akm, mask) ((mask) & VALID_AKM_BIT((akm)) || ((akm) ==  0 && (mask) == 0))

typedef enum eapol_key_type {
	EAPOL_KEY_NONE		= 0,
	EAPOL_KEY_PMK		= 1,
	EAPOL_KEY_KCK_MIC	= 2,
	EAPOL_KEY_KEK		= 3,
	EAPOL_KEY_TK		= 4,
	EAPOL_KEY_PTK		= 5,
	EAPOL_KEY_KCK		= 6,
	EAPOL_KEY_KCK2		= 7,
	EAPOL_KEY_KEK2		= 8
} eapol_key_type_t;

/* Return address of max or min array depending first argument.
 * Return NULL in case of a draw.
 */
extern const uint8 *wpa_array_cmp(int max_array, const uint8 *x, const uint8 *y, uint len);

/* Increment the array argument */
extern void wpa_incr_array(uint8 *array, uint len);

/* Convert WPA IE cipher suite to locally used value */
extern bool wpa_cipher(wpa_suite_t *suite, ushort *cipher, bool wep_ok);

/* Look for a WPA IE; return it's address if found, NULL otherwise */
extern wpa_ie_fixed_t *bcm_find_wpaie(uint8 *parse, uint len);
extern bcm_tlv_t *bcm_find_wmeie(uint8 *parse, uint len, uint8 subtype, uint8 subtype_len);
/* Look for a WPS IE; return it's address if found, NULL otherwise */
extern wps_ie_fixed_t *bcm_find_wpsie(const uint8 *parse, uint len);
extern wps_at_fixed_t *bcm_wps_find_at(wps_at_fixed_t *at, uint len, uint16 id);
int bcm_find_security_ies(uint8 *buf, uint buflen, void **wpa_ie,
		void **rsn_ie);

#ifdef WLP2P
/* Look for a WiFi P2P IE; return it's address if found, NULL otherwise */
extern wifi_p2p_ie_t *bcm_find_p2pie(const uint8 *parse, uint len);
#endif
/* Look for a hotspot2.0 IE; return it's address if found, NULL otherwise */
bcm_tlv_t *bcm_find_hs20ie(uint8 *parse, uint len);
/* Look for a OSEN IE; return it's address if found, NULL otherwise */
bcm_tlv_t *bcm_find_osenie(uint8 *parse, uint len);

/* Check whether the given IE has the specific OUI and the specific type. */
extern bool bcm_has_ie(uint8 *ie, uint8 **tlvs, uint *tlvs_len,
                       const uint8 *oui, uint oui_len, uint8 type);

/* Check whether pointed-to IE looks like WPA. */
#define bcm_is_wpa_ie(ie, tlvs, len)	bcm_has_ie(ie, tlvs, len, \
	(const uint8 *)WPA_OUI, WPA_OUI_LEN, WPA_OUI_TYPE)
/* Check whether pointed-to IE looks like WPS. */
#define bcm_is_wps_ie(ie, tlvs, len)	bcm_has_ie(ie, tlvs, len, \
	(const uint8 *)WPS_OUI, WPS_OUI_LEN, WPS_OUI_TYPE)
#ifdef WLP2P
/* Check whether the given IE looks like WFA P2P IE. */
#define bcm_is_p2p_ie(ie, tlvs, len)	bcm_has_ie(ie, tlvs, len, \
	(const uint8 *)P2P_OUI, P2P_OUI_LEN, P2P_OUI_TYPE)
#endif

/* Convert WPA2 IE cipher suite to locally used value */
extern bool wpa2_cipher(wpa_suite_t *suite, ushort *cipher, bool wep_ok);

#if defined(BCMSUP_PSK) || defined(BCMSUPPL) || defined(GTKOE) || defined(WL_FILS)
/* Look for an encapsulated GTK; return it's address if found, NULL otherwise */
extern eapol_wpa2_encap_data_t *wpa_find_gtk_encap(uint8 *parse, uint len);

/* Check whether pointed-to IE looks like an encapsulated GTK. */
extern bool wpa_is_gtk_encap(uint8 *ie, uint8 **tlvs, uint *tlvs_len);

/* Look for encapsulated key data; return it's address if found, NULL otherwise */
extern eapol_wpa2_encap_data_t *wpa_find_kde(const uint8 *parse, uint len, uint8 type);

/* Find kde data given eapol header. */
extern int wpa_find_eapol_kde_data(eapol_header_t *eapol, uint8 eapol_mic_len,
	uint8 subtype, eapol_wpa2_encap_data_t **out_data);

/* Look for kde data in key data. */
extern int wpa_find_kde_data(const uint8 *kde_buf, uint16 buf_len,
	uint8 subtype, eapol_wpa2_encap_data_t **out_data);

#ifdef WL_OCV
/* Check if both local and remote are OCV capable */
extern bool wpa_check_ocv_caps(uint16 local_caps, uint16 peer_caps);

/* Write OCI KDE into the buffer */
extern int wpa_add_oci_encap(chanspec_t chspec, uint8* buf, uint buf_len);

/* Validate OCI KDE */
extern int wpa_validate_oci_encap(chanspec_t chspec, const uint8* buf, uint buf_len);

/* Write OCI IE into the buffer */
extern int wpa_add_oci_ie(chanspec_t chspec, uint8* buf, uint buf_len);

/* Validate OCI IE */
extern int wpa_validate_oci_ie(chanspec_t chspec, const uint8* buf, uint buf_len);

/* Write OCI subelement into the FTE buffer */
extern int wpa_add_oci_ft_subelem(chanspec_t chspec, uint8* buf, uint buf_len);

/* Validate OCI FTE subelement */
extern int wpa_validate_oci_ft_subelem(chanspec_t chspec,
	const uint8* buf, uint buf_len);
#endif /* WL_OCV */
#endif /* defined(BCMSUP_PSK) || defined(BCMSUPPL) || defined(GTKOE) || defined(WL_FILS) */

#if defined(BCMSUP_PSK) || defined(WLFBT) || defined(BCMAUTH_PSK)|| \
	defined(WL_OKC) || defined(GTKOE)
/* Calculate a pair-wise transient key */
extern int wpa_calc_ptk(rsn_akm_t akm, const struct ether_addr *auth_ea,
		const struct ether_addr *sta_ea, const uint8 *anonce, uint8 anonce_len,
		const uint8* snonce, uint8 snonce_len, const uint8 *pmk,
		uint pmk_len, uint8 *ptk, uint ptk_len);

/* Compute Message Integrity Code (MIC) over EAPOL message */
extern int wpa_make_mic(eapol_header_t *eapol, uint key_desc, uint8 *mic_key,
                                   rsn_ie_info_t *rsn_info, uchar *mic, uint mic_len);

/* Check MIC of EAPOL message */
extern bool wpa_check_mic(eapol_header_t *eapol,
	uint key_desc, uint8 *mic_key, rsn_ie_info_t *rsn_info);

/* Calculate PMKID */
extern void wpa_calc_pmkid(const struct ether_addr *auth_ea,
	const struct ether_addr *sta_ea, const uint8 *pmk, uint pmk_len, uint8 *pmkid);

/* Encrypt key data for a WPA key message */
extern bool wpa_encr_key_data(eapol_wpa_key_header_t *body, uint16 key_info,
	uint8 *ekey, uint8 *gtk, uint8 *data, uint8 *encrkey, rc4_ks_t *rc4key,
	const rsn_ie_info_t *rsn_info);

typedef uint8 wpa_rc4_ivkbuf_t[EAPOL_WPA_KEY_IV_LEN + EAPOL_WPA_ENCR_KEY_MAX_LEN];
/* Decrypt key data from a WPA key message */
extern int wpa_decr_key_data(eapol_wpa_key_header_t *body, uint16 key_info,
	uint8 *ekey, wpa_rc4_ivkbuf_t ivk, rc4_ks_t *rc4key, const rsn_ie_info_t *rsn_info,
	uint16 *dec_len);
#endif	/* BCMSUP_PSK || WLFBT || BCMAUTH_PSK || defined(GTKOE) */

#if defined(BCMSUP_PSK) || defined(WLFBT) || defined(BCMAUTH_PSK)|| \
	defined(WL_OKC) || defined(GTKOE) || defined(WLHOSTFBT)

/* Calculate PMKR0 for FT association */
extern void wpa_calc_pmkR0(sha2_hash_type_t hash_type, const uint8 *ssid, uint ssid_len,
	uint16 mdid, const uint8 *r0kh, uint r0kh_len, const struct ether_addr *sta_ea,
	const uint8 *pmk, uint pmk_len, uint8 *pmkr0, uint8 *pmkr0name);

/* Calculate PMKR1 for FT association */
extern void wpa_calc_pmkR1(sha2_hash_type_t hash_type, const struct ether_addr *r1kh,
	const struct ether_addr *sta_ea, const uint8 *pmk, uint pmk_len,
	const uint8 *pmkr0name, uint8 *pmkr1, uint8 *pmkr1name);

/* Calculate PTK for FT association */
extern void wpa_calc_ft_ptk(sha2_hash_type_t hash_type, const struct ether_addr *bssid,
	const struct ether_addr *sta_ea, const uint8 *anonce, const uint8* snonce,
	const uint8 *pmk, uint pmk_len, uint8 *ptk, uint ptk_len);

extern void wpa_derive_pmkR1_name(sha2_hash_type_t hash_type, struct ether_addr *r1kh,
		struct ether_addr *sta_ea, uint8 *pmkr0name, uint8 *pmkr1name);

#endif /* defined(BCMSUP_PSK) || defined(WLFBT) || defined(BCMAUTH_PSK) ||
	* defined(WL_OKC) || defined(WLTDLS) || defined(GTKOE) || defined(WLHOSTFBT)
	*/

#if defined(BCMSUP_PSK) || defined(BCMSUPPL)

/* Translate RSNE group mgmt cipher to CRYPTO_ALGO_XXX */
extern uint8 bcmwpa_find_group_mgmt_algo(rsn_cipher_t g_mgmt_cipher);

#endif /* BCMSUP_PSK || BCMSUPPL */

extern bool bcmwpa_akm2WPAauth(uint8 *akm, uint32 *auth, bool sta_iswpa);

extern bool bcmwpa_cipher2wsec(uint8 *cipher, uint32 *wsec);

#ifdef RSN_IE_INFO_STRUCT_RELOCATED
extern uint32 bcmwpa_wpaciphers2wsec(uint32 unicast);
extern int bcmwpa_decode_ie_type(const bcm_tlv_t *ie, rsn_ie_info_t *info,
    uint32 *remaining, uint8 *type);

/* to be removed after merge to NEWT (changed into bcmwpa_rsn_ie_info_reset) */
void rsn_ie_info_reset(rsn_ie_info_t *rsn_info, osl_t *osh);
uint32 wlc_convert_rsn_to_wsec_bitmap(uint32 ap_cipher_mask);
#else
uint32 bcmwpa_wpaciphers2wsec(uint8 wpacipher);
int bcmwpa_decode_ie_type(const bcm_tlv_t *ie, rsn_ie_info_t *info, uint32 *remaining);
#endif /* RSN_IE_INFO_STRUCT_RELOCATED */

extern int bcmwpa_parse_rsnie(const bcm_tlv_t *ie, rsn_ie_info_t *info, device_type_t dev_type);

/* Calculate PMKID */
extern void kdf_calc_pmkid(const struct ether_addr *auth_ea,
	const struct ether_addr *sta_ea, const uint8 *key, uint key_len, uint8 *pmkid,
	rsn_ie_info_t *rsn_info);

extern void kdf_calc_ptk(const struct ether_addr *auth_ea, const struct ether_addr *sta_ea,
	const uint8 *anonce, const uint8 *snonce, const uint8 *pmk, uint pmk_len,
	uint8 *ptk, uint ptk_len);

#ifdef WLTDLS
/* Calculate TPK for TDLS association */
extern void wpa_calc_tpk(const struct ether_addr *init_ea,
	const struct ether_addr *resp_ea, const struct ether_addr *bssid,
	const uint8 *anonce, const uint8* snonce, uint8 *tpk, uint tpk_len);
#endif
extern bool bcmwpa_is_wpa_auth(uint32 wpa_auth);
extern bool bcmwpa_includes_wpa_auth(uint32 wpa_auth);
extern bool bcmwpa_is_rsn_auth(uint32 wpa_auth);
extern bool bcmwpa_includes_rsn_auth(uint32 wpa_auth);
extern int bcmwpa_get_algo_key_len(uint8 algo, uint16 *key_len);

/* macro to pass precommit on ndis builds */
#define bcmwpa_is_wpa2_auth(wpa_auth) bcmwpa_is_rsn_auth(wpa_auth)
extern uint8 bcmwpa_eapol_key_length(eapol_key_type_t key, rsn_akm_t akm, rsn_cipher_t cipher);

/* rsn info allocation utilities. */
void bcmwpa_rsn_ie_info_reset(rsn_ie_info_t *rsn_info, osl_t *osh);
void bcmwpa_rsn_ie_info_rel_ref(rsn_ie_info_t **rsn_info, osl_t *osh);
int bcmwpa_rsn_ie_info_add_ref(rsn_ie_info_t *rsn_info);
int bcmwpa_rsn_akm_cipher_match(rsn_ie_info_t *rsn_info);
int bcmwpa_rsnie_eapol_key_len(rsn_ie_info_t *info);
#if defined(WL_BAND6G)
/* Return TRUE if any of the akm in akms_bmp is invalid in 6Ghz */
bool bcmwpa_is_invalid_6g_akm(const rsn_akm_mask_t akms_bmp);
/* Return TRUE if any of the cipher in ciphers_bmp is invalid in 6Ghz */
bool bcmwpa_is_invalid_6g_cipher(const rsn_ciphers_t ciphers_bmp);
#endif /* WL_BAND6G */
#endif	/* _BCMWPA_H_ */
