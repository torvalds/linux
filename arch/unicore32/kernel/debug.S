/*
 * linux/arch/unicore32/kernel/debug.S
 *
 * Code specific to PKUnity SoC and UniCore ISA
 *
 * Copyright (C) 2001-2010 GUAN Xue-tao
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  32-bit debugging code
 */
#include <linux/linkage.h>
#include <asm/assembler.h>

		.text

/*
 * Some debugging routines (useful if you've got MM problems and
 * printk isn't working).  For DEBUGGING ONLY!!!  Do not leave
 * references to these in a production kernel!
 */
#include "debug-macro.S"

/*
 * Useful debugging routines
 */
ENTRY(printhex8)
		mov	r1, #8
		b	printhex
ENDPROC(printhex8)

ENTRY(printhex4)
		mov	r1, #4
		b	printhex
ENDPROC(printhex4)

ENTRY(printhex2)
		mov	r1, #2
printhex:	adr	r2, hexbuf
		add	r3, r2, r1
		mov	r1, #0
		stb	r1, [r3]
1:		and	r1, r0, #15
		mov	r0, r0 >> #4
		csub.a	r1, #10
		beg	2f
		add	r1, r1, #'0' - 'a' + 10
2:		add	r1, r1, #'a' - 10
		stb.w	r1, [r3+], #-1
		cxor.a	r3, r2
		bne	1b
		mov	r0, r2
		b	printascii
ENDPROC(printhex2)

		.ltorg

ENTRY(printascii)
		addruart r3
		b	2f
1:		waituart r2, r3
		senduart r1, r3
		busyuart r2, r3
		cxor.a	r1, #'\n'
		cmoveq	r1, #'\r'
		beq	1b
2:		cxor.a	r0, #0
		beq	3f
		ldb.w	r1, [r0]+, #1
		cxor.a	r1, #0
		bne	1b
3:		mov	pc, lr
ENDPROC(printascii)

ENTRY(printch)
		addruart r3
		mov	r1, r0
		mov	r0, #0
		b	1b
ENDPROC(printch)

hexbuf:		.space 16

