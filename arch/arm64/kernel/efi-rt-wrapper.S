/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2018 Linaro Ltd <ard.biesheuvel@linaro.org>
 */

#include <linux/linkage.h>

SYM_FUNC_START(__efi_rt_asm_wrapper)
	stp	x29, x30, [sp, #-112]!
	mov	x29, sp

	/*
	 * Register x18 is designated as the 'platform' register by the AAPCS,
	 * which means firmware running at the same exception level as the OS
	 * (such as UEFI) should never touch it.
	 */
	stp	x1, x18, [sp, #16]

	/*
	 * Preserve all callee saved registers and record the stack pointer
	 * value in a per-CPU variable so we can recover from synchronous
	 * exceptions occurring while running the firmware routines.
	 */
	stp	x19, x20, [sp, #32]
	stp	x21, x22, [sp, #48]
	stp	x23, x24, [sp, #64]
	stp	x25, x26, [sp, #80]
	stp	x27, x28, [sp, #96]

	adr_this_cpu	x8, __efi_rt_asm_recover_sp, x9
	str		x29, [x8]

	/*
	 * We are lucky enough that no EFI runtime services take more than
	 * 5 arguments, so all are passed in registers rather than via the
	 * stack.
	 */
	mov	x8, x0
	mov	x0, x2
	mov	x1, x3
	mov	x2, x4
	mov	x3, x5
	mov	x4, x6
	blr	x8

	ldp	x1, x2, [sp, #16]
	cmp	x2, x18
	ldp	x29, x30, [sp], #112
	b.ne	0f
	ret
0:
	/*
	 * With CONFIG_SHADOW_CALL_STACK, the kernel uses x18 to store a
	 * shadow stack pointer, which we need to restore before returning to
	 * potentially instrumented code. This is safe because the wrapper is
	 * called with preemption disabled and a separate shadow stack is used
	 * for interrupts.
	 */
	mov	x18, x2
	b	efi_handle_corrupted_x18	// tail call
SYM_FUNC_END(__efi_rt_asm_wrapper)

SYM_FUNC_START(__efi_rt_asm_recover)
	ldr_this_cpu	x8, __efi_rt_asm_recover_sp, x9
	mov		sp, x8

	ldp	x0,  x18, [sp, #16]
	ldp	x19, x20, [sp, #32]
	ldp	x21, x22, [sp, #48]
	ldp	x23, x24, [sp, #64]
	ldp	x25, x26, [sp, #80]
	ldp	x27, x28, [sp, #96]
	ldp	x29, x30, [sp], #112

	b	efi_handle_runtime_exception
SYM_FUNC_END(__efi_rt_asm_recover)
