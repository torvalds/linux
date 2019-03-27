/*
 * Copyright (C) 2001 WIDE Project.  All rights reserved.
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

/* \summary: Multi-Protocol Label Switching (MPLS) printer */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <netdissect-stdinc.h>

#include "netdissect.h"
#include "extract.h"
#include "mpls.h"

static const char *mpls_labelname[] = {
/*0*/	"IPv4 explicit NULL", "router alert", "IPv6 explicit NULL",
	"implicit NULL", "rsvd",
/*5*/	"rsvd", "rsvd", "rsvd", "rsvd", "rsvd",
/*10*/	"rsvd", "rsvd", "rsvd", "rsvd", "rsvd",
/*15*/	"rsvd",
};

enum mpls_packet_type {
	PT_UNKNOWN,
	PT_IPV4,
	PT_IPV6,
	PT_OSI
};

/*
 * RFC3032: MPLS label stack encoding
 */
void
mpls_print(netdissect_options *ndo, const u_char *bp, u_int length)
{
	const u_char *p;
	uint32_t label_entry;
	uint16_t label_stack_depth = 0;
	enum mpls_packet_type pt = PT_UNKNOWN;

	p = bp;
	ND_PRINT((ndo, "MPLS"));
	do {
		ND_TCHECK2(*p, sizeof(label_entry));
		if (length < sizeof(label_entry)) {
			ND_PRINT((ndo, "[|MPLS], length %u", length));
			return;
		}
		label_entry = EXTRACT_32BITS(p);
		ND_PRINT((ndo, "%s(label %u",
		       (label_stack_depth && ndo->ndo_vflag) ? "\n\t" : " ",
       		       MPLS_LABEL(label_entry)));
		label_stack_depth++;
		if (ndo->ndo_vflag &&
		    MPLS_LABEL(label_entry) < sizeof(mpls_labelname) / sizeof(mpls_labelname[0]))
			ND_PRINT((ndo, " (%s)", mpls_labelname[MPLS_LABEL(label_entry)]));
		ND_PRINT((ndo, ", exp %u", MPLS_EXP(label_entry)));
		if (MPLS_STACK(label_entry))
			ND_PRINT((ndo, ", [S]"));
		ND_PRINT((ndo, ", ttl %u)", MPLS_TTL(label_entry)));

		p += sizeof(label_entry);
		length -= sizeof(label_entry);
	} while (!MPLS_STACK(label_entry));

	/*
	 * Try to figure out the packet type.
	 */
	switch (MPLS_LABEL(label_entry)) {

	case 0:	/* IPv4 explicit NULL label */
	case 3:	/* IPv4 implicit NULL label */
		pt = PT_IPV4;
		break;

	case 2:	/* IPv6 explicit NULL label */
		pt = PT_IPV6;
		break;

	default:
		/*
		 * Generally there's no indication of protocol in MPLS label
		 * encoding.
		 *
		 * However, draft-hsmit-isis-aal5mux-00.txt describes a
		 * technique for encapsulating IS-IS and IP traffic on the
		 * same ATM virtual circuit; you look at the first payload
		 * byte to determine the network layer protocol, based on
		 * the fact that
		 *
		 *	1) the first byte of an IP header is 0x45-0x4f
		 *	   for IPv4 and 0x60-0x6f for IPv6;
		 *
		 *	2) the first byte of an OSI CLNP packet is 0x81,
		 *	   the first byte of an OSI ES-IS packet is 0x82,
		 *	   and the first byte of an OSI IS-IS packet is
		 *	   0x83;
		 *
		 * so the network layer protocol can be inferred from the
		 * first byte of the packet, if the protocol is one of the
		 * ones listed above.
		 *
		 * Cisco sends control-plane traffic MPLS-encapsulated in
		 * this fashion.
		 */
		ND_TCHECK(*p);
		if (length < 1) {
			/* nothing to print */
			return;
		}
		switch(*p) {

		case 0x45:
		case 0x46:
		case 0x47:
		case 0x48:
		case 0x49:
		case 0x4a:
		case 0x4b:
		case 0x4c:
		case 0x4d:
		case 0x4e:
		case 0x4f:
			pt = PT_IPV4;
			break;

		case 0x60:
		case 0x61:
		case 0x62:
		case 0x63:
		case 0x64:
		case 0x65:
		case 0x66:
		case 0x67:
		case 0x68:
		case 0x69:
		case 0x6a:
		case 0x6b:
		case 0x6c:
		case 0x6d:
		case 0x6e:
		case 0x6f:
			pt = PT_IPV6;
			break;

		case 0x81:
		case 0x82:
		case 0x83:
			pt = PT_OSI;
			break;

		default:
			/* ok bail out - we did not figure out what it is*/
			break;
		}
	}

	/*
	 * Print the payload.
	 */
	if (pt == PT_UNKNOWN) {
		if (!ndo->ndo_suppress_default_print)
			ND_DEFAULTPRINT(p, length);
		return;
	}
	ND_PRINT((ndo, ndo->ndo_vflag ? "\n\t" : " "));
	switch (pt) {

	case PT_IPV4:
		ip_print(ndo, p, length);
		break;

	case PT_IPV6:
		ip6_print(ndo, p, length);
		break;

	case PT_OSI:
		isoclns_print(ndo, p, length);
		break;

	default:
		break;
	}
	return;

trunc:
	ND_PRINT((ndo, "[|MPLS]"));
}


/*
 * Local Variables:
 * c-style: whitesmith
 * c-basic-offset: 8
 * End:
 */
