/*
 *  linux/drivers/char/tty_io.c
 *
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
#include <linux/smp_lock.h>
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

#undef TTY_DEBUG_HANGUP

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
static int tty_fasync(int fd, struct file *filp, int on);
static void release_tty(struct tty_struct *tty, int idx);
static void __proc_set_tty(struct task_struct *tsk, struct tty_struct *tty);
static void proc_set_tty(struct task_struct *tsk, struct tty_struct *tty);

/**
 *	alloc_tty_struct	-	allocate a tty object
 *
 *	Return a new empty tty structure. The data fields have not
 *	been initialized in any way but has been zeroed
 *
 *	Locking: none
 */

struct tty_struct *alloc_tty_struct(void)
{
	return kzalloc(sizeof(struct tty_struct), GFP_KERNEL);
}

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
	kfree(tty->write_buf);
	tty_buffer_free_all(tty);
	kfree(tty);
}

#define TTY_NUMBER(tty) ((tty)->index + (tty)->driver->name_base)

/**
 *	tty_name	-	return tty naming
 *	@tty: tty structure
 *	@buf: buffer for output
 *
 *	Convert a tty structure into a name. The name reflects the kernel
 *	naming policy and if udev is in use may not reflect user space
 *
 *	Locking: none
 */

char *tty_name(struct tty_struct *tty, char *buf)
{
	if (!tty) /* Hmm.  NULL pointer.  That's fun. */
		strcpy(buf, "NULL tty");
	else
		strcpy(buf, tty->name);
	return buf;
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

static int check_tty_count(struct tty_struct *tty, const char *routine)
{
#ifdef CHECK_TTY_COUNT
	struct list_head *p;
	int count = 0;

	file_list_lock();
	list_for_each(p, &tty->tty_files) {
		count++;
	}
	file_list_unlock();
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

		if (tty_line >= 0 && tty_line <= p->num && p->ops &&
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

int tty_check_change(struct tty_struct *tty)
{
	unsigned long flags;
	int ret = 0;

	if (current->signal->tty != tty)
		return 0;

	spin_lock_irqsave(&tty->ctrl_lock, flags);

	if (!tty->pgrp) {
		printk(KERN_WARNING "tty_check_change: tty->pgrp == NULL!\n");
		goto out_unlock;
	}
	if (task_pgrp(current) == tty->pgrp)
		goto out_unlock;
	spin_unlock_irqrestore(&tty->ctrl_lock, flags);
	if (is_ignored(SIGTTOU))
		goto out;
	if (is_current_pgrp_orphaned()) {
		ret = -EIO;
		goto out;
	}
	kill_pgrp(task_pgrp(current), SIGTTOU, 1);
	set_thread_flag(TIF_SIGPENDING);
	ret = -ERESTARTSYS;
out:
	return ret;
out_unlock:
	spin_unlock_irqrestore(&tty->ctrl_lock, flags);
	return ret;
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
 *	do_tty_hangup		-	actual handler for hangup events
 *	@work: tty device
 *
 *	This can be called by the "eventd" kernel thread.  That is process
 *	synchronous but doesn't hold any locks, so we need to make sure we
 *	have the appropriate locks for what we're doing.
 *
 *	The hangup event clears any pending redirections onto the hung up
 *	device. It ensures future writes will error and it does the needed
 *	line discipline hangup and signal delivery. The tty object itself
 *	remains intact.
 *
 *	Locking:
 *		BKL
 *		  redirect lock for undoing redirection
 *		  file list lock for manipulating list of ttys
 *		  tty_ldisc_lock from called functions
 *		  termios_mutex resetting termios data
 *		  tasklist_lock to walk task list for hangup event
 *		    ->siglock to protect ->signal/->sighand
 */
static void do_tty_hangup(struct work_struct *work)
{
	struct tty_struct *tty =
		container_of(work, struct tty_struct, hangup_work);
	struct file *cons_filp = NULL;
	struct file *filp, *f = NULL;
	struct task_struct *p;
	int    closecount = 0, n;
	unsigned long flags;
	int refs = 0;

	if (!tty)
		return;


	spin_lock(&redirect_lock);
	if (redirect && redirect->private_data == tty) {
		f = redirect;
		redirect = NULL;
	}
	spin_unlock(&redirect_lock);

	/* inuse_filps is protected by the single kernel lock */
	lock_kernel();
	check_tty_count(tty, "do_tty_hangup");

	file_list_lock();
	/* This breaks for file handles being sent over AF_UNIX sockets ? */
	list_for_each_entry(filp, &tty->tty_files, f_u.fu_list) {
		if (filp->f_op->write == redirected_tty_write)
			cons_filp = filp;
		if (filp->f_op->write != tty_write)
			continue;
		closecount++;
		tty_fasync(-1, filp, 0);	/* can't block */
		filp->f_op = &hung_up_tty_fops;
	}
	file_list_unlock();

	tty_ldisc_hangup(tty);

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
			spin_lock_irqsave(&tty->ctrl_lock, flags);
			if (tty->pgrp)
				p->signal->tty_old_pgrp = get_pid(tty->pgrp);
			spin_unlock_irqrestore(&tty->ctrl_lock, flags);
			spin_unlock_irq(&p->sighand->siglock);
		} while_each_pid_task(tty->session, PIDTYPE_SID, p);
	}
	read_unlock(&tasklist_lock);

	spin_lock_irqsave(&tty->ctrl_lock, flags);
	clear_bit(TTY_THROTTLED, &tty->flags);
	clear_bit(TTY_PUSH, &tty->flags);
	clear_bit(TTY_DO_WRITE_WAKEUP, &tty->flags);
	put_pid(tty->session);
	put_pid(tty->pgrp);
	tty->session = NULL;
	tty->pgrp = NULL;
	tty->ctrl_status = 0;
	set_bit(TTY_HUPPED, &tty->flags);
	spin_unlock_irqrestore(&tty->ctrl_lock, flags);

	/* Account for the p->signal references we killed */
	while (refs--)
		tty_kref_put(tty);

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
		(tty->ops->hangup)(tty);
	/*
	 * We don't want to have driver/ldisc interactions beyond
	 * the ones we did here. The driver layer expects no
	 * calls after ->hangup() from the ldisc side. However we
	 * can't yet guarantee all that.
	 */
	set_bit(TTY_HUPPED, &tty->flags);
	tty_ldisc_enable(tty);
	unlock_kernel();
	if (f)
		fput(f);
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
#ifdef TTY_DEBUG_HANGUP
	char	buf[64];
	printk(KERN_DEBUG "%s hangup...\n", tty_name(tty, buf));
#endif
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
#ifdef TTY_DEBUG_HANGUP
	char	buf[64];

	printk(KERN_DEBUG "%s vhangup...\n", tty_name(tty, buf));
#endif
	do_tty_hangup(&tty->hangup_work);
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

static void session_clear_tty(struct pid *session)
{
	struct task_struct *p;
	do_each_pid_task(session, PIDTYPE_SID, p) {
		proc_clear_tty(p);
	} while_each_pid_task(session, PIDTYPE_SID, p);
}

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
 *		BKL is taken for hysterical raisins
 *		  tty_mutex is taken to protect tty
 *		  ->siglock is taken to protect ->signal/->sighand
 *		  tasklist_lock is taken to walk process list for sessions
 *		    ->siglock is taken to protect ->signal/->sighand
 */

void disassociate_ctty(int on_exit)
{
	struct tty_struct *tty;
	struct pid *tty_pgrp = NULL;

	if (!current->signal->leader)
		return;

	tty = get_current_tty();
	if (tty) {
		tty_pgrp = get_pid(tty->pgrp);
		lock_kernel();
		if (on_exit && tty->driver->type != TTY_DRIVER_TYPE_PTY)
			tty_vhangup(tty);
		unlock_kernel();
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
	if (tty_pgrp) {
		kill_pgrp(tty_pgrp, SIGHUP, on_exit);
		if (!on_exit)
			kill_pgrp(tty_pgrp, SIGCONT, on_exit);
		put_pid(tty_pgrp);
	}

	spin_lock_irq(&current->sighand->siglock);
	put_pid(current->signal->tty_old_pgrp);
	current->signal->tty_old_pgrp = NULL;
	spin_unlock_irq(&current->sighand->siglock);

	tty = get_current_tty();
	if (tty) {
		unsigned long flags;
		spin_lock_irqsave(&tty->ctrl_lock, flags);
		put_pid(tty->session);
		put_pid(tty->pgrp);
		tty->session = NULL;
		tty->pgrp = NULL;
		spin_unlock_irqrestore(&tty->ctrl_lock, flags);
		tty_kref_put(tty);
	} else {
#ifdef TTY_DEBUG_HANGUP
		printk(KERN_DEBUG "error attempted to write to tty [0x%p]"
		       " = NULL", tty);
#endif
	}

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
	struct task_struct *tsk = current;
	lock_kernel();
	disassociate_ctty(0);
	unlock_kernel();
	proc_clear_tty(tsk);
}


/**
 *	stop_tty	-	propagate flow control
 *	@tty: tty to stop
 *
 *	Perform flow control to the driver. For PTY/TTY pairs we
 *	must also propagate the TIOCKPKT status. May be called
 *	on an already stopped device and will not re-call the driver
 *	method.
 *
 *	This functionality is used by both the line disciplines for
 *	halting incoming flow and by the driver. It may therefore be
 *	called from any context, may be under the tty atomic_write_lock
 *	but not always.
 *
 *	Locking:
 *		Uses the tty control lock internally
 */

void stop_tty(struct tty_struct *tty)
{
	unsigned long flags;
	spin_lock_irqsave(&tty->ctrl_lock, flags);
	if (tty->stopped) {
		spin_unlock_irqrestore(&tty->ctrl_lock, flags);
		return;
	}
	tty->stopped = 1;
	if (tty->link && tty->link->packet) {
		tty->ctrl_status &= ~TIOCPKT_START;
		tty->ctrl_status |= TIOCPKT_STOP;
		wake_up_interruptible_poll(&tty->link->read_wait, POLLIN);
	}
	spin_unlock_irqrestore(&tty->ctrl_lock, flags);
	if (tty->ops->stop)
		(tty->ops->stop)(tty);
}

EXPORT_SYMBOL(stop_tty);

/**
 *	start_tty	-	propagate flow control
 *	@tty: tty to start
 *
 *	Start a tty that has been stopped if at all possible. Perform
 *	any necessary wakeups and propagate the TIOCPKT status. If this
 *	is the tty was previous stopped and is being started then the
 *	driver start method is invoked and the line discipline woken.
 *
 *	Locking:
 *		ctrl_lock
 */

void start_tty(struct tty_struct *tty)
{
	unsigned long flags;
	spin_lock_irqsave(&tty->ctrl_lock, flags);
	if (!tty->stopped || tty->flow_stopped) {
		spin_unlock_irqrestore(&tty->ctrl_lock, flags);
		return;
	}
	tty->stopped = 0;
	if (tty->link && tty->link->packet) {
		tty->ctrl_status &= ~TIOCPKT_STOP;
		tty->ctrl_status |= TIOCPKT_START;
		wake_up_interruptible_poll(&tty->link->read_wait, POLLIN);
	}
	spin_unlock_irqrestore(&tty->ctrl_lock, flags);
	if (tty->ops->start)
		(tty->ops->start)(tty);
	/* If we have a running line discipline it may need kicking */
	tty_wakeup(tty);
}

EXPORT_SYMBOL(start_tty);

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
	struct tty_struct *tty;
	struct inode *inode;
	struct tty_ldisc *ld;

	tty = (struct tty_struct *)file->private_data;
	inode = file->f_path.dentry->d_inode;
	if (tty_paranoia_check(tty, inode, "tty_read"))
		return -EIO;
	if (!tty || (test_bit(TTY_IO_ERROR, &tty->flags)))
		return -EIO;

	/* We want to wait for the line discipline to sort out in this
	   situation */
	ld = tty_ldisc_ref_wait(tty);
	if (ld->ops->read)
		i = (ld->ops->read)(tty, file, buf, count);
	else
		i = -EIO;
	tty_ldisc_deref(ld);
	if (i > 0)
		inode->i_atime = current_fs_time(inode->i_sb);
	return i;
}

void tty_write_unlock(struct tty_struct *tty)
{
	mutex_unlock(&tty->atomic_write_lock);
	wake_up_interruptible_poll(&tty->write_wait, POLLOUT);
}

int tty_write_lock(struct tty_struct *tty, int ndelay)
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
		struct inode *inode = file->f_path.dentry->d_inode;
		inode->i_mtime = current_fs_time(inode->i_sb);
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
 * We must still hold the BKL and test the CLOSING flag for the moment.
 */

void tty_write_message(struct tty_struct *tty, char *msg)
{
	if (tty) {
		mutex_lock(&tty->atomic_write_lock);
		lock_kernel();
		if (tty->ops->write && !test_bit(TTY_CLOSING, &tty->flags)) {
			unlock_kernel();
			tty->ops->write(tty, msg, strlen(msg));
		} else
			unlock_kernel();
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
	struct tty_struct *tty;
	struct inode *inode = file->f_path.dentry->d_inode;
	ssize_t ret;
	struct tty_ldisc *ld;

	tty = (struct tty_struct *)file->private_data;
	if (tty_paranoia_check(tty, inode, "tty_write"))
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
	if (redirect) {
		get_file(redirect);
		p = redirect;
	}
	spin_unlock(&redirect_lock);

	if (p) {
		ssize_t res;
		res = vfs_write(p, buf, count, &p->f_pos);
		fput(p);
		return res;
	}
	return tty_write(file, buf, count, ppos);
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
static void tty_line_name(struct tty_driver *driver, int index, char *p)
{
	sprintf(p, "%s%d", driver->name, index + driver->name_base);
}

/**
 *	tty_driver_lookup_tty() - find an existing tty, if any
 *	@driver: the driver for the tty
 *	@idx:	 the minor number
 *
 *	Return the tty, if found or ERR_PTR() otherwise.
 *
 *	Locking: tty_mutex must be held. If tty is found, the mutex must
 *	be held until the 'fast-open' is also done. Will change once we
 *	have refcounting in the driver and per driver locking
 */
static struct tty_struct *tty_driver_lookup_tty(struct tty_driver *driver,
		struct inode *inode, int idx)
{
	struct tty_struct *tty;

	if (driver->ops->lookup)
		return driver->ops->lookup(driver, inode, idx);

	tty = driver->ttys[idx];
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

	tp = tty->driver->termios[idx];
	if (tp == NULL) {
		tp = kzalloc(sizeof(struct ktermios[2]), GFP_KERNEL);
		if (tp == NULL)
			return -ENOMEM;
		memcpy(tp, &tty->driver->init_termios,
						sizeof(struct ktermios));
		tty->driver->termios[idx] = tp;
	}
	tty->termios = tp;
	tty->termios_locked = tp + 1;

	/* Compatibility until drivers always set this */
	tty->termios->c_ispeed = tty_termios_input_baud_rate(tty->termios);
	tty->termios->c_ospeed = tty_termios_baud_rate(tty->termios);
	return 0;
}
EXPORT_SYMBOL_GPL(tty_init_termios);

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
	int idx = tty->index;
	int ret;

	if (driver->ops->install) {
		lock_kernel();
		ret = driver->ops->install(driver, tty);
		unlock_kernel();
		return ret;
	}

	if (tty_init_termios(tty) == 0) {
		lock_kernel();
		tty_driver_kref_get(driver);
		tty->count++;
		driver->ttys[idx] = tty;
		unlock_kernel();
		return 0;
	}
	return -ENOMEM;
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
static void tty_driver_remove_tty(struct tty_driver *driver,
						struct tty_struct *tty)
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
 *
 *	Locking: tty_mutex must be held from the time the tty was found
 *		 till this open completes.
 */
static int tty_reopen(struct tty_struct *tty)
{
	struct tty_driver *driver = tty->driver;

	if (test_bit(TTY_CLOSING, &tty->flags))
		return -EIO;

	if (driver->type == TTY_DRIVER_TYPE_PTY &&
	    driver->subtype == PTY_TYPE_MASTER) {
		/*
		 * special case for PTY masters: only one open permitted,
		 * and the slave side open count is incremented as well.
		 */
		if (tty->count)
			return -EIO;

		tty->link->count++;
	}
	tty->count++;
	tty->driver = driver; /* N.B. why do this every time?? */

	mutex_lock(&tty->ldisc_mutex);
	WARN_ON(!test_bit(TTY_LDISC, &tty->flags));
	mutex_unlock(&tty->ldisc_mutex);

	return 0;
}

/**
 *	tty_init_dev		-	initialise a tty device
 *	@driver: tty driver we are opening a device on
 *	@idx: device index
 *	@ret_tty: returned tty structure
 *	@first_ok: ok to open a new device (used by ptmx)
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

struct tty_struct *tty_init_dev(struct tty_driver *driver, int idx,
								int first_ok)
{
	struct tty_struct *tty;
	int retval;

	lock_kernel();
	/* Check if pty master is being opened multiple times */
	if (driver->subtype == PTY_TYPE_MASTER &&
		(driver->flags & TTY_DRIVER_DEVPTS_MEM) && !first_ok) {
		unlock_kernel();
		return ERR_PTR(-EIO);
	}
	unlock_kernel();

	/*
	 * First time open is complex, especially for PTY devices.
	 * This code guarantees that either everything succeeds and the
	 * TTY is ready for operation, or else the table slots are vacated
	 * and the allocated memory released.  (Except that the termios
	 * and locked termios may be retained.)
	 */

	if (!try_module_get(driver->owner))
		return ERR_PTR(-ENODEV);

	tty = alloc_tty_struct();
	if (!tty)
		goto fail_no_mem;
	initialize_tty_struct(tty, driver, idx);

	retval = tty_driver_install_tty(driver, tty);
	if (retval < 0) {
		free_tty_struct(tty);
		module_put(driver->owner);
		return ERR_PTR(retval);
	}

	/*
	 * Structures all installed ... call the ldisc open routines.
	 * If we fail here just call release_tty to clean up.  No need
	 * to decrement the use counts, as release_tty doesn't care.
	 */
	retval = tty_ldisc_setup(tty, tty->link);
	if (retval)
		goto release_mem_out;
	return tty;

fail_no_mem:
	module_put(driver->owner);
	return ERR_PTR(-ENOMEM);

	/* call the tty release_tty routine to clean out this slot */
release_mem_out:
	if (printk_ratelimit())
		printk(KERN_INFO "tty_init_dev: ldisc open failed, "
				 "clearing slot %d\n", idx);
	lock_kernel();
	release_tty(tty, idx);
	unlock_kernel();
	return ERR_PTR(retval);
}

void tty_free_termios(struct tty_struct *tty)
{
	struct ktermios *tp;
	int idx = tty->index;
	/* Kill this flag and push into drivers for locking etc */
	if (tty->driver->flags & TTY_DRIVER_RESET_TERMIOS) {
		/* FIXME: Locking on ->termios array */
		tp = tty->termios;
		tty->driver->termios[idx] = NULL;
		kfree(tp);
	}
}
EXPORT_SYMBOL(tty_free_termios);

void tty_shutdown(struct tty_struct *tty)
{
	tty_driver_remove_tty(tty->driver, tty);
	tty_free_termios(tty);
}
EXPORT_SYMBOL(tty_shutdown);

/**
 *	release_one_tty		-	release tty structure memory
 *	@kref: kref of tty we are obliterating
 *
 *	Releases memory associated with a tty structure, and clears out the
 *	driver table slots. This function is called when a device is no longer
 *	in use. It also gets called when setup of a device fails.
 *
 *	Locking:
 *		tty_mutex - sometimes only
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

	if (tty->ops->cleanup)
		tty->ops->cleanup(tty);

	tty->magic = 0;
	tty_driver_kref_put(driver);
	module_put(driver->owner);

	file_list_lock();
	list_del_init(&tty->tty_files);
	file_list_unlock();

	put_pid(tty->pgrp);
	put_pid(tty->session);
	free_tty_struct(tty);
}

static void queue_release_one_tty(struct kref *kref)
{
	struct tty_struct *tty = container_of(kref, struct tty_struct, kref);

	if (tty->ops->shutdown)
		tty->ops->shutdown(tty);
	else
		tty_shutdown(tty);

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
 *		tty_mutex - sometimes only
 *		takes the file list lock internally when working on the list
 *	of ttys that the driver keeps.
 *		FIXME: should we require tty_mutex is held here ??
 *
 */
static void release_tty(struct tty_struct *tty, int idx)
{
	/* This should always be true but check for the moment */
	WARN_ON(tty->index != idx);

	if (tty->link)
		tty_kref_put(tty->link);
	tty_kref_put(tty);
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
	struct tty_struct *tty, *o_tty;
	int	pty_master, tty_closing, o_tty_closing, do_sleep;
	int	devpts;
	int	idx;
	char	buf[64];

	tty = (struct tty_struct *)filp->private_data;
	if (tty_paranoia_check(tty, inode, "tty_release_dev"))
		return 0;

	lock_kernel();
	check_tty_count(tty, "tty_release_dev");

	tty_fasync(-1, filp, 0);

	idx = tty->index;
	pty_master = (tty->driver->type == TTY_DRIVER_TYPE_PTY &&
		      tty->driver->subtype == PTY_TYPE_MASTER);
	devpts = (tty->driver->flags & TTY_DRIVER_DEVPTS_MEM) != 0;
	o_tty = tty->link;

#ifdef TTY_PARANOIA_CHECK
	if (idx < 0 || idx >= tty->driver->num) {
		printk(KERN_DEBUG "tty_release_dev: bad idx when trying to "
				  "free (%s)\n", tty->name);
		unlock_kernel();
		return 0;
	}
	if (!devpts) {
		if (tty != tty->driver->ttys[idx]) {
			unlock_kernel();
			printk(KERN_DEBUG "tty_release_dev: driver.table[%d] not tty "
			       "for (%s)\n", idx, tty->name);
			return 0;
		}
		if (tty->termios != tty->driver->termios[idx]) {
			unlock_kernel();
			printk(KERN_DEBUG "tty_release_dev: driver.termios[%d] not termios "
			       "for (%s)\n",
			       idx, tty->name);
			return 0;
		}
	}
#endif

#ifdef TTY_DEBUG_HANGUP
	printk(KERN_DEBUG "tty_release_dev of %s (tty count=%d)...",
	       tty_name(tty, buf), tty->count);
#endif

#ifdef TTY_PARANOIA_CHECK
	if (tty->driver->other &&
	     !(tty->driver->flags & TTY_DRIVER_DEVPTS_MEM)) {
		if (o_tty != tty->driver->other->ttys[idx]) {
			unlock_kernel();
			printk(KERN_DEBUG "tty_release_dev: other->table[%d] "
					  "not o_tty for (%s)\n",
			       idx, tty->name);
			return 0 ;
		}
		if (o_tty->termios != tty->driver->other->termios[idx]) {
			unlock_kernel();
			printk(KERN_DEBUG "tty_release_dev: other->termios[%d] "
					  "not o_termios for (%s)\n",
			       idx, tty->name);
			return 0;
		}
		if (o_tty->link != tty) {
			unlock_kernel();
			printk(KERN_DEBUG "tty_release_dev: bad pty pointers\n");
			return 0;
		}
	}
#endif
	if (tty->ops->close)
		tty->ops->close(tty, filp);

	unlock_kernel();
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
	 * Thus this test wouldn't be triggered at the time the slave closes,
	 * so we do it now.
	 *
	 * Note that it's possible for the tty to be opened again while we're
	 * flushing out waiters.  By recalculating the closing flags before
	 * each iteration we avoid any problems.
	 */
	while (1) {
		/* Guard against races with tty->count changes elsewhere and
		   opens on /dev/tty */

		mutex_lock(&tty_mutex);
		lock_kernel();
		tty_closing = tty->count <= 1;
		o_tty_closing = o_tty &&
			(o_tty->count <= (pty_master ? 1 : 0));
		do_sleep = 0;

		if (tty_closing) {
			if (waitqueue_active(&tty->read_wait)) {
				wake_up_poll(&tty->read_wait, POLLIN);
				do_sleep++;
			}
			if (waitqueue_active(&tty->write_wait)) {
				wake_up_poll(&tty->write_wait, POLLOUT);
				do_sleep++;
			}
		}
		if (o_tty_closing) {
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

		printk(KERN_WARNING "tty_release_dev: %s: read/write wait queue "
				    "active!\n", tty_name(tty, buf));
		unlock_kernel();
		mutex_unlock(&tty_mutex);
		schedule();
	}

	/*
	 * The closing flags are now consistent with the open counts on
	 * both sides, and we've completed the last operation that could
	 * block, so it's safe to proceed with closing.
	 */
	if (pty_master) {
		if (--o_tty->count < 0) {
			printk(KERN_WARNING "tty_release_dev: bad pty slave count "
					    "(%d) for %s\n",
			       o_tty->count, tty_name(o_tty, buf));
			o_tty->count = 0;
		}
	}
	if (--tty->count < 0) {
		printk(KERN_WARNING "tty_release_dev: bad tty->count (%d) for %s\n",
		       tty->count, tty_name(tty, buf));
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
	file_kill(filp);
	filp->private_data = NULL;

	/*
	 * Perform some housekeeping before deciding whether to return.
	 *
	 * Set the TTY_CLOSING flag if this was the last open.  In the
	 * case of a pty we may have to wait around for the other side
	 * to close, and TTY_CLOSING makes sure we can't be reopened.
	 */
	if (tty_closing)
		set_bit(TTY_CLOSING, &tty->flags);
	if (o_tty_closing)
		set_bit(TTY_CLOSING, &o_tty->flags);

	/*
	 * If _either_ side is closing, make sure there aren't any
	 * processes that still think tty or o_tty is their controlling
	 * tty.
	 */
	if (tty_closing || o_tty_closing) {
		read_lock(&tasklist_lock);
		session_clear_tty(tty->session);
		if (o_tty)
			session_clear_tty(o_tty->session);
		read_unlock(&tasklist_lock);
	}

	mutex_unlock(&tty_mutex);

	/* check whether both sides are closing ... */
	if (!tty_closing || (o_tty && !o_tty_closing)) {
		unlock_kernel();
		return 0;
	}

#ifdef TTY_DEBUG_HANGUP
	printk(KERN_DEBUG "freeing tty structure...");
#endif
	/*
	 * Ask the line discipline code to release its structures
	 */
	tty_ldisc_release(tty, o_tty);
	/*
	 * The release_tty function takes care of the details of clearing
	 * the slots and preserving the termios structure.
	 */
	release_tty(tty, idx);

	/* Make this pty number available for reallocation */
	if (devpts)
		devpts_kill_index(inode, idx);
	unlock_kernel();
	return 0;
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
 *	Locking: tty_mutex protects tty, get_tty_driver and tty_init_dev work.
 *		 tty->count should protect the rest.
 *		 ->siglock protects ->signal/->sighand
 */

static int tty_open(struct inode *inode, struct file *filp)
{
	struct tty_struct *tty = NULL;
	int noctty, retval;
	struct tty_driver *driver;
	int index;
	dev_t device = inode->i_rdev;
	unsigned saved_flags = filp->f_flags;

	nonseekable_open(inode, filp);

retry_open:
	noctty = filp->f_flags & O_NOCTTY;
	index  = -1;
	retval = 0;

	mutex_lock(&tty_mutex);
	lock_kernel();

	if (device == MKDEV(TTYAUX_MAJOR, 0)) {
		tty = get_current_tty();
		if (!tty) {
			unlock_kernel();
			mutex_unlock(&tty_mutex);
			return -ENXIO;
		}
		driver = tty_driver_kref_get(tty->driver);
		index = tty->index;
		filp->f_flags |= O_NONBLOCK; /* Don't let /dev/tty block */
		/* noctty = 1; */
		/* FIXME: Should we take a driver reference ? */
		tty_kref_put(tty);
		goto got_driver;
	}
#ifdef CONFIG_VT
	if (device == MKDEV(TTY_MAJOR, 0)) {
		extern struct tty_driver *console_driver;
		driver = tty_driver_kref_get(console_driver);
		index = fg_console;
		noctty = 1;
		goto got_driver;
	}
#endif
	if (device == MKDEV(TTYAUX_MAJOR, 1)) {
		struct tty_driver *console_driver = console_device(&index);
		if (console_driver) {
			driver = tty_driver_kref_get(console_driver);
			if (driver) {
				/* Don't let /dev/console block */
				filp->f_flags |= O_NONBLOCK;
				noctty = 1;
				goto got_driver;
			}
		}
		unlock_kernel();
		mutex_unlock(&tty_mutex);
		return -ENODEV;
	}

	driver = get_tty_driver(device, &index);
	if (!driver) {
		unlock_kernel();
		mutex_unlock(&tty_mutex);
		return -ENODEV;
	}
got_driver:
	if (!tty) {
		/* check whether we're reopening an existing tty */
		tty = tty_driver_lookup_tty(driver, inode, index);

		if (IS_ERR(tty)) {
			unlock_kernel();
			mutex_unlock(&tty_mutex);
			return PTR_ERR(tty);
		}
	}

	if (tty) {
		retval = tty_reopen(tty);
		if (retval)
			tty = ERR_PTR(retval);
	} else
		tty = tty_init_dev(driver, index, 0);

	mutex_unlock(&tty_mutex);
	tty_driver_kref_put(driver);
	if (IS_ERR(tty)) {
		unlock_kernel();
		return PTR_ERR(tty);
	}

	filp->private_data = tty;
	file_move(filp, &tty->tty_files);
	check_tty_count(tty, "tty_open");
	if (tty->driver->type == TTY_DRIVER_TYPE_PTY &&
	    tty->driver->subtype == PTY_TYPE_MASTER)
		noctty = 1;
#ifdef TTY_DEBUG_HANGUP
	printk(KERN_DEBUG "opening %s...", tty->name);
#endif
	if (!retval) {
		if (tty->ops->open)
			retval = tty->ops->open(tty, filp);
		else
			retval = -ENODEV;
	}
	filp->f_flags = saved_flags;

	if (!retval && test_bit(TTY_EXCLUSIVE, &tty->flags) &&
						!capable(CAP_SYS_ADMIN))
		retval = -EBUSY;

	if (retval) {
#ifdef TTY_DEBUG_HANGUP
		printk(KERN_DEBUG "error %d in opening %s...", retval,
		       tty->name);
#endif
		tty_release(inode, filp);
		if (retval != -ERESTARTSYS) {
			unlock_kernel();
			return retval;
		}
		if (signal_pending(current)) {
			unlock_kernel();
			return retval;
		}
		schedule();
		/*
		 * Need to reset f_op in case a hangup happened.
		 */
		if (filp->f_op == &hung_up_tty_fops)
			filp->f_op = &tty_fops;
		unlock_kernel();
		goto retry_open;
	}
	unlock_kernel();


	mutex_lock(&tty_mutex);
	lock_kernel();
	spin_lock_irq(&current->sighand->siglock);
	if (!noctty &&
	    current->signal->leader &&
	    !current->signal->tty &&
	    tty->session == NULL)
		__proc_set_tty(current, tty);
	spin_unlock_irq(&current->sighand->siglock);
	unlock_kernel();
	mutex_unlock(&tty_mutex);
	return 0;
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
	struct tty_struct *tty;
	struct tty_ldisc *ld;
	int ret = 0;

	tty = (struct tty_struct *)filp->private_data;
	if (tty_paranoia_check(tty, filp->f_path.dentry->d_inode, "tty_poll"))
		return 0;

	ld = tty_ldisc_ref_wait(tty);
	if (ld->ops->poll)
		ret = (ld->ops->poll)(tty, filp, wait);
	tty_ldisc_deref(ld);
	return ret;
}

static int tty_fasync(int fd, struct file *filp, int on)
{
	struct tty_struct *tty;
	unsigned long flags;
	int retval = 0;

	lock_kernel();
	tty = (struct tty_struct *)filp->private_data;
	if (tty_paranoia_check(tty, filp->f_path.dentry->d_inode, "tty_fasync"))
		goto out;

	retval = fasync_helper(fd, filp, on, &tty->fasync);
	if (retval <= 0)
		goto out;

	if (on) {
		enum pid_type type;
		struct pid *pid;
		if (!waitqueue_active(&tty->read_wait))
			tty->minimum_to_wake = 1;
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
		retval = __f_setown(filp, pid, type, 0);
		put_pid(pid);
		if (retval)
			goto out;
	} else {
		if (!tty->fasync && !waitqueue_active(&tty->read_wait))
			tty->minimum_to_wake = N_TTY_BUF_SIZE;
	}
	retval = 0;
out:
	unlock_kernel();
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
 *		Called functions take tty_ldisc_lock
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
 *	Locking: tty->termios_mutex is taken to ensure the winsize data
 *		is consistent.
 */

static int tiocgwinsz(struct tty_struct *tty, struct winsize __user *arg)
{
	int err;

	mutex_lock(&tty->termios_mutex);
	err = copy_to_user(arg, &tty->winsize, sizeof(*arg));
	mutex_unlock(&tty->termios_mutex);

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
	unsigned long flags;

	/* Lock the tty */
	mutex_lock(&tty->termios_mutex);
	if (!memcmp(ws, &tty->winsize, sizeof(*ws)))
		goto done;
	/* Get the PID values and reference them so we can
	   avoid holding the tty ctrl lock while sending signals */
	spin_lock_irqsave(&tty->ctrl_lock, flags);
	pgrp = get_pid(tty->pgrp);
	spin_unlock_irqrestore(&tty->ctrl_lock, flags);

	if (pgrp)
		kill_pgrp(pgrp, SIGWINCH, 1);
	put_pid(pgrp);

	tty->winsize = *ws;
done:
	mutex_unlock(&tty->termios_mutex);
	return 0;
}

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
 *		Driver dependant. The default do_resize method takes the
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
 *	Allow the adminstrator to move the redirected console device
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
	get_file(file);
	redirect = file;
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
 *		Takes tty_mutex() to protect tty instance
 *		Takes tasklist_lock internally to walk sessions
 *		Takes ->siglock() when updating signal->tty
 */

static int tiocsctty(struct tty_struct *tty, int arg)
{
	int ret = 0;
	if (current->signal->leader && (task_session(current) == tty->session))
		return ret;

	mutex_lock(&tty_mutex);
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
			read_lock(&tasklist_lock);
			session_clear_tty(tty->session);
			read_unlock(&tasklist_lock);
		} else {
			ret = -EPERM;
			goto unlock;
		}
	}
	proc_set_tty(current, tty);
unlock:
	mutex_unlock(&tty_mutex);
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

/**
 *	tiocgpgrp		-	get process group
 *	@tty: tty passed by user
 *	@real_tty: tty side of the tty pased by the user if a pty else the tty
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
	unsigned long flags;

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
	spin_lock_irqsave(&tty->ctrl_lock, flags);
	put_pid(real_tty->pgrp);
	real_tty->pgrp = get_pid(pgrp);
	spin_unlock_irqrestore(&tty->ctrl_lock, flags);
out_unlock:
	rcu_read_unlock();
	return retval;
}

/**
 *	tiocgsid		-	get session id
 *	@tty: tty passed by user
 *	@real_tty: tty side of the tty pased by the user if a pty else the tty
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

static int tty_tiocmget(struct tty_struct *tty, struct file *file, int __user *p)
{
	int retval = -EINVAL;

	if (tty->ops->tiocmget) {
		retval = tty->ops->tiocmget(tty, file);

		if (retval >= 0)
			retval = put_user(retval, p);
	}
	return retval;
}

/**
 *	tty_tiocmset		-	set modem status
 *	@tty: tty device
 *	@file: user file pointer
 *	@cmd: command - clear bits, set bits or set all
 *	@p: pointer to desired bits
 *
 *	Set the modem status bits from the tty driver if the feature
 *	is supported. Return -EINVAL if it is not available.
 *
 *	Locking: none (up to the driver)
 */

static int tty_tiocmset(struct tty_struct *tty, struct file *file, unsigned int cmd,
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
	return tty->ops->tiocmset(tty, file, set, clear);
}

struct tty_struct *tty_pair_get_tty(struct tty_struct *tty)
{
	if (tty->driver->type == TTY_DRIVER_TYPE_PTY &&
	    tty->driver->subtype == PTY_TYPE_MASTER)
		tty = tty->link;
	return tty;
}
EXPORT_SYMBOL(tty_pair_get_tty);

struct tty_struct *tty_pair_get_pty(struct tty_struct *tty)
{
	if (tty->driver->type == TTY_DRIVER_TYPE_PTY &&
	    tty->driver->subtype == PTY_TYPE_MASTER)
	    return tty;
	return tty->link;
}
EXPORT_SYMBOL(tty_pair_get_pty);

/*
 * Split this up, as gcc can choke on it otherwise..
 */
long tty_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct tty_struct *tty, *real_tty;
	void __user *p = (void __user *)arg;
	int retval;
	struct tty_ldisc *ld;
	struct inode *inode = file->f_dentry->d_inode;

	tty = (struct tty_struct *)file->private_data;
	if (tty_paranoia_check(tty, inode, "tty_ioctl"))
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
	case TIOCNOTTY:
		if (current->signal->tty != tty)
			return -ENOTTY;
		no_tty();
		return 0;
	case TIOCSCTTY:
		return tiocsctty(tty, arg);
	case TIOCGPGRP:
		return tiocgpgrp(tty, real_tty, p);
	case TIOCSPGRP:
		return tiocspgrp(tty, real_tty, p);
	case TIOCGSID:
		return tiocgsid(tty, real_tty, p);
	case TIOCGETD:
		return put_user(tty->ldisc->ops->num, (int __user *)p);
	case TIOCSETD:
		return tiocsetd(tty, p);
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
		return tty_tiocmget(tty, file, p);
	case TIOCMSET:
	case TIOCMBIC:
	case TIOCMBIS:
		return tty_tiocmset(tty, file, cmd, p);
	case TCFLSH:
		switch (arg) {
		case TCIFLUSH:
		case TCIOFLUSH:
		/* flush tty buffer and allow ldisc to process ioctl */
			tty_buffer_flush(tty);
			break;
		}
		break;
	}
	if (tty->ops->ioctl) {
		retval = (tty->ops->ioctl)(tty, file, cmd, arg);
		if (retval != -ENOIOCTLCMD)
			return retval;
	}
	ld = tty_ldisc_ref_wait(tty);
	retval = -EINVAL;
	if (ld->ops->ioctl) {
		retval = ld->ops->ioctl(tty, file, cmd, arg);
		if (retval == -ENOIOCTLCMD)
			retval = -EINVAL;
	}
	tty_ldisc_deref(ld);
	return retval;
}

#ifdef CONFIG_COMPAT
static long tty_compat_ioctl(struct file *file, unsigned int cmd,
				unsigned long arg)
{
	struct inode *inode = file->f_dentry->d_inode;
	struct tty_struct *tty = file->private_data;
	struct tty_ldisc *ld;
	int retval = -ENOIOCTLCMD;

	if (tty_paranoia_check(tty, inode, "tty_ioctl"))
		return -EINVAL;

	if (tty->ops->compat_ioctl) {
		retval = (tty->ops->compat_ioctl)(tty, file, cmd, arg);
		if (retval != -ENOIOCTLCMD)
			return retval;
	}

	ld = tty_ldisc_ref_wait(tty);
	if (ld->ops->compat_ioctl)
		retval = ld->ops->compat_ioctl(tty, file, cmd, arg);
	tty_ldisc_deref(ld);

	return retval;
}
#endif

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
	struct file	*filp;
	struct fdtable *fdt;

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
		if (p->files) {
			/*
			 * We don't take a ref to the file, so we must
			 * hold ->file_lock instead.
			 */
			spin_lock(&p->files->file_lock);
			fdt = files_fdtable(p->files);
			for (i = 0; i < fdt->max_fds; i++) {
				filp = fcheck_files(p->files, i);
				if (!filp)
					continue;
				if (filp->f_op->read == tty_read &&
				    filp->private_data == tty) {
					printk(KERN_NOTICE "SAK: killed process %d"
					    " (%s): fd#%d opened to the tty\n",
					    task_pid_nr(p), p->comm, i);
					force_sig(SIGKILL, p);
					break;
				}
			}
			spin_unlock(&p->files->file_lock);
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

/**
 *	initialize_tty_struct
 *	@tty: tty to initialize
 *
 *	This subroutine initializes a tty structure that has been newly
 *	allocated.
 *
 *	Locking: none - tty in question must not be exposed at this point
 */

void initialize_tty_struct(struct tty_struct *tty,
		struct tty_driver *driver, int idx)
{
	memset(tty, 0, sizeof(struct tty_struct));
	kref_init(&tty->kref);
	tty->magic = TTY_MAGIC;
	tty_ldisc_init(tty);
	tty->session = NULL;
	tty->pgrp = NULL;
	tty->overrun_time = jiffies;
	tty->buf.head = tty->buf.tail = NULL;
	tty_buffer_init(tty);
	mutex_init(&tty->termios_mutex);
	mutex_init(&tty->ldisc_mutex);
	init_waitqueue_head(&tty->write_wait);
	init_waitqueue_head(&tty->read_wait);
	INIT_WORK(&tty->hangup_work, do_tty_hangup);
	mutex_init(&tty->atomic_read_lock);
	mutex_init(&tty->atomic_write_lock);
	mutex_init(&tty->output_lock);
	mutex_init(&tty->echo_lock);
	spin_lock_init(&tty->read_lock);
	spin_lock_init(&tty->ctrl_lock);
	INIT_LIST_HEAD(&tty->tty_files);
	INIT_WORK(&tty->SAK_work, do_SAK_work);

	tty->driver = driver;
	tty->ops = driver->ops;
	tty->index = idx;
	tty_line_name(driver, idx, tty->name);
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
	char name[64];
	dev_t dev = MKDEV(driver->major, driver->minor_start) + index;

	if (index >= driver->num) {
		printk(KERN_ERR "Attempt to register invalid tty line number "
		       " (%d).\n", index);
		return ERR_PTR(-EINVAL);
	}

	if (driver->type == TTY_DRIVER_TYPE_PTY)
		pty_line_name(driver, index, name);
	else
		tty_line_name(driver, index, name);

	return device_create(tty_class, device, dev, NULL, name);
}
EXPORT_SYMBOL(tty_register_device);

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
}
EXPORT_SYMBOL(tty_unregister_device);

struct tty_driver *alloc_tty_driver(int lines)
{
	struct tty_driver *driver;

	driver = kzalloc(sizeof(struct tty_driver), GFP_KERNEL);
	if (driver) {
		kref_init(&driver->kref);
		driver->magic = TTY_DRIVER_MAGIC;
		driver->num = lines;
		/* later we'll move allocation of tables here */
	}
	return driver;
}
EXPORT_SYMBOL(alloc_tty_driver);

static void destruct_tty_driver(struct kref *kref)
{
	struct tty_driver *driver = container_of(kref, struct tty_driver, kref);
	int i;
	struct ktermios *tp;
	void *p;

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
		p = driver->ttys;
		proc_tty_unregister_driver(driver);
		driver->ttys = NULL;
		driver->termios = NULL;
		kfree(p);
		cdev_del(&driver->cdev);
	}
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
	void **p = NULL;

	if (!(driver->flags & TTY_DRIVER_DEVPTS_MEM) && driver->num) {
		p = kzalloc(driver->num * 2 * sizeof(void *), GFP_KERNEL);
		if (!p)
			return -ENOMEM;
	}

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
	if (error < 0) {
		kfree(p);
		return error;
	}

	if (p) {
		driver->ttys = (struct tty_struct **)p;
		driver->termios = (struct ktermios **)(p + driver->num);
	} else {
		driver->ttys = NULL;
		driver->termios = NULL;
	}

	cdev_init(&driver->cdev, &tty_fops);
	driver->cdev.owner = driver->owner;
	error = cdev_add(&driver->cdev, dev, driver->num);
	if (error) {
		unregister_chrdev_region(dev, driver->num);
		driver->ttys = NULL;
		driver->termios = NULL;
		kfree(p);
		return error;
	}

	mutex_lock(&tty_mutex);
	list_add(&driver->tty_drivers, &tty_drivers);
	mutex_unlock(&tty_mutex);

	if (!(driver->flags & TTY_DRIVER_DYNAMIC_DEV)) {
		for (i = 0; i < driver->num; i++)
		    tty_register_device(driver, i, NULL);
	}
	proc_tty_register_driver(driver);
	driver->flags |= TTY_DRIVER_INSTALLED;
	return 0;
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

/* Called under the sighand lock */

static void __proc_set_tty(struct task_struct *tsk, struct tty_struct *tty)
{
	if (tty) {
		unsigned long flags;
		/* We should not have a session or pgrp to put here but.... */
		spin_lock_irqsave(&tty->ctrl_lock, flags);
		put_pid(tty->session);
		put_pid(tty->pgrp);
		tty->pgrp = get_pid(task_pgrp(tsk));
		spin_unlock_irqrestore(&tty->ctrl_lock, flags);
		tty->session = get_pid(task_session(tsk));
		if (tsk->signal->tty) {
			printk(KERN_DEBUG "tty not NULL!!\n");
			tty_kref_put(tsk->signal->tty);
		}
	}
	put_pid(tsk->signal->tty_old_pgrp);
	tsk->signal->tty = tty_kref_get(tty);
	tsk->signal->tty_old_pgrp = NULL;
}

static void proc_set_tty(struct task_struct *tsk, struct tty_struct *tty)
{
	spin_lock_irq(&tsk->sighand->siglock);
	__proc_set_tty(tsk, tty);
	spin_unlock_irq(&tsk->sighand->siglock);
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

static char *tty_devnode(struct device *dev, mode_t *mode)
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
	device_create(tty_class, NULL, MKDEV(TTYAUX_MAJOR, 0), NULL,
			      "tty");

	cdev_init(&console_cdev, &console_fops);
	if (cdev_add(&console_cdev, MKDEV(TTYAUX_MAJOR, 1), 1) ||
	    register_chrdev_region(MKDEV(TTYAUX_MAJOR, 1), 1, "/dev/console") < 0)
		panic("Couldn't register /dev/console driver\n");
	device_create(tty_class, NULL, MKDEV(TTYAUX_MAJOR, 1), NULL,
			      "console");

#ifdef CONFIG_VT
	vty_init(&console_fops);
#endif
	return 0;
}

