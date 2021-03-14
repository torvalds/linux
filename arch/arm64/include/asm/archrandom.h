/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_ARCHRANDOM_H
#define _ASM_ARCHRANDOM_H

#ifdef CONFIG_ARCH_RANDOM

#include <linux/arm-smccc.h>
#include <linux/bug.h>
#include <linux/kernel.h>
#include <asm/cpufeature.h>

#define ARM_SMCCC_TRNG_MIN_VERSION	0x10000UL

extern bool smccc_trng_available;

static inline bool __init smccc_probe_trng(void)
{
	struct arm_smccc_res res;

	arm_smccc_1_1_invoke(ARM_SMCCC_TRNG_VERSION, &res);
	if ((s32)res.a0 < 0)
		return false;

	return res.a0 >= ARM_SMCCC_TRNG_MIN_VERSION;
}

static inline bool __arm64_rndr(unsigned long *v)
{
	bool ok;

	/*
	 * Reads of RNDR set PSTATE.NZCV to 0b0000 on success,
	 * and set PSTATE.NZCV to 0b0100 otherwise.
	 */
	asm volatile(
		__mrs_s("%0", SYS_RNDR_EL0) "\n"
	"	cset %w1, ne\n"
	: "=r" (*v), "=r" (ok)
	:
	: "cc");

	return ok;
}

static inline bool __must_check arch_get_random_long(unsigned long *v)
{
	return false;
}

static inline bool __must_check arch_get_random_int(unsigned int *v)
{
	return false;
}

static inline bool __must_check arch_get_random_seed_long(unsigned long *v)
{
	struct arm_smccc_res res;

	/*
	 * We prefer the SMCCC call, since its semantics (return actual
	 * hardware backed entropy) is closer to the idea behind this
	 * function here than what even the RNDRSS register provides
	 * (the output of a pseudo RNG freshly seeded by a TRNG).
	 */
	if (smccc_trng_available) {
		arm_smccc_1_1_invoke(ARM_SMCCC_TRNG_RND64, 64, &res);
		if ((int)res.a0 >= 0) {
			*v = res.a3;
			return true;
		}
	}

	/*
	 * Only support the generic interface after we have detected
	 * the system wide capability, avoiding complexity with the
	 * cpufeature code and with potential scheduling between CPUs
	 * with and without the feature.
	 */
	if (cpus_have_const_cap(ARM64_HAS_RNG) && __arm64_rndr(v))
		return true;

	return false;
}

static inline bool __must_check arch_get_random_seed_int(unsigned int *v)
{
	struct arm_smccc_res res;
	unsigned long val;

	if (smccc_trng_available) {
		arm_smccc_1_1_invoke(ARM_SMCCC_TRNG_RND64, 32, &res);
		if ((int)res.a0 >= 0) {
			*v = res.a3 & GENMASK(31, 0);
			return true;
		}
	}

	if (cpus_have_const_cap(ARM64_HAS_RNG)) {
		if (__arm64_rndr(&val)) {
			*v = val;
			return true;
		}
	}

	return false;
}

static inline bool __init __early_cpu_has_rndr(void)
{
	/* Open code as we run prior to the first call to cpufeature. */
	unsigned long ftr = read_sysreg_s(SYS_ID_AA64ISAR0_EL1);
	return (ftr >> ID_AA64ISAR0_RNDR_SHIFT) & 0xf;
}

static inline bool __init __must_check
arch_get_random_seed_long_early(unsigned long *v)
{
	WARN_ON(system_state != SYSTEM_BOOTING);

	if (smccc_trng_available) {
		struct arm_smccc_res res;

		arm_smccc_1_1_invoke(ARM_SMCCC_TRNG_RND64, 64, &res);
		if ((int)res.a0 >= 0) {
			*v = res.a3;
			return true;
		}
	}

	if (__early_cpu_has_rndr() && __arm64_rndr(v))
		return true;

	return false;
}
#define arch_get_random_seed_long_early arch_get_random_seed_long_early

#else /* !CONFIG_ARCH_RANDOM */

static inline bool __init smccc_probe_trng(void)
{
	return false;
}

#endif /* CONFIG_ARCH_RANDOM */
#endif /* _ASM_ARCHRANDOM_H */
