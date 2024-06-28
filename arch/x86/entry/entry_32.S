/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  Copyright (C) 1991,1992  Linus Torvalds
 *
 * entry_32.S contains the system-call and low-level fault and trap handling routines.
 *
 * Stack layout while running C code:
 *	ptrace needs to have all registers on the stack.
 *	If the order here is changed, it needs to be
 *	updated in fork.c:copy_process(), signal.c:do_signal(),
 *	ptrace.c and ptrace.h
 *
 *	 0(%esp) - %ebx
 *	 4(%esp) - %ecx
 *	 8(%esp) - %edx
 *	 C(%esp) - %esi
 *	10(%esp) - %edi
 *	14(%esp) - %ebp
 *	18(%esp) - %eax
 *	1C(%esp) - %ds
 *	20(%esp) - %es
 *	24(%esp) - %fs
 *	28(%esp) - unused -- was %gs on old stackprotector kernels
 *	2C(%esp) - orig_eax
 *	30(%esp) - %eip
 *	34(%esp) - %cs
 *	38(%esp) - %eflags
 *	3C(%esp) - %oldesp
 *	40(%esp) - %oldss
 */

#include <linux/linkage.h>
#include <linux/err.h>
#include <asm/thread_info.h>
#include <asm/irqflags.h>
#include <asm/errno.h>
#include <asm/segment.h>
#include <asm/smp.h>
#include <asm/percpu.h>
#include <asm/processor-flags.h>
#include <asm/irq_vectors.h>
#include <asm/cpufeatures.h>
#include <asm/alternative.h>
#include <asm/asm.h>
#include <asm/smap.h>
#include <asm/frame.h>
#include <asm/trapnr.h>
#include <asm/nospec-branch.h>

#include "calling.h"

	.section .entry.text, "ax"

#define PTI_SWITCH_MASK         (1 << PAGE_SHIFT)

/* Unconditionally switch to user cr3 */
.macro SWITCH_TO_USER_CR3 scratch_reg:req
	ALTERNATIVE "jmp .Lend_\@", "", X86_FEATURE_PTI

	movl	%cr3, \scratch_reg
	orl	$PTI_SWITCH_MASK, \scratch_reg
	movl	\scratch_reg, %cr3
.Lend_\@:
.endm

.macro BUG_IF_WRONG_CR3 no_user_check=0
#ifdef CONFIG_DEBUG_ENTRY
	ALTERNATIVE "jmp .Lend_\@", "", X86_FEATURE_PTI
	.if \no_user_check == 0
	/* coming from usermode? */
	testl	$USER_SEGMENT_RPL_MASK, PT_CS(%esp)
	jz	.Lend_\@
	.endif
	/* On user-cr3? */
	movl	%cr3, %eax
	testl	$PTI_SWITCH_MASK, %eax
	jnz	.Lend_\@
	/* From userspace with kernel cr3 - BUG */
	ud2
.Lend_\@:
#endif
.endm

/*
 * Switch to kernel cr3 if not already loaded and return current cr3 in
 * \scratch_reg
 */
.macro SWITCH_TO_KERNEL_CR3 scratch_reg:req
	ALTERNATIVE "jmp .Lend_\@", "", X86_FEATURE_PTI
	movl	%cr3, \scratch_reg
	/* Test if we are already on kernel CR3 */
	testl	$PTI_SWITCH_MASK, \scratch_reg
	jz	.Lend_\@
	andl	$(~PTI_SWITCH_MASK), \scratch_reg
	movl	\scratch_reg, %cr3
	/* Return original CR3 in \scratch_reg */
	orl	$PTI_SWITCH_MASK, \scratch_reg
.Lend_\@:
.endm

#define CS_FROM_ENTRY_STACK	(1 << 31)
#define CS_FROM_USER_CR3	(1 << 30)
#define CS_FROM_KERNEL		(1 << 29)
#define CS_FROM_ESPFIX		(1 << 28)

.macro FIXUP_FRAME
	/*
	 * The high bits of the CS dword (__csh) are used for CS_FROM_*.
	 * Clear them in case hardware didn't do this for us.
	 */
	andl	$0x0000ffff, 4*4(%esp)

#ifdef CONFIG_VM86
	testl	$X86_EFLAGS_VM, 5*4(%esp)
	jnz	.Lfrom_usermode_no_fixup_\@
#endif
	testl	$USER_SEGMENT_RPL_MASK, 4*4(%esp)
	jnz	.Lfrom_usermode_no_fixup_\@

	orl	$CS_FROM_KERNEL, 4*4(%esp)

	/*
	 * When we're here from kernel mode; the (exception) stack looks like:
	 *
	 *  6*4(%esp) - <previous context>
	 *  5*4(%esp) - flags
	 *  4*4(%esp) - cs
	 *  3*4(%esp) - ip
	 *  2*4(%esp) - orig_eax
	 *  1*4(%esp) - gs / function
	 *  0*4(%esp) - fs
	 *
	 * Lets build a 5 entry IRET frame after that, such that struct pt_regs
	 * is complete and in particular regs->sp is correct. This gives us
	 * the original 6 entries as gap:
	 *
	 * 14*4(%esp) - <previous context>
	 * 13*4(%esp) - gap / flags
	 * 12*4(%esp) - gap / cs
	 * 11*4(%esp) - gap / ip
	 * 10*4(%esp) - gap / orig_eax
	 *  9*4(%esp) - gap / gs / function
	 *  8*4(%esp) - gap / fs
	 *  7*4(%esp) - ss
	 *  6*4(%esp) - sp
	 *  5*4(%esp) - flags
	 *  4*4(%esp) - cs
	 *  3*4(%esp) - ip
	 *  2*4(%esp) - orig_eax
	 *  1*4(%esp) - gs / function
	 *  0*4(%esp) - fs
	 */

	pushl	%ss		# ss
	pushl	%esp		# sp (points at ss)
	addl	$7*4, (%esp)	# point sp back at the previous context
	pushl	7*4(%esp)	# flags
	pushl	7*4(%esp)	# cs
	pushl	7*4(%esp)	# ip
	pushl	7*4(%esp)	# orig_eax
	pushl	7*4(%esp)	# gs / function
	pushl	7*4(%esp)	# fs
.Lfrom_usermode_no_fixup_\@:
.endm

.macro IRET_FRAME
	/*
	 * We're called with %ds, %es, %fs, and %gs from the interrupted
	 * frame, so we shouldn't use them.  Also, we may be in ESPFIX
	 * mode and therefore have a nonzero SS base and an offset ESP,
	 * so any attempt to access the stack needs to use SS.  (except for
	 * accesses through %esp, which automatically use SS.)
	 */
	testl $CS_FROM_KERNEL, 1*4(%esp)
	jz .Lfinished_frame_\@

	/*
	 * Reconstruct the 3 entry IRET frame right after the (modified)
	 * regs->sp without lowering %esp in between, such that an NMI in the
	 * middle doesn't scribble our stack.
	 */
	pushl	%eax
	pushl	%ecx
	movl	5*4(%esp), %eax		# (modified) regs->sp

	movl	4*4(%esp), %ecx		# flags
	movl	%ecx, %ss:-1*4(%eax)

	movl	3*4(%esp), %ecx		# cs
	andl	$0x0000ffff, %ecx
	movl	%ecx, %ss:-2*4(%eax)

	movl	2*4(%esp), %ecx		# ip
	movl	%ecx, %ss:-3*4(%eax)

	movl	1*4(%esp), %ecx		# eax
	movl	%ecx, %ss:-4*4(%eax)

	popl	%ecx
	lea	-4*4(%eax), %esp
	popl	%eax
.Lfinished_frame_\@:
.endm

.macro SAVE_ALL pt_regs_ax=%eax switch_stacks=0 skip_gs=0 unwind_espfix=0
	cld
.if \skip_gs == 0
	pushl	$0
.endif
	pushl	%fs

	pushl	%eax
	movl	$(__KERNEL_PERCPU), %eax
	movl	%eax, %fs
.if \unwind_espfix > 0
	UNWIND_ESPFIX_STACK
.endif
	popl	%eax

	FIXUP_FRAME
	pushl	%es
	pushl	%ds
	pushl	\pt_regs_ax
	pushl	%ebp
	pushl	%edi
	pushl	%esi
	pushl	%edx
	pushl	%ecx
	pushl	%ebx
	movl	$(__USER_DS), %edx
	movl	%edx, %ds
	movl	%edx, %es
	/* Switch to kernel stack if necessary */
.if \switch_stacks > 0
	SWITCH_TO_KERNEL_STACK
.endif
.endm

.macro SAVE_ALL_NMI cr3_reg:req unwind_espfix=0
	SAVE_ALL unwind_espfix=\unwind_espfix

	BUG_IF_WRONG_CR3

	/*
	 * Now switch the CR3 when PTI is enabled.
	 *
	 * We can enter with either user or kernel cr3, the code will
	 * store the old cr3 in \cr3_reg and switches to the kernel cr3
	 * if necessary.
	 */
	SWITCH_TO_KERNEL_CR3 scratch_reg=\cr3_reg

.Lend_\@:
.endm

.macro RESTORE_INT_REGS
	popl	%ebx
	popl	%ecx
	popl	%edx
	popl	%esi
	popl	%edi
	popl	%ebp
	popl	%eax
.endm

.macro RESTORE_REGS pop=0
	RESTORE_INT_REGS
1:	popl	%ds
2:	popl	%es
3:	popl	%fs
4:	addl	$(4 + \pop), %esp	/* pop the unused "gs" slot */
	IRET_FRAME

	/*
	 * There is no _ASM_EXTABLE_TYPE_REG() for ASM, however since this is
	 * ASM the registers are known and we can trivially hard-code them.
	 */
	_ASM_EXTABLE_TYPE(1b, 2b, EX_TYPE_POP_ZERO|EX_REG_DS)
	_ASM_EXTABLE_TYPE(2b, 3b, EX_TYPE_POP_ZERO|EX_REG_ES)
	_ASM_EXTABLE_TYPE(3b, 4b, EX_TYPE_POP_ZERO|EX_REG_FS)
.endm

.macro RESTORE_ALL_NMI cr3_reg:req pop=0
	/*
	 * Now switch the CR3 when PTI is enabled.
	 *
	 * We enter with kernel cr3 and switch the cr3 to the value
	 * stored on \cr3_reg, which is either a user or a kernel cr3.
	 */
	ALTERNATIVE "jmp .Lswitched_\@", "", X86_FEATURE_PTI

	testl	$PTI_SWITCH_MASK, \cr3_reg
	jz	.Lswitched_\@

	/* User cr3 in \cr3_reg - write it to hardware cr3 */
	movl	\cr3_reg, %cr3

.Lswitched_\@:

	BUG_IF_WRONG_CR3

	RESTORE_REGS pop=\pop
.endm

.macro CHECK_AND_APPLY_ESPFIX
#ifdef CONFIG_X86_ESPFIX32
#define GDT_ESPFIX_OFFSET (GDT_ENTRY_ESPFIX_SS * 8)
#define GDT_ESPFIX_SS PER_CPU_VAR(gdt_page) + GDT_ESPFIX_OFFSET

	ALTERNATIVE	"jmp .Lend_\@", "", X86_BUG_ESPFIX

	movl	PT_EFLAGS(%esp), %eax		# mix EFLAGS, SS and CS
	/*
	 * Warning: PT_OLDSS(%esp) contains the wrong/random values if we
	 * are returning to the kernel.
	 * See comments in process.c:copy_thread() for details.
	 */
	movb	PT_OLDSS(%esp), %ah
	movb	PT_CS(%esp), %al
	andl	$(X86_EFLAGS_VM | (SEGMENT_TI_MASK << 8) | SEGMENT_RPL_MASK), %eax
	cmpl	$((SEGMENT_LDT << 8) | USER_RPL), %eax
	jne	.Lend_\@	# returning to user-space with LDT SS

	/*
	 * Setup and switch to ESPFIX stack
	 *
	 * We're returning to userspace with a 16 bit stack. The CPU will not
	 * restore the high word of ESP for us on executing iret... This is an
	 * "official" bug of all the x86-compatible CPUs, which we can work
	 * around to make dosemu and wine happy. We do this by preloading the
	 * high word of ESP with the high word of the userspace ESP while
	 * compensating for the offset by changing to the ESPFIX segment with
	 * a base address that matches for the difference.
	 */
	mov	%esp, %edx			/* load kernel esp */
	mov	PT_OLDESP(%esp), %eax		/* load userspace esp */
	mov	%dx, %ax			/* eax: new kernel esp */
	sub	%eax, %edx			/* offset (low word is 0) */
	shr	$16, %edx
	mov	%dl, GDT_ESPFIX_SS + 4		/* bits 16..23 */
	mov	%dh, GDT_ESPFIX_SS + 7		/* bits 24..31 */
	pushl	$__ESPFIX_SS
	pushl	%eax				/* new kernel esp */
	/*
	 * Disable interrupts, but do not irqtrace this section: we
	 * will soon execute iret and the tracer was already set to
	 * the irqstate after the IRET:
	 */
	cli
	lss	(%esp), %esp			/* switch to espfix segment */
.Lend_\@:
#endif /* CONFIG_X86_ESPFIX32 */
.endm

/*
 * Called with pt_regs fully populated and kernel segments loaded,
 * so we can access PER_CPU and use the integer registers.
 *
 * We need to be very careful here with the %esp switch, because an NMI
 * can happen everywhere. If the NMI handler finds itself on the
 * entry-stack, it will overwrite the task-stack and everything we
 * copied there. So allocate the stack-frame on the task-stack and
 * switch to it before we do any copying.
 */

.macro SWITCH_TO_KERNEL_STACK

	BUG_IF_WRONG_CR3

	SWITCH_TO_KERNEL_CR3 scratch_reg=%eax

	/*
	 * %eax now contains the entry cr3 and we carry it forward in
	 * that register for the time this macro runs
	 */

	/* Are we on the entry stack? Bail out if not! */
	movl	PER_CPU_VAR(cpu_entry_area), %ecx
	addl	$CPU_ENTRY_AREA_entry_stack + SIZEOF_entry_stack, %ecx
	subl	%esp, %ecx	/* ecx = (end of entry_stack) - esp */
	cmpl	$SIZEOF_entry_stack, %ecx
	jae	.Lend_\@

	/* Load stack pointer into %esi and %edi */
	movl	%esp, %esi
	movl	%esi, %edi

	/* Move %edi to the top of the entry stack */
	andl	$(MASK_entry_stack), %edi
	addl	$(SIZEOF_entry_stack), %edi

	/* Load top of task-stack into %edi */
	movl	TSS_entry2task_stack(%edi), %edi

	/* Special case - entry from kernel mode via entry stack */
#ifdef CONFIG_VM86
	movl	PT_EFLAGS(%esp), %ecx		# mix EFLAGS and CS
	movb	PT_CS(%esp), %cl
	andl	$(X86_EFLAGS_VM | SEGMENT_RPL_MASK), %ecx
#else
	movl	PT_CS(%esp), %ecx
	andl	$SEGMENT_RPL_MASK, %ecx
#endif
	cmpl	$USER_RPL, %ecx
	jb	.Lentry_from_kernel_\@

	/* Bytes to copy */
	movl	$PTREGS_SIZE, %ecx

#ifdef CONFIG_VM86
	testl	$X86_EFLAGS_VM, PT_EFLAGS(%esi)
	jz	.Lcopy_pt_regs_\@

	/*
	 * Stack-frame contains 4 additional segment registers when
	 * coming from VM86 mode
	 */
	addl	$(4 * 4), %ecx

#endif
.Lcopy_pt_regs_\@:

	/* Allocate frame on task-stack */
	subl	%ecx, %edi

	/* Switch to task-stack */
	movl	%edi, %esp

	/*
	 * We are now on the task-stack and can safely copy over the
	 * stack-frame
	 */
	shrl	$2, %ecx
	cld
	rep movsl

	jmp .Lend_\@

.Lentry_from_kernel_\@:

	/*
	 * This handles the case when we enter the kernel from
	 * kernel-mode and %esp points to the entry-stack. When this
	 * happens we need to switch to the task-stack to run C code,
	 * but switch back to the entry-stack again when we approach
	 * iret and return to the interrupted code-path. This usually
	 * happens when we hit an exception while restoring user-space
	 * segment registers on the way back to user-space or when the
	 * sysenter handler runs with eflags.tf set.
	 *
	 * When we switch to the task-stack here, we can't trust the
	 * contents of the entry-stack anymore, as the exception handler
	 * might be scheduled out or moved to another CPU. Therefore we
	 * copy the complete entry-stack to the task-stack and set a
	 * marker in the iret-frame (bit 31 of the CS dword) to detect
	 * what we've done on the iret path.
	 *
	 * On the iret path we copy everything back and switch to the
	 * entry-stack, so that the interrupted kernel code-path
	 * continues on the same stack it was interrupted with.
	 *
	 * Be aware that an NMI can happen anytime in this code.
	 *
	 * %esi: Entry-Stack pointer (same as %esp)
	 * %edi: Top of the task stack
	 * %eax: CR3 on kernel entry
	 */

	/* Calculate number of bytes on the entry stack in %ecx */
	movl	%esi, %ecx

	/* %ecx to the top of entry-stack */
	andl	$(MASK_entry_stack), %ecx
	addl	$(SIZEOF_entry_stack), %ecx

	/* Number of bytes on the entry stack to %ecx */
	sub	%esi, %ecx

	/* Mark stackframe as coming from entry stack */
	orl	$CS_FROM_ENTRY_STACK, PT_CS(%esp)

	/*
	 * Test the cr3 used to enter the kernel and add a marker
	 * so that we can switch back to it before iret.
	 */
	testl	$PTI_SWITCH_MASK, %eax
	jz	.Lcopy_pt_regs_\@
	orl	$CS_FROM_USER_CR3, PT_CS(%esp)

	/*
	 * %esi and %edi are unchanged, %ecx contains the number of
	 * bytes to copy. The code at .Lcopy_pt_regs_\@ will allocate
	 * the stack-frame on task-stack and copy everything over
	 */
	jmp .Lcopy_pt_regs_\@

.Lend_\@:
.endm

/*
 * Switch back from the kernel stack to the entry stack.
 *
 * The %esp register must point to pt_regs on the task stack. It will
 * first calculate the size of the stack-frame to copy, depending on
 * whether we return to VM86 mode or not. With that it uses 'rep movsl'
 * to copy the contents of the stack over to the entry stack.
 *
 * We must be very careful here, as we can't trust the contents of the
 * task-stack once we switched to the entry-stack. When an NMI happens
 * while on the entry-stack, the NMI handler will switch back to the top
 * of the task stack, overwriting our stack-frame we are about to copy.
 * Therefore we switch the stack only after everything is copied over.
 */
.macro SWITCH_TO_ENTRY_STACK

	/* Bytes to copy */
	movl	$PTREGS_SIZE, %ecx

#ifdef CONFIG_VM86
	testl	$(X86_EFLAGS_VM), PT_EFLAGS(%esp)
	jz	.Lcopy_pt_regs_\@

	/* Additional 4 registers to copy when returning to VM86 mode */
	addl    $(4 * 4), %ecx

.Lcopy_pt_regs_\@:
#endif

	/* Initialize source and destination for movsl */
	movl	PER_CPU_VAR(cpu_tss_rw + TSS_sp0), %edi
	subl	%ecx, %edi
	movl	%esp, %esi

	/* Save future stack pointer in %ebx */
	movl	%edi, %ebx

	/* Copy over the stack-frame */
	shrl	$2, %ecx
	cld
	rep movsl

	/*
	 * Switch to entry-stack - needs to happen after everything is
	 * copied because the NMI handler will overwrite the task-stack
	 * when on entry-stack
	 */
	movl	%ebx, %esp

.Lend_\@:
.endm

/*
 * This macro handles the case when we return to kernel-mode on the iret
 * path and have to switch back to the entry stack and/or user-cr3
 *
 * See the comments below the .Lentry_from_kernel_\@ label in the
 * SWITCH_TO_KERNEL_STACK macro for more details.
 */
.macro PARANOID_EXIT_TO_KERNEL_MODE

	/*
	 * Test if we entered the kernel with the entry-stack. Most
	 * likely we did not, because this code only runs on the
	 * return-to-kernel path.
	 */
	testl	$CS_FROM_ENTRY_STACK, PT_CS(%esp)
	jz	.Lend_\@

	/* Unlikely slow-path */

	/* Clear marker from stack-frame */
	andl	$(~CS_FROM_ENTRY_STACK), PT_CS(%esp)

	/* Copy the remaining task-stack contents to entry-stack */
	movl	%esp, %esi
	movl	PER_CPU_VAR(cpu_tss_rw + TSS_sp0), %edi

	/* Bytes on the task-stack to ecx */
	movl	PER_CPU_VAR(cpu_tss_rw + TSS_sp1), %ecx
	subl	%esi, %ecx

	/* Allocate stack-frame on entry-stack */
	subl	%ecx, %edi

	/*
	 * Save future stack-pointer, we must not switch until the
	 * copy is done, otherwise the NMI handler could destroy the
	 * contents of the task-stack we are about to copy.
	 */
	movl	%edi, %ebx

	/* Do the copy */
	shrl	$2, %ecx
	cld
	rep movsl

	/* Safe to switch to entry-stack now */
	movl	%ebx, %esp

	/*
	 * We came from entry-stack and need to check if we also need to
	 * switch back to user cr3.
	 */
	testl	$CS_FROM_USER_CR3, PT_CS(%esp)
	jz	.Lend_\@

	/* Clear marker from stack-frame */
	andl	$(~CS_FROM_USER_CR3), PT_CS(%esp)

	SWITCH_TO_USER_CR3 scratch_reg=%eax

.Lend_\@:
.endm

/**
 * idtentry - Macro to generate entry stubs for simple IDT entries
 * @vector:		Vector number
 * @asmsym:		ASM symbol for the entry point
 * @cfunc:		C function to be called
 * @has_error_code:	Hardware pushed error code on stack
 */
.macro idtentry vector asmsym cfunc has_error_code:req
SYM_CODE_START(\asmsym)
	ASM_CLAC
	cld

	.if \has_error_code == 0
		pushl	$0		/* Clear the error code */
	.endif

	/* Push the C-function address into the GS slot */
	pushl	$\cfunc
	/* Invoke the common exception entry */
	jmp	handle_exception
SYM_CODE_END(\asmsym)
.endm

.macro idtentry_irq vector cfunc
	.p2align CONFIG_X86_L1_CACHE_SHIFT
SYM_CODE_START_LOCAL(asm_\cfunc)
	ASM_CLAC
	SAVE_ALL switch_stacks=1
	ENCODE_FRAME_POINTER
	movl	%esp, %eax
	movl	PT_ORIG_EAX(%esp), %edx		/* get the vector from stack */
	movl	$-1, PT_ORIG_EAX(%esp)		/* no syscall to restart */
	call	\cfunc
	jmp	handle_exception_return
SYM_CODE_END(asm_\cfunc)
.endm

.macro idtentry_sysvec vector cfunc
	idtentry \vector asm_\cfunc \cfunc has_error_code=0
.endm

/*
 * Include the defines which emit the idt entries which are shared
 * shared between 32 and 64 bit and emit the __irqentry_text_* markers
 * so the stacktrace boundary checks work.
 */
	.align 16
	.globl __irqentry_text_start
__irqentry_text_start:

#include <asm/idtentry.h>

	.align 16
	.globl __irqentry_text_end
__irqentry_text_end:

/*
 * %eax: prev task
 * %edx: next task
 */
.pushsection .text, "ax"
SYM_CODE_START(__switch_to_asm)
	/*
	 * Save callee-saved registers
	 * This must match the order in struct inactive_task_frame
	 */
	pushl	%ebp
	pushl	%ebx
	pushl	%edi
	pushl	%esi
	/*
	 * Flags are saved to prevent AC leakage. This could go
	 * away if objtool would have 32bit support to verify
	 * the STAC/CLAC correctness.
	 */
	pushfl

	/* switch stack */
	movl	%esp, TASK_threadsp(%eax)
	movl	TASK_threadsp(%edx), %esp

#ifdef CONFIG_STACKPROTECTOR
	movl	TASK_stack_canary(%edx), %ebx
	movl	%ebx, PER_CPU_VAR(__stack_chk_guard)
#endif

	/*
	 * When switching from a shallower to a deeper call stack
	 * the RSB may either underflow or use entries populated
	 * with userspace addresses. On CPUs where those concerns
	 * exist, overwrite the RSB with entries which capture
	 * speculative execution to prevent attack.
	 */
	FILL_RETURN_BUFFER %ebx, RSB_CLEAR_LOOPS, X86_FEATURE_RSB_CTXSW

	/* Restore flags or the incoming task to restore AC state. */
	popfl
	/* restore callee-saved registers */
	popl	%esi
	popl	%edi
	popl	%ebx
	popl	%ebp

	jmp	__switch_to
SYM_CODE_END(__switch_to_asm)
.popsection

/*
 * The unwinder expects the last frame on the stack to always be at the same
 * offset from the end of the page, which allows it to validate the stack.
 * Calling schedule_tail() directly would break that convention because its an
 * asmlinkage function so its argument has to be pushed on the stack.  This
 * wrapper creates a proper "end of stack" frame header before the call.
 */
.pushsection .text, "ax"
SYM_FUNC_START(schedule_tail_wrapper)
	FRAME_BEGIN

	pushl	%eax
	call	schedule_tail
	popl	%eax

	FRAME_END
	RET
SYM_FUNC_END(schedule_tail_wrapper)
.popsection

/*
 * A newly forked process directly context switches into this address.
 *
 * eax: prev task we switched from
 * ebx: kernel thread func (NULL for user thread)
 * edi: kernel thread arg
 */
.pushsection .text, "ax"
SYM_CODE_START(ret_from_fork)
	call	schedule_tail_wrapper

	testl	%ebx, %ebx
	jnz	1f		/* kernel threads are uncommon */

2:
	/* When we fork, we trace the syscall return in the child, too. */
	movl    %esp, %eax
	call    syscall_exit_to_user_mode
	jmp     .Lsyscall_32_done

	/* kernel thread */
1:	movl	%edi, %eax
	CALL_NOSPEC ebx
	/*
	 * A kernel thread is allowed to return here after successfully
	 * calling kernel_execve().  Exit to userspace to complete the execve()
	 * syscall.
	 */
	movl	$0, PT_EAX(%esp)
	jmp	2b
SYM_CODE_END(ret_from_fork)
.popsection

SYM_ENTRY(__begin_SYSENTER_singlestep_region, SYM_L_GLOBAL, SYM_A_NONE)
/*
 * All code from here through __end_SYSENTER_singlestep_region is subject
 * to being single-stepped if a user program sets TF and executes SYSENTER.
 * There is absolutely nothing that we can do to prevent this from happening
 * (thanks Intel!).  To keep our handling of this situation as simple as
 * possible, we handle TF just like AC and NT, except that our #DB handler
 * will ignore all of the single-step traps generated in this range.
 */

/*
 * 32-bit SYSENTER entry.
 *
 * 32-bit system calls through the vDSO's __kernel_vsyscall enter here
 * if X86_FEATURE_SEP is available.  This is the preferred system call
 * entry on 32-bit systems.
 *
 * The SYSENTER instruction, in principle, should *only* occur in the
 * vDSO.  In practice, a small number of Android devices were shipped
 * with a copy of Bionic that inlined a SYSENTER instruction.  This
 * never happened in any of Google's Bionic versions -- it only happened
 * in a narrow range of Intel-provided versions.
 *
 * SYSENTER loads SS, ESP, CS, and EIP from previously programmed MSRs.
 * IF and VM in RFLAGS are cleared (IOW: interrupts are off).
 * SYSENTER does not save anything on the stack,
 * and does not save old EIP (!!!), ESP, or EFLAGS.
 *
 * To avoid losing track of EFLAGS.VM (and thus potentially corrupting
 * user and/or vm86 state), we explicitly disable the SYSENTER
 * instruction in vm86 mode by reprogramming the MSRs.
 *
 * Arguments:
 * eax  system call number
 * ebx  arg1
 * ecx  arg2
 * edx  arg3
 * esi  arg4
 * edi  arg5
 * ebp  user stack
 * 0(%ebp) arg6
 */
SYM_FUNC_START(entry_SYSENTER_32)
	/*
	 * On entry-stack with all userspace-regs live - save and
	 * restore eflags and %eax to use it as scratch-reg for the cr3
	 * switch.
	 */
	pushfl
	pushl	%eax
	BUG_IF_WRONG_CR3 no_user_check=1
	SWITCH_TO_KERNEL_CR3 scratch_reg=%eax
	popl	%eax
	popfl

	/* Stack empty again, switch to task stack */
	movl	TSS_entry2task_stack(%esp), %esp

.Lsysenter_past_esp:
	pushl	$__USER_DS		/* pt_regs->ss */
	pushl	$0			/* pt_regs->sp (placeholder) */
	pushfl				/* pt_regs->flags (except IF = 0) */
	pushl	$__USER_CS		/* pt_regs->cs */
	pushl	$0			/* pt_regs->ip = 0 (placeholder) */
	pushl	%eax			/* pt_regs->orig_ax */
	SAVE_ALL pt_regs_ax=$-ENOSYS	/* save rest, stack already switched */

	/*
	 * SYSENTER doesn't filter flags, so we need to clear NT, AC
	 * and TF ourselves.  To save a few cycles, we can check whether
	 * either was set instead of doing an unconditional popfq.
	 * This needs to happen before enabling interrupts so that
	 * we don't get preempted with NT set.
	 *
	 * If TF is set, we will single-step all the way to here -- do_debug
	 * will ignore all the traps.  (Yes, this is slow, but so is
	 * single-stepping in general.  This allows us to avoid having
	 * a more complicated code to handle the case where a user program
	 * forces us to single-step through the SYSENTER entry code.)
	 *
	 * NB.: .Lsysenter_fix_flags is a label with the code under it moved
	 * out-of-line as an optimization: NT is unlikely to be set in the
	 * majority of the cases and instead of polluting the I$ unnecessarily,
	 * we're keeping that code behind a branch which will predict as
	 * not-taken and therefore its instructions won't be fetched.
	 */
	testl	$X86_EFLAGS_NT|X86_EFLAGS_AC|X86_EFLAGS_TF, PT_EFLAGS(%esp)
	jnz	.Lsysenter_fix_flags
.Lsysenter_flags_fixed:

	movl	%esp, %eax
	call	do_SYSENTER_32
	testl	%eax, %eax
	jz	.Lsyscall_32_done

	STACKLEAK_ERASE

	/* Opportunistic SYSEXIT */

	/*
	 * Setup entry stack - we keep the pointer in %eax and do the
	 * switch after almost all user-state is restored.
	 */

	/* Load entry stack pointer and allocate frame for eflags/eax */
	movl	PER_CPU_VAR(cpu_tss_rw + TSS_sp0), %eax
	subl	$(2*4), %eax

	/* Copy eflags and eax to entry stack */
	movl	PT_EFLAGS(%esp), %edi
	movl	PT_EAX(%esp), %esi
	movl	%edi, (%eax)
	movl	%esi, 4(%eax)

	/* Restore user registers and segments */
	movl	PT_EIP(%esp), %edx	/* pt_regs->ip */
	movl	PT_OLDESP(%esp), %ecx	/* pt_regs->sp */
1:	mov	PT_FS(%esp), %fs

	popl	%ebx			/* pt_regs->bx */
	addl	$2*4, %esp		/* skip pt_regs->cx and pt_regs->dx */
	popl	%esi			/* pt_regs->si */
	popl	%edi			/* pt_regs->di */
	popl	%ebp			/* pt_regs->bp */

	/* Switch to entry stack */
	movl	%eax, %esp

	/* Now ready to switch the cr3 */
	SWITCH_TO_USER_CR3 scratch_reg=%eax

	/*
	 * Restore all flags except IF. (We restore IF separately because
	 * STI gives a one-instruction window in which we won't be interrupted,
	 * whereas POPF does not.)
	 */
	btrl	$X86_EFLAGS_IF_BIT, (%esp)
	BUG_IF_WRONG_CR3 no_user_check=1
	popfl
	popl	%eax
	CLEAR_CPU_BUFFERS

	/*
	 * Return back to the vDSO, which will pop ecx and edx.
	 * Don't bother with DS and ES (they already contain __USER_DS).
	 */
	sti
	sysexit

2:	movl    $0, PT_FS(%esp)
	jmp     1b
	_ASM_EXTABLE(1b, 2b)

.Lsysenter_fix_flags:
	pushl	$X86_EFLAGS_FIXED
	popfl
	jmp	.Lsysenter_flags_fixed
SYM_ENTRY(__end_SYSENTER_singlestep_region, SYM_L_GLOBAL, SYM_A_NONE)
SYM_FUNC_END(entry_SYSENTER_32)

/*
 * 32-bit legacy system call entry.
 *
 * 32-bit x86 Linux system calls traditionally used the INT $0x80
 * instruction.  INT $0x80 lands here.
 *
 * This entry point can be used by any 32-bit perform system calls.
 * Instances of INT $0x80 can be found inline in various programs and
 * libraries.  It is also used by the vDSO's __kernel_vsyscall
 * fallback for hardware that doesn't support a faster entry method.
 * Restarted 32-bit system calls also fall back to INT $0x80
 * regardless of what instruction was originally used to do the system
 * call.  (64-bit programs can use INT $0x80 as well, but they can
 * only run on 64-bit kernels and therefore land in
 * entry_INT80_compat.)
 *
 * This is considered a slow path.  It is not used by most libc
 * implementations on modern hardware except during process startup.
 *
 * Arguments:
 * eax  system call number
 * ebx  arg1
 * ecx  arg2
 * edx  arg3
 * esi  arg4
 * edi  arg5
 * ebp  arg6
 */
SYM_FUNC_START(entry_INT80_32)
	ASM_CLAC
	pushl	%eax			/* pt_regs->orig_ax */

	SAVE_ALL pt_regs_ax=$-ENOSYS switch_stacks=1	/* save rest */

	movl	%esp, %eax
	call	do_int80_syscall_32
.Lsyscall_32_done:
	STACKLEAK_ERASE

restore_all_switch_stack:
	SWITCH_TO_ENTRY_STACK
	CHECK_AND_APPLY_ESPFIX

	/* Switch back to user CR3 */
	SWITCH_TO_USER_CR3 scratch_reg=%eax

	BUG_IF_WRONG_CR3

	/* Restore user state */
	RESTORE_REGS pop=4			# skip orig_eax/error_code
	CLEAR_CPU_BUFFERS
.Lirq_return:
	/*
	 * ARCH_HAS_MEMBARRIER_SYNC_CORE rely on IRET core serialization
	 * when returning from IPI handler and when returning from
	 * scheduler to user-space.
	 */
	iret

.Lasm_iret_error:
	pushl	$0				# no error code
	pushl	$iret_error

#ifdef CONFIG_DEBUG_ENTRY
	/*
	 * The stack-frame here is the one that iret faulted on, so its a
	 * return-to-user frame. We are on kernel-cr3 because we come here from
	 * the fixup code. This confuses the CR3 checker, so switch to user-cr3
	 * as the checker expects it.
	 */
	pushl	%eax
	SWITCH_TO_USER_CR3 scratch_reg=%eax
	popl	%eax
#endif

	jmp	handle_exception

	_ASM_EXTABLE(.Lirq_return, .Lasm_iret_error)
SYM_FUNC_END(entry_INT80_32)

.macro FIXUP_ESPFIX_STACK
/*
 * Switch back for ESPFIX stack to the normal zerobased stack
 *
 * We can't call C functions using the ESPFIX stack. This code reads
 * the high word of the segment base from the GDT and swiches to the
 * normal stack and adjusts ESP with the matching offset.
 *
 * We might be on user CR3 here, so percpu data is not mapped and we can't
 * access the GDT through the percpu segment.  Instead, use SGDT to find
 * the cpu_entry_area alias of the GDT.
 */
#ifdef CONFIG_X86_ESPFIX32
	/* fixup the stack */
	pushl	%ecx
	subl	$2*4, %esp
	sgdt	(%esp)
	movl	2(%esp), %ecx				/* GDT address */
	/*
	 * Careful: ECX is a linear pointer, so we need to force base
	 * zero.  %cs is the only known-linear segment we have right now.
	 */
	mov	%cs:GDT_ESPFIX_OFFSET + 4(%ecx), %al	/* bits 16..23 */
	mov	%cs:GDT_ESPFIX_OFFSET + 7(%ecx), %ah	/* bits 24..31 */
	shl	$16, %eax
	addl	$2*4, %esp
	popl	%ecx
	addl	%esp, %eax			/* the adjusted stack pointer */
	pushl	$__KERNEL_DS
	pushl	%eax
	lss	(%esp), %esp			/* switch to the normal stack segment */
#endif
.endm

.macro UNWIND_ESPFIX_STACK
	/* It's safe to clobber %eax, all other regs need to be preserved */
#ifdef CONFIG_X86_ESPFIX32
	movl	%ss, %eax
	/* see if on espfix stack */
	cmpw	$__ESPFIX_SS, %ax
	jne	.Lno_fixup_\@
	/* switch to normal stack */
	FIXUP_ESPFIX_STACK
.Lno_fixup_\@:
#endif
.endm

SYM_CODE_START_LOCAL_NOALIGN(handle_exception)
	/* the function address is in %gs's slot on the stack */
	SAVE_ALL switch_stacks=1 skip_gs=1 unwind_espfix=1
	ENCODE_FRAME_POINTER

	movl	PT_GS(%esp), %edi		# get the function address

	/* fixup orig %eax */
	movl	PT_ORIG_EAX(%esp), %edx		# get the error code
	movl	$-1, PT_ORIG_EAX(%esp)		# no syscall to restart

	movl	%esp, %eax			# pt_regs pointer
	CALL_NOSPEC edi

handle_exception_return:
#ifdef CONFIG_VM86
	movl	PT_EFLAGS(%esp), %eax		# mix EFLAGS and CS
	movb	PT_CS(%esp), %al
	andl	$(X86_EFLAGS_VM | SEGMENT_RPL_MASK), %eax
#else
	/*
	 * We can be coming here from child spawned by kernel_thread().
	 */
	movl	PT_CS(%esp), %eax
	andl	$SEGMENT_RPL_MASK, %eax
#endif
	cmpl	$USER_RPL, %eax			# returning to v8086 or userspace ?
	jnb	ret_to_user

	PARANOID_EXIT_TO_KERNEL_MODE
	BUG_IF_WRONG_CR3
	RESTORE_REGS 4
	jmp	.Lirq_return

ret_to_user:
	movl	%esp, %eax
	jmp	restore_all_switch_stack
SYM_CODE_END(handle_exception)

SYM_CODE_START(asm_exc_double_fault)
1:
	/*
	 * This is a task gate handler, not an interrupt gate handler.
	 * The error code is on the stack, but the stack is otherwise
	 * empty.  Interrupts are off.  Our state is sane with the following
	 * exceptions:
	 *
	 *  - CR0.TS is set.  "TS" literally means "task switched".
	 *  - EFLAGS.NT is set because we're a "nested task".
	 *  - The doublefault TSS has back_link set and has been marked busy.
	 *  - TR points to the doublefault TSS and the normal TSS is busy.
	 *  - CR3 is the normal kernel PGD.  This would be delightful, except
	 *    that the CPU didn't bother to save the old CR3 anywhere.  This
	 *    would make it very awkward to return back to the context we came
	 *    from.
	 *
	 * The rest of EFLAGS is sanitized for us, so we don't need to
	 * worry about AC or DF.
	 *
	 * Don't even bother popping the error code.  It's always zero,
	 * and ignoring it makes us a bit more robust against buggy
	 * hypervisor task gate implementations.
	 *
	 * We will manually undo the task switch instead of doing a
	 * task-switching IRET.
	 */

	clts				/* clear CR0.TS */
	pushl	$X86_EFLAGS_FIXED
	popfl				/* clear EFLAGS.NT */

	call	doublefault_shim

	/* We don't support returning, so we have no IRET here. */
1:
	hlt
	jmp 1b
SYM_CODE_END(asm_exc_double_fault)

/*
 * NMI is doubly nasty.  It can happen on the first instruction of
 * entry_SYSENTER_32 (just like #DB), but it can also interrupt the beginning
 * of the #DB handler even if that #DB in turn hit before entry_SYSENTER_32
 * switched stacks.  We handle both conditions by simply checking whether we
 * interrupted kernel code running on the SYSENTER stack.
 */
SYM_CODE_START(asm_exc_nmi)
	ASM_CLAC

#ifdef CONFIG_X86_ESPFIX32
	/*
	 * ESPFIX_SS is only ever set on the return to user path
	 * after we've switched to the entry stack.
	 */
	pushl	%eax
	movl	%ss, %eax
	cmpw	$__ESPFIX_SS, %ax
	popl	%eax
	je	.Lnmi_espfix_stack
#endif

	pushl	%eax				# pt_regs->orig_ax
	SAVE_ALL_NMI cr3_reg=%edi
	ENCODE_FRAME_POINTER
	xorl	%edx, %edx			# zero error code
	movl	%esp, %eax			# pt_regs pointer

	/* Are we currently on the SYSENTER stack? */
	movl	PER_CPU_VAR(cpu_entry_area), %ecx
	addl	$CPU_ENTRY_AREA_entry_stack + SIZEOF_entry_stack, %ecx
	subl	%eax, %ecx	/* ecx = (end of entry_stack) - esp */
	cmpl	$SIZEOF_entry_stack, %ecx
	jb	.Lnmi_from_sysenter_stack

	/* Not on SYSENTER stack. */
	call	exc_nmi
	CLEAR_CPU_BUFFERS
	jmp	.Lnmi_return

.Lnmi_from_sysenter_stack:
	/*
	 * We're on the SYSENTER stack.  Switch off.  No one (not even debug)
	 * is using the thread stack right now, so it's safe for us to use it.
	 */
	movl	%esp, %ebx
	movl	PER_CPU_VAR(cpu_current_top_of_stack), %esp
	call	exc_nmi
	movl	%ebx, %esp

.Lnmi_return:
#ifdef CONFIG_X86_ESPFIX32
	testl	$CS_FROM_ESPFIX, PT_CS(%esp)
	jnz	.Lnmi_from_espfix
#endif

	CHECK_AND_APPLY_ESPFIX
	RESTORE_ALL_NMI cr3_reg=%edi pop=4
	jmp	.Lirq_return

#ifdef CONFIG_X86_ESPFIX32
.Lnmi_espfix_stack:
	/*
	 * Create the pointer to LSS back
	 */
	pushl	%ss
	pushl	%esp
	addl	$4, (%esp)

	/* Copy the (short) IRET frame */
	pushl	4*4(%esp)	# flags
	pushl	4*4(%esp)	# cs
	pushl	4*4(%esp)	# ip

	pushl	%eax		# orig_ax

	SAVE_ALL_NMI cr3_reg=%edi unwind_espfix=1
	ENCODE_FRAME_POINTER

	/* clear CS_FROM_KERNEL, set CS_FROM_ESPFIX */
	xorl	$(CS_FROM_ESPFIX | CS_FROM_KERNEL), PT_CS(%esp)

	xorl	%edx, %edx			# zero error code
	movl	%esp, %eax			# pt_regs pointer
	jmp	.Lnmi_from_sysenter_stack

.Lnmi_from_espfix:
	RESTORE_ALL_NMI cr3_reg=%edi
	/*
	 * Because we cleared CS_FROM_KERNEL, IRET_FRAME 'forgot' to
	 * fix up the gap and long frame:
	 *
	 *  3 - original frame	(exception)
	 *  2 - ESPFIX block	(above)
	 *  6 - gap		(FIXUP_FRAME)
	 *  5 - long frame	(FIXUP_FRAME)
	 *  1 - orig_ax
	 */
	lss	(1+5+6)*4(%esp), %esp			# back to espfix stack
	jmp	.Lirq_return
#endif
SYM_CODE_END(asm_exc_nmi)

.pushsection .text, "ax"
SYM_CODE_START(rewind_stack_and_make_dead)
	/* Prevent any naive code from trying to unwind to our caller. */
	xorl	%ebp, %ebp

	movl	PER_CPU_VAR(cpu_current_top_of_stack), %esi
	leal	-TOP_OF_KERNEL_STACK_PADDING-PTREGS_SIZE(%esi), %esp

	call	make_task_dead
1:	jmp 1b
SYM_CODE_END(rewind_stack_and_make_dead)
.popsection
