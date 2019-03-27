/*
 * Copyright (c) 1988, 1989, 1990, 1991, 1993, 1994, 1995, 1996
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

/* \summary: Internet Control Message Protocol (ICMP) printer */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <netdissect-stdinc.h>

#include <stdio.h>
#include <string.h>

#include "netdissect.h"
#include "addrtoname.h"
#include "extract.h"

#include "ip.h"
#include "udp.h"
#include "ipproto.h"
#include "mpls.h"

/*
 * Interface Control Message Protocol Definitions.
 * Per RFC 792, September 1981.
 */

/*
 * Structure of an icmp header.
 */
struct icmp {
	uint8_t  icmp_type;		/* type of message, see below */
	uint8_t  icmp_code;		/* type sub code */
	uint16_t icmp_cksum;		/* ones complement cksum of struct */
	union {
		uint8_t ih_pptr;			/* ICMP_PARAMPROB */
		struct in_addr ih_gwaddr;	/* ICMP_REDIRECT */
		struct ih_idseq {
			uint16_t icd_id;
			uint16_t icd_seq;
		} ih_idseq;
		uint32_t ih_void;
	} icmp_hun;
#define	icmp_pptr	icmp_hun.ih_pptr
#define	icmp_gwaddr	icmp_hun.ih_gwaddr
#define	icmp_id		icmp_hun.ih_idseq.icd_id
#define	icmp_seq	icmp_hun.ih_idseq.icd_seq
#define	icmp_void	icmp_hun.ih_void
	union {
		struct id_ts {
			uint32_t its_otime;
			uint32_t its_rtime;
			uint32_t its_ttime;
		} id_ts;
		struct id_ip  {
			struct ip idi_ip;
			/* options and then 64 bits of data */
		} id_ip;
		uint32_t id_mask;
		uint8_t id_data[1];
	} icmp_dun;
#define	icmp_otime	icmp_dun.id_ts.its_otime
#define	icmp_rtime	icmp_dun.id_ts.its_rtime
#define	icmp_ttime	icmp_dun.id_ts.its_ttime
#define	icmp_ip		icmp_dun.id_ip.idi_ip
#define	icmp_mask	icmp_dun.id_mask
#define	icmp_data	icmp_dun.id_data
};

#define ICMP_MPLS_EXT_EXTRACT_VERSION(x) (((x)&0xf0)>>4)
#define ICMP_MPLS_EXT_VERSION 2

/*
 * Lower bounds on packet lengths for various types.
 * For the error advice packets must first insure that the
 * packet is large enought to contain the returned ip header.
 * Only then can we do the check to see if 64 bits of packet
 * data have been returned, since we need to check the returned
 * ip header length.
 */
#define	ICMP_MINLEN	8				/* abs minimum */
#define ICMP_EXTD_MINLEN (156 - sizeof (struct ip))     /* draft-bonica-internet-icmp-08 */
#define	ICMP_TSLEN	(8 + 3 * sizeof (uint32_t))	/* timestamp */
#define	ICMP_MASKLEN	12				/* address mask */
#define	ICMP_ADVLENMIN	(8 + sizeof (struct ip) + 8)	/* min */
#define	ICMP_ADVLEN(p)	(8 + (IP_HL(&(p)->icmp_ip) << 2) + 8)
	/* N.B.: must separately check that ip_hl >= 5 */

/*
 * Definition of type and code field values.
 */
#define	ICMP_ECHOREPLY		0		/* echo reply */
#define	ICMP_UNREACH		3		/* dest unreachable, codes: */
#define		ICMP_UNREACH_NET	0		/* bad net */
#define		ICMP_UNREACH_HOST	1		/* bad host */
#define		ICMP_UNREACH_PROTOCOL	2		/* bad protocol */
#define		ICMP_UNREACH_PORT	3		/* bad port */
#define		ICMP_UNREACH_NEEDFRAG	4		/* IP_DF caused drop */
#define		ICMP_UNREACH_SRCFAIL	5		/* src route failed */
#define		ICMP_UNREACH_NET_UNKNOWN 6		/* unknown net */
#define		ICMP_UNREACH_HOST_UNKNOWN 7		/* unknown host */
#define		ICMP_UNREACH_ISOLATED	8		/* src host isolated */
#define		ICMP_UNREACH_NET_PROHIB	9		/* prohibited access */
#define		ICMP_UNREACH_HOST_PROHIB 10		/* ditto */
#define		ICMP_UNREACH_TOSNET	11		/* bad tos for net */
#define		ICMP_UNREACH_TOSHOST	12		/* bad tos for host */
#define	ICMP_SOURCEQUENCH	4		/* packet lost, slow down */
#define	ICMP_REDIRECT		5		/* shorter route, codes: */
#define		ICMP_REDIRECT_NET	0		/* for network */
#define		ICMP_REDIRECT_HOST	1		/* for host */
#define		ICMP_REDIRECT_TOSNET	2		/* for tos and net */
#define		ICMP_REDIRECT_TOSHOST	3		/* for tos and host */
#define	ICMP_ECHO		8		/* echo service */
#define	ICMP_ROUTERADVERT	9		/* router advertisement */
#define	ICMP_ROUTERSOLICIT	10		/* router solicitation */
#define	ICMP_TIMXCEED		11		/* time exceeded, code: */
#define		ICMP_TIMXCEED_INTRANS	0		/* ttl==0 in transit */
#define		ICMP_TIMXCEED_REASS	1		/* ttl==0 in reass */
#define	ICMP_PARAMPROB		12		/* ip header bad */
#define		ICMP_PARAMPROB_OPTABSENT 1		/* req. opt. absent */
#define	ICMP_TSTAMP		13		/* timestamp request */
#define	ICMP_TSTAMPREPLY	14		/* timestamp reply */
#define	ICMP_IREQ		15		/* information request */
#define	ICMP_IREQREPLY		16		/* information reply */
#define	ICMP_MASKREQ		17		/* address mask request */
#define	ICMP_MASKREPLY		18		/* address mask reply */

#define	ICMP_MAXTYPE		18

#define ICMP_ERRTYPE(type) \
	((type) == ICMP_UNREACH || (type) == ICMP_SOURCEQUENCH || \
	(type) == ICMP_REDIRECT || (type) == ICMP_TIMXCEED || \
	(type) == ICMP_PARAMPROB)
#define	ICMP_MPLS_EXT_TYPE(type) \
	((type) == ICMP_UNREACH || \
         (type) == ICMP_TIMXCEED || \
         (type) == ICMP_PARAMPROB)
/* rfc1700 */
#ifndef ICMP_UNREACH_NET_UNKNOWN
#define ICMP_UNREACH_NET_UNKNOWN	6	/* destination net unknown */
#endif
#ifndef ICMP_UNREACH_HOST_UNKNOWN
#define ICMP_UNREACH_HOST_UNKNOWN	7	/* destination host unknown */
#endif
#ifndef ICMP_UNREACH_ISOLATED
#define ICMP_UNREACH_ISOLATED		8	/* source host isolated */
#endif
#ifndef ICMP_UNREACH_NET_PROHIB
#define ICMP_UNREACH_NET_PROHIB		9	/* admin prohibited net */
#endif
#ifndef ICMP_UNREACH_HOST_PROHIB
#define ICMP_UNREACH_HOST_PROHIB	10	/* admin prohibited host */
#endif
#ifndef ICMP_UNREACH_TOSNET
#define ICMP_UNREACH_TOSNET		11	/* tos prohibited net */
#endif
#ifndef ICMP_UNREACH_TOSHOST
#define ICMP_UNREACH_TOSHOST		12	/* tos prohibited host */
#endif

/* rfc1716 */
#ifndef ICMP_UNREACH_FILTER_PROHIB
#define ICMP_UNREACH_FILTER_PROHIB	13	/* admin prohibited filter */
#endif
#ifndef ICMP_UNREACH_HOST_PRECEDENCE
#define ICMP_UNREACH_HOST_PRECEDENCE	14	/* host precedence violation */
#endif
#ifndef ICMP_UNREACH_PRECEDENCE_CUTOFF
#define ICMP_UNREACH_PRECEDENCE_CUTOFF	15	/* precedence cutoff */
#endif

/* Most of the icmp types */
static const struct tok icmp2str[] = {
	{ ICMP_ECHOREPLY,		"echo reply" },
	{ ICMP_SOURCEQUENCH,		"source quench" },
	{ ICMP_ECHO,			"echo request" },
	{ ICMP_ROUTERSOLICIT,		"router solicitation" },
	{ ICMP_TSTAMP,			"time stamp request" },
	{ ICMP_TSTAMPREPLY,		"time stamp reply" },
	{ ICMP_IREQ,			"information request" },
	{ ICMP_IREQREPLY,		"information reply" },
	{ ICMP_MASKREQ,			"address mask request" },
	{ 0,				NULL }
};

/* Formats for most of the ICMP_UNREACH codes */
static const struct tok unreach2str[] = {
	{ ICMP_UNREACH_NET,		"net %s unreachable" },
	{ ICMP_UNREACH_HOST,		"host %s unreachable" },
	{ ICMP_UNREACH_SRCFAIL,
	    "%s unreachable - source route failed" },
	{ ICMP_UNREACH_NET_UNKNOWN,	"net %s unreachable - unknown" },
	{ ICMP_UNREACH_HOST_UNKNOWN,	"host %s unreachable - unknown" },
	{ ICMP_UNREACH_ISOLATED,
	    "%s unreachable - source host isolated" },
	{ ICMP_UNREACH_NET_PROHIB,
	    "net %s unreachable - admin prohibited" },
	{ ICMP_UNREACH_HOST_PROHIB,
	    "host %s unreachable - admin prohibited" },
	{ ICMP_UNREACH_TOSNET,
	    "net %s unreachable - tos prohibited" },
	{ ICMP_UNREACH_TOSHOST,
	    "host %s unreachable - tos prohibited" },
	{ ICMP_UNREACH_FILTER_PROHIB,
	    "host %s unreachable - admin prohibited filter" },
	{ ICMP_UNREACH_HOST_PRECEDENCE,
	    "host %s unreachable - host precedence violation" },
	{ ICMP_UNREACH_PRECEDENCE_CUTOFF,
	    "host %s unreachable - precedence cutoff" },
	{ 0,				NULL }
};

/* Formats for the ICMP_REDIRECT codes */
static const struct tok type2str[] = {
	{ ICMP_REDIRECT_NET,		"redirect %s to net %s" },
	{ ICMP_REDIRECT_HOST,		"redirect %s to host %s" },
	{ ICMP_REDIRECT_TOSNET,		"redirect-tos %s to net %s" },
	{ ICMP_REDIRECT_TOSHOST,	"redirect-tos %s to host %s" },
	{ 0,				NULL }
};

/* rfc1191 */
struct mtu_discovery {
	uint16_t unused;
	uint16_t nexthopmtu;
};

/* rfc1256 */
struct ih_rdiscovery {
	uint8_t ird_addrnum;
	uint8_t ird_addrsiz;
	uint16_t ird_lifetime;
};

struct id_rdiscovery {
	uint32_t ird_addr;
	uint32_t ird_pref;
};

/*
 * draft-bonica-internet-icmp-08
 *
 * The Destination Unreachable, Time Exceeded
 * and Parameter Problem messages are slighly changed as per
 * the above draft. A new Length field gets added to give
 * the caller an idea about the length of the piggypacked
 * IP packet before the MPLS extension header starts.
 *
 * The Length field represents length of the padded "original datagram"
 * field  measured in 32-bit words.
 *
 * 0                   1                   2                   3
 * 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |     Type      |     Code      |          Checksum             |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |     unused    |    Length     |          unused               |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |      Internet Header + leading octets of original datagram    |
 * |                                                               |
 * |                           //                                  |
 * |                                                               |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 */

struct icmp_ext_t {
    uint8_t icmp_type;
    uint8_t icmp_code;
    uint8_t icmp_checksum[2];
    uint8_t icmp_reserved;
    uint8_t icmp_length;
    uint8_t icmp_reserved2[2];
    uint8_t icmp_ext_legacy_header[128]; /* extension header starts 128 bytes after ICMP header */
    uint8_t icmp_ext_version_res[2];
    uint8_t icmp_ext_checksum[2];
    uint8_t icmp_ext_data[1];
};

struct icmp_mpls_ext_object_header_t {
    uint8_t length[2];
    uint8_t class_num;
    uint8_t ctype;
};

static const struct tok icmp_mpls_ext_obj_values[] = {
    { 1, "MPLS Stack Entry" },
    { 2, "Extended Payload" },
    { 0, NULL}
};

/* prototypes */
const char *icmp_tstamp_print(u_int);

/* print the milliseconds since midnight UTC */
const char *
icmp_tstamp_print(u_int tstamp)
{
    u_int msec,sec,min,hrs;

    static char buf[64];

    msec = tstamp % 1000;
    sec = tstamp / 1000;
    min = sec / 60; sec -= min * 60;
    hrs = min / 60; min -= hrs * 60;
    snprintf(buf, sizeof(buf), "%02u:%02u:%02u.%03u",hrs,min,sec,msec);
    return buf;
}

void
icmp_print(netdissect_options *ndo, const u_char *bp, u_int plen, const u_char *bp2,
           int fragmented)
{
	char *cp;
	const struct icmp *dp;
        const struct icmp_ext_t *ext_dp;
	const struct ip *ip;
	const char *str, *fmt;
	const struct ip *oip;
	const struct udphdr *ouh;
        const uint8_t *obj_tptr;
        uint32_t raw_label;
        const u_char *snapend_save;
	const struct icmp_mpls_ext_object_header_t *icmp_mpls_ext_object_header;
	u_int hlen, dport, mtu, obj_tlen, obj_class_num, obj_ctype;
	char buf[MAXHOSTNAMELEN + 100];
	struct cksum_vec vec[1];

	dp = (const struct icmp *)bp;
        ext_dp = (const struct icmp_ext_t *)bp;
	ip = (const struct ip *)bp2;
	str = buf;

	ND_TCHECK(dp->icmp_code);
	switch (dp->icmp_type) {

	case ICMP_ECHO:
	case ICMP_ECHOREPLY:
		ND_TCHECK(dp->icmp_seq);
		(void)snprintf(buf, sizeof(buf), "echo %s, id %u, seq %u",
                               dp->icmp_type == ICMP_ECHO ?
                               "request" : "reply",
                               EXTRACT_16BITS(&dp->icmp_id),
                               EXTRACT_16BITS(&dp->icmp_seq));
		break;

	case ICMP_UNREACH:
		ND_TCHECK(dp->icmp_ip.ip_dst);
		switch (dp->icmp_code) {

		case ICMP_UNREACH_PROTOCOL:
			ND_TCHECK(dp->icmp_ip.ip_p);
			(void)snprintf(buf, sizeof(buf),
			    "%s protocol %d unreachable",
			    ipaddr_string(ndo, &dp->icmp_ip.ip_dst),
			    dp->icmp_ip.ip_p);
			break;

		case ICMP_UNREACH_PORT:
			ND_TCHECK(dp->icmp_ip.ip_p);
			oip = &dp->icmp_ip;
			hlen = IP_HL(oip) * 4;
			ouh = (const struct udphdr *)(((const u_char *)oip) + hlen);
			ND_TCHECK(ouh->uh_dport);
			dport = EXTRACT_16BITS(&ouh->uh_dport);
			switch (oip->ip_p) {

			case IPPROTO_TCP:
				(void)snprintf(buf, sizeof(buf),
					"%s tcp port %s unreachable",
					ipaddr_string(ndo, &oip->ip_dst),
					tcpport_string(ndo, dport));
				break;

			case IPPROTO_UDP:
				(void)snprintf(buf, sizeof(buf),
					"%s udp port %s unreachable",
					ipaddr_string(ndo, &oip->ip_dst),
					udpport_string(ndo, dport));
				break;

			default:
				(void)snprintf(buf, sizeof(buf),
					"%s protocol %d port %d unreachable",
					ipaddr_string(ndo, &oip->ip_dst),
					oip->ip_p, dport);
				break;
			}
			break;

		case ICMP_UNREACH_NEEDFRAG:
		    {
			register const struct mtu_discovery *mp;
			mp = (const struct mtu_discovery *)(const u_char *)&dp->icmp_void;
			mtu = EXTRACT_16BITS(&mp->nexthopmtu);
			if (mtu) {
				(void)snprintf(buf, sizeof(buf),
				    "%s unreachable - need to frag (mtu %d)",
				    ipaddr_string(ndo, &dp->icmp_ip.ip_dst), mtu);
			} else {
				(void)snprintf(buf, sizeof(buf),
				    "%s unreachable - need to frag",
				    ipaddr_string(ndo, &dp->icmp_ip.ip_dst));
			}
		    }
			break;

		default:
			fmt = tok2str(unreach2str, "#%d %%s unreachable",
			    dp->icmp_code);
			(void)snprintf(buf, sizeof(buf), fmt,
			    ipaddr_string(ndo, &dp->icmp_ip.ip_dst));
			break;
		}
		break;

	case ICMP_REDIRECT:
		ND_TCHECK(dp->icmp_ip.ip_dst);
		fmt = tok2str(type2str, "redirect-#%d %%s to net %%s",
		    dp->icmp_code);
		(void)snprintf(buf, sizeof(buf), fmt,
		    ipaddr_string(ndo, &dp->icmp_ip.ip_dst),
		    ipaddr_string(ndo, &dp->icmp_gwaddr));
		break;

	case ICMP_ROUTERADVERT:
	    {
		register const struct ih_rdiscovery *ihp;
		register const struct id_rdiscovery *idp;
		u_int lifetime, num, size;

		(void)snprintf(buf, sizeof(buf), "router advertisement");
		cp = buf + strlen(buf);

		ihp = (const struct ih_rdiscovery *)&dp->icmp_void;
		ND_TCHECK(*ihp);
		(void)strncpy(cp, " lifetime ", sizeof(buf) - (cp - buf));
		cp = buf + strlen(buf);
		lifetime = EXTRACT_16BITS(&ihp->ird_lifetime);
		if (lifetime < 60) {
			(void)snprintf(cp, sizeof(buf) - (cp - buf), "%u",
			    lifetime);
		} else if (lifetime < 60 * 60) {
			(void)snprintf(cp, sizeof(buf) - (cp - buf), "%u:%02u",
			    lifetime / 60, lifetime % 60);
		} else {
			(void)snprintf(cp, sizeof(buf) - (cp - buf),
			    "%u:%02u:%02u",
			    lifetime / 3600,
			    (lifetime % 3600) / 60,
			    lifetime % 60);
		}
		cp = buf + strlen(buf);

		num = ihp->ird_addrnum;
		(void)snprintf(cp, sizeof(buf) - (cp - buf), " %d:", num);
		cp = buf + strlen(buf);

		size = ihp->ird_addrsiz;
		if (size != 2) {
			(void)snprintf(cp, sizeof(buf) - (cp - buf),
			    " [size %d]", size);
			break;
		}
		idp = (const struct id_rdiscovery *)&dp->icmp_data;
		while (num-- > 0) {
			ND_TCHECK(*idp);
			(void)snprintf(cp, sizeof(buf) - (cp - buf), " {%s %u}",
			    ipaddr_string(ndo, &idp->ird_addr),
			    EXTRACT_32BITS(&idp->ird_pref));
			cp = buf + strlen(buf);
			++idp;
		}
	    }
		break;

	case ICMP_TIMXCEED:
		ND_TCHECK(dp->icmp_ip.ip_dst);
		switch (dp->icmp_code) {

		case ICMP_TIMXCEED_INTRANS:
			str = "time exceeded in-transit";
			break;

		case ICMP_TIMXCEED_REASS:
			str = "ip reassembly time exceeded";
			break;

		default:
			(void)snprintf(buf, sizeof(buf), "time exceeded-#%d",
			    dp->icmp_code);
			break;
		}
		break;

	case ICMP_PARAMPROB:
		if (dp->icmp_code)
			(void)snprintf(buf, sizeof(buf),
			    "parameter problem - code %d", dp->icmp_code);
		else {
			ND_TCHECK(dp->icmp_pptr);
			(void)snprintf(buf, sizeof(buf),
			    "parameter problem - octet %d", dp->icmp_pptr);
		}
		break;

	case ICMP_MASKREPLY:
		ND_TCHECK(dp->icmp_mask);
		(void)snprintf(buf, sizeof(buf), "address mask is 0x%08x",
		    EXTRACT_32BITS(&dp->icmp_mask));
		break;

	case ICMP_TSTAMP:
		ND_TCHECK(dp->icmp_seq);
		(void)snprintf(buf, sizeof(buf),
		    "time stamp query id %u seq %u",
		    EXTRACT_16BITS(&dp->icmp_id),
		    EXTRACT_16BITS(&dp->icmp_seq));
		break;

	case ICMP_TSTAMPREPLY:
		ND_TCHECK(dp->icmp_ttime);
		(void)snprintf(buf, sizeof(buf),
		    "time stamp reply id %u seq %u: org %s",
                               EXTRACT_16BITS(&dp->icmp_id),
                               EXTRACT_16BITS(&dp->icmp_seq),
                               icmp_tstamp_print(EXTRACT_32BITS(&dp->icmp_otime)));

                (void)snprintf(buf+strlen(buf),sizeof(buf)-strlen(buf),", recv %s",
                         icmp_tstamp_print(EXTRACT_32BITS(&dp->icmp_rtime)));
                (void)snprintf(buf+strlen(buf),sizeof(buf)-strlen(buf),", xmit %s",
                         icmp_tstamp_print(EXTRACT_32BITS(&dp->icmp_ttime)));
                break;

	default:
		str = tok2str(icmp2str, "type-#%d", dp->icmp_type);
		break;
	}
	ND_PRINT((ndo, "ICMP %s, length %u", str, plen));
	if (ndo->ndo_vflag && !fragmented) { /* don't attempt checksumming if this is a frag */
		uint16_t sum, icmp_sum;

		if (ND_TTEST2(*bp, plen)) {
			vec[0].ptr = (const uint8_t *)(const void *)dp;
			vec[0].len = plen;
			sum = in_cksum(vec, 1);
			if (sum != 0) {
				icmp_sum = EXTRACT_16BITS(&dp->icmp_cksum);
				ND_PRINT((ndo, " (wrong icmp cksum %x (->%x)!)",
					     icmp_sum,
					     in_cksum_shouldbe(icmp_sum, sum)));
			}
		}
	}

        /*
         * print the remnants of the IP packet.
         * save the snaplength as this may get overidden in the IP printer.
         */
	if (ndo->ndo_vflag >= 1 && ICMP_ERRTYPE(dp->icmp_type)) {
		bp += 8;
		ND_PRINT((ndo, "\n\t"));
		ip = (const struct ip *)bp;
		ndo->ndo_snaplen = ndo->ndo_snapend - bp;
                snapend_save = ndo->ndo_snapend;
		ND_TCHECK_16BITS(&ip->ip_len);
		ip_print(ndo, bp, EXTRACT_16BITS(&ip->ip_len));
                ndo->ndo_snapend = snapend_save;
	}

        /*
         * Attempt to decode the MPLS extensions only for some ICMP types.
         */
        if (ndo->ndo_vflag >= 1 && plen > ICMP_EXTD_MINLEN && ICMP_MPLS_EXT_TYPE(dp->icmp_type)) {

            ND_TCHECK(*ext_dp);

            /*
             * Check first if the mpls extension header shows a non-zero length.
             * If the length field is not set then silently verify the checksum
             * to check if an extension header is present. This is expedient,
             * however not all implementations set the length field proper.
             */
            if (!ext_dp->icmp_length &&
                ND_TTEST2(ext_dp->icmp_ext_version_res, plen - ICMP_EXTD_MINLEN)) {
                vec[0].ptr = (const uint8_t *)(const void *)&ext_dp->icmp_ext_version_res;
                vec[0].len = plen - ICMP_EXTD_MINLEN;
                if (in_cksum(vec, 1)) {
                    return;
                }
            }

            ND_PRINT((ndo, "\n\tMPLS extension v%u",
                   ICMP_MPLS_EXT_EXTRACT_VERSION(*(ext_dp->icmp_ext_version_res))));

            /*
             * Sanity checking of the header.
             */
            if (ICMP_MPLS_EXT_EXTRACT_VERSION(*(ext_dp->icmp_ext_version_res)) !=
                ICMP_MPLS_EXT_VERSION) {
                ND_PRINT((ndo, " packet not supported"));
                return;
            }

            hlen = plen - ICMP_EXTD_MINLEN;
            if (ND_TTEST2(ext_dp->icmp_ext_version_res, hlen)) {
                vec[0].ptr = (const uint8_t *)(const void *)&ext_dp->icmp_ext_version_res;
                vec[0].len = hlen;
                ND_PRINT((ndo, ", checksum 0x%04x (%scorrect), length %u",
                       EXTRACT_16BITS(ext_dp->icmp_ext_checksum),
                       in_cksum(vec, 1) ? "in" : "",
                       hlen));
            }

            hlen -= 4; /* subtract common header size */
            obj_tptr = (const uint8_t *)ext_dp->icmp_ext_data;

            while (hlen > sizeof(struct icmp_mpls_ext_object_header_t)) {

                icmp_mpls_ext_object_header = (const struct icmp_mpls_ext_object_header_t *)obj_tptr;
                ND_TCHECK(*icmp_mpls_ext_object_header);
                obj_tlen = EXTRACT_16BITS(icmp_mpls_ext_object_header->length);
                obj_class_num = icmp_mpls_ext_object_header->class_num;
                obj_ctype = icmp_mpls_ext_object_header->ctype;
                obj_tptr += sizeof(struct icmp_mpls_ext_object_header_t);

                ND_PRINT((ndo, "\n\t  %s Object (%u), Class-Type: %u, length %u",
                       tok2str(icmp_mpls_ext_obj_values,"unknown",obj_class_num),
                       obj_class_num,
                       obj_ctype,
                       obj_tlen));

                hlen-=sizeof(struct icmp_mpls_ext_object_header_t); /* length field includes tlv header */

                /* infinite loop protection */
                if ((obj_class_num == 0) ||
                    (obj_tlen < sizeof(struct icmp_mpls_ext_object_header_t))) {
                    return;
                }
                obj_tlen-=sizeof(struct icmp_mpls_ext_object_header_t);

                switch (obj_class_num) {
                case 1:
                    switch(obj_ctype) {
                    case 1:
                        ND_TCHECK2(*obj_tptr, 4);
                        raw_label = EXTRACT_32BITS(obj_tptr);
                        ND_PRINT((ndo, "\n\t    label %u, exp %u", MPLS_LABEL(raw_label), MPLS_EXP(raw_label)));
                        if (MPLS_STACK(raw_label))
                            ND_PRINT((ndo, ", [S]"));
                        ND_PRINT((ndo, ", ttl %u", MPLS_TTL(raw_label)));
                        break;
                    default:
                        print_unknown_data(ndo, obj_tptr, "\n\t    ", obj_tlen);
                    }
                    break;

               /*
                *  FIXME those are the defined objects that lack a decoder
                *  you are welcome to contribute code ;-)
                */
                case 2:
                default:
                    print_unknown_data(ndo, obj_tptr, "\n\t    ", obj_tlen);
                    break;
                }
                if (hlen < obj_tlen)
                    break;
                hlen -= obj_tlen;
                obj_tptr += obj_tlen;
            }
        }

	return;
trunc:
	ND_PRINT((ndo, "[|icmp]"));
}
/*
 * Local Variables:
 * c-style: whitesmith
 * c-basic-offset: 8
 * End:
 */
