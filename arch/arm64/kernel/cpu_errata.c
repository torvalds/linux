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
		.desc = "Spectre-v2",
		.capability = ARM64_SPECTRE_V2,
		.type = ARM64_CPUCAP_LOCAL_CPU_ERRATUM,
		.matches = has_spectre_v2,
		.cpu_enable = spectre_v2_enable_mitigation,
	},
#ifdef CONFIG_RANDOMIZE_BASE
	{
		.desc = "EL2 vector hardening",
		.capability = ARM64_HARDEN_EL2_VECTORS,
		ERRATA_MIDR_RANGE_LIST(ca57_a72),
	},
#endif
	{
		.desc = "Spectre-v4",
		.capability = ARM64_SPECTRE_V4,
		.type = ARM64_CPUCAP_LOCAL_CPU_ERRATUM,
		.matches = has_spectre_v4,
		.cpu_enable = spectre_v4_enable_mitigation,
	},
#ifdef CONFIG_ARM64_ERRATUM_1418040
	{
		.desc = "ARM erratum 1418040",
		.capability = ARM64_WORKAROUND_1418040,
		ERRATA_MIDR_RANGE_LIST(erratum_1418040_list),
		/*
		 * We need to allow affected CPUs to come in late, but
		 * also need the non-affected CPUs to be able to come
		 * in at any point in time. Wonderful.
		 */
		.type = ARM64_CPUCAP_WEAK_LOCAL_CPU_FEATURE,
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
