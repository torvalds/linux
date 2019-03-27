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
 * ASSERTION:
 *   Declare a dynamic type and then use it to copyin the first 3 environment
 *   variable pointers from the current process.
 *
 * SECTION: Structs and Unions/Structs;
 *	Actions and Subroutines/copyin();
 * 	Actions and Subroutines/copyinstr();
 *	Variables/External Variables
 *
 * NOTES:
 *  This test program declares a dynamic type and then uses it to copyin the
 *  first three environment variable pointers from the current process.  We
 *  then use the dynamic type to access the result of our copyin().  The
 *  special "D" module type scope is also tested.
 */

#pragma D option quiet

struct env_vars_32 {
	uint32_t e1;
	uint32_t e2;
	uint32_t e3;
};

struct env_vars_64 {
	uint64_t e1;
	uint64_t e2;
	uint64_t e3;
};

BEGIN
/curpsinfo->pr_dmodel == PR_MODEL_ILP32/
{
	e32 = (struct D`env_vars_32 *)
	    copyin(curpsinfo->pr_envp, sizeof (struct D`env_vars_32));

	printf("e1 = \"%s\"\n", stringof(copyinstr(e32->e1)));
	printf("e2 = \"%s\"\n", stringof(copyinstr(e32->e2)));
	printf("e3 = \"%s\"\n", stringof(copyinstr(e32->e3)));

	exit(0);
}

BEGIN
/curpsinfo->pr_dmodel == PR_MODEL_LP64/
{
	e64 = (struct D`env_vars_64 *)
	    copyin(curpsinfo->pr_envp, sizeof (struct D`env_vars_64));

	printf("e1 = \"%s\"\n", stringof(copyinstr(e64->e1)));
	printf("e2 = \"%s\"\n", stringof(copyinstr(e64->e2)));
	printf("e3 = \"%s\"\n", stringof(copyinstr(e64->e3)));

	exit(0);
}
