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

#define RTLX_TARG_VPE 1

static struct rtlx_info *rtlx;
static int major;
static char module_name[] = "rtlx";

static struct chan_waitqueues {
	wait_queue_head_t rt_queue;
	wait_queue_head_t lx_queue;
	atomic_t in_open;
} channel_wqs[RTLX_CHANNELS];

static struct irqaction irq;
static int irq_num;
static struct vpe_notifications notify;
static int sp_stopping = 0;

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
	int i;

	for (i = 0; i < RTLX_CHANNELS; i++) {
			wake_up(&channel_wqs[i].lx_queue);
			wake_up(&channel_wqs[i].rt_queue);
	}

	return IRQ_HANDLED;
}

static __attribute_used__ void dump_rtlx(void)
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
		printk(KERN_ERR "no valid RTLX id at 0x%p 0x%x\n", rtlxi, rtlxi->id);
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
	volatile struct rtlx_info **p;
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
		if( (p = vpe_get_shared(RTLX_TARG_VPE)) == NULL) {
			if (can_sleep) {
				__wait_event_interruptible(channel_wqs[index].lx_queue,
				                           (p = vpe_get_shared(RTLX_TARG_VPE)),
				                           ret);
				if (ret)
					goto out_fail;
			} else {
				printk(KERN_DEBUG "No SP program loaded, and device "
					"opened with O_NONBLOCK\n");
				ret = -ENOSYS;
				goto out_fail;
			}
		}

		if (*p == NULL) {
			if (can_sleep) {
				__wait_event_interruptible(channel_wqs[index].lx_queue,
				                           *p != NULL,
				                           ret);
				if (ret)
					goto out_fail;
			} else {
				printk(" *vpe_get_shared is NULL. "
				       "Has an SP program been loaded?\n");
				ret = -ENOSYS;
				goto out_fail;
			}
		}

		if ((unsigned int)*p < KSEG0) {
			printk(KERN_WARNING "vpe_get_shared returned an invalid pointer "
			       "maybe an error code %d\n", (int)*p);
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
			                           chan->lx_read != chan->lx_write || sp_stopping,
			                           ret);
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
	return write_spacefree(chan->rt_read, chan->rt_write, chan->buffer_size);
}

static inline void copy_to(void *dst, void *src, size_t count, int user)
{
	if (user)
		copy_to_user(dst, src, count);
	else
		memcpy(dst, src, count);
}

static inline void copy_from(void *dst, void *src, size_t count, int user)
{
	if (user)
		copy_from_user(dst, src, count);
	else
		memcpy(dst, src, count);
}

ssize_t rtlx_read(int index, void *buff, size_t count, int user)
{
	size_t fl = 0L;
	struct rtlx_channel *lx;

	if (rtlx == NULL)
		return -ENOSYS;

	lx = &rtlx->channel[index];

	/* find out how much in total */
	count = min(count,
		     (size_t)(lx->lx_write + lx->buffer_size - lx->lx_read)
		     % lx->buffer_size);

	/* then how much from the read pointer onwards */
	fl = min( count, (size_t)lx->buffer_size - lx->lx_read);

	copy_to(buff, &lx->lx_buffer[lx->lx_read], fl, user);

	/* and if there is anything left at the beginning of the buffer */
	if ( count - fl )
		copy_to (buff + fl, lx->lx_buffer, count - fl, user);

	/* update the index */
	lx->lx_read += count;
	lx->lx_read %= lx->buffer_size;

	return count;
}

ssize_t rtlx_write(int index, void *buffer, size_t count, int user)
{
	struct rtlx_channel *rt;
	size_t fl;

	if (rtlx == NULL)
		return(-ENOSYS);

	rt = &rtlx->channel[index];

	/* total number of bytes to copy */
	count = min(count,
		    (size_t)write_spacefree(rt->rt_read, rt->rt_write,
					    rt->buffer_size));

	/* first bit from write pointer to the end of the buffer, or count */
	fl = min(count, (size_t) rt->buffer_size - rt->rt_write);

	copy_from (&rt->rt_buffer[rt->rt_write], buffer, fl, user);

	/* if there's any left copy to the beginning of the buffer */
	if( count - fl )
		copy_from (rt->rt_buffer, buffer + fl, count - fl, user);

	rt->rt_write += count;
	rt->rt_write %= rt->buffer_size;

	return(count);
}


static int file_open(struct inode *inode, struct file *filp)
{
	int minor = iminor(inode);

	return rtlx_open(minor, (filp->f_flags & O_NONBLOCK) ? 0 : 1);
}

static int file_release(struct inode *inode, struct file *filp)
{
	int minor = iminor(inode);

	return rtlx_release(minor);
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

	return rtlx_read(minor, buffer, count, 1);
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

	return rtlx_write(minor, (void *)buffer, count, 1);
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

static int rtlx_module_init(void)
{
	struct device *dev;
	int i, err;

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

		dev = device_create(mt_class, NULL, MKDEV(major, i),
		                    "%s%d", module_name, i);
		if (IS_ERR(dev)) {
			err = PTR_ERR(dev);
			goto out_chrdev;
		}
	}

	/* set up notifiers */
	notify.start = starting;
	notify.stop = stopping;
	vpe_notify(RTLX_TARG_VPE, &notify);

	if (cpu_has_vint)
		set_vi_handler(MIPS_CPU_RTLX_IRQ, rtlx_dispatch);

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
