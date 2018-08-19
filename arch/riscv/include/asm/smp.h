/*
 * Copyright (C) 2012 Regents of the University of California
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 */

#ifndef _ASM_RISCV_SMP_H
#define _ASM_RISCV_SMP_H

/* This both needs asm-offsets.h and is used when generating it. */
#ifndef GENERATING_ASM_OFFSETS
#include <asm/asm-offsets.h>
#endif

#include <linux/cpumask.h>
#include <linux/irqreturn.h>

#ifdef CONFIG_SMP

/* SMP initialization hook for setup_arch */
void __init setup_smp(void);

/* Hook for the generic smp_call_function_many() routine. */
void arch_send_call_function_ipi_mask(struct cpumask *mask);

/* Hook for the generic smp_call_function_single() routine. */
void arch_send_call_function_single_ipi(int cpu);

/*
 * This is particularly ugly: it appears we can't actually get the definition
 * of task_struct here, but we need access to the CPU this task is running on.
 * Instead of using C we're using asm-offsets.h to get the current processor
 * ID.
 */
#define raw_smp_processor_id() (*((int*)((char*)get_current() + TASK_TI_CPU)))

#endif /* CONFIG_SMP */

#endif /* _ASM_RISCV_SMP_H */
