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
 * Copyright (c) 2018 Michael Tuexen <tuexen@FreeBSD.org>
 */

#pragma D depends_on library ip.d
#pragma D depends_on module kernel
#pragma D depends_on provider udplite

/*
 * udplitesinfo contains stable UDPLite details.
 */
typedef struct udplitesinfo {
	uintptr_t udplites_addr;
	uint16_t udplites_lport;	/* local port */
	uint16_t udplites_rport;	/* remote port */
	string udplites_laddr;		/* local address, as a string */
	string udplites_raddr;		/* remote address, as a string */
} udplitesinfo_t;

/*
 * udpliteinfo is the UDPLite header fields.
 */
typedef struct udpliteinfo {
	uint16_t udplite_sport;		/* source port */
	uint16_t udplite_dport;		/* destination port */
	uint16_t udplite_coverage;	/* checksum coverage */
	uint16_t udplite_checksum;	/* headers + data checksum */
	struct udplitehdr *udplite_hdr;	/* raw UDPLite header */
} udpliteinfo_t;

#pragma D binding "1.13" translator
translator udplitesinfo_t < struct inpcb *p > {
	udplites_addr =	(uintptr_t)p;
	udplites_lport =	p == NULL ? 0 : ntohs(p->inp_inc.inc_ie.ie_lport);
	udplites_rport =	p == NULL ? 0 : ntohs(p->inp_inc.inc_ie.ie_fport);
	udplites_laddr =	p == NULL ? "<unknown>" :
	    p->inp_vflag == INP_IPV4 ?
	    inet_ntoa(&p->inp_inc.inc_ie.ie_dependladdr.id46_addr.ia46_addr4.s_addr) :
	    inet_ntoa6(&p->inp_inc.inc_ie.ie_dependladdr.id6_addr);
	udplites_raddr =	p == NULL ? "<unknown>" :
	    p->inp_vflag == INP_IPV4 ?
	    inet_ntoa(&p->inp_inc.inc_ie.ie_dependfaddr.id46_addr.ia46_addr4.s_addr) :
	    inet_ntoa6(&p->inp_inc.inc_ie.ie_dependfaddr.id6_addr);
};

#pragma D binding "1.13" translator
translator udpliteinfo_t < struct udphdr *p > {
	udplite_sport =		p == NULL ? 0 : ntohs(p->uh_sport);
	udplite_dport =		p == NULL ? 0 : ntohs(p->uh_dport);
	udplite_coverage =	p == NULL ? 0 : ntohs(p->uh_ulen);
	udplite_checksum =	p == NULL ? 0 : ntohs(p->uh_sum);
	udplite_hdr =		(struct udplitehdr *)p;
};
