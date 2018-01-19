/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Asm versions of Xen pv-ops, suitable for direct use.
 *
 * We only bother with direct forms (ie, vcpu in pda) of the
 * operations here; the indirect forms are better handled in C.
 */

#include <asm/errno.h>
#include <asm/percpu.h>
#include <asm/processor-flags.h>
#include <asm/segment.h>
#include <asm/asm-offsets.h>
#include <asm/thread_info.h>

#include <xen/interface/xen.h>

#include <linux/linkage.h>

.macro xen_pv_trap name
ENTRY(xen_\name)
	pop %rcx
	pop %r11
	jmp  \name
END(xen_\name)
.endm

xen_pv_trap divide_error
xen_pv_trap debug
xen_pv_trap xendebug
xen_pv_trap int3
xen_pv_trap xenint3
xen_pv_trap nmi
xen_pv_trap overflow
xen_pv_trap bounds
xen_pv_trap invalid_op
xen_pv_trap device_not_available
xen_pv_trap double_fault
xen_pv_trap coprocessor_segment_overrun
xen_pv_trap invalid_TSS
xen_pv_trap segment_not_present
xen_pv_trap stack_segment
xen_pv_trap general_protection
xen_pv_trap page_fault
xen_pv_trap spurious_interrupt_bug
xen_pv_trap coprocessor_error
xen_pv_trap alignment_check
#ifdef CONFIG_X86_MCE
xen_pv_trap machine_check
#endif /* CONFIG_X86_MCE */
xen_pv_trap simd_coprocessor_error
#ifdef CONFIG_IA32_EMULATION
xen_pv_trap entry_INT80_compat
#endif
xen_pv_trap hypervisor_callback

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
ENTRY(xen_iret)
	pushq $0
	jmp hypercall_iret

ENTRY(xen_sysret64)
	/*
	 * We're already on the usermode stack at this point, but
	 * still with the kernel gs, so we can easily switch back
	 */
	movq %rsp, PER_CPU_VAR(rsp_scratch)
	movq PER_CPU_VAR(cpu_current_top_of_stack), %rsp

	pushq $__USER_DS
	pushq PER_CPU_VAR(rsp_scratch)
	pushq %r11
	pushq $__USER_CS
	pushq %rcx

	pushq $VGCF_in_syscall
	jmp hypercall_iret

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
ENTRY(xen_syscall_target)
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
ENDPROC(xen_syscall_target)

#ifdef CONFIG_IA32_EMULATION

/* 32-bit compat syscall target */
ENTRY(xen_syscall32_target)
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
ENDPROC(xen_syscall32_target)

/* 32-bit compat sysenter target */
ENTRY(xen_sysenter_target)
	mov 0*8(%rsp), %rcx
	mov 1*8(%rsp), %r11
	mov 5*8(%rsp), %rsp
	jmp entry_SYSENTER_compat
ENDPROC(xen_sysenter_target)

#else /* !CONFIG_IA32_EMULATION */

ENTRY(xen_syscall32_target)
ENTRY(xen_sysenter_target)
	lea 16(%rsp), %rsp	/* strip %rcx, %r11 */
	mov $-ENOSYS, %rax
	pushq $0
	jmp hypercall_iret
ENDPROC(xen_syscall32_target)
ENDPROC(xen_sysenter_target)

#endif	/* CONFIG_IA32_EMULATION */
