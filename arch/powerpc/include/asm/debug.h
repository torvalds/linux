/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 1999 Cort Dougan <cort@cs.nmt.edu>
 */
#ifndef _ASM_POWERPC_DE_H
#define _ASM_POWERPC_DE_H

#include <asm/hw_breakpoint.h>

struct pt_regs;

#if defined(CONFIG_DEGER) || defined(CONFIG_KEXEC_CORE)

extern int (*__deger)(struct pt_regs *regs);
extern int (*__deger_ipi)(struct pt_regs *regs);
extern int (*__deger_bpt)(struct pt_regs *regs);
extern int (*__deger_sstep)(struct pt_regs *regs);
extern int (*__deger_iabr_match)(struct pt_regs *regs);
extern int (*__deger_break_match)(struct pt_regs *regs);
extern int (*__deger_fault_handler)(struct pt_regs *regs);

#define DEGER_BOILERPLATE(__NAME) \
static inline int __NAME(struct pt_regs *regs) \
{ \
	if (unlikely(__ ## __NAME)) \
		return __ ## __NAME(regs); \
	return 0; \
}

DEGER_BOILERPLATE(deger)
DEGER_BOILERPLATE(deger_ipi)
DEGER_BOILERPLATE(deger_bpt)
DEGER_BOILERPLATE(deger_sstep)
DEGER_BOILERPLATE(deger_iabr_match)
DEGER_BOILERPLATE(deger_break_match)
DEGER_BOILERPLATE(deger_fault_handler)

#else
static inline int deger(struct pt_regs *regs) { return 0; }
static inline int deger_ipi(struct pt_regs *regs) { return 0; }
static inline int deger_bpt(struct pt_regs *regs) { return 0; }
static inline int deger_sstep(struct pt_regs *regs) { return 0; }
static inline int deger_iabr_match(struct pt_regs *regs) { return 0; }
static inline int deger_break_match(struct pt_regs *regs) { return 0; }
static inline int deger_fault_handler(struct pt_regs *regs) { return 0; }
#endif

void __set_breakpoint(struct arch_hw_breakpoint *brk);
bool ppc_breakpoint_available(void);
#ifdef CONFIG_PPC_ADV_DE_REGS
extern void do_send_trap(struct pt_regs *regs, unsigned long address,
			 unsigned long error_code, int brkpt);
#else

extern void do_break(struct pt_regs *regs, unsigned long address,
		     unsigned long error_code);
#endif

#endif /* _ASM_POWERPC_DE_H */
