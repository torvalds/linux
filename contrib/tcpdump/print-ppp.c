/*
 * Copyright (c) 1990, 1991, 1993, 1994, 1995, 1996, 1997
 *	The Regents of the University of California.  All rights reserved.
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
 * Extensively modified by Motonori Shindo (mshindo@mshindo.net) for more
 * complete PPP support.
 */

/* \summary: Point to Point Protocol (PPP) printer */

/*
 * TODO:
 * o resolve XXX as much as possible
 * o MP support
 * o BAP support
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <netdissect-stdinc.h>

#ifdef __bsdi__
#include <net/slcompress.h>
#include <net/if_ppp.h>
#endif

#include <stdlib.h>

#include "netdissect.h"
#include "extract.h"
#include "addrtoname.h"
#include "ppp.h"
#include "chdlc.h"
#include "ethertype.h"
#include "oui.h"

/*
 * The following constatns are defined by IANA. Please refer to
 *    http://www.isi.edu/in-notes/iana/assignments/ppp-numbers
 * for the up-to-date information.
 */

/* Protocol Codes defined in ppp.h */

static const struct tok ppptype2str[] = {
        { PPP_IP,	  "IP" },
        { PPP_OSI,	  "OSI" },
        { PPP_NS,	  "NS" },
        { PPP_DECNET,	  "DECNET" },
        { PPP_APPLE,	  "APPLE" },
	{ PPP_IPX,	  "IPX" },
	{ PPP_VJC,	  "VJC IP" },
	{ PPP_VJNC,	  "VJNC IP" },
	{ PPP_BRPDU,	  "BRPDU" },
	{ PPP_STII,	  "STII" },
	{ PPP_VINES,	  "VINES" },
	{ PPP_MPLS_UCAST, "MPLS" },
	{ PPP_MPLS_MCAST, "MPLS" },
        { PPP_COMP,       "Compressed"},
        { PPP_ML,         "MLPPP"},
        { PPP_IPV6,       "IP6"},

	{ PPP_HELLO,	  "HELLO" },
	{ PPP_LUXCOM,	  "LUXCOM" },
	{ PPP_SNS,	  "SNS" },
	{ PPP_IPCP,	  "IPCP" },
	{ PPP_OSICP,	  "OSICP" },
	{ PPP_NSCP,	  "NSCP" },
	{ PPP_DECNETCP,   "DECNETCP" },
	{ PPP_APPLECP,	  "APPLECP" },
	{ PPP_IPXCP,	  "IPXCP" },
	{ PPP_STIICP,	  "STIICP" },
	{ PPP_VINESCP,	  "VINESCP" },
        { PPP_IPV6CP,     "IP6CP" },
	{ PPP_MPLSCP,	  "MPLSCP" },

	{ PPP_LCP,	  "LCP" },
	{ PPP_PAP,	  "PAP" },
	{ PPP_LQM,	  "LQM" },
	{ PPP_CHAP,	  "CHAP" },
	{ PPP_EAP,	  "EAP" },
	{ PPP_SPAP,	  "SPAP" },
	{ PPP_SPAP_OLD,	  "Old-SPAP" },
	{ PPP_BACP,	  "BACP" },
	{ PPP_BAP,	  "BAP" },
	{ PPP_MPCP,	  "MLPPP-CP" },
	{ PPP_CCP,	  "CCP" },
	{ 0,		  NULL }
};

/* Control Protocols (LCP/IPCP/CCP etc.) Codes defined in RFC 1661 */

#define CPCODES_VEXT		0	/* Vendor-Specific (RFC2153) */
#define CPCODES_CONF_REQ	1	/* Configure-Request */
#define CPCODES_CONF_ACK	2	/* Configure-Ack */
#define CPCODES_CONF_NAK	3	/* Configure-Nak */
#define CPCODES_CONF_REJ	4	/* Configure-Reject */
#define CPCODES_TERM_REQ	5	/* Terminate-Request */
#define CPCODES_TERM_ACK	6	/* Terminate-Ack */
#define CPCODES_CODE_REJ	7	/* Code-Reject */
#define CPCODES_PROT_REJ	8	/* Protocol-Reject (LCP only) */
#define CPCODES_ECHO_REQ	9	/* Echo-Request (LCP only) */
#define CPCODES_ECHO_RPL	10	/* Echo-Reply (LCP only) */
#define CPCODES_DISC_REQ	11	/* Discard-Request (LCP only) */
#define CPCODES_ID		12	/* Identification (LCP only) RFC1570 */
#define CPCODES_TIME_REM	13	/* Time-Remaining (LCP only) RFC1570 */
#define CPCODES_RESET_REQ	14	/* Reset-Request (CCP only) RFC1962 */
#define CPCODES_RESET_REP	15	/* Reset-Reply (CCP only) */

static const struct tok cpcodes[] = {
	{CPCODES_VEXT,      "Vendor-Extension"}, /* RFC2153 */
	{CPCODES_CONF_REQ,  "Conf-Request"},
        {CPCODES_CONF_ACK,  "Conf-Ack"},
	{CPCODES_CONF_NAK,  "Conf-Nack"},
	{CPCODES_CONF_REJ,  "Conf-Reject"},
	{CPCODES_TERM_REQ,  "Term-Request"},
	{CPCODES_TERM_ACK,  "Term-Ack"},
	{CPCODES_CODE_REJ,  "Code-Reject"},
	{CPCODES_PROT_REJ,  "Prot-Reject"},
	{CPCODES_ECHO_REQ,  "Echo-Request"},
	{CPCODES_ECHO_RPL,  "Echo-Reply"},
	{CPCODES_DISC_REQ,  "Disc-Req"},
	{CPCODES_ID,        "Ident"},            /* RFC1570 */
	{CPCODES_TIME_REM,  "Time-Rem"},         /* RFC1570 */
	{CPCODES_RESET_REQ, "Reset-Req"},        /* RFC1962 */
	{CPCODES_RESET_REP, "Reset-Ack"},        /* RFC1962 */
        {0,                 NULL}
};

/* LCP Config Options */

#define LCPOPT_VEXT	0
#define LCPOPT_MRU	1
#define LCPOPT_ACCM	2
#define LCPOPT_AP	3
#define LCPOPT_QP	4
#define LCPOPT_MN	5
#define LCPOPT_DEP6	6
#define LCPOPT_PFC	7
#define LCPOPT_ACFC	8
#define LCPOPT_FCSALT	9
#define LCPOPT_SDP	10
#define LCPOPT_NUMMODE	11
#define LCPOPT_DEP12	12
#define LCPOPT_CBACK	13
#define LCPOPT_DEP14	14
#define LCPOPT_DEP15	15
#define LCPOPT_DEP16	16
#define LCPOPT_MLMRRU	17
#define LCPOPT_MLSSNHF	18
#define LCPOPT_MLED	19
#define LCPOPT_PROP	20
#define LCPOPT_DCEID	21
#define LCPOPT_MPP	22
#define LCPOPT_LD	23
#define LCPOPT_LCPAOPT	24
#define LCPOPT_COBS	25
#define LCPOPT_PE	26
#define LCPOPT_MLHF	27
#define LCPOPT_I18N	28
#define LCPOPT_SDLOS	29
#define LCPOPT_PPPMUX	30

#define LCPOPT_MIN LCPOPT_VEXT
#define LCPOPT_MAX LCPOPT_PPPMUX

static const char *lcpconfopts[] = {
	"Vend-Ext",		/* (0) */
	"MRU",			/* (1) */
	"ACCM",			/* (2) */
	"Auth-Prot",		/* (3) */
	"Qual-Prot",		/* (4) */
	"Magic-Num",		/* (5) */
	"deprecated(6)",	/* used to be a Quality Protocol */
	"PFC",			/* (7) */
	"ACFC",			/* (8) */
	"FCS-Alt",		/* (9) */
	"SDP",			/* (10) */
	"Num-Mode",		/* (11) */
	"deprecated(12)",	/* used to be a Multi-Link-Procedure*/
	"Call-Back",		/* (13) */
	"deprecated(14)",	/* used to be a Connect-Time */
	"deprecated(15)",	/* used to be a Compund-Frames */
	"deprecated(16)",	/* used to be a Nominal-Data-Encap */
	"MRRU",			/* (17) */
	"12-Bit seq #",		/* (18) */
	"End-Disc",		/* (19) */
	"Proprietary",		/* (20) */
	"DCE-Id",		/* (21) */
	"MP+",			/* (22) */
	"Link-Disc",		/* (23) */
	"LCP-Auth-Opt",		/* (24) */
	"COBS",			/* (25) */
	"Prefix-elision",	/* (26) */
	"Multilink-header-Form",/* (27) */
	"I18N",			/* (28) */
	"SDL-over-SONET/SDH",	/* (29) */
	"PPP-Muxing",		/* (30) */
};

/* ECP - to be supported */

/* CCP Config Options */

#define CCPOPT_OUI	0	/* RFC1962 */
#define CCPOPT_PRED1	1	/* RFC1962 */
#define CCPOPT_PRED2	2	/* RFC1962 */
#define CCPOPT_PJUMP	3	/* RFC1962 */
/* 4-15 unassigned */
#define CCPOPT_HPPPC	16	/* RFC1962 */
#define CCPOPT_STACLZS	17	/* RFC1974 */
#define CCPOPT_MPPC	18	/* RFC2118 */
#define CCPOPT_GFZA	19	/* RFC1962 */
#define CCPOPT_V42BIS	20	/* RFC1962 */
#define CCPOPT_BSDCOMP	21	/* RFC1977 */
/* 22 unassigned */
#define CCPOPT_LZSDCP	23	/* RFC1967 */
#define CCPOPT_MVRCA	24	/* RFC1975 */
#define CCPOPT_DEC	25	/* RFC1976 */
#define CCPOPT_DEFLATE	26	/* RFC1979 */
/* 27-254 unassigned */
#define CCPOPT_RESV	255	/* RFC1962 */

static const struct tok ccpconfopts_values[] = {
        { CCPOPT_OUI, "OUI" },
        { CCPOPT_PRED1, "Pred-1" },
        { CCPOPT_PRED2, "Pred-2" },
        { CCPOPT_PJUMP, "Puddle" },
        { CCPOPT_HPPPC, "HP-PPC" },
        { CCPOPT_STACLZS, "Stac-LZS" },
        { CCPOPT_MPPC, "MPPC" },
        { CCPOPT_GFZA, "Gand-FZA" },
        { CCPOPT_V42BIS, "V.42bis" },
        { CCPOPT_BSDCOMP, "BSD-Comp" },
        { CCPOPT_LZSDCP, "LZS-DCP" },
        { CCPOPT_MVRCA, "MVRCA" },
        { CCPOPT_DEC, "DEC" },
        { CCPOPT_DEFLATE, "Deflate" },
        { CCPOPT_RESV, "Reserved"},
        {0,                 NULL}
};

/* BACP Config Options */

#define BACPOPT_FPEER	1	/* RFC2125 */

static const struct tok bacconfopts_values[] = {
        { BACPOPT_FPEER, "Favored-Peer" },
        {0,                 NULL}
};


/* SDCP - to be supported */

/* IPCP Config Options */
#define IPCPOPT_2ADDR	1	/* RFC1172, RFC1332 (deprecated) */
#define IPCPOPT_IPCOMP	2	/* RFC1332 */
#define IPCPOPT_ADDR	3	/* RFC1332 */
#define IPCPOPT_MOBILE4	4	/* RFC2290 */
#define IPCPOPT_PRIDNS	129	/* RFC1877 */
#define IPCPOPT_PRINBNS	130	/* RFC1877 */
#define IPCPOPT_SECDNS	131	/* RFC1877 */
#define IPCPOPT_SECNBNS	132	/* RFC1877 */

static const struct tok ipcpopt_values[] = {
        { IPCPOPT_2ADDR, "IP-Addrs" },
        { IPCPOPT_IPCOMP, "IP-Comp" },
        { IPCPOPT_ADDR, "IP-Addr" },
        { IPCPOPT_MOBILE4, "Home-Addr" },
        { IPCPOPT_PRIDNS, "Pri-DNS" },
        { IPCPOPT_PRINBNS, "Pri-NBNS" },
        { IPCPOPT_SECDNS, "Sec-DNS" },
        { IPCPOPT_SECNBNS, "Sec-NBNS" },
	{ 0,		  NULL }
};

#define IPCPOPT_IPCOMP_HDRCOMP 0x61  /* rfc3544 */
#define IPCPOPT_IPCOMP_MINLEN    14

static const struct tok ipcpopt_compproto_values[] = {
        { PPP_VJC, "VJ-Comp" },
        { IPCPOPT_IPCOMP_HDRCOMP, "IP Header Compression" },
	{ 0,		  NULL }
};

static const struct tok ipcpopt_compproto_subopt_values[] = {
        { 1, "RTP-Compression" },
        { 2, "Enhanced RTP-Compression" },
	{ 0,		  NULL }
};

/* IP6CP Config Options */
#define IP6CP_IFID      1

static const struct tok ip6cpopt_values[] = {
        { IP6CP_IFID, "Interface-ID" },
	{ 0,		  NULL }
};

/* ATCP - to be supported */
/* OSINLCP - to be supported */
/* BVCP - to be supported */
/* BCP - to be supported */
/* IPXCP - to be supported */
/* MPLSCP - to be supported */

/* Auth Algorithms */

/* 0-4 Reserved (RFC1994) */
#define AUTHALG_CHAPMD5	5	/* RFC1994 */
#define AUTHALG_MSCHAP1	128	/* RFC2433 */
#define AUTHALG_MSCHAP2	129	/* RFC2795 */

static const struct tok authalg_values[] = {
        { AUTHALG_CHAPMD5, "MD5" },
        { AUTHALG_MSCHAP1, "MS-CHAPv1" },
        { AUTHALG_MSCHAP2, "MS-CHAPv2" },
	{ 0,		  NULL }
};

/* FCS Alternatives - to be supported */

/* Multilink Endpoint Discriminator (RFC1717) */
#define MEDCLASS_NULL	0	/* Null Class */
#define MEDCLASS_LOCAL	1	/* Locally Assigned */
#define MEDCLASS_IPV4	2	/* Internet Protocol (IPv4) */
#define MEDCLASS_MAC	3	/* IEEE 802.1 global MAC address */
#define MEDCLASS_MNB	4	/* PPP Magic Number Block */
#define MEDCLASS_PSNDN	5	/* Public Switched Network Director Number */

/* PPP LCP Callback */
#define CALLBACK_AUTH	0	/* Location determined by user auth */
#define CALLBACK_DSTR	1	/* Dialing string */
#define CALLBACK_LID	2	/* Location identifier */
#define CALLBACK_E164	3	/* E.164 number */
#define CALLBACK_X500	4	/* X.500 distinguished name */
#define CALLBACK_CBCP	6	/* Location is determined during CBCP nego */

static const struct tok ppp_callback_values[] = {
        { CALLBACK_AUTH, "UserAuth" },
        { CALLBACK_DSTR, "DialString" },
        { CALLBACK_LID, "LocalID" },
        { CALLBACK_E164, "E.164" },
        { CALLBACK_X500, "X.500" },
        { CALLBACK_CBCP, "CBCP" },
	{ 0,		  NULL }
};

/* CHAP */

#define CHAP_CHAL	1
#define CHAP_RESP	2
#define CHAP_SUCC	3
#define CHAP_FAIL	4

static const struct tok chapcode_values[] = {
	{ CHAP_CHAL, "Challenge" },
	{ CHAP_RESP, "Response" },
	{ CHAP_SUCC, "Success" },
	{ CHAP_FAIL, "Fail" },
        { 0, NULL}
};

/* PAP */

#define PAP_AREQ	1
#define PAP_AACK	2
#define PAP_ANAK	3

static const struct tok papcode_values[] = {
        { PAP_AREQ, "Auth-Req" },
        { PAP_AACK, "Auth-ACK" },
        { PAP_ANAK, "Auth-NACK" },
        { 0, NULL }
};

/* BAP */
#define BAP_CALLREQ	1
#define BAP_CALLRES	2
#define BAP_CBREQ	3
#define BAP_CBRES	4
#define BAP_LDQREQ	5
#define BAP_LDQRES	6
#define BAP_CSIND	7
#define BAP_CSRES	8

static int print_lcp_config_options(netdissect_options *, const u_char *p, int);
static int print_ipcp_config_options(netdissect_options *, const u_char *p, int);
static int print_ip6cp_config_options(netdissect_options *, const u_char *p, int);
static int print_ccp_config_options(netdissect_options *, const u_char *p, int);
static int print_bacp_config_options(netdissect_options *, const u_char *p, int);
static void handle_ppp(netdissect_options *, u_int proto, const u_char *p, int length);

/* generic Control Protocol (e.g. LCP, IPCP, CCP, etc.) handler */
static void
handle_ctrl_proto(netdissect_options *ndo,
                  u_int proto, const u_char *pptr, int length)
{
	const char *typestr;
	u_int code, len;
	int (*pfunc)(netdissect_options *, const u_char *, int);
	int x, j;
        const u_char *tptr;

        tptr=pptr;

        typestr = tok2str(ppptype2str, "unknown ctrl-proto (0x%04x)", proto);
	ND_PRINT((ndo, "%s, ", typestr));

	if (length < 4) /* FIXME weak boundary checking */
		goto trunc;
	ND_TCHECK2(*tptr, 2);

	code = *tptr++;

	ND_PRINT((ndo, "%s (0x%02x), id %u, length %u",
	          tok2str(cpcodes, "Unknown Opcode",code),
	          code,
	          *tptr++, /* ID */
	          length + 2));

	if (!ndo->ndo_vflag)
		return;

	if (length <= 4)
		return;    /* there may be a NULL confreq etc. */

	ND_TCHECK2(*tptr, 2);
	len = EXTRACT_16BITS(tptr);
	tptr += 2;

	ND_PRINT((ndo, "\n\tencoded length %u (=Option(s) length %u)", len, len - 4));

	if (ndo->ndo_vflag > 1)
		print_unknown_data(ndo, pptr - 2, "\n\t", 6);


	switch (code) {
	case CPCODES_VEXT:
		if (length < 11)
			break;
		ND_TCHECK2(*tptr, 4);
		ND_PRINT((ndo, "\n\t  Magic-Num 0x%08x", EXTRACT_32BITS(tptr)));
		tptr += 4;
		ND_TCHECK2(*tptr, 3);
		ND_PRINT((ndo, " Vendor: %s (%u)",
                       tok2str(oui_values,"Unknown",EXTRACT_24BITS(tptr)),
                       EXTRACT_24BITS(tptr)));
		/* XXX: need to decode Kind and Value(s)? */
		break;
	case CPCODES_CONF_REQ:
	case CPCODES_CONF_ACK:
	case CPCODES_CONF_NAK:
	case CPCODES_CONF_REJ:
		x = len - 4;	/* Code(1), Identifier(1) and Length(2) */
		do {
			switch (proto) {
			case PPP_LCP:
				pfunc = print_lcp_config_options;
				break;
			case PPP_IPCP:
				pfunc = print_ipcp_config_options;
				break;
			case PPP_IPV6CP:
				pfunc = print_ip6cp_config_options;
				break;
			case PPP_CCP:
				pfunc = print_ccp_config_options;
				break;
			case PPP_BACP:
				pfunc = print_bacp_config_options;
				break;
			default:
				/*
				 * No print routine for the options for
				 * this protocol.
				 */
				pfunc = NULL;
				break;
			}

			if (pfunc == NULL) /* catch the above null pointer if unknown CP */
				break;

			if ((j = (*pfunc)(ndo, tptr, len)) == 0)
				break;
			x -= j;
			tptr += j;
		} while (x > 0);
		break;

	case CPCODES_TERM_REQ:
	case CPCODES_TERM_ACK:
		/* XXX: need to decode Data? */
		break;
	case CPCODES_CODE_REJ:
		/* XXX: need to decode Rejected-Packet? */
		break;
	case CPCODES_PROT_REJ:
		if (length < 6)
			break;
		ND_TCHECK2(*tptr, 2);
		ND_PRINT((ndo, "\n\t  Rejected %s Protocol (0x%04x)",
		       tok2str(ppptype2str,"unknown", EXTRACT_16BITS(tptr)),
		       EXTRACT_16BITS(tptr)));
		/* XXX: need to decode Rejected-Information? - hexdump for now */
		if (len > 6) {
			ND_PRINT((ndo, "\n\t  Rejected Packet"));
			print_unknown_data(ndo, tptr + 2, "\n\t    ", len - 2);
		}
		break;
	case CPCODES_ECHO_REQ:
	case CPCODES_ECHO_RPL:
	case CPCODES_DISC_REQ:
		if (length < 8)
			break;
		ND_TCHECK2(*tptr, 4);
		ND_PRINT((ndo, "\n\t  Magic-Num 0x%08x", EXTRACT_32BITS(tptr)));
		/* XXX: need to decode Data? - hexdump for now */
		if (len > 8) {
			ND_PRINT((ndo, "\n\t  -----trailing data-----"));
			ND_TCHECK2(tptr[4], len - 8);
			print_unknown_data(ndo, tptr + 4, "\n\t  ", len - 8);
		}
		break;
	case CPCODES_ID:
		if (length < 8)
			break;
		ND_TCHECK2(*tptr, 4);
		ND_PRINT((ndo, "\n\t  Magic-Num 0x%08x", EXTRACT_32BITS(tptr)));
		/* RFC 1661 says this is intended to be human readable */
		if (len > 8) {
			ND_PRINT((ndo, "\n\t  Message\n\t    "));
			if (fn_printn(ndo, tptr + 4, len - 4, ndo->ndo_snapend))
				goto trunc;
		}
		break;
	case CPCODES_TIME_REM:
		if (length < 12)
			break;
		ND_TCHECK2(*tptr, 4);
		ND_PRINT((ndo, "\n\t  Magic-Num 0x%08x", EXTRACT_32BITS(tptr)));
		ND_TCHECK2(*(tptr + 4), 4);
		ND_PRINT((ndo, ", Seconds-Remaining %us", EXTRACT_32BITS(tptr + 4)));
		/* XXX: need to decode Message? */
		break;
	default:
		/* XXX this is dirty but we do not get the
		 * original pointer passed to the begin
		 * the PPP packet */
		if (ndo->ndo_vflag <= 1)
			print_unknown_data(ndo, pptr - 2, "\n\t  ", length + 2);
		break;
	}
	return;

trunc:
	ND_PRINT((ndo, "[|%s]", typestr));
}

/* LCP config options */
static int
print_lcp_config_options(netdissect_options *ndo,
                         const u_char *p, int length)
{
	int len, opt;

	if (length < 2)
		return 0;
	ND_TCHECK2(*p, 2);
	len = p[1];
	opt = p[0];
	if (length < len)
		return 0;
	if (len < 2) {
		if ((opt >= LCPOPT_MIN) && (opt <= LCPOPT_MAX))
			ND_PRINT((ndo, "\n\t  %s Option (0x%02x), length %u (length bogus, should be >= 2)",
			          lcpconfopts[opt], opt, len));
		else
			ND_PRINT((ndo, "\n\tunknown LCP option 0x%02x", opt));
		return 0;
	}
	if ((opt >= LCPOPT_MIN) && (opt <= LCPOPT_MAX))
		ND_PRINT((ndo, "\n\t  %s Option (0x%02x), length %u", lcpconfopts[opt], opt, len));
	else {
		ND_PRINT((ndo, "\n\tunknown LCP option 0x%02x", opt));
		return len;
	}

	switch (opt) {
	case LCPOPT_VEXT:
		if (len < 6) {
			ND_PRINT((ndo, " (length bogus, should be >= 6)"));
			return len;
		}
		ND_TCHECK_24BITS(p + 2);
		ND_PRINT((ndo, ": Vendor: %s (%u)",
			tok2str(oui_values,"Unknown",EXTRACT_24BITS(p+2)),
			EXTRACT_24BITS(p + 2)));
#if 0
		ND_TCHECK(p[5]);
		ND_PRINT((ndo, ", kind: 0x%02x", p[5]));
		ND_PRINT((ndo, ", Value: 0x"));
		for (i = 0; i < len - 6; i++) {
			ND_TCHECK(p[6 + i]);
			ND_PRINT((ndo, "%02x", p[6 + i]));
		}
#endif
		break;
	case LCPOPT_MRU:
		if (len != 4) {
			ND_PRINT((ndo, " (length bogus, should be = 4)"));
			return len;
		}
		ND_TCHECK_16BITS(p + 2);
		ND_PRINT((ndo, ": %u", EXTRACT_16BITS(p + 2)));
		break;
	case LCPOPT_ACCM:
		if (len != 6) {
			ND_PRINT((ndo, " (length bogus, should be = 6)"));
			return len;
		}
		ND_TCHECK_32BITS(p + 2);
		ND_PRINT((ndo, ": 0x%08x", EXTRACT_32BITS(p + 2)));
		break;
	case LCPOPT_AP:
		if (len < 4) {
			ND_PRINT((ndo, " (length bogus, should be >= 4)"));
			return len;
		}
		ND_TCHECK_16BITS(p + 2);
		ND_PRINT((ndo, ": %s", tok2str(ppptype2str, "Unknown Auth Proto (0x04x)", EXTRACT_16BITS(p + 2))));

		switch (EXTRACT_16BITS(p+2)) {
		case PPP_CHAP:
			ND_TCHECK(p[4]);
			ND_PRINT((ndo, ", %s", tok2str(authalg_values, "Unknown Auth Alg %u", p[4])));
			break;
		case PPP_PAP: /* fall through */
		case PPP_EAP:
		case PPP_SPAP:
		case PPP_SPAP_OLD:
                        break;
		default:
			print_unknown_data(ndo, p, "\n\t", len);
		}
		break;
	case LCPOPT_QP:
		if (len < 4) {
			ND_PRINT((ndo, " (length bogus, should be >= 4)"));
			return 0;
		}
		ND_TCHECK_16BITS(p+2);
		if (EXTRACT_16BITS(p+2) == PPP_LQM)
			ND_PRINT((ndo, ": LQR"));
		else
			ND_PRINT((ndo, ": unknown"));
		break;
	case LCPOPT_MN:
		if (len != 6) {
			ND_PRINT((ndo, " (length bogus, should be = 6)"));
			return 0;
		}
		ND_TCHECK_32BITS(p + 2);
		ND_PRINT((ndo, ": 0x%08x", EXTRACT_32BITS(p + 2)));
		break;
	case LCPOPT_PFC:
		break;
	case LCPOPT_ACFC:
		break;
	case LCPOPT_LD:
		if (len != 4) {
			ND_PRINT((ndo, " (length bogus, should be = 4)"));
			return 0;
		}
		ND_TCHECK_16BITS(p + 2);
		ND_PRINT((ndo, ": 0x%04x", EXTRACT_16BITS(p + 2)));
		break;
	case LCPOPT_CBACK:
		if (len < 3) {
			ND_PRINT((ndo, " (length bogus, should be >= 3)"));
			return 0;
		}
		ND_PRINT((ndo, ": "));
		ND_TCHECK(p[2]);
		ND_PRINT((ndo, ": Callback Operation %s (%u)",
                       tok2str(ppp_callback_values, "Unknown", p[2]),
                       p[2]));
		break;
	case LCPOPT_MLMRRU:
		if (len != 4) {
			ND_PRINT((ndo, " (length bogus, should be = 4)"));
			return 0;
		}
		ND_TCHECK_16BITS(p + 2);
		ND_PRINT((ndo, ": %u", EXTRACT_16BITS(p + 2)));
		break;
	case LCPOPT_MLED:
		if (len < 3) {
			ND_PRINT((ndo, " (length bogus, should be >= 3)"));
			return 0;
		}
		ND_TCHECK(p[2]);
		switch (p[2]) {		/* class */
		case MEDCLASS_NULL:
			ND_PRINT((ndo, ": Null"));
			break;
		case MEDCLASS_LOCAL:
			ND_PRINT((ndo, ": Local")); /* XXX */
			break;
		case MEDCLASS_IPV4:
			if (len != 7) {
				ND_PRINT((ndo, " (length bogus, should be = 7)"));
				return 0;
			}
			ND_TCHECK2(*(p + 3), 4);
			ND_PRINT((ndo, ": IPv4 %s", ipaddr_string(ndo, p + 3)));
			break;
		case MEDCLASS_MAC:
			if (len != 9) {
				ND_PRINT((ndo, " (length bogus, should be = 9)"));
				return 0;
			}
			ND_TCHECK2(*(p + 3), 6);
			ND_PRINT((ndo, ": MAC %s", etheraddr_string(ndo, p + 3)));
			break;
		case MEDCLASS_MNB:
			ND_PRINT((ndo, ": Magic-Num-Block")); /* XXX */
			break;
		case MEDCLASS_PSNDN:
			ND_PRINT((ndo, ": PSNDN")); /* XXX */
			break;
		default:
			ND_PRINT((ndo, ": Unknown class %u", p[2]));
			break;
		}
		break;

/* XXX: to be supported */
#if 0
	case LCPOPT_DEP6:
	case LCPOPT_FCSALT:
	case LCPOPT_SDP:
	case LCPOPT_NUMMODE:
	case LCPOPT_DEP12:
	case LCPOPT_DEP14:
	case LCPOPT_DEP15:
	case LCPOPT_DEP16:
        case LCPOPT_MLSSNHF:
	case LCPOPT_PROP:
	case LCPOPT_DCEID:
	case LCPOPT_MPP:
	case LCPOPT_LCPAOPT:
	case LCPOPT_COBS:
	case LCPOPT_PE:
	case LCPOPT_MLHF:
	case LCPOPT_I18N:
	case LCPOPT_SDLOS:
	case LCPOPT_PPPMUX:
		break;
#endif
	default:
		/*
		 * Unknown option; dump it as raw bytes now if we're
		 * not going to do so below.
		 */
		if (ndo->ndo_vflag < 2)
			print_unknown_data(ndo, &p[2], "\n\t    ", len - 2);
		break;
	}

	if (ndo->ndo_vflag > 1)
		print_unknown_data(ndo, &p[2], "\n\t    ", len - 2); /* exclude TLV header */

	return len;

trunc:
	ND_PRINT((ndo, "[|lcp]"));
	return 0;
}

/* ML-PPP*/
static const struct tok ppp_ml_flag_values[] = {
    { 0x80, "begin" },
    { 0x40, "end" },
    { 0, NULL }
};

static void
handle_mlppp(netdissect_options *ndo,
             const u_char *p, int length)
{
    if (!ndo->ndo_eflag)
        ND_PRINT((ndo, "MLPPP, "));

    if (length < 2) {
        ND_PRINT((ndo, "[|mlppp]"));
        return;
    }
    if (!ND_TTEST_16BITS(p)) {
        ND_PRINT((ndo, "[|mlppp]"));
        return;
    }

    ND_PRINT((ndo, "seq 0x%03x, Flags [%s], length %u",
           (EXTRACT_16BITS(p))&0x0fff, /* only support 12-Bit sequence space for now */
           bittok2str(ppp_ml_flag_values, "none", *p & 0xc0),
           length));
}

/* CHAP */
static void
handle_chap(netdissect_options *ndo,
            const u_char *p, int length)
{
	u_int code, len;
	int val_size, name_size, msg_size;
	const u_char *p0;
	int i;

	p0 = p;
	if (length < 1) {
		ND_PRINT((ndo, "[|chap]"));
		return;
	} else if (length < 4) {
		ND_TCHECK(*p);
		ND_PRINT((ndo, "[|chap 0x%02x]", *p));
		return;
	}

	ND_TCHECK(*p);
	code = *p;
	ND_PRINT((ndo, "CHAP, %s (0x%02x)",
               tok2str(chapcode_values,"unknown",code),
               code));
	p++;

	ND_TCHECK(*p);
	ND_PRINT((ndo, ", id %u", *p));		/* ID */
	p++;

	ND_TCHECK2(*p, 2);
	len = EXTRACT_16BITS(p);
	p += 2;

	/*
	 * Note that this is a generic CHAP decoding routine. Since we
	 * don't know which flavor of CHAP (i.e. CHAP-MD5, MS-CHAPv1,
	 * MS-CHAPv2) is used at this point, we can't decode packet
	 * specifically to each algorithms. Instead, we simply decode
	 * the GCD (Gratest Common Denominator) for all algorithms.
	 */
	switch (code) {
	case CHAP_CHAL:
	case CHAP_RESP:
		if (length - (p - p0) < 1)
			return;
		ND_TCHECK(*p);
		val_size = *p;		/* value size */
		p++;
		if (length - (p - p0) < val_size)
			return;
		ND_PRINT((ndo, ", Value "));
		for (i = 0; i < val_size; i++) {
			ND_TCHECK(*p);
			ND_PRINT((ndo, "%02x", *p++));
		}
		name_size = len - (p - p0);
		ND_PRINT((ndo, ", Name "));
		for (i = 0; i < name_size; i++) {
			ND_TCHECK(*p);
			safeputchar(ndo, *p++);
		}
		break;
	case CHAP_SUCC:
	case CHAP_FAIL:
		msg_size = len - (p - p0);
		ND_PRINT((ndo, ", Msg "));
		for (i = 0; i< msg_size; i++) {
			ND_TCHECK(*p);
			safeputchar(ndo, *p++);
		}
		break;
	}
	return;

trunc:
	ND_PRINT((ndo, "[|chap]"));
}

/* PAP (see RFC 1334) */
static void
handle_pap(netdissect_options *ndo,
           const u_char *p, int length)
{
	u_int code, len;
	int peerid_len, passwd_len, msg_len;
	const u_char *p0;
	int i;

	p0 = p;
	if (length < 1) {
		ND_PRINT((ndo, "[|pap]"));
		return;
	} else if (length < 4) {
		ND_TCHECK(*p);
		ND_PRINT((ndo, "[|pap 0x%02x]", *p));
		return;
	}

	ND_TCHECK(*p);
	code = *p;
	ND_PRINT((ndo, "PAP, %s (0x%02x)",
	          tok2str(papcode_values, "unknown", code),
	          code));
	p++;

	ND_TCHECK(*p);
	ND_PRINT((ndo, ", id %u", *p));		/* ID */
	p++;

	ND_TCHECK2(*p, 2);
	len = EXTRACT_16BITS(p);
	p += 2;

	if ((int)len > length) {
		ND_PRINT((ndo, ", length %u > packet size", len));
		return;
	}
	length = len;
	if (length < (p - p0)) {
		ND_PRINT((ndo, ", length %u < PAP header length", length));
		return;
	}

	switch (code) {
	case PAP_AREQ:
		/* A valid Authenticate-Request is 6 or more octets long. */
		if (len < 6)
			goto trunc;
		if (length - (p - p0) < 1)
			return;
		ND_TCHECK(*p);
		peerid_len = *p;	/* Peer-ID Length */
		p++;
		if (length - (p - p0) < peerid_len)
			return;
		ND_PRINT((ndo, ", Peer "));
		for (i = 0; i < peerid_len; i++) {
			ND_TCHECK(*p);
			safeputchar(ndo, *p++);
		}

		if (length - (p - p0) < 1)
			return;
		ND_TCHECK(*p);
		passwd_len = *p;	/* Password Length */
		p++;
		if (length - (p - p0) < passwd_len)
			return;
		ND_PRINT((ndo, ", Name "));
		for (i = 0; i < passwd_len; i++) {
			ND_TCHECK(*p);
			safeputchar(ndo, *p++);
		}
		break;
	case PAP_AACK:
	case PAP_ANAK:
		/* Although some implementations ignore truncation at
		 * this point and at least one generates a truncated
		 * packet, RFC 1334 section 2.2.2 clearly states that
		 * both AACK and ANAK are at least 5 bytes long.
		 */
		if (len < 5)
			goto trunc;
		if (length - (p - p0) < 1)
			return;
		ND_TCHECK(*p);
		msg_len = *p;		/* Msg-Length */
		p++;
		if (length - (p - p0) < msg_len)
			return;
		ND_PRINT((ndo, ", Msg "));
		for (i = 0; i< msg_len; i++) {
			ND_TCHECK(*p);
			safeputchar(ndo, *p++);
		}
		break;
	}
	return;

trunc:
	ND_PRINT((ndo, "[|pap]"));
}

/* BAP */
static void
handle_bap(netdissect_options *ndo _U_,
           const u_char *p _U_, int length _U_)
{
	/* XXX: to be supported!! */
}


/* IPCP config options */
static int
print_ipcp_config_options(netdissect_options *ndo,
                          const u_char *p, int length)
{
	int len, opt;
        u_int compproto, ipcomp_subopttotallen, ipcomp_subopt, ipcomp_suboptlen;

	if (length < 2)
		return 0;
	ND_TCHECK2(*p, 2);
	len = p[1];
	opt = p[0];
	if (length < len)
		return 0;
	if (len < 2) {
		ND_PRINT((ndo, "\n\t  %s Option (0x%02x), length %u (length bogus, should be >= 2)",
		       tok2str(ipcpopt_values,"unknown",opt),
		       opt,
		       len));
		return 0;
	}

	ND_PRINT((ndo, "\n\t  %s Option (0x%02x), length %u",
	       tok2str(ipcpopt_values,"unknown",opt),
	       opt,
	       len));

	switch (opt) {
	case IPCPOPT_2ADDR:		/* deprecated */
		if (len != 10) {
			ND_PRINT((ndo, " (length bogus, should be = 10)"));
			return len;
		}
		ND_TCHECK2(*(p + 6), 4);
		ND_PRINT((ndo, ": src %s, dst %s",
		       ipaddr_string(ndo, p + 2),
		       ipaddr_string(ndo, p + 6)));
		break;
	case IPCPOPT_IPCOMP:
		if (len < 4) {
			ND_PRINT((ndo, " (length bogus, should be >= 4)"));
			return 0;
		}
		ND_TCHECK_16BITS(p+2);
		compproto = EXTRACT_16BITS(p+2);

		ND_PRINT((ndo, ": %s (0x%02x):",
		          tok2str(ipcpopt_compproto_values, "Unknown", compproto),
		          compproto));

		switch (compproto) {
                case PPP_VJC:
			/* XXX: VJ-Comp parameters should be decoded */
                        break;
                case IPCPOPT_IPCOMP_HDRCOMP:
                        if (len < IPCPOPT_IPCOMP_MINLEN) {
                        	ND_PRINT((ndo, " (length bogus, should be >= %u)",
                        		IPCPOPT_IPCOMP_MINLEN));
                        	return 0;
                        }

                        ND_TCHECK2(*(p + 2), IPCPOPT_IPCOMP_MINLEN);
                        ND_PRINT((ndo, "\n\t    TCP Space %u, non-TCP Space %u" \
                               ", maxPeriod %u, maxTime %u, maxHdr %u",
                               EXTRACT_16BITS(p+4),
                               EXTRACT_16BITS(p+6),
                               EXTRACT_16BITS(p+8),
                               EXTRACT_16BITS(p+10),
                               EXTRACT_16BITS(p+12)));

                        /* suboptions present ? */
                        if (len > IPCPOPT_IPCOMP_MINLEN) {
                                ipcomp_subopttotallen = len - IPCPOPT_IPCOMP_MINLEN;
                                p += IPCPOPT_IPCOMP_MINLEN;

                                ND_PRINT((ndo, "\n\t      Suboptions, length %u", ipcomp_subopttotallen));

                                while (ipcomp_subopttotallen >= 2) {
                                        ND_TCHECK2(*p, 2);
                                        ipcomp_subopt = *p;
                                        ipcomp_suboptlen = *(p+1);

                                        /* sanity check */
                                        if (ipcomp_subopt == 0 ||
                                            ipcomp_suboptlen == 0 )
                                                break;

                                        /* XXX: just display the suboptions for now */
                                        ND_PRINT((ndo, "\n\t\t%s Suboption #%u, length %u",
                                               tok2str(ipcpopt_compproto_subopt_values,
                                                       "Unknown",
                                                       ipcomp_subopt),
                                               ipcomp_subopt,
                                               ipcomp_suboptlen));

                                        ipcomp_subopttotallen -= ipcomp_suboptlen;
                                        p += ipcomp_suboptlen;
                                }
                        }
                        break;
                default:
                        break;
		}
		break;

	case IPCPOPT_ADDR:     /* those options share the same format - fall through */
	case IPCPOPT_MOBILE4:
	case IPCPOPT_PRIDNS:
	case IPCPOPT_PRINBNS:
	case IPCPOPT_SECDNS:
	case IPCPOPT_SECNBNS:
		if (len != 6) {
			ND_PRINT((ndo, " (length bogus, should be = 6)"));
			return 0;
		}
		ND_TCHECK2(*(p + 2), 4);
		ND_PRINT((ndo, ": %s", ipaddr_string(ndo, p + 2)));
		break;
	default:
		/*
		 * Unknown option; dump it as raw bytes now if we're
		 * not going to do so below.
		 */
		if (ndo->ndo_vflag < 2)
			print_unknown_data(ndo, &p[2], "\n\t    ", len - 2);
		break;
	}
	if (ndo->ndo_vflag > 1)
		print_unknown_data(ndo, &p[2], "\n\t    ", len - 2); /* exclude TLV header */
	return len;

trunc:
	ND_PRINT((ndo, "[|ipcp]"));
	return 0;
}

/* IP6CP config options */
static int
print_ip6cp_config_options(netdissect_options *ndo,
                           const u_char *p, int length)
{
	int len, opt;

	if (length < 2)
		return 0;
	ND_TCHECK2(*p, 2);
	len = p[1];
	opt = p[0];
	if (length < len)
		return 0;
	if (len < 2) {
		ND_PRINT((ndo, "\n\t  %s Option (0x%02x), length %u (length bogus, should be >= 2)",
		       tok2str(ip6cpopt_values,"unknown",opt),
		       opt,
		       len));
		return 0;
	}

	ND_PRINT((ndo, "\n\t  %s Option (0x%02x), length %u",
	       tok2str(ip6cpopt_values,"unknown",opt),
	       opt,
	       len));

	switch (opt) {
	case IP6CP_IFID:
		if (len != 10) {
			ND_PRINT((ndo, " (length bogus, should be = 10)"));
			return len;
		}
		ND_TCHECK2(*(p + 2), 8);
		ND_PRINT((ndo, ": %04x:%04x:%04x:%04x",
		       EXTRACT_16BITS(p + 2),
		       EXTRACT_16BITS(p + 4),
		       EXTRACT_16BITS(p + 6),
		       EXTRACT_16BITS(p + 8)));
		break;
	default:
		/*
		 * Unknown option; dump it as raw bytes now if we're
		 * not going to do so below.
		 */
		if (ndo->ndo_vflag < 2)
			print_unknown_data(ndo, &p[2], "\n\t    ", len - 2);
		break;
	}
	if (ndo->ndo_vflag > 1)
		print_unknown_data(ndo, &p[2], "\n\t    ", len - 2); /* exclude TLV header */

	return len;

trunc:
	ND_PRINT((ndo, "[|ip6cp]"));
	return 0;
}


/* CCP config options */
static int
print_ccp_config_options(netdissect_options *ndo,
                         const u_char *p, int length)
{
	int len, opt;

	if (length < 2)
		return 0;
	ND_TCHECK2(*p, 2);
	len = p[1];
	opt = p[0];
	if (length < len)
		return 0;
	if (len < 2) {
		ND_PRINT((ndo, "\n\t  %s Option (0x%02x), length %u (length bogus, should be >= 2)",
		          tok2str(ccpconfopts_values, "Unknown", opt),
		          opt,
		          len));
		return 0;
	}

	ND_PRINT((ndo, "\n\t  %s Option (0x%02x), length %u",
	          tok2str(ccpconfopts_values, "Unknown", opt),
	          opt,
	          len));

	switch (opt) {
	case CCPOPT_BSDCOMP:
		if (len < 3) {
			ND_PRINT((ndo, " (length bogus, should be >= 3)"));
			return len;
		}
		ND_TCHECK(p[2]);
		ND_PRINT((ndo, ": Version: %u, Dictionary Bits: %u",
			p[2] >> 5, p[2] & 0x1f));
		break;
	case CCPOPT_MVRCA:
		if (len < 4) {
			ND_PRINT((ndo, " (length bogus, should be >= 4)"));
			return len;
		}
		ND_TCHECK(p[3]);
		ND_PRINT((ndo, ": Features: %u, PxP: %s, History: %u, #CTX-ID: %u",
				(p[2] & 0xc0) >> 6,
				(p[2] & 0x20) ? "Enabled" : "Disabled",
				p[2] & 0x1f, p[3]));
		break;
	case CCPOPT_DEFLATE:
		if (len < 4) {
			ND_PRINT((ndo, " (length bogus, should be >= 4)"));
			return len;
		}
		ND_TCHECK(p[3]);
		ND_PRINT((ndo, ": Window: %uK, Method: %s (0x%x), MBZ: %u, CHK: %u",
			(p[2] & 0xf0) >> 4,
			((p[2] & 0x0f) == 8) ? "zlib" : "unknown",
			p[2] & 0x0f, (p[3] & 0xfc) >> 2, p[3] & 0x03));
		break;

/* XXX: to be supported */
#if 0
	case CCPOPT_OUI:
	case CCPOPT_PRED1:
	case CCPOPT_PRED2:
	case CCPOPT_PJUMP:
	case CCPOPT_HPPPC:
	case CCPOPT_STACLZS:
	case CCPOPT_MPPC:
	case CCPOPT_GFZA:
	case CCPOPT_V42BIS:
	case CCPOPT_LZSDCP:
	case CCPOPT_DEC:
	case CCPOPT_RESV:
		break;
#endif
	default:
		/*
		 * Unknown option; dump it as raw bytes now if we're
		 * not going to do so below.
		 */
		if (ndo->ndo_vflag < 2)
			print_unknown_data(ndo, &p[2], "\n\t    ", len - 2);
		break;
	}
	if (ndo->ndo_vflag > 1)
		print_unknown_data(ndo, &p[2], "\n\t    ", len - 2); /* exclude TLV header */

	return len;

trunc:
	ND_PRINT((ndo, "[|ccp]"));
	return 0;
}

/* BACP config options */
static int
print_bacp_config_options(netdissect_options *ndo,
                          const u_char *p, int length)
{
	int len, opt;

	if (length < 2)
		return 0;
	ND_TCHECK2(*p, 2);
	len = p[1];
	opt = p[0];
	if (length < len)
		return 0;
	if (len < 2) {
		ND_PRINT((ndo, "\n\t  %s Option (0x%02x), length %u (length bogus, should be >= 2)",
		          tok2str(bacconfopts_values, "Unknown", opt),
		          opt,
		          len));
		return 0;
	}

	ND_PRINT((ndo, "\n\t  %s Option (0x%02x), length %u",
	          tok2str(bacconfopts_values, "Unknown", opt),
	          opt,
	          len));

	switch (opt) {
	case BACPOPT_FPEER:
		if (len != 6) {
			ND_PRINT((ndo, " (length bogus, should be = 6)"));
			return len;
		}
		ND_TCHECK_32BITS(p + 2);
		ND_PRINT((ndo, ": Magic-Num 0x%08x", EXTRACT_32BITS(p + 2)));
		break;
	default:
		/*
		 * Unknown option; dump it as raw bytes now if we're
		 * not going to do so below.
		 */
		if (ndo->ndo_vflag < 2)
			print_unknown_data(ndo, &p[2], "\n\t    ", len - 2);
		break;
	}
	if (ndo->ndo_vflag > 1)
		print_unknown_data(ndo, &p[2], "\n\t    ", len - 2); /* exclude TLV header */

	return len;

trunc:
	ND_PRINT((ndo, "[|bacp]"));
	return 0;
}

static void
ppp_hdlc(netdissect_options *ndo,
         const u_char *p, int length)
{
	u_char *b, *t, c;
	const u_char *s;
	int i, proto;
	const void *se;

        if (length <= 0)
                return;

	b = (u_char *)malloc(length);
	if (b == NULL)
		return;

	/*
	 * Unescape all the data into a temporary, private, buffer.
	 * Do this so that we dont overwrite the original packet
	 * contents.
	 */
	for (s = p, t = b, i = length; i > 0 && ND_TTEST(*s); i--) {
		c = *s++;
		if (c == 0x7d) {
			if (i <= 1 || !ND_TTEST(*s))
				break;
			i--;
			c = *s++ ^ 0x20;
		}
		*t++ = c;
	}

	se = ndo->ndo_snapend;
	ndo->ndo_snapend = t;
	length = t - b;

        /* now lets guess about the payload codepoint format */
        if (length < 1)
                goto trunc;
        proto = *b; /* start with a one-octet codepoint guess */

        switch (proto) {
        case PPP_IP:
		ip_print(ndo, b + 1, length - 1);
		goto cleanup;
        case PPP_IPV6:
		ip6_print(ndo, b + 1, length - 1);
		goto cleanup;
        default: /* no luck - try next guess */
		break;
        }

        if (length < 2)
                goto trunc;
        proto = EXTRACT_16BITS(b); /* next guess - load two octets */

        switch (proto) {
        case (PPP_ADDRESS << 8 | PPP_CONTROL): /* looks like a PPP frame */
            if (length < 4)
                goto trunc;
            proto = EXTRACT_16BITS(b+2); /* load the PPP proto-id */
            handle_ppp(ndo, proto, b + 4, length - 4);
            break;
        default: /* last guess - proto must be a PPP proto-id */
            handle_ppp(ndo, proto, b + 2, length - 2);
            break;
        }

cleanup:
	ndo->ndo_snapend = se;
	free(b);
        return;

trunc:
	ndo->ndo_snapend = se;
	free(b);
	ND_PRINT((ndo, "[|ppp]"));
}


/* PPP */
static void
handle_ppp(netdissect_options *ndo,
           u_int proto, const u_char *p, int length)
{
	if ((proto & 0xff00) == 0x7e00) { /* is this an escape code ? */
		ppp_hdlc(ndo, p - 1, length);
		return;
	}

	switch (proto) {
	case PPP_LCP: /* fall through */
	case PPP_IPCP:
	case PPP_OSICP:
	case PPP_MPLSCP:
	case PPP_IPV6CP:
	case PPP_CCP:
	case PPP_BACP:
		handle_ctrl_proto(ndo, proto, p, length);
		break;
	case PPP_ML:
		handle_mlppp(ndo, p, length);
		break;
	case PPP_CHAP:
		handle_chap(ndo, p, length);
		break;
	case PPP_PAP:
		handle_pap(ndo, p, length);
		break;
	case PPP_BAP:		/* XXX: not yet completed */
		handle_bap(ndo, p, length);
		break;
	case ETHERTYPE_IP:	/*XXX*/
        case PPP_VJNC:
	case PPP_IP:
		ip_print(ndo, p, length);
		break;
	case ETHERTYPE_IPV6:	/*XXX*/
	case PPP_IPV6:
		ip6_print(ndo, p, length);
		break;
	case ETHERTYPE_IPX:	/*XXX*/
	case PPP_IPX:
		ipx_print(ndo, p, length);
		break;
	case PPP_OSI:
		isoclns_print(ndo, p, length);
		break;
	case PPP_MPLS_UCAST:
	case PPP_MPLS_MCAST:
		mpls_print(ndo, p, length);
		break;
	case PPP_COMP:
		ND_PRINT((ndo, "compressed PPP data"));
		break;
	default:
		ND_PRINT((ndo, "%s ", tok2str(ppptype2str, "unknown PPP protocol (0x%04x)", proto)));
		print_unknown_data(ndo, p, "\n\t", length);
		break;
	}
}

/* Standard PPP printer */
u_int
ppp_print(netdissect_options *ndo,
          register const u_char *p, u_int length)
{
	u_int proto,ppp_header;
        u_int olen = length; /* _o_riginal length */
	u_int hdr_len = 0;

	/*
	 * Here, we assume that p points to the Address and Control
	 * field (if they present).
	 */
	if (length < 2)
		goto trunc;
	ND_TCHECK2(*p, 2);
        ppp_header = EXTRACT_16BITS(p);

        switch(ppp_header) {
        case (PPP_WITHDIRECTION_IN  << 8 | PPP_CONTROL):
            if (ndo->ndo_eflag) ND_PRINT((ndo, "In  "));
            p += 2;
            length -= 2;
            hdr_len += 2;
            break;
        case (PPP_WITHDIRECTION_OUT << 8 | PPP_CONTROL):
            if (ndo->ndo_eflag) ND_PRINT((ndo, "Out "));
            p += 2;
            length -= 2;
            hdr_len += 2;
            break;
        case (PPP_ADDRESS << 8 | PPP_CONTROL):
            p += 2;			/* ACFC not used */
            length -= 2;
            hdr_len += 2;
            break;

        default:
            break;
        }

	if (length < 2)
		goto trunc;
	ND_TCHECK(*p);
	if (*p % 2) {
		proto = *p;		/* PFC is used */
		p++;
		length--;
		hdr_len++;
	} else {
		ND_TCHECK2(*p, 2);
		proto = EXTRACT_16BITS(p);
		p += 2;
		length -= 2;
		hdr_len += 2;
	}

	if (ndo->ndo_eflag)
		ND_PRINT((ndo, "%s (0x%04x), length %u: ",
		          tok2str(ppptype2str, "unknown", proto),
		          proto,
		          olen));

	handle_ppp(ndo, proto, p, length);
	return (hdr_len);
trunc:
	ND_PRINT((ndo, "[|ppp]"));
	return (0);
}


/* PPP I/F printer */
u_int
ppp_if_print(netdissect_options *ndo,
             const struct pcap_pkthdr *h, register const u_char *p)
{
	register u_int length = h->len;
	register u_int caplen = h->caplen;

	if (caplen < PPP_HDRLEN) {
		ND_PRINT((ndo, "[|ppp]"));
		return (caplen);
	}

#if 0
	/*
	 * XXX: seems to assume that there are 2 octets prepended to an
	 * actual PPP frame. The 1st octet looks like Input/Output flag
	 * while 2nd octet is unknown, at least to me
	 * (mshindo@mshindo.net).
	 *
	 * That was what the original tcpdump code did.
	 *
	 * FreeBSD's "if_ppp.c" *does* set the first octet to 1 for outbound
	 * packets and 0 for inbound packets - but only if the
	 * protocol field has the 0x8000 bit set (i.e., it's a network
	 * control protocol); it does so before running the packet through
	 * "bpf_filter" to see if it should be discarded, and to see
	 * if we should update the time we sent the most recent packet...
	 *
	 * ...but it puts the original address field back after doing
	 * so.
	 *
	 * NetBSD's "if_ppp.c" doesn't set the first octet in that fashion.
	 *
	 * I don't know if any PPP implementation handed up to a BPF
	 * device packets with the first octet being 1 for outbound and
	 * 0 for inbound packets, so I (guy@alum.mit.edu) don't know
	 * whether that ever needs to be checked or not.
	 *
	 * Note that NetBSD has a DLT_PPP_SERIAL, which it uses for PPP,
	 * and its tcpdump appears to assume that the frame always
	 * begins with an address field and a control field, and that
	 * the address field might be 0x0f or 0x8f, for Cisco
	 * point-to-point with HDLC framing as per section 4.3.1 of RFC
	 * 1547, as well as 0xff, for PPP in HDLC-like framing as per
	 * RFC 1662.
	 *
	 * (Is the Cisco framing in question what DLT_C_HDLC, in
	 * BSD/OS, is?)
	 */
	if (ndo->ndo_eflag)
		ND_PRINT((ndo, "%c %4d %02x ", p[0] ? 'O' : 'I', length, p[1]));
#endif

	ppp_print(ndo, p, length);

	return (0);
}

/*
 * PPP I/F printer to use if we know that RFC 1662-style PPP in HDLC-like
 * framing, or Cisco PPP with HDLC framing as per section 4.3.1 of RFC 1547,
 * is being used (i.e., we don't check for PPP_ADDRESS and PPP_CONTROL,
 * discard them *if* those are the first two octets, and parse the remaining
 * packet as a PPP packet, as "ppp_print()" does).
 *
 * This handles, for example, DLT_PPP_SERIAL in NetBSD.
 */
u_int
ppp_hdlc_if_print(netdissect_options *ndo,
                  const struct pcap_pkthdr *h, register const u_char *p)
{
	register u_int length = h->len;
	register u_int caplen = h->caplen;
	u_int proto;
	u_int hdrlen = 0;

	if (caplen < 2) {
		ND_PRINT((ndo, "[|ppp]"));
		return (caplen);
	}

	switch (p[0]) {

	case PPP_ADDRESS:
		if (caplen < 4) {
			ND_PRINT((ndo, "[|ppp]"));
			return (caplen);
		}

		if (ndo->ndo_eflag)
			ND_PRINT((ndo, "%02x %02x %d ", p[0], p[1], length));
		p += 2;
		length -= 2;
		hdrlen += 2;

		proto = EXTRACT_16BITS(p);
		p += 2;
		length -= 2;
		hdrlen += 2;
		ND_PRINT((ndo, "%s: ", tok2str(ppptype2str, "unknown PPP protocol (0x%04x)", proto)));

		handle_ppp(ndo, proto, p, length);
		break;

	case CHDLC_UNICAST:
	case CHDLC_BCAST:
		return (chdlc_if_print(ndo, h, p));

	default:
		if (caplen < 4) {
			ND_PRINT((ndo, "[|ppp]"));
			return (caplen);
		}

		if (ndo->ndo_eflag)
			ND_PRINT((ndo, "%02x %02x %d ", p[0], p[1], length));
		p += 2;
		hdrlen += 2;

		/*
		 * XXX - NetBSD's "ppp_netbsd_serial_if_print()" treats
		 * the next two octets as an Ethernet type; does that
		 * ever happen?
		 */
		ND_PRINT((ndo, "unknown addr %02x; ctrl %02x", p[0], p[1]));
		break;
	}

	return (hdrlen);
}

#define PPP_BSDI_HDRLEN 24

/* BSD/OS specific PPP printer */
u_int
ppp_bsdos_if_print(netdissect_options *ndo _U_,
                   const struct pcap_pkthdr *h _U_, register const u_char *p _U_)
{
	register int hdrlength;
#ifdef __bsdi__
	register u_int length = h->len;
	register u_int caplen = h->caplen;
	uint16_t ptype;
	const u_char *q;
	int i;

	if (caplen < PPP_BSDI_HDRLEN) {
		ND_PRINT((ndo, "[|ppp]"));
		return (caplen)
	}

	hdrlength = 0;

#if 0
	if (p[0] == PPP_ADDRESS && p[1] == PPP_CONTROL) {
		if (ndo->ndo_eflag)
			ND_PRINT((ndo, "%02x %02x ", p[0], p[1]));
		p += 2;
		hdrlength = 2;
	}

	if (ndo->ndo_eflag)
		ND_PRINT((ndo, "%d ", length));
	/* Retrieve the protocol type */
	if (*p & 01) {
		/* Compressed protocol field */
		ptype = *p;
		if (ndo->ndo_eflag)
			ND_PRINT((ndo, "%02x ", ptype));
		p++;
		hdrlength += 1;
	} else {
		/* Un-compressed protocol field */
		ptype = EXTRACT_16BITS(p);
		if (ndo->ndo_eflag)
			ND_PRINT((ndo, "%04x ", ptype));
		p += 2;
		hdrlength += 2;
	}
#else
	ptype = 0;	/*XXX*/
	if (ndo->ndo_eflag)
		ND_PRINT((ndo, "%c ", p[SLC_DIR] ? 'O' : 'I'));
	if (p[SLC_LLHL]) {
		/* link level header */
		struct ppp_header *ph;

		q = p + SLC_BPFHDRLEN;
		ph = (struct ppp_header *)q;
		if (ph->phdr_addr == PPP_ADDRESS
		 && ph->phdr_ctl == PPP_CONTROL) {
			if (ndo->ndo_eflag)
				ND_PRINT((ndo, "%02x %02x ", q[0], q[1]));
			ptype = EXTRACT_16BITS(&ph->phdr_type);
			if (ndo->ndo_eflag && (ptype == PPP_VJC || ptype == PPP_VJNC)) {
				ND_PRINT((ndo, "%s ", tok2str(ppptype2str,
						"proto-#%d", ptype)));
			}
		} else {
			if (ndo->ndo_eflag) {
				ND_PRINT((ndo, "LLH=["));
				for (i = 0; i < p[SLC_LLHL]; i++)
					ND_PRINT((ndo, "%02x", q[i]));
				ND_PRINT((ndo, "] "));
			}
		}
	}
	if (ndo->ndo_eflag)
		ND_PRINT((ndo, "%d ", length));
	if (p[SLC_CHL]) {
		q = p + SLC_BPFHDRLEN + p[SLC_LLHL];

		switch (ptype) {
		case PPP_VJC:
			ptype = vjc_print(ndo, q, ptype);
			hdrlength = PPP_BSDI_HDRLEN;
			p += hdrlength;
			switch (ptype) {
			case PPP_IP:
				ip_print(ndo, p, length);
				break;
			case PPP_IPV6:
				ip6_print(ndo, p, length);
				break;
			case PPP_MPLS_UCAST:
			case PPP_MPLS_MCAST:
				mpls_print(ndo, p, length);
				break;
			}
			goto printx;
		case PPP_VJNC:
			ptype = vjc_print(ndo, q, ptype);
			hdrlength = PPP_BSDI_HDRLEN;
			p += hdrlength;
			switch (ptype) {
			case PPP_IP:
				ip_print(ndo, p, length);
				break;
			case PPP_IPV6:
				ip6_print(ndo, p, length);
				break;
			case PPP_MPLS_UCAST:
			case PPP_MPLS_MCAST:
				mpls_print(ndo, p, length);
				break;
			}
			goto printx;
		default:
			if (ndo->ndo_eflag) {
				ND_PRINT((ndo, "CH=["));
				for (i = 0; i < p[SLC_LLHL]; i++)
					ND_PRINT((ndo, "%02x", q[i]));
				ND_PRINT((ndo, "] "));
			}
			break;
		}
	}

	hdrlength = PPP_BSDI_HDRLEN;
#endif

	length -= hdrlength;
	p += hdrlength;

	switch (ptype) {
	case PPP_IP:
		ip_print(p, length);
		break;
	case PPP_IPV6:
		ip6_print(ndo, p, length);
		break;
	case PPP_MPLS_UCAST:
	case PPP_MPLS_MCAST:
		mpls_print(ndo, p, length);
		break;
	default:
		ND_PRINT((ndo, "%s ", tok2str(ppptype2str, "unknown PPP protocol (0x%04x)", ptype)));
	}

printx:
#else /* __bsdi */
	hdrlength = 0;
#endif /* __bsdi__ */
	return (hdrlength);
}


/*
 * Local Variables:
 * c-style: whitesmith
 * c-basic-offset: 8
 * End:
 */
