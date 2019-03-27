/*
 * Copyright (C) 1998 WIDE Project.
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

/* \summary: IPv6 header option printer */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <netdissect-stdinc.h>

#include "netdissect.h"
#include "addrtoname.h"
#include "extract.h"

#include "ip6.h"

static void
ip6_sopt_print(netdissect_options *ndo, const u_char *bp, int len)
{
    int i;
    int optlen;

    for (i = 0; i < len; i += optlen) {
	if (bp[i] == IP6OPT_PAD1)
	    optlen = 1;
	else {
	    if (i + 1 < len)
		optlen = bp[i + 1] + 2;
	    else
		goto trunc;
	}
	if (i + optlen > len)
	    goto trunc;

	switch (bp[i]) {
	case IP6OPT_PAD1:
            ND_PRINT((ndo, ", pad1"));
	    break;
	case IP6OPT_PADN:
	    if (len - i < IP6OPT_MINLEN) {
		ND_PRINT((ndo, ", padn: trunc"));
		goto trunc;
	    }
            ND_PRINT((ndo, ", padn"));
	    break;
	default:
	    if (len - i < IP6OPT_MINLEN) {
		ND_PRINT((ndo, ", sopt_type %d: trunc)", bp[i]));
		goto trunc;
	    }
	    ND_PRINT((ndo, ", sopt_type 0x%02x: len=%d", bp[i], bp[i + 1]));
	    break;
	}
    }
    return;

trunc:
    ND_PRINT((ndo, "[trunc] "));
}

static void
ip6_opt_print(netdissect_options *ndo, const u_char *bp, int len)
{
    int i;
    int optlen = 0;

    if (len == 0)
        return;
    for (i = 0; i < len; i += optlen) {
	if (bp[i] == IP6OPT_PAD1)
	    optlen = 1;
	else {
	    if (i + 1 < len)
		optlen = bp[i + 1] + 2;
	    else
		goto trunc;
	}
	if (i + optlen > len)
	    goto trunc;

	switch (bp[i]) {
	case IP6OPT_PAD1:
            ND_PRINT((ndo, "(pad1)"));
	    break;
	case IP6OPT_PADN:
	    if (len - i < IP6OPT_MINLEN) {
		ND_PRINT((ndo, "(padn: trunc)"));
		goto trunc;
	    }
            ND_PRINT((ndo, "(padn)"));
	    break;
	case IP6OPT_ROUTER_ALERT:
	    if (len - i < IP6OPT_RTALERT_LEN) {
		ND_PRINT((ndo, "(rtalert: trunc)"));
		goto trunc;
	    }
	    if (bp[i + 1] != IP6OPT_RTALERT_LEN - 2) {
		ND_PRINT((ndo, "(rtalert: invalid len %d)", bp[i + 1]));
		goto trunc;
	    }
	    ND_PRINT((ndo, "(rtalert: 0x%04x) ", EXTRACT_16BITS(&bp[i + 2])));
	    break;
	case IP6OPT_JUMBO:
	    if (len - i < IP6OPT_JUMBO_LEN) {
		ND_PRINT((ndo, "(jumbo: trunc)"));
		goto trunc;
	    }
	    if (bp[i + 1] != IP6OPT_JUMBO_LEN - 2) {
		ND_PRINT((ndo, "(jumbo: invalid len %d)", bp[i + 1]));
		goto trunc;
	    }
	    ND_PRINT((ndo, "(jumbo: %u) ", EXTRACT_32BITS(&bp[i + 2])));
	    break;
        case IP6OPT_HOME_ADDRESS:
	    if (len - i < IP6OPT_HOMEADDR_MINLEN) {
		ND_PRINT((ndo, "(homeaddr: trunc)"));
		goto trunc;
	    }
	    if (bp[i + 1] < IP6OPT_HOMEADDR_MINLEN - 2) {
		ND_PRINT((ndo, "(homeaddr: invalid len %d)", bp[i + 1]));
		goto trunc;
	    }
	    ND_PRINT((ndo, "(homeaddr: %s", ip6addr_string(ndo, &bp[i + 2])));
            if (bp[i + 1] > IP6OPT_HOMEADDR_MINLEN - 2) {
		ip6_sopt_print(ndo, &bp[i + IP6OPT_HOMEADDR_MINLEN],
		    (optlen - IP6OPT_HOMEADDR_MINLEN));
	    }
            ND_PRINT((ndo, ")"));
	    break;
	default:
	    if (len - i < IP6OPT_MINLEN) {
		ND_PRINT((ndo, "(type %d: trunc)", bp[i]));
		goto trunc;
	    }
	    ND_PRINT((ndo, "(opt_type 0x%02x: len=%d)", bp[i], bp[i + 1]));
	    break;
	}
    }
    ND_PRINT((ndo, " "));
    return;

trunc:
    ND_PRINT((ndo, "[trunc] "));
}

int
hbhopt_print(netdissect_options *ndo, register const u_char *bp)
{
    const struct ip6_hbh *dp = (const struct ip6_hbh *)bp;
    int hbhlen = 0;

    ND_TCHECK(dp->ip6h_len);
    hbhlen = (int)((dp->ip6h_len + 1) << 3);
    ND_TCHECK2(*dp, hbhlen);
    ND_PRINT((ndo, "HBH "));
    if (ndo->ndo_vflag)
	ip6_opt_print(ndo, (const u_char *)dp + sizeof(*dp), hbhlen - sizeof(*dp));

    return(hbhlen);

  trunc:
    ND_PRINT((ndo, "[|HBH]"));
    return(-1);
}

int
dstopt_print(netdissect_options *ndo, register const u_char *bp)
{
    const struct ip6_dest *dp = (const struct ip6_dest *)bp;
    int dstoptlen = 0;

    ND_TCHECK(dp->ip6d_len);
    dstoptlen = (int)((dp->ip6d_len + 1) << 3);
    ND_TCHECK2(*dp, dstoptlen);
    ND_PRINT((ndo, "DSTOPT "));
    if (ndo->ndo_vflag) {
	ip6_opt_print(ndo, (const u_char *)dp + sizeof(*dp),
	    dstoptlen - sizeof(*dp));
    }

    return(dstoptlen);

  trunc:
    ND_PRINT((ndo, "[|DSTOPT]"));
    return(-1);
}
