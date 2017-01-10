/*
 * tsc_msr.c - TSC frequency enumeration via MSR
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

#define MAX_NUM_FREQS	9

/*
 * If MSR_PERF_STAT[31] is set, the maximum resolved bus ratio can be
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
	{ 6, 0x27, 0, { 0, 0, 0, 0, 0, 99840, 0, 83200 } },
	/* CLV+ */
	{ 6, 0x35, 0, { 0, 133200, 0, 0, 0, 99840, 0, 83200 } },
	/* TNG - Intel Atom processor Z3400 series */
	{ 6, 0x4a, 1, { 0, 100000, 133300, 0, 0, 0, 0, 0 } },
	/* VLV2 - Intel Atom processor E3000, Z3600, Z3700 series */
	{ 6, 0x37, 1, { 83300, 100000, 133300, 116700, 80000, 0, 0, 0 } },
	/* ANN - Intel Atom processor Z3500 series */
	{ 6, 0x5a, 1, { 83300, 100000, 133300, 100000, 0, 0, 0, 0 } },
	/* AMT - Intel Atom processor X7-Z8000 and X5-Z8000 series */
	{ 6, 0x4c, 1, { 83300, 100000, 133300, 116700,
			80000, 93300, 90000, 88900, 87500 } },
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
 * MSR-based CPU/TSC frequency discovery for certain CPUs.
 *
 * Set global "lapic_timer_frequency" to bus_clock_cycles/jiffy
 * Return processor base frequency in KHz, or 0 on failure.
 */
unsigned long cpu_khz_from_msr(void)
{
	u32 lo, hi, ratio, freq_id, freq;
	unsigned long res;
	int cpu_index;

	if (boot_cpu_data.x86_vendor != X86_VENDOR_INTEL)
		return 0;

	cpu_index = match_cpu(boot_cpu_data.x86, boot_cpu_data.x86_model);
	if (cpu_index < 0)
		return 0;

	if (freq_desc_tables[cpu_index].msr_plat) {
		rdmsr(MSR_PLATFORM_INFO, lo, hi);
		ratio = (lo >> 8) & 0xff;
	} else {
		rdmsr(MSR_IA32_PERF_STATUS, lo, hi);
		ratio = (hi >> 8) & 0x1f;
	}

	/* Get FSB FREQ ID */
	rdmsr(MSR_FSB_FREQ, lo, hi);
	freq_id = lo & 0x7;
	freq = id_to_freq(cpu_index, freq_id);

	/* TSC frequency = maximum resolved freq * maximum resolved bus ratio */
	res = freq * ratio;

#ifdef CONFIG_X86_LOCAL_APIC
	lapic_timer_frequency = (freq * 1000) / HZ;
#endif

	/*
	 * TSC frequency determined by MSR is always considered "known"
	 * because it is reported by HW.
	 * Another fact is that on MSR capable platforms, PIT/HPET is
	 * generally not available so calibration won't work at all.
	 */
	setup_force_cpu_cap(X86_FEATURE_TSC_KNOWN_FREQ);

	/*
	 * Unfortunately there is no way for hardware to tell whether the
	 * TSC is reliable.  We were told by silicon design team that TSC
	 * on Atom SoCs are always "reliable". TSC is also the only
	 * reliable clocksource on these SoCs (HPET is either not present
	 * or not functional) so mark TSC reliable which removes the
	 * requirement for a watchdog clocksource.
	 */
	setup_force_cpu_cap(X86_FEATURE_TSC_RELIABLE);

	return res;
}
