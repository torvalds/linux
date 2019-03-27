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
 *	from: @(#)auth.h 1.17 88/02/08 SMI
 *	from: @(#)auth.h	2.3 88/08/07 4.0 RPCSRC
 * $FreeBSD: projects/clang400-import/contrib/tcpdump/rpc_auth.h 276788 2015-01-07 19:55:18Z delphij $
 */

/*
 * auth.h, Authentication interface.
 *
 * Copyright (C) 1984, Sun Microsystems, Inc.
 *
 * The data structures are completely opaque to the client.  The client
 * is required to pass a AUTH * to routines that create rpc
 * "sessions".
 */

/*
 * Status returned from authentication check
 */
enum sunrpc_auth_stat {
	SUNRPC_AUTH_OK=0,
	/*
	 * failed at remote end
	 */
	SUNRPC_AUTH_BADCRED=1,		/* bogus credentials (seal broken) */
	SUNRPC_AUTH_REJECTEDCRED=2,	/* client should begin new session */
	SUNRPC_AUTH_BADVERF=3,		/* bogus verifier (seal broken) */
	SUNRPC_AUTH_REJECTEDVERF=4,	/* verifier expired or was replayed */
	SUNRPC_AUTH_TOOWEAK=5,		/* rejected due to security reasons */
	/*
	 * failed locally
	*/
	SUNRPC_AUTH_INVALIDRESP=6,	/* bogus response verifier */
	SUNRPC_AUTH_FAILED=7		/* some unknown reason */
};

/*
 * Authentication info.  Opaque to client.
 */
struct sunrpc_opaque_auth {
	uint32_t oa_flavor;		/* flavor of auth */
	uint32_t oa_len;		/* length of opaque body */
	/* zero or more bytes of body */
};

#define SUNRPC_AUTH_NONE	0	/* no authentication */
#define	SUNRPC_AUTH_NULL	0	/* backward compatibility */
#define	SUNRPC_AUTH_UNIX	1	/* unix style (uid, gids) */
#define	SUNRPC_AUTH_SYS		1	/* forward compatibility */
#define	SUNRPC_AUTH_SHORT	2	/* short hand unix style */
#define SUNRPC_AUTH_DES		3	/* des style (encrypted timestamps) */
