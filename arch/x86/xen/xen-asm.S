/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Asm versions of Xen pv-ops, suitable for direct use.
 *
 * We only bother with direct forms (ie, vcpu in percpu data) of the
 * operations here; the indirect forms are better handled in C.
 */

#include <asm/errno.h>
#include <asm/asm-offsets.h>
#include <asm/percpu.h>
#include <asm/processor-flags.h>
#include <asm/segment.h>
#include <asm/thread_info.h>
#include <asm/asm.h>
#include <asm/frame.h>
#include <asm/unwind_hints.h>

#include <xen/interface/xen.h>

#include <linux/init.h>
#include <linux/linkage.h>

.pushsection .noinstr.text, "ax"
/*
 * Disabling events is simply a matter of making the event mask
 * non-zero.
 */
SYM_FUNC_START(xen_irq_disable_direct)
	movb $1, PER_CPU_VAR(xen_vcpu_info) + XEN_vcpu_info_mask
	ret
SYM_FUNC_END(xen_irq_disable_direct)

/*
 * Force an event check by making a hypercall, but preserve regs
 * before making the call.
 */
SYM_FUNC_START(check_events)
	FRAME_BEGIN
	push %rax
	push %rcx
	push %rdx
	push %rsi
	push %rdi
	push %r8
	push %r9
	push %r10
	push %r11
	call xen_force_evtchn_callback
	pop %r11
	pop %r10
	pop %r9
	pop %r8
	pop %rdi
	pop %rsi
	pop %rdx
	pop %rcx
	pop %rax
	FRAME_END
	ret
SYM_FUNC_END(check_events)

/*
 * Enable events.  This clears the event mask and tests the pending
 * event status with one and operation.  If there are pending events,
 * then enter the hypervisor to get them handled.
 */
SYM_FUNC_START(xen_irq_enable_direct)
	FRAME_BEGIN
	/* Unmask events */
	movb $0, PER_CPU_VAR(xen_vcpu_info) + XEN_vcpu_info_mask

	/*
	 * Preempt here doesn't matter because that will deal with any
	 * pending interrupts.  The pending check may end up being run
	 * on the wrong CPU, but that doesn't hurt.
	 */

	/* Test for pending */
	testb $0xff, PER_CPU_VAR(xen_vcpu_info) + XEN_vcpu_info_pending
	jz 1f

	call check_events
1:
	FRAME_END
	ret
SYM_FUNC_END(xen_irq_enable_direct)

/*
 * (xen_)save_fl is used to get the current interrupt enable status.
 * Callers expect the status to be in X86_EFLAGS_IF, and other bits
 * may be set in the return value.  We take advantage of this by
 * making sure that X86_EFLAGS_IF has the right value (and other bits
 * in that byte are 0), but other bits in the return value are
 * undefined.  We need to toggle the state of the bit, because Xen and
 * x86 use opposite senses (mask vs enable).
 */
SYM_FUNC_START(xen_save_fl_direct)
	testb $0xff, PER_CPU_VAR(xen_vcpu_info) + XEN_vcpu_info_mask
	setz %ah
	addb %ah, %ah
	ret
SYM_FUNC_END(xen_save_fl_direct)

SYM_FUNC_START(xen_read_cr2)
	FRAME_BEGIN
	_ASM_MOV PER_CPU_VAR(xen_vcpu), %_ASM_AX
	_ASM_MOV XEN_vcpu_info_arch_cr2(%_ASM_AX), %_ASM_AX
	FRAME_END
	ret
SYM_FUNC_END(xen_read_cr2);

SYM_FUNC_START(xen_read_cr2_direct)
	FRAME_BEGIN
	_ASM_MOV PER_CPU_VAR(xen_vcpu_info) + XEN_vcpu_info_arch_cr2, %_ASM_AX
	FRAME_END
	ret
SYM_FUNC_END(xen_read_cr2_direct);
.popsection

.macro xen_pv_trap name
SYM_CODE_START(xen_\name)
	UNWIND_HINT_EMPTY
	pop %rcx
	pop %r11
	jmp  \name
SYM_CODE_END(xen_\name)
_ASM_NOKPROBE(xen_\name)
.endm

xen_pv_trap asm_exc_divide_error
xen_pv_trap asm_xenpv_exc_debug
xen_pv_trap asm_exc_int3
xen_pv_trap asm_xenpv_exc_nmi
xen_pv_trap asm_exc_overflow
xen_pv_trap asm_exc_bounds
xen_pv_trap asm_exc_invalid_op
xen_pv_trap asm_exc_device_not_available
xen_pv_trap asm_xenpv_exc_double_fault
xen_pv_trap asm_exc_coproc_segment_overrun
xen_pv_trap asm_exc_invalid_tss
xen_pv_trap asm_exc_segment_not_present
xen_pv_trap asm_exc_stack_segment
xen_pv_trap asm_exc_general_protection
xen_pv_trap asm_exc_page_fault
xen_pv_trap asm_exc_spurious_interrupt_bug
xen_pv_trap asm_exc_coprocessor_error
xen_pv_trap asm_exc_alignment_check
#ifdef CONFIG_X86_MCE
xen_pv_trap asm_xenpv_exc_machine_check
#endif /* CONFIG_X86_MCE */
xen_pv_trap asm_exc_simd_coprocessor_error
#ifdef CONFIG_IA32_EMULATION
xen_pv_trap entry_INT80_compat
#endif
xen_pv_trap asm_exc_xen_unknown_trap
xen_pv_trap asm_exc_xen_hypervisor_callback

	__INIT
SYM_CODE_START(xen_early_idt_handler_array)
	i = 0
	.rept NUM_EXCEPTION_VECTORS
	UNWIND_HINT_EMPTY
	pop %rcx
	pop %r11
	jmp early_idt_handler_array + i*EARLY_IDT_HANDLER_SIZE
	i = i + 1
	.fill xen_early_idt_handler_array + i*XEN_EARLY_IDT_HANDLER_SIZE - ., 1, 0xcc
	.endr
SYM_CODE_END(xen_early_idt_handler_array)
	__FINIT

hypercall_iret = hypercall_page + __HYPERVISOR_iret * 32
/*
 * Xen64 iret frame:
 *
 *	ss
 *	rsp
 *	rflags
 *	cs
 *	rip		<-- standard iret frame
 *
 *	flags
 *
 *	rcx		}
 *	r11		}<-- pushed by hypercall page
 * rsp->rax		}
 */
SYM_CODE_START(xen_iret)
	UNWIND_HINT_EMPTY
	pushq $0
	jmp hypercall_iret
SYM_CODE_END(xen_iret)

/*
 * Xen handles syscall callbacks much like ordinary exceptions, which
 * means we have:
 * - kernel gs
 * - kernel rsp
 * - an iret-like stack frame on the stack (including rcx and r11):
 *	ss
 *	rsp
 *	rflags
 *	cs
 *	rip
 *	r11
 * rsp->rcx
 */

/* Normal 64-bit system call target */
SYM_CODE_START(xen_syscall_target)
	UNWIND_HINT_EMPTY
	popq %rcx
	popq %r11

	/*
	 * Neither Xen nor the kernel really knows what the old SS and
	 * CS were.  The kernel expects __USER_DS and __USER_CS, so
	 * report those values even though Xen will guess its own values.
	 */
	movq $__USER_DS, 4*8(%rsp)
	movq $__USER_CS, 1*8(%rsp)

	jmp entry_SYSCALL_64_after_hwframe
SYM_CODE_END(xen_syscall_target)

#ifdef CONFIG_IA32_EMULATION

/* 32-bit compat syscall target */
SYM_CODE_START(xen_syscall32_target)
	UNWIND_HINT_EMPTY
	popq %rcx
	popq %r11

	/*
	 * Neither Xen nor the kernel really knows what the old SS and
	 * CS were.  The kernel expects __USER32_DS and __USER32_CS, so
	 * report those values even though Xen will guess its own values.
	 */
	movq $__USER32_DS, 4*8(%rsp)
	movq $__USER32_CS, 1*8(%rsp)

	jmp entry_SYSCALL_compat_after_hwframe
SYM_CODE_END(xen_syscall32_target)

/* 32-bit compat sysenter target */
SYM_CODE_START(xen_sysenter_target)
	UNWIND_HINT_EMPTY
	/*
	 * NB: Xen is polite and clears TF from EFLAGS for us.  This means
	 * that we don't need to guard against single step exceptions here.
	 */
	popq %rcx
	popq %r11

	/*
	 * Neither Xen nor the kernel really knows what the old SS and
	 * CS were.  The kernel expects __USER32_DS and __USER32_CS, so
	 * report those values even though Xen will guess its own values.
	 */
	movq $__USER32_DS, 4*8(%rsp)
	movq $__USER32_CS, 1*8(%rsp)

	jmp entry_SYSENTER_compat_after_hwframe
SYM_CODE_END(xen_sysenter_target)

#else /* !CONFIG_IA32_EMULATION */

SYM_CODE_START(xen_syscall32_target)
SYM_CODE_START(xen_sysenter_target)
	UNWIND_HINT_EMPTY
	lea 16(%rsp), %rsp	/* strip %rcx, %r11 */
	mov $-ENOSYS, %rax
	pushq $0
	jmp hypercall_iret
SYM_CODE_END(xen_sysenter_target)
SYM_CODE_END(xen_syscall32_target)

#endif	/* CONFIG_IA32_EMULATION */
