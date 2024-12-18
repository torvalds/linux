/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Cell Broadband Engine Performance Monitor
 *
 * (C) Copyright IBM Corporation 2006
 *
 * Author:
 *   David Erb (djerb@us.ibm.com)
 *   Kevin Corry (kevcorry@us.ibm.com)
 */

#ifndef __ASM_CELL_PMU_H__
#define __ASM_CELL_PMU_H__

/* The Cell PMU has four hardware performance counters, which can be
 * configured as four 32-bit counters or eight 16-bit counters.
 */
#define NR_PHYS_CTRS 4
#define NR_CTRS      (NR_PHYS_CTRS * 2)

/* Macros for the pm_control register. */
#define CBE_PM_16BIT_CTR(ctr)              (1 << (24 - ((ctr) & (NR_PHYS_CTRS - 1))))

/* Macros for the trace_address register. */
#define CBE_PM_TRACE_BUF_EMPTY             0x00000400

enum pm_reg_name {
	group_control,
	debug_bus_control,
	trace_address,
	ext_tr_timer,
	pm_status,
	pm_control,
	pm_interval,
	pm_start_stop,
};

#endif /* __ASM_CELL_PMU_H__ */
