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
 */

/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <strings.h>
#include <rpc/rpc.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/param.h>
#include <rpcsvc/mount.h>

#include "rpcsvc/nfs_prot.h"

char sharedpath[MAXPATHLEN];
fhandle3 *rootfh;

/*
 * The waiting() function returns the value passed in, until something
 * external modifies it.  In this case, the D script tst.call.d will
 * modify the value of *a, and thus break the while loop in dotest().
 *
 * This serves the purpose of not making the RPC calls until tst.call.d
 * is active.  Thus, the probes in tst.call.d can fire as a result of
 * the RPC call in dotest().
 */

int
waiting(volatile int *a)
{
	return (*a);
}

static void
getattr_arginit(void *argp)
{
	GETATTR3args *args = argp;

	args->object.data.data_len = rootfh->fhandle3_len;
	args->object.data.data_val = rootfh->fhandle3_val;
}

static void
setattr_arginit(void *argp)
{
	SETATTR3args *args = argp;

	bzero(args, sizeof (*args));
	args->object.data.data_len = rootfh->fhandle3_len;
	args->object.data.data_val = rootfh->fhandle3_val;
}

static void
lookup_arginit(void *argp)
{
	LOOKUP3args *args = argp;

	args->what.name = "giant-skunk";
	args->what.dir.data.data_len = rootfh->fhandle3_len;
	args->what.dir.data.data_val = rootfh->fhandle3_val;
}

static void
access_arginit(void *argp)
{
	ACCESS3args *args = argp;

	args->object.data.data_len = rootfh->fhandle3_len;
	args->object.data.data_val = rootfh->fhandle3_val;
}

static void
commit_arginit(void *argp)
{
	COMMIT3args *args = argp;

	bzero(args, sizeof (*args));
	args->file.data.data_len = rootfh->fhandle3_len;
	args->file.data.data_val = rootfh->fhandle3_val;
}

static void
create_arginit(void *argp)
{
	CREATE3args *args = argp;

	bzero(args, sizeof (*args));
	args->where.name = "pinky-blue";
	args->where.dir.data.data_len = rootfh->fhandle3_len;
	args->where.dir.data.data_val = rootfh->fhandle3_val;
}

static void
fsinfo_arginit(void *argp)
{
	FSINFO3args *args = argp;

	args->fsroot.data.data_len = rootfh->fhandle3_len;
	args->fsroot.data.data_val = rootfh->fhandle3_val;
}

static void
fsstat_arginit(void *argp)
{
	FSSTAT3args *args = argp;

	args->fsroot.data.data_len = rootfh->fhandle3_len;
	args->fsroot.data.data_val = rootfh->fhandle3_val;
}

static void
link_arginit(void *argp)
{
	LINK3args *args = argp;

	args->file.data.data_len = rootfh->fhandle3_len;
	args->file.data.data_val = rootfh->fhandle3_val;
	args->link.dir.data.data_len = rootfh->fhandle3_len;
	args->link.dir.data.data_val = rootfh->fhandle3_val;
	args->link.name = "samf";
}

static void
mkdir_arginit(void *argp)
{
	MKDIR3args *args = argp;

	bzero(args, sizeof (*args));
	args->where.dir.data.data_len = rootfh->fhandle3_len;
	args->where.dir.data.data_val = rootfh->fhandle3_val;
	args->where.name = "cookie";
}

static void
mknod_arginit(void *argp)
{
	MKNOD3args *args = argp;

	bzero(args, sizeof (*args));
	args->where.dir.data.data_len = rootfh->fhandle3_len;
	args->where.dir.data.data_val = rootfh->fhandle3_val;
	args->where.name = "pookie";
}

static void
null_arginit(void *argp)
{
}

static void
pathconf_arginit(void *argp)
{
	PATHCONF3args *args = argp;

	args->object.data.data_len = rootfh->fhandle3_len;
	args->object.data.data_val = rootfh->fhandle3_val;
}

static void
read_arginit(void *argp)
{
	READ3args *args = argp;

	bzero(args, sizeof (*args));
	args->file.data.data_len = rootfh->fhandle3_len;
	args->file.data.data_val = rootfh->fhandle3_val;
}

static void
readdir_arginit(void *argp)
{
	READDIR3args *args = argp;

	bzero(args, sizeof (*args));
	args->dir.data.data_len = rootfh->fhandle3_len;
	args->dir.data.data_val = rootfh->fhandle3_val;
	args->count = 1024;
}

static void
readdirplus_arginit(void *argp)
{
	READDIRPLUS3args *args = argp;

	bzero(args, sizeof (*args));
	args->dir.data.data_len = rootfh->fhandle3_len;
	args->dir.data.data_val = rootfh->fhandle3_val;
	args->dircount = 1024;
	args->maxcount = 1024;
}

static void
readlink_arginit(void *argp)
{
	READLINK3args *args = argp;

	args->symlink.data.data_len = rootfh->fhandle3_len;
	args->symlink.data.data_val = rootfh->fhandle3_val;
}

static void
remove_arginit(void *argp)
{
	REMOVE3args *args = argp;

	args->object.dir.data.data_len = rootfh->fhandle3_len;
	args->object.dir.data.data_val = rootfh->fhandle3_val;
	args->object.name = "antelope";
}

static void
rename_arginit(void *argp)
{
	RENAME3args *args = argp;

	args->from.dir.data.data_len = rootfh->fhandle3_len;
	args->from.dir.data.data_val = rootfh->fhandle3_val;
	args->from.name = "walter";
	args->to.dir.data.data_len = rootfh->fhandle3_len;
	args->to.dir.data.data_val = rootfh->fhandle3_val;
	args->to.name = "wendy";
}

static void
rmdir_arginit(void *argp)
{
	RMDIR3args *args = argp;

	args->object.dir.data.data_len = rootfh->fhandle3_len;
	args->object.dir.data.data_val = rootfh->fhandle3_val;
	args->object.name = "bunny";
}

static void
symlink_arginit(void *argp)
{
	SYMLINK3args *args = argp;

	bzero(args, sizeof (*args));
	args->where.dir.data.data_len = rootfh->fhandle3_len;
	args->where.dir.data.data_val = rootfh->fhandle3_val;
	args->where.name = "parlor";
	args->symlink.symlink_data = "interior";
}

static void
write_arginit(void *argp)
{
	WRITE3args *args = argp;

	bzero(args, sizeof (*args));
	args->file.data.data_len = rootfh->fhandle3_len;
	args->file.data.data_val = rootfh->fhandle3_val;
}

typedef void (*call3_arginit_t)(void *);

typedef struct {
	call3_arginit_t arginit;
	rpcproc_t proc;
	xdrproc_t xdrargs;
	size_t argsize;
	xdrproc_t xdrres;
	size_t ressize;
} call3_test_t;
call3_test_t call3_tests[] = {
	{getattr_arginit, NFSPROC3_GETATTR, xdr_GETATTR3args,
	    sizeof (GETATTR3args), xdr_GETATTR3res, sizeof (GETATTR3res)},
	{setattr_arginit, NFSPROC3_SETATTR, xdr_SETATTR3args,
	    sizeof (SETATTR3args), xdr_SETATTR3res, sizeof (SETATTR3res)},
	{lookup_arginit, NFSPROC3_LOOKUP, xdr_LOOKUP3args,
	    sizeof (LOOKUP3args), xdr_LOOKUP3res, sizeof (LOOKUP3res)},
	{access_arginit, NFSPROC3_ACCESS, xdr_ACCESS3args,
	    sizeof (ACCESS3args), xdr_ACCESS3res, sizeof (ACCESS3res)},
	{commit_arginit, NFSPROC3_COMMIT, xdr_COMMIT3args,
	    sizeof (COMMIT3args), xdr_COMMIT3res, sizeof (COMMIT3res)},
	{create_arginit, NFSPROC3_CREATE, xdr_CREATE3args,
	    sizeof (CREATE3args), xdr_CREATE3res, sizeof (CREATE3res)},
	{fsinfo_arginit, NFSPROC3_FSINFO, xdr_FSINFO3args,
	    sizeof (FSINFO3args), xdr_FSINFO3res, sizeof (FSINFO3res)},
	{fsstat_arginit, NFSPROC3_FSSTAT, xdr_FSSTAT3args,
	    sizeof (FSSTAT3args), xdr_FSSTAT3res, sizeof (FSSTAT3res)},
	{link_arginit, NFSPROC3_LINK, xdr_LINK3args,
	    sizeof (LINK3args), xdr_LINK3res, sizeof (LINK3res)},
	{mkdir_arginit, NFSPROC3_MKDIR, xdr_MKDIR3args,
	    sizeof (MKDIR3args), xdr_MKDIR3res, sizeof (MKDIR3res)},
	{mknod_arginit, NFSPROC3_MKNOD, xdr_MKNOD3args,
	    sizeof (MKNOD3args), xdr_MKNOD3res, sizeof (MKNOD3res)},
	/*
	 * NULL proc is special.  Rather than special case its zero-sized
	 * args/results, we give it a small nonzero size, so as to not
	 * make realloc() do the wrong thing.
	 */
	{null_arginit, NFSPROC3_NULL, xdr_void, sizeof (int), xdr_void,
	    sizeof (int)},
	{pathconf_arginit, NFSPROC3_PATHCONF, xdr_PATHCONF3args,
	    sizeof (PATHCONF3args), xdr_PATHCONF3res, sizeof (PATHCONF3res)},
	{read_arginit, NFSPROC3_READ, xdr_READ3args,
	    sizeof (READ3args), xdr_READ3res, sizeof (READ3res)},
	{readdir_arginit, NFSPROC3_READDIR, xdr_READDIR3args,
	    sizeof (READDIR3args), xdr_READDIR3res, sizeof (READDIR3res)},
	{readdirplus_arginit, NFSPROC3_READDIRPLUS, xdr_READDIRPLUS3args,
	    sizeof (READDIRPLUS3args), xdr_READDIRPLUS3res,
	    sizeof (READDIRPLUS3res)},
	{readlink_arginit, NFSPROC3_READLINK, xdr_READLINK3args,
	    sizeof (READLINK3args), xdr_READLINK3res, sizeof (READLINK3res)},
	{remove_arginit, NFSPROC3_REMOVE, xdr_REMOVE3args,
	    sizeof (REMOVE3args), xdr_REMOVE3res, sizeof (REMOVE3res)},
	{rename_arginit, NFSPROC3_RENAME, xdr_RENAME3args,
	    sizeof (RENAME3args), xdr_RENAME3res, sizeof (RENAME3res)},
	{rmdir_arginit, NFSPROC3_RMDIR, xdr_RMDIR3args,
	    sizeof (RMDIR3args), xdr_RMDIR3res, sizeof (RMDIR3res)},
	{symlink_arginit, NFSPROC3_SYMLINK, xdr_SYMLINK3args,
	    sizeof (SYMLINK3args), xdr_SYMLINK3res, sizeof (SYMLINK3res)},
	{write_arginit, NFSPROC3_WRITE, xdr_WRITE3args,
	    sizeof (WRITE3args), xdr_WRITE3res, sizeof (WRITE3res)},
	{NULL}
};

int
dotest(void)
{
	CLIENT *client, *mountclient;
	AUTH *auth;
	struct timeval timeout;
	caddr_t args, res;
	enum clnt_stat status;
	rpcproc_t proc;
	call3_test_t *test;
	void *argbuf = NULL;
	void *resbuf = NULL;
	struct mountres3 mountres3;
	char *sp;
	volatile int a = 0;

	while (waiting(&a) == 0)
		continue;

	timeout.tv_sec = 30;
	timeout.tv_usec = 0;

	mountclient = clnt_create("localhost", MOUNTPROG, MOUNTVERS3, "tcp");
	if (mountclient == NULL) {
		clnt_pcreateerror("clnt_create mount");
		return (1);
	}
	auth = authsys_create_default();
	mountclient->cl_auth = auth;
	sp = sharedpath;
	bzero(&mountres3, sizeof (mountres3));
	status = clnt_call(mountclient, MOUNTPROC_MNT,
	    xdr_dirpath, (char *)&sp,
	    xdr_mountres3, (char *)&mountres3,
	    timeout);
	if (status != RPC_SUCCESS) {
		clnt_perror(mountclient, "mnt");
		return (1);
	}
	if (mountres3.fhs_status != 0) {
		fprintf(stderr, "MOUNTPROG/MOUNTVERS3 failed %d\n",
		    mountres3.fhs_status);
		return (1);
	}
	rootfh = &mountres3.mountres3_u.mountinfo.fhandle;

	client = clnt_create("localhost", NFS3_PROGRAM, NFS_V3, "tcp");
	if (client == NULL) {
		clnt_pcreateerror("clnt_create");
		return (1);
	}
	client->cl_auth = auth;

	for (test = call3_tests; test->arginit; ++test) {
		argbuf = realloc(argbuf, test->argsize);
		resbuf = realloc(resbuf, test->ressize);
		if ((argbuf == NULL) || (resbuf == NULL)) {
			perror("realloc() failed");
			return (1);
		}
		(test->arginit)(argbuf);
		bzero(resbuf, test->ressize);
		status = clnt_call(client, test->proc,
		    test->xdrargs, argbuf,
		    test->xdrres, resbuf,
		    timeout);
		if (status != RPC_SUCCESS)
			clnt_perror(client, "call");
	}

	status = clnt_call(mountclient, MOUNTPROC_UMNT,
	    xdr_dirpath, (char *)&sp,
	    xdr_void, NULL,
	    timeout);
	if (status != RPC_SUCCESS)
		clnt_perror(mountclient, "umnt");

	return (0);
}

/*ARGSUSED*/
int
main(int argc, char **argv)
{
	char shareline[BUFSIZ], unshareline[BUFSIZ];
	int rc;

	(void) snprintf(sharedpath, sizeof (sharedpath),
	    "/tmp/nfsv3test.%d", getpid());
	(void) snprintf(shareline, sizeof (shareline),
	    "mkdir %s ; share %s", sharedpath, sharedpath);
	(void) snprintf(unshareline, sizeof (unshareline),
	    "unshare %s ; rmdir %s", sharedpath, sharedpath);

	(void) system(shareline);
	rc = dotest();
	(void) system(unshareline);

	return (rc);
}
