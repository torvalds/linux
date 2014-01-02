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

	.arm

sigreturn_codes:

	/* ARM sigreturn syscall code snippet */
	mov	r7, #(__NR_sigreturn - __NR_SYSCALL_BASE)
	swi	#(__NR_sigreturn)|(__NR_OABI_SYSCALL_BASE)

	/* Thumb sigreturn syscall code snippet */
	.thumb
	movs	r7, #(__NR_sigreturn - __NR_SYSCALL_BASE)
	swi	#0

	/* ARM sigreturn_rt syscall code snippet */
	.arm
	mov	r7, #(__NR_rt_sigreturn - __NR_SYSCALL_BASE)
	swi	#(__NR_rt_sigreturn)|(__NR_OABI_SYSCALL_BASE)

	/* Thumb sigreturn_rt syscall code snippet */
	.thumb
	movs	r7, #(__NR_rt_sigreturn - __NR_SYSCALL_BASE)
	swi	#0

	/*
	 * Note on addtional space: setup_return in signal.c
	 * algorithm uses two words copy regardless whether
	 * it is thumb case or not, so we need additional
	 * word after real last entry.
	 */
	.arm
	.space	4

	.size	sigreturn_codes, . - sigreturn_codes
