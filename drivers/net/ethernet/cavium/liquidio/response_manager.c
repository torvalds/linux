/**********************************************************************
 * Author: Cavium, Inc.
 *
 * Contact: support@cavium.com
 *          Please include "LiquidIO" in the subject.
 *
 * Copyright (c) 2003-2016 Cavium, Inc.
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, Version 2, as
 * published by the Free Software Foundation.
 *
 * This file is distributed in the hope that it will be useful, but
 * AS-IS and WITHOUT ANY WARRANTY; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE, TITLE, or
 * NONINFRINGEMENT.  See the GNU General Public License for more
 * details.
 **********************************************************************/
#include <linux/pci.h>
#include <linux/netdevice.h>
#include "liquidio_common.h"
#include "octeon_droq.h"
#include "octeon_iq.h"
#include "response_manager.h"
#include "octeon_device.h"
#include "octeon_main.h"

static void oct_poll_req_completion(struct work_struct *work);

int octeon_setup_response_list(struct octeon_device *oct)
{
	int i, ret = 0;
	struct cavium_wq *cwq;

	for (i = 0; i < MAX_RESPONSE_LISTS; i++) {
		INIT_LIST_HEAD(&oct->response_list[i].head);
		spin_lock_init(&oct->response_list[i].lock);
		atomic_set(&oct->response_list[i].pending_req_count, 0);
	}
	spin_lock_init(&oct->cmd_resp_wqlock);

	oct->dma_comp_wq.wq = alloc_workqueue("dma-comp", WQ_MEM_RECLAIM, 0);
	if (!oct->dma_comp_wq.wq) {
		dev_err(&oct->pci_dev->dev, "failed to create wq thread\n");
		return -ENOMEM;
	}

	cwq = &oct->dma_comp_wq;
	INIT_DELAYED_WORK(&cwq->wk.work, oct_poll_req_completion);
	cwq->wk.ctxptr = oct;
	oct->cmd_resp_state = OCT_DRV_ONLINE;

	return ret;
}

void octeon_delete_response_list(struct octeon_device *oct)
{
	cancel_delayed_work_sync(&oct->dma_comp_wq.wk.work);
	destroy_workqueue(oct->dma_comp_wq.wq);
}

int lio_process_ordered_list(struct octeon_device *octeon_dev,
			     u32 force_quit)
{
	struct octeon_response_list *ordered_sc_list;
	struct octeon_soft_command *sc;
	int request_complete = 0;
	int resp_to_process = MAX_ORD_REQS_TO_PROCESS;
	u32 status;
	u64 status64;

	ordered_sc_list = &octeon_dev->response_list[OCTEON_ORDERED_SC_LIST];

	do {
		spin_lock_bh(&ordered_sc_list->lock);

		if (list_empty(&ordered_sc_list->head)) {
			spin_unlock_bh(&ordered_sc_list->lock);
			return 1;
		}

		sc = list_first_entry(&ordered_sc_list->head,
				      struct octeon_soft_command, node);

		status = OCTEON_REQUEST_PENDING;

		/* check if octeon has finished DMA'ing a response
		 * to where rptr is pointing to
		 */
		status64 = *sc->status_word;

		if (status64 != COMPLETION_WORD_INIT) {
			/* This logic ensures that all 64b have been written.
			 * 1. check byte 0 for non-FF
			 * 2. if non-FF, then swap result from BE to host order
			 * 3. check byte 7 (swapped to 0) for non-FF
			 * 4. if non-FF, use the low 32-bit status code
			 * 5. if either byte 0 or byte 7 is FF, don't use status
			 */
			if ((status64 & 0xff) != 0xff) {
				octeon_swap_8B_data(&status64, 1);
				if (((status64 & 0xff) != 0xff)) {
					/* retrieve 16-bit firmware status */
					status = (u32)(status64 & 0xffffULL);
					if (status) {
						status =
						  FIRMWARE_STATUS_CODE(status);
					} else {
						/* i.e. no error */
						status = OCTEON_REQUEST_DONE;
					}
				}
			}
		} else if (force_quit || (sc->timeout &&
			time_after(jiffies, (unsigned long)sc->timeout))) {
			dev_err(&octeon_dev->pci_dev->dev, "%s: cmd failed, timeout (%ld, %ld)\n",
				__func__, (long)jiffies, (long)sc->timeout);
			status = OCTEON_REQUEST_TIMEOUT;
		}

		if (status != OCTEON_REQUEST_PENDING) {
			/* we have received a response or we have timed out */
			/* remove node from linked list */
			list_del(&sc->node);
			atomic_dec(&octeon_dev->response_list
					  [OCTEON_ORDERED_SC_LIST].
					  pending_req_count);
			spin_unlock_bh
			    (&ordered_sc_list->lock);

			if (sc->callback)
				sc->callback(octeon_dev, status,
					     sc->callback_arg);

			request_complete++;

		} else {
			/* no response yet */
			request_complete = 0;
			spin_unlock_bh
			    (&ordered_sc_list->lock);
		}

		/* If we hit the Max Ordered requests to process every loop,
		 * we quit
		 * and let this function be invoked the next time the poll
		 * thread runs
		 * to process the remaining requests. This function can take up
		 * the entire CPU if there is no upper limit to the requests
		 * processed.
		 */
		if (request_complete >= resp_to_process)
			break;
	} while (request_complete);

	return 0;
}

static void oct_poll_req_completion(struct work_struct *work)
{
	struct cavium_wk *wk = (struct cavium_wk *)work;
	struct octeon_device *oct = (struct octeon_device *)wk->ctxptr;
	struct cavium_wq *cwq = &oct->dma_comp_wq;

	lio_process_ordered_list(oct, 0);

	if (atomic_read(&oct->response_list
			[OCTEON_ORDERED_SC_LIST].pending_req_count))
		queue_delayed_work(cwq->wq, &cwq->wk.work, msecs_to_jiffies(1));
}
