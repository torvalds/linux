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
 * Data recording actions may not follow commit.
 *
 * SECTION: Speculative Tracing/Committing a Speculation;
 *	Options and Tunables/cleanrate
 *
 */
#pragma D option quiet
#pragma D option cleanrate=2000hz

BEGIN
{
	self->speculateFlag = 0;
}

syscall:::entry
{
	self->spec = speculation();
}

syscall:::
/self->spec/
{
	speculate(self->spec);
	printf("Called speculate with id: %d\n", self->spec);
	self->speculateFlag++;
}

syscall:::
/(self->spec) && (self->speculateFlag)/
{
	commit(self->spec);
	printf("Data recording after commit\n");
}

END
{
	exit(0);
}

ERROR
{
	exit(0);
}
