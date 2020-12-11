/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2020 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 */

#ifndef _NE_MISC_DEV_H_
#define _NE_MISC_DEV_H_

#include <linux/cpumask.h>
#include <linux/list.h>
#include <linux/miscdevice.h>
#include <linux/mm.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/wait.h>

#include "ne_pci_dev.h"

/**
 * struct ne_mem_region - Entry in the enclave user space memory regions list.
 * @mem_region_list_entry:	Entry in the list of enclave memory regions.
 * @memory_size:		Size of the user space memory region.
 * @nr_pages:			Number of pages that make up the memory region.
 * @pages:			Pages that make up the user space memory region.
 * @userspace_addr:		User space address of the memory region.
 */
struct ne_mem_region {
	struct list_head	mem_region_list_entry;
	u64			memory_size;
	unsigned long		nr_pages;
	struct page		**pages;
	u64			userspace_addr;
};

/**
 * struct ne_enclave - Per-enclave data used for enclave lifetime management.
 * @enclave_info_mutex :	Mutex for accessing this internal state.
 * @enclave_list_entry :	Entry in the list of created enclaves.
 * @eventq:			Wait queue used for out-of-band event notifications
 *				triggered from the PCI device event handler to
 *				the enclave process via the poll function.
 * @has_event:			Variable used to determine if the out-of-band event
 *				was triggered.
 * @max_mem_regions:		The maximum number of memory regions that can be
 *				handled by the hypervisor.
 * @mem_regions_list:		Enclave user space memory regions list.
 * @mem_size:			Enclave memory size.
 * @mm :			Enclave process abstraction mm data struct.
 * @nr_mem_regions:		Number of memory regions associated with the enclave.
 * @nr_parent_vm_cores :	The size of the threads per core array. The
 *				total number of CPU cores available on the
 *				parent / primary VM.
 * @nr_threads_per_core:	The number of threads that a full CPU core has.
 * @nr_vcpus:			Number of vcpus associated with the enclave.
 * @numa_node:			NUMA node of the enclave memory and CPUs.
 * @slot_uid:			Slot unique id mapped to the enclave.
 * @state:			Enclave state, updated during enclave lifetime.
 * @threads_per_core:		Enclave full CPU cores array, indexed by core id,
 *				consisting of cpumasks with all their threads.
 *				Full CPU cores are taken from the NE CPU pool
 *				and are available to the enclave.
 * @vcpu_ids:			Cpumask of the vCPUs that are set for the enclave.
 */
struct ne_enclave {
	struct mutex		enclave_info_mutex;
	struct list_head	enclave_list_entry;
	wait_queue_head_t	eventq;
	bool			has_event;
	u64			max_mem_regions;
	struct list_head	mem_regions_list;
	u64			mem_size;
	struct mm_struct	*mm;
	unsigned int		nr_mem_regions;
	unsigned int		nr_parent_vm_cores;
	unsigned int		nr_threads_per_core;
	unsigned int		nr_vcpus;
	int			numa_node;
	u64			slot_uid;
	u16			state;
	cpumask_var_t		*threads_per_core;
	cpumask_var_t		vcpu_ids;
};

/**
 * enum ne_state - States available for an enclave.
 * @NE_STATE_INIT:	The enclave has not been started yet.
 * @NE_STATE_RUNNING:	The enclave was started and is running as expected.
 * @NE_STATE_STOPPED:	The enclave exited without userspace interaction.
 */
enum ne_state {
	NE_STATE_INIT		= 0,
	NE_STATE_RUNNING	= 2,
	NE_STATE_STOPPED	= U16_MAX,
};

/**
 * struct ne_devs - Data structure to keep refs to the NE misc and PCI devices.
 * @ne_misc_dev:	Nitro Enclaves misc device.
 * @ne_pci_dev :	Nitro Enclaves PCI device.
 */
struct ne_devs {
	struct miscdevice	*ne_misc_dev;
	struct ne_pci_dev	*ne_pci_dev;
};

/* Nitro Enclaves (NE) data structure for keeping refs to the NE misc and PCI devices. */
extern struct ne_devs ne_devs;

#endif /* _NE_MISC_DEV_H_ */
