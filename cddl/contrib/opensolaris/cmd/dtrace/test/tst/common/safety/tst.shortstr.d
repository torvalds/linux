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
#pragma D option strsize=16

BEGIN
{
	this->str = ",,,Carrots,,Barley,Oatmeal,,,,,,,,,,,,,,,,,,Beans,";
}

BEGIN
{
	strtok(this->str, ",");
}

BEGIN
{
	this->str = ",,,,,,,,,,,,,,,,,,,,,,Carrots,";
	strtok(this->str, ",");
}

BEGIN
{
	strtok(this->str, "a");
}

BEGIN
{
	printf("%s\n", substr(this->str, 1, 40));
}

BEGIN
{
	printf("%s\n", strjoin(this->str, this->str));
}

BEGIN
{
	this->str1 = ".........................................";
	printf("%d\n", index(this->str, this->str1));
}

BEGIN
{
	printf("%d\n", rindex(this->str, this->str1));
}

BEGIN
{
	exit(0);
}
