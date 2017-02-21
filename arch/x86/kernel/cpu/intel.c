#include <linux/kernel.h>

#include <linux/string.h>
#include <linux/bitops.h>
#include <linux/smp.h>
#include <linux/sched.h>
#include <linux/thread_info.h>
#include <linux/init.h>
#include <linux/uaccess.h>

#include <asm/cpufeature.h>
#include <asm/pgtable.h>
#include <asm/msr.h>
#include <asm/bugs.h>
#include <asm/cpu.h>
#include <asm/intel-family.h>
#include <asm/microcode_intel.h>
#include <asm/hwcap2.h>
#include <asm/elf.h>

#ifdef CONFIG_X86_64
#include <linux/topology.h>
#endif

#include "cpu.h"

#ifdef CONFIG_X86_LOCAL_APIC
#include <asm/mpspec.h>
#include <asm/apic.h>
#endif

/*
 * Just in case our CPU detection goes bad, or you have a weird system,
 * allow a way to override the automatic disabling of MPX.
 */
static int forcempx;

static int __init forcempx_setup(char *__unused)
{
	forcempx = 1;

	return 1;
}
__setup("intel-skd-046-workaround=disable", forcempx_setup);

void check_mpx_erratum(struct cpuinfo_x86 *c)
{
	if (forcempx)
		return;
	/*
	 * Turn off the MPX feature on CPUs where SMEP is not
	 * available or disabled.
	 *
	 * Works around Intel Erratum SKD046: "Branch Instructions
	 * May Initialize MPX Bound Registers Incorrectly".
	 *
	 * This might falsely disable MPX on systems without
	 * SMEP, like Atom processors without SMEP.  But there
	 * is no such hardware known at the moment.
	 */
	if (cpu_has(c, X86_FEATURE_MPX) && !cpu_has(c, X86_FEATURE_SMEP)) {
		setup_clear_cpu_cap(X86_FEATURE_MPX);
		pr_warn("x86/mpx: Disabling MPX since SMEP not present\n");
	}
}

static bool ring3mwait_disabled __read_mostly;

static int __init ring3mwait_disable(char *__unused)
{
	ring3mwait_disabled = true;
	return 0;
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

	if (ring3mwait_disabled) {
		msr_clear_bit(MSR_MISC_FEATURE_ENABLES,
			      MSR_MISC_FEATURE_ENABLES_RING3MWAIT_BIT);
		return;
	}

	msr_set_bit(MSR_MISC_FEATURE_ENABLES,
		    MSR_MISC_FEATURE_ENABLES_RING3MWAIT_BIT);

	set_cpu_cap(c, X86_FEATURE_RING3MWAIT);

	if (c == &boot_cpu_data)
		ELF_HWCAP2 |= HWCAP2_RING3MWAIT;
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

	/*
	 * Atom erratum AAE44/AAF40/AAG38/AAH41:
	 *
	 * A race condition between speculative fetches and invalidating
	 * a large page.  This is worked around in microcode, but we
	 * need the microcode to have already been loaded... so if it is
	 * not, recommend a BIOS update and disable large pages.
	 */
	if (c->x86 == 6 && c->x86_model == 0x1c && c->x86_mask <= 2 &&
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
	    && (c->x86_mask == 0x3 || c->x86_mask == 0x4))
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
		if (check_tsc_unstable())
			clear_sched_clock_stable();
	} else {
		clear_sched_clock_stable();
	}

	/* Penwell and Cloverview have the TSC which doesn't sleep on S3 */
	if (c->x86 == 6) {
		switch (c->x86_model) {
		case 0x27:	/* Penwell */
		case 0x35:	/* Cloverview */
		case 0x4a:	/* Merrifield */
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

#ifdef CONFIG_KMEMCHECK
	/*
	 * P4s have a "fast strings" feature which causes single-
	 * stepping REP instructions to only generate a #DB on
	 * cache-line boundaries.
	 *
	 * Ingo Molnar reported a Pentium D (model 6) and a Xeon
	 * (model 2) with the same problem.
	 */
	if (c->x86 == 15)
		if (msr_clear_bit(MSR_IA32_MISC_ENABLE,
				  MSR_IA32_MISC_ENABLE_FAST_STRING_BIT) > 0)
			pr_info("kmemcheck: Disabling fast string operations\n");
#endif

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
	 * should be false so that __flush_tlb_all() causes CR3 insted of CR4.PGE
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

	check_mpx_erratum(c);
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
	    boot_cpu_data.x86_mask < 8) {
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
	    c->x86_mask >= 1 && c->x86_mask <= 4 &&
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
	if ((c->x86<<8 | c->x86_model<<4 | c->x86_mask) < 0x633)
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
	if ((c->x86 == 15) && (c->x86_model == 1) && (c->x86_mask == 1)) {
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
	    (c->x86_mask < 0x6 || c->x86_mask == 0xb))
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

/*
 * find out the number of processor cores on the die
 */
static int intel_num_cpu_cores(struct cpuinfo_x86 *c)
{
	unsigned int eax, ebx, ecx, edx;

	if (!IS_ENABLED(CONFIG_SMP) || c->cpuid_level < 4)
		return 1;

	/* Intel has a non-standard dependency on %ecx for this CPUID level. */
	cpuid_count(4, 0, &eax, &ebx, &ecx, &edx);
	if (eax & 0x1f)
		return (eax >> 26) + 1;
	else
		return 1;
}

static void detect_vmx_virtcap(struct cpuinfo_x86 *c)
{
	/* Intel VMX MSR indicated features */
#define X86_VMX_FEATURE_PROC_CTLS_TPR_SHADOW	0x00200000
#define X86_VMX_FEATURE_PROC_CTLS_VNMI		0x00400000
#define X86_VMX_FEATURE_PROC_CTLS_2ND_CTLS	0x80000000
#define X86_VMX_FEATURE_PROC_CTLS2_VIRT_APIC	0x00000001
#define X86_VMX_FEATURE_PROC_CTLS2_EPT		0x00000002
#define X86_VMX_FEATURE_PROC_CTLS2_VPID		0x00000020

	u32 vmx_msr_low, vmx_msr_high, msr_ctl, msr_ctl2;

	clear_cpu_cap(c, X86_FEATURE_TPR_SHADOW);
	clear_cpu_cap(c, X86_FEATURE_VNMI);
	clear_cpu_cap(c, X86_FEATURE_FLEXPRIORITY);
	clear_cpu_cap(c, X86_FEATURE_EPT);
	clear_cpu_cap(c, X86_FEATURE_VPID);

	rdmsr(MSR_IA32_VMX_PROCBASED_CTLS, vmx_msr_low, vmx_msr_high);
	msr_ctl = vmx_msr_high | vmx_msr_low;
	if (msr_ctl & X86_VMX_FEATURE_PROC_CTLS_TPR_SHADOW)
		set_cpu_cap(c, X86_FEATURE_TPR_SHADOW);
	if (msr_ctl & X86_VMX_FEATURE_PROC_CTLS_VNMI)
		set_cpu_cap(c, X86_FEATURE_VNMI);
	if (msr_ctl & X86_VMX_FEATURE_PROC_CTLS_2ND_CTLS) {
		rdmsr(MSR_IA32_VMX_PROCBASED_CTLS2,
		      vmx_msr_low, vmx_msr_high);
		msr_ctl2 = vmx_msr_high | vmx_msr_low;
		if ((msr_ctl2 & X86_VMX_FEATURE_PROC_CTLS2_VIRT_APIC) &&
		    (msr_ctl & X86_VMX_FEATURE_PROC_CTLS_TPR_SHADOW))
			set_cpu_cap(c, X86_FEATURE_FLEXPRIORITY);
		if (msr_ctl2 & X86_VMX_FEATURE_PROC_CTLS2_EPT)
			set_cpu_cap(c, X86_FEATURE_EPT);
		if (msr_ctl2 & X86_VMX_FEATURE_PROC_CTLS2_VPID)
			set_cpu_cap(c, X86_FEATURE_VPID);
	}
}

static void init_intel_energy_perf(struct cpuinfo_x86 *c)
{
	u64 epb;

	/*
	 * Initialize MSR_IA32_ENERGY_PERF_BIAS if not already initialized.
	 * (x86_energy_perf_policy(8) is available to change it at run-time.)
	 */
	if (!cpu_has(c, X86_FEATURE_EPB))
		return;

	rdmsrl(MSR_IA32_ENERGY_PERF_BIAS, epb);
	if ((epb & 0xF) != ENERGY_PERF_BIAS_PERFORMANCE)
		return;

	pr_warn_once("ENERGY_PERF_BIAS: Set to 'normal', was 'performance'\n");
	pr_warn_once("ENERGY_PERF_BIAS: View and update with x86_energy_perf_policy(8)\n");
	epb = (epb & ~0xF) | ENERGY_PERF_BIAS_NORMAL;
	wrmsrl(MSR_IA32_ENERGY_PERF_BIAS, epb);
}

static void intel_bsp_resume(struct cpuinfo_x86 *c)
{
	/*
	 * MSR_IA32_ENERGY_PERF_BIAS is lost across suspend/resume,
	 * so reinitialize it properly like during bootup:
	 */
	init_intel_energy_perf(c);
}

static void init_intel(struct cpuinfo_x86 *c)
{
	unsigned int l2 = 0;

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
		c->x86_max_cores = intel_num_cpu_cores(c);
#ifdef CONFIG_X86_32
		detect_ht(c);
#endif
	}

	l2 = init_intel_cacheinfo(c);

	/* Detect legacy cache sizes if init_intel_cacheinfo did not */
	if (l2 == 0) {
		cpu_detect_cache_sizes(c);
		l2 = c->x86_cache_size;
	}

	if (c->cpuid_level > 9) {
		unsigned eax = cpuid_eax(10);
		/* Check for version and the number of counters */
		if ((eax & 0xff) && (((eax>>8) & 0xff) > 1))
			set_cpu_cap(c, X86_FEATURE_ARCH_PERFMON);
	}

	if (cpu_has(c, X86_FEATURE_XMM2))
		set_cpu_cap(c, X86_FEATURE_LFENCE_RDTSC);

	if (boot_cpu_has(X86_FEATURE_DS)) {
		unsigned int l1;
		rdmsr(MSR_IA32_MISC_ENABLE, l1, l2);
		if (!(l1 & (1<<11)))
			set_cpu_cap(c, X86_FEATURE_BTS);
		if (!(l1 & (1<<12)))
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
			else if (c->x86_mask == 0 || c->x86_mask == 5)
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

	if (cpu_has(c, X86_FEATURE_VMX))
		detect_vmx_virtcap(c);

	init_intel_energy_perf(c);

	probe_xeon_phi_r3mwait(c);
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
	{ 0x4f, TLB_INST_4K,		32,	" TLB_INST 4 KByte pages */" },
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
	{ 0xc2, TLB_DATA_2M_4M,		16,	" DTLB 2 MByte/4MByte pages, 4-way associative" },
	{ 0xca, STLB_4K,		512,	" STLB 4 KByte pages, 4-way associative" },
	{ 0x00, 0, 0 }
};

static void intel_tlb_lookup(const unsigned char desc)
{
	unsigned char k;
	if (desc == 0)
		return;

	/* look up this descriptor in the table */
	for (k = 0; intel_tlb_table[k].descriptor != desc && \
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
	.c_init		= init_intel,
	.c_bsp_resume	= intel_bsp_resume,
	.c_x86_vendor	= X86_VENDOR_INTEL,
};

cpu_dev_register(intel_cpu_dev);

