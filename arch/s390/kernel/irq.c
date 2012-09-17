/*
 *    Copyright IBM Corp. 2004, 2011
 *    Author(s): Martin Schwidefsky <schwidefsky@de.ibm.com>,
 *		 Holger Smolinski <Holger.Smolinski@de.ibm.com>,
 *		 Thomas Spatzier <tspat@de.ibm.com>,
 *
 * This file contains interrupt related functions.
 */

#include <linux/kernel_stat.h>
#include <linux/interrupt.h>
#include <linux/seq_file.h>
#include <linux/proc_fs.h>
#include <linux/profile.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/ftrace.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/cpu.h>
#include <asm/irq_regs.h>
#include <asm/cputime.h>
#include <asm/lowcore.h>
#include <asm/irq.h>
#include "entry.h"

struct irq_class {
	char *name;
	char *desc;
};

static const struct irq_class intrclass_names[] = {
	[EXTERNAL_INTERRUPT] = {.name = "EXT"},
	[IO_INTERRUPT]	     = {.name = "I/O"},
	[EXTINT_CLK] = {.name = "CLK", .desc = "[EXT] Clock Comparator"},
	[EXTINT_EXC] = {.name = "EXC", .desc = "[EXT] External Call"},
	[EXTINT_EMS] = {.name = "EMS", .desc = "[EXT] Emergency Signal"},
	[EXTINT_TMR] = {.name = "TMR", .desc = "[EXT] CPU Timer"},
	[EXTINT_TLA] = {.name = "TAL", .desc = "[EXT] Timing Alert"},
	[EXTINT_PFL] = {.name = "PFL", .desc = "[EXT] Pseudo Page Fault"},
	[EXTINT_DSD] = {.name = "DSD", .desc = "[EXT] DASD Diag"},
	[EXTINT_VRT] = {.name = "VRT", .desc = "[EXT] Virtio"},
	[EXTINT_SCP] = {.name = "SCP", .desc = "[EXT] Service Call"},
	[EXTINT_IUC] = {.name = "IUC", .desc = "[EXT] IUCV"},
	[EXTINT_CMS] = {.name = "CMS", .desc = "[EXT] CPU-Measurement: Sampling"},
	[EXTINT_CMC] = {.name = "CMC", .desc = "[EXT] CPU-Measurement: Counter"},
	[EXTINT_CMR] = {.name = "CMR", .desc = "[EXT] CPU-Measurement: RI"},
	[IOINT_CIO]  = {.name = "CIO", .desc = "[I/O] Common I/O Layer Interrupt"},
	[IOINT_QAI]  = {.name = "QAI", .desc = "[I/O] QDIO Adapter Interrupt"},
	[IOINT_DAS]  = {.name = "DAS", .desc = "[I/O] DASD"},
	[IOINT_C15]  = {.name = "C15", .desc = "[I/O] 3215"},
	[IOINT_C70]  = {.name = "C70", .desc = "[I/O] 3270"},
	[IOINT_TAP]  = {.name = "TAP", .desc = "[I/O] Tape"},
	[IOINT_VMR]  = {.name = "VMR", .desc = "[I/O] Unit Record Devices"},
	[IOINT_LCS]  = {.name = "LCS", .desc = "[I/O] LCS"},
	[IOINT_CLW]  = {.name = "CLW", .desc = "[I/O] CLAW"},
	[IOINT_CTC]  = {.name = "CTC", .desc = "[I/O] CTC"},
	[IOINT_APB]  = {.name = "APB", .desc = "[I/O] AP Bus"},
	[IOINT_ADM]  = {.name = "ADM", .desc = "[I/O] EADM Subchannel"},
	[IOINT_CSC]  = {.name = "CSC", .desc = "[I/O] CHSC Subchannel"},
	[NMI_NMI]    = {.name = "NMI", .desc = "[NMI] Machine Check"},
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
		} else {
			/* We are already on the async stack. */
			__do_softirq();
		}
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

/*
 * ext_int_hash[index] is the list head for all external interrupts that hash
 * to this index.
 */
static struct list_head ext_int_hash[256];

struct ext_int_info {
	ext_int_handler_t handler;
	u16 code;
	struct list_head entry;
	struct rcu_head rcu;
};

/* ext_int_hash_lock protects the handler lists for external interrupts */
DEFINE_SPINLOCK(ext_int_hash_lock);

static void __init init_external_interrupts(void)
{
	int idx;

	for (idx = 0; idx < ARRAY_SIZE(ext_int_hash); idx++)
		INIT_LIST_HEAD(&ext_int_hash[idx]);
}

static inline int ext_hash(u16 code)
{
	return (code + (code >> 9)) & 0xff;
}

int register_external_interrupt(u16 code, ext_int_handler_t handler)
{
	struct ext_int_info *p;
	unsigned long flags;
	int index;

	p = kmalloc(sizeof(*p), GFP_ATOMIC);
	if (!p)
		return -ENOMEM;
	p->code = code;
	p->handler = handler;
	index = ext_hash(code);

	spin_lock_irqsave(&ext_int_hash_lock, flags);
	list_add_rcu(&p->entry, &ext_int_hash[index]);
	spin_unlock_irqrestore(&ext_int_hash_lock, flags);
	return 0;
}
EXPORT_SYMBOL(register_external_interrupt);

int unregister_external_interrupt(u16 code, ext_int_handler_t handler)
{
	struct ext_int_info *p;
	unsigned long flags;
	int index = ext_hash(code);

	spin_lock_irqsave(&ext_int_hash_lock, flags);
	list_for_each_entry_rcu(p, &ext_int_hash[index], entry) {
		if (p->code == code && p->handler == handler) {
			list_del_rcu(&p->entry);
			kfree_rcu(p, rcu);
		}
	}
	spin_unlock_irqrestore(&ext_int_hash_lock, flags);
	return 0;
}
EXPORT_SYMBOL(unregister_external_interrupt);

void __irq_entry do_extint(struct pt_regs *regs, struct ext_code ext_code,
			   unsigned int param32, unsigned long param64)
{
	struct pt_regs *old_regs;
	struct ext_int_info *p;
	int index;

	old_regs = set_irq_regs(regs);
	irq_enter();
	if (S390_lowcore.int_clock >= S390_lowcore.clock_comparator) {
		/* Serve timer interrupts first. */
		clock_comparator_work();
	}
	kstat_cpu(smp_processor_id()).irqs[EXTERNAL_INTERRUPT]++;
	if (ext_code.code != 0x1004)
		__get_cpu_var(s390_idle).nohz_delay = 1;

	index = ext_hash(ext_code.code);
	rcu_read_lock();
	list_for_each_entry_rcu(p, &ext_int_hash[index], entry)
		if (likely(p->code == ext_code.code))
			p->handler(ext_code, param32, param64);
	rcu_read_unlock();
	irq_exit();
	set_irq_regs(old_regs);
}

void __init init_IRQ(void)
{
	init_external_interrupts();
}

static DEFINE_SPINLOCK(sc_irq_lock);
static int sc_irq_refcount;

void service_subclass_irq_register(void)
{
	spin_lock(&sc_irq_lock);
	if (!sc_irq_refcount)
		ctl_set_bit(0, 9);
	sc_irq_refcount++;
	spin_unlock(&sc_irq_lock);
}
EXPORT_SYMBOL(service_subclass_irq_register);

void service_subclass_irq_unregister(void)
{
	spin_lock(&sc_irq_lock);
	sc_irq_refcount--;
	if (!sc_irq_refcount)
		ctl_clear_bit(0, 9);
	spin_unlock(&sc_irq_lock);
}
EXPORT_SYMBOL(service_subclass_irq_unregister);

static DEFINE_SPINLOCK(ma_subclass_lock);
static int ma_subclass_refcount;

void measurement_alert_subclass_register(void)
{
	spin_lock(&ma_subclass_lock);
	if (!ma_subclass_refcount)
		ctl_set_bit(0, 5);
	ma_subclass_refcount++;
	spin_unlock(&ma_subclass_lock);
}
EXPORT_SYMBOL(measurement_alert_subclass_register);

void measurement_alert_subclass_unregister(void)
{
	spin_lock(&ma_subclass_lock);
	ma_subclass_refcount--;
	if (!ma_subclass_refcount)
		ctl_clear_bit(0, 5);
	spin_unlock(&ma_subclass_lock);
}
EXPORT_SYMBOL(measurement_alert_subclass_unregister);
