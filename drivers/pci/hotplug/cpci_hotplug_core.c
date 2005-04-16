/*
 * CompactPCI Hot Plug Driver
 *
 * Copyright (C) 2002 SOMA Networks, Inc.
 * Copyright (C) 2001 Greg Kroah-Hartman (greg@kroah.com)
 * Copyright (C) 2001 IBM Corp.
 *
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Send feedback to <scottm@somanetworks.com>
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/smp_lock.h>
#include <linux/delay.h>
#include "pci_hotplug.h"
#include "cpci_hotplug.h"

#define DRIVER_VERSION	"0.2"
#define DRIVER_AUTHOR	"Scott Murray <scottm@somanetworks.com>"
#define DRIVER_DESC	"CompactPCI Hot Plug Core"

#define MY_NAME	"cpci_hotplug"

#define dbg(format, arg...)					\
	do {							\
		if(cpci_debug)					\
			printk (KERN_DEBUG "%s: " format "\n",	\
				MY_NAME , ## arg); 		\
	} while(0)
#define err(format, arg...) printk(KERN_ERR "%s: " format "\n", MY_NAME , ## arg)
#define info(format, arg...) printk(KERN_INFO "%s: " format "\n", MY_NAME , ## arg)
#define warn(format, arg...) printk(KERN_WARNING "%s: " format "\n", MY_NAME , ## arg)

/* local variables */
static spinlock_t list_lock;
static LIST_HEAD(slot_list);
static int slots;
int cpci_debug;
static struct cpci_hp_controller *controller;
static struct semaphore event_semaphore;	/* mutex for process loop (up if something to process) */
static struct semaphore thread_exit;		/* guard ensure thread has exited before calling it quits */
static int thread_finished = 1;

static int enable_slot(struct hotplug_slot *slot);
static int disable_slot(struct hotplug_slot *slot);
static int set_attention_status(struct hotplug_slot *slot, u8 value);
static int get_power_status(struct hotplug_slot *slot, u8 * value);
static int get_attention_status(struct hotplug_slot *slot, u8 * value);

static struct hotplug_slot_ops cpci_hotplug_slot_ops = {
	.owner = THIS_MODULE,
	.enable_slot = enable_slot,
	.disable_slot = disable_slot,
	.set_attention_status = set_attention_status,
	.get_power_status = get_power_status,
	.get_attention_status = get_attention_status,
};

static int
update_latch_status(struct hotplug_slot *hotplug_slot, u8 value)
{
	struct hotplug_slot_info info;

	memcpy(&info, hotplug_slot->info, sizeof(struct hotplug_slot_info));
	info.latch_status = value;
	return pci_hp_change_slot_info(hotplug_slot, &info);
}

static int
update_adapter_status(struct hotplug_slot *hotplug_slot, u8 value)
{
	struct hotplug_slot_info info;

	memcpy(&info, hotplug_slot->info, sizeof(struct hotplug_slot_info));
	info.adapter_status = value;
	return pci_hp_change_slot_info(hotplug_slot, &info);
}

static int
enable_slot(struct hotplug_slot *hotplug_slot)
{
	struct slot *slot = hotplug_slot->private;
	int retval = 0;

	dbg("%s - physical_slot = %s", __FUNCTION__, hotplug_slot->name);

	if(controller->ops->set_power) {
		retval = controller->ops->set_power(slot, 1);
	}

	return retval;
}

static int
disable_slot(struct hotplug_slot *hotplug_slot)
{
	struct slot *slot = hotplug_slot->private;
	int retval = 0;

	dbg("%s - physical_slot = %s", __FUNCTION__, hotplug_slot->name);

	/* Unconfigure device */
	dbg("%s - unconfiguring slot %s",
	    __FUNCTION__, slot->hotplug_slot->name);
	if((retval = cpci_unconfigure_slot(slot))) {
		err("%s - could not unconfigure slot %s",
		    __FUNCTION__, slot->hotplug_slot->name);
		return retval;
	}
	dbg("%s - finished unconfiguring slot %s",
	    __FUNCTION__, slot->hotplug_slot->name);

	/* Clear EXT (by setting it) */
	if(cpci_clear_ext(slot)) {
		err("%s - could not clear EXT for slot %s",
		    __FUNCTION__, slot->hotplug_slot->name);
		retval = -ENODEV;
	}
	cpci_led_on(slot);

	if(controller->ops->set_power) {
		retval = controller->ops->set_power(slot, 0);
	}

	if(update_adapter_status(slot->hotplug_slot, 0)) {
		warn("failure to update adapter file");
	}

	slot->extracting = 0;

	return retval;
}

static u8
cpci_get_power_status(struct slot *slot)
{
	u8 power = 1;

	if(controller->ops->get_power) {
		power = controller->ops->get_power(slot);
	}
	return power;
}

static int
get_power_status(struct hotplug_slot *hotplug_slot, u8 * value)
{
	struct slot *slot = hotplug_slot->private;

	*value = cpci_get_power_status(slot);
	return 0;
}

static int
get_attention_status(struct hotplug_slot *hotplug_slot, u8 * value)
{
	struct slot *slot = hotplug_slot->private;

	*value = cpci_get_attention_status(slot);
	return 0;
}

static int
set_attention_status(struct hotplug_slot *hotplug_slot, u8 status)
{
	return cpci_set_attention_status(hotplug_slot->private, status);
}

static void release_slot(struct hotplug_slot *hotplug_slot)
{
	struct slot *slot = hotplug_slot->private;

	kfree(slot->hotplug_slot->info);
	kfree(slot->hotplug_slot->name);
	kfree(slot->hotplug_slot);
	kfree(slot);
}

#define SLOT_NAME_SIZE	6
static void
make_slot_name(struct slot *slot)
{
	snprintf(slot->hotplug_slot->name,
		 SLOT_NAME_SIZE, "%02x:%02x", slot->bus->number, slot->number);
}

int
cpci_hp_register_bus(struct pci_bus *bus, u8 first, u8 last)
{
	struct slot *slot;
	struct hotplug_slot *hotplug_slot;
	struct hotplug_slot_info *info;
	char *name;
	int status = -ENOMEM;
	int i;

	if(!(controller && bus)) {
		return -ENODEV;
	}

	/*
	 * Create a structure for each slot, and register that slot
	 * with the pci_hotplug subsystem.
	 */
	for (i = first; i <= last; ++i) {
		slot = kmalloc(sizeof (struct slot), GFP_KERNEL);
		if (!slot)
			goto error;
		memset(slot, 0, sizeof (struct slot));

		hotplug_slot =
		    kmalloc(sizeof (struct hotplug_slot), GFP_KERNEL);
		if (!hotplug_slot)
			goto error_slot;
		memset(hotplug_slot, 0, sizeof (struct hotplug_slot));
		slot->hotplug_slot = hotplug_slot;

		info = kmalloc(sizeof (struct hotplug_slot_info), GFP_KERNEL);
		if (!info)
			goto error_hpslot;
		memset(info, 0, sizeof (struct hotplug_slot_info));
		hotplug_slot->info = info;

		name = kmalloc(SLOT_NAME_SIZE, GFP_KERNEL);
		if (!name)
			goto error_info;
		hotplug_slot->name = name;

		slot->bus = bus;
		slot->number = i;
		slot->devfn = PCI_DEVFN(i, 0);

		hotplug_slot->private = slot;
		hotplug_slot->release = &release_slot;
		make_slot_name(slot);
		hotplug_slot->ops = &cpci_hotplug_slot_ops;

		/*
		 * Initialize the slot info structure with some known
		 * good values.
		 */
		dbg("initializing slot %s", slot->hotplug_slot->name);
		info->power_status = cpci_get_power_status(slot);
		info->attention_status = cpci_get_attention_status(slot);

		dbg("registering slot %s", slot->hotplug_slot->name);
		status = pci_hp_register(slot->hotplug_slot);
		if (status) {
			err("pci_hp_register failed with error %d", status);
			goto error_name;
		}

		/* Add slot to our internal list */
		spin_lock(&list_lock);
		list_add(&slot->slot_list, &slot_list);
		slots++;
		spin_unlock(&list_lock);
	}
	return 0;
error_name:
	kfree(name);
error_info:
	kfree(info);
error_hpslot:
	kfree(hotplug_slot);
error_slot:
	kfree(slot);
error:
	return status;
}

int
cpci_hp_unregister_bus(struct pci_bus *bus)
{
	struct slot *slot;
	struct list_head *tmp;
	struct list_head *next;
	int status;

	spin_lock(&list_lock);
	if(!slots) {
		spin_unlock(&list_lock);
		return -1;
	}
	list_for_each_safe(tmp, next, &slot_list) {
		slot = list_entry(tmp, struct slot, slot_list);
		if(slot->bus == bus) {
			dbg("deregistering slot %s", slot->hotplug_slot->name);
			status = pci_hp_deregister(slot->hotplug_slot);
			if(status) {
				err("pci_hp_deregister failed with error %d",
				    status);
				return status;
			}

			list_del(&slot->slot_list);
			slots--;
		}
	}
	spin_unlock(&list_lock);
	return 0;
}

/* This is the interrupt mode interrupt handler */
static irqreturn_t
cpci_hp_intr(int irq, void *data, struct pt_regs *regs)
{
	dbg("entered cpci_hp_intr");

	/* Check to see if it was our interrupt */
	if((controller->irq_flags & SA_SHIRQ) &&
	    !controller->ops->check_irq(controller->dev_id)) {
		dbg("exited cpci_hp_intr, not our interrupt");
		return IRQ_NONE;
	}

	/* Disable ENUM interrupt */
	controller->ops->disable_irq();

	/* Trigger processing by the event thread */
	dbg("Signal event_semaphore");
	up(&event_semaphore);
	dbg("exited cpci_hp_intr");
	return IRQ_HANDLED;
}

/*
 * According to PICMG 2.12 R2.0, section 6.3.2, upon
 * initialization, the system driver shall clear the
 * INS bits of the cold-inserted devices.
 */
static int
init_slots(void)
{
	struct slot *slot;
	struct list_head *tmp;
	struct pci_dev* dev;

	dbg("%s - enter", __FUNCTION__);
	spin_lock(&list_lock);
	if(!slots) {
		spin_unlock(&list_lock);
		return -1;
	}
	list_for_each(tmp, &slot_list) {
		slot = list_entry(tmp, struct slot, slot_list);
		dbg("%s - looking at slot %s",
		    __FUNCTION__, slot->hotplug_slot->name);
		if(cpci_check_and_clear_ins(slot)) {
			dbg("%s - cleared INS for slot %s",
			    __FUNCTION__, slot->hotplug_slot->name);
			dev = pci_find_slot(slot->bus->number, PCI_DEVFN(slot->number, 0));
			if(dev) {
				if(update_adapter_status(slot->hotplug_slot, 1)) {
					warn("failure to update adapter file");
				}
				if(update_latch_status(slot->hotplug_slot, 1)) {
					warn("failure to update latch file");
				}
				slot->dev = dev;
			} else {
				err("%s - no driver attached to device in slot %s",
				    __FUNCTION__, slot->hotplug_slot->name);
			}
		}
	}
	spin_unlock(&list_lock);
	dbg("%s - exit", __FUNCTION__);
	return 0;
}

static int
check_slots(void)
{
	struct slot *slot;
	struct list_head *tmp;
	int extracted;
	int inserted;

	spin_lock(&list_lock);
	if(!slots) {
		spin_unlock(&list_lock);
		err("no slots registered, shutting down");
		return -1;
	}
	extracted = inserted = 0;
	list_for_each(tmp, &slot_list) {
		slot = list_entry(tmp, struct slot, slot_list);
		dbg("%s - looking at slot %s",
		    __FUNCTION__, slot->hotplug_slot->name);
		if(cpci_check_and_clear_ins(slot)) {
			u16 hs_csr;

			/* Some broken hardware (e.g. PLX 9054AB) asserts ENUM# twice... */
			if(slot->dev) {
				warn("slot %s already inserted", slot->hotplug_slot->name);
				inserted++;
				continue;
			}

			/* Process insertion */
			dbg("%s - slot %s inserted",
			    __FUNCTION__, slot->hotplug_slot->name);

			/* GSM, debug */
			hs_csr = cpci_get_hs_csr(slot);
			dbg("%s - slot %s HS_CSR (1) = %04x",
			    __FUNCTION__, slot->hotplug_slot->name, hs_csr);

			/* Configure device */
			dbg("%s - configuring slot %s",
			    __FUNCTION__, slot->hotplug_slot->name);
			if(cpci_configure_slot(slot)) {
				err("%s - could not configure slot %s",
				    __FUNCTION__, slot->hotplug_slot->name);
				continue;
			}
			dbg("%s - finished configuring slot %s",
			    __FUNCTION__, slot->hotplug_slot->name);

			/* GSM, debug */
			hs_csr = cpci_get_hs_csr(slot);
			dbg("%s - slot %s HS_CSR (2) = %04x",
			    __FUNCTION__, slot->hotplug_slot->name, hs_csr);

			if(update_latch_status(slot->hotplug_slot, 1)) {
				warn("failure to update latch file");
			}

			if(update_adapter_status(slot->hotplug_slot, 1)) {
				warn("failure to update adapter file");
			}

			cpci_led_off(slot);

			/* GSM, debug */
			hs_csr = cpci_get_hs_csr(slot);
			dbg("%s - slot %s HS_CSR (3) = %04x",
			    __FUNCTION__, slot->hotplug_slot->name, hs_csr);

			inserted++;
		} else if(cpci_check_ext(slot)) {
			u16 hs_csr;

			/* Process extraction request */
			dbg("%s - slot %s extracted",
			    __FUNCTION__, slot->hotplug_slot->name);

			/* GSM, debug */
			hs_csr = cpci_get_hs_csr(slot);
			dbg("%s - slot %s HS_CSR = %04x",
			    __FUNCTION__, slot->hotplug_slot->name, hs_csr);

			if(!slot->extracting) {
				if(update_latch_status(slot->hotplug_slot, 0)) {
					warn("failure to update latch file");
				}
				slot->extracting = 1;
			}
			extracted++;
		}
	}
	spin_unlock(&list_lock);
	if(inserted || extracted) {
		return extracted;
	}
	else {
		err("cannot find ENUM# source, shutting down");
		return -1;
	}
}

/* This is the interrupt mode worker thread body */
static int
event_thread(void *data)
{
	int rc;
	struct slot *slot;
	struct list_head *tmp;

	lock_kernel();
	daemonize("cpci_hp_eventd");
	unlock_kernel();

	dbg("%s - event thread started", __FUNCTION__);
	while(1) {
		dbg("event thread sleeping");
		down_interruptible(&event_semaphore);
		dbg("event thread woken, thread_finished = %d",
		    thread_finished);
		if(thread_finished || signal_pending(current))
			break;
		while(controller->ops->query_enum()) {
			rc = check_slots();
			if (rc > 0)
				/* Give userspace a chance to handle extraction */
				msleep(500);
			else if (rc < 0) {
				dbg("%s - error checking slots", __FUNCTION__);
				thread_finished = 1;
				break;
			}
		}
		/* Check for someone yanking out a board */
		list_for_each(tmp, &slot_list) {
			slot = list_entry(tmp, struct slot, slot_list);
			if(slot->extracting) {
				/*
				 * Hmmm, we're likely hosed at this point, should we
				 * bother trying to tell the driver or not?
				 */
				err("card in slot %s was improperly removed",
				    slot->hotplug_slot->name);
				if(update_adapter_status(slot->hotplug_slot, 0)) {
					warn("failure to update adapter file");
				}
				slot->extracting = 0;
			}
		}

		/* Re-enable ENUM# interrupt */
		dbg("%s - re-enabling irq", __FUNCTION__);
		controller->ops->enable_irq();
	}

	dbg("%s - event thread signals exit", __FUNCTION__);
	up(&thread_exit);
	return 0;
}

/* This is the polling mode worker thread body */
static int
poll_thread(void *data)
{
	int rc;
	struct slot *slot;
	struct list_head *tmp;

	lock_kernel();
	daemonize("cpci_hp_polld");
	unlock_kernel();

	while(1) {
		if(thread_finished || signal_pending(current))
			break;

		while(controller->ops->query_enum()) {
			rc = check_slots();
			if(rc > 0)
				/* Give userspace a chance to handle extraction */
				msleep(500);
			else if (rc < 0) {
				dbg("%s - error checking slots", __FUNCTION__);
				thread_finished = 1;
				break;
			}
		}
		/* Check for someone yanking out a board */
		list_for_each(tmp, &slot_list) {
			slot = list_entry(tmp, struct slot, slot_list);
			if(slot->extracting) {
				/*
				 * Hmmm, we're likely hosed at this point, should we
				 * bother trying to tell the driver or not?
				 */
				err("card in slot %s was improperly removed",
				    slot->hotplug_slot->name);
				if(update_adapter_status(slot->hotplug_slot, 0)) {
					warn("failure to update adapter file");
				}
				slot->extracting = 0;
			}
		}

		msleep(100);
	}
	dbg("poll thread signals exit");
	up(&thread_exit);
	return 0;
}

static int
cpci_start_thread(void)
{
	int pid;

	/* initialize our semaphores */
	init_MUTEX_LOCKED(&event_semaphore);
	init_MUTEX_LOCKED(&thread_exit);
	thread_finished = 0;

	if(controller->irq) {
		pid = kernel_thread(event_thread, NULL, 0);
	} else {
		pid = kernel_thread(poll_thread, NULL, 0);
	}
	if(pid < 0) {
		err("Can't start up our thread");
		return -1;
	}
	dbg("Our thread pid = %d", pid);
	return 0;
}

static void
cpci_stop_thread(void)
{
	thread_finished = 1;
	dbg("thread finish command given");
	if(controller->irq) {
		up(&event_semaphore);
	}
	dbg("wait for thread to exit");
	down(&thread_exit);
}

int
cpci_hp_register_controller(struct cpci_hp_controller *new_controller)
{
	int status = 0;

	if(!controller) {
		controller = new_controller;
		if(controller->irq) {
			if(request_irq(controller->irq,
					cpci_hp_intr,
					controller->irq_flags,
					MY_NAME, controller->dev_id)) {
				err("Can't get irq %d for the hotplug cPCI controller", controller->irq);
				status = -ENODEV;
			}
			dbg("%s - acquired controller irq %d", __FUNCTION__,
			    controller->irq);
		}
	} else {
		err("cPCI hotplug controller already registered");
		status = -1;
	}
	return status;
}

int
cpci_hp_unregister_controller(struct cpci_hp_controller *old_controller)
{
	int status = 0;

	if(controller) {
		if(!thread_finished) {
			cpci_stop_thread();
		}
		if(controller->irq) {
			free_irq(controller->irq, controller->dev_id);
		}
		controller = NULL;
	} else {
		status = -ENODEV;
	}
	return status;
}

int
cpci_hp_start(void)
{
	static int first = 1;
	int status;

	dbg("%s - enter", __FUNCTION__);
	if(!controller) {
		return -ENODEV;
	}

	spin_lock(&list_lock);
	if(!slots) {
		spin_unlock(&list_lock);
		return -ENODEV;
	}
	spin_unlock(&list_lock);

	if(first) {
		status = init_slots();
		if(status) {
			return status;
		}
		first = 0;
	}

	status = cpci_start_thread();
	if(status) {
		return status;
	}
	dbg("%s - thread started", __FUNCTION__);

	if(controller->irq) {
		/* Start enum interrupt processing */
		dbg("%s - enabling irq", __FUNCTION__);
		controller->ops->enable_irq();
	}
	dbg("%s - exit", __FUNCTION__);
	return 0;
}

int
cpci_hp_stop(void)
{
	if(!controller) {
		return -ENODEV;
	}

	if(controller->irq) {
		/* Stop enum interrupt processing */
		dbg("%s - disabling irq", __FUNCTION__);
		controller->ops->disable_irq();
	}
	cpci_stop_thread();
	return 0;
}

static void __exit
cleanup_slots(void)
{
	struct list_head *tmp;
	struct slot *slot;

	/*
	 * Unregister all of our slots with the pci_hotplug subsystem,
	 * and free up all memory that we had allocated.
	 */
	spin_lock(&list_lock);
	if(!slots) {
		goto null_cleanup;
	}
	list_for_each(tmp, &slot_list) {
		slot = list_entry(tmp, struct slot, slot_list);
		list_del(&slot->slot_list);
		pci_hp_deregister(slot->hotplug_slot);
		kfree(slot->hotplug_slot->info);
		kfree(slot->hotplug_slot->name);
		kfree(slot->hotplug_slot);
		kfree(slot);
	}
      null_cleanup:
	spin_unlock(&list_lock);
	return;
}

int __init
cpci_hotplug_init(int debug)
{
	spin_lock_init(&list_lock);
	cpci_debug = debug;

	info(DRIVER_DESC " version: " DRIVER_VERSION);
	return 0;
}

void __exit
cpci_hotplug_exit(void)
{
	/*
	 * Clean everything up.
	 */
	cleanup_slots();
}

EXPORT_SYMBOL_GPL(cpci_hp_register_controller);
EXPORT_SYMBOL_GPL(cpci_hp_unregister_controller);
EXPORT_SYMBOL_GPL(cpci_hp_register_bus);
EXPORT_SYMBOL_GPL(cpci_hp_unregister_bus);
EXPORT_SYMBOL_GPL(cpci_hp_start);
EXPORT_SYMBOL_GPL(cpci_hp_stop);
