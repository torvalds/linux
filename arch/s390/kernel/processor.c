// SPDX-License-Identifier: GPL-2.0
/*
 *  Copyright IBM Corp. 2008
 *  Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com)
 */

#define KMSG_COMPONENT "cpu"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/stop_machine.h>
#include <linux/cpufeature.h>
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

unsigned long int_hwcap;

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

/*
 * cpu_have_feature - Test CPU features on module initialization
 */
int cpu_have_feature(unsigned int num)
{
	return elf_hwcap & (1UL << num);
}
EXPORT_SYMBOL(cpu_have_feature);

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
	};
	static const char * const int_hwcap_str[] = {
		[HWCAP_INT_NR_SIE]	= "sie",
	};
	int i, cpu;

	BUILD_BUG_ON(ARRAY_SIZE(hwcap_str) != HWCAP_NR_MAX);
	BUILD_BUG_ON(ARRAY_SIZE(int_hwcap_str) != HWCAP_INT_NR_MAX);
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
	for (i = 0; i < ARRAY_SIZE(int_hwcap_str); i++)
		if (int_hwcap_str[i] && (int_hwcap & (1UL << i)))
			seq_printf(m, "%s ", int_hwcap_str[i]);
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

/*
 * Setup hardware capabilities.
 */
static int __init setup_hwcaps(void)
{
	static const int stfl_bits[6] = { 0, 2, 7, 17, 19, 21 };
	int i;

	/*
	 * The store facility list bits numbers as found in the principles
	 * of operation are numbered with bit 1UL<<31 as number 0 to
	 * bit 1UL<<0 as number 31.
	 *   Bit 0: instructions named N3, "backported" to esa-mode
	 *   Bit 2: z/Architecture mode is active
	 *   Bit 7: the store-facility-list-extended facility is installed
	 *   Bit 17: the message-security assist is installed
	 *   Bit 19: the long-displacement facility is installed
	 *   Bit 21: the extended-immediate facility is installed
	 *   Bit 22: extended-translation facility 3 is installed
	 *   Bit 30: extended-translation facility 3 enhancement facility
	 * These get translated to:
	 *   HWCAP_ESAN3 bit 0, HWCAP_ZARCH bit 1,
	 *   HWCAP_STFLE bit 2, HWCAP_MSA bit 3,
	 *   HWCAP_LDISP bit 4, HWCAP_EIMM bit 5 and
	 *   HWCAP_ETF3EH bit 8 (22 && 30).
	 */
	for (i = 0; i < 6; i++)
		if (test_facility(stfl_bits[i]))
			elf_hwcap |= 1UL << i;

	if (test_facility(22) && test_facility(30))
		elf_hwcap |= HWCAP_ETF3EH;

	/*
	 * Check for additional facilities with store-facility-list-extended.
	 * stfle stores doublewords (8 byte) with bit 1ULL<<63 as bit 0
	 * and 1ULL<<0 as bit 63. Bits 0-31 contain the same information
	 * as stored by stfl, bits 32-xxx contain additional facilities.
	 * How many facility words are stored depends on the number of
	 * doublewords passed to the instruction. The additional facilities
	 * are:
	 *   Bit 42: decimal floating point facility is installed
	 *   Bit 44: perform floating point operation facility is installed
	 * translated to:
	 *   HWCAP_DFP bit 6 (42 && 44).
	 */
	if ((elf_hwcap & (1UL << 2)) && test_facility(42) && test_facility(44))
		elf_hwcap |= HWCAP_DFP;

	/*
	 * Huge page support HWCAP_HPAGE is bit 7.
	 */
	if (MACHINE_HAS_EDAT1)
		elf_hwcap |= HWCAP_HPAGE;

	/*
	 * 64-bit register support for 31-bit processes
	 * HWCAP_HIGH_GPRS is bit 9.
	 */
	elf_hwcap |= HWCAP_HIGH_GPRS;

	/*
	 * Transactional execution support HWCAP_TE is bit 10.
	 */
	if (MACHINE_HAS_TE)
		elf_hwcap |= HWCAP_TE;

	/*
	 * Vector extension HWCAP_VXRS is bit 11. The Vector extension
	 * can be disabled with the "novx" parameter. Use MACHINE_HAS_VX
	 * instead of facility bit 129.
	 */
	if (MACHINE_HAS_VX) {
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

	/*
	 * Guarded storage support HWCAP_GS is bit 12.
	 */
	if (MACHINE_HAS_GS)
		elf_hwcap |= HWCAP_GS;
	if (MACHINE_HAS_PCI_MIO)
		elf_hwcap |= HWCAP_PCI_MIO;

	/*
	 * Virtualization support HWCAP_INT_SIE is bit 0.
	 */
	if (sclp.has_sief2)
		int_hwcap |= HWCAP_INT_SIE;

	return 0;
}
arch_initcall(setup_hwcaps);

static int __init setup_elf_platform(void)
{
	struct cpuid cpu_id;

	get_cpu_id(&cpu_id);
	add_device_randomness(&cpu_id, sizeof(cpu_id));
	switch (cpu_id.machine) {
	case 0x2064:
	case 0x2066:
	default:	/* Use "z900" as default for 64 bit kernels. */
		strcpy(elf_platform, "z900");
		break;
	case 0x2084:
	case 0x2086:
		strcpy(elf_platform, "z990");
		break;
	case 0x2094:
	case 0x2096:
		strcpy(elf_platform, "z9-109");
		break;
	case 0x2097:
	case 0x2098:
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
	get_online_cpus();
	return c_update(pos);
}

static void *c_next(struct seq_file *m, void *v, loff_t *pos)
{
	++*pos;
	return c_update(pos);
}

static void c_stop(struct seq_file *m, void *v)
{
	put_online_cpus();
}

const struct seq_operations cpuinfo_op = {
	.start	= c_start,
	.next	= c_next,
	.stop	= c_stop,
	.show	= show_cpuinfo,
};

int s390_isolate_bp(void)
{
	if (!test_facility(82))
		return -EOPNOTSUPP;
	set_thread_flag(TIF_ISOLATE_BP);
	return 0;
}
EXPORT_SYMBOL(s390_isolate_bp);

int s390_isolate_bp_guest(void)
{
	if (!test_facility(82))
		return -EOPNOTSUPP;
	set_thread_flag(TIF_ISOLATE_BP_GUEST);
	return 0;
}
EXPORT_SYMBOL(s390_isolate_bp_guest);
