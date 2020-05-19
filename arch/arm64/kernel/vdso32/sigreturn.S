/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This file provides both A32 and T32 versions, in accordance with the
 * arm sigreturn code.
 *
 * Please read the comments in arch/arm64/kernel/vdso/sigreturn.S to
 * understand some of the craziness in here.
 *
 * Copyright (C) 2018 ARM Limited
 */

#include <linux/linkage.h>
#include <asm/asm-offsets.h>
#include <asm/unistd.h>

	.text

	.arm
	.fnstart
	.save {r0-r15}
	.pad #COMPAT_SIGFRAME_REGS_OFFSET
	nop
SYM_CODE_START(__kernel_sigreturn_arm)
	mov r7, #__NR_compat_sigreturn
	svc #0
	.fnend
SYM_CODE_END(__kernel_sigreturn_arm)

	.fnstart
	.save {r0-r15}
	.pad #COMPAT_RT_SIGFRAME_REGS_OFFSET
	nop
SYM_CODE_START(__kernel_rt_sigreturn_arm)
	mov r7, #__NR_compat_rt_sigreturn
	svc #0
	.fnend
SYM_CODE_END(__kernel_rt_sigreturn_arm)

	.thumb
	.fnstart
	.save {r0-r15}
	.pad #COMPAT_SIGFRAME_REGS_OFFSET
	nop
SYM_CODE_START(__kernel_sigreturn_thumb)
	mov r7, #__NR_compat_sigreturn
	svc #0
	.fnend
SYM_CODE_END(__kernel_sigreturn_thumb)

	.fnstart
	.save {r0-r15}
	.pad #COMPAT_RT_SIGFRAME_REGS_OFFSET
	nop
SYM_CODE_START(__kernel_rt_sigreturn_thumb)
	mov r7, #__NR_compat_rt_sigreturn
	svc #0
	.fnend
SYM_CODE_END(__kernel_rt_sigreturn_thumb)
