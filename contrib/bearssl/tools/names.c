/*
 * Copyright (c) 2016 Thomas Pornin <pornin@bolet.org>
 *
 * Permission is hereby granted, free of charge, to any person obtaining 
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be 
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, 
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND 
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "brssl.h"
#include "bearssl.h"

/* see brssl.h */
const protocol_version protocol_versions[] = {
	{ "tls10", BR_TLS10, "TLS 1.0" },
	{ "tls11", BR_TLS11, "TLS 1.1" },
	{ "tls12", BR_TLS12, "TLS 1.2" },
	{ NULL, 0, NULL }
};

/* see brssl.h */
const hash_function hash_functions[] = {
	{ "md5",     &br_md5_vtable,     "MD5" },
	{ "sha1",    &br_sha1_vtable,    "SHA-1" },
	{ "sha224",  &br_sha224_vtable,  "SHA-224" },
	{ "sha256",  &br_sha256_vtable,  "SHA-256" },
	{ "sha384",  &br_sha384_vtable,  "SHA-384" },
	{ "sha512",  &br_sha512_vtable,  "SHA-512" },
	{ NULL, 0, NULL }
};

/* see brssl.h */
const cipher_suite cipher_suites[] = {
	{
		"ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256",
		BR_TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256,
		REQ_ECDHE_ECDSA | REQ_CHAPOL | REQ_SHA256 | REQ_TLS12,
		"ECDHE with ECDSA, ChaCha20+Poly1305 encryption (TLS 1.2+)"
	},
	{
		"ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256",
		BR_TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256,
		REQ_ECDHE_RSA | REQ_CHAPOL | REQ_SHA256 | REQ_TLS12,
		"ECDHE with RSA, ChaCha20+Poly1305 encryption (TLS 1.2+)"
	},
	{
		"ECDHE_ECDSA_WITH_AES_128_GCM_SHA256",
		BR_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256,
		REQ_ECDHE_ECDSA | REQ_AESGCM | REQ_SHA256 | REQ_TLS12,
		"ECDHE with ECDSA, AES-128/GCM encryption (TLS 1.2+)"
	},
	{
		"ECDHE_RSA_WITH_AES_128_GCM_SHA256",
		BR_TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256,
		REQ_ECDHE_RSA | REQ_AESGCM | REQ_SHA256 | REQ_TLS12,
		"ECDHE with RSA, AES-128/GCM encryption (TLS 1.2+)"
	},
	{
		"ECDHE_ECDSA_WITH_AES_256_GCM_SHA384",
		BR_TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384,
		REQ_ECDHE_ECDSA | REQ_AESGCM | REQ_SHA384 | REQ_TLS12,
		"ECDHE with ECDSA, AES-256/GCM encryption (TLS 1.2+)"
	},
	{
		"ECDHE_RSA_WITH_AES_256_GCM_SHA384",
		BR_TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384,
		REQ_ECDHE_RSA | REQ_AESGCM | REQ_SHA384 | REQ_TLS12,
		"ECDHE with RSA, AES-256/GCM encryption (TLS 1.2+)"
	},
	{
		"ECDHE_ECDSA_WITH_AES_128_CCM",
		BR_TLS_ECDHE_ECDSA_WITH_AES_128_CCM,
		REQ_ECDHE_ECDSA | REQ_AESCCM | REQ_SHA256 | REQ_TLS12,
		"ECDHE with ECDSA, AES-128/CCM encryption (TLS 1.2+)"
	},
	{
		"ECDHE_ECDSA_WITH_AES_256_CCM",
		BR_TLS_ECDHE_ECDSA_WITH_AES_256_CCM,
		REQ_ECDHE_ECDSA | REQ_AESCCM | REQ_SHA256 | REQ_TLS12,
		"ECDHE with ECDSA, AES-256/CCM encryption (TLS 1.2+)"
	},
	{
		"ECDHE_ECDSA_WITH_AES_128_CCM_8",
		BR_TLS_ECDHE_ECDSA_WITH_AES_128_CCM_8,
		REQ_ECDHE_ECDSA | REQ_AESCCM | REQ_SHA256 | REQ_TLS12,
		"ECDHE with ECDSA, AES-128/CCM_8 encryption (TLS 1.2+)"
	},
	{
		"ECDHE_ECDSA_WITH_AES_256_CCM_8",
		BR_TLS_ECDHE_ECDSA_WITH_AES_256_CCM_8,
		REQ_ECDHE_ECDSA | REQ_AESCCM | REQ_SHA256 | REQ_TLS12,
		"ECDHE with ECDSA, AES-256/CCM_8 encryption (TLS 1.2+)"
	},
	{
		"ECDHE_ECDSA_WITH_AES_128_CBC_SHA256",
		BR_TLS_ECDHE_ECDSA_WITH_AES_128_CBC_SHA256,
		REQ_ECDHE_ECDSA | REQ_AESCBC | REQ_SHA256 | REQ_TLS12,
		"ECDHE with ECDSA, AES-128/CBC + SHA-256 (TLS 1.2+)"
	},
	{
		"ECDHE_RSA_WITH_AES_128_CBC_SHA256",
		BR_TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA256,
		REQ_ECDHE_RSA | REQ_AESCBC | REQ_SHA256 | REQ_TLS12,
		"ECDHE with RSA, AES-128/CBC + SHA-256 (TLS 1.2+)"
	},
	{
		"ECDHE_ECDSA_WITH_AES_256_CBC_SHA384",
		BR_TLS_ECDHE_ECDSA_WITH_AES_256_CBC_SHA384,
		REQ_ECDHE_ECDSA | REQ_AESCBC | REQ_SHA384 | REQ_TLS12,
		"ECDHE with ECDSA, AES-256/CBC + SHA-384 (TLS 1.2+)"
	},
	{
		"ECDHE_RSA_WITH_AES_256_CBC_SHA384",
		BR_TLS_ECDHE_RSA_WITH_AES_256_CBC_SHA384,
		REQ_ECDHE_RSA | REQ_AESCBC | REQ_SHA384 | REQ_TLS12,
		"ECDHE with RSA, AES-256/CBC + SHA-384 (TLS 1.2+)"
	},
	{
		"ECDHE_ECDSA_WITH_AES_128_CBC_SHA",
		BR_TLS_ECDHE_ECDSA_WITH_AES_128_CBC_SHA,
		REQ_ECDHE_ECDSA | REQ_AESCBC | REQ_SHA1,
		"ECDHE with ECDSA, AES-128/CBC + SHA-1"
	},
	{
		"ECDHE_RSA_WITH_AES_128_CBC_SHA",
		BR_TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA,
		REQ_ECDHE_RSA | REQ_AESCBC | REQ_SHA1,
		"ECDHE with RSA, AES-128/CBC + SHA-1"
	},
	{
		"ECDHE_ECDSA_WITH_AES_256_CBC_SHA",
		BR_TLS_ECDHE_ECDSA_WITH_AES_256_CBC_SHA,
		REQ_ECDHE_ECDSA | REQ_AESCBC | REQ_SHA1,
		"ECDHE with ECDSA, AES-256/CBC + SHA-1"
	},
	{
		"ECDHE_RSA_WITH_AES_256_CBC_SHA",
		BR_TLS_ECDHE_RSA_WITH_AES_256_CBC_SHA,
		REQ_ECDHE_RSA | REQ_AESCBC | REQ_SHA1,
		"ECDHE with RSA, AES-256/CBC + SHA-1"
	},
	{
		"ECDH_ECDSA_WITH_AES_128_GCM_SHA256",
		BR_TLS_ECDH_ECDSA_WITH_AES_128_GCM_SHA256,
		REQ_ECDH | REQ_AESGCM | REQ_SHA256 | REQ_TLS12,
		"ECDH key exchange (EC cert), AES-128/GCM (TLS 1.2+)"
	},
	{
		"ECDH_RSA_WITH_AES_128_GCM_SHA256",
		BR_TLS_ECDH_RSA_WITH_AES_128_GCM_SHA256,
		REQ_ECDH | REQ_AESGCM | REQ_SHA256 | REQ_TLS12,
		"ECDH key exchange (RSA cert), AES-128/GCM (TLS 1.2+)"
	},
	{
		"ECDH_ECDSA_WITH_AES_256_GCM_SHA384",
		BR_TLS_ECDH_ECDSA_WITH_AES_256_GCM_SHA384,
		REQ_ECDH | REQ_AESGCM | REQ_SHA384 | REQ_TLS12,
		"ECDH key exchange (EC cert), AES-256/GCM (TLS 1.2+)"
	},
	{
		"ECDH_RSA_WITH_AES_256_GCM_SHA384",
		BR_TLS_ECDH_RSA_WITH_AES_256_GCM_SHA384,
		REQ_ECDH | REQ_AESGCM | REQ_SHA384 | REQ_TLS12,
		"ECDH key exchange (RSA cert), AES-256/GCM (TLS 1.2+)"
	},
	{
		"ECDH_ECDSA_WITH_AES_128_CBC_SHA256",
		BR_TLS_ECDH_ECDSA_WITH_AES_128_CBC_SHA256,
		REQ_ECDH | REQ_AESCBC | REQ_SHA256 | REQ_TLS12,
		"ECDH key exchange (EC cert), AES-128/CBC + HMAC/SHA-256 (TLS 1.2+)"
	},
	{
		"ECDH_RSA_WITH_AES_128_CBC_SHA256",
		BR_TLS_ECDH_RSA_WITH_AES_128_CBC_SHA256,
		REQ_ECDH | REQ_AESCBC | REQ_SHA256 | REQ_TLS12,
		"ECDH key exchange (RSA cert), AES-128/CBC + HMAC/SHA-256 (TLS 1.2+)"
	},
	{
		"ECDH_ECDSA_WITH_AES_256_CBC_SHA384",
		BR_TLS_ECDH_ECDSA_WITH_AES_256_CBC_SHA384,
		REQ_ECDH | REQ_AESCBC | REQ_SHA384 | REQ_TLS12,
		"ECDH key exchange (EC cert), AES-256/CBC + HMAC/SHA-384 (TLS 1.2+)"
	},
	{
		"ECDH_RSA_WITH_AES_256_CBC_SHA384",
		BR_TLS_ECDH_RSA_WITH_AES_256_CBC_SHA384,
		REQ_ECDH | REQ_AESCBC | REQ_SHA384 | REQ_TLS12,
		"ECDH key exchange (RSA cert), AES-256/CBC + HMAC/SHA-384 (TLS 1.2+)"
	},
	{
		"ECDH_ECDSA_WITH_AES_128_CBC_SHA",
		BR_TLS_ECDH_ECDSA_WITH_AES_128_CBC_SHA,
		REQ_ECDH | REQ_AESCBC | REQ_SHA1,
		"ECDH key exchange (EC cert), AES-128/CBC + HMAC/SHA-1"
	},
	{
		"ECDH_RSA_WITH_AES_128_CBC_SHA",
		BR_TLS_ECDH_RSA_WITH_AES_128_CBC_SHA,
		REQ_ECDH | REQ_AESCBC | REQ_SHA1,
		"ECDH key exchange (RSA cert), AES-128/CBC + HMAC/SHA-1"
	},
	{
		"ECDH_ECDSA_WITH_AES_256_CBC_SHA",
		BR_TLS_ECDH_ECDSA_WITH_AES_256_CBC_SHA,
		REQ_ECDH | REQ_AESCBC | REQ_SHA1,
		"ECDH key exchange (EC cert), AES-256/CBC + HMAC/SHA-1"
	},
	{
		"ECDH_RSA_WITH_AES_256_CBC_SHA",
		BR_TLS_ECDH_RSA_WITH_AES_256_CBC_SHA,
		REQ_ECDH | REQ_AESCBC | REQ_SHA1,
		"ECDH key exchange (RSA cert), AES-256/CBC + HMAC/SHA-1"
	},
	{
		"RSA_WITH_AES_128_GCM_SHA256",
		BR_TLS_RSA_WITH_AES_128_GCM_SHA256,
		REQ_RSAKEYX | REQ_AESGCM | REQ_SHA256 | REQ_TLS12,
		"RSA key exchange, AES-128/GCM encryption (TLS 1.2+)"
	},
	{
		"RSA_WITH_AES_256_GCM_SHA384",
		BR_TLS_RSA_WITH_AES_256_GCM_SHA384,
		REQ_RSAKEYX | REQ_AESGCM | REQ_SHA384 | REQ_TLS12,
		"RSA key exchange, AES-256/GCM encryption (TLS 1.2+)"
	},
	{
		"RSA_WITH_AES_128_CCM",
		BR_TLS_RSA_WITH_AES_128_CCM,
		REQ_RSAKEYX | REQ_AESCCM | REQ_SHA256 | REQ_TLS12,
		"RSA key exchange, AES-128/CCM encryption (TLS 1.2+)"
	},
	{
		"RSA_WITH_AES_256_CCM",
		BR_TLS_RSA_WITH_AES_256_CCM,
		REQ_RSAKEYX | REQ_AESCCM | REQ_SHA256 | REQ_TLS12,
		"RSA key exchange, AES-256/CCM encryption (TLS 1.2+)"
	},
	{
		"RSA_WITH_AES_128_CCM_8",
		BR_TLS_RSA_WITH_AES_128_CCM_8,
		REQ_RSAKEYX | REQ_AESCCM | REQ_SHA256 | REQ_TLS12,
		"RSA key exchange, AES-128/CCM_8 encryption (TLS 1.2+)"
	},
	{
		"RSA_WITH_AES_256_CCM_8",
		BR_TLS_RSA_WITH_AES_256_CCM_8,
		REQ_RSAKEYX | REQ_AESCCM | REQ_SHA256 | REQ_TLS12,
		"RSA key exchange, AES-256/CCM_8 encryption (TLS 1.2+)"
	},
	{
		"RSA_WITH_AES_128_CBC_SHA256",
		BR_TLS_RSA_WITH_AES_128_CBC_SHA256,
		REQ_RSAKEYX | REQ_AESCBC | REQ_SHA256 | REQ_TLS12,
		"RSA key exchange, AES-128/CBC + HMAC/SHA-256 (TLS 1.2+)"
	},
	{
		"RSA_WITH_AES_256_CBC_SHA256",
		BR_TLS_RSA_WITH_AES_256_CBC_SHA256,
		REQ_RSAKEYX | REQ_AESCBC | REQ_SHA256 | REQ_TLS12,
		"RSA key exchange, AES-256/CBC + HMAC/SHA-256 (TLS 1.2+)"
	},
	{
		"RSA_WITH_AES_128_CBC_SHA",
		BR_TLS_RSA_WITH_AES_128_CBC_SHA,
		REQ_RSAKEYX | REQ_AESCBC | REQ_SHA1,
		"RSA key exchange, AES-128/CBC + HMAC/SHA-1"
	},
	{
		"RSA_WITH_AES_256_CBC_SHA",
		BR_TLS_RSA_WITH_AES_256_CBC_SHA,
		REQ_RSAKEYX | REQ_AESCBC | REQ_SHA1,
		"RSA key exchange, AES-256/CBC + HMAC/SHA-1"
	},
	{
		"ECDHE_ECDSA_WITH_3DES_EDE_CBC_SHA",
		BR_TLS_ECDHE_ECDSA_WITH_3DES_EDE_CBC_SHA,
		REQ_ECDHE_ECDSA | REQ_3DESCBC | REQ_SHA1,
		"ECDHE with ECDSA, 3DES/CBC + SHA-1"
	},
	{
		"ECDHE_RSA_WITH_3DES_EDE_CBC_SHA",
		BR_TLS_ECDHE_RSA_WITH_3DES_EDE_CBC_SHA,
		REQ_ECDHE_RSA | REQ_3DESCBC | REQ_SHA1,
		"ECDHE with RSA, 3DES/CBC + SHA-1"
	},
	{
		"ECDH_ECDSA_WITH_3DES_EDE_CBC_SHA",
		BR_TLS_ECDH_ECDSA_WITH_3DES_EDE_CBC_SHA,
		REQ_ECDH | REQ_3DESCBC | REQ_SHA1,
		"ECDH key exchange (EC cert), 3DES/CBC + HMAC/SHA-1"
	},
	{
		"ECDH_RSA_WITH_3DES_EDE_CBC_SHA",
		BR_TLS_ECDH_RSA_WITH_3DES_EDE_CBC_SHA,
		REQ_ECDH | REQ_3DESCBC | REQ_SHA1,
		"ECDH key exchange (RSA cert), 3DES/CBC + HMAC/SHA-1"
	},
	{
		"RSA_WITH_3DES_EDE_CBC_SHA",
		BR_TLS_RSA_WITH_3DES_EDE_CBC_SHA,
		REQ_RSAKEYX | REQ_3DESCBC | REQ_SHA1,
		"RSA key exchange, 3DES/CBC + HMAC/SHA-1"
	},
	{ NULL, 0, 0, NULL }
};

static const struct {
	int id;
	const char *name;
	const char *sid[4];
} curves[] = {
	{ BR_EC_sect163k1,
	  "sect163k1",
	  { "sect163k1", "K-163", NULL, NULL } },
	{ BR_EC_sect163r1,
	  "sect163r1",
	  { "sect163r1", NULL, NULL, NULL } },
	{ BR_EC_sect163r2,
	  "sect163r2",
	  { "sect163r2", "B-163", NULL, NULL } },
	{ BR_EC_sect193r1,
	  "sect193r1",
	  { "sect193r1", NULL, NULL, NULL } },
	{ BR_EC_sect193r2,
	  "sect193r2",
	  { "sect193r2", NULL, NULL, NULL } },
	{ BR_EC_sect233k1,
	  "sect233k1",
	  { "sect233k1", "K-233", NULL, NULL } },
	{ BR_EC_sect233r1,
	  "sect233r1",
	  { "sect233r1", "B-233", NULL, NULL } },
	{ BR_EC_sect239k1,
	  "sect239k1",
	  { "sect239k1", NULL, NULL, NULL } },
	{ BR_EC_sect283k1,
	  "sect283k1",
	  { "sect283k1", "K-283", NULL, NULL } },
	{ BR_EC_sect283r1,
	  "sect283r1",
	  { "sect283r1", "B-283", NULL, NULL } },
	{ BR_EC_sect409k1,
	  "sect409k1",
	  { "sect409k1", "K-409", NULL, NULL } },
	{ BR_EC_sect409r1,
	  "sect409r1",
	  { "sect409r1", "B-409", NULL, NULL } },
	{ BR_EC_sect571k1,
	  "sect571k1",
	  { "sect571k1", "K-571", NULL, NULL } },
	{ BR_EC_sect571r1,
	  "sect571r1",
	  { "sect571r1", "B-571", NULL, NULL } },
	{ BR_EC_secp160k1,
	  "secp160k1",
	  { "secp160k1", NULL, NULL, NULL } },
	{ BR_EC_secp160r1,
	  "secp160r1",
	  { "secp160r1", NULL, NULL, NULL } },
	{ BR_EC_secp160r2,
	  "secp160r2",
	  { "secp160r2", NULL, NULL, NULL } },
	{ BR_EC_secp192k1,
	  "secp192k1",
	  { "secp192k1", NULL, NULL, NULL } },
	{ BR_EC_secp192r1,
	  "secp192r1",
	  { "secp192r1", "P-192", NULL, NULL } },
	{ BR_EC_secp224k1,
	  "secp224k1",
	  { "secp224k1", NULL, NULL, NULL } },
	{ BR_EC_secp224r1,
	  "secp224r1",
	  { "secp224r1", "P-224", NULL, NULL } },
	{ BR_EC_secp256k1,
	  "secp256k1",
	  { "secp256k1", NULL, NULL, NULL } },
	{ BR_EC_secp256r1,
	  "secp256r1 (P-256)",
	  { "secp256r1", "P-256", "prime256v1", NULL } },
	{ BR_EC_secp384r1,
	  "secp384r1 (P-384)",
	  { "secp384r1", "P-384", NULL, NULL } },
	{ BR_EC_secp521r1,
	  "secp521r1 (P-521)",
	  { "secp521r1", "P-521", NULL, NULL } },
	{ BR_EC_brainpoolP256r1,
	  "brainpoolP256r1",
	  { "brainpoolP256r1", NULL, NULL, NULL } },
	{ BR_EC_brainpoolP384r1,
	  "brainpoolP384r1",
	  { "brainpoolP384r1", NULL, NULL, NULL } },
	{ BR_EC_brainpoolP512r1,
	  "brainpoolP512r1",
	  { "brainpoolP512r1", NULL, NULL, NULL } },
	{ BR_EC_curve25519,
	  "Curve25519",
	  { "curve25519", "c25519", NULL, NULL } },
	{ BR_EC_curve448,
	  "Curve448",
	  { "curve448", "c448", NULL, NULL } },
	{ 0, 0, { 0, 0, 0, 0 } }
};

static const struct {
	const char *long_name;
	const char *short_name;
	const void *impl;
} algo_names[] = {
	/* Block ciphers */
	{ "aes_big_cbcenc",    "big",         &br_aes_big_cbcenc_vtable },
	{ "aes_big_cbcdec",    "big",         &br_aes_big_cbcdec_vtable },
	{ "aes_big_ctr",       "big",         &br_aes_big_ctr_vtable },
	{ "aes_big_ctrcbc",    "big",         &br_aes_big_ctrcbc_vtable },
	{ "aes_small_cbcenc",  "small",       &br_aes_small_cbcenc_vtable },
	{ "aes_small_cbcdec",  "small",       &br_aes_small_cbcdec_vtable },
	{ "aes_small_ctr",     "small",       &br_aes_small_ctr_vtable },
	{ "aes_small_ctrcbc",  "small",       &br_aes_small_ctrcbc_vtable },
	{ "aes_ct_cbcenc",     "ct",          &br_aes_ct_cbcenc_vtable },
	{ "aes_ct_cbcdec",     "ct",          &br_aes_ct_cbcdec_vtable },
	{ "aes_ct_ctr",        "ct",          &br_aes_ct_ctr_vtable },
	{ "aes_ct_ctrcbc",     "ct",          &br_aes_ct_ctrcbc_vtable },
	{ "aes_ct64_cbcenc",   "ct64",        &br_aes_ct64_cbcenc_vtable },
	{ "aes_ct64_cbcdec",   "ct64",        &br_aes_ct64_cbcdec_vtable },
	{ "aes_ct64_ctr",      "ct64",        &br_aes_ct64_ctr_vtable },
	{ "aes_ct64_ctrcbc",   "ct64",        &br_aes_ct64_ctrcbc_vtable },

	{ "des_tab_cbcenc",    "tab",         &br_des_tab_cbcenc_vtable },
	{ "des_tab_cbcdec",    "tab",         &br_des_tab_cbcdec_vtable },
	{ "des_ct_cbcenc",     "ct",          &br_des_ct_cbcenc_vtable },
	{ "des_ct_cbcdec",     "ct",          &br_des_ct_cbcdec_vtable },

	{ "chacha20_ct",       "ct",          &br_chacha20_ct_run },

	{ "ghash_ctmul",       "ctmul",       &br_ghash_ctmul },
	{ "ghash_ctmul32",     "ctmul32",     &br_ghash_ctmul32 },
	{ "ghash_ctmul64",     "ctmul64",     &br_ghash_ctmul64 },

	{ "poly1305_ctmul",    "ctmul",       &br_poly1305_ctmul_run },
	{ "poly1305_ctmul32",  "ctmul32",     &br_poly1305_ctmul32_run },

	{ "ec_all_m15",        "all_m15",     &br_ec_all_m15 },
	{ "ec_all_m31",        "all_m31",     &br_ec_all_m31 },
	{ "ec_c25519_i15",     "c25519_i15",  &br_ec_c25519_i15 },
	{ "ec_c25519_i31",     "c25519_i31",  &br_ec_c25519_i31 },
	{ "ec_c25519_m15",     "c25519_m15",  &br_ec_c25519_m15 },
	{ "ec_c25519_m31",     "c25519_m31",  &br_ec_c25519_m31 },
	{ "ec_p256_m15",       "p256_m15",    &br_ec_p256_m15 },
	{ "ec_p256_m31",       "p256_m31",    &br_ec_p256_m31 },
	{ "ec_prime_i15",      "prime_i15",   &br_ec_prime_i15 },
	{ "ec_prime_i31",      "prime_i31",   &br_ec_prime_i31 },

	{ "ecdsa_i15_sign_asn1",  "i15_asn1",  &br_ecdsa_i15_sign_asn1 },
	{ "ecdsa_i15_sign_raw",   "i15_raw",   &br_ecdsa_i15_sign_raw },
	{ "ecdsa_i31_sign_asn1",  "i31_asn1",  &br_ecdsa_i31_sign_asn1 },
	{ "ecdsa_i31_sign_raw",   "i31_raw",   &br_ecdsa_i31_sign_raw },
	{ "ecdsa_i15_vrfy_asn1",  "i15_asn1",  &br_ecdsa_i15_vrfy_asn1 },
	{ "ecdsa_i15_vrfy_raw",   "i15_raw",   &br_ecdsa_i15_vrfy_raw },
	{ "ecdsa_i31_vrfy_asn1",  "i31_asn1",  &br_ecdsa_i31_vrfy_asn1 },
	{ "ecdsa_i31_vrfy_raw",   "i31_raw",   &br_ecdsa_i31_vrfy_raw },

	{ "rsa_i15_pkcs1_sign",   "i15",       &br_rsa_i15_pkcs1_sign },
	{ "rsa_i31_pkcs1_sign",   "i31",       &br_rsa_i31_pkcs1_sign },
	{ "rsa_i32_pkcs1_sign",   "i32",       &br_rsa_i32_pkcs1_sign },
	{ "rsa_i15_pkcs1_vrfy",   "i15",       &br_rsa_i15_pkcs1_vrfy },
	{ "rsa_i31_pkcs1_vrfy",   "i31",       &br_rsa_i31_pkcs1_vrfy },
	{ "rsa_i32_pkcs1_vrfy",   "i32",       &br_rsa_i32_pkcs1_vrfy },

	{ 0, 0, 0 }
};

static const struct {
	const char *long_name;
	const char *short_name;
	const void *(*get)(void);
} algo_names_dyn[] = {
	{ "aes_pwr8_cbcenc",      "pwr8",
		(const void *(*)(void))&br_aes_pwr8_cbcenc_get_vtable },
	{ "aes_pwr8_cbcdec",      "pwr8",
		(const void *(*)(void))&br_aes_pwr8_cbcdec_get_vtable },
	{ "aes_pwr8_ctr",         "pwr8",
		(const void *(*)(void))&br_aes_pwr8_ctr_get_vtable },
	{ "aes_pwr8_ctrcbc",      "pwr8",
		(const void *(*)(void))&br_aes_pwr8_ctrcbc_get_vtable },
	{ "aes_x86ni_cbcenc",     "x86ni",
		(const void *(*)(void))&br_aes_x86ni_cbcenc_get_vtable },
	{ "aes_x86ni_cbcdec",     "x86ni",
		(const void *(*)(void))&br_aes_x86ni_cbcdec_get_vtable },
	{ "aes_x86ni_ctr",        "x86ni",
		(const void *(*)(void))&br_aes_x86ni_ctr_get_vtable },
	{ "aes_x86ni_ctrcbc",     "x86ni",
		(const void *(*)(void))&br_aes_x86ni_ctrcbc_get_vtable },
	{ "chacha20_sse2",        "sse2",
		(const void *(*)(void))&br_chacha20_sse2_get },
	{ "ghash_pclmul",         "pclmul",
		(const void *(*)(void))&br_ghash_pclmul_get },
	{ "ghash_pwr8",           "pwr8",
		(const void *(*)(void))&br_ghash_pwr8_get },
	{ "poly1305_ctmulq",      "ctmulq",
		(const void *(*)(void))&br_poly1305_ctmulq_get },
	{ "rsa_i62_pkcs1_sign",   "i62",
		(const void *(*)(void))&br_rsa_i62_pkcs1_sign_get },
	{ "rsa_i62_pkcs1_vrfy",   "i62",
		(const void *(*)(void))&br_rsa_i62_pkcs1_vrfy_get },
	{ "ec_c25519_m62",        "m62",
		(const void *(*)(void))&br_ec_c25519_m62_get },
	{ "ec_c25519_m64",        "m64",
		(const void *(*)(void))&br_ec_c25519_m64_get },
	{ "ec_p256_m62",          "m62",
		(const void *(*)(void))&br_ec_p256_m62_get },
	{ "ec_p256_m64",          "m64",
		(const void *(*)(void))&br_ec_p256_m64_get },
	{ 0, 0, 0, }
};

/* see brssl.h */
const char *
get_algo_name(const void *impl, int long_name)
{
	size_t u;

	for (u = 0; algo_names[u].long_name; u ++) {
		if (impl == algo_names[u].impl) {
			return long_name
				? algo_names[u].long_name
				: algo_names[u].short_name;
		}
	}
	for (u = 0; algo_names_dyn[u].long_name; u ++) {
		if (impl == algo_names_dyn[u].get()) {
			return long_name
				? algo_names_dyn[u].long_name
				: algo_names_dyn[u].short_name;
		}
	}
	return "UNKNOWN";
}

/* see brssl.h */
const char *
get_curve_name(int id)
{
	size_t u;

	for (u = 0; curves[u].name; u ++) {
		if (curves[u].id == id) {
			return curves[u].name;
		}
	}
	return NULL;
}

/* see brssl.h */
int
get_curve_name_ext(int id, char *dst, size_t len)
{
	const char *name;
	char tmp[30];
	size_t n;

	name = get_curve_name(id);
	if (name == NULL) {
		sprintf(tmp, "unknown (%d)", id);
		name = tmp;
	}
	n = 1 + strlen(name);
	if (n > len) {
		if (len > 0) {
			dst[0] = 0;
		}
		return -1;
	}
	memcpy(dst, name, n);
	return 0;
}

/* see brssl.h */
const char *
get_suite_name(unsigned suite)
{
	size_t u;

	for (u = 0; cipher_suites[u].name; u ++) {
		if (cipher_suites[u].suite == suite) {
			return cipher_suites[u].name;
		}
	}
	return NULL;
}

/* see brssl.h */
int
get_suite_name_ext(unsigned suite, char *dst, size_t len)
{
	const char *name;
	char tmp[30];
	size_t n;

	name = get_suite_name(suite);
	if (name == NULL) {
		sprintf(tmp, "unknown (0x%04X)", suite);
		name = tmp;
	}
	n = 1 + strlen(name);
	if (n > len) {
		if (len > 0) {
			dst[0] = 0;
		}
		return -1;
	}
	memcpy(dst, name, n);
	return 0;
}

/* see brssl.h */
int
uses_ecdhe(unsigned suite)
{
	size_t u;

	for (u = 0; cipher_suites[u].name; u ++) {
		if (cipher_suites[u].suite == suite) {
			return (cipher_suites[u].req
				& (REQ_ECDHE_RSA | REQ_ECDHE_ECDSA)) != 0;
		}
	}
	return 0;
}

/* see brssl.h */
void
list_names(void)
{
	size_t u;

	printf("Protocol versions:\n");
	for (u = 0; protocol_versions[u].name; u ++) {
		printf("   %-8s %s\n",
			protocol_versions[u].name,
			protocol_versions[u].comment);
	}
	printf("Hash functions:\n");
	for (u = 0; hash_functions[u].name; u ++) {
		printf("   %-8s %s\n",
			hash_functions[u].name,
			hash_functions[u].comment);
	}
	printf("Cipher suites:\n");
	for (u = 0; cipher_suites[u].name; u ++) {
		printf("   %s\n        %s\n",
			cipher_suites[u].name,
			cipher_suites[u].comment);
	}
}

/* see brssl.h */
void
list_curves(void)
{
	size_t u;
	for (u = 0; curves[u].name; u ++) {
		size_t v;

		for (v = 0; curves[u].sid[v]; v ++) {
			if (v == 0) {
				printf("   ");
			} else if (v == 1) {
				printf(" (");
			} else {
				printf(", ");
			}
			printf("%s", curves[u].sid[v]);
		}
		if (v > 1) {
			printf(")");
		}
		printf("\n");
	}
}

static int
is_ign(int c)
{
	if (c == 0) {
		return 0;
	}
	if (c <= 32 || c == '-' || c == '_' || c == '.'
		|| c == '/' || c == '+' || c == ':')
	{
		return 1;
	}
	return 0;
}

/*
 * Get next non-ignored character, normalised:
 *    ASCII letters are converted to lowercase
 *    control characters, space, '-', '_', '.', '/', '+' and ':' are ignored
 * A terminating zero is returned as 0.
 */
static int
next_char(const char **ps, const char *limit)
{
	for (;;) {
		int c;

		if (*ps == limit) {
			return 0;
		}
		c = *(*ps) ++;
		if (c == 0) {
			return 0;
		}
		if (c >= 'A' && c <= 'Z') {
			c += 'a' - 'A';
		}
		if (!is_ign(c)) {
			return c;
		}
	}
}

/*
 * Partial string equality comparison, with normalisation.
 */
static int
eqstr_chunk(const char *s1, size_t s1_len, const char *s2, size_t s2_len)
{
	const char *lim1, *lim2;

	lim1 = s1 + s1_len;
	lim2 = s2 + s2_len;
	for (;;) {
		int c1, c2;

		c1 = next_char(&s1, lim1);
		c2 = next_char(&s2, lim2);
		if (c1 != c2) {
			return 0;
		}
		if (c1 == 0) {
			return 1;
		}
	}
}

/* see brssl.h */
int
eqstr(const char *s1, const char *s2)
{
	return eqstr_chunk(s1, strlen(s1), s2, strlen(s2));
}

static int
hexval(int c)
{
	if (c >= '0' && c <= '9') {
		return c - '0';
	} else if (c >= 'A' && c <= 'F') {
		return c - 'A' + 10;
	} else if (c >= 'a' && c <= 'f') {
		return c - 'a' + 10;
	} else {
		return -1;
	}
}

/* see brssl.h */
size_t
parse_size(const char *s)
{
	int radix;
	size_t acc;
	const char *t;

	t = s;
	if (t[0] == '0' && (t[1] == 'x' || t[1] == 'X')) {
		radix = 16;
		t += 2;
	} else {
		radix = 10;
	}
	acc = 0;
	for (;;) {
		int c, d;
		size_t z;

		c = *t ++;
		if (c == 0) {
			return acc;
		}
		d = hexval(c);
		if (d < 0 || d >= radix) {
			fprintf(stderr, "ERROR: not a valid digit: '%c'\n", c);
			return (size_t)-1;
		}
		z = acc * (size_t)radix + (size_t)d;
		if (z < (size_t)d || (z / (size_t)radix) != acc
			|| z == (size_t)-1)
		{
			fprintf(stderr, "ERROR: value too large: %s\n", s);
			return (size_t)-1;
		}
		acc = z;
	}
}

/*
 * Comma-separated list enumeration. This returns a pointer to the first
 * word in the string, skipping leading ignored characters. '*len' is
 * set to the word length (not counting trailing ignored characters).
 * '*str' is updated to point to immediately after the next comma, or to
 * the terminating zero, whichever comes first.
 *
 * Empty words are skipped. If there is no next non-empty word, then this
 * function returns NULL and sets *len to 0.
 */
static const char *
next_word(const char **str, size_t *len)
{
	int c;
	const char *begin;
	size_t u;

	/*
	 * Find next non-ignored character which is not a comma.
	 */
	for (;;) {
		c = **str;
		if (c == 0) {
			*len = 0;
			return NULL;
		}
		if (!is_ign(c) && c != ',') {
			break;
		}
		(*str) ++;
	}

	/*
	 * Find next comma or terminator.
	 */
	begin = *str;
	for (;;) {
		c = *(*str);
		if (c == 0 || c == ',') {
			break;
		}
		(*str) ++;
	}

	/*
	 * Remove trailing ignored characters.
	 */
	u = (size_t)(*str - begin);
	while (u > 0 && is_ign(begin[u - 1])) {
		u --;
	}
	if (c == ',') {
		(*str) ++;
	}
	*len = u;
	return begin;
}

/* see brssl.h */
unsigned
parse_version(const char *name, size_t len)
{
	size_t u;

	for (u = 0;; u ++) {
		const char *ref;

		ref = protocol_versions[u].name;
		if (ref == NULL) {
			fprintf(stderr, "ERROR: unrecognised protocol"
				" version name: '%s'\n", name);
			return 0;
		}
		if (eqstr_chunk(ref, strlen(ref), name, len)) {
			return protocol_versions[u].version;
		}
	}
}

/* see brssl.h */
unsigned
parse_hash_functions(const char *arg)
{
	unsigned r;

	r = 0;
	for (;;) {
		const char *name;
		size_t len;
		size_t u;

		name = next_word(&arg, &len);
		if (name == NULL) {
			break;
		}
		for (u = 0;; u ++) {
			const char *ref;

			ref = hash_functions[u].name;
			if (ref == 0) {
				fprintf(stderr, "ERROR: unrecognised"
					" hash function name: '");
				fwrite(name, 1, len, stderr);
				fprintf(stderr, "'\n");
				return 0;
			}
			if (eqstr_chunk(ref, strlen(ref), name, len)) {
				int id;

				id = (hash_functions[u].hclass->desc
					>> BR_HASHDESC_ID_OFF)
					& BR_HASHDESC_ID_MASK;
				r |= (unsigned)1 << id;
				break;
			}
		}
	}
	if (r == 0) {
		fprintf(stderr, "ERROR: no hash function name provided\n");
	}
	return r;
}

/* see brssl.h */
cipher_suite *
parse_suites(const char *arg, size_t *num)
{
	VECTOR(cipher_suite) suites = VEC_INIT;
	cipher_suite *r;

	for (;;) {
		const char *name;
		size_t u, len;

		name = next_word(&arg, &len);
		if (name == NULL) {
			break;
		}
		for (u = 0;; u ++) {
			const char *ref;

			ref = cipher_suites[u].name;
			if (ref == NULL) {
				fprintf(stderr, "ERROR: unrecognised"
					" cipher suite '");
				fwrite(name, 1, len, stderr);
				fprintf(stderr, "'\n");
				return 0;
			}
			if (eqstr_chunk(ref, strlen(ref), name, len)) {
				VEC_ADD(suites, cipher_suites[u]);
				break;
			}
		}
	}
	if (VEC_LEN(suites) == 0) {
		fprintf(stderr, "ERROR: no cipher suite provided\n");
	}
	r = VEC_TOARRAY(suites);
	*num = VEC_LEN(suites);
	VEC_CLEAR(suites);
	return r;
}

/* see brssl.h */
const char *
ec_curve_name(int curve)
{
	switch (curve) {
	case BR_EC_sect163k1:        return "sect163k1";
	case BR_EC_sect163r1:        return "sect163r1";
	case BR_EC_sect163r2:        return "sect163r2";
	case BR_EC_sect193r1:        return "sect193r1";
	case BR_EC_sect193r2:        return "sect193r2";
	case BR_EC_sect233k1:        return "sect233k1";
	case BR_EC_sect233r1:        return "sect233r1";
	case BR_EC_sect239k1:        return "sect239k1";
	case BR_EC_sect283k1:        return "sect283k1";
	case BR_EC_sect283r1:        return "sect283r1";
	case BR_EC_sect409k1:        return "sect409k1";
	case BR_EC_sect409r1:        return "sect409r1";
	case BR_EC_sect571k1:        return "sect571k1";
	case BR_EC_sect571r1:        return "sect571r1";
	case BR_EC_secp160k1:        return "secp160k1";
	case BR_EC_secp160r1:        return "secp160r1";
	case BR_EC_secp160r2:        return "secp160r2";
	case BR_EC_secp192k1:        return "secp192k1";
	case BR_EC_secp192r1:        return "secp192r1";
	case BR_EC_secp224k1:        return "secp224k1";
	case BR_EC_secp224r1:        return "secp224r1";
	case BR_EC_secp256k1:        return "secp256k1";
	case BR_EC_secp256r1:        return "secp256r1";
	case BR_EC_secp384r1:        return "secp384r1";
	case BR_EC_secp521r1:        return "secp521r1";
	case BR_EC_brainpoolP256r1:  return "brainpoolP256r1";
	case BR_EC_brainpoolP384r1:  return "brainpoolP384r1";
	case BR_EC_brainpoolP512r1:  return "brainpoolP512r1";
	default:
		return "unknown";
	}
}

/* see brssl.h */
int
get_curve_by_name(const char *str)
{
	size_t u, v;

	for (u = 0; curves[u].name; u ++) {
		for (v = 0; curves[u].sid[v]; v ++) {
			if (eqstr(curves[u].sid[v], str)) {
				return curves[u].id;
			}
		}
	}
	return -1;
}

/* see brssl.h */
const char *
hash_function_name(int id)
{
	switch (id) {
	case br_md5sha1_ID:  return "MD5+SHA-1";
	case br_md5_ID:      return "MD5";
	case br_sha1_ID:     return "SHA-1";
	case br_sha224_ID:   return "SHA-224";
	case br_sha256_ID:   return "SHA-256";
	case br_sha384_ID:   return "SHA-384";
	case br_sha512_ID:   return "SHA-512";
	default:
		return "unknown";
	}
}
