/*
 * Routines common for drivers handling Enhanced Speedstep Technology
 *  Copyright (C) 2004 Venkatesh Pallipadi <venkatesh.pallipadi@intel.com>
 *
 *  Licensed under the terms of the GNU GPL License version 2 -- see
 *  COPYING for details.
 */

static inline int is_const_loops_cpu(unsigned int cpu)
{
	struct cpuinfo_x86 	*c = cpu_data + cpu;

	if (c->x86_vendor != X86_VENDOR_INTEL || !cpu_has(c, X86_FEATURE_EST))
		return 0;

	/*
	 * on P-4s, the TSC runs with constant frequency independent of cpu freq
	 * when we use EST
	 */
	if (c->x86 == 0xf)
		return 1;

	return 0;
}

