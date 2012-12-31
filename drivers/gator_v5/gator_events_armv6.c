/**
 * Copyright (C) ARM Limited 2010-2012. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "gator.h"

// gator_events_perf_pmu.c is used if perf is supported
#if GATOR_NO_PERF_SUPPORT

static const char *pmnc_name;

/*
 * Per-CPU PMCR
 */
#define PMCR_E			(1 << 0)	/* Enable */
#define PMCR_P			(1 << 1)	/* Count reset */
#define PMCR_C			(1 << 2)	/* Cycle counter reset */
#define PMCR_OFL_PMN0	(1 << 8)	/* Count reg 0 overflow */
#define PMCR_OFL_PMN1	(1 << 9)	/* Count reg 1 overflow */
#define PMCR_OFL_CCNT	(1 << 10)	/* Cycle counter overflow */

#define PMN0 0
#define PMN1 1
#define CCNT 2
#define CNTMAX	(CCNT+1)

static int pmnc_counters = 0;
static unsigned long pmnc_enabled[CNTMAX];
static unsigned long pmnc_event[CNTMAX];
static unsigned long pmnc_key[CNTMAX];

static DEFINE_PER_CPU(int[CNTMAX * 2], perfCnt);

static inline void armv6_pmnc_write(u32 val)
{
	/* upper 4bits and 7, 11 are write-as-0 */
	val &= 0x0ffff77f;
	asm volatile("mcr p15, 0, %0, c15, c12, 0" : : "r" (val));
}

static inline u32 armv6_pmnc_read(void)
{
	u32 val;
	asm volatile("mrc p15, 0, %0, c15, c12, 0" : "=r" (val));
	return val;
}

static void armv6_pmnc_reset_counter(unsigned int cnt)
{
	u32 val = 0;
	switch (cnt) {
	case CCNT:
		asm volatile("mcr p15, 0, %0, c15, c12, 1" : : "r" (val));
		break;
	case PMN0:
		asm volatile("mcr p15, 0, %0, c15, c12, 2" : : "r" (val));
		break;
	case PMN1:
		asm volatile("mcr p15, 0, %0, c15, c12, 3" : : "r" (val));
		break;
	}
}

int gator_events_armv6_create_files(struct super_block *sb, struct dentry *root)
{
	struct dentry *dir;
	int i;

	pmnc_counters = 3;

	for (i = PMN0; i <= CCNT; i++) {
		char buf[40];
		if (i == CCNT) {
			snprintf(buf, sizeof buf, "ARM_%s_ccnt", pmnc_name);
		} else {
			snprintf(buf, sizeof buf, "ARM_%s_cnt%d", pmnc_name, i);
		}
		dir = gatorfs_mkdir(sb, root, buf);
		if (!dir) {
			return -1;
		}
		gatorfs_create_ulong(sb, dir, "enabled", &pmnc_enabled[i]);
		gatorfs_create_ro_ulong(sb, dir, "key", &pmnc_key[i]);
		if (i != CCNT) {
			gatorfs_create_ulong(sb, dir, "event", &pmnc_event[i]);
		}
	}

	return 0;
}

static int gator_events_armv6_online(int** buffer)
{
	unsigned int cnt, len = 0, cpu = smp_processor_id();
	u32 pmnc;

	if (armv6_pmnc_read() & PMCR_E) {
		armv6_pmnc_write(armv6_pmnc_read() & ~PMCR_E);
	}

	/* initialize PMNC, reset overflow, D bit, C bit and P bit. */
	armv6_pmnc_write(PMCR_OFL_PMN0 | PMCR_OFL_PMN1 | PMCR_OFL_CCNT |
			PMCR_C | PMCR_P);

	/* configure control register */
	for (pmnc = 0, cnt = PMN0; cnt <= CCNT; cnt++) {
		unsigned long event;

		if (!pmnc_enabled[cnt])
			continue;

		event = pmnc_event[cnt] & 255;

		// Set event (if destined for PMNx counters)
		if (cnt == PMN0) {
			pmnc |= event << 20;
		} else if (cnt == PMN1) {
			pmnc |= event << 12;
		}

		// Reset counter
		armv6_pmnc_reset_counter(cnt);
	}
	armv6_pmnc_write(pmnc | PMCR_E);

	// return zero values, no need to read as the counters were just reset
	for (cnt = PMN0; cnt <= CCNT; cnt++) {
		if (pmnc_enabled[cnt]) {
			per_cpu(perfCnt, cpu)[len++] = pmnc_key[cnt];
			per_cpu(perfCnt, cpu)[len++] = 0;
		}
	}

	if (buffer)
		*buffer = per_cpu(perfCnt, cpu);

	return len;
}

static int gator_events_armv6_offline(int** buffer)
{
	unsigned int cnt;

	armv6_pmnc_write(armv6_pmnc_read() & ~PMCR_E);
	for (cnt = PMN0; cnt <= CCNT; cnt++) {
		armv6_pmnc_reset_counter(cnt);
	}

	return 0;
}

static void gator_events_armv6_stop(void)
{
	unsigned int cnt;

	for (cnt = PMN0; cnt <= CCNT; cnt++) {
		pmnc_enabled[cnt] = 0;
		pmnc_event[cnt] = 0;
	}
}

static int gator_events_armv6_read(int **buffer)
{
	int cnt, len = 0;
	int cpu = smp_processor_id();

	// a context switch may occur before the online hotplug event, thus need to check that the pmu is enabled
	if (!(armv6_pmnc_read() & PMCR_E)) {
		return 0;
	}

	for (cnt = PMN0; cnt <= CCNT; cnt++) {
		if (pmnc_enabled[cnt]) {
			u32 value = 0;
			switch (cnt) {
			case CCNT:
				asm volatile("mrc p15, 0, %0, c15, c12, 1" : "=r" (value));
				break;
			case PMN0:
				asm volatile("mrc p15, 0, %0, c15, c12, 2" : "=r" (value));
				break;
			case PMN1:
				asm volatile("mrc p15, 0, %0, c15, c12, 3" : "=r" (value));
				break;
			}
			armv6_pmnc_reset_counter(cnt);

			per_cpu(perfCnt, cpu)[len++] = pmnc_key[cnt];
			per_cpu(perfCnt, cpu)[len++] = value;
		}
	}

	if (buffer)
		*buffer = per_cpu(perfCnt, cpu);

	return len;
}

static struct gator_interface gator_events_armv6_interface = {
	.create_files = gator_events_armv6_create_files,
	.stop = gator_events_armv6_stop,
	.online = gator_events_armv6_online,
	.offline = gator_events_armv6_offline,
	.read = gator_events_armv6_read,
};

int gator_events_armv6_init(void)
{
	unsigned int cnt;

	switch (gator_cpuid()) {
	case ARM1136:
	case ARM1156:
	case ARM1176:
		pmnc_name = "ARM11";
		break;
	case ARM11MPCORE:
		pmnc_name = "ARM11MPCore";
		break;
	default:
		return -1;
	}

	for (cnt = PMN0; cnt <= CCNT; cnt++) {
		pmnc_enabled[cnt] = 0;
		pmnc_event[cnt] = 0;
		pmnc_key[cnt] = gator_events_get_key();
	}

	return gator_events_install(&gator_events_armv6_interface);
}

gator_events_init(gator_events_armv6_init);

#else
int gator_events_armv6_init(void)
{
	return -1;
}
#endif
