// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2017 Arm Ltd.
#ifndef __ASM_SDEI_H
#define __ASM_SDEI_H

/* Values for sdei_exit_mode */
#define SDEI_EXIT_HVC  0
#define SDEI_EXIT_SMC  1

#define SDEI_STACK_SIZE		IRQ_STACK_SIZE

#ifndef __ASSEMBLY__

#include <linux/linkage.h>
#include <linux/preempt.h>
#include <linux/types.h>

#include <asm/virt.h>

extern unsigned long sdei_exit_mode;

/* Software Delegated Exception entry point from firmware*/
asmlinkage void __sdei_asm_handler(unsigned long event_num, unsigned long arg,
				   unsigned long pc, unsigned long pstate);

/* and its CONFIG_UNMAP_KERNEL_AT_EL0 trampoline */
asmlinkage void __sdei_asm_entry_trampoline(unsigned long event_num,
						   unsigned long arg,
						   unsigned long pc,
						   unsigned long pstate);

/*
 * The above entry point does the minimum to call C code. This function does
 * anything else, before calling the driver.
 */
struct sdei_registered_event;
asmlinkage unsigned long __sdei_handler(struct pt_regs *regs,
					struct sdei_registered_event *arg);

unsigned long sdei_arch_get_entry_point(int conduit);
#define sdei_arch_get_entry_point(x)	sdei_arch_get_entry_point(x)

bool _on_sdei_stack(unsigned long sp);
static inline bool on_sdei_stack(unsigned long sp)
{
	if (!IS_ENABLED(CONFIG_VMAP_STACK))
		return false;
	if (!IS_ENABLED(CONFIG_ARM_SDE_INTERFACE))
		return false;
	if (in_nmi())
		return _on_sdei_stack(sp);

	return false;
}

#endif /* __ASSEMBLY__ */
#endif	/* __ASM_SDEI_H */
