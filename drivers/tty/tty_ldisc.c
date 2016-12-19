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
#include <linux/module.h>
#include <linux/device.h>
#include <linux/wait.h>
#include <linux/bitops.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/ratelimit.h>

#undef LDISC_DEBUG_HANGUP

#ifdef LDISC_DEBUG_HANGUP
#define tty_ldisc_debug(tty, f, args...)	tty_debug(tty, f, ##args)
#else
#define tty_ldisc_debug(tty, f, args...)
#endif

/* lockdep nested classes for tty->ldisc_sem */
enum {
	LDISC_SEM_NORMAL,
	LDISC_SEM_OTHER,
};


/*
 *	This guards the refcounted line discipline lists. The lock
 *	must be taken with irqs off because there are hangup path
 *	callers who will do ldisc lookups and cannot sleep.
 */

static DEFINE_RAW_SPINLOCK(tty_ldiscs_lock);
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
 *		takes tty_ldiscs_lock to guard against ldisc races
 */

int tty_register_ldisc(int disc, struct tty_ldisc_ops *new_ldisc)
{
	unsigned long flags;
	int ret = 0;

	if (disc < N_TTY || disc >= NR_LDISCS)
		return -EINVAL;

	raw_spin_lock_irqsave(&tty_ldiscs_lock, flags);
	tty_ldiscs[disc] = new_ldisc;
	new_ldisc->num = disc;
	new_ldisc->refcount = 0;
	raw_spin_unlock_irqrestore(&tty_ldiscs_lock, flags);

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
 *		takes tty_ldiscs_lock to guard against ldisc races
 */

int tty_unregister_ldisc(int disc)
{
	unsigned long flags;
	int ret = 0;

	if (disc < N_TTY || disc >= NR_LDISCS)
		return -EINVAL;

	raw_spin_lock_irqsave(&tty_ldiscs_lock, flags);
	if (tty_ldiscs[disc]->refcount)
		ret = -EBUSY;
	else
		tty_ldiscs[disc] = NULL;
	raw_spin_unlock_irqrestore(&tty_ldiscs_lock, flags);

	return ret;
}
EXPORT_SYMBOL(tty_unregister_ldisc);

static struct tty_ldisc_ops *get_ldops(int disc)
{
	unsigned long flags;
	struct tty_ldisc_ops *ldops, *ret;

	raw_spin_lock_irqsave(&tty_ldiscs_lock, flags);
	ret = ERR_PTR(-EINVAL);
	ldops = tty_ldiscs[disc];
	if (ldops) {
		ret = ERR_PTR(-EAGAIN);
		if (try_module_get(ldops->owner)) {
			ldops->refcount++;
			ret = ldops;
		}
	}
	raw_spin_unlock_irqrestore(&tty_ldiscs_lock, flags);
	return ret;
}

static void put_ldops(struct tty_ldisc_ops *ldops)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&tty_ldiscs_lock, flags);
	ldops->refcount--;
	module_put(ldops->owner);
	raw_spin_unlock_irqrestore(&tty_ldiscs_lock, flags);
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
 *		takes tty_ldiscs_lock to guard against ldisc races
 */

static struct tty_ldisc *tty_ldisc_get(struct tty_struct *tty, int disc)
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
	ld->tty = tty;

	return ld;
}

/**
 *	tty_ldisc_put		-	release the ldisc
 *
 *	Complement of tty_ldisc_get().
 */
static inline void tty_ldisc_put(struct tty_ldisc *ld)
{
	if (WARN_ON_ONCE(!ld))
		return;

	put_ldops(ld->ops);
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
 *	Note: only callable from a file_operations routine (which
 *	guarantees tty->ldisc != NULL when the lock is acquired).
 */

struct tty_ldisc *tty_ldisc_ref_wait(struct tty_struct *tty)
{
	ldsem_down_read(&tty->ldisc_sem, MAX_SCHEDULE_TIMEOUT);
	WARN_ON(!tty->ldisc);
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
 */

struct tty_ldisc *tty_ldisc_ref(struct tty_struct *tty)
{
	struct tty_ldisc *ld = NULL;

	if (ldsem_down_read_trylock(&tty->ldisc_sem)) {
		ld = tty->ldisc;
		if (!ld)
			ldsem_up_read(&tty->ldisc_sem);
	}
	return ld;
}
EXPORT_SYMBOL_GPL(tty_ldisc_ref);

/**
 *	tty_ldisc_deref		-	free a tty ldisc reference
 *	@ld: reference to free up
 *
 *	Undoes the effect of tty_ldisc_ref or tty_ldisc_ref_wait. May
 *	be called in IRQ context.
 */

void tty_ldisc_deref(struct tty_ldisc *ld)
{
	ldsem_up_read(&ld->tty->ldisc_sem);
}
EXPORT_SYMBOL_GPL(tty_ldisc_deref);


static inline int __lockfunc
__tty_ldisc_lock(struct tty_struct *tty, unsigned long timeout)
{
	return ldsem_down_write(&tty->ldisc_sem, timeout);
}

static inline int __lockfunc
__tty_ldisc_lock_nested(struct tty_struct *tty, unsigned long timeout)
{
	return ldsem_down_write_nested(&tty->ldisc_sem,
				       LDISC_SEM_OTHER, timeout);
}

static inline void __tty_ldisc_unlock(struct tty_struct *tty)
{
	ldsem_up_write(&tty->ldisc_sem);
}

static int __lockfunc
tty_ldisc_lock(struct tty_struct *tty, unsigned long timeout)
{
	int ret;

	ret = __tty_ldisc_lock(tty, timeout);
	if (!ret)
		return -EBUSY;
	set_bit(TTY_LDISC_HALTED, &tty->flags);
	return 0;
}

static void tty_ldisc_unlock(struct tty_struct *tty)
{
	clear_bit(TTY_LDISC_HALTED, &tty->flags);
	__tty_ldisc_unlock(tty);
}

static int __lockfunc
tty_ldisc_lock_pair_timeout(struct tty_struct *tty, struct tty_struct *tty2,
			    unsigned long timeout)
{
	int ret;

	if (tty < tty2) {
		ret = __tty_ldisc_lock(tty, timeout);
		if (ret) {
			ret = __tty_ldisc_lock_nested(tty2, timeout);
			if (!ret)
				__tty_ldisc_unlock(tty);
		}
	} else {
		/* if this is possible, it has lots of implications */
		WARN_ON_ONCE(tty == tty2);
		if (tty2 && tty != tty2) {
			ret = __tty_ldisc_lock(tty2, timeout);
			if (ret) {
				ret = __tty_ldisc_lock_nested(tty, timeout);
				if (!ret)
					__tty_ldisc_unlock(tty2);
			}
		} else
			ret = __tty_ldisc_lock(tty, timeout);
	}

	if (!ret)
		return -EBUSY;

	set_bit(TTY_LDISC_HALTED, &tty->flags);
	if (tty2)
		set_bit(TTY_LDISC_HALTED, &tty2->flags);
	return 0;
}

static void __lockfunc
tty_ldisc_lock_pair(struct tty_struct *tty, struct tty_struct *tty2)
{
	tty_ldisc_lock_pair_timeout(tty, tty2, MAX_SCHEDULE_TIMEOUT);
}

static void __lockfunc tty_ldisc_unlock_pair(struct tty_struct *tty,
					     struct tty_struct *tty2)
{
	__tty_ldisc_unlock(tty);
	if (tty2)
		__tty_ldisc_unlock(tty2);
}

/**
 *	tty_ldisc_flush	-	flush line discipline queue
 *	@tty: tty
 *
 *	Flush the line discipline queue (if any) and the tty flip buffers
 *	for this tty.
 */

void tty_ldisc_flush(struct tty_struct *tty)
{
	struct tty_ldisc *ld = tty_ldisc_ref(tty);

	tty_buffer_flush(tty, ld);
	if (ld)
		tty_ldisc_deref(ld);
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
 *	The line discipline-related tty_struct fields are reset to
 *	prevent the ldisc driver from re-using stale information for
 *	the new ldisc instance.
 *
 *	Locking: takes termios_rwsem
 */

static void tty_set_termios_ldisc(struct tty_struct *tty, int num)
{
	down_write(&tty->termios_rwsem);
	tty->termios.c_line = num;
	up_write(&tty->termios_rwsem);

	tty->disc_data = NULL;
	tty->receive_room = 0;
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

		tty_ldisc_debug(tty, "%p: opened\n", tty->ldisc);
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
	tty_ldisc_debug(tty, "%p: closed\n", tty->ldisc);
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
	struct tty_ldisc *new_ldisc;
	int r;

	/* There is an outstanding reference here so this is safe */
	old = tty_ldisc_get(tty, old->ops->num);
	WARN_ON(IS_ERR(old));
	tty->ldisc = old;
	tty_set_termios_ldisc(tty, old->ops->num);
	if (tty_ldisc_open(tty, old) < 0) {
		tty_ldisc_put(old);
		/* This driver is always present */
		new_ldisc = tty_ldisc_get(tty, N_TTY);
		if (IS_ERR(new_ldisc))
			panic("n_tty: get");
		tty->ldisc = new_ldisc;
		tty_set_termios_ldisc(tty, N_TTY);
		r = tty_ldisc_open(tty, new_ldisc);
		if (r < 0)
			panic("Couldn't open N_TTY ldisc for "
			      "%s --- error %d.",
			      tty_name(tty), r);
	}
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
 */

int tty_set_ldisc(struct tty_struct *tty, int ldisc)
{
	int retval;
	struct tty_ldisc *old_ldisc, *new_ldisc;

	new_ldisc = tty_ldisc_get(tty, ldisc);
	if (IS_ERR(new_ldisc))
		return PTR_ERR(new_ldisc);

	tty_lock(tty);
	retval = tty_ldisc_lock(tty, 5 * HZ);
	if (retval) {
		tty_ldisc_put(new_ldisc);
		tty_unlock(tty);
		return retval;
	}

	/*
	 *	Check the no-op case
	 */

	if (tty->ldisc->ops->num == ldisc) {
		tty_ldisc_unlock(tty);
		tty_ldisc_put(new_ldisc);
		tty_unlock(tty);
		return 0;
	}

	old_ldisc = tty->ldisc;

	if (test_bit(TTY_HUPPED, &tty->flags)) {
		/* We were raced by the hangup method. It will have stomped
		   the ldisc data and closed the ldisc down */
		tty_ldisc_unlock(tty);
		tty_ldisc_put(new_ldisc);
		tty_unlock(tty);
		return -EIO;
	}

	/* Shutdown the old discipline. */
	tty_ldisc_close(tty, old_ldisc);

	/* Now set up the new line discipline. */
	tty->ldisc = new_ldisc;
	tty_set_termios_ldisc(tty, ldisc);

	retval = tty_ldisc_open(tty, new_ldisc);
	if (retval < 0) {
		/* Back to the old one or N_TTY if we can't */
		tty_ldisc_put(new_ldisc);
		tty_ldisc_restore(tty, old_ldisc);
	}

	if (tty->ldisc->ops->num != old_ldisc->ops->num && tty->ops->set_ldisc) {
		down_read(&tty->termios_rwsem);
		tty->ops->set_ldisc(tty);
		up_read(&tty->termios_rwsem);
	}

	/* At this point we hold a reference to the new ldisc and a
	   reference to the old ldisc, or we hold two references to
	   the old ldisc (if it was restored as part of error cleanup
	   above). In either case, releasing a single reference from
	   the old ldisc is correct. */

	tty_ldisc_put(old_ldisc);

	/*
	 *	Allow ldisc referencing to occur again
	 */
	tty_ldisc_unlock(tty);

	/* Restart the work queue in case no characters kick it off. Safe if
	   already running */
	tty_buffer_restart_work(tty->port);

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
	down_write(&tty->termios_rwsem);
	tty->termios = tty->driver->init_termios;
	tty->termios.c_ispeed = tty_termios_input_baud_rate(&tty->termios);
	tty->termios.c_ospeed = tty_termios_baud_rate(&tty->termios);
	up_write(&tty->termios_rwsem);
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
	struct tty_ldisc *ld = tty_ldisc_get(tty, ldisc);

	if (IS_ERR(ld))
		return -1;

	tty_ldisc_close(tty, tty->ldisc);
	tty_ldisc_put(tty->ldisc);
	/*
	 *	Switch the line discipline back
	 */
	tty->ldisc = ld;
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

	tty_ldisc_debug(tty, "%p: closing\n", tty->ldisc);

	ld = tty_ldisc_ref(tty);
	if (ld != NULL) {
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

	wake_up_interruptible_poll(&tty->write_wait, POLLOUT);
	wake_up_interruptible_poll(&tty->read_wait, POLLIN);

	/*
	 * Shutdown the current line discipline, and reset it to
	 * N_TTY if need be.
	 *
	 * Avoid racing set_ldisc or tty_ldisc_release
	 */
	tty_ldisc_lock(tty, MAX_SCHEDULE_TIMEOUT);

	if (tty->ldisc) {

		/* At this point we have a halted ldisc; we want to close it and
		   reopen a new ldisc. We could defer the reopen to the next
		   open but it means auditing a lot of other paths so this is
		   a FIXME */
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
	}
	tty_ldisc_unlock(tty);
	if (reset)
		tty_reset_termios(tty);

	tty_ldisc_debug(tty, "%p: re-opened\n", tty->ldisc);
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
	}
	return 0;
}

static void tty_ldisc_kill(struct tty_struct *tty)
{
	/*
	 * Now kill off the ldisc
	 */
	tty_ldisc_close(tty, tty->ldisc);
	tty_ldisc_put(tty->ldisc);
	/* Force an oops if we mess this up */
	tty->ldisc = NULL;

	/* Ensure the next open requests the N_TTY ldisc */
	tty_set_termios_ldisc(tty, N_TTY);
}

/**
 *	tty_ldisc_release		-	release line discipline
 *	@tty: tty being shut down (or one end of pty pair)
 *
 *	Called during the final close of a tty or a pty pair in order to shut
 *	down the line discpline layer. On exit, each ldisc assigned is N_TTY and
 *	each ldisc has not been opened.
 */

void tty_ldisc_release(struct tty_struct *tty)
{
	struct tty_struct *o_tty = tty->link;

	/*
	 * Shutdown this line discipline. As this is the final close,
	 * it does not race with the set_ldisc code path.
	 */

	tty_ldisc_lock_pair(tty, o_tty);
	tty_ldisc_kill(tty);
	if (o_tty)
		tty_ldisc_kill(o_tty);
	tty_ldisc_unlock_pair(tty, o_tty);

	/* And the memory resources remaining (buffers, termios) will be
	   disposed of when the kref hits zero */

	tty_ldisc_debug(tty, "released\n");
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
	struct tty_ldisc *ld = tty_ldisc_get(tty, N_TTY);
	if (IS_ERR(ld))
		panic("n_tty: init_tty");
	tty->ldisc = ld;
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
	tty_ldisc_put(tty->ldisc);
	tty->ldisc = NULL;
}

void tty_ldisc_begin(void)
{
	/* Setup the default TTY line discipline. */
	(void) tty_register_ldisc(N_TTY, &tty_ldisc_N_TTY);
}
