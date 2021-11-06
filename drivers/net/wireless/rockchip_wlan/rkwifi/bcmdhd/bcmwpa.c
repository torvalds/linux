/*
 *   bcmwpa.c - shared WPA-related functions
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

/* include wl driver config file if this file is compiled for driver */
#ifdef BCMDRIVER
#include <osl.h>
/* HACK: this case for external supplicant use */
#else
#include <string.h>
#if defined(BCMEXTSUP)
#include <bcm_osl.h>
#else
#ifndef ASSERT
#define ASSERT(exp)
#endif
#endif /* BCMEXTSUP */
#endif /* BCMDRIVER */

#include <ethernet.h>
#include <eapol.h>
#include <802.11.h>
#include <wpa.h>
#include <802.11r.h>

#include <bcmutils.h>
#include <bcmendian.h>
#include <bcmwpa.h>
#include <aeskeywrap.h>

#include <bcmstdlib_s.h>

#include <wlioctl.h>

#include <bcmutils.h>
#include <bcmwpa.h>
#ifdef WL_OCV
#include <bcm_ocv.h>
#endif /* WL_OCV */

#if defined(BCMSUP_PSK) || defined(WLFBT) || defined(BCMAUTH_PSK) || \
	defined(WL_OKC) || defined(WLTDLS) || defined(GTKOE) || defined(WLHOSTFBT)
#ifdef WLHOSTFBT
#include <string.h>
#endif
#endif /* defined(BCMSUP_PSK) || defined(WLFBT) || defined(BCMAUTH_PSK) ||
	* defined(WL_OKC) || defined(WLTDLS) || defined(GTKOE) || defined(WLHOSTFBT)
	*/

/* prefix strings */
#define PMK_NAME_PFX "PMK Name"
#define FT_PTK_PFX "FT-PTK"
#define FT_R0_PFX "FT-R0"
#define FT_R0N_PFX "FT-R0N"
#define FT_R1_PFX "FT-R1"
#define FT_R1N_PFX "FT-R1N"
#define WPA_PTK_PFX "Pairwise key expansion"
#define TDLS_PMK_PFX "TDLS PMK"
/* end prefix strings */

#ifndef BIT
#define BIT(x) (1 << (x))
#endif

#define PRF_PREFIXES_NUM	5u

typedef struct key_length_entry {
	uint8 suite;
	uint8 len;
} key_length_entry_t;

/* EAPOL key(PMK/KCK/KEK/TK) length lookup tables */
static const key_length_entry_t eapol_pmk_len[] = {
	{RSN_AKM_SUITEB_SHA384_1X, EAPOL_WPA_PMK_SHA384_LEN},
	{RSN_AKM_FBT_SHA384_1X, EAPOL_WPA_PMK_SHA384_LEN},
	{RSN_AKM_FBT_SHA384_PSK, EAPOL_WPA_PMK_SHA384_LEN},
	{0u, EAPOL_WPA_PMK_DEFAULT_LEN} /* default */
};

static const key_length_entry_t eapol_kck_mic_len[] = {
	{RSN_AKM_SUITEB_SHA384_1X, EAPOL_WPA_KCK_MIC_SHA384_LEN},
	{RSN_AKM_FILS_SHA256, 0u},
	{RSN_AKM_FILS_SHA384, 0u},
	{RSN_AKM_FBT_SHA256_FILS, EAPOL_WPA_KCK_MIC_DEFAULT_LEN},
	{RSN_AKM_FBT_SHA384_FILS, EAPOL_WPA_KCK2_SHA384_LEN},
	{RSN_AKM_OWE, EAPOL_WPA_KCK_MIC_DEFAULT_LEN},
	{RSN_AKM_FBT_SHA384_1X, EAPOL_WPA_KCK_MIC_SHA384_LEN},
	{RSN_AKM_FBT_SHA384_PSK, EAPOL_WPA_KCK_MIC_SHA384_LEN},
	{0u, EAPOL_WPA_KCK_MIC_DEFAULT_LEN} /* default */
};

static const key_length_entry_t eapol_kck_len[] = {
	{RSN_AKM_SUITEB_SHA384_1X, EAPOL_WPA_KCK_SHA384_LEN},
	{RSN_AKM_FILS_SHA256, 0u},
	{RSN_AKM_FILS_SHA384, 0u},
	{RSN_AKM_FBT_SHA256_FILS, 0u},
	{RSN_AKM_FBT_SHA384_FILS, 0u},
	{RSN_AKM_OWE, EAPOL_WPA_KCK_DEFAULT_LEN},
	{RSN_AKM_FBT_SHA384_1X, EAPOL_WPA_KCK_SHA384_LEN},
	{RSN_AKM_FBT_SHA384_PSK, EAPOL_WPA_KCK_SHA384_LEN},
	{0u, EAPOL_WPA_KCK_DEFAULT_LEN} /* default */
};

static const key_length_entry_t eapol_kek_len[] = {
	{RSN_AKM_FILS_SHA384, EAPOL_WPA_ENCR_KEY_MAX_LEN},
	{RSN_AKM_FBT_SHA384_FILS, EAPOL_WPA_ENCR_KEY_MAX_LEN},
	{RSN_AKM_SUITEB_SHA384_1X, EAPOL_WPA_ENCR_KEY_MAX_LEN / 2},
	{RSN_AKM_FILS_SHA256, EAPOL_WPA_ENCR_KEY_MAX_LEN / 2},
	{RSN_AKM_FBT_SHA256_FILS, EAPOL_WPA_ENCR_KEY_MAX_LEN / 2},
	{RSN_AKM_OWE, EAPOL_WPA_ENCR_KEY_DEFAULT_LEN},
	{RSN_AKM_FBT_SHA384_1X, EAPOL_WPA_ENCR_KEY_MAX_LEN / 2},
	{RSN_AKM_FBT_SHA384_PSK, EAPOL_WPA_ENCR_KEY_MAX_LEN / 2},
	{0u, EAPOL_WPA_ENCR_KEY_DEFAULT_LEN} /* default */
};

static const key_length_entry_t eapol_tk_len[] = {
	{WPA_CIPHER_CCMP_256, EAPOL_WPA_TEMP_ENCR_KEY_MAX_LEN},
	{WPA_CIPHER_AES_GCM256, EAPOL_WPA_TEMP_ENCR_KEY_MAX_LEN},
	{WPA_CIPHER_BIP_GMAC_256, EAPOL_WPA_TEMP_ENCR_KEY_MAX_LEN},
	{WPA_CIPHER_BIP_CMAC_256, EAPOL_WPA_TEMP_ENCR_KEY_MAX_LEN},
	{WPA_CIPHER_AES_CCM, EAPOL_WPA_TEMP_ENCR_KEY_MAX_LEN / 2},
	{WPA_CIPHER_AES_GCM, EAPOL_WPA_TEMP_ENCR_KEY_MAX_LEN / 2},
	{WPA_CIPHER_BIP_GMAC_128, EAPOL_WPA_TEMP_ENCR_KEY_MAX_LEN / 2},
	{WPA_CIPHER_TKIP, EAPOL_WPA_TEMP_ENCR_KEY_MAX_LEN},
	{0u, 0u} /* default */
};

#if defined(WL_FILS) && defined(WLFBT)
static const key_length_entry_t eapol_kck2_len[] = {
	{RSN_AKM_FBT_SHA256_FILS, EAPOL_WPA_KCK2_SHA256_LEN},
	{RSN_AKM_FBT_SHA384_FILS, EAPOL_WPA_KCK2_SHA384_LEN},
	{0u, 0u} /* default */
};

static const key_length_entry_t eapol_kek2_len[] = {
	{RSN_AKM_FBT_SHA256_FILS, EAPOL_WPA_KEK2_SHA256_LEN},
	{RSN_AKM_FBT_SHA384_FILS, EAPOL_WPA_KEK2_SHA384_LEN},
	{0u, 0u} /* default */
};
#endif /* WL_FILS && WLFBT */

typedef struct key_length_lookup {
	const eapol_key_type_t key;
	const key_length_entry_t *key_entry;
} key_length_lookup_t;

static const key_length_lookup_t eapol_key_lookup_tbl[] = {
	{EAPOL_KEY_PMK, eapol_pmk_len},
	{EAPOL_KEY_KCK_MIC, eapol_kck_mic_len},
	{EAPOL_KEY_KCK, eapol_kck_len},
	{EAPOL_KEY_KEK, eapol_kek_len},
	{EAPOL_KEY_TK, eapol_tk_len},
#if defined(WL_FILS) && defined(WLFBT)
	{EAPOL_KEY_KCK2, eapol_kck2_len},
	{EAPOL_KEY_KEK2, eapol_kek2_len},
#endif /* WL_FILS && WLFBT */
};

typedef struct rsn_akm_lookup_entry {
	const rsn_akm_t rsn_akm;
	const sha2_hash_type_t hash_type;
} rsn_akm_lookup_entry_t;

static const rsn_akm_lookup_entry_t rsn_akm_lookup_tbl[] = {
	{RSN_AKM_NONE, HASH_SHA1},
	{RSN_AKM_UNSPECIFIED, HASH_SHA1},
	{RSN_AKM_PSK, HASH_SHA1},
	{RSN_AKM_FBT_1X, HASH_SHA256},
	{RSN_AKM_FBT_PSK, HASH_SHA256},
	{RSN_AKM_MFP_1X, HASH_SHA256},
	{RSN_AKM_MFP_PSK, HASH_SHA256},
	{RSN_AKM_SHA256_1X, HASH_SHA256},
	{RSN_AKM_SHA256_PSK, HASH_SHA256},
	{RSN_AKM_TPK, HASH_SHA256},
	{RSN_AKM_SAE_PSK, HASH_SHA256},
	{RSN_AKM_SAE_FBT, HASH_SHA256},
	{RSN_AKM_SUITEB_SHA256_1X, HASH_SHA256},
	{RSN_AKM_SUITEB_SHA384_1X, HASH_SHA384},
	{RSN_AKM_FBT_SHA384_1X, HASH_SHA384},
	{RSN_AKM_FILS_SHA256, HASH_SHA256},
	{RSN_AKM_FILS_SHA384, HASH_SHA384},
	{RSN_AKM_FBT_SHA256_FILS, HASH_SHA256},
	{RSN_AKM_FBT_SHA384_FILS, HASH_SHA384},
	{RSN_AKM_OWE, HASH_SHA256},
	{RSN_AKM_FBT_SHA384_PSK, HASH_SHA384},
	{RSN_AKM_PSK_SHA384, HASH_SHA384},
};

typedef struct rsn_akm_cipher_match_entry {
	uint16  akm_type;
	uint32	u_cast; /* BITMAP */
	uint32	m_cast; /* BITMAP */
	uint32	g_mgmt; /* BITMAP */
} rsn_akm_cipher_match_entry_t;

/* list only explicit cipher restriction for given AKM (e.g SuiteB)
 * refer to 802.11 spec 9.4.2.24.3
 * If not listed here, it means no restriction in using any ciphers.
 */
static const rsn_akm_cipher_match_entry_t rsn_akm_cipher_match_table[] = {
	{RSN_AKM_SUITEB_SHA256_1X,
	BCM_BIT(WPA_CIPHER_AES_GCM),
	BCM_BIT(WPA_CIPHER_AES_GCM),
	BCM_BIT(WPA_CIPHER_BIP_GMAC_128)},
	{RSN_AKM_SUITEB_SHA384_1X,
	BCM_BIT(WPA_CIPHER_AES_GCM256) | BCM_BIT(WPA_CIPHER_CCMP_256),
	BCM_BIT(WPA_CIPHER_AES_GCM256) | BCM_BIT(WPA_CIPHER_AES_GCM256),
	BCM_BIT(WPA_CIPHER_BIP_GMAC_256) | BCM_BIT(WPA_CIPHER_BIP_CMAC_256)},
	{RSN_AKM_FBT_SHA384_1X,
	BCM_BIT(WPA_CIPHER_AES_GCM256) | BCM_BIT(WPA_CIPHER_CCMP_256),
	BCM_BIT(WPA_CIPHER_AES_GCM256) | BCM_BIT(WPA_CIPHER_AES_GCM256),
	BCM_BIT(WPA_CIPHER_BIP_GMAC_256) | BCM_BIT(WPA_CIPHER_BIP_CMAC_256)}
};

#if defined(WL_BAND6G)
static const rsn_akm_mask_t rsn_akm_6g_inval_mask =
	BCM_BIT(RSN_AKM_PSK) |
	BCM_BIT(RSN_AKM_FBT_PSK) |
	BCM_BIT(RSN_AKM_SHA256_PSK) |
	BCM_BIT(RSN_AKM_FBT_SHA384_PSK) |
	BCM_BIT(RSN_AKM_PSK_SHA384);

static const rsn_ciphers_t cipher_6g_inval_mask =
	BCM_BIT(WPA_CIPHER_NONE) |
	BCM_BIT(WPA_CIPHER_WEP_40) |
	BCM_BIT(WPA_CIPHER_TKIP) |
	BCM_BIT(WPA_CIPHER_WEP_104);
#endif /* WL_BAND6G */

#if defined(BCMSUP_PSK) || defined(BCMSUPPL)
typedef struct group_cipher_algo_entry {
	rsn_cipher_t g_mgmt_cipher;
	uint8 bip_algo;
} group_cipher_algo_entry_t;

static const group_cipher_algo_entry_t group_mgmt_cipher_algo[] = {
	{WPA_CIPHER_BIP_GMAC_256, CRYPTO_ALGO_BIP_GMAC256},
	{WPA_CIPHER_BIP_CMAC_256, CRYPTO_ALGO_BIP_CMAC256},
	{WPA_CIPHER_BIP_GMAC_128, CRYPTO_ALGO_BIP_GMAC},
	{WPA_CIPHER_BIP, CRYPTO_ALGO_BIP},
};
#endif /* defined(BCMSUP_PSK) || defined(BCMSUPPL) */

static uint16 wlc_calc_rsn_desc_version(const rsn_ie_info_t *rsn_info);
static int bcmwpa_is_valid_akm(const rsn_akm_t akm);
#if defined(BCMSUP_PSK) || defined(BCMAUTH_PSK) || defined(WLFBT) || defined(GTKOE)
static sha2_hash_type_t bcmwpa_rsn_akm_to_hash(const rsn_akm_t akm);
#ifdef RSN_IE_INFO_STRUCT_RELOCATED
static int bcmwpa_decode_cipher_suite(rsn_ie_info_t *info, const uint8 **ptr, uint ie_len, uint
	*remain_len, uint16 *p_count);
#endif
#endif /* defined(BCMSUP_PSK) || defined(BCMAUTH_PSK) || defined(WLFBT) || defined(GTKOE) */
#if defined(BCMSUP_PSK) || defined(WLFBT) || defined(WL_OKC) || defined(WLHOSTFBT)
#include <rc4.h>

/* calculate wpa PMKID: HMAC-SHA1-128(PMK, "PMK Name" | AA | SPA) */
static void
wpa_calc_pmkid_impl(sha2_hash_type_t hash_type,
	const struct ether_addr *auth_ea, const struct ether_addr *sta_ea,
	const uint8 *pmk, uint pmk_len, uint8 *pmkid)
{
	int err;
	hmac_sha2_ctx_t ctx;

	err = hmac_sha2_init(&ctx, hash_type, pmk, pmk_len);
	if (err != BCME_OK)
		goto done;
	hmac_sha2_update(&ctx, (const uint8 *)PMK_NAME_PFX, sizeof(PMK_NAME_PFX) - 1);
	hmac_sha2_update(&ctx, (const uint8 *)auth_ea, ETHER_ADDR_LEN);
	hmac_sha2_update(&ctx, (const uint8 *)sta_ea, ETHER_ADDR_LEN);
	hmac_sha2_final(&ctx, pmkid, WPA2_PMKID_LEN);
done:;
}

void
wpa_calc_pmkid(const struct ether_addr *auth_ea, const struct ether_addr *sta_ea,
	const uint8 *pmk, uint pmk_len, uint8 *pmkid)
{
	wpa_calc_pmkid_impl(HASH_SHA1, auth_ea, sta_ea, pmk, pmk_len, pmkid);
}

void
kdf_calc_pmkid(const struct ether_addr *auth_ea, const struct ether_addr *sta_ea,
	const uint8 *key, uint key_len, uint8 *pmkid, rsn_ie_info_t *rsn_info)
{
	sha2_hash_type_t hash_type;

	if (rsn_info->sta_akm == RSN_AKM_SUITEB_SHA384_1X) {
		hash_type = HASH_SHA384;
	} else {
		hash_type = HASH_SHA256;
	}

	wpa_calc_pmkid_impl(hash_type, auth_ea, sta_ea, key, key_len, pmkid);
}

#if defined(WLFBT) || defined(WLHOSTFBT)
void
wpa_calc_pmkR0(sha2_hash_type_t hash_type, const uint8 *ssid, uint ssid_len,
	uint16 mdid, const uint8 *r0kh, uint r0kh_len, const struct ether_addr *sta_ea,
	const uint8 *pmk, uint pmk_len, uint8 *pmkr0, uint8 *pmkr0name)
{
	uint8 out[FBT_R0KH_ID_LEN + WPA2_PMKID_LEN - 1];
	int out_len = FBT_R0KH_ID_LEN - 1;
	bcm_const_xlvp_t pfx[7];
	bcm_const_xlvp_t pfx2[2];
	int npfx = 0;
	int npfx2 = 0;
	uint8 mdid_le[2];
	uint8 pfx_ssid_len;
	uint8 pfx_r0kh_len;

	if (hash_type == HASH_SHA384) {
		out_len += WPA2_PMKID_LEN;
	}

	/* create prefixes for pmkr0 */
	pfx[npfx].len = sizeof(FT_R0_PFX) - 1;
	pfx[npfx++].data = (uint8 *)FT_R0_PFX;

	/* ssid length and ssid */
	pfx_ssid_len = ssid_len & 0xff;
	pfx[npfx].len = (uint16)sizeof(pfx_ssid_len);
	pfx[npfx++].data = &pfx_ssid_len;

	pfx[npfx].len = (uint16)(ssid_len & 0xffff);
	pfx[npfx++].data = ssid;

	/* mdid */
	htol16_ua_store(mdid, mdid_le);
	pfx[npfx].len = sizeof(mdid_le);
	pfx[npfx++].data = mdid_le;

	/* r0kh len and r0kh */
	pfx_r0kh_len = r0kh_len & 0xff;
	pfx[npfx].len = sizeof(pfx_r0kh_len);
	pfx[npfx++].data = &pfx_r0kh_len;

	pfx[npfx].len = (uint16)(r0kh_len & 0xffff);
	pfx[npfx++].data = r0kh;

	/* sta addr */
	pfx[npfx].len = ETHER_ADDR_LEN;
	pfx[npfx++].data = (const uint8 *)sta_ea;

	hmac_sha2_n(hash_type, pmk, pmk_len, pfx, npfx, NULL, 0, out, out_len);
	(void)memcpy_s(pmkr0, pmk_len, out, pmk_len);

	/* coverity checks overflow if pfx size changes */

	/* create prefixes for pmkr0 name */
	pfx2[npfx2].len = sizeof(FT_R0N_PFX) - 1;
	pfx2[npfx2++].data = (uint8 *)FT_R0N_PFX;
	pfx2[npfx2].len = WPA2_PMKID_LEN;
	pfx2[npfx2++].data = &out[pmk_len];

	(void)sha2(hash_type, pfx2, npfx2, NULL, 0, pmkr0name, WPA2_PMKID_LEN);
}

void
wpa_calc_pmkR1(sha2_hash_type_t hash_type, const struct ether_addr *r1kh,
	const struct ether_addr *sta_ea, const uint8 *pmk, uint pmk_len, const uint8 *pmkr0name,
	uint8 *pmkr1, uint8 *pmkr1name)
{
	bcm_const_xlvp_t pfx[3];
	bcm_const_xlvp_t pfx2[4];
	int npfx = 0;
	int npfx2 = 0;

	if (!pmkr1 && !pmkr1name)
		goto done;
	else if (!pmkr1)
		goto calc_r1name;

	/* create prefixes for pmkr1 */
	pfx[npfx].len = sizeof(FT_R1_PFX) - 1;
	pfx[npfx++].data = (uint8 *)FT_R1_PFX;

	pfx[npfx].len = ETHER_ADDR_LEN;
	pfx[npfx++].data = (const uint8 *)r1kh;

	pfx[npfx].len = ETHER_ADDR_LEN;
	pfx[npfx++].data = (const uint8 *)sta_ea;

	hmac_sha2_n(hash_type, pmk, pmk_len, pfx, npfx, NULL, 0,
		pmkr1, sha2_digest_len(hash_type));

calc_r1name:
	/* create prefixes for pmkr1 name */
	pfx2[npfx2].len = sizeof(FT_R1N_PFX) - 1;
	pfx2[npfx2++].data = (uint8 *)FT_R1N_PFX;

	pfx2[npfx2].len = WPA2_PMKID_LEN;
	pfx2[npfx2++].data = pmkr0name;

	pfx2[npfx2].len = ETHER_ADDR_LEN;
	pfx2[npfx2++].data = (const uint8 *)r1kh;

	pfx2[npfx2].len = ETHER_ADDR_LEN;
	pfx2[npfx2++].data = (const uint8 *)sta_ea;

	sha2(hash_type, pfx2, npfx2, NULL, 0, pmkr1name, WPA2_PMKID_LEN);
done:;
}

void
wpa_calc_ft_ptk(sha2_hash_type_t hash_type,
	const struct ether_addr *bssid, const struct ether_addr *sta_ea,
	const uint8 *anonce, const uint8* snonce,
	const uint8 *pmk, uint pmk_len, uint8 *ptk, uint ptk_len)
{
	bcm_const_xlvp_t pfx[5];
	int npfx = 0;

	/* FT-PTK||SNONCE||ANONCE||BSSID||STA Addr */

	pfx[npfx].len = sizeof(FT_PTK_PFX) - 1;
	pfx[npfx++].data = (uint8 *)FT_PTK_PFX;

	pfx[npfx].len = EAPOL_WPA_KEY_NONCE_LEN;
	pfx[npfx++].data = snonce;

	pfx[npfx].len = EAPOL_WPA_KEY_NONCE_LEN;
	pfx[npfx++].data = anonce;

	pfx[npfx].len = ETHER_ADDR_LEN;
	pfx[npfx++].data = (const uint8 *)bssid;

	pfx[npfx].len = ETHER_ADDR_LEN;
	pfx[npfx++].data = (const uint8 *)sta_ea;

	hmac_sha2_n(hash_type, pmk, pmk_len, pfx, npfx, NULL, 0, ptk, ptk_len);
}

void
wpa_derive_pmkR1_name(sha2_hash_type_t hash_type,
	struct ether_addr *r1kh, struct ether_addr *sta_ea,
	uint8 *pmkr0name, uint8 *pmkr1name)
{
	wpa_calc_pmkR1(hash_type, r1kh, sta_ea, NULL /* pmk */, 0,
		pmkr0name, NULL /* pmkr1 */, pmkr1name);
}
#endif /* WLFBT || WLHOSTFBT */
#endif /* BCMSUP_PSK || WLFBT || WL_OKC */

#if defined(BCMSUP_PSK) || defined(GTKOE) || defined(BCMAUTH_PSK) || defined(WLFBT)
/* Decrypt a key data from a WPA key message */
int
wpa_decr_key_data(eapol_wpa_key_header_t *body, uint16 key_info, uint8 *ekey,
	uint8 *encrkey, rc4_ks_t *rc4key, const rsn_ie_info_t *rsn_info, uint16 *dec_len)
{
	uint16 len;
	int err = BCME_OK;
	uint8 *key_data;

	switch (key_info & (WPA_KEY_DESC_V1 | WPA_KEY_DESC_V2)) {
	case WPA_KEY_DESC_V1:
		err = memcpy_s(encrkey, EAPOL_WPA_KEY_IV_LEN + EAPOL_WPA_ENCR_KEY_MAX_LEN,
			body->iv, EAPOL_WPA_KEY_IV_LEN);
		if (err) {
			ASSERT(0);
			return err;
		}
		err = memcpy_s(&encrkey[EAPOL_WPA_KEY_IV_LEN], EAPOL_WPA_ENCR_KEY_MAX_LEN,
			ekey, rsn_info->kek_len);
		if (err) {
			ASSERT(0);
			return err;
		}
		/* decrypt the key data */
		prepare_key(encrkey, EAPOL_WPA_KEY_IV_LEN + rsn_info->kek_len, rc4key);
		rc4(NULL, WPA_KEY_DATA_LEN_256, rc4key); /* dump 256 bytes */
		len = ntoh16_ua(EAPOL_WPA_KEY_HDR_DATA_LEN_PTR(body, rsn_info->kck_mic_len));
		key_data = EAPOL_WPA_KEY_HDR_DATA_PTR(body, rsn_info->kck_mic_len);
		rc4(key_data, len, rc4key);
		break;

	case WPA_KEY_DESC_V2:
	case WPA_KEY_DESC_V3:
	case WPA_KEY_DESC_V0:
		/* fallthrough */
		len = ntoh16_ua(EAPOL_WPA_KEY_HDR_DATA_LEN_PTR(body, rsn_info->kck_mic_len));
		if (!len) {
			*dec_len = 0;
			break; /* ignore zero length */
		}
		key_data = EAPOL_WPA_KEY_HDR_DATA_PTR(body, rsn_info->kck_mic_len);
		if (aes_unwrap(rsn_info->kek_len, ekey, len, key_data, key_data)) {
			*dec_len = 0;
			err = BCME_DECERR;
			break;
		}
		*dec_len = (len > AKW_BLOCK_LEN) ? (len - AKW_BLOCK_LEN) : 0;
		break;

	default:
		*dec_len = 0;
		err = BCME_UNSUPPORTED; /* may need revisiting - see 802.11-2016 */
		break;
	}

	return err;
}

/* internal function - assumes enouch space allocated, retuns written number */
static int
wpa_calc_ptk_prefixes(const uint8 *prefix, uint prefix_len,
	const struct ether_addr *auth_ea, const struct ether_addr *sta_ea,
	const uint8 *anonce, uint8 anonce_len, const uint8 *snonce, uint8 snonce_len,
	bcm_const_xlvp_t *pfx)
{
	int npfx = 0;
	const uint8 *nonce;

	/* prefix || min ea || max ea || min nonce || max nonce */
	pfx[npfx].len = (uint16)(prefix_len & 0xffff);
	pfx[npfx++].data = prefix;

	pfx[npfx].len = ETHER_ADDR_LEN;
	pfx[npfx++].data = (const uint8 *) wpa_array_cmp(MIN_ARRAY,
		(const uint8 *)auth_ea, (const uint8 *)sta_ea, ETHER_ADDR_LEN);

	pfx[npfx].len = ETHER_ADDR_LEN;
	pfx[npfx++].data = (const uint8 *) wpa_array_cmp(MAX_ARRAY,
		(const uint8 *)auth_ea, (const uint8 *)sta_ea, ETHER_ADDR_LEN);

	nonce = (const uint8 *)wpa_array_cmp(MIN_ARRAY, snonce, anonce, snonce_len);

	if (nonce == snonce) {
		pfx[npfx].len = snonce_len;
		pfx[npfx++].data = snonce;
		pfx[npfx].len = anonce_len;
		pfx[npfx++].data = anonce;
	} else {
		pfx[npfx].len = anonce_len;
		pfx[npfx++].data = anonce;
		pfx[npfx].len = snonce_len;
		pfx[npfx++].data = snonce;
	}

	return npfx;
}

void
kdf_calc_ptk(const struct ether_addr *auth_ea, const struct ether_addr *sta_ea,
	const uint8 *anonce, const uint8* snonce,
	const uint8 *pmk, uint pmk_len, uint8 *ptk, uint ptk_len)
{
	bcm_const_xlvp_t pfx[5];
	int npfx;

	/* note: kdf omits trailing NULL in prefix */
	npfx = wpa_calc_ptk_prefixes((uint8 *)WPA_PTK_PFX, sizeof(WPA_PTK_PFX) - 1,
		auth_ea, sta_ea, anonce, EAPOL_WPA_KEY_NONCE_LEN, snonce,
		EAPOL_WPA_KEY_NONCE_LEN, pfx);
	hmac_sha2_n(HASH_SHA256, pmk, pmk_len, pfx, npfx, NULL, 0, ptk, ptk_len);
}
#endif	/* BCMSUP_PSK || GTKOE || BCMAUTH_PSK || WLFBT */

#if defined(BCMSUP_PSK) || defined(BCMAUTH_PSK) || defined(WLFBT) || defined(GTKOE)
/* Compute Message Integrity Code (MIC) over EAPOL message */
int
wpa_make_mic(eapol_header_t *eapol, uint key_desc, uint8 *mic_key,
	rsn_ie_info_t *rsn_info, uchar *mic, uint mic_len)
{
	uint data_len;
	int err = BCME_OK;
	sha2_hash_type_t type = HASH_NONE;

	/* length of eapol pkt from the version field on */
	data_len = 4 + ntoh16_ua((uint8 *)&eapol->length);

	/* Create the MIC for the pkt */
	switch (key_desc) {
		case WPA_KEY_DESC_V1:
			type = HASH_MD5;
			break;
		case WPA_KEY_DESC_V2:
			/* note: transparent truncation to mic_len */
			type = HASH_SHA1;
			break;
		case WPA_KEY_DESC_V3:
			aes_cmac_calc(NULL, 0, &eapol->version, data_len, mic_key,
				mic_len, mic, AES_BLOCK_SZ);
			goto exit;
		case WPA_KEY_DESC_V0:
			ASSERT(rsn_info != NULL);
			if (rsn_info == NULL) {
				return BCME_BADARG;
			}
			if (IS_SAE_AKM(rsn_info->sta_akm)) {
				aes_cmac_calc(NULL, 0, &eapol->version, data_len, mic_key,
					mic_len, mic, AES_BLOCK_SZ);
					goto exit;
			}
			type = bcmwpa_rsn_akm_to_hash(rsn_info->sta_akm);
			break;
		default:
			/* 11mc D8.0 some AKMs use descriptor version 0 */
			err = BCME_UNSUPPORTED;
			goto exit;
	}

	if (type) {
		err = hmac_sha2(type, mic_key, mic_len, NULL, 0, (uint8 *)&eapol->version, data_len,
			mic, mic_len);
	}
exit:
	return err;
}

int
wpa_calc_ptk(rsn_akm_t akm, const struct ether_addr *auth_ea, const struct ether_addr *sta_ea,
	const uint8 *anonce, uint8 anon_len, const uint8 *snonce, uint8 snon_len, const uint8 *pmk,
	uint pmk_len, uint8 *ptk, uint ptk_len)
{
	bcm_const_xlvp_t pfx[PRF_PREFIXES_NUM];
	int npfx;
	int ret = BCME_OK;
	sha2_hash_type_t hash_type;
	uint label_len;

	if (RSN_AKM_USE_KDF(akm)) {
		label_len = sizeof(WPA_PTK_PFX) - 1u;
	} else { //WPA AKMS
		label_len = sizeof(WPA_PTK_PFX); /* note: wpa needs trailing NULL in prefix */
	}

	hash_type = bcmwpa_rsn_akm_to_hash(akm);

	npfx = wpa_calc_ptk_prefixes((uint8 *)WPA_PTK_PFX, label_len,
		auth_ea, sta_ea, anonce, anon_len, snonce, snon_len, pfx);
	ret = hmac_sha2_n(hash_type, pmk, pmk_len, pfx, npfx, NULL, 0, ptk, ptk_len);
	return ret;
}

bool
wpa_encr_key_data(eapol_wpa_key_header_t *body, uint16 key_info, uint8 *ekey,
	uint8 *gtk, uint8 *data, uint8 *encrkey, rc4_ks_t *rc4key, const rsn_ie_info_t *rsn_info)
{
	uint16 len;
	uint8 *key_data;

	switch (key_info & (WPA_KEY_DESC_V1 | WPA_KEY_DESC_V2)) {
		case WPA_KEY_DESC_V1:
			if (gtk) {
				len = ntoh16_ua((uint8 *)&body->key_len);
			} else {
				len = ntoh16_ua(EAPOL_WPA_KEY_HDR_DATA_LEN_PTR(body,
					rsn_info->kck_mic_len));
			}

			/* create the iv/ptk key */
			if (memcpy_s(encrkey, EAPOL_WPA_KEY_IV_LEN, body->iv, sizeof(body->iv))) {
				return FALSE;
			}
			if (memcpy_s(&encrkey[EAPOL_WPA_KEY_IV_LEN], EAPOL_WPA_ENCR_KEY_DEFAULT_LEN,
				ekey, EAPOL_WPA_ENCR_KEY_DEFAULT_LEN)) {
				return FALSE;
			}
			/* encrypt the key data */
			prepare_key(encrkey, EAPOL_WPA_KEY_IV_LEN + EAPOL_WPA_ENCR_KEY_DEFAULT_LEN,
				rc4key);
			rc4(data, WPA_KEY_DATA_LEN_256, rc4key); /* dump 256 bytes */
			key_data = EAPOL_WPA_KEY_HDR_DATA_PTR(body, rsn_info->kck_mic_len);
			rc4(key_data, len, rc4key);
			break;
		case WPA_KEY_DESC_V2: /* fall through */
		case WPA_KEY_DESC_V3:
		case WPA_KEY_DESC_V0:
			len = ntoh16_ua(EAPOL_WPA_KEY_HDR_DATA_LEN_PTR(body,
					rsn_info->kck_mic_len));
			/* FIXME: data_len is length to encrypt, but need to make sure
			 * buffer is big enought
			 * for expansion.  how?  problem for caller?
			 */
			key_data = EAPOL_WPA_KEY_HDR_DATA_PTR(body, rsn_info->kck_mic_len);
			/* pad if needed - min. 16 bytes, 8 byte aligned */
			/* padding is 0xdd followed by 0's */
			if (len < 2u *AKW_BLOCK_LEN) {
				key_data[len] = WPA2_KEY_DATA_PAD;
				bzero(&key_data[len + 1u], 2u * AKW_BLOCK_LEN - (len + 1u));
				len = 2u *AKW_BLOCK_LEN;
			} else if (len % AKW_BLOCK_LEN) {
				key_data[len] = WPA2_KEY_DATA_PAD;
				bzero(&key_data[len + 1u],
					AKW_BLOCK_LEN - ((len + 1u) % AKW_BLOCK_LEN));
				len += AKW_BLOCK_LEN - (len % AKW_BLOCK_LEN);
			}
			if (aes_wrap(rsn_info->kek_len, ekey, len, key_data, key_data)) {
				return FALSE;
			}
			len += AKW_BLOCK_LEN;
			hton16_ua_store(len,
				(uint8 *)EAPOL_WPA_KEY_HDR_DATA_LEN_PTR(body,
				rsn_info->kck_mic_len));
			break;
		default:
			/* 11mc D8.0 key descriptor version 0 used */
			return FALSE;
	}

	return TRUE;
}

/* Check MIC of EAPOL message */
bool
wpa_check_mic(eapol_header_t *eapol, uint key_desc, uint8 *mic_key, rsn_ie_info_t *rsn_info)
{
	eapol_wpa_key_header_t *body = NULL;
	uchar digest[SHA2_MAX_DIGEST_LEN];
	uchar mic[EAPOL_WPA_KEY_MAX_MIC_LEN];

	if (!mic_key || !rsn_info || !eapol) {
		return FALSE;
	}

	body = (eapol_wpa_key_header_t *)eapol->body;

#ifndef EAPOL_KEY_HDR_VER_V2
	if (rsn_info->kck_mic_len != EAPOL_WPA_KCK_DEFAULT_LEN)
#else
	if (rsn_info->kck_mic_len > EAPOL_WPA_KEY_MAX_MIC_LEN)
#endif /* EAPOL_KEY_HDR_VER_V2 */
	{
		ASSERT(0);
		return FALSE;
	}
	/* save MIC and clear its space in message */
	if (memcpy_s(mic, sizeof(mic), EAPOL_WPA_KEY_HDR_MIC_PTR(body),
		rsn_info->kck_mic_len)) {
		return FALSE;
	}
	bzero(EAPOL_WPA_KEY_HDR_MIC_PTR(body), rsn_info->kck_mic_len);
	if (wpa_make_mic(eapol, key_desc, mic_key, rsn_info, digest, rsn_info->kck_mic_len)
		!= BCME_OK) {
		return FALSE;
	}
	return !memcmp(digest, mic, rsn_info->kck_mic_len);
}

static sha2_hash_type_t bcmwpa_rsn_akm_to_hash(const rsn_akm_t akm)
{
	uint i = 0;
	sha2_hash_type_t type = HASH_NONE;

	for (i = 0; i < ARRAYSIZE(rsn_akm_lookup_tbl); i++) {
		if (akm == rsn_akm_lookup_tbl[i].rsn_akm) {
			type = rsn_akm_lookup_tbl[i].hash_type;
			break;
		}
	}
	return type;
}
#endif /* BCMSUP_PSK || BCMAUTH_PSK  || WLFBT || GTKOE */

#ifdef WLTDLS
void
wpa_calc_tpk(const struct ether_addr *init_ea, const struct ether_addr *resp_ea,
	const struct ether_addr *bssid, const uint8 *anonce, const uint8* snonce,
	uint8 *tpk, uint tpk_len)
{
	uint8 pmk[SHA2_MAX_DIGEST_LEN];
	uint pmk_len;
	bcm_const_xlvp_t ikpfx[2];
	int nikpfx = 0;
	bcm_const_xlvp_t  tpkpfx[4];
	int ntpkpfx = 0;

	pmk_len = sha2_digest_len(HASH_SHA256);

	/* compute pmk to use - using anonce and snonce  - min and then max */
	ikpfx[nikpfx].len = EAPOL_WPA_KEY_NONCE_LEN;
	ikpfx[nikpfx++].data = wpa_array_cmp(MIN_ARRAY, snonce, anonce,
	                    EAPOL_WPA_KEY_NONCE_LEN),

	ikpfx[nikpfx].len = EAPOL_WPA_KEY_NONCE_LEN;
	ikpfx[nikpfx++].data = wpa_array_cmp(MAX_ARRAY, snonce, anonce,
	                    EAPOL_WPA_KEY_NONCE_LEN),

	(void)sha2(HASH_SHA256, ikpfx, nikpfx, NULL, 0, pmk, SHA2_SHA256_DIGEST_LEN);

	/* compute the tpk - using prefix, min ea, max ea, bssid */
	tpkpfx[ntpkpfx].len = sizeof(TDLS_PMK_PFX) - 1;
	tpkpfx[ntpkpfx++].data = (const uint8 *)TDLS_PMK_PFX;

	tpkpfx[ntpkpfx].len = ETHER_ADDR_LEN;
	tpkpfx[ntpkpfx++].data = wpa_array_cmp(MIN_ARRAY, (const uint8 *)init_ea,
		(const uint8 *)resp_ea, ETHER_ADDR_LEN),

	tpkpfx[ntpkpfx].len = ETHER_ADDR_LEN;
	tpkpfx[ntpkpfx++].data = wpa_array_cmp(MAX_ARRAY, (const uint8 *)init_ea,
		(const uint8 *)resp_ea, ETHER_ADDR_LEN),

	tpkpfx[ntpkpfx].len = ETHER_ADDR_LEN;
	tpkpfx[ntpkpfx++].data = (const uint8 *)bssid;

	(void)hmac_sha2_n(HASH_SHA256, pmk, pmk_len, tpkpfx, ntpkpfx, NULL, 0, tpk, tpk_len);
}
#endif /* WLTDLS */

/* Convert WPA/WPA2 IE cipher suite to locally used value */
static bool
rsn_cipher(wpa_suite_t *suite, ushort *cipher, const uint8 *std_oui, bool wep_ok)
{
	bool ret = TRUE;

	if (!memcmp((const char *)suite->oui, std_oui, DOT11_OUI_LEN)) {
		switch (suite->type) {
		case WPA_CIPHER_TKIP:
			*cipher = CRYPTO_ALGO_TKIP;
			break;
		case WPA_CIPHER_AES_CCM:
			*cipher = CRYPTO_ALGO_AES_CCM;
			break;
		case WPA_CIPHER_AES_GCM:
			*cipher = CRYPTO_ALGO_AES_GCM;
			break;
		case WPA_CIPHER_AES_GCM256:
			*cipher = CRYPTO_ALGO_AES_GCM256;
			break;
		case WPA_CIPHER_WEP_40:
			if (wep_ok)
				*cipher = CRYPTO_ALGO_WEP1;
			else
				ret = FALSE;
			break;
		case WPA_CIPHER_WEP_104:
			if (wep_ok)
				*cipher = CRYPTO_ALGO_WEP128;
			else
				ret = FALSE;
			break;
		default:
			ret = FALSE;
			break;
		}
		return ret;
	}

	return FALSE;
}

bool
wpa_cipher(wpa_suite_t *suite, ushort *cipher, bool wep_ok)
{
	return rsn_cipher(suite, cipher, (const uchar*)WPA_OUI, wep_ok);
}

bool
wpa2_cipher(wpa_suite_t *suite, ushort *cipher, bool wep_ok)
{
	return rsn_cipher(suite, cipher, (const uchar*)WPA2_OUI, wep_ok);
}

/* Is any of the tlvs the expected entry? If
 * not update the tlvs buffer pointer/length.
 */
bool
bcm_has_ie(uint8 *ie, uint8 **tlvs, uint *tlvs_len, const uint8 *oui, uint oui_len, uint8 type)
{
	/* If the contents match the OUI and the type */
	if (ie[TLV_LEN_OFF] >= oui_len + 1 &&
	    !memcmp(&ie[TLV_BODY_OFF], oui, oui_len) &&
	    type == ie[TLV_BODY_OFF + oui_len]) {
		return TRUE;
	}

	/* point to the next ie */
	ie += ie[TLV_LEN_OFF] + TLV_HDR_LEN;
	/* calculate the length of the rest of the buffer */
	*tlvs_len -= (uint)(ie - *tlvs);
	/* update the pointer to the start of the buffer */
	*tlvs = ie;

	return FALSE;
}

wpa_ie_fixed_t *
bcm_find_wpaie(uint8 *parse, uint len)
{
	return (wpa_ie_fixed_t *) bcm_find_ie(parse, len, DOT11_MNG_VS_ID,
		WPA_OUI_LEN, (const char*) WPA_OUI, WPA_OUI_TYPE);
}

int
bcm_find_security_ies(uint8 *buf, uint buflen, void **wpa_ie,
		void **rsn_ie)
{
	bcm_tlv_t *tlv = NULL;
	uint totlen = 0;
	uint8 *end = NULL;
	uint len = 0;
	uint tlvs_len = 0;
	uint8 *tlvs = NULL;

	if ((tlv = (bcm_tlv_t*)buf) == NULL ||
			!wpa_ie || !rsn_ie || buflen == 0) {
		return BCME_BADARG;
	}

	totlen = buflen;
	*rsn_ie = *wpa_ie = NULL;
	end = buf;
	end += buflen;

	/* find rsn ie and wpa ie */
	while (totlen >= TLV_HDR_LEN) {
		len = tlv->len;
		tlvs_len = buflen;
		tlvs = buf;

		/* check if tlv overruns buffer */
		if (totlen < (len + TLV_HDR_LEN)) {
			return BCME_BUFTOOSHORT;
		}

		/* validate remaining totlen */
		if (totlen >= (len + TLV_HDR_LEN)) {
			if ((*rsn_ie == NULL) && (tlv->id == DOT11_MNG_RSN_ID)) {
				*rsn_ie = tlv;
			} else if ((*wpa_ie == NULL) && (tlv->id == DOT11_MNG_VS_ID)) {
				/* if vendor ie, check if its wpa ie */
				if (bcm_is_wpa_ie((uint8 *)tlv, &tlvs, &tlvs_len))
					*wpa_ie = tlv;
			}
		}

		if (*rsn_ie && *wpa_ie)
			break;

		tlv = (bcm_tlv_t*)((uint8*)tlv + (len + TLV_HDR_LEN));
		totlen -= (len + TLV_HDR_LEN);

		if (totlen > buflen) {
			return BCME_BUFTOOLONG;
		}

		if ((uint8 *)tlv > end) {
			return BCME_BUFTOOSHORT;
		}

	}

	if (*wpa_ie || *rsn_ie)
		return BCME_OK;
	else
		return BCME_NOTFOUND;
}

bcm_tlv_t *
bcm_find_wmeie(uint8 *parse, uint len, uint8 subtype, uint8 subtype_len)
{
	bcm_tlv_t *ie;
	if ((ie = bcm_find_ie(parse, len, DOT11_MNG_VS_ID, WME_OUI_LEN,
		(const char*) WME_OUI, WME_OUI_TYPE))) {
		uint ie_len = TLV_HDR_LEN + ie->len;
		wme_ie_t *ie_data = (wme_ie_t *)ie->data;
		/* the subtype_len must include OUI+type+subtype */
		if (subtype_len > WME_OUI_LEN + 1 &&
		    ie_len == (uint)TLV_HDR_LEN + subtype_len &&
		    ie_data->subtype == subtype) {
			return ie;
		}
		/* move to next IE */
		len -= (uint)((uint8 *)ie + ie_len - parse);
		parse = (uint8 *)ie + ie_len;
	}
	return NULL;
}

wps_ie_fixed_t *
bcm_find_wpsie(const uint8 *parse, uint len)
{
	uint8 type = WPS_OUI_TYPE;

	return (wps_ie_fixed_t *)bcm_find_vendor_ie(parse, len, WPS_OUI, &type, sizeof(type));
}

/* locate the Attribute in the WPS IE */
/* assume the caller has validated the WPS IE tag and length */
wps_at_fixed_t *
bcm_wps_find_at(wps_at_fixed_t *at, uint len, uint16 id)
{
	while ((int)len >= WPS_AT_FIXED_LEN) {
		uint alen = WPS_AT_FIXED_LEN + ntoh16_ua(((wps_at_fixed_t *)at)->len);
		if (ntoh16_ua(((wps_at_fixed_t *)at)->at) == id && alen <= len)
			return at;
		at = (wps_at_fixed_t *)((uint8 *)at + alen);
		len -= alen;
	}
	return NULL;
}

#ifdef WLP2P
wifi_p2p_ie_t *
bcm_find_p2pie(const uint8 *parse, uint len)
{
	uint8 type = P2P_OUI_TYPE;

	return (wifi_p2p_ie_t *)bcm_find_vendor_ie(parse, len, P2P_OUI, &type, sizeof(type));
}
#endif

bcm_tlv_t *
bcm_find_hs20ie(uint8 *parse, uint len)
{
	return bcm_find_ie(parse, len, DOT11_MNG_VS_ID, WFA_OUI_LEN,
		(const char *)WFA_OUI, WFA_OUI_TYPE_HS20);
}

bcm_tlv_t *
bcm_find_osenie(uint8 *parse, uint len)
{
	return bcm_find_ie(parse, len, DOT11_MNG_VS_ID, WFA_OUI_LEN,
		(const char *) WFA_OUI, WFA_OUI_TYPE_OSEN);
}

#if defined(BCMSUP_PSK) || defined(BCMSUPPL) || defined(GTKOE) || defined(WL_FILS)
#define wpa_is_kde(ie, tlvs, len, type)	bcm_has_ie(ie, tlvs, len, \
	(const uint8 *)WPA2_OUI, WPA2_OUI_LEN, type)

eapol_wpa2_encap_data_t *
wpa_find_kde(const uint8 *parse, uint len, uint8 type)
{
	return (eapol_wpa2_encap_data_t *) bcm_find_ie(parse, len,
		DOT11_MNG_PROPR_ID, WPA2_OUI_LEN, (const char *) WPA2_OUI, type);
}

bool
wpa_is_gtk_encap(uint8 *ie, uint8 **tlvs, uint *tlvs_len)
{
	return wpa_is_kde(ie, tlvs, tlvs_len, WPA2_KEY_DATA_SUBTYPE_GTK);
}

eapol_wpa2_encap_data_t *
wpa_find_gtk_encap(uint8 *parse, uint len)
{
	eapol_wpa2_encap_data_t *data;

	/* minimum length includes kde upto gtk field in eapol_wpa2_key_gtk_encap_t */
	data = wpa_find_kde(parse, len, WPA2_KEY_DATA_SUBTYPE_GTK);
	if (data && (data->length < EAPOL_WPA2_GTK_ENCAP_MIN_LEN)) {
		data = NULL;
	}

	return data;
}

int
wpa_find_eapol_kde_data(eapol_header_t* eapol, uint8 eapol_mic_len,
	uint8 subtype, eapol_wpa2_encap_data_t **out_data)
{
	eapol_wpa_key_header_t *body;
	uint8 *parse;
	uint16 body_len;
	uint16 data_len;

	if (!eapol) {
		return BCME_BADARG;
	}

	body = (eapol_wpa_key_header_t *)eapol->body;
	body_len = ntoh16_ua(&eapol->length);

	data_len = ntoh16_ua(EAPOL_WPA_KEY_HDR_DATA_LEN_PTR(body,
			eapol_mic_len));

	parse = EAPOL_WPA_KEY_HDR_DATA_PTR(body, eapol_mic_len);

	if (((uint8 *)body + body_len) < ((uint8 *)parse + data_len)) {
		return BCME_BUFTOOSHORT;
	}

	return wpa_find_kde_data(parse, data_len, subtype, out_data);
}

int
wpa_find_kde_data(const uint8 *kde_buf, uint16 buf_len,
	uint8 subtype, eapol_wpa2_encap_data_t **out_data)
{
	eapol_wpa2_encap_data_t *data;
	uint8 min_len;

	if (!kde_buf) {
		return BCME_BADARG;
	}

	/* minimum length includes kde upto gtk field in eapol_wpa2_key_gtk_encap_t */
	data = wpa_find_kde(kde_buf, buf_len, subtype);
	if (!data) {
		return BCME_IE_NOTFOUND;
	}

	switch (subtype) {
	case WPA2_KEY_DATA_SUBTYPE_GTK:
		min_len = EAPOL_WPA2_GTK_ENCAP_MIN_LEN;
		break;
	case WPA2_KEY_DATA_SUBTYPE_IGTK:
		min_len = EAPOL_WPA2_BIGTK_ENCAP_MIN_LEN;
		break;
	case WPA2_KEY_DATA_SUBTYPE_BIGTK:
		min_len = EAPOL_WPA2_IGTK_ENCAP_MIN_LEN;
		break;
#ifdef WL_OCV
	case WPA2_KEY_DATA_SUBTYPE_OCI:
		min_len = EAPOL_WPA2_OCI_ENCAP_MIN_LEN;
		break;
#endif /* WL_OCV */
	default:
		return BCME_UNSUPPORTED;
	}

	if (data->length < min_len) {
		return BCME_BADLEN;
	}

	*out_data = data;

	return BCME_OK;
}

#ifdef WL_OCV
bool
wpa_check_ocv_caps(uint16 local_caps, uint16 peer_caps)
{
	bool ocv_enabled =
		((local_caps & RSN_CAP_OCVC) &&
		(peer_caps & RSN_CAP_OCVC));
	bool mfp_enabled =
		((peer_caps & RSN_CAP_MFPC) ||
		(peer_caps & RSN_CAP_MFPR));

	return (ocv_enabled && mfp_enabled);
}

int
wpa_add_oci_encap(chanspec_t chspec, uint8* buf, uint buf_len)
{
	int retval = BCME_OK;
	eapol_wpa2_encap_data_t* oci_kde;
	uint len = buf_len;

	if (buf_len < WPA_OCV_OCI_KDE_SIZE) {
		retval = BCME_BUFTOOSHORT;
		goto done;
	}

	oci_kde = (eapol_wpa2_encap_data_t*)buf;

	oci_kde->type = DOT11_MNG_WPA_ID;
	oci_kde->subtype = WPA2_KEY_DATA_SUBTYPE_OCI;
	oci_kde->length = (WPA_OCV_OCI_KDE_SIZE - TLV_HDR_LEN);

	oci_kde->oui[0u] = WPA2_OUI[0u];
	oci_kde->oui[1u] = WPA2_OUI[1u];
	oci_kde->oui[2u] = WPA2_OUI[2u];

	buf += EAPOL_WPA2_ENCAP_DATA_HDR_LEN;
	len -= EAPOL_WPA2_ENCAP_DATA_HDR_LEN;

	retval = bcm_ocv_write_oci(chspec, buf, len);
	if (retval != BCME_OK) {
		goto done;
	}

done:
	return retval;
}

int
wpa_add_oci_ie(chanspec_t chspec, uint8* buf, uint buf_len)
{
	int retval = BCME_OK;
	uint8* oci_buf = buf + BCM_TLV_EXT_HDR_SIZE;

	if (buf_len < (bcm_ocv_get_oci_len() + BCM_TLV_EXT_HDR_SIZE)) {
		retval = BCME_BUFTOOSHORT;
		goto done;
	}

	retval = bcm_ocv_write_oci(chspec, oci_buf, bcm_ocv_get_oci_len());
	if (retval != BCME_OK) {
		goto done;
	}

	(void)bcm_write_tlv_ext(DOT11_MNG_ID_EXT_ID,
		OCV_EXTID_MNG_OCI_ID, oci_buf, bcm_ocv_get_oci_len(), buf);

done:
	return retval;
}

int
wpa_add_oci_ft_subelem(chanspec_t chspec, uint8* buf, uint buf_len)
{
	int retval = BCME_OK;
	uint8* oci_buf = buf + BCM_TLV_HDR_SIZE;

	if (buf_len < (bcm_ocv_get_oci_len() + BCM_TLV_HDR_SIZE)) {
		retval = BCME_BUFTOOSHORT;
		goto done;
	}

	retval = bcm_ocv_write_oci(chspec, oci_buf, bcm_ocv_get_oci_len());
	if (retval != BCME_OK) {
		goto done;
	}

	bcm_write_tlv_safe(DOT11_FBT_SUBELEM_ID_OCI,
		oci_buf, bcm_ocv_get_oci_len(), buf, buf_len);

done:
	return retval;
}

int wpa_validate_oci_encap(chanspec_t chspec, const uint8* buf, uint buf_len)
{
	int retval = BCME_OK;
	eapol_wpa2_encap_data_t *encap = NULL;

	retval = wpa_find_kde_data(buf, buf_len, WPA2_KEY_DATA_SUBTYPE_OCI, &encap);
	if (retval != BCME_OK) {
		retval = BCME_NOTFOUND;
		goto done;
	}

	retval = bcm_ocv_validate_oci(chspec,
		encap->data, encap->length);
	if (retval != BCME_OK) {
		goto done;
	}

done:
	return retval;
}

int wpa_validate_oci_ie(chanspec_t chspec, const uint8* buf, uint buf_len)
{
	int retval = BCME_OK;
	bcm_tlv_ext_t *oci_ie;

	oci_ie = (bcm_tlv_ext_t *)bcm_parse_tlvs_dot11(buf, buf_len,
			OCV_EXTID_MNG_OCI_ID, TRUE);

	if (!oci_ie) {
		retval = BCME_NOTFOUND;
		goto done;
	}

	retval = bcm_ocv_validate_oci(chspec, oci_ie->data, oci_ie->len);
	if (retval != BCME_OK) {
		goto done;
	}

done:
	return retval;
}

int wpa_validate_oci_ft_subelem(chanspec_t chspec, const uint8* buf, uint buf_len)
{
	int retval = BCME_OK;
	bcm_tlv_t *oci_ie;

	oci_ie = (bcm_tlv_t *)bcm_parse_tlvs_dot11(buf, buf_len,
			DOT11_FBT_SUBELEM_ID_OCI, FALSE);

	if (!oci_ie) {
		retval = BCME_NOTFOUND;
		goto done;
	}

	retval = bcm_ocv_validate_oci(chspec, oci_ie->data, oci_ie->len);
	if (retval != BCME_OK) {
		goto done;
	}

done:
	return retval;
}
#endif /* WL_OCV */
#endif /* defined(BCMSUP_PSK) || defined(BCMSUPPL) || defined(GTKOE) || defined(WL_FILS) */

const uint8 *
wpa_array_cmp(int max_array, const uint8 *x, const uint8 *y, uint len)
{
	uint i;
	const uint8 *ret = x;

	for (i = 0; i < len; i++)
		if (x[i] != y[i])
			break;

	if (i == len) {
		/* returning null will cause crash, return value used for copying */
		/* return first param in this case to close security loophole */
		return x;
	}
	if (max_array && (y[i] > x[i]))
		ret = y;
	if (!max_array && (y[i] < x[i]))
		ret = y;

	return (ret);
}

void
wpa_incr_array(uint8 *array, uint len)
{
	int i;

	for (i = (len-1); i >= 0; i--)
		if (array[i]++ != 0xff) {
			break;
		}
}

bool
bcmwpa_akm2WPAauth(uint8 *akm, uint32 *auth, bool sta_iswpa)
{
	uint i;
	oui_akm_wpa_tbl_t wpa_auth_tbl_match[] = {
		{WPA2_OUI, RSN_AKM_NONE, WPA_AUTH_NONE},
		{WPA2_OUI, RSN_AKM_UNSPECIFIED, WPA2_AUTH_UNSPECIFIED},
		{WPA2_OUI, RSN_AKM_PSK, WPA2_AUTH_PSK},
		{WPA2_OUI, RSN_AKM_FBT_1X, WPA2_AUTH_UNSPECIFIED | WPA2_AUTH_FT},
		{WPA2_OUI, RSN_AKM_FBT_PSK, WPA2_AUTH_PSK | WPA2_AUTH_FT},
		{WPA2_OUI, RSN_AKM_SHA256_1X, WPA2_AUTH_1X_SHA256},
		{WPA2_OUI, RSN_AKM_SHA256_PSK, WPA2_AUTH_PSK_SHA256},
		{WPA2_OUI, RSN_AKM_FILS_SHA256, WPA2_AUTH_FILS_SHA256},
		{WPA2_OUI, RSN_AKM_FILS_SHA384, WPA2_AUTH_FILS_SHA384},
		{WPA2_OUI, RSN_AKM_FBT_SHA256_FILS, WPA2_AUTH_FILS_SHA256 | WPA2_AUTH_FT},
		{WPA2_OUI, RSN_AKM_FBT_SHA384_FILS, WPA2_AUTH_FILS_SHA384 | WPA2_AUTH_FT},
		{WPA2_OUI, RSN_AKM_SAE_PSK, WPA3_AUTH_SAE_PSK},
		{WPA2_OUI, RSN_AKM_SAE_FBT, WPA3_AUTH_SAE_PSK | WPA2_AUTH_FT},
		{WPA2_OUI, RSN_AKM_OWE, WPA3_AUTH_OWE},
		{WPA2_OUI, RSN_AKM_SUITEB_SHA256_1X, WPA3_AUTH_1X_SUITE_B_SHA256},
		{WPA2_OUI, RSN_AKM_SUITEB_SHA384_1X, WPA3_AUTH_1X_SUITE_B_SHA384},
		{WFA_OUI, OSEN_AKM_UNSPECIFIED, WPA2_AUTH_UNSPECIFIED},
		{WFA_OUI, RSN_AKM_FBT_SHA256_FILS, WPA2_AUTH_FILS_SHA256 | WPA2_AUTH_FT},
		{WFA_OUI, RSN_AKM_FBT_SHA384_FILS, WPA2_AUTH_FILS_SHA384 | WPA2_AUTH_FT},
		{WFA_OUI, RSN_AKM_DPP, WPA3_AUTH_DPP_AKM},

#ifdef BCMWAPI_WAI
		{WAPI_OUI, RSN_AKM_NONE, WAPI_AUTH_NONE},
		{WAPI_OUI, RSN_AKM_UNSPECIFIED, WAPI_AUTH_UNSPECIFIED},
		{WAPI_OUI, RSN_AKM_PSK, WAPI_AUTH_PSK},
#endif /* BCMWAPI_WAI */

		{WPA_OUI, RSN_AKM_NONE, WPA_AUTH_NONE},
		{WPA_OUI, RSN_AKM_UNSPECIFIED, WPA_AUTH_UNSPECIFIED},
		{WPA_OUI, RSN_AKM_PSK, WPA_AUTH_PSK},
	};

	BCM_REFERENCE(sta_iswpa);

	for (i = 0; i < ARRAYSIZE(wpa_auth_tbl_match); i++)  {
		if (!memcmp(akm, wpa_auth_tbl_match[i].oui, DOT11_OUI_LEN)) {
			if (wpa_auth_tbl_match[i].rsn_akm == akm[DOT11_OUI_LEN]) {
				*auth = wpa_auth_tbl_match[i].wpa_auth;
				return TRUE;
			}
		}
	}
	return FALSE;
}

/* map cipher suite to internal WSEC_XXXX */
/* cs points 4 byte cipher suite, and only the type is used for non CCX ciphers */
bool
bcmwpa_cipher2wsec(uint8 *cipher, uint32 *wsec)
{

#ifdef BCMWAPI_WAI
	if (!memcmp(cipher, WAPI_OUI, DOT11_OUI_LEN)) {
		switch (WAPI_CSE_WPI_2_CIPHER(cipher[DOT11_OUI_LEN])) {
		case WAPI_CIPHER_NONE:
			*wsec = 0;
			break;
		case WAPI_CIPHER_SMS4:
			*wsec = SMS4_ENABLED;
			break;
		default:
			return FALSE;
		}
		return TRUE;
	}
#endif /* BCMWAPI_WAI */

	switch (cipher[DOT11_OUI_LEN]) {
	case WPA_CIPHER_NONE:
		*wsec = 0;
		break;
	case WPA_CIPHER_WEP_40:
	case WPA_CIPHER_WEP_104:
		*wsec = WEP_ENABLED;
		break;
	case WPA_CIPHER_TKIP:
		*wsec = TKIP_ENABLED;
		break;
	case WPA_CIPHER_AES_CCM:
		/* fall through */
	case WPA_CIPHER_AES_GCM:
		/* fall through */
	case WPA_CIPHER_AES_GCM256:
		*wsec = AES_ENABLED;
		break;

#ifdef BCMWAPI_WAI
	case WAPI_CIPHER_SMS4:
		*wsec = SMS4_ENABLED;
		break;
#endif /* BCMWAPI_WAI */

	default:
		return FALSE;
	}
	return TRUE;
}

#ifdef RSN_IE_INFO_STRUCT_RELOCATED
/* map WPA/RSN cipher to internal WSEC */
uint32
bcmwpa_wpaciphers2wsec(uint32 wpacipher)
{
	uint32 wsec = 0;

	switch (wpacipher) {
	case BCM_BIT(WPA_CIPHER_WEP_40):
		case BCM_BIT(WPA_CIPHER_WEP_104):
			wsec = WEP_ENABLED;
			break;
		case BCM_BIT(WPA_CIPHER_TKIP):
			wsec = TKIP_ENABLED;
			break;
		case BCM_BIT(WPA_CIPHER_AES_OCB):
			/* fall through */
		case BCM_BIT(WPA_CIPHER_AES_CCM):
			wsec = AES_ENABLED;
			break;
		case BCM_BIT(WPA_CIPHER_AES_GCM):
			/* fall through */
		case BCM_BIT(WPA_CIPHER_AES_GCM256):
			wsec = AES_ENABLED;
		break;

#ifdef BCMWAPI_WAI
		case BCM_BIT(WAPI_CIPHER_SMS4):
			wsec = SMS4_ENABLED;
			break;
#endif /* BCMWAPI_WAI */

		default:
			break;
	}

	return wsec;
}

uint32
wlc_convert_rsn_to_wsec_bitmap(uint32 ap_cipher_mask)
{

	uint32 ap_wsec = 0;
	uint32 tmp_mask = ap_cipher_mask;
	uint32 c;

	FOREACH_BIT(c, tmp_mask) {
		ap_wsec |= bcmwpa_wpaciphers2wsec(c);
	}

	return ap_wsec;
}

#else /* Not RSN_IE_INFO_STRUCT_RELOCATED */
uint32
bcmwpa_wpaciphers2wsec(uint8 wpacipher)
{
	uint32 wsec = 0;

	switch (wpacipher) {
	case WPA_CIPHER_NONE:
		break;
	case WPA_CIPHER_WEP_40:
	case WPA_CIPHER_WEP_104:
		wsec = WEP_ENABLED;
		break;
	case WPA_CIPHER_TKIP:
		wsec = TKIP_ENABLED;
		break;
	case WPA_CIPHER_AES_OCB:
		/* fall through */
	case WPA_CIPHER_AES_CCM:
		wsec = AES_ENABLED;
		break;
	case WPA_CIPHER_AES_GCM:
	/* fall through */
	case WPA_CIPHER_AES_GCM256:
		wsec = AES_ENABLED;
		break;

#ifdef BCMWAPI_WAI
	case WAPI_CIPHER_SMS4:
		wsec = SMS4_ENABLED;
		break;
#endif /* BCMWAPI_WAI */

	default:
		break;
	}

	return wsec;
}
#endif /* RSN_IE_INFO_STRUCT_RELOCATED */

bool
bcmwpa_is_wpa_auth(uint32 auth)
{
	if ((auth == WPA_AUTH_NONE) ||
	   (auth == WPA_AUTH_UNSPECIFIED) ||
	   (auth == WPA_AUTH_PSK))
		return TRUE;
	else
		return FALSE;
}

bool
bcmwpa_includes_wpa_auth(uint32 auth)
{
	if (auth & (WPA_AUTH_NONE |
		WPA_AUTH_UNSPECIFIED |
		WPA_AUTH_PSK))
		return TRUE;
	else
		return FALSE;
}

bool
bcmwpa_is_rsn_auth(uint32 auth)
{
	auth = auth & ~WPA2_AUTH_FT;

	if ((auth == WPA2_AUTH_UNSPECIFIED) ||
	    (auth == WPA2_AUTH_PSK) ||
	    (auth == BRCM_AUTH_PSK) ||
	    (auth == WPA2_AUTH_1X_SHA256) ||
	    (auth == WPA2_AUTH_PSK_SHA256) ||
	    (auth == WPA3_AUTH_SAE_PSK) ||
	    (auth == WPA3_AUTH_OWE) ||
	    WPA2_AUTH_IS_FILS(auth) ||
	    (auth == WPA3_AUTH_1X_SUITE_B_SHA256) ||
	    (auth == WPA3_AUTH_1X_SUITE_B_SHA384) ||
	    (auth == WPA3_AUTH_PSK_SHA384) ||
	    (auth == WPA3_AUTH_DPP_AKM)) {
		return TRUE;
	} else {
		return FALSE;
	}
}

bool
bcmwpa_includes_rsn_auth(uint32 auth)
{
	if (auth & (WPA2_AUTH_UNSPECIFIED |
	            WPA2_AUTH_PSK |
	            BRCM_AUTH_PSK | WPA2_AUTH_1X_SHA256 | WPA2_AUTH_PSK_SHA256 |
	            WPA2_AUTH_IS_FILS(auth) | WPA3_AUTH_SAE_PSK | WPA3_AUTH_OWE |
	            WPA3_AUTH_1X_SUITE_B_SHA256 | WPA3_AUTH_1X_SUITE_B_SHA384 |
			WPA3_AUTH_PSK_SHA384 | WPA3_AUTH_DPP_AKM))
		return TRUE;
	else
		return FALSE;
}

#ifdef RSN_IE_INFO_STRUCT_RELOCATED
/* decode unicast/multicast cipher in RSNIE */
static int
bcmwpa_decode_cipher_suite(rsn_ie_info_t *info, const uint8 **ptr_inc, uint ie_len, uint
	*remain_len, uint16 *p_count)
{
	const wpa_suite_ucast_t *ucast;
	const wpa_suite_mcast_t *mcast;
	uint i;

	if (!(*remain_len)) {
		info->g_cipher = WPA_CIPHER_UNSPECIFIED;
		info->p_ciphers = WPA_P_CIPHERS_UNSPECIFIED;
		goto done; /* only have upto ver */
	}
	*ptr_inc += ie_len - *remain_len;

	if (*remain_len < sizeof(wpa_suite_mcast_t)) {
		info->parse_status = BCME_BADLEN;
		goto done;
	}
	mcast = (const wpa_suite_mcast_t *)*ptr_inc;

	if (IS_WPA_CIPHER(mcast->type)) {
		info->g_cipher = mcast->type;
	} else {
		info->parse_status = BCME_BAD_IE_DATA;
		goto done;
	}

	/* for rsn pairwise cipher suite */
	*ptr_inc += sizeof(wpa_suite_mcast_t);
	*remain_len -= sizeof(wpa_suite_mcast_t);

	if (!(*remain_len)) {
		info->p_ciphers = WPA_P_CIPHERS_UNSPECIFIED;
		info->sta_akm = WPA_CIPHER_UNSPECIFIED;
		goto done;
	}

	ucast = (const wpa_suite_ucast_t *)*ptr_inc;

	if ((*remain_len) < sizeof(ucast->count)) {
		info->parse_status = BCME_BADLEN;
		goto done;
	}

	if (!ucast->count.low && !ucast->count.high) {
		info->parse_status = BCME_BADLEN;
		goto done;
	}

	*p_count = ltoh16_ua(&ucast->count);
	if (info->dev_type == DEV_STA && *p_count != 1u) {
		info->parse_status = BCME_BAD_IE_DATA;
		goto done;
	}
	if ((*remain_len) < (*p_count * WPA_SUITE_LEN + sizeof(ucast->count))) {
		info->parse_status = BCME_BADLEN;
		goto done;
	}

	if (info->dev_type == DEV_STA) {
		if (IS_WPA_CIPHER(ucast->list[0].type)) {
			/* update the pairwise cipher */
			info->sta_cipher = ucast->list[0].type;
		} else {
			info->parse_status = BCME_BAD_IE_DATA;
			goto done;
		}
	} else {
		for (i = 0; i < *p_count; i++) {
			if (IS_WPA_CIPHER(ucast->list[i].type)) {
				info->p_ciphers |= BIT(ucast->list[i].type);
				info->rsn_p_ciphers = info->p_ciphers;
			} else {
				info->parse_status = BCME_BAD_IE_DATA;
				goto done;
			}
		}
	}

	/* update buffer ptr and remaining length */
	*ptr_inc += (*p_count * WPA_SUITE_LEN) + sizeof(ucast->count);
	*remain_len -= (*p_count * WPA_SUITE_LEN) + sizeof(ucast->count);

done:

	if (info->parse_status == BCME_OK) {
		if (info->g_cipher == WPA_CIPHER_UNSPECIFIED) {
			info->g_cipher = WPA_CIPHER_AES_CCM;
		}
		if (info->p_ciphers == WPA_P_CIPHERS_UNSPECIFIED) {
			info->p_ciphers = BIT(WPA_CIPHER_AES_CCM);
			info->rsn_p_ciphers = info->p_ciphers;
		}
	}

	return info->parse_status;
}
/* sta_akm/sta_cipher must be set before this call */
int
bcmwpa_rsnie_eapol_key_len(rsn_ie_info_t *info)
{
	info->pmk_len = bcmwpa_eapol_key_length(EAPOL_KEY_PMK, info->sta_akm, 0);
	info->kck_mic_len = bcmwpa_eapol_key_length(EAPOL_KEY_KCK_MIC, info->sta_akm, 0);
	info->kck_len = bcmwpa_eapol_key_length(EAPOL_KEY_KCK, info->sta_akm, 0);
	info->kek_len = bcmwpa_eapol_key_length(EAPOL_KEY_KEK, info->sta_akm, 0);
	info->tk_len = bcmwpa_eapol_key_length(EAPOL_KEY_TK, 0, info->sta_cipher);
	info->ptk_len = info->kck_len + info->kek_len + info->tk_len;
#if defined(WL_FILS) && defined(WLFBT)
	info->kck2_len = bcmwpa_eapol_key_length(EAPOL_KEY_KCK2, info->sta_akm, 0);
	info->kek2_len = bcmwpa_eapol_key_length(EAPOL_KEY_KEK2, info->sta_akm, 0);
	if (WPA_IS_FILS_FT_AKM(info->sta_akm)) {
		info->ptk_len += (info->kck2_len + info->kek2_len);
	}
#endif /* WL_FILS && WLFBT */
	return BCME_OK;
}
/* Extract and store information from WPA or RSN IEs
 *
 * called after either
 *  -an association request has been built (STA),
 * - an association was received (AP)
 * - a probe request has been built (AP)
 * - a probe response was received (STA)
 *
 * All available information is extracted to be used for subsequent
 * bss pruning, association request validation, key descriptor compuation etc.
 *
 * To be expanded as needed.
 *
 * ie: RSN IE input
 * rsn_info:  parsed information. Placed in either bsscfg for self, or scb for peer.
 * dev_type: STA_RSN or AP_RSN
 *
 * Return : parse status.
 * NOTE: the parse status is also saved in the the parse_status field.
 * NOTE 2 : the IE itself is copied at the end of the structure. Since there is
 * no reference to the osh available here, the allocation has to happen outside
 * and so the structure cannot be zeroed in this function.
 * For the STA, it should happen everytime.
 * For the AP, it should happen right after a new beacon/probe has been acquired.
 */

int
bcmwpa_parse_rsnie(const bcm_tlv_t *ie, rsn_ie_info_t *info, device_type_t dev_type)
{

	const uint8 *ptr_inc = NULL;
	const wpa_suite_mcast_t *mcast;
	const wpa_suite_auth_key_mgmt_t *mgmt;
	const wpa_pmkid_list_t *pmkid_list;
	uint32 remain_len = 0, i;
	uint8 auth_ie_type;
	uint16 p_count = 0;
	uint16 akm_count;

	ASSERT(info != NULL);

	/* this function might be called from place where there
	 * is no error detection.
	 * e.g. fron the iem callback. Store status here.
	 */

	info->parse_status = BCME_OK;

	if (!ie) {
		info->parse_status = BCME_BADARG;
		goto done;
	}

	/* For AP, do not zero this structure since there could be multiple
	 * IEs. In that case, add to the existing
	 * bits in field (ciphers, akms) as necessary.
	 */
	if (dev_type == DEV_AP) {
		/* if already created, check device type */
		if (info->dev_type != DEV_NONE) {
			if (info->dev_type != DEV_AP) {
				info->parse_status = BCME_BADARG;
				goto done;
			}
		}
	}
	info->dev_type = dev_type;
	ptr_inc = ie->data;

	/* decode auth IE (WPA vs RSN). Fill in the auth_ie_type and version.
	 * Modify remain_len to indicate the position of the pointer.
	 */
	/* NOTE the status field will be updated in this call */
	if (bcmwpa_decode_ie_type(ie, info, &remain_len, &auth_ie_type) != BCME_OK) {
		goto done;
	}

	/* decode multicast, unicast ciphers */
	if (bcmwpa_decode_cipher_suite(info, &ptr_inc, ie->len, &remain_len, &p_count) != BCME_OK) {
		goto done;
	}

	if (!(remain_len)) {
		info->akms = BIT(RSN_AKM_UNSPECIFIED);
		goto done;
	}

	mgmt = (const wpa_suite_auth_key_mgmt_t *)ptr_inc;

	if (remain_len < sizeof(mgmt->count)) {
		info->parse_status = BCME_BADLEN;
		goto done;
	}

	akm_count = ltoh16_ua(&mgmt->count);

	if (!akm_count) {
		info->parse_status = BCME_BADARG;
		goto done;
	}

	if (dev_type == DEV_STA && akm_count != 1) {
		info->parse_status = BCME_BADARG;
		goto done;
	}

	if ((remain_len) < (akm_count * WPA_SUITE_LEN + sizeof(mgmt->count))) {
		info->parse_status = BCME_BADLEN;
		goto done;
	}

	if (dev_type == DEV_STA) {
		info->sta_akm = mgmt->list[0].type;
	}
	for (i = 0; i < akm_count; i++) {
		if (bcmwpa_is_valid_akm(mgmt->list[i].type) == BCME_OK) {
			ASSERT((mgmt->list[i].type) <
				(sizeof(info->akms) * NBBY));
			info->akms |= BIT(mgmt->list[i].type);
		}
	}

	/* save IE dependent values in their respective fields */
	if (dev_type == DEV_AP) {
		if (auth_ie_type == RSN_AUTH_IE) {
			info->rsn_akms = info->akms;
		} else if (auth_ie_type == WPA_AUTH_IE) {
			info->wpa_akms = info->akms;
			info->wpa_p_ciphers = info->p_ciphers;
		}
	}

	/* as a STA,  at this point, we can compute the key descriptor version */
	if (dev_type == DEV_STA) {
		info->key_desc = wlc_calc_rsn_desc_version(info);
		/* For STA, we can set the auth ie */
		if (auth_ie_type == RSN_AUTH_IE) {
			info->auth_ie = info->rsn_ie;
			info->auth_ie_len = info->rsn_ie_len;
		} else {
			info->auth_ie = info->wpa_ie;
			info->auth_ie_len = info->wpa_ie_len;
		}
	}

	/* RSN AKM/cipher suite related EAPOL key length update */
	bcmwpa_rsnie_eapol_key_len(info);

	/* for rsn capabilities */
	ptr_inc += akm_count * WPA_SUITE_LEN + sizeof(mgmt->count);
	remain_len -=  akm_count * WPA_SUITE_LEN + sizeof(mgmt->count);

	if (!(remain_len)) {
		goto done;
	}
	if (remain_len < RSN_CAP_LEN) {
		info->parse_status = BCME_BADLEN;
		goto done;
	}

	if (ie->id == DOT11_MNG_RSN_ID) {
		info->caps = ltoh16_ua(ptr_inc);
	}

	/* check if AKMs require MFP capable to be set */
	if ((info->akms & RSN_MFPC_AKM_MASK) && !(info->caps & RSN_CAP_MFPC)) {
		/* NOTE: Acting as WPA3 CTT testbed device, it requires to send assoc request frame
		with user provided mfp value as is. So should not return error here.
		*/
#ifndef WPA3_CTT
		info->parse_status = BCME_EPERM;
		goto done;
#endif /* WPA3_CTT */
	}

	/* for rsn PMKID */
	ptr_inc += RSN_CAP_LEN;
	remain_len -= RSN_CAP_LEN;

	if (!(remain_len)) {
		goto done;
	}

	/* here's possible cases after RSN_CAP parsed
	 * a) pmkid_count 2B(00 00)
	 * b) pmkid_count 2B(00 00) + BIP 4B
	 * c) pmkid_count 2B(non zero) + pmkid_count * 16B
	 * d) pmkid_count 2B(non zero) + pmkid_count * 16B + BIP 4B
	 */

	/* pmkids_offset set to
	 * 1) if pmkid_count field(2B) present, point to first PMKID offset in the RSN ID
	 *    no matter what pmkid_count value is. (true, even if pmkid_count == 00 00)
	 * 2) if pmkid_count field(2B) not present, it shall be zero.
	 */

	pmkid_list = (const wpa_pmkid_list_t*)ptr_inc;

	if ((remain_len) < sizeof(pmkid_list->count)) {
		info->parse_status = BCME_BADLEN;
		goto done;
	}

	info->pmkid_count = (uint8)ltoh16_ua(&pmkid_list->count);
	ptr_inc += sizeof(pmkid_list->count);
	remain_len -= sizeof(pmkid_list->count);

	if (remain_len < (uint32)(info->pmkid_count * WPA2_PMKID_LEN)) {
		info->parse_status = BCME_BADLEN;
		goto done;
	}

	info->pmkids_offset = ie->len + TLV_HDR_LEN - remain_len;
	/* for rsn group management cipher suite */
	ptr_inc += info->pmkid_count * WPA2_PMKID_LEN;
	remain_len -= info->pmkid_count * WPA2_PMKID_LEN;

	if (!(remain_len)) {
		goto done;
	}
	/*
	 * from WPA2_Security_Improvements_Test_Plan_v1.0
	 * 4.2.4 APUT RSNE bounds verification using WPA2-PSK
	 * May content RSNE extensibile element ay this point
	 */
	if (remain_len < sizeof(wpa_suite_mcast_t)) {
		info->parse_status = BCME_BADLEN;
		goto done;
	}

	mcast = (const wpa_suite_mcast_t *)ptr_inc;
	if (IS_VALID_BIP_CIPHER((rsn_cipher_t)mcast->type)) {
		info->g_mgmt_cipher = (rsn_cipher_t)mcast->type;
	}

done:
	return info->parse_status;
}

/* Determine if the IE is of WPA or RSN type. Decode
 * up to version field. Modify the remaining len parameter to
 * indicate where the next field is.
 * Store and return error status.
 */

int
bcmwpa_decode_ie_type(const bcm_tlv_t *ie, rsn_ie_info_t *info, uint32 *remaining,
    uint8 *type)
{
	const uint8 * ptr_inc = (const uint8 *)ie->data;
	uint32 remain_len = ie->len;
	uint8 version, version_len;

	if (ie->id == DOT11_MNG_WPA_ID) {
		/* min len check */
		if (remain_len < WPA_IE_FIXED_LEN) {
			info->parse_status = BCME_BADLEN;
			goto done;
		}
		/* WPA IE */
		if (memcmp(WPA_OUI, ie->data, WPA_OUI_LEN)) {
			/* bad OUI */
			info->parse_status = BCME_BADARG;
			goto done;
		}
		ptr_inc += WPA_OUI_LEN;
		if (*ptr_inc == WPA_OUI_TYPE) {
			*type = WPA_AUTH_IE;
		} else if  (*ptr_inc == WFA_OUI_TYPE_OSEN) {
			*type = OSEN_AUTH_IE;
		}
		else {
			/* wrong type */
			info->parse_status = BCME_BADARG;
			goto done;
		}

		ptr_inc ++;
		remain_len -= WPA_OUI_LEN + 1u;
		version_len = WPA_VERSION_LEN;
	}
	else if  (ie->id == DOT11_MNG_RSN_ID) {
		if (remain_len < WPA2_VERSION_LEN) {
			info->parse_status = BCME_BADLEN;
			goto done;
		}
		/* RSN IE */
		*type = RSN_AUTH_IE;
		version_len = WPA2_VERSION_LEN;
	} else {
		printf("IE ID %d\n", ie->id);
		/* TODO : add support for CCX, WAPI ? */
		info->parse_status = BCME_UNSUPPORTED;
		goto done;
	}
	info->auth_ie_type |= *type;
	/* mask down to uint8 for Windows build */
	version = 0xff & ltoh16_ua(ptr_inc);
	if (version > MAX_RSNE_SUPPORTED_VERSION) {
		info->parse_status = BCME_UNSUPPORTED;
		goto done;
	}

	info->version = (uint8)version;
	*remaining = remain_len - version_len;
done:
	return info->parse_status;
}

/* rsn info allocation management.
 *
 * In some cases, the rsn ie info structures are embedded in the scan results
 * which can be shared by different lists.
 * To keep track of their allocation, we use a reference counter.
 * The counter is incremented on demand by rsn_ie_info_add_ref()
 * at the time the reference is shared.
 * It is decremented in rsn_ie_info_rel_ref
 * When ref_count gets to 0, bcmwpa_rsn_ie_info_free_mem
 * is called to free the whole structure.
 */

/* free rsn_ie and wpa_ie, if any, and zero the rsn_info */
void
bcmwpa_rsn_ie_info_reset(rsn_ie_info_t *rsn_info, osl_t *osh)
{
	uint8 ref_count;
	if (rsn_info == NULL) {
		return;
	}
	ref_count = rsn_info->ref_count;
	MFREE(osh, rsn_info->rsn_ie, rsn_info->rsn_ie_len);
	MFREE(osh, rsn_info->wpa_ie, rsn_info->wpa_ie_len);
	MFREE(osh, rsn_info->rsnxe, rsn_info->rsnxe_len);
	bzero(rsn_info, sizeof(*rsn_info));
	rsn_info->ref_count = ref_count;

}

static
void bcmwpa_rsn_ie_info_free_mem(rsn_ie_info_t **rsn_info, osl_t *osh)
{
	bcmwpa_rsn_ie_info_reset(*rsn_info, osh);
	MFREE(osh, *rsn_info, sizeof(**rsn_info));
	*rsn_info = NULL;
}

void bcmwpa_rsn_ie_info_rel_ref(rsn_ie_info_t **rsn_info, osl_t *osh)
{

	if (rsn_info == NULL || *rsn_info == NULL) {
		return;
	}

	/* already freed ? */
	if ((*rsn_info)->ref_count == 0) {
		ASSERT(0);
		return;
	}
	/* decrement ref count */
	(*rsn_info)->ref_count -= 1;
	/* clear reference. */
	if ((*rsn_info)->ref_count > 0) {
		*rsn_info = NULL;
		return;
	}
	/* free memory and clear reference */
	bcmwpa_rsn_ie_info_free_mem(rsn_info, osh);
}

int
bcmwpa_rsn_ie_info_add_ref(rsn_ie_info_t *rsn_info)
{
	int status = BCME_OK;
	if (rsn_info == NULL) {
		goto done;
	}
	if (rsn_info->ref_count == 0) {
		/* don't increase from 0, which means this structure has been freed earlier.
		 * That reference should not exist anymore.
		 */
		ASSERT(0);
		status = BCME_BADARG;
		goto  done;
	}
	rsn_info->ref_count++;
done:
	return status;
}

#else /* Not RSN_IE_INFO_STRUCT_RELOCATED */

int
bcmwpa_parse_rsnie(const bcm_tlv_t *ie, rsn_ie_info_t *info, device_type_t dev_type)
{

	const uint8 *ptr_inc = NULL;
	const wpa_suite_ucast_t *ucast;
	const wpa_suite_mcast_t *mcast;
	const wpa_suite_auth_key_mgmt_t *mgmt;
	const wpa_pmkid_list_t *pmkid_list;
	uint32 remain_len = 0, i;

	ASSERT(info != NULL);

	/* this function might be called from place where there
	 * is no error detection.
	 * e.g. fron the iem callback. Store status here.
	 */

	info->parse_status = BCME_OK;

	if (!ie) {
			info->parse_status = BCME_BADARG;
			goto done;
	}

	/* For AP, do not zero this structure since there could be multiple
	 * IEs. In that case, add to the existing
	 * bits in field (ciphers, akms) as necessary.
	 */
	if (dev_type != DEV_AP) {
		bzero(info, sizeof(*info));
	} else {
		/* if already created, check device type */
		if (info->dev_type != DEV_NONE) {
			if (info->dev_type != DEV_AP) {
				info->parse_status = BCME_BADARG;
				goto done;
			}
		}
	}
	info->dev_type = dev_type;
	ptr_inc = ie->data;

	/* decode auth IE (WPA vs RSN). Fill in the auth_ie_type and version.
	 * Modify remain_len to indicate the position of the pointer.
	 */
	/* NOTE the status field will be updated in this call */
	if (bcmwpa_decode_ie_type(ie, info, &remain_len) != BCME_OK) {
		goto done;
	}

	if (!(remain_len)) {
		info->g_cipher = WPA_CIPHER_NONE;
		goto done; /* only have upto ver */
	}
	ptr_inc += ie->len - remain_len;

	if (remain_len < sizeof(wpa_suite_mcast_t)) {
		info->parse_status = BCME_BADLEN;
		goto done;
	}
	mcast = (const wpa_suite_mcast_t *)ptr_inc;

	if (IS_WPA_CIPHER(mcast->type)) {
		info->g_cipher = mcast->type;
	}

	/* for rsn pairwise cipher suite */
	ptr_inc += sizeof(wpa_suite_mcast_t);
	remain_len -= sizeof(wpa_suite_mcast_t);

	if (!(remain_len)) {
		goto done;
	}

	ucast = (const wpa_suite_ucast_t *)ptr_inc;

	if ((remain_len) < sizeof(ucast->count)) {
		info->parse_status = BCME_BADLEN;
		goto done;
	}

	if (!ucast->count.low && !ucast->count.high) {
		info->parse_status = BCME_BADLEN;
		goto done;
	}

	info->p_count = (uint8)ltoh16_ua(&ucast->count);

	if (dev_type == DEV_STA && info->p_count != 1) {
		info->parse_status = BCME_BADARG;
		goto done;
	}
	if ((remain_len) < (info->p_count * WPA_SUITE_LEN + sizeof(ucast->count))) {
		info->parse_status = BCME_BADLEN;
		goto done;
	}

	if (IS_WPA_CIPHER(ucast->list[0].type)) {
		/* update the pairwise cipher */
		/* set cipher to invald value */
		if (dev_type == DEV_STA) {
			info->sta_cipher = ucast->list[0].type;
		} else {
			for (i = 0; i < info->p_count; i++) {
				if (IS_WPA_CIPHER(ucast->list[i].type)) {
					info->p_ciphers |= BIT(ucast->list[i].type);
				} else {
					info->parse_status = BCME_BAD_IE_DATA;
					goto done;
				}
			}
		}
	} else {
		info->parse_status = BCME_BAD_IE_DATA;
		goto done;
	}

	/* for rsn AKM authentication */
	ptr_inc += info->p_count * WPA_SUITE_LEN + sizeof(ucast->count);
	remain_len -= (info->p_count * WPA_SUITE_LEN + sizeof(ucast->count));

	mgmt = (const wpa_suite_auth_key_mgmt_t *)ptr_inc;

	if (remain_len < sizeof(mgmt->count)) {
		info->parse_status = BCME_BADLEN;
		goto done;
	}

	info->akm_count = (uint8)ltoh16_ua(&mgmt->count);

	if (!info->akm_count) {
		info->parse_status = BCME_BADARG;
		goto done;
	}

	if (dev_type == DEV_STA && info->akm_count != 1) {
		info->parse_status = BCME_BADARG;
		goto done;
	}

	if ((remain_len) < (info->akm_count * WPA_SUITE_LEN + sizeof(mgmt->count))) {
		info->parse_status = BCME_BADLEN;
		goto done;
	}

	if (dev_type == DEV_STA) {
		info->sta_akm = mgmt->list[0].type;
	}
	for (i = 0; i < info->akm_count; i++) {
		if (bcmwpa_is_valid_akm(mgmt->list[i].type) == BCME_OK) {
			ASSERT((mgmt->list[i].type) <
				(sizeof(info->akms) * NBBY));
			info->akms |= BIT(mgmt->list[i].type);
		}
	}

	/* RSN AKM/cipher suite related EAPOL key length update */
	info->pmk_len = bcmwpa_eapol_key_length(EAPOL_KEY_PMK, info->sta_akm, 0);
	info->kck_mic_len = bcmwpa_eapol_key_length(EAPOL_KEY_KCK_MIC, info->sta_akm, 0);
	info->kck_len = bcmwpa_eapol_key_length(EAPOL_KEY_KCK, info->sta_akm, 0);
	info->kek_len = bcmwpa_eapol_key_length(EAPOL_KEY_KEK, info->sta_akm, 0);
	info->tk_len = bcmwpa_eapol_key_length(EAPOL_KEY_TK, 0, info->sta_cipher);
	info->ptk_len = info->kck_mic_len + info->kek_len + info->tk_len;
#if defined(WL_FILS) && defined(WLFBT)
	info->kck2_len = bcmwpa_eapol_key_length(EAPOL_KEY_KCK2, info->sta_akm, 0);
	info->kek2_len = bcmwpa_eapol_key_length(EAPOL_KEY_KEK2, info->sta_akm, 0);
#endif /* WL_FILS && WLFBT */

	/* for rsn capabilities */
	ptr_inc += info->akm_count * WPA_SUITE_LEN + sizeof(mgmt->count);
	remain_len -=  info->akm_count * WPA_SUITE_LEN + sizeof(mgmt->count);

	/* as a STA,  at this point, we can compute the key descriptor version */
	if (dev_type == DEV_STA) {
		info->key_desc = wlc_calc_rsn_desc_version(info);
	}

	if (!(remain_len)) {
		goto done;
	}
	if (remain_len < RSN_CAP_LEN) {
		info->parse_status = BCME_BADLEN;
		goto done;
	}

	if (ie->id == DOT11_MNG_RSN_ID) {
		info->caps = ltoh16_ua(ptr_inc);
	}

	/* for WFA If MFP required, check that we are using a SHA256 AKM
	 * or higher  and nothing else.
	 * In case MFP Required and MFP Capable do not enforce check of AKM.
	 */
	if ((info->caps & RSN_CAP_MFPR) && !(info->akms & (1u << RSN_AKM_PSK))) {
		if ((info->akms & (AKM_SHA256_MASK | AKM_SHA384_MASK)) == 0 ||
			(info->akms & ~(AKM_SHA256_MASK | AKM_SHA384_MASK))) {
			info->parse_status = BCME_EPERM;
			goto done;
		}
	}

	/* check if AKMs require MFP capable to be set */
	if ((info->akms & RSN_MFPC_AKM_MASK) && !(info->caps & RSN_CAP_MFPC)) {
		info->parse_status = BCME_EPERM;
		goto done;
	}

	/* for rsn PMKID */
	ptr_inc += RSN_CAP_LEN;
	remain_len -= RSN_CAP_LEN;

	if (!(remain_len)) {
		goto done;
	}

	pmkid_list = (const wpa_pmkid_list_t*)ptr_inc;

	if ((remain_len) < sizeof(pmkid_list->count)) {
		info->parse_status = BCME_BADLEN;
		goto done;
	}

	info->pmkid_count = (uint8)ltoh16_ua(&pmkid_list->count);
	ptr_inc += sizeof(pmkid_list->count);
	remain_len -= sizeof(pmkid_list->count);

	if (info->pmkid_count) {
		if (remain_len < (uint32)(info->pmkid_count * WPA2_PMKID_LEN)) {
			info->parse_status = BCME_BADLEN;
			goto done;
		}
		info->pmkids_offset = ie->len + TLV_HDR_LEN - remain_len;
		/* for rsn group management cipher suite */
		ptr_inc += info->pmkid_count * WPA2_PMKID_LEN;
		remain_len -= info->pmkid_count * WPA2_PMKID_LEN;
	}

	if (!(remain_len)) {
		goto done;
	}
	/*
	 * from WPA2_Security_Improvements_Test_Plan_v1.0
	 * 4.2.4 APUT RSNE bounds verification using WPA2-PSK
	 * May content RSNE extensibile element ay this point
	 */
	if (remain_len < sizeof(wpa_suite_mcast_t)) {
		info->parse_status = BCME_BADLEN;
		goto done;
	}

	mcast = (const wpa_suite_mcast_t *)ptr_inc;
	if (IS_VALID_BIP_CIPHER((rsn_cipher_t)mcast->type)) {
		info->g_mgmt_cipher = (rsn_cipher_t)mcast->type;
	}

done:
	return info->parse_status;
}

/* Determine if the IE is of WPA or RSN type. Decode
 * up to version field. Modify the remaining len parameter to
 * indicate where the next field is.
 * Store and return error status.
 */

int
bcmwpa_decode_ie_type(const bcm_tlv_t *ie, rsn_ie_info_t *info, uint32 *remaining)
{
	const uint8 * ptr_inc = (const uint8 *)ie->data;
	uint32 remain_len = ie->len;
	uint8 version, version_len;

	if (ie->id == DOT11_MNG_WPA_ID) {
		/* min len check */
		if (remain_len < WPA_IE_FIXED_LEN) {
			info->parse_status = BCME_BADLEN;
			goto done;
		}
		/* WPA IE */
		if (memcmp(WPA_OUI, ie->data, WPA_OUI_LEN)) {
			/* bad OUI */
			info->parse_status = BCME_BADARG;
			goto done;
		}
		ptr_inc += WPA_OUI_LEN;
		if (*ptr_inc != WPA_OUI_TYPE) {
			/* wrong type */
			info->parse_status = BCME_BADARG;
			goto done;
		}
		ptr_inc ++;
		remain_len -= WPA_OUI_LEN + 1u;
		info->auth_ie_type |= WPA_AUTH_IE;
		version_len = WPA_VERSION_LEN;
	}
	else if  (ie->id == DOT11_MNG_RSN_ID) {
		if (remain_len < WPA2_VERSION_LEN) {
			info->parse_status = BCME_BADLEN;
			goto done;
		}
		/* RSN IE */
		info->auth_ie_type |= RSN_AUTH_IE;
		version_len = WPA2_VERSION_LEN;
	} else {
		/* TODO : add support for CCX, WAPI ? */
		info->parse_status = BCME_UNSUPPORTED;
		goto done;
	}

	/* mask down to uint8 for Windows build */
	version = 0xff & ltoh16_ua(ptr_inc);
	if (version > MAX_RSNE_SUPPORTED_VERSION) {
		info->parse_status = BCME_UNSUPPORTED;
		goto done;
	}

	info->version = (uint8)version;
	*remaining = remain_len - version_len;
done:
	return info->parse_status;
}

#endif /* RSN_IE_INFO_STRUCT_RELOCATED */

/* return the key descriptor version based on the AKM suite
 * applicable only for STA with RSN
 */
static uint16
wlc_calc_rsn_desc_version(const rsn_ie_info_t *rsn_info)
{
	uint16 key_desc_ver = WPA_KEY_DESC_V0;
	uint8 akm;

	ASSERT(rsn_info != NULL);
	ASSERT(rsn_info->dev_type == DEV_STA);
	akm = rsn_info->sta_akm;

	/* Refer Draft 802.11REVmd_D1.0.pdf  Section 12.7.2 */
	if ((akm == RSN_AKM_UNSPECIFIED) ||
		(akm == RSN_AKM_PSK)) {
		if ((rsn_info->sta_cipher == WPA_CIPHER_TKIP) ||
			(rsn_info->sta_cipher == WPA_CIPHER_NONE)) {
			key_desc_ver = WPA_KEY_DESC_V1;
		} else if ((rsn_info->sta_cipher != WPA_CIPHER_TKIP) ||
			(rsn_info->g_cipher != WPA_CIPHER_TKIP)) {
			key_desc_ver = WPA_KEY_DESC_V2;
		}
	} else if ((akm == RSN_AKM_FBT_1X) ||
		(akm == RSN_AKM_FBT_PSK) ||
		(akm == RSN_AKM_SHA256_1X) ||
		(akm == RSN_AKM_SHA256_PSK)) {
			key_desc_ver = WPA_KEY_DESC_V3;
	}
	return key_desc_ver;
}

/* get EAPOL key length based on RSN IE AKM/Cipher(unicast) suite
 * key: EAPOL key type
 * akm: RSN AKM suite selector
 * cipher: RSN unicast cipher suite selector
 * return: key length found in matching key_length_entry table
 */
uint8
bcmwpa_eapol_key_length(eapol_key_type_t key, rsn_akm_t akm, rsn_cipher_t cipher)
{
	uint i;
	uint8 key_length = 0;
	uint8 suite;
	const key_length_entry_t *key_entry = NULL;

	if (key == EAPOL_KEY_TK) {
		suite = cipher;
	} else {
		suite = akm;
	}
	for (i = 0; i < ARRAYSIZE(eapol_key_lookup_tbl); i++) {
		if (eapol_key_lookup_tbl[i].key == key) {
			key_entry = eapol_key_lookup_tbl[i].key_entry;
			break;
		}
	}

	if (key_entry) {
		i = 0;
		do {
			if (key_entry[i].suite == suite || key_entry[i].suite == 0) {
				key_length = key_entry[i].len;
				break;
			}
			i++;
		} while (i > 0);
	}

	return key_length;
}

/* check if RSM AKM suite is valid */
static int bcmwpa_is_valid_akm(const rsn_akm_t akm)
{
	uint i = 0;
	for (i = 0; i < ARRAYSIZE(rsn_akm_lookup_tbl); i++) {
		if (akm == rsn_akm_lookup_tbl[i].rsn_akm) {
			return BCME_OK;
		}
	}
	return BCME_ERROR;
}

/* checking cipher suite selector restriction based on AKM */
int
bcmwpa_rsn_akm_cipher_match(rsn_ie_info_t *rsn_info)
{
	uint i;
	const rsn_akm_cipher_match_entry_t *p_entry = NULL;

	for (i = 0; i < ARRAYSIZE(rsn_akm_cipher_match_table); i++) {
		/* akm match */
		if (rsn_info->sta_akm == rsn_akm_cipher_match_table[i].akm_type) {
			p_entry = &rsn_akm_cipher_match_table[i];
			break;
		}
	}

	if (p_entry) {
		/* unicast cipher match */
		if (!(rsn_info->p_ciphers & p_entry->u_cast)) {
			return BCME_UNSUPPORTED;
		}
		/* multicast cipher match */
		if (!(BCM_BIT(rsn_info->g_cipher) & p_entry->m_cast)) {
			return BCME_UNSUPPORTED;
		}
		/* group management cipher match */
		if (!(BCM_BIT(rsn_info->g_mgmt_cipher) & p_entry->g_mgmt)) {
			return BCME_UNSUPPORTED;
		}
	}
	return BCME_OK;
}

#if defined(BCMSUP_PSK) || defined(BCMSUPPL)
uint8 bcmwpa_find_group_mgmt_algo(rsn_cipher_t g_mgmt_cipher)
{
	uint8 i;
	uint8 algo = CRYPTO_ALGO_BIP;

	for (i = 0; i < ARRAYSIZE(group_mgmt_cipher_algo); i++) {
		if ((group_mgmt_cipher_algo[i].g_mgmt_cipher == g_mgmt_cipher)) {
			algo = group_mgmt_cipher_algo[i].bip_algo;
			break;
		}
	}

	return algo;
}
#endif /* defined(BCMSUP_PSK) || defined(BCMSUPPL) */

#if defined(WL_BAND6G)
bool
bcmwpa_is_invalid_6g_akm(const rsn_akm_mask_t akms_bmp)
{
	if (akms_bmp & rsn_akm_6g_inval_mask) {
		return TRUE;
	}
	return FALSE;
}

bool
bcmwpa_is_invalid_6g_cipher(const rsn_ciphers_t ciphers_bmp)
{
	if (ciphers_bmp & cipher_6g_inval_mask) {
		return TRUE;
	}
	return FALSE;
}
#endif /* WL_BAND6G */

/*
 * bcmwpa_get_algo_key_len returns the key_length for the algorithm.
 * API : bcm_get_algorithm key length
 * input: algo: Get the crypto algorithm.
 *        km: Keymgmt information.
 * output: returns the key length and error status.
 *         BCME_OK is valid else BCME_UNSUPPORTED if not supported
 */
int
bcmwpa_get_algo_key_len(uint8 algo, uint16 *key_len)
{
	int err = BCME_OK;

	if (key_len == NULL) {
		return BCME_BADARG;
	}

	switch (algo) {
		case CRYPTO_ALGO_WEP1:
			*key_len = WEP1_KEY_SIZE;
			break;

		case CRYPTO_ALGO_TKIP:
			*key_len = TKIP_KEY_SIZE;
			break;

		case CRYPTO_ALGO_WEP128:
			*key_len = WEP128_KEY_SIZE;
			break;

		case CRYPTO_ALGO_AES_CCM:       /* fall through */
		case CRYPTO_ALGO_AES_GCM:       /* fall through */
		case CRYPTO_ALGO_AES_OCB_MSDU : /* fall through */
		case CRYPTO_ALGO_AES_OCB_MPDU:
			*key_len = AES_KEY_SIZE;
			break;

#ifdef BCMWAPI_WPI
		/* TODO: Need to double check */
		case CRYPTO_ALGO_SMS4:
			*key_len = SMS4_KEY_LEN + SMS4_WPI_CBC_MAC_LEN;
			break;
#endif /* BCMWAPI_WPI */

		case CRYPTO_ALGO_BIP: /* fall through */
		case CRYPTO_ALGO_BIP_GMAC:
			*key_len = BIP_KEY_SIZE;
			break;

		case CRYPTO_ALGO_AES_CCM256:  /* fall through */
		case CRYPTO_ALGO_AES_GCM256:  /* fall through */
		case CRYPTO_ALGO_BIP_CMAC256: /* fall through */
		case CRYPTO_ALGO_BIP_GMAC256:
			*key_len = AES256_KEY_SIZE;
			break;

		case CRYPTO_ALGO_OFF:
			*key_len = 0;
			break;

#if !defined(BCMCCX) && !defined(BCMEXTCCX)
		case CRYPTO_ALGO_NALG:     /* fall through */
#else
		case CRYPTO_ALGO_CKIP:     /* fall through */
		case CRYPTO_ALGO_CKIP_MMH: /* fall through */
		case CRYPTO_ALGO_WEP_MMH:  /* fall through */
		case CRYPTO_ALGO_PMK:      /* fall through default */
#endif /* !defined(BCMCCX) && !defined(BCMEXTCCX) */
		default:
			*key_len = 0;
			err = BCME_UNSUPPORTED;
			break;
	}
	return err;
}
