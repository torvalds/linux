/*
 * The USB Monitor, inspired by Dave Harding's USBMon.
 *
 * This is the 's' or 'stat' reader which debugs usbmon itself.
 * Note that this code blows through locks, so make sure that
 * /dbg/usbmon/0s is well protected from non-root users.
 *
 */

#include <linux/kernel.h>
#include <linux/usb.h>
#include <linux/fs.h>
#include <asm/uaccess.h>

#include "usb_mon.h"

#define STAT_BUF_SIZE  80

struct snap {
	int slen;
	char str[STAT_BUF_SIZE];
};

static int mon_stat_open(struct inode *inode, struct file *file)
{
	struct mon_bus *mbus;
	struct snap *sp;

	if ((sp = kmalloc(sizeof(struct snap), GFP_KERNEL)) == NULL)
		return -ENOMEM;

	mbus = inode->i_private;

	sp->slen = snprintf(sp->str, STAT_BUF_SIZE,
	    "nreaders %d events %u text_lost %u\n",
	    mbus->nreaders, mbus->cnt_events, mbus->cnt_text_lost);

	file->private_data = sp;
	return 0;
}

static ssize_t mon_stat_read(struct file *file, char __user *buf,
				size_t nbytes, loff_t *ppos)
{
	struct snap *sp = file->private_data;

	return simple_read_from_buffer(buf, nbytes, ppos, sp->str, sp->slen);
}

static int mon_stat_release(struct inode *inode, struct file *file)
{
	struct snap *sp = file->private_data;
	file->private_data = NULL;
	kfree(sp);
	return 0;
}

const struct file_operations mon_fops_stat = {
	.owner =	THIS_MODULE,
	.open =		mon_stat_open,
	.llseek =	no_llseek,
	.read =		mon_stat_read,
	/* .write =	mon_stat_write, */
	/* .poll =		mon_stat_poll, */
	/* .ioctl =	mon_stat_ioctl, */
	.release =	mon_stat_release,
};
