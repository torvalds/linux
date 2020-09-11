// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2017, Nicholas Piggin, IBM Corporation
 */

#define pr_fmt(fmt) "dt-cpu-ftrs: " fmt

#include <linux/export.h>
#include <linux/init.h>
#include <linux/jump_label.h>
#include <linux/libfdt.h>
#include <linux/memblock.h>
#include <linux/printk.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/threads.h>

#include <asm/cputable.h>
#include <asm/dt_cpu_ftrs.h>
#include <asm/mmu.h>
#include <asm/oprofile_impl.h>
#include <asm/prom.h>
#include <asm/setup.h>


/* Device-tree visible constants follow */
#define ISA_V3_0B       3000
#define ISA_V3_1        3100

#define USABLE_PR               (1U << 0)
#define USABLE_OS               (1U << 1)
#define USABLE_HV               (1U << 2)

#define HV_SUPPORT_HFSCR        (1U << 0)
#define OS_SUPPORT_FSCR         (1U << 0)

/* For parsing, we define all bits set as "NONE" case */
#define HV_SUPPORT_NONE		0xffffffffU
#define OS_SUPPORT_NONE		0xffffffffU

struct dt_cpu_feature {
	const char *name;
	uint32_t isa;
	uint32_t usable_privilege;
	uint32_t hv_support;
	uint32_t os_support;
	uint32_t hfscr_bit_nr;
	uint32_t fscr_bit_nr;
	uint32_t hwcap_bit_nr;
	/* fdt parsing */
	unsigned long node;
	int enabled;
	int disabled;
};

#define MMU_FTRS_HASH_BASE (MMU_FTRS_POWER8)

#define COMMON_USER_BASE	(PPC_FEATURE_32 | PPC_FEATURE_64 | \
				 PPC_FEATURE_ARCH_2_06 |\
				 PPC_FEATURE_ICACHE_SNOOP)
#define COMMON_USER2_BASE	(PPC_FEATURE2_ARCH_2_07 | \
				 PPC_FEATURE2_ISEL)
/*
 * Set up the base CPU
 */

static int hv_mode;

static struct {
	u64	lpcr;
	u64	lpcr_clear;
	u64	hfscr;
	u64	fscr;
	u64	pcr;
} system_registers;

static void (*init_pmu_registers)(void);

static void __restore_cpu_cpufeatures(void)
{
	u64 lpcr;

	/*
	 * LPCR is restored by the power on engine already. It can be changed
	 * after early init e.g., by radix enable, and we have no unified API
	 * for saving and restoring such SPRs.
	 *
	 * This ->restore hook should really be removed from idle and register
	 * restore moved directly into the idle restore code, because this code
	 * doesn't know how idle is implemented or what it needs restored here.
	 *
	 * The best we can do to accommodate secondary boot and idle restore
	 * for now is "or" LPCR with existing.
	 */
	lpcr = mfspr(SPRN_LPCR);
	lpcr |= system_registers.lpcr;
	lpcr &= ~system_registers.lpcr_clear;
	mtspr(SPRN_LPCR, lpcr);
	if (hv_mode) {
		mtspr(SPRN_LPID, 0);
		mtspr(SPRN_HFSCR, system_registers.hfscr);
		mtspr(SPRN_PCR, system_registers.pcr);
	}
	mtspr(SPRN_FSCR, system_registers.fscr);

	if (init_pmu_registers)
		init_pmu_registers();
}

static char dt_cpu_name[64];

static struct cpu_spec __initdata base_cpu_spec = {
	.cpu_name		= NULL,
	.cpu_features		= CPU_FTRS_DT_CPU_BASE,
	.cpu_user_features	= COMMON_USER_BASE,
	.cpu_user_features2	= COMMON_USER2_BASE,
	.mmu_features		= 0,
	.icache_bsize		= 32, /* minimum block size, fixed by */
	.dcache_bsize		= 32, /* cache info init.             */
	.num_pmcs		= 0,
	.pmc_type		= PPC_PMC_DEFAULT,
	.oprofile_cpu_type	= NULL,
	.oprofile_type		= PPC_OPROFILE_INVALID,
	.cpu_setup		= NULL,
	.cpu_restore		= __restore_cpu_cpufeatures,
	.machine_check_early	= NULL,
	.platform		= NULL,
};

static void __init cpufeatures_setup_cpu(void)
{
	set_cur_cpu_spec(&base_cpu_spec);

	cur_cpu_spec->pvr_mask = -1;
	cur_cpu_spec->pvr_value = mfspr(SPRN_PVR);

	/* Initialize the base environment -- clear FSCR/HFSCR.  */
	hv_mode = !!(mfmsr() & MSR_HV);
	if (hv_mode) {
		cur_cpu_spec->cpu_features |= CPU_FTR_HVMODE;
		mtspr(SPRN_HFSCR, 0);
	}
	mtspr(SPRN_FSCR, 0);
	mtspr(SPRN_PCR, PCR_MASK);

	/*
	 * LPCR does not get cleared, to match behaviour with secondaries
	 * in __restore_cpu_cpufeatures. Once the idle code is fixed, this
	 * could clear LPCR too.
	 */
}

static int __init feat_try_enable_unknown(struct dt_cpu_feature *f)
{
	if (f->hv_support == HV_SUPPORT_NONE) {
	} else if (f->hv_support & HV_SUPPORT_HFSCR) {
		u64 hfscr = mfspr(SPRN_HFSCR);
		hfscr |= 1UL << f->hfscr_bit_nr;
		mtspr(SPRN_HFSCR, hfscr);
	} else {
		/* Does not have a known recipe */
		return 0;
	}

	if (f->os_support == OS_SUPPORT_NONE) {
	} else if (f->os_support & OS_SUPPORT_FSCR) {
		u64 fscr = mfspr(SPRN_FSCR);
		fscr |= 1UL << f->fscr_bit_nr;
		mtspr(SPRN_FSCR, fscr);
	} else {
		/* Does not have a known recipe */
		return 0;
	}

	if ((f->usable_privilege & USABLE_PR) && (f->hwcap_bit_nr != -1)) {
		uint32_t word = f->hwcap_bit_nr / 32;
		uint32_t bit = f->hwcap_bit_nr % 32;

		if (word == 0)
			cur_cpu_spec->cpu_user_features |= 1U << bit;
		else if (word == 1)
			cur_cpu_spec->cpu_user_features2 |= 1U << bit;
		else
			pr_err("%s could not advertise to user (no hwcap bits)\n", f->name);
	}

	return 1;
}

static int __init feat_enable(struct dt_cpu_feature *f)
{
	if (f->hv_support != HV_SUPPORT_NONE) {
		if (f->hfscr_bit_nr != -1) {
			u64 hfscr = mfspr(SPRN_HFSCR);
			hfscr |= 1UL << f->hfscr_bit_nr;
			mtspr(SPRN_HFSCR, hfscr);
		}
	}

	if (f->os_support != OS_SUPPORT_NONE) {
		if (f->fscr_bit_nr != -1) {
			u64 fscr = mfspr(SPRN_FSCR);
			fscr |= 1UL << f->fscr_bit_nr;
			mtspr(SPRN_FSCR, fscr);
		}
	}

	if ((f->usable_privilege & USABLE_PR) && (f->hwcap_bit_nr != -1)) {
		uint32_t word = f->hwcap_bit_nr / 32;
		uint32_t bit = f->hwcap_bit_nr % 32;

		if (word == 0)
			cur_cpu_spec->cpu_user_features |= 1U << bit;
		else if (word == 1)
			cur_cpu_spec->cpu_user_features2 |= 1U << bit;
		else
			pr_err("CPU feature: %s could not advertise to user (no hwcap bits)\n", f->name);
	}

	return 1;
}

static int __init feat_disable(struct dt_cpu_feature *f)
{
	return 0;
}

static int __init feat_enable_hv(struct dt_cpu_feature *f)
{
	u64 lpcr;

	if (!hv_mode) {
		pr_err("CPU feature hypervisor present in device tree but HV mode not enabled in the CPU. Ignoring.\n");
		return 0;
	}

	mtspr(SPRN_LPID, 0);

	lpcr = mfspr(SPRN_LPCR);
	lpcr &=  ~LPCR_LPES0; /* HV external interrupts */
	mtspr(SPRN_LPCR, lpcr);

	cur_cpu_spec->cpu_features |= CPU_FTR_HVMODE;

	return 1;
}

static int __init feat_enable_le(struct dt_cpu_feature *f)
{
	cur_cpu_spec->cpu_user_features |= PPC_FEATURE_TRUE_LE;
	return 1;
}

static int __init feat_enable_smt(struct dt_cpu_feature *f)
{
	cur_cpu_spec->cpu_features |= CPU_FTR_SMT;
	cur_cpu_spec->cpu_user_features |= PPC_FEATURE_SMT;
	return 1;
}

static int __init feat_enable_idle_nap(struct dt_cpu_feature *f)
{
	u64 lpcr;

	/* Set PECE wakeup modes for ISA 207 */
	lpcr = mfspr(SPRN_LPCR);
	lpcr |=  LPCR_PECE0;
	lpcr |=  LPCR_PECE1;
	lpcr |=  LPCR_PECE2;
	mtspr(SPRN_LPCR, lpcr);

	return 1;
}

static int __init feat_enable_align_dsisr(struct dt_cpu_feature *f)
{
	cur_cpu_spec->cpu_features &= ~CPU_FTR_NODSISRALIGN;

	return 1;
}

static int __init feat_enable_idle_stop(struct dt_cpu_feature *f)
{
	u64 lpcr;

	/* Set PECE wakeup modes for ISAv3.0B */
	lpcr = mfspr(SPRN_LPCR);
	lpcr |=  LPCR_PECE0;
	lpcr |=  LPCR_PECE1;
	lpcr |=  LPCR_PECE2;
	mtspr(SPRN_LPCR, lpcr);

	return 1;
}

static int __init feat_enable_mmu_hash(struct dt_cpu_feature *f)
{
	u64 lpcr;

	lpcr = mfspr(SPRN_LPCR);
	lpcr &= ~LPCR_ISL;

	/* VRMASD */
	lpcr |= LPCR_VPM0;
	lpcr &= ~LPCR_VPM1;
	lpcr |= 0x10UL << LPCR_VRMASD_SH; /* L=1 LP=00 */
	mtspr(SPRN_LPCR, lpcr);

	cur_cpu_spec->mmu_features |= MMU_FTRS_HASH_BASE;
	cur_cpu_spec->cpu_user_features |= PPC_FEATURE_HAS_MMU;

	return 1;
}

static int __init feat_enable_mmu_hash_v3(struct dt_cpu_feature *f)
{
	u64 lpcr;

	system_registers.lpcr_clear |= (LPCR_ISL | LPCR_UPRT | LPCR_HR);
	lpcr = mfspr(SPRN_LPCR);
	lpcr &= ~(LPCR_ISL | LPCR_UPRT | LPCR_HR);
	mtspr(SPRN_LPCR, lpcr);

	cur_cpu_spec->mmu_features |= MMU_FTRS_HASH_BASE;
	cur_cpu_spec->cpu_user_features |= PPC_FEATURE_HAS_MMU;

	return 1;
}


static int __init feat_enable_mmu_radix(struct dt_cpu_feature *f)
{
#ifdef CONFIG_PPC_RADIX_MMU
	cur_cpu_spec->mmu_features |= MMU_FTR_TYPE_RADIX;
	cur_cpu_spec->mmu_features |= MMU_FTRS_HASH_BASE;
	cur_cpu_spec->mmu_features |= MMU_FTR_GTSE;
	cur_cpu_spec->cpu_user_features |= PPC_FEATURE_HAS_MMU;

	return 1;
#endif
	return 0;
}

static int __init feat_enable_dscr(struct dt_cpu_feature *f)
{
	u64 lpcr;

	/*
	 * Linux relies on FSCR[DSCR] being clear, so that we can take the
	 * facility unavailable interrupt and track the task's usage of DSCR.
	 * See facility_unavailable_exception().
	 * Clear the bit here so that feat_enable() doesn't set it.
	 */
	f->fscr_bit_nr = -1;

	feat_enable(f);

	lpcr = mfspr(SPRN_LPCR);
	lpcr &= ~LPCR_DPFD;
	lpcr |=  (4UL << LPCR_DPFD_SH);
	mtspr(SPRN_LPCR, lpcr);

	return 1;
}

static void hfscr_pmu_enable(void)
{
	u64 hfscr = mfspr(SPRN_HFSCR);
	hfscr |= PPC_BIT(60);
	mtspr(SPRN_HFSCR, hfscr);
}

static void init_pmu_power8(void)
{
	if (hv_mode) {
		mtspr(SPRN_MMCRC, 0);
		mtspr(SPRN_MMCRH, 0);
	}

	mtspr(SPRN_MMCRA, 0);
	mtspr(SPRN_MMCR0, 0);
	mtspr(SPRN_MMCR1, 0);
	mtspr(SPRN_MMCR2, 0);
	mtspr(SPRN_MMCRS, 0);
}

static int __init feat_enable_mce_power8(struct dt_cpu_feature *f)
{
	cur_cpu_spec->platform = "power8";
	cur_cpu_spec->machine_check_early = __machine_check_early_realmode_p8;

	return 1;
}

static int __init feat_enable_pmu_power8(struct dt_cpu_feature *f)
{
	hfscr_pmu_enable();

	init_pmu_power8();
	init_pmu_registers = init_pmu_power8;

	cur_cpu_spec->cpu_features |= CPU_FTR_MMCRA;
	cur_cpu_spec->cpu_user_features |= PPC_FEATURE_PSERIES_PERFMON_COMPAT;
	if (pvr_version_is(PVR_POWER8E))
		cur_cpu_spec->cpu_features |= CPU_FTR_PMAO_BUG;

	cur_cpu_spec->num_pmcs		= 6;
	cur_cpu_spec->pmc_type		= PPC_PMC_IBM;
	cur_cpu_spec->oprofile_cpu_type	= "ppc64/power8";

	return 1;
}

static void init_pmu_power9(void)
{
	if (hv_mode)
		mtspr(SPRN_MMCRC, 0);

	mtspr(SPRN_MMCRA, 0);
	mtspr(SPRN_MMCR0, 0);
	mtspr(SPRN_MMCR1, 0);
	mtspr(SPRN_MMCR2, 0);
}

static int __init feat_enable_mce_power9(struct dt_cpu_feature *f)
{
	cur_cpu_spec->platform = "power9";
	cur_cpu_spec->machine_check_early = __machine_check_early_realmode_p9;

	return 1;
}

static int __init feat_enable_pmu_power9(struct dt_cpu_feature *f)
{
	hfscr_pmu_enable();

	init_pmu_power9();
	init_pmu_registers = init_pmu_power9;

	cur_cpu_spec->cpu_features |= CPU_FTR_MMCRA;
	cur_cpu_spec->cpu_user_features |= PPC_FEATURE_PSERIES_PERFMON_COMPAT;

	cur_cpu_spec->num_pmcs		= 6;
	cur_cpu_spec->pmc_type		= PPC_PMC_IBM;
	cur_cpu_spec->oprofile_cpu_type	= "ppc64/power9";

	return 1;
}

static void init_pmu_power10(void)
{
	init_pmu_power9();

	mtspr(SPRN_MMCR3, 0);
	mtspr(SPRN_MMCRA, MMCRA_BHRB_DISABLE);
}

static int __init feat_enable_pmu_power10(struct dt_cpu_feature *f)
{
	hfscr_pmu_enable();

	init_pmu_power10();
	init_pmu_registers = init_pmu_power10;

	cur_cpu_spec->cpu_features |= CPU_FTR_MMCRA;
	cur_cpu_spec->cpu_user_features |= PPC_FEATURE_PSERIES_PERFMON_COMPAT;

	cur_cpu_spec->num_pmcs          = 6;
	cur_cpu_spec->pmc_type          = PPC_PMC_IBM;
	cur_cpu_spec->oprofile_cpu_type = "ppc64/power10";

	return 1;
}

static int __init feat_enable_mce_power10(struct dt_cpu_feature *f)
{
	cur_cpu_spec->platform = "power10";
	cur_cpu_spec->machine_check_early = __machine_check_early_realmode_p10;

	return 1;
}

static int __init feat_enable_tm(struct dt_cpu_feature *f)
{
#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
	feat_enable(f);
	cur_cpu_spec->cpu_user_features2 |= PPC_FEATURE2_HTM_NOSC;
	return 1;
#endif
	return 0;
}

static int __init feat_enable_fp(struct dt_cpu_feature *f)
{
	feat_enable(f);
	cur_cpu_spec->cpu_features &= ~CPU_FTR_FPU_UNAVAILABLE;

	return 1;
}

static int __init feat_enable_vector(struct dt_cpu_feature *f)
{
#ifdef CONFIG_ALTIVEC
	feat_enable(f);
	cur_cpu_spec->cpu_features |= CPU_FTR_ALTIVEC;
	cur_cpu_spec->cpu_features |= CPU_FTR_VMX_COPY;
	cur_cpu_spec->cpu_user_features |= PPC_FEATURE_HAS_ALTIVEC;

	return 1;
#endif
	return 0;
}

static int __init feat_enable_vsx(struct dt_cpu_feature *f)
{
#ifdef CONFIG_VSX
	feat_enable(f);
	cur_cpu_spec->cpu_features |= CPU_FTR_VSX;
	cur_cpu_spec->cpu_user_features |= PPC_FEATURE_HAS_VSX;

	return 1;
#endif
	return 0;
}

static int __init feat_enable_purr(struct dt_cpu_feature *f)
{
	cur_cpu_spec->cpu_features |= CPU_FTR_PURR | CPU_FTR_SPURR;

	return 1;
}

static int __init feat_enable_ebb(struct dt_cpu_feature *f)
{
	/*
	 * PPC_FEATURE2_EBB is enabled in PMU init code because it has
	 * historically been related to the PMU facility. This may have
	 * to be decoupled if EBB becomes more generic. For now, follow
	 * existing convention.
	 */
	f->hwcap_bit_nr = -1;
	feat_enable(f);

	return 1;
}

static int __init feat_enable_dbell(struct dt_cpu_feature *f)
{
	u64 lpcr;

	/* P9 has an HFSCR for privileged state */
	feat_enable(f);

	cur_cpu_spec->cpu_features |= CPU_FTR_DBELL;

	lpcr = mfspr(SPRN_LPCR);
	lpcr |=  LPCR_PECEDH; /* hyp doorbell wakeup */
	mtspr(SPRN_LPCR, lpcr);

	return 1;
}

static int __init feat_enable_hvi(struct dt_cpu_feature *f)
{
	u64 lpcr;

	/*
	 * POWER9 XIVE interrupts including in OPAL XICS compatibility
	 * are always delivered as hypervisor virtualization interrupts (HVI)
	 * rather than EE.
	 *
	 * However LPES0 is not set here, in the chance that an EE does get
	 * delivered to the host somehow, the EE handler would not expect it
	 * to be delivered in LPES0 mode (e.g., using SRR[01]). This could
	 * happen if there is a bug in interrupt controller code, or IC is
	 * misconfigured in systemsim.
	 */

	lpcr = mfspr(SPRN_LPCR);
	lpcr |= LPCR_HVICE;	/* enable hvi interrupts */
	lpcr |= LPCR_HEIC;	/* disable ee interrupts when MSR_HV */
	lpcr |= LPCR_PECE_HVEE; /* hvi can wake from stop */
	mtspr(SPRN_LPCR, lpcr);

	return 1;
}

static int __init feat_enable_large_ci(struct dt_cpu_feature *f)
{
	cur_cpu_spec->mmu_features |= MMU_FTR_CI_LARGE_PAGE;

	return 1;
}

static int __init feat_enable_mma(struct dt_cpu_feature *f)
{
	u64 pcr;

	feat_enable(f);
	pcr = mfspr(SPRN_PCR);
	pcr &= ~PCR_MMA_DIS;
	mtspr(SPRN_PCR, pcr);

	return 1;
}

struct dt_cpu_feature_match {
	const char *name;
	int (*enable)(struct dt_cpu_feature *f);
	u64 cpu_ftr_bit_mask;
};

static struct dt_cpu_feature_match __initdata
		dt_cpu_feature_match_table[] = {
	{"hypervisor", feat_enable_hv, 0},
	{"big-endian", feat_enable, 0},
	{"little-endian", feat_enable_le, CPU_FTR_REAL_LE},
	{"smt", feat_enable_smt, 0},
	{"interrupt-facilities", feat_enable, 0},
	{"system-call-vectored", feat_enable, 0},
	{"timer-facilities", feat_enable, 0},
	{"timer-facilities-v3", feat_enable, 0},
	{"debug-facilities", feat_enable, 0},
	{"come-from-address-register", feat_enable, CPU_FTR_CFAR},
	{"branch-tracing", feat_enable, 0},
	{"floating-point", feat_enable_fp, 0},
	{"vector", feat_enable_vector, 0},
	{"vector-scalar", feat_enable_vsx, 0},
	{"vector-scalar-v3", feat_enable, 0},
	{"decimal-floating-point", feat_enable, 0},
	{"decimal-integer", feat_enable, 0},
	{"quadword-load-store", feat_enable, 0},
	{"vector-crypto", feat_enable, 0},
	{"mmu-hash", feat_enable_mmu_hash, 0},
	{"mmu-radix", feat_enable_mmu_radix, 0},
	{"mmu-hash-v3", feat_enable_mmu_hash_v3, 0},
	{"virtual-page-class-key-protection", feat_enable, 0},
	{"transactional-memory", feat_enable_tm, CPU_FTR_TM},
	{"transactional-memory-v3", feat_enable_tm, 0},
	{"tm-suspend-hypervisor-assist", feat_enable, CPU_FTR_P9_TM_HV_ASSIST},
	{"tm-suspend-xer-so-bug", feat_enable, CPU_FTR_P9_TM_XER_SO_BUG},
	{"idle-nap", feat_enable_idle_nap, 0},
	{"alignment-interrupt-dsisr", feat_enable_align_dsisr, 0},
	{"idle-stop", feat_enable_idle_stop, 0},
	{"machine-check-power8", feat_enable_mce_power8, 0},
	{"performance-monitor-power8", feat_enable_pmu_power8, 0},
	{"data-stream-control-register", feat_enable_dscr, CPU_FTR_DSCR},
	{"event-based-branch", feat_enable_ebb, 0},
	{"target-address-register", feat_enable, 0},
	{"branch-history-rolling-buffer", feat_enable, 0},
	{"control-register", feat_enable, CPU_FTR_CTRL},
	{"processor-control-facility", feat_enable_dbell, CPU_FTR_DBELL},
	{"processor-control-facility-v3", feat_enable_dbell, CPU_FTR_DBELL},
	{"processor-utilization-of-resources-register", feat_enable_purr, 0},
	{"no-execute", feat_enable, 0},
	{"strong-access-ordering", feat_enable, CPU_FTR_SAO},
	{"cache-inhibited-large-page", feat_enable_large_ci, 0},
	{"coprocessor-icswx", feat_enable, 0},
	{"hypervisor-virtualization-interrupt", feat_enable_hvi, 0},
	{"program-priority-register", feat_enable, CPU_FTR_HAS_PPR},
	{"wait", feat_enable, 0},
	{"atomic-memory-operations", feat_enable, 0},
	{"branch-v3", feat_enable, 0},
	{"copy-paste", feat_enable, 0},
	{"decimal-floating-point-v3", feat_enable, 0},
	{"decimal-integer-v3", feat_enable, 0},
	{"fixed-point-v3", feat_enable, 0},
	{"floating-point-v3", feat_enable, 0},
	{"group-start-register", feat_enable, 0},
	{"pc-relative-addressing", feat_enable, 0},
	{"machine-check-power9", feat_enable_mce_power9, 0},
	{"machine-check-power10", feat_enable_mce_power10, 0},
	{"performance-monitor-power9", feat_enable_pmu_power9, 0},
	{"performance-monitor-power10", feat_enable_pmu_power10, 0},
	{"event-based-branch-v3", feat_enable, 0},
	{"random-number-generator", feat_enable, 0},
	{"system-call-vectored", feat_disable, 0},
	{"trace-interrupt-v3", feat_enable, 0},
	{"vector-v3", feat_enable, 0},
	{"vector-binary128", feat_enable, 0},
	{"vector-binary16", feat_enable, 0},
	{"wait-v3", feat_enable, 0},
	{"prefix-instructions", feat_enable, 0},
	{"matrix-multiply-assist", feat_enable_mma, 0},
	{"debug-facilities-v31", feat_enable, CPU_FTR_DAWR1},
};

static bool __initdata using_dt_cpu_ftrs;
static bool __initdata enable_unknown = true;

static int __init dt_cpu_ftrs_parse(char *str)
{
	if (!str)
		return 0;

	if (!strcmp(str, "off"))
		using_dt_cpu_ftrs = false;
	else if (!strcmp(str, "known"))
		enable_unknown = false;
	else
		return 1;

	return 0;
}
early_param("dt_cpu_ftrs", dt_cpu_ftrs_parse);

static void __init cpufeatures_setup_start(u32 isa)
{
	pr_info("setup for ISA %d\n", isa);

	if (isa >= ISA_V3_0B) {
		cur_cpu_spec->cpu_features |= CPU_FTR_ARCH_300;
		cur_cpu_spec->cpu_user_features2 |= PPC_FEATURE2_ARCH_3_00;
	}

	if (isa >= ISA_V3_1) {
		cur_cpu_spec->cpu_features |= CPU_FTR_ARCH_31;
		cur_cpu_spec->cpu_user_features2 |= PPC_FEATURE2_ARCH_3_1;
	}
}

static bool __init cpufeatures_process_feature(struct dt_cpu_feature *f)
{
	const struct dt_cpu_feature_match *m;
	bool known = false;
	int i;

	for (i = 0; i < ARRAY_SIZE(dt_cpu_feature_match_table); i++) {
		m = &dt_cpu_feature_match_table[i];
		if (!strcmp(f->name, m->name)) {
			known = true;
			if (m->enable(f)) {
				cur_cpu_spec->cpu_features |= m->cpu_ftr_bit_mask;
				break;
			}

			pr_info("not enabling: %s (disabled or unsupported by kernel)\n",
				f->name);
			return false;
		}
	}

	if (!known && (!enable_unknown || !feat_try_enable_unknown(f))) {
		pr_info("not enabling: %s (unknown and unsupported by kernel)\n",
			f->name);
		return false;
	}

	if (known)
		pr_debug("enabling: %s\n", f->name);
	else
		pr_debug("enabling: %s (unknown)\n", f->name);

	return true;
}

/*
 * Handle POWER9 broadcast tlbie invalidation issue using
 * cpu feature flag.
 */
static __init void update_tlbie_feature_flag(unsigned long pvr)
{
	if (PVR_VER(pvr) == PVR_POWER9) {
		/*
		 * Set the tlbie feature flag for anything below
		 * Nimbus DD 2.3 and Cumulus DD 1.3
		 */
		if ((pvr & 0xe000) == 0) {
			/* Nimbus */
			if ((pvr & 0xfff) < 0x203)
				cur_cpu_spec->cpu_features |= CPU_FTR_P9_TLBIE_STQ_BUG;
		} else if ((pvr & 0xc000) == 0) {
			/* Cumulus */
			if ((pvr & 0xfff) < 0x103)
				cur_cpu_spec->cpu_features |= CPU_FTR_P9_TLBIE_STQ_BUG;
		} else {
			WARN_ONCE(1, "Unknown PVR");
			cur_cpu_spec->cpu_features |= CPU_FTR_P9_TLBIE_STQ_BUG;
		}

		cur_cpu_spec->cpu_features |= CPU_FTR_P9_TLBIE_ERAT_BUG;
	}
}

static __init void cpufeatures_cpu_quirks(void)
{
	unsigned long version = mfspr(SPRN_PVR);

	/*
	 * Not all quirks can be derived from the cpufeatures device tree.
	 */
	if ((version & 0xffffefff) == 0x004e0200) {
		/* DD2.0 has no feature flag */
		cur_cpu_spec->cpu_features |= CPU_FTR_P9_RADIX_PREFETCH_BUG;
	} else if ((version & 0xffffefff) == 0x004e0201) {
		cur_cpu_spec->cpu_features |= CPU_FTR_POWER9_DD2_1;
		cur_cpu_spec->cpu_features |= CPU_FTR_P9_RADIX_PREFETCH_BUG;
	} else if ((version & 0xffffefff) == 0x004e0202) {
		cur_cpu_spec->cpu_features |= CPU_FTR_P9_TM_HV_ASSIST;
		cur_cpu_spec->cpu_features |= CPU_FTR_P9_TM_XER_SO_BUG;
		cur_cpu_spec->cpu_features |= CPU_FTR_POWER9_DD2_1;
	} else if ((version & 0xffff0000) == 0x004e0000) {
		/* DD2.1 and up have DD2_1 */
		cur_cpu_spec->cpu_features |= CPU_FTR_POWER9_DD2_1;
	}

	if ((version & 0xffff0000) == 0x004e0000) {
		cur_cpu_spec->cpu_features &= ~(CPU_FTR_DAWR);
		cur_cpu_spec->cpu_features |= CPU_FTR_P9_TIDR;
	}

	update_tlbie_feature_flag(version);
}

static void __init cpufeatures_setup_finished(void)
{
	cpufeatures_cpu_quirks();

	if (hv_mode && !(cur_cpu_spec->cpu_features & CPU_FTR_HVMODE)) {
		pr_err("hypervisor not present in device tree but HV mode is enabled in the CPU. Enabling.\n");
		cur_cpu_spec->cpu_features |= CPU_FTR_HVMODE;
	}

	/* Make sure powerpc_base_platform is non-NULL */
	powerpc_base_platform = cur_cpu_spec->platform;

	system_registers.lpcr = mfspr(SPRN_LPCR);
	system_registers.hfscr = mfspr(SPRN_HFSCR);
	system_registers.fscr = mfspr(SPRN_FSCR);
	system_registers.pcr = mfspr(SPRN_PCR);

	pr_info("final cpu/mmu features = 0x%016lx 0x%08x\n",
		cur_cpu_spec->cpu_features, cur_cpu_spec->mmu_features);
}

static int __init disabled_on_cmdline(void)
{
	unsigned long root, chosen;
	const char *p;

	root = of_get_flat_dt_root();
	chosen = of_get_flat_dt_subnode_by_name(root, "chosen");
	if (chosen == -FDT_ERR_NOTFOUND)
		return false;

	p = of_get_flat_dt_prop(chosen, "bootargs", NULL);
	if (!p)
		return false;

	if (strstr(p, "dt_cpu_ftrs=off"))
		return true;

	return false;
}

static int __init fdt_find_cpu_features(unsigned long node, const char *uname,
					int depth, void *data)
{
	if (of_flat_dt_is_compatible(node, "ibm,powerpc-cpu-features")
	    && of_get_flat_dt_prop(node, "isa", NULL))
		return 1;

	return 0;
}

bool __init dt_cpu_ftrs_in_use(void)
{
	return using_dt_cpu_ftrs;
}

bool __init dt_cpu_ftrs_init(void *fdt)
{
	using_dt_cpu_ftrs = false;

	/* Setup and verify the FDT, if it fails we just bail */
	if (!early_init_dt_verify(fdt))
		return false;

	if (!of_scan_flat_dt(fdt_find_cpu_features, NULL))
		return false;

	if (disabled_on_cmdline())
		return false;

	cpufeatures_setup_cpu();

	using_dt_cpu_ftrs = true;
	return true;
}

static int nr_dt_cpu_features;
static struct dt_cpu_feature *dt_cpu_features;

static int __init process_cpufeatures_node(unsigned long node,
					  const char *uname, int i)
{
	const __be32 *prop;
	struct dt_cpu_feature *f;
	int len;

	f = &dt_cpu_features[i];

	f->node = node;

	f->name = uname;

	prop = of_get_flat_dt_prop(node, "isa", &len);
	if (!prop) {
		pr_warn("%s: missing isa property\n", uname);
		return 0;
	}
	f->isa = be32_to_cpup(prop);

	prop = of_get_flat_dt_prop(node, "usable-privilege", &len);
	if (!prop) {
		pr_warn("%s: missing usable-privilege property", uname);
		return 0;
	}
	f->usable_privilege = be32_to_cpup(prop);

	prop = of_get_flat_dt_prop(node, "hv-support", &len);
	if (prop)
		f->hv_support = be32_to_cpup(prop);
	else
		f->hv_support = HV_SUPPORT_NONE;

	prop = of_get_flat_dt_prop(node, "os-support", &len);
	if (prop)
		f->os_support = be32_to_cpup(prop);
	else
		f->os_support = OS_SUPPORT_NONE;

	prop = of_get_flat_dt_prop(node, "hfscr-bit-nr", &len);
	if (prop)
		f->hfscr_bit_nr = be32_to_cpup(prop);
	else
		f->hfscr_bit_nr = -1;
	prop = of_get_flat_dt_prop(node, "fscr-bit-nr", &len);
	if (prop)
		f->fscr_bit_nr = be32_to_cpup(prop);
	else
		f->fscr_bit_nr = -1;
	prop = of_get_flat_dt_prop(node, "hwcap-bit-nr", &len);
	if (prop)
		f->hwcap_bit_nr = be32_to_cpup(prop);
	else
		f->hwcap_bit_nr = -1;

	if (f->usable_privilege & USABLE_HV) {
		if (!(mfmsr() & MSR_HV)) {
			pr_warn("%s: HV feature passed to guest\n", uname);
			return 0;
		}

		if (f->hv_support == HV_SUPPORT_NONE && f->hfscr_bit_nr != -1) {
			pr_warn("%s: unwanted hfscr_bit_nr\n", uname);
			return 0;
		}

		if (f->hv_support == HV_SUPPORT_HFSCR) {
			if (f->hfscr_bit_nr == -1) {
				pr_warn("%s: missing hfscr_bit_nr\n", uname);
				return 0;
			}
		}
	} else {
		if (f->hv_support != HV_SUPPORT_NONE || f->hfscr_bit_nr != -1) {
			pr_warn("%s: unwanted hv_support/hfscr_bit_nr\n", uname);
			return 0;
		}
	}

	if (f->usable_privilege & USABLE_OS) {
		if (f->os_support == OS_SUPPORT_NONE && f->fscr_bit_nr != -1) {
			pr_warn("%s: unwanted fscr_bit_nr\n", uname);
			return 0;
		}

		if (f->os_support == OS_SUPPORT_FSCR) {
			if (f->fscr_bit_nr == -1) {
				pr_warn("%s: missing fscr_bit_nr\n", uname);
				return 0;
			}
		}
	} else {
		if (f->os_support != OS_SUPPORT_NONE || f->fscr_bit_nr != -1) {
			pr_warn("%s: unwanted os_support/fscr_bit_nr\n", uname);
			return 0;
		}
	}

	if (!(f->usable_privilege & USABLE_PR)) {
		if (f->hwcap_bit_nr != -1) {
			pr_warn("%s: unwanted hwcap_bit_nr\n", uname);
			return 0;
		}
	}

	/* Do all the independent features in the first pass */
	if (!of_get_flat_dt_prop(node, "dependencies", &len)) {
		if (cpufeatures_process_feature(f))
			f->enabled = 1;
		else
			f->disabled = 1;
	}

	return 0;
}

static void __init cpufeatures_deps_enable(struct dt_cpu_feature *f)
{
	const __be32 *prop;
	int len;
	int nr_deps;
	int i;

	if (f->enabled || f->disabled)
		return;

	prop = of_get_flat_dt_prop(f->node, "dependencies", &len);
	if (!prop) {
		pr_warn("%s: missing dependencies property", f->name);
		return;
	}

	nr_deps = len / sizeof(int);

	for (i = 0; i < nr_deps; i++) {
		unsigned long phandle = be32_to_cpu(prop[i]);
		int j;

		for (j = 0; j < nr_dt_cpu_features; j++) {
			struct dt_cpu_feature *d = &dt_cpu_features[j];

			if (of_get_flat_dt_phandle(d->node) == phandle) {
				cpufeatures_deps_enable(d);
				if (d->disabled) {
					f->disabled = 1;
					return;
				}
			}
		}
	}

	if (cpufeatures_process_feature(f))
		f->enabled = 1;
	else
		f->disabled = 1;
}

static int __init scan_cpufeatures_subnodes(unsigned long node,
					  const char *uname,
					  void *data)
{
	int *count = data;

	process_cpufeatures_node(node, uname, *count);

	(*count)++;

	return 0;
}

static int __init count_cpufeatures_subnodes(unsigned long node,
					  const char *uname,
					  void *data)
{
	int *count = data;

	(*count)++;

	return 0;
}

static int __init dt_cpu_ftrs_scan_callback(unsigned long node, const char
					    *uname, int depth, void *data)
{
	const __be32 *prop;
	int count, i;
	u32 isa;

	/* We are scanning "ibm,powerpc-cpu-features" nodes only */
	if (!of_flat_dt_is_compatible(node, "ibm,powerpc-cpu-features"))
		return 0;

	prop = of_get_flat_dt_prop(node, "isa", NULL);
	if (!prop)
		/* We checked before, "can't happen" */
		return 0;

	isa = be32_to_cpup(prop);

	/* Count and allocate space for cpu features */
	of_scan_flat_dt_subnodes(node, count_cpufeatures_subnodes,
						&nr_dt_cpu_features);
	dt_cpu_features = memblock_alloc(sizeof(struct dt_cpu_feature) * nr_dt_cpu_features, PAGE_SIZE);
	if (!dt_cpu_features)
		panic("%s: Failed to allocate %zu bytes align=0x%lx\n",
		      __func__,
		      sizeof(struct dt_cpu_feature) * nr_dt_cpu_features,
		      PAGE_SIZE);

	cpufeatures_setup_start(isa);

	/* Scan nodes into dt_cpu_features and enable those without deps  */
	count = 0;
	of_scan_flat_dt_subnodes(node, scan_cpufeatures_subnodes, &count);

	/* Recursive enable remaining features with dependencies */
	for (i = 0; i < nr_dt_cpu_features; i++) {
		struct dt_cpu_feature *f = &dt_cpu_features[i];

		cpufeatures_deps_enable(f);
	}

	prop = of_get_flat_dt_prop(node, "display-name", NULL);
	if (prop && strlen((char *)prop) != 0) {
		strlcpy(dt_cpu_name, (char *)prop, sizeof(dt_cpu_name));
		cur_cpu_spec->cpu_name = dt_cpu_name;
	}

	cpufeatures_setup_finished();

	memblock_free(__pa(dt_cpu_features),
			sizeof(struct dt_cpu_feature)*nr_dt_cpu_features);

	return 0;
}

void __init dt_cpu_ftrs_scan(void)
{
	if (!using_dt_cpu_ftrs)
		return;

	of_scan_flat_dt(dt_cpu_ftrs_scan_callback, NULL);
}
