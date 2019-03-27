/*
 * DPP functionality shared between hostapd and wpa_supplicant
 * Copyright (c) 2017, Qualcomm Atheros, Inc.
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "utils/includes.h"
#include <openssl/opensslv.h>
#include <openssl/err.h>
#include <openssl/asn1.h>
#include <openssl/asn1t.h>

#include "utils/common.h"
#include "utils/base64.h"
#include "utils/json.h"
#include "common/ieee802_11_common.h"
#include "common/ieee802_11_defs.h"
#include "common/wpa_ctrl.h"
#include "crypto/crypto.h"
#include "crypto/random.h"
#include "crypto/aes.h"
#include "crypto/aes_siv.h"
#include "crypto/sha384.h"
#include "crypto/sha512.h"
#include "drivers/driver.h"
#include "dpp.h"


#ifdef CONFIG_TESTING_OPTIONS
enum dpp_test_behavior dpp_test = DPP_TEST_DISABLED;
u8 dpp_pkex_own_mac_override[ETH_ALEN] = { 0, 0, 0, 0, 0, 0 };
u8 dpp_pkex_peer_mac_override[ETH_ALEN] = { 0, 0, 0, 0, 0, 0 };
u8 dpp_pkex_ephemeral_key_override[600];
size_t dpp_pkex_ephemeral_key_override_len = 0;
u8 dpp_protocol_key_override[600];
size_t dpp_protocol_key_override_len = 0;
u8 dpp_nonce_override[DPP_MAX_NONCE_LEN];
size_t dpp_nonce_override_len = 0;

static int dpp_test_gen_invalid_key(struct wpabuf *msg,
				    const struct dpp_curve_params *curve);
#endif /* CONFIG_TESTING_OPTIONS */

#if OPENSSL_VERSION_NUMBER < 0x10100000L || \
	(defined(LIBRESSL_VERSION_NUMBER) && \
	 LIBRESSL_VERSION_NUMBER < 0x20700000L)
/* Compatibility wrappers for older versions. */

static int ECDSA_SIG_set0(ECDSA_SIG *sig, BIGNUM *r, BIGNUM *s)
{
	sig->r = r;
	sig->s = s;
	return 1;
}


static void ECDSA_SIG_get0(const ECDSA_SIG *sig, const BIGNUM **pr,
			   const BIGNUM **ps)
{
	if (pr)
		*pr = sig->r;
	if (ps)
		*ps = sig->s;
}

#endif


static const struct dpp_curve_params dpp_curves[] = {
	/* The mandatory to support and the default NIST P-256 curve needs to
	 * be the first entry on this list. */
	{ "prime256v1", 32, 32, 16, 32, "P-256", 19, "ES256" },
	{ "secp384r1", 48, 48, 24, 48, "P-384", 20, "ES384" },
	{ "secp521r1", 64, 64, 32, 66, "P-521", 21, "ES512" },
	{ "brainpoolP256r1", 32, 32, 16, 32, "BP-256", 28, "BS256" },
	{ "brainpoolP384r1", 48, 48, 24, 48, "BP-384", 29, "BS384" },
	{ "brainpoolP512r1", 64, 64, 32, 64, "BP-512", 30, "BS512" },
	{ NULL, 0, 0, 0, 0, NULL, 0, NULL }
};


/* Role-specific elements for PKEX */

/* NIST P-256 */
static const u8 pkex_init_x_p256[32] = {
	0x56, 0x26, 0x12, 0xcf, 0x36, 0x48, 0xfe, 0x0b,
	0x07, 0x04, 0xbb, 0x12, 0x22, 0x50, 0xb2, 0x54,
	0xb1, 0x94, 0x64, 0x7e, 0x54, 0xce, 0x08, 0x07,
	0x2e, 0xec, 0xca, 0x74, 0x5b, 0x61, 0x2d, 0x25
 };
static const u8 pkex_init_y_p256[32] = {
	0x3e, 0x44, 0xc7, 0xc9, 0x8c, 0x1c, 0xa1, 0x0b,
	0x20, 0x09, 0x93, 0xb2, 0xfd, 0xe5, 0x69, 0xdc,
	0x75, 0xbc, 0xad, 0x33, 0xc1, 0xe7, 0xc6, 0x45,
	0x4d, 0x10, 0x1e, 0x6a, 0x3d, 0x84, 0x3c, 0xa4
 };
static const u8 pkex_resp_x_p256[32] = {
	0x1e, 0xa4, 0x8a, 0xb1, 0xa4, 0xe8, 0x42, 0x39,
	0xad, 0x73, 0x07, 0xf2, 0x34, 0xdf, 0x57, 0x4f,
	0xc0, 0x9d, 0x54, 0xbe, 0x36, 0x1b, 0x31, 0x0f,
	0x59, 0x91, 0x52, 0x33, 0xac, 0x19, 0x9d, 0x76
};
static const u8 pkex_resp_y_p256[32] = {
	0xd9, 0xfb, 0xf6, 0xb9, 0xf5, 0xfa, 0xdf, 0x19,
	0x58, 0xd8, 0x3e, 0xc9, 0x89, 0x7a, 0x35, 0xc1,
	0xbd, 0xe9, 0x0b, 0x77, 0x7a, 0xcb, 0x91, 0x2a,
	0xe8, 0x21, 0x3f, 0x47, 0x52, 0x02, 0x4d, 0x67
};

/* NIST P-384 */
static const u8 pkex_init_x_p384[48] = {
	0x95, 0x3f, 0x42, 0x9e, 0x50, 0x7f, 0xf9, 0xaa,
	0xac, 0x1a, 0xf2, 0x85, 0x2e, 0x64, 0x91, 0x68,
	0x64, 0xc4, 0x3c, 0xb7, 0x5c, 0xf8, 0xc9, 0x53,
	0x6e, 0x58, 0x4c, 0x7f, 0xc4, 0x64, 0x61, 0xac,
	0x51, 0x8a, 0x6f, 0xfe, 0xab, 0x74, 0xe6, 0x12,
	0x81, 0xac, 0x38, 0x5d, 0x41, 0xe6, 0xb9, 0xa3
};
static const u8 pkex_init_y_p384[48] = {
	0x76, 0x2f, 0x68, 0x84, 0xa6, 0xb0, 0x59, 0x29,
	0x83, 0xa2, 0x6c, 0xa4, 0x6c, 0x3b, 0xf8, 0x56,
	0x76, 0x11, 0x2a, 0x32, 0x90, 0xbd, 0x07, 0xc7,
	0x37, 0x39, 0x9d, 0xdb, 0x96, 0xf3, 0x2b, 0xb6,
	0x27, 0xbb, 0x29, 0x3c, 0x17, 0x33, 0x9d, 0x94,
	0xc3, 0xda, 0xac, 0x46, 0xb0, 0x8e, 0x07, 0x18
};
static const u8 pkex_resp_x_p384[48] = {
	0xad, 0xbe, 0xd7, 0x1d, 0x3a, 0x71, 0x64, 0x98,
	0x5f, 0xb4, 0xd6, 0x4b, 0x50, 0xd0, 0x84, 0x97,
	0x4b, 0x7e, 0x57, 0x70, 0xd2, 0xd9, 0xf4, 0x92,
	0x2a, 0x3f, 0xce, 0x99, 0xc5, 0x77, 0x33, 0x44,
	0x14, 0x56, 0x92, 0xcb, 0xae, 0x46, 0x64, 0xdf,
	0xe0, 0xbb, 0xd7, 0xb1, 0x29, 0x20, 0x72, 0xdf
};
static const u8 pkex_resp_y_p384[48] = {
	0xab, 0xa7, 0xdf, 0x52, 0xaa, 0xe2, 0x35, 0x0c,
	0xe3, 0x75, 0x32, 0xe6, 0xbf, 0x06, 0xc8, 0x7c,
	0x38, 0x29, 0x4c, 0xec, 0x82, 0xac, 0xd7, 0xa3,
	0x09, 0xd2, 0x0e, 0x22, 0x5a, 0x74, 0x52, 0xa1,
	0x7e, 0x54, 0x4e, 0xfe, 0xc6, 0x29, 0x33, 0x63,
	0x15, 0xe1, 0x7b, 0xe3, 0x40, 0x1c, 0xca, 0x06
};

/* NIST P-521 */
static const u8 pkex_init_x_p521[66] = {
	0x00, 0x16, 0x20, 0x45, 0x19, 0x50, 0x95, 0x23,
	0x0d, 0x24, 0xbe, 0x00, 0x87, 0xdc, 0xfa, 0xf0,
	0x58, 0x9a, 0x01, 0x60, 0x07, 0x7a, 0xca, 0x76,
	0x01, 0xab, 0x2d, 0x5a, 0x46, 0xcd, 0x2c, 0xb5,
	0x11, 0x9a, 0xff, 0xaa, 0x48, 0x04, 0x91, 0x38,
	0xcf, 0x86, 0xfc, 0xa4, 0xa5, 0x0f, 0x47, 0x01,
	0x80, 0x1b, 0x30, 0xa3, 0xae, 0xe8, 0x1c, 0x2e,
	0xea, 0xcc, 0xf0, 0x03, 0x9f, 0x77, 0x4c, 0x8d,
	0x97, 0x76
};
static const u8 pkex_init_y_p521[66] = {
	0x00, 0xb3, 0x8e, 0x02, 0xe4, 0x2a, 0x63, 0x59,
	0x12, 0xc6, 0x10, 0xba, 0x3a, 0xf9, 0x02, 0x99,
	0x3f, 0x14, 0xf0, 0x40, 0xde, 0x5c, 0xc9, 0x8b,
	0x02, 0x55, 0xfa, 0x91, 0xb1, 0xcc, 0x6a, 0xbd,
	0xe5, 0x62, 0xc0, 0xc5, 0xe3, 0xa1, 0x57, 0x9f,
	0x08, 0x1a, 0xa6, 0xe2, 0xf8, 0x55, 0x90, 0xbf,
	0xf5, 0xa6, 0xc3, 0xd8, 0x52, 0x1f, 0xb7, 0x02,
	0x2e, 0x7c, 0xc8, 0xb3, 0x20, 0x1e, 0x79, 0x8d,
	0x03, 0xa8
};
static const u8 pkex_resp_x_p521[66] = {
	0x00, 0x79, 0xe4, 0x4d, 0x6b, 0x5e, 0x12, 0x0a,
	0x18, 0x2c, 0xb3, 0x05, 0x77, 0x0f, 0xc3, 0x44,
	0x1a, 0xcd, 0x78, 0x46, 0x14, 0xee, 0x46, 0x3f,
	0xab, 0xc9, 0x59, 0x7c, 0x85, 0xa0, 0xc2, 0xfb,
	0x02, 0x32, 0x99, 0xde, 0x5d, 0xe1, 0x0d, 0x48,
	0x2d, 0x71, 0x7d, 0x8d, 0x3f, 0x61, 0x67, 0x9e,
	0x2b, 0x8b, 0x12, 0xde, 0x10, 0x21, 0x55, 0x0a,
	0x5b, 0x2d, 0xe8, 0x05, 0x09, 0xf6, 0x20, 0x97,
	0x84, 0xb4
};
static const u8 pkex_resp_y_p521[66] = {
	0x00, 0x46, 0x63, 0x39, 0xbe, 0xcd, 0xa4, 0x2d,
	0xca, 0x27, 0x74, 0xd4, 0x1b, 0x91, 0x33, 0x20,
	0x83, 0xc7, 0x3b, 0xa4, 0x09, 0x8b, 0x8e, 0xa3,
	0x88, 0xe9, 0x75, 0x7f, 0x56, 0x7b, 0x38, 0x84,
	0x62, 0x02, 0x7c, 0x90, 0x51, 0x07, 0xdb, 0xe9,
	0xd0, 0xde, 0xda, 0x9a, 0x5d, 0xe5, 0x94, 0xd2,
	0xcf, 0x9d, 0x4c, 0x33, 0x91, 0xa6, 0xc3, 0x80,
	0xa7, 0x6e, 0x7e, 0x8d, 0xf8, 0x73, 0x6e, 0x53,
	0xce, 0xe1
};

/* Brainpool P-256r1 */
static const u8 pkex_init_x_bp_p256r1[32] = {
	0x46, 0x98, 0x18, 0x6c, 0x27, 0xcd, 0x4b, 0x10,
	0x7d, 0x55, 0xa3, 0xdd, 0x89, 0x1f, 0x9f, 0xca,
	0xc7, 0x42, 0x5b, 0x8a, 0x23, 0xed, 0xf8, 0x75,
	0xac, 0xc7, 0xe9, 0x8d, 0xc2, 0x6f, 0xec, 0xd8
};
static const u8 pkex_init_y_bp_p256r1[32] = {
	0x93, 0xca, 0xef, 0xa9, 0x66, 0x3e, 0x87, 0xcd,
	0x52, 0x6e, 0x54, 0x13, 0xef, 0x31, 0x67, 0x30,
	0x15, 0x13, 0x9d, 0x6d, 0xc0, 0x95, 0x32, 0xbe,
	0x4f, 0xab, 0x5d, 0xf7, 0xbf, 0x5e, 0xaa, 0x0b
};
static const u8 pkex_resp_x_bp_p256r1[32] = {
	0x90, 0x18, 0x84, 0xc9, 0xdc, 0xcc, 0xb5, 0x2f,
	0x4a, 0x3f, 0x4f, 0x18, 0x0a, 0x22, 0x56, 0x6a,
	0xa9, 0xef, 0xd4, 0xe6, 0xc3, 0x53, 0xc2, 0x1a,
	0x23, 0x54, 0xdd, 0x08, 0x7e, 0x10, 0xd8, 0xe3
};
static const u8 pkex_resp_y_bp_p256r1[32] = {
	0x2a, 0xfa, 0x98, 0x9b, 0xe3, 0xda, 0x30, 0xfd,
	0x32, 0x28, 0xcb, 0x66, 0xfb, 0x40, 0x7f, 0xf2,
	0xb2, 0x25, 0x80, 0x82, 0x44, 0x85, 0x13, 0x7e,
	0x4b, 0xb5, 0x06, 0xc0, 0x03, 0x69, 0x23, 0x64
};

/* Brainpool P-384r1 */
static const u8 pkex_init_x_bp_p384r1[48] = {
	0x0a, 0x2c, 0xeb, 0x49, 0x5e, 0xb7, 0x23, 0xbd,
	0x20, 0x5b, 0xe0, 0x49, 0xdf, 0xcf, 0xcf, 0x19,
	0x37, 0x36, 0xe1, 0x2f, 0x59, 0xdb, 0x07, 0x06,
	0xb5, 0xeb, 0x2d, 0xae, 0xc2, 0xb2, 0x38, 0x62,
	0xa6, 0x73, 0x09, 0xa0, 0x6c, 0x0a, 0xa2, 0x30,
	0x99, 0xeb, 0xf7, 0x1e, 0x47, 0xb9, 0x5e, 0xbe
};
static const u8 pkex_init_y_bp_p384r1[48] = {
	0x54, 0x76, 0x61, 0x65, 0x75, 0x5a, 0x2f, 0x99,
	0x39, 0x73, 0xca, 0x6c, 0xf9, 0xf7, 0x12, 0x86,
	0x54, 0xd5, 0xd4, 0xad, 0x45, 0x7b, 0xbf, 0x32,
	0xee, 0x62, 0x8b, 0x9f, 0x52, 0xe8, 0xa0, 0xc9,
	0xb7, 0x9d, 0xd1, 0x09, 0xb4, 0x79, 0x1c, 0x3e,
	0x1a, 0xbf, 0x21, 0x45, 0x66, 0x6b, 0x02, 0x52
};
static const u8 pkex_resp_x_bp_p384r1[48] = {
	0x03, 0xa2, 0x57, 0xef, 0xe8, 0x51, 0x21, 0xa0,
	0xc8, 0x9e, 0x21, 0x02, 0xb5, 0x9a, 0x36, 0x25,
	0x74, 0x22, 0xd1, 0xf2, 0x1b, 0xa8, 0x9a, 0x9b,
	0x97, 0xbc, 0x5a, 0xeb, 0x26, 0x15, 0x09, 0x71,
	0x77, 0x59, 0xec, 0x8b, 0xb7, 0xe1, 0xe8, 0xce,
	0x65, 0xb8, 0xaf, 0xf8, 0x80, 0xae, 0x74, 0x6c
};
static const u8 pkex_resp_y_bp_p384r1[48] = {
	0x2f, 0xd9, 0x6a, 0xc7, 0x3e, 0xec, 0x76, 0x65,
	0x2d, 0x38, 0x7f, 0xec, 0x63, 0x26, 0x3f, 0x04,
	0xd8, 0x4e, 0xff, 0xe1, 0x0a, 0x51, 0x74, 0x70,
	0xe5, 0x46, 0x63, 0x7f, 0x5c, 0xc0, 0xd1, 0x7c,
	0xfb, 0x2f, 0xea, 0xe2, 0xd8, 0x0f, 0x84, 0xcb,
	0xe9, 0x39, 0x5c, 0x64, 0xfe, 0xcb, 0x2f, 0xf1
};

/* Brainpool P-512r1 */
static const u8 pkex_init_x_bp_p512r1[64] = {
	0x4c, 0xe9, 0xb6, 0x1c, 0xe2, 0x00, 0x3c, 0x9c,
	0xa9, 0xc8, 0x56, 0x52, 0xaf, 0x87, 0x3e, 0x51,
	0x9c, 0xbb, 0x15, 0x31, 0x1e, 0xc1, 0x05, 0xfc,
	0x7c, 0x77, 0xd7, 0x37, 0x61, 0x27, 0xd0, 0x95,
	0x98, 0xee, 0x5d, 0xa4, 0x3d, 0x09, 0xdb, 0x3d,
	0xfa, 0x89, 0x9e, 0x7f, 0xa6, 0xa6, 0x9c, 0xff,
	0x83, 0x5c, 0x21, 0x6c, 0x3e, 0xf2, 0xfe, 0xdc,
	0x63, 0xe4, 0xd1, 0x0e, 0x75, 0x45, 0x69, 0x0f
};
static const u8 pkex_init_y_bp_p512r1[64] = {
	0x50, 0xb5, 0x9b, 0xfa, 0x45, 0x67, 0x75, 0x94,
	0x44, 0xe7, 0x68, 0xb0, 0xeb, 0x3e, 0xb3, 0xb8,
	0xf9, 0x99, 0x05, 0xef, 0xae, 0x6c, 0xbc, 0xe3,
	0xe1, 0xd2, 0x51, 0x54, 0xdf, 0x59, 0xd4, 0x45,
	0x41, 0x3a, 0xa8, 0x0b, 0x76, 0x32, 0x44, 0x0e,
	0x07, 0x60, 0x3a, 0x6e, 0xbe, 0xfe, 0xe0, 0x58,
	0x52, 0xa0, 0xaa, 0x8b, 0xd8, 0x5b, 0xf2, 0x71,
	0x11, 0x9a, 0x9e, 0x8f, 0x1a, 0xd1, 0xc9, 0x99
};
static const u8 pkex_resp_x_bp_p512r1[64] = {
	0x2a, 0x60, 0x32, 0x27, 0xa1, 0xe6, 0x94, 0x72,
	0x1c, 0x48, 0xbe, 0xc5, 0x77, 0x14, 0x30, 0x76,
	0xe4, 0xbf, 0xf7, 0x7b, 0xc5, 0xfd, 0xdf, 0x19,
	0x1e, 0x0f, 0xdf, 0x1c, 0x40, 0xfa, 0x34, 0x9e,
	0x1f, 0x42, 0x24, 0xa3, 0x2c, 0xd5, 0xc7, 0xc9,
	0x7b, 0x47, 0x78, 0x96, 0xf1, 0x37, 0x0e, 0x88,
	0xcb, 0xa6, 0x52, 0x29, 0xd7, 0xa8, 0x38, 0x29,
	0x8e, 0x6e, 0x23, 0x47, 0xd4, 0x4b, 0x70, 0x3e
};
static const u8 pkex_resp_y_bp_p512r1[64] = {
	0x80, 0x1f, 0x43, 0xd2, 0x17, 0x35, 0xec, 0x81,
	0xd9, 0x4b, 0xdc, 0x81, 0x19, 0xd9, 0x5f, 0x68,
	0x16, 0x84, 0xfe, 0x63, 0x4b, 0x8d, 0x5d, 0xaa,
	0x88, 0x4a, 0x47, 0x48, 0xd4, 0xea, 0xab, 0x7d,
	0x6a, 0xbf, 0xe1, 0x28, 0x99, 0x6a, 0x87, 0x1c,
	0x30, 0xb4, 0x44, 0x2d, 0x75, 0xac, 0x35, 0x09,
	0x73, 0x24, 0x3d, 0xb4, 0x43, 0xb1, 0xc1, 0x56,
	0x56, 0xad, 0x30, 0x87, 0xf4, 0xc3, 0x00, 0xc7
};


static void dpp_debug_print_point(const char *title, const EC_GROUP *group,
				  const EC_POINT *point)
{
	BIGNUM *x, *y;
	BN_CTX *ctx;
	char *x_str = NULL, *y_str = NULL;

	if (!wpa_debug_show_keys)
		return;

	ctx = BN_CTX_new();
	x = BN_new();
	y = BN_new();
	if (!ctx || !x || !y ||
	    EC_POINT_get_affine_coordinates_GFp(group, point, x, y, ctx) != 1)
		goto fail;

	x_str = BN_bn2hex(x);
	y_str = BN_bn2hex(y);
	if (!x_str || !y_str)
		goto fail;

	wpa_printf(MSG_DEBUG, "%s (%s,%s)", title, x_str, y_str);

fail:
	OPENSSL_free(x_str);
	OPENSSL_free(y_str);
	BN_free(x);
	BN_free(y);
	BN_CTX_free(ctx);
}


static int dpp_hash_vector(const struct dpp_curve_params *curve,
			   size_t num_elem, const u8 *addr[], const size_t *len,
			   u8 *mac)
{
	if (curve->hash_len == 32)
		return sha256_vector(num_elem, addr, len, mac);
	if (curve->hash_len == 48)
		return sha384_vector(num_elem, addr, len, mac);
	if (curve->hash_len == 64)
		return sha512_vector(num_elem, addr, len, mac);
	return -1;
}


static int dpp_hkdf_expand(size_t hash_len, const u8 *secret, size_t secret_len,
			   const char *label, u8 *out, size_t outlen)
{
	if (hash_len == 32)
		return hmac_sha256_kdf(secret, secret_len, NULL,
				       (const u8 *) label, os_strlen(label),
				       out, outlen);
	if (hash_len == 48)
		return hmac_sha384_kdf(secret, secret_len, NULL,
				       (const u8 *) label, os_strlen(label),
				       out, outlen);
	if (hash_len == 64)
		return hmac_sha512_kdf(secret, secret_len, NULL,
				       (const u8 *) label, os_strlen(label),
				       out, outlen);
	return -1;
}


static int dpp_hmac_vector(size_t hash_len, const u8 *key, size_t key_len,
			   size_t num_elem, const u8 *addr[],
			   const size_t *len, u8 *mac)
{
	if (hash_len == 32)
		return hmac_sha256_vector(key, key_len, num_elem, addr, len,
					  mac);
	if (hash_len == 48)
		return hmac_sha384_vector(key, key_len, num_elem, addr, len,
					  mac);
	if (hash_len == 64)
		return hmac_sha512_vector(key, key_len, num_elem, addr, len,
					  mac);
	return -1;
}


static int dpp_hmac(size_t hash_len, const u8 *key, size_t key_len,
		    const u8 *data, size_t data_len, u8 *mac)
{
	if (hash_len == 32)
		return hmac_sha256(key, key_len, data, data_len, mac);
	if (hash_len == 48)
		return hmac_sha384(key, key_len, data, data_len, mac);
	if (hash_len == 64)
		return hmac_sha512(key, key_len, data, data_len, mac);
	return -1;
}


static int dpp_bn2bin_pad(const BIGNUM *bn, u8 *pos, size_t len)
{
	int num_bytes, offset;

	num_bytes = BN_num_bytes(bn);
	if ((size_t) num_bytes > len)
		return -1;
	offset = len - num_bytes;
	os_memset(pos, 0, offset);
	BN_bn2bin(bn, pos + offset);
	return 0;
}


static struct wpabuf * dpp_get_pubkey_point(EVP_PKEY *pkey, int prefix)
{
	int len, res;
	EC_KEY *eckey;
	struct wpabuf *buf;
	unsigned char *pos;

	eckey = EVP_PKEY_get1_EC_KEY(pkey);
	if (!eckey)
		return NULL;
	EC_KEY_set_conv_form(eckey, POINT_CONVERSION_UNCOMPRESSED);
	len = i2o_ECPublicKey(eckey, NULL);
	if (len <= 0) {
		wpa_printf(MSG_ERROR,
			   "DDP: Failed to determine public key encoding length");
		EC_KEY_free(eckey);
		return NULL;
	}

	buf = wpabuf_alloc(len);
	if (!buf) {
		EC_KEY_free(eckey);
		return NULL;
	}

	pos = wpabuf_put(buf, len);
	res = i2o_ECPublicKey(eckey, &pos);
	EC_KEY_free(eckey);
	if (res != len) {
		wpa_printf(MSG_ERROR,
			   "DDP: Failed to encode public key (res=%d/%d)",
			   res, len);
		wpabuf_free(buf);
		return NULL;
	}

	if (!prefix) {
		/* Remove 0x04 prefix to match DPP definition */
		pos = wpabuf_mhead(buf);
		os_memmove(pos, pos + 1, len - 1);
		buf->used--;
	}

	return buf;
}


static EVP_PKEY * dpp_set_pubkey_point_group(const EC_GROUP *group,
					     const u8 *buf_x, const u8 *buf_y,
					     size_t len)
{
	EC_KEY *eckey = NULL;
	BN_CTX *ctx;
	EC_POINT *point = NULL;
	BIGNUM *x = NULL, *y = NULL;
	EVP_PKEY *pkey = NULL;

	ctx = BN_CTX_new();
	if (!ctx) {
		wpa_printf(MSG_ERROR, "DPP: Out of memory");
		return NULL;
	}

	point = EC_POINT_new(group);
	x = BN_bin2bn(buf_x, len, NULL);
	y = BN_bin2bn(buf_y, len, NULL);
	if (!point || !x || !y) {
		wpa_printf(MSG_ERROR, "DPP: Out of memory");
		goto fail;
	}

	if (!EC_POINT_set_affine_coordinates_GFp(group, point, x, y, ctx)) {
		wpa_printf(MSG_ERROR,
			   "DPP: OpenSSL: EC_POINT_set_affine_coordinates_GFp failed: %s",
			   ERR_error_string(ERR_get_error(), NULL));
		goto fail;
	}

	if (!EC_POINT_is_on_curve(group, point, ctx) ||
	    EC_POINT_is_at_infinity(group, point)) {
		wpa_printf(MSG_ERROR, "DPP: Invalid point");
		goto fail;
	}
	dpp_debug_print_point("DPP: dpp_set_pubkey_point_group", group, point);

	eckey = EC_KEY_new();
	if (!eckey ||
	    EC_KEY_set_group(eckey, group) != 1 ||
	    EC_KEY_set_public_key(eckey, point) != 1) {
		wpa_printf(MSG_ERROR,
			   "DPP: Failed to set EC_KEY: %s",
			   ERR_error_string(ERR_get_error(), NULL));
		goto fail;
	}
	EC_KEY_set_asn1_flag(eckey, OPENSSL_EC_NAMED_CURVE);

	pkey = EVP_PKEY_new();
	if (!pkey || EVP_PKEY_set1_EC_KEY(pkey, eckey) != 1) {
		wpa_printf(MSG_ERROR, "DPP: Could not create EVP_PKEY");
		goto fail;
	}

out:
	BN_free(x);
	BN_free(y);
	EC_KEY_free(eckey);
	EC_POINT_free(point);
	BN_CTX_free(ctx);
	return pkey;
fail:
	EVP_PKEY_free(pkey);
	pkey = NULL;
	goto out;
}


static EVP_PKEY * dpp_set_pubkey_point(EVP_PKEY *group_key,
				       const u8 *buf, size_t len)
{
	EC_KEY *eckey;
	const EC_GROUP *group;
	EVP_PKEY *pkey = NULL;

	if (len & 1)
		return NULL;

	eckey = EVP_PKEY_get1_EC_KEY(group_key);
	if (!eckey) {
		wpa_printf(MSG_ERROR,
			   "DPP: Could not get EC_KEY from group_key");
		return NULL;
	}

	group = EC_KEY_get0_group(eckey);
	if (group)
		pkey = dpp_set_pubkey_point_group(group, buf, buf + len / 2,
						  len / 2);
	else
		wpa_printf(MSG_ERROR, "DPP: Could not get EC group");

	EC_KEY_free(eckey);
	return pkey;
}


static void dpp_auth_fail(struct dpp_authentication *auth, const char *txt)
{
	wpa_msg(auth->msg_ctx, MSG_INFO, DPP_EVENT_FAIL "%s", txt);
}


struct wpabuf * dpp_alloc_msg(enum dpp_public_action_frame_type type,
			      size_t len)
{
	struct wpabuf *msg;

	msg = wpabuf_alloc(8 + len);
	if (!msg)
		return NULL;
	wpabuf_put_u8(msg, WLAN_ACTION_PUBLIC);
	wpabuf_put_u8(msg, WLAN_PA_VENDOR_SPECIFIC);
	wpabuf_put_be24(msg, OUI_WFA);
	wpabuf_put_u8(msg, DPP_OUI_TYPE);
	wpabuf_put_u8(msg, 1); /* Crypto Suite */
	wpabuf_put_u8(msg, type);
	return msg;
}


const u8 * dpp_get_attr(const u8 *buf, size_t len, u16 req_id, u16 *ret_len)
{
	u16 id, alen;
	const u8 *pos = buf, *end = buf + len;

	while (end - pos >= 4) {
		id = WPA_GET_LE16(pos);
		pos += 2;
		alen = WPA_GET_LE16(pos);
		pos += 2;
		if (alen > end - pos)
			return NULL;
		if (id == req_id) {
			*ret_len = alen;
			return pos;
		}
		pos += alen;
	}

	return NULL;
}


int dpp_check_attrs(const u8 *buf, size_t len)
{
	const u8 *pos, *end;
	int wrapped_data = 0;

	pos = buf;
	end = buf + len;
	while (end - pos >= 4) {
		u16 id, alen;

		id = WPA_GET_LE16(pos);
		pos += 2;
		alen = WPA_GET_LE16(pos);
		pos += 2;
		wpa_printf(MSG_MSGDUMP, "DPP: Attribute ID %04x len %u",
			   id, alen);
		if (alen > end - pos) {
			wpa_printf(MSG_DEBUG,
				   "DPP: Truncated message - not enough room for the attribute - dropped");
			return -1;
		}
		if (wrapped_data) {
			wpa_printf(MSG_DEBUG,
				   "DPP: An unexpected attribute included after the Wrapped Data attribute");
			return -1;
		}
		if (id == DPP_ATTR_WRAPPED_DATA)
			wrapped_data = 1;
		pos += alen;
	}

	if (end != pos) {
		wpa_printf(MSG_DEBUG,
			   "DPP: Unexpected octets (%d) after the last attribute",
			   (int) (end - pos));
		return -1;
	}

	return 0;
}


void dpp_bootstrap_info_free(struct dpp_bootstrap_info *info)
{
	if (!info)
		return;
	os_free(info->uri);
	os_free(info->info);
	EVP_PKEY_free(info->pubkey);
	os_free(info);
}


const char * dpp_bootstrap_type_txt(enum dpp_bootstrap_type type)
{
	switch (type) {
	case DPP_BOOTSTRAP_QR_CODE:
		return "QRCODE";
	case DPP_BOOTSTRAP_PKEX:
		return "PKEX";
	}
	return "??";
}


static int dpp_uri_valid_info(const char *info)
{
	while (*info) {
		unsigned char val = *info++;

		if (val < 0x20 || val > 0x7e || val == 0x3b)
			return 0;
	}

	return 1;
}


static int dpp_clone_uri(struct dpp_bootstrap_info *bi, const char *uri)
{
	bi->uri = os_strdup(uri);
	return bi->uri ? 0 : -1;
}


int dpp_parse_uri_chan_list(struct dpp_bootstrap_info *bi,
			    const char *chan_list)
{
	const char *pos = chan_list;
	int opclass, channel, freq;

	while (pos && *pos && *pos != ';') {
		opclass = atoi(pos);
		if (opclass <= 0)
			goto fail;
		pos = os_strchr(pos, '/');
		if (!pos)
			goto fail;
		pos++;
		channel = atoi(pos);
		if (channel <= 0)
			goto fail;
		while (*pos >= '0' && *pos <= '9')
			pos++;
		freq = ieee80211_chan_to_freq(NULL, opclass, channel);
		wpa_printf(MSG_DEBUG,
			   "DPP: URI channel-list: opclass=%d channel=%d ==> freq=%d",
			   opclass, channel, freq);
		if (freq < 0) {
			wpa_printf(MSG_DEBUG,
				   "DPP: Ignore unknown URI channel-list channel (opclass=%d channel=%d)",
				   opclass, channel);
		} else if (bi->num_freq == DPP_BOOTSTRAP_MAX_FREQ) {
			wpa_printf(MSG_DEBUG,
				   "DPP: Too many channels in URI channel-list - ignore list");
			bi->num_freq = 0;
			break;
		} else {
			bi->freq[bi->num_freq++] = freq;
		}

		if (*pos == ';' || *pos == '\0')
			break;
		if (*pos != ',')
			goto fail;
		pos++;
	}

	return 0;
fail:
	wpa_printf(MSG_DEBUG, "DPP: Invalid URI channel-list");
	return -1;
}


int dpp_parse_uri_mac(struct dpp_bootstrap_info *bi, const char *mac)
{
	if (!mac)
		return 0;

	if (hwaddr_aton2(mac, bi->mac_addr) < 0) {
		wpa_printf(MSG_DEBUG, "DPP: Invalid URI mac");
		return -1;
	}

	wpa_printf(MSG_DEBUG, "DPP: URI mac: " MACSTR, MAC2STR(bi->mac_addr));

	return 0;
}


int dpp_parse_uri_info(struct dpp_bootstrap_info *bi, const char *info)
{
	const char *end;

	if (!info)
		return 0;

	end = os_strchr(info, ';');
	if (!end)
		end = info + os_strlen(info);
	bi->info = os_malloc(end - info + 1);
	if (!bi->info)
		return -1;
	os_memcpy(bi->info, info, end - info);
	bi->info[end - info] = '\0';
	wpa_printf(MSG_DEBUG, "DPP: URI(information): %s", bi->info);
	if (!dpp_uri_valid_info(bi->info)) {
		wpa_printf(MSG_DEBUG, "DPP: Invalid URI information payload");
		return -1;
	}

	return 0;
}


static const struct dpp_curve_params *
dpp_get_curve_oid(const ASN1_OBJECT *poid)
{
	ASN1_OBJECT *oid;
	int i;

	for (i = 0; dpp_curves[i].name; i++) {
		oid = OBJ_txt2obj(dpp_curves[i].name, 0);
		if (oid && OBJ_cmp(poid, oid) == 0)
			return &dpp_curves[i];
	}
	return NULL;
}


static const struct dpp_curve_params * dpp_get_curve_nid(int nid)
{
	int i, tmp;

	if (!nid)
		return NULL;
	for (i = 0; dpp_curves[i].name; i++) {
		tmp = OBJ_txt2nid(dpp_curves[i].name);
		if (tmp == nid)
			return &dpp_curves[i];
	}
	return NULL;
}


static int dpp_parse_uri_pk(struct dpp_bootstrap_info *bi, const char *info)
{
	const char *end;
	u8 *data;
	size_t data_len;
	EVP_PKEY *pkey;
	const unsigned char *p;
	int res;
	X509_PUBKEY *pub = NULL;
	ASN1_OBJECT *ppkalg;
	const unsigned char *pk;
	int ppklen;
	X509_ALGOR *pa;
#if OPENSSL_VERSION_NUMBER < 0x10100000L || defined(LIBRESSL_VERSION_NUMBER)
	ASN1_OBJECT *pa_oid;
#else
	const ASN1_OBJECT *pa_oid;
#endif
	const void *pval;
	int ptype;
	const ASN1_OBJECT *poid;
	char buf[100];

	end = os_strchr(info, ';');
	if (!end)
		return -1;

	data = base64_decode((const unsigned char *) info, end - info,
			     &data_len);
	if (!data) {
		wpa_printf(MSG_DEBUG,
			   "DPP: Invalid base64 encoding on URI public-key");
		return -1;
	}
	wpa_hexdump(MSG_DEBUG, "DPP: Base64 decoded URI public-key",
		    data, data_len);

	if (sha256_vector(1, (const u8 **) &data, &data_len,
			  bi->pubkey_hash) < 0) {
		wpa_printf(MSG_DEBUG, "DPP: Failed to hash public key");
		os_free(data);
		return -1;
	}
	wpa_hexdump(MSG_DEBUG, "DPP: Public key hash",
		    bi->pubkey_hash, SHA256_MAC_LEN);

	/* DER encoded ASN.1 SubjectPublicKeyInfo
	 *
	 * SubjectPublicKeyInfo  ::=  SEQUENCE  {
	 *      algorithm            AlgorithmIdentifier,
	 *      subjectPublicKey     BIT STRING  }
	 *
	 * AlgorithmIdentifier  ::=  SEQUENCE  {
	 *      algorithm               OBJECT IDENTIFIER,
	 *      parameters              ANY DEFINED BY algorithm OPTIONAL  }
	 *
	 * subjectPublicKey = compressed format public key per ANSI X9.63
	 * algorithm = ecPublicKey (1.2.840.10045.2.1)
	 * parameters = shall be present and shall be OBJECT IDENTIFIER; e.g.,
	 *       prime256v1 (1.2.840.10045.3.1.7)
	 */

	p = data;
	pkey = d2i_PUBKEY(NULL, &p, data_len);
	os_free(data);

	if (!pkey) {
		wpa_printf(MSG_DEBUG,
			   "DPP: Could not parse URI public-key SubjectPublicKeyInfo");
		return -1;
	}

	if (EVP_PKEY_type(EVP_PKEY_id(pkey)) != EVP_PKEY_EC) {
		wpa_printf(MSG_DEBUG,
			   "DPP: SubjectPublicKeyInfo does not describe an EC key");
		EVP_PKEY_free(pkey);
		return -1;
	}

	res = X509_PUBKEY_set(&pub, pkey);
	if (res != 1) {
		wpa_printf(MSG_DEBUG, "DPP: Could not set pubkey");
		goto fail;
	}

	res = X509_PUBKEY_get0_param(&ppkalg, &pk, &ppklen, &pa, pub);
	if (res != 1) {
		wpa_printf(MSG_DEBUG,
			   "DPP: Could not extract SubjectPublicKeyInfo parameters");
		goto fail;
	}
	res = OBJ_obj2txt(buf, sizeof(buf), ppkalg, 0);
	if (res < 0 || (size_t) res >= sizeof(buf)) {
		wpa_printf(MSG_DEBUG,
			   "DPP: Could not extract SubjectPublicKeyInfo algorithm");
		goto fail;
	}
	wpa_printf(MSG_DEBUG, "DPP: URI subjectPublicKey algorithm: %s", buf);
	if (os_strcmp(buf, "id-ecPublicKey") != 0) {
		wpa_printf(MSG_DEBUG,
			   "DPP: Unsupported SubjectPublicKeyInfo algorithm");
		goto fail;
	}

	X509_ALGOR_get0(&pa_oid, &ptype, (void *) &pval, pa);
	if (ptype != V_ASN1_OBJECT) {
		wpa_printf(MSG_DEBUG,
			   "DPP: SubjectPublicKeyInfo parameters did not contain an OID");
		goto fail;
	}
	poid = pval;
	res = OBJ_obj2txt(buf, sizeof(buf), poid, 0);
	if (res < 0 || (size_t) res >= sizeof(buf)) {
		wpa_printf(MSG_DEBUG,
			   "DPP: Could not extract SubjectPublicKeyInfo parameters OID");
		goto fail;
	}
	wpa_printf(MSG_DEBUG, "DPP: URI subjectPublicKey parameters: %s", buf);
	bi->curve = dpp_get_curve_oid(poid);
	if (!bi->curve) {
		wpa_printf(MSG_DEBUG,
			   "DPP: Unsupported SubjectPublicKeyInfo curve: %s",
			   buf);
		goto fail;
	}

	wpa_hexdump(MSG_DEBUG, "DPP: URI subjectPublicKey", pk, ppklen);

	X509_PUBKEY_free(pub);
	bi->pubkey = pkey;
	return 0;
fail:
	X509_PUBKEY_free(pub);
	EVP_PKEY_free(pkey);
	return -1;
}


static struct dpp_bootstrap_info * dpp_parse_uri(const char *uri)
{
	const char *pos = uri;
	const char *end;
	const char *chan_list = NULL, *mac = NULL, *info = NULL, *pk = NULL;
	struct dpp_bootstrap_info *bi;

	wpa_hexdump_ascii(MSG_DEBUG, "DPP: URI", uri, os_strlen(uri));

	if (os_strncmp(pos, "DPP:", 4) != 0) {
		wpa_printf(MSG_INFO, "DPP: Not a DPP URI");
		return NULL;
	}
	pos += 4;

	for (;;) {
		end = os_strchr(pos, ';');
		if (!end)
			break;

		if (end == pos) {
			/* Handle terminating ";;" and ignore unexpected ";"
			 * for parsing robustness. */
			pos++;
			continue;
		}

		if (pos[0] == 'C' && pos[1] == ':' && !chan_list)
			chan_list = pos + 2;
		else if (pos[0] == 'M' && pos[1] == ':' && !mac)
			mac = pos + 2;
		else if (pos[0] == 'I' && pos[1] == ':' && !info)
			info = pos + 2;
		else if (pos[0] == 'K' && pos[1] == ':' && !pk)
			pk = pos + 2;
		else
			wpa_hexdump_ascii(MSG_DEBUG,
					  "DPP: Ignore unrecognized URI parameter",
					  pos, end - pos);
		pos = end + 1;
	}

	if (!pk) {
		wpa_printf(MSG_INFO, "DPP: URI missing public-key");
		return NULL;
	}

	bi = os_zalloc(sizeof(*bi));
	if (!bi)
		return NULL;

	if (dpp_clone_uri(bi, uri) < 0 ||
	    dpp_parse_uri_chan_list(bi, chan_list) < 0 ||
	    dpp_parse_uri_mac(bi, mac) < 0 ||
	    dpp_parse_uri_info(bi, info) < 0 ||
	    dpp_parse_uri_pk(bi, pk) < 0) {
		dpp_bootstrap_info_free(bi);
		bi = NULL;
	}

	return bi;
}


struct dpp_bootstrap_info * dpp_parse_qr_code(const char *uri)
{
	struct dpp_bootstrap_info *bi;

	bi = dpp_parse_uri(uri);
	if (bi)
		bi->type = DPP_BOOTSTRAP_QR_CODE;
	return bi;
}


static void dpp_debug_print_key(const char *title, EVP_PKEY *key)
{
	EC_KEY *eckey;
	BIO *out;
	size_t rlen;
	char *txt;
	int res;
	unsigned char *der = NULL;
	int der_len;
	const EC_GROUP *group;
	const EC_POINT *point;

	out = BIO_new(BIO_s_mem());
	if (!out)
		return;

	EVP_PKEY_print_private(out, key, 0, NULL);
	rlen = BIO_ctrl_pending(out);
	txt = os_malloc(rlen + 1);
	if (txt) {
		res = BIO_read(out, txt, rlen);
		if (res > 0) {
			txt[res] = '\0';
			wpa_printf(MSG_DEBUG, "%s: %s", title, txt);
		}
		os_free(txt);
	}
	BIO_free(out);

	eckey = EVP_PKEY_get1_EC_KEY(key);
	if (!eckey)
		return;

	group = EC_KEY_get0_group(eckey);
	point = EC_KEY_get0_public_key(eckey);
	if (group && point)
		dpp_debug_print_point(title, group, point);

	der_len = i2d_ECPrivateKey(eckey, &der);
	if (der_len > 0)
		wpa_hexdump_key(MSG_DEBUG, "DPP: ECPrivateKey", der, der_len);
	OPENSSL_free(der);
	if (der_len <= 0) {
		der = NULL;
		der_len = i2d_EC_PUBKEY(eckey, &der);
		if (der_len > 0)
			wpa_hexdump(MSG_DEBUG, "DPP: EC_PUBKEY", der, der_len);
		OPENSSL_free(der);
	}

	EC_KEY_free(eckey);
}


static EVP_PKEY * dpp_gen_keypair(const struct dpp_curve_params *curve)
{
	EVP_PKEY_CTX *kctx = NULL;
	EC_KEY *ec_params;
	EVP_PKEY *params = NULL, *key = NULL;
	int nid;

	wpa_printf(MSG_DEBUG, "DPP: Generating a keypair");

	nid = OBJ_txt2nid(curve->name);
	if (nid == NID_undef) {
		wpa_printf(MSG_INFO, "DPP: Unsupported curve %s", curve->name);
		return NULL;
	}

	ec_params = EC_KEY_new_by_curve_name(nid);
	if (!ec_params) {
		wpa_printf(MSG_ERROR,
			   "DPP: Failed to generate EC_KEY parameters");
		goto fail;
	}
	EC_KEY_set_asn1_flag(ec_params, OPENSSL_EC_NAMED_CURVE);
	params = EVP_PKEY_new();
	if (!params || EVP_PKEY_set1_EC_KEY(params, ec_params) != 1) {
		wpa_printf(MSG_ERROR,
			   "DPP: Failed to generate EVP_PKEY parameters");
		goto fail;
	}

	kctx = EVP_PKEY_CTX_new(params, NULL);
	if (!kctx ||
	    EVP_PKEY_keygen_init(kctx) != 1 ||
	    EVP_PKEY_keygen(kctx, &key) != 1) {
		wpa_printf(MSG_ERROR, "DPP: Failed to generate EC key");
		goto fail;
	}

	if (wpa_debug_show_keys)
		dpp_debug_print_key("Own generated key", key);

	EVP_PKEY_free(params);
	EVP_PKEY_CTX_free(kctx);
	return key;
fail:
	EVP_PKEY_CTX_free(kctx);
	EVP_PKEY_free(params);
	return NULL;
}


static const struct dpp_curve_params *
dpp_get_curve_name(const char *name)
{
	int i;

	for (i = 0; dpp_curves[i].name; i++) {
		if (os_strcmp(name, dpp_curves[i].name) == 0 ||
		    (dpp_curves[i].jwk_crv &&
		     os_strcmp(name, dpp_curves[i].jwk_crv) == 0))
			return &dpp_curves[i];
	}
	return NULL;
}


static const struct dpp_curve_params *
dpp_get_curve_jwk_crv(const char *name)
{
	int i;

	for (i = 0; dpp_curves[i].name; i++) {
		if (dpp_curves[i].jwk_crv &&
		    os_strcmp(name, dpp_curves[i].jwk_crv) == 0)
			return &dpp_curves[i];
	}
	return NULL;
}


static EVP_PKEY * dpp_set_keypair(const struct dpp_curve_params **curve,
				  const u8 *privkey, size_t privkey_len)
{
	EVP_PKEY *pkey;
	EC_KEY *eckey;
	const EC_GROUP *group;
	int nid;

	pkey = EVP_PKEY_new();
	if (!pkey)
		return NULL;
	eckey = d2i_ECPrivateKey(NULL, &privkey, privkey_len);
	if (!eckey) {
		wpa_printf(MSG_INFO,
			   "DPP: OpenSSL: d2i_ECPrivateKey() failed: %s",
			   ERR_error_string(ERR_get_error(), NULL));
		EVP_PKEY_free(pkey);
		return NULL;
	}
	group = EC_KEY_get0_group(eckey);
	if (!group) {
		EC_KEY_free(eckey);
		EVP_PKEY_free(pkey);
		return NULL;
	}
	nid = EC_GROUP_get_curve_name(group);
	*curve = dpp_get_curve_nid(nid);
	if (!*curve) {
		wpa_printf(MSG_INFO,
			   "DPP: Unsupported curve (nid=%d) in pre-assigned key",
			   nid);
		EC_KEY_free(eckey);
		EVP_PKEY_free(pkey);
		return NULL;
	}

	if (EVP_PKEY_assign_EC_KEY(pkey, eckey) != 1) {
		EC_KEY_free(eckey);
		EVP_PKEY_free(pkey);
		return NULL;
	}
	return pkey;
}


typedef struct {
	/* AlgorithmIdentifier ecPublicKey with optional parameters present
	 * as an OID identifying the curve */
	X509_ALGOR *alg;
	/* Compressed format public key per ANSI X9.63 */
	ASN1_BIT_STRING *pub_key;
} DPP_BOOTSTRAPPING_KEY;

ASN1_SEQUENCE(DPP_BOOTSTRAPPING_KEY) = {
	ASN1_SIMPLE(DPP_BOOTSTRAPPING_KEY, alg, X509_ALGOR),
	ASN1_SIMPLE(DPP_BOOTSTRAPPING_KEY, pub_key, ASN1_BIT_STRING)
} ASN1_SEQUENCE_END(DPP_BOOTSTRAPPING_KEY);

IMPLEMENT_ASN1_FUNCTIONS(DPP_BOOTSTRAPPING_KEY);


static struct wpabuf * dpp_bootstrap_key_der(EVP_PKEY *key)
{
	unsigned char *der = NULL;
	int der_len;
	EC_KEY *eckey;
	struct wpabuf *ret = NULL;
	size_t len;
	const EC_GROUP *group;
	const EC_POINT *point;
	BN_CTX *ctx;
	DPP_BOOTSTRAPPING_KEY *bootstrap = NULL;
	int nid;

	ctx = BN_CTX_new();
	eckey = EVP_PKEY_get1_EC_KEY(key);
	if (!ctx || !eckey)
		goto fail;

	group = EC_KEY_get0_group(eckey);
	point = EC_KEY_get0_public_key(eckey);
	if (!group || !point)
		goto fail;
	dpp_debug_print_point("DPP: bootstrap public key", group, point);
	nid = EC_GROUP_get_curve_name(group);

	bootstrap = DPP_BOOTSTRAPPING_KEY_new();
	if (!bootstrap ||
	    X509_ALGOR_set0(bootstrap->alg, OBJ_nid2obj(EVP_PKEY_EC),
			    V_ASN1_OBJECT, (void *) OBJ_nid2obj(nid)) != 1)
		goto fail;

	len = EC_POINT_point2oct(group, point, POINT_CONVERSION_COMPRESSED,
				 NULL, 0, ctx);
	if (len == 0)
		goto fail;

	der = OPENSSL_malloc(len);
	if (!der)
		goto fail;
	len = EC_POINT_point2oct(group, point, POINT_CONVERSION_COMPRESSED,
				 der, len, ctx);

	OPENSSL_free(bootstrap->pub_key->data);
	bootstrap->pub_key->data = der;
	der = NULL;
	bootstrap->pub_key->length = len;
	/* No unused bits */
	bootstrap->pub_key->flags &= ~(ASN1_STRING_FLAG_BITS_LEFT | 0x07);
	bootstrap->pub_key->flags |= ASN1_STRING_FLAG_BITS_LEFT;

	der_len = i2d_DPP_BOOTSTRAPPING_KEY(bootstrap, &der);
	if (der_len <= 0) {
		wpa_printf(MSG_ERROR,
			   "DDP: Failed to build DER encoded public key");
		goto fail;
	}

	ret = wpabuf_alloc_copy(der, der_len);
fail:
	DPP_BOOTSTRAPPING_KEY_free(bootstrap);
	OPENSSL_free(der);
	EC_KEY_free(eckey);
	BN_CTX_free(ctx);
	return ret;
}


int dpp_bootstrap_key_hash(struct dpp_bootstrap_info *bi)
{
	struct wpabuf *der;
	int res;
	const u8 *addr[1];
	size_t len[1];

	der = dpp_bootstrap_key_der(bi->pubkey);
	if (!der)
		return -1;
	wpa_hexdump_buf(MSG_DEBUG, "DPP: Compressed public key (DER)",
			der);

	addr[0] = wpabuf_head(der);
	len[0] = wpabuf_len(der);
	res = sha256_vector(1, addr, len, bi->pubkey_hash);
	if (res < 0)
		wpa_printf(MSG_DEBUG, "DPP: Failed to hash public key");
	else
		wpa_hexdump(MSG_DEBUG, "DPP: Public key hash", bi->pubkey_hash,
			    SHA256_MAC_LEN);
	wpabuf_free(der);
	return res;
}


char * dpp_keygen(struct dpp_bootstrap_info *bi, const char *curve,
		  const u8 *privkey, size_t privkey_len)
{
	unsigned char *base64 = NULL;
	char *pos, *end;
	size_t len;
	struct wpabuf *der = NULL;
	const u8 *addr[1];
	int res;

	if (!curve) {
		bi->curve = &dpp_curves[0];
	} else {
		bi->curve = dpp_get_curve_name(curve);
		if (!bi->curve) {
			wpa_printf(MSG_INFO, "DPP: Unsupported curve: %s",
				   curve);
			return NULL;
		}
	}
	if (privkey)
		bi->pubkey = dpp_set_keypair(&bi->curve, privkey, privkey_len);
	else
		bi->pubkey = dpp_gen_keypair(bi->curve);
	if (!bi->pubkey)
		goto fail;
	bi->own = 1;

	der = dpp_bootstrap_key_der(bi->pubkey);
	if (!der)
		goto fail;
	wpa_hexdump_buf(MSG_DEBUG, "DPP: Compressed public key (DER)",
			der);

	addr[0] = wpabuf_head(der);
	len = wpabuf_len(der);
	res = sha256_vector(1, addr, &len, bi->pubkey_hash);
	if (res < 0) {
		wpa_printf(MSG_DEBUG, "DPP: Failed to hash public key");
		goto fail;
	}
	wpa_hexdump(MSG_DEBUG, "DPP: Public key hash", bi->pubkey_hash,
		    SHA256_MAC_LEN);

	base64 = base64_encode(wpabuf_head(der), wpabuf_len(der), &len);
	wpabuf_free(der);
	der = NULL;
	if (!base64)
		goto fail;
	pos = (char *) base64;
	end = pos + len;
	for (;;) {
		pos = os_strchr(pos, '\n');
		if (!pos)
			break;
		os_memmove(pos, pos + 1, end - pos);
	}
	return (char *) base64;
fail:
	os_free(base64);
	wpabuf_free(der);
	return NULL;
}


static int dpp_derive_k1(const u8 *Mx, size_t Mx_len, u8 *k1,
			 unsigned int hash_len)
{
	u8 salt[DPP_MAX_HASH_LEN], prk[DPP_MAX_HASH_LEN];
	const char *info = "first intermediate key";
	int res;

	/* k1 = HKDF(<>, "first intermediate key", M.x) */

	/* HKDF-Extract(<>, M.x) */
	os_memset(salt, 0, hash_len);
	if (dpp_hmac(hash_len, salt, hash_len, Mx, Mx_len, prk) < 0)
		return -1;
	wpa_hexdump_key(MSG_DEBUG, "DPP: PRK = HKDF-Extract(<>, IKM=M.x)",
			prk, hash_len);

	/* HKDF-Expand(PRK, info, L) */
	res = dpp_hkdf_expand(hash_len, prk, hash_len, info, k1, hash_len);
	os_memset(prk, 0, hash_len);
	if (res < 0)
		return -1;

	wpa_hexdump_key(MSG_DEBUG, "DPP: k1 = HKDF-Expand(PRK, info, L)",
			k1, hash_len);
	return 0;
}


static int dpp_derive_k2(const u8 *Nx, size_t Nx_len, u8 *k2,
			 unsigned int hash_len)
{
	u8 salt[DPP_MAX_HASH_LEN], prk[DPP_MAX_HASH_LEN];
	const char *info = "second intermediate key";
	int res;

	/* k2 = HKDF(<>, "second intermediate key", N.x) */

	/* HKDF-Extract(<>, N.x) */
	os_memset(salt, 0, hash_len);
	res = dpp_hmac(hash_len, salt, hash_len, Nx, Nx_len, prk);
	if (res < 0)
		return -1;
	wpa_hexdump_key(MSG_DEBUG, "DPP: PRK = HKDF-Extract(<>, IKM=N.x)",
			prk, hash_len);

	/* HKDF-Expand(PRK, info, L) */
	res = dpp_hkdf_expand(hash_len, prk, hash_len, info, k2, hash_len);
	os_memset(prk, 0, hash_len);
	if (res < 0)
		return -1;

	wpa_hexdump_key(MSG_DEBUG, "DPP: k2 = HKDF-Expand(PRK, info, L)",
			k2, hash_len);
	return 0;
}


static int dpp_derive_ke(struct dpp_authentication *auth, u8 *ke,
			 unsigned int hash_len)
{
	size_t nonce_len;
	u8 nonces[2 * DPP_MAX_NONCE_LEN];
	const char *info_ke = "DPP Key";
	u8 prk[DPP_MAX_HASH_LEN];
	int res;
	const u8 *addr[3];
	size_t len[3];
	size_t num_elem = 0;

	if (!auth->Mx_len || !auth->Nx_len) {
		wpa_printf(MSG_DEBUG,
			   "DPP: Mx/Nx not available - cannot derive ke");
		return -1;
	}

	/* ke = HKDF(I-nonce | R-nonce, "DPP Key", M.x | N.x [| L.x]) */

	/* HKDF-Extract(I-nonce | R-nonce, M.x | N.x [| L.x]) */
	nonce_len = auth->curve->nonce_len;
	os_memcpy(nonces, auth->i_nonce, nonce_len);
	os_memcpy(&nonces[nonce_len], auth->r_nonce, nonce_len);
	addr[num_elem] = auth->Mx;
	len[num_elem] = auth->Mx_len;
	num_elem++;
	addr[num_elem] = auth->Nx;
	len[num_elem] = auth->Nx_len;
	num_elem++;
	if (auth->peer_bi && auth->own_bi) {
		if (!auth->Lx_len) {
			wpa_printf(MSG_DEBUG,
				   "DPP: Lx not available - cannot derive ke");
			return -1;
		}
		addr[num_elem] = auth->Lx;
		len[num_elem] = auth->secret_len;
		num_elem++;
	}
	res = dpp_hmac_vector(hash_len, nonces, 2 * nonce_len,
			      num_elem, addr, len, prk);
	if (res < 0)
		return -1;
	wpa_hexdump_key(MSG_DEBUG, "DPP: PRK = HKDF-Extract(<>, IKM)",
			prk, hash_len);

	/* HKDF-Expand(PRK, info, L) */
	res = dpp_hkdf_expand(hash_len, prk, hash_len, info_ke, ke, hash_len);
	os_memset(prk, 0, hash_len);
	if (res < 0)
		return -1;

	wpa_hexdump_key(MSG_DEBUG, "DPP: ke = HKDF-Expand(PRK, info, L)",
			ke, hash_len);
	return 0;
}


static void dpp_build_attr_status(struct wpabuf *msg,
				  enum dpp_status_error status)
{
	wpa_printf(MSG_DEBUG, "DPP: Status %d", status);
	wpabuf_put_le16(msg, DPP_ATTR_STATUS);
	wpabuf_put_le16(msg, 1);
	wpabuf_put_u8(msg, status);
}


static void dpp_build_attr_r_bootstrap_key_hash(struct wpabuf *msg,
						const u8 *hash)
{
	if (hash) {
		wpa_printf(MSG_DEBUG, "DPP: R-Bootstrap Key Hash");
		wpabuf_put_le16(msg, DPP_ATTR_R_BOOTSTRAP_KEY_HASH);
		wpabuf_put_le16(msg, SHA256_MAC_LEN);
		wpabuf_put_data(msg, hash, SHA256_MAC_LEN);
	}
}


static void dpp_build_attr_i_bootstrap_key_hash(struct wpabuf *msg,
						const u8 *hash)
{
	if (hash) {
		wpa_printf(MSG_DEBUG, "DPP: I-Bootstrap Key Hash");
		wpabuf_put_le16(msg, DPP_ATTR_I_BOOTSTRAP_KEY_HASH);
		wpabuf_put_le16(msg, SHA256_MAC_LEN);
		wpabuf_put_data(msg, hash, SHA256_MAC_LEN);
	}
}


static struct wpabuf * dpp_auth_build_req(struct dpp_authentication *auth,
					  const struct wpabuf *pi,
					  size_t nonce_len,
					  const u8 *r_pubkey_hash,
					  const u8 *i_pubkey_hash,
					  unsigned int neg_freq)
{
	struct wpabuf *msg;
	u8 clear[4 + DPP_MAX_NONCE_LEN + 4 + 1];
	u8 wrapped_data[4 + DPP_MAX_NONCE_LEN + 4 + 1 + AES_BLOCK_SIZE];
	u8 *pos;
	const u8 *addr[2];
	size_t len[2], siv_len, attr_len;
	u8 *attr_start, *attr_end;

	/* Build DPP Authentication Request frame attributes */
	attr_len = 2 * (4 + SHA256_MAC_LEN) + 4 + (pi ? wpabuf_len(pi) : 0) +
		4 + sizeof(wrapped_data);
	if (neg_freq > 0)
		attr_len += 4 + 2;
#ifdef CONFIG_TESTING_OPTIONS
	if (dpp_test == DPP_TEST_AFTER_WRAPPED_DATA_AUTH_REQ)
		attr_len += 5;
#endif /* CONFIG_TESTING_OPTIONS */
	msg = dpp_alloc_msg(DPP_PA_AUTHENTICATION_REQ, attr_len);
	if (!msg)
		return NULL;

	attr_start = wpabuf_put(msg, 0);

	/* Responder Bootstrapping Key Hash */
	dpp_build_attr_r_bootstrap_key_hash(msg, r_pubkey_hash);

	/* Initiator Bootstrapping Key Hash */
	dpp_build_attr_i_bootstrap_key_hash(msg, i_pubkey_hash);

	/* Initiator Protocol Key */
	if (pi) {
		wpabuf_put_le16(msg, DPP_ATTR_I_PROTOCOL_KEY);
		wpabuf_put_le16(msg, wpabuf_len(pi));
		wpabuf_put_buf(msg, pi);
	}

	/* Channel */
	if (neg_freq > 0) {
		u8 op_class, channel;

		if (ieee80211_freq_to_channel_ext(neg_freq, 0, 0, &op_class,
						  &channel) ==
		    NUM_HOSTAPD_MODES) {
			wpa_printf(MSG_INFO,
				   "DPP: Unsupported negotiation frequency request: %d",
				   neg_freq);
			wpabuf_free(msg);
			return NULL;
		}
		wpabuf_put_le16(msg, DPP_ATTR_CHANNEL);
		wpabuf_put_le16(msg, 2);
		wpabuf_put_u8(msg, op_class);
		wpabuf_put_u8(msg, channel);
	}

#ifdef CONFIG_TESTING_OPTIONS
	if (dpp_test == DPP_TEST_NO_WRAPPED_DATA_AUTH_REQ) {
		wpa_printf(MSG_INFO, "DPP: TESTING - no Wrapped Data");
		goto skip_wrapped_data;
	}
#endif /* CONFIG_TESTING_OPTIONS */

	/* Wrapped data ({I-nonce, I-capabilities}k1) */
	pos = clear;

#ifdef CONFIG_TESTING_OPTIONS
	if (dpp_test == DPP_TEST_NO_I_NONCE_AUTH_REQ) {
		wpa_printf(MSG_INFO, "DPP: TESTING - no I-nonce");
		goto skip_i_nonce;
	}
	if (dpp_test == DPP_TEST_INVALID_I_NONCE_AUTH_REQ) {
		wpa_printf(MSG_INFO, "DPP: TESTING - invalid I-nonce");
		WPA_PUT_LE16(pos, DPP_ATTR_I_NONCE);
		pos += 2;
		WPA_PUT_LE16(pos, nonce_len - 1);
		pos += 2;
		os_memcpy(pos, auth->i_nonce, nonce_len - 1);
		pos += nonce_len - 1;
		goto skip_i_nonce;
	}
#endif /* CONFIG_TESTING_OPTIONS */

	/* I-nonce */
	WPA_PUT_LE16(pos, DPP_ATTR_I_NONCE);
	pos += 2;
	WPA_PUT_LE16(pos, nonce_len);
	pos += 2;
	os_memcpy(pos, auth->i_nonce, nonce_len);
	pos += nonce_len;

#ifdef CONFIG_TESTING_OPTIONS
skip_i_nonce:
	if (dpp_test == DPP_TEST_NO_I_CAPAB_AUTH_REQ) {
		wpa_printf(MSG_INFO, "DPP: TESTING - no I-capab");
		goto skip_i_capab;
	}
#endif /* CONFIG_TESTING_OPTIONS */

	/* I-capabilities */
	WPA_PUT_LE16(pos, DPP_ATTR_I_CAPABILITIES);
	pos += 2;
	WPA_PUT_LE16(pos, 1);
	pos += 2;
	auth->i_capab = auth->allowed_roles;
	*pos++ = auth->i_capab;
#ifdef CONFIG_TESTING_OPTIONS
	if (dpp_test == DPP_TEST_ZERO_I_CAPAB) {
		wpa_printf(MSG_INFO, "DPP: TESTING - zero I-capabilities");
		pos[-1] = 0;
	}
skip_i_capab:
#endif /* CONFIG_TESTING_OPTIONS */

	attr_end = wpabuf_put(msg, 0);

	/* OUI, OUI type, Crypto Suite, DPP frame type */
	addr[0] = wpabuf_head_u8(msg) + 2;
	len[0] = 3 + 1 + 1 + 1;
	wpa_hexdump(MSG_DEBUG, "DDP: AES-SIV AD[0]", addr[0], len[0]);

	/* Attributes before Wrapped Data */
	addr[1] = attr_start;
	len[1] = attr_end - attr_start;
	wpa_hexdump(MSG_DEBUG, "DDP: AES-SIV AD[1]", addr[1], len[1]);

	siv_len = pos - clear;
	wpa_hexdump(MSG_DEBUG, "DPP: AES-SIV cleartext", clear, siv_len);
	if (aes_siv_encrypt(auth->k1, auth->curve->hash_len, clear, siv_len,
			    2, addr, len, wrapped_data) < 0) {
		wpabuf_free(msg);
		return NULL;
	}
	siv_len += AES_BLOCK_SIZE;
	wpa_hexdump(MSG_DEBUG, "DPP: AES-SIV ciphertext",
		    wrapped_data, siv_len);

	wpabuf_put_le16(msg, DPP_ATTR_WRAPPED_DATA);
	wpabuf_put_le16(msg, siv_len);
	wpabuf_put_data(msg, wrapped_data, siv_len);

#ifdef CONFIG_TESTING_OPTIONS
	if (dpp_test == DPP_TEST_AFTER_WRAPPED_DATA_AUTH_REQ) {
		wpa_printf(MSG_INFO, "DPP: TESTING - attr after Wrapped Data");
		dpp_build_attr_status(msg, DPP_STATUS_OK);
	}
skip_wrapped_data:
#endif /* CONFIG_TESTING_OPTIONS */

	wpa_hexdump_buf(MSG_DEBUG,
			"DPP: Authentication Request frame attributes", msg);

	return msg;
}


static struct wpabuf * dpp_auth_build_resp(struct dpp_authentication *auth,
					   enum dpp_status_error status,
					   const struct wpabuf *pr,
					   size_t nonce_len,
					   const u8 *r_pubkey_hash,
					   const u8 *i_pubkey_hash,
					   const u8 *r_nonce, const u8 *i_nonce,
					   const u8 *wrapped_r_auth,
					   size_t wrapped_r_auth_len,
					   const u8 *siv_key)
{
	struct wpabuf *msg;
#define DPP_AUTH_RESP_CLEAR_LEN 2 * (4 + DPP_MAX_NONCE_LEN) + 4 + 1 + \
		4 + 4 + DPP_MAX_HASH_LEN + AES_BLOCK_SIZE
	u8 clear[DPP_AUTH_RESP_CLEAR_LEN];
	u8 wrapped_data[DPP_AUTH_RESP_CLEAR_LEN + AES_BLOCK_SIZE];
	const u8 *addr[2];
	size_t len[2], siv_len, attr_len;
	u8 *attr_start, *attr_end, *pos;

	auth->waiting_auth_conf = 1;
	auth->auth_resp_tries = 0;

	/* Build DPP Authentication Response frame attributes */
	attr_len = 4 + 1 + 2 * (4 + SHA256_MAC_LEN) +
		4 + (pr ? wpabuf_len(pr) : 0) + 4 + sizeof(wrapped_data);
#ifdef CONFIG_TESTING_OPTIONS
	if (dpp_test == DPP_TEST_AFTER_WRAPPED_DATA_AUTH_RESP)
		attr_len += 5;
#endif /* CONFIG_TESTING_OPTIONS */
	msg = dpp_alloc_msg(DPP_PA_AUTHENTICATION_RESP, attr_len);
	if (!msg)
		return NULL;

	attr_start = wpabuf_put(msg, 0);

	/* DPP Status */
	if (status != 255)
		dpp_build_attr_status(msg, status);

	/* Responder Bootstrapping Key Hash */
	dpp_build_attr_r_bootstrap_key_hash(msg, r_pubkey_hash);

	/* Initiator Bootstrapping Key Hash (mutual authentication) */
	dpp_build_attr_i_bootstrap_key_hash(msg, i_pubkey_hash);

	/* Responder Protocol Key */
	if (pr) {
		wpabuf_put_le16(msg, DPP_ATTR_R_PROTOCOL_KEY);
		wpabuf_put_le16(msg, wpabuf_len(pr));
		wpabuf_put_buf(msg, pr);
	}

	attr_end = wpabuf_put(msg, 0);

#ifdef CONFIG_TESTING_OPTIONS
	if (dpp_test == DPP_TEST_NO_WRAPPED_DATA_AUTH_RESP) {
		wpa_printf(MSG_INFO, "DPP: TESTING - no Wrapped Data");
		goto skip_wrapped_data;
	}
#endif /* CONFIG_TESTING_OPTIONS */

	/* Wrapped data ({R-nonce, I-nonce, R-capabilities, {R-auth}ke}k2) */
	pos = clear;

	if (r_nonce) {
		/* R-nonce */
		WPA_PUT_LE16(pos, DPP_ATTR_R_NONCE);
		pos += 2;
		WPA_PUT_LE16(pos, nonce_len);
		pos += 2;
		os_memcpy(pos, r_nonce, nonce_len);
		pos += nonce_len;
	}

	if (i_nonce) {
		/* I-nonce */
		WPA_PUT_LE16(pos, DPP_ATTR_I_NONCE);
		pos += 2;
		WPA_PUT_LE16(pos, nonce_len);
		pos += 2;
		os_memcpy(pos, i_nonce, nonce_len);
#ifdef CONFIG_TESTING_OPTIONS
		if (dpp_test == DPP_TEST_I_NONCE_MISMATCH_AUTH_RESP) {
			wpa_printf(MSG_INFO, "DPP: TESTING - I-nonce mismatch");
			pos[nonce_len / 2] ^= 0x01;
		}
#endif /* CONFIG_TESTING_OPTIONS */
		pos += nonce_len;
	}

#ifdef CONFIG_TESTING_OPTIONS
	if (dpp_test == DPP_TEST_NO_R_CAPAB_AUTH_RESP) {
		wpa_printf(MSG_INFO, "DPP: TESTING - no R-capab");
		goto skip_r_capab;
	}
#endif /* CONFIG_TESTING_OPTIONS */

	/* R-capabilities */
	WPA_PUT_LE16(pos, DPP_ATTR_R_CAPABILITIES);
	pos += 2;
	WPA_PUT_LE16(pos, 1);
	pos += 2;
	auth->r_capab = auth->configurator ? DPP_CAPAB_CONFIGURATOR :
		DPP_CAPAB_ENROLLEE;
	*pos++ = auth->r_capab;
#ifdef CONFIG_TESTING_OPTIONS
	if (dpp_test == DPP_TEST_ZERO_R_CAPAB) {
		wpa_printf(MSG_INFO, "DPP: TESTING - zero R-capabilities");
		pos[-1] = 0;
	} else if (dpp_test == DPP_TEST_INCOMPATIBLE_R_CAPAB_AUTH_RESP) {
		wpa_printf(MSG_INFO,
			   "DPP: TESTING - incompatible R-capabilities");
		if ((auth->i_capab & DPP_CAPAB_ROLE_MASK) ==
		    (DPP_CAPAB_CONFIGURATOR | DPP_CAPAB_ENROLLEE))
			pos[-1] = 0;
		else
			pos[-1] = auth->configurator ? DPP_CAPAB_ENROLLEE :
				DPP_CAPAB_CONFIGURATOR;
	}
skip_r_capab:
#endif /* CONFIG_TESTING_OPTIONS */

	if (wrapped_r_auth) {
		/* {R-auth}ke */
		WPA_PUT_LE16(pos, DPP_ATTR_WRAPPED_DATA);
		pos += 2;
		WPA_PUT_LE16(pos, wrapped_r_auth_len);
		pos += 2;
		os_memcpy(pos, wrapped_r_auth, wrapped_r_auth_len);
		pos += wrapped_r_auth_len;
	}

	/* OUI, OUI type, Crypto Suite, DPP frame type */
	addr[0] = wpabuf_head_u8(msg) + 2;
	len[0] = 3 + 1 + 1 + 1;
	wpa_hexdump(MSG_DEBUG, "DDP: AES-SIV AD[0]", addr[0], len[0]);

	/* Attributes before Wrapped Data */
	addr[1] = attr_start;
	len[1] = attr_end - attr_start;
	wpa_hexdump(MSG_DEBUG, "DDP: AES-SIV AD[1]", addr[1], len[1]);

	siv_len = pos - clear;
	wpa_hexdump(MSG_DEBUG, "DPP: AES-SIV cleartext", clear, siv_len);
	if (aes_siv_encrypt(siv_key, auth->curve->hash_len, clear, siv_len,
			    2, addr, len, wrapped_data) < 0) {
		wpabuf_free(msg);
		return NULL;
	}
	siv_len += AES_BLOCK_SIZE;
	wpa_hexdump(MSG_DEBUG, "DPP: AES-SIV ciphertext",
		    wrapped_data, siv_len);

	wpabuf_put_le16(msg, DPP_ATTR_WRAPPED_DATA);
	wpabuf_put_le16(msg, siv_len);
	wpabuf_put_data(msg, wrapped_data, siv_len);

#ifdef CONFIG_TESTING_OPTIONS
	if (dpp_test == DPP_TEST_AFTER_WRAPPED_DATA_AUTH_RESP) {
		wpa_printf(MSG_INFO, "DPP: TESTING - attr after Wrapped Data");
		dpp_build_attr_status(msg, DPP_STATUS_OK);
	}
skip_wrapped_data:
#endif /* CONFIG_TESTING_OPTIONS */

	wpa_hexdump_buf(MSG_DEBUG,
			"DPP: Authentication Response frame attributes", msg);
	return msg;
}


static int dpp_channel_ok_init(struct hostapd_hw_modes *own_modes,
			       u16 num_modes, unsigned int freq)
{
	u16 m;
	int c, flag;

	if (!own_modes || !num_modes)
		return 1;

	for (m = 0; m < num_modes; m++) {
		for (c = 0; c < own_modes[m].num_channels; c++) {
			if ((unsigned int) own_modes[m].channels[c].freq !=
			    freq)
				continue;
			flag = own_modes[m].channels[c].flag;
			if (!(flag & (HOSTAPD_CHAN_DISABLED |
				      HOSTAPD_CHAN_NO_IR |
				      HOSTAPD_CHAN_RADAR)))
				return 1;
		}
	}

	wpa_printf(MSG_DEBUG, "DPP: Peer channel %u MHz not supported", freq);
	return 0;
}


static int freq_included(const unsigned int freqs[], unsigned int num,
			 unsigned int freq)
{
	while (num > 0) {
		if (freqs[--num] == freq)
			return 1;
	}
	return 0;
}


static void freq_to_start(unsigned int freqs[], unsigned int num,
			  unsigned int freq)
{
	unsigned int i;

	for (i = 0; i < num; i++) {
		if (freqs[i] == freq)
			break;
	}
	if (i == 0 || i >= num)
		return;
	os_memmove(&freqs[1], &freqs[0], i * sizeof(freqs[0]));
	freqs[0] = freq;
}


static int dpp_channel_intersect(struct dpp_authentication *auth,
				 struct hostapd_hw_modes *own_modes,
				 u16 num_modes)
{
	struct dpp_bootstrap_info *peer_bi = auth->peer_bi;
	unsigned int i, freq;

	for (i = 0; i < peer_bi->num_freq; i++) {
		freq = peer_bi->freq[i];
		if (freq_included(auth->freq, auth->num_freq, freq))
			continue;
		if (dpp_channel_ok_init(own_modes, num_modes, freq))
			auth->freq[auth->num_freq++] = freq;
	}
	if (!auth->num_freq) {
		wpa_printf(MSG_INFO,
			   "DPP: No available channels for initiating DPP Authentication");
		return -1;
	}
	auth->curr_freq = auth->freq[0];
	return 0;
}


static int dpp_channel_local_list(struct dpp_authentication *auth,
				  struct hostapd_hw_modes *own_modes,
				  u16 num_modes)
{
	u16 m;
	int c, flag;
	unsigned int freq;

	auth->num_freq = 0;

	if (!own_modes || !num_modes) {
		auth->freq[0] = 2412;
		auth->freq[1] = 2437;
		auth->freq[2] = 2462;
		auth->num_freq = 3;
		return 0;
	}

	for (m = 0; m < num_modes; m++) {
		for (c = 0; c < own_modes[m].num_channels; c++) {
			freq = own_modes[m].channels[c].freq;
			flag = own_modes[m].channels[c].flag;
			if (flag & (HOSTAPD_CHAN_DISABLED |
				    HOSTAPD_CHAN_NO_IR |
				    HOSTAPD_CHAN_RADAR))
				continue;
			if (freq_included(auth->freq, auth->num_freq, freq))
				continue;
			auth->freq[auth->num_freq++] = freq;
			if (auth->num_freq == DPP_BOOTSTRAP_MAX_FREQ) {
				m = num_modes;
				break;
			}
		}
	}

	return auth->num_freq == 0 ? -1 : 0;
}


static int dpp_prepare_channel_list(struct dpp_authentication *auth,
				    struct hostapd_hw_modes *own_modes,
				    u16 num_modes)
{
	int res;
	char freqs[DPP_BOOTSTRAP_MAX_FREQ * 6 + 10], *pos, *end;
	unsigned int i;

	if (auth->peer_bi->num_freq > 0)
		res = dpp_channel_intersect(auth, own_modes, num_modes);
	else
		res = dpp_channel_local_list(auth, own_modes, num_modes);
	if (res < 0)
		return res;

	/* Prioritize 2.4 GHz channels 6, 1, 11 (in this order) to hit the most
	 * likely channels first. */
	freq_to_start(auth->freq, auth->num_freq, 2462);
	freq_to_start(auth->freq, auth->num_freq, 2412);
	freq_to_start(auth->freq, auth->num_freq, 2437);

	auth->freq_idx = 0;
	auth->curr_freq = auth->freq[0];

	pos = freqs;
	end = pos + sizeof(freqs);
	for (i = 0; i < auth->num_freq; i++) {
		res = os_snprintf(pos, end - pos, " %u", auth->freq[i]);
		if (os_snprintf_error(end - pos, res))
			break;
		pos += res;
	}
	*pos = '\0';
	wpa_printf(MSG_DEBUG, "DPP: Possible frequencies for initiating:%s",
		   freqs);

	return 0;
}


static int dpp_autogen_bootstrap_key(struct dpp_authentication *auth)
{
	struct dpp_bootstrap_info *bi;
	char *pk = NULL;
	size_t len;

	if (auth->own_bi)
		return 0; /* already generated */

	bi = os_zalloc(sizeof(*bi));
	if (!bi)
		return -1;
	bi->type = DPP_BOOTSTRAP_QR_CODE;
	pk = dpp_keygen(bi, auth->peer_bi->curve->name, NULL, 0);
	if (!pk)
		goto fail;

	len = 4; /* "DPP:" */
	len += 4 + os_strlen(pk);
	bi->uri = os_malloc(len + 1);
	if (!bi->uri)
		goto fail;
	os_snprintf(bi->uri, len + 1, "DPP:K:%s;;", pk);
	wpa_printf(MSG_DEBUG,
		   "DPP: Auto-generated own bootstrapping key info: URI %s",
		   bi->uri);

	auth->tmp_own_bi = auth->own_bi = bi;

	os_free(pk);

	return 0;
fail:
	os_free(pk);
	dpp_bootstrap_info_free(bi);
	return -1;
}


struct dpp_authentication * dpp_auth_init(void *msg_ctx,
					  struct dpp_bootstrap_info *peer_bi,
					  struct dpp_bootstrap_info *own_bi,
					  u8 dpp_allowed_roles,
					  unsigned int neg_freq,
					  struct hostapd_hw_modes *own_modes,
					  u16 num_modes)
{
	struct dpp_authentication *auth;
	size_t nonce_len;
	EVP_PKEY_CTX *ctx = NULL;
	size_t secret_len;
	struct wpabuf *pi = NULL;
	const u8 *r_pubkey_hash, *i_pubkey_hash;
#ifdef CONFIG_TESTING_OPTIONS
	u8 test_hash[SHA256_MAC_LEN];
#endif /* CONFIG_TESTING_OPTIONS */

	auth = os_zalloc(sizeof(*auth));
	if (!auth)
		return NULL;
	auth->msg_ctx = msg_ctx;
	auth->initiator = 1;
	auth->waiting_auth_resp = 1;
	auth->allowed_roles = dpp_allowed_roles;
	auth->configurator = !!(dpp_allowed_roles & DPP_CAPAB_CONFIGURATOR);
	auth->peer_bi = peer_bi;
	auth->own_bi = own_bi;
	auth->curve = peer_bi->curve;

	if (dpp_autogen_bootstrap_key(auth) < 0 ||
	    dpp_prepare_channel_list(auth, own_modes, num_modes) < 0)
		goto fail;

#ifdef CONFIG_TESTING_OPTIONS
	if (dpp_nonce_override_len > 0) {
		wpa_printf(MSG_INFO, "DPP: TESTING - override I-nonce");
		nonce_len = dpp_nonce_override_len;
		os_memcpy(auth->i_nonce, dpp_nonce_override, nonce_len);
	} else {
		nonce_len = auth->curve->nonce_len;
		if (random_get_bytes(auth->i_nonce, nonce_len)) {
			wpa_printf(MSG_ERROR,
				   "DPP: Failed to generate I-nonce");
			goto fail;
		}
	}
#else /* CONFIG_TESTING_OPTIONS */
	nonce_len = auth->curve->nonce_len;
	if (random_get_bytes(auth->i_nonce, nonce_len)) {
		wpa_printf(MSG_ERROR, "DPP: Failed to generate I-nonce");
		goto fail;
	}
#endif /* CONFIG_TESTING_OPTIONS */
	wpa_hexdump(MSG_DEBUG, "DPP: I-nonce", auth->i_nonce, nonce_len);

#ifdef CONFIG_TESTING_OPTIONS
	if (dpp_protocol_key_override_len) {
		const struct dpp_curve_params *tmp_curve;

		wpa_printf(MSG_INFO,
			   "DPP: TESTING - override protocol key");
		auth->own_protocol_key = dpp_set_keypair(
			&tmp_curve, dpp_protocol_key_override,
			dpp_protocol_key_override_len);
	} else {
		auth->own_protocol_key = dpp_gen_keypair(auth->curve);
	}
#else /* CONFIG_TESTING_OPTIONS */
	auth->own_protocol_key = dpp_gen_keypair(auth->curve);
#endif /* CONFIG_TESTING_OPTIONS */
	if (!auth->own_protocol_key)
		goto fail;

	pi = dpp_get_pubkey_point(auth->own_protocol_key, 0);
	if (!pi)
		goto fail;

	/* ECDH: M = pI * BR */
	ctx = EVP_PKEY_CTX_new(auth->own_protocol_key, NULL);
	if (!ctx ||
	    EVP_PKEY_derive_init(ctx) != 1 ||
	    EVP_PKEY_derive_set_peer(ctx, auth->peer_bi->pubkey) != 1 ||
	    EVP_PKEY_derive(ctx, NULL, &secret_len) != 1 ||
	    secret_len > DPP_MAX_SHARED_SECRET_LEN ||
	    EVP_PKEY_derive(ctx, auth->Mx, &secret_len) != 1) {
		wpa_printf(MSG_ERROR,
			   "DPP: Failed to derive ECDH shared secret: %s",
			   ERR_error_string(ERR_get_error(), NULL));
		goto fail;
	}
	auth->secret_len = secret_len;
	EVP_PKEY_CTX_free(ctx);
	ctx = NULL;

	wpa_hexdump_key(MSG_DEBUG, "DPP: ECDH shared secret (M.x)",
			auth->Mx, auth->secret_len);
	auth->Mx_len = auth->secret_len;

	if (dpp_derive_k1(auth->Mx, auth->secret_len, auth->k1,
			  auth->curve->hash_len) < 0)
		goto fail;

	r_pubkey_hash = auth->peer_bi->pubkey_hash;
	i_pubkey_hash = auth->own_bi->pubkey_hash;

#ifdef CONFIG_TESTING_OPTIONS
	if (dpp_test == DPP_TEST_NO_R_BOOTSTRAP_KEY_HASH_AUTH_REQ) {
		wpa_printf(MSG_INFO, "DPP: TESTING - no R-Bootstrap Key Hash");
		r_pubkey_hash = NULL;
	} else if (dpp_test == DPP_TEST_INVALID_R_BOOTSTRAP_KEY_HASH_AUTH_REQ) {
		wpa_printf(MSG_INFO,
			   "DPP: TESTING - invalid R-Bootstrap Key Hash");
		os_memcpy(test_hash, r_pubkey_hash, SHA256_MAC_LEN);
		test_hash[SHA256_MAC_LEN - 1] ^= 0x01;
		r_pubkey_hash = test_hash;
	} else if (dpp_test == DPP_TEST_NO_I_BOOTSTRAP_KEY_HASH_AUTH_REQ) {
		wpa_printf(MSG_INFO, "DPP: TESTING - no I-Bootstrap Key Hash");
		i_pubkey_hash = NULL;
	} else if (dpp_test == DPP_TEST_INVALID_I_BOOTSTRAP_KEY_HASH_AUTH_REQ) {
		wpa_printf(MSG_INFO,
			   "DPP: TESTING - invalid I-Bootstrap Key Hash");
		os_memcpy(test_hash, i_pubkey_hash, SHA256_MAC_LEN);
		test_hash[SHA256_MAC_LEN - 1] ^= 0x01;
		i_pubkey_hash = test_hash;
	} else if (dpp_test == DPP_TEST_NO_I_PROTO_KEY_AUTH_REQ) {
		wpa_printf(MSG_INFO, "DPP: TESTING - no I-Proto Key");
		wpabuf_free(pi);
		pi = NULL;
	} else if (dpp_test == DPP_TEST_INVALID_I_PROTO_KEY_AUTH_REQ) {
		wpa_printf(MSG_INFO, "DPP: TESTING - invalid I-Proto Key");
		wpabuf_free(pi);
		pi = wpabuf_alloc(2 * auth->curve->prime_len);
		if (!pi || dpp_test_gen_invalid_key(pi, auth->curve) < 0)
			goto fail;
	}
#endif /* CONFIG_TESTING_OPTIONS */

	auth->req_msg = dpp_auth_build_req(auth, pi, nonce_len, r_pubkey_hash,
					   i_pubkey_hash, neg_freq);
	if (!auth->req_msg)
		goto fail;

out:
	wpabuf_free(pi);
	EVP_PKEY_CTX_free(ctx);
	return auth;
fail:
	dpp_auth_deinit(auth);
	auth = NULL;
	goto out;
}


struct wpabuf * dpp_build_conf_req(struct dpp_authentication *auth,
				   const char *json)
{
	size_t nonce_len;
	size_t json_len, clear_len;
	struct wpabuf *clear = NULL, *msg = NULL;
	u8 *wrapped;
	size_t attr_len;

	wpa_printf(MSG_DEBUG, "DPP: Build configuration request");

	nonce_len = auth->curve->nonce_len;
	if (random_get_bytes(auth->e_nonce, nonce_len)) {
		wpa_printf(MSG_ERROR, "DPP: Failed to generate E-nonce");
		goto fail;
	}
	wpa_hexdump(MSG_DEBUG, "DPP: E-nonce", auth->e_nonce, nonce_len);
	json_len = os_strlen(json);
	wpa_hexdump_ascii(MSG_DEBUG, "DPP: configAttr JSON", json, json_len);

	/* { E-nonce, configAttrib }ke */
	clear_len = 4 + nonce_len + 4 + json_len;
	clear = wpabuf_alloc(clear_len);
	attr_len = 4 + clear_len + AES_BLOCK_SIZE;
#ifdef CONFIG_TESTING_OPTIONS
	if (dpp_test == DPP_TEST_AFTER_WRAPPED_DATA_CONF_REQ)
		attr_len += 5;
#endif /* CONFIG_TESTING_OPTIONS */
	msg = wpabuf_alloc(attr_len);
	if (!clear || !msg)
		goto fail;

#ifdef CONFIG_TESTING_OPTIONS
	if (dpp_test == DPP_TEST_NO_E_NONCE_CONF_REQ) {
		wpa_printf(MSG_INFO, "DPP: TESTING - no E-nonce");
		goto skip_e_nonce;
	}
	if (dpp_test == DPP_TEST_INVALID_E_NONCE_CONF_REQ) {
		wpa_printf(MSG_INFO, "DPP: TESTING - invalid E-nonce");
		wpabuf_put_le16(clear, DPP_ATTR_ENROLLEE_NONCE);
		wpabuf_put_le16(clear, nonce_len - 1);
		wpabuf_put_data(clear, auth->e_nonce, nonce_len - 1);
		goto skip_e_nonce;
	}
	if (dpp_test == DPP_TEST_NO_WRAPPED_DATA_CONF_REQ) {
		wpa_printf(MSG_INFO, "DPP: TESTING - no Wrapped Data");
		goto skip_wrapped_data;
	}
#endif /* CONFIG_TESTING_OPTIONS */

	/* E-nonce */
	wpabuf_put_le16(clear, DPP_ATTR_ENROLLEE_NONCE);
	wpabuf_put_le16(clear, nonce_len);
	wpabuf_put_data(clear, auth->e_nonce, nonce_len);

#ifdef CONFIG_TESTING_OPTIONS
skip_e_nonce:
	if (dpp_test == DPP_TEST_NO_CONFIG_ATTR_OBJ_CONF_REQ) {
		wpa_printf(MSG_INFO, "DPP: TESTING - no configAttrib");
		goto skip_conf_attr_obj;
	}
#endif /* CONFIG_TESTING_OPTIONS */

	/* configAttrib */
	wpabuf_put_le16(clear, DPP_ATTR_CONFIG_ATTR_OBJ);
	wpabuf_put_le16(clear, json_len);
	wpabuf_put_data(clear, json, json_len);

#ifdef CONFIG_TESTING_OPTIONS
skip_conf_attr_obj:
#endif /* CONFIG_TESTING_OPTIONS */

	wpabuf_put_le16(msg, DPP_ATTR_WRAPPED_DATA);
	wpabuf_put_le16(msg, wpabuf_len(clear) + AES_BLOCK_SIZE);
	wrapped = wpabuf_put(msg, wpabuf_len(clear) + AES_BLOCK_SIZE);

	/* No AES-SIV AD */
	wpa_hexdump_buf(MSG_DEBUG, "DPP: AES-SIV cleartext", clear);
	if (aes_siv_encrypt(auth->ke, auth->curve->hash_len,
			    wpabuf_head(clear), wpabuf_len(clear),
			    0, NULL, NULL, wrapped) < 0)
		goto fail;
	wpa_hexdump(MSG_DEBUG, "DPP: AES-SIV ciphertext",
		    wrapped, wpabuf_len(clear) + AES_BLOCK_SIZE);

#ifdef CONFIG_TESTING_OPTIONS
	if (dpp_test == DPP_TEST_AFTER_WRAPPED_DATA_CONF_REQ) {
		wpa_printf(MSG_INFO, "DPP: TESTING - attr after Wrapped Data");
		dpp_build_attr_status(msg, DPP_STATUS_OK);
	}
skip_wrapped_data:
#endif /* CONFIG_TESTING_OPTIONS */

	wpa_hexdump_buf(MSG_DEBUG,
			"DPP: Configuration Request frame attributes", msg);
	wpabuf_free(clear);
	return msg;

fail:
	wpabuf_free(clear);
	wpabuf_free(msg);
	return NULL;
}


static void dpp_auth_success(struct dpp_authentication *auth)
{
	wpa_printf(MSG_DEBUG,
		   "DPP: Authentication success - clear temporary keys");
	os_memset(auth->Mx, 0, sizeof(auth->Mx));
	auth->Mx_len = 0;
	os_memset(auth->Nx, 0, sizeof(auth->Nx));
	auth->Nx_len = 0;
	os_memset(auth->Lx, 0, sizeof(auth->Lx));
	auth->Lx_len = 0;
	os_memset(auth->k1, 0, sizeof(auth->k1));
	os_memset(auth->k2, 0, sizeof(auth->k2));

	auth->auth_success = 1;
}


static int dpp_gen_r_auth(struct dpp_authentication *auth, u8 *r_auth)
{
	struct wpabuf *pix, *prx, *bix, *brx;
	const u8 *addr[7];
	size_t len[7];
	size_t i, num_elem = 0;
	size_t nonce_len;
	u8 zero = 0;
	int res = -1;

	/* R-auth = H(I-nonce | R-nonce | PI.x | PR.x | [BI.x |] BR.x | 0) */
	nonce_len = auth->curve->nonce_len;

	if (auth->initiator) {
		pix = dpp_get_pubkey_point(auth->own_protocol_key, 0);
		prx = dpp_get_pubkey_point(auth->peer_protocol_key, 0);
		if (auth->own_bi)
			bix = dpp_get_pubkey_point(auth->own_bi->pubkey, 0);
		else
			bix = NULL;
		brx = dpp_get_pubkey_point(auth->peer_bi->pubkey, 0);
	} else {
		pix = dpp_get_pubkey_point(auth->peer_protocol_key, 0);
		prx = dpp_get_pubkey_point(auth->own_protocol_key, 0);
		if (auth->peer_bi)
			bix = dpp_get_pubkey_point(auth->peer_bi->pubkey, 0);
		else
			bix = NULL;
		brx = dpp_get_pubkey_point(auth->own_bi->pubkey, 0);
	}
	if (!pix || !prx || !brx)
		goto fail;

	addr[num_elem] = auth->i_nonce;
	len[num_elem] = nonce_len;
	num_elem++;

	addr[num_elem] = auth->r_nonce;
	len[num_elem] = nonce_len;
	num_elem++;

	addr[num_elem] = wpabuf_head(pix);
	len[num_elem] = wpabuf_len(pix) / 2;
	num_elem++;

	addr[num_elem] = wpabuf_head(prx);
	len[num_elem] = wpabuf_len(prx) / 2;
	num_elem++;

	if (bix) {
		addr[num_elem] = wpabuf_head(bix);
		len[num_elem] = wpabuf_len(bix) / 2;
		num_elem++;
	}

	addr[num_elem] = wpabuf_head(brx);
	len[num_elem] = wpabuf_len(brx) / 2;
	num_elem++;

	addr[num_elem] = &zero;
	len[num_elem] = 1;
	num_elem++;

	wpa_printf(MSG_DEBUG, "DPP: R-auth hash components");
	for (i = 0; i < num_elem; i++)
		wpa_hexdump(MSG_DEBUG, "DPP: hash component", addr[i], len[i]);
	res = dpp_hash_vector(auth->curve, num_elem, addr, len, r_auth);
	if (res == 0)
		wpa_hexdump(MSG_DEBUG, "DPP: R-auth", r_auth,
			    auth->curve->hash_len);
fail:
	wpabuf_free(pix);
	wpabuf_free(prx);
	wpabuf_free(bix);
	wpabuf_free(brx);
	return res;
}


static int dpp_gen_i_auth(struct dpp_authentication *auth, u8 *i_auth)
{
	struct wpabuf *pix = NULL, *prx = NULL, *bix = NULL, *brx = NULL;
	const u8 *addr[7];
	size_t len[7];
	size_t i, num_elem = 0;
	size_t nonce_len;
	u8 one = 1;
	int res = -1;

	/* I-auth = H(R-nonce | I-nonce | PR.x | PI.x | BR.x | [BI.x |] 1) */
	nonce_len = auth->curve->nonce_len;

	if (auth->initiator) {
		pix = dpp_get_pubkey_point(auth->own_protocol_key, 0);
		prx = dpp_get_pubkey_point(auth->peer_protocol_key, 0);
		if (auth->own_bi)
			bix = dpp_get_pubkey_point(auth->own_bi->pubkey, 0);
		else
			bix = NULL;
		if (!auth->peer_bi)
			goto fail;
		brx = dpp_get_pubkey_point(auth->peer_bi->pubkey, 0);
	} else {
		pix = dpp_get_pubkey_point(auth->peer_protocol_key, 0);
		prx = dpp_get_pubkey_point(auth->own_protocol_key, 0);
		if (auth->peer_bi)
			bix = dpp_get_pubkey_point(auth->peer_bi->pubkey, 0);
		else
			bix = NULL;
		if (!auth->own_bi)
			goto fail;
		brx = dpp_get_pubkey_point(auth->own_bi->pubkey, 0);
	}
	if (!pix || !prx || !brx)
		goto fail;

	addr[num_elem] = auth->r_nonce;
	len[num_elem] = nonce_len;
	num_elem++;

	addr[num_elem] = auth->i_nonce;
	len[num_elem] = nonce_len;
	num_elem++;

	addr[num_elem] = wpabuf_head(prx);
	len[num_elem] = wpabuf_len(prx) / 2;
	num_elem++;

	addr[num_elem] = wpabuf_head(pix);
	len[num_elem] = wpabuf_len(pix) / 2;
	num_elem++;

	addr[num_elem] = wpabuf_head(brx);
	len[num_elem] = wpabuf_len(brx) / 2;
	num_elem++;

	if (bix) {
		addr[num_elem] = wpabuf_head(bix);
		len[num_elem] = wpabuf_len(bix) / 2;
		num_elem++;
	}

	addr[num_elem] = &one;
	len[num_elem] = 1;
	num_elem++;

	wpa_printf(MSG_DEBUG, "DPP: I-auth hash components");
	for (i = 0; i < num_elem; i++)
		wpa_hexdump(MSG_DEBUG, "DPP: hash component", addr[i], len[i]);
	res = dpp_hash_vector(auth->curve, num_elem, addr, len, i_auth);
	if (res == 0)
		wpa_hexdump(MSG_DEBUG, "DPP: I-auth", i_auth,
			    auth->curve->hash_len);
fail:
	wpabuf_free(pix);
	wpabuf_free(prx);
	wpabuf_free(bix);
	wpabuf_free(brx);
	return res;
}


static int dpp_auth_derive_l_responder(struct dpp_authentication *auth)
{
	const EC_GROUP *group;
	EC_POINT *l = NULL;
	EC_KEY *BI = NULL, *bR = NULL, *pR = NULL;
	const EC_POINT *BI_point;
	BN_CTX *bnctx;
	BIGNUM *lx, *sum, *q;
	const BIGNUM *bR_bn, *pR_bn;
	int ret = -1;

	/* L = ((bR + pR) modulo q) * BI */

	bnctx = BN_CTX_new();
	sum = BN_new();
	q = BN_new();
	lx = BN_new();
	if (!bnctx || !sum || !q || !lx)
		goto fail;
	BI = EVP_PKEY_get1_EC_KEY(auth->peer_bi->pubkey);
	if (!BI)
		goto fail;
	BI_point = EC_KEY_get0_public_key(BI);
	group = EC_KEY_get0_group(BI);
	if (!group)
		goto fail;

	bR = EVP_PKEY_get1_EC_KEY(auth->own_bi->pubkey);
	pR = EVP_PKEY_get1_EC_KEY(auth->own_protocol_key);
	if (!bR || !pR)
		goto fail;
	bR_bn = EC_KEY_get0_private_key(bR);
	pR_bn = EC_KEY_get0_private_key(pR);
	if (!bR_bn || !pR_bn)
		goto fail;
	if (EC_GROUP_get_order(group, q, bnctx) != 1 ||
	    BN_mod_add(sum, bR_bn, pR_bn, q, bnctx) != 1)
		goto fail;
	l = EC_POINT_new(group);
	if (!l ||
	    EC_POINT_mul(group, l, NULL, BI_point, sum, bnctx) != 1 ||
	    EC_POINT_get_affine_coordinates_GFp(group, l, lx, NULL,
						bnctx) != 1) {
		wpa_printf(MSG_ERROR,
			   "OpenSSL: failed: %s",
			   ERR_error_string(ERR_get_error(), NULL));
		goto fail;
	}

	if (dpp_bn2bin_pad(lx, auth->Lx, auth->secret_len) < 0)
		goto fail;
	wpa_hexdump_key(MSG_DEBUG, "DPP: L.x", auth->Lx, auth->secret_len);
	auth->Lx_len = auth->secret_len;
	ret = 0;
fail:
	EC_POINT_clear_free(l);
	EC_KEY_free(BI);
	EC_KEY_free(bR);
	EC_KEY_free(pR);
	BN_clear_free(lx);
	BN_clear_free(sum);
	BN_free(q);
	BN_CTX_free(bnctx);
	return ret;
}


static int dpp_auth_derive_l_initiator(struct dpp_authentication *auth)
{
	const EC_GROUP *group;
	EC_POINT *l = NULL, *sum = NULL;
	EC_KEY *bI = NULL, *BR = NULL, *PR = NULL;
	const EC_POINT *BR_point, *PR_point;
	BN_CTX *bnctx;
	BIGNUM *lx;
	const BIGNUM *bI_bn;
	int ret = -1;

	/* L = bI * (BR + PR) */

	bnctx = BN_CTX_new();
	lx = BN_new();
	if (!bnctx || !lx)
		goto fail;
	BR = EVP_PKEY_get1_EC_KEY(auth->peer_bi->pubkey);
	PR = EVP_PKEY_get1_EC_KEY(auth->peer_protocol_key);
	if (!BR || !PR)
		goto fail;
	BR_point = EC_KEY_get0_public_key(BR);
	PR_point = EC_KEY_get0_public_key(PR);

	bI = EVP_PKEY_get1_EC_KEY(auth->own_bi->pubkey);
	if (!bI)
		goto fail;
	group = EC_KEY_get0_group(bI);
	bI_bn = EC_KEY_get0_private_key(bI);
	if (!group || !bI_bn)
		goto fail;
	sum = EC_POINT_new(group);
	l = EC_POINT_new(group);
	if (!sum || !l ||
	    EC_POINT_add(group, sum, BR_point, PR_point, bnctx) != 1 ||
	    EC_POINT_mul(group, l, NULL, sum, bI_bn, bnctx) != 1 ||
	    EC_POINT_get_affine_coordinates_GFp(group, l, lx, NULL,
						bnctx) != 1) {
		wpa_printf(MSG_ERROR,
			   "OpenSSL: failed: %s",
			   ERR_error_string(ERR_get_error(), NULL));
		goto fail;
	}

	if (dpp_bn2bin_pad(lx, auth->Lx, auth->secret_len) < 0)
		goto fail;
	wpa_hexdump_key(MSG_DEBUG, "DPP: L.x", auth->Lx, auth->secret_len);
	auth->Lx_len = auth->secret_len;
	ret = 0;
fail:
	EC_POINT_clear_free(l);
	EC_POINT_clear_free(sum);
	EC_KEY_free(bI);
	EC_KEY_free(BR);
	EC_KEY_free(PR);
	BN_clear_free(lx);
	BN_CTX_free(bnctx);
	return ret;
}


static int dpp_auth_build_resp_ok(struct dpp_authentication *auth)
{
	size_t nonce_len;
	EVP_PKEY_CTX *ctx = NULL;
	size_t secret_len;
	struct wpabuf *msg, *pr = NULL;
	u8 r_auth[4 + DPP_MAX_HASH_LEN];
	u8 wrapped_r_auth[4 + DPP_MAX_HASH_LEN + AES_BLOCK_SIZE], *w_r_auth;
	size_t wrapped_r_auth_len;
	int ret = -1;
	const u8 *r_pubkey_hash, *i_pubkey_hash, *r_nonce, *i_nonce;
	enum dpp_status_error status = DPP_STATUS_OK;
#ifdef CONFIG_TESTING_OPTIONS
	u8 test_hash[SHA256_MAC_LEN];
#endif /* CONFIG_TESTING_OPTIONS */

	wpa_printf(MSG_DEBUG, "DPP: Build Authentication Response");
	if (!auth->own_bi)
		return -1;

#ifdef CONFIG_TESTING_OPTIONS
	if (dpp_nonce_override_len > 0) {
		wpa_printf(MSG_INFO, "DPP: TESTING - override R-nonce");
		nonce_len = dpp_nonce_override_len;
		os_memcpy(auth->r_nonce, dpp_nonce_override, nonce_len);
	} else {
		nonce_len = auth->curve->nonce_len;
		if (random_get_bytes(auth->r_nonce, nonce_len)) {
			wpa_printf(MSG_ERROR,
				   "DPP: Failed to generate R-nonce");
			goto fail;
		}
	}
#else /* CONFIG_TESTING_OPTIONS */
	nonce_len = auth->curve->nonce_len;
	if (random_get_bytes(auth->r_nonce, nonce_len)) {
		wpa_printf(MSG_ERROR, "DPP: Failed to generate R-nonce");
		goto fail;
	}
#endif /* CONFIG_TESTING_OPTIONS */
	wpa_hexdump(MSG_DEBUG, "DPP: R-nonce", auth->r_nonce, nonce_len);

#ifdef CONFIG_TESTING_OPTIONS
	if (dpp_protocol_key_override_len) {
		const struct dpp_curve_params *tmp_curve;

		wpa_printf(MSG_INFO,
			   "DPP: TESTING - override protocol key");
		auth->own_protocol_key = dpp_set_keypair(
			&tmp_curve, dpp_protocol_key_override,
			dpp_protocol_key_override_len);
	} else {
		auth->own_protocol_key = dpp_gen_keypair(auth->curve);
	}
#else /* CONFIG_TESTING_OPTIONS */
	auth->own_protocol_key = dpp_gen_keypair(auth->curve);
#endif /* CONFIG_TESTING_OPTIONS */
	if (!auth->own_protocol_key)
		goto fail;

	pr = dpp_get_pubkey_point(auth->own_protocol_key, 0);
	if (!pr)
		goto fail;

	/* ECDH: N = pR * PI */
	ctx = EVP_PKEY_CTX_new(auth->own_protocol_key, NULL);
	if (!ctx ||
	    EVP_PKEY_derive_init(ctx) != 1 ||
	    EVP_PKEY_derive_set_peer(ctx, auth->peer_protocol_key) != 1 ||
	    EVP_PKEY_derive(ctx, NULL, &secret_len) != 1 ||
	    secret_len > DPP_MAX_SHARED_SECRET_LEN ||
	    EVP_PKEY_derive(ctx, auth->Nx, &secret_len) != 1) {
		wpa_printf(MSG_ERROR,
			   "DPP: Failed to derive ECDH shared secret: %s",
			   ERR_error_string(ERR_get_error(), NULL));
		goto fail;
	}
	EVP_PKEY_CTX_free(ctx);
	ctx = NULL;

	wpa_hexdump_key(MSG_DEBUG, "DPP: ECDH shared secret (N.x)",
			auth->Nx, auth->secret_len);
	auth->Nx_len = auth->secret_len;

	if (dpp_derive_k2(auth->Nx, auth->secret_len, auth->k2,
			  auth->curve->hash_len) < 0)
		goto fail;

	if (auth->own_bi && auth->peer_bi) {
		/* Mutual authentication */
		if (dpp_auth_derive_l_responder(auth) < 0)
			goto fail;
	}

	if (dpp_derive_ke(auth, auth->ke, auth->curve->hash_len) < 0)
		goto fail;

	/* R-auth = H(I-nonce | R-nonce | PI.x | PR.x | [BI.x |] BR.x | 0) */
	WPA_PUT_LE16(r_auth, DPP_ATTR_R_AUTH_TAG);
	WPA_PUT_LE16(&r_auth[2], auth->curve->hash_len);
	if (dpp_gen_r_auth(auth, r_auth + 4) < 0)
		goto fail;
#ifdef CONFIG_TESTING_OPTIONS
	if (dpp_test == DPP_TEST_R_AUTH_MISMATCH_AUTH_RESP) {
		wpa_printf(MSG_INFO, "DPP: TESTING - R-auth mismatch");
		r_auth[4 + auth->curve->hash_len / 2] ^= 0x01;
	}
#endif /* CONFIG_TESTING_OPTIONS */
	if (aes_siv_encrypt(auth->ke, auth->curve->hash_len,
			    r_auth, 4 + auth->curve->hash_len,
			    0, NULL, NULL, wrapped_r_auth) < 0)
		goto fail;
	wrapped_r_auth_len = 4 + auth->curve->hash_len + AES_BLOCK_SIZE;
	wpa_hexdump(MSG_DEBUG, "DPP: {R-auth}ke",
		    wrapped_r_auth, wrapped_r_auth_len);
	w_r_auth = wrapped_r_auth;

	r_pubkey_hash = auth->own_bi->pubkey_hash;
	if (auth->peer_bi)
		i_pubkey_hash = auth->peer_bi->pubkey_hash;
	else
		i_pubkey_hash = NULL;

	i_nonce = auth->i_nonce;
	r_nonce = auth->r_nonce;

#ifdef CONFIG_TESTING_OPTIONS
	if (dpp_test == DPP_TEST_NO_R_BOOTSTRAP_KEY_HASH_AUTH_RESP) {
		wpa_printf(MSG_INFO, "DPP: TESTING - no R-Bootstrap Key Hash");
		r_pubkey_hash = NULL;
	} else if (dpp_test ==
		   DPP_TEST_INVALID_R_BOOTSTRAP_KEY_HASH_AUTH_RESP) {
		wpa_printf(MSG_INFO,
			   "DPP: TESTING - invalid R-Bootstrap Key Hash");
		os_memcpy(test_hash, r_pubkey_hash, SHA256_MAC_LEN);
		test_hash[SHA256_MAC_LEN - 1] ^= 0x01;
		r_pubkey_hash = test_hash;
	} else if (dpp_test == DPP_TEST_NO_I_BOOTSTRAP_KEY_HASH_AUTH_RESP) {
		wpa_printf(MSG_INFO, "DPP: TESTING - no I-Bootstrap Key Hash");
		i_pubkey_hash = NULL;
	} else if (dpp_test ==
		   DPP_TEST_INVALID_I_BOOTSTRAP_KEY_HASH_AUTH_RESP) {
		wpa_printf(MSG_INFO,
			   "DPP: TESTING - invalid I-Bootstrap Key Hash");
		if (i_pubkey_hash)
			os_memcpy(test_hash, i_pubkey_hash, SHA256_MAC_LEN);
		else
			os_memset(test_hash, 0, SHA256_MAC_LEN);
		test_hash[SHA256_MAC_LEN - 1] ^= 0x01;
		i_pubkey_hash = test_hash;
	} else if (dpp_test == DPP_TEST_NO_R_PROTO_KEY_AUTH_RESP) {
		wpa_printf(MSG_INFO, "DPP: TESTING - no R-Proto Key");
		wpabuf_free(pr);
		pr = NULL;
	} else if (dpp_test == DPP_TEST_INVALID_R_PROTO_KEY_AUTH_RESP) {
		wpa_printf(MSG_INFO, "DPP: TESTING - invalid R-Proto Key");
		wpabuf_free(pr);
		pr = wpabuf_alloc(2 * auth->curve->prime_len);
		if (!pr || dpp_test_gen_invalid_key(pr, auth->curve) < 0)
			goto fail;
	} else if (dpp_test == DPP_TEST_NO_R_AUTH_AUTH_RESP) {
		wpa_printf(MSG_INFO, "DPP: TESTING - no R-Auth");
		w_r_auth = NULL;
		wrapped_r_auth_len = 0;
	} else if (dpp_test == DPP_TEST_NO_STATUS_AUTH_RESP) {
		wpa_printf(MSG_INFO, "DPP: TESTING - no Status");
		status = 255;
	} else if (dpp_test == DPP_TEST_INVALID_STATUS_AUTH_RESP) {
		wpa_printf(MSG_INFO, "DPP: TESTING - invalid Status");
		status = 254;
	} else if (dpp_test == DPP_TEST_NO_R_NONCE_AUTH_RESP) {
		wpa_printf(MSG_INFO, "DPP: TESTING - no R-nonce");
		r_nonce = NULL;
	} else if (dpp_test == DPP_TEST_NO_I_NONCE_AUTH_RESP) {
		wpa_printf(MSG_INFO, "DPP: TESTING - no I-nonce");
		i_nonce = NULL;
	}
#endif /* CONFIG_TESTING_OPTIONS */

	msg = dpp_auth_build_resp(auth, status, pr, nonce_len,
				  r_pubkey_hash, i_pubkey_hash,
				  r_nonce, i_nonce,
				  w_r_auth, wrapped_r_auth_len,
				  auth->k2);
	if (!msg)
		goto fail;
	wpabuf_free(auth->resp_msg);
	auth->resp_msg = msg;
	ret = 0;
fail:
	wpabuf_free(pr);
	return ret;
}


static int dpp_auth_build_resp_status(struct dpp_authentication *auth,
				      enum dpp_status_error status)
{
	struct wpabuf *msg;
	const u8 *r_pubkey_hash, *i_pubkey_hash, *i_nonce;
#ifdef CONFIG_TESTING_OPTIONS
	u8 test_hash[SHA256_MAC_LEN];
#endif /* CONFIG_TESTING_OPTIONS */

	if (!auth->own_bi)
		return -1;
	wpa_printf(MSG_DEBUG, "DPP: Build Authentication Response");

	r_pubkey_hash = auth->own_bi->pubkey_hash;
	if (auth->peer_bi)
		i_pubkey_hash = auth->peer_bi->pubkey_hash;
	else
		i_pubkey_hash = NULL;

	i_nonce = auth->i_nonce;

#ifdef CONFIG_TESTING_OPTIONS
	if (dpp_test == DPP_TEST_NO_R_BOOTSTRAP_KEY_HASH_AUTH_RESP) {
		wpa_printf(MSG_INFO, "DPP: TESTING - no R-Bootstrap Key Hash");
		r_pubkey_hash = NULL;
	} else if (dpp_test ==
		   DPP_TEST_INVALID_R_BOOTSTRAP_KEY_HASH_AUTH_RESP) {
		wpa_printf(MSG_INFO,
			   "DPP: TESTING - invalid R-Bootstrap Key Hash");
		os_memcpy(test_hash, r_pubkey_hash, SHA256_MAC_LEN);
		test_hash[SHA256_MAC_LEN - 1] ^= 0x01;
		r_pubkey_hash = test_hash;
	} else if (dpp_test == DPP_TEST_NO_I_BOOTSTRAP_KEY_HASH_AUTH_RESP) {
		wpa_printf(MSG_INFO, "DPP: TESTING - no I-Bootstrap Key Hash");
		i_pubkey_hash = NULL;
	} else if (dpp_test ==
		   DPP_TEST_INVALID_I_BOOTSTRAP_KEY_HASH_AUTH_RESP) {
		wpa_printf(MSG_INFO,
			   "DPP: TESTING - invalid I-Bootstrap Key Hash");
		if (i_pubkey_hash)
			os_memcpy(test_hash, i_pubkey_hash, SHA256_MAC_LEN);
		else
			os_memset(test_hash, 0, SHA256_MAC_LEN);
		test_hash[SHA256_MAC_LEN - 1] ^= 0x01;
		i_pubkey_hash = test_hash;
	} else if (dpp_test == DPP_TEST_NO_STATUS_AUTH_RESP) {
		wpa_printf(MSG_INFO, "DPP: TESTING - no Status");
		status = 255;
	} else if (dpp_test == DPP_TEST_NO_I_NONCE_AUTH_RESP) {
		wpa_printf(MSG_INFO, "DPP: TESTING - no I-nonce");
		i_nonce = NULL;
	}
#endif /* CONFIG_TESTING_OPTIONS */

	msg = dpp_auth_build_resp(auth, status, NULL, auth->curve->nonce_len,
				  r_pubkey_hash, i_pubkey_hash,
				  NULL, i_nonce, NULL, 0, auth->k1);
	if (!msg)
		return -1;
	wpabuf_free(auth->resp_msg);
	auth->resp_msg = msg;
	return 0;
}


struct dpp_authentication *
dpp_auth_req_rx(void *msg_ctx, u8 dpp_allowed_roles, int qr_mutual,
		struct dpp_bootstrap_info *peer_bi,
		struct dpp_bootstrap_info *own_bi,
		unsigned int freq, const u8 *hdr, const u8 *attr_start,
		size_t attr_len)
{
	EVP_PKEY *pi = NULL;
	EVP_PKEY_CTX *ctx = NULL;
	size_t secret_len;
	const u8 *addr[2];
	size_t len[2];
	u8 *unwrapped = NULL;
	size_t unwrapped_len = 0;
	const u8 *wrapped_data, *i_proto, *i_nonce, *i_capab, *i_bootstrap,
		*channel;
	u16 wrapped_data_len, i_proto_len, i_nonce_len, i_capab_len,
		i_bootstrap_len, channel_len;
	struct dpp_authentication *auth = NULL;

#ifdef CONFIG_TESTING_OPTIONS
	if (dpp_test == DPP_TEST_STOP_AT_AUTH_REQ) {
		wpa_printf(MSG_INFO,
			   "DPP: TESTING - stop at Authentication Request");
		return NULL;
	}
#endif /* CONFIG_TESTING_OPTIONS */

	wrapped_data = dpp_get_attr(attr_start, attr_len, DPP_ATTR_WRAPPED_DATA,
				    &wrapped_data_len);
	if (!wrapped_data || wrapped_data_len < AES_BLOCK_SIZE) {
		wpa_msg(msg_ctx, MSG_INFO, DPP_EVENT_FAIL
			"Missing or invalid required Wrapped Data attribute");
		return NULL;
	}
	wpa_hexdump(MSG_MSGDUMP, "DPP: Wrapped Data",
		    wrapped_data, wrapped_data_len);
	attr_len = wrapped_data - 4 - attr_start;

	auth = os_zalloc(sizeof(*auth));
	if (!auth)
		goto fail;
	auth->msg_ctx = msg_ctx;
	auth->peer_bi = peer_bi;
	auth->own_bi = own_bi;
	auth->curve = own_bi->curve;
	auth->curr_freq = freq;

	channel = dpp_get_attr(attr_start, attr_len, DPP_ATTR_CHANNEL,
			       &channel_len);
	if (channel) {
		int neg_freq;

		if (channel_len < 2) {
			dpp_auth_fail(auth, "Too short Channel attribute");
			goto fail;
		}

		neg_freq = ieee80211_chan_to_freq(NULL, channel[0], channel[1]);
		wpa_printf(MSG_DEBUG,
			   "DPP: Initiator requested different channel for negotiation: op_class=%u channel=%u --> freq=%d",
			   channel[0], channel[1], neg_freq);
		if (neg_freq < 0) {
			dpp_auth_fail(auth,
				      "Unsupported Channel attribute value");
			goto fail;
		}

		if (auth->curr_freq != (unsigned int) neg_freq) {
			wpa_printf(MSG_DEBUG,
				   "DPP: Changing negotiation channel from %u MHz to %u MHz",
				   freq, neg_freq);
			auth->curr_freq = neg_freq;
		}
	}

	i_proto = dpp_get_attr(attr_start, attr_len, DPP_ATTR_I_PROTOCOL_KEY,
			       &i_proto_len);
	if (!i_proto) {
		dpp_auth_fail(auth,
			      "Missing required Initiator Protocol Key attribute");
		goto fail;
	}
	wpa_hexdump(MSG_MSGDUMP, "DPP: Initiator Protocol Key",
		    i_proto, i_proto_len);

	/* M = bR * PI */
	pi = dpp_set_pubkey_point(own_bi->pubkey, i_proto, i_proto_len);
	if (!pi) {
		dpp_auth_fail(auth, "Invalid Initiator Protocol Key");
		goto fail;
	}
	dpp_debug_print_key("Peer (Initiator) Protocol Key", pi);

	ctx = EVP_PKEY_CTX_new(own_bi->pubkey, NULL);
	if (!ctx ||
	    EVP_PKEY_derive_init(ctx) != 1 ||
	    EVP_PKEY_derive_set_peer(ctx, pi) != 1 ||
	    EVP_PKEY_derive(ctx, NULL, &secret_len) != 1 ||
	    secret_len > DPP_MAX_SHARED_SECRET_LEN ||
	    EVP_PKEY_derive(ctx, auth->Mx, &secret_len) != 1) {
		wpa_printf(MSG_ERROR,
			   "DPP: Failed to derive ECDH shared secret: %s",
			   ERR_error_string(ERR_get_error(), NULL));
		dpp_auth_fail(auth, "Failed to derive ECDH shared secret");
		goto fail;
	}
	auth->secret_len = secret_len;
	EVP_PKEY_CTX_free(ctx);
	ctx = NULL;

	wpa_hexdump_key(MSG_DEBUG, "DPP: ECDH shared secret (M.x)",
			auth->Mx, auth->secret_len);
	auth->Mx_len = auth->secret_len;

	if (dpp_derive_k1(auth->Mx, auth->secret_len, auth->k1,
			  auth->curve->hash_len) < 0)
		goto fail;

	addr[0] = hdr;
	len[0] = DPP_HDR_LEN;
	addr[1] = attr_start;
	len[1] = attr_len;
	wpa_hexdump(MSG_DEBUG, "DDP: AES-SIV AD[0]", addr[0], len[0]);
	wpa_hexdump(MSG_DEBUG, "DDP: AES-SIV AD[1]", addr[1], len[1]);
	wpa_hexdump(MSG_DEBUG, "DPP: AES-SIV ciphertext",
		    wrapped_data, wrapped_data_len);
	unwrapped_len = wrapped_data_len - AES_BLOCK_SIZE;
	unwrapped = os_malloc(unwrapped_len);
	if (!unwrapped)
		goto fail;
	if (aes_siv_decrypt(auth->k1, auth->curve->hash_len,
			    wrapped_data, wrapped_data_len,
			    2, addr, len, unwrapped) < 0) {
		dpp_auth_fail(auth, "AES-SIV decryption failed");
		goto fail;
	}
	wpa_hexdump(MSG_DEBUG, "DPP: AES-SIV cleartext",
		    unwrapped, unwrapped_len);

	if (dpp_check_attrs(unwrapped, unwrapped_len) < 0) {
		dpp_auth_fail(auth, "Invalid attribute in unwrapped data");
		goto fail;
	}

	i_nonce = dpp_get_attr(unwrapped, unwrapped_len, DPP_ATTR_I_NONCE,
			       &i_nonce_len);
	if (!i_nonce || i_nonce_len != auth->curve->nonce_len) {
		dpp_auth_fail(auth, "Missing or invalid I-nonce");
		goto fail;
	}
	wpa_hexdump(MSG_DEBUG, "DPP: I-nonce", i_nonce, i_nonce_len);
	os_memcpy(auth->i_nonce, i_nonce, i_nonce_len);

	i_capab = dpp_get_attr(unwrapped, unwrapped_len,
			       DPP_ATTR_I_CAPABILITIES,
			       &i_capab_len);
	if (!i_capab || i_capab_len < 1) {
		dpp_auth_fail(auth, "Missing or invalid I-capabilities");
		goto fail;
	}
	auth->i_capab = i_capab[0];
	wpa_printf(MSG_DEBUG, "DPP: I-capabilities: 0x%02x", auth->i_capab);

	bin_clear_free(unwrapped, unwrapped_len);
	unwrapped = NULL;

	switch (auth->i_capab & DPP_CAPAB_ROLE_MASK) {
	case DPP_CAPAB_ENROLLEE:
		if (!(dpp_allowed_roles & DPP_CAPAB_CONFIGURATOR)) {
			wpa_printf(MSG_DEBUG,
				   "DPP: Local policy does not allow Configurator role");
			goto not_compatible;
		}
		wpa_printf(MSG_DEBUG, "DPP: Acting as Configurator");
		auth->configurator = 1;
		break;
	case DPP_CAPAB_CONFIGURATOR:
		if (!(dpp_allowed_roles & DPP_CAPAB_ENROLLEE)) {
			wpa_printf(MSG_DEBUG,
				   "DPP: Local policy does not allow Enrollee role");
			goto not_compatible;
		}
		wpa_printf(MSG_DEBUG, "DPP: Acting as Enrollee");
		auth->configurator = 0;
		break;
	case DPP_CAPAB_CONFIGURATOR | DPP_CAPAB_ENROLLEE:
		if (dpp_allowed_roles & DPP_CAPAB_ENROLLEE) {
			wpa_printf(MSG_DEBUG, "DPP: Acting as Enrollee");
			auth->configurator = 0;
		} else if (dpp_allowed_roles & DPP_CAPAB_CONFIGURATOR) {
			wpa_printf(MSG_DEBUG, "DPP: Acting as Configurator");
			auth->configurator = 1;
		} else {
			wpa_printf(MSG_DEBUG,
				   "DPP: Local policy does not allow Configurator/Enrollee role");
			goto not_compatible;
		}
		break;
	default:
		wpa_printf(MSG_DEBUG, "DPP: Unexpected role in I-capabilities");
		wpa_msg(auth->msg_ctx, MSG_INFO,
			DPP_EVENT_FAIL "Invalid role in I-capabilities 0x%02x",
			auth->i_capab & DPP_CAPAB_ROLE_MASK);
		goto fail;
	}

	auth->peer_protocol_key = pi;
	pi = NULL;
	if (qr_mutual && !peer_bi && own_bi->type == DPP_BOOTSTRAP_QR_CODE) {
		char hex[SHA256_MAC_LEN * 2 + 1];

		wpa_printf(MSG_DEBUG,
			   "DPP: Mutual authentication required with QR Codes, but peer info is not yet available - request more time");
		if (dpp_auth_build_resp_status(auth,
					       DPP_STATUS_RESPONSE_PENDING) < 0)
			goto fail;
		i_bootstrap = dpp_get_attr(attr_start, attr_len,
					   DPP_ATTR_I_BOOTSTRAP_KEY_HASH,
					   &i_bootstrap_len);
		if (i_bootstrap && i_bootstrap_len == SHA256_MAC_LEN) {
			auth->response_pending = 1;
			os_memcpy(auth->waiting_pubkey_hash,
				  i_bootstrap, i_bootstrap_len);
			wpa_snprintf_hex(hex, sizeof(hex), i_bootstrap,
					 i_bootstrap_len);
		} else {
			hex[0] = '\0';
		}

		wpa_msg(auth->msg_ctx, MSG_INFO, DPP_EVENT_SCAN_PEER_QR_CODE
			"%s", hex);
		return auth;
	}
	if (dpp_auth_build_resp_ok(auth) < 0)
		goto fail;

	return auth;

not_compatible:
	wpa_msg(auth->msg_ctx, MSG_INFO, DPP_EVENT_NOT_COMPATIBLE
		"i-capab=0x%02x", auth->i_capab);
	if (dpp_allowed_roles & DPP_CAPAB_CONFIGURATOR)
		auth->configurator = 1;
	else
		auth->configurator = 0;
	auth->peer_protocol_key = pi;
	pi = NULL;
	if (dpp_auth_build_resp_status(auth, DPP_STATUS_NOT_COMPATIBLE) < 0)
		goto fail;

	auth->remove_on_tx_status = 1;
	return auth;
fail:
	bin_clear_free(unwrapped, unwrapped_len);
	EVP_PKEY_free(pi);
	EVP_PKEY_CTX_free(ctx);
	dpp_auth_deinit(auth);
	return NULL;
}


int dpp_notify_new_qr_code(struct dpp_authentication *auth,
			   struct dpp_bootstrap_info *peer_bi)
{
	if (!auth || !auth->response_pending ||
	    os_memcmp(auth->waiting_pubkey_hash, peer_bi->pubkey_hash,
		      SHA256_MAC_LEN) != 0)
		return 0;

	wpa_printf(MSG_DEBUG,
		   "DPP: New scanned QR Code has matching public key that was needed to continue DPP Authentication exchange with "
		   MACSTR, MAC2STR(auth->peer_mac_addr));
	auth->peer_bi = peer_bi;

	if (dpp_auth_build_resp_ok(auth) < 0)
		return -1;

	return 1;
}


static struct wpabuf * dpp_auth_build_conf(struct dpp_authentication *auth,
					   enum dpp_status_error status)
{
	struct wpabuf *msg;
	u8 i_auth[4 + DPP_MAX_HASH_LEN];
	size_t i_auth_len;
	u8 r_nonce[4 + DPP_MAX_NONCE_LEN];
	size_t r_nonce_len;
	const u8 *addr[2];
	size_t len[2], attr_len;
	u8 *wrapped_i_auth;
	u8 *wrapped_r_nonce;
	u8 *attr_start, *attr_end;
	const u8 *r_pubkey_hash, *i_pubkey_hash;
#ifdef CONFIG_TESTING_OPTIONS
	u8 test_hash[SHA256_MAC_LEN];
#endif /* CONFIG_TESTING_OPTIONS */

	wpa_printf(MSG_DEBUG, "DPP: Build Authentication Confirmation");

	i_auth_len = 4 + auth->curve->hash_len;
	r_nonce_len = 4 + auth->curve->nonce_len;
	/* Build DPP Authentication Confirmation frame attributes */
	attr_len = 4 + 1 + 2 * (4 + SHA256_MAC_LEN) +
		4 + i_auth_len + r_nonce_len + AES_BLOCK_SIZE;
#ifdef CONFIG_TESTING_OPTIONS
	if (dpp_test == DPP_TEST_AFTER_WRAPPED_DATA_AUTH_CONF)
		attr_len += 5;
#endif /* CONFIG_TESTING_OPTIONS */
	msg = dpp_alloc_msg(DPP_PA_AUTHENTICATION_CONF, attr_len);
	if (!msg)
		goto fail;

	attr_start = wpabuf_put(msg, 0);

	r_pubkey_hash = auth->peer_bi->pubkey_hash;
	if (auth->own_bi)
		i_pubkey_hash = auth->own_bi->pubkey_hash;
	else
		i_pubkey_hash = NULL;

#ifdef CONFIG_TESTING_OPTIONS
	if (dpp_test == DPP_TEST_NO_STATUS_AUTH_CONF) {
		wpa_printf(MSG_INFO, "DPP: TESTING - no Status");
		goto skip_status;
	} else if (dpp_test == DPP_TEST_INVALID_STATUS_AUTH_CONF) {
		wpa_printf(MSG_INFO, "DPP: TESTING - invalid Status");
		status = 254;
	}
#endif /* CONFIG_TESTING_OPTIONS */

	/* DPP Status */
	dpp_build_attr_status(msg, status);

#ifdef CONFIG_TESTING_OPTIONS
skip_status:
	if (dpp_test == DPP_TEST_NO_R_BOOTSTRAP_KEY_HASH_AUTH_CONF) {
		wpa_printf(MSG_INFO, "DPP: TESTING - no R-Bootstrap Key Hash");
		r_pubkey_hash = NULL;
	} else if (dpp_test ==
		   DPP_TEST_INVALID_R_BOOTSTRAP_KEY_HASH_AUTH_CONF) {
		wpa_printf(MSG_INFO,
			   "DPP: TESTING - invalid R-Bootstrap Key Hash");
		os_memcpy(test_hash, r_pubkey_hash, SHA256_MAC_LEN);
		test_hash[SHA256_MAC_LEN - 1] ^= 0x01;
		r_pubkey_hash = test_hash;
	} else if (dpp_test == DPP_TEST_NO_I_BOOTSTRAP_KEY_HASH_AUTH_CONF) {
		wpa_printf(MSG_INFO, "DPP: TESTING - no I-Bootstrap Key Hash");
		i_pubkey_hash = NULL;
	} else if (dpp_test ==
		   DPP_TEST_INVALID_I_BOOTSTRAP_KEY_HASH_AUTH_CONF) {
		wpa_printf(MSG_INFO,
			   "DPP: TESTING - invalid I-Bootstrap Key Hash");
		if (i_pubkey_hash)
			os_memcpy(test_hash, i_pubkey_hash, SHA256_MAC_LEN);
		else
			os_memset(test_hash, 0, SHA256_MAC_LEN);
		test_hash[SHA256_MAC_LEN - 1] ^= 0x01;
		i_pubkey_hash = test_hash;
	}
#endif /* CONFIG_TESTING_OPTIONS */

	/* Responder Bootstrapping Key Hash */
	dpp_build_attr_r_bootstrap_key_hash(msg, r_pubkey_hash);

	/* Initiator Bootstrapping Key Hash (mutual authentication) */
	dpp_build_attr_i_bootstrap_key_hash(msg, i_pubkey_hash);

#ifdef CONFIG_TESTING_OPTIONS
	if (dpp_test == DPP_TEST_NO_WRAPPED_DATA_AUTH_CONF)
		goto skip_wrapped_data;
	if (dpp_test == DPP_TEST_NO_I_AUTH_AUTH_CONF)
		i_auth_len = 0;
#endif /* CONFIG_TESTING_OPTIONS */

	attr_end = wpabuf_put(msg, 0);

	/* OUI, OUI type, Crypto Suite, DPP frame type */
	addr[0] = wpabuf_head_u8(msg) + 2;
	len[0] = 3 + 1 + 1 + 1;
	wpa_hexdump(MSG_DEBUG, "DDP: AES-SIV AD[0]", addr[0], len[0]);

	/* Attributes before Wrapped Data */
	addr[1] = attr_start;
	len[1] = attr_end - attr_start;
	wpa_hexdump(MSG_DEBUG, "DDP: AES-SIV AD[1]", addr[1], len[1]);

	if (status == DPP_STATUS_OK) {
		/* I-auth wrapped with ke */
		wpabuf_put_le16(msg, DPP_ATTR_WRAPPED_DATA);
		wpabuf_put_le16(msg, i_auth_len + AES_BLOCK_SIZE);
		wrapped_i_auth = wpabuf_put(msg, i_auth_len + AES_BLOCK_SIZE);

#ifdef CONFIG_TESTING_OPTIONS
		if (dpp_test == DPP_TEST_NO_I_AUTH_AUTH_CONF)
			goto skip_i_auth;
#endif /* CONFIG_TESTING_OPTIONS */

		/* I-auth = H(R-nonce | I-nonce | PR.x | PI.x | BR.x | [BI.x |]
		 *	      1) */
		WPA_PUT_LE16(i_auth, DPP_ATTR_I_AUTH_TAG);
		WPA_PUT_LE16(&i_auth[2], auth->curve->hash_len);
		if (dpp_gen_i_auth(auth, i_auth + 4) < 0)
			goto fail;

#ifdef CONFIG_TESTING_OPTIONS
		if (dpp_test == DPP_TEST_I_AUTH_MISMATCH_AUTH_CONF) {
			wpa_printf(MSG_INFO, "DPP: TESTING - I-auth mismatch");
			i_auth[4 + auth->curve->hash_len / 2] ^= 0x01;
		}
skip_i_auth:
#endif /* CONFIG_TESTING_OPTIONS */
		if (aes_siv_encrypt(auth->ke, auth->curve->hash_len,
				    i_auth, i_auth_len,
				    2, addr, len, wrapped_i_auth) < 0)
			goto fail;
		wpa_hexdump(MSG_DEBUG, "DPP: {I-auth}ke",
			    wrapped_i_auth, i_auth_len + AES_BLOCK_SIZE);
	} else {
		/* R-nonce wrapped with k2 */
		wpabuf_put_le16(msg, DPP_ATTR_WRAPPED_DATA);
		wpabuf_put_le16(msg, r_nonce_len + AES_BLOCK_SIZE);
		wrapped_r_nonce = wpabuf_put(msg, r_nonce_len + AES_BLOCK_SIZE);

		WPA_PUT_LE16(r_nonce, DPP_ATTR_R_NONCE);
		WPA_PUT_LE16(&r_nonce[2], auth->curve->nonce_len);
		os_memcpy(r_nonce + 4, auth->r_nonce, auth->curve->nonce_len);

		if (aes_siv_encrypt(auth->k2, auth->curve->hash_len,
				    r_nonce, r_nonce_len,
				    2, addr, len, wrapped_r_nonce) < 0)
			goto fail;
		wpa_hexdump(MSG_DEBUG, "DPP: {R-nonce}k2",
			    wrapped_r_nonce, r_nonce_len + AES_BLOCK_SIZE);
	}

#ifdef CONFIG_TESTING_OPTIONS
	if (dpp_test == DPP_TEST_AFTER_WRAPPED_DATA_AUTH_CONF) {
		wpa_printf(MSG_INFO, "DPP: TESTING - attr after Wrapped Data");
		dpp_build_attr_status(msg, DPP_STATUS_OK);
	}
skip_wrapped_data:
#endif /* CONFIG_TESTING_OPTIONS */

	wpa_hexdump_buf(MSG_DEBUG,
			"DPP: Authentication Confirmation frame attributes",
			msg);
	if (status == DPP_STATUS_OK)
		dpp_auth_success(auth);

	return msg;

fail:
	wpabuf_free(msg);
	return NULL;
}


static void
dpp_auth_resp_rx_status(struct dpp_authentication *auth, const u8 *hdr,
			const u8 *attr_start, size_t attr_len,
			const u8 *wrapped_data, u16 wrapped_data_len,
			enum dpp_status_error status)
{
	const u8 *addr[2];
	size_t len[2];
	u8 *unwrapped = NULL;
	size_t unwrapped_len = 0;
	const u8 *i_nonce, *r_capab;
	u16 i_nonce_len, r_capab_len;

	if (status == DPP_STATUS_NOT_COMPATIBLE) {
		wpa_printf(MSG_DEBUG,
			   "DPP: Responder reported incompatible roles");
	} else if (status == DPP_STATUS_RESPONSE_PENDING) {
		wpa_printf(MSG_DEBUG,
			   "DPP: Responder reported more time needed");
	} else {
		wpa_printf(MSG_DEBUG,
			   "DPP: Responder reported failure (status %d)",
			   status);
		dpp_auth_fail(auth, "Responder reported failure");
		return;
	}

	addr[0] = hdr;
	len[0] = DPP_HDR_LEN;
	addr[1] = attr_start;
	len[1] = attr_len;
	wpa_hexdump(MSG_DEBUG, "DDP: AES-SIV AD[0]", addr[0], len[0]);
	wpa_hexdump(MSG_DEBUG, "DDP: AES-SIV AD[1]", addr[1], len[1]);
	wpa_hexdump(MSG_DEBUG, "DPP: AES-SIV ciphertext",
		    wrapped_data, wrapped_data_len);
	unwrapped_len = wrapped_data_len - AES_BLOCK_SIZE;
	unwrapped = os_malloc(unwrapped_len);
	if (!unwrapped)
		goto fail;
	if (aes_siv_decrypt(auth->k1, auth->curve->hash_len,
			    wrapped_data, wrapped_data_len,
			    2, addr, len, unwrapped) < 0) {
		dpp_auth_fail(auth, "AES-SIV decryption failed");
		goto fail;
	}
	wpa_hexdump(MSG_DEBUG, "DPP: AES-SIV cleartext",
		    unwrapped, unwrapped_len);

	if (dpp_check_attrs(unwrapped, unwrapped_len) < 0) {
		dpp_auth_fail(auth, "Invalid attribute in unwrapped data");
		goto fail;
	}

	i_nonce = dpp_get_attr(unwrapped, unwrapped_len, DPP_ATTR_I_NONCE,
			       &i_nonce_len);
	if (!i_nonce || i_nonce_len != auth->curve->nonce_len) {
		dpp_auth_fail(auth, "Missing or invalid I-nonce");
		goto fail;
	}
	wpa_hexdump(MSG_DEBUG, "DPP: I-nonce", i_nonce, i_nonce_len);
	if (os_memcmp(auth->i_nonce, i_nonce, i_nonce_len) != 0) {
		dpp_auth_fail(auth, "I-nonce mismatch");
		goto fail;
	}

	r_capab = dpp_get_attr(unwrapped, unwrapped_len,
			       DPP_ATTR_R_CAPABILITIES,
			       &r_capab_len);
	if (!r_capab || r_capab_len < 1) {
		dpp_auth_fail(auth, "Missing or invalid R-capabilities");
		goto fail;
	}
	auth->r_capab = r_capab[0];
	wpa_printf(MSG_DEBUG, "DPP: R-capabilities: 0x%02x", auth->r_capab);
	if (status == DPP_STATUS_NOT_COMPATIBLE) {
		wpa_msg(auth->msg_ctx, MSG_INFO, DPP_EVENT_NOT_COMPATIBLE
			"r-capab=0x%02x", auth->r_capab);
	} else if (status == DPP_STATUS_RESPONSE_PENDING) {
		u8 role = auth->r_capab & DPP_CAPAB_ROLE_MASK;

		if ((auth->configurator && role != DPP_CAPAB_ENROLLEE) ||
		    (!auth->configurator && role != DPP_CAPAB_CONFIGURATOR)) {
			wpa_msg(auth->msg_ctx, MSG_INFO,
				DPP_EVENT_FAIL "Unexpected role in R-capabilities 0x%02x",
				role);
		} else {
			wpa_printf(MSG_DEBUG,
				   "DPP: Continue waiting for full DPP Authentication Response");
			wpa_msg(auth->msg_ctx, MSG_INFO,
				DPP_EVENT_RESPONSE_PENDING "%s",
				auth->tmp_own_bi ? auth->tmp_own_bi->uri : "");
		}
	}
fail:
	bin_clear_free(unwrapped, unwrapped_len);
}


struct wpabuf *
dpp_auth_resp_rx(struct dpp_authentication *auth, const u8 *hdr,
		 const u8 *attr_start, size_t attr_len)
{
	EVP_PKEY *pr;
	EVP_PKEY_CTX *ctx = NULL;
	size_t secret_len;
	const u8 *addr[2];
	size_t len[2];
	u8 *unwrapped = NULL, *unwrapped2 = NULL;
	size_t unwrapped_len = 0, unwrapped2_len = 0;
	const u8 *r_bootstrap, *i_bootstrap, *wrapped_data, *status, *r_proto,
		*r_nonce, *i_nonce, *r_capab, *wrapped2, *r_auth;
	u16 r_bootstrap_len, i_bootstrap_len, wrapped_data_len, status_len,
		r_proto_len, r_nonce_len, i_nonce_len, r_capab_len,
		wrapped2_len, r_auth_len;
	u8 r_auth2[DPP_MAX_HASH_LEN];
	u8 role;

#ifdef CONFIG_TESTING_OPTIONS
	if (dpp_test == DPP_TEST_STOP_AT_AUTH_RESP) {
		wpa_printf(MSG_INFO,
			   "DPP: TESTING - stop at Authentication Response");
		return NULL;
	}
#endif /* CONFIG_TESTING_OPTIONS */

	if (!auth->initiator || !auth->peer_bi) {
		dpp_auth_fail(auth, "Unexpected Authentication Response");
		return NULL;
	}

	auth->waiting_auth_resp = 0;

	wrapped_data = dpp_get_attr(attr_start, attr_len, DPP_ATTR_WRAPPED_DATA,
				    &wrapped_data_len);
	if (!wrapped_data || wrapped_data_len < AES_BLOCK_SIZE) {
		dpp_auth_fail(auth,
			      "Missing or invalid required Wrapped Data attribute");
		return NULL;
	}
	wpa_hexdump(MSG_DEBUG, "DPP: Wrapped data",
		    wrapped_data, wrapped_data_len);

	attr_len = wrapped_data - 4 - attr_start;

	r_bootstrap = dpp_get_attr(attr_start, attr_len,
				   DPP_ATTR_R_BOOTSTRAP_KEY_HASH,
				   &r_bootstrap_len);
	if (!r_bootstrap || r_bootstrap_len != SHA256_MAC_LEN) {
		dpp_auth_fail(auth,
			      "Missing or invalid required Responder Bootstrapping Key Hash attribute");
		return NULL;
	}
	wpa_hexdump(MSG_DEBUG, "DPP: Responder Bootstrapping Key Hash",
		    r_bootstrap, r_bootstrap_len);
	if (os_memcmp(r_bootstrap, auth->peer_bi->pubkey_hash,
		      SHA256_MAC_LEN) != 0) {
		dpp_auth_fail(auth,
			      "Unexpected Responder Bootstrapping Key Hash value");
		wpa_hexdump(MSG_DEBUG,
			    "DPP: Expected Responder Bootstrapping Key Hash",
			    auth->peer_bi->pubkey_hash, SHA256_MAC_LEN);
		return NULL;
	}

	i_bootstrap = dpp_get_attr(attr_start, attr_len,
				   DPP_ATTR_I_BOOTSTRAP_KEY_HASH,
				   &i_bootstrap_len);
	if (i_bootstrap) {
		if (i_bootstrap_len != SHA256_MAC_LEN) {
			dpp_auth_fail(auth,
				      "Invalid Initiator Bootstrapping Key Hash attribute");
			return NULL;
		}
		wpa_hexdump(MSG_MSGDUMP,
			    "DPP: Initiator Bootstrapping Key Hash",
			    i_bootstrap, i_bootstrap_len);
		if (!auth->own_bi ||
		    os_memcmp(i_bootstrap, auth->own_bi->pubkey_hash,
			      SHA256_MAC_LEN) != 0) {
			dpp_auth_fail(auth,
				      "Initiator Bootstrapping Key Hash attribute did not match");
			return NULL;
		}
	} else if (auth->own_bi && auth->own_bi->type == DPP_BOOTSTRAP_PKEX) {
		/* PKEX bootstrapping mandates use of mutual authentication */
		dpp_auth_fail(auth,
			      "Missing Initiator Bootstrapping Key Hash attribute");
		return NULL;
	}

	status = dpp_get_attr(attr_start, attr_len, DPP_ATTR_STATUS,
			      &status_len);
	if (!status || status_len < 1) {
		dpp_auth_fail(auth,
			      "Missing or invalid required DPP Status attribute");
		return NULL;
	}
	wpa_printf(MSG_DEBUG, "DPP: Status %u", status[0]);
	auth->auth_resp_status = status[0];
	if (status[0] != DPP_STATUS_OK) {
		dpp_auth_resp_rx_status(auth, hdr, attr_start,
					attr_len, wrapped_data,
					wrapped_data_len, status[0]);
		return NULL;
	}

	if (!i_bootstrap && auth->own_bi) {
		wpa_printf(MSG_DEBUG,
			   "DPP: Responder decided not to use mutual authentication");
		auth->own_bi = NULL;
	}

	wpa_msg(auth->msg_ctx, MSG_INFO, DPP_EVENT_AUTH_DIRECTION "mutual=%d",
		auth->own_bi != NULL);

	r_proto = dpp_get_attr(attr_start, attr_len, DPP_ATTR_R_PROTOCOL_KEY,
			       &r_proto_len);
	if (!r_proto) {
		dpp_auth_fail(auth,
			      "Missing required Responder Protocol Key attribute");
		return NULL;
	}
	wpa_hexdump(MSG_MSGDUMP, "DPP: Responder Protocol Key",
		    r_proto, r_proto_len);

	/* N = pI * PR */
	pr = dpp_set_pubkey_point(auth->own_protocol_key, r_proto, r_proto_len);
	if (!pr) {
		dpp_auth_fail(auth, "Invalid Responder Protocol Key");
		return NULL;
	}
	dpp_debug_print_key("Peer (Responder) Protocol Key", pr);

	ctx = EVP_PKEY_CTX_new(auth->own_protocol_key, NULL);
	if (!ctx ||
	    EVP_PKEY_derive_init(ctx) != 1 ||
	    EVP_PKEY_derive_set_peer(ctx, pr) != 1 ||
	    EVP_PKEY_derive(ctx, NULL, &secret_len) != 1 ||
	    secret_len > DPP_MAX_SHARED_SECRET_LEN ||
	    EVP_PKEY_derive(ctx, auth->Nx, &secret_len) != 1) {
		wpa_printf(MSG_ERROR,
			   "DPP: Failed to derive ECDH shared secret: %s",
			   ERR_error_string(ERR_get_error(), NULL));
		dpp_auth_fail(auth, "Failed to derive ECDH shared secret");
		goto fail;
	}
	EVP_PKEY_CTX_free(ctx);
	ctx = NULL;
	auth->peer_protocol_key = pr;
	pr = NULL;

	wpa_hexdump_key(MSG_DEBUG, "DPP: ECDH shared secret (N.x)",
			auth->Nx, auth->secret_len);
	auth->Nx_len = auth->secret_len;

	if (dpp_derive_k2(auth->Nx, auth->secret_len, auth->k2,
			  auth->curve->hash_len) < 0)
		goto fail;

	addr[0] = hdr;
	len[0] = DPP_HDR_LEN;
	addr[1] = attr_start;
	len[1] = attr_len;
	wpa_hexdump(MSG_DEBUG, "DDP: AES-SIV AD[0]", addr[0], len[0]);
	wpa_hexdump(MSG_DEBUG, "DDP: AES-SIV AD[1]", addr[1], len[1]);
	wpa_hexdump(MSG_DEBUG, "DPP: AES-SIV ciphertext",
		    wrapped_data, wrapped_data_len);
	unwrapped_len = wrapped_data_len - AES_BLOCK_SIZE;
	unwrapped = os_malloc(unwrapped_len);
	if (!unwrapped)
		goto fail;
	if (aes_siv_decrypt(auth->k2, auth->curve->hash_len,
			    wrapped_data, wrapped_data_len,
			    2, addr, len, unwrapped) < 0) {
		dpp_auth_fail(auth, "AES-SIV decryption failed");
		goto fail;
	}
	wpa_hexdump(MSG_DEBUG, "DPP: AES-SIV cleartext",
		    unwrapped, unwrapped_len);

	if (dpp_check_attrs(unwrapped, unwrapped_len) < 0) {
		dpp_auth_fail(auth, "Invalid attribute in unwrapped data");
		goto fail;
	}

	r_nonce = dpp_get_attr(unwrapped, unwrapped_len, DPP_ATTR_R_NONCE,
			       &r_nonce_len);
	if (!r_nonce || r_nonce_len != auth->curve->nonce_len) {
		dpp_auth_fail(auth, "DPP: Missing or invalid R-nonce");
		goto fail;
	}
	wpa_hexdump(MSG_DEBUG, "DPP: R-nonce", r_nonce, r_nonce_len);
	os_memcpy(auth->r_nonce, r_nonce, r_nonce_len);

	i_nonce = dpp_get_attr(unwrapped, unwrapped_len, DPP_ATTR_I_NONCE,
			       &i_nonce_len);
	if (!i_nonce || i_nonce_len != auth->curve->nonce_len) {
		dpp_auth_fail(auth, "Missing or invalid I-nonce");
		goto fail;
	}
	wpa_hexdump(MSG_DEBUG, "DPP: I-nonce", i_nonce, i_nonce_len);
	if (os_memcmp(auth->i_nonce, i_nonce, i_nonce_len) != 0) {
		dpp_auth_fail(auth, "I-nonce mismatch");
		goto fail;
	}

	if (auth->own_bi) {
		/* Mutual authentication */
		if (dpp_auth_derive_l_initiator(auth) < 0)
			goto fail;
	}

	r_capab = dpp_get_attr(unwrapped, unwrapped_len,
			       DPP_ATTR_R_CAPABILITIES,
			       &r_capab_len);
	if (!r_capab || r_capab_len < 1) {
		dpp_auth_fail(auth, "Missing or invalid R-capabilities");
		goto fail;
	}
	auth->r_capab = r_capab[0];
	wpa_printf(MSG_DEBUG, "DPP: R-capabilities: 0x%02x", auth->r_capab);
	role = auth->r_capab & DPP_CAPAB_ROLE_MASK;
	if ((auth->allowed_roles ==
	     (DPP_CAPAB_CONFIGURATOR | DPP_CAPAB_ENROLLEE)) &&
	    (role == DPP_CAPAB_CONFIGURATOR || role == DPP_CAPAB_ENROLLEE)) {
		/* Peer selected its role, so move from "either role" to the
		 * role that is compatible with peer's selection. */
		auth->configurator = role == DPP_CAPAB_ENROLLEE;
		wpa_printf(MSG_DEBUG, "DPP: Acting as %s",
			   auth->configurator ? "Configurator" : "Enrollee");
	} else if ((auth->configurator && role != DPP_CAPAB_ENROLLEE) ||
		   (!auth->configurator && role != DPP_CAPAB_CONFIGURATOR)) {
		wpa_printf(MSG_DEBUG, "DPP: Incompatible role selection");
		wpa_msg(auth->msg_ctx, MSG_INFO, DPP_EVENT_FAIL
			"Unexpected role in R-capabilities 0x%02x",
			role);
		if (role != DPP_CAPAB_ENROLLEE &&
		    role != DPP_CAPAB_CONFIGURATOR)
			goto fail;
		bin_clear_free(unwrapped, unwrapped_len);
		auth->remove_on_tx_status = 1;
		return dpp_auth_build_conf(auth, DPP_STATUS_NOT_COMPATIBLE);
	}

	wrapped2 = dpp_get_attr(unwrapped, unwrapped_len,
				DPP_ATTR_WRAPPED_DATA, &wrapped2_len);
	if (!wrapped2 || wrapped2_len < AES_BLOCK_SIZE) {
		dpp_auth_fail(auth,
			      "Missing or invalid Secondary Wrapped Data");
		goto fail;
	}

	wpa_hexdump(MSG_DEBUG, "DPP: AES-SIV ciphertext",
		    wrapped2, wrapped2_len);

	if (dpp_derive_ke(auth, auth->ke, auth->curve->hash_len) < 0)
		goto fail;

	unwrapped2_len = wrapped2_len - AES_BLOCK_SIZE;
	unwrapped2 = os_malloc(unwrapped2_len);
	if (!unwrapped2)
		goto fail;
	if (aes_siv_decrypt(auth->ke, auth->curve->hash_len,
			    wrapped2, wrapped2_len,
			    0, NULL, NULL, unwrapped2) < 0) {
		dpp_auth_fail(auth, "AES-SIV decryption failed");
		goto fail;
	}
	wpa_hexdump(MSG_DEBUG, "DPP: AES-SIV cleartext",
		    unwrapped2, unwrapped2_len);

	if (dpp_check_attrs(unwrapped2, unwrapped2_len) < 0) {
		dpp_auth_fail(auth,
			      "Invalid attribute in secondary unwrapped data");
		goto fail;
	}

	r_auth = dpp_get_attr(unwrapped2, unwrapped2_len, DPP_ATTR_R_AUTH_TAG,
			       &r_auth_len);
	if (!r_auth || r_auth_len != auth->curve->hash_len) {
		dpp_auth_fail(auth,
			      "Missing or invalid Responder Authenticating Tag");
		goto fail;
	}
	wpa_hexdump(MSG_DEBUG, "DPP: Received Responder Authenticating Tag",
		    r_auth, r_auth_len);
	/* R-auth' = H(I-nonce | R-nonce | PI.x | PR.x | [BI.x |] BR.x | 0) */
	if (dpp_gen_r_auth(auth, r_auth2) < 0)
		goto fail;
	wpa_hexdump(MSG_DEBUG, "DPP: Calculated Responder Authenticating Tag",
		    r_auth2, r_auth_len);
	if (os_memcmp(r_auth, r_auth2, r_auth_len) != 0) {
		dpp_auth_fail(auth, "Mismatching Responder Authenticating Tag");
		bin_clear_free(unwrapped, unwrapped_len);
		bin_clear_free(unwrapped2, unwrapped2_len);
		auth->remove_on_tx_status = 1;
		return dpp_auth_build_conf(auth, DPP_STATUS_AUTH_FAILURE);
	}

	bin_clear_free(unwrapped, unwrapped_len);
	bin_clear_free(unwrapped2, unwrapped2_len);

#ifdef CONFIG_TESTING_OPTIONS
	if (dpp_test == DPP_TEST_AUTH_RESP_IN_PLACE_OF_CONF) {
		wpa_printf(MSG_INFO,
			   "DPP: TESTING - Authentication Response in place of Confirm");
		if (dpp_auth_build_resp_ok(auth) < 0)
			return NULL;
		return wpabuf_dup(auth->resp_msg);
	}
#endif /* CONFIG_TESTING_OPTIONS */

	return dpp_auth_build_conf(auth, DPP_STATUS_OK);

fail:
	bin_clear_free(unwrapped, unwrapped_len);
	bin_clear_free(unwrapped2, unwrapped2_len);
	EVP_PKEY_free(pr);
	EVP_PKEY_CTX_free(ctx);
	return NULL;
}


static int dpp_auth_conf_rx_failure(struct dpp_authentication *auth,
				    const u8 *hdr,
				    const u8 *attr_start, size_t attr_len,
				    const u8 *wrapped_data,
				    u16 wrapped_data_len,
				    enum dpp_status_error status)
{
	const u8 *addr[2];
	size_t len[2];
	u8 *unwrapped = NULL;
	size_t unwrapped_len = 0;
	const u8 *r_nonce;
	u16 r_nonce_len;

	/* Authentication Confirm failure cases are expected to include
	 * {R-nonce}k2 in the Wrapped Data attribute. */

	addr[0] = hdr;
	len[0] = DPP_HDR_LEN;
	addr[1] = attr_start;
	len[1] = attr_len;
	wpa_hexdump(MSG_DEBUG, "DDP: AES-SIV AD[0]", addr[0], len[0]);
	wpa_hexdump(MSG_DEBUG, "DDP: AES-SIV AD[1]", addr[1], len[1]);
	wpa_hexdump(MSG_DEBUG, "DPP: AES-SIV ciphertext",
		    wrapped_data, wrapped_data_len);
	unwrapped_len = wrapped_data_len - AES_BLOCK_SIZE;
	unwrapped = os_malloc(unwrapped_len);
	if (!unwrapped) {
		dpp_auth_fail(auth, "Authentication failed");
		goto fail;
	}
	if (aes_siv_decrypt(auth->k2, auth->curve->hash_len,
			    wrapped_data, wrapped_data_len,
			    2, addr, len, unwrapped) < 0) {
		dpp_auth_fail(auth, "AES-SIV decryption failed");
		goto fail;
	}
	wpa_hexdump(MSG_DEBUG, "DPP: AES-SIV cleartext",
		    unwrapped, unwrapped_len);

	if (dpp_check_attrs(unwrapped, unwrapped_len) < 0) {
		dpp_auth_fail(auth, "Invalid attribute in unwrapped data");
		goto fail;
	}

	r_nonce = dpp_get_attr(unwrapped, unwrapped_len, DPP_ATTR_R_NONCE,
			       &r_nonce_len);
	if (!r_nonce || r_nonce_len != auth->curve->nonce_len) {
		dpp_auth_fail(auth, "DPP: Missing or invalid R-nonce");
		goto fail;
	}
	if (os_memcmp(r_nonce, auth->r_nonce, r_nonce_len) != 0) {
		wpa_hexdump(MSG_DEBUG, "DPP: Received R-nonce",
			    r_nonce, r_nonce_len);
		wpa_hexdump(MSG_DEBUG, "DPP: Expected R-nonce",
			    auth->r_nonce, r_nonce_len);
		dpp_auth_fail(auth, "R-nonce mismatch");
		goto fail;
	}

	if (status == DPP_STATUS_NOT_COMPATIBLE)
		dpp_auth_fail(auth, "Peer reported incompatible R-capab role");
	else if (status == DPP_STATUS_AUTH_FAILURE)
		dpp_auth_fail(auth, "Peer reported authentication failure)");

fail:
	bin_clear_free(unwrapped, unwrapped_len);
	return -1;
}


int dpp_auth_conf_rx(struct dpp_authentication *auth, const u8 *hdr,
		     const u8 *attr_start, size_t attr_len)
{
	const u8 *r_bootstrap, *i_bootstrap, *wrapped_data, *status, *i_auth;
	u16 r_bootstrap_len, i_bootstrap_len, wrapped_data_len, status_len,
		i_auth_len;
	const u8 *addr[2];
	size_t len[2];
	u8 *unwrapped = NULL;
	size_t unwrapped_len = 0;
	u8 i_auth2[DPP_MAX_HASH_LEN];

#ifdef CONFIG_TESTING_OPTIONS
	if (dpp_test == DPP_TEST_STOP_AT_AUTH_CONF) {
		wpa_printf(MSG_INFO,
			   "DPP: TESTING - stop at Authentication Confirm");
		return -1;
	}
#endif /* CONFIG_TESTING_OPTIONS */

	if (auth->initiator || !auth->own_bi) {
		dpp_auth_fail(auth, "Unexpected Authentication Confirm");
		return -1;
	}

	auth->waiting_auth_conf = 0;

	wrapped_data = dpp_get_attr(attr_start, attr_len, DPP_ATTR_WRAPPED_DATA,
				    &wrapped_data_len);
	if (!wrapped_data || wrapped_data_len < AES_BLOCK_SIZE) {
		dpp_auth_fail(auth,
			      "Missing or invalid required Wrapped Data attribute");
		return -1;
	}
	wpa_hexdump(MSG_DEBUG, "DPP: Wrapped data",
		    wrapped_data, wrapped_data_len);

	attr_len = wrapped_data - 4 - attr_start;

	r_bootstrap = dpp_get_attr(attr_start, attr_len,
				   DPP_ATTR_R_BOOTSTRAP_KEY_HASH,
				   &r_bootstrap_len);
	if (!r_bootstrap || r_bootstrap_len != SHA256_MAC_LEN) {
		dpp_auth_fail(auth,
			      "Missing or invalid required Responder Bootstrapping Key Hash attribute");
		return -1;
	}
	wpa_hexdump(MSG_DEBUG, "DPP: Responder Bootstrapping Key Hash",
		    r_bootstrap, r_bootstrap_len);
	if (os_memcmp(r_bootstrap, auth->own_bi->pubkey_hash,
		      SHA256_MAC_LEN) != 0) {
		wpa_hexdump(MSG_DEBUG,
			    "DPP: Expected Responder Bootstrapping Key Hash",
			    auth->peer_bi->pubkey_hash, SHA256_MAC_LEN);
		dpp_auth_fail(auth,
			      "Responder Bootstrapping Key Hash mismatch");
		return -1;
	}

	i_bootstrap = dpp_get_attr(attr_start, attr_len,
				   DPP_ATTR_I_BOOTSTRAP_KEY_HASH,
				   &i_bootstrap_len);
	if (i_bootstrap) {
		if (i_bootstrap_len != SHA256_MAC_LEN) {
			dpp_auth_fail(auth,
				      "Invalid Initiator Bootstrapping Key Hash attribute");
			return -1;
		}
		wpa_hexdump(MSG_MSGDUMP,
			    "DPP: Initiator Bootstrapping Key Hash",
			    i_bootstrap, i_bootstrap_len);
		if (!auth->peer_bi ||
		    os_memcmp(i_bootstrap, auth->peer_bi->pubkey_hash,
			      SHA256_MAC_LEN) != 0) {
			dpp_auth_fail(auth,
				      "Initiator Bootstrapping Key Hash mismatch");
			return -1;
		}
	} else if (auth->peer_bi) {
		/* Mutual authentication and peer did not include its
		 * Bootstrapping Key Hash attribute. */
		dpp_auth_fail(auth,
			      "Missing Initiator Bootstrapping Key Hash attribute");
		return -1;
	}

	status = dpp_get_attr(attr_start, attr_len, DPP_ATTR_STATUS,
			      &status_len);
	if (!status || status_len < 1) {
		dpp_auth_fail(auth,
			      "Missing or invalid required DPP Status attribute");
		return -1;
	}
	wpa_printf(MSG_DEBUG, "DPP: Status %u", status[0]);
	if (status[0] == DPP_STATUS_NOT_COMPATIBLE ||
	    status[0] == DPP_STATUS_AUTH_FAILURE)
		return dpp_auth_conf_rx_failure(auth, hdr, attr_start,
						attr_len, wrapped_data,
						wrapped_data_len, status[0]);

	if (status[0] != DPP_STATUS_OK) {
		dpp_auth_fail(auth, "Authentication failed");
		return -1;
	}

	addr[0] = hdr;
	len[0] = DPP_HDR_LEN;
	addr[1] = attr_start;
	len[1] = attr_len;
	wpa_hexdump(MSG_DEBUG, "DDP: AES-SIV AD[0]", addr[0], len[0]);
	wpa_hexdump(MSG_DEBUG, "DDP: AES-SIV AD[1]", addr[1], len[1]);
	wpa_hexdump(MSG_DEBUG, "DPP: AES-SIV ciphertext",
		    wrapped_data, wrapped_data_len);
	unwrapped_len = wrapped_data_len - AES_BLOCK_SIZE;
	unwrapped = os_malloc(unwrapped_len);
	if (!unwrapped)
		return -1;
	if (aes_siv_decrypt(auth->ke, auth->curve->hash_len,
			    wrapped_data, wrapped_data_len,
			    2, addr, len, unwrapped) < 0) {
		dpp_auth_fail(auth, "AES-SIV decryption failed");
		goto fail;
	}
	wpa_hexdump(MSG_DEBUG, "DPP: AES-SIV cleartext",
		    unwrapped, unwrapped_len);

	if (dpp_check_attrs(unwrapped, unwrapped_len) < 0) {
		dpp_auth_fail(auth, "Invalid attribute in unwrapped data");
		goto fail;
	}

	i_auth = dpp_get_attr(unwrapped, unwrapped_len, DPP_ATTR_I_AUTH_TAG,
			      &i_auth_len);
	if (!i_auth || i_auth_len != auth->curve->hash_len) {
		dpp_auth_fail(auth,
			      "Missing or invalid Initiator Authenticating Tag");
		goto fail;
	}
	wpa_hexdump(MSG_DEBUG, "DPP: Received Initiator Authenticating Tag",
		    i_auth, i_auth_len);
	/* I-auth' = H(R-nonce | I-nonce | PR.x | PI.x | BR.x | [BI.x |] 1) */
	if (dpp_gen_i_auth(auth, i_auth2) < 0)
		goto fail;
	wpa_hexdump(MSG_DEBUG, "DPP: Calculated Initiator Authenticating Tag",
		    i_auth2, i_auth_len);
	if (os_memcmp(i_auth, i_auth2, i_auth_len) != 0) {
		dpp_auth_fail(auth, "Mismatching Initiator Authenticating Tag");
		goto fail;
	}

	bin_clear_free(unwrapped, unwrapped_len);
	dpp_auth_success(auth);
	return 0;
fail:
	bin_clear_free(unwrapped, unwrapped_len);
	return -1;
}


void dpp_configuration_free(struct dpp_configuration *conf)
{
	if (!conf)
		return;
	str_clear_free(conf->passphrase);
	os_free(conf->group_id);
	bin_clear_free(conf, sizeof(*conf));
}


void dpp_auth_deinit(struct dpp_authentication *auth)
{
	if (!auth)
		return;
	dpp_configuration_free(auth->conf_ap);
	dpp_configuration_free(auth->conf_sta);
	EVP_PKEY_free(auth->own_protocol_key);
	EVP_PKEY_free(auth->peer_protocol_key);
	wpabuf_free(auth->req_msg);
	wpabuf_free(auth->resp_msg);
	wpabuf_free(auth->conf_req);
	os_free(auth->connector);
	wpabuf_free(auth->net_access_key);
	wpabuf_free(auth->c_sign_key);
	dpp_bootstrap_info_free(auth->tmp_own_bi);
#ifdef CONFIG_TESTING_OPTIONS
	os_free(auth->config_obj_override);
	os_free(auth->discovery_override);
	os_free(auth->groups_override);
#endif /* CONFIG_TESTING_OPTIONS */
	bin_clear_free(auth, sizeof(*auth));
}


static struct wpabuf *
dpp_build_conf_start(struct dpp_authentication *auth,
		     struct dpp_configuration *conf, size_t tailroom)
{
	struct wpabuf *buf;
	char ssid[6 * sizeof(conf->ssid) + 1];

#ifdef CONFIG_TESTING_OPTIONS
	if (auth->discovery_override)
		tailroom += os_strlen(auth->discovery_override);
#endif /* CONFIG_TESTING_OPTIONS */

	buf = wpabuf_alloc(200 + tailroom);
	if (!buf)
		return NULL;
	wpabuf_put_str(buf, "{\"wi-fi_tech\":\"infra\",\"discovery\":");
#ifdef CONFIG_TESTING_OPTIONS
	if (auth->discovery_override) {
		wpa_printf(MSG_DEBUG, "DPP: TESTING - discovery override: '%s'",
			   auth->discovery_override);
		wpabuf_put_str(buf, auth->discovery_override);
		wpabuf_put_u8(buf, ',');
		return buf;
	}
#endif /* CONFIG_TESTING_OPTIONS */
	wpabuf_put_str(buf, "{\"ssid\":\"");
	json_escape_string(ssid, sizeof(ssid),
			   (const char *) conf->ssid, conf->ssid_len);
	wpabuf_put_str(buf, ssid);
	wpabuf_put_str(buf, "\"},");

	return buf;
}


static int dpp_build_jwk(struct wpabuf *buf, const char *name, EVP_PKEY *key,
			 const char *kid, const struct dpp_curve_params *curve)
{
	struct wpabuf *pub;
	const u8 *pos;
	char *x = NULL, *y = NULL;
	int ret = -1;

	pub = dpp_get_pubkey_point(key, 0);
	if (!pub)
		goto fail;
	pos = wpabuf_head(pub);
	x = (char *) base64_url_encode(pos, curve->prime_len, NULL, 0);
	pos += curve->prime_len;
	y = (char *) base64_url_encode(pos, curve->prime_len, NULL, 0);
	if (!x || !y)
		goto fail;

	wpabuf_put_str(buf, "\"");
	wpabuf_put_str(buf, name);
	wpabuf_put_str(buf, "\":{\"kty\":\"EC\",\"crv\":\"");
	wpabuf_put_str(buf, curve->jwk_crv);
	wpabuf_put_str(buf, "\",\"x\":\"");
	wpabuf_put_str(buf, x);
	wpabuf_put_str(buf, "\",\"y\":\"");
	wpabuf_put_str(buf, y);
	if (kid) {
		wpabuf_put_str(buf, "\",\"kid\":\"");
		wpabuf_put_str(buf, kid);
	}
	wpabuf_put_str(buf, "\"}");
	ret = 0;
fail:
	wpabuf_free(pub);
	os_free(x);
	os_free(y);
	return ret;
}


static struct wpabuf *
dpp_build_conf_obj_dpp(struct dpp_authentication *auth, int ap,
		       struct dpp_configuration *conf)
{
	struct wpabuf *buf = NULL;
	char *signed1 = NULL, *signed2 = NULL, *signed3 = NULL;
	size_t tailroom;
	const struct dpp_curve_params *curve;
	char jws_prot_hdr[100];
	size_t signed1_len, signed2_len, signed3_len;
	struct wpabuf *dppcon = NULL;
	unsigned char *signature = NULL;
	const unsigned char *p;
	size_t signature_len;
	EVP_MD_CTX *md_ctx = NULL;
	ECDSA_SIG *sig = NULL;
	char *dot = ".";
	const EVP_MD *sign_md;
	const BIGNUM *r, *s;
	size_t extra_len = 1000;

	if (!auth->conf) {
		wpa_printf(MSG_INFO,
			   "DPP: No configurator specified - cannot generate DPP config object");
		goto fail;
	}
	curve = auth->conf->curve;
	if (curve->hash_len == SHA256_MAC_LEN) {
		sign_md = EVP_sha256();
	} else if (curve->hash_len == SHA384_MAC_LEN) {
		sign_md = EVP_sha384();
	} else if (curve->hash_len == SHA512_MAC_LEN) {
		sign_md = EVP_sha512();
	} else {
		wpa_printf(MSG_DEBUG, "DPP: Unknown signature algorithm");
		goto fail;
	}

#ifdef CONFIG_TESTING_OPTIONS
	if (auth->groups_override)
		extra_len += os_strlen(auth->groups_override);
#endif /* CONFIG_TESTING_OPTIONS */

	if (conf->group_id)
		extra_len += os_strlen(conf->group_id);

	/* Connector (JSON dppCon object) */
	dppcon = wpabuf_alloc(extra_len + 2 * auth->curve->prime_len * 4 / 3);
	if (!dppcon)
		goto fail;
#ifdef CONFIG_TESTING_OPTIONS
	if (auth->groups_override) {
		wpabuf_put_u8(dppcon, '{');
		if (auth->groups_override) {
			wpa_printf(MSG_DEBUG,
				   "DPP: TESTING - groups override: '%s'",
				   auth->groups_override);
			wpabuf_put_str(dppcon, "\"groups\":");
			wpabuf_put_str(dppcon, auth->groups_override);
			wpabuf_put_u8(dppcon, ',');
		}
		goto skip_groups;
	}
#endif /* CONFIG_TESTING_OPTIONS */
	wpabuf_printf(dppcon, "{\"groups\":[{\"groupId\":\"%s\",",
		      conf->group_id ? conf->group_id : "*");
	wpabuf_printf(dppcon, "\"netRole\":\"%s\"}],", ap ? "ap" : "sta");
#ifdef CONFIG_TESTING_OPTIONS
skip_groups:
#endif /* CONFIG_TESTING_OPTIONS */
	if (dpp_build_jwk(dppcon, "netAccessKey", auth->peer_protocol_key, NULL,
			  auth->curve) < 0) {
		wpa_printf(MSG_DEBUG, "DPP: Failed to build netAccessKey JWK");
		goto fail;
	}
	if (conf->netaccesskey_expiry) {
		struct os_tm tm;

		if (os_gmtime(conf->netaccesskey_expiry, &tm) < 0) {
			wpa_printf(MSG_DEBUG,
				   "DPP: Failed to generate expiry string");
			goto fail;
		}
		wpabuf_printf(dppcon,
			      ",\"expiry\":\"%04u-%02u-%02uT%02u:%02u:%02uZ\"",
			      tm.year, tm.month, tm.day,
			      tm.hour, tm.min, tm.sec);
	}
	wpabuf_put_u8(dppcon, '}');
	wpa_printf(MSG_DEBUG, "DPP: dppCon: %s",
		   (const char *) wpabuf_head(dppcon));

	os_snprintf(jws_prot_hdr, sizeof(jws_prot_hdr),
		    "{\"typ\":\"dppCon\",\"kid\":\"%s\",\"alg\":\"%s\"}",
		    auth->conf->kid, curve->jws_alg);
	signed1 = (char *) base64_url_encode((unsigned char *) jws_prot_hdr,
					     os_strlen(jws_prot_hdr),
					     &signed1_len, 0);
	signed2 = (char *) base64_url_encode(wpabuf_head(dppcon),
					     wpabuf_len(dppcon),
					     &signed2_len, 0);
	if (!signed1 || !signed2)
		goto fail;

	md_ctx = EVP_MD_CTX_create();
	if (!md_ctx)
		goto fail;

	ERR_clear_error();
	if (EVP_DigestSignInit(md_ctx, NULL, sign_md, NULL,
			       auth->conf->csign) != 1) {
		wpa_printf(MSG_DEBUG, "DPP: EVP_DigestSignInit failed: %s",
			   ERR_error_string(ERR_get_error(), NULL));
		goto fail;
	}
	if (EVP_DigestSignUpdate(md_ctx, signed1, signed1_len) != 1 ||
	    EVP_DigestSignUpdate(md_ctx, dot, 1) != 1 ||
	    EVP_DigestSignUpdate(md_ctx, signed2, signed2_len) != 1) {
		wpa_printf(MSG_DEBUG, "DPP: EVP_DigestSignUpdate failed: %s",
			   ERR_error_string(ERR_get_error(), NULL));
		goto fail;
	}
	if (EVP_DigestSignFinal(md_ctx, NULL, &signature_len) != 1) {
		wpa_printf(MSG_DEBUG, "DPP: EVP_DigestSignFinal failed: %s",
			   ERR_error_string(ERR_get_error(), NULL));
		goto fail;
	}
	signature = os_malloc(signature_len);
	if (!signature)
		goto fail;
	if (EVP_DigestSignFinal(md_ctx, signature, &signature_len) != 1) {
		wpa_printf(MSG_DEBUG, "DPP: EVP_DigestSignFinal failed: %s",
			   ERR_error_string(ERR_get_error(), NULL));
		goto fail;
	}
	wpa_hexdump(MSG_DEBUG, "DPP: signedConnector ECDSA signature (DER)",
		    signature, signature_len);
	/* Convert to raw coordinates r,s */
	p = signature;
	sig = d2i_ECDSA_SIG(NULL, &p, signature_len);
	if (!sig)
		goto fail;
	ECDSA_SIG_get0(sig, &r, &s);
	if (dpp_bn2bin_pad(r, signature, curve->prime_len) < 0 ||
	    dpp_bn2bin_pad(s, signature + curve->prime_len,
			   curve->prime_len) < 0)
		goto fail;
	signature_len = 2 * curve->prime_len;
	wpa_hexdump(MSG_DEBUG, "DPP: signedConnector ECDSA signature (raw r,s)",
		    signature, signature_len);
	signed3 = (char *) base64_url_encode(signature, signature_len,
					     &signed3_len, 0);
	if (!signed3)
		goto fail;

	tailroom = 1000;
	tailroom += 2 * curve->prime_len * 4 / 3 + os_strlen(auth->conf->kid);
	tailroom += signed1_len + signed2_len + signed3_len;
	buf = dpp_build_conf_start(auth, conf, tailroom);
	if (!buf)
		goto fail;

	wpabuf_put_str(buf, "\"cred\":{\"akm\":\"dpp\",\"signedConnector\":\"");
	wpabuf_put_str(buf, signed1);
	wpabuf_put_u8(buf, '.');
	wpabuf_put_str(buf, signed2);
	wpabuf_put_u8(buf, '.');
	wpabuf_put_str(buf, signed3);
	wpabuf_put_str(buf, "\",");
	if (dpp_build_jwk(buf, "csign", auth->conf->csign, auth->conf->kid,
			  curve) < 0) {
		wpa_printf(MSG_DEBUG, "DPP: Failed to build csign JWK");
		goto fail;
	}

	wpabuf_put_str(buf, "}}");

	wpa_hexdump_ascii_key(MSG_DEBUG, "DPP: Configuration Object",
			      wpabuf_head(buf), wpabuf_len(buf));

out:
	EVP_MD_CTX_destroy(md_ctx);
	ECDSA_SIG_free(sig);
	os_free(signed1);
	os_free(signed2);
	os_free(signed3);
	os_free(signature);
	wpabuf_free(dppcon);
	return buf;
fail:
	wpa_printf(MSG_DEBUG, "DPP: Failed to build configuration object");
	wpabuf_free(buf);
	buf = NULL;
	goto out;
}


static struct wpabuf *
dpp_build_conf_obj_legacy(struct dpp_authentication *auth, int ap,
			  struct dpp_configuration *conf)
{
	struct wpabuf *buf;

	buf = dpp_build_conf_start(auth, conf, 1000);
	if (!buf)
		return NULL;

	wpabuf_printf(buf, "\"cred\":{\"akm\":\"%s\",", dpp_akm_str(conf->akm));
	if (conf->passphrase) {
		char pass[63 * 6 + 1];

		if (os_strlen(conf->passphrase) > 63) {
			wpabuf_free(buf);
			return NULL;
		}

		json_escape_string(pass, sizeof(pass), conf->passphrase,
				   os_strlen(conf->passphrase));
		wpabuf_put_str(buf, "\"pass\":\"");
		wpabuf_put_str(buf, pass);
		wpabuf_put_str(buf, "\"");
	} else {
		char psk[2 * sizeof(conf->psk) + 1];

		wpa_snprintf_hex(psk, sizeof(psk),
				 conf->psk, sizeof(conf->psk));
		wpabuf_put_str(buf, "\"psk_hex\":\"");
		wpabuf_put_str(buf, psk);
		wpabuf_put_str(buf, "\"");
	}
	wpabuf_put_str(buf, "}}");

	wpa_hexdump_ascii_key(MSG_DEBUG, "DPP: Configuration Object (legacy)",
			      wpabuf_head(buf), wpabuf_len(buf));

	return buf;
}


static struct wpabuf *
dpp_build_conf_obj(struct dpp_authentication *auth, int ap)
{
	struct dpp_configuration *conf;

#ifdef CONFIG_TESTING_OPTIONS
	if (auth->config_obj_override) {
		wpa_printf(MSG_DEBUG, "DPP: Testing - Config Object override");
		return wpabuf_alloc_copy(auth->config_obj_override,
					 os_strlen(auth->config_obj_override));
	}
#endif /* CONFIG_TESTING_OPTIONS */

	conf = ap ? auth->conf_ap : auth->conf_sta;
	if (!conf) {
		wpa_printf(MSG_DEBUG,
			   "DPP: No configuration available for Enrollee(%s) - reject configuration request",
			   ap ? "ap" : "sta");
		return NULL;
	}

	if (conf->akm == DPP_AKM_DPP)
		return dpp_build_conf_obj_dpp(auth, ap, conf);
	return dpp_build_conf_obj_legacy(auth, ap, conf);
}


static struct wpabuf *
dpp_build_conf_resp(struct dpp_authentication *auth, const u8 *e_nonce,
		    u16 e_nonce_len, int ap)
{
	struct wpabuf *conf;
	size_t clear_len, attr_len;
	struct wpabuf *clear = NULL, *msg = NULL;
	u8 *wrapped;
	const u8 *addr[1];
	size_t len[1];
	enum dpp_status_error status;

	conf = dpp_build_conf_obj(auth, ap);
	if (conf) {
		wpa_hexdump_ascii(MSG_DEBUG, "DPP: configurationObject JSON",
				  wpabuf_head(conf), wpabuf_len(conf));
	}
	status = conf ? DPP_STATUS_OK : DPP_STATUS_CONFIGURE_FAILURE;

	/* { E-nonce, configurationObject}ke */
	clear_len = 4 + e_nonce_len;
	if (conf)
		clear_len += 4 + wpabuf_len(conf);
	clear = wpabuf_alloc(clear_len);
	attr_len = 4 + 1 + 4 + clear_len + AES_BLOCK_SIZE;
#ifdef CONFIG_TESTING_OPTIONS
	if (dpp_test == DPP_TEST_AFTER_WRAPPED_DATA_CONF_RESP)
		attr_len += 5;
#endif /* CONFIG_TESTING_OPTIONS */
	msg = wpabuf_alloc(attr_len);
	if (!clear || !msg)
		goto fail;

#ifdef CONFIG_TESTING_OPTIONS
	if (dpp_test == DPP_TEST_NO_E_NONCE_CONF_RESP) {
		wpa_printf(MSG_INFO, "DPP: TESTING - no E-nonce");
		goto skip_e_nonce;
	}
	if (dpp_test == DPP_TEST_E_NONCE_MISMATCH_CONF_RESP) {
		wpa_printf(MSG_INFO, "DPP: TESTING - E-nonce mismatch");
		wpabuf_put_le16(clear, DPP_ATTR_ENROLLEE_NONCE);
		wpabuf_put_le16(clear, e_nonce_len);
		wpabuf_put_data(clear, e_nonce, e_nonce_len - 1);
		wpabuf_put_u8(clear, e_nonce[e_nonce_len - 1] ^ 0x01);
		goto skip_e_nonce;
	}
	if (dpp_test == DPP_TEST_NO_WRAPPED_DATA_CONF_RESP) {
		wpa_printf(MSG_INFO, "DPP: TESTING - no Wrapped Data");
		goto skip_wrapped_data;
	}
#endif /* CONFIG_TESTING_OPTIONS */

	/* E-nonce */
	wpabuf_put_le16(clear, DPP_ATTR_ENROLLEE_NONCE);
	wpabuf_put_le16(clear, e_nonce_len);
	wpabuf_put_data(clear, e_nonce, e_nonce_len);

#ifdef CONFIG_TESTING_OPTIONS
skip_e_nonce:
	if (dpp_test == DPP_TEST_NO_CONFIG_OBJ_CONF_RESP) {
		wpa_printf(MSG_INFO, "DPP: TESTING - Config Object");
		goto skip_config_obj;
	}
#endif /* CONFIG_TESTING_OPTIONS */

	if (conf) {
		wpabuf_put_le16(clear, DPP_ATTR_CONFIG_OBJ);
		wpabuf_put_le16(clear, wpabuf_len(conf));
		wpabuf_put_buf(clear, conf);
	}

#ifdef CONFIG_TESTING_OPTIONS
skip_config_obj:
	if (dpp_test == DPP_TEST_NO_STATUS_CONF_RESP) {
		wpa_printf(MSG_INFO, "DPP: TESTING - Status");
		goto skip_status;
	}
	if (dpp_test == DPP_TEST_INVALID_STATUS_CONF_RESP) {
		wpa_printf(MSG_INFO, "DPP: TESTING - invalid Status");
		status = 255;
	}
#endif /* CONFIG_TESTING_OPTIONS */

	/* DPP Status */
	dpp_build_attr_status(msg, status);

#ifdef CONFIG_TESTING_OPTIONS
skip_status:
#endif /* CONFIG_TESTING_OPTIONS */

	addr[0] = wpabuf_head(msg);
	len[0] = wpabuf_len(msg);
	wpa_hexdump(MSG_DEBUG, "DDP: AES-SIV AD", addr[0], len[0]);

	wpabuf_put_le16(msg, DPP_ATTR_WRAPPED_DATA);
	wpabuf_put_le16(msg, wpabuf_len(clear) + AES_BLOCK_SIZE);
	wrapped = wpabuf_put(msg, wpabuf_len(clear) + AES_BLOCK_SIZE);

	wpa_hexdump_buf(MSG_DEBUG, "DPP: AES-SIV cleartext", clear);
	if (aes_siv_encrypt(auth->ke, auth->curve->hash_len,
			    wpabuf_head(clear), wpabuf_len(clear),
			    1, addr, len, wrapped) < 0)
		goto fail;
	wpa_hexdump(MSG_DEBUG, "DPP: AES-SIV ciphertext",
		    wrapped, wpabuf_len(clear) + AES_BLOCK_SIZE);

#ifdef CONFIG_TESTING_OPTIONS
	if (dpp_test == DPP_TEST_AFTER_WRAPPED_DATA_CONF_RESP) {
		wpa_printf(MSG_INFO, "DPP: TESTING - attr after Wrapped Data");
		dpp_build_attr_status(msg, DPP_STATUS_OK);
	}
skip_wrapped_data:
#endif /* CONFIG_TESTING_OPTIONS */

	wpa_hexdump_buf(MSG_DEBUG,
			"DPP: Configuration Response attributes", msg);
out:
	wpabuf_free(conf);
	wpabuf_free(clear);

	return msg;
fail:
	wpabuf_free(msg);
	msg = NULL;
	goto out;
}


struct wpabuf *
dpp_conf_req_rx(struct dpp_authentication *auth, const u8 *attr_start,
		size_t attr_len)
{
	const u8 *wrapped_data, *e_nonce, *config_attr;
	u16 wrapped_data_len, e_nonce_len, config_attr_len;
	u8 *unwrapped = NULL;
	size_t unwrapped_len = 0;
	struct wpabuf *resp = NULL;
	struct json_token *root = NULL, *token;
	int ap;

#ifdef CONFIG_TESTING_OPTIONS
	if (dpp_test == DPP_TEST_STOP_AT_CONF_REQ) {
		wpa_printf(MSG_INFO,
			   "DPP: TESTING - stop at Config Request");
		return NULL;
	}
#endif /* CONFIG_TESTING_OPTIONS */

	if (dpp_check_attrs(attr_start, attr_len) < 0) {
		dpp_auth_fail(auth, "Invalid attribute in config request");
		return NULL;
	}

	wrapped_data = dpp_get_attr(attr_start, attr_len, DPP_ATTR_WRAPPED_DATA,
				    &wrapped_data_len);
	if (!wrapped_data || wrapped_data_len < AES_BLOCK_SIZE) {
		dpp_auth_fail(auth,
			      "Missing or invalid required Wrapped Data attribute");
		return NULL;
	}

	wpa_hexdump(MSG_DEBUG, "DPP: AES-SIV ciphertext",
		    wrapped_data, wrapped_data_len);
	unwrapped_len = wrapped_data_len - AES_BLOCK_SIZE;
	unwrapped = os_malloc(unwrapped_len);
	if (!unwrapped)
		return NULL;
	if (aes_siv_decrypt(auth->ke, auth->curve->hash_len,
			    wrapped_data, wrapped_data_len,
			    0, NULL, NULL, unwrapped) < 0) {
		dpp_auth_fail(auth, "AES-SIV decryption failed");
		goto fail;
	}
	wpa_hexdump(MSG_DEBUG, "DPP: AES-SIV cleartext",
		    unwrapped, unwrapped_len);

	if (dpp_check_attrs(unwrapped, unwrapped_len) < 0) {
		dpp_auth_fail(auth, "Invalid attribute in unwrapped data");
		goto fail;
	}

	e_nonce = dpp_get_attr(unwrapped, unwrapped_len,
			       DPP_ATTR_ENROLLEE_NONCE,
			       &e_nonce_len);
	if (!e_nonce || e_nonce_len != auth->curve->nonce_len) {
		dpp_auth_fail(auth,
			      "Missing or invalid Enrollee Nonce attribute");
		goto fail;
	}
	wpa_hexdump(MSG_DEBUG, "DPP: Enrollee Nonce", e_nonce, e_nonce_len);

	config_attr = dpp_get_attr(unwrapped, unwrapped_len,
				   DPP_ATTR_CONFIG_ATTR_OBJ,
				   &config_attr_len);
	if (!config_attr) {
		dpp_auth_fail(auth,
			      "Missing or invalid Config Attributes attribute");
		goto fail;
	}
	wpa_hexdump_ascii(MSG_DEBUG, "DPP: Config Attributes",
			  config_attr, config_attr_len);

	root = json_parse((const char *) config_attr, config_attr_len);
	if (!root) {
		dpp_auth_fail(auth, "Could not parse Config Attributes");
		goto fail;
	}

	token = json_get_member(root, "name");
	if (!token || token->type != JSON_STRING) {
		dpp_auth_fail(auth, "No Config Attributes - name");
		goto fail;
	}
	wpa_printf(MSG_DEBUG, "DPP: Enrollee name = '%s'", token->string);

	token = json_get_member(root, "wi-fi_tech");
	if (!token || token->type != JSON_STRING) {
		dpp_auth_fail(auth, "No Config Attributes - wi-fi_tech");
		goto fail;
	}
	wpa_printf(MSG_DEBUG, "DPP: wi-fi_tech = '%s'", token->string);
	if (os_strcmp(token->string, "infra") != 0) {
		wpa_printf(MSG_DEBUG, "DPP: Unsupported wi-fi_tech '%s'",
			   token->string);
		dpp_auth_fail(auth, "Unsupported wi-fi_tech");
		goto fail;
	}

	token = json_get_member(root, "netRole");
	if (!token || token->type != JSON_STRING) {
		dpp_auth_fail(auth, "No Config Attributes - netRole");
		goto fail;
	}
	wpa_printf(MSG_DEBUG, "DPP: netRole = '%s'", token->string);
	if (os_strcmp(token->string, "sta") == 0) {
		ap = 0;
	} else if (os_strcmp(token->string, "ap") == 0) {
		ap = 1;
	} else {
		wpa_printf(MSG_DEBUG, "DPP: Unsupported netRole '%s'",
			   token->string);
		dpp_auth_fail(auth, "Unsupported netRole");
		goto fail;
	}

	resp = dpp_build_conf_resp(auth, e_nonce, e_nonce_len, ap);

fail:
	json_free(root);
	os_free(unwrapped);
	return resp;
}


static struct wpabuf *
dpp_parse_jws_prot_hdr(const struct dpp_curve_params *curve,
		       const u8 *prot_hdr, u16 prot_hdr_len,
		       const EVP_MD **ret_md)
{
	struct json_token *root, *token;
	struct wpabuf *kid = NULL;

	root = json_parse((const char *) prot_hdr, prot_hdr_len);
	if (!root) {
		wpa_printf(MSG_DEBUG,
			   "DPP: JSON parsing failed for JWS Protected Header");
		goto fail;
	}

	if (root->type != JSON_OBJECT) {
		wpa_printf(MSG_DEBUG,
			   "DPP: JWS Protected Header root is not an object");
		goto fail;
	}

	token = json_get_member(root, "typ");
	if (!token || token->type != JSON_STRING) {
		wpa_printf(MSG_DEBUG, "DPP: No typ string value found");
		goto fail;
	}
	wpa_printf(MSG_DEBUG, "DPP: JWS Protected Header typ=%s",
		   token->string);
	if (os_strcmp(token->string, "dppCon") != 0) {
		wpa_printf(MSG_DEBUG,
			   "DPP: Unsupported JWS Protected Header typ=%s",
			   token->string);
		goto fail;
	}

	token = json_get_member(root, "alg");
	if (!token || token->type != JSON_STRING) {
		wpa_printf(MSG_DEBUG, "DPP: No alg string value found");
		goto fail;
	}
	wpa_printf(MSG_DEBUG, "DPP: JWS Protected Header alg=%s",
		   token->string);
	if (os_strcmp(token->string, curve->jws_alg) != 0) {
		wpa_printf(MSG_DEBUG,
			   "DPP: Unexpected JWS Protected Header alg=%s (expected %s based on C-sign-key)",
			   token->string, curve->jws_alg);
		goto fail;
	}
	if (os_strcmp(token->string, "ES256") == 0 ||
	    os_strcmp(token->string, "BS256") == 0)
		*ret_md = EVP_sha256();
	else if (os_strcmp(token->string, "ES384") == 0 ||
		 os_strcmp(token->string, "BS384") == 0)
		*ret_md = EVP_sha384();
	else if (os_strcmp(token->string, "ES512") == 0 ||
		 os_strcmp(token->string, "BS512") == 0)
		*ret_md = EVP_sha512();
	else
		*ret_md = NULL;
	if (!*ret_md) {
		wpa_printf(MSG_DEBUG,
			   "DPP: Unsupported JWS Protected Header alg=%s",
			   token->string);
		goto fail;
	}

	kid = json_get_member_base64url(root, "kid");
	if (!kid) {
		wpa_printf(MSG_DEBUG, "DPP: No kid string value found");
		goto fail;
	}
	wpa_hexdump_buf(MSG_DEBUG, "DPP: JWS Protected Header kid (decoded)",
			kid);

fail:
	json_free(root);
	return kid;
}


static int dpp_parse_cred_legacy(struct dpp_authentication *auth,
				 struct json_token *cred)
{
	struct json_token *pass, *psk_hex;

	wpa_printf(MSG_DEBUG, "DPP: Legacy akm=psk credential");

	pass = json_get_member(cred, "pass");
	psk_hex = json_get_member(cred, "psk_hex");

	if (pass && pass->type == JSON_STRING) {
		size_t len = os_strlen(pass->string);

		wpa_hexdump_ascii_key(MSG_DEBUG, "DPP: Legacy passphrase",
				      pass->string, len);
		if (len < 8 || len > 63)
			return -1;
		os_strlcpy(auth->passphrase, pass->string,
			   sizeof(auth->passphrase));
	} else if (psk_hex && psk_hex->type == JSON_STRING) {
		if (auth->akm == DPP_AKM_SAE) {
			wpa_printf(MSG_DEBUG,
				   "DPP: Unexpected psk_hex with akm=sae");
			return -1;
		}
		if (os_strlen(psk_hex->string) != PMK_LEN * 2 ||
		    hexstr2bin(psk_hex->string, auth->psk, PMK_LEN) < 0) {
			wpa_printf(MSG_DEBUG, "DPP: Invalid psk_hex encoding");
			return -1;
		}
		wpa_hexdump_key(MSG_DEBUG, "DPP: Legacy PSK",
				auth->psk, PMK_LEN);
		auth->psk_set = 1;
	} else {
		wpa_printf(MSG_DEBUG, "DPP: No pass or psk_hex strings found");
		return -1;
	}

	if ((auth->akm == DPP_AKM_SAE || auth->akm == DPP_AKM_PSK_SAE) &&
	    !auth->passphrase[0]) {
		wpa_printf(MSG_DEBUG, "DPP: No pass for sae found");
		return -1;
	}

	return 0;
}


static EVP_PKEY * dpp_parse_jwk(struct json_token *jwk,
				const struct dpp_curve_params **key_curve)
{
	struct json_token *token;
	const struct dpp_curve_params *curve;
	struct wpabuf *x = NULL, *y = NULL;
	EC_GROUP *group;
	EVP_PKEY *pkey = NULL;

	token = json_get_member(jwk, "kty");
	if (!token || token->type != JSON_STRING) {
		wpa_printf(MSG_DEBUG, "DPP: No kty in JWK");
		goto fail;
	}
	if (os_strcmp(token->string, "EC") != 0) {
		wpa_printf(MSG_DEBUG, "DPP: Unexpected JWK kty '%s'",
			   token->string);
		goto fail;
	}

	token = json_get_member(jwk, "crv");
	if (!token || token->type != JSON_STRING) {
		wpa_printf(MSG_DEBUG, "DPP: No crv in JWK");
		goto fail;
	}
	curve = dpp_get_curve_jwk_crv(token->string);
	if (!curve) {
		wpa_printf(MSG_DEBUG, "DPP: Unsupported JWK crv '%s'",
			   token->string);
		goto fail;
	}

	x = json_get_member_base64url(jwk, "x");
	if (!x) {
		wpa_printf(MSG_DEBUG, "DPP: No x in JWK");
		goto fail;
	}
	wpa_hexdump_buf(MSG_DEBUG, "DPP: JWK x", x);
	if (wpabuf_len(x) != curve->prime_len) {
		wpa_printf(MSG_DEBUG,
			   "DPP: Unexpected JWK x length %u (expected %u for curve %s)",
			   (unsigned int) wpabuf_len(x),
			   (unsigned int) curve->prime_len, curve->name);
		goto fail;
	}

	y = json_get_member_base64url(jwk, "y");
	if (!y) {
		wpa_printf(MSG_DEBUG, "DPP: No y in JWK");
		goto fail;
	}
	wpa_hexdump_buf(MSG_DEBUG, "DPP: JWK y", y);
	if (wpabuf_len(y) != curve->prime_len) {
		wpa_printf(MSG_DEBUG,
			   "DPP: Unexpected JWK y length %u (expected %u for curve %s)",
			   (unsigned int) wpabuf_len(y),
			   (unsigned int) curve->prime_len, curve->name);
		goto fail;
	}

	group = EC_GROUP_new_by_curve_name(OBJ_txt2nid(curve->name));
	if (!group) {
		wpa_printf(MSG_DEBUG, "DPP: Could not prepare group for JWK");
		goto fail;
	}

	pkey = dpp_set_pubkey_point_group(group, wpabuf_head(x), wpabuf_head(y),
					  wpabuf_len(x));
	*key_curve = curve;

fail:
	wpabuf_free(x);
	wpabuf_free(y);

	return pkey;
}


int dpp_key_expired(const char *timestamp, os_time_t *expiry)
{
	struct os_time now;
	unsigned int year, month, day, hour, min, sec;
	os_time_t utime;
	const char *pos;

	/* ISO 8601 date and time:
	 * <date>T<time>
	 * YYYY-MM-DDTHH:MM:SSZ
	 * YYYY-MM-DDTHH:MM:SS+03:00
	 */
	if (os_strlen(timestamp) < 19) {
		wpa_printf(MSG_DEBUG,
			   "DPP: Too short timestamp - assume expired key");
		return 1;
	}
	if (sscanf(timestamp, "%04u-%02u-%02uT%02u:%02u:%02u",
		   &year, &month, &day, &hour, &min, &sec) != 6) {
		wpa_printf(MSG_DEBUG,
			   "DPP: Failed to parse expiration day - assume expired key");
		return 1;
	}

	if (os_mktime(year, month, day, hour, min, sec, &utime) < 0) {
		wpa_printf(MSG_DEBUG,
			   "DPP: Invalid date/time information - assume expired key");
		return 1;
	}

	pos = timestamp + 19;
	if (*pos == 'Z' || *pos == '\0') {
		/* In UTC - no need to adjust */
	} else if (*pos == '-' || *pos == '+') {
		int items;

		/* Adjust local time to UTC */
		items = sscanf(pos + 1, "%02u:%02u", &hour, &min);
		if (items < 1) {
			wpa_printf(MSG_DEBUG,
				   "DPP: Invalid time zone designator (%s) - assume expired key",
				   pos);
			return 1;
		}
		if (*pos == '-')
			utime += 3600 * hour;
		if (*pos == '+')
			utime -= 3600 * hour;
		if (items > 1) {
			if (*pos == '-')
				utime += 60 * min;
			if (*pos == '+')
				utime -= 60 * min;
		}
	} else {
		wpa_printf(MSG_DEBUG,
			   "DPP: Invalid time zone designator (%s) - assume expired key",
			   pos);
		return 1;
	}
	if (expiry)
		*expiry = utime;

	if (os_get_time(&now) < 0) {
		wpa_printf(MSG_DEBUG,
			   "DPP: Cannot get current time - assume expired key");
		return 1;
	}

	if (now.sec > utime) {
		wpa_printf(MSG_DEBUG, "DPP: Key has expired (%lu < %lu)",
			   utime, now.sec);
		return 1;
	}

	return 0;
}


static int dpp_parse_connector(struct dpp_authentication *auth,
			       const unsigned char *payload,
			       u16 payload_len)
{
	struct json_token *root, *groups, *netkey, *token;
	int ret = -1;
	EVP_PKEY *key = NULL;
	const struct dpp_curve_params *curve;
	unsigned int rules = 0;

	root = json_parse((const char *) payload, payload_len);
	if (!root) {
		wpa_printf(MSG_DEBUG, "DPP: JSON parsing of connector failed");
		goto fail;
	}

	groups = json_get_member(root, "groups");
	if (!groups || groups->type != JSON_ARRAY) {
		wpa_printf(MSG_DEBUG, "DPP: No groups array found");
		goto skip_groups;
	}
	for (token = groups->child; token; token = token->sibling) {
		struct json_token *id, *role;

		id = json_get_member(token, "groupId");
		if (!id || id->type != JSON_STRING) {
			wpa_printf(MSG_DEBUG, "DPP: Missing groupId string");
			goto fail;
		}

		role = json_get_member(token, "netRole");
		if (!role || role->type != JSON_STRING) {
			wpa_printf(MSG_DEBUG, "DPP: Missing netRole string");
			goto fail;
		}
		wpa_printf(MSG_DEBUG,
			   "DPP: connector group: groupId='%s' netRole='%s'",
			   id->string, role->string);
		rules++;
	}
skip_groups:

	if (!rules) {
		wpa_printf(MSG_DEBUG,
			   "DPP: Connector includes no groups");
		goto fail;
	}

	token = json_get_member(root, "expiry");
	if (!token || token->type != JSON_STRING) {
		wpa_printf(MSG_DEBUG,
			   "DPP: No expiry string found - connector does not expire");
	} else {
		wpa_printf(MSG_DEBUG, "DPP: expiry = %s", token->string);
		if (dpp_key_expired(token->string,
				    &auth->net_access_key_expiry)) {
			wpa_printf(MSG_DEBUG,
				   "DPP: Connector (netAccessKey) has expired");
			goto fail;
		}
	}

	netkey = json_get_member(root, "netAccessKey");
	if (!netkey || netkey->type != JSON_OBJECT) {
		wpa_printf(MSG_DEBUG, "DPP: No netAccessKey object found");
		goto fail;
	}

	key = dpp_parse_jwk(netkey, &curve);
	if (!key)
		goto fail;
	dpp_debug_print_key("DPP: Received netAccessKey", key);

	if (EVP_PKEY_cmp(key, auth->own_protocol_key) != 1) {
		wpa_printf(MSG_DEBUG,
			   "DPP: netAccessKey in connector does not match own protocol key");
#ifdef CONFIG_TESTING_OPTIONS
		if (auth->ignore_netaccesskey_mismatch) {
			wpa_printf(MSG_DEBUG,
				   "DPP: TESTING - skip netAccessKey mismatch");
		} else {
			goto fail;
		}
#else /* CONFIG_TESTING_OPTIONS */
		goto fail;
#endif /* CONFIG_TESTING_OPTIONS */
	}

	ret = 0;
fail:
	EVP_PKEY_free(key);
	json_free(root);
	return ret;
}


static int dpp_check_pubkey_match(EVP_PKEY *pub, struct wpabuf *r_hash)
{
	struct wpabuf *uncomp;
	int res;
	u8 hash[SHA256_MAC_LEN];
	const u8 *addr[1];
	size_t len[1];

	if (wpabuf_len(r_hash) != SHA256_MAC_LEN)
		return -1;
	uncomp = dpp_get_pubkey_point(pub, 1);
	if (!uncomp)
		return -1;
	addr[0] = wpabuf_head(uncomp);
	len[0] = wpabuf_len(uncomp);
	wpa_hexdump(MSG_DEBUG, "DPP: Uncompressed public key",
		    addr[0], len[0]);
	res = sha256_vector(1, addr, len, hash);
	wpabuf_free(uncomp);
	if (res < 0)
		return -1;
	if (os_memcmp(hash, wpabuf_head(r_hash), SHA256_MAC_LEN) != 0) {
		wpa_printf(MSG_DEBUG,
			   "DPP: Received hash value does not match calculated public key hash value");
		wpa_hexdump(MSG_DEBUG, "DPP: Calculated hash",
			    hash, SHA256_MAC_LEN);
		return -1;
	}
	return 0;
}


static void dpp_copy_csign(struct dpp_authentication *auth, EVP_PKEY *csign)
{
	unsigned char *der = NULL;
	int der_len;

	der_len = i2d_PUBKEY(csign, &der);
	if (der_len <= 0)
		return;
	wpabuf_free(auth->c_sign_key);
	auth->c_sign_key = wpabuf_alloc_copy(der, der_len);
	OPENSSL_free(der);
}


static void dpp_copy_netaccesskey(struct dpp_authentication *auth)
{
	unsigned char *der = NULL;
	int der_len;
	EC_KEY *eckey;

	eckey = EVP_PKEY_get1_EC_KEY(auth->own_protocol_key);
	if (!eckey)
		return;

	der_len = i2d_ECPrivateKey(eckey, &der);
	if (der_len <= 0) {
		EC_KEY_free(eckey);
		return;
	}
	wpabuf_free(auth->net_access_key);
	auth->net_access_key = wpabuf_alloc_copy(der, der_len);
	OPENSSL_free(der);
	EC_KEY_free(eckey);
}


struct dpp_signed_connector_info {
	unsigned char *payload;
	size_t payload_len;
};

static enum dpp_status_error
dpp_process_signed_connector(struct dpp_signed_connector_info *info,
			     EVP_PKEY *csign_pub, const char *connector)
{
	enum dpp_status_error ret = 255;
	const char *pos, *end, *signed_start, *signed_end;
	struct wpabuf *kid = NULL;
	unsigned char *prot_hdr = NULL, *signature = NULL;
	size_t prot_hdr_len = 0, signature_len = 0;
	const EVP_MD *sign_md = NULL;
	unsigned char *der = NULL;
	int der_len;
	int res;
	EVP_MD_CTX *md_ctx = NULL;
	ECDSA_SIG *sig = NULL;
	BIGNUM *r = NULL, *s = NULL;
	const struct dpp_curve_params *curve;
	EC_KEY *eckey;
	const EC_GROUP *group;
	int nid;

	eckey = EVP_PKEY_get1_EC_KEY(csign_pub);
	if (!eckey)
		goto fail;
	group = EC_KEY_get0_group(eckey);
	if (!group)
		goto fail;
	nid = EC_GROUP_get_curve_name(group);
	curve = dpp_get_curve_nid(nid);
	if (!curve)
		goto fail;
	wpa_printf(MSG_DEBUG, "DPP: C-sign-key group: %s", curve->jwk_crv);
	os_memset(info, 0, sizeof(*info));

	signed_start = pos = connector;
	end = os_strchr(pos, '.');
	if (!end) {
		wpa_printf(MSG_DEBUG, "DPP: Missing dot(1) in signedConnector");
		ret = DPP_STATUS_INVALID_CONNECTOR;
		goto fail;
	}
	prot_hdr = base64_url_decode((const unsigned char *) pos,
				     end - pos, &prot_hdr_len);
	if (!prot_hdr) {
		wpa_printf(MSG_DEBUG,
			   "DPP: Failed to base64url decode signedConnector JWS Protected Header");
		ret = DPP_STATUS_INVALID_CONNECTOR;
		goto fail;
	}
	wpa_hexdump_ascii(MSG_DEBUG,
			  "DPP: signedConnector - JWS Protected Header",
			  prot_hdr, prot_hdr_len);
	kid = dpp_parse_jws_prot_hdr(curve, prot_hdr, prot_hdr_len, &sign_md);
	if (!kid) {
		ret = DPP_STATUS_INVALID_CONNECTOR;
		goto fail;
	}
	if (wpabuf_len(kid) != SHA256_MAC_LEN) {
		wpa_printf(MSG_DEBUG,
			   "DPP: Unexpected signedConnector JWS Protected Header kid length: %u (expected %u)",
			   (unsigned int) wpabuf_len(kid), SHA256_MAC_LEN);
		ret = DPP_STATUS_INVALID_CONNECTOR;
		goto fail;
	}

	pos = end + 1;
	end = os_strchr(pos, '.');
	if (!end) {
		wpa_printf(MSG_DEBUG,
			   "DPP: Missing dot(2) in signedConnector");
		ret = DPP_STATUS_INVALID_CONNECTOR;
		goto fail;
	}
	signed_end = end - 1;
	info->payload = base64_url_decode((const unsigned char *) pos,
					  end - pos, &info->payload_len);
	if (!info->payload) {
		wpa_printf(MSG_DEBUG,
			   "DPP: Failed to base64url decode signedConnector JWS Payload");
		ret = DPP_STATUS_INVALID_CONNECTOR;
		goto fail;
	}
	wpa_hexdump_ascii(MSG_DEBUG,
			  "DPP: signedConnector - JWS Payload",
			  info->payload, info->payload_len);
	pos = end + 1;
	signature = base64_url_decode((const unsigned char *) pos,
				      os_strlen(pos), &signature_len);
	if (!signature) {
		wpa_printf(MSG_DEBUG,
			   "DPP: Failed to base64url decode signedConnector signature");
		ret = DPP_STATUS_INVALID_CONNECTOR;
		goto fail;
		}
	wpa_hexdump(MSG_DEBUG, "DPP: signedConnector - signature",
		    signature, signature_len);

	if (dpp_check_pubkey_match(csign_pub, kid) < 0) {
		ret = DPP_STATUS_NO_MATCH;
		goto fail;
	}

	if (signature_len & 0x01) {
		wpa_printf(MSG_DEBUG,
			   "DPP: Unexpected signedConnector signature length (%d)",
			   (int) signature_len);
		ret = DPP_STATUS_INVALID_CONNECTOR;
		goto fail;
	}

	/* JWS Signature encodes the signature (r,s) as two octet strings. Need
	 * to convert that to DER encoded ECDSA_SIG for OpenSSL EVP routines. */
	r = BN_bin2bn(signature, signature_len / 2, NULL);
	s = BN_bin2bn(signature + signature_len / 2, signature_len / 2, NULL);
	sig = ECDSA_SIG_new();
	if (!r || !s || !sig || ECDSA_SIG_set0(sig, r, s) != 1)
		goto fail;
	r = NULL;
	s = NULL;

	der_len = i2d_ECDSA_SIG(sig, &der);
	if (der_len <= 0) {
		wpa_printf(MSG_DEBUG, "DPP: Could not DER encode signature");
		goto fail;
	}
	wpa_hexdump(MSG_DEBUG, "DPP: DER encoded signature", der, der_len);
	md_ctx = EVP_MD_CTX_create();
	if (!md_ctx)
		goto fail;

	ERR_clear_error();
	if (EVP_DigestVerifyInit(md_ctx, NULL, sign_md, NULL, csign_pub) != 1) {
		wpa_printf(MSG_DEBUG, "DPP: EVP_DigestVerifyInit failed: %s",
			   ERR_error_string(ERR_get_error(), NULL));
		goto fail;
	}
	if (EVP_DigestVerifyUpdate(md_ctx, signed_start,
				   signed_end - signed_start + 1) != 1) {
		wpa_printf(MSG_DEBUG, "DPP: EVP_DigestVerifyUpdate failed: %s",
			   ERR_error_string(ERR_get_error(), NULL));
		goto fail;
	}
	res = EVP_DigestVerifyFinal(md_ctx, der, der_len);
	if (res != 1) {
		wpa_printf(MSG_DEBUG,
			   "DPP: EVP_DigestVerifyFinal failed (res=%d): %s",
			   res, ERR_error_string(ERR_get_error(), NULL));
		ret = DPP_STATUS_INVALID_CONNECTOR;
		goto fail;
	}

	ret = DPP_STATUS_OK;
fail:
	EC_KEY_free(eckey);
	EVP_MD_CTX_destroy(md_ctx);
	os_free(prot_hdr);
	wpabuf_free(kid);
	os_free(signature);
	ECDSA_SIG_free(sig);
	BN_free(r);
	BN_free(s);
	OPENSSL_free(der);
	return ret;
}


static int dpp_parse_cred_dpp(struct dpp_authentication *auth,
			      struct json_token *cred)
{
	struct dpp_signed_connector_info info;
	struct json_token *token, *csign;
	int ret = -1;
	EVP_PKEY *csign_pub = NULL;
	const struct dpp_curve_params *key_curve = NULL;
	const char *signed_connector;

	os_memset(&info, 0, sizeof(info));

	wpa_printf(MSG_DEBUG, "DPP: Connector credential");

	csign = json_get_member(cred, "csign");
	if (!csign || csign->type != JSON_OBJECT) {
		wpa_printf(MSG_DEBUG, "DPP: No csign JWK in JSON");
		goto fail;
	}

	csign_pub = dpp_parse_jwk(csign, &key_curve);
	if (!csign_pub) {
		wpa_printf(MSG_DEBUG, "DPP: Failed to parse csign JWK");
		goto fail;
	}
	dpp_debug_print_key("DPP: Received C-sign-key", csign_pub);

	token = json_get_member(cred, "signedConnector");
	if (!token || token->type != JSON_STRING) {
		wpa_printf(MSG_DEBUG, "DPP: No signedConnector string found");
		goto fail;
	}
	wpa_hexdump_ascii(MSG_DEBUG, "DPP: signedConnector",
			  token->string, os_strlen(token->string));
	signed_connector = token->string;

	if (os_strchr(signed_connector, '"') ||
	    os_strchr(signed_connector, '\n')) {
		wpa_printf(MSG_DEBUG,
			   "DPP: Unexpected character in signedConnector");
		goto fail;
	}

	if (dpp_process_signed_connector(&info, csign_pub,
					 signed_connector) != DPP_STATUS_OK)
		goto fail;

	if (dpp_parse_connector(auth, info.payload, info.payload_len) < 0) {
		wpa_printf(MSG_DEBUG, "DPP: Failed to parse connector");
		goto fail;
	}

	os_free(auth->connector);
	auth->connector = os_strdup(signed_connector);

	dpp_copy_csign(auth, csign_pub);
	dpp_copy_netaccesskey(auth);

	ret = 0;
fail:
	EVP_PKEY_free(csign_pub);
	os_free(info.payload);
	return ret;
}


const char * dpp_akm_str(enum dpp_akm akm)
{
	switch (akm) {
	case DPP_AKM_DPP:
		return "dpp";
	case DPP_AKM_PSK:
		return "psk";
	case DPP_AKM_SAE:
		return "sae";
	case DPP_AKM_PSK_SAE:
		return "psk+sae";
	default:
		return "??";
	}
}


static enum dpp_akm dpp_akm_from_str(const char *akm)
{
	if (os_strcmp(akm, "psk") == 0)
		return DPP_AKM_PSK;
	if (os_strcmp(akm, "sae") == 0)
		return DPP_AKM_SAE;
	if (os_strcmp(akm, "psk+sae") == 0)
		return DPP_AKM_PSK_SAE;
	if (os_strcmp(akm, "dpp") == 0)
		return DPP_AKM_DPP;
	return DPP_AKM_UNKNOWN;
}


static int dpp_parse_conf_obj(struct dpp_authentication *auth,
			      const u8 *conf_obj, u16 conf_obj_len)
{
	int ret = -1;
	struct json_token *root, *token, *discovery, *cred;

	root = json_parse((const char *) conf_obj, conf_obj_len);
	if (!root)
		return -1;
	if (root->type != JSON_OBJECT) {
		dpp_auth_fail(auth, "JSON root is not an object");
		goto fail;
	}

	token = json_get_member(root, "wi-fi_tech");
	if (!token || token->type != JSON_STRING) {
		dpp_auth_fail(auth, "No wi-fi_tech string value found");
		goto fail;
	}
	if (os_strcmp(token->string, "infra") != 0) {
		wpa_printf(MSG_DEBUG, "DPP: Unsupported wi-fi_tech value: '%s'",
			   token->string);
		dpp_auth_fail(auth, "Unsupported wi-fi_tech value");
		goto fail;
	}

	discovery = json_get_member(root, "discovery");
	if (!discovery || discovery->type != JSON_OBJECT) {
		dpp_auth_fail(auth, "No discovery object in JSON");
		goto fail;
	}

	token = json_get_member(discovery, "ssid");
	if (!token || token->type != JSON_STRING) {
		dpp_auth_fail(auth, "No discovery::ssid string value found");
		goto fail;
	}
	wpa_hexdump_ascii(MSG_DEBUG, "DPP: discovery::ssid",
			  token->string, os_strlen(token->string));
	if (os_strlen(token->string) > SSID_MAX_LEN) {
		dpp_auth_fail(auth, "Too long discovery::ssid string value");
		goto fail;
	}
	auth->ssid_len = os_strlen(token->string);
	os_memcpy(auth->ssid, token->string, auth->ssid_len);

	cred = json_get_member(root, "cred");
	if (!cred || cred->type != JSON_OBJECT) {
		dpp_auth_fail(auth, "No cred object in JSON");
		goto fail;
	}

	token = json_get_member(cred, "akm");
	if (!token || token->type != JSON_STRING) {
		dpp_auth_fail(auth, "No cred::akm string value found");
		goto fail;
	}
	auth->akm = dpp_akm_from_str(token->string);

	if (auth->akm == DPP_AKM_PSK || auth->akm == DPP_AKM_SAE ||
	    auth->akm == DPP_AKM_PSK_SAE) {
		if (dpp_parse_cred_legacy(auth, cred) < 0)
			goto fail;
	} else if (auth->akm == DPP_AKM_DPP) {
		if (dpp_parse_cred_dpp(auth, cred) < 0)
			goto fail;
	} else {
		wpa_printf(MSG_DEBUG, "DPP: Unsupported akm: %s",
			   token->string);
		dpp_auth_fail(auth, "Unsupported akm");
		goto fail;
	}

	wpa_printf(MSG_DEBUG, "DPP: JSON parsing completed successfully");
	ret = 0;
fail:
	json_free(root);
	return ret;
}


int dpp_conf_resp_rx(struct dpp_authentication *auth,
		     const struct wpabuf *resp)
{
	const u8 *wrapped_data, *e_nonce, *status, *conf_obj;
	u16 wrapped_data_len, e_nonce_len, status_len, conf_obj_len;
	const u8 *addr[1];
	size_t len[1];
	u8 *unwrapped = NULL;
	size_t unwrapped_len = 0;
	int ret = -1;

	if (dpp_check_attrs(wpabuf_head(resp), wpabuf_len(resp)) < 0) {
		dpp_auth_fail(auth, "Invalid attribute in config response");
		return -1;
	}

	wrapped_data = dpp_get_attr(wpabuf_head(resp), wpabuf_len(resp),
				    DPP_ATTR_WRAPPED_DATA,
				    &wrapped_data_len);
	if (!wrapped_data || wrapped_data_len < AES_BLOCK_SIZE) {
		dpp_auth_fail(auth,
			      "Missing or invalid required Wrapped Data attribute");
		return -1;
	}

	wpa_hexdump(MSG_DEBUG, "DPP: AES-SIV ciphertext",
		    wrapped_data, wrapped_data_len);
	unwrapped_len = wrapped_data_len - AES_BLOCK_SIZE;
	unwrapped = os_malloc(unwrapped_len);
	if (!unwrapped)
		return -1;

	addr[0] = wpabuf_head(resp);
	len[0] = wrapped_data - 4 - (const u8 *) wpabuf_head(resp);
	wpa_hexdump(MSG_DEBUG, "DDP: AES-SIV AD", addr[0], len[0]);

	if (aes_siv_decrypt(auth->ke, auth->curve->hash_len,
			    wrapped_data, wrapped_data_len,
			    1, addr, len, unwrapped) < 0) {
		dpp_auth_fail(auth, "AES-SIV decryption failed");
		goto fail;
	}
	wpa_hexdump(MSG_DEBUG, "DPP: AES-SIV cleartext",
		    unwrapped, unwrapped_len);

	if (dpp_check_attrs(unwrapped, unwrapped_len) < 0) {
		dpp_auth_fail(auth, "Invalid attribute in unwrapped data");
		goto fail;
	}

	e_nonce = dpp_get_attr(unwrapped, unwrapped_len,
			       DPP_ATTR_ENROLLEE_NONCE,
			       &e_nonce_len);
	if (!e_nonce || e_nonce_len != auth->curve->nonce_len) {
		dpp_auth_fail(auth,
			      "Missing or invalid Enrollee Nonce attribute");
		goto fail;
	}
	wpa_hexdump(MSG_DEBUG, "DPP: Enrollee Nonce", e_nonce, e_nonce_len);
	if (os_memcmp(e_nonce, auth->e_nonce, e_nonce_len) != 0) {
		dpp_auth_fail(auth, "Enrollee Nonce mismatch");
		goto fail;
	}

	status = dpp_get_attr(wpabuf_head(resp), wpabuf_len(resp),
			      DPP_ATTR_STATUS, &status_len);
	if (!status || status_len < 1) {
		dpp_auth_fail(auth,
			      "Missing or invalid required DPP Status attribute");
		goto fail;
	}
	wpa_printf(MSG_DEBUG, "DPP: Status %u", status[0]);
	if (status[0] != DPP_STATUS_OK) {
		dpp_auth_fail(auth, "Configurator rejected configuration");
		goto fail;
	}

	conf_obj = dpp_get_attr(unwrapped, unwrapped_len,
				DPP_ATTR_CONFIG_OBJ, &conf_obj_len);
	if (!conf_obj) {
		dpp_auth_fail(auth,
			      "Missing required Configuration Object attribute");
		goto fail;
	}
	wpa_hexdump_ascii(MSG_DEBUG, "DPP: configurationObject JSON",
			  conf_obj, conf_obj_len);
	if (dpp_parse_conf_obj(auth, conf_obj, conf_obj_len) < 0)
		goto fail;

	ret = 0;

fail:
	os_free(unwrapped);
	return ret;
}


void dpp_configurator_free(struct dpp_configurator *conf)
{
	if (!conf)
		return;
	EVP_PKEY_free(conf->csign);
	os_free(conf->kid);
	os_free(conf);
}


int dpp_configurator_get_key(const struct dpp_configurator *conf, char *buf,
			     size_t buflen)
{
	EC_KEY *eckey;
	int key_len, ret = -1;
	unsigned char *key = NULL;

	if (!conf->csign)
		return -1;

	eckey = EVP_PKEY_get1_EC_KEY(conf->csign);
	if (!eckey)
		return -1;

	key_len = i2d_ECPrivateKey(eckey, &key);
	if (key_len > 0)
		ret = wpa_snprintf_hex(buf, buflen, key, key_len);

	EC_KEY_free(eckey);
	OPENSSL_free(key);
	return ret;
}


struct dpp_configurator *
dpp_keygen_configurator(const char *curve, const u8 *privkey,
			size_t privkey_len)
{
	struct dpp_configurator *conf;
	struct wpabuf *csign_pub = NULL;
	u8 kid_hash[SHA256_MAC_LEN];
	const u8 *addr[1];
	size_t len[1];

	conf = os_zalloc(sizeof(*conf));
	if (!conf)
		return NULL;

	if (!curve) {
		conf->curve = &dpp_curves[0];
	} else {
		conf->curve = dpp_get_curve_name(curve);
		if (!conf->curve) {
			wpa_printf(MSG_INFO, "DPP: Unsupported curve: %s",
				   curve);
			os_free(conf);
			return NULL;
		}
	}
	if (privkey)
		conf->csign = dpp_set_keypair(&conf->curve, privkey,
					      privkey_len);
	else
		conf->csign = dpp_gen_keypair(conf->curve);
	if (!conf->csign)
		goto fail;
	conf->own = 1;

	csign_pub = dpp_get_pubkey_point(conf->csign, 1);
	if (!csign_pub) {
		wpa_printf(MSG_INFO, "DPP: Failed to extract C-sign-key");
		goto fail;
	}

	/* kid = SHA256(ANSI X9.63 uncompressed C-sign-key) */
	addr[0] = wpabuf_head(csign_pub);
	len[0] = wpabuf_len(csign_pub);
	if (sha256_vector(1, addr, len, kid_hash) < 0) {
		wpa_printf(MSG_DEBUG,
			   "DPP: Failed to derive kid for C-sign-key");
		goto fail;
	}

	conf->kid = (char *) base64_url_encode(kid_hash, sizeof(kid_hash),
					       NULL, 0);
	if (!conf->kid)
		goto fail;
out:
	wpabuf_free(csign_pub);
	return conf;
fail:
	dpp_configurator_free(conf);
	conf = NULL;
	goto out;
}


int dpp_configurator_own_config(struct dpp_authentication *auth,
				const char *curve, int ap)
{
	struct wpabuf *conf_obj;
	int ret = -1;

	if (!auth->conf) {
		wpa_printf(MSG_DEBUG, "DPP: No configurator specified");
		return -1;
	}

	if (!curve) {
		auth->curve = &dpp_curves[0];
	} else {
		auth->curve = dpp_get_curve_name(curve);
		if (!auth->curve) {
			wpa_printf(MSG_INFO, "DPP: Unsupported curve: %s",
				   curve);
			return -1;
		}
	}
	wpa_printf(MSG_DEBUG,
		   "DPP: Building own configuration/connector with curve %s",
		   auth->curve->name);

	auth->own_protocol_key = dpp_gen_keypair(auth->curve);
	if (!auth->own_protocol_key)
		return -1;
	dpp_copy_netaccesskey(auth);
	auth->peer_protocol_key = auth->own_protocol_key;
	dpp_copy_csign(auth, auth->conf->csign);

	conf_obj = dpp_build_conf_obj(auth, ap);
	if (!conf_obj)
		goto fail;
	ret = dpp_parse_conf_obj(auth, wpabuf_head(conf_obj),
				 wpabuf_len(conf_obj));
fail:
	wpabuf_free(conf_obj);
	auth->peer_protocol_key = NULL;
	return ret;
}


static int dpp_compatible_netrole(const char *role1, const char *role2)
{
	return (os_strcmp(role1, "sta") == 0 && os_strcmp(role2, "ap") == 0) ||
		(os_strcmp(role1, "ap") == 0 && os_strcmp(role2, "sta") == 0);
}


static int dpp_connector_compatible_group(struct json_token *root,
					  const char *group_id,
					  const char *net_role)
{
	struct json_token *groups, *token;

	groups = json_get_member(root, "groups");
	if (!groups || groups->type != JSON_ARRAY)
		return 0;

	for (token = groups->child; token; token = token->sibling) {
		struct json_token *id, *role;

		id = json_get_member(token, "groupId");
		if (!id || id->type != JSON_STRING)
			continue;

		role = json_get_member(token, "netRole");
		if (!role || role->type != JSON_STRING)
			continue;

		if (os_strcmp(id->string, "*") != 0 &&
		    os_strcmp(group_id, "*") != 0 &&
		    os_strcmp(id->string, group_id) != 0)
			continue;

		if (dpp_compatible_netrole(role->string, net_role))
			return 1;
	}

	return 0;
}


static int dpp_connector_match_groups(struct json_token *own_root,
				      struct json_token *peer_root)
{
	struct json_token *groups, *token;

	groups = json_get_member(peer_root, "groups");
	if (!groups || groups->type != JSON_ARRAY) {
		wpa_printf(MSG_DEBUG, "DPP: No peer groups array found");
		return 0;
	}

	for (token = groups->child; token; token = token->sibling) {
		struct json_token *id, *role;

		id = json_get_member(token, "groupId");
		if (!id || id->type != JSON_STRING) {
			wpa_printf(MSG_DEBUG,
				   "DPP: Missing peer groupId string");
			continue;
		}

		role = json_get_member(token, "netRole");
		if (!role || role->type != JSON_STRING) {
			wpa_printf(MSG_DEBUG,
				   "DPP: Missing peer groups::netRole string");
			continue;
		}
		wpa_printf(MSG_DEBUG,
			   "DPP: peer connector group: groupId='%s' netRole='%s'",
			   id->string, role->string);
		if (dpp_connector_compatible_group(own_root, id->string,
						   role->string)) {
			wpa_printf(MSG_DEBUG,
				   "DPP: Compatible group/netRole in own connector");
			return 1;
		}
	}

	return 0;
}


static int dpp_derive_pmk(const u8 *Nx, size_t Nx_len, u8 *pmk,
			  unsigned int hash_len)
{
	u8 salt[DPP_MAX_HASH_LEN], prk[DPP_MAX_HASH_LEN];
	const char *info = "DPP PMK";
	int res;

	/* PMK = HKDF(<>, "DPP PMK", N.x) */

	/* HKDF-Extract(<>, N.x) */
	os_memset(salt, 0, hash_len);
	if (dpp_hmac(hash_len, salt, hash_len, Nx, Nx_len, prk) < 0)
		return -1;
	wpa_hexdump_key(MSG_DEBUG, "DPP: PRK = HKDF-Extract(<>, IKM=N.x)",
			prk, hash_len);

	/* HKDF-Expand(PRK, info, L) */
	res = dpp_hkdf_expand(hash_len, prk, hash_len, info, pmk, hash_len);
	os_memset(prk, 0, hash_len);
	if (res < 0)
		return -1;

	wpa_hexdump_key(MSG_DEBUG, "DPP: PMK = HKDF-Expand(PRK, info, L)",
			pmk, hash_len);
	return 0;
}


static int dpp_derive_pmkid(const struct dpp_curve_params *curve,
			    EVP_PKEY *own_key, EVP_PKEY *peer_key, u8 *pmkid)
{
	struct wpabuf *nkx, *pkx;
	int ret = -1, res;
	const u8 *addr[2];
	size_t len[2];
	u8 hash[SHA256_MAC_LEN];

	/* PMKID = Truncate-128(H(min(NK.x, PK.x) | max(NK.x, PK.x))) */
	nkx = dpp_get_pubkey_point(own_key, 0);
	pkx = dpp_get_pubkey_point(peer_key, 0);
	if (!nkx || !pkx)
		goto fail;
	addr[0] = wpabuf_head(nkx);
	len[0] = wpabuf_len(nkx) / 2;
	addr[1] = wpabuf_head(pkx);
	len[1] = wpabuf_len(pkx) / 2;
	if (len[0] != len[1])
		goto fail;
	if (os_memcmp(addr[0], addr[1], len[0]) > 0) {
		addr[0] = wpabuf_head(pkx);
		addr[1] = wpabuf_head(nkx);
	}
	wpa_hexdump(MSG_DEBUG, "DPP: PMKID hash payload 1", addr[0], len[0]);
	wpa_hexdump(MSG_DEBUG, "DPP: PMKID hash payload 2", addr[1], len[1]);
	res = sha256_vector(2, addr, len, hash);
	if (res < 0)
		goto fail;
	wpa_hexdump(MSG_DEBUG, "DPP: PMKID hash output", hash, SHA256_MAC_LEN);
	os_memcpy(pmkid, hash, PMKID_LEN);
	wpa_hexdump(MSG_DEBUG, "DPP: PMKID", pmkid, PMKID_LEN);
	ret = 0;
fail:
	wpabuf_free(nkx);
	wpabuf_free(pkx);
	return ret;
}


enum dpp_status_error
dpp_peer_intro(struct dpp_introduction *intro, const char *own_connector,
	       const u8 *net_access_key, size_t net_access_key_len,
	       const u8 *csign_key, size_t csign_key_len,
	       const u8 *peer_connector, size_t peer_connector_len,
	       os_time_t *expiry)
{
	struct json_token *root = NULL, *netkey, *token;
	struct json_token *own_root = NULL;
	enum dpp_status_error ret = 255, res;
	EVP_PKEY *own_key = NULL, *peer_key = NULL;
	struct wpabuf *own_key_pub = NULL;
	const struct dpp_curve_params *curve, *own_curve;
	struct dpp_signed_connector_info info;
	const unsigned char *p;
	EVP_PKEY *csign = NULL;
	char *signed_connector = NULL;
	const char *pos, *end;
	unsigned char *own_conn = NULL;
	size_t own_conn_len;
	EVP_PKEY_CTX *ctx = NULL;
	size_t Nx_len;
	u8 Nx[DPP_MAX_SHARED_SECRET_LEN];

	os_memset(intro, 0, sizeof(*intro));
	os_memset(&info, 0, sizeof(info));
	if (expiry)
		*expiry = 0;

	p = csign_key;
	csign = d2i_PUBKEY(NULL, &p, csign_key_len);
	if (!csign) {
		wpa_printf(MSG_ERROR,
			   "DPP: Failed to parse local C-sign-key information");
		goto fail;
	}

	own_key = dpp_set_keypair(&own_curve, net_access_key,
				  net_access_key_len);
	if (!own_key) {
		wpa_printf(MSG_ERROR, "DPP: Failed to parse own netAccessKey");
		goto fail;
	}

	pos = os_strchr(own_connector, '.');
	if (!pos) {
		wpa_printf(MSG_DEBUG, "DPP: Own connector is missing the first dot (.)");
		goto fail;
	}
	pos++;
	end = os_strchr(pos, '.');
	if (!end) {
		wpa_printf(MSG_DEBUG, "DPP: Own connector is missing the second dot (.)");
		goto fail;
	}
	own_conn = base64_url_decode((const unsigned char *) pos,
				     end - pos, &own_conn_len);
	if (!own_conn) {
		wpa_printf(MSG_DEBUG,
			   "DPP: Failed to base64url decode own signedConnector JWS Payload");
		goto fail;
	}

	own_root = json_parse((const char *) own_conn, own_conn_len);
	if (!own_root) {
		wpa_printf(MSG_DEBUG, "DPP: Failed to parse local connector");
		goto fail;
	}

	wpa_hexdump_ascii(MSG_DEBUG, "DPP: Peer signedConnector",
			  peer_connector, peer_connector_len);
	signed_connector = os_malloc(peer_connector_len + 1);
	if (!signed_connector)
		goto fail;
	os_memcpy(signed_connector, peer_connector, peer_connector_len);
	signed_connector[peer_connector_len] = '\0';

	res = dpp_process_signed_connector(&info, csign, signed_connector);
	if (res != DPP_STATUS_OK) {
		ret = res;
		goto fail;
	}

	root = json_parse((const char *) info.payload, info.payload_len);
	if (!root) {
		wpa_printf(MSG_DEBUG, "DPP: JSON parsing of connector failed");
		ret = DPP_STATUS_INVALID_CONNECTOR;
		goto fail;
	}

	if (!dpp_connector_match_groups(own_root, root)) {
		wpa_printf(MSG_DEBUG,
			   "DPP: Peer connector does not include compatible group netrole with own connector");
		ret = DPP_STATUS_NO_MATCH;
		goto fail;
	}

	token = json_get_member(root, "expiry");
	if (!token || token->type != JSON_STRING) {
		wpa_printf(MSG_DEBUG,
			   "DPP: No expiry string found - connector does not expire");
	} else {
		wpa_printf(MSG_DEBUG, "DPP: expiry = %s", token->string);
		if (dpp_key_expired(token->string, expiry)) {
			wpa_printf(MSG_DEBUG,
				   "DPP: Connector (netAccessKey) has expired");
			ret = DPP_STATUS_INVALID_CONNECTOR;
			goto fail;
		}
	}

	netkey = json_get_member(root, "netAccessKey");
	if (!netkey || netkey->type != JSON_OBJECT) {
		wpa_printf(MSG_DEBUG, "DPP: No netAccessKey object found");
		ret = DPP_STATUS_INVALID_CONNECTOR;
		goto fail;
	}

	peer_key = dpp_parse_jwk(netkey, &curve);
	if (!peer_key) {
		ret = DPP_STATUS_INVALID_CONNECTOR;
		goto fail;
	}
	dpp_debug_print_key("DPP: Received netAccessKey", peer_key);

	if (own_curve != curve) {
		wpa_printf(MSG_DEBUG,
			   "DPP: Mismatching netAccessKey curves (%s != %s)",
			   own_curve->name, curve->name);
		ret = DPP_STATUS_INVALID_CONNECTOR;
		goto fail;
	}

	/* ECDH: N = nk * PK */
	ctx = EVP_PKEY_CTX_new(own_key, NULL);
	if (!ctx ||
	    EVP_PKEY_derive_init(ctx) != 1 ||
	    EVP_PKEY_derive_set_peer(ctx, peer_key) != 1 ||
	    EVP_PKEY_derive(ctx, NULL, &Nx_len) != 1 ||
	    Nx_len > DPP_MAX_SHARED_SECRET_LEN ||
	    EVP_PKEY_derive(ctx, Nx, &Nx_len) != 1) {
		wpa_printf(MSG_ERROR,
			   "DPP: Failed to derive ECDH shared secret: %s",
			   ERR_error_string(ERR_get_error(), NULL));
		goto fail;
	}

	wpa_hexdump_key(MSG_DEBUG, "DPP: ECDH shared secret (N.x)",
			Nx, Nx_len);

	/* PMK = HKDF(<>, "DPP PMK", N.x) */
	if (dpp_derive_pmk(Nx, Nx_len, intro->pmk, curve->hash_len) < 0) {
		wpa_printf(MSG_ERROR, "DPP: Failed to derive PMK");
		goto fail;
	}
	intro->pmk_len = curve->hash_len;

	/* PMKID = Truncate-128(H(min(NK.x, PK.x) | max(NK.x, PK.x))) */
	if (dpp_derive_pmkid(curve, own_key, peer_key, intro->pmkid) < 0) {
		wpa_printf(MSG_ERROR, "DPP: Failed to derive PMKID");
		goto fail;
	}

	ret = DPP_STATUS_OK;
fail:
	if (ret != DPP_STATUS_OK)
		os_memset(intro, 0, sizeof(*intro));
	os_memset(Nx, 0, sizeof(Nx));
	EVP_PKEY_CTX_free(ctx);
	os_free(own_conn);
	os_free(signed_connector);
	os_free(info.payload);
	EVP_PKEY_free(own_key);
	wpabuf_free(own_key_pub);
	EVP_PKEY_free(peer_key);
	EVP_PKEY_free(csign);
	json_free(root);
	json_free(own_root);
	return ret;
}


static EVP_PKEY * dpp_pkex_get_role_elem(const struct dpp_curve_params *curve,
					 int init)
{
	EC_GROUP *group;
	size_t len = curve->prime_len;
	const u8 *x, *y;

	switch (curve->ike_group) {
	case 19:
		x = init ? pkex_init_x_p256 : pkex_resp_x_p256;
		y = init ? pkex_init_y_p256 : pkex_resp_y_p256;
		break;
	case 20:
		x = init ? pkex_init_x_p384 : pkex_resp_x_p384;
		y = init ? pkex_init_y_p384 : pkex_resp_y_p384;
		break;
	case 21:
		x = init ? pkex_init_x_p521 : pkex_resp_x_p521;
		y = init ? pkex_init_y_p521 : pkex_resp_y_p521;
		break;
	case 28:
		x = init ? pkex_init_x_bp_p256r1 : pkex_resp_x_bp_p256r1;
		y = init ? pkex_init_y_bp_p256r1 : pkex_resp_y_bp_p256r1;
		break;
	case 29:
		x = init ? pkex_init_x_bp_p384r1 : pkex_resp_x_bp_p384r1;
		y = init ? pkex_init_y_bp_p384r1 : pkex_resp_y_bp_p384r1;
		break;
	case 30:
		x = init ? pkex_init_x_bp_p512r1 : pkex_resp_x_bp_p512r1;
		y = init ? pkex_init_y_bp_p512r1 : pkex_resp_y_bp_p512r1;
		break;
	default:
		return NULL;
	}

	group = EC_GROUP_new_by_curve_name(OBJ_txt2nid(curve->name));
	if (!group)
		return NULL;
	return dpp_set_pubkey_point_group(group, x, y, len);
}


static EC_POINT * dpp_pkex_derive_Qi(const struct dpp_curve_params *curve,
				     const u8 *mac_init, const char *code,
				     const char *identifier, BN_CTX *bnctx,
				     const EC_GROUP **ret_group)
{
	u8 hash[DPP_MAX_HASH_LEN];
	const u8 *addr[3];
	size_t len[3];
	unsigned int num_elem = 0;
	EC_POINT *Qi = NULL;
	EVP_PKEY *Pi = NULL;
	EC_KEY *Pi_ec = NULL;
	const EC_POINT *Pi_point;
	BIGNUM *hash_bn = NULL;
	const EC_GROUP *group = NULL;
	EC_GROUP *group2 = NULL;

	/* Qi = H(MAC-Initiator | [identifier |] code) * Pi */

	wpa_printf(MSG_DEBUG, "DPP: MAC-Initiator: " MACSTR, MAC2STR(mac_init));
	addr[num_elem] = mac_init;
	len[num_elem] = ETH_ALEN;
	num_elem++;
	if (identifier) {
		wpa_printf(MSG_DEBUG, "DPP: code identifier: %s",
			   identifier);
		addr[num_elem] = (const u8 *) identifier;
		len[num_elem] = os_strlen(identifier);
		num_elem++;
	}
	wpa_hexdump_ascii_key(MSG_DEBUG, "DPP: code", code, os_strlen(code));
	addr[num_elem] = (const u8 *) code;
	len[num_elem] = os_strlen(code);
	num_elem++;
	if (dpp_hash_vector(curve, num_elem, addr, len, hash) < 0)
		goto fail;
	wpa_hexdump_key(MSG_DEBUG,
			"DPP: H(MAC-Initiator | [identifier |] code)",
			hash, curve->hash_len);
	Pi = dpp_pkex_get_role_elem(curve, 1);
	if (!Pi)
		goto fail;
	dpp_debug_print_key("DPP: Pi", Pi);
	Pi_ec = EVP_PKEY_get1_EC_KEY(Pi);
	if (!Pi_ec)
		goto fail;
	Pi_point = EC_KEY_get0_public_key(Pi_ec);

	group = EC_KEY_get0_group(Pi_ec);
	if (!group)
		goto fail;
	group2 = EC_GROUP_dup(group);
	if (!group2)
		goto fail;
	Qi = EC_POINT_new(group2);
	if (!Qi) {
		EC_GROUP_free(group2);
		goto fail;
	}
	hash_bn = BN_bin2bn(hash, curve->hash_len, NULL);
	if (!hash_bn ||
	    EC_POINT_mul(group2, Qi, NULL, Pi_point, hash_bn, bnctx) != 1)
		goto fail;
	if (EC_POINT_is_at_infinity(group, Qi)) {
		wpa_printf(MSG_INFO, "DPP: Qi is the point-at-infinity");
		goto fail;
	}
	dpp_debug_print_point("DPP: Qi", group, Qi);
out:
	EC_KEY_free(Pi_ec);
	EVP_PKEY_free(Pi);
	BN_clear_free(hash_bn);
	if (ret_group)
		*ret_group = group2;
	return Qi;
fail:
	EC_POINT_free(Qi);
	Qi = NULL;
	goto out;
}


static EC_POINT * dpp_pkex_derive_Qr(const struct dpp_curve_params *curve,
				     const u8 *mac_resp, const char *code,
				     const char *identifier, BN_CTX *bnctx,
				     const EC_GROUP **ret_group)
{
	u8 hash[DPP_MAX_HASH_LEN];
	const u8 *addr[3];
	size_t len[3];
	unsigned int num_elem = 0;
	EC_POINT *Qr = NULL;
	EVP_PKEY *Pr = NULL;
	EC_KEY *Pr_ec = NULL;
	const EC_POINT *Pr_point;
	BIGNUM *hash_bn = NULL;
	const EC_GROUP *group = NULL;
	EC_GROUP *group2 = NULL;

	/* Qr = H(MAC-Responder | | [identifier | ] code) * Pr */

	wpa_printf(MSG_DEBUG, "DPP: MAC-Responder: " MACSTR, MAC2STR(mac_resp));
	addr[num_elem] = mac_resp;
	len[num_elem] = ETH_ALEN;
	num_elem++;
	if (identifier) {
		wpa_printf(MSG_DEBUG, "DPP: code identifier: %s",
			   identifier);
		addr[num_elem] = (const u8 *) identifier;
		len[num_elem] = os_strlen(identifier);
		num_elem++;
	}
	wpa_hexdump_ascii_key(MSG_DEBUG, "DPP: code", code, os_strlen(code));
	addr[num_elem] = (const u8 *) code;
	len[num_elem] = os_strlen(code);
	num_elem++;
	if (dpp_hash_vector(curve, num_elem, addr, len, hash) < 0)
		goto fail;
	wpa_hexdump_key(MSG_DEBUG,
			"DPP: H(MAC-Responder | [identifier |] code)",
			hash, curve->hash_len);
	Pr = dpp_pkex_get_role_elem(curve, 0);
	if (!Pr)
		goto fail;
	dpp_debug_print_key("DPP: Pr", Pr);
	Pr_ec = EVP_PKEY_get1_EC_KEY(Pr);
	if (!Pr_ec)
		goto fail;
	Pr_point = EC_KEY_get0_public_key(Pr_ec);

	group = EC_KEY_get0_group(Pr_ec);
	if (!group)
		goto fail;
	group2 = EC_GROUP_dup(group);
	if (!group2)
		goto fail;
	Qr = EC_POINT_new(group2);
	if (!Qr) {
		EC_GROUP_free(group2);
		goto fail;
	}
	hash_bn = BN_bin2bn(hash, curve->hash_len, NULL);
	if (!hash_bn ||
	    EC_POINT_mul(group2, Qr, NULL, Pr_point, hash_bn, bnctx) != 1)
		goto fail;
	if (EC_POINT_is_at_infinity(group, Qr)) {
		wpa_printf(MSG_INFO, "DPP: Qr is the point-at-infinity");
		goto fail;
	}
	dpp_debug_print_point("DPP: Qr", group, Qr);
out:
	EC_KEY_free(Pr_ec);
	EVP_PKEY_free(Pr);
	BN_clear_free(hash_bn);
	if (ret_group)
		*ret_group = group2;
	return Qr;
fail:
	EC_POINT_free(Qr);
	Qr = NULL;
	goto out;
}


#ifdef CONFIG_TESTING_OPTIONS
static int dpp_test_gen_invalid_key(struct wpabuf *msg,
				    const struct dpp_curve_params *curve)
{
	BN_CTX *ctx;
	BIGNUM *x, *y;
	int ret = -1;
	EC_GROUP *group;
	EC_POINT *point;

	group = EC_GROUP_new_by_curve_name(OBJ_txt2nid(curve->name));
	if (!group)
		return -1;

	ctx = BN_CTX_new();
	point = EC_POINT_new(group);
	x = BN_new();
	y = BN_new();
	if (!ctx || !point || !x || !y)
		goto fail;

	if (BN_rand(x, curve->prime_len * 8, 0, 0) != 1)
		goto fail;

	/* Generate a random y coordinate that results in a point that is not
	 * on the curve. */
	for (;;) {
		if (BN_rand(y, curve->prime_len * 8, 0, 0) != 1)
			goto fail;

		if (EC_POINT_set_affine_coordinates_GFp(group, point, x, y,
							ctx) != 1) {
#if OPENSSL_VERSION_NUMBER >= 0x10100000L || defined(OPENSSL_IS_BORINGSSL)
		/* Unlike older OpenSSL versions, OpenSSL 1.1.1 and BoringSSL
		 * return an error from EC_POINT_set_affine_coordinates_GFp()
		 * when the point is not on the curve. */
			break;
#else /* >=1.1.0 or OPENSSL_IS_BORINGSSL */
			goto fail;
#endif /* >= 1.1.0 or OPENSSL_IS_BORINGSSL */
		}

		if (!EC_POINT_is_on_curve(group, point, ctx))
			break;
	}

	if (dpp_bn2bin_pad(x, wpabuf_put(msg, curve->prime_len),
			   curve->prime_len) < 0 ||
	    dpp_bn2bin_pad(y, wpabuf_put(msg, curve->prime_len),
			   curve->prime_len) < 0)
		goto fail;

	ret = 0;
fail:
	if (ret < 0)
		wpa_printf(MSG_INFO, "DPP: Failed to generate invalid key");
	BN_free(x);
	BN_free(y);
	EC_POINT_free(point);
	BN_CTX_free(ctx);

	return ret;
}
#endif /* CONFIG_TESTING_OPTIONS */


static struct wpabuf * dpp_pkex_build_exchange_req(struct dpp_pkex *pkex)
{
	EC_KEY *X_ec = NULL;
	const EC_POINT *X_point;
	BN_CTX *bnctx = NULL;
	const EC_GROUP *group;
	EC_POINT *Qi = NULL, *M = NULL;
	struct wpabuf *M_buf = NULL;
	BIGNUM *Mx = NULL, *My = NULL;
	struct wpabuf *msg = NULL;
	size_t attr_len;
	const struct dpp_curve_params *curve = pkex->own_bi->curve;

	wpa_printf(MSG_DEBUG, "DPP: Build PKEX Exchange Request");

	/* Qi = H(MAC-Initiator | [identifier |] code) * Pi */
	bnctx = BN_CTX_new();
	if (!bnctx)
		goto fail;
	Qi = dpp_pkex_derive_Qi(curve, pkex->own_mac, pkex->code,
				pkex->identifier, bnctx, &group);
	if (!Qi)
		goto fail;

	/* Generate a random ephemeral keypair x/X */
#ifdef CONFIG_TESTING_OPTIONS
	if (dpp_pkex_ephemeral_key_override_len) {
		const struct dpp_curve_params *tmp_curve;

		wpa_printf(MSG_INFO,
			   "DPP: TESTING - override ephemeral key x/X");
		pkex->x = dpp_set_keypair(&tmp_curve,
					  dpp_pkex_ephemeral_key_override,
					  dpp_pkex_ephemeral_key_override_len);
	} else {
		pkex->x = dpp_gen_keypair(curve);
	}
#else /* CONFIG_TESTING_OPTIONS */
	pkex->x = dpp_gen_keypair(curve);
#endif /* CONFIG_TESTING_OPTIONS */
	if (!pkex->x)
		goto fail;

	/* M = X + Qi */
	X_ec = EVP_PKEY_get1_EC_KEY(pkex->x);
	if (!X_ec)
		goto fail;
	X_point = EC_KEY_get0_public_key(X_ec);
	if (!X_point)
		goto fail;
	dpp_debug_print_point("DPP: X", group, X_point);
	M = EC_POINT_new(group);
	Mx = BN_new();
	My = BN_new();
	if (!M || !Mx || !My ||
	    EC_POINT_add(group, M, X_point, Qi, bnctx) != 1 ||
	    EC_POINT_get_affine_coordinates_GFp(group, M, Mx, My, bnctx) != 1)
		goto fail;
	dpp_debug_print_point("DPP: M", group, M);

	/* Initiator -> Responder: group, [identifier,] M */
	attr_len = 4 + 2;
	if (pkex->identifier)
		attr_len += 4 + os_strlen(pkex->identifier);
	attr_len += 4 + 2 * curve->prime_len;
	msg = dpp_alloc_msg(DPP_PA_PKEX_EXCHANGE_REQ, attr_len);
	if (!msg)
		goto fail;

#ifdef CONFIG_TESTING_OPTIONS
	if (dpp_test == DPP_TEST_NO_FINITE_CYCLIC_GROUP_PKEX_EXCHANGE_REQ) {
		wpa_printf(MSG_INFO, "DPP: TESTING - no Finite Cyclic Group");
		goto skip_finite_cyclic_group;
	}
#endif /* CONFIG_TESTING_OPTIONS */

	/* Finite Cyclic Group attribute */
	wpabuf_put_le16(msg, DPP_ATTR_FINITE_CYCLIC_GROUP);
	wpabuf_put_le16(msg, 2);
	wpabuf_put_le16(msg, curve->ike_group);

#ifdef CONFIG_TESTING_OPTIONS
skip_finite_cyclic_group:
#endif /* CONFIG_TESTING_OPTIONS */

	/* Code Identifier attribute */
	if (pkex->identifier) {
		wpabuf_put_le16(msg, DPP_ATTR_CODE_IDENTIFIER);
		wpabuf_put_le16(msg, os_strlen(pkex->identifier));
		wpabuf_put_str(msg, pkex->identifier);
	}

#ifdef CONFIG_TESTING_OPTIONS
	if (dpp_test == DPP_TEST_NO_ENCRYPTED_KEY_PKEX_EXCHANGE_REQ) {
		wpa_printf(MSG_INFO, "DPP: TESTING - no Encrypted Key");
		goto out;
	}
#endif /* CONFIG_TESTING_OPTIONS */

	/* M in Encrypted Key attribute */
	wpabuf_put_le16(msg, DPP_ATTR_ENCRYPTED_KEY);
	wpabuf_put_le16(msg, 2 * curve->prime_len);

#ifdef CONFIG_TESTING_OPTIONS
	if (dpp_test == DPP_TEST_INVALID_ENCRYPTED_KEY_PKEX_EXCHANGE_REQ) {
		wpa_printf(MSG_INFO, "DPP: TESTING - invalid Encrypted Key");
		if (dpp_test_gen_invalid_key(msg, curve) < 0)
			goto fail;
		goto out;
	}
#endif /* CONFIG_TESTING_OPTIONS */

	if (dpp_bn2bin_pad(Mx, wpabuf_put(msg, curve->prime_len),
			   curve->prime_len) < 0 ||
	    dpp_bn2bin_pad(Mx, pkex->Mx, curve->prime_len) < 0 ||
	    dpp_bn2bin_pad(My, wpabuf_put(msg, curve->prime_len),
			   curve->prime_len) < 0)
		goto fail;

out:
	wpabuf_free(M_buf);
	EC_KEY_free(X_ec);
	EC_POINT_free(M);
	EC_POINT_free(Qi);
	BN_clear_free(Mx);
	BN_clear_free(My);
	BN_CTX_free(bnctx);
	return msg;
fail:
	wpa_printf(MSG_INFO, "DPP: Failed to build PKEX Exchange Request");
	wpabuf_free(msg);
	msg = NULL;
	goto out;
}


static void dpp_pkex_fail(struct dpp_pkex *pkex, const char *txt)
{
	wpa_msg(pkex->msg_ctx, MSG_INFO, DPP_EVENT_FAIL "%s", txt);
}


struct dpp_pkex * dpp_pkex_init(void *msg_ctx, struct dpp_bootstrap_info *bi,
				const u8 *own_mac,
				const char *identifier,
				const char *code)
{
	struct dpp_pkex *pkex;

#ifdef CONFIG_TESTING_OPTIONS
	if (!is_zero_ether_addr(dpp_pkex_own_mac_override)) {
		wpa_printf(MSG_INFO, "DPP: TESTING - own_mac override " MACSTR,
			   MAC2STR(dpp_pkex_own_mac_override));
		own_mac = dpp_pkex_own_mac_override;
	}
#endif /* CONFIG_TESTING_OPTIONS */

	pkex = os_zalloc(sizeof(*pkex));
	if (!pkex)
		return NULL;
	pkex->msg_ctx = msg_ctx;
	pkex->initiator = 1;
	pkex->own_bi = bi;
	os_memcpy(pkex->own_mac, own_mac, ETH_ALEN);
	if (identifier) {
		pkex->identifier = os_strdup(identifier);
		if (!pkex->identifier)
			goto fail;
	}
	pkex->code = os_strdup(code);
	if (!pkex->code)
		goto fail;
	pkex->exchange_req = dpp_pkex_build_exchange_req(pkex);
	if (!pkex->exchange_req)
		goto fail;
	return pkex;
fail:
	dpp_pkex_free(pkex);
	return NULL;
}


static struct wpabuf *
dpp_pkex_build_exchange_resp(struct dpp_pkex *pkex,
			     enum dpp_status_error status,
			     const BIGNUM *Nx, const BIGNUM *Ny)
{
	struct wpabuf *msg = NULL;
	size_t attr_len;
	const struct dpp_curve_params *curve = pkex->own_bi->curve;

	/* Initiator -> Responder: DPP Status, [identifier,] N */
	attr_len = 4 + 1;
	if (pkex->identifier)
		attr_len += 4 + os_strlen(pkex->identifier);
	attr_len += 4 + 2 * curve->prime_len;
	msg = dpp_alloc_msg(DPP_PA_PKEX_EXCHANGE_RESP, attr_len);
	if (!msg)
		goto fail;

#ifdef CONFIG_TESTING_OPTIONS
	if (dpp_test == DPP_TEST_NO_STATUS_PKEX_EXCHANGE_RESP) {
		wpa_printf(MSG_INFO, "DPP: TESTING - no Status");
		goto skip_status;
	}

	if (dpp_test == DPP_TEST_INVALID_STATUS_PKEX_EXCHANGE_RESP) {
		wpa_printf(MSG_INFO, "DPP: TESTING - invalid Status");
		status = 255;
	}
#endif /* CONFIG_TESTING_OPTIONS */

	/* DPP Status */
	dpp_build_attr_status(msg, status);

#ifdef CONFIG_TESTING_OPTIONS
skip_status:
#endif /* CONFIG_TESTING_OPTIONS */

	/* Code Identifier attribute */
	if (pkex->identifier) {
		wpabuf_put_le16(msg, DPP_ATTR_CODE_IDENTIFIER);
		wpabuf_put_le16(msg, os_strlen(pkex->identifier));
		wpabuf_put_str(msg, pkex->identifier);
	}

	if (status != DPP_STATUS_OK)
		goto skip_encrypted_key;

#ifdef CONFIG_TESTING_OPTIONS
	if (dpp_test == DPP_TEST_NO_ENCRYPTED_KEY_PKEX_EXCHANGE_RESP) {
		wpa_printf(MSG_INFO, "DPP: TESTING - no Encrypted Key");
		goto skip_encrypted_key;
	}
#endif /* CONFIG_TESTING_OPTIONS */

	/* N in Encrypted Key attribute */
	wpabuf_put_le16(msg, DPP_ATTR_ENCRYPTED_KEY);
	wpabuf_put_le16(msg, 2 * curve->prime_len);

#ifdef CONFIG_TESTING_OPTIONS
	if (dpp_test == DPP_TEST_INVALID_ENCRYPTED_KEY_PKEX_EXCHANGE_RESP) {
		wpa_printf(MSG_INFO, "DPP: TESTING - invalid Encrypted Key");
		if (dpp_test_gen_invalid_key(msg, curve) < 0)
			goto fail;
		goto skip_encrypted_key;
	}
#endif /* CONFIG_TESTING_OPTIONS */

	if (dpp_bn2bin_pad(Nx, wpabuf_put(msg, curve->prime_len),
			   curve->prime_len) < 0 ||
	    dpp_bn2bin_pad(Nx, pkex->Nx, curve->prime_len) < 0 ||
	    dpp_bn2bin_pad(Ny, wpabuf_put(msg, curve->prime_len),
			   curve->prime_len) < 0)
		goto fail;

skip_encrypted_key:
	if (status == DPP_STATUS_BAD_GROUP) {
		/* Finite Cyclic Group attribute */
		wpabuf_put_le16(msg, DPP_ATTR_FINITE_CYCLIC_GROUP);
		wpabuf_put_le16(msg, 2);
		wpabuf_put_le16(msg, curve->ike_group);
	}

	return msg;
fail:
	wpabuf_free(msg);
	return NULL;
}


static int dpp_pkex_derive_z(const u8 *mac_init, const u8 *mac_resp,
			     const u8 *Mx, size_t Mx_len,
			     const u8 *Nx, size_t Nx_len,
			     const char *code,
			     const u8 *Kx, size_t Kx_len,
			     u8 *z, unsigned int hash_len)
{
	u8 salt[DPP_MAX_HASH_LEN], prk[DPP_MAX_HASH_LEN];
	int res;
	u8 *info, *pos;
	size_t info_len;

	/* z = HKDF(<>, MAC-Initiator | MAC-Responder | M.x | N.x | code, K.x)
	 */

	/* HKDF-Extract(<>, IKM=K.x) */
	os_memset(salt, 0, hash_len);
	if (dpp_hmac(hash_len, salt, hash_len, Kx, Kx_len, prk) < 0)
		return -1;
	wpa_hexdump_key(MSG_DEBUG, "DPP: PRK = HKDF-Extract(<>, IKM)",
			prk, hash_len);
	info_len = 2 * ETH_ALEN + Mx_len + Nx_len + os_strlen(code);
	info = os_malloc(info_len);
	if (!info)
		return -1;
	pos = info;
	os_memcpy(pos, mac_init, ETH_ALEN);
	pos += ETH_ALEN;
	os_memcpy(pos, mac_resp, ETH_ALEN);
	pos += ETH_ALEN;
	os_memcpy(pos, Mx, Mx_len);
	pos += Mx_len;
	os_memcpy(pos, Nx, Nx_len);
	pos += Nx_len;
	os_memcpy(pos, code, os_strlen(code));

	/* HKDF-Expand(PRK, info, L) */
	if (hash_len == 32)
		res = hmac_sha256_kdf(prk, hash_len, NULL, info, info_len,
				      z, hash_len);
	else if (hash_len == 48)
		res = hmac_sha384_kdf(prk, hash_len, NULL, info, info_len,
				      z, hash_len);
	else if (hash_len == 64)
		res = hmac_sha512_kdf(prk, hash_len, NULL, info, info_len,
				      z, hash_len);
	else
		res = -1;
	os_free(info);
	os_memset(prk, 0, hash_len);
	if (res < 0)
		return -1;

	wpa_hexdump_key(MSG_DEBUG, "DPP: z = HKDF-Expand(PRK, info, L)",
			z, hash_len);
	return 0;
}


static int dpp_pkex_identifier_match(const u8 *attr_id, u16 attr_id_len,
				     const char *identifier)
{
	if (!attr_id && identifier) {
		wpa_printf(MSG_DEBUG,
			   "DPP: No PKEX code identifier received, but expected one");
		return 0;
	}

	if (attr_id && !identifier) {
		wpa_printf(MSG_DEBUG,
			   "DPP: PKEX code identifier received, but not expecting one");
		return 0;
	}

	if (attr_id && identifier &&
	    (os_strlen(identifier) != attr_id_len ||
	     os_memcmp(identifier, attr_id, attr_id_len) != 0)) {
		wpa_printf(MSG_DEBUG, "DPP: PKEX code identifier mismatch");
		return 0;
	}

	return 1;
}


struct dpp_pkex * dpp_pkex_rx_exchange_req(void *msg_ctx,
					   struct dpp_bootstrap_info *bi,
					   const u8 *own_mac,
					   const u8 *peer_mac,
					   const char *identifier,
					   const char *code,
					   const u8 *buf, size_t len)
{
	const u8 *attr_group, *attr_id, *attr_key;
	u16 attr_group_len, attr_id_len, attr_key_len;
	const struct dpp_curve_params *curve = bi->curve;
	u16 ike_group;
	struct dpp_pkex *pkex = NULL;
	EC_POINT *Qi = NULL, *Qr = NULL, *M = NULL, *X = NULL, *N = NULL;
	BN_CTX *bnctx = NULL;
	const EC_GROUP *group;
	BIGNUM *Mx = NULL, *My = NULL;
	EC_KEY *Y_ec = NULL, *X_ec = NULL;;
	const EC_POINT *Y_point;
	BIGNUM *Nx = NULL, *Ny = NULL;
	u8 Kx[DPP_MAX_SHARED_SECRET_LEN];
	size_t Kx_len;
	int res;
	EVP_PKEY_CTX *ctx = NULL;

	if (bi->pkex_t >= PKEX_COUNTER_T_LIMIT) {
		wpa_msg(msg_ctx, MSG_INFO, DPP_EVENT_FAIL
			"PKEX counter t limit reached - ignore message");
		return NULL;
	}

#ifdef CONFIG_TESTING_OPTIONS
	if (!is_zero_ether_addr(dpp_pkex_peer_mac_override)) {
		wpa_printf(MSG_INFO, "DPP: TESTING - peer_mac override " MACSTR,
			   MAC2STR(dpp_pkex_peer_mac_override));
		peer_mac = dpp_pkex_peer_mac_override;
	}
	if (!is_zero_ether_addr(dpp_pkex_own_mac_override)) {
		wpa_printf(MSG_INFO, "DPP: TESTING - own_mac override " MACSTR,
			   MAC2STR(dpp_pkex_own_mac_override));
		own_mac = dpp_pkex_own_mac_override;
	}
#endif /* CONFIG_TESTING_OPTIONS */

	attr_id_len = 0;
	attr_id = dpp_get_attr(buf, len, DPP_ATTR_CODE_IDENTIFIER,
			       &attr_id_len);
	if (!dpp_pkex_identifier_match(attr_id, attr_id_len, identifier))
		return NULL;

	attr_group = dpp_get_attr(buf, len, DPP_ATTR_FINITE_CYCLIC_GROUP,
				  &attr_group_len);
	if (!attr_group || attr_group_len != 2) {
		wpa_msg(msg_ctx, MSG_INFO, DPP_EVENT_FAIL
			"Missing or invalid Finite Cyclic Group attribute");
		return NULL;
	}
	ike_group = WPA_GET_LE16(attr_group);
	if (ike_group != curve->ike_group) {
		wpa_msg(msg_ctx, MSG_INFO, DPP_EVENT_FAIL
			"Mismatching PKEX curve: peer=%u own=%u",
			ike_group, curve->ike_group);
		pkex = os_zalloc(sizeof(*pkex));
		if (!pkex)
			goto fail;
		pkex->own_bi = bi;
		pkex->failed = 1;
		pkex->exchange_resp = dpp_pkex_build_exchange_resp(
			pkex, DPP_STATUS_BAD_GROUP, NULL, NULL);
		if (!pkex->exchange_resp)
			goto fail;
		return pkex;
	}

	/* M in Encrypted Key attribute */
	attr_key = dpp_get_attr(buf, len, DPP_ATTR_ENCRYPTED_KEY,
				&attr_key_len);
	if (!attr_key || attr_key_len & 0x01 || attr_key_len < 2 ||
	    attr_key_len / 2 > DPP_MAX_SHARED_SECRET_LEN) {
		wpa_msg(msg_ctx, MSG_INFO, DPP_EVENT_FAIL
			"Missing Encrypted Key attribute");
		return NULL;
	}

	/* Qi = H(MAC-Initiator | [identifier |] code) * Pi */
	bnctx = BN_CTX_new();
	if (!bnctx)
		goto fail;
	Qi = dpp_pkex_derive_Qi(curve, peer_mac, code, identifier, bnctx,
				&group);
	if (!Qi)
		goto fail;

	/* X' = M - Qi */
	X = EC_POINT_new(group);
	M = EC_POINT_new(group);
	Mx = BN_bin2bn(attr_key, attr_key_len / 2, NULL);
	My = BN_bin2bn(attr_key + attr_key_len / 2, attr_key_len / 2, NULL);
	if (!X || !M || !Mx || !My ||
	    EC_POINT_set_affine_coordinates_GFp(group, M, Mx, My, bnctx) != 1 ||
	    EC_POINT_is_at_infinity(group, M) ||
	    !EC_POINT_is_on_curve(group, M, bnctx) ||
	    EC_POINT_invert(group, Qi, bnctx) != 1 ||
	    EC_POINT_add(group, X, M, Qi, bnctx) != 1 ||
	    EC_POINT_is_at_infinity(group, X) ||
	    !EC_POINT_is_on_curve(group, X, bnctx)) {
		wpa_msg(msg_ctx, MSG_INFO, DPP_EVENT_FAIL
			"Invalid Encrypted Key value");
		bi->pkex_t++;
		goto fail;
	}
	dpp_debug_print_point("DPP: M", group, M);
	dpp_debug_print_point("DPP: X'", group, X);

	pkex = os_zalloc(sizeof(*pkex));
	if (!pkex)
		goto fail;
	pkex->t = bi->pkex_t;
	pkex->msg_ctx = msg_ctx;
	pkex->own_bi = bi;
	os_memcpy(pkex->own_mac, own_mac, ETH_ALEN);
	os_memcpy(pkex->peer_mac, peer_mac, ETH_ALEN);
	if (identifier) {
		pkex->identifier = os_strdup(identifier);
		if (!pkex->identifier)
			goto fail;
	}
	pkex->code = os_strdup(code);
	if (!pkex->code)
		goto fail;

	os_memcpy(pkex->Mx, attr_key, attr_key_len / 2);

	X_ec = EC_KEY_new();
	if (!X_ec ||
	    EC_KEY_set_group(X_ec, group) != 1 ||
	    EC_KEY_set_public_key(X_ec, X) != 1)
		goto fail;
	pkex->x = EVP_PKEY_new();
	if (!pkex->x ||
	    EVP_PKEY_set1_EC_KEY(pkex->x, X_ec) != 1)
		goto fail;

	/* Qr = H(MAC-Responder | | [identifier | ] code) * Pr */
	Qr = dpp_pkex_derive_Qr(curve, own_mac, code, identifier, bnctx, NULL);
	if (!Qr)
		goto fail;

	/* Generate a random ephemeral keypair y/Y */
#ifdef CONFIG_TESTING_OPTIONS
	if (dpp_pkex_ephemeral_key_override_len) {
		const struct dpp_curve_params *tmp_curve;

		wpa_printf(MSG_INFO,
			   "DPP: TESTING - override ephemeral key y/Y");
		pkex->y = dpp_set_keypair(&tmp_curve,
					  dpp_pkex_ephemeral_key_override,
					  dpp_pkex_ephemeral_key_override_len);
	} else {
		pkex->y = dpp_gen_keypair(curve);
	}
#else /* CONFIG_TESTING_OPTIONS */
	pkex->y = dpp_gen_keypair(curve);
#endif /* CONFIG_TESTING_OPTIONS */
	if (!pkex->y)
		goto fail;

	/* N = Y + Qr */
	Y_ec = EVP_PKEY_get1_EC_KEY(pkex->y);
	if (!Y_ec)
		goto fail;
	Y_point = EC_KEY_get0_public_key(Y_ec);
	if (!Y_point)
		goto fail;
	dpp_debug_print_point("DPP: Y", group, Y_point);
	N = EC_POINT_new(group);
	Nx = BN_new();
	Ny = BN_new();
	if (!N || !Nx || !Ny ||
	    EC_POINT_add(group, N, Y_point, Qr, bnctx) != 1 ||
	    EC_POINT_get_affine_coordinates_GFp(group, N, Nx, Ny, bnctx) != 1)
		goto fail;
	dpp_debug_print_point("DPP: N", group, N);

	pkex->exchange_resp = dpp_pkex_build_exchange_resp(pkex, DPP_STATUS_OK,
							   Nx, Ny);
	if (!pkex->exchange_resp)
		goto fail;

	/* K = y * X' */
	ctx = EVP_PKEY_CTX_new(pkex->y, NULL);
	if (!ctx ||
	    EVP_PKEY_derive_init(ctx) != 1 ||
	    EVP_PKEY_derive_set_peer(ctx, pkex->x) != 1 ||
	    EVP_PKEY_derive(ctx, NULL, &Kx_len) != 1 ||
	    Kx_len > DPP_MAX_SHARED_SECRET_LEN ||
	    EVP_PKEY_derive(ctx, Kx, &Kx_len) != 1) {
		wpa_printf(MSG_ERROR,
			   "DPP: Failed to derive ECDH shared secret: %s",
			   ERR_error_string(ERR_get_error(), NULL));
		goto fail;
	}

	wpa_hexdump_key(MSG_DEBUG, "DPP: ECDH shared secret (K.x)",
			Kx, Kx_len);

	/* z = HKDF(<>, MAC-Initiator | MAC-Responder | M.x | N.x | code, K.x)
	 */
	res = dpp_pkex_derive_z(pkex->peer_mac, pkex->own_mac,
				pkex->Mx, curve->prime_len,
				pkex->Nx, curve->prime_len, pkex->code,
				Kx, Kx_len, pkex->z, curve->hash_len);
	os_memset(Kx, 0, Kx_len);
	if (res < 0)
		goto fail;

	pkex->exchange_done = 1;

out:
	EVP_PKEY_CTX_free(ctx);
	BN_CTX_free(bnctx);
	EC_POINT_free(Qi);
	EC_POINT_free(Qr);
	BN_free(Mx);
	BN_free(My);
	BN_free(Nx);
	BN_free(Ny);
	EC_POINT_free(M);
	EC_POINT_free(N);
	EC_POINT_free(X);
	EC_KEY_free(X_ec);
	EC_KEY_free(Y_ec);
	return pkex;
fail:
	wpa_printf(MSG_DEBUG, "DPP: PKEX Exchange Request processing failed");
	dpp_pkex_free(pkex);
	pkex = NULL;
	goto out;
}


static struct wpabuf *
dpp_pkex_build_commit_reveal_req(struct dpp_pkex *pkex,
				 const struct wpabuf *A_pub, const u8 *u)
{
	const struct dpp_curve_params *curve = pkex->own_bi->curve;
	struct wpabuf *msg = NULL;
	size_t clear_len, attr_len;
	struct wpabuf *clear = NULL;
	u8 *wrapped;
	u8 octet;
	const u8 *addr[2];
	size_t len[2];

	/* {A, u, [bootstrapping info]}z */
	clear_len = 4 + 2 * curve->prime_len + 4 + curve->hash_len;
	clear = wpabuf_alloc(clear_len);
	attr_len = 4 + clear_len + AES_BLOCK_SIZE;
#ifdef CONFIG_TESTING_OPTIONS
	if (dpp_test == DPP_TEST_AFTER_WRAPPED_DATA_PKEX_CR_REQ)
		attr_len += 5;
#endif /* CONFIG_TESTING_OPTIONS */
	msg = dpp_alloc_msg(DPP_PA_PKEX_COMMIT_REVEAL_REQ, attr_len);
	if (!clear || !msg)
		goto fail;

#ifdef CONFIG_TESTING_OPTIONS
	if (dpp_test == DPP_TEST_NO_BOOTSTRAP_KEY_PKEX_CR_REQ) {
		wpa_printf(MSG_INFO, "DPP: TESTING - no Bootstrap Key");
		goto skip_bootstrap_key;
	}
	if (dpp_test == DPP_TEST_INVALID_BOOTSTRAP_KEY_PKEX_CR_REQ) {
		wpa_printf(MSG_INFO, "DPP: TESTING - invalid Bootstrap Key");
		wpabuf_put_le16(clear, DPP_ATTR_BOOTSTRAP_KEY);
		wpabuf_put_le16(clear, 2 * curve->prime_len);
		if (dpp_test_gen_invalid_key(clear, curve) < 0)
			goto fail;
		goto skip_bootstrap_key;
	}
#endif /* CONFIG_TESTING_OPTIONS */

	/* A in Bootstrap Key attribute */
	wpabuf_put_le16(clear, DPP_ATTR_BOOTSTRAP_KEY);
	wpabuf_put_le16(clear, wpabuf_len(A_pub));
	wpabuf_put_buf(clear, A_pub);

#ifdef CONFIG_TESTING_OPTIONS
skip_bootstrap_key:
	if (dpp_test == DPP_TEST_NO_I_AUTH_TAG_PKEX_CR_REQ) {
		wpa_printf(MSG_INFO, "DPP: TESTING - no I-Auth tag");
		goto skip_i_auth_tag;
	}
	if (dpp_test == DPP_TEST_I_AUTH_TAG_MISMATCH_PKEX_CR_REQ) {
		wpa_printf(MSG_INFO, "DPP: TESTING - I-Auth tag mismatch");
		wpabuf_put_le16(clear, DPP_ATTR_I_AUTH_TAG);
		wpabuf_put_le16(clear, curve->hash_len);
		wpabuf_put_data(clear, u, curve->hash_len - 1);
		wpabuf_put_u8(clear, u[curve->hash_len - 1] ^ 0x01);
		goto skip_i_auth_tag;
	}
#endif /* CONFIG_TESTING_OPTIONS */

	/* u in I-Auth tag attribute */
	wpabuf_put_le16(clear, DPP_ATTR_I_AUTH_TAG);
	wpabuf_put_le16(clear, curve->hash_len);
	wpabuf_put_data(clear, u, curve->hash_len);

#ifdef CONFIG_TESTING_OPTIONS
skip_i_auth_tag:
	if (dpp_test == DPP_TEST_NO_WRAPPED_DATA_PKEX_CR_REQ) {
		wpa_printf(MSG_INFO, "DPP: TESTING - no Wrapped Data");
		goto skip_wrapped_data;
	}
#endif /* CONFIG_TESTING_OPTIONS */

	addr[0] = wpabuf_head_u8(msg) + 2;
	len[0] = DPP_HDR_LEN;
	octet = 0;
	addr[1] = &octet;
	len[1] = sizeof(octet);
	wpa_hexdump(MSG_DEBUG, "DDP: AES-SIV AD[0]", addr[0], len[0]);
	wpa_hexdump(MSG_DEBUG, "DDP: AES-SIV AD[1]", addr[1], len[1]);

	wpabuf_put_le16(msg, DPP_ATTR_WRAPPED_DATA);
	wpabuf_put_le16(msg, wpabuf_len(clear) + AES_BLOCK_SIZE);
	wrapped = wpabuf_put(msg, wpabuf_len(clear) + AES_BLOCK_SIZE);

	wpa_hexdump_buf(MSG_DEBUG, "DPP: AES-SIV cleartext", clear);
	if (aes_siv_encrypt(pkex->z, curve->hash_len,
			    wpabuf_head(clear), wpabuf_len(clear),
			    2, addr, len, wrapped) < 0)
		goto fail;
	wpa_hexdump(MSG_DEBUG, "DPP: AES-SIV ciphertext",
		    wrapped, wpabuf_len(clear) + AES_BLOCK_SIZE);

#ifdef CONFIG_TESTING_OPTIONS
	if (dpp_test == DPP_TEST_AFTER_WRAPPED_DATA_PKEX_CR_REQ) {
		wpa_printf(MSG_INFO, "DPP: TESTING - attr after Wrapped Data");
		dpp_build_attr_status(msg, DPP_STATUS_OK);
	}
skip_wrapped_data:
#endif /* CONFIG_TESTING_OPTIONS */

out:
	wpabuf_free(clear);
	return msg;

fail:
	wpabuf_free(msg);
	msg = NULL;
	goto out;
}


struct wpabuf * dpp_pkex_rx_exchange_resp(struct dpp_pkex *pkex,
					  const u8 *peer_mac,
					  const u8 *buf, size_t buflen)
{
	const u8 *attr_status, *attr_id, *attr_key, *attr_group;
	u16 attr_status_len, attr_id_len, attr_key_len, attr_group_len;
	const EC_GROUP *group;
	BN_CTX *bnctx = NULL;
	struct wpabuf *msg = NULL, *A_pub = NULL, *X_pub = NULL, *Y_pub = NULL;
	const struct dpp_curve_params *curve = pkex->own_bi->curve;
	EC_POINT *Qr = NULL, *Y = NULL, *N = NULL;
	BIGNUM *Nx = NULL, *Ny = NULL;
	EVP_PKEY_CTX *ctx = NULL;
	EC_KEY *Y_ec = NULL;
	size_t Jx_len, Kx_len;
	u8 Jx[DPP_MAX_SHARED_SECRET_LEN], Kx[DPP_MAX_SHARED_SECRET_LEN];
	const u8 *addr[4];
	size_t len[4];
	u8 u[DPP_MAX_HASH_LEN];
	int res;

	if (pkex->failed || pkex->t >= PKEX_COUNTER_T_LIMIT || !pkex->initiator)
		return NULL;

#ifdef CONFIG_TESTING_OPTIONS
	if (dpp_test == DPP_TEST_STOP_AT_PKEX_EXCHANGE_RESP) {
		wpa_printf(MSG_INFO,
			   "DPP: TESTING - stop at PKEX Exchange Response");
		pkex->failed = 1;
		return NULL;
	}

	if (!is_zero_ether_addr(dpp_pkex_peer_mac_override)) {
		wpa_printf(MSG_INFO, "DPP: TESTING - peer_mac override " MACSTR,
			   MAC2STR(dpp_pkex_peer_mac_override));
		peer_mac = dpp_pkex_peer_mac_override;
	}
#endif /* CONFIG_TESTING_OPTIONS */

	os_memcpy(pkex->peer_mac, peer_mac, ETH_ALEN);

	attr_status = dpp_get_attr(buf, buflen, DPP_ATTR_STATUS,
				   &attr_status_len);
	if (!attr_status || attr_status_len != 1) {
		dpp_pkex_fail(pkex, "No DPP Status attribute");
		return NULL;
	}
	wpa_printf(MSG_DEBUG, "DPP: Status %u", attr_status[0]);

	if (attr_status[0] == DPP_STATUS_BAD_GROUP) {
		attr_group = dpp_get_attr(buf, buflen,
					  DPP_ATTR_FINITE_CYCLIC_GROUP,
					  &attr_group_len);
		if (attr_group && attr_group_len == 2) {
			wpa_msg(pkex->msg_ctx, MSG_INFO, DPP_EVENT_FAIL
				"Peer indicated mismatching PKEX group - proposed %u",
				WPA_GET_LE16(attr_group));
			return NULL;
		}
	}

	if (attr_status[0] != DPP_STATUS_OK) {
		dpp_pkex_fail(pkex, "PKEX failed (peer indicated failure)");
		return NULL;
	}

	attr_id_len = 0;
	attr_id = dpp_get_attr(buf, buflen, DPP_ATTR_CODE_IDENTIFIER,
			       &attr_id_len);
	if (!dpp_pkex_identifier_match(attr_id, attr_id_len,
				       pkex->identifier)) {
		dpp_pkex_fail(pkex, "PKEX code identifier mismatch");
		return NULL;
	}

	/* N in Encrypted Key attribute */
	attr_key = dpp_get_attr(buf, buflen, DPP_ATTR_ENCRYPTED_KEY,
				&attr_key_len);
	if (!attr_key || attr_key_len & 0x01 || attr_key_len < 2) {
		dpp_pkex_fail(pkex, "Missing Encrypted Key attribute");
		return NULL;
	}

	/* Qr = H(MAC-Responder | [identifier |] code) * Pr */
	bnctx = BN_CTX_new();
	if (!bnctx)
		goto fail;
	Qr = dpp_pkex_derive_Qr(curve, pkex->peer_mac, pkex->code,
				pkex->identifier, bnctx, &group);
	if (!Qr)
		goto fail;

	/* Y' = N - Qr */
	Y = EC_POINT_new(group);
	N = EC_POINT_new(group);
	Nx = BN_bin2bn(attr_key, attr_key_len / 2, NULL);
	Ny = BN_bin2bn(attr_key + attr_key_len / 2, attr_key_len / 2, NULL);
	if (!Y || !N || !Nx || !Ny ||
	    EC_POINT_set_affine_coordinates_GFp(group, N, Nx, Ny, bnctx) != 1 ||
	    EC_POINT_is_at_infinity(group, N) ||
	    !EC_POINT_is_on_curve(group, N, bnctx) ||
	    EC_POINT_invert(group, Qr, bnctx) != 1 ||
	    EC_POINT_add(group, Y, N, Qr, bnctx) != 1 ||
	    EC_POINT_is_at_infinity(group, Y) ||
	    !EC_POINT_is_on_curve(group, Y, bnctx)) {
		dpp_pkex_fail(pkex, "Invalid Encrypted Key value");
		pkex->t++;
		goto fail;
	}
	dpp_debug_print_point("DPP: N", group, N);
	dpp_debug_print_point("DPP: Y'", group, Y);

	pkex->exchange_done = 1;

	/* ECDH: J = a * Y’ */
	Y_ec = EC_KEY_new();
	if (!Y_ec ||
	    EC_KEY_set_group(Y_ec, group) != 1 ||
	    EC_KEY_set_public_key(Y_ec, Y) != 1)
		goto fail;
	pkex->y = EVP_PKEY_new();
	if (!pkex->y ||
	    EVP_PKEY_set1_EC_KEY(pkex->y, Y_ec) != 1)
		goto fail;
	ctx = EVP_PKEY_CTX_new(pkex->own_bi->pubkey, NULL);
	if (!ctx ||
	    EVP_PKEY_derive_init(ctx) != 1 ||
	    EVP_PKEY_derive_set_peer(ctx, pkex->y) != 1 ||
	    EVP_PKEY_derive(ctx, NULL, &Jx_len) != 1 ||
	    Jx_len > DPP_MAX_SHARED_SECRET_LEN ||
	    EVP_PKEY_derive(ctx, Jx, &Jx_len) != 1) {
		wpa_printf(MSG_ERROR,
			   "DPP: Failed to derive ECDH shared secret: %s",
			   ERR_error_string(ERR_get_error(), NULL));
		goto fail;
	}

	wpa_hexdump_key(MSG_DEBUG, "DPP: ECDH shared secret (J.x)",
			Jx, Jx_len);

	/* u = HMAC(J.x,  MAC-Initiator | A.x | Y’.x | X.x ) */
	A_pub = dpp_get_pubkey_point(pkex->own_bi->pubkey, 0);
	Y_pub = dpp_get_pubkey_point(pkex->y, 0);
	X_pub = dpp_get_pubkey_point(pkex->x, 0);
	if (!A_pub || !Y_pub || !X_pub)
		goto fail;
	addr[0] = pkex->own_mac;
	len[0] = ETH_ALEN;
	addr[1] = wpabuf_head(A_pub);
	len[1] = wpabuf_len(A_pub) / 2;
	addr[2] = wpabuf_head(Y_pub);
	len[2] = wpabuf_len(Y_pub) / 2;
	addr[3] = wpabuf_head(X_pub);
	len[3] = wpabuf_len(X_pub) / 2;
	if (dpp_hmac_vector(curve->hash_len, Jx, Jx_len, 4, addr, len, u) < 0)
		goto fail;
	wpa_hexdump(MSG_DEBUG, "DPP: u", u, curve->hash_len);

	/* K = x * Y’ */
	EVP_PKEY_CTX_free(ctx);
	ctx = EVP_PKEY_CTX_new(pkex->x, NULL);
	if (!ctx ||
	    EVP_PKEY_derive_init(ctx) != 1 ||
	    EVP_PKEY_derive_set_peer(ctx, pkex->y) != 1 ||
	    EVP_PKEY_derive(ctx, NULL, &Kx_len) != 1 ||
	    Kx_len > DPP_MAX_SHARED_SECRET_LEN ||
	    EVP_PKEY_derive(ctx, Kx, &Kx_len) != 1) {
		wpa_printf(MSG_ERROR,
			   "DPP: Failed to derive ECDH shared secret: %s",
			   ERR_error_string(ERR_get_error(), NULL));
		goto fail;
	}

	wpa_hexdump_key(MSG_DEBUG, "DPP: ECDH shared secret (K.x)",
			Kx, Kx_len);

	/* z = HKDF(<>, MAC-Initiator | MAC-Responder | M.x | N.x | code, K.x)
	 */
	res = dpp_pkex_derive_z(pkex->own_mac, pkex->peer_mac,
				pkex->Mx, curve->prime_len,
				attr_key /* N.x */, attr_key_len / 2,
				pkex->code, Kx, Kx_len,
				pkex->z, curve->hash_len);
	os_memset(Kx, 0, Kx_len);
	if (res < 0)
		goto fail;

	msg = dpp_pkex_build_commit_reveal_req(pkex, A_pub, u);
	if (!msg)
		goto fail;

out:
	wpabuf_free(A_pub);
	wpabuf_free(X_pub);
	wpabuf_free(Y_pub);
	EC_POINT_free(Qr);
	EC_POINT_free(Y);
	EC_POINT_free(N);
	BN_free(Nx);
	BN_free(Ny);
	EC_KEY_free(Y_ec);
	EVP_PKEY_CTX_free(ctx);
	BN_CTX_free(bnctx);
	return msg;
fail:
	wpa_printf(MSG_DEBUG, "DPP: PKEX Exchange Response processing failed");
	goto out;
}


static struct wpabuf *
dpp_pkex_build_commit_reveal_resp(struct dpp_pkex *pkex,
				  const struct wpabuf *B_pub, const u8 *v)
{
	const struct dpp_curve_params *curve = pkex->own_bi->curve;
	struct wpabuf *msg = NULL;
	const u8 *addr[2];
	size_t len[2];
	u8 octet;
	u8 *wrapped;
	struct wpabuf *clear = NULL;
	size_t clear_len, attr_len;

	/* {B, v [bootstrapping info]}z */
	clear_len = 4 + 2 * curve->prime_len + 4 + curve->hash_len;
	clear = wpabuf_alloc(clear_len);
	attr_len = 4 + clear_len + AES_BLOCK_SIZE;
#ifdef CONFIG_TESTING_OPTIONS
	if (dpp_test == DPP_TEST_AFTER_WRAPPED_DATA_PKEX_CR_RESP)
		attr_len += 5;
#endif /* CONFIG_TESTING_OPTIONS */
	msg = dpp_alloc_msg(DPP_PA_PKEX_COMMIT_REVEAL_RESP, attr_len);
	if (!clear || !msg)
		goto fail;

#ifdef CONFIG_TESTING_OPTIONS
	if (dpp_test == DPP_TEST_NO_BOOTSTRAP_KEY_PKEX_CR_RESP) {
		wpa_printf(MSG_INFO, "DPP: TESTING - no Bootstrap Key");
		goto skip_bootstrap_key;
	}
	if (dpp_test == DPP_TEST_INVALID_BOOTSTRAP_KEY_PKEX_CR_RESP) {
		wpa_printf(MSG_INFO, "DPP: TESTING - invalid Bootstrap Key");
		wpabuf_put_le16(clear, DPP_ATTR_BOOTSTRAP_KEY);
		wpabuf_put_le16(clear, 2 * curve->prime_len);
		if (dpp_test_gen_invalid_key(clear, curve) < 0)
			goto fail;
		goto skip_bootstrap_key;
	}
#endif /* CONFIG_TESTING_OPTIONS */

	/* B in Bootstrap Key attribute */
	wpabuf_put_le16(clear, DPP_ATTR_BOOTSTRAP_KEY);
	wpabuf_put_le16(clear, wpabuf_len(B_pub));
	wpabuf_put_buf(clear, B_pub);

#ifdef CONFIG_TESTING_OPTIONS
skip_bootstrap_key:
	if (dpp_test == DPP_TEST_NO_R_AUTH_TAG_PKEX_CR_RESP) {
		wpa_printf(MSG_INFO, "DPP: TESTING - no R-Auth tag");
		goto skip_r_auth_tag;
	}
	if (dpp_test == DPP_TEST_R_AUTH_TAG_MISMATCH_PKEX_CR_RESP) {
		wpa_printf(MSG_INFO, "DPP: TESTING - R-Auth tag mismatch");
		wpabuf_put_le16(clear, DPP_ATTR_R_AUTH_TAG);
		wpabuf_put_le16(clear, curve->hash_len);
		wpabuf_put_data(clear, v, curve->hash_len - 1);
		wpabuf_put_u8(clear, v[curve->hash_len - 1] ^ 0x01);
		goto skip_r_auth_tag;
	}
#endif /* CONFIG_TESTING_OPTIONS */

	/* v in R-Auth tag attribute */
	wpabuf_put_le16(clear, DPP_ATTR_R_AUTH_TAG);
	wpabuf_put_le16(clear, curve->hash_len);
	wpabuf_put_data(clear, v, curve->hash_len);

#ifdef CONFIG_TESTING_OPTIONS
skip_r_auth_tag:
	if (dpp_test == DPP_TEST_NO_WRAPPED_DATA_PKEX_CR_RESP) {
		wpa_printf(MSG_INFO, "DPP: TESTING - no Wrapped Data");
		goto skip_wrapped_data;
	}
#endif /* CONFIG_TESTING_OPTIONS */

	addr[0] = wpabuf_head_u8(msg) + 2;
	len[0] = DPP_HDR_LEN;
	octet = 1;
	addr[1] = &octet;
	len[1] = sizeof(octet);
	wpa_hexdump(MSG_DEBUG, "DDP: AES-SIV AD[0]", addr[0], len[0]);
	wpa_hexdump(MSG_DEBUG, "DDP: AES-SIV AD[1]", addr[1], len[1]);

	wpabuf_put_le16(msg, DPP_ATTR_WRAPPED_DATA);
	wpabuf_put_le16(msg, wpabuf_len(clear) + AES_BLOCK_SIZE);
	wrapped = wpabuf_put(msg, wpabuf_len(clear) + AES_BLOCK_SIZE);

	wpa_hexdump_buf(MSG_DEBUG, "DPP: AES-SIV cleartext", clear);
	if (aes_siv_encrypt(pkex->z, curve->hash_len,
			    wpabuf_head(clear), wpabuf_len(clear),
			    2, addr, len, wrapped) < 0)
		goto fail;
	wpa_hexdump(MSG_DEBUG, "DPP: AES-SIV ciphertext",
		    wrapped, wpabuf_len(clear) + AES_BLOCK_SIZE);

#ifdef CONFIG_TESTING_OPTIONS
	if (dpp_test == DPP_TEST_AFTER_WRAPPED_DATA_PKEX_CR_RESP) {
		wpa_printf(MSG_INFO, "DPP: TESTING - attr after Wrapped Data");
		dpp_build_attr_status(msg, DPP_STATUS_OK);
	}
skip_wrapped_data:
#endif /* CONFIG_TESTING_OPTIONS */

out:
	wpabuf_free(clear);
	return msg;

fail:
	wpabuf_free(msg);
	msg = NULL;
	goto out;
}


struct wpabuf * dpp_pkex_rx_commit_reveal_req(struct dpp_pkex *pkex,
					      const u8 *hdr,
					      const u8 *buf, size_t buflen)
{
	const struct dpp_curve_params *curve = pkex->own_bi->curve;
	EVP_PKEY_CTX *ctx = NULL;
	size_t Jx_len, Lx_len;
	u8 Jx[DPP_MAX_SHARED_SECRET_LEN];
	u8 Lx[DPP_MAX_SHARED_SECRET_LEN];
	const u8 *wrapped_data, *b_key, *peer_u;
	u16 wrapped_data_len, b_key_len, peer_u_len = 0;
	const u8 *addr[4];
	size_t len[4];
	u8 octet;
	u8 *unwrapped = NULL;
	size_t unwrapped_len = 0;
	struct wpabuf *msg = NULL, *A_pub = NULL, *X_pub = NULL, *Y_pub = NULL;
	struct wpabuf *B_pub = NULL;
	u8 u[DPP_MAX_HASH_LEN], v[DPP_MAX_HASH_LEN];

#ifdef CONFIG_TESTING_OPTIONS
	if (dpp_test == DPP_TEST_STOP_AT_PKEX_CR_REQ) {
		wpa_printf(MSG_INFO,
			   "DPP: TESTING - stop at PKEX CR Request");
		pkex->failed = 1;
		return NULL;
	}
#endif /* CONFIG_TESTING_OPTIONS */

	if (!pkex->exchange_done || pkex->failed ||
	    pkex->t >= PKEX_COUNTER_T_LIMIT || pkex->initiator)
		goto fail;

	wrapped_data = dpp_get_attr(buf, buflen, DPP_ATTR_WRAPPED_DATA,
				    &wrapped_data_len);
	if (!wrapped_data || wrapped_data_len < AES_BLOCK_SIZE) {
		dpp_pkex_fail(pkex,
			      "Missing or invalid required Wrapped Data attribute");
		goto fail;
	}

	wpa_hexdump(MSG_DEBUG, "DPP: AES-SIV ciphertext",
		    wrapped_data, wrapped_data_len);
	unwrapped_len = wrapped_data_len - AES_BLOCK_SIZE;
	unwrapped = os_malloc(unwrapped_len);
	if (!unwrapped)
		goto fail;

	addr[0] = hdr;
	len[0] = DPP_HDR_LEN;
	octet = 0;
	addr[1] = &octet;
	len[1] = sizeof(octet);
	wpa_hexdump(MSG_DEBUG, "DDP: AES-SIV AD[0]", addr[0], len[0]);
	wpa_hexdump(MSG_DEBUG, "DDP: AES-SIV AD[1]", addr[1], len[1]);

	if (aes_siv_decrypt(pkex->z, curve->hash_len,
			    wrapped_data, wrapped_data_len,
			    2, addr, len, unwrapped) < 0) {
		dpp_pkex_fail(pkex,
			      "AES-SIV decryption failed - possible PKEX code mismatch");
		pkex->failed = 1;
		pkex->t++;
		goto fail;
	}
	wpa_hexdump(MSG_DEBUG, "DPP: AES-SIV cleartext",
		    unwrapped, unwrapped_len);

	if (dpp_check_attrs(unwrapped, unwrapped_len) < 0) {
		dpp_pkex_fail(pkex, "Invalid attribute in unwrapped data");
		goto fail;
	}

	b_key = dpp_get_attr(unwrapped, unwrapped_len, DPP_ATTR_BOOTSTRAP_KEY,
			     &b_key_len);
	if (!b_key || b_key_len != 2 * curve->prime_len) {
		dpp_pkex_fail(pkex, "No valid peer bootstrapping key found");
		goto fail;
	}
	pkex->peer_bootstrap_key = dpp_set_pubkey_point(pkex->x, b_key,
							b_key_len);
	if (!pkex->peer_bootstrap_key) {
		dpp_pkex_fail(pkex, "Peer bootstrapping key is invalid");
		goto fail;
	}
	dpp_debug_print_key("DPP: Peer bootstrap public key",
			    pkex->peer_bootstrap_key);

	/* ECDH: J' = y * A' */
	ctx = EVP_PKEY_CTX_new(pkex->y, NULL);
	if (!ctx ||
	    EVP_PKEY_derive_init(ctx) != 1 ||
	    EVP_PKEY_derive_set_peer(ctx, pkex->peer_bootstrap_key) != 1 ||
	    EVP_PKEY_derive(ctx, NULL, &Jx_len) != 1 ||
	    Jx_len > DPP_MAX_SHARED_SECRET_LEN ||
	    EVP_PKEY_derive(ctx, Jx, &Jx_len) != 1) {
		wpa_printf(MSG_ERROR,
			   "DPP: Failed to derive ECDH shared secret: %s",
			   ERR_error_string(ERR_get_error(), NULL));
		goto fail;
	}

	wpa_hexdump_key(MSG_DEBUG, "DPP: ECDH shared secret (J.x)",
			Jx, Jx_len);

	/* u' = HMAC(J'.x, MAC-Initiator | A'.x | Y.x | X'.x) */
	A_pub = dpp_get_pubkey_point(pkex->peer_bootstrap_key, 0);
	Y_pub = dpp_get_pubkey_point(pkex->y, 0);
	X_pub = dpp_get_pubkey_point(pkex->x, 0);
	if (!A_pub || !Y_pub || !X_pub)
		goto fail;
	addr[0] = pkex->peer_mac;
	len[0] = ETH_ALEN;
	addr[1] = wpabuf_head(A_pub);
	len[1] = wpabuf_len(A_pub) / 2;
	addr[2] = wpabuf_head(Y_pub);
	len[2] = wpabuf_len(Y_pub) / 2;
	addr[3] = wpabuf_head(X_pub);
	len[3] = wpabuf_len(X_pub) / 2;
	if (dpp_hmac_vector(curve->hash_len, Jx, Jx_len, 4, addr, len, u) < 0)
		goto fail;

	peer_u = dpp_get_attr(unwrapped, unwrapped_len, DPP_ATTR_I_AUTH_TAG,
			      &peer_u_len);
	if (!peer_u || peer_u_len != curve->hash_len ||
	    os_memcmp(peer_u, u, curve->hash_len) != 0) {
		dpp_pkex_fail(pkex, "No valid u (I-Auth tag) found");
		wpa_hexdump(MSG_DEBUG, "DPP: Calculated u'",
			    u, curve->hash_len);
		wpa_hexdump(MSG_DEBUG, "DPP: Received u", peer_u, peer_u_len);
		pkex->t++;
		goto fail;
	}
	wpa_printf(MSG_DEBUG, "DPP: Valid u (I-Auth tag) received");

	/* ECDH: L = b * X' */
	EVP_PKEY_CTX_free(ctx);
	ctx = EVP_PKEY_CTX_new(pkex->own_bi->pubkey, NULL);
	if (!ctx ||
	    EVP_PKEY_derive_init(ctx) != 1 ||
	    EVP_PKEY_derive_set_peer(ctx, pkex->x) != 1 ||
	    EVP_PKEY_derive(ctx, NULL, &Lx_len) != 1 ||
	    Lx_len > DPP_MAX_SHARED_SECRET_LEN ||
	    EVP_PKEY_derive(ctx, Lx, &Lx_len) != 1) {
		wpa_printf(MSG_ERROR,
			   "DPP: Failed to derive ECDH shared secret: %s",
			   ERR_error_string(ERR_get_error(), NULL));
		goto fail;
	}

	wpa_hexdump_key(MSG_DEBUG, "DPP: ECDH shared secret (L.x)",
			Lx, Lx_len);

	/* v = HMAC(L.x, MAC-Responder | B.x | X'.x | Y.x) */
	B_pub = dpp_get_pubkey_point(pkex->own_bi->pubkey, 0);
	if (!B_pub)
		goto fail;
	addr[0] = pkex->own_mac;
	len[0] = ETH_ALEN;
	addr[1] = wpabuf_head(B_pub);
	len[1] = wpabuf_len(B_pub) / 2;
	addr[2] = wpabuf_head(X_pub);
	len[2] = wpabuf_len(X_pub) / 2;
	addr[3] = wpabuf_head(Y_pub);
	len[3] = wpabuf_len(Y_pub) / 2;
	if (dpp_hmac_vector(curve->hash_len, Lx, Lx_len, 4, addr, len, v) < 0)
		goto fail;
	wpa_hexdump(MSG_DEBUG, "DPP: v", v, curve->hash_len);

	msg = dpp_pkex_build_commit_reveal_resp(pkex, B_pub, v);
	if (!msg)
		goto fail;

out:
	EVP_PKEY_CTX_free(ctx);
	os_free(unwrapped);
	wpabuf_free(A_pub);
	wpabuf_free(B_pub);
	wpabuf_free(X_pub);
	wpabuf_free(Y_pub);
	return msg;
fail:
	wpa_printf(MSG_DEBUG,
		   "DPP: PKEX Commit-Reveal Request processing failed");
	goto out;
}


int dpp_pkex_rx_commit_reveal_resp(struct dpp_pkex *pkex, const u8 *hdr,
				   const u8 *buf, size_t buflen)
{
	const struct dpp_curve_params *curve = pkex->own_bi->curve;
	const u8 *wrapped_data, *b_key, *peer_v;
	u16 wrapped_data_len, b_key_len, peer_v_len = 0;
	const u8 *addr[4];
	size_t len[4];
	u8 octet;
	u8 *unwrapped = NULL;
	size_t unwrapped_len = 0;
	int ret = -1;
	u8 v[DPP_MAX_HASH_LEN];
	size_t Lx_len;
	u8 Lx[DPP_MAX_SHARED_SECRET_LEN];
	EVP_PKEY_CTX *ctx = NULL;
	struct wpabuf *B_pub = NULL, *X_pub = NULL, *Y_pub = NULL;

#ifdef CONFIG_TESTING_OPTIONS
	if (dpp_test == DPP_TEST_STOP_AT_PKEX_CR_RESP) {
		wpa_printf(MSG_INFO,
			   "DPP: TESTING - stop at PKEX CR Response");
		pkex->failed = 1;
		goto fail;
	}
#endif /* CONFIG_TESTING_OPTIONS */

	if (!pkex->exchange_done || pkex->failed ||
	    pkex->t >= PKEX_COUNTER_T_LIMIT || !pkex->initiator)
		goto fail;

	wrapped_data = dpp_get_attr(buf, buflen, DPP_ATTR_WRAPPED_DATA,
				    &wrapped_data_len);
	if (!wrapped_data || wrapped_data_len < AES_BLOCK_SIZE) {
		dpp_pkex_fail(pkex,
			      "Missing or invalid required Wrapped Data attribute");
		goto fail;
	}

	wpa_hexdump(MSG_DEBUG, "DPP: AES-SIV ciphertext",
		    wrapped_data, wrapped_data_len);
	unwrapped_len = wrapped_data_len - AES_BLOCK_SIZE;
	unwrapped = os_malloc(unwrapped_len);
	if (!unwrapped)
		goto fail;

	addr[0] = hdr;
	len[0] = DPP_HDR_LEN;
	octet = 1;
	addr[1] = &octet;
	len[1] = sizeof(octet);
	wpa_hexdump(MSG_DEBUG, "DDP: AES-SIV AD[0]", addr[0], len[0]);
	wpa_hexdump(MSG_DEBUG, "DDP: AES-SIV AD[1]", addr[1], len[1]);

	if (aes_siv_decrypt(pkex->z, curve->hash_len,
			    wrapped_data, wrapped_data_len,
			    2, addr, len, unwrapped) < 0) {
		dpp_pkex_fail(pkex,
			      "AES-SIV decryption failed - possible PKEX code mismatch");
		pkex->t++;
		goto fail;
	}
	wpa_hexdump(MSG_DEBUG, "DPP: AES-SIV cleartext",
		    unwrapped, unwrapped_len);

	if (dpp_check_attrs(unwrapped, unwrapped_len) < 0) {
		dpp_pkex_fail(pkex, "Invalid attribute in unwrapped data");
		goto fail;
	}

	b_key = dpp_get_attr(unwrapped, unwrapped_len, DPP_ATTR_BOOTSTRAP_KEY,
			     &b_key_len);
	if (!b_key || b_key_len != 2 * curve->prime_len) {
		dpp_pkex_fail(pkex, "No valid peer bootstrapping key found");
		goto fail;
	}
	pkex->peer_bootstrap_key = dpp_set_pubkey_point(pkex->x, b_key,
							b_key_len);
	if (!pkex->peer_bootstrap_key) {
		dpp_pkex_fail(pkex, "Peer bootstrapping key is invalid");
		goto fail;
	}
	dpp_debug_print_key("DPP: Peer bootstrap public key",
			    pkex->peer_bootstrap_key);

	/* ECDH: L' = x * B' */
	ctx = EVP_PKEY_CTX_new(pkex->x, NULL);
	if (!ctx ||
	    EVP_PKEY_derive_init(ctx) != 1 ||
	    EVP_PKEY_derive_set_peer(ctx, pkex->peer_bootstrap_key) != 1 ||
	    EVP_PKEY_derive(ctx, NULL, &Lx_len) != 1 ||
	    Lx_len > DPP_MAX_SHARED_SECRET_LEN ||
	    EVP_PKEY_derive(ctx, Lx, &Lx_len) != 1) {
		wpa_printf(MSG_ERROR,
			   "DPP: Failed to derive ECDH shared secret: %s",
			   ERR_error_string(ERR_get_error(), NULL));
		goto fail;
	}

	wpa_hexdump_key(MSG_DEBUG, "DPP: ECDH shared secret (L.x)",
			Lx, Lx_len);

	/* v' = HMAC(L.x, MAC-Responder | B'.x | X.x | Y'.x) */
	B_pub = dpp_get_pubkey_point(pkex->peer_bootstrap_key, 0);
	X_pub = dpp_get_pubkey_point(pkex->x, 0);
	Y_pub = dpp_get_pubkey_point(pkex->y, 0);
	if (!B_pub || !X_pub || !Y_pub)
		goto fail;
	addr[0] = pkex->peer_mac;
	len[0] = ETH_ALEN;
	addr[1] = wpabuf_head(B_pub);
	len[1] = wpabuf_len(B_pub) / 2;
	addr[2] = wpabuf_head(X_pub);
	len[2] = wpabuf_len(X_pub) / 2;
	addr[3] = wpabuf_head(Y_pub);
	len[3] = wpabuf_len(Y_pub) / 2;
	if (dpp_hmac_vector(curve->hash_len, Lx, Lx_len, 4, addr, len, v) < 0)
		goto fail;

	peer_v = dpp_get_attr(unwrapped, unwrapped_len, DPP_ATTR_R_AUTH_TAG,
			      &peer_v_len);
	if (!peer_v || peer_v_len != curve->hash_len ||
	    os_memcmp(peer_v, v, curve->hash_len) != 0) {
		dpp_pkex_fail(pkex, "No valid v (R-Auth tag) found");
		wpa_hexdump(MSG_DEBUG, "DPP: Calculated v'",
			    v, curve->hash_len);
		wpa_hexdump(MSG_DEBUG, "DPP: Received v", peer_v, peer_v_len);
		pkex->t++;
		goto fail;
	}
	wpa_printf(MSG_DEBUG, "DPP: Valid v (R-Auth tag) received");

	ret = 0;
out:
	wpabuf_free(B_pub);
	wpabuf_free(X_pub);
	wpabuf_free(Y_pub);
	EVP_PKEY_CTX_free(ctx);
	os_free(unwrapped);
	return ret;
fail:
	goto out;
}


void dpp_pkex_free(struct dpp_pkex *pkex)
{
	if (!pkex)
		return;

	os_free(pkex->identifier);
	os_free(pkex->code);
	EVP_PKEY_free(pkex->x);
	EVP_PKEY_free(pkex->y);
	EVP_PKEY_free(pkex->peer_bootstrap_key);
	wpabuf_free(pkex->exchange_req);
	wpabuf_free(pkex->exchange_resp);
	os_free(pkex);
}


#ifdef CONFIG_TESTING_OPTIONS
char * dpp_corrupt_connector_signature(const char *connector)
{
	char *tmp, *pos, *signed3 = NULL;
	unsigned char *signature = NULL;
	size_t signature_len = 0, signed3_len;

	tmp = os_zalloc(os_strlen(connector) + 5);
	if (!tmp)
		goto fail;
	os_memcpy(tmp, connector, os_strlen(connector));

	pos = os_strchr(tmp, '.');
	if (!pos)
		goto fail;

	pos = os_strchr(pos + 1, '.');
	if (!pos)
		goto fail;
	pos++;

	wpa_printf(MSG_DEBUG, "DPP: Original base64url encoded signature: %s",
		   pos);
	signature = base64_url_decode((const unsigned char *) pos,
				      os_strlen(pos), &signature_len);
	if (!signature || signature_len == 0)
		goto fail;
	wpa_hexdump(MSG_DEBUG, "DPP: Original Connector signature",
		    signature, signature_len);
	signature[signature_len - 1] ^= 0x01;
	wpa_hexdump(MSG_DEBUG, "DPP: Corrupted Connector signature",
		    signature, signature_len);
	signed3 = (char *) base64_url_encode(signature, signature_len,
					     &signed3_len, 0);
	if (!signed3)
		goto fail;
	os_memcpy(pos, signed3, signed3_len);
	pos[signed3_len] = '\0';
	wpa_printf(MSG_DEBUG, "DPP: Corrupted base64url encoded signature: %s",
		   pos);

out:
	os_free(signature);
	os_free(signed3);
	return tmp;
fail:
	os_free(tmp);
	tmp = NULL;
	goto out;
}
#endif /* CONFIG_TESTING_OPTIONS */
