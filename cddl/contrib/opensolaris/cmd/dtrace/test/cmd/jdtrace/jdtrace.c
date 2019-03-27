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
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <alloca.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/systeminfo.h>

int
main(int argc, char **argv)
{
	int i, ac, has64;
	char **av, **p;

	ac = argc + 3;
	av = p = alloca(sizeof (char *) * ac);

	*p++ = "java";
	*p++ = "-jar";
	*p++ = "/opt/SUNWdtrt/lib/java/jdtrace.jar";

	argc--;
	argv++;

	for (i = 0; i < argc; i++) {
		p[i] = argv[i];
	}
	p[i] = NULL;

	(void) execvp(av[0], av);

	perror("exec failed");

	return (0);
}
