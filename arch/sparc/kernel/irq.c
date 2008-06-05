/*
 *  arch/sparc/kernel/irq.c:  Interrupt request handling routines. On the
 *                            Sparc the IRQs are basically 'cast in stone'
 *                            and you are supposed to probe the prom's device
 *                            node trees to find out who's got which IRQ.
 *
 *  Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 *  Copyright (C) 1995 Miguel de Icaza (miguel@nuclecu.unam.mx)
 *  Copyright (C) 1995,2002 Pete A. Zaitcev (zaitcev@yahoo.com)
 *  Copyright (C) 1996 Dave Redman (djhr@tadpole.co.uk)
 *  Copyright (C) 1998-2000 Anton Blanchard (anton@samba.org)
 */

#include <linux/module.h>
#include <linux/sched.h>
#include <linux/ptrace.h>
#include <linux/errno.h>
#include <linux/linkage.h>
#include <linux/kernel_stat.h>
#include <linux/signal.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/random.h>
#include <linux/init.h>
#include <linux/smp.h>
#include <linux/delay.h>
#include <linux/threads.h>
#include <linux/spinlock.h>
#include <linux/seq_file.h>

#include <asm/ptrace.h>
#include <asm/processor.h>
#include <asm/system.h>
#include <asm/psr.h>
#include <asm/smp.h>
#include <asm/vaddrs.h>
#include <asm/timer.h>
#include <asm/openprom.h>
#include <asm/oplib.h>
#include <asm/traps.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/pgalloc.h>
#include <asm/pgtable.h>
#include <asm/pcic.h>
#include <asm/cacheflush.h>
#include <asm/irq_regs.h>

#include "irq.h"

#ifdef CONFIG_SMP
#define SMP_NOP2 "nop; nop;\n\t"
#define SMP_NOP3 "nop; nop; nop;\n\t"
#else
#define SMP_NOP2
#define SMP_NOP3
#endif /* SMP */
unsigned long __raw_local_irq_save(void)
{
	unsigned long retval;
	unsigned long tmp;

	__asm__ __volatile__(
		"rd	%%psr, %0\n\t"
		SMP_NOP3	/* Sun4m + Cypress + SMP bug */
		"or	%0, %2, %1\n\t"
		"wr	%1, 0, %%psr\n\t"
		"nop; nop; nop\n"
		: "=&r" (retval), "=r" (tmp)
		: "i" (PSR_PIL)
		: "memory");

	return retval;
}

void raw_local_irq_enable(void)
{
	unsigned long tmp;

	__asm__ __volatile__(
		"rd	%%psr, %0\n\t"
		SMP_NOP3	/* Sun4m + Cypress + SMP bug */
		"andn	%0, %1, %0\n\t"
		"wr	%0, 0, %%psr\n\t"
		"nop; nop; nop\n"
		: "=&r" (tmp)
		: "i" (PSR_PIL)
		: "memory");
}

void raw_local_irq_restore(unsigned long old_psr)
{
	unsigned long tmp;

	__asm__ __volatile__(
		"rd	%%psr, %0\n\t"
		"and	%2, %1, %2\n\t"
		SMP_NOP2	/* Sun4m + Cypress + SMP bug */
		"andn	%0, %1, %0\n\t"
		"wr	%0, %2, %%psr\n\t"
		"nop; nop; nop\n"
		: "=&r" (tmp)
		: "i" (PSR_PIL), "r" (old_psr)
		: "memory");
}

EXPORT_SYMBOL(__raw_local_irq_save);
EXPORT_SYMBOL(raw_local_irq_enable);
EXPORT_SYMBOL(raw_local_irq_restore);

/*
 * Dave Redman (djhr@tadpole.co.uk)
 *
 * IRQ numbers.. These are no longer restricted to 15..
 *
 * this is done to enable SBUS cards and onboard IO to be masked
 * correctly. using the interrupt level isn't good enough.
 *
 * For example:
 *   A device interrupting at sbus level6 and the Floppy both come in
 *   at IRQ11, but enabling and disabling them requires writing to
 *   different bits in the SLAVIO/SEC.
 *
 * As a result of these changes sun4m machines could now support
 * directed CPU interrupts using the existing enable/disable irq code
 * with tweaks.
 *
 */

static void irq_panic(void)
{
    extern char *cputypval;
    prom_printf("machine: %s doesn't have irq handlers defined!\n",cputypval);
    prom_halt();
}

void (*sparc_init_timers)(irq_handler_t ) =
    (void (*)(irq_handler_t )) irq_panic;

/*
 * Dave Redman (djhr@tadpole.co.uk)
 *
 * There used to be extern calls and hard coded values here.. very sucky!
 * instead, because some of the devices attach very early, I do something
 * equally sucky but at least we'll never try to free statically allocated
 * space or call kmalloc before kmalloc_init :(.
 * 
 * In fact it's the timer10 that attaches first.. then timer14
 * then kmalloc_init is called.. then the tty interrupts attach.
 * hmmm....
 *
 */
#define MAX_STATIC_ALLOC	4
struct irqaction static_irqaction[MAX_STATIC_ALLOC];
int static_irq_count;

static struct {
	struct irqaction *action;
	int flags;
} sparc_irq[NR_IRQS];
#define SPARC_IRQ_INPROGRESS 1

/* Used to protect the IRQ action lists */
DEFINE_SPINLOCK(irq_action_lock);

int show_interrupts(struct seq_file *p, void *v)
{
	int i = *(loff_t *) v;
	struct irqaction * action;
	unsigned long flags;
#ifdef CONFIG_SMP
	int j;
#endif

	if (sparc_cpu_model == sun4d) {
		extern int show_sun4d_interrupts(struct seq_file *, void *);
		
		return show_sun4d_interrupts(p, v);
	}
	spin_lock_irqsave(&irq_action_lock, flags);
	if (i < NR_IRQS) {
		action = sparc_irq[i].action;
		if (!action) 
			goto out_unlock;
		seq_printf(p, "%3d: ", i);
#ifndef CONFIG_SMP
		seq_printf(p, "%10u ", kstat_irqs(i));
#else
		for_each_online_cpu(j) {
			seq_printf(p, "%10u ",
				    kstat_cpu(j).irqs[i]);
		}
#endif
		seq_printf(p, " %c %s",
			(action->flags & IRQF_DISABLED) ? '+' : ' ',
			action->name);
		for (action=action->next; action; action = action->next) {
			seq_printf(p, ",%s %s",
				(action->flags & IRQF_DISABLED) ? " +" : "",
				action->name);
		}
		seq_putc(p, '\n');
	}
out_unlock:
	spin_unlock_irqrestore(&irq_action_lock, flags);
	return 0;
}

void free_irq(unsigned int irq, void *dev_id)
{
	struct irqaction * action;
	struct irqaction **actionp;
        unsigned long flags;
	unsigned int cpu_irq;
	
	if (sparc_cpu_model == sun4d) {
		extern void sun4d_free_irq(unsigned int, void *);
		
		sun4d_free_irq(irq, dev_id);
		return;
	}
	cpu_irq = irq & (NR_IRQS - 1);
        if (cpu_irq > 14) {  /* 14 irq levels on the sparc */
                printk("Trying to free bogus IRQ %d\n", irq);
                return;
        }

	spin_lock_irqsave(&irq_action_lock, flags);

	actionp = &sparc_irq[cpu_irq].action;
	action = *actionp;

	if (!action->handler) {
		printk("Trying to free free IRQ%d\n",irq);
		goto out_unlock;
	}
	if (dev_id) {
		for (; action; action = action->next) {
			if (action->dev_id == dev_id)
				break;
			actionp = &action->next;
		}
		if (!action) {
			printk("Trying to free free shared IRQ%d\n",irq);
			goto out_unlock;
		}
	} else if (action->flags & IRQF_SHARED) {
		printk("Trying to free shared IRQ%d with NULL device ID\n", irq);
		goto out_unlock;
	}
	if (action->flags & SA_STATIC_ALLOC)
	{
		/* This interrupt is marked as specially allocated
		 * so it is a bad idea to free it.
		 */
		printk("Attempt to free statically allocated IRQ%d (%s)\n",
		       irq, action->name);
		goto out_unlock;
	}

	*actionp = action->next;

	spin_unlock_irqrestore(&irq_action_lock, flags);

	synchronize_irq(irq);

	spin_lock_irqsave(&irq_action_lock, flags);

	kfree(action);

	if (!sparc_irq[cpu_irq].action)
		__disable_irq(irq);

out_unlock:
	spin_unlock_irqrestore(&irq_action_lock, flags);
}

EXPORT_SYMBOL(free_irq);

/*
 * This is called when we want to synchronize with
 * interrupts. We may for example tell a device to
 * stop sending interrupts: but to make sure there
 * are no interrupts that are executing on another
 * CPU we need to call this function.
 */
#ifdef CONFIG_SMP
void synchronize_irq(unsigned int irq)
{
	unsigned int cpu_irq;

	cpu_irq = irq & (NR_IRQS - 1);
	while (sparc_irq[cpu_irq].flags & SPARC_IRQ_INPROGRESS)
		cpu_relax();
}
#endif /* SMP */

void unexpected_irq(int irq, void *dev_id, struct pt_regs * regs)
{
        int i;
	struct irqaction * action;
	unsigned int cpu_irq;
	
	cpu_irq = irq & (NR_IRQS - 1);
	action = sparc_irq[cpu_irq].action;

        printk("IO device interrupt, irq = %d\n", irq);
        printk("PC = %08lx NPC = %08lx FP=%08lx\n", regs->pc, 
		    regs->npc, regs->u_regs[14]);
	if (action) {
		printk("Expecting: ");
        	for (i = 0; i < 16; i++)
                	if (action->handler)
                        	printk("[%s:%d:0x%x] ", action->name,
				       (int) i, (unsigned int) action->handler);
	}
        printk("AIEEE\n");
	panic("bogus interrupt received");
}

void handler_irq(int irq, struct pt_regs * regs)
{
	struct pt_regs *old_regs;
	struct irqaction * action;
	int cpu = smp_processor_id();
#ifdef CONFIG_SMP
	extern void smp4m_irq_rotate(int cpu);
#endif

	old_regs = set_irq_regs(regs);
	irq_enter();
	disable_pil_irq(irq);
#ifdef CONFIG_SMP
	/* Only rotate on lower priority IRQs (scsi, ethernet, etc.). */
	if((sparc_cpu_model==sun4m) && (irq < 10))
		smp4m_irq_rotate(cpu);
#endif
	action = sparc_irq[irq].action;
	sparc_irq[irq].flags |= SPARC_IRQ_INPROGRESS;
	kstat_cpu(cpu).irqs[irq]++;
	do {
		if (!action || !action->handler)
			unexpected_irq(irq, NULL, regs);
		action->handler(irq, action->dev_id);
		action = action->next;
	} while (action);
	sparc_irq[irq].flags &= ~SPARC_IRQ_INPROGRESS;
	enable_pil_irq(irq);
	irq_exit();
	set_irq_regs(old_regs);
}

#if defined(CONFIG_BLK_DEV_FD) || defined(CONFIG_BLK_DEV_FD_MODULE)

/* Fast IRQs on the Sparc can only have one routine attached to them,
 * thus no sharing possible.
 */
static int request_fast_irq(unsigned int irq,
			    void (*handler)(void),
			    unsigned long irqflags, const char *devname)
{
	struct irqaction *action;
	unsigned long flags;
	unsigned int cpu_irq;
	int ret;
#ifdef CONFIG_SMP
	struct tt_entry *trap_table;
	extern struct tt_entry trapbase_cpu1, trapbase_cpu2, trapbase_cpu3;
#endif
	
	cpu_irq = irq & (NR_IRQS - 1);
	if(cpu_irq > 14) {
		ret = -EINVAL;
		goto out;
	}
	if(!handler) {
		ret = -EINVAL;
		goto out;
	}

	spin_lock_irqsave(&irq_action_lock, flags);

	action = sparc_irq[cpu_irq].action;
	if(action) {
		if(action->flags & IRQF_SHARED)
			panic("Trying to register fast irq when already shared.\n");
		if(irqflags & IRQF_SHARED)
			panic("Trying to register fast irq as shared.\n");

		/* Anyway, someone already owns it so cannot be made fast. */
		printk("request_fast_irq: Trying to register yet already owned.\n");
		ret = -EBUSY;
		goto out_unlock;
	}

	/* If this is flagged as statically allocated then we use our
	 * private struct which is never freed.
	 */
	if (irqflags & SA_STATIC_ALLOC) {
	    if (static_irq_count < MAX_STATIC_ALLOC)
		action = &static_irqaction[static_irq_count++];
	    else
		printk("Fast IRQ%d (%s) SA_STATIC_ALLOC failed using kmalloc\n",
		       irq, devname);
	}
	
	if (action == NULL)
	    action = kmalloc(sizeof(struct irqaction),
						 GFP_ATOMIC);
	
	if (!action) { 
		ret = -ENOMEM;
		goto out_unlock;
	}

	/* Dork with trap table if we get this far. */
#define INSTANTIATE(table) \
	table[SP_TRAP_IRQ1+(cpu_irq-1)].inst_one = SPARC_RD_PSR_L0; \
	table[SP_TRAP_IRQ1+(cpu_irq-1)].inst_two = \
		SPARC_BRANCH((unsigned long) handler, \
			     (unsigned long) &table[SP_TRAP_IRQ1+(cpu_irq-1)].inst_two);\
	table[SP_TRAP_IRQ1+(cpu_irq-1)].inst_three = SPARC_RD_WIM_L3; \
	table[SP_TRAP_IRQ1+(cpu_irq-1)].inst_four = SPARC_NOP;

	INSTANTIATE(sparc_ttable)
#ifdef CONFIG_SMP
	trap_table = &trapbase_cpu1; INSTANTIATE(trap_table)
	trap_table = &trapbase_cpu2; INSTANTIATE(trap_table)
	trap_table = &trapbase_cpu3; INSTANTIATE(trap_table)
#endif
#undef INSTANTIATE
	/*
	 * XXX Correct thing whould be to flush only I- and D-cache lines
	 * which contain the handler in question. But as of time of the
	 * writing we have no CPU-neutral interface to fine-grained flushes.
	 */
	flush_cache_all();

	action->flags = irqflags;
	cpus_clear(action->mask);
	action->name = devname;
	action->dev_id = NULL;
	action->next = NULL;

	sparc_irq[cpu_irq].action = action;

	__enable_irq(irq);

	ret = 0;
out_unlock:
	spin_unlock_irqrestore(&irq_action_lock, flags);
out:
	return ret;
}

/* These variables are used to access state from the assembler
 * interrupt handler, floppy_hardint, so we cannot put these in
 * the floppy driver image because that would not work in the
 * modular case.
 */
volatile unsigned char *fdc_status;
EXPORT_SYMBOL(fdc_status);

char *pdma_vaddr;
EXPORT_SYMBOL(pdma_vaddr);

unsigned long pdma_size;
EXPORT_SYMBOL(pdma_size);

volatile int doing_pdma;
EXPORT_SYMBOL(doing_pdma);

char *pdma_base;
EXPORT_SYMBOL(pdma_base);

unsigned long pdma_areasize;
EXPORT_SYMBOL(pdma_areasize);

extern void floppy_hardint(void);

static irq_handler_t floppy_irq_handler;

void sparc_floppy_irq(int irq, void *dev_id, struct pt_regs *regs)
{
	struct pt_regs *old_regs;
	int cpu = smp_processor_id();

	old_regs = set_irq_regs(regs);
	disable_pil_irq(irq);
	irq_enter();
	kstat_cpu(cpu).irqs[irq]++;
	floppy_irq_handler(irq, dev_id);
	irq_exit();
	enable_pil_irq(irq);
	set_irq_regs(old_regs);
	// XXX Eek, it's totally changed with preempt_count() and such
	// if (softirq_pending(cpu))
	//	do_softirq();
}

int sparc_floppy_request_irq(int irq, unsigned long flags,
			     irq_handler_t irq_handler)
{
	floppy_irq_handler = irq_handler;
	return request_fast_irq(irq, floppy_hardint, flags, "floppy");
}
EXPORT_SYMBOL(sparc_floppy_request_irq);

#endif

int request_irq(unsigned int irq,
		irq_handler_t handler,
		unsigned long irqflags, const char * devname, void *dev_id)
{
	struct irqaction * action, **actionp;
	unsigned long flags;
	unsigned int cpu_irq;
	int ret;
	
	if (sparc_cpu_model == sun4d) {
		extern int sun4d_request_irq(unsigned int, 
					     irq_handler_t ,
					     unsigned long, const char *, void *);
		return sun4d_request_irq(irq, handler, irqflags, devname, dev_id);
	}
	cpu_irq = irq & (NR_IRQS - 1);
	if(cpu_irq > 14) {
		ret = -EINVAL;
		goto out;
	}
	if (!handler) {
		ret = -EINVAL;
		goto out;
	}
	    
	spin_lock_irqsave(&irq_action_lock, flags);

	actionp = &sparc_irq[cpu_irq].action;
	action = *actionp;
	if (action) {
		if (!(action->flags & IRQF_SHARED) || !(irqflags & IRQF_SHARED)) {
			ret = -EBUSY;
			goto out_unlock;
		}
		if ((action->flags & IRQF_DISABLED) != (irqflags & IRQF_DISABLED)) {
			printk("Attempt to mix fast and slow interrupts on IRQ%d denied\n", irq);
			ret = -EBUSY;
			goto out_unlock;
		}
		for ( ; action; action = *actionp)
			actionp = &action->next;
	}

	/* If this is flagged as statically allocated then we use our
	 * private struct which is never freed.
	 */
	if (irqflags & SA_STATIC_ALLOC) {
		if (static_irq_count < MAX_STATIC_ALLOC)
			action = &static_irqaction[static_irq_count++];
		else
			printk("Request for IRQ%d (%s) SA_STATIC_ALLOC failed using kmalloc\n", irq, devname);
	}
	
	if (action == NULL)
		action = kmalloc(sizeof(struct irqaction),
						     GFP_ATOMIC);
	
	if (!action) { 
		ret = -ENOMEM;
		goto out_unlock;
	}

	action->handler = handler;
	action->flags = irqflags;
	cpus_clear(action->mask);
	action->name = devname;
	action->next = NULL;
	action->dev_id = dev_id;

	*actionp = action;

	__enable_irq(irq);

	ret = 0;
out_unlock:
	spin_unlock_irqrestore(&irq_action_lock, flags);
out:
	return ret;
}

EXPORT_SYMBOL(request_irq);

void disable_irq_nosync(unsigned int irq)
{
	return __disable_irq(irq);
}
EXPORT_SYMBOL(disable_irq_nosync);

void disable_irq(unsigned int irq)
{
	return __disable_irq(irq);
}
EXPORT_SYMBOL(disable_irq);

void enable_irq(unsigned int irq)
{
	return __enable_irq(irq);
}

EXPORT_SYMBOL(enable_irq);

/* We really don't need these at all on the Sparc.  We only have
 * stubs here because they are exported to modules.
 */
unsigned long probe_irq_on(void)
{
	return 0;
}

EXPORT_SYMBOL(probe_irq_on);

int probe_irq_off(unsigned long mask)
{
	return 0;
}

EXPORT_SYMBOL(probe_irq_off);

/* djhr
 * This could probably be made indirect too and assigned in the CPU
 * bits of the code. That would be much nicer I think and would also
 * fit in with the idea of being able to tune your kernel for your machine
 * by removing unrequired machine and device support.
 *
 */

void __init init_IRQ(void)
{
	extern void sun4c_init_IRQ( void );
	extern void sun4m_init_IRQ( void );
	extern void sun4d_init_IRQ( void );

	switch(sparc_cpu_model) {
	case sun4c:
	case sun4:
		sun4c_init_IRQ();
		break;

	case sun4m:
#ifdef CONFIG_PCI
		pcic_probe();
		if (pcic_present()) {
			sun4m_pci_init_IRQ();
			break;
		}
#endif
		sun4m_init_IRQ();
		break;
		
	case sun4d:
		sun4d_init_IRQ();
		break;

	default:
		prom_printf("Cannot initialize IRQs on this Sun machine...");
		break;
	}
	btfixup();
}

void init_irq_proc(void)
{
	/* For now, nothing... */
}
