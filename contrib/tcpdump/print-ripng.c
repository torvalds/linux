/*
 * Copyright (c) 1989, 1990, 1991, 1993, 1994
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
 */

/* \summary: IPv6 Routing Information Protocol (RIPng) printer */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <netdissect-stdinc.h>

#include "netdissect.h"
#include "addrtoname.h"
#include "extract.h"

/*
 * Copyright (C) 1995, 1996, 1997 and 1998 WIDE Project.
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
#define	RIP6_VERSION	1

#define	RIP6_REQUEST	1
#define	RIP6_RESPONSE	2

struct netinfo6 {
	struct in6_addr	rip6_dest;
	uint16_t	rip6_tag;
	uint8_t		rip6_plen;
	uint8_t		rip6_metric;
};

struct	rip6 {
	uint8_t		rip6_cmd;
	uint8_t		rip6_vers;
	uint8_t		rip6_res1[2];
	union {
		struct	netinfo6	ru6_nets[1];
		char	ru6_tracefile[1];
	} rip6un;
#define	rip6_nets	rip6un.ru6_nets
#define	rip6_tracefile	rip6un.ru6_tracefile
};

#define	HOPCNT_INFINITY6	16

#if !defined(IN6_IS_ADDR_UNSPECIFIED) && !defined(_MSC_VER) /* MSVC inline */
static int IN6_IS_ADDR_UNSPECIFIED(const struct in6_addr *addr)
{
    static const struct in6_addr in6addr_any;        /* :: */
    return (memcmp(addr, &in6addr_any, sizeof(*addr)) == 0);
}
#endif

static int
rip6_entry_print(netdissect_options *ndo, register const struct netinfo6 *ni, int metric)
{
	int l;
	l = ND_PRINT((ndo, "%s/%d", ip6addr_string(ndo, &ni->rip6_dest), ni->rip6_plen));
	if (ni->rip6_tag)
		l += ND_PRINT((ndo, " [%d]", EXTRACT_16BITS(&ni->rip6_tag)));
	if (metric)
		l += ND_PRINT((ndo, " (%d)", ni->rip6_metric));
	return l;
}

void
ripng_print(netdissect_options *ndo, const u_char *dat, unsigned int length)
{
	register const struct rip6 *rp = (const struct rip6 *)dat;
	register const struct netinfo6 *ni;
	unsigned int length_left;
	u_int j;

	ND_TCHECK(rp->rip6_cmd);
	switch (rp->rip6_cmd) {

	case RIP6_REQUEST:
		length_left = length;
		if (length_left < (sizeof(struct rip6) - sizeof(struct netinfo6)))
			goto trunc;
		length_left -= (sizeof(struct rip6) - sizeof(struct netinfo6));
 		j = length_left / sizeof(*ni);
		if (j == 1) {
			ND_TCHECK(rp->rip6_nets);
			if (rp->rip6_nets->rip6_metric == HOPCNT_INFINITY6
			    &&  IN6_IS_ADDR_UNSPECIFIED(&rp->rip6_nets->rip6_dest)) {
				ND_PRINT((ndo, " ripng-req dump"));
				break;
			}
		}
		if (j * sizeof(*ni) != length_left)
			ND_PRINT((ndo, " ripng-req %u[%u]:", j, length));
		else
			ND_PRINT((ndo, " ripng-req %u:", j));
		for (ni = rp->rip6_nets; length_left >= sizeof(*ni);
		    length_left -= sizeof(*ni), ++ni) {
			ND_TCHECK(*ni);
			if (ndo->ndo_vflag > 1)
				ND_PRINT((ndo, "\n\t"));
			else
				ND_PRINT((ndo, " "));
			rip6_entry_print(ndo, ni, 0);
		}
		if (length_left != 0)
			goto trunc;
		break;
	case RIP6_RESPONSE:
		length_left = length;
		if (length_left < (sizeof(struct rip6) - sizeof(struct netinfo6)))
			goto trunc;
		length_left -= (sizeof(struct rip6) - sizeof(struct netinfo6));
		j = length_left / sizeof(*ni);
		if (j * sizeof(*ni) != length_left)
			ND_PRINT((ndo, " ripng-resp %d[%u]:", j, length));
		else
			ND_PRINT((ndo, " ripng-resp %d:", j));
		for (ni = rp->rip6_nets; length_left >= sizeof(*ni);
		    length_left -= sizeof(*ni), ++ni) {
			ND_TCHECK(*ni);
			if (ndo->ndo_vflag > 1)
				ND_PRINT((ndo, "\n\t"));
			else
				ND_PRINT((ndo, " "));
			rip6_entry_print(ndo, ni, ni->rip6_metric);
		}
		if (length_left != 0)
			goto trunc;
		break;
	default:
		ND_PRINT((ndo, " ripng-%d ?? %u", rp->rip6_cmd, length));
		break;
	}
	ND_TCHECK(rp->rip6_vers);
	if (rp->rip6_vers != RIP6_VERSION)
		ND_PRINT((ndo, " [vers %d]", rp->rip6_vers));
	return;

trunc:
	ND_PRINT((ndo, "[|ripng]"));
	return;
}
