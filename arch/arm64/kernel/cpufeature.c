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

#define pr_fmt(fmt) "CPU features: " fmt

#include <linux/types.h>
#include <asm/cpu.h>
#include <asm/cpufeature.h>
#include <asm/processor.h>

unsigned long elf_hwcap __read_mostly;
EXPORT_SYMBOL_GPL(elf_hwcap);

#ifdef CONFIG_COMPAT
#define COMPAT_ELF_HWCAP_DEFAULT	\
				(COMPAT_HWCAP_HALF|COMPAT_HWCAP_THUMB|\
				 COMPAT_HWCAP_FAST_MULT|COMPAT_HWCAP_EDSP|\
				 COMPAT_HWCAP_TLS|COMPAT_HWCAP_VFP|\
				 COMPAT_HWCAP_VFPv3|COMPAT_HWCAP_VFPv4|\
				 COMPAT_HWCAP_NEON|COMPAT_HWCAP_IDIV|\
				 COMPAT_HWCAP_LPAE)
unsigned int compat_elf_hwcap __read_mostly = COMPAT_ELF_HWCAP_DEFAULT;
unsigned int compat_elf_hwcap2 __read_mostly;
#endif

DECLARE_BITMAP(cpu_hwcaps, ARM64_NCAPS);


static bool
feature_matches(u64 reg, const struct arm64_cpu_capabilities *entry)
{
	int val = cpuid_feature_extract_field(reg, entry->field_pos);

	return val >= entry->min_field_value;
}

#define __ID_FEAT_CHK(reg)						\
static bool __maybe_unused						\
has_##reg##_feature(const struct arm64_cpu_capabilities *entry)		\
{									\
	u64 val;							\
									\
	val = read_cpuid(reg##_el1);					\
	return feature_matches(val, entry);				\
}

__ID_FEAT_CHK(id_aa64pfr0);
__ID_FEAT_CHK(id_aa64mmfr1);
__ID_FEAT_CHK(id_aa64isar0);

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
#if defined(CONFIG_AS_LSE) && defined(CONFIG_ARM64_LSE_ATOMICS)
	{
		.desc = "LSE atomic instructions",
		.capability = ARM64_HAS_LSE_ATOMICS,
		.matches = has_id_aa64isar0_feature,
		.field_pos = 20,
		.min_field_value = 2,
	},
#endif /* CONFIG_AS_LSE && CONFIG_ARM64_LSE_ATOMICS */
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
	check_cpu_capabilities(arm64_features, "detected feature:");
}

void __init setup_cpu_features(void)
{
	u64 features;
	s64 block;
	u32 cwg;
	int cls;

	/*
	 * Check for sane CTR_EL0.CWG value.
	 */
	cwg = cache_type_cwg();
	cls = cache_line_size();
	if (!cwg)
		pr_warn("No Cache Writeback Granule information, assuming cache line size %d\n",
			cls);
	if (L1_CACHE_BYTES < cls)
		pr_warn("L1_CACHE_BYTES smaller than the Cache Writeback Granule (%d < %d)\n",
			L1_CACHE_BYTES, cls);

	/*
	 * ID_AA64ISAR0_EL1 contains 4-bit wide signed feature blocks.
	 * The blocks we test below represent incremental functionality
	 * for non-negative values. Negative values are reserved.
	 */
	features = read_cpuid(ID_AA64ISAR0_EL1);
	block = cpuid_feature_extract_field(features, 4);
	if (block > 0) {
		switch (block) {
		default:
		case 2:
			elf_hwcap |= HWCAP_PMULL;
		case 1:
			elf_hwcap |= HWCAP_AES;
		case 0:
			break;
		}
	}

	if (cpuid_feature_extract_field(features, 8) > 0)
		elf_hwcap |= HWCAP_SHA1;

	if (cpuid_feature_extract_field(features, 12) > 0)
		elf_hwcap |= HWCAP_SHA2;

	if (cpuid_feature_extract_field(features, 16) > 0)
		elf_hwcap |= HWCAP_CRC32;

	block = cpuid_feature_extract_field(features, 20);
	if (block > 0) {
		switch (block) {
		default:
		case 2:
			elf_hwcap |= HWCAP_ATOMICS;
		case 1:
			/* RESERVED */
		case 0:
			break;
		}
	}

#ifdef CONFIG_COMPAT
	/*
	 * ID_ISAR5_EL1 carries similar information as above, but pertaining to
	 * the AArch32 32-bit execution state.
	 */
	features = read_cpuid(ID_ISAR5_EL1);
	block = cpuid_feature_extract_field(features, 4);
	if (block > 0) {
		switch (block) {
		default:
		case 2:
			compat_elf_hwcap2 |= COMPAT_HWCAP2_PMULL;
		case 1:
			compat_elf_hwcap2 |= COMPAT_HWCAP2_AES;
		case 0:
			break;
		}
	}

	if (cpuid_feature_extract_field(features, 8) > 0)
		compat_elf_hwcap2 |= COMPAT_HWCAP2_SHA1;

	if (cpuid_feature_extract_field(features, 12) > 0)
		compat_elf_hwcap2 |= COMPAT_HWCAP2_SHA2;

	if (cpuid_feature_extract_field(features, 16) > 0)
		compat_elf_hwcap2 |= COMPAT_HWCAP2_CRC32;
#endif
}
