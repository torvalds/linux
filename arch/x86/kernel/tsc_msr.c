/*
 * tsc_msr.c - MSR based TSC calibration on Intel Atom SoC platforms.
 *
 * TSC in Intel Atom SoC runs at a constant rate which can be figured
 * by this formula:
 * <maximum core-clock to bus-clock ratio> * <maximum resolved frequency>
 * See Intel 64 and IA-32 System Programming Guid section 16.12 and 30.11.5
 * for details.
 * Especially some Intel Atom SoCs don't have PIT(i8254) or HPET, so MSR
 * based calibration is the only option.
 *
 *
 * Copyright (C) 2013 Intel Corporation
 * Author: Bin Gao <bin.gao@intel.com>
 *
 * This file is released under the GPLv2.
 */

#include <linux/kernel.h>
#include <asm/processor.h>
#include <asm/setup.h>
#include <asm/apic.h>
#include <asm/param.h>

/* CPU reference clock frequency: in KHz */
#define FREQ_83		83200
#define FREQ_100	99840
#define FREQ_133	133200
#define FREQ_166	166400

#define MAX_NUM_FREQS	8

/*
 * According to Intel 64 and IA-32 System Programming Guide,
 * if MSR_PERF_STAT[31] is set, the maximum resolved bus ratio can be
 * read in MSR_PLATFORM_ID[12:8], otherwise in MSR_PERF_STAT[44:40].
 * Unfortunately some Intel Atom SoCs aren't quite compliant to this,
 * so we need manually differentiate SoC families. This is what the
 * field msr_plat does.
 */
struct freq_desc {
	u8 x86_family;	/* CPU family */
	u8 x86_model;	/* model */
	u8 msr_plat;	/* 1: use MSR_PLATFORM_INFO, 0: MSR_IA32_PERF_STATUS */
	u32 freqs[MAX_NUM_FREQS];
};

static struct freq_desc freq_desc_tables[] = {
	/* PNW */
	{ 6, 0x27, 0, { 0, 0, 0, 0, 0, FREQ_100, 0, FREQ_83 } },
	/* CLV+ */
	{ 6, 0x35, 0, { 0, FREQ_133, 0, 0, 0, FREQ_100, 0, FREQ_83 } },
	/* TNG */
	{ 6, 0x4a, 1, { 0, FREQ_100, FREQ_133, 0, 0, 0, 0, 0 } },
	/* VLV2 */
	{ 6, 0x37, 1, { 0, FREQ_100, FREQ_133, FREQ_166, 0, 0, 0, 0 } },
	/* ANN */
	{ 6, 0x5a, 1, { FREQ_83, FREQ_100, FREQ_133, FREQ_100, 0, 0, 0, 0 } },
};

static int match_cpu(u8 family, u8 model)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(freq_desc_tables); i++) {
		if ((family == freq_desc_tables[i].x86_family) &&
			(model == freq_desc_tables[i].x86_model))
			return i;
	}

	return -1;
}

/* Map CPU reference clock freq ID(0-7) to CPU reference clock freq(KHz) */
#define id_to_freq(cpu_index, freq_id) \
	(freq_desc_tables[cpu_index].freqs[freq_id])

/*
 * Do MSR calibration only for known/supported CPUs.
 * Return values:
 * -1: CPU is unknown/unsupported for MSR based calibration
 *  0: CPU is known/supported, but calibration failed
 *  1: CPU is known/supported, and calibration succeeded
 */
int try_msr_calibrate_tsc(unsigned long *fast_calibrate)
{
	int cpu_index;
	u32 lo, hi, ratio, freq_id, freq;

	cpu_index = match_cpu(boot_cpu_data.x86, boot_cpu_data.x86_model);
	if (cpu_index < 0)
		return -1;

	*fast_calibrate = 0;

	if (freq_desc_tables[cpu_index].msr_plat) {
		rdmsr(MSR_PLATFORM_INFO, lo, hi);
		ratio = (lo >> 8) & 0x1f;
	} else {
		rdmsr(MSR_IA32_PERF_STATUS, lo, hi);
		ratio = (hi >> 8) & 0x1f;
	}
	pr_info("Maximum core-clock to bus-clock ratio: 0x%x\n", ratio);

	if (!ratio)
		return 0;

	/* Get FSB FREQ ID */
	rdmsr(MSR_FSB_FREQ, lo, hi);
	freq_id = lo & 0x7;
	freq = id_to_freq(cpu_index, freq_id);
	pr_info("Resolved frequency ID: %u, frequency: %u KHz\n",
				freq_id, freq);
	if (!freq)
		return 0;

	/* TSC frequency = maximum resolved freq * maximum resolved bus ratio */
	*fast_calibrate = freq * ratio;
	pr_info("TSC runs at %lu KHz\n", *fast_calibrate);

#ifdef CONFIG_X86_LOCAL_APIC
	lapic_timer_frequency = (freq * 1000) / HZ;
	pr_info("lapic_timer_frequency = %d\n", lapic_timer_frequency);
#endif

	return 1;
}
