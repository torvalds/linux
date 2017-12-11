/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Function calling ABI conversion from Linux to EFI for x86_64
 *
 * Copyright (C) 2007 Intel Corp
 *	Bibo Mao <bibo.mao@intel.com>
 *	Huang Ying <ying.huang@intel.com>
 */

#include <linux/linkage.h>
#include <asm/segment.h>
#include <asm/msr.h>
#include <asm/processor-flags.h>
#include <asm/page_types.h>

#define SAVE_XMM			\
	mov %rsp, %rax;			\
	subq $0x70, %rsp;		\
	and $~0xf, %rsp;		\
	mov %rax, (%rsp);		\
	mov %cr0, %rax;			\
	clts;				\
	mov %rax, 0x8(%rsp);		\
	movaps %xmm0, 0x60(%rsp);	\
	movaps %xmm1, 0x50(%rsp);	\
	movaps %xmm2, 0x40(%rsp);	\
	movaps %xmm3, 0x30(%rsp);	\
	movaps %xmm4, 0x20(%rsp);	\
	movaps %xmm5, 0x10(%rsp)

#define RESTORE_XMM			\
	movaps 0x60(%rsp), %xmm0;	\
	movaps 0x50(%rsp), %xmm1;	\
	movaps 0x40(%rsp), %xmm2;	\
	movaps 0x30(%rsp), %xmm3;	\
	movaps 0x20(%rsp), %xmm4;	\
	movaps 0x10(%rsp), %xmm5;	\
	mov 0x8(%rsp), %rsi;		\
	mov %rsi, %cr0;			\
	mov (%rsp), %rsp

ENTRY(efi_call)
	pushq %rbp
	movq %rsp, %rbp
	SAVE_XMM
	mov 16(%rbp), %rax
	subq $48, %rsp
	mov %r9, 32(%rsp)
	mov %rax, 40(%rsp)
	mov %r8, %r9
	mov %rcx, %r8
	mov %rsi, %rcx
	call *%rdi
	addq $48, %rsp
	RESTORE_XMM
	popq %rbp
	ret
ENDPROC(efi_call)
