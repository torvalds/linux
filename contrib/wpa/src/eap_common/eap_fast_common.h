/*
 * EAP-FAST definitions (RFC 4851)
 * Copyright (c) 2004-2008, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef EAP_FAST_H
#define EAP_FAST_H

#define EAP_FAST_VERSION 1
#define EAP_FAST_KEY_LEN 64
#define EAP_FAST_SIMCK_LEN 40
#define EAP_FAST_SKS_LEN 40
#define EAP_FAST_CMK_LEN 20

#define TLS_EXT_PAC_OPAQUE 35

/*
 * RFC 5422: Section 4.2.1 - Formats for PAC TLV Attributes / Type Field
 * Note: bit 0x8000 (Mandatory) and bit 0x4000 (Reserved) are also defined
 * in the general PAC TLV format (Section 4.2).
 */
#define PAC_TYPE_PAC_KEY 1
#define PAC_TYPE_PAC_OPAQUE 2
#define PAC_TYPE_CRED_LIFETIME 3
#define PAC_TYPE_A_ID 4
#define PAC_TYPE_I_ID 5
/*
 * 6 was previous assigned for SERVER_PROTECTED_DATA, but
 * draft-cam-winget-eap-fast-provisioning-02.txt changed this to Reserved.
 */
#define PAC_TYPE_A_ID_INFO 7
#define PAC_TYPE_PAC_ACKNOWLEDGEMENT 8
#define PAC_TYPE_PAC_INFO 9
#define PAC_TYPE_PAC_TYPE 10

#ifdef _MSC_VER
#pragma pack(push, 1)
#endif /* _MSC_VER */

struct pac_tlv_hdr {
	be16 type;
	be16 len;
} STRUCT_PACKED;

#ifdef _MSC_VER
#pragma pack(pop)
#endif /* _MSC_VER */


#define EAP_FAST_PAC_KEY_LEN 32

/* RFC 5422: 4.2.6 PAC-Type TLV */
#define PAC_TYPE_TUNNEL_PAC 1
/* Application Specific Short Lived PACs (only in volatile storage) */
/* User Authorization PAC */
#define PAC_TYPE_USER_AUTHORIZATION 3
/* Application Specific Long Lived PACs */
/* Machine Authentication PAC */
#define PAC_TYPE_MACHINE_AUTHENTICATION 2


/*
 * RFC 5422:
 * Section 3.3 - Key Derivations Used in the EAP-FAST Provisioning Exchange
 */
struct eap_fast_key_block_provisioning {
	/* Extra key material after TLS key_block */
	u8 session_key_seed[EAP_FAST_SKS_LEN];
	u8 server_challenge[16]; /* MSCHAPv2 ServerChallenge */
	u8 client_challenge[16]; /* MSCHAPv2 ClientChallenge */
};


struct wpabuf;
struct tls_connection;

struct eap_fast_tlv_parse {
	u8 *eap_payload_tlv;
	size_t eap_payload_tlv_len;
	struct eap_tlv_crypto_binding_tlv *crypto_binding;
	size_t crypto_binding_len;
	int iresult;
	int result;
	int request_action;
	u8 *pac;
	size_t pac_len;
};

void eap_fast_put_tlv_hdr(struct wpabuf *buf, u16 type, u16 len);
void eap_fast_put_tlv(struct wpabuf *buf, u16 type, const void *data,
		      u16 len);
void eap_fast_put_tlv_buf(struct wpabuf *buf, u16 type,
			  const struct wpabuf *data);
struct wpabuf * eap_fast_tlv_eap_payload(struct wpabuf *buf);
void eap_fast_derive_master_secret(const u8 *pac_key, const u8 *server_random,
				   const u8 *client_random, u8 *master_secret);
u8 * eap_fast_derive_key(void *ssl_ctx, struct tls_connection *conn,
			 size_t len);
int eap_fast_derive_eap_msk(const u8 *simck, u8 *msk);
int eap_fast_derive_eap_emsk(const u8 *simck, u8 *emsk);
int eap_fast_parse_tlv(struct eap_fast_tlv_parse *tlv,
		       int tlv_type, u8 *pos, size_t len);

#endif /* EAP_FAST_H */
