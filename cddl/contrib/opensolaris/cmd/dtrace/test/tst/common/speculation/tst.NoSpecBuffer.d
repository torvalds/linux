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
 * The number of speculative buffers defaults to one. If no speculative buffer
 * is available when speculation is called, an ID of zero is returned.
 *
 * SECTION: Speculative Tracing/Creating a Speculation
 *
 */
#pragma D option quiet

BEGIN
{
	i = 0;
	notused = $1;
}

syscall::open:entry
/i < 2/
{
	self->spec = speculation();
	printf("Speculative buffer ID: %d\n", self->spec);
	i++;
}

syscall:::
/(2 == i) && (0 == self->spec)/
{
	printf("i: %d\tself->spec: %d", i, self->spec);
	exit(0);
}

syscall:::
/(2 == i) && (0 != self->spec)/
{
	printf("i: %d\tself->spec: %d", i, self->spec);
	exit(1);
}
