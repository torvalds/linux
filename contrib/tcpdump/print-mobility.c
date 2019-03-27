/*
 * Copyright (C) 2002 WIDE Project.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the project nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/* \summary: IPv6 mobility printer */
/* RFC 3775 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <netdissect-stdinc.h>

#include "netdissect.h"
#include "addrtoname.h"
#include "extract.h"

#include "ip6.h"

static const char tstr[] = "[|MOBILITY]";

/* Mobility header */
struct ip6_mobility {
	uint8_t ip6m_pproto;	/* following payload protocol (for PG) */
	uint8_t ip6m_len;	/* length in units of 8 octets */
	uint8_t ip6m_type;	/* message type */
	uint8_t reserved;	/* reserved */
	uint16_t ip6m_cksum;	/* sum of IPv6 pseudo-header and MH */
	union {
		uint16_t	ip6m_un_data16[1]; /* type-specific field */
		uint8_t		ip6m_un_data8[2];  /* type-specific field */
	} ip6m_dataun;
};

#define ip6m_data16	ip6m_dataun.ip6m_un_data16
#define ip6m_data8	ip6m_dataun.ip6m_un_data8

#define IP6M_MINLEN	8

/* http://www.iana.org/assignments/mobility-parameters/mobility-parameters.xhtml */

/* message type */
#define IP6M_BINDING_REQUEST	0	/* Binding Refresh Request */
#define IP6M_HOME_TEST_INIT	1	/* Home Test Init */
#define IP6M_CAREOF_TEST_INIT	2	/* Care-of Test Init */
#define IP6M_HOME_TEST		3	/* Home Test */
#define IP6M_CAREOF_TEST	4	/* Care-of Test */
#define IP6M_BINDING_UPDATE	5	/* Binding Update */
#define IP6M_BINDING_ACK	6	/* Binding Acknowledgement */
#define IP6M_BINDING_ERROR	7	/* Binding Error */
#define IP6M_MAX		7

static const struct tok ip6m_str[] = {
	{ IP6M_BINDING_REQUEST,  "BRR"  },
	{ IP6M_HOME_TEST_INIT,   "HoTI" },
	{ IP6M_CAREOF_TEST_INIT, "CoTI" },
	{ IP6M_HOME_TEST,        "HoT"  },
	{ IP6M_CAREOF_TEST,      "CoT"  },
	{ IP6M_BINDING_UPDATE,   "BU"   },
	{ IP6M_BINDING_ACK,      "BA"   },
	{ IP6M_BINDING_ERROR,    "BE"   },
	{ 0, NULL }
};

static const unsigned ip6m_hdrlen[IP6M_MAX + 1] = {
	IP6M_MINLEN,      /* IP6M_BINDING_REQUEST  */
	IP6M_MINLEN + 8,  /* IP6M_HOME_TEST_INIT   */
	IP6M_MINLEN + 8,  /* IP6M_CAREOF_TEST_INIT */
	IP6M_MINLEN + 16, /* IP6M_HOME_TEST        */
	IP6M_MINLEN + 16, /* IP6M_CAREOF_TEST      */
	IP6M_MINLEN + 4,  /* IP6M_BINDING_UPDATE   */
	IP6M_MINLEN + 4,  /* IP6M_BINDING_ACK      */
	IP6M_MINLEN + 16, /* IP6M_BINDING_ERROR    */
};

/* Mobility Header Options */
#define IP6MOPT_MINLEN		2
#define IP6MOPT_PAD1          0x0	/* Pad1 */
#define IP6MOPT_PADN          0x1	/* PadN */
#define IP6MOPT_REFRESH	      0x2	/* Binding Refresh Advice */
#define IP6MOPT_REFRESH_MINLEN  4
#define IP6MOPT_ALTCOA        0x3	/* Alternate Care-of Address */
#define IP6MOPT_ALTCOA_MINLEN  18
#define IP6MOPT_NONCEID       0x4	/* Nonce Indices */
#define IP6MOPT_NONCEID_MINLEN  6
#define IP6MOPT_AUTH          0x5	/* Binding Authorization Data */
#define IP6MOPT_AUTH_MINLEN    12

static int
mobility_opt_print(netdissect_options *ndo,
                   const u_char *bp, const unsigned len)
{
	unsigned i, optlen;

	for (i = 0; i < len; i += optlen) {
		ND_TCHECK(bp[i]);
		if (bp[i] == IP6MOPT_PAD1)
			optlen = 1;
		else {
			if (i + 1 < len) {
				ND_TCHECK(bp[i + 1]);
				optlen = bp[i + 1] + 2;
			}
			else
				goto trunc;
		}
		if (i + optlen > len)
			goto trunc;
		ND_TCHECK(bp[i + optlen]);

		switch (bp[i]) {
		case IP6MOPT_PAD1:
			ND_PRINT((ndo, "(pad1)"));
			break;
		case IP6MOPT_PADN:
			if (len - i < IP6MOPT_MINLEN) {
				ND_PRINT((ndo, "(padn: trunc)"));
				goto trunc;
			}
			ND_PRINT((ndo, "(padn)"));
			break;
		case IP6MOPT_REFRESH:
			if (len - i < IP6MOPT_REFRESH_MINLEN) {
				ND_PRINT((ndo, "(refresh: trunc)"));
				goto trunc;
			}
			/* units of 4 secs */
			ND_TCHECK_16BITS(&bp[i+2]);
			ND_PRINT((ndo, "(refresh: %u)",
				EXTRACT_16BITS(&bp[i+2]) << 2));
			break;
		case IP6MOPT_ALTCOA:
			if (len - i < IP6MOPT_ALTCOA_MINLEN) {
				ND_PRINT((ndo, "(altcoa: trunc)"));
				goto trunc;
			}
			ND_TCHECK_128BITS(&bp[i+2]);
			ND_PRINT((ndo, "(alt-CoA: %s)", ip6addr_string(ndo, &bp[i+2])));
			break;
		case IP6MOPT_NONCEID:
			if (len - i < IP6MOPT_NONCEID_MINLEN) {
				ND_PRINT((ndo, "(ni: trunc)"));
				goto trunc;
			}
			ND_TCHECK_16BITS(&bp[i+2]);
			ND_TCHECK_16BITS(&bp[i+4]);
			ND_PRINT((ndo, "(ni: ho=0x%04x co=0x%04x)",
				EXTRACT_16BITS(&bp[i+2]),
				EXTRACT_16BITS(&bp[i+4])));
			break;
		case IP6MOPT_AUTH:
			if (len - i < IP6MOPT_AUTH_MINLEN) {
				ND_PRINT((ndo, "(auth: trunc)"));
				goto trunc;
			}
			ND_PRINT((ndo, "(auth)"));
			break;
		default:
			if (len - i < IP6MOPT_MINLEN) {
				ND_PRINT((ndo, "(sopt_type %u: trunc)", bp[i]));
				goto trunc;
			}
			ND_PRINT((ndo, "(type-0x%02x: len=%u)", bp[i], bp[i + 1]));
			break;
		}
	}
	return 0;

trunc:
	return 1;
}

/*
 * Mobility Header
 */
int
mobility_print(netdissect_options *ndo,
               const u_char *bp, const u_char *bp2 _U_)
{
	const struct ip6_mobility *mh;
	const u_char *ep;
	unsigned mhlen, hlen;
	uint8_t type;

	mh = (const struct ip6_mobility *)bp;

	/* 'ep' points to the end of available data. */
	ep = ndo->ndo_snapend;

	if (!ND_TTEST(mh->ip6m_len)) {
		/*
		 * There's not enough captured data to include the
		 * mobility header length.
		 *
		 * Our caller expects us to return the length, however,
		 * so return a value that will run to the end of the
		 * captured data.
		 *
		 * XXX - "ip6_print()" doesn't do anything with the
		 * returned length, however, as it breaks out of the
		 * header-processing loop.
		 */
		mhlen = ep - bp;
		goto trunc;
	}
	mhlen = (mh->ip6m_len + 1) << 3;

	/* XXX ip6m_cksum */

	ND_TCHECK(mh->ip6m_type);
	type = mh->ip6m_type;
	if (type <= IP6M_MAX && mhlen < ip6m_hdrlen[type]) {
		ND_PRINT((ndo, "(header length %u is too small for type %u)", mhlen, type));
		goto trunc;
	}
	ND_PRINT((ndo, "mobility: %s", tok2str(ip6m_str, "type-#%u", type)));
	switch (type) {
	case IP6M_BINDING_REQUEST:
		hlen = IP6M_MINLEN;
		break;
	case IP6M_HOME_TEST_INIT:
	case IP6M_CAREOF_TEST_INIT:
		hlen = IP6M_MINLEN;
		if (ndo->ndo_vflag) {
			ND_TCHECK_32BITS(&bp[hlen + 4]);
			ND_PRINT((ndo, " %s Init Cookie=%08x:%08x",
			       type == IP6M_HOME_TEST_INIT ? "Home" : "Care-of",
			       EXTRACT_32BITS(&bp[hlen]),
			       EXTRACT_32BITS(&bp[hlen + 4])));
		}
		hlen += 8;
		break;
	case IP6M_HOME_TEST:
	case IP6M_CAREOF_TEST:
		ND_TCHECK(mh->ip6m_data16[0]);
		ND_PRINT((ndo, " nonce id=0x%x", EXTRACT_16BITS(&mh->ip6m_data16[0])));
		hlen = IP6M_MINLEN;
		if (ndo->ndo_vflag) {
			ND_TCHECK_32BITS(&bp[hlen + 4]);
			ND_PRINT((ndo, " %s Init Cookie=%08x:%08x",
			       type == IP6M_HOME_TEST ? "Home" : "Care-of",
			       EXTRACT_32BITS(&bp[hlen]),
			       EXTRACT_32BITS(&bp[hlen + 4])));
		}
		hlen += 8;
		if (ndo->ndo_vflag) {
			ND_TCHECK_32BITS(&bp[hlen + 4]);
			ND_PRINT((ndo, " %s Keygen Token=%08x:%08x",
			       type == IP6M_HOME_TEST ? "Home" : "Care-of",
			       EXTRACT_32BITS(&bp[hlen]),
			       EXTRACT_32BITS(&bp[hlen + 4])));
		}
		hlen += 8;
		break;
	case IP6M_BINDING_UPDATE:
		ND_TCHECK(mh->ip6m_data16[0]);
		ND_PRINT((ndo, " seq#=%u", EXTRACT_16BITS(&mh->ip6m_data16[0])));
		hlen = IP6M_MINLEN;
		ND_TCHECK_16BITS(&bp[hlen]);
		if (bp[hlen] & 0xf0) {
			ND_PRINT((ndo, " "));
			if (bp[hlen] & 0x80)
				ND_PRINT((ndo, "A"));
			if (bp[hlen] & 0x40)
				ND_PRINT((ndo, "H"));
			if (bp[hlen] & 0x20)
				ND_PRINT((ndo, "L"));
			if (bp[hlen] & 0x10)
				ND_PRINT((ndo, "K"));
		}
		/* Reserved (4bits) */
		hlen += 1;
		/* Reserved (8bits) */
		hlen += 1;
		ND_TCHECK_16BITS(&bp[hlen]);
		/* units of 4 secs */
		ND_PRINT((ndo, " lifetime=%u", EXTRACT_16BITS(&bp[hlen]) << 2));
		hlen += 2;
		break;
	case IP6M_BINDING_ACK:
		ND_TCHECK(mh->ip6m_data8[0]);
		ND_PRINT((ndo, " status=%u", mh->ip6m_data8[0]));
		ND_TCHECK(mh->ip6m_data8[1]);
		if (mh->ip6m_data8[1] & 0x80)
			ND_PRINT((ndo, " K"));
		/* Reserved (7bits) */
		hlen = IP6M_MINLEN;
		ND_TCHECK_16BITS(&bp[hlen]);
		ND_PRINT((ndo, " seq#=%u", EXTRACT_16BITS(&bp[hlen])));
		hlen += 2;
		ND_TCHECK_16BITS(&bp[hlen]);
		/* units of 4 secs */
		ND_PRINT((ndo, " lifetime=%u", EXTRACT_16BITS(&bp[hlen]) << 2));
		hlen += 2;
		break;
	case IP6M_BINDING_ERROR:
		ND_TCHECK(mh->ip6m_data8[0]);
		ND_PRINT((ndo, " status=%u", mh->ip6m_data8[0]));
		/* Reserved */
		hlen = IP6M_MINLEN;
		ND_TCHECK2(bp[hlen], 16);
		ND_PRINT((ndo, " homeaddr %s", ip6addr_string(ndo, &bp[hlen])));
		hlen += 16;
		break;
	default:
		ND_PRINT((ndo, " len=%u", mh->ip6m_len));
		return(mhlen);
		break;
	}
	if (ndo->ndo_vflag)
		if (mobility_opt_print(ndo, &bp[hlen], mhlen - hlen))
			goto trunc;;

	return(mhlen);

 trunc:
	ND_PRINT((ndo, "%s", tstr));
	return(-1);
}
