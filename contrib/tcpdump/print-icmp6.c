/*
 * Copyright (c) 1988, 1989, 1990, 1991, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that: (1) source code distributions
 * retain the above copyright notice and this paragraph in its entirety, (2)
 * distributions including binary code include the above copyright notice and
 * this paragraph in its entirety in the documentation or other materials
 * provided with the distribution, and (3) all advertising materials mentioning
 * features or use of this software display the following acknowledgement:
 * ``This product includes software developed by the University of California,
 * Lawrence Berkeley Laboratory and its contributors.'' Neither the name of
 * the University nor the names of its contributors may be used to endorse
 * or promote products derived from this software without specific prior
 * written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

/* \summary: IPv6 Internet Control Message Protocol (ICMPv6) printer */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <netdissect-stdinc.h>

#include <stdio.h>
#include <string.h>

#include "netdissect.h"
#include "addrtoname.h"
#include "addrtostr.h"
#include "extract.h"

#include "ip6.h"
#include "ipproto.h"

#include "udp.h"
#include "ah.h"

/*	NetBSD: icmp6.h,v 1.13 2000/08/03 16:30:37 itojun Exp 	*/
/*	$KAME: icmp6.h,v 1.22 2000/08/03 15:25:16 jinmei Exp $	*/

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

struct icmp6_hdr {
	uint8_t		icmp6_type;	/* type field */
	uint8_t		icmp6_code;	/* code field */
	uint16_t	icmp6_cksum;	/* checksum field */
	union {
		uint32_t	icmp6_un_data32[1]; /* type-specific field */
		uint16_t	icmp6_un_data16[2]; /* type-specific field */
		uint8_t		icmp6_un_data8[4];  /* type-specific field */
	} icmp6_dataun;
};

#define icmp6_data32	icmp6_dataun.icmp6_un_data32
#define icmp6_data16	icmp6_dataun.icmp6_un_data16
#define icmp6_data8	icmp6_dataun.icmp6_un_data8
#define icmp6_pptr	icmp6_data32[0]		/* parameter prob */
#define icmp6_mtu	icmp6_data32[0]		/* packet too big */
#define icmp6_id	icmp6_data16[0]		/* echo request/reply */
#define icmp6_seq	icmp6_data16[1]		/* echo request/reply */
#define icmp6_maxdelay	icmp6_data16[0]		/* mcast group membership */

#define ICMP6_DST_UNREACH		1	/* dest unreachable, codes: */
#define ICMP6_PACKET_TOO_BIG		2	/* packet too big */
#define ICMP6_TIME_EXCEEDED		3	/* time exceeded, code: */
#define ICMP6_PARAM_PROB		4	/* ip6 header bad */

#define ICMP6_ECHO_REQUEST		128	/* echo service */
#define ICMP6_ECHO_REPLY		129	/* echo reply */
#define ICMP6_MEMBERSHIP_QUERY		130	/* group membership query */
#define MLD6_LISTENER_QUERY		130 	/* multicast listener query */
#define ICMP6_MEMBERSHIP_REPORT		131	/* group membership report */
#define MLD6_LISTENER_REPORT		131	/* multicast listener report */
#define ICMP6_MEMBERSHIP_REDUCTION	132	/* group membership termination */
#define MLD6_LISTENER_DONE		132	/* multicast listener done */

#define ND_ROUTER_SOLICIT		133	/* router solicitation */
#define ND_ROUTER_ADVERT		134	/* router advertisement */
#define ND_NEIGHBOR_SOLICIT		135	/* neighbor solicitation */
#define ND_NEIGHBOR_ADVERT		136	/* neighbor advertisement */
#define ND_REDIRECT			137	/* redirect */

#define ICMP6_ROUTER_RENUMBERING	138	/* router renumbering */

#define ICMP6_WRUREQUEST		139	/* who are you request */
#define ICMP6_WRUREPLY			140	/* who are you reply */
#define ICMP6_FQDN_QUERY		139	/* FQDN query */
#define ICMP6_FQDN_REPLY		140	/* FQDN reply */
#define ICMP6_NI_QUERY			139	/* node information request */
#define ICMP6_NI_REPLY			140	/* node information reply */
#define IND_SOLICIT			141	/* inverse neighbor solicitation */
#define IND_ADVERT			142	/* inverse neighbor advertisement */

#define ICMP6_V2_MEMBERSHIP_REPORT	143	/* v2 membership report */
#define MLDV2_LISTENER_REPORT		143	/* v2 multicast listener report */
#define ICMP6_HADISCOV_REQUEST		144
#define ICMP6_HADISCOV_REPLY		145
#define ICMP6_MOBILEPREFIX_SOLICIT	146
#define ICMP6_MOBILEPREFIX_ADVERT	147

#define MLD6_MTRACE_RESP		200	/* mtrace response(to sender) */
#define MLD6_MTRACE			201	/* mtrace messages */

#define ICMP6_MAXTYPE			201

#define ICMP6_DST_UNREACH_NOROUTE	0	/* no route to destination */
#define ICMP6_DST_UNREACH_ADMIN	 	1	/* administratively prohibited */
#define ICMP6_DST_UNREACH_NOTNEIGHBOR	2	/* not a neighbor(obsolete) */
#define ICMP6_DST_UNREACH_BEYONDSCOPE	2	/* beyond scope of source address */
#define ICMP6_DST_UNREACH_ADDR		3	/* address unreachable */
#define ICMP6_DST_UNREACH_NOPORT	4	/* port unreachable */

#define ICMP6_TIME_EXCEED_TRANSIT 	0	/* ttl==0 in transit */
#define ICMP6_TIME_EXCEED_REASSEMBLY	1	/* ttl==0 in reass */

#define ICMP6_PARAMPROB_HEADER 	 	0	/* erroneous header field */
#define ICMP6_PARAMPROB_NEXTHEADER	1	/* unrecognized next header */
#define ICMP6_PARAMPROB_OPTION		2	/* unrecognized option */

#define ICMP6_INFOMSG_MASK		0x80	/* all informational messages */

#define ICMP6_NI_SUBJ_IPV6	0	/* Query Subject is an IPv6 address */
#define ICMP6_NI_SUBJ_FQDN	1	/* Query Subject is a Domain name */
#define ICMP6_NI_SUBJ_IPV4	2	/* Query Subject is an IPv4 address */

#define ICMP6_NI_SUCCESS	0	/* node information successful reply */
#define ICMP6_NI_REFUSED	1	/* node information request is refused */
#define ICMP6_NI_UNKNOWN	2	/* unknown Qtype */

#define ICMP6_ROUTER_RENUMBERING_COMMAND  0	/* rr command */
#define ICMP6_ROUTER_RENUMBERING_RESULT   1	/* rr result */
#define ICMP6_ROUTER_RENUMBERING_SEQNUM_RESET   255	/* rr seq num reset */

/* Used in kernel only */
#define ND_REDIRECT_ONLINK	0	/* redirect to an on-link node */
#define ND_REDIRECT_ROUTER	1	/* redirect to a better router */

/*
 * Multicast Listener Discovery
 */
struct mld6_hdr {
	struct icmp6_hdr	mld6_hdr;
	struct in6_addr		mld6_addr; /* multicast address */
};

#define mld6_type	mld6_hdr.icmp6_type
#define mld6_code	mld6_hdr.icmp6_code
#define mld6_cksum	mld6_hdr.icmp6_cksum
#define mld6_maxdelay	mld6_hdr.icmp6_data16[0]
#define mld6_reserved	mld6_hdr.icmp6_data16[1]

#define MLD_MINLEN	24
#define MLDV2_MINLEN	28

/*
 * Neighbor Discovery
 */

struct nd_router_solicit {	/* router solicitation */
	struct icmp6_hdr 	nd_rs_hdr;
	/* could be followed by options */
};

#define nd_rs_type	nd_rs_hdr.icmp6_type
#define nd_rs_code	nd_rs_hdr.icmp6_code
#define nd_rs_cksum	nd_rs_hdr.icmp6_cksum
#define nd_rs_reserved	nd_rs_hdr.icmp6_data32[0]

struct nd_router_advert {	/* router advertisement */
	struct icmp6_hdr	nd_ra_hdr;
	uint32_t		nd_ra_reachable;	/* reachable time */
	uint32_t		nd_ra_retransmit;	/* retransmit timer */
	/* could be followed by options */
};

#define nd_ra_type		nd_ra_hdr.icmp6_type
#define nd_ra_code		nd_ra_hdr.icmp6_code
#define nd_ra_cksum		nd_ra_hdr.icmp6_cksum
#define nd_ra_curhoplimit	nd_ra_hdr.icmp6_data8[0]
#define nd_ra_flags_reserved	nd_ra_hdr.icmp6_data8[1]
#define ND_RA_FLAG_MANAGED	0x80
#define ND_RA_FLAG_OTHER	0x40
#define ND_RA_FLAG_HOME_AGENT	0x20

/*
 * Router preference values based on draft-draves-ipngwg-router-selection-01.
 * These are non-standard definitions.
 */
#define ND_RA_FLAG_RTPREF_MASK	0x18 /* 00011000 */

#define ND_RA_FLAG_RTPREF_HIGH	0x08 /* 00001000 */
#define ND_RA_FLAG_RTPREF_MEDIUM	0x00 /* 00000000 */
#define ND_RA_FLAG_RTPREF_LOW	0x18 /* 00011000 */
#define ND_RA_FLAG_RTPREF_RSV	0x10 /* 00010000 */

#define nd_ra_router_lifetime	nd_ra_hdr.icmp6_data16[1]

struct nd_neighbor_solicit {	/* neighbor solicitation */
	struct icmp6_hdr	nd_ns_hdr;
	struct in6_addr		nd_ns_target;	/*target address */
	/* could be followed by options */
};

#define nd_ns_type		nd_ns_hdr.icmp6_type
#define nd_ns_code		nd_ns_hdr.icmp6_code
#define nd_ns_cksum		nd_ns_hdr.icmp6_cksum
#define nd_ns_reserved		nd_ns_hdr.icmp6_data32[0]

struct nd_neighbor_advert {	/* neighbor advertisement */
	struct icmp6_hdr	nd_na_hdr;
	struct in6_addr		nd_na_target;	/* target address */
	/* could be followed by options */
};

#define nd_na_type		nd_na_hdr.icmp6_type
#define nd_na_code		nd_na_hdr.icmp6_code
#define nd_na_cksum		nd_na_hdr.icmp6_cksum
#define nd_na_flags_reserved	nd_na_hdr.icmp6_data32[0]

#define ND_NA_FLAG_ROUTER		0x80000000
#define ND_NA_FLAG_SOLICITED		0x40000000
#define ND_NA_FLAG_OVERRIDE		0x20000000

struct nd_redirect {		/* redirect */
	struct icmp6_hdr	nd_rd_hdr;
	struct in6_addr		nd_rd_target;	/* target address */
	struct in6_addr		nd_rd_dst;	/* destination address */
	/* could be followed by options */
};

#define nd_rd_type		nd_rd_hdr.icmp6_type
#define nd_rd_code		nd_rd_hdr.icmp6_code
#define nd_rd_cksum		nd_rd_hdr.icmp6_cksum
#define nd_rd_reserved		nd_rd_hdr.icmp6_data32[0]

struct nd_opt_hdr {		/* Neighbor discovery option header */
	uint8_t		nd_opt_type;
	uint8_t		nd_opt_len;
	/* followed by option specific data*/
};

#define ND_OPT_SOURCE_LINKADDR		1
#define ND_OPT_TARGET_LINKADDR		2
#define ND_OPT_PREFIX_INFORMATION	3
#define ND_OPT_REDIRECTED_HEADER	4
#define ND_OPT_MTU			5
#define ND_OPT_ADVINTERVAL		7
#define ND_OPT_HOMEAGENT_INFO		8
#define ND_OPT_ROUTE_INFO		24	/* RFC4191 */
#define ND_OPT_RDNSS			25
#define ND_OPT_DNSSL			31

struct nd_opt_prefix_info {	/* prefix information */
	nd_uint8_t		nd_opt_pi_type;
	nd_uint8_t		nd_opt_pi_len;
	nd_uint8_t		nd_opt_pi_prefix_len;
	nd_uint8_t		nd_opt_pi_flags_reserved;
	nd_uint32_t		nd_opt_pi_valid_time;
	nd_uint32_t		nd_opt_pi_preferred_time;
	nd_uint32_t		nd_opt_pi_reserved2;
	struct in6_addr	nd_opt_pi_prefix;
};

#define ND_OPT_PI_FLAG_ONLINK		0x80
#define ND_OPT_PI_FLAG_AUTO		0x40
#define ND_OPT_PI_FLAG_ROUTER		0x20	/*2292bis*/

struct nd_opt_rd_hdr {         /* redirected header */
	uint8_t		nd_opt_rh_type;
	uint8_t		nd_opt_rh_len;
	uint16_t	nd_opt_rh_reserved1;
	uint32_t	nd_opt_rh_reserved2;
	/* followed by IP header and data */
};

struct nd_opt_mtu {		/* MTU option */
	uint8_t		nd_opt_mtu_type;
	uint8_t		nd_opt_mtu_len;
	uint16_t	nd_opt_mtu_reserved;
	uint32_t	nd_opt_mtu_mtu;
};

struct nd_opt_rdnss {		/* RDNSS RFC 6106 5.1 */
	uint8_t		nd_opt_rdnss_type;
	uint8_t		nd_opt_rdnss_len;
	uint16_t	nd_opt_rdnss_reserved;
	uint32_t	nd_opt_rdnss_lifetime;
	struct in6_addr nd_opt_rdnss_addr[1];	/* variable-length */
};

struct nd_opt_dnssl {		/* DNSSL RFC 6106 5.2 */
	uint8_t  nd_opt_dnssl_type;
	uint8_t  nd_opt_dnssl_len;
	uint16_t nd_opt_dnssl_reserved;
	uint32_t nd_opt_dnssl_lifetime;
	/* followed by list of DNS search domains, variable-length */
};

struct nd_opt_advinterval {	/* Advertisement interval option */
	uint8_t		nd_opt_adv_type;
	uint8_t		nd_opt_adv_len;
	uint16_t	nd_opt_adv_reserved;
	uint32_t	nd_opt_adv_interval;
};

struct nd_opt_homeagent_info {	/* Home Agent info */
	uint8_t		nd_opt_hai_type;
	uint8_t		nd_opt_hai_len;
	uint16_t	nd_opt_hai_reserved;
	int16_t		nd_opt_hai_preference;
	uint16_t	nd_opt_hai_lifetime;
};

struct nd_opt_route_info {	/* route info */
	uint8_t		nd_opt_rti_type;
	uint8_t		nd_opt_rti_len;
	uint8_t		nd_opt_rti_prefixlen;
	uint8_t		nd_opt_rti_flags;
	uint32_t	nd_opt_rti_lifetime;
	/* prefix follows */
};

/*
 * icmp6 namelookup
 */

struct icmp6_namelookup {
	struct icmp6_hdr 	icmp6_nl_hdr;
	uint8_t		icmp6_nl_nonce[8];
	int32_t		icmp6_nl_ttl;
#if 0
	uint8_t		icmp6_nl_len;
	uint8_t		icmp6_nl_name[3];
#endif
	/* could be followed by options */
};

/*
 * icmp6 node information
 */
struct icmp6_nodeinfo {
	struct icmp6_hdr icmp6_ni_hdr;
	uint8_t icmp6_ni_nonce[8];
	/* could be followed by reply data */
};

#define ni_type		icmp6_ni_hdr.icmp6_type
#define ni_code		icmp6_ni_hdr.icmp6_code
#define ni_cksum	icmp6_ni_hdr.icmp6_cksum
#define ni_qtype	icmp6_ni_hdr.icmp6_data16[0]
#define ni_flags	icmp6_ni_hdr.icmp6_data16[1]

#define NI_QTYPE_NOOP		0 /* NOOP  */
#define NI_QTYPE_SUPTYPES	1 /* Supported Qtypes */
#define NI_QTYPE_FQDN		2 /* FQDN (draft 04) */
#define NI_QTYPE_DNSNAME	2 /* DNS Name */
#define NI_QTYPE_NODEADDR	3 /* Node Addresses */
#define NI_QTYPE_IPV4ADDR	4 /* IPv4 Addresses */

/* network endian */
#define NI_SUPTYPE_FLAG_COMPRESS	((uint16_t)htons(0x1))
#define NI_FQDN_FLAG_VALIDTTL		((uint16_t)htons(0x1))

/* network endian */
#define NI_NODEADDR_FLAG_TRUNCATE	((uint16_t)htons(0x1))
#define NI_NODEADDR_FLAG_ALL		((uint16_t)htons(0x2))
#define NI_NODEADDR_FLAG_COMPAT		((uint16_t)htons(0x4))
#define NI_NODEADDR_FLAG_LINKLOCAL	((uint16_t)htons(0x8))
#define NI_NODEADDR_FLAG_SITELOCAL	((uint16_t)htons(0x10))
#define NI_NODEADDR_FLAG_GLOBAL		((uint16_t)htons(0x20))
#define NI_NODEADDR_FLAG_ANYCAST	((uint16_t)htons(0x40)) /* just experimental. not in spec */

struct ni_reply_fqdn {
	uint32_t ni_fqdn_ttl;	/* TTL */
	uint8_t ni_fqdn_namelen; /* length in octets of the FQDN */
	uint8_t ni_fqdn_name[3]; /* XXX: alignment */
};

/*
 * Router Renumbering. as router-renum-08.txt
 */
struct icmp6_router_renum {	/* router renumbering header */
	struct icmp6_hdr	rr_hdr;
	uint8_t		rr_segnum;
	uint8_t		rr_flags;
	uint16_t	rr_maxdelay;
	uint32_t	rr_reserved;
};
#define ICMP6_RR_FLAGS_TEST		0x80
#define ICMP6_RR_FLAGS_REQRESULT	0x40
#define ICMP6_RR_FLAGS_FORCEAPPLY	0x20
#define ICMP6_RR_FLAGS_SPECSITE		0x10
#define ICMP6_RR_FLAGS_PREVDONE		0x08

#define rr_type		rr_hdr.icmp6_type
#define rr_code		rr_hdr.icmp6_code
#define rr_cksum	rr_hdr.icmp6_cksum
#define rr_seqnum 	rr_hdr.icmp6_data32[0]

struct rr_pco_match {		/* match prefix part */
	uint8_t		rpm_code;
	uint8_t		rpm_len;
	uint8_t		rpm_ordinal;
	uint8_t		rpm_matchlen;
	uint8_t		rpm_minlen;
	uint8_t		rpm_maxlen;
	uint16_t	rpm_reserved;
	struct	in6_addr	rpm_prefix;
};

#define RPM_PCO_ADD		1
#define RPM_PCO_CHANGE		2
#define RPM_PCO_SETGLOBAL	3
#define RPM_PCO_MAX		4

struct rr_pco_use {		/* use prefix part */
	uint8_t		rpu_uselen;
	uint8_t		rpu_keeplen;
	uint8_t		rpu_ramask;
	uint8_t		rpu_raflags;
	uint32_t	rpu_vltime;
	uint32_t	rpu_pltime;
	uint32_t	rpu_flags;
	struct	in6_addr rpu_prefix;
};
#define ICMP6_RR_PCOUSE_RAFLAGS_ONLINK	0x80
#define ICMP6_RR_PCOUSE_RAFLAGS_AUTO	0x40

/* network endian */
#define ICMP6_RR_PCOUSE_FLAGS_DECRVLTIME     ((uint32_t)htonl(0x80000000))
#define ICMP6_RR_PCOUSE_FLAGS_DECRPLTIME     ((uint32_t)htonl(0x40000000))

struct rr_result {		/* router renumbering result message */
	uint16_t	rrr_flags;
	uint8_t		rrr_ordinal;
	uint8_t		rrr_matchedlen;
	uint32_t	rrr_ifid;
	struct	in6_addr rrr_prefix;
};
/* network endian */
#define ICMP6_RR_RESULT_FLAGS_OOB		((uint16_t)htons(0x0002))
#define ICMP6_RR_RESULT_FLAGS_FORBIDDEN		((uint16_t)htons(0x0001))

static const char *get_rtpref(u_int);
static const char *get_lifetime(uint32_t);
static void print_lladdr(netdissect_options *ndo, const u_char *, size_t);
static void icmp6_opt_print(netdissect_options *ndo, const u_char *, int);
static void mld6_print(netdissect_options *ndo, const u_char *);
static void mldv2_report_print(netdissect_options *ndo, const u_char *, u_int);
static void mldv2_query_print(netdissect_options *ndo, const u_char *, u_int);
static const struct udphdr *get_upperlayer(netdissect_options *ndo, const u_char *, u_int *);
static void dnsname_print(netdissect_options *ndo, const u_char *, const u_char *);
static void icmp6_nodeinfo_print(netdissect_options *ndo, u_int, const u_char *, const u_char *);
static void icmp6_rrenum_print(netdissect_options *ndo, const u_char *, const u_char *);

#ifndef abs
#define abs(a)	((0 < (a)) ? (a) : -(a))
#endif

#include "rpl.h"

static const struct tok icmp6_type_values[] = {
    { ICMP6_DST_UNREACH, "destination unreachable"},
    { ICMP6_PACKET_TOO_BIG, "packet too big"},
    { ICMP6_TIME_EXCEEDED, "time exceeded in-transit"},
    { ICMP6_PARAM_PROB, "parameter problem"},
    { ICMP6_ECHO_REQUEST, "echo request"},
    { ICMP6_ECHO_REPLY, "echo reply"},
    { MLD6_LISTENER_QUERY, "multicast listener query"},
    { MLD6_LISTENER_REPORT, "multicast listener report"},
    { MLD6_LISTENER_DONE, "multicast listener done"},
    { ND_ROUTER_SOLICIT, "router solicitation"},
    { ND_ROUTER_ADVERT, "router advertisement"},
    { ND_NEIGHBOR_SOLICIT, "neighbor solicitation"},
    { ND_NEIGHBOR_ADVERT, "neighbor advertisement"},
    { ND_REDIRECT, "redirect"},
    { ICMP6_ROUTER_RENUMBERING, "router renumbering"},
    { IND_SOLICIT, "inverse neighbor solicitation"},
    { IND_ADVERT, "inverse neighbor advertisement"},
    { MLDV2_LISTENER_REPORT, "multicast listener report v2"},
    { ICMP6_HADISCOV_REQUEST, "ha discovery request"},
    { ICMP6_HADISCOV_REPLY, "ha discovery reply"},
    { ICMP6_MOBILEPREFIX_SOLICIT, "mobile router solicitation"},
    { ICMP6_MOBILEPREFIX_ADVERT, "mobile router advertisement"},
    { ICMP6_WRUREQUEST, "who-are-you request"},
    { ICMP6_WRUREPLY, "who-are-you reply"},
    { ICMP6_NI_QUERY, "node information query"},
    { ICMP6_NI_REPLY, "node information reply"},
    { MLD6_MTRACE, "mtrace message"},
    { MLD6_MTRACE_RESP, "mtrace response"},
    { ND_RPL_MESSAGE,   "RPL"},
    { 0,	NULL }
};

static const struct tok icmp6_dst_unreach_code_values[] = {
    { ICMP6_DST_UNREACH_NOROUTE, "unreachable route" },
    { ICMP6_DST_UNREACH_ADMIN, " unreachable prohibited"},
    { ICMP6_DST_UNREACH_BEYONDSCOPE, "beyond scope"},
    { ICMP6_DST_UNREACH_ADDR, "unreachable address"},
    { ICMP6_DST_UNREACH_NOPORT, "unreachable port"},
    { 0,	NULL }
};

static const struct tok icmp6_opt_pi_flag_values[] = {
    { ND_OPT_PI_FLAG_ONLINK, "onlink" },
    { ND_OPT_PI_FLAG_AUTO, "auto" },
    { ND_OPT_PI_FLAG_ROUTER, "router" },
    { 0,	NULL }
};

static const struct tok icmp6_opt_ra_flag_values[] = {
    { ND_RA_FLAG_MANAGED, "managed" },
    { ND_RA_FLAG_OTHER, "other stateful"},
    { ND_RA_FLAG_HOME_AGENT, "home agent"},
    { 0,	NULL }
};

static const struct tok icmp6_nd_na_flag_values[] = {
    { ND_NA_FLAG_ROUTER, "router" },
    { ND_NA_FLAG_SOLICITED, "solicited" },
    { ND_NA_FLAG_OVERRIDE, "override" },
    { 0,	NULL }
};


static const struct tok icmp6_opt_values[] = {
   { ND_OPT_SOURCE_LINKADDR, "source link-address"},
   { ND_OPT_TARGET_LINKADDR, "destination link-address"},
   { ND_OPT_PREFIX_INFORMATION, "prefix info"},
   { ND_OPT_REDIRECTED_HEADER, "redirected header"},
   { ND_OPT_MTU, "mtu"},
   { ND_OPT_RDNSS, "rdnss"},
   { ND_OPT_DNSSL, "dnssl"},
   { ND_OPT_ADVINTERVAL, "advertisement interval"},
   { ND_OPT_HOMEAGENT_INFO, "homeagent information"},
   { ND_OPT_ROUTE_INFO, "route info"},
   { 0,	NULL }
};

/* mldv2 report types */
static const struct tok mldv2report2str[] = {
	{ 1,	"is_in" },
	{ 2,	"is_ex" },
	{ 3,	"to_in" },
	{ 4,	"to_ex" },
	{ 5,	"allow" },
	{ 6,	"block" },
	{ 0,	NULL }
};

static const char *
get_rtpref(u_int v)
{
	static const char *rtpref_str[] = {
		"medium",		/* 00 */
		"high",			/* 01 */
		"rsv",			/* 10 */
		"low"			/* 11 */
	};

	return rtpref_str[((v & ND_RA_FLAG_RTPREF_MASK) >> 3) & 0xff];
}

static const char *
get_lifetime(uint32_t v)
{
	static char buf[20];

	if (v == (uint32_t)~0UL)
		return "infinity";
	else {
		snprintf(buf, sizeof(buf), "%us", v);
		return buf;
	}
}

static void
print_lladdr(netdissect_options *ndo, const uint8_t *p, size_t l)
{
	const uint8_t *ep, *q;

	q = p;
	ep = p + l;
	while (l > 0 && q < ep) {
		if (q > p)
                        ND_PRINT((ndo,":"));
		ND_PRINT((ndo,"%02x", *q++));
		l--;
	}
}

static int icmp6_cksum(netdissect_options *ndo, const struct ip6_hdr *ip6,
	const struct icmp6_hdr *icp, u_int len)
{
	return nextproto6_cksum(ndo, ip6, (const uint8_t *)(const void *)icp, len, len,
				IPPROTO_ICMPV6);
}

static const struct tok rpl_mop_values[] = {
        { RPL_DIO_NONSTORING,         "nonstoring"},
        { RPL_DIO_STORING,            "storing"},
        { RPL_DIO_NONSTORING_MULTICAST, "nonstoring-multicast"},
        { RPL_DIO_STORING_MULTICAST,  "storing-multicast"},
        { 0, NULL},
};

static const struct tok rpl_subopt_values[] = {
        { RPL_OPT_PAD0, "pad0"},
        { RPL_OPT_PADN, "padN"},
        { RPL_DIO_METRICS, "metrics"},
        { RPL_DIO_ROUTINGINFO, "routinginfo"},
        { RPL_DIO_CONFIG,    "config"},
        { RPL_DAO_RPLTARGET, "rpltarget"},
        { RPL_DAO_TRANSITINFO, "transitinfo"},
        { RPL_DIO_DESTPREFIX, "destprefix"},
        { RPL_DAO_RPLTARGET_DESC, "rpltargetdesc"},
        { 0, NULL},
};

static void
rpl_dio_printopt(netdissect_options *ndo,
                 const struct rpl_dio_genoption *opt,
                 u_int length)
{
        if(length < RPL_DIO_GENOPTION_LEN) return;
        length -= RPL_DIO_GENOPTION_LEN;

        ND_TCHECK(opt->rpl_dio_len);

        while((opt->rpl_dio_type == RPL_OPT_PAD0 &&
               (const u_char *)opt < ndo->ndo_snapend) ||
              ND_TTEST2(*opt,(opt->rpl_dio_len+2))) {

                unsigned int optlen = opt->rpl_dio_len+2;
                if(opt->rpl_dio_type == RPL_OPT_PAD0) {
                        optlen = 1;
                        ND_PRINT((ndo, " opt:pad0"));
                } else {
                        ND_PRINT((ndo, " opt:%s len:%u ",
                                  tok2str(rpl_subopt_values, "subopt:%u", opt->rpl_dio_type),
                                  optlen));
                        if(ndo->ndo_vflag > 2) {
                                unsigned int paylen = opt->rpl_dio_len;
                                if(paylen > length) paylen = length;
                                hex_print(ndo,
                                          " ",
                                          ((const uint8_t *)opt) + RPL_DIO_GENOPTION_LEN,  /* content of DIO option */
                                          paylen);
                        }
                }
                opt = (const struct rpl_dio_genoption *)(((const char *)opt) + optlen);
                length -= optlen;
        }
        return;
trunc:
	ND_PRINT((ndo," [|truncated]"));
	return;
}

static void
rpl_dio_print(netdissect_options *ndo,
              const u_char *bp, u_int length)
{
        const struct nd_rpl_dio *dio = (const struct nd_rpl_dio *)bp;
        const char *dagid_str;

        ND_TCHECK(*dio);
        dagid_str = ip6addr_string (ndo, dio->rpl_dagid);

        ND_PRINT((ndo, " [dagid:%s,seq:%u,instance:%u,rank:%u,%smop:%s,prf:%u]",
                  dagid_str,
                  dio->rpl_dtsn,
                  dio->rpl_instanceid,
                  EXTRACT_16BITS(&dio->rpl_dagrank),
                  RPL_DIO_GROUNDED(dio->rpl_mopprf) ? "grounded,":"",
                  tok2str(rpl_mop_values, "mop%u", RPL_DIO_MOP(dio->rpl_mopprf)),
                  RPL_DIO_PRF(dio->rpl_mopprf)));

        if(ndo->ndo_vflag > 1) {
                const struct rpl_dio_genoption *opt = (const struct rpl_dio_genoption *)&dio[1];
                rpl_dio_printopt(ndo, opt, length);
        }
	return;
trunc:
	ND_PRINT((ndo," [|truncated]"));
	return;
}

static void
rpl_dao_print(netdissect_options *ndo,
              const u_char *bp, u_int length)
{
        const struct nd_rpl_dao *dao = (const struct nd_rpl_dao *)bp;
        const char *dagid_str = "<elided>";

        ND_TCHECK(*dao);
        if (length < ND_RPL_DAO_MIN_LEN)
        	goto tooshort;

        bp += ND_RPL_DAO_MIN_LEN;
        length -= ND_RPL_DAO_MIN_LEN;
        if(RPL_DAO_D(dao->rpl_flags)) {
                ND_TCHECK2(dao->rpl_dagid, DAGID_LEN);
                if (length < DAGID_LEN)
                	goto tooshort;
                dagid_str = ip6addr_string (ndo, dao->rpl_dagid);
                bp += DAGID_LEN;
                length -= DAGID_LEN;
        }

        ND_PRINT((ndo, " [dagid:%s,seq:%u,instance:%u%s%s,%02x]",
                  dagid_str,
                  dao->rpl_daoseq,
                  dao->rpl_instanceid,
                  RPL_DAO_K(dao->rpl_flags) ? ",acK":"",
                  RPL_DAO_D(dao->rpl_flags) ? ",Dagid":"",
                  dao->rpl_flags));

        if(ndo->ndo_vflag > 1) {
                const struct rpl_dio_genoption *opt = (const struct rpl_dio_genoption *)bp;
                rpl_dio_printopt(ndo, opt, length);
        }
	return;

trunc:
	ND_PRINT((ndo," [|truncated]"));
	return;

tooshort:
	ND_PRINT((ndo," [|length too short]"));
	return;
}

static void
rpl_daoack_print(netdissect_options *ndo,
                 const u_char *bp, u_int length)
{
        const struct nd_rpl_daoack *daoack = (const struct nd_rpl_daoack *)bp;
        const char *dagid_str = "<elided>";

        ND_TCHECK2(*daoack, ND_RPL_DAOACK_MIN_LEN);
        if (length < ND_RPL_DAOACK_MIN_LEN)
        	goto tooshort;

        bp += ND_RPL_DAOACK_MIN_LEN;
        length -= ND_RPL_DAOACK_MIN_LEN;
        if(RPL_DAOACK_D(daoack->rpl_flags)) {
                ND_TCHECK2(daoack->rpl_dagid, DAGID_LEN);
                if (length < DAGID_LEN)
                	goto tooshort;
                dagid_str = ip6addr_string (ndo, daoack->rpl_dagid);
                bp += DAGID_LEN;
                length -= DAGID_LEN;
        }

        ND_PRINT((ndo, " [dagid:%s,seq:%u,instance:%u,status:%u]",
                  dagid_str,
                  daoack->rpl_daoseq,
                  daoack->rpl_instanceid,
                  daoack->rpl_status));

        /* no officially defined options for DAOACK, but print any we find */
        if(ndo->ndo_vflag > 1) {
                const struct rpl_dio_genoption *opt = (const struct rpl_dio_genoption *)bp;
                rpl_dio_printopt(ndo, opt, length);
        }
	return;

trunc:
	ND_PRINT((ndo," [|dao-truncated]"));
	return;

tooshort:
	ND_PRINT((ndo," [|dao-length too short]"));
	return;
}

static void
rpl_print(netdissect_options *ndo,
          const struct icmp6_hdr *hdr,
          const u_char *bp, u_int length)
{
        int secured = hdr->icmp6_code & 0x80;
        int basecode= hdr->icmp6_code & 0x7f;

        if(secured) {
                ND_PRINT((ndo, ", (SEC) [worktodo]"));
                /* XXX
                 * the next header pointer needs to move forward to
                 * skip the secure part.
                 */
                return;
        } else {
                ND_PRINT((ndo, ", (CLR)"));
        }

        switch(basecode) {
        case ND_RPL_DAG_IS:
                ND_PRINT((ndo, "DODAG Information Solicitation"));
                if(ndo->ndo_vflag) {
                }
                break;
        case ND_RPL_DAG_IO:
                ND_PRINT((ndo, "DODAG Information Object"));
                if(ndo->ndo_vflag) {
                        rpl_dio_print(ndo, bp, length);
                }
                break;
        case ND_RPL_DAO:
                ND_PRINT((ndo, "Destination Advertisement Object"));
                if(ndo->ndo_vflag) {
                        rpl_dao_print(ndo, bp, length);
                }
                break;
        case ND_RPL_DAO_ACK:
                ND_PRINT((ndo, "Destination Advertisement Object Ack"));
                if(ndo->ndo_vflag) {
                        rpl_daoack_print(ndo, bp, length);
                }
                break;
        default:
                ND_PRINT((ndo, "RPL message, unknown code %u",hdr->icmp6_code));
                break;
        }
	return;

#if 0
trunc:
	ND_PRINT((ndo," [|truncated]"));
	return;
#endif

}


void
icmp6_print(netdissect_options *ndo,
            const u_char *bp, u_int length, const u_char *bp2, int fragmented)
{
	const struct icmp6_hdr *dp;
	const struct ip6_hdr *ip;
	const struct ip6_hdr *oip;
	const struct udphdr *ouh;
	int dport;
	const u_char *ep;
	u_int prot;

	dp = (const struct icmp6_hdr *)bp;
	ip = (const struct ip6_hdr *)bp2;
	oip = (const struct ip6_hdr *)(dp + 1);
	/* 'ep' points to the end of available data. */
	ep = ndo->ndo_snapend;

	ND_TCHECK(dp->icmp6_cksum);

	if (ndo->ndo_vflag && !fragmented) {
		uint16_t sum, udp_sum;

		if (ND_TTEST2(bp[0], length)) {
			udp_sum = EXTRACT_16BITS(&dp->icmp6_cksum);
			sum = icmp6_cksum(ndo, ip, dp, length);
			if (sum != 0)
				ND_PRINT((ndo,"[bad icmp6 cksum 0x%04x -> 0x%04x!] ",
                                                udp_sum,
                                                in_cksum_shouldbe(udp_sum, sum)));
			else
				ND_PRINT((ndo,"[icmp6 sum ok] "));
		}
	}

        ND_PRINT((ndo,"ICMP6, %s", tok2str(icmp6_type_values,"unknown icmp6 type (%u)",dp->icmp6_type)));

        /* display cosmetics: print the packet length for printer that use the vflag now */
        if (ndo->ndo_vflag && (dp->icmp6_type == ND_ROUTER_SOLICIT ||
                      dp->icmp6_type == ND_ROUTER_ADVERT ||
                      dp->icmp6_type == ND_NEIGHBOR_ADVERT ||
                      dp->icmp6_type == ND_NEIGHBOR_SOLICIT ||
                      dp->icmp6_type == ND_REDIRECT ||
                      dp->icmp6_type == ICMP6_HADISCOV_REPLY ||
                      dp->icmp6_type == ICMP6_MOBILEPREFIX_ADVERT ))
                ND_PRINT((ndo,", length %u", length));

	switch (dp->icmp6_type) {
	case ICMP6_DST_UNREACH:
		ND_TCHECK(oip->ip6_dst);
                ND_PRINT((ndo,", %s", tok2str(icmp6_dst_unreach_code_values,"unknown unreach code (%u)",dp->icmp6_code)));
		switch (dp->icmp6_code) {

		case ICMP6_DST_UNREACH_NOROUTE: /* fall through */
		case ICMP6_DST_UNREACH_ADMIN:
		case ICMP6_DST_UNREACH_ADDR:
                        ND_PRINT((ndo," %s",ip6addr_string(ndo, &oip->ip6_dst)));
                        break;
		case ICMP6_DST_UNREACH_BEYONDSCOPE:
			ND_PRINT((ndo," %s, source address %s",
			       ip6addr_string(ndo, &oip->ip6_dst),
                                  ip6addr_string(ndo, &oip->ip6_src)));
			break;
		case ICMP6_DST_UNREACH_NOPORT:
			if ((ouh = get_upperlayer(ndo, (const u_char *)oip, &prot))
			    == NULL)
				goto trunc;

			dport = EXTRACT_16BITS(&ouh->uh_dport);
			switch (prot) {
			case IPPROTO_TCP:
				ND_PRINT((ndo,", %s tcp port %s",
					ip6addr_string(ndo, &oip->ip6_dst),
                                          tcpport_string(ndo, dport)));
				break;
			case IPPROTO_UDP:
				ND_PRINT((ndo,", %s udp port %s",
					ip6addr_string(ndo, &oip->ip6_dst),
                                          udpport_string(ndo, dport)));
				break;
			default:
				ND_PRINT((ndo,", %s protocol %d port %d unreachable",
					ip6addr_string(ndo, &oip->ip6_dst),
                                          oip->ip6_nxt, dport));
				break;
			}
			break;
		default:
                  if (ndo->ndo_vflag <= 1) {
                    print_unknown_data(ndo, bp,"\n\t",length);
                    return;
                  }
                    break;
		}
		break;
	case ICMP6_PACKET_TOO_BIG:
		ND_TCHECK(dp->icmp6_mtu);
		ND_PRINT((ndo,", mtu %u", EXTRACT_32BITS(&dp->icmp6_mtu)));
		break;
	case ICMP6_TIME_EXCEEDED:
		ND_TCHECK(oip->ip6_dst);
		switch (dp->icmp6_code) {
		case ICMP6_TIME_EXCEED_TRANSIT:
			ND_PRINT((ndo," for %s",
                                  ip6addr_string(ndo, &oip->ip6_dst)));
			break;
		case ICMP6_TIME_EXCEED_REASSEMBLY:
			ND_PRINT((ndo," (reassembly)"));
			break;
		default:
                        ND_PRINT((ndo,", unknown code (%u)", dp->icmp6_code));
			break;
		}
		break;
	case ICMP6_PARAM_PROB:
		ND_TCHECK(oip->ip6_dst);
		switch (dp->icmp6_code) {
		case ICMP6_PARAMPROB_HEADER:
                        ND_PRINT((ndo,", erroneous - octet %u", EXTRACT_32BITS(&dp->icmp6_pptr)));
                        break;
		case ICMP6_PARAMPROB_NEXTHEADER:
                        ND_PRINT((ndo,", next header - octet %u", EXTRACT_32BITS(&dp->icmp6_pptr)));
                        break;
		case ICMP6_PARAMPROB_OPTION:
                        ND_PRINT((ndo,", option - octet %u", EXTRACT_32BITS(&dp->icmp6_pptr)));
                        break;
		default:
                        ND_PRINT((ndo,", code-#%d",
                                  dp->icmp6_code));
                        break;
		}
		break;
	case ICMP6_ECHO_REQUEST:
	case ICMP6_ECHO_REPLY:
                ND_TCHECK(dp->icmp6_seq);
                ND_PRINT((ndo,", seq %u", EXTRACT_16BITS(&dp->icmp6_seq)));
		break;
	case ICMP6_MEMBERSHIP_QUERY:
		if (length == MLD_MINLEN) {
			mld6_print(ndo, (const u_char *)dp);
		} else if (length >= MLDV2_MINLEN) {
			ND_PRINT((ndo," v2"));
			mldv2_query_print(ndo, (const u_char *)dp, length);
		} else {
                        ND_PRINT((ndo," unknown-version (len %u) ", length));
		}
		break;
	case ICMP6_MEMBERSHIP_REPORT:
		mld6_print(ndo, (const u_char *)dp);
		break;
	case ICMP6_MEMBERSHIP_REDUCTION:
		mld6_print(ndo, (const u_char *)dp);
		break;
	case ND_ROUTER_SOLICIT:
#define RTSOLLEN 8
		if (ndo->ndo_vflag) {
			icmp6_opt_print(ndo, (const u_char *)dp + RTSOLLEN,
					length - RTSOLLEN);
		}
		break;
	case ND_ROUTER_ADVERT:
#define RTADVLEN 16
		if (ndo->ndo_vflag) {
			const struct nd_router_advert *p;

			p = (const struct nd_router_advert *)dp;
			ND_TCHECK(p->nd_ra_retransmit);
			ND_PRINT((ndo,"\n\thop limit %u, Flags [%s]" \
                                  ", pref %s, router lifetime %us, reachable time %us, retrans time %us",
                                  (u_int)p->nd_ra_curhoplimit,
                                  bittok2str(icmp6_opt_ra_flag_values,"none",(p->nd_ra_flags_reserved)),
                                  get_rtpref(p->nd_ra_flags_reserved),
                                  EXTRACT_16BITS(&p->nd_ra_router_lifetime),
                                  EXTRACT_32BITS(&p->nd_ra_reachable),
                                  EXTRACT_32BITS(&p->nd_ra_retransmit)));

			icmp6_opt_print(ndo, (const u_char *)dp + RTADVLEN,
					length - RTADVLEN);
		}
		break;
	case ND_NEIGHBOR_SOLICIT:
	    {
		const struct nd_neighbor_solicit *p;
		p = (const struct nd_neighbor_solicit *)dp;
		ND_TCHECK(p->nd_ns_target);
		ND_PRINT((ndo,", who has %s", ip6addr_string(ndo, &p->nd_ns_target)));
		if (ndo->ndo_vflag) {
#define NDSOLLEN 24
			icmp6_opt_print(ndo, (const u_char *)dp + NDSOLLEN,
					length - NDSOLLEN);
		}
	    }
		break;
	case ND_NEIGHBOR_ADVERT:
	    {
		const struct nd_neighbor_advert *p;

		p = (const struct nd_neighbor_advert *)dp;
		ND_TCHECK(p->nd_na_target);
		ND_PRINT((ndo,", tgt is %s",
                          ip6addr_string(ndo, &p->nd_na_target)));
		if (ndo->ndo_vflag) {
                        ND_PRINT((ndo,", Flags [%s]",
                                  bittok2str(icmp6_nd_na_flag_values,
                                             "none",
                                             EXTRACT_32BITS(&p->nd_na_flags_reserved))));
#define NDADVLEN 24
			icmp6_opt_print(ndo, (const u_char *)dp + NDADVLEN,
					length - NDADVLEN);
#undef NDADVLEN
		}
	    }
		break;
	case ND_REDIRECT:
#define RDR(i) ((const struct nd_redirect *)(i))
                         ND_TCHECK(RDR(dp)->nd_rd_dst);
                         ND_PRINT((ndo,", %s", ip6addr_string(ndo, &RDR(dp)->nd_rd_dst)));
		ND_TCHECK(RDR(dp)->nd_rd_target);
		ND_PRINT((ndo," to %s",
                          ip6addr_string(ndo, &RDR(dp)->nd_rd_target)));
#define REDIRECTLEN 40
		if (ndo->ndo_vflag) {
			icmp6_opt_print(ndo, (const u_char *)dp + REDIRECTLEN,
					length - REDIRECTLEN);
		}
		break;
#undef REDIRECTLEN
#undef RDR
	case ICMP6_ROUTER_RENUMBERING:
		icmp6_rrenum_print(ndo, bp, ep);
		break;
	case ICMP6_NI_QUERY:
	case ICMP6_NI_REPLY:
		icmp6_nodeinfo_print(ndo, length, bp, ep);
		break;
	case IND_SOLICIT:
	case IND_ADVERT:
		break;
	case ICMP6_V2_MEMBERSHIP_REPORT:
		mldv2_report_print(ndo, (const u_char *) dp, length);
		break;
	case ICMP6_MOBILEPREFIX_SOLICIT: /* fall through */
	case ICMP6_HADISCOV_REQUEST:
                ND_TCHECK(dp->icmp6_data16[0]);
                ND_PRINT((ndo,", id 0x%04x", EXTRACT_16BITS(&dp->icmp6_data16[0])));
                break;
	case ICMP6_HADISCOV_REPLY:
		if (ndo->ndo_vflag) {
			const struct in6_addr *in6;
			const u_char *cp;

			ND_TCHECK(dp->icmp6_data16[0]);
			ND_PRINT((ndo,", id 0x%04x", EXTRACT_16BITS(&dp->icmp6_data16[0])));
			cp = (const u_char *)dp + length;
			in6 = (const struct in6_addr *)(dp + 1);
			for (; (const u_char *)in6 < cp; in6++) {
				ND_TCHECK(*in6);
				ND_PRINT((ndo,", %s", ip6addr_string(ndo, in6)));
			}
		}
		break;
	case ICMP6_MOBILEPREFIX_ADVERT:
		if (ndo->ndo_vflag) {
			ND_TCHECK(dp->icmp6_data16[0]);
			ND_PRINT((ndo,", id 0x%04x", EXTRACT_16BITS(&dp->icmp6_data16[0])));
			ND_TCHECK(dp->icmp6_data16[1]);
			if (dp->icmp6_data16[1] & 0xc0)
				ND_PRINT((ndo," "));
			if (dp->icmp6_data16[1] & 0x80)
				ND_PRINT((ndo,"M"));
			if (dp->icmp6_data16[1] & 0x40)
				ND_PRINT((ndo,"O"));
#define MPADVLEN 8
			icmp6_opt_print(ndo, (const u_char *)dp + MPADVLEN,
					length - MPADVLEN);
		}
		break;
        case ND_RPL_MESSAGE:
                /* plus 4, because struct icmp6_hdr contains 4 bytes of icmp payload */
                rpl_print(ndo, dp, &dp->icmp6_data8[0], length-sizeof(struct icmp6_hdr)+4);
                break;
	default:
                ND_PRINT((ndo,", length %u", length));
                if (ndo->ndo_vflag <= 1)
                        print_unknown_data(ndo, bp,"\n\t", length);
                return;
        }
        if (!ndo->ndo_vflag)
                ND_PRINT((ndo,", length %u", length));
	return;
trunc:
	ND_PRINT((ndo, "[|icmp6]"));
}

static const struct udphdr *
get_upperlayer(netdissect_options *ndo, const u_char *bp, u_int *prot)
{
	const u_char *ep;
	const struct ip6_hdr *ip6 = (const struct ip6_hdr *)bp;
	const struct udphdr *uh;
	const struct ip6_hbh *hbh;
	const struct ip6_frag *fragh;
	const struct ah *ah;
	u_int nh;
	int hlen;

	/* 'ep' points to the end of available data. */
	ep = ndo->ndo_snapend;

	if (!ND_TTEST(ip6->ip6_nxt))
		return NULL;

	nh = ip6->ip6_nxt;
	hlen = sizeof(struct ip6_hdr);

	while (bp < ep) {
		bp += hlen;

		switch(nh) {
		case IPPROTO_UDP:
		case IPPROTO_TCP:
			uh = (const struct udphdr *)bp;
			if (ND_TTEST(uh->uh_dport)) {
				*prot = nh;
				return(uh);
			}
			else
				return(NULL);
			/* NOTREACHED */

		case IPPROTO_HOPOPTS:
		case IPPROTO_DSTOPTS:
		case IPPROTO_ROUTING:
			hbh = (const struct ip6_hbh *)bp;
			if (!ND_TTEST(hbh->ip6h_len))
				return(NULL);
			nh = hbh->ip6h_nxt;
			hlen = (hbh->ip6h_len + 1) << 3;
			break;

		case IPPROTO_FRAGMENT: /* this should be odd, but try anyway */
			fragh = (const struct ip6_frag *)bp;
			if (!ND_TTEST(fragh->ip6f_offlg))
				return(NULL);
			/* fragments with non-zero offset are meaningless */
			if ((EXTRACT_16BITS(&fragh->ip6f_offlg) & IP6F_OFF_MASK) != 0)
				return(NULL);
			nh = fragh->ip6f_nxt;
			hlen = sizeof(struct ip6_frag);
			break;

		case IPPROTO_AH:
			ah = (const struct ah *)bp;
			if (!ND_TTEST(ah->ah_len))
				return(NULL);
			nh = ah->ah_nxt;
			hlen = (ah->ah_len + 2) << 2;
			break;

		default:	/* unknown or undecodable header */
			*prot = nh; /* meaningless, but set here anyway */
			return(NULL);
		}
	}

	return(NULL);		/* should be notreached, though */
}

static void
icmp6_opt_print(netdissect_options *ndo, const u_char *bp, int resid)
{
	const struct nd_opt_hdr *op;
	const struct nd_opt_prefix_info *opp;
	const struct nd_opt_mtu *opm;
	const struct nd_opt_rdnss *oprd;
	const struct nd_opt_dnssl *opds;
	const struct nd_opt_advinterval *opa;
	const struct nd_opt_homeagent_info *oph;
	const struct nd_opt_route_info *opri;
	const u_char *cp, *ep, *domp;
	struct in6_addr in6;
	const struct in6_addr *in6p;
	size_t l;
	u_int i;

#define ECHECK(var) if ((const u_char *)&(var) > ep - sizeof(var)) return

	cp = bp;
	/* 'ep' points to the end of available data. */
	ep = ndo->ndo_snapend;

	while (cp < ep) {
		op = (const struct nd_opt_hdr *)cp;

		ECHECK(op->nd_opt_len);
		if (resid <= 0)
			return;
		if (op->nd_opt_len == 0)
			goto trunc;
		if (cp + (op->nd_opt_len << 3) > ep)
			goto trunc;

                ND_PRINT((ndo,"\n\t  %s option (%u), length %u (%u): ",
                          tok2str(icmp6_opt_values, "unknown", op->nd_opt_type),
                          op->nd_opt_type,
                          op->nd_opt_len << 3,
                          op->nd_opt_len));

		switch (op->nd_opt_type) {
		case ND_OPT_SOURCE_LINKADDR:
			l = (op->nd_opt_len << 3) - 2;
			print_lladdr(ndo, cp + 2, l);
			break;
		case ND_OPT_TARGET_LINKADDR:
			l = (op->nd_opt_len << 3) - 2;
			print_lladdr(ndo, cp + 2, l);
			break;
		case ND_OPT_PREFIX_INFORMATION:
			opp = (const struct nd_opt_prefix_info *)op;
			ND_TCHECK(opp->nd_opt_pi_prefix);
                        ND_PRINT((ndo,"%s/%u%s, Flags [%s], valid time %s",
                                  ip6addr_string(ndo, &opp->nd_opt_pi_prefix),
                                  opp->nd_opt_pi_prefix_len,
                                  (op->nd_opt_len != 4) ? "badlen" : "",
                                  bittok2str(icmp6_opt_pi_flag_values, "none", opp->nd_opt_pi_flags_reserved),
                                  get_lifetime(EXTRACT_32BITS(&opp->nd_opt_pi_valid_time))));
                        ND_PRINT((ndo,", pref. time %s", get_lifetime(EXTRACT_32BITS(&opp->nd_opt_pi_preferred_time))));
			break;
		case ND_OPT_REDIRECTED_HEADER:
                        print_unknown_data(ndo, bp,"\n\t    ",op->nd_opt_len<<3);
			/* xxx */
			break;
		case ND_OPT_MTU:
			opm = (const struct nd_opt_mtu *)op;
			ND_TCHECK(opm->nd_opt_mtu_mtu);
			ND_PRINT((ndo," %u%s",
                               EXTRACT_32BITS(&opm->nd_opt_mtu_mtu),
                                  (op->nd_opt_len != 1) ? "bad option length" : "" ));
                        break;
		case ND_OPT_RDNSS:
			oprd = (const struct nd_opt_rdnss *)op;
			l = (op->nd_opt_len - 1) / 2;
			ND_PRINT((ndo," lifetime %us,",
                                  EXTRACT_32BITS(&oprd->nd_opt_rdnss_lifetime)));
			for (i = 0; i < l; i++) {
				ND_TCHECK(oprd->nd_opt_rdnss_addr[i]);
				ND_PRINT((ndo," addr: %s",
                                          ip6addr_string(ndo, &oprd->nd_opt_rdnss_addr[i])));
			}
			break;
		case ND_OPT_DNSSL:
			opds = (const struct nd_opt_dnssl *)op;
			ND_PRINT((ndo," lifetime %us, domain(s):",
                                  EXTRACT_32BITS(&opds->nd_opt_dnssl_lifetime)));
			domp = cp + 8; /* domain names, variable-sized, RFC1035-encoded */
			while (domp < cp + (op->nd_opt_len << 3) && *domp != '\0')
			{
				ND_PRINT((ndo, " "));
				if ((domp = ns_nprint (ndo, domp, bp)) == NULL)
					goto trunc;
			}
			break;
		case ND_OPT_ADVINTERVAL:
			opa = (const struct nd_opt_advinterval *)op;
			ND_TCHECK(opa->nd_opt_adv_interval);
			ND_PRINT((ndo," %ums", EXTRACT_32BITS(&opa->nd_opt_adv_interval)));
			break;
                case ND_OPT_HOMEAGENT_INFO:
			oph = (const struct nd_opt_homeagent_info *)op;
			ND_TCHECK(oph->nd_opt_hai_lifetime);
			ND_PRINT((ndo," preference %u, lifetime %u",
                                  EXTRACT_16BITS(&oph->nd_opt_hai_preference),
                                  EXTRACT_16BITS(&oph->nd_opt_hai_lifetime)));
			break;
		case ND_OPT_ROUTE_INFO:
			opri = (const struct nd_opt_route_info *)op;
			ND_TCHECK(opri->nd_opt_rti_lifetime);
			memset(&in6, 0, sizeof(in6));
			in6p = (const struct in6_addr *)(opri + 1);
			switch (op->nd_opt_len) {
			case 1:
				break;
			case 2:
				ND_TCHECK2(*in6p, 8);
				memcpy(&in6, opri + 1, 8);
				break;
			case 3:
				ND_TCHECK(*in6p);
				memcpy(&in6, opri + 1, sizeof(in6));
				break;
			default:
				goto trunc;
			}
			ND_PRINT((ndo," %s/%u", ip6addr_string(ndo, &in6),
                                  opri->nd_opt_rti_prefixlen));
			ND_PRINT((ndo,", pref=%s", get_rtpref(opri->nd_opt_rti_flags)));
			ND_PRINT((ndo,", lifetime=%s",
                                  get_lifetime(EXTRACT_32BITS(&opri->nd_opt_rti_lifetime))));
			break;
		default:
                        if (ndo->ndo_vflag <= 1) {
                                print_unknown_data(ndo,cp+2,"\n\t  ", (op->nd_opt_len << 3) - 2); /* skip option header */
                            return;
                        }
                        break;
		}
                /* do we want to see an additional hexdump ? */
                if (ndo->ndo_vflag> 1)
                        print_unknown_data(ndo, cp+2,"\n\t    ", (op->nd_opt_len << 3) - 2); /* skip option header */

		cp += op->nd_opt_len << 3;
		resid -= op->nd_opt_len << 3;
	}
	return;

 trunc:
	ND_PRINT((ndo, "[ndp opt]"));
	return;
#undef ECHECK
}

static void
mld6_print(netdissect_options *ndo, const u_char *bp)
{
	const struct mld6_hdr *mp = (const struct mld6_hdr *)bp;
	const u_char *ep;

	/* 'ep' points to the end of available data. */
	ep = ndo->ndo_snapend;

	if ((const u_char *)mp + sizeof(*mp) > ep)
		return;

	ND_PRINT((ndo,"max resp delay: %d ", EXTRACT_16BITS(&mp->mld6_maxdelay)));
	ND_PRINT((ndo,"addr: %s", ip6addr_string(ndo, &mp->mld6_addr)));
}

static void
mldv2_report_print(netdissect_options *ndo, const u_char *bp, u_int len)
{
    const struct icmp6_hdr *icp = (const struct icmp6_hdr *) bp;
    u_int group, nsrcs, ngroups;
    u_int i, j;

    /* Minimum len is 8 */
    if (len < 8) {
            ND_PRINT((ndo," [invalid len %d]", len));
            return;
    }

    ND_TCHECK(icp->icmp6_data16[1]);
    ngroups = EXTRACT_16BITS(&icp->icmp6_data16[1]);
    ND_PRINT((ndo,", %d group record(s)", ngroups));
    if (ndo->ndo_vflag > 0) {
	/* Print the group records */
	group = 8;
        for (i = 0; i < ngroups; i++) {
	    /* type(1) + auxlen(1) + numsrc(2) + grp(16) */
	    if (len < group + 20) {
                    ND_PRINT((ndo," [invalid number of groups]"));
                    return;
	    }
            ND_TCHECK2(bp[group + 4], sizeof(struct in6_addr));
            ND_PRINT((ndo," [gaddr %s", ip6addr_string(ndo, &bp[group + 4])));
	    ND_PRINT((ndo," %s", tok2str(mldv2report2str, " [v2-report-#%d]",
                                         bp[group])));
            nsrcs = (bp[group + 2] << 8) + bp[group + 3];
	    /* Check the number of sources and print them */
	    if (len < group + 20 + (nsrcs * sizeof(struct in6_addr))) {
                    ND_PRINT((ndo," [invalid number of sources %d]", nsrcs));
                    return;
	    }
            if (ndo->ndo_vflag == 1)
                    ND_PRINT((ndo,", %d source(s)", nsrcs));
            else {
		/* Print the sources */
                    ND_PRINT((ndo," {"));
                for (j = 0; j < nsrcs; j++) {
                    ND_TCHECK2(bp[group + 20 + j * sizeof(struct in6_addr)],
                            sizeof(struct in6_addr));
		    ND_PRINT((ndo," %s", ip6addr_string(ndo, &bp[group + 20 + j * sizeof(struct in6_addr)])));
		}
                ND_PRINT((ndo," }"));
            }
	    /* Next group record */
            group += 20 + nsrcs * sizeof(struct in6_addr);
	    ND_PRINT((ndo,"]"));
        }
    }
    return;
trunc:
    ND_PRINT((ndo,"[|icmp6]"));
    return;
}

static void
mldv2_query_print(netdissect_options *ndo, const u_char *bp, u_int len)
{
    const struct icmp6_hdr *icp = (const struct icmp6_hdr *) bp;
    u_int mrc;
    int mrt, qqi;
    u_int nsrcs;
    register u_int i;

    /* Minimum len is 28 */
    if (len < 28) {
            ND_PRINT((ndo," [invalid len %d]", len));
	return;
    }
    ND_TCHECK(icp->icmp6_data16[0]);
    mrc = EXTRACT_16BITS(&icp->icmp6_data16[0]);
    if (mrc < 32768) {
	mrt = mrc;
    } else {
        mrt = ((mrc & 0x0fff) | 0x1000) << (((mrc & 0x7000) >> 12) + 3);
    }
    if (ndo->ndo_vflag) {
            ND_PRINT((ndo," [max resp delay=%d]", mrt));
    }
    ND_TCHECK2(bp[8], sizeof(struct in6_addr));
    ND_PRINT((ndo," [gaddr %s", ip6addr_string(ndo, &bp[8])));

    if (ndo->ndo_vflag) {
        ND_TCHECK(bp[25]);
	if (bp[24] & 0x08) {
		ND_PRINT((ndo," sflag"));
	}
	if (bp[24] & 0x07) {
		ND_PRINT((ndo," robustness=%d", bp[24] & 0x07));
	}
	if (bp[25] < 128) {
		qqi = bp[25];
	} else {
		qqi = ((bp[25] & 0x0f) | 0x10) << (((bp[25] & 0x70) >> 4) + 3);
	}
	ND_PRINT((ndo," qqi=%d", qqi));
    }

    ND_TCHECK2(bp[26], 2);
    nsrcs = EXTRACT_16BITS(&bp[26]);
    if (nsrcs > 0) {
	if (len < 28 + nsrcs * sizeof(struct in6_addr))
	    ND_PRINT((ndo," [invalid number of sources]"));
	else if (ndo->ndo_vflag > 1) {
	    ND_PRINT((ndo," {"));
	    for (i = 0; i < nsrcs; i++) {
		ND_TCHECK2(bp[28 + i * sizeof(struct in6_addr)],
                        sizeof(struct in6_addr));
		ND_PRINT((ndo," %s", ip6addr_string(ndo, &bp[28 + i * sizeof(struct in6_addr)])));
	    }
	    ND_PRINT((ndo," }"));
	} else
                ND_PRINT((ndo,", %d source(s)", nsrcs));
    }
    ND_PRINT((ndo,"]"));
    return;
trunc:
    ND_PRINT((ndo,"[|icmp6]"));
    return;
}

static void
dnsname_print(netdissect_options *ndo, const u_char *cp, const u_char *ep)
{
	int i;

	/* DNS name decoding - no decompression */
	ND_PRINT((ndo,", \""));
	while (cp < ep) {
		i = *cp++;
		if (i) {
			if (i > ep - cp) {
				ND_PRINT((ndo,"???"));
				break;
			}
			while (i-- && cp < ep) {
				safeputchar(ndo, *cp);
				cp++;
			}
			if (cp + 1 < ep && *cp)
				ND_PRINT((ndo,"."));
		} else {
			if (cp == ep) {
				/* FQDN */
				ND_PRINT((ndo,"."));
			} else if (cp + 1 == ep && *cp == '\0') {
				/* truncated */
			} else {
				/* invalid */
				ND_PRINT((ndo,"???"));
			}
			break;
		}
	}
	ND_PRINT((ndo,"\""));
}

static void
icmp6_nodeinfo_print(netdissect_options *ndo, u_int icmp6len, const u_char *bp, const u_char *ep)
{
	const struct icmp6_nodeinfo *ni6;
	const struct icmp6_hdr *dp;
	const u_char *cp;
	size_t siz, i;
	int needcomma;

	if (ep < bp)
		return;
	dp = (const struct icmp6_hdr *)bp;
	ni6 = (const struct icmp6_nodeinfo *)bp;
	siz = ep - bp;

	switch (ni6->ni_type) {
	case ICMP6_NI_QUERY:
		if (siz == sizeof(*dp) + 4) {
			/* KAME who-are-you */
			ND_PRINT((ndo," who-are-you request"));
			break;
		}
		ND_PRINT((ndo," node information query"));

		ND_TCHECK2(*dp, sizeof(*ni6));
		ni6 = (const struct icmp6_nodeinfo *)dp;
		ND_PRINT((ndo," ("));	/*)*/
		switch (EXTRACT_16BITS(&ni6->ni_qtype)) {
		case NI_QTYPE_NOOP:
			ND_PRINT((ndo,"noop"));
			break;
		case NI_QTYPE_SUPTYPES:
			ND_PRINT((ndo,"supported qtypes"));
			i = EXTRACT_16BITS(&ni6->ni_flags);
			if (i)
				ND_PRINT((ndo," [%s]", (i & 0x01) ? "C" : ""));
			break;
		case NI_QTYPE_FQDN:
			ND_PRINT((ndo,"DNS name"));
			break;
		case NI_QTYPE_NODEADDR:
			ND_PRINT((ndo,"node addresses"));
			i = ni6->ni_flags;
			if (!i)
				break;
			/* NI_NODEADDR_FLAG_TRUNCATE undefined for query */
			ND_PRINT((ndo," [%s%s%s%s%s%s]",
			    (i & NI_NODEADDR_FLAG_ANYCAST) ? "a" : "",
			    (i & NI_NODEADDR_FLAG_GLOBAL) ? "G" : "",
			    (i & NI_NODEADDR_FLAG_SITELOCAL) ? "S" : "",
			    (i & NI_NODEADDR_FLAG_LINKLOCAL) ? "L" : "",
			    (i & NI_NODEADDR_FLAG_COMPAT) ? "C" : "",
			    (i & NI_NODEADDR_FLAG_ALL) ? "A" : ""));
			break;
		default:
			ND_PRINT((ndo,"unknown"));
			break;
		}

		if (ni6->ni_qtype == NI_QTYPE_NOOP ||
		    ni6->ni_qtype == NI_QTYPE_SUPTYPES) {
			if (siz != sizeof(*ni6))
				if (ndo->ndo_vflag)
					ND_PRINT((ndo,", invalid len"));
			/*(*/
			ND_PRINT((ndo,")"));
			break;
		}


		/* XXX backward compat, icmp-name-lookup-03 */
		if (siz == sizeof(*ni6)) {
			ND_PRINT((ndo,", 03 draft"));
			/*(*/
			ND_PRINT((ndo,")"));
			break;
		}

		switch (ni6->ni_code) {
		case ICMP6_NI_SUBJ_IPV6:
			if (!ND_TTEST2(*dp,
			    sizeof(*ni6) + sizeof(struct in6_addr)))
				break;
			if (siz != sizeof(*ni6) + sizeof(struct in6_addr)) {
				if (ndo->ndo_vflag)
					ND_PRINT((ndo,", invalid subject len"));
				break;
			}
			ND_PRINT((ndo,", subject=%s",
                                  ip6addr_string(ndo, ni6 + 1)));
			break;
		case ICMP6_NI_SUBJ_FQDN:
			ND_PRINT((ndo,", subject=DNS name"));
			cp = (const u_char *)(ni6 + 1);
			if (cp[0] == ep - cp - 1) {
				/* icmp-name-lookup-03, pascal string */
				if (ndo->ndo_vflag)
					ND_PRINT((ndo,", 03 draft"));
				cp++;
				ND_PRINT((ndo,", \""));
				while (cp < ep) {
					safeputchar(ndo, *cp);
					cp++;
				}
				ND_PRINT((ndo,"\""));
			} else
				dnsname_print(ndo, cp, ep);
			break;
		case ICMP6_NI_SUBJ_IPV4:
			if (!ND_TTEST2(*dp, sizeof(*ni6) + sizeof(struct in_addr)))
				break;
			if (siz != sizeof(*ni6) + sizeof(struct in_addr)) {
				if (ndo->ndo_vflag)
					ND_PRINT((ndo,", invalid subject len"));
				break;
			}
			ND_PRINT((ndo,", subject=%s",
                                  ipaddr_string(ndo, ni6 + 1)));
			break;
		default:
			ND_PRINT((ndo,", unknown subject"));
			break;
		}

		/*(*/
		ND_PRINT((ndo,")"));
		break;

	case ICMP6_NI_REPLY:
		if (icmp6len > siz) {
			ND_PRINT((ndo,"[|icmp6: node information reply]"));
			break;
		}

		needcomma = 0;

		ND_TCHECK2(*dp, sizeof(*ni6));
		ni6 = (const struct icmp6_nodeinfo *)dp;
		ND_PRINT((ndo," node information reply"));
		ND_PRINT((ndo," ("));	/*)*/
		switch (ni6->ni_code) {
		case ICMP6_NI_SUCCESS:
			if (ndo->ndo_vflag) {
				ND_PRINT((ndo,"success"));
				needcomma++;
			}
			break;
		case ICMP6_NI_REFUSED:
			ND_PRINT((ndo,"refused"));
			needcomma++;
			if (siz != sizeof(*ni6))
				if (ndo->ndo_vflag)
					ND_PRINT((ndo,", invalid length"));
			break;
		case ICMP6_NI_UNKNOWN:
			ND_PRINT((ndo,"unknown"));
			needcomma++;
			if (siz != sizeof(*ni6))
				if (ndo->ndo_vflag)
					ND_PRINT((ndo,", invalid length"));
			break;
		}

		if (ni6->ni_code != ICMP6_NI_SUCCESS) {
			/*(*/
			ND_PRINT((ndo,")"));
			break;
		}

		switch (EXTRACT_16BITS(&ni6->ni_qtype)) {
		case NI_QTYPE_NOOP:
			if (needcomma)
				ND_PRINT((ndo,", "));
			ND_PRINT((ndo,"noop"));
			if (siz != sizeof(*ni6))
				if (ndo->ndo_vflag)
					ND_PRINT((ndo,", invalid length"));
			break;
		case NI_QTYPE_SUPTYPES:
			if (needcomma)
				ND_PRINT((ndo,", "));
			ND_PRINT((ndo,"supported qtypes"));
			i = EXTRACT_16BITS(&ni6->ni_flags);
			if (i)
				ND_PRINT((ndo," [%s]", (i & 0x01) ? "C" : ""));
			break;
		case NI_QTYPE_FQDN:
			if (needcomma)
				ND_PRINT((ndo,", "));
			ND_PRINT((ndo,"DNS name"));
			cp = (const u_char *)(ni6 + 1) + 4;
			ND_TCHECK(cp[0]);
			if (cp[0] == ep - cp - 1) {
				/* icmp-name-lookup-03, pascal string */
				if (ndo->ndo_vflag)
					ND_PRINT((ndo,", 03 draft"));
				cp++;
				ND_PRINT((ndo,", \""));
				while (cp < ep) {
					safeputchar(ndo, *cp);
					cp++;
				}
				ND_PRINT((ndo,"\""));
			} else
				dnsname_print(ndo, cp, ep);
			if ((EXTRACT_16BITS(&ni6->ni_flags) & 0x01) != 0)
				ND_PRINT((ndo," [TTL=%u]", EXTRACT_32BITS(ni6 + 1)));
			break;
		case NI_QTYPE_NODEADDR:
			if (needcomma)
				ND_PRINT((ndo,", "));
			ND_PRINT((ndo,"node addresses"));
			i = sizeof(*ni6);
			while (i < siz) {
				if (i + sizeof(struct in6_addr) + sizeof(int32_t) > siz)
					break;
				ND_PRINT((ndo," %s", ip6addr_string(ndo, bp + i)));
				i += sizeof(struct in6_addr);
				ND_PRINT((ndo,"(%d)", (int32_t)EXTRACT_32BITS(bp + i)));
				i += sizeof(int32_t);
			}
			i = ni6->ni_flags;
			if (!i)
				break;
			ND_PRINT((ndo," [%s%s%s%s%s%s%s]",
                                  (i & NI_NODEADDR_FLAG_ANYCAST) ? "a" : "",
                                  (i & NI_NODEADDR_FLAG_GLOBAL) ? "G" : "",
                                  (i & NI_NODEADDR_FLAG_SITELOCAL) ? "S" : "",
                                  (i & NI_NODEADDR_FLAG_LINKLOCAL) ? "L" : "",
                                  (i & NI_NODEADDR_FLAG_COMPAT) ? "C" : "",
                                  (i & NI_NODEADDR_FLAG_ALL) ? "A" : "",
                                  (i & NI_NODEADDR_FLAG_TRUNCATE) ? "T" : ""));
			break;
		default:
			if (needcomma)
				ND_PRINT((ndo,", "));
			ND_PRINT((ndo,"unknown"));
			break;
		}

		/*(*/
		ND_PRINT((ndo,")"));
		break;
	}
	return;

trunc:
	ND_PRINT((ndo, "[|icmp6]"));
}

static void
icmp6_rrenum_print(netdissect_options *ndo, const u_char *bp, const u_char *ep)
{
	const struct icmp6_router_renum *rr6;
	const char *cp;
	const struct rr_pco_match *match;
	const struct rr_pco_use *use;
	char hbuf[NI_MAXHOST];
	int n;

	if (ep < bp)
		return;
	rr6 = (const struct icmp6_router_renum *)bp;
	cp = (const char *)(rr6 + 1);

	ND_TCHECK(rr6->rr_reserved);
	switch (rr6->rr_code) {
	case ICMP6_ROUTER_RENUMBERING_COMMAND:
		ND_PRINT((ndo,"router renum: command"));
		break;
	case ICMP6_ROUTER_RENUMBERING_RESULT:
		ND_PRINT((ndo,"router renum: result"));
		break;
	case ICMP6_ROUTER_RENUMBERING_SEQNUM_RESET:
		ND_PRINT((ndo,"router renum: sequence number reset"));
		break;
	default:
		ND_PRINT((ndo,"router renum: code-#%d", rr6->rr_code));
		break;
	}

        ND_PRINT((ndo,", seq=%u", EXTRACT_32BITS(&rr6->rr_seqnum)));

	if (ndo->ndo_vflag) {
#define F(x, y)	((rr6->rr_flags) & (x) ? (y) : "")
		ND_PRINT((ndo,"["));	/*]*/
		if (rr6->rr_flags) {
			ND_PRINT((ndo,"%s%s%s%s%s,", F(ICMP6_RR_FLAGS_TEST, "T"),
                                  F(ICMP6_RR_FLAGS_REQRESULT, "R"),
                                  F(ICMP6_RR_FLAGS_FORCEAPPLY, "A"),
                                  F(ICMP6_RR_FLAGS_SPECSITE, "S"),
                                  F(ICMP6_RR_FLAGS_PREVDONE, "P")));
		}
                ND_PRINT((ndo,"seg=%u,", rr6->rr_segnum));
                ND_PRINT((ndo,"maxdelay=%u", EXTRACT_16BITS(&rr6->rr_maxdelay)));
		if (rr6->rr_reserved)
			ND_PRINT((ndo,"rsvd=0x%x", EXTRACT_32BITS(&rr6->rr_reserved)));
		/*[*/
		ND_PRINT((ndo,"]"));
#undef F
	}

	if (rr6->rr_code == ICMP6_ROUTER_RENUMBERING_COMMAND) {
		match = (const struct rr_pco_match *)cp;
		cp = (const char *)(match + 1);

		ND_TCHECK(match->rpm_prefix);

		if (ndo->ndo_vflag > 1)
			ND_PRINT((ndo,"\n\t"));
		else
			ND_PRINT((ndo," "));
		ND_PRINT((ndo,"match("));	/*)*/
		switch (match->rpm_code) {
		case RPM_PCO_ADD:	ND_PRINT((ndo,"add")); break;
		case RPM_PCO_CHANGE:	ND_PRINT((ndo,"change")); break;
		case RPM_PCO_SETGLOBAL:	ND_PRINT((ndo,"setglobal")); break;
		default:		ND_PRINT((ndo,"#%u", match->rpm_code)); break;
		}

		if (ndo->ndo_vflag) {
			ND_PRINT((ndo,",ord=%u", match->rpm_ordinal));
			ND_PRINT((ndo,",min=%u", match->rpm_minlen));
			ND_PRINT((ndo,",max=%u", match->rpm_maxlen));
		}
		if (addrtostr6(&match->rpm_prefix, hbuf, sizeof(hbuf)))
			ND_PRINT((ndo,",%s/%u", hbuf, match->rpm_matchlen));
		else
			ND_PRINT((ndo,",?/%u", match->rpm_matchlen));
		/*(*/
		ND_PRINT((ndo,")"));

		n = match->rpm_len - 3;
		if (n % 4)
			goto trunc;
		n /= 4;
		while (n-- > 0) {
			use = (const struct rr_pco_use *)cp;
			cp = (const char *)(use + 1);

			ND_TCHECK(use->rpu_prefix);

			if (ndo->ndo_vflag > 1)
				ND_PRINT((ndo,"\n\t"));
			else
				ND_PRINT((ndo," "));
			ND_PRINT((ndo,"use("));	/*)*/
			if (use->rpu_flags) {
#define F(x, y)	((use->rpu_flags) & (x) ? (y) : "")
				ND_PRINT((ndo,"%s%s,",
                                          F(ICMP6_RR_PCOUSE_FLAGS_DECRVLTIME, "V"),
                                          F(ICMP6_RR_PCOUSE_FLAGS_DECRPLTIME, "P")));
#undef F
			}
			if (ndo->ndo_vflag) {
				ND_PRINT((ndo,"mask=0x%x,", use->rpu_ramask));
				ND_PRINT((ndo,"raflags=0x%x,", use->rpu_raflags));
				if (~use->rpu_vltime == 0)
					ND_PRINT((ndo,"vltime=infty,"));
				else
					ND_PRINT((ndo,"vltime=%u,",
                                                  EXTRACT_32BITS(&use->rpu_vltime)));
				if (~use->rpu_pltime == 0)
					ND_PRINT((ndo,"pltime=infty,"));
				else
					ND_PRINT((ndo,"pltime=%u,",
                                                  EXTRACT_32BITS(&use->rpu_pltime)));
			}
			if (addrtostr6(&use->rpu_prefix, hbuf, sizeof(hbuf)))
				ND_PRINT((ndo,"%s/%u/%u", hbuf, use->rpu_uselen,
                                          use->rpu_keeplen));
			else
				ND_PRINT((ndo,"?/%u/%u", use->rpu_uselen,
                                          use->rpu_keeplen));
			/*(*/
                        ND_PRINT((ndo,")"));
		}
	}

	return;

trunc:
	ND_PRINT((ndo,"[|icmp6]"));
}

/*
 * Local Variables:
 * c-style: whitesmith
 * c-basic-offset: 8
 * End:
 */
