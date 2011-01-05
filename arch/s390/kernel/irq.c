/*
 *    Copyright IBM Corp. 2004,2010
 *    Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com),
 *		 Thomas Spatzier (tspat@de.ibm.com)
 *
 * This file contains interrupt related functions.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kernel_stat.h>
#include <linux/interrupt.h>
#include <linux/seq_file.h>
#include <linux/cpu.h>
#include <linux/proc_fs.h>
#include <linux/profile.h>

struct irq_class {
	char *name;
	char *desc;
};

static const struct irq_class intrclass_names[] = {
	{.name = "EXT" },
	{.name = "I/O" },
	{.name = "CLK", .desc = "[EXT] Clock Comparator" },
	{.name = "IPI", .desc = "[EXT] Signal Processor" },
	{.name = "TMR", .desc = "[EXT] CPU Timer" },
	{.name = "TAL", .desc = "[EXT] Timing Alert" },
	{.name = "PFL", .desc = "[EXT] Pseudo Page Fault" },
	{.name = "DSD", .desc = "[EXT] DASD Diag" },
	{.name = "VRT", .desc = "[EXT] Virtio" },
	{.name = "SCP", .desc = "[EXT] Service Call" },
	{.name = "IUC", .desc = "[EXT] IUCV" },
	{.name = "QAI", .desc = "[I/O] QDIO Adapter Interrupt" },
	{.name = "QDI", .desc = "[I/O] QDIO Interrupt" },
	{.name = "DAS", .desc = "[I/O] DASD" },
	{.name = "C15", .desc = "[I/O] 3215" },
	{.name = "C70", .desc = "[I/O] 3270" },
	{.name = "TAP", .desc = "[I/O] Tape" },
	{.name = "VMR", .desc = "[I/O] Unit Record Devices" },
	{.name = "LCS", .desc = "[I/O] LCS" },
	{.name = "NMI", .desc = "[NMI] Machine Check" },
};

/*
 * show_interrupts is needed by /proc/interrupts.
 */
int show_interrupts(struct seq_file *p, void *v)
{
	int i = *(loff_t *) v, j;

	get_online_cpus();
	if (i == 0) {
		seq_puts(p, "           ");
		for_each_online_cpu(j)
			seq_printf(p, "CPU%d       ",j);
		seq_putc(p, '\n');
	}

	if (i < NR_IRQS) {
		seq_printf(p, "%s: ", intrclass_names[i].name);
#ifndef CONFIG_SMP
		seq_printf(p, "%10u ", kstat_irqs(i));
#else
		for_each_online_cpu(j)
			seq_printf(p, "%10u ", kstat_cpu(j).irqs[i]);
#endif
		if (intrclass_names[i].desc)
			seq_printf(p, "  %s", intrclass_names[i].desc);
                seq_putc(p, '\n');
        }
	put_online_cpus();
        return 0;
}

/*
 * For compatibilty only. S/390 specific setup of interrupts et al. is done
 * much later in init_channel_subsystem().
 */
void __init
init_IRQ(void)
{
	/* nothing... */
}

/*
 * Switch to the asynchronous interrupt stack for softirq execution.
 */
asmlinkage void do_softirq(void)
{
	unsigned long flags, old, new;

	if (in_interrupt())
		return;

	local_irq_save(flags);

	if (local_softirq_pending()) {
		/* Get current stack pointer. */
		asm volatile("la %0,0(15)" : "=a" (old));
		/* Check against async. stack address range. */
		new = S390_lowcore.async_stack;
		if (((new - old) >> (PAGE_SHIFT + THREAD_ORDER)) != 0) {
			/* Need to switch to the async. stack. */
			new -= STACK_FRAME_OVERHEAD;
			((struct stack_frame *) new)->back_chain = old;

			asm volatile("   la    15,0(%0)\n"
				     "   basr  14,%2\n"
				     "   la    15,0(%1)\n"
				     : : "a" (new), "a" (old),
				         "a" (__do_softirq)
				     : "0", "1", "2", "3", "4", "5", "14",
				       "cc", "memory" );
		} else
			/* We are already on the async stack. */
			__do_softirq();
	}

	local_irq_restore(flags);
}

#ifdef CONFIG_PROC_FS
void init_irq_proc(void)
{
	struct proc_dir_entry *root_irq_dir;

	root_irq_dir = proc_mkdir("irq", NULL);
	create_prof_cpu_mask(root_irq_dir);
}
#endif
