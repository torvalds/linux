/*
 * Support for Medifield PNW Camera Imaging ISP subsystem.
 *
 * Copyright (c) 2010 Intel Corporation. All Rights Reserved.
 *
 * Copyright (c) 2010 Silicon Hive www.siliconhive.com.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 */
/*
 * This file contains function for ISP virtual address management in ISP driver
 */
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <asm/page.h>

#include "atomisp_internal.h"
#include "mmu/isp_mmu.h"
#include "hmm/hmm_vm.h"
#include "hmm/hmm_common.h"

static unsigned int vm_node_end(unsigned int start, unsigned int pgnr)
{
	return start + pgnr_to_size(pgnr);
}

static int addr_in_vm_node(unsigned int addr,
		struct hmm_vm_node *node)
{
	return (addr >= node->start) && (addr < (node->start + node->size));
}

int hmm_vm_init(struct hmm_vm *vm, unsigned int start,
		unsigned int size)
{
	if (!vm)
		return -1;

	vm->start = start;
	vm->pgnr = size_to_pgnr_ceil(size);
	vm->size = pgnr_to_size(vm->pgnr);

	INIT_LIST_HEAD(&vm->vm_node_list);
	spin_lock_init(&vm->lock);
	vm->cache = kmem_cache_create("atomisp_vm", sizeof(struct hmm_vm_node),
				      0, 0, NULL);

	return vm->cache != NULL ? 0 : -ENOMEM;
}

void hmm_vm_clean(struct hmm_vm *vm)
{
	struct hmm_vm_node *node, *tmp;
	struct list_head new_head;

	if (!vm)
		return;

	spin_lock(&vm->lock);
	list_replace_init(&vm->vm_node_list, &new_head);
	spin_unlock(&vm->lock);

	list_for_each_entry_safe(node, tmp, &new_head, list) {
		list_del(&node->list);
		kmem_cache_free(vm->cache, node);
	}

	kmem_cache_destroy(vm->cache);
}

static struct hmm_vm_node *alloc_hmm_vm_node(unsigned int pgnr,
					     struct hmm_vm *vm)
{
	struct hmm_vm_node *node;

	node = kmem_cache_alloc(vm->cache, GFP_KERNEL);
	if (!node) {
		dev_err(atomisp_dev, "out of memory.\n");
		return NULL;
	}

	INIT_LIST_HEAD(&node->list);
	node->pgnr = pgnr;
	node->size = pgnr_to_size(pgnr);
	node->vm = vm;

	return node;
}

struct hmm_vm_node *hmm_vm_alloc_node(struct hmm_vm *vm, unsigned int pgnr)
{
	struct list_head *head;
	struct hmm_vm_node *node, *cur, *next;
	unsigned int vm_start, vm_end;
	unsigned int addr;
	unsigned int size;

	if (!vm)
		return NULL;

	vm_start = vm->start;
	vm_end = vm_node_end(vm->start, vm->pgnr);
	size = pgnr_to_size(pgnr);

	addr = vm_start;
	head = &vm->vm_node_list;

	node = alloc_hmm_vm_node(pgnr, vm);
	if (!node) {
		dev_err(atomisp_dev, "no memory to allocate hmm vm node.\n");
		return NULL;
	}

	spin_lock(&vm->lock);
	/*
	 * if list is empty, the loop code will not be executed.
	 */
	list_for_each_entry(cur, head, list) {
		/* Add gap between vm areas as helper to not hide overflow */
		addr = PAGE_ALIGN(vm_node_end(cur->start, cur->pgnr) + 1);

		if (list_is_last(&cur->list, head)) {
			if (addr + size > vm_end) {
				/* vm area does not have space anymore */
				spin_unlock(&vm->lock);
				kmem_cache_free(vm->cache, node);
				dev_err(atomisp_dev,
					  "no enough virtual address space.\n");
				return NULL;
			}

			/* We still have vm space to add new node to tail */
			break;
		}

		next = list_entry(cur->list.next, struct hmm_vm_node, list);
		if ((next->start - addr) > size)
			break;
	}
	node->start = addr;
	node->vm = vm;
	list_add(&node->list, &cur->list);
	spin_unlock(&vm->lock);

	return node;
}

void hmm_vm_free_node(struct hmm_vm_node *node)
{
	struct hmm_vm *vm;

	if (!node)
		return;

	vm = node->vm;

	spin_lock(&vm->lock);
	list_del(&node->list);
	spin_unlock(&vm->lock);

	kmem_cache_free(vm->cache, node);
}

struct hmm_vm_node *hmm_vm_find_node_start(struct hmm_vm *vm, unsigned int addr)
{
	struct hmm_vm_node *node;

	if (!vm)
		return NULL;

	spin_lock(&vm->lock);

	list_for_each_entry(node, &vm->vm_node_list, list) {
		if (node->start == addr) {
			spin_unlock(&vm->lock);
			return node;
		}
	}

	spin_unlock(&vm->lock);
	return NULL;
}

struct hmm_vm_node *hmm_vm_find_node_in_range(struct hmm_vm *vm,
					      unsigned int addr)
{
	struct hmm_vm_node *node;

	if (!vm)
		return NULL;

	spin_lock(&vm->lock);

	list_for_each_entry(node, &vm->vm_node_list, list) {
		if (addr_in_vm_node(addr, node)) {
			spin_unlock(&vm->lock);
			return node;
		}
	}

	spin_unlock(&vm->lock);
	return NULL;
}
