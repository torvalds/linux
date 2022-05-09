// SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only)
/* Copyright(c) 2022 Intel Corporation */
#include "adf_transport.h"
#include "qat_algs_send.h"
#include "qat_crypto.h"

#define ADF_MAX_RETRIES		20

int qat_alg_send_message(struct qat_alg_req *req)
{
	int ret = 0, ctr = 0;

	do {
		ret = adf_send_message(req->tx_ring, req->fw_req);
	} while (ret == -EAGAIN && ctr++ < ADF_MAX_RETRIES);

	if (ret == -EAGAIN)
		return -ENOSPC;

	return -EINPROGRESS;
}
