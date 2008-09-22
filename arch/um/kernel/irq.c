/*
 * Copyright (C) 2000 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 * Licensed under the GPL
 * Derived (i.e. mostly copied) from arch/i386/kernel/irq.c:
 *	Copyright (C) 1992, 1998 Linus Torvalds, Ingo Molnar
 */

#include "linux/cpumask.h"
#include "linux/hardirq.h"
#include "linux/interrupt.h"
#include "linux/kernel_stat.h"
#include "linux/module.h"
#include "linux/seq_file.h"
#include "as-layout.h"
#include "kern_util.h"
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
		seq_printf(p, " %14s", irq_desc[i].chip->typename);
		seq_printf(p, "  %s", action->name);

		for (action=action->next; action; action = action->next)
			seq_printf(p, ", %s", action->name);

		seq_putc(p, '\n');
skip:
		spin_unlock_irqrestore(&irq_desc[i].lock, flags);
	} else if (i == NR_IRQS)
		seq_putc(p, '\n');

	return 0;
}

/*
 * This list is accessed under irq_lock, except in sigio_handler,
 * where it is safe from being modified.  IRQ handlers won't change it -
 * if an IRQ source has vanished, it will be freed by free_irqs just
 * before returning from sigio_handler.  That will process a separate
 * list of irqs to free, with its own locking, coming back here to
 * remove list elements, taking the irq_lock to do so.
 */
static struct irq_fd *active_fds = NULL;
static struct irq_fd **last_irq_ptr = &active_fds;

extern void free_irqs(void);

void sigio_handler(int sig, struct uml_pt_regs *regs)
{
	struct irq_fd *irq_fd;
	int n;

	if (smp_sigio_handler())
		return;

	while (1) {
		n = os_waiting_for_events(active_fds);
		if (n <= 0) {
			if (n == -EINTR)
				continue;
			else break;
		}

		for (irq_fd = active_fds; irq_fd != NULL;
		     irq_fd = irq_fd->next) {
			if (irq_fd->current_events != 0) {
				irq_fd->current_events = 0;
				do_IRQ(irq_fd->irq, regs);
			}
		}
	}

	free_irqs();
}

static DEFINE_SPINLOCK(irq_lock);

static int activate_fd(int irq, int fd, int type, void *dev_id)
{
	struct pollfd *tmp_pfd;
	struct irq_fd *new_fd, *irq_fd;
	unsigned long flags;
	int events, err, n;

	err = os_set_fd_async(fd);
	if (err < 0)
		goto out;

	err = -ENOMEM;
	new_fd = kmalloc(sizeof(struct irq_fd), GFP_KERNEL);
	if (new_fd == NULL)
		goto out;

	if (type == IRQ_READ)
		events = UM_POLLIN | UM_POLLPRI;
	else events = UM_POLLOUT;
	*new_fd = ((struct irq_fd) { .next  		= NULL,
				     .id 		= dev_id,
				     .fd 		= fd,
				     .type 		= type,
				     .irq 		= irq,
				     .events 		= events,
				     .current_events 	= 0 } );

	err = -EBUSY;
	spin_lock_irqsave(&irq_lock, flags);
	for (irq_fd = active_fds; irq_fd != NULL; irq_fd = irq_fd->next) {
		if ((irq_fd->fd == fd) && (irq_fd->type == type)) {
			printk(KERN_ERR "Registering fd %d twice\n", fd);
			printk(KERN_ERR "Irqs : %d, %d\n", irq_fd->irq, irq);
			printk(KERN_ERR "Ids : 0x%p, 0x%p\n", irq_fd->id,
			       dev_id);
			goto out_unlock;
		}
	}

	if (type == IRQ_WRITE)
		fd = -1;

	tmp_pfd = NULL;
	n = 0;

	while (1) {
		n = os_create_pollfd(fd, events, tmp_pfd, n);
		if (n == 0)
			break;

		/*
		 * n > 0
		 * It means we couldn't put new pollfd to current pollfds
		 * and tmp_fds is NULL or too small for new pollfds array.
		 * Needed size is equal to n as minimum.
		 *
		 * Here we have to drop the lock in order to call
		 * kmalloc, which might sleep.
		 * If something else came in and changed the pollfds array
		 * so we will not be able to put new pollfd struct to pollfds
		 * then we free the buffer tmp_fds and try again.
		 */
		spin_unlock_irqrestore(&irq_lock, flags);
		kfree(tmp_pfd);

		tmp_pfd = kmalloc(n, GFP_KERNEL);
		if (tmp_pfd == NULL)
			goto out_kfree;

		spin_lock_irqsave(&irq_lock, flags);
	}

	*last_irq_ptr = new_fd;
	last_irq_ptr = &new_fd->next;

	spin_unlock_irqrestore(&irq_lock, flags);

	/*
	 * This calls activate_fd, so it has to be outside the critical
	 * section.
	 */
	maybe_sigio_broken(fd, (type == IRQ_READ));

	return 0;

 out_unlock:
	spin_unlock_irqrestore(&irq_lock, flags);
 out_kfree:
	kfree(new_fd);
 out:
	return err;
}

static void free_irq_by_cb(int (*test)(struct irq_fd *, void *), void *arg)
{
	unsigned long flags;

	spin_lock_irqsave(&irq_lock, flags);
	os_free_irq_by_cb(test, arg, active_fds, &last_irq_ptr);
	spin_unlock_irqrestore(&irq_lock, flags);
}

struct irq_and_dev {
	int irq;
	void *dev;
};

static int same_irq_and_dev(struct irq_fd *irq, void *d)
{
	struct irq_and_dev *data = d;

	return ((irq->irq == data->irq) && (irq->id == data->dev));
}

static void free_irq_by_irq_and_dev(unsigned int irq, void *dev)
{
	struct irq_and_dev data = ((struct irq_and_dev) { .irq  = irq,
							  .dev  = dev });

	free_irq_by_cb(same_irq_and_dev, &data);
}

static int same_fd(struct irq_fd *irq, void *fd)
{
	return (irq->fd == *((int *)fd));
}

void free_irq_by_fd(int fd)
{
	free_irq_by_cb(same_fd, &fd);
}

/* Must be called with irq_lock held */
static struct irq_fd *find_irq_by_fd(int fd, int irqnum, int *index_out)
{
	struct irq_fd *irq;
	int i = 0;
	int fdi;

	for (irq = active_fds; irq != NULL; irq = irq->next) {
		if ((irq->fd == fd) && (irq->irq == irqnum))
			break;
		i++;
	}
	if (irq == NULL) {
		printk(KERN_ERR "find_irq_by_fd doesn't have descriptor %d\n",
		       fd);
		goto out;
	}
	fdi = os_get_pollfd(i);
	if ((fdi != -1) && (fdi != fd)) {
		printk(KERN_ERR "find_irq_by_fd - mismatch between active_fds "
		       "and pollfds, fd %d vs %d, need %d\n", irq->fd,
		       fdi, fd);
		irq = NULL;
		goto out;
	}
	*index_out = i;
 out:
	return irq;
}

void reactivate_fd(int fd, int irqnum)
{
	struct irq_fd *irq;
	unsigned long flags;
	int i;

	spin_lock_irqsave(&irq_lock, flags);
	irq = find_irq_by_fd(fd, irqnum, &i);
	if (irq == NULL) {
		spin_unlock_irqrestore(&irq_lock, flags);
		return;
	}
	os_set_pollfd(i, irq->fd);
	spin_unlock_irqrestore(&irq_lock, flags);

	add_sigio_fd(fd);
}

void deactivate_fd(int fd, int irqnum)
{
	struct irq_fd *irq;
	unsigned long flags;
	int i;

	spin_lock_irqsave(&irq_lock, flags);
	irq = find_irq_by_fd(fd, irqnum, &i);
	if (irq == NULL) {
		spin_unlock_irqrestore(&irq_lock, flags);
		return;
	}

	os_set_pollfd(i, -1);
	spin_unlock_irqrestore(&irq_lock, flags);

	ignore_sigio_fd(fd);
}

/*
 * Called just before shutdown in order to provide a clean exec
 * environment in case the system is rebooting.  No locking because
 * that would cause a pointless shutdown hang if something hadn't
 * released the lock.
 */
int deactivate_all_fds(void)
{
	struct irq_fd *irq;
	int err;

	for (irq = active_fds; irq != NULL; irq = irq->next) {
		err = os_clear_fd_async(irq->fd);
		if (err)
			return err;
	}
	/* If there is a signal already queued, after unblocking ignore it */
	os_set_ioignore();

	return 0;
}

/*
 * do_IRQ handles all normal device IRQs (the special
 * SMP cross-CPU interrupts have their own specific
 * handlers).
 */
unsigned int do_IRQ(int irq, struct uml_pt_regs *regs)
{
	struct pt_regs *old_regs = set_irq_regs((struct pt_regs *)regs);
	irq_enter();
	__do_IRQ(irq);
	irq_exit();
	set_irq_regs(old_regs);
	return 1;
}

int um_request_irq(unsigned int irq, int fd, int type,
		   irq_handler_t handler,
		   unsigned long irqflags, const char * devname,
		   void *dev_id)
{
	int err;

	if (fd != -1) {
		err = activate_fd(irq, fd, type, dev_id);
		if (err)
			return err;
	}

	return request_irq(irq, handler, irqflags, devname, dev_id);
}

EXPORT_SYMBOL(um_request_irq);
EXPORT_SYMBOL(reactivate_fd);

/*
 * hw_interrupt_type must define (startup || enable) &&
 * (shutdown || disable) && end
 */
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
	irq_desc[TIMER_IRQ].chip = &SIGVTALRM_irq_type;
	enable_irq(TIMER_IRQ);
	for (i = 1; i < NR_IRQS; i++) {
		irq_desc[i].status = IRQ_DISABLED;
		irq_desc[i].action = NULL;
		irq_desc[i].depth = 1;
		irq_desc[i].chip = &normal_irq_type;
		enable_irq(i);
	}
}

/*
 * IRQ stack entry and exit:
 *
 * Unlike i386, UML doesn't receive IRQs on the normal kernel stack
 * and switch over to the IRQ stack after some preparation.  We use
 * sigaltstack to receive signals on a separate stack from the start.
 * These two functions make sure the rest of the kernel won't be too
 * upset by being on a different stack.  The IRQ stack has a
 * thread_info structure at the bottom so that current et al continue
 * to work.
 *
 * to_irq_stack copies the current task's thread_info to the IRQ stack
 * thread_info and sets the tasks's stack to point to the IRQ stack.
 *
 * from_irq_stack copies the thread_info struct back (flags may have
 * been modified) and resets the task's stack pointer.
 *
 * Tricky bits -
 *
 * What happens when two signals race each other?  UML doesn't block
 * signals with sigprocmask, SA_DEFER, or sa_mask, so a second signal
 * could arrive while a previous one is still setting up the
 * thread_info.
 *
 * There are three cases -
 *     The first interrupt on the stack - sets up the thread_info and
 * handles the interrupt
 *     A nested interrupt interrupting the copying of the thread_info -
 * can't handle the interrupt, as the stack is in an unknown state
 *     A nested interrupt not interrupting the copying of the
 * thread_info - doesn't do any setup, just handles the interrupt
 *
 * The first job is to figure out whether we interrupted stack setup.
 * This is done by xchging the signal mask with thread_info->pending.
 * If the value that comes back is zero, then there is no setup in
 * progress, and the interrupt can be handled.  If the value is
 * non-zero, then there is stack setup in progress.  In order to have
 * the interrupt handled, we leave our signal in the mask, and it will
 * be handled by the upper handler after it has set up the stack.
 *
 * Next is to figure out whether we are the outer handler or a nested
 * one.  As part of setting up the stack, thread_info->real_thread is
 * set to non-NULL (and is reset to NULL on exit).  This is the
 * nesting indicator.  If it is non-NULL, then the stack is already
 * set up and the handler can run.
 */

static unsigned long pending_mask;

unsigned long to_irq_stack(unsigned long *mask_out)
{
	struct thread_info *ti;
	unsigned long mask, old;
	int nested;

	mask = xchg(&pending_mask, *mask_out);
	if (mask != 0) {
		/*
		 * If any interrupts come in at this point, we want to
		 * make sure that their bits aren't lost by our
		 * putting our bit in.  So, this loop accumulates bits
		 * until xchg returns the same value that we put in.
		 * When that happens, there were no new interrupts,
		 * and pending_mask contains a bit for each interrupt
		 * that came in.
		 */
		old = *mask_out;
		do {
			old |= mask;
			mask = xchg(&pending_mask, old);
		} while (mask != old);
		return 1;
	}

	ti = current_thread_info();
	nested = (ti->real_thread != NULL);
	if (!nested) {
		struct task_struct *task;
		struct thread_info *tti;

		task = cpu_tasks[ti->cpu].task;
		tti = task_thread_info(task);

		*ti = *tti;
		ti->real_thread = tti;
		task->stack = ti;
	}

	mask = xchg(&pending_mask, 0);
	*mask_out |= mask | nested;
	return 0;
}

unsigned long from_irq_stack(int nested)
{
	struct thread_info *ti, *to;
	unsigned long mask;

	ti = current_thread_info();

	pending_mask = 1;

	to = ti->real_thread;
	current->stack = to;
	ti->real_thread = NULL;
	*to = *ti;

	mask = xchg(&pending_mask, 0);
	return mask & ~1;
}

