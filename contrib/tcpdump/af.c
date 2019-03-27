/*
 * Copyright (c) 1998-2006 The TCPDUMP project
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
 * Original code by Hannes Gredler (hannes@gredler.at)
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <netdissect-stdinc.h>
#include "netdissect.h"
#include "af.h"

const struct tok af_values[] = {
    { 0,                      "Reserved"},
    { AFNUM_INET,             "IPv4"},
    { AFNUM_INET6,            "IPv6"},
    { AFNUM_NSAP,             "NSAP"},
    { AFNUM_HDLC,             "HDLC"},
    { AFNUM_BBN1822,          "BBN 1822"},
    { AFNUM_802,              "802"},
    { AFNUM_E163,             "E.163"},
    { AFNUM_E164,             "E.164"},
    { AFNUM_F69,              "F.69"},
    { AFNUM_X121,             "X.121"},
    { AFNUM_IPX,              "Novell IPX"},
    { AFNUM_ATALK,            "Appletalk"},
    { AFNUM_DECNET,           "Decnet IV"},
    { AFNUM_BANYAN,           "Banyan Vines"},
    { AFNUM_E164NSAP,         "E.164 with NSAP subaddress"},
    { AFNUM_L2VPN,            "Layer-2 VPN"},
    { AFNUM_VPLS,             "VPLS"},
    { 0, NULL},
};

const struct tok bsd_af_values[] = {
    { BSD_AFNUM_INET, "IPv4" },
    { BSD_AFNUM_NS, "NS" },
    { BSD_AFNUM_ISO, "ISO" },
    { BSD_AFNUM_APPLETALK, "Appletalk" },
    { BSD_AFNUM_IPX, "IPX" },
    { BSD_AFNUM_INET6_BSD, "IPv6" },
    { BSD_AFNUM_INET6_FREEBSD, "IPv6" },
    { BSD_AFNUM_INET6_DARWIN, "IPv6" },
    { 0, NULL}
};
