/*
 * ACPI PCI HotPlug Utility functions
 *
 * Copyright (C) 1995,2001 Compaq Computer Corporation
 * Copyright (C) 2001 Greg Kroah-Hartman (greg@kroah.com)
 * Copyright (C) 2001 IBM Corp.
 * Copyright (C) 2002 Hiroshi Aono (h-aono@ap.jp.nec.com)
 * Copyright (C) 2002 Takayoshi Kochi (t-kochi@bq.jp.nec.com)
 * Copyright (C) 2002 NEC Corporation
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
 * Send feedback to <gregkh@us.ibm.com>, <t-kochi@bq.jp.nec.com>
 *
 */

#include <linux/init.h>
#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/proc_fs.h>
#include <linux/sysctl.h>
#include <linux/pci.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>

#include <linux/string.h>
#include <linux/mm.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/timer.h>

#include <linux/ioctl.h>
#include <linux/fcntl.h>

#include <linux/list.h>

#include "pci_hotplug.h"
#include "acpiphp.h"

#define MY_NAME "acpiphp_res"


/*
 * sort_by_size - sort nodes by their length, smallest first
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

#if 0
/*
 * sort_by_max_size - sort nodes by their length, largest first
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
#endif

/**
 * get_io_resource - get resource for I/O ports
 *
 * this function sorts the resource list by size and then
 * returns the first node of "size" length that is not in the
 * ISA aliasing window.  If it finds a node larger than "size"
 * it will split it up.
 *
 * size must be a power of two.
 *
 * difference from get_resource is handling of ISA aliasing space.
 *
 */
struct pci_resource *acpiphp_get_io_resource (struct pci_resource **head, u32 size)
{
	struct pci_resource *prevnode;
	struct pci_resource *node;
	struct pci_resource *split_node;
	u64 temp_qword;

	if (!(*head))
		return NULL;

	if (acpiphp_resource_sort_and_combine(head))
		return NULL;

	if (sort_by_size(head))
		return NULL;

	for (node = *head; node; node = node->next) {
		if (node->length < size)
			continue;

		if (node->base & (size - 1)) {
			/* this one isn't base aligned properly
			   so we'll make a new entry and split it up */
			temp_qword = (node->base | (size-1)) + 1;

			/* Short circuit if adjusted size is too small */
			if ((node->length - (temp_qword - node->base)) < size)
				continue;

			split_node = acpiphp_make_resource(node->base, temp_qword - node->base);

			if (!split_node)
				return NULL;

			node->base = temp_qword;
			node->length -= split_node->length;

			/* Put it in the list */
			split_node->next = node->next;
			node->next = split_node;
		} /* End of non-aligned base */

		/* Don't need to check if too small since we already did */
		if (node->length > size) {
			/* this one is longer than we need
			   so we'll make a new entry and split it up */
			split_node = acpiphp_make_resource(node->base + size, node->length - size);

			if (!split_node)
				return NULL;

			node->length = size;

			/* Put it in the list */
			split_node->next = node->next;
			node->next = split_node;
		}  /* End of too big on top end */

		/* For IO make sure it's not in the ISA aliasing space */
		if ((node->base & 0x300L) && !(node->base & 0xfffff000))
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

	return node;
}


#if 0
/**
 * get_max_resource - get the largest resource
 *
 * Gets the largest node that is at least "size" big from the
 * list pointed to by head.  It aligns the node on top and bottom
 * to "size" alignment before returning it.
 */
static struct pci_resource *acpiphp_get_max_resource (struct pci_resource **head, u32 size)
{
	struct pci_resource *max;
	struct pci_resource *temp;
	struct pci_resource *split_node;
	u64 temp_qword;

	if (!(*head))
		return NULL;

	if (acpiphp_resource_sort_and_combine(head))
		return NULL;

	if (sort_by_max_size(head))
		return NULL;

	for (max = *head;max; max = max->next) {

		/* If not big enough we could probably just bail,
		   instead we'll continue to the next. */
		if (max->length < size)
			continue;

		if (max->base & (size - 1)) {
			/* this one isn't base aligned properly
			   so we'll make a new entry and split it up */
			temp_qword = (max->base | (size-1)) + 1;

			/* Short circuit if adjusted size is too small */
			if ((max->length - (temp_qword - max->base)) < size)
				continue;

			split_node = acpiphp_make_resource(max->base, temp_qword - max->base);

			if (!split_node)
				return NULL;

			max->base = temp_qword;
			max->length -= split_node->length;

			/* Put it next in the list */
			split_node->next = max->next;
			max->next = split_node;
		}

		if ((max->base + max->length) & (size - 1)) {
			/* this one isn't end aligned properly at the top
			   so we'll make a new entry and split it up */
			temp_qword = ((max->base + max->length) & ~(size - 1));

			split_node = acpiphp_make_resource(temp_qword,
							   max->length + max->base - temp_qword);

			if (!split_node)
				return NULL;

			max->length -= split_node->length;

			/* Put it in the list */
			split_node->next = max->next;
			max->next = split_node;
		}

		/* Make sure it didn't shrink too much when we aligned it */
		if (max->length < size)
			continue;

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
		return max;
	}

	/* If we get here, we couldn't find one */
	return NULL;
}
#endif

/**
 * get_resource - get resource (mem, pfmem)
 *
 * this function sorts the resource list by size and then
 * returns the first node of "size" length.  If it finds a node
 * larger than "size" it will split it up.
 *
 * size must be a power of two.
 *
 */
struct pci_resource *acpiphp_get_resource (struct pci_resource **head, u32 size)
{
	struct pci_resource *prevnode;
	struct pci_resource *node;
	struct pci_resource *split_node;
	u64 temp_qword;

	if (!(*head))
		return NULL;

	if (acpiphp_resource_sort_and_combine(head))
		return NULL;

	if (sort_by_size(head))
		return NULL;

	for (node = *head; node; node = node->next) {
		dbg("%s: req_size =%x node=%p, base=%x, length=%x\n",
		    __FUNCTION__, size, node, (u32)node->base, node->length);
		if (node->length < size)
			continue;

		if (node->base & (size - 1)) {
			dbg("%s: not aligned\n", __FUNCTION__);
			/* this one isn't base aligned properly
			   so we'll make a new entry and split it up */
			temp_qword = (node->base | (size-1)) + 1;

			/* Short circuit if adjusted size is too small */
			if ((node->length - (temp_qword - node->base)) < size)
				continue;

			split_node = acpiphp_make_resource(node->base, temp_qword - node->base);

			if (!split_node)
				return NULL;

			node->base = temp_qword;
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
			split_node = acpiphp_make_resource(node->base + size, node->length - size);

			if (!split_node)
				return NULL;

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
	return node;
}

/**
 * get_resource_with_base - get resource with specific base address
 *
 * this function
 * returns the first node of "size" length located at specified base address.
 * If it finds a node larger than "size" it will split it up.
 *
 * size must be a power of two.
 *
 */
struct pci_resource *acpiphp_get_resource_with_base (struct pci_resource **head, u64 base, u32 size)
{
	struct pci_resource *prevnode;
	struct pci_resource *node;
	struct pci_resource *split_node;
	u64 temp_qword;

	if (!(*head))
		return NULL;

	if (acpiphp_resource_sort_and_combine(head))
		return NULL;

	for (node = *head; node; node = node->next) {
		dbg(": 1st req_base=%x req_size =%x node=%p, base=%x, length=%x\n",
		    (u32)base, size, node, (u32)node->base, node->length);
		if (node->base > base)
			continue;

		if ((node->base + node->length) < (base + size))
			continue;

		if (node->base < base) {
			dbg(": split 1\n");
			/* this one isn't base aligned properly
			   so we'll make a new entry and split it up */
			temp_qword = base;

			/* Short circuit if adjusted size is too small */
			if ((node->length - (temp_qword - node->base)) < size)
				continue;

			split_node = acpiphp_make_resource(node->base, temp_qword - node->base);

			if (!split_node)
				return NULL;

			node->base = temp_qword;
			node->length -= split_node->length;

			/* Put it in the list */
			split_node->next = node->next;
			node->next = split_node;
		}

		dbg(": 2nd req_base=%x req_size =%x node=%p, base=%x, length=%x\n",
		    (u32)base, size, node, (u32)node->base, node->length);

		/* Don't need to check if too small since we already did */
		if (node->length > size) {
			dbg(": split 2\n");
			/* this one is longer than we need
			   so we'll make a new entry and split it up */
			split_node = acpiphp_make_resource(node->base + size, node->length - size);

			if (!split_node)
				return NULL;

			node->length = size;

			/* Put it in the list */
			split_node->next = node->next;
			node->next = split_node;
		}  /* End of too big on top end */

		dbg(": got one!!!\n");
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
	return node;
}


/**
 * acpiphp_resource_sort_and_combine
 *
 * Sorts all of the nodes in the list in ascending order by
 * their base addresses.  Also does garbage collection by
 * combining adjacent nodes.
 *
 * returns 0 if success
 */
int acpiphp_resource_sort_and_combine (struct pci_resource **head)
{
	struct pci_resource *node1;
	struct pci_resource *node2;
	int out_of_order = 1;

	if (!(*head))
		return 1;

	dbg("*head->next = %p\n",(*head)->next);

	if (!(*head)->next)
		return 0;	/* only one item on the list, already sorted! */

	dbg("*head->base = 0x%x\n",(u32)(*head)->base);
	dbg("*head->next->base = 0x%x\n", (u32)(*head)->next->base);
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


/**
 * acpiphp_make_resource - make resource structure
 * @base: base address of a resource
 * @length: length of a resource
 */
struct pci_resource *acpiphp_make_resource (u64 base, u32 length)
{
	struct pci_resource *res;

	res = kmalloc(sizeof(struct pci_resource), GFP_KERNEL);
	if (res) {
		memset(res, 0, sizeof(struct pci_resource));
		res->base = base;
		res->length = length;
	}

	return res;
}


/**
 * acpiphp_move_resource - move linked resources from one to another
 * @from: head of linked resource list
 * @to: head of linked resource list
 */
void acpiphp_move_resource (struct pci_resource **from, struct pci_resource **to)
{
	struct pci_resource *tmp;

	while (*from) {
		tmp = (*from)->next;
		(*from)->next = *to;
		*to = *from;
		*from = tmp;
	}

	/* *from = NULL is guaranteed */
}


/**
 * acpiphp_free_resource - free all linked resources
 * @res: head of linked resource list
 */
void acpiphp_free_resource (struct pci_resource **res)
{
	struct pci_resource *tmp;

	while (*res) {
		tmp = (*res)->next;
		kfree(*res);
		*res = tmp;
	}

	/* *res = NULL is guaranteed */
}


/* debug support functions;  will go away sometime :) */
static void dump_resource(struct pci_resource *head)
{
	struct pci_resource *p;
	int cnt;

	p = head;
	cnt = 0;

	while (p) {
		dbg("[%02d] %08x - %08x\n",
		    cnt++, (u32)p->base, (u32)p->base + p->length - 1);
		p = p->next;
	}
}

void acpiphp_dump_resource(struct acpiphp_bridge *bridge)
{
	dbg("I/O resource:\n");
	dump_resource(bridge->io_head);
	dbg("MEM resource:\n");
	dump_resource(bridge->mem_head);
	dbg("PMEM resource:\n");
	dump_resource(bridge->p_mem_head);
	dbg("BUS resource:\n");
	dump_resource(bridge->bus_head);
}

void acpiphp_dump_func_resource(struct acpiphp_func *func)
{
	dbg("I/O resource:\n");
	dump_resource(func->io_head);
	dbg("MEM resource:\n");
	dump_resource(func->mem_head);
	dbg("PMEM resource:\n");
	dump_resource(func->p_mem_head);
	dbg("BUS resource:\n");
	dump_resource(func->bus_head);
}
