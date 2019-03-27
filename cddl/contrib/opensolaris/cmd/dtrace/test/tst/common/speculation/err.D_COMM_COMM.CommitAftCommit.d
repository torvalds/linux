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

/*
 * ASSERTION:
 * A clause cannot contain multiple commit() calls to same buffer.
 *
 * SECTION: Speculative Tracing/Committing a Speculation;
 *	Options and Tunables/cleanrate
 */
#pragma D option quiet
#pragma D option cleanrate=3000hz

BEGIN
{
	self->i = 0;
	var1 = 0;
}

profile:::tick-1sec
/!var1/
{
	var1 = speculation();
	printf("Speculation ID: %d\n", var1);
}

profile:::tick-1sec
/var1/
{
	speculate(var1);
	printf("Speculating on id: %d\n", var1);
	self->i++;
}

profile:::tick-1sec
/(!self->i)/
{
}

profile:::tick-1sec
/(self->i)/
{
	commit(var1);
	commit(var1);
	exit(0);
}

END
{
	printf("Succesfully commited both buffers");
	exit(0);
}

ERROR
{
	exit(0);
}
