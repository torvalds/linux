/*
 * Copyright (c) 2014 VMware, Inc. All Rights Reserved.
 *
 * Jesse Gross <jesse@nicira.com>
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
 */

/* \summary: Generic Network Virtualization Encapsulation (Geneve) printer */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <netdissect-stdinc.h>

#include "netdissect.h"
#include "extract.h"
#include "ethertype.h"

/*
 * Geneve header, draft-ietf-nvo3-geneve
 *
 *    0                   1                   2                   3
 *    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    |Ver|  Opt Len  |O|C|    Rsvd.  |          Protocol Type        |
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    |        Virtual Network Identifier (VNI)       |    Reserved   |
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    |                    Variable Length Options                    |
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 * Options:
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    |          Option Class         |      Type     |R|R|R| Length  |
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *    |                      Variable Option Data                     |
 *    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 */

#define VER_SHIFT 6
#define HDR_OPTS_LEN_MASK 0x3F

#define FLAG_OAM      (1 << 7)
#define FLAG_CRITICAL (1 << 6)
#define FLAG_R1       (1 << 5)
#define FLAG_R2       (1 << 4)
#define FLAG_R3       (1 << 3)
#define FLAG_R4       (1 << 2)
#define FLAG_R5       (1 << 1)
#define FLAG_R6       (1 << 0)

#define OPT_TYPE_CRITICAL (1 << 7)
#define OPT_LEN_MASK 0x1F

static const struct tok geneve_flag_values[] = {
        { FLAG_OAM, "O" },
        { FLAG_CRITICAL, "C" },
        { FLAG_R1, "R1" },
        { FLAG_R2, "R2" },
        { FLAG_R3, "R3" },
        { FLAG_R4, "R4" },
        { FLAG_R5, "R5" },
        { FLAG_R6, "R6" },
        { 0, NULL }
};

static const char *
format_opt_class(uint16_t opt_class)
{
    switch (opt_class) {
    case 0x0100:
        return "Linux";
    case 0x0101:
        return "Open vSwitch";
    case 0x0102:
        return "Open Virtual Networking (OVN)";
    case 0x0103:
        return "In-band Network Telemetry (INT)";
    case 0x0104:
        return "VMware";
    default:
        if (opt_class <= 0x00ff)
            return "Standard";
        else if (opt_class >= 0xfff0)
            return "Experimental";
    }

    return "Unknown";
}

static void
geneve_opts_print(netdissect_options *ndo, const u_char *bp, u_int len)
{
    const char *sep = "";

    while (len > 0) {
        uint16_t opt_class;
        uint8_t opt_type;
        uint8_t opt_len;

        ND_PRINT((ndo, "%s", sep));
        sep = ", ";

        opt_class = EXTRACT_16BITS(bp);
        opt_type = *(bp + 2);
        opt_len = 4 + ((*(bp + 3) & OPT_LEN_MASK) * 4);

        ND_PRINT((ndo, "class %s (0x%x) type 0x%x%s len %u",
                  format_opt_class(opt_class), opt_class, opt_type,
                  opt_type & OPT_TYPE_CRITICAL ? "(C)" : "", opt_len));

        if (opt_len > len) {
            ND_PRINT((ndo, " [bad length]"));
            return;
        }

        if (ndo->ndo_vflag > 1 && opt_len > 4) {
            const uint32_t *data = (const uint32_t *)(bp + 4);
            int i;

            ND_PRINT((ndo, " data"));

            for (i = 4; i < opt_len; i += 4) {
                ND_PRINT((ndo, " %08x", EXTRACT_32BITS(data)));
                data++;
            }
        }

        bp += opt_len;
        len -= opt_len;
    }
}

void
geneve_print(netdissect_options *ndo, const u_char *bp, u_int len)
{
    uint8_t ver_opt;
    u_int version;
    uint8_t flags;
    uint16_t prot;
    uint32_t vni;
    uint8_t reserved;
    u_int opts_len;

    ND_PRINT((ndo, "Geneve"));

    ND_TCHECK2(*bp, 8);

    ver_opt = *bp;
    bp += 1;
    len -= 1;

    version = ver_opt >> VER_SHIFT;
    if (version != 0) {
        ND_PRINT((ndo, " ERROR: unknown-version %u", version));
        return;
    }

    flags = *bp;
    bp += 1;
    len -= 1;

    prot = EXTRACT_16BITS(bp);
    bp += 2;
    len -= 2;

    vni = EXTRACT_24BITS(bp);
    bp += 3;
    len -= 3;

    reserved = *bp;
    bp += 1;
    len -= 1;

    ND_PRINT((ndo, ", Flags [%s]",
              bittok2str_nosep(geneve_flag_values, "none", flags)));
    ND_PRINT((ndo, ", vni 0x%x", vni));

    if (reserved)
        ND_PRINT((ndo, ", rsvd 0x%x", reserved));

    if (ndo->ndo_eflag)
        ND_PRINT((ndo, ", proto %s (0x%04x)",
                  tok2str(ethertype_values, "unknown", prot), prot));

    opts_len = (ver_opt & HDR_OPTS_LEN_MASK) * 4;

    if (len < opts_len) {
        ND_PRINT((ndo, " truncated-geneve - %u bytes missing",
                  opts_len - len));
        return;
    }

    ND_TCHECK2(*bp, opts_len);

    if (opts_len > 0) {
        ND_PRINT((ndo, ", options ["));

        if (ndo->ndo_vflag)
            geneve_opts_print(ndo, bp, opts_len);
        else
            ND_PRINT((ndo, "%u bytes", opts_len));

        ND_PRINT((ndo, "]"));
    }

    bp += opts_len;
    len -= opts_len;

    if (ndo->ndo_vflag < 1)
        ND_PRINT((ndo, ": "));
    else
        ND_PRINT((ndo, "\n\t"));

    if (ethertype_print(ndo, prot, bp, len, ndo->ndo_snapend - bp, NULL, NULL) == 0) {
        if (prot == ETHERTYPE_TEB)
            ether_print(ndo, bp, len, ndo->ndo_snapend - bp, NULL, NULL);
        else
            ND_PRINT((ndo, "geneve-proto-0x%x", prot));
    }

    return;

trunc:
    ND_PRINT((ndo, " [|geneve]"));
}
