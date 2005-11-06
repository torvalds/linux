/* 
 * Copyright (C) 2000 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 * Derived (i.e. mostly copied) from arch/i386/kernel/irq.c:
 *	Copyright (C) 1992, 1998 Linus Torvalds, Ingo Molnar
 */

#include "linux/config.h"
#include "linux/kernel.h"
#include "linux/module.h"
#include "linux/smp.h"
#include "linux/kernel_stat.h"
#include "linux/interrupt.h"
#include "linux/random.h"
#include "linux/slab.h"
#include "linux/file.h"
#include "linux/proc_fs.h"
#include "linux/init.h"
#include "linux/seq_file.h"
#include "linux/profile.h"
#include "linux/hardirq.h"
#include "asm/irq.h"
#include "asm/hw_irq.h"
#include "asm/atomic.h"
#include "asm/signal.h"
#include "asm/system.h"
#include "asm/errno.h"
#include "asm/uaccess.h"
#include "user_util.h"
#include "kern_util.h"
#include "irq_user.h"
#include "irq_kern.h"
#include "os.h"

/*
 * Generic, controller-independent functions:
 */

int show_interrupts(struct seq_file *p, void *v)
{
	int i = *(loff_t *) v, j;
	struct irqaction * action;
	unsigned long flags;

	if (i == 0) {
		seq_printf(p, "           ");
		for_each_online_cpu(j)
			seq_printf(p, "CPU%d       ",j);
		seq_putc(p, '\n');
	}

	if (i < NR_IRQS) {
		spin_lock_irqsave(&irq_desc[i].lock, flags);
		action = irq_desc[i].action;
		if (!action) 
			goto skip;
		seq_printf(p, "%3d: ",i);
#ifndef CONFIG_SMP
		seq_printf(p, "%10u ", kstat_irqs(i));
#else
		for_each_online_cpu(j)
			seq_printf(p, "%10u ", kstat_cpu(j).irqs[i]);
#endif
		seq_printf(p, " %14s", irq_desc[i].handler->typename);
		seq_printf(p, "  %s", action->name);

		for (action=action->next; action; action = action->next)
			seq_printf(p, ", %s", action->name);

		seq_putc(p, '\n');
skip:
		spin_unlock_irqrestore(&irq_desc[i].lock, flags);
	} else if (i == NR_IRQS) {
		seq_putc(p, '\n');
	}

	return 0;
}

/*
 * do_IRQ handles all normal device IRQ's (the special
 * SMP cross-CPU interrupts have their own specific
 * handlers).
 */
unsigned int do_IRQ(int irq, union uml_pt_regs *regs)
{
       irq_enter();
       __do_IRQ(irq, (struct pt_regs *) regs);
       irq_exit();
       return 1;
}

int um_request_irq(unsigned int irq, int fd, int type,
		   irqreturn_t (*handler)(int, void *, struct pt_regs *),
		   unsigned long irqflags, const char * devname,
		   void *dev_id)
{
	int err;

	err = request_irq(irq, handler, irqflags, devname, dev_id);
	if(err)
		return(err);

	if(fd != -1)
		err = activate_fd(irq, fd, type, dev_id);
	return(err);
}
EXPORT_SYMBOL(um_request_irq);
EXPORT_SYMBOL(reactivate_fd);

static DEFINE_SPINLOCK(irq_spinlock);

unsigned long irq_lock(void)
{
	unsigned long flags;

	spin_lock_irqsave(&irq_spinlock, flags);
	return(flags);
}

void irq_unlock(unsigned long flags)
{
	spin_unlock_irqrestore(&irq_spinlock, flags);
}

/* hw_interrupt_type must define (startup || enable) &&
 * (shutdown || disable) && end */
static void dummy(unsigned int irq)
{
}

/* This is used for everything else than the timer. */
static struct hw_interrupt_type normal_irq_type = {
	.typename = "SIGIO",
	.release = free_irq_by_irq_and_dev,
	.disable = dummy,
	.enable = dummy,
	.ack = dummy,
	.end = dummy
};

static struct hw_interrupt_type SIGVTALRM_irq_type = {
	.typename = "SIGVTALRM",
	.release = free_irq_by_irq_and_dev,
	.shutdown = dummy, /* never called */
	.disable = dummy,
	.enable = dummy,
	.ack = dummy,
	.end = dummy
};

void __init init_IRQ(void)
{
	int i;

	irq_desc[TIMER_IRQ].status = IRQ_DISABLED;
	irq_desc[TIMER_IRQ].action = NULL;
	irq_desc[TIMER_IRQ].depth = 1;
	irq_desc[TIMER_IRQ].handler = &SIGVTALRM_irq_type;
	enable_irq(TIMER_IRQ);
	for(i=1;i<NR_IRQS;i++){
		irq_desc[i].status = IRQ_DISABLED;
		irq_desc[i].action = NULL;
		irq_desc[i].depth = 1;
		irq_desc[i].handler = &normal_irq_type;
		enable_irq(i);
	}
}

int init_aio_irq(int irq, char *name, irqreturn_t (*handler)(int, void *,
							     struct pt_regs *))
{
	int fds[2], err;

	err = os_pipe(fds, 1, 1);
	if(err){
		printk("init_aio_irq - os_pipe failed, err = %d\n", -err);
		goto out;
	}

	err = um_request_irq(irq, fds[0], IRQ_READ, handler,
			     SA_INTERRUPT | SA_SAMPLE_RANDOM, name,
			     (void *) (long) fds[0]);
	if(err){
		printk("init_aio_irq - : um_request_irq failed, err = %d\n",
		       err);
		goto out_close;
	}

	err = fds[1];
	goto out;

 out_close:
	os_close_file(fds[0]);
	os_close_file(fds[1]);
 out:
	return(err);
}
