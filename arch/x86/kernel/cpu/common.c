#include <linux/bootmem.h>
#include <linux/linkage.h>
#include <linux/bitops.h>
#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/percpu.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/kprobes.h>
#include <linux/kgdb.h>
#include <linux/smp.h>
#include <linux/io.h>
#include <linux/syscore_ops.h>

#include <asm/stackprotector.h>
#include <asm/perf_event.h>
#include <asm/mmu_context.h>
#include <asm/archrandom.h>
#include <asm/hypervisor.h>
#include <asm/processor.h>
#include <asm/tlbflush.h>
#include <asm/debugreg.h>
#include <asm/sections.h>
#include <asm/vsyscall.h>
#include <linux/topology.h>
#include <linux/cpumask.h>
#include <asm/pgtable.h>
#include <linux/atomic.h>
#include <asm/proto.h>
#include <asm/setup.h>
#include <asm/apic.h>
#include <asm/desc.h>
#include <asm/fpu/internal.h>
#include <asm/mtrr.h>
#include <linux/numa.h>
#include <asm/asm.h>
#include <asm/bugs.h>
#include <asm/cpu.h>
#include <asm/mce.h>
#include <asm/msr.h>
#include <asm/pat.h>
#include <asm/microcode.h>
#include <asm/microcode_intel.h>

#ifdef CONFIG_X86_LOCAL_APIC
#include <asm/uv/uv.h>
#endif

#include "cpu.h"

/* all of these masks are initialized in setup_cpu_local_masks() */
cpumask_var_t cpu_initialized_mask;
cpumask_var_t cpu_callout_mask;
cpumask_var_t cpu_callin_mask;

/* representing cpus for which sibling maps can be computed */
cpumask_var_t cpu_sibling_setup_mask;

/* correctly size the local cpu masks */
void __init setup_cpu_local_masks(void)
{
	alloc_bootmem_cpumask_var(&cpu_initialized_mask);
	alloc_bootmem_cpumask_var(&cpu_callin_mask);
	alloc_bootmem_cpumask_var(&cpu_callout_mask);
	alloc_bootmem_cpumask_var(&cpu_sibling_setup_mask);
}

static void default_init(struct cpuinfo_x86 *c)
{
#ifdef CONFIG_X86_64
	cpu_detect_cache_sizes(c);
#else
	/* Not much we can do here... */
	/* Check if at least it has cpuid */
	if (c->cpuid_level == -1) {
		/* No cpuid. It must be an ancient CPU */
		if (c->x86 == 4)
			strcpy(c->x86_model_id, "486");
		else if (c->x86 == 3)
			strcpy(c->x86_model_id, "386");
	}
#endif
}

static const struct cpu_dev default_cpu = {
	.c_init		= default_init,
	.c_vendor	= "Unknown",
	.c_x86_vendor	= X86_VENDOR_UNKNOWN,
};

static const struct cpu_dev *this_cpu = &default_cpu;

DEFINE_PER_CPU_PAGE_ALIGNED(struct gdt_page, gdt_page) = { .gdt = {
#ifdef CONFIG_X86_64
	/*
	 * We need valid kernel segments for data and code in long mode too
	 * IRET will check the segment types  kkeil 2000/10/28
	 * Also sysret mandates a special GDT layout
	 *
	 * TLS descriptors are currently at a different place compared to i386.
	 * Hopefully nobody expects them at a fixed place (Wine?)
	 */
	[GDT_ENTRY_KERNEL32_CS]		= GDT_ENTRY_INIT(0xc09b, 0, 0xfffff),
	[GDT_ENTRY_KERNEL_CS]		= GDT_ENTRY_INIT(0xa09b, 0, 0xfffff),
	[GDT_ENTRY_KERNEL_DS]		= GDT_ENTRY_INIT(0xc093, 0, 0xfffff),
	[GDT_ENTRY_DEFAULT_USER32_CS]	= GDT_ENTRY_INIT(0xc0fb, 0, 0xfffff),
	[GDT_ENTRY_DEFAULT_USER_DS]	= GDT_ENTRY_INIT(0xc0f3, 0, 0xfffff),
	[GDT_ENTRY_DEFAULT_USER_CS]	= GDT_ENTRY_INIT(0xa0fb, 0, 0xfffff),
#else
	[GDT_ENTRY_KERNEL_CS]		= GDT_ENTRY_INIT(0xc09a, 0, 0xfffff),
	[GDT_ENTRY_KERNEL_DS]		= GDT_ENTRY_INIT(0xc092, 0, 0xfffff),
	[GDT_ENTRY_DEFAULT_USER_CS]	= GDT_ENTRY_INIT(0xc0fa, 0, 0xfffff),
	[GDT_ENTRY_DEFAULT_USER_DS]	= GDT_ENTRY_INIT(0xc0f2, 0, 0xfffff),
	/*
	 * Segments used for calling PnP BIOS have byte granularity.
	 * They code segments and data segments have fixed 64k limits,
	 * the transfer segment sizes are set at run time.
	 */
	/* 32-bit code */
	[GDT_ENTRY_PNPBIOS_CS32]	= GDT_ENTRY_INIT(0x409a, 0, 0xffff),
	/* 16-bit code */
	[GDT_ENTRY_PNPBIOS_CS16]	= GDT_ENTRY_INIT(0x009a, 0, 0xffff),
	/* 16-bit data */
	[GDT_ENTRY_PNPBIOS_DS]		= GDT_ENTRY_INIT(0x0092, 0, 0xffff),
	/* 16-bit data */
	[GDT_ENTRY_PNPBIOS_TS1]		= GDT_ENTRY_INIT(0x0092, 0, 0),
	/* 16-bit data */
	[GDT_ENTRY_PNPBIOS_TS2]		= GDT_ENTRY_INIT(0x0092, 0, 0),
	/*
	 * The APM segments have byte granularity and their bases
	 * are set at run time.  All have 64k limits.
	 */
	/* 32-bit code */
	[GDT_ENTRY_APMBIOS_BASE]	= GDT_ENTRY_INIT(0x409a, 0, 0xffff),
	/* 16-bit code */
	[GDT_ENTRY_APMBIOS_BASE+1]	= GDT_ENTRY_INIT(0x009a, 0, 0xffff),
	/* data */
	[GDT_ENTRY_APMBIOS_BASE+2]	= GDT_ENTRY_INIT(0x4092, 0, 0xffff),

	[GDT_ENTRY_ESPFIX_SS]		= GDT_ENTRY_INIT(0xc092, 0, 0xfffff),
	[GDT_ENTRY_PERCPU]		= GDT_ENTRY_INIT(0xc092, 0, 0xfffff),
	GDT_STACK_CANARY_INIT
#endif
} };
EXPORT_PER_CPU_SYMBOL_GPL(gdt_page);

static int __init x86_mpx_setup(char *s)
{
	/* require an exact match without trailing characters */
	if (strlen(s))
		return 0;

	/* do not emit a message if the feature is not present */
	if (!boot_cpu_has(X86_FEATURE_MPX))
		return 1;

	setup_clear_cpu_cap(X86_FEATURE_MPX);
	pr_info("nompx: Intel Memory Protection Extensions (MPX) disabled\n");
	return 1;
}
__setup("nompx", x86_mpx_setup);

static int __init x86_noinvpcid_setup(char *s)
{
	/* noinvpcid doesn't accept parameters */
	if (s)
		return -EINVAL;

	/* do not emit a message if the feature is not present */
	if (!boot_cpu_has(X86_FEATURE_INVPCID))
		return 0;

	setup_clear_cpu_cap(X86_FEATURE_INVPCID);
	pr_info("noinvpcid: INVPCID feature disabled\n");
	return 0;
}
early_param("noinvpcid", x86_noinvpcid_setup);

#ifdef CONFIG_X86_32
static int cachesize_override = -1;
static int disable_x86_serial_nr = 1;

static int __init cachesize_setup(char *str)
{
	get_option(&str, &cachesize_override);
	return 1;
}
__setup("cachesize=", cachesize_setup);

static int __init x86_sep_setup(char *s)
{
	setup_clear_cpu_cap(X86_FEATURE_SEP);
	return 1;
}
__setup("nosep", x86_sep_setup);

/* Standard macro to see if a specific flag is changeable */
static inline int flag_is_changeable_p(u32 flag)
{
	u32 f1, f2;

	/*
	 * Cyrix and IDT cpus allow disabling of CPUID
	 * so the code below may return different results
	 * when it is executed before and after enabling
	 * the CPUID. Add "volatile" to not allow gcc to
	 * optimize the subsequent calls to this function.
	 */
	asm volatile ("pushfl		\n\t"
		      "pushfl		\n\t"
		      "popl %0		\n\t"
		      "movl %0, %1	\n\t"
		      "xorl %2, %0	\n\t"
		      "pushl %0		\n\t"
		      "popfl		\n\t"
		      "pushfl		\n\t"
		      "popl %0		\n\t"
		      "popfl		\n\t"

		      : "=&r" (f1), "=&r" (f2)
		      : "ir" (flag));

	return ((f1^f2) & flag) != 0;
}

/* Probe for the CPUID instruction */
int have_cpuid_p(void)
{
	return flag_is_changeable_p(X86_EFLAGS_ID);
}

static void squash_the_stupid_serial_number(struct cpuinfo_x86 *c)
{
	unsigned long lo, hi;

	if (!cpu_has(c, X86_FEATURE_PN) || !disable_x86_serial_nr)
		return;

	/* Disable processor serial number: */

	rdmsr(MSR_IA32_BBL_CR_CTL, lo, hi);
	lo |= 0x200000;
	wrmsr(MSR_IA32_BBL_CR_CTL, lo, hi);

	pr_notice("CPU serial number disabled.\n");
	clear_cpu_cap(c, X86_FEATURE_PN);

	/* Disabling the serial number may affect the cpuid level */
	c->cpuid_level = cpuid_eax(0);
}

static int __init x86_serial_nr_setup(char *s)
{
	disable_x86_serial_nr = 0;
	return 1;
}
__setup("serialnumber", x86_serial_nr_setup);
#else
static inline int flag_is_changeable_p(u32 flag)
{
	return 1;
}
static inline void squash_the_stupid_serial_number(struct cpuinfo_x86 *c)
{
}
#endif

static __init int setup_disable_smep(char *arg)
{
	setup_clear_cpu_cap(X86_FEATURE_SMEP);
	/* Check for things that depend on SMEP being enabled: */
	check_mpx_erratum(&boot_cpu_data);
	return 1;
}
__setup("nosmep", setup_disable_smep);

static __always_inline void setup_smep(struct cpuinfo_x86 *c)
{
	if (cpu_has(c, X86_FEATURE_SMEP))
		cr4_set_bits(X86_CR4_SMEP);
}

static __init int setup_disable_smap(char *arg)
{
	setup_clear_cpu_cap(X86_FEATURE_SMAP);
	return 1;
}
__setup("nosmap", setup_disable_smap);

static __always_inline void setup_smap(struct cpuinfo_x86 *c)
{
	unsigned long eflags = native_save_fl();

	/* This should have been cleared long ago */
	BUG_ON(eflags & X86_EFLAGS_AC);

	if (cpu_has(c, X86_FEATURE_SMAP)) {
#ifdef CONFIG_X86_SMAP
		cr4_set_bits(X86_CR4_SMAP);
#else
		cr4_clear_bits(X86_CR4_SMAP);
#endif
	}
}

/*
 * Protection Keys are not available in 32-bit mode.
 */
static bool pku_disabled;

static __always_inline void setup_pku(struct cpuinfo_x86 *c)
{
	/* check the boot processor, plus compile options for PKU: */
	if (!cpu_feature_enabled(X86_FEATURE_PKU))
		return;
	/* checks the actual processor's cpuid bits: */
	if (!cpu_has(c, X86_FEATURE_PKU))
		return;
	if (pku_disabled)
		return;

	cr4_set_bits(X86_CR4_PKE);
	/*
	 * Seting X86_CR4_PKE will cause the X86_FEATURE_OSPKE
	 * cpuid bit to be set.  We need to ensure that we
	 * update that bit in this CPU's "cpu_info".
	 */
	get_cpu_cap(c);
}

#ifdef CONFIG_X86_INTEL_MEMORY_PROTECTION_KEYS
static __init int setup_disable_pku(char *arg)
{
	/*
	 * Do not clear the X86_FEATURE_PKU bit.  All of the
	 * runtime checks are against OSPKE so clearing the
	 * bit does nothing.
	 *
	 * This way, we will see "pku" in cpuinfo, but not
	 * "ospke", which is exactly what we want.  It shows
	 * that the CPU has PKU, but the OS has not enabled it.
	 * This happens to be exactly how a system would look
	 * if we disabled the config option.
	 */
	pr_info("x86: 'nopku' specified, disabling Memory Protection Keys\n");
	pku_disabled = true;
	return 1;
}
__setup("nopku", setup_disable_pku);
#endif /* CONFIG_X86_64 */

/*
 * Some CPU features depend on higher CPUID levels, which may not always
 * be available due to CPUID level capping or broken virtualization
 * software.  Add those features to this table to auto-disable them.
 */
struct cpuid_dependent_feature {
	u32 feature;
	u32 level;
};

static const struct cpuid_dependent_feature
cpuid_dependent_features[] = {
	{ X86_FEATURE_MWAIT,		0x00000005 },
	{ X86_FEATURE_DCA,		0x00000009 },
	{ X86_FEATURE_XSAVE,		0x0000000d },
	{ 0, 0 }
};

static void filter_cpuid_features(struct cpuinfo_x86 *c, bool warn)
{
	const struct cpuid_dependent_feature *df;

	for (df = cpuid_dependent_features; df->feature; df++) {

		if (!cpu_has(c, df->feature))
			continue;
		/*
		 * Note: cpuid_level is set to -1 if unavailable, but
		 * extended_extended_level is set to 0 if unavailable
		 * and the legitimate extended levels are all negative
		 * when signed; hence the weird messing around with
		 * signs here...
		 */
		if (!((s32)df->level < 0 ?
		     (u32)df->level > (u32)c->extended_cpuid_level :
		     (s32)df->level > (s32)c->cpuid_level))
			continue;

		clear_cpu_cap(c, df->feature);
		if (!warn)
			continue;

		pr_warn("CPU: CPU feature " X86_CAP_FMT " disabled, no CPUID level 0x%x\n",
			x86_cap_flag(df->feature), df->level);
	}
}

/*
 * Naming convention should be: <Name> [(<Codename>)]
 * This table only is used unless init_<vendor>() below doesn't set it;
 * in particular, if CPUID levels 0x80000002..4 are supported, this
 * isn't used
 */

/* Look up CPU names by table lookup. */
static const char *table_lookup_model(struct cpuinfo_x86 *c)
{
#ifdef CONFIG_X86_32
	const struct legacy_cpu_model_info *info;

	if (c->x86_model >= 16)
		return NULL;	/* Range check */

	if (!this_cpu)
		return NULL;

	info = this_cpu->legacy_models;

	while (info->family) {
		if (info->family == c->x86)
			return info->model_names[c->x86_model];
		info++;
	}
#endif
	return NULL;		/* Not found */
}

__u32 cpu_caps_cleared[NCAPINTS];
__u32 cpu_caps_set[NCAPINTS];

void load_percpu_segment(int cpu)
{
#ifdef CONFIG_X86_32
	loadsegment(fs, __KERNEL_PERCPU);
#else
	__loadsegment_simple(gs, 0);
	wrmsrl(MSR_GS_BASE, (unsigned long)per_cpu(irq_stack_union.gs_base, cpu));
#endif
	load_stack_canary_segment();
}

/*
 * Current gdt points %fs at the "master" per-cpu area: after this,
 * it's on the real one.
 */
void switch_to_new_gdt(int cpu)
{
	struct desc_ptr gdt_descr;

	gdt_descr.address = (long)get_cpu_gdt_table(cpu);
	gdt_descr.size = GDT_SIZE - 1;
	load_gdt(&gdt_descr);
	/* Reload the per-cpu base */

	load_percpu_segment(cpu);
}

static const struct cpu_dev *cpu_devs[X86_VENDOR_NUM] = {};

static void get_model_name(struct cpuinfo_x86 *c)
{
	unsigned int *v;
	char *p, *q, *s;

	if (c->extended_cpuid_level < 0x80000004)
		return;

	v = (unsigned int *)c->x86_model_id;
	cpuid(0x80000002, &v[0], &v[1], &v[2], &v[3]);
	cpuid(0x80000003, &v[4], &v[5], &v[6], &v[7]);
	cpuid(0x80000004, &v[8], &v[9], &v[10], &v[11]);
	c->x86_model_id[48] = 0;

	/* Trim whitespace */
	p = q = s = &c->x86_model_id[0];

	while (*p == ' ')
		p++;

	while (*p) {
		/* Note the last non-whitespace index */
		if (!isspace(*p))
			s = q;

		*q++ = *p++;
	}

	*(s + 1) = '\0';
}

void cpu_detect_cache_sizes(struct cpuinfo_x86 *c)
{
	unsigned int n, dummy, ebx, ecx, edx, l2size;

	n = c->extended_cpuid_level;

	if (n >= 0x80000005) {
		cpuid(0x80000005, &dummy, &ebx, &ecx, &edx);
		c->x86_cache_size = (ecx>>24) + (edx>>24);
#ifdef CONFIG_X86_64
		/* On K8 L1 TLB is inclusive, so don't count it */
		c->x86_tlbsize = 0;
#endif
	}

	if (n < 0x80000006)	/* Some chips just has a large L1. */
		return;

	cpuid(0x80000006, &dummy, &ebx, &ecx, &edx);
	l2size = ecx >> 16;

#ifdef CONFIG_X86_64
	c->x86_tlbsize += ((ebx >> 16) & 0xfff) + (ebx & 0xfff);
#else
	/* do processor-specific cache resizing */
	if (this_cpu->legacy_cache_size)
		l2size = this_cpu->legacy_cache_size(c, l2size);

	/* Allow user to override all this if necessary. */
	if (cachesize_override != -1)
		l2size = cachesize_override;

	if (l2size == 0)
		return;		/* Again, no L2 cache is possible */
#endif

	c->x86_cache_size = l2size;
}

u16 __read_mostly tlb_lli_4k[NR_INFO];
u16 __read_mostly tlb_lli_2m[NR_INFO];
u16 __read_mostly tlb_lli_4m[NR_INFO];
u16 __read_mostly tlb_lld_4k[NR_INFO];
u16 __read_mostly tlb_lld_2m[NR_INFO];
u16 __read_mostly tlb_lld_4m[NR_INFO];
u16 __read_mostly tlb_lld_1g[NR_INFO];

static void cpu_detect_tlb(struct cpuinfo_x86 *c)
{
	if (this_cpu->c_detect_tlb)
		this_cpu->c_detect_tlb(c);

	pr_info("Last level iTLB entries: 4KB %d, 2MB %d, 4MB %d\n",
		tlb_lli_4k[ENTRIES], tlb_lli_2m[ENTRIES],
		tlb_lli_4m[ENTRIES]);

	pr_info("Last level dTLB entries: 4KB %d, 2MB %d, 4MB %d, 1GB %d\n",
		tlb_lld_4k[ENTRIES], tlb_lld_2m[ENTRIES],
		tlb_lld_4m[ENTRIES], tlb_lld_1g[ENTRIES]);
}

void detect_ht(struct cpuinfo_x86 *c)
{
#ifdef CONFIG_SMP
	u32 eax, ebx, ecx, edx;
	int index_msb, core_bits;
	static bool printed;

	if (!cpu_has(c, X86_FEATURE_HT))
		return;

	if (cpu_has(c, X86_FEATURE_CMP_LEGACY))
		goto out;

	if (cpu_has(c, X86_FEATURE_XTOPOLOGY))
		return;

	cpuid(1, &eax, &ebx, &ecx, &edx);

	smp_num_siblings = (ebx & 0xff0000) >> 16;

	if (smp_num_siblings == 1) {
		pr_info_once("CPU0: Hyper-Threading is disabled\n");
		goto out;
	}

	if (smp_num_siblings <= 1)
		goto out;

	index_msb = get_count_order(smp_num_siblings);
	c->phys_proc_id = apic->phys_pkg_id(c->initial_apicid, index_msb);

	smp_num_siblings = smp_num_siblings / c->x86_max_cores;

	index_msb = get_count_order(smp_num_siblings);

	core_bits = get_count_order(c->x86_max_cores);

	c->cpu_core_id = apic->phys_pkg_id(c->initial_apicid, index_msb) &
				       ((1 << core_bits) - 1);

out:
	if (!printed && (c->x86_max_cores * smp_num_siblings) > 1) {
		pr_info("CPU: Physical Processor ID: %d\n",
			c->phys_proc_id);
		pr_info("CPU: Processor Core ID: %d\n",
			c->cpu_core_id);
		printed = 1;
	}
#endif
}

static void get_cpu_vendor(struct cpuinfo_x86 *c)
{
	char *v = c->x86_vendor_id;
	int i;

	for (i = 0; i < X86_VENDOR_NUM; i++) {
		if (!cpu_devs[i])
			break;

		if (!strcmp(v, cpu_devs[i]->c_ident[0]) ||
		    (cpu_devs[i]->c_ident[1] &&
		     !strcmp(v, cpu_devs[i]->c_ident[1]))) {

			this_cpu = cpu_devs[i];
			c->x86_vendor = this_cpu->c_x86_vendor;
			return;
		}
	}

	pr_err_once("CPU: vendor_id '%s' unknown, using generic init.\n" \
		    "CPU: Your system may be unstable.\n", v);

	c->x86_vendor = X86_VENDOR_UNKNOWN;
	this_cpu = &default_cpu;
}

void cpu_detect(struct cpuinfo_x86 *c)
{
	/* Get vendor name */
	cpuid(0x00000000, (unsigned int *)&c->cpuid_level,
	      (unsigned int *)&c->x86_vendor_id[0],
	      (unsigned int *)&c->x86_vendor_id[8],
	      (unsigned int *)&c->x86_vendor_id[4]);

	c->x86 = 4;
	/* Intel-defined flags: level 0x00000001 */
	if (c->cpuid_level >= 0x00000001) {
		u32 junk, tfms, cap0, misc;

		cpuid(0x00000001, &tfms, &misc, &junk, &cap0);
		c->x86		= x86_family(tfms);
		c->x86_model	= x86_model(tfms);
		c->x86_mask	= x86_stepping(tfms);

		if (cap0 & (1<<19)) {
			c->x86_clflush_size = ((misc >> 8) & 0xff) * 8;
			c->x86_cache_alignment = c->x86_clflush_size;
		}
	}
}

void get_cpu_cap(struct cpuinfo_x86 *c)
{
	u32 eax, ebx, ecx, edx;

	/* Intel-defined flags: level 0x00000001 */
	if (c->cpuid_level >= 0x00000001) {
		cpuid(0x00000001, &eax, &ebx, &ecx, &edx);

		c->x86_capability[CPUID_1_ECX] = ecx;
		c->x86_capability[CPUID_1_EDX] = edx;
	}

	/* Additional Intel-defined flags: level 0x00000007 */
	if (c->cpuid_level >= 0x00000007) {
		cpuid_count(0x00000007, 0, &eax, &ebx, &ecx, &edx);

		c->x86_capability[CPUID_7_0_EBX] = ebx;

		c->x86_capability[CPUID_6_EAX] = cpuid_eax(0x00000006);
		c->x86_capability[CPUID_7_ECX] = ecx;
	}

	/* Extended state features: level 0x0000000d */
	if (c->cpuid_level >= 0x0000000d) {
		cpuid_count(0x0000000d, 1, &eax, &ebx, &ecx, &edx);

		c->x86_capability[CPUID_D_1_EAX] = eax;
	}

	/* Additional Intel-defined flags: level 0x0000000F */
	if (c->cpuid_level >= 0x0000000F) {

		/* QoS sub-leaf, EAX=0Fh, ECX=0 */
		cpuid_count(0x0000000F, 0, &eax, &ebx, &ecx, &edx);
		c->x86_capability[CPUID_F_0_EDX] = edx;

		if (cpu_has(c, X86_FEATURE_CQM_LLC)) {
			/* will be overridden if occupancy monitoring exists */
			c->x86_cache_max_rmid = ebx;

			/* QoS sub-leaf, EAX=0Fh, ECX=1 */
			cpuid_count(0x0000000F, 1, &eax, &ebx, &ecx, &edx);
			c->x86_capability[CPUID_F_1_EDX] = edx;

			if ((cpu_has(c, X86_FEATURE_CQM_OCCUP_LLC)) ||
			      ((cpu_has(c, X86_FEATURE_CQM_MBM_TOTAL)) ||
			       (cpu_has(c, X86_FEATURE_CQM_MBM_LOCAL)))) {
				c->x86_cache_max_rmid = ecx;
				c->x86_cache_occ_scale = ebx;
			}
		} else {
			c->x86_cache_max_rmid = -1;
			c->x86_cache_occ_scale = -1;
		}
	}

	/* AMD-defined flags: level 0x80000001 */
	eax = cpuid_eax(0x80000000);
	c->extended_cpuid_level = eax;

	if ((eax & 0xffff0000) == 0x80000000) {
		if (eax >= 0x80000001) {
			cpuid(0x80000001, &eax, &ebx, &ecx, &edx);

			c->x86_capability[CPUID_8000_0001_ECX] = ecx;
			c->x86_capability[CPUID_8000_0001_EDX] = edx;
		}
	}

	if (c->extended_cpuid_level >= 0x80000007) {
		cpuid(0x80000007, &eax, &ebx, &ecx, &edx);

		c->x86_capability[CPUID_8000_0007_EBX] = ebx;
		c->x86_power = edx;
	}

	if (c->extended_cpuid_level >= 0x80000008) {
		cpuid(0x80000008, &eax, &ebx, &ecx, &edx);

		c->x86_virt_bits = (eax >> 8) & 0xff;
		c->x86_phys_bits = eax & 0xff;
		c->x86_capability[CPUID_8000_0008_EBX] = ebx;
	}
#ifdef CONFIG_X86_32
	else if (cpu_has(c, X86_FEATURE_PAE) || cpu_has(c, X86_FEATURE_PSE36))
		c->x86_phys_bits = 36;
#endif

	if (c->extended_cpuid_level >= 0x8000000a)
		c->x86_capability[CPUID_8000_000A_EDX] = cpuid_edx(0x8000000a);

	init_scattered_cpuid_features(c);
}

static void identify_cpu_without_cpuid(struct cpuinfo_x86 *c)
{
#ifdef CONFIG_X86_32
	int i;

	/*
	 * First of all, decide if this is a 486 or higher
	 * It's a 486 if we can modify the AC flag
	 */
	if (flag_is_changeable_p(X86_EFLAGS_AC))
		c->x86 = 4;
	else
		c->x86 = 3;

	for (i = 0; i < X86_VENDOR_NUM; i++)
		if (cpu_devs[i] && cpu_devs[i]->c_identify) {
			c->x86_vendor_id[0] = 0;
			cpu_devs[i]->c_identify(c);
			if (c->x86_vendor_id[0]) {
				get_cpu_vendor(c);
				break;
			}
		}
#endif
}

/*
 * Do minimum CPU detection early.
 * Fields really needed: vendor, cpuid_level, family, model, mask,
 * cache alignment.
 * The others are not touched to avoid unwanted side effects.
 *
 * WARNING: this function is only called on the BP.  Don't add code here
 * that is supposed to run on all CPUs.
 */
static void __init early_identify_cpu(struct cpuinfo_x86 *c)
{
#ifdef CONFIG_X86_64
	c->x86_clflush_size = 64;
	c->x86_phys_bits = 36;
	c->x86_virt_bits = 48;
#else
	c->x86_clflush_size = 32;
	c->x86_phys_bits = 32;
	c->x86_virt_bits = 32;
#endif
	c->x86_cache_alignment = c->x86_clflush_size;

	memset(&c->x86_capability, 0, sizeof c->x86_capability);
	c->extended_cpuid_level = 0;

	if (!have_cpuid_p())
		identify_cpu_without_cpuid(c);

	/* cyrix could have cpuid enabled via c_identify()*/
	if (have_cpuid_p()) {
		cpu_detect(c);
		get_cpu_vendor(c);
		get_cpu_cap(c);

		if (this_cpu->c_early_init)
			this_cpu->c_early_init(c);

		c->cpu_index = 0;
		filter_cpuid_features(c, false);

		if (this_cpu->c_bsp_init)
			this_cpu->c_bsp_init(c);
	}

	setup_force_cpu_cap(X86_FEATURE_ALWAYS);
	fpu__init_system(c);
}

void __init early_cpu_init(void)
{
	const struct cpu_dev *const *cdev;
	int count = 0;

#ifdef CONFIG_PROCESSOR_SELECT
	pr_info("KERNEL supported cpus:\n");
#endif

	for (cdev = __x86_cpu_dev_start; cdev < __x86_cpu_dev_end; cdev++) {
		const struct cpu_dev *cpudev = *cdev;

		if (count >= X86_VENDOR_NUM)
			break;
		cpu_devs[count] = cpudev;
		count++;

#ifdef CONFIG_PROCESSOR_SELECT
		{
			unsigned int j;

			for (j = 0; j < 2; j++) {
				if (!cpudev->c_ident[j])
					continue;
				pr_info("  %s %s\n", cpudev->c_vendor,
					cpudev->c_ident[j]);
			}
		}
#endif
	}
	early_identify_cpu(&boot_cpu_data);
}

/*
 * The NOPL instruction is supposed to exist on all CPUs of family >= 6;
 * unfortunately, that's not true in practice because of early VIA
 * chips and (more importantly) broken virtualizers that are not easy
 * to detect. In the latter case it doesn't even *fail* reliably, so
 * probing for it doesn't even work. Disable it completely on 32-bit
 * unless we can find a reliable way to detect all the broken cases.
 * Enable it explicitly on 64-bit for non-constant inputs of cpu_has().
 */
static void detect_nopl(struct cpuinfo_x86 *c)
{
#ifdef CONFIG_X86_32
	clear_cpu_cap(c, X86_FEATURE_NOPL);
#else
	set_cpu_cap(c, X86_FEATURE_NOPL);
#endif
}

static void detect_null_seg_behavior(struct cpuinfo_x86 *c)
{
#ifdef CONFIG_X86_64
	/*
	 * Empirically, writing zero to a segment selector on AMD does
	 * not clear the base, whereas writing zero to a segment
	 * selector on Intel does clear the base.  Intel's behavior
	 * allows slightly faster context switches in the common case
	 * where GS is unused by the prev and next threads.
	 *
	 * Since neither vendor documents this anywhere that I can see,
	 * detect it directly instead of hardcoding the choice by
	 * vendor.
	 *
	 * I've designated AMD's behavior as the "bug" because it's
	 * counterintuitive and less friendly.
	 */

	unsigned long old_base, tmp;
	rdmsrl(MSR_FS_BASE, old_base);
	wrmsrl(MSR_FS_BASE, 1);
	loadsegment(fs, 0);
	rdmsrl(MSR_FS_BASE, tmp);
	if (tmp != 0)
		set_cpu_bug(c, X86_BUG_NULL_SEG);
	wrmsrl(MSR_FS_BASE, old_base);
#endif
}

static void generic_identify(struct cpuinfo_x86 *c)
{
	c->extended_cpuid_level = 0;

	if (!have_cpuid_p())
		identify_cpu_without_cpuid(c);

	/* cyrix could have cpuid enabled via c_identify()*/
	if (!have_cpuid_p())
		return;

	cpu_detect(c);

	get_cpu_vendor(c);

	get_cpu_cap(c);

	if (c->cpuid_level >= 0x00000001) {
		c->initial_apicid = (cpuid_ebx(1) >> 24) & 0xFF;
#ifdef CONFIG_X86_32
# ifdef CONFIG_SMP
		c->apicid = apic->phys_pkg_id(c->initial_apicid, 0);
# else
		c->apicid = c->initial_apicid;
# endif
#endif
		c->phys_proc_id = c->initial_apicid;
	}

	get_model_name(c); /* Default name */

	detect_nopl(c);

	detect_null_seg_behavior(c);

	/*
	 * ESPFIX is a strange bug.  All real CPUs have it.  Paravirt
	 * systems that run Linux at CPL > 0 may or may not have the
	 * issue, but, even if they have the issue, there's absolutely
	 * nothing we can do about it because we can't use the real IRET
	 * instruction.
	 *
	 * NB: For the time being, only 32-bit kernels support
	 * X86_BUG_ESPFIX as such.  64-bit kernels directly choose
	 * whether to apply espfix using paravirt hooks.  If any
	 * non-paravirt system ever shows up that does *not* have the
	 * ESPFIX issue, we can change this.
	 */
#ifdef CONFIG_X86_32
# ifdef CONFIG_PARAVIRT
	do {
		extern void native_iret(void);
		if (pv_cpu_ops.iret == native_iret)
			set_cpu_bug(c, X86_BUG_ESPFIX);
	} while (0);
# else
	set_cpu_bug(c, X86_BUG_ESPFIX);
# endif
#endif
}

static void x86_init_cache_qos(struct cpuinfo_x86 *c)
{
	/*
	 * The heavy lifting of max_rmid and cache_occ_scale are handled
	 * in get_cpu_cap().  Here we just set the max_rmid for the boot_cpu
	 * in case CQM bits really aren't there in this CPU.
	 */
	if (c != &boot_cpu_data) {
		boot_cpu_data.x86_cache_max_rmid =
			min(boot_cpu_data.x86_cache_max_rmid,
			    c->x86_cache_max_rmid);
	}
}

/*
 * The physical to logical package id mapping is initialized from the
 * acpi/mptables information. Make sure that CPUID actually agrees with
 * that.
 */
static void sanitize_package_id(struct cpuinfo_x86 *c)
{
#ifdef CONFIG_SMP
	unsigned int pkg, apicid, cpu = smp_processor_id();

	apicid = apic->cpu_present_to_apicid(cpu);
	pkg = apicid >> boot_cpu_data.x86_coreid_bits;

	if (apicid != c->initial_apicid) {
		pr_err(FW_BUG "CPU%u: APIC id mismatch. Firmware: %x CPUID: %x\n",
		       cpu, apicid, c->initial_apicid);
		c->initial_apicid = apicid;
	}
	if (pkg != c->phys_proc_id) {
		pr_err(FW_BUG "CPU%u: Using firmware package id %u instead of %u\n",
		       cpu, pkg, c->phys_proc_id);
		c->phys_proc_id = pkg;
	}
	c->logical_proc_id = topology_phys_to_logical_pkg(pkg);
#else
	c->logical_proc_id = 0;
#endif
}

/*
 * This does the hard work of actually picking apart the CPU stuff...
 */
static void identify_cpu(struct cpuinfo_x86 *c)
{
	int i;

	c->loops_per_jiffy = loops_per_jiffy;
	c->x86_cache_size = -1;
	c->x86_vendor = X86_VENDOR_UNKNOWN;
	c->x86_model = c->x86_mask = 0;	/* So far unknown... */
	c->x86_vendor_id[0] = '\0'; /* Unset */
	c->x86_model_id[0] = '\0';  /* Unset */
	c->x86_max_cores = 1;
	c->x86_coreid_bits = 0;
#ifdef CONFIG_X86_64
	c->x86_clflush_size = 64;
	c->x86_phys_bits = 36;
	c->x86_virt_bits = 48;
#else
	c->cpuid_level = -1;	/* CPUID not detected */
	c->x86_clflush_size = 32;
	c->x86_phys_bits = 32;
	c->x86_virt_bits = 32;
#endif
	c->x86_cache_alignment = c->x86_clflush_size;
	memset(&c->x86_capability, 0, sizeof c->x86_capability);

	generic_identify(c);

	if (this_cpu->c_identify)
		this_cpu->c_identify(c);

	/* Clear/Set all flags overridden by options, after probe */
	for (i = 0; i < NCAPINTS; i++) {
		c->x86_capability[i] &= ~cpu_caps_cleared[i];
		c->x86_capability[i] |= cpu_caps_set[i];
	}

#ifdef CONFIG_X86_64
	c->apicid = apic->phys_pkg_id(c->initial_apicid, 0);
#endif

	/*
	 * Vendor-specific initialization.  In this section we
	 * canonicalize the feature flags, meaning if there are
	 * features a certain CPU supports which CPUID doesn't
	 * tell us, CPUID claiming incorrect flags, or other bugs,
	 * we handle them here.
	 *
	 * At the end of this section, c->x86_capability better
	 * indicate the features this CPU genuinely supports!
	 */
	if (this_cpu->c_init)
		this_cpu->c_init(c);

	/* Disable the PN if appropriate */
	squash_the_stupid_serial_number(c);

	/* Set up SMEP/SMAP */
	setup_smep(c);
	setup_smap(c);

	/*
	 * The vendor-specific functions might have changed features.
	 * Now we do "generic changes."
	 */

	/* Filter out anything that depends on CPUID levels we don't have */
	filter_cpuid_features(c, true);

	/* If the model name is still unset, do table lookup. */
	if (!c->x86_model_id[0]) {
		const char *p;
		p = table_lookup_model(c);
		if (p)
			strcpy(c->x86_model_id, p);
		else
			/* Last resort... */
			sprintf(c->x86_model_id, "%02x/%02x",
				c->x86, c->x86_model);
	}

#ifdef CONFIG_X86_64
	detect_ht(c);
#endif

	init_hypervisor(c);
	x86_init_rdrand(c);
	x86_init_cache_qos(c);
	setup_pku(c);

	/*
	 * Clear/Set all flags overridden by options, need do it
	 * before following smp all cpus cap AND.
	 */
	for (i = 0; i < NCAPINTS; i++) {
		c->x86_capability[i] &= ~cpu_caps_cleared[i];
		c->x86_capability[i] |= cpu_caps_set[i];
	}

	/*
	 * On SMP, boot_cpu_data holds the common feature set between
	 * all CPUs; so make sure that we indicate which features are
	 * common between the CPUs.  The first time this routine gets
	 * executed, c == &boot_cpu_data.
	 */
	if (c != &boot_cpu_data) {
		/* AND the already accumulated flags with these */
		for (i = 0; i < NCAPINTS; i++)
			boot_cpu_data.x86_capability[i] &= c->x86_capability[i];

		/* OR, i.e. replicate the bug flags */
		for (i = NCAPINTS; i < NCAPINTS + NBUGINTS; i++)
			c->x86_capability[i] |= boot_cpu_data.x86_capability[i];
	}

	/* Init Machine Check Exception if available. */
	mcheck_cpu_init(c);

	select_idle_routine(c);

#ifdef CONFIG_NUMA
	numa_add_cpu(smp_processor_id());
#endif
	sanitize_package_id(c);
}

/*
 * Set up the CPU state needed to execute SYSENTER/SYSEXIT instructions
 * on 32-bit kernels:
 */
#ifdef CONFIG_X86_32
void enable_sep_cpu(void)
{
	struct tss_struct *tss;
	int cpu;

	if (!boot_cpu_has(X86_FEATURE_SEP))
		return;

	cpu = get_cpu();
	tss = &per_cpu(cpu_tss, cpu);

	/*
	 * We cache MSR_IA32_SYSENTER_CS's value in the TSS's ss1 field --
	 * see the big comment in struct x86_hw_tss's definition.
	 */

	tss->x86_tss.ss1 = __KERNEL_CS;
	wrmsr(MSR_IA32_SYSENTER_CS, tss->x86_tss.ss1, 0);

	wrmsr(MSR_IA32_SYSENTER_ESP,
	      (unsigned long)tss + offsetofend(struct tss_struct, SYSENTER_stack),
	      0);

	wrmsr(MSR_IA32_SYSENTER_EIP, (unsigned long)entry_SYSENTER_32, 0);

	put_cpu();
}
#endif

void __init identify_boot_cpu(void)
{
	identify_cpu(&boot_cpu_data);
	init_amd_e400_c1e_mask();
#ifdef CONFIG_X86_32
	sysenter_setup();
	enable_sep_cpu();
#endif
	cpu_detect_tlb(&boot_cpu_data);
}

void identify_secondary_cpu(struct cpuinfo_x86 *c)
{
	BUG_ON(c == &boot_cpu_data);
	identify_cpu(c);
#ifdef CONFIG_X86_32
	enable_sep_cpu();
#endif
	mtrr_ap_init();
}

struct msr_range {
	unsigned	min;
	unsigned	max;
};

static const struct msr_range msr_range_array[] = {
	{ 0x00000000, 0x00000418},
	{ 0xc0000000, 0xc000040b},
	{ 0xc0010000, 0xc0010142},
	{ 0xc0011000, 0xc001103b},
};

static void __print_cpu_msr(void)
{
	unsigned index_min, index_max;
	unsigned index;
	u64 val;
	int i;

	for (i = 0; i < ARRAY_SIZE(msr_range_array); i++) {
		index_min = msr_range_array[i].min;
		index_max = msr_range_array[i].max;

		for (index = index_min; index < index_max; index++) {
			if (rdmsrl_safe(index, &val))
				continue;
			pr_info(" MSR%08x: %016llx\n", index, val);
		}
	}
}

static int show_msr;

static __init int setup_show_msr(char *arg)
{
	int num;

	get_option(&arg, &num);

	if (num > 0)
		show_msr = num;
	return 1;
}
__setup("show_msr=", setup_show_msr);

static __init int setup_noclflush(char *arg)
{
	setup_clear_cpu_cap(X86_FEATURE_CLFLUSH);
	setup_clear_cpu_cap(X86_FEATURE_CLFLUSHOPT);
	return 1;
}
__setup("noclflush", setup_noclflush);

void print_cpu_info(struct cpuinfo_x86 *c)
{
	const char *vendor = NULL;

	if (c->x86_vendor < X86_VENDOR_NUM) {
		vendor = this_cpu->c_vendor;
	} else {
		if (c->cpuid_level >= 0)
			vendor = c->x86_vendor_id;
	}

	if (vendor && !strstr(c->x86_model_id, vendor))
		pr_cont("%s ", vendor);

	if (c->x86_model_id[0])
		pr_cont("%s", c->x86_model_id);
	else
		pr_cont("%d86", c->x86);

	pr_cont(" (family: 0x%x, model: 0x%x", c->x86, c->x86_model);

	if (c->x86_mask || c->cpuid_level >= 0)
		pr_cont(", stepping: 0x%x)\n", c->x86_mask);
	else
		pr_cont(")\n");

	print_cpu_msr(c);
}

void print_cpu_msr(struct cpuinfo_x86 *c)
{
	if (c->cpu_index < show_msr)
		__print_cpu_msr();
}

static __init int setup_disablecpuid(char *arg)
{
	int bit;

	if (get_option(&arg, &bit) && bit < NCAPINTS*32)
		setup_clear_cpu_cap(bit);
	else
		return 0;

	return 1;
}
__setup("clearcpuid=", setup_disablecpuid);

#ifdef CONFIG_X86_64
struct desc_ptr idt_descr __ro_after_init = {
	.size = NR_VECTORS * 16 - 1,
	.address = (unsigned long) idt_table,
};
const struct desc_ptr debug_idt_descr = {
	.size = NR_VECTORS * 16 - 1,
	.address = (unsigned long) debug_idt_table,
};

DEFINE_PER_CPU_FIRST(union irq_stack_union,
		     irq_stack_union) __aligned(PAGE_SIZE) __visible;

/*
 * The following percpu variables are hot.  Align current_task to
 * cacheline size such that they fall in the same cacheline.
 */
DEFINE_PER_CPU(struct task_struct *, current_task) ____cacheline_aligned =
	&init_task;
EXPORT_PER_CPU_SYMBOL(current_task);

DEFINE_PER_CPU(char *, irq_stack_ptr) =
	init_per_cpu_var(irq_stack_union.irq_stack) + IRQ_STACK_SIZE;

DEFINE_PER_CPU(unsigned int, irq_count) __visible = -1;

DEFINE_PER_CPU(int, __preempt_count) = INIT_PREEMPT_COUNT;
EXPORT_PER_CPU_SYMBOL(__preempt_count);

/*
 * Special IST stacks which the CPU switches to when it calls
 * an IST-marked descriptor entry. Up to 7 stacks (hardware
 * limit), all of them are 4K, except the debug stack which
 * is 8K.
 */
static const unsigned int exception_stack_sizes[N_EXCEPTION_STACKS] = {
	  [0 ... N_EXCEPTION_STACKS - 1]	= EXCEPTION_STKSZ,
	  [DEBUG_STACK - 1]			= DEBUG_STKSZ
};

static DEFINE_PER_CPU_PAGE_ALIGNED(char, exception_stacks
	[(N_EXCEPTION_STACKS - 1) * EXCEPTION_STKSZ + DEBUG_STKSZ]);

/* May not be marked __init: used by software suspend */
void syscall_init(void)
{
	wrmsr(MSR_STAR, 0, (__USER32_CS << 16) | __KERNEL_CS);
	wrmsrl(MSR_LSTAR, (unsigned long)entry_SYSCALL_64);

#ifdef CONFIG_IA32_EMULATION
	wrmsrl(MSR_CSTAR, (unsigned long)entry_SYSCALL_compat);
	/*
	 * This only works on Intel CPUs.
	 * On AMD CPUs these MSRs are 32-bit, CPU truncates MSR_IA32_SYSENTER_EIP.
	 * This does not cause SYSENTER to jump to the wrong location, because
	 * AMD doesn't allow SYSENTER in long mode (either 32- or 64-bit).
	 */
	wrmsrl_safe(MSR_IA32_SYSENTER_CS, (u64)__KERNEL_CS);
	wrmsrl_safe(MSR_IA32_SYSENTER_ESP, 0ULL);
	wrmsrl_safe(MSR_IA32_SYSENTER_EIP, (u64)entry_SYSENTER_compat);
#else
	wrmsrl(MSR_CSTAR, (unsigned long)ignore_sysret);
	wrmsrl_safe(MSR_IA32_SYSENTER_CS, (u64)GDT_ENTRY_INVALID_SEG);
	wrmsrl_safe(MSR_IA32_SYSENTER_ESP, 0ULL);
	wrmsrl_safe(MSR_IA32_SYSENTER_EIP, 0ULL);
#endif

	/* Flags to clear on syscall */
	wrmsrl(MSR_SYSCALL_MASK,
	       X86_EFLAGS_TF|X86_EFLAGS_DF|X86_EFLAGS_IF|
	       X86_EFLAGS_IOPL|X86_EFLAGS_AC|X86_EFLAGS_NT);
}

/*
 * Copies of the original ist values from the tss are only accessed during
 * debugging, no special alignment required.
 */
DEFINE_PER_CPU(struct orig_ist, orig_ist);

static DEFINE_PER_CPU(unsigned long, debug_stack_addr);
DEFINE_PER_CPU(int, debug_stack_usage);

int is_debug_stack(unsigned long addr)
{
	return __this_cpu_read(debug_stack_usage) ||
		(addr <= __this_cpu_read(debug_stack_addr) &&
		 addr > (__this_cpu_read(debug_stack_addr) - DEBUG_STKSZ));
}
NOKPROBE_SYMBOL(is_debug_stack);

DEFINE_PER_CPU(u32, debug_idt_ctr);

void debug_stack_set_zero(void)
{
	this_cpu_inc(debug_idt_ctr);
	load_current_idt();
}
NOKPROBE_SYMBOL(debug_stack_set_zero);

void debug_stack_reset(void)
{
	if (WARN_ON(!this_cpu_read(debug_idt_ctr)))
		return;
	if (this_cpu_dec_return(debug_idt_ctr) == 0)
		load_current_idt();
}
NOKPROBE_SYMBOL(debug_stack_reset);

#else	/* CONFIG_X86_64 */

DEFINE_PER_CPU(struct task_struct *, current_task) = &init_task;
EXPORT_PER_CPU_SYMBOL(current_task);
DEFINE_PER_CPU(int, __preempt_count) = INIT_PREEMPT_COUNT;
EXPORT_PER_CPU_SYMBOL(__preempt_count);

/*
 * On x86_32, vm86 modifies tss.sp0, so sp0 isn't a reliable way to find
 * the top of the kernel stack.  Use an extra percpu variable to track the
 * top of the kernel stack directly.
 */
DEFINE_PER_CPU(unsigned long, cpu_current_top_of_stack) =
	(unsigned long)&init_thread_union + THREAD_SIZE;
EXPORT_PER_CPU_SYMBOL(cpu_current_top_of_stack);

#ifdef CONFIG_CC_STACKPROTECTOR
DEFINE_PER_CPU_ALIGNED(struct stack_canary, stack_canary);
#endif

#endif	/* CONFIG_X86_64 */

/*
 * Clear all 6 debug registers:
 */
static void clear_all_debug_regs(void)
{
	int i;

	for (i = 0; i < 8; i++) {
		/* Ignore db4, db5 */
		if ((i == 4) || (i == 5))
			continue;

		set_debugreg(0, i);
	}
}

#ifdef CONFIG_KGDB
/*
 * Restore debug regs if using kgdbwait and you have a kernel debugger
 * connection established.
 */
static void dbg_restore_debug_regs(void)
{
	if (unlikely(kgdb_connected && arch_kgdb_ops.correct_hw_break))
		arch_kgdb_ops.correct_hw_break();
}
#else /* ! CONFIG_KGDB */
#define dbg_restore_debug_regs()
#endif /* ! CONFIG_KGDB */

static void wait_for_master_cpu(int cpu)
{
#ifdef CONFIG_SMP
	/*
	 * wait for ACK from master CPU before continuing
	 * with AP initialization
	 */
	WARN_ON(cpumask_test_and_set_cpu(cpu, cpu_initialized_mask));
	while (!cpumask_test_cpu(cpu, cpu_callout_mask))
		cpu_relax();
#endif
}

/*
 * cpu_init() initializes state that is per-CPU. Some data is already
 * initialized (naturally) in the bootstrap process, such as the GDT
 * and IDT. We reload them nevertheless, this function acts as a
 * 'CPU state barrier', nothing should get across.
 * A lot of state is already set up in PDA init for 64 bit
 */
#ifdef CONFIG_X86_64

void cpu_init(void)
{
	struct orig_ist *oist;
	struct task_struct *me;
	struct tss_struct *t;
	unsigned long v;
	int cpu = raw_smp_processor_id();
	int i;

	wait_for_master_cpu(cpu);

	/*
	 * Initialize the CR4 shadow before doing anything that could
	 * try to read it.
	 */
	cr4_init_shadow();

	/*
	 * Load microcode on this cpu if a valid microcode is available.
	 * This is early microcode loading procedure.
	 */
	load_ucode_ap();

	t = &per_cpu(cpu_tss, cpu);
	oist = &per_cpu(orig_ist, cpu);

#ifdef CONFIG_NUMA
	if (this_cpu_read(numa_node) == 0 &&
	    early_cpu_to_node(cpu) != NUMA_NO_NODE)
		set_numa_node(early_cpu_to_node(cpu));
#endif

	me = current;

	pr_debug("Initializing CPU#%d\n", cpu);

	cr4_clear_bits(X86_CR4_VME|X86_CR4_PVI|X86_CR4_TSD|X86_CR4_DE);

	/*
	 * Initialize the per-CPU GDT with the boot GDT,
	 * and set up the GDT descriptor:
	 */

	switch_to_new_gdt(cpu);
	loadsegment(fs, 0);

	load_current_idt();

	memset(me->thread.tls_array, 0, GDT_ENTRY_TLS_ENTRIES * 8);
	syscall_init();

	wrmsrl(MSR_FS_BASE, 0);
	wrmsrl(MSR_KERNEL_GS_BASE, 0);
	barrier();

	x86_configure_nx();
	x2apic_setup();

	/*
	 * set up and load the per-CPU TSS
	 */
	if (!oist->ist[0]) {
		char *estacks = per_cpu(exception_stacks, cpu);

		for (v = 0; v < N_EXCEPTION_STACKS; v++) {
			estacks += exception_stack_sizes[v];
			oist->ist[v] = t->x86_tss.ist[v] =
					(unsigned long)estacks;
			if (v == DEBUG_STACK-1)
				per_cpu(debug_stack_addr, cpu) = (unsigned long)estacks;
		}
	}

	t->x86_tss.io_bitmap_base = offsetof(struct tss_struct, io_bitmap);

	/*
	 * <= is required because the CPU will access up to
	 * 8 bits beyond the end of the IO permission bitmap.
	 */
	for (i = 0; i <= IO_BITMAP_LONGS; i++)
		t->io_bitmap[i] = ~0UL;

	atomic_inc(&init_mm.mm_count);
	me->active_mm = &init_mm;
	BUG_ON(me->mm);
	enter_lazy_tlb(&init_mm, me);

	load_sp0(t, &current->thread);
	set_tss_desc(cpu, t);
	load_TR_desc();
	load_mm_ldt(&init_mm);

	clear_all_debug_regs();
	dbg_restore_debug_regs();

	fpu__init_cpu();

	if (is_uv_system())
		uv_cpu_init();
}

#else

void cpu_init(void)
{
	int cpu = smp_processor_id();
	struct task_struct *curr = current;
	struct tss_struct *t = &per_cpu(cpu_tss, cpu);
	struct thread_struct *thread = &curr->thread;

	wait_for_master_cpu(cpu);

	/*
	 * Initialize the CR4 shadow before doing anything that could
	 * try to read it.
	 */
	cr4_init_shadow();

	show_ucode_info_early();

	pr_info("Initializing CPU#%d\n", cpu);

	if (cpu_feature_enabled(X86_FEATURE_VME) ||
	    boot_cpu_has(X86_FEATURE_TSC) ||
	    boot_cpu_has(X86_FEATURE_DE))
		cr4_clear_bits(X86_CR4_VME|X86_CR4_PVI|X86_CR4_TSD|X86_CR4_DE);

	load_current_idt();
	switch_to_new_gdt(cpu);

	/*
	 * Set up and load the per-CPU TSS and LDT
	 */
	atomic_inc(&init_mm.mm_count);
	curr->active_mm = &init_mm;
	BUG_ON(curr->mm);
	enter_lazy_tlb(&init_mm, curr);

	load_sp0(t, thread);
	set_tss_desc(cpu, t);
	load_TR_desc();
	load_mm_ldt(&init_mm);

	t->x86_tss.io_bitmap_base = offsetof(struct tss_struct, io_bitmap);

#ifdef CONFIG_DOUBLEFAULT
	/* Set up doublefault TSS pointer in the GDT */
	__set_tss_desc(cpu, GDT_ENTRY_DOUBLEFAULT_TSS, &doublefault_tss);
#endif

	clear_all_debug_regs();
	dbg_restore_debug_regs();

	fpu__init_cpu();
}
#endif

static void bsp_resume(void)
{
	if (this_cpu->c_bsp_resume)
		this_cpu->c_bsp_resume(&boot_cpu_data);
}

static struct syscore_ops cpu_syscore_ops = {
	.resume		= bsp_resume,
};

static int __init init_cpu_syscore(void)
{
	register_syscore_ops(&cpu_syscore_ops);
	return 0;
}
core_initcall(init_cpu_syscore);
