/*
 * linux/arch/unicore32/lib/backtrace.S
 *
 * Code specific to PKUnity SoC and UniCore ISA
 *
 * Copyright (C) 2001-2010 GUAN Xue-tao
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/linkage.h>
#include <asm/assembler.h>
		.text

@ fp is 0 or stack frame

#define frame	v4
#define sv_fp	v5
#define sv_pc	v6
#define offset	v8

ENTRY(__backtrace)
		mov	r0, fp

ENTRY(c_backtrace)

#if !defined(CONFIG_FRAME_POINTER) || !defined(CONFIG_PRINTK)
		mov	pc, lr
ENDPROC(__backtrace)
ENDPROC(c_backtrace)
#else
		stm.w	(v4 - v8, lr), [sp-]	@ Save an extra register
						@ so we have a location...
		mov.a	frame, r0		@ if frame pointer is zero
		beq	no_frame		@ we have no stack frames

1:		stm.w	(pc), [sp-]		@ calculate offset of PC stored
		ldw.w	r0, [sp]+, #4		@ by stmfd for this CPU
		adr	r1, 1b
		sub	offset, r0, r1

/*
 * Stack frame layout:
 *             optionally saved caller registers (r4 - r10)
 *             saved fp
 *             saved sp
 *             saved lr
 *    frame => saved pc
 *             optionally saved arguments (r0 - r3)
 * saved sp => <next word>
 *
 * Functions start with the following code sequence:
 *                  mov   ip, sp
 *                  stm.w (r0 - r3), [sp-] (optional)
 * corrected pc =>  stm.w sp, (..., fp, ip, lr, pc)
 */
for_each_frame:

1001:		ldw	sv_pc, [frame+], #0	@ get saved pc
1002:		ldw	sv_fp, [frame+], #-12	@ get saved fp

		sub	sv_pc, sv_pc, offset	@ Correct PC for prefetching

1003:		ldw	r2, [sv_pc+], #-4	@ if stmfd sp, {args} exists,
		ldw	r3, .Ldsi+4		@ adjust saved 'pc' back one
		cxor.a	r3, r2 >> #14		@ instruction
		beq	201f
		sub	r0, sv_pc, #4		@ allow for mov
		b	202f
201:
		sub	r0, sv_pc, #8		@ allow for mov + stmia
202:
		ldw	r1, [frame+], #-4	@ get saved lr
		mov	r2, frame
		b.l	dump_backtrace_entry

		ldw	r1, [sv_pc+], #-4	@ if stmfd sp, {args} exists,
		ldw	r3, .Ldsi+4
		cxor.a	r3, r1 >> #14
		bne	1004f
		ldw	r0, [frame+], #-8	@ get sp
		sub	r0, r0, #4		@ point at the last arg
		b.l	.Ldumpstm		@ dump saved registers

1004:		ldw	r1, [sv_pc+], #0	@ if stmfd {, fp, ip, lr, pc}
		ldw	r3, .Ldsi		@ instruction exists,
		cxor.a	r3, r1 >> #14
		bne	201f
		sub	r0, frame, #16
		b.l	.Ldumpstm		@ dump saved registers
201:
		cxor.a	sv_fp, #0		@ zero saved fp means
		beq	no_frame		@ no further frames

		csub.a	sv_fp, frame		@ next frame must be
		mov	frame, sv_fp		@ above the current frame
		bua	for_each_frame

1006:		adr	r0, .Lbad
		mov	r1, frame
		b.l	printk
no_frame:	ldm.w	(v4 - v8, pc), [sp]+
ENDPROC(__backtrace)
ENDPROC(c_backtrace)

		.pushsection __ex_table,"a"
		.align	3
		.long	1001b, 1006b
		.long	1002b, 1006b
		.long	1003b, 1006b
		.long	1004b, 1006b
		.popsection

#define instr v4
#define reg   v5
#define stack v6

.Ldumpstm:	stm.w	(instr, reg, stack, v7, lr), [sp-]
		mov	stack, r0
		mov	instr, r1
		mov	reg, #14
		mov	v7, #0
1:		mov	r3, #1
		csub.a	reg, #8
		bne	201f
		sub	reg, reg, #3
201:
		cand.a	instr, r3 << reg
		beq	2f
		add	v7, v7, #1
		cxor.a	v7, #6
		cmoveq	v7, #1
		cmoveq	r1, #'\n'
		cmovne	r1, #' '
		ldw.w	r3, [stack]+, #-4
		mov	r2, reg
		csub.a	r2, #8
		bsl	201f
		sub	r2, r2, #3
201:
		cand.a	instr, #0x40		@ if H is 1, high 16 regs
		beq	201f
		add	r2, r2, #0x10		@ so r2 need add 16
201:
		adr	r0, .Lfp
		b.l	printk
2:		sub.a	reg, reg, #1
		bns	1b
		cxor.a	v7, #0
		beq	201f
		adr	r0, .Lcr
		b.l	printk
201:		ldm.w	(instr, reg, stack, v7, pc), [sp]+

.Lfp:		.asciz	"%cr%d:%08x"
.Lcr:		.asciz	"\n"
.Lbad:		.asciz	"Backtrace aborted due to bad frame pointer <%p>\n"
		.align
.Ldsi:		.word	0x92eec000 >> 14	@ stm.w sp, (... fp, ip, lr, pc)
		.word	0x92e10000 >> 14	@ stm.w sp, ()

#endif
