/*
 * Copyright(c) 2015, 2016 Intel Corporation.
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

#include "hfi.h"
#include "affinity.h"
#include "sdma.h"
#include "trace.h"

struct cpu_mask_set {
	struct cpumask mask;
	struct cpumask used;
	uint gen;
};

struct hfi1_affinity {
	struct cpu_mask_set def_intr;
	struct cpu_mask_set rcv_intr;
	struct cpu_mask_set proc;
	/* spin lock to protect affinity struct */
	spinlock_t lock;
};

/* Name of IRQ types, indexed by enum irq_type */
static const char * const irq_type_names[] = {
	"SDMA",
	"RCVCTXT",
	"GENERAL",
	"OTHER",
};

static inline void init_cpu_mask_set(struct cpu_mask_set *set)
{
	cpumask_clear(&set->mask);
	cpumask_clear(&set->used);
	set->gen = 0;
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
	struct hfi1_affinity *info;
	const struct cpumask *local_mask;
	int curr_cpu, possible, i, ht;

	if (node < 0)
		node = numa_node_id();
	dd->node = node;

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;
	spin_lock_init(&info->lock);

	init_cpu_mask_set(&info->def_intr);
	init_cpu_mask_set(&info->rcv_intr);
	init_cpu_mask_set(&info->proc);

	local_mask = cpumask_of_node(dd->node);
	if (cpumask_first(local_mask) >= nr_cpu_ids)
		local_mask = topology_core_cpumask(0);
	/* use local mask as default */
	cpumask_copy(&info->def_intr.mask, local_mask);
	/*
	 * Remove HT cores from the default mask.  Do this in two steps below.
	 */
	possible = cpumask_weight(&info->def_intr.mask);
	ht = cpumask_weight(topology_sibling_cpumask(
					cpumask_first(&info->def_intr.mask)));
	/*
	 * Step 1.  Skip over the first N HT siblings and use them as the
	 * "real" cores.  Assumes that HT cores are not enumerated in
	 * succession (except in the single core case).
	 */
	curr_cpu = cpumask_first(&info->def_intr.mask);
	for (i = 0; i < possible / ht; i++)
		curr_cpu = cpumask_next(curr_cpu, &info->def_intr.mask);
	/*
	 * Step 2.  Remove the remaining HT siblings.  Use cpumask_next() to
	 * skip any gaps.
	 */
	for (; i < possible; i++) {
		cpumask_clear_cpu(curr_cpu, &info->def_intr.mask);
		curr_cpu = cpumask_next(curr_cpu, &info->def_intr.mask);
	}

	/*  fill in the receive list */
	possible = cpumask_weight(&info->def_intr.mask);
	curr_cpu = cpumask_first(&info->def_intr.mask);
	if (possible == 1) {
		/*  only one CPU, everyone will use it */
		cpumask_set_cpu(curr_cpu, &info->rcv_intr.mask);
	} else {
		/*
		 * Retain the first CPU in the default list for the control
		 * context.
		 */
		curr_cpu = cpumask_next(curr_cpu, &info->def_intr.mask);
		/*
		 * Remove the remaining kernel receive queues from
		 * the default list and add them to the receive list.
		 */
		for (i = 0; i < dd->n_krcv_queues - 1; i++) {
			cpumask_clear_cpu(curr_cpu, &info->def_intr.mask);
			cpumask_set_cpu(curr_cpu, &info->rcv_intr.mask);
			curr_cpu = cpumask_next(curr_cpu, &info->def_intr.mask);
			if (curr_cpu >= nr_cpu_ids)
				break;
		}
	}

	cpumask_copy(&info->proc.mask, cpu_online_mask);
	dd->affinity = info;
	return 0;
}

void hfi1_dev_affinity_free(struct hfi1_devdata *dd)
{
	kfree(dd->affinity);
}

int hfi1_get_irq_affinity(struct hfi1_devdata *dd, struct hfi1_msix_entry *msix)
{
	int ret;
	cpumask_var_t diff;
	struct cpu_mask_set *set;
	struct sdma_engine *sde = NULL;
	struct hfi1_ctxtdata *rcd = NULL;
	char extra[64];
	int cpu = -1;

	extra[0] = '\0';
	cpumask_clear(&msix->mask);

	ret = zalloc_cpumask_var(&diff, GFP_KERNEL);
	if (!ret)
		return -ENOMEM;

	switch (msix->type) {
	case IRQ_SDMA:
		sde = (struct sdma_engine *)msix->arg;
		scnprintf(extra, 64, "engine %u", sde->this_idx);
		/* fall through */
	case IRQ_GENERAL:
		set = &dd->affinity->def_intr;
		break;
	case IRQ_RCVCTXT:
		rcd = (struct hfi1_ctxtdata *)msix->arg;
		if (rcd->ctxt == HFI1_CTRL_CTXT) {
			set = &dd->affinity->def_intr;
			cpu = cpumask_first(&set->mask);
		} else {
			set = &dd->affinity->rcv_intr;
		}
		scnprintf(extra, 64, "ctxt %u", rcd->ctxt);
		break;
	default:
		dd_dev_err(dd, "Invalid IRQ type %d\n", msix->type);
		return -EINVAL;
	}

	/*
	 * The control receive context is placed on a particular CPU, which
	 * is set above.  Skip accounting for it.  Everything else finds its
	 * CPU here.
	 */
	if (cpu == -1) {
		spin_lock(&dd->affinity->lock);
		if (cpumask_equal(&set->mask, &set->used)) {
			/*
			 * We've used up all the CPUs, bump up the generation
			 * and reset the 'used' map
			 */
			set->gen++;
			cpumask_clear(&set->used);
		}
		cpumask_andnot(diff, &set->mask, &set->used);
		cpu = cpumask_first(diff);
		cpumask_set_cpu(cpu, &set->used);
		spin_unlock(&dd->affinity->lock);
	}

	switch (msix->type) {
	case IRQ_SDMA:
		sde->cpu = cpu;
		break;
	case IRQ_GENERAL:
	case IRQ_RCVCTXT:
	case IRQ_OTHER:
		break;
	}

	cpumask_set_cpu(cpu, &msix->mask);
	dd_dev_info(dd, "IRQ vector: %u, type %s %s -> cpu: %d\n",
		    msix->msix.vector, irq_type_names[msix->type],
		    extra, cpu);
	irq_set_affinity_hint(msix->msix.vector, &msix->mask);

	free_cpumask_var(diff);
	return 0;
}

void hfi1_put_irq_affinity(struct hfi1_devdata *dd,
			   struct hfi1_msix_entry *msix)
{
	struct cpu_mask_set *set = NULL;
	struct hfi1_ctxtdata *rcd;

	switch (msix->type) {
	case IRQ_SDMA:
	case IRQ_GENERAL:
		set = &dd->affinity->def_intr;
		break;
	case IRQ_RCVCTXT:
		rcd = (struct hfi1_ctxtdata *)msix->arg;
		/* only do accounting for non control contexts */
		if (rcd->ctxt != HFI1_CTRL_CTXT)
			set = &dd->affinity->rcv_intr;
		break;
	default:
		return;
	}

	if (set) {
		spin_lock(&dd->affinity->lock);
		cpumask_andnot(&set->used, &set->used, &msix->mask);
		if (cpumask_empty(&set->used) && set->gen) {
			set->gen--;
			cpumask_copy(&set->used, &set->mask);
		}
		spin_unlock(&dd->affinity->lock);
	}

	irq_set_affinity_hint(msix->msix.vector, NULL);
	cpumask_clear(&msix->mask);
}

int hfi1_get_proc_affinity(struct hfi1_devdata *dd, int node)
{
	int cpu = -1, ret;
	cpumask_var_t diff, mask, intrs;
	const struct cpumask *node_mask,
		*proc_mask = tsk_cpus_allowed(current);
	struct cpu_mask_set *set = &dd->affinity->proc;
	char buf[1024];

	/*
	 * check whether process/context affinity has already
	 * been set
	 */
	if (cpumask_weight(proc_mask) == 1) {
		scnprintf(buf, 1024, "%*pbl", cpumask_pr_args(proc_mask));
		hfi1_cdbg(PROC, "PID %u %s affinity set to CPU %s",
			  current->pid, current->comm, buf);
		/*
		 * Mark the pre-set CPU as used. This is atomic so we don't
		 * need the lock
		 */
		cpu = cpumask_first(proc_mask);
		cpumask_set_cpu(cpu, &set->used);
		goto done;
	} else if (cpumask_weight(proc_mask) < cpumask_weight(&set->mask)) {
		scnprintf(buf, 1024, "%*pbl", cpumask_pr_args(proc_mask));
		hfi1_cdbg(PROC, "PID %u %s affinity set to CPU set(s) %s",
			  current->pid, current->comm, buf);
		goto done;
	}

	/*
	 * The process does not have a preset CPU affinity so find one to
	 * recommend. We prefer CPUs on the same NUMA as the device.
	 */

	ret = zalloc_cpumask_var(&diff, GFP_KERNEL);
	if (!ret)
		goto done;
	ret = zalloc_cpumask_var(&mask, GFP_KERNEL);
	if (!ret)
		goto free_diff;
	ret = zalloc_cpumask_var(&intrs, GFP_KERNEL);
	if (!ret)
		goto free_mask;

	spin_lock(&dd->affinity->lock);
	/*
	 * If we've used all available CPUs, clear the mask and start
	 * overloading.
	 */
	if (cpumask_equal(&set->mask, &set->used)) {
		set->gen++;
		cpumask_clear(&set->used);
	}

	/* CPUs used by interrupt handlers */
	cpumask_copy(intrs, (dd->affinity->def_intr.gen ?
			     &dd->affinity->def_intr.mask :
			     &dd->affinity->def_intr.used));
	cpumask_or(intrs, intrs, (dd->affinity->rcv_intr.gen ?
				  &dd->affinity->rcv_intr.mask :
				  &dd->affinity->rcv_intr.used));
	scnprintf(buf, 1024, "%*pbl", cpumask_pr_args(intrs));
	hfi1_cdbg(PROC, "CPUs used by interrupts: %s", buf);

	/*
	 * If we don't have a NUMA node requested, preference is towards
	 * device NUMA node
	 */
	if (node == -1)
		node = dd->node;
	node_mask = cpumask_of_node(node);
	scnprintf(buf, 1024, "%*pbl", cpumask_pr_args(node_mask));
	hfi1_cdbg(PROC, "device on NUMA %u, CPUs %s", node, buf);

	/* diff will hold all unused cpus */
	cpumask_andnot(diff, &set->mask, &set->used);
	scnprintf(buf, 1024, "%*pbl", cpumask_pr_args(diff));
	hfi1_cdbg(PROC, "unused CPUs (all) %s", buf);

	/* get cpumask of available CPUs on preferred NUMA */
	cpumask_and(mask, diff, node_mask);
	scnprintf(buf, 1024, "%*pbl", cpumask_pr_args(mask));
	hfi1_cdbg(PROC, "available cpus on NUMA %s", buf);

	/*
	 * At first, we don't want to place processes on the same
	 * CPUs as interrupt handlers.
	 */
	cpumask_andnot(diff, mask, intrs);
	if (!cpumask_empty(diff))
		cpumask_copy(mask, diff);

	/*
	 * if we don't have a cpu on the preferred NUMA, get
	 * the list of the remaining available CPUs
	 */
	if (cpumask_empty(mask)) {
		cpumask_andnot(diff, &set->mask, &set->used);
		cpumask_andnot(mask, diff, node_mask);
	}
	scnprintf(buf, 1024, "%*pbl", cpumask_pr_args(mask));
	hfi1_cdbg(PROC, "possible CPUs for process %s", buf);

	cpu = cpumask_first(mask);
	if (cpu >= nr_cpu_ids) /* empty */
		cpu = -1;
	else
		cpumask_set_cpu(cpu, &set->used);
	spin_unlock(&dd->affinity->lock);

	free_cpumask_var(intrs);
free_mask:
	free_cpumask_var(mask);
free_diff:
	free_cpumask_var(diff);
done:
	return cpu;
}

void hfi1_put_proc_affinity(struct hfi1_devdata *dd, int cpu)
{
	struct cpu_mask_set *set = &dd->affinity->proc;

	if (cpu < 0)
		return;
	spin_lock(&dd->affinity->lock);
	cpumask_clear_cpu(cpu, &set->used);
	if (cpumask_empty(&set->used) && set->gen) {
		set->gen--;
		cpumask_copy(&set->used, &set->mask);
	}
	spin_unlock(&dd->affinity->lock);
}

