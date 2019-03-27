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
 * Test the code generation and results of the various kinds of inlines.
 * In particular, we test constant and expression-based scalar inlines,
 * associative array inlines, and inlines using translators.
 */

#pragma D option quiet

inline int i0 = 100 + 23;		/* constant-folded integer constant */
inline string i1 = probename;		/* string variable reference */
inline int i2 = pid != 0;		/* expression involving a variable */

struct s {
	int s_x;
};

translator struct s < int T > {
	s_x = T + 1;
};

inline struct s i3 = xlate < struct s > (i0);		/* translator */
inline int i4[int x, int y] = x + y;			/* associative array */
inline int i5[int x] = (xlate < struct s > (x)).s_x;	/* array by xlate */

BEGIN
{
	printf("i0 = %d\n", i0);
	printf("i1 = %s\n", i1);
	printf("i2 = %d\n", i2);

	printf("i3.s_x = %d\n", i3.s_x);
	printf("i4[10, 20] = %d\n", i4[10, 20]);
	printf("i5[123] = %d\n", i5[123]);

	exit(0);
}
