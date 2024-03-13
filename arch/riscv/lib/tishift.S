/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2018 Free Software Foundation, Inc.
 */

#include <linux/linkage.h>
#include <asm-generic/export.h>

SYM_FUNC_START(__lshrti3)
	beqz	a2, .L1
	li	a5,64
	sub	a5,a5,a2
	sext.w	a4,a5
	blez	a5, .L2
	sext.w	a2,a2
	srl	a0,a0,a2
	sll	a4,a1,a4
	srl	a2,a1,a2
	or	a0,a0,a4
	mv	a1,a2
.L1:
	ret
.L2:
	negw	a0,a4
	li	a2,0
	srl	a0,a1,a0
	mv	a1,a2
	ret
SYM_FUNC_END(__lshrti3)
EXPORT_SYMBOL(__lshrti3)

SYM_FUNC_START(__ashrti3)
	beqz	a2, .L3
	li	a5,64
	sub	a5,a5,a2
	sext.w	a4,a5
	blez	a5, .L4
	sext.w	a2,a2
	srl	a0,a0,a2
	sll	a4,a1,a4
	sra	a2,a1,a2
	or	a0,a0,a4
	mv	a1,a2
.L3:
	ret
.L4:
	negw	a0,a4
	srai	a2,a1,0x3f
	sra	a0,a1,a0
	mv	a1,a2
	ret
SYM_FUNC_END(__ashrti3)
EXPORT_SYMBOL(__ashrti3)

SYM_FUNC_START(__ashlti3)
	beqz	a2, .L5
	li	a5,64
	sub	a5,a5,a2
	sext.w	a4,a5
	blez	a5, .L6
	sext.w	a2,a2
	sll	a1,a1,a2
	srl	a4,a0,a4
	sll	a2,a0,a2
	or	a1,a1,a4
	mv	a0,a2
.L5:
	ret
.L6:
	negw	a1,a4
	li	a2,0
	sll	a1,a0,a1
	mv	a0,a2
	ret
SYM_FUNC_END(__ashlti3)
EXPORT_SYMBOL(__ashlti3)
