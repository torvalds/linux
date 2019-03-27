/*	$OpenBSD: print-cnfp.c,v 1.2 1998/06/25 20:26:59 mickey Exp $	*/

/*
 * Copyright (c) 1998 Michael Shalayeff
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Michael Shalayeff.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* \summary: Cisco NetFlow protocol printer */

/*
 * Cisco NetFlow protocol
 *
 * See
 *
 *    http://www.cisco.com/c/en/us/td/docs/net_mgmt/netflow_collection_engine/3-6/user/guide/format.html#wp1005892
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <netdissect-stdinc.h>

#include <stdio.h>
#include <string.h>

#include "netdissect.h"
#include "addrtoname.h"
#include "extract.h"

#include "tcp.h"
#include "ipproto.h"

struct nfhdr_v1 {
	uint16_t	version;	/* version number */
	uint16_t	count;		/* # of records */
	uint32_t	msys_uptime;
	uint32_t	utc_sec;
	uint32_t	utc_nsec;
};

struct nfrec_v1 {
	struct in_addr	src_ina;
	struct in_addr	dst_ina;
	struct in_addr	nhop_ina;
	uint16_t	input;		/* SNMP index of input interface */
	uint16_t	output;		/* SNMP index of output interface */
	uint32_t	packets;	/* packets in the flow */
	uint32_t	octets;		/* layer 3 octets in the packets of the flow */
	uint32_t	start_time;	/* sys_uptime value at start of flow */
	uint32_t	last_time;	/* sys_uptime value when last packet of flow was received */
	uint16_t	srcport;	/* TCP/UDP source port or equivalent */
	uint16_t	dstport;	/* TCP/UDP source port or equivalent */
	uint16_t	pad1;		/* pad */
	uint8_t		proto;		/* IP protocol type */
	uint8_t		tos;		/* IP type of service */
	uint8_t		tcp_flags;	/* cumulative OR of TCP flags */
	uint8_t		pad[3];		/* padding */
	uint32_t	reserved;	/* unused */
};

struct nfhdr_v5 {
	uint16_t	version;	/* version number */
	uint16_t	count;		/* # of records */
	uint32_t	msys_uptime;
	uint32_t	utc_sec;
	uint32_t	utc_nsec;
	uint32_t	sequence;	/* flow sequence number */
	uint8_t		engine_type;	/* type of flow-switching engine */
	uint8_t		engine_id;	/* slot number of the flow-switching engine */
	uint16_t	sampling_interval; /* sampling mode and interval */
};

struct nfrec_v5 {
	struct in_addr	src_ina;
	struct in_addr	dst_ina;
	struct in_addr	nhop_ina;
	uint16_t	input;		/* SNMP index of input interface */
	uint16_t	output;		/* SNMP index of output interface */
	uint32_t	packets;	/* packets in the flow */
	uint32_t	octets;		/* layer 3 octets in the packets of the flow */
	uint32_t	start_time;	/* sys_uptime value at start of flow */
	uint32_t	last_time;	/* sys_uptime value when last packet of flow was received */
	uint16_t	srcport;	/* TCP/UDP source port or equivalent */
	uint16_t	dstport;	/* TCP/UDP source port or equivalent */
	uint8_t		pad1;		/* pad */
	uint8_t		tcp_flags;	/* cumulative OR of TCP flags */
	uint8_t		proto;		/* IP protocol type */
	uint8_t		tos;		/* IP type of service */
	uint16_t	src_as;		/* AS number of the source */
	uint16_t	dst_as;		/* AS number of the destination */
	uint8_t		src_mask;	/* source address mask bits */
	uint8_t		dst_mask;	/* destination address prefix mask bits */
	uint16_t	pad2;
	struct in_addr	peer_nexthop;	/* v6: IP address of the nexthop within the peer (FIB)*/
};

struct nfhdr_v6 {
	uint16_t	version;	/* version number */
	uint16_t	count;		/* # of records */
	uint32_t	msys_uptime;
	uint32_t	utc_sec;
	uint32_t	utc_nsec;
	uint32_t	sequence;	/* v5 flow sequence number */
	uint32_t	reserved;	/* v5 only */
};

struct nfrec_v6 {
	struct in_addr	src_ina;
	struct in_addr	dst_ina;
	struct in_addr	nhop_ina;
	uint16_t	input;		/* SNMP index of input interface */
	uint16_t	output;		/* SNMP index of output interface */
	uint32_t	packets;	/* packets in the flow */
	uint32_t	octets;		/* layer 3 octets in the packets of the flow */
	uint32_t	start_time;	/* sys_uptime value at start of flow */
	uint32_t	last_time;	/* sys_uptime value when last packet of flow was received */
	uint16_t	srcport;	/* TCP/UDP source port or equivalent */
	uint16_t	dstport;	/* TCP/UDP source port or equivalent */
	uint8_t		pad1;		/* pad */
	uint8_t		tcp_flags;	/* cumulative OR of TCP flags */
	uint8_t		proto;		/* IP protocol type */
	uint8_t		tos;		/* IP type of service */
	uint16_t	src_as;		/* AS number of the source */
	uint16_t	dst_as;		/* AS number of the destination */
	uint8_t		src_mask;	/* source address mask bits */
	uint8_t		dst_mask;	/* destination address prefix mask bits */
	uint16_t	flags;
	struct in_addr	peer_nexthop;	/* v6: IP address of the nexthop within the peer (FIB)*/
};

static void
cnfp_v1_print(netdissect_options *ndo, const u_char *cp)
{
	register const struct nfhdr_v1 *nh;
	register const struct nfrec_v1 *nr;
	const char *p_name;
	int nrecs, ver;
#if 0
	time_t t;
#endif

	nh = (const struct nfhdr_v1 *)cp;
	ND_TCHECK(*nh);

	ver = EXTRACT_16BITS(&nh->version);
	nrecs = EXTRACT_32BITS(&nh->count);
#if 0
	/*
	 * This is seconds since the UN*X epoch, and is followed by
	 * nanoseconds.  XXX - format it, rather than just dumping the
	 * raw seconds-since-the-Epoch.
	 */
	t = EXTRACT_32BITS(&nh->utc_sec);
#endif

	ND_PRINT((ndo, "NetFlow v%x, %u.%03u uptime, %u.%09u, ", ver,
	       EXTRACT_32BITS(&nh->msys_uptime)/1000,
	       EXTRACT_32BITS(&nh->msys_uptime)%1000,
	       EXTRACT_32BITS(&nh->utc_sec), EXTRACT_32BITS(&nh->utc_nsec)));

	nr = (const struct nfrec_v1 *)&nh[1];

	ND_PRINT((ndo, "%2u recs", nrecs));

	for (; nrecs != 0; nr++, nrecs--) {
		char buf[20];
		char asbuf[20];

		/*
		 * Make sure we have the entire record.
		 */
		ND_TCHECK(*nr);
		ND_PRINT((ndo, "\n  started %u.%03u, last %u.%03u",
		       EXTRACT_32BITS(&nr->start_time)/1000,
		       EXTRACT_32BITS(&nr->start_time)%1000,
		       EXTRACT_32BITS(&nr->last_time)/1000,
		       EXTRACT_32BITS(&nr->last_time)%1000));

		asbuf[0] = buf[0] = '\0';
		ND_PRINT((ndo, "\n    %s%s%s:%u ", intoa(nr->src_ina.s_addr), buf, asbuf,
			EXTRACT_16BITS(&nr->srcport)));

		ND_PRINT((ndo, "> %s%s%s:%u ", intoa(nr->dst_ina.s_addr), buf, asbuf,
			EXTRACT_16BITS(&nr->dstport)));

		ND_PRINT((ndo, ">> %s\n    ", intoa(nr->nhop_ina.s_addr)));

		if (!ndo->ndo_nflag && (p_name = netdb_protoname(nr->proto)) != NULL)
			ND_PRINT((ndo, "%s ", p_name));
		else
			ND_PRINT((ndo, "%u ", nr->proto));

		/* tcp flags for tcp only */
		if (nr->proto == IPPROTO_TCP) {
			int flags;
			flags = nr->tcp_flags;
			ND_PRINT((ndo, "%s%s%s%s%s%s%s",
				flags & TH_FIN  ? "F" : "",
				flags & TH_SYN  ? "S" : "",
				flags & TH_RST  ? "R" : "",
				flags & TH_PUSH ? "P" : "",
				flags & TH_ACK  ? "A" : "",
				flags & TH_URG  ? "U" : "",
				flags           ? " " : ""));
		}

		buf[0]='\0';
		ND_PRINT((ndo, "tos %u, %u (%u octets) %s",
		       nr->tos,
		       EXTRACT_32BITS(&nr->packets),
		       EXTRACT_32BITS(&nr->octets), buf));
	}
	return;

trunc:
	ND_PRINT((ndo, "[|cnfp]"));
	return;
}

static void
cnfp_v5_print(netdissect_options *ndo, const u_char *cp)
{
	register const struct nfhdr_v5 *nh;
	register const struct nfrec_v5 *nr;
	const char *p_name;
	int nrecs, ver;
#if 0
	time_t t;
#endif

	nh = (const struct nfhdr_v5 *)cp;
	ND_TCHECK(*nh);

	ver = EXTRACT_16BITS(&nh->version);
	nrecs = EXTRACT_32BITS(&nh->count);
#if 0
	/*
	 * This is seconds since the UN*X epoch, and is followed by
	 * nanoseconds.  XXX - format it, rather than just dumping the
	 * raw seconds-since-the-Epoch.
	 */
	t = EXTRACT_32BITS(&nh->utc_sec);
#endif

	ND_PRINT((ndo, "NetFlow v%x, %u.%03u uptime, %u.%09u, ", ver,
	       EXTRACT_32BITS(&nh->msys_uptime)/1000,
	       EXTRACT_32BITS(&nh->msys_uptime)%1000,
	       EXTRACT_32BITS(&nh->utc_sec), EXTRACT_32BITS(&nh->utc_nsec)));

	ND_PRINT((ndo, "#%u, ", EXTRACT_32BITS(&nh->sequence)));
	nr = (const struct nfrec_v5 *)&nh[1];

	ND_PRINT((ndo, "%2u recs", nrecs));

	for (; nrecs != 0; nr++, nrecs--) {
		char buf[20];
		char asbuf[20];

		/*
		 * Make sure we have the entire record.
		 */
		ND_TCHECK(*nr);
		ND_PRINT((ndo, "\n  started %u.%03u, last %u.%03u",
		       EXTRACT_32BITS(&nr->start_time)/1000,
		       EXTRACT_32BITS(&nr->start_time)%1000,
		       EXTRACT_32BITS(&nr->last_time)/1000,
		       EXTRACT_32BITS(&nr->last_time)%1000));

		asbuf[0] = buf[0] = '\0';
		snprintf(buf, sizeof(buf), "/%u", nr->src_mask);
		snprintf(asbuf, sizeof(asbuf), ":%u",
			EXTRACT_16BITS(&nr->src_as));
		ND_PRINT((ndo, "\n    %s%s%s:%u ", intoa(nr->src_ina.s_addr), buf, asbuf,
			EXTRACT_16BITS(&nr->srcport)));

		snprintf(buf, sizeof(buf), "/%d", nr->dst_mask);
		snprintf(asbuf, sizeof(asbuf), ":%u",
			 EXTRACT_16BITS(&nr->dst_as));
		ND_PRINT((ndo, "> %s%s%s:%u ", intoa(nr->dst_ina.s_addr), buf, asbuf,
			EXTRACT_16BITS(&nr->dstport)));

		ND_PRINT((ndo, ">> %s\n    ", intoa(nr->nhop_ina.s_addr)));

		if (!ndo->ndo_nflag && (p_name = netdb_protoname(nr->proto)) != NULL)
			ND_PRINT((ndo, "%s ", p_name));
		else
			ND_PRINT((ndo, "%u ", nr->proto));

		/* tcp flags for tcp only */
		if (nr->proto == IPPROTO_TCP) {
			int flags;
			flags = nr->tcp_flags;
			ND_PRINT((ndo, "%s%s%s%s%s%s%s",
				flags & TH_FIN  ? "F" : "",
				flags & TH_SYN  ? "S" : "",
				flags & TH_RST  ? "R" : "",
				flags & TH_PUSH ? "P" : "",
				flags & TH_ACK  ? "A" : "",
				flags & TH_URG  ? "U" : "",
				flags           ? " " : ""));
		}

		buf[0]='\0';
		ND_PRINT((ndo, "tos %u, %u (%u octets) %s",
		       nr->tos,
		       EXTRACT_32BITS(&nr->packets),
		       EXTRACT_32BITS(&nr->octets), buf));
	}
	return;

trunc:
	ND_PRINT((ndo, "[|cnfp]"));
	return;
}

static void
cnfp_v6_print(netdissect_options *ndo, const u_char *cp)
{
	register const struct nfhdr_v6 *nh;
	register const struct nfrec_v6 *nr;
	const char *p_name;
	int nrecs, ver;
#if 0
	time_t t;
#endif

	nh = (const struct nfhdr_v6 *)cp;
	ND_TCHECK(*nh);

	ver = EXTRACT_16BITS(&nh->version);
	nrecs = EXTRACT_32BITS(&nh->count);
#if 0
	/*
	 * This is seconds since the UN*X epoch, and is followed by
	 * nanoseconds.  XXX - format it, rather than just dumping the
	 * raw seconds-since-the-Epoch.
	 */
	t = EXTRACT_32BITS(&nh->utc_sec);
#endif

	ND_PRINT((ndo, "NetFlow v%x, %u.%03u uptime, %u.%09u, ", ver,
	       EXTRACT_32BITS(&nh->msys_uptime)/1000,
	       EXTRACT_32BITS(&nh->msys_uptime)%1000,
	       EXTRACT_32BITS(&nh->utc_sec), EXTRACT_32BITS(&nh->utc_nsec)));

	ND_PRINT((ndo, "#%u, ", EXTRACT_32BITS(&nh->sequence)));
	nr = (const struct nfrec_v6 *)&nh[1];

	ND_PRINT((ndo, "%2u recs", nrecs));

	for (; nrecs != 0; nr++, nrecs--) {
		char buf[20];
		char asbuf[20];

		/*
		 * Make sure we have the entire record.
		 */
		ND_TCHECK(*nr);
		ND_PRINT((ndo, "\n  started %u.%03u, last %u.%03u",
		       EXTRACT_32BITS(&nr->start_time)/1000,
		       EXTRACT_32BITS(&nr->start_time)%1000,
		       EXTRACT_32BITS(&nr->last_time)/1000,
		       EXTRACT_32BITS(&nr->last_time)%1000));

		asbuf[0] = buf[0] = '\0';
		snprintf(buf, sizeof(buf), "/%u", nr->src_mask);
		snprintf(asbuf, sizeof(asbuf), ":%u",
			EXTRACT_16BITS(&nr->src_as));
		ND_PRINT((ndo, "\n    %s%s%s:%u ", intoa(nr->src_ina.s_addr), buf, asbuf,
			EXTRACT_16BITS(&nr->srcport)));

		snprintf(buf, sizeof(buf), "/%d", nr->dst_mask);
		snprintf(asbuf, sizeof(asbuf), ":%u",
			 EXTRACT_16BITS(&nr->dst_as));
		ND_PRINT((ndo, "> %s%s%s:%u ", intoa(nr->dst_ina.s_addr), buf, asbuf,
			EXTRACT_16BITS(&nr->dstport)));

		ND_PRINT((ndo, ">> %s\n    ", intoa(nr->nhop_ina.s_addr)));

		if (!ndo->ndo_nflag && (p_name = netdb_protoname(nr->proto)) != NULL)
			ND_PRINT((ndo, "%s ", p_name));
		else
			ND_PRINT((ndo, "%u ", nr->proto));

		/* tcp flags for tcp only */
		if (nr->proto == IPPROTO_TCP) {
			int flags;
			flags = nr->tcp_flags;
			ND_PRINT((ndo, "%s%s%s%s%s%s%s",
				flags & TH_FIN  ? "F" : "",
				flags & TH_SYN  ? "S" : "",
				flags & TH_RST  ? "R" : "",
				flags & TH_PUSH ? "P" : "",
				flags & TH_ACK  ? "A" : "",
				flags & TH_URG  ? "U" : "",
				flags           ? " " : ""));
		}

		buf[0]='\0';
		snprintf(buf, sizeof(buf), "(%u<>%u encaps)",
			 (EXTRACT_16BITS(&nr->flags) >> 8) & 0xff,
			 (EXTRACT_16BITS(&nr->flags)) & 0xff);
		ND_PRINT((ndo, "tos %u, %u (%u octets) %s",
		       nr->tos,
		       EXTRACT_32BITS(&nr->packets),
		       EXTRACT_32BITS(&nr->octets), buf));
	}
	return;

trunc:
	ND_PRINT((ndo, "[|cnfp]"));
	return;
}

void
cnfp_print(netdissect_options *ndo, const u_char *cp)
{
	int ver;

	/*
	 * First 2 bytes are the version number.
	 */
	ND_TCHECK2(*cp, 2);
	ver = EXTRACT_16BITS(cp);
	switch (ver) {

	case 1:
		cnfp_v1_print(ndo, cp);
		break;

	case 5:
		cnfp_v5_print(ndo, cp);
		break;

	case 6:
		cnfp_v6_print(ndo, cp);
		break;

	default:
		ND_PRINT((ndo, "NetFlow v%x", ver));
		break;
	}
	return;

trunc:
	ND_PRINT((ndo, "[|cnfp]"));
	return;
}
