// SPDX-License-Identifier: GPL-2.0+
/*
 * Compaq Hot Plug Controller Driver
 *
 * Copyright (C) 1995,2001 Compaq Computer Corporation
 * Copyright (C) 2001 Greg Kroah-Hartman (greg@kroah.com)
 * Copyright (C) 2001 IBM Corp.
 *
 * All rights reserved.
 *
 * Send feedback to <greg@kroah.com>
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/wait.h>
#include <linux/pci.h>
#include <linux/pci_hotplug.h>
#include <linux/kthread.h>
#include "cpqphp.h"

static u32 configure_new_device(struct controller *ctrl, struct pci_func *func,
			u8 behind_bridge, struct resource_lists *resources);
static int configure_new_function(struct controller *ctrl, struct pci_func *func,
			u8 behind_bridge, struct resource_lists *resources);
static void interrupt_event_handler(struct controller *ctrl);


static struct task_struct *cpqhp_event_thread;
static struct timer_list *pushbutton_pending;	/* = NULL */

/* delay is in jiffies to wait for */
static void long_delay(int delay)
{
	/*
	 * XXX(hch): if someone is bored please convert all callers
	 * to call msleep_interruptible directly.  They really want
	 * to specify timeouts in natural units and spend a lot of
	 * effort converting them to jiffies..
	 */
	msleep_interruptible(jiffies_to_msecs(delay));
}


/* FIXME: The following line needs to be somewhere else... */
#define WRONG_BUS_FREQUENCY 0x07
static u8 handle_switch_change(u8 change, struct controller *ctrl)
{
	int hp_slot;
	u8 rc = 0;
	u16 temp_word;
	struct pci_func *func;
	struct event_info *taskInfo;

	if (!change)
		return 0;

	/* Switch Change */
	dbg("cpqsbd:  Switch interrupt received.\n");

	for (hp_slot = 0; hp_slot < 6; hp_slot++) {
		if (change & (0x1L << hp_slot)) {
			/*
			 * this one changed.
			 */
			func = cpqhp_slot_find(ctrl->bus,
				(hp_slot + ctrl->slot_device_offset), 0);

			/* this is the structure that tells the worker thread
			 * what to do
			 */
			taskInfo = &(ctrl->event_queue[ctrl->next_event]);
			ctrl->next_event = (ctrl->next_event + 1) % 10;
			taskInfo->hp_slot = hp_slot;

			rc++;

			temp_word = ctrl->ctrl_int_comp >> 16;
			func->presence_save = (temp_word >> hp_slot) & 0x01;
			func->presence_save |= (temp_word >> (hp_slot + 7)) & 0x02;

			if (ctrl->ctrl_int_comp & (0x1L << hp_slot)) {
				/*
				 * Switch opened
				 */

				func->switch_save = 0;

				taskInfo->event_type = INT_SWITCH_OPEN;
			} else {
				/*
				 * Switch closed
				 */

				func->switch_save = 0x10;

				taskInfo->event_type = INT_SWITCH_CLOSE;
			}
		}
	}

	return rc;
}

/**
 * cpqhp_find_slot - find the struct slot of given device
 * @ctrl: scan lots of this controller
 * @device: the device id to find
 */
static struct slot *cpqhp_find_slot(struct controller *ctrl, u8 device)
{
	struct slot *slot = ctrl->slot;

	while (slot && (slot->device != device))
		slot = slot->next;

	return slot;
}


static u8 handle_presence_change(u16 change, struct controller *ctrl)
{
	int hp_slot;
	u8 rc = 0;
	u8 temp_byte;
	u16 temp_word;
	struct pci_func *func;
	struct event_info *taskInfo;
	struct slot *p_slot;

	if (!change)
		return 0;

	/*
	 * Presence Change
	 */
	dbg("cpqsbd:  Presence/Notify input change.\n");
	dbg("         Changed bits are 0x%4.4x\n", change);

	for (hp_slot = 0; hp_slot < 6; hp_slot++) {
		if (change & (0x0101 << hp_slot)) {
			/*
			 * this one changed.
			 */
			func = cpqhp_slot_find(ctrl->bus,
				(hp_slot + ctrl->slot_device_offset), 0);

			taskInfo = &(ctrl->event_queue[ctrl->next_event]);
			ctrl->next_event = (ctrl->next_event + 1) % 10;
			taskInfo->hp_slot = hp_slot;

			rc++;

			p_slot = cpqhp_find_slot(ctrl, hp_slot + (readb(ctrl->hpc_reg + SLOT_MASK) >> 4));
			if (!p_slot)
				return 0;

			/* If the switch closed, must be a button
			 * If not in button mode, nevermind
			 */
			if (func->switch_save && (ctrl->push_button == 1)) {
				temp_word = ctrl->ctrl_int_comp >> 16;
				temp_byte = (temp_word >> hp_slot) & 0x01;
				temp_byte |= (temp_word >> (hp_slot + 7)) & 0x02;

				if (temp_byte != func->presence_save) {
					/*
					 * button Pressed (doesn't do anything)
					 */
					dbg("hp_slot %d button pressed\n", hp_slot);
					taskInfo->event_type = INT_BUTTON_PRESS;
				} else {
					/*
					 * button Released - TAKE ACTION!!!!
					 */
					dbg("hp_slot %d button released\n", hp_slot);
					taskInfo->event_type = INT_BUTTON_RELEASE;

					/* Cancel if we are still blinking */
					if ((p_slot->state == BLINKINGON_STATE)
					    || (p_slot->state == BLINKINGOFF_STATE)) {
						taskInfo->event_type = INT_BUTTON_CANCEL;
						dbg("hp_slot %d button cancel\n", hp_slot);
					} else if ((p_slot->state == POWERON_STATE)
						   || (p_slot->state == POWEROFF_STATE)) {
						/* info(msg_button_ignore, p_slot->number); */
						taskInfo->event_type = INT_BUTTON_IGNORE;
						dbg("hp_slot %d button ignore\n", hp_slot);
					}
				}
			} else {
				/* Switch is open, assume a presence change
				 * Save the presence state
				 */
				temp_word = ctrl->ctrl_int_comp >> 16;
				func->presence_save = (temp_word >> hp_slot) & 0x01;
				func->presence_save |= (temp_word >> (hp_slot + 7)) & 0x02;

				if ((!(ctrl->ctrl_int_comp & (0x010000 << hp_slot))) ||
				    (!(ctrl->ctrl_int_comp & (0x01000000 << hp_slot)))) {
					/* Present */
					taskInfo->event_type = INT_PRESENCE_ON;
				} else {
					/* Not Present */
					taskInfo->event_type = INT_PRESENCE_OFF;
				}
			}
		}
	}

	return rc;
}


static u8 handle_power_fault(u8 change, struct controller *ctrl)
{
	int hp_slot;
	u8 rc = 0;
	struct pci_func *func;
	struct event_info *taskInfo;

	if (!change)
		return 0;

	/*
	 * power fault
	 */

	info("power fault interrupt\n");

	for (hp_slot = 0; hp_slot < 6; hp_slot++) {
		if (change & (0x01 << hp_slot)) {
			/*
			 * this one changed.
			 */
			func = cpqhp_slot_find(ctrl->bus,
				(hp_slot + ctrl->slot_device_offset), 0);

			taskInfo = &(ctrl->event_queue[ctrl->next_event]);
			ctrl->next_event = (ctrl->next_event + 1) % 10;
			taskInfo->hp_slot = hp_slot;

			rc++;

			if (ctrl->ctrl_int_comp & (0x00000100 << hp_slot)) {
				/*
				 * power fault Cleared
				 */
				func->status = 0x00;

				taskInfo->event_type = INT_POWER_FAULT_CLEAR;
			} else {
				/*
				 * power fault
				 */
				taskInfo->event_type = INT_POWER_FAULT;

				if (ctrl->rev < 4) {
					amber_LED_on(ctrl, hp_slot);
					green_LED_off(ctrl, hp_slot);
					set_SOGO(ctrl);

					/* this is a fatal condition, we want
					 * to crash the machine to protect from
					 * data corruption. simulated_NMI
					 * shouldn't ever return */
					/* FIXME
					simulated_NMI(hp_slot, ctrl); */

					/* The following code causes a software
					 * crash just in case simulated_NMI did
					 * return */
					/*FIXME
					panic(msg_power_fault); */
				} else {
					/* set power fault status for this board */
					func->status = 0xFF;
					info("power fault bit %x set\n", hp_slot);
				}
			}
		}
	}

	return rc;
}


/**
 * sort_by_size - sort nodes on the list by their length, smallest first.
 * @head: list to sort
 */
static int sort_by_size(struct pci_resource **head)
{
	struct pci_resource *current_res;
	struct pci_resource *next_res;
	int out_of_order = 1;

	if (!(*head))
		return 1;

	if (!((*head)->next))
		return 0;

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

	return 0;
}


/**
 * sort_by_max_size - sort nodes on the list by their length, largest first.
 * @head: list to sort
 */
static int sort_by_max_size(struct pci_resource **head)
{
	struct pci_resource *current_res;
	struct pci_resource *next_res;
	int out_of_order = 1;

	if (!(*head))
		return 1;

	if (!((*head)->next))
		return 0;

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

	return 0;
}


/**
 * do_pre_bridge_resource_split - find node of resources that are unused
 * @head: new list head
 * @orig_head: original list head
 * @alignment: max node size (?)
 */
static struct pci_resource *do_pre_bridge_resource_split(struct pci_resource **head,
				struct pci_resource **orig_head, u32 alignment)
{
	struct pci_resource *prevnode = NULL;
	struct pci_resource *node;
	struct pci_resource *split_node;
	u32 rc;
	u32 temp_dword;
	dbg("do_pre_bridge_resource_split\n");

	if (!(*head) || !(*orig_head))
		return NULL;

	rc = cpqhp_resource_sort_and_combine(head);

	if (rc)
		return NULL;

	if ((*head)->base != (*orig_head)->base)
		return NULL;

	if ((*head)->length == (*orig_head)->length)
		return NULL;


	/* If we got here, there the bridge requires some of the resource, but
	 * we may be able to split some off of the front
	 */

	node = *head;

	if (node->length & (alignment - 1)) {
		/* this one isn't an aligned length, so we'll make a new entry
		 * and split it up.
		 */
		split_node = kmalloc(sizeof(*split_node), GFP_KERNEL);

		if (!split_node)
			return NULL;

		temp_dword = (node->length | (alignment-1)) + 1 - alignment;

		split_node->base = node->base;
		split_node->length = temp_dword;

		node->length -= temp_dword;
		node->base += split_node->length;

		/* Put it in the list */
		*head = split_node;
		split_node->next = node;
	}

	if (node->length < alignment)
		return NULL;

	/* Now unlink it */
	if (*head == node) {
		*head = node->next;
	} else {
		prevnode = *head;
		while (prevnode->next != node)
			prevnode = prevnode->next;

		prevnode->next = node->next;
	}
	node->next = NULL;

	return node;
}


/**
 * do_bridge_resource_split - find one node of resources that aren't in use
 * @head: list head
 * @alignment: max node size (?)
 */
static struct pci_resource *do_bridge_resource_split(struct pci_resource **head, u32 alignment)
{
	struct pci_resource *prevnode = NULL;
	struct pci_resource *node;
	u32 rc;
	u32 temp_dword;

	rc = cpqhp_resource_sort_and_combine(head);

	if (rc)
		return NULL;

	node = *head;

	while (node->next) {
		prevnode = node;
		node = node->next;
		kfree(prevnode);
	}

	if (node->length < alignment)
		goto error;

	if (node->base & (alignment - 1)) {
		/* Short circuit if adjusted size is too small */
		temp_dword = (node->base | (alignment-1)) + 1;
		if ((node->length - (temp_dword - node->base)) < alignment)
			goto error;

		node->length -= (temp_dword - node->base);
		node->base = temp_dword;
	}

	if (node->length & (alignment - 1))
		/* There's stuff in use after this node */
		goto error;

	return node;
error:
	kfree(node);
	return NULL;
}


/**
 * get_io_resource - find first node of given size not in ISA aliasing window.
 * @head: list to search
 * @size: size of node to find, must be a power of two.
 *
 * Description: This function sorts the resource list by size and then
 * returns the first node of "size" length that is not in the ISA aliasing
 * window.  If it finds a node larger than "size" it will split it up.
 */
static struct pci_resource *get_io_resource(struct pci_resource **head, u32 size)
{
	struct pci_resource *prevnode;
	struct pci_resource *node;
	struct pci_resource *split_node;
	u32 temp_dword;

	if (!(*head))
		return NULL;

	if (cpqhp_resource_sort_and_combine(head))
		return NULL;

	if (sort_by_size(head))
		return NULL;

	for (node = *head; node; node = node->next) {
		if (node->length < size)
			continue;

		if (node->base & (size - 1)) {
			/* this one isn't base aligned properly
			 * so we'll make a new entry and split it up
			 */
			temp_dword = (node->base | (size-1)) + 1;

			/* Short circuit if adjusted size is too small */
			if ((node->length - (temp_dword - node->base)) < size)
				continue;

			split_node = kmalloc(sizeof(*split_node), GFP_KERNEL);

			if (!split_node)
				return NULL;

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
			/* this one is longer than we need
			 * so we'll make a new entry and split it up
			 */
			split_node = kmalloc(sizeof(*split_node), GFP_KERNEL);

			if (!split_node)
				return NULL;

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
		 * Now take it out of the list and break
		 */
		if (*head == node) {
			*head = node->next;
		} else {
			prevnode = *head;
			while (prevnode->next != node)
				prevnode = prevnode->next;

			prevnode->next = node->next;
		}
		node->next = NULL;
		break;
	}

	return node;
}


/**
 * get_max_resource - get largest node which has at least the given size.
 * @head: the list to search the node in
 * @size: the minimum size of the node to find
 *
 * Description: Gets the largest node that is at least "size" big from the
 * list pointed to by head.  It aligns the node on top and bottom
 * to "size" alignment before returning it.
 */
static struct pci_resource *get_max_resource(struct pci_resource **head, u32 size)
{
	struct pci_resource *max;
	struct pci_resource *temp;
	struct pci_resource *split_node;
	u32 temp_dword;

	if (cpqhp_resource_sort_and_combine(head))
		return NULL;

	if (sort_by_max_size(head))
		return NULL;

	for (max = *head; max; max = max->next) {
		/* If not big enough we could probably just bail,
		 * instead we'll continue to the next.
		 */
		if (max->length < size)
			continue;

		if (max->base & (size - 1)) {
			/* this one isn't base aligned properly
			 * so we'll make a new entry and split it up
			 */
			temp_dword = (max->base | (size-1)) + 1;

			/* Short circuit if adjusted size is too small */
			if ((max->length - (temp_dword - max->base)) < size)
				continue;

			split_node = kmalloc(sizeof(*split_node), GFP_KERNEL);

			if (!split_node)
				return NULL;

			split_node->base = max->base;
			split_node->length = temp_dword - max->base;
			max->base = temp_dword;
			max->length -= split_node->length;

			split_node->next = max->next;
			max->next = split_node;
		}

		if ((max->base + max->length) & (size - 1)) {
			/* this one isn't end aligned properly at the top
			 * so we'll make a new entry and split it up
			 */
			split_node = kmalloc(sizeof(*split_node), GFP_KERNEL);

			if (!split_node)
				return NULL;
			temp_dword = ((max->base + max->length) & ~(size - 1));
			split_node->base = temp_dword;
			split_node->length = max->length + max->base
					     - split_node->base;
			max->length -= split_node->length;

			split_node->next = max->next;
			max->next = split_node;
		}

		/* Make sure it didn't shrink too much when we aligned it */
		if (max->length < size)
			continue;

		/* Now take it out of the list */
		temp = *head;
		if (temp == max) {
			*head = max->next;
		} else {
			while (temp && temp->next != max)
				temp = temp->next;

			if (temp)
				temp->next = max->next;
		}

		max->next = NULL;
		break;
	}

	return max;
}


/**
 * get_resource - find resource of given size and split up larger ones.
 * @head: the list to search for resources
 * @size: the size limit to use
 *
 * Description: This function sorts the resource list by size and then
 * returns the first node of "size" length.  If it finds a node
 * larger than "size" it will split it up.
 *
 * size must be a power of two.
 */
static struct pci_resource *get_resource(struct pci_resource **head, u32 size)
{
	struct pci_resource *prevnode;
	struct pci_resource *node;
	struct pci_resource *split_node;
	u32 temp_dword;

	if (cpqhp_resource_sort_and_combine(head))
		return NULL;

	if (sort_by_size(head))
		return NULL;

	for (node = *head; node; node = node->next) {
		dbg("%s: req_size =%x node=%p, base=%x, length=%x\n",
		    __func__, size, node, node->base, node->length);
		if (node->length < size)
			continue;

		if (node->base & (size - 1)) {
			dbg("%s: not aligned\n", __func__);
			/* this one isn't base aligned properly
			 * so we'll make a new entry and split it up
			 */
			temp_dword = (node->base | (size-1)) + 1;

			/* Short circuit if adjusted size is too small */
			if ((node->length - (temp_dword - node->base)) < size)
				continue;

			split_node = kmalloc(sizeof(*split_node), GFP_KERNEL);

			if (!split_node)
				return NULL;

			split_node->base = node->base;
			split_node->length = temp_dword - node->base;
			node->base = temp_dword;
			node->length -= split_node->length;

			split_node->next = node->next;
			node->next = split_node;
		} /* End of non-aligned base */

		/* Don't need to check if too small since we already did */
		if (node->length > size) {
			dbg("%s: too big\n", __func__);
			/* this one is longer than we need
			 * so we'll make a new entry and split it up
			 */
			split_node = kmalloc(sizeof(*split_node), GFP_KERNEL);

			if (!split_node)
				return NULL;

			split_node->base = node->base + size;
			split_node->length = node->length - size;
			node->length = size;

			/* Put it in the list */
			split_node->next = node->next;
			node->next = split_node;
		}  /* End of too big on top end */

		dbg("%s: got one!!!\n", __func__);
		/* If we got here, then it is the right size
		 * Now take it out of the list */
		if (*head == node) {
			*head = node->next;
		} else {
			prevnode = *head;
			while (prevnode->next != node)
				prevnode = prevnode->next;

			prevnode->next = node->next;
		}
		node->next = NULL;
		break;
	}
	return node;
}


/**
 * cpqhp_resource_sort_and_combine - sort nodes by base addresses and clean up
 * @head: the list to sort and clean up
 *
 * Description: Sorts all of the nodes in the list in ascending order by
 * their base addresses.  Also does garbage collection by
 * combining adjacent nodes.
 *
 * Returns %0 if success.
 */
int cpqhp_resource_sort_and_combine(struct pci_resource **head)
{
	struct pci_resource *node1;
	struct pci_resource *node2;
	int out_of_order = 1;

	dbg("%s: head = %p, *head = %p\n", __func__, head, *head);

	if (!(*head))
		return 1;

	dbg("*head->next = %p\n", (*head)->next);

	if (!(*head)->next)
		return 0;	/* only one item on the list, already sorted! */

	dbg("*head->base = 0x%x\n", (*head)->base);
	dbg("*head->next->base = 0x%x\n", (*head)->next->base);
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

	return 0;
}


irqreturn_t cpqhp_ctrl_intr(int IRQ, void *data)
{
	struct controller *ctrl = data;
	u8 schedule_flag = 0;
	u8 reset;
	u16 misc;
	u32 Diff;


	misc = readw(ctrl->hpc_reg + MISC);
	/*
	 * Check to see if it was our interrupt
	 */
	if (!(misc & 0x000C))
		return IRQ_NONE;

	if (misc & 0x0004) {
		/*
		 * Serial Output interrupt Pending
		 */

		/* Clear the interrupt */
		misc |= 0x0004;
		writew(misc, ctrl->hpc_reg + MISC);

		/* Read to clear posted writes */
		misc = readw(ctrl->hpc_reg + MISC);

		dbg("%s - waking up\n", __func__);
		wake_up_interruptible(&ctrl->queue);
	}

	if (misc & 0x0008) {
		/* General-interrupt-input interrupt Pending */
		Diff = readl(ctrl->hpc_reg + INT_INPUT_CLEAR) ^ ctrl->ctrl_int_comp;

		ctrl->ctrl_int_comp = readl(ctrl->hpc_reg + INT_INPUT_CLEAR);

		/* Clear the interrupt */
		writel(Diff, ctrl->hpc_reg + INT_INPUT_CLEAR);

		/* Read it back to clear any posted writes */
		readl(ctrl->hpc_reg + INT_INPUT_CLEAR);

		if (!Diff)
			/* Clear all interrupts */
			writel(0xFFFFFFFF, ctrl->hpc_reg + INT_INPUT_CLEAR);

		schedule_flag += handle_switch_change((u8)(Diff & 0xFFL), ctrl);
		schedule_flag += handle_presence_change((u16)((Diff & 0xFFFF0000L) >> 16), ctrl);
		schedule_flag += handle_power_fault((u8)((Diff & 0xFF00L) >> 8), ctrl);
	}

	reset = readb(ctrl->hpc_reg + RESET_FREQ_MODE);
	if (reset & 0x40) {
		/* Bus reset has completed */
		reset &= 0xCF;
		writeb(reset, ctrl->hpc_reg + RESET_FREQ_MODE);
		reset = readb(ctrl->hpc_reg + RESET_FREQ_MODE);
		wake_up_interruptible(&ctrl->queue);
	}

	if (schedule_flag) {
		wake_up_process(cpqhp_event_thread);
		dbg("Waking even thread");
	}
	return IRQ_HANDLED;
}


/**
 * cpqhp_slot_create - Creates a node and adds it to the proper bus.
 * @busnumber: bus where new node is to be located
 *
 * Returns pointer to the new node or %NULL if unsuccessful.
 */
struct pci_func *cpqhp_slot_create(u8 busnumber)
{
	struct pci_func *new_slot;
	struct pci_func *next;

	new_slot = kzalloc(sizeof(*new_slot), GFP_KERNEL);
	if (new_slot == NULL)
		return new_slot;

	new_slot->next = NULL;
	new_slot->configured = 1;

	if (cpqhp_slot_list[busnumber] == NULL) {
		cpqhp_slot_list[busnumber] = new_slot;
	} else {
		next = cpqhp_slot_list[busnumber];
		while (next->next != NULL)
			next = next->next;
		next->next = new_slot;
	}
	return new_slot;
}


/**
 * slot_remove - Removes a node from the linked list of slots.
 * @old_slot: slot to remove
 *
 * Returns %0 if successful, !0 otherwise.
 */
static int slot_remove(struct pci_func *old_slot)
{
	struct pci_func *next;

	if (old_slot == NULL)
		return 1;

	next = cpqhp_slot_list[old_slot->bus];
	if (next == NULL)
		return 1;

	if (next == old_slot) {
		cpqhp_slot_list[old_slot->bus] = old_slot->next;
		cpqhp_destroy_board_resources(old_slot);
		kfree(old_slot);
		return 0;
	}

	while ((next->next != old_slot) && (next->next != NULL))
		next = next->next;

	if (next->next == old_slot) {
		next->next = old_slot->next;
		cpqhp_destroy_board_resources(old_slot);
		kfree(old_slot);
		return 0;
	} else
		return 2;
}


/**
 * bridge_slot_remove - Removes a node from the linked list of slots.
 * @bridge: bridge to remove
 *
 * Returns %0 if successful, !0 otherwise.
 */
static int bridge_slot_remove(struct pci_func *bridge)
{
	u8 subordinateBus, secondaryBus;
	u8 tempBus;
	struct pci_func *next;

	secondaryBus = (bridge->config_space[0x06] >> 8) & 0xFF;
	subordinateBus = (bridge->config_space[0x06] >> 16) & 0xFF;

	for (tempBus = secondaryBus; tempBus <= subordinateBus; tempBus++) {
		next = cpqhp_slot_list[tempBus];

		while (!slot_remove(next))
			next = cpqhp_slot_list[tempBus];
	}

	next = cpqhp_slot_list[bridge->bus];

	if (next == NULL)
		return 1;

	if (next == bridge) {
		cpqhp_slot_list[bridge->bus] = bridge->next;
		goto out;
	}

	while ((next->next != bridge) && (next->next != NULL))
		next = next->next;

	if (next->next != bridge)
		return 2;
	next->next = bridge->next;
out:
	kfree(bridge);
	return 0;
}


/**
 * cpqhp_slot_find - Looks for a node by bus, and device, multiple functions accessed
 * @bus: bus to find
 * @device: device to find
 * @index: is %0 for first function found, %1 for the second...
 *
 * Returns pointer to the node if successful, %NULL otherwise.
 */
struct pci_func *cpqhp_slot_find(u8 bus, u8 device, u8 index)
{
	int found = -1;
	struct pci_func *func;

	func = cpqhp_slot_list[bus];

	if ((func == NULL) || ((func->device == device) && (index == 0)))
		return func;

	if (func->device == device)
		found++;

	while (func->next != NULL) {
		func = func->next;

		if (func->device == device)
			found++;

		if (found == index)
			return func;
	}

	return NULL;
}


/* DJZ: I don't think is_bridge will work as is.
 * FIXME */
static int is_bridge(struct pci_func *func)
{
	/* Check the header type */
	if (((func->config_space[0x03] >> 16) & 0xFF) == 0x01)
		return 1;
	else
		return 0;
}


/**
 * set_controller_speed - set the frequency and/or mode of a specific controller segment.
 * @ctrl: controller to change frequency/mode for.
 * @adapter_speed: the speed of the adapter we want to match.
 * @hp_slot: the slot number where the adapter is installed.
 *
 * Returns %0 if we successfully change frequency and/or mode to match the
 * adapter speed.
 */
static u8 set_controller_speed(struct controller *ctrl, u8 adapter_speed, u8 hp_slot)
{
	struct slot *slot;
	struct pci_bus *bus = ctrl->pci_bus;
	u8 reg;
	u8 slot_power = readb(ctrl->hpc_reg + SLOT_POWER);
	u16 reg16;
	u32 leds = readl(ctrl->hpc_reg + LED_CONTROL);

	if (bus->cur_bus_speed == adapter_speed)
		return 0;

	/* We don't allow freq/mode changes if we find another adapter running
	 * in another slot on this controller
	 */
	for (slot = ctrl->slot; slot; slot = slot->next) {
		if (slot->device == (hp_slot + ctrl->slot_device_offset))
			continue;
		if (get_presence_status(ctrl, slot) == 0)
			continue;
		/* If another adapter is running on the same segment but at a
		 * lower speed/mode, we allow the new adapter to function at
		 * this rate if supported
		 */
		if (bus->cur_bus_speed < adapter_speed)
			return 0;

		return 1;
	}

	/* If the controller doesn't support freq/mode changes and the
	 * controller is running at a higher mode, we bail
	 */
	if ((bus->cur_bus_speed > adapter_speed) && (!ctrl->pcix_speed_capability))
		return 1;

	/* But we allow the adapter to run at a lower rate if possible */
	if ((bus->cur_bus_speed < adapter_speed) && (!ctrl->pcix_speed_capability))
		return 0;

	/* We try to set the max speed supported by both the adapter and
	 * controller
	 */
	if (bus->max_bus_speed < adapter_speed) {
		if (bus->cur_bus_speed == bus->max_bus_speed)
			return 0;
		adapter_speed = bus->max_bus_speed;
	}

	writel(0x0L, ctrl->hpc_reg + LED_CONTROL);
	writeb(0x00, ctrl->hpc_reg + SLOT_ENABLE);

	set_SOGO(ctrl);
	wait_for_ctrl_irq(ctrl);

	if (adapter_speed != PCI_SPEED_133MHz_PCIX)
		reg = 0xF5;
	else
		reg = 0xF4;
	pci_write_config_byte(ctrl->pci_dev, 0x41, reg);

	reg16 = readw(ctrl->hpc_reg + NEXT_CURR_FREQ);
	reg16 &= ~0x000F;
	switch (adapter_speed) {
		case(PCI_SPEED_133MHz_PCIX):
			reg = 0x75;
			reg16 |= 0xB;
			break;
		case(PCI_SPEED_100MHz_PCIX):
			reg = 0x74;
			reg16 |= 0xA;
			break;
		case(PCI_SPEED_66MHz_PCIX):
			reg = 0x73;
			reg16 |= 0x9;
			break;
		case(PCI_SPEED_66MHz):
			reg = 0x73;
			reg16 |= 0x1;
			break;
		default: /* 33MHz PCI 2.2 */
			reg = 0x71;
			break;

	}
	reg16 |= 0xB << 12;
	writew(reg16, ctrl->hpc_reg + NEXT_CURR_FREQ);

	mdelay(5);

	/* Re-enable interrupts */
	writel(0, ctrl->hpc_reg + INT_MASK);

	pci_write_config_byte(ctrl->pci_dev, 0x41, reg);

	/* Restart state machine */
	reg = ~0xF;
	pci_read_config_byte(ctrl->pci_dev, 0x43, &reg);
	pci_write_config_byte(ctrl->pci_dev, 0x43, reg);

	/* Only if mode change...*/
	if (((bus->cur_bus_speed == PCI_SPEED_66MHz) && (adapter_speed == PCI_SPEED_66MHz_PCIX)) ||
		((bus->cur_bus_speed == PCI_SPEED_66MHz_PCIX) && (adapter_speed == PCI_SPEED_66MHz)))
			set_SOGO(ctrl);

	wait_for_ctrl_irq(ctrl);
	mdelay(1100);

	/* Restore LED/Slot state */
	writel(leds, ctrl->hpc_reg + LED_CONTROL);
	writeb(slot_power, ctrl->hpc_reg + SLOT_ENABLE);

	set_SOGO(ctrl);
	wait_for_ctrl_irq(ctrl);

	bus->cur_bus_speed = adapter_speed;
	slot = cpqhp_find_slot(ctrl, hp_slot + ctrl->slot_device_offset);

	info("Successfully changed frequency/mode for adapter in slot %d\n",
			slot->number);
	return 0;
}

/* the following routines constitute the bulk of the
 * hotplug controller logic
 */


/**
 * board_replaced - Called after a board has been replaced in the system.
 * @func: PCI device/function information
 * @ctrl: hotplug controller
 *
 * This is only used if we don't have resources for hot add.
 * Turns power on for the board.
 * Checks to see if board is the same.
 * If board is same, reconfigures it.
 * If board isn't same, turns it back off.
 */
static u32 board_replaced(struct pci_func *func, struct controller *ctrl)
{
	struct pci_bus *bus = ctrl->pci_bus;
	u8 hp_slot;
	u8 temp_byte;
	u8 adapter_speed;
	u32 rc = 0;

	hp_slot = func->device - ctrl->slot_device_offset;

	/*
	 * The switch is open.
	 */
	if (readl(ctrl->hpc_reg + INT_INPUT_CLEAR) & (0x01L << hp_slot))
		rc = INTERLOCK_OPEN;
	/*
	 * The board is already on
	 */
	else if (is_slot_enabled(ctrl, hp_slot))
		rc = CARD_FUNCTIONING;
	else {
		mutex_lock(&ctrl->crit_sect);

		/* turn on board without attaching to the bus */
		enable_slot_power(ctrl, hp_slot);

		set_SOGO(ctrl);

		/* Wait for SOBS to be unset */
		wait_for_ctrl_irq(ctrl);

		/* Change bits in slot power register to force another shift out
		 * NOTE: this is to work around the timer bug */
		temp_byte = readb(ctrl->hpc_reg + SLOT_POWER);
		writeb(0x00, ctrl->hpc_reg + SLOT_POWER);
		writeb(temp_byte, ctrl->hpc_reg + SLOT_POWER);

		set_SOGO(ctrl);

		/* Wait for SOBS to be unset */
		wait_for_ctrl_irq(ctrl);

		adapter_speed = get_adapter_speed(ctrl, hp_slot);
		if (bus->cur_bus_speed != adapter_speed)
			if (set_controller_speed(ctrl, adapter_speed, hp_slot))
				rc = WRONG_BUS_FREQUENCY;

		/* turn off board without attaching to the bus */
		disable_slot_power(ctrl, hp_slot);

		set_SOGO(ctrl);

		/* Wait for SOBS to be unset */
		wait_for_ctrl_irq(ctrl);

		mutex_unlock(&ctrl->crit_sect);

		if (rc)
			return rc;

		mutex_lock(&ctrl->crit_sect);

		slot_enable(ctrl, hp_slot);
		green_LED_blink(ctrl, hp_slot);

		amber_LED_off(ctrl, hp_slot);

		set_SOGO(ctrl);

		/* Wait for SOBS to be unset */
		wait_for_ctrl_irq(ctrl);

		mutex_unlock(&ctrl->crit_sect);

		/* Wait for ~1 second because of hot plug spec */
		long_delay(1*HZ);

		/* Check for a power fault */
		if (func->status == 0xFF) {
			/* power fault occurred, but it was benign */
			rc = POWER_FAILURE;
			func->status = 0;
		} else
			rc = cpqhp_valid_replace(ctrl, func);

		if (!rc) {
			/* It must be the same board */

			rc = cpqhp_configure_board(ctrl, func);

			/* If configuration fails, turn it off
			 * Get slot won't work for devices behind
			 * bridges, but in this case it will always be
			 * called for the "base" bus/dev/func of an
			 * adapter.
			 */

			mutex_lock(&ctrl->crit_sect);

			amber_LED_on(ctrl, hp_slot);
			green_LED_off(ctrl, hp_slot);
			slot_disable(ctrl, hp_slot);

			set_SOGO(ctrl);

			/* Wait for SOBS to be unset */
			wait_for_ctrl_irq(ctrl);

			mutex_unlock(&ctrl->crit_sect);

			if (rc)
				return rc;
			else
				return 1;

		} else {
			/* Something is wrong

			 * Get slot won't work for devices behind bridges, but
			 * in this case it will always be called for the "base"
			 * bus/dev/func of an adapter.
			 */

			mutex_lock(&ctrl->crit_sect);

			amber_LED_on(ctrl, hp_slot);
			green_LED_off(ctrl, hp_slot);
			slot_disable(ctrl, hp_slot);

			set_SOGO(ctrl);

			/* Wait for SOBS to be unset */
			wait_for_ctrl_irq(ctrl);

			mutex_unlock(&ctrl->crit_sect);
		}

	}
	return rc;

}


/**
 * board_added - Called after a board has been added to the system.
 * @func: PCI device/function info
 * @ctrl: hotplug controller
 *
 * Turns power on for the board.
 * Configures board.
 */
static u32 board_added(struct pci_func *func, struct controller *ctrl)
{
	u8 hp_slot;
	u8 temp_byte;
	u8 adapter_speed;
	int index;
	u32 temp_register = 0xFFFFFFFF;
	u32 rc = 0;
	struct pci_func *new_slot = NULL;
	struct pci_bus *bus = ctrl->pci_bus;
	struct resource_lists res_lists;

	hp_slot = func->device - ctrl->slot_device_offset;
	dbg("%s: func->device, slot_offset, hp_slot = %d, %d ,%d\n",
	    __func__, func->device, ctrl->slot_device_offset, hp_slot);

	mutex_lock(&ctrl->crit_sect);

	/* turn on board without attaching to the bus */
	enable_slot_power(ctrl, hp_slot);

	set_SOGO(ctrl);

	/* Wait for SOBS to be unset */
	wait_for_ctrl_irq(ctrl);

	/* Change bits in slot power register to force another shift out
	 * NOTE: this is to work around the timer bug
	 */
	temp_byte = readb(ctrl->hpc_reg + SLOT_POWER);
	writeb(0x00, ctrl->hpc_reg + SLOT_POWER);
	writeb(temp_byte, ctrl->hpc_reg + SLOT_POWER);

	set_SOGO(ctrl);

	/* Wait for SOBS to be unset */
	wait_for_ctrl_irq(ctrl);

	adapter_speed = get_adapter_speed(ctrl, hp_slot);
	if (bus->cur_bus_speed != adapter_speed)
		if (set_controller_speed(ctrl, adapter_speed, hp_slot))
			rc = WRONG_BUS_FREQUENCY;

	/* turn off board without attaching to the bus */
	disable_slot_power(ctrl, hp_slot);

	set_SOGO(ctrl);

	/* Wait for SOBS to be unset */
	wait_for_ctrl_irq(ctrl);

	mutex_unlock(&ctrl->crit_sect);

	if (rc)
		return rc;

	cpqhp_find_slot(ctrl, hp_slot + ctrl->slot_device_offset);

	/* turn on board and blink green LED */

	dbg("%s: before down\n", __func__);
	mutex_lock(&ctrl->crit_sect);
	dbg("%s: after down\n", __func__);

	dbg("%s: before slot_enable\n", __func__);
	slot_enable(ctrl, hp_slot);

	dbg("%s: before green_LED_blink\n", __func__);
	green_LED_blink(ctrl, hp_slot);

	dbg("%s: before amber_LED_blink\n", __func__);
	amber_LED_off(ctrl, hp_slot);

	dbg("%s: before set_SOGO\n", __func__);
	set_SOGO(ctrl);

	/* Wait for SOBS to be unset */
	dbg("%s: before wait_for_ctrl_irq\n", __func__);
	wait_for_ctrl_irq(ctrl);
	dbg("%s: after wait_for_ctrl_irq\n", __func__);

	dbg("%s: before up\n", __func__);
	mutex_unlock(&ctrl->crit_sect);
	dbg("%s: after up\n", __func__);

	/* Wait for ~1 second because of hot plug spec */
	dbg("%s: before long_delay\n", __func__);
	long_delay(1*HZ);
	dbg("%s: after long_delay\n", __func__);

	dbg("%s: func status = %x\n", __func__, func->status);
	/* Check for a power fault */
	if (func->status == 0xFF) {
		/* power fault occurred, but it was benign */
		temp_register = 0xFFFFFFFF;
		dbg("%s: temp register set to %x by power fault\n", __func__, temp_register);
		rc = POWER_FAILURE;
		func->status = 0;
	} else {
		/* Get vendor/device ID u32 */
		ctrl->pci_bus->number = func->bus;
		rc = pci_bus_read_config_dword(ctrl->pci_bus, PCI_DEVFN(func->device, func->function), PCI_VENDOR_ID, &temp_register);
		dbg("%s: pci_read_config_dword returns %d\n", __func__, rc);
		dbg("%s: temp_register is %x\n", __func__, temp_register);

		if (rc != 0) {
			/* Something's wrong here */
			temp_register = 0xFFFFFFFF;
			dbg("%s: temp register set to %x by error\n", __func__, temp_register);
		}
		/* Preset return code.  It will be changed later if things go okay. */
		rc = NO_ADAPTER_PRESENT;
	}

	/* All F's is an empty slot or an invalid board */
	if (temp_register != 0xFFFFFFFF) {
		res_lists.io_head = ctrl->io_head;
		res_lists.mem_head = ctrl->mem_head;
		res_lists.p_mem_head = ctrl->p_mem_head;
		res_lists.bus_head = ctrl->bus_head;
		res_lists.irqs = NULL;

		rc = configure_new_device(ctrl, func, 0, &res_lists);

		dbg("%s: back from configure_new_device\n", __func__);
		ctrl->io_head = res_lists.io_head;
		ctrl->mem_head = res_lists.mem_head;
		ctrl->p_mem_head = res_lists.p_mem_head;
		ctrl->bus_head = res_lists.bus_head;

		cpqhp_resource_sort_and_combine(&(ctrl->mem_head));
		cpqhp_resource_sort_and_combine(&(ctrl->p_mem_head));
		cpqhp_resource_sort_and_combine(&(ctrl->io_head));
		cpqhp_resource_sort_and_combine(&(ctrl->bus_head));

		if (rc) {
			mutex_lock(&ctrl->crit_sect);

			amber_LED_on(ctrl, hp_slot);
			green_LED_off(ctrl, hp_slot);
			slot_disable(ctrl, hp_slot);

			set_SOGO(ctrl);

			/* Wait for SOBS to be unset */
			wait_for_ctrl_irq(ctrl);

			mutex_unlock(&ctrl->crit_sect);
			return rc;
		} else {
			cpqhp_save_slot_config(ctrl, func);
		}


		func->status = 0;
		func->switch_save = 0x10;
		func->is_a_board = 0x01;

		/* next, we will instantiate the linux pci_dev structures (with
		 * appropriate driver notification, if already present) */
		dbg("%s: configure linux pci_dev structure\n", __func__);
		index = 0;
		do {
			new_slot = cpqhp_slot_find(ctrl->bus, func->device, index++);
			if (new_slot && !new_slot->pci_dev)
				cpqhp_configure_device(ctrl, new_slot);
		} while (new_slot);

		mutex_lock(&ctrl->crit_sect);

		green_LED_on(ctrl, hp_slot);

		set_SOGO(ctrl);

		/* Wait for SOBS to be unset */
		wait_for_ctrl_irq(ctrl);

		mutex_unlock(&ctrl->crit_sect);
	} else {
		mutex_lock(&ctrl->crit_sect);

		amber_LED_on(ctrl, hp_slot);
		green_LED_off(ctrl, hp_slot);
		slot_disable(ctrl, hp_slot);

		set_SOGO(ctrl);

		/* Wait for SOBS to be unset */
		wait_for_ctrl_irq(ctrl);

		mutex_unlock(&ctrl->crit_sect);

		return rc;
	}
	return 0;
}


/**
 * remove_board - Turns off slot and LEDs
 * @func: PCI device/function info
 * @replace_flag: whether replacing or adding a new device
 * @ctrl: target controller
 */
static u32 remove_board(struct pci_func *func, u32 replace_flag, struct controller *ctrl)
{
	int index;
	u8 skip = 0;
	u8 device;
	u8 hp_slot;
	u8 temp_byte;
	struct resource_lists res_lists;
	struct pci_func *temp_func;

	if (cpqhp_unconfigure_device(func))
		return 1;

	device = func->device;

	hp_slot = func->device - ctrl->slot_device_offset;
	dbg("In %s, hp_slot = %d\n", __func__, hp_slot);

	/* When we get here, it is safe to change base address registers.
	 * We will attempt to save the base address register lengths */
	if (replace_flag || !ctrl->add_support)
		cpqhp_save_base_addr_length(ctrl, func);
	else if (!func->bus_head && !func->mem_head &&
		 !func->p_mem_head && !func->io_head) {
		/* Here we check to see if we've saved any of the board's
		 * resources already.  If so, we'll skip the attempt to
		 * determine what's being used. */
		index = 0;
		temp_func = cpqhp_slot_find(func->bus, func->device, index++);
		while (temp_func) {
			if (temp_func->bus_head || temp_func->mem_head
			    || temp_func->p_mem_head || temp_func->io_head) {
				skip = 1;
				break;
			}
			temp_func = cpqhp_slot_find(temp_func->bus, temp_func->device, index++);
		}

		if (!skip)
			cpqhp_save_used_resources(ctrl, func);
	}
	/* Change status to shutdown */
	if (func->is_a_board)
		func->status = 0x01;
	func->configured = 0;

	mutex_lock(&ctrl->crit_sect);

	green_LED_off(ctrl, hp_slot);
	slot_disable(ctrl, hp_slot);

	set_SOGO(ctrl);

	/* turn off SERR for slot */
	temp_byte = readb(ctrl->hpc_reg + SLOT_SERR);
	temp_byte &= ~(0x01 << hp_slot);
	writeb(temp_byte, ctrl->hpc_reg + SLOT_SERR);

	/* Wait for SOBS to be unset */
	wait_for_ctrl_irq(ctrl);

	mutex_unlock(&ctrl->crit_sect);

	if (!replace_flag && ctrl->add_support) {
		while (func) {
			res_lists.io_head = ctrl->io_head;
			res_lists.mem_head = ctrl->mem_head;
			res_lists.p_mem_head = ctrl->p_mem_head;
			res_lists.bus_head = ctrl->bus_head;

			cpqhp_return_board_resources(func, &res_lists);

			ctrl->io_head = res_lists.io_head;
			ctrl->mem_head = res_lists.mem_head;
			ctrl->p_mem_head = res_lists.p_mem_head;
			ctrl->bus_head = res_lists.bus_head;

			cpqhp_resource_sort_and_combine(&(ctrl->mem_head));
			cpqhp_resource_sort_and_combine(&(ctrl->p_mem_head));
			cpqhp_resource_sort_and_combine(&(ctrl->io_head));
			cpqhp_resource_sort_and_combine(&(ctrl->bus_head));

			if (is_bridge(func)) {
				bridge_slot_remove(func);
			} else
				slot_remove(func);

			func = cpqhp_slot_find(ctrl->bus, device, 0);
		}

		/* Setup slot structure with entry for empty slot */
		func = cpqhp_slot_create(ctrl->bus);

		if (func == NULL)
			return 1;

		func->bus = ctrl->bus;
		func->device = device;
		func->function = 0;
		func->configured = 0;
		func->switch_save = 0x10;
		func->is_a_board = 0;
		func->p_task_event = NULL;
	}

	return 0;
}

static void pushbutton_helper_thread(struct timer_list *t)
{
	pushbutton_pending = t;

	wake_up_process(cpqhp_event_thread);
}


/* this is the main worker thread */
static int event_thread(void *data)
{
	struct controller *ctrl;

	while (1) {
		dbg("!!!!event_thread sleeping\n");
		set_current_state(TASK_INTERRUPTIBLE);
		schedule();

		if (kthread_should_stop())
			break;
		/* Do stuff here */
		if (pushbutton_pending)
			cpqhp_pushbutton_thread(pushbutton_pending);
		else
			for (ctrl = cpqhp_ctrl_list; ctrl; ctrl = ctrl->next)
				interrupt_event_handler(ctrl);
	}
	dbg("event_thread signals exit\n");
	return 0;
}

int cpqhp_event_start_thread(void)
{
	cpqhp_event_thread = kthread_run(event_thread, NULL, "phpd_event");
	if (IS_ERR(cpqhp_event_thread)) {
		err("Can't start up our event thread\n");
		return PTR_ERR(cpqhp_event_thread);
	}

	return 0;
}


void cpqhp_event_stop_thread(void)
{
	kthread_stop(cpqhp_event_thread);
}


static void interrupt_event_handler(struct controller *ctrl)
{
	int loop;
	int change = 1;
	struct pci_func *func;
	u8 hp_slot;
	struct slot *p_slot;

	while (change) {
		change = 0;

		for (loop = 0; loop < 10; loop++) {
			/* dbg("loop %d\n", loop); */
			if (ctrl->event_queue[loop].event_type != 0) {
				hp_slot = ctrl->event_queue[loop].hp_slot;

				func = cpqhp_slot_find(ctrl->bus, (hp_slot + ctrl->slot_device_offset), 0);
				if (!func)
					return;

				p_slot = cpqhp_find_slot(ctrl, hp_slot + ctrl->slot_device_offset);
				if (!p_slot)
					return;

				dbg("hp_slot %d, func %p, p_slot %p\n",
				    hp_slot, func, p_slot);

				if (ctrl->event_queue[loop].event_type == INT_BUTTON_PRESS) {
					dbg("button pressed\n");
				} else if (ctrl->event_queue[loop].event_type ==
					   INT_BUTTON_CANCEL) {
					dbg("button cancel\n");
					timer_delete(&p_slot->task_event);

					mutex_lock(&ctrl->crit_sect);

					if (p_slot->state == BLINKINGOFF_STATE) {
						/* slot is on */
						dbg("turn on green LED\n");
						green_LED_on(ctrl, hp_slot);
					} else if (p_slot->state == BLINKINGON_STATE) {
						/* slot is off */
						dbg("turn off green LED\n");
						green_LED_off(ctrl, hp_slot);
					}

					info(msg_button_cancel, p_slot->number);

					p_slot->state = STATIC_STATE;

					amber_LED_off(ctrl, hp_slot);

					set_SOGO(ctrl);

					/* Wait for SOBS to be unset */
					wait_for_ctrl_irq(ctrl);

					mutex_unlock(&ctrl->crit_sect);
				}
				/*** button Released (No action on press...) */
				else if (ctrl->event_queue[loop].event_type == INT_BUTTON_RELEASE) {
					dbg("button release\n");

					if (is_slot_enabled(ctrl, hp_slot)) {
						dbg("slot is on\n");
						p_slot->state = BLINKINGOFF_STATE;
						info(msg_button_off, p_slot->number);
					} else {
						dbg("slot is off\n");
						p_slot->state = BLINKINGON_STATE;
						info(msg_button_on, p_slot->number);
					}
					mutex_lock(&ctrl->crit_sect);

					dbg("blink green LED and turn off amber\n");

					amber_LED_off(ctrl, hp_slot);
					green_LED_blink(ctrl, hp_slot);

					set_SOGO(ctrl);

					/* Wait for SOBS to be unset */
					wait_for_ctrl_irq(ctrl);

					mutex_unlock(&ctrl->crit_sect);
					timer_setup(&p_slot->task_event,
						    pushbutton_helper_thread,
						    0);
					p_slot->hp_slot = hp_slot;
					p_slot->ctrl = ctrl;
/*					p_slot->physical_slot = physical_slot; */
					p_slot->task_event.expires = jiffies + 5 * HZ;   /* 5 second delay */

					dbg("add_timer p_slot = %p\n", p_slot);
					add_timer(&p_slot->task_event);
				}
				/***********POWER FAULT */
				else if (ctrl->event_queue[loop].event_type == INT_POWER_FAULT) {
					dbg("power fault\n");
				}

				ctrl->event_queue[loop].event_type = 0;

				change = 1;
			}
		}		/* End of FOR loop */
	}
}


/**
 * cpqhp_pushbutton_thread - handle pushbutton events
 * @t: pointer to struct timer_list which holds all timer-related callbacks
 *
 * Scheduled procedure to handle blocking stuff for the pushbuttons.
 * Handles all pending events and exits.
 */
void cpqhp_pushbutton_thread(struct timer_list *t)
{
	u8 hp_slot;
	struct pci_func *func;
	struct slot *p_slot = timer_container_of(p_slot, t, task_event);
	struct controller *ctrl = (struct controller *) p_slot->ctrl;

	pushbutton_pending = NULL;
	hp_slot = p_slot->hp_slot;

	if (is_slot_enabled(ctrl, hp_slot)) {
		p_slot->state = POWEROFF_STATE;
		/* power Down board */
		func = cpqhp_slot_find(p_slot->bus, p_slot->device, 0);
		dbg("In power_down_board, func = %p, ctrl = %p\n", func, ctrl);
		if (!func) {
			dbg("Error! func NULL in %s\n", __func__);
			return;
		}

		if (cpqhp_process_SS(ctrl, func) != 0) {
			amber_LED_on(ctrl, hp_slot);
			green_LED_on(ctrl, hp_slot);

			set_SOGO(ctrl);

			/* Wait for SOBS to be unset */
			wait_for_ctrl_irq(ctrl);
		}

		p_slot->state = STATIC_STATE;
	} else {
		p_slot->state = POWERON_STATE;
		/* slot is off */

		func = cpqhp_slot_find(p_slot->bus, p_slot->device, 0);
		dbg("In add_board, func = %p, ctrl = %p\n", func, ctrl);
		if (!func) {
			dbg("Error! func NULL in %s\n", __func__);
			return;
		}

		if (ctrl != NULL) {
			if (cpqhp_process_SI(ctrl, func) != 0) {
				amber_LED_on(ctrl, hp_slot);
				green_LED_off(ctrl, hp_slot);

				set_SOGO(ctrl);

				/* Wait for SOBS to be unset */
				wait_for_ctrl_irq(ctrl);
			}
		}

		p_slot->state = STATIC_STATE;
	}
}


int cpqhp_process_SI(struct controller *ctrl, struct pci_func *func)
{
	u8 device, hp_slot;
	u16 temp_word;
	u32 tempdword;
	int rc;
	struct slot *p_slot;

	tempdword = 0;

	device = func->device;
	hp_slot = device - ctrl->slot_device_offset;
	p_slot = cpqhp_find_slot(ctrl, device);

	/* Check to see if the interlock is closed */
	tempdword = readl(ctrl->hpc_reg + INT_INPUT_CLEAR);

	if (tempdword & (0x01 << hp_slot))
		return 1;

	if (func->is_a_board) {
		rc = board_replaced(func, ctrl);
	} else {
		/* add board */
		slot_remove(func);

		func = cpqhp_slot_create(ctrl->bus);
		if (func == NULL)
			return 1;

		func->bus = ctrl->bus;
		func->device = device;
		func->function = 0;
		func->configured = 0;
		func->is_a_board = 1;

		/* We have to save the presence info for these slots */
		temp_word = ctrl->ctrl_int_comp >> 16;
		func->presence_save = (temp_word >> hp_slot) & 0x01;
		func->presence_save |= (temp_word >> (hp_slot + 7)) & 0x02;

		if (ctrl->ctrl_int_comp & (0x1L << hp_slot)) {
			func->switch_save = 0;
		} else {
			func->switch_save = 0x10;
		}

		rc = board_added(func, ctrl);
		if (rc) {
			if (is_bridge(func)) {
				bridge_slot_remove(func);
			} else
				slot_remove(func);

			/* Setup slot structure with entry for empty slot */
			func = cpqhp_slot_create(ctrl->bus);

			if (func == NULL)
				return 1;

			func->bus = ctrl->bus;
			func->device = device;
			func->function = 0;
			func->configured = 0;
			func->is_a_board = 0;

			/* We have to save the presence info for these slots */
			temp_word = ctrl->ctrl_int_comp >> 16;
			func->presence_save = (temp_word >> hp_slot) & 0x01;
			func->presence_save |=
			(temp_word >> (hp_slot + 7)) & 0x02;

			if (ctrl->ctrl_int_comp & (0x1L << hp_slot)) {
				func->switch_save = 0;
			} else {
				func->switch_save = 0x10;
			}
		}
	}

	if (rc)
		dbg("%s: rc = %d\n", __func__, rc);

	return rc;
}


int cpqhp_process_SS(struct controller *ctrl, struct pci_func *func)
{
	u8 device, class_code, header_type, BCR;
	u8 index = 0;
	u8 replace_flag;
	u32 rc = 0;
	unsigned int devfn;
	struct slot *p_slot;
	struct pci_bus *pci_bus = ctrl->pci_bus;

	device = func->device;
	func = cpqhp_slot_find(ctrl->bus, device, index++);
	p_slot = cpqhp_find_slot(ctrl, device);

	/* Make sure there are no video controllers here */
	while (func && !rc) {
		pci_bus->number = func->bus;
		devfn = PCI_DEVFN(func->device, func->function);

		/* Check the Class Code */
		rc = pci_bus_read_config_byte(pci_bus, devfn, 0x0B, &class_code);
		if (rc)
			return rc;

		if (class_code == PCI_BASE_CLASS_DISPLAY) {
			/* Display/Video adapter (not supported) */
			rc = REMOVE_NOT_SUPPORTED;
		} else {
			/* See if it's a bridge */
			rc = pci_bus_read_config_byte(pci_bus, devfn, PCI_HEADER_TYPE, &header_type);
			if (rc)
				return rc;

			/* If it's a bridge, check the VGA Enable bit */
			if ((header_type & PCI_HEADER_TYPE_MASK) == PCI_HEADER_TYPE_BRIDGE) {
				rc = pci_bus_read_config_byte(pci_bus, devfn, PCI_BRIDGE_CONTROL, &BCR);
				if (rc)
					return rc;

				/* If the VGA Enable bit is set, remove isn't
				 * supported */
				if (BCR & PCI_BRIDGE_CTL_VGA)
					rc = REMOVE_NOT_SUPPORTED;
			}
		}

		func = cpqhp_slot_find(ctrl->bus, device, index++);
	}

	func = cpqhp_slot_find(ctrl->bus, device, 0);
	if ((func != NULL) && !rc) {
		/* FIXME: Replace flag should be passed into process_SS */
		replace_flag = !(ctrl->add_support);
		rc = remove_board(func, replace_flag, ctrl);
	} else if (!rc) {
		rc = 1;
	}

	return rc;
}

/**
 * switch_leds - switch the leds, go from one site to the other.
 * @ctrl: controller to use
 * @num_of_slots: number of slots to use
 * @work_LED: LED control value
 * @direction: 1 to start from the left side, 0 to start right.
 */
static void switch_leds(struct controller *ctrl, const int num_of_slots,
			u32 *work_LED, const int direction)
{
	int loop;

	for (loop = 0; loop < num_of_slots; loop++) {
		if (direction)
			*work_LED = *work_LED >> 1;
		else
			*work_LED = *work_LED << 1;
		writel(*work_LED, ctrl->hpc_reg + LED_CONTROL);

		set_SOGO(ctrl);

		/* Wait for SOGO interrupt */
		wait_for_ctrl_irq(ctrl);

		/* Get ready for next iteration */
		long_delay((2*HZ)/10);
	}
}

/**
 * cpqhp_hardware_test - runs hardware tests
 * @ctrl: target controller
 * @test_num: the number written to the "test" file in sysfs.
 *
 * For hot plug ctrl folks to play with.
 */
int cpqhp_hardware_test(struct controller *ctrl, int test_num)
{
	u32 save_LED;
	u32 work_LED;
	int loop;
	int num_of_slots;

	num_of_slots = readb(ctrl->hpc_reg + SLOT_MASK) & 0x0f;

	switch (test_num) {
	case 1:
		/* Do stuff here! */

		/* Do that funky LED thing */
		/* so we can restore them later */
		save_LED = readl(ctrl->hpc_reg + LED_CONTROL);
		work_LED = 0x01010101;
		switch_leds(ctrl, num_of_slots, &work_LED, 0);
		switch_leds(ctrl, num_of_slots, &work_LED, 1);
		switch_leds(ctrl, num_of_slots, &work_LED, 0);
		switch_leds(ctrl, num_of_slots, &work_LED, 1);

		work_LED = 0x01010000;
		writel(work_LED, ctrl->hpc_reg + LED_CONTROL);
		switch_leds(ctrl, num_of_slots, &work_LED, 0);
		switch_leds(ctrl, num_of_slots, &work_LED, 1);
		work_LED = 0x00000101;
		writel(work_LED, ctrl->hpc_reg + LED_CONTROL);
		switch_leds(ctrl, num_of_slots, &work_LED, 0);
		switch_leds(ctrl, num_of_slots, &work_LED, 1);

		work_LED = 0x01010000;
		writel(work_LED, ctrl->hpc_reg + LED_CONTROL);
		for (loop = 0; loop < num_of_slots; loop++) {
			set_SOGO(ctrl);

			/* Wait for SOGO interrupt */
			wait_for_ctrl_irq(ctrl);

			/* Get ready for next iteration */
			long_delay((3*HZ)/10);
			work_LED = work_LED >> 16;
			writel(work_LED, ctrl->hpc_reg + LED_CONTROL);

			set_SOGO(ctrl);

			/* Wait for SOGO interrupt */
			wait_for_ctrl_irq(ctrl);

			/* Get ready for next iteration */
			long_delay((3*HZ)/10);
			work_LED = work_LED << 16;
			writel(work_LED, ctrl->hpc_reg + LED_CONTROL);
			work_LED = work_LED << 1;
			writel(work_LED, ctrl->hpc_reg + LED_CONTROL);
		}

		/* put it back the way it was */
		writel(save_LED, ctrl->hpc_reg + LED_CONTROL);

		set_SOGO(ctrl);

		/* Wait for SOBS to be unset */
		wait_for_ctrl_irq(ctrl);
		break;
	case 2:
		/* Do other stuff here! */
		break;
	case 3:
		/* and more... */
		break;
	}
	return 0;
}


/**
 * configure_new_device - Configures the PCI header information of one board.
 * @ctrl: pointer to controller structure
 * @func: pointer to function structure
 * @behind_bridge: 1 if this is a recursive call, 0 if not
 * @resources: pointer to set of resource lists
 *
 * Returns 0 if success.
 */
static u32 configure_new_device(struct controller  *ctrl, struct pci_func  *func,
				 u8 behind_bridge, struct resource_lists  *resources)
{
	u8 temp_byte, function, max_functions, stop_it;
	int rc;
	u32 ID;
	struct pci_func *new_slot;
	int index;

	new_slot = func;

	dbg("%s\n", __func__);
	/* Check for Multi-function device */
	ctrl->pci_bus->number = func->bus;
	rc = pci_bus_read_config_byte(ctrl->pci_bus, PCI_DEVFN(func->device, func->function), 0x0E, &temp_byte);
	if (rc) {
		dbg("%s: rc = %d\n", __func__, rc);
		return rc;
	}

	if (temp_byte & 0x80)	/* Multi-function device */
		max_functions = 8;
	else
		max_functions = 1;

	function = 0;

	do {
		rc = configure_new_function(ctrl, new_slot, behind_bridge, resources);

		if (rc) {
			dbg("configure_new_function failed %d\n", rc);
			index = 0;

			while (new_slot) {
				new_slot = cpqhp_slot_find(new_slot->bus, new_slot->device, index++);

				if (new_slot)
					cpqhp_return_board_resources(new_slot, resources);
			}

			return rc;
		}

		function++;

		stop_it = 0;

		/* The following loop skips to the next present function
		 * and creates a board structure */

		while ((function < max_functions) && (!stop_it)) {
			pci_bus_read_config_dword(ctrl->pci_bus, PCI_DEVFN(func->device, function), 0x00, &ID);

			if (PCI_POSSIBLE_ERROR(ID)) {
				function++;
			} else {
				/* Setup slot structure. */
				new_slot = cpqhp_slot_create(func->bus);

				if (new_slot == NULL)
					return 1;

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
 * @ctrl: pointer to controller structure
 * @func: pointer to function structure
 * @behind_bridge: 1 if this is a recursive call, 0 if not
 * @resources: pointer to set of resource lists
 *
 * Calls itself recursively for bridged devices.
 * Returns 0 if success.
 */
static int configure_new_function(struct controller *ctrl, struct pci_func *func,
				   u8 behind_bridge,
				   struct resource_lists *resources)
{
	int cloop;
	u8 IRQ = 0;
	u8 temp_byte;
	u8 device;
	u8 class_code;
	u16 command;
	u16 temp_word;
	u32 temp_dword;
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
	struct pci_bus *pci_bus;
	struct resource_lists temp_resources;

	pci_bus = ctrl->pci_bus;
	pci_bus->number = func->bus;
	devfn = PCI_DEVFN(func->device, func->function);

	/* Check for Bridge */
	rc = pci_bus_read_config_byte(pci_bus, devfn, PCI_HEADER_TYPE, &temp_byte);
	if (rc)
		return rc;

	if ((temp_byte & PCI_HEADER_TYPE_MASK) == PCI_HEADER_TYPE_BRIDGE) {
		/* set Primary bus */
		dbg("set Primary bus = %d\n", func->bus);
		rc = pci_bus_write_config_byte(pci_bus, devfn, PCI_PRIMARY_BUS, func->bus);
		if (rc)
			return rc;

		/* find range of buses to use */
		dbg("find ranges of buses to use\n");
		bus_node = get_max_resource(&(resources->bus_head), 1);

		/* If we don't have any buses to allocate, we can't continue */
		if (!bus_node)
			return -ENOMEM;

		/* set Secondary bus */
		temp_byte = bus_node->base;
		dbg("set Secondary bus = %d\n", bus_node->base);
		rc = pci_bus_write_config_byte(pci_bus, devfn, PCI_SECONDARY_BUS, temp_byte);
		if (rc)
			return rc;

		/* set subordinate bus */
		temp_byte = bus_node->base + bus_node->length - 1;
		dbg("set subordinate bus = %d\n", bus_node->base + bus_node->length - 1);
		rc = pci_bus_write_config_byte(pci_bus, devfn, PCI_SUBORDINATE_BUS, temp_byte);
		if (rc)
			return rc;

		/* set subordinate Latency Timer and base Latency Timer */
		temp_byte = 0x40;
		rc = pci_bus_write_config_byte(pci_bus, devfn, PCI_SEC_LATENCY_TIMER, temp_byte);
		if (rc)
			return rc;
		rc = pci_bus_write_config_byte(pci_bus, devfn, PCI_LATENCY_TIMER, temp_byte);
		if (rc)
			return rc;

		/* set Cache Line size */
		temp_byte = 0x08;
		rc = pci_bus_write_config_byte(pci_bus, devfn, PCI_CACHE_LINE_SIZE, temp_byte);
		if (rc)
			return rc;

		/* Setup the IO, memory, and prefetchable windows */
		io_node = get_max_resource(&(resources->io_head), 0x1000);
		if (!io_node)
			return -ENOMEM;
		mem_node = get_max_resource(&(resources->mem_head), 0x100000);
		if (!mem_node)
			return -ENOMEM;
		p_mem_node = get_max_resource(&(resources->p_mem_head), 0x100000);
		if (!p_mem_node)
			return -ENOMEM;
		dbg("Setup the IO, memory, and prefetchable windows\n");
		dbg("io_node\n");
		dbg("(base, len, next) (%x, %x, %p)\n", io_node->base,
					io_node->length, io_node->next);
		dbg("mem_node\n");
		dbg("(base, len, next) (%x, %x, %p)\n", mem_node->base,
					mem_node->length, mem_node->next);
		dbg("p_mem_node\n");
		dbg("(base, len, next) (%x, %x, %p)\n", p_mem_node->base,
					p_mem_node->length, p_mem_node->next);

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
		 * for anything behind the bridge. */
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
		 * IO range registers */
		memcpy(hold_IO_node, io_node, sizeof(struct pci_resource));
		io_node->next = NULL;

		/* set IO base and Limit registers */
		temp_byte = io_node->base >> 8;
		rc = pci_bus_write_config_byte(pci_bus, devfn, PCI_IO_BASE, temp_byte);

		temp_byte = (io_node->base + io_node->length - 1) >> 8;
		rc = pci_bus_write_config_byte(pci_bus, devfn, PCI_IO_LIMIT, temp_byte);

		/* Copy the memory resources and fill in the bridge's memory
		 * range registers.
		 */
		memcpy(hold_mem_node, mem_node, sizeof(struct pci_resource));
		mem_node->next = NULL;

		/* set Mem base and Limit registers */
		temp_word = mem_node->base >> 16;
		rc = pci_bus_write_config_word(pci_bus, devfn, PCI_MEMORY_BASE, temp_word);

		temp_word = (mem_node->base + mem_node->length - 1) >> 16;
		rc = pci_bus_write_config_word(pci_bus, devfn, PCI_MEMORY_LIMIT, temp_word);

		memcpy(hold_p_mem_node, p_mem_node, sizeof(struct pci_resource));
		p_mem_node->next = NULL;

		/* set Pre Mem base and Limit registers */
		temp_word = p_mem_node->base >> 16;
		rc = pci_bus_write_config_word(pci_bus, devfn, PCI_PREF_MEMORY_BASE, temp_word);

		temp_word = (p_mem_node->base + p_mem_node->length - 1) >> 16;
		rc = pci_bus_write_config_word(pci_bus, devfn, PCI_PREF_MEMORY_LIMIT, temp_word);

		/* Adjust this to compensate for extra adjustment in first loop
		 */
		irqs.barber_pole--;

		rc = 0;

		/* Here we actually find the devices and configure them */
		for (device = 0; (device <= 0x1F) && !rc; device++) {
			irqs.barber_pole = (irqs.barber_pole + 1) & 0x03;

			ID = 0xFFFFFFFF;
			pci_bus->number = hold_bus_node->base;
			pci_bus_read_config_dword(pci_bus, PCI_DEVFN(device, 0), 0x00, &ID);
			pci_bus->number = func->bus;

			if (!PCI_POSSIBLE_ERROR(ID)) {	  /*  device present */
				/* Setup slot structure. */
				new_slot = cpqhp_slot_create(hold_bus_node->base);

				if (new_slot == NULL) {
					rc = -ENOMEM;
					continue;
				}

				new_slot->bus = hold_bus_node->base;
				new_slot->device = device;
				new_slot->function = 0;
				new_slot->is_a_board = 1;
				new_slot->status = 0;

				rc = configure_new_device(ctrl, new_slot, 1, &temp_resources);
				dbg("configure_new_device rc=0x%x\n", rc);
			}	/* End of IF (device in slot?) */
		}		/* End of FOR loop */

		if (rc)
			goto free_and_out;
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
					rc = cpqhp_set_irq(func->bus, func->device,
							   cloop + 1, irqs.interrupt[cloop]);
					if (rc)
						goto free_and_out;
				}
			}	/* end of for loop */
		}
		/* Return unused bus resources
		 * First use the temporary node to store information for
		 * the board */
		if (bus_node && temp_resources.bus_head) {
			hold_bus_node->length = bus_node->base - hold_bus_node->base;

			hold_bus_node->next = func->bus_head;
			func->bus_head = hold_bus_node;

			temp_byte = temp_resources.bus_head->base - 1;

			/* set subordinate bus */
			rc = pci_bus_write_config_byte(pci_bus, devfn, PCI_SUBORDINATE_BUS, temp_byte);

			if (temp_resources.bus_head->length == 0) {
				kfree(temp_resources.bus_head);
				temp_resources.bus_head = NULL;
			} else {
				return_resource(&(resources->bus_head), temp_resources.bus_head);
			}
		}

		/* If we have IO space available and there is some left,
		 * return the unused portion */
		if (hold_IO_node && temp_resources.io_head) {
			io_node = do_pre_bridge_resource_split(&(temp_resources.io_head),
							       &hold_IO_node, 0x1000);

			/* Check if we were able to split something off */
			if (io_node) {
				hold_IO_node->base = io_node->base + io_node->length;

				temp_byte = (hold_IO_node->base) >> 8;
				rc = pci_bus_write_config_word(pci_bus, devfn, PCI_IO_BASE, temp_byte);

				return_resource(&(resources->io_head), io_node);
			}

			io_node = do_bridge_resource_split(&(temp_resources.io_head), 0x1000);

			/* Check if we were able to split something off */
			if (io_node) {
				/* First use the temporary node to store
				 * information for the board */
				hold_IO_node->length = io_node->base - hold_IO_node->base;

				/* If we used any, add it to the board's list */
				if (hold_IO_node->length) {
					hold_IO_node->next = func->io_head;
					func->io_head = hold_IO_node;

					temp_byte = (io_node->base - 1) >> 8;
					rc = pci_bus_write_config_byte(pci_bus, devfn, PCI_IO_LIMIT, temp_byte);

					return_resource(&(resources->io_head), io_node);
				} else {
					/* it doesn't need any IO */
					temp_word = 0x0000;
					rc = pci_bus_write_config_word(pci_bus, devfn, PCI_IO_LIMIT, temp_word);

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
		 * return the unused portion */
		if (hold_mem_node && temp_resources.mem_head) {
			mem_node = do_pre_bridge_resource_split(&(temp_resources.  mem_head),
								&hold_mem_node, 0x100000);

			/* Check if we were able to split something off */
			if (mem_node) {
				hold_mem_node->base = mem_node->base + mem_node->length;

				temp_word = (hold_mem_node->base) >> 16;
				rc = pci_bus_write_config_word(pci_bus, devfn, PCI_MEMORY_BASE, temp_word);

				return_resource(&(resources->mem_head), mem_node);
			}

			mem_node = do_bridge_resource_split(&(temp_resources.mem_head), 0x100000);

			/* Check if we were able to split something off */
			if (mem_node) {
				/* First use the temporary node to store
				 * information for the board */
				hold_mem_node->length = mem_node->base - hold_mem_node->base;

				if (hold_mem_node->length) {
					hold_mem_node->next = func->mem_head;
					func->mem_head = hold_mem_node;

					/* configure end address */
					temp_word = (mem_node->base - 1) >> 16;
					rc = pci_bus_write_config_word(pci_bus, devfn, PCI_MEMORY_LIMIT, temp_word);

					/* Return unused resources to the pool */
					return_resource(&(resources->mem_head), mem_node);
				} else {
					/* it doesn't need any Mem */
					temp_word = 0x0000;
					rc = pci_bus_write_config_word(pci_bus, devfn, PCI_MEMORY_LIMIT, temp_word);

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
		/* If we have prefetchable memory space available and there
		 * is some left at the end, return the unused portion */
		if (temp_resources.p_mem_head) {
			p_mem_node = do_pre_bridge_resource_split(&(temp_resources.p_mem_head),
								  &hold_p_mem_node, 0x100000);

			/* Check if we were able to split something off */
			if (p_mem_node) {
				hold_p_mem_node->base = p_mem_node->base + p_mem_node->length;

				temp_word = (hold_p_mem_node->base) >> 16;
				rc = pci_bus_write_config_word(pci_bus, devfn, PCI_PREF_MEMORY_BASE, temp_word);

				return_resource(&(resources->p_mem_head), p_mem_node);
			}

			p_mem_node = do_bridge_resource_split(&(temp_resources.p_mem_head), 0x100000);

			/* Check if we were able to split something off */
			if (p_mem_node) {
				/* First use the temporary node to store
				 * information for the board */
				hold_p_mem_node->length = p_mem_node->base - hold_p_mem_node->base;

				/* If we used any, add it to the board's list */
				if (hold_p_mem_node->length) {
					hold_p_mem_node->next = func->p_mem_head;
					func->p_mem_head = hold_p_mem_node;

					temp_word = (p_mem_node->base - 1) >> 16;
					rc = pci_bus_write_config_word(pci_bus, devfn, PCI_PREF_MEMORY_LIMIT, temp_word);

					return_resource(&(resources->p_mem_head), p_mem_node);
				} else {
					/* it doesn't need any PMem */
					temp_word = 0x0000;
					rc = pci_bus_write_config_word(pci_bus, devfn, PCI_PREF_MEMORY_LIMIT, temp_word);

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
		 * a device */

		/* enable card */
		command = 0x0157;	/* = PCI_COMMAND_IO |
					 *   PCI_COMMAND_MEMORY |
					 *   PCI_COMMAND_MASTER |
					 *   PCI_COMMAND_INVALIDATE |
					 *   PCI_COMMAND_PARITY |
					 *   PCI_COMMAND_SERR */
		rc = pci_bus_write_config_word(pci_bus, devfn, PCI_COMMAND, command);

		/* set Bridge Control Register */
		command = 0x07;		/* = PCI_BRIDGE_CTL_PARITY |
					 *   PCI_BRIDGE_CTL_SERR |
					 *   PCI_BRIDGE_CTL_NO_ISA */
		rc = pci_bus_write_config_word(pci_bus, devfn, PCI_BRIDGE_CONTROL, command);
	} else if ((temp_byte & PCI_HEADER_TYPE_MASK) == PCI_HEADER_TYPE_NORMAL) {
		/* Standard device */
		rc = pci_bus_read_config_byte(pci_bus, devfn, 0x0B, &class_code);

		if (class_code == PCI_BASE_CLASS_DISPLAY) {
			/* Display (video) adapter (not supported) */
			return DEVICE_TYPE_NOT_SUPPORTED;
		}
		/* Figure out IO and memory needs */
		for (cloop = 0x10; cloop <= 0x24; cloop += 4) {
			temp_register = 0xFFFFFFFF;

			dbg("CND: bus=%d, devfn=%d, offset=%d\n", pci_bus->number, devfn, cloop);
			rc = pci_bus_write_config_dword(pci_bus, devfn, cloop, temp_register);

			rc = pci_bus_read_config_dword(pci_bus, devfn, cloop, &temp_register);
			dbg("CND: base = 0x%x\n", temp_register);

			if (temp_register) {	  /* If this register is implemented */
				if ((temp_register & 0x03L) == 0x01) {
					/* Map IO */

					/* set base = amount of IO space */
					base = temp_register & 0xFFFFFFFC;
					base = ~base + 1;

					dbg("CND:      length = 0x%x\n", base);
					io_node = get_io_resource(&(resources->io_head), base);
					if (!io_node)
						return -ENOMEM;
					dbg("Got io_node start = %8.8x, length = %8.8x next (%p)\n",
					    io_node->base, io_node->length, io_node->next);
					dbg("func (%p) io_head (%p)\n", func, func->io_head);

					/* allocate the resource to the board */
					base = io_node->base;
					io_node->next = func->io_head;
					func->io_head = io_node;
				} else if ((temp_register & 0x0BL) == 0x08) {
					/* Map prefetchable memory */
					base = temp_register & 0xFFFFFFF0;
					base = ~base + 1;

					dbg("CND:      length = 0x%x\n", base);
					p_mem_node = get_resource(&(resources->p_mem_head), base);

					/* allocate the resource to the board */
					if (p_mem_node) {
						base = p_mem_node->base;

						p_mem_node->next = func->p_mem_head;
						func->p_mem_head = p_mem_node;
					} else
						return -ENOMEM;
				} else if ((temp_register & 0x0BL) == 0x00) {
					/* Map memory */
					base = temp_register & 0xFFFFFFF0;
					base = ~base + 1;

					dbg("CND:      length = 0x%x\n", base);
					mem_node = get_resource(&(resources->mem_head), base);

					/* allocate the resource to the board */
					if (mem_node) {
						base = mem_node->base;

						mem_node->next = func->mem_head;
						func->mem_head = mem_node;
					} else
						return -ENOMEM;
				} else {
					/* Reserved bits or requesting space below 1M */
					return NOT_ENOUGH_RESOURCES;
				}

				rc = pci_bus_write_config_dword(pci_bus, devfn, cloop, base);

				/* Check for 64-bit base */
				if ((temp_register & 0x07L) == 0x04) {
					cloop += 4;

					/* Upper 32 bits of address always zero
					 * on today's systems */
					/* FIXME this is probably not true on
					 * Alpha and ia64??? */
					base = 0;
					rc = pci_bus_write_config_dword(pci_bus, devfn, cloop, base);
				}
			}
		}		/* End of base register loop */
		if (cpqhp_legacy_mode) {
			/* Figure out which interrupt pin this function uses */
			rc = pci_bus_read_config_byte(pci_bus, devfn,
				PCI_INTERRUPT_PIN, &temp_byte);

			/* If this function needs an interrupt and we are behind
			 * a bridge and the pin is tied to something that's
			 * already mapped, set this one the same */
			if (temp_byte && resources->irqs &&
			    (resources->irqs->valid_INT &
			     (0x01 << ((temp_byte + resources->irqs->barber_pole - 1) & 0x03)))) {
				/* We have to share with something already set up */
				IRQ = resources->irqs->interrupt[(temp_byte +
					resources->irqs->barber_pole - 1) & 0x03];
			} else {
				/* Program IRQ based on card type */
				rc = pci_bus_read_config_byte(pci_bus, devfn, 0x0B, &class_code);

				if (class_code == PCI_BASE_CLASS_STORAGE)
					IRQ = cpqhp_disk_irq;
				else
					IRQ = cpqhp_nic_irq;
			}

			/* IRQ Line */
			rc = pci_bus_write_config_byte(pci_bus, devfn, PCI_INTERRUPT_LINE, IRQ);
		}

		if (!behind_bridge) {
			rc = cpqhp_set_irq(func->bus, func->device, temp_byte, IRQ);
			if (rc)
				return 1;
		} else {
			/* TBD - this code may also belong in the other clause
			 * of this If statement */
			resources->irqs->interrupt[(temp_byte + resources->irqs->barber_pole - 1) & 0x03] = IRQ;
			resources->irqs->valid_INT |= 0x01 << (temp_byte + resources->irqs->barber_pole - 1) & 0x03;
		}

		/* Latency Timer */
		temp_byte = 0x40;
		rc = pci_bus_write_config_byte(pci_bus, devfn,
					PCI_LATENCY_TIMER, temp_byte);

		/* Cache Line size */
		temp_byte = 0x08;
		rc = pci_bus_write_config_byte(pci_bus, devfn,
					PCI_CACHE_LINE_SIZE, temp_byte);

		/* disable ROM base Address */
		temp_dword = 0x00L;
		rc = pci_bus_write_config_word(pci_bus, devfn,
					PCI_ROM_ADDRESS, temp_dword);

		/* enable card */
		temp_word = 0x0157;	/* = PCI_COMMAND_IO |
					 *   PCI_COMMAND_MEMORY |
					 *   PCI_COMMAND_MASTER |
					 *   PCI_COMMAND_INVALIDATE |
					 *   PCI_COMMAND_PARITY |
					 *   PCI_COMMAND_SERR */
		rc = pci_bus_write_config_word(pci_bus, devfn,
					PCI_COMMAND, temp_word);
	} else {		/* End of Not-A-Bridge else */
		/* It's some strange type of PCI adapter (Cardbus?) */
		return DEVICE_TYPE_NOT_SUPPORTED;
	}

	func->configured = 1;

	return 0;
free_and_out:
	cpqhp_destroy_resource_list(&temp_resources);

	return_resource(&(resources->bus_head), hold_bus_node);
	return_resource(&(resources->io_head), hold_IO_node);
	return_resource(&(resources->mem_head), hold_mem_node);
	return_resource(&(resources->p_mem_head), hold_p_mem_node);
	return rc;
}
