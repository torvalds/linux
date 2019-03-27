/*
 * Copyright (c) 1982, 1986, 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * From:
 *	@(#)in.h	8.3 (Berkeley) 1/3/94
 * $FreeBSD: projects/clang400-import/contrib/tcpdump/ipproto.h 276788 2015-01-07 19:55:18Z delphij $
 */

extern const struct tok ipproto_values[];
extern const char *netdb_protoname (const nd_uint8_t);

#ifndef IPPROTO_IP
#define	IPPROTO_IP		0		/* dummy for IP */
#endif
#ifndef IPPROTO_HOPOPTS
#define IPPROTO_HOPOPTS		0		/* IPv6 hop-by-hop options */
#endif
#ifndef IPPROTO_ICMP
#define	IPPROTO_ICMP		1		/* control message protocol */
#endif
#ifndef IPPROTO_IGMP
#define	IPPROTO_IGMP		2		/* group mgmt protocol */
#endif
#ifndef IPPROTO_IPV4
#define IPPROTO_IPV4		4
#endif
#ifndef IPPROTO_TCP
#define	IPPROTO_TCP		6		/* tcp */
#endif
#ifndef IPPROTO_EGP
#define	IPPROTO_EGP		8		/* exterior gateway protocol */
#endif
#ifndef IPPROTO_PIGP
#define IPPROTO_PIGP		9
#endif
#ifndef IPPROTO_UDP
#define	IPPROTO_UDP		17		/* user datagram protocol */
#endif
#ifndef IPPROTO_DCCP
#define	IPPROTO_DCCP		33		/* datagram congestion control protocol */
#endif
#ifndef IPPROTO_IPV6
#define IPPROTO_IPV6		41
#endif
#ifndef IPPROTO_ROUTING
#define IPPROTO_ROUTING		43		/* IPv6 routing header */
#endif
#ifndef IPPROTO_FRAGMENT
#define IPPROTO_FRAGMENT	44		/* IPv6 fragmentation header */
#endif
#ifndef IPPROTO_RSVP
#define IPPROTO_RSVP		46 		/* resource reservation */
#endif
#ifndef IPPROTO_GRE
#define	IPPROTO_GRE		47		/* General Routing Encap. */
#endif
#ifndef IPPROTO_ESP
#define	IPPROTO_ESP		50		/* SIPP Encap Sec. Payload */
#endif
#ifndef IPPROTO_AH
#define	IPPROTO_AH		51		/* SIPP Auth Header */
#endif
#ifndef IPPROTO_MOBILE
#define IPPROTO_MOBILE		55
#endif
#ifndef IPPROTO_ICMPV6
#define IPPROTO_ICMPV6		58		/* ICMPv6 */
#endif
#ifndef IPPROTO_NONE
#define IPPROTO_NONE		59		/* IPv6 no next header */
#endif
#ifndef IPPROTO_DSTOPTS
#define IPPROTO_DSTOPTS		60		/* IPv6 destination options */
#endif
#ifndef IPPROTO_MOBILITY_OLD
/*
 * The current Protocol Numbers list says that the IP protocol number for
 * mobility headers is 135; it cites draft-ietf-mobileip-ipv6-24, but
 * that draft doesn't actually give a number.
 *
 * It appears that 62 used to be used, even though that's assigned to
 * a protocol called CFTP; however, the only reference for CFTP is a
 * Network Message from BBN back in 1982, so, for now, we support 62,
 * as well as 135, as a protocol number for mobility headers.
 */
#define IPPROTO_MOBILITY_OLD	62
#endif
#ifndef IPPROTO_ND
#define	IPPROTO_ND		77		/* Sun net disk proto (temp.) */
#endif
#ifndef IPPROTO_EIGRP
#define	IPPROTO_EIGRP		88		/* Cisco/GXS IGRP */
#endif
#ifndef IPPROTO_OSPF
#define IPPROTO_OSPF		89
#endif
#ifndef IPPROTO_PIM
#define IPPROTO_PIM		103
#endif
#ifndef IPPROTO_IPCOMP
#define IPPROTO_IPCOMP		108
#endif
#ifndef IPPROTO_VRRP
#define IPPROTO_VRRP		112
#endif
#ifndef IPPROTO_CARP
#define IPPROTO_CARP		112
#endif
#ifndef IPPROTO_PGM
#define IPPROTO_PGM             113
#endif
#ifndef IPPROTO_SCTP
#define IPPROTO_SCTP		132
#endif
#ifndef IPPROTO_MOBILITY
#define IPPROTO_MOBILITY	135
#endif
