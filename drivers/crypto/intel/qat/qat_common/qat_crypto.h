/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only) */
/* Copyright(c) 2014 - 2020 Intel Corporation */
#ifndef _QAT_CRYPTO_INSTANCE_H_
#define _QAT_CRYPTO_INSTANCE_H_

#include <crypto/aes.h>
#include <linux/list.h>
#include <linux/slab.h>
#include "adf_accel_devices.h"
#include "icp_qat_fw_la.h"
#include "qat_algs_send.h"
#include "qat_bl.h"

struct qat_crypto_instance {
	struct adf_etr_ring_data *sym_tx;
	struct adf_etr_ring_data *sym_rx;
	struct adf_etr_ring_data *pke_tx;
	struct adf_etr_ring_data *pke_rx;
	struct adf_accel_dev *accel_dev;
	struct list_head list;
	unsigned long state;
	int id;
	atomic_t refctr;
	struct qat_instance_backlog backlog;
};

struct qat_crypto_request;

struct qat_crypto_request {
	struct icp_qat_fw_la_bulk_req req;
	union {
		struct qat_alg_aead_ctx *aead_ctx;
		struct qat_alg_skcipher_ctx *skcipher_ctx;
	};
	union {
		struct aead_request *aead_req;
		struct skcipher_request *skcipher_req;
	};
	struct qat_request_buffs buf;
	void (*cb)(struct icp_qat_fw_la_resp *resp,
		   struct qat_crypto_request *req);
	union {
		struct {
			__be64 iv_hi;
			__be64 iv_lo;
		};
		u8 iv[AES_BLOCK_SIZE];
	};
	bool encryption;
	struct qat_alg_req alg_req;
};

static inline bool adf_hw_dev_has_crypto(struct adf_accel_dev *accel_dev)
{
	struct adf_hw_device_data *hw_device = accel_dev->hw_device;
	u32 mask = ~hw_device->accel_capabilities_mask;

	if (mask & ADF_ACCEL_CAPABILITIES_CRYPTO_SYMMETRIC)
		return false;
	if (mask & ADF_ACCEL_CAPABILITIES_CRYPTO_ASYMMETRIC)
		return false;
	if (mask & ADF_ACCEL_CAPABILITIES_AUTHENTICATION)
		return false;

	return true;
}

#endif
