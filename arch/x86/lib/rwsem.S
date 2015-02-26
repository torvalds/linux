/*
 * x86 semaphore implementation.
 *
 * (C) Copyright 1999 Linus Torvalds
 *
 * Portions Copyright 1999 Red Hat, Inc.
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 * rw semaphores implemented November 1999 by Benjamin LaHaise <bcrl@kvack.org>
 */

#include <linux/linkage.h>
#include <asm/alternative-asm.h>
#include <asm/dwarf2.h>

#define __ASM_HALF_REG(reg)	__ASM_SEL(reg, e##reg)
#define __ASM_HALF_SIZE(inst)	__ASM_SEL(inst##w, inst##l)

#ifdef CONFIG_X86_32

/*
 * The semaphore operations have a special calling sequence that
 * allow us to do a simpler in-line version of them. These routines
 * need to convert that sequence back into the C sequence when
 * there is contention on the semaphore.
 *
 * %eax contains the semaphore pointer on entry. Save the C-clobbered
 * registers (%eax, %edx and %ecx) except %eax whish is either a return
 * value or just clobbered..
 */

#define save_common_regs \
	pushl_cfi_reg ecx

#define restore_common_regs \
	popl_cfi_reg ecx

	/* Avoid uglifying the argument copying x86-64 needs to do. */
	.macro movq src, dst
	.endm

#else

/*
 * x86-64 rwsem wrappers
 *
 * This interfaces the inline asm code to the slow-path
 * C routines. We need to save the call-clobbered regs
 * that the asm does not mark as clobbered, and move the
 * argument from %rax to %rdi.
 *
 * NOTE! We don't need to save %rax, because the functions
 * will always return the semaphore pointer in %rax (which
 * is also the input argument to these helpers)
 *
 * The following can clobber %rdx because the asm clobbers it:
 *   call_rwsem_down_write_failed
 *   call_rwsem_wake
 * but %rdi, %rsi, %rcx, %r8-r11 always need saving.
 */

#define save_common_regs \
	pushq_cfi_reg rdi; \
	pushq_cfi_reg rsi; \
	pushq_cfi_reg rcx; \
	pushq_cfi_reg r8;  \
	pushq_cfi_reg r9;  \
	pushq_cfi_reg r10; \
	pushq_cfi_reg r11

#define restore_common_regs \
	popq_cfi_reg r11; \
	popq_cfi_reg r10; \
	popq_cfi_reg r9; \
	popq_cfi_reg r8; \
	popq_cfi_reg rcx; \
	popq_cfi_reg rsi; \
	popq_cfi_reg rdi

#endif

/* Fix up special calling conventions */
ENTRY(call_rwsem_down_read_failed)
	CFI_STARTPROC
	save_common_regs
	__ASM_SIZE(push,_cfi_reg) __ASM_REG(dx)
	movq %rax,%rdi
	call rwsem_down_read_failed
	__ASM_SIZE(pop,_cfi_reg) __ASM_REG(dx)
	restore_common_regs
	ret
	CFI_ENDPROC
ENDPROC(call_rwsem_down_read_failed)

ENTRY(call_rwsem_down_write_failed)
	CFI_STARTPROC
	save_common_regs
	movq %rax,%rdi
	call rwsem_down_write_failed
	restore_common_regs
	ret
	CFI_ENDPROC
ENDPROC(call_rwsem_down_write_failed)

ENTRY(call_rwsem_wake)
	CFI_STARTPROC
	/* do nothing if still outstanding active readers */
	__ASM_HALF_SIZE(dec) %__ASM_HALF_REG(dx)
	jnz 1f
	save_common_regs
	movq %rax,%rdi
	call rwsem_wake
	restore_common_regs
1:	ret
	CFI_ENDPROC
ENDPROC(call_rwsem_wake)

ENTRY(call_rwsem_downgrade_wake)
	CFI_STARTPROC
	save_common_regs
	__ASM_SIZE(push,_cfi_reg) __ASM_REG(dx)
	movq %rax,%rdi
	call rwsem_downgrade_wake
	__ASM_SIZE(pop,_cfi_reg) __ASM_REG(dx)
	restore_common_regs
	ret
	CFI_ENDPROC
ENDPROC(call_rwsem_downgrade_wake)
