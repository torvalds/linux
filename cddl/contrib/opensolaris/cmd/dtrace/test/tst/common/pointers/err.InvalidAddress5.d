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
 * Using integer arithmetic providing a non-aligned memory address will throw
 * a runtime error.
 *
 * SECTION: Pointers and Arrays/Generic Pointers
 */

#pragma D option quiet

#if defined(__i386) || defined(__amd64)
#define __x86 1
#endif

int array[2];
char *ptr;
int *p;
int *q;
int *r;

BEGIN
{
	array[0] = 0x12345678;
	array[1] = 0xabcdefff;

	ptr = (char *) &array[0];

	p = (int *) (ptr);
	q = (int *) (ptr + 2);
	r = (int *) (ptr + 3);

	printf("*p: 0x%x\n", *p);
	printf("*q: 0x%x\n", *q);
	printf("*r: 0x%x\n", *r);

	/*
	 * On x86, the above unaligned memory accesses are allowed and should
	 * not result in the ERROR probe firing.
	 */
#ifdef __x86
	exit(1);
#else
	exit(0);
#endif
}

ERROR
{
#ifdef __x86
	exit(0);
#else
	exit(1);
#endif
}
