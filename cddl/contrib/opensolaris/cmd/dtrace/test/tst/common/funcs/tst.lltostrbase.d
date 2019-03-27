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
 * Copyright (c) 2011, Joyent, Inc. All rights reserved.
 */

#pragma D option quiet

int64_t val[int];

BEGIN
{
	base = -2;
	i = 0;
	val[i++] = -10;
	val[i++] = -1;
	val[i++] = 0;
	val[i++] = 10;
	val[i++] = 100;
	val[i++] = 1000;
	val[i++] = (1LL << 62);
	maxval = i;
	i = 0;
}

tick-1ms
/i < maxval/
{
	printf("base %2d of %20d:  ", base, val[i]);
}

tick-1ms
/i < maxval/
{
	printf("  %s\n", lltostr(val[i], base));
}

ERROR
{
	printf("  <error>\n");
}

tick-1ms
/i < maxval/
{
	i++;
}

tick-1ms
/i == maxval/
{
	i = 0;
	base++;
}

tick-1ms
/base > 40/
{
	exit(0);
}

