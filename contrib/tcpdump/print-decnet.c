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
 */

/* \summary: DECnet printer */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <netdissect-stdinc.h>

struct mbuf;
struct rtentry;

#ifdef HAVE_NETDNET_DNETDB_H
#include <netdnet/dnetdb.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "extract.h"
#include "netdissect.h"
#include "addrtoname.h"

static const char tstr[] = "[|decnet]";

#ifndef _WIN32
typedef uint8_t byte[1];		/* single byte field */
#else
/*
 * the keyword 'byte' generates conflicts in Windows
 */
typedef unsigned char Byte[1];		/* single byte field */
#define byte Byte
#endif /* _WIN32 */
typedef uint8_t word[2];		/* 2 byte field */
typedef uint8_t longword[4];		/* 4 bytes field */

/*
 * Definitions for DECNET Phase IV protocol headers
 */
union etheraddress {
	uint8_t   dne_addr[6];		/* full ethernet address */
	struct {
		uint8_t dne_hiord[4];	/* DECnet HIORD prefix */
		uint8_t dne_nodeaddr[2]; /* DECnet node address */
	} dne_remote;
};

typedef union etheraddress etheraddr;	/* Ethernet address */

#define HIORD 0x000400aa		/* high 32-bits of address (swapped) */

#define AREAMASK	0176000		/* mask for area field */
#define	AREASHIFT	10		/* bit-offset for area field */
#define NODEMASK	01777		/* mask for node address field */

#define DN_MAXADDL	20		/* max size of DECnet address */
struct dn_naddr {
	uint16_t	a_len;		/* length of address */
	uint8_t a_addr[DN_MAXADDL]; /* address as bytes */
};

/*
 * Define long and short header formats.
 */
struct shorthdr
  {
    byte	sh_flags;		/* route flags */
    word	sh_dst;			/* destination node address */
    word	sh_src;			/* source node address */
    byte	sh_visits;		/* visit count */
  };

struct longhdr
  {
    byte	lg_flags;		/* route flags */
    byte	lg_darea;		/* destination area (reserved) */
    byte	lg_dsarea;		/* destination subarea (reserved) */
    etheraddr	lg_dst;			/* destination id */
    byte	lg_sarea;		/* source area (reserved) */
    byte	lg_ssarea;		/* source subarea (reserved) */
    etheraddr	lg_src;			/* source id */
    byte	lg_nextl2;		/* next level 2 router (reserved) */
    byte	lg_visits;		/* visit count */
    byte	lg_service;		/* service class (reserved) */
    byte	lg_pt;			/* protocol type (reserved) */
  };

union routehdr
  {
    struct shorthdr rh_short;		/* short route header */
    struct longhdr rh_long;		/* long route header */
  };

/*
 * Define the values of various fields in the protocol messages.
 *
 * 1. Data packet formats.
 */
#define RMF_MASK	7		/* mask for message type */
#define RMF_SHORT	2		/* short message format */
#define RMF_LONG	6		/* long message format */
#ifndef RMF_RQR
#define RMF_RQR		010		/* request return to sender */
#define RMF_RTS		020		/* returning to sender */
#define RMF_IE		040		/* intra-ethernet packet */
#endif /* RMR_RQR */
#define RMF_FVER	0100		/* future version flag */
#define RMF_PAD		0200		/* pad field */
#define RMF_PADMASK	0177		/* pad field mask */

#define VIS_MASK	077		/* visit field mask */

/*
 * 2. Control packet formats.
 */
#define RMF_CTLMASK	017		/* mask for message type */
#define RMF_CTLMSG	01		/* control message indicator */
#define RMF_INIT	01		/* initialization message */
#define RMF_VER		03		/* verification message */
#define RMF_TEST	05		/* hello and test message */
#define RMF_L1ROUT	07		/* level 1 routing message */
#define RMF_L2ROUT	011		/* level 2 routing message */
#define RMF_RHELLO	013		/* router hello message */
#define RMF_EHELLO	015		/* endnode hello message */

#define TI_L2ROUT	01		/* level 2 router */
#define TI_L1ROUT	02		/* level 1 router */
#define TI_ENDNODE	03		/* endnode */
#define TI_VERIF	04		/* verification required */
#define TI_BLOCK	010		/* blocking requested */

#define VE_VERS		2		/* version number (2) */
#define VE_ECO		0		/* ECO number */
#define VE_UECO		0		/* user ECO number (0) */

#define P3_VERS		1		/* phase III version number (1) */
#define P3_ECO		3		/* ECO number (3) */
#define P3_UECO		0		/* user ECO number (0) */

#define II_L2ROUT	01		/* level 2 router */
#define II_L1ROUT	02		/* level 1 router */
#define II_ENDNODE	03		/* endnode */
#define II_VERIF	04		/* verification required */
#define II_NOMCAST	040		/* no multicast traffic accepted */
#define II_BLOCK	0100		/* blocking requested */
#define II_TYPEMASK	03		/* mask for node type */

#define TESTDATA	0252		/* test data bytes */
#define TESTLEN		1		/* length of transmitted test data */

/*
 * Define control message formats.
 */
struct initmsgIII			/* phase III initialization message */
  {
    byte	inIII_flags;		/* route flags */
    word	inIII_src;		/* source node address */
    byte	inIII_info;		/* routing layer information */
    word	inIII_blksize;		/* maximum data link block size */
    byte	inIII_vers;		/* version number */
    byte	inIII_eco;		/* ECO number */
    byte	inIII_ueco;		/* user ECO number */
    byte	inIII_rsvd;		/* reserved image field */
  };

struct initmsg				/* initialization message */
  {
    byte	in_flags;		/* route flags */
    word	in_src;			/* source node address */
    byte	in_info;		/* routing layer information */
    word	in_blksize;		/* maximum data link block size */
    byte	in_vers;		/* version number */
    byte	in_eco;			/* ECO number */
    byte	in_ueco;		/* user ECO number */
    word	in_hello;		/* hello timer */
    byte	in_rsvd;		/* reserved image field */
  };

struct verifmsg				/* verification message */
  {
    byte	ve_flags;		/* route flags */
    word	ve_src;			/* source node address */
    byte	ve_fcnval;		/* function value image field */
  };

struct testmsg				/* hello and test message */
  {
    byte	te_flags;		/* route flags */
    word	te_src;			/* source node address */
    byte	te_data;		/* test data image field */
  };

struct l1rout				/* level 1 routing message */
  {
    byte	r1_flags;		/* route flags */
    word	r1_src;			/* source node address */
    byte	r1_rsvd;		/* reserved field */
  };

struct l2rout				/* level 2 routing message */
  {
    byte	r2_flags;		/* route flags */
    word	r2_src;			/* source node address */
    byte	r2_rsvd;		/* reserved field */
  };

struct rhellomsg			/* router hello message */
  {
    byte	rh_flags;		/* route flags */
    byte	rh_vers;		/* version number */
    byte	rh_eco;			/* ECO number */
    byte	rh_ueco;		/* user ECO number */
    etheraddr	rh_src;			/* source id */
    byte	rh_info;		/* routing layer information */
    word	rh_blksize;		/* maximum data link block size */
    byte	rh_priority;		/* router's priority */
    byte	rh_area;		/* reserved */
    word	rh_hello;		/* hello timer */
    byte	rh_mpd;			/* reserved */
  };

struct ehellomsg			/* endnode hello message */
  {
    byte	eh_flags;		/* route flags */
    byte	eh_vers;		/* version number */
    byte	eh_eco;			/* ECO number */
    byte	eh_ueco;		/* user ECO number */
    etheraddr	eh_src;			/* source id */
    byte	eh_info;		/* routing layer information */
    word	eh_blksize;		/* maximum data link block size */
    byte	eh_area;		/* area (reserved) */
    byte	eh_seed[8];		/* verification seed */
    etheraddr	eh_router;		/* designated router */
    word	eh_hello;		/* hello timer */
    byte	eh_mpd;			/* (reserved) */
    byte	eh_data;		/* test data image field */
  };

union controlmsg
  {
    struct initmsg	cm_init;	/* initialization message */
    struct verifmsg	cm_ver;		/* verification message */
    struct testmsg	cm_test;	/* hello and test message */
    struct l1rout	cm_l1rou;	/* level 1 routing message */
    struct l2rout	cm_l2rout;	/* level 2 routing message */
    struct rhellomsg	cm_rhello;	/* router hello message */
    struct ehellomsg	cm_ehello;	/* endnode hello message */
  };

/* Macros for decoding routing-info fields */
#define	RI_COST(x)	((x)&0777)
#define	RI_HOPS(x)	(((x)>>10)&037)

/*
 * NSP protocol fields and values.
 */

#define NSP_TYPEMASK 014		/* mask to isolate type code */
#define NSP_SUBMASK 0160		/* mask to isolate subtype code */
#define NSP_SUBSHFT 4			/* shift to move subtype code */

#define MFT_DATA 0			/* data message */
#define MFT_ACK  04			/* acknowledgement message */
#define MFT_CTL  010			/* control message */

#define MFS_ILS  020			/* data or I/LS indicator */
#define MFS_BOM  040			/* beginning of message (data) */
#define MFS_MOM  0			/* middle of message (data) */
#define MFS_EOM  0100			/* end of message (data) */
#define MFS_INT  040			/* interrupt message */

#define MFS_DACK 0			/* data acknowledgement */
#define MFS_IACK 020			/* I/LS acknowledgement */
#define MFS_CACK 040			/* connect acknowledgement */

#define MFS_NOP  0			/* no operation */
#define MFS_CI   020			/* connect initiate */
#define MFS_CC   040			/* connect confirm */
#define MFS_DI   060			/* disconnect initiate */
#define MFS_DC   0100			/* disconnect confirm */
#define MFS_RCI  0140			/* retransmitted connect initiate */

#define SGQ_ACK  0100000		/* ack */
#define SGQ_NAK  0110000		/* negative ack */
#define SGQ_OACK 0120000		/* other channel ack */
#define SGQ_ONAK 0130000		/* other channel negative ack */
#define SGQ_MASK 07777			/* mask to isolate seq # */
#define SGQ_OTHER 020000		/* other channel qualifier */
#define SGQ_DELAY 010000		/* ack delay flag */

#define SGQ_EOM  0100000		/* pseudo flag for end-of-message */

#define LSM_MASK 03			/* mask for modifier field */
#define LSM_NOCHANGE 0			/* no change */
#define LSM_DONOTSEND 1			/* do not send data */
#define LSM_SEND 2			/* send data */

#define LSI_MASK 014			/* mask for interpretation field */
#define LSI_DATA 0			/* data segment or message count */
#define LSI_INTR 4			/* interrupt request count */
#define LSI_INTM 0377			/* funny marker for int. message */

#define COS_MASK 014			/* mask for flow control field */
#define COS_NONE 0			/* no flow control */
#define COS_SEGMENT 04			/* segment flow control */
#define COS_MESSAGE 010			/* message flow control */
#define COS_DEFAULT 1			/* default value for field */

#define COI_MASK 3			/* mask for version field */
#define COI_32 0			/* version 3.2 */
#define COI_31 1			/* version 3.1 */
#define COI_40 2			/* version 4.0 */
#define COI_41 3			/* version 4.1 */

#define MNU_MASK 140			/* mask for session control version */
#define MNU_10 000				/* session V1.0 */
#define MNU_20 040				/* session V2.0 */
#define MNU_ACCESS 1			/* access control present */
#define MNU_USRDATA 2			/* user data field present */
#define MNU_INVKPROXY 4			/* invoke proxy field present */
#define MNU_UICPROXY 8			/* use uic-based proxy */

#define DC_NORESOURCES 1		/* no resource reason code */
#define DC_NOLINK 41			/* no link terminate reason code */
#define DC_COMPLETE 42			/* disconnect complete reason code */

#define DI_NOERROR 0			/* user disconnect */
#define DI_SHUT 3			/* node is shutting down */
#define DI_NOUSER 4			/* destination end user does not exist */
#define DI_INVDEST 5			/* invalid end user destination */
#define DI_REMRESRC 6			/* insufficient remote resources */
#define DI_TPA 8			/* third party abort */
#define DI_PROTOCOL 7			/* protocol error discovered */
#define DI_ABORT 9			/* user abort */
#define DI_LOCALRESRC 32		/* insufficient local resources */
#define DI_REMUSERRESRC 33		/* insufficient remote user resources */
#define DI_BADACCESS 34			/* bad access control information */
#define DI_BADACCNT 36			/* bad ACCOUNT information */
#define DI_CONNECTABORT 38		/* connect request cancelled */
#define DI_TIMEDOUT 38			/* remote node or user crashed */
#define DI_UNREACHABLE 39		/* local timers expired due to ... */
#define DI_BADIMAGE 43			/* bad image data in connect */
#define DI_SERVMISMATCH 54		/* cryptographic service mismatch */

#define UC_OBJREJECT 0			/* object rejected connect */
#define UC_USERDISCONNECT 0		/* user disconnect */
#define UC_RESOURCES 1			/* insufficient resources (local or remote) */
#define UC_NOSUCHNODE 2			/* unrecognized node name */
#define UC_REMOTESHUT 3			/* remote node shutting down */
#define UC_NOSUCHOBJ 4			/* unrecognized object */
#define UC_INVOBJFORMAT 5		/* invalid object name format */
#define UC_OBJTOOBUSY 6			/* object too busy */
#define UC_NETWORKABORT 8		/* network abort */
#define UC_USERABORT 9			/* user abort */
#define UC_INVNODEFORMAT 10		/* invalid node name format */
#define UC_LOCALSHUT 11			/* local node shutting down */
#define UC_ACCESSREJECT 34		/* invalid access control information */
#define UC_NORESPONSE 38		/* no response from object */
#define UC_UNREACHABLE 39		/* node unreachable */

/*
 * NSP message formats.
 */
struct nsphdr				/* general nsp header */
  {
    byte	nh_flags;		/* message flags */
    word	nh_dst;			/* destination link address */
    word	nh_src;			/* source link address */
  };

struct seghdr				/* data segment header */
  {
    byte	sh_flags;		/* message flags */
    word	sh_dst;			/* destination link address */
    word	sh_src;			/* source link address */
    word	sh_seq[3];		/* sequence numbers */
  };

struct minseghdr			/* minimum data segment header */
  {
    byte	ms_flags;		/* message flags */
    word	ms_dst;			/* destination link address */
    word	ms_src;			/* source link address */
    word	ms_seq;			/* sequence number */
  };

struct lsmsg				/* link service message (after hdr) */
  {
    byte	ls_lsflags;		/* link service flags */
    byte	ls_fcval;		/* flow control value */
  };

struct ackmsg				/* acknowledgement message */
  {
    byte	ak_flags;		/* message flags */
    word	ak_dst;			/* destination link address */
    word	ak_src;			/* source link address */
    word	ak_acknum[2];		/* acknowledgement numbers */
  };

struct minackmsg			/* minimum acknowledgement message */
  {
    byte	mk_flags;		/* message flags */
    word	mk_dst;			/* destination link address */
    word	mk_src;			/* source link address */
    word	mk_acknum;		/* acknowledgement number */
  };

struct ciackmsg				/* connect acknowledgement message */
  {
    byte	ck_flags;		/* message flags */
    word	ck_dst;			/* destination link address */
  };

struct cimsg				/* connect initiate message */
  {
    byte	ci_flags;		/* message flags */
    word	ci_dst;			/* destination link address (0) */
    word	ci_src;			/* source link address */
    byte	ci_services;		/* requested services */
    byte	ci_info;		/* information */
    word	ci_segsize;		/* maximum segment size */
  };

struct ccmsg				/* connect confirm message */
  {
    byte	cc_flags;		/* message flags */
    word	cc_dst;			/* destination link address */
    word	cc_src;			/* source link address */
    byte	cc_services;		/* requested services */
    byte	cc_info;		/* information */
    word	cc_segsize;		/* maximum segment size */
    byte	cc_optlen;		/* optional data length */
  };

struct cnmsg				/* generic connect message */
  {
    byte	cn_flags;		/* message flags */
    word	cn_dst;			/* destination link address */
    word	cn_src;			/* source link address */
    byte	cn_services;		/* requested services */
    byte	cn_info;		/* information */
    word	cn_segsize;		/* maximum segment size */
  };

struct dimsg				/* disconnect initiate message */
  {
    byte	di_flags;		/* message flags */
    word	di_dst;			/* destination link address */
    word	di_src;			/* source link address */
    word	di_reason;		/* reason code */
    byte	di_optlen;		/* optional data length */
  };

struct dcmsg				/* disconnect confirm message */
  {
    byte	dc_flags;		/* message flags */
    word	dc_dst;			/* destination link address */
    word	dc_src;			/* source link address */
    word	dc_reason;		/* reason code */
  };

/* Forwards */
static int print_decnet_ctlmsg(netdissect_options *, const union routehdr *, u_int, u_int);
static void print_t_info(netdissect_options *, int);
static int print_l1_routes(netdissect_options *, const char *, u_int);
static int print_l2_routes(netdissect_options *, const char *, u_int);
static void print_i_info(netdissect_options *, int);
static int print_elist(const char *, u_int);
static int print_nsp(netdissect_options *, const u_char *, u_int);
static void print_reason(netdissect_options *, int);

#ifndef HAVE_NETDNET_DNETDB_H_DNET_HTOA
extern char *dnet_htoa(struct dn_naddr *);
#endif

void
decnet_print(netdissect_options *ndo,
             register const u_char *ap, register u_int length,
             register u_int caplen)
{
	register const union routehdr *rhp;
	register int mflags;
	int dst, src, hops;
	u_int nsplen, pktlen;
	const u_char *nspp;

	if (length < sizeof(struct shorthdr)) {
		ND_PRINT((ndo, "%s", tstr));
		return;
	}

	ND_TCHECK2(*ap, sizeof(short));
	pktlen = EXTRACT_LE_16BITS(ap);
	if (pktlen < sizeof(struct shorthdr)) {
		ND_PRINT((ndo, "%s", tstr));
		return;
	}
	if (pktlen > length) {
		ND_PRINT((ndo, "%s", tstr));
		return;
	}
	length = pktlen;

	rhp = (const union routehdr *)&(ap[sizeof(short)]);
	ND_TCHECK(rhp->rh_short.sh_flags);
	mflags = EXTRACT_LE_8BITS(rhp->rh_short.sh_flags);

	if (mflags & RMF_PAD) {
	    /* pad bytes of some sort in front of message */
	    u_int padlen = mflags & RMF_PADMASK;
	    if (ndo->ndo_vflag)
		ND_PRINT((ndo, "[pad:%d] ", padlen));
	    if (length < padlen + 2) {
		ND_PRINT((ndo, "%s", tstr));
		return;
	    }
	    ND_TCHECK2(ap[sizeof(short)], padlen);
	    ap += padlen;
	    length -= padlen;
	    caplen -= padlen;
	    rhp = (const union routehdr *)&(ap[sizeof(short)]);
	    ND_TCHECK(rhp->rh_short.sh_flags);
	    mflags = EXTRACT_LE_8BITS(rhp->rh_short.sh_flags);
	}

	if (mflags & RMF_FVER) {
		ND_PRINT((ndo, "future-version-decnet"));
		ND_DEFAULTPRINT(ap, min(length, caplen));
		return;
	}

	/* is it a control message? */
	if (mflags & RMF_CTLMSG) {
		if (!print_decnet_ctlmsg(ndo, rhp, length, caplen))
			goto trunc;
		return;
	}

	switch (mflags & RMF_MASK) {
	case RMF_LONG:
	    if (length < sizeof(struct longhdr)) {
		ND_PRINT((ndo, "%s", tstr));
		return;
	    }
	    ND_TCHECK(rhp->rh_long);
	    dst =
		EXTRACT_LE_16BITS(rhp->rh_long.lg_dst.dne_remote.dne_nodeaddr);
	    src =
		EXTRACT_LE_16BITS(rhp->rh_long.lg_src.dne_remote.dne_nodeaddr);
	    hops = EXTRACT_LE_8BITS(rhp->rh_long.lg_visits);
	    nspp = &(ap[sizeof(short) + sizeof(struct longhdr)]);
	    nsplen = length - sizeof(struct longhdr);
	    break;
	case RMF_SHORT:
	    ND_TCHECK(rhp->rh_short);
	    dst = EXTRACT_LE_16BITS(rhp->rh_short.sh_dst);
	    src = EXTRACT_LE_16BITS(rhp->rh_short.sh_src);
	    hops = (EXTRACT_LE_8BITS(rhp->rh_short.sh_visits) & VIS_MASK)+1;
	    nspp = &(ap[sizeof(short) + sizeof(struct shorthdr)]);
	    nsplen = length - sizeof(struct shorthdr);
	    break;
	default:
	    ND_PRINT((ndo, "unknown message flags under mask"));
	    ND_DEFAULTPRINT((const u_char *)ap, min(length, caplen));
	    return;
	}

	ND_PRINT((ndo, "%s > %s %d ",
			dnaddr_string(ndo, src), dnaddr_string(ndo, dst), pktlen));
	if (ndo->ndo_vflag) {
	    if (mflags & RMF_RQR)
		ND_PRINT((ndo, "RQR "));
	    if (mflags & RMF_RTS)
		ND_PRINT((ndo, "RTS "));
	    if (mflags & RMF_IE)
		ND_PRINT((ndo, "IE "));
	    ND_PRINT((ndo, "%d hops ", hops));
	}

	if (!print_nsp(ndo, nspp, nsplen))
		goto trunc;
	return;

trunc:
	ND_PRINT((ndo, "%s", tstr));
	return;
}

static int
print_decnet_ctlmsg(netdissect_options *ndo,
                    register const union routehdr *rhp, u_int length,
                    u_int caplen)
{
	/* Our caller has already checked for mflags */
	int mflags = EXTRACT_LE_8BITS(rhp->rh_short.sh_flags);
	register const union controlmsg *cmp = (const union controlmsg *)rhp;
	int src, dst, info, blksize, eco, ueco, hello, other, vers;
	etheraddr srcea, rtea;
	int priority;
	const char *rhpx = (const char *)rhp;
	int ret;

	switch (mflags & RMF_CTLMASK) {
	case RMF_INIT:
	    ND_PRINT((ndo, "init "));
	    if (length < sizeof(struct initmsg))
		goto trunc;
	    ND_TCHECK(cmp->cm_init);
	    src = EXTRACT_LE_16BITS(cmp->cm_init.in_src);
	    info = EXTRACT_LE_8BITS(cmp->cm_init.in_info);
	    blksize = EXTRACT_LE_16BITS(cmp->cm_init.in_blksize);
	    vers = EXTRACT_LE_8BITS(cmp->cm_init.in_vers);
	    eco = EXTRACT_LE_8BITS(cmp->cm_init.in_eco);
	    ueco = EXTRACT_LE_8BITS(cmp->cm_init.in_ueco);
	    hello = EXTRACT_LE_16BITS(cmp->cm_init.in_hello);
	    print_t_info(ndo, info);
	    ND_PRINT((ndo,
		"src %sblksize %d vers %d eco %d ueco %d hello %d",
			dnaddr_string(ndo, src), blksize, vers, eco, ueco,
			hello));
	    ret = 1;
	    break;
	case RMF_VER:
	    ND_PRINT((ndo, "verification "));
	    if (length < sizeof(struct verifmsg))
		goto trunc;
	    ND_TCHECK(cmp->cm_ver);
	    src = EXTRACT_LE_16BITS(cmp->cm_ver.ve_src);
	    other = EXTRACT_LE_8BITS(cmp->cm_ver.ve_fcnval);
	    ND_PRINT((ndo, "src %s fcnval %o", dnaddr_string(ndo, src), other));
	    ret = 1;
	    break;
	case RMF_TEST:
	    ND_PRINT((ndo, "test "));
	    if (length < sizeof(struct testmsg))
		goto trunc;
	    ND_TCHECK(cmp->cm_test);
	    src = EXTRACT_LE_16BITS(cmp->cm_test.te_src);
	    other = EXTRACT_LE_8BITS(cmp->cm_test.te_data);
	    ND_PRINT((ndo, "src %s data %o", dnaddr_string(ndo, src), other));
	    ret = 1;
	    break;
	case RMF_L1ROUT:
	    ND_PRINT((ndo, "lev-1-routing "));
	    if (length < sizeof(struct l1rout))
		goto trunc;
	    ND_TCHECK(cmp->cm_l1rou);
	    src = EXTRACT_LE_16BITS(cmp->cm_l1rou.r1_src);
	    ND_PRINT((ndo, "src %s ", dnaddr_string(ndo, src)));
	    ret = print_l1_routes(ndo, &(rhpx[sizeof(struct l1rout)]),
				length - sizeof(struct l1rout));
	    break;
	case RMF_L2ROUT:
	    ND_PRINT((ndo, "lev-2-routing "));
	    if (length < sizeof(struct l2rout))
		goto trunc;
	    ND_TCHECK(cmp->cm_l2rout);
	    src = EXTRACT_LE_16BITS(cmp->cm_l2rout.r2_src);
	    ND_PRINT((ndo, "src %s ", dnaddr_string(ndo, src)));
	    ret = print_l2_routes(ndo, &(rhpx[sizeof(struct l2rout)]),
				length - sizeof(struct l2rout));
	    break;
	case RMF_RHELLO:
	    ND_PRINT((ndo, "router-hello "));
	    if (length < sizeof(struct rhellomsg))
		goto trunc;
	    ND_TCHECK(cmp->cm_rhello);
	    vers = EXTRACT_LE_8BITS(cmp->cm_rhello.rh_vers);
	    eco = EXTRACT_LE_8BITS(cmp->cm_rhello.rh_eco);
	    ueco = EXTRACT_LE_8BITS(cmp->cm_rhello.rh_ueco);
	    memcpy((char *)&srcea, (const char *)&(cmp->cm_rhello.rh_src),
		sizeof(srcea));
	    src = EXTRACT_LE_16BITS(srcea.dne_remote.dne_nodeaddr);
	    info = EXTRACT_LE_8BITS(cmp->cm_rhello.rh_info);
	    blksize = EXTRACT_LE_16BITS(cmp->cm_rhello.rh_blksize);
	    priority = EXTRACT_LE_8BITS(cmp->cm_rhello.rh_priority);
	    hello = EXTRACT_LE_16BITS(cmp->cm_rhello.rh_hello);
	    print_i_info(ndo, info);
	    ND_PRINT((ndo,
	    "vers %d eco %d ueco %d src %s blksize %d pri %d hello %d",
			vers, eco, ueco, dnaddr_string(ndo, src),
			blksize, priority, hello));
	    ret = print_elist(&(rhpx[sizeof(struct rhellomsg)]),
				length - sizeof(struct rhellomsg));
	    break;
	case RMF_EHELLO:
	    ND_PRINT((ndo, "endnode-hello "));
	    if (length < sizeof(struct ehellomsg))
		goto trunc;
	    ND_TCHECK(cmp->cm_ehello);
	    vers = EXTRACT_LE_8BITS(cmp->cm_ehello.eh_vers);
	    eco = EXTRACT_LE_8BITS(cmp->cm_ehello.eh_eco);
	    ueco = EXTRACT_LE_8BITS(cmp->cm_ehello.eh_ueco);
	    memcpy((char *)&srcea, (const char *)&(cmp->cm_ehello.eh_src),
		sizeof(srcea));
	    src = EXTRACT_LE_16BITS(srcea.dne_remote.dne_nodeaddr);
	    info = EXTRACT_LE_8BITS(cmp->cm_ehello.eh_info);
	    blksize = EXTRACT_LE_16BITS(cmp->cm_ehello.eh_blksize);
	    /*seed*/
	    memcpy((char *)&rtea, (const char *)&(cmp->cm_ehello.eh_router),
		sizeof(rtea));
	    dst = EXTRACT_LE_16BITS(rtea.dne_remote.dne_nodeaddr);
	    hello = EXTRACT_LE_16BITS(cmp->cm_ehello.eh_hello);
	    other = EXTRACT_LE_8BITS(cmp->cm_ehello.eh_data);
	    print_i_info(ndo, info);
	    ND_PRINT((ndo,
	"vers %d eco %d ueco %d src %s blksize %d rtr %s hello %d data %o",
			vers, eco, ueco, dnaddr_string(ndo, src),
			blksize, dnaddr_string(ndo, dst), hello, other));
	    ret = 1;
	    break;

	default:
	    ND_PRINT((ndo, "unknown control message"));
	    ND_DEFAULTPRINT((const u_char *)rhp, min(length, caplen));
	    ret = 1;
	    break;
	}
	return (ret);

trunc:
	return (0);
}

static void
print_t_info(netdissect_options *ndo,
             int info)
{
	int ntype = info & 3;
	switch (ntype) {
	case 0: ND_PRINT((ndo, "reserved-ntype? ")); break;
	case TI_L2ROUT: ND_PRINT((ndo, "l2rout ")); break;
	case TI_L1ROUT: ND_PRINT((ndo, "l1rout ")); break;
	case TI_ENDNODE: ND_PRINT((ndo, "endnode ")); break;
	}
	if (info & TI_VERIF)
	    ND_PRINT((ndo, "verif "));
	if (info & TI_BLOCK)
	    ND_PRINT((ndo, "blo "));
}

static int
print_l1_routes(netdissect_options *ndo,
                const char *rp, u_int len)
{
	int count;
	int id;
	int info;

	/* The last short is a checksum */
	while (len > (3 * sizeof(short))) {
	    ND_TCHECK2(*rp, 3 * sizeof(short));
	    count = EXTRACT_LE_16BITS(rp);
	    if (count > 1024)
		return (1);	/* seems to be bogus from here on */
	    rp += sizeof(short);
	    len -= sizeof(short);
	    id = EXTRACT_LE_16BITS(rp);
	    rp += sizeof(short);
	    len -= sizeof(short);
	    info = EXTRACT_LE_16BITS(rp);
	    rp += sizeof(short);
	    len -= sizeof(short);
	    ND_PRINT((ndo, "{ids %d-%d cost %d hops %d} ", id, id + count,
			    RI_COST(info), RI_HOPS(info)));
	}
	return (1);

trunc:
	return (0);
}

static int
print_l2_routes(netdissect_options *ndo,
                const char *rp, u_int len)
{
	int count;
	int area;
	int info;

	/* The last short is a checksum */
	while (len > (3 * sizeof(short))) {
	    ND_TCHECK2(*rp, 3 * sizeof(short));
	    count = EXTRACT_LE_16BITS(rp);
	    if (count > 1024)
		return (1);	/* seems to be bogus from here on */
	    rp += sizeof(short);
	    len -= sizeof(short);
	    area = EXTRACT_LE_16BITS(rp);
	    rp += sizeof(short);
	    len -= sizeof(short);
	    info = EXTRACT_LE_16BITS(rp);
	    rp += sizeof(short);
	    len -= sizeof(short);
	    ND_PRINT((ndo, "{areas %d-%d cost %d hops %d} ", area, area + count,
			    RI_COST(info), RI_HOPS(info)));
	}
	return (1);

trunc:
	return (0);
}

static void
print_i_info(netdissect_options *ndo,
             int info)
{
	int ntype = info & II_TYPEMASK;
	switch (ntype) {
	case 0: ND_PRINT((ndo, "reserved-ntype? ")); break;
	case II_L2ROUT: ND_PRINT((ndo, "l2rout ")); break;
	case II_L1ROUT: ND_PRINT((ndo, "l1rout ")); break;
	case II_ENDNODE: ND_PRINT((ndo, "endnode ")); break;
	}
	if (info & II_VERIF)
	    ND_PRINT((ndo, "verif "));
	if (info & II_NOMCAST)
	    ND_PRINT((ndo, "nomcast "));
	if (info & II_BLOCK)
	    ND_PRINT((ndo, "blo "));
}

static int
print_elist(const char *elp _U_, u_int len _U_)
{
	/* Not enough examples available for me to debug this */
	return (1);
}

static int
print_nsp(netdissect_options *ndo,
          const u_char *nspp, u_int nsplen)
{
	const struct nsphdr *nsphp = (const struct nsphdr *)nspp;
	int dst, src, flags;

	if (nsplen < sizeof(struct nsphdr))
		goto trunc;
	ND_TCHECK(*nsphp);
	flags = EXTRACT_LE_8BITS(nsphp->nh_flags);
	dst = EXTRACT_LE_16BITS(nsphp->nh_dst);
	src = EXTRACT_LE_16BITS(nsphp->nh_src);

	switch (flags & NSP_TYPEMASK) {
	case MFT_DATA:
	    switch (flags & NSP_SUBMASK) {
	    case MFS_BOM:
	    case MFS_MOM:
	    case MFS_EOM:
	    case MFS_BOM+MFS_EOM:
		ND_PRINT((ndo, "data %d>%d ", src, dst));
		{
		    const struct seghdr *shp = (const struct seghdr *)nspp;
		    int ack;
		    u_int data_off = sizeof(struct minseghdr);

		    if (nsplen < data_off)
			goto trunc;
		    ND_TCHECK(shp->sh_seq[0]);
		    ack = EXTRACT_LE_16BITS(shp->sh_seq[0]);
		    if (ack & SGQ_ACK) {	/* acknum field */
			if ((ack & SGQ_NAK) == SGQ_NAK)
			    ND_PRINT((ndo, "nak %d ", ack & SGQ_MASK));
			else
			    ND_PRINT((ndo, "ack %d ", ack & SGQ_MASK));
			data_off += sizeof(short);
			if (nsplen < data_off)
			    goto trunc;
			ND_TCHECK(shp->sh_seq[1]);
		        ack = EXTRACT_LE_16BITS(shp->sh_seq[1]);
			if (ack & SGQ_OACK) {	/* ackoth field */
			    if ((ack & SGQ_ONAK) == SGQ_ONAK)
				ND_PRINT((ndo, "onak %d ", ack & SGQ_MASK));
			    else
				ND_PRINT((ndo, "oack %d ", ack & SGQ_MASK));
			    data_off += sizeof(short);
			    if (nsplen < data_off)
				goto trunc;
			    ND_TCHECK(shp->sh_seq[2]);
			    ack = EXTRACT_LE_16BITS(shp->sh_seq[2]);
			}
		    }
		    ND_PRINT((ndo, "seg %d ", ack & SGQ_MASK));
		}
		break;
	    case MFS_ILS+MFS_INT:
		ND_PRINT((ndo, "intr "));
		{
		    const struct seghdr *shp = (const struct seghdr *)nspp;
		    int ack;
		    u_int data_off = sizeof(struct minseghdr);

		    if (nsplen < data_off)
			goto trunc;
		    ND_TCHECK(shp->sh_seq[0]);
		    ack = EXTRACT_LE_16BITS(shp->sh_seq[0]);
		    if (ack & SGQ_ACK) {	/* acknum field */
			if ((ack & SGQ_NAK) == SGQ_NAK)
			    ND_PRINT((ndo, "nak %d ", ack & SGQ_MASK));
			else
			    ND_PRINT((ndo, "ack %d ", ack & SGQ_MASK));
			data_off += sizeof(short);
			if (nsplen < data_off)
			    goto trunc;
			ND_TCHECK(shp->sh_seq[1]);
		        ack = EXTRACT_LE_16BITS(shp->sh_seq[1]);
			if (ack & SGQ_OACK) {	/* ackdat field */
			    if ((ack & SGQ_ONAK) == SGQ_ONAK)
				ND_PRINT((ndo, "nakdat %d ", ack & SGQ_MASK));
			    else
				ND_PRINT((ndo, "ackdat %d ", ack & SGQ_MASK));
			    data_off += sizeof(short);
			    if (nsplen < data_off)
				goto trunc;
			    ND_TCHECK(shp->sh_seq[2]);
			    ack = EXTRACT_LE_16BITS(shp->sh_seq[2]);
			}
		    }
		    ND_PRINT((ndo, "seg %d ", ack & SGQ_MASK));
		}
		break;
	    case MFS_ILS:
		ND_PRINT((ndo, "link-service %d>%d ", src, dst));
		{
		    const struct seghdr *shp = (const struct seghdr *)nspp;
		    const struct lsmsg *lsmp =
			(const struct lsmsg *)&(nspp[sizeof(struct seghdr)]);
		    int ack;
		    int lsflags, fcval;

		    if (nsplen < sizeof(struct seghdr) + sizeof(struct lsmsg))
			goto trunc;
		    ND_TCHECK(shp->sh_seq[0]);
		    ack = EXTRACT_LE_16BITS(shp->sh_seq[0]);
		    if (ack & SGQ_ACK) {	/* acknum field */
			if ((ack & SGQ_NAK) == SGQ_NAK)
			    ND_PRINT((ndo, "nak %d ", ack & SGQ_MASK));
			else
			    ND_PRINT((ndo, "ack %d ", ack & SGQ_MASK));
			ND_TCHECK(shp->sh_seq[1]);
		        ack = EXTRACT_LE_16BITS(shp->sh_seq[1]);
			if (ack & SGQ_OACK) {	/* ackdat field */
			    if ((ack & SGQ_ONAK) == SGQ_ONAK)
				ND_PRINT((ndo, "nakdat %d ", ack & SGQ_MASK));
			    else
				ND_PRINT((ndo, "ackdat %d ", ack & SGQ_MASK));
			    ND_TCHECK(shp->sh_seq[2]);
			    ack = EXTRACT_LE_16BITS(shp->sh_seq[2]);
			}
		    }
		    ND_PRINT((ndo, "seg %d ", ack & SGQ_MASK));
		    ND_TCHECK(*lsmp);
		    lsflags = EXTRACT_LE_8BITS(lsmp->ls_lsflags);
		    fcval = EXTRACT_LE_8BITS(lsmp->ls_fcval);
		    switch (lsflags & LSI_MASK) {
		    case LSI_DATA:
			ND_PRINT((ndo, "dat seg count %d ", fcval));
			switch (lsflags & LSM_MASK) {
			case LSM_NOCHANGE:
			    break;
			case LSM_DONOTSEND:
			    ND_PRINT((ndo, "donotsend-data "));
			    break;
			case LSM_SEND:
			    ND_PRINT((ndo, "send-data "));
			    break;
			default:
			    ND_PRINT((ndo, "reserved-fcmod? %x", lsflags));
			    break;
			}
			break;
		    case LSI_INTR:
			ND_PRINT((ndo, "intr req count %d ", fcval));
			break;
		    default:
			ND_PRINT((ndo, "reserved-fcval-int? %x", lsflags));
			break;
		    }
		}
		break;
	    default:
		ND_PRINT((ndo, "reserved-subtype? %x %d > %d", flags, src, dst));
		break;
	    }
	    break;
	case MFT_ACK:
	    switch (flags & NSP_SUBMASK) {
	    case MFS_DACK:
		ND_PRINT((ndo, "data-ack %d>%d ", src, dst));
		{
		    const struct ackmsg *amp = (const struct ackmsg *)nspp;
		    int ack;

		    if (nsplen < sizeof(struct ackmsg))
			goto trunc;
		    ND_TCHECK(*amp);
		    ack = EXTRACT_LE_16BITS(amp->ak_acknum[0]);
		    if (ack & SGQ_ACK) {	/* acknum field */
			if ((ack & SGQ_NAK) == SGQ_NAK)
			    ND_PRINT((ndo, "nak %d ", ack & SGQ_MASK));
			else
			    ND_PRINT((ndo, "ack %d ", ack & SGQ_MASK));
		        ack = EXTRACT_LE_16BITS(amp->ak_acknum[1]);
			if (ack & SGQ_OACK) {	/* ackoth field */
			    if ((ack & SGQ_ONAK) == SGQ_ONAK)
				ND_PRINT((ndo, "onak %d ", ack & SGQ_MASK));
			    else
				ND_PRINT((ndo, "oack %d ", ack & SGQ_MASK));
			}
		    }
		}
		break;
	    case MFS_IACK:
		ND_PRINT((ndo, "ils-ack %d>%d ", src, dst));
		{
		    const struct ackmsg *amp = (const struct ackmsg *)nspp;
		    int ack;

		    if (nsplen < sizeof(struct ackmsg))
			goto trunc;
		    ND_TCHECK(*amp);
		    ack = EXTRACT_LE_16BITS(amp->ak_acknum[0]);
		    if (ack & SGQ_ACK) {	/* acknum field */
			if ((ack & SGQ_NAK) == SGQ_NAK)
			    ND_PRINT((ndo, "nak %d ", ack & SGQ_MASK));
			else
			    ND_PRINT((ndo, "ack %d ", ack & SGQ_MASK));
			ND_TCHECK(amp->ak_acknum[1]);
		        ack = EXTRACT_LE_16BITS(amp->ak_acknum[1]);
			if (ack & SGQ_OACK) {	/* ackdat field */
			    if ((ack & SGQ_ONAK) == SGQ_ONAK)
				ND_PRINT((ndo, "nakdat %d ", ack & SGQ_MASK));
			    else
				ND_PRINT((ndo, "ackdat %d ", ack & SGQ_MASK));
			}
		    }
		}
		break;
	    case MFS_CACK:
		ND_PRINT((ndo, "conn-ack %d", dst));
		break;
	    default:
		ND_PRINT((ndo, "reserved-acktype? %x %d > %d", flags, src, dst));
		break;
	    }
	    break;
	case MFT_CTL:
	    switch (flags & NSP_SUBMASK) {
	    case MFS_CI:
	    case MFS_RCI:
		if ((flags & NSP_SUBMASK) == MFS_CI)
		    ND_PRINT((ndo, "conn-initiate "));
		else
		    ND_PRINT((ndo, "retrans-conn-initiate "));
		ND_PRINT((ndo, "%d>%d ", src, dst));
		{
		    const struct cimsg *cimp = (const struct cimsg *)nspp;
		    int services, info, segsize;

		    if (nsplen < sizeof(struct cimsg))
			goto trunc;
		    ND_TCHECK(*cimp);
		    services = EXTRACT_LE_8BITS(cimp->ci_services);
		    info = EXTRACT_LE_8BITS(cimp->ci_info);
		    segsize = EXTRACT_LE_16BITS(cimp->ci_segsize);

		    switch (services & COS_MASK) {
		    case COS_NONE:
			break;
		    case COS_SEGMENT:
			ND_PRINT((ndo, "seg "));
			break;
		    case COS_MESSAGE:
			ND_PRINT((ndo, "msg "));
			break;
		    }
		    switch (info & COI_MASK) {
		    case COI_32:
			ND_PRINT((ndo, "ver 3.2 "));
			break;
		    case COI_31:
			ND_PRINT((ndo, "ver 3.1 "));
			break;
		    case COI_40:
			ND_PRINT((ndo, "ver 4.0 "));
			break;
		    case COI_41:
			ND_PRINT((ndo, "ver 4.1 "));
			break;
		    }
		    ND_PRINT((ndo, "segsize %d ", segsize));
		}
		break;
	    case MFS_CC:
		ND_PRINT((ndo, "conn-confirm %d>%d ", src, dst));
		{
		    const struct ccmsg *ccmp = (const struct ccmsg *)nspp;
		    int services, info;
		    u_int segsize, optlen;

		    if (nsplen < sizeof(struct ccmsg))
			goto trunc;
		    ND_TCHECK(*ccmp);
		    services = EXTRACT_LE_8BITS(ccmp->cc_services);
		    info = EXTRACT_LE_8BITS(ccmp->cc_info);
		    segsize = EXTRACT_LE_16BITS(ccmp->cc_segsize);
		    optlen = EXTRACT_LE_8BITS(ccmp->cc_optlen);

		    switch (services & COS_MASK) {
		    case COS_NONE:
			break;
		    case COS_SEGMENT:
			ND_PRINT((ndo, "seg "));
			break;
		    case COS_MESSAGE:
			ND_PRINT((ndo, "msg "));
			break;
		    }
		    switch (info & COI_MASK) {
		    case COI_32:
			ND_PRINT((ndo, "ver 3.2 "));
			break;
		    case COI_31:
			ND_PRINT((ndo, "ver 3.1 "));
			break;
		    case COI_40:
			ND_PRINT((ndo, "ver 4.0 "));
			break;
		    case COI_41:
			ND_PRINT((ndo, "ver 4.1 "));
			break;
		    }
		    ND_PRINT((ndo, "segsize %d ", segsize));
		    if (optlen) {
			ND_PRINT((ndo, "optlen %d ", optlen));
		    }
		}
		break;
	    case MFS_DI:
		ND_PRINT((ndo, "disconn-initiate %d>%d ", src, dst));
		{
		    const struct dimsg *dimp = (const struct dimsg *)nspp;
		    int reason;
		    u_int optlen;

		    if (nsplen < sizeof(struct dimsg))
			goto trunc;
		    ND_TCHECK(*dimp);
		    reason = EXTRACT_LE_16BITS(dimp->di_reason);
		    optlen = EXTRACT_LE_8BITS(dimp->di_optlen);

		    print_reason(ndo, reason);
		    if (optlen) {
			ND_PRINT((ndo, "optlen %d ", optlen));
		    }
		}
		break;
	    case MFS_DC:
		ND_PRINT((ndo, "disconn-confirm %d>%d ", src, dst));
		{
		    const struct dcmsg *dcmp = (const struct dcmsg *)nspp;
		    int reason;

		    ND_TCHECK(*dcmp);
		    reason = EXTRACT_LE_16BITS(dcmp->dc_reason);

		    print_reason(ndo, reason);
		}
		break;
	    default:
		ND_PRINT((ndo, "reserved-ctltype? %x %d > %d", flags, src, dst));
		break;
	    }
	    break;
	default:
	    ND_PRINT((ndo, "reserved-type? %x %d > %d", flags, src, dst));
	    break;
	}
	return (1);

trunc:
	return (0);
}

static const struct tok reason2str[] = {
	{ UC_OBJREJECT,		"object rejected connect" },
	{ UC_RESOURCES,		"insufficient resources" },
	{ UC_NOSUCHNODE,	"unrecognized node name" },
	{ DI_SHUT,		"node is shutting down" },
	{ UC_NOSUCHOBJ,		"unrecognized object" },
	{ UC_INVOBJFORMAT,	"invalid object name format" },
	{ UC_OBJTOOBUSY,	"object too busy" },
	{ DI_PROTOCOL,		"protocol error discovered" },
	{ DI_TPA,		"third party abort" },
	{ UC_USERABORT,		"user abort" },
	{ UC_INVNODEFORMAT,	"invalid node name format" },
	{ UC_LOCALSHUT,		"local node shutting down" },
	{ DI_LOCALRESRC,	"insufficient local resources" },
	{ DI_REMUSERRESRC,	"insufficient remote user resources" },
	{ UC_ACCESSREJECT,	"invalid access control information" },
	{ DI_BADACCNT,		"bad ACCOUNT information" },
	{ UC_NORESPONSE,	"no response from object" },
	{ UC_UNREACHABLE,	"node unreachable" },
	{ DC_NOLINK,		"no link terminate" },
	{ DC_COMPLETE,		"disconnect complete" },
	{ DI_BADIMAGE,		"bad image data in connect" },
	{ DI_SERVMISMATCH,	"cryptographic service mismatch" },
	{ 0,			NULL }
};

static void
print_reason(netdissect_options *ndo,
             register int reason)
{
	ND_PRINT((ndo, "%s ", tok2str(reason2str, "reason-%d", reason)));
}

const char *
dnnum_string(netdissect_options *ndo, u_short dnaddr)
{
	char *str;
	size_t siz;
	int area = (u_short)(dnaddr & AREAMASK) >> AREASHIFT;
	int node = dnaddr & NODEMASK;

	str = (char *)malloc(siz = sizeof("00.0000"));
	if (str == NULL)
		(*ndo->ndo_error)(ndo, "dnnum_string: malloc");
	snprintf(str, siz, "%d.%d", area, node);
	return(str);
}

const char *
dnname_string(netdissect_options *ndo, u_short dnaddr)
{
#ifdef HAVE_DNET_HTOA
	struct dn_naddr dna;
	char *dnname;

	dna.a_len = sizeof(short);
	memcpy((char *)dna.a_addr, (char *)&dnaddr, sizeof(short));
	dnname = dnet_htoa(&dna);
	if(dnname != NULL)
		return (strdup(dnname));
	else
		return(dnnum_string(ndo, dnaddr));
#else
	return(dnnum_string(ndo, dnaddr));	/* punt */
#endif
}
