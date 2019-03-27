/*
 * Copyright (c) 1988, 1989, 1990, 1991, 1992, 1993, 1994, 1995, 1996, 1997
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

/* \summary: Network File System (NFS) printer */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <netdissect-stdinc.h>

#include <stdio.h>
#include <string.h>

#include "netdissect.h"
#include "addrtoname.h"
#include "extract.h"

#include "nfs.h"
#include "nfsfh.h"

#include "ip.h"
#include "ip6.h"
#include "rpc_auth.h"
#include "rpc_msg.h"

static const char tstr[] = " [|nfs]";

static void nfs_printfh(netdissect_options *, const uint32_t *, const u_int);
static int xid_map_enter(netdissect_options *, const struct sunrpc_msg *, const u_char *);
static int xid_map_find(const struct sunrpc_msg *, const u_char *,
			    uint32_t *, uint32_t *);
static void interp_reply(netdissect_options *, const struct sunrpc_msg *, uint32_t, uint32_t, int);
static const uint32_t *parse_post_op_attr(netdissect_options *, const uint32_t *, int);

/*
 * Mapping of old NFS Version 2 RPC numbers to generic numbers.
 */
static uint32_t nfsv3_procid[NFS_NPROCS] = {
	NFSPROC_NULL,
	NFSPROC_GETATTR,
	NFSPROC_SETATTR,
	NFSPROC_NOOP,
	NFSPROC_LOOKUP,
	NFSPROC_READLINK,
	NFSPROC_READ,
	NFSPROC_NOOP,
	NFSPROC_WRITE,
	NFSPROC_CREATE,
	NFSPROC_REMOVE,
	NFSPROC_RENAME,
	NFSPROC_LINK,
	NFSPROC_SYMLINK,
	NFSPROC_MKDIR,
	NFSPROC_RMDIR,
	NFSPROC_READDIR,
	NFSPROC_FSSTAT,
	NFSPROC_NOOP,
	NFSPROC_NOOP,
	NFSPROC_NOOP,
	NFSPROC_NOOP,
	NFSPROC_NOOP,
	NFSPROC_NOOP,
	NFSPROC_NOOP,
	NFSPROC_NOOP
};

static const struct tok nfsproc_str[] = {
	{ NFSPROC_NOOP,        "nop"         },
	{ NFSPROC_NULL,        "null"        },
	{ NFSPROC_GETATTR,     "getattr"     },
	{ NFSPROC_SETATTR,     "setattr"     },
	{ NFSPROC_LOOKUP,      "lookup"      },
	{ NFSPROC_ACCESS,      "access"      },
	{ NFSPROC_READLINK,    "readlink"    },
	{ NFSPROC_READ,        "read"        },
	{ NFSPROC_WRITE,       "write"       },
	{ NFSPROC_CREATE,      "create"      },
	{ NFSPROC_MKDIR,       "mkdir"       },
	{ NFSPROC_SYMLINK,     "symlink"     },
	{ NFSPROC_MKNOD,       "mknod"       },
	{ NFSPROC_REMOVE,      "remove"      },
	{ NFSPROC_RMDIR,       "rmdir"       },
	{ NFSPROC_RENAME,      "rename"      },
	{ NFSPROC_LINK,        "link"        },
	{ NFSPROC_READDIR,     "readdir"     },
	{ NFSPROC_READDIRPLUS, "readdirplus" },
	{ NFSPROC_FSSTAT,      "fsstat"      },
	{ NFSPROC_FSINFO,      "fsinfo"      },
	{ NFSPROC_PATHCONF,    "pathconf"    },
	{ NFSPROC_COMMIT,      "commit"      },
	{ 0, NULL }
};

/*
 * NFS V2 and V3 status values.
 *
 * Some of these come from the RFCs for NFS V2 and V3, with the message
 * strings taken from the FreeBSD C library "errlst.c".
 *
 * Others are errors that are not in the RFC but that I suspect some
 * NFS servers could return; the values are FreeBSD errno values, as
 * the first NFS server was the SunOS 2.0 one, and until 5.0 SunOS
 * was primarily BSD-derived.
 */
static const struct tok status2str[] = {
	{ 1,     "Operation not permitted" },	/* EPERM */
	{ 2,     "No such file or directory" },	/* ENOENT */
	{ 5,     "Input/output error" },	/* EIO */
	{ 6,     "Device not configured" },	/* ENXIO */
	{ 11,    "Resource deadlock avoided" },	/* EDEADLK */
	{ 12,    "Cannot allocate memory" },	/* ENOMEM */
	{ 13,    "Permission denied" },		/* EACCES */
	{ 17,    "File exists" },		/* EEXIST */
	{ 18,    "Cross-device link" },		/* EXDEV */
	{ 19,    "Operation not supported by device" }, /* ENODEV */
	{ 20,    "Not a directory" },		/* ENOTDIR */
	{ 21,    "Is a directory" },		/* EISDIR */
	{ 22,    "Invalid argument" },		/* EINVAL */
	{ 26,    "Text file busy" },		/* ETXTBSY */
	{ 27,    "File too large" },		/* EFBIG */
	{ 28,    "No space left on device" },	/* ENOSPC */
	{ 30,    "Read-only file system" },	/* EROFS */
	{ 31,    "Too many links" },		/* EMLINK */
	{ 45,    "Operation not supported" },	/* EOPNOTSUPP */
	{ 62,    "Too many levels of symbolic links" }, /* ELOOP */
	{ 63,    "File name too long" },	/* ENAMETOOLONG */
	{ 66,    "Directory not empty" },	/* ENOTEMPTY */
	{ 69,    "Disc quota exceeded" },	/* EDQUOT */
	{ 70,    "Stale NFS file handle" },	/* ESTALE */
	{ 71,    "Too many levels of remote in path" }, /* EREMOTE */
	{ 99,    "Write cache flushed to disk" }, /* NFSERR_WFLUSH (not used) */
	{ 10001, "Illegal NFS file handle" },	/* NFS3ERR_BADHANDLE */
	{ 10002, "Update synchronization mismatch" }, /* NFS3ERR_NOT_SYNC */
	{ 10003, "READDIR/READDIRPLUS cookie is stale" }, /* NFS3ERR_BAD_COOKIE */
	{ 10004, "Operation not supported" },	/* NFS3ERR_NOTSUPP */
	{ 10005, "Buffer or request is too small" }, /* NFS3ERR_TOOSMALL */
	{ 10006, "Unspecified error on server" }, /* NFS3ERR_SERVERFAULT */
	{ 10007, "Object of that type not supported" }, /* NFS3ERR_BADTYPE */
	{ 10008, "Request couldn't be completed in time" }, /* NFS3ERR_JUKEBOX */
	{ 0,     NULL }
};

static const struct tok nfsv3_writemodes[] = {
	{ 0,		"unstable" },
	{ 1,		"datasync" },
	{ 2,		"filesync" },
	{ 0,		NULL }
};

static const struct tok type2str[] = {
	{ NFNON,	"NON" },
	{ NFREG,	"REG" },
	{ NFDIR,	"DIR" },
	{ NFBLK,	"BLK" },
	{ NFCHR,	"CHR" },
	{ NFLNK,	"LNK" },
	{ NFFIFO,	"FIFO" },
	{ 0,		NULL }
};

static const struct tok sunrpc_auth_str[] = {
	{ SUNRPC_AUTH_OK,           "OK"                                                     },
	{ SUNRPC_AUTH_BADCRED,      "Bogus Credentials (seal broken)"                        },
	{ SUNRPC_AUTH_REJECTEDCRED, "Rejected Credentials (client should begin new session)" },
	{ SUNRPC_AUTH_BADVERF,      "Bogus Verifier (seal broken)"                           },
	{ SUNRPC_AUTH_REJECTEDVERF, "Verifier expired or was replayed"                       },
	{ SUNRPC_AUTH_TOOWEAK,      "Credentials are too weak"                               },
	{ SUNRPC_AUTH_INVALIDRESP,  "Bogus response verifier"                                },
	{ SUNRPC_AUTH_FAILED,       "Unknown failure"                                        },
	{ 0, NULL }
};

static const struct tok sunrpc_str[] = {
	{ SUNRPC_PROG_UNAVAIL,  "PROG_UNAVAIL"  },
	{ SUNRPC_PROG_MISMATCH, "PROG_MISMATCH" },
	{ SUNRPC_PROC_UNAVAIL,  "PROC_UNAVAIL"  },
	{ SUNRPC_GARBAGE_ARGS,  "GARBAGE_ARGS"  },
	{ SUNRPC_SYSTEM_ERR,    "SYSTEM_ERR"    },
	{ 0, NULL }
};

static void
print_nfsaddr(netdissect_options *ndo,
              const u_char *bp, const char *s, const char *d)
{
	const struct ip *ip;
	const struct ip6_hdr *ip6;
	char srcaddr[INET6_ADDRSTRLEN], dstaddr[INET6_ADDRSTRLEN];

	srcaddr[0] = dstaddr[0] = '\0';
	switch (IP_V((const struct ip *)bp)) {
	case 4:
		ip = (const struct ip *)bp;
		strlcpy(srcaddr, ipaddr_string(ndo, &ip->ip_src), sizeof(srcaddr));
		strlcpy(dstaddr, ipaddr_string(ndo, &ip->ip_dst), sizeof(dstaddr));
		break;
	case 6:
		ip6 = (const struct ip6_hdr *)bp;
		strlcpy(srcaddr, ip6addr_string(ndo, &ip6->ip6_src),
		    sizeof(srcaddr));
		strlcpy(dstaddr, ip6addr_string(ndo, &ip6->ip6_dst),
		    sizeof(dstaddr));
		break;
	default:
		strlcpy(srcaddr, "?", sizeof(srcaddr));
		strlcpy(dstaddr, "?", sizeof(dstaddr));
		break;
	}

	ND_PRINT((ndo, "%s.%s > %s.%s: ", srcaddr, s, dstaddr, d));
}

static const uint32_t *
parse_sattr3(netdissect_options *ndo,
             const uint32_t *dp, struct nfsv3_sattr *sa3)
{
	ND_TCHECK(dp[0]);
	sa3->sa_modeset = EXTRACT_32BITS(dp);
	dp++;
	if (sa3->sa_modeset) {
		ND_TCHECK(dp[0]);
		sa3->sa_mode = EXTRACT_32BITS(dp);
		dp++;
	}

	ND_TCHECK(dp[0]);
	sa3->sa_uidset = EXTRACT_32BITS(dp);
	dp++;
	if (sa3->sa_uidset) {
		ND_TCHECK(dp[0]);
		sa3->sa_uid = EXTRACT_32BITS(dp);
		dp++;
	}

	ND_TCHECK(dp[0]);
	sa3->sa_gidset = EXTRACT_32BITS(dp);
	dp++;
	if (sa3->sa_gidset) {
		ND_TCHECK(dp[0]);
		sa3->sa_gid = EXTRACT_32BITS(dp);
		dp++;
	}

	ND_TCHECK(dp[0]);
	sa3->sa_sizeset = EXTRACT_32BITS(dp);
	dp++;
	if (sa3->sa_sizeset) {
		ND_TCHECK(dp[0]);
		sa3->sa_size = EXTRACT_32BITS(dp);
		dp++;
	}

	ND_TCHECK(dp[0]);
	sa3->sa_atimetype = EXTRACT_32BITS(dp);
	dp++;
	if (sa3->sa_atimetype == NFSV3SATTRTIME_TOCLIENT) {
		ND_TCHECK(dp[1]);
		sa3->sa_atime.nfsv3_sec = EXTRACT_32BITS(dp);
		dp++;
		sa3->sa_atime.nfsv3_nsec = EXTRACT_32BITS(dp);
		dp++;
	}

	ND_TCHECK(dp[0]);
	sa3->sa_mtimetype = EXTRACT_32BITS(dp);
	dp++;
	if (sa3->sa_mtimetype == NFSV3SATTRTIME_TOCLIENT) {
		ND_TCHECK(dp[1]);
		sa3->sa_mtime.nfsv3_sec = EXTRACT_32BITS(dp);
		dp++;
		sa3->sa_mtime.nfsv3_nsec = EXTRACT_32BITS(dp);
		dp++;
	}

	return dp;
trunc:
	return NULL;
}

static int nfserr;		/* true if we error rather than trunc */

static void
print_sattr3(netdissect_options *ndo,
             const struct nfsv3_sattr *sa3, int verbose)
{
	if (sa3->sa_modeset)
		ND_PRINT((ndo, " mode %o", sa3->sa_mode));
	if (sa3->sa_uidset)
		ND_PRINT((ndo, " uid %u", sa3->sa_uid));
	if (sa3->sa_gidset)
		ND_PRINT((ndo, " gid %u", sa3->sa_gid));
	if (verbose > 1) {
		if (sa3->sa_atimetype == NFSV3SATTRTIME_TOCLIENT)
			ND_PRINT((ndo, " atime %u.%06u", sa3->sa_atime.nfsv3_sec,
			       sa3->sa_atime.nfsv3_nsec));
		if (sa3->sa_mtimetype == NFSV3SATTRTIME_TOCLIENT)
			ND_PRINT((ndo, " mtime %u.%06u", sa3->sa_mtime.nfsv3_sec,
			       sa3->sa_mtime.nfsv3_nsec));
	}
}

void
nfsreply_print(netdissect_options *ndo,
               register const u_char *bp, u_int length,
               register const u_char *bp2)
{
	register const struct sunrpc_msg *rp;
	char srcid[20], dstid[20];	/*fits 32bit*/

	nfserr = 0;		/* assume no error */
	rp = (const struct sunrpc_msg *)bp;

	ND_TCHECK(rp->rm_xid);
	if (!ndo->ndo_nflag) {
		strlcpy(srcid, "nfs", sizeof(srcid));
		snprintf(dstid, sizeof(dstid), "%u",
		    EXTRACT_32BITS(&rp->rm_xid));
	} else {
		snprintf(srcid, sizeof(srcid), "%u", NFS_PORT);
		snprintf(dstid, sizeof(dstid), "%u",
		    EXTRACT_32BITS(&rp->rm_xid));
	}
	print_nfsaddr(ndo, bp2, srcid, dstid);

	nfsreply_print_noaddr(ndo, bp, length, bp2);
	return;

trunc:
	if (!nfserr)
		ND_PRINT((ndo, "%s", tstr));
}

void
nfsreply_print_noaddr(netdissect_options *ndo,
                      register const u_char *bp, u_int length,
                      register const u_char *bp2)
{
	register const struct sunrpc_msg *rp;
	uint32_t proc, vers, reply_stat;
	enum sunrpc_reject_stat rstat;
	uint32_t rlow;
	uint32_t rhigh;
	enum sunrpc_auth_stat rwhy;

	nfserr = 0;		/* assume no error */
	rp = (const struct sunrpc_msg *)bp;

	ND_TCHECK(rp->rm_reply.rp_stat);
	reply_stat = EXTRACT_32BITS(&rp->rm_reply.rp_stat);
	switch (reply_stat) {

	case SUNRPC_MSG_ACCEPTED:
		ND_PRINT((ndo, "reply ok %u", length));
		if (xid_map_find(rp, bp2, &proc, &vers) >= 0)
			interp_reply(ndo, rp, proc, vers, length);
		break;

	case SUNRPC_MSG_DENIED:
		ND_PRINT((ndo, "reply ERR %u: ", length));
		ND_TCHECK(rp->rm_reply.rp_reject.rj_stat);
		rstat = EXTRACT_32BITS(&rp->rm_reply.rp_reject.rj_stat);
		switch (rstat) {

		case SUNRPC_RPC_MISMATCH:
			ND_TCHECK(rp->rm_reply.rp_reject.rj_vers.high);
			rlow = EXTRACT_32BITS(&rp->rm_reply.rp_reject.rj_vers.low);
			rhigh = EXTRACT_32BITS(&rp->rm_reply.rp_reject.rj_vers.high);
			ND_PRINT((ndo, "RPC Version mismatch (%u-%u)", rlow, rhigh));
			break;

		case SUNRPC_AUTH_ERROR:
			ND_TCHECK(rp->rm_reply.rp_reject.rj_why);
			rwhy = EXTRACT_32BITS(&rp->rm_reply.rp_reject.rj_why);
			ND_PRINT((ndo, "Auth %s", tok2str(sunrpc_auth_str, "Invalid failure code %u", rwhy)));
			break;

		default:
			ND_PRINT((ndo, "Unknown reason for rejecting rpc message %u", (unsigned int)rstat));
			break;
		}
		break;

	default:
		ND_PRINT((ndo, "reply Unknown rpc response code=%u %u", reply_stat, length));
		break;
	}
	return;

trunc:
	if (!nfserr)
		ND_PRINT((ndo, "%s", tstr));
}

/*
 * Return a pointer to the first file handle in the packet.
 * If the packet was truncated, return 0.
 */
static const uint32_t *
parsereq(netdissect_options *ndo,
         register const struct sunrpc_msg *rp, register u_int length)
{
	register const uint32_t *dp;
	register u_int len;

	/*
	 * find the start of the req data (if we captured it)
	 */
	dp = (const uint32_t *)&rp->rm_call.cb_cred;
	ND_TCHECK(dp[1]);
	len = EXTRACT_32BITS(&dp[1]);
	if (len < length) {
		dp += (len + (2 * sizeof(*dp) + 3)) / sizeof(*dp);
		ND_TCHECK(dp[1]);
		len = EXTRACT_32BITS(&dp[1]);
		if (len < length) {
			dp += (len + (2 * sizeof(*dp) + 3)) / sizeof(*dp);
			ND_TCHECK2(dp[0], 0);
			return (dp);
		}
	}
trunc:
	return (NULL);
}

/*
 * Print out an NFS file handle and return a pointer to following word.
 * If packet was truncated, return 0.
 */
static const uint32_t *
parsefh(netdissect_options *ndo,
        register const uint32_t *dp, int v3)
{
	u_int len;

	if (v3) {
		ND_TCHECK(dp[0]);
		len = EXTRACT_32BITS(dp) / 4;
		dp++;
	} else
		len = NFSX_V2FH / 4;

	if (ND_TTEST2(*dp, len * sizeof(*dp))) {
		nfs_printfh(ndo, dp, len);
		return (dp + len);
	}
trunc:
	return (NULL);
}

/*
 * Print out a file name and return pointer to 32-bit word past it.
 * If packet was truncated, return 0.
 */
static const uint32_t *
parsefn(netdissect_options *ndo,
        register const uint32_t *dp)
{
	register uint32_t len;
	register const u_char *cp;

	/* Bail if we don't have the string length */
	ND_TCHECK(*dp);

	/* Fetch string length; convert to host order */
	len = *dp++;
	NTOHL(len);

	ND_TCHECK2(*dp, ((len + 3) & ~3));

	cp = (const u_char *)dp;
	/* Update 32-bit pointer (NFS filenames padded to 32-bit boundaries) */
	dp += ((len + 3) & ~3) / sizeof(*dp);
	ND_PRINT((ndo, "\""));
	if (fn_printn(ndo, cp, len, ndo->ndo_snapend)) {
		ND_PRINT((ndo, "\""));
		goto trunc;
	}
	ND_PRINT((ndo, "\""));

	return (dp);
trunc:
	return NULL;
}

/*
 * Print out file handle and file name.
 * Return pointer to 32-bit word past file name.
 * If packet was truncated (or there was some other error), return 0.
 */
static const uint32_t *
parsefhn(netdissect_options *ndo,
         register const uint32_t *dp, int v3)
{
	dp = parsefh(ndo, dp, v3);
	if (dp == NULL)
		return (NULL);
	ND_PRINT((ndo, " "));
	return (parsefn(ndo, dp));
}

void
nfsreq_print_noaddr(netdissect_options *ndo,
                    register const u_char *bp, u_int length,
                    register const u_char *bp2)
{
	register const struct sunrpc_msg *rp;
	register const uint32_t *dp;
	nfs_type type;
	int v3;
	uint32_t proc;
	uint32_t access_flags;
	struct nfsv3_sattr sa3;

	ND_PRINT((ndo, "%d", length));
	nfserr = 0;		/* assume no error */
	rp = (const struct sunrpc_msg *)bp;

	if (!xid_map_enter(ndo, rp, bp2))	/* record proc number for later on */
		goto trunc;

	v3 = (EXTRACT_32BITS(&rp->rm_call.cb_vers) == NFS_VER3);
	proc = EXTRACT_32BITS(&rp->rm_call.cb_proc);

	if (!v3 && proc < NFS_NPROCS)
		proc =  nfsv3_procid[proc];

	ND_PRINT((ndo, " %s", tok2str(nfsproc_str, "proc-%u", proc)));
	switch (proc) {

	case NFSPROC_GETATTR:
	case NFSPROC_SETATTR:
	case NFSPROC_READLINK:
	case NFSPROC_FSSTAT:
	case NFSPROC_FSINFO:
	case NFSPROC_PATHCONF:
		if ((dp = parsereq(ndo, rp, length)) != NULL &&
		    parsefh(ndo, dp, v3) != NULL)
			return;
		break;

	case NFSPROC_LOOKUP:
	case NFSPROC_CREATE:
	case NFSPROC_MKDIR:
	case NFSPROC_REMOVE:
	case NFSPROC_RMDIR:
		if ((dp = parsereq(ndo, rp, length)) != NULL &&
		    parsefhn(ndo, dp, v3) != NULL)
			return;
		break;

	case NFSPROC_ACCESS:
		if ((dp = parsereq(ndo, rp, length)) != NULL &&
		    (dp = parsefh(ndo, dp, v3)) != NULL) {
			ND_TCHECK(dp[0]);
			access_flags = EXTRACT_32BITS(&dp[0]);
			if (access_flags & ~NFSV3ACCESS_FULL) {
				/* NFSV3ACCESS definitions aren't up to date */
				ND_PRINT((ndo, " %04x", access_flags));
			} else if ((access_flags & NFSV3ACCESS_FULL) == NFSV3ACCESS_FULL) {
				ND_PRINT((ndo, " NFS_ACCESS_FULL"));
			} else {
				char separator = ' ';
				if (access_flags & NFSV3ACCESS_READ) {
					ND_PRINT((ndo, " NFS_ACCESS_READ"));
					separator = '|';
				}
				if (access_flags & NFSV3ACCESS_LOOKUP) {
					ND_PRINT((ndo, "%cNFS_ACCESS_LOOKUP", separator));
					separator = '|';
				}
				if (access_flags & NFSV3ACCESS_MODIFY) {
					ND_PRINT((ndo, "%cNFS_ACCESS_MODIFY", separator));
					separator = '|';
				}
				if (access_flags & NFSV3ACCESS_EXTEND) {
					ND_PRINT((ndo, "%cNFS_ACCESS_EXTEND", separator));
					separator = '|';
				}
				if (access_flags & NFSV3ACCESS_DELETE) {
					ND_PRINT((ndo, "%cNFS_ACCESS_DELETE", separator));
					separator = '|';
				}
				if (access_flags & NFSV3ACCESS_EXECUTE)
					ND_PRINT((ndo, "%cNFS_ACCESS_EXECUTE", separator));
			}
			return;
		}
		break;

	case NFSPROC_READ:
		if ((dp = parsereq(ndo, rp, length)) != NULL &&
		    (dp = parsefh(ndo, dp, v3)) != NULL) {
			if (v3) {
				ND_TCHECK(dp[2]);
				ND_PRINT((ndo, " %u bytes @ %" PRIu64,
				       EXTRACT_32BITS(&dp[2]),
				       EXTRACT_64BITS(&dp[0])));
			} else {
				ND_TCHECK(dp[1]);
				ND_PRINT((ndo, " %u bytes @ %u",
				    EXTRACT_32BITS(&dp[1]),
				    EXTRACT_32BITS(&dp[0])));
			}
			return;
		}
		break;

	case NFSPROC_WRITE:
		if ((dp = parsereq(ndo, rp, length)) != NULL &&
		    (dp = parsefh(ndo, dp, v3)) != NULL) {
			if (v3) {
				ND_TCHECK(dp[4]);
				ND_PRINT((ndo, " %u (%u) bytes @ %" PRIu64,
						EXTRACT_32BITS(&dp[4]),
						EXTRACT_32BITS(&dp[2]),
						EXTRACT_64BITS(&dp[0])));
				if (ndo->ndo_vflag) {
					ND_PRINT((ndo, " <%s>",
						tok2str(nfsv3_writemodes,
							NULL, EXTRACT_32BITS(&dp[3]))));
				}
			} else {
				ND_TCHECK(dp[3]);
				ND_PRINT((ndo, " %u (%u) bytes @ %u (%u)",
						EXTRACT_32BITS(&dp[3]),
						EXTRACT_32BITS(&dp[2]),
						EXTRACT_32BITS(&dp[1]),
						EXTRACT_32BITS(&dp[0])));
			}
			return;
		}
		break;

	case NFSPROC_SYMLINK:
		if ((dp = parsereq(ndo, rp, length)) != NULL &&
		    (dp = parsefhn(ndo, dp, v3)) != NULL) {
			ND_PRINT((ndo, " ->"));
			if (v3 && (dp = parse_sattr3(ndo, dp, &sa3)) == NULL)
				break;
			if (parsefn(ndo, dp) == NULL)
				break;
			if (v3 && ndo->ndo_vflag)
				print_sattr3(ndo, &sa3, ndo->ndo_vflag);
			return;
		}
		break;

	case NFSPROC_MKNOD:
		if ((dp = parsereq(ndo, rp, length)) != NULL &&
		    (dp = parsefhn(ndo, dp, v3)) != NULL) {
			ND_TCHECK(*dp);
			type = (nfs_type)EXTRACT_32BITS(dp);
			dp++;
			if ((dp = parse_sattr3(ndo, dp, &sa3)) == NULL)
				break;
			ND_PRINT((ndo, " %s", tok2str(type2str, "unk-ft %d", type)));
			if (ndo->ndo_vflag && (type == NFCHR || type == NFBLK)) {
				ND_TCHECK(dp[1]);
				ND_PRINT((ndo, " %u/%u",
				       EXTRACT_32BITS(&dp[0]),
				       EXTRACT_32BITS(&dp[1])));
				dp += 2;
			}
			if (ndo->ndo_vflag)
				print_sattr3(ndo, &sa3, ndo->ndo_vflag);
			return;
		}
		break;

	case NFSPROC_RENAME:
		if ((dp = parsereq(ndo, rp, length)) != NULL &&
		    (dp = parsefhn(ndo, dp, v3)) != NULL) {
			ND_PRINT((ndo, " ->"));
			if (parsefhn(ndo, dp, v3) != NULL)
				return;
		}
		break;

	case NFSPROC_LINK:
		if ((dp = parsereq(ndo, rp, length)) != NULL &&
		    (dp = parsefh(ndo, dp, v3)) != NULL) {
			ND_PRINT((ndo, " ->"));
			if (parsefhn(ndo, dp, v3) != NULL)
				return;
		}
		break;

	case NFSPROC_READDIR:
		if ((dp = parsereq(ndo, rp, length)) != NULL &&
		    (dp = parsefh(ndo, dp, v3)) != NULL) {
			if (v3) {
				ND_TCHECK(dp[4]);
				/*
				 * We shouldn't really try to interpret the
				 * offset cookie here.
				 */
				ND_PRINT((ndo, " %u bytes @ %" PRId64,
				    EXTRACT_32BITS(&dp[4]),
				    EXTRACT_64BITS(&dp[0])));
				if (ndo->ndo_vflag)
					ND_PRINT((ndo, " verf %08x%08x", dp[2], dp[3]));
			} else {
				ND_TCHECK(dp[1]);
				/*
				 * Print the offset as signed, since -1 is
				 * common, but offsets > 2^31 aren't.
				 */
				ND_PRINT((ndo, " %u bytes @ %d",
				    EXTRACT_32BITS(&dp[1]),
				    EXTRACT_32BITS(&dp[0])));
			}
			return;
		}
		break;

	case NFSPROC_READDIRPLUS:
		if ((dp = parsereq(ndo, rp, length)) != NULL &&
		    (dp = parsefh(ndo, dp, v3)) != NULL) {
			ND_TCHECK(dp[4]);
			/*
			 * We don't try to interpret the offset
			 * cookie here.
			 */
			ND_PRINT((ndo, " %u bytes @ %" PRId64,
				EXTRACT_32BITS(&dp[4]),
				EXTRACT_64BITS(&dp[0])));
			if (ndo->ndo_vflag) {
				ND_TCHECK(dp[5]);
				ND_PRINT((ndo, " max %u verf %08x%08x",
				       EXTRACT_32BITS(&dp[5]), dp[2], dp[3]));
			}
			return;
		}
		break;

	case NFSPROC_COMMIT:
		if ((dp = parsereq(ndo, rp, length)) != NULL &&
		    (dp = parsefh(ndo, dp, v3)) != NULL) {
			ND_TCHECK(dp[2]);
			ND_PRINT((ndo, " %u bytes @ %" PRIu64,
				EXTRACT_32BITS(&dp[2]),
				EXTRACT_64BITS(&dp[0])));
			return;
		}
		break;

	default:
		return;
	}

trunc:
	if (!nfserr)
		ND_PRINT((ndo, "%s", tstr));
}

/*
 * Print out an NFS file handle.
 * We assume packet was not truncated before the end of the
 * file handle pointed to by dp.
 *
 * Note: new version (using portable file-handle parser) doesn't produce
 * generation number.  It probably could be made to do that, with some
 * additional hacking on the parser code.
 */
static void
nfs_printfh(netdissect_options *ndo,
            register const uint32_t *dp, const u_int len)
{
	my_fsid fsid;
	uint32_t ino;
	const char *sfsname = NULL;
	char *spacep;

	if (ndo->ndo_uflag) {
		u_int i;
		char const *sep = "";

		ND_PRINT((ndo, " fh["));
		for (i=0; i<len; i++) {
			ND_PRINT((ndo, "%s%x", sep, dp[i]));
			sep = ":";
		}
		ND_PRINT((ndo, "]"));
		return;
	}

	Parse_fh((const u_char *)dp, len, &fsid, &ino, NULL, &sfsname, 0);

	if (sfsname) {
		/* file system ID is ASCII, not numeric, for this server OS */
		char temp[NFSX_V3FHMAX+1];
		u_int stringlen;

		/* Make sure string is null-terminated */
		stringlen = len;
		if (stringlen > NFSX_V3FHMAX)
			stringlen = NFSX_V3FHMAX;
		strncpy(temp, sfsname, stringlen);
		temp[stringlen] = '\0';
		/* Remove trailing spaces */
		spacep = strchr(temp, ' ');
		if (spacep)
			*spacep = '\0';

		ND_PRINT((ndo, " fh %s/", temp));
	} else {
		ND_PRINT((ndo, " fh %d,%d/",
			     fsid.Fsid_dev.Major, fsid.Fsid_dev.Minor));
	}

	if(fsid.Fsid_dev.Minor == 257)
		/* Print the undecoded handle */
		ND_PRINT((ndo, "%s", fsid.Opaque_Handle));
	else
		ND_PRINT((ndo, "%ld", (long) ino));
}

/*
 * Maintain a small cache of recent client.XID.server/proc pairs, to allow
 * us to match up replies with requests and thus to know how to parse
 * the reply.
 */

struct xid_map_entry {
	uint32_t	xid;		/* transaction ID (net order) */
	int ipver;			/* IP version (4 or 6) */
	struct in6_addr	client;		/* client IP address (net order) */
	struct in6_addr	server;		/* server IP address (net order) */
	uint32_t	proc;		/* call proc number (host order) */
	uint32_t	vers;		/* program version (host order) */
};

/*
 * Map entries are kept in an array that we manage as a ring;
 * new entries are always added at the tail of the ring.  Initially,
 * all the entries are zero and hence don't match anything.
 */

#define	XIDMAPSIZE	64

static struct xid_map_entry xid_map[XIDMAPSIZE];

static int xid_map_next = 0;
static int xid_map_hint = 0;

static int
xid_map_enter(netdissect_options *ndo,
              const struct sunrpc_msg *rp, const u_char *bp)
{
	const struct ip *ip = NULL;
	const struct ip6_hdr *ip6 = NULL;
	struct xid_map_entry *xmep;

	if (!ND_TTEST(rp->rm_call.cb_proc))
		return (0);
	switch (IP_V((const struct ip *)bp)) {
	case 4:
		ip = (const struct ip *)bp;
		break;
	case 6:
		ip6 = (const struct ip6_hdr *)bp;
		break;
	default:
		return (1);
	}

	xmep = &xid_map[xid_map_next];

	if (++xid_map_next >= XIDMAPSIZE)
		xid_map_next = 0;

	UNALIGNED_MEMCPY(&xmep->xid, &rp->rm_xid, sizeof(xmep->xid));
	if (ip) {
		xmep->ipver = 4;
		UNALIGNED_MEMCPY(&xmep->client, &ip->ip_src, sizeof(ip->ip_src));
		UNALIGNED_MEMCPY(&xmep->server, &ip->ip_dst, sizeof(ip->ip_dst));
	}
	else if (ip6) {
		xmep->ipver = 6;
		UNALIGNED_MEMCPY(&xmep->client, &ip6->ip6_src, sizeof(ip6->ip6_src));
		UNALIGNED_MEMCPY(&xmep->server, &ip6->ip6_dst, sizeof(ip6->ip6_dst));
	}
	xmep->proc = EXTRACT_32BITS(&rp->rm_call.cb_proc);
	xmep->vers = EXTRACT_32BITS(&rp->rm_call.cb_vers);
	return (1);
}

/*
 * Returns 0 and puts NFSPROC_xxx in proc return and
 * version in vers return, or returns -1 on failure
 */
static int
xid_map_find(const struct sunrpc_msg *rp, const u_char *bp, uint32_t *proc,
	     uint32_t *vers)
{
	int i;
	struct xid_map_entry *xmep;
	uint32_t xid;
	const struct ip *ip = (const struct ip *)bp;
	const struct ip6_hdr *ip6 = (const struct ip6_hdr *)bp;
	int cmp;

	UNALIGNED_MEMCPY(&xid, &rp->rm_xid, sizeof(xmep->xid));
	/* Start searching from where we last left off */
	i = xid_map_hint;
	do {
		xmep = &xid_map[i];
		cmp = 1;
		if (xmep->ipver != IP_V(ip) || xmep->xid != xid)
			goto nextitem;
		switch (xmep->ipver) {
		case 4:
			if (UNALIGNED_MEMCMP(&ip->ip_src, &xmep->server,
				   sizeof(ip->ip_src)) != 0 ||
			    UNALIGNED_MEMCMP(&ip->ip_dst, &xmep->client,
				   sizeof(ip->ip_dst)) != 0) {
				cmp = 0;
			}
			break;
		case 6:
			if (UNALIGNED_MEMCMP(&ip6->ip6_src, &xmep->server,
				   sizeof(ip6->ip6_src)) != 0 ||
			    UNALIGNED_MEMCMP(&ip6->ip6_dst, &xmep->client,
				   sizeof(ip6->ip6_dst)) != 0) {
				cmp = 0;
			}
			break;
		default:
			cmp = 0;
			break;
		}
		if (cmp) {
			/* match */
			xid_map_hint = i;
			*proc = xmep->proc;
			*vers = xmep->vers;
			return 0;
		}
	nextitem:
		if (++i >= XIDMAPSIZE)
			i = 0;
	} while (i != xid_map_hint);

	/* search failed */
	return (-1);
}

/*
 * Routines for parsing reply packets
 */

/*
 * Return a pointer to the beginning of the actual results.
 * If the packet was truncated, return 0.
 */
static const uint32_t *
parserep(netdissect_options *ndo,
         register const struct sunrpc_msg *rp, register u_int length)
{
	register const uint32_t *dp;
	u_int len;
	enum sunrpc_accept_stat astat;

	/*
	 * Portability note:
	 * Here we find the address of the ar_verf credentials.
	 * Originally, this calculation was
	 *	dp = (uint32_t *)&rp->rm_reply.rp_acpt.ar_verf
	 * On the wire, the rp_acpt field starts immediately after
	 * the (32 bit) rp_stat field.  However, rp_acpt (which is a
	 * "struct accepted_reply") contains a "struct opaque_auth",
	 * whose internal representation contains a pointer, so on a
	 * 64-bit machine the compiler inserts 32 bits of padding
	 * before rp->rm_reply.rp_acpt.ar_verf.  So, we cannot use
	 * the internal representation to parse the on-the-wire
	 * representation.  Instead, we skip past the rp_stat field,
	 * which is an "enum" and so occupies one 32-bit word.
	 */
	dp = ((const uint32_t *)&rp->rm_reply) + 1;
	ND_TCHECK(dp[1]);
	len = EXTRACT_32BITS(&dp[1]);
	if (len >= length)
		return (NULL);
	/*
	 * skip past the ar_verf credentials.
	 */
	dp += (len + (2*sizeof(uint32_t) + 3)) / sizeof(uint32_t);

	/*
	 * now we can check the ar_stat field
	 */
	ND_TCHECK(dp[0]);
	astat = (enum sunrpc_accept_stat) EXTRACT_32BITS(dp);
	if (astat != SUNRPC_SUCCESS) {
		ND_PRINT((ndo, " %s", tok2str(sunrpc_str, "ar_stat %d", astat)));
		nfserr = 1;		/* suppress trunc string */
		return (NULL);
	}
	/* successful return */
	ND_TCHECK2(*dp, sizeof(astat));
	return ((const uint32_t *) (sizeof(astat) + ((const char *)dp)));
trunc:
	return (0);
}

static const uint32_t *
parsestatus(netdissect_options *ndo,
            const uint32_t *dp, int *er)
{
	int errnum;

	ND_TCHECK(dp[0]);

	errnum = EXTRACT_32BITS(&dp[0]);
	if (er)
		*er = errnum;
	if (errnum != 0) {
		if (!ndo->ndo_qflag)
			ND_PRINT((ndo, " ERROR: %s",
			    tok2str(status2str, "unk %d", errnum)));
		nfserr = 1;
	}
	return (dp + 1);
trunc:
	return NULL;
}

static const uint32_t *
parsefattr(netdissect_options *ndo,
           const uint32_t *dp, int verbose, int v3)
{
	const struct nfs_fattr *fap;

	fap = (const struct nfs_fattr *)dp;
	ND_TCHECK(fap->fa_gid);
	if (verbose) {
		ND_PRINT((ndo, " %s %o ids %d/%d",
		    tok2str(type2str, "unk-ft %d ",
		    EXTRACT_32BITS(&fap->fa_type)),
		    EXTRACT_32BITS(&fap->fa_mode),
		    EXTRACT_32BITS(&fap->fa_uid),
		    EXTRACT_32BITS(&fap->fa_gid)));
		if (v3) {
			ND_TCHECK(fap->fa3_size);
			ND_PRINT((ndo, " sz %" PRIu64,
				EXTRACT_64BITS((const uint32_t *)&fap->fa3_size)));
		} else {
			ND_TCHECK(fap->fa2_size);
			ND_PRINT((ndo, " sz %d", EXTRACT_32BITS(&fap->fa2_size)));
		}
	}
	/* print lots more stuff */
	if (verbose > 1) {
		if (v3) {
			ND_TCHECK(fap->fa3_ctime);
			ND_PRINT((ndo, " nlink %d rdev %d/%d",
			       EXTRACT_32BITS(&fap->fa_nlink),
			       EXTRACT_32BITS(&fap->fa3_rdev.specdata1),
			       EXTRACT_32BITS(&fap->fa3_rdev.specdata2)));
			ND_PRINT((ndo, " fsid %" PRIx64,
				EXTRACT_64BITS((const uint32_t *)&fap->fa3_fsid)));
			ND_PRINT((ndo, " fileid %" PRIx64,
				EXTRACT_64BITS((const uint32_t *)&fap->fa3_fileid)));
			ND_PRINT((ndo, " a/m/ctime %u.%06u",
			       EXTRACT_32BITS(&fap->fa3_atime.nfsv3_sec),
			       EXTRACT_32BITS(&fap->fa3_atime.nfsv3_nsec)));
			ND_PRINT((ndo, " %u.%06u",
			       EXTRACT_32BITS(&fap->fa3_mtime.nfsv3_sec),
			       EXTRACT_32BITS(&fap->fa3_mtime.nfsv3_nsec)));
			ND_PRINT((ndo, " %u.%06u",
			       EXTRACT_32BITS(&fap->fa3_ctime.nfsv3_sec),
			       EXTRACT_32BITS(&fap->fa3_ctime.nfsv3_nsec)));
		} else {
			ND_TCHECK(fap->fa2_ctime);
			ND_PRINT((ndo, " nlink %d rdev 0x%x fsid 0x%x nodeid 0x%x a/m/ctime",
			       EXTRACT_32BITS(&fap->fa_nlink),
			       EXTRACT_32BITS(&fap->fa2_rdev),
			       EXTRACT_32BITS(&fap->fa2_fsid),
			       EXTRACT_32BITS(&fap->fa2_fileid)));
			ND_PRINT((ndo, " %u.%06u",
			       EXTRACT_32BITS(&fap->fa2_atime.nfsv2_sec),
			       EXTRACT_32BITS(&fap->fa2_atime.nfsv2_usec)));
			ND_PRINT((ndo, " %u.%06u",
			       EXTRACT_32BITS(&fap->fa2_mtime.nfsv2_sec),
			       EXTRACT_32BITS(&fap->fa2_mtime.nfsv2_usec)));
			ND_PRINT((ndo, " %u.%06u",
			       EXTRACT_32BITS(&fap->fa2_ctime.nfsv2_sec),
			       EXTRACT_32BITS(&fap->fa2_ctime.nfsv2_usec)));
		}
	}
	return ((const uint32_t *)((const unsigned char *)dp +
		(v3 ? NFSX_V3FATTR : NFSX_V2FATTR)));
trunc:
	return (NULL);
}

static int
parseattrstat(netdissect_options *ndo,
              const uint32_t *dp, int verbose, int v3)
{
	int er;

	dp = parsestatus(ndo, dp, &er);
	if (dp == NULL)
		return (0);
	if (er)
		return (1);

	return (parsefattr(ndo, dp, verbose, v3) != NULL);
}

static int
parsediropres(netdissect_options *ndo,
              const uint32_t *dp)
{
	int er;

	if (!(dp = parsestatus(ndo, dp, &er)))
		return (0);
	if (er)
		return (1);

	dp = parsefh(ndo, dp, 0);
	if (dp == NULL)
		return (0);

	return (parsefattr(ndo, dp, ndo->ndo_vflag, 0) != NULL);
}

static int
parselinkres(netdissect_options *ndo,
             const uint32_t *dp, int v3)
{
	int er;

	dp = parsestatus(ndo, dp, &er);
	if (dp == NULL)
		return(0);
	if (er)
		return(1);
	if (v3 && !(dp = parse_post_op_attr(ndo, dp, ndo->ndo_vflag)))
		return (0);
	ND_PRINT((ndo, " "));
	return (parsefn(ndo, dp) != NULL);
}

static int
parsestatfs(netdissect_options *ndo,
            const uint32_t *dp, int v3)
{
	const struct nfs_statfs *sfsp;
	int er;

	dp = parsestatus(ndo, dp, &er);
	if (dp == NULL)
		return (0);
	if (!v3 && er)
		return (1);

	if (ndo->ndo_qflag)
		return(1);

	if (v3) {
		if (ndo->ndo_vflag)
			ND_PRINT((ndo, " POST:"));
		if (!(dp = parse_post_op_attr(ndo, dp, ndo->ndo_vflag)))
			return (0);
	}

	ND_TCHECK2(*dp, (v3 ? NFSX_V3STATFS : NFSX_V2STATFS));

	sfsp = (const struct nfs_statfs *)dp;

	if (v3) {
		ND_PRINT((ndo, " tbytes %" PRIu64 " fbytes %" PRIu64 " abytes %" PRIu64,
			EXTRACT_64BITS((const uint32_t *)&sfsp->sf_tbytes),
			EXTRACT_64BITS((const uint32_t *)&sfsp->sf_fbytes),
			EXTRACT_64BITS((const uint32_t *)&sfsp->sf_abytes)));
		if (ndo->ndo_vflag) {
			ND_PRINT((ndo, " tfiles %" PRIu64 " ffiles %" PRIu64 " afiles %" PRIu64 " invar %u",
			       EXTRACT_64BITS((const uint32_t *)&sfsp->sf_tfiles),
			       EXTRACT_64BITS((const uint32_t *)&sfsp->sf_ffiles),
			       EXTRACT_64BITS((const uint32_t *)&sfsp->sf_afiles),
			       EXTRACT_32BITS(&sfsp->sf_invarsec)));
		}
	} else {
		ND_PRINT((ndo, " tsize %d bsize %d blocks %d bfree %d bavail %d",
			EXTRACT_32BITS(&sfsp->sf_tsize),
			EXTRACT_32BITS(&sfsp->sf_bsize),
			EXTRACT_32BITS(&sfsp->sf_blocks),
			EXTRACT_32BITS(&sfsp->sf_bfree),
			EXTRACT_32BITS(&sfsp->sf_bavail)));
	}

	return (1);
trunc:
	return (0);
}

static int
parserddires(netdissect_options *ndo,
             const uint32_t *dp)
{
	int er;

	dp = parsestatus(ndo, dp, &er);
	if (dp == NULL)
		return (0);
	if (er)
		return (1);
	if (ndo->ndo_qflag)
		return (1);

	ND_TCHECK(dp[2]);
	ND_PRINT((ndo, " offset 0x%x size %d ",
	       EXTRACT_32BITS(&dp[0]), EXTRACT_32BITS(&dp[1])));
	if (dp[2] != 0)
		ND_PRINT((ndo, " eof"));

	return (1);
trunc:
	return (0);
}

static const uint32_t *
parse_wcc_attr(netdissect_options *ndo,
               const uint32_t *dp)
{
	/* Our caller has already checked this */
	ND_PRINT((ndo, " sz %" PRIu64, EXTRACT_64BITS(&dp[0])));
	ND_PRINT((ndo, " mtime %u.%06u ctime %u.%06u",
	       EXTRACT_32BITS(&dp[2]), EXTRACT_32BITS(&dp[3]),
	       EXTRACT_32BITS(&dp[4]), EXTRACT_32BITS(&dp[5])));
	return (dp + 6);
}

/*
 * Pre operation attributes. Print only if vflag > 1.
 */
static const uint32_t *
parse_pre_op_attr(netdissect_options *ndo,
                  const uint32_t *dp, int verbose)
{
	ND_TCHECK(dp[0]);
	if (!EXTRACT_32BITS(&dp[0]))
		return (dp + 1);
	dp++;
	ND_TCHECK2(*dp, 24);
	if (verbose > 1) {
		return parse_wcc_attr(ndo, dp);
	} else {
		/* If not verbose enough, just skip over wcc_attr */
		return (dp + 6);
	}
trunc:
	return (NULL);
}

/*
 * Post operation attributes are printed if vflag >= 1
 */
static const uint32_t *
parse_post_op_attr(netdissect_options *ndo,
                   const uint32_t *dp, int verbose)
{
	ND_TCHECK(dp[0]);
	if (!EXTRACT_32BITS(&dp[0]))
		return (dp + 1);
	dp++;
	if (verbose) {
		return parsefattr(ndo, dp, verbose, 1);
	} else
		return (dp + (NFSX_V3FATTR / sizeof (uint32_t)));
trunc:
	return (NULL);
}

static const uint32_t *
parse_wcc_data(netdissect_options *ndo,
               const uint32_t *dp, int verbose)
{
	if (verbose > 1)
		ND_PRINT((ndo, " PRE:"));
	if (!(dp = parse_pre_op_attr(ndo, dp, verbose)))
		return (0);

	if (verbose)
		ND_PRINT((ndo, " POST:"));
	return parse_post_op_attr(ndo, dp, verbose);
}

static const uint32_t *
parsecreateopres(netdissect_options *ndo,
                 const uint32_t *dp, int verbose)
{
	int er;

	if (!(dp = parsestatus(ndo, dp, &er)))
		return (0);
	if (er)
		dp = parse_wcc_data(ndo, dp, verbose);
	else {
		ND_TCHECK(dp[0]);
		if (!EXTRACT_32BITS(&dp[0]))
			return (dp + 1);
		dp++;
		if (!(dp = parsefh(ndo, dp, 1)))
			return (0);
		if (verbose) {
			if (!(dp = parse_post_op_attr(ndo, dp, verbose)))
				return (0);
			if (ndo->ndo_vflag > 1) {
				ND_PRINT((ndo, " dir attr:"));
				dp = parse_wcc_data(ndo, dp, verbose);
			}
		}
	}
	return (dp);
trunc:
	return (NULL);
}

static int
parsewccres(netdissect_options *ndo,
            const uint32_t *dp, int verbose)
{
	int er;

	if (!(dp = parsestatus(ndo, dp, &er)))
		return (0);
	return parse_wcc_data(ndo, dp, verbose) != NULL;
}

static const uint32_t *
parsev3rddirres(netdissect_options *ndo,
                const uint32_t *dp, int verbose)
{
	int er;

	if (!(dp = parsestatus(ndo, dp, &er)))
		return (0);
	if (ndo->ndo_vflag)
		ND_PRINT((ndo, " POST:"));
	if (!(dp = parse_post_op_attr(ndo, dp, verbose)))
		return (0);
	if (er)
		return dp;
	if (ndo->ndo_vflag) {
		ND_TCHECK(dp[1]);
		ND_PRINT((ndo, " verf %08x%08x", dp[0], dp[1]));
		dp += 2;
	}
	return dp;
trunc:
	return (NULL);
}

static int
parsefsinfo(netdissect_options *ndo,
            const uint32_t *dp)
{
	const struct nfsv3_fsinfo *sfp;
	int er;

	if (!(dp = parsestatus(ndo, dp, &er)))
		return (0);
	if (ndo->ndo_vflag)
		ND_PRINT((ndo, " POST:"));
	if (!(dp = parse_post_op_attr(ndo, dp, ndo->ndo_vflag)))
		return (0);
	if (er)
		return (1);

	sfp = (const struct nfsv3_fsinfo *)dp;
	ND_TCHECK(*sfp);
	ND_PRINT((ndo, " rtmax %u rtpref %u wtmax %u wtpref %u dtpref %u",
	       EXTRACT_32BITS(&sfp->fs_rtmax),
	       EXTRACT_32BITS(&sfp->fs_rtpref),
	       EXTRACT_32BITS(&sfp->fs_wtmax),
	       EXTRACT_32BITS(&sfp->fs_wtpref),
	       EXTRACT_32BITS(&sfp->fs_dtpref)));
	if (ndo->ndo_vflag) {
		ND_PRINT((ndo, " rtmult %u wtmult %u maxfsz %" PRIu64,
		       EXTRACT_32BITS(&sfp->fs_rtmult),
		       EXTRACT_32BITS(&sfp->fs_wtmult),
		       EXTRACT_64BITS((const uint32_t *)&sfp->fs_maxfilesize)));
		ND_PRINT((ndo, " delta %u.%06u ",
		       EXTRACT_32BITS(&sfp->fs_timedelta.nfsv3_sec),
		       EXTRACT_32BITS(&sfp->fs_timedelta.nfsv3_nsec)));
	}
	return (1);
trunc:
	return (0);
}

static int
parsepathconf(netdissect_options *ndo,
              const uint32_t *dp)
{
	int er;
	const struct nfsv3_pathconf *spp;

	if (!(dp = parsestatus(ndo, dp, &er)))
		return (0);
	if (ndo->ndo_vflag)
		ND_PRINT((ndo, " POST:"));
	if (!(dp = parse_post_op_attr(ndo, dp, ndo->ndo_vflag)))
		return (0);
	if (er)
		return (1);

	spp = (const struct nfsv3_pathconf *)dp;
	ND_TCHECK(*spp);

	ND_PRINT((ndo, " linkmax %u namemax %u %s %s %s %s",
	       EXTRACT_32BITS(&spp->pc_linkmax),
	       EXTRACT_32BITS(&spp->pc_namemax),
	       EXTRACT_32BITS(&spp->pc_notrunc) ? "notrunc" : "",
	       EXTRACT_32BITS(&spp->pc_chownrestricted) ? "chownres" : "",
	       EXTRACT_32BITS(&spp->pc_caseinsensitive) ? "igncase" : "",
	       EXTRACT_32BITS(&spp->pc_casepreserving) ? "keepcase" : ""));
	return (1);
trunc:
	return (0);
}

static void
interp_reply(netdissect_options *ndo,
             const struct sunrpc_msg *rp, uint32_t proc, uint32_t vers, int length)
{
	register const uint32_t *dp;
	register int v3;
	int er;

	v3 = (vers == NFS_VER3);

	if (!v3 && proc < NFS_NPROCS)
		proc = nfsv3_procid[proc];

	ND_PRINT((ndo, " %s", tok2str(nfsproc_str, "proc-%u", proc)));
	switch (proc) {

	case NFSPROC_GETATTR:
		dp = parserep(ndo, rp, length);
		if (dp != NULL && parseattrstat(ndo, dp, !ndo->ndo_qflag, v3) != 0)
			return;
		break;

	case NFSPROC_SETATTR:
		if (!(dp = parserep(ndo, rp, length)))
			return;
		if (v3) {
			if (parsewccres(ndo, dp, ndo->ndo_vflag))
				return;
		} else {
			if (parseattrstat(ndo, dp, !ndo->ndo_qflag, 0) != 0)
				return;
		}
		break;

	case NFSPROC_LOOKUP:
		if (!(dp = parserep(ndo, rp, length)))
			break;
		if (v3) {
			if (!(dp = parsestatus(ndo, dp, &er)))
				break;
			if (er) {
				if (ndo->ndo_vflag > 1) {
					ND_PRINT((ndo, " post dattr:"));
					dp = parse_post_op_attr(ndo, dp, ndo->ndo_vflag);
				}
			} else {
				if (!(dp = parsefh(ndo, dp, v3)))
					break;
				if ((dp = parse_post_op_attr(ndo, dp, ndo->ndo_vflag)) &&
				    ndo->ndo_vflag > 1) {
					ND_PRINT((ndo, " post dattr:"));
					dp = parse_post_op_attr(ndo, dp, ndo->ndo_vflag);
				}
			}
			if (dp)
				return;
		} else {
			if (parsediropres(ndo, dp) != 0)
				return;
		}
		break;

	case NFSPROC_ACCESS:
		if (!(dp = parserep(ndo, rp, length)))
			break;
		if (!(dp = parsestatus(ndo, dp, &er)))
			break;
		if (ndo->ndo_vflag)
			ND_PRINT((ndo, " attr:"));
		if (!(dp = parse_post_op_attr(ndo, dp, ndo->ndo_vflag)))
			break;
		if (!er) {
			ND_TCHECK(dp[0]);
			ND_PRINT((ndo, " c %04x", EXTRACT_32BITS(&dp[0])));
		}
		return;

	case NFSPROC_READLINK:
		dp = parserep(ndo, rp, length);
		if (dp != NULL && parselinkres(ndo, dp, v3) != 0)
			return;
		break;

	case NFSPROC_READ:
		if (!(dp = parserep(ndo, rp, length)))
			break;
		if (v3) {
			if (!(dp = parsestatus(ndo, dp, &er)))
				break;
			if (!(dp = parse_post_op_attr(ndo, dp, ndo->ndo_vflag)))
				break;
			if (er)
				return;
			if (ndo->ndo_vflag) {
				ND_TCHECK(dp[1]);
				ND_PRINT((ndo, " %u bytes", EXTRACT_32BITS(&dp[0])));
				if (EXTRACT_32BITS(&dp[1]))
					ND_PRINT((ndo, " EOF"));
			}
			return;
		} else {
			if (parseattrstat(ndo, dp, ndo->ndo_vflag, 0) != 0)
				return;
		}
		break;

	case NFSPROC_WRITE:
		if (!(dp = parserep(ndo, rp, length)))
			break;
		if (v3) {
			if (!(dp = parsestatus(ndo, dp, &er)))
				break;
			if (!(dp = parse_wcc_data(ndo, dp, ndo->ndo_vflag)))
				break;
			if (er)
				return;
			if (ndo->ndo_vflag) {
				ND_TCHECK(dp[0]);
				ND_PRINT((ndo, " %u bytes", EXTRACT_32BITS(&dp[0])));
				if (ndo->ndo_vflag > 1) {
					ND_TCHECK(dp[1]);
					ND_PRINT((ndo, " <%s>",
						tok2str(nfsv3_writemodes,
							NULL, EXTRACT_32BITS(&dp[1]))));
				}
				return;
			}
		} else {
			if (parseattrstat(ndo, dp, ndo->ndo_vflag, v3) != 0)
				return;
		}
		break;

	case NFSPROC_CREATE:
	case NFSPROC_MKDIR:
		if (!(dp = parserep(ndo, rp, length)))
			break;
		if (v3) {
			if (parsecreateopres(ndo, dp, ndo->ndo_vflag) != NULL)
				return;
		} else {
			if (parsediropres(ndo, dp) != 0)
				return;
		}
		break;

	case NFSPROC_SYMLINK:
		if (!(dp = parserep(ndo, rp, length)))
			break;
		if (v3) {
			if (parsecreateopres(ndo, dp, ndo->ndo_vflag) != NULL)
				return;
		} else {
			if (parsestatus(ndo, dp, &er) != NULL)
				return;
		}
		break;

	case NFSPROC_MKNOD:
		if (!(dp = parserep(ndo, rp, length)))
			break;
		if (parsecreateopres(ndo, dp, ndo->ndo_vflag) != NULL)
			return;
		break;

	case NFSPROC_REMOVE:
	case NFSPROC_RMDIR:
		if (!(dp = parserep(ndo, rp, length)))
			break;
		if (v3) {
			if (parsewccres(ndo, dp, ndo->ndo_vflag))
				return;
		} else {
			if (parsestatus(ndo, dp, &er) != NULL)
				return;
		}
		break;

	case NFSPROC_RENAME:
		if (!(dp = parserep(ndo, rp, length)))
			break;
		if (v3) {
			if (!(dp = parsestatus(ndo, dp, &er)))
				break;
			if (ndo->ndo_vflag) {
				ND_PRINT((ndo, " from:"));
				if (!(dp = parse_wcc_data(ndo, dp, ndo->ndo_vflag)))
					break;
				ND_PRINT((ndo, " to:"));
				if (!(dp = parse_wcc_data(ndo, dp, ndo->ndo_vflag)))
					break;
			}
			return;
		} else {
			if (parsestatus(ndo, dp, &er) != NULL)
				return;
		}
		break;

	case NFSPROC_LINK:
		if (!(dp = parserep(ndo, rp, length)))
			break;
		if (v3) {
			if (!(dp = parsestatus(ndo, dp, &er)))
				break;
			if (ndo->ndo_vflag) {
				ND_PRINT((ndo, " file POST:"));
				if (!(dp = parse_post_op_attr(ndo, dp, ndo->ndo_vflag)))
					break;
				ND_PRINT((ndo, " dir:"));
				if (!(dp = parse_wcc_data(ndo, dp, ndo->ndo_vflag)))
					break;
				return;
			}
		} else {
			if (parsestatus(ndo, dp, &er) != NULL)
				return;
		}
		break;

	case NFSPROC_READDIR:
		if (!(dp = parserep(ndo, rp, length)))
			break;
		if (v3) {
			if (parsev3rddirres(ndo, dp, ndo->ndo_vflag))
				return;
		} else {
			if (parserddires(ndo, dp) != 0)
				return;
		}
		break;

	case NFSPROC_READDIRPLUS:
		if (!(dp = parserep(ndo, rp, length)))
			break;
		if (parsev3rddirres(ndo, dp, ndo->ndo_vflag))
			return;
		break;

	case NFSPROC_FSSTAT:
		dp = parserep(ndo, rp, length);
		if (dp != NULL && parsestatfs(ndo, dp, v3) != 0)
			return;
		break;

	case NFSPROC_FSINFO:
		dp = parserep(ndo, rp, length);
		if (dp != NULL && parsefsinfo(ndo, dp) != 0)
			return;
		break;

	case NFSPROC_PATHCONF:
		dp = parserep(ndo, rp, length);
		if (dp != NULL && parsepathconf(ndo, dp) != 0)
			return;
		break;

	case NFSPROC_COMMIT:
		dp = parserep(ndo, rp, length);
		if (dp != NULL && parsewccres(ndo, dp, ndo->ndo_vflag) != 0)
			return;
		break;

	default:
		return;
	}
trunc:
	if (!nfserr)
		ND_PRINT((ndo, "%s", tstr));
}
