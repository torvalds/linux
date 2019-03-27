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
 * ASSERTION: sizeof returns the size in bytes of any D expression or data
 * type. For sizeof strings the D compiler throws D_SIZEOF_TYPE.
 *
 * SECTION: Structs and Unions/Member Sizes and Offsets
 *
 */
#pragma D option quiet
#pragma D option strsize=256

BEGIN
{
	assoc_array["hello"] = "hello";
	assoc_array["hi"] = "hi";
	assoc_array["hello"] = "hello, world";

	printf("sizeof (assoc_array[\"hello\"]): %d\n",
	sizeof (assoc_array["hello"]));
	printf("sizeof (assoc_array[\"hi\"]): %d\n",
	sizeof (assoc_array["hi"]));

	exit(0);
}
