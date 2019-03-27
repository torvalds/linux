/*
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
 * Original code by Francesco Fondelli (francesco dot fondelli, gmail dot com)
 */

/* \summary: Overlay Transport Virtualization (OTV) printer */

/* specification: draft-hasmit-otv-04 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <netdissect-stdinc.h>

#include "netdissect.h"
#include "extract.h"

/*
 * OTV header, draft-hasmit-otv-04
 *
 *     0                   1                   2                   3
 *     0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 *     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *     |R|R|R|R|I|R|R|R|           Overlay ID                          |
 *     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *     |          Instance ID                          | Reserved      |
 *     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 */

void
otv_print(netdissect_options *ndo, const u_char *bp, u_int len)
{
    uint8_t flags;

    ND_PRINT((ndo, "OTV, "));
    if (len < 8)
        goto trunc;

    ND_TCHECK(*bp);
    flags = *bp;
    ND_PRINT((ndo, "flags [%s] (0x%02x), ", flags & 0x08 ? "I" : ".", flags));
    bp += 1;

    ND_TCHECK2(*bp, 3);
    ND_PRINT((ndo, "overlay %u, ", EXTRACT_24BITS(bp)));
    bp += 3;

    ND_TCHECK2(*bp, 3);
    ND_PRINT((ndo, "instance %u\n", EXTRACT_24BITS(bp)));
    bp += 3;

    /* Reserved */
    ND_TCHECK(*bp);
    bp += 1;

    ether_print(ndo, bp, len - 8, ndo->ndo_snapend - bp, NULL, NULL);
    return;

trunc:
    ND_PRINT((ndo, " [|OTV]"));
}
