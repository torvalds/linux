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
 * PPTP support contributed by Motonori Shindo (mshindo@mshindo.net)
 */

/* \summary: Point-to-Point Tunnelling Protocol (PPTP) printer */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <netdissect-stdinc.h>

#include "netdissect.h"
#include "extract.h"

static const char tstr[] = " [|pptp]";

#define PPTP_MSG_TYPE_CTRL	1	/* Control Message */
#define PPTP_MSG_TYPE_MGMT	2	/* Management Message (currently not used */
#define PPTP_MAGIC_COOKIE	0x1a2b3c4d	/* for sanity check */

#define PPTP_CTRL_MSG_TYPE_SCCRQ	1
#define PPTP_CTRL_MSG_TYPE_SCCRP	2
#define PPTP_CTRL_MSG_TYPE_StopCCRQ	3
#define PPTP_CTRL_MSG_TYPE_StopCCRP	4
#define PPTP_CTRL_MSG_TYPE_ECHORQ	5
#define PPTP_CTRL_MSG_TYPE_ECHORP	6
#define PPTP_CTRL_MSG_TYPE_OCRQ		7
#define PPTP_CTRL_MSG_TYPE_OCRP		8
#define PPTP_CTRL_MSG_TYPE_ICRQ		9
#define PPTP_CTRL_MSG_TYPE_ICRP		10
#define PPTP_CTRL_MSG_TYPE_ICCN		11
#define PPTP_CTRL_MSG_TYPE_CCRQ		12
#define PPTP_CTRL_MSG_TYPE_CDN		13
#define PPTP_CTRL_MSG_TYPE_WEN		14
#define PPTP_CTRL_MSG_TYPE_SLI		15

#define PPTP_FRAMING_CAP_ASYNC_MASK	0x00000001      /* Aynchronous */
#define PPTP_FRAMING_CAP_SYNC_MASK	0x00000002      /* Synchronous */

#define PPTP_BEARER_CAP_ANALOG_MASK	0x00000001      /* Analog */
#define PPTP_BEARER_CAP_DIGITAL_MASK	0x00000002      /* Digital */

static const char *pptp_message_type_string[] = {
	"NOT_DEFINED",		/* 0  Not defined in the RFC2637 */
	"SCCRQ",		/* 1  Start-Control-Connection-Request */
	"SCCRP",		/* 2  Start-Control-Connection-Reply */
	"StopCCRQ",		/* 3  Stop-Control-Connection-Request */
	"StopCCRP",		/* 4  Stop-Control-Connection-Reply */
	"ECHORQ",		/* 5  Echo Request */
	"ECHORP",		/* 6  Echo Reply */

	"OCRQ",			/* 7  Outgoing-Call-Request */
	"OCRP",			/* 8  Outgoing-Call-Reply */
	"ICRQ",			/* 9  Incoming-Call-Request */
	"ICRP",			/* 10 Incoming-Call-Reply */
	"ICCN",			/* 11 Incoming-Call-Connected */
	"CCRQ",			/* 12 Call-Clear-Request */
	"CDN",			/* 13 Call-Disconnect-Notify */

	"WEN",			/* 14 WAN-Error-Notify */

	"SLI"			/* 15 Set-Link-Info */
#define PPTP_MAX_MSGTYPE_INDEX	16
};

/* common for all PPTP control messages */
struct pptp_hdr {
	uint16_t length;
	uint16_t msg_type;
	uint32_t magic_cookie;
	uint16_t ctrl_msg_type;
	uint16_t reserved0;
};

struct pptp_msg_sccrq {
	uint16_t proto_ver;
	uint16_t reserved1;
	uint32_t framing_cap;
	uint32_t bearer_cap;
	uint16_t max_channel;
	uint16_t firm_rev;
	u_char hostname[64];
	u_char vendor[64];
};

struct pptp_msg_sccrp {
	uint16_t proto_ver;
	uint8_t result_code;
	uint8_t err_code;
	uint32_t framing_cap;
	uint32_t bearer_cap;
	uint16_t max_channel;
	uint16_t firm_rev;
	u_char hostname[64];
	u_char vendor[64];
};

struct pptp_msg_stopccrq {
	uint8_t reason;
	uint8_t reserved1;
	uint16_t reserved2;
};

struct pptp_msg_stopccrp {
	uint8_t result_code;
	uint8_t err_code;
	uint16_t reserved1;
};

struct pptp_msg_echorq {
	uint32_t id;
};

struct pptp_msg_echorp {
	uint32_t id;
	uint8_t result_code;
	uint8_t err_code;
	uint16_t reserved1;
};

struct pptp_msg_ocrq {
	uint16_t call_id;
	uint16_t call_ser;
	uint32_t min_bps;
	uint32_t max_bps;
	uint32_t bearer_type;
	uint32_t framing_type;
	uint16_t recv_winsiz;
	uint16_t pkt_proc_delay;
	uint16_t phone_no_len;
	uint16_t reserved1;
	u_char phone_no[64];
	u_char subaddr[64];
};

struct pptp_msg_ocrp {
	uint16_t call_id;
	uint16_t peer_call_id;
	uint8_t result_code;
	uint8_t err_code;
	uint16_t cause_code;
	uint32_t conn_speed;
	uint16_t recv_winsiz;
	uint16_t pkt_proc_delay;
	uint32_t phy_chan_id;
};

struct pptp_msg_icrq {
	uint16_t call_id;
	uint16_t call_ser;
	uint32_t bearer_type;
	uint32_t phy_chan_id;
	uint16_t dialed_no_len;
	uint16_t dialing_no_len;
	u_char dialed_no[64];		/* DNIS */
	u_char dialing_no[64];		/* CLID */
	u_char subaddr[64];
};

struct pptp_msg_icrp {
	uint16_t call_id;
	uint16_t peer_call_id;
	uint8_t result_code;
	uint8_t err_code;
	uint16_t recv_winsiz;
	uint16_t pkt_proc_delay;
	uint16_t reserved1;
};

struct pptp_msg_iccn {
	uint16_t peer_call_id;
	uint16_t reserved1;
	uint32_t conn_speed;
	uint16_t recv_winsiz;
	uint16_t pkt_proc_delay;
	uint32_t framing_type;
};

struct pptp_msg_ccrq {
	uint16_t call_id;
	uint16_t reserved1;
};

struct pptp_msg_cdn {
	uint16_t call_id;
	uint8_t result_code;
	uint8_t err_code;
	uint16_t cause_code;
	uint16_t reserved1;
	u_char call_stats[128];
};

struct pptp_msg_wen {
	uint16_t peer_call_id;
	uint16_t reserved1;
	uint32_t crc_err;
	uint32_t framing_err;
	uint32_t hardware_overrun;
	uint32_t buffer_overrun;
	uint32_t timeout_err;
	uint32_t align_err;
};

struct pptp_msg_sli {
	uint16_t peer_call_id;
	uint16_t reserved1;
	uint32_t send_accm;
	uint32_t recv_accm;
};

/* attributes that appear more than once in above messages:

   Number of
   occurence    attributes
  --------------------------------------
      2         uint32_t bearer_cap;
      2         uint32_t bearer_type;
      6         uint16_t call_id;
      2         uint16_t call_ser;
      2         uint16_t cause_code;
      2         uint32_t conn_speed;
      6         uint8_t err_code;
      2         uint16_t firm_rev;
      2         uint32_t framing_cap;
      2         uint32_t framing_type;
      2         u_char hostname[64];
      2         uint32_t id;
      2         uint16_t max_channel;
      5         uint16_t peer_call_id;
      2         uint32_t phy_chan_id;
      4         uint16_t pkt_proc_delay;
      2         uint16_t proto_ver;
      4         uint16_t recv_winsiz;
      2         uint8_t reserved1;
      9         uint16_t reserved1;
      6         uint8_t result_code;
      2         u_char subaddr[64];
      2         u_char vendor[64];

  so I will prepare print out functions for these attributes (except for
  reserved*).
*/

/******************************************/
/* Attribute-specific print out functions */
/******************************************/

/* In these attribute-specific print-out functions, it't not necessary
   to do ND_TCHECK because they are already checked in the caller of
   these functions. */

static void
pptp_bearer_cap_print(netdissect_options *ndo,
                      const uint32_t *bearer_cap)
{
	ND_PRINT((ndo, " BEARER_CAP(%s%s)",
	          EXTRACT_32BITS(bearer_cap) & PPTP_BEARER_CAP_DIGITAL_MASK ? "D" : "",
	          EXTRACT_32BITS(bearer_cap) & PPTP_BEARER_CAP_ANALOG_MASK ? "A" : ""));
}

static const struct tok pptp_btype_str[] = {
	{ 1, "A"   }, /* Analog */
	{ 2, "D"   }, /* Digital */
	{ 3, "Any" },
	{ 0, NULL }
};

static void
pptp_bearer_type_print(netdissect_options *ndo,
                       const uint32_t *bearer_type)
{
	ND_PRINT((ndo, " BEARER_TYPE(%s)",
	          tok2str(pptp_btype_str, "?", EXTRACT_32BITS(bearer_type))));
}

static void
pptp_call_id_print(netdissect_options *ndo,
                   const uint16_t *call_id)
{
	ND_PRINT((ndo, " CALL_ID(%u)", EXTRACT_16BITS(call_id)));
}

static void
pptp_call_ser_print(netdissect_options *ndo,
                    const uint16_t *call_ser)
{
	ND_PRINT((ndo, " CALL_SER_NUM(%u)", EXTRACT_16BITS(call_ser)));
}

static void
pptp_cause_code_print(netdissect_options *ndo,
                      const uint16_t *cause_code)
{
	ND_PRINT((ndo, " CAUSE_CODE(%u)", EXTRACT_16BITS(cause_code)));
}

static void
pptp_conn_speed_print(netdissect_options *ndo,
                      const uint32_t *conn_speed)
{
	ND_PRINT((ndo, " CONN_SPEED(%u)", EXTRACT_32BITS(conn_speed)));
}

static const struct tok pptp_errcode_str[] = {
	{ 0, "None"          },
	{ 1, "Not-Connected" },
	{ 2, "Bad-Format"    },
	{ 3, "Bad-Value"     },
	{ 4, "No-Resource"   },
	{ 5, "Bad-Call-ID"   },
	{ 6, "PAC-Error"     },
	{ 0, NULL }
};

static void
pptp_err_code_print(netdissect_options *ndo,
                    const uint8_t *err_code)
{
	ND_PRINT((ndo, " ERR_CODE(%u", *err_code));
	if (ndo->ndo_vflag) {
		ND_PRINT((ndo, ":%s", tok2str(pptp_errcode_str, "?", *err_code)));
	}
	ND_PRINT((ndo, ")"));
}

static void
pptp_firm_rev_print(netdissect_options *ndo,
                    const uint16_t *firm_rev)
{
	ND_PRINT((ndo, " FIRM_REV(%u)", EXTRACT_16BITS(firm_rev)));
}

static void
pptp_framing_cap_print(netdissect_options *ndo,
                       const uint32_t *framing_cap)
{
	ND_PRINT((ndo, " FRAME_CAP("));
	if (EXTRACT_32BITS(framing_cap) & PPTP_FRAMING_CAP_ASYNC_MASK) {
                ND_PRINT((ndo, "A"));		/* Async */
        }
        if (EXTRACT_32BITS(framing_cap) & PPTP_FRAMING_CAP_SYNC_MASK) {
                ND_PRINT((ndo, "S"));		/* Sync */
        }
	ND_PRINT((ndo, ")"));
}

static const struct tok pptp_ftype_str[] = {
	{ 1, "A" }, /* Async */
	{ 2, "S" }, /* Sync */
	{ 3, "E" }, /* Either */
	{ 0, NULL }
};

static void
pptp_framing_type_print(netdissect_options *ndo,
                        const uint32_t *framing_type)
{
	ND_PRINT((ndo, " FRAME_TYPE(%s)",
	          tok2str(pptp_ftype_str, "?", EXTRACT_32BITS(framing_type))));
}

static void
pptp_hostname_print(netdissect_options *ndo,
                    const u_char *hostname)
{
	ND_PRINT((ndo, " HOSTNAME(%.64s)", hostname));
}

static void
pptp_id_print(netdissect_options *ndo,
              const uint32_t *id)
{
	ND_PRINT((ndo, " ID(%u)", EXTRACT_32BITS(id)));
}

static void
pptp_max_channel_print(netdissect_options *ndo,
                       const uint16_t *max_channel)
{
	ND_PRINT((ndo, " MAX_CHAN(%u)", EXTRACT_16BITS(max_channel)));
}

static void
pptp_peer_call_id_print(netdissect_options *ndo,
                        const uint16_t *peer_call_id)
{
	ND_PRINT((ndo, " PEER_CALL_ID(%u)", EXTRACT_16BITS(peer_call_id)));
}

static void
pptp_phy_chan_id_print(netdissect_options *ndo,
                       const uint32_t *phy_chan_id)
{
	ND_PRINT((ndo, " PHY_CHAN_ID(%u)", EXTRACT_32BITS(phy_chan_id)));
}

static void
pptp_pkt_proc_delay_print(netdissect_options *ndo,
                          const uint16_t *pkt_proc_delay)
{
	ND_PRINT((ndo, " PROC_DELAY(%u)", EXTRACT_16BITS(pkt_proc_delay)));
}

static void
pptp_proto_ver_print(netdissect_options *ndo,
                     const uint16_t *proto_ver)
{
	ND_PRINT((ndo, " PROTO_VER(%u.%u)",	/* Version.Revision */
	       EXTRACT_16BITS(proto_ver) >> 8,
	       EXTRACT_16BITS(proto_ver) & 0xff));
}

static void
pptp_recv_winsiz_print(netdissect_options *ndo,
                       const uint16_t *recv_winsiz)
{
	ND_PRINT((ndo, " RECV_WIN(%u)", EXTRACT_16BITS(recv_winsiz)));
}

static const struct tok pptp_scrrp_str[] = {
	{ 1, "Successful channel establishment"                           },
	{ 2, "General error"                                              },
	{ 3, "Command channel already exists"                             },
	{ 4, "Requester is not authorized to establish a command channel" },
	{ 5, "The protocol version of the requester is not supported"     },
	{ 0, NULL }
};

static const struct tok pptp_echorp_str[] = {
	{ 1, "OK" },
	{ 2, "General Error" },
	{ 0, NULL }
};

static const struct tok pptp_ocrp_str[] = {
	{ 1, "Connected"     },
	{ 2, "General Error" },
	{ 3, "No Carrier"    },
	{ 4, "Busy"          },
	{ 5, "No Dial Tone"  },
	{ 6, "Time-out"      },
	{ 7, "Do Not Accept" },
	{ 0, NULL }
};

static const struct tok pptp_icrp_str[] = {
	{ 1, "Connect"       },
	{ 2, "General Error" },
	{ 3, "Do Not Accept" },
	{ 0, NULL }
};

static const struct tok pptp_cdn_str[] = {
	{ 1, "Lost Carrier"   },
	{ 2, "General Error"  },
	{ 3, "Admin Shutdown" },
	{ 4, "Request"        },
	{ 0, NULL }
};

static void
pptp_result_code_print(netdissect_options *ndo,
                       const uint8_t *result_code, int ctrl_msg_type)
{
	ND_PRINT((ndo, " RESULT_CODE(%u", *result_code));
	if (ndo->ndo_vflag) {
		const struct tok *dict =
			ctrl_msg_type == PPTP_CTRL_MSG_TYPE_SCCRP    ? pptp_scrrp_str :
			ctrl_msg_type == PPTP_CTRL_MSG_TYPE_StopCCRP ? pptp_echorp_str :
			ctrl_msg_type == PPTP_CTRL_MSG_TYPE_ECHORP   ? pptp_echorp_str :
			ctrl_msg_type == PPTP_CTRL_MSG_TYPE_OCRP     ? pptp_ocrp_str :
			ctrl_msg_type == PPTP_CTRL_MSG_TYPE_ICRP     ? pptp_icrp_str :
			ctrl_msg_type == PPTP_CTRL_MSG_TYPE_CDN      ? pptp_cdn_str :
			NULL; /* assertion error */
		if (dict != NULL)
			ND_PRINT((ndo, ":%s", tok2str(dict, "?", *result_code)));
	}
	ND_PRINT((ndo, ")"));
}

static void
pptp_subaddr_print(netdissect_options *ndo,
                   const u_char *subaddr)
{
	ND_PRINT((ndo, " SUB_ADDR(%.64s)", subaddr));
}

static void
pptp_vendor_print(netdissect_options *ndo,
                  const u_char *vendor)
{
	ND_PRINT((ndo, " VENDOR(%.64s)", vendor));
}

/************************************/
/* PPTP message print out functions */
/************************************/
static void
pptp_sccrq_print(netdissect_options *ndo,
                 const u_char *dat)
{
	const struct pptp_msg_sccrq *ptr = (const struct pptp_msg_sccrq *)dat;

	ND_TCHECK(ptr->proto_ver);
	pptp_proto_ver_print(ndo, &ptr->proto_ver);
	ND_TCHECK(ptr->reserved1);
	ND_TCHECK(ptr->framing_cap);
	pptp_framing_cap_print(ndo, &ptr->framing_cap);
	ND_TCHECK(ptr->bearer_cap);
	pptp_bearer_cap_print(ndo, &ptr->bearer_cap);
	ND_TCHECK(ptr->max_channel);
	pptp_max_channel_print(ndo, &ptr->max_channel);
	ND_TCHECK(ptr->firm_rev);
	pptp_firm_rev_print(ndo, &ptr->firm_rev);
	ND_TCHECK(ptr->hostname);
	pptp_hostname_print(ndo, &ptr->hostname[0]);
	ND_TCHECK(ptr->vendor);
	pptp_vendor_print(ndo, &ptr->vendor[0]);

	return;

trunc:
	ND_PRINT((ndo, "%s", tstr));
}

static void
pptp_sccrp_print(netdissect_options *ndo,
                 const u_char *dat)
{
	const struct pptp_msg_sccrp *ptr = (const struct pptp_msg_sccrp *)dat;

	ND_TCHECK(ptr->proto_ver);
	pptp_proto_ver_print(ndo, &ptr->proto_ver);
	ND_TCHECK(ptr->result_code);
	pptp_result_code_print(ndo, &ptr->result_code, PPTP_CTRL_MSG_TYPE_SCCRP);
	ND_TCHECK(ptr->err_code);
	pptp_err_code_print(ndo, &ptr->err_code);
	ND_TCHECK(ptr->framing_cap);
	pptp_framing_cap_print(ndo, &ptr->framing_cap);
	ND_TCHECK(ptr->bearer_cap);
	pptp_bearer_cap_print(ndo, &ptr->bearer_cap);
	ND_TCHECK(ptr->max_channel);
	pptp_max_channel_print(ndo, &ptr->max_channel);
	ND_TCHECK(ptr->firm_rev);
	pptp_firm_rev_print(ndo, &ptr->firm_rev);
	ND_TCHECK(ptr->hostname);
	pptp_hostname_print(ndo, &ptr->hostname[0]);
	ND_TCHECK(ptr->vendor);
	pptp_vendor_print(ndo, &ptr->vendor[0]);

	return;

trunc:
	ND_PRINT((ndo, "%s", tstr));
}

static void
pptp_stopccrq_print(netdissect_options *ndo,
                    const u_char *dat)
{
	const struct pptp_msg_stopccrq *ptr = (const struct pptp_msg_stopccrq *)dat;

	ND_TCHECK(ptr->reason);
	ND_PRINT((ndo, " REASON(%u", ptr->reason));
	if (ndo->ndo_vflag) {
		switch (ptr->reason) {
		case 1:
			ND_PRINT((ndo, ":None"));
			break;
		case 2:
			ND_PRINT((ndo, ":Stop-Protocol"));
			break;
		case 3:
			ND_PRINT((ndo, ":Stop-Local-Shutdown"));
			break;
		default:
			ND_PRINT((ndo, ":?"));
			break;
		}
	}
	ND_PRINT((ndo, ")"));
	ND_TCHECK(ptr->reserved1);
	ND_TCHECK(ptr->reserved2);

	return;

trunc:
	ND_PRINT((ndo, "%s", tstr));
}

static void
pptp_stopccrp_print(netdissect_options *ndo,
                    const u_char *dat)
{
	const struct pptp_msg_stopccrp *ptr = (const struct pptp_msg_stopccrp *)dat;

	ND_TCHECK(ptr->result_code);
	pptp_result_code_print(ndo, &ptr->result_code, PPTP_CTRL_MSG_TYPE_StopCCRP);
	ND_TCHECK(ptr->err_code);
	pptp_err_code_print(ndo, &ptr->err_code);
	ND_TCHECK(ptr->reserved1);

	return;

trunc:
	ND_PRINT((ndo, "%s", tstr));
}

static void
pptp_echorq_print(netdissect_options *ndo,
                  const u_char *dat)
{
	const struct pptp_msg_echorq *ptr = (const struct pptp_msg_echorq *)dat;

	ND_TCHECK(ptr->id);
	pptp_id_print(ndo, &ptr->id);

	return;

trunc:
	ND_PRINT((ndo, "%s", tstr));
}

static void
pptp_echorp_print(netdissect_options *ndo,
                  const u_char *dat)
{
	const struct pptp_msg_echorp *ptr = (const struct pptp_msg_echorp *)dat;

	ND_TCHECK(ptr->id);
	pptp_id_print(ndo, &ptr->id);
	ND_TCHECK(ptr->result_code);
	pptp_result_code_print(ndo, &ptr->result_code, PPTP_CTRL_MSG_TYPE_ECHORP);
	ND_TCHECK(ptr->err_code);
	pptp_err_code_print(ndo, &ptr->err_code);
	ND_TCHECK(ptr->reserved1);

	return;

trunc:
	ND_PRINT((ndo, "%s", tstr));
}

static void
pptp_ocrq_print(netdissect_options *ndo,
                const u_char *dat)
{
	const struct pptp_msg_ocrq *ptr = (const struct pptp_msg_ocrq *)dat;

	ND_TCHECK(ptr->call_id);
	pptp_call_id_print(ndo, &ptr->call_id);
	ND_TCHECK(ptr->call_ser);
	pptp_call_ser_print(ndo, &ptr->call_ser);
	ND_TCHECK(ptr->min_bps);
	ND_PRINT((ndo, " MIN_BPS(%u)", EXTRACT_32BITS(&ptr->min_bps)));
	ND_TCHECK(ptr->max_bps);
	ND_PRINT((ndo, " MAX_BPS(%u)", EXTRACT_32BITS(&ptr->max_bps)));
	ND_TCHECK(ptr->bearer_type);
	pptp_bearer_type_print(ndo, &ptr->bearer_type);
	ND_TCHECK(ptr->framing_type);
	pptp_framing_type_print(ndo, &ptr->framing_type);
	ND_TCHECK(ptr->recv_winsiz);
	pptp_recv_winsiz_print(ndo, &ptr->recv_winsiz);
	ND_TCHECK(ptr->pkt_proc_delay);
	pptp_pkt_proc_delay_print(ndo, &ptr->pkt_proc_delay);
	ND_TCHECK(ptr->phone_no_len);
	ND_PRINT((ndo, " PHONE_NO_LEN(%u)", EXTRACT_16BITS(&ptr->phone_no_len)));
	ND_TCHECK(ptr->reserved1);
	ND_TCHECK(ptr->phone_no);
	ND_PRINT((ndo, " PHONE_NO(%.64s)", ptr->phone_no));
	ND_TCHECK(ptr->subaddr);
	pptp_subaddr_print(ndo, &ptr->subaddr[0]);

	return;

trunc:
	ND_PRINT((ndo, "%s", tstr));
}

static void
pptp_ocrp_print(netdissect_options *ndo,
                const u_char *dat)
{
	const struct pptp_msg_ocrp *ptr = (const struct pptp_msg_ocrp *)dat;

	ND_TCHECK(ptr->call_id);
	pptp_call_id_print(ndo, &ptr->call_id);
	ND_TCHECK(ptr->peer_call_id);
	pptp_peer_call_id_print(ndo, &ptr->peer_call_id);
	ND_TCHECK(ptr->result_code);
	pptp_result_code_print(ndo, &ptr->result_code, PPTP_CTRL_MSG_TYPE_OCRP);
	ND_TCHECK(ptr->err_code);
	pptp_err_code_print(ndo, &ptr->err_code);
	ND_TCHECK(ptr->cause_code);
	pptp_cause_code_print(ndo, &ptr->cause_code);
	ND_TCHECK(ptr->conn_speed);
	pptp_conn_speed_print(ndo, &ptr->conn_speed);
	ND_TCHECK(ptr->recv_winsiz);
	pptp_recv_winsiz_print(ndo, &ptr->recv_winsiz);
	ND_TCHECK(ptr->pkt_proc_delay);
	pptp_pkt_proc_delay_print(ndo, &ptr->pkt_proc_delay);
	ND_TCHECK(ptr->phy_chan_id);
	pptp_phy_chan_id_print(ndo, &ptr->phy_chan_id);

	return;

trunc:
	ND_PRINT((ndo, "%s", tstr));
}

static void
pptp_icrq_print(netdissect_options *ndo,
                const u_char *dat)
{
	const struct pptp_msg_icrq *ptr = (const struct pptp_msg_icrq *)dat;

	ND_TCHECK(ptr->call_id);
	pptp_call_id_print(ndo, &ptr->call_id);
	ND_TCHECK(ptr->call_ser);
	pptp_call_ser_print(ndo, &ptr->call_ser);
	ND_TCHECK(ptr->bearer_type);
	pptp_bearer_type_print(ndo, &ptr->bearer_type);
	ND_TCHECK(ptr->phy_chan_id);
	pptp_phy_chan_id_print(ndo, &ptr->phy_chan_id);
	ND_TCHECK(ptr->dialed_no_len);
	ND_PRINT((ndo, " DIALED_NO_LEN(%u)", EXTRACT_16BITS(&ptr->dialed_no_len)));
	ND_TCHECK(ptr->dialing_no_len);
	ND_PRINT((ndo, " DIALING_NO_LEN(%u)", EXTRACT_16BITS(&ptr->dialing_no_len)));
	ND_TCHECK(ptr->dialed_no);
	ND_PRINT((ndo, " DIALED_NO(%.64s)", ptr->dialed_no));
	ND_TCHECK(ptr->dialing_no);
	ND_PRINT((ndo, " DIALING_NO(%.64s)", ptr->dialing_no));
	ND_TCHECK(ptr->subaddr);
	pptp_subaddr_print(ndo, &ptr->subaddr[0]);

	return;

trunc:
	ND_PRINT((ndo, "%s", tstr));
}

static void
pptp_icrp_print(netdissect_options *ndo,
                const u_char *dat)
{
	const struct pptp_msg_icrp *ptr = (const struct pptp_msg_icrp *)dat;

	ND_TCHECK(ptr->call_id);
	pptp_call_id_print(ndo, &ptr->call_id);
	ND_TCHECK(ptr->peer_call_id);
	pptp_peer_call_id_print(ndo, &ptr->peer_call_id);
	ND_TCHECK(ptr->result_code);
	pptp_result_code_print(ndo, &ptr->result_code, PPTP_CTRL_MSG_TYPE_ICRP);
	ND_TCHECK(ptr->err_code);
	pptp_err_code_print(ndo, &ptr->err_code);
	ND_TCHECK(ptr->recv_winsiz);
	pptp_recv_winsiz_print(ndo, &ptr->recv_winsiz);
	ND_TCHECK(ptr->pkt_proc_delay);
	pptp_pkt_proc_delay_print(ndo, &ptr->pkt_proc_delay);
	ND_TCHECK(ptr->reserved1);

	return;

trunc:
	ND_PRINT((ndo, "%s", tstr));
}

static void
pptp_iccn_print(netdissect_options *ndo,
                const u_char *dat)
{
	const struct pptp_msg_iccn *ptr = (const struct pptp_msg_iccn *)dat;

	ND_TCHECK(ptr->peer_call_id);
	pptp_peer_call_id_print(ndo, &ptr->peer_call_id);
	ND_TCHECK(ptr->reserved1);
	ND_TCHECK(ptr->conn_speed);
	pptp_conn_speed_print(ndo, &ptr->conn_speed);
	ND_TCHECK(ptr->recv_winsiz);
	pptp_recv_winsiz_print(ndo, &ptr->recv_winsiz);
	ND_TCHECK(ptr->pkt_proc_delay);
	pptp_pkt_proc_delay_print(ndo, &ptr->pkt_proc_delay);
	ND_TCHECK(ptr->framing_type);
	pptp_framing_type_print(ndo, &ptr->framing_type);

	return;

trunc:
	ND_PRINT((ndo, "%s", tstr));
}

static void
pptp_ccrq_print(netdissect_options *ndo,
                const u_char *dat)
{
	const struct pptp_msg_ccrq *ptr = (const struct pptp_msg_ccrq *)dat;

	ND_TCHECK(ptr->call_id);
	pptp_call_id_print(ndo, &ptr->call_id);
	ND_TCHECK(ptr->reserved1);

	return;

trunc:
	ND_PRINT((ndo, "%s", tstr));
}

static void
pptp_cdn_print(netdissect_options *ndo,
               const u_char *dat)
{
	const struct pptp_msg_cdn *ptr = (const struct pptp_msg_cdn *)dat;

	ND_TCHECK(ptr->call_id);
	pptp_call_id_print(ndo, &ptr->call_id);
	ND_TCHECK(ptr->result_code);
	pptp_result_code_print(ndo, &ptr->result_code, PPTP_CTRL_MSG_TYPE_CDN);
	ND_TCHECK(ptr->err_code);
	pptp_err_code_print(ndo, &ptr->err_code);
	ND_TCHECK(ptr->cause_code);
	pptp_cause_code_print(ndo, &ptr->cause_code);
	ND_TCHECK(ptr->reserved1);
	ND_TCHECK(ptr->call_stats);
	ND_PRINT((ndo, " CALL_STATS(%.128s)", ptr->call_stats));

	return;

trunc:
	ND_PRINT((ndo, "%s", tstr));
}

static void
pptp_wen_print(netdissect_options *ndo,
               const u_char *dat)
{
	const struct pptp_msg_wen *ptr = (const struct pptp_msg_wen *)dat;

	ND_TCHECK(ptr->peer_call_id);
	pptp_peer_call_id_print(ndo, &ptr->peer_call_id);
	ND_TCHECK(ptr->reserved1);
	ND_TCHECK(ptr->crc_err);
	ND_PRINT((ndo, " CRC_ERR(%u)", EXTRACT_32BITS(&ptr->crc_err)));
	ND_TCHECK(ptr->framing_err);
	ND_PRINT((ndo, " FRAMING_ERR(%u)", EXTRACT_32BITS(&ptr->framing_err)));
	ND_TCHECK(ptr->hardware_overrun);
	ND_PRINT((ndo, " HARDWARE_OVERRUN(%u)", EXTRACT_32BITS(&ptr->hardware_overrun)));
	ND_TCHECK(ptr->buffer_overrun);
	ND_PRINT((ndo, " BUFFER_OVERRUN(%u)", EXTRACT_32BITS(&ptr->buffer_overrun)));
	ND_TCHECK(ptr->timeout_err);
	ND_PRINT((ndo, " TIMEOUT_ERR(%u)", EXTRACT_32BITS(&ptr->timeout_err)));
	ND_TCHECK(ptr->align_err);
	ND_PRINT((ndo, " ALIGN_ERR(%u)", EXTRACT_32BITS(&ptr->align_err)));

	return;

trunc:
	ND_PRINT((ndo, "%s", tstr));
}

static void
pptp_sli_print(netdissect_options *ndo,
               const u_char *dat)
{
	const struct pptp_msg_sli *ptr = (const struct pptp_msg_sli *)dat;

	ND_TCHECK(ptr->peer_call_id);
	pptp_peer_call_id_print(ndo, &ptr->peer_call_id);
	ND_TCHECK(ptr->reserved1);
	ND_TCHECK(ptr->send_accm);
	ND_PRINT((ndo, " SEND_ACCM(0x%08x)", EXTRACT_32BITS(&ptr->send_accm)));
	ND_TCHECK(ptr->recv_accm);
	ND_PRINT((ndo, " RECV_ACCM(0x%08x)", EXTRACT_32BITS(&ptr->recv_accm)));

	return;

trunc:
	ND_PRINT((ndo, "%s", tstr));
}

void
pptp_print(netdissect_options *ndo,
           const u_char *dat)
{
	const struct pptp_hdr *hdr;
	uint32_t mc;
	uint16_t ctrl_msg_type;

	ND_PRINT((ndo, ": pptp"));

	hdr = (const struct pptp_hdr *)dat;

	ND_TCHECK(hdr->length);
	if (ndo->ndo_vflag) {
		ND_PRINT((ndo, " Length=%u", EXTRACT_16BITS(&hdr->length)));
	}
	ND_TCHECK(hdr->msg_type);
	if (ndo->ndo_vflag) {
		switch(EXTRACT_16BITS(&hdr->msg_type)) {
		case PPTP_MSG_TYPE_CTRL:
			ND_PRINT((ndo, " CTRL-MSG"));
			break;
		case PPTP_MSG_TYPE_MGMT:
			ND_PRINT((ndo, " MGMT-MSG"));
			break;
		default:
			ND_PRINT((ndo, " UNKNOWN-MSG-TYPE"));
			break;
		}
	}

	ND_TCHECK(hdr->magic_cookie);
	mc = EXTRACT_32BITS(&hdr->magic_cookie);
	if (mc != PPTP_MAGIC_COOKIE) {
		ND_PRINT((ndo, " UNEXPECTED Magic-Cookie!!(%08x)", mc));
	}
	if (ndo->ndo_vflag || mc != PPTP_MAGIC_COOKIE) {
		ND_PRINT((ndo, " Magic-Cookie=%08x", mc));
	}
	ND_TCHECK(hdr->ctrl_msg_type);
	ctrl_msg_type = EXTRACT_16BITS(&hdr->ctrl_msg_type);
	if (ctrl_msg_type < PPTP_MAX_MSGTYPE_INDEX) {
		ND_PRINT((ndo, " CTRL_MSGTYPE=%s",
		       pptp_message_type_string[ctrl_msg_type]));
	} else {
		ND_PRINT((ndo, " UNKNOWN_CTRL_MSGTYPE(%u)", ctrl_msg_type));
	}
	ND_TCHECK(hdr->reserved0);

	dat += 12;

	switch(ctrl_msg_type) {
	case PPTP_CTRL_MSG_TYPE_SCCRQ:
		pptp_sccrq_print(ndo, dat);
		break;
	case PPTP_CTRL_MSG_TYPE_SCCRP:
		pptp_sccrp_print(ndo, dat);
		break;
	case PPTP_CTRL_MSG_TYPE_StopCCRQ:
		pptp_stopccrq_print(ndo, dat);
		break;
	case PPTP_CTRL_MSG_TYPE_StopCCRP:
		pptp_stopccrp_print(ndo, dat);
		break;
	case PPTP_CTRL_MSG_TYPE_ECHORQ:
		pptp_echorq_print(ndo, dat);
		break;
	case PPTP_CTRL_MSG_TYPE_ECHORP:
		pptp_echorp_print(ndo, dat);
		break;
	case PPTP_CTRL_MSG_TYPE_OCRQ:
		pptp_ocrq_print(ndo, dat);
		break;
	case PPTP_CTRL_MSG_TYPE_OCRP:
		pptp_ocrp_print(ndo, dat);
		break;
	case PPTP_CTRL_MSG_TYPE_ICRQ:
		pptp_icrq_print(ndo, dat);
		break;
	case PPTP_CTRL_MSG_TYPE_ICRP:
		pptp_icrp_print(ndo, dat);
		break;
	case PPTP_CTRL_MSG_TYPE_ICCN:
		pptp_iccn_print(ndo, dat);
		break;
	case PPTP_CTRL_MSG_TYPE_CCRQ:
		pptp_ccrq_print(ndo, dat);
		break;
	case PPTP_CTRL_MSG_TYPE_CDN:
		pptp_cdn_print(ndo, dat);
		break;
	case PPTP_CTRL_MSG_TYPE_WEN:
		pptp_wen_print(ndo, dat);
		break;
	case PPTP_CTRL_MSG_TYPE_SLI:
		pptp_sli_print(ndo, dat);
		break;
	default:
		/* do nothing */
		break;
	}

	return;

trunc:
	ND_PRINT((ndo, "%s", tstr));
}
