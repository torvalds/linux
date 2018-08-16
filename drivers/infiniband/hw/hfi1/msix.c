// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
/*
 * Copyright(c) 2018 Intel Corporation.
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

#include "hfi.h"
#include "sdma.h"

/*
 * Returns:
 *	- actual number of interrupts allocated or
 *      - error
 */
int request_msix(struct hfi1_devdata *dd, u32 msireq)
{
	int nvec;

	nvec = pci_alloc_irq_vectors(dd->pcidev, msireq, msireq, PCI_IRQ_MSIX);
	if (nvec < 0) {
		dd_dev_err(dd, "pci_alloc_irq_vectors() failed: %d\n", nvec);
		return nvec;
	}

	return nvec;
}

int set_up_interrupts(struct hfi1_devdata *dd)
{
	u32 total;
	int ret, request;

	/*
	 * Interrupt count:
	 *	1 general, "slow path" interrupt (includes the SDMA engines
	 *		slow source, SDMACleanupDone)
	 *	N interrupts - one per used SDMA engine
	 *	M interrupt - one per kernel receive context
	 *	V interrupt - one for each VNIC context
	 */
	total = 1 + dd->num_sdma + dd->n_krcv_queues + dd->num_vnic_contexts;

	/* ask for MSI-X interrupts */
	request = request_msix(dd, total);
	if (request < 0) {
		ret = request;
		goto fail;
	} else {
		dd->msix_entries = kcalloc(total, sizeof(*dd->msix_entries),
					   GFP_KERNEL);
		if (!dd->msix_entries) {
			ret = -ENOMEM;
			goto fail;
		}
		/* using MSI-X */
		dd->num_msix_entries = total;
		dd_dev_info(dd, "%u MSI-X interrupts allocated\n", total);
	}

	/* mask all interrupts */	set_intr_state(dd, 0);
	/* clear all pending interrupts */
	clear_all_interrupts(dd);

	/* reset general handler mask, chip MSI-X mappings */
	reset_interrupts(dd);

	ret = request_msix_irqs(dd);
	if (ret)
		goto fail;

	return 0;

fail:
	hfi1_clean_up_interrupts(dd);
	return ret;
}

int request_msix_irqs(struct hfi1_devdata *dd)
{
	int first_general, last_general;
	int first_sdma, last_sdma;
	int first_rx, last_rx;
	int i, ret = 0;

	/* calculate the ranges we are going to use */
	first_general = 0;
	last_general = first_general + 1;
	first_sdma = last_general;
	last_sdma = first_sdma + dd->num_sdma;
	first_rx = last_sdma;
	last_rx = first_rx + dd->n_krcv_queues + dd->num_vnic_contexts;

	/* VNIC MSIx interrupts get mapped when VNIC contexts are created */
	dd->first_dyn_msix_idx = first_rx + dd->n_krcv_queues;

	/*
	 * Sanity check - the code expects all SDMA chip source
	 * interrupts to be in the same CSR, starting at bit 0.  Verify
	 * that this is true by checking the bit location of the start.
	 */
	BUILD_BUG_ON(IS_SDMA_START % 64);

	for (i = 0; i < dd->num_msix_entries; i++) {
		struct hfi1_msix_entry *me = &dd->msix_entries[i];
		const char *err_info;
		irq_handler_t handler;
		irq_handler_t thread = NULL;
		void *arg = NULL;
		int idx;
		struct hfi1_ctxtdata *rcd = NULL;
		struct sdma_engine *sde = NULL;
		char name[MAX_NAME_SIZE];

		/* obtain the arguments to pci_request_irq */
		if (first_general <= i && i < last_general) {
			idx = i - first_general;
			handler = general_interrupt;
			arg = dd;
			snprintf(name, sizeof(name),
				 DRIVER_NAME "_%d", dd->unit);
			err_info = "general";
			me->type = IRQ_GENERAL;
		} else if (first_sdma <= i && i < last_sdma) {
			idx = i - first_sdma;
			sde = &dd->per_sdma[idx];
			handler = sdma_interrupt;
			arg = sde;
			snprintf(name, sizeof(name),
				 DRIVER_NAME "_%d sdma%d", dd->unit, idx);
			err_info = "sdma";
			remap_sdma_interrupts(dd, idx, i);
			me->type = IRQ_SDMA;
		} else if (first_rx <= i && i < last_rx) {
			idx = i - first_rx;
			rcd = hfi1_rcd_get_by_index_safe(dd, idx);
			if (rcd) {
				/*
				 * Set the interrupt register and mask for this
				 * context's interrupt.
				 */
				rcd->ireg = (IS_RCVAVAIL_START + idx) / 64;
				rcd->imask = ((u64)1) <<
					  ((IS_RCVAVAIL_START + idx) % 64);
				handler = receive_context_interrupt;
				thread = receive_context_thread;
				arg = rcd;
				snprintf(name, sizeof(name),
					 DRIVER_NAME "_%d kctxt%d",
					 dd->unit, idx);
				err_info = "receive context";
				remap_intr(dd, IS_RCVAVAIL_START + idx, i);
				me->type = IRQ_RCVCTXT;
				rcd->msix_intr = i;
				hfi1_rcd_put(rcd);
			}
		} else {
			/* not in our expected range - complain, then
			 * ignore it
			 */
			dd_dev_err(dd,
				   "Unexpected extra MSI-X interrupt %d\n", i);
			continue;
		}
		/* no argument, no interrupt */
		if (!arg)
			continue;
		/* make sure the name is terminated */
		name[sizeof(name) - 1] = 0;
		me->irq = pci_irq_vector(dd->pcidev, i);
		ret = pci_request_irq(dd->pcidev, i, handler, thread, arg,
				      name);
		if (ret) {
			dd_dev_err(dd,
				   "unable to allocate %s interrupt, irq %d, index %d, err %d\n",
				   err_info, me->irq, idx, ret);
			return ret;
		}
		/*
		 * assign arg after pci_request_irq call, so it will be
		 * cleaned up
		 */
		me->arg = arg;

		ret = hfi1_get_irq_affinity(dd, me);
		if (ret)
			dd_dev_err(dd, "unable to pin IRQ %d\n", ret);
	}

	return ret;
}

void hfi1_vnic_synchronize_irq(struct hfi1_devdata *dd)
{
	int i;

	for (i = 0; i < dd->vnic.num_ctxt; i++) {
		struct hfi1_ctxtdata *rcd = dd->vnic.ctxt[i];
		struct hfi1_msix_entry *me = &dd->msix_entries[rcd->msix_intr];

		synchronize_irq(me->irq);
	}
}

void hfi1_reset_vnic_msix_info(struct hfi1_ctxtdata *rcd)
{
	struct hfi1_devdata *dd = rcd->dd;
	struct hfi1_msix_entry *me = &dd->msix_entries[rcd->msix_intr];

	if (!me->arg) /* => no irq, no affinity */
		return;

	hfi1_put_irq_affinity(dd, me);
	pci_free_irq(dd->pcidev, rcd->msix_intr, me->arg);

	me->arg = NULL;
}

void hfi1_set_vnic_msix_info(struct hfi1_ctxtdata *rcd)
{
	struct hfi1_devdata *dd = rcd->dd;
	struct hfi1_msix_entry *me;
	int idx = rcd->ctxt;
	void *arg = rcd;
	int ret;

	rcd->msix_intr = dd->vnic.msix_idx++;
	me = &dd->msix_entries[rcd->msix_intr];

	/*
	 * Set the interrupt register and mask for this
	 * context's interrupt.
	 */
	rcd->ireg = (IS_RCVAVAIL_START + idx) / 64;
	rcd->imask = ((u64)1) <<
		  ((IS_RCVAVAIL_START + idx) % 64);
	me->type = IRQ_RCVCTXT;
	me->irq = pci_irq_vector(dd->pcidev, rcd->msix_intr);
	remap_intr(dd, IS_RCVAVAIL_START + idx, rcd->msix_intr);

	ret = pci_request_irq(dd->pcidev, rcd->msix_intr,
			      receive_context_interrupt,
			      receive_context_thread, arg,
			      DRIVER_NAME "_%d kctxt%d", dd->unit, idx);
	if (ret) {
		dd_dev_err(dd, "vnic irq request (irq %d, idx %d) fail %d\n",
			   me->irq, idx, ret);
		return;
	}
	/*
	 * assign arg after pci_request_irq call, so it will be
	 * cleaned up
	 */
	me->arg = arg;

	ret = hfi1_get_irq_affinity(dd, me);
	if (ret) {
		dd_dev_err(dd,
			   "unable to pin IRQ %d\n", ret);
		pci_free_irq(dd->pcidev, rcd->msix_intr, me->arg);
	}
}

/**
 * hfi1_clean_up_interrupts() - Free all IRQ resources
 * @dd: valid device data data structure
 *
 * Free the MSIx and associated PCI resources, if they have been allocated.
 */
void hfi1_clean_up_interrupts(struct hfi1_devdata *dd)
{
	int i;
	struct hfi1_msix_entry *me = dd->msix_entries;

	/* remove irqs - must happen before disabling/turning off */
	for (i = 0; i < dd->num_msix_entries; i++, me++) {
		if (!me->arg) /* => no irq, no affinity */
			continue;
		hfi1_put_irq_affinity(dd, me);
		pci_free_irq(dd->pcidev, i, me->arg);
	}

	/* clean structures */
	kfree(dd->msix_entries);
	dd->msix_entries = NULL;
	dd->num_msix_entries = 0;

	pci_free_irq_vectors(dd->pcidev);
}
