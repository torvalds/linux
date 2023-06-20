// SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only)
/* Copyright(c) 2022 Intel Corporation */
#include "adf_transport.h"
#include "qat_algs_send.h"
#include "qat_crypto.h"

#define ADF_MAX_RETRIES		20

static int qat_alg_send_message_retry(struct qat_alg_req *req)
{
	int ret = 0, ctr = 0;

	do {
		ret = adf_send_message(req->tx_ring, req->fw_req);
	} while (ret == -EAGAIN && ctr++ < ADF_MAX_RETRIES);

	if (ret == -EAGAIN)
		return -ENOSPC;

	return -EINPROGRESS;
}

void qat_alg_send_backlog(struct qat_instance_backlog *backlog)
{
	struct qat_alg_req *req, *tmp;

	spin_lock_bh(&backlog->lock);
	list_for_each_entry_safe(req, tmp, &backlog->list, list) {
		if (adf_send_message(req->tx_ring, req->fw_req)) {
			/* The HW ring is full. Do nothing.
			 * qat_alg_send_backlog() will be invoked again by
			 * another callback.
			 */
			break;
		}
		list_del(&req->list);
		req->base->complete(req->base, -EINPROGRESS);
	}
	spin_unlock_bh(&backlog->lock);
}

static void qat_alg_backlog_req(struct qat_alg_req *req,
				struct qat_instance_backlog *backlog)
{
	INIT_LIST_HEAD(&req->list);

	spin_lock_bh(&backlog->lock);
	list_add_tail(&req->list, &backlog->list);
	spin_unlock_bh(&backlog->lock);
}

static int qat_alg_send_message_maybacklog(struct qat_alg_req *req)
{
	struct qat_instance_backlog *backlog = req->backlog;
	struct adf_etr_ring_data *tx_ring = req->tx_ring;
	u32 *fw_req = req->fw_req;

	/* If any request is already backlogged, then add to backlog list */
	if (!list_empty(&backlog->list))
		goto enqueue;

	/* If ring is nearly full, then add to backlog list */
	if (adf_ring_nearly_full(tx_ring))
		goto enqueue;

	/* If adding request to HW ring fails, then add to backlog list */
	if (adf_send_message(tx_ring, fw_req))
		goto enqueue;

	return -EINPROGRESS;

enqueue:
	qat_alg_backlog_req(req, backlog);

	return -EBUSY;
}

int qat_alg_send_message(struct qat_alg_req *req)
{
	u32 flags = req->base->flags;

	if (flags & CRYPTO_TFM_REQ_MAY_BACKLOG)
		return qat_alg_send_message_maybacklog(req);
	else
		return qat_alg_send_message_retry(req);
}
