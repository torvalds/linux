// SPDX-License-Identifier: GPL-2.0-only
/*
 * Handle detection, reporting and mitigation of Spectre v1, v2, v3a and v4, as
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
#include <linux/bpf.h>
#include <linux/cpu.h>
#include <linux/device.h>
#include <linux/nospec.h>
#include <linux/prctl.h>
#include <linux/sched/task_stack.h>

#include <asm/debug-monitors.h>
#include <asm/insn.h>
#include <asm/spectre.h>
#include <asm/traps.h>
#include <asm/vectors.h>
#include <asm/virt.h>

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
 * - Mitigated in software by a CPU-specific dance in the kernel and a
 *   firmware call at EL2.
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

static const char *get_bhb_affected_string(enum mitigation_state bhb_state)
{
	switch (bhb_state) {
	case SPECTRE_UNAFFECTED:
		return "";
	default:
	case SPECTRE_VULNERABLE:
		return ", but not BHB";
	case SPECTRE_MITIGATED:
		return ", BHB";
	}
}

static bool _unprivileged_ebpf_enabled(void)
{
#ifdef CONFIG_BPF_SYSCALL
	return !sysctl_unprivileged_bpf_disabled;
#else
	return false;
#endif
}

ssize_t cpu_show_spectre_v2(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	enum mitigation_state bhb_state = arm64_get_spectre_bhb_state();
	const char *bhb_str = get_bhb_affected_string(bhb_state);
	const char *v2_str = "Branch predictor hardening";

	switch (spectre_v2_state) {
	case SPECTRE_UNAFFECTED:
		if (bhb_state == SPECTRE_UNAFFECTED)
			return sprintf(buf, "Not affected\n");

		/*
		 * Platforms affected by Spectre-BHB can't report
		 * "Not affected" for Spectre-v2.
		 */
		v2_str = "CSV2";
		fallthrough;
	case SPECTRE_MITIGATED:
		if (bhb_state == SPECTRE_MITIGATED && _unprivileged_ebpf_enabled())
			return sprintf(buf, "Vulnerable: Unprivileged eBPF enabled\n");

		return sprintf(buf, "Mitigation: %s%s\n", v2_str, bhb_str);
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
		MIDR_ALL_VERSIONS(MIDR_QCOM_KRYO_2XX_SILVER),
		MIDR_ALL_VERSIONS(MIDR_QCOM_KRYO_3XX_SILVER),
		MIDR_ALL_VERSIONS(MIDR_QCOM_KRYO_4XX_SILVER),
		{ /* sentinel */ }
	};

	/* If the CPU has CSV2 set, we're safe */
	pfr0 = read_cpuid(ID_AA64PFR0_EL1);
	if (cpuid_feature_extract_unsigned_field(pfr0, ID_AA64PFR0_EL1_CSV2_SHIFT))
		return SPECTRE_UNAFFECTED;

	/* Alternatively, we have a list of unaffected CPUs */
	if (is_midr_in_range_list(read_cpuid_id(), spectre_v2_safe_list))
		return SPECTRE_UNAFFECTED;

	return SPECTRE_VULNERABLE;
}

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

enum mitigation_state arm64_get_spectre_v2_state(void)
{
	return spectre_v2_state;
}

DEFINE_PER_CPU_READ_MOSTLY(struct bp_hardening_data, bp_hardening_data);

static void install_bp_hardening_cb(bp_hardening_cb_t fn)
{
	__this_cpu_write(bp_hardening_data.fn, fn);

	/*
	 * Vinz Clortho takes the hyp_vecs start/end "keys" at
	 * the door when we're a guest. Skip the hyp-vectors work.
	 */
	if (!is_hyp_mode_available())
		return;

	__this_cpu_write(bp_hardening_data.slot, HYP_VECTOR_SPECTRE_DIRECT);
}

/* Called during entry so must be noinstr */
static noinstr void call_smc_arch_workaround_1(void)
{
	arm_smccc_1_1_smc(ARM_SMCCC_ARCH_WORKAROUND_1, NULL);
}

/* Called during entry so must be noinstr */
static noinstr void call_hvc_arch_workaround_1(void)
{
	arm_smccc_1_1_hvc(ARM_SMCCC_ARCH_WORKAROUND_1, NULL);
}

/* Called during entry so must be noinstr */
static noinstr void qcom_link_stack_sanitisation(void)
{
	u64 tmp;

	asm volatile("mov	%0, x30		\n"
		     ".rept	16		\n"
		     "bl	. + 4		\n"
		     ".endr			\n"
		     "mov	x30, %0		\n"
		     : "=&r" (tmp));
}

static bp_hardening_cb_t spectre_v2_get_sw_mitigation_cb(void)
{
	u32 midr = read_cpuid_id();
	if (((midr & MIDR_CPU_MODEL_MASK) != MIDR_QCOM_FALKOR) &&
	    ((midr & MIDR_CPU_MODEL_MASK) != MIDR_QCOM_FALKOR_V1))
		return NULL;

	return qcom_link_stack_sanitisation;
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

	/*
	 * Prefer a CPU-specific workaround if it exists. Note that we
	 * still rely on firmware for the mitigation at EL2.
	 */
	cb = spectre_v2_get_sw_mitigation_cb() ?: cb;
	install_bp_hardening_cb(cb);
	return SPECTRE_MITIGATED;
}

void spectre_v2_enable_mitigation(const struct arm64_cpu_capabilities *__unused)
{
	enum mitigation_state state;

	WARN_ON(preemptible());

	state = spectre_v2_get_cpu_hw_mitigation_state();
	if (state == SPECTRE_VULNERABLE)
		state = spectre_v2_enable_fw_mitigation();

	update_mitigation_state(&spectre_v2_state, state);
}

/*
 * Spectre-v3a.
 *
 * Phew, there's not an awful lot to do here! We just instruct EL2 to use
 * an indirect trampoline for the hyp vectors so that guests can't read
 * VBAR_EL2 to defeat randomisation of the hypervisor VA layout.
 */
bool has_spectre_v3a(const struct arm64_cpu_capabilities *entry, int scope)
{
	static const struct midr_range spectre_v3a_unsafe_list[] = {
		MIDR_ALL_VERSIONS(MIDR_CORTEX_A57),
		MIDR_ALL_VERSIONS(MIDR_CORTEX_A72),
		{},
	};

	WARN_ON(scope != SCOPE_LOCAL_CPU || preemptible());
	return is_midr_in_range_list(read_cpuid_id(), spectre_v3a_unsafe_list);
}

void spectre_v3a_enable_mitigation(const struct arm64_cpu_capabilities *__unused)
{
	struct bp_hardening_data *data = this_cpu_ptr(&bp_hardening_data);

	if (this_cpu_has_cap(ARM64_SPECTRE_V3A))
		data->slot += HYP_VECTOR_INDIRECT;
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

bool try_emulate_el1_ssbs(struct pt_regs *regs, u32 instr)
{
	const u32 instr_mask = ~(1U << PSTATE_Imm_shift);
	const u32 instr_val = 0xd500401f | PSTATE_SSBS;

	if ((instr & instr_mask) != instr_val)
		return false;

	if (instr & BIT(PSTATE_Imm_shift))
		regs->pstate |= PSR_SSBS_BIT;
	else
		regs->pstate &= ~PSR_SSBS_BIT;

	arm64_skip_faulting_instruction(regs, 4);
	return true;
}

static enum mitigation_state spectre_v4_enable_hw_mitigation(void)
{
	enum mitigation_state state;

	/*
	 * If the system is mitigated but this CPU doesn't have SSBS, then
	 * we must be on the safelist and there's nothing more to do.
	 */
	state = spectre_v4_get_cpu_hw_mitigation_state();
	if (state != SPECTRE_MITIGATED || !this_cpu_has_cap(ARM64_SSBS))
		return state;

	if (spectre_v4_mitigations_off()) {
		sysreg_clear_set(sctlr_el1, 0, SCTLR_ELx_DSSBS);
		set_pstate_ssbs(1);
		return SPECTRE_VULNERABLE;
	}

	/* SCTLR_EL1.DSSBS was initialised to 0 during boot */
	set_pstate_ssbs(0);

	/*
	 * SSBS is self-synchronizing and is intended to affect subsequent
	 * speculative instructions, but some CPUs can speculate with a stale
	 * value of SSBS.
	 *
	 * Mitigate this with an unconditional speculation barrier, as CPUs
	 * could mis-speculate branches and bypass a conditional barrier.
	 */
	if (IS_ENABLED(CONFIG_ARM64_ERRATUM_3194386))
		spec_bar();

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

	if (cpus_have_cap(ARM64_SSBS))
		return;

	if (spectre_v4_mitigations_dynamic())
		*updptr = cpu_to_le32(aarch64_insn_gen_nop());
}

/*
 * Patch a NOP in the Spectre-v4 mitigation code with an SMC/HVC instruction
 * to call into firmware to adjust the mitigation state.
 */
void __init smccc_patch_fw_mitigation_conduit(struct alt_instr *alt,
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
static void ssbd_prctl_enable_mitigation(struct task_struct *task)
{
	task_clear_spec_ssb_noexec(task);
	task_set_spec_ssb_disable(task);
	set_tsk_thread_flag(task, TIF_SSBD);
}

static void ssbd_prctl_disable_mitigation(struct task_struct *task)
{
	task_clear_spec_ssb_noexec(task);
	task_clear_spec_ssb_disable(task);
	clear_tsk_thread_flag(task, TIF_SSBD);
}

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

		ssbd_prctl_disable_mitigation(task);
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

		ssbd_prctl_enable_mitigation(task);
		break;
	case PR_SPEC_DISABLE_NOEXEC:
		/* Disable speculation until execve(): enable mitigation */
		/*
		 * If the mitigation state is forced one way or the other, then
		 * we must fail now before we try to toggle it on execve().
		 */
		if (task_spec_ssb_force_disable(task) ||
		    spectre_v4_mitigations_off() ||
		    spectre_v4_mitigations_on()) {
			return -EPERM;
		}

		ssbd_prctl_enable_mitigation(task);
		task_set_spec_ssb_noexec(task);
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

	if (task_spec_ssb_noexec(task))
		return PR_SPEC_PRCTL | PR_SPEC_DISABLE_NOEXEC;

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

/*
 * Spectre BHB.
 *
 * A CPU is either:
 * - Mitigated by a branchy loop a CPU specific number of times, and listed
 *   in our "loop mitigated list".
 * - Mitigated in software by the firmware Spectre v2 call.
 * - Has the ClearBHB instruction to perform the mitigation.
 * - Has the 'Exception Clears Branch History Buffer' (ECBHB) feature, so no
 *   software mitigation in the vectors is needed.
 * - Has CSV2.3, so is unaffected.
 */
static enum mitigation_state spectre_bhb_state;

enum mitigation_state arm64_get_spectre_bhb_state(void)
{
	return spectre_bhb_state;
}

enum bhb_mitigation_bits {
	BHB_LOOP,
	BHB_FW,
	BHB_HW,
	BHB_INSN,
};
static unsigned long system_bhb_mitigations;

/*
 * This must be called with SCOPE_LOCAL_CPU for each type of CPU, before any
 * SCOPE_SYSTEM call will give the right answer.
 */
u8 spectre_bhb_loop_affected(int scope)
{
	u8 k = 0;
	static u8 max_bhb_k;

	if (scope == SCOPE_LOCAL_CPU) {
		static const struct midr_range spectre_bhb_k32_list[] = {
			MIDR_ALL_VERSIONS(MIDR_CORTEX_A78),
			MIDR_ALL_VERSIONS(MIDR_CORTEX_A78AE),
			MIDR_ALL_VERSIONS(MIDR_CORTEX_A78C),
			MIDR_ALL_VERSIONS(MIDR_CORTEX_X1),
			MIDR_ALL_VERSIONS(MIDR_CORTEX_A710),
			MIDR_ALL_VERSIONS(MIDR_CORTEX_X2),
			MIDR_ALL_VERSIONS(MIDR_NEOVERSE_N2),
			MIDR_ALL_VERSIONS(MIDR_NEOVERSE_V1),
			{},
		};
		static const struct midr_range spectre_bhb_k24_list[] = {
			MIDR_ALL_VERSIONS(MIDR_CORTEX_A76),
			MIDR_ALL_VERSIONS(MIDR_CORTEX_A77),
			MIDR_ALL_VERSIONS(MIDR_NEOVERSE_N1),
			{},
		};
		static const struct midr_range spectre_bhb_k11_list[] = {
			MIDR_ALL_VERSIONS(MIDR_AMPERE1),
			{},
		};
		static const struct midr_range spectre_bhb_k8_list[] = {
			MIDR_ALL_VERSIONS(MIDR_CORTEX_A72),
			MIDR_ALL_VERSIONS(MIDR_CORTEX_A57),
			{},
		};

		if (is_midr_in_range_list(read_cpuid_id(), spectre_bhb_k32_list))
			k = 32;
		else if (is_midr_in_range_list(read_cpuid_id(), spectre_bhb_k24_list))
			k = 24;
		else if (is_midr_in_range_list(read_cpuid_id(), spectre_bhb_k11_list))
			k = 11;
		else if (is_midr_in_range_list(read_cpuid_id(), spectre_bhb_k8_list))
			k =  8;

		max_bhb_k = max(max_bhb_k, k);
	} else {
		k = max_bhb_k;
	}

	return k;
}

static enum mitigation_state spectre_bhb_get_cpu_fw_mitigation_state(void)
{
	int ret;
	struct arm_smccc_res res;

	arm_smccc_1_1_invoke(ARM_SMCCC_ARCH_FEATURES_FUNC_ID,
			     ARM_SMCCC_ARCH_WORKAROUND_3, &res);

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

static bool is_spectre_bhb_fw_affected(int scope)
{
	static bool system_affected;
	enum mitigation_state fw_state;
	bool has_smccc = arm_smccc_1_1_get_conduit() != SMCCC_CONDUIT_NONE;
	static const struct midr_range spectre_bhb_firmware_mitigated_list[] = {
		MIDR_ALL_VERSIONS(MIDR_CORTEX_A73),
		MIDR_ALL_VERSIONS(MIDR_CORTEX_A75),
		{},
	};
	bool cpu_in_list = is_midr_in_range_list(read_cpuid_id(),
					 spectre_bhb_firmware_mitigated_list);

	if (scope != SCOPE_LOCAL_CPU)
		return system_affected;

	fw_state = spectre_bhb_get_cpu_fw_mitigation_state();
	if (cpu_in_list || (has_smccc && fw_state == SPECTRE_MITIGATED)) {
		system_affected = true;
		return true;
	}

	return false;
}

static bool supports_ecbhb(int scope)
{
	u64 mmfr1;

	if (scope == SCOPE_LOCAL_CPU)
		mmfr1 = read_sysreg_s(SYS_ID_AA64MMFR1_EL1);
	else
		mmfr1 = read_sanitised_ftr_reg(SYS_ID_AA64MMFR1_EL1);

	return cpuid_feature_extract_unsigned_field(mmfr1,
						    ID_AA64MMFR1_EL1_ECBHB_SHIFT);
}

bool is_spectre_bhb_affected(const struct arm64_cpu_capabilities *entry,
			     int scope)
{
	WARN_ON(scope != SCOPE_LOCAL_CPU || preemptible());

	if (supports_csv2p3(scope))
		return false;

	if (supports_clearbhb(scope))
		return true;

	if (spectre_bhb_loop_affected(scope))
		return true;

	if (is_spectre_bhb_fw_affected(scope))
		return true;

	return false;
}

static void this_cpu_set_vectors(enum arm64_bp_harden_el1_vectors slot)
{
	const char *v = arm64_get_bp_hardening_vector(slot);

	__this_cpu_write(this_cpu_vector, v);

	/*
	 * When KPTI is in use, the vectors are switched when exiting to
	 * user-space.
	 */
	if (cpus_have_cap(ARM64_UNMAP_KERNEL_AT_EL0))
		return;

	write_sysreg(v, vbar_el1);
	isb();
}

static bool __read_mostly __nospectre_bhb;
static int __init parse_spectre_bhb_param(char *str)
{
	__nospectre_bhb = true;
	return 0;
}
early_param("nospectre_bhb", parse_spectre_bhb_param);

void spectre_bhb_enable_mitigation(const struct arm64_cpu_capabilities *entry)
{
	bp_hardening_cb_t cpu_cb;
	enum mitigation_state fw_state, state = SPECTRE_VULNERABLE;
	struct bp_hardening_data *data = this_cpu_ptr(&bp_hardening_data);

	if (!is_spectre_bhb_affected(entry, SCOPE_LOCAL_CPU))
		return;

	if (arm64_get_spectre_v2_state() == SPECTRE_VULNERABLE) {
		/* No point mitigating Spectre-BHB alone. */
	} else if (!IS_ENABLED(CONFIG_MITIGATE_SPECTRE_BRANCH_HISTORY)) {
		pr_info_once("spectre-bhb mitigation disabled by compile time option\n");
	} else if (cpu_mitigations_off() || __nospectre_bhb) {
		pr_info_once("spectre-bhb mitigation disabled by command line option\n");
	} else if (supports_ecbhb(SCOPE_LOCAL_CPU)) {
		state = SPECTRE_MITIGATED;
		set_bit(BHB_HW, &system_bhb_mitigations);
	} else if (supports_clearbhb(SCOPE_LOCAL_CPU)) {
		/*
		 * Ensure KVM uses the indirect vector which will have ClearBHB
		 * added.
		 */
		if (!data->slot)
			data->slot = HYP_VECTOR_INDIRECT;

		this_cpu_set_vectors(EL1_VECTOR_BHB_CLEAR_INSN);
		state = SPECTRE_MITIGATED;
		set_bit(BHB_INSN, &system_bhb_mitigations);
	} else if (spectre_bhb_loop_affected(SCOPE_LOCAL_CPU)) {
		/*
		 * Ensure KVM uses the indirect vector which will have the
		 * branchy-loop added. A57/A72-r0 will already have selected
		 * the spectre-indirect vector, which is sufficient for BHB
		 * too.
		 */
		if (!data->slot)
			data->slot = HYP_VECTOR_INDIRECT;

		this_cpu_set_vectors(EL1_VECTOR_BHB_LOOP);
		state = SPECTRE_MITIGATED;
		set_bit(BHB_LOOP, &system_bhb_mitigations);
	} else if (is_spectre_bhb_fw_affected(SCOPE_LOCAL_CPU)) {
		fw_state = spectre_bhb_get_cpu_fw_mitigation_state();
		if (fw_state == SPECTRE_MITIGATED) {
			/*
			 * Ensure KVM uses one of the spectre bp_hardening
			 * vectors. The indirect vector doesn't include the EL3
			 * call, so needs upgrading to
			 * HYP_VECTOR_SPECTRE_INDIRECT.
			 */
			if (!data->slot || data->slot == HYP_VECTOR_INDIRECT)
				data->slot += 1;

			this_cpu_set_vectors(EL1_VECTOR_BHB_FW);

			/*
			 * The WA3 call in the vectors supersedes the WA1 call
			 * made during context-switch. Uninstall any firmware
			 * bp_hardening callback.
			 */
			cpu_cb = spectre_v2_get_sw_mitigation_cb();
			if (__this_cpu_read(bp_hardening_data.fn) != cpu_cb)
				__this_cpu_write(bp_hardening_data.fn, NULL);

			state = SPECTRE_MITIGATED;
			set_bit(BHB_FW, &system_bhb_mitigations);
		}
	}

	update_mitigation_state(&spectre_bhb_state, state);
}

/* Patched to NOP when enabled */
void noinstr spectre_bhb_patch_loop_mitigation_enable(struct alt_instr *alt,
						     __le32 *origptr,
						      __le32 *updptr, int nr_inst)
{
	BUG_ON(nr_inst != 1);

	if (test_bit(BHB_LOOP, &system_bhb_mitigations))
		*updptr++ = cpu_to_le32(aarch64_insn_gen_nop());
}

/* Patched to NOP when enabled */
void noinstr spectre_bhb_patch_fw_mitigation_enabled(struct alt_instr *alt,
						   __le32 *origptr,
						   __le32 *updptr, int nr_inst)
{
	BUG_ON(nr_inst != 1);

	if (test_bit(BHB_FW, &system_bhb_mitigations))
		*updptr++ = cpu_to_le32(aarch64_insn_gen_nop());
}

/* Patched to correct the immediate */
void noinstr spectre_bhb_patch_loop_iter(struct alt_instr *alt,
				   __le32 *origptr, __le32 *updptr, int nr_inst)
{
	u8 rd;
	u32 insn;
	u16 loop_count = spectre_bhb_loop_affected(SCOPE_SYSTEM);

	BUG_ON(nr_inst != 1); /* MOV -> MOV */

	if (!IS_ENABLED(CONFIG_MITIGATE_SPECTRE_BRANCH_HISTORY))
		return;

	insn = le32_to_cpu(*origptr);
	rd = aarch64_insn_decode_register(AARCH64_INSN_REGTYPE_RD, insn);
	insn = aarch64_insn_gen_movewide(rd, loop_count, 0,
					 AARCH64_INSN_VARIANT_64BIT,
					 AARCH64_INSN_MOVEWIDE_ZERO);
	*updptr++ = cpu_to_le32(insn);
}

/* Patched to mov WA3 when supported */
void noinstr spectre_bhb_patch_wa3(struct alt_instr *alt,
				   __le32 *origptr, __le32 *updptr, int nr_inst)
{
	u8 rd;
	u32 insn;

	BUG_ON(nr_inst != 1); /* MOV -> MOV */

	if (!IS_ENABLED(CONFIG_MITIGATE_SPECTRE_BRANCH_HISTORY) ||
	    !test_bit(BHB_FW, &system_bhb_mitigations))
		return;

	insn = le32_to_cpu(*origptr);
	rd = aarch64_insn_decode_register(AARCH64_INSN_REGTYPE_RD, insn);

	insn = aarch64_insn_gen_logical_immediate(AARCH64_INSN_LOGIC_ORR,
						  AARCH64_INSN_VARIANT_32BIT,
						  AARCH64_INSN_REG_ZR, rd,
						  ARM_SMCCC_ARCH_WORKAROUND_3);
	if (WARN_ON_ONCE(insn == AARCH64_BREAK_FAULT))
		return;

	*updptr++ = cpu_to_le32(insn);
}

/* Patched to NOP when not supported */
void __init spectre_bhb_patch_clearbhb(struct alt_instr *alt,
				   __le32 *origptr, __le32 *updptr, int nr_inst)
{
	BUG_ON(nr_inst != 2);

	if (test_bit(BHB_INSN, &system_bhb_mitigations))
		return;

	*updptr++ = cpu_to_le32(aarch64_insn_gen_nop());
	*updptr++ = cpu_to_le32(aarch64_insn_gen_nop());
}

#ifdef CONFIG_BPF_SYSCALL
#define EBPF_WARN "Unprivileged eBPF is enabled, data leaks possible via Spectre v2 BHB attacks!\n"
void unpriv_ebpf_notify(int new_state)
{
	if (spectre_v2_state == SPECTRE_VULNERABLE ||
	    spectre_bhb_state != SPECTRE_MITIGATED)
		return;

	if (!new_state)
		pr_err("WARNING: %s", EBPF_WARN);
}
#endif
