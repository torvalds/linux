/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_TRAPS_H
#define _ASM_X86_TRAPS_H

#include <linux/context_tracking_state.h>
#include <linux/kprobes.h>

#include <asm/debugreg.h>
#include <asm/idtentry.h>
#include <asm/siginfo.h>			/* TRAP_TRACE, ... */

#define dotraplinkage __visible

asmlinkage void debug(void);
asmlinkage void nmi(void);
asmlinkage void int3(void);
#ifdef CONFIG_X86_64
asmlinkage void double_fault(void);
#endif
asmlinkage void segment_not_present(void);
asmlinkage void stack_segment(void);
asmlinkage void general_protection(void);
asmlinkage void page_fault(void);
asmlinkage void async_page_fault(void);
asmlinkage void spurious_interrupt_bug(void);
asmlinkage void coprocessor_error(void);
asmlinkage void alignment_check(void);
#ifdef CONFIG_X86_MCE
asmlinkage void machine_check(void);
#endif /* CONFIG_X86_MCE */
asmlinkage void simd_coprocessor_error(void);

#if defined(CONFIG_X86_64) && defined(CONFIG_XEN_PV)
asmlinkage void xen_xennmi(void);
asmlinkage void xen_xendebug(void);
asmlinkage void xen_int3(void);
asmlinkage void xen_double_fault(void);
asmlinkage void xen_segment_not_present(void);
asmlinkage void xen_stack_segment(void);
asmlinkage void xen_general_protection(void);
asmlinkage void xen_page_fault(void);
asmlinkage void xen_spurious_interrupt_bug(void);
asmlinkage void xen_coprocessor_error(void);
asmlinkage void xen_alignment_check(void);
#ifdef CONFIG_X86_MCE
asmlinkage void xen_machine_check(void);
#endif /* CONFIG_X86_MCE */
asmlinkage void xen_simd_coprocessor_error(void);
#endif

dotraplinkage void do_debug(struct pt_regs *regs, long error_code);
dotraplinkage void do_nmi(struct pt_regs *regs, long error_code);
dotraplinkage void do_int3(struct pt_regs *regs, long error_code);
dotraplinkage void do_double_fault(struct pt_regs *regs, long error_code, unsigned long cr2);
dotraplinkage void do_segment_not_present(struct pt_regs *regs, long error_code);
dotraplinkage void do_stack_segment(struct pt_regs *regs, long error_code);
dotraplinkage void do_general_protection(struct pt_regs *regs, long error_code);
dotraplinkage void do_page_fault(struct pt_regs *regs, unsigned long error_code, unsigned long address);
dotraplinkage void do_spurious_interrupt_bug(struct pt_regs *regs, long error_code);
dotraplinkage void do_coprocessor_error(struct pt_regs *regs, long error_code);
dotraplinkage void do_alignment_check(struct pt_regs *regs, long error_code);
dotraplinkage void do_simd_coprocessor_error(struct pt_regs *regs, long error_code);
#ifdef CONFIG_X86_32
dotraplinkage void do_iret_error(struct pt_regs *regs, long error_code);
#endif
dotraplinkage void do_mce(struct pt_regs *regs, long error_code);

#ifdef CONFIG_X86_64
asmlinkage __visible notrace struct pt_regs *sync_regs(struct pt_regs *eregs);
asmlinkage __visible notrace
struct bad_iret_stack *fixup_bad_iret(struct bad_iret_stack *s);
void __init trap_init(void);
#endif

#ifdef CONFIG_X86_F00F_BUG
/* For handling the FOOF bug */
void handle_invalid_op(struct pt_regs *regs);
#endif

static inline int get_si_code(unsigned long condition)
{
	if (condition & DR_STEP)
		return TRAP_TRACE;
	else if (condition & (DR_TRAP0|DR_TRAP1|DR_TRAP2|DR_TRAP3))
		return TRAP_HWBKPT;
	else
		return TRAP_BRKPT;
}

extern int panic_on_unrecovered_nmi;

void math_emulate(struct math_emu_info *);
#ifndef CONFIG_X86_32
asmlinkage void smp_thermal_interrupt(struct pt_regs *regs);
asmlinkage void smp_threshold_interrupt(struct pt_regs *regs);
asmlinkage void smp_deferred_error_interrupt(struct pt_regs *regs);
#endif

void smp_apic_timer_interrupt(struct pt_regs *regs);
void smp_spurious_interrupt(struct pt_regs *regs);
void smp_error_interrupt(struct pt_regs *regs);
asmlinkage void smp_irq_move_cleanup_interrupt(void);

#ifdef CONFIG_VMAP_STACK
void __noreturn handle_stack_overflow(const char *message,
				      struct pt_regs *regs,
				      unsigned long fault_address);
#endif

/*
 * Page fault error code bits:
 *
 *   bit 0 ==	 0: no page found	1: protection fault
 *   bit 1 ==	 0: read access		1: write access
 *   bit 2 ==	 0: kernel-mode access	1: user-mode access
 *   bit 3 ==				1: use of reserved bit detected
 *   bit 4 ==				1: fault was an instruction fetch
 *   bit 5 ==				1: protection keys block access
 */
enum x86_pf_error_code {
	X86_PF_PROT	=		1 << 0,
	X86_PF_WRITE	=		1 << 1,
	X86_PF_USER	=		1 << 2,
	X86_PF_RSVD	=		1 << 3,
	X86_PF_INSTR	=		1 << 4,
	X86_PF_PK	=		1 << 5,
};
#endif /* _ASM_X86_TRAPS_H */
