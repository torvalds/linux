/*
 * Copyright (C) 2012 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __ASM_PERF_EVENT_H
#define __ASM_PERF_EVENT_H

#include <asm/stack_pointer.h>
#include <asm/ptrace.h>

#define	ARMV8_PMU_MAX_COUNTERS	32
#define	ARMV8_PMU_COUNTER_MASK	(ARMV8_PMU_MAX_COUNTERS - 1)

/*
 * Per-CPU PMCR: config reg
 */
#define ARMV8_PMU_PMCR_E	(1 << 0) /* Enable all counters */
#define ARMV8_PMU_PMCR_P	(1 << 1) /* Reset all counters */
#define ARMV8_PMU_PMCR_C	(1 << 2) /* Cycle counter reset */
#define ARMV8_PMU_PMCR_D	(1 << 3) /* CCNT counts every 64th cpu cycle */
#define ARMV8_PMU_PMCR_X	(1 << 4) /* Export to ETM */
#define ARMV8_PMU_PMCR_DP	(1 << 5) /* Disable CCNT if non-invasive debug*/
#define ARMV8_PMU_PMCR_LC	(1 << 6) /* Overflow on 64 bit cycle counter */
#define	ARMV8_PMU_PMCR_N_SHIFT	11	 /* Number of counters supported */
#define	ARMV8_PMU_PMCR_N_MASK	0x1f
#define	ARMV8_PMU_PMCR_MASK	0x7f	 /* Mask for writable bits */

/*
 * PMOVSR: counters overflow flag status reg
 */
#define	ARMV8_PMU_OVSR_MASK		0xffffffff	/* Mask for writable bits */
#define	ARMV8_PMU_OVERFLOWED_MASK	ARMV8_PMU_OVSR_MASK

/*
 * PMXEVTYPER: Event selection reg
 */
#define	ARMV8_PMU_EVTYPE_MASK	0xc800ffff	/* Mask for writable bits */
#define	ARMV8_PMU_EVTYPE_EVENT	0xffff		/* Mask for EVENT bits */

/*
 * PMUv3 event types: required events
 */
#define ARMV8_PMUV3_PERFCTR_SW_INCR				0x00
#define ARMV8_PMUV3_PERFCTR_L1D_CACHE_REFILL			0x03
#define ARMV8_PMUV3_PERFCTR_L1D_CACHE				0x04
#define ARMV8_PMUV3_PERFCTR_BR_MIS_PRED				0x10
#define ARMV8_PMUV3_PERFCTR_CPU_CYCLES				0x11
#define ARMV8_PMUV3_PERFCTR_BR_PRED				0x12

/*
 * Event filters for PMUv3
 */
#define	ARMV8_PMU_EXCLUDE_EL1	(1 << 31)
#define	ARMV8_PMU_EXCLUDE_EL0	(1 << 30)
#define	ARMV8_PMU_INCLUDE_EL2	(1 << 27)

/*
 * PMUSERENR: user enable reg
 */
#define ARMV8_PMU_USERENR_MASK	0xf		/* Mask for writable bits */
#define ARMV8_PMU_USERENR_EN	(1 << 0) /* PMU regs can be accessed at EL0 */
#define ARMV8_PMU_USERENR_SW	(1 << 1) /* PMSWINC can be written at EL0 */
#define ARMV8_PMU_USERENR_CR	(1 << 2) /* Cycle counter can be read at EL0 */
#define ARMV8_PMU_USERENR_ER	(1 << 3) /* Event counter can be read at EL0 */

#ifdef CONFIG_PERF_EVENTS
struct pt_regs;
extern unsigned long perf_instruction_pointer(struct pt_regs *regs);
extern unsigned long perf_misc_flags(struct pt_regs *regs);
#define perf_misc_flags(regs)	perf_misc_flags(regs)
#define perf_arch_bpf_user_pt_regs(regs) &regs->user_regs
#endif

#define perf_arch_fetch_caller_regs(regs, __ip) { \
	(regs)->pc = (__ip);    \
	(regs)->regs[29] = (unsigned long) __builtin_frame_address(0); \
	(regs)->sp = current_stack_pointer; \
	(regs)->pstate = PSR_MODE_EL1h;	\
}

#endif
