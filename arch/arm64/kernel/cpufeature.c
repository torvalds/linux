/*
 * Contains CPU feature definitions
 *
 * Copyright (C) 2015 ARM Ltd.
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
#include <asm/cpufeature.h>
#include <asm/processor.h>

static bool
feature_matches(u64 reg, const struct arm64_cpu_capabilities *entry)
{
	int val = cpuid_feature_extract_field(reg, entry->field_pos);

	return val >= entry->min_field_value;
}

static bool
has_id_aa64pfr0_feature(const struct arm64_cpu_capabilities *entry)
{
	u64 val;

	val = read_cpuid(id_aa64pfr0_el1);
	return feature_matches(val, entry);
}

static bool __maybe_unused
has_id_aa64mmfr1_feature(const struct arm64_cpu_capabilities *entry)
{
	u64 val;

	val = read_cpuid(id_aa64mmfr1_el1);
	return feature_matches(val, entry);
}

static const struct arm64_cpu_capabilities arm64_features[] = {
	{
		.desc = "GIC system register CPU interface",
		.capability = ARM64_HAS_SYSREG_GIC_CPUIF,
		.matches = has_id_aa64pfr0_feature,
		.field_pos = 24,
		.min_field_value = 1,
	},
#ifdef CONFIG_ARM64_PAN
	{
		.desc = "Privileged Access Never",
		.capability = ARM64_HAS_PAN,
		.matches = has_id_aa64mmfr1_feature,
		.field_pos = 20,
		.min_field_value = 1,
		.enable = cpu_enable_pan,
	},
#endif /* CONFIG_ARM64_PAN */
	{},
};

void check_cpu_capabilities(const struct arm64_cpu_capabilities *caps,
			    const char *info)
{
	int i;

	for (i = 0; caps[i].desc; i++) {
		if (!caps[i].matches(&caps[i]))
			continue;

		if (!cpus_have_cap(caps[i].capability))
			pr_info("%s %s\n", info, caps[i].desc);
		cpus_set_cap(caps[i].capability);
	}

	/* second pass allows enable() to consider interacting capabilities */
	for (i = 0; caps[i].desc; i++) {
		if (cpus_have_cap(caps[i].capability) && caps[i].enable)
			caps[i].enable();
	}
}

void check_local_cpu_features(void)
{
	check_cpu_capabilities(arm64_features, "detected feature");
}
