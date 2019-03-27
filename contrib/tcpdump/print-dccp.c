/*
 * Copyright (C) Arnaldo Carvalho de Melo 2004
 * Copyright (C) Ian McDonald 2005
 * Copyright (C) Yoshifumi Nishida 2005
 *
 * This software may be distributed either under the terms of the
 * BSD-style license that accompanies tcpdump or the GNU GPL version 2
 */

/* \summary: Datagram Congestion Control Protocol (DCCP) printer */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <netdissect-stdinc.h>

#include <stdio.h>
#include <string.h>

#include "netdissect.h"
#include "addrtoname.h"
#include "extract.h"
#include "ip.h"
#include "ip6.h"
#include "ipproto.h"

/* RFC4340: Datagram Congestion Control Protocol (DCCP) */

/**
 * struct dccp_hdr - generic part of DCCP packet header, with a 24-bit
 * sequence number
 *
 * @dccph_sport - Relevant port on the endpoint that sent this packet
 * @dccph_dport - Relevant port on the other endpoint
 * @dccph_doff - Data Offset from the start of the DCCP header, in 32-bit words
 * @dccph_ccval - Used by the HC-Sender CCID
 * @dccph_cscov - Parts of the packet that are covered by the Checksum field
 * @dccph_checksum - Internet checksum, depends on dccph_cscov
 * @dccph_x - 0 = 24 bit sequence number, 1 = 48
 * @dccph_type - packet type, see DCCP_PKT_ prefixed macros
 * @dccph_seq - 24-bit sequence number
 */
struct dccp_hdr {
	uint16_t	dccph_sport,
			dccph_dport;
	uint8_t		dccph_doff;
	uint8_t		dccph_ccval_cscov;
	uint16_t	dccph_checksum;
	uint8_t		dccph_xtr;
	uint8_t		dccph_seq[3];
} UNALIGNED;

/**
 * struct dccp_hdr_ext - generic part of DCCP packet header, with a 48-bit
 * sequence number
 *
 * @dccph_sport - Relevant port on the endpoint that sent this packet
 * @dccph_dport - Relevant port on the other endpoint
 * @dccph_doff - Data Offset from the start of the DCCP header, in 32-bit words
 * @dccph_ccval - Used by the HC-Sender CCID
 * @dccph_cscov - Parts of the packet that are covered by the Checksum field
 * @dccph_checksum - Internet checksum, depends on dccph_cscov
 * @dccph_x - 0 = 24 bit sequence number, 1 = 48
 * @dccph_type - packet type, see DCCP_PKT_ prefixed macros
 * @dccph_seq - 48-bit sequence number
 */
struct dccp_hdr_ext {
	uint16_t	dccph_sport,
			dccph_dport;
	uint8_t		dccph_doff;
	uint8_t		dccph_ccval_cscov;
	uint16_t	dccph_checksum;
	uint8_t		dccph_xtr;
	uint8_t		reserved;
	uint8_t		dccph_seq[6];
} UNALIGNED;

#define DCCPH_CCVAL(dh)	(((dh)->dccph_ccval_cscov >> 4) & 0xF)
#define DCCPH_CSCOV(dh)	(((dh)->dccph_ccval_cscov) & 0xF)

#define DCCPH_X(dh)	((dh)->dccph_xtr & 1)
#define DCCPH_TYPE(dh)	(((dh)->dccph_xtr >> 1) & 0xF)

/**
 * struct dccp_hdr_request - Conection initiation request header
 *
 * @dccph_req_service - Service to which the client app wants to connect
 */
struct dccp_hdr_request {
	uint32_t	dccph_req_service;
} UNALIGNED;

/**
 * struct dccp_hdr_response - Conection initiation response header
 *
 * @dccph_resp_ack - 48 bit ack number, contains GSR
 * @dccph_resp_service - Echoes the Service Code on a received DCCP-Request
 */
struct dccp_hdr_response {
	uint8_t				dccph_resp_ack[8];	/* always 8 bytes */
	uint32_t			dccph_resp_service;
} UNALIGNED;

/**
 * struct dccp_hdr_reset - Unconditionally shut down a connection
 *
 * @dccph_resp_ack - 48 bit ack number
 * @dccph_reset_service - Echoes the Service Code on a received DCCP-Request
 */
struct dccp_hdr_reset {
	uint8_t				dccph_reset_ack[8];	/* always 8 bytes */
	uint8_t				dccph_reset_code,
					dccph_reset_data[3];
} UNALIGNED;

enum dccp_pkt_type {
	DCCP_PKT_REQUEST = 0,
	DCCP_PKT_RESPONSE,
	DCCP_PKT_DATA,
	DCCP_PKT_ACK,
	DCCP_PKT_DATAACK,
	DCCP_PKT_CLOSEREQ,
	DCCP_PKT_CLOSE,
	DCCP_PKT_RESET,
	DCCP_PKT_SYNC,
	DCCP_PKT_SYNCACK
};

static const struct tok dccp_pkt_type_str[] = {
	{ DCCP_PKT_REQUEST, "DCCP-Request" },
	{ DCCP_PKT_RESPONSE, "DCCP-Response" },
	{ DCCP_PKT_DATA, "DCCP-Data" },
	{ DCCP_PKT_ACK, "DCCP-Ack" },
	{ DCCP_PKT_DATAACK, "DCCP-DataAck" },
	{ DCCP_PKT_CLOSEREQ, "DCCP-CloseReq" },
	{ DCCP_PKT_CLOSE, "DCCP-Close" },
	{ DCCP_PKT_RESET, "DCCP-Reset" },
	{ DCCP_PKT_SYNC, "DCCP-Sync" },
	{ DCCP_PKT_SYNCACK, "DCCP-SyncAck" },
	{ 0, NULL}
};

enum dccp_reset_codes {
	DCCP_RESET_CODE_UNSPECIFIED = 0,
	DCCP_RESET_CODE_CLOSED,
	DCCP_RESET_CODE_ABORTED,
	DCCP_RESET_CODE_NO_CONNECTION,
	DCCP_RESET_CODE_PACKET_ERROR,
	DCCP_RESET_CODE_OPTION_ERROR,
	DCCP_RESET_CODE_MANDATORY_ERROR,
	DCCP_RESET_CODE_CONNECTION_REFUSED,
	DCCP_RESET_CODE_BAD_SERVICE_CODE,
	DCCP_RESET_CODE_TOO_BUSY,
	DCCP_RESET_CODE_BAD_INIT_COOKIE,
	DCCP_RESET_CODE_AGGRESSION_PENALTY,
	__DCCP_RESET_CODE_LAST
};

static const char tstr[] = "[|dccp]";

static const char *dccp_reset_codes[] = {
	"unspecified",
	"closed",
	"aborted",
	"no_connection",
	"packet_error",
	"option_error",
	"mandatory_error",
	"connection_refused",
	"bad_service_code",
	"too_busy",
	"bad_init_cookie",
	"aggression_penalty",
};

static const char *dccp_feature_nums[] = {
	"reserved",
	"ccid",
	"allow_short_seqno",
	"sequence_window",
	"ecn_incapable",
	"ack_ratio",
	"send_ack_vector",
	"send_ndp_count",
	"minimum checksum coverage",
	"check data checksum",
};

static inline u_int dccp_csum_coverage(const struct dccp_hdr* dh, u_int len)
{
	u_int cov;

	if (DCCPH_CSCOV(dh) == 0)
		return len;
	cov = (dh->dccph_doff + DCCPH_CSCOV(dh) - 1) * sizeof(uint32_t);
	return (cov > len)? len : cov;
}

static int dccp_cksum(netdissect_options *ndo, const struct ip *ip,
	const struct dccp_hdr *dh, u_int len)
{
	return nextproto4_cksum(ndo, ip, (const uint8_t *)(const void *)dh, len,
				dccp_csum_coverage(dh, len), IPPROTO_DCCP);
}

static int dccp6_cksum(netdissect_options *ndo, const struct ip6_hdr *ip6,
	const struct dccp_hdr *dh, u_int len)
{
	return nextproto6_cksum(ndo, ip6, (const uint8_t *)(const void *)dh, len,
				dccp_csum_coverage(dh, len), IPPROTO_DCCP);
}

static const char *dccp_reset_code(uint8_t code)
{
	if (code >= __DCCP_RESET_CODE_LAST)
		return "invalid";
	return dccp_reset_codes[code];
}

static uint64_t dccp_seqno(const u_char *bp)
{
	const struct dccp_hdr *dh = (const struct dccp_hdr *)bp;
	uint64_t seqno;

	if (DCCPH_X(dh) != 0) {
		const struct dccp_hdr_ext *dhx = (const struct dccp_hdr_ext *)bp;
		seqno = EXTRACT_48BITS(dhx->dccph_seq);
	} else {
		seqno = EXTRACT_24BITS(dh->dccph_seq);
	}

	return seqno;
}

static inline unsigned int dccp_basic_hdr_len(const struct dccp_hdr *dh)
{
	return DCCPH_X(dh) ? sizeof(struct dccp_hdr_ext) : sizeof(struct dccp_hdr);
}

static void dccp_print_ack_no(netdissect_options *ndo, const u_char *bp)
{
	const struct dccp_hdr *dh = (const struct dccp_hdr *)bp;
	const u_char *ackp = bp + dccp_basic_hdr_len(dh);
	uint64_t ackno;

	if (DCCPH_X(dh) != 0) {
		ND_TCHECK2(*ackp, 8);
		ackno = EXTRACT_48BITS(ackp + 2);
	} else {
		ND_TCHECK2(*ackp, 4);
		ackno = EXTRACT_24BITS(ackp + 1);
	}

	ND_PRINT((ndo, "(ack=%" PRIu64 ") ", ackno));
trunc:
	return;
}

static int dccp_print_option(netdissect_options *, const u_char *, u_int);

/**
 * dccp_print - show dccp packet
 * @bp - beginning of dccp packet
 * @data2 - beginning of enclosing
 * @len - lenght of ip packet
 */
void dccp_print(netdissect_options *ndo, const u_char *bp, const u_char *data2,
		u_int len)
{
	const struct dccp_hdr *dh;
	const struct ip *ip;
	const struct ip6_hdr *ip6;
	const u_char *cp;
	u_short sport, dport;
	u_int hlen;
	u_int fixed_hdrlen;
	uint8_t	dccph_type;

	dh = (const struct dccp_hdr *)bp;

	ip = (const struct ip *)data2;
	if (IP_V(ip) == 6)
		ip6 = (const struct ip6_hdr *)data2;
	else
		ip6 = NULL;

	/* make sure we have enough data to look at the X bit */
	cp = (const u_char *)(dh + 1);
	if (cp > ndo->ndo_snapend) {
		ND_PRINT((ndo, "[Invalid packet|dccp]"));
		return;
	}
	if (len < sizeof(struct dccp_hdr)) {
		ND_PRINT((ndo, "truncated-dccp - %u bytes missing!",
			  len - (u_int)sizeof(struct dccp_hdr)));
		return;
	}

	/* get the length of the generic header */
	fixed_hdrlen = dccp_basic_hdr_len(dh);
	if (len < fixed_hdrlen) {
		ND_PRINT((ndo, "truncated-dccp - %u bytes missing!",
			  len - fixed_hdrlen));
		return;
	}
	ND_TCHECK2(*dh, fixed_hdrlen);

	sport = EXTRACT_16BITS(&dh->dccph_sport);
	dport = EXTRACT_16BITS(&dh->dccph_dport);
	hlen = dh->dccph_doff * 4;

	if (ip6) {
		ND_PRINT((ndo, "%s.%d > %s.%d: ",
			  ip6addr_string(ndo, &ip6->ip6_src), sport,
			  ip6addr_string(ndo, &ip6->ip6_dst), dport));
	} else {
		ND_PRINT((ndo, "%s.%d > %s.%d: ",
			  ipaddr_string(ndo, &ip->ip_src), sport,
			  ipaddr_string(ndo, &ip->ip_dst), dport));
	}

	ND_PRINT((ndo, "DCCP"));

	if (ndo->ndo_qflag) {
		ND_PRINT((ndo, " %d", len - hlen));
		if (hlen > len) {
			ND_PRINT((ndo, " [bad hdr length %u - too long, > %u]",
				  hlen, len));
		}
		return;
	}

	/* other variables in generic header */
	if (ndo->ndo_vflag) {
		ND_PRINT((ndo, " (CCVal %d, CsCov %d, ", DCCPH_CCVAL(dh), DCCPH_CSCOV(dh)));
	}

	/* checksum calculation */
	if (ndo->ndo_vflag && ND_TTEST2(bp[0], len)) {
		uint16_t sum = 0, dccp_sum;

		dccp_sum = EXTRACT_16BITS(&dh->dccph_checksum);
		ND_PRINT((ndo, "cksum 0x%04x ", dccp_sum));
		if (IP_V(ip) == 4)
			sum = dccp_cksum(ndo, ip, dh, len);
		else if (IP_V(ip) == 6)
			sum = dccp6_cksum(ndo, ip6, dh, len);
		if (sum != 0)
			ND_PRINT((ndo, "(incorrect -> 0x%04x)",in_cksum_shouldbe(dccp_sum, sum)));
		else
			ND_PRINT((ndo, "(correct)"));
	}

	if (ndo->ndo_vflag)
		ND_PRINT((ndo, ")"));
	ND_PRINT((ndo, " "));

	dccph_type = DCCPH_TYPE(dh);
	switch (dccph_type) {
	case DCCP_PKT_REQUEST: {
		const struct dccp_hdr_request *dhr =
			(const struct dccp_hdr_request *)(bp + fixed_hdrlen);
		fixed_hdrlen += 4;
		if (len < fixed_hdrlen) {
			ND_PRINT((ndo, "truncated-%s - %u bytes missing!",
				  tok2str(dccp_pkt_type_str, "", dccph_type),
				  len - fixed_hdrlen));
			return;
		}
		ND_TCHECK(*dhr);
		ND_PRINT((ndo, "%s (service=%d) ",
			  tok2str(dccp_pkt_type_str, "", dccph_type),
			  EXTRACT_32BITS(&dhr->dccph_req_service)));
		break;
	}
	case DCCP_PKT_RESPONSE: {
		const struct dccp_hdr_response *dhr =
			(const struct dccp_hdr_response *)(bp + fixed_hdrlen);
		fixed_hdrlen += 12;
		if (len < fixed_hdrlen) {
			ND_PRINT((ndo, "truncated-%s - %u bytes missing!",
				  tok2str(dccp_pkt_type_str, "", dccph_type),
				  len - fixed_hdrlen));
			return;
		}
		ND_TCHECK(*dhr);
		ND_PRINT((ndo, "%s (service=%d) ",
			  tok2str(dccp_pkt_type_str, "", dccph_type),
			  EXTRACT_32BITS(&dhr->dccph_resp_service)));
		break;
	}
	case DCCP_PKT_DATA:
		ND_PRINT((ndo, "%s ", tok2str(dccp_pkt_type_str, "", dccph_type)));
		break;
	case DCCP_PKT_ACK: {
		fixed_hdrlen += 8;
		if (len < fixed_hdrlen) {
			ND_PRINT((ndo, "truncated-%s - %u bytes missing!",
				  tok2str(dccp_pkt_type_str, "", dccph_type),
				  len - fixed_hdrlen));
			return;
		}
		ND_PRINT((ndo, "%s ", tok2str(dccp_pkt_type_str, "", dccph_type)));
		break;
	}
	case DCCP_PKT_DATAACK: {
		fixed_hdrlen += 8;
		if (len < fixed_hdrlen) {
			ND_PRINT((ndo, "truncated-%s - %u bytes missing!",
				  tok2str(dccp_pkt_type_str, "", dccph_type),
				  len - fixed_hdrlen));
			return;
		}
		ND_PRINT((ndo, "%s ", tok2str(dccp_pkt_type_str, "", dccph_type)));
		break;
	}
	case DCCP_PKT_CLOSEREQ:
		fixed_hdrlen += 8;
		if (len < fixed_hdrlen) {
			ND_PRINT((ndo, "truncated-%s - %u bytes missing!",
				  tok2str(dccp_pkt_type_str, "", dccph_type),
				  len - fixed_hdrlen));
			return;
		}
		ND_PRINT((ndo, "%s ", tok2str(dccp_pkt_type_str, "", dccph_type)));
		break;
	case DCCP_PKT_CLOSE:
		fixed_hdrlen += 8;
		if (len < fixed_hdrlen) {
			ND_PRINT((ndo, "truncated-%s - %u bytes missing!",
				  tok2str(dccp_pkt_type_str, "", dccph_type),
				  len - fixed_hdrlen));
			return;
		}
		ND_PRINT((ndo, "%s ", tok2str(dccp_pkt_type_str, "", dccph_type)));
		break;
	case DCCP_PKT_RESET: {
		const struct dccp_hdr_reset *dhr =
			(const struct dccp_hdr_reset *)(bp + fixed_hdrlen);
		fixed_hdrlen += 12;
		if (len < fixed_hdrlen) {
			ND_PRINT((ndo, "truncated-%s - %u bytes missing!",
				  tok2str(dccp_pkt_type_str, "", dccph_type),
				  len - fixed_hdrlen));
			return;
		}
		ND_TCHECK(*dhr);
		ND_PRINT((ndo, "%s (code=%s) ",
			  tok2str(dccp_pkt_type_str, "", dccph_type),
			  dccp_reset_code(dhr->dccph_reset_code)));
		break;
	}
	case DCCP_PKT_SYNC:
		fixed_hdrlen += 8;
		if (len < fixed_hdrlen) {
			ND_PRINT((ndo, "truncated-%s - %u bytes missing!",
				  tok2str(dccp_pkt_type_str, "", dccph_type),
				  len - fixed_hdrlen));
			return;
		}
		ND_PRINT((ndo, "%s ", tok2str(dccp_pkt_type_str, "", dccph_type)));
		break;
	case DCCP_PKT_SYNCACK:
		fixed_hdrlen += 8;
		if (len < fixed_hdrlen) {
			ND_PRINT((ndo, "truncated-%s - %u bytes missing!",
				  tok2str(dccp_pkt_type_str, "", dccph_type),
				  len - fixed_hdrlen));
			return;
		}
		ND_PRINT((ndo, "%s ", tok2str(dccp_pkt_type_str, "", dccph_type)));
		break;
	default:
		ND_PRINT((ndo, "%s ", tok2str(dccp_pkt_type_str, "unknown-type-%u", dccph_type)));
		break;
	}

	if ((DCCPH_TYPE(dh) != DCCP_PKT_DATA) &&
			(DCCPH_TYPE(dh) != DCCP_PKT_REQUEST))
		dccp_print_ack_no(ndo, bp);

	if (ndo->ndo_vflag < 2)
		return;

	ND_PRINT((ndo, "seq %" PRIu64, dccp_seqno(bp)));

	/* process options */
	if (hlen > fixed_hdrlen){
		u_int optlen;
		cp = bp + fixed_hdrlen;
		ND_PRINT((ndo, " <"));

		hlen -= fixed_hdrlen;
		while(1){
			optlen = dccp_print_option(ndo, cp, hlen);
			if (!optlen)
				break;
			if (hlen <= optlen)
				break;
			hlen -= optlen;
			cp += optlen;
			ND_PRINT((ndo, ", "));
		}
		ND_PRINT((ndo, ">"));
	}
	return;
trunc:
	ND_PRINT((ndo, "%s", tstr));
	return;
}

static const struct tok dccp_option_values[] = {
	{ 0, "nop" },
	{ 1, "mandatory" },
	{ 2, "slowreceiver" },
	{ 32, "change_l" },
	{ 33, "confirm_l" },
	{ 34, "change_r" },
	{ 35, "confirm_r" },
	{ 36, "initcookie" },
	{ 37, "ndp_count" },
	{ 38, "ack_vector0" },
	{ 39, "ack_vector1" },
	{ 40, "data_dropped" },
	{ 41, "timestamp" },
	{ 42, "timestamp_echo" },
	{ 43, "elapsed_time" },
	{ 44, "data_checksum" },
	{ 0, NULL }
};

static int dccp_print_option(netdissect_options *ndo, const u_char *option, u_int hlen)
{
	uint8_t optlen, i;

	ND_TCHECK(*option);

	if (*option >= 32) {
		ND_TCHECK(*(option+1));
		optlen = *(option +1);
		if (optlen < 2) {
			if (*option >= 128)
				ND_PRINT((ndo, "CCID option %u optlen too short", *option));
			else
				ND_PRINT((ndo, "%s optlen too short",
					  tok2str(dccp_option_values, "Option %u", *option)));
			return 0;
		}
	} else
		optlen = 1;

	if (hlen < optlen) {
		if (*option >= 128)
			ND_PRINT((ndo, "CCID option %u optlen goes past header length",
				  *option));
		else
			ND_PRINT((ndo, "%s optlen goes past header length",
				  tok2str(dccp_option_values, "Option %u", *option)));
		return 0;
	}
	ND_TCHECK2(*option, optlen);

	if (*option >= 128) {
		ND_PRINT((ndo, "CCID option %d", *option));
		switch (optlen) {
			case 4:
				ND_PRINT((ndo, " %u", EXTRACT_16BITS(option + 2)));
				break;
			case 6:
				ND_PRINT((ndo, " %u", EXTRACT_32BITS(option + 2)));
				break;
			default:
				break;
		}
	} else {
		ND_PRINT((ndo, "%s", tok2str(dccp_option_values, "Option %u", *option)));
		switch (*option) {
		case 32:
		case 33:
		case 34:
		case 35:
			if (optlen < 3) {
				ND_PRINT((ndo, " optlen too short"));
				return optlen;
			}
			if (*(option + 2) < 10){
				ND_PRINT((ndo, " %s", dccp_feature_nums[*(option + 2)]));
				for (i = 0; i < optlen - 3; i++)
					ND_PRINT((ndo, " %d", *(option + 3 + i)));
			}
			break;
		case 36:
			if (optlen > 2) {
				ND_PRINT((ndo, " 0x"));
				for (i = 0; i < optlen - 2; i++)
					ND_PRINT((ndo, "%02x", *(option + 2 + i)));
			}
			break;
		case 37:
			for (i = 0; i < optlen - 2; i++)
				ND_PRINT((ndo, " %d", *(option + 2 + i)));
			break;
		case 38:
			if (optlen > 2) {
				ND_PRINT((ndo, " 0x"));
				for (i = 0; i < optlen - 2; i++)
					ND_PRINT((ndo, "%02x", *(option + 2 + i)));
			}
			break;
		case 39:
			if (optlen > 2) {
				ND_PRINT((ndo, " 0x"));
				for (i = 0; i < optlen - 2; i++)
					ND_PRINT((ndo, "%02x", *(option + 2 + i)));
			}
			break;
		case 40:
			if (optlen > 2) {
				ND_PRINT((ndo, " 0x"));
				for (i = 0; i < optlen - 2; i++)
					ND_PRINT((ndo, "%02x", *(option + 2 + i)));
			}
			break;
		case 41:
			if (optlen == 4)
				ND_PRINT((ndo, " %u", EXTRACT_32BITS(option + 2)));
			else
				ND_PRINT((ndo, " optlen != 4"));
			break;
		case 42:
			if (optlen == 4)
				ND_PRINT((ndo, " %u", EXTRACT_32BITS(option + 2)));
			else
				ND_PRINT((ndo, " optlen != 4"));
			break;
		case 43:
			if (optlen == 6)
				ND_PRINT((ndo, " %u", EXTRACT_32BITS(option + 2)));
			else if (optlen == 4)
				ND_PRINT((ndo, " %u", EXTRACT_16BITS(option + 2)));
			else
				ND_PRINT((ndo, " optlen != 4 or 6"));
			break;
		case 44:
			if (optlen > 2) {
				ND_PRINT((ndo, " "));
				for (i = 0; i < optlen - 2; i++)
					ND_PRINT((ndo, "%02x", *(option + 2 + i)));
			}
			break;
		}
	}

	return optlen;
trunc:
	ND_PRINT((ndo, "%s", tstr));
	return 0;
}
