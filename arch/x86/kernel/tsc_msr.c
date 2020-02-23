// SPDX-License-Identifier: GPL-2.0
/*
 * TSC frequency enumeration via MSR
 *
 * Copyright (C) 2013, 2018 Intel Corporation
 * Author: Bin Gao <bin.gao@intel.com>
 */

#include <linux/kernel.h>

#include <asm/apic.h>
#include <asm/cpu_device_id.h>
#include <asm/intel-family.h>
#include <asm/msr.h>
#include <asm/param.h>
#include <asm/tsc.h>

#define MAX_NUM_FREQS	16 /* 4 bits to select the frequency */

/*
 * If MSR_PERF_STAT[31] is set, the maximum resolved bus ratio can be
 * read in MSR_PLATFORM_ID[12:8], otherwise in MSR_PERF_STAT[44:40].
 * Unfortunately some Intel Atom SoCs aren't quite compliant to this,
 * so we need manually differentiate SoC families. This is what the
 * field use_msr_plat does.
 */
struct freq_desc {
	bool use_msr_plat;
	u32 freqs[MAX_NUM_FREQS];
	u32 mask;
};

/*
 * Penwell and Clovertrail use spread spectrum clock,
 * so the freq number is not exactly the same as reported
 * by MSR based on SDM.
 */
static const struct freq_desc freq_desc_pnw = {
	.use_msr_plat = false,
	.freqs = { 0, 0, 0, 0, 0, 99840, 0, 83200 },
	.mask = 0x07,
};

static const struct freq_desc freq_desc_clv = {
	.use_msr_plat = false,
	.freqs = { 0, 133200, 0, 0, 0, 99840, 0, 83200 },
	.mask = 0x07,
};

static const struct freq_desc freq_desc_byt = {
	.use_msr_plat = true,
	.freqs = { 83300, 100000, 133300, 116700, 80000, 0, 0, 0 },
	.mask = 0x07,
};

static const struct freq_desc freq_desc_cht = {
	.use_msr_plat = true,
	.freqs = { 83300, 100000, 133300, 116700, 80000, 93300, 90000,
		   88900, 87500 },
	.mask = 0x0f,
};

static const struct freq_desc freq_desc_tng = {
	.use_msr_plat = true,
	.freqs = { 0, 100000, 133300, 0, 0, 0, 0, 0 },
	.mask = 0x07,
};

static const struct freq_desc freq_desc_ann = {
	.use_msr_plat = true,
	.freqs = { 83300, 100000, 133300, 100000, 0, 0, 0, 0 },
	.mask = 0x0f,
};

static const struct freq_desc freq_desc_lgm = {
	.use_msr_plat = true,
	.freqs = { 78000, 78000, 78000, 78000, 78000, 78000, 78000, 78000 },
	.mask = 0x0f,
};

static const struct x86_cpu_id tsc_msr_cpu_ids[] = {
	INTEL_CPU_FAM6(ATOM_SALTWELL_MID,	freq_desc_pnw),
	INTEL_CPU_FAM6(ATOM_SALTWELL_TABLET,	freq_desc_clv),
	INTEL_CPU_FAM6(ATOM_SILVERMONT,		freq_desc_byt),
	INTEL_CPU_FAM6(ATOM_SILVERMONT_MID,	freq_desc_tng),
	INTEL_CPU_FAM6(ATOM_AIRMONT,		freq_desc_cht),
	INTEL_CPU_FAM6(ATOM_AIRMONT_MID,	freq_desc_ann),
	INTEL_CPU_FAM6(ATOM_AIRMONT_NP,		freq_desc_lgm),
	{}
};

/*
 * MSR-based CPU/TSC frequency discovery for certain CPUs.
 *
 * Set global "lapic_timer_period" to bus_clock_cycles/jiffy
 * Return processor base frequency in KHz, or 0 on failure.
 */
unsigned long cpu_khz_from_msr(void)
{
	u32 lo, hi, ratio, freq;
	const struct freq_desc *freq_desc;
	const struct x86_cpu_id *id;
	unsigned long res;
	int index;

	id = x86_match_cpu(tsc_msr_cpu_ids);
	if (!id)
		return 0;

	freq_desc = (struct freq_desc *)id->driver_data;
	if (freq_desc->use_msr_plat) {
		rdmsr(MSR_PLATFORM_INFO, lo, hi);
		ratio = (lo >> 8) & 0xff;
	} else {
		rdmsr(MSR_IA32_PERF_STATUS, lo, hi);
		ratio = (hi >> 8) & 0x1f;
	}

	/* Get FSB FREQ ID */
	rdmsr(MSR_FSB_FREQ, lo, hi);
	index = lo & freq_desc->mask;

	/* Map CPU reference clock freq ID(0-7) to CPU reference clock freq(KHz) */
	freq = freq_desc->freqs[index];

	/* TSC frequency = maximum resolved freq * maximum resolved bus ratio */
	res = freq * ratio;

	if (freq == 0)
		pr_err("Error MSR_FSB_FREQ index %d is unknown\n", index);

#ifdef CONFIG_X86_LOCAL_APIC
	lapic_timer_period = (freq * 1000) / HZ;
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
