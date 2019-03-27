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

extern const struct tok af_values[];
extern const struct tok bsd_af_values[];

/* RFC1700 address family numbers */
#define AFNUM_INET	1
#define AFNUM_INET6	2
#define AFNUM_NSAP	3
#define AFNUM_HDLC	4
#define AFNUM_BBN1822	5
#define AFNUM_802	6
#define AFNUM_E163	7
#define AFNUM_E164	8
#define AFNUM_F69	9
#define AFNUM_X121	10
#define AFNUM_IPX	11
#define AFNUM_ATALK	12
#define AFNUM_DECNET	13
#define AFNUM_BANYAN	14
#define AFNUM_E164NSAP	15
#define AFNUM_VPLS      25
/* draft-kompella-ppvpn-l2vpn */
#define AFNUM_L2VPN     196 /* still to be approved by IANA */

/*
 * BSD AF_ values.
 *
 * Unfortunately, the BSDs don't all use the same value for AF_INET6,
 * so, because we want to be able to read captures from all of the BSDs,
 * we check for all of them.
 */
#define BSD_AFNUM_INET		2
#define BSD_AFNUM_NS		6		/* XEROX NS protocols */
#define BSD_AFNUM_ISO		7
#define BSD_AFNUM_APPLETALK	16
#define BSD_AFNUM_IPX		23
#define BSD_AFNUM_INET6_BSD	24	/* NetBSD, OpenBSD, BSD/OS, Npcap */
#define BSD_AFNUM_INET6_FREEBSD	28	/* FreeBSD */
#define BSD_AFNUM_INET6_DARWIN	30	/* OS X, iOS, other Darwin-based OSes */
