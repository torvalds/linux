/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * linux/arch/unicore32/lib/delay.S
 *
 * Code specific to PKUnity SoC and UniCore ISA
 *
 * Copyright (C) 2001-2010 GUAN Xue-tao
 */
#include <linux/linkage.h>
#include <asm/assembler.h>
#include <asm/param.h>
		.text

.LC0:		.word	loops_per_jiffy
.LC1:		.word	(2199023*HZ)>>11

/*
 * r0  <= 2000
 * lpj <= 0x01ffffff (max. 3355 bogomips)
 * HZ  <= 1000
 */

ENTRY(__udelay)
		ldw	r2, .LC1
		mul	r0, r2, r0
ENTRY(__const_udelay)				@ 0 <= r0 <= 0x7fffff06
		ldw	r2, .LC0
		ldw	r2, [r2]		@ max = 0x01ffffff
		mov	r0, r0 >> #14		@ max = 0x0001ffff
		mov	r2, r2 >> #10		@ max = 0x00007fff
		mul	r0, r2, r0		@ max = 2^32-1
		mov.a	r0, r0 >> #6
		cmoveq	pc, lr

/*
 * loops = r0 * HZ * loops_per_jiffy / 1000000
 *
 * Oh, if only we had a cycle counter...
 */

@ Delay routine
ENTRY(__delay)
		sub.a	r0, r0, #2
		bua	__delay
		mov	pc, lr
ENDPROC(__udelay)
ENDPROC(__const_udelay)
ENDPROC(__delay)
