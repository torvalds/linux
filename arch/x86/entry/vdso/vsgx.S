/* SPDX-License-Identifier: GPL-2.0 */

#include <linux/linkage.h>
#include <asm/errno.h>
#include <asm/enclu.h>

#include "extable.h"

/* Relative to %rbp. */
#define SGX_ENCLAVE_OFFSET_OF_RUN		16

/* The offsets relative to struct sgx_enclave_run. */
#define SGX_ENCLAVE_RUN_TCS			0
#define SGX_ENCLAVE_RUN_LEAF			8
#define SGX_ENCLAVE_RUN_EXCEPTION_VECTOR	12
#define SGX_ENCLAVE_RUN_EXCEPTION_ERROR_CODE	14
#define SGX_ENCLAVE_RUN_EXCEPTION_ADDR		16
#define SGX_ENCLAVE_RUN_USER_HANDLER		24
#define SGX_ENCLAVE_RUN_USER_DATA		32	/* not used */
#define SGX_ENCLAVE_RUN_RESERVED_START		40
#define SGX_ENCLAVE_RUN_RESERVED_END		256

.code64
.section .text, "ax"

SYM_FUNC_START(__vdso_sgx_enter_enclave)
	/* Prolog */
	.cfi_startproc
	push	%rbp
	.cfi_adjust_cfa_offset	8
	.cfi_rel_offset		%rbp, 0
	mov	%rsp, %rbp
	.cfi_def_cfa_register	%rbp
	push	%rbx
	.cfi_rel_offset		%rbx, -8

	mov	%ecx, %eax
.Lenter_enclave:
	/* EENTER <= function <= ERESUME */
	cmp	$EENTER, %eax
	jb	.Linvalid_input
	cmp	$ERESUME, %eax
	ja	.Linvalid_input

	mov	SGX_ENCLAVE_OFFSET_OF_RUN(%rbp), %rcx

	/* Validate that the reserved area contains only zeros. */
	mov	$SGX_ENCLAVE_RUN_RESERVED_START, %rbx
1:
	cmpq	$0, (%rcx, %rbx)
	jne	.Linvalid_input
	add	$8, %rbx
	cmpq	$SGX_ENCLAVE_RUN_RESERVED_END, %rbx
	jne	1b

	/* Load TCS and AEP */
	mov	SGX_ENCLAVE_RUN_TCS(%rcx), %rbx
	lea	.Lasync_exit_pointer(%rip), %rcx

	/* Single ENCLU serving as both EENTER and AEP (ERESUME) */
.Lasync_exit_pointer:
.Lenclu_eenter_eresume:
	enclu

	/* EEXIT jumps here unless the enclave is doing something fancy. */
	mov	SGX_ENCLAVE_OFFSET_OF_RUN(%rbp), %rbx

	/* Set exit_reason. */
	movl	$EEXIT, SGX_ENCLAVE_RUN_LEAF(%rbx)

	/* Invoke userspace's exit handler if one was provided. */
.Lhandle_exit:
	cmpq	$0, SGX_ENCLAVE_RUN_USER_HANDLER(%rbx)
	jne	.Linvoke_userspace_handler

	/* Success, in the sense that ENCLU was attempted. */
	xor	%eax, %eax

.Lout:
	pop	%rbx
	leave
	.cfi_def_cfa		%rsp, 8
	RET

	/* The out-of-line code runs with the pre-leave stack frame. */
	.cfi_def_cfa		%rbp, 16

.Linvalid_input:
	mov	$(-EINVAL), %eax
	jmp	.Lout

.Lhandle_exception:
	mov	SGX_ENCLAVE_OFFSET_OF_RUN(%rbp), %rbx

	/* Set the exception info. */
	mov	%eax, (SGX_ENCLAVE_RUN_LEAF)(%rbx)
	mov	%di,  (SGX_ENCLAVE_RUN_EXCEPTION_VECTOR)(%rbx)
	mov	%si,  (SGX_ENCLAVE_RUN_EXCEPTION_ERROR_CODE)(%rbx)
	mov	%rdx, (SGX_ENCLAVE_RUN_EXCEPTION_ADDR)(%rbx)
	jmp	.Lhandle_exit

.Linvoke_userspace_handler:
	/* Pass the untrusted RSP (at exit) to the callback via %rcx. */
	mov	%rsp, %rcx

	/* Save struct sgx_enclave_exception %rbx is about to be clobbered. */
	mov	%rbx, %rax

	/* Save the untrusted RSP offset in %rbx (non-volatile register). */
	mov	%rsp, %rbx
	and	$0xf, %rbx

	/*
	 * Align stack per x86_64 ABI. Note, %rsp needs to be 16-byte aligned
	 * _after_ pushing the parameters on the stack, hence the bonus push.
	 */
	and	$-0x10, %rsp
	push	%rax

	/* Push struct sgx_enclave_exception as a param to the callback. */
	push	%rax

	/* Clear RFLAGS.DF per x86_64 ABI */
	cld

	/*
	 * Load the callback pointer to %rax and lfence for LVI (load value
	 * injection) protection before making the call.
	 */
	mov	SGX_ENCLAVE_RUN_USER_HANDLER(%rax), %rax
	lfence
	call	*%rax

	/* Undo the post-exit %rsp adjustment. */
	lea	0x10(%rsp, %rbx), %rsp

	/*
	 * If the return from callback is zero or negative, return immediately,
	 * else re-execute ENCLU with the positive return value interpreted as
	 * the requested ENCLU function.
	 */
	cmp	$0, %eax
	jle	.Lout
	jmp	.Lenter_enclave

	.cfi_endproc

_ASM_VDSO_EXTABLE_HANDLE(.Lenclu_eenter_eresume, .Lhandle_exception)

SYM_FUNC_END(__vdso_sgx_enter_enclave)
