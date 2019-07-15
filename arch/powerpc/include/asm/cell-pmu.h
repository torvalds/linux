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
#define CBE_PM_ENABLE_PERF_MON             0x80000000
#define CBE_PM_STOP_AT_MAX                 0x40000000
#define CBE_PM_TRACE_MODE_GET(pm_control)  (((pm_control) >> 28) & 0x3)
#define CBE_PM_TRACE_MODE_SET(mode)        (((mode)  & 0x3) << 28)
#define CBE_PM_TRACE_BUF_OVFLW(bit)        (((bit) & 0x1) << 17)
#define CBE_PM_COUNT_MODE_SET(count)       (((count) & 0x3) << 18)
#define CBE_PM_FREEZE_ALL_CTRS             0x00100000
#define CBE_PM_ENABLE_EXT_TRACE            0x00008000
#define CBE_PM_SPU_ADDR_TRACE_SET(msk)     (((msk) & 0x3) << 9)

/* Macros for the trace_address register. */
#define CBE_PM_TRACE_BUF_FULL              0x00000800
#define CBE_PM_TRACE_BUF_EMPTY             0x00000400
#define CBE_PM_TRACE_BUF_DATA_COUNT(ta)    ((ta) & 0x3ff)
#define CBE_PM_TRACE_BUF_MAX_COUNT         0x400

/* Macros for the pm07_control registers. */
#define CBE_PM_CTR_INPUT_MUX(pm07_control) (((pm07_control) >> 26) & 0x3f)
#define CBE_PM_CTR_INPUT_CONTROL           0x02000000
#define CBE_PM_CTR_POLARITY                0x01000000
#define CBE_PM_CTR_COUNT_CYCLES            0x00800000
#define CBE_PM_CTR_ENABLE                  0x00400000
#define PM07_CTR_INPUT_MUX(x)              (((x) & 0x3F) << 26)
#define PM07_CTR_INPUT_CONTROL(x)          (((x) & 1) << 25)
#define PM07_CTR_POLARITY(x)               (((x) & 1) << 24)
#define PM07_CTR_COUNT_CYCLES(x)           (((x) & 1) << 23)
#define PM07_CTR_ENABLE(x)                 (((x) & 1) << 22)

/* Macros for the pm_status register. */
#define CBE_PM_CTR_OVERFLOW_INTR(ctr)      (1 << (31 - ((ctr) & 7)))

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

/* Routines for reading/writing the PMU registers. */
extern u32  cbe_read_phys_ctr(u32 cpu, u32 phys_ctr);
extern void cbe_write_phys_ctr(u32 cpu, u32 phys_ctr, u32 val);
extern u32  cbe_read_ctr(u32 cpu, u32 ctr);
extern void cbe_write_ctr(u32 cpu, u32 ctr, u32 val);

extern u32  cbe_read_pm07_control(u32 cpu, u32 ctr);
extern void cbe_write_pm07_control(u32 cpu, u32 ctr, u32 val);
extern u32  cbe_read_pm(u32 cpu, enum pm_reg_name reg);
extern void cbe_write_pm(u32 cpu, enum pm_reg_name reg, u32 val);

extern u32  cbe_get_ctr_size(u32 cpu, u32 phys_ctr);
extern void cbe_set_ctr_size(u32 cpu, u32 phys_ctr, u32 ctr_size);

extern void cbe_enable_pm(u32 cpu);
extern void cbe_disable_pm(u32 cpu);

extern void cbe_read_trace_buffer(u32 cpu, u64 *buf);

extern void cbe_enable_pm_interrupts(u32 cpu, u32 thread, u32 mask);
extern void cbe_disable_pm_interrupts(u32 cpu);
extern u32  cbe_get_and_clear_pm_interrupts(u32 cpu);
extern void cbe_sync_irq(int node);

#define CBE_COUNT_SUPERVISOR_MODE       0
#define CBE_COUNT_HYPERVISOR_MODE       1
#define CBE_COUNT_PROBLEM_MODE          2
#define CBE_COUNT_ALL_MODES             3

#endif /* __ASM_CELL_PMU_H__ */
