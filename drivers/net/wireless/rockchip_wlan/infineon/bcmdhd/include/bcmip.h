/*
 * Fundamental constants relating to IP Protocol
 *
 * Portions of this code are copyright (c) 2022 Cypress Semiconductor Corporation
 *
 * Copyright (C) 1999-2017, Broadcom Corporation
 *
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 *
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 *
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id$
 */

#ifndef _bcmip_h_
#define _bcmip_h_

#ifndef _TYPEDEFS_H_
#include <typedefs.h>
#endif // endif

/* This marks the start of a packed structure section. */
#include <packed_section_start.h>

/* IPV4 and IPV6 common */
#define IP_VER_OFFSET		0x0	/* offset to version field */
#define IP_VER_MASK		0xf0	/* version mask */
#define IP_VER_SHIFT		4	/* version shift */
#define IP_VER_4		4	/* version number for IPV4 */
#define IP_VER_6		6	/* version number for IPV6 */

#define IP_VER(ip_body) \
	((((uint8 *)(ip_body))[IP_VER_OFFSET] & IP_VER_MASK) >> IP_VER_SHIFT)

#define IP_PROT_ICMP		0x1	/* ICMP protocol */
#define IP_PROT_IGMP		0x2	/* IGMP protocol */
#define IP_PROT_TCP		0x6	/* TCP protocol */
#define IP_PROT_UDP		0x11	/* UDP protocol type */
#define IP_PROT_GRE		0x2f	/* GRE protocol type */
#define IP_PROT_ICMP6           0x3a    /* ICMPv6 protocol type */

/* IPV4 field offsets */
#define IPV4_VER_HL_OFFSET      0       /* version and ihl byte offset */
#define IPV4_TOS_OFFSET         1       /* type of service offset */
#define IPV4_PKTLEN_OFFSET      2       /* packet length offset */
#define IPV4_PKTFLAG_OFFSET     6       /* more-frag,dont-frag flag offset */
#define IPV4_PROT_OFFSET        9       /* protocol type offset */
#define IPV4_CHKSUM_OFFSET      10      /* IP header checksum offset */
#define IPV4_SRC_IP_OFFSET      12      /* src IP addr offset */
#define IPV4_DEST_IP_OFFSET     16      /* dest IP addr offset */
#define IPV4_OPTIONS_OFFSET     20      /* IP options offset */
#define IPV4_MIN_HEADER_LEN     20      /* Minimum size for an IP header (no options) */

/* IPV4 field decodes */
#define IPV4_VER_MASK		0xf0	/* IPV4 version mask */
#define IPV4_VER_SHIFT		4	/* IPV4 version shift */

#define IPV4_HLEN_MASK		0x0f	/* IPV4 header length mask */
#define IPV4_HLEN(ipv4_body)	(4 * (((uint8 *)(ipv4_body))[IPV4_VER_HL_OFFSET] & IPV4_HLEN_MASK))

#define IPV4_HLEN_MIN		(4 * 5)	/* IPV4 header minimum length */

#define IPV4_ADDR_LEN		4	/* IPV4 address length */

#define IPV4_ADDR_NULL(a)	((((uint8 *)(a))[0] | ((uint8 *)(a))[1] | \
				  ((uint8 *)(a))[2] | ((uint8 *)(a))[3]) == 0)

#define IPV4_ADDR_BCAST(a)	((((uint8 *)(a))[0] & ((uint8 *)(a))[1] & \
				  ((uint8 *)(a))[2] & ((uint8 *)(a))[3]) == 0xff)

#define	IPV4_TOS_DSCP_MASK	0xfc	/* DiffServ codepoint mask */
#define	IPV4_TOS_DSCP_SHIFT	2	/* DiffServ codepoint shift */

#define	IPV4_TOS(ipv4_body)	(((uint8 *)(ipv4_body))[IPV4_TOS_OFFSET])

#define	IPV4_TOS_PREC_MASK	0xe0	/* Historical precedence mask */
#define	IPV4_TOS_PREC_SHIFT	5	/* Historical precedence shift */

#define IPV4_TOS_LOWDELAY	0x10	/* Lowest delay requested */
#define IPV4_TOS_THROUGHPUT	0x8	/* Best throughput requested */
#define IPV4_TOS_RELIABILITY	0x4	/* Most reliable delivery requested */

#define IPV4_TOS_ROUTINE        0
#define IPV4_TOS_PRIORITY       1
#define IPV4_TOS_IMMEDIATE      2
#define IPV4_TOS_FLASH          3
#define IPV4_TOS_FLASHOVERRIDE  4
#define IPV4_TOS_CRITICAL       5
#define IPV4_TOS_INETWORK_CTRL  6
#define IPV4_TOS_NETWORK_CTRL   7

#define IPV4_PROT(ipv4_body)	(((uint8 *)(ipv4_body))[IPV4_PROT_OFFSET])

#define IPV4_FRAG_RESV		0x8000	/* Reserved */
#define IPV4_FRAG_DONT		0x4000	/* Don't fragment */
#define IPV4_FRAG_MORE		0x2000	/* More fragments */
#define IPV4_FRAG_OFFSET_MASK	0x1fff	/* Fragment offset */

#define IPV4_ADDR_STR_LEN	16	/* Max IP address length in string format */

/* IPV4 packet formats */
BWL_PRE_PACKED_STRUCT struct ipv4_addr {
	uint8	addr[IPV4_ADDR_LEN];
} BWL_POST_PACKED_STRUCT;

BWL_PRE_PACKED_STRUCT struct ipv4_hdr {
	uint8	version_ihl;		/* Version and Internet Header Length */
	uint8	tos;			/* Type Of Service */
	uint16	tot_len;		/* Number of bytes in packet (max 65535) */
	uint16	id;
	uint16	frag;			/* 3 flag bits and fragment offset */
	uint8	ttl;			/* Time To Live */
	uint8	prot;			/* Protocol */
	uint16	hdr_chksum;		/* IP header checksum */
	uint8	src_ip[IPV4_ADDR_LEN];	/* Source IP Address */
	uint8	dst_ip[IPV4_ADDR_LEN];	/* Destination IP Address */
} BWL_POST_PACKED_STRUCT;

/* IPV6 field offsets */
#define IPV6_PAYLOAD_LEN_OFFSET	4	/* payload length offset */
#define IPV6_NEXT_HDR_OFFSET	6	/* next header/protocol offset */
#define IPV6_HOP_LIMIT_OFFSET	7	/* hop limit offset */
#define IPV6_SRC_IP_OFFSET	8	/* src IP addr offset */
#define IPV6_DEST_IP_OFFSET	24	/* dst IP addr offset */

/* IPV6 field decodes */
#define IPV6_TRAFFIC_CLASS(ipv6_body) \
	(((((uint8 *)(ipv6_body))[0] & 0x0f) << 4) | \
	 ((((uint8 *)(ipv6_body))[1] & 0xf0) >> 4))

#define IPV6_FLOW_LABEL(ipv6_body) \
	(((((uint8 *)(ipv6_body))[1] & 0x0f) << 16) | \
	 (((uint8 *)(ipv6_body))[2] << 8) | \
	 (((uint8 *)(ipv6_body))[3]))

#define IPV6_PAYLOAD_LEN(ipv6_body) \
	((((uint8 *)(ipv6_body))[IPV6_PAYLOAD_LEN_OFFSET + 0] << 8) | \
	 ((uint8 *)(ipv6_body))[IPV6_PAYLOAD_LEN_OFFSET + 1])

#define IPV6_NEXT_HDR(ipv6_body) \
	(((uint8 *)(ipv6_body))[IPV6_NEXT_HDR_OFFSET])

#define IPV6_PROT(ipv6_body)	IPV6_NEXT_HDR(ipv6_body)

#define IPV6_ADDR_LEN		16	/* IPV6 address length */

/* IPV4 TOS or IPV6 Traffic Classifier or 0 */
#define IP_TOS46(ip_body) \
	(IP_VER(ip_body) == IP_VER_4 ? IPV4_TOS(ip_body) : \
	 IP_VER(ip_body) == IP_VER_6 ? IPV6_TRAFFIC_CLASS(ip_body) : 0)

#define IP_DSCP46(ip_body) (IP_TOS46(ip_body) >> IPV4_TOS_DSCP_SHIFT);

/* IPV4 or IPV6 Protocol Classifier or 0 */
#define IP_PROT46(ip_body) \
	(IP_VER(ip_body) == IP_VER_4 ? IPV4_PROT(ip_body) : \
	 IP_VER(ip_body) == IP_VER_6 ? IPV6_PROT(ip_body) : 0)

/* IPV6 extension headers (options) */
#define IPV6_EXTHDR_HOP		0
#define IPV6_EXTHDR_ROUTING	43
#define IPV6_EXTHDR_FRAGMENT	44
#define IPV6_EXTHDR_AUTH	51
#define IPV6_EXTHDR_NONE	59
#define IPV6_EXTHDR_DEST	60

#define IPV6_EXTHDR(prot)	(((prot) == IPV6_EXTHDR_HOP) || \
	                         ((prot) == IPV6_EXTHDR_ROUTING) || \
	                         ((prot) == IPV6_EXTHDR_FRAGMENT) || \
	                         ((prot) == IPV6_EXTHDR_AUTH) || \
	                         ((prot) == IPV6_EXTHDR_NONE) || \
	                         ((prot) == IPV6_EXTHDR_DEST))

#define IPV6_MIN_HLEN 		40

#define IPV6_EXTHDR_LEN(eh)	((((struct ipv6_exthdr *)(eh))->hdrlen + 1) << 3)

BWL_PRE_PACKED_STRUCT struct ipv6_exthdr {
	uint8	nexthdr;
	uint8	hdrlen;
} BWL_POST_PACKED_STRUCT;

BWL_PRE_PACKED_STRUCT struct ipv6_exthdr_frag {
	uint8	nexthdr;
	uint8	rsvd;
	uint16	frag_off;
	uint32	ident;
} BWL_POST_PACKED_STRUCT;

static INLINE int32
ipv6_exthdr_len(uint8 *h, uint8 *proto)
{
	uint16 len = 0, hlen;
	struct ipv6_exthdr *eh = (struct ipv6_exthdr *)h;

	while (IPV6_EXTHDR(eh->nexthdr)) {
		if (eh->nexthdr == IPV6_EXTHDR_NONE)
			return -1;
		else if (eh->nexthdr == IPV6_EXTHDR_FRAGMENT)
			hlen = 8;
		else if (eh->nexthdr == IPV6_EXTHDR_AUTH)
			hlen = (eh->hdrlen + 2) << 2;
		else
			hlen = IPV6_EXTHDR_LEN(eh);

		len += hlen;
		eh = (struct ipv6_exthdr *)(h + len);
	}

	*proto = eh->nexthdr;
	return len;
}

#define IPV4_ISMULTI(a) (((a) & 0xf0000000) == 0xe0000000)

#define IPV4_MCAST_TO_ETHER_MCAST(ipv4, ether) \
{ \
	ether[0] = 0x01; \
	ether[1] = 0x00; \
	ether[2] = 0x5E; \
	ether[3] = (ipv4 & 0x7f0000) >> 16; \
	ether[4] = (ipv4 & 0xff00) >> 8; \
	ether[5] = (ipv4 & 0xff); \
}

/* This marks the end of a packed structure section. */
#include <packed_section_end.h>

#define IPV4_ADDR_STR "%d.%d.%d.%d"
#define IPV4_ADDR_TO_STR(addr)	((uint32)addr & 0xff000000) >> 24, \
								((uint32)addr & 0x00ff0000) >> 16, \
								((uint32)addr & 0x0000ff00) >> 8, \
								((uint32)addr & 0x000000ff)

#define IPCOPY(s, d) \
do { \
	((uint16 *)(d))[0] = ((const uint16 *)(s))[0]; \
	((uint16 *)(d))[1] = ((const uint16 *)(s))[1]; \
} while (0)
#endif	/* _bcmip_h_ */
