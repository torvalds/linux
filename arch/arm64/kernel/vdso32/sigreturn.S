/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This file provides both A32 and T32 versions, in accordance with the
 * arm sigreturn code.
 *
 * Copyright (C) 2018 ARM Limited
 */

#include <linux/linkage.h>
#include <asm/asm-offsets.h>
#include <asm/unistd.h>

#define ARM_ENTRY(name)		\
	ENTRY(name)

#define ARM_ENDPROC(name)	\
	.type name, %function;	\
	END(name)

	.text

	.arm
	.fnstart
	.save {r0-r15}
	.pad #COMPAT_SIGFRAME_REGS_OFFSET
	nop
ARM_ENTRY(__kernel_sigreturn_arm)
	mov r7, #__NR_compat_sigreturn
	svc #0
	.fnend
ARM_ENDPROC(__kernel_sigreturn_arm)

	.fnstart
	.save {r0-r15}
	.pad #COMPAT_RT_SIGFRAME_REGS_OFFSET
	nop
ARM_ENTRY(__kernel_rt_sigreturn_arm)
	mov r7, #__NR_compat_rt_sigreturn
	svc #0
	.fnend
ARM_ENDPROC(__kernel_rt_sigreturn_arm)

	.thumb
	.fnstart
	.save {r0-r15}
	.pad #COMPAT_SIGFRAME_REGS_OFFSET
	nop
ARM_ENTRY(__kernel_sigreturn_thumb)
	mov r7, #__NR_compat_sigreturn
	svc #0
	.fnend
ARM_ENDPROC(__kernel_sigreturn_thumb)

	.fnstart
	.save {r0-r15}
	.pad #COMPAT_RT_SIGFRAME_REGS_OFFSET
	nop
ARM_ENTRY(__kernel_rt_sigreturn_thumb)
	mov r7, #__NR_compat_rt_sigreturn
	svc #0
	.fnend
ARM_ENDPROC(__kernel_rt_sigreturn_thumb)
