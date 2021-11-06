/*
 * Extensible Authentication Protocol (EAP) definitions
 *
 * See
 * RFC 2284: PPP Extensible Authentication Protocol (EAP)
 *
 * Copyright (C) 2020, Broadcom.
 *
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 *
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 *
 *
 * <<Broadcom-WL-IPTag/Dual:>>
 */

#ifndef _eap_h_
#define _eap_h_

/* This marks the start of a packed structure section. */
#include <packed_section_start.h>

/* EAP packet format */
typedef BWL_PRE_PACKED_STRUCT struct {
	unsigned char code;	/* EAP code */
	unsigned char id;	/* Current request ID */
	unsigned short length;	/* Length including header */
	unsigned char type;	/* EAP type (optional) */
	unsigned char data[1];	/* Type data (optional) */
} BWL_POST_PACKED_STRUCT eap_header_t;

#define EAP_HEADER_LEN			4u
#define EAP_HEADER_LEN_WITH_TYPE	5u
#define ERP_FLAGS_LEN			1u
#define ERP_SEQ_LEN			2u
#define ERP_KEYNAMENAI_HEADER_LEN	2u
#define ERP_CRYPTOSUITE_LEN		1u

/* EAP codes */
#define EAP_REQUEST		1u
#define EAP_RESPONSE		2u
#define EAP_SUCCESS		3u
#define EAP_FAILURE		4u
#define EAP_INITIATE		5u
#define EAP_FINISH		6u

/* EAP types */
#define EAP_IDENTITY		1
#define EAP_NOTIFICATION	2
#define EAP_NAK			3
#define EAP_MD5			4
#define EAP_OTP			5
#define EAP_GTC			6
#define EAP_TLS			13
#define EAP_EXPANDED		254
#define BCM_EAP_SES		10
#define BCM_EAP_EXP_LEN		12  /* EAP_LEN 5 + 3 bytes for SMI ID + 4 bytes for ven type */
#define BCM_SMI_ID		0x113d
#define WFA_VENDOR_SMI	0x009F68

/* ERP types */
#define EAP_ERP_TYPE_REAUTH_START	1u
#define EAP_ERP_TYPE_REAUTH		2u

/* EAP FLAGS */
#define ERP_R_FLAG	0x80 /* result flag, set = failure */
#define ERP_B_FLAG	0x40 /* bootstrap flag, set = bootstrap */
#define ERP_L_FLAG	0x20 /* rrk lifetime tlv is present */

/* ERP TV/TLV types */
#define EAP_ERP_TLV_KEYNAME_NAI		1u

/* ERP Cryptosuite */
#define EAP_ERP_CS_HMAC_SHA256_128	2u

#ifdef  BCMCCX
#define EAP_LEAP		17

#define LEAP_VERSION		1
#define LEAP_CHALLENGE_LEN	8
#define LEAP_RESPONSE_LEN	24

/* LEAP challenge */
typedef struct {
	unsigned char version;		/* should be value of LEAP_VERSION */
	unsigned char reserved;		/* not used */
	unsigned char chall_len;	/* always value of LEAP_CHALLENGE_LEN */
	unsigned char challenge[LEAP_CHALLENGE_LEN]; /* random */
	unsigned char username[1];
} leap_challenge_t;

#define LEAP_CHALLENGE_HDR_LEN	12

/* LEAP challenge reponse */
typedef struct {
	unsigned char version;	/* should be value of LEAP_VERSION */
	unsigned char reserved;	/* not used */
	unsigned char resp_len;	/* always value of LEAP_RESPONSE_LEN */
	/* MS-CHAP hash of challenge and user's password */
	unsigned char response[LEAP_RESPONSE_LEN];
	unsigned char username[1];
} leap_response_t;

#define LEAP_RESPONSE_HDR_LEN	28

#endif /* BCMCCX */

/* This marks the end of a packed structure section. */
#include <packed_section_end.h>

#endif /* _eap_h_ */
