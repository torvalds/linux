/* SPDX-License-Identifier: GPL-2.0 */
/*
 * The FRED specific kernel/user entry functions which are invoked from
 * assembly code and dispatch to the associated handlers.
 */
#include <linux/kernel.h>
#include <linux/kdebug.h>
#include <linux/nospec.h>

#include <asm/desc.h>
#include <asm/fred.h>
#include <asm/idtentry.h>
#include <asm/syscall.h>
#include <asm/trapnr.h>
#include <asm/traps.h>

/* FRED EVENT_TYPE_OTHER vector numbers */
#define FRED_SYSCALL			1
#define FRED_SYSENTER			2

static noinstr void fred_bad_type(struct pt_regs *regs, unsigned long error_code)
{
	irqentry_state_t irq_state = irqentry_nmi_enter(regs);

	instrumentation_begin();

	/* Panic on events from a high stack level */
	if (regs->fred_cs.sl > 0) {
		pr_emerg("PANIC: invalid or fatal FRED event; event type %u "
			 "vector %u error 0x%lx aux 0x%lx at %04x:%016lx\n",
			 regs->fred_ss.type, regs->fred_ss.vector, error_code,
			 fred_event_data(regs), regs->cs, regs->ip);
		die("invalid or fatal FRED event", regs, error_code);
		panic("invalid or fatal FRED event");
	} else {
		unsigned long flags = oops_begin();
		int sig = SIGKILL;

		pr_alert("BUG: invalid or fatal FRED event; event type %u "
			 "vector %u error 0x%lx aux 0x%lx at %04x:%016lx\n",
			 regs->fred_ss.type, regs->fred_ss.vector, error_code,
			 fred_event_data(regs), regs->cs, regs->ip);

		if (__die("Invalid or fatal FRED event", regs, error_code))
			sig = 0;

		oops_end(flags, regs, sig);
	}

	instrumentation_end();
	irqentry_nmi_exit(regs, irq_state);
}

static noinstr void fred_intx(struct pt_regs *regs)
{
	switch (regs->fred_ss.vector) {
	/* Opcode 0xcd, 0x3, NOT INT3 (opcode 0xcc) */
	case X86_TRAP_BP:
		return exc_int3(regs);

	/* Opcode 0xcd, 0x4, NOT INTO (opcode 0xce) */
	case X86_TRAP_OF:
		return exc_overflow(regs);

#ifdef CONFIG_IA32_EMULATION
	/* INT80 */
	case IA32_SYSCALL_VECTOR:
		if (ia32_enabled())
			return fred_int80_emulation(regs);
		fallthrough;
#endif

	default:
		return exc_general_protection(regs, 0);
	}
}

static __always_inline void fred_other(struct pt_regs *regs)
{
	/* The compiler can fold these conditions into a single test */
	if (likely(regs->fred_ss.vector == FRED_SYSCALL && regs->fred_ss.lm)) {
		regs->orig_ax = regs->ax;
		regs->ax = -ENOSYS;
		do_syscall_64(regs, regs->orig_ax);
		return;
	} else if (ia32_enabled() &&
		   likely(regs->fred_ss.vector == FRED_SYSENTER && !regs->fred_ss.lm)) {
		regs->orig_ax = regs->ax;
		regs->ax = -ENOSYS;
		do_fast_syscall_32(regs);
		return;
	} else {
		exc_invalid_op(regs);
		return;
	}
}

#define SYSVEC(_vector, _function) [_vector - FIRST_SYSTEM_VECTOR] = fred_sysvec_##_function

static idtentry_t sysvec_table[NR_SYSTEM_VECTORS] __ro_after_init = {
	SYSVEC(ERROR_APIC_VECTOR,		error_interrupt),
	SYSVEC(SPURIOUS_APIC_VECTOR,		spurious_apic_interrupt),
	SYSVEC(LOCAL_TIMER_VECTOR,		apic_timer_interrupt),
	SYSVEC(X86_PLATFORM_IPI_VECTOR,		x86_platform_ipi),

	SYSVEC(RESCHEDULE_VECTOR,		reschedule_ipi),
	SYSVEC(CALL_FUNCTION_SINGLE_VECTOR,	call_function_single),
	SYSVEC(CALL_FUNCTION_VECTOR,		call_function),
	SYSVEC(REBOOT_VECTOR,			reboot),

	SYSVEC(THRESHOLD_APIC_VECTOR,		threshold),
	SYSVEC(DEFERRED_ERROR_VECTOR,		deferred_error),
	SYSVEC(THERMAL_APIC_VECTOR,		thermal),

	SYSVEC(IRQ_WORK_VECTOR,			irq_work),

	SYSVEC(POSTED_INTR_VECTOR,		kvm_posted_intr_ipi),
	SYSVEC(POSTED_INTR_WAKEUP_VECTOR,	kvm_posted_intr_wakeup_ipi),
	SYSVEC(POSTED_INTR_NESTED_VECTOR,	kvm_posted_intr_nested_ipi),
};

static bool fred_setup_done __initdata;

void __init fred_install_sysvec(unsigned int sysvec, idtentry_t handler)
{
	if (WARN_ON_ONCE(sysvec < FIRST_SYSTEM_VECTOR))
		return;

	if (WARN_ON_ONCE(fred_setup_done))
		return;

	if (!WARN_ON_ONCE(sysvec_table[sysvec - FIRST_SYSTEM_VECTOR]))
		 sysvec_table[sysvec - FIRST_SYSTEM_VECTOR] = handler;
}

static noinstr void fred_handle_spurious_interrupt(struct pt_regs *regs)
{
	spurious_interrupt(regs, regs->fred_ss.vector);
}

void __init fred_complete_exception_setup(void)
{
	unsigned int vector;

	for (vector = 0; vector < FIRST_EXTERNAL_VECTOR; vector++)
		set_bit(vector, system_vectors);

	for (vector = 0; vector < NR_SYSTEM_VECTORS; vector++) {
		if (sysvec_table[vector])
			set_bit(vector + FIRST_SYSTEM_VECTOR, system_vectors);
		else
			sysvec_table[vector] = fred_handle_spurious_interrupt;
	}
	fred_setup_done = true;
}

static noinstr void fred_extint(struct pt_regs *regs)
{
	unsigned int vector = regs->fred_ss.vector;
	unsigned int index = array_index_nospec(vector - FIRST_SYSTEM_VECTOR,
						NR_SYSTEM_VECTORS);

	if (WARN_ON_ONCE(vector < FIRST_EXTERNAL_VECTOR))
		return;

	if (likely(vector >= FIRST_SYSTEM_VECTOR)) {
		irqentry_state_t state = irqentry_enter(regs);

		instrumentation_begin();
		sysvec_table[index](regs);
		instrumentation_end();
		irqentry_exit(regs, state);
	} else {
		common_interrupt(regs, vector);
	}
}

static noinstr void fred_hwexc(struct pt_regs *regs, unsigned long error_code)
{
	/* Optimize for #PF. That's the only exception which matters performance wise */
	if (likely(regs->fred_ss.vector == X86_TRAP_PF))
		return exc_page_fault(regs, error_code);

	switch (regs->fred_ss.vector) {
	case X86_TRAP_DE: return exc_divide_error(regs);
	case X86_TRAP_DB: return fred_exc_debug(regs);
	case X86_TRAP_BR: return exc_bounds(regs);
	case X86_TRAP_UD: return exc_invalid_op(regs);
	case X86_TRAP_NM: return exc_device_not_available(regs);
	case X86_TRAP_DF: return exc_double_fault(regs, error_code);
	case X86_TRAP_TS: return exc_invalid_tss(regs, error_code);
	case X86_TRAP_NP: return exc_segment_not_present(regs, error_code);
	case X86_TRAP_SS: return exc_stack_segment(regs, error_code);
	case X86_TRAP_GP: return exc_general_protection(regs, error_code);
	case X86_TRAP_MF: return exc_coprocessor_error(regs);
	case X86_TRAP_AC: return exc_alignment_check(regs, error_code);
	case X86_TRAP_XF: return exc_simd_coprocessor_error(regs);

#ifdef CONFIG_X86_MCE
	case X86_TRAP_MC: return fred_exc_machine_check(regs);
#endif
#ifdef CONFIG_INTEL_TDX_GUEST
	case X86_TRAP_VE: return exc_virtualization_exception(regs);
#endif
#ifdef CONFIG_X86_CET
	case X86_TRAP_CP: return exc_control_protection(regs, error_code);
#endif
	default: return fred_bad_type(regs, error_code);
	}

}

static noinstr void fred_swexc(struct pt_regs *regs, unsigned long error_code)
{
	switch (regs->fred_ss.vector) {
	case X86_TRAP_BP: return exc_int3(regs);
	case X86_TRAP_OF: return exc_overflow(regs);
	default: return fred_bad_type(regs, error_code);
	}
}

__visible noinstr void fred_entry_from_user(struct pt_regs *regs)
{
	unsigned long error_code = regs->orig_ax;

	/* Invalidate orig_ax so that syscall_get_nr() works correctly */
	regs->orig_ax = -1;

	switch (regs->fred_ss.type) {
	case EVENT_TYPE_EXTINT:
		return fred_extint(regs);
	case EVENT_TYPE_NMI:
		if (likely(regs->fred_ss.vector == X86_TRAP_NMI))
			return fred_exc_nmi(regs);
		break;
	case EVENT_TYPE_HWEXC:
		return fred_hwexc(regs, error_code);
	case EVENT_TYPE_SWINT:
		return fred_intx(regs);
	case EVENT_TYPE_PRIV_SWEXC:
		if (likely(regs->fred_ss.vector == X86_TRAP_DB))
			return fred_exc_debug(regs);
		break;
	case EVENT_TYPE_SWEXC:
		return fred_swexc(regs, error_code);
	case EVENT_TYPE_OTHER:
		return fred_other(regs);
	default: break;
	}

	return fred_bad_type(regs, error_code);
}

__visible noinstr void fred_entry_from_kernel(struct pt_regs *regs)
{
	unsigned long error_code = regs->orig_ax;

	/* Invalidate orig_ax so that syscall_get_nr() works correctly */
	regs->orig_ax = -1;

	switch (regs->fred_ss.type) {
	case EVENT_TYPE_EXTINT:
		return fred_extint(regs);
	case EVENT_TYPE_NMI:
		if (likely(regs->fred_ss.vector == X86_TRAP_NMI))
			return fred_exc_nmi(regs);
		break;
	case EVENT_TYPE_HWEXC:
		return fred_hwexc(regs, error_code);
	case EVENT_TYPE_PRIV_SWEXC:
		if (likely(regs->fred_ss.vector == X86_TRAP_DB))
			return fred_exc_debug(regs);
		break;
	case EVENT_TYPE_SWEXC:
		return fred_swexc(regs, error_code);
	default: break;
	}

	return fred_bad_type(regs, error_code);
}

#if IS_ENABLED(CONFIG_KVM_INTEL)
__visible noinstr void __fred_entry_from_kvm(struct pt_regs *regs)
{
	switch (regs->fred_ss.type) {
	case EVENT_TYPE_EXTINT:
		return fred_extint(regs);
	case EVENT_TYPE_NMI:
		return fred_exc_nmi(regs);
	default:
		WARN_ON_ONCE(1);
	}
}
#endif
