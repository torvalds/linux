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
#include <asm/asm.h>

#include <xen/interface/xen.h>

#include <linux/init.h>
#include <linux/linkage.h>

.macro xen_pv_trap name
SYM_CODE_START(xen_\name)
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
xen_pv_trap asm_exc_double_fault
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
xen_pv_trap asm_exc_machine_check
#endif /* CONFIG_X86_MCE */
xen_pv_trap asm_exc_simd_coprocessor_error
#ifdef CONFIG_IA32_EMULATION
xen_pv_trap entry_INT80_compat
#endif
xen_pv_trap asm_exc_xen_hypervisor_callback

	__INIT
SYM_CODE_START(xen_early_idt_handler_array)
	i = 0
	.rept NUM_EXCEPTION_VECTORS
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
	pushq $0
	jmp hypercall_iret
SYM_CODE_END(xen_iret)

SYM_CODE_START(xen_sysret64)
	/*
	 * We're already on the usermode stack at this point, but
	 * still with the kernel gs, so we can easily switch back.
	 *
	 * tss.sp2 is scratch space.
	 */
	movq %rsp, PER_CPU_VAR(cpu_tss_rw + TSS_sp2)
	movq PER_CPU_VAR(cpu_current_top_of_stack), %rsp

	pushq $__USER_DS
	pushq PER_CPU_VAR(cpu_tss_rw + TSS_sp2)
	pushq %r11
	pushq $__USER_CS
	pushq %rcx

	pushq $VGCF_in_syscall
	jmp hypercall_iret
SYM_CODE_END(xen_sysret64)

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
SYM_FUNC_START(xen_syscall_target)
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
SYM_FUNC_END(xen_syscall_target)

#ifdef CONFIG_IA32_EMULATION

/* 32-bit compat syscall target */
SYM_FUNC_START(xen_syscall32_target)
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
SYM_FUNC_END(xen_syscall32_target)

/* 32-bit compat sysenter target */
SYM_FUNC_START(xen_sysenter_target)
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
SYM_FUNC_END(xen_sysenter_target)

#else /* !CONFIG_IA32_EMULATION */

SYM_FUNC_START_ALIAS(xen_syscall32_target)
SYM_FUNC_START(xen_sysenter_target)
	lea 16(%rsp), %rsp	/* strip %rcx, %r11 */
	mov $-ENOSYS, %rax
	pushq $0
	jmp hypercall_iret
SYM_FUNC_END(xen_sysenter_target)
SYM_FUNC_END_ALIAS(xen_syscall32_target)

#endif	/* CONFIG_IA32_EMULATION */
