/*
 * Function calling ABI conversion from Linux to EFI for x86_64
 *
 * Copyright (C) 2007 Intel Corp
 *	Bibo Mao <bibo.mao@intel.com>
 *	Huang Ying <ying.huang@intel.com>
 */

#include <linux/linkage.h>

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

	/* stolen from gcc */
	.macro FLUSH_TLB_ALL
	movq %r15, efi_scratch(%rip)
	movq %r14, efi_scratch+8(%rip)
	movq %cr4, %r15
	movq %r15, %r14
	andb $0x7f, %r14b
	movq %r14, %cr4
	movq %r15, %cr4
	movq efi_scratch+8(%rip), %r14
	movq efi_scratch(%rip), %r15
	.endm

	.macro SWITCH_PGT
	cmpb $0, efi_scratch+24(%rip)
	je 1f
	movq %r15, efi_scratch(%rip)		# r15
	# save previous CR3
	movq %cr3, %r15
	movq %r15, efi_scratch+8(%rip)		# prev_cr3
	movq efi_scratch+16(%rip), %r15		# EFI pgt
	movq %r15, %cr3
	1:
	.endm

	.macro RESTORE_PGT
	cmpb $0, efi_scratch+24(%rip)
	je 2f
	movq efi_scratch+8(%rip), %r15
	movq %r15, %cr3
	movq efi_scratch(%rip), %r15
	FLUSH_TLB_ALL
	2:
	.endm

ENTRY(efi_call0)
	SAVE_XMM
	subq $32, %rsp
	SWITCH_PGT
	call *%rdi
	RESTORE_PGT
	addq $32, %rsp
	RESTORE_XMM
	ret
ENDPROC(efi_call0)

ENTRY(efi_call1)
	SAVE_XMM
	subq $32, %rsp
	mov  %rsi, %rcx
	SWITCH_PGT
	call *%rdi
	RESTORE_PGT
	addq $32, %rsp
	RESTORE_XMM
	ret
ENDPROC(efi_call1)

ENTRY(efi_call2)
	SAVE_XMM
	subq $32, %rsp
	mov  %rsi, %rcx
	SWITCH_PGT
	call *%rdi
	RESTORE_PGT
	addq $32, %rsp
	RESTORE_XMM
	ret
ENDPROC(efi_call2)

ENTRY(efi_call3)
	SAVE_XMM
	subq $32, %rsp
	mov  %rcx, %r8
	mov  %rsi, %rcx
	SWITCH_PGT
	call *%rdi
	RESTORE_PGT
	addq $32, %rsp
	RESTORE_XMM
	ret
ENDPROC(efi_call3)

ENTRY(efi_call4)
	SAVE_XMM
	subq $32, %rsp
	mov %r8, %r9
	mov %rcx, %r8
	mov %rsi, %rcx
	SWITCH_PGT
	call *%rdi
	RESTORE_PGT
	addq $32, %rsp
	RESTORE_XMM
	ret
ENDPROC(efi_call4)

ENTRY(efi_call5)
	SAVE_XMM
	subq $48, %rsp
	mov %r9, 32(%rsp)
	mov %r8, %r9
	mov %rcx, %r8
	mov %rsi, %rcx
	SWITCH_PGT
	call *%rdi
	RESTORE_PGT
	addq $48, %rsp
	RESTORE_XMM
	ret
ENDPROC(efi_call5)

ENTRY(efi_call6)
	SAVE_XMM
	mov (%rsp), %rax
	mov 8(%rax), %rax
	subq $48, %rsp
	mov %r9, 32(%rsp)
	mov %rax, 40(%rsp)
	mov %r8, %r9
	mov %rcx, %r8
	mov %rsi, %rcx
	SWITCH_PGT
	call *%rdi
	RESTORE_PGT
	addq $48, %rsp
	RESTORE_XMM
	ret
ENDPROC(efi_call6)

	.data
ENTRY(efi_scratch)
	.fill 3,8,0
	.byte 0
