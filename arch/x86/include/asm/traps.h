/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_TRAPS_H
#define _ASM_X86_TRAPS_H

#include <linux/context_tracking_state.h>
#include <linux/kprobes.h>

#include <asm/debugreg.h>
#include <asm/idtentry.h>
#include <asm/siginfo.h>			/* TRAP_TRACE, ... */
#include <asm/trap_pf.h>

#ifdef CONFIG_X86_64
asmlinkage __visible notrace struct pt_regs *sync_regs(struct pt_regs *eregs);
asmlinkage __visible notrace
struct pt_regs *fixup_bad_iret(struct pt_regs *bad_regs);
asmlinkage __visible noinstr struct pt_regs *vc_switch_off_ist(struct pt_regs *eregs);
#endif

extern int ibt_selftest(void);
extern int ibt_selftest_noendbr(void);

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

bool fault_in_kernel_space(unsigned long address);

#ifdef CONFIG_VMAP_STACK
void __noreturn handle_stack_overflow(struct pt_regs *regs,
				      unsigned long fault_address,
				      struct stack_info *info);
#endif

static inline void cond_local_irq_enable(struct pt_regs *regs)
{
	if (regs->flags & X86_EFLAGS_IF)
		local_irq_enable();
}

static inline void cond_local_irq_disable(struct pt_regs *regs)
{
	if (regs->flags & X86_EFLAGS_IF)
		local_irq_disable();
}

#endif /* _ASM_X86_TRAPS_H */
