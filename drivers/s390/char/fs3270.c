/*
 * IBM/3270 Driver - fullscreen driver.
 *
 * Author(s):
 *   Original 3270 Code for 2.4 written by Richard Hitt (UTS Global)
 *   Rewritten for 2.5/2.6 by Martin Schwidefsky <schwidefsky@de.ibm.com>
 *     Copyright IBM Corp. 2003, 2009
 */

#include <linux/bootmem.h>
#include <linux/console.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/smp_lock.h>

#include <asm/compat.h>
#include <asm/ccwdev.h>
#include <asm/cio.h>
#include <asm/ebcdic.h>
#include <asm/idals.h>

#include "raw3270.h"
#include "ctrlchar.h"

static struct raw3270_fn fs3270_fn;

struct fs3270 {
	struct raw3270_view view;
	struct pid *fs_pid;		/* Pid of controlling program. */
	int read_command;		/* ccw command to use for reads. */
	int write_command;		/* ccw command to use for writes. */
	int attention;			/* Got attention. */
	int active;			/* Fullscreen view is active. */
	struct raw3270_request *init;	/* single init request. */
	wait_queue_head_t wait;		/* Init & attention wait queue. */
	struct idal_buffer *rdbuf;	/* full-screen-deactivate buffer */
	size_t rdbuf_size;		/* size of data returned by RDBUF */
};

static DEFINE_MUTEX(fs3270_mutex);

static void
fs3270_wake_up(struct raw3270_request *rq, void *data)
{
	wake_up((wait_queue_head_t *) data);
}

static inline int
fs3270_working(struct fs3270 *fp)
{
	/*
	 * The fullscreen view is in working order if the view
	 * has been activated AND the initial request is finished.
	 */
	return fp->active && raw3270_request_final(fp->init);
}

static int
fs3270_do_io(struct raw3270_view *view, struct raw3270_request *rq)
{
	struct fs3270 *fp;
	int rc;

	fp = (struct fs3270 *) view;
	rq->callback = fs3270_wake_up;
	rq->callback_data = &fp->wait;

	do {
		if (!fs3270_working(fp)) {
			/* Fullscreen view isn't ready yet. */
			rc = wait_event_interruptible(fp->wait,
						      fs3270_working(fp));
			if (rc != 0)
				break;
		}
		rc = raw3270_start(view, rq);
		if (rc == 0) {
			/* Started successfully. Now wait for completion. */
			wait_event(fp->wait, raw3270_request_final(rq));
		}
	} while (rc == -EACCES);
	return rc;
}

/*
 * Switch to the fullscreen view.
 */
static void
fs3270_reset_callback(struct raw3270_request *rq, void *data)
{
	struct fs3270 *fp;

	fp = (struct fs3270 *) rq->view;
	raw3270_request_reset(rq);
	wake_up(&fp->wait);
}

static void
fs3270_restore_callback(struct raw3270_request *rq, void *data)
{
	struct fs3270 *fp;

	fp = (struct fs3270 *) rq->view;
	if (rq->rc != 0 || rq->rescnt != 0) {
		if (fp->fs_pid)
			kill_pid(fp->fs_pid, SIGHUP, 1);
	}
	fp->rdbuf_size = 0;
	raw3270_request_reset(rq);
	wake_up(&fp->wait);
}

static int
fs3270_activate(struct raw3270_view *view)
{
	struct fs3270 *fp;
	char *cp;
	int rc;

	fp = (struct fs3270 *) view;

	/* If an old init command is still running just return. */
	if (!raw3270_request_final(fp->init))
		return 0;

	if (fp->rdbuf_size == 0) {
		/* No saved buffer. Just clear the screen. */
		raw3270_request_set_cmd(fp->init, TC_EWRITEA);
		fp->init->callback = fs3270_reset_callback;
	} else {
		/* Restore fullscreen buffer saved by fs3270_deactivate. */
		raw3270_request_set_cmd(fp->init, TC_EWRITEA);
		raw3270_request_set_idal(fp->init, fp->rdbuf);
		fp->init->ccw.count = fp->rdbuf_size;
		cp = fp->rdbuf->data[0];
		cp[0] = TW_KR;
		cp[1] = TO_SBA;
		cp[2] = cp[6];
		cp[3] = cp[7];
		cp[4] = TO_IC;
		cp[5] = TO_SBA;
		cp[6] = 0x40;
		cp[7] = 0x40;
		fp->init->rescnt = 0;
		fp->init->callback = fs3270_restore_callback;
	}
	rc = fp->init->rc = raw3270_start_locked(view, fp->init);
	if (rc)
		fp->init->callback(fp->init, NULL);
	else
		fp->active = 1;
	return rc;
}

/*
 * Shutdown fullscreen view.
 */
static void
fs3270_save_callback(struct raw3270_request *rq, void *data)
{
	struct fs3270 *fp;

	fp = (struct fs3270 *) rq->view;

	/* Correct idal buffer element 0 address. */
	fp->rdbuf->data[0] -= 5;
	fp->rdbuf->size += 5;

	/*
	 * If the rdbuf command failed or the idal buffer is
	 * to small for the amount of data returned by the
	 * rdbuf command, then we have no choice but to send
	 * a SIGHUP to the application.
	 */
	if (rq->rc != 0 || rq->rescnt == 0) {
		if (fp->fs_pid)
			kill_pid(fp->fs_pid, SIGHUP, 1);
		fp->rdbuf_size = 0;
	} else
		fp->rdbuf_size = fp->rdbuf->size - rq->rescnt;
	raw3270_request_reset(rq);
	wake_up(&fp->wait);
}

static void
fs3270_deactivate(struct raw3270_view *view)
{
	struct fs3270 *fp;

	fp = (struct fs3270 *) view;
	fp->active = 0;

	/* If an old init command is still running just return. */
	if (!raw3270_request_final(fp->init))
		return;

	/* Prepare read-buffer request. */
	raw3270_request_set_cmd(fp->init, TC_RDBUF);
	/*
	 * Hackish: skip first 5 bytes of the idal buffer to make
	 * room for the TW_KR/TO_SBA/<address>/<address>/TO_IC sequence
	 * in the activation command.
	 */
	fp->rdbuf->data[0] += 5;
	fp->rdbuf->size -= 5;
	raw3270_request_set_idal(fp->init, fp->rdbuf);
	fp->init->rescnt = 0;
	fp->init->callback = fs3270_save_callback;

	/* Start I/O to read in the 3270 buffer. */
	fp->init->rc = raw3270_start_locked(view, fp->init);
	if (fp->init->rc)
		fp->init->callback(fp->init, NULL);
}

static int
fs3270_irq(struct fs3270 *fp, struct raw3270_request *rq, struct irb *irb)
{
	/* Handle ATTN. Set indication and wake waiters for attention. */
	if (irb->scsw.cmd.dstat & DEV_STAT_ATTENTION) {
		fp->attention = 1;
		wake_up(&fp->wait);
	}

	if (rq) {
		if (irb->scsw.cmd.dstat & DEV_STAT_UNIT_CHECK)
			rq->rc = -EIO;
		else
			/* Normal end. Copy residual count. */
			rq->rescnt = irb->scsw.cmd.count;
	}
	return RAW3270_IO_DONE;
}

/*
 * Process reads from fullscreen 3270.
 */
static ssize_t
fs3270_read(struct file *filp, char __user *data, size_t count, loff_t *off)
{
	struct fs3270 *fp;
	struct raw3270_request *rq;
	struct idal_buffer *ib;
	ssize_t rc;
	
	if (count == 0 || count > 65535)
		return -EINVAL;
	fp = filp->private_data;
	if (!fp)
		return -ENODEV;
	ib = idal_buffer_alloc(count, 0);
	if (IS_ERR(ib))
		return -ENOMEM;
	rq = raw3270_request_alloc(0);
	if (!IS_ERR(rq)) {
		if (fp->read_command == 0 && fp->write_command != 0)
			fp->read_command = 6;
		raw3270_request_set_cmd(rq, fp->read_command ? : 2);
		raw3270_request_set_idal(rq, ib);
		rc = wait_event_interruptible(fp->wait, fp->attention);
		fp->attention = 0;
		if (rc == 0) {
			rc = fs3270_do_io(&fp->view, rq);
			if (rc == 0) {
				count -= rq->rescnt;
				if (idal_buffer_to_user(ib, data, count) != 0)
					rc = -EFAULT;
				else
					rc = count;

			}
		}
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
fs3270_write(struct file *filp, const char __user *data, size_t count, loff_t *off)
{
	struct fs3270 *fp;
	struct raw3270_request *rq;
	struct idal_buffer *ib;
	int write_command;
	ssize_t rc;

	fp = filp->private_data;
	if (!fp)
		return -ENODEV;
	ib = idal_buffer_alloc(count, 0);
	if (IS_ERR(ib))
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
			if (rc == 0)
				rc = count - rq->rescnt;
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
static long
fs3270_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	char __user *argp;
	struct fs3270 *fp;
	struct raw3270_iocb iocb;
	int rc;

	fp = filp->private_data;
	if (!fp)
		return -ENODEV;
	if (is_compat_task())
		argp = compat_ptr(arg);
	else
		argp = (char __user *)arg;
	rc = 0;
	mutex_lock(&fs3270_mutex);
	switch (cmd) {
	case TUBICMD:
		fp->read_command = arg;
		break;
	case TUBOCMD:
		fp->write_command = arg;
		break;
	case TUBGETI:
		rc = put_user(fp->read_command, argp);
		break;
	case TUBGETO:
		rc = put_user(fp->write_command, argp);
		break;
	case TUBGETMOD:
		iocb.model = fp->view.model;
		iocb.line_cnt = fp->view.rows;
		iocb.col_cnt = fp->view.cols;
		iocb.pf_cnt = 24;
		iocb.re_cnt = 20;
		iocb.map = 0;
		if (copy_to_user(argp, &iocb, sizeof(struct raw3270_iocb)))
			rc = -EFAULT;
		break;
	}
	mutex_unlock(&fs3270_mutex);
	return rc;
}

/*
 * Allocate fs3270 structure.
 */
static struct fs3270 *
fs3270_alloc_view(void)
{
	struct fs3270 *fp;

	fp = kzalloc(sizeof(struct fs3270),GFP_KERNEL);
	if (!fp)
		return ERR_PTR(-ENOMEM);
	fp->init = raw3270_request_alloc(0);
	if (IS_ERR(fp->init)) {
		kfree(fp);
		return ERR_PTR(-ENOMEM);
	}
	return fp;
}

/*
 * Free fs3270 structure.
 */
static void
fs3270_free_view(struct raw3270_view *view)
{
	struct fs3270 *fp;

	fp = (struct fs3270 *) view;
	if (fp->rdbuf)
		idal_buffer_free(fp->rdbuf);
	raw3270_request_free(((struct fs3270 *) view)->init);
	kfree(view);
}

/*
 * Unlink fs3270 data structure from filp.
 */
static void
fs3270_release(struct raw3270_view *view)
{
	struct fs3270 *fp;

	fp = (struct fs3270 *) view;
	if (fp->fs_pid)
		kill_pid(fp->fs_pid, SIGHUP, 1);
}

/* View to a 3270 device. Can be console, tty or fullscreen. */
static struct raw3270_fn fs3270_fn = {
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
	struct idal_buffer *ib;
	int minor, rc = 0;

	if (imajor(filp->f_path.dentry->d_inode) != IBM_FS3270_MAJOR)
		return -ENODEV;
	minor = iminor(filp->f_path.dentry->d_inode);
	/* Check for minor 0 multiplexer. */
	if (minor == 0) {
		struct tty_struct *tty = get_current_tty();
		if (!tty || tty->driver->major != IBM_TTY3270_MAJOR) {
			tty_kref_put(tty);
			return -ENODEV;
		}
		minor = tty->index + RAW3270_FIRSTMINOR;
		tty_kref_put(tty);
	}
	mutex_lock(&fs3270_mutex);
	/* Check if some other program is already using fullscreen mode. */
	fp = (struct fs3270 *) raw3270_find_view(&fs3270_fn, minor);
	if (!IS_ERR(fp)) {
		raw3270_put_view(&fp->view);
		rc = -EBUSY;
		goto out;
	}
	/* Allocate fullscreen view structure. */
	fp = fs3270_alloc_view();
	if (IS_ERR(fp)) {
		rc = PTR_ERR(fp);
		goto out;
	}

	init_waitqueue_head(&fp->wait);
	fp->fs_pid = get_pid(task_pid(current));
	rc = raw3270_add_view(&fp->view, &fs3270_fn, minor);
	if (rc) {
		fs3270_free_view(&fp->view);
		goto out;
	}

	/* Allocate idal-buffer. */
	ib = idal_buffer_alloc(2*fp->view.rows*fp->view.cols + 5, 0);
	if (IS_ERR(ib)) {
		raw3270_put_view(&fp->view);
		raw3270_del_view(&fp->view);
		rc = PTR_ERR(ib);
		goto out;
	}
	fp->rdbuf = ib;

	rc = raw3270_activate_view(&fp->view);
	if (rc) {
		raw3270_put_view(&fp->view);
		raw3270_del_view(&fp->view);
		goto out;
	}
	nonseekable_open(inode, filp);
	filp->private_data = fp;
out:
	mutex_unlock(&fs3270_mutex);
	return rc;
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
	filp->private_data = NULL;
	if (fp) {
		put_pid(fp->fs_pid);
		fp->fs_pid = NULL;
		raw3270_reset(&fp->view);
		raw3270_put_view(&fp->view);
		raw3270_del_view(&fp->view);
	}
	return 0;
}

static const struct file_operations fs3270_fops = {
	.owner		 = THIS_MODULE,		/* owner */
	.read		 = fs3270_read,		/* read */
	.write		 = fs3270_write,	/* write */
	.unlocked_ioctl	 = fs3270_ioctl,	/* ioctl */
	.compat_ioctl	 = fs3270_ioctl,	/* ioctl */
	.open		 = fs3270_open,		/* open */
	.release	 = fs3270_close,	/* release */
};

/*
 * 3270 fullscreen driver initialization.
 */
static int __init
fs3270_init(void)
{
	int rc;

	rc = register_chrdev(IBM_FS3270_MAJOR, "fs3270", &fs3270_fops);
	if (rc)
		return rc;
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
