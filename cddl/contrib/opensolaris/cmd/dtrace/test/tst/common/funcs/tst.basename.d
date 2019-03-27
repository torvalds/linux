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
#pragma D option dynvarsize=2m

/*
 * This test verifies that the basename() and dirname() functions are working
 * properly.  Note that the output of this is a ksh script.  When run,
 * it will give no output if the output is correct.
 */
BEGIN
{
	dir[i++] = "/foo/bar/baz";
	dir[i++] = "/foo/bar///baz/";
	dir[i++] = "/foo/bar/baz/";
	dir[i++] = "/foo/bar/baz//";
	dir[i++] = "/foo/bar/baz/.";
	dir[i++] = "/foo/bar/baz/./";
	dir[i++] = "/foo/bar/baz/.//";
	dir[i++] = "foo/bar/baz/";
	dir[i++] = "/";
	dir[i++] = "./";
	dir[i++] = "//";
	dir[i++] = "/.";
	dir[i++] = "/./";
	dir[i++] = "/./.";
	dir[i++] = "/.//";
	dir[i++] = ".";
	dir[i++] = "f";
	dir[i++] = "f/";
	dir[i++] = "/////";
	/*
	 * basename(3) and basename(1) return different results for the empty
	 * string on FreeBSD, so we need special handling.
	dir[i++] = "";
	*/

	end = i;
	i = 0;

	printf("#!/usr/bin/env ksh\n\n");
}

tick-1ms
/i < end/
{
	printf("if [ `basename \"%s\"` != \"%s\" ]; then\n",
	    dir[i], basename(dir[i]));
	printf("	echo \"basename(\\\"%s\\\") is \\\"%s\\\"; ",
	    dir[i], basename(dir[i]));
	printf("expected \\\"`basename \"%s\"`\"\\\"\n", dir[i]);
	printf("fi\n\n");
	printf("if [ `dirname \"%s\"` != \"%s\" ]; then\n",
	    dir[i], dirname(dir[i]));
	printf("	echo \"dirname(\\\"%s\\\") is \\\"%s\\\"; ",
	    dir[i], dirname(dir[i]));
	printf("expected \\\"`dirname \"%s\"`\"\\\"\n", dir[i]);
	printf("fi\n\n");
	i++;
}

tick-1ms
/i == end/
{
	dir[i] = "";
	printf("if [ \"`basename \"%s\"`\" != \"%s\" -a \".\" != \"%s\" ]; then\n",
	    dir[i], basename(dir[i]), basename(dir[i]));
	printf("	echo \"basename(\\\"%s\\\") is \\\"%s\\\"; ",
	    dir[i], basename(dir[i]));
	printf("expected \\\"`basename \"%s\"`\\\" or \\\".\\\"\"\n", dir[i]);
	printf("fi\n\n");
	printf("if [ `dirname \"%s\"` != \"%s\" ]; then\n",
	    dir[i], dirname(dir[i]));
	printf("	echo \"dirname(\\\"%s\\\") is \\\"%s\\\"; ",
	    dir[i], dirname(dir[i]));
	printf("expected \\\"`dirname \"%s\"`\"\\\"\n", dir[i]);
	printf("fi\n\n");

	exit(0);
}
