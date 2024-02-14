/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_ARCHRANDOM_H
#define _ASM_ARCHRANDOM_H

#include <linux/arm-smccc.h>
#include <linux/bug.h>
#include <linux/kernel.h>
#include <linux/irqflags.h>
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

static inline bool __arm64_rndrrs(unsigned long *v)
{
	bool ok;

	/*
	 * Reads of RNDRRS set PSTATE.NZCV to 0b0000 on success,
	 * and set PSTATE.NZCV to 0b0100 otherwise.
	 */
	asm volatile(
		__mrs_s("%0", SYS_RNDRRS_EL0) "\n"
	"	cset %w1, ne\n"
	: "=r" (*v), "=r" (ok)
	:
	: "cc");

	return ok;
}

static __always_inline bool __cpu_has_rng(void)
{
	if (unlikely(!system_capabilities_finalized() && !preemptible()))
		return this_cpu_has_cap(ARM64_HAS_RNG);
	return alternative_has_cap_unlikely(ARM64_HAS_RNG);
}

static inline size_t __must_check arch_get_random_longs(unsigned long *v, size_t max_longs)
{
	/*
	 * Only support the generic interface after we have detected
	 * the system wide capability, avoiding complexity with the
	 * cpufeature code and with potential scheduling between CPUs
	 * with and without the feature.
	 */
	if (max_longs && __cpu_has_rng() && __arm64_rndr(v))
		return 1;
	return 0;
}

static inline size_t __must_check arch_get_random_seed_longs(unsigned long *v, size_t max_longs)
{
	if (!max_longs)
		return 0;

	/*
	 * We prefer the SMCCC call, since its semantics (return actual
	 * hardware backed entropy) is closer to the idea behind this
	 * function here than what even the RNDRSS register provides
	 * (the output of a pseudo RNG freshly seeded by a TRNG).
	 */
	if (smccc_trng_available) {
		struct arm_smccc_res res;

		max_longs = min_t(size_t, 3, max_longs);
		arm_smccc_1_1_invoke(ARM_SMCCC_TRNG_RND64, max_longs * 64, &res);
		if ((int)res.a0 >= 0) {
			switch (max_longs) {
			case 3:
				*v++ = res.a1;
				fallthrough;
			case 2:
				*v++ = res.a2;
				fallthrough;
			case 1:
				*v++ = res.a3;
				break;
			}
			return max_longs;
		}
	}

	/*
	 * RNDRRS is not backed by an entropy source but by a DRBG that is
	 * reseeded after each invocation. This is not a 100% fit but good
	 * enough to implement this API if no other entropy source exists.
	 */
	if (__cpu_has_rng() && __arm64_rndrrs(v))
		return 1;

	return 0;
}

static inline bool __init __early_cpu_has_rndr(void)
{
	/* Open code as we run prior to the first call to cpufeature. */
	unsigned long ftr = read_sysreg_s(SYS_ID_AA64ISAR0_EL1);
	return (ftr >> ID_AA64ISAR0_EL1_RNDR_SHIFT) & 0xf;
}

u64 kaslr_early_init(void *fdt);

#endif /* _ASM_ARCHRANDOM_H */
