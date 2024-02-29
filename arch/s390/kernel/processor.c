// SPDX-License-Identifier: GPL-2.0
/*
 *  Copyright IBM Corp. 2008
 *  Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com)
 */

#define KMSG_COMPONENT "cpu"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/stop_machine.h>
#include <linux/bitops.h>
#include <linux/kernel.h>
#include <linux/random.h>
#include <linux/sched/mm.h>
#include <linux/init.h>
#include <linux/seq_file.h>
#include <linux/mm_types.h>
#include <linux/delay.h>
#include <linux/cpu.h>

#include <asm/diag.h>
#include <asm/facility.h>
#include <asm/elf.h>
#include <asm/lowcore.h>
#include <asm/param.h>
#include <asm/sclp.h>
#include <asm/smp.h>

unsigned long __read_mostly elf_hwcap;
char elf_platform[ELF_PLATFORM_SIZE];

struct cpu_info {
	unsigned int cpu_mhz_dynamic;
	unsigned int cpu_mhz_static;
	struct cpuid cpu_id;
};

static DEFINE_PER_CPU(struct cpu_info, cpu_info);
static DEFINE_PER_CPU(int, cpu_relax_retry);

static bool machine_has_cpu_mhz;

void __init cpu_detect_mhz_feature(void)
{
	if (test_facility(34) && __ecag(ECAG_CPU_ATTRIBUTE, 0) != -1UL)
		machine_has_cpu_mhz = true;
}

static void update_cpu_mhz(void *arg)
{
	unsigned long mhz;
	struct cpu_info *c;

	mhz = __ecag(ECAG_CPU_ATTRIBUTE, 0);
	c = this_cpu_ptr(&cpu_info);
	c->cpu_mhz_dynamic = mhz >> 32;
	c->cpu_mhz_static = mhz & 0xffffffff;
}

void s390_update_cpu_mhz(void)
{
	s390_adjust_jiffies();
	if (machine_has_cpu_mhz)
		on_each_cpu(update_cpu_mhz, NULL, 0);
}

void notrace stop_machine_yield(const struct cpumask *cpumask)
{
	int cpu, this_cpu;

	this_cpu = smp_processor_id();
	if (__this_cpu_inc_return(cpu_relax_retry) >= spin_retry) {
		__this_cpu_write(cpu_relax_retry, 0);
		cpu = cpumask_next_wrap(this_cpu, cpumask, this_cpu, false);
		if (cpu >= nr_cpu_ids)
			return;
		if (arch_vcpu_is_preempted(cpu))
			smp_yield_cpu(cpu);
	}
}

/*
 * cpu_init - initializes state that is per-CPU.
 */
void cpu_init(void)
{
	struct cpuid *id = this_cpu_ptr(&cpu_info.cpu_id);

	get_cpu_id(id);
	if (machine_has_cpu_mhz)
		update_cpu_mhz(NULL);
	mmgrab(&init_mm);
	current->active_mm = &init_mm;
	BUG_ON(current->mm);
	enter_lazy_tlb(&init_mm, current);
}

static void show_facilities(struct seq_file *m)
{
	unsigned int bit;

	seq_puts(m, "facilities      :");
	for_each_set_bit_inv(bit, (long *)&stfle_fac_list, MAX_FACILITY_BIT)
		seq_printf(m, " %d", bit);
	seq_putc(m, '\n');
}

static void show_cpu_summary(struct seq_file *m, void *v)
{
	static const char *hwcap_str[] = {
		[HWCAP_NR_ESAN3]	= "esan3",
		[HWCAP_NR_ZARCH]	= "zarch",
		[HWCAP_NR_STFLE]	= "stfle",
		[HWCAP_NR_MSA]		= "msa",
		[HWCAP_NR_LDISP]	= "ldisp",
		[HWCAP_NR_EIMM]		= "eimm",
		[HWCAP_NR_DFP]		= "dfp",
		[HWCAP_NR_HPAGE]	= "edat",
		[HWCAP_NR_ETF3EH]	= "etf3eh",
		[HWCAP_NR_HIGH_GPRS]	= "highgprs",
		[HWCAP_NR_TE]		= "te",
		[HWCAP_NR_VXRS]		= "vx",
		[HWCAP_NR_VXRS_BCD]	= "vxd",
		[HWCAP_NR_VXRS_EXT]	= "vxe",
		[HWCAP_NR_GS]		= "gs",
		[HWCAP_NR_VXRS_EXT2]	= "vxe2",
		[HWCAP_NR_VXRS_PDE]	= "vxp",
		[HWCAP_NR_SORT]		= "sort",
		[HWCAP_NR_DFLT]		= "dflt",
		[HWCAP_NR_VXRS_PDE2]	= "vxp2",
		[HWCAP_NR_NNPA]		= "nnpa",
		[HWCAP_NR_PCI_MIO]	= "pcimio",
		[HWCAP_NR_SIE]		= "sie",
	};
	int i, cpu;

	BUILD_BUG_ON(ARRAY_SIZE(hwcap_str) != HWCAP_NR_MAX);
	seq_printf(m, "vendor_id       : IBM/S390\n"
		   "# processors    : %i\n"
		   "bogomips per cpu: %lu.%02lu\n",
		   num_online_cpus(), loops_per_jiffy/(500000/HZ),
		   (loops_per_jiffy/(5000/HZ))%100);
	seq_printf(m, "max thread id   : %d\n", smp_cpu_mtid);
	seq_puts(m, "features\t: ");
	for (i = 0; i < ARRAY_SIZE(hwcap_str); i++)
		if (hwcap_str[i] && (elf_hwcap & (1UL << i)))
			seq_printf(m, "%s ", hwcap_str[i]);
	seq_puts(m, "\n");
	show_facilities(m);
	show_cacheinfo(m);
	for_each_online_cpu(cpu) {
		struct cpuid *id = &per_cpu(cpu_info.cpu_id, cpu);

		seq_printf(m, "processor %d: "
			   "version = %02X,  "
			   "identification = %06X,  "
			   "machine = %04X\n",
			   cpu, id->version, id->ident, id->machine);
	}
}

static int __init setup_hwcaps(void)
{
	/* instructions named N3, "backported" to esa-mode */
	elf_hwcap |= HWCAP_ESAN3;

	/* z/Architecture mode active */
	elf_hwcap |= HWCAP_ZARCH;

	/* store-facility-list-extended */
	if (test_facility(7))
		elf_hwcap |= HWCAP_STFLE;

	/* message-security assist */
	if (test_facility(17))
		elf_hwcap |= HWCAP_MSA;

	/* long-displacement */
	if (test_facility(19))
		elf_hwcap |= HWCAP_LDISP;

	/* extended-immediate */
	elf_hwcap |= HWCAP_EIMM;

	/* extended-translation facility 3 enhancement */
	if (test_facility(22) && test_facility(30))
		elf_hwcap |= HWCAP_ETF3EH;

	/* decimal floating point & perform floating point operation */
	if (test_facility(42) && test_facility(44))
		elf_hwcap |= HWCAP_DFP;

	/* huge page support */
	if (MACHINE_HAS_EDAT1)
		elf_hwcap |= HWCAP_HPAGE;

	/* 64-bit register support for 31-bit processes */
	elf_hwcap |= HWCAP_HIGH_GPRS;

	/* transactional execution */
	if (MACHINE_HAS_TE)
		elf_hwcap |= HWCAP_TE;

	/* vector */
	if (test_facility(129)) {
		elf_hwcap |= HWCAP_VXRS;
		if (test_facility(134))
			elf_hwcap |= HWCAP_VXRS_BCD;
		if (test_facility(135))
			elf_hwcap |= HWCAP_VXRS_EXT;
		if (test_facility(148))
			elf_hwcap |= HWCAP_VXRS_EXT2;
		if (test_facility(152))
			elf_hwcap |= HWCAP_VXRS_PDE;
		if (test_facility(192))
			elf_hwcap |= HWCAP_VXRS_PDE2;
	}

	if (test_facility(150))
		elf_hwcap |= HWCAP_SORT;

	if (test_facility(151))
		elf_hwcap |= HWCAP_DFLT;

	if (test_facility(165))
		elf_hwcap |= HWCAP_NNPA;

	/* guarded storage */
	if (MACHINE_HAS_GS)
		elf_hwcap |= HWCAP_GS;

	if (MACHINE_HAS_PCI_MIO)
		elf_hwcap |= HWCAP_PCI_MIO;

	/* virtualization support */
	if (sclp.has_sief2)
		elf_hwcap |= HWCAP_SIE;

	return 0;
}
arch_initcall(setup_hwcaps);

static int __init setup_elf_platform(void)
{
	struct cpuid cpu_id;

	get_cpu_id(&cpu_id);
	add_device_randomness(&cpu_id, sizeof(cpu_id));
	switch (cpu_id.machine) {
	default:	/* Use "z10" as default. */
		strcpy(elf_platform, "z10");
		break;
	case 0x2817:
	case 0x2818:
		strcpy(elf_platform, "z196");
		break;
	case 0x2827:
	case 0x2828:
		strcpy(elf_platform, "zEC12");
		break;
	case 0x2964:
	case 0x2965:
		strcpy(elf_platform, "z13");
		break;
	case 0x3906:
	case 0x3907:
		strcpy(elf_platform, "z14");
		break;
	case 0x8561:
	case 0x8562:
		strcpy(elf_platform, "z15");
		break;
	case 0x3931:
	case 0x3932:
		strcpy(elf_platform, "z16");
		break;
	}
	return 0;
}
arch_initcall(setup_elf_platform);

static void show_cpu_topology(struct seq_file *m, unsigned long n)
{
#ifdef CONFIG_SCHED_TOPOLOGY
	seq_printf(m, "physical id     : %d\n", topology_physical_package_id(n));
	seq_printf(m, "core id         : %d\n", topology_core_id(n));
	seq_printf(m, "book id         : %d\n", topology_book_id(n));
	seq_printf(m, "drawer id       : %d\n", topology_drawer_id(n));
	seq_printf(m, "dedicated       : %d\n", topology_cpu_dedicated(n));
	seq_printf(m, "address         : %d\n", smp_cpu_get_cpu_address(n));
	seq_printf(m, "siblings        : %d\n", cpumask_weight(topology_core_cpumask(n)));
	seq_printf(m, "cpu cores       : %d\n", topology_booted_cores(n));
#endif /* CONFIG_SCHED_TOPOLOGY */
}

static void show_cpu_ids(struct seq_file *m, unsigned long n)
{
	struct cpuid *id = &per_cpu(cpu_info.cpu_id, n);

	seq_printf(m, "version         : %02X\n", id->version);
	seq_printf(m, "identification  : %06X\n", id->ident);
	seq_printf(m, "machine         : %04X\n", id->machine);
}

static void show_cpu_mhz(struct seq_file *m, unsigned long n)
{
	struct cpu_info *c = per_cpu_ptr(&cpu_info, n);

	if (!machine_has_cpu_mhz)
		return;
	seq_printf(m, "cpu MHz dynamic : %d\n", c->cpu_mhz_dynamic);
	seq_printf(m, "cpu MHz static  : %d\n", c->cpu_mhz_static);
}

/*
 * show_cpuinfo - Get information on one CPU for use by procfs.
 */
static int show_cpuinfo(struct seq_file *m, void *v)
{
	unsigned long n = (unsigned long) v - 1;
	unsigned long first = cpumask_first(cpu_online_mask);

	if (n == first)
		show_cpu_summary(m, v);
	seq_printf(m, "\ncpu number      : %ld\n", n);
	show_cpu_topology(m, n);
	show_cpu_ids(m, n);
	show_cpu_mhz(m, n);
	return 0;
}

static inline void *c_update(loff_t *pos)
{
	if (*pos)
		*pos = cpumask_next(*pos - 1, cpu_online_mask);
	else
		*pos = cpumask_first(cpu_online_mask);
	return *pos < nr_cpu_ids ? (void *)*pos + 1 : NULL;
}

static void *c_start(struct seq_file *m, loff_t *pos)
{
	cpus_read_lock();
	return c_update(pos);
}

static void *c_next(struct seq_file *m, void *v, loff_t *pos)
{
	++*pos;
	return c_update(pos);
}

static void c_stop(struct seq_file *m, void *v)
{
	cpus_read_unlock();
}

const struct seq_operations cpuinfo_op = {
	.start	= c_start,
	.next	= c_next,
	.stop	= c_stop,
	.show	= show_cpuinfo,
};
