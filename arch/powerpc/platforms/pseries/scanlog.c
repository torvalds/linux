/*
 *  c 2001 PPC 64 Team, IBM Corp
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 *
 * scan-log-data driver for PPC64  Todd Inglett <tinglett@vnet.ibm.com>
 *
 * When ppc64 hardware fails the service processor dumps internal state
 * of the system.  After a reboot the operating system can access a dump
 * of this data using this driver.  A dump exists if the device-tree
 * /chosen/ibm,scan-log-data property exists.
 *
 * This driver exports /proc/ppc64/scan-log-dump which can be read.
 * The driver supports only sequential reads.
 *
 * The driver looks at a write to the driver for the single word "reset".
 * If given, the driver will reset the scanlog so the platform can free it.
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/proc_fs.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <asm/uaccess.h>
#include <asm/rtas.h>
#include <asm/prom.h>

#define MODULE_VERS "1.0"
#define MODULE_NAME "scanlog"

/* Status returns from ibm,scan-log-dump */
#define SCANLOG_COMPLETE 0
#define SCANLOG_HWERROR -1
#define SCANLOG_CONTINUE 1

#define DEBUG(A...) do { if (scanlog_debug) printk(KERN_ERR "scanlog: " A); } while (0)

static int scanlog_debug;
static unsigned int ibm_scan_log_dump;			/* RTAS token */
static struct proc_dir_entry *proc_ppc64_scan_log_dump;	/* The proc file */

static ssize_t scanlog_read(struct file *file, char __user *buf,
			    size_t count, loff_t *ppos)
{
        struct inode * inode = file->f_dentry->d_inode;
	struct proc_dir_entry *dp;
	unsigned int *data;
	int status;
	unsigned long len, off;
	unsigned int wait_time;

        dp = PDE(inode);
 	data = (unsigned int *)dp->data;

	if (!data) {
		printk(KERN_ERR "scanlog: read failed no data\n");
		return -EIO;
	}

	if (count > RTAS_DATA_BUF_SIZE)
		count = RTAS_DATA_BUF_SIZE;

	if (count < 1024) {
		/* This is the min supported by this RTAS call.  Rather
		 * than do all the buffering we insist the user code handle
		 * larger reads.  As long as cp works... :)
		 */
		printk(KERN_ERR "scanlog: cannot perform a small read (%ld)\n", count);
		return -EINVAL;
	}

	if (!access_ok(VERIFY_WRITE, buf, count))
		return -EFAULT;

	for (;;) {
		wait_time = 500;	/* default wait if no data */
		spin_lock(&rtas_data_buf_lock);
		memcpy(rtas_data_buf, data, RTAS_DATA_BUF_SIZE);
		status = rtas_call(ibm_scan_log_dump, 2, 1, NULL,
				   (u32) __pa(rtas_data_buf), (u32) count);
		memcpy(data, rtas_data_buf, RTAS_DATA_BUF_SIZE);
		spin_unlock(&rtas_data_buf_lock);

		DEBUG("status=%d, data[0]=%x, data[1]=%x, data[2]=%x\n",
		      status, data[0], data[1], data[2]);
		switch (status) {
		    case SCANLOG_COMPLETE:
			DEBUG("hit eof\n");
			return 0;
		    case SCANLOG_HWERROR:
			DEBUG("hardware error reading scan log data\n");
			return -EIO;
		    case SCANLOG_CONTINUE:
			/* We may or may not have data yet */
			len = data[1];
			off = data[2];
			if (len > 0) {
				if (copy_to_user(buf, ((char *)data)+off, len))
					return -EFAULT;
				return len;
			}
			/* Break to sleep default time */
			break;
		    default:
			if (status > 9900 && status <= 9905) {
				wait_time = rtas_extended_busy_delay_time(status);
			} else {
				printk(KERN_ERR "scanlog: unknown error from rtas: %d\n", status);
				return -EIO;
			}
		}
		/* Apparently no data yet.  Wait and try again. */
		msleep_interruptible(wait_time);
	}
	/*NOTREACHED*/
}

static ssize_t scanlog_write(struct file * file, const char __user * buf,
			     size_t count, loff_t *ppos)
{
	char stkbuf[20];
	int status;

	if (count > 19) count = 19;
	if (copy_from_user (stkbuf, buf, count)) {
		return -EFAULT;
	}
	stkbuf[count] = 0;

	if (buf) {
		if (strncmp(stkbuf, "reset", 5) == 0) {
			DEBUG("reset scanlog\n");
			status = rtas_call(ibm_scan_log_dump, 2, 1, NULL, 0, 0);
			DEBUG("rtas returns %d\n", status);
		} else if (strncmp(stkbuf, "debugon", 7) == 0) {
			printk(KERN_ERR "scanlog: debug on\n");
			scanlog_debug = 1;
		} else if (strncmp(stkbuf, "debugoff", 8) == 0) {
			printk(KERN_ERR "scanlog: debug off\n");
			scanlog_debug = 0;
		}
	}
	return count;
}

static int scanlog_open(struct inode * inode, struct file * file)
{
	struct proc_dir_entry *dp = PDE(inode);
	unsigned int *data = (unsigned int *)dp->data;

	if (!data) {
		printk(KERN_ERR "scanlog: open failed no data\n");
		return -EIO;
	}

	if (data[0] != 0) {
		/* This imperfect test stops a second copy of the
		 * data (or a reset while data is being copied)
		 */
		return -EBUSY;
	}

	data[0] = 0;	/* re-init so we restart the scan */

	return 0;
}

static int scanlog_release(struct inode * inode, struct file * file)
{
	struct proc_dir_entry *dp = PDE(inode);
	unsigned int *data = (unsigned int *)dp->data;

	if (!data) {
		printk(KERN_ERR "scanlog: release failed no data\n");
		return -EIO;
	}
	data[0] = 0;

	return 0;
}

struct file_operations scanlog_fops = {
	.owner		= THIS_MODULE,
	.read		= scanlog_read,
	.write		= scanlog_write,
	.open		= scanlog_open,
	.release	= scanlog_release,
};

int __init scanlog_init(void)
{
	struct proc_dir_entry *ent;

	ibm_scan_log_dump = rtas_token("ibm,scan-log-dump");
	if (ibm_scan_log_dump == RTAS_UNKNOWN_SERVICE) {
		printk(KERN_ERR "scan-log-dump not implemented on this system\n");
		return -EIO;
	}

        ent = create_proc_entry("ppc64/rtas/scan-log-dump",  S_IRUSR, NULL);
	if (ent) {
		ent->proc_fops = &scanlog_fops;
		/* Ideally we could allocate a buffer < 4G */
		ent->data = kmalloc(RTAS_DATA_BUF_SIZE, GFP_KERNEL);
		if (!ent->data) {
			printk(KERN_ERR "Failed to allocate a buffer\n");
			remove_proc_entry("scan-log-dump", ent->parent);
			return -ENOMEM;
		}
		((unsigned int *)ent->data)[0] = 0;
	} else {
		printk(KERN_ERR "Failed to create ppc64/scan-log-dump proc entry\n");
		return -EIO;
	}
	proc_ppc64_scan_log_dump = ent;

	return 0;
}

void __exit scanlog_cleanup(void)
{
	if (proc_ppc64_scan_log_dump) {
		kfree(proc_ppc64_scan_log_dump->data);
		remove_proc_entry("scan-log-dump", proc_ppc64_scan_log_dump->parent);
	}
}

module_init(scanlog_init);
module_exit(scanlog_cleanup);
MODULE_LICENSE("GPL");
