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
 *  Test a complex structure and verify that tracemem can be used to
 *  look at it.
 *
 * SECTION: Structs and Unions/Structs;
 * 	Actions and Subroutines/tracemem()
 */



#pragma D option quiet

struct s {
	int i;
	char c;
	double d;
	float f;
	long l;
	long long ll;
	union sigval u;
	enum uio_rw e;
	struct vnode s;
	struct s1 {
		int i;
		char c;
		double d;
		float f;
		long l;
		long long ll;
		union sigval u;
		enum uio_rw e;
		struct vnode s;
	} sx;
	int a[2];
	int *p;
	int *ap[4];
	int (*fp)();
	int (*afp[2])();
};

BEGIN
{
	tracemem(curthread, sizeof (struct s));
	exit(0);
}
