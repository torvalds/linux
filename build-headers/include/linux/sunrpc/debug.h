/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * linux/include/linux/sunrpc/debug.h
 *
 * Debugging support for sunrpc module
 *
 * Copyright (C) 1996, Olaf Kirch <okir@monad.swb.de>
 */

#ifndef _LINUX_SUNRPC_DEBUG_H_
#define _LINUX_SUNRPC_DEBUG_H_

/*
 * RPC debug facilities
 */
#define RPCDBG_XPRT		0x0001
#define RPCDBG_CALL		0x0002
#define RPCDBG_DEBUG		0x0004
#define RPCDBG_NFS		0x0008
#define RPCDBG_AUTH		0x0010
#define RPCDBG_BIND		0x0020
#define RPCDBG_SCHED		0x0040
#define RPCDBG_TRANS		0x0080
#define RPCDBG_SVCXPRT		0x0100
#define RPCDBG_SVCDSP		0x0200
#define RPCDBG_MISC		0x0400
#define RPCDBG_CACHE		0x0800
#define RPCDBG_ALL		0x7fff


/*
 * Declarations for the sysctl debug interface, which allows to read or
 * change the debug flags for rpc, nfs, nfsd, and lockd. Since the sunrpc
 * module currently registers its sysctl table dynamically, the sysctl path
 * for module FOO is <CTL_SUNRPC, CTL_FOODEBUG>.
 */

enum {
	CTL_RPCDEBUG = 1,
	CTL_NFSDEBUG,
	CTL_NFSDDEBUG,
	CTL_NLMDEBUG,
	CTL_SLOTTABLE_UDP,
	CTL_SLOTTABLE_TCP,
	CTL_MIN_RESVPORT,
	CTL_MAX_RESVPORT,
};

#endif /* _LINUX_SUNRPC_DEBUG_H_ */
