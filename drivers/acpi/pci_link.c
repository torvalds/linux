/*
 *  pci_link.c - ACPI PCI Interrupt Link Device Driver ($Revision: 34 $)
 *
 *  Copyright (C) 2001, 2002 Andy Grover <andrew.grover@intel.com>
 *  Copyright (C) 2001, 2002 Paul Diefenbaugh <paul.s.diefenbaugh@intel.com>
 *  Copyright (C) 2002       Dominik Brodowski <devel@brodo.de>
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or (at
 *  your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * TBD: 
 *      1. Support more than one IRQ resource entry per link device (index).
 *	2. Implement start/stop mechanism and use ACPI Bus Driver facilities
 *	   for IRQ management (e.g. start()->_SRS).
 */

#include <linux/syscore_ops.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/pm.h>
#include <linux/pci.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/acpi.h>
#include <linux/irq.h>

#include "internal.h"

#define _COMPONENT			ACPI_PCI_COMPONENT
ACPI_MODULE_NAME("pci_link");
#define ACPI_PCI_LINK_CLASS		"pci_irq_routing"
#define ACPI_PCI_LINK_DEVICE_NAME	"PCI Interrupt Link"
#define ACPI_PCI_LINK_FILE_INFO		"info"
#define ACPI_PCI_LINK_FILE_STATUS	"state"
#define ACPI_PCI_LINK_MAX_POSSIBLE	16

static int acpi_pci_link_add(struct acpi_device *device,
			     const struct acpi_device_id *not_used);
static void acpi_pci_link_remove(struct acpi_device *device);

static const struct acpi_device_id link_device_ids[] = {
	{"PNP0C0F", 0},
	{"", 0},
};

static struct acpi_scan_handler pci_link_handler = {
	.ids = link_device_ids,
	.attach = acpi_pci_link_add,
	.detach = acpi_pci_link_remove,
};

/*
 * If a link is initialized, we never change its active and initialized
 * later even the link is disable. Instead, we just repick the active irq
 */
struct acpi_pci_link_irq {
	u32 active;		/* Current IRQ */
	u8 triggering;		/* All IRQs */
	u8 polarity;		/* All IRQs */
	u8 resource_type;
	u8 possible_count;
	u32 possible[ACPI_PCI_LINK_MAX_POSSIBLE];
	u8 initialized:1;
	u8 reserved:7;
};

struct acpi_pci_link {
	struct list_head		list;
	struct acpi_device		*device;
	struct acpi_pci_link_irq	irq;
	int				refcnt;
};

static LIST_HEAD(acpi_link_list);
static DEFINE_MUTEX(acpi_link_lock);

/* --------------------------------------------------------------------------
                            PCI Link Device Management
   -------------------------------------------------------------------------- */

/*
 * set context (link) possible list from resource list
 */
static acpi_status acpi_pci_link_check_possible(struct acpi_resource *resource,
						void *context)
{
	struct acpi_pci_link *link = context;
	u32 i;

	switch (resource->type) {
	case ACPI_RESOURCE_TYPE_START_DEPENDENT:
	case ACPI_RESOURCE_TYPE_END_TAG:
		return AE_OK;
	case ACPI_RESOURCE_TYPE_IRQ:
		{
			struct acpi_resource_irq *p = &resource->data.irq;
			if (!p || !p->interrupt_count) {
				ACPI_DEBUG_PRINT((ACPI_DB_INFO,
						  "Blank _PRS IRQ resource\n"));
				return AE_OK;
			}
			for (i = 0;
			     (i < p->interrupt_count
			      && i < ACPI_PCI_LINK_MAX_POSSIBLE); i++) {
				if (!p->interrupts[i]) {
					printk(KERN_WARNING PREFIX
					       "Invalid _PRS IRQ %d\n",
					       p->interrupts[i]);
					continue;
				}
				link->irq.possible[i] = p->interrupts[i];
				link->irq.possible_count++;
			}
			link->irq.triggering = p->triggering;
			link->irq.polarity = p->polarity;
			link->irq.resource_type = ACPI_RESOURCE_TYPE_IRQ;
			break;
		}
	case ACPI_RESOURCE_TYPE_EXTENDED_IRQ:
		{
			struct acpi_resource_extended_irq *p =
			    &resource->data.extended_irq;
			if (!p || !p->interrupt_count) {
				printk(KERN_WARNING PREFIX
					      "Blank _PRS EXT IRQ resource\n");
				return AE_OK;
			}
			for (i = 0;
			     (i < p->interrupt_count
			      && i < ACPI_PCI_LINK_MAX_POSSIBLE); i++) {
				if (!p->interrupts[i]) {
					printk(KERN_WARNING PREFIX
					       "Invalid _PRS IRQ %d\n",
					       p->interrupts[i]);
					continue;
				}
				link->irq.possible[i] = p->interrupts[i];
				link->irq.possible_count++;
			}
			link->irq.triggering = p->triggering;
			link->irq.polarity = p->polarity;
			link->irq.resource_type = ACPI_RESOURCE_TYPE_EXTENDED_IRQ;
			break;
		}
	default:
		printk(KERN_ERR PREFIX "_PRS resource type 0x%x isn't an IRQ\n",
		       resource->type);
		return AE_OK;
	}

	return AE_CTRL_TERMINATE;
}

static int acpi_pci_link_get_possible(struct acpi_pci_link *link)
{
	acpi_status status;

	status = acpi_walk_resources(link->device->handle, METHOD_NAME__PRS,
				     acpi_pci_link_check_possible, link);
	if (ACPI_FAILURE(status)) {
		ACPI_EXCEPTION((AE_INFO, status, "Evaluating _PRS"));
		return -ENODEV;
	}

	ACPI_DEBUG_PRINT((ACPI_DB_INFO,
			  "Found %d possible IRQs\n",
			  link->irq.possible_count));

	return 0;
}

static acpi_status acpi_pci_link_check_current(struct acpi_resource *resource,
					       void *context)
{
	int *irq = context;

	switch (resource->type) {
	case ACPI_RESOURCE_TYPE_START_DEPENDENT:
	case ACPI_RESOURCE_TYPE_END_TAG:
		return AE_OK;
	case ACPI_RESOURCE_TYPE_IRQ:
		{
			struct acpi_resource_irq *p = &resource->data.irq;
			if (!p || !p->interrupt_count) {
				/*
				 * IRQ descriptors may have no IRQ# bits set,
				 * particularly those those w/ _STA disabled
				 */
				ACPI_DEBUG_PRINT((ACPI_DB_INFO,
						  "Blank _CRS IRQ resource\n"));
				return AE_OK;
			}
			*irq = p->interrupts[0];
			break;
		}
	case ACPI_RESOURCE_TYPE_EXTENDED_IRQ:
		{
			struct acpi_resource_extended_irq *p =
			    &resource->data.extended_irq;
			if (!p || !p->interrupt_count) {
				/*
				 * extended IRQ descriptors must
				 * return at least 1 IRQ
				 */
				printk(KERN_WARNING PREFIX
					      "Blank _CRS EXT IRQ resource\n");
				return AE_OK;
			}
			*irq = p->interrupts[0];
			break;
		}
		break;
	default:
		printk(KERN_ERR PREFIX "_CRS resource type 0x%x isn't an IRQ\n",
		       resource->type);
		return AE_OK;
	}

	return AE_CTRL_TERMINATE;
}

/*
 * Run _CRS and set link->irq.active
 *
 * return value:
 * 0 - success
 * !0 - failure
 */
static int acpi_pci_link_get_current(struct acpi_pci_link *link)
{
	int result = 0;
	acpi_status status;
	int irq = 0;

	link->irq.active = 0;

	/* in practice, status disabled is meaningless, ignore it */
	if (acpi_strict) {
		/* Query _STA, set link->device->status */
		result = acpi_bus_get_status(link->device);
		if (result) {
			printk(KERN_ERR PREFIX "Unable to read status\n");
			goto end;
		}

		if (!link->device->status.enabled) {
			ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Link disabled\n"));
			return 0;
		}
	}

	/* 
	 * Query and parse _CRS to get the current IRQ assignment. 
	 */

	status = acpi_walk_resources(link->device->handle, METHOD_NAME__CRS,
				     acpi_pci_link_check_current, &irq);
	if (ACPI_FAILURE(status)) {
		ACPI_EXCEPTION((AE_INFO, status, "Evaluating _CRS"));
		result = -ENODEV;
		goto end;
	}

	if (acpi_strict && !irq) {
		printk(KERN_ERR PREFIX "_CRS returned 0\n");
		result = -ENODEV;
	}

	link->irq.active = irq;

	ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Link at IRQ %d \n", link->irq.active));

      end:
	return result;
}

static int acpi_pci_link_set(struct acpi_pci_link *link, int irq)
{
	int result;
	acpi_status status;
	struct {
		struct acpi_resource res;
		struct acpi_resource end;
	} *resource;
	struct acpi_buffer buffer = { 0, NULL };

	if (!irq)
		return -EINVAL;

	resource = kzalloc(sizeof(*resource) + 1, irqs_disabled() ? GFP_ATOMIC: GFP_KERNEL);
	if (!resource)
		return -ENOMEM;

	buffer.length = sizeof(*resource) + 1;
	buffer.pointer = resource;

	switch (link->irq.resource_type) {
	case ACPI_RESOURCE_TYPE_IRQ:
		resource->res.type = ACPI_RESOURCE_TYPE_IRQ;
		resource->res.length = sizeof(struct acpi_resource);
		resource->res.data.irq.triggering = link->irq.triggering;
		resource->res.data.irq.polarity =
		    link->irq.polarity;
		if (link->irq.triggering == ACPI_EDGE_SENSITIVE)
			resource->res.data.irq.sharable =
			    ACPI_EXCLUSIVE;
		else
			resource->res.data.irq.sharable = ACPI_SHARED;
		resource->res.data.irq.interrupt_count = 1;
		resource->res.data.irq.interrupts[0] = irq;
		break;

	case ACPI_RESOURCE_TYPE_EXTENDED_IRQ:
		resource->res.type = ACPI_RESOURCE_TYPE_EXTENDED_IRQ;
		resource->res.length = sizeof(struct acpi_resource);
		resource->res.data.extended_irq.producer_consumer =
		    ACPI_CONSUMER;
		resource->res.data.extended_irq.triggering =
		    link->irq.triggering;
		resource->res.data.extended_irq.polarity =
		    link->irq.polarity;
		if (link->irq.triggering == ACPI_EDGE_SENSITIVE)
			resource->res.data.irq.sharable =
			    ACPI_EXCLUSIVE;
		else
			resource->res.data.irq.sharable = ACPI_SHARED;
		resource->res.data.extended_irq.interrupt_count = 1;
		resource->res.data.extended_irq.interrupts[0] = irq;
		/* ignore resource_source, it's optional */
		break;
	default:
		printk(KERN_ERR PREFIX "Invalid Resource_type %d\n", link->irq.resource_type);
		result = -EINVAL;
		goto end;

	}
	resource->end.type = ACPI_RESOURCE_TYPE_END_TAG;
	resource->end.length = sizeof(struct acpi_resource);

	/* Attempt to set the resource */
	status = acpi_set_current_resources(link->device->handle, &buffer);

	/* check for total failure */
	if (ACPI_FAILURE(status)) {
		ACPI_EXCEPTION((AE_INFO, status, "Evaluating _SRS"));
		result = -ENODEV;
		goto end;
	}

	/* Query _STA, set device->status */
	result = acpi_bus_get_status(link->device);
	if (result) {
		printk(KERN_ERR PREFIX "Unable to read status\n");
		goto end;
	}
	if (!link->device->status.enabled) {
		printk(KERN_WARNING PREFIX
			      "%s [%s] disabled and referenced, BIOS bug\n",
			      acpi_device_name(link->device),
			      acpi_device_bid(link->device));
	}

	/* Query _CRS, set link->irq.active */
	result = acpi_pci_link_get_current(link);
	if (result) {
		goto end;
	}

	/*
	 * Is current setting not what we set?
	 * set link->irq.active
	 */
	if (link->irq.active != irq) {
		/*
		 * policy: when _CRS doesn't return what we just _SRS
		 * assume _SRS worked and override _CRS value.
		 */
		printk(KERN_WARNING PREFIX
			      "%s [%s] BIOS reported IRQ %d, using IRQ %d\n",
			      acpi_device_name(link->device),
			      acpi_device_bid(link->device), link->irq.active, irq);
		link->irq.active = irq;
	}

	ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Set IRQ %d\n", link->irq.active));

      end:
	kfree(resource);
	return result;
}

/* --------------------------------------------------------------------------
                            PCI Link IRQ Management
   -------------------------------------------------------------------------- */

/*
 * "acpi_irq_balance" (default in APIC mode) enables ACPI to use PIC Interrupt
 * Link Devices to move the PIRQs around to minimize sharing.
 * 
 * "acpi_irq_nobalance" (default in PIC mode) tells ACPI not to move any PIC IRQs
 * that the BIOS has already set to active.  This is necessary because
 * ACPI has no automatic means of knowing what ISA IRQs are used.  Note that
 * if the BIOS doesn't set a Link Device active, ACPI needs to program it
 * even if acpi_irq_nobalance is set.
 *
 * A tables of penalties avoids directing PCI interrupts to well known
 * ISA IRQs. Boot params are available to over-ride the default table:
 *
 * List interrupts that are free for PCI use.
 * acpi_irq_pci=n[,m]
 *
 * List interrupts that should not be used for PCI:
 * acpi_irq_isa=n[,m]
 *
 * Note that PCI IRQ routers have a list of possible IRQs,
 * which may not include the IRQs this table says are available.
 * 
 * Since this heuristic can't tell the difference between a link
 * that no device will attach to, vs. a link which may be shared
 * by multiple active devices -- it is not optimal.
 *
 * If interrupt performance is that important, get an IO-APIC system
 * with a pin dedicated to each device.  Or for that matter, an MSI
 * enabled system.
 */

#define ACPI_MAX_ISA_IRQS	16

#define PIRQ_PENALTY_PCI_POSSIBLE	(16*16)
#define PIRQ_PENALTY_PCI_USING		(16*16*16)
#define PIRQ_PENALTY_ISA_TYPICAL	(16*16*16*16)
#define PIRQ_PENALTY_ISA_USED		(16*16*16*16*16)
#define PIRQ_PENALTY_ISA_ALWAYS		(16*16*16*16*16*16)

static int acpi_isa_irq_penalty[ACPI_MAX_ISA_IRQS] = {
	PIRQ_PENALTY_ISA_ALWAYS,	/* IRQ0 timer */
	PIRQ_PENALTY_ISA_ALWAYS,	/* IRQ1 keyboard */
	PIRQ_PENALTY_ISA_ALWAYS,	/* IRQ2 cascade */
	PIRQ_PENALTY_ISA_TYPICAL,	/* IRQ3 serial */
	PIRQ_PENALTY_ISA_TYPICAL,	/* IRQ4 serial */
	PIRQ_PENALTY_ISA_TYPICAL,	/* IRQ5 sometimes SoundBlaster */
	PIRQ_PENALTY_ISA_TYPICAL,	/* IRQ6 */
	PIRQ_PENALTY_ISA_TYPICAL,	/* IRQ7 parallel, spurious */
	PIRQ_PENALTY_ISA_TYPICAL,	/* IRQ8 rtc, sometimes */
	0,				/* IRQ9  PCI, often acpi */
	0,				/* IRQ10 PCI */
	0,				/* IRQ11 PCI */
	PIRQ_PENALTY_ISA_USED,		/* IRQ12 mouse */
	PIRQ_PENALTY_ISA_USED,		/* IRQ13 fpe, sometimes */
	PIRQ_PENALTY_ISA_USED,		/* IRQ14 ide0 */
	PIRQ_PENALTY_ISA_USED,		/* IRQ15 ide1 */
	/* >IRQ15 */
};

static int acpi_irq_pci_sharing_penalty(int irq)
{
	struct acpi_pci_link *link;
	int penalty = 0;
	int i;

	list_for_each_entry(link, &acpi_link_list, list) {
		/*
		 * If a link is active, penalize its IRQ heavily
		 * so we try to choose a different IRQ.
		 */
		if (link->irq.active && link->irq.active == irq)
			penalty += PIRQ_PENALTY_PCI_USING;

		/*
		 * penalize the IRQs PCI might use, but not as severely.
		 */
		for (i = 0; i < link->irq.possible_count; i++)
			if (link->irq.possible[i] == irq)
				penalty += PIRQ_PENALTY_PCI_POSSIBLE /
					link->irq.possible_count;
	}

	return penalty;
}

static int acpi_irq_get_penalty(int irq)
{
	int penalty = 0;

	if (irq < ACPI_MAX_ISA_IRQS)
		penalty += acpi_isa_irq_penalty[irq];

	/*
	* Penalize IRQ used by ACPI SCI. If ACPI SCI pin attributes conflict
	* with PCI IRQ attributes, mark ACPI SCI as ISA_ALWAYS so it won't be
	* use for PCI IRQs.
	*/
	if (irq == acpi_gbl_FADT.sci_interrupt) {
		u32 type = irq_get_trigger_type(irq) & IRQ_TYPE_SENSE_MASK;

		if (type != IRQ_TYPE_LEVEL_LOW)
			penalty += PIRQ_PENALTY_ISA_ALWAYS;
		else
			penalty += PIRQ_PENALTY_PCI_USING;
	}

	penalty += acpi_irq_pci_sharing_penalty(irq);
	return penalty;
}

int __init acpi_irq_penalty_init(void)
{
	struct acpi_pci_link *link;
	int i;

	/*
	 * Update penalties to facilitate IRQ balancing.
	 */
	list_for_each_entry(link, &acpi_link_list, list) {

		/*
		 * reflect the possible and active irqs in the penalty table --
		 * useful for breaking ties.
		 */
		if (link->irq.possible_count) {
			int penalty =
			    PIRQ_PENALTY_PCI_POSSIBLE /
			    link->irq.possible_count;

			for (i = 0; i < link->irq.possible_count; i++) {
				if (link->irq.possible[i] < ACPI_MAX_ISA_IRQS)
					acpi_isa_irq_penalty[link->irq.
							 possible[i]] +=
					    penalty;
			}

		} else if (link->irq.active &&
				(link->irq.active < ACPI_MAX_ISA_IRQS)) {
			acpi_isa_irq_penalty[link->irq.active] +=
			    PIRQ_PENALTY_PCI_POSSIBLE;
		}
	}

	return 0;
}

static int acpi_irq_balance = -1;	/* 0: static, 1: balance */

static int acpi_pci_link_allocate(struct acpi_pci_link *link)
{
	int irq;
	int i;

	if (link->irq.initialized) {
		if (link->refcnt == 0)
			/* This means the link is disabled but initialized */
			acpi_pci_link_set(link, link->irq.active);
		return 0;
	}

	/*
	 * search for active IRQ in list of possible IRQs.
	 */
	for (i = 0; i < link->irq.possible_count; ++i) {
		if (link->irq.active == link->irq.possible[i])
			break;
	}
	/*
	 * forget active IRQ that is not in possible list
	 */
	if (i == link->irq.possible_count) {
		if (acpi_strict)
			printk(KERN_WARNING PREFIX "_CRS %d not found"
				      " in _PRS\n", link->irq.active);
		link->irq.active = 0;
	}

	/*
	 * if active found, use it; else pick entry from end of possible list.
	 */
	if (link->irq.active)
		irq = link->irq.active;
	else
		irq = link->irq.possible[link->irq.possible_count - 1];

	if (acpi_irq_balance || !link->irq.active) {
		/*
		 * Select the best IRQ.  This is done in reverse to promote
		 * the use of IRQs 9, 10, 11, and >15.
		 */
		for (i = (link->irq.possible_count - 1); i >= 0; i--) {
			if (acpi_irq_get_penalty(irq) >
			    acpi_irq_get_penalty(link->irq.possible[i]))
				irq = link->irq.possible[i];
		}
	}
	if (acpi_irq_get_penalty(irq) >= PIRQ_PENALTY_ISA_ALWAYS) {
		printk(KERN_ERR PREFIX "No IRQ available for %s [%s]. "
			    "Try pci=noacpi or acpi=off\n",
			    acpi_device_name(link->device),
			    acpi_device_bid(link->device));
		return -ENODEV;
	}

	/* Attempt to enable the link device at this IRQ. */
	if (acpi_pci_link_set(link, irq)) {
		printk(KERN_ERR PREFIX "Unable to set IRQ for %s [%s]. "
			    "Try pci=noacpi or acpi=off\n",
			    acpi_device_name(link->device),
			    acpi_device_bid(link->device));
		return -ENODEV;
	} else {
		printk(KERN_WARNING PREFIX "%s [%s] enabled at IRQ %d\n",
		       acpi_device_name(link->device),
		       acpi_device_bid(link->device), link->irq.active);
	}

	link->irq.initialized = 1;
	return 0;
}

/*
 * acpi_pci_link_allocate_irq
 * success: return IRQ >= 0
 * failure: return -1
 */
int acpi_pci_link_allocate_irq(acpi_handle handle, int index, int *triggering,
			       int *polarity, char **name)
{
	int result;
	struct acpi_device *device;
	struct acpi_pci_link *link;

	result = acpi_bus_get_device(handle, &device);
	if (result) {
		printk(KERN_ERR PREFIX "Invalid link device\n");
		return -1;
	}

	link = acpi_driver_data(device);
	if (!link) {
		printk(KERN_ERR PREFIX "Invalid link context\n");
		return -1;
	}

	/* TBD: Support multiple index (IRQ) entries per Link Device */
	if (index) {
		printk(KERN_ERR PREFIX "Invalid index %d\n", index);
		return -1;
	}

	mutex_lock(&acpi_link_lock);
	if (acpi_pci_link_allocate(link)) {
		mutex_unlock(&acpi_link_lock);
		return -1;
	}

	if (!link->irq.active) {
		mutex_unlock(&acpi_link_lock);
		printk(KERN_ERR PREFIX "Link active IRQ is 0!\n");
		return -1;
	}
	link->refcnt++;
	mutex_unlock(&acpi_link_lock);

	if (triggering)
		*triggering = link->irq.triggering;
	if (polarity)
		*polarity = link->irq.polarity;
	if (name)
		*name = acpi_device_bid(link->device);
	ACPI_DEBUG_PRINT((ACPI_DB_INFO,
			  "Link %s is referenced\n",
			  acpi_device_bid(link->device)));
	return (link->irq.active);
}

/*
 * We don't change link's irq information here.  After it is reenabled, we
 * continue use the info
 */
int acpi_pci_link_free_irq(acpi_handle handle)
{
	struct acpi_device *device;
	struct acpi_pci_link *link;
	acpi_status result;

	result = acpi_bus_get_device(handle, &device);
	if (result) {
		printk(KERN_ERR PREFIX "Invalid link device\n");
		return -1;
	}

	link = acpi_driver_data(device);
	if (!link) {
		printk(KERN_ERR PREFIX "Invalid link context\n");
		return -1;
	}

	mutex_lock(&acpi_link_lock);
	if (!link->irq.initialized) {
		mutex_unlock(&acpi_link_lock);
		printk(KERN_ERR PREFIX "Link isn't initialized\n");
		return -1;
	}
#ifdef	FUTURE_USE
	/*
	 * The Link reference count allows us to _DISable an unused link
	 * and suspend time, and set it again  on resume.
	 * However, 2.6.12 still has irq_router.resume
	 * which blindly restores the link state.
	 * So we disable the reference count method
	 * to prevent duplicate acpi_pci_link_set()
	 * which would harm some systems
	 */
	link->refcnt--;
#endif
	ACPI_DEBUG_PRINT((ACPI_DB_INFO,
			  "Link %s is dereferenced\n",
			  acpi_device_bid(link->device)));

	if (link->refcnt == 0)
		acpi_evaluate_object(link->device->handle, "_DIS", NULL, NULL);

	mutex_unlock(&acpi_link_lock);
	return (link->irq.active);
}

/* --------------------------------------------------------------------------
                                 Driver Interface
   -------------------------------------------------------------------------- */

static int acpi_pci_link_add(struct acpi_device *device,
			     const struct acpi_device_id *not_used)
{
	int result;
	struct acpi_pci_link *link;
	int i;
	int found = 0;

	link = kzalloc(sizeof(struct acpi_pci_link), GFP_KERNEL);
	if (!link)
		return -ENOMEM;

	link->device = device;
	strcpy(acpi_device_name(device), ACPI_PCI_LINK_DEVICE_NAME);
	strcpy(acpi_device_class(device), ACPI_PCI_LINK_CLASS);
	device->driver_data = link;

	mutex_lock(&acpi_link_lock);
	result = acpi_pci_link_get_possible(link);
	if (result)
		goto end;

	/* query and set link->irq.active */
	acpi_pci_link_get_current(link);

	printk(KERN_INFO PREFIX "%s [%s] (IRQs", acpi_device_name(device),
	       acpi_device_bid(device));
	for (i = 0; i < link->irq.possible_count; i++) {
		if (link->irq.active == link->irq.possible[i]) {
			printk(KERN_CONT " *%d", link->irq.possible[i]);
			found = 1;
		} else
			printk(KERN_CONT " %d", link->irq.possible[i]);
	}

	printk(KERN_CONT ")");

	if (!found)
		printk(KERN_CONT " *%d", link->irq.active);

	if (!link->device->status.enabled)
		printk(KERN_CONT ", disabled.");

	printk(KERN_CONT "\n");

	list_add_tail(&link->list, &acpi_link_list);

      end:
	/* disable all links -- to be activated on use */
	acpi_evaluate_object(device->handle, "_DIS", NULL, NULL);
	mutex_unlock(&acpi_link_lock);

	if (result)
		kfree(link);

	return result < 0 ? result : 1;
}

static int acpi_pci_link_resume(struct acpi_pci_link *link)
{
	if (link->refcnt && link->irq.active && link->irq.initialized)
		return (acpi_pci_link_set(link, link->irq.active));

	return 0;
}

static void irqrouter_resume(void)
{
	struct acpi_pci_link *link;

	list_for_each_entry(link, &acpi_link_list, list) {
		acpi_pci_link_resume(link);
	}
}

static void acpi_pci_link_remove(struct acpi_device *device)
{
	struct acpi_pci_link *link;

	link = acpi_driver_data(device);

	mutex_lock(&acpi_link_lock);
	list_del(&link->list);
	mutex_unlock(&acpi_link_lock);

	kfree(link);
}

/*
 * modify acpi_isa_irq_penalty[] from cmdline
 */
static int __init acpi_irq_penalty_update(char *str, int used)
{
	int i;

	for (i = 0; i < 16; i++) {
		int retval;
		int irq;
		int new_penalty;

		retval = get_option(&str, &irq);

		if (!retval)
			break;	/* no number found */

		/* see if this is a ISA IRQ */
		if ((irq < 0) || (irq >= ACPI_MAX_ISA_IRQS))
			continue;

		if (used)
			new_penalty = acpi_irq_get_penalty(irq) +
					PIRQ_PENALTY_ISA_USED;
		else
			new_penalty = 0;

		acpi_isa_irq_penalty[irq] = new_penalty;
		if (retval != 2)	/* no next number */
			break;
	}
	return 1;
}

/*
 * We'd like PNP to call this routine for the
 * single ISA_USED value for each legacy device.
 * But instead it calls us with each POSSIBLE setting.
 * There is no ISA_POSSIBLE weight, so we simply use
 * the (small) PCI_USING penalty.
 */
void acpi_penalize_isa_irq(int irq, int active)
{
	if ((irq >= 0) && (irq < ARRAY_SIZE(acpi_isa_irq_penalty)))
		acpi_isa_irq_penalty[irq] = acpi_irq_get_penalty(irq) +
		  (active ? PIRQ_PENALTY_ISA_USED : PIRQ_PENALTY_PCI_USING);
}

bool acpi_isa_irq_available(int irq)
{
	return irq >= 0 && (irq >= ARRAY_SIZE(acpi_isa_irq_penalty) ||
		    acpi_irq_get_penalty(irq) < PIRQ_PENALTY_ISA_ALWAYS);
}

/*
 * Over-ride default table to reserve additional IRQs for use by ISA
 * e.g. acpi_irq_isa=5
 * Useful for telling ACPI how not to interfere with your ISA sound card.
 */
static int __init acpi_irq_isa(char *str)
{
	return acpi_irq_penalty_update(str, 1);
}

__setup("acpi_irq_isa=", acpi_irq_isa);

/*
 * Over-ride default table to free additional IRQs for use by PCI
 * e.g. acpi_irq_pci=7,15
 * Used for acpi_irq_balance to free up IRQs to reduce PCI IRQ sharing.
 */
static int __init acpi_irq_pci(char *str)
{
	return acpi_irq_penalty_update(str, 0);
}

__setup("acpi_irq_pci=", acpi_irq_pci);

static int __init acpi_irq_nobalance_set(char *str)
{
	acpi_irq_balance = 0;
	return 1;
}

__setup("acpi_irq_nobalance", acpi_irq_nobalance_set);

static int __init acpi_irq_balance_set(char *str)
{
	acpi_irq_balance = 1;
	return 1;
}

__setup("acpi_irq_balance", acpi_irq_balance_set);

static struct syscore_ops irqrouter_syscore_ops = {
	.resume = irqrouter_resume,
};

void __init acpi_pci_link_init(void)
{
	if (acpi_noirq)
		return;

	if (acpi_irq_balance == -1) {
		/* no command line switch: enable balancing in IOAPIC mode */
		if (acpi_irq_model == ACPI_IRQ_MODEL_IOAPIC)
			acpi_irq_balance = 1;
		else
			acpi_irq_balance = 0;
	}
	register_syscore_ops(&irqrouter_syscore_ops);
	acpi_scan_add_handler(&pci_link_handler);
}
