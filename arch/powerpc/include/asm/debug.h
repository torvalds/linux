/*
 * Copyright (C) 1999 Cort Dougan <cort@cs.nmt.edu>
 */
#ifndef _ASM_POWERPC_DEBUG_H
#define _ASM_POWERPC_DEBUG_H

#include <asm/hw_breakpoint.h>

struct pt_regs;

#if defined(CONFIG_DEBUGGER) || defined(CONFIG_KEXEC_CORE)

extern int (*__debugger)(struct pt_regs *regs);
extern int (*__debugger_ipi)(struct pt_regs *regs);
extern int (*__debugger_bpt)(struct pt_regs *regs);
extern int (*__debugger_sstep)(struct pt_regs *regs);
extern int (*__debugger_iabr_match)(struct pt_regs *regs);
extern int (*__debugger_break_match)(struct pt_regs *regs);
extern int (*__debugger_fault_handler)(struct pt_regs *regs);

#define DEBUGGER_BOILERPLATE(__NAME) \
static inline int __NAME(struct pt_regs *regs) \
{ \
	if (unlikely(__ ## __NAME)) \
		return __ ## __NAME(regs); \
	return 0; \
}

DEBUGGER_BOILERPLATE(debugger)
DEBUGGER_BOILERPLATE(debugger_ipi)
DEBUGGER_BOILERPLATE(debugger_bpt)
DEBUGGER_BOILERPLATE(debugger_sstep)
DEBUGGER_BOILERPLATE(debugger_iabr_match)
DEBUGGER_BOILERPLATE(debugger_break_match)
DEBUGGER_BOILERPLATE(debugger_fault_handler)

#else
static inline int debugger(struct pt_regs *regs) { return 0; }
static inline int debugger_ipi(struct pt_regs *regs) { return 0; }
static inline int debugger_bpt(struct pt_regs *regs) { return 0; }
static inline int debugger_sstep(struct pt_regs *regs) { return 0; }
static inline int debugger_iabr_match(struct pt_regs *regs) { return 0; }
static inline int debugger_break_match(struct pt_regs *regs) { return 0; }
static inline int debugger_fault_handler(struct pt_regs *regs) { return 0; }
#endif

void set_breakpoint(struct arch_hw_breakpoint *brk);
void __set_breakpoint(struct arch_hw_breakpoint *brk);
#ifdef CONFIG_PPC_ADV_DEBUG_REGS
extern void do_send_trap(struct pt_regs *regs, unsigned long address,
			 unsigned long error_code, int signal_code, int brkpt);
#else

extern void do_break(struct pt_regs *regs, unsigned long address,
		     unsigned long error_code);
#endif

#endif /* _ASM_POWERPC_DEBUG_H */
