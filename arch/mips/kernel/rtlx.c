/*
 * Copyright (C) 2005 MIPS Technologies, Inc.  All rights reserved.
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
#include <asm/cacheflush.h>
#include <asm/atomic.h>
#include <asm/cpu.h>
#include <asm/processor.h>
#include <asm/system.h>
#include <asm/rtlx.h>

#define RTLX_MAJOR 64
#define RTLX_TARG_VPE 1

struct rtlx_info *rtlx;
static int major;
static char module_name[] = "rtlx";
static inline int spacefree(int read, int write, int size);

static struct chan_waitqueues {
	wait_queue_head_t rt_queue;
	wait_queue_head_t lx_queue;
} channel_wqs[RTLX_CHANNELS];

static struct irqaction irq;
static int irq_num;

extern void *vpe_get_shared(int index);

static void rtlx_dispatch(struct pt_regs *regs)
{
	do_IRQ(MIPSCPU_INT_BASE + MIPS_CPU_RTLX_IRQ, regs);
}

irqreturn_t rtlx_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	irqreturn_t r = IRQ_HANDLED;
	int i;

	for (i = 0; i < RTLX_CHANNELS; i++) {
		struct rtlx_channel *chan = &rtlx->channel[i];

		if (chan->lx_read != chan->lx_write)
			wake_up_interruptible(&channel_wqs[i].lx_queue);
	}

	return r;
}

void dump_rtlx(void)
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
	int i;

	if (rtlxi->id != RTLX_ID) {
		printk(KERN_WARNING "no valid RTLX id at 0x%p\n", rtlxi);
		return (-ENOEXEC);
	}

	/* initialise the wait queues */
	for (i = 0; i < RTLX_CHANNELS; i++) {
		init_waitqueue_head(&channel_wqs[i].rt_queue);
		init_waitqueue_head(&channel_wqs[i].lx_queue);
	}

	/* set up for interrupt handling */
	memset(&irq, 0, sizeof(struct irqaction));

	if (cpu_has_vint) {
		set_vi_handler(MIPS_CPU_RTLX_IRQ, rtlx_dispatch);
	}

	irq_num = MIPSCPU_INT_BASE + MIPS_CPU_RTLX_IRQ;
	irq.handler = rtlx_interrupt;
	irq.flags = SA_INTERRUPT;
	irq.name = "RTLX";
	irq.dev_id = rtlx;
	setup_irq(irq_num, &irq);

	rtlx = rtlxi;
	return (0);
}

/* only allow one open process at a time to open each channel */
static int rtlx_open(struct inode *inode, struct file *filp)
{
	int minor, ret;
	struct rtlx_channel *chan;

	/* assume only 1 device at the mo. */
	minor = MINOR(inode->i_rdev);

	if (rtlx == NULL) {
		struct rtlx_info **p;
		if( (p = vpe_get_shared(RTLX_TARG_VPE)) == NULL) {
			printk(" vpe_get_shared is NULL. Has an SP program been loaded?\n");
			return (-EFAULT);
		}

		if (*p == NULL) {
			printk(" vpe_shared %p %p\n", p, *p);
			return (-EFAULT);
		}

		if ((ret = rtlx_init(*p)) < 0)
			return (ret);
	}

	chan = &rtlx->channel[minor];

	/* already open? */
	if (chan->lx_state == RTLX_STATE_OPENED)
		return (-EBUSY);

	chan->lx_state = RTLX_STATE_OPENED;
	return (0);
}

static int rtlx_release(struct inode *inode, struct file *filp)
{
	int minor;

	minor = MINOR(inode->i_rdev);
	rtlx->channel[minor].lx_state = RTLX_STATE_UNUSED;
	return (0);
}

static unsigned int rtlx_poll(struct file *file, poll_table * wait)
{
	int minor;
	unsigned int mask = 0;
	struct rtlx_channel *chan;

	minor = MINOR(file->f_dentry->d_inode->i_rdev);
	chan = &rtlx->channel[minor];

	poll_wait(file, &channel_wqs[minor].rt_queue, wait);
	poll_wait(file, &channel_wqs[minor].lx_queue, wait);

	/* data available to read? */
	if (chan->lx_read != chan->lx_write)
		mask |= POLLIN | POLLRDNORM;

	/* space to write */
	if (spacefree(chan->rt_read, chan->rt_write, chan->buffer_size))
		mask |= POLLOUT | POLLWRNORM;

	return (mask);
}

static ssize_t rtlx_read(struct file *file, char __user * buffer, size_t count,
			 loff_t * ppos)
{
	size_t fl = 0L;
	int minor;
	struct rtlx_channel *lx;
	DECLARE_WAITQUEUE(wait, current);

	minor = MINOR(file->f_dentry->d_inode->i_rdev);
	lx = &rtlx->channel[minor];

	/* data available? */
	if (lx->lx_write == lx->lx_read) {
		if (file->f_flags & O_NONBLOCK)
			return (0);	// -EAGAIN makes cat whinge

		/* go to sleep */
		add_wait_queue(&channel_wqs[minor].lx_queue, &wait);
		set_current_state(TASK_INTERRUPTIBLE);

		while (lx->lx_write == lx->lx_read)
			schedule();

		set_current_state(TASK_RUNNING);
		remove_wait_queue(&channel_wqs[minor].lx_queue, &wait);

		/* back running */
	}

	/* find out how much in total */
	count = min( count,
		     (size_t)(lx->lx_write + lx->buffer_size - lx->lx_read) % lx->buffer_size);

	/* then how much from the read pointer onwards */
	fl = min( count, (size_t)lx->buffer_size - lx->lx_read);

	copy_to_user (buffer, &lx->lx_buffer[lx->lx_read], fl);

	/* and if there is anything left at the beginning of the buffer */
	if ( count - fl )
		copy_to_user (buffer + fl, lx->lx_buffer, count - fl);

	/* update the index */
	lx->lx_read += count;
	lx->lx_read %= lx->buffer_size;

	return (count);
}

static inline int spacefree(int read, int write, int size)
{
	if (read == write) {
		/* never fill the buffer completely, so indexes are always equal if empty
		   and only empty, or !equal if data available */
		return (size - 1);
	}

	return ((read + size - write) % size) - 1;
}

static ssize_t rtlx_write(struct file *file, const char __user * buffer,
			  size_t count, loff_t * ppos)
{
	int minor;
	struct rtlx_channel *rt;
	size_t fl;
	DECLARE_WAITQUEUE(wait, current);

	minor = MINOR(file->f_dentry->d_inode->i_rdev);
	rt = &rtlx->channel[minor];

	/* any space left... */
	if (!spacefree(rt->rt_read, rt->rt_write, rt->buffer_size)) {

		if (file->f_flags & O_NONBLOCK)
			return (-EAGAIN);

		add_wait_queue(&channel_wqs[minor].rt_queue, &wait);
		set_current_state(TASK_INTERRUPTIBLE);

		while (!spacefree(rt->rt_read, rt->rt_write, rt->buffer_size))
			schedule();

		set_current_state(TASK_RUNNING);
		remove_wait_queue(&channel_wqs[minor].rt_queue, &wait);
	}

	/* total number of bytes to copy */
	count = min( count, (size_t)spacefree(rt->rt_read, rt->rt_write, rt->buffer_size) );

	/* first bit from write pointer to the end of the buffer, or count */
	fl = min(count, (size_t) rt->buffer_size - rt->rt_write);

	copy_from_user(&rt->rt_buffer[rt->rt_write], buffer, fl);

	/* if there's any left copy to the beginning of the buffer */
	if( count - fl )
		copy_from_user(rt->rt_buffer, buffer + fl, count - fl);

	rt->rt_write += count;
	rt->rt_write %= rt->buffer_size;

	return(count);
}

static struct file_operations rtlx_fops = {
	.owner = THIS_MODULE,
	.open = rtlx_open,
	.release = rtlx_release,
	.write = rtlx_write,
	.read = rtlx_read,
	.poll = rtlx_poll
};

static int rtlx_module_init(void)
{
	if ((major = register_chrdev(RTLX_MAJOR, module_name, &rtlx_fops)) < 0) {
		printk("rtlx_module_init: unable to register device\n");
		return (-EBUSY);
	}

	if (major == 0)
		major = RTLX_MAJOR;

	return (0);
}

static void rtlx_module_exit(void)
{
	unregister_chrdev(major, module_name);
}

module_init(rtlx_module_init);
module_exit(rtlx_module_exit);
MODULE_DESCRIPTION("MIPS RTLX");
MODULE_AUTHOR("Elizabeth Clarke, MIPS Technologies, Inc");
MODULE_LICENSE("GPL");
