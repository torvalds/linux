/* SPDX-License-Identifier: GPL-2.0 */
/*
 * AArch32 sigreturn code.
 * Based on the kuser helpers in arch/arm/kernel/entry-armv.S.
 *
 * Copyright (C) 2005-2011 Nicolas Pitre <nico@fluxnic.net>
 * Copyright (C) 2012-2018 ARM Ltd.
 *
 * For ARM syscalls, the syscall number has to be loaded into r7.
 * We do not support an OABI userspace.
 *
 * For Thumb syscalls, we also pass the syscall number via r7. We therefore
 * need two 16-bit instructions.
 */

#include <asm/unistd.h>

	.globl __aarch32_sigret_code_start
__aarch32_sigret_code_start:

	/*
	 * ARM Code
	 */
	.byte	__NR_compat_sigreturn, 0x70, 0xa0, 0xe3		// mov	r7, #__NR_compat_sigreturn
	.byte	__NR_compat_sigreturn, 0x00, 0x00, 0xef		// svc	#__NR_compat_sigreturn

	/*
	 * Thumb code
	 */
	.byte	__NR_compat_sigreturn, 0x27			// svc	#__NR_compat_sigreturn
	.byte	__NR_compat_sigreturn, 0xdf			// mov	r7, #__NR_compat_sigreturn

	/*
	 * ARM code
	 */
	.byte	__NR_compat_rt_sigreturn, 0x70, 0xa0, 0xe3	// mov	r7, #__NR_compat_rt_sigreturn
	.byte	__NR_compat_rt_sigreturn, 0x00, 0x00, 0xef	// svc	#__NR_compat_rt_sigreturn

	/*
	 * Thumb code
	 */
	.byte	__NR_compat_rt_sigreturn, 0x27			// svc	#__NR_compat_rt_sigreturn
	.byte	__NR_compat_rt_sigreturn, 0xdf			// mov	r7, #__NR_compat_rt_sigreturn

        .globl __aarch32_sigret_code_end
__aarch32_sigret_code_end:
