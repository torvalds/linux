/*
 * sigreturn_codes.S - code sinpets for sigreturn syscalls
 *
 * Created by:	Victor Kamensky, 2013-08-13
 * Copyright:	(C) 2013  Linaro Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <asm/assembler.h>
#include <asm/asm-offsets.h>
#include <asm/unistd.h>

/*
 * For ARM syscalls, we encode the syscall number into the instruction.
 * With EABI, the syscall number has to be loaded into r7. As result
 * ARM syscall sequence snippet will have move and svc in .arm encoding
 *
 * For Thumb syscalls, we pass the syscall number via r7.  We therefore
 * need two 16-bit instructions in .thumb encoding
 *
 * Please note sigreturn_codes code are not executed in place. Instead
 * they just copied by kernel into appropriate places. Code inside of
 * arch/arm/kernel/signal.c is very sensitive to layout of these code
 * snippets.
 */

/*
 * In CPU_THUMBONLY case kernel arm opcodes are not allowed.
 * Note in this case codes skips those instructions but it uses .org
 * directive to keep correct layout of sigreturn_codes array.
 */
#ifndef CONFIG_CPU_THUMBONLY
#define ARM_OK(code...)	code
#else
#define ARM_OK(code...)
#endif

	.macro arm_slot n
	.org	sigreturn_codes + 12 * (\n)
ARM_OK(	.arm	)
	.endm

	.macro thumb_slot n
	.org	sigreturn_codes + 12 * (\n) + 8
	.thumb
	.endm

	.macro arm_fdpic_slot n
	.org	sigreturn_codes + 24 + 20 * (\n)
ARM_OK(	.arm	)
	.endm

	.macro thumb_fdpic_slot n
	.org	sigreturn_codes + 24 + 20 * (\n) + 12
	.thumb
	.endm


#if __LINUX_ARM_ARCH__ <= 4
	/*
	 * Note we manually set minimally required arch that supports
	 * required thumb opcodes for early arch versions. It is OK
	 * for this file to be used in combination with other
	 * lower arch variants, since these code snippets are only
	 * used as input data.
	 */
	.arch armv4t
#endif

	.section .rodata
	.global sigreturn_codes
	.type	sigreturn_codes, #object

	.align

sigreturn_codes:

	/* ARM sigreturn syscall code snippet */
	arm_slot 0
ARM_OK(	mov	r7, #(__NR_sigreturn - __NR_SYSCALL_BASE)	)
ARM_OK(	swi	#(__NR_sigreturn)|(__NR_OABI_SYSCALL_BASE)	)

	/* Thumb sigreturn syscall code snippet */
	thumb_slot 0
	movs	r7, #(__NR_sigreturn - __NR_SYSCALL_BASE)
	swi	#0

	/* ARM sigreturn_rt syscall code snippet */
	arm_slot 1
ARM_OK(	mov	r7, #(__NR_rt_sigreturn - __NR_SYSCALL_BASE)	)
ARM_OK(	swi	#(__NR_rt_sigreturn)|(__NR_OABI_SYSCALL_BASE)	)

	/* Thumb sigreturn_rt syscall code snippet */
	thumb_slot 1
	movs	r7, #(__NR_rt_sigreturn - __NR_SYSCALL_BASE)
	swi	#0

	/* ARM sigreturn restorer FDPIC bounce code snippet */
	arm_fdpic_slot 0
ARM_OK(	ldr	r3, [sp, #SIGFRAME_RC3_OFFSET] )
ARM_OK(	ldmia	r3, {r3, r9} )
#ifdef CONFIG_ARM_THUMB
ARM_OK(	bx	r3 )
#else
ARM_OK(	ret	r3 )
#endif

	/* Thumb sigreturn restorer FDPIC bounce code snippet */
	thumb_fdpic_slot 0
	ldr	r3, [sp, #SIGFRAME_RC3_OFFSET]
	ldmia	r3, {r2, r3}
	mov	r9, r3
	bx	r2

	/* ARM sigreturn_rt restorer FDPIC bounce code snippet */
	arm_fdpic_slot 1
ARM_OK(	ldr	r3, [sp, #RT_SIGFRAME_RC3_OFFSET] )
ARM_OK(	ldmia	r3, {r3, r9} )
#ifdef CONFIG_ARM_THUMB
ARM_OK(	bx	r3 )
#else
ARM_OK(	ret	r3 )
#endif

	/* Thumb sigreturn_rt restorer FDPIC bounce code snippet */
	thumb_fdpic_slot 1
	ldr	r3, [sp, #RT_SIGFRAME_RC3_OFFSET]
	ldmia	r3, {r2, r3}
	mov	r9, r3
	bx	r2

	/*
	 * Note on additional space: setup_return in signal.c
	 * always copies the same number of words regardless whether
	 * it is thumb case or not, so we need one additional padding
	 * word after the last entry.
	 */
	.space	4

	.size	sigreturn_codes, . - sigreturn_codes
