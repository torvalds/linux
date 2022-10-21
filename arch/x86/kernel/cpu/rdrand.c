// SPDX-License-Identifier: GPL-2.0-only
/*
 * This file is part of the Linux kernel.
 *
 * Copyright (c) 2011, Intel Corporation
 * Authors: Fenghua Yu <fenghua.yu@intel.com>,
 *          H. Peter Anvin <hpa@linux.intel.com>
 */

#include <asm/processor.h>
#include <asm/archrandom.h>
#include <asm/sections.h>

/*
 * RDRAND has Built-In-Self-Test (BIST) that runs on every invocation.
 * Run the instruction a few times as a sanity check. Also make sure
 * it's not outputting the same value over and over, which has happened
 * as a result of past CPU bugs.
 *
 * If it fails, it is simple to disable RDRAND and RDSEED here.
 */

void x86_init_rdrand(struct cpuinfo_x86 *c)
{
	enum { SAMPLES = 8, MIN_CHANGE = 5 };
	unsigned long sample, prev;
	bool failure = false;
	size_t i, changed;

	if (!cpu_has(c, X86_FEATURE_RDRAND))
		return;

	for (changed = 0, i = 0; i < SAMPLES; ++i) {
		if (!rdrand_long(&sample)) {
			failure = true;
			break;
		}
		changed += i && sample != prev;
		prev = sample;
	}
	if (changed < MIN_CHANGE)
		failure = true;

	if (failure) {
		clear_cpu_cap(c, X86_FEATURE_RDRAND);
		clear_cpu_cap(c, X86_FEATURE_RDSEED);
		pr_emerg("RDRAND is not reliable on this platform; disabling.\n");
	}
}
