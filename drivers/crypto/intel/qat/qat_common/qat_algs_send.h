/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only) */
/* Copyright(c) 2022 Intel Corporation */
#ifndef QAT_ALGS_SEND_H
#define QAT_ALGS_SEND_H

#include <linux/list.h>
#include "adf_transport_internal.h"

struct qat_instance_backlog {
	struct list_head list;
	spinlock_t lock; /* protects backlog list */
};

struct qat_alg_req {
	u32 *fw_req;
	struct adf_etr_ring_data *tx_ring;
	struct crypto_async_request *base;
	struct list_head list;
	struct qat_instance_backlog *backlog;
};

int qat_alg_send_message(struct qat_alg_req *req);
void qat_alg_send_backlog(struct qat_instance_backlog *backlog);

#endif
