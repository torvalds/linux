/*
 * Copyright (c) 2013 The TCPDUMP project
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that: (1) source code
 * distributions retain the above copyright notice and this paragraph
 * in its entirety, and (2) distributions including binary code include
 * the above copyright notice and this paragraph in its entirety in
 * the documentation or other materials provided with the distribution.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND
 * WITHOUT ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, WITHOUT
 * LIMITATION, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE.
 *
 * Original code by Ola Martin Lykkja (ola.lykkja@q-free.com)
 */

/* \summary: Communication access for land mobiles (CALM) printer */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <netdissect-stdinc.h>

#include "netdissect.h"
#include "addrtoname.h"

/*
   ISO 29281:2009
   Intelligent Transport Systems . Communications access for land mobiles (CALM)
   CALM non-IP networking
*/

/*
 * This is the top level routine of the printer.  'bp' points
 * to the calm header of the packet.
 */
void
calm_fast_print(netdissect_options *ndo, const u_char *bp, u_int length, const struct lladdr_info *src)
{
	int srcNwref;
	int dstNwref;

	ND_TCHECK2(*bp, 2);
	if (length < 2)
		goto trunc;
	srcNwref = bp[0];
	dstNwref = bp[1];
	length -= 2;
	bp += 2;

	ND_PRINT((ndo, "CALM FAST"));
	if (src != NULL)
		ND_PRINT((ndo, " src:%s", (src->addr_string)(ndo, src->addr)));
	ND_PRINT((ndo, "; "));
	ND_PRINT((ndo, "SrcNwref:%d; ", srcNwref));
	ND_PRINT((ndo, "DstNwref:%d; ", dstNwref));

	if (ndo->ndo_vflag)
		ND_DEFAULTPRINT(bp, length);
	return;

trunc:
	ND_PRINT((ndo, "[|calm fast]"));
	return;
}


/*
 * Local Variables:
 * c-style: whitesmith
 * c-basic-offset: 8
 * End:
 */
