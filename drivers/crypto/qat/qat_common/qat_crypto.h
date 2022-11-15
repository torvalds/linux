/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only) */
/* Copyright(c) 2014 - 2020 Intel Corporation */
#ifndef _QAT_CRYPTO_INSTANCE_H_
#define _QAT_CRYPTO_INSTANCE_H_

#include <linux/list.h>
#include <linux/slab.h>
#include "adf_accel_devices.h"
#include "icp_qat_fw_la.h"

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
};

#define QAT_MAX_BUFF_DESC	4

struct qat_alg_buf {
	u32 len;
	u32 resrvd;
	u64 addr;
} __packed;

struct qat_alg_buf_list {
	u64 resrvd;
	u32 num_bufs;
	u32 num_mapped_bufs;
	struct qat_alg_buf bufers[];
} __packed;

struct qat_alg_fixed_buf_list {
	struct qat_alg_buf_list sgl_hdr;
	struct qat_alg_buf descriptors[QAT_MAX_BUFF_DESC];
} __packed __aligned(64);

struct qat_crypto_request_buffs {
	struct qat_alg_buf_list *bl;
	dma_addr_t blp;
	struct qat_alg_buf_list *blout;
	dma_addr_t bloutp;
	size_t sz;
	size_t sz_out;
	bool sgl_src_valid;
	bool sgl_dst_valid;
	struct qat_alg_fixed_buf_list sgl_src;
	struct qat_alg_fixed_buf_list sgl_dst;
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
	struct qat_crypto_request_buffs buf;
	void (*cb)(struct icp_qat_fw_la_resp *resp,
		   struct qat_crypto_request *req);
	void *iv;
	dma_addr_t iv_paddr;
};

#endif
