/*
 *  linux/drivers/char/pty.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  Added support for a Unix98-style ptmx device.
 *    -- C. Scott Ananian <cananian@alumni.princeton.edu>, 14-Jan-1998
 *  Added TTY_DO_WRITE_WAKEUP to enable n_tty to send POLL_OUT to
 *      waiting writers -- Sapan Bhatia <sapan@corewars.org>
 *
 *
 */

#include <linux/config.h>
#include <linux/module.h>	/* For EXPORT_SYMBOL */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/fcntl.h>
#include <linux/string.h>
#include <linux/major.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/devfs_fs_kernel.h>
#include <linux/sysctl.h>

#include <asm/uaccess.h>
#include <asm/system.h>
#include <linux/bitops.h>
#include <linux/devpts_fs.h>

/* These are global because they are accessed in tty_io.c */
#ifdef CONFIG_UNIX98_PTYS
struct tty_driver *ptm_driver;
static struct tty_driver *pts_driver;
#endif

static void pty_close(struct tty_struct * tty, struct file * filp)
{
	if (!tty)
		return;
	if (tty->driver->subtype == PTY_TYPE_MASTER) {
		if (tty->count > 1)
			printk("master pty_close: count = %d!!\n", tty->count);
	} else {
		if (tty->count > 2)
			return;
	}
	wake_up_interruptible(&tty->read_wait);
	wake_up_interruptible(&tty->write_wait);
	tty->packet = 0;
	if (!tty->link)
		return;
	tty->link->packet = 0;
	set_bit(TTY_OTHER_CLOSED, &tty->link->flags);
	wake_up_interruptible(&tty->link->read_wait);
	wake_up_interruptible(&tty->link->write_wait);
	if (tty->driver->subtype == PTY_TYPE_MASTER) {
		set_bit(TTY_OTHER_CLOSED, &tty->flags);
#ifdef CONFIG_UNIX98_PTYS
		if (tty->driver == ptm_driver)
			devpts_pty_kill(tty->index);
#endif
		tty_vhangup(tty->link);
	}
}

/*
 * The unthrottle routine is called by the line discipline to signal
 * that it can receive more characters.  For PTY's, the TTY_THROTTLED
 * flag is always set, to force the line discipline to always call the
 * unthrottle routine when there are fewer than TTY_THRESHOLD_UNTHROTTLE 
 * characters in the queue.  This is necessary since each time this
 * happens, we need to wake up any sleeping processes that could be
 * (1) trying to send data to the pty, or (2) waiting in wait_until_sent()
 * for the pty buffer to be drained.
 */
static void pty_unthrottle(struct tty_struct * tty)
{
	struct tty_struct *o_tty = tty->link;

	if (!o_tty)
		return;

	tty_wakeup(o_tty);
	set_bit(TTY_THROTTLED, &tty->flags);
}

/*
 * WSH 05/24/97: modified to 
 *   (1) use space in tty->flip instead of a shared temp buffer
 *	 The flip buffers aren't being used for a pty, so there's lots
 *	 of space available.  The buffer is protected by a per-pty
 *	 semaphore that should almost never come under contention.
 *   (2) avoid redundant copying for cases where count >> receive_room
 * N.B. Calls from user space may now return an error code instead of
 * a count.
 *
 * FIXME: Our pty_write method is called with our ldisc lock held but
 * not our partners. We can't just take the other one blindly without
 * risking deadlocks.  There is also the small matter of TTY_DONT_FLIP
 */
static int pty_write(struct tty_struct * tty, const unsigned char *buf, int count)
{
	struct tty_struct *to = tty->link;
	int	c;

	if (!to || tty->stopped)
		return 0;

	c = to->ldisc.receive_room(to);
	if (c > count)
		c = count;
	to->ldisc.receive_buf(to, buf, NULL, c);
	
	return c;
}

static int pty_write_room(struct tty_struct *tty)
{
	struct tty_struct *to = tty->link;

	if (!to || tty->stopped)
		return 0;

	return to->ldisc.receive_room(to);
}

/*
 *	WSH 05/24/97:  Modified for asymmetric MASTER/SLAVE behavior
 *	The chars_in_buffer() value is used by the ldisc select() function 
 *	to hold off writing when chars_in_buffer > WAKEUP_CHARS (== 256).
 *	The pty driver chars_in_buffer() Master/Slave must behave differently:
 *
 *      The Master side needs to allow typed-ahead commands to accumulate
 *      while being canonicalized, so we report "our buffer" as empty until
 *	some threshold is reached, and then report the count. (Any count >
 *	WAKEUP_CHARS is regarded by select() as "full".)  To avoid deadlock 
 *	the count returned must be 0 if no canonical data is available to be 
 *	read. (The N_TTY ldisc.chars_in_buffer now knows this.)
 *  
 *	The Slave side passes all characters in raw mode to the Master side's
 *	buffer where they can be read immediately, so in this case we can
 *	return the true count in the buffer.
 */
static int pty_chars_in_buffer(struct tty_struct *tty)
{
	struct tty_struct *to = tty->link;
	ssize_t (*chars_in_buffer)(struct tty_struct *);
	int count;

	/* We should get the line discipline lock for "tty->link" */
	if (!to || !(chars_in_buffer = to->ldisc.chars_in_buffer))
		return 0;

	/* The ldisc must report 0 if no characters available to be read */
	count = chars_in_buffer(to);

	if (tty->driver->subtype == PTY_TYPE_SLAVE) return count;

	/* Master side driver ... if the other side's read buffer is less than 
	 * half full, return 0 to allow writers to proceed; otherwise return
	 * the count.  This leaves a comfortable margin to avoid overflow, 
	 * and still allows half a buffer's worth of typed-ahead commands.
	 */
	return ((count < N_TTY_BUF_SIZE/2) ? 0 : count);
}

/* Set the lock flag on a pty */
static int pty_set_lock(struct tty_struct *tty, int __user * arg)
{
	int val;
	if (get_user(val,arg))
		return -EFAULT;
	if (val)
		set_bit(TTY_PTY_LOCK, &tty->flags);
	else
		clear_bit(TTY_PTY_LOCK, &tty->flags);
	return 0;
}

static void pty_flush_buffer(struct tty_struct *tty)
{
	struct tty_struct *to = tty->link;
	
	if (!to)
		return;
	
	if (to->ldisc.flush_buffer)
		to->ldisc.flush_buffer(to);
	
	if (to->packet) {
		tty->ctrl_status |= TIOCPKT_FLUSHWRITE;
		wake_up_interruptible(&to->read_wait);
	}
}

static int pty_open(struct tty_struct *tty, struct file * filp)
{
	int	retval = -ENODEV;

	if (!tty || !tty->link)
		goto out;

	retval = -EIO;
	if (test_bit(TTY_OTHER_CLOSED, &tty->flags))
		goto out;
	if (test_bit(TTY_PTY_LOCK, &tty->link->flags))
		goto out;
	if (tty->link->count != 1)
		goto out;

	clear_bit(TTY_OTHER_CLOSED, &tty->link->flags);
	set_bit(TTY_THROTTLED, &tty->flags);
	set_bit(TTY_DO_WRITE_WAKEUP, &tty->flags);
	retval = 0;
out:
	return retval;
}

static void pty_set_termios(struct tty_struct *tty, struct termios *old_termios)
{
        tty->termios->c_cflag &= ~(CSIZE | PARENB);
        tty->termios->c_cflag |= (CS8 | CREAD);
}

static struct tty_operations pty_ops = {
	.open = pty_open,
	.close = pty_close,
	.write = pty_write,
	.write_room = pty_write_room,
	.flush_buffer = pty_flush_buffer,
	.chars_in_buffer = pty_chars_in_buffer,
	.unthrottle = pty_unthrottle,
	.set_termios = pty_set_termios,
};

/* Traditional BSD devices */
#ifdef CONFIG_LEGACY_PTYS
static struct tty_driver *pty_driver, *pty_slave_driver;

static int pty_bsd_ioctl(struct tty_struct *tty, struct file *file,
			 unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
	case TIOCSPTLCK: /* Set PT Lock (disallow slave open) */
		return pty_set_lock(tty, (int __user *) arg);
	}
	return -ENOIOCTLCMD;
}

static void __init legacy_pty_init(void)
{

	pty_driver = alloc_tty_driver(NR_PTYS);
	if (!pty_driver)
		panic("Couldn't allocate pty driver");

	pty_slave_driver = alloc_tty_driver(NR_PTYS);
	if (!pty_slave_driver)
		panic("Couldn't allocate pty slave driver");

	pty_driver->owner = THIS_MODULE;
	pty_driver->driver_name = "pty_master";
	pty_driver->name = "pty";
	pty_driver->devfs_name = "pty/m";
	pty_driver->major = PTY_MASTER_MAJOR;
	pty_driver->minor_start = 0;
	pty_driver->type = TTY_DRIVER_TYPE_PTY;
	pty_driver->subtype = PTY_TYPE_MASTER;
	pty_driver->init_termios = tty_std_termios;
	pty_driver->init_termios.c_iflag = 0;
	pty_driver->init_termios.c_oflag = 0;
	pty_driver->init_termios.c_cflag = B38400 | CS8 | CREAD;
	pty_driver->init_termios.c_lflag = 0;
	pty_driver->flags = TTY_DRIVER_RESET_TERMIOS | TTY_DRIVER_REAL_RAW;
	pty_driver->other = pty_slave_driver;
	tty_set_operations(pty_driver, &pty_ops);
	pty_driver->ioctl = pty_bsd_ioctl;

	pty_slave_driver->owner = THIS_MODULE;
	pty_slave_driver->driver_name = "pty_slave";
	pty_slave_driver->name = "ttyp";
	pty_slave_driver->devfs_name = "pty/s";
	pty_slave_driver->major = PTY_SLAVE_MAJOR;
	pty_slave_driver->minor_start = 0;
	pty_slave_driver->type = TTY_DRIVER_TYPE_PTY;
	pty_slave_driver->subtype = PTY_TYPE_SLAVE;
	pty_slave_driver->init_termios = tty_std_termios;
	pty_slave_driver->init_termios.c_cflag = B38400 | CS8 | CREAD;
	pty_slave_driver->flags = TTY_DRIVER_RESET_TERMIOS |
					TTY_DRIVER_REAL_RAW;
	pty_slave_driver->other = pty_driver;
	tty_set_operations(pty_slave_driver, &pty_ops);

	if (tty_register_driver(pty_driver))
		panic("Couldn't register pty driver");
	if (tty_register_driver(pty_slave_driver))
		panic("Couldn't register pty slave driver");
}
#else
static inline void legacy_pty_init(void) { }
#endif

/* Unix98 devices */
#ifdef CONFIG_UNIX98_PTYS
/*
 * sysctl support for setting limits on the number of Unix98 ptys allocated.
 * Otherwise one can eat up all kernel memory by opening /dev/ptmx repeatedly.
 */
int pty_limit = NR_UNIX98_PTY_DEFAULT;
static int pty_limit_min = 0;
static int pty_limit_max = NR_UNIX98_PTY_MAX;

ctl_table pty_table[] = {
	{
		.ctl_name	= PTY_MAX,
		.procname	= "max",
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.data		= &pty_limit,
		.proc_handler	= &proc_dointvec_minmax,
		.strategy	= &sysctl_intvec,
		.extra1		= &pty_limit_min,
		.extra2		= &pty_limit_max,
	}, {
		.ctl_name	= PTY_NR,
		.procname	= "nr",
		.maxlen		= sizeof(int),
		.mode		= 0444,
		.proc_handler	= &proc_dointvec,
	}, {
		.ctl_name	= 0
	}
};

static int pty_unix98_ioctl(struct tty_struct *tty, struct file *file,
			    unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
	case TIOCSPTLCK: /* Set PT Lock (disallow slave open) */
		return pty_set_lock(tty, (int __user *)arg);
	case TIOCGPTN: /* Get PT Number */
		return put_user(tty->index, (unsigned int __user *)arg);
	}

	return -ENOIOCTLCMD;
}

static void __init unix98_pty_init(void)
{
	devfs_mk_dir("pts");
	ptm_driver = alloc_tty_driver(NR_UNIX98_PTY_MAX);
	if (!ptm_driver)
		panic("Couldn't allocate Unix98 ptm driver");
	pts_driver = alloc_tty_driver(NR_UNIX98_PTY_MAX);
	if (!pts_driver)
		panic("Couldn't allocate Unix98 pts driver");

	ptm_driver->owner = THIS_MODULE;
	ptm_driver->driver_name = "pty_master";
	ptm_driver->name = "ptm";
	ptm_driver->major = UNIX98_PTY_MASTER_MAJOR;
	ptm_driver->minor_start = 0;
	ptm_driver->type = TTY_DRIVER_TYPE_PTY;
	ptm_driver->subtype = PTY_TYPE_MASTER;
	ptm_driver->init_termios = tty_std_termios;
	ptm_driver->init_termios.c_iflag = 0;
	ptm_driver->init_termios.c_oflag = 0;
	ptm_driver->init_termios.c_cflag = B38400 | CS8 | CREAD;
	ptm_driver->init_termios.c_lflag = 0;
	ptm_driver->flags = TTY_DRIVER_RESET_TERMIOS | TTY_DRIVER_REAL_RAW |
		TTY_DRIVER_NO_DEVFS | TTY_DRIVER_DEVPTS_MEM;
	ptm_driver->other = pts_driver;
	tty_set_operations(ptm_driver, &pty_ops);
	ptm_driver->ioctl = pty_unix98_ioctl;

	pts_driver->owner = THIS_MODULE;
	pts_driver->driver_name = "pty_slave";
	pts_driver->name = "pts";
	pts_driver->major = UNIX98_PTY_SLAVE_MAJOR;
	pts_driver->minor_start = 0;
	pts_driver->type = TTY_DRIVER_TYPE_PTY;
	pts_driver->subtype = PTY_TYPE_SLAVE;
	pts_driver->init_termios = tty_std_termios;
	pts_driver->init_termios.c_cflag = B38400 | CS8 | CREAD;
	pts_driver->flags = TTY_DRIVER_RESET_TERMIOS | TTY_DRIVER_REAL_RAW |
		TTY_DRIVER_NO_DEVFS | TTY_DRIVER_DEVPTS_MEM;
	pts_driver->other = ptm_driver;
	tty_set_operations(pts_driver, &pty_ops);
	
	if (tty_register_driver(ptm_driver))
		panic("Couldn't register Unix98 ptm driver");
	if (tty_register_driver(pts_driver))
		panic("Couldn't register Unix98 pts driver");

	pty_table[1].data = &ptm_driver->refcount;
}
#else
static inline void unix98_pty_init(void) { }
#endif

static int __init pty_init(void)
{
	legacy_pty_init();
	unix98_pty_init();
	return 0;
}
module_init(pty_init);
