/*
 * Copyright (c) 1992, 1993, 1994, 1995, 1996
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

/* \summary: Sun Remote Procedure Call printer */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

/*
 * At least on HP-UX:
 *
 *	1) getrpcbynumber() is declared in <netdb.h>, not any of the RPC
 *	   header files
 *
 * and
 *
 *	2) if _XOPEN_SOURCE_EXTENDED is defined, <netdb.h> doesn't declare
 *	   it
 *
 * so we undefine it.
 */
#undef _XOPEN_SOURCE_EXTENDED

#include <netdissect-stdinc.h>

#if defined(HAVE_GETRPCBYNUMBER) && defined(HAVE_RPC_RPC_H)
#include <rpc/rpc.h>
#ifdef HAVE_RPC_RPCENT_H
#include <rpc/rpcent.h>
#endif /* HAVE_RPC_RPCENT_H */
#endif /* defined(HAVE_GETRPCBYNUMBER) && defined(HAVE_RPC_RPC_H) */

#include <stdio.h>
#include <string.h>

#include "netdissect.h"
#include "addrtoname.h"
#include "extract.h"

#include "ip.h"
#include "ip6.h"

#include "rpc_auth.h"
#include "rpc_msg.h"

/*
 * Sun RPC is a product of Sun Microsystems, Inc. and is provided for
 * unrestricted use provided that this legend is included on all tape
 * media and as a part of the software program in whole or part.  Users
 * may copy or modify Sun RPC without charge, but are not authorized
 * to license or distribute it to anyone else except as part of a product or
 * program developed by the user.
 *
 * SUN RPC IS PROVIDED AS IS WITH NO WARRANTIES OF ANY KIND INCLUDING THE
 * WARRANTIES OF DESIGN, MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE, OR ARISING FROM A COURSE OF DEALING, USAGE OR TRADE PRACTICE.
 *
 * Sun RPC is provided with no support and without any obligation on the
 * part of Sun Microsystems, Inc. to assist in its use, correction,
 * modification or enhancement.
 *
 * SUN MICROSYSTEMS, INC. SHALL HAVE NO LIABILITY WITH RESPECT TO THE
 * INFRINGEMENT OF COPYRIGHTS, TRADE SECRETS OR ANY PATENTS BY SUN RPC
 * OR ANY PART THEREOF.
 *
 * In no event will Sun Microsystems, Inc. be liable for any lost revenue
 * or profits or other special, indirect and consequential damages, even if
 * Sun has been advised of the possibility of such damages.
 *
 * Sun Microsystems, Inc.
 * 2550 Garcia Avenue
 * Mountain View, California  94043
 *
 *	from: @(#)pmap_prot.h 1.14 88/02/08 SMI
 *	from: @(#)pmap_prot.h	2.1 88/07/29 4.0 RPCSRC
 * $FreeBSD: projects/clang400-import/contrib/tcpdump/print-sunrpc.c 276788 2015-01-07 19:55:18Z delphij $
 */

/*
 * pmap_prot.h
 * Protocol for the local binder service, or pmap.
 *
 * Copyright (C) 1984, Sun Microsystems, Inc.
 *
 * The following procedures are supported by the protocol:
 *
 * PMAPPROC_NULL() returns ()
 * 	takes nothing, returns nothing
 *
 * PMAPPROC_SET(struct pmap) returns (bool_t)
 * 	TRUE is success, FALSE is failure.  Registers the tuple
 *	[prog, vers, prot, port].
 *
 * PMAPPROC_UNSET(struct pmap) returns (bool_t)
 *	TRUE is success, FALSE is failure.  Un-registers pair
 *	[prog, vers].  prot and port are ignored.
 *
 * PMAPPROC_GETPORT(struct pmap) returns (long unsigned).
 *	0 is failure.  Otherwise returns the port number where the pair
 *	[prog, vers] is registered.  It may lie!
 *
 * PMAPPROC_DUMP() RETURNS (struct pmaplist *)
 *
 * PMAPPROC_CALLIT(unsigned, unsigned, unsigned, string<>)
 * 	RETURNS (port, string<>);
 * usage: encapsulatedresults = PMAPPROC_CALLIT(prog, vers, proc, encapsulatedargs);
 * 	Calls the procedure on the local machine.  If it is not registered,
 *	this procedure is quite; ie it does not return error information!!!
 *	This procedure only is supported on rpc/udp and calls via
 *	rpc/udp.  This routine only passes null authentication parameters.
 *	This file has no interface to xdr routines for PMAPPROC_CALLIT.
 *
 * The service supports remote procedure calls on udp/ip or tcp/ip socket 111.
 */

#define SUNRPC_PMAPPORT		((uint16_t)111)
#define SUNRPC_PMAPPROG		((uint32_t)100000)
#define SUNRPC_PMAPVERS		((uint32_t)2)
#define SUNRPC_PMAPVERS_PROTO	((uint32_t)2)
#define SUNRPC_PMAPVERS_ORIG	((uint32_t)1)
#define SUNRPC_PMAPPROC_NULL	((uint32_t)0)
#define SUNRPC_PMAPPROC_SET	((uint32_t)1)
#define SUNRPC_PMAPPROC_UNSET	((uint32_t)2)
#define SUNRPC_PMAPPROC_GETPORT	((uint32_t)3)
#define SUNRPC_PMAPPROC_DUMP	((uint32_t)4)
#define SUNRPC_PMAPPROC_CALLIT	((uint32_t)5)

struct sunrpc_pmap {
	uint32_t pm_prog;
	uint32_t pm_vers;
	uint32_t pm_prot;
	uint32_t pm_port;
};

static const struct tok proc2str[] = {
	{ SUNRPC_PMAPPROC_NULL,		"null" },
	{ SUNRPC_PMAPPROC_SET,		"set" },
	{ SUNRPC_PMAPPROC_UNSET,	"unset" },
	{ SUNRPC_PMAPPROC_GETPORT,	"getport" },
	{ SUNRPC_PMAPPROC_DUMP,		"dump" },
	{ SUNRPC_PMAPPROC_CALLIT,	"call" },
	{ 0,				NULL }
};

/* Forwards */
static char *progstr(uint32_t);

void
sunrpcrequest_print(netdissect_options *ndo, register const u_char *bp,
                    register u_int length, register const u_char *bp2)
{
	register const struct sunrpc_msg *rp;
	register const struct ip *ip;
	register const struct ip6_hdr *ip6;
	uint32_t x;
	char srcid[20], dstid[20];	/*fits 32bit*/

	rp = (const struct sunrpc_msg *)bp;

	if (!ndo->ndo_nflag) {
		snprintf(srcid, sizeof(srcid), "0x%x",
		    EXTRACT_32BITS(&rp->rm_xid));
		strlcpy(dstid, "sunrpc", sizeof(dstid));
	} else {
		snprintf(srcid, sizeof(srcid), "0x%x",
		    EXTRACT_32BITS(&rp->rm_xid));
		snprintf(dstid, sizeof(dstid), "0x%x", SUNRPC_PMAPPORT);
	}

	switch (IP_V((const struct ip *)bp2)) {
	case 4:
		ip = (const struct ip *)bp2;
		ND_PRINT((ndo, "%s.%s > %s.%s: %d",
		    ipaddr_string(ndo, &ip->ip_src), srcid,
		    ipaddr_string(ndo, &ip->ip_dst), dstid, length));
		break;
	case 6:
		ip6 = (const struct ip6_hdr *)bp2;
		ND_PRINT((ndo, "%s.%s > %s.%s: %d",
		    ip6addr_string(ndo, &ip6->ip6_src), srcid,
		    ip6addr_string(ndo, &ip6->ip6_dst), dstid, length));
		break;
	default:
		ND_PRINT((ndo, "%s.%s > %s.%s: %d", "?", srcid, "?", dstid, length));
		break;
	}

	ND_PRINT((ndo, " %s", tok2str(proc2str, " proc #%u",
	    EXTRACT_32BITS(&rp->rm_call.cb_proc))));
	x = EXTRACT_32BITS(&rp->rm_call.cb_rpcvers);
	if (x != 2)
		ND_PRINT((ndo, " [rpcver %u]", x));

	switch (EXTRACT_32BITS(&rp->rm_call.cb_proc)) {

	case SUNRPC_PMAPPROC_SET:
	case SUNRPC_PMAPPROC_UNSET:
	case SUNRPC_PMAPPROC_GETPORT:
	case SUNRPC_PMAPPROC_CALLIT:
		x = EXTRACT_32BITS(&rp->rm_call.cb_prog);
		if (!ndo->ndo_nflag)
			ND_PRINT((ndo, " %s", progstr(x)));
		else
			ND_PRINT((ndo, " %u", x));
		ND_PRINT((ndo, ".%u", EXTRACT_32BITS(&rp->rm_call.cb_vers)));
		break;
	}
}

static char *
progstr(uint32_t prog)
{
#if defined(HAVE_GETRPCBYNUMBER) && defined(HAVE_RPC_RPC_H)
	register struct rpcent *rp;
#endif
	static char buf[32];
	static uint32_t lastprog = 0;

	if (lastprog != 0 && prog == lastprog)
		return (buf);
#if defined(HAVE_GETRPCBYNUMBER) && defined(HAVE_RPC_RPC_H)
	rp = getrpcbynumber(prog);
	if (rp == NULL)
#endif
		(void) snprintf(buf, sizeof(buf), "#%u", prog);
#if defined(HAVE_GETRPCBYNUMBER) && defined(HAVE_RPC_RPC_H)
	else
		strlcpy(buf, rp->r_name, sizeof(buf));
#endif
	return (buf);
}
