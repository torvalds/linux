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

	ENTRY(waiting)
	pushl	%ebp
	movl	%esp, %ebp
	movl	8(%ebp), %eax
	movl	(%eax), %eax
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

	addl	$0x4, %esp

	xorl	%eax, %eax
	testl	%eax, %eax
	jz	other

	ALTENTRY(bad)
	movl	0x0, %eax
	SET_SIZE(bad)
	SET_SIZE(main)

	ENTRY(other)
	xorl	%eax, %eax
	popl	%ebp
	ret
	SET_SIZE(other)
