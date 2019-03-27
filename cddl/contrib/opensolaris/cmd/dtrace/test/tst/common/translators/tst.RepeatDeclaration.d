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
 * Reassign the members of the output struct using another translator
 * input struct.
 *
 * SECTION: Translators/Translator Declarations
 */

#pragma D option quiet

struct input_struct1 {
	int ii1;
	char ic1;
} *ivar1;

struct input_struct2 {
	long ii2;
	long ic2;
} *ivar2;

struct output_struct {
	int oi;
	char oc;
};


translator struct output_struct < struct input_struct1 *ivar1 >
{
	oi = ((struct input_struct1 *) ivar1)->ii1;
	oc = ((struct input_struct1 *) ivar1)->ic1;
};

translator struct output_struct < struct input_struct2 *ivar2 >
{
	oi = ((struct input_struct2 *) ivar2)->ii2;
	oc = ((struct input_struct2 *) ivar2)->ic2;
};

BEGIN
{
	printf("Reassignment of a struct's members with different input");
	exit(0);
}
