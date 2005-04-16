/*
 * Standard Hot Plug Controller Driver
 *
 * Copyright (C) 1995,2001 Compaq Computer Corporation
 * Copyright (C) 2001 Greg Kroah-Hartman (greg@kroah.com)
 * Copyright (C) 2001 IBM Corp.
 * Copyright (C) 2003-2004 Intel Corporation
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
 * Send feedback to <greg@kroah.com>, <dely.l.sy@intel.com>
 *
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/wait.h>
#include <linux/smp_lock.h>
#include <linux/pci.h>
#include "shpchp.h"
#include "shpchprm.h"

static u32 configure_new_device(struct controller *ctrl, struct pci_func *func,
	u8 behind_bridge, struct resource_lists *resources, u8 bridge_bus, u8 bridge_dev);
static int configure_new_function( struct controller *ctrl, struct pci_func *func,
	u8 behind_bridge, struct resource_lists *resources, u8 bridge_bus, u8 bridge_dev);
static void interrupt_event_handler(struct controller *ctrl);

static struct semaphore event_semaphore;	/* mutex for process loop (up if something to process) */
static struct semaphore event_exit;		/* guard ensure thread has exited before calling it quits */
static int event_finished;
static unsigned long pushbutton_pending;	/* = 0 */

u8 shpchp_disk_irq;
u8 shpchp_nic_irq;

u8 shpchp_handle_attention_button(u8 hp_slot, void *inst_id)
{
	struct controller *ctrl = (struct controller *) inst_id;
	struct slot *p_slot;
	u8 rc = 0;
	u8 getstatus;
	struct pci_func *func;
	struct event_info *taskInfo;

	/* Attention Button Change */
	dbg("shpchp:  Attention button interrupt received.\n");
	
	func = shpchp_slot_find(ctrl->slot_bus, (hp_slot + ctrl->slot_device_offset), 0);

	/* This is the structure that tells the worker thread what to do */
	taskInfo = &(ctrl->event_queue[ctrl->next_event]);
	p_slot = shpchp_find_slot(ctrl, hp_slot + ctrl->slot_device_offset);

	p_slot->hpc_ops->get_adapter_status(p_slot, &(func->presence_save));
	p_slot->hpc_ops->get_latch_status(p_slot, &getstatus);
	
	ctrl->next_event = (ctrl->next_event + 1) % 10;
	taskInfo->hp_slot = hp_slot;

	rc++;

	/*
	 *  Button pressed - See if need to TAKE ACTION!!!
	 */
	info("Button pressed on Slot(%d)\n", ctrl->first_slot + hp_slot);
	taskInfo->event_type = INT_BUTTON_PRESS;

	if ((p_slot->state == BLINKINGON_STATE)
	    || (p_slot->state == BLINKINGOFF_STATE)) {
		/* Cancel if we are still blinking; this means that we press the
		 * attention again before the 5 sec. limit expires to cancel hot-add
		 * or hot-remove
		 */
		taskInfo->event_type = INT_BUTTON_CANCEL;
		info("Button cancel on Slot(%d)\n", ctrl->first_slot + hp_slot);
	} else if ((p_slot->state == POWERON_STATE)
		   || (p_slot->state == POWEROFF_STATE)) {
		/* Ignore if the slot is on power-on or power-off state; this 
		 * means that the previous attention button action to hot-add or
		 * hot-remove is undergoing
		 */
		taskInfo->event_type = INT_BUTTON_IGNORE;
		info("Button ignore on Slot(%d)\n", ctrl->first_slot + hp_slot);
	}

	if (rc)
		up(&event_semaphore);	/* signal event thread that new event is posted */

	return 0;

}

u8 shpchp_handle_switch_change(u8 hp_slot, void *inst_id)
{
	struct controller *ctrl = (struct controller *) inst_id;
	struct slot *p_slot;
	u8 rc = 0;
	u8 getstatus;
	struct pci_func *func;
	struct event_info *taskInfo;

	/* Switch Change */
	dbg("shpchp:  Switch interrupt received.\n");

	func = shpchp_slot_find(ctrl->slot_bus, (hp_slot + ctrl->slot_device_offset), 0);

	/* This is the structure that tells the worker thread
	 * what to do
	 */
	taskInfo = &(ctrl->event_queue[ctrl->next_event]);
	ctrl->next_event = (ctrl->next_event + 1) % 10;
	taskInfo->hp_slot = hp_slot;

	rc++;
	p_slot = shpchp_find_slot(ctrl, hp_slot + ctrl->slot_device_offset);
	p_slot->hpc_ops->get_adapter_status(p_slot, &(func->presence_save));
	p_slot->hpc_ops->get_latch_status(p_slot, &getstatus);
	dbg("%s: Card present %x Power status %x\n", __FUNCTION__,
		func->presence_save, func->pwr_save);

	if (getstatus) {
		/*
		 * Switch opened
		 */
		info("Latch open on Slot(%d)\n", ctrl->first_slot + hp_slot);
		func->switch_save = 0;
		taskInfo->event_type = INT_SWITCH_OPEN;
		if (func->pwr_save && func->presence_save) {
			taskInfo->event_type = INT_POWER_FAULT;
			err("Surprise Removal of card\n");
		}
	} else {
		/*
		 *  Switch closed
		 */
		info("Latch close on Slot(%d)\n", ctrl->first_slot + hp_slot);
		func->switch_save = 0x10;
		taskInfo->event_type = INT_SWITCH_CLOSE;
	}

	if (rc)
		up(&event_semaphore);	/* signal event thread that new event is posted */

	return rc;
}

u8 shpchp_handle_presence_change(u8 hp_slot, void *inst_id)
{
	struct controller *ctrl = (struct controller *) inst_id;
	struct slot *p_slot;
	u8 rc = 0;
	/*u8 temp_byte;*/
	struct pci_func *func;
	struct event_info *taskInfo;

	/* Presence Change */
	dbg("shpchp:  Presence/Notify input change.\n");

	func = shpchp_slot_find(ctrl->slot_bus, (hp_slot + ctrl->slot_device_offset), 0);

	/* This is the structure that tells the worker thread
	 * what to do
	 */
	taskInfo = &(ctrl->event_queue[ctrl->next_event]);
	ctrl->next_event = (ctrl->next_event + 1) % 10;
	taskInfo->hp_slot = hp_slot;

	rc++;
	p_slot = shpchp_find_slot(ctrl, hp_slot + ctrl->slot_device_offset);

	/* 
	 * Save the presence state
	 */
	p_slot->hpc_ops->get_adapter_status(p_slot, &(func->presence_save));
	if (func->presence_save) {
		/*
		 * Card Present
		 */
		info("Card present on Slot(%d)\n", ctrl->first_slot + hp_slot);
		taskInfo->event_type = INT_PRESENCE_ON;
	} else {
		/*
		 * Not Present
		 */
		info("Card not present on Slot(%d)\n", ctrl->first_slot + hp_slot);
		taskInfo->event_type = INT_PRESENCE_OFF;
	}

	if (rc)
		up(&event_semaphore);	/* signal event thread that new event is posted */

	return rc;
}

u8 shpchp_handle_power_fault(u8 hp_slot, void *inst_id)
{
	struct controller *ctrl = (struct controller *) inst_id;
	struct slot *p_slot;
	u8 rc = 0;
	struct pci_func *func;
	struct event_info *taskInfo;

	/* Power fault */
	dbg("shpchp:  Power fault interrupt received.\n");

	func = shpchp_slot_find(ctrl->slot_bus, (hp_slot + ctrl->slot_device_offset), 0);

	/* This is the structure that tells the worker thread
	 * what to do
	 */
	taskInfo = &(ctrl->event_queue[ctrl->next_event]);
	ctrl->next_event = (ctrl->next_event + 1) % 10;
	taskInfo->hp_slot = hp_slot;

	rc++;
	p_slot = shpchp_find_slot(ctrl, hp_slot + ctrl->slot_device_offset);

	if ( !(p_slot->hpc_ops->query_power_fault(p_slot))) {
		/*
		 * Power fault Cleared
		 */
		info("Power fault cleared on Slot(%d)\n", ctrl->first_slot + hp_slot);
		func->status = 0x00;
		taskInfo->event_type = INT_POWER_FAULT_CLEAR;
	} else {
		/*
		 *   Power fault
		 */
		info("Power fault on Slot(%d)\n", ctrl->first_slot + hp_slot);
		taskInfo->event_type = INT_POWER_FAULT;
		/* set power fault status for this board */
		func->status = 0xFF;
		info("power fault bit %x set\n", hp_slot);
	}
	if (rc)
		up(&event_semaphore);	/* signal event thread that new event is posted */

	return rc;
}


/*
 * sort_by_size
 *
 * Sorts nodes on the list by their length.
 * Smallest first.
 *
 */
static int sort_by_size(struct pci_resource **head)
{
	struct pci_resource *current_res;
	struct pci_resource *next_res;
	int out_of_order = 1;

	if (!(*head))
		return(1);

	if (!((*head)->next))
		return(0);

	while (out_of_order) {
		out_of_order = 0;

		/* Special case for swapping list head */
		if (((*head)->next) &&
		    ((*head)->length > (*head)->next->length)) {
			out_of_order++;
			current_res = *head;
			*head = (*head)->next;
			current_res->next = (*head)->next;
			(*head)->next = current_res;
		}

		current_res = *head;

		while (current_res->next && current_res->next->next) {
			if (current_res->next->length > current_res->next->next->length) {
				out_of_order++;
				next_res = current_res->next;
				current_res->next = current_res->next->next;
				current_res = current_res->next;
				next_res->next = current_res->next;
				current_res->next = next_res;
			} else
				current_res = current_res->next;
		}
	}  /* End of out_of_order loop */

	return(0);
}


/*
 * sort_by_max_size
 *
 * Sorts nodes on the list by their length.
 * Largest first.
 *
 */
static int sort_by_max_size(struct pci_resource **head)
{
	struct pci_resource *current_res;
	struct pci_resource *next_res;
	int out_of_order = 1;

	if (!(*head))
		return(1);

	if (!((*head)->next))
		return(0);

	while (out_of_order) {
		out_of_order = 0;

		/* Special case for swapping list head */
		if (((*head)->next) &&
		    ((*head)->length < (*head)->next->length)) {
			out_of_order++;
			current_res = *head;
			*head = (*head)->next;
			current_res->next = (*head)->next;
			(*head)->next = current_res;
		}

		current_res = *head;

		while (current_res->next && current_res->next->next) {
			if (current_res->next->length < current_res->next->next->length) {
				out_of_order++;
				next_res = current_res->next;
				current_res->next = current_res->next->next;
				current_res = current_res->next;
				next_res->next = current_res->next;
				current_res->next = next_res;
			} else
				current_res = current_res->next;
		}
	}  /* End of out_of_order loop */

	return(0);
}


/*
 * do_pre_bridge_resource_split
 *
 *	Returns zero or one node of resources that aren't in use
 *
 */
static struct pci_resource *do_pre_bridge_resource_split (struct pci_resource **head, struct pci_resource **orig_head, u32 alignment)
{
	struct pci_resource *prevnode = NULL;
	struct pci_resource *node;
	struct pci_resource *split_node;
	u32 rc;
	u32 temp_dword;
	dbg("do_pre_bridge_resource_split\n");

	if (!(*head) || !(*orig_head))
		return(NULL);

	rc = shpchp_resource_sort_and_combine(head);

	if (rc)
		return(NULL);

	if ((*head)->base != (*orig_head)->base)
		return(NULL);

	if ((*head)->length == (*orig_head)->length)
		return(NULL);


	/* If we got here, there the bridge requires some of the resource, but
	 *  we may be able to split some off of the front
	 */	
	node = *head;

	if (node->length & (alignment -1)) {
		/* This one isn't an aligned length, so we'll make a new entry
		 * and split it up.
		 */
		split_node = kmalloc(sizeof(*split_node), GFP_KERNEL);

		if (!split_node)
			return(NULL);

		temp_dword = (node->length | (alignment-1)) + 1 - alignment;

		split_node->base = node->base;
		split_node->length = temp_dword;

		node->length -= temp_dword;
		node->base += split_node->length;

		/* Put it in the list */
		*head = split_node;
		split_node->next = node;
	}

	if (node->length < alignment) {
		return(NULL);
	}

	/* Now unlink it */
	if (*head == node) {
		*head = node->next;
		node->next = NULL;
	} else {
		prevnode = *head;
		while (prevnode->next != node)
			prevnode = prevnode->next;

		prevnode->next = node->next;
		node->next = NULL;
	}

	return(node);
}


/*
 * do_bridge_resource_split
 *
 *	Returns zero or one node of resources that aren't in use
 *
 */
static struct pci_resource *do_bridge_resource_split (struct pci_resource **head, u32 alignment)
{
	struct pci_resource *prevnode = NULL;
	struct pci_resource *node;
	u32 rc;
	u32 temp_dword;

	if (!(*head))
		return(NULL);

	rc = shpchp_resource_sort_and_combine(head);

	if (rc)
		return(NULL);

	node = *head;

	while (node->next) {
		prevnode = node;
		node = node->next;
		kfree(prevnode);
	}

	if (node->length < alignment) {
		kfree(node);
		return(NULL);
	}

	if (node->base & (alignment - 1)) {
		/* Short circuit if adjusted size is too small */
		temp_dword = (node->base | (alignment-1)) + 1;
		if ((node->length - (temp_dword - node->base)) < alignment) {
			kfree(node);
			return(NULL);
		}

		node->length -= (temp_dword - node->base);
		node->base = temp_dword;
	}

	if (node->length & (alignment - 1)) {
		/* There's stuff in use after this node */
		kfree(node);
		return(NULL);
	}

	return(node);
}


/*
 * get_io_resource
 *
 * this function sorts the resource list by size and then
 * returns the first node of "size" length that is not in the
 * ISA aliasing window.  If it finds a node larger than "size"
 * it will split it up.
 *
 * size must be a power of two.
 */
static struct pci_resource *get_io_resource (struct pci_resource **head, u32 size)
{
	struct pci_resource *prevnode;
	struct pci_resource *node;
	struct pci_resource *split_node = NULL;
	u32 temp_dword;

	if (!(*head))
		return(NULL);

	if ( shpchp_resource_sort_and_combine(head) )
		return(NULL);

	if ( sort_by_size(head) )
		return(NULL);

	for (node = *head; node; node = node->next) {
		if (node->length < size)
			continue;

		if (node->base & (size - 1)) {
			/* This one isn't base aligned properly
			   so we'll make a new entry and split it up */
			temp_dword = (node->base | (size-1)) + 1;

			/*/ Short circuit if adjusted size is too small */
			if ((node->length - (temp_dword - node->base)) < size)
				continue;

			split_node = kmalloc(sizeof(*split_node), GFP_KERNEL);

			if (!split_node)
				return(NULL);

			split_node->base = node->base;
			split_node->length = temp_dword - node->base;
			node->base = temp_dword;
			node->length -= split_node->length;

			/* Put it in the list */
			split_node->next = node->next;
			node->next = split_node;
		} /* End of non-aligned base */

		/* Don't need to check if too small since we already did */
		if (node->length > size) {
			/* This one is longer than we need
			   so we'll make a new entry and split it up */
			split_node = kmalloc(sizeof(*split_node), GFP_KERNEL);

			if (!split_node)
				return(NULL);

			split_node->base = node->base + size;
			split_node->length = node->length - size;
			node->length = size;

			/* Put it in the list */
			split_node->next = node->next;
			node->next = split_node;
		}  /* End of too big on top end */

		/* For IO make sure it's not in the ISA aliasing space */
		if (node->base & 0x300L)
			continue;

		/* If we got here, then it is the right size 
		   Now take it out of the list */
		if (*head == node) {
			*head = node->next;
		} else {
			prevnode = *head;
			while (prevnode->next != node)
				prevnode = prevnode->next;

			prevnode->next = node->next;
		}
		node->next = NULL;
		/* Stop looping */
		break;
	}

	return(node);
}


/*
 * get_max_resource
 *
 * Gets the largest node that is at least "size" big from the
 * list pointed to by head.  It aligns the node on top and bottom
 * to "size" alignment before returning it.
 * J.I. modified to put max size limits of; 64M->32M->16M->8M->4M->1M
 *  This is needed to avoid allocating entire ACPI _CRS res to one child bridge/slot.
 */
static struct pci_resource *get_max_resource (struct pci_resource **head, u32 size)
{
	struct pci_resource *max;
	struct pci_resource *temp;
	struct pci_resource *split_node;
	u32 temp_dword;
	u32 max_size[] = { 0x4000000, 0x2000000, 0x1000000, 0x0800000, 0x0400000, 0x0200000, 0x0100000, 0x00 };
	int i;

	if (!(*head))
		return(NULL);

	if (shpchp_resource_sort_and_combine(head))
		return(NULL);

	if (sort_by_max_size(head))
		return(NULL);

	for (max = *head;max; max = max->next) {

		/* If not big enough we could probably just bail, 
		   instead we'll continue to the next. */
		if (max->length < size)
			continue;

		if (max->base & (size - 1)) {
			/* This one isn't base aligned properly
			   so we'll make a new entry and split it up */
			temp_dword = (max->base | (size-1)) + 1;

			/* Short circuit if adjusted size is too small */
			if ((max->length - (temp_dword - max->base)) < size)
				continue;

			split_node = kmalloc(sizeof(*split_node), GFP_KERNEL);

			if (!split_node)
				return(NULL);

			split_node->base = max->base;
			split_node->length = temp_dword - max->base;
			max->base = temp_dword;
			max->length -= split_node->length;

			/* Put it next in the list */
			split_node->next = max->next;
			max->next = split_node;
		}

		if ((max->base + max->length) & (size - 1)) {
			/* This one isn't end aligned properly at the top
			   so we'll make a new entry and split it up */
			split_node = kmalloc(sizeof(*split_node), GFP_KERNEL);

			if (!split_node)
				return(NULL);
			temp_dword = ((max->base + max->length) & ~(size - 1));
			split_node->base = temp_dword;
			split_node->length = max->length + max->base
					     - split_node->base;
			max->length -= split_node->length;

			/* Put it in the list */
			split_node->next = max->next;
			max->next = split_node;
		}

		/* Make sure it didn't shrink too much when we aligned it */
		if (max->length < size)
			continue;

		for ( i = 0; max_size[i] > size; i++) {
			if (max->length > max_size[i]) {
				split_node = kmalloc(sizeof(*split_node),
							GFP_KERNEL);
				if (!split_node)
					break;	/* return (NULL); */
				split_node->base = max->base + max_size[i];
				split_node->length = max->length - max_size[i];
				max->length = max_size[i];
				/* Put it next in the list */
				split_node->next = max->next;
				max->next = split_node;
				break;
			}
		}

		/* Now take it out of the list */
		temp = (struct pci_resource*) *head;
		if (temp == max) {
			*head = max->next;
		} else {
			while (temp && temp->next != max) {
				temp = temp->next;
			}

			temp->next = max->next;
		}

		max->next = NULL;
		return(max);
	}

	/* If we get here, we couldn't find one */
	return(NULL);
}


/*
 * get_resource
 *
 * this function sorts the resource list by size and then
 * returns the first node of "size" length.  If it finds a node
 * larger than "size" it will split it up.
 *
 * size must be a power of two.
 */
static struct pci_resource *get_resource (struct pci_resource **head, u32 size)
{
	struct pci_resource *prevnode;
	struct pci_resource *node;
	struct pci_resource *split_node;
	u32 temp_dword;

	if (!(*head))
		return(NULL);

	if ( shpchp_resource_sort_and_combine(head) )
		return(NULL);

	if ( sort_by_size(head) )
		return(NULL);

	for (node = *head; node; node = node->next) {
		dbg("%s: req_size =0x%x node=%p, base=0x%x, length=0x%x\n",
		    __FUNCTION__, size, node, node->base, node->length);
		if (node->length < size)
			continue;

		if (node->base & (size - 1)) {
			dbg("%s: not aligned\n", __FUNCTION__);
			/* this one isn't base aligned properly
			   so we'll make a new entry and split it up */
			temp_dword = (node->base | (size-1)) + 1;

			/* Short circuit if adjusted size is too small */
			if ((node->length - (temp_dword - node->base)) < size)
				continue;

			split_node = kmalloc(sizeof(*split_node), GFP_KERNEL);

			if (!split_node)
				return(NULL);

			split_node->base = node->base;
			split_node->length = temp_dword - node->base;
			node->base = temp_dword;
			node->length -= split_node->length;

			/* Put it in the list */
			split_node->next = node->next;
			node->next = split_node;
		} /* End of non-aligned base */

		/* Don't need to check if too small since we already did */
		if (node->length > size) {
			dbg("%s: too big\n", __FUNCTION__);
			/* this one is longer than we need
			   so we'll make a new entry and split it up */
			split_node = kmalloc(sizeof(*split_node), GFP_KERNEL);

			if (!split_node)
				return(NULL);

			split_node->base = node->base + size;
			split_node->length = node->length - size;
			node->length = size;

			/* Put it in the list */
			split_node->next = node->next;
			node->next = split_node;
		}  /* End of too big on top end */

		dbg("%s: got one!!!\n", __FUNCTION__);
		/* If we got here, then it is the right size
		   Now take it out of the list */
		if (*head == node) {
			*head = node->next;
		} else {
			prevnode = *head;
			while (prevnode->next != node)
				prevnode = prevnode->next;

			prevnode->next = node->next;
		}
		node->next = NULL;
		/* Stop looping */
		break;
	}
	return(node);
}


/*
 * shpchp_resource_sort_and_combine
 *
 * Sorts all of the nodes in the list in ascending order by
 * their base addresses.  Also does garbage collection by
 * combining adjacent nodes.
 *
 * returns 0 if success
 */
int shpchp_resource_sort_and_combine(struct pci_resource **head)
{
	struct pci_resource *node1;
	struct pci_resource *node2;
	int out_of_order = 1;

	dbg("%s: head = %p, *head = %p\n", __FUNCTION__, head, *head);

	if (!(*head))
		return(1);

	dbg("*head->next = %p\n",(*head)->next);

	if (!(*head)->next)
		return(0);	/* only one item on the list, already sorted! */

	dbg("*head->base = 0x%x\n",(*head)->base);
	dbg("*head->next->base = 0x%x\n",(*head)->next->base);
	while (out_of_order) {
		out_of_order = 0;

		/* Special case for swapping list head */
		if (((*head)->next) &&
		    ((*head)->base > (*head)->next->base)) {
			node1 = *head;
			(*head) = (*head)->next;
			node1->next = (*head)->next;
			(*head)->next = node1;
			out_of_order++;
		}

		node1 = (*head);

		while (node1->next && node1->next->next) {
			if (node1->next->base > node1->next->next->base) {
				out_of_order++;
				node2 = node1->next;
				node1->next = node1->next->next;
				node1 = node1->next;
				node2->next = node1->next;
				node1->next = node2;
			} else
				node1 = node1->next;
		}
	}  /* End of out_of_order loop */

	node1 = *head;

	while (node1 && node1->next) {
		if ((node1->base + node1->length) == node1->next->base) {
			/* Combine */
			dbg("8..\n");
			node1->length += node1->next->length;
			node2 = node1->next;
			node1->next = node1->next->next;
			kfree(node2);
		} else
			node1 = node1->next;
	}

	return(0);
}


/**
 * shpchp_slot_create - Creates a node and adds it to the proper bus.
 * @busnumber - bus where new node is to be located
 *
 * Returns pointer to the new node or NULL if unsuccessful
 */
struct pci_func *shpchp_slot_create(u8 busnumber)
{
	struct pci_func *new_slot;
	struct pci_func *next;

	new_slot = kmalloc(sizeof(*new_slot), GFP_KERNEL);

	if (new_slot == NULL) {
		return(new_slot);
	}

	memset(new_slot, 0, sizeof(struct pci_func));

	new_slot->next = NULL;
	new_slot->configured = 1;

	if (shpchp_slot_list[busnumber] == NULL) {
		shpchp_slot_list[busnumber] = new_slot;
	} else {
		next = shpchp_slot_list[busnumber];
		while (next->next != NULL)
			next = next->next;
		next->next = new_slot;
	}
	return(new_slot);
}


/*
 * slot_remove - Removes a node from the linked list of slots.
 * @old_slot: slot to remove
 *
 * Returns 0 if successful, !0 otherwise.
 */
static int slot_remove(struct pci_func * old_slot)
{
	struct pci_func *next;

	if (old_slot == NULL)
		return(1);

	next = shpchp_slot_list[old_slot->bus];

	if (next == NULL) {
		return(1);
	}

	if (next == old_slot) {
		shpchp_slot_list[old_slot->bus] = old_slot->next;
		shpchp_destroy_board_resources(old_slot);
		kfree(old_slot);
		return(0);
	}

	while ((next->next != old_slot) && (next->next != NULL)) {
		next = next->next;
	}

	if (next->next == old_slot) {
		next->next = old_slot->next;
		shpchp_destroy_board_resources(old_slot);
		kfree(old_slot);
		return(0);
	} else
		return(2);
}


/**
 * bridge_slot_remove - Removes a node from the linked list of slots.
 * @bridge: bridge to remove
 *
 * Returns 0 if successful, !0 otherwise.
 */
static int bridge_slot_remove(struct pci_func *bridge)
{
	u8 subordinateBus, secondaryBus;
	u8 tempBus;
	struct pci_func *next;

	if (bridge == NULL)
		return(1);

	secondaryBus = (bridge->config_space[0x06] >> 8) & 0xFF;
	subordinateBus = (bridge->config_space[0x06] >> 16) & 0xFF;

	for (tempBus = secondaryBus; tempBus <= subordinateBus; tempBus++) {
		next = shpchp_slot_list[tempBus];

		while (!slot_remove(next)) {
			next = shpchp_slot_list[tempBus];
		}
	}

	next = shpchp_slot_list[bridge->bus];

	if (next == NULL) {
		return(1);
	}

	if (next == bridge) {
		shpchp_slot_list[bridge->bus] = bridge->next;
		kfree(bridge);
		return(0);
	}

	while ((next->next != bridge) && (next->next != NULL)) {
		next = next->next;
	}

	if (next->next == bridge) {
		next->next = bridge->next;
		kfree(bridge);
		return(0);
	} else
		return(2);
}


/**
 * shpchp_slot_find - Looks for a node by bus, and device, multiple functions accessed
 * @bus: bus to find
 * @device: device to find
 * @index: is 0 for first function found, 1 for the second...
 *
 * Returns pointer to the node if successful, %NULL otherwise.
 */
struct pci_func *shpchp_slot_find(u8 bus, u8 device, u8 index)
{
	int found = -1;
	struct pci_func *func;

	func = shpchp_slot_list[bus];

	if ((func == NULL) || ((func->device == device) && (index == 0)))
		return(func);

	if (func->device == device)
		found++;

	while (func->next != NULL) {
		func = func->next;

		if (func->device == device)
			found++;

		if (found == index)
			return(func);
	}

	return(NULL);
}

static int is_bridge(struct pci_func * func)
{
	/* Check the header type */
	if (((func->config_space[0x03] >> 16) & 0xFF) == 0x01)
		return 1;
	else
		return 0;
}


/* The following routines constitute the bulk of the 
   hotplug controller logic
 */
static u32 change_bus_speed(struct controller *ctrl, struct slot *p_slot, enum pci_bus_speed speed)
{ 
	u32 rc = 0;

	dbg("%s: change to speed %d\n", __FUNCTION__, speed);
	down(&ctrl->crit_sect);
	if ((rc = p_slot->hpc_ops->set_bus_speed_mode(p_slot, speed))) {
		err("%s: Issue of set bus speed mode command failed\n", __FUNCTION__);
		up(&ctrl->crit_sect);
		return WRONG_BUS_FREQUENCY;
	}
	wait_for_ctrl_irq (ctrl);
		
	if ((rc = p_slot->hpc_ops->check_cmd_status(ctrl))) {
		err("%s: Can't set bus speed/mode in the case of adapter & bus mismatch\n",
			  __FUNCTION__);
		err("%s: Error code (%d)\n", __FUNCTION__, rc);
		up(&ctrl->crit_sect);
		return WRONG_BUS_FREQUENCY;
	}
	up(&ctrl->crit_sect);
	return rc;
}

static u32 fix_bus_speed(struct controller *ctrl, struct slot *pslot, u8 flag, 
enum pci_bus_speed asp, enum pci_bus_speed bsp, enum pci_bus_speed msp)
{ 
	u32 rc = 0;
	
	if (flag != 0) { /* Other slots on the same bus are occupied */
		if ( asp < bsp ) {
			err("%s: speed of bus %x and adapter %x mismatch\n", __FUNCTION__, bsp, asp);
			return WRONG_BUS_FREQUENCY;
		}
	} else {
		/* Other slots on the same bus are empty */
		if (msp == bsp) {
		/* if adapter_speed >= bus_speed, do nothing */
			if (asp < bsp) {
				/* 
				* Try to lower bus speed to accommodate the adapter if other slots 
				* on the same controller are empty
				*/
				if ((rc = change_bus_speed(ctrl, pslot, asp)))
					return rc;
			} 
		} else {
			if (asp < msp) {
				if ((rc = change_bus_speed(ctrl, pslot, asp)))
					return rc;
			} else {
				if ((rc = change_bus_speed(ctrl, pslot, msp)))
					return rc;
			}
		}
	}
	return rc;
}

/**
 * board_added - Called after a board has been added to the system.
 *
 * Turns power on for the board
 * Configures board
 *
 */
static u32 board_added(struct pci_func * func, struct controller * ctrl)
{
	u8 hp_slot;
	u8 slots_not_empty = 0;
	int index;
	u32 temp_register = 0xFFFFFFFF;
	u32 retval, rc = 0;
	struct pci_func *new_func = NULL;
	struct slot *p_slot;
	struct resource_lists res_lists;
	enum pci_bus_speed adapter_speed, bus_speed, max_bus_speed;
	u8 pi, mode;

	p_slot = shpchp_find_slot(ctrl, func->device);
	hp_slot = func->device - ctrl->slot_device_offset;

	dbg("%s: func->device, slot_offset, hp_slot = %d, %d ,%d\n", __FUNCTION__, func->device, ctrl->slot_device_offset, hp_slot);

	/* Wait for exclusive access to hardware */
	down(&ctrl->crit_sect);

	/* Power on slot without connecting to bus */
	rc = p_slot->hpc_ops->power_on_slot(p_slot);
	if (rc) {
		err("%s: Failed to power on slot\n", __FUNCTION__);
		/* Done with exclusive hardware access */
		up(&ctrl->crit_sect);
		return -1;
	}
			
	/* Wait for the command to complete */
	wait_for_ctrl_irq (ctrl);
	
	rc = p_slot->hpc_ops->check_cmd_status(ctrl);
	if (rc) {
		err("%s: Failed to power on slot, error code(%d)\n", __FUNCTION__, rc);
		/* Done with exclusive hardware access */
		up(&ctrl->crit_sect);
		return -1;
	}

	
	if ((ctrl->pci_dev->vendor == 0x8086) && (ctrl->pci_dev->device == 0x0332)) {
		if (slots_not_empty)
			return WRONG_BUS_FREQUENCY;
		
		if ((rc = p_slot->hpc_ops->set_bus_speed_mode(p_slot, PCI_SPEED_33MHz))) {
			err("%s: Issue of set bus speed mode command failed\n", __FUNCTION__);
			up(&ctrl->crit_sect);
			return WRONG_BUS_FREQUENCY;
		}
		wait_for_ctrl_irq (ctrl);
		
		if ((rc = p_slot->hpc_ops->check_cmd_status(ctrl))) {
			err("%s: Can't set bus speed/mode in the case of adapter & bus mismatch\n",
				  __FUNCTION__);
			err("%s: Error code (%d)\n", __FUNCTION__, rc);
			up(&ctrl->crit_sect);
			return WRONG_BUS_FREQUENCY;
		}
		/* turn on board, blink green LED, turn off Amber LED */
		if ((rc = p_slot->hpc_ops->slot_enable(p_slot))) {
			err("%s: Issue of Slot Enable command failed\n", __FUNCTION__);
			up(&ctrl->crit_sect);
			return rc;
		}
		wait_for_ctrl_irq (ctrl);

		if ((rc = p_slot->hpc_ops->check_cmd_status(ctrl))) {
			err("%s: Failed to enable slot, error code(%d)\n", __FUNCTION__, rc);
			up(&ctrl->crit_sect);
			return rc;  
		}
	}
 
	rc = p_slot->hpc_ops->get_adapter_speed(p_slot, &adapter_speed);
	/* 0 = PCI 33Mhz, 1 = PCI 66 Mhz, 2 = PCI-X 66 PA, 4 = PCI-X 66 ECC, */
	/* 5 = PCI-X 133 PA, 7 = PCI-X 133 ECC,  0xa = PCI-X 133 Mhz 266, */
	/* 0xd = PCI-X 133 Mhz 533 */
	/* This encoding is different from the one used in cur_bus_speed & */
	/* max_bus_speed */

	if (rc  || adapter_speed == PCI_SPEED_UNKNOWN) {
		err("%s: Can't get adapter speed or bus mode mismatch\n", __FUNCTION__);
		/* Done with exclusive hardware access */
		up(&ctrl->crit_sect);
		return WRONG_BUS_FREQUENCY;
	}

	rc = p_slot->hpc_ops->get_cur_bus_speed(p_slot, &bus_speed);
	if (rc || bus_speed == PCI_SPEED_UNKNOWN) {
		err("%s: Can't get bus operation speed\n", __FUNCTION__);
		/* Done with exclusive hardware access */
		up(&ctrl->crit_sect);
		return WRONG_BUS_FREQUENCY;
	}

	rc = p_slot->hpc_ops->get_max_bus_speed(p_slot, &max_bus_speed);
	if (rc || max_bus_speed == PCI_SPEED_UNKNOWN) {
		err("%s: Can't get max bus operation speed\n", __FUNCTION__);
		max_bus_speed = bus_speed;
	}

	/* Done with exclusive hardware access */
	up(&ctrl->crit_sect);

	if ((rc  = p_slot->hpc_ops->get_prog_int(p_slot, &pi))) {
		err("%s: Can't get controller programming interface, set it to 1\n", __FUNCTION__);
		pi = 1;
	}

	/* Check if there are other slots or devices on the same bus */
	if (!list_empty(&ctrl->pci_dev->subordinate->devices))
		slots_not_empty = 1;

	dbg("%s: slots_not_empty %d, pi %d\n", __FUNCTION__, 
		slots_not_empty, pi);
	dbg("adapter_speed %d, bus_speed %d, max_bus_speed %d\n", 
		adapter_speed, bus_speed, max_bus_speed);

	if (pi == 2) {
		dbg("%s: In PI = %d\n", __FUNCTION__, pi);
		if ((rc = p_slot->hpc_ops->get_mode1_ECC_cap(p_slot, &mode))) {
			err("%s: Can't get Mode1_ECC, set mode to 0\n", __FUNCTION__);
			mode = 0;
		}

		switch (adapter_speed) {
		case PCI_SPEED_133MHz_PCIX_533:
		case PCI_SPEED_133MHz_PCIX_266:
			if ((bus_speed != adapter_speed) &&
			   ((rc = fix_bus_speed(ctrl, p_slot, slots_not_empty, adapter_speed, bus_speed, max_bus_speed)))) 
				return rc;
			break;	
		case PCI_SPEED_133MHz_PCIX_ECC:
		case PCI_SPEED_133MHz_PCIX:
			if (mode) { /* Bus - Mode 1 ECC */
				if ((bus_speed != 0x7) &&
				   ((rc = fix_bus_speed(ctrl, p_slot, slots_not_empty, adapter_speed, bus_speed, max_bus_speed)))) 
					return rc;
			} else {
				if ((bus_speed != 0x4) &&
				   ((rc = fix_bus_speed(ctrl, p_slot, slots_not_empty, adapter_speed, bus_speed, max_bus_speed)))) 
					return rc;
			}
			break;
		case PCI_SPEED_66MHz_PCIX_ECC:
		case PCI_SPEED_66MHz_PCIX:
			if (mode) { /* Bus - Mode 1 ECC */
				if ((bus_speed != 0x5) &&
				   ((rc = fix_bus_speed(ctrl, p_slot, slots_not_empty, adapter_speed, bus_speed, max_bus_speed)))) 
					return rc;
			} else {
				if ((bus_speed != 0x2) &&
				   ((rc = fix_bus_speed(ctrl, p_slot, slots_not_empty, adapter_speed, bus_speed, max_bus_speed)))) 
					return rc;
			}
			break;
		case PCI_SPEED_66MHz:
			if ((bus_speed != 0x1) &&
			   ((rc = fix_bus_speed(ctrl, p_slot, slots_not_empty, adapter_speed, bus_speed, max_bus_speed)))) 
				return rc;
			break;	
		case PCI_SPEED_33MHz:
			if (bus_speed > 0x0) {
				if (slots_not_empty == 0) {
					if ((rc = change_bus_speed(ctrl, p_slot, adapter_speed)))
						return rc;
				} else {
					err("%s: speed of bus %x and adapter %x mismatch\n", __FUNCTION__, bus_speed, adapter_speed);
					return WRONG_BUS_FREQUENCY;
				}
			}
			break;
		default:
			err("%s: speed of bus %x and adapter %x mismatch\n", __FUNCTION__, bus_speed, adapter_speed);
			return WRONG_BUS_FREQUENCY;
		}
	} else {
		/* If adpater_speed == bus_speed, nothing to do here */
		dbg("%s: In PI = %d\n", __FUNCTION__, pi);
		if ((adapter_speed != bus_speed) &&
		   ((rc = fix_bus_speed(ctrl, p_slot, slots_not_empty, adapter_speed, bus_speed, max_bus_speed))))
				return rc;
	}

	down(&ctrl->crit_sect);
	/* turn on board, blink green LED, turn off Amber LED */
	if ((rc = p_slot->hpc_ops->slot_enable(p_slot))) {
		err("%s: Issue of Slot Enable command failed\n", __FUNCTION__);
		up(&ctrl->crit_sect);
		return rc;
	}
	wait_for_ctrl_irq (ctrl);

	if ((rc = p_slot->hpc_ops->check_cmd_status(ctrl))) {
		err("%s: Failed to enable slot, error code(%d)\n", __FUNCTION__, rc);
		up(&ctrl->crit_sect);
		return rc;  
	}

	up(&ctrl->crit_sect);

	/* Wait for ~1 second */
	dbg("%s: before long_delay\n", __FUNCTION__);
	wait_for_ctrl_irq (ctrl);
	dbg("%s: after long_delay\n", __FUNCTION__);

	dbg("%s: func status = %x\n", __FUNCTION__, func->status);
	/* Check for a power fault */
	if (func->status == 0xFF) {
		/* power fault occurred, but it was benign */
		temp_register = 0xFFFFFFFF;
		dbg("%s: temp register set to %x by power fault\n", __FUNCTION__, temp_register);
		rc = POWER_FAILURE;
		func->status = 0;
	} else {
		/* Get vendor/device ID u32 */
		rc = pci_bus_read_config_dword (ctrl->pci_dev->subordinate, PCI_DEVFN(func->device, func->function), 
			PCI_VENDOR_ID, &temp_register);
		dbg("%s: pci_bus_read_config_dword returns %d\n", __FUNCTION__, rc);
		dbg("%s: temp_register is %x\n", __FUNCTION__, temp_register);

		if (rc != 0) {
			/* Something's wrong here */
			temp_register = 0xFFFFFFFF;
			dbg("%s: temp register set to %x by error\n", __FUNCTION__, temp_register);
		}
		/* Preset return code.  It will be changed later if things go okay. */
		rc = NO_ADAPTER_PRESENT;
	}

	/* All F's is an empty slot or an invalid board */
	if (temp_register != 0xFFFFFFFF) {	  /* Check for a board in the slot */
		res_lists.io_head = ctrl->io_head;
		res_lists.mem_head = ctrl->mem_head;
		res_lists.p_mem_head = ctrl->p_mem_head;
		res_lists.bus_head = ctrl->bus_head;
		res_lists.irqs = NULL;

		rc = configure_new_device(ctrl, func, 0, &res_lists, 0, 0);
		dbg("%s: back from configure_new_device\n", __FUNCTION__);

		ctrl->io_head = res_lists.io_head;
		ctrl->mem_head = res_lists.mem_head;
		ctrl->p_mem_head = res_lists.p_mem_head;
		ctrl->bus_head = res_lists.bus_head;

		shpchp_resource_sort_and_combine(&(ctrl->mem_head));
		shpchp_resource_sort_and_combine(&(ctrl->p_mem_head));
		shpchp_resource_sort_and_combine(&(ctrl->io_head));
		shpchp_resource_sort_and_combine(&(ctrl->bus_head));

		if (rc) {
			/* Wait for exclusive access to hardware */
			down(&ctrl->crit_sect);

			/* turn off slot, turn on Amber LED, turn off Green LED */
			retval = p_slot->hpc_ops->slot_disable(p_slot);
			if (retval) {
				err("%s: Issue of Slot Enable command failed\n", __FUNCTION__);
				/* Done with exclusive hardware access */
				up(&ctrl->crit_sect);
				return retval;
			}
			/* Wait for the command to complete */
			wait_for_ctrl_irq (ctrl);

			retval = p_slot->hpc_ops->check_cmd_status(ctrl);
			if (retval) {
				err("%s: Failed to disable slot, error code(%d)\n", __FUNCTION__, retval);
				/* Done with exclusive hardware access */
				up(&ctrl->crit_sect);
				return retval;  
			}

			/* Done with exclusive hardware access */
			up(&ctrl->crit_sect);

			return(rc);
		}
		shpchp_save_slot_config(ctrl, func);

		func->status = 0;
		func->switch_save = 0x10;
		func->is_a_board = 0x01;
		func->pwr_save = 1;

		/* Next, we will instantiate the linux pci_dev structures 
		 * (with appropriate driver notification, if already present) 
		 */
		index = 0;
		do {
			new_func = shpchp_slot_find(ctrl->slot_bus, func->device, index++);
			if (new_func && !new_func->pci_dev) {
				dbg("%s:call pci_hp_configure_dev\n", __FUNCTION__);
				shpchp_configure_device(ctrl, new_func);
			}
		} while (new_func);

		/* Wait for exclusive access to hardware */
		down(&ctrl->crit_sect);

		p_slot->hpc_ops->green_led_on(p_slot);

		/* Wait for the command to complete */
		wait_for_ctrl_irq (ctrl);


		/* Done with exclusive hardware access */
		up(&ctrl->crit_sect);

	} else {
		/* Wait for exclusive access to hardware */
		down(&ctrl->crit_sect);

		/* turn off slot, turn on Amber LED, turn off Green LED */
		rc = p_slot->hpc_ops->slot_disable(p_slot);
		if (rc) {
			err("%s: Issue of Slot Disable command failed\n", __FUNCTION__);
			/* Done with exclusive hardware access */
			up(&ctrl->crit_sect);
			return rc;
		}
		/* Wait for the command to complete */
		wait_for_ctrl_irq (ctrl);

		rc = p_slot->hpc_ops->check_cmd_status(ctrl);
		if (rc) {
			err("%s: Failed to disable slot, error code(%d)\n", __FUNCTION__, rc);
			/* Done with exclusive hardware access */
			up(&ctrl->crit_sect);
			return rc;  
		}

		/* Done with exclusive hardware access */
		up(&ctrl->crit_sect);

		return(rc);
	}
	return 0;
}


/**
 * remove_board - Turns off slot and LED's
 *
 */
static u32 remove_board(struct pci_func *func, struct controller *ctrl)
{
	int index;
	u8 skip = 0;
	u8 device;
	u8 hp_slot;
	u32 rc;
	struct resource_lists res_lists;
	struct pci_func *temp_func;
	struct slot *p_slot;

	if (func == NULL)
		return(1);

	if (shpchp_unconfigure_device(func))
		return(1);

	device = func->device;

	hp_slot = func->device - ctrl->slot_device_offset;
	p_slot = shpchp_find_slot(ctrl, hp_slot + ctrl->slot_device_offset);

	dbg("In %s, hp_slot = %d\n", __FUNCTION__, hp_slot);

	if ((ctrl->add_support) &&
		!(func->bus_head || func->mem_head || func->p_mem_head || func->io_head)) {
		/* Here we check to see if we've saved any of the board's
		 * resources already.  If so, we'll skip the attempt to
		 * determine what's being used.
		 */
		index = 0;

		temp_func = func;

		while ((temp_func = shpchp_slot_find(temp_func->bus, temp_func->device, index++))) {
			if (temp_func->bus_head || temp_func->mem_head
			    || temp_func->p_mem_head || temp_func->io_head) {
				skip = 1;
				break;
			}
		}

		if (!skip)
			rc = shpchp_save_used_resources(ctrl, func, DISABLE_CARD);
	}
	/* Change status to shutdown */
	if (func->is_a_board)
		func->status = 0x01;
	func->configured = 0;

	/* Wait for exclusive access to hardware */
	down(&ctrl->crit_sect);

	/* turn off slot, turn on Amber LED, turn off Green LED */
	rc = p_slot->hpc_ops->slot_disable(p_slot);
	if (rc) {
		err("%s: Issue of Slot Disable command failed\n", __FUNCTION__);
		/* Done with exclusive hardware access */
		up(&ctrl->crit_sect);
		return rc;
	}
	/* Wait for the command to complete */
	wait_for_ctrl_irq (ctrl);

	rc = p_slot->hpc_ops->check_cmd_status(ctrl);
	if (rc) {
		err("%s: Failed to disable slot, error code(%d)\n", __FUNCTION__, rc);
		/* Done with exclusive hardware access */
		up(&ctrl->crit_sect);
		return rc;  
	}
	
	rc = p_slot->hpc_ops->set_attention_status(p_slot, 0);
	if (rc) {
		err("%s: Issue of Set Attention command failed\n", __FUNCTION__);
		/* Done with exclusive hardware access */
		up(&ctrl->crit_sect);
		return rc;
	}
	/* Wait for the command to complete */
	wait_for_ctrl_irq (ctrl);

	/* Done with exclusive hardware access */
	up(&ctrl->crit_sect);

	if (ctrl->add_support) {
		while (func) {
			res_lists.io_head = ctrl->io_head;
			res_lists.mem_head = ctrl->mem_head;
			res_lists.p_mem_head = ctrl->p_mem_head;
			res_lists.bus_head = ctrl->bus_head;

			dbg("Returning resources to ctlr lists for (B/D/F) = (%#x/%#x/%#x)\n", func->bus, 
				func->device, func->function);

			shpchp_return_board_resources(func, &res_lists);

			ctrl->io_head = res_lists.io_head;
			ctrl->mem_head = res_lists.mem_head;
			ctrl->p_mem_head = res_lists.p_mem_head;
			ctrl->bus_head = res_lists.bus_head;

			shpchp_resource_sort_and_combine(&(ctrl->mem_head));
			shpchp_resource_sort_and_combine(&(ctrl->p_mem_head));
			shpchp_resource_sort_and_combine(&(ctrl->io_head));
			shpchp_resource_sort_and_combine(&(ctrl->bus_head));

			if (is_bridge(func)) {
				dbg("PCI Bridge Hot-Remove s:b:d:f(%02x:%02x:%02x:%02x)\n", ctrl->seg, func->bus, 
					func->device, func->function);
				bridge_slot_remove(func);
			} else
				dbg("PCI Function Hot-Remove s:b:d:f(%02x:%02x:%02x:%02x)\n", ctrl->seg, func->bus, 
					func->device, func->function);
				slot_remove(func);

			func = shpchp_slot_find(ctrl->slot_bus, device, 0);
		}

		/* Setup slot structure with entry for empty slot */
		func = shpchp_slot_create(ctrl->slot_bus);

		if (func == NULL) {
			return(1);
		}

		func->bus = ctrl->slot_bus;
		func->device = device;
		func->function = 0;
		func->configured = 0;
		func->switch_save = 0x10;
		func->pwr_save = 0;
		func->is_a_board = 0;
	}

	return 0;
}


static void pushbutton_helper_thread (unsigned long data)
{
	pushbutton_pending = data;

	up(&event_semaphore);
}


/**
 * shpchp_pushbutton_thread
 *
 * Scheduled procedure to handle blocking stuff for the pushbuttons
 * Handles all pending events and exits.
 *
 */
static void shpchp_pushbutton_thread (unsigned long slot)
{
	struct slot *p_slot = (struct slot *) slot;
	u8 getstatus;
	
	pushbutton_pending = 0;

	if (!p_slot) {
		dbg("%s: Error! slot NULL\n", __FUNCTION__);
		return;
	}

	p_slot->hpc_ops->get_power_status(p_slot, &getstatus);
	if (getstatus) {
		p_slot->state = POWEROFF_STATE;
		dbg("In power_down_board, b:d(%x:%x)\n", p_slot->bus, p_slot->device);

		shpchp_disable_slot(p_slot);
		p_slot->state = STATIC_STATE;
	} else {
		p_slot->state = POWERON_STATE;
		dbg("In add_board, b:d(%x:%x)\n", p_slot->bus, p_slot->device);

		if (shpchp_enable_slot(p_slot)) {
			/* Wait for exclusive access to hardware */
			down(&p_slot->ctrl->crit_sect);

			p_slot->hpc_ops->green_led_off(p_slot);

			/* Wait for the command to complete */
			wait_for_ctrl_irq (p_slot->ctrl);

			/* Done with exclusive hardware access */
			up(&p_slot->ctrl->crit_sect);
		}
		p_slot->state = STATIC_STATE;
	}

	return;
}


/* this is the main worker thread */
static int event_thread(void* data)
{
	struct controller *ctrl;
	lock_kernel();
	daemonize("shpchpd_event");
	unlock_kernel();

	while (1) {
		dbg("!!!!event_thread sleeping\n");
		down_interruptible (&event_semaphore);
		dbg("event_thread woken finished = %d\n", event_finished);
		if (event_finished || signal_pending(current))
			break;
		/* Do stuff here */
		if (pushbutton_pending)
			shpchp_pushbutton_thread(pushbutton_pending);
		else
			for (ctrl = shpchp_ctrl_list; ctrl; ctrl=ctrl->next)
				interrupt_event_handler(ctrl);
	}
	dbg("event_thread signals exit\n");
	up(&event_exit);
	return 0;
}

int shpchp_event_start_thread (void)
{
	int pid;

	/* initialize our semaphores */
	init_MUTEX_LOCKED(&event_exit);
	event_finished=0;

	init_MUTEX_LOCKED(&event_semaphore);
	pid = kernel_thread(event_thread, NULL, 0);

	if (pid < 0) {
		err ("Can't start up our event thread\n");
		return -1;
	}
	dbg("Our event thread pid = %d\n", pid);
	return 0;
}


void shpchp_event_stop_thread (void)
{
	event_finished = 1;
	dbg("event_thread finish command given\n");
	up(&event_semaphore);
	dbg("wait for event_thread to exit\n");
	down(&event_exit);
}


static int update_slot_info (struct slot *slot)
{
	struct hotplug_slot_info *info;
	int result;

	info = kmalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	slot->hpc_ops->get_power_status(slot, &(info->power_status));
	slot->hpc_ops->get_attention_status(slot, &(info->attention_status));
	slot->hpc_ops->get_latch_status(slot, &(info->latch_status));
	slot->hpc_ops->get_adapter_status(slot, &(info->adapter_status));

	result = pci_hp_change_slot_info(slot->hotplug_slot, info);
	kfree (info);
	return result;
}

static void interrupt_event_handler(struct controller *ctrl)
{
	int loop = 0;
	int change = 1;
	struct pci_func *func;
	u8 hp_slot;
	u8 getstatus;
	struct slot *p_slot;

	dbg("%s:\n", __FUNCTION__);
	while (change) {
		change = 0;

		for (loop = 0; loop < 10; loop++) {
			if (ctrl->event_queue[loop].event_type != 0) {
				dbg("%s:loop %x event_type %x\n", __FUNCTION__, loop, 
					ctrl->event_queue[loop].event_type);
				hp_slot = ctrl->event_queue[loop].hp_slot;

				func = shpchp_slot_find(ctrl->slot_bus, (hp_slot + ctrl->slot_device_offset), 0);

				p_slot = shpchp_find_slot(ctrl, hp_slot + ctrl->slot_device_offset);

				dbg("%s: hp_slot %d, func %p, p_slot %p\n", __FUNCTION__, hp_slot, func, p_slot);

				if (ctrl->event_queue[loop].event_type == INT_BUTTON_CANCEL) {
					dbg("%s: button cancel\n", __FUNCTION__);
					del_timer(&p_slot->task_event);

					switch (p_slot->state) {
					case BLINKINGOFF_STATE:
						/* Wait for exclusive access to hardware */
						down(&ctrl->crit_sect);

						p_slot->hpc_ops->green_led_on(p_slot);
						/* Wait for the command to complete */
						wait_for_ctrl_irq (ctrl);

						p_slot->hpc_ops->set_attention_status(p_slot, 0);

						/* Wait for the command to complete */
						wait_for_ctrl_irq (ctrl);

						/* Done with exclusive hardware access */
						up(&ctrl->crit_sect);
						break;
					case BLINKINGON_STATE:
						/* Wait for exclusive access to hardware */
						down(&ctrl->crit_sect);

						p_slot->hpc_ops->green_led_off(p_slot);
						/* Wait for the command to complete */
						wait_for_ctrl_irq (ctrl);

						p_slot->hpc_ops->set_attention_status(p_slot, 0);
						/* Wait for the command to complete */
						wait_for_ctrl_irq (ctrl);

						/* Done with exclusive hardware access */
						up(&ctrl->crit_sect);

						break;
					default:
						warn("Not a valid state\n");
						return;
					}
					info(msg_button_cancel, p_slot->number);
					p_slot->state = STATIC_STATE;
				} else if (ctrl->event_queue[loop].event_type == INT_BUTTON_PRESS) {
					/* Button Pressed (No action on 1st press...) */
					dbg("%s: Button pressed\n", __FUNCTION__);

					p_slot->hpc_ops->get_power_status(p_slot, &getstatus);
					if (getstatus) {
						/* slot is on */
						dbg("%s: slot is on\n", __FUNCTION__);
						p_slot->state = BLINKINGOFF_STATE;
						info(msg_button_off, p_slot->number);
					} else {
						/* slot is off */
						dbg("%s: slot is off\n", __FUNCTION__);
						p_slot->state = BLINKINGON_STATE;
						info(msg_button_on, p_slot->number);
					}

					/* Wait for exclusive access to hardware */
					down(&ctrl->crit_sect);

					/* blink green LED and turn off amber */
					p_slot->hpc_ops->green_led_blink(p_slot);
					/* Wait for the command to complete */
					wait_for_ctrl_irq (ctrl);
					
					p_slot->hpc_ops->set_attention_status(p_slot, 0);

					/* Wait for the command to complete */
					wait_for_ctrl_irq (ctrl);

					/* Done with exclusive hardware access */
					up(&ctrl->crit_sect);

					init_timer(&p_slot->task_event);
					p_slot->task_event.expires = jiffies + 5 * HZ;   /* 5 second delay */
					p_slot->task_event.function = (void (*)(unsigned long)) pushbutton_helper_thread;
					p_slot->task_event.data = (unsigned long) p_slot;

					dbg("%s: add_timer p_slot = %p\n", __FUNCTION__,(void *) p_slot);
					add_timer(&p_slot->task_event);
				} else if (ctrl->event_queue[loop].event_type == INT_POWER_FAULT) {
					/***********POWER FAULT********************/
					dbg("%s: power fault\n", __FUNCTION__);
					/* Wait for exclusive access to hardware */
					down(&ctrl->crit_sect);

					p_slot->hpc_ops->set_attention_status(p_slot, 1);
					/* Wait for the command to complete */
					wait_for_ctrl_irq (ctrl);
					
					p_slot->hpc_ops->green_led_off(p_slot);
					/* Wait for the command to complete */
					wait_for_ctrl_irq (ctrl);

					/* Done with exclusive hardware access */
					up(&ctrl->crit_sect);
				} else {
					/* refresh notification */
					if (p_slot)
						update_slot_info(p_slot);
				}

				ctrl->event_queue[loop].event_type = 0;

				change = 1;
			}
		}		/* End of FOR loop */
	}

	return;
}


int shpchp_enable_slot (struct slot *p_slot)
{
	u8 getstatus = 0;
	int rc;
	struct pci_func *func;

	func = shpchp_slot_find(p_slot->bus, p_slot->device, 0);
	if (!func) {
		dbg("%s: Error! slot NULL\n", __FUNCTION__);
		return 1;
	}

	/* Check to see if (latch closed, card present, power off) */
	down(&p_slot->ctrl->crit_sect);
	rc = p_slot->hpc_ops->get_adapter_status(p_slot, &getstatus);
	if (rc || !getstatus) {
		info("%s: no adapter on slot(%x)\n", __FUNCTION__, p_slot->number);
		up(&p_slot->ctrl->crit_sect);
		return 1;
	}
	rc = p_slot->hpc_ops->get_latch_status(p_slot, &getstatus);
	if (rc || getstatus) {
		info("%s: latch open on slot(%x)\n", __FUNCTION__, p_slot->number);
		up(&p_slot->ctrl->crit_sect);
		return 1;
	}
	rc = p_slot->hpc_ops->get_power_status(p_slot, &getstatus);
	if (rc || getstatus) {
		info("%s: already enabled on slot(%x)\n", __FUNCTION__, p_slot->number);
		up(&p_slot->ctrl->crit_sect);
		return 1;
	}
	up(&p_slot->ctrl->crit_sect);

	slot_remove(func);

	func = shpchp_slot_create(p_slot->bus);
	if (func == NULL)
		return 1;

	func->bus = p_slot->bus;
	func->device = p_slot->device;
	func->function = 0;
	func->configured = 0;
	func->is_a_board = 1;

	/* We have to save the presence info for these slots */
	p_slot->hpc_ops->get_adapter_status(p_slot, &(func->presence_save));
	p_slot->hpc_ops->get_power_status(p_slot, &(func->pwr_save));
	dbg("%s: func->pwr_save %x\n", __FUNCTION__, func->pwr_save);
	p_slot->hpc_ops->get_latch_status(p_slot, &getstatus);
	func->switch_save = !getstatus? 0x10:0;

	rc = board_added(func, p_slot->ctrl);
	if (rc) {
		if (is_bridge(func))
			bridge_slot_remove(func);
		else
			slot_remove(func);

		/* Setup slot structure with entry for empty slot */
		func = shpchp_slot_create(p_slot->bus);
		if (func == NULL)
			return (1);	/* Out of memory */

		func->bus = p_slot->bus;
		func->device = p_slot->device;
		func->function = 0;
		func->configured = 0;
		func->is_a_board = 1;

		/* We have to save the presence info for these slots */
		p_slot->hpc_ops->get_adapter_status(p_slot, &(func->presence_save));
		p_slot->hpc_ops->get_latch_status(p_slot, &getstatus);
		func->switch_save = !getstatus? 0x10:0;
	}

	if (p_slot)
		update_slot_info(p_slot);

	return rc;
}


int shpchp_disable_slot (struct slot *p_slot)
{
	u8 class_code, header_type, BCR;
	u8 index = 0;
	u8 getstatus = 0;
	u32 rc = 0;
	int ret = 0;
	unsigned int devfn;
	struct pci_bus *pci_bus;
	struct pci_func *func;

	if (!p_slot->ctrl)
		return 1;

	pci_bus = p_slot->ctrl->pci_dev->subordinate;

	/* Check to see if (latch closed, card present, power on) */
	down(&p_slot->ctrl->crit_sect);

	ret = p_slot->hpc_ops->get_adapter_status(p_slot, &getstatus);
	if (ret || !getstatus) {
		info("%s: no adapter on slot(%x)\n", __FUNCTION__, p_slot->number);
		up(&p_slot->ctrl->crit_sect);
		return 1;
	}
	ret = p_slot->hpc_ops->get_latch_status(p_slot, &getstatus);
	if (ret || getstatus) {
		info("%s: latch open on slot(%x)\n", __FUNCTION__, p_slot->number);
		up(&p_slot->ctrl->crit_sect);
		return 1;
	}
	ret = p_slot->hpc_ops->get_power_status(p_slot, &getstatus);
	if (ret || !getstatus) {
		info("%s: already disabled slot(%x)\n", __FUNCTION__, p_slot->number);
		up(&p_slot->ctrl->crit_sect);
		return 1;
	}
	up(&p_slot->ctrl->crit_sect);

	func = shpchp_slot_find(p_slot->bus, p_slot->device, index++);

	/* Make sure there are no video controllers here
	 * for all func of p_slot
	 */
	while (func && !rc) {
		pci_bus->number = func->bus;
		devfn = PCI_DEVFN(func->device, func->function);

		/* Check the Class Code */
		rc = pci_bus_read_config_byte (pci_bus, devfn, 0x0B, &class_code);
		if (rc)
			return rc;

		if (class_code == PCI_BASE_CLASS_DISPLAY) {
			/* Display/Video adapter (not supported) */
			rc = REMOVE_NOT_SUPPORTED;
		} else {
			/* See if it's a bridge */
			rc = pci_bus_read_config_byte (pci_bus, devfn, PCI_HEADER_TYPE, &header_type);
			if (rc)
				return rc;

			/* If it's a bridge, check the VGA Enable bit */
			if ((header_type & 0x7F) == PCI_HEADER_TYPE_BRIDGE) {
				rc = pci_bus_read_config_byte (pci_bus, devfn, PCI_BRIDGE_CONTROL, &BCR);
				if (rc)
					return rc;

				/* If the VGA Enable bit is set, remove isn't supported */
				if (BCR & PCI_BRIDGE_CTL_VGA) {
					rc = REMOVE_NOT_SUPPORTED;
				}
			}
		}

		func = shpchp_slot_find(p_slot->bus, p_slot->device, index++);
	}

	func = shpchp_slot_find(p_slot->bus, p_slot->device, 0);
	if ((func != NULL) && !rc) {
		rc = remove_board(func, p_slot->ctrl);
	} else if (!rc)
		rc = 1;

	if (p_slot)
		update_slot_info(p_slot);

	return(rc);
}


/**
 * configure_new_device - Configures the PCI header information of one board.
 *
 * @ctrl: pointer to controller structure
 * @func: pointer to function structure
 * @behind_bridge: 1 if this is a recursive call, 0 if not
 * @resources: pointer to set of resource lists
 *
 * Returns 0 if success
 *
 */
static u32 configure_new_device (struct controller * ctrl, struct pci_func * func,
	u8 behind_bridge, struct resource_lists * resources, u8 bridge_bus, u8 bridge_dev)
{
	u8 temp_byte, function, max_functions, stop_it;
	int rc;
	u32 ID;
	struct pci_func *new_slot;
	struct pci_bus lpci_bus, *pci_bus;
	int index;

	new_slot = func;

	dbg("%s\n", __FUNCTION__);
	memcpy(&lpci_bus, ctrl->pci_dev->subordinate, sizeof(lpci_bus));
	pci_bus = &lpci_bus;
	pci_bus->number = func->bus;

	/* Check for Multi-function device */
	rc = pci_bus_read_config_byte(pci_bus, PCI_DEVFN(func->device, func->function), 0x0E, &temp_byte);
	if (rc) {
		dbg("%s: rc = %d\n", __FUNCTION__, rc);
		return rc;
	}

	if (temp_byte & 0x80)	/* Multi-function device */
		max_functions = 8;
	else
		max_functions = 1;

	function = 0;

	do {
		rc = configure_new_function(ctrl, new_slot, behind_bridge, resources, bridge_bus, bridge_dev);

		if (rc) {
			dbg("configure_new_function failed %d\n",rc);
			index = 0;

			while (new_slot) {
				new_slot = shpchp_slot_find(new_slot->bus, new_slot->device, index++);

				if (new_slot)
					shpchp_return_board_resources(new_slot, resources);
			}

			return(rc);
		}

		function++;

		stop_it = 0;

		/*  The following loop skips to the next present function
		 *  and creates a board structure
		 */

		while ((function < max_functions) && (!stop_it)) {
			pci_bus_read_config_dword(pci_bus, PCI_DEVFN(func->device, function), 0x00, &ID);

			if (ID == 0xFFFFFFFF) {	  /* There's nothing there. */
				function++;
			} else {  /* There's something there */
				/* Setup slot structure. */
				new_slot = shpchp_slot_create(func->bus);

				if (new_slot == NULL) {
					/* Out of memory */
					return(1);
				}

				new_slot->bus = func->bus;
				new_slot->device = func->device;
				new_slot->function = function;
				new_slot->is_a_board = 1;
				new_slot->status = 0;

				stop_it++;
			}
		}

	} while (function < max_functions);
	dbg("returning from configure_new_device\n");

	return 0;
}


/*
 * Configuration logic that involves the hotplug data structures and 
 * their bookkeeping
 */


/**
 * configure_new_function - Configures the PCI header information of one device
 *
 * @ctrl: pointer to controller structure
 * @func: pointer to function structure
 * @behind_bridge: 1 if this is a recursive call, 0 if not
 * @resources: pointer to set of resource lists
 *
 * Calls itself recursively for bridged devices.
 * Returns 0 if success
 *
 */
static int configure_new_function (struct controller * ctrl, struct pci_func * func,
	u8 behind_bridge, struct resource_lists *resources, u8 bridge_bus, u8 bridge_dev)
{
	int cloop;
	u8 temp_byte;
	u8 device;
	u8 class_code;
	u16 temp_word;
	u32 rc;
	u32 temp_register;
	u32 base;
	u32 ID;
	unsigned int devfn;
	struct pci_resource *mem_node;
	struct pci_resource *p_mem_node;
	struct pci_resource *io_node;
	struct pci_resource *bus_node;
	struct pci_resource *hold_mem_node;
	struct pci_resource *hold_p_mem_node;
	struct pci_resource *hold_IO_node;
	struct pci_resource *hold_bus_node;
	struct irq_mapping irqs;
	struct pci_func *new_slot;
	struct pci_bus lpci_bus, *pci_bus;
	struct resource_lists temp_resources;
#if defined(CONFIG_X86_64)
	u8 IRQ=0;
#endif

	memcpy(&lpci_bus, ctrl->pci_dev->subordinate, sizeof(lpci_bus));
	pci_bus = &lpci_bus;
	pci_bus->number = func->bus;
	devfn = PCI_DEVFN(func->device, func->function);

	/* Check for Bridge */
	rc = pci_bus_read_config_byte (pci_bus, devfn, PCI_HEADER_TYPE, &temp_byte);
	if (rc)
		return rc;

	if ((temp_byte & 0x7F) == PCI_HEADER_TYPE_BRIDGE) { /* PCI-PCI Bridge */
		/* set Primary bus */
		dbg("set Primary bus = 0x%x\n", func->bus);
		rc = pci_bus_write_config_byte(pci_bus, devfn, PCI_PRIMARY_BUS, func->bus);
		if (rc)
			return rc;

		/* find range of busses to use */
		bus_node = get_max_resource(&resources->bus_head, 1L);

		/* If we don't have any busses to allocate, we can't continue */
		if (!bus_node) {
			err("Got NO bus resource to use\n");
			return -ENOMEM;
		}
		dbg("Got ranges of buses to use: base:len=0x%x:%x\n", bus_node->base, bus_node->length);

		/* set Secondary bus */
		temp_byte = (u8)bus_node->base;
		dbg("set Secondary bus = 0x%x\n", temp_byte);
		rc = pci_bus_write_config_byte(pci_bus, devfn, PCI_SECONDARY_BUS, temp_byte);
		if (rc)
			return rc;

		/* set subordinate bus */
		temp_byte = (u8)(bus_node->base + bus_node->length - 1);
		dbg("set subordinate bus = 0x%x\n", temp_byte);
		rc = pci_bus_write_config_byte (pci_bus, devfn, PCI_SUBORDINATE_BUS, temp_byte);
		if (rc)
			return rc;

		/* Set HP parameters (Cache Line Size, Latency Timer) */
		rc = shpchprm_set_hpp(ctrl, func, PCI_HEADER_TYPE_BRIDGE);
		if (rc)
			return rc;

		/* Setup the IO, memory, and prefetchable windows */

		io_node = get_max_resource(&(resources->io_head), 0x1000L);
		if (io_node) {
			dbg("io_node(base, len, next) (%x, %x, %p)\n", io_node->base, io_node->length, io_node->next);
		}

		mem_node = get_max_resource(&(resources->mem_head), 0x100000L);
		if (mem_node) {
			dbg("mem_node(base, len, next) (%x, %x, %p)\n", mem_node->base, mem_node->length, mem_node->next);
		}

		if (resources->p_mem_head)
			p_mem_node = get_max_resource(&(resources->p_mem_head), 0x100000L);
		else {
			/*
			 * In some platform implementation, MEM and PMEM are not
			 *  distinguished, and hence ACPI _CRS has only MEM entries
			 *  for both MEM and PMEM.
			 */
			dbg("using MEM for PMEM\n");
			p_mem_node = get_max_resource(&(resources->mem_head), 0x100000L);
		}
		if (p_mem_node) {
			dbg("p_mem_node(base, len, next) (%x, %x, %p)\n", p_mem_node->base, p_mem_node->length, p_mem_node->next);
		}

		/* set up the IRQ info */
		if (!resources->irqs) {
			irqs.barber_pole = 0;
			irqs.interrupt[0] = 0;
			irqs.interrupt[1] = 0;
			irqs.interrupt[2] = 0;
			irqs.interrupt[3] = 0;
			irqs.valid_INT = 0;
		} else {
			irqs.barber_pole = resources->irqs->barber_pole;
			irqs.interrupt[0] = resources->irqs->interrupt[0];
			irqs.interrupt[1] = resources->irqs->interrupt[1];
			irqs.interrupt[2] = resources->irqs->interrupt[2];
			irqs.interrupt[3] = resources->irqs->interrupt[3];
			irqs.valid_INT = resources->irqs->valid_INT;
		}

		/* set up resource lists that are now aligned on top and bottom
		 * for anything behind the bridge.
		 */
		temp_resources.bus_head = bus_node;
		temp_resources.io_head = io_node;
		temp_resources.mem_head = mem_node;
		temp_resources.p_mem_head = p_mem_node;
		temp_resources.irqs = &irqs;

		/* Make copies of the nodes we are going to pass down so that
		 * if there is a problem,we can just use these to free resources
		 */
		hold_bus_node = kmalloc(sizeof(*hold_bus_node), GFP_KERNEL);
		hold_IO_node = kmalloc(sizeof(*hold_IO_node), GFP_KERNEL);
		hold_mem_node = kmalloc(sizeof(*hold_mem_node), GFP_KERNEL);
		hold_p_mem_node = kmalloc(sizeof(*hold_p_mem_node), GFP_KERNEL);

		if (!hold_bus_node || !hold_IO_node || !hold_mem_node || !hold_p_mem_node) {
			kfree(hold_bus_node);
			kfree(hold_IO_node);
			kfree(hold_mem_node);
			kfree(hold_p_mem_node);

			return 1;
		}

		memcpy(hold_bus_node, bus_node, sizeof(struct pci_resource));

		bus_node->base += 1;
		bus_node->length -= 1;
		bus_node->next = NULL;

		/* If we have IO resources copy them and fill in the bridge's
		 * IO range registers
		 */
		if (io_node) {
			memcpy(hold_IO_node, io_node, sizeof(struct pci_resource));
			io_node->next = NULL;

			/* set IO base and Limit registers */
			RES_CHECK(io_node->base, 8);
			temp_byte = (u8)(io_node->base >> 8);
			rc = pci_bus_write_config_byte(pci_bus, devfn, PCI_IO_BASE, temp_byte);

			RES_CHECK(io_node->base + io_node->length - 1, 8);
			temp_byte = (u8)((io_node->base + io_node->length - 1) >> 8);
			rc = pci_bus_write_config_byte(pci_bus, devfn, PCI_IO_LIMIT, temp_byte);
		} else {
			kfree(hold_IO_node);
			hold_IO_node = NULL;
		}

		/* If we have memory resources copy them and fill in the bridge's
		 * memory range registers.  Otherwise, fill in the range
		 * registers with values that disable them.
		 */
		if (mem_node) {
			memcpy(hold_mem_node, mem_node, sizeof(struct pci_resource));
			mem_node->next = NULL;

			/* set Mem base and Limit registers */
			RES_CHECK(mem_node->base, 16);
			temp_word = (u32)(mem_node->base >> 16);
			rc = pci_bus_write_config_word(pci_bus, devfn, PCI_MEMORY_BASE, temp_word);

			RES_CHECK(mem_node->base + mem_node->length - 1, 16);
			temp_word = (u32)((mem_node->base + mem_node->length - 1) >> 16);
			rc = pci_bus_write_config_word(pci_bus, devfn, PCI_MEMORY_LIMIT, temp_word);
		} else {
			temp_word = 0xFFFF;
			rc = pci_bus_write_config_word(pci_bus, devfn, PCI_MEMORY_BASE, temp_word);

			temp_word = 0x0000;
			rc = pci_bus_write_config_word(pci_bus, devfn, PCI_MEMORY_LIMIT, temp_word);

			kfree(hold_mem_node);
			hold_mem_node = NULL;
		}

		/* If we have prefetchable memory resources copy them and 
		 * fill in the bridge's memory range registers.  Otherwise,
		 * fill in the range registers with values that disable them.
		 */
		if (p_mem_node) {
			memcpy(hold_p_mem_node, p_mem_node, sizeof(struct pci_resource));
			p_mem_node->next = NULL;

			/* set Pre Mem base and Limit registers */
			RES_CHECK(p_mem_node->base, 16);
			temp_word = (u32)(p_mem_node->base >> 16);
			rc = pci_bus_write_config_word(pci_bus, devfn, PCI_PREF_MEMORY_BASE, temp_word);

			RES_CHECK(p_mem_node->base + p_mem_node->length - 1, 16);
			temp_word = (u32)((p_mem_node->base + p_mem_node->length - 1) >> 16);
			rc = pci_bus_write_config_word(pci_bus, devfn, PCI_PREF_MEMORY_LIMIT, temp_word);
		} else {
			temp_word = 0xFFFF;
			rc = pci_bus_write_config_word(pci_bus, devfn, PCI_PREF_MEMORY_BASE, temp_word);

			temp_word = 0x0000;
			rc = pci_bus_write_config_word(pci_bus, devfn, PCI_PREF_MEMORY_LIMIT, temp_word);

			kfree(hold_p_mem_node);
			hold_p_mem_node = NULL;
		}

		/* Adjust this to compensate for extra adjustment in first loop */
		irqs.barber_pole--;

		rc = 0;

		/* Here we actually find the devices and configure them */
		for (device = 0; (device <= 0x1F) && !rc; device++) {
			irqs.barber_pole = (irqs.barber_pole + 1) & 0x03;

			ID = 0xFFFFFFFF;
			pci_bus->number = hold_bus_node->base;
			pci_bus_read_config_dword(pci_bus, PCI_DEVFN(device, 0),
					PCI_VENDOR_ID, &ID);
			pci_bus->number = func->bus;

			if (ID != 0xFFFFFFFF) {	  /*  device Present */
				/* Setup slot structure. */
				new_slot = shpchp_slot_create(hold_bus_node->base);

				if (new_slot == NULL) {
					/* Out of memory */
					rc = -ENOMEM;
					continue;
				}

				new_slot->bus = hold_bus_node->base;
				new_slot->device = device;
				new_slot->function = 0;
				new_slot->is_a_board = 1;
				new_slot->status = 0;

				rc = configure_new_device(ctrl, new_slot, 1, &temp_resources, func->bus, func->device);
				dbg("configure_new_device rc=0x%x\n",rc);
			}	/* End of IF (device in slot?) */
		}		/* End of FOR loop */

		if (rc) {
			shpchp_destroy_resource_list(&temp_resources);

			return_resource(&(resources->bus_head), hold_bus_node);
			return_resource(&(resources->io_head), hold_IO_node);
			return_resource(&(resources->mem_head), hold_mem_node);
			return_resource(&(resources->p_mem_head), hold_p_mem_node);
			return(rc);
		}

		/* save the interrupt routing information */
		if (resources->irqs) {
			resources->irqs->interrupt[0] = irqs.interrupt[0];
			resources->irqs->interrupt[1] = irqs.interrupt[1];
			resources->irqs->interrupt[2] = irqs.interrupt[2];
			resources->irqs->interrupt[3] = irqs.interrupt[3];
			resources->irqs->valid_INT = irqs.valid_INT;
		} else if (!behind_bridge) {
			/* We need to hook up the interrupts here */
			for (cloop = 0; cloop < 4; cloop++) {
				if (irqs.valid_INT & (0x01 << cloop)) {
					rc = shpchp_set_irq(func->bus, func->device,
							   0x0A + cloop, irqs.interrupt[cloop]);
					if (rc) {
						shpchp_destroy_resource_list (&temp_resources);
						return_resource(&(resources->bus_head), hold_bus_node);
						return_resource(&(resources->io_head), hold_IO_node);
						return_resource(&(resources->mem_head), hold_mem_node);
						return_resource(&(resources->p_mem_head), hold_p_mem_node);
						return rc;
					}
				}
			}	/* end of for loop */
		}

		/* Return unused bus resources
		 * First use the temporary node to store information for the board
		 */
		if (hold_bus_node && bus_node && temp_resources.bus_head) {
			hold_bus_node->length = bus_node->base - hold_bus_node->base;

			hold_bus_node->next = func->bus_head;
			func->bus_head = hold_bus_node;

			temp_byte = (u8)(temp_resources.bus_head->base - 1);

			/* set subordinate bus */
			dbg("re-set subordinate bus = 0x%x\n", temp_byte);
			rc = pci_bus_write_config_byte (pci_bus, devfn, PCI_SUBORDINATE_BUS, temp_byte);

			if (temp_resources.bus_head->length == 0) {
				kfree(temp_resources.bus_head);
				temp_resources.bus_head = NULL;
			} else {
				dbg("return bus res of b:d(0x%x:%x) base:len(0x%x:%x)\n",
					func->bus, func->device, temp_resources.bus_head->base, temp_resources.bus_head->length);
				return_resource(&(resources->bus_head), temp_resources.bus_head);
			}
		}

		/* If we have IO space available and there is some left,
		 * return the unused portion
		 */
		if (hold_IO_node && temp_resources.io_head) {
			io_node = do_pre_bridge_resource_split(&(temp_resources.io_head),
							       &hold_IO_node, 0x1000);

			/* Check if we were able to split something off */
			if (io_node) {
				hold_IO_node->base = io_node->base + io_node->length;

				RES_CHECK(hold_IO_node->base, 8);
				temp_byte = (u8)((hold_IO_node->base) >> 8);
				rc = pci_bus_write_config_byte (pci_bus, devfn, PCI_IO_BASE, temp_byte);

				return_resource(&(resources->io_head), io_node);
			}

			io_node = do_bridge_resource_split(&(temp_resources.io_head), 0x1000);

			/*  Check if we were able to split something off */
			if (io_node) {
				/* First use the temporary node to store information for the board */
				hold_IO_node->length = io_node->base - hold_IO_node->base;

				/* If we used any, add it to the board's list */
				if (hold_IO_node->length) {
					hold_IO_node->next = func->io_head;
					func->io_head = hold_IO_node;

					RES_CHECK(io_node->base - 1, 8);
					temp_byte = (u8)((io_node->base - 1) >> 8);
					rc = pci_bus_write_config_byte (pci_bus, devfn, PCI_IO_LIMIT, temp_byte);

					return_resource(&(resources->io_head), io_node);
				} else {
					/* it doesn't need any IO */
					temp_byte = 0x00;
					rc = pci_bus_write_config_byte(pci_bus, devfn, PCI_IO_LIMIT, temp_byte);

					return_resource(&(resources->io_head), io_node);
					kfree(hold_IO_node);
				}
			} else {
				/* it used most of the range */
				hold_IO_node->next = func->io_head;
				func->io_head = hold_IO_node;
			}
		} else if (hold_IO_node) {
			/* it used the whole range */
			hold_IO_node->next = func->io_head;
			func->io_head = hold_IO_node;
		}

		/* If we have memory space available and there is some left,
		 * return the unused portion
		 */
		if (hold_mem_node && temp_resources.mem_head) {
			mem_node = do_pre_bridge_resource_split(&(temp_resources.mem_head), &hold_mem_node, 0x100000L);

			/* Check if we were able to split something off */
			if (mem_node) {
				hold_mem_node->base = mem_node->base + mem_node->length;

				RES_CHECK(hold_mem_node->base, 16);
				temp_word = (u32)((hold_mem_node->base) >> 16);
				rc = pci_bus_write_config_word (pci_bus, devfn, PCI_MEMORY_BASE, temp_word);

				return_resource(&(resources->mem_head), mem_node);
			}

			mem_node = do_bridge_resource_split(&(temp_resources.mem_head), 0x100000L);

			/* Check if we were able to split something off */
			if (mem_node) {
				/* First use the temporary node to store information for the board */
				hold_mem_node->length = mem_node->base - hold_mem_node->base;

				if (hold_mem_node->length) {
					hold_mem_node->next = func->mem_head;
					func->mem_head = hold_mem_node;

					/* configure end address */
					RES_CHECK(mem_node->base - 1, 16);
					temp_word = (u32)((mem_node->base - 1) >> 16);
					rc = pci_bus_write_config_word (pci_bus, devfn, PCI_MEMORY_LIMIT, temp_word);

					/* Return unused resources to the pool */
					return_resource(&(resources->mem_head), mem_node);
				} else {
					/* it doesn't need any Mem */
					temp_word = 0x0000;
					rc = pci_bus_write_config_word (pci_bus, devfn, PCI_MEMORY_LIMIT, temp_word);

					return_resource(&(resources->mem_head), mem_node);
					kfree(hold_mem_node);
				}
			} else {
				/* it used most of the range */
				hold_mem_node->next = func->mem_head;
				func->mem_head = hold_mem_node;
			}
		} else if (hold_mem_node) {
			/* it used the whole range */
			hold_mem_node->next = func->mem_head;
			func->mem_head = hold_mem_node;
		}

		/* If we have prefetchable memory space available and there is some 
		 * left at the end, return the unused portion
		 */
		if (hold_p_mem_node && temp_resources.p_mem_head) {
			p_mem_node = do_pre_bridge_resource_split(&(temp_resources.p_mem_head),
								  &hold_p_mem_node, 0x100000L);

			/* Check if we were able to split something off */
			if (p_mem_node) {
				hold_p_mem_node->base = p_mem_node->base + p_mem_node->length;

				RES_CHECK(hold_p_mem_node->base, 16);
				temp_word = (u32)((hold_p_mem_node->base) >> 16);
				rc = pci_bus_write_config_word (pci_bus, devfn, PCI_PREF_MEMORY_BASE, temp_word);

				return_resource(&(resources->p_mem_head), p_mem_node);
			}

			p_mem_node = do_bridge_resource_split(&(temp_resources.p_mem_head), 0x100000L);

			/* Check if we were able to split something off */
			if (p_mem_node) {
				/* First use the temporary node to store information for the board */
				hold_p_mem_node->length = p_mem_node->base - hold_p_mem_node->base;

				/* If we used any, add it to the board's list */
				if (hold_p_mem_node->length) {
					hold_p_mem_node->next = func->p_mem_head;
					func->p_mem_head = hold_p_mem_node;

					RES_CHECK(p_mem_node->base - 1, 16);
					temp_word = (u32)((p_mem_node->base - 1) >> 16);
					rc = pci_bus_write_config_word (pci_bus, devfn, PCI_PREF_MEMORY_LIMIT, temp_word);

					return_resource(&(resources->p_mem_head), p_mem_node);
				} else {
					/* it doesn't need any PMem */
					temp_word = 0x0000;
					rc = pci_bus_write_config_word (pci_bus, devfn, PCI_PREF_MEMORY_LIMIT, temp_word);

					return_resource(&(resources->p_mem_head), p_mem_node);
					kfree(hold_p_mem_node);
				}
			} else {
				/* it used the most of the range */
				hold_p_mem_node->next = func->p_mem_head;
				func->p_mem_head = hold_p_mem_node;
			}
		} else if (hold_p_mem_node) {
			/* it used the whole range */
			hold_p_mem_node->next = func->p_mem_head;
			func->p_mem_head = hold_p_mem_node;
		}

		/* We should be configuring an IRQ and the bridge's base address
		 * registers if it needs them.  Although we have never seen such
		 * a device
		 */

		shpchprm_enable_card(ctrl, func, PCI_HEADER_TYPE_BRIDGE);

		dbg("PCI Bridge Hot-Added s:b:d:f(%02x:%02x:%02x:%02x)\n", ctrl->seg, func->bus, func->device, func->function);
	} else if ((temp_byte & 0x7F) == PCI_HEADER_TYPE_NORMAL) {
		/* Standard device */
		u64	base64;
		rc = pci_bus_read_config_byte (pci_bus, devfn, 0x0B, &class_code);

		if (class_code == PCI_BASE_CLASS_DISPLAY)
			return (DEVICE_TYPE_NOT_SUPPORTED);

		/* Figure out IO and memory needs */
		for (cloop = PCI_BASE_ADDRESS_0; cloop <= PCI_BASE_ADDRESS_5; cloop += 4) {
			temp_register = 0xFFFFFFFF;

			rc = pci_bus_write_config_dword (pci_bus, devfn, cloop, temp_register);
			rc = pci_bus_read_config_dword(pci_bus, devfn, cloop, &temp_register);
			dbg("Bar[%x]=0x%x on bus:dev:func(0x%x:%x:%x)\n", cloop, temp_register, func->bus, func->device, 
				func->function);

			if (!temp_register)
				continue;

			base64 = 0L;
			if (temp_register & PCI_BASE_ADDRESS_SPACE_IO) {
				/* Map IO */

				/* set base = amount of IO space */
				base = temp_register & 0xFFFFFFFC;
				base = ~base + 1;

				dbg("NEED IO length(0x%x)\n", base);
				io_node = get_io_resource(&(resources->io_head),(ulong)base);

				/* allocate the resource to the board */
				if (io_node) {
					dbg("Got IO base=0x%x(length=0x%x)\n", io_node->base, io_node->length);
					base = (u32)io_node->base;
					io_node->next = func->io_head;
					func->io_head = io_node;
				} else {
					err("Got NO IO resource(length=0x%x)\n", base);
					return -ENOMEM;
				}
			} else {	/* map MEM */
				int prefetchable = 1;
				struct pci_resource **res_node = &func->p_mem_head;
				char *res_type_str = "PMEM";
				u32	temp_register2;

				if (!(temp_register & PCI_BASE_ADDRESS_MEM_PREFETCH)) {
					prefetchable = 0;
					res_node = &func->mem_head;
					res_type_str++;
				}

				base = temp_register & 0xFFFFFFF0;
				base = ~base + 1;

				switch (temp_register & PCI_BASE_ADDRESS_MEM_TYPE_MASK) {
				case PCI_BASE_ADDRESS_MEM_TYPE_32:
					dbg("NEED 32 %s bar=0x%x(length=0x%x)\n", res_type_str, temp_register, base);

					if (prefetchable && resources->p_mem_head)
						mem_node=get_resource(&(resources->p_mem_head), (ulong)base);
					else {
						if (prefetchable)
							dbg("using MEM for PMEM\n");
						mem_node=get_resource(&(resources->mem_head), (ulong)base);
					}

					/* allocate the resource to the board */
					if (mem_node) {
						base = (u32)mem_node->base; 
						mem_node->next = *res_node;
						*res_node = mem_node;
						dbg("Got 32 %s base=0x%x(length=0x%x)\n", res_type_str, mem_node->base, 
							mem_node->length);
					} else {
						err("Got NO 32 %s resource(length=0x%x)\n", res_type_str, base);
						return -ENOMEM;
					}
					break;
				case PCI_BASE_ADDRESS_MEM_TYPE_64:
					rc = pci_bus_read_config_dword(pci_bus, devfn, cloop+4, &temp_register2);
					dbg("NEED 64 %s bar=0x%x:%x(length=0x%x)\n", res_type_str, temp_register2, 
						temp_register, base);

					if (prefetchable && resources->p_mem_head)
						mem_node = get_resource(&(resources->p_mem_head), (ulong)base);
					else {
						if (prefetchable)
							dbg("using MEM for PMEM\n");
						mem_node = get_resource(&(resources->mem_head), (ulong)base);
					}

					/* allocate the resource to the board */
					if (mem_node) {
						base64 = mem_node->base; 
						mem_node->next = *res_node;
						*res_node = mem_node;
						dbg("Got 64 %s base=0x%x:%x(length=%x)\n", res_type_str, (u32)(base64 >> 32), 
							(u32)base64, mem_node->length);
					} else {
						err("Got NO 64 %s resource(length=0x%x)\n", res_type_str, base);
						return -ENOMEM;
					}
					break;
				default:
					dbg("reserved BAR type=0x%x\n", temp_register);
					break;
				}

			}

			if (base64) {
				rc = pci_bus_write_config_dword(pci_bus, devfn, cloop, (u32)base64);
				cloop += 4;
				base64 >>= 32;

				if (base64) {
					dbg("%s: high dword of base64(0x%x) set to 0\n", __FUNCTION__, (u32)base64);
					base64 = 0x0L;
				}

				rc = pci_bus_write_config_dword(pci_bus, devfn, cloop, (u32)base64);
			} else {
				rc = pci_bus_write_config_dword(pci_bus, devfn, cloop, base);
			}
		}		/* End of base register loop */

#if defined(CONFIG_X86_64)
		/* Figure out which interrupt pin this function uses */
		rc = pci_bus_read_config_byte (pci_bus, devfn, PCI_INTERRUPT_PIN, &temp_byte);

		/* If this function needs an interrupt and we are behind a bridge
		   and the pin is tied to something that's alread mapped,
		   set this one the same
		 */
		if (temp_byte && resources->irqs && 
		    (resources->irqs->valid_INT & 
		     (0x01 << ((temp_byte + resources->irqs->barber_pole - 1) & 0x03)))) {
			/* We have to share with something already set up */
			IRQ = resources->irqs->interrupt[(temp_byte + resources->irqs->barber_pole - 1) & 0x03];
		} else {
			/* Program IRQ based on card type */
			rc = pci_bus_read_config_byte (pci_bus, devfn, 0x0B, &class_code);

			if (class_code == PCI_BASE_CLASS_STORAGE) {
				IRQ = shpchp_disk_irq;
			} else {
				IRQ = shpchp_nic_irq;
			}
		}

		/* IRQ Line */
		rc = pci_bus_write_config_byte (pci_bus, devfn, PCI_INTERRUPT_LINE, IRQ);

		if (!behind_bridge) {
			rc = shpchp_set_irq(func->bus, func->device, temp_byte + 0x09, IRQ);
			if (rc)
				return(1);
		} else {
			/* TBD - this code may also belong in the other clause of this If statement */
			resources->irqs->interrupt[(temp_byte + resources->irqs->barber_pole - 1) & 0x03] = IRQ;
			resources->irqs->valid_INT |= 0x01 << (temp_byte + resources->irqs->barber_pole - 1) & 0x03;
		}
#endif
		/* Disable ROM base Address */
		temp_word = 0x00L;
		rc = pci_bus_write_config_word (pci_bus, devfn, PCI_ROM_ADDRESS, temp_word);

		/* Set HP parameters (Cache Line Size, Latency Timer) */
		rc = shpchprm_set_hpp(ctrl, func, PCI_HEADER_TYPE_NORMAL);
		if (rc)
			return rc;

		shpchprm_enable_card(ctrl, func, PCI_HEADER_TYPE_NORMAL);

		dbg("PCI function Hot-Added s:b:d:f(%02x:%02x:%02x:%02x)\n", ctrl->seg, func->bus, func->device, func->function);
	}			/* End of Not-A-Bridge else */
	else {
		/* It's some strange type of PCI adapter (Cardbus?) */
		return(DEVICE_TYPE_NOT_SUPPORTED);
	}

	func->configured = 1;

	return 0;
}

