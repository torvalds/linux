/* $Id: divamnt.c,v 1.32.6.10 2005/02/11 19:40:25 armin Exp $
 *
 * Driver for Eicon DIVA Server ISDN cards.
 * Maint module
 *
 * Copyright 2000-2003 by Armin Schindler (mac@melware.de)
 * Copyright 2000-2003 Cytronics & Melware (info@melware.de)
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/poll.h>
#include <linux/smp_lock.h>
#include <asm/uaccess.h>

#include "platform.h"
#include "di_defs.h"
#include "divasync.h"
#include "debug_if.h"

static char *main_revision = "$Revision: 1.32.6.10 $";

static int major;

MODULE_DESCRIPTION("Maint driver for Eicon DIVA Server cards");
MODULE_AUTHOR("Cytronics & Melware, Eicon Networks");
MODULE_SUPPORTED_DEVICE("DIVA card driver");
MODULE_LICENSE("GPL");

static int buffer_length = 128;
module_param(buffer_length, int, 0);
static unsigned long diva_dbg_mem = 0;
module_param(diva_dbg_mem, ulong, 0);

static char *DRIVERNAME =
    "Eicon DIVA - MAINT module (http://www.melware.net)";
static char *DRIVERLNAME = "diva_mnt";
static char *DEVNAME = "DivasMAINT";
char *DRIVERRELEASE_MNT = "2.0";

static wait_queue_head_t msgwaitq;
static unsigned long opened;
static struct timeval start_time;

extern int mntfunc_init(int *, void **, unsigned long);
extern void mntfunc_finit(void);
extern int maint_read_write(void __user *buf, int count);

/*
 *  helper functions
 */
static char *getrev(const char *revision)
{
	char *rev;
	char *p;

	if ((p = strchr(revision, ':'))) {
		rev = p + 2;
		p = strchr(rev, '$');
		*--p = 0;
	} else
		rev = "1.0";

	return rev;
}

/*
 * kernel/user space copy functions
 */
int diva_os_copy_to_user(void *os_handle, void __user *dst, const void *src,
			 int length)
{
	return (copy_to_user(dst, src, length));
}
int diva_os_copy_from_user(void *os_handle, void *dst, const void __user *src,
			   int length)
{
	return (copy_from_user(dst, src, length));
}

/*
 * get time
 */
void diva_os_get_time(dword * sec, dword * usec)
{
	struct timeval tv;

	do_gettimeofday(&tv);

	if (tv.tv_sec > start_time.tv_sec) {
		if (start_time.tv_usec > tv.tv_usec) {
			tv.tv_sec--;
			tv.tv_usec += 1000000;
		}
		*sec = (dword) (tv.tv_sec - start_time.tv_sec);
		*usec = (dword) (tv.tv_usec - start_time.tv_usec);
	} else if (tv.tv_sec == start_time.tv_sec) {
		*sec = 0;
		if (start_time.tv_usec < tv.tv_usec) {
			*usec = (dword) (tv.tv_usec - start_time.tv_usec);
		} else {
			*usec = 0;
		}
	} else {
		*sec = (dword) tv.tv_sec;
		*usec = (dword) tv.tv_usec;
	}
}

/*
 * device node operations
 */
static unsigned int maint_poll(struct file *file, poll_table * wait)
{
	unsigned int mask = 0;

	poll_wait(file, &msgwaitq, wait);
	mask = POLLOUT | POLLWRNORM;
	if (file->private_data || diva_dbg_q_length()) {
		mask |= POLLIN | POLLRDNORM;
	}
	return (mask);
}

static int maint_open(struct inode *ino, struct file *filep)
{
	int ret;

	lock_kernel();
	/* only one open is allowed, so we test
	   it atomically */
	if (test_and_set_bit(0, &opened))
		ret = -EBUSY;
	else {
		filep->private_data = NULL;
		ret = nonseekable_open(ino, filep);
	}
	unlock_kernel();
	return ret;
}

static int maint_close(struct inode *ino, struct file *filep)
{
	if (filep->private_data) {
		diva_os_free(0, filep->private_data);
		filep->private_data = NULL;
	}

	/* clear 'used' flag */
	clear_bit(0, &opened);
	
	return (0);
}

static ssize_t divas_maint_write(struct file *file, const char __user *buf,
				 size_t count, loff_t * ppos)
{
	return (maint_read_write((char __user *) buf, (int) count));
}

static ssize_t divas_maint_read(struct file *file, char __user *buf,
				size_t count, loff_t * ppos)
{
	return (maint_read_write(buf, (int) count));
}

static const struct file_operations divas_maint_fops = {
	.owner   = THIS_MODULE,
	.llseek  = no_llseek,
	.read    = divas_maint_read,
	.write   = divas_maint_write,
	.poll    = maint_poll,
	.open    = maint_open,
	.release = maint_close
};

static void divas_maint_unregister_chrdev(void)
{
	unregister_chrdev(major, DEVNAME);
}

static int DIVA_INIT_FUNCTION divas_maint_register_chrdev(void)
{
	if ((major = register_chrdev(0, DEVNAME, &divas_maint_fops)) < 0)
	{
		printk(KERN_ERR "%s: failed to create /dev entry.\n",
		       DRIVERLNAME);
		return (0);
	}

	return (1);
}

/*
 * wake up reader
 */
void diva_maint_wakeup_read(void)
{
	wake_up_interruptible(&msgwaitq);
}

/*
 *  Driver Load
 */
static int DIVA_INIT_FUNCTION maint_init(void)
{
	char tmprev[50];
	int ret = 0;
	void *buffer = NULL;

	do_gettimeofday(&start_time);
	init_waitqueue_head(&msgwaitq);

	printk(KERN_INFO "%s\n", DRIVERNAME);
	printk(KERN_INFO "%s: Rel:%s  Rev:", DRIVERLNAME, DRIVERRELEASE_MNT);
	strcpy(tmprev, main_revision);
	printk("%s  Build: %s \n", getrev(tmprev), DIVA_BUILD);

	if (!divas_maint_register_chrdev()) {
		ret = -EIO;
		goto out;
	}

	if (!(mntfunc_init(&buffer_length, &buffer, diva_dbg_mem))) {
		printk(KERN_ERR "%s: failed to connect to DIDD.\n",
		       DRIVERLNAME);
		divas_maint_unregister_chrdev();
		ret = -EIO;
		goto out;
	}

	printk(KERN_INFO "%s: trace buffer = %p - %d kBytes, %s (Major: %d)\n",
	       DRIVERLNAME, buffer, (buffer_length / 1024),
	       (diva_dbg_mem == 0) ? "internal" : "external", major);

      out:
	return (ret);
}

/*
**  Driver Unload
*/
static void DIVA_EXIT_FUNCTION maint_exit(void)
{
	divas_maint_unregister_chrdev();
	mntfunc_finit();

	printk(KERN_INFO "%s: module unloaded.\n", DRIVERLNAME);
}

module_init(maint_init);
module_exit(maint_exit);

