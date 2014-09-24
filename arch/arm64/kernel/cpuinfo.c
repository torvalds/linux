/*
 * Record and handle CPU attributes.
 *
 * Copyright (C) 2014 ARM Ltd.
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
#include <asm/arch_timer.h>
#include <asm/cachetype.h>
#include <asm/cpu.h>
#include <asm/cputype.h>

#include <linux/bitops.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/printk.h>
#include <linux/smp.h>

/*
 * In case the boot CPU is hotpluggable, we record its initial state and
 * current state separately. Certain system registers may contain different
 * values depending on configuration at or after reset.
 */
DEFINE_PER_CPU(struct cpuinfo_arm64, cpu_data);
static struct cpuinfo_arm64 boot_cpu_data;

static char *icache_policy_str[] = {
	[ICACHE_POLICY_RESERVED] = "RESERVED/UNKNOWN",
	[ICACHE_POLICY_AIVIVT] = "AIVIVT",
	[ICACHE_POLICY_VIPT] = "VIPT",
	[ICACHE_POLICY_PIPT] = "PIPT",
};

unsigned long __icache_flags;

static void cpuinfo_detect_icache_policy(struct cpuinfo_arm64 *info)
{
	unsigned int cpu = smp_processor_id();
	u32 l1ip = CTR_L1IP(info->reg_ctr);

	if (l1ip != ICACHE_POLICY_PIPT)
		set_bit(ICACHEF_ALIASING, &__icache_flags);
	if (l1ip == ICACHE_POLICY_AIVIVT)
		set_bit(ICACHEF_AIVIVT, &__icache_flags);

	pr_info("Detected %s I-cache on CPU%d\n", icache_policy_str[l1ip], cpu);
}

static int check_reg_mask(char *name, u64 mask, u64 boot, u64 cur, int cpu)
{
	if ((boot & mask) == (cur & mask))
		return 0;

	pr_warn("SANITY CHECK: Unexpected variation in %s. Boot CPU: %#016lx, CPU%d: %#016lx\n",
		name, (unsigned long)boot, cpu, (unsigned long)cur);

	return 1;
}

#define CHECK_MASK(field, mask, boot, cur, cpu) \
	check_reg_mask(#field, mask, (boot)->reg_ ## field, (cur)->reg_ ## field, cpu)

#define CHECK(field, boot, cur, cpu) \
	CHECK_MASK(field, ~0ULL, boot, cur, cpu)

/*
 * Verify that CPUs don't have unexpected differences that will cause problems.
 */
static void cpuinfo_sanity_check(struct cpuinfo_arm64 *cur)
{
	unsigned int cpu = smp_processor_id();
	struct cpuinfo_arm64 *boot = &boot_cpu_data;
	unsigned int diff = 0;

	/*
	 * The kernel can handle differing I-cache policies, but otherwise
	 * caches should look identical. Userspace JITs will make use of
	 * *minLine.
	 */
	diff |= CHECK_MASK(ctr, 0xffff3fff, boot, cur, cpu);

	/*
	 * Userspace may perform DC ZVA instructions. Mismatched block sizes
	 * could result in too much or too little memory being zeroed if a
	 * process is preempted and migrated between CPUs.
	 */
	diff |= CHECK(dczid, boot, cur, cpu);

	/* If different, timekeeping will be broken (especially with KVM) */
	diff |= CHECK(cntfrq, boot, cur, cpu);

	/*
	 * Even in big.LITTLE, processors should be identical instruction-set
	 * wise.
	 */
	diff |= CHECK(id_aa64isar0, boot, cur, cpu);
	diff |= CHECK(id_aa64isar1, boot, cur, cpu);

	/*
	 * Differing PARange support is fine as long as all peripherals and
	 * memory are mapped within the minimum PARange of all CPUs.
	 * Linux should not care about secure memory.
	 * ID_AA64MMFR1 is currently RES0.
	 */
	diff |= CHECK_MASK(id_aa64mmfr0, 0xffffffffffff0ff0, boot, cur, cpu);
	diff |= CHECK(id_aa64mmfr1, boot, cur, cpu);

	/*
	 * EL3 is not our concern.
	 * ID_AA64PFR1 is currently RES0.
	 */
	diff |= CHECK_MASK(id_aa64pfr0, 0xffffffffffff0fff, boot, cur, cpu);
	diff |= CHECK(id_aa64pfr1, boot, cur, cpu);

	/*
	 * If we have AArch32, we care about 32-bit features for compat. These
	 * registers should be RES0 otherwise.
	 */
	diff |= CHECK(id_isar0, boot, cur, cpu);
	diff |= CHECK(id_isar1, boot, cur, cpu);
	diff |= CHECK(id_isar2, boot, cur, cpu);
	diff |= CHECK(id_isar3, boot, cur, cpu);
	diff |= CHECK(id_isar4, boot, cur, cpu);
	diff |= CHECK(id_isar5, boot, cur, cpu);
	diff |= CHECK(id_mmfr0, boot, cur, cpu);
	diff |= CHECK(id_mmfr1, boot, cur, cpu);
	diff |= CHECK(id_mmfr2, boot, cur, cpu);
	diff |= CHECK(id_mmfr3, boot, cur, cpu);
	diff |= CHECK(id_pfr0, boot, cur, cpu);
	diff |= CHECK(id_pfr1, boot, cur, cpu);

	/*
	 * Mismatched CPU features are a recipe for disaster. Don't even
	 * pretend to support them.
	 */
	WARN_TAINT_ONCE(diff, TAINT_CPU_OUT_OF_SPEC,
			"Unsupported CPU feature variation.");
}

static void __cpuinfo_store_cpu(struct cpuinfo_arm64 *info)
{
	info->reg_cntfrq = arch_timer_get_cntfrq();
	info->reg_ctr = read_cpuid_cachetype();
	info->reg_dczid = read_cpuid(DCZID_EL0);
	info->reg_midr = read_cpuid_id();

	info->reg_id_aa64isar0 = read_cpuid(ID_AA64ISAR0_EL1);
	info->reg_id_aa64isar1 = read_cpuid(ID_AA64ISAR1_EL1);
	info->reg_id_aa64mmfr0 = read_cpuid(ID_AA64MMFR0_EL1);
	info->reg_id_aa64mmfr1 = read_cpuid(ID_AA64MMFR1_EL1);
	info->reg_id_aa64pfr0 = read_cpuid(ID_AA64PFR0_EL1);
	info->reg_id_aa64pfr1 = read_cpuid(ID_AA64PFR1_EL1);

	info->reg_id_isar0 = read_cpuid(ID_ISAR0_EL1);
	info->reg_id_isar1 = read_cpuid(ID_ISAR1_EL1);
	info->reg_id_isar2 = read_cpuid(ID_ISAR2_EL1);
	info->reg_id_isar3 = read_cpuid(ID_ISAR3_EL1);
	info->reg_id_isar4 = read_cpuid(ID_ISAR4_EL1);
	info->reg_id_isar5 = read_cpuid(ID_ISAR5_EL1);
	info->reg_id_mmfr0 = read_cpuid(ID_MMFR0_EL1);
	info->reg_id_mmfr1 = read_cpuid(ID_MMFR1_EL1);
	info->reg_id_mmfr2 = read_cpuid(ID_MMFR2_EL1);
	info->reg_id_mmfr3 = read_cpuid(ID_MMFR3_EL1);
	info->reg_id_pfr0 = read_cpuid(ID_PFR0_EL1);
	info->reg_id_pfr1 = read_cpuid(ID_PFR1_EL1);

	cpuinfo_detect_icache_policy(info);
}

void cpuinfo_store_cpu(void)
{
	struct cpuinfo_arm64 *info = this_cpu_ptr(&cpu_data);
	__cpuinfo_store_cpu(info);
	cpuinfo_sanity_check(info);
}

void __init cpuinfo_store_boot_cpu(void)
{
	struct cpuinfo_arm64 *info = &per_cpu(cpu_data, 0);
	__cpuinfo_store_cpu(info);

	boot_cpu_data = *info;
}
