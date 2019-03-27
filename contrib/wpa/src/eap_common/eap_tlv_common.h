/*
 * EAP-TLV definitions (draft-josefsson-pppext-eap-tls-eap-10.txt)
 * Copyright (c) 2004-2008, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef EAP_TLV_COMMON_H
#define EAP_TLV_COMMON_H

/* EAP-TLV TLVs (draft-josefsson-ppext-eap-tls-eap-10.txt) */
#define EAP_TLV_RESULT_TLV 3 /* Acknowledged Result */
#define EAP_TLV_NAK_TLV 4
#define EAP_TLV_ERROR_CODE_TLV 5
#define EAP_TLV_CONNECTION_BINDING_TLV 6
#define EAP_TLV_VENDOR_SPECIFIC_TLV 7
#define EAP_TLV_URI_TLV 8
#define EAP_TLV_EAP_PAYLOAD_TLV 9
#define EAP_TLV_INTERMEDIATE_RESULT_TLV 10
#define EAP_TLV_PAC_TLV 11 /* RFC 5422, Section 4.2 */
#define EAP_TLV_CRYPTO_BINDING_TLV 12
#define EAP_TLV_CALLING_STATION_ID_TLV 13
#define EAP_TLV_CALLED_STATION_ID_TLV 14
#define EAP_TLV_NAS_PORT_TYPE_TLV 15
#define EAP_TLV_SERVER_IDENTIFIER_TLV 16
#define EAP_TLV_IDENTITY_TYPE_TLV 17
#define EAP_TLV_SERVER_TRUSTED_ROOT_TLV 18
#define EAP_TLV_REQUEST_ACTION_TLV 19
#define EAP_TLV_PKCS7_TLV 20

#define EAP_TLV_RESULT_SUCCESS 1
#define EAP_TLV_RESULT_FAILURE 2

#define EAP_TLV_TYPE_MANDATORY 0x8000
#define EAP_TLV_TYPE_MASK 0x3fff

#ifdef _MSC_VER
#pragma pack(push, 1)
#endif /* _MSC_VER */

struct eap_tlv_hdr {
	be16 tlv_type;
	be16 length;
} STRUCT_PACKED;

struct eap_tlv_nak_tlv {
	be16 tlv_type;
	be16 length;
	be32 vendor_id;
	be16 nak_type;
} STRUCT_PACKED;

struct eap_tlv_result_tlv {
	be16 tlv_type;
	be16 length;
	be16 status;
} STRUCT_PACKED;

/* RFC 4851, Section 4.2.7 - Intermediate-Result TLV */
struct eap_tlv_intermediate_result_tlv {
	be16 tlv_type;
	be16 length;
	be16 status;
	/* Followed by optional TLVs */
} STRUCT_PACKED;

/* RFC 4851, Section 4.2.8 - Crypto-Binding TLV */
struct eap_tlv_crypto_binding_tlv {
	be16 tlv_type;
	be16 length;
	u8 reserved;
	u8 version;
	u8 received_version;
	u8 subtype;
	u8 nonce[32];
	u8 compound_mac[20];
} STRUCT_PACKED;

struct eap_tlv_pac_ack_tlv {
	be16 tlv_type;
	be16 length;
	be16 pac_type;
	be16 pac_len;
	be16 result;
} STRUCT_PACKED;

/* RFC 4851, Section 4.2.9 - Request-Action TLV */
struct eap_tlv_request_action_tlv {
	be16 tlv_type;
	be16 length;
	be16 action;
} STRUCT_PACKED;

/* RFC 5422, Section 4.2.6 - PAC-Type TLV */
struct eap_tlv_pac_type_tlv {
	be16 tlv_type; /* PAC_TYPE_PAC_TYPE */
	be16 length;
	be16 pac_type;
} STRUCT_PACKED;

#ifdef _MSC_VER
#pragma pack(pop)
#endif /* _MSC_VER */

#define EAP_TLV_CRYPTO_BINDING_SUBTYPE_REQUEST 0
#define EAP_TLV_CRYPTO_BINDING_SUBTYPE_RESPONSE 1

#define EAP_TLV_ACTION_PROCESS_TLV 1
#define EAP_TLV_ACTION_NEGOTIATE_EAP 2

#endif /* EAP_TLV_COMMON_H */
