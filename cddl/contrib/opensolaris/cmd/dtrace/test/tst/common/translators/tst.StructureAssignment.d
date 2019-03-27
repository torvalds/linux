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
 * If the entire output is copied by means of a structure assignment, any
 * members for which no translation expressions are defined will be filled
 * with zeroes.
 *
 * SECTION: Translators/ Translate Operator
 */

#pragma D option quiet

struct input_struct {
	int ii;
	char ic;
};

struct output_struct {
	int oi;
	char oc;
};


translator struct output_struct < struct input_struct ivar >
{
	oi = ((struct input_struct) ivar).ii;
};

struct output_struct out;
struct input_struct in;

BEGIN
{
	in.ii = 100;
	in.ic = 'z';

	printf("Translating via struct assignment\n");
	out = xlate < struct output_struct > (in);

	printf("out.oi: %d\t out.oc: %d\n", out.oi, out.oc);
}

BEGIN
/(100 != out.oi) || (0 != out.oc)/
{
	exit(1);
}

BEGIN
/(100 == out.oi) && (0 == out.oc)/
{
	exit(0);
}

ERROR
{
	exit(1);
}
