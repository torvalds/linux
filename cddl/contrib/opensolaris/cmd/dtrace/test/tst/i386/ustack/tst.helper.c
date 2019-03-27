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

#include <stdint.h>
#include <stdlib.h>
#include <strings.h>

int
baz(void)
{
	return (8);
}

static int
foo(void)
{
	/*
	 * In order to assure that our helper is properly employed to identify
	 * the frame, we're going to trampoline through data.
	 */
	uint8_t instr[] = {
	    0x55,			/* pushl %ebp		*/
	    0x8b, 0xec,			/* movl  %esp, %ebp	*/
	    0xe8, 0x0, 0x0, 0x0, 0x0,	/* call  baz		*/
	    0x8b, 0xe5,			/* movl  %ebp, %esp	*/
	    0x5d,			/* popl  %ebp		*/
	    0xc3			/* ret			*/
	};
	uint8_t *fp = malloc(sizeof (instr));

	/*
	 * Do our little relocation dance.
	 */
	*((int *)&instr[4]) = (uintptr_t)baz - (uintptr_t)&fp[8];

	/*
	 * Copy the code to the heap (it's a pain to build in ON with an
	 * executable stack).
	 */
	bcopy(instr, fp, sizeof (instr));

	(*(int (*)(void))fp)();

	free(fp);

	return (0);
}

int
main(int argc, char **argv)
{
	for (;;) {
		foo();
	}

	return (0);
}
