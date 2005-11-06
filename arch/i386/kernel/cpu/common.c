#include <linux/init.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/smp.h>
#include <linux/module.h>
#include <linux/percpu.h>
#include <asm/semaphore.h>
#include <asm/processor.h>
#include <asm/i387.h>
#include <asm/msr.h>
#include <asm/io.h>
#include <asm/mmu_context.h>
#ifdef CONFIG_X86_LOCAL_APIC
#include <asm/mpspec.h>
#include <asm/apic.h>
#include <mach_apic.h>
#endif

#include "cpu.h"

DEFINE_PER_CPU(struct desc_struct, cpu_gdt_table[GDT_ENTRIES]);
EXPORT_PER_CPU_SYMBOL(cpu_gdt_table);

DEFINE_PER_CPU(unsigned char, cpu_16bit_stack[CPU_16BIT_STACK_SIZE]);
EXPORT_PER_CPU_SYMBOL(cpu_16bit_stack);

static int cachesize_override __devinitdata = -1;
static int disable_x86_fxsr __devinitdata = 0;
static int disable_x86_serial_nr __devinitdata = 1;

struct cpu_dev * cpu_devs[X86_VENDOR_NUM] = {};

extern void mcheck_init(struct cpuinfo_x86 *c);

extern int disable_pse;

static void default_init(struct cpuinfo_x86 * c)
{
	/* Not much we can do here... */
	/* Check if at least it has cpuid */
	if (c->cpuid_level == -1) {
		/* No cpuid. It must be an ancient CPU */
		if (c->x86 == 4)
			strcpy(c->x86_model_id, "486");
		else if (c->x86 == 3)
			strcpy(c->x86_model_id, "386");
	}
}

static struct cpu_dev default_cpu = {
	.c_init	= default_init,
};
static struct cpu_dev * this_cpu = &default_cpu;

static int __init cachesize_setup(char *str)
{
	get_option (&str, &cachesize_override);
	return 1;
}
__setup("cachesize=", cachesize_setup);

int __devinit get_model_name(struct cpuinfo_x86 *c)
{
	unsigned int *v;
	char *p, *q;

	if (cpuid_eax(0x80000000) < 0x80000004)
		return 0;

	v = (unsigned int *) c->x86_model_id;
	cpuid(0x80000002, &v[0], &v[1], &v[2], &v[3]);
	cpuid(0x80000003, &v[4], &v[5], &v[6], &v[7]);
	cpuid(0x80000004, &v[8], &v[9], &v[10], &v[11]);
	c->x86_model_id[48] = 0;

	/* Intel chips right-justify this string for some dumb reason;
	   undo that brain damage */
	p = q = &c->x86_model_id[0];
	while ( *p == ' ' )
	     p++;
	if ( p != q ) {
	     while ( *p )
		  *q++ = *p++;
	     while ( q <= &c->x86_model_id[48] )
		  *q++ = '\0';	/* Zero-pad the rest */
	}

	return 1;
}


void __devinit display_cacheinfo(struct cpuinfo_x86 *c)
{
	unsigned int n, dummy, ecx, edx, l2size;

	n = cpuid_eax(0x80000000);

	if (n >= 0x80000005) {
		cpuid(0x80000005, &dummy, &dummy, &ecx, &edx);
		printk(KERN_INFO "CPU: L1 I Cache: %dK (%d bytes/line), D cache %dK (%d bytes/line)\n",
			edx>>24, edx&0xFF, ecx>>24, ecx&0xFF);
		c->x86_cache_size=(ecx>>24)+(edx>>24);	
	}

	if (n < 0x80000006)	/* Some chips just has a large L1. */
		return;

	ecx = cpuid_ecx(0x80000006);
	l2size = ecx >> 16;
	
	/* do processor-specific cache resizing */
	if (this_cpu->c_size_cache)
		l2size = this_cpu->c_size_cache(c,l2size);

	/* Allow user to override all this if necessary. */
	if (cachesize_override != -1)
		l2size = cachesize_override;

	if ( l2size == 0 )
		return;		/* Again, no L2 cache is possible */

	c->x86_cache_size = l2size;

	printk(KERN_INFO "CPU: L2 Cache: %dK (%d bytes/line)\n",
	       l2size, ecx & 0xFF);
}

/* Naming convention should be: <Name> [(<Codename>)] */
/* This table only is used unless init_<vendor>() below doesn't set it; */
/* in particular, if CPUID levels 0x80000002..4 are supported, this isn't used */

/* Look up CPU names by table lookup. */
static char __devinit *table_lookup_model(struct cpuinfo_x86 *c)
{
	struct cpu_model_info *info;

	if ( c->x86_model >= 16 )
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


static void __devinit get_cpu_vendor(struct cpuinfo_x86 *c, int early)
{
	char *v = c->x86_vendor_id;
	int i;

	for (i = 0; i < X86_VENDOR_NUM; i++) {
		if (cpu_devs[i]) {
			if (!strcmp(v,cpu_devs[i]->c_ident[0]) ||
			    (cpu_devs[i]->c_ident[1] && 
			     !strcmp(v,cpu_devs[i]->c_ident[1]))) {
				c->x86_vendor = i;
				if (!early)
					this_cpu = cpu_devs[i];
				break;
			}
		}
	}
}


static int __init x86_fxsr_setup(char * s)
{
	disable_x86_fxsr = 1;
	return 1;
}
__setup("nofxsr", x86_fxsr_setup);


/* Standard macro to see if a specific flag is changeable */
static inline int flag_is_changeable_p(u32 flag)
{
	u32 f1, f2;

	asm("pushfl\n\t"
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
static int __devinit have_cpuid_p(void)
{
	return flag_is_changeable_p(X86_EFLAGS_ID);
}

/* Do minimum CPU detection early.
   Fields really needed: vendor, cpuid_level, family, model, mask, cache alignment.
   The others are not touched to avoid unwanted side effects. */
static void __init early_cpu_detect(void)
{
	struct cpuinfo_x86 *c = &boot_cpu_data;

	c->x86_cache_alignment = 32;

	if (!have_cpuid_p())
		return;

	/* Get vendor name */
	cpuid(0x00000000, &c->cpuid_level,
	      (int *)&c->x86_vendor_id[0],
	      (int *)&c->x86_vendor_id[8],
	      (int *)&c->x86_vendor_id[4]);

	get_cpu_vendor(c, 1);

	c->x86 = 4;
	if (c->cpuid_level >= 0x00000001) {
		u32 junk, tfms, cap0, misc;
		cpuid(0x00000001, &tfms, &misc, &junk, &cap0);
		c->x86 = (tfms >> 8) & 15;
		c->x86_model = (tfms >> 4) & 15;
		if (c->x86 == 0xf) {
			c->x86 += (tfms >> 20) & 0xff;
			c->x86_model += ((tfms >> 16) & 0xF) << 4;
		}
		c->x86_mask = tfms & 15;
		if (cap0 & (1<<19))
			c->x86_cache_alignment = ((misc >> 8) & 0xff) * 8;
	}

	early_intel_workaround(c);

#ifdef CONFIG_X86_HT
	phys_proc_id[smp_processor_id()] = (cpuid_ebx(1) >> 24) & 0xff;
#endif
}

void __devinit generic_identify(struct cpuinfo_x86 * c)
{
	u32 tfms, xlvl;
	int junk;

	if (have_cpuid_p()) {
		/* Get vendor name */
		cpuid(0x00000000, &c->cpuid_level,
		      (int *)&c->x86_vendor_id[0],
		      (int *)&c->x86_vendor_id[8],
		      (int *)&c->x86_vendor_id[4]);
		
		get_cpu_vendor(c, 0);
		/* Initialize the standard set of capabilities */
		/* Note that the vendor-specific code below might override */
	
		/* Intel-defined flags: level 0x00000001 */
		if ( c->cpuid_level >= 0x00000001 ) {
			u32 capability, excap;
			cpuid(0x00000001, &tfms, &junk, &excap, &capability);
			c->x86_capability[0] = capability;
			c->x86_capability[4] = excap;
			c->x86 = (tfms >> 8) & 15;
			c->x86_model = (tfms >> 4) & 15;
			if (c->x86 == 0xf) {
				c->x86 += (tfms >> 20) & 0xff;
				c->x86_model += ((tfms >> 16) & 0xF) << 4;
			} 
			c->x86_mask = tfms & 15;
		} else {
			/* Have CPUID level 0 only - unheard of */
			c->x86 = 4;
		}

		/* AMD-defined flags: level 0x80000001 */
		xlvl = cpuid_eax(0x80000000);
		if ( (xlvl & 0xffff0000) == 0x80000000 ) {
			if ( xlvl >= 0x80000001 ) {
				c->x86_capability[1] = cpuid_edx(0x80000001);
				c->x86_capability[6] = cpuid_ecx(0x80000001);
			}
			if ( xlvl >= 0x80000004 )
				get_model_name(c); /* Default name */
		}
	}
}

static void __devinit squash_the_stupid_serial_number(struct cpuinfo_x86 *c)
{
	if (cpu_has(c, X86_FEATURE_PN) && disable_x86_serial_nr ) {
		/* Disable processor serial number */
		unsigned long lo,hi;
		rdmsr(MSR_IA32_BBL_CR_CTL,lo,hi);
		lo |= 0x200000;
		wrmsr(MSR_IA32_BBL_CR_CTL,lo,hi);
		printk(KERN_NOTICE "CPU serial number disabled.\n");
		clear_bit(X86_FEATURE_PN, c->x86_capability);

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



/*
 * This does the hard work of actually picking apart the CPU stuff...
 */
void __devinit identify_cpu(struct cpuinfo_x86 *c)
{
	int i;

	c->loops_per_jiffy = loops_per_jiffy;
	c->x86_cache_size = -1;
	c->x86_vendor = X86_VENDOR_UNKNOWN;
	c->cpuid_level = -1;	/* CPUID not detected */
	c->x86_model = c->x86_mask = 0;	/* So far unknown... */
	c->x86_vendor_id[0] = '\0'; /* Unset */
	c->x86_model_id[0] = '\0';  /* Unset */
	c->x86_num_cores = 1;
	memset(&c->x86_capability, 0, sizeof c->x86_capability);

	if (!have_cpuid_p()) {
		/* First of all, decide if this is a 486 or higher */
		/* It's a 486 if we can modify the AC flag */
		if ( flag_is_changeable_p(X86_EFLAGS_AC) )
			c->x86 = 4;
		else
			c->x86 = 3;
	}

	generic_identify(c);

	printk(KERN_DEBUG "CPU: After generic identify, caps:");
	for (i = 0; i < NCAPINTS; i++)
		printk(" %08lx", c->x86_capability[i]);
	printk("\n");

	if (this_cpu->c_identify) {
		this_cpu->c_identify(c);

		printk(KERN_DEBUG "CPU: After vendor identify, caps:");
		for (i = 0; i < NCAPINTS; i++)
			printk(" %08lx", c->x86_capability[i]);
		printk("\n");
	}

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

	/* TSC disabled? */
	if ( tsc_disable )
		clear_bit(X86_FEATURE_TSC, c->x86_capability);

	/* FXSR disabled? */
	if (disable_x86_fxsr) {
		clear_bit(X86_FEATURE_FXSR, c->x86_capability);
		clear_bit(X86_FEATURE_XMM, c->x86_capability);
	}

	if (disable_pse)
		clear_bit(X86_FEATURE_PSE, c->x86_capability);

	/* If the model name is still unset, do table lookup. */
	if ( !c->x86_model_id[0] ) {
		char *p;
		p = table_lookup_model(c);
		if ( p )
			strcpy(c->x86_model_id, p);
		else
			/* Last resort... */
			sprintf(c->x86_model_id, "%02x/%02x",
				c->x86_vendor, c->x86_model);
	}

	/* Now the feature flags better reflect actual CPU features! */

	printk(KERN_DEBUG "CPU: After all inits, caps:");
	for (i = 0; i < NCAPINTS; i++)
		printk(" %08lx", c->x86_capability[i]);
	printk("\n");

	/*
	 * On SMP, boot_cpu_data holds the common feature set between
	 * all CPUs; so make sure that we indicate which features are
	 * common between the CPUs.  The first time this routine gets
	 * executed, c == &boot_cpu_data.
	 */
	if ( c != &boot_cpu_data ) {
		/* AND the already accumulated flags with these */
		for ( i = 0 ; i < NCAPINTS ; i++ )
			boot_cpu_data.x86_capability[i] &= c->x86_capability[i];
	}

	/* Init Machine Check Exception if available. */
#ifdef CONFIG_X86_MCE
	mcheck_init(c);
#endif
	if (c == &boot_cpu_data)
		sysenter_setup();
	enable_sep_cpu();

	if (c == &boot_cpu_data)
		mtrr_bp_init();
	else
		mtrr_ap_init();
}

#ifdef CONFIG_X86_HT
void __devinit detect_ht(struct cpuinfo_x86 *c)
{
	u32 	eax, ebx, ecx, edx;
	int 	index_msb, tmp;
	int 	cpu = smp_processor_id();

	if (!cpu_has(c, X86_FEATURE_HT) || cpu_has(c, X86_FEATURE_CMP_LEGACY))
		return;

	cpuid(1, &eax, &ebx, &ecx, &edx);
	smp_num_siblings = (ebx & 0xff0000) >> 16;

	if (smp_num_siblings == 1) {
		printk(KERN_INFO  "CPU: Hyper-Threading is disabled\n");
	} else if (smp_num_siblings > 1 ) {
		index_msb = 31;

		if (smp_num_siblings > NR_CPUS) {
			printk(KERN_WARNING "CPU: Unsupported number of the siblings %d", smp_num_siblings);
			smp_num_siblings = 1;
			return;
		}
		tmp = smp_num_siblings;
		while ((tmp & 0x80000000 ) == 0) {
			tmp <<=1 ;
			index_msb--;
		}
		if (smp_num_siblings & (smp_num_siblings - 1))
			index_msb++;
		phys_proc_id[cpu] = phys_pkg_id((ebx >> 24) & 0xFF, index_msb);

		printk(KERN_INFO  "CPU: Physical Processor ID: %d\n",
		       phys_proc_id[cpu]);

		smp_num_siblings = smp_num_siblings / c->x86_num_cores;

		tmp = smp_num_siblings;
		index_msb = 31;
		while ((tmp & 0x80000000) == 0) {
			tmp <<=1 ;
			index_msb--;
		}

		if (smp_num_siblings & (smp_num_siblings - 1))
			index_msb++;

		cpu_core_id[cpu] = phys_pkg_id((ebx >> 24) & 0xFF, index_msb);

		if (c->x86_num_cores > 1)
			printk(KERN_INFO  "CPU: Processor Core ID: %d\n",
			       cpu_core_id[cpu]);
	}
}
#endif

void __devinit print_cpu_info(struct cpuinfo_x86 *c)
{
	char *vendor = NULL;

	if (c->x86_vendor < X86_VENDOR_NUM)
		vendor = this_cpu->c_vendor;
	else if (c->cpuid_level >= 0)
		vendor = c->x86_vendor_id;

	if (vendor && strncmp(c->x86_model_id, vendor, strlen(vendor)))
		printk("%s ", vendor);

	if (!c->x86_model_id[0])
		printk("%d86", c->x86);
	else
		printk("%s", c->x86_model_id);

	if (c->x86_mask || c->cpuid_level >= 0) 
		printk(" stepping %02x\n", c->x86_mask);
	else
		printk("\n");
}

cpumask_t cpu_initialized __devinitdata = CPU_MASK_NONE;

/* This is hacky. :)
 * We're emulating future behavior.
 * In the future, the cpu-specific init functions will be called implicitly
 * via the magic of initcalls.
 * They will insert themselves into the cpu_devs structure.
 * Then, when cpu_init() is called, we can just iterate over that array.
 */

extern int intel_cpu_init(void);
extern int cyrix_init_cpu(void);
extern int nsc_init_cpu(void);
extern int amd_init_cpu(void);
extern int centaur_init_cpu(void);
extern int transmeta_init_cpu(void);
extern int rise_init_cpu(void);
extern int nexgen_init_cpu(void);
extern int umc_init_cpu(void);

void __init early_cpu_init(void)
{
	intel_cpu_init();
	cyrix_init_cpu();
	nsc_init_cpu();
	amd_init_cpu();
	centaur_init_cpu();
	transmeta_init_cpu();
	rise_init_cpu();
	nexgen_init_cpu();
	umc_init_cpu();
	early_cpu_detect();

#ifdef CONFIG_DEBUG_PAGEALLOC
	/* pse is not compatible with on-the-fly unmapping,
	 * disable it even if the cpus claim to support it.
	 */
	clear_bit(X86_FEATURE_PSE, boot_cpu_data.x86_capability);
	disable_pse = 1;
#endif
}
/*
 * cpu_init() initializes state that is per-CPU. Some data is already
 * initialized (naturally) in the bootstrap process, such as the GDT
 * and IDT. We reload them nevertheless, this function acts as a
 * 'CPU state barrier', nothing should get across.
 */
void __devinit cpu_init(void)
{
	int cpu = smp_processor_id();
	struct tss_struct * t = &per_cpu(init_tss, cpu);
	struct thread_struct *thread = &current->thread;
	struct desc_struct *gdt = get_cpu_gdt_table(cpu);
	__u32 stk16_off = (__u32)&per_cpu(cpu_16bit_stack, cpu);

	if (cpu_test_and_set(cpu, cpu_initialized)) {
		printk(KERN_WARNING "CPU#%d already initialized!\n", cpu);
		for (;;) local_irq_enable();
	}
	printk(KERN_INFO "Initializing CPU#%d\n", cpu);

	if (cpu_has_vme || cpu_has_tsc || cpu_has_de)
		clear_in_cr4(X86_CR4_VME|X86_CR4_PVI|X86_CR4_TSD|X86_CR4_DE);
	if (tsc_disable && cpu_has_tsc) {
		printk(KERN_NOTICE "Disabling TSC...\n");
		/**** FIX-HPA: DOES THIS REALLY BELONG HERE? ****/
		clear_bit(X86_FEATURE_TSC, boot_cpu_data.x86_capability);
		set_in_cr4(X86_CR4_TSD);
	}

	/*
	 * Initialize the per-CPU GDT with the boot GDT,
	 * and set up the GDT descriptor:
	 */
 	memcpy(gdt, cpu_gdt_table, GDT_SIZE);

	/* Set up GDT entry for 16bit stack */
 	*(__u64 *)(&gdt[GDT_ENTRY_ESPFIX_SS]) |=
		((((__u64)stk16_off) << 16) & 0x000000ffffff0000ULL) |
		((((__u64)stk16_off) << 32) & 0xff00000000000000ULL) |
		(CPU_16BIT_STACK_SIZE - 1);

	cpu_gdt_descr[cpu].size = GDT_SIZE - 1;
 	cpu_gdt_descr[cpu].address = (unsigned long)gdt;

	load_gdt(&cpu_gdt_descr[cpu]);
	load_idt(&idt_descr);

	/*
	 * Delete NT
	 */
	__asm__("pushfl ; andl $0xffffbfff,(%esp) ; popfl");

	/*
	 * Set up and load the per-CPU TSS and LDT
	 */
	atomic_inc(&init_mm.mm_count);
	current->active_mm = &init_mm;
	if (current->mm)
		BUG();
	enter_lazy_tlb(&init_mm, current);

	load_esp0(t, thread);
	set_tss_desc(cpu,t);
	load_TR_desc();
	load_LDT(&init_mm.context);

	/* Set up doublefault TSS pointer in the GDT */
	__set_tss_desc(cpu, GDT_ENTRY_DOUBLEFAULT_TSS, &doublefault_tss);

	/* Clear %fs and %gs. */
	asm volatile ("xorl %eax, %eax; movl %eax, %fs; movl %eax, %gs");

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
	current_thread_info()->status = 0;
	clear_used_math();
	mxcsr_feature_mask_init();
}

#ifdef CONFIG_HOTPLUG_CPU
void __devinit cpu_uninit(void)
{
	int cpu = raw_smp_processor_id();
	cpu_clear(cpu, cpu_initialized);

	/* lazy TLB state */
	per_cpu(cpu_tlbstate, cpu).state = 0;
	per_cpu(cpu_tlbstate, cpu).active_mm = &init_mm;
}
#endif
