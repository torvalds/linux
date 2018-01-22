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
#include <asm/frame.h>

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
 * registers (%eax, %edx and %ecx) except %eax which is either a return
 * value or just gets clobbered. Same is true for %edx so make sure GCC
 * reloads it after the slow path, by making it hold a temporary, for
 * example see ____down_write().
 */

#define save_common_regs \
	pushl %ecx

#define restore_common_regs \
	popl %ecx

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
	pushq %rdi; \
	pushq %rsi; \
	pushq %rcx; \
	pushq %r8;  \
	pushq %r9;  \
	pushq %r10; \
	pushq %r11

#define restore_common_regs \
	popq %r11; \
	popq %r10; \
	popq %r9; \
	popq %r8; \
	popq %rcx; \
	popq %rsi; \
	popq %rdi

#endif

/* Fix up special calling conventions */
ENTRY(call_rwsem_down_read_failed)
	FRAME_BEGIN
	save_common_regs
	__ASM_SIZE(push,) %__ASM_REG(dx)
	movq %rax,%rdi
	call rwsem_down_read_failed
	__ASM_SIZE(pop,) %__ASM_REG(dx)
	restore_common_regs
	FRAME_END
	ret
ENDPROC(call_rwsem_down_read_failed)

ENTRY(call_rwsem_down_read_failed_killable)
	FRAME_BEGIN
	save_common_regs
	__ASM_SIZE(push,) %__ASM_REG(dx)
	movq %rax,%rdi
	call rwsem_down_read_failed_killable
	__ASM_SIZE(pop,) %__ASM_REG(dx)
	restore_common_regs
	FRAME_END
	ret
ENDPROC(call_rwsem_down_read_failed_killable)

ENTRY(call_rwsem_down_write_failed)
	FRAME_BEGIN
	save_common_regs
	movq %rax,%rdi
	call rwsem_down_write_failed
	restore_common_regs
	FRAME_END
	ret
ENDPROC(call_rwsem_down_write_failed)

ENTRY(call_rwsem_down_write_failed_killable)
	FRAME_BEGIN
	save_common_regs
	movq %rax,%rdi
	call rwsem_down_write_failed_killable
	restore_common_regs
	FRAME_END
	ret
ENDPROC(call_rwsem_down_write_failed_killable)

ENTRY(call_rwsem_wake)
	FRAME_BEGIN
	/* do nothing if still outstanding active readers */
	__ASM_HALF_SIZE(dec) %__ASM_HALF_REG(dx)
	jnz 1f
	save_common_regs
	movq %rax,%rdi
	call rwsem_wake
	restore_common_regs
1:	FRAME_END
	ret
ENDPROC(call_rwsem_wake)

ENTRY(call_rwsem_downgrade_wake)
	FRAME_BEGIN
	save_common_regs
	__ASM_SIZE(push,) %__ASM_REG(dx)
	movq %rax,%rdi
	call rwsem_downgrade_wake
	__ASM_SIZE(pop,) %__ASM_REG(dx)
	restore_common_regs
	FRAME_END
	ret
ENDPROC(call_rwsem_downgrade_wake)
