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
#include <linux/nospec.h>
#include <linux/prctl.h>
#include <linux/sched/task_stack.h>

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

/*
 * Spectre v4.
 *
 * If you thought Spectre v2 was nasty, wait until you see this mess. A CPU is
 * either:
 *
 * - Mitigated in hardware and listed in our "safe list".
 * - Mitigated in hardware via PSTATE.SSBS.
 * - Mitigated in software by firmware (sometimes referred to as SSBD).
 *
 * Wait, that doesn't sound so bad, does it? Keep reading...
 *
 * A major source of headaches is that the software mitigation is enabled both
 * on a per-task basis, but can also be forced on for the kernel, necessitating
 * both context-switch *and* entry/exit hooks. To make it even worse, some CPUs
 * allow EL0 to toggle SSBS directly, which can end up with the prctl() state
 * being stale when re-entering the kernel. The usual big.LITTLE caveats apply,
 * so you can have systems that have both firmware and SSBS mitigations. This
 * means we actually have to reject late onlining of CPUs with mitigations if
 * all of the currently onlined CPUs are safelisted, as the mitigation tends to
 * be opt-in for userspace. Yes, really, the cure is worse than the disease.
 *
 * The only good part is that if the firmware mitigation is present, then it is
 * present for all CPUs, meaning we don't have to worry about late onlining of a
 * vulnerable CPU if one of the boot CPUs is using the firmware mitigation.
 *
 * Give me a VAX-11/780 any day of the week...
 */
static enum mitigation_state spectre_v4_state;

/* This is the per-cpu state tracking whether we need to talk to firmware */
DEFINE_PER_CPU_READ_MOSTLY(u64, arm64_ssbd_callback_required);

enum spectre_v4_policy {
	SPECTRE_V4_POLICY_MITIGATION_DYNAMIC,
	SPECTRE_V4_POLICY_MITIGATION_ENABLED,
	SPECTRE_V4_POLICY_MITIGATION_DISABLED,
};

static enum spectre_v4_policy __read_mostly __spectre_v4_policy;

static const struct spectre_v4_param {
	const char		*str;
	enum spectre_v4_policy	policy;
} spectre_v4_params[] = {
	{ "force-on",	SPECTRE_V4_POLICY_MITIGATION_ENABLED, },
	{ "force-off",	SPECTRE_V4_POLICY_MITIGATION_DISABLED, },
	{ "kernel",	SPECTRE_V4_POLICY_MITIGATION_DYNAMIC, },
};
static int __init parse_spectre_v4_param(char *str)
{
	int i;

	if (!str || !str[0])
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(spectre_v4_params); i++) {
		const struct spectre_v4_param *param = &spectre_v4_params[i];

		if (strncmp(str, param->str, strlen(param->str)))
			continue;

		__spectre_v4_policy = param->policy;
		return 0;
	}

	return -EINVAL;
}
early_param("ssbd", parse_spectre_v4_param);

/*
 * Because this was all written in a rush by people working in different silos,
 * we've ended up with multiple command line options to control the same thing.
 * Wrap these up in some helpers, which prefer disabling the mitigation if faced
 * with contradictory parameters. The mitigation is always either "off",
 * "dynamic" or "on".
 */
static bool spectre_v4_mitigations_off(void)
{
	bool ret = cpu_mitigations_off() ||
		   __spectre_v4_policy == SPECTRE_V4_POLICY_MITIGATION_DISABLED;

	if (ret)
		pr_info_once("spectre-v4 mitigation disabled by command-line option\n");

	return ret;
}

/* Do we need to toggle the mitigation state on entry to/exit from the kernel? */
static bool spectre_v4_mitigations_dynamic(void)
{
	return !spectre_v4_mitigations_off() &&
	       __spectre_v4_policy == SPECTRE_V4_POLICY_MITIGATION_DYNAMIC;
}

static bool spectre_v4_mitigations_on(void)
{
	return !spectre_v4_mitigations_off() &&
	       __spectre_v4_policy == SPECTRE_V4_POLICY_MITIGATION_ENABLED;
}

ssize_t cpu_show_spec_store_bypass(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	switch (spectre_v4_state) {
	case SPECTRE_UNAFFECTED:
		return sprintf(buf, "Not affected\n");
	case SPECTRE_MITIGATED:
		return sprintf(buf, "Mitigation: Speculative Store Bypass disabled via prctl\n");
	case SPECTRE_VULNERABLE:
		fallthrough;
	default:
		return sprintf(buf, "Vulnerable\n");
	}
}

enum mitigation_state arm64_get_spectre_v4_state(void)
{
	return spectre_v4_state;
}

static enum mitigation_state spectre_v4_get_cpu_hw_mitigation_state(void)
{
	static const struct midr_range spectre_v4_safe_list[] = {
		MIDR_ALL_VERSIONS(MIDR_CORTEX_A35),
		MIDR_ALL_VERSIONS(MIDR_CORTEX_A53),
		MIDR_ALL_VERSIONS(MIDR_CORTEX_A55),
		MIDR_ALL_VERSIONS(MIDR_BRAHMA_B53),
		MIDR_ALL_VERSIONS(MIDR_QCOM_KRYO_3XX_SILVER),
		MIDR_ALL_VERSIONS(MIDR_QCOM_KRYO_4XX_SILVER),
		{ /* sentinel */ },
	};

	if (is_midr_in_range_list(read_cpuid_id(), spectre_v4_safe_list))
		return SPECTRE_UNAFFECTED;

	/* CPU features are detected first */
	if (this_cpu_has_cap(ARM64_SSBS))
		return SPECTRE_MITIGATED;

	return SPECTRE_VULNERABLE;
}

static enum mitigation_state spectre_v4_get_cpu_fw_mitigation_state(void)
{
	int ret;
	struct arm_smccc_res res;

	arm_smccc_1_1_invoke(ARM_SMCCC_ARCH_FEATURES_FUNC_ID,
			     ARM_SMCCC_ARCH_WORKAROUND_2, &res);

	ret = res.a0;
	switch (ret) {
	case SMCCC_RET_SUCCESS:
		return SPECTRE_MITIGATED;
	case SMCCC_ARCH_WORKAROUND_RET_UNAFFECTED:
		fallthrough;
	case SMCCC_RET_NOT_REQUIRED:
		return SPECTRE_UNAFFECTED;
	default:
		fallthrough;
	case SMCCC_RET_NOT_SUPPORTED:
		return SPECTRE_VULNERABLE;
	}
}

bool has_spectre_v4(const struct arm64_cpu_capabilities *cap, int scope)
{
	enum mitigation_state state;

	WARN_ON(scope != SCOPE_LOCAL_CPU || preemptible());

	state = spectre_v4_get_cpu_hw_mitigation_state();
	if (state == SPECTRE_VULNERABLE)
		state = spectre_v4_get_cpu_fw_mitigation_state();

	return state != SPECTRE_UNAFFECTED;
}

static int ssbs_emulation_handler(struct pt_regs *regs, u32 instr)
{
	if (user_mode(regs))
		return 1;

	if (instr & BIT(PSTATE_Imm_shift))
		regs->pstate |= PSR_SSBS_BIT;
	else
		regs->pstate &= ~PSR_SSBS_BIT;

	arm64_skip_faulting_instruction(regs, 4);
	return 0;
}

static struct undef_hook ssbs_emulation_hook = {
	.instr_mask	= ~(1U << PSTATE_Imm_shift),
	.instr_val	= 0xd500401f | PSTATE_SSBS,
	.fn		= ssbs_emulation_handler,
};

static enum mitigation_state spectre_v4_enable_hw_mitigation(void)
{
	static bool undef_hook_registered = false;
	static DEFINE_RAW_SPINLOCK(hook_lock);
	enum mitigation_state state;

	/*
	 * If the system is mitigated but this CPU doesn't have SSBS, then
	 * we must be on the safelist and there's nothing more to do.
	 */
	state = spectre_v4_get_cpu_hw_mitigation_state();
	if (state != SPECTRE_MITIGATED || !this_cpu_has_cap(ARM64_SSBS))
		return state;

	raw_spin_lock(&hook_lock);
	if (!undef_hook_registered) {
		register_undef_hook(&ssbs_emulation_hook);
		undef_hook_registered = true;
	}
	raw_spin_unlock(&hook_lock);

	if (spectre_v4_mitigations_off()) {
		sysreg_clear_set(sctlr_el1, 0, SCTLR_ELx_DSSBS);
		asm volatile(SET_PSTATE_SSBS(1));
		return SPECTRE_VULNERABLE;
	}

	/* SCTLR_EL1.DSSBS was initialised to 0 during boot */
	asm volatile(SET_PSTATE_SSBS(0));
	return SPECTRE_MITIGATED;
}

/*
 * Patch a branch over the Spectre-v4 mitigation code with a NOP so that
 * we fallthrough and check whether firmware needs to be called on this CPU.
 */
void __init spectre_v4_patch_fw_mitigation_enable(struct alt_instr *alt,
						  __le32 *origptr,
						  __le32 *updptr, int nr_inst)
{
	BUG_ON(nr_inst != 1); /* Branch -> NOP */

	if (spectre_v4_mitigations_off())
		return;

	if (cpus_have_final_cap(ARM64_SSBS))
		return;

	if (spectre_v4_mitigations_dynamic())
		*updptr = cpu_to_le32(aarch64_insn_gen_nop());
}

/*
 * Patch a NOP in the Spectre-v4 mitigation code with an SMC/HVC instruction
 * to call into firmware to adjust the mitigation state.
 */
void __init spectre_v4_patch_fw_mitigation_conduit(struct alt_instr *alt,
						   __le32 *origptr,
						   __le32 *updptr, int nr_inst)
{
	u32 insn;

	BUG_ON(nr_inst != 1); /* NOP -> HVC/SMC */

	switch (arm_smccc_1_1_get_conduit()) {
	case SMCCC_CONDUIT_HVC:
		insn = aarch64_insn_get_hvc_value();
		break;
	case SMCCC_CONDUIT_SMC:
		insn = aarch64_insn_get_smc_value();
		break;
	default:
		return;
	}

	*updptr = cpu_to_le32(insn);
}

static enum mitigation_state spectre_v4_enable_fw_mitigation(void)
{
	enum mitigation_state state;

	state = spectre_v4_get_cpu_fw_mitigation_state();
	if (state != SPECTRE_MITIGATED)
		return state;

	if (spectre_v4_mitigations_off()) {
		arm_smccc_1_1_invoke(ARM_SMCCC_ARCH_WORKAROUND_2, false, NULL);
		return SPECTRE_VULNERABLE;
	}

	arm_smccc_1_1_invoke(ARM_SMCCC_ARCH_WORKAROUND_2, true, NULL);

	if (spectre_v4_mitigations_dynamic())
		__this_cpu_write(arm64_ssbd_callback_required, 1);

	return SPECTRE_MITIGATED;
}

void spectre_v4_enable_mitigation(const struct arm64_cpu_capabilities *__unused)
{
	enum mitigation_state state;

	WARN_ON(preemptible());

	state = spectre_v4_enable_hw_mitigation();
	if (state == SPECTRE_VULNERABLE)
		state = spectre_v4_enable_fw_mitigation();

	update_mitigation_state(&spectre_v4_state, state);
}

static void __update_pstate_ssbs(struct pt_regs *regs, bool state)
{
	u64 bit = compat_user_mode(regs) ? PSR_AA32_SSBS_BIT : PSR_SSBS_BIT;

	if (state)
		regs->pstate |= bit;
	else
		regs->pstate &= ~bit;
}

void spectre_v4_enable_task_mitigation(struct task_struct *tsk)
{
	struct pt_regs *regs = task_pt_regs(tsk);
	bool ssbs = false, kthread = tsk->flags & PF_KTHREAD;

	if (spectre_v4_mitigations_off())
		ssbs = true;
	else if (spectre_v4_mitigations_dynamic() && !kthread)
		ssbs = !test_tsk_thread_flag(tsk, TIF_SSBD);

	__update_pstate_ssbs(regs, ssbs);
}

/*
 * The Spectre-v4 mitigation can be controlled via a prctl() from userspace.
 * This is interesting because the "speculation disabled" behaviour can be
 * configured so that it is preserved across exec(), which means that the
 * prctl() may be necessary even when PSTATE.SSBS can be toggled directly
 * from userspace.
 */
static int ssbd_prctl_set(struct task_struct *task, unsigned long ctrl)
{
	switch (ctrl) {
	case PR_SPEC_ENABLE:
		/* Enable speculation: disable mitigation */
		/*
		 * Force disabled speculation prevents it from being
		 * re-enabled.
		 */
		if (task_spec_ssb_force_disable(task))
			return -EPERM;

		/*
		 * If the mitigation is forced on, then speculation is forced
		 * off and we again prevent it from being re-enabled.
		 */
		if (spectre_v4_mitigations_on())
			return -EPERM;

		task_clear_spec_ssb_disable(task);
		clear_tsk_thread_flag(task, TIF_SSBD);
		break;
	case PR_SPEC_FORCE_DISABLE:
		/* Force disable speculation: force enable mitigation */
		/*
		 * If the mitigation is forced off, then speculation is forced
		 * on and we prevent it from being disabled.
		 */
		if (spectre_v4_mitigations_off())
			return -EPERM;

		task_set_spec_ssb_force_disable(task);
		fallthrough;
	case PR_SPEC_DISABLE:
		/* Disable speculation: enable mitigation */
		/* Same as PR_SPEC_FORCE_DISABLE */
		if (spectre_v4_mitigations_off())
			return -EPERM;

		task_set_spec_ssb_disable(task);
		set_tsk_thread_flag(task, TIF_SSBD);
		break;
	default:
		return -ERANGE;
	}

	spectre_v4_enable_task_mitigation(task);
	return 0;
}

int arch_prctl_spec_ctrl_set(struct task_struct *task, unsigned long which,
			     unsigned long ctrl)
{
	switch (which) {
	case PR_SPEC_STORE_BYPASS:
		return ssbd_prctl_set(task, ctrl);
	default:
		return -ENODEV;
	}
}

static int ssbd_prctl_get(struct task_struct *task)
{
	switch (spectre_v4_state) {
	case SPECTRE_UNAFFECTED:
		return PR_SPEC_NOT_AFFECTED;
	case SPECTRE_MITIGATED:
		if (spectre_v4_mitigations_on())
			return PR_SPEC_NOT_AFFECTED;

		if (spectre_v4_mitigations_dynamic())
			break;

		/* Mitigations are disabled, so we're vulnerable. */
		fallthrough;
	case SPECTRE_VULNERABLE:
		fallthrough;
	default:
		return PR_SPEC_ENABLE;
	}

	/* Check the mitigation state for this task */
	if (task_spec_ssb_force_disable(task))
		return PR_SPEC_PRCTL | PR_SPEC_FORCE_DISABLE;

	if (task_spec_ssb_disable(task))
		return PR_SPEC_PRCTL | PR_SPEC_DISABLE;

	return PR_SPEC_PRCTL | PR_SPEC_ENABLE;
}

int arch_prctl_spec_ctrl_get(struct task_struct *task, unsigned long which)
{
	switch (which) {
	case PR_SPEC_STORE_BYPASS:
		return ssbd_prctl_get(task);
	default:
		return -ENODEV;
	}
}
