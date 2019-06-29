/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2018 Free Software Foundation, Inc.
 */

#include <linux/linkage.h>

ENTRY(__lshrti3)
	beqz	a2, .L1
	li	a5,64
	sub	a5,a5,a2
	addi	sp,sp,-16
	sext.w	a4,a5
	blez	a5, .L2
	sext.w	a2,a2
	sll	a4,a1,a4
	srl	a0,a0,a2
	srl	a1,a1,a2
	or	a0,a0,a4
	sd	a1,8(sp)
	sd	a0,0(sp)
	ld	a0,0(sp)
	ld	a1,8(sp)
	addi	sp,sp,16
	ret
.L1:
	ret
.L2:
	negw	a4,a4
	srl	a1,a1,a4
	sd	a1,0(sp)
	sd	zero,8(sp)
	ld	a0,0(sp)
	ld	a1,8(sp)
	addi	sp,sp,16
	ret
ENDPROC(__lshrti3)
