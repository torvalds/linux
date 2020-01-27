// SPDX-License-Identifier: GPL-2.0
/* Marvell OcteonTx2 RVU Ethernet driver
 *
 * Copyright (C) 2020 Marvell International Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/etherdevice.h>
#include <net/ip.h>

#include "otx2_reg.h"
#include "otx2_common.h"
#include "otx2_struct.h"
#include "otx2_txrx.h"

static int otx2_rx_napi_handler(struct otx2_nic *pfvf,
				struct napi_struct *napi,
				struct otx2_cq_queue *cq, int budget)
{
	 /* Nothing to do, for now */
	return 0;
}

static int otx2_tx_napi_handler(struct otx2_nic *pfvf,
				struct otx2_cq_queue *cq, int budget)
{
	 /* Nothing to do, for now */
	return 0;
}

int otx2_napi_handler(struct napi_struct *napi, int budget)
{
	struct otx2_cq_poll *cq_poll;
	int workdone = 0, cq_idx, i;
	struct otx2_cq_queue *cq;
	struct otx2_qset *qset;
	struct otx2_nic *pfvf;

	cq_poll = container_of(napi, struct otx2_cq_poll, napi);
	pfvf = (struct otx2_nic *)cq_poll->dev;
	qset = &pfvf->qset;

	for (i = CQS_PER_CINT - 1; i >= 0; i--) {
		cq_idx = cq_poll->cq_ids[i];
		if (unlikely(cq_idx == CINT_INVALID_CQ))
			continue;
		cq = &qset->cq[cq_idx];
		if (cq->cq_type == CQ_RX) {
			workdone += otx2_rx_napi_handler(pfvf, napi,
							 cq, budget);
		} else {
			workdone += otx2_tx_napi_handler(pfvf, cq, budget);
		}
	}

	/* Clear the IRQ */
	otx2_write64(pfvf, NIX_LF_CINTX_INT(cq_poll->cint_idx), BIT_ULL(0));

	if (workdone < budget && napi_complete_done(napi, workdone)) {
		/* Re-enable interrupts */
		otx2_write64(pfvf, NIX_LF_CINTX_ENA_W1S(cq_poll->cint_idx),
			     BIT_ULL(0));
	}
	return workdone;
}
