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
 * Test circular declaration of translations
 *
 * SECTION: Translators/ Translator Declarations
 * SECTION: Translators/ Translate Operator
 */

#pragma D option quiet

struct input_struct {
	int i;
	char c;
};

struct output_struct {
	int myi;
	char myc;
};

translator struct output_struct < struct input_struct ivar >
{
	myi = ((struct input_struct ) ivar).i;
	myc = ((struct input_struct ) ivar).c;
};

translator struct input_struct < struct output_struct uvar >
{
	i = ((struct output_struct ) uvar).myi;
	c = ((struct output_struct ) uvar).myc;
};

struct input_struct f1;
struct output_struct f2;

BEGIN
{
	f1.i = 10;
	f1.c = 'c';

	f2.myi = 100;
	f2.myc = 'd';

	printf("Testing circular translations\n");
	forwardi = xlate < struct output_struct > (f1).myi;
	forwardc = xlate < struct output_struct > (f1).myc;
	backwardi = xlate < struct input_struct > (f2).i;
	backwardc = xlate < struct input_struct > (f2).c;

	printf("forwardi: %d\tforwardc: %c\n", forwardi, forwardc);
	printf("backwardi: %d\tbackwardc: %c", backwardi, backwardc);
	exit(0);
}

BEGIN
/(10 == forwardi) && ('c' == forwardc) && (100 == backwardi) &&
    ('d' == backwardc)/
{
	exit(0);
}

BEGIN
/(10 != forwardi) || ('c' != forwardc) || (100 != backwardi) ||
    ('d' != backwardc)/
{
	exit(1);
}

ERROR
{
	exit(1);
}
