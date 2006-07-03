/*
 *  linux/arch/arm26/lib/backtrace.S
 *
 *  Copyright (C) 1995, 1996 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/linkage.h>
#include <asm/assembler.h>
		.text

@ fp is 0 or stack frame

#define frame	r4
#define next	r5
#define save	r6
#define mask	r7
#define offset	r8

ENTRY(__backtrace)
		mov	r1, #0x10
		mov	r0, fp

ENTRY(c_backtrace)

#ifdef CONFIG_NO_FRAME_POINTER
		mov	pc, lr
#else

		stmfd	sp!, {r4 - r8, lr}	@ Save an extra register so we have a location...
		mov	mask, #0xfc000003
		tst	mask, r0
		movne	r0, #0
		movs	frame, r0
1:		moveq	r0, #-2
		LOADREGS(eqfd, sp!, {r4 - r8, pc})

2:		stmfd	sp!, {pc}		@ calculate offset of PC in STMIA instruction
		ldr	r0, [sp], #4
		adr	r1, 2b - 4
		sub	offset, r0, r1

3:		tst	frame, mask		@ Check for address exceptions...
		bne	1b

1001:		ldr	next, [frame, #-12]	@ get fp
1002:		ldr	r2, [frame, #-4]	@ get lr
1003:		ldr	r3, [frame, #0]		@ get pc
		sub	save, r3, offset	@ Correct PC for prefetching
		bic	save, save, mask
1004:		ldr	r1, [save, #0]		@ get instruction at function
		mov	r1, r1, lsr #10
		ldr	r3, .Ldsi+4
		teq	r1, r3
		subeq	save, save, #4
		adr	r0, .Lfe
		mov	r1, save
		bic	r2, r2, mask
		bl	printk			@ print pc and link register

		ldr	r0, [frame, #-8]	@ get sp
		sub	r0, r0, #4
1005:		ldr	r1, [save, #4]		@ get instruction at function+4
		mov	r3, r1, lsr #10
		ldr	r2, .Ldsi+4
		teq	r3, r2			@ Check for stmia sp!, {args}
		addeq	save, save, #4		@ next instruction
		bleq	.Ldumpstm

		sub	r0, frame, #16
1006:		ldr	r1, [save, #4]		@ Get 'stmia sp!, {rlist, fp, ip, lr, pc}' instruction
		mov	r3, r1, lsr #10
		ldr	r2, .Ldsi
		teq	r3, r2
		bleq	.Ldumpstm

		teq	frame, next
		movne	frame, next
		teqne	frame, #0
		bne	3b
		LOADREGS(fd, sp!, {r4 - r8, pc})

/*
 * Fixup for LDMDB
 */
		.section .fixup,"ax"
		.align	0
1007:		ldr	r0, =.Lbad
		mov	r1, frame
		bl	printk
		LOADREGS(fd, sp!, {r4 - r8, pc})
		.ltorg
		.previous
		
		.section __ex_table,"a"
		.align	3
		.long	1001b, 1007b
		.long	1002b, 1007b
		.long	1003b, 1007b
		.long	1004b, 1007b
		.long	1005b, 1007b
		.long	1006b, 1007b
		.previous

#define instr r4
#define reg   r5
#define stack r6

.Ldumpstm:	stmfd	sp!, {instr, reg, stack, r7, lr}
		mov	stack, r0
		mov	instr, r1
		mov	reg, #9
		mov	r7, #0
1:		mov	r3, #1
		tst	instr, r3, lsl reg
		beq	2f
		add	r7, r7, #1
		teq	r7, #4
		moveq	r7, #0
		moveq	r3, #'\n'
		movne	r3, #' '
		ldr	r2, [stack], #-4
		mov	r1, reg
		adr	r0, .Lfp
		bl	printk
2:		subs	reg, reg, #1
		bpl	1b
		teq	r7, #0
		adrne	r0, .Lcr
		blne	printk
		mov	r0, stack
		LOADREGS(fd, sp!, {instr, reg, stack, r7, pc})

.Lfe:		.asciz	"Function entered at [<%p>] from [<%p>]\n"
.Lfp:		.asciz	" r%d = %08X%c"
.Lcr:		.asciz	"\n"
.Lbad:		.asciz	"Backtrace aborted due to bad frame pointer <%p>\n"
		.align
.Ldsi:		.word	0x00e92dd8 >> 2
		.word	0x00e92d00 >> 2

#endif
