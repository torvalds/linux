/*
 *  drivers/s390/char/fs3270.c
 *    IBM/3270 Driver - fullscreen driver.
 *
 *  Author(s):
 *    Original 3270 Code for 2.4 written by Richard Hitt (UTS Global)
 *    Rewritten for 2.5/2.6 by Martin Schwidefsky <schwidefsky@de.ibm.com>
 *	-- Copyright (C) 2003 IBM Deutschland Entwicklung GmbH, IBM Corporation
 */

#include <linux/config.h>
#include <linux/bootmem.h>
#include <linux/console.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/types.h>

#include <asm/ccwdev.h>
#include <asm/cio.h>
#include <asm/cpcmd.h>
#include <asm/ebcdic.h>
#include <asm/idals.h>

#include "raw3270.h"
#include "ctrlchar.h"

struct raw3270_fn fs3270_fn;

struct fs3270 {
	struct raw3270_view view;
	pid_t fs_pid;			/* Pid of controlling program. */
	int read_command;		/* ccw command to use for reads. */
	int write_command;		/* ccw command to use for writes. */
	int attention;			/* Got attention. */
	struct raw3270_request *clear;	/* single clear request. */
	wait_queue_head_t attn_wait;	/* Attention wait queue. */
};

static void
fs3270_wake_up(struct raw3270_request *rq, void *data)
{
	wake_up((wait_queue_head_t *) data);
}

static int
fs3270_do_io(struct raw3270_view *view, struct raw3270_request *rq)
{
	wait_queue_head_t wq;
	int rc;

	init_waitqueue_head(&wq);
	rq->callback = fs3270_wake_up;
	rq->callback_data = &wq;
	rc = raw3270_start(view, rq);
	if (rc)
		return rc;
	/* Started sucessfully. Now wait for completion. */
	wait_event(wq, raw3270_request_final(rq));
	return rq->rc;
}

static void
fs3270_reset_callback(struct raw3270_request *rq, void *data)
{
	raw3270_request_reset(rq);
}

/*
 * Switch to the fullscreen view.
 */
static int
fs3270_activate(struct raw3270_view *view)
{
	struct fs3270 *fp;

	fp = (struct fs3270 *) view;
	raw3270_request_set_cmd(fp->clear, TC_EWRITEA);
	fp->clear->callback = fs3270_reset_callback;
	return raw3270_start(view, fp->clear);
}

/*
 * Shutdown fullscreen view.
 */
static void
fs3270_deactivate(struct raw3270_view *view)
{
	// FIXME: is this a good idea? The user program using fullscreen 3270
	// will die just because a console message appeared. On the other
	// hand the fullscreen device is unoperational now.
	struct fs3270 *fp;

	fp = (struct fs3270 *) view;
	if (fp->fs_pid != 0)
		kill_proc(fp->fs_pid, SIGHUP, 1);
	fp->fs_pid = 0;
}

static int
fs3270_irq(struct fs3270 *fp, struct raw3270_request *rq, struct irb *irb)
{
	/* Handle ATTN. Set indication and wake waiters for attention. */
	if (irb->scsw.dstat & DEV_STAT_ATTENTION) {
		fp->attention = 1;
		wake_up(&fp->attn_wait);
	}

	if (rq) {
		if (irb->scsw.dstat & DEV_STAT_UNIT_CHECK)
			rq->rc = -EIO;
		else
			/* Normal end. Copy residual count. */
			rq->rescnt = irb->scsw.count;
	}
	return RAW3270_IO_DONE;
}

/*
 * Process reads from fullscreen 3270.
 */
static ssize_t
fs3270_read(struct file *filp, char *data, size_t count, loff_t *off)
{
	struct fs3270 *fp;
	struct raw3270_request *rq;
	struct idal_buffer *ib;
	int rc;
	
	if (count == 0 || count > 65535)
		return -EINVAL;
	fp = filp->private_data;
	if (!fp)
		return -ENODEV;
	ib = idal_buffer_alloc(count, 0);
	if (!ib)
		return -ENOMEM;
	rq = raw3270_request_alloc(0);
	if (!IS_ERR(rq)) {
		if (fp->read_command == 0 && fp->write_command != 0)
			fp->read_command = 6;
		raw3270_request_set_cmd(rq, fp->read_command ? : 2);
		raw3270_request_set_idal(rq, ib);
		wait_event(fp->attn_wait, fp->attention);
		rc = fs3270_do_io(&fp->view, rq);
		if (rc == 0 && idal_buffer_to_user(ib, data, count))
			rc = -EFAULT;
		raw3270_request_free(rq);
	} else
		rc = PTR_ERR(rq);
	idal_buffer_free(ib);
	return rc;
}

/*
 * Process writes to fullscreen 3270.
 */
static ssize_t
fs3270_write(struct file *filp, const char *data, size_t count, loff_t *off)
{
	struct fs3270 *fp;
	struct raw3270_request *rq;
	struct idal_buffer *ib;
	int write_command;
	int rc;

	fp = filp->private_data;
	if (!fp)
		return -ENODEV;
	ib = idal_buffer_alloc(count, 0);
	if (!ib)
		return -ENOMEM;
	rq = raw3270_request_alloc(0);
	if (!IS_ERR(rq)) {
		if (idal_buffer_from_user(ib, data, count) == 0) {
			write_command = fp->write_command ? : 1;
			if (write_command == 5)
				write_command = 13;
			raw3270_request_set_cmd(rq, write_command);
			raw3270_request_set_idal(rq, ib);
			rc = fs3270_do_io(&fp->view, rq);
		} else
			rc = -EFAULT;
		raw3270_request_free(rq);
	} else
		rc = PTR_ERR(rq);
	idal_buffer_free(ib);
	return rc;
}

/*
 * process ioctl commands for the tube driver
 */
static int
fs3270_ioctl(struct inode *inode, struct file *filp,
	     unsigned int cmd, unsigned long arg)
{
	struct fs3270 *fp;
	struct raw3270_iocb iocb;
	int rc;

	fp = filp->private_data;
	if (!fp)
		return -ENODEV;
	rc = 0;
	switch (cmd) {
	case TUBICMD:
		fp->read_command = arg;
		break;
	case TUBOCMD:
		fp->write_command = arg;
		break;
	case TUBGETI:
		rc = put_user(fp->read_command, (char *) arg);
		break;
	case TUBGETO:
		rc = put_user(fp->write_command,(char *) arg);
		break;
	case TUBGETMOD:
		iocb.model = fp->view.model;
		iocb.line_cnt = fp->view.rows;
		iocb.col_cnt = fp->view.cols;
		iocb.pf_cnt = 24;
		iocb.re_cnt = 20;
		iocb.map = 0;
		if (copy_to_user((char *) arg, &iocb,
				 sizeof(struct raw3270_iocb)))
			rc = -EFAULT;
		break;
	}
	return rc;
}

/*
 * Allocate tty3270 structure.
 */
static struct fs3270 *
fs3270_alloc_view(void)
{
	struct fs3270 *fp;

	fp = (struct fs3270 *) kmalloc(sizeof(struct fs3270),GFP_KERNEL);
	if (!fp)
		return ERR_PTR(-ENOMEM);
	memset(fp, 0, sizeof(struct fs3270));
	fp->clear = raw3270_request_alloc(0);
	if (!IS_ERR(fp->clear)) {
		kfree(fp);
		return ERR_PTR(-ENOMEM);
	}
	return fp;
}

/*
 * Free tty3270 structure.
 */
static void
fs3270_free_view(struct raw3270_view *view)
{
	raw3270_request_free(((struct fs3270 *) view)->clear);
	kfree(view);
}

/*
 * Unlink fs3270 data structure from filp.
 */
static void
fs3270_release(struct raw3270_view *view)
{
}

/* View to a 3270 device. Can be console, tty or fullscreen. */
struct raw3270_fn fs3270_fn = {
	.activate = fs3270_activate,
	.deactivate = fs3270_deactivate,
	.intv = (void *) fs3270_irq,
	.release = fs3270_release,
	.free = fs3270_free_view
};

/*
 * This routine is called whenever a 3270 fullscreen device is opened.
 */
static int
fs3270_open(struct inode *inode, struct file *filp)
{
	struct fs3270 *fp;
	int minor, rc;

	if (imajor(filp->f_dentry->d_inode) != IBM_FS3270_MAJOR)
		return -ENODEV;
	minor = iminor(filp->f_dentry->d_inode);
	/* Check if some other program is already using fullscreen mode. */
	fp = (struct fs3270 *) raw3270_find_view(&fs3270_fn, minor);
	if (!IS_ERR(fp)) {
		raw3270_put_view(&fp->view);
		return -EBUSY;
	}
	/* Allocate fullscreen view structure. */
	fp = fs3270_alloc_view();
	if (IS_ERR(fp))
		return PTR_ERR(fp);

	init_waitqueue_head(&fp->attn_wait);
	fp->fs_pid = current->pid;
	rc = raw3270_add_view(&fp->view, &fs3270_fn, minor);
	if (rc) {
		fs3270_free_view(&fp->view);
		return rc;
	}

	rc = raw3270_activate_view(&fp->view);
	if (rc) {
		raw3270_del_view(&fp->view);
		return rc;
	}
	filp->private_data = fp;
	return 0;
}

/*
 * This routine is called when the 3270 tty is closed. We wait
 * for the remaining request to be completed. Then we clean up.
 */
static int
fs3270_close(struct inode *inode, struct file *filp)
{
	struct fs3270 *fp;

	fp = filp->private_data;
	filp->private_data = 0;
	if (fp)
		raw3270_del_view(&fp->view);
	return 0;
}

static struct file_operations fs3270_fops = {
	.owner	 = THIS_MODULE,		/* owner */
	.read	 = fs3270_read,		/* read */
	.write	 = fs3270_write,	/* write */
	.ioctl	 = fs3270_ioctl,	/* ioctl */
	.open	 = fs3270_open,		/* open */
	.release = fs3270_close,	/* release */
};

/*
 * 3270 fullscreen driver initialization.
 */
static int __init
fs3270_init(void)
{
	int rc;

	rc = register_chrdev(IBM_FS3270_MAJOR, "fs3270", &fs3270_fops);
	if (rc) {
		printk(KERN_ERR "fs3270 can't get major number %d: errno %d\n",
		       IBM_FS3270_MAJOR, rc);
		return rc;
	}
	return 0;
}

static void __exit
fs3270_exit(void)
{
	unregister_chrdev(IBM_FS3270_MAJOR, "fs3270");
}

MODULE_LICENSE("GPL");
MODULE_ALIAS_CHARDEV_MAJOR(IBM_FS3270_MAJOR);

module_init(fs3270_init);
module_exit(fs3270_exit);
