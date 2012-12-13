#include <linux/types.h>
#include <linux/errno.h>
#include <linux/kmod.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/file.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/proc_fs.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/wait.h>
#include <linux/bitops.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/ratelimit.h>

/*
 *	This guards the refcounted line discipline lists. The lock
 *	must be taken with irqs off because there are hangup path
 *	callers who will do ldisc lookups and cannot sleep.
 */

static DEFINE_SPINLOCK(tty_ldisc_lock);
static DECLARE_WAIT_QUEUE_HEAD(tty_ldisc_wait);
/* Line disc dispatch table */
static struct tty_ldisc_ops *tty_ldiscs[NR_LDISCS];

static inline struct tty_ldisc *get_ldisc(struct tty_ldisc *ld)
{
	if (ld)
		atomic_inc(&ld->users);
	return ld;
}

static void put_ldisc(struct tty_ldisc *ld)
{
	unsigned long flags;

	if (WARN_ON_ONCE(!ld))
		return;

	/*
	 * If this is the last user, free the ldisc, and
	 * release the ldisc ops.
	 *
	 * We really want an "atomic_dec_and_lock_irqsave()",
	 * but we don't have it, so this does it by hand.
	 */
	local_irq_save(flags);
	if (atomic_dec_and_lock(&ld->users, &tty_ldisc_lock)) {
		struct tty_ldisc_ops *ldo = ld->ops;

		ldo->refcount--;
		module_put(ldo->owner);
		spin_unlock_irqrestore(&tty_ldisc_lock, flags);

		kfree(ld);
		return;
	}
	local_irq_restore(flags);
	wake_up(&ld->wq_idle);
}

/**
 *	tty_register_ldisc	-	install a line discipline
 *	@disc: ldisc number
 *	@new_ldisc: pointer to the ldisc object
 *
 *	Installs a new line discipline into the kernel. The discipline
 *	is set up as unreferenced and then made available to the kernel
 *	from this point onwards.
 *
 *	Locking:
 *		takes tty_ldisc_lock to guard against ldisc races
 */

int tty_register_ldisc(int disc, struct tty_ldisc_ops *new_ldisc)
{
	unsigned long flags;
	int ret = 0;

	if (disc < N_TTY || disc >= NR_LDISCS)
		return -EINVAL;

	spin_lock_irqsave(&tty_ldisc_lock, flags);
	tty_ldiscs[disc] = new_ldisc;
	new_ldisc->num = disc;
	new_ldisc->refcount = 0;
	spin_unlock_irqrestore(&tty_ldisc_lock, flags);

	return ret;
}
EXPORT_SYMBOL(tty_register_ldisc);

/**
 *	tty_unregister_ldisc	-	unload a line discipline
 *	@disc: ldisc number
 *	@new_ldisc: pointer to the ldisc object
 *
 *	Remove a line discipline from the kernel providing it is not
 *	currently in use.
 *
 *	Locking:
 *		takes tty_ldisc_lock to guard against ldisc races
 */

int tty_unregister_ldisc(int disc)
{
	unsigned long flags;
	int ret = 0;

	if (disc < N_TTY || disc >= NR_LDISCS)
		return -EINVAL;

	spin_lock_irqsave(&tty_ldisc_lock, flags);
	if (tty_ldiscs[disc]->refcount)
		ret = -EBUSY;
	else
		tty_ldiscs[disc] = NULL;
	spin_unlock_irqrestore(&tty_ldisc_lock, flags);

	return ret;
}
EXPORT_SYMBOL(tty_unregister_ldisc);

static struct tty_ldisc_ops *get_ldops(int disc)
{
	unsigned long flags;
	struct tty_ldisc_ops *ldops, *ret;

	spin_lock_irqsave(&tty_ldisc_lock, flags);
	ret = ERR_PTR(-EINVAL);
	ldops = tty_ldiscs[disc];
	if (ldops) {
		ret = ERR_PTR(-EAGAIN);
		if (try_module_get(ldops->owner)) {
			ldops->refcount++;
			ret = ldops;
		}
	}
	spin_unlock_irqrestore(&tty_ldisc_lock, flags);
	return ret;
}

static void put_ldops(struct tty_ldisc_ops *ldops)
{
	unsigned long flags;

	spin_lock_irqsave(&tty_ldisc_lock, flags);
	ldops->refcount--;
	module_put(ldops->owner);
	spin_unlock_irqrestore(&tty_ldisc_lock, flags);
}

/**
 *	tty_ldisc_get		-	take a reference to an ldisc
 *	@disc: ldisc number
 *
 *	Takes a reference to a line discipline. Deals with refcounts and
 *	module locking counts. Returns NULL if the discipline is not available.
 *	Returns a pointer to the discipline and bumps the ref count if it is
 *	available
 *
 *	Locking:
 *		takes tty_ldisc_lock to guard against ldisc races
 */

static struct tty_ldisc *tty_ldisc_get(int disc)
{
	struct tty_ldisc *ld;
	struct tty_ldisc_ops *ldops;

	if (disc < N_TTY || disc >= NR_LDISCS)
		return ERR_PTR(-EINVAL);

	/*
	 * Get the ldisc ops - we may need to request them to be loaded
	 * dynamically and try again.
	 */
	ldops = get_ldops(disc);
	if (IS_ERR(ldops)) {
		request_module("tty-ldisc-%d", disc);
		ldops = get_ldops(disc);
		if (IS_ERR(ldops))
			return ERR_CAST(ldops);
	}

	ld = kmalloc(sizeof(struct tty_ldisc), GFP_KERNEL);
	if (ld == NULL) {
		put_ldops(ldops);
		return ERR_PTR(-ENOMEM);
	}

	ld->ops = ldops;
	atomic_set(&ld->users, 1);
	init_waitqueue_head(&ld->wq_idle);

	return ld;
}

static void *tty_ldiscs_seq_start(struct seq_file *m, loff_t *pos)
{
	return (*pos < NR_LDISCS) ? pos : NULL;
}

static void *tty_ldiscs_seq_next(struct seq_file *m, void *v, loff_t *pos)
{
	(*pos)++;
	return (*pos < NR_LDISCS) ? pos : NULL;
}

static void tty_ldiscs_seq_stop(struct seq_file *m, void *v)
{
}

static int tty_ldiscs_seq_show(struct seq_file *m, void *v)
{
	int i = *(loff_t *)v;
	struct tty_ldisc_ops *ldops;

	ldops = get_ldops(i);
	if (IS_ERR(ldops))
		return 0;
	seq_printf(m, "%-10s %2d\n", ldops->name ? ldops->name : "???", i);
	put_ldops(ldops);
	return 0;
}

static const struct seq_operations tty_ldiscs_seq_ops = {
	.start	= tty_ldiscs_seq_start,
	.next	= tty_ldiscs_seq_next,
	.stop	= tty_ldiscs_seq_stop,
	.show	= tty_ldiscs_seq_show,
};

static int proc_tty_ldiscs_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &tty_ldiscs_seq_ops);
}

const struct file_operations tty_ldiscs_proc_fops = {
	.owner		= THIS_MODULE,
	.open		= proc_tty_ldiscs_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

/**
 *	tty_ldisc_assign	-	set ldisc on a tty
 *	@tty: tty to assign
 *	@ld: line discipline
 *
 *	Install an instance of a line discipline into a tty structure. The
 *	ldisc must have a reference count above zero to ensure it remains.
 *	The tty instance refcount starts at zero.
 *
 *	Locking:
 *		Caller must hold references
 */

static void tty_ldisc_assign(struct tty_struct *tty, struct tty_ldisc *ld)
{
	tty->ldisc = ld;
}

/**
 *	tty_ldisc_try		-	internal helper
 *	@tty: the tty
 *
 *	Make a single attempt to grab and bump the refcount on
 *	the tty ldisc. Return 0 on failure or 1 on success. This is
 *	used to implement both the waiting and non waiting versions
 *	of tty_ldisc_ref
 *
 *	Locking: takes tty_ldisc_lock
 */

static struct tty_ldisc *tty_ldisc_try(struct tty_struct *tty)
{
	unsigned long flags;
	struct tty_ldisc *ld;

	spin_lock_irqsave(&tty_ldisc_lock, flags);
	ld = NULL;
	if (test_bit(TTY_LDISC, &tty->flags))
		ld = get_ldisc(tty->ldisc);
	spin_unlock_irqrestore(&tty_ldisc_lock, flags);
	return ld;
}

/**
 *	tty_ldisc_ref_wait	-	wait for the tty ldisc
 *	@tty: tty device
 *
 *	Dereference the line discipline for the terminal and take a
 *	reference to it. If the line discipline is in flux then
 *	wait patiently until it changes.
 *
 *	Note: Must not be called from an IRQ/timer context. The caller
 *	must also be careful not to hold other locks that will deadlock
 *	against a discipline change, such as an existing ldisc reference
 *	(which we check for)
 *
 *	Locking: call functions take tty_ldisc_lock
 */

struct tty_ldisc *tty_ldisc_ref_wait(struct tty_struct *tty)
{
	struct tty_ldisc *ld;

	/* wait_event is a macro */
	wait_event(tty_ldisc_wait, (ld = tty_ldisc_try(tty)) != NULL);
	return ld;
}
EXPORT_SYMBOL_GPL(tty_ldisc_ref_wait);

/**
 *	tty_ldisc_ref		-	get the tty ldisc
 *	@tty: tty device
 *
 *	Dereference the line discipline for the terminal and take a
 *	reference to it. If the line discipline is in flux then
 *	return NULL. Can be called from IRQ and timer functions.
 *
 *	Locking: called functions take tty_ldisc_lock
 */

struct tty_ldisc *tty_ldisc_ref(struct tty_struct *tty)
{
	return tty_ldisc_try(tty);
}
EXPORT_SYMBOL_GPL(tty_ldisc_ref);

/**
 *	tty_ldisc_deref		-	free a tty ldisc reference
 *	@ld: reference to free up
 *
 *	Undoes the effect of tty_ldisc_ref or tty_ldisc_ref_wait. May
 *	be called in IRQ context.
 *
 *	Locking: takes tty_ldisc_lock
 */

void tty_ldisc_deref(struct tty_ldisc *ld)
{
	put_ldisc(ld);
}
EXPORT_SYMBOL_GPL(tty_ldisc_deref);

static inline void tty_ldisc_put(struct tty_ldisc *ld)
{
	put_ldisc(ld);
}

/**
 *	tty_ldisc_enable	-	allow ldisc use
 *	@tty: terminal to activate ldisc on
 *
 *	Set the TTY_LDISC flag when the line discipline can be called
 *	again. Do necessary wakeups for existing sleepers. Clear the LDISC
 *	changing flag to indicate any ldisc change is now over.
 *
 *	Note: nobody should set the TTY_LDISC bit except via this function.
 *	Clearing directly is allowed.
 */

void tty_ldisc_enable(struct tty_struct *tty)
{
	set_bit(TTY_LDISC, &tty->flags);
	clear_bit(TTY_LDISC_CHANGING, &tty->flags);
	wake_up(&tty_ldisc_wait);
}

/**
 *	tty_ldisc_flush	-	flush line discipline queue
 *	@tty: tty
 *
 *	Flush the line discipline queue (if any) for this tty. If there
 *	is no line discipline active this is a no-op.
 */

void tty_ldisc_flush(struct tty_struct *tty)
{
	struct tty_ldisc *ld = tty_ldisc_ref(tty);
	if (ld) {
		if (ld->ops->flush_buffer)
			ld->ops->flush_buffer(tty);
		tty_ldisc_deref(ld);
	}
	tty_buffer_flush(tty);
}
EXPORT_SYMBOL_GPL(tty_ldisc_flush);

/**
 *	tty_set_termios_ldisc		-	set ldisc field
 *	@tty: tty structure
 *	@num: line discipline number
 *
 *	This is probably overkill for real world processors but
 *	they are not on hot paths so a little discipline won't do
 *	any harm.
 *
 *	Locking: takes termios_mutex
 */

static void tty_set_termios_ldisc(struct tty_struct *tty, int num)
{
	mutex_lock(&tty->termios_mutex);
	tty->termios.c_line = num;
	mutex_unlock(&tty->termios_mutex);
}

/**
 *	tty_ldisc_open		-	open a line discipline
 *	@tty: tty we are opening the ldisc on
 *	@ld: discipline to open
 *
 *	A helper opening method. Also a convenient debugging and check
 *	point.
 *
 *	Locking: always called with BTM already held.
 */

static int tty_ldisc_open(struct tty_struct *tty, struct tty_ldisc *ld)
{
	WARN_ON(test_and_set_bit(TTY_LDISC_OPEN, &tty->flags));
	if (ld->ops->open) {
		int ret;
                /* BTM here locks versus a hangup event */
		ret = ld->ops->open(tty);
		if (ret)
			clear_bit(TTY_LDISC_OPEN, &tty->flags);
		return ret;
	}
	return 0;
}

/**
 *	tty_ldisc_close		-	close a line discipline
 *	@tty: tty we are opening the ldisc on
 *	@ld: discipline to close
 *
 *	A helper close method. Also a convenient debugging and check
 *	point.
 */

static void tty_ldisc_close(struct tty_struct *tty, struct tty_ldisc *ld)
{
	WARN_ON(!test_bit(TTY_LDISC_OPEN, &tty->flags));
	clear_bit(TTY_LDISC_OPEN, &tty->flags);
	if (ld->ops->close)
		ld->ops->close(tty);
}

/**
 *	tty_ldisc_restore	-	helper for tty ldisc change
 *	@tty: tty to recover
 *	@old: previous ldisc
 *
 *	Restore the previous line discipline or N_TTY when a line discipline
 *	change fails due to an open error
 */

static void tty_ldisc_restore(struct tty_struct *tty, struct tty_ldisc *old)
{
	char buf[64];
	struct tty_ldisc *new_ldisc;
	int r;

	/* There is an outstanding reference here so this is safe */
	old = tty_ldisc_get(old->ops->num);
	WARN_ON(IS_ERR(old));
	tty_ldisc_assign(tty, old);
	tty_set_termios_ldisc(tty, old->ops->num);
	if (tty_ldisc_open(tty, old) < 0) {
		tty_ldisc_put(old);
		/* This driver is always present */
		new_ldisc = tty_ldisc_get(N_TTY);
		if (IS_ERR(new_ldisc))
			panic("n_tty: get");
		tty_ldisc_assign(tty, new_ldisc);
		tty_set_termios_ldisc(tty, N_TTY);
		r = tty_ldisc_open(tty, new_ldisc);
		if (r < 0)
			panic("Couldn't open N_TTY ldisc for "
			      "%s --- error %d.",
			      tty_name(tty, buf), r);
	}
}

/**
 *	tty_ldisc_halt		-	shut down the line discipline
 *	@tty: tty device
 *
 *	Shut down the line discipline and work queue for this tty device.
 *	The TTY_LDISC flag being cleared ensures no further references can
 *	be obtained while the delayed work queue halt ensures that no more
 *	data is fed to the ldisc.
 *
 *	You need to do a 'flush_scheduled_work()' (outside the ldisc_mutex)
 *	in order to make sure any currently executing ldisc work is also
 *	flushed.
 */

static int tty_ldisc_halt(struct tty_struct *tty)
{
	clear_bit(TTY_LDISC, &tty->flags);
	return cancel_work_sync(&tty->port->buf.work);
}

/**
 *	tty_ldisc_flush_works	-	flush all works of a tty
 *	@tty: tty device to flush works for
 *
 *	Sync flush all works belonging to @tty.
 */
static void tty_ldisc_flush_works(struct tty_struct *tty)
{
	flush_work(&tty->hangup_work);
	flush_work(&tty->SAK_work);
	flush_work(&tty->port->buf.work);
}

/**
 *	tty_ldisc_wait_idle	-	wait for the ldisc to become idle
 *	@tty: tty to wait for
 *	@timeout: for how long to wait at most
 *
 *	Wait for the line discipline to become idle. The discipline must
 *	have been halted for this to guarantee it remains idle.
 */
static int tty_ldisc_wait_idle(struct tty_struct *tty, long timeout)
{
	long ret;
	ret = wait_event_timeout(tty->ldisc->wq_idle,
			atomic_read(&tty->ldisc->users) == 1, timeout);
	return ret > 0 ? 0 : -EBUSY;
}

/**
 *	tty_set_ldisc		-	set line discipline
 *	@tty: the terminal to set
 *	@ldisc: the line discipline
 *
 *	Set the discipline of a tty line. Must be called from a process
 *	context. The ldisc change logic has to protect itself against any
 *	overlapping ldisc change (including on the other end of pty pairs),
 *	the close of one side of a tty/pty pair, and eventually hangup.
 *
 *	Locking: takes tty_ldisc_lock, termios_mutex
 */

int tty_set_ldisc(struct tty_struct *tty, int ldisc)
{
	int retval;
	struct tty_ldisc *o_ldisc, *new_ldisc;
	int work, o_work = 0;
	struct tty_struct *o_tty;

	new_ldisc = tty_ldisc_get(ldisc);
	if (IS_ERR(new_ldisc))
		return PTR_ERR(new_ldisc);

	tty_lock(tty);
	/*
	 *	We need to look at the tty locking here for pty/tty pairs
	 *	when both sides try to change in parallel.
	 */

	o_tty = tty->link;	/* o_tty is the pty side or NULL */


	/*
	 *	Check the no-op case
	 */

	if (tty->ldisc->ops->num == ldisc) {
		tty_unlock(tty);
		tty_ldisc_put(new_ldisc);
		return 0;
	}

	tty_unlock(tty);
	/*
	 *	Problem: What do we do if this blocks ?
	 *	We could deadlock here
	 */

	tty_wait_until_sent(tty, 0);

	tty_lock(tty);
	mutex_lock(&tty->ldisc_mutex);

	/*
	 *	We could be midstream of another ldisc change which has
	 *	dropped the lock during processing. If so we need to wait.
	 */

	while (test_bit(TTY_LDISC_CHANGING, &tty->flags)) {
		mutex_unlock(&tty->ldisc_mutex);
		tty_unlock(tty);
		wait_event(tty_ldisc_wait,
			test_bit(TTY_LDISC_CHANGING, &tty->flags) == 0);
		tty_lock(tty);
		mutex_lock(&tty->ldisc_mutex);
	}

	set_bit(TTY_LDISC_CHANGING, &tty->flags);

	/*
	 *	No more input please, we are switching. The new ldisc
	 *	will update this value in the ldisc open function
	 */

	tty->receive_room = 0;

	o_ldisc = tty->ldisc;

	tty_unlock(tty);
	/*
	 *	Make sure we don't change while someone holds a
	 *	reference to the line discipline. The TTY_LDISC bit
	 *	prevents anyone taking a reference once it is clear.
	 *	We need the lock to avoid racing reference takers.
	 *
	 *	We must clear the TTY_LDISC bit here to avoid a livelock
	 *	with a userspace app continually trying to use the tty in
	 *	parallel to the change and re-referencing the tty.
	 */

	work = tty_ldisc_halt(tty);
	if (o_tty)
		o_work = tty_ldisc_halt(o_tty);

	/*
	 * Wait for ->hangup_work and ->buf.work handlers to terminate.
	 * We must drop the mutex here in case a hangup is also in process.
	 */

	mutex_unlock(&tty->ldisc_mutex);

	tty_ldisc_flush_works(tty);

	retval = tty_ldisc_wait_idle(tty, 5 * HZ);

	tty_lock(tty);
	mutex_lock(&tty->ldisc_mutex);

	/* handle wait idle failure locked */
	if (retval) {
		tty_ldisc_put(new_ldisc);
		goto enable;
	}

	if (test_bit(TTY_HUPPING, &tty->flags)) {
		/* We were raced by the hangup method. It will have stomped
		   the ldisc data and closed the ldisc down */
		clear_bit(TTY_LDISC_CHANGING, &tty->flags);
		mutex_unlock(&tty->ldisc_mutex);
		tty_ldisc_put(new_ldisc);
		tty_unlock(tty);
		return -EIO;
	}

	/* Shutdown the current discipline. */
	tty_ldisc_close(tty, o_ldisc);

	/* Now set up the new line discipline. */
	tty_ldisc_assign(tty, new_ldisc);
	tty_set_termios_ldisc(tty, ldisc);

	retval = tty_ldisc_open(tty, new_ldisc);
	if (retval < 0) {
		/* Back to the old one or N_TTY if we can't */
		tty_ldisc_put(new_ldisc);
		tty_ldisc_restore(tty, o_ldisc);
	}

	/* At this point we hold a reference to the new ldisc and a
	   a reference to the old ldisc. If we ended up flipping back
	   to the existing ldisc we have two references to it */

	if (tty->ldisc->ops->num != o_ldisc->ops->num && tty->ops->set_ldisc)
		tty->ops->set_ldisc(tty);

	tty_ldisc_put(o_ldisc);

enable:
	/*
	 *	Allow ldisc referencing to occur again
	 */

	tty_ldisc_enable(tty);
	if (o_tty)
		tty_ldisc_enable(o_tty);

	/* Restart the work queue in case no characters kick it off. Safe if
	   already running */
	if (work)
		schedule_work(&tty->port->buf.work);
	if (o_work)
		schedule_work(&o_tty->port->buf.work);
	mutex_unlock(&tty->ldisc_mutex);
	tty_unlock(tty);
	return retval;
}

/**
 *	tty_reset_termios	-	reset terminal state
 *	@tty: tty to reset
 *
 *	Restore a terminal to the driver default state.
 */

static void tty_reset_termios(struct tty_struct *tty)
{
	mutex_lock(&tty->termios_mutex);
	tty->termios = tty->driver->init_termios;
	tty->termios.c_ispeed = tty_termios_input_baud_rate(&tty->termios);
	tty->termios.c_ospeed = tty_termios_baud_rate(&tty->termios);
	mutex_unlock(&tty->termios_mutex);
}


/**
 *	tty_ldisc_reinit	-	reinitialise the tty ldisc
 *	@tty: tty to reinit
 *	@ldisc: line discipline to reinitialize
 *
 *	Switch the tty to a line discipline and leave the ldisc
 *	state closed
 */

static int tty_ldisc_reinit(struct tty_struct *tty, int ldisc)
{
	struct tty_ldisc *ld = tty_ldisc_get(ldisc);

	if (IS_ERR(ld))
		return -1;

	tty_ldisc_close(tty, tty->ldisc);
	tty_ldisc_put(tty->ldisc);
	tty->ldisc = NULL;
	/*
	 *	Switch the line discipline back
	 */
	tty_ldisc_assign(tty, ld);
	tty_set_termios_ldisc(tty, ldisc);

	return 0;
}

/**
 *	tty_ldisc_hangup		-	hangup ldisc reset
 *	@tty: tty being hung up
 *
 *	Some tty devices reset their termios when they receive a hangup
 *	event. In that situation we must also switch back to N_TTY properly
 *	before we reset the termios data.
 *
 *	Locking: We can take the ldisc mutex as the rest of the code is
 *	careful to allow for this.
 *
 *	In the pty pair case this occurs in the close() path of the
 *	tty itself so we must be careful about locking rules.
 */

void tty_ldisc_hangup(struct tty_struct *tty)
{
	struct tty_ldisc *ld;
	int reset = tty->driver->flags & TTY_DRIVER_RESET_TERMIOS;
	int err = 0;

	/*
	 * FIXME! What are the locking issues here? This may me overdoing
	 * things... This question is especially important now that we've
	 * removed the irqlock.
	 */
	ld = tty_ldisc_ref(tty);
	if (ld != NULL) {
		/* We may have no line discipline at this point */
		if (ld->ops->flush_buffer)
			ld->ops->flush_buffer(tty);
		tty_driver_flush_buffer(tty);
		if ((test_bit(TTY_DO_WRITE_WAKEUP, &tty->flags)) &&
		    ld->ops->write_wakeup)
			ld->ops->write_wakeup(tty);
		if (ld->ops->hangup)
			ld->ops->hangup(tty);
		tty_ldisc_deref(ld);
	}
	/*
	 * FIXME: Once we trust the LDISC code better we can wait here for
	 * ldisc completion and fix the driver call race
	 */
	wake_up_interruptible_poll(&tty->write_wait, POLLOUT);
	wake_up_interruptible_poll(&tty->read_wait, POLLIN);
	/*
	 * Shutdown the current line discipline, and reset it to
	 * N_TTY if need be.
	 *
	 * Avoid racing set_ldisc or tty_ldisc_release
	 */
	mutex_lock(&tty->ldisc_mutex);

	/*
	 * this is like tty_ldisc_halt, but we need to give up
	 * the BTM before calling cancel_work_sync, which may
	 * need to wait for another function taking the BTM
	 */
	clear_bit(TTY_LDISC, &tty->flags);
	tty_unlock(tty);
	cancel_work_sync(&tty->port->buf.work);
	mutex_unlock(&tty->ldisc_mutex);
retry:
	tty_lock(tty);
	mutex_lock(&tty->ldisc_mutex);

	/* At this point we have a closed ldisc and we want to
	   reopen it. We could defer this to the next open but
	   it means auditing a lot of other paths so this is
	   a FIXME */
	if (tty->ldisc) {	/* Not yet closed */
		if (atomic_read(&tty->ldisc->users) != 1) {
			char cur_n[TASK_COMM_LEN], tty_n[64];
			long timeout = 3 * HZ;
			tty_unlock(tty);

			while (tty_ldisc_wait_idle(tty, timeout) == -EBUSY) {
				timeout = MAX_SCHEDULE_TIMEOUT;
				printk_ratelimited(KERN_WARNING
					"%s: waiting (%s) for %s took too long, but we keep waiting...\n",
					__func__, get_task_comm(cur_n, current),
					tty_name(tty, tty_n));
			}
			mutex_unlock(&tty->ldisc_mutex);
			goto retry;
		}

		if (reset == 0) {

			if (!tty_ldisc_reinit(tty, tty->termios.c_line))
				err = tty_ldisc_open(tty, tty->ldisc);
			else
				err = 1;
		}
		/* If the re-open fails or we reset then go to N_TTY. The
		   N_TTY open cannot fail */
		if (reset || err) {
			BUG_ON(tty_ldisc_reinit(tty, N_TTY));
			WARN_ON(tty_ldisc_open(tty, tty->ldisc));
		}
		tty_ldisc_enable(tty);
	}
	mutex_unlock(&tty->ldisc_mutex);
	if (reset)
		tty_reset_termios(tty);
}

/**
 *	tty_ldisc_setup			-	open line discipline
 *	@tty: tty being shut down
 *	@o_tty: pair tty for pty/tty pairs
 *
 *	Called during the initial open of a tty/pty pair in order to set up the
 *	line disciplines and bind them to the tty. This has no locking issues
 *	as the device isn't yet active.
 */

int tty_ldisc_setup(struct tty_struct *tty, struct tty_struct *o_tty)
{
	struct tty_ldisc *ld = tty->ldisc;
	int retval;

	retval = tty_ldisc_open(tty, ld);
	if (retval)
		return retval;

	if (o_tty) {
		retval = tty_ldisc_open(o_tty, o_tty->ldisc);
		if (retval) {
			tty_ldisc_close(tty, ld);
			return retval;
		}
		tty_ldisc_enable(o_tty);
	}
	tty_ldisc_enable(tty);
	return 0;
}

static void tty_ldisc_kill(struct tty_struct *tty)
{
	/* There cannot be users from userspace now. But there still might be
	 * drivers holding a reference via tty_ldisc_ref. Do not steal them the
	 * ldisc until they are done. */
	tty_ldisc_wait_idle(tty, MAX_SCHEDULE_TIMEOUT);

	mutex_lock(&tty->ldisc_mutex);
	/*
	 * Now kill off the ldisc
	 */
	tty_ldisc_close(tty, tty->ldisc);
	tty_ldisc_put(tty->ldisc);
	/* Force an oops if we mess this up */
	tty->ldisc = NULL;

	/* Ensure the next open requests the N_TTY ldisc */
	tty_set_termios_ldisc(tty, N_TTY);
	mutex_unlock(&tty->ldisc_mutex);
}

/**
 *	tty_ldisc_release		-	release line discipline
 *	@tty: tty being shut down
 *	@o_tty: pair tty for pty/tty pairs
 *
 *	Called during the final close of a tty/pty pair in order to shut down
 *	the line discpline layer. On exit the ldisc assigned is N_TTY and the
 *	ldisc has not been opened.
 */

void tty_ldisc_release(struct tty_struct *tty, struct tty_struct *o_tty)
{
	/*
	 * Prevent flush_to_ldisc() from rescheduling the work for later.  Then
	 * kill any delayed work. As this is the final close it does not
	 * race with the set_ldisc code path.
	 */

	tty_lock_pair(tty, o_tty);
	tty_ldisc_halt(tty);
	tty_ldisc_flush_works(tty);
	if (o_tty) {
		tty_ldisc_halt(o_tty);
		tty_ldisc_flush_works(o_tty);
	}

	/* This will need doing differently if we need to lock */
	tty_ldisc_kill(tty);

	if (o_tty)
		tty_ldisc_kill(o_tty);

	tty_unlock_pair(tty, o_tty);
	/* And the memory resources remaining (buffers, termios) will be
	   disposed of when the kref hits zero */
}

/**
 *	tty_ldisc_init		-	ldisc setup for new tty
 *	@tty: tty being allocated
 *
 *	Set up the line discipline objects for a newly allocated tty. Note that
 *	the tty structure is not completely set up when this call is made.
 */

void tty_ldisc_init(struct tty_struct *tty)
{
	struct tty_ldisc *ld = tty_ldisc_get(N_TTY);
	if (IS_ERR(ld))
		panic("n_tty: init_tty");
	tty_ldisc_assign(tty, ld);
}

/**
 *	tty_ldisc_init		-	ldisc cleanup for new tty
 *	@tty: tty that was allocated recently
 *
 *	The tty structure must not becompletely set up (tty_ldisc_setup) when
 *      this call is made.
 */
void tty_ldisc_deinit(struct tty_struct *tty)
{
	put_ldisc(tty->ldisc);
	tty_ldisc_assign(tty, NULL);
}

void tty_ldisc_begin(void)
{
	/* Setup the default TTY line discipline. */
	(void) tty_register_ldisc(N_TTY, &tty_ldisc_N_TTY);
}
