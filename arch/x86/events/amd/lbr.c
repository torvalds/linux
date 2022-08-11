// SPDX-License-Identifier: GPL-2.0
#include <linux/perf_event.h>
#include <asm/perf_event.h>

#include "../perf_event.h"

__init int amd_pmu_lbr_init(void)
{
	union cpuid_0x80000022_ebx ebx;

	if (x86_pmu.version < 2 || !boot_cpu_has(X86_FEATURE_AMD_LBR_V2))
		return -EOPNOTSUPP;

	/* Set number of entries */
	ebx.full = cpuid_ebx(EXT_PERFMON_DEBUG_FEATURES);
	x86_pmu.lbr_nr = ebx.split.lbr_v2_stack_sz;

	pr_cont("%d-deep LBR, ", x86_pmu.lbr_nr);

	return 0;
}
