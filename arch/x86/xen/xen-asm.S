/*
 * Asm versions of Xen pv-ops, suitable for either direct use or
 * inlining.  The inline versions are the same as the direct-use
 * versions, with the pre- and post-amble chopped off.
 *
 * This code is encoded for size rather than absolute efficiency, with
 * a view to being able to inline as much as possible.
 *
 * We only bother with direct forms (ie, vcpu in percpu data) of the
 * operations here; the indirect forms are better handled in C, since
 * they're generally too large to inline anyway.
 */

#include <asm/asm-offsets.h>
#include <asm/percpu.h>
#include <asm/processor-flags.h>

#include "xen-asm.h"

/*
 * Enable events.  This clears the event mask and tests the pending
 * event status with one and operation.  If there are pending events,
 * then enter the hypervisor to get them handled.
 */
ENTRY(xen_irq_enable_direct)
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

2:	call check_events
1:
ENDPATCH(xen_irq_enable_direct)
	ret
	ENDPROC(xen_irq_enable_direct)
	RELOC(xen_irq_enable_direct, 2b+1)


/*
 * Disabling events is simply a matter of making the event mask
 * non-zero.
 */
ENTRY(xen_irq_disable_direct)
	movb $1, PER_CPU_VAR(xen_vcpu_info) + XEN_vcpu_info_mask
ENDPATCH(xen_irq_disable_direct)
	ret
	ENDPROC(xen_irq_disable_direct)
	RELOC(xen_irq_disable_direct, 0)

/*
 * (xen_)save_fl is used to get the current interrupt enable status.
 * Callers expect the status to be in X86_EFLAGS_IF, and other bits
 * may be set in the return value.  We take advantage of this by
 * making sure that X86_EFLAGS_IF has the right value (and other bits
 * in that byte are 0), but other bits in the return value are
 * undefined.  We need to toggle the state of the bit, because Xen and
 * x86 use opposite senses (mask vs enable).
 */
ENTRY(xen_save_fl_direct)
	testb $0xff, PER_CPU_VAR(xen_vcpu_info) + XEN_vcpu_info_mask
	setz %ah
	addb %ah, %ah
ENDPATCH(xen_save_fl_direct)
	ret
	ENDPROC(xen_save_fl_direct)
	RELOC(xen_save_fl_direct, 0)


/*
 * In principle the caller should be passing us a value return from
 * xen_save_fl_direct, but for robustness sake we test only the
 * X86_EFLAGS_IF flag rather than the whole byte. After setting the
 * interrupt mask state, it checks for unmasked pending events and
 * enters the hypervisor to get them delivered if so.
 */
ENTRY(xen_restore_fl_direct)
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
2:	call check_events
1:
ENDPATCH(xen_restore_fl_direct)
	ret
	ENDPROC(xen_restore_fl_direct)
	RELOC(xen_restore_fl_direct, 2b+1)


/*
 * Force an event check by making a hypercall, but preserve regs
 * before making the call.
 */
check_events:
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
	ret
