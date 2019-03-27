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
 *  Test a variety of trace() action invocations.
 *
 * SECTION: Actions and Subroutines/trace();
 *	Output Formatting/trace()
 *
 * NOTES:
 *   We test things that exercise different kinds of DIFO return types
 *   to ensure each one can be traced.
 */

BEGIN
{
	i = 1;
}


tick-1
/i != 5/
{
	trace("test trace");	/* DT_TYPE_STRING */
	trace(12345);		/* DT_TYPE_INT (constant) */
	trace(x++);		/* DT_TYPE_INT (derived) */
	trace(timestamp);	/* DT_TYPE_INT (variable) */
	trace(`kmem_flags);	/* CTF type (by value) */
	trace(*`rootvp);	/* CTF type (by ref) */
	i++;
}

tick-1
/i == 5/
{
	exit(0);
}

ERROR
{
	exit(1);
}
