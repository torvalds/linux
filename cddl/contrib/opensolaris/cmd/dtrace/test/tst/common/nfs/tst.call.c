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
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <strings.h>
#include <rpc/rpc.h>

#include "rpcsvc/nfs4_prot.h"

int nfs4_skip_bytes;

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

int
dotest(void)
{
	CLIENT *client;
	AUTH *auth;
	COMPOUND4args args;
	COMPOUND4res res;
	enum clnt_stat status;
	struct timeval timeout;
	nfs_argop4 arg[1];
	char *tag = "dtrace test";
	volatile int a = 0;

	while (waiting(&a) == 0)
		continue;

	timeout.tv_sec = 30;
	timeout.tv_usec = 0;

	client = clnt_create("localhost", NFS4_PROGRAM, NFS_V4, "tcp");
	if (client == NULL) {
		clnt_pcreateerror("test");
		return (1);
	}
	auth = authsys_create_default();
	client->cl_auth = auth;
	args.minorversion = 0;
	args.tag.utf8string_len = strlen(tag);
	args.tag.utf8string_val = tag;
	args.argarray.argarray_len = sizeof (arg) / sizeof (nfs_argop4);
	args.argarray.argarray_val = arg;

	arg[0].argop = OP_PUTROOTFH;
	/* no need to manipulate nfs_argop4_u */

	bzero(&res, sizeof (res));

	status = clnt_call(client, NFSPROC4_COMPOUND,
	    xdr_COMPOUND4args, (caddr_t)&args,
	    xdr_COMPOUND4res, (caddr_t)&res,
	    timeout);
	if (status != RPC_SUCCESS) {
		clnt_perror(client, "test");
		return (2);
	}

	return (0);
}

/*ARGSUSED*/
int
main(int argc, char **argv)
{
	char shareline[BUFSIZ], unshareline[BUFSIZ];
	int rc;

	(void) snprintf(shareline, sizeof (shareline),
	    "mkdir /tmp/nfsv4test.%d ; share /tmp/nfsv4test.%d", getpid(),
	    getpid());
	(void) snprintf(unshareline, sizeof (unshareline),
	    "unshare /tmp/nfsv4test.%d ; rmdir /tmp/nfsv4test.%d", getpid(),
	    getpid());

	(void) system(shareline);
	rc = dotest();
	(void) system(unshareline);

	return (rc);
}
