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
 * If you dereference a single member of the translation input, then the
 * compiler will generate the code corresponding to that member
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
	oc = ((struct input_struct) ivar).ic;
};

struct output_struct out;
struct input_struct in;

BEGIN
{
	in.ii = 100;
	in.ic = 'z';

	printf("Translating only a part of the input struct\n");
	out.oi = xlate < struct output_struct > (in).oi;

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
