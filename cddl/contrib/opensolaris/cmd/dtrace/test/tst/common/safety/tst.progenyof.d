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
 * 	trace progenyof() at every fbt probe point.
 *
 * SECTION: Actions and Subroutines/progenyof();
 *	Options and Tunables/bufsize;
 * 	Options and Tunables/bufpolicy;
 * 	Options and Tunables/statusrate
 */

#pragma D option bufsize=1000
#pragma D option bufpolicy=ring
#pragma D option statusrate=10ms

fbt:::
{
	trace(progenyof(rand()));
}

profile-4999hz
{
	trace(progenyof(rand()));
}

tick-1sec
/n++ == 10/
{
	exit(0);
}
