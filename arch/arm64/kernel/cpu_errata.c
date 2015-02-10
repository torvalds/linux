/*
 * Contains CPU specific errata definitions
 *
 * Copyright (C) 2014 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#define pr_fmt(fmt) "alternatives: " fmt

#include <linux/types.h>
#include <asm/cpu.h>
#include <asm/cputype.h>
#include <asm/cpufeature.h>

#define MIDR_CORTEX_A53 MIDR_CPU_PART(ARM_CPU_IMP_ARM, ARM_CPU_PART_CORTEX_A53)
#define MIDR_CORTEX_A57 MIDR_CPU_PART(ARM_CPU_IMP_ARM, ARM_CPU_PART_CORTEX_A57)

/*
 * Add a struct or another datatype to the union below if you need
 * different means to detect an affected CPU.
 */
struct arm64_cpu_capabilities {
	const char *desc;
	u16 capability;
	bool (*is_affected)(struct arm64_cpu_capabilities *);
	union {
		struct {
			u32 midr_model;
			u32 midr_range_min, midr_range_max;
		};
	};
};

#define CPU_MODEL_MASK (MIDR_IMPLEMENTOR_MASK | MIDR_PARTNUM_MASK | \
			MIDR_ARCHITECTURE_MASK)

static bool __maybe_unused
is_affected_midr_range(struct arm64_cpu_capabilities *entry)
{
	u32 midr = read_cpuid_id();

	if ((midr & CPU_MODEL_MASK) != entry->midr_model)
		return false;

	midr &= MIDR_REVISION_MASK | MIDR_VARIANT_MASK;

	return (midr >= entry->midr_range_min && midr <= entry->midr_range_max);
}

#define MIDR_RANGE(model, min, max) \
	.is_affected = is_affected_midr_range, \
	.midr_model = model, \
	.midr_range_min = min, \
	.midr_range_max = max

struct arm64_cpu_capabilities arm64_errata[] = {
#if	defined(CONFIG_ARM64_ERRATUM_826319) || \
	defined(CONFIG_ARM64_ERRATUM_827319) || \
	defined(CONFIG_ARM64_ERRATUM_824069)
	{
	/* Cortex-A53 r0p[012] */
		.desc = "ARM errata 826319, 827319, 824069",
		.capability = ARM64_WORKAROUND_CLEAN_CACHE,
		MIDR_RANGE(MIDR_CORTEX_A53, 0x00, 0x02),
	},
#endif
#ifdef CONFIG_ARM64_ERRATUM_819472
	{
	/* Cortex-A53 r0p[01] */
		.desc = "ARM errata 819472",
		.capability = ARM64_WORKAROUND_CLEAN_CACHE,
		MIDR_RANGE(MIDR_CORTEX_A53, 0x00, 0x01),
	},
#endif
#ifdef CONFIG_ARM64_ERRATUM_832075
	{
	/* Cortex-A57 r0p0 - r1p2 */
		.desc = "ARM erratum 832075",
		.capability = ARM64_WORKAROUND_DEVICE_LOAD_ACQUIRE,
		MIDR_RANGE(MIDR_CORTEX_A57, 0x00, 0x12),
	},
#endif
	{
	}
};

void check_local_cpu_errata(void)
{
	struct arm64_cpu_capabilities *cpus = arm64_errata;
	int i;

	for (i = 0; cpus[i].desc; i++) {
		if (!cpus[i].is_affected(&cpus[i]))
			continue;

		if (!cpus_have_cap(cpus[i].capability))
			pr_info("enabling workaround for %s\n", cpus[i].desc);
		cpus_set_cap(cpus[i].capability);
	}
}
