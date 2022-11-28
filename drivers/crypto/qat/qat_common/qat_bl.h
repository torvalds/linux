/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright(c) 2014 - 2022 Intel Corporation */
#ifndef QAT_BL_H
#define QAT_BL_H
#include <linux/scatterlist.h>
#include <linux/types.h>
#include "qat_crypto.h"

void qat_alg_free_bufl(struct qat_crypto_instance *inst,
		       struct qat_crypto_request *qat_req);
int qat_alg_sgl_to_bufl(struct qat_crypto_instance *inst,
			struct scatterlist *sgl,
			struct scatterlist *sglout,
			struct qat_crypto_request *qat_req,
			gfp_t flags);

#endif
