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

#pragma	ident	"%Z%%M%	%I%	%E% SMI"

/*
 * ASSERTION:
 * 	test copyinto by copying the first string of the user's
 *	environment.
 *
 * SECTION: Actions and Subroutines/copyinto()
 *
 */

#pragma D option quiet

BEGIN
/curpsinfo->pr_dmodel == PR_MODEL_ILP32/
{
	envp = alloca(sizeof (uint32_t));
	copyinto(curpsinfo->pr_envp, sizeof (uint32_t), envp);
	printf("envp[0] = \"%s\"", copyinstr(*(uint32_t *)envp));
	exit(0);
}

BEGIN
/curpsinfo->pr_dmodel == PR_MODEL_LP64/
{
	envp = alloca(sizeof (uint64_t));
	copyinto(curpsinfo->pr_envp, sizeof (uint64_t), envp);
	printf("envp[0] = \"%s\"", copyinstr(*(uint64_t *)envp));
	exit(0);
}
