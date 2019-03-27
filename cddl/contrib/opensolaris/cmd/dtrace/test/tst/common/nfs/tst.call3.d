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

/*
 * ASSERTION: Make sure nfsv3 provider probes are firing, and that the
 * arguments are properly visible.
 *
 * SECTION: nfs3 provider
 */

#pragma D option destructive
#pragma D option quiet

pid$1:a.out:waiting:entry
{
	this->value = (int *)alloca(sizeof (int));
	*this->value = 1;
	copyout(this->value, arg0, sizeof (int));
}

nfsv3:::op-getattr-start
{
	printf("ci_local: %s\n", args[0]->ci_local);
	printf("ci_remote: %s\n", args[0]->ci_remote);
	printf("ci_protocol: %s\n", args[0]->ci_protocol);

	printf("noi_xid: %d\n", args[1]->noi_xid);
	printf("noi_cred->cr_uid: %d\n", args[1]->noi_cred->cr_uid);
	printf("noi_curpath: %s\n", args[1]->noi_curpath);

	printf("fh3_flags: %d\n", args[2]->object.fh3_flags);
}

nfsv3:::op-getattr-done
{
	printf("ci_local: %s\n", args[0]->ci_local);
	printf("ci_remote: %s\n", args[0]->ci_remote);
	printf("ci_protocol: %s\n", args[0]->ci_protocol);

	printf("noi_xid: %d\n", args[1]->noi_xid);
	printf("noi_cred->cr_uid: %d\n", args[1]->noi_cred->cr_uid);
	printf("noi_curpath: %s\n", args[1]->noi_curpath);

	printf("status: %d\n", args[2]->status);
}

nfsv3:::*-done
/seen[probename] == 0/
{
	++numberseen;
	seen[probename] = 1;
	printf("%d ops seen, latest op is %s\n", numberseen, probename);
}

nfsv3:::*-done
/numberseen == 22/
{
	exit(0);
}

tick-1s
/tick++ == 10/
{
	printf("%d nfsv3 ops seen; should be 22\n", numberseen);
	exit(1);
}
