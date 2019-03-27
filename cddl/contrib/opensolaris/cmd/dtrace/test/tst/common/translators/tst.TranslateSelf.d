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
 * Test the translation of a struct to itself.
 *
 * SECTION: Translators/ Translator Declarations.
 * SECTION: Translators/ Translate operator.
 *
 *
 */

#pragma D option quiet

struct output_struct {
	int myi;
	char myc;
};

translator struct output_struct < struct output_struct uvar >
{
	myi = ((struct output_struct ) uvar).myi;
	myc = ((struct output_struct ) uvar).myc;
};

struct output_struct out;
struct output_struct outer;

BEGIN
{
	out.myi = 1234;
	out.myc = 'a';

	printf("Test translation of a struct to itself\n");
	outer = xlate < struct output_struct > (out);

	printf("outer.myi: %d\t outer.myc: %c\n", outer.myi, outer.myc);
}

BEGIN
/(1234 != outer.myi) || ('a' != outer.myc)/
{
	exit(1);
}

BEGIN
/(1234 == outer.myi) && ('a' == outer.myc)/
{
	exit(0);
}
