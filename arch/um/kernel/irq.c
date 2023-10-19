// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2017 - Cambridge Greys Ltd
 * Copyright (C) 2011 - 2014 Cisco Systems Inc
 * Copyright (C) 2000 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 * Derived (i.e. mostly copied) from arch/i386/kernel/irq.c:
 *	Copyright (C) 1992, 1998 Linus Torvalds, Ingo Molnar
 */

#include <linux/cpumask.h>
#include <linux/hardirq.h>
#include <linux/interrupt.h>
#include <linux/kernel_stat.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <as-layout.h>
#include <kern_util.h>
#include <os.h>
#include <irq_user.h>
#include <irq_kern.h>
#include <linux/time-internal.h>


extern void free_irqs(void);

/* When epoll triggers we do not know why it did so
 * we can also have different IRQs for read and write.
 * This is why we keep a small irq_reg array for each fd -
 * one entry per IRQ type
 */
struct irq_reg {
	void *id;
	int irq;
	/* it's cheaper to store this than to query it */
	int events;
	bool active;
	bool pending;
	bool wakeup;
#ifdef CONFIG_UML_TIME_TRAVEL_SUPPORT
	bool pending_on_resume;
	void (*timetravel_handler)(int, int, void *,
				   struct time_travel_event *);
	struct time_travel_event event;
#endif
};

struct irq_entry {
	struct list_head list;
	int fd;
	struct irq_reg reg[NUM_IRQ_TYPES];
	bool suspended;
	bool sigio_workaround;
};

static DEFINE_SPINLOCK(irq_lock);
static LIST_HEAD(active_fds);
static DECLARE_BITMAP(irqs_allocated, UM_LAST_SIGNAL_IRQ);
static bool irqs_suspended;

static void irq_io_loop(struct irq_reg *irq, struct uml_pt_regs *regs)
{
/*
 * irq->active guards against reentry
 * irq->pending accumulates pending requests
 * if pending is raised the irq_handler is re-run
 * until pending is cleared
 */
	if (irq->active) {
		irq->active = false;

		do {
			irq->pending = false;
			do_IRQ(irq->irq, regs);
		} while (irq->pending);

		irq->active = true;
	} else {
		irq->pending = true;
	}
}

#ifdef CONFIG_UML_TIME_TRAVEL_SUPPORT
static void irq_event_handler(struct time_travel_event *ev)
{
	struct irq_reg *reg = container_of(ev, struct irq_reg, event);

	/* do nothing if suspended - just to cause a wakeup */
	if (irqs_suspended)
		return;

	generic_handle_irq(reg->irq);
}

static bool irq_do_timetravel_handler(struct irq_entry *entry,
				      enum um_irq_type t)
{
	struct irq_reg *reg = &entry->reg[t];

	if (!reg->timetravel_handler)
		return false;

	/*
	 * Handle all messages - we might get multiple even while
	 * interrupts are already suspended, due to suspend order
	 * etc. Note that time_travel_add_irq_event() will not add
	 * an event twice, if it's pending already "first wins".
	 */
	reg->timetravel_handler(reg->irq, entry->fd, reg->id, &reg->event);

	if (!reg->event.pending)
		return false;

	if (irqs_suspended)
		reg->pending_on_resume = true;
	return true;
}
#else
static bool irq_do_timetravel_handler(struct irq_entry *entry,
				      enum um_irq_type t)
{
	return false;
}
#endif

static void sigio_reg_handler(int idx, struct irq_entry *entry, enum um_irq_type t,
			      struct uml_pt_regs *regs,
			      bool timetravel_handlers_only)
{
	struct irq_reg *reg = &entry->reg[t];

	if (!reg->events)
		return;

	if (os_epoll_triggered(idx, reg->events) <= 0)
		return;

	if (irq_do_timetravel_handler(entry, t))
		return;

	/*
	 * If we're called to only run time-travel handlers then don't
	 * actually proceed but mark sigio as pending (if applicable).
	 * For suspend/resume, timetravel_handlers_only may be true
	 * despite time-travel not being configured and used.
	 */
	if (timetravel_handlers_only) {
#ifdef CONFIG_UML_TIME_TRAVEL_SUPPORT
		mark_sigio_pending();
#endif
		return;
	}

	irq_io_loop(reg, regs);
}

static void _sigio_handler(struct uml_pt_regs *regs,
			   bool timetravel_handlers_only)
{
	struct irq_entry *irq_entry;
	int n, i;

	if (timetravel_handlers_only && !um_irq_timetravel_handler_used())
		return;

	while (1) {
		/* This is now lockless - epoll keeps back-referencesto the irqs
		 * which have trigger it so there is no need to walk the irq
		 * list and lock it every time. We avoid locking by turning off
		 * IO for a specific fd by executing os_del_epoll_fd(fd) before
		 * we do any changes to the actual data structures
		 */
		n = os_waiting_for_events_epoll();

		if (n <= 0) {
			if (n == -EINTR)
				continue;
			else
				break;
		}

		for (i = 0; i < n ; i++) {
			enum um_irq_type t;

			irq_entry = os_epoll_get_data_pointer(i);

			for (t = 0; t < NUM_IRQ_TYPES; t++)
				sigio_reg_handler(i, irq_entry, t, regs,
						  timetravel_handlers_only);
		}
	}

	if (!timetravel_handlers_only)
		free_irqs();
}

void sigio_handler(int sig, struct siginfo *unused_si, struct uml_pt_regs *regs)
{
	_sigio_handler(regs, irqs_suspended);
}

static struct irq_entry *get_irq_entry_by_fd(int fd)
{
	struct irq_entry *walk;

	lockdep_assert_held(&irq_lock);

	list_for_each_entry(walk, &active_fds, list) {
		if (walk->fd == fd)
			return walk;
	}

	return NULL;
}

static void free_irq_entry(struct irq_entry *to_free, bool remove)
{
	if (!to_free)
		return;

	if (remove)
		os_del_epoll_fd(to_free->fd);
	list_del(&to_free->list);
	kfree(to_free);
}

static bool update_irq_entry(struct irq_entry *entry)
{
	enum um_irq_type i;
	int events = 0;

	for (i = 0; i < NUM_IRQ_TYPES; i++)
		events |= entry->reg[i].events;

	if (events) {
		/* will modify (instead of add) if needed */
		os_add_epoll_fd(events, entry->fd, entry);
		return true;
	}

	os_del_epoll_fd(entry->fd);
	return false;
}

static void update_or_free_irq_entry(struct irq_entry *entry)
{
	if (!update_irq_entry(entry))
		free_irq_entry(entry, false);
}

static int activate_fd(int irq, int fd, enum um_irq_type type, void *dev_id,
		       void (*timetravel_handler)(int, int, void *,
						  struct time_travel_event *))
{
	struct irq_entry *irq_entry;
	int err, events = os_event_mask(type);
	unsigned long flags;

	err = os_set_fd_async(fd);
	if (err < 0)
		goto out;

	spin_lock_irqsave(&irq_lock, flags);
	irq_entry = get_irq_entry_by_fd(fd);
	if (irq_entry) {
		/* cannot register the same FD twice with the same type */
		if (WARN_ON(irq_entry->reg[type].events)) {
			err = -EALREADY;
			goto out_unlock;
		}

		/* temporarily disable to avoid IRQ-side locking */
		os_del_epoll_fd(fd);
	} else {
		irq_entry = kzalloc(sizeof(*irq_entry), GFP_ATOMIC);
		if (!irq_entry) {
			err = -ENOMEM;
			goto out_unlock;
		}
		irq_entry->fd = fd;
		list_add_tail(&irq_entry->list, &active_fds);
		maybe_sigio_broken(fd);
	}

	irq_entry->reg[type].id = dev_id;
	irq_entry->reg[type].irq = irq;
	irq_entry->reg[type].active = true;
	irq_entry->reg[type].events = events;

#ifdef CONFIG_UML_TIME_TRAVEL_SUPPORT
	if (um_irq_timetravel_handler_used()) {
		irq_entry->reg[type].timetravel_handler = timetravel_handler;
		irq_entry->reg[type].event.fn = irq_event_handler;
	}
#endif

	WARN_ON(!update_irq_entry(irq_entry));
	spin_unlock_irqrestore(&irq_lock, flags);

	return 0;
out_unlock:
	spin_unlock_irqrestore(&irq_lock, flags);
out:
	return err;
}

/*
 * Remove the entry or entries for a specific FD, if you
 * don't want to remove all the possible entries then use
 * um_free_irq() or deactivate_fd() instead.
 */
void free_irq_by_fd(int fd)
{
	struct irq_entry *to_free;
	unsigned long flags;

	spin_lock_irqsave(&irq_lock, flags);
	to_free = get_irq_entry_by_fd(fd);
	free_irq_entry(to_free, true);
	spin_unlock_irqrestore(&irq_lock, flags);
}
EXPORT_SYMBOL(free_irq_by_fd);

static void free_irq_by_irq_and_dev(unsigned int irq, void *dev)
{
	struct irq_entry *entry;
	unsigned long flags;

	spin_lock_irqsave(&irq_lock, flags);
	list_for_each_entry(entry, &active_fds, list) {
		enum um_irq_type i;

		for (i = 0; i < NUM_IRQ_TYPES; i++) {
			struct irq_reg *reg = &entry->reg[i];

			if (!reg->events)
				continue;
			if (reg->irq != irq)
				continue;
			if (reg->id != dev)
				continue;

			os_del_epoll_fd(entry->fd);
			reg->events = 0;
			update_or_free_irq_entry(entry);
			goto out;
		}
	}
out:
	spin_unlock_irqrestore(&irq_lock, flags);
}

void deactivate_fd(int fd, int irqnum)
{
	struct irq_entry *entry;
	unsigned long flags;
	enum um_irq_type i;

	os_del_epoll_fd(fd);

	spin_lock_irqsave(&irq_lock, flags);
	entry = get_irq_entry_by_fd(fd);
	if (!entry)
		goto out;

	for (i = 0; i < NUM_IRQ_TYPES; i++) {
		if (!entry->reg[i].events)
			continue;
		if (entry->reg[i].irq == irqnum)
			entry->reg[i].events = 0;
	}

	update_or_free_irq_entry(entry);
out:
	spin_unlock_irqrestore(&irq_lock, flags);

	ignore_sigio_fd(fd);
}
EXPORT_SYMBOL(deactivate_fd);

/*
 * Called just before shutdown in order to provide a clean exec
 * environment in case the system is rebooting.  No locking because
 * that would cause a pointless shutdown hang if something hadn't
 * released the lock.
 */
int deactivate_all_fds(void)
{
	struct irq_entry *entry;

	/* Stop IO. The IRQ loop has no lock so this is our
	 * only way of making sure we are safe to dispose
	 * of all IRQ handlers
	 */
	os_set_ioignore();

	/* we can no longer call kfree() here so just deactivate */
	list_for_each_entry(entry, &active_fds, list)
		os_del_epoll_fd(entry->fd);
	os_close_epoll_fd();
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
	generic_handle_irq(irq);
	irq_exit();
	set_irq_regs(old_regs);
	return 1;
}

void um_free_irq(int irq, void *dev)
{
	if (WARN(irq < 0 || irq > UM_LAST_SIGNAL_IRQ,
		 "freeing invalid irq %d", irq))
		return;

	free_irq_by_irq_and_dev(irq, dev);
	free_irq(irq, dev);
	clear_bit(irq, irqs_allocated);
}
EXPORT_SYMBOL(um_free_irq);

static int
_um_request_irq(int irq, int fd, enum um_irq_type type,
		irq_handler_t handler, unsigned long irqflags,
		const char *devname, void *dev_id,
		void (*timetravel_handler)(int, int, void *,
					   struct time_travel_event *))
{
	int err;

	if (irq == UM_IRQ_ALLOC) {
		int i;

		for (i = UM_FIRST_DYN_IRQ; i < NR_IRQS; i++) {
			if (!test_and_set_bit(i, irqs_allocated)) {
				irq = i;
				break;
			}
		}
	}

	if (irq < 0)
		return -ENOSPC;

	if (fd != -1) {
		err = activate_fd(irq, fd, type, dev_id, timetravel_handler);
		if (err)
			goto error;
	}

	err = request_irq(irq, handler, irqflags, devname, dev_id);
	if (err < 0)
		goto error;

	return irq;
error:
	clear_bit(irq, irqs_allocated);
	return err;
}

int um_request_irq(int irq, int fd, enum um_irq_type type,
		   irq_handler_t handler, unsigned long irqflags,
		   const char *devname, void *dev_id)
{
	return _um_request_irq(irq, fd, type, handler, irqflags,
			       devname, dev_id, NULL);
}
EXPORT_SYMBOL(um_request_irq);

#ifdef CONFIG_UML_TIME_TRAVEL_SUPPORT
int um_request_irq_tt(int irq, int fd, enum um_irq_type type,
		      irq_handler_t handler, unsigned long irqflags,
		      const char *devname, void *dev_id,
		      void (*timetravel_handler)(int, int, void *,
						 struct time_travel_event *))
{
	return _um_request_irq(irq, fd, type, handler, irqflags,
			       devname, dev_id, timetravel_handler);
}
EXPORT_SYMBOL(um_request_irq_tt);

void sigio_run_timetravel_handlers(void)
{
	_sigio_handler(NULL, true);
}
#endif

#ifdef CONFIG_PM_SLEEP
void um_irqs_suspend(void)
{
	struct irq_entry *entry;
	unsigned long flags;

	irqs_suspended = true;

	spin_lock_irqsave(&irq_lock, flags);
	list_for_each_entry(entry, &active_fds, list) {
		enum um_irq_type t;
		bool clear = true;

		for (t = 0; t < NUM_IRQ_TYPES; t++) {
			if (!entry->reg[t].events)
				continue;

			/*
			 * For the SIGIO_WRITE_IRQ, which is used to handle the
			 * SIGIO workaround thread, we need special handling:
			 * enable wake for it itself, but below we tell it about
			 * any FDs that should be suspended.
			 */
			if (entry->reg[t].wakeup ||
			    entry->reg[t].irq == SIGIO_WRITE_IRQ
#ifdef CONFIG_UML_TIME_TRAVEL_SUPPORT
			    || entry->reg[t].timetravel_handler
#endif
			    ) {
				clear = false;
				break;
			}
		}

		if (clear) {
			entry->suspended = true;
			os_clear_fd_async(entry->fd);
			entry->sigio_workaround =
				!__ignore_sigio_fd(entry->fd);
		}
	}
	spin_unlock_irqrestore(&irq_lock, flags);
}

void um_irqs_resume(void)
{
	struct irq_entry *entry;
	unsigned long flags;


	local_irq_save(flags);
#ifdef CONFIG_UML_TIME_TRAVEL_SUPPORT
	/*
	 * We don't need to lock anything here since we're in resume
	 * and nothing else is running, but have disabled IRQs so we
	 * don't try anything else with the interrupt list from there.
	 */
	list_for_each_entry(entry, &active_fds, list) {
		enum um_irq_type t;

		for (t = 0; t < NUM_IRQ_TYPES; t++) {
			struct irq_reg *reg = &entry->reg[t];

			if (reg->pending_on_resume) {
				irq_enter();
				generic_handle_irq(reg->irq);
				irq_exit();
				reg->pending_on_resume = false;
			}
		}
	}
#endif

	spin_lock(&irq_lock);
	list_for_each_entry(entry, &active_fds, list) {
		if (entry->suspended) {
			int err = os_set_fd_async(entry->fd);

			WARN(err < 0, "os_set_fd_async returned %d\n", err);
			entry->suspended = false;

			if (entry->sigio_workaround) {
				err = __add_sigio_fd(entry->fd);
				WARN(err < 0, "add_sigio_returned %d\n", err);
			}
		}
	}
	spin_unlock_irqrestore(&irq_lock, flags);

	irqs_suspended = false;
	send_sigio_to_self();
}

static int normal_irq_set_wake(struct irq_data *d, unsigned int on)
{
	struct irq_entry *entry;
	unsigned long flags;

	spin_lock_irqsave(&irq_lock, flags);
	list_for_each_entry(entry, &active_fds, list) {
		enum um_irq_type t;

		for (t = 0; t < NUM_IRQ_TYPES; t++) {
			if (!entry->reg[t].events)
				continue;

			if (entry->reg[t].irq != d->irq)
				continue;
			entry->reg[t].wakeup = on;
			goto unlock;
		}
	}
unlock:
	spin_unlock_irqrestore(&irq_lock, flags);
	return 0;
}
#else
#define normal_irq_set_wake NULL
#endif

/*
 * irq_chip must define at least enable/disable and ack when
 * the edge handler is used.
 */
static void dummy(struct irq_data *d)
{
}

/* This is used for everything other than the timer. */
static struct irq_chip normal_irq_type = {
	.name = "SIGIO",
	.irq_disable = dummy,
	.irq_enable = dummy,
	.irq_ack = dummy,
	.irq_mask = dummy,
	.irq_unmask = dummy,
	.irq_set_wake = normal_irq_set_wake,
};

static struct irq_chip alarm_irq_type = {
	.name = "SIGALRM",
	.irq_disable = dummy,
	.irq_enable = dummy,
	.irq_ack = dummy,
	.irq_mask = dummy,
	.irq_unmask = dummy,
};

void __init init_IRQ(void)
{
	int i;

	irq_set_chip_and_handler(TIMER_IRQ, &alarm_irq_type, handle_edge_irq);

	for (i = 1; i < UM_LAST_SIGNAL_IRQ; i++)
		irq_set_chip_and_handler(i, &normal_irq_type, handle_edge_irq);
	/* Initialize EPOLL Loop */
	os_setup_epoll();
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

