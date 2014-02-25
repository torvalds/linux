/*
 *  arch/arm/include/asm/assembler.h
 *
 *  Copyright (C) 1996-2000 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  This file contains arm architecture specific defines
 *  for the different processors.
 *
 *  Do not include any C declarations in this file - it is included by
 *  assembler source.
 */
#ifndef __ASM_ASSEMBLER_H__
#define __ASM_ASSEMBLER_H__

#ifndef __ASSEMBLY__
#error "Only include this from assembly code"
#endif

#include <asm/ptrace.h>
#include <asm/domain.h>
#include <asm/opcodes-virt.h>

#define IOMEM(x)	(x)

/*
 * Endian independent macros for shifting bytes within registers.
 */
#ifndef __ARMEB__
#define lspull          lsr
#define lspush          lsl
#define get_byte_0      lsl #0
#define get_byte_1	lsr #8
#define get_byte_2	lsr #16
#define get_byte_3	lsr #24
#define put_byte_0      lsl #0
#define put_byte_1	lsl #8
#define put_byte_2	lsl #16
#define put_byte_3	lsl #24
#else
#define lspull          lsl
#define lspush          lsr
#define get_byte_0	lsr #24
#define get_byte_1	lsr #16
#define get_byte_2	lsr #8
#define get_byte_3      lsl #0
#define put_byte_0	lsl #24
#define put_byte_1	lsl #16
#define put_byte_2	lsl #8
#define put_byte_3      lsl #0
#endif

/*
 * Data preload for architectures that support it
 */
#if __LINUX_ARM_ARCH__ >= 5
#define PLD(code...)	code
#else
#define PLD(code...)
#endif

/*
 * This can be used to enable code to cacheline align the destination
 * pointer when bulk writing to memory.  Experiments on StrongARM and
 * XScale didn't show this a worthwhile thing to do when the cache is not
 * set to write-allocate (this would need further testing on XScale when WA
 * is used).
 *
 * On Feroceon there is much to gain however, regardless of cache mode.
 */
#ifdef CONFIG_CPU_FEROCEON
#define CALGN(code...) code
#else
#define CALGN(code...)
#endif

/*
 * Enable and disable interrupts
 */
#if __LINUX_ARM_ARCH__ >= 6
	.macro	disable_irq_notrace
	cpsid	i
	.endm

	.macro	enable_irq_notrace
	cpsie	i
	.endm
#else
	.macro	disable_irq_notrace
	msr	cpsr_c, #PSR_I_BIT | SVC_MODE
	.endm

	.macro	enable_irq_notrace
	msr	cpsr_c, #SVC_MODE
	.endm
#endif

	.macro asm_trace_hardirqs_off
#if defined(CONFIG_TRACE_IRQFLAGS)
	stmdb   sp!, {r0-r3, ip, lr}
	bl	trace_hardirqs_off
	ldmia	sp!, {r0-r3, ip, lr}
#endif
	.endm

	.macro asm_trace_hardirqs_on_cond, cond
#if defined(CONFIG_TRACE_IRQFLAGS)
	/*
	 * actually the registers should be pushed and pop'd conditionally, but
	 * after bl the flags are certainly clobbered
	 */
	stmdb   sp!, {r0-r3, ip, lr}
	bl\cond	trace_hardirqs_on
	ldmia	sp!, {r0-r3, ip, lr}
#endif
	.endm

	.macro asm_trace_hardirqs_on
	asm_trace_hardirqs_on_cond al
	.endm

	.macro disable_irq
	disable_irq_notrace
	asm_trace_hardirqs_off
	.endm

	.macro enable_irq
	asm_trace_hardirqs_on
	enable_irq_notrace
	.endm
/*
 * Save the current IRQ state and disable IRQs.  Note that this macro
 * assumes FIQs are enabled, and that the processor is in SVC mode.
 */
	.macro	save_and_disable_irqs, oldcpsr
	mrs	\oldcpsr, cpsr
	disable_irq
	.endm

	.macro	save_and_disable_irqs_notrace, oldcpsr
	mrs	\oldcpsr, cpsr
	disable_irq_notrace
	.endm

/*
 * Restore interrupt state previously stored in a register.  We don't
 * guarantee that this will preserve the flags.
 */
	.macro	restore_irqs_notrace, oldcpsr
	msr	cpsr_c, \oldcpsr
	.endm

	.macro restore_irqs, oldcpsr
	tst	\oldcpsr, #PSR_I_BIT
	asm_trace_hardirqs_on_cond eq
	restore_irqs_notrace \oldcpsr
	.endm

#define USER(x...)				\
9999:	x;					\
	.pushsection __ex_table,"a";		\
	.align	3;				\
	.long	9999b,9001f;			\
	.popsection

#ifdef CONFIG_SMP
#define ALT_SMP(instr...)					\
9998:	instr
/*
 * Note: if you get assembler errors from ALT_UP() when building with
 * CONFIG_THUMB2_KERNEL, you almost certainly need to use
 * ALT_SMP( W(instr) ... )
 */
#define ALT_UP(instr...)					\
	.pushsection ".alt.smp.init", "a"			;\
	.long	9998b						;\
9997:	instr							;\
	.if . - 9997b != 4					;\
		.error "ALT_UP() content must assemble to exactly 4 bytes";\
	.endif							;\
	.popsection
#define ALT_UP_B(label)					\
	.equ	up_b_offset, label - 9998b			;\
	.pushsection ".alt.smp.init", "a"			;\
	.long	9998b						;\
	W(b)	. + up_b_offset					;\
	.popsection
#else
#define ALT_SMP(instr...)
#define ALT_UP(instr...) instr
#define ALT_UP_B(label) b label
#endif

/*
 * Instruction barrier
 */
	.macro	instr_sync
#if __LINUX_ARM_ARCH__ >= 7
	isb
#elif __LINUX_ARM_ARCH__ == 6
	mcr	p15, 0, r0, c7, c5, 4
#endif
	.endm

/*
 * SMP data memory barrier
 */
	.macro	smp_dmb mode
#ifdef CONFIG_SMP
#if __LINUX_ARM_ARCH__ >= 7
	.ifeqs "\mode","arm"
	ALT_SMP(dmb)
	.else
	ALT_SMP(W(dmb))
	.endif
#elif __LINUX_ARM_ARCH__ == 6
	ALT_SMP(mcr	p15, 0, r0, c7, c10, 5)	@ dmb
#else
#error Incompatible SMP platform
#endif
	.ifeqs "\mode","arm"
	ALT_UP(nop)
	.else
	ALT_UP(W(nop))
	.endif
#endif
	.endm

#ifdef CONFIG_THUMB2_KERNEL
	.macro	setmode, mode, reg
	mov	\reg, #\mode
	msr	cpsr_c, \reg
	.endm
#else
	.macro	setmode, mode, reg
	msr	cpsr_c, #\mode
	.endm
#endif

/*
 * Helper macro to enter SVC mode cleanly and mask interrupts. reg is
 * a scratch register for the macro to overwrite.
 *
 * This macro is intended for forcing the CPU into SVC mode at boot time.
 * you cannot return to the original mode.
 */
.macro safe_svcmode_maskall reg:req
#if __LINUX_ARM_ARCH__ >= 6
	mrs	\reg , cpsr
	eor	\reg, \reg, #HYP_MODE
	tst	\reg, #MODE_MASK
	bic	\reg , \reg , #MODE_MASK
	orr	\reg , \reg , #PSR_I_BIT | PSR_F_BIT | SVC_MODE
THUMB(	orr	\reg , \reg , #PSR_T_BIT	)
	bne	1f
	orr	\reg, \reg, #PSR_A_BIT
	adr	lr, BSYM(2f)
	msr	spsr_cxsf, \reg
	__MSR_ELR_HYP(14)
	__ERET
1:	msr	cpsr_c, \reg
2:
#else
/*
 * workaround for possibly broken pre-v6 hardware
 * (akita, Sharp Zaurus C-1000, PXA270-based)
 */
	setmode	PSR_F_BIT | PSR_I_BIT | SVC_MODE, \reg
#endif
.endm

/*
 * STRT/LDRT access macros with ARM and Thumb-2 variants
 */
#ifdef CONFIG_THUMB2_KERNEL

	.macro	usraccoff, instr, reg, ptr, inc, off, cond, abort, t=TUSER()
9999:
	.if	\inc == 1
	\instr\cond\()b\()\t\().w \reg, [\ptr, #\off]
	.elseif	\inc == 4
	\instr\cond\()\t\().w \reg, [\ptr, #\off]
	.else
	.error	"Unsupported inc macro argument"
	.endif

	.pushsection __ex_table,"a"
	.align	3
	.long	9999b, \abort
	.popsection
	.endm

	.macro	usracc, instr, reg, ptr, inc, cond, rept, abort
	@ explicit IT instruction needed because of the label
	@ introduced by the USER macro
	.ifnc	\cond,al
	.if	\rept == 1
	itt	\cond
	.elseif	\rept == 2
	ittt	\cond
	.else
	.error	"Unsupported rept macro argument"
	.endif
	.endif

	@ Slightly optimised to avoid incrementing the pointer twice
	usraccoff \instr, \reg, \ptr, \inc, 0, \cond, \abort
	.if	\rept == 2
	usraccoff \instr, \reg, \ptr, \inc, \inc, \cond, \abort
	.endif

	add\cond \ptr, #\rept * \inc
	.endm

#else	/* !CONFIG_THUMB2_KERNEL */

	.macro	usracc, instr, reg, ptr, inc, cond, rept, abort, t=TUSER()
	.rept	\rept
9999:
	.if	\inc == 1
	\instr\cond\()b\()\t \reg, [\ptr], #\inc
	.elseif	\inc == 4
	\instr\cond\()\t \reg, [\ptr], #\inc
	.else
	.error	"Unsupported inc macro argument"
	.endif

	.pushsection __ex_table,"a"
	.align	3
	.long	9999b, \abort
	.popsection
	.endr
	.endm

#endif	/* CONFIG_THUMB2_KERNEL */

	.macro	strusr, reg, ptr, inc, cond=al, rept=1, abort=9001f
	usracc	str, \reg, \ptr, \inc, \cond, \rept, \abort
	.endm

	.macro	ldrusr, reg, ptr, inc, cond=al, rept=1, abort=9001f
	usracc	ldr, \reg, \ptr, \inc, \cond, \rept, \abort
	.endm

/* Utility macro for declaring string literals */
	.macro	string name:req, string
	.type \name , #object
\name:
	.asciz "\string"
	.size \name , . - \name
	.endm

	.macro check_uaccess, addr:req, size:req, limit:req, tmp:req, bad:req
#ifndef CONFIG_CPU_USE_DOMAINS
	adds	\tmp, \addr, #\size - 1
	sbcccs	\tmp, \tmp, \limit
	bcs	\bad
#endif
	.endm

#endif /* __ASM_ASSEMBLER_H__ */
