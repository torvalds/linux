/*	NetBSD: ip6.h,v 1.9 2000/07/13 05:34:21 itojun Exp 	*/
/*	$KAME: ip6.h,v 1.9 2000/07/02 21:01:32 itojun Exp $	*/

/*
 * Copyright (C) 1995, 1996, 1997, and 1998 WIDE Project.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the project nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Copyright (c) 1982, 1986, 1993
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
 *	@(#)ip.h	8.1 (Berkeley) 6/10/93
 */

#ifndef _NETINET_IP6_H_
#define _NETINET_IP6_H_

/*
 * Definition for internet protocol version 6.
 * RFC 2460
 */

struct ip6_hdr {
	union {
		struct ip6_hdrctl {
			uint32_t ip6_un1_flow;	/* 20 bits of flow-ID */
			uint16_t ip6_un1_plen;	/* payload length */
			uint8_t  ip6_un1_nxt;	/* next header */
			uint8_t  ip6_un1_hlim;	/* hop limit */
		} ip6_un1;
		uint8_t ip6_un2_vfc;	/* 4 bits version, top 4 bits class */
	} ip6_ctlun;
	struct in6_addr ip6_src;	/* source address */
	struct in6_addr ip6_dst;	/* destination address */
} UNALIGNED;

#define ip6_vfc		ip6_ctlun.ip6_un2_vfc
#define IP6_VERSION(ip6_hdr)	(((ip6_hdr)->ip6_vfc & 0xf0) >> 4)
#define ip6_flow	ip6_ctlun.ip6_un1.ip6_un1_flow
#define ip6_plen	ip6_ctlun.ip6_un1.ip6_un1_plen
#define ip6_nxt		ip6_ctlun.ip6_un1.ip6_un1_nxt
#define ip6_hlim	ip6_ctlun.ip6_un1.ip6_un1_hlim
#define ip6_hops	ip6_ctlun.ip6_un1.ip6_un1_hlim

/* in network endian */
#define IPV6_FLOWINFO_MASK	((uint32_t)htonl(0x0fffffff))	/* flow info (28 bits) */
#define IPV6_FLOWLABEL_MASK	((uint32_t)htonl(0x000fffff))	/* flow label (20 bits) */
#if 1
/* ECN bits proposed by Sally Floyd */
#define IP6TOS_CE		0x01	/* congestion experienced */
#define IP6TOS_ECT		0x02	/* ECN-capable transport */
#endif

/*
 * Extension Headers
 */

struct	ip6_ext {
	uint8_t ip6e_nxt;
	uint8_t ip6e_len;
} UNALIGNED;

/* Hop-by-Hop options header */
struct ip6_hbh {
	uint8_t ip6h_nxt;	/* next header */
	uint8_t ip6h_len;	/* length in units of 8 octets */
	/* followed by options */
} UNALIGNED;

/* Destination options header */
struct ip6_dest {
	uint8_t ip6d_nxt;	/* next header */
	uint8_t ip6d_len;	/* length in units of 8 octets */
	/* followed by options */
} UNALIGNED;

/* http://www.iana.org/assignments/ipv6-parameters/ipv6-parameters.xhtml */

/* Option types and related macros */
#define IP6OPT_PAD1		0x00	/* 00 0 00000 */
#define IP6OPT_PADN		0x01	/* 00 0 00001 */
#define IP6OPT_JUMBO		0xC2	/* 11 0 00010 = 194 */
#define IP6OPT_JUMBO_LEN	6
#define IP6OPT_RPL		0x63	/* 01 1 00011 */
#define IP6OPT_TUN_ENC_LIMIT	0x04	/* 00 0 00100 */
#define IP6OPT_ROUTER_ALERT	0x05	/* 00 0 00101 */

#define IP6OPT_RTALERT_LEN	4
#define IP6OPT_RTALERT_MLD	0	/* Datagram contains an MLD message */
#define IP6OPT_RTALERT_RSVP	1	/* Datagram contains an RSVP message */
#define IP6OPT_RTALERT_ACTNET	2 	/* contains an Active Networks msg */
#define IP6OPT_MINLEN		2

#define IP6OPT_QUICK_START	0x26	/* 00 1 00110 */
#define IP6OPT_CALIPSO		0x07	/* 00 0 00111 */
#define IP6OPT_SMF_DPD		0x08	/* 00 0 01000 */
#define IP6OPT_HOME_ADDRESS	0xc9	/* 11 0 01001 */
#define IP6OPT_HOMEADDR_MINLEN	18
#define IP6OPT_EID		0x8a	/* 10 0 01010 */
#define IP6OPT_ILNP_NOTICE	0x8b	/* 10 0 01011 */
#define IP6OPT_LINE_ID		0x8c	/* 10 0 01100 */
#define IP6OPT_MPL		0x6d	/* 01 1 01101 */
#define IP6OPT_IP_DFF		0xee	/* 11 1 01110 */

#define IP6OPT_TYPE(o)		((o) & 0xC0)
#define IP6OPT_TYPE_SKIP	0x00
#define IP6OPT_TYPE_DISCARD	0x40
#define IP6OPT_TYPE_FORCEICMP	0x80
#define IP6OPT_TYPE_ICMP	0xC0

#define IP6OPT_MUTABLE		0x20

/* Routing header */
struct ip6_rthdr {
	uint8_t  ip6r_nxt;	/* next header */
	uint8_t  ip6r_len;	/* length in units of 8 octets */
	uint8_t  ip6r_type;	/* routing type */
	uint8_t  ip6r_segleft;	/* segments left */
	/* followed by routing type specific data */
} UNALIGNED;

#define IPV6_RTHDR_TYPE_0 0
#define IPV6_RTHDR_TYPE_2 2

/* Type 0 Routing header */
/* Also used for Type 2 */
struct ip6_rthdr0 {
	nd_uint8_t  ip6r0_nxt;		/* next header */
	nd_uint8_t  ip6r0_len;		/* length in units of 8 octets */
	nd_uint8_t  ip6r0_type;		/* always zero */
	nd_uint8_t  ip6r0_segleft;	/* segments left */
	nd_uint32_t ip6r0_reserved;	/* reserved field */
	struct in6_addr ip6r0_addr[1];	/* up to 23 addresses */
};

/* Fragment header */
struct ip6_frag {
	uint8_t  ip6f_nxt;		/* next header */
	uint8_t  ip6f_reserved;	/* reserved field */
	uint16_t ip6f_offlg;		/* offset, reserved, and flag */
	uint32_t ip6f_ident;		/* identification */
} UNALIGNED;

#define IP6F_OFF_MASK		0xfff8	/* mask out offset from ip6f_offlg */
#define IP6F_RESERVED_MASK	0x0006	/* reserved bits in ip6f_offlg */
#define IP6F_MORE_FRAG		0x0001	/* more-fragments flag */

#endif /* not _NETINET_IP6_H_ */
