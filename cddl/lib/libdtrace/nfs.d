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
 * Portions Copyright 2006-2008 John Birrell jb@freebsd.org
 *
 * $FreeBSD$
 */

/*
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma	D depends_on library ip.d
#pragma	D depends_on library net.d
#pragma	D depends_on module kernel
#pragma	D depends_on module nfs

typedef struct nfsv4opinfo {
	uint64_t noi_xid;	/* unique transation ID */
	cred_t *noi_cred;	/* credentials for operation */
	string noi_curpath;	/* current file handle path (if any) */
} nfsv4opinfo_t;

typedef struct nfsv4cbinfo {
	string nci_curpath;	/* current file handle path (if any) */
} nfsv4cbinfo_t;

#pragma D binding "1.5" translator
translator conninfo_t < struct svc_req *P > {
	ci_protocol = P->rq_xprt->xp_master->xp_netid == "tcp" ? "ipv4" :
	    P->rq_xprt->xp_master->xp_netid == "udp" ? "ipv4" :
	    P->rq_xprt->xp_master->xp_netid == "tcp6" ? "ipv6" :
	    P->rq_xprt->xp_master->xp_netid == "udp6" ? "ipv6" :
	    "<unknown>";

	ci_local = (P->rq_xprt->xp_master->xp_netid == "tcp" ||
	    P->rq_xprt->xp_master->xp_netid == "udp") ?
	    inet_ntoa(&((struct sockaddr_in *)
	    P->rq_xprt->xp_xpc.xpc_lcladdr.buf)->sin_addr.S_un.S_addr) :
	    (P->rq_xprt->xp_master->xp_netid == "tcp6" ||
	    P->rq_xprt->xp_master->xp_netid == "udp6") ?
	    inet_ntoa6(&((struct sockaddr_in6 *)
	    P->rq_xprt->xp_xpc.xpc_lcladdr.buf)->sin6_addr) :
	    "unknown";

	ci_remote = (P->rq_xprt->xp_master->xp_netid == "tcp" ||
	    P->rq_xprt->xp_master->xp_netid == "udp") ?
	    inet_ntoa(&((struct sockaddr_in *)
	    P->rq_xprt->xp_xpc.xpc_rtaddr.buf)->sin_addr.S_un.S_addr) :
	    (P->rq_xprt->xp_master->xp_netid == "tcp6" ||
	    P->rq_xprt->xp_master->xp_netid == "udp6") ?
	    inet_ntoa6(&((struct sockaddr_in6 *)
	    P->rq_xprt->xp_xpc.xpc_rtaddr.buf)->sin6_addr) :
	    "unknown";
};

#pragma D binding "1.5" translator
translator conninfo_t < rfs4_client_t *P > {
	ci_protocol = (P->rc_addr.ss_family == AF_INET) ? "ipv4" : "ipv6";

	ci_local = "<unknown>";

	ci_remote = (P->rc_addr.ss_family == AF_INET) ?
	    inet_ntoa((ipaddr_t *)
	    &((struct sockaddr_in *)&P->rc_addr)->sin_addr) :
	    inet_ntoa6(&((struct sockaddr_in6 *)&P->rc_addr)->sin6_addr);
};

#pragma D binding "1.5" translator
translator nfsv4cbinfo_t < rfs4_deleg_state_t *P > {
	nci_curpath = (P->rds_finfo->rf_vp == NULL) ? "<unknown>" :
	    P->rds_finfo->rf_vp->v_path;
};

typedef struct nfsv3opinfo {
	uint64_t noi_xid;	/* unique transation ID */
	cred_t *noi_cred;	/* credentials for operation */
	string noi_curpath;	/* current file handle path (if any) */
} nfsv3opinfo_t;

typedef struct nfsv3oparg nfsv3oparg_t;

#pragma D binding "1.5" translator
translator nfsv3opinfo_t < nfsv3oparg_t *P > {
	noi_xid = ((struct svc_req *)arg0)->rq_xprt->xp_xid;
	noi_cred = (cred_t *)arg1;
	noi_curpath = (arg2 == 0 || ((vnode_t *)arg2)->v_path == NULL) ?
	    "<unknown>" : ((vnode_t *)arg2)->v_path;
};
