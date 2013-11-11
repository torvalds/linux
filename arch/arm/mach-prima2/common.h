/*
 * This file contains common function prototypes to avoid externs in the c files.
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */

#ifndef __MACH_PRIMA2_COMMON_H__
#define __MACH_PRIMA2_COMMON_H__

#include <linux/init.h>
#include <asm/mach/time.h>
#include <asm/exception.h>

#define SIRFSOC_VA_BASE		_AC(0xFEC00000, UL)
#define SIRFSOC_VA(x)		(SIRFSOC_VA_BASE + ((x) & 0x00FFF000))

extern struct smp_operations   sirfsoc_smp_ops;
extern void sirfsoc_secondary_startup(void);
extern void sirfsoc_cpu_die(unsigned int cpu);

extern void __init sirfsoc_of_irq_init(void);
extern void __init sirfsoc_of_clk_init(void);
extern void sirfsoc_restart(char, const char *);
extern asmlinkage void __exception_irq_entry sirfsoc_handle_irq(struct pt_regs *regs);

#ifndef CONFIG_DEBUG_LL
static inline void sirfsoc_map_lluart(void)  {}
#else
extern void __init sirfsoc_map_lluart(void);
#endif

#ifndef CONFIG_SMP
static inline void sirfsoc_map_scu(void) {}
#else
extern void sirfsoc_map_scu(void);
#endif

#ifdef CONFIG_SUSPEND
extern int sirfsoc_pm_init(void);
#else
static inline int sirfsoc_pm_init(void) { return 0; }
#endif

#endif
