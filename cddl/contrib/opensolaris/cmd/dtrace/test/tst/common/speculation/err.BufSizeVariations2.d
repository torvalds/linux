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

/*
 * Copyright (c) 2012 by Delphix. All rights reserved.
 */

/*
 * ASSERTION:
 * Verify the behavior of variations in bufsize.
 *
 * SECTION: Speculative Tracing/Options and Tuning;
 *	    Options and Tunables/bufsize
 *
 * NOTES: This test behaves differently depending on the values
 * assigned to bufsize.
 * 1. 0 > bufsize.
 * 2. 0 == bufsize.
 * 3. 0 < bufsize <= 7
 * 4. 8 <= bufsize <= 31
 * 5. 32 <= bufsize <= 47
 * 6. 48 <= bufsize <= 71
 * 7. 72 <= bufsize
 */

#pragma D option quiet
#pragma D option bufsize=4

BEGIN
{
	self->speculateFlag = 0;
	self->commitFlag = 0;
	self->spec = speculation();
	printf("Speculative buffer ID: %d\n", self->spec);
}

BEGIN
{
	speculate(self->spec);
	printf("Lots of data\n");
	printf("Has to be crammed into this buffer\n");
	printf("Until it overflows\n");
	printf("And causes flops\n");
	self->speculateFlag++;

}

BEGIN
/1 <= self->speculateFlag/
{
	commit(self->spec);
	self->commitFlag++;
}

BEGIN
/1 <= self->commitFlag/
{
	printf("Statement was executed\n");
	exit(0);
}

BEGIN
/1 > self->commitFlag/
{
	printf("Statement wasn't executed\n");
	exit(1);
}
