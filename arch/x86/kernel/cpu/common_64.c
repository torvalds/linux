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
#ifdef CONFIG_X86_LOCAL_APIC
#include <asm/mpspec.h>
#include <asm/apic.h>
#include <mach_apic.h>
#endif
#include <asm/pda.h>
#include <asm/pgtable.h>
#include <asm/processor.h>
#include <asm/desc.h>
#include <asm/atomic.h>
#include <asm/proto.h>
#include <asm/sections.h>
#include <asm/setup.h>
#include <asm/genapic.h>

#include "cpu.h"

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
EXPORT_PER_CPU_SYMBOL_GPL(gdt_page);

__u32 cleared_cpu_caps[NCAPINTS] __cpuinitdata;

/* Current gdt points %fs at the "master" per-cpu area: after this,
 * it's on the real one. */
void switch_to_new_gdt(void)
{
	struct desc_ptr gdt_descr;

	gdt_descr.address = (long)get_cpu_gdt_table(smp_processor_id());
	gdt_descr.size = GDT_SIZE - 1;
	load_gdt(&gdt_descr);
}

static struct cpu_dev *cpu_devs[X86_VENDOR_NUM] = {};

static void __cpuinit default_init(struct cpuinfo_x86 *c)
{
	display_cacheinfo(c);
}

static struct cpu_dev __cpuinitdata default_cpu = {
	.c_init	= default_init,
	.c_vendor = "Unknown",
	.c_x86_vendor = X86_VENDOR_UNKNOWN,
};
static struct cpu_dev *this_cpu __cpuinitdata;

int __cpuinit get_model_name(struct cpuinfo_x86 *c)
{
	unsigned int *v;
	char *p, *q;

	if (c->extended_cpuid_level < 0x80000004)
		return 0;

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

	return 1;
}


void __cpuinit display_cacheinfo(struct cpuinfo_x86 *c)
{
	unsigned int n, dummy, ebx, ecx, edx;

	n = c->extended_cpuid_level;

	if (n >= 0x80000005) {
		cpuid(0x80000005, &dummy, &ebx, &ecx, &edx);
		printk(KERN_INFO "CPU: L1 I Cache: %dK (%d bytes/line), D cache %dK (%d bytes/line)\n",
				edx>>24, edx&0xFF, ecx>>24, ecx&0xFF);
		c->x86_cache_size = (ecx>>24) + (edx>>24);
		/* On K8 L1 TLB is inclusive, so don't count it */
		c->x86_tlbsize = 0;
	}

	if (n >= 0x80000006) {
		cpuid(0x80000006, &dummy, &ebx, &ecx, &edx);
		ecx = cpuid_ecx(0x80000006);
		c->x86_cache_size = ecx >> 16;
		c->x86_tlbsize += ((ebx >> 16) & 0xfff) + (ebx & 0xfff);

		printk(KERN_INFO "CPU: L2 Cache: %dK (%d bytes/line)\n",
		c->x86_cache_size, ecx & 0xFF);
	}
}

void __cpuinit detect_ht(struct cpuinfo_x86 *c)
{
#ifdef CONFIG_SMP
	u32 eax, ebx, ecx, edx;
	int index_msb, core_bits;

	cpuid(1, &eax, &ebx, &ecx, &edx);


	if (!cpu_has(c, X86_FEATURE_HT))
		return;
	if (cpu_has(c, X86_FEATURE_CMP_LEGACY))
		goto out;

	smp_num_siblings = (ebx & 0xff0000) >> 16;

	if (smp_num_siblings == 1) {
		printk(KERN_INFO  "CPU: Hyper-Threading is disabled\n");
	} else if (smp_num_siblings > 1) {

		if (smp_num_siblings > NR_CPUS) {
			printk(KERN_WARNING "CPU: Unsupported number of siblings %d",
					smp_num_siblings);
			smp_num_siblings = 1;
			return;
		}

		index_msb = get_count_order(smp_num_siblings);
		c->phys_proc_id = phys_pkg_id(index_msb);

		smp_num_siblings = smp_num_siblings / c->x86_max_cores;

		index_msb = get_count_order(smp_num_siblings);

		core_bits = get_count_order(c->x86_max_cores);

		c->cpu_core_id = phys_pkg_id(index_msb) &
					       ((1 << core_bits) - 1);
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
		printk(KERN_ERR "CPU: Vendor unknown, using generic init.\n");
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

	/* Transmeta-defined flags: level 0x80860001 */
	xlvl = cpuid_eax(0x80860000);
	if ((xlvl & 0xffff0000) == 0x80860000) {
		/* Don't set x86_cpuid_level here for now to not confuse. */
		if (xlvl >= 0x80860001)
			c->x86_capability[2] = cpuid_edx(0x80860001);
	}

	if (c->extended_cpuid_level >= 0x80000007)
		c->x86_power = cpuid_edx(0x80000007);

	if (c->extended_cpuid_level >= 0x80000008) {
		u32 eax = cpuid_eax(0x80000008);

		c->x86_virt_bits = (eax >> 8) & 0xff;
		c->x86_phys_bits = eax & 0xff;
	}
}

/* Do some early cpuid on the boot CPU to get some parameter that are
   needed before check_bugs. Everything advanced is in identify_cpu
   below. */
static void __init early_identify_cpu(struct cpuinfo_x86 *c)
{

	c->x86_clflush_size = 64;
	c->x86_cache_alignment = c->x86_clflush_size;

	memset(&c->x86_capability, 0, sizeof c->x86_capability);

	c->extended_cpuid_level = 0;

	cpu_detect(c);

	get_cpu_vendor(c);

	get_cpu_cap(c);

	if (this_cpu->c_early_init)
		this_cpu->c_early_init(c);

	validate_pat_support(c);
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
 * family >= 6, unfortunately, that's not true in practice because
 * of early VIA chips and (more importantly) broken virtualizers that
 * are not easy to detect.  Hence, probe for it based on first
 * principles.
 *
 * Note: no 64-bit chip is known to lack these, but put the code here
 * for consistency with 32 bits, and to make it utterly trivial to
 * diagnose the problem should it ever surface.
 */
static void __cpuinit detect_nopl(struct cpuinfo_x86 *c)
{
	const u32 nopl_signature = 0x888c53b1; /* Random number */
	u32 has_nopl = nopl_signature;

	clear_cpu_cap(c, X86_FEATURE_NOPL);
	if (c->x86 >= 6) {
		asm volatile("\n"
			     "1:      .byte 0x0f,0x1f,0xc0\n" /* nopl %eax */
			     "2:\n"
			     "        .section .fixup,\"ax\"\n"
			     "3:      xor %0,%0\n"
			     "        jmp 2b\n"
			     "        .previous\n"
			     _ASM_EXTABLE(1b,3b)
			     : "+a" (has_nopl));

		if (has_nopl == nopl_signature)
			set_cpu_cap(c, X86_FEATURE_NOPL);
	}
}

static void __cpuinit generic_identify(struct cpuinfo_x86 *c)
{
	c->extended_cpuid_level = 0;

	cpu_detect(c);

	get_cpu_vendor(c);

	get_cpu_cap(c);

	c->initial_apicid = (cpuid_ebx(1) >> 24) & 0xff;
#ifdef CONFIG_SMP
	c->phys_proc_id = c->initial_apicid;
#endif

	if (c->extended_cpuid_level >= 0x80000004)
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
	c->x86_clflush_size = 64;
	c->x86_cache_alignment = c->x86_clflush_size;
	c->x86_max_cores = 1;
	c->x86_coreid_bits = 0;
	memset(&c->x86_capability, 0, sizeof c->x86_capability);

	generic_identify(c);

	c->apicid = phys_pkg_id(0);

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

	detect_ht(c);

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
	mcheck_init(c);
#endif
	select_idle_routine(c);

#ifdef CONFIG_NUMA
	numa_add_cpu(smp_processor_id());
#endif

}

void __init identify_boot_cpu(void)
{
	identify_cpu(&boot_cpu_data);
}

void __cpuinit identify_secondary_cpu(struct cpuinfo_x86 *c)
{
	BUG_ON(c == &boot_cpu_data);
	identify_cpu(c);
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
	if (c->x86_model_id[0])
		printk(KERN_CONT "%s", c->x86_model_id);

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

cpumask_t cpu_initialized __cpuinitdata = CPU_MASK_NONE;

struct x8664_pda **_cpu_pda __read_mostly;
EXPORT_SYMBOL(_cpu_pda);

struct desc_ptr idt_descr = { 256 * 16 - 1, (unsigned long) idt_table };

char boot_cpu_stack[IRQSTACKSIZE] __page_aligned_bss;

unsigned long __supported_pte_mask __read_mostly = ~0UL;
EXPORT_SYMBOL_GPL(__supported_pte_mask);

static int do_not_nx __cpuinitdata;

/* noexec=on|off
Control non executable mappings for 64bit processes.

on	Enable(default)
off	Disable
*/
static int __init nonx_setup(char *str)
{
	if (!str)
		return -EINVAL;
	if (!strncmp(str, "on", 2)) {
		__supported_pte_mask |= _PAGE_NX;
		do_not_nx = 0;
	} else if (!strncmp(str, "off", 3)) {
		do_not_nx = 1;
		__supported_pte_mask &= ~_PAGE_NX;
	}
	return 0;
}
early_param("noexec", nonx_setup);

int force_personality32;

/* noexec32=on|off
Control non executable heap for 32bit processes.
To control the stack too use noexec=off

on	PROT_READ does not imply PROT_EXEC for 32bit processes (default)
off	PROT_READ implies PROT_EXEC
*/
static int __init nonx32_setup(char *str)
{
	if (!strcmp(str, "on"))
		force_personality32 &= ~READ_IMPLIES_EXEC;
	else if (!strcmp(str, "off"))
		force_personality32 |= READ_IMPLIES_EXEC;
	return 1;
}
__setup("noexec32=", nonx32_setup);

void pda_init(int cpu)
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
	} else {
		pda->irqstackptr = (char *)
			__get_free_pages(GFP_ATOMIC, IRQSTACK_ORDER);
		if (!pda->irqstackptr)
			panic("cannot allocate irqstack for cpu %d", cpu);

		if (pda->nodenumber == 0 && cpu_to_node(cpu) != NUMA_NO_NODE)
			pda->nodenumber = cpu_to_node(cpu);
	}

	pda->irqstackptr += IRQSTACKSIZE-64;
}

char boot_exception_stacks[(N_EXCEPTION_STACKS - 1) * EXCEPTION_STKSZ +
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

void __cpuinit check_efer(void)
{
	unsigned long efer;

	rdmsrl(MSR_EFER, efer);
	if (!(efer & EFER_NX) || do_not_nx)
		__supported_pte_mask &= ~_PAGE_NX;
}

unsigned long kernel_eflags;

/*
 * Copies of the original ist values from the tss are only accessed during
 * debugging, no special alignment required.
 */
DEFINE_PER_CPU(struct orig_ist, orig_ist);

/*
 * cpu_init() initializes state that is per-CPU. Some data is already
 * initialized (naturally) in the bootstrap process, such as the GDT
 * and IDT. We reload them nevertheless, this function acts as a
 * 'CPU state barrier', nothing should get across.
 * A lot of state is already set up in PDA init.
 */
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

	if (cpu_test_and_set(cpu, cpu_initialized))
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

	/*
	 * set up and load the per-CPU TSS
	 */
	for (v = 0; v < N_EXCEPTION_STACKS; v++) {
		static const unsigned int order[N_EXCEPTION_STACKS] = {
			[0 ... N_EXCEPTION_STACKS - 1] = EXCEPTION_STACK_ORDER,
			[DEBUG_STACK - 1] = DEBUG_STACK_ORDER
		};
		if (cpu) {
			estacks = (char *)__get_free_pages(GFP_ATOMIC, order[v]);
			if (!estacks)
				panic("Cannot allocate exception stack %ld %d\n",
				      v, cpu);
		}
		estacks += PAGE_SIZE << order[v];
		orig_ist->ist[v] = t->x86_tss.ist[v] = (unsigned long)estacks;
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
