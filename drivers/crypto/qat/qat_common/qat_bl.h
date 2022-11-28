/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright(c) 2014 - 2022 Intel Corporation */
#ifndef QAT_BL_H
#define QAT_BL_H
#include <linux/scatterlist.h>
#include <linux/types.h>
#include "qat_crypto.h"

void qat_bl_free_bufl(struct adf_accel_dev *accel_dev,
		      struct qat_crypto_request_buffs *buf);
int qat_bl_sgl_to_bufl(struct adf_accel_dev *accel_dev,
		       struct scatterlist *sgl,
		       struct scatterlist *sglout,
		       struct qat_crypto_request_buffs *buf,
		       gfp_t flags);

#endif
