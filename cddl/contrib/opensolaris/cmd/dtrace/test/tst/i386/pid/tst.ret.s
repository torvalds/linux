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
	.long 0

	ENTRY(ret1)
	ret
	SET_SIZE(ret1)

	ENTRY(ret2)
	repz
	ret
	SET_SIZE(ret2)

	ENTRY(ret3)
	ret	$0
	SET_SIZE(ret3)

	ENTRY(ret4)
	repz
	ret	$0
	SET_SIZE(ret4)

	ENTRY(ret5)
	pushl	(%esp)
	ret	$4
	SET_SIZE(ret5)

	ENTRY(ret6)
	pushl	(%esp)
	repz
	ret	$4
	SET_SIZE(ret6)

	ENTRY(waiting)
	pushl	%ebp
	movl	%esp, %ebp
	movl	8(%ebp), %eax
	movl	(%eax), %eax
	movl	%ebp, %esp
	popl	%ebp
	ret
	SET_SIZE(waiting)

	ENTRY(main)
	pushl	%ebp
	movl	%esp, %ebp
	subl	$0x4, %esp
	movl	$0x0, -4(%ebp)

1:
	leal	-4(%ebp), %eax
	pushl	%eax
	call	waiting
	addl	$0x4, %esp

	testl	%eax, %eax
	jz	1b

	movl	%esp, %esi

	call	ret1
	call	ret2
	call	ret3
	call	ret4
	call	ret5
	call	ret6

	cmpl	%esp, %esi
	jne	1f

	ALTENTRY(done)
	nop
	SET_SIZE(done)

	movl	$0, %eax
	movl	%ebp, %esp
	popl	%ebp
	ret

1:
	movl	$1, %eax
	movl	%ebp, %esp
	popl	%ebp
	ret
	SET_SIZE(main)
