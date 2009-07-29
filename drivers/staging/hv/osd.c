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
#include <linux/wait.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include <linux/kernel.h>
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <linux/delay.h>
#include <linux/time.h>

#include <asm/io.h>
#include <asm/bitops.h>
#include <asm/kmap_types.h>
#include <asm/atomic.h>

#include "include/osd.h"


/* Data types */


typedef struct _WORKITEM {
	struct work_struct work;
	PFN_WORKITEM_CALLBACK callback;
	void* context;
} WORKITEM;


void BitSet(unsigned int* addr, int bit)
{
	set_bit(bit, (unsigned long*)addr);
}

int BitTest(unsigned int* addr, int bit)
{
	return test_bit(bit, (unsigned long*)addr);
}

void BitClear(unsigned int* addr, int bit)
{
	clear_bit(bit, (unsigned long*)addr);
}

int BitTestAndClear(unsigned int* addr, int bit)
{
	return test_and_clear_bit(bit, (unsigned long*)addr);
}

int BitTestAndSet(unsigned int* addr, int bit)
{
	return test_and_set_bit(bit, (unsigned long*)addr);
}


int InterlockedIncrement(int *val)
{
	return atomic_inc_return((atomic_t*)val);
}

int InterlockedDecrement(int *val)
{
	return atomic_dec_return((atomic_t*)val);
}

#ifndef atomic_cmpxchg
#define atomic_cmpxchg(v, old, new) ((int)cmpxchg(&((v)->counter), old, new))
#endif
int InterlockedCompareExchange(int *val, int new, int curr)
{
	/* return ((int)cmpxchg(((atomic_t*)val), curr, new)); */
	return atomic_cmpxchg((atomic_t*)val, curr, new);

}

void* VirtualAllocExec(unsigned int size)
{
#ifdef __x86_64__
	return __vmalloc(size, GFP_KERNEL, PAGE_KERNEL_EXEC);
#else
	return __vmalloc(size, GFP_KERNEL, __pgprot(__PAGE_KERNEL & (~_PAGE_NX)));
#endif
}

void VirtualFree(void* VirtAddr)
{
	return vfree(VirtAddr);
}

void* PageAlloc(unsigned int count)
{
	void *p;
	p = (void *)__get_free_pages(GFP_KERNEL, get_order(count * PAGE_SIZE));
	if (p) memset(p, 0, count * PAGE_SIZE);
	return p;

	/* struct page* page = alloc_page(GFP_KERNEL|__GFP_ZERO); */
	/* void *p; */

	/* BUGBUG: We need to use kmap in case we are in HIMEM region */
	/* p = page_address(page); */
	/* if (p) memset(p, 0, PAGE_SIZE); */
	/* return p; */
}

void PageFree(void* page, unsigned int count)
{
	free_pages((unsigned long)page, get_order(count * PAGE_SIZE));
	/*struct page* p = virt_to_page(page);
	__free_page(p);*/
}


void* PageMapVirtualAddress(unsigned long Pfn)
{
	return kmap_atomic(pfn_to_page(Pfn), KM_IRQ0);
}

void PageUnmapVirtualAddress(void* VirtAddr)
{
	kunmap_atomic(VirtAddr, KM_IRQ0);
}

void *MemMapIO(unsigned long phys, unsigned long size)
{
	return (void*)GetVirtualAddress(phys); /* return ioremap_nocache(phys, size); */
}

void MemUnmapIO(void *virt)
{
	/* iounmap(virt); */
}

static void TimerCallback(unsigned long data)
{
	struct osd_timer *t = (struct osd_timer *) data;

	t->callback(t->context);
}

struct osd_timer *TimerCreate(PFN_TIMER_CALLBACK pfnTimerCB, void* context)
{
	struct osd_timer *t = kmalloc(sizeof(struct osd_timer), GFP_KERNEL);
	if (!t)
	{
		return NULL;
	}

	t->callback = pfnTimerCB;
	t->context = context;

	init_timer(&t->timer);
	t->timer.data = (unsigned long)t;
	t->timer.function = TimerCallback;

	return t;
}

void TimerStart(struct osd_timer *t, u32 expirationInUs)
{
	t->timer.expires = jiffies + usecs_to_jiffies(expirationInUs);
	add_timer(&t->timer);
}

int TimerStop(struct osd_timer *t)
{
	return del_timer(&t->timer);
}

void TimerClose(struct osd_timer *t)
{
	del_timer(&t->timer);
	kfree(t);
}

struct osd_waitevent *WaitEventCreate(void)
{
	struct osd_waitevent *wait = kmalloc(sizeof(struct osd_waitevent), GFP_KERNEL);
	if (!wait)
	{
		return NULL;
	}

	wait->condition = 0;
	init_waitqueue_head(&wait->event);
	return wait;
}

void WaitEventClose(struct osd_waitevent *waitEvent)
{
	kfree(waitEvent);
}

void WaitEventSet(struct osd_waitevent *waitEvent)
{
	waitEvent->condition = 1;
	wake_up_interruptible(&waitEvent->event);
}

int WaitEventWait(struct osd_waitevent *waitEvent)
{
	int ret=0;

	ret = wait_event_interruptible(waitEvent->event,
				       waitEvent->condition);
	waitEvent->condition = 0;
	return ret;
}

int WaitEventWaitEx(struct osd_waitevent *waitEvent, u32 TimeoutInMs)
{
	int ret=0;

	ret = wait_event_interruptible_timeout(waitEvent->event,
					       waitEvent->condition,
					       msecs_to_jiffies(TimeoutInMs));
	waitEvent->condition = 0;
	return ret;
}

void* Physical2LogicalAddr(unsigned long PhysAddr)
{
	void* logicalAddr = phys_to_virt(PhysAddr);
	BUG_ON(!virt_addr_valid(logicalAddr));
	return logicalAddr;
}

unsigned long Logical2PhysicalAddr(void * LogicalAddr)
{
	BUG_ON(!virt_addr_valid(LogicalAddr));
	return virt_to_phys(LogicalAddr);
}


unsigned long Virtual2Physical(void * VirtAddr)
{
	unsigned long pfn = vmalloc_to_pfn(VirtAddr);

	return pfn << PAGE_SHIFT;
}

static void WorkItemCallback(struct work_struct *work)
{
	WORKITEM* w = (WORKITEM*)work;

	w->callback(w->context);

	kfree(w);
}

struct workqueue_struct *WorkQueueCreate(char *name)
{
	struct workqueue_struct *wq;
	wq = create_workqueue(name);
	if (unlikely(!wq))
		return NULL;
	return wq;
}

void WorkQueueClose(struct workqueue_struct *hWorkQueue)
{
	destroy_workqueue(hWorkQueue);
	return;
}

int WorkQueueQueueWorkItem(struct workqueue_struct *hWorkQueue,
			   PFN_WORKITEM_CALLBACK workItem,
			   void* context)
{
	WORKITEM* w = kmalloc(sizeof(WORKITEM), GFP_ATOMIC);
	if (!w)
	{
		return -1;
	}

	w->callback = workItem,
	w->context = context;
	INIT_WORK(&w->work, WorkItemCallback);
	return queue_work(hWorkQueue, &w->work);
}

void QueueWorkItem(PFN_WORKITEM_CALLBACK workItem, void* context)
{
	WORKITEM* w = kmalloc(sizeof(WORKITEM), GFP_ATOMIC);
	if (!w)
	{
		return;
	}

	w->callback = workItem,
	w->context = context;
	INIT_WORK(&w->work, WorkItemCallback);
	schedule_work(&w->work);
}
