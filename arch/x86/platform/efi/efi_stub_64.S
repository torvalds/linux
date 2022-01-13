/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Function calling ABI conversion from Linux to EFI for x86_64
 *
 * Copyright (C) 2007 Intel Corp
 *	Bibo Mao <bibo.mao@intel.com>
 *	Huang Ying <ying.huang@intel.com>
 */

#include <linux/linkage.h>
#include <asm/nospec-branch.h>

SYM_FUNC_START(__efi_call)
	pushq %rbp
	movq %rsp, %rbp
	and $~0xf, %rsp
	mov 16(%rbp), %rax
	subq $48, %rsp
	mov %r9, 32(%rsp)
	mov %rax, 40(%rsp)
	mov %r8, %r9
	mov %rcx, %r8
	mov %rsi, %rcx
	CALL_NOSPEC rdi
	leave
	RET
SYM_FUNC_END(__efi_call)
