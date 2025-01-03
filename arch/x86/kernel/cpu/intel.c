// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <linux/pgtable.h>

#include <linux/string.h>
#include <linux/bitops.h>
#include <linux/smp.h>
#include <linux/sched.h>
#include <linux/sched/clock.h>
#include <linux/thread_info.h>
#include <linux/init.h>
#include <linux/uaccess.h>

#include <asm/cpufeature.h>
#include <asm/msr.h>
#include <asm/bugs.h>
#include <asm/cpu.h>
#include <asm/intel-family.h>
#include <asm/microcode.h>
#include <asm/hwcap2.h>
#include <asm/elf.h>
#include <asm/cpu_device_id.h>
#include <asm/resctrl.h>
#include <asm/numa.h>
#include <asm/thermal.h>

#ifdef CONFIG_X86_64
#include <linux/topology.h>
#endif

#include "cpu.h"

#ifdef CONFIG_X86_LOCAL_APIC
#include <asm/mpspec.h>
#include <asm/apic.h>
#endif

/*
 * Processors which have self-snooping capability can handle conflicting
 * memory type across CPUs by snooping its own cache. However, there exists
 * CPU models in which having conflicting memory types still leads to
 * unpredictable behavior, machine check errors, or hangs. Clear this
 * feature to prevent its use on machines with known erratas.
 */
static void check_memory_type_self_snoop_errata(struct cpuinfo_x86 *c)
{
	switch (c->x86_vfm) {
	case INTEL_CORE_YONAH:
	case INTEL_CORE2_MEROM:
	case INTEL_CORE2_MEROM_L:
	case INTEL_CORE2_PENRYN:
	case INTEL_CORE2_DUNNINGTON:
	case INTEL_NEHALEM:
	case INTEL_NEHALEM_G:
	case INTEL_NEHALEM_EP:
	case INTEL_NEHALEM_EX:
	case INTEL_WESTMERE:
	case INTEL_WESTMERE_EP:
	case INTEL_SANDYBRIDGE:
		setup_clear_cpu_cap(X86_FEATURE_SELFSNOOP);
	}
}

static bool ring3mwait_disabled __read_mostly;

static int __init ring3mwait_disable(char *__unused)
{
	ring3mwait_disabled = true;
	return 1;
}
__setup("ring3mwait=disable", ring3mwait_disable);

static void probe_xeon_phi_r3mwait(struct cpuinfo_x86 *c)
{
	/*
	 * Ring 3 MONITOR/MWAIT feature cannot be detected without
	 * cpu model and family comparison.
	 */
	if (c->x86 != 6)
		return;
	switch (c->x86_vfm) {
	case INTEL_XEON_PHI_KNL:
	case INTEL_XEON_PHI_KNM:
		break;
	default:
		return;
	}

	if (ring3mwait_disabled)
		return;

	set_cpu_cap(c, X86_FEATURE_RING3MWAIT);
	this_cpu_or(msr_misc_features_shadow,
		    1UL << MSR_MISC_FEATURES_ENABLES_RING3MWAIT_BIT);

	if (c == &boot_cpu_data)
		ELF_HWCAP2 |= HWCAP2_RING3MWAIT;
}

/*
 * Early microcode releases for the Spectre v2 mitigation were broken.
 * Information taken from;
 * - https://newsroom.intel.com/wp-content/uploads/sites/11/2018/03/microcode-update-guidance.pdf
 * - https://kb.vmware.com/s/article/52345
 * - Microcode revisions observed in the wild
 * - Release note from 20180108 microcode release
 */
struct sku_microcode {
	u32 vfm;
	u8 stepping;
	u32 microcode;
};
static const struct sku_microcode spectre_bad_microcodes[] = {
	{ INTEL_KABYLAKE,	0x0B,	0x80 },
	{ INTEL_KABYLAKE,	0x0A,	0x80 },
	{ INTEL_KABYLAKE,	0x09,	0x80 },
	{ INTEL_KABYLAKE_L,	0x0A,	0x80 },
	{ INTEL_KABYLAKE_L,	0x09,	0x80 },
	{ INTEL_SKYLAKE_X,	0x03,	0x0100013e },
	{ INTEL_SKYLAKE_X,	0x04,	0x0200003c },
	{ INTEL_BROADWELL,	0x04,	0x28 },
	{ INTEL_BROADWELL_G,	0x01,	0x1b },
	{ INTEL_BROADWELL_D,	0x02,	0x14 },
	{ INTEL_BROADWELL_D,	0x03,	0x07000011 },
	{ INTEL_BROADWELL_X,	0x01,	0x0b000025 },
	{ INTEL_HASWELL_L,	0x01,	0x21 },
	{ INTEL_HASWELL_G,	0x01,	0x18 },
	{ INTEL_HASWELL,	0x03,	0x23 },
	{ INTEL_HASWELL_X,	0x02,	0x3b },
	{ INTEL_HASWELL_X,	0x04,	0x10 },
	{ INTEL_IVYBRIDGE_X,	0x04,	0x42a },
	/* Observed in the wild */
	{ INTEL_SANDYBRIDGE_X,	0x06,	0x61b },
	{ INTEL_SANDYBRIDGE_X,	0x07,	0x712 },
};

static bool bad_spectre_microcode(struct cpuinfo_x86 *c)
{
	int i;

	/*
	 * We know that the hypervisor lie to us on the microcode version so
	 * we may as well hope that it is running the correct version.
	 */
	if (cpu_has(c, X86_FEATURE_HYPERVISOR))
		return false;

	for (i = 0; i < ARRAY_SIZE(spectre_bad_microcodes); i++) {
		if (c->x86_vfm == spectre_bad_microcodes[i].vfm &&
		    c->x86_stepping == spectre_bad_microcodes[i].stepping)
			return (c->microcode <= spectre_bad_microcodes[i].microcode);
	}
	return false;
}

#define MSR_IA32_TME_ACTIVATE		0x982

/* Helpers to access TME_ACTIVATE MSR */
#define TME_ACTIVATE_LOCKED(x)		(x & 0x1)
#define TME_ACTIVATE_ENABLED(x)		(x & 0x2)

#define TME_ACTIVATE_KEYID_BITS(x)	((x >> 32) & 0xf)	/* Bits 35:32 */

static void detect_tme_early(struct cpuinfo_x86 *c)
{
	u64 tme_activate;
	int keyid_bits;

	rdmsrl(MSR_IA32_TME_ACTIVATE, tme_activate);

	if (!TME_ACTIVATE_LOCKED(tme_activate) || !TME_ACTIVATE_ENABLED(tme_activate)) {
		pr_info_once("x86/tme: not enabled by BIOS\n");
		clear_cpu_cap(c, X86_FEATURE_TME);
		return;
	}
	pr_info_once("x86/tme: enabled by BIOS\n");
	keyid_bits = TME_ACTIVATE_KEYID_BITS(tme_activate);
	if (!keyid_bits)
		return;

	/*
	 * KeyID bits are set by BIOS and can be present regardless
	 * of whether the kernel is using them. They effectively lower
	 * the number of physical address bits.
	 *
	 * Update cpuinfo_x86::x86_phys_bits accordingly.
	 */
	c->x86_phys_bits -= keyid_bits;
	pr_info_once("x86/mktme: BIOS enabled: x86_phys_bits reduced by %d\n",
		     keyid_bits);
}

void intel_unlock_cpuid_leafs(struct cpuinfo_x86 *c)
{
	if (boot_cpu_data.x86_vendor != X86_VENDOR_INTEL)
		return;

	if (c->x86 < 6 || (c->x86 == 6 && c->x86_model < 0xd))
		return;

	/*
	 * The BIOS can have limited CPUID to leaf 2, which breaks feature
	 * enumeration. Unlock it and update the maximum leaf info.
	 */
	if (msr_clear_bit(MSR_IA32_MISC_ENABLE, MSR_IA32_MISC_ENABLE_LIMIT_CPUID_BIT) > 0)
		c->cpuid_level = cpuid_eax(0);
}

static void early_init_intel(struct cpuinfo_x86 *c)
{
	u64 misc_enable;

	if ((c->x86 == 0xf && c->x86_model >= 0x03) ||
		(c->x86 == 0x6 && c->x86_model >= 0x0e))
		set_cpu_cap(c, X86_FEATURE_CONSTANT_TSC);

	if (c->x86 >= 6 && !cpu_has(c, X86_FEATURE_IA64))
		c->microcode = intel_get_microcode_revision();

	/* Now if any of them are set, check the blacklist and clear the lot */
	if ((cpu_has(c, X86_FEATURE_SPEC_CTRL) ||
	     cpu_has(c, X86_FEATURE_INTEL_STIBP) ||
	     cpu_has(c, X86_FEATURE_IBRS) || cpu_has(c, X86_FEATURE_IBPB) ||
	     cpu_has(c, X86_FEATURE_STIBP)) && bad_spectre_microcode(c)) {
		pr_warn("Intel Spectre v2 broken microcode detected; disabling Speculation Control\n");
		setup_clear_cpu_cap(X86_FEATURE_IBRS);
		setup_clear_cpu_cap(X86_FEATURE_IBPB);
		setup_clear_cpu_cap(X86_FEATURE_STIBP);
		setup_clear_cpu_cap(X86_FEATURE_SPEC_CTRL);
		setup_clear_cpu_cap(X86_FEATURE_MSR_SPEC_CTRL);
		setup_clear_cpu_cap(X86_FEATURE_INTEL_STIBP);
		setup_clear_cpu_cap(X86_FEATURE_SSBD);
		setup_clear_cpu_cap(X86_FEATURE_SPEC_CTRL_SSBD);
	}

	/*
	 * Atom erratum AAE44/AAF40/AAG38/AAH41:
	 *
	 * A race condition between speculative fetches and invalidating
	 * a large page.  This is worked around in microcode, but we
	 * need the microcode to have already been loaded... so if it is
	 * not, recommend a BIOS update and disable large pages.
	 */
	if (c->x86_vfm == INTEL_ATOM_BONNELL && c->x86_stepping <= 2 &&
	    c->microcode < 0x20e) {
		pr_warn("Atom PSE erratum detected, BIOS microcode update recommended\n");
		clear_cpu_cap(c, X86_FEATURE_PSE);
	}

#ifdef CONFIG_X86_64
	set_cpu_cap(c, X86_FEATURE_SYSENTER32);
#else
	/* Netburst reports 64 bytes clflush size, but does IO in 128 bytes */
	if (c->x86 == 15 && c->x86_cache_alignment == 64)
		c->x86_cache_alignment = 128;
#endif

	/* CPUID workaround for 0F33/0F34 CPU */
	if (c->x86 == 0xF && c->x86_model == 0x3
	    && (c->x86_stepping == 0x3 || c->x86_stepping == 0x4))
		c->x86_phys_bits = 36;

	/*
	 * c->x86_power is 8000_0007 edx. Bit 8 is TSC runs at constant rate
	 * with P/T states and does not stop in deep C-states.
	 *
	 * It is also reliable across cores and sockets. (but not across
	 * cabinets - we turn it off in that case explicitly.)
	 */
	if (c->x86_power & (1 << 8)) {
		set_cpu_cap(c, X86_FEATURE_CONSTANT_TSC);
		set_cpu_cap(c, X86_FEATURE_NONSTOP_TSC);
	}

	/* Penwell and Cloverview have the TSC which doesn't sleep on S3 */
	switch (c->x86_vfm) {
	case INTEL_ATOM_SALTWELL_MID:
	case INTEL_ATOM_SALTWELL_TABLET:
	case INTEL_ATOM_SILVERMONT_MID:
	case INTEL_ATOM_AIRMONT_NP:
		set_cpu_cap(c, X86_FEATURE_NONSTOP_TSC_S3);
		break;
	}

	/*
	 * PAT is broken on early family 6 CPUs, the last of which
	 * is "Yonah" where the erratum is named "AN7":
	 *
	 * 	Page with PAT (Page Attribute Table) Set to USWC
	 * 	(Uncacheable Speculative Write Combine) While
	 * 	Associated MTRR (Memory Type Range Register) Is UC
	 * 	(Uncacheable) May Consolidate to UC
	 *
	 * Disable PAT and fall back to MTRR on these CPUs.
	 */
	if (c->x86_vfm >= INTEL_PENTIUM_PRO &&
	    c->x86_vfm <= INTEL_CORE_YONAH)
		clear_cpu_cap(c, X86_FEATURE_PAT);

	/*
	 * If fast string is not enabled in IA32_MISC_ENABLE for any reason,
	 * clear the fast string and enhanced fast string CPU capabilities.
	 */
	if (c->x86 > 6 || (c->x86 == 6 && c->x86_model >= 0xd)) {
		rdmsrl(MSR_IA32_MISC_ENABLE, misc_enable);
		if (!(misc_enable & MSR_IA32_MISC_ENABLE_FAST_STRING)) {
			pr_info("Disabled fast string operations\n");
			setup_clear_cpu_cap(X86_FEATURE_REP_GOOD);
			setup_clear_cpu_cap(X86_FEATURE_ERMS);
		}
	}

	/*
	 * Intel Quark Core DevMan_001.pdf section 6.4.11
	 * "The operating system also is required to invalidate (i.e., flush)
	 *  the TLB when any changes are made to any of the page table entries.
	 *  The operating system must reload CR3 to cause the TLB to be flushed"
	 *
	 * As a result, boot_cpu_has(X86_FEATURE_PGE) in arch/x86/include/asm/tlbflush.h
	 * should be false so that __flush_tlb_all() causes CR3 instead of CR4.PGE
	 * to be modified.
	 */
	if (c->x86_vfm == INTEL_QUARK_X1000) {
		pr_info("Disabling PGE capability bit\n");
		setup_clear_cpu_cap(X86_FEATURE_PGE);
	}

	check_memory_type_self_snoop_errata(c);

	/*
	 * Adjust the number of physical bits early because it affects the
	 * valid bits of the MTRR mask registers.
	 */
	if (cpu_has(c, X86_FEATURE_TME))
		detect_tme_early(c);
}

static void bsp_init_intel(struct cpuinfo_x86 *c)
{
	resctrl_cpu_detect(c);
}

#ifdef CONFIG_X86_32
/*
 *	Early probe support logic for ppro memory erratum #50
 *
 *	This is called before we do cpu ident work
 */

int ppro_with_ram_bug(void)
{
	/* Uses data from early_cpu_detect now */
	if (boot_cpu_data.x86_vendor == X86_VENDOR_INTEL &&
	    boot_cpu_data.x86 == 6 &&
	    boot_cpu_data.x86_model == 1 &&
	    boot_cpu_data.x86_stepping < 8) {
		pr_info("Pentium Pro with Errata#50 detected. Taking evasive action.\n");
		return 1;
	}
	return 0;
}

static void intel_smp_check(struct cpuinfo_x86 *c)
{
	/* calling is from identify_secondary_cpu() ? */
	if (!c->cpu_index)
		return;

	/*
	 * Mask B, Pentium, but not Pentium MMX
	 */
	if (c->x86 == 5 &&
	    c->x86_stepping >= 1 && c->x86_stepping <= 4 &&
	    c->x86_model <= 3) {
		/*
		 * Remember we have B step Pentia with bugs
		 */
		WARN_ONCE(1, "WARNING: SMP operation may be unreliable"
				    "with B stepping processors.\n");
	}
}

static int forcepae;
static int __init forcepae_setup(char *__unused)
{
	forcepae = 1;
	return 1;
}
__setup("forcepae", forcepae_setup);

static void intel_workarounds(struct cpuinfo_x86 *c)
{
#ifdef CONFIG_X86_F00F_BUG
	/*
	 * All models of Pentium and Pentium with MMX technology CPUs
	 * have the F0 0F bug, which lets nonprivileged users lock up the
	 * system. Announce that the fault handler will be checking for it.
	 * The Quark is also family 5, but does not have the same bug.
	 */
	clear_cpu_bug(c, X86_BUG_F00F);
	if (c->x86 == 5 && c->x86_model < 9) {
		static int f00f_workaround_enabled;

		set_cpu_bug(c, X86_BUG_F00F);
		if (!f00f_workaround_enabled) {
			pr_notice("Intel Pentium with F0 0F bug - workaround enabled.\n");
			f00f_workaround_enabled = 1;
		}
	}
#endif

	/*
	 * SEP CPUID bug: Pentium Pro reports SEP but doesn't have it until
	 * model 3 mask 3
	 */
	if ((c->x86<<8 | c->x86_model<<4 | c->x86_stepping) < 0x633)
		clear_cpu_cap(c, X86_FEATURE_SEP);

	/*
	 * PAE CPUID issue: many Pentium M report no PAE but may have a
	 * functionally usable PAE implementation.
	 * Forcefully enable PAE if kernel parameter "forcepae" is present.
	 */
	if (forcepae) {
		pr_warn("PAE forced!\n");
		set_cpu_cap(c, X86_FEATURE_PAE);
		add_taint(TAINT_CPU_OUT_OF_SPEC, LOCKDEP_NOW_UNRELIABLE);
	}

	/*
	 * P4 Xeon erratum 037 workaround.
	 * Hardware prefetcher may cause stale data to be loaded into the cache.
	 */
	if ((c->x86 == 15) && (c->x86_model == 1) && (c->x86_stepping == 1)) {
		if (msr_set_bit(MSR_IA32_MISC_ENABLE,
				MSR_IA32_MISC_ENABLE_PREFETCH_DISABLE_BIT) > 0) {
			pr_info("CPU: C0 stepping P4 Xeon detected.\n");
			pr_info("CPU: Disabling hardware prefetching (Erratum 037)\n");
		}
	}

	/*
	 * See if we have a good local APIC by checking for buggy Pentia,
	 * i.e. all B steppings and the C2 stepping of P54C when using their
	 * integrated APIC (see 11AP erratum in "Pentium Processor
	 * Specification Update").
	 */
	if (boot_cpu_has(X86_FEATURE_APIC) && (c->x86<<8 | c->x86_model<<4) == 0x520 &&
	    (c->x86_stepping < 0x6 || c->x86_stepping == 0xb))
		set_cpu_bug(c, X86_BUG_11AP);


#ifdef CONFIG_X86_INTEL_USERCOPY
	/*
	 * Set up the preferred alignment for movsl bulk memory moves
	 */
	switch (c->x86) {
	case 4:		/* 486: untested */
		break;
	case 5:		/* Old Pentia: untested */
		break;
	case 6:		/* PII/PIII only like movsl with 8-byte alignment */
		movsl_mask.mask = 7;
		break;
	case 15:	/* P4 is OK down to 8-byte alignment */
		movsl_mask.mask = 7;
		break;
	}
#endif

	intel_smp_check(c);
}
#else
static void intel_workarounds(struct cpuinfo_x86 *c)
{
}
#endif

static void srat_detect_node(struct cpuinfo_x86 *c)
{
#ifdef CONFIG_NUMA
	unsigned node;
	int cpu = smp_processor_id();

	/* Don't do the funky fallback heuristics the AMD version employs
	   for now. */
	node = numa_cpu_node(cpu);
	if (node == NUMA_NO_NODE || !node_online(node)) {
		/* reuse the value from init_cpu_to_node() */
		node = cpu_to_node(cpu);
	}
	numa_set_node(cpu, node);
#endif
}

static void init_cpuid_fault(struct cpuinfo_x86 *c)
{
	u64 msr;

	if (!rdmsrl_safe(MSR_PLATFORM_INFO, &msr)) {
		if (msr & MSR_PLATFORM_INFO_CPUID_FAULT)
			set_cpu_cap(c, X86_FEATURE_CPUID_FAULT);
	}
}

static void init_intel_misc_features(struct cpuinfo_x86 *c)
{
	u64 msr;

	if (rdmsrl_safe(MSR_MISC_FEATURES_ENABLES, &msr))
		return;

	/* Clear all MISC features */
	this_cpu_write(msr_misc_features_shadow, 0);

	/* Check features and update capabilities and shadow control bits */
	init_cpuid_fault(c);
	probe_xeon_phi_r3mwait(c);

	msr = this_cpu_read(msr_misc_features_shadow);
	wrmsrl(MSR_MISC_FEATURES_ENABLES, msr);
}

static void init_intel(struct cpuinfo_x86 *c)
{
	early_init_intel(c);

	intel_workarounds(c);

	init_intel_cacheinfo(c);

	if (c->cpuid_level > 9) {
		unsigned eax = cpuid_eax(10);
		/* Check for version and the number of counters */
		if ((eax & 0xff) && (((eax>>8) & 0xff) > 1))
			set_cpu_cap(c, X86_FEATURE_ARCH_PERFMON);
	}

	if (cpu_has(c, X86_FEATURE_XMM2))
		set_cpu_cap(c, X86_FEATURE_LFENCE_RDTSC);

	if (boot_cpu_has(X86_FEATURE_DS)) {
		unsigned int l1, l2;

		rdmsr(MSR_IA32_MISC_ENABLE, l1, l2);
		if (!(l1 & MSR_IA32_MISC_ENABLE_BTS_UNAVAIL))
			set_cpu_cap(c, X86_FEATURE_BTS);
		if (!(l1 & MSR_IA32_MISC_ENABLE_PEBS_UNAVAIL))
			set_cpu_cap(c, X86_FEATURE_PEBS);
	}

	if (boot_cpu_has(X86_FEATURE_CLFLUSH) &&
	    (c->x86_vfm == INTEL_CORE2_DUNNINGTON ||
	     c->x86_vfm == INTEL_NEHALEM_EX ||
	     c->x86_vfm == INTEL_WESTMERE_EX))
		set_cpu_bug(c, X86_BUG_CLFLUSH_MONITOR);

	if (boot_cpu_has(X86_FEATURE_MWAIT) && c->x86_vfm == INTEL_ATOM_GOLDMONT)
		set_cpu_bug(c, X86_BUG_MONITOR);

#ifdef CONFIG_X86_64
	if (c->x86 == 15)
		c->x86_cache_alignment = c->x86_clflush_size * 2;
	if (c->x86 == 6)
		set_cpu_cap(c, X86_FEATURE_REP_GOOD);
#else
	/*
	 * Names for the Pentium II/Celeron processors
	 * detectable only by also checking the cache size.
	 * Dixon is NOT a Celeron.
	 */
	if (c->x86 == 6) {
		unsigned int l2 = c->x86_cache_size;
		char *p = NULL;

		switch (c->x86_model) {
		case 5:
			if (l2 == 0)
				p = "Celeron (Covington)";
			else if (l2 == 256)
				p = "Mobile Pentium II (Dixon)";
			break;

		case 6:
			if (l2 == 128)
				p = "Celeron (Mendocino)";
			else if (c->x86_stepping == 0 || c->x86_stepping == 5)
				p = "Celeron-A";
			break;

		case 8:
			if (l2 == 128)
				p = "Celeron (Coppermine)";
			break;
		}

		if (p)
			strcpy(c->x86_model_id, p);
	}

	if (c->x86 == 15)
		set_cpu_cap(c, X86_FEATURE_P4);
	if (c->x86 == 6)
		set_cpu_cap(c, X86_FEATURE_P3);
#endif

	/* Work around errata */
	srat_detect_node(c);

	init_ia32_feat_ctl(c);

	init_intel_misc_features(c);

	split_lock_init();

	intel_init_thermal(c);
}

#ifdef CONFIG_X86_32
static unsigned int intel_size_cache(struct cpuinfo_x86 *c, unsigned int size)
{
	/*
	 * Intel PIII Tualatin. This comes in two flavours.
	 * One has 256kb of cache, the other 512. We have no way
	 * to determine which, so we use a boottime override
	 * for the 512kb model, and assume 256 otherwise.
	 */
	if ((c->x86 == 6) && (c->x86_model == 11) && (size == 0))
		size = 256;

	/*
	 * Intel Quark SoC X1000 contains a 4-way set associative
	 * 16K cache with a 16 byte cache line and 256 lines per tag
	 */
	if ((c->x86 == 5) && (c->x86_model == 9))
		size = 16;
	return size;
}
#endif

#define TLB_INST_4K	0x01
#define TLB_INST_4M	0x02
#define TLB_INST_2M_4M	0x03

#define TLB_INST_ALL	0x05
#define TLB_INST_1G	0x06

#define TLB_DATA_4K	0x11
#define TLB_DATA_4M	0x12
#define TLB_DATA_2M_4M	0x13
#define TLB_DATA_4K_4M	0x14

#define TLB_DATA_1G	0x16

#define TLB_DATA0_4K	0x21
#define TLB_DATA0_4M	0x22
#define TLB_DATA0_2M_4M	0x23

#define STLB_4K		0x41
#define STLB_4K_2M	0x42

static const struct _tlb_table intel_tlb_table[] = {
	{ 0x01, TLB_INST_4K,		32,	" TLB_INST 4 KByte pages, 4-way set associative" },
	{ 0x02, TLB_INST_4M,		2,	" TLB_INST 4 MByte pages, full associative" },
	{ 0x03, TLB_DATA_4K,		64,	" TLB_DATA 4 KByte pages, 4-way set associative" },
	{ 0x04, TLB_DATA_4M,		8,	" TLB_DATA 4 MByte pages, 4-way set associative" },
	{ 0x05, TLB_DATA_4M,		32,	" TLB_DATA 4 MByte pages, 4-way set associative" },
	{ 0x0b, TLB_INST_4M,		4,	" TLB_INST 4 MByte pages, 4-way set associative" },
	{ 0x4f, TLB_INST_4K,		32,	" TLB_INST 4 KByte pages" },
	{ 0x50, TLB_INST_ALL,		64,	" TLB_INST 4 KByte and 2-MByte or 4-MByte pages" },
	{ 0x51, TLB_INST_ALL,		128,	" TLB_INST 4 KByte and 2-MByte or 4-MByte pages" },
	{ 0x52, TLB_INST_ALL,		256,	" TLB_INST 4 KByte and 2-MByte or 4-MByte pages" },
	{ 0x55, TLB_INST_2M_4M,		7,	" TLB_INST 2-MByte or 4-MByte pages, fully associative" },
	{ 0x56, TLB_DATA0_4M,		16,	" TLB_DATA0 4 MByte pages, 4-way set associative" },
	{ 0x57, TLB_DATA0_4K,		16,	" TLB_DATA0 4 KByte pages, 4-way associative" },
	{ 0x59, TLB_DATA0_4K,		16,	" TLB_DATA0 4 KByte pages, fully associative" },
	{ 0x5a, TLB_DATA0_2M_4M,	32,	" TLB_DATA0 2-MByte or 4 MByte pages, 4-way set associative" },
	{ 0x5b, TLB_DATA_4K_4M,		64,	" TLB_DATA 4 KByte and 4 MByte pages" },
	{ 0x5c, TLB_DATA_4K_4M,		128,	" TLB_DATA 4 KByte and 4 MByte pages" },
	{ 0x5d, TLB_DATA_4K_4M,		256,	" TLB_DATA 4 KByte and 4 MByte pages" },
	{ 0x61, TLB_INST_4K,		48,	" TLB_INST 4 KByte pages, full associative" },
	{ 0x63, TLB_DATA_1G,		4,	" TLB_DATA 1 GByte pages, 4-way set associative" },
	{ 0x6b, TLB_DATA_4K,		256,	" TLB_DATA 4 KByte pages, 8-way associative" },
	{ 0x6c, TLB_DATA_2M_4M,		128,	" TLB_DATA 2 MByte or 4 MByte pages, 8-way associative" },
	{ 0x6d, TLB_DATA_1G,		16,	" TLB_DATA 1 GByte pages, fully associative" },
	{ 0x76, TLB_INST_2M_4M,		8,	" TLB_INST 2-MByte or 4-MByte pages, fully associative" },
	{ 0xb0, TLB_INST_4K,		128,	" TLB_INST 4 KByte pages, 4-way set associative" },
	{ 0xb1, TLB_INST_2M_4M,		4,	" TLB_INST 2M pages, 4-way, 8 entries or 4M pages, 4-way entries" },
	{ 0xb2, TLB_INST_4K,		64,	" TLB_INST 4KByte pages, 4-way set associative" },
	{ 0xb3, TLB_DATA_4K,		128,	" TLB_DATA 4 KByte pages, 4-way set associative" },
	{ 0xb4, TLB_DATA_4K,		256,	" TLB_DATA 4 KByte pages, 4-way associative" },
	{ 0xb5, TLB_INST_4K,		64,	" TLB_INST 4 KByte pages, 8-way set associative" },
	{ 0xb6, TLB_INST_4K,		128,	" TLB_INST 4 KByte pages, 8-way set associative" },
	{ 0xba, TLB_DATA_4K,		64,	" TLB_DATA 4 KByte pages, 4-way associative" },
	{ 0xc0, TLB_DATA_4K_4M,		8,	" TLB_DATA 4 KByte and 4 MByte pages, 4-way associative" },
	{ 0xc1, STLB_4K_2M,		1024,	" STLB 4 KByte and 2 MByte pages, 8-way associative" },
	{ 0xc2, TLB_DATA_2M_4M,		16,	" TLB_DATA 2 MByte/4MByte pages, 4-way associative" },
	{ 0xca, STLB_4K,		512,	" STLB 4 KByte pages, 4-way associative" },
	{ 0x00, 0, 0 }
};

static void intel_tlb_lookup(const unsigned char desc)
{
	unsigned char k;
	if (desc == 0)
		return;

	/* look up this descriptor in the table */
	for (k = 0; intel_tlb_table[k].descriptor != desc &&
	     intel_tlb_table[k].descriptor != 0; k++)
		;

	if (intel_tlb_table[k].tlb_type == 0)
		return;

	switch (intel_tlb_table[k].tlb_type) {
	case STLB_4K:
		if (tlb_lli_4k[ENTRIES] < intel_tlb_table[k].entries)
			tlb_lli_4k[ENTRIES] = intel_tlb_table[k].entries;
		if (tlb_lld_4k[ENTRIES] < intel_tlb_table[k].entries)
			tlb_lld_4k[ENTRIES] = intel_tlb_table[k].entries;
		break;
	case STLB_4K_2M:
		if (tlb_lli_4k[ENTRIES] < intel_tlb_table[k].entries)
			tlb_lli_4k[ENTRIES] = intel_tlb_table[k].entries;
		if (tlb_lld_4k[ENTRIES] < intel_tlb_table[k].entries)
			tlb_lld_4k[ENTRIES] = intel_tlb_table[k].entries;
		if (tlb_lli_2m[ENTRIES] < intel_tlb_table[k].entries)
			tlb_lli_2m[ENTRIES] = intel_tlb_table[k].entries;
		if (tlb_lld_2m[ENTRIES] < intel_tlb_table[k].entries)
			tlb_lld_2m[ENTRIES] = intel_tlb_table[k].entries;
		if (tlb_lli_4m[ENTRIES] < intel_tlb_table[k].entries)
			tlb_lli_4m[ENTRIES] = intel_tlb_table[k].entries;
		if (tlb_lld_4m[ENTRIES] < intel_tlb_table[k].entries)
			tlb_lld_4m[ENTRIES] = intel_tlb_table[k].entries;
		break;
	case TLB_INST_ALL:
		if (tlb_lli_4k[ENTRIES] < intel_tlb_table[k].entries)
			tlb_lli_4k[ENTRIES] = intel_tlb_table[k].entries;
		if (tlb_lli_2m[ENTRIES] < intel_tlb_table[k].entries)
			tlb_lli_2m[ENTRIES] = intel_tlb_table[k].entries;
		if (tlb_lli_4m[ENTRIES] < intel_tlb_table[k].entries)
			tlb_lli_4m[ENTRIES] = intel_tlb_table[k].entries;
		break;
	case TLB_INST_4K:
		if (tlb_lli_4k[ENTRIES] < intel_tlb_table[k].entries)
			tlb_lli_4k[ENTRIES] = intel_tlb_table[k].entries;
		break;
	case TLB_INST_4M:
		if (tlb_lli_4m[ENTRIES] < intel_tlb_table[k].entries)
			tlb_lli_4m[ENTRIES] = intel_tlb_table[k].entries;
		break;
	case TLB_INST_2M_4M:
		if (tlb_lli_2m[ENTRIES] < intel_tlb_table[k].entries)
			tlb_lli_2m[ENTRIES] = intel_tlb_table[k].entries;
		if (tlb_lli_4m[ENTRIES] < intel_tlb_table[k].entries)
			tlb_lli_4m[ENTRIES] = intel_tlb_table[k].entries;
		break;
	case TLB_DATA_4K:
	case TLB_DATA0_4K:
		if (tlb_lld_4k[ENTRIES] < intel_tlb_table[k].entries)
			tlb_lld_4k[ENTRIES] = intel_tlb_table[k].entries;
		break;
	case TLB_DATA_4M:
	case TLB_DATA0_4M:
		if (tlb_lld_4m[ENTRIES] < intel_tlb_table[k].entries)
			tlb_lld_4m[ENTRIES] = intel_tlb_table[k].entries;
		break;
	case TLB_DATA_2M_4M:
	case TLB_DATA0_2M_4M:
		if (tlb_lld_2m[ENTRIES] < intel_tlb_table[k].entries)
			tlb_lld_2m[ENTRIES] = intel_tlb_table[k].entries;
		if (tlb_lld_4m[ENTRIES] < intel_tlb_table[k].entries)
			tlb_lld_4m[ENTRIES] = intel_tlb_table[k].entries;
		break;
	case TLB_DATA_4K_4M:
		if (tlb_lld_4k[ENTRIES] < intel_tlb_table[k].entries)
			tlb_lld_4k[ENTRIES] = intel_tlb_table[k].entries;
		if (tlb_lld_4m[ENTRIES] < intel_tlb_table[k].entries)
			tlb_lld_4m[ENTRIES] = intel_tlb_table[k].entries;
		break;
	case TLB_DATA_1G:
		if (tlb_lld_1g[ENTRIES] < intel_tlb_table[k].entries)
			tlb_lld_1g[ENTRIES] = intel_tlb_table[k].entries;
		break;
	}
}

static void intel_detect_tlb(struct cpuinfo_x86 *c)
{
	int i, j, n;
	unsigned int regs[4];
	unsigned char *desc = (unsigned char *)regs;

	if (c->cpuid_level < 2)
		return;

	/* Number of times to iterate */
	n = cpuid_eax(2) & 0xFF;

	for (i = 0 ; i < n ; i++) {
		cpuid(2, &regs[0], &regs[1], &regs[2], &regs[3]);

		/* If bit 31 is set, this is an unknown format */
		for (j = 0 ; j < 3 ; j++)
			if (regs[j] & (1 << 31))
				regs[j] = 0;

		/* Byte 0 is level count, not a descriptor */
		for (j = 1 ; j < 16 ; j++)
			intel_tlb_lookup(desc[j]);
	}
}

static const struct cpu_dev intel_cpu_dev = {
	.c_vendor	= "Intel",
	.c_ident	= { "GenuineIntel" },
#ifdef CONFIG_X86_32
	.legacy_models = {
		{ .family = 4, .model_names =
		  {
			  [0] = "486 DX-25/33",
			  [1] = "486 DX-50",
			  [2] = "486 SX",
			  [3] = "486 DX/2",
			  [4] = "486 SL",
			  [5] = "486 SX/2",
			  [7] = "486 DX/2-WB",
			  [8] = "486 DX/4",
			  [9] = "486 DX/4-WB"
		  }
		},
		{ .family = 5, .model_names =
		  {
			  [0] = "Pentium 60/66 A-step",
			  [1] = "Pentium 60/66",
			  [2] = "Pentium 75 - 200",
			  [3] = "OverDrive PODP5V83",
			  [4] = "Pentium MMX",
			  [7] = "Mobile Pentium 75 - 200",
			  [8] = "Mobile Pentium MMX",
			  [9] = "Quark SoC X1000",
		  }
		},
		{ .family = 6, .model_names =
		  {
			  [0] = "Pentium Pro A-step",
			  [1] = "Pentium Pro",
			  [3] = "Pentium II (Klamath)",
			  [4] = "Pentium II (Deschutes)",
			  [5] = "Pentium II (Deschutes)",
			  [6] = "Mobile Pentium II",
			  [7] = "Pentium III (Katmai)",
			  [8] = "Pentium III (Coppermine)",
			  [10] = "Pentium III (Cascades)",
			  [11] = "Pentium III (Tualatin)",
		  }
		},
		{ .family = 15, .model_names =
		  {
			  [0] = "Pentium 4 (Unknown)",
			  [1] = "Pentium 4 (Willamette)",
			  [2] = "Pentium 4 (Northwood)",
			  [4] = "Pentium 4 (Foster)",
			  [5] = "Pentium 4 (Foster)",
		  }
		},
	},
	.legacy_cache_size = intel_size_cache,
#endif
	.c_detect_tlb	= intel_detect_tlb,
	.c_early_init   = early_init_intel,
	.c_bsp_init	= bsp_init_intel,
	.c_init		= init_intel,
	.c_x86_vendor	= X86_VENDOR_INTEL,
};

cpu_dev_register(intel_cpu_dev);

#define X86_HYBRID_CPU_TYPE_ID_SHIFT	24

/**
 * get_this_hybrid_cpu_type() - Get the type of this hybrid CPU
 *
 * Returns the CPU type [31:24] (i.e., Atom or Core) of a CPU in
 * a hybrid processor. If the processor is not hybrid, returns 0.
 */
u8 get_this_hybrid_cpu_type(void)
{
	if (!cpu_feature_enabled(X86_FEATURE_HYBRID_CPU))
		return 0;

	return cpuid_eax(0x0000001a) >> X86_HYBRID_CPU_TYPE_ID_SHIFT;
}

/**
 * get_this_hybrid_cpu_native_id() - Get the native id of this hybrid CPU
 *
 * Returns the uarch native ID [23:0] of a CPU in a hybrid processor.
 * If the processor is not hybrid, returns 0.
 */
u32 get_this_hybrid_cpu_native_id(void)
{
	if (!cpu_feature_enabled(X86_FEATURE_HYBRID_CPU))
		return 0;

	return cpuid_eax(0x0000001a) &
	       (BIT_ULL(X86_HYBRID_CPU_TYPE_ID_SHIFT) - 1);
}
