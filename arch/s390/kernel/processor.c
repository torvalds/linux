/*
 *  Copyright IBM Corp. 2008
 *  Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com)
 */

#define KMSG_COMPONENT "cpu"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/cpufeature.h>
#include <linux/kernel.h>
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
#include <asm/smp.h>

struct cpu_info {
	unsigned int cpu_mhz_dynamic;
	unsigned int cpu_mhz_static;
	struct cpuid cpu_id;
};

static DEFINE_PER_CPU(struct cpu_info, cpu_info);

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

void notrace cpu_relax_yield(void)
{
	if (!smp_cpu_mtid && MACHINE_HAS_DIAG44) {
		diag_stat_inc(DIAG_STAT_X044);
		asm volatile("diag 0,0,0x44");
	}
	barrier();
}
EXPORT_SYMBOL(cpu_relax_yield);

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

static void show_cpu_summary(struct seq_file *m, void *v)
{
	static const char *hwcap_str[] = {
		"esan3", "zarch", "stfle", "msa", "ldisp", "eimm", "dfp",
		"edat", "etf3eh", "highgprs", "te", "vx", "vxd", "vxe"
	};
	static const char * const int_hwcap_str[] = {
		"sie"
	};
	int i, cpu;

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

static void show_cpu_mhz(struct seq_file *m, unsigned long n)
{
	struct cpu_info *c = per_cpu_ptr(&cpu_info, n);

	seq_printf(m, "cpu MHz dynamic : %d\n", c->cpu_mhz_dynamic);
	seq_printf(m, "cpu MHz static  : %d\n", c->cpu_mhz_static);
}

/*
 * show_cpuinfo - Get information on one CPU for use by procfs.
 */
static int show_cpuinfo(struct seq_file *m, void *v)
{
	unsigned long n = (unsigned long) v - 1;

	if (!n)
		show_cpu_summary(m, v);
	if (!machine_has_cpu_mhz)
		return 0;
	seq_printf(m, "\ncpu number      : %ld\n", n);
	show_cpu_mhz(m, n);
	return 0;
}

static inline void *c_update(loff_t *pos)
{
	if (*pos)
		*pos = cpumask_next(*pos - 1, cpu_online_mask);
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
