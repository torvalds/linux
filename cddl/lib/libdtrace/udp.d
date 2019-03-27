/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 *
 * $FreeBSD$
 */
/*
 * Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2013 Mark Johnston <markj@FreeBSD.org>
 */

#pragma D depends_on library ip.d
#pragma D depends_on module kernel
#pragma D depends_on provider udp

/*
 * udpsinfo contains stable UDP details.
 */
typedef struct udpsinfo {
	uintptr_t udps_addr;
	uint16_t udps_lport;		/* local port */
	uint16_t udps_rport;		/* remote port */
	string udps_laddr;		/* local address, as a string */
	string udps_raddr;		/* remote address, as a string */
} udpsinfo_t;

/*
 * udpinfo is the UDP header fields.
 */
typedef struct udpinfo {
	uint16_t udp_sport;		/* source port */
	uint16_t udp_dport;		/* destination port */
	uint16_t udp_length;		/* total length */
	uint16_t udp_checksum;		/* headers + data checksum */
	struct udphdr *udp_hdr;		/* raw UDP header */
} udpinfo_t;

#pragma D binding "1.6.3" translator
translator udpsinfo_t < struct inpcb *p > {
	udps_addr =	(uintptr_t)p;
	udps_lport =	p == NULL ? 0 : ntohs(p->inp_inc.inc_ie.ie_lport);
	udps_rport =	p == NULL ? 0 : ntohs(p->inp_inc.inc_ie.ie_fport);
	udps_laddr =	p == NULL ? "<unknown>" :
	    p->inp_vflag == INP_IPV4 ?
	    inet_ntoa(&p->inp_inc.inc_ie.ie_dependladdr.id46_addr.ia46_addr4.s_addr) :
	    inet_ntoa6(&p->inp_inc.inc_ie.ie_dependladdr.id6_addr);
	udps_raddr =	p == NULL ? "<unknown>" :
	    p->inp_vflag == INP_IPV4 ?
	    inet_ntoa(&p->inp_inc.inc_ie.ie_dependfaddr.id46_addr.ia46_addr4.s_addr) :
	    inet_ntoa6(&p->inp_inc.inc_ie.ie_dependfaddr.id6_addr);
};

#pragma D binding "1.6.3" translator
translator udpinfo_t < struct udphdr *p > {
	udp_sport =	p == NULL ? 0 : ntohs(p->uh_sport);
	udp_dport =	p == NULL ? 0 : ntohs(p->uh_dport);
	udp_length =	p == NULL ? 0 : ntohs(p->uh_ulen);
	udp_checksum =	p == NULL ? 0 : ntohs(p->uh_sum);
	udp_hdr =	p;
};
