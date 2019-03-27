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

#pragma D option quiet

BEGIN
{
	j = 0;
}

tick-1ms
/i < 100/
{
	i++;
	@a[i] = sum(i);
	@b[i] = sum((25 + i) % 100);
	@c[i] = sum((50 + i) % 100);
	@d[i] = sum((75 + i) % 100);
}

tick-1ms
/i == 100 && j < 10/
{
	printf("Sorted at position %d:\n", j);
	setopt("aggsortpos", lltostr(j));
	printa("%9d %@9d %@9d %@9d %@9d %@9d %@9d\n", @a, @b, @c, @a, @d, @a);
	printf("\n");
	j++;
}

tick-1ms
/i == 100 && j == 10/
{
	exit(0);
}
