/*
 * Copyright 2014 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 */

#ifndef _ASM_TILE_PMC_H
#define _ASM_TILE_PMC_H

#include <linux/ptrace.h>

#define TILE_BASE_COUNTERS	2

/* Bitfields below are derived from SPR PERF_COUNT_CTL*/
#ifndef __tilegx__
/* PERF_COUNT_CTL on TILEPro */
#define TILE_CTL_EXCL_USER	(1 << 7) /* exclude user level */
#define TILE_CTL_EXCL_KERNEL	(1 << 8) /* exclude kernel level */
#define TILE_CTL_EXCL_HV	(1 << 9) /* exclude hypervisor level */

#define TILE_SEL_MASK		0x7f	/* 7 bits for event SEL,
					COUNT_0_SEL */
#define TILE_PLM_MASK		0x780	/* 4 bits priv level msks,
					COUNT_0_MASK*/
#define TILE_EVENT_MASK	(TILE_SEL_MASK | TILE_PLM_MASK)

#else /* __tilegx__*/
/* PERF_COUNT_CTL on TILEGx*/
#define TILE_CTL_EXCL_USER	(1 << 10) /* exclude user level */
#define TILE_CTL_EXCL_KERNEL	(1 << 11) /* exclude kernel level */
#define TILE_CTL_EXCL_HV	(1 << 12) /* exclude hypervisor level */

#define TILE_SEL_MASK		0x3f	/* 6 bits for event SEL,
					COUNT_0_SEL*/
#define TILE_BOX_MASK		0x1c0	/* 3 bits box msks,
					COUNT_0_BOX */
#define TILE_PLM_MASK		0x3c00	/* 4 bits priv level msks,
					COUNT_0_MASK */
#define TILE_EVENT_MASK	(TILE_SEL_MASK | TILE_BOX_MASK | TILE_PLM_MASK)
#endif /* __tilegx__*/

/* Takes register and fault number.  Returns error to disable the interrupt. */
typedef int (*perf_irq_t)(struct pt_regs *, int);

int userspace_perf_handler(struct pt_regs *regs, int fault);

perf_irq_t reserve_pmc_hardware(perf_irq_t new_perf_irq);
void release_pmc_hardware(void);

unsigned long pmc_get_overflow(void);
void pmc_ack_overflow(unsigned long status);

void unmask_pmc_interrupts(void);
void mask_pmc_interrupts(void);

#endif /* _ASM_TILE_PMC_H */
