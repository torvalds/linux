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
 * Copyright (c) 2007, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2013 Mark Johnston <markj@freebsd.org>
 */

#pragma D depends_on module kernel
#pragma D depends_on provider ip

/*
 * pktinfo is where packet ID info can be made available for deeper
 * analysis if packet IDs become supported by the kernel in the future.
 * The pkt_addr member is currently always NULL.
 */
typedef struct pktinfo {
	uintptr_t pkt_addr;
} pktinfo_t;

/*
 * csinfo is where connection state info is made available.
 */
typedef uint32_t zoneid_t;
typedef struct csinfo {
	uintptr_t cs_addr;
	uint64_t cs_cid;
	pid_t cs_pid;
	zoneid_t cs_zoneid;
} csinfo_t;

/*
 * ipinfo contains common IP info for both IPv4 and IPv6.
 */
typedef struct ipinfo {
	uint8_t ip_ver;			/* IP version (4, 6) */
	uint32_t ip_plength;		/* payload length */
	string ip_saddr;		/* source address */
	string ip_daddr;		/* destination address */
} ipinfo_t;

/*
 * ifinfo contains network interface info.
 */
typedef struct ifinfo {
	string if_name;			/* interface name */
	int8_t if_local;		/* is delivered locally */
	/*netstackid_t if_ipstack;*/	/* ipstack ID */
	uintptr_t if_addr;		/* pointer to raw ill_t */
} ifinfo_t;

typedef uint32_t ipaddr_t;
typedef struct {
	uint8_t		ipha_version_and_hdr_length;
	uint8_t		ipha_type_of_service;
	uint16_t	ipha_length;
	uint16_t	ipha_ident;
	uint16_t	ipha_fragment_offset_and_flags;
	uint8_t		ipha_ttl;
	uint8_t		ipha_protocol;
	uint16_t	ipha_hdr_checksum;
	ipaddr_t	ipha_src;
	ipaddr_t	ipha_dst;
} ipha_t;

/*
 * ipv4info is a translated version of the IPv4 header (with raw pointer).
 * These values are NULL if the packet is not IPv4.
 */
typedef struct ipv4info {
	uint8_t ipv4_ver;		/* IP version (4) */
	uint8_t ipv4_ihl;		/* header length, bytes */
	uint8_t ipv4_tos;		/* type of service field */
	uint16_t ipv4_length;		/* length (header + payload) */
	uint16_t ipv4_ident;		/* identification */
	uint8_t ipv4_flags;		/* IP flags */
	uint16_t ipv4_offset;		/* fragment offset */
	uint8_t ipv4_ttl;		/* time to live */
	uint8_t ipv4_protocol;		/* next level protocol */
	string ipv4_protostr;		/* next level protocol, as a string */
	uint16_t ipv4_checksum;		/* header checksum */
	ipaddr_t ipv4_src;		/* source address */
	ipaddr_t ipv4_dst;		/* destination address */
	string ipv4_saddr;		/* source address, string */
	string ipv4_daddr;		/* destination address, string */
	ipha_t *ipv4_hdr;		/* pointer to raw header */
} ipv4info_t;

/*
 * ipv6info is a translated version of the IPv6 header (with raw pointer).
 * These values are NULL if the packet is not IPv6.
 */
typedef struct in6_addr in6_addr_t;
typedef struct ipv6info {
	uint8_t ipv6_ver;		/* IP version (6) */
	uint8_t ipv6_tclass;		/* traffic class */
	uint32_t ipv6_flow;		/* flow label */
	uint16_t ipv6_plen;		/* payload length */
	uint8_t ipv6_nexthdr;		/* next header protocol */
	string ipv6_nextstr;		/* next header protocol, as a string */
	uint8_t ipv6_hlim;		/* hop limit */
	in6_addr_t *ipv6_src;		/* source address */
	in6_addr_t *ipv6_dst;		/* destination address */
	string ipv6_saddr;		/* source address, string */
	string ipv6_daddr;		/* destination address, string */
	struct ip6_hdr *ipv6_hdr;	/* pointer to raw header */
} ipv6info_t;

#pragma D binding "1.5" IPPROTO_IP
inline short IPPROTO_IP =	0;
#pragma D binding "1.5" IPPROTO_ICMP
inline short IPPROTO_ICMP =	1;
#pragma D binding "1.5" IPPROTO_IGMP
inline short IPPROTO_IGMP =	2;
#pragma D binding "1.5" IPPROTO_IPV4
inline short IPPROTO_IPV4 =	4;
#pragma D binding "1.5" IPPROTO_TCP
inline short IPPROTO_TCP =	6;
#pragma D binding "1.5" IPPROTO_UDP
inline short IPPROTO_UDP =	17;
#pragma D binding "1.5" IPPROTO_IPV6
inline short IPPROTO_IPV6 =	41;
#pragma D binding "1.5" IPPROTO_ROUTING
inline short IPPROTO_ROUTING =	43;
#pragma D binding "1.5" IPPROTO_FRAGMENT
inline short IPPROTO_FRAGMENT =	44;
#pragma D binding "1.5" IPPROTO_RSVP
inline short IPPROTO_RSVP =	46;
#pragma D binding "1.5" IPPROTO_GRE
inline short IPPROTO_GRE =	47;
#pragma D binding "1.5" IPPROTO_ESP
inline short IPPROTO_ESP =	50;
#pragma D binding "1.5" IPPROTO_AH
inline short IPPROTO_AH =	51;
#pragma D binding "1.5" IPPROTO_MOBILE
inline short IPPROTO_MOBILE =	55;
#pragma D binding "1.5" IPPROTO_ICMPV6
inline short IPPROTO_ICMPV6 =	58;
#pragma D binding "1.5" IPPROTO_DSTOPTS
inline short IPPROTO_DSTOPTS =	60;
#pragma D binding "1.5" IPPROTO_ETHERIP
inline short IPPROTO_ETHERIP =	97;
#pragma D binding "1.5" IPPROTO_PIM
inline short IPPROTO_PIM =	103;
#pragma D binding "1.5" IPPROTO_IPCOMP
inline short IPPROTO_IPCOMP =	108;
#pragma D binding "1.5" IPPROTO_SCTP
inline short IPPROTO_SCTP =	132;
#pragma D binding "1.5" IPPROTO_RAW
inline short IPPROTO_RAW =	255;
#pragma D binding "1.13" IPPROTO_UDPLITE
inline short IPPROTO_UDPLITE = 	136;

inline uint8_t INP_IPV4	= 0x01;
inline uint8_t INP_IPV6 = 0x02;

#pragma D binding "1.5" protocols
inline string protocols[int proto] =
	proto == IPPROTO_IP ? "IP" :
	proto == IPPROTO_ICMP ? "ICMP" :
	proto == IPPROTO_IGMP ? "IGMP" :
	proto == IPPROTO_IPV4 ? "IPV4" :
	proto == IPPROTO_TCP ? "TCP" :
	proto == IPPROTO_UDP ? "UDP" :
	proto == IPPROTO_IPV6 ? "IPV6" :
	proto == IPPROTO_ROUTING ? "ROUTING" :
	proto == IPPROTO_FRAGMENT ? "FRAGMENT" :
	proto == IPPROTO_RSVP ? "RSVP" :
	proto == IPPROTO_GRE ? "GRE" :
	proto == IPPROTO_ESP ? "ESP" :
	proto == IPPROTO_AH ? "AH" :
	proto == IPPROTO_MOBILE ? "MOBILE" :
	proto == IPPROTO_ICMPV6 ? "ICMPV6" :
	proto == IPPROTO_DSTOPTS ? "DSTOPTS" :
	proto == IPPROTO_ETHERIP ? "ETHERIP" :
	proto == IPPROTO_PIM ? "PIM" :
	proto == IPPROTO_IPCOMP ? "IPCOMP" :
	proto == IPPROTO_SCTP ? "SCTP" :
	proto == IPPROTO_UDPLITE ? "UDPLITE" :
	proto == IPPROTO_RAW ? "RAW" :
	"<unknown>";

/*
 * This field is always NULL according to the current definition of the ip
 * probes.
 */
#pragma D binding "1.5" translator
translator pktinfo_t < void *p > {
	pkt_addr =	NULL;
};

#pragma D binding "1.5" translator
translator csinfo_t < void *p > {
	cs_addr =	NULL;
	cs_cid =	(uint64_t)p;
	cs_pid =	0;
	cs_zoneid =	0;
};

#pragma D binding "1.6.3" translator
translator csinfo_t < struct inpcb *p > {
	cs_addr =	NULL;
	cs_cid =	(uint64_t)p;
	cs_pid =	0;	/* XXX */
	cs_zoneid =	0;
};

#pragma D binding "1.5" translator
translator ipinfo_t < uint8_t *p > {
	ip_ver =	p == NULL ? 0 : ((struct ip *)p)->ip_v;
	ip_plength =	p == NULL ? 0 :
	    ((struct ip *)p)->ip_v == 4 ?
	    ntohs(((struct ip *)p)->ip_len) - (((struct ip *)p)->ip_hl << 2):
	    ntohs(((struct ip6_hdr *)p)->ip6_ctlun.ip6_un1.ip6_un1_plen);
	ip_saddr =	p == NULL ? "<unknown>" :
	    ((struct ip *)p)->ip_v == 4 ?
	    inet_ntoa(&((struct ip *)p)->ip_src.s_addr) :
	    inet_ntoa6(&((struct ip6_hdr *)p)->ip6_src);
	ip_daddr =	p == NULL ? "<unknown>" :
	    ((struct ip *)p)->ip_v == 4 ?
	    inet_ntoa(&((struct ip *)p)->ip_dst.s_addr) :
	    inet_ntoa6(&((struct ip6_hdr *)p)->ip6_dst);
};

#pragma D binding "1.13" translator
translator ipinfo_t < struct mbuf *m > {
	ip_ver =	m == NULL ? 0 : ((struct ip *)m->m_data)->ip_v;
	ip_plength =	m == NULL ? 0 :
	    ((struct ip *)m->m_data)->ip_v == 4 ?
	    ntohs(((struct ip *)m->m_data)->ip_len) - 
			(((struct ip *)m->m_data)->ip_hl << 2):
	    ntohs(((struct ip6_hdr *)m->m_data)->ip6_ctlun.ip6_un1.ip6_un1_plen);
	ip_saddr =	m == NULL ? "<unknown>" :
	    ((struct ip *)m->m_data)->ip_v == 4 ?
	    inet_ntoa(&((struct ip *)m->m_data)->ip_src.s_addr) :
	    inet_ntoa6(&((struct ip6_hdr *)m->m_data)->ip6_src);
	ip_daddr =	m == NULL ? "<unknown>" :
	    ((struct ip *)m->m_data)->ip_v == 4 ?
	    inet_ntoa(&((struct ip *)m->m_data)->ip_dst.s_addr) :
	    inet_ntoa6(&((struct ip6_hdr *)m->m_data)->ip6_dst);
};

#pragma D binding "1.5" IFF_LOOPBACK
inline int IFF_LOOPBACK =	0x8;

#pragma D binding "1.5" translator
translator ifinfo_t < struct ifnet *p > {
	if_name =	p->if_xname;
	if_local =	(p->if_flags & IFF_LOOPBACK) == 0 ? 0 : 1;
	if_addr =	(uintptr_t)p;
};

#pragma D binding "1.5" translator
translator ipv4info_t < struct ip *p > {
	ipv4_ver =	p == NULL ? 0 : p->ip_v;
	ipv4_ihl =	p == NULL ? 0 : p->ip_hl;
	ipv4_tos =	p == NULL ? 0 : p->ip_tos;
	ipv4_length =	p == NULL ? 0 : ntohs(p->ip_len);
	ipv4_ident =	p == NULL ? 0 : ntohs(p->ip_id);
	ipv4_flags =	p == NULL ? 0 : (ntohs(p->ip_off) & 0xe000) >> 8;
	ipv4_offset =	p == NULL ? 0 : ntohs(p->ip_off) & 0x1fff;
	ipv4_ttl =	p == NULL ? 0 : p->ip_ttl;
	ipv4_protocol =	p == NULL ? 0 : p->ip_p;
	ipv4_protostr = p == NULL ? "<null>" : protocols[p->ip_p];
	ipv4_checksum =	p == NULL ? 0 : ntohs(p->ip_sum);
	ipv4_src =	p == NULL ? 0 : (ipaddr_t)ntohl(p->ip_src.s_addr);
	ipv4_dst =	p == NULL ? 0 : (ipaddr_t)ntohl(p->ip_dst.s_addr);
	ipv4_saddr =	p == NULL ? 0 : inet_ntoa(&p->ip_src.s_addr);
	ipv4_daddr =	p == NULL ? 0 : inet_ntoa(&p->ip_dst.s_addr);
	ipv4_hdr =	(ipha_t *)p;
};

#pragma D binding "1.5" translator
translator ipv6info_t < struct ip6_hdr *p > {
	ipv6_ver =	p == NULL ? 0 : (ntohl(p->ip6_ctlun.ip6_un1.ip6_un1_flow) & 0xf0000000) >> 28;
	ipv6_tclass =	p == NULL ? 0 : (ntohl(p->ip6_ctlun.ip6_un1.ip6_un1_flow) & 0x0ff00000) >> 20;
	ipv6_flow =	p == NULL ? 0 : ntohl(p->ip6_ctlun.ip6_un1.ip6_un1_flow) & 0x000fffff;
	ipv6_plen =	p == NULL ? 0 : ntohs(p->ip6_ctlun.ip6_un1.ip6_un1_plen);
	ipv6_nexthdr =	p == NULL ? 0 : p->ip6_ctlun.ip6_un1.ip6_un1_nxt;
	ipv6_nextstr =	p == NULL ? "<null>" : protocols[p->ip6_ctlun.ip6_un1.ip6_un1_nxt];
	ipv6_hlim =	p == NULL ? 0 : p->ip6_ctlun.ip6_un1.ip6_un1_hlim;
	ipv6_src =	p == NULL ? 0 : (in6_addr_t *)&p->ip6_src;
	ipv6_dst =	p == NULL ? 0 : (in6_addr_t *)&p->ip6_dst;
	ipv6_saddr =	p == NULL ? 0 : inet_ntoa6(&p->ip6_src);
	ipv6_daddr =	p == NULL ? 0 : inet_ntoa6(&p->ip6_dst);
	ipv6_hdr =	p;
};
