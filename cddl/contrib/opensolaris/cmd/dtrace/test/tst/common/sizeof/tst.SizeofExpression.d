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
 * Test the use of sizeof with expressions.
 *
 * SECTION: Structs and Unions/Member Sizes and Offsets
 *
 */

#pragma D option quiet

BEGIN
{
	printf("sizeof ('c') : %d\n", sizeof ('c'));
	printf("sizeof (10 * 'c') : %d\n", sizeof (10 * 'c'));
	printf("sizeof (100 + 12345) : %d\n", sizeof (100 + 12345));
	printf("sizeof (1234567890) : %d\n", sizeof (1234567890));

	printf("sizeof (1234512345 * 1234512345 * 12345678 * 1ULL) : %d\n",
	sizeof (1234512345 * 1234512345 * 12345678 * 1ULL));
	printf("sizeof (-129) : %d\n", sizeof (-129));
	printf("sizeof (0x67890/0x77000) : %d\n", sizeof (0x67890/0x77000));

	printf("sizeof (3 > 2 ? 3 : 2) : %d\n", sizeof (3 > 2 ? 3 : 2));

	exit(0);
}

END
/(4 != sizeof ('c')) || (4 != sizeof (10 * 'c')) ||
    (4 != sizeof (100 + 12345)) || (4 != sizeof (1234567890)) ||
    (8 != sizeof (1234512345 * 1234512345 * 12345678 * 1ULL)) ||
    (4 != sizeof (-129)) || (4 != sizeof (0x67890/0x77000)) ||
    (4 != sizeof (3 > 2 ? 3 : 2))/
{
	exit(1);
}
