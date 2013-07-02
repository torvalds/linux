/*
 * /proc hooks for SMTC kernel
 * Copyright (C) 2005 Mips Technologies, Inc
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/cpumask.h>
#include <linux/interrupt.h>

#include <asm/cpu.h>
#include <asm/processor.h>
#include <linux/atomic.h>
#include <asm/hardirq.h>
#include <asm/mmu_context.h>
#include <asm/mipsregs.h>
#include <asm/cacheflush.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#include <asm/smtc_proc.h>

/*
 * /proc diagnostic and statistics hooks
 */

/*
 * Statistics gathered
 */
unsigned long selfipis[NR_CPUS];

struct smtc_cpu_proc smtc_cpu_stats[NR_CPUS];

atomic_t smtc_fpu_recoveries;

static int smtc_proc_show(struct seq_file *m, void *v)
{
	int i;
	extern unsigned long ebase;

	seq_printf(m, "SMTC Status Word: 0x%08x\n", smtc_status);
	seq_printf(m, "Config7: 0x%08x\n", read_c0_config7());
	seq_printf(m, "EBASE: 0x%08lx\n", ebase);
	seq_printf(m, "Counter Interrupts taken per CPU (TC)\n");
	for (i=0; i < NR_CPUS; i++)
		seq_printf(m, "%d: %ld\n", i, smtc_cpu_stats[i].timerints);
	seq_printf(m, "Self-IPIs by CPU:\n");
	for(i = 0; i < NR_CPUS; i++)
		seq_printf(m, "%d: %ld\n", i, smtc_cpu_stats[i].selfipis);
	seq_printf(m, "%d Recoveries of \"stolen\" FPU\n",
		   atomic_read(&smtc_fpu_recoveries));
	return 0;
}

static int smtc_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, smtc_proc_show, NULL);
}

static const struct file_operations smtc_proc_fops = {
	.open		= smtc_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

void init_smtc_stats(void)
{
	int i;

	for (i=0; i<NR_CPUS; i++) {
		smtc_cpu_stats[i].timerints = 0;
		smtc_cpu_stats[i].selfipis = 0;
	}

	atomic_set(&smtc_fpu_recoveries, 0);

	proc_create("smtc", 0444, NULL, &smtc_proc_fops);
}
