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
 * A clause cannot contain multiple commit() calls to disjoint buffers.
 *
 * SECTION: Speculative Tracing/Committing a Speculation;
 *	Options and Tunables/cleanrate
 */
#pragma D option quiet
#pragma D option cleanrate=3000hz

BEGIN
{
	self->i = 0;
	self->j = 0;
	self->commit = 0;
	var1 = 0;
	var2 = 0;
}

BEGIN
{
	var1 = speculation();
	printf("Speculation ID: %d\n", var1);
}

BEGIN
{
	var2 = speculation();
	printf("Speculation ID: %d\n", var2);
}

BEGIN
/var1/
{
	speculate(var1);
	printf("Speculating on id: %d\n", var1);
	self->i++;
}

BEGIN
/var2/
{
	speculate(var2);
	printf("Speculating on id: %d", var2);
	self->j++;

}

BEGIN
/(self->i) && (self->j)/
{
	commit(var1);
	commit(var2);
	self->commit++;
}

BEGIN
/self->commit/
{
	printf("Succesfully commited both buffers");
	exit(0);
}

BEGIN
/!self->commit/
{
	printf("Couldnt commit both buffers");
	exit(1);
}

ERROR
{
	exit(1);
}
