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
	die = "Die";
	tap = ", SystemTap, ";
	the = "The";
}

BEGIN
{
	phrase = strjoin(die, tap);
	phrase = strjoin(phrase, die);
	expected = "Die, SystemTap, Die";
}

BEGIN
/phrase != expected/
{
	printf("global: expected '%s', found '%s'\n", expected, phrase);
	exit(1);
}

BEGIN
{
	this->phrase = strjoin(the, tap);
}

BEGIN
{
	this->phrase = strjoin(this->phrase, the);
	expected = "The, SystemTap, The";
}

BEGIN
/this->phrase != expected/
{
	printf("clause-local: expected '%s', found '%s'\n",
	    expected, this->phrase);
	exit(2);
}

BEGIN
{
	phrase = NULL;
	this->phrase = NULL;
}

BEGIN
/phrase != NULL/
{
	printf("expected global to be NULL\n");
	exit(3);
}

BEGIN
/this->phrase != NULL/
{
	printf("expected clause-local to be NULL\n");
	exit(4);
}

BEGIN
{
	exit(0);
}
