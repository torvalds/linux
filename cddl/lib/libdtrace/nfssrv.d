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
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma	D depends_on library ip.d
#pragma	D depends_on library net.d
#pragma	D depends_on library nfs.d
#pragma	D depends_on module kernel
#pragma D depends_on module nfssrv

#pragma D binding "1.5" translator
translator conninfo_t < struct compound_state *P > {
	ci_protocol = P->req->rq_xprt->xp_master->xp_netid == "tcp" ? "ipv4" :
	    P->req->rq_xprt->xp_master->xp_netid == "tcp6" ? "ipv6" :
	    "<unknown>";

	ci_local = inet_ntoa6(&((conn_t *)P->req->rq_xprt->xp_xpc.
	    xpc_wq->q_next->q_ptr)->connua_v6addr.connua_laddr);

	ci_remote = inet_ntoa6(&((conn_t *)P->req->rq_xprt->xp_xpc.
	    xpc_wq->q_next->q_ptr)->connua_v6addr.connua_faddr);
};

#pragma D binding "1.5" translator
translator nfsv4opinfo_t < struct compound_state *P > {
	noi_xid = P->req->rq_xprt->xp_xid;
	noi_cred = P->basecr;
	noi_curpath = (P->vp == NULL) ? "<unknown>" : P->vp->v_path;
};
