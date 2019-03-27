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

#pragma D option quiet

BEGIN
{
	str = "fooeyfooeyfoo";
	this->success = 0;

	c = 'f';
	printf("strchr(\"%s\", '%c') = \"%s\"\n", str, c, strchr(str, c));
	printf("strrchr(\"%s\", '%c') = \"%s\"\n", str, c, strrchr(str, c));

	c = 'y';
	printf("strchr(\"%s\", '%c') = \"%s\"\n", str, c, strchr(str, c));
	printf("strrchr(\"%s\", '%c') = \"%s\"\n", str, c, strrchr(str, c));

	printf("strrchr(\"%s\", '%c') = \"%s\"\n", strchr(str, c), c,
	    strrchr(strchr(str, c), c));

	this->success = 1;
}

BEGIN
/!this->success/
{
	exit(1);
}

BEGIN
/strchr(str, 'a') != NULL/
{
	exit(2);
}

BEGIN
/strrchr(str, 'a') != NULL/
{
	exit(3);
}

BEGIN
{
	exit(0);
}
