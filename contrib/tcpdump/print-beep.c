/*
 * Copyright (C) 2000, Richard Sharpe
 *
 * This software may be distributed either under the terms of the
 * BSD-style license that accompanies tcpdump or under the GNU GPL
 * version 2 or later.
 *
 * print-beep.c
 *
 */

/* \summary: Blocks Extensible Exchange Protocol (BEEP) printer */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <netdissect-stdinc.h>

#include <string.h>

#include "netdissect.h"

/* Check for a string but not go beyond length
 * Return TRUE on match, FALSE otherwise
 *
 * Looks at the first few chars up to tl1 ...
 */

static int
l_strnstart(netdissect_options *ndo, const char *tstr1, u_int tl1,
    const char *str2, u_int l2)
{
	if (!ND_TTEST2(*str2, tl1)) {
		/*
		 * We don't have tl1 bytes worth of captured data
		 * for the string, so we can't check for this
		 * string.
		 */
		return 0;
	}
	if (tl1 > l2)
		return 0;

	return (strncmp(tstr1, str2, tl1) == 0 ? 1 : 0);
}

void
beep_print(netdissect_options *ndo, const u_char *bp, u_int length)
{

	if (l_strnstart(ndo, "MSG", 4, (const char *)bp, length)) /* A REQuest */
		ND_PRINT((ndo, " BEEP MSG"));
	else if (l_strnstart(ndo, "RPY ", 4, (const char *)bp, length))
		ND_PRINT((ndo, " BEEP RPY"));
	else if (l_strnstart(ndo, "ERR ", 4, (const char *)bp, length))
		ND_PRINT((ndo, " BEEP ERR"));
	else if (l_strnstart(ndo, "ANS ", 4, (const char *)bp, length))
		ND_PRINT((ndo, " BEEP ANS"));
	else if (l_strnstart(ndo, "NUL ", 4, (const char *)bp, length))
		ND_PRINT((ndo, " BEEP NUL"));
	else if (l_strnstart(ndo, "SEQ ", 4, (const char *)bp, length))
		ND_PRINT((ndo, " BEEP SEQ"));
	else if (l_strnstart(ndo, "END", 4, (const char *)bp, length))
		ND_PRINT((ndo, " BEEP END"));
	else
		ND_PRINT((ndo, " BEEP (payload or undecoded)"));
}
