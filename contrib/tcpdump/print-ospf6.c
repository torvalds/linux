/*
 * Copyright (c) 1992, 1993, 1994, 1995, 1996, 1997
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
 *
 * OSPF support contributed by Jeffrey Honig (jch@mitchell.cit.cornell.edu)
 */

/* \summary: IPv6 Open Shortest Path First (OSPFv3) printer */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <netdissect-stdinc.h>

#include <string.h>

#include "netdissect.h"
#include "addrtoname.h"
#include "extract.h"

#include "ospf.h"

#define	OSPF_TYPE_HELLO         1	/* Hello */
#define	OSPF_TYPE_DD            2	/* Database Description */
#define	OSPF_TYPE_LS_REQ        3	/* Link State Request */
#define	OSPF_TYPE_LS_UPDATE     4	/* Link State Update */
#define	OSPF_TYPE_LS_ACK        5	/* Link State Ack */

/* Options *_options	*/
#define OSPF6_OPTION_V6	0x01	/* V6 bit: A bit for peeping tom */
#define OSPF6_OPTION_E	0x02	/* E bit: External routes advertised	*/
#define OSPF6_OPTION_MC	0x04	/* MC bit: Multicast capable */
#define OSPF6_OPTION_N	0x08	/* N bit: For type-7 LSA */
#define OSPF6_OPTION_R	0x10	/* R bit: Router bit */
#define OSPF6_OPTION_DC	0x20	/* DC bit: Demand circuits */
/* The field is actually 24-bit (RFC5340 Section A.2). */
#define OSPF6_OPTION_AF	0x0100	/* AF bit: Multiple address families */
#define OSPF6_OPTION_L	0x0200	/* L bit: Link-local signaling (LLS) */
#define OSPF6_OPTION_AT	0x0400	/* AT bit: Authentication trailer */


/* db_flags	*/
#define	OSPF6_DB_INIT		0x04	    /*	*/
#define	OSPF6_DB_MORE		0x02
#define	OSPF6_DB_MASTER		0x01
#define	OSPF6_DB_M6		0x10  /* IPv6 MTU */

/* ls_type	*/
#define	LS_TYPE_ROUTER		1   /* router link */
#define	LS_TYPE_NETWORK		2   /* network link */
#define	LS_TYPE_INTER_AP	3   /* Inter-Area-Prefix */
#define	LS_TYPE_INTER_AR	4   /* Inter-Area-Router */
#define	LS_TYPE_ASE		5   /* ASE */
#define	LS_TYPE_GROUP		6   /* Group membership */
#define	LS_TYPE_NSSA		7   /* NSSA */
#define	LS_TYPE_LINK		8   /* Link LSA */
#define	LS_TYPE_INTRA_AP	9   /* Intra-Area-Prefix */
#define LS_TYPE_INTRA_ATE       10  /* Intra-Area-TE */
#define LS_TYPE_GRACE           11  /* Grace LSA */
#define LS_TYPE_RI		12  /* Router information */
#define LS_TYPE_INTER_ASTE	13  /* Inter-AS-TE */
#define LS_TYPE_L1VPN		14  /* L1VPN */
#define LS_TYPE_MASK		0x1fff

#define LS_SCOPE_LINKLOCAL	0x0000
#define LS_SCOPE_AREA		0x2000
#define LS_SCOPE_AS		0x4000
#define LS_SCOPE_MASK		0x6000
#define LS_SCOPE_U              0x8000

/* rla_link.link_type	*/
#define	RLA_TYPE_ROUTER		1   /* point-to-point to another router	*/
#define	RLA_TYPE_TRANSIT	2   /* connection to transit network	*/
#define RLA_TYPE_VIRTUAL	4   /* virtual link			*/

/* rla_flags	*/
#define	RLA_FLAG_B	0x01
#define	RLA_FLAG_E	0x02
#define	RLA_FLAG_V	0x04
#define	RLA_FLAG_W	0x08
#define RLA_FLAG_N      0x10

/* lsa_prefix options */
#define LSA_PREFIX_OPT_NU 0x01
#define LSA_PREFIX_OPT_LA 0x02
#define LSA_PREFIX_OPT_MC 0x04
#define LSA_PREFIX_OPT_P  0x08
#define LSA_PREFIX_OPT_DN 0x10

/* sla_tosmetric breakdown	*/
#define	SLA_MASK_TOS		0x7f000000
#define	SLA_MASK_METRIC		0x00ffffff
#define SLA_SHIFT_TOS		24

/* asla_metric */
#define ASLA_FLAG_FWDADDR	0x02000000
#define ASLA_FLAG_ROUTETAG	0x01000000
#define	ASLA_MASK_METRIC	0x00ffffff

/* RFC6506 Section 4.1 */
#define OSPF6_AT_HDRLEN             16U
#define OSPF6_AUTH_TYPE_HMAC        0x0001

typedef uint32_t rtrid_t;

/* link state advertisement header */
struct lsa6_hdr {
    uint16_t ls_age;
    uint16_t ls_type;
    rtrid_t ls_stateid;
    rtrid_t ls_router;
    uint32_t ls_seq;
    uint16_t ls_chksum;
    uint16_t ls_length;
};

/* Length of an IPv6 address, in bytes. */
#define IPV6_ADDR_LEN_BYTES (128/8)

struct lsa6_prefix {
    uint8_t lsa_p_len;
    uint8_t lsa_p_opt;
    uint16_t lsa_p_metric;
    uint8_t lsa_p_prefix[IPV6_ADDR_LEN_BYTES]; /* maximum length */
};

/* link state advertisement */
struct lsa6 {
    struct lsa6_hdr ls_hdr;

    /* Link state types */
    union {
	/* Router links advertisements */
	struct {
	    union {
		uint8_t flg;
		uint32_t opt;
	    } rla_flgandopt;
#define rla_flags	rla_flgandopt.flg
#define rla_options	rla_flgandopt.opt
	    struct rlalink6 {
		uint8_t link_type;
		uint8_t link_zero[1];
		uint16_t link_metric;
		uint32_t link_ifid;
		uint32_t link_nifid;
		rtrid_t link_nrtid;
	    } rla_link[1];		/* may repeat	*/
	} un_rla;

	/* Network links advertisements */
	struct {
	    uint32_t nla_options;
	    rtrid_t nla_router[1];	/* may repeat	*/
	} un_nla;

	/* Inter Area Prefix LSA */
	struct {
	    uint32_t inter_ap_metric;
	    struct lsa6_prefix inter_ap_prefix[1];
	} un_inter_ap;

	/* AS external links advertisements */
	struct {
	    uint32_t asla_metric;
	    struct lsa6_prefix asla_prefix[1];
	    /* some optional fields follow */
	} un_asla;

#if 0
	/* Summary links advertisements */
	struct {
	    struct in_addr sla_mask;
	    uint32_t sla_tosmetric[1];	/* may repeat	*/
	} un_sla;

	/* Multicast group membership */
	struct mcla {
	    uint32_t mcla_vtype;
	    struct in_addr mcla_vid;
	} un_mcla[1];
#endif

	/* Type 7 LSA */

	/* Link LSA */
	struct llsa {
	    union {
		uint8_t pri;
		uint32_t opt;
	    } llsa_priandopt;
#define llsa_priority	llsa_priandopt.pri
#define llsa_options	llsa_priandopt.opt
	    struct in6_addr llsa_lladdr;
	    uint32_t llsa_nprefix;
	    struct lsa6_prefix llsa_prefix[1];
	} un_llsa;

	/* Intra-Area-Prefix */
	struct {
	    uint16_t intra_ap_nprefix;
	    uint16_t intra_ap_lstype;
	    rtrid_t intra_ap_lsid;
	    rtrid_t intra_ap_rtid;
	    struct lsa6_prefix intra_ap_prefix[1];
	} un_intra_ap;
    } lsa_un;
};

/*
 * the main header
 */
struct ospf6hdr {
    uint8_t ospf6_version;
    uint8_t ospf6_type;
    uint16_t ospf6_len;
    rtrid_t ospf6_routerid;
    rtrid_t ospf6_areaid;
    uint16_t ospf6_chksum;
    uint8_t ospf6_instanceid;
    uint8_t ospf6_rsvd;
};

/*
 * The OSPF6 header length is 16 bytes, regardless of how your compiler
 * might choose to pad the above structure.
 */
#define OSPF6HDR_LEN    16

/* Hello packet */
struct hello6 {
    uint32_t hello_ifid;
    union {
	uint8_t pri;
	uint32_t opt;
    } hello_priandopt;
#define hello_priority	hello_priandopt.pri
#define hello_options	hello_priandopt.opt
    uint16_t hello_helloint;
    uint16_t hello_deadint;
    rtrid_t hello_dr;
    rtrid_t hello_bdr;
    rtrid_t hello_neighbor[1]; /* may repeat	*/
};

/* Database Description packet */
struct dd6 {
    uint32_t db_options;
    uint16_t db_mtu;
    uint8_t db_mbz;
    uint8_t db_flags;
    uint32_t db_seq;
    struct lsa6_hdr db_lshdr[1]; /* may repeat	*/
};

/* Link State Request */
struct lsr6 {
    uint16_t ls_mbz;
    uint16_t ls_type;
    rtrid_t ls_stateid;
    rtrid_t ls_router;
};

/* Link State Update */
struct lsu6 {
    uint32_t lsu_count;
    struct lsa6 lsu_lsa[1]; /* may repeat	*/
};

static const char tstr[] = " [|ospf3]";

static const struct tok ospf6_option_values[] = {
	{ OSPF6_OPTION_V6,	"V6" },
	{ OSPF6_OPTION_E,	"External" },
	{ OSPF6_OPTION_MC,	"Deprecated" },
	{ OSPF6_OPTION_N,	"NSSA" },
	{ OSPF6_OPTION_R,	"Router" },
	{ OSPF6_OPTION_DC,	"Demand Circuit" },
	{ OSPF6_OPTION_AF,	"AFs Support" },
	{ OSPF6_OPTION_L,	"LLS" },
	{ OSPF6_OPTION_AT,	"Authentication Trailer" },
	{ 0,			NULL }
};

static const struct tok ospf6_rla_flag_values[] = {
	{ RLA_FLAG_B,		"ABR" },
	{ RLA_FLAG_E,		"External" },
	{ RLA_FLAG_V,		"Virtual-Link Endpoint" },
	{ RLA_FLAG_W,		"Wildcard Receiver" },
        { RLA_FLAG_N,           "NSSA Translator" },
	{ 0,			NULL }
};

static const struct tok ospf6_asla_flag_values[] = {
	{ ASLA_FLAG_EXTERNAL,	"External Type 2" },
	{ ASLA_FLAG_FWDADDR,	"Forwarding" },
	{ ASLA_FLAG_ROUTETAG,	"Tag" },
	{ 0,			NULL }
};

static const struct tok ospf6_type_values[] = {
	{ OSPF_TYPE_HELLO,	"Hello" },
	{ OSPF_TYPE_DD,		"Database Description" },
	{ OSPF_TYPE_LS_REQ,	"LS-Request" },
	{ OSPF_TYPE_LS_UPDATE,	"LS-Update" },
	{ OSPF_TYPE_LS_ACK,	"LS-Ack" },
	{ 0,			NULL }
};

static const struct tok ospf6_lsa_values[] = {
	{ LS_TYPE_ROUTER,       "Router" },
	{ LS_TYPE_NETWORK,      "Network" },
	{ LS_TYPE_INTER_AP,     "Inter-Area Prefix" },
	{ LS_TYPE_INTER_AR,     "Inter-Area Router" },
	{ LS_TYPE_ASE,          "External" },
	{ LS_TYPE_GROUP,        "Deprecated" },
	{ LS_TYPE_NSSA,         "NSSA" },
	{ LS_TYPE_LINK,         "Link" },
	{ LS_TYPE_INTRA_AP,     "Intra-Area Prefix" },
        { LS_TYPE_INTRA_ATE,    "Intra-Area TE" },
        { LS_TYPE_GRACE,        "Grace" },
	{ LS_TYPE_RI,           "Router Information" },
	{ LS_TYPE_INTER_ASTE,   "Inter-AS-TE" },
	{ LS_TYPE_L1VPN,        "Layer 1 VPN" },
	{ 0,			NULL }
};

static const struct tok ospf6_ls_scope_values[] = {
	{ LS_SCOPE_LINKLOCAL,   "Link Local" },
	{ LS_SCOPE_AREA,        "Area Local" },
	{ LS_SCOPE_AS,          "Domain Wide" },
	{ 0,			NULL }
};

static const struct tok ospf6_dd_flag_values[] = {
	{ OSPF6_DB_INIT,	"Init" },
	{ OSPF6_DB_MORE,	"More" },
	{ OSPF6_DB_MASTER,	"Master" },
	{ OSPF6_DB_M6,		"IPv6 MTU" },
	{ 0,			NULL }
};

static const struct tok ospf6_lsa_prefix_option_values[] = {
        { LSA_PREFIX_OPT_NU, "No Unicast" },
        { LSA_PREFIX_OPT_LA, "Local address" },
        { LSA_PREFIX_OPT_MC, "Deprecated" },
        { LSA_PREFIX_OPT_P, "Propagate" },
        { LSA_PREFIX_OPT_DN, "Down" },
	{ 0, NULL }
};

static const struct tok ospf6_auth_type_str[] = {
	{ OSPF6_AUTH_TYPE_HMAC,        "HMAC" },
	{ 0, NULL }
};

static void
ospf6_print_ls_type(netdissect_options *ndo,
                    register u_int ls_type, register const rtrid_t *ls_stateid)
{
        ND_PRINT((ndo, "\n\t    %s LSA (%d), %s Scope%s, LSA-ID %s",
               tok2str(ospf6_lsa_values, "Unknown", ls_type & LS_TYPE_MASK),
               ls_type & LS_TYPE_MASK,
               tok2str(ospf6_ls_scope_values, "Unknown", ls_type & LS_SCOPE_MASK),
               ls_type &0x8000 ? ", transitive" : "", /* U-bit */
               ipaddr_string(ndo, ls_stateid)));
}

static int
ospf6_print_lshdr(netdissect_options *ndo,
                  register const struct lsa6_hdr *lshp, const u_char *dataend)
{
	if ((const u_char *)(lshp + 1) > dataend)
		goto trunc;
	ND_TCHECK(lshp->ls_type);
	ND_TCHECK(lshp->ls_seq);

	ND_PRINT((ndo, "\n\t  Advertising Router %s, seq 0x%08x, age %us, length %u",
               ipaddr_string(ndo, &lshp->ls_router),
               EXTRACT_32BITS(&lshp->ls_seq),
               EXTRACT_16BITS(&lshp->ls_age),
               EXTRACT_16BITS(&lshp->ls_length)-(u_int)sizeof(struct lsa6_hdr)));

	ospf6_print_ls_type(ndo, EXTRACT_16BITS(&lshp->ls_type), &lshp->ls_stateid);

	return (0);
trunc:
	return (1);
}

static int
ospf6_print_lsaprefix(netdissect_options *ndo,
                      const uint8_t *tptr, u_int lsa_length)
{
	const struct lsa6_prefix *lsapp = (const struct lsa6_prefix *)tptr;
	u_int wordlen;
	struct in6_addr prefix;

	if (lsa_length < sizeof (*lsapp) - IPV6_ADDR_LEN_BYTES)
		goto trunc;
	lsa_length -= sizeof (*lsapp) - IPV6_ADDR_LEN_BYTES;
	ND_TCHECK2(*lsapp, sizeof (*lsapp) - IPV6_ADDR_LEN_BYTES);
	wordlen = (lsapp->lsa_p_len + 31) / 32;
	if (wordlen * 4 > sizeof(struct in6_addr)) {
		ND_PRINT((ndo, " bogus prefixlen /%d", lsapp->lsa_p_len));
		goto trunc;
	}
	if (lsa_length < wordlen * 4)
		goto trunc;
	lsa_length -= wordlen * 4;
	ND_TCHECK2(lsapp->lsa_p_prefix, wordlen * 4);
	memset(&prefix, 0, sizeof(prefix));
	memcpy(&prefix, lsapp->lsa_p_prefix, wordlen * 4);
	ND_PRINT((ndo, "\n\t\t%s/%d", ip6addr_string(ndo, &prefix),
		lsapp->lsa_p_len));
        if (lsapp->lsa_p_opt) {
            ND_PRINT((ndo, ", Options [%s]",
                   bittok2str(ospf6_lsa_prefix_option_values,
                              "none", lsapp->lsa_p_opt)));
        }
        ND_PRINT((ndo, ", metric %u", EXTRACT_16BITS(&lsapp->lsa_p_metric)));
	return sizeof(*lsapp) - IPV6_ADDR_LEN_BYTES + wordlen * 4;

trunc:
	return -1;
}


/*
 * Print a single link state advertisement.  If truncated return 1, else 0.
 */
static int
ospf6_print_lsa(netdissect_options *ndo,
                register const struct lsa6 *lsap, const u_char *dataend)
{
	register const struct rlalink6 *rlp;
#if 0
	register const struct tos_metric *tosp;
#endif
	register const rtrid_t *ap;
#if 0
	register const struct aslametric *almp;
	register const struct mcla *mcp;
#endif
	register const struct llsa *llsap;
	register const struct lsa6_prefix *lsapp;
#if 0
	register const uint32_t *lp;
#endif
	register u_int prefixes;
	register int bytelen;
	register u_int length, lsa_length;
	uint32_t flags32;
	const uint8_t *tptr;

	if (ospf6_print_lshdr(ndo, &lsap->ls_hdr, dataend))
		return (1);
	ND_TCHECK(lsap->ls_hdr.ls_length);
        length = EXTRACT_16BITS(&lsap->ls_hdr.ls_length);

	/*
	 * The LSA length includes the length of the header;
	 * it must have a value that's at least that length.
	 * If it does, find the length of what follows the
	 * header.
	 */
        if (length < sizeof(struct lsa6_hdr) || (const u_char *)lsap + length > dataend)
        	return (1);
        lsa_length = length - sizeof(struct lsa6_hdr);
        tptr = (const uint8_t *)lsap+sizeof(struct lsa6_hdr);

	switch (EXTRACT_16BITS(&lsap->ls_hdr.ls_type)) {
	case LS_TYPE_ROUTER | LS_SCOPE_AREA:
		if (lsa_length < sizeof (lsap->lsa_un.un_rla.rla_options))
			return (1);
		lsa_length -= sizeof (lsap->lsa_un.un_rla.rla_options);
		ND_TCHECK(lsap->lsa_un.un_rla.rla_options);
		ND_PRINT((ndo, "\n\t      Options [%s]",
		          bittok2str(ospf6_option_values, "none",
		          EXTRACT_32BITS(&lsap->lsa_un.un_rla.rla_options))));
		ND_PRINT((ndo, ", RLA-Flags [%s]",
		          bittok2str(ospf6_rla_flag_values, "none",
		          lsap->lsa_un.un_rla.rla_flags)));

		rlp = lsap->lsa_un.un_rla.rla_link;
		while (lsa_length != 0) {
			if (lsa_length < sizeof (*rlp))
				return (1);
			lsa_length -= sizeof (*rlp);
			ND_TCHECK(*rlp);
			switch (rlp->link_type) {

			case RLA_TYPE_VIRTUAL:
				ND_PRINT((ndo, "\n\t      Virtual Link: Neighbor Router-ID %s"
                                       "\n\t      Neighbor Interface-ID %s, Interface %s",
                                       ipaddr_string(ndo, &rlp->link_nrtid),
                                       ipaddr_string(ndo, &rlp->link_nifid),
                                       ipaddr_string(ndo, &rlp->link_ifid)));
                                break;

			case RLA_TYPE_ROUTER:
				ND_PRINT((ndo, "\n\t      Neighbor Router-ID %s"
                                       "\n\t      Neighbor Interface-ID %s, Interface %s",
                                       ipaddr_string(ndo, &rlp->link_nrtid),
                                       ipaddr_string(ndo, &rlp->link_nifid),
                                       ipaddr_string(ndo, &rlp->link_ifid)));
				break;

			case RLA_TYPE_TRANSIT:
				ND_PRINT((ndo, "\n\t      Neighbor Network-ID %s"
                                       "\n\t      Neighbor Interface-ID %s, Interface %s",
				    ipaddr_string(ndo, &rlp->link_nrtid),
				    ipaddr_string(ndo, &rlp->link_nifid),
				    ipaddr_string(ndo, &rlp->link_ifid)));
				break;

			default:
				ND_PRINT((ndo, "\n\t      Unknown Router Links Type 0x%02x",
				    rlp->link_type));
				return (0);
			}
			ND_PRINT((ndo, ", metric %d", EXTRACT_16BITS(&rlp->link_metric)));
			rlp++;
		}
		break;

	case LS_TYPE_NETWORK | LS_SCOPE_AREA:
		if (lsa_length < sizeof (lsap->lsa_un.un_nla.nla_options))
			return (1);
		lsa_length -= sizeof (lsap->lsa_un.un_nla.nla_options);
		ND_TCHECK(lsap->lsa_un.un_nla.nla_options);
		ND_PRINT((ndo, "\n\t      Options [%s]",
		          bittok2str(ospf6_option_values, "none",
		          EXTRACT_32BITS(&lsap->lsa_un.un_nla.nla_options))));

		ND_PRINT((ndo, "\n\t      Connected Routers:"));
		ap = lsap->lsa_un.un_nla.nla_router;
		while (lsa_length != 0) {
			if (lsa_length < sizeof (*ap))
				return (1);
			lsa_length -= sizeof (*ap);
			ND_TCHECK(*ap);
			ND_PRINT((ndo, "\n\t\t%s", ipaddr_string(ndo, ap)));
			++ap;
		}
		break;

	case LS_TYPE_INTER_AP | LS_SCOPE_AREA:
		if (lsa_length < sizeof (lsap->lsa_un.un_inter_ap.inter_ap_metric))
			return (1);
		lsa_length -= sizeof (lsap->lsa_un.un_inter_ap.inter_ap_metric);
		ND_TCHECK(lsap->lsa_un.un_inter_ap.inter_ap_metric);
		ND_PRINT((ndo, ", metric %u",
			EXTRACT_32BITS(&lsap->lsa_un.un_inter_ap.inter_ap_metric) & SLA_MASK_METRIC));

		tptr = (const uint8_t *)lsap->lsa_un.un_inter_ap.inter_ap_prefix;
		while (lsa_length != 0) {
			bytelen = ospf6_print_lsaprefix(ndo, tptr, lsa_length);
			if (bytelen < 0)
				goto trunc;
			lsa_length -= bytelen;
			tptr += bytelen;
		}
		break;

	case LS_TYPE_ASE | LS_SCOPE_AS:
		if (lsa_length < sizeof (lsap->lsa_un.un_asla.asla_metric))
			return (1);
		lsa_length -= sizeof (lsap->lsa_un.un_asla.asla_metric);
		ND_TCHECK(lsap->lsa_un.un_asla.asla_metric);
		flags32 = EXTRACT_32BITS(&lsap->lsa_un.un_asla.asla_metric);
		ND_PRINT((ndo, "\n\t     Flags [%s]",
		          bittok2str(ospf6_asla_flag_values, "none", flags32)));
		ND_PRINT((ndo, " metric %u",
		       EXTRACT_32BITS(&lsap->lsa_un.un_asla.asla_metric) &
		       ASLA_MASK_METRIC));

		tptr = (const uint8_t *)lsap->lsa_un.un_asla.asla_prefix;
		lsapp = (const struct lsa6_prefix *)tptr;
		bytelen = ospf6_print_lsaprefix(ndo, tptr, lsa_length);
		if (bytelen < 0)
			goto trunc;
		lsa_length -= bytelen;
		tptr += bytelen;

		if ((flags32 & ASLA_FLAG_FWDADDR) != 0) {
			const struct in6_addr *fwdaddr6;

			fwdaddr6 = (const struct in6_addr *)tptr;
			if (lsa_length < sizeof (*fwdaddr6))
				return (1);
			lsa_length -= sizeof (*fwdaddr6);
			ND_TCHECK(*fwdaddr6);
			ND_PRINT((ndo, " forward %s",
			       ip6addr_string(ndo, fwdaddr6)));
			tptr += sizeof(*fwdaddr6);
		}

		if ((flags32 & ASLA_FLAG_ROUTETAG) != 0) {
			if (lsa_length < sizeof (uint32_t))
				return (1);
			lsa_length -= sizeof (uint32_t);
			ND_TCHECK(*(const uint32_t *)tptr);
			ND_PRINT((ndo, " tag %s",
			       ipaddr_string(ndo, (const uint32_t *)tptr)));
			tptr += sizeof(uint32_t);
		}

		if (lsapp->lsa_p_metric) {
			if (lsa_length < sizeof (uint32_t))
				return (1);
			lsa_length -= sizeof (uint32_t);
			ND_TCHECK(*(const uint32_t *)tptr);
			ND_PRINT((ndo, " RefLSID: %s",
			       ipaddr_string(ndo, (const uint32_t *)tptr)));
			tptr += sizeof(uint32_t);
		}
		break;

	case LS_TYPE_LINK:
		/* Link LSA */
		llsap = &lsap->lsa_un.un_llsa;
		if (lsa_length < sizeof (llsap->llsa_priandopt))
			return (1);
		lsa_length -= sizeof (llsap->llsa_priandopt);
		ND_TCHECK(llsap->llsa_priandopt);
		ND_PRINT((ndo, "\n\t      Options [%s]",
		          bittok2str(ospf6_option_values, "none",
		          EXTRACT_32BITS(&llsap->llsa_options))));

		if (lsa_length < sizeof (llsap->llsa_lladdr) + sizeof (llsap->llsa_nprefix))
			return (1);
		lsa_length -= sizeof (llsap->llsa_lladdr) + sizeof (llsap->llsa_nprefix);
                ND_TCHECK(llsap->llsa_nprefix);
                prefixes = EXTRACT_32BITS(&llsap->llsa_nprefix);
		ND_PRINT((ndo, "\n\t      Priority %d, Link-local address %s, Prefixes %d:",
                       llsap->llsa_priority,
                       ip6addr_string(ndo, &llsap->llsa_lladdr),
                       prefixes));

		tptr = (const uint8_t *)llsap->llsa_prefix;
		while (prefixes > 0) {
			bytelen = ospf6_print_lsaprefix(ndo, tptr, lsa_length);
			if (bytelen < 0)
				goto trunc;
			prefixes--;
			lsa_length -= bytelen;
			tptr += bytelen;
		}
		break;

	case LS_TYPE_INTRA_AP | LS_SCOPE_AREA:
		/* Intra-Area-Prefix LSA */
		if (lsa_length < sizeof (lsap->lsa_un.un_intra_ap.intra_ap_rtid))
			return (1);
		lsa_length -= sizeof (lsap->lsa_un.un_intra_ap.intra_ap_rtid);
		ND_TCHECK(lsap->lsa_un.un_intra_ap.intra_ap_rtid);
		ospf6_print_ls_type(ndo,
			EXTRACT_16BITS(&lsap->lsa_un.un_intra_ap.intra_ap_lstype),
			&lsap->lsa_un.un_intra_ap.intra_ap_lsid);

		if (lsa_length < sizeof (lsap->lsa_un.un_intra_ap.intra_ap_nprefix))
			return (1);
		lsa_length -= sizeof (lsap->lsa_un.un_intra_ap.intra_ap_nprefix);
		ND_TCHECK(lsap->lsa_un.un_intra_ap.intra_ap_nprefix);
                prefixes = EXTRACT_16BITS(&lsap->lsa_un.un_intra_ap.intra_ap_nprefix);
		ND_PRINT((ndo, "\n\t      Prefixes %d:", prefixes));

		tptr = (const uint8_t *)lsap->lsa_un.un_intra_ap.intra_ap_prefix;
		while (prefixes > 0) {
			bytelen = ospf6_print_lsaprefix(ndo, tptr, lsa_length);
			if (bytelen < 0)
				goto trunc;
			prefixes--;
			lsa_length -= bytelen;
			tptr += bytelen;
		}
		break;

        case LS_TYPE_GRACE | LS_SCOPE_LINKLOCAL:
                if (ospf_print_grace_lsa(ndo, tptr, lsa_length) == -1) {
                    return 1;
                }
                break;

        case LS_TYPE_INTRA_ATE | LS_SCOPE_LINKLOCAL:
                if (ospf_print_te_lsa(ndo, tptr, lsa_length) == -1) {
                    return 1;
                }
                break;

	default:
                if(!print_unknown_data(ndo,tptr,
                                       "\n\t      ",
                                       lsa_length)) {
                    return (1);
                }
                break;
	}

	return (0);
trunc:
	return (1);
}

static int
ospf6_decode_v3(netdissect_options *ndo,
                register const struct ospf6hdr *op,
                register const u_char *dataend)
{
	register const rtrid_t *ap;
	register const struct lsr6 *lsrp;
	register const struct lsa6_hdr *lshp;
	register const struct lsa6 *lsap;
	register int i;

	switch (op->ospf6_type) {

	case OSPF_TYPE_HELLO: {
		register const struct hello6 *hellop = (const struct hello6 *)((const uint8_t *)op + OSPF6HDR_LEN);

		ND_TCHECK_32BITS(&hellop->hello_options);
		ND_PRINT((ndo, "\n\tOptions [%s]",
		          bittok2str(ospf6_option_values, "none",
		          EXTRACT_32BITS(&hellop->hello_options))));

		ND_TCHECK(hellop->hello_deadint);
		ND_PRINT((ndo, "\n\t  Hello Timer %us, Dead Timer %us, Interface-ID %s, Priority %u",
		          EXTRACT_16BITS(&hellop->hello_helloint),
		          EXTRACT_16BITS(&hellop->hello_deadint),
		          ipaddr_string(ndo, &hellop->hello_ifid),
		          hellop->hello_priority));

		ND_TCHECK(hellop->hello_dr);
		if (EXTRACT_32BITS(&hellop->hello_dr) != 0)
			ND_PRINT((ndo, "\n\t  Designated Router %s",
			    ipaddr_string(ndo, &hellop->hello_dr)));
		ND_TCHECK(hellop->hello_bdr);
		if (EXTRACT_32BITS(&hellop->hello_bdr) != 0)
			ND_PRINT((ndo, ", Backup Designated Router %s",
			    ipaddr_string(ndo, &hellop->hello_bdr)));
		if (ndo->ndo_vflag > 1) {
			ND_PRINT((ndo, "\n\t  Neighbor List:"));
			ap = hellop->hello_neighbor;
			while ((const u_char *)ap < dataend) {
				ND_TCHECK(*ap);
				ND_PRINT((ndo, "\n\t    %s", ipaddr_string(ndo, ap)));
				++ap;
			}
		}
		break;	/* HELLO */
	}

	case OSPF_TYPE_DD: {
		register const struct dd6 *ddp = (const struct dd6 *)((const uint8_t *)op + OSPF6HDR_LEN);

		ND_TCHECK(ddp->db_options);
		ND_PRINT((ndo, "\n\tOptions [%s]",
		          bittok2str(ospf6_option_values, "none",
		          EXTRACT_32BITS(&ddp->db_options))));
		ND_TCHECK(ddp->db_flags);
		ND_PRINT((ndo, ", DD Flags [%s]",
		          bittok2str(ospf6_dd_flag_values,"none",ddp->db_flags)));

		ND_TCHECK(ddp->db_seq);
		ND_PRINT((ndo, ", MTU %u, DD-Sequence 0x%08x",
                       EXTRACT_16BITS(&ddp->db_mtu),
                       EXTRACT_32BITS(&ddp->db_seq)));
		if (ndo->ndo_vflag > 1) {
			/* Print all the LS adv's */
			lshp = ddp->db_lshdr;
			while ((const u_char *)lshp < dataend) {
				if (ospf6_print_lshdr(ndo, lshp++, dataend))
					goto trunc;
			}
		}
		break;
	}

	case OSPF_TYPE_LS_REQ:
		if (ndo->ndo_vflag > 1) {
			lsrp = (const struct lsr6 *)((const uint8_t *)op + OSPF6HDR_LEN);
			while ((const u_char *)lsrp < dataend) {
				ND_TCHECK(*lsrp);
				ND_PRINT((ndo, "\n\t  Advertising Router %s",
				          ipaddr_string(ndo, &lsrp->ls_router)));
				ospf6_print_ls_type(ndo, EXTRACT_16BITS(&lsrp->ls_type),
                                                    &lsrp->ls_stateid);
				++lsrp;
			}
		}
		break;

	case OSPF_TYPE_LS_UPDATE:
		if (ndo->ndo_vflag > 1) {
			register const struct lsu6 *lsup = (const struct lsu6 *)((const uint8_t *)op + OSPF6HDR_LEN);

			ND_TCHECK(lsup->lsu_count);
			i = EXTRACT_32BITS(&lsup->lsu_count);
			lsap = lsup->lsu_lsa;
			while ((const u_char *)lsap < dataend && i--) {
				if (ospf6_print_lsa(ndo, lsap, dataend))
					goto trunc;
				lsap = (const struct lsa6 *)((const u_char *)lsap +
				    EXTRACT_16BITS(&lsap->ls_hdr.ls_length));
			}
		}
		break;

	case OSPF_TYPE_LS_ACK:
		if (ndo->ndo_vflag > 1) {
			lshp = (const struct lsa6_hdr *)((const uint8_t *)op + OSPF6HDR_LEN);
			while ((const u_char *)lshp < dataend) {
				if (ospf6_print_lshdr(ndo, lshp++, dataend))
					goto trunc;
			}
		}
		break;

	default:
		break;
	}
	return (0);
trunc:
	return (1);
}

/* RFC5613 Section 2.2 (w/o the TLVs) */
static int
ospf6_print_lls(netdissect_options *ndo,
                const u_char *cp, const u_int len)
{
	uint16_t llsdatalen;

	if (len == 0)
		return 0;
	if (len < OSPF_LLS_HDRLEN)
		goto trunc;
	/* Checksum */
	ND_TCHECK2(*cp, 2);
	ND_PRINT((ndo, "\n\tLLS Checksum 0x%04x", EXTRACT_16BITS(cp)));
	cp += 2;
	/* LLS Data Length */
	ND_TCHECK2(*cp, 2);
	llsdatalen = EXTRACT_16BITS(cp);
	ND_PRINT((ndo, ", Data Length %u", llsdatalen));
	if (llsdatalen < OSPF_LLS_HDRLEN || llsdatalen > len)
		goto trunc;
	cp += 2;
	/* LLS TLVs */
	ND_TCHECK2(*cp, llsdatalen - OSPF_LLS_HDRLEN);
	/* FIXME: code in print-ospf.c can be reused to decode the TLVs */

	return llsdatalen;
trunc:
	return -1;
}

/* RFC6506 Section 4.1 */
static int
ospf6_decode_at(netdissect_options *ndo,
                const u_char *cp, const u_int len)
{
	uint16_t authdatalen;

	if (len == 0)
		return 0;
	if (len < OSPF6_AT_HDRLEN)
		goto trunc;
	/* Authentication Type */
	ND_TCHECK2(*cp, 2);
	ND_PRINT((ndo, "\n\tAuthentication Type %s", tok2str(ospf6_auth_type_str, "unknown (0x%04x)", EXTRACT_16BITS(cp))));
	cp += 2;
	/* Auth Data Len */
	ND_TCHECK2(*cp, 2);
	authdatalen = EXTRACT_16BITS(cp);
	ND_PRINT((ndo, ", Length %u", authdatalen));
	if (authdatalen < OSPF6_AT_HDRLEN || authdatalen > len)
		goto trunc;
	cp += 2;
	/* Reserved */
	ND_TCHECK2(*cp, 2);
	cp += 2;
	/* Security Association ID */
	ND_TCHECK2(*cp, 2);
	ND_PRINT((ndo, ", SAID %u", EXTRACT_16BITS(cp)));
	cp += 2;
	/* Cryptographic Sequence Number (High-Order 32 Bits) */
	ND_TCHECK2(*cp, 4);
	ND_PRINT((ndo, ", CSN 0x%08x", EXTRACT_32BITS(cp)));
	cp += 4;
	/* Cryptographic Sequence Number (Low-Order 32 Bits) */
	ND_TCHECK2(*cp, 4);
	ND_PRINT((ndo, ":%08x", EXTRACT_32BITS(cp)));
	cp += 4;
	/* Authentication Data */
	ND_TCHECK2(*cp, authdatalen - OSPF6_AT_HDRLEN);
	if (ndo->ndo_vflag > 1)
		print_unknown_data(ndo,cp, "\n\tAuthentication Data ", authdatalen - OSPF6_AT_HDRLEN);
	return 0;

trunc:
	return 1;
}

/* The trailing data may include LLS and/or AT data (in this specific order).
 * LLS data may be present only in Hello and DBDesc packets with the L-bit set.
 * AT data may be present in Hello and DBDesc packets with the AT-bit set or in
 * any other packet type, thus decode the AT data regardless of the AT-bit.
 */
static int
ospf6_decode_v3_trailer(netdissect_options *ndo,
                        const struct ospf6hdr *op, const u_char *cp, const unsigned len)
{
	int llslen = 0;
	int lls_hello = 0;
	int lls_dd = 0;

	if (op->ospf6_type == OSPF_TYPE_HELLO) {
		const struct hello6 *hellop = (const struct hello6 *)((const uint8_t *)op + OSPF6HDR_LEN);
		ND_TCHECK(hellop->hello_options);
		if (EXTRACT_32BITS(&hellop->hello_options) & OSPF6_OPTION_L)
			lls_hello = 1;
	} else if (op->ospf6_type == OSPF_TYPE_DD) {
		const struct dd6 *ddp = (const struct dd6 *)((const uint8_t *)op + OSPF6HDR_LEN);
		ND_TCHECK(ddp->db_options);
		if (EXTRACT_32BITS(&ddp->db_options) & OSPF6_OPTION_L)
			lls_dd = 1;
	}
	if ((lls_hello || lls_dd) && (llslen = ospf6_print_lls(ndo, cp, len)) < 0)
		goto trunc;
	return ospf6_decode_at(ndo, cp + llslen, len - llslen);

trunc:
	return 1;
}

void
ospf6_print(netdissect_options *ndo,
            register const u_char *bp, register u_int length)
{
	register const struct ospf6hdr *op;
	register const u_char *dataend;
	register const char *cp;
	uint16_t datalen;

	op = (const struct ospf6hdr *)bp;

	/* If the type is valid translate it, or just print the type */
	/* value.  If it's not valid, say so and return */
	ND_TCHECK(op->ospf6_type);
	cp = tok2str(ospf6_type_values, "unknown packet type (%u)", op->ospf6_type);
	ND_PRINT((ndo, "OSPFv%u, %s, length %d", op->ospf6_version, cp, length));
	if (*cp == 'u') {
		return;
	}

	if(!ndo->ndo_vflag) { /* non verbose - so lets bail out here */
		return;
	}

	/* OSPFv3 data always comes first and optional trailing data may follow. */
	ND_TCHECK(op->ospf6_len);
	datalen = EXTRACT_16BITS(&op->ospf6_len);
	if (datalen > length) {
		ND_PRINT((ndo, " [len %d]", datalen));
		return;
	}
	dataend = bp + datalen;

	ND_TCHECK(op->ospf6_routerid);
	ND_PRINT((ndo, "\n\tRouter-ID %s", ipaddr_string(ndo, &op->ospf6_routerid)));

	ND_TCHECK(op->ospf6_areaid);
	if (EXTRACT_32BITS(&op->ospf6_areaid) != 0)
		ND_PRINT((ndo, ", Area %s", ipaddr_string(ndo, &op->ospf6_areaid)));
	else
		ND_PRINT((ndo, ", Backbone Area"));
	ND_TCHECK(op->ospf6_instanceid);
	if (op->ospf6_instanceid)
		ND_PRINT((ndo, ", Instance %u", op->ospf6_instanceid));

	/* Do rest according to version.	 */
	switch (op->ospf6_version) {

	case 3:
		/* ospf version 3 */
		if (ospf6_decode_v3(ndo, op, dataend) ||
		    ospf6_decode_v3_trailer(ndo, op, dataend, length - datalen))
			goto trunc;
		break;
	}			/* end switch on version */

	return;
trunc:
	ND_PRINT((ndo, "%s", tstr));
}
