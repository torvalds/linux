/*
 * EAP server/peer: EAP-PAX shared routines
 * Copyright (c) 2005-2007, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef EAP_PAX_COMMON_H
#define EAP_PAX_COMMON_H

#ifdef _MSC_VER
#pragma pack(push, 1)
#endif /* _MSC_VER */

struct eap_pax_hdr {
	u8 op_code;
	u8 flags;
	u8 mac_id;
	u8 dh_group_id;
	u8 public_key_id;
	/* Followed by variable length payload and ICV */
} STRUCT_PACKED;

#ifdef _MSC_VER
#pragma pack(pop)
#endif /* _MSC_VER */


/* op_code: */
enum {
	EAP_PAX_OP_STD_1 = 0x01,
	EAP_PAX_OP_STD_2 = 0x02,
	EAP_PAX_OP_STD_3 = 0x03,
	EAP_PAX_OP_SEC_1 = 0x11,
	EAP_PAX_OP_SEC_2 = 0x12,
	EAP_PAX_OP_SEC_3 = 0x13,
	EAP_PAX_OP_SEC_4 = 0x14,
	EAP_PAX_OP_SEC_5 = 0x15,
	EAP_PAX_OP_ACK = 0x21
};

/* flags: */
#define EAP_PAX_FLAGS_MF			0x01
#define EAP_PAX_FLAGS_CE			0x02
#define EAP_PAX_FLAGS_AI			0x04

/* mac_id: */
#define EAP_PAX_MAC_HMAC_SHA1_128		0x01
#define EAP_PAX_HMAC_SHA256_128			0x02

/* dh_group_id: */
#define EAP_PAX_DH_GROUP_NONE			0x00
#define EAP_PAX_DH_GROUP_2048_MODP		0x01
#define EAP_PAX_DH_GROUP_3072_MODP		0x02
#define EAP_PAX_DH_GROUP_NIST_ECC_P_256		0x03

/* public_key_id: */
#define EAP_PAX_PUBLIC_KEY_NONE			0x00
#define EAP_PAX_PUBLIC_KEY_RSAES_OAEP		0x01
#define EAP_PAX_PUBLIC_KEY_RSA_PKCS1_V1_5	0x02
#define EAP_PAX_PUBLIC_KEY_EL_GAMAL_NIST_ECC	0x03

/* ADE type: */
#define EAP_PAX_ADE_VENDOR_SPECIFIC		0x01
#define EAP_PAX_ADE_CLIENT_CHANNEL_BINDING	0x02
#define EAP_PAX_ADE_SERVER_CHANNEL_BINDING	0x03


#define EAP_PAX_RAND_LEN 32
#define EAP_PAX_MAC_LEN 16
#define EAP_PAX_ICV_LEN 16
#define EAP_PAX_AK_LEN 16
#define EAP_PAX_MK_LEN 16
#define EAP_PAX_CK_LEN 16
#define EAP_PAX_ICK_LEN 16
#define EAP_PAX_MID_LEN 16


int eap_pax_kdf(u8 mac_id, const u8 *key, size_t key_len,
		const char *identifier,
		const u8 *entropy, size_t entropy_len,
		size_t output_len, u8 *output);
int eap_pax_mac(u8 mac_id, const u8 *key, size_t key_len,
		const u8 *data1, size_t data1_len,
		const u8 *data2, size_t data2_len,
		const u8 *data3, size_t data3_len,
		u8 *mac);
int eap_pax_initial_key_derivation(u8 mac_id, const u8 *ak, const u8 *e,
				   u8 *mk, u8 *ck, u8 *ick, u8 *mid);

#endif /* EAP_PAX_COMMON_H */
