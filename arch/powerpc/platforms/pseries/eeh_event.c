/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 * Copyright (c) 2005 Linas Vepstas <linas@linas.org>
 */

#include <linux/delay.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/sched.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <asm/eeh_event.h>
#include <asm/ppc-pci.h>

/** Overview:
 *  EEH error states may be detected within exception handlers;
 *  however, the recovery processing needs to occur asynchronously
 *  in a normal kernel context and not an interrupt context.
 *  This pair of routines creates an event and queues it onto a
 *  work-queue, where a worker thread can drive recovery.
 */

/* EEH event workqueue setup. */
static DEFINE_SPINLOCK(eeh_eventlist_lock);
LIST_HEAD(eeh_eventlist);
static void eeh_thread_launcher(struct work_struct *);
DECLARE_WORK(eeh_event_wq, eeh_thread_launcher);

/* Serialize reset sequences for a given pci device */
DEFINE_MUTEX(eeh_event_mutex);

/**
 * eeh_event_handler - Dispatch EEH events.
 * @dummy - unused
 *
 * The detection of a frozen slot can occur inside an interrupt,
 * where it can be hard to do anything about it.  The goal of this
 * routine is to pull these detection events out of the context
 * of the interrupt handler, and re-dispatch them for processing
 * at a later time in a normal context.
 */
static int eeh_event_handler(void * dummy)
{
	unsigned long flags;
	struct eeh_event *event;
	struct eeh_pe *pe;

	set_task_comm(current, "eehd");

	spin_lock_irqsave(&eeh_eventlist_lock, flags);
	event = NULL;

	/* Unqueue the event, get ready to process. */
	if (!list_empty(&eeh_eventlist)) {
		event = list_entry(eeh_eventlist.next, struct eeh_event, list);
		list_del(&event->list);
	}
	spin_unlock_irqrestore(&eeh_eventlist_lock, flags);

	if (event == NULL)
		return 0;

	/* Serialize processing of EEH events */
	mutex_lock(&eeh_event_mutex);
	pe = event->pe;
	eeh_pe_state_mark(pe, EEH_PE_RECOVERING);
	pr_info("EEH: Detected PCI bus error on PHB#%d-PE#%x\n",
		pe->phb->global_number, pe->addr);

	set_current_state(TASK_INTERRUPTIBLE);	/* Don't add to load average */
	eeh_handle_event(pe);
	eeh_pe_state_clear(pe, EEH_PE_RECOVERING);

	kfree(event);
	mutex_unlock(&eeh_event_mutex);

	/* If there are no new errors after an hour, clear the counter. */
	if (pe && pe->freeze_count > 0) {
		msleep_interruptible(3600*1000);
		if (pe->freeze_count > 0)
			pe->freeze_count--;

	}

	return 0;
}

/**
 * eeh_thread_launcher - Start kernel thread to handle EEH events
 * @dummy - unused
 *
 * This routine is called to start the kernel thread for processing
 * EEH event.
 */
static void eeh_thread_launcher(struct work_struct *dummy)
{
	if (kernel_thread(eeh_event_handler, NULL, CLONE_KERNEL) < 0)
		printk(KERN_ERR "Failed to start EEH daemon\n");
}

/**
 * eeh_send_failure_event - Generate a PCI error event
 * @pe: EEH PE
 *
 * This routine can be called within an interrupt context;
 * the actual event will be delivered in a normal context
 * (from a workqueue).
 */
int eeh_send_failure_event(struct eeh_pe *pe)
{
	unsigned long flags;
	struct eeh_event *event;

	event = kzalloc(sizeof(*event), GFP_ATOMIC);
	if (!event) {
		pr_err("EEH: out of memory, event not handled\n");
		return -ENOMEM;
	}
	event->pe = pe;

	/* We may or may not be called in an interrupt context */
	spin_lock_irqsave(&eeh_eventlist_lock, flags);
	list_add(&event->list, &eeh_eventlist);
	spin_unlock_irqrestore(&eeh_eventlist_lock, flags);

	schedule_work(&eeh_event_wq);

	return 0;
}
