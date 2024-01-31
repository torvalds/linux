// SPDX-License-Identifier: GPL-2.0
#include <linux/kernel.h>
#include <linux/pgtable.h>

#include <linux/string.h>
#include <linux/bitops.h>
#include <linux/smp.h>
#include <linux/sched.h>
#include <linux/sched/clock.h>
#include <linux/semaphore.h>
#include <linux/thread_info.h>
#include <linux/init.h>
#include <linux/uaccess.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#include <linux/cpuhotplug.h>

#include <asm/cpufeature.h>
#include <asm/msr.h>
#include <asm/bugs.h>
#include <asm/cpu.h>
#include <asm/intel-family.h>
#include <asm/microcode.h>
#include <asm/hwcap2.h>
#include <asm/elf.h>
#include <asm/cpu_device_id.h>
#include <asm/cmdline.h>
#include <asm/traps.h>
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

enum split_lock_detect_state {
	sld_off = 0,
	sld_warn,
	sld_fatal,
	sld_ratelimit,
};

/*
 * Default to sld_off because most systems do not support split lock detection.
 * sld_state_setup() will switch this to sld_warn on systems that support
 * split lock/bus lock detect, unless there is a command line override.
 */
static enum split_lock_detect_state sld_state __ro_after_init = sld_off;
static u64 msr_test_ctrl_cache __ro_after_init;

/*
 * With a name like MSR_TEST_CTL it should go without saying, but don't touch
 * MSR_TEST_CTL unless the CPU is one of the whitelisted models.  Writing it
 * on CPUs that do not support SLD can cause fireworks, even when writing '0'.
 */
static bool cpu_model_supports_sld __ro_after_init;

/*
 * Processors which have self-snooping capability can handle conflicting
 * memory type across CPUs by snooping its own cache. However, there exists
 * CPU models in which having conflicting memory types still leads to
 * unpredictable behavior, machine check errors, or hangs. Clear this
 * feature to prevent its use on machines with known erratas.
 */
static void check_memory_type_self_snoop_errata(struct cpuinfo_x86 *c)
{
	switch (c->x86_model) {
	case INTEL_FAM6_CORE_YONAH:
	case INTEL_FAM6_CORE2_MEROM:
	case INTEL_FAM6_CORE2_MEROM_L:
	case INTEL_FAM6_CORE2_PENRYN:
	case INTEL_FAM6_CORE2_DUNNINGTON:
	case INTEL_FAM6_NEHALEM:
	case INTEL_FAM6_NEHALEM_G:
	case INTEL_FAM6_NEHALEM_EP:
	case INTEL_FAM6_NEHALEM_EX:
	case INTEL_FAM6_WESTMERE:
	case INTEL_FAM6_WESTMERE_EP:
	case INTEL_FAM6_SANDYBRIDGE:
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
	switch (c->x86_model) {
	case INTEL_FAM6_XEON_PHI_KNL:
	case INTEL_FAM6_XEON_PHI_KNM:
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
	u8 model;
	u8 stepping;
	u32 microcode;
};
static const struct sku_microcode spectre_bad_microcodes[] = {
	{ INTEL_FAM6_KABYLAKE,		0x0B,	0x80 },
	{ INTEL_FAM6_KABYLAKE,		0x0A,	0x80 },
	{ INTEL_FAM6_KABYLAKE,		0x09,	0x80 },
	{ INTEL_FAM6_KABYLAKE_L,	0x0A,	0x80 },
	{ INTEL_FAM6_KABYLAKE_L,	0x09,	0x80 },
	{ INTEL_FAM6_SKYLAKE_X,		0x03,	0x0100013e },
	{ INTEL_FAM6_SKYLAKE_X,		0x04,	0x0200003c },
	{ INTEL_FAM6_BROADWELL,		0x04,	0x28 },
	{ INTEL_FAM6_BROADWELL_G,	0x01,	0x1b },
	{ INTEL_FAM6_BROADWELL_D,	0x02,	0x14 },
	{ INTEL_FAM6_BROADWELL_D,	0x03,	0x07000011 },
	{ INTEL_FAM6_BROADWELL_X,	0x01,	0x0b000025 },
	{ INTEL_FAM6_HASWELL_L,		0x01,	0x21 },
	{ INTEL_FAM6_HASWELL_G,		0x01,	0x18 },
	{ INTEL_FAM6_HASWELL,		0x03,	0x23 },
	{ INTEL_FAM6_HASWELL_X,		0x02,	0x3b },
	{ INTEL_FAM6_HASWELL_X,		0x04,	0x10 },
	{ INTEL_FAM6_IVYBRIDGE_X,	0x04,	0x42a },
	/* Observed in the wild */
	{ INTEL_FAM6_SANDYBRIDGE_X,	0x06,	0x61b },
	{ INTEL_FAM6_SANDYBRIDGE_X,	0x07,	0x712 },
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

	if (c->x86 != 6)
		return false;

	for (i = 0; i < ARRAY_SIZE(spectre_bad_microcodes); i++) {
		if (c->x86_model == spectre_bad_microcodes[i].model &&
		    c->x86_stepping == spectre_bad_microcodes[i].stepping)
			return (c->microcode <= spectre_bad_microcodes[i].microcode);
	}
	return false;
}

#define MSR_IA32_TME_ACTIVATE		0x982

/* Helpers to access TME_ACTIVATE MSR */
#define TME_ACTIVATE_LOCKED(x)		(x & 0x1)
#define TME_ACTIVATE_ENABLED(x)		(x & 0x2)

#define TME_ACTIVATE_POLICY(x)		((x >> 4) & 0xf)	/* Bits 7:4 */
#define TME_ACTIVATE_POLICY_AES_XTS_128	0

#define TME_ACTIVATE_KEYID_BITS(x)	((x >> 32) & 0xf)	/* Bits 35:32 */

#define TME_ACTIVATE_CRYPTO_ALGS(x)	((x >> 48) & 0xffff)	/* Bits 63:48 */
#define TME_ACTIVATE_CRYPTO_AES_XTS_128	1

/* Values for mktme_status (SW only construct) */
#define MKTME_ENABLED			0
#define MKTME_DISABLED			1
#define MKTME_UNINITIALIZED		2
static int mktme_status = MKTME_UNINITIALIZED;

static void detect_tme_early(struct cpuinfo_x86 *c)
{
	u64 tme_activate, tme_policy, tme_crypto_algs;
	int keyid_bits = 0, nr_keyids = 0;
	static u64 tme_activate_cpu0 = 0;

	rdmsrl(MSR_IA32_TME_ACTIVATE, tme_activate);

	if (mktme_status != MKTME_UNINITIALIZED) {
		if (tme_activate != tme_activate_cpu0) {
			/* Broken BIOS? */
			pr_err_once("x86/tme: configuration is inconsistent between CPUs\n");
			pr_err_once("x86/tme: MKTME is not usable\n");
			mktme_status = MKTME_DISABLED;

			/* Proceed. We may need to exclude bits from x86_phys_bits. */
		}
	} else {
		tme_activate_cpu0 = tme_activate;
	}

	if (!TME_ACTIVATE_LOCKED(tme_activate) || !TME_ACTIVATE_ENABLED(tme_activate)) {
		pr_info_once("x86/tme: not enabled by BIOS\n");
		mktme_status = MKTME_DISABLED;
		return;
	}

	if (mktme_status != MKTME_UNINITIALIZED)
		goto detect_keyid_bits;

	pr_info("x86/tme: enabled by BIOS\n");

	tme_policy = TME_ACTIVATE_POLICY(tme_activate);
	if (tme_policy != TME_ACTIVATE_POLICY_AES_XTS_128)
		pr_warn("x86/tme: Unknown policy is active: %#llx\n", tme_policy);

	tme_crypto_algs = TME_ACTIVATE_CRYPTO_ALGS(tme_activate);
	if (!(tme_crypto_algs & TME_ACTIVATE_CRYPTO_AES_XTS_128)) {
		pr_err("x86/mktme: No known encryption algorithm is supported: %#llx\n",
				tme_crypto_algs);
		mktme_status = MKTME_DISABLED;
	}
detect_keyid_bits:
	keyid_bits = TME_ACTIVATE_KEYID_BITS(tme_activate);
	nr_keyids = (1UL << keyid_bits) - 1;
	if (nr_keyids) {
		pr_info_once("x86/mktme: enabled by BIOS\n");
		pr_info_once("x86/mktme: %d KeyIDs available\n", nr_keyids);
	} else {
		pr_info_once("x86/mktme: disabled by BIOS\n");
	}

	if (mktme_status == MKTME_UNINITIALIZED) {
		/* MKTME is usable */
		mktme_status = MKTME_ENABLED;
	}

	/*
	 * KeyID bits effectively lower the number of physical address
	 * bits.  Update cpuinfo_x86::x86_phys_bits accordingly.
	 */
	c->x86_phys_bits -= keyid_bits;
}

static void early_init_intel(struct cpuinfo_x86 *c)
{
	u64 misc_enable;

	/* Unmask CPUID levels if masked: */
	if (c->x86 > 6 || (c->x86 == 6 && c->x86_model >= 0xd)) {
		if (msr_clear_bit(MSR_IA32_MISC_ENABLE,
				  MSR_IA32_MISC_ENABLE_LIMIT_CPUID_BIT) > 0) {
			c->cpuid_level = cpuid_eax(0);
			get_cpu_cap(c);
		}
	}

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
	if (c->x86 == 6 && c->x86_model == 0x1c && c->x86_stepping <= 2 &&
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
	if (c->x86 == 6) {
		switch (c->x86_model) {
		case INTEL_FAM6_ATOM_SALTWELL_MID:
		case INTEL_FAM6_ATOM_SALTWELL_TABLET:
		case INTEL_FAM6_ATOM_SILVERMONT_MID:
		case INTEL_FAM6_ATOM_AIRMONT_NP:
			set_cpu_cap(c, X86_FEATURE_NONSTOP_TSC_S3);
			break;
		default:
			break;
		}
	}

	/*
	 * There is a known erratum on Pentium III and Core Solo
	 * and Core Duo CPUs.
	 * " Page with PAT set to WC while associated MTRR is UC
	 *   may consolidate to UC "
	 * Because of this erratum, it is better to stick with
	 * setting WC in MTRR rather than using PAT on these CPUs.
	 *
	 * Enable PAT WC only on P4, Core 2 or later CPUs.
	 */
	if (c->x86 == 6 && c->x86_model < 15)
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
	if (c->x86 == 5 && c->x86_model == 9) {
		pr_info("Disabling PGE capability bit\n");
		setup_clear_cpu_cap(X86_FEATURE_PGE);
	}

	if (c->cpuid_level >= 0x00000001) {
		u32 eax, ebx, ecx, edx;

		cpuid(0x00000001, &eax, &ebx, &ecx, &edx);
		/*
		 * If HTT (EDX[28]) is set EBX[16:23] contain the number of
		 * apicids which are reserved per package. Store the resulting
		 * shift value for the package management code.
		 */
		if (edx & (1U << 28))
			c->x86_coreid_bits = get_count_order((ebx >> 16) & 0xff);
	}

	check_memory_type_self_snoop_errata(c);

	/*
	 * Get the number of SMT siblings early from the extended topology
	 * leaf, if available. Otherwise try the legacy SMT detection.
	 */
	if (detect_extended_topology_early(c) < 0)
		detect_ht_early(c);

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

static void split_lock_init(void);
static void bus_lock_init(void);

static void init_intel(struct cpuinfo_x86 *c)
{
	early_init_intel(c);

	intel_workarounds(c);

	/*
	 * Detect the extended topology information if available. This
	 * will reinitialise the initial_apicid which will be used
	 * in init_intel_cacheinfo()
	 */
	detect_extended_topology(c);

	if (!cpu_has(c, X86_FEATURE_XTOPOLOGY)) {
		/*
		 * let's use the legacy cpuid vector 0x1 and 0x4 for topology
		 * detection.
		 */
		detect_num_cpu_cores(c);
#ifdef CONFIG_X86_32
		detect_ht(c);
#endif
	}

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

	if (c->x86 == 6 && boot_cpu_has(X86_FEATURE_CLFLUSH) &&
	    (c->x86_model == 29 || c->x86_model == 46 || c->x86_model == 47))
		set_cpu_bug(c, X86_BUG_CLFLUSH_MONITOR);

	if (c->x86 == 6 && boot_cpu_has(X86_FEATURE_MWAIT) &&
		((c->x86_model == INTEL_FAM6_ATOM_GOLDMONT)))
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
	bus_lock_init();

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

#undef pr_fmt
#define pr_fmt(fmt) "x86/split lock detection: " fmt

static const struct {
	const char			*option;
	enum split_lock_detect_state	state;
} sld_options[] __initconst = {
	{ "off",	sld_off   },
	{ "warn",	sld_warn  },
	{ "fatal",	sld_fatal },
	{ "ratelimit:", sld_ratelimit },
};

static struct ratelimit_state bld_ratelimit;

static unsigned int sysctl_sld_mitigate = 1;
static DEFINE_SEMAPHORE(buslock_sem, 1);

#ifdef CONFIG_PROC_SYSCTL
static struct ctl_table sld_sysctls[] = {
	{
		.procname       = "split_lock_mitigate",
		.data           = &sysctl_sld_mitigate,
		.maxlen         = sizeof(unsigned int),
		.mode           = 0644,
		.proc_handler	= proc_douintvec_minmax,
		.extra1         = SYSCTL_ZERO,
		.extra2         = SYSCTL_ONE,
	},
	{}
};

static int __init sld_mitigate_sysctl_init(void)
{
	register_sysctl_init("kernel", sld_sysctls);
	return 0;
}

late_initcall(sld_mitigate_sysctl_init);
#endif

static inline bool match_option(const char *arg, int arglen, const char *opt)
{
	int len = strlen(opt), ratelimit;

	if (strncmp(arg, opt, len))
		return false;

	/*
	 * Min ratelimit is 1 bus lock/sec.
	 * Max ratelimit is 1000 bus locks/sec.
	 */
	if (sscanf(arg, "ratelimit:%d", &ratelimit) == 1 &&
	    ratelimit > 0 && ratelimit <= 1000) {
		ratelimit_state_init(&bld_ratelimit, HZ, ratelimit);
		ratelimit_set_flags(&bld_ratelimit, RATELIMIT_MSG_ON_RELEASE);
		return true;
	}

	return len == arglen;
}

static bool split_lock_verify_msr(bool on)
{
	u64 ctrl, tmp;

	if (rdmsrl_safe(MSR_TEST_CTRL, &ctrl))
		return false;
	if (on)
		ctrl |= MSR_TEST_CTRL_SPLIT_LOCK_DETECT;
	else
		ctrl &= ~MSR_TEST_CTRL_SPLIT_LOCK_DETECT;
	if (wrmsrl_safe(MSR_TEST_CTRL, ctrl))
		return false;
	rdmsrl(MSR_TEST_CTRL, tmp);
	return ctrl == tmp;
}

static void __init sld_state_setup(void)
{
	enum split_lock_detect_state state = sld_warn;
	char arg[20];
	int i, ret;

	if (!boot_cpu_has(X86_FEATURE_SPLIT_LOCK_DETECT) &&
	    !boot_cpu_has(X86_FEATURE_BUS_LOCK_DETECT))
		return;

	ret = cmdline_find_option(boot_command_line, "split_lock_detect",
				  arg, sizeof(arg));
	if (ret >= 0) {
		for (i = 0; i < ARRAY_SIZE(sld_options); i++) {
			if (match_option(arg, ret, sld_options[i].option)) {
				state = sld_options[i].state;
				break;
			}
		}
	}
	sld_state = state;
}

static void __init __split_lock_setup(void)
{
	if (!split_lock_verify_msr(false)) {
		pr_info("MSR access failed: Disabled\n");
		return;
	}

	rdmsrl(MSR_TEST_CTRL, msr_test_ctrl_cache);

	if (!split_lock_verify_msr(true)) {
		pr_info("MSR access failed: Disabled\n");
		return;
	}

	/* Restore the MSR to its cached value. */
	wrmsrl(MSR_TEST_CTRL, msr_test_ctrl_cache);

	setup_force_cpu_cap(X86_FEATURE_SPLIT_LOCK_DETECT);
}

/*
 * MSR_TEST_CTRL is per core, but we treat it like a per CPU MSR. Locking
 * is not implemented as one thread could undo the setting of the other
 * thread immediately after dropping the lock anyway.
 */
static void sld_update_msr(bool on)
{
	u64 test_ctrl_val = msr_test_ctrl_cache;

	if (on)
		test_ctrl_val |= MSR_TEST_CTRL_SPLIT_LOCK_DETECT;

	wrmsrl(MSR_TEST_CTRL, test_ctrl_val);
}

static void split_lock_init(void)
{
	/*
	 * #DB for bus lock handles ratelimit and #AC for split lock is
	 * disabled.
	 */
	if (sld_state == sld_ratelimit) {
		split_lock_verify_msr(false);
		return;
	}

	if (cpu_model_supports_sld)
		split_lock_verify_msr(sld_state != sld_off);
}

static void __split_lock_reenable_unlock(struct work_struct *work)
{
	sld_update_msr(true);
	up(&buslock_sem);
}

static DECLARE_DELAYED_WORK(sl_reenable_unlock, __split_lock_reenable_unlock);

static void __split_lock_reenable(struct work_struct *work)
{
	sld_update_msr(true);
}
static DECLARE_DELAYED_WORK(sl_reenable, __split_lock_reenable);

/*
 * If a CPU goes offline with pending delayed work to re-enable split lock
 * detection then the delayed work will be executed on some other CPU. That
 * handles releasing the buslock_sem, but because it executes on a
 * different CPU probably won't re-enable split lock detection. This is a
 * problem on HT systems since the sibling CPU on the same core may then be
 * left running with split lock detection disabled.
 *
 * Unconditionally re-enable detection here.
 */
static int splitlock_cpu_offline(unsigned int cpu)
{
	sld_update_msr(true);

	return 0;
}

static void split_lock_warn(unsigned long ip)
{
	struct delayed_work *work;
	int cpu;

	if (!current->reported_split_lock)
		pr_warn_ratelimited("#AC: %s/%d took a split_lock trap at address: 0x%lx\n",
				    current->comm, current->pid, ip);
	current->reported_split_lock = 1;

	if (sysctl_sld_mitigate) {
		/*
		 * misery factor #1:
		 * sleep 10ms before trying to execute split lock.
		 */
		if (msleep_interruptible(10) > 0)
			return;
		/*
		 * Misery factor #2:
		 * only allow one buslocked disabled core at a time.
		 */
		if (down_interruptible(&buslock_sem) == -EINTR)
			return;
		work = &sl_reenable_unlock;
	} else {
		work = &sl_reenable;
	}

	cpu = get_cpu();
	schedule_delayed_work_on(cpu, work, 2);

	/* Disable split lock detection on this CPU to make progress */
	sld_update_msr(false);
	put_cpu();
}

bool handle_guest_split_lock(unsigned long ip)
{
	if (sld_state == sld_warn) {
		split_lock_warn(ip);
		return true;
	}

	pr_warn_once("#AC: %s/%d %s split_lock trap at address: 0x%lx\n",
		     current->comm, current->pid,
		     sld_state == sld_fatal ? "fatal" : "bogus", ip);

	current->thread.error_code = 0;
	current->thread.trap_nr = X86_TRAP_AC;
	force_sig_fault(SIGBUS, BUS_ADRALN, NULL);
	return false;
}
EXPORT_SYMBOL_GPL(handle_guest_split_lock);

static void bus_lock_init(void)
{
	u64 val;

	if (!boot_cpu_has(X86_FEATURE_BUS_LOCK_DETECT))
		return;

	rdmsrl(MSR_IA32_DEBUGCTLMSR, val);

	if ((boot_cpu_has(X86_FEATURE_SPLIT_LOCK_DETECT) &&
	    (sld_state == sld_warn || sld_state == sld_fatal)) ||
	    sld_state == sld_off) {
		/*
		 * Warn and fatal are handled by #AC for split lock if #AC for
		 * split lock is supported.
		 */
		val &= ~DEBUGCTLMSR_BUS_LOCK_DETECT;
	} else {
		val |= DEBUGCTLMSR_BUS_LOCK_DETECT;
	}

	wrmsrl(MSR_IA32_DEBUGCTLMSR, val);
}

bool handle_user_split_lock(struct pt_regs *regs, long error_code)
{
	if ((regs->flags & X86_EFLAGS_AC) || sld_state == sld_fatal)
		return false;
	split_lock_warn(regs->ip);
	return true;
}

void handle_bus_lock(struct pt_regs *regs)
{
	switch (sld_state) {
	case sld_off:
		break;
	case sld_ratelimit:
		/* Enforce no more than bld_ratelimit bus locks/sec. */
		while (!__ratelimit(&bld_ratelimit))
			msleep(20);
		/* Warn on the bus lock. */
		fallthrough;
	case sld_warn:
		pr_warn_ratelimited("#DB: %s/%d took a bus_lock trap at address: 0x%lx\n",
				    current->comm, current->pid, regs->ip);
		break;
	case sld_fatal:
		force_sig_fault(SIGBUS, BUS_ADRALN, NULL);
		break;
	}
}

/*
 * CPU models that are known to have the per-core split-lock detection
 * feature even though they do not enumerate IA32_CORE_CAPABILITIES.
 */
static const struct x86_cpu_id split_lock_cpu_ids[] __initconst = {
	X86_MATCH_INTEL_FAM6_MODEL(ICELAKE_X,	0),
	X86_MATCH_INTEL_FAM6_MODEL(ICELAKE_L,	0),
	X86_MATCH_INTEL_FAM6_MODEL(ICELAKE_D,	0),
	{}
};

static void __init split_lock_setup(struct cpuinfo_x86 *c)
{
	const struct x86_cpu_id *m;
	u64 ia32_core_caps;

	if (boot_cpu_has(X86_FEATURE_HYPERVISOR))
		return;

	/* Check for CPUs that have support but do not enumerate it: */
	m = x86_match_cpu(split_lock_cpu_ids);
	if (m)
		goto supported;

	if (!cpu_has(c, X86_FEATURE_CORE_CAPABILITIES))
		return;

	/*
	 * Not all bits in MSR_IA32_CORE_CAPS are architectural, but
	 * MSR_IA32_CORE_CAPS_SPLIT_LOCK_DETECT is.  All CPUs that set
	 * it have split lock detection.
	 */
	rdmsrl(MSR_IA32_CORE_CAPS, ia32_core_caps);
	if (ia32_core_caps & MSR_IA32_CORE_CAPS_SPLIT_LOCK_DETECT)
		goto supported;

	/* CPU is not in the model list and does not have the MSR bit: */
	return;

supported:
	cpu_model_supports_sld = true;
	__split_lock_setup();
}

static void sld_state_show(void)
{
	if (!boot_cpu_has(X86_FEATURE_BUS_LOCK_DETECT) &&
	    !boot_cpu_has(X86_FEATURE_SPLIT_LOCK_DETECT))
		return;

	switch (sld_state) {
	case sld_off:
		pr_info("disabled\n");
		break;
	case sld_warn:
		if (boot_cpu_has(X86_FEATURE_SPLIT_LOCK_DETECT)) {
			pr_info("#AC: crashing the kernel on kernel split_locks and warning on user-space split_locks\n");
			if (cpuhp_setup_state(CPUHP_AP_ONLINE_DYN,
					      "x86/splitlock", NULL, splitlock_cpu_offline) < 0)
				pr_warn("No splitlock CPU offline handler\n");
		} else if (boot_cpu_has(X86_FEATURE_BUS_LOCK_DETECT)) {
			pr_info("#DB: warning on user-space bus_locks\n");
		}
		break;
	case sld_fatal:
		if (boot_cpu_has(X86_FEATURE_SPLIT_LOCK_DETECT)) {
			pr_info("#AC: crashing the kernel on kernel split_locks and sending SIGBUS on user-space split_locks\n");
		} else if (boot_cpu_has(X86_FEATURE_BUS_LOCK_DETECT)) {
			pr_info("#DB: sending SIGBUS on user-space bus_locks%s\n",
				boot_cpu_has(X86_FEATURE_SPLIT_LOCK_DETECT) ?
				" from non-WB" : "");
		}
		break;
	case sld_ratelimit:
		if (boot_cpu_has(X86_FEATURE_BUS_LOCK_DETECT))
			pr_info("#DB: setting system wide bus lock rate limit to %u/sec\n", bld_ratelimit.burst);
		break;
	}
}

void __init sld_setup(struct cpuinfo_x86 *c)
{
	split_lock_setup(c);
	sld_state_setup();
	sld_state_show();
}

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
