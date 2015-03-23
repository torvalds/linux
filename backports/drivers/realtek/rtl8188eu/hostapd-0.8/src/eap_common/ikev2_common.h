/*
 * IKEv2 definitions
 * Copyright (c) 2007, Jouni Malinen <j@w1.fi>
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

#ifndef IKEV2_COMMON_H
#define IKEV2_COMMON_H

/*
 * Nonce length must be at least 16 octets. It must also be at least half the
 * key size of the negotiated PRF.
 */
#define IKEV2_NONCE_MIN_LEN 16
#define IKEV2_NONCE_MAX_LEN 256

/* IKE Header - RFC 4306, Sect. 3.1 */
#ifdef _MSC_VER
#pragma pack(push, 1)
#endif /* _MSC_VER */

#define IKEV2_SPI_LEN 8

struct ikev2_hdr {
	u8 i_spi[IKEV2_SPI_LEN]; /* IKE_SA Initiator's SPI */
	u8 r_spi[IKEV2_SPI_LEN]; /* IKE_SA Responder's SPI */
	u8 next_payload;
	u8 version; /* MjVer | MnVer */
	u8 exchange_type;
	u8 flags;
	u8 message_id[4];
	u8 length[4]; /* total length of HDR + payloads */
} STRUCT_PACKED;

struct ikev2_payload_hdr {
	u8 next_payload;
	u8 flags;
	u8 payload_length[2]; /* this payload, including the payload header */
} STRUCT_PACKED;

struct ikev2_proposal {
	u8 type; /* 0 (last) or 2 (more) */
	u8 reserved;
	u8 proposal_length[2]; /* including all transform and attributes */
	u8 proposal_num;
	u8 protocol_id; /* IKEV2_PROTOCOL_* */
	u8 spi_size;
	u8 num_transforms;
	/* SPI of spi_size octets */
	/* Transforms */
} STRUCT_PACKED;

struct ikev2_transform {
	u8 type; /* 0 (last) or 3 (more) */
	u8 reserved;
	u8 transform_length[2]; /* including Header and Attributes */
	u8 transform_type;
	u8 reserved2;
	u8 transform_id[2];
	/* Transform Attributes */
} STRUCT_PACKED;

#ifdef _MSC_VER
#pragma pack(pop)
#endif /* _MSC_VER */


/* Current IKEv2 version from RFC 4306 */
#define IKEV2_MjVer 2
#define IKEV2_MnVer 0
#ifdef CCNS_PL
#define IKEV2_VERSION ((IKEV2_MjVer) | ((IKEV2_MnVer) << 4))
#else /* CCNS_PL */
#define IKEV2_VERSION (((IKEV2_MjVer) << 4) | (IKEV2_MnVer))
#endif /* CCNS_PL */

/* IKEv2 Exchange Types */
enum {
	/* 0-33 RESERVED */
	IKE_SA_INIT = 34,
	IKE_SA_AUTH = 35,
	CREATE_CHILD_SA = 36,
	INFORMATION = 37
	/* 38-239 RESERVED TO IANA */
	/* 240-255 Reserved for private use */
};

/* IKEv2 Flags */
#define IKEV2_HDR_INITIATOR	0x08
#define IKEV2_HDR_VERSION	0x10
#define IKEV2_HDR_RESPONSE	0x20

/* Payload Header Flags */
#define IKEV2_PAYLOAD_FLAGS_CRITICAL 0x01


/* EAP-IKEv2 Payload Types (in Next Payload Type field)
 * http://www.iana.org/assignments/eap-ikev2-payloads */
enum {
	IKEV2_PAYLOAD_NO_NEXT_PAYLOAD = 0,
	IKEV2_PAYLOAD_SA = 33,
	IKEV2_PAYLOAD_KEY_EXCHANGE = 34,
	IKEV2_PAYLOAD_IDi = 35,
	IKEV2_PAYLOAD_IDr = 36,
	IKEV2_PAYLOAD_CERTIFICATE = 37,
	IKEV2_PAYLOAD_CERT_REQ = 38,
	IKEV2_PAYLOAD_AUTHENTICATION = 39,
	IKEV2_PAYLOAD_NONCE = 40,
	IKEV2_PAYLOAD_NOTIFICATION = 41,
	IKEV2_PAYLOAD_VENDOD_ID = 43,
	IKEV2_PAYLOAD_ENCRYPTED = 46,
	IKEV2_PAYLOAD_NEXT_FAST_ID = 121
};


/* IKEv2 Proposal - Protocol ID */
enum {
	IKEV2_PROTOCOL_RESERVED = 0,
	IKEV2_PROTOCOL_IKE = 1, /* IKE is the only one allowed for EAP-IKEv2 */
	IKEV2_PROTOCOL_AH = 2,
	IKEV2_PROTOCOL_ESP = 3
};


/* IKEv2 Transform Types */
enum {
	IKEV2_TRANSFORM_ENCR = 1,
	IKEV2_TRANSFORM_PRF = 2,
	IKEV2_TRANSFORM_INTEG = 3,
	IKEV2_TRANSFORM_DH = 4,
	IKEV2_TRANSFORM_ESN = 5
};

/* IKEv2 Tranform Type 1 (Encryption Algorithm) */
enum {
	ENCR_DES_IV64 = 1,
	ENCR_DES = 2,
	ENCR_3DES = 3,
	ENCR_RC5 = 4,
	ENCR_IDEA = 5,
	ENCR_CAST = 6,
	ENCR_BLOWFISH = 7,
	ENCR_3IDEA = 8,
	ENCR_DES_IV32 = 9,
	ENCR_NULL = 11,
	ENCR_AES_CBC = 12,
	ENCR_AES_CTR = 13
};

/* IKEv2 Transform Type 2 (Pseudo-random Function) */
enum {
	PRF_HMAC_MD5 = 1,
	PRF_HMAC_SHA1 = 2,
	PRF_HMAC_TIGER = 3,
	PRF_AES128_XCBC = 4
};

/* IKEv2 Transform Type 3 (Integrity Algorithm) */
enum {
	AUTH_HMAC_MD5_96 = 1,
	AUTH_HMAC_SHA1_96 = 2,
	AUTH_DES_MAC = 3,
	AUTH_KPDK_MD5 = 4,
	AUTH_AES_XCBC_96 = 5
};

/* IKEv2 Transform Type 4 (Diffie-Hellman Group) */
enum {
	DH_GROUP1_768BIT_MODP = 1, /* RFC 4306 */
	DH_GROUP2_1024BIT_MODP = 2, /* RFC 4306 */
	DH_GROUP5_1536BIT_MODP = 5, /* RFC 3526 */
	DH_GROUP5_2048BIT_MODP = 14, /* RFC 3526 */
	DH_GROUP5_3072BIT_MODP = 15, /* RFC 3526 */
	DH_GROUP5_4096BIT_MODP = 16, /* RFC 3526 */
	DH_GROUP5_6144BIT_MODP = 17, /* RFC 3526 */
	DH_GROUP5_8192BIT_MODP = 18 /* RFC 3526 */
};


/* Identification Data Types (RFC 4306, Sect. 3.5) */
enum {
	ID_IPV4_ADDR = 1,
	ID_FQDN = 2,
	ID_RFC822_ADDR = 3,
	ID_IPV6_ADDR = 5,
	ID_DER_ASN1_DN = 9,
	ID_DER_ASN1_GN= 10,
	ID_KEY_ID = 11
};


/* Certificate Encoding (RFC 4306, Sect. 3.6) */
enum {
	CERT_ENCODING_PKCS7_X509 = 1,
	CERT_ENCODING_PGP_CERT = 2,
	CERT_ENCODING_DNS_SIGNED_KEY = 3,
	/* X.509 Certificate - Signature: DER encoded X.509 certificate whose
	 * public key is used to validate the sender's AUTH payload */
	CERT_ENCODING_X509_CERT_SIGN = 4,
	CERT_ENCODING_KERBEROS_TOKEN = 6,
	/* DER encoded X.509 certificate revocation list */
	CERT_ENCODING_CRL = 7,
	CERT_ENCODING_ARL = 8,
	CERT_ENCODING_SPKI_CERT = 9,
	CERT_ENCODING_X509_CERT_ATTR = 10,
	/* PKCS #1 encoded RSA key */
	CERT_ENCODING_RAW_RSA_KEY = 11,
	CERT_ENCODING_HASH_AND_URL_X509_CERT = 12,
	CERT_ENCODING_HASH_AND_URL_X509_BUNDLE = 13
};


/* Authentication Method (RFC 4306, Sect. 3.8) */
enum {
	AUTH_RSA_SIGN = 1,
	AUTH_SHARED_KEY_MIC = 2,
	AUTH_DSS_SIGN = 3
};


/* Notify Message Types (RFC 4306, Sect. 3.10.1) */
enum {
	UNSUPPORTED_CRITICAL_PAYLOAD = 1,
	INVALID_IKE_SPI = 4,
	INVALID_MAJOR_VERSION = 5,
	INVALID_SYNTAX = 7,
	INVALID_MESSAGE_ID = 9,
	INVALID_SPI = 11,
	NO_PROPOSAL_CHOSEN = 14,
	INVALID_KE_PAYLOAD = 17,
	AUTHENTICATION_FAILED = 24,
	SINGLE_PAIR_REQUIRED = 34,
	NO_ADDITIONAL_SAS = 35,
	INTERNAL_ADDRESS_FAILURE = 36,
	FAILED_CP_REQUIRED = 37,
	TS_UNACCEPTABLE = 38,
	INVALID_SELECTORS = 39
};


struct ikev2_keys {
	u8 *SK_d, *SK_ai, *SK_ar, *SK_ei, *SK_er, *SK_pi, *SK_pr;
	size_t SK_d_len, SK_integ_len, SK_encr_len, SK_prf_len;
};


int ikev2_keys_set(struct ikev2_keys *keys);
void ikev2_free_keys(struct ikev2_keys *keys);


/* Maximum hash length for supported hash algorithms */
#define IKEV2_MAX_HASH_LEN 20

struct ikev2_integ_alg {
	int id;
	size_t key_len;
	size_t hash_len;
};

struct ikev2_prf_alg {
	int id;
	size_t key_len;
	size_t hash_len;
};

struct ikev2_encr_alg {
	int id;
	size_t key_len;
	size_t block_size;
};

const struct ikev2_integ_alg * ikev2_get_integ(int id);
int ikev2_integ_hash(int alg, const u8 *key, size_t key_len, const u8 *data,
		     size_t data_len, u8 *hash);
const struct ikev2_prf_alg * ikev2_get_prf(int id);
int ikev2_prf_hash(int alg, const u8 *key, size_t key_len,
		   size_t num_elem, const u8 *addr[], const size_t *len,
		   u8 *hash);
int ikev2_prf_plus(int alg, const u8 *key, size_t key_len,
		   const u8 *data, size_t data_len,
		   u8 *out, size_t out_len);
const struct ikev2_encr_alg * ikev2_get_encr(int id);
int ikev2_encr_encrypt(int alg, const u8 *key, size_t key_len, const u8 *iv,
		       const u8 *plain, u8 *crypt, size_t len);
int ikev2_encr_decrypt(int alg, const u8 *key, size_t key_len, const u8 *iv,
		       const u8 *crypt, u8 *plain, size_t len);

int ikev2_derive_auth_data(int prf_alg, const struct wpabuf *sign_msg,
			   const u8 *ID, size_t ID_len, u8 ID_type,
			   struct ikev2_keys *keys, int initiator,
			   const u8 *shared_secret, size_t shared_secret_len,
			   const u8 *nonce, size_t nonce_len,
			   const u8 *key_pad, size_t key_pad_len,
			   u8 *auth_data);


struct ikev2_payloads {
	const u8 *sa;
	size_t sa_len;
	const u8 *ke;
	size_t ke_len;
	const u8 *idi;
	size_t idi_len;
	const u8 *idr;
	size_t idr_len;
	const u8 *cert;
	size_t cert_len;
	const u8 *auth;
	size_t auth_len;
	const u8 *nonce;
	size_t nonce_len;
	const u8 *encrypted;
	size_t encrypted_len;
	u8 encr_next_payload;
	const u8 *notification;
	size_t notification_len;
};

int ikev2_parse_payloads(struct ikev2_payloads *payloads,
			 u8 next_payload, const u8 *pos, const u8 *end);

u8 * ikev2_decrypt_payload(int encr_id, int integ_id, struct ikev2_keys *keys,
			   int initiator, const struct ikev2_hdr *hdr,
			   const u8 *encrypted, size_t encrypted_len,
			   size_t *res_len);
void ikev2_update_hdr(struct wpabuf *msg);
int ikev2_build_encrypted(int encr_id, int integ_id, struct ikev2_keys *keys,
			  int initiator, struct wpabuf *msg,
			  struct wpabuf *plain, u8 next_payload);
int ikev2_derive_sk_keys(const struct ikev2_prf_alg *prf,
			 const struct ikev2_integ_alg *integ,
			 const struct ikev2_encr_alg *encr,
			 const u8 *skeyseed, const u8 *data, size_t data_len,
			 struct ikev2_keys *keys);

#endif /* IKEV2_COMMON_H */
