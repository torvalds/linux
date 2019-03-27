/*
 * Copyright (c) 2001 William C. Fenner.
 *                All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that: (1) source code
 * distributions retain the above copyright notice and this paragraph
 * in its entirety, and (2) distributions including binary code include
 * the above copyright notice and this paragraph in its entirety in
 * the documentation or other materials provided with the distribution.
 * The name of William C. Fenner may not be used to endorse or
 * promote products derived from this software without specific prior
 * written permission.  THIS SOFTWARE IS PROVIDED ``AS IS'' AND
 * WITHOUT ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, WITHOUT
 * LIMITATION, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE.
 */

/* \summary: Multicast Source Discovery Protocol (MSDP) printer */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <netdissect-stdinc.h>

#include "netdissect.h"
#include "addrtoname.h"
#include "extract.h"

#define MSDP_TYPE_MAX	7

void
msdp_print(netdissect_options *ndo, const u_char *sp, u_int length)
{
	unsigned int type, len;

	ND_TCHECK2(*sp, 3);
	/* See if we think we're at the beginning of a compound packet */
	type = *sp;
	len = EXTRACT_16BITS(sp + 1);
	if (len > 1500 || len < 3 || type == 0 || type > MSDP_TYPE_MAX)
		goto trunc;	/* not really truncated, but still not decodable */
	ND_PRINT((ndo, " msdp:"));
	while (length > 0) {
		ND_TCHECK2(*sp, 3);
		type = *sp;
		len = EXTRACT_16BITS(sp + 1);
		if (len > 1400 || ndo->ndo_vflag)
			ND_PRINT((ndo, " [len %u]", len));
		if (len < 3)
			goto trunc;
		sp += 3;
		length -= 3;
		switch (type) {
		case 1:	/* IPv4 Source-Active */
		case 3: /* IPv4 Source-Active Response */
			if (type == 1)
				ND_PRINT((ndo, " SA"));
			else
				ND_PRINT((ndo, " SA-Response"));
			ND_TCHECK(*sp);
			ND_PRINT((ndo, " %u entries", *sp));
			if ((u_int)((*sp * 12) + 8) < len) {
				ND_PRINT((ndo, " [w/data]"));
				if (ndo->ndo_vflag > 1) {
					ND_PRINT((ndo, " "));
					ip_print(ndo, sp + *sp * 12 + 8 - 3,
					         len - (*sp * 12 + 8));
				}
			}
			break;
		case 2:
			ND_PRINT((ndo, " SA-Request"));
			ND_TCHECK2(*sp, 5);
			ND_PRINT((ndo, " for %s", ipaddr_string(ndo, sp + 1)));
			break;
		case 4:
			ND_PRINT((ndo, " Keepalive"));
			if (len != 3)
				ND_PRINT((ndo, "[len=%d] ", len));
			break;
		case 5:
			ND_PRINT((ndo, " Notification"));
			break;
		default:
			ND_PRINT((ndo, " [type=%d len=%d]", type, len));
			break;
		}
		sp += (len - 3);
		length -= (len - 3);
	}
	return;
trunc:
	ND_PRINT((ndo, " [|msdp]"));
}

/*
 * Local Variables:
 * c-style: whitesmith
 * c-basic-offset: 8
 * End:
 */
