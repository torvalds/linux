#include <linux/types.h>
#include <linux/major.h>
#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/fcntl.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/devpts_fs.h>
#include <linux/file.h>
#include <linux/console.h>
#include <linux/timer.h>
#include <linux/ctype.h>
#include <linux/kd.h>
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
#include <linux/delay.h>
#include <linux/seq_file.h>

#include <linux/uaccess.h>
#include <asm/system.h>

#include <linux/kbd_kern.h>
#include <linux/vt_kern.h>
#include <linux/selection.h>

#include <linux/kmod.h>
#include <linux/nsproxy.h>

/*
 *	This guards the refcounted line discipline lists. The lock
 *	must be taken with irqs off because there are hangup path
 *	callers who will do ldisc lookups and cannot sleep.
 */

static DEFINE_SPINLOCK(tty_ldisc_lock);
static DECLARE_WAIT_QUEUE_HEAD(tty_ldisc_wait);
/* Line disc dispatch table */
static struct tty_ldisc_ops *tty_ldiscs[NR_LDISCS];

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


/**
 *	tty_ldisc_try_get	-	try and reference an ldisc
 *	@disc: ldisc number
 *
 *	Attempt to open and lock a line discipline into place. Return
 *	the line discipline refcounted or an error.
 */

static struct tty_ldisc *tty_ldisc_try_get(int disc)
{
	unsigned long flags;
	struct tty_ldisc *ld;
	struct tty_ldisc_ops *ldops;
	int err = -EINVAL;

	ld = kmalloc(sizeof(struct tty_ldisc), GFP_KERNEL);
	if (ld == NULL)
		return ERR_PTR(-ENOMEM);

	spin_lock_irqsave(&tty_ldisc_lock, flags);
	ld->ops = NULL;
	ldops = tty_ldiscs[disc];
	/* Check the entry is defined */
	if (ldops) {
		/* If the module is being unloaded we can't use it */
		if (!try_module_get(ldops->owner))
			err = -EAGAIN;
		else {
			/* lock it */
			ldops->refcount++;
			ld->ops = ldops;
			ld->refcount = 0;
			err = 0;
		}
	}
	spin_unlock_irqrestore(&tty_ldisc_lock, flags);
	if (err) {
		kfree(ld);
		return ERR_PTR(err);
	}
	return ld;
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

	if (disc < N_TTY || disc >= NR_LDISCS)
		return ERR_PTR(-EINVAL);
	ld = tty_ldisc_try_get(disc);
	if (IS_ERR(ld)) {
		request_module("tty-ldisc-%d", disc);
		ld = tty_ldisc_try_get(disc);
	}
	return ld;
}

/**
 *	tty_ldisc_put		-	drop ldisc reference
 *	@ld: ldisc
 *
 *	Drop a reference to a line discipline. Manage refcounts and
 *	module usage counts. Free the ldisc once the recount hits zero.
 *
 *	Locking:
 *		takes tty_ldisc_lock to guard against ldisc races
 */

static void tty_ldisc_put(struct tty_ldisc *ld)
{
	unsigned long flags;
	int disc = ld->ops->num;
	struct tty_ldisc_ops *ldo;

	BUG_ON(disc < N_TTY || disc >= NR_LDISCS);

	spin_lock_irqsave(&tty_ldisc_lock, flags);
	ldo = tty_ldiscs[disc];
	BUG_ON(ldo->refcount == 0);
	ldo->refcount--;
	module_put(ldo->owner);
	spin_unlock_irqrestore(&tty_ldisc_lock, flags);
	WARN_ON(ld->refcount);
	kfree(ld);
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
	struct tty_ldisc *ld;

	ld = tty_ldisc_try_get(i);
	if (IS_ERR(ld))
		return 0;
	seq_printf(m, "%-10s %2d\n", ld->ops->name ? ld->ops->name : "???", i);
	tty_ldisc_put(ld);
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

static int tty_ldisc_try(struct tty_struct *tty)
{
	unsigned long flags;
	struct tty_ldisc *ld;
	int ret = 0;

	spin_lock_irqsave(&tty_ldisc_lock, flags);
	ld = tty->ldisc;
	if (test_bit(TTY_LDISC, &tty->flags)) {
		ld->refcount++;
		ret = 1;
	}
	spin_unlock_irqrestore(&tty_ldisc_lock, flags);
	return ret;
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
	/* wait_event is a macro */
	wait_event(tty_ldisc_wait, tty_ldisc_try(tty));
	WARN_ON(tty->ldisc->refcount == 0);
	return tty->ldisc;
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
	if (tty_ldisc_try(tty))
		return tty->ldisc;
	return NULL;
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
	unsigned long flags;

	BUG_ON(ld == NULL);

	spin_lock_irqsave(&tty_ldisc_lock, flags);
	if (ld->refcount == 0)
		printk(KERN_ERR "tty_ldisc_deref: no references.\n");
	else
		ld->refcount--;
	if (ld->refcount == 0)
		wake_up(&tty_ldisc_wait);
	spin_unlock_irqrestore(&tty_ldisc_lock, flags);
}
EXPORT_SYMBOL_GPL(tty_ldisc_deref);

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
	tty->termios->c_line = num;
	mutex_unlock(&tty->termios_mutex);
}

/**
 *	tty_ldisc_open		-	open a line discipline
 *	@tty: tty we are opening the ldisc on
 *	@ld: discipline to open
 *
 *	A helper opening method. Also a convenient debugging and check
 *	point.
 */

static int tty_ldisc_open(struct tty_struct *tty, struct tty_ldisc *ld)
{
	WARN_ON(test_and_set_bit(TTY_LDISC_OPEN, &tty->flags));
	if (ld->ops->open)
		return ld->ops->open(tty);
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
 *	In order to wait for any existing references to complete see
 *	tty_ldisc_wait_idle.
 */

static int tty_ldisc_halt(struct tty_struct *tty)
{
	clear_bit(TTY_LDISC, &tty->flags);
	return cancel_delayed_work(&tty->buf.work);
}

/**
 *	tty_ldisc_wait_idle	-	wait for the ldisc to become idle
 *	@tty: tty to wait for
 *
 *	Wait for the line discipline to become idle. The discipline must
 *	have been halted for this to guarantee it remains idle.
 *
 *	tty_ldisc_lock protects the ref counts currently.
 */

static int tty_ldisc_wait_idle(struct tty_struct *tty)
{
	unsigned long flags;
	spin_lock_irqsave(&tty_ldisc_lock, flags);
	while (tty->ldisc->refcount) {
		spin_unlock_irqrestore(&tty_ldisc_lock, flags);
		if (wait_event_timeout(tty_ldisc_wait,
				tty->ldisc->refcount == 0, 5 * HZ) == 0)
			return -EBUSY;
		spin_lock_irqsave(&tty_ldisc_lock, flags);
	}
	spin_unlock_irqrestore(&tty_ldisc_lock, flags);
	return 0;
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

	/*
	 *	We need to look at the tty locking here for pty/tty pairs
	 *	when both sides try to change in parallel.
	 */

	o_tty = tty->link;	/* o_tty is the pty side or NULL */


	/*
	 *	Check the no-op case
	 */

	if (tty->ldisc->ops->num == ldisc) {
		tty_ldisc_put(new_ldisc);
		return 0;
	}

	/*
	 *	Problem: What do we do if this blocks ?
	 *	We could deadlock here
	 */

	tty_wait_until_sent(tty, 0);

	mutex_lock(&tty->ldisc_mutex);

	/*
	 *	We could be midstream of another ldisc change which has
	 *	dropped the lock during processing. If so we need to wait.
	 */

	while (test_bit(TTY_LDISC_CHANGING, &tty->flags)) {
		mutex_unlock(&tty->ldisc_mutex);
		wait_event(tty_ldisc_wait,
			test_bit(TTY_LDISC_CHANGING, &tty->flags) == 0);
		mutex_lock(&tty->ldisc_mutex);
	}
	set_bit(TTY_LDISC_CHANGING, &tty->flags);

	/*
	 *	No more input please, we are switching. The new ldisc
	 *	will update this value in the ldisc open function
	 */

	tty->receive_room = 0;

	o_ldisc = tty->ldisc;
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

	flush_scheduled_work();

	/* Let any existing reference holders finish */
	retval = tty_ldisc_wait_idle(tty);
	if (retval < 0) {
		clear_bit(TTY_LDISC_CHANGING, &tty->flags);
		tty_ldisc_put(new_ldisc);
		return retval;
	}

	mutex_lock(&tty->ldisc_mutex);
	if (test_bit(TTY_HUPPED, &tty->flags)) {
		/* We were raced by the hangup method. It will have stomped
		   the ldisc data and closed the ldisc down */
		clear_bit(TTY_LDISC_CHANGING, &tty->flags);
		mutex_unlock(&tty->ldisc_mutex);
		tty_ldisc_put(new_ldisc);
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

	/*
	 *	Allow ldisc referencing to occur again
	 */

	tty_ldisc_enable(tty);
	if (o_tty)
		tty_ldisc_enable(o_tty);

	/* Restart the work queue in case no characters kick it off. Safe if
	   already running */
	if (work)
		schedule_delayed_work(&tty->buf.work, 1);
	if (o_work)
		schedule_delayed_work(&o_tty->buf.work, 1);
	mutex_unlock(&tty->ldisc_mutex);
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
	*tty->termios = tty->driver->init_termios;
	tty->termios->c_ispeed = tty_termios_input_baud_rate(tty->termios);
	tty->termios->c_ospeed = tty_termios_baud_rate(tty->termios);
	mutex_unlock(&tty->termios_mutex);
}


/**
 *	tty_ldisc_reinit	-	reinitialise the tty ldisc
 *	@tty: tty to reinit
 *
 *	Switch the tty back to N_TTY line discipline and leave the
 *	ldisc state closed
 */

static void tty_ldisc_reinit(struct tty_struct *tty)
{
	struct tty_ldisc *ld;

	tty_ldisc_close(tty, tty->ldisc);
	tty_ldisc_put(tty->ldisc);
	tty->ldisc = NULL;
	/*
	 *	Switch the line discipline back
	 */
	ld = tty_ldisc_get(N_TTY);
	BUG_ON(IS_ERR(ld));
	tty_ldisc_assign(tty, ld);
	tty_set_termios_ldisc(tty, N_TTY);
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
	 * N_TTY.
	 */
	if (tty->driver->flags & TTY_DRIVER_RESET_TERMIOS) {
		/* Avoid racing set_ldisc */
		mutex_lock(&tty->ldisc_mutex);
		/* Switch back to N_TTY */
		tty_ldisc_halt(tty);
		tty_ldisc_wait_idle(tty);
		tty_ldisc_reinit(tty);
		/* At this point we have a closed ldisc and we want to
		   reopen it. We could defer this to the next open but
		   it means auditing a lot of other paths so this is a FIXME */
		WARN_ON(tty_ldisc_open(tty, tty->ldisc));
		tty_ldisc_enable(tty);
		mutex_unlock(&tty->ldisc_mutex);
		tty_reset_termios(tty);
	}
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

	tty_ldisc_halt(tty);
	flush_scheduled_work();

	/*
	 * Wait for any short term users (we know they are just driver
	 * side waiters as the file is closing so user count on the file
	 * side is zero.
	 */

	tty_ldisc_wait_idle(tty);

	/*
	 * Now kill off the ldisc
	 */
	tty_ldisc_close(tty, tty->ldisc);
	tty_ldisc_put(tty->ldisc);
	/* Force an oops if we mess this up */
	tty->ldisc = NULL;

	/* Ensure the next open requests the N_TTY ldisc */
	tty_set_termios_ldisc(tty, N_TTY);

	/* This will need doing differently if we need to lock */
	if (o_tty)
		tty_ldisc_release(o_tty, NULL);

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

void tty_ldisc_begin(void)
{
	/* Setup the default TTY line discipline. */
	(void) tty_register_ldisc(N_TTY, &tty_ldisc_N_TTY);
}
