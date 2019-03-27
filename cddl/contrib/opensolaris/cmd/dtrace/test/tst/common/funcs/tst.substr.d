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
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#pragma D option quiet
#pragma D option strsize=32

struct {
	int index;
	int length;
	int nolen;
	int alt;
} command[int];

int i;

BEGIN
{
	str = "foobarbazbop";
	str2 = "";
	altstr = "CRAIG: Positioned them, I don't ";
	altstr2 = "know... I'm fairly wide guy.";

	command[i].index = 3;
	command[i].nolen = 1;
	i++;

	command[i].index = 300;
	command[i].nolen = 1;
	i++;

	command[i].index = -10;
	command[i].nolen = 1;
	i++;

	command[i].index = 0;
	command[i].nolen = 1;
	i++;

	command[i].index = 1;
	command[i].nolen = 1;
	i++;

	command[i].index = strlen(str) - 1;
	command[i].nolen = 1;
	i++;

	command[i].index = strlen(str);
	command[i].nolen = 1;
	i++;

	command[i].index = strlen(str) + 1;
	command[i].nolen = 1;
	i++;

	command[i].index = 8;
	command[i].length = 20;
	i++;

	command[i].index = 4;
	command[i].length = 4;
	i++;

	command[i].index = 5;
	command[i].length = strlen(str) - command[i].index + 1;
	i++;

	command[i].index = 5;
	command[i].length = strlen(str) - command[i].index + 2;
	i++;

	command[i].index = 400;
	command[i].length = 20;
	i++;

	command[i].index = 400;
	command[i].length = 0;
	i++;

	command[i].index = 400;
	command[i].length = -1;
	i++;

	command[i].index = 3;
	command[i].length = 0;
	i++;

	command[i].index = 3;
	command[i].length = -1;
	i++;

	command[i].index = 3;
	command[i].length = -4;
	i++;

	command[i].index = 3;
	command[i].length = -20;
	i++;

	command[i].index = -10;
	command[i].length = -5;
	i++;

	command[i].index = 0;
	command[i].length = 400;
	i++;

	command[i].index = -1;
	command[i].length = 400;
	i++;

	command[i].index = -1;
	command[i].length = 0;
	i++;

	command[i].index = -1;
	command[i].length = -1;
	i++;

	command[i].index = -2 * strlen(str);
	command[i].length = 2 * strlen(str);
	i++;

	command[i].index = -2 * strlen(str);
	command[i].length = strlen(str);
	i++;

	command[i].index = -2 * strlen(str);
	command[i].length = strlen(str) + 1;
	i++;

	command[i].index = -1 * strlen(str);
	command[i].length = strlen(str);
	i++;

	command[i].index = -1 * strlen(str);
	command[i].length = strlen(str) - 1;
	i++;

	command[i].index = 100;
	command[i].length = 10;
	command[i].alt = 1;
	i++;

	command[i].index = 100;
	command[i].nolen = 1;
	command[i].alt = 1;
	i++;

	end = i;
	i = 0;
	printf("#!/usr/perl5/bin/perl\n\nBEGIN {\n");

}

tick-1ms
/i < end && command[i].nolen/
{
	this->str = command[i].alt ? altstr : str;
	this->str2 = command[i].alt ? altstr2 : str2;
	this->result = substr(command[i].alt ?
	    "CRAIG: Positioned them, I don't know... I'm fairly wide guy." :
	    str, command[i].index);

	printf("\tif (substr(\"%s%s\", %d) ne \"%s\") {\n",
	    this->str, this->str2, command[i].index, this->result);

	printf("\t\tprintf(\"perl => substr(\\\"%s%s\\\", %d) = ",
	    this->str, this->str2, command[i].index);
	printf("\\\"%%s\\\"\\n\",\n\t\t    substr(\"%s%s\", %d));\n",
	    this->str, this->str2, command[i].index);
	printf("\t\tprintf(\"   D => substr(\\\"%s%s\\\", %d) = ",
	    this->str, this->str2, command[i].index);
	printf("\\\"%%s\\\"\\n\",\n\t\t    \"%s\");\n", this->result);
	printf("\t\t$failed++;\n");
	printf("\t}\n\n");
}

tick-1ms
/i < end && !command[i].nolen/
{
	this->str = command[i].alt ? altstr : str;
	this->str2 = command[i].alt ? altstr2 : str2;
	this->result = substr(command[i].alt ?
	    "CRAIG: Positioned them, I don't know... I'm fairly wide guy." :
	    str, command[i].index, command[i].length);

	printf("\tif (substr(\"%s%s\", %d, %d) ne \"%s\") {\n",
	    this->str, this->str2, command[i].index, command[i].length,
	    this->result);
	printf("\t\tprintf(\"perl => substr(\\\"%s%s\\\", %d, %d) = ",
	    this->str, this->str2, command[i].index, command[i].length);
	printf("\\\"%%s\\\"\\n\",\n\t\t    substr(\"%s%s\", %d, %d));\n",
	    this->str, this->str2, command[i].index, command[i].length);
	printf("\t\tprintf(\"   D => substr(\\\"%s%s\\\", %d, %d) = ",
	    this->str, this->str2, command[i].index, command[i].length);
	printf("\\\"%%s\\\"\\n\",\n\t\t    \"%s\");\n", this->result);
	printf("\t\t$failed++;\n");
	printf("\t}\n\n");
}

tick-1ms
/++i == end/
{
	printf("\texit($failed);\n}\n");
	exit(0);
}
