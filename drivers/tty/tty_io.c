/*
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

/*
 * 'tty_io.c' gives an orthogonal feeling to tty's, be they consoles
 * or rs-channels. It also implements echoing, cooked mode etc.
 *
 * Kill-line thanks to John T Kohl, who also corrected VMIN = VTIME = 0.
 *
 * Modified by Theodore Ts'o, 9/14/92, to dynamically allocate the
 * tty_struct and tty_queue structures.  Previously there was an array
 * of 256 tty_struct's which was statically allocated, and the
 * tty_queue structures were allocated at boot time.  Both are now
 * dynamically allocated only when the tty is open.
 *
 * Also restructured routines so that there is more of a separation
 * between the high-level tty routines (tty_io.c and tty_ioctl.c) and
 * the low-level tty routines (serial.c, pty.c, console.c).  This
 * makes for cleaner and more compact code.  -TYT, 9/17/92
 *
 * Modified by Fred N. van Kempen, 01/29/93, to add line disciplines
 * which can be dynamically activated and de-activated by the line
 * discipline handling modules (like SLIP).
 *
 * NOTE: pay no attention to the line discipline code (yet); its
 * interface is still subject to change in this version...
 * -- TYT, 1/31/92
 *
 * Added functionality to the OPOST tty handling.  No delays, but all
 * other bits should be there.
 *	-- Nick Holloway <alfie@dcs.warwick.ac.uk>, 27th May 1993.
 *
 * Rewrote canonical mode and added more termios flags.
 * 	-- julian@uhunix.uhcc.hawaii.edu (J. Cowley), 13Jan94
 *
 * Reorganized FASYNC support so mouse code can share it.
 *	-- ctm@ardi.com, 9Sep95
 *
 * New TIOCLINUX variants added.
 *	-- mj@k332.feld.cvut.cz, 19-Nov-95
 *
 * Restrict vt switching via ioctl()
 *      -- grif@cs.ucr.edu, 5-Dec-95
 *
 * Move console and virtual terminal code to more appropriate files,
 * implement CONFIG_VT and generalize console device interface.
 *	-- Marko Kohtala <Marko.Kohtala@hut.fi>, March 97
 *
 * Rewrote tty_init_dev and tty_release_dev to eliminate races.
 *	-- Bill Hawes <whawes@star.net>, June 97
 *
 * Added devfs support.
 *      -- C. Scott Ananian <cananian@alumni.princeton.edu>, 13-Jan-1998
 *
 * Added support for a Unix98-style ptmx device.
 *      -- C. Scott Ananian <cananian@alumni.princeton.edu>, 14-Jan-1998
 *
 * Reduced memory usage for older ARM systems
 *      -- Russell King <rmk@arm.linux.org.uk>
 *
 * Move do_SAK() into process context.  Less stack use in devfs functions.
 * alloc_tty_struct() always uses kmalloc()
 *			 -- Andrew Morton <andrewm@uow.edu.eu> 17Mar01
 */

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
#include <linux/fdtable.h>
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
#include <linux/serial.h>
#include <linux/ratelimit.h>

#include <linux/uaccess.h>

#include <linux/kbd_kern.h>
#include <linux/vt_kern.h>
#include <linux/selection.h>

#include <linux/kmod.h>
#include <linux/nsproxy.h>

#undef TTY_DEBUG_HANGUP
#ifdef TTY_DEBUG_HANGUP
# define tty_debug_hangup(tty, f, args...)	tty_debug(tty, f, ##args)
#else
# define tty_debug_hangup(tty, f, args...)	do { } while (0)
#endif

#define TTY_PARANOIA_CHECK 1
#define CHECK_TTY_COUNT 1

struct ktermios tty_std_termios = {	/* for the benefit of tty drivers  */
	.c_iflag = ICRNL | IXON,
	.c_oflag = OPOST | ONLCR,
	.c_cflag = B38400 | CS8 | CREAD | HUPCL,
	.c_lflag = ISIG | ICANON | ECHO | ECHOE | ECHOK |
		   ECHOCTL | ECHOKE | IEXTEN,
	.c_cc = INIT_C_CC,
	.c_ispeed = 38400,
	.c_ospeed = 38400
};

EXPORT_SYMBOL(tty_std_termios);

/* This list gets poked at by procfs and various bits of boot up code. This
   could do with some rationalisation such as pulling the tty proc function
   into this file */

LIST_HEAD(tty_drivers);			/* linked list of tty drivers */

/* Mutex to protect creating and releasing a tty. This is shared with
   vt.c for deeply disgusting hack reasons */
DEFINE_MUTEX(tty_mutex);
EXPORT_SYMBOL(tty_mutex);

/* Spinlock to protect the tty->tty_files list */
DEFINE_SPINLOCK(tty_files_lock);

static ssize_t tty_read(struct file *, char __user *, size_t, loff_t *);
static ssize_t tty_write(struct file *, const char __user *, size_t, loff_t *);
ssize_t redirected_tty_write(struct file *, const char __user *,
							size_t, loff_t *);
static unsigned int tty_poll(struct file *, poll_table *);
static int tty_open(struct inode *, struct file *);
long tty_ioctl(struct file *file, unsigned int cmd, unsigned long arg);
#ifdef CONFIG_COMPAT
static long tty_compat_ioctl(struct file *file, unsigned int cmd,
				unsigned long arg);
#else
#define tty_compat_ioctl NULL
#endif
static int __tty_fasync(int fd, struct file *filp, int on);
static int tty_fasync(int fd, struct file *filp, int on);
static void release_tty(struct tty_struct *tty, int idx);

/**
 *	free_tty_struct		-	free a disused tty
 *	@tty: tty struct to free
 *
 *	Free the write buffers, tty queue and tty memory itself.
 *
 *	Locking: none. Must be called after tty is definitely unused
 */

void free_tty_struct(struct tty_struct *tty)
{
	if (!tty)
		return;
	put_device(tty->dev);
	kfree(tty->write_buf);
	tty->magic = 0xDEADDEAD;
	kfree(tty);
}

static inline struct tty_struct *file_tty(struct file *file)
{
	return ((struct tty_file_private *)file->private_data)->tty;
}

int tty_alloc_file(struct file *file)
{
	struct tty_file_private *priv;

	priv = kmalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	file->private_data = priv;

	return 0;
}

/* Associate a new file with the tty structure */
void tty_add_file(struct tty_struct *tty, struct file *file)
{
	struct tty_file_private *priv = file->private_data;

	priv->tty = tty;
	priv->file = file;

	spin_lock(&tty_files_lock);
	list_add(&priv->list, &tty->tty_files);
	spin_unlock(&tty_files_lock);
}

/**
 * tty_free_file - free file->private_data
 *
 * This shall be used only for fail path handling when tty_add_file was not
 * called yet.
 */
void tty_free_file(struct file *file)
{
	struct tty_file_private *priv = file->private_data;

	file->private_data = NULL;
	kfree(priv);
}

/* Delete file from its tty */
static void tty_del_file(struct file *file)
{
	struct tty_file_private *priv = file->private_data;

	spin_lock(&tty_files_lock);
	list_del(&priv->list);
	spin_unlock(&tty_files_lock);
	tty_free_file(file);
}


#define TTY_NUMBER(tty) ((tty)->index + (tty)->driver->name_base)

/**
 *	tty_name	-	return tty naming
 *	@tty: tty structure
 *
 *	Convert a tty structure into a name. The name reflects the kernel
 *	naming policy and if udev is in use may not reflect user space
 *
 *	Locking: none
 */

const char *tty_name(const struct tty_struct *tty)
{
	if (!tty) /* Hmm.  NULL pointer.  That's fun. */
		return "NULL tty";
	return tty->name;
}

EXPORT_SYMBOL(tty_name);

int tty_paranoia_check(struct tty_struct *tty, struct inode *inode,
			      const char *routine)
{
#ifdef TTY_PARANOIA_CHECK
	if (!tty) {
		printk(KERN_WARNING
			"null TTY for (%d:%d) in %s\n",
			imajor(inode), iminor(inode), routine);
		return 1;
	}
	if (tty->magic != TTY_MAGIC) {
		printk(KERN_WARNING
			"bad magic number for tty struct (%d:%d) in %s\n",
			imajor(inode), iminor(inode), routine);
		return 1;
	}
#endif
	return 0;
}

/* Caller must hold tty_lock */
static int check_tty_count(struct tty_struct *tty, const char *routine)
{
#ifdef CHECK_TTY_COUNT
	struct list_head *p;
	int count = 0;

	spin_lock(&tty_files_lock);
	list_for_each(p, &tty->tty_files) {
		count++;
	}
	spin_unlock(&tty_files_lock);
	if (tty->driver->type == TTY_DRIVER_TYPE_PTY &&
	    tty->driver->subtype == PTY_TYPE_SLAVE &&
	    tty->link && tty->link->count)
		count++;
	if (tty->count != count) {
		printk(KERN_WARNING "Warning: dev (%s) tty->count(%d) "
				    "!= #fd's(%d) in %s\n",
		       tty->name, tty->count, count, routine);
		return count;
	}
#endif
	return 0;
}

/**
 *	get_tty_driver		-	find device of a tty
 *	@dev_t: device identifier
 *	@index: returns the index of the tty
 *
 *	This routine returns a tty driver structure, given a device number
 *	and also passes back the index number.
 *
 *	Locking: caller must hold tty_mutex
 */

static struct tty_driver *get_tty_driver(dev_t device, int *index)
{
	struct tty_driver *p;

	list_for_each_entry(p, &tty_drivers, tty_drivers) {
		dev_t base = MKDEV(p->major, p->minor_start);
		if (device < base || device >= base + p->num)
			continue;
		*index = device - base;
		return tty_driver_kref_get(p);
	}
	return NULL;
}

#ifdef CONFIG_CONSOLE_POLL

/**
 *	tty_find_polling_driver	-	find device of a polled tty
 *	@name: name string to match
 *	@line: pointer to resulting tty line nr
 *
 *	This routine returns a tty driver structure, given a name
 *	and the condition that the tty driver is capable of polled
 *	operation.
 */
struct tty_driver *tty_find_polling_driver(char *name, int *line)
{
	struct tty_driver *p, *res = NULL;
	int tty_line = 0;
	int len;
	char *str, *stp;

	for (str = name; *str; str++)
		if ((*str >= '0' && *str <= '9') || *str == ',')
			break;
	if (!*str)
		return NULL;

	len = str - name;
	tty_line = simple_strtoul(str, &str, 10);

	mutex_lock(&tty_mutex);
	/* Search through the tty devices to look for a match */
	list_for_each_entry(p, &tty_drivers, tty_drivers) {
		if (strncmp(name, p->name, len) != 0)
			continue;
		stp = str;
		if (*stp == ',')
			stp++;
		if (*stp == '\0')
			stp = NULL;

		if (tty_line >= 0 && tty_line < p->num && p->ops &&
		    p->ops->poll_init && !p->ops->poll_init(p, tty_line, stp)) {
			res = tty_driver_kref_get(p);
			*line = tty_line;
			break;
		}
	}
	mutex_unlock(&tty_mutex);

	return res;
}
EXPORT_SYMBOL_GPL(tty_find_polling_driver);
#endif

/**
 *	tty_check_change	-	check for POSIX terminal changes
 *	@tty: tty to check
 *
 *	If we try to write to, or set the state of, a terminal and we're
 *	not in the foreground, send a SIGTTOU.  If the signal is blocked or
 *	ignored, go ahead and perform the operation.  (POSIX 7.2)
 *
 *	Locking: ctrl_lock
 */

int __tty_check_change(struct tty_struct *tty, int sig)
{
	unsigned long flags;
	struct pid *pgrp, *tty_pgrp;
	int ret = 0;

	if (current->signal->tty != tty)
		return 0;

	rcu_read_lock();
	pgrp = task_pgrp(current);

	spin_lock_irqsave(&tty->ctrl_lock, flags);
	tty_pgrp = tty->pgrp;
	spin_unlock_irqrestore(&tty->ctrl_lock, flags);

	if (tty_pgrp && pgrp != tty->pgrp) {
		if (is_ignored(sig)) {
			if (sig == SIGTTIN)
				ret = -EIO;
		} else if (is_current_pgrp_orphaned())
			ret = -EIO;
		else {
			kill_pgrp(pgrp, sig, 1);
			set_thread_flag(TIF_SIGPENDING);
			ret = -ERESTARTSYS;
		}
	}
	rcu_read_unlock();

	if (!tty_pgrp) {
		pr_warn("%s: tty_check_change: sig=%d, tty->pgrp == NULL!\n",
			tty_name(tty), sig);
	}

	return ret;
}

int tty_check_change(struct tty_struct *tty)
{
	return __tty_check_change(tty, SIGTTOU);
}
EXPORT_SYMBOL(tty_check_change);

static ssize_t hung_up_tty_read(struct file *file, char __user *buf,
				size_t count, loff_t *ppos)
{
	return 0;
}

static ssize_t hung_up_tty_write(struct file *file, const char __user *buf,
				 size_t count, loff_t *ppos)
{
	return -EIO;
}

/* No kernel lock held - none needed ;) */
static unsigned int hung_up_tty_poll(struct file *filp, poll_table *wait)
{
	return POLLIN | POLLOUT | POLLERR | POLLHUP | POLLRDNORM | POLLWRNORM;
}

static long hung_up_tty_ioctl(struct file *file, unsigned int cmd,
		unsigned long arg)
{
	return cmd == TIOCSPGRP ? -ENOTTY : -EIO;
}

static long hung_up_tty_compat_ioctl(struct file *file,
				     unsigned int cmd, unsigned long arg)
{
	return cmd == TIOCSPGRP ? -ENOTTY : -EIO;
}

static const struct file_operations tty_fops = {
	.llseek		= no_llseek,
	.read		= tty_read,
	.write		= tty_write,
	.poll		= tty_poll,
	.unlocked_ioctl	= tty_ioctl,
	.compat_ioctl	= tty_compat_ioctl,
	.open		= tty_open,
	.release	= tty_release,
	.fasync		= tty_fasync,
};

static const struct file_operations console_fops = {
	.llseek		= no_llseek,
	.read		= tty_read,
	.write		= redirected_tty_write,
	.poll		= tty_poll,
	.unlocked_ioctl	= tty_ioctl,
	.compat_ioctl	= tty_compat_ioctl,
	.open		= tty_open,
	.release	= tty_release,
	.fasync		= tty_fasync,
};

static const struct file_operations hung_up_tty_fops = {
	.llseek		= no_llseek,
	.read		= hung_up_tty_read,
	.write		= hung_up_tty_write,
	.poll		= hung_up_tty_poll,
	.unlocked_ioctl	= hung_up_tty_ioctl,
	.compat_ioctl	= hung_up_tty_compat_ioctl,
	.release	= tty_release,
};

static DEFINE_SPINLOCK(redirect_lock);
static struct file *redirect;


void proc_clear_tty(struct task_struct *p)
{
	unsigned long flags;
	struct tty_struct *tty;
	spin_lock_irqsave(&p->sighand->siglock, flags);
	tty = p->signal->tty;
	p->signal->tty = NULL;
	spin_unlock_irqrestore(&p->sighand->siglock, flags);
	tty_kref_put(tty);
}

/**
 * proc_set_tty -  set the controlling terminal
 *
 * Only callable by the session leader and only if it does not already have
 * a controlling terminal.
 *
 * Caller must hold:  tty_lock()
 *		      a readlock on tasklist_lock
 *		      sighand lock
 */
static void __proc_set_tty(struct tty_struct *tty)
{
	unsigned long flags;

	spin_lock_irqsave(&tty->ctrl_lock, flags);
	/*
	 * The session and fg pgrp references will be non-NULL if
	 * tiocsctty() is stealing the controlling tty
	 */
	put_pid(tty->session);
	put_pid(tty->pgrp);
	tty->pgrp = get_pid(task_pgrp(current));
	spin_unlock_irqrestore(&tty->ctrl_lock, flags);
	tty->session = get_pid(task_session(current));
	if (current->signal->tty) {
		tty_debug(tty, "current tty %s not NULL!!\n",
			  current->signal->tty->name);
		tty_kref_put(current->signal->tty);
	}
	put_pid(current->signal->tty_old_pgrp);
	current->signal->tty = tty_kref_get(tty);
	current->signal->tty_old_pgrp = NULL;
}

static void proc_set_tty(struct tty_struct *tty)
{
	spin_lock_irq(&current->sighand->siglock);
	__proc_set_tty(tty);
	spin_unlock_irq(&current->sighand->siglock);
}

struct tty_struct *get_current_tty(void)
{
	struct tty_struct *tty;
	unsigned long flags;

	spin_lock_irqsave(&current->sighand->siglock, flags);
	tty = tty_kref_get(current->signal->tty);
	spin_unlock_irqrestore(&current->sighand->siglock, flags);
	return tty;
}
EXPORT_SYMBOL_GPL(get_current_tty);

static void session_clear_tty(struct pid *session)
{
	struct task_struct *p;
	do_each_pid_task(session, PIDTYPE_SID, p) {
		proc_clear_tty(p);
	} while_each_pid_task(session, PIDTYPE_SID, p);
}

/**
 *	tty_wakeup	-	request more data
 *	@tty: terminal
 *
 *	Internal and external helper for wakeups of tty. This function
 *	informs the line discipline if present that the driver is ready
 *	to receive more output data.
 */

void tty_wakeup(struct tty_struct *tty)
{
	struct tty_ldisc *ld;

	if (test_bit(TTY_DO_WRITE_WAKEUP, &tty->flags)) {
		ld = tty_ldisc_ref(tty);
		if (ld) {
			if (ld->ops->write_wakeup)
				ld->ops->write_wakeup(tty);
			tty_ldisc_deref(ld);
		}
	}
	wake_up_interruptible_poll(&tty->write_wait, POLLOUT);
}

EXPORT_SYMBOL_GPL(tty_wakeup);

/**
 *	tty_signal_session_leader	- sends SIGHUP to session leader
 *	@tty		controlling tty
 *	@exit_session	if non-zero, signal all foreground group processes
 *
 *	Send SIGHUP and SIGCONT to the session leader and its process group.
 *	Optionally, signal all processes in the foreground process group.
 *
 *	Returns the number of processes in the session with this tty
 *	as their controlling terminal. This value is used to drop
 *	tty references for those processes.
 */
static int tty_signal_session_leader(struct tty_struct *tty, int exit_session)
{
	struct task_struct *p;
	int refs = 0;
	struct pid *tty_pgrp = NULL;

	read_lock(&tasklist_lock);
	if (tty->session) {
		do_each_pid_task(tty->session, PIDTYPE_SID, p) {
			spin_lock_irq(&p->sighand->siglock);
			if (p->signal->tty == tty) {
				p->signal->tty = NULL;
				/* We defer the dereferences outside fo
				   the tasklist lock */
				refs++;
			}
			if (!p->signal->leader) {
				spin_unlock_irq(&p->sighand->siglock);
				continue;
			}
			__group_send_sig_info(SIGHUP, SEND_SIG_PRIV, p);
			__group_send_sig_info(SIGCONT, SEND_SIG_PRIV, p);
			put_pid(p->signal->tty_old_pgrp);  /* A noop */
			spin_lock(&tty->ctrl_lock);
			tty_pgrp = get_pid(tty->pgrp);
			if (tty->pgrp)
				p->signal->tty_old_pgrp = get_pid(tty->pgrp);
			spin_unlock(&tty->ctrl_lock);
			spin_unlock_irq(&p->sighand->siglock);
		} while_each_pid_task(tty->session, PIDTYPE_SID, p);
	}
	read_unlock(&tasklist_lock);

	if (tty_pgrp) {
		if (exit_session)
			kill_pgrp(tty_pgrp, SIGHUP, exit_session);
		put_pid(tty_pgrp);
	}

	return refs;
}

/**
 *	__tty_hangup		-	actual handler for hangup events
 *	@work: tty device
 *
 *	This can be called by a "kworker" kernel thread.  That is process
 *	synchronous but doesn't hold any locks, so we need to make sure we
 *	have the appropriate locks for what we're doing.
 *
 *	The hangup event clears any pending redirections onto the hung up
 *	device. It ensures future writes will error and it does the needed
 *	line discipline hangup and signal delivery. The tty object itself
 *	remains intact.
 *
 *	Locking:
 *		BTM
 *		  redirect lock for undoing redirection
 *		  file list lock for manipulating list of ttys
 *		  tty_ldiscs_lock from called functions
 *		  termios_rwsem resetting termios data
 *		  tasklist_lock to walk task list for hangup event
 *		    ->siglock to protect ->signal/->sighand
 */
static void __tty_hangup(struct tty_struct *tty, int exit_session)
{
	struct file *cons_filp = NULL;
	struct file *filp, *f = NULL;
	struct tty_file_private *priv;
	int    closecount = 0, n;
	int refs;

	if (!tty)
		return;


	spin_lock(&redirect_lock);
	if (redirect && file_tty(redirect) == tty) {
		f = redirect;
		redirect = NULL;
	}
	spin_unlock(&redirect_lock);

	tty_lock(tty);

	if (test_bit(TTY_HUPPED, &tty->flags)) {
		tty_unlock(tty);
		return;
	}

	/* inuse_filps is protected by the single tty lock,
	   this really needs to change if we want to flush the
	   workqueue with the lock held */
	check_tty_count(tty, "tty_hangup");

	spin_lock(&tty_files_lock);
	/* This breaks for file handles being sent over AF_UNIX sockets ? */
	list_for_each_entry(priv, &tty->tty_files, list) {
		filp = priv->file;
		if (filp->f_op->write == redirected_tty_write)
			cons_filp = filp;
		if (filp->f_op->write != tty_write)
			continue;
		closecount++;
		__tty_fasync(-1, filp, 0);	/* can't block */
		filp->f_op = &hung_up_tty_fops;
	}
	spin_unlock(&tty_files_lock);

	refs = tty_signal_session_leader(tty, exit_session);
	/* Account for the p->signal references we killed */
	while (refs--)
		tty_kref_put(tty);

	tty_ldisc_hangup(tty);

	spin_lock_irq(&tty->ctrl_lock);
	clear_bit(TTY_THROTTLED, &tty->flags);
	clear_bit(TTY_DO_WRITE_WAKEUP, &tty->flags);
	put_pid(tty->session);
	put_pid(tty->pgrp);
	tty->session = NULL;
	tty->pgrp = NULL;
	tty->ctrl_status = 0;
	spin_unlock_irq(&tty->ctrl_lock);

	/*
	 * If one of the devices matches a console pointer, we
	 * cannot just call hangup() because that will cause
	 * tty->count and state->count to go out of sync.
	 * So we just call close() the right number of times.
	 */
	if (cons_filp) {
		if (tty->ops->close)
			for (n = 0; n < closecount; n++)
				tty->ops->close(tty, cons_filp);
	} else if (tty->ops->hangup)
		tty->ops->hangup(tty);
	/*
	 * We don't want to have driver/ldisc interactions beyond
	 * the ones we did here. The driver layer expects no
	 * calls after ->hangup() from the ldisc side. However we
	 * can't yet guarantee all that.
	 */
	set_bit(TTY_HUPPED, &tty->flags);
	tty_unlock(tty);

	if (f)
		fput(f);
}

static void do_tty_hangup(struct work_struct *work)
{
	struct tty_struct *tty =
		container_of(work, struct tty_struct, hangup_work);

	__tty_hangup(tty, 0);
}

/**
 *	tty_hangup		-	trigger a hangup event
 *	@tty: tty to hangup
 *
 *	A carrier loss (virtual or otherwise) has occurred on this like
 *	schedule a hangup sequence to run after this event.
 */

void tty_hangup(struct tty_struct *tty)
{
	tty_debug_hangup(tty, "\n");
	schedule_work(&tty->hangup_work);
}

EXPORT_SYMBOL(tty_hangup);

/**
 *	tty_vhangup		-	process vhangup
 *	@tty: tty to hangup
 *
 *	The user has asked via system call for the terminal to be hung up.
 *	We do this synchronously so that when the syscall returns the process
 *	is complete. That guarantee is necessary for security reasons.
 */

void tty_vhangup(struct tty_struct *tty)
{
	tty_debug_hangup(tty, "\n");
	__tty_hangup(tty, 0);
}

EXPORT_SYMBOL(tty_vhangup);


/**
 *	tty_vhangup_self	-	process vhangup for own ctty
 *
 *	Perform a vhangup on the current controlling tty
 */

void tty_vhangup_self(void)
{
	struct tty_struct *tty;

	tty = get_current_tty();
	if (tty) {
		tty_vhangup(tty);
		tty_kref_put(tty);
	}
}

/**
 *	tty_vhangup_session		-	hangup session leader exit
 *	@tty: tty to hangup
 *
 *	The session leader is exiting and hanging up its controlling terminal.
 *	Every process in the foreground process group is signalled SIGHUP.
 *
 *	We do this synchronously so that when the syscall returns the process
 *	is complete. That guarantee is necessary for security reasons.
 */

static void tty_vhangup_session(struct tty_struct *tty)
{
	tty_debug_hangup(tty, "\n");
	__tty_hangup(tty, 1);
}

/**
 *	tty_hung_up_p		-	was tty hung up
 *	@filp: file pointer of tty
 *
 *	Return true if the tty has been subject to a vhangup or a carrier
 *	loss
 */

int tty_hung_up_p(struct file *filp)
{
	return (filp->f_op == &hung_up_tty_fops);
}

EXPORT_SYMBOL(tty_hung_up_p);

/**
 *	disassociate_ctty	-	disconnect controlling tty
 *	@on_exit: true if exiting so need to "hang up" the session
 *
 *	This function is typically called only by the session leader, when
 *	it wants to disassociate itself from its controlling tty.
 *
 *	It performs the following functions:
 * 	(1)  Sends a SIGHUP and SIGCONT to the foreground process group
 * 	(2)  Clears the tty from being controlling the session
 * 	(3)  Clears the controlling tty for all processes in the
 * 		session group.
 *
 *	The argument on_exit is set to 1 if called when a process is
 *	exiting; it is 0 if called by the ioctl TIOCNOTTY.
 *
 *	Locking:
 *		BTM is taken for hysterical raisins, and held when
 *		  called from no_tty().
 *		  tty_mutex is taken to protect tty
 *		  ->siglock is taken to protect ->signal/->sighand
 *		  tasklist_lock is taken to walk process list for sessions
 *		    ->siglock is taken to protect ->signal/->sighand
 */

void disassociate_ctty(int on_exit)
{
	struct tty_struct *tty;

	if (!current->signal->leader)
		return;

	tty = get_current_tty();
	if (tty) {
		if (on_exit && tty->driver->type != TTY_DRIVER_TYPE_PTY) {
			tty_vhangup_session(tty);
		} else {
			struct pid *tty_pgrp = tty_get_pgrp(tty);
			if (tty_pgrp) {
				kill_pgrp(tty_pgrp, SIGHUP, on_exit);
				if (!on_exit)
					kill_pgrp(tty_pgrp, SIGCONT, on_exit);
				put_pid(tty_pgrp);
			}
		}
		tty_kref_put(tty);

	} else if (on_exit) {
		struct pid *old_pgrp;
		spin_lock_irq(&current->sighand->siglock);
		old_pgrp = current->signal->tty_old_pgrp;
		current->signal->tty_old_pgrp = NULL;
		spin_unlock_irq(&current->sighand->siglock);
		if (old_pgrp) {
			kill_pgrp(old_pgrp, SIGHUP, on_exit);
			kill_pgrp(old_pgrp, SIGCONT, on_exit);
			put_pid(old_pgrp);
		}
		return;
	}

	spin_lock_irq(&current->sighand->siglock);
	put_pid(current->signal->tty_old_pgrp);
	current->signal->tty_old_pgrp = NULL;

	tty = tty_kref_get(current->signal->tty);
	if (tty) {
		unsigned long flags;
		spin_lock_irqsave(&tty->ctrl_lock, flags);
		put_pid(tty->session);
		put_pid(tty->pgrp);
		tty->session = NULL;
		tty->pgrp = NULL;
		spin_unlock_irqrestore(&tty->ctrl_lock, flags);
		tty_kref_put(tty);
	} else
		tty_debug_hangup(tty, "no current tty\n");

	spin_unlock_irq(&current->sighand->siglock);
	/* Now clear signal->tty under the lock */
	read_lock(&tasklist_lock);
	session_clear_tty(task_session(current));
	read_unlock(&tasklist_lock);
}

/**
 *
 *	no_tty	- Ensure the current process does not have a controlling tty
 */
void no_tty(void)
{
	/* FIXME: Review locking here. The tty_lock never covered any race
	   between a new association and proc_clear_tty but possible we need
	   to protect against this anyway */
	struct task_struct *tsk = current;
	disassociate_ctty(0);
	proc_clear_tty(tsk);
}


/**
 *	stop_tty	-	propagate flow control
 *	@tty: tty to stop
 *
 *	Perform flow control to the driver. May be called
 *	on an already stopped device and will not re-call the driver
 *	method.
 *
 *	This functionality is used by both the line disciplines for
 *	halting incoming flow and by the driver. It may therefore be
 *	called from any context, may be under the tty atomic_write_lock
 *	but not always.
 *
 *	Locking:
 *		flow_lock
 */

void __stop_tty(struct tty_struct *tty)
{
	if (tty->stopped)
		return;
	tty->stopped = 1;
	if (tty->ops->stop)
		tty->ops->stop(tty);
}

void stop_tty(struct tty_struct *tty)
{
	unsigned long flags;

	spin_lock_irqsave(&tty->flow_lock, flags);
	__stop_tty(tty);
	spin_unlock_irqrestore(&tty->flow_lock, flags);
}
EXPORT_SYMBOL(stop_tty);

/**
 *	start_tty	-	propagate flow control
 *	@tty: tty to start
 *
 *	Start a tty that has been stopped if at all possible. If this
 *	tty was previous stopped and is now being started, the driver
 *	start method is invoked and the line discipline woken.
 *
 *	Locking:
 *		flow_lock
 */

void __start_tty(struct tty_struct *tty)
{
	if (!tty->stopped || tty->flow_stopped)
		return;
	tty->stopped = 0;
	if (tty->ops->start)
		tty->ops->start(tty);
	tty_wakeup(tty);
}

void start_tty(struct tty_struct *tty)
{
	unsigned long flags;

	spin_lock_irqsave(&tty->flow_lock, flags);
	__start_tty(tty);
	spin_unlock_irqrestore(&tty->flow_lock, flags);
}
EXPORT_SYMBOL(start_tty);

static void tty_update_time(struct timespec *time)
{
	unsigned long sec = get_seconds();

	/*
	 * We only care if the two values differ in anything other than the
	 * lower three bits (i.e every 8 seconds).  If so, then we can update
	 * the time of the tty device, otherwise it could be construded as a
	 * security leak to let userspace know the exact timing of the tty.
	 */
	if ((sec ^ time->tv_sec) & ~7)
		time->tv_sec = sec;
}

/**
 *	tty_read	-	read method for tty device files
 *	@file: pointer to tty file
 *	@buf: user buffer
 *	@count: size of user buffer
 *	@ppos: unused
 *
 *	Perform the read system call function on this terminal device. Checks
 *	for hung up devices before calling the line discipline method.
 *
 *	Locking:
 *		Locks the line discipline internally while needed. Multiple
 *	read calls may be outstanding in parallel.
 */

static ssize_t tty_read(struct file *file, char __user *buf, size_t count,
			loff_t *ppos)
{
	int i;
	struct inode *inode = file_inode(file);
	struct tty_struct *tty = file_tty(file);
	struct tty_ldisc *ld;

	if (tty_paranoia_check(tty, inode, "tty_read"))
		return -EIO;
	if (!tty || (test_bit(TTY_IO_ERROR, &tty->flags)))
		return -EIO;

	/* We want to wait for the line discipline to sort out in this
	   situation */
	ld = tty_ldisc_ref_wait(tty);
	if (ld->ops->read)
		i = ld->ops->read(tty, file, buf, count);
	else
		i = -EIO;
	tty_ldisc_deref(ld);

	if (i > 0)
		tty_update_time(&inode->i_atime);

	return i;
}

static void tty_write_unlock(struct tty_struct *tty)
{
	mutex_unlock(&tty->atomic_write_lock);
	wake_up_interruptible_poll(&tty->write_wait, POLLOUT);
}

static int tty_write_lock(struct tty_struct *tty, int ndelay)
{
	if (!mutex_trylock(&tty->atomic_write_lock)) {
		if (ndelay)
			return -EAGAIN;
		if (mutex_lock_interruptible(&tty->atomic_write_lock))
			return -ERESTARTSYS;
	}
	return 0;
}

/*
 * Split writes up in sane blocksizes to avoid
 * denial-of-service type attacks
 */
static inline ssize_t do_tty_write(
	ssize_t (*write)(struct tty_struct *, struct file *, const unsigned char *, size_t),
	struct tty_struct *tty,
	struct file *file,
	const char __user *buf,
	size_t count)
{
	ssize_t ret, written = 0;
	unsigned int chunk;

	ret = tty_write_lock(tty, file->f_flags & O_NDELAY);
	if (ret < 0)
		return ret;

	/*
	 * We chunk up writes into a temporary buffer. This
	 * simplifies low-level drivers immensely, since they
	 * don't have locking issues and user mode accesses.
	 *
	 * But if TTY_NO_WRITE_SPLIT is set, we should use a
	 * big chunk-size..
	 *
	 * The default chunk-size is 2kB, because the NTTY
	 * layer has problems with bigger chunks. It will
	 * claim to be able to handle more characters than
	 * it actually does.
	 *
	 * FIXME: This can probably go away now except that 64K chunks
	 * are too likely to fail unless switched to vmalloc...
	 */
	chunk = 2048;
	if (test_bit(TTY_NO_WRITE_SPLIT, &tty->flags))
		chunk = 65536;
	if (count < chunk)
		chunk = count;

	/* write_buf/write_cnt is protected by the atomic_write_lock mutex */
	if (tty->write_cnt < chunk) {
		unsigned char *buf_chunk;

		if (chunk < 1024)
			chunk = 1024;

		buf_chunk = kmalloc(chunk, GFP_KERNEL);
		if (!buf_chunk) {
			ret = -ENOMEM;
			goto out;
		}
		kfree(tty->write_buf);
		tty->write_cnt = chunk;
		tty->write_buf = buf_chunk;
	}

	/* Do the write .. */
	for (;;) {
		size_t size = count;
		if (size > chunk)
			size = chunk;
		ret = -EFAULT;
		if (copy_from_user(tty->write_buf, buf, size))
			break;
		ret = write(tty, file, tty->write_buf, size);
		if (ret <= 0)
			break;
		written += ret;
		buf += ret;
		count -= ret;
		if (!count)
			break;
		ret = -ERESTARTSYS;
		if (signal_pending(current))
			break;
		cond_resched();
	}
	if (written) {
		tty_update_time(&file_inode(file)->i_mtime);
		ret = written;
	}
out:
	tty_write_unlock(tty);
	return ret;
}

/**
 * tty_write_message - write a message to a certain tty, not just the console.
 * @tty: the destination tty_struct
 * @msg: the message to write
 *
 * This is used for messages that need to be redirected to a specific tty.
 * We don't put it into the syslog queue right now maybe in the future if
 * really needed.
 *
 * We must still hold the BTM and test the CLOSING flag for the moment.
 */

void tty_write_message(struct tty_struct *tty, char *msg)
{
	if (tty) {
		mutex_lock(&tty->atomic_write_lock);
		tty_lock(tty);
		if (tty->ops->write && tty->count > 0)
			tty->ops->write(tty, msg, strlen(msg));
		tty_unlock(tty);
		tty_write_unlock(tty);
	}
	return;
}


/**
 *	tty_write		-	write method for tty device file
 *	@file: tty file pointer
 *	@buf: user data to write
 *	@count: bytes to write
 *	@ppos: unused
 *
 *	Write data to a tty device via the line discipline.
 *
 *	Locking:
 *		Locks the line discipline as required
 *		Writes to the tty driver are serialized by the atomic_write_lock
 *	and are then processed in chunks to the device. The line discipline
 *	write method will not be invoked in parallel for each device.
 */

static ssize_t tty_write(struct file *file, const char __user *buf,
						size_t count, loff_t *ppos)
{
	struct tty_struct *tty = file_tty(file);
 	struct tty_ldisc *ld;
	ssize_t ret;

	if (tty_paranoia_check(tty, file_inode(file), "tty_write"))
		return -EIO;
	if (!tty || !tty->ops->write ||
		(test_bit(TTY_IO_ERROR, &tty->flags)))
			return -EIO;
	/* Short term debug to catch buggy drivers */
	if (tty->ops->write_room == NULL)
		printk(KERN_ERR "tty driver %s lacks a write_room method.\n",
			tty->driver->name);
	ld = tty_ldisc_ref_wait(tty);
	if (!ld->ops->write)
		ret = -EIO;
	else
		ret = do_tty_write(ld->ops->write, tty, file, buf, count);
	tty_ldisc_deref(ld);
	return ret;
}

ssize_t redirected_tty_write(struct file *file, const char __user *buf,
						size_t count, loff_t *ppos)
{
	struct file *p = NULL;

	spin_lock(&redirect_lock);
	if (redirect)
		p = get_file(redirect);
	spin_unlock(&redirect_lock);

	if (p) {
		ssize_t res;
		res = vfs_write(p, buf, count, &p->f_pos);
		fput(p);
		return res;
	}
	return tty_write(file, buf, count, ppos);
}

/**
 *	tty_send_xchar	-	send priority character
 *
 *	Send a high priority character to the tty even if stopped
 *
 *	Locking: none for xchar method, write ordering for write method.
 */

int tty_send_xchar(struct tty_struct *tty, char ch)
{
	int	was_stopped = tty->stopped;

	if (tty->ops->send_xchar) {
		down_read(&tty->termios_rwsem);
		tty->ops->send_xchar(tty, ch);
		up_read(&tty->termios_rwsem);
		return 0;
	}

	if (tty_write_lock(tty, 0) < 0)
		return -ERESTARTSYS;

	down_read(&tty->termios_rwsem);
	if (was_stopped)
		start_tty(tty);
	tty->ops->write(tty, &ch, 1);
	if (was_stopped)
		stop_tty(tty);
	up_read(&tty->termios_rwsem);
	tty_write_unlock(tty);
	return 0;
}

static char ptychar[] = "pqrstuvwxyzabcde";

/**
 *	pty_line_name	-	generate name for a pty
 *	@driver: the tty driver in use
 *	@index: the minor number
 *	@p: output buffer of at least 6 bytes
 *
 *	Generate a name from a driver reference and write it to the output
 *	buffer.
 *
 *	Locking: None
 */
static void pty_line_name(struct tty_driver *driver, int index, char *p)
{
	int i = index + driver->name_base;
	/* ->name is initialized to "ttyp", but "tty" is expected */
	sprintf(p, "%s%c%x",
		driver->subtype == PTY_TYPE_SLAVE ? "tty" : driver->name,
		ptychar[i >> 4 & 0xf], i & 0xf);
}

/**
 *	tty_line_name	-	generate name for a tty
 *	@driver: the tty driver in use
 *	@index: the minor number
 *	@p: output buffer of at least 7 bytes
 *
 *	Generate a name from a driver reference and write it to the output
 *	buffer.
 *
 *	Locking: None
 */
static ssize_t tty_line_name(struct tty_driver *driver, int index, char *p)
{
	if (driver->flags & TTY_DRIVER_UNNUMBERED_NODE)
		return sprintf(p, "%s", driver->name);
	else
		return sprintf(p, "%s%d", driver->name,
			       index + driver->name_base);
}

/**
 *	tty_driver_lookup_tty() - find an existing tty, if any
 *	@driver: the driver for the tty
 *	@idx:	 the minor number
 *
 *	Return the tty, if found. If not found, return NULL or ERR_PTR() if the
 *	driver lookup() method returns an error.
 *
 *	Locking: tty_mutex must be held. If the tty is found, bump the tty kref.
 */
static struct tty_struct *tty_driver_lookup_tty(struct tty_driver *driver,
		struct inode *inode, int idx)
{
	struct tty_struct *tty;

	if (driver->ops->lookup)
		tty = driver->ops->lookup(driver, inode, idx);
	else
		tty = driver->ttys[idx];

	if (!IS_ERR(tty))
		tty_kref_get(tty);
	return tty;
}

/**
 *	tty_init_termios	-  helper for termios setup
 *	@tty: the tty to set up
 *
 *	Initialise the termios structures for this tty. Thus runs under
 *	the tty_mutex currently so we can be relaxed about ordering.
 */

int tty_init_termios(struct tty_struct *tty)
{
	struct ktermios *tp;
	int idx = tty->index;

	if (tty->driver->flags & TTY_DRIVER_RESET_TERMIOS)
		tty->termios = tty->driver->init_termios;
	else {
		/* Check for lazy saved data */
		tp = tty->driver->termios[idx];
		if (tp != NULL)
			tty->termios = *tp;
		else
			tty->termios = tty->driver->init_termios;
	}
	/* Compatibility until drivers always set this */
	tty->termios.c_ispeed = tty_termios_input_baud_rate(&tty->termios);
	tty->termios.c_ospeed = tty_termios_baud_rate(&tty->termios);
	return 0;
}
EXPORT_SYMBOL_GPL(tty_init_termios);

int tty_standard_install(struct tty_driver *driver, struct tty_struct *tty)
{
	int ret = tty_init_termios(tty);
	if (ret)
		return ret;

	tty_driver_kref_get(driver);
	tty->count++;
	driver->ttys[tty->index] = tty;
	return 0;
}
EXPORT_SYMBOL_GPL(tty_standard_install);

/**
 *	tty_driver_install_tty() - install a tty entry in the driver
 *	@driver: the driver for the tty
 *	@tty: the tty
 *
 *	Install a tty object into the driver tables. The tty->index field
 *	will be set by the time this is called. This method is responsible
 *	for ensuring any need additional structures are allocated and
 *	configured.
 *
 *	Locking: tty_mutex for now
 */
static int tty_driver_install_tty(struct tty_driver *driver,
						struct tty_struct *tty)
{
	return driver->ops->install ? driver->ops->install(driver, tty) :
		tty_standard_install(driver, tty);
}

/**
 *	tty_driver_remove_tty() - remove a tty from the driver tables
 *	@driver: the driver for the tty
 *	@idx:	 the minor number
 *
 *	Remvoe a tty object from the driver tables. The tty->index field
 *	will be set by the time this is called.
 *
 *	Locking: tty_mutex for now
 */
void tty_driver_remove_tty(struct tty_driver *driver, struct tty_struct *tty)
{
	if (driver->ops->remove)
		driver->ops->remove(driver, tty);
	else
		driver->ttys[tty->index] = NULL;
}

/*
 * 	tty_reopen()	- fast re-open of an open tty
 * 	@tty	- the tty to open
 *
 *	Return 0 on success, -errno on error.
 *	Re-opens on master ptys are not allowed and return -EIO.
 *
 *	Locking: Caller must hold tty_lock
 */
static int tty_reopen(struct tty_struct *tty)
{
	struct tty_driver *driver = tty->driver;

	if (driver->type == TTY_DRIVER_TYPE_PTY &&
	    driver->subtype == PTY_TYPE_MASTER)
		return -EIO;

	if (!tty->count)
		return -EAGAIN;

	if (test_bit(TTY_EXCLUSIVE, &tty->flags) && !capable(CAP_SYS_ADMIN))
		return -EBUSY;

	tty->count++;

	WARN_ON(!tty->ldisc);

	return 0;
}

/**
 *	tty_init_dev		-	initialise a tty device
 *	@driver: tty driver we are opening a device on
 *	@idx: device index
 *	@ret_tty: returned tty structure
 *
 *	Prepare a tty device. This may not be a "new" clean device but
 *	could also be an active device. The pty drivers require special
 *	handling because of this.
 *
 *	Locking:
 *		The function is called under the tty_mutex, which
 *	protects us from the tty struct or driver itself going away.
 *
 *	On exit the tty device has the line discipline attached and
 *	a reference count of 1. If a pair was created for pty/tty use
 *	and the other was a pty master then it too has a reference count of 1.
 *
 * WSH 06/09/97: Rewritten to remove races and properly clean up after a
 * failed open.  The new code protects the open with a mutex, so it's
 * really quite straightforward.  The mutex locking can probably be
 * relaxed for the (most common) case of reopening a tty.
 */

struct tty_struct *tty_init_dev(struct tty_driver *driver, int idx)
{
	struct tty_struct *tty;
	int retval;

	/*
	 * First time open is complex, especially for PTY devices.
	 * This code guarantees that either everything succeeds and the
	 * TTY is ready for operation, or else the table slots are vacated
	 * and the allocated memory released.  (Except that the termios
	 * and locked termios may be retained.)
	 */

	if (!try_module_get(driver->owner))
		return ERR_PTR(-ENODEV);

	tty = alloc_tty_struct(driver, idx);
	if (!tty) {
		retval = -ENOMEM;
		goto err_module_put;
	}

	tty_lock(tty);
	retval = tty_driver_install_tty(driver, tty);
	if (retval < 0)
		goto err_deinit_tty;

	if (!tty->port)
		tty->port = driver->ports[idx];

	WARN_RATELIMIT(!tty->port,
			"%s: %s driver does not set tty->port. This will crash the kernel later. Fix the driver!\n",
			__func__, tty->driver->name);

	tty->port->itty = tty;

	/*
	 * Structures all installed ... call the ldisc open routines.
	 * If we fail here just call release_tty to clean up.  No need
	 * to decrement the use counts, as release_tty doesn't care.
	 */
	retval = tty_ldisc_setup(tty, tty->link);
	if (retval)
		goto err_release_tty;
	/* Return the tty locked so that it cannot vanish under the caller */
	return tty;

err_deinit_tty:
	tty_unlock(tty);
	deinitialize_tty_struct(tty);
	free_tty_struct(tty);
err_module_put:
	module_put(driver->owner);
	return ERR_PTR(retval);

	/* call the tty release_tty routine to clean out this slot */
err_release_tty:
	tty_unlock(tty);
	printk_ratelimited(KERN_INFO "tty_init_dev: ldisc open failed, "
				 "clearing slot %d\n", idx);
	release_tty(tty, idx);
	return ERR_PTR(retval);
}

void tty_free_termios(struct tty_struct *tty)
{
	struct ktermios *tp;
	int idx = tty->index;

	/* If the port is going to reset then it has no termios to save */
	if (tty->driver->flags & TTY_DRIVER_RESET_TERMIOS)
		return;

	/* Stash the termios data */
	tp = tty->driver->termios[idx];
	if (tp == NULL) {
		tp = kmalloc(sizeof(struct ktermios), GFP_KERNEL);
		if (tp == NULL) {
			pr_warn("tty: no memory to save termios state.\n");
			return;
		}
		tty->driver->termios[idx] = tp;
	}
	*tp = tty->termios;
}
EXPORT_SYMBOL(tty_free_termios);

/**
 *	tty_flush_works		-	flush all works of a tty/pty pair
 *	@tty: tty device to flush works for (or either end of a pty pair)
 *
 *	Sync flush all works belonging to @tty (and the 'other' tty).
 */
static void tty_flush_works(struct tty_struct *tty)
{
	flush_work(&tty->SAK_work);
	flush_work(&tty->hangup_work);
	if (tty->link) {
		flush_work(&tty->link->SAK_work);
		flush_work(&tty->link->hangup_work);
	}
}

/**
 *	release_one_tty		-	release tty structure memory
 *	@kref: kref of tty we are obliterating
 *
 *	Releases memory associated with a tty structure, and clears out the
 *	driver table slots. This function is called when a device is no longer
 *	in use. It also gets called when setup of a device fails.
 *
 *	Locking:
 *		takes the file list lock internally when working on the list
 *	of ttys that the driver keeps.
 *
 *	This method gets called from a work queue so that the driver private
 *	cleanup ops can sleep (needed for USB at least)
 */
static void release_one_tty(struct work_struct *work)
{
	struct tty_struct *tty =
		container_of(work, struct tty_struct, hangup_work);
	struct tty_driver *driver = tty->driver;
	struct module *owner = driver->owner;

	if (tty->ops->cleanup)
		tty->ops->cleanup(tty);

	tty->magic = 0;
	tty_driver_kref_put(driver);
	module_put(owner);

	spin_lock(&tty_files_lock);
	list_del_init(&tty->tty_files);
	spin_unlock(&tty_files_lock);

	put_pid(tty->pgrp);
	put_pid(tty->session);
	free_tty_struct(tty);
}

static void queue_release_one_tty(struct kref *kref)
{
	struct tty_struct *tty = container_of(kref, struct tty_struct, kref);

	/* The hangup queue is now free so we can reuse it rather than
	   waste a chunk of memory for each port */
	INIT_WORK(&tty->hangup_work, release_one_tty);
	schedule_work(&tty->hangup_work);
}

/**
 *	tty_kref_put		-	release a tty kref
 *	@tty: tty device
 *
 *	Release a reference to a tty device and if need be let the kref
 *	layer destruct the object for us
 */

void tty_kref_put(struct tty_struct *tty)
{
	if (tty)
		kref_put(&tty->kref, queue_release_one_tty);
}
EXPORT_SYMBOL(tty_kref_put);

/**
 *	release_tty		-	release tty structure memory
 *
 *	Release both @tty and a possible linked partner (think pty pair),
 *	and decrement the refcount of the backing module.
 *
 *	Locking:
 *		tty_mutex
 *		takes the file list lock internally when working on the list
 *	of ttys that the driver keeps.
 *
 */
static void release_tty(struct tty_struct *tty, int idx)
{
	/* This should always be true but check for the moment */
	WARN_ON(tty->index != idx);
	WARN_ON(!mutex_is_locked(&tty_mutex));
	if (tty->ops->shutdown)
		tty->ops->shutdown(tty);
	tty_free_termios(tty);
	tty_driver_remove_tty(tty->driver, tty);
	tty->port->itty = NULL;
	if (tty->link)
		tty->link->port->itty = NULL;
	tty_buffer_cancel_work(tty->port);
	if (tty->link)
		tty_buffer_cancel_work(tty->link->port);

	tty_kref_put(tty->link);
	tty_kref_put(tty);
}

/**
 *	tty_release_checks - check a tty before real release
 *	@tty: tty to check
 *	@o_tty: link of @tty (if any)
 *	@idx: index of the tty
 *
 *	Performs some paranoid checking before true release of the @tty.
 *	This is a no-op unless TTY_PARANOIA_CHECK is defined.
 */
static int tty_release_checks(struct tty_struct *tty, int idx)
{
#ifdef TTY_PARANOIA_CHECK
	if (idx < 0 || idx >= tty->driver->num) {
		tty_debug(tty, "bad idx %d\n", idx);
		return -1;
	}

	/* not much to check for devpts */
	if (tty->driver->flags & TTY_DRIVER_DEVPTS_MEM)
		return 0;

	if (tty != tty->driver->ttys[idx]) {
		tty_debug(tty, "bad driver table[%d] = %p\n",
			  idx, tty->driver->ttys[idx]);
		return -1;
	}
	if (tty->driver->other) {
		struct tty_struct *o_tty = tty->link;

		if (o_tty != tty->driver->other->ttys[idx]) {
			tty_debug(tty, "bad other table[%d] = %p\n",
				  idx, tty->driver->other->ttys[idx]);
			return -1;
		}
		if (o_tty->link != tty) {
			tty_debug(tty, "bad link = %p\n", o_tty->link);
			return -1;
		}
	}
#endif
	return 0;
}

/**
 *	tty_release		-	vfs callback for close
 *	@inode: inode of tty
 *	@filp: file pointer for handle to tty
 *
 *	Called the last time each file handle is closed that references
 *	this tty. There may however be several such references.
 *
 *	Locking:
 *		Takes bkl. See tty_release_dev
 *
 * Even releasing the tty structures is a tricky business.. We have
 * to be very careful that the structures are all released at the
 * same time, as interrupts might otherwise get the wrong pointers.
 *
 * WSH 09/09/97: rewritten to avoid some nasty race conditions that could
 * lead to double frees or releasing memory still in use.
 */

int tty_release(struct inode *inode, struct file *filp)
{
	struct tty_struct *tty = file_tty(filp);
	struct tty_struct *o_tty = NULL;
	int	do_sleep, final;
	int	idx;
	long	timeout = 0;
	int	once = 1;

	if (tty_paranoia_check(tty, inode, __func__))
		return 0;

	tty_lock(tty);
	check_tty_count(tty, __func__);

	__tty_fasync(-1, filp, 0);

	idx = tty->index;
	if (tty->driver->type == TTY_DRIVER_TYPE_PTY &&
	    tty->driver->subtype == PTY_TYPE_MASTER)
		o_tty = tty->link;

	if (tty_release_checks(tty, idx)) {
		tty_unlock(tty);
		return 0;
	}

	tty_debug_hangup(tty, "(tty count=%d)...\n", tty->count);

	if (tty->ops->close)
		tty->ops->close(tty, filp);

	/* If tty is pty master, lock the slave pty (stable lock order) */
	tty_lock_slave(o_tty);

	/*
	 * Sanity check: if tty->count is going to zero, there shouldn't be
	 * any waiters on tty->read_wait or tty->write_wait.  We test the
	 * wait queues and kick everyone out _before_ actually starting to
	 * close.  This ensures that we won't block while releasing the tty
	 * structure.
	 *
	 * The test for the o_tty closing is necessary, since the master and
	 * slave sides may close in any order.  If the slave side closes out
	 * first, its count will be one, since the master side holds an open.
	 * Thus this test wouldn't be triggered at the time the slave closed,
	 * so we do it now.
	 */
	while (1) {
		do_sleep = 0;

		if (tty->count <= 1) {
			if (waitqueue_active(&tty->read_wait)) {
				wake_up_poll(&tty->read_wait, POLLIN);
				do_sleep++;
			}
			if (waitqueue_active(&tty->write_wait)) {
				wake_up_poll(&tty->write_wait, POLLOUT);
				do_sleep++;
			}
		}
		if (o_tty && o_tty->count <= 1) {
			if (waitqueue_active(&o_tty->read_wait)) {
				wake_up_poll(&o_tty->read_wait, POLLIN);
				do_sleep++;
			}
			if (waitqueue_active(&o_tty->write_wait)) {
				wake_up_poll(&o_tty->write_wait, POLLOUT);
				do_sleep++;
			}
		}
		if (!do_sleep)
			break;

		if (once) {
			once = 0;
			printk(KERN_WARNING "%s: %s: read/write wait queue active!\n",
			       __func__, tty_name(tty));
		}
		schedule_timeout_killable(timeout);
		if (timeout < 120 * HZ)
			timeout = 2 * timeout + 1;
		else
			timeout = MAX_SCHEDULE_TIMEOUT;
	}

	if (o_tty) {
		if (--o_tty->count < 0) {
			printk(KERN_WARNING "%s: bad pty slave count (%d) for %s\n",
				__func__, o_tty->count, tty_name(o_tty));
			o_tty->count = 0;
		}
	}
	if (--tty->count < 0) {
		printk(KERN_WARNING "%s: bad tty->count (%d) for %s\n",
				__func__, tty->count, tty_name(tty));
		tty->count = 0;
	}

	/*
	 * We've decremented tty->count, so we need to remove this file
	 * descriptor off the tty->tty_files list; this serves two
	 * purposes:
	 *  - check_tty_count sees the correct number of file descriptors
	 *    associated with this tty.
	 *  - do_tty_hangup no longer sees this file descriptor as
	 *    something that needs to be handled for hangups.
	 */
	tty_del_file(filp);

	/*
	 * Perform some housekeeping before deciding whether to return.
	 *
	 * If _either_ side is closing, make sure there aren't any
	 * processes that still think tty or o_tty is their controlling
	 * tty.
	 */
	if (!tty->count) {
		read_lock(&tasklist_lock);
		session_clear_tty(tty->session);
		if (o_tty)
			session_clear_tty(o_tty->session);
		read_unlock(&tasklist_lock);
	}

	/* check whether both sides are closing ... */
	final = !tty->count && !(o_tty && o_tty->count);

	tty_unlock_slave(o_tty);
	tty_unlock(tty);

	/* At this point, the tty->count == 0 should ensure a dead tty
	   cannot be re-opened by a racing opener */

	if (!final)
		return 0;

	tty_debug_hangup(tty, "final close\n");
	/*
	 * Ask the line discipline code to release its structures
	 */
	tty_ldisc_release(tty);

	/* Wait for pending work before tty destruction commmences */
	tty_flush_works(tty);

	tty_debug_hangup(tty, "freeing structure...\n");
	/*
	 * The release_tty function takes care of the details of clearing
	 * the slots and preserving the termios structure. The tty_unlock_pair
	 * should be safe as we keep a kref while the tty is locked (so the
	 * unlock never unlocks a freed tty).
	 */
	mutex_lock(&tty_mutex);
	release_tty(tty, idx);
	mutex_unlock(&tty_mutex);

	return 0;
}

/**
 *	tty_open_current_tty - get locked tty of current task
 *	@device: device number
 *	@filp: file pointer to tty
 *	@return: locked tty of the current task iff @device is /dev/tty
 *
 *	Performs a re-open of the current task's controlling tty.
 *
 *	We cannot return driver and index like for the other nodes because
 *	devpts will not work then. It expects inodes to be from devpts FS.
 */
static struct tty_struct *tty_open_current_tty(dev_t device, struct file *filp)
{
	struct tty_struct *tty;
	int retval;

	if (device != MKDEV(TTYAUX_MAJOR, 0))
		return NULL;

	tty = get_current_tty();
	if (!tty)
		return ERR_PTR(-ENXIO);

	filp->f_flags |= O_NONBLOCK; /* Don't let /dev/tty block */
	/* noctty = 1; */
	tty_lock(tty);
	tty_kref_put(tty);	/* safe to drop the kref now */

	retval = tty_reopen(tty);
	if (retval < 0) {
		tty_unlock(tty);
		tty = ERR_PTR(retval);
	}
	return tty;
}

/**
 *	tty_lookup_driver - lookup a tty driver for a given device file
 *	@device: device number
 *	@filp: file pointer to tty
 *	@noctty: set if the device should not become a controlling tty
 *	@index: index for the device in the @return driver
 *	@return: driver for this inode (with increased refcount)
 *
 * 	If @return is not erroneous, the caller is responsible to decrement the
 * 	refcount by tty_driver_kref_put.
 *
 *	Locking: tty_mutex protects get_tty_driver
 */
static struct tty_driver *tty_lookup_driver(dev_t device, struct file *filp,
		int *noctty, int *index)
{
	struct tty_driver *driver;

	switch (device) {
#ifdef CONFIG_VT
	case MKDEV(TTY_MAJOR, 0): {
		extern struct tty_driver *console_driver;
		driver = tty_driver_kref_get(console_driver);
		*index = fg_console;
		*noctty = 1;
		break;
	}
#endif
	case MKDEV(TTYAUX_MAJOR, 1): {
		struct tty_driver *console_driver = console_device(index);
		if (console_driver) {
			driver = tty_driver_kref_get(console_driver);
			if (driver) {
				/* Don't let /dev/console block */
				filp->f_flags |= O_NONBLOCK;
				*noctty = 1;
				break;
			}
		}
		return ERR_PTR(-ENODEV);
	}
	default:
		driver = get_tty_driver(device, index);
		if (!driver)
			return ERR_PTR(-ENODEV);
		break;
	}
	return driver;
}

/**
 *	tty_open		-	open a tty device
 *	@inode: inode of device file
 *	@filp: file pointer to tty
 *
 *	tty_open and tty_release keep up the tty count that contains the
 *	number of opens done on a tty. We cannot use the inode-count, as
 *	different inodes might point to the same tty.
 *
 *	Open-counting is needed for pty masters, as well as for keeping
 *	track of serial lines: DTR is dropped when the last close happens.
 *	(This is not done solely through tty->count, now.  - Ted 1/27/92)
 *
 *	The termios state of a pty is reset on first open so that
 *	settings don't persist across reuse.
 *
 *	Locking: tty_mutex protects tty, tty_lookup_driver and tty_init_dev.
 *		 tty->count should protect the rest.
 *		 ->siglock protects ->signal/->sighand
 *
 *	Note: the tty_unlock/lock cases without a ref are only safe due to
 *	tty_mutex
 */

static int tty_open(struct inode *inode, struct file *filp)
{
	struct tty_struct *tty;
	int noctty, retval;
	struct tty_driver *driver = NULL;
	int index;
	dev_t device = inode->i_rdev;
	unsigned saved_flags = filp->f_flags;

	nonseekable_open(inode, filp);

retry_open:
	retval = tty_alloc_file(filp);
	if (retval)
		return -ENOMEM;

	noctty = filp->f_flags & O_NOCTTY;
	index  = -1;
	retval = 0;

	tty = tty_open_current_tty(device, filp);
	if (!tty) {
		mutex_lock(&tty_mutex);
		driver = tty_lookup_driver(device, filp, &noctty, &index);
		if (IS_ERR(driver)) {
			retval = PTR_ERR(driver);
			goto err_unlock;
		}

		/* check whether we're reopening an existing tty */
		tty = tty_driver_lookup_tty(driver, inode, index);
		if (IS_ERR(tty)) {
			retval = PTR_ERR(tty);
			goto err_unlock;
		}

		if (tty) {
			mutex_unlock(&tty_mutex);
			retval = tty_lock_interruptible(tty);
			tty_kref_put(tty);  /* drop kref from tty_driver_lookup_tty() */
			if (retval) {
				if (retval == -EINTR)
					retval = -ERESTARTSYS;
				goto err_unref;
			}
			retval = tty_reopen(tty);
			if (retval < 0) {
				tty_unlock(tty);
				tty = ERR_PTR(retval);
			}
		} else { /* Returns with the tty_lock held for now */
			tty = tty_init_dev(driver, index);
			mutex_unlock(&tty_mutex);
		}

		tty_driver_kref_put(driver);
	}

	if (IS_ERR(tty)) {
		retval = PTR_ERR(tty);
		if (retval != -EAGAIN || signal_pending(current))
			goto err_file;
		tty_free_file(filp);
		schedule();
		goto retry_open;
	}

	tty_add_file(tty, filp);

	check_tty_count(tty, __func__);
	if (tty->driver->type == TTY_DRIVER_TYPE_PTY &&
	    tty->driver->subtype == PTY_TYPE_MASTER)
		noctty = 1;

	tty_debug_hangup(tty, "(tty count=%d)\n", tty->count);

	if (tty->ops->open)
		retval = tty->ops->open(tty, filp);
	else
		retval = -ENODEV;
	filp->f_flags = saved_flags;

	if (retval) {
		tty_debug_hangup(tty, "error %d, releasing...\n", retval);

		tty_unlock(tty); /* need to call tty_release without BTM */
		tty_release(inode, filp);
		if (retval != -ERESTARTSYS)
			return retval;

		if (signal_pending(current))
			return retval;

		schedule();
		/*
		 * Need to reset f_op in case a hangup happened.
		 */
		if (tty_hung_up_p(filp))
			filp->f_op = &tty_fops;
		goto retry_open;
	}
	clear_bit(TTY_HUPPED, &tty->flags);


	read_lock(&tasklist_lock);
	spin_lock_irq(&current->sighand->siglock);
	if (!noctty &&
	    current->signal->leader &&
	    !current->signal->tty &&
	    tty->session == NULL) {
		/*
		 * Don't let a process that only has write access to the tty
		 * obtain the privileges associated with having a tty as
		 * controlling terminal (being able to reopen it with full
		 * access through /dev/tty, being able to perform pushback).
		 * Many distributions set the group of all ttys to "tty" and
		 * grant write-only access to all terminals for setgid tty
		 * binaries, which should not imply full privileges on all ttys.
		 *
		 * This could theoretically break old code that performs open()
		 * on a write-only file descriptor. In that case, it might be
		 * necessary to also permit this if
		 * inode_permission(inode, MAY_READ) == 0.
		 */
		if (filp->f_mode & FMODE_READ)
			__proc_set_tty(tty);
	}
	spin_unlock_irq(&current->sighand->siglock);
	read_unlock(&tasklist_lock);
	tty_unlock(tty);
	return 0;
err_unlock:
	mutex_unlock(&tty_mutex);
err_unref:
	/* after locks to avoid deadlock */
	if (!IS_ERR_OR_NULL(driver))
		tty_driver_kref_put(driver);
err_file:
	tty_free_file(filp);
	return retval;
}



/**
 *	tty_poll	-	check tty status
 *	@filp: file being polled
 *	@wait: poll wait structures to update
 *
 *	Call the line discipline polling method to obtain the poll
 *	status of the device.
 *
 *	Locking: locks called line discipline but ldisc poll method
 *	may be re-entered freely by other callers.
 */

static unsigned int tty_poll(struct file *filp, poll_table *wait)
{
	struct tty_struct *tty = file_tty(filp);
	struct tty_ldisc *ld;
	int ret = 0;

	if (tty_paranoia_check(tty, file_inode(filp), "tty_poll"))
		return 0;

	ld = tty_ldisc_ref_wait(tty);
	if (ld->ops->poll)
		ret = ld->ops->poll(tty, filp, wait);
	tty_ldisc_deref(ld);
	return ret;
}

static int __tty_fasync(int fd, struct file *filp, int on)
{
	struct tty_struct *tty = file_tty(filp);
	struct tty_ldisc *ldisc;
	unsigned long flags;
	int retval = 0;

	if (tty_paranoia_check(tty, file_inode(filp), "tty_fasync"))
		goto out;

	retval = fasync_helper(fd, filp, on, &tty->fasync);
	if (retval <= 0)
		goto out;

	ldisc = tty_ldisc_ref(tty);
	if (ldisc) {
		if (ldisc->ops->fasync)
			ldisc->ops->fasync(tty, on);
		tty_ldisc_deref(ldisc);
	}

	if (on) {
		enum pid_type type;
		struct pid *pid;

		spin_lock_irqsave(&tty->ctrl_lock, flags);
		if (tty->pgrp) {
			pid = tty->pgrp;
			type = PIDTYPE_PGID;
		} else {
			pid = task_pid(current);
			type = PIDTYPE_PID;
		}
		get_pid(pid);
		spin_unlock_irqrestore(&tty->ctrl_lock, flags);
		__f_setown(filp, pid, type, 0);
		put_pid(pid);
		retval = 0;
	}
out:
	return retval;
}

static int tty_fasync(int fd, struct file *filp, int on)
{
	struct tty_struct *tty = file_tty(filp);
	int retval;

	tty_lock(tty);
	retval = __tty_fasync(fd, filp, on);
	tty_unlock(tty);

	return retval;
}

/**
 *	tiocsti			-	fake input character
 *	@tty: tty to fake input into
 *	@p: pointer to character
 *
 *	Fake input to a tty device. Does the necessary locking and
 *	input management.
 *
 *	FIXME: does not honour flow control ??
 *
 *	Locking:
 *		Called functions take tty_ldiscs_lock
 *		current->signal->tty check is safe without locks
 *
 *	FIXME: may race normal receive processing
 */

static int tiocsti(struct tty_struct *tty, char __user *p)
{
	char ch, mbz = 0;
	struct tty_ldisc *ld;

	if ((current->signal->tty != tty) && !capable(CAP_SYS_ADMIN))
		return -EPERM;
	if (get_user(ch, p))
		return -EFAULT;
	tty_audit_tiocsti(tty, ch);
	ld = tty_ldisc_ref_wait(tty);
	ld->ops->receive_buf(tty, &ch, &mbz, 1);
	tty_ldisc_deref(ld);
	return 0;
}

/**
 *	tiocgwinsz		-	implement window query ioctl
 *	@tty; tty
 *	@arg: user buffer for result
 *
 *	Copies the kernel idea of the window size into the user buffer.
 *
 *	Locking: tty->winsize_mutex is taken to ensure the winsize data
 *		is consistent.
 */

static int tiocgwinsz(struct tty_struct *tty, struct winsize __user *arg)
{
	int err;

	mutex_lock(&tty->winsize_mutex);
	err = copy_to_user(arg, &tty->winsize, sizeof(*arg));
	mutex_unlock(&tty->winsize_mutex);

	return err ? -EFAULT: 0;
}

/**
 *	tty_do_resize		-	resize event
 *	@tty: tty being resized
 *	@rows: rows (character)
 *	@cols: cols (character)
 *
 *	Update the termios variables and send the necessary signals to
 *	peform a terminal resize correctly
 */

int tty_do_resize(struct tty_struct *tty, struct winsize *ws)
{
	struct pid *pgrp;

	/* Lock the tty */
	mutex_lock(&tty->winsize_mutex);
	if (!memcmp(ws, &tty->winsize, sizeof(*ws)))
		goto done;

	/* Signal the foreground process group */
	pgrp = tty_get_pgrp(tty);
	if (pgrp)
		kill_pgrp(pgrp, SIGWINCH, 1);
	put_pid(pgrp);

	tty->winsize = *ws;
done:
	mutex_unlock(&tty->winsize_mutex);
	return 0;
}
EXPORT_SYMBOL(tty_do_resize);

/**
 *	tiocswinsz		-	implement window size set ioctl
 *	@tty; tty side of tty
 *	@arg: user buffer for result
 *
 *	Copies the user idea of the window size to the kernel. Traditionally
 *	this is just advisory information but for the Linux console it
 *	actually has driver level meaning and triggers a VC resize.
 *
 *	Locking:
 *		Driver dependent. The default do_resize method takes the
 *	tty termios mutex and ctrl_lock. The console takes its own lock
 *	then calls into the default method.
 */

static int tiocswinsz(struct tty_struct *tty, struct winsize __user *arg)
{
	struct winsize tmp_ws;
	if (copy_from_user(&tmp_ws, arg, sizeof(*arg)))
		return -EFAULT;

	if (tty->ops->resize)
		return tty->ops->resize(tty, &tmp_ws);
	else
		return tty_do_resize(tty, &tmp_ws);
}

/**
 *	tioccons	-	allow admin to move logical console
 *	@file: the file to become console
 *
 *	Allow the administrator to move the redirected console device
 *
 *	Locking: uses redirect_lock to guard the redirect information
 */

static int tioccons(struct file *file)
{
	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	if (file->f_op->write == redirected_tty_write) {
		struct file *f;
		spin_lock(&redirect_lock);
		f = redirect;
		redirect = NULL;
		spin_unlock(&redirect_lock);
		if (f)
			fput(f);
		return 0;
	}
	spin_lock(&redirect_lock);
	if (redirect) {
		spin_unlock(&redirect_lock);
		return -EBUSY;
	}
	redirect = get_file(file);
	spin_unlock(&redirect_lock);
	return 0;
}

/**
 *	fionbio		-	non blocking ioctl
 *	@file: file to set blocking value
 *	@p: user parameter
 *
 *	Historical tty interfaces had a blocking control ioctl before
 *	the generic functionality existed. This piece of history is preserved
 *	in the expected tty API of posix OS's.
 *
 *	Locking: none, the open file handle ensures it won't go away.
 */

static int fionbio(struct file *file, int __user *p)
{
	int nonblock;

	if (get_user(nonblock, p))
		return -EFAULT;

	spin_lock(&file->f_lock);
	if (nonblock)
		file->f_flags |= O_NONBLOCK;
	else
		file->f_flags &= ~O_NONBLOCK;
	spin_unlock(&file->f_lock);
	return 0;
}

/**
 *	tiocsctty	-	set controlling tty
 *	@tty: tty structure
 *	@arg: user argument
 *
 *	This ioctl is used to manage job control. It permits a session
 *	leader to set this tty as the controlling tty for the session.
 *
 *	Locking:
 *		Takes tty_lock() to serialize proc_set_tty() for this tty
 *		Takes tasklist_lock internally to walk sessions
 *		Takes ->siglock() when updating signal->tty
 */

static int tiocsctty(struct tty_struct *tty, struct file *file, int arg)
{
	int ret = 0;

	tty_lock(tty);
	read_lock(&tasklist_lock);

	if (current->signal->leader && (task_session(current) == tty->session))
		goto unlock;

	/*
	 * The process must be a session leader and
	 * not have a controlling tty already.
	 */
	if (!current->signal->leader || current->signal->tty) {
		ret = -EPERM;
		goto unlock;
	}

	if (tty->session) {
		/*
		 * This tty is already the controlling
		 * tty for another session group!
		 */
		if (arg == 1 && capable(CAP_SYS_ADMIN)) {
			/*
			 * Steal it away
			 */
			session_clear_tty(tty->session);
		} else {
			ret = -EPERM;
			goto unlock;
		}
	}

	/* See the comment in tty_open(). */
	if ((file->f_mode & FMODE_READ) == 0 && !capable(CAP_SYS_ADMIN)) {
		ret = -EPERM;
		goto unlock;
	}

	proc_set_tty(tty);
unlock:
	read_unlock(&tasklist_lock);
	tty_unlock(tty);
	return ret;
}

/**
 *	tty_get_pgrp	-	return a ref counted pgrp pid
 *	@tty: tty to read
 *
 *	Returns a refcounted instance of the pid struct for the process
 *	group controlling the tty.
 */

struct pid *tty_get_pgrp(struct tty_struct *tty)
{
	unsigned long flags;
	struct pid *pgrp;

	spin_lock_irqsave(&tty->ctrl_lock, flags);
	pgrp = get_pid(tty->pgrp);
	spin_unlock_irqrestore(&tty->ctrl_lock, flags);

	return pgrp;
}
EXPORT_SYMBOL_GPL(tty_get_pgrp);

/*
 * This checks not only the pgrp, but falls back on the pid if no
 * satisfactory pgrp is found. I dunno - gdb doesn't work correctly
 * without this...
 *
 * The caller must hold rcu lock or the tasklist lock.
 */
static struct pid *session_of_pgrp(struct pid *pgrp)
{
	struct task_struct *p;
	struct pid *sid = NULL;

	p = pid_task(pgrp, PIDTYPE_PGID);
	if (p == NULL)
		p = pid_task(pgrp, PIDTYPE_PID);
	if (p != NULL)
		sid = task_session(p);

	return sid;
}

/**
 *	tiocgpgrp		-	get process group
 *	@tty: tty passed by user
 *	@real_tty: tty side of the tty passed by the user if a pty else the tty
 *	@p: returned pid
 *
 *	Obtain the process group of the tty. If there is no process group
 *	return an error.
 *
 *	Locking: none. Reference to current->signal->tty is safe.
 */

static int tiocgpgrp(struct tty_struct *tty, struct tty_struct *real_tty, pid_t __user *p)
{
	struct pid *pid;
	int ret;
	/*
	 * (tty == real_tty) is a cheap way of
	 * testing if the tty is NOT a master pty.
	 */
	if (tty == real_tty && current->signal->tty != real_tty)
		return -ENOTTY;
	pid = tty_get_pgrp(real_tty);
	ret =  put_user(pid_vnr(pid), p);
	put_pid(pid);
	return ret;
}

/**
 *	tiocspgrp		-	attempt to set process group
 *	@tty: tty passed by user
 *	@real_tty: tty side device matching tty passed by user
 *	@p: pid pointer
 *
 *	Set the process group of the tty to the session passed. Only
 *	permitted where the tty session is our session.
 *
 *	Locking: RCU, ctrl lock
 */

static int tiocspgrp(struct tty_struct *tty, struct tty_struct *real_tty, pid_t __user *p)
{
	struct pid *pgrp;
	pid_t pgrp_nr;
	int retval = tty_check_change(real_tty);

	if (retval == -EIO)
		return -ENOTTY;
	if (retval)
		return retval;
	if (!current->signal->tty ||
	    (current->signal->tty != real_tty) ||
	    (real_tty->session != task_session(current)))
		return -ENOTTY;
	if (get_user(pgrp_nr, p))
		return -EFAULT;
	if (pgrp_nr < 0)
		return -EINVAL;
	rcu_read_lock();
	pgrp = find_vpid(pgrp_nr);
	retval = -ESRCH;
	if (!pgrp)
		goto out_unlock;
	retval = -EPERM;
	if (session_of_pgrp(pgrp) != task_session(current))
		goto out_unlock;
	retval = 0;
	spin_lock_irq(&tty->ctrl_lock);
	put_pid(real_tty->pgrp);
	real_tty->pgrp = get_pid(pgrp);
	spin_unlock_irq(&tty->ctrl_lock);
out_unlock:
	rcu_read_unlock();
	return retval;
}

/**
 *	tiocgsid		-	get session id
 *	@tty: tty passed by user
 *	@real_tty: tty side of the tty passed by the user if a pty else the tty
 *	@p: pointer to returned session id
 *
 *	Obtain the session id of the tty. If there is no session
 *	return an error.
 *
 *	Locking: none. Reference to current->signal->tty is safe.
 */

static int tiocgsid(struct tty_struct *tty, struct tty_struct *real_tty, pid_t __user *p)
{
	/*
	 * (tty == real_tty) is a cheap way of
	 * testing if the tty is NOT a master pty.
	*/
	if (tty == real_tty && current->signal->tty != real_tty)
		return -ENOTTY;
	if (!real_tty->session)
		return -ENOTTY;
	return put_user(pid_vnr(real_tty->session), p);
}

/**
 *	tiocsetd	-	set line discipline
 *	@tty: tty device
 *	@p: pointer to user data
 *
 *	Set the line discipline according to user request.
 *
 *	Locking: see tty_set_ldisc, this function is just a helper
 */

static int tiocsetd(struct tty_struct *tty, int __user *p)
{
	int ldisc;
	int ret;

	if (get_user(ldisc, p))
		return -EFAULT;

	ret = tty_set_ldisc(tty, ldisc);

	return ret;
}

/**
 *	tiocgetd	-	get line discipline
 *	@tty: tty device
 *	@p: pointer to user data
 *
 *	Retrieves the line discipline id directly from the ldisc.
 *
 *	Locking: waits for ldisc reference (in case the line discipline
 *		is changing or the tty is being hungup)
 */

static int tiocgetd(struct tty_struct *tty, int __user *p)
{
	struct tty_ldisc *ld;
	int ret;

	ld = tty_ldisc_ref_wait(tty);
	ret = put_user(ld->ops->num, p);
	tty_ldisc_deref(ld);
	return ret;
}

/**
 *	send_break	-	performed time break
 *	@tty: device to break on
 *	@duration: timeout in mS
 *
 *	Perform a timed break on hardware that lacks its own driver level
 *	timed break functionality.
 *
 *	Locking:
 *		atomic_write_lock serializes
 *
 */

static int send_break(struct tty_struct *tty, unsigned int duration)
{
	int retval;

	if (tty->ops->break_ctl == NULL)
		return 0;

	if (tty->driver->flags & TTY_DRIVER_HARDWARE_BREAK)
		retval = tty->ops->break_ctl(tty, duration);
	else {
		/* Do the work ourselves */
		if (tty_write_lock(tty, 0) < 0)
			return -EINTR;
		retval = tty->ops->break_ctl(tty, -1);
		if (retval)
			goto out;
		if (!signal_pending(current))
			msleep_interruptible(duration);
		retval = tty->ops->break_ctl(tty, 0);
out:
		tty_write_unlock(tty);
		if (signal_pending(current))
			retval = -EINTR;
	}
	return retval;
}

/**
 *	tty_tiocmget		-	get modem status
 *	@tty: tty device
 *	@file: user file pointer
 *	@p: pointer to result
 *
 *	Obtain the modem status bits from the tty driver if the feature
 *	is supported. Return -EINVAL if it is not available.
 *
 *	Locking: none (up to the driver)
 */

static int tty_tiocmget(struct tty_struct *tty, int __user *p)
{
	int retval = -EINVAL;

	if (tty->ops->tiocmget) {
		retval = tty->ops->tiocmget(tty);

		if (retval >= 0)
			retval = put_user(retval, p);
	}
	return retval;
}

/**
 *	tty_tiocmset		-	set modem status
 *	@tty: tty device
 *	@cmd: command - clear bits, set bits or set all
 *	@p: pointer to desired bits
 *
 *	Set the modem status bits from the tty driver if the feature
 *	is supported. Return -EINVAL if it is not available.
 *
 *	Locking: none (up to the driver)
 */

static int tty_tiocmset(struct tty_struct *tty, unsigned int cmd,
	     unsigned __user *p)
{
	int retval;
	unsigned int set, clear, val;

	if (tty->ops->tiocmset == NULL)
		return -EINVAL;

	retval = get_user(val, p);
	if (retval)
		return retval;
	set = clear = 0;
	switch (cmd) {
	case TIOCMBIS:
		set = val;
		break;
	case TIOCMBIC:
		clear = val;
		break;
	case TIOCMSET:
		set = val;
		clear = ~val;
		break;
	}
	set &= TIOCM_DTR|TIOCM_RTS|TIOCM_OUT1|TIOCM_OUT2|TIOCM_LOOP;
	clear &= TIOCM_DTR|TIOCM_RTS|TIOCM_OUT1|TIOCM_OUT2|TIOCM_LOOP;
	return tty->ops->tiocmset(tty, set, clear);
}

static int tty_tiocgicount(struct tty_struct *tty, void __user *arg)
{
	int retval = -EINVAL;
	struct serial_icounter_struct icount;
	memset(&icount, 0, sizeof(icount));
	if (tty->ops->get_icount)
		retval = tty->ops->get_icount(tty, &icount);
	if (retval != 0)
		return retval;
	if (copy_to_user(arg, &icount, sizeof(icount)))
		return -EFAULT;
	return 0;
}

static void tty_warn_deprecated_flags(struct serial_struct __user *ss)
{
	static DEFINE_RATELIMIT_STATE(depr_flags,
			DEFAULT_RATELIMIT_INTERVAL,
			DEFAULT_RATELIMIT_BURST);
	char comm[TASK_COMM_LEN];
	int flags;

	if (get_user(flags, &ss->flags))
		return;

	flags &= ASYNC_DEPRECATED;

	if (flags && __ratelimit(&depr_flags))
		pr_warning("%s: '%s' is using deprecated serial flags (with no effect): %.8x\n",
				__func__, get_task_comm(comm, current), flags);
}

/*
 * if pty, return the slave side (real_tty)
 * otherwise, return self
 */
static struct tty_struct *tty_pair_get_tty(struct tty_struct *tty)
{
	if (tty->driver->type == TTY_DRIVER_TYPE_PTY &&
	    tty->driver->subtype == PTY_TYPE_MASTER)
		tty = tty->link;
	return tty;
}

/*
 * Split this up, as gcc can choke on it otherwise..
 */
long tty_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct tty_struct *tty = file_tty(file);
	struct tty_struct *real_tty;
	void __user *p = (void __user *)arg;
	int retval;
	struct tty_ldisc *ld;

	if (tty_paranoia_check(tty, file_inode(file), "tty_ioctl"))
		return -EINVAL;

	real_tty = tty_pair_get_tty(tty);

	/*
	 * Factor out some common prep work
	 */
	switch (cmd) {
	case TIOCSETD:
	case TIOCSBRK:
	case TIOCCBRK:
	case TCSBRK:
	case TCSBRKP:
		retval = tty_check_change(tty);
		if (retval)
			return retval;
		if (cmd != TIOCCBRK) {
			tty_wait_until_sent(tty, 0);
			if (signal_pending(current))
				return -EINTR;
		}
		break;
	}

	/*
	 *	Now do the stuff.
	 */
	switch (cmd) {
	case TIOCSTI:
		return tiocsti(tty, p);
	case TIOCGWINSZ:
		return tiocgwinsz(real_tty, p);
	case TIOCSWINSZ:
		return tiocswinsz(real_tty, p);
	case TIOCCONS:
		return real_tty != tty ? -EINVAL : tioccons(file);
	case FIONBIO:
		return fionbio(file, p);
	case TIOCEXCL:
		set_bit(TTY_EXCLUSIVE, &tty->flags);
		return 0;
	case TIOCNXCL:
		clear_bit(TTY_EXCLUSIVE, &tty->flags);
		return 0;
	case TIOCGEXCL:
	{
		int excl = test_bit(TTY_EXCLUSIVE, &tty->flags);
		return put_user(excl, (int __user *)p);
	}
	case TIOCNOTTY:
		if (current->signal->tty != tty)
			return -ENOTTY;
		no_tty();
		return 0;
	case TIOCSCTTY:
		return tiocsctty(tty, file, arg);
	case TIOCGPGRP:
		return tiocgpgrp(tty, real_tty, p);
	case TIOCSPGRP:
		return tiocspgrp(tty, real_tty, p);
	case TIOCGSID:
		return tiocgsid(tty, real_tty, p);
	case TIOCGETD:
		return tiocgetd(tty, p);
	case TIOCSETD:
		return tiocsetd(tty, p);
	case TIOCVHANGUP:
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;
		tty_vhangup(tty);
		return 0;
	case TIOCGDEV:
	{
		unsigned int ret = new_encode_dev(tty_devnum(real_tty));
		return put_user(ret, (unsigned int __user *)p);
	}
	/*
	 * Break handling
	 */
	case TIOCSBRK:	/* Turn break on, unconditionally */
		if (tty->ops->break_ctl)
			return tty->ops->break_ctl(tty, -1);
		return 0;
	case TIOCCBRK:	/* Turn break off, unconditionally */
		if (tty->ops->break_ctl)
			return tty->ops->break_ctl(tty, 0);
		return 0;
	case TCSBRK:   /* SVID version: non-zero arg --> no break */
		/* non-zero arg means wait for all output data
		 * to be sent (performed above) but don't send break.
		 * This is used by the tcdrain() termios function.
		 */
		if (!arg)
			return send_break(tty, 250);
		return 0;
	case TCSBRKP:	/* support for POSIX tcsendbreak() */
		return send_break(tty, arg ? arg*100 : 250);

	case TIOCMGET:
		return tty_tiocmget(tty, p);
	case TIOCMSET:
	case TIOCMBIC:
	case TIOCMBIS:
		return tty_tiocmset(tty, cmd, p);
	case TIOCGICOUNT:
		retval = tty_tiocgicount(tty, p);
		/* For the moment allow fall through to the old method */
        	if (retval != -EINVAL)
			return retval;
		break;
	case TCFLSH:
		switch (arg) {
		case TCIFLUSH:
		case TCIOFLUSH:
		/* flush tty buffer and allow ldisc to process ioctl */
			tty_buffer_flush(tty, NULL);
			break;
		}
		break;
	case TIOCSSERIAL:
		tty_warn_deprecated_flags(p);
		break;
	}
	if (tty->ops->ioctl) {
		retval = tty->ops->ioctl(tty, cmd, arg);
		if (retval != -ENOIOCTLCMD)
			return retval;
	}
	ld = tty_ldisc_ref_wait(tty);
	retval = -EINVAL;
	if (ld->ops->ioctl) {
		retval = ld->ops->ioctl(tty, file, cmd, arg);
		if (retval == -ENOIOCTLCMD)
			retval = -ENOTTY;
	}
	tty_ldisc_deref(ld);
	return retval;
}

#ifdef CONFIG_COMPAT
static long tty_compat_ioctl(struct file *file, unsigned int cmd,
				unsigned long arg)
{
	struct tty_struct *tty = file_tty(file);
	struct tty_ldisc *ld;
	int retval = -ENOIOCTLCMD;

	if (tty_paranoia_check(tty, file_inode(file), "tty_ioctl"))
		return -EINVAL;

	if (tty->ops->compat_ioctl) {
		retval = tty->ops->compat_ioctl(tty, cmd, arg);
		if (retval != -ENOIOCTLCMD)
			return retval;
	}

	ld = tty_ldisc_ref_wait(tty);
	if (ld->ops->compat_ioctl)
		retval = ld->ops->compat_ioctl(tty, file, cmd, arg);
	else
		retval = n_tty_compat_ioctl_helper(tty, file, cmd, arg);
	tty_ldisc_deref(ld);

	return retval;
}
#endif

static int this_tty(const void *t, struct file *file, unsigned fd)
{
	if (likely(file->f_op->read != tty_read))
		return 0;
	return file_tty(file) != t ? 0 : fd + 1;
}
	
/*
 * This implements the "Secure Attention Key" ---  the idea is to
 * prevent trojan horses by killing all processes associated with this
 * tty when the user hits the "Secure Attention Key".  Required for
 * super-paranoid applications --- see the Orange Book for more details.
 *
 * This code could be nicer; ideally it should send a HUP, wait a few
 * seconds, then send a INT, and then a KILL signal.  But you then
 * have to coordinate with the init process, since all processes associated
 * with the current tty must be dead before the new getty is allowed
 * to spawn.
 *
 * Now, if it would be correct ;-/ The current code has a nasty hole -
 * it doesn't catch files in flight. We may send the descriptor to ourselves
 * via AF_UNIX socket, close it and later fetch from socket. FIXME.
 *
 * Nasty bug: do_SAK is being called in interrupt context.  This can
 * deadlock.  We punt it up to process context.  AKPM - 16Mar2001
 */
void __do_SAK(struct tty_struct *tty)
{
#ifdef TTY_SOFT_SAK
	tty_hangup(tty);
#else
	struct task_struct *g, *p;
	struct pid *session;
	int		i;

	if (!tty)
		return;
	session = tty->session;

	tty_ldisc_flush(tty);

	tty_driver_flush_buffer(tty);

	read_lock(&tasklist_lock);
	/* Kill the entire session */
	do_each_pid_task(session, PIDTYPE_SID, p) {
		printk(KERN_NOTICE "SAK: killed process %d"
			" (%s): task_session(p)==tty->session\n",
			task_pid_nr(p), p->comm);
		send_sig(SIGKILL, p, 1);
	} while_each_pid_task(session, PIDTYPE_SID, p);
	/* Now kill any processes that happen to have the
	 * tty open.
	 */
	do_each_thread(g, p) {
		if (p->signal->tty == tty) {
			printk(KERN_NOTICE "SAK: killed process %d"
			    " (%s): task_session(p)==tty->session\n",
			    task_pid_nr(p), p->comm);
			send_sig(SIGKILL, p, 1);
			continue;
		}
		task_lock(p);
		i = iterate_fd(p->files, 0, this_tty, tty);
		if (i != 0) {
			printk(KERN_NOTICE "SAK: killed process %d"
			    " (%s): fd#%d opened to the tty\n",
				    task_pid_nr(p), p->comm, i - 1);
			force_sig(SIGKILL, p);
		}
		task_unlock(p);
	} while_each_thread(g, p);
	read_unlock(&tasklist_lock);
#endif
}

static void do_SAK_work(struct work_struct *work)
{
	struct tty_struct *tty =
		container_of(work, struct tty_struct, SAK_work);
	__do_SAK(tty);
}

/*
 * The tq handling here is a little racy - tty->SAK_work may already be queued.
 * Fortunately we don't need to worry, because if ->SAK_work is already queued,
 * the values which we write to it will be identical to the values which it
 * already has. --akpm
 */
void do_SAK(struct tty_struct *tty)
{
	if (!tty)
		return;
	schedule_work(&tty->SAK_work);
}

EXPORT_SYMBOL(do_SAK);

static int dev_match_devt(struct device *dev, const void *data)
{
	const dev_t *devt = data;
	return dev->devt == *devt;
}

/* Must put_device() after it's unused! */
static struct device *tty_get_device(struct tty_struct *tty)
{
	dev_t devt = tty_devnum(tty);
	return class_find_device(tty_class, NULL, &devt, dev_match_devt);
}


/**
 *	alloc_tty_struct
 *
 *	This subroutine allocates and initializes a tty structure.
 *
 *	Locking: none - tty in question is not exposed at this point
 */

struct tty_struct *alloc_tty_struct(struct tty_driver *driver, int idx)
{
	struct tty_struct *tty;

	tty = kzalloc(sizeof(*tty), GFP_KERNEL);
	if (!tty)
		return NULL;

	kref_init(&tty->kref);
	tty->magic = TTY_MAGIC;
	tty_ldisc_init(tty);
	tty->session = NULL;
	tty->pgrp = NULL;
	mutex_init(&tty->legacy_mutex);
	mutex_init(&tty->throttle_mutex);
	init_rwsem(&tty->termios_rwsem);
	mutex_init(&tty->winsize_mutex);
	init_ldsem(&tty->ldisc_sem);
	init_waitqueue_head(&tty->write_wait);
	init_waitqueue_head(&tty->read_wait);
	INIT_WORK(&tty->hangup_work, do_tty_hangup);
	mutex_init(&tty->atomic_write_lock);
	spin_lock_init(&tty->ctrl_lock);
	spin_lock_init(&tty->flow_lock);
	INIT_LIST_HEAD(&tty->tty_files);
	INIT_WORK(&tty->SAK_work, do_SAK_work);

	tty->driver = driver;
	tty->ops = driver->ops;
	tty->index = idx;
	tty_line_name(driver, idx, tty->name);
	tty->dev = tty_get_device(tty);

	return tty;
}

/**
 *	deinitialize_tty_struct
 *	@tty: tty to deinitialize
 *
 *	This subroutine deinitializes a tty structure that has been newly
 *	allocated but tty_release cannot be called on that yet.
 *
 *	Locking: none - tty in question must not be exposed at this point
 */
void deinitialize_tty_struct(struct tty_struct *tty)
{
	tty_ldisc_deinit(tty);
}

/**
 *	tty_put_char	-	write one character to a tty
 *	@tty: tty
 *	@ch: character
 *
 *	Write one byte to the tty using the provided put_char method
 *	if present. Returns the number of characters successfully output.
 *
 *	Note: the specific put_char operation in the driver layer may go
 *	away soon. Don't call it directly, use this method
 */

int tty_put_char(struct tty_struct *tty, unsigned char ch)
{
	if (tty->ops->put_char)
		return tty->ops->put_char(tty, ch);
	return tty->ops->write(tty, &ch, 1);
}
EXPORT_SYMBOL_GPL(tty_put_char);

struct class *tty_class;

static int tty_cdev_add(struct tty_driver *driver, dev_t dev,
		unsigned int index, unsigned int count)
{
	int err;

	/* init here, since reused cdevs cause crashes */
	driver->cdevs[index] = cdev_alloc();
	if (!driver->cdevs[index])
		return -ENOMEM;
	driver->cdevs[index]->ops = &tty_fops;
	driver->cdevs[index]->owner = driver->owner;
	err = cdev_add(driver->cdevs[index], dev, count);
	if (err)
		kobject_put(&driver->cdevs[index]->kobj);
	return err;
}

/**
 *	tty_register_device - register a tty device
 *	@driver: the tty driver that describes the tty device
 *	@index: the index in the tty driver for this tty device
 *	@device: a struct device that is associated with this tty device.
 *		This field is optional, if there is no known struct device
 *		for this tty device it can be set to NULL safely.
 *
 *	Returns a pointer to the struct device for this tty device
 *	(or ERR_PTR(-EFOO) on error).
 *
 *	This call is required to be made to register an individual tty device
 *	if the tty driver's flags have the TTY_DRIVER_DYNAMIC_DEV bit set.  If
 *	that bit is not set, this function should not be called by a tty
 *	driver.
 *
 *	Locking: ??
 */

struct device *tty_register_device(struct tty_driver *driver, unsigned index,
				   struct device *device)
{
	return tty_register_device_attr(driver, index, device, NULL, NULL);
}
EXPORT_SYMBOL(tty_register_device);

static void tty_device_create_release(struct device *dev)
{
	pr_debug("device: '%s': %s\n", dev_name(dev), __func__);
	kfree(dev);
}

/**
 *	tty_register_device_attr - register a tty device
 *	@driver: the tty driver that describes the tty device
 *	@index: the index in the tty driver for this tty device
 *	@device: a struct device that is associated with this tty device.
 *		This field is optional, if there is no known struct device
 *		for this tty device it can be set to NULL safely.
 *	@drvdata: Driver data to be set to device.
 *	@attr_grp: Attribute group to be set on device.
 *
 *	Returns a pointer to the struct device for this tty device
 *	(or ERR_PTR(-EFOO) on error).
 *
 *	This call is required to be made to register an individual tty device
 *	if the tty driver's flags have the TTY_DRIVER_DYNAMIC_DEV bit set.  If
 *	that bit is not set, this function should not be called by a tty
 *	driver.
 *
 *	Locking: ??
 */
struct device *tty_register_device_attr(struct tty_driver *driver,
				   unsigned index, struct device *device,
				   void *drvdata,
				   const struct attribute_group **attr_grp)
{
	char name[64];
	dev_t devt = MKDEV(driver->major, driver->minor_start) + index;
	struct device *dev = NULL;
	int retval = -ENODEV;
	bool cdev = false;

	if (index >= driver->num) {
		printk(KERN_ERR "Attempt to register invalid tty line number "
		       " (%d).\n", index);
		return ERR_PTR(-EINVAL);
	}

	if (driver->type == TTY_DRIVER_TYPE_PTY)
		pty_line_name(driver, index, name);
	else
		tty_line_name(driver, index, name);

	if (!(driver->flags & TTY_DRIVER_DYNAMIC_ALLOC)) {
		retval = tty_cdev_add(driver, devt, index, 1);
		if (retval)
			goto error;
		cdev = true;
	}

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev) {
		retval = -ENOMEM;
		goto error;
	}

	dev->devt = devt;
	dev->class = tty_class;
	dev->parent = device;
	dev->release = tty_device_create_release;
	dev_set_name(dev, "%s", name);
	dev->groups = attr_grp;
	dev_set_drvdata(dev, drvdata);

	retval = device_register(dev);
	if (retval)
		goto error;

	return dev;

error:
	put_device(dev);
	if (cdev) {
		cdev_del(driver->cdevs[index]);
		driver->cdevs[index] = NULL;
	}
	return ERR_PTR(retval);
}
EXPORT_SYMBOL_GPL(tty_register_device_attr);

/**
 * 	tty_unregister_device - unregister a tty device
 * 	@driver: the tty driver that describes the tty device
 * 	@index: the index in the tty driver for this tty device
 *
 * 	If a tty device is registered with a call to tty_register_device() then
 *	this function must be called when the tty device is gone.
 *
 *	Locking: ??
 */

void tty_unregister_device(struct tty_driver *driver, unsigned index)
{
	device_destroy(tty_class,
		MKDEV(driver->major, driver->minor_start) + index);
	if (!(driver->flags & TTY_DRIVER_DYNAMIC_ALLOC)) {
		cdev_del(driver->cdevs[index]);
		driver->cdevs[index] = NULL;
	}
}
EXPORT_SYMBOL(tty_unregister_device);

/**
 * __tty_alloc_driver -- allocate tty driver
 * @lines: count of lines this driver can handle at most
 * @owner: module which is repsonsible for this driver
 * @flags: some of TTY_DRIVER_* flags, will be set in driver->flags
 *
 * This should not be called directly, some of the provided macros should be
 * used instead. Use IS_ERR and friends on @retval.
 */
struct tty_driver *__tty_alloc_driver(unsigned int lines, struct module *owner,
		unsigned long flags)
{
	struct tty_driver *driver;
	unsigned int cdevs = 1;
	int err;

	if (!lines || (flags & TTY_DRIVER_UNNUMBERED_NODE && lines > 1))
		return ERR_PTR(-EINVAL);

	driver = kzalloc(sizeof(struct tty_driver), GFP_KERNEL);
	if (!driver)
		return ERR_PTR(-ENOMEM);

	kref_init(&driver->kref);
	driver->magic = TTY_DRIVER_MAGIC;
	driver->num = lines;
	driver->owner = owner;
	driver->flags = flags;

	if (!(flags & TTY_DRIVER_DEVPTS_MEM)) {
		driver->ttys = kcalloc(lines, sizeof(*driver->ttys),
				GFP_KERNEL);
		driver->termios = kcalloc(lines, sizeof(*driver->termios),
				GFP_KERNEL);
		if (!driver->ttys || !driver->termios) {
			err = -ENOMEM;
			goto err_free_all;
		}
	}

	if (!(flags & TTY_DRIVER_DYNAMIC_ALLOC)) {
		driver->ports = kcalloc(lines, sizeof(*driver->ports),
				GFP_KERNEL);
		if (!driver->ports) {
			err = -ENOMEM;
			goto err_free_all;
		}
		cdevs = lines;
	}

	driver->cdevs = kcalloc(cdevs, sizeof(*driver->cdevs), GFP_KERNEL);
	if (!driver->cdevs) {
		err = -ENOMEM;
		goto err_free_all;
	}

	return driver;
err_free_all:
	kfree(driver->ports);
	kfree(driver->ttys);
	kfree(driver->termios);
	kfree(driver->cdevs);
	kfree(driver);
	return ERR_PTR(err);
}
EXPORT_SYMBOL(__tty_alloc_driver);

static void destruct_tty_driver(struct kref *kref)
{
	struct tty_driver *driver = container_of(kref, struct tty_driver, kref);
	int i;
	struct ktermios *tp;

	if (driver->flags & TTY_DRIVER_INSTALLED) {
		/*
		 * Free the termios and termios_locked structures because
		 * we don't want to get memory leaks when modular tty
		 * drivers are removed from the kernel.
		 */
		for (i = 0; i < driver->num; i++) {
			tp = driver->termios[i];
			if (tp) {
				driver->termios[i] = NULL;
				kfree(tp);
			}
			if (!(driver->flags & TTY_DRIVER_DYNAMIC_DEV))
				tty_unregister_device(driver, i);
		}
		proc_tty_unregister_driver(driver);
		if (driver->flags & TTY_DRIVER_DYNAMIC_ALLOC)
			cdev_del(driver->cdevs[0]);
	}
	kfree(driver->cdevs);
	kfree(driver->ports);
	kfree(driver->termios);
	kfree(driver->ttys);
	kfree(driver);
}

void tty_driver_kref_put(struct tty_driver *driver)
{
	kref_put(&driver->kref, destruct_tty_driver);
}
EXPORT_SYMBOL(tty_driver_kref_put);

void tty_set_operations(struct tty_driver *driver,
			const struct tty_operations *op)
{
	driver->ops = op;
};
EXPORT_SYMBOL(tty_set_operations);

void put_tty_driver(struct tty_driver *d)
{
	tty_driver_kref_put(d);
}
EXPORT_SYMBOL(put_tty_driver);

/*
 * Called by a tty driver to register itself.
 */
int tty_register_driver(struct tty_driver *driver)
{
	int error;
	int i;
	dev_t dev;
	struct device *d;

	if (!driver->major) {
		error = alloc_chrdev_region(&dev, driver->minor_start,
						driver->num, driver->name);
		if (!error) {
			driver->major = MAJOR(dev);
			driver->minor_start = MINOR(dev);
		}
	} else {
		dev = MKDEV(driver->major, driver->minor_start);
		error = register_chrdev_region(dev, driver->num, driver->name);
	}
	if (error < 0)
		goto err;

	if (driver->flags & TTY_DRIVER_DYNAMIC_ALLOC) {
		error = tty_cdev_add(driver, dev, 0, driver->num);
		if (error)
			goto err_unreg_char;
	}

	mutex_lock(&tty_mutex);
	list_add(&driver->tty_drivers, &tty_drivers);
	mutex_unlock(&tty_mutex);

	if (!(driver->flags & TTY_DRIVER_DYNAMIC_DEV)) {
		for (i = 0; i < driver->num; i++) {
			d = tty_register_device(driver, i, NULL);
			if (IS_ERR(d)) {
				error = PTR_ERR(d);
				goto err_unreg_devs;
			}
		}
	}
	proc_tty_register_driver(driver);
	driver->flags |= TTY_DRIVER_INSTALLED;
	return 0;

err_unreg_devs:
	for (i--; i >= 0; i--)
		tty_unregister_device(driver, i);

	mutex_lock(&tty_mutex);
	list_del(&driver->tty_drivers);
	mutex_unlock(&tty_mutex);

err_unreg_char:
	unregister_chrdev_region(dev, driver->num);
err:
	return error;
}
EXPORT_SYMBOL(tty_register_driver);

/*
 * Called by a tty driver to unregister itself.
 */
int tty_unregister_driver(struct tty_driver *driver)
{
#if 0
	/* FIXME */
	if (driver->refcount)
		return -EBUSY;
#endif
	unregister_chrdev_region(MKDEV(driver->major, driver->minor_start),
				driver->num);
	mutex_lock(&tty_mutex);
	list_del(&driver->tty_drivers);
	mutex_unlock(&tty_mutex);
	return 0;
}

EXPORT_SYMBOL(tty_unregister_driver);

dev_t tty_devnum(struct tty_struct *tty)
{
	return MKDEV(tty->driver->major, tty->driver->minor_start) + tty->index;
}
EXPORT_SYMBOL(tty_devnum);

void tty_default_fops(struct file_operations *fops)
{
	*fops = tty_fops;
}

/*
 * Initialize the console device. This is called *early*, so
 * we can't necessarily depend on lots of kernel help here.
 * Just do some early initializations, and do the complex setup
 * later.
 */
void __init console_init(void)
{
	initcall_t *call;

	/* Setup the default TTY line discipline. */
	tty_ldisc_begin();

	/*
	 * set up the console device so that later boot sequences can
	 * inform about problems etc..
	 */
	call = __con_initcall_start;
	while (call < __con_initcall_end) {
		(*call)();
		call++;
	}
}

static char *tty_devnode(struct device *dev, umode_t *mode)
{
	if (!mode)
		return NULL;
	if (dev->devt == MKDEV(TTYAUX_MAJOR, 0) ||
	    dev->devt == MKDEV(TTYAUX_MAJOR, 2))
		*mode = 0666;
	return NULL;
}

static int __init tty_class_init(void)
{
	tty_class = class_create(THIS_MODULE, "tty");
	if (IS_ERR(tty_class))
		return PTR_ERR(tty_class);
	tty_class->devnode = tty_devnode;
	return 0;
}

postcore_initcall(tty_class_init);

/* 3/2004 jmc: why do these devices exist? */
static struct cdev tty_cdev, console_cdev;

static ssize_t show_cons_active(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct console *cs[16];
	int i = 0;
	struct console *c;
	ssize_t count = 0;

	console_lock();
	for_each_console(c) {
		if (!c->device)
			continue;
		if (!c->write)
			continue;
		if ((c->flags & CON_ENABLED) == 0)
			continue;
		cs[i++] = c;
		if (i >= ARRAY_SIZE(cs))
			break;
	}
	while (i--) {
		int index = cs[i]->index;
		struct tty_driver *drv = cs[i]->device(cs[i], &index);

		/* don't resolve tty0 as some programs depend on it */
		if (drv && (cs[i]->index > 0 || drv->major != TTY_MAJOR))
			count += tty_line_name(drv, index, buf + count);
		else
			count += sprintf(buf + count, "%s%d",
					 cs[i]->name, cs[i]->index);

		count += sprintf(buf + count, "%c", i ? ' ':'\n');
	}
	console_unlock();

	return count;
}
static DEVICE_ATTR(active, S_IRUGO, show_cons_active, NULL);

static struct attribute *cons_dev_attrs[] = {
	&dev_attr_active.attr,
	NULL
};

ATTRIBUTE_GROUPS(cons_dev);

static struct device *consdev;

void console_sysfs_notify(void)
{
	if (consdev)
		sysfs_notify(&consdev->kobj, NULL, "active");
}

/*
 * Ok, now we can initialize the rest of the tty devices and can count
 * on memory allocations, interrupts etc..
 */
int __init tty_init(void)
{
	cdev_init(&tty_cdev, &tty_fops);
	if (cdev_add(&tty_cdev, MKDEV(TTYAUX_MAJOR, 0), 1) ||
	    register_chrdev_region(MKDEV(TTYAUX_MAJOR, 0), 1, "/dev/tty") < 0)
		panic("Couldn't register /dev/tty driver\n");
	device_create(tty_class, NULL, MKDEV(TTYAUX_MAJOR, 0), NULL, "tty");

	cdev_init(&console_cdev, &console_fops);
	if (cdev_add(&console_cdev, MKDEV(TTYAUX_MAJOR, 1), 1) ||
	    register_chrdev_region(MKDEV(TTYAUX_MAJOR, 1), 1, "/dev/console") < 0)
		panic("Couldn't register /dev/console driver\n");
	consdev = device_create_with_groups(tty_class, NULL,
					    MKDEV(TTYAUX_MAJOR, 1), NULL,
					    cons_dev_groups, "console");
	if (IS_ERR(consdev))
		consdev = NULL;

#ifdef CONFIG_VT
	vty_init(&console_fops);
#endif
	return 0;
}

