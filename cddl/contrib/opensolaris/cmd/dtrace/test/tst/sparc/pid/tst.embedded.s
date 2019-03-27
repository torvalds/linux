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

#include <sys/asm_linkage.h>

	DGDEF(__fsr_init_value)
	.word 0

	ENTRY(waiting)
	retl
	ldub	[%o0], %o0
	SET_SIZE(waiting)

	ENTRY(main)
	save	%sp, -SA(MINFRAME + 4), %sp
	stb	%g0, [%fp - 4]
1:
	call	waiting
	sub	%fp, 4, %o0
	tst	%o0
	bz	1b
	nop

	restore

	ALTENTRY(inner)
	nop
	nop
	nop
	SET_SIZE(inner)

	retl
	clr	%o0
	SET_SIZE(main)
