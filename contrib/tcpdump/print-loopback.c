/*
 * Copyright (c) 2014 The TCPDUMP project
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/* \summary: Loopback Protocol printer */

/*
 * originally defined as the Ethernet Configuration Testing Protocol.
 * specification: http://www.mit.edu/people/jhawk/ctp.pdf
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <netdissect-stdinc.h>

#include "netdissect.h"
#include "extract.h"
#include "ether.h"
#include "addrtoname.h"

static const char tstr[] = " [|loopback]";

#define LOOPBACK_REPLY   1
#define LOOPBACK_FWDDATA 2

static const struct tok fcode_str[] = {
	{ LOOPBACK_REPLY,   "Reply"        },
	{ LOOPBACK_FWDDATA, "Forward Data" },
	{ 0, NULL }
};

static void
loopback_message_print(netdissect_options *ndo, const u_char *cp, const u_int len)
{
	const u_char *ep = cp + len;
	uint16_t function;

	if (len < 2)
		goto invalid;
	/* function */
	ND_TCHECK2(*cp, 2);
	function = EXTRACT_LE_16BITS(cp);
	cp += 2;
	ND_PRINT((ndo, ", %s", tok2str(fcode_str, " invalid (%u)", function)));

	switch (function) {
		case LOOPBACK_REPLY:
			if (len < 4)
				goto invalid;
			/* receipt number */
			ND_TCHECK2(*cp, 2);
			ND_PRINT((ndo, ", receipt number %u", EXTRACT_LE_16BITS(cp)));
			cp += 2;
			/* data */
			ND_PRINT((ndo, ", data (%u octets)", len - 4));
			ND_TCHECK2(*cp, len - 4);
			break;
		case LOOPBACK_FWDDATA:
			if (len < 8)
				goto invalid;
			/* forwarding address */
			ND_TCHECK2(*cp, ETHER_ADDR_LEN);
			ND_PRINT((ndo, ", forwarding address %s", etheraddr_string(ndo, cp)));
			cp += ETHER_ADDR_LEN;
			/* data */
			ND_PRINT((ndo, ", data (%u octets)", len - 8));
			ND_TCHECK2(*cp, len - 8);
			break;
		default:
			ND_TCHECK2(*cp, len - 2);
			break;
	}
	return;

invalid:
	ND_PRINT((ndo, "%s", istr));
	ND_TCHECK2(*cp, ep - cp);
	return;
trunc:
	ND_PRINT((ndo, "%s", tstr));
}

void
loopback_print(netdissect_options *ndo, const u_char *cp, const u_int len)
{
	const u_char *ep = cp + len;
	uint16_t skipCount;

	ND_PRINT((ndo, "Loopback"));
	if (len < 2)
		goto invalid;
	/* skipCount */
	ND_TCHECK2(*cp, 2);
	skipCount = EXTRACT_LE_16BITS(cp);
	cp += 2;
	ND_PRINT((ndo, ", skipCount %u", skipCount));
	if (skipCount % 8)
		ND_PRINT((ndo, " (bogus)"));
	if (skipCount > len - 2)
		goto invalid;
	loopback_message_print(ndo, cp + skipCount, len - 2 - skipCount);
	return;

invalid:
	ND_PRINT((ndo, "%s", istr));
	ND_TCHECK2(*cp, ep - cp);
	return;
trunc:
	ND_PRINT((ndo, "%s", tstr));
}

