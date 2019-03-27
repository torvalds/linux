/* Copyright (c) 2015, bugyo
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* \summary: Generic Protocol Extension for VXLAN (VXLAN GPE) printer */

/* specification: draft-ietf-nvo3-vxlan-gpe-01 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <netdissect-stdinc.h>

#include "netdissect.h"
#include "extract.h"

static const char tstr[] = " [|VXLAN-GPE]";
static const struct tok vxlan_gpe_flags [] = {
    { 0x08, "I" },
    { 0x04, "P" },
    { 0x01, "O" },
    { 0, NULL }
};

#define VXLAN_GPE_HDR_LEN 8

/*
 * VXLAN GPE header, draft-ietf-nvo3-vxlan-gpe-01
 *                   Generic Protocol Extension for VXLAN
 *
 *     0                   1                   2                   3
 *     0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    |R|R|Ver|I|P|R|O|       Reserved                |Next Protocol  |
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    |                VXLAN Network Identifier (VNI) |   Reserved    |
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 */

void
vxlan_gpe_print(netdissect_options *ndo, const u_char *bp, u_int len)
{
    uint8_t flags;
    uint8_t next_protocol;
    uint32_t vni;

    if (len < VXLAN_GPE_HDR_LEN)
        goto trunc;

    ND_TCHECK2(*bp, VXLAN_GPE_HDR_LEN);

    flags = *bp;
    bp += 3;

    next_protocol = *bp;
    bp += 1;

    vni = EXTRACT_24BITS(bp);
    bp += 4;

    ND_PRINT((ndo, "VXLAN-GPE, "));
    ND_PRINT((ndo, "flags [%s], ",
              bittok2str_nosep(vxlan_gpe_flags, "none", flags)));
    ND_PRINT((ndo, "vni %u", vni));
    ND_PRINT((ndo, ndo->ndo_vflag ? "\n    " : ": "));

    switch (next_protocol) {
    case 0x1:
        ip_print(ndo, bp, len - 8);
        break;
    case 0x2:
        ip6_print(ndo, bp, len - 8);
        break;
    case 0x3:
        ether_print(ndo, bp, len - 8, ndo->ndo_snapend - bp, NULL, NULL);
        break;
    case 0x4:
        nsh_print(ndo, bp, len - 8);
        break;
    case 0x5:
        mpls_print(ndo, bp, len - 8);
        break;
    default:
        ND_PRINT((ndo, "ERROR: unknown-next-protocol"));
        return;
    }

	return;

trunc:
	ND_PRINT((ndo, "%s", tstr));
}

