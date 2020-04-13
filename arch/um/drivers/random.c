/* Copyright (C) 2005 - 2008 Jeff Dike <jdike@{linux.intel,addtoit}.com> */

/* Much of this ripped from drivers/char/hw_random.c, see there for other
 * copyright.
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 */
#include <linux/sched/signal.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/miscdevice.h>
#include <linux/delay.h>
#include <linux/uaccess.h>
#include <init.h>
#include <irq_kern.h>
#include <os.h>

/*
 * core module and version information
 */
#define RNG_VERSION "1.0.0"
#define RNG_MODULE_NAME "hw_random"

/* Changed at init time, in the non-modular case, and at module load
 * time, in the module case.  Presumably, the module subsystem
 * protects against a module being loaded twice at the same time.
 */
static int random_fd = -1;
static DECLARE_WAIT_QUEUE_HEAD(host_read_wait);

static int rng_dev_open (struct inode *inode, struct file *filp)
{
	/* enforce read-only access to this chrdev */
	if ((filp->f_mode & FMODE_READ) == 0)
		return -EINVAL;
	if ((filp->f_mode & FMODE_WRITE) != 0)
		return -EINVAL;

	return 0;
}

static atomic_t host_sleep_count = ATOMIC_INIT(0);

static ssize_t rng_dev_read (struct file *filp, char __user *buf, size_t size,
			     loff_t *offp)
{
	u32 data;
	int n, ret = 0, have_data;

	while (size) {
		n = os_read_file(random_fd, &data, sizeof(data));
		if (n > 0) {
			have_data = n;
			while (have_data && size) {
				if (put_user((u8) data, buf++)) {
					ret = ret ? : -EFAULT;
					break;
				}
				size--;
				ret++;
				have_data--;
				data >>= 8;
			}
		}
		else if (n == -EAGAIN) {
			DECLARE_WAITQUEUE(wait, current);

			if (filp->f_flags & O_NONBLOCK)
				return ret ? : -EAGAIN;

			atomic_inc(&host_sleep_count);
			add_sigio_fd(random_fd);

			add_wait_queue(&host_read_wait, &wait);
			set_current_state(TASK_INTERRUPTIBLE);

			schedule();
			remove_wait_queue(&host_read_wait, &wait);

			if (atomic_dec_and_test(&host_sleep_count)) {
				ignore_sigio_fd(random_fd);
				deactivate_fd(random_fd, RANDOM_IRQ);
			}
		}
		else
			return n;

		if (signal_pending (current))
			return ret ? : -ERESTARTSYS;
	}
	return ret;
}

static const struct file_operations rng_chrdev_ops = {
	.owner		= THIS_MODULE,
	.open		= rng_dev_open,
	.read		= rng_dev_read,
	.llseek		= noop_llseek,
};

/* rng_init shouldn't be called more than once at boot time */
static struct miscdevice rng_miscdev = {
	HWRNG_MINOR,
	RNG_MODULE_NAME,
	&rng_chrdev_ops,
};

static irqreturn_t random_interrupt(int irq, void *data)
{
	wake_up(&host_read_wait);

	return IRQ_HANDLED;
}

/*
 * rng_init - initialize RNG module
 */
static int __init rng_init (void)
{
	int err;

	err = os_open_file("/dev/random", of_read(OPENFLAGS()), 0);
	if (err < 0)
		goto out;

	random_fd = err;

	err = um_request_irq(RANDOM_IRQ, random_fd, IRQ_READ, random_interrupt,
			     0, "random", NULL);
	if (err)
		goto err_out_cleanup_hw;

	sigio_broken(random_fd, 1);

	err = misc_register (&rng_miscdev);
	if (err) {
		printk (KERN_ERR RNG_MODULE_NAME ": misc device register "
			"failed\n");
		goto err_out_cleanup_hw;
	}
out:
	return err;

err_out_cleanup_hw:
	os_close_file(random_fd);
	random_fd = -1;
	goto out;
}

/*
 * rng_cleanup - shutdown RNG module
 */

static void cleanup(void)
{
	free_irq_by_fd(random_fd);
	os_close_file(random_fd);
}

static void __exit rng_cleanup(void)
{
	os_close_file(random_fd);
	misc_deregister (&rng_miscdev);
}

module_init (rng_init);
module_exit (rng_cleanup);
__uml_exitcall(cleanup);

MODULE_DESCRIPTION("UML Host Random Number Generator (RNG) driver");
MODULE_LICENSE("GPL");
