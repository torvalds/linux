/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * This file contains common function prototypes to avoid externs in the c files.
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 */

#ifndef __MACH_PRIMA2_COMMON_H__
#define __MACH_PRIMA2_COMMON_H__

#include <linux/init.h>
#include <linux/reboot.h>

#include <asm/mach/time.h>
#include <asm/exception.h>

extern volatile int prima2_pen_release;

extern const struct smp_operations sirfsoc_smp_ops;
extern void sirfsoc_secondary_startup(void);
extern void sirfsoc_cpu_die(unsigned int cpu);

extern void __init sirfsoc_of_irq_init(void);
extern asmlinkage void __exception_irq_entry sirfsoc_handle_irq(struct pt_regs *regs);

#ifdef CONFIG_SUSPEND
extern int sirfsoc_pm_init(void);
#else
static inline int sirfsoc_pm_init(void) { return 0; }
#endif

#endif
