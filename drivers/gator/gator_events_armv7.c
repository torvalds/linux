/**
 * Copyright (C) ARM Limited 2010-2015. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/*  Disabling interrupts
 *    Many of the functions below disable interrupts via local_irq_save(). This disabling of interrupts is done to prevent any race conditions
 *    between multiple entities (e.g. hrtimer interrupts and event based interrupts) calling the same functions. As accessing the pmu involves
 *    several steps (disable, select, read, enable), these steps must be performed atomically. Normal synchronization routines cannot be used
 *    as these functions are being called from interrupt context.
 */

#include "gator.h"

/* gator_events_perf_pmu.c is used if perf is supported */
#if GATOR_NO_PERF_SUPPORT

/* Per-CPU PMNC: config reg */
#define PMNC_E		(1 << 0)	/* Enable all counters */
#define PMNC_P		(1 << 1)	/* Reset all counters */
#define PMNC_C		(1 << 2)	/* Cycle counter reset */
#define	PMNC_MASK	0x3f	/* Mask for writable bits */

/* ccnt reg */
#define CCNT_REG	(1 << 31)

#define CCNT		0
#define CNT0		1
#define CNTMAX		(6+1)

static const char *pmnc_name;
static int pmnc_counters;

static unsigned long pmnc_enabled[CNTMAX];
static unsigned long pmnc_event[CNTMAX];
static unsigned long pmnc_key[CNTMAX];

static DEFINE_PER_CPU(int[CNTMAX * 2], perfCnt);

inline void armv7_pmnc_write(u32 val)
{
	val &= PMNC_MASK;
	asm volatile("mcr p15, 0, %0, c9, c12, 0" : : "r" (val));
}

inline u32 armv7_pmnc_read(void)
{
	u32 val;

	asm volatile("mrc p15, 0, %0, c9, c12, 0" : "=r" (val));
	return val;
}

inline u32 armv7_ccnt_read(u32 reset_value)
{
	unsigned long flags;
	u32 newval = -reset_value;
	u32 den = CCNT_REG;
	u32 val;

	local_irq_save(flags);
	asm volatile("mcr p15, 0, %0, c9, c12, 2" : : "r" (den));	/* disable */
	asm volatile("mrc p15, 0, %0, c9, c13, 0" : "=r" (val));	/* read */
	asm volatile("mcr p15, 0, %0, c9, c13, 0" : : "r" (newval));	/* new value */
	asm volatile("mcr p15, 0, %0, c9, c12, 1" : : "r" (den));	/* enable */
	local_irq_restore(flags);

	return val;
}

inline u32 armv7_cntn_read(unsigned int cnt, u32 reset_value)
{
	unsigned long flags;
	u32 newval = -reset_value;
	u32 sel = (cnt - CNT0);
	u32 den = 1 << sel;
	u32 oldval;

	local_irq_save(flags);
	asm volatile("mcr p15, 0, %0, c9, c12, 2" : : "r" (den));	/* disable */
	asm volatile("mcr p15, 0, %0, c9, c12, 5" : : "r" (sel));	/* select */
	asm volatile("mrc p15, 0, %0, c9, c13, 2" : "=r" (oldval));	/* read */
	asm volatile("mcr p15, 0, %0, c9, c13, 2" : : "r" (newval));	/* new value */
	asm volatile("mcr p15, 0, %0, c9, c12, 1" : : "r" (den));	/* enable */
	local_irq_restore(flags);

	return oldval;
}

static inline void armv7_pmnc_disable_interrupt(unsigned int cnt)
{
	u32 val = cnt ? (1 << (cnt - CNT0)) : (1 << 31);

	asm volatile("mcr p15, 0, %0, c9, c14, 2" : : "r" (val));
}

inline u32 armv7_pmnc_reset_interrupt(void)
{
	/* Get and reset overflow status flags */
	u32 flags;

	asm volatile("mrc p15, 0, %0, c9, c12, 3" : "=r" (flags));
	flags &= 0x8000003f;
	asm volatile("mcr p15, 0, %0, c9, c12, 3" : : "r" (flags));
	return flags;
}

static inline u32 armv7_pmnc_enable_counter(unsigned int cnt)
{
	u32 val = cnt ? (1 << (cnt - CNT0)) : CCNT_REG;

	asm volatile("mcr p15, 0, %0, c9, c12, 1" : : "r" (val));
	return cnt;
}

static inline u32 armv7_pmnc_disable_counter(unsigned int cnt)
{
	u32 val = cnt ? (1 << (cnt - CNT0)) : CCNT_REG;

	asm volatile("mcr p15, 0, %0, c9, c12, 2" : : "r" (val));
	return cnt;
}

static inline int armv7_pmnc_select_counter(unsigned int cnt)
{
	u32 val = (cnt - CNT0);

	asm volatile("mcr p15, 0, %0, c9, c12, 5" : : "r" (val));
	return cnt;
}

static inline void armv7_pmnc_write_evtsel(unsigned int cnt, u32 val)
{
	if (armv7_pmnc_select_counter(cnt) == cnt)
		asm volatile("mcr p15, 0, %0, c9, c13, 1" : : "r" (val));
}

static int gator_events_armv7_create_files(struct super_block *sb, struct dentry *root)
{
	struct dentry *dir;
	int i;

	for (i = 0; i < pmnc_counters; i++) {
		char buf[40];

		if (i == 0)
			snprintf(buf, sizeof(buf), "%s_ccnt", pmnc_name);
		else
			snprintf(buf, sizeof(buf), "%s_cnt%d", pmnc_name, i - 1);
		dir = gatorfs_mkdir(sb, root, buf);
		if (!dir)
			return -1;
		gatorfs_create_ulong(sb, dir, "enabled", &pmnc_enabled[i]);
		gatorfs_create_ro_ulong(sb, dir, "key", &pmnc_key[i]);
		if (i > 0)
			gatorfs_create_ulong(sb, dir, "event", &pmnc_event[i]);
	}

	return 0;
}

static int gator_events_armv7_online(int **buffer, bool migrate)
{
	unsigned int cnt, len = 0, cpu = smp_processor_id();

	if (armv7_pmnc_read() & PMNC_E)
		armv7_pmnc_write(armv7_pmnc_read() & ~PMNC_E);

	/* Initialize & Reset PMNC: C bit and P bit */
	armv7_pmnc_write(PMNC_P | PMNC_C);

	/* Reset overflow flags */
	armv7_pmnc_reset_interrupt();

	for (cnt = CCNT; cnt < CNTMAX; cnt++) {
		unsigned long event;

		if (!pmnc_enabled[cnt])
			continue;

		/* Disable counter */
		armv7_pmnc_disable_counter(cnt);

		event = pmnc_event[cnt] & 255;

		/* Set event (if destined for PMNx counters), we don't need to set the event if it's a cycle count */
		if (cnt != CCNT)
			armv7_pmnc_write_evtsel(cnt, event);

		armv7_pmnc_disable_interrupt(cnt);

		/* Reset counter */
		cnt ? armv7_cntn_read(cnt, 0) : armv7_ccnt_read(0);

		/* Enable counter */
		armv7_pmnc_enable_counter(cnt);
	}

	/* enable */
	armv7_pmnc_write(armv7_pmnc_read() | PMNC_E);

	/* return zero values, no need to read as the counters were just reset */
	for (cnt = 0; cnt < pmnc_counters; cnt++) {
		if (pmnc_enabled[cnt]) {
			per_cpu(perfCnt, cpu)[len++] = pmnc_key[cnt];
			per_cpu(perfCnt, cpu)[len++] = 0;
		}
	}

	if (buffer)
		*buffer = per_cpu(perfCnt, cpu);

	return len;
}

static int gator_events_armv7_offline(int **buffer, bool migrate)
{
	/* disable all counters, including PMCCNTR; overflow IRQs will not be signaled */
	armv7_pmnc_write(armv7_pmnc_read() & ~PMNC_E);

	return 0;
}

static void gator_events_armv7_stop(void)
{
	unsigned int cnt;

	for (cnt = CCNT; cnt < CNTMAX; cnt++) {
		pmnc_enabled[cnt] = 0;
		pmnc_event[cnt] = 0;
	}
}

static int gator_events_armv7_read(int **buffer, bool sched_switch)
{
	int cnt, len = 0;
	int cpu = smp_processor_id();

	/* a context switch may occur before the online hotplug event, thus need to check that the pmu is enabled */
	if (!(armv7_pmnc_read() & PMNC_E))
		return 0;

	for (cnt = 0; cnt < pmnc_counters; cnt++) {
		if (pmnc_enabled[cnt]) {
			int value;

			if (cnt == CCNT)
				value = armv7_ccnt_read(0);
			else
				value = armv7_cntn_read(cnt, 0);
			per_cpu(perfCnt, cpu)[len++] = pmnc_key[cnt];
			per_cpu(perfCnt, cpu)[len++] = value;
		}
	}

	if (buffer)
		*buffer = per_cpu(perfCnt, cpu);

	return len;
}

static struct gator_interface gator_events_armv7_interface = {
	.create_files = gator_events_armv7_create_files,
	.stop = gator_events_armv7_stop,
	.online = gator_events_armv7_online,
	.offline = gator_events_armv7_offline,
	.read = gator_events_armv7_read,
};

int gator_events_armv7_init(void)
{
	unsigned int cnt;

	switch (gator_cpuid()) {
	case CORTEX_A5:
		pmnc_name = "ARMv7_Cortex_A5";
		pmnc_counters = 2;
		break;
	case CORTEX_A7:
		pmnc_name = "ARMv7_Cortex_A7";
		pmnc_counters = 4;
		break;
	case CORTEX_A8:
		pmnc_name = "ARMv7_Cortex_A8";
		pmnc_counters = 4;
		break;
	case CORTEX_A9:
		pmnc_name = "ARMv7_Cortex_A9";
		pmnc_counters = 6;
		break;
	case CORTEX_A15:
		pmnc_name = "ARMv7_Cortex_A15";
		pmnc_counters = 6;
		break;
	/* ARM Cortex A17 is not supported by version of Linux before 3.0 */
	default:
		return -1;
	}

	pmnc_counters++;	/* CNT[n] + CCNT */

	for (cnt = CCNT; cnt < CNTMAX; cnt++) {
		pmnc_enabled[cnt] = 0;
		pmnc_event[cnt] = 0;
		pmnc_key[cnt] = gator_events_get_key();
	}

	return gator_events_install(&gator_events_armv7_interface);
}

#endif
