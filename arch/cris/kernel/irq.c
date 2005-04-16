/*
 *
 *	linux/arch/cris/kernel/irq.c
 *
 *      Copyright (c) 2000,2001 Axis Communications AB
 *
 *      Authors: Bjorn Wesen (bjornw@axis.com)
 *
 * This file contains the code used by various IRQ handling routines:
 * asking for different IRQ's should be done through these routines
 * instead of just grabbing them. Thus setups with different IRQ numbers
 * shouldn't result in any weird surprises, and installing new handlers
 * should be easier.
 *
 * Notice Linux/CRIS: these routines do not care about SMP
 *
 */

/*
 * IRQ's are in fact implemented a bit like signal handlers for the kernel.
 * Naturally it's not a 1:1 relation, but there are similarities.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/ptrace.h>

#include <linux/kernel_stat.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/timex.h>
#include <linux/slab.h>
#include <linux/random.h>
#include <linux/init.h>
#include <linux/seq_file.h>
#include <linux/errno.h>
#include <linux/bitops.h>

#include <asm/io.h>

/* Defined in arch specific irq.c */
extern void arch_setup_irq(int irq);
extern void arch_free_irq(int irq);

void
disable_irq(unsigned int irq_nr)
{
	unsigned long flags;
	
	local_save_flags(flags);
	local_irq_disable();
	mask_irq(irq_nr);
	local_irq_restore(flags);
}

void
enable_irq(unsigned int irq_nr)
{
	unsigned long flags;
	local_save_flags(flags);
	local_irq_disable();
	unmask_irq(irq_nr);
	local_irq_restore(flags);
}

unsigned long
probe_irq_on()
{
	return 0;
}

EXPORT_SYMBOL(probe_irq_on);

int
probe_irq_off(unsigned long x)
{
	return 0;
}

EXPORT_SYMBOL(probe_irq_off);

/*
 * Initial irq handlers.
 */

static struct irqaction *irq_action[NR_IRQS];

int show_interrupts(struct seq_file *p, void *v)
{
	int i = *(loff_t *) v;
	struct irqaction * action;
	unsigned long flags;

	if (i < NR_IRQS) {
		local_irq_save(flags);
		action = irq_action[i];
		if (!action) 
			goto skip;
		seq_printf(p, "%2d: %10u %c %s",
			i, kstat_this_cpu.irqs[i],
			(action->flags & SA_INTERRUPT) ? '+' : ' ',
			action->name);
		for (action = action->next; action; action = action->next) {
			seq_printf(p, ",%s %s",
				(action->flags & SA_INTERRUPT) ? " +" : "",
				action->name);
		}
		seq_putc(p, '\n');
skip:
		local_irq_restore(flags);
	}
	return 0;
}

/* called by the assembler IRQ entry functions defined in irq.h
 * to dispatch the interrupts to registred handlers
 * interrupts are disabled upon entry - depending on if the
 * interrupt was registred with SA_INTERRUPT or not, interrupts
 * are re-enabled or not.
 */

asmlinkage void do_IRQ(int irq, struct pt_regs * regs)
{
	struct irqaction *action;
	int do_random, cpu;
        int ret, retval = 0;

        cpu = smp_processor_id();
        irq_enter();
	kstat_cpu(cpu).irqs[irq - FIRST_IRQ]++;
	action = irq_action[irq - FIRST_IRQ];

        if (action) {
                if (!(action->flags & SA_INTERRUPT))
                        local_irq_enable();
                do_random = 0;
                do {
			ret = action->handler(irq, action->dev_id, regs);
			if (ret == IRQ_HANDLED)
				do_random |= action->flags;
                        retval |= ret;
                        action = action->next;
                } while (action);

                if (retval != 1) {
			if (retval) {
				printk("irq event %d: bogus retval mask %x\n",
					irq, retval);
			} else {
				printk("irq %d: nobody cared\n", irq);
			}
		}

                if (do_random & SA_SAMPLE_RANDOM)
                        add_interrupt_randomness(irq);
		local_irq_disable();
        }
        irq_exit();
}

/* this function links in a handler into the chain of handlers for the
   given irq, and if the irq has never been registred, the appropriate
   handler is entered into the interrupt vector
*/

int setup_irq(int irq, struct irqaction * new)
{
	int shared = 0;
	struct irqaction *old, **p;
	unsigned long flags;

	p = irq_action + irq - FIRST_IRQ;
	if ((old = *p) != NULL) {
		/* Can't share interrupts unless both agree to */
		if (!(old->flags & new->flags & SA_SHIRQ))
			return -EBUSY;

		/* Can't share interrupts unless both are same type */
		if ((old->flags ^ new->flags) & SA_INTERRUPT)
			return -EBUSY;

		/* add new interrupt at end of irq queue */
		do {
			p = &old->next;
			old = *p;
		} while (old);
		shared = 1;
	}

	if (new->flags & SA_SAMPLE_RANDOM)
		rand_initialize_irq(irq);

	local_save_flags(flags);
	local_irq_disable();
	*p = new;

	if (!shared) {
		/* if the irq wasn't registred before, enter it into the vector table
		   and unmask it physically 
		*/
		arch_setup_irq(irq);
		unmask_irq(irq);
	}
	
	local_irq_restore(flags);
	return 0;
}

/* this function is called by a driver to register an irq handler
   Valid flags:
   SA_INTERRUPT -> it's a fast interrupt, handler called with irq disabled and
                   no signal checking etc is performed upon exit
   SA_SHIRQ -> the interrupt can be shared between different handlers, the handler
                is required to check if the irq was "aimed" at it explicitely
   SA_RANDOM -> the interrupt will add to the random generators entropy
*/

int request_irq(unsigned int irq, 
		irqreturn_t (*handler)(int, void *, struct pt_regs *),
		unsigned long irqflags, 
		const char * devname,
		void *dev_id)
{
	int retval;
	struct irqaction * action;

	if(!handler)
		return -EINVAL;

	/* allocate and fill in a handler structure and setup the irq */

	action = (struct irqaction *)kmalloc(sizeof(struct irqaction), GFP_KERNEL);
	if (!action)
		return -ENOMEM;

	action->handler = handler;
	action->flags = irqflags;
	cpus_clear(action->mask);
	action->name = devname;
	action->next = NULL;
	action->dev_id = dev_id;

	retval = setup_irq(irq, action);

	if (retval)
		kfree(action);
	return retval;
}

EXPORT_SYMBOL(request_irq);
		
void free_irq(unsigned int irq, void *dev_id)
{
	struct irqaction * action, **p;
	unsigned long flags;

	if (irq >= NR_IRQS) {
		printk("Trying to free IRQ%d\n",irq);
		return;
	}
	for (p = irq - FIRST_IRQ + irq_action; (action = *p) != NULL; p = &action->next) {
		if (action->dev_id != dev_id)
			continue;

		/* Found it - now free it */
		local_save_flags(flags);
		local_irq_disable();
		*p = action->next;
		if (!irq_action[irq - FIRST_IRQ]) {
			mask_irq(irq);
			arch_free_irq(irq);
		}
		local_irq_restore(flags);
		kfree(action);
		return;
	}
	printk("Trying to free free IRQ%d\n",irq);
}

EXPORT_SYMBOL(free_irq);

void weird_irq(void)
{
	local_irq_disable();
	printk("weird irq\n");
	while(1);
}

#if defined(CONFIG_PROC_FS) && defined(CONFIG_SYSCTL)
/* Used by other archs to show/control IRQ steering during SMP */
void __init
init_irq_proc(void)
{
}
#endif
