/*
 * Copyright(c) 2015 - 2018 Intel Corporation.
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * BSD LICENSE
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  - Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  - Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  - Neither the name of Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
#include <linux/topology.h>
#include <linux/cpumask.h>
#include <linux/module.h>
#include <linux/interrupt.h>

#include "hfi.h"
#include "affinity.h"
#include "sdma.h"
#include "trace.h"

struct hfi1_affinity_node_list node_affinity = {
	.list = LIST_HEAD_INIT(node_affinity.list),
	.lock = __MUTEX_INITIALIZER(node_affinity.lock)
};

/* Name of IRQ types, indexed by enum irq_type */
static const char * const irq_type_names[] = {
	"SDMA",
	"RCVCTXT",
	"GENERAL",
	"OTHER",
};

/* Per NUMA node count of HFI devices */
static unsigned int *hfi1_per_node_cntr;

static inline void init_cpu_mask_set(struct cpu_mask_set *set)
{
	cpumask_clear(&set->mask);
	cpumask_clear(&set->used);
	set->gen = 0;
}

/* Increment generation of CPU set if needed */
static void _cpu_mask_set_gen_inc(struct cpu_mask_set *set)
{
	if (cpumask_equal(&set->mask, &set->used)) {
		/*
		 * We've used up all the CPUs, bump up the generation
		 * and reset the 'used' map
		 */
		set->gen++;
		cpumask_clear(&set->used);
	}
}

static void _cpu_mask_set_gen_dec(struct cpu_mask_set *set)
{
	if (cpumask_empty(&set->used) && set->gen) {
		set->gen--;
		cpumask_copy(&set->used, &set->mask);
	}
}

/* Get the first CPU from the list of unused CPUs in a CPU set data structure */
static int cpu_mask_set_get_first(struct cpu_mask_set *set, cpumask_var_t diff)
{
	int cpu;

	if (!diff || !set)
		return -EINVAL;

	_cpu_mask_set_gen_inc(set);

	/* Find out CPUs left in CPU mask */
	cpumask_andnot(diff, &set->mask, &set->used);

	cpu = cpumask_first(diff);
	if (cpu >= nr_cpu_ids) /* empty */
		cpu = -EINVAL;
	else
		cpumask_set_cpu(cpu, &set->used);

	return cpu;
}

static void cpu_mask_set_put(struct cpu_mask_set *set, int cpu)
{
	if (!set)
		return;

	cpumask_clear_cpu(cpu, &set->used);
	_cpu_mask_set_gen_dec(set);
}

/* Initialize non-HT cpu cores mask */
void init_real_cpu_mask(void)
{
	int possible, curr_cpu, i, ht;

	cpumask_clear(&node_affinity.real_cpu_mask);

	/* Start with cpu online mask as the real cpu mask */
	cpumask_copy(&node_affinity.real_cpu_mask, cpu_online_mask);

	/*
	 * Remove HT cores from the real cpu mask.  Do this in two steps below.
	 */
	possible = cpumask_weight(&node_affinity.real_cpu_mask);
	ht = cpumask_weight(topology_sibling_cpumask(
				cpumask_first(&node_affinity.real_cpu_mask)));
	/*
	 * Step 1.  Skip over the first N HT siblings and use them as the
	 * "real" cores.  Assumes that HT cores are not enumerated in
	 * succession (except in the single core case).
	 */
	curr_cpu = cpumask_first(&node_affinity.real_cpu_mask);
	for (i = 0; i < possible / ht; i++)
		curr_cpu = cpumask_next(curr_cpu, &node_affinity.real_cpu_mask);
	/*
	 * Step 2.  Remove the remaining HT siblings.  Use cpumask_next() to
	 * skip any gaps.
	 */
	for (; i < possible; i++) {
		cpumask_clear_cpu(curr_cpu, &node_affinity.real_cpu_mask);
		curr_cpu = cpumask_next(curr_cpu, &node_affinity.real_cpu_mask);
	}
}

int node_affinity_init(void)
{
	int node;
	struct pci_dev *dev = NULL;
	const struct pci_device_id *ids = hfi1_pci_tbl;

	cpumask_clear(&node_affinity.proc.used);
	cpumask_copy(&node_affinity.proc.mask, cpu_online_mask);

	node_affinity.proc.gen = 0;
	node_affinity.num_core_siblings =
				cpumask_weight(topology_sibling_cpumask(
					cpumask_first(&node_affinity.proc.mask)
					));
	node_affinity.num_possible_nodes = num_possible_nodes();
	node_affinity.num_online_nodes = num_online_nodes();
	node_affinity.num_online_cpus = num_online_cpus();

	/*
	 * The real cpu mask is part of the affinity struct but it has to be
	 * initialized early. It is needed to calculate the number of user
	 * contexts in set_up_context_variables().
	 */
	init_real_cpu_mask();

	hfi1_per_node_cntr = kcalloc(node_affinity.num_possible_nodes,
				     sizeof(*hfi1_per_node_cntr), GFP_KERNEL);
	if (!hfi1_per_node_cntr)
		return -ENOMEM;

	while (ids->vendor) {
		dev = NULL;
		while ((dev = pci_get_device(ids->vendor, ids->device, dev))) {
			node = pcibus_to_node(dev->bus);
			if (node < 0)
				goto out;

			hfi1_per_node_cntr[node]++;
		}
		ids++;
	}

	return 0;

out:
	/*
	 * Invalid PCI NUMA node information found, note it, and populate
	 * our database 1:1.
	 */
	pr_err("HFI: Invalid PCI NUMA node. Performance may be affected\n");
	pr_err("HFI: System BIOS may need to be upgraded\n");
	for (node = 0; node < node_affinity.num_possible_nodes; node++)
		hfi1_per_node_cntr[node] = 1;

	return 0;
}

static void node_affinity_destroy(struct hfi1_affinity_node *entry)
{
	free_percpu(entry->comp_vect_affinity);
	kfree(entry);
}

void node_affinity_destroy_all(void)
{
	struct list_head *pos, *q;
	struct hfi1_affinity_node *entry;

	mutex_lock(&node_affinity.lock);
	list_for_each_safe(pos, q, &node_affinity.list) {
		entry = list_entry(pos, struct hfi1_affinity_node,
				   list);
		list_del(pos);
		node_affinity_destroy(entry);
	}
	mutex_unlock(&node_affinity.lock);
	kfree(hfi1_per_node_cntr);
}

static struct hfi1_affinity_node *node_affinity_allocate(int node)
{
	struct hfi1_affinity_node *entry;

	entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry)
		return NULL;
	entry->node = node;
	entry->comp_vect_affinity = alloc_percpu(u16);
	INIT_LIST_HEAD(&entry->list);

	return entry;
}

/*
 * It appends an entry to the list.
 * It *must* be called with node_affinity.lock held.
 */
static void node_affinity_add_tail(struct hfi1_affinity_node *entry)
{
	list_add_tail(&entry->list, &node_affinity.list);
}

/* It must be called with node_affinity.lock held */
static struct hfi1_affinity_node *node_affinity_lookup(int node)
{
	struct list_head *pos;
	struct hfi1_affinity_node *entry;

	list_for_each(pos, &node_affinity.list) {
		entry = list_entry(pos, struct hfi1_affinity_node, list);
		if (entry->node == node)
			return entry;
	}

	return NULL;
}

static int per_cpu_affinity_get(cpumask_var_t possible_cpumask,
				u16 __percpu *comp_vect_affinity)
{
	int curr_cpu;
	u16 cntr;
	u16 prev_cntr;
	int ret_cpu;

	if (!possible_cpumask) {
		ret_cpu = -EINVAL;
		goto fail;
	}

	if (!comp_vect_affinity) {
		ret_cpu = -EINVAL;
		goto fail;
	}

	ret_cpu = cpumask_first(possible_cpumask);
	if (ret_cpu >= nr_cpu_ids) {
		ret_cpu = -EINVAL;
		goto fail;
	}

	prev_cntr = *per_cpu_ptr(comp_vect_affinity, ret_cpu);
	for_each_cpu(curr_cpu, possible_cpumask) {
		cntr = *per_cpu_ptr(comp_vect_affinity, curr_cpu);

		if (cntr < prev_cntr) {
			ret_cpu = curr_cpu;
			prev_cntr = cntr;
		}
	}

	*per_cpu_ptr(comp_vect_affinity, ret_cpu) += 1;

fail:
	return ret_cpu;
}

static int per_cpu_affinity_put_max(cpumask_var_t possible_cpumask,
				    u16 __percpu *comp_vect_affinity)
{
	int curr_cpu;
	int max_cpu;
	u16 cntr;
	u16 prev_cntr;

	if (!possible_cpumask)
		return -EINVAL;

	if (!comp_vect_affinity)
		return -EINVAL;

	max_cpu = cpumask_first(possible_cpumask);
	if (max_cpu >= nr_cpu_ids)
		return -EINVAL;

	prev_cntr = *per_cpu_ptr(comp_vect_affinity, max_cpu);
	for_each_cpu(curr_cpu, possible_cpumask) {
		cntr = *per_cpu_ptr(comp_vect_affinity, curr_cpu);

		if (cntr > prev_cntr) {
			max_cpu = curr_cpu;
			prev_cntr = cntr;
		}
	}

	*per_cpu_ptr(comp_vect_affinity, max_cpu) -= 1;

	return max_cpu;
}

/*
 * Non-interrupt CPUs are used first, then interrupt CPUs.
 * Two already allocated cpu masks must be passed.
 */
static int _dev_comp_vect_cpu_get(struct hfi1_devdata *dd,
				  struct hfi1_affinity_node *entry,
				  cpumask_var_t non_intr_cpus,
				  cpumask_var_t available_cpus)
	__must_hold(&node_affinity.lock)
{
	int cpu;
	struct cpu_mask_set *set = dd->comp_vect;

	lockdep_assert_held(&node_affinity.lock);
	if (!non_intr_cpus) {
		cpu = -1;
		goto fail;
	}

	if (!available_cpus) {
		cpu = -1;
		goto fail;
	}

	/* Available CPUs for pinning completion vectors */
	_cpu_mask_set_gen_inc(set);
	cpumask_andnot(available_cpus, &set->mask, &set->used);

	/* Available CPUs without SDMA engine interrupts */
	cpumask_andnot(non_intr_cpus, available_cpus,
		       &entry->def_intr.used);

	/* If there are non-interrupt CPUs available, use them first */
	if (!cpumask_empty(non_intr_cpus))
		cpu = cpumask_first(non_intr_cpus);
	else /* Otherwise, use interrupt CPUs */
		cpu = cpumask_first(available_cpus);

	if (cpu >= nr_cpu_ids) { /* empty */
		cpu = -1;
		goto fail;
	}
	cpumask_set_cpu(cpu, &set->used);

fail:
	return cpu;
}

static void _dev_comp_vect_cpu_put(struct hfi1_devdata *dd, int cpu)
{
	struct cpu_mask_set *set = dd->comp_vect;

	if (cpu < 0)
		return;

	cpu_mask_set_put(set, cpu);
}

/* _dev_comp_vect_mappings_destroy() is reentrant */
static void _dev_comp_vect_mappings_destroy(struct hfi1_devdata *dd)
{
	int i, cpu;

	if (!dd->comp_vect_mappings)
		return;

	for (i = 0; i < dd->comp_vect_possible_cpus; i++) {
		cpu = dd->comp_vect_mappings[i];
		_dev_comp_vect_cpu_put(dd, cpu);
		dd->comp_vect_mappings[i] = -1;
		hfi1_cdbg(AFFINITY,
			  "[%s] Release CPU %d from completion vector %d",
			  rvt_get_ibdev_name(&(dd)->verbs_dev.rdi), cpu, i);
	}

	kfree(dd->comp_vect_mappings);
	dd->comp_vect_mappings = NULL;
}

/*
 * This function creates the table for looking up CPUs for completion vectors.
 * num_comp_vectors needs to have been initilized before calling this function.
 */
static int _dev_comp_vect_mappings_create(struct hfi1_devdata *dd,
					  struct hfi1_affinity_node *entry)
	__must_hold(&node_affinity.lock)
{
	int i, cpu, ret;
	cpumask_var_t non_intr_cpus;
	cpumask_var_t available_cpus;

	lockdep_assert_held(&node_affinity.lock);

	if (!zalloc_cpumask_var(&non_intr_cpus, GFP_KERNEL))
		return -ENOMEM;

	if (!zalloc_cpumask_var(&available_cpus, GFP_KERNEL)) {
		free_cpumask_var(non_intr_cpus);
		return -ENOMEM;
	}

	dd->comp_vect_mappings = kcalloc(dd->comp_vect_possible_cpus,
					 sizeof(*dd->comp_vect_mappings),
					 GFP_KERNEL);
	if (!dd->comp_vect_mappings) {
		ret = -ENOMEM;
		goto fail;
	}
	for (i = 0; i < dd->comp_vect_possible_cpus; i++)
		dd->comp_vect_mappings[i] = -1;

	for (i = 0; i < dd->comp_vect_possible_cpus; i++) {
		cpu = _dev_comp_vect_cpu_get(dd, entry, non_intr_cpus,
					     available_cpus);
		if (cpu < 0) {
			ret = -EINVAL;
			goto fail;
		}

		dd->comp_vect_mappings[i] = cpu;
		hfi1_cdbg(AFFINITY,
			  "[%s] Completion Vector %d -> CPU %d",
			  rvt_get_ibdev_name(&(dd)->verbs_dev.rdi), i, cpu);
	}

	free_cpumask_var(available_cpus);
	free_cpumask_var(non_intr_cpus);
	return 0;

fail:
	free_cpumask_var(available_cpus);
	free_cpumask_var(non_intr_cpus);
	_dev_comp_vect_mappings_destroy(dd);

	return ret;
}

int hfi1_comp_vectors_set_up(struct hfi1_devdata *dd)
{
	int ret;
	struct hfi1_affinity_node *entry;

	mutex_lock(&node_affinity.lock);
	entry = node_affinity_lookup(dd->node);
	if (!entry) {
		ret = -EINVAL;
		goto unlock;
	}
	ret = _dev_comp_vect_mappings_create(dd, entry);
unlock:
	mutex_unlock(&node_affinity.lock);

	return ret;
}

void hfi1_comp_vectors_clean_up(struct hfi1_devdata *dd)
{
	_dev_comp_vect_mappings_destroy(dd);
}

int hfi1_comp_vect_mappings_lookup(struct rvt_dev_info *rdi, int comp_vect)
{
	struct hfi1_ibdev *verbs_dev = dev_from_rdi(rdi);
	struct hfi1_devdata *dd = dd_from_dev(verbs_dev);

	if (!dd->comp_vect_mappings)
		return -EINVAL;
	if (comp_vect >= dd->comp_vect_possible_cpus)
		return -EINVAL;

	return dd->comp_vect_mappings[comp_vect];
}

/*
 * It assumes dd->comp_vect_possible_cpus is available.
 */
static int _dev_comp_vect_cpu_mask_init(struct hfi1_devdata *dd,
					struct hfi1_affinity_node *entry,
					bool first_dev_init)
	__must_hold(&node_affinity.lock)
{
	int i, j, curr_cpu;
	int possible_cpus_comp_vect = 0;
	struct cpumask *dev_comp_vect_mask = &dd->comp_vect->mask;

	lockdep_assert_held(&node_affinity.lock);
	/*
	 * If there's only one CPU available for completion vectors, then
	 * there will only be one completion vector available. Othewise,
	 * the number of completion vector available will be the number of
	 * available CPUs divide it by the number of devices in the
	 * local NUMA node.
	 */
	if (cpumask_weight(&entry->comp_vect_mask) == 1) {
		possible_cpus_comp_vect = 1;
		dd_dev_warn(dd,
			    "Number of kernel receive queues is too large for completion vector affinity to be effective\n");
	} else {
		possible_cpus_comp_vect +=
			cpumask_weight(&entry->comp_vect_mask) /
				       hfi1_per_node_cntr[dd->node];

		/*
		 * If the completion vector CPUs available doesn't divide
		 * evenly among devices, then the first device device to be
		 * initialized gets an extra CPU.
		 */
		if (first_dev_init &&
		    cpumask_weight(&entry->comp_vect_mask) %
		    hfi1_per_node_cntr[dd->node] != 0)
			possible_cpus_comp_vect++;
	}

	dd->comp_vect_possible_cpus = possible_cpus_comp_vect;

	/* Reserving CPUs for device completion vector */
	for (i = 0; i < dd->comp_vect_possible_cpus; i++) {
		curr_cpu = per_cpu_affinity_get(&entry->comp_vect_mask,
						entry->comp_vect_affinity);
		if (curr_cpu < 0)
			goto fail;

		cpumask_set_cpu(curr_cpu, dev_comp_vect_mask);
	}

	hfi1_cdbg(AFFINITY,
		  "[%s] Completion vector affinity CPU set(s) %*pbl",
		  rvt_get_ibdev_name(&(dd)->verbs_dev.rdi),
		  cpumask_pr_args(dev_comp_vect_mask));

	return 0;

fail:
	for (j = 0; j < i; j++)
		per_cpu_affinity_put_max(&entry->comp_vect_mask,
					 entry->comp_vect_affinity);

	return curr_cpu;
}

/*
 * It assumes dd->comp_vect_possible_cpus is available.
 */
static void _dev_comp_vect_cpu_mask_clean_up(struct hfi1_devdata *dd,
					     struct hfi1_affinity_node *entry)
	__must_hold(&node_affinity.lock)
{
	int i, cpu;

	lockdep_assert_held(&node_affinity.lock);
	if (!dd->comp_vect_possible_cpus)
		return;

	for (i = 0; i < dd->comp_vect_possible_cpus; i++) {
		cpu = per_cpu_affinity_put_max(&dd->comp_vect->mask,
					       entry->comp_vect_affinity);
		/* Clearing CPU in device completion vector cpu mask */
		if (cpu >= 0)
			cpumask_clear_cpu(cpu, &dd->comp_vect->mask);
	}

	dd->comp_vect_possible_cpus = 0;
}

/*
 * Interrupt affinity.
 *
 * non-rcv avail gets a default mask that
 * starts as possible cpus with threads reset
 * and each rcv avail reset.
 *
 * rcv avail gets node relative 1 wrapping back
 * to the node relative 1 as necessary.
 *
 */
int hfi1_dev_affinity_init(struct hfi1_devdata *dd)
{
	int node = pcibus_to_node(dd->pcidev->bus);
	struct hfi1_affinity_node *entry;
	const struct cpumask *local_mask;
	int curr_cpu, possible, i, ret;
	bool new_entry = false;

	/*
	 * If the BIOS does not have the NUMA node information set, select
	 * NUMA 0 so we get consistent performance.
	 */
	if (node < 0) {
		dd_dev_err(dd, "Invalid PCI NUMA node. Performance may be affected\n");
		node = 0;
	}
	dd->node = node;

	local_mask = cpumask_of_node(dd->node);
	if (cpumask_first(local_mask) >= nr_cpu_ids)
		local_mask = topology_core_cpumask(0);

	mutex_lock(&node_affinity.lock);
	entry = node_affinity_lookup(dd->node);

	/*
	 * If this is the first time this NUMA node's affinity is used,
	 * create an entry in the global affinity structure and initialize it.
	 */
	if (!entry) {
		entry = node_affinity_allocate(node);
		if (!entry) {
			dd_dev_err(dd,
				   "Unable to allocate global affinity node\n");
			ret = -ENOMEM;
			goto fail;
		}
		new_entry = true;

		init_cpu_mask_set(&entry->def_intr);
		init_cpu_mask_set(&entry->rcv_intr);
		cpumask_clear(&entry->comp_vect_mask);
		cpumask_clear(&entry->general_intr_mask);
		/* Use the "real" cpu mask of this node as the default */
		cpumask_and(&entry->def_intr.mask, &node_affinity.real_cpu_mask,
			    local_mask);

		/* fill in the receive list */
		possible = cpumask_weight(&entry->def_intr.mask);
		curr_cpu = cpumask_first(&entry->def_intr.mask);

		if (possible == 1) {
			/* only one CPU, everyone will use it */
			cpumask_set_cpu(curr_cpu, &entry->rcv_intr.mask);
			cpumask_set_cpu(curr_cpu, &entry->general_intr_mask);
		} else {
			/*
			 * The general/control context will be the first CPU in
			 * the default list, so it is removed from the default
			 * list and added to the general interrupt list.
			 */
			cpumask_clear_cpu(curr_cpu, &entry->def_intr.mask);
			cpumask_set_cpu(curr_cpu, &entry->general_intr_mask);
			curr_cpu = cpumask_next(curr_cpu,
						&entry->def_intr.mask);

			/*
			 * Remove the remaining kernel receive queues from
			 * the default list and add them to the receive list.
			 */
			for (i = 0;
			     i < (dd->n_krcv_queues - 1) *
				  hfi1_per_node_cntr[dd->node];
			     i++) {
				cpumask_clear_cpu(curr_cpu,
						  &entry->def_intr.mask);
				cpumask_set_cpu(curr_cpu,
						&entry->rcv_intr.mask);
				curr_cpu = cpumask_next(curr_cpu,
							&entry->def_intr.mask);
				if (curr_cpu >= nr_cpu_ids)
					break;
			}

			/*
			 * If there ends up being 0 CPU cores leftover for SDMA
			 * engines, use the same CPU cores as general/control
			 * context.
			 */
			if (cpumask_weight(&entry->def_intr.mask) == 0)
				cpumask_copy(&entry->def_intr.mask,
					     &entry->general_intr_mask);
		}

		/* Determine completion vector CPUs for the entire node */
		cpumask_and(&entry->comp_vect_mask,
			    &node_affinity.real_cpu_mask, local_mask);
		cpumask_andnot(&entry->comp_vect_mask,
			       &entry->comp_vect_mask,
			       &entry->rcv_intr.mask);
		cpumask_andnot(&entry->comp_vect_mask,
			       &entry->comp_vect_mask,
			       &entry->general_intr_mask);

		/*
		 * If there ends up being 0 CPU cores leftover for completion
		 * vectors, use the same CPU core as the general/control
		 * context.
		 */
		if (cpumask_weight(&entry->comp_vect_mask) == 0)
			cpumask_copy(&entry->comp_vect_mask,
				     &entry->general_intr_mask);
	}

	ret = _dev_comp_vect_cpu_mask_init(dd, entry, new_entry);
	if (ret < 0)
		goto fail;

	if (new_entry)
		node_affinity_add_tail(entry);

	mutex_unlock(&node_affinity.lock);

	return 0;

fail:
	if (new_entry)
		node_affinity_destroy(entry);
	mutex_unlock(&node_affinity.lock);
	return ret;
}

void hfi1_dev_affinity_clean_up(struct hfi1_devdata *dd)
{
	struct hfi1_affinity_node *entry;

	if (dd->node < 0)
		return;

	mutex_lock(&node_affinity.lock);
	entry = node_affinity_lookup(dd->node);
	if (!entry)
		goto unlock;

	/*
	 * Free device completion vector CPUs to be used by future
	 * completion vectors
	 */
	_dev_comp_vect_cpu_mask_clean_up(dd, entry);
unlock:
	mutex_unlock(&node_affinity.lock);
	dd->node = -1;
}

/*
 * Function updates the irq affinity hint for msix after it has been changed
 * by the user using the /proc/irq interface. This function only accepts
 * one cpu in the mask.
 */
static void hfi1_update_sdma_affinity(struct hfi1_msix_entry *msix, int cpu)
{
	struct sdma_engine *sde = msix->arg;
	struct hfi1_devdata *dd = sde->dd;
	struct hfi1_affinity_node *entry;
	struct cpu_mask_set *set;
	int i, old_cpu;

	if (cpu > num_online_cpus() || cpu == sde->cpu)
		return;

	mutex_lock(&node_affinity.lock);
	entry = node_affinity_lookup(dd->node);
	if (!entry)
		goto unlock;

	old_cpu = sde->cpu;
	sde->cpu = cpu;
	cpumask_clear(&msix->mask);
	cpumask_set_cpu(cpu, &msix->mask);
	dd_dev_dbg(dd, "IRQ: %u, type %s engine %u -> cpu: %d\n",
		   msix->irq, irq_type_names[msix->type],
		   sde->this_idx, cpu);
	irq_set_affinity_hint(msix->irq, &msix->mask);

	/*
	 * Set the new cpu in the hfi1_affinity_node and clean
	 * the old cpu if it is not used by any other IRQ
	 */
	set = &entry->def_intr;
	cpumask_set_cpu(cpu, &set->mask);
	cpumask_set_cpu(cpu, &set->used);
	for (i = 0; i < dd->num_msix_entries; i++) {
		struct hfi1_msix_entry *other_msix;

		other_msix = &dd->msix_entries[i];
		if (other_msix->type != IRQ_SDMA || other_msix == msix)
			continue;

		if (cpumask_test_cpu(old_cpu, &other_msix->mask))
			goto unlock;
	}
	cpumask_clear_cpu(old_cpu, &set->mask);
	cpumask_clear_cpu(old_cpu, &set->used);
unlock:
	mutex_unlock(&node_affinity.lock);
}

static void hfi1_irq_notifier_notify(struct irq_affinity_notify *notify,
				     const cpumask_t *mask)
{
	int cpu = cpumask_first(mask);
	struct hfi1_msix_entry *msix = container_of(notify,
						    struct hfi1_msix_entry,
						    notify);

	/* Only one CPU configuration supported currently */
	hfi1_update_sdma_affinity(msix, cpu);
}

static void hfi1_irq_notifier_release(struct kref *ref)
{
	/*
	 * This is required by affinity notifier. We don't have anything to
	 * free here.
	 */
}

static void hfi1_setup_sdma_notifier(struct hfi1_msix_entry *msix)
{
	struct irq_affinity_notify *notify = &msix->notify;

	notify->irq = msix->irq;
	notify->notify = hfi1_irq_notifier_notify;
	notify->release = hfi1_irq_notifier_release;

	if (irq_set_affinity_notifier(notify->irq, notify))
		pr_err("Failed to register sdma irq affinity notifier for irq %d\n",
		       notify->irq);
}

static void hfi1_cleanup_sdma_notifier(struct hfi1_msix_entry *msix)
{
	struct irq_affinity_notify *notify = &msix->notify;

	if (irq_set_affinity_notifier(notify->irq, NULL))
		pr_err("Failed to cleanup sdma irq affinity notifier for irq %d\n",
		       notify->irq);
}

/*
 * Function sets the irq affinity for msix.
 * It *must* be called with node_affinity.lock held.
 */
static int get_irq_affinity(struct hfi1_devdata *dd,
			    struct hfi1_msix_entry *msix)
{
	cpumask_var_t diff;
	struct hfi1_affinity_node *entry;
	struct cpu_mask_set *set = NULL;
	struct sdma_engine *sde = NULL;
	struct hfi1_ctxtdata *rcd = NULL;
	char extra[64];
	int cpu = -1;

	extra[0] = '\0';
	cpumask_clear(&msix->mask);

	entry = node_affinity_lookup(dd->node);

	switch (msix->type) {
	case IRQ_SDMA:
		sde = (struct sdma_engine *)msix->arg;
		scnprintf(extra, 64, "engine %u", sde->this_idx);
		set = &entry->def_intr;
		break;
	case IRQ_GENERAL:
		cpu = cpumask_first(&entry->general_intr_mask);
		break;
	case IRQ_RCVCTXT:
		rcd = (struct hfi1_ctxtdata *)msix->arg;
		if (rcd->ctxt == HFI1_CTRL_CTXT)
			cpu = cpumask_first(&entry->general_intr_mask);
		else
			set = &entry->rcv_intr;
		scnprintf(extra, 64, "ctxt %u", rcd->ctxt);
		break;
	default:
		dd_dev_err(dd, "Invalid IRQ type %d\n", msix->type);
		return -EINVAL;
	}

	/*
	 * The general and control contexts are placed on a particular
	 * CPU, which is set above. Skip accounting for it. Everything else
	 * finds its CPU here.
	 */
	if (cpu == -1 && set) {
		if (!zalloc_cpumask_var(&diff, GFP_KERNEL))
			return -ENOMEM;

		cpu = cpu_mask_set_get_first(set, diff);
		if (cpu < 0) {
			free_cpumask_var(diff);
			dd_dev_err(dd, "Failure to obtain CPU for IRQ\n");
			return cpu;
		}

		free_cpumask_var(diff);
	}

	cpumask_set_cpu(cpu, &msix->mask);
	dd_dev_info(dd, "IRQ: %u, type %s %s -> cpu: %d\n",
		    msix->irq, irq_type_names[msix->type],
		    extra, cpu);
	irq_set_affinity_hint(msix->irq, &msix->mask);

	if (msix->type == IRQ_SDMA) {
		sde->cpu = cpu;
		hfi1_setup_sdma_notifier(msix);
	}

	return 0;
}

int hfi1_get_irq_affinity(struct hfi1_devdata *dd, struct hfi1_msix_entry *msix)
{
	int ret;

	mutex_lock(&node_affinity.lock);
	ret = get_irq_affinity(dd, msix);
	mutex_unlock(&node_affinity.lock);
	return ret;
}

void hfi1_put_irq_affinity(struct hfi1_devdata *dd,
			   struct hfi1_msix_entry *msix)
{
	struct cpu_mask_set *set = NULL;
	struct hfi1_ctxtdata *rcd;
	struct hfi1_affinity_node *entry;

	mutex_lock(&node_affinity.lock);
	entry = node_affinity_lookup(dd->node);

	switch (msix->type) {
	case IRQ_SDMA:
		set = &entry->def_intr;
		hfi1_cleanup_sdma_notifier(msix);
		break;
	case IRQ_GENERAL:
		/* Don't do accounting for general contexts */
		break;
	case IRQ_RCVCTXT:
		rcd = (struct hfi1_ctxtdata *)msix->arg;
		/* Don't do accounting for control contexts */
		if (rcd->ctxt != HFI1_CTRL_CTXT)
			set = &entry->rcv_intr;
		break;
	default:
		mutex_unlock(&node_affinity.lock);
		return;
	}

	if (set) {
		cpumask_andnot(&set->used, &set->used, &msix->mask);
		_cpu_mask_set_gen_dec(set);
	}

	irq_set_affinity_hint(msix->irq, NULL);
	cpumask_clear(&msix->mask);
	mutex_unlock(&node_affinity.lock);
}

/* This should be called with node_affinity.lock held */
static void find_hw_thread_mask(uint hw_thread_no, cpumask_var_t hw_thread_mask,
				struct hfi1_affinity_node_list *affinity)
{
	int possible, curr_cpu, i;
	uint num_cores_per_socket = node_affinity.num_online_cpus /
					affinity->num_core_siblings /
						node_affinity.num_online_nodes;

	cpumask_copy(hw_thread_mask, &affinity->proc.mask);
	if (affinity->num_core_siblings > 0) {
		/* Removing other siblings not needed for now */
		possible = cpumask_weight(hw_thread_mask);
		curr_cpu = cpumask_first(hw_thread_mask);
		for (i = 0;
		     i < num_cores_per_socket * node_affinity.num_online_nodes;
		     i++)
			curr_cpu = cpumask_next(curr_cpu, hw_thread_mask);

		for (; i < possible; i++) {
			cpumask_clear_cpu(curr_cpu, hw_thread_mask);
			curr_cpu = cpumask_next(curr_cpu, hw_thread_mask);
		}

		/* Identifying correct HW threads within physical cores */
		cpumask_shift_left(hw_thread_mask, hw_thread_mask,
				   num_cores_per_socket *
				   node_affinity.num_online_nodes *
				   hw_thread_no);
	}
}

int hfi1_get_proc_affinity(int node)
{
	int cpu = -1, ret, i;
	struct hfi1_affinity_node *entry;
	cpumask_var_t diff, hw_thread_mask, available_mask, intrs_mask;
	const struct cpumask *node_mask,
		*proc_mask = &current->cpus_allowed;
	struct hfi1_affinity_node_list *affinity = &node_affinity;
	struct cpu_mask_set *set = &affinity->proc;

	/*
	 * check whether process/context affinity has already
	 * been set
	 */
	if (cpumask_weight(proc_mask) == 1) {
		hfi1_cdbg(PROC, "PID %u %s affinity set to CPU %*pbl",
			  current->pid, current->comm,
			  cpumask_pr_args(proc_mask));
		/*
		 * Mark the pre-set CPU as used. This is atomic so we don't
		 * need the lock
		 */
		cpu = cpumask_first(proc_mask);
		cpumask_set_cpu(cpu, &set->used);
		goto done;
	} else if (cpumask_weight(proc_mask) < cpumask_weight(&set->mask)) {
		hfi1_cdbg(PROC, "PID %u %s affinity set to CPU set(s) %*pbl",
			  current->pid, current->comm,
			  cpumask_pr_args(proc_mask));
		goto done;
	}

	/*
	 * The process does not have a preset CPU affinity so find one to
	 * recommend using the following algorithm:
	 *
	 * For each user process that is opening a context on HFI Y:
	 *  a) If all cores are filled, reinitialize the bitmask
	 *  b) Fill real cores first, then HT cores (First set of HT
	 *     cores on all physical cores, then second set of HT core,
	 *     and, so on) in the following order:
	 *
	 *     1. Same NUMA node as HFI Y and not running an IRQ
	 *        handler
	 *     2. Same NUMA node as HFI Y and running an IRQ handler
	 *     3. Different NUMA node to HFI Y and not running an IRQ
	 *        handler
	 *     4. Different NUMA node to HFI Y and running an IRQ
	 *        handler
	 *  c) Mark core as filled in the bitmask. As user processes are
	 *     done, clear cores from the bitmask.
	 */

	ret = zalloc_cpumask_var(&diff, GFP_KERNEL);
	if (!ret)
		goto done;
	ret = zalloc_cpumask_var(&hw_thread_mask, GFP_KERNEL);
	if (!ret)
		goto free_diff;
	ret = zalloc_cpumask_var(&available_mask, GFP_KERNEL);
	if (!ret)
		goto free_hw_thread_mask;
	ret = zalloc_cpumask_var(&intrs_mask, GFP_KERNEL);
	if (!ret)
		goto free_available_mask;

	mutex_lock(&affinity->lock);
	/*
	 * If we've used all available HW threads, clear the mask and start
	 * overloading.
	 */
	_cpu_mask_set_gen_inc(set);

	/*
	 * If NUMA node has CPUs used by interrupt handlers, include them in the
	 * interrupt handler mask.
	 */
	entry = node_affinity_lookup(node);
	if (entry) {
		cpumask_copy(intrs_mask, (entry->def_intr.gen ?
					  &entry->def_intr.mask :
					  &entry->def_intr.used));
		cpumask_or(intrs_mask, intrs_mask, (entry->rcv_intr.gen ?
						    &entry->rcv_intr.mask :
						    &entry->rcv_intr.used));
		cpumask_or(intrs_mask, intrs_mask, &entry->general_intr_mask);
	}
	hfi1_cdbg(PROC, "CPUs used by interrupts: %*pbl",
		  cpumask_pr_args(intrs_mask));

	cpumask_copy(hw_thread_mask, &set->mask);

	/*
	 * If HT cores are enabled, identify which HW threads within the
	 * physical cores should be used.
	 */
	if (affinity->num_core_siblings > 0) {
		for (i = 0; i < affinity->num_core_siblings; i++) {
			find_hw_thread_mask(i, hw_thread_mask, affinity);

			/*
			 * If there's at least one available core for this HW
			 * thread number, stop looking for a core.
			 *
			 * diff will always be not empty at least once in this
			 * loop as the used mask gets reset when
			 * (set->mask == set->used) before this loop.
			 */
			cpumask_andnot(diff, hw_thread_mask, &set->used);
			if (!cpumask_empty(diff))
				break;
		}
	}
	hfi1_cdbg(PROC, "Same available HW thread on all physical CPUs: %*pbl",
		  cpumask_pr_args(hw_thread_mask));

	node_mask = cpumask_of_node(node);
	hfi1_cdbg(PROC, "Device on NUMA %u, CPUs %*pbl", node,
		  cpumask_pr_args(node_mask));

	/* Get cpumask of available CPUs on preferred NUMA */
	cpumask_and(available_mask, hw_thread_mask, node_mask);
	cpumask_andnot(available_mask, available_mask, &set->used);
	hfi1_cdbg(PROC, "Available CPUs on NUMA %u: %*pbl", node,
		  cpumask_pr_args(available_mask));

	/*
	 * At first, we don't want to place processes on the same
	 * CPUs as interrupt handlers. Then, CPUs running interrupt
	 * handlers are used.
	 *
	 * 1) If diff is not empty, then there are CPUs not running
	 *    non-interrupt handlers available, so diff gets copied
	 *    over to available_mask.
	 * 2) If diff is empty, then all CPUs not running interrupt
	 *    handlers are taken, so available_mask contains all
	 *    available CPUs running interrupt handlers.
	 * 3) If available_mask is empty, then all CPUs on the
	 *    preferred NUMA node are taken, so other NUMA nodes are
	 *    used for process assignments using the same method as
	 *    the preferred NUMA node.
	 */
	cpumask_andnot(diff, available_mask, intrs_mask);
	if (!cpumask_empty(diff))
		cpumask_copy(available_mask, diff);

	/* If we don't have CPUs on the preferred node, use other NUMA nodes */
	if (cpumask_empty(available_mask)) {
		cpumask_andnot(available_mask, hw_thread_mask, &set->used);
		/* Excluding preferred NUMA cores */
		cpumask_andnot(available_mask, available_mask, node_mask);
		hfi1_cdbg(PROC,
			  "Preferred NUMA node cores are taken, cores available in other NUMA nodes: %*pbl",
			  cpumask_pr_args(available_mask));

		/*
		 * At first, we don't want to place processes on the same
		 * CPUs as interrupt handlers.
		 */
		cpumask_andnot(diff, available_mask, intrs_mask);
		if (!cpumask_empty(diff))
			cpumask_copy(available_mask, diff);
	}
	hfi1_cdbg(PROC, "Possible CPUs for process: %*pbl",
		  cpumask_pr_args(available_mask));

	cpu = cpumask_first(available_mask);
	if (cpu >= nr_cpu_ids) /* empty */
		cpu = -1;
	else
		cpumask_set_cpu(cpu, &set->used);

	mutex_unlock(&affinity->lock);
	hfi1_cdbg(PROC, "Process assigned to CPU %d", cpu);

	free_cpumask_var(intrs_mask);
free_available_mask:
	free_cpumask_var(available_mask);
free_hw_thread_mask:
	free_cpumask_var(hw_thread_mask);
free_diff:
	free_cpumask_var(diff);
done:
	return cpu;
}

void hfi1_put_proc_affinity(int cpu)
{
	struct hfi1_affinity_node_list *affinity = &node_affinity;
	struct cpu_mask_set *set = &affinity->proc;

	if (cpu < 0)
		return;

	mutex_lock(&affinity->lock);
	cpu_mask_set_put(set, cpu);
	hfi1_cdbg(PROC, "Returning CPU %d for future process assignment", cpu);
	mutex_unlock(&affinity->lock);
}
