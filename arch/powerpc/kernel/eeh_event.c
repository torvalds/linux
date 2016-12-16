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
#include <linux/sched.h>
#include <linux/semaphore.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/kthread.h>
#include <asm/eeh_event.h>
#include <asm/ppc-pci.h>

/** Overview:
 *  EEH error states may be detected within exception handlers;
 *  however, the recovery processing needs to occur asynchronously
 *  in a normal kernel context and not an interrupt context.
 *  This pair of routines creates an event and queues it onto a
 *  work-queue, where a worker thread can drive recovery.
 */

static DEFINE_SPINLOCK(eeh_eventlist_lock);
static struct semaphore eeh_eventlist_sem;
static LIST_HEAD(eeh_eventlist);

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

	while (!kthread_should_stop()) {
		if (down_interruptible(&eeh_eventlist_sem))
			break;

		/* Fetch EEH event from the queue */
		spin_lock_irqsave(&eeh_eventlist_lock, flags);
		event = NULL;
		if (!list_empty(&eeh_eventlist)) {
			event = list_entry(eeh_eventlist.next,
					   struct eeh_event, list);
			list_del(&event->list);
		}
		spin_unlock_irqrestore(&eeh_eventlist_lock, flags);
		if (!event)
			continue;

		/* We might have event without binding PE */
		pe = event->pe;
		if (pe) {
			eeh_pe_state_mark(pe, EEH_PE_RECOVERING);
			if (pe->type & EEH_PE_PHB)
				pr_info("EEH: Detected error on PHB#%d\n",
					 pe->phb->global_number);
			else
				pr_info("EEH: Detected PCI bus error on "
					"PHB#%d-PE#%x\n",
					pe->phb->global_number, pe->addr);
			eeh_handle_event(pe);
			eeh_pe_state_clear(pe, EEH_PE_RECOVERING);
		} else {
			eeh_handle_event(NULL);
		}

		kfree(event);
	}

	return 0;
}

/**
 * eeh_event_init - Start kernel thread to handle EEH events
 *
 * This routine is called to start the kernel thread for processing
 * EEH event.
 */
int eeh_event_init(void)
{
	struct task_struct *t;
	int ret = 0;

	/* Initialize semaphore */
	sema_init(&eeh_eventlist_sem, 0);

	t = kthread_run(eeh_event_handler, NULL, "eehd");
	if (IS_ERR(t)) {
		ret = PTR_ERR(t);
		pr_err("%s: Failed to start EEH daemon (%d)\n",
			__func__, ret);
		return ret;
	}

	return 0;
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

	/* For EEH deamon to knick in */
	up(&eeh_eventlist_sem);

	return 0;
}

/**
 * eeh_remove_event - Remove EEH event from the queue
 * @pe: Event binding to the PE
 * @force: Event will be removed unconditionally
 *
 * On PowerNV platform, we might have subsequent coming events
 * is part of the former one. For that case, those subsequent
 * coming events are totally duplicated and unnecessary, thus
 * they should be removed.
 */
void eeh_remove_event(struct eeh_pe *pe, bool force)
{
	unsigned long flags;
	struct eeh_event *event, *tmp;

	/*
	 * If we have NULL PE passed in, we have dead IOC
	 * or we're sure we can report all existing errors
	 * by the caller.
	 *
	 * With "force", the event with associated PE that
	 * have been isolated, the event won't be removed
	 * to avoid event lost.
	 */
	spin_lock_irqsave(&eeh_eventlist_lock, flags);
	list_for_each_entry_safe(event, tmp, &eeh_eventlist, list) {
		if (!force && event->pe &&
		    (event->pe->state & EEH_PE_ISOLATED))
			continue;

		if (!pe) {
			list_del(&event->list);
			kfree(event);
		} else if (pe->type & EEH_PE_PHB) {
			if (event->pe && event->pe->phb == pe->phb) {
				list_del(&event->list);
				kfree(event);
			}
		} else if (event->pe == pe) {
			list_del(&event->list);
			kfree(event);
		}
	}
	spin_unlock_irqrestore(&eeh_eventlist_lock, flags);
}
