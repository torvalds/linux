/*
 *  arch/s390/kernel/processor.c
 *
 *  Copyright IBM Corp. 2008
 *  Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com)
 */

#define KMSG_COMPONENT "cpu"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/smp.h>
#include <linux/seq_file.h>
#include <linux/delay.h>
#include <linux/cpu.h>
#include <asm/elf.h>
#include <asm/lowcore.h>
#include <asm/param.h>

static DEFINE_PER_CPU(struct cpuid, cpu_id);

/*
 * cpu_init - initializes state that is per-CPU.
 */
void __cpuinit cpu_init(void)
{
	struct cpuid *id = &per_cpu(cpu_id, smp_processor_id());
	struct s390_idle_data *idle = &__get_cpu_var(s390_idle);

	get_cpu_id(id);
	atomic_inc(&init_mm.mm_count);
	current->active_mm = &init_mm;
	BUG_ON(current->mm);
	enter_lazy_tlb(&init_mm, current);
	memset(idle, 0, sizeof(*idle));
}

/*
 * show_cpuinfo - Get information on one CPU for use by procfs.
 */
static int show_cpuinfo(struct seq_file *m, void *v)
{
	static const char *hwcap_str[10] = {
		"esan3", "zarch", "stfle", "msa", "ldisp", "eimm", "dfp",
		"edat", "etf3eh", "highgprs"
	};
	unsigned long n = (unsigned long) v - 1;
	int i;

	if (!n) {
		s390_adjust_jiffies();
		seq_printf(m, "vendor_id       : IBM/S390\n"
			   "# processors    : %i\n"
			   "bogomips per cpu: %lu.%02lu\n",
			   num_online_cpus(), loops_per_jiffy/(500000/HZ),
			   (loops_per_jiffy/(5000/HZ))%100);
		seq_puts(m, "features\t: ");
		for (i = 0; i < 10; i++)
			if (hwcap_str[i] && (elf_hwcap & (1UL << i)))
				seq_printf(m, "%s ", hwcap_str[i]);
		seq_puts(m, "\n");
	}
	get_online_cpus();
	if (cpu_online(n)) {
		struct cpuid *id = &per_cpu(cpu_id, n);
		seq_printf(m, "processor %li: "
			   "version = %02X,  "
			   "identification = %06X,  "
			   "machine = %04X\n",
			   n, id->version, id->ident, id->machine);
	}
	put_online_cpus();
	return 0;
}

static void *c_start(struct seq_file *m, loff_t *pos)
{
	return *pos < nr_cpu_ids ? (void *)((unsigned long) *pos + 1) : NULL;
}

static void *c_next(struct seq_file *m, void *v, loff_t *pos)
{
	++*pos;
	return c_start(m, pos);
}

static void c_stop(struct seq_file *m, void *v)
{
}

const struct seq_operations cpuinfo_op = {
	.start	= c_start,
	.next	= c_next,
	.stop	= c_stop,
	.show	= show_cpuinfo,
};

