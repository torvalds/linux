/*
 * Huawei HiNIC PCI Express Linux driver
 * Copyright(c) 2017 Huawei Technologies Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 */

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/pci.h>
#include <linux/device.h>
#include <linux/workqueue.h>

#include "hinic_hw_if.h"
#include "hinic_hw_eqs.h"

#define HINIC_EQS_WQ_NAME                       "hinic_eqs"

/**
 * hinic_aeq_register_hw_cb - register AEQ callback for specific event
 * @aeqs: pointer to Async eqs of the chip
 * @event: aeq event to register callback for it
 * @handle: private data will be used by the callback
 * @hw_handler: callback function
 **/
void hinic_aeq_register_hw_cb(struct hinic_aeqs *aeqs,
			      enum hinic_aeq_type event, void *handle,
			      void (*hwe_handler)(void *handle, void *data,
						  u8 size))
{
	struct hinic_hw_event_cb *hwe_cb = &aeqs->hwe_cb[event];

	hwe_cb->hwe_handler = hwe_handler;
	hwe_cb->handle = handle;
	hwe_cb->hwe_state = HINIC_EQE_ENABLED;
}

/**
 * hinic_aeq_unregister_hw_cb - unregister the AEQ callback for specific event
 * @aeqs: pointer to Async eqs of the chip
 * @event: aeq event to unregister callback for it
 **/
void hinic_aeq_unregister_hw_cb(struct hinic_aeqs *aeqs,
				enum hinic_aeq_type event)
{
	struct hinic_hw_event_cb *hwe_cb = &aeqs->hwe_cb[event];

	hwe_cb->hwe_state &= ~HINIC_EQE_ENABLED;

	while (hwe_cb->hwe_state & HINIC_EQE_RUNNING)
		schedule();

	hwe_cb->hwe_handler = NULL;
}

/**
 * init_eq - initialize Event Queue
 * @eq: the event queue
 * @hwif: the HW interface of a PCI function device
 * @type: the type of the event queue, aeq or ceq
 * @q_id: Queue id number
 * @q_len: the number of EQ elements
 * @page_size: the page size of the pages in the event queue
 * @entry: msix entry associated with the event queue
 *
 * Return 0 - Success, Negative - Failure
 **/
static int init_eq(struct hinic_eq *eq, struct hinic_hwif *hwif,
		   enum hinic_eq_type type, int q_id, u32 q_len, u32 page_size,
		   struct msix_entry entry)
{
	/* should be implemented */
	return 0;
}

/**
 * remove_eq - remove Event Queue
 * @eq: the event queue
 **/
static void remove_eq(struct hinic_eq *eq)
{
	/* should be implemented */
}

/**
 * hinic_aeqs_init - initialize all the aeqs
 * @aeqs: pointer to Async eqs of the chip
 * @hwif: the HW interface of a PCI function device
 * @num_aeqs: number of AEQs
 * @q_len: number of EQ elements
 * @page_size: the page size of the pages in the event queue
 * @msix_entries: msix entries associated with the event queues
 *
 * Return 0 - Success, negative - Failure
 **/
int hinic_aeqs_init(struct hinic_aeqs *aeqs, struct hinic_hwif *hwif,
		    int num_aeqs, u32 q_len, u32 page_size,
		    struct msix_entry *msix_entries)
{
	struct pci_dev *pdev = hwif->pdev;
	int err, i, q_id;

	aeqs->workq = create_singlethread_workqueue(HINIC_EQS_WQ_NAME);
	if (!aeqs->workq)
		return -ENOMEM;

	aeqs->hwif = hwif;
	aeqs->num_aeqs = num_aeqs;

	for (q_id = 0; q_id < num_aeqs; q_id++) {
		err = init_eq(&aeqs->aeq[q_id], hwif, HINIC_AEQ, q_id, q_len,
			      page_size, msix_entries[q_id]);
		if (err) {
			dev_err(&pdev->dev, "Failed to init aeq %d\n", q_id);
			goto err_init_aeq;
		}
	}

	return 0;

err_init_aeq:
	for (i = 0; i < q_id; i++)
		remove_eq(&aeqs->aeq[i]);

	destroy_workqueue(aeqs->workq);
	return err;
}

/**
 * hinic_aeqs_free - free all the aeqs
 * @aeqs: pointer to Async eqs of the chip
 **/
void hinic_aeqs_free(struct hinic_aeqs *aeqs)
{
	int q_id;

	for (q_id = 0; q_id < aeqs->num_aeqs ; q_id++)
		remove_eq(&aeqs->aeq[q_id]);

	destroy_workqueue(aeqs->workq);
}
