// SPDX-License-Identifier: GPL-2.0-only
/*
 * Contains CPU specific errata definitions
 *
 * Copyright (C) 2014 ARM Ltd.
 */

#include <linux/arm-smccc.h>
#include <linux/types.h>
#include <linux/cpu.h>
#include <asm/cpu.h>
#include <asm/cputype.h>
#include <asm/cpufeature.h>
#include <asm/kvm_asm.h>
#include <asm/smp_plat.h>

static bool __maybe_unused
is_affected_midr_range(const struct arm64_cpu_capabilities *entry, int scope)
{
	const struct arm64_midr_revidr *fix;
	u32 midr = read_cpuid_id(), revidr;

	WARN_ON(scope != SCOPE_LOCAL_CPU || preemptible());
	if (!is_midr_in_range(midr, &entry->midr_range))
		return false;

	midr &= MIDR_REVISION_MASK | MIDR_VARIANT_MASK;
	revidr = read_cpuid(REVIDR_EL1);
	for (fix = entry->fixed_revs; fix && fix->revidr_mask; fix++)
		if (midr == fix->midr_rv && (revidr & fix->revidr_mask))
			return false;

	return true;
}

static bool __maybe_unused
is_affected_midr_range_list(const struct arm64_cpu_capabilities *entry,
			    int scope)
{
	WARN_ON(scope != SCOPE_LOCAL_CPU || preemptible());
	return is_midr_in_range_list(read_cpuid_id(), entry->midr_range_list);
}

static bool __maybe_unused
is_kryo_midr(const struct arm64_cpu_capabilities *entry, int scope)
{
	u32 model;

	WARN_ON(scope != SCOPE_LOCAL_CPU || preemptible());

	model = read_cpuid_id();
	model &= MIDR_IMPLEMENTOR_MASK | (0xf00 << MIDR_PARTNUM_SHIFT) |
		 MIDR_ARCHITECTURE_MASK;

	return model == entry->midr_range.model;
}

static bool
has_mismatched_cache_type(const struct arm64_cpu_capabilities *entry,
			  int scope)
{
	u64 mask = arm64_ftr_reg_ctrel0.strict_mask;
	u64 sys = arm64_ftr_reg_ctrel0.sys_val & mask;
	u64 ctr_raw, ctr_real;

	WARN_ON(scope != SCOPE_LOCAL_CPU || preemptible());

	/*
	 * We want to make sure that all the CPUs in the system expose
	 * a consistent CTR_EL0 to make sure that applications behaves
	 * correctly with migration.
	 *
	 * If a CPU has CTR_EL0.IDC but does not advertise it via CTR_EL0 :
	 *
	 * 1) It is safe if the system doesn't support IDC, as CPU anyway
	 *    reports IDC = 0, consistent with the rest.
	 *
	 * 2) If the system has IDC, it is still safe as we trap CTR_EL0
	 *    access on this CPU via the ARM64_HAS_CACHE_IDC capability.
	 *
	 * So, we need to make sure either the raw CTR_EL0 or the effective
	 * CTR_EL0 matches the system's copy to allow a secondary CPU to boot.
	 */
	ctr_raw = read_cpuid_cachetype() & mask;
	ctr_real = read_cpuid_effective_cachetype() & mask;

	return (ctr_real != sys) && (ctr_raw != sys);
}

static void
cpu_enable_trap_ctr_access(const struct arm64_cpu_capabilities *cap)
{
	u64 mask = arm64_ftr_reg_ctrel0.strict_mask;
	bool enable_uct_trap = false;

	/* Trap CTR_EL0 access on this CPU, only if it has a mismatch */
	if ((read_cpuid_cachetype() & mask) !=
	    (arm64_ftr_reg_ctrel0.sys_val & mask))
		enable_uct_trap = true;

	/* ... or if the system is affected by an erratum */
	if (cap->capability == ARM64_WORKAROUND_1542419)
		enable_uct_trap = true;

	if (enable_uct_trap)
		sysreg_clear_set(sctlr_el1, SCTLR_EL1_UCT, 0);
}

atomic_t arm64_el2_vector_last_slot = ATOMIC_INIT(-1);

#include <asm/mmu_context.h>
#include <asm/cacheflush.h>

DEFINE_PER_CPU_READ_MOSTLY(struct bp_hardening_data, bp_hardening_data);

#ifdef CONFIG_RANDOMIZE_BASE
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

#include <linux/arm-smccc.h>

static void __maybe_unused call_smc_arch_workaround_1(void)
{
	arm_smccc_1_1_smc(ARM_SMCCC_ARCH_WORKAROUND_1, NULL);
}

static void call_hvc_arch_workaround_1(void)
{
	arm_smccc_1_1_hvc(ARM_SMCCC_ARCH_WORKAROUND_1, NULL);
}

static void qcom_link_stack_sanitization(void)
{
	u64 tmp;

	asm volatile("mov	%0, x30		\n"
		     ".rept	16		\n"
		     "bl	. + 4		\n"
		     ".endr			\n"
		     "mov	x30, %0		\n"
		     : "=&r" (tmp));
}

static bool __nospectre_v2;
static int __init parse_nospectre_v2(char *str)
{
	__nospectre_v2 = true;
	return 0;
}
early_param("nospectre_v2", parse_nospectre_v2);

/*
 * -1: No workaround
 *  0: No workaround required
 *  1: Workaround installed
 */
static int detect_harden_bp_fw(void)
{
	bp_hardening_cb_t cb;
	struct arm_smccc_res res;
	u32 midr = read_cpuid_id();

	arm_smccc_1_1_invoke(ARM_SMCCC_ARCH_FEATURES_FUNC_ID,
			     ARM_SMCCC_ARCH_WORKAROUND_1, &res);

	switch ((int)res.a0) {
	case 1:
		/* Firmware says we're just fine */
		return 0;
	case 0:
		break;
	default:
		return -1;
	}

	switch (arm_smccc_1_1_get_conduit()) {
	case SMCCC_CONDUIT_HVC:
		cb = call_hvc_arch_workaround_1;
		break;

	case SMCCC_CONDUIT_SMC:
		cb = call_smc_arch_workaround_1;
		break;

	default:
		return -1;
	}

	if (((midr & MIDR_CPU_MODEL_MASK) == MIDR_QCOM_FALKOR) ||
	    ((midr & MIDR_CPU_MODEL_MASK) == MIDR_QCOM_FALKOR_V1))
		cb = qcom_link_stack_sanitization;

	install_bp_hardening_cb(cb);
	return 1;
}

DEFINE_PER_CPU_READ_MOSTLY(u64, arm64_ssbd_callback_required);

int ssbd_state __read_mostly = ARM64_SSBD_KERNEL;
static bool __ssb_safe = true;

static const struct ssbd_options {
	const char	*str;
	int		state;
} ssbd_options[] = {
	{ "force-on",	ARM64_SSBD_FORCE_ENABLE, },
	{ "force-off",	ARM64_SSBD_FORCE_DISABLE, },
	{ "kernel",	ARM64_SSBD_KERNEL, },
};

static int __init ssbd_cfg(char *buf)
{
	int i;

	if (!buf || !buf[0])
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(ssbd_options); i++) {
		int len = strlen(ssbd_options[i].str);

		if (strncmp(buf, ssbd_options[i].str, len))
			continue;

		ssbd_state = ssbd_options[i].state;
		return 0;
	}

	return -EINVAL;
}
early_param("ssbd", ssbd_cfg);

void __init arm64_update_smccc_conduit(struct alt_instr *alt,
				       __le32 *origptr, __le32 *updptr,
				       int nr_inst)
{
	u32 insn;

	BUG_ON(nr_inst != 1);

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

void __init arm64_enable_wa2_handling(struct alt_instr *alt,
				      __le32 *origptr, __le32 *updptr,
				      int nr_inst)
{
	BUG_ON(nr_inst != 1);
	/*
	 * Only allow mitigation on EL1 entry/exit and guest
	 * ARCH_WORKAROUND_2 handling if the SSBD state allows it to
	 * be flipped.
	 */
	if (arm64_get_ssbd_state() == ARM64_SSBD_KERNEL)
		*updptr = cpu_to_le32(aarch64_insn_gen_nop());
}

void arm64_set_ssbd_mitigation(bool state)
{
	int conduit;

	if (this_cpu_has_cap(ARM64_SSBS)) {
		if (state)
			asm volatile(SET_PSTATE_SSBS(0));
		else
			asm volatile(SET_PSTATE_SSBS(1));
		return;
	}

	conduit = arm_smccc_1_1_invoke(ARM_SMCCC_ARCH_WORKAROUND_2, state,
				       NULL);

	WARN_ON_ONCE(conduit == SMCCC_CONDUIT_NONE);
}

static bool has_ssbd_mitigation(const struct arm64_cpu_capabilities *entry,
				    int scope)
{
	struct arm_smccc_res res;
	bool required = true;
	s32 val;
	bool this_cpu_safe = false;
	int conduit;

	WARN_ON(scope != SCOPE_LOCAL_CPU || preemptible());

	if (cpu_mitigations_off())
		ssbd_state = ARM64_SSBD_FORCE_DISABLE;

	/* delay setting __ssb_safe until we get a firmware response */
	if (is_midr_in_range_list(read_cpuid_id(), entry->midr_range_list))
		this_cpu_safe = true;

	if (this_cpu_has_cap(ARM64_SSBS)) {
		if (!this_cpu_safe)
			__ssb_safe = false;
		required = false;
		goto out_printmsg;
	}

	conduit = arm_smccc_1_1_invoke(ARM_SMCCC_ARCH_FEATURES_FUNC_ID,
				       ARM_SMCCC_ARCH_WORKAROUND_2, &res);

	if (conduit == SMCCC_CONDUIT_NONE) {
		ssbd_state = ARM64_SSBD_UNKNOWN;
		if (!this_cpu_safe)
			__ssb_safe = false;
		return false;
	}

	val = (s32)res.a0;

	switch (val) {
	case SMCCC_RET_NOT_SUPPORTED:
		ssbd_state = ARM64_SSBD_UNKNOWN;
		if (!this_cpu_safe)
			__ssb_safe = false;
		return false;

	/* machines with mixed mitigation requirements must not return this */
	case SMCCC_RET_NOT_REQUIRED:
		pr_info_once("%s mitigation not required\n", entry->desc);
		ssbd_state = ARM64_SSBD_MITIGATED;
		return false;

	case SMCCC_RET_SUCCESS:
		__ssb_safe = false;
		required = true;
		break;

	case 1:	/* Mitigation not required on this CPU */
		required = false;
		break;

	default:
		WARN_ON(1);
		if (!this_cpu_safe)
			__ssb_safe = false;
		return false;
	}

	switch (ssbd_state) {
	case ARM64_SSBD_FORCE_DISABLE:
		arm64_set_ssbd_mitigation(false);
		required = false;
		break;

	case ARM64_SSBD_KERNEL:
		if (required) {
			__this_cpu_write(arm64_ssbd_callback_required, 1);
			arm64_set_ssbd_mitigation(true);
		}
		break;

	case ARM64_SSBD_FORCE_ENABLE:
		arm64_set_ssbd_mitigation(true);
		required = true;
		break;

	default:
		WARN_ON(1);
		break;
	}

out_printmsg:
	switch (ssbd_state) {
	case ARM64_SSBD_FORCE_DISABLE:
		pr_info_once("%s disabled from command-line\n", entry->desc);
		break;

	case ARM64_SSBD_FORCE_ENABLE:
		pr_info_once("%s forced from command-line\n", entry->desc);
		break;
	}

	return required;
}

static void cpu_enable_ssbd_mitigation(const struct arm64_cpu_capabilities *cap)
{
	if (ssbd_state != ARM64_SSBD_FORCE_DISABLE)
		cap->matches(cap, SCOPE_LOCAL_CPU);
}

/* known invulnerable cores */
static const struct midr_range arm64_ssb_cpus[] = {
	MIDR_ALL_VERSIONS(MIDR_CORTEX_A35),
	MIDR_ALL_VERSIONS(MIDR_CORTEX_A53),
	MIDR_ALL_VERSIONS(MIDR_CORTEX_A55),
	MIDR_ALL_VERSIONS(MIDR_BRAHMA_B53),
	MIDR_ALL_VERSIONS(MIDR_QCOM_KRYO_3XX_SILVER),
	MIDR_ALL_VERSIONS(MIDR_QCOM_KRYO_4XX_SILVER),
	{},
};

#ifdef CONFIG_ARM64_ERRATUM_1463225
DEFINE_PER_CPU(int, __in_cortex_a76_erratum_1463225_wa);

static bool
has_cortex_a76_erratum_1463225(const struct arm64_cpu_capabilities *entry,
			       int scope)
{
	return is_affected_midr_range_list(entry, scope) && is_kernel_in_hyp_mode();
}
#endif

static void __maybe_unused
cpu_enable_cache_maint_trap(const struct arm64_cpu_capabilities *__unused)
{
	sysreg_clear_set(sctlr_el1, SCTLR_EL1_UCI, 0);
}

#define CAP_MIDR_RANGE(model, v_min, r_min, v_max, r_max)	\
	.matches = is_affected_midr_range,			\
	.midr_range = MIDR_RANGE(model, v_min, r_min, v_max, r_max)

#define CAP_MIDR_ALL_VERSIONS(model)					\
	.matches = is_affected_midr_range,				\
	.midr_range = MIDR_ALL_VERSIONS(model)

#define MIDR_FIXED(rev, revidr_mask) \
	.fixed_revs = (struct arm64_midr_revidr[]){{ (rev), (revidr_mask) }, {}}

#define ERRATA_MIDR_RANGE(model, v_min, r_min, v_max, r_max)		\
	.type = ARM64_CPUCAP_LOCAL_CPU_ERRATUM,				\
	CAP_MIDR_RANGE(model, v_min, r_min, v_max, r_max)

#define CAP_MIDR_RANGE_LIST(list)				\
	.matches = is_affected_midr_range_list,			\
	.midr_range_list = list

/* Errata affecting a range of revisions of  given model variant */
#define ERRATA_MIDR_REV_RANGE(m, var, r_min, r_max)	 \
	ERRATA_MIDR_RANGE(m, var, r_min, var, r_max)

/* Errata affecting a single variant/revision of a model */
#define ERRATA_MIDR_REV(model, var, rev)	\
	ERRATA_MIDR_RANGE(model, var, rev, var, rev)

/* Errata affecting all variants/revisions of a given a model */
#define ERRATA_MIDR_ALL_VERSIONS(model)				\
	.type = ARM64_CPUCAP_LOCAL_CPU_ERRATUM,			\
	CAP_MIDR_ALL_VERSIONS(model)

/* Errata affecting a list of midr ranges, with same work around */
#define ERRATA_MIDR_RANGE_LIST(midr_list)			\
	.type = ARM64_CPUCAP_LOCAL_CPU_ERRATUM,			\
	CAP_MIDR_RANGE_LIST(midr_list)

/* Track overall mitigation state. We are only mitigated if all cores are ok */
static bool __hardenbp_enab = true;
static bool __spectrev2_safe = true;

int get_spectre_v2_workaround_state(void)
{
	if (__spectrev2_safe)
		return ARM64_BP_HARDEN_NOT_REQUIRED;

	if (!__hardenbp_enab)
		return ARM64_BP_HARDEN_UNKNOWN;

	return ARM64_BP_HARDEN_WA_NEEDED;
}

/*
 * List of CPUs that do not need any Spectre-v2 mitigation at all.
 */
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

/*
 * Track overall bp hardening for all heterogeneous cores in the machine.
 * We are only considered "safe" if all booted cores are known safe.
 */
static bool __maybe_unused
check_branch_predictor(const struct arm64_cpu_capabilities *entry, int scope)
{
	int need_wa;

	WARN_ON(scope != SCOPE_LOCAL_CPU || preemptible());

	/* If the CPU has CSV2 set, we're safe */
	if (cpuid_feature_extract_unsigned_field(read_cpuid(ID_AA64PFR0_EL1),
						 ID_AA64PFR0_CSV2_SHIFT))
		return false;

	/* Alternatively, we have a list of unaffected CPUs */
	if (is_midr_in_range_list(read_cpuid_id(), spectre_v2_safe_list))
		return false;

	/* Fallback to firmware detection */
	need_wa = detect_harden_bp_fw();
	if (!need_wa)
		return false;

	__spectrev2_safe = false;

	/* forced off */
	if (__nospectre_v2 || cpu_mitigations_off()) {
		pr_info_once("spectrev2 mitigation disabled by command line option\n");
		__hardenbp_enab = false;
		return false;
	}

	if (need_wa < 0) {
		pr_warn_once("ARM_SMCCC_ARCH_WORKAROUND_1 missing from firmware\n");
		__hardenbp_enab = false;
	}

	return (need_wa > 0);
}

static void
cpu_enable_branch_predictor_hardening(const struct arm64_cpu_capabilities *cap)
{
	cap->matches(cap, SCOPE_LOCAL_CPU);
}

static const __maybe_unused struct midr_range tx2_family_cpus[] = {
	MIDR_ALL_VERSIONS(MIDR_BRCM_VULCAN),
	MIDR_ALL_VERSIONS(MIDR_CAVIUM_THUNDERX2),
	{},
};

static bool __maybe_unused
needs_tx2_tvm_workaround(const struct arm64_cpu_capabilities *entry,
			 int scope)
{
	int i;

	if (!is_affected_midr_range_list(entry, scope) ||
	    !is_hyp_mode_available())
		return false;

	for_each_possible_cpu(i) {
		if (MPIDR_AFFINITY_LEVEL(cpu_logical_map(i), 0) != 0)
			return true;
	}

	return false;
}

static bool __maybe_unused
has_neoverse_n1_erratum_1542419(const struct arm64_cpu_capabilities *entry,
				int scope)
{
	u32 midr = read_cpuid_id();
	bool has_dic = read_cpuid_cachetype() & BIT(CTR_DIC_SHIFT);
	const struct midr_range range = MIDR_ALL_VERSIONS(MIDR_NEOVERSE_N1);

	WARN_ON(scope != SCOPE_LOCAL_CPU || preemptible());
	return is_midr_in_range(midr, &range) && has_dic;
}

#ifdef CONFIG_RANDOMIZE_BASE

static const struct midr_range ca57_a72[] = {
	MIDR_ALL_VERSIONS(MIDR_CORTEX_A57),
	MIDR_ALL_VERSIONS(MIDR_CORTEX_A72),
	{},
};

#endif

#ifdef CONFIG_ARM64_WORKAROUND_REPEAT_TLBI
static const struct arm64_cpu_capabilities arm64_repeat_tlbi_list[] = {
#ifdef CONFIG_QCOM_FALKOR_ERRATUM_1009
	{
		ERRATA_MIDR_REV(MIDR_QCOM_FALKOR_V1, 0, 0)
	},
	{
		.midr_range.model = MIDR_QCOM_KRYO,
		.matches = is_kryo_midr,
	},
#endif
#ifdef CONFIG_ARM64_ERRATUM_1286807
	{
		ERRATA_MIDR_RANGE(MIDR_CORTEX_A76, 0, 0, 3, 0),
	},
#endif
	{},
};
#endif

#ifdef CONFIG_CAVIUM_ERRATUM_27456
const struct midr_range cavium_erratum_27456_cpus[] = {
	/* Cavium ThunderX, T88 pass 1.x - 2.1 */
	MIDR_RANGE(MIDR_THUNDERX, 0, 0, 1, 1),
	/* Cavium ThunderX, T81 pass 1.0 */
	MIDR_REV(MIDR_THUNDERX_81XX, 0, 0),
	{},
};
#endif

#ifdef CONFIG_CAVIUM_ERRATUM_30115
static const struct midr_range cavium_erratum_30115_cpus[] = {
	/* Cavium ThunderX, T88 pass 1.x - 2.2 */
	MIDR_RANGE(MIDR_THUNDERX, 0, 0, 1, 2),
	/* Cavium ThunderX, T81 pass 1.0 - 1.2 */
	MIDR_REV_RANGE(MIDR_THUNDERX_81XX, 0, 0, 2),
	/* Cavium ThunderX, T83 pass 1.0 */
	MIDR_REV(MIDR_THUNDERX_83XX, 0, 0),
	{},
};
#endif

#ifdef CONFIG_QCOM_FALKOR_ERRATUM_1003
static const struct arm64_cpu_capabilities qcom_erratum_1003_list[] = {
	{
		ERRATA_MIDR_REV(MIDR_QCOM_FALKOR_V1, 0, 0),
	},
	{
		.midr_range.model = MIDR_QCOM_KRYO,
		.matches = is_kryo_midr,
	},
	{},
};
#endif

#ifdef CONFIG_ARM64_WORKAROUND_CLEAN_CACHE
static const struct midr_range workaround_clean_cache[] = {
#if	defined(CONFIG_ARM64_ERRATUM_826319) || \
	defined(CONFIG_ARM64_ERRATUM_827319) || \
	defined(CONFIG_ARM64_ERRATUM_824069)
	/* Cortex-A53 r0p[012]: ARM errata 826319, 827319, 824069 */
	MIDR_REV_RANGE(MIDR_CORTEX_A53, 0, 0, 2),
#endif
#ifdef	CONFIG_ARM64_ERRATUM_819472
	/* Cortex-A53 r0p[01] : ARM errata 819472 */
	MIDR_REV_RANGE(MIDR_CORTEX_A53, 0, 0, 1),
#endif
	{},
};
#endif

#ifdef CONFIG_ARM64_ERRATUM_1418040
/*
 * - 1188873 affects r0p0 to r2p0
 * - 1418040 affects r0p0 to r3p1
 */
static const struct midr_range erratum_1418040_list[] = {
	/* Cortex-A76 r0p0 to r3p1 */
	MIDR_RANGE(MIDR_CORTEX_A76, 0, 0, 3, 1),
	/* Neoverse-N1 r0p0 to r3p1 */
	MIDR_RANGE(MIDR_NEOVERSE_N1, 0, 0, 3, 1),
	/* Kryo4xx Gold (rcpe to rfpf) => (r0p0 to r3p1) */
	MIDR_RANGE(MIDR_QCOM_KRYO_4XX_GOLD, 0xc, 0xe, 0xf, 0xf),
	{},
};
#endif

#ifdef CONFIG_ARM64_ERRATUM_845719
static const struct midr_range erratum_845719_list[] = {
	/* Cortex-A53 r0p[01234] */
	MIDR_REV_RANGE(MIDR_CORTEX_A53, 0, 0, 4),
	/* Brahma-B53 r0p[0] */
	MIDR_REV(MIDR_BRAHMA_B53, 0, 0),
	{},
};
#endif

#ifdef CONFIG_ARM64_ERRATUM_843419
static const struct arm64_cpu_capabilities erratum_843419_list[] = {
	{
		/* Cortex-A53 r0p[01234] */
		.matches = is_affected_midr_range,
		ERRATA_MIDR_REV_RANGE(MIDR_CORTEX_A53, 0, 0, 4),
		MIDR_FIXED(0x4, BIT(8)),
	},
	{
		/* Brahma-B53 r0p[0] */
		.matches = is_affected_midr_range,
		ERRATA_MIDR_REV(MIDR_BRAHMA_B53, 0, 0),
	},
	{},
};
#endif

#ifdef CONFIG_ARM64_WORKAROUND_SPECULATIVE_AT
static const struct midr_range erratum_speculative_at_list[] = {
#ifdef CONFIG_ARM64_ERRATUM_1165522
	/* Cortex A76 r0p0 to r2p0 */
	MIDR_RANGE(MIDR_CORTEX_A76, 0, 0, 2, 0),
#endif
#ifdef CONFIG_ARM64_ERRATUM_1319367
	MIDR_ALL_VERSIONS(MIDR_CORTEX_A57),
	MIDR_ALL_VERSIONS(MIDR_CORTEX_A72),
#endif
#ifdef CONFIG_ARM64_ERRATUM_1530923
	/* Cortex A55 r0p0 to r2p0 */
	MIDR_RANGE(MIDR_CORTEX_A55, 0, 0, 2, 0),
	/* Kryo4xx Silver (rdpe => r1p0) */
	MIDR_REV(MIDR_QCOM_KRYO_4XX_SILVER, 0xd, 0xe),
#endif
	{},
};
#endif

#ifdef CONFIG_ARM64_ERRATUM_1463225
static const struct midr_range erratum_1463225[] = {
	/* Cortex-A76 r0p0 - r3p1 */
	MIDR_RANGE(MIDR_CORTEX_A76, 0, 0, 3, 1),
	/* Kryo4xx Gold (rcpe to rfpf) => (r0p0 to r3p1) */
	MIDR_RANGE(MIDR_QCOM_KRYO_4XX_GOLD, 0xc, 0xe, 0xf, 0xf),
	{},
};
#endif

const struct arm64_cpu_capabilities arm64_errata[] = {
#ifdef CONFIG_ARM64_WORKAROUND_CLEAN_CACHE
	{
		.desc = "ARM errata 826319, 827319, 824069, or 819472",
		.capability = ARM64_WORKAROUND_CLEAN_CACHE,
		ERRATA_MIDR_RANGE_LIST(workaround_clean_cache),
		.cpu_enable = cpu_enable_cache_maint_trap,
	},
#endif
#ifdef CONFIG_ARM64_ERRATUM_832075
	{
	/* Cortex-A57 r0p0 - r1p2 */
		.desc = "ARM erratum 832075",
		.capability = ARM64_WORKAROUND_DEVICE_LOAD_ACQUIRE,
		ERRATA_MIDR_RANGE(MIDR_CORTEX_A57,
				  0, 0,
				  1, 2),
	},
#endif
#ifdef CONFIG_ARM64_ERRATUM_834220
	{
	/* Cortex-A57 r0p0 - r1p2 */
		.desc = "ARM erratum 834220",
		.capability = ARM64_WORKAROUND_834220,
		ERRATA_MIDR_RANGE(MIDR_CORTEX_A57,
				  0, 0,
				  1, 2),
	},
#endif
#ifdef CONFIG_ARM64_ERRATUM_843419
	{
		.desc = "ARM erratum 843419",
		.capability = ARM64_WORKAROUND_843419,
		.type = ARM64_CPUCAP_LOCAL_CPU_ERRATUM,
		.matches = cpucap_multi_entry_cap_matches,
		.match_list = erratum_843419_list,
	},
#endif
#ifdef CONFIG_ARM64_ERRATUM_845719
	{
		.desc = "ARM erratum 845719",
		.capability = ARM64_WORKAROUND_845719,
		ERRATA_MIDR_RANGE_LIST(erratum_845719_list),
	},
#endif
#ifdef CONFIG_CAVIUM_ERRATUM_23154
	{
	/* Cavium ThunderX, pass 1.x */
		.desc = "Cavium erratum 23154",
		.capability = ARM64_WORKAROUND_CAVIUM_23154,
		ERRATA_MIDR_REV_RANGE(MIDR_THUNDERX, 0, 0, 1),
	},
#endif
#ifdef CONFIG_CAVIUM_ERRATUM_27456
	{
		.desc = "Cavium erratum 27456",
		.capability = ARM64_WORKAROUND_CAVIUM_27456,
		ERRATA_MIDR_RANGE_LIST(cavium_erratum_27456_cpus),
	},
#endif
#ifdef CONFIG_CAVIUM_ERRATUM_30115
	{
		.desc = "Cavium erratum 30115",
		.capability = ARM64_WORKAROUND_CAVIUM_30115,
		ERRATA_MIDR_RANGE_LIST(cavium_erratum_30115_cpus),
	},
#endif
	{
		.desc = "Mismatched cache type (CTR_EL0)",
		.capability = ARM64_MISMATCHED_CACHE_TYPE,
		.matches = has_mismatched_cache_type,
		.type = ARM64_CPUCAP_LOCAL_CPU_ERRATUM,
		.cpu_enable = cpu_enable_trap_ctr_access,
	},
#ifdef CONFIG_QCOM_FALKOR_ERRATUM_1003
	{
		.desc = "Qualcomm Technologies Falkor/Kryo erratum 1003",
		.capability = ARM64_WORKAROUND_QCOM_FALKOR_E1003,
		.type = ARM64_CPUCAP_LOCAL_CPU_ERRATUM,
		.matches = cpucap_multi_entry_cap_matches,
		.match_list = qcom_erratum_1003_list,
	},
#endif
#ifdef CONFIG_ARM64_WORKAROUND_REPEAT_TLBI
	{
		.desc = "Qualcomm erratum 1009, or ARM erratum 1286807",
		.capability = ARM64_WORKAROUND_REPEAT_TLBI,
		.type = ARM64_CPUCAP_LOCAL_CPU_ERRATUM,
		.matches = cpucap_multi_entry_cap_matches,
		.match_list = arm64_repeat_tlbi_list,
	},
#endif
#ifdef CONFIG_ARM64_ERRATUM_858921
	{
	/* Cortex-A73 all versions */
		.desc = "ARM erratum 858921",
		.capability = ARM64_WORKAROUND_858921,
		ERRATA_MIDR_ALL_VERSIONS(MIDR_CORTEX_A73),
	},
#endif
	{
		.desc = "Branch predictor hardening",
		.capability = ARM64_SPECTRE_V2,
		.type = ARM64_CPUCAP_LOCAL_CPU_ERRATUM,
		.matches = check_branch_predictor,
		.cpu_enable = cpu_enable_branch_predictor_hardening,
	},
#ifdef CONFIG_RANDOMIZE_BASE
	{
		.desc = "EL2 vector hardening",
		.capability = ARM64_HARDEN_EL2_VECTORS,
		ERRATA_MIDR_RANGE_LIST(ca57_a72),
	},
#endif
	{
		.desc = "Speculative Store Bypass Disable",
		.capability = ARM64_SSBD,
		.type = ARM64_CPUCAP_LOCAL_CPU_ERRATUM,
		.matches = has_ssbd_mitigation,
		.cpu_enable = cpu_enable_ssbd_mitigation,
		.midr_range_list = arm64_ssb_cpus,
	},
#ifdef CONFIG_ARM64_ERRATUM_1418040
	{
		.desc = "ARM erratum 1418040",
		.capability = ARM64_WORKAROUND_1418040,
		ERRATA_MIDR_RANGE_LIST(erratum_1418040_list),
		.type = (ARM64_CPUCAP_SCOPE_LOCAL_CPU |
			 ARM64_CPUCAP_PERMITTED_FOR_LATE_CPU),
	},
#endif
#ifdef CONFIG_ARM64_WORKAROUND_SPECULATIVE_AT
	{
		.desc = "ARM errata 1165522, 1319367, or 1530923",
		.capability = ARM64_WORKAROUND_SPECULATIVE_AT,
		ERRATA_MIDR_RANGE_LIST(erratum_speculative_at_list),
	},
#endif
#ifdef CONFIG_ARM64_ERRATUM_1463225
	{
		.desc = "ARM erratum 1463225",
		.capability = ARM64_WORKAROUND_1463225,
		.type = ARM64_CPUCAP_LOCAL_CPU_ERRATUM,
		.matches = has_cortex_a76_erratum_1463225,
		.midr_range_list = erratum_1463225,
	},
#endif
#ifdef CONFIG_CAVIUM_TX2_ERRATUM_219
	{
		.desc = "Cavium ThunderX2 erratum 219 (KVM guest sysreg trapping)",
		.capability = ARM64_WORKAROUND_CAVIUM_TX2_219_TVM,
		ERRATA_MIDR_RANGE_LIST(tx2_family_cpus),
		.matches = needs_tx2_tvm_workaround,
	},
	{
		.desc = "Cavium ThunderX2 erratum 219 (PRFM removal)",
		.capability = ARM64_WORKAROUND_CAVIUM_TX2_219_PRFM,
		ERRATA_MIDR_RANGE_LIST(tx2_family_cpus),
	},
#endif
#ifdef CONFIG_ARM64_ERRATUM_1542419
	{
		/* we depend on the firmware portion for correctness */
		.desc = "ARM erratum 1542419 (kernel portion)",
		.capability = ARM64_WORKAROUND_1542419,
		.type = ARM64_CPUCAP_LOCAL_CPU_ERRATUM,
		.matches = has_neoverse_n1_erratum_1542419,
		.cpu_enable = cpu_enable_trap_ctr_access,
	},
#endif
	{
	}
};

ssize_t cpu_show_spectre_v1(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	return sprintf(buf, "Mitigation: __user pointer sanitization\n");
}

ssize_t cpu_show_spectre_v2(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	switch (get_spectre_v2_workaround_state()) {
	case ARM64_BP_HARDEN_NOT_REQUIRED:
		return sprintf(buf, "Not affected\n");
        case ARM64_BP_HARDEN_WA_NEEDED:
		return sprintf(buf, "Mitigation: Branch predictor hardening\n");
        case ARM64_BP_HARDEN_UNKNOWN:
	default:
		return sprintf(buf, "Vulnerable\n");
	}
}

ssize_t cpu_show_spec_store_bypass(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	if (__ssb_safe)
		return sprintf(buf, "Not affected\n");

	switch (ssbd_state) {
	case ARM64_SSBD_KERNEL:
	case ARM64_SSBD_FORCE_ENABLE:
		return sprintf(buf, "Mitigation: Speculative Store Bypass disabled via prctl\n");
	}

	return sprintf(buf, "Vulnerable\n");
}
