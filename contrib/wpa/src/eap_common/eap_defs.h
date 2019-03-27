/*
 * EAP server/peer: Shared EAP definitions
 * Copyright (c) 2004-2014, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef EAP_DEFS_H
#define EAP_DEFS_H

/* RFC 3748 - Extensible Authentication Protocol (EAP) */

#ifdef _MSC_VER
#pragma pack(push, 1)
#endif /* _MSC_VER */

struct eap_hdr {
	u8 code;
	u8 identifier;
	be16 length; /* including code and identifier; network byte order */
	/* followed by length-4 octets of data */
} STRUCT_PACKED;

#ifdef _MSC_VER
#pragma pack(pop)
#endif /* _MSC_VER */

enum { EAP_CODE_REQUEST = 1, EAP_CODE_RESPONSE = 2, EAP_CODE_SUCCESS = 3,
       EAP_CODE_FAILURE = 4, EAP_CODE_INITIATE = 5, EAP_CODE_FINISH = 6 };

/* EAP Request and Response data begins with one octet Type. Success and
 * Failure do not have additional data. */

/* Type field in EAP-Initiate and EAP-Finish messages */
enum eap_erp_type {
	EAP_ERP_TYPE_REAUTH_START = 1,
	EAP_ERP_TYPE_REAUTH = 2,
};

/* ERP TV/TLV types */
enum eap_erp_tlv_type {
	EAP_ERP_TLV_KEYNAME_NAI = 1,
	EAP_ERP_TV_RRK_LIFETIME = 2,
	EAP_ERP_TV_RMSK_LIFETIME = 3,
	EAP_ERP_TLV_DOMAIN_NAME = 4,
	EAP_ERP_TLV_CRYPTOSUITES = 5,
	EAP_ERP_TLV_AUTHORIZATION_INDICATION = 6,
	EAP_ERP_TLV_CALLED_STATION_ID = 128,
	EAP_ERP_TLV_CALLING_STATION_ID = 129,
	EAP_ERP_TLV_NAS_IDENTIFIER = 130,
	EAP_ERP_TLV_NAS_IP_ADDRESS = 131,
	EAP_ERP_TLV_NAS_IPV6_ADDRESS = 132,
};

/* ERP Cryptosuite */
enum eap_erp_cryptosuite {
	EAP_ERP_CS_HMAC_SHA256_64 = 1,
	EAP_ERP_CS_HMAC_SHA256_128 = 2,
	EAP_ERP_CS_HMAC_SHA256_256 = 3,
};

/*
 * EAP Method Types as allocated by IANA:
 * http://www.iana.org/assignments/eap-numbers
 */
typedef enum {
	EAP_TYPE_NONE = 0,
	EAP_TYPE_IDENTITY = 1 /* RFC 3748 */,
	EAP_TYPE_NOTIFICATION = 2 /* RFC 3748 */,
	EAP_TYPE_NAK = 3 /* Response only, RFC 3748 */,
	EAP_TYPE_MD5 = 4, /* RFC 3748 */
	EAP_TYPE_OTP = 5 /* RFC 3748 */,
	EAP_TYPE_GTC = 6, /* RFC 3748 */
	EAP_TYPE_TLS = 13 /* RFC 2716 */,
	EAP_TYPE_LEAP = 17 /* Cisco proprietary */,
	EAP_TYPE_SIM = 18 /* RFC 4186 */,
	EAP_TYPE_TTLS = 21 /* RFC 5281 */,
	EAP_TYPE_AKA = 23 /* RFC 4187 */,
	EAP_TYPE_PEAP = 25 /* draft-josefsson-pppext-eap-tls-eap-06.txt */,
	EAP_TYPE_MSCHAPV2 = 26 /* draft-kamath-pppext-eap-mschapv2-00.txt */,
	EAP_TYPE_TLV = 33 /* draft-josefsson-pppext-eap-tls-eap-07.txt */,
	EAP_TYPE_TNC = 38 /* TNC IF-T v1.0-r3; note: tentative assignment;
			   * type 38 has previously been allocated for
			   * EAP-HTTP Digest, (funk.com) */,
	EAP_TYPE_FAST = 43 /* RFC 4851 */,
	EAP_TYPE_PAX = 46 /* RFC 4746 */,
	EAP_TYPE_PSK = 47 /* RFC 4764 */,
	EAP_TYPE_SAKE = 48 /* RFC 4763 */,
	EAP_TYPE_IKEV2 = 49 /* RFC 5106 */,
	EAP_TYPE_AKA_PRIME = 50 /* RFC 5448 */,
	EAP_TYPE_GPSK = 51 /* RFC 5433 */,
	EAP_TYPE_PWD = 52 /* RFC 5931 */,
	EAP_TYPE_EKE = 53 /* RFC 6124 */,
	EAP_TYPE_EXPANDED = 254 /* RFC 3748 */
} EapType;


/* SMI Network Management Private Enterprise Code for vendor specific types */
enum {
	EAP_VENDOR_IETF = 0,
	EAP_VENDOR_MICROSOFT = 0x000137 /* Microsoft */,
	EAP_VENDOR_WFA = 0x00372A /* Wi-Fi Alliance (moved to WBA) */,
	EAP_VENDOR_HOSTAP = 39068 /* hostapd/wpa_supplicant project */,
	EAP_VENDOR_WFA_NEW = 40808 /* Wi-Fi Alliance */
};

#define EAP_VENDOR_UNAUTH_TLS EAP_VENDOR_HOSTAP
#define EAP_VENDOR_TYPE_UNAUTH_TLS 1

#define EAP_VENDOR_WFA_UNAUTH_TLS 13

#define EAP_MSK_LEN 64
#define EAP_EMSK_LEN 64
#define EAP_EMSK_NAME_LEN 8
#define ERP_MAX_KEY_LEN 64

#endif /* EAP_DEFS_H */
