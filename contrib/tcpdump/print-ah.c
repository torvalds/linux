/*	$NetBSD: print-ah.c,v 1.4 1996/05/20 00:41:16 fvdl Exp $	*/

/*
 * Copyright (c) 1988, 1989, 1990, 1991, 1992, 1993, 1994
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

/* \summary: IPSEC Authentication Header printer */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <netdissect-stdinc.h>

#include "ah.h"

#include "netdissect.h"
#include "extract.h"

int
ah_print(netdissect_options *ndo, register const u_char *bp)
{
	register const struct ah *ah;
	int sumlen;

	ah = (const struct ah *)bp;

	ND_TCHECK(*ah);

	sumlen = ah->ah_len << 2;

	ND_PRINT((ndo, "AH(spi=0x%08x", EXTRACT_32BITS(&ah->ah_spi)));
	if (ndo->ndo_vflag)
		ND_PRINT((ndo, ",sumlen=%d", sumlen));
	ND_TCHECK_32BITS(ah + 1);
	ND_PRINT((ndo, ",seq=0x%x", EXTRACT_32BITS(ah + 1)));
	if (!ND_TTEST2(*bp, sizeof(struct ah) + sumlen)) {
		ND_PRINT((ndo, "[truncated]):"));
		return -1;
	}
	ND_PRINT((ndo, "): "));

	return sizeof(struct ah) + sumlen;
 trunc:
	ND_PRINT((ndo, "[|AH]"));
	return -1;
}
