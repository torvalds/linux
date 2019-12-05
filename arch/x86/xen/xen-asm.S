/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Asm versions of Xen pv-ops, suitable for direct use.
 *
 * We only bother with direct forms (ie, vcpu in percpu data) of the
 * operations here; the indirect forms are better handled in C.
 */

#include <asm/asm-offsets.h>
#include <asm/percpu.h>
#include <asm/processor-flags.h>
#include <asm/frame.h>
#include <asm/asm.h>

#include <linux/linkage.h>

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
 * Disabling events is simply a matter of making the event mask
 * non-zero.
 */
SYM_FUNC_START(xen_irq_disable_direct)
	movb $1, PER_CPU_VAR(xen_vcpu_info) + XEN_vcpu_info_mask
	ret
SYM_FUNC_END(xen_irq_disable_direct)

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


/*
 * In principle the caller should be passing us a value return from
 * xen_save_fl_direct, but for robustness sake we test only the
 * X86_EFLAGS_IF flag rather than the whole byte. After setting the
 * interrupt mask state, it checks for unmasked pending events and
 * enters the hypervisor to get them delivered if so.
 */
SYM_FUNC_START(xen_restore_fl_direct)
	FRAME_BEGIN
#ifdef CONFIG_X86_64
	testw $X86_EFLAGS_IF, %di
#else
	testb $X86_EFLAGS_IF>>8, %ah
#endif
	setz PER_CPU_VAR(xen_vcpu_info) + XEN_vcpu_info_mask
	/*
	 * Preempt here doesn't matter because that will deal with any
	 * pending interrupts.  The pending check may end up being run
	 * on the wrong CPU, but that doesn't hurt.
	 */

	/* check for unmasked and pending */
	cmpw $0x0001, PER_CPU_VAR(xen_vcpu_info) + XEN_vcpu_info_pending
	jnz 1f
	call check_events
1:
	FRAME_END
	ret
SYM_FUNC_END(xen_restore_fl_direct)


/*
 * Force an event check by making a hypercall, but preserve regs
 * before making the call.
 */
SYM_FUNC_START(check_events)
	FRAME_BEGIN
#ifdef CONFIG_X86_32
	push %eax
	push %ecx
	push %edx
	call xen_force_evtchn_callback
	pop %edx
	pop %ecx
	pop %eax
#else
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
#endif
	FRAME_END
	ret
SYM_FUNC_END(check_events)

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
