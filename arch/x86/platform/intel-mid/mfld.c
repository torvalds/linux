/*
 * mfld.c: Intel Medfield platform setup code
 *
 * (C) Copyright 2013 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 */

#include <linux/init.h>

#include <asm/apic.h>
#include <asm/intel-mid.h>
#include <asm/intel_mid_vrtc.h>

#include "intel_mid_weak_decls.h"

static void penwell_arch_setup(void);
/* penwell arch ops */
static struct intel_mid_ops penwell_ops = {
	.arch_setup = penwell_arch_setup,
};

static void mfld_power_off(void)
{
}

static unsigned long __init mfld_calibrate_tsc(void)
{
	unsigned long fast_calibrate;
	u32 lo, hi, ratio, fsb;

	rdmsr(MSR_IA32_PERF_STATUS, lo, hi);
	pr_debug("IA32 perf status is 0x%x, 0x%0x\n", lo, hi);
	ratio = (hi >> 8) & 0x1f;
	pr_debug("ratio is %d\n", ratio);
	if (!ratio) {
		pr_err("read a zero ratio, should be incorrect!\n");
		pr_err("force tsc ratio to 16 ...\n");
		ratio = 16;
	}
	rdmsr(MSR_FSB_FREQ, lo, hi);
	if ((lo & 0x7) == 0x7)
		fsb = FSB_FREQ_83SKU;
	else
		fsb = FSB_FREQ_100SKU;
	fast_calibrate = ratio * fsb;
	pr_debug("read penwell tsc %lu khz\n", fast_calibrate);
	lapic_timer_frequency = fsb * 1000 / HZ;

	/*
	 * TSC on Intel Atom SoCs is reliable and of known frequency.
	 * See tsc_msr.c for details.
	 */
	setup_force_cpu_cap(X86_FEATURE_TSC_KNOWN_FREQ);
	setup_force_cpu_cap(X86_FEATURE_TSC_RELIABLE);

	return fast_calibrate;
}

static void __init penwell_arch_setup(void)
{
	x86_platform.calibrate_tsc = mfld_calibrate_tsc;
	pm_power_off = mfld_power_off;
}

void *get_penwell_ops(void)
{
	return &penwell_ops;
}

void *get_cloverview_ops(void)
{
	return &penwell_ops;
}
