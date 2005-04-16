/*
 *	linux/arch/alpha/kernel/irq.c
 *
 *	Copyright (C) 1995 Linus Torvalds
 *
 * This file contains the code used by various IRQ handling routines:
 * asking for different IRQ's should be done through these routines
 * instead of just grabbing them. Thus setups with different IRQ numbers
 * shouldn't result in any weird surprises, and installing new handlers
 * should be easier.
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/kernel_stat.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/ptrace.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/random.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/profile.h>
#include <linux/bitops.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/uaccess.h>

/*
 * Controller mappings for all interrupt sources:
 */
irq_desc_t irq_desc[NR_IRQS] __cacheline_aligned = {
	[0 ... NR_IRQS-1] = {
		.handler = &no_irq_type,
		.lock = SPIN_LOCK_UNLOCKED
	}
};

static void register_irq_proc(unsigned int irq);

volatile unsigned long irq_err_count;

/*
 * Special irq handlers.
 */

irqreturn_t no_action(int cpl, void *dev_id, struct pt_regs *regs)
{
	return IRQ_NONE;
}

/*
 * Generic no controller code
 */

static void no_irq_enable_disable(unsigned int irq) { }
static unsigned int no_irq_startup(unsigned int irq) { return 0; }

static void
no_irq_ack(unsigned int irq)
{
	irq_err_count++;
	printk(KERN_CRIT "Unexpected IRQ trap at vector %u\n", irq);
}

struct hw_interrupt_type no_irq_type = {
	.typename	= "none",
	.startup	= no_irq_startup,
	.shutdown	= no_irq_enable_disable,
	.enable		= no_irq_enable_disable,
	.disable	= no_irq_enable_disable,
	.ack		= no_irq_ack,
	.end		= no_irq_enable_disable,
};

int
handle_IRQ_event(unsigned int irq, struct pt_regs *regs,
		 struct irqaction *action)
{
	int status = 1;	/* Force the "do bottom halves" bit */
	int ret;

	do {
		if (!(action->flags & SA_INTERRUPT))
			local_irq_enable();
		else
			local_irq_disable();

		ret = action->handler(irq, action->dev_id, regs);
		if (ret == IRQ_HANDLED)
			status |= action->flags;
		action = action->next;
	} while (action);
	if (status & SA_SAMPLE_RANDOM)
		add_interrupt_randomness(irq);
	local_irq_disable();

	return status;
}

/*
 * Generic enable/disable code: this just calls
 * down into the PIC-specific version for the actual
 * hardware disable after having gotten the irq
 * controller lock. 
 */
void inline
disable_irq_nosync(unsigned int irq)
{
	irq_desc_t *desc = irq_desc + irq;
	unsigned long flags;

	spin_lock_irqsave(&desc->lock, flags);
	if (!desc->depth++) {
		desc->status |= IRQ_DISABLED;
		desc->handler->disable(irq);
	}
	spin_unlock_irqrestore(&desc->lock, flags);
}

/*
 * Synchronous version of the above, making sure the IRQ is
 * no longer running on any other IRQ..
 */
void
disable_irq(unsigned int irq)
{
	disable_irq_nosync(irq);
	synchronize_irq(irq);
}

void
enable_irq(unsigned int irq)
{
	irq_desc_t *desc = irq_desc + irq;
	unsigned long flags;

	spin_lock_irqsave(&desc->lock, flags);
	switch (desc->depth) {
	case 1: {
		unsigned int status = desc->status & ~IRQ_DISABLED;
		desc->status = status;
		if ((status & (IRQ_PENDING | IRQ_REPLAY)) == IRQ_PENDING) {
			desc->status = status | IRQ_REPLAY;
			hw_resend_irq(desc->handler,irq);
		}
		desc->handler->enable(irq);
		/* fall-through */
	}
	default:
		desc->depth--;
		break;
	case 0:
		printk(KERN_ERR "enable_irq() unbalanced from %p\n",
		       __builtin_return_address(0));
	}
	spin_unlock_irqrestore(&desc->lock, flags);
}

int
setup_irq(unsigned int irq, struct irqaction * new)
{
	int shared = 0;
	struct irqaction *old, **p;
	unsigned long flags;
	irq_desc_t *desc = irq_desc + irq;

        if (desc->handler == &no_irq_type)
		return -ENOSYS;

	/*
	 * Some drivers like serial.c use request_irq() heavily,
	 * so we have to be careful not to interfere with a
	 * running system.
	 */
	if (new->flags & SA_SAMPLE_RANDOM) {
		/*
		 * This function might sleep, we want to call it first,
		 * outside of the atomic block.
		 * Yes, this might clear the entropy pool if the wrong
		 * driver is attempted to be loaded, without actually
		 * installing a new handler, but is this really a problem,
		 * only the sysadmin is able to do this.
		 */
		rand_initialize_irq(irq);
	}

	/*
	 * The following block of code has to be executed atomically
	 */
	spin_lock_irqsave(&desc->lock,flags);
	p = &desc->action;
	if ((old = *p) != NULL) {
		/* Can't share interrupts unless both agree to */
		if (!(old->flags & new->flags & SA_SHIRQ)) {
			spin_unlock_irqrestore(&desc->lock,flags);
			return -EBUSY;
		}

		/* add new interrupt at end of irq queue */
		do {
			p = &old->next;
			old = *p;
		} while (old);
		shared = 1;
	}

	*p = new;

	if (!shared) {
		desc->depth = 0;
		desc->status &=
		    ~(IRQ_DISABLED|IRQ_AUTODETECT|IRQ_WAITING|IRQ_INPROGRESS);
		desc->handler->startup(irq);
	}
	spin_unlock_irqrestore(&desc->lock,flags);

	return 0;
}

static struct proc_dir_entry * root_irq_dir;
static struct proc_dir_entry * irq_dir[NR_IRQS];

#ifdef CONFIG_SMP 
static struct proc_dir_entry * smp_affinity_entry[NR_IRQS];
static char irq_user_affinity[NR_IRQS];
static cpumask_t irq_affinity[NR_IRQS] = { [0 ... NR_IRQS-1] = CPU_MASK_ALL };

static void
select_smp_affinity(int irq)
{
	static int last_cpu;
	int cpu = last_cpu + 1;

	if (! irq_desc[irq].handler->set_affinity || irq_user_affinity[irq])
		return;

	while (!cpu_possible(cpu))
		cpu = (cpu < (NR_CPUS-1) ? cpu + 1 : 0);
	last_cpu = cpu;

	irq_affinity[irq] = cpumask_of_cpu(cpu);
	irq_desc[irq].handler->set_affinity(irq, cpumask_of_cpu(cpu));
}

static int
irq_affinity_read_proc (char *page, char **start, off_t off,
			int count, int *eof, void *data)
{
	int len = cpumask_scnprintf(page, count, irq_affinity[(long)data]);
	if (count - len < 2)
		return -EINVAL;
	len += sprintf(page + len, "\n");
	return len;
}

static int
irq_affinity_write_proc(struct file *file, const char __user *buffer,
			unsigned long count, void *data)
{
	int irq = (long) data, full_count = count, err;
	cpumask_t new_value;

	if (!irq_desc[irq].handler->set_affinity)
		return -EIO;

	err = cpumask_parse(buffer, count, new_value);

	/* The special value 0 means release control of the
	   affinity to kernel.  */
	cpus_and(new_value, new_value, cpu_online_map);
	if (cpus_empty(new_value)) {
		irq_user_affinity[irq] = 0;
		select_smp_affinity(irq);
	}
	/* Do not allow disabling IRQs completely - it's a too easy
	   way to make the system unusable accidentally :-) At least
	   one online CPU still has to be targeted.  */
	else {
		irq_affinity[irq] = new_value;
		irq_user_affinity[irq] = 1;
		irq_desc[irq].handler->set_affinity(irq, new_value);
	}

	return full_count;
}

#endif /* CONFIG_SMP */

#define MAX_NAMELEN 10

static void
register_irq_proc (unsigned int irq)
{
	char name [MAX_NAMELEN];

	if (!root_irq_dir || (irq_desc[irq].handler == &no_irq_type) ||
	    irq_dir[irq])
		return;

	memset(name, 0, MAX_NAMELEN);
	sprintf(name, "%d", irq);

	/* create /proc/irq/1234 */
	irq_dir[irq] = proc_mkdir(name, root_irq_dir);

#ifdef CONFIG_SMP 
	if (irq_desc[irq].handler->set_affinity) {
		struct proc_dir_entry *entry;
		/* create /proc/irq/1234/smp_affinity */
		entry = create_proc_entry("smp_affinity", 0600, irq_dir[irq]);

		if (entry) {
			entry->nlink = 1;
			entry->data = (void *)(long)irq;
			entry->read_proc = irq_affinity_read_proc;
			entry->write_proc = irq_affinity_write_proc;
		}

		smp_affinity_entry[irq] = entry;
	}
#endif
}

void
init_irq_proc (void)
{
	int i;

	/* create /proc/irq */
	root_irq_dir = proc_mkdir("irq", NULL);

#ifdef CONFIG_SMP 
	/* create /proc/irq/prof_cpu_mask */
	create_prof_cpu_mask(root_irq_dir);
#endif

	/*
	 * Create entries for all existing IRQs.
	 */
	for (i = 0; i < ACTUAL_NR_IRQS; i++) {
		if (irq_desc[i].handler == &no_irq_type)
			continue;
		register_irq_proc(i);
	}
}

int
request_irq(unsigned int irq, irqreturn_t (*handler)(int, void *, struct pt_regs *),
	    unsigned long irqflags, const char * devname, void *dev_id)
{
	int retval;
	struct irqaction * action;

	if (irq >= ACTUAL_NR_IRQS)
		return -EINVAL;
	if (!handler)
		return -EINVAL;

#if 1
	/*
	 * Sanity-check: shared interrupts should REALLY pass in
	 * a real dev-ID, otherwise we'll have trouble later trying
	 * to figure out which interrupt is which (messes up the
	 * interrupt freeing logic etc).
	 */
	if ((irqflags & SA_SHIRQ) && !dev_id) {
		printk(KERN_ERR
		       "Bad boy: %s (at %p) called us without a dev_id!\n",
		       devname, __builtin_return_address(0));
	}
#endif

	action = (struct irqaction *)
			kmalloc(sizeof(struct irqaction), GFP_KERNEL);
	if (!action)
		return -ENOMEM;

	action->handler = handler;
	action->flags = irqflags;
	cpus_clear(action->mask);
	action->name = devname;
	action->next = NULL;
	action->dev_id = dev_id;

#ifdef CONFIG_SMP
	select_smp_affinity(irq);
#endif

	retval = setup_irq(irq, action);
	if (retval)
		kfree(action);
	return retval;
}

EXPORT_SYMBOL(request_irq);

void
free_irq(unsigned int irq, void *dev_id)
{
	irq_desc_t *desc;
	struct irqaction **p;
	unsigned long flags;

	if (irq >= ACTUAL_NR_IRQS) {
		printk(KERN_CRIT "Trying to free IRQ%d\n", irq);
		return;
	}

	desc = irq_desc + irq;
	spin_lock_irqsave(&desc->lock,flags);
	p = &desc->action;
	for (;;) {
		struct irqaction * action = *p;
		if (action) {
			struct irqaction **pp = p;
			p = &action->next;
			if (action->dev_id != dev_id)
				continue;

			/* Found - now remove it from the list of entries.  */
			*pp = action->next;
			if (!desc->action) {
				desc->status |= IRQ_DISABLED;
				desc->handler->shutdown(irq);
			}
			spin_unlock_irqrestore(&desc->lock,flags);

#ifdef CONFIG_SMP
			/* Wait to make sure it's not being used on
			   another CPU.  */
			while (desc->status & IRQ_INPROGRESS)
				barrier();
#endif
			kfree(action);
			return;
		}
		printk(KERN_ERR "Trying to free free IRQ%d\n",irq);
		spin_unlock_irqrestore(&desc->lock,flags);
		return;
	}
}

EXPORT_SYMBOL(free_irq);

int
show_interrupts(struct seq_file *p, void *v)
{
#ifdef CONFIG_SMP
	int j;
#endif
	int i = *(loff_t *) v;
	struct irqaction * action;
	unsigned long flags;

#ifdef CONFIG_SMP
	if (i == 0) {
		seq_puts(p, "           ");
		for (i = 0; i < NR_CPUS; i++)
			if (cpu_online(i))
				seq_printf(p, "CPU%d       ", i);
		seq_putc(p, '\n');
	}
#endif

	if (i < ACTUAL_NR_IRQS) {
		spin_lock_irqsave(&irq_desc[i].lock, flags);
		action = irq_desc[i].action;
		if (!action) 
			goto unlock;
		seq_printf(p, "%3d: ",i);
#ifndef CONFIG_SMP
		seq_printf(p, "%10u ", kstat_irqs(i));
#else
		for (j = 0; j < NR_CPUS; j++)
			if (cpu_online(j))
				seq_printf(p, "%10u ", kstat_cpu(j).irqs[i]);
#endif
		seq_printf(p, " %14s", irq_desc[i].handler->typename);
		seq_printf(p, "  %c%s",
			(action->flags & SA_INTERRUPT)?'+':' ',
			action->name);

		for (action=action->next; action; action = action->next) {
			seq_printf(p, ", %c%s",
				  (action->flags & SA_INTERRUPT)?'+':' ',
				   action->name);
		}

		seq_putc(p, '\n');
unlock:
		spin_unlock_irqrestore(&irq_desc[i].lock, flags);
	} else if (i == ACTUAL_NR_IRQS) {
#ifdef CONFIG_SMP
		seq_puts(p, "IPI: ");
		for (i = 0; i < NR_CPUS; i++)
			if (cpu_online(i))
				seq_printf(p, "%10lu ", cpu_data[i].ipi_count);
		seq_putc(p, '\n');
#endif
		seq_printf(p, "ERR: %10lu\n", irq_err_count);
	}
	return 0;
}


/*
 * handle_irq handles all normal device IRQ's (the special
 * SMP cross-CPU interrupts have their own specific
 * handlers).
 */

#define MAX_ILLEGAL_IRQS 16

void
handle_irq(int irq, struct pt_regs * regs)
{	
	/* 
	 * We ack quickly, we don't want the irq controller
	 * thinking we're snobs just because some other CPU has
	 * disabled global interrupts (we have already done the
	 * INT_ACK cycles, it's too late to try to pretend to the
	 * controller that we aren't taking the interrupt).
	 *
	 * 0 return value means that this irq is already being
	 * handled by some other CPU. (or is disabled)
	 */
	int cpu = smp_processor_id();
	irq_desc_t *desc = irq_desc + irq;
	struct irqaction * action;
	unsigned int status;
	static unsigned int illegal_count=0;
	
	if ((unsigned) irq > ACTUAL_NR_IRQS && illegal_count < MAX_ILLEGAL_IRQS ) {
		irq_err_count++;
		illegal_count++;
		printk(KERN_CRIT "device_interrupt: invalid interrupt %d\n",
		       irq);
		return;
	}

	irq_enter();
	kstat_cpu(cpu).irqs[irq]++;
	spin_lock_irq(&desc->lock); /* mask also the higher prio events */
	desc->handler->ack(irq);
	/*
	 * REPLAY is when Linux resends an IRQ that was dropped earlier.
	 * WAITING is used by probe to mark irqs that are being tested.
	 */
	status = desc->status & ~(IRQ_REPLAY | IRQ_WAITING);
	status |= IRQ_PENDING; /* we _want_ to handle it */

	/*
	 * If the IRQ is disabled for whatever reason, we cannot
	 * use the action we have.
	 */
	action = NULL;
	if (!(status & (IRQ_DISABLED | IRQ_INPROGRESS))) {
		action = desc->action;
		status &= ~IRQ_PENDING; /* we commit to handling */
		status |= IRQ_INPROGRESS; /* we are handling it */
	}
	desc->status = status;

	/*
	 * If there is no IRQ handler or it was disabled, exit early.
	 * Since we set PENDING, if another processor is handling
	 * a different instance of this same irq, the other processor
	 * will take care of it.
	 */
	if (!action)
		goto out;

	/*
	 * Edge triggered interrupts need to remember pending events.
	 * This applies to any hw interrupts that allow a second
	 * instance of the same irq to arrive while we are in handle_irq
	 * or in the handler. But the code here only handles the _second_
	 * instance of the irq, not the third or fourth. So it is mostly
	 * useful for irq hardware that does not mask cleanly in an
	 * SMP environment.
	 */
	for (;;) {
		spin_unlock(&desc->lock);
		handle_IRQ_event(irq, regs, action);
		spin_lock(&desc->lock);
		
		if (!(desc->status & IRQ_PENDING)
		    || (desc->status & IRQ_LEVEL))
			break;
		desc->status &= ~IRQ_PENDING;
	}
	desc->status &= ~IRQ_INPROGRESS;
out:
	/*
	 * The ->end() handler has to deal with interrupts which got
	 * disabled while the handler was running.
	 */
	desc->handler->end(irq);
	spin_unlock(&desc->lock);

	irq_exit();
}

/*
 * IRQ autodetection code..
 *
 * This depends on the fact that any interrupt that
 * comes in on to an unassigned handler will get stuck
 * with "IRQ_WAITING" cleared and the interrupt
 * disabled.
 */
unsigned long
probe_irq_on(void)
{
	int i;
	irq_desc_t *desc;
	unsigned long delay;
	unsigned long val;

	/* Something may have generated an irq long ago and we want to
	   flush such a longstanding irq before considering it as spurious. */
	for (i = NR_IRQS-1; i >= 0; i--) {
		desc = irq_desc + i;

		spin_lock_irq(&desc->lock);
		if (!irq_desc[i].action) 
			irq_desc[i].handler->startup(i);
		spin_unlock_irq(&desc->lock);
	}

	/* Wait for longstanding interrupts to trigger. */
	for (delay = jiffies + HZ/50; time_after(delay, jiffies); )
		/* about 20ms delay */ barrier();

	/* enable any unassigned irqs (we must startup again here because
	   if a longstanding irq happened in the previous stage, it may have
	   masked itself) first, enable any unassigned irqs. */
	for (i = NR_IRQS-1; i >= 0; i--) {
		desc = irq_desc + i;

		spin_lock_irq(&desc->lock);
		if (!desc->action) {
			desc->status |= IRQ_AUTODETECT | IRQ_WAITING;
			if (desc->handler->startup(i))
				desc->status |= IRQ_PENDING;
		}
		spin_unlock_irq(&desc->lock);
	}

	/*
	 * Wait for spurious interrupts to trigger
	 */
	for (delay = jiffies + HZ/10; time_after(delay, jiffies); )
		/* about 100ms delay */ barrier();

	/*
	 * Now filter out any obviously spurious interrupts
	 */
	val = 0;
	for (i=0; i<NR_IRQS; i++) {
		irq_desc_t *desc = irq_desc + i;
		unsigned int status;

		spin_lock_irq(&desc->lock);
		status = desc->status;

		if (status & IRQ_AUTODETECT) {
			/* It triggered already - consider it spurious. */
			if (!(status & IRQ_WAITING)) {
				desc->status = status & ~IRQ_AUTODETECT;
				desc->handler->shutdown(i);
			} else
				if (i < 32)
					val |= 1 << i;
		}
		spin_unlock_irq(&desc->lock);
	}

	return val;
}

EXPORT_SYMBOL(probe_irq_on);

/*
 * Return a mask of triggered interrupts (this
 * can handle only legacy ISA interrupts).
 */
unsigned int
probe_irq_mask(unsigned long val)
{
	int i;
	unsigned int mask;

	mask = 0;
	for (i = 0; i < NR_IRQS; i++) {
		irq_desc_t *desc = irq_desc + i;
		unsigned int status;

		spin_lock_irq(&desc->lock);
		status = desc->status;

		if (status & IRQ_AUTODETECT) {
			/* We only react to ISA interrupts */
			if (!(status & IRQ_WAITING)) {
				if (i < 16)
					mask |= 1 << i;
			}

			desc->status = status & ~IRQ_AUTODETECT;
			desc->handler->shutdown(i);
		}
		spin_unlock_irq(&desc->lock);
	}

	return mask & val;
}

/*
 * Get the result of the IRQ probe.. A negative result means that
 * we have several candidates (but we return the lowest-numbered
 * one).
 */

int
probe_irq_off(unsigned long val)
{
	int i, irq_found, nr_irqs;

	nr_irqs = 0;
	irq_found = 0;
	for (i=0; i<NR_IRQS; i++) {
		irq_desc_t *desc = irq_desc + i;
		unsigned int status;

		spin_lock_irq(&desc->lock);
		status = desc->status;

		if (status & IRQ_AUTODETECT) {
			if (!(status & IRQ_WAITING)) {
				if (!nr_irqs)
					irq_found = i;
				nr_irqs++;
			}
			desc->status = status & ~IRQ_AUTODETECT;
			desc->handler->shutdown(i);
		}
		spin_unlock_irq(&desc->lock);
	}

	if (nr_irqs > 1)
		irq_found = -irq_found;
	return irq_found;
}

EXPORT_SYMBOL(probe_irq_off);

#ifdef CONFIG_SMP
void synchronize_irq(unsigned int irq)
{
        /* is there anything to synchronize with? */
	if (!irq_desc[irq].action)
		return;

	while (irq_desc[irq].status & IRQ_INPROGRESS)
		barrier();
}
#endif
