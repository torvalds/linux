#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/bootmem.h>
#include <linux/bitops.h>
#include <linux/module.h>
#include <linux/kgdb.h>
#include <linux/topology.h>
#include <linux/delay.h>
#include <linux/smp.h>
#include <linux/percpu.h>
#include <asm/i387.h>
#include <asm/msr.h>
#include <asm/io.h>
#include <asm/linkage.h>
#include <asm/mmu_context.h>
#include <asm/mtrr.h>
#include <asm/mce.h>
#include <asm/pat.h>
#include <asm/asm.h>
#include <asm/numa.h>
#include <asm/smp.h>
#ifdef CONFIG_X86_LOCAL_APIC
#include <asm/mpspec.h>
#include <asm/apic.h>
#include <mach_apic.h>
#include <asm/genapic.h>
#endif

#include <asm/pda.h>
#include <asm/pgtable.h>
#include <asm/processor.h>
#include <asm/desc.h>
#include <asm/atomic.h>
#include <asm/proto.h>
#include <asm/sections.h>
#include <asm/setup.h>
#include <asm/hypervisor.h>

#include "cpu.h"

#ifdef CONFIG_X86_64

/* all of these masks are initialized in setup_cpu_local_masks() */
cpumask_var_t cpu_callin_mask;
cpumask_var_t cpu_callout_mask;
cpumask_var_t cpu_initialized_mask;

/* representing cpus for which sibling maps can be computed */
cpumask_var_t cpu_sibling_setup_mask;

#else /* CONFIG_X86_32 */

cpumask_t cpu_callin_map;
cpumask_t cpu_callout_map;
cpumask_t cpu_initialized;
cpumask_t cpu_sibling_setup_map;

#endif /* CONFIG_X86_32 */


static struct cpu_dev *this_cpu __cpuinitdata;

#ifdef CONFIG_X86_64
/* We need valid kernel segments for data and code in long mode too
 * IRET will check the segment types  kkeil 2000/10/28
 * Also sysret mandates a special GDT layout
 */
/* The TLS descriptors are currently at a different place compared to i386.
   Hopefully nobody expects them at a fixed place (Wine?) */
DEFINE_PER_CPU(struct gdt_page, gdt_page) = { .gdt = {
	[GDT_ENTRY_KERNEL32_CS] = { { { 0x0000ffff, 0x00cf9b00 } } },
	[GDT_ENTRY_KERNEL_CS] = { { { 0x0000ffff, 0x00af9b00 } } },
	[GDT_ENTRY_KERNEL_DS] = { { { 0x0000ffff, 0x00cf9300 } } },
	[GDT_ENTRY_DEFAULT_USER32_CS] = { { { 0x0000ffff, 0x00cffb00 } } },
	[GDT_ENTRY_DEFAULT_USER_DS] = { { { 0x0000ffff, 0x00cff300 } } },
	[GDT_ENTRY_DEFAULT_USER_CS] = { { { 0x0000ffff, 0x00affb00 } } },
} };
#else
DEFINE_PER_CPU_PAGE_ALIGNED(struct gdt_page, gdt_page) = { .gdt = {
	[GDT_ENTRY_KERNEL_CS] = { { { 0x0000ffff, 0x00cf9a00 } } },
	[GDT_ENTRY_KERNEL_DS] = { { { 0x0000ffff, 0x00cf9200 } } },
	[GDT_ENTRY_DEFAULT_USER_CS] = { { { 0x0000ffff, 0x00cffa00 } } },
	[GDT_ENTRY_DEFAULT_USER_DS] = { { { 0x0000ffff, 0x00cff200 } } },
	/*
	 * Segments used for calling PnP BIOS have byte granularity.
	 * They code segments and data segments have fixed 64k limits,
	 * the transfer segment sizes are set at run time.
	 */
	/* 32-bit code */
	[GDT_ENTRY_PNPBIOS_CS32] = { { { 0x0000ffff, 0x00409a00 } } },
	/* 16-bit code */
	[GDT_ENTRY_PNPBIOS_CS16] = { { { 0x0000ffff, 0x00009a00 } } },
	/* 16-bit data */
	[GDT_ENTRY_PNPBIOS_DS] = { { { 0x0000ffff, 0x00009200 } } },
	/* 16-bit data */
	[GDT_ENTRY_PNPBIOS_TS1] = { { { 0x00000000, 0x00009200 } } },
	/* 16-bit data */
	[GDT_ENTRY_PNPBIOS_TS2] = { { { 0x00000000, 0x00009200 } } },
	/*
	 * The APM segments have byte granularity and their bases
	 * are set at run time.  All have 64k limits.
	 */
	/* 32-bit code */
	[GDT_ENTRY_APMBIOS_BASE] = { { { 0x0000ffff, 0x00409a00 } } },
	/* 16-bit code */
	[GDT_ENTRY_APMBIOS_BASE+1] = { { { 0x0000ffff, 0x00009a00 } } },
	/* data */
	[GDT_ENTRY_APMBIOS_BASE+2] = { { { 0x0000ffff, 0x00409200 } } },

	[GDT_ENTRY_ESPFIX_SS] = { { { 0x00000000, 0x00c09200 } } },
	[GDT_ENTRY_PERCPU] = { { { 0x00000000, 0x00000000 } } },
} };
#endif
EXPORT_PER_CPU_SYMBOL_GPL(gdt_page);

#ifdef CONFIG_X86_32
static int cachesize_override __cpuinitdata = -1;
static int disable_x86_serial_nr __cpuinitdata = 1;

static int __init cachesize_setup(char *str)
{
	get_option(&str, &cachesize_override);
	return 1;
}
__setup("cachesize=", cachesize_setup);

static int __init x86_fxsr_setup(char *s)
{
	setup_clear_cpu_cap(X86_FEATURE_FXSR);
	setup_clear_cpu_cap(X86_FEATURE_XMM);
	return 1;
}
__setup("nofxsr", x86_fxsr_setup);

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
	asm volatile ("pushfl\n\t"
		      "pushfl\n\t"
		      "popl %0\n\t"
		      "movl %0,%1\n\t"
		      "xorl %2,%0\n\t"
		      "pushl %0\n\t"
		      "popfl\n\t"
		      "pushfl\n\t"
		      "popl %0\n\t"
		      "popfl\n\t"
		      : "=&r" (f1), "=&r" (f2)
		      : "ir" (flag));

	return ((f1^f2) & flag) != 0;
}

/* Probe for the CPUID instruction */
static int __cpuinit have_cpuid_p(void)
{
	return flag_is_changeable_p(X86_EFLAGS_ID);
}

static void __cpuinit squash_the_stupid_serial_number(struct cpuinfo_x86 *c)
{
	if (cpu_has(c, X86_FEATURE_PN) && disable_x86_serial_nr) {
		/* Disable processor serial number */
		unsigned long lo, hi;
		rdmsr(MSR_IA32_BBL_CR_CTL, lo, hi);
		lo |= 0x200000;
		wrmsr(MSR_IA32_BBL_CR_CTL, lo, hi);
		printk(KERN_NOTICE "CPU serial number disabled.\n");
		clear_cpu_cap(c, X86_FEATURE_PN);

		/* Disabling the serial number may affect the cpuid level */
		c->cpuid_level = cpuid_eax(0);
	}
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
/* Probe for the CPUID instruction */
static inline int have_cpuid_p(void)
{
	return 1;
}
static inline void squash_the_stupid_serial_number(struct cpuinfo_x86 *c)
{
}
#endif

/*
 * Naming convention should be: <Name> [(<Codename>)]
 * This table only is used unless init_<vendor>() below doesn't set it;
 * in particular, if CPUID levels 0x80000002..4 are supported, this isn't used
 *
 */

/* Look up CPU names by table lookup. */
static char __cpuinit *table_lookup_model(struct cpuinfo_x86 *c)
{
	struct cpu_model_info *info;

	if (c->x86_model >= 16)
		return NULL;	/* Range check */

	if (!this_cpu)
		return NULL;

	info = this_cpu->c_models;

	while (info && info->family) {
		if (info->family == c->x86)
			return info->model_names[c->x86_model];
		info++;
	}
	return NULL;		/* Not found */
}

__u32 cleared_cpu_caps[NCAPINTS] __cpuinitdata;

/* Current gdt points %fs at the "master" per-cpu area: after this,
 * it's on the real one. */
void switch_to_new_gdt(void)
{
	struct desc_ptr gdt_descr;

	gdt_descr.address = (long)get_cpu_gdt_table(smp_processor_id());
	gdt_descr.size = GDT_SIZE - 1;
	load_gdt(&gdt_descr);
#ifdef CONFIG_X86_32
	asm("mov %0, %%fs" : : "r" (__KERNEL_PERCPU) : "memory");
#endif
}

static struct cpu_dev *cpu_devs[X86_VENDOR_NUM] = {};

static void __cpuinit default_init(struct cpuinfo_x86 *c)
{
#ifdef CONFIG_X86_64
	display_cacheinfo(c);
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

static struct cpu_dev __cpuinitdata default_cpu = {
	.c_init	= default_init,
	.c_vendor = "Unknown",
	.c_x86_vendor = X86_VENDOR_UNKNOWN,
};

static void __cpuinit get_model_name(struct cpuinfo_x86 *c)
{
	unsigned int *v;
	char *p, *q;

	if (c->extended_cpuid_level < 0x80000004)
		return;

	v = (unsigned int *) c->x86_model_id;
	cpuid(0x80000002, &v[0], &v[1], &v[2], &v[3]);
	cpuid(0x80000003, &v[4], &v[5], &v[6], &v[7]);
	cpuid(0x80000004, &v[8], &v[9], &v[10], &v[11]);
	c->x86_model_id[48] = 0;

	/* Intel chips right-justify this string for some dumb reason;
	   undo that brain damage */
	p = q = &c->x86_model_id[0];
	while (*p == ' ')
	     p++;
	if (p != q) {
	     while (*p)
		  *q++ = *p++;
	     while (q <= &c->x86_model_id[48])
		  *q++ = '\0';	/* Zero-pad the rest */
	}
}

void __cpuinit display_cacheinfo(struct cpuinfo_x86 *c)
{
	unsigned int n, dummy, ebx, ecx, edx, l2size;

	n = c->extended_cpuid_level;

	if (n >= 0x80000005) {
		cpuid(0x80000005, &dummy, &ebx, &ecx, &edx);
		printk(KERN_INFO "CPU: L1 I Cache: %dK (%d bytes/line), D cache %dK (%d bytes/line)\n",
				edx>>24, edx&0xFF, ecx>>24, ecx&0xFF);
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
	if (this_cpu->c_size_cache)
		l2size = this_cpu->c_size_cache(c, l2size);

	/* Allow user to override all this if necessary. */
	if (cachesize_override != -1)
		l2size = cachesize_override;

	if (l2size == 0)
		return;		/* Again, no L2 cache is possible */
#endif

	c->x86_cache_size = l2size;

	printk(KERN_INFO "CPU: L2 Cache: %dK (%d bytes/line)\n",
			l2size, ecx & 0xFF);
}

void __cpuinit detect_ht(struct cpuinfo_x86 *c)
{
#ifdef CONFIG_X86_HT
	u32 eax, ebx, ecx, edx;
	int index_msb, core_bits;

	if (!cpu_has(c, X86_FEATURE_HT))
		return;

	if (cpu_has(c, X86_FEATURE_CMP_LEGACY))
		goto out;

	if (cpu_has(c, X86_FEATURE_XTOPOLOGY))
		return;

	cpuid(1, &eax, &ebx, &ecx, &edx);

	smp_num_siblings = (ebx & 0xff0000) >> 16;

	if (smp_num_siblings == 1) {
		printk(KERN_INFO  "CPU: Hyper-Threading is disabled\n");
	} else if (smp_num_siblings > 1) {

		if (smp_num_siblings > nr_cpu_ids) {
			printk(KERN_WARNING "CPU: Unsupported number of siblings %d",
					smp_num_siblings);
			smp_num_siblings = 1;
			return;
		}

		index_msb = get_count_order(smp_num_siblings);
#ifdef CONFIG_X86_64
		c->phys_proc_id = phys_pkg_id(index_msb);
#else
		c->phys_proc_id = phys_pkg_id(c->initial_apicid, index_msb);
#endif

		smp_num_siblings = smp_num_siblings / c->x86_max_cores;

		index_msb = get_count_order(smp_num_siblings);

		core_bits = get_count_order(c->x86_max_cores);

#ifdef CONFIG_X86_64
		c->cpu_core_id = phys_pkg_id(index_msb) &
					       ((1 << core_bits) - 1);
#else
		c->cpu_core_id = phys_pkg_id(c->initial_apicid, index_msb) &
					       ((1 << core_bits) - 1);
#endif
	}

out:
	if ((c->x86_max_cores * smp_num_siblings) > 1) {
		printk(KERN_INFO  "CPU: Physical Processor ID: %d\n",
		       c->phys_proc_id);
		printk(KERN_INFO  "CPU: Processor Core ID: %d\n",
		       c->cpu_core_id);
	}
#endif
}

static void __cpuinit get_cpu_vendor(struct cpuinfo_x86 *c)
{
	char *v = c->x86_vendor_id;
	int i;
	static int printed;

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

	if (!printed) {
		printed++;
		printk(KERN_ERR "CPU: vendor_id '%s' unknown, using generic init.\n", v);
		printk(KERN_ERR "CPU: Your system may be unstable.\n");
	}

	c->x86_vendor = X86_VENDOR_UNKNOWN;
	this_cpu = &default_cpu;
}

void __cpuinit cpu_detect(struct cpuinfo_x86 *c)
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
		c->x86 = (tfms >> 8) & 0xf;
		c->x86_model = (tfms >> 4) & 0xf;
		c->x86_mask = tfms & 0xf;
		if (c->x86 == 0xf)
			c->x86 += (tfms >> 20) & 0xff;
		if (c->x86 >= 0x6)
			c->x86_model += ((tfms >> 16) & 0xf) << 4;
		if (cap0 & (1<<19)) {
			c->x86_clflush_size = ((misc >> 8) & 0xff) * 8;
			c->x86_cache_alignment = c->x86_clflush_size;
		}
	}
}

static void __cpuinit get_cpu_cap(struct cpuinfo_x86 *c)
{
	u32 tfms, xlvl;
	u32 ebx;

	/* Intel-defined flags: level 0x00000001 */
	if (c->cpuid_level >= 0x00000001) {
		u32 capability, excap;
		cpuid(0x00000001, &tfms, &ebx, &excap, &capability);
		c->x86_capability[0] = capability;
		c->x86_capability[4] = excap;
	}

	/* AMD-defined flags: level 0x80000001 */
	xlvl = cpuid_eax(0x80000000);
	c->extended_cpuid_level = xlvl;
	if ((xlvl & 0xffff0000) == 0x80000000) {
		if (xlvl >= 0x80000001) {
			c->x86_capability[1] = cpuid_edx(0x80000001);
			c->x86_capability[6] = cpuid_ecx(0x80000001);
		}
	}

#ifdef CONFIG_X86_64
	if (c->extended_cpuid_level >= 0x80000008) {
		u32 eax = cpuid_eax(0x80000008);

		c->x86_virt_bits = (eax >> 8) & 0xff;
		c->x86_phys_bits = eax & 0xff;
	}
#endif

	if (c->extended_cpuid_level >= 0x80000007)
		c->x86_power = cpuid_edx(0x80000007);

}

static void __cpuinit identify_cpu_without_cpuid(struct cpuinfo_x86 *c)
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
#else
	c->x86_clflush_size = 32;
#endif
	c->x86_cache_alignment = c->x86_clflush_size;

	memset(&c->x86_capability, 0, sizeof c->x86_capability);
	c->extended_cpuid_level = 0;

	if (!have_cpuid_p())
		identify_cpu_without_cpuid(c);

	/* cyrix could have cpuid enabled via c_identify()*/
	if (!have_cpuid_p())
		return;

	cpu_detect(c);

	get_cpu_vendor(c);

	get_cpu_cap(c);

	if (this_cpu->c_early_init)
		this_cpu->c_early_init(c);

	validate_pat_support(c);

#ifdef CONFIG_SMP
	c->cpu_index = boot_cpu_id;
#endif
}

void __init early_cpu_init(void)
{
	struct cpu_dev **cdev;
	int count = 0;

	printk("KERNEL supported cpus:\n");
	for (cdev = __x86_cpu_dev_start; cdev < __x86_cpu_dev_end; cdev++) {
		struct cpu_dev *cpudev = *cdev;
		unsigned int j;

		if (count >= X86_VENDOR_NUM)
			break;
		cpu_devs[count] = cpudev;
		count++;

		for (j = 0; j < 2; j++) {
			if (!cpudev->c_ident[j])
				continue;
			printk("  %s %s\n", cpudev->c_vendor,
				cpudev->c_ident[j]);
		}
	}

	early_identify_cpu(&boot_cpu_data);
}

/*
 * The NOPL instruction is supposed to exist on all CPUs with
 * family >= 6; unfortunately, that's not true in practice because
 * of early VIA chips and (more importantly) broken virtualizers that
 * are not easy to detect.  In the latter case it doesn't even *fail*
 * reliably, so probing for it doesn't even work.  Disable it completely
 * unless we can find a reliable way to detect all the broken cases.
 */
static void __cpuinit detect_nopl(struct cpuinfo_x86 *c)
{
	clear_cpu_cap(c, X86_FEATURE_NOPL);
}

static void __cpuinit generic_identify(struct cpuinfo_x86 *c)
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
# ifdef CONFIG_X86_HT
		c->apicid = phys_pkg_id(c->initial_apicid, 0);
# else
		c->apicid = c->initial_apicid;
# endif
#endif

#ifdef CONFIG_X86_HT
		c->phys_proc_id = c->initial_apicid;
#endif
	}

	get_model_name(c); /* Default name */

	init_scattered_cpuid_features(c);
	detect_nopl(c);
}

/*
 * This does the hard work of actually picking apart the CPU stuff...
 */
static void __cpuinit identify_cpu(struct cpuinfo_x86 *c)
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
#else
	c->cpuid_level = -1;	/* CPUID not detected */
	c->x86_clflush_size = 32;
#endif
	c->x86_cache_alignment = c->x86_clflush_size;
	memset(&c->x86_capability, 0, sizeof c->x86_capability);

	generic_identify(c);

	if (this_cpu->c_identify)
		this_cpu->c_identify(c);

#ifdef CONFIG_X86_64
	c->apicid = phys_pkg_id(0);
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

	/*
	 * The vendor-specific functions might have changed features.  Now
	 * we do "generic changes."
	 */

	/* If the model name is still unset, do table lookup. */
	if (!c->x86_model_id[0]) {
		char *p;
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
	}

	/* Clear all flags overriden by options */
	for (i = 0; i < NCAPINTS; i++)
		c->x86_capability[i] &= ~cleared_cpu_caps[i];

#ifdef CONFIG_X86_MCE
	/* Init Machine Check Exception if available. */
	mcheck_init(c);
#endif

	select_idle_routine(c);

#if defined(CONFIG_NUMA) && defined(CONFIG_X86_64)
	numa_add_cpu(smp_processor_id());
#endif
}

#ifdef CONFIG_X86_64
static void vgetcpu_set_mode(void)
{
	if (cpu_has(&boot_cpu_data, X86_FEATURE_RDTSCP))
		vgetcpu_mode = VGETCPU_RDTSCP;
	else
		vgetcpu_mode = VGETCPU_LSL;
}
#endif

void __init identify_boot_cpu(void)
{
	identify_cpu(&boot_cpu_data);
#ifdef CONFIG_X86_32
	sysenter_setup();
	enable_sep_cpu();
#else
	vgetcpu_set_mode();
#endif
}

void __cpuinit identify_secondary_cpu(struct cpuinfo_x86 *c)
{
	BUG_ON(c == &boot_cpu_data);
	identify_cpu(c);
#ifdef CONFIG_X86_32
	enable_sep_cpu();
#endif
	mtrr_ap_init();
}

struct msr_range {
	unsigned min;
	unsigned max;
};

static struct msr_range msr_range_array[] __cpuinitdata = {
	{ 0x00000000, 0x00000418},
	{ 0xc0000000, 0xc000040b},
	{ 0xc0010000, 0xc0010142},
	{ 0xc0011000, 0xc001103b},
};

static void __cpuinit print_cpu_msr(void)
{
	unsigned index;
	u64 val;
	int i;
	unsigned index_min, index_max;

	for (i = 0; i < ARRAY_SIZE(msr_range_array); i++) {
		index_min = msr_range_array[i].min;
		index_max = msr_range_array[i].max;
		for (index = index_min; index < index_max; index++) {
			if (rdmsrl_amd_safe(index, &val))
				continue;
			printk(KERN_INFO " MSR%08x: %016llx\n", index, val);
		}
	}
}

static int show_msr __cpuinitdata;
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
	setup_clear_cpu_cap(X86_FEATURE_CLFLSH);
	return 1;
}
__setup("noclflush", setup_noclflush);

void __cpuinit print_cpu_info(struct cpuinfo_x86 *c)
{
	char *vendor = NULL;

	if (c->x86_vendor < X86_VENDOR_NUM)
		vendor = this_cpu->c_vendor;
	else if (c->cpuid_level >= 0)
		vendor = c->x86_vendor_id;

	if (vendor && !strstr(c->x86_model_id, vendor))
		printk(KERN_CONT "%s ", vendor);

	if (c->x86_model_id[0])
		printk(KERN_CONT "%s", c->x86_model_id);
	else
		printk(KERN_CONT "%d86", c->x86);

	if (c->x86_mask || c->cpuid_level >= 0)
		printk(KERN_CONT " stepping %02x\n", c->x86_mask);
	else
		printk(KERN_CONT "\n");

#ifdef CONFIG_SMP
	if (c->cpu_index < show_msr)
		print_cpu_msr();
#else
	if (show_msr)
		print_cpu_msr();
#endif
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
struct x8664_pda **_cpu_pda __read_mostly;
EXPORT_SYMBOL(_cpu_pda);

struct desc_ptr idt_descr = { 256 * 16 - 1, (unsigned long) idt_table };

static char boot_cpu_stack[IRQSTACKSIZE] __page_aligned_bss;

void __cpuinit pda_init(int cpu)
{
	struct x8664_pda *pda = cpu_pda(cpu);

	/* Setup up data that may be needed in __get_free_pages early */
	loadsegment(fs, 0);
	loadsegment(gs, 0);
	/* Memory clobbers used to order PDA accessed */
	mb();
	wrmsrl(MSR_GS_BASE, pda);
	mb();

	pda->cpunumber = cpu;
	pda->irqcount = -1;
	pda->kernelstack = (unsigned long)stack_thread_info() -
				 PDA_STACKOFFSET + THREAD_SIZE;
	pda->active_mm = &init_mm;
	pda->mmu_state = 0;

	if (cpu == 0) {
		/* others are initialized in smpboot.c */
		pda->pcurrent = &init_task;
		pda->irqstackptr = boot_cpu_stack;
		pda->irqstackptr += IRQSTACKSIZE - 64;
	} else {
		if (!pda->irqstackptr) {
			pda->irqstackptr = (char *)
				__get_free_pages(GFP_ATOMIC, IRQSTACK_ORDER);
			if (!pda->irqstackptr)
				panic("cannot allocate irqstack for cpu %d",
				      cpu);
			pda->irqstackptr += IRQSTACKSIZE - 64;
		}

		if (pda->nodenumber == 0 && cpu_to_node(cpu) != NUMA_NO_NODE)
			pda->nodenumber = cpu_to_node(cpu);
	}
}

static char boot_exception_stacks[(N_EXCEPTION_STACKS - 1) * EXCEPTION_STKSZ +
				  DEBUG_STKSZ] __page_aligned_bss;

extern asmlinkage void ignore_sysret(void);

/* May not be marked __init: used by software suspend */
void syscall_init(void)
{
	/*
	 * LSTAR and STAR live in a bit strange symbiosis.
	 * They both write to the same internal register. STAR allows to
	 * set CS/DS but only a 32bit target. LSTAR sets the 64bit rip.
	 */
	wrmsrl(MSR_STAR,  ((u64)__USER32_CS)<<48  | ((u64)__KERNEL_CS)<<32);
	wrmsrl(MSR_LSTAR, system_call);
	wrmsrl(MSR_CSTAR, ignore_sysret);

#ifdef CONFIG_IA32_EMULATION
	syscall32_cpu_init();
#endif

	/* Flags to clear on syscall */
	wrmsrl(MSR_SYSCALL_MASK,
	       X86_EFLAGS_TF|X86_EFLAGS_DF|X86_EFLAGS_IF|X86_EFLAGS_IOPL);
}

unsigned long kernel_eflags;

/*
 * Copies of the original ist values from the tss are only accessed during
 * debugging, no special alignment required.
 */
DEFINE_PER_CPU(struct orig_ist, orig_ist);

#else

/* Make sure %fs is initialized properly in idle threads */
struct pt_regs * __cpuinit idle_regs(struct pt_regs *regs)
{
	memset(regs, 0, sizeof(struct pt_regs));
	regs->fs = __KERNEL_PERCPU;
	return regs;
}
#endif

/*
 * cpu_init() initializes state that is per-CPU. Some data is already
 * initialized (naturally) in the bootstrap process, such as the GDT
 * and IDT. We reload them nevertheless, this function acts as a
 * 'CPU state barrier', nothing should get across.
 * A lot of state is already set up in PDA init for 64 bit
 */
#ifdef CONFIG_X86_64
void __cpuinit cpu_init(void)
{
	int cpu = stack_smp_processor_id();
	struct tss_struct *t = &per_cpu(init_tss, cpu);
	struct orig_ist *orig_ist = &per_cpu(orig_ist, cpu);
	unsigned long v;
	char *estacks = NULL;
	struct task_struct *me;
	int i;

	/* CPU 0 is initialised in head64.c */
	if (cpu != 0)
		pda_init(cpu);
	else
		estacks = boot_exception_stacks;

	me = current;

	if (cpumask_test_and_set_cpu(cpu, cpu_initialized_mask))
		panic("CPU#%d already initialized!\n", cpu);

	printk(KERN_INFO "Initializing CPU#%d\n", cpu);

	clear_in_cr4(X86_CR4_VME|X86_CR4_PVI|X86_CR4_TSD|X86_CR4_DE);

	/*
	 * Initialize the per-CPU GDT with the boot GDT,
	 * and set up the GDT descriptor:
	 */

	switch_to_new_gdt();
	load_idt((const struct desc_ptr *)&idt_descr);

	memset(me->thread.tls_array, 0, GDT_ENTRY_TLS_ENTRIES * 8);
	syscall_init();

	wrmsrl(MSR_FS_BASE, 0);
	wrmsrl(MSR_KERNEL_GS_BASE, 0);
	barrier();

	check_efer();
	if (cpu != 0 && x2apic)
		enable_x2apic();

	/*
	 * set up and load the per-CPU TSS
	 */
	if (!orig_ist->ist[0]) {
		static const unsigned int order[N_EXCEPTION_STACKS] = {
		  [0 ... N_EXCEPTION_STACKS - 1] = EXCEPTION_STACK_ORDER,
		  [DEBUG_STACK - 1] = DEBUG_STACK_ORDER
		};
		for (v = 0; v < N_EXCEPTION_STACKS; v++) {
			if (cpu) {
				estacks = (char *)__get_free_pages(GFP_ATOMIC, order[v]);
				if (!estacks)
					panic("Cannot allocate exception "
					      "stack %ld %d\n", v, cpu);
			}
			estacks += PAGE_SIZE << order[v];
			orig_ist->ist[v] = t->x86_tss.ist[v] =
					(unsigned long)estacks;
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
	if (me->mm)
		BUG();
	enter_lazy_tlb(&init_mm, me);

	load_sp0(t, &current->thread);
	set_tss_desc(cpu, t);
	load_TR_desc();
	load_LDT(&init_mm.context);

#ifdef CONFIG_KGDB
	/*
	 * If the kgdb is connected no debug regs should be altered.  This
	 * is only applicable when KGDB and a KGDB I/O module are built
	 * into the kernel and you are using early debugging with
	 * kgdbwait. KGDB will control the kernel HW breakpoint registers.
	 */
	if (kgdb_connected && arch_kgdb_ops.correct_hw_break)
		arch_kgdb_ops.correct_hw_break();
	else {
#endif
	/*
	 * Clear all 6 debug registers:
	 */

	set_debugreg(0UL, 0);
	set_debugreg(0UL, 1);
	set_debugreg(0UL, 2);
	set_debugreg(0UL, 3);
	set_debugreg(0UL, 6);
	set_debugreg(0UL, 7);
#ifdef CONFIG_KGDB
	/* If the kgdb is connected no debug regs should be altered. */
	}
#endif

	fpu_init();

	raw_local_save_flags(kernel_eflags);

	if (is_uv_system())
		uv_cpu_init();
}

#else

void __cpuinit cpu_init(void)
{
	int cpu = smp_processor_id();
	struct task_struct *curr = current;
	struct tss_struct *t = &per_cpu(init_tss, cpu);
	struct thread_struct *thread = &curr->thread;

	if (cpumask_test_and_set_cpu(cpu, cpu_initialized_mask)) {
		printk(KERN_WARNING "CPU#%d already initialized!\n", cpu);
		for (;;) local_irq_enable();
	}

	printk(KERN_INFO "Initializing CPU#%d\n", cpu);

	if (cpu_has_vme || cpu_has_tsc || cpu_has_de)
		clear_in_cr4(X86_CR4_VME|X86_CR4_PVI|X86_CR4_TSD|X86_CR4_DE);

	load_idt(&idt_descr);
	switch_to_new_gdt();

	/*
	 * Set up and load the per-CPU TSS and LDT
	 */
	atomic_inc(&init_mm.mm_count);
	curr->active_mm = &init_mm;
	if (curr->mm)
		BUG();
	enter_lazy_tlb(&init_mm, curr);

	load_sp0(t, thread);
	set_tss_desc(cpu, t);
	load_TR_desc();
	load_LDT(&init_mm.context);

#ifdef CONFIG_DOUBLEFAULT
	/* Set up doublefault TSS pointer in the GDT */
	__set_tss_desc(cpu, GDT_ENTRY_DOUBLEFAULT_TSS, &doublefault_tss);
#endif

	/* Clear %gs. */
	asm volatile ("mov %0, %%gs" : : "r" (0));

	/* Clear all 6 debug registers: */
	set_debugreg(0, 0);
	set_debugreg(0, 1);
	set_debugreg(0, 2);
	set_debugreg(0, 3);
	set_debugreg(0, 6);
	set_debugreg(0, 7);

	/*
	 * Force FPU initialization:
	 */
	if (cpu_has_xsave)
		current_thread_info()->status = TS_XSAVE;
	else
		current_thread_info()->status = 0;
	clear_used_math();
	mxcsr_feature_mask_init();

	/*
	 * Boot processor to setup the FP and extended state context info.
	 */
	if (smp_processor_id() == boot_cpu_id)
		init_thread_xstate();

	xsave_init();
}


#endif
