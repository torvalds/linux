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
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * ASSERTION:
 *	Verify doc example 7-2
 *
 * SECTION:
 *	DocExamples/ksyms
 */


/* Must run "strings -a /dev/ksyms in another shell on the system */


#pragma D option quiet

syscall::read:entry
/curpsinfo->pr_psargs == "strings -a /dev/ksyms"/
{
	printf("read %u bytes to user address %x\n", arg2, arg1);
	self->watched = 1;
}

syscall::read:return
/self->watched/
{
	self->watched = 0;
}

fbt::uiomove:entry
/self->watched/
{
	this->iov = args[3]->uio_iov;

	printf("uiomove %u bytes to %p in pid %d\n", this->iov->iov_len,
	    this->iov->iov_base, pid);
}
