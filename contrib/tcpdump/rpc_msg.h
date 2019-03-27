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
 *	from: @(#)rpc_msg.h 1.7 86/07/16 SMI
 *	from: @(#)rpc_msg.h	2.1 88/07/29 4.0 RPCSRC
 * $FreeBSD: projects/clang400-import/contrib/tcpdump/rpc_msg.h 276788 2015-01-07 19:55:18Z delphij $
 */

/*
 * rpc_msg.h
 * rpc message definition
 *
 * Copyright (C) 1984, Sun Microsystems, Inc.
 */

#define SUNRPC_MSG_VERSION	((uint32_t) 2)

/*
 * Bottom up definition of an rpc message.
 * NOTE: call and reply use the same overall stuct but
 * different parts of unions within it.
 */

enum sunrpc_msg_type {
	SUNRPC_CALL=0,
	SUNRPC_REPLY=1
};

enum sunrpc_reply_stat {
	SUNRPC_MSG_ACCEPTED=0,
	SUNRPC_MSG_DENIED=1
};

enum sunrpc_accept_stat {
	SUNRPC_SUCCESS=0,
	SUNRPC_PROG_UNAVAIL=1,
	SUNRPC_PROG_MISMATCH=2,
	SUNRPC_PROC_UNAVAIL=3,
	SUNRPC_GARBAGE_ARGS=4,
	SUNRPC_SYSTEM_ERR=5
};

enum sunrpc_reject_stat {
	SUNRPC_RPC_MISMATCH=0,
	SUNRPC_AUTH_ERROR=1
};

/*
 * Reply part of an rpc exchange
 */

/*
 * Reply to an rpc request that was rejected by the server.
 */
struct sunrpc_rejected_reply {
	uint32_t		 rj_stat;	/* enum reject_stat */
	union {
		struct {
			uint32_t low;
			uint32_t high;
		} RJ_versions;
		uint32_t RJ_why;  /* enum auth_stat - why authentication did not work */
	} ru;
#define	rj_vers	ru.RJ_versions
#define	rj_why	ru.RJ_why
};

/*
 * Body of a reply to an rpc request.
 */
struct sunrpc_reply_body {
	uint32_t	rp_stat;		/* enum reply_stat */
	struct sunrpc_rejected_reply rp_reject;	/* if rejected */
};

/*
 * Body of an rpc request call.
 */
struct sunrpc_call_body {
	uint32_t cb_rpcvers;	/* must be equal to two */
	uint32_t cb_prog;
	uint32_t cb_vers;
	uint32_t cb_proc;
	struct sunrpc_opaque_auth cb_cred;
	/* followed by opaque verifier */
};

/*
 * The rpc message
 */
struct sunrpc_msg {
	uint32_t		rm_xid;
	uint32_t		rm_direction;	/* enum msg_type */
	union {
		struct sunrpc_call_body RM_cmb;
		struct sunrpc_reply_body RM_rmb;
	} ru;
#define	rm_call		ru.RM_cmb
#define	rm_reply	ru.RM_rmb
};
#define	acpted_rply	ru.RM_rmb.ru.RP_ar
#define	rjcted_rply	ru.RM_rmb.ru.RP_dr
