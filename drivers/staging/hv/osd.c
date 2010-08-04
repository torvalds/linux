/*
 *
 * Copyright (c) 2009, Microsoft Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place - Suite 330, Boston, MA 02111-1307 USA.
 *
 * Authors:
 *   Haiyang Zhang <haiyangz@microsoft.com>
 *   Hank Janssen  <hjanssen@microsoft.com>
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/highmem.h>
#include <linux/vmalloc.h>
#include <linux/ioport.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include <linux/kernel.h>
#include <linux/jiffies.h>
#include <linux/delay.h>
#include <linux/time.h>
#include <linux/io.h>
#include <linux/bitops.h>
#include <linux/slab.h>
#include "osd.h"

struct osd_callback_struct {
	struct work_struct work;
	void (*callback)(void *);
	void *data;
};

void *osd_VirtualAllocExec(unsigned int size)
{
#ifdef __x86_64__
	return __vmalloc(size, GFP_KERNEL, PAGE_KERNEL_EXEC);
#else
	return __vmalloc(size, GFP_KERNEL,
			 __pgprot(__PAGE_KERNEL & (~_PAGE_NX)));
#endif
}

/**
 * osd_PageAlloc() - Allocate pages
 * @count:      Total number of Kernel pages you want to allocate
 *
 * Tries to allocate @count number of consecutive free kernel pages.
 * And if successful, it will set the pages to 0 before returning.
 * If successfull it will return pointer to the @count pages.
 * Mainly used by Hyper-V drivers.
 */
void *osd_PageAlloc(unsigned int count)
{
	void *p;

	p = (void *)__get_free_pages(GFP_KERNEL, get_order(count * PAGE_SIZE));
	if (p)
		memset(p, 0, count * PAGE_SIZE);
	return p;

	/* struct page* page = alloc_page(GFP_KERNEL|__GFP_ZERO); */
	/* void *p; */

	/* BUGBUG: We need to use kmap in case we are in HIMEM region */
	/* p = page_address(page); */
	/* if (p) memset(p, 0, PAGE_SIZE); */
	/* return p; */
}
EXPORT_SYMBOL_GPL(osd_PageAlloc);

/**
 * osd_PageFree() - Free pages
 * @page:       Pointer to the first page to be freed
 * @count:      Total number of Kernel pages you free
 *
 * Frees the pages allocated by osd_PageAlloc()
 * Mainly used by Hyper-V drivers.
 */
void osd_PageFree(void *page, unsigned int count)
{
	free_pages((unsigned long)page, get_order(count * PAGE_SIZE));
	/*struct page* p = virt_to_page(page);
	__free_page(p);*/
}
EXPORT_SYMBOL_GPL(osd_PageFree);

/**
 * osd_WaitEventCreate() - Create the event queue
 *
 * Allocates memory for a &struct osd_waitevent. And then calls
 * init_waitqueue_head to set up the wait queue for the event.
 * This structure is usually part of a another structure that contains
 * the actual Hyper-V device driver structure.
 *
 * Returns pointer to &struct osd_waitevent
 * Mainly used by Hyper-V drivers.
 */
struct osd_waitevent *osd_WaitEventCreate(void)
{
	struct osd_waitevent *wait = kmalloc(sizeof(struct osd_waitevent),
					     GFP_KERNEL);
	if (!wait)
		return NULL;

	wait->condition = 0;
	init_waitqueue_head(&wait->event);
	return wait;
}
EXPORT_SYMBOL_GPL(osd_WaitEventCreate);


/**
 * osd_WaitEventSet() - Wake up the process
 * @waitEvent: Structure to event to be woken up
 *
 * @waitevent is of type &struct osd_waitevent
 *
 * Wake up the sleeping process so it can do some work.
 * And set condition indicator in &struct osd_waitevent to indicate
 * the process is in a woken state.
 *
 * Only used by Network and Storage Hyper-V drivers.
 */
void osd_WaitEventSet(struct osd_waitevent *waitEvent)
{
	waitEvent->condition = 1;
	wake_up_interruptible(&waitEvent->event);
}
EXPORT_SYMBOL_GPL(osd_WaitEventSet);

/**
 * osd_WaitEventWait() - Wait for event till condition is true
 * @waitEvent: Structure to event to be put to sleep
 *
 * @waitevent is of type &struct osd_waitevent
 *
 * Set up the process to sleep until waitEvent->condition get true.
 * And set condition indicator in &struct osd_waitevent to indicate
 * the process is in a sleeping state.
 *
 * Returns the status of 'wait_event_interruptible()' system call
 *
 * Mainly used by Hyper-V drivers.
 */
int osd_WaitEventWait(struct osd_waitevent *waitEvent)
{
	int ret = 0;

	ret = wait_event_interruptible(waitEvent->event,
				       waitEvent->condition);
	waitEvent->condition = 0;
	return ret;
}
EXPORT_SYMBOL_GPL(osd_WaitEventWait);

/**
 * osd_WaitEventWaitEx() - Wait for event or timeout for process wakeup
 * @waitEvent: Structure to event to be put to sleep
 * @TimeoutInMs:       Total number of Milliseconds to wait before waking up
 *
 * @waitevent is of type &struct osd_waitevent
 * Set up the process to sleep until @waitEvent->condition get true or
 * @TimeoutInMs (Time out in Milliseconds) has been reached.
 * And set condition indicator in &struct osd_waitevent to indicate
 * the process is in a sleeping state.
 *
 * Returns the status of 'wait_event_interruptible_timeout()' system call
 *
 * Mainly used by Hyper-V drivers.
 */
int osd_WaitEventWaitEx(struct osd_waitevent *waitEvent, u32 TimeoutInMs)
{
	int ret = 0;

	ret = wait_event_interruptible_timeout(waitEvent->event,
					       waitEvent->condition,
					       msecs_to_jiffies(TimeoutInMs));
	waitEvent->condition = 0;
	return ret;
}
EXPORT_SYMBOL_GPL(osd_WaitEventWaitEx);

static void osd_callback_work(struct work_struct *work)
{
	struct osd_callback_struct *cb = container_of(work,
						struct osd_callback_struct,
						work);
	(cb->callback)(cb->data);
	kfree(cb);
}

int osd_schedule_callback(struct workqueue_struct *wq,
			  void (*func)(void *),
			  void *data)
{
	struct osd_callback_struct *cb;

	cb = kmalloc(sizeof(*cb), GFP_KERNEL);
	if (!cb) {
		printk(KERN_ERR "unable to allocate memory in osd_schedule_callback\n");
		return -1;
	}

	cb->callback = func;
	cb->data = data;
	INIT_WORK(&cb->work, osd_callback_work);
	return queue_work(wq, &cb->work);
}

