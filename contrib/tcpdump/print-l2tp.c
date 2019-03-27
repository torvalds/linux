/*
 * Copyright (c) 1991, 1993, 1994, 1995, 1996, 1997
 *      The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that: (1) source code distributions
 * retain the above copyright notice and this paragraph in its entirety, (2)
 * distributions including binary code include the above copyright notice and
 * this paragraph in its entirety in the documentation or other materials
 * provided with the distribution, and (3) all advertising materials mentioning
 * features or use of this software display the following acknowledgement:
 * ``This product includes software developed by the University of California,
 * Lawrence Berkeley Laboratory and its contributors.'' Neither the name of
 * the University nor the names of its contributors may be used to endorse
 * or promote products derived from this software without specific prior
 * written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 * L2TP support contributed by Motonori Shindo (mshindo@mshindo.net)
 */

/* \summary: Layer Two Tunneling Protocol (L2TP) printer */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <netdissect-stdinc.h>

#include "netdissect.h"
#include "extract.h"

#define L2TP_FLAG_TYPE		0x8000	/* Type (0=Data, 1=Control) */
#define L2TP_FLAG_LENGTH	0x4000	/* Length */
#define L2TP_FLAG_SEQUENCE	0x0800	/* Sequence */
#define L2TP_FLAG_OFFSET	0x0200	/* Offset */
#define L2TP_FLAG_PRIORITY	0x0100	/* Priority */

#define L2TP_VERSION_MASK	0x000f	/* Version Mask */
#define L2TP_VERSION_L2F	0x0001	/* L2F */
#define L2TP_VERSION_L2TP	0x0002	/* L2TP */

#define L2TP_AVP_HDR_FLAG_MANDATORY	0x8000	/* Mandatory Flag */
#define L2TP_AVP_HDR_FLAG_HIDDEN	0x4000	/* Hidden Flag */
#define L2TP_AVP_HDR_LEN_MASK		0x03ff	/* Length Mask */

#define L2TP_FRAMING_CAP_SYNC_MASK	0x00000001	/* Synchronous */
#define L2TP_FRAMING_CAP_ASYNC_MASK	0x00000002	/* Asynchronous */

#define L2TP_FRAMING_TYPE_SYNC_MASK	0x00000001	/* Synchronous */
#define L2TP_FRAMING_TYPE_ASYNC_MASK	0x00000002	/* Asynchronous */

#define L2TP_BEARER_CAP_DIGITAL_MASK	0x00000001	/* Digital */
#define L2TP_BEARER_CAP_ANALOG_MASK	0x00000002	/* Analog */

#define L2TP_BEARER_TYPE_DIGITAL_MASK	0x00000001	/* Digital */
#define L2TP_BEARER_TYPE_ANALOG_MASK	0x00000002	/* Analog */

/* Authen Type */
#define L2TP_AUTHEN_TYPE_RESERVED	0x0000	/* Reserved */
#define L2TP_AUTHEN_TYPE_TEXTUAL	0x0001	/* Textual username/password exchange */
#define L2TP_AUTHEN_TYPE_CHAP		0x0002	/* PPP CHAP */
#define L2TP_AUTHEN_TYPE_PAP		0x0003	/* PPP PAP */
#define L2TP_AUTHEN_TYPE_NO_AUTH	0x0004	/* No Authentication */
#define L2TP_AUTHEN_TYPE_MSCHAPv1	0x0005	/* MSCHAPv1 */

#define L2TP_PROXY_AUTH_ID_MASK		0x00ff

static const char tstr[] = " [|l2tp]";

#define	L2TP_MSGTYPE_SCCRQ	1  /* Start-Control-Connection-Request */
#define	L2TP_MSGTYPE_SCCRP	2  /* Start-Control-Connection-Reply */
#define	L2TP_MSGTYPE_SCCCN	3  /* Start-Control-Connection-Connected */
#define	L2TP_MSGTYPE_STOPCCN	4  /* Stop-Control-Connection-Notification */
#define	L2TP_MSGTYPE_HELLO	6  /* Hello */
#define	L2TP_MSGTYPE_OCRQ	7  /* Outgoing-Call-Request */
#define	L2TP_MSGTYPE_OCRP	8  /* Outgoing-Call-Reply */
#define	L2TP_MSGTYPE_OCCN	9  /* Outgoing-Call-Connected */
#define	L2TP_MSGTYPE_ICRQ	10 /* Incoming-Call-Request */
#define	L2TP_MSGTYPE_ICRP	11 /* Incoming-Call-Reply */
#define	L2TP_MSGTYPE_ICCN	12 /* Incoming-Call-Connected */
#define	L2TP_MSGTYPE_CDN	14 /* Call-Disconnect-Notify */
#define	L2TP_MSGTYPE_WEN	15 /* WAN-Error-Notify */
#define	L2TP_MSGTYPE_SLI	16 /* Set-Link-Info */

static const struct tok l2tp_msgtype2str[] = {
	{ L2TP_MSGTYPE_SCCRQ, 	"SCCRQ" },
	{ L2TP_MSGTYPE_SCCRP,	"SCCRP" },
	{ L2TP_MSGTYPE_SCCCN,	"SCCCN" },
	{ L2TP_MSGTYPE_STOPCCN,	"StopCCN" },
	{ L2TP_MSGTYPE_HELLO,	"HELLO" },
	{ L2TP_MSGTYPE_OCRQ,	"OCRQ" },
	{ L2TP_MSGTYPE_OCRP,	"OCRP" },
	{ L2TP_MSGTYPE_OCCN,	"OCCN" },
	{ L2TP_MSGTYPE_ICRQ,	"ICRQ" },
	{ L2TP_MSGTYPE_ICRP,	"ICRP" },
	{ L2TP_MSGTYPE_ICCN,	"ICCN" },
	{ L2TP_MSGTYPE_CDN,	"CDN" },
	{ L2TP_MSGTYPE_WEN,	"WEN" },
	{ L2TP_MSGTYPE_SLI,	"SLI" },
	{ 0,			NULL }
};

#define L2TP_AVP_MSGTYPE		0  /* Message Type */
#define L2TP_AVP_RESULT_CODE		1  /* Result Code */
#define L2TP_AVP_PROTO_VER		2  /* Protocol Version */
#define L2TP_AVP_FRAMING_CAP		3  /* Framing Capabilities */
#define L2TP_AVP_BEARER_CAP		4  /* Bearer Capabilities */
#define L2TP_AVP_TIE_BREAKER		5  /* Tie Breaker */
#define L2TP_AVP_FIRM_VER		6  /* Firmware Revision */
#define L2TP_AVP_HOST_NAME		7  /* Host Name */
#define L2TP_AVP_VENDOR_NAME		8  /* Vendor Name */
#define L2TP_AVP_ASSND_TUN_ID 		9  /* Assigned Tunnel ID */
#define L2TP_AVP_RECV_WIN_SIZE		10 /* Receive Window Size */
#define L2TP_AVP_CHALLENGE		11 /* Challenge */
#define L2TP_AVP_Q931_CC		12 /* Q.931 Cause Code */
#define L2TP_AVP_CHALLENGE_RESP		13 /* Challenge Response */
#define L2TP_AVP_ASSND_SESS_ID  	14 /* Assigned Session ID */
#define L2TP_AVP_CALL_SER_NUM 		15 /* Call Serial Number */
#define L2TP_AVP_MINIMUM_BPS		16 /* Minimum BPS */
#define L2TP_AVP_MAXIMUM_BPS		17 /* Maximum BPS */
#define L2TP_AVP_BEARER_TYPE		18 /* Bearer Type */
#define L2TP_AVP_FRAMING_TYPE 		19 /* Framing Type */
#define L2TP_AVP_PACKET_PROC_DELAY	20 /* Packet Processing Delay (OBSOLETE) */
#define L2TP_AVP_CALLED_NUMBER		21 /* Called Number */
#define L2TP_AVP_CALLING_NUMBER		22 /* Calling Number */
#define L2TP_AVP_SUB_ADDRESS		23 /* Sub-Address */
#define L2TP_AVP_TX_CONN_SPEED		24 /* (Tx) Connect Speed */
#define L2TP_AVP_PHY_CHANNEL_ID		25 /* Physical Channel ID */
#define L2TP_AVP_INI_RECV_LCP		26 /* Initial Received LCP CONFREQ */
#define L2TP_AVP_LAST_SENT_LCP		27 /* Last Sent LCP CONFREQ */
#define L2TP_AVP_LAST_RECV_LCP		28 /* Last Received LCP CONFREQ */
#define L2TP_AVP_PROXY_AUTH_TYPE	29 /* Proxy Authen Type */
#define L2TP_AVP_PROXY_AUTH_NAME	30 /* Proxy Authen Name */
#define L2TP_AVP_PROXY_AUTH_CHAL	31 /* Proxy Authen Challenge */
#define L2TP_AVP_PROXY_AUTH_ID		32 /* Proxy Authen ID */
#define L2TP_AVP_PROXY_AUTH_RESP	33 /* Proxy Authen Response */
#define L2TP_AVP_CALL_ERRORS		34 /* Call Errors */
#define L2TP_AVP_ACCM			35 /* ACCM */
#define L2TP_AVP_RANDOM_VECTOR		36 /* Random Vector */
#define L2TP_AVP_PRIVATE_GRP_ID		37 /* Private Group ID */
#define L2TP_AVP_RX_CONN_SPEED		38 /* (Rx) Connect Speed */
#define L2TP_AVP_SEQ_REQUIRED 		39 /* Sequencing Required */
#define L2TP_AVP_PPP_DISCON_CC		46 /* PPP Disconnect Cause Code */

static const struct tok l2tp_avp2str[] = {
	{ L2TP_AVP_MSGTYPE,		"MSGTYPE" },
	{ L2TP_AVP_RESULT_CODE,		"RESULT_CODE" },
	{ L2TP_AVP_PROTO_VER,		"PROTO_VER" },
	{ L2TP_AVP_FRAMING_CAP,		"FRAMING_CAP" },
	{ L2TP_AVP_BEARER_CAP,		"BEARER_CAP" },
	{ L2TP_AVP_TIE_BREAKER,		"TIE_BREAKER" },
	{ L2TP_AVP_FIRM_VER,		"FIRM_VER" },
	{ L2TP_AVP_HOST_NAME,		"HOST_NAME" },
	{ L2TP_AVP_VENDOR_NAME,		"VENDOR_NAME" },
	{ L2TP_AVP_ASSND_TUN_ID,	"ASSND_TUN_ID" },
	{ L2TP_AVP_RECV_WIN_SIZE,	"RECV_WIN_SIZE" },
	{ L2TP_AVP_CHALLENGE,		"CHALLENGE" },
	{ L2TP_AVP_Q931_CC,		"Q931_CC", },
	{ L2TP_AVP_CHALLENGE_RESP,	"CHALLENGE_RESP" },
	{ L2TP_AVP_ASSND_SESS_ID,	"ASSND_SESS_ID" },
	{ L2TP_AVP_CALL_SER_NUM,	"CALL_SER_NUM" },
	{ L2TP_AVP_MINIMUM_BPS,		"MINIMUM_BPS" },
	{ L2TP_AVP_MAXIMUM_BPS,		"MAXIMUM_BPS" },
	{ L2TP_AVP_BEARER_TYPE,		"BEARER_TYPE" },
	{ L2TP_AVP_FRAMING_TYPE,	"FRAMING_TYPE" },
	{ L2TP_AVP_PACKET_PROC_DELAY,	"PACKET_PROC_DELAY" },
	{ L2TP_AVP_CALLED_NUMBER,	"CALLED_NUMBER" },
	{ L2TP_AVP_CALLING_NUMBER,	"CALLING_NUMBER" },
	{ L2TP_AVP_SUB_ADDRESS,		"SUB_ADDRESS" },
	{ L2TP_AVP_TX_CONN_SPEED,	"TX_CONN_SPEED" },
	{ L2TP_AVP_PHY_CHANNEL_ID,	"PHY_CHANNEL_ID" },
	{ L2TP_AVP_INI_RECV_LCP,	"INI_RECV_LCP" },
	{ L2TP_AVP_LAST_SENT_LCP,	"LAST_SENT_LCP" },
	{ L2TP_AVP_LAST_RECV_LCP,	"LAST_RECV_LCP" },
	{ L2TP_AVP_PROXY_AUTH_TYPE,	"PROXY_AUTH_TYPE" },
	{ L2TP_AVP_PROXY_AUTH_NAME,	"PROXY_AUTH_NAME" },
	{ L2TP_AVP_PROXY_AUTH_CHAL,	"PROXY_AUTH_CHAL" },
	{ L2TP_AVP_PROXY_AUTH_ID,	"PROXY_AUTH_ID" },
	{ L2TP_AVP_PROXY_AUTH_RESP,	"PROXY_AUTH_RESP" },
	{ L2TP_AVP_CALL_ERRORS,		"CALL_ERRORS" },
	{ L2TP_AVP_ACCM,		"ACCM" },
	{ L2TP_AVP_RANDOM_VECTOR,	"RANDOM_VECTOR" },
	{ L2TP_AVP_PRIVATE_GRP_ID,	"PRIVATE_GRP_ID" },
	{ L2TP_AVP_RX_CONN_SPEED,	"RX_CONN_SPEED" },
	{ L2TP_AVP_SEQ_REQUIRED,	"SEQ_REQUIRED" },
	{ L2TP_AVP_PPP_DISCON_CC,	"PPP_DISCON_CC" },
	{ 0,				NULL }
};

static const struct tok l2tp_authentype2str[] = {
	{ L2TP_AUTHEN_TYPE_RESERVED,	"Reserved" },
	{ L2TP_AUTHEN_TYPE_TEXTUAL,	"Textual" },
	{ L2TP_AUTHEN_TYPE_CHAP,	"CHAP" },
	{ L2TP_AUTHEN_TYPE_PAP,		"PAP" },
	{ L2TP_AUTHEN_TYPE_NO_AUTH,	"No Auth" },
	{ L2TP_AUTHEN_TYPE_MSCHAPv1,	"MS-CHAPv1" },
	{ 0,				NULL }
};

#define L2TP_PPP_DISCON_CC_DIRECTION_GLOBAL	0
#define L2TP_PPP_DISCON_CC_DIRECTION_AT_PEER	1
#define L2TP_PPP_DISCON_CC_DIRECTION_AT_LOCAL	2

static const struct tok l2tp_cc_direction2str[] = {
	{ L2TP_PPP_DISCON_CC_DIRECTION_GLOBAL,	"global error" },
	{ L2TP_PPP_DISCON_CC_DIRECTION_AT_PEER,	"at peer" },
	{ L2TP_PPP_DISCON_CC_DIRECTION_AT_LOCAL,"at local" },
	{ 0,					NULL }
};

#if 0
static char *l2tp_result_code_StopCCN[] = {
         "Reserved",
         "General request to clear control connection",
         "General error--Error Code indicates the problem",
         "Control channel already exists",
         "Requester is not authorized to establish a control channel",
         "The protocol version of the requester is not supported",
         "Requester is being shut down",
         "Finite State Machine error"
#define L2TP_MAX_RESULT_CODE_STOPCC_INDEX	8
};
#endif

#if 0
static char *l2tp_result_code_CDN[] = {
	"Reserved",
	"Call disconnected due to loss of carrier",
	"Call disconnected for the reason indicated in error code",
	"Call disconnected for administrative reasons",
	"Call failed due to lack of appropriate facilities being " \
	"available (temporary condition)",
	"Call failed due to lack of appropriate facilities being " \
	"available (permanent condition)",
	"Invalid destination",
	"Call failed due to no carrier detected",
	"Call failed due to detection of a busy signal",
	"Call failed due to lack of a dial tone",
	"Call was not established within time allotted by LAC",
	"Call was connected but no appropriate framing was detected"
#define L2TP_MAX_RESULT_CODE_CDN_INDEX	12
};
#endif

#if 0
static char *l2tp_error_code_general[] = {
	"No general error",
	"No control connection exists yet for this LAC-LNS pair",
	"Length is wrong",
	"One of the field values was out of range or " \
	"reserved field was non-zero"
	"Insufficient resources to handle this operation now",
	"The Session ID is invalid in this context",
	"A generic vendor-specific error occurred in the LAC",
	"Try another"
#define L2TP_MAX_ERROR_CODE_GENERAL_INDEX	8
};
#endif

/******************************/
/* generic print out routines */
/******************************/
static void
print_string(netdissect_options *ndo, const u_char *dat, u_int length)
{
	u_int i;
	for (i=0; i<length; i++) {
		ND_PRINT((ndo, "%c", *dat++));
	}
}

static void
print_octets(netdissect_options *ndo, const u_char *dat, u_int length)
{
	u_int i;
	for (i=0; i<length; i++) {
		ND_PRINT((ndo, "%02x", *dat++));
	}
}

static void
print_16bits_val(netdissect_options *ndo, const uint16_t *dat)
{
	ND_PRINT((ndo, "%u", EXTRACT_16BITS(dat)));
}

static void
print_32bits_val(netdissect_options *ndo, const uint32_t *dat)
{
	ND_PRINT((ndo, "%lu", (u_long)EXTRACT_32BITS(dat)));
}

/***********************************/
/* AVP-specific print out routines */
/***********************************/
static void
l2tp_msgtype_print(netdissect_options *ndo, const u_char *dat, u_int length)
{
	const uint16_t *ptr = (const uint16_t *)dat;

	if (length < 2) {
		ND_PRINT((ndo, "AVP too short"));
		return;
	}
	ND_PRINT((ndo, "%s", tok2str(l2tp_msgtype2str, "MSGTYPE-#%u",
	    EXTRACT_16BITS(ptr))));
}

static void
l2tp_result_code_print(netdissect_options *ndo, const u_char *dat, u_int length)
{
	const uint16_t *ptr = (const uint16_t *)dat;

	/* Result Code */
	if (length < 2) {
		ND_PRINT((ndo, "AVP too short"));
		return;
	}
	ND_PRINT((ndo, "%u", EXTRACT_16BITS(ptr)));
	ptr++;
	length -= 2;

	/* Error Code (opt) */
	if (length == 0)
		return;
	if (length < 2) {
		ND_PRINT((ndo, " AVP too short"));
		return;
	}
	ND_PRINT((ndo, "/%u", EXTRACT_16BITS(ptr)));
	ptr++;
	length -= 2;

	/* Error Message (opt) */
	if (length == 0)
		return;
	ND_PRINT((ndo, " "));
	print_string(ndo, (const u_char *)ptr, length);
}

static void
l2tp_proto_ver_print(netdissect_options *ndo, const uint16_t *dat, u_int length)
{
	if (length < 2) {
		ND_PRINT((ndo, "AVP too short"));
		return;
	}
	ND_PRINT((ndo, "%u.%u", (EXTRACT_16BITS(dat) >> 8),
	    (EXTRACT_16BITS(dat) & 0xff)));
}

static void
l2tp_framing_cap_print(netdissect_options *ndo, const u_char *dat, u_int length)
{
	const uint32_t *ptr = (const uint32_t *)dat;

	if (length < 4) {
		ND_PRINT((ndo, "AVP too short"));
		return;
	}
	if (EXTRACT_32BITS(ptr) &  L2TP_FRAMING_CAP_ASYNC_MASK) {
		ND_PRINT((ndo, "A"));
	}
	if (EXTRACT_32BITS(ptr) &  L2TP_FRAMING_CAP_SYNC_MASK) {
		ND_PRINT((ndo, "S"));
	}
}

static void
l2tp_bearer_cap_print(netdissect_options *ndo, const u_char *dat, u_int length)
{
	const uint32_t *ptr = (const uint32_t *)dat;

	if (length < 4) {
		ND_PRINT((ndo, "AVP too short"));
		return;
	}
	if (EXTRACT_32BITS(ptr) &  L2TP_BEARER_CAP_ANALOG_MASK) {
		ND_PRINT((ndo, "A"));
	}
	if (EXTRACT_32BITS(ptr) &  L2TP_BEARER_CAP_DIGITAL_MASK) {
		ND_PRINT((ndo, "D"));
	}
}

static void
l2tp_q931_cc_print(netdissect_options *ndo, const u_char *dat, u_int length)
{
	if (length < 3) {
		ND_PRINT((ndo, "AVP too short"));
		return;
	}
	print_16bits_val(ndo, (const uint16_t *)dat);
	ND_PRINT((ndo, ", %02x", dat[2]));
	dat += 3;
	length -= 3;
	if (length != 0) {
		ND_PRINT((ndo, " "));
		print_string(ndo, dat, length);
	}
}

static void
l2tp_bearer_type_print(netdissect_options *ndo, const u_char *dat, u_int length)
{
	const uint32_t *ptr = (const uint32_t *)dat;

	if (length < 4) {
		ND_PRINT((ndo, "AVP too short"));
		return;
	}
	if (EXTRACT_32BITS(ptr) &  L2TP_BEARER_TYPE_ANALOG_MASK) {
		ND_PRINT((ndo, "A"));
	}
	if (EXTRACT_32BITS(ptr) &  L2TP_BEARER_TYPE_DIGITAL_MASK) {
		ND_PRINT((ndo, "D"));
	}
}

static void
l2tp_framing_type_print(netdissect_options *ndo, const u_char *dat, u_int length)
{
	const uint32_t *ptr = (const uint32_t *)dat;

	if (length < 4) {
		ND_PRINT((ndo, "AVP too short"));
		return;
	}
	if (EXTRACT_32BITS(ptr) &  L2TP_FRAMING_TYPE_ASYNC_MASK) {
		ND_PRINT((ndo, "A"));
	}
	if (EXTRACT_32BITS(ptr) &  L2TP_FRAMING_TYPE_SYNC_MASK) {
		ND_PRINT((ndo, "S"));
	}
}

static void
l2tp_packet_proc_delay_print(netdissect_options *ndo)
{
	ND_PRINT((ndo, "obsolete"));
}

static void
l2tp_proxy_auth_type_print(netdissect_options *ndo, const u_char *dat, u_int length)
{
	const uint16_t *ptr = (const uint16_t *)dat;

	if (length < 2) {
		ND_PRINT((ndo, "AVP too short"));
		return;
	}
	ND_PRINT((ndo, "%s", tok2str(l2tp_authentype2str,
			     "AuthType-#%u", EXTRACT_16BITS(ptr))));
}

static void
l2tp_proxy_auth_id_print(netdissect_options *ndo, const u_char *dat, u_int length)
{
	const uint16_t *ptr = (const uint16_t *)dat;

	if (length < 2) {
		ND_PRINT((ndo, "AVP too short"));
		return;
	}
	ND_PRINT((ndo, "%u", EXTRACT_16BITS(ptr) & L2TP_PROXY_AUTH_ID_MASK));
}

static void
l2tp_call_errors_print(netdissect_options *ndo, const u_char *dat, u_int length)
{
	const uint16_t *ptr = (const uint16_t *)dat;
	uint16_t val_h, val_l;

	if (length < 2) {
		ND_PRINT((ndo, "AVP too short"));
		return;
	}
	ptr++;		/* skip "Reserved" */
	length -= 2;

	if (length < 4) {
		ND_PRINT((ndo, "AVP too short"));
		return;
	}
	val_h = EXTRACT_16BITS(ptr); ptr++; length -= 2;
	val_l = EXTRACT_16BITS(ptr); ptr++; length -= 2;
	ND_PRINT((ndo, "CRCErr=%u ", (val_h<<16) + val_l));

	if (length < 4) {
		ND_PRINT((ndo, "AVP too short"));
		return;
	}
	val_h = EXTRACT_16BITS(ptr); ptr++; length -= 2;
	val_l = EXTRACT_16BITS(ptr); ptr++; length -= 2;
	ND_PRINT((ndo, "FrameErr=%u ", (val_h<<16) + val_l));

	if (length < 4) {
		ND_PRINT((ndo, "AVP too short"));
		return;
	}
	val_h = EXTRACT_16BITS(ptr); ptr++; length -= 2;
	val_l = EXTRACT_16BITS(ptr); ptr++; length -= 2;
	ND_PRINT((ndo, "HardOver=%u ", (val_h<<16) + val_l));

	if (length < 4) {
		ND_PRINT((ndo, "AVP too short"));
		return;
	}
	val_h = EXTRACT_16BITS(ptr); ptr++; length -= 2;
	val_l = EXTRACT_16BITS(ptr); ptr++; length -= 2;
	ND_PRINT((ndo, "BufOver=%u ", (val_h<<16) + val_l));

	if (length < 4) {
		ND_PRINT((ndo, "AVP too short"));
		return;
	}
	val_h = EXTRACT_16BITS(ptr); ptr++; length -= 2;
	val_l = EXTRACT_16BITS(ptr); ptr++; length -= 2;
	ND_PRINT((ndo, "Timeout=%u ", (val_h<<16) + val_l));

	if (length < 4) {
		ND_PRINT((ndo, "AVP too short"));
		return;
	}
	val_h = EXTRACT_16BITS(ptr); ptr++;
	val_l = EXTRACT_16BITS(ptr); ptr++;
	ND_PRINT((ndo, "AlignErr=%u ", (val_h<<16) + val_l));
}

static void
l2tp_accm_print(netdissect_options *ndo, const u_char *dat, u_int length)
{
	const uint16_t *ptr = (const uint16_t *)dat;
	uint16_t val_h, val_l;

	if (length < 2) {
		ND_PRINT((ndo, "AVP too short"));
		return;
	}
	ptr++;		/* skip "Reserved" */
	length -= 2;

	if (length < 4) {
		ND_PRINT((ndo, "AVP too short"));
		return;
	}
	val_h = EXTRACT_16BITS(ptr); ptr++; length -= 2;
	val_l = EXTRACT_16BITS(ptr); ptr++; length -= 2;
	ND_PRINT((ndo, "send=%08x ", (val_h<<16) + val_l));

	if (length < 4) {
		ND_PRINT((ndo, "AVP too short"));
		return;
	}
	val_h = EXTRACT_16BITS(ptr); ptr++;
	val_l = EXTRACT_16BITS(ptr); ptr++;
	ND_PRINT((ndo, "recv=%08x ", (val_h<<16) + val_l));
}

static void
l2tp_ppp_discon_cc_print(netdissect_options *ndo, const u_char *dat, u_int length)
{
	const uint16_t *ptr = (const uint16_t *)dat;

	if (length < 5) {
		ND_PRINT((ndo, "AVP too short"));
		return;
	}
	/* Disconnect Code */
	ND_PRINT((ndo, "%04x, ", EXTRACT_16BITS(dat)));
	dat += 2;
	length -= 2;
	/* Control Protocol Number */
	ND_PRINT((ndo, "%04x ",  EXTRACT_16BITS(dat)));
	dat += 2;
	length -= 2;
	/* Direction */
	ND_PRINT((ndo, "%s", tok2str(l2tp_cc_direction2str,
			     "Direction-#%u", EXTRACT_8BITS(ptr))));
	ptr++;
	length--;

	if (length != 0) {
		ND_PRINT((ndo, " "));
		print_string(ndo, (const u_char *)ptr, length);
	}
}

static void
l2tp_avp_print(netdissect_options *ndo, const u_char *dat, int length)
{
	u_int len;
	const uint16_t *ptr = (const uint16_t *)dat;
	uint16_t attr_type;
	int hidden = FALSE;

	if (length <= 0) {
		return;
	}

	ND_PRINT((ndo, " "));

	ND_TCHECK(*ptr);	/* Flags & Length */
	len = EXTRACT_16BITS(ptr) & L2TP_AVP_HDR_LEN_MASK;

	/* If it is not long enough to contain the header, we'll give up. */
	if (len < 6)
		goto trunc;

	/* If it goes past the end of the remaining length of the packet,
	   we'll give up. */
	if (len > (u_int)length)
		goto trunc;

	/* If it goes past the end of the remaining length of the captured
	   data, we'll give up. */
	ND_TCHECK2(*ptr, len);

	/*
	 * After this point, we don't need to check whether we go past
	 * the length of the captured data; however, we *do* need to
	 * check whether we go past the end of the AVP.
	 */

	if (EXTRACT_16BITS(ptr) & L2TP_AVP_HDR_FLAG_MANDATORY) {
		ND_PRINT((ndo, "*"));
	}
	if (EXTRACT_16BITS(ptr) & L2TP_AVP_HDR_FLAG_HIDDEN) {
		hidden = TRUE;
		ND_PRINT((ndo, "?"));
	}
	ptr++;

	if (EXTRACT_16BITS(ptr)) {
		/* Vendor Specific Attribute */
	        ND_PRINT((ndo, "VENDOR%04x:", EXTRACT_16BITS(ptr))); ptr++;
		ND_PRINT((ndo, "ATTR%04x", EXTRACT_16BITS(ptr))); ptr++;
		ND_PRINT((ndo, "("));
		print_octets(ndo, (const u_char *)ptr, len-6);
		ND_PRINT((ndo, ")"));
	} else {
		/* IETF-defined Attributes */
		ptr++;
		attr_type = EXTRACT_16BITS(ptr); ptr++;
		ND_PRINT((ndo, "%s", tok2str(l2tp_avp2str, "AVP-#%u", attr_type)));
		ND_PRINT((ndo, "("));
		if (hidden) {
			ND_PRINT((ndo, "???"));
		} else {
			switch (attr_type) {
			case L2TP_AVP_MSGTYPE:
				l2tp_msgtype_print(ndo, (const u_char *)ptr, len-6);
				break;
			case L2TP_AVP_RESULT_CODE:
				l2tp_result_code_print(ndo, (const u_char *)ptr, len-6);
				break;
			case L2TP_AVP_PROTO_VER:
				l2tp_proto_ver_print(ndo, ptr, len-6);
				break;
			case L2TP_AVP_FRAMING_CAP:
				l2tp_framing_cap_print(ndo, (const u_char *)ptr, len-6);
				break;
			case L2TP_AVP_BEARER_CAP:
				l2tp_bearer_cap_print(ndo, (const u_char *)ptr, len-6);
				break;
			case L2TP_AVP_TIE_BREAKER:
				if (len-6 < 8) {
					ND_PRINT((ndo, "AVP too short"));
					break;
				}
				print_octets(ndo, (const u_char *)ptr, 8);
				break;
			case L2TP_AVP_FIRM_VER:
			case L2TP_AVP_ASSND_TUN_ID:
			case L2TP_AVP_RECV_WIN_SIZE:
			case L2TP_AVP_ASSND_SESS_ID:
				if (len-6 < 2) {
					ND_PRINT((ndo, "AVP too short"));
					break;
				}
				print_16bits_val(ndo, ptr);
				break;
			case L2TP_AVP_HOST_NAME:
			case L2TP_AVP_VENDOR_NAME:
			case L2TP_AVP_CALLING_NUMBER:
			case L2TP_AVP_CALLED_NUMBER:
			case L2TP_AVP_SUB_ADDRESS:
			case L2TP_AVP_PROXY_AUTH_NAME:
			case L2TP_AVP_PRIVATE_GRP_ID:
				print_string(ndo, (const u_char *)ptr, len-6);
				break;
			case L2TP_AVP_CHALLENGE:
			case L2TP_AVP_INI_RECV_LCP:
			case L2TP_AVP_LAST_SENT_LCP:
			case L2TP_AVP_LAST_RECV_LCP:
			case L2TP_AVP_PROXY_AUTH_CHAL:
			case L2TP_AVP_PROXY_AUTH_RESP:
			case L2TP_AVP_RANDOM_VECTOR:
				print_octets(ndo, (const u_char *)ptr, len-6);
				break;
			case L2TP_AVP_Q931_CC:
				l2tp_q931_cc_print(ndo, (const u_char *)ptr, len-6);
				break;
			case L2TP_AVP_CHALLENGE_RESP:
				if (len-6 < 16) {
					ND_PRINT((ndo, "AVP too short"));
					break;
				}
				print_octets(ndo, (const u_char *)ptr, 16);
				break;
			case L2TP_AVP_CALL_SER_NUM:
			case L2TP_AVP_MINIMUM_BPS:
			case L2TP_AVP_MAXIMUM_BPS:
			case L2TP_AVP_TX_CONN_SPEED:
			case L2TP_AVP_PHY_CHANNEL_ID:
			case L2TP_AVP_RX_CONN_SPEED:
				if (len-6 < 4) {
					ND_PRINT((ndo, "AVP too short"));
					break;
				}
				print_32bits_val(ndo, (const uint32_t *)ptr);
				break;
			case L2TP_AVP_BEARER_TYPE:
				l2tp_bearer_type_print(ndo, (const u_char *)ptr, len-6);
				break;
			case L2TP_AVP_FRAMING_TYPE:
				l2tp_framing_type_print(ndo, (const u_char *)ptr, len-6);
				break;
			case L2TP_AVP_PACKET_PROC_DELAY:
				l2tp_packet_proc_delay_print(ndo);
				break;
			case L2TP_AVP_PROXY_AUTH_TYPE:
				l2tp_proxy_auth_type_print(ndo, (const u_char *)ptr, len-6);
				break;
			case L2TP_AVP_PROXY_AUTH_ID:
				l2tp_proxy_auth_id_print(ndo, (const u_char *)ptr, len-6);
				break;
			case L2TP_AVP_CALL_ERRORS:
				l2tp_call_errors_print(ndo, (const u_char *)ptr, len-6);
				break;
			case L2TP_AVP_ACCM:
				l2tp_accm_print(ndo, (const u_char *)ptr, len-6);
				break;
			case L2TP_AVP_SEQ_REQUIRED:
				break;	/* No Attribute Value */
			case L2TP_AVP_PPP_DISCON_CC:
				l2tp_ppp_discon_cc_print(ndo, (const u_char *)ptr, len-6);
				break;
			default:
				break;
			}
		}
		ND_PRINT((ndo, ")"));
	}

	l2tp_avp_print(ndo, dat+len, length-len);
	return;

 trunc:
	ND_PRINT((ndo, "|..."));
}


void
l2tp_print(netdissect_options *ndo, const u_char *dat, u_int length)
{
	const u_char *ptr = dat;
	u_int cnt = 0;			/* total octets consumed */
	uint16_t pad;
	int flag_t, flag_l, flag_s, flag_o;
	uint16_t l2tp_len;

	flag_t = flag_l = flag_s = flag_o = FALSE;

	ND_TCHECK2(*ptr, 2);	/* Flags & Version */
	if ((EXTRACT_16BITS(ptr) & L2TP_VERSION_MASK) == L2TP_VERSION_L2TP) {
		ND_PRINT((ndo, " l2tp:"));
	} else if ((EXTRACT_16BITS(ptr) & L2TP_VERSION_MASK) == L2TP_VERSION_L2F) {
		ND_PRINT((ndo, " l2f:"));
		return;		/* nothing to do */
	} else {
		ND_PRINT((ndo, " Unknown Version, neither L2F(1) nor L2TP(2)"));
		return;		/* nothing we can do */
	}

	ND_PRINT((ndo, "["));
	if (EXTRACT_16BITS(ptr) & L2TP_FLAG_TYPE) {
		flag_t = TRUE;
		ND_PRINT((ndo, "T"));
	}
	if (EXTRACT_16BITS(ptr) & L2TP_FLAG_LENGTH) {
		flag_l = TRUE;
		ND_PRINT((ndo, "L"));
	}
	if (EXTRACT_16BITS(ptr) & L2TP_FLAG_SEQUENCE) {
		flag_s = TRUE;
		ND_PRINT((ndo, "S"));
	}
	if (EXTRACT_16BITS(ptr) & L2TP_FLAG_OFFSET) {
		flag_o = TRUE;
		ND_PRINT((ndo, "O"));
	}
	if (EXTRACT_16BITS(ptr) & L2TP_FLAG_PRIORITY)
		ND_PRINT((ndo, "P"));
	ND_PRINT((ndo, "]"));

	ptr += 2;
	cnt += 2;

	if (flag_l) {
		ND_TCHECK2(*ptr, 2);	/* Length */
		l2tp_len = EXTRACT_16BITS(ptr);
		ptr += 2;
		cnt += 2;
	} else {
		l2tp_len = 0;
	}

	ND_TCHECK2(*ptr, 2);		/* Tunnel ID */
	ND_PRINT((ndo, "(%u/", EXTRACT_16BITS(ptr)));
	ptr += 2;
	cnt += 2;
	ND_TCHECK2(*ptr, 2);		/* Session ID */
	ND_PRINT((ndo, "%u)",  EXTRACT_16BITS(ptr)));
	ptr += 2;
	cnt += 2;

	if (flag_s) {
		ND_TCHECK2(*ptr, 2);	/* Ns */
		ND_PRINT((ndo, "Ns=%u,", EXTRACT_16BITS(ptr)));
		ptr += 2;
		cnt += 2;
		ND_TCHECK2(*ptr, 2);	/* Nr */
		ND_PRINT((ndo, "Nr=%u",  EXTRACT_16BITS(ptr)));
		ptr += 2;
		cnt += 2;
	}

	if (flag_o) {
		ND_TCHECK2(*ptr, 2);	/* Offset Size */
		pad =  EXTRACT_16BITS(ptr);
		ptr += (2 + pad);
		cnt += (2 + pad);
	}

	if (flag_l) {
		if (length < l2tp_len) {
			ND_PRINT((ndo, " Length %u larger than packet", l2tp_len));
			return;
		}
		length = l2tp_len;
	}
	if (length < cnt) {
		ND_PRINT((ndo, " Length %u smaller than header length", length));
		return;
	}
	if (flag_t) {
		if (!flag_l) {
			ND_PRINT((ndo, " No length"));
			return;
		}
		if (length - cnt == 0) {
			ND_PRINT((ndo, " ZLB"));
		} else {
			l2tp_avp_print(ndo, ptr, length - cnt);
		}
	} else {
		ND_PRINT((ndo, " {"));
		ppp_print(ndo, ptr, length - cnt);
		ND_PRINT((ndo, "}"));
	}

	return;

 trunc:
	ND_PRINT((ndo, "%s", tstr));
}
