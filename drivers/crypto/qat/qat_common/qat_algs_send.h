/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only) */
/* Copyright(c) 2022 Intel Corporation */
#ifndef QAT_ALGS_SEND_H
#define QAT_ALGS_SEND_H

#include "qat_crypto.h"

int qat_alg_send_message(struct qat_alg_req *req);
void qat_alg_send_backlog(struct qat_instance_backlog *backlog);

#endif
