/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Based on arch/arm/include/asm/proc-fns.h
 *
 * Copyright (C) 1997-1999 Russell King
 * Copyright (C) 2000 Deep Blue Solutions Ltd
 * Copyright (C) 2012 ARM Ltd.
 */
#ifndef __ASM_PROCFNS_H
#define __ASM_PROCFNS_H

#ifdef __KERNEL__
#ifndef __ASSEMBLY__

#include <asm/page.h>

struct mm_struct;
struct cpu_suspend_ctx;

extern void cpu_do_idle(void);
extern void cpu_do_switch_mm(unsigned long pgd_phys, struct mm_struct *mm);
extern void cpu_do_suspend(struct cpu_suspend_ctx *ptr);
extern u64 cpu_do_resume(phys_addr_t ptr, u64 idmap_ttbr);

#include <asm/memory.h>

#endif /* __ASSEMBLY__ */
#endif /* __KERNEL__ */
#endif /* __ASM_PROCFNS_H */
