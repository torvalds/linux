/*
 * eeh_event.c
 *
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

#include <linux/list.h>
#include <linux/pci.h>
#include <asm/eeh_event.h>

/** Overview:
 *  EEH error states may be detected within exception handlers;
 *  however, the recovery processing needs to occur asynchronously
 *  in a normal kernel context and not an interrupt context.
 *  This pair of routines creates an event and queues it onto a
 *  work-queue, where a worker thread can drive recovery.
 */

/* EEH event workqueue setup. */
static spinlock_t eeh_eventlist_lock = SPIN_LOCK_UNLOCKED;
LIST_HEAD(eeh_eventlist);
static void eeh_thread_launcher(void *);
DECLARE_WORK(eeh_event_wq, eeh_thread_launcher, NULL);

/**
 * eeh_panic - call panic() for an eeh event that cannot be handled.
 * The philosophy of this routine is that it is better to panic and
 * halt the OS than it is to risk possible data corruption by
 * oblivious device drivers that don't know better.
 *
 * @dev pci device that had an eeh event
 * @reset_state current reset state of the device slot
 */
static void eeh_panic(struct pci_dev *dev, int reset_state)
{
	/*
	 * Since the panic_on_oops sysctl is used to halt the system
	 * in light of potential corruption, we can use it here.
	 */
	if (panic_on_oops) {
		panic("EEH: MMIO failure (%d) on device:%s\n", reset_state,
		      pci_name(dev));
	}
	else {
		printk(KERN_INFO "EEH: Ignored MMIO failure (%d) on device:%s\n",
		       reset_state, pci_name(dev));
	}
}

/**
 * eeh_event_handler - dispatch EEH events.  The detection of a frozen
 * slot can occur inside an interrupt, where it can be hard to do
 * anything about it.  The goal of this routine is to pull these
 * detection events out of the context of the interrupt handler, and
 * re-dispatch them for processing at a later time in a normal context.
 *
 * @dummy - unused
 */
static int eeh_event_handler(void * dummy)
{
	unsigned long flags;
	struct eeh_event	*event;

	daemonize ("eehd");

	while (1) {
		set_current_state(TASK_INTERRUPTIBLE);

		spin_lock_irqsave(&eeh_eventlist_lock, flags);
		event = NULL;
		if (!list_empty(&eeh_eventlist)) {
			event = list_entry(eeh_eventlist.next, struct eeh_event, list);
			list_del(&event->list);
		}
		spin_unlock_irqrestore(&eeh_eventlist_lock, flags);
		if (event == NULL)
			break;

		printk(KERN_INFO "EEH: Detected PCI bus error on device %s\n",
		       pci_name(event->dev));

		eeh_panic (event->dev, event->state);

		kfree(event);
	}

	return 0;
}

/**
 * eeh_thread_launcher
 *
 * @dummy - unused
 */
static void eeh_thread_launcher(void *dummy)
{
	if (kernel_thread(eeh_event_handler, NULL, CLONE_KERNEL) < 0)
		printk(KERN_ERR "Failed to start EEH daemon\n");
}

/**
 * eeh_send_failure_event - generate a PCI error event
 * @dev pci device
 *
 * This routine can be called within an interrupt context;
 * the actual event will be delivered in a normal context
 * (from a workqueue).
 */
int eeh_send_failure_event (struct device_node *dn,
                            struct pci_dev *dev,
                            int state,
                            int time_unavail)
{
	unsigned long flags;
	struct eeh_event *event;

	event = kmalloc(sizeof(*event), GFP_ATOMIC);
	if (event == NULL) {
		printk (KERN_ERR "EEH: out of memory, event not handled\n");
		return 1;
 	}

	if (dev)
		pci_dev_get(dev);

	event->dn = dn;
	event->dev = dev;
	event->state = state;
	event->time_unavail = time_unavail;

	/* We may or may not be called in an interrupt context */
	spin_lock_irqsave(&eeh_eventlist_lock, flags);
	list_add(&event->list, &eeh_eventlist);
	spin_unlock_irqrestore(&eeh_eventlist_lock, flags);

	schedule_work(&eeh_event_wq);

	return 0;
}

/********************** END OF FILE ******************************/
