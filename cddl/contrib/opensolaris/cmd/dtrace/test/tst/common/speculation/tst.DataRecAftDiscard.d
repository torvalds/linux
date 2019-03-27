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
 * Data recording actions may follow discard.
 *
 * SECTION: Speculative Tracing/Discarding a Speculation;
 *	Options and Tunables/cleanrate
 */
#pragma D option quiet
#pragma D option cleanrate=2000hz

BEGIN
{
	self->speculateFlag = 0;
	self->discardFlag = 0;
	self->spec = speculation();
}

BEGIN
/self->spec/
{
	speculate(self->spec);
	printf("Called speculate with id: %d\n", self->spec);
	self->speculateFlag++;
}

BEGIN
/(self->spec) && (self->speculateFlag)/
{
	discard(self->spec);
	self->discardFlag++;
	printf("Data recording after discard\n");
}

BEGIN
/self->discardFlag/
{
	exit(0);
}

BEGIN
/!self->discardFlag/
{
	exit(1);
}

ERROR
{
	exit(1);
}
