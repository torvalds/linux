/*
 * Copyright (C) 2005 MIPS Technologies, Inc.  All rights reserved.
 * Copyright (C) 2005, 06 Ralf Baechle (ralf@linux-mips.org)
 *
 *  This program is free software; you can distribute it and/or modify it
 *  under the terms of the GNU General Public License (Version 2) as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 */

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <asm/uaccess.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/vmalloc.h>
#include <linux/elf.h>
#include <linux/seq_file.h>
#include <linux/syscalls.h>
#include <linux/moduleloader.h>
#include <linux/interrupt.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <asm/mipsmtregs.h>
#include <asm/mips_mt.h>
#include <asm/cacheflush.h>
#include <asm/atomic.h>
#include <asm/cpu.h>
#include <asm/processor.h>
#include <asm/system.h>
#include <asm/vpe.h>
#include <asm/rtlx.h>

static struct rtlx_info *rtlx;
static int major;
static char module_name[] = "rtlx";

static struct chan_waitqueues {
	wait_queue_head_t rt_queue;
	wait_queue_head_t lx_queue;
	atomic_t in_open;
	struct mutex mutex;
} channel_wqs[RTLX_CHANNELS];

static struct vpe_notifications notify;
static int sp_stopping;

extern void *vpe_get_shared(int index);

static void rtlx_dispatch(void)
{
	do_IRQ(MIPS_CPU_IRQ_BASE + MIPS_CPU_RTLX_IRQ);
}


/* Interrupt handler may be called before rtlx_init has otherwise had
   a chance to run.
*/
static irqreturn_t rtlx_interrupt(int irq, void *dev_id)
{
	unsigned int vpeflags;
	unsigned long flags;
	int i;

	/* Ought not to be strictly necessary for SMTC builds */
	local_irq_save(flags);
	vpeflags = dvpe();
	set_c0_status(0x100 << MIPS_CPU_RTLX_IRQ);
	irq_enable_hazard();
	evpe(vpeflags);
	local_irq_restore(flags);

	for (i = 0; i < RTLX_CHANNELS; i++) {
			wake_up(&channel_wqs[i].lx_queue);
			wake_up(&channel_wqs[i].rt_queue);
	}

	return IRQ_HANDLED;
}

static void __used dump_rtlx(void)
{
	int i;

	printk("id 0x%lx state %d\n", rtlx->id, rtlx->state);

	for (i = 0; i < RTLX_CHANNELS; i++) {
		struct rtlx_channel *chan = &rtlx->channel[i];

		printk(" rt_state %d lx_state %d buffer_size %d\n",
		       chan->rt_state, chan->lx_state, chan->buffer_size);

		printk(" rt_read %d rt_write %d\n",
		       chan->rt_read, chan->rt_write);

		printk(" lx_read %d lx_write %d\n",
		       chan->lx_read, chan->lx_write);

		printk(" rt_buffer <%s>\n", chan->rt_buffer);
		printk(" lx_buffer <%s>\n", chan->lx_buffer);
	}
}

/* call when we have the address of the shared structure from the SP side. */
static int rtlx_init(struct rtlx_info *rtlxi)
{
	if (rtlxi->id != RTLX_ID) {
		printk(KERN_ERR "no valid RTLX id at 0x%p 0x%lx\n",
			rtlxi, rtlxi->id);
		return -ENOEXEC;
	}

	rtlx = rtlxi;

	return 0;
}

/* notifications */
static void starting(int vpe)
{
	int i;
	sp_stopping = 0;

	/* force a reload of rtlx */
	rtlx=NULL;

	/* wake up any sleeping rtlx_open's */
	for (i = 0; i < RTLX_CHANNELS; i++)
		wake_up_interruptible(&channel_wqs[i].lx_queue);
}

static void stopping(int vpe)
{
	int i;

	sp_stopping = 1;
	for (i = 0; i < RTLX_CHANNELS; i++)
		wake_up_interruptible(&channel_wqs[i].lx_queue);
}


int rtlx_open(int index, int can_sleep)
{
	struct rtlx_info **p;
	struct rtlx_channel *chan;
	enum rtlx_state state;
	int ret = 0;

	if (index >= RTLX_CHANNELS) {
		printk(KERN_DEBUG "rtlx_open index out of range\n");
		return -ENOSYS;
	}

	if (atomic_inc_return(&channel_wqs[index].in_open) > 1) {
		printk(KERN_DEBUG "rtlx_open channel %d already opened\n",
		       index);
		ret = -EBUSY;
		goto out_fail;
	}

	if (rtlx == NULL) {
		if( (p = vpe_get_shared(tclimit)) == NULL) {
		    if (can_sleep) {
			__wait_event_interruptible(channel_wqs[index].lx_queue,
				(p = vpe_get_shared(tclimit)), ret);
			if (ret)
				goto out_fail;
		    } else {
			printk(KERN_DEBUG "No SP program loaded, and device "
					"opened with O_NONBLOCK\n");
			ret = -ENOSYS;
			goto out_fail;
		    }
		}

		smp_rmb();
		if (*p == NULL) {
			if (can_sleep) {
				DEFINE_WAIT(wait);

				for (;;) {
					prepare_to_wait(
						&channel_wqs[index].lx_queue,
						&wait, TASK_INTERRUPTIBLE);
					smp_rmb();
					if (*p != NULL)
						break;
					if (!signal_pending(current)) {
						schedule();
						continue;
					}
					ret = -ERESTARTSYS;
					goto out_fail;
				}
				finish_wait(&channel_wqs[index].lx_queue, &wait);
			} else {
				pr_err(" *vpe_get_shared is NULL. "
				       "Has an SP program been loaded?\n");
				ret = -ENOSYS;
				goto out_fail;
			}
		}

		if ((unsigned int)*p < KSEG0) {
			printk(KERN_WARNING "vpe_get_shared returned an "
			       "invalid pointer maybe an error code %d\n",
			       (int)*p);
			ret = -ENOSYS;
			goto out_fail;
		}

		if ((ret = rtlx_init(*p)) < 0)
			goto out_ret;
	}

	chan = &rtlx->channel[index];

	state = xchg(&chan->lx_state, RTLX_STATE_OPENED);
	if (state == RTLX_STATE_OPENED) {
		ret = -EBUSY;
		goto out_fail;
	}

out_fail:
	smp_mb();
	atomic_dec(&channel_wqs[index].in_open);
	smp_mb();

out_ret:
	return ret;
}

int rtlx_release(int index)
{
	if (rtlx == NULL) {
		pr_err("rtlx_release() with null rtlx\n");
		return 0;
	}
	rtlx->channel[index].lx_state = RTLX_STATE_UNUSED;
	return 0;
}

unsigned int rtlx_read_poll(int index, int can_sleep)
{
 	struct rtlx_channel *chan;

 	if (rtlx == NULL)
 		return 0;

 	chan = &rtlx->channel[index];

	/* data available to read? */
	if (chan->lx_read == chan->lx_write) {
		if (can_sleep) {
			int ret = 0;

			__wait_event_interruptible(channel_wqs[index].lx_queue,
				(chan->lx_read != chan->lx_write) ||
				sp_stopping, ret);
			if (ret)
				return ret;

			if (sp_stopping)
				return 0;
		} else
			return 0;
	}

	return (chan->lx_write + chan->buffer_size - chan->lx_read)
	       % chan->buffer_size;
}

static inline int write_spacefree(int read, int write, int size)
{
	if (read == write) {
		/*
		 * Never fill the buffer completely, so indexes are always
		 * equal if empty and only empty, or !equal if data available
		 */
		return size - 1;
	}

	return ((read + size - write) % size) - 1;
}

unsigned int rtlx_write_poll(int index)
{
	struct rtlx_channel *chan = &rtlx->channel[index];

	return write_spacefree(chan->rt_read, chan->rt_write,
				chan->buffer_size);
}

ssize_t rtlx_read(int index, void __user *buff, size_t count)
{
	size_t lx_write, fl = 0L;
	struct rtlx_channel *lx;
	unsigned long failed;

	if (rtlx == NULL)
		return -ENOSYS;

	lx = &rtlx->channel[index];

	mutex_lock(&channel_wqs[index].mutex);
	smp_rmb();
	lx_write = lx->lx_write;

	/* find out how much in total */
	count = min(count,
		     (size_t)(lx_write + lx->buffer_size - lx->lx_read)
		     % lx->buffer_size);

	/* then how much from the read pointer onwards */
	fl = min(count, (size_t)lx->buffer_size - lx->lx_read);

	failed = copy_to_user(buff, lx->lx_buffer + lx->lx_read, fl);
	if (failed)
		goto out;

	/* and if there is anything left at the beginning of the buffer */
	if (count - fl)
		failed = copy_to_user(buff + fl, lx->lx_buffer, count - fl);

out:
	count -= failed;

	smp_wmb();
	lx->lx_read = (lx->lx_read + count) % lx->buffer_size;
	smp_wmb();
	mutex_unlock(&channel_wqs[index].mutex);

	return count;
}

ssize_t rtlx_write(int index, const void __user *buffer, size_t count)
{
	struct rtlx_channel *rt;
	unsigned long failed;
	size_t rt_read;
	size_t fl;

	if (rtlx == NULL)
		return(-ENOSYS);

	rt = &rtlx->channel[index];

	mutex_lock(&channel_wqs[index].mutex);
	smp_rmb();
	rt_read = rt->rt_read;

	/* total number of bytes to copy */
	count = min(count, (size_t)write_spacefree(rt_read, rt->rt_write,
							rt->buffer_size));

	/* first bit from write pointer to the end of the buffer, or count */
	fl = min(count, (size_t) rt->buffer_size - rt->rt_write);

	failed = copy_from_user(rt->rt_buffer + rt->rt_write, buffer, fl);
	if (failed)
		goto out;

	/* if there's any left copy to the beginning of the buffer */
	if (count - fl) {
		failed = copy_from_user(rt->rt_buffer, buffer + fl, count - fl);
	}

out:
	count -= failed;

	smp_wmb();
	rt->rt_write = (rt->rt_write + count) % rt->buffer_size;
	smp_wmb();
	mutex_unlock(&channel_wqs[index].mutex);

	return count;
}


static int file_open(struct inode *inode, struct file *filp)
{
	return rtlx_open(iminor(inode), (filp->f_flags & O_NONBLOCK) ? 0 : 1);
}

static int file_release(struct inode *inode, struct file *filp)
{
	return rtlx_release(iminor(inode));
}

static unsigned int file_poll(struct file *file, poll_table * wait)
{
	int minor;
	unsigned int mask = 0;

	minor = iminor(file->f_path.dentry->d_inode);

	poll_wait(file, &channel_wqs[minor].rt_queue, wait);
	poll_wait(file, &channel_wqs[minor].lx_queue, wait);

	if (rtlx == NULL)
		return 0;

	/* data available to read? */
	if (rtlx_read_poll(minor, 0))
		mask |= POLLIN | POLLRDNORM;

	/* space to write */
	if (rtlx_write_poll(minor))
		mask |= POLLOUT | POLLWRNORM;

	return mask;
}

static ssize_t file_read(struct file *file, char __user * buffer, size_t count,
			 loff_t * ppos)
{
	int minor = iminor(file->f_path.dentry->d_inode);

	/* data available? */
	if (!rtlx_read_poll(minor, (file->f_flags & O_NONBLOCK) ? 0 : 1)) {
		return 0;	// -EAGAIN makes cat whinge
	}

	return rtlx_read(minor, buffer, count);
}

static ssize_t file_write(struct file *file, const char __user * buffer,
			  size_t count, loff_t * ppos)
{
	int minor;
	struct rtlx_channel *rt;

	minor = iminor(file->f_path.dentry->d_inode);
	rt = &rtlx->channel[minor];

	/* any space left... */
	if (!rtlx_write_poll(minor)) {
		int ret = 0;

		if (file->f_flags & O_NONBLOCK)
			return -EAGAIN;

		__wait_event_interruptible(channel_wqs[minor].rt_queue,
		                           rtlx_write_poll(minor),
		                           ret);
		if (ret)
			return ret;
	}

	return rtlx_write(minor, buffer, count);
}

static const struct file_operations rtlx_fops = {
	.owner =   THIS_MODULE,
	.open =    file_open,
	.release = file_release,
	.write =   file_write,
	.read =    file_read,
	.poll =    file_poll
};

static struct irqaction rtlx_irq = {
	.handler	= rtlx_interrupt,
	.flags		= IRQF_DISABLED,
	.name		= "RTLX",
};

static int rtlx_irq_num = MIPS_CPU_IRQ_BASE + MIPS_CPU_RTLX_IRQ;

static char register_chrdev_failed[] __initdata =
	KERN_ERR "rtlx_module_init: unable to register device\n";

static int __init rtlx_module_init(void)
{
	struct device *dev;
	int i, err;

	if (!cpu_has_mipsmt) {
		printk("VPE loader: not a MIPS MT capable processor\n");
		return -ENODEV;
	}

	if (tclimit == 0) {
		printk(KERN_WARNING "No TCs reserved for AP/SP, not "
		       "initializing RTLX.\nPass maxtcs=<n> argument as kernel "
		       "argument\n");

		return -ENODEV;
	}

	major = register_chrdev(0, module_name, &rtlx_fops);
	if (major < 0) {
		printk(register_chrdev_failed);
		return major;
	}

	/* initialise the wait queues */
	for (i = 0; i < RTLX_CHANNELS; i++) {
		init_waitqueue_head(&channel_wqs[i].rt_queue);
		init_waitqueue_head(&channel_wqs[i].lx_queue);
		atomic_set(&channel_wqs[i].in_open, 0);
		mutex_init(&channel_wqs[i].mutex);

		dev = device_create(mt_class, NULL, MKDEV(major, i), NULL,
				    "%s%d", module_name, i);
		if (IS_ERR(dev)) {
			err = PTR_ERR(dev);
			goto out_chrdev;
		}
	}

	/* set up notifiers */
	notify.start = starting;
	notify.stop = stopping;
	vpe_notify(tclimit, &notify);

	if (cpu_has_vint)
		set_vi_handler(MIPS_CPU_RTLX_IRQ, rtlx_dispatch);
	else {
		pr_err("APRP RTLX init on non-vectored-interrupt processor\n");
		err = -ENODEV;
		goto out_chrdev;
	}

	rtlx_irq.dev_id = rtlx;
	setup_irq(rtlx_irq_num, &rtlx_irq);

	return 0;

out_chrdev:
	for (i = 0; i < RTLX_CHANNELS; i++)
		device_destroy(mt_class, MKDEV(major, i));

	return err;
}

static void __exit rtlx_module_exit(void)
{
	int i;

	for (i = 0; i < RTLX_CHANNELS; i++)
		device_destroy(mt_class, MKDEV(major, i));

	unregister_chrdev(major, module_name);
}

module_init(rtlx_module_init);
module_exit(rtlx_module_exit);

MODULE_DESCRIPTION("MIPS RTLX");
MODULE_AUTHOR("Elizabeth Oldham, MIPS Technologies, Inc.");
MODULE_LICENSE("GPL");
