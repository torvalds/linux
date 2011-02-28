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

#include <linux/linkage.h>
#include <asm/rwlock.h>
#include <asm/alternative-asm.h>
#include <asm/frame.h>
#include <asm/dwarf2.h>

#define save_common_regs \
	pushq_cfi %rdi; CFI_REL_OFFSET rdi, 0; \
	pushq_cfi %rsi; CFI_REL_OFFSET rsi, 0; \
	pushq_cfi %rcx; CFI_REL_OFFSET rcx, 0; \
	pushq_cfi %r8;  CFI_REL_OFFSET r8,  0; \
	pushq_cfi %r9;  CFI_REL_OFFSET r9,  0; \
	pushq_cfi %r10; CFI_REL_OFFSET r10, 0; \
	pushq_cfi %r11; CFI_REL_OFFSET r11, 0

#define restore_common_regs \
	popq_cfi %r11; CFI_RESTORE r11; \
	popq_cfi %r10; CFI_RESTORE r10; \
	popq_cfi %r9;  CFI_RESTORE r9; \
	popq_cfi %r8;  CFI_RESTORE r8; \
	popq_cfi %rcx; CFI_RESTORE rcx; \
	popq_cfi %rsi; CFI_RESTORE rsi; \
	popq_cfi %rdi; CFI_RESTORE rdi

/* Fix up special calling conventions */
ENTRY(call_rwsem_down_read_failed)
	CFI_STARTPROC
	save_common_regs
	pushq_cfi %rdx
	CFI_REL_OFFSET rdx, 0
	movq %rax,%rdi
	call rwsem_down_read_failed
	popq_cfi %rdx
	CFI_RESTORE rdx
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
	decl %edx	/* do nothing if still outstanding active readers */
	jnz 1f
	save_common_regs
	movq %rax,%rdi
	call rwsem_wake
	restore_common_regs
1:	ret
	CFI_ENDPROC
ENDPROC(call_rwsem_wake)

/* Fix up special calling conventions */
ENTRY(call_rwsem_downgrade_wake)
	CFI_STARTPROC
	save_common_regs
	pushq_cfi %rdx
	CFI_REL_OFFSET rdx, 0
	movq %rax,%rdi
	call rwsem_downgrade_wake
	popq_cfi %rdx
	CFI_RESTORE rdx
	restore_common_regs
	ret
	CFI_ENDPROC
ENDPROC(call_rwsem_downgrade_wake)
