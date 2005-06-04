/* Copyright (C) 2005 Jeff Dike <jdike@addtoit.com> */
/* Much of this ripped from drivers/char/hw_random.c, see there for other
 * copyright.
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 */
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/delay.h>
#include <asm/uaccess.h>
#include "os.h"

/*
 * core module and version information
 */
#define RNG_VERSION "1.0.0"
#define RNG_MODULE_NAME "random"

#define RNG_MISCDEV_MINOR		183 /* official */

static int random_fd = -1;

static int rng_dev_open (struct inode *inode, struct file *filp)
{
	/* enforce read-only access to this chrdev */
	if ((filp->f_mode & FMODE_READ) == 0)
		return -EINVAL;
	if (filp->f_mode & FMODE_WRITE)
		return -EINVAL;

	return 0;
}

static ssize_t rng_dev_read (struct file *filp, char __user *buf, size_t size,
                             loff_t * offp)
{
        u32 data;
        int n, ret = 0, have_data;

        while(size){
                n = os_read_file(random_fd, &data, sizeof(data));
                if(n > 0){
                        have_data = n;
                        while (have_data && size) {
                                if (put_user((u8)data, buf++)) {
                                        ret = ret ? : -EFAULT;
                                        break;
                                }
                                size--;
                                ret++;
                                have_data--;
                                data>>=8;
                        }
                }
                else if(n == -EAGAIN){
                        if (filp->f_flags & O_NONBLOCK)
                                return ret ? : -EAGAIN;

                        if(need_resched()){
                                current->state = TASK_INTERRUPTIBLE;
                                schedule_timeout(1);
                        }
                }
                else return n;
		if (signal_pending (current))
			return ret ? : -ERESTARTSYS;
	}
	return ret;
}

static struct file_operations rng_chrdev_ops = {
	.owner		= THIS_MODULE,
	.open		= rng_dev_open,
	.read		= rng_dev_read,
};

static struct miscdevice rng_miscdev = {
	RNG_MISCDEV_MINOR,
	RNG_MODULE_NAME,
	&rng_chrdev_ops,
};

/*
 * rng_init - initialize RNG module
 */
static int __init rng_init (void)
{
	int err;

        err = os_open_file("/dev/random", of_read(OPENFLAGS()), 0);
        if(err < 0)
                goto out;

        random_fd = err;

        err = os_set_fd_block(random_fd, 0);
        if(err)
		goto err_out_cleanup_hw;

	err = misc_register (&rng_miscdev);
	if (err) {
		printk (KERN_ERR RNG_MODULE_NAME ": misc device register failed\n");
		goto err_out_cleanup_hw;
	}

 out:
        return err;

 err_out_cleanup_hw:
        random_fd = -1;
        goto out;
}

/*
 * rng_cleanup - shutdown RNG module
 */
static void __exit rng_cleanup (void)
{
	misc_deregister (&rng_miscdev);
}

module_init (rng_init);
module_exit (rng_cleanup);

MODULE_DESCRIPTION("UML Host Random Number Generator (RNG) driver");
MODULE_LICENSE("GPL");
