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
 * Copyright (c) 2011, Joyent, Inc. All rights reserved.
 */

#pragma D option quiet

BEGIN
{
	i = 0;

	input[i] = "ahi";
	expected[i++] = "AHI";

	input[i] = "MaHi!";
	expected[i++] = "MAHI!";

	input[i] = "   dace-9";
	expected[i++] = "   DACE-9";

	input[i] = "!@#$%";
	expected[i++] = "!@#$%";

	i = 0;
}

tick-1ms
/input[i] != NULL && (this->out = toupper(input[i])) != expected[i]/
{
	printf("expected toupper(\"%s\") to be \"%s\"; found \"%s\"\n",
	    input[i], expected[i], this->out);
	exit(1);
}

tick-1ms
/input[i] != NULL/
{
	printf("toupper(\"%s\") is \"%s\", as expected\n",
	    input[i], expected[i]);
}

tick-1ms
/input[i++] == NULL/
{
	exit(0);
}
