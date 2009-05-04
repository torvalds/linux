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

#include <asm/elf.h>
#include <asm/lowcore.h>
#include <asm/param.h>

void __cpuinit print_cpu_info(void)
{
	pr_info("Processor %d started, address %d, identification %06X\n",
		S390_lowcore.cpu_nr, S390_lowcore.cpu_addr,
		S390_lowcore.cpu_id.ident);
}

/*
 * show_cpuinfo - Get information on one CPU for use by procfs.
 */

static int show_cpuinfo(struct seq_file *m, void *v)
{
	static const char *hwcap_str[9] = {
		"esan3", "zarch", "stfle", "msa", "ldisp", "eimm", "dfp",
		"edat", "etf3eh"
	};
	struct _lowcore *lc;
	unsigned long n = (unsigned long) v - 1;
	int i;

	s390_adjust_jiffies();
	preempt_disable();
	if (!n) {
		seq_printf(m, "vendor_id       : IBM/S390\n"
			   "# processors    : %i\n"
			   "bogomips per cpu: %lu.%02lu\n",
			   num_online_cpus(), loops_per_jiffy/(500000/HZ),
			   (loops_per_jiffy/(5000/HZ))%100);
		seq_puts(m, "features\t: ");
		for (i = 0; i < 9; i++)
			if (hwcap_str[i] && (elf_hwcap & (1UL << i)))
				seq_printf(m, "%s ", hwcap_str[i]);
		seq_puts(m, "\n");
	}

	if (cpu_online(n)) {
#ifdef CONFIG_SMP
		lc = (smp_processor_id() == n) ?
			&S390_lowcore : lowcore_ptr[n];
#else
		lc = &S390_lowcore;
#endif
		seq_printf(m, "processor %li: "
			   "version = %02X,  "
			   "identification = %06X,  "
			   "machine = %04X\n",
			   n, lc->cpu_id.version,
			   lc->cpu_id.ident,
			   lc->cpu_id.machine);
	}
	preempt_enable();
	return 0;
}

static void *c_start(struct seq_file *m, loff_t *pos)
{
	return *pos < NR_CPUS ? (void *)((unsigned long) *pos + 1) : NULL;
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

