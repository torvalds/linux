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
 *   Test declaration processing of all the fundamental kinds of type
 *   declarations.  Check their sizes.
 *
 * SECTION: Types, Operators, and Expressions/Data Types and Sizes
 */


#pragma D option quiet

BEGIN
{
	printf("\nsizeof (char) = %u\n", sizeof (char));
	printf("sizeof (signed char) = %u\n", sizeof (signed char));
	printf("sizeof (unsigned char) = %u\n", sizeof (unsigned char));
	printf("sizeof (short) = %u\n", sizeof (short));
	printf("sizeof (signed short) = %u\n", sizeof (signed short));
	printf("sizeof (unsigned short) = %u\n", sizeof (unsigned short));
	printf("sizeof (int) = %u\n", sizeof (int));
	printf("sizeof (signed int) = %u\n", sizeof (signed int));
	printf("sizeof (unsigned int) = %u\n", sizeof (unsigned int));
	printf("sizeof (long long) = %u\n", sizeof (long long));
	printf("sizeof (signed long long) = %u\n", sizeof (signed long long));
	printf("sizeof (unsigned long long) = %u\n",
	    sizeof (unsigned long long));
	printf("sizeof (float) = %u\n", sizeof (float));
	printf("sizeof (double) = %u\n", sizeof (double));

	exit(0);
}
