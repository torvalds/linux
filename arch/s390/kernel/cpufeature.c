// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright IBM Corp. 2022
 */

#include <linux/cpufeature.h>
#include <linux/export.h>
#include <linux/bug.h>
#include <asm/machine.h>
#include <asm/elf.h>

enum {
	TYPE_HWCAP,
	TYPE_FACILITY,
	TYPE_MACHINE,
};

struct s390_cpu_feature {
	unsigned int type	: 4;
	unsigned int num	: 28;
};

static struct s390_cpu_feature s390_cpu_features[MAX_CPU_FEATURES] = {
	[S390_CPU_FEATURE_MSA]	= {.type = TYPE_HWCAP, .num = HWCAP_NR_MSA},
	[S390_CPU_FEATURE_VXRS]	= {.type = TYPE_HWCAP, .num = HWCAP_NR_VXRS},
	[S390_CPU_FEATURE_UV]	= {.type = TYPE_FACILITY, .num = 158},
	[S390_CPU_FEATURE_D288]	= {.type = TYPE_MACHINE, .num = MFEATURE_DIAG288},
};

/*
 * cpu_have_feature - Test CPU features on module initialization
 */
int cpu_have_feature(unsigned int num)
{
	struct s390_cpu_feature *feature;

	if (WARN_ON_ONCE(num >= MAX_CPU_FEATURES))
		return 0;
	feature = &s390_cpu_features[num];
	switch (feature->type) {
	case TYPE_HWCAP:
		return !!(elf_hwcap & BIT(feature->num));
	case TYPE_FACILITY:
		return test_facility(feature->num);
	case TYPE_MACHINE:
		return test_machine_feature(feature->num);
	default:
		WARN_ON_ONCE(1);
		return 0;
	}
}
EXPORT_SYMBOL(cpu_have_feature);
