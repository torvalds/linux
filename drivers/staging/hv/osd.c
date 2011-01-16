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

void *osd_virtual_alloc_exec(unsigned int size)
{
#ifdef __x86_64__
	return __vmalloc(size, GFP_KERNEL, PAGE_KERNEL_EXEC);
#else
	return __vmalloc(size, GFP_KERNEL,
			 __pgprot(__PAGE_KERNEL & (~_PAGE_NX)));
#endif
}

/**
 * osd_page_alloc() - Allocate pages
 * @count:      Total number of Kernel pages you want to allocate
 *
 * Tries to allocate @count number of consecutive free kernel pages.
 * And if successful, it will set the pages to 0 before returning.
 * If successfull it will return pointer to the @count pages.
 * Mainly used by Hyper-V drivers.
 */
void *osd_page_alloc(unsigned int count)
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
EXPORT_SYMBOL_GPL(osd_page_alloc);

/**
 * osd_page_free() - Free pages
 * @page:       Pointer to the first page to be freed
 * @count:      Total number of Kernel pages you free
 *
 * Frees the pages allocated by osd_page_alloc()
 * Mainly used by Hyper-V drivers.
 */
void osd_page_free(void *page, unsigned int count)
{
	free_pages((unsigned long)page, get_order(count * PAGE_SIZE));
	/*struct page* p = virt_to_page(page);
	__free_page(p);*/
}
EXPORT_SYMBOL_GPL(osd_page_free);

/**
 * osd_waitevent_create() - Create the event queue
 *
 * Allocates memory for a &struct osd_waitevent. And then calls
 * init_waitqueue_head to set up the wait queue for the event.
 * This structure is usually part of a another structure that contains
 * the actual Hyper-V device driver structure.
 *
 * Returns pointer to &struct osd_waitevent
 * Mainly used by Hyper-V drivers.
 */
struct osd_waitevent *osd_waitevent_create(void)
{
	struct osd_waitevent *wait = kmalloc(sizeof(struct osd_waitevent),
					     GFP_KERNEL);
	if (!wait)
		return NULL;

	wait->condition = 0;
	init_waitqueue_head(&wait->event);
	return wait;
}
EXPORT_SYMBOL_GPL(osd_waitevent_create);


/**
 * osd_waitevent_set() - Wake up the process
 * @wait_event: Structure to event to be woken up
 *
 * @wait_event is of type &struct osd_waitevent
 *
 * Wake up the sleeping process so it can do some work.
 * And set condition indicator in &struct osd_waitevent to indicate
 * the process is in a woken state.
 *
 * Only used by Network and Storage Hyper-V drivers.
 */
void osd_waitevent_set(struct osd_waitevent *wait_event)
{
	wait_event->condition = 1;
	wake_up_interruptible(&wait_event->event);
}
EXPORT_SYMBOL_GPL(osd_waitevent_set);

/**
 * osd_waitevent_wait() - Wait for event till condition is true
 * @wait_event: Structure to event to be put to sleep
 *
 * @wait_event is of type &struct osd_waitevent
 *
 * Set up the process to sleep until waitEvent->condition get true.
 * And set condition indicator in &struct osd_waitevent to indicate
 * the process is in a sleeping state.
 *
 * Returns the status of 'wait_event_interruptible()' system call
 *
 * Mainly used by Hyper-V drivers.
 */
int osd_waitevent_wait(struct osd_waitevent *wait_event)
{
	int ret = 0;

	ret = wait_event_interruptible(wait_event->event,
				       wait_event->condition);
	wait_event->condition = 0;
	return ret;
}
EXPORT_SYMBOL_GPL(osd_waitevent_wait);

/**
 * osd_waitevent_waitex() - Wait for event or timeout for process wakeup
 * @wait_event: Structure to event to be put to sleep
 * @timeout_in_ms:       Total number of Milliseconds to wait before waking up
 *
 * @wait_event is of type &struct osd_waitevent
 * Set up the process to sleep until @waitEvent->condition get true or
 * @timeout_in_ms (Time out in Milliseconds) has been reached.
 * And set condition indicator in &struct osd_waitevent to indicate
 * the process is in a sleeping state.
 *
 * Returns the status of 'wait_event_interruptible_timeout()' system call
 *
 * Mainly used by Hyper-V drivers.
 */
int osd_waitevent_waitex(struct osd_waitevent *wait_event, u32 timeout_in_ms)
{
	int ret = 0;

	ret = wait_event_interruptible_timeout(wait_event->event,
					       wait_event->condition,
					       msecs_to_jiffies(timeout_in_ms));
	wait_event->condition = 0;
	return ret;
}
EXPORT_SYMBOL_GPL(osd_waitevent_waitex);
