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
 * Verify the nested behavior of unions.
 *
 * SECTION: Structs and Unions/Unions
 *
 */
#pragma D option quiet

union InnerMost {
	int position;
	char content;
};

union InnerMore {
	union InnerMost IMost;
	int dummy_More;
};

union Inner {
	union InnerMore IMore;
	int dummy_More;
};

union Outer {
	union Inner I;
	int dummy_More;
};

union OuterMore {
	union Outer O;
	int dummy_More;
};

union OuterMost {
	union OuterMore OMore;
	int dummy_More;
} OMost;


BEGIN
{

	OMost.OMore.O.I.IMore.IMost.position = 5;
	OMost.OMore.O.I.IMore.IMost.content = 'e';

	printf("OMost.OMore.O.I.IMore.IMost.content: %c\n",
	       OMost.OMore.O.I.IMore.IMost.content);

	exit(0);
}

END
/'e' != OMost.OMore.O.I.IMore.IMost.content/
{
	exit(1);
}
