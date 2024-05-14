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

static bool qat_alg_try_enqueue(struct qat_alg_req *req)
{
	struct qat_instance_backlog *backlog = req->backlog;
	struct adf_etr_ring_data *tx_ring = req->tx_ring;
	u32 *fw_req = req->fw_req;

	/* Check if any request is already backlogged */
	if (!list_empty(&backlog->list))
		return false;

	/* Check if ring is nearly full */
	if (adf_ring_nearly_full(tx_ring))
		return false;

	/* Try to enqueue to HW ring */
	if (adf_send_message(tx_ring, fw_req))
		return false;

	return true;
}


static int qat_alg_send_message_maybacklog(struct qat_alg_req *req)
{
	struct qat_instance_backlog *backlog = req->backlog;
	int ret = -EINPROGRESS;

	if (qat_alg_try_enqueue(req))
		return ret;

	spin_lock_bh(&backlog->lock);
	if (!qat_alg_try_enqueue(req)) {
		list_add_tail(&req->list, &backlog->list);
		ret = -EBUSY;
	}
	spin_unlock_bh(&backlog->lock);

	return ret;
}

int qat_alg_send_message(struct qat_alg_req *req)
{
	u32 flags = req->base->flags;

	if (flags & CRYPTO_TFM_REQ_MAY_BACKLOG)
		return qat_alg_send_message_maybacklog(req);
	else
		return qat_alg_send_message_retry(req);
}
