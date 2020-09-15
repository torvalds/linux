// SPDX-License-Identifier: GPL-2.0-only
/*
 * Handle detection, reporting and mitigation of Spectre v1, v2 and v4, as
 * detailed at:
 *
 *   https://developer.arm.com/support/arm-security-updates/speculative-processor-vulnerability
 *
 * This code was originally written hastily under an awful lot of stress and so
 * aspects of it are somewhat hacky. Unfortunately, changing anything in here
 * instantly makes me feel ill. Thanks, Jann. Thann.
 *
 * Copyright (C) 2018 ARM Ltd, All Rights Reserved.
 * Copyright (C) 2020 Google LLC
 *
 * "If there's something strange in your neighbourhood, who you gonna call?"
 *
 * Authors: Will Deacon <will@kernel.org> and Marc Zyngier <maz@kernel.org>
 */

#include <linux/arm-smccc.h>
#include <linux/cpu.h>
#include <linux/device.h>
#include <linux/prctl.h>

#include <asm/spectre.h>
#include <asm/traps.h>

/*
 * We try to ensure that the mitigation state can never change as the result of
 * onlining a late CPU.
 */
static void update_mitigation_state(enum mitigation_state *oldp,
				    enum mitigation_state new)
{
	enum mitigation_state state;

	do {
		state = READ_ONCE(*oldp);
		if (new <= state)
			break;

		/* Userspace almost certainly can't deal with this. */
		if (WARN_ON(system_capabilities_finalized()))
			break;
	} while (cmpxchg_relaxed(oldp, state, new) != state);
}

/*
 * Spectre v1.
 *
 * The kernel can't protect userspace for this one: it's each person for
 * themselves. Advertise what we're doing and be done with it.
 */
ssize_t cpu_show_spectre_v1(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	return sprintf(buf, "Mitigation: __user pointer sanitization\n");
}

/*
 * Spectre v2.
 *
 * This one sucks. A CPU is either:
 *
 * - Mitigated in hardware and advertised by ID_AA64PFR0_EL1.CSV2.
 * - Mitigated in hardware and listed in our "safe list".
 * - Mitigated in software by firmware.
 * - Mitigated in software by a CPU-specific dance in the kernel.
 * - Vulnerable.
 *
 * It's not unlikely for different CPUs in a big.LITTLE system to fall into
 * different camps.
 */
static enum mitigation_state spectre_v2_state;

static bool __read_mostly __nospectre_v2;
static int __init parse_spectre_v2_param(char *str)
{
	__nospectre_v2 = true;
	return 0;
}
early_param("nospectre_v2", parse_spectre_v2_param);

static bool spectre_v2_mitigations_off(void)
{
	bool ret = __nospectre_v2 || cpu_mitigations_off();

	if (ret)
		pr_info_once("spectre-v2 mitigation disabled by command line option\n");

	return ret;
}

ssize_t cpu_show_spectre_v2(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	switch (spectre_v2_state) {
	case SPECTRE_UNAFFECTED:
		return sprintf(buf, "Not affected\n");
	case SPECTRE_MITIGATED:
		return sprintf(buf, "Mitigation: Branch predictor hardening\n");
	case SPECTRE_VULNERABLE:
		fallthrough;
	default:
		return sprintf(buf, "Vulnerable\n");
	}
}

static enum mitigation_state spectre_v2_get_cpu_hw_mitigation_state(void)
{
	u64 pfr0;
	static const struct midr_range spectre_v2_safe_list[] = {
		MIDR_ALL_VERSIONS(MIDR_CORTEX_A35),
		MIDR_ALL_VERSIONS(MIDR_CORTEX_A53),
		MIDR_ALL_VERSIONS(MIDR_CORTEX_A55),
		MIDR_ALL_VERSIONS(MIDR_BRAHMA_B53),
		MIDR_ALL_VERSIONS(MIDR_HISI_TSV110),
		MIDR_ALL_VERSIONS(MIDR_QCOM_KRYO_3XX_SILVER),
		MIDR_ALL_VERSIONS(MIDR_QCOM_KRYO_4XX_SILVER),
		{ /* sentinel */ }
	};

	/* If the CPU has CSV2 set, we're safe */
	pfr0 = read_cpuid(ID_AA64PFR0_EL1);
	if (cpuid_feature_extract_unsigned_field(pfr0, ID_AA64PFR0_CSV2_SHIFT))
		return SPECTRE_UNAFFECTED;

	/* Alternatively, we have a list of unaffected CPUs */
	if (is_midr_in_range_list(read_cpuid_id(), spectre_v2_safe_list))
		return SPECTRE_UNAFFECTED;

	return SPECTRE_VULNERABLE;
}

#define SMCCC_ARCH_WORKAROUND_RET_UNAFFECTED	(1)

static enum mitigation_state spectre_v2_get_cpu_fw_mitigation_state(void)
{
	int ret;
	struct arm_smccc_res res;

	arm_smccc_1_1_invoke(ARM_SMCCC_ARCH_FEATURES_FUNC_ID,
			     ARM_SMCCC_ARCH_WORKAROUND_1, &res);

	ret = res.a0;
	switch (ret) {
	case SMCCC_RET_SUCCESS:
		return SPECTRE_MITIGATED;
	case SMCCC_ARCH_WORKAROUND_RET_UNAFFECTED:
		return SPECTRE_UNAFFECTED;
	default:
		fallthrough;
	case SMCCC_RET_NOT_SUPPORTED:
		return SPECTRE_VULNERABLE;
	}
}

bool has_spectre_v2(const struct arm64_cpu_capabilities *entry, int scope)
{
	WARN_ON(scope != SCOPE_LOCAL_CPU || preemptible());

	if (spectre_v2_get_cpu_hw_mitigation_state() == SPECTRE_UNAFFECTED)
		return false;

	if (spectre_v2_get_cpu_fw_mitigation_state() == SPECTRE_UNAFFECTED)
		return false;

	return true;
}

DEFINE_PER_CPU_READ_MOSTLY(struct bp_hardening_data, bp_hardening_data);

enum mitigation_state arm64_get_spectre_v2_state(void)
{
	return spectre_v2_state;
}

#ifdef CONFIG_KVM
#ifdef CONFIG_RANDOMIZE_BASE
#include <asm/cacheflush.h>
#include <asm/kvm_asm.h>

atomic_t arm64_el2_vector_last_slot = ATOMIC_INIT(-1);

static void __copy_hyp_vect_bpi(int slot, const char *hyp_vecs_start,
				const char *hyp_vecs_end)
{
	void *dst = lm_alias(__bp_harden_hyp_vecs + slot * SZ_2K);
	int i;

	for (i = 0; i < SZ_2K; i += 0x80)
		memcpy(dst + i, hyp_vecs_start, hyp_vecs_end - hyp_vecs_start);

	__flush_icache_range((uintptr_t)dst, (uintptr_t)dst + SZ_2K);
}

static void install_bp_hardening_cb(bp_hardening_cb_t fn)
{
	static DEFINE_RAW_SPINLOCK(bp_lock);
	int cpu, slot = -1;
	const char *hyp_vecs_start = __smccc_workaround_1_smc;
	const char *hyp_vecs_end = __smccc_workaround_1_smc +
				   __SMCCC_WORKAROUND_1_SMC_SZ;

	/*
	 * detect_harden_bp_fw() passes NULL for the hyp_vecs start/end if
	 * we're a guest. Skip the hyp-vectors work.
	 */
	if (!is_hyp_mode_available()) {
		__this_cpu_write(bp_hardening_data.fn, fn);
		return;
	}

	raw_spin_lock(&bp_lock);
	for_each_possible_cpu(cpu) {
		if (per_cpu(bp_hardening_data.fn, cpu) == fn) {
			slot = per_cpu(bp_hardening_data.hyp_vectors_slot, cpu);
			break;
		}
	}

	if (slot == -1) {
		slot = atomic_inc_return(&arm64_el2_vector_last_slot);
		BUG_ON(slot >= BP_HARDEN_EL2_SLOTS);
		__copy_hyp_vect_bpi(slot, hyp_vecs_start, hyp_vecs_end);
	}

	__this_cpu_write(bp_hardening_data.hyp_vectors_slot, slot);
	__this_cpu_write(bp_hardening_data.fn, fn);
	raw_spin_unlock(&bp_lock);
}
#else
static void install_bp_hardening_cb(bp_hardening_cb_t fn)
{
	__this_cpu_write(bp_hardening_data.fn, fn);
}
#endif	/* CONFIG_RANDOMIZE_BASE */
#endif	/* CONFIG_KVM */

static void call_smc_arch_workaround_1(void)
{
	arm_smccc_1_1_smc(ARM_SMCCC_ARCH_WORKAROUND_1, NULL);
}

static void call_hvc_arch_workaround_1(void)
{
	arm_smccc_1_1_hvc(ARM_SMCCC_ARCH_WORKAROUND_1, NULL);
}

static void qcom_link_stack_sanitisation(void)
{
	u64 tmp;

	asm volatile("mov	%0, x30		\n"
		     ".rept	16		\n"
		     "bl	. + 4		\n"
		     ".endr			\n"
		     "mov	x30, %0		\n"
		     : "=&r" (tmp));
}

static enum mitigation_state spectre_v2_enable_fw_mitigation(void)
{
	bp_hardening_cb_t cb;
	enum mitigation_state state;

	state = spectre_v2_get_cpu_fw_mitigation_state();
	if (state != SPECTRE_MITIGATED)
		return state;

	if (spectre_v2_mitigations_off())
		return SPECTRE_VULNERABLE;

	switch (arm_smccc_1_1_get_conduit()) {
	case SMCCC_CONDUIT_HVC:
		cb = call_hvc_arch_workaround_1;
		break;

	case SMCCC_CONDUIT_SMC:
		cb = call_smc_arch_workaround_1;
		break;

	default:
		return SPECTRE_VULNERABLE;
	}

	install_bp_hardening_cb(cb);
	return SPECTRE_MITIGATED;
}

static enum mitigation_state spectre_v2_enable_sw_mitigation(void)
{
	u32 midr;

	if (spectre_v2_mitigations_off())
		return SPECTRE_VULNERABLE;

	midr = read_cpuid_id();
	if (((midr & MIDR_CPU_MODEL_MASK) != MIDR_QCOM_FALKOR) &&
	    ((midr & MIDR_CPU_MODEL_MASK) != MIDR_QCOM_FALKOR_V1))
		return SPECTRE_VULNERABLE;

	install_bp_hardening_cb(qcom_link_stack_sanitisation);
	return SPECTRE_MITIGATED;
}

void spectre_v2_enable_mitigation(const struct arm64_cpu_capabilities *__unused)
{
	enum mitigation_state state;

	WARN_ON(preemptible());

	state = spectre_v2_get_cpu_hw_mitigation_state();
	if (state == SPECTRE_VULNERABLE)
		state = spectre_v2_enable_fw_mitigation();
	if (state == SPECTRE_VULNERABLE)
		state = spectre_v2_enable_sw_mitigation();

	update_mitigation_state(&spectre_v2_state, state);
}
