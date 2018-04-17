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
 *
 */

#ifndef	__HMM_VM_H__
#define	__HMM_VM_H__

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/mutex.h>
#include <linux/list.h>

struct hmm_vm {
	unsigned int start;
	unsigned int pgnr;
	unsigned int size;
	struct list_head vm_node_list;
	spinlock_t lock;
	struct kmem_cache *cache;
};

struct hmm_vm_node {
	struct list_head list;
	unsigned int start;
	unsigned int pgnr;
	unsigned int size;
	struct hmm_vm *vm;
};
#define	ISP_VM_START	0x0
#define	ISP_VM_SIZE	(0x7FFFFFFF)	/* 2G address space */
#define	ISP_PTR_NULL	NULL

int hmm_vm_init(struct hmm_vm *vm, unsigned int start,
		unsigned int size);

void hmm_vm_clean(struct hmm_vm *vm);

struct hmm_vm_node *hmm_vm_alloc_node(struct hmm_vm *vm,
		unsigned int pgnr);

void hmm_vm_free_node(struct hmm_vm_node *node);

struct hmm_vm_node *hmm_vm_find_node_start(struct hmm_vm *vm,
		unsigned int addr);

struct hmm_vm_node *hmm_vm_find_node_in_range(struct hmm_vm *vm,
		unsigned int addr);

#endif
