/*
 * Copyright (c) 1989, 1990, 1991, 1993, 1994, 1996
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

/* \summary: Routing Information Protocol (RIP) printer */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <netdissect-stdinc.h>

#include <stdio.h>

#include "netdissect.h"
#include "addrtoname.h"
#include "extract.h"

#include "af.h"

static const char tstr[] = "[|rip]";

struct rip {
	uint8_t rip_cmd;		/* request/response */
	uint8_t rip_vers;		/* protocol version # */
	uint8_t unused[2];		/* unused */
};

#define	RIPCMD_REQUEST		1	/* want info */
#define	RIPCMD_RESPONSE		2	/* responding to request */
#define	RIPCMD_TRACEON		3	/* turn tracing on */
#define	RIPCMD_TRACEOFF		4	/* turn it off */
#define	RIPCMD_POLL		5	/* want info from everybody */
#define	RIPCMD_POLLENTRY	6	/* poll for entry */

static const struct tok rip_cmd_values[] = {
    { RIPCMD_REQUEST,	        "Request" },
    { RIPCMD_RESPONSE,	        "Response" },
    { RIPCMD_TRACEON,	        "Trace on" },
    { RIPCMD_TRACEOFF,	        "Trace off" },
    { RIPCMD_POLL,	        "Poll" },
    { RIPCMD_POLLENTRY,	        "Poll Entry" },
    { 0, NULL}
};

#define RIP_AUTHLEN  16
#define RIP_ROUTELEN 20

/*
 * rfc 1723
 *
 *  0                   1                   2                   3 3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * | Command (1)   | Version (1)   |           unused              |
 * +---------------+---------------+-------------------------------+
 * | Address Family Identifier (2) |        Route Tag (2)          |
 * +-------------------------------+-------------------------------+
 * |                         IP Address (4)                        |
 * +---------------------------------------------------------------+
 * |                         Subnet Mask (4)                       |
 * +---------------------------------------------------------------+
 * |                         Next Hop (4)                          |
 * +---------------------------------------------------------------+
 * |                         Metric (4)                            |
 * +---------------------------------------------------------------+
 *
 */

struct rip_netinfo {
	uint16_t rip_family;
	uint16_t rip_tag;
	uint32_t rip_dest;
	uint32_t rip_dest_mask;
	uint32_t rip_router;
	uint32_t rip_metric;		/* cost of route */
};

static void
rip_entry_print_v1(netdissect_options *ndo,
                   register const struct rip_netinfo *ni)
{
	register u_short family;

	/* RFC 1058 */
	family = EXTRACT_16BITS(&ni->rip_family);
	if (family != BSD_AFNUM_INET && family != 0) {
		ND_PRINT((ndo, "\n\t AFI %s, ", tok2str(bsd_af_values, "Unknown (%u)", family)));
		print_unknown_data(ndo, (const uint8_t *)&ni->rip_family, "\n\t  ", RIP_ROUTELEN);
		return;
	}
	if (EXTRACT_16BITS(&ni->rip_tag) ||
	    EXTRACT_32BITS(&ni->rip_dest_mask) ||
	    EXTRACT_32BITS(&ni->rip_router)) {
		/* MBZ fields not zero */
                print_unknown_data(ndo, (const uint8_t *)&ni->rip_family, "\n\t  ", RIP_ROUTELEN);
		return;
	}
	if (family == 0) {
		ND_PRINT((ndo, "\n\t  AFI 0, %s, metric: %u",
			ipaddr_string(ndo, &ni->rip_dest),
			EXTRACT_32BITS(&ni->rip_metric)));
		return;
	} /* BSD_AFNUM_INET */
	ND_PRINT((ndo, "\n\t  %s, metric: %u",
               ipaddr_string(ndo, &ni->rip_dest),
	       EXTRACT_32BITS(&ni->rip_metric)));
}

static unsigned
rip_entry_print_v2(netdissect_options *ndo,
                   register const struct rip_netinfo *ni, const unsigned remaining)
{
	register u_short family;

	family = EXTRACT_16BITS(&ni->rip_family);
	if (family == 0xFFFF) { /* variable-sized authentication structures */
		uint16_t auth_type = EXTRACT_16BITS(&ni->rip_tag);
		if (auth_type == 2) {
			register const u_char *p = (const u_char *)&ni->rip_dest;
			u_int i = 0;
			ND_PRINT((ndo, "\n\t  Simple Text Authentication data: "));
			for (; i < RIP_AUTHLEN; p++, i++)
				ND_PRINT((ndo, "%c", ND_ISPRINT(*p) ? *p : '.'));
		} else if (auth_type == 3) {
			ND_PRINT((ndo, "\n\t  Auth header:"));
			ND_PRINT((ndo, " Packet Len %u,", EXTRACT_16BITS((const uint8_t *)ni + 4)));
			ND_PRINT((ndo, " Key-ID %u,", *((const uint8_t *)ni + 6)));
			ND_PRINT((ndo, " Auth Data Len %u,", *((const uint8_t *)ni + 7)));
			ND_PRINT((ndo, " SeqNo %u,", EXTRACT_32BITS(&ni->rip_dest_mask)));
			ND_PRINT((ndo, " MBZ %u,", EXTRACT_32BITS(&ni->rip_router)));
			ND_PRINT((ndo, " MBZ %u", EXTRACT_32BITS(&ni->rip_metric)));
		} else if (auth_type == 1) {
			ND_PRINT((ndo, "\n\t  Auth trailer:"));
			print_unknown_data(ndo, (const uint8_t *)&ni->rip_dest, "\n\t  ", remaining);
			return remaining; /* AT spans till the packet end */
		} else {
			ND_PRINT((ndo, "\n\t  Unknown (%u) Authentication data:",
			       EXTRACT_16BITS(&ni->rip_tag)));
			print_unknown_data(ndo, (const uint8_t *)&ni->rip_dest, "\n\t  ", remaining);
		}
	} else if (family != BSD_AFNUM_INET && family != 0) {
		ND_PRINT((ndo, "\n\t  AFI %s", tok2str(bsd_af_values, "Unknown (%u)", family)));
                print_unknown_data(ndo, (const uint8_t *)&ni->rip_tag, "\n\t  ", RIP_ROUTELEN-2);
	} else { /* BSD_AFNUM_INET or AFI 0 */
		ND_PRINT((ndo, "\n\t  AFI %s, %15s/%-2d, tag 0x%04x, metric: %u, next-hop: ",
                       tok2str(bsd_af_values, "%u", family),
                       ipaddr_string(ndo, &ni->rip_dest),
		       mask2plen(EXTRACT_32BITS(&ni->rip_dest_mask)),
                       EXTRACT_16BITS(&ni->rip_tag),
                       EXTRACT_32BITS(&ni->rip_metric)));
		if (EXTRACT_32BITS(&ni->rip_router))
			ND_PRINT((ndo, "%s", ipaddr_string(ndo, &ni->rip_router)));
		else
			ND_PRINT((ndo, "self"));
	}
	return sizeof (*ni);
}

void
rip_print(netdissect_options *ndo,
          const u_char *dat, u_int length)
{
	register const struct rip *rp;
	register const struct rip_netinfo *ni;
	register u_int i, j;

	if (ndo->ndo_snapend < dat) {
		ND_PRINT((ndo, " %s", tstr));
		return;
	}
	i = ndo->ndo_snapend - dat;
	if (i > length)
		i = length;
	if (i < sizeof(*rp)) {
		ND_PRINT((ndo, " %s", tstr));
		return;
	}
	i -= sizeof(*rp);

	rp = (const struct rip *)dat;

	ND_PRINT((ndo, "%sRIPv%u",
               (ndo->ndo_vflag >= 1) ? "\n\t" : "",
               rp->rip_vers));

	switch (rp->rip_vers) {
	case 0:
		/*
		 * RFC 1058.
		 *
		 * XXX - RFC 1058 says
		 *
		 * 0  Datagrams whose version number is zero are to be ignored.
		 *    These are from a previous version of the protocol, whose
		 *    packet format was machine-specific.
		 *
		 * so perhaps we should just dump the packet, in hex.
		 */
                print_unknown_data(ndo, (const uint8_t *)&rp->rip_cmd, "\n\t", length);
		break;
	default:
                /* dump version and lets see if we know the commands name*/
                ND_PRINT((ndo, ", %s, length: %u",
                       tok2str(rip_cmd_values,
                               "unknown command (%u)",
                               rp->rip_cmd),
                       length));

                if (ndo->ndo_vflag < 1)
                    return;

		switch (rp->rip_cmd) {
		case RIPCMD_REQUEST:
		case RIPCMD_RESPONSE:
			j = length / sizeof(*ni);
			ND_PRINT((ndo, ", routes: %u%s", j, rp->rip_vers == 2 ? " or less" : ""));
			ni = (const struct rip_netinfo *)(rp + 1);
			for (; i >= sizeof(*ni); ++ni) {
				if (rp->rip_vers == 1)
				{
					rip_entry_print_v1(ndo, ni);
					i -= sizeof(*ni);
				}
				else if (rp->rip_vers == 2)
					i -= rip_entry_print_v2(ndo, ni, i);
                                else
                                    break;
			}
			if (i)
				ND_PRINT((ndo, "%s", tstr));
			break;

		case RIPCMD_TRACEOFF:
		case RIPCMD_POLL:
		case RIPCMD_POLLENTRY:
			break;

		case RIPCMD_TRACEON:
                    /* fall through */
	        default:
                    if (ndo->ndo_vflag <= 1) {
                        if(!print_unknown_data(ndo, (const uint8_t *)rp, "\n\t", length))
                            return;
                    }
                    break;
                }
                /* do we want to see an additionally hexdump ? */
                if (ndo->ndo_vflag> 1) {
                    if(!print_unknown_data(ndo, (const uint8_t *)rp, "\n\t", length))
                        return;
                }
        }
}


