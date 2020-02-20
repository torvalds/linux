/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2019 HiSilicon Limited. */

#ifndef __HISI_SEC_V2_H
#define __HISI_SEC_V2_H

#include <linux/list.h>

#include "../qm.h"
#include "sec_crypto.h"

/* Cipher resource per hardware SEC queue */
struct sec_cipher_res {
	u8 *c_ivin;
	dma_addr_t c_ivin_dma;
};

/* Cipher request of SEC private */
struct sec_cipher_req {
	struct hisi_acc_hw_sgl *c_in;
	dma_addr_t c_in_dma;
	struct hisi_acc_hw_sgl *c_out;
	dma_addr_t c_out_dma;
	u8 *c_ivin;
	dma_addr_t c_ivin_dma;
	struct skcipher_request *sk_req;
	u32 c_len;
	bool encrypt;
};

/* SEC request of Crypto */
struct sec_req {
	struct sec_sqe sec_sqe;
	struct sec_ctx *ctx;
	struct sec_qp_ctx *qp_ctx;

	/* Cipher supported only at present */
	struct sec_cipher_req c_req;
	int err_type;
	int req_id;

	/* Status of the SEC request */
	atomic_t fake_busy;
};

/**
 * struct sec_req_op - Operations for SEC request
 * @get_res: Get resources for TFM on the SEC device
 * @resource_alloc: Allocate resources for queue context on the SEC device
 * @resource_free: Free resources for queue context on the SEC device
 * @buf_map: DMA map the SGL buffers of the request
 * @buf_unmap: DMA unmap the SGL buffers of the request
 * @bd_fill: Fill the SEC queue BD
 * @bd_send: Send the SEC BD into the hardware queue
 * @callback: Call back for the request
 * @process: Main processing logic of Skcipher
 */
struct sec_req_op {
	int (*get_res)(struct sec_ctx *ctx, struct sec_req *req);
	int (*resource_alloc)(struct sec_ctx *ctx, struct sec_qp_ctx *qp_ctx);
	void (*resource_free)(struct sec_ctx *ctx, struct sec_qp_ctx *qp_ctx);
	int (*buf_map)(struct sec_ctx *ctx, struct sec_req *req);
	void (*buf_unmap)(struct sec_ctx *ctx, struct sec_req *req);
	void (*do_transfer)(struct sec_ctx *ctx, struct sec_req *req);
	int (*bd_fill)(struct sec_ctx *ctx, struct sec_req *req);
	int (*bd_send)(struct sec_ctx *ctx, struct sec_req *req);
	void (*callback)(struct sec_ctx *ctx, struct sec_req *req);
	int (*process)(struct sec_ctx *ctx, struct sec_req *req);
};

/* SEC cipher context which cipher's relatives */
struct sec_cipher_ctx {
	u8 *c_key;
	dma_addr_t c_key_dma;
	sector_t iv_offset;
	u32 c_gran_size;
	u32 ivsize;
	u8 c_mode;
	u8 c_alg;
	u8 c_key_len;
};

/* SEC queue context which defines queue's relatives */
struct sec_qp_ctx {
	struct hisi_qp *qp;
	struct sec_req **req_list;
	struct idr req_idr;
	void *alg_meta_data;
	struct sec_ctx *ctx;
	struct mutex req_lock;
	struct hisi_acc_sgl_pool *c_in_pool;
	struct hisi_acc_sgl_pool *c_out_pool;
	atomic_t pending_reqs;
};

/* SEC Crypto TFM context which defines queue and cipher .etc relatives */
struct sec_ctx {
	struct sec_qp_ctx *qp_ctx;
	struct sec_dev *sec;
	const struct sec_req_op *req_op;

	/* Half queues for encipher, and half for decipher */
	u32 hlf_q_num;

	/* Threshold for fake busy, trigger to return -EBUSY to user */
	u32 fake_req_limit;

	/* Currrent cyclic index to select a queue for encipher */
	atomic_t enc_qcyclic;

	 /* Currrent cyclic index to select a queue for decipher */
	atomic_t dec_qcyclic;
	struct sec_cipher_ctx c_ctx;
};

enum sec_endian {
	SEC_LE = 0,
	SEC_32BE,
	SEC_64BE
};

enum sec_debug_file_index {
	SEC_CURRENT_QM,
	SEC_CLEAR_ENABLE,
	SEC_DEBUG_FILE_NUM,
};

struct sec_debug_file {
	enum sec_debug_file_index index;
	spinlock_t lock;
	struct hisi_qm *qm;
};

struct sec_dfx {
	atomic64_t send_cnt;
	atomic64_t recv_cnt;
};

struct sec_debug {
	struct sec_dfx dfx;
	struct sec_debug_file files[SEC_DEBUG_FILE_NUM];
};

struct sec_dev {
	struct hisi_qm qm;
	struct list_head list;
	struct sec_debug debug;
	u32 ctx_q_num;
	u32 num_vfs;
	unsigned long status;
};

struct sec_dev *sec_find_device(int node);
int sec_register_to_crypto(void);
void sec_unregister_from_crypto(void);
#endif
