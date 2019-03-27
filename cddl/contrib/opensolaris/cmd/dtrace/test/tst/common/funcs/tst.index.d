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

struct {
	string str;
	string substr;
	int haspos;
	int position;
} command[int];

int i;

BEGIN
{
	command[i].str = "foobarbaz";
	command[i].substr = "barbaz";
	i++;

	command[i].str = "foofoofoo";
	command[i].substr = "foo";
	i++;

	command[i].str = "boofoofoo";
	command[i].substr = "foo";
	i++;

	command[i].str = "foobarbaz";
	command[i].substr = "barbazzy";
	i++;

	command[i].str = "foobar";
	command[i].substr = "foobar";
	i++;

	command[i].str = "foobar";
	command[i].substr = "foobarbaz";
	i++;

	command[i].str = "";
	command[i].substr = "foobar";
	i++;

	command[i].str = "foobar";
	command[i].substr = "";
	i++;

	command[i].str = "";
	command[i].substr = "";
	i++;

	command[i].str = "foo";
	command[i].substr = "";
	i++;

	end = j = k = 0;
	printf("#!/usr/bin/perl\n\nBEGIN {\n");
}

tick-1ms
/j < i && end == 0/
{
	command[i + k].str = command[j].str;
	command[i + k].substr = command[j].substr;
	command[i + k].haspos = 1;
	command[i + k].position = -400;
	k++;

	command[i + k].str = command[j].str;
	command[i + k].substr = command[j].substr;
	command[i + k].haspos = 1;
	command[i + k].position = -1;
	k++;

	command[i + k].str = command[j].str;
	command[i + k].substr = command[j].substr;
	command[i + k].haspos = 1;
	command[i + k].position = 0;
	k++;

	command[i + k].str = command[j].str;
	command[i + k].substr = command[j].substr;
	command[i + k].haspos = 1;
	command[i + k].position = strlen(command[j].str) / 2;
	k++;

	command[i + k].str = command[j].str;
	command[i + k].substr = command[j].substr;
	command[i + k].haspos = 1;
	command[i + k].position = strlen(command[j].str);
	k++;

	command[i + k].str = command[j].str;
	command[i + k].substr = command[j].substr;
	command[i + k].haspos = 1;
	command[i + k].position = strlen(command[j].str) + 1;
	k++;

	command[i + k].str = command[j].str;
	command[i + k].substr = command[j].substr;
	command[i + k].haspos = 1;
	command[i + k].position = strlen(command[j].str) + 2;
	k++;

	command[i + k].str = command[j].str;
	command[i + k].substr = command[j].substr;
	command[i + k].haspos = 1;
	command[i + k].position = 400;
	k++;

	j++;
}

tick-1ms
/j == i && end == 0/
{
	end = k;
	i = 0;
}

tick-1ms
/end != 0 && i < end && !command[i].haspos/
{
	this->result = index(command[i].str, command[i].substr);

	printf("\tif (index(\"%s\", \"%s\") != %d) {\n",
	    command[i].str, command[i].substr, this->result);
	printf("\t\tprintf(\"perl => index(\\\"%s\\\", \\\"%s\\\") = ",
	    command[i].str, command[i].substr);
	printf("%%d\\n\",\n\t\t    index(\"%s\", \"%s\"));\n",
	    command[i].str, command[i].substr);
	printf("\t\tprintf(\"   D => index(\\\"%s\\\", \\\"%s\\\") = ",
	    command[i].str, command[i].substr);
	printf("%d\\n\");\n", this->result);
	printf("\t\t$failed++;\n");
	printf("\t}\n\n");
}

tick-1ms
/end != 0 && i < end && !command[i].haspos/
{
	this->result = rindex(command[i].str, command[i].substr);

	printf("\tif (rindex(\"%s\", \"%s\") != %d) {\n",
	    command[i].str, command[i].substr, this->result);
	printf("\t\tprintf(\"perl => rindex(\\\"%s\\\", \\\"%s\\\") = ",
	    command[i].str, command[i].substr);
	printf("%%d\\n\",\n\t\t    rindex(\"%s\", \"%s\"));\n",
	    command[i].str, command[i].substr);
	printf("\t\tprintf(\"   D => rindex(\\\"%s\\\", \\\"%s\\\") = ",
	    command[i].str, command[i].substr);
	printf("%d\\n\");\n", this->result);
	printf("\t\t$failed++;\n");
	printf("\t}\n\n");
}

tick-1ms
/end != 0 && i < end && command[i].haspos/
{
	this->result = index(command[i].str,
	    command[i].substr, command[i].position);

	printf("\tif (index(\"%s\", \"%s\", %d) != %d) {\n", command[i].str,
	    command[i].substr, command[i].position, this->result);
	printf("\t\tprintf(\"perl => index(\\\"%s\\\", \\\"%s\\\", %d) = ",
	    command[i].str, command[i].substr, command[i].position);
	printf("%%d\\n\",\n\t\t    index(\"%s\", \"%s\", %d));\n",
	    command[i].str, command[i].substr, command[i].position);
	printf("\t\tprintf(\"   D => index(\\\"%s\\\", \\\"%s\\\", %d) = ",
	    command[i].str, command[i].substr, command[i].position);
	printf("%d\\n\");\n", this->result);
	printf("\t\t$failed++;\n");
	printf("\t}\n\n");
}

tick-1ms
/end != 0 && i < end && command[i].haspos/
{
	this->result = rindex(command[i].str,
	    command[i].substr, command[i].position);

	printf("\tif (rindex(\"%s\", \"%s\", %d) != %d) {\n", command[i].str,
	    command[i].substr, command[i].position, this->result);
	printf("\t\tprintf(\"perl => rindex(\\\"%s\\\", \\\"%s\\\", %d) = ",
	    command[i].str, command[i].substr, command[i].position);
	printf("%%d\\n\",\n\t\t    rindex(\"%s\", \"%s\", %d));\n",
	    command[i].str, command[i].substr, command[i].position);
	printf("\t\tprintf(\"   D => rindex(\\\"%s\\\", \\\"%s\\\", %d) = ",
	    command[i].str, command[i].substr, command[i].position);
	printf("%d\\n\");\n", this->result);
	printf("\t\t$failed++;\n");
	printf("\t}\n\n");
}

tick-1ms
/end != 0 && ++i == end/
{
	printf("\texit($failed);\n}\n");
	exit(0);
}
