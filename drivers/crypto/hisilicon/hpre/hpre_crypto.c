// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 HiSilicon Limited. */
#include <crypto/akcipher.h>
#include <crypto/curve25519.h>
#include <crypto/dh.h>
#include <crypto/ecc_curve.h>
#include <crypto/ecdh.h>
#include <crypto/rng.h>
#include <crypto/internal/akcipher.h>
#include <crypto/internal/kpp.h>
#include <crypto/internal/rsa.h>
#include <crypto/kpp.h>
#include <crypto/scatterwalk.h>
#include <linux/dma-mapping.h>
#include <linux/fips.h>
#include <linux/module.h>
#include <linux/time.h>
#include "hpre.h"

struct hpre_ctx;

#define HPRE_CRYPTO_ALG_PRI	1000
#define HPRE_ALIGN_SZ		64
#define HPRE_BITS_2_BYTES_SHIFT	3
#define HPRE_RSA_512BITS_KSZ	64
#define HPRE_RSA_1536BITS_KSZ	192
#define HPRE_CRT_PRMS		5
#define HPRE_CRT_Q		2
#define HPRE_CRT_P		3
#define HPRE_CRT_INV		4
#define HPRE_DH_G_FLAG		0x02
#define HPRE_TRY_SEND_TIMES	100
#define HPRE_INVLD_REQ_ID		(-1)

#define HPRE_SQE_ALG_BITS	5
#define HPRE_SQE_DONE_SHIFT	30
#define HPRE_DH_MAX_P_SZ	512

#define HPRE_DFX_SEC_TO_US	1000000
#define HPRE_DFX_US_TO_NS	1000

/* due to nist p521  */
#define HPRE_ECC_MAX_KSZ	66

/* size in bytes of the n prime */
#define HPRE_ECC_NIST_P192_N_SIZE	24
#define HPRE_ECC_NIST_P256_N_SIZE	32
#define HPRE_ECC_NIST_P384_N_SIZE	48

/* size in bytes */
#define HPRE_ECC_HW256_KSZ_B	32
#define HPRE_ECC_HW384_KSZ_B	48

typedef void (*hpre_cb)(struct hpre_ctx *ctx, void *sqe);

struct hpre_rsa_ctx {
	/* low address: e--->n */
	char *pubkey;
	dma_addr_t dma_pubkey;

	/* low address: d--->n */
	char *prikey;
	dma_addr_t dma_prikey;

	/* low address: dq->dp->q->p->qinv */
	char *crt_prikey;
	dma_addr_t dma_crt_prikey;

	struct crypto_akcipher *soft_tfm;
};

struct hpre_dh_ctx {
	/*
	 * If base is g we compute the public key
	 *	ya = g^xa mod p; [RFC2631 sec 2.1.1]
	 * else if base if the counterpart public key we
	 * compute the shared secret
	 *	ZZ = yb^xa mod p; [RFC2631 sec 2.1.1]
	 * low address: d--->n, please refer to Hisilicon HPRE UM
	 */
	char *xa_p;
	dma_addr_t dma_xa_p;

	char *g; /* m */
	dma_addr_t dma_g;
};

struct hpre_ecdh_ctx {
	/* low address: p->a->k->b */
	unsigned char *p;
	dma_addr_t dma_p;

	/* low address: x->y */
	unsigned char *g;
	dma_addr_t dma_g;
};

struct hpre_curve25519_ctx {
	/* low address: p->a->k */
	unsigned char *p;
	dma_addr_t dma_p;

	/* gx coordinate */
	unsigned char *g;
	dma_addr_t dma_g;
};

struct hpre_ctx {
	struct hisi_qp *qp;
	struct device *dev;
	struct hpre_asym_request **req_list;
	struct hpre *hpre;
	spinlock_t req_lock;
	unsigned int key_sz;
	bool crt_g2_mode;
	struct idr req_idr;
	union {
		struct hpre_rsa_ctx rsa;
		struct hpre_dh_ctx dh;
		struct hpre_ecdh_ctx ecdh;
		struct hpre_curve25519_ctx curve25519;
	};
	/* for ecc algorithms */
	unsigned int curve_id;
};

struct hpre_asym_request {
	char *src;
	char *dst;
	struct hpre_sqe req;
	struct hpre_ctx *ctx;
	union {
		struct akcipher_request *rsa;
		struct kpp_request *dh;
		struct kpp_request *ecdh;
		struct kpp_request *curve25519;
	} areq;
	int err;
	int req_id;
	hpre_cb cb;
	struct timespec64 req_time;
};

static int hpre_alloc_req_id(struct hpre_ctx *ctx)
{
	unsigned long flags;
	int id;

	spin_lock_irqsave(&ctx->req_lock, flags);
	id = idr_alloc(&ctx->req_idr, NULL, 0, QM_Q_DEPTH, GFP_ATOMIC);
	spin_unlock_irqrestore(&ctx->req_lock, flags);

	return id;
}

static void hpre_free_req_id(struct hpre_ctx *ctx, int req_id)
{
	unsigned long flags;

	spin_lock_irqsave(&ctx->req_lock, flags);
	idr_remove(&ctx->req_idr, req_id);
	spin_unlock_irqrestore(&ctx->req_lock, flags);
}

static int hpre_add_req_to_ctx(struct hpre_asym_request *hpre_req)
{
	struct hpre_ctx *ctx;
	struct hpre_dfx *dfx;
	int id;

	ctx = hpre_req->ctx;
	id = hpre_alloc_req_id(ctx);
	if (unlikely(id < 0))
		return -EINVAL;

	ctx->req_list[id] = hpre_req;
	hpre_req->req_id = id;

	dfx = ctx->hpre->debug.dfx;
	if (atomic64_read(&dfx[HPRE_OVERTIME_THRHLD].value))
		ktime_get_ts64(&hpre_req->req_time);

	return id;
}

static void hpre_rm_req_from_ctx(struct hpre_asym_request *hpre_req)
{
	struct hpre_ctx *ctx = hpre_req->ctx;
	int id = hpre_req->req_id;

	if (hpre_req->req_id >= 0) {
		hpre_req->req_id = HPRE_INVLD_REQ_ID;
		ctx->req_list[id] = NULL;
		hpre_free_req_id(ctx, id);
	}
}

static struct hisi_qp *hpre_get_qp_and_start(u8 type)
{
	struct hisi_qp *qp;
	int ret;

	qp = hpre_create_qp(type);
	if (!qp) {
		pr_err("Can not create hpre qp!\n");
		return ERR_PTR(-ENODEV);
	}

	ret = hisi_qm_start_qp(qp, 0);
	if (ret < 0) {
		hisi_qm_free_qps(&qp, 1);
		pci_err(qp->qm->pdev, "Can not start qp!\n");
		return ERR_PTR(-EINVAL);
	}

	return qp;
}

static int hpre_get_data_dma_addr(struct hpre_asym_request *hpre_req,
				  struct scatterlist *data, unsigned int len,
				  int is_src, dma_addr_t *tmp)
{
	struct device *dev = hpre_req->ctx->dev;
	enum dma_data_direction dma_dir;

	if (is_src) {
		hpre_req->src = NULL;
		dma_dir = DMA_TO_DEVICE;
	} else {
		hpre_req->dst = NULL;
		dma_dir = DMA_FROM_DEVICE;
	}
	*tmp = dma_map_single(dev, sg_virt(data), len, dma_dir);
	if (unlikely(dma_mapping_error(dev, *tmp))) {
		dev_err(dev, "dma map data err!\n");
		return -ENOMEM;
	}

	return 0;
}

static int hpre_prepare_dma_buf(struct hpre_asym_request *hpre_req,
				struct scatterlist *data, unsigned int len,
				int is_src, dma_addr_t *tmp)
{
	struct hpre_ctx *ctx = hpre_req->ctx;
	struct device *dev = ctx->dev;
	void *ptr;
	int shift;

	shift = ctx->key_sz - len;
	if (unlikely(shift < 0))
		return -EINVAL;

	ptr = dma_alloc_coherent(dev, ctx->key_sz, tmp, GFP_ATOMIC);
	if (unlikely(!ptr))
		return -ENOMEM;

	if (is_src) {
		scatterwalk_map_and_copy(ptr + shift, data, 0, len, 0);
		hpre_req->src = ptr;
	} else {
		hpre_req->dst = ptr;
	}

	return 0;
}

static int hpre_hw_data_init(struct hpre_asym_request *hpre_req,
			     struct scatterlist *data, unsigned int len,
			     int is_src, int is_dh)
{
	struct hpre_sqe *msg = &hpre_req->req;
	struct hpre_ctx *ctx = hpre_req->ctx;
	dma_addr_t tmp = 0;
	int ret;

	/* when the data is dh's source, we should format it */
	if ((sg_is_last(data) && len == ctx->key_sz) &&
	    ((is_dh && !is_src) || !is_dh))
		ret = hpre_get_data_dma_addr(hpre_req, data, len, is_src, &tmp);
	else
		ret = hpre_prepare_dma_buf(hpre_req, data, len, is_src, &tmp);

	if (unlikely(ret))
		return ret;

	if (is_src)
		msg->in = cpu_to_le64(tmp);
	else
		msg->out = cpu_to_le64(tmp);

	return 0;
}

static void hpre_hw_data_clr_all(struct hpre_ctx *ctx,
				 struct hpre_asym_request *req,
				 struct scatterlist *dst,
				 struct scatterlist *src)
{
	struct device *dev = ctx->dev;
	struct hpre_sqe *sqe = &req->req;
	dma_addr_t tmp;

	tmp = le64_to_cpu(sqe->in);
	if (unlikely(dma_mapping_error(dev, tmp)))
		return;

	if (src) {
		if (req->src)
			dma_free_coherent(dev, ctx->key_sz, req->src, tmp);
		else
			dma_unmap_single(dev, tmp, ctx->key_sz, DMA_TO_DEVICE);
	}

	tmp = le64_to_cpu(sqe->out);
	if (unlikely(dma_mapping_error(dev, tmp)))
		return;

	if (req->dst) {
		if (dst)
			scatterwalk_map_and_copy(req->dst, dst, 0,
						 ctx->key_sz, 1);
		dma_free_coherent(dev, ctx->key_sz, req->dst, tmp);
	} else {
		dma_unmap_single(dev, tmp, ctx->key_sz, DMA_FROM_DEVICE);
	}
}

static int hpre_alg_res_post_hf(struct hpre_ctx *ctx, struct hpre_sqe *sqe,
				void **kreq)
{
	struct hpre_asym_request *req;
	unsigned int err, done, alg;
	int id;

#define HPRE_NO_HW_ERR		0
#define HPRE_HW_TASK_DONE	3
#define HREE_HW_ERR_MASK	GENMASK(10, 0)
#define HREE_SQE_DONE_MASK	GENMASK(1, 0)
#define HREE_ALG_TYPE_MASK	GENMASK(4, 0)
	id = (int)le16_to_cpu(sqe->tag);
	req = ctx->req_list[id];
	hpre_rm_req_from_ctx(req);
	*kreq = req;

	err = (le32_to_cpu(sqe->dw0) >> HPRE_SQE_ALG_BITS) &
		HREE_HW_ERR_MASK;

	done = (le32_to_cpu(sqe->dw0) >> HPRE_SQE_DONE_SHIFT) &
		HREE_SQE_DONE_MASK;

	if (likely(err == HPRE_NO_HW_ERR && done == HPRE_HW_TASK_DONE))
		return 0;

	alg = le32_to_cpu(sqe->dw0) & HREE_ALG_TYPE_MASK;
	dev_err_ratelimited(ctx->dev, "alg[0x%x] error: done[0x%x], etype[0x%x]\n",
		alg, done, err);

	return -EINVAL;
}

static int hpre_ctx_set(struct hpre_ctx *ctx, struct hisi_qp *qp, int qlen)
{
	struct hpre *hpre;

	if (!ctx || !qp || qlen < 0)
		return -EINVAL;

	spin_lock_init(&ctx->req_lock);
	ctx->qp = qp;
	ctx->dev = &qp->qm->pdev->dev;

	hpre = container_of(ctx->qp->qm, struct hpre, qm);
	ctx->hpre = hpre;
	ctx->req_list = kcalloc(qlen, sizeof(void *), GFP_KERNEL);
	if (!ctx->req_list)
		return -ENOMEM;
	ctx->key_sz = 0;
	ctx->crt_g2_mode = false;
	idr_init(&ctx->req_idr);

	return 0;
}

static void hpre_ctx_clear(struct hpre_ctx *ctx, bool is_clear_all)
{
	if (is_clear_all) {
		idr_destroy(&ctx->req_idr);
		kfree(ctx->req_list);
		hisi_qm_free_qps(&ctx->qp, 1);
	}

	ctx->crt_g2_mode = false;
	ctx->key_sz = 0;
}

static bool hpre_is_bd_timeout(struct hpre_asym_request *req,
			       u64 overtime_thrhld)
{
	struct timespec64 reply_time;
	u64 time_use_us;

	ktime_get_ts64(&reply_time);
	time_use_us = (reply_time.tv_sec - req->req_time.tv_sec) *
		HPRE_DFX_SEC_TO_US +
		(reply_time.tv_nsec - req->req_time.tv_nsec) /
		HPRE_DFX_US_TO_NS;

	if (time_use_us <= overtime_thrhld)
		return false;

	return true;
}

static void hpre_dh_cb(struct hpre_ctx *ctx, void *resp)
{
	struct hpre_dfx *dfx = ctx->hpre->debug.dfx;
	struct hpre_asym_request *req;
	struct kpp_request *areq;
	u64 overtime_thrhld;
	int ret;

	ret = hpre_alg_res_post_hf(ctx, resp, (void **)&req);
	areq = req->areq.dh;
	areq->dst_len = ctx->key_sz;

	overtime_thrhld = atomic64_read(&dfx[HPRE_OVERTIME_THRHLD].value);
	if (overtime_thrhld && hpre_is_bd_timeout(req, overtime_thrhld))
		atomic64_inc(&dfx[HPRE_OVER_THRHLD_CNT].value);

	hpre_hw_data_clr_all(ctx, req, areq->dst, areq->src);
	kpp_request_complete(areq, ret);
	atomic64_inc(&dfx[HPRE_RECV_CNT].value);
}

static void hpre_rsa_cb(struct hpre_ctx *ctx, void *resp)
{
	struct hpre_dfx *dfx = ctx->hpre->debug.dfx;
	struct hpre_asym_request *req;
	struct akcipher_request *areq;
	u64 overtime_thrhld;
	int ret;

	ret = hpre_alg_res_post_hf(ctx, resp, (void **)&req);

	overtime_thrhld = atomic64_read(&dfx[HPRE_OVERTIME_THRHLD].value);
	if (overtime_thrhld && hpre_is_bd_timeout(req, overtime_thrhld))
		atomic64_inc(&dfx[HPRE_OVER_THRHLD_CNT].value);

	areq = req->areq.rsa;
	areq->dst_len = ctx->key_sz;
	hpre_hw_data_clr_all(ctx, req, areq->dst, areq->src);
	akcipher_request_complete(areq, ret);
	atomic64_inc(&dfx[HPRE_RECV_CNT].value);
}

static void hpre_alg_cb(struct hisi_qp *qp, void *resp)
{
	struct hpre_ctx *ctx = qp->qp_ctx;
	struct hpre_dfx *dfx = ctx->hpre->debug.dfx;
	struct hpre_sqe *sqe = resp;
	struct hpre_asym_request *req = ctx->req_list[le16_to_cpu(sqe->tag)];

	if (unlikely(!req)) {
		atomic64_inc(&dfx[HPRE_INVALID_REQ_CNT].value);
		return;
	}

	req->cb(ctx, resp);
}

static void hpre_stop_qp_and_put(struct hisi_qp *qp)
{
	hisi_qm_stop_qp(qp);
	hisi_qm_free_qps(&qp, 1);
}

static int hpre_ctx_init(struct hpre_ctx *ctx, u8 type)
{
	struct hisi_qp *qp;
	int ret;

	qp = hpre_get_qp_and_start(type);
	if (IS_ERR(qp))
		return PTR_ERR(qp);

	qp->qp_ctx = ctx;
	qp->req_cb = hpre_alg_cb;

	ret = hpre_ctx_set(ctx, qp, QM_Q_DEPTH);
	if (ret)
		hpre_stop_qp_and_put(qp);

	return ret;
}

static int hpre_msg_request_set(struct hpre_ctx *ctx, void *req, bool is_rsa)
{
	struct hpre_asym_request *h_req;
	struct hpre_sqe *msg;
	int req_id;
	void *tmp;

	if (is_rsa) {
		struct akcipher_request *akreq = req;

		if (akreq->dst_len < ctx->key_sz) {
			akreq->dst_len = ctx->key_sz;
			return -EOVERFLOW;
		}

		tmp = akcipher_request_ctx(akreq);
		h_req = PTR_ALIGN(tmp, HPRE_ALIGN_SZ);
		h_req->cb = hpre_rsa_cb;
		h_req->areq.rsa = akreq;
		msg = &h_req->req;
		memset(msg, 0, sizeof(*msg));
	} else {
		struct kpp_request *kreq = req;

		if (kreq->dst_len < ctx->key_sz) {
			kreq->dst_len = ctx->key_sz;
			return -EOVERFLOW;
		}

		tmp = kpp_request_ctx(kreq);
		h_req = PTR_ALIGN(tmp, HPRE_ALIGN_SZ);
		h_req->cb = hpre_dh_cb;
		h_req->areq.dh = kreq;
		msg = &h_req->req;
		memset(msg, 0, sizeof(*msg));
		msg->key = cpu_to_le64(ctx->dh.dma_xa_p);
	}

	msg->in = cpu_to_le64(DMA_MAPPING_ERROR);
	msg->out = cpu_to_le64(DMA_MAPPING_ERROR);
	msg->dw0 |= cpu_to_le32(0x1 << HPRE_SQE_DONE_SHIFT);
	msg->task_len1 = (ctx->key_sz >> HPRE_BITS_2_BYTES_SHIFT) - 1;
	h_req->ctx = ctx;

	req_id = hpre_add_req_to_ctx(h_req);
	if (req_id < 0)
		return -EBUSY;

	msg->tag = cpu_to_le16((u16)req_id);

	return 0;
}

static int hpre_send(struct hpre_ctx *ctx, struct hpre_sqe *msg)
{
	struct hpre_dfx *dfx = ctx->hpre->debug.dfx;
	int ctr = 0;
	int ret;

	do {
		atomic64_inc(&dfx[HPRE_SEND_CNT].value);
		ret = hisi_qp_send(ctx->qp, msg);
		if (ret != -EBUSY)
			break;
		atomic64_inc(&dfx[HPRE_SEND_BUSY_CNT].value);
	} while (ctr++ < HPRE_TRY_SEND_TIMES);

	if (likely(!ret))
		return ret;

	if (ret != -EBUSY)
		atomic64_inc(&dfx[HPRE_SEND_FAIL_CNT].value);

	return ret;
}

static int hpre_dh_compute_value(struct kpp_request *req)
{
	struct crypto_kpp *tfm = crypto_kpp_reqtfm(req);
	struct hpre_ctx *ctx = kpp_tfm_ctx(tfm);
	void *tmp = kpp_request_ctx(req);
	struct hpre_asym_request *hpre_req = PTR_ALIGN(tmp, HPRE_ALIGN_SZ);
	struct hpre_sqe *msg = &hpre_req->req;
	int ret;

	ret = hpre_msg_request_set(ctx, req, false);
	if (unlikely(ret))
		return ret;

	if (req->src) {
		ret = hpre_hw_data_init(hpre_req, req->src, req->src_len, 1, 1);
		if (unlikely(ret))
			goto clear_all;
	} else {
		msg->in = cpu_to_le64(ctx->dh.dma_g);
	}

	ret = hpre_hw_data_init(hpre_req, req->dst, req->dst_len, 0, 1);
	if (unlikely(ret))
		goto clear_all;

	if (ctx->crt_g2_mode && !req->src)
		msg->dw0 = cpu_to_le32(le32_to_cpu(msg->dw0) | HPRE_ALG_DH_G2);
	else
		msg->dw0 = cpu_to_le32(le32_to_cpu(msg->dw0) | HPRE_ALG_DH);

	/* success */
	ret = hpre_send(ctx, msg);
	if (likely(!ret))
		return -EINPROGRESS;

clear_all:
	hpre_rm_req_from_ctx(hpre_req);
	hpre_hw_data_clr_all(ctx, hpre_req, req->dst, req->src);

	return ret;
}

static int hpre_is_dh_params_length_valid(unsigned int key_sz)
{
#define _HPRE_DH_GRP1		768
#define _HPRE_DH_GRP2		1024
#define _HPRE_DH_GRP5		1536
#define _HPRE_DH_GRP14		2048
#define _HPRE_DH_GRP15		3072
#define _HPRE_DH_GRP16		4096
	switch (key_sz) {
	case _HPRE_DH_GRP1:
	case _HPRE_DH_GRP2:
	case _HPRE_DH_GRP5:
	case _HPRE_DH_GRP14:
	case _HPRE_DH_GRP15:
	case _HPRE_DH_GRP16:
		return 0;
	default:
		return -EINVAL;
	}
}

static int hpre_dh_set_params(struct hpre_ctx *ctx, struct dh *params)
{
	struct device *dev = ctx->dev;
	unsigned int sz;

	if (params->p_size > HPRE_DH_MAX_P_SZ)
		return -EINVAL;

	if (hpre_is_dh_params_length_valid(params->p_size <<
					   HPRE_BITS_2_BYTES_SHIFT))
		return -EINVAL;

	sz = ctx->key_sz = params->p_size;
	ctx->dh.xa_p = dma_alloc_coherent(dev, sz << 1,
					  &ctx->dh.dma_xa_p, GFP_KERNEL);
	if (!ctx->dh.xa_p)
		return -ENOMEM;

	memcpy(ctx->dh.xa_p + sz, params->p, sz);

	/* If g equals 2 don't copy it */
	if (params->g_size == 1 && *(char *)params->g == HPRE_DH_G_FLAG) {
		ctx->crt_g2_mode = true;
		return 0;
	}

	ctx->dh.g = dma_alloc_coherent(dev, sz, &ctx->dh.dma_g, GFP_KERNEL);
	if (!ctx->dh.g) {
		dma_free_coherent(dev, sz << 1, ctx->dh.xa_p,
				  ctx->dh.dma_xa_p);
		ctx->dh.xa_p = NULL;
		return -ENOMEM;
	}

	memcpy(ctx->dh.g + (sz - params->g_size), params->g, params->g_size);

	return 0;
}

static void hpre_dh_clear_ctx(struct hpre_ctx *ctx, bool is_clear_all)
{
	struct device *dev = ctx->dev;
	unsigned int sz = ctx->key_sz;

	if (is_clear_all)
		hisi_qm_stop_qp(ctx->qp);

	if (ctx->dh.g) {
		dma_free_coherent(dev, sz, ctx->dh.g, ctx->dh.dma_g);
		ctx->dh.g = NULL;
	}

	if (ctx->dh.xa_p) {
		memzero_explicit(ctx->dh.xa_p, sz);
		dma_free_coherent(dev, sz << 1, ctx->dh.xa_p,
				  ctx->dh.dma_xa_p);
		ctx->dh.xa_p = NULL;
	}

	hpre_ctx_clear(ctx, is_clear_all);
}

static int hpre_dh_set_secret(struct crypto_kpp *tfm, const void *buf,
			      unsigned int len)
{
	struct hpre_ctx *ctx = kpp_tfm_ctx(tfm);
	struct dh params;
	int ret;

	if (crypto_dh_decode_key(buf, len, &params) < 0)
		return -EINVAL;

	/* Free old secret if any */
	hpre_dh_clear_ctx(ctx, false);

	ret = hpre_dh_set_params(ctx, &params);
	if (ret < 0)
		goto err_clear_ctx;

	memcpy(ctx->dh.xa_p + (ctx->key_sz - params.key_size), params.key,
	       params.key_size);

	return 0;

err_clear_ctx:
	hpre_dh_clear_ctx(ctx, false);
	return ret;
}

static unsigned int hpre_dh_max_size(struct crypto_kpp *tfm)
{
	struct hpre_ctx *ctx = kpp_tfm_ctx(tfm);

	return ctx->key_sz;
}

static int hpre_dh_init_tfm(struct crypto_kpp *tfm)
{
	struct hpre_ctx *ctx = kpp_tfm_ctx(tfm);

	return hpre_ctx_init(ctx, HPRE_V2_ALG_TYPE);
}

static void hpre_dh_exit_tfm(struct crypto_kpp *tfm)
{
	struct hpre_ctx *ctx = kpp_tfm_ctx(tfm);

	hpre_dh_clear_ctx(ctx, true);
}

static void hpre_rsa_drop_leading_zeros(const char **ptr, size_t *len)
{
	while (!**ptr && *len) {
		(*ptr)++;
		(*len)--;
	}
}

static bool hpre_rsa_key_size_is_support(unsigned int len)
{
	unsigned int bits = len << HPRE_BITS_2_BYTES_SHIFT;

#define _RSA_1024BITS_KEY_WDTH		1024
#define _RSA_2048BITS_KEY_WDTH		2048
#define _RSA_3072BITS_KEY_WDTH		3072
#define _RSA_4096BITS_KEY_WDTH		4096

	switch (bits) {
	case _RSA_1024BITS_KEY_WDTH:
	case _RSA_2048BITS_KEY_WDTH:
	case _RSA_3072BITS_KEY_WDTH:
	case _RSA_4096BITS_KEY_WDTH:
		return true;
	default:
		return false;
	}
}

static int hpre_rsa_enc(struct akcipher_request *req)
{
	struct crypto_akcipher *tfm = crypto_akcipher_reqtfm(req);
	struct hpre_ctx *ctx = akcipher_tfm_ctx(tfm);
	void *tmp = akcipher_request_ctx(req);
	struct hpre_asym_request *hpre_req = PTR_ALIGN(tmp, HPRE_ALIGN_SZ);
	struct hpre_sqe *msg = &hpre_req->req;
	int ret;

	/* For 512 and 1536 bits key size, use soft tfm instead */
	if (ctx->key_sz == HPRE_RSA_512BITS_KSZ ||
	    ctx->key_sz == HPRE_RSA_1536BITS_KSZ) {
		akcipher_request_set_tfm(req, ctx->rsa.soft_tfm);
		ret = crypto_akcipher_encrypt(req);
		akcipher_request_set_tfm(req, tfm);
		return ret;
	}

	if (unlikely(!ctx->rsa.pubkey))
		return -EINVAL;

	ret = hpre_msg_request_set(ctx, req, true);
	if (unlikely(ret))
		return ret;

	msg->dw0 |= cpu_to_le32(HPRE_ALG_NC_NCRT);
	msg->key = cpu_to_le64(ctx->rsa.dma_pubkey);

	ret = hpre_hw_data_init(hpre_req, req->src, req->src_len, 1, 0);
	if (unlikely(ret))
		goto clear_all;

	ret = hpre_hw_data_init(hpre_req, req->dst, req->dst_len, 0, 0);
	if (unlikely(ret))
		goto clear_all;

	/* success */
	ret = hpre_send(ctx, msg);
	if (likely(!ret))
		return -EINPROGRESS;

clear_all:
	hpre_rm_req_from_ctx(hpre_req);
	hpre_hw_data_clr_all(ctx, hpre_req, req->dst, req->src);

	return ret;
}

static int hpre_rsa_dec(struct akcipher_request *req)
{
	struct crypto_akcipher *tfm = crypto_akcipher_reqtfm(req);
	struct hpre_ctx *ctx = akcipher_tfm_ctx(tfm);
	void *tmp = akcipher_request_ctx(req);
	struct hpre_asym_request *hpre_req = PTR_ALIGN(tmp, HPRE_ALIGN_SZ);
	struct hpre_sqe *msg = &hpre_req->req;
	int ret;

	/* For 512 and 1536 bits key size, use soft tfm instead */
	if (ctx->key_sz == HPRE_RSA_512BITS_KSZ ||
	    ctx->key_sz == HPRE_RSA_1536BITS_KSZ) {
		akcipher_request_set_tfm(req, ctx->rsa.soft_tfm);
		ret = crypto_akcipher_decrypt(req);
		akcipher_request_set_tfm(req, tfm);
		return ret;
	}

	if (unlikely(!ctx->rsa.prikey))
		return -EINVAL;

	ret = hpre_msg_request_set(ctx, req, true);
	if (unlikely(ret))
		return ret;

	if (ctx->crt_g2_mode) {
		msg->key = cpu_to_le64(ctx->rsa.dma_crt_prikey);
		msg->dw0 = cpu_to_le32(le32_to_cpu(msg->dw0) |
				       HPRE_ALG_NC_CRT);
	} else {
		msg->key = cpu_to_le64(ctx->rsa.dma_prikey);
		msg->dw0 = cpu_to_le32(le32_to_cpu(msg->dw0) |
				       HPRE_ALG_NC_NCRT);
	}

	ret = hpre_hw_data_init(hpre_req, req->src, req->src_len, 1, 0);
	if (unlikely(ret))
		goto clear_all;

	ret = hpre_hw_data_init(hpre_req, req->dst, req->dst_len, 0, 0);
	if (unlikely(ret))
		goto clear_all;

	/* success */
	ret = hpre_send(ctx, msg);
	if (likely(!ret))
		return -EINPROGRESS;

clear_all:
	hpre_rm_req_from_ctx(hpre_req);
	hpre_hw_data_clr_all(ctx, hpre_req, req->dst, req->src);

	return ret;
}

static int hpre_rsa_set_n(struct hpre_ctx *ctx, const char *value,
			  size_t vlen, bool private)
{
	const char *ptr = value;

	hpre_rsa_drop_leading_zeros(&ptr, &vlen);

	ctx->key_sz = vlen;

	/* if invalid key size provided, we use software tfm */
	if (!hpre_rsa_key_size_is_support(ctx->key_sz))
		return 0;

	ctx->rsa.pubkey = dma_alloc_coherent(ctx->dev, vlen << 1,
					     &ctx->rsa.dma_pubkey,
					     GFP_KERNEL);
	if (!ctx->rsa.pubkey)
		return -ENOMEM;

	if (private) {
		ctx->rsa.prikey = dma_alloc_coherent(ctx->dev, vlen << 1,
						     &ctx->rsa.dma_prikey,
						     GFP_KERNEL);
		if (!ctx->rsa.prikey) {
			dma_free_coherent(ctx->dev, vlen << 1,
					  ctx->rsa.pubkey,
					  ctx->rsa.dma_pubkey);
			ctx->rsa.pubkey = NULL;
			return -ENOMEM;
		}
		memcpy(ctx->rsa.prikey + vlen, ptr, vlen);
	}
	memcpy(ctx->rsa.pubkey + vlen, ptr, vlen);

	/* Using hardware HPRE to do RSA */
	return 1;
}

static int hpre_rsa_set_e(struct hpre_ctx *ctx, const char *value,
			  size_t vlen)
{
	const char *ptr = value;

	hpre_rsa_drop_leading_zeros(&ptr, &vlen);

	if (!ctx->key_sz || !vlen || vlen > ctx->key_sz)
		return -EINVAL;

	memcpy(ctx->rsa.pubkey + ctx->key_sz - vlen, ptr, vlen);

	return 0;
}

static int hpre_rsa_set_d(struct hpre_ctx *ctx, const char *value,
			  size_t vlen)
{
	const char *ptr = value;

	hpre_rsa_drop_leading_zeros(&ptr, &vlen);

	if (!ctx->key_sz || !vlen || vlen > ctx->key_sz)
		return -EINVAL;

	memcpy(ctx->rsa.prikey + ctx->key_sz - vlen, ptr, vlen);

	return 0;
}

static int hpre_crt_para_get(char *para, size_t para_sz,
			     const char *raw, size_t raw_sz)
{
	const char *ptr = raw;
	size_t len = raw_sz;

	hpre_rsa_drop_leading_zeros(&ptr, &len);
	if (!len || len > para_sz)
		return -EINVAL;

	memcpy(para + para_sz - len, ptr, len);

	return 0;
}

static int hpre_rsa_setkey_crt(struct hpre_ctx *ctx, struct rsa_key *rsa_key)
{
	unsigned int hlf_ksz = ctx->key_sz >> 1;
	struct device *dev = ctx->dev;
	u64 offset;
	int ret;

	ctx->rsa.crt_prikey = dma_alloc_coherent(dev, hlf_ksz * HPRE_CRT_PRMS,
					&ctx->rsa.dma_crt_prikey,
					GFP_KERNEL);
	if (!ctx->rsa.crt_prikey)
		return -ENOMEM;

	ret = hpre_crt_para_get(ctx->rsa.crt_prikey, hlf_ksz,
				rsa_key->dq, rsa_key->dq_sz);
	if (ret)
		goto free_key;

	offset = hlf_ksz;
	ret = hpre_crt_para_get(ctx->rsa.crt_prikey + offset, hlf_ksz,
				rsa_key->dp, rsa_key->dp_sz);
	if (ret)
		goto free_key;

	offset = hlf_ksz * HPRE_CRT_Q;
	ret = hpre_crt_para_get(ctx->rsa.crt_prikey + offset, hlf_ksz,
				rsa_key->q, rsa_key->q_sz);
	if (ret)
		goto free_key;

	offset = hlf_ksz * HPRE_CRT_P;
	ret = hpre_crt_para_get(ctx->rsa.crt_prikey + offset, hlf_ksz,
				rsa_key->p, rsa_key->p_sz);
	if (ret)
		goto free_key;

	offset = hlf_ksz * HPRE_CRT_INV;
	ret = hpre_crt_para_get(ctx->rsa.crt_prikey + offset, hlf_ksz,
				rsa_key->qinv, rsa_key->qinv_sz);
	if (ret)
		goto free_key;

	ctx->crt_g2_mode = true;

	return 0;

free_key:
	offset = hlf_ksz * HPRE_CRT_PRMS;
	memzero_explicit(ctx->rsa.crt_prikey, offset);
	dma_free_coherent(dev, hlf_ksz * HPRE_CRT_PRMS, ctx->rsa.crt_prikey,
			  ctx->rsa.dma_crt_prikey);
	ctx->rsa.crt_prikey = NULL;
	ctx->crt_g2_mode = false;

	return ret;
}

/* If it is clear all, all the resources of the QP will be cleaned. */
static void hpre_rsa_clear_ctx(struct hpre_ctx *ctx, bool is_clear_all)
{
	unsigned int half_key_sz = ctx->key_sz >> 1;
	struct device *dev = ctx->dev;

	if (is_clear_all)
		hisi_qm_stop_qp(ctx->qp);

	if (ctx->rsa.pubkey) {
		dma_free_coherent(dev, ctx->key_sz << 1,
				  ctx->rsa.pubkey, ctx->rsa.dma_pubkey);
		ctx->rsa.pubkey = NULL;
	}

	if (ctx->rsa.crt_prikey) {
		memzero_explicit(ctx->rsa.crt_prikey,
				 half_key_sz * HPRE_CRT_PRMS);
		dma_free_coherent(dev, half_key_sz * HPRE_CRT_PRMS,
				  ctx->rsa.crt_prikey, ctx->rsa.dma_crt_prikey);
		ctx->rsa.crt_prikey = NULL;
	}

	if (ctx->rsa.prikey) {
		memzero_explicit(ctx->rsa.prikey, ctx->key_sz);
		dma_free_coherent(dev, ctx->key_sz << 1, ctx->rsa.prikey,
				  ctx->rsa.dma_prikey);
		ctx->rsa.prikey = NULL;
	}

	hpre_ctx_clear(ctx, is_clear_all);
}

/*
 * we should judge if it is CRT or not,
 * CRT: return true,  N-CRT: return false .
 */
static bool hpre_is_crt_key(struct rsa_key *key)
{
	u16 len = key->p_sz + key->q_sz + key->dp_sz + key->dq_sz +
		  key->qinv_sz;

#define LEN_OF_NCRT_PARA	5

	/* N-CRT less than 5 parameters */
	return len > LEN_OF_NCRT_PARA;
}

static int hpre_rsa_setkey(struct hpre_ctx *ctx, const void *key,
			   unsigned int keylen, bool private)
{
	struct rsa_key rsa_key;
	int ret;

	hpre_rsa_clear_ctx(ctx, false);

	if (private)
		ret = rsa_parse_priv_key(&rsa_key, key, keylen);
	else
		ret = rsa_parse_pub_key(&rsa_key, key, keylen);
	if (ret < 0)
		return ret;

	ret = hpre_rsa_set_n(ctx, rsa_key.n, rsa_key.n_sz, private);
	if (ret <= 0)
		return ret;

	if (private) {
		ret = hpre_rsa_set_d(ctx, rsa_key.d, rsa_key.d_sz);
		if (ret < 0)
			goto free;

		if (hpre_is_crt_key(&rsa_key)) {
			ret = hpre_rsa_setkey_crt(ctx, &rsa_key);
			if (ret < 0)
				goto free;
		}
	}

	ret = hpre_rsa_set_e(ctx, rsa_key.e, rsa_key.e_sz);
	if (ret < 0)
		goto free;

	if ((private && !ctx->rsa.prikey) || !ctx->rsa.pubkey) {
		ret = -EINVAL;
		goto free;
	}

	return 0;

free:
	hpre_rsa_clear_ctx(ctx, false);
	return ret;
}

static int hpre_rsa_setpubkey(struct crypto_akcipher *tfm, const void *key,
			      unsigned int keylen)
{
	struct hpre_ctx *ctx = akcipher_tfm_ctx(tfm);
	int ret;

	ret = crypto_akcipher_set_pub_key(ctx->rsa.soft_tfm, key, keylen);
	if (ret)
		return ret;

	return hpre_rsa_setkey(ctx, key, keylen, false);
}

static int hpre_rsa_setprivkey(struct crypto_akcipher *tfm, const void *key,
			       unsigned int keylen)
{
	struct hpre_ctx *ctx = akcipher_tfm_ctx(tfm);
	int ret;

	ret = crypto_akcipher_set_priv_key(ctx->rsa.soft_tfm, key, keylen);
	if (ret)
		return ret;

	return hpre_rsa_setkey(ctx, key, keylen, true);
}

static unsigned int hpre_rsa_max_size(struct crypto_akcipher *tfm)
{
	struct hpre_ctx *ctx = akcipher_tfm_ctx(tfm);

	/* For 512 and 1536 bits key size, use soft tfm instead */
	if (ctx->key_sz == HPRE_RSA_512BITS_KSZ ||
	    ctx->key_sz == HPRE_RSA_1536BITS_KSZ)
		return crypto_akcipher_maxsize(ctx->rsa.soft_tfm);

	return ctx->key_sz;
}

static int hpre_rsa_init_tfm(struct crypto_akcipher *tfm)
{
	struct hpre_ctx *ctx = akcipher_tfm_ctx(tfm);
	int ret;

	ctx->rsa.soft_tfm = crypto_alloc_akcipher("rsa-generic", 0, 0);
	if (IS_ERR(ctx->rsa.soft_tfm)) {
		pr_err("Can not alloc_akcipher!\n");
		return PTR_ERR(ctx->rsa.soft_tfm);
	}

	ret = hpre_ctx_init(ctx, HPRE_V2_ALG_TYPE);
	if (ret)
		crypto_free_akcipher(ctx->rsa.soft_tfm);

	return ret;
}

static void hpre_rsa_exit_tfm(struct crypto_akcipher *tfm)
{
	struct hpre_ctx *ctx = akcipher_tfm_ctx(tfm);

	hpre_rsa_clear_ctx(ctx, true);
	crypto_free_akcipher(ctx->rsa.soft_tfm);
}

static void hpre_key_to_big_end(u8 *data, int len)
{
	int i, j;
	u8 tmp;

	for (i = 0; i < len / 2; i++) {
		j = len - i - 1;
		tmp = data[j];
		data[j] = data[i];
		data[i] = tmp;
	}
}

static void hpre_ecc_clear_ctx(struct hpre_ctx *ctx, bool is_clear_all,
			       bool is_ecdh)
{
	struct device *dev = ctx->dev;
	unsigned int sz = ctx->key_sz;
	unsigned int shift = sz << 1;

	if (is_clear_all)
		hisi_qm_stop_qp(ctx->qp);

	if (is_ecdh && ctx->ecdh.p) {
		/* ecdh: p->a->k->b */
		memzero_explicit(ctx->ecdh.p + shift, sz);
		dma_free_coherent(dev, sz << 3, ctx->ecdh.p, ctx->ecdh.dma_p);
		ctx->ecdh.p = NULL;
	} else if (!is_ecdh && ctx->curve25519.p) {
		/* curve25519: p->a->k */
		memzero_explicit(ctx->curve25519.p + shift, sz);
		dma_free_coherent(dev, sz << 2, ctx->curve25519.p,
				  ctx->curve25519.dma_p);
		ctx->curve25519.p = NULL;
	}

	hpre_ctx_clear(ctx, is_clear_all);
}

/*
 * The bits of 192/224/256/384/521 are supported by HPRE,
 * and convert the bits like:
 * bits<=256, bits=256; 256<bits<=384, bits=384; 384<bits<=576, bits=576;
 * If the parameter bit width is insufficient, then we fill in the
 * high-order zeros by soft, so TASK_LENGTH1 is 0x3/0x5/0x8;
 */
static unsigned int hpre_ecdh_supported_curve(unsigned short id)
{
	switch (id) {
	case ECC_CURVE_NIST_P192:
	case ECC_CURVE_NIST_P256:
		return HPRE_ECC_HW256_KSZ_B;
	case ECC_CURVE_NIST_P384:
		return HPRE_ECC_HW384_KSZ_B;
	default:
		break;
	}

	return 0;
}

static void fill_curve_param(void *addr, u64 *param, unsigned int cur_sz, u8 ndigits)
{
	unsigned int sz = cur_sz - (ndigits - 1) * sizeof(u64);
	u8 i = 0;

	while (i < ndigits - 1) {
		memcpy(addr + sizeof(u64) * i, &param[i], sizeof(u64));
		i++;
	}

	memcpy(addr + sizeof(u64) * i, &param[ndigits - 1], sz);
	hpre_key_to_big_end((u8 *)addr, cur_sz);
}

static int hpre_ecdh_fill_curve(struct hpre_ctx *ctx, struct ecdh *params,
				unsigned int cur_sz)
{
	unsigned int shifta = ctx->key_sz << 1;
	unsigned int shiftb = ctx->key_sz << 2;
	void *p = ctx->ecdh.p + ctx->key_sz - cur_sz;
	void *a = ctx->ecdh.p + shifta - cur_sz;
	void *b = ctx->ecdh.p + shiftb - cur_sz;
	void *x = ctx->ecdh.g + ctx->key_sz - cur_sz;
	void *y = ctx->ecdh.g + shifta - cur_sz;
	const struct ecc_curve *curve = ecc_get_curve(ctx->curve_id);
	char *n;

	if (unlikely(!curve))
		return -EINVAL;

	n = kzalloc(ctx->key_sz, GFP_KERNEL);
	if (!n)
		return -ENOMEM;

	fill_curve_param(p, curve->p, cur_sz, curve->g.ndigits);
	fill_curve_param(a, curve->a, cur_sz, curve->g.ndigits);
	fill_curve_param(b, curve->b, cur_sz, curve->g.ndigits);
	fill_curve_param(x, curve->g.x, cur_sz, curve->g.ndigits);
	fill_curve_param(y, curve->g.y, cur_sz, curve->g.ndigits);
	fill_curve_param(n, curve->n, cur_sz, curve->g.ndigits);

	if (params->key_size == cur_sz && memcmp(params->key, n, cur_sz) >= 0) {
		kfree(n);
		return -EINVAL;
	}

	kfree(n);
	return 0;
}

static unsigned int hpre_ecdh_get_curvesz(unsigned short id)
{
	switch (id) {
	case ECC_CURVE_NIST_P192:
		return HPRE_ECC_NIST_P192_N_SIZE;
	case ECC_CURVE_NIST_P256:
		return HPRE_ECC_NIST_P256_N_SIZE;
	case ECC_CURVE_NIST_P384:
		return HPRE_ECC_NIST_P384_N_SIZE;
	default:
		break;
	}

	return 0;
}

static int hpre_ecdh_set_param(struct hpre_ctx *ctx, struct ecdh *params)
{
	struct device *dev = ctx->dev;
	unsigned int sz, shift, curve_sz;
	int ret;

	ctx->key_sz = hpre_ecdh_supported_curve(ctx->curve_id);
	if (!ctx->key_sz)
		return -EINVAL;

	curve_sz = hpre_ecdh_get_curvesz(ctx->curve_id);
	if (!curve_sz || params->key_size > curve_sz)
		return -EINVAL;

	sz = ctx->key_sz;

	if (!ctx->ecdh.p) {
		ctx->ecdh.p = dma_alloc_coherent(dev, sz << 3, &ctx->ecdh.dma_p,
						 GFP_KERNEL);
		if (!ctx->ecdh.p)
			return -ENOMEM;
	}

	shift = sz << 2;
	ctx->ecdh.g = ctx->ecdh.p + shift;
	ctx->ecdh.dma_g = ctx->ecdh.dma_p + shift;

	ret = hpre_ecdh_fill_curve(ctx, params, curve_sz);
	if (ret) {
		dev_err(dev, "failed to fill curve_param, ret = %d!\n", ret);
		dma_free_coherent(dev, sz << 3, ctx->ecdh.p, ctx->ecdh.dma_p);
		ctx->ecdh.p = NULL;
		return ret;
	}

	return 0;
}

static bool hpre_key_is_zero(char *key, unsigned short key_sz)
{
	int i;

	for (i = 0; i < key_sz; i++)
		if (key[i])
			return false;

	return true;
}

static int ecdh_gen_privkey(struct hpre_ctx *ctx, struct ecdh *params)
{
	struct device *dev = ctx->dev;
	int ret;

	ret = crypto_get_default_rng();
	if (ret) {
		dev_err(dev, "failed to get default rng, ret = %d!\n", ret);
		return ret;
	}

	ret = crypto_rng_get_bytes(crypto_default_rng, (u8 *)params->key,
				   params->key_size);
	crypto_put_default_rng();
	if (ret)
		dev_err(dev, "failed to get rng, ret = %d!\n", ret);

	return ret;
}

static int hpre_ecdh_set_secret(struct crypto_kpp *tfm, const void *buf,
				unsigned int len)
{
	struct hpre_ctx *ctx = kpp_tfm_ctx(tfm);
	struct device *dev = ctx->dev;
	char key[HPRE_ECC_MAX_KSZ];
	unsigned int sz, sz_shift;
	struct ecdh params;
	int ret;

	if (crypto_ecdh_decode_key(buf, len, &params) < 0) {
		dev_err(dev, "failed to decode ecdh key!\n");
		return -EINVAL;
	}

	/* Use stdrng to generate private key */
	if (!params.key || !params.key_size) {
		params.key = key;
		params.key_size = hpre_ecdh_get_curvesz(ctx->curve_id);
		ret = ecdh_gen_privkey(ctx, &params);
		if (ret)
			return ret;
	}

	if (hpre_key_is_zero(params.key, params.key_size)) {
		dev_err(dev, "Invalid hpre key!\n");
		return -EINVAL;
	}

	hpre_ecc_clear_ctx(ctx, false, true);

	ret = hpre_ecdh_set_param(ctx, &params);
	if (ret < 0) {
		dev_err(dev, "failed to set hpre param, ret = %d!\n", ret);
		return ret;
	}

	sz = ctx->key_sz;
	sz_shift = (sz << 1) + sz - params.key_size;
	memcpy(ctx->ecdh.p + sz_shift, params.key, params.key_size);

	return 0;
}

static void hpre_ecdh_hw_data_clr_all(struct hpre_ctx *ctx,
				      struct hpre_asym_request *req,
				      struct scatterlist *dst,
				      struct scatterlist *src)
{
	struct device *dev = ctx->dev;
	struct hpre_sqe *sqe = &req->req;
	dma_addr_t dma;

	dma = le64_to_cpu(sqe->in);
	if (unlikely(dma_mapping_error(dev, dma)))
		return;

	if (src && req->src)
		dma_free_coherent(dev, ctx->key_sz << 2, req->src, dma);

	dma = le64_to_cpu(sqe->out);
	if (unlikely(dma_mapping_error(dev, dma)))
		return;

	if (req->dst)
		dma_free_coherent(dev, ctx->key_sz << 1, req->dst, dma);
	if (dst)
		dma_unmap_single(dev, dma, ctx->key_sz << 1, DMA_FROM_DEVICE);
}

static void hpre_ecdh_cb(struct hpre_ctx *ctx, void *resp)
{
	unsigned int curve_sz = hpre_ecdh_get_curvesz(ctx->curve_id);
	struct hpre_dfx *dfx = ctx->hpre->debug.dfx;
	struct hpre_asym_request *req = NULL;
	struct kpp_request *areq;
	u64 overtime_thrhld;
	char *p;
	int ret;

	ret = hpre_alg_res_post_hf(ctx, resp, (void **)&req);
	areq = req->areq.ecdh;
	areq->dst_len = ctx->key_sz << 1;

	overtime_thrhld = atomic64_read(&dfx[HPRE_OVERTIME_THRHLD].value);
	if (overtime_thrhld && hpre_is_bd_timeout(req, overtime_thrhld))
		atomic64_inc(&dfx[HPRE_OVER_THRHLD_CNT].value);

	p = sg_virt(areq->dst);
	memmove(p, p + ctx->key_sz - curve_sz, curve_sz);
	memmove(p + curve_sz, p + areq->dst_len - curve_sz, curve_sz);

	hpre_ecdh_hw_data_clr_all(ctx, req, areq->dst, areq->src);
	kpp_request_complete(areq, ret);

	atomic64_inc(&dfx[HPRE_RECV_CNT].value);
}

static int hpre_ecdh_msg_request_set(struct hpre_ctx *ctx,
				     struct kpp_request *req)
{
	struct hpre_asym_request *h_req;
	struct hpre_sqe *msg;
	int req_id;
	void *tmp;

	if (req->dst_len < ctx->key_sz << 1) {
		req->dst_len = ctx->key_sz << 1;
		return -EINVAL;
	}

	tmp = kpp_request_ctx(req);
	h_req = PTR_ALIGN(tmp, HPRE_ALIGN_SZ);
	h_req->cb = hpre_ecdh_cb;
	h_req->areq.ecdh = req;
	msg = &h_req->req;
	memset(msg, 0, sizeof(*msg));
	msg->in = cpu_to_le64(DMA_MAPPING_ERROR);
	msg->out = cpu_to_le64(DMA_MAPPING_ERROR);
	msg->key = cpu_to_le64(ctx->ecdh.dma_p);

	msg->dw0 |= cpu_to_le32(0x1U << HPRE_SQE_DONE_SHIFT);
	msg->task_len1 = (ctx->key_sz >> HPRE_BITS_2_BYTES_SHIFT) - 1;
	h_req->ctx = ctx;

	req_id = hpre_add_req_to_ctx(h_req);
	if (req_id < 0)
		return -EBUSY;

	msg->tag = cpu_to_le16((u16)req_id);
	return 0;
}

static int hpre_ecdh_src_data_init(struct hpre_asym_request *hpre_req,
				   struct scatterlist *data, unsigned int len)
{
	struct hpre_sqe *msg = &hpre_req->req;
	struct hpre_ctx *ctx = hpre_req->ctx;
	struct device *dev = ctx->dev;
	unsigned int tmpshift;
	dma_addr_t dma = 0;
	void *ptr;
	int shift;

	/* Src_data include gx and gy. */
	shift = ctx->key_sz - (len >> 1);
	if (unlikely(shift < 0))
		return -EINVAL;

	ptr = dma_alloc_coherent(dev, ctx->key_sz << 2, &dma, GFP_KERNEL);
	if (unlikely(!ptr))
		return -ENOMEM;

	tmpshift = ctx->key_sz << 1;
	scatterwalk_map_and_copy(ptr + tmpshift, data, 0, len, 0);
	memcpy(ptr + shift, ptr + tmpshift, len >> 1);
	memcpy(ptr + ctx->key_sz + shift, ptr + tmpshift + (len >> 1), len >> 1);

	hpre_req->src = ptr;
	msg->in = cpu_to_le64(dma);
	return 0;
}

static int hpre_ecdh_dst_data_init(struct hpre_asym_request *hpre_req,
				   struct scatterlist *data, unsigned int len)
{
	struct hpre_sqe *msg = &hpre_req->req;
	struct hpre_ctx *ctx = hpre_req->ctx;
	struct device *dev = ctx->dev;
	dma_addr_t dma;

	if (unlikely(!data || !sg_is_last(data) || len != ctx->key_sz << 1)) {
		dev_err(dev, "data or data length is illegal!\n");
		return -EINVAL;
	}

	hpre_req->dst = NULL;
	dma = dma_map_single(dev, sg_virt(data), len, DMA_FROM_DEVICE);
	if (unlikely(dma_mapping_error(dev, dma))) {
		dev_err(dev, "dma map data err!\n");
		return -ENOMEM;
	}

	msg->out = cpu_to_le64(dma);
	return 0;
}

static int hpre_ecdh_compute_value(struct kpp_request *req)
{
	struct crypto_kpp *tfm = crypto_kpp_reqtfm(req);
	struct hpre_ctx *ctx = kpp_tfm_ctx(tfm);
	struct device *dev = ctx->dev;
	void *tmp = kpp_request_ctx(req);
	struct hpre_asym_request *hpre_req = PTR_ALIGN(tmp, HPRE_ALIGN_SZ);
	struct hpre_sqe *msg = &hpre_req->req;
	int ret;

	ret = hpre_ecdh_msg_request_set(ctx, req);
	if (unlikely(ret)) {
		dev_err(dev, "failed to set ecdh request, ret = %d!\n", ret);
		return ret;
	}

	if (req->src) {
		ret = hpre_ecdh_src_data_init(hpre_req, req->src, req->src_len);
		if (unlikely(ret)) {
			dev_err(dev, "failed to init src data, ret = %d!\n", ret);
			goto clear_all;
		}
	} else {
		msg->in = cpu_to_le64(ctx->ecdh.dma_g);
	}

	ret = hpre_ecdh_dst_data_init(hpre_req, req->dst, req->dst_len);
	if (unlikely(ret)) {
		dev_err(dev, "failed to init dst data, ret = %d!\n", ret);
		goto clear_all;
	}

	msg->dw0 = cpu_to_le32(le32_to_cpu(msg->dw0) | HPRE_ALG_ECC_MUL);
	ret = hpre_send(ctx, msg);
	if (likely(!ret))
		return -EINPROGRESS;

clear_all:
	hpre_rm_req_from_ctx(hpre_req);
	hpre_ecdh_hw_data_clr_all(ctx, hpre_req, req->dst, req->src);
	return ret;
}

static unsigned int hpre_ecdh_max_size(struct crypto_kpp *tfm)
{
	struct hpre_ctx *ctx = kpp_tfm_ctx(tfm);

	/* max size is the pub_key_size, include x and y */
	return ctx->key_sz << 1;
}

static int hpre_ecdh_nist_p192_init_tfm(struct crypto_kpp *tfm)
{
	struct hpre_ctx *ctx = kpp_tfm_ctx(tfm);

	ctx->curve_id = ECC_CURVE_NIST_P192;

	return hpre_ctx_init(ctx, HPRE_V3_ECC_ALG_TYPE);
}

static int hpre_ecdh_nist_p256_init_tfm(struct crypto_kpp *tfm)
{
	struct hpre_ctx *ctx = kpp_tfm_ctx(tfm);

	ctx->curve_id = ECC_CURVE_NIST_P256;

	return hpre_ctx_init(ctx, HPRE_V3_ECC_ALG_TYPE);
}

static int hpre_ecdh_nist_p384_init_tfm(struct crypto_kpp *tfm)
{
	struct hpre_ctx *ctx = kpp_tfm_ctx(tfm);

	ctx->curve_id = ECC_CURVE_NIST_P384;

	return hpre_ctx_init(ctx, HPRE_V3_ECC_ALG_TYPE);
}

static void hpre_ecdh_exit_tfm(struct crypto_kpp *tfm)
{
	struct hpre_ctx *ctx = kpp_tfm_ctx(tfm);

	hpre_ecc_clear_ctx(ctx, true, true);
}

static void hpre_curve25519_fill_curve(struct hpre_ctx *ctx, const void *buf,
				       unsigned int len)
{
	u8 secret[CURVE25519_KEY_SIZE] = { 0 };
	unsigned int sz = ctx->key_sz;
	const struct ecc_curve *curve;
	unsigned int shift = sz << 1;
	void *p;

	/*
	 * The key from 'buf' is in little-endian, we should preprocess it as
	 * the description in rfc7748: "k[0] &= 248, k[31] &= 127, k[31] |= 64",
	 * then convert it to big endian. Only in this way, the result can be
	 * the same as the software curve-25519 that exists in crypto.
	 */
	memcpy(secret, buf, len);
	curve25519_clamp_secret(secret);
	hpre_key_to_big_end(secret, CURVE25519_KEY_SIZE);

	p = ctx->curve25519.p + sz - len;

	curve = ecc_get_curve25519();

	/* fill curve parameters */
	fill_curve_param(p, curve->p, len, curve->g.ndigits);
	fill_curve_param(p + sz, curve->a, len, curve->g.ndigits);
	memcpy(p + shift, secret, len);
	fill_curve_param(p + shift + sz, curve->g.x, len, curve->g.ndigits);
	memzero_explicit(secret, CURVE25519_KEY_SIZE);
}

static int hpre_curve25519_set_param(struct hpre_ctx *ctx, const void *buf,
				     unsigned int len)
{
	struct device *dev = ctx->dev;
	unsigned int sz = ctx->key_sz;
	unsigned int shift = sz << 1;

	/* p->a->k->gx */
	if (!ctx->curve25519.p) {
		ctx->curve25519.p = dma_alloc_coherent(dev, sz << 2,
						       &ctx->curve25519.dma_p,
						       GFP_KERNEL);
		if (!ctx->curve25519.p)
			return -ENOMEM;
	}

	ctx->curve25519.g = ctx->curve25519.p + shift + sz;
	ctx->curve25519.dma_g = ctx->curve25519.dma_p + shift + sz;

	hpre_curve25519_fill_curve(ctx, buf, len);

	return 0;
}

static int hpre_curve25519_set_secret(struct crypto_kpp *tfm, const void *buf,
				      unsigned int len)
{
	struct hpre_ctx *ctx = kpp_tfm_ctx(tfm);
	struct device *dev = ctx->dev;
	int ret = -EINVAL;

	if (len != CURVE25519_KEY_SIZE ||
	    !crypto_memneq(buf, curve25519_null_point, CURVE25519_KEY_SIZE)) {
		dev_err(dev, "key is null or key len is not 32bytes!\n");
		return ret;
	}

	/* Free old secret if any */
	hpre_ecc_clear_ctx(ctx, false, false);

	ctx->key_sz = CURVE25519_KEY_SIZE;
	ret = hpre_curve25519_set_param(ctx, buf, CURVE25519_KEY_SIZE);
	if (ret) {
		dev_err(dev, "failed to set curve25519 param, ret = %d!\n", ret);
		hpre_ecc_clear_ctx(ctx, false, false);
		return ret;
	}

	return 0;
}

static void hpre_curve25519_hw_data_clr_all(struct hpre_ctx *ctx,
					    struct hpre_asym_request *req,
					    struct scatterlist *dst,
					    struct scatterlist *src)
{
	struct device *dev = ctx->dev;
	struct hpre_sqe *sqe = &req->req;
	dma_addr_t dma;

	dma = le64_to_cpu(sqe->in);
	if (unlikely(dma_mapping_error(dev, dma)))
		return;

	if (src && req->src)
		dma_free_coherent(dev, ctx->key_sz, req->src, dma);

	dma = le64_to_cpu(sqe->out);
	if (unlikely(dma_mapping_error(dev, dma)))
		return;

	if (req->dst)
		dma_free_coherent(dev, ctx->key_sz, req->dst, dma);
	if (dst)
		dma_unmap_single(dev, dma, ctx->key_sz, DMA_FROM_DEVICE);
}

static void hpre_curve25519_cb(struct hpre_ctx *ctx, void *resp)
{
	struct hpre_dfx *dfx = ctx->hpre->debug.dfx;
	struct hpre_asym_request *req = NULL;
	struct kpp_request *areq;
	u64 overtime_thrhld;
	int ret;

	ret = hpre_alg_res_post_hf(ctx, resp, (void **)&req);
	areq = req->areq.curve25519;
	areq->dst_len = ctx->key_sz;

	overtime_thrhld = atomic64_read(&dfx[HPRE_OVERTIME_THRHLD].value);
	if (overtime_thrhld && hpre_is_bd_timeout(req, overtime_thrhld))
		atomic64_inc(&dfx[HPRE_OVER_THRHLD_CNT].value);

	hpre_key_to_big_end(sg_virt(areq->dst), CURVE25519_KEY_SIZE);

	hpre_curve25519_hw_data_clr_all(ctx, req, areq->dst, areq->src);
	kpp_request_complete(areq, ret);

	atomic64_inc(&dfx[HPRE_RECV_CNT].value);
}

static int hpre_curve25519_msg_request_set(struct hpre_ctx *ctx,
					   struct kpp_request *req)
{
	struct hpre_asym_request *h_req;
	struct hpre_sqe *msg;
	int req_id;
	void *tmp;

	if (unlikely(req->dst_len < ctx->key_sz)) {
		req->dst_len = ctx->key_sz;
		return -EINVAL;
	}

	tmp = kpp_request_ctx(req);
	h_req = PTR_ALIGN(tmp, HPRE_ALIGN_SZ);
	h_req->cb = hpre_curve25519_cb;
	h_req->areq.curve25519 = req;
	msg = &h_req->req;
	memset(msg, 0, sizeof(*msg));
	msg->in = cpu_to_le64(DMA_MAPPING_ERROR);
	msg->out = cpu_to_le64(DMA_MAPPING_ERROR);
	msg->key = cpu_to_le64(ctx->curve25519.dma_p);

	msg->dw0 |= cpu_to_le32(0x1U << HPRE_SQE_DONE_SHIFT);
	msg->task_len1 = (ctx->key_sz >> HPRE_BITS_2_BYTES_SHIFT) - 1;
	h_req->ctx = ctx;

	req_id = hpre_add_req_to_ctx(h_req);
	if (req_id < 0)
		return -EBUSY;

	msg->tag = cpu_to_le16((u16)req_id);
	return 0;
}

static void hpre_curve25519_src_modulo_p(u8 *ptr)
{
	int i;

	for (i = 0; i < CURVE25519_KEY_SIZE - 1; i++)
		ptr[i] = 0;

	/* The modulus is ptr's last byte minus '0xed'(last byte of p) */
	ptr[i] -= 0xed;
}

static int hpre_curve25519_src_init(struct hpre_asym_request *hpre_req,
				    struct scatterlist *data, unsigned int len)
{
	struct hpre_sqe *msg = &hpre_req->req;
	struct hpre_ctx *ctx = hpre_req->ctx;
	struct device *dev = ctx->dev;
	u8 p[CURVE25519_KEY_SIZE] = { 0 };
	const struct ecc_curve *curve;
	dma_addr_t dma = 0;
	u8 *ptr;

	if (len != CURVE25519_KEY_SIZE) {
		dev_err(dev, "sourc_data len is not 32bytes, len = %u!\n", len);
		return -EINVAL;
	}

	ptr = dma_alloc_coherent(dev, ctx->key_sz, &dma, GFP_KERNEL);
	if (unlikely(!ptr))
		return -ENOMEM;

	scatterwalk_map_and_copy(ptr, data, 0, len, 0);

	if (!crypto_memneq(ptr, curve25519_null_point, CURVE25519_KEY_SIZE)) {
		dev_err(dev, "gx is null!\n");
		goto err;
	}

	/*
	 * Src_data(gx) is in little-endian order, MSB in the final byte should
	 * be masked as described in RFC7748, then transform it to big-endian
	 * form, then hisi_hpre can use the data.
	 */
	ptr[31] &= 0x7f;
	hpre_key_to_big_end(ptr, CURVE25519_KEY_SIZE);

	curve = ecc_get_curve25519();

	fill_curve_param(p, curve->p, CURVE25519_KEY_SIZE, curve->g.ndigits);

	/*
	 * When src_data equals (2^255 - 19) ~  (2^255 - 1), it is out of p,
	 * we get its modulus to p, and then use it.
	 */
	if (memcmp(ptr, p, ctx->key_sz) == 0) {
		dev_err(dev, "gx is p!\n");
		goto err;
	} else if (memcmp(ptr, p, ctx->key_sz) > 0) {
		hpre_curve25519_src_modulo_p(ptr);
	}

	hpre_req->src = ptr;
	msg->in = cpu_to_le64(dma);
	return 0;

err:
	dma_free_coherent(dev, ctx->key_sz, ptr, dma);
	return -EINVAL;
}

static int hpre_curve25519_dst_init(struct hpre_asym_request *hpre_req,
				    struct scatterlist *data, unsigned int len)
{
	struct hpre_sqe *msg = &hpre_req->req;
	struct hpre_ctx *ctx = hpre_req->ctx;
	struct device *dev = ctx->dev;
	dma_addr_t dma;

	if (!data || !sg_is_last(data) || len != ctx->key_sz) {
		dev_err(dev, "data or data length is illegal!\n");
		return -EINVAL;
	}

	hpre_req->dst = NULL;
	dma = dma_map_single(dev, sg_virt(data), len, DMA_FROM_DEVICE);
	if (unlikely(dma_mapping_error(dev, dma))) {
		dev_err(dev, "dma map data err!\n");
		return -ENOMEM;
	}

	msg->out = cpu_to_le64(dma);
	return 0;
}

static int hpre_curve25519_compute_value(struct kpp_request *req)
{
	struct crypto_kpp *tfm = crypto_kpp_reqtfm(req);
	struct hpre_ctx *ctx = kpp_tfm_ctx(tfm);
	struct device *dev = ctx->dev;
	void *tmp = kpp_request_ctx(req);
	struct hpre_asym_request *hpre_req = PTR_ALIGN(tmp, HPRE_ALIGN_SZ);
	struct hpre_sqe *msg = &hpre_req->req;
	int ret;

	ret = hpre_curve25519_msg_request_set(ctx, req);
	if (unlikely(ret)) {
		dev_err(dev, "failed to set curve25519 request, ret = %d!\n", ret);
		return ret;
	}

	if (req->src) {
		ret = hpre_curve25519_src_init(hpre_req, req->src, req->src_len);
		if (unlikely(ret)) {
			dev_err(dev, "failed to init src data, ret = %d!\n",
				ret);
			goto clear_all;
		}
	} else {
		msg->in = cpu_to_le64(ctx->curve25519.dma_g);
	}

	ret = hpre_curve25519_dst_init(hpre_req, req->dst, req->dst_len);
	if (unlikely(ret)) {
		dev_err(dev, "failed to init dst data, ret = %d!\n", ret);
		goto clear_all;
	}

	msg->dw0 = cpu_to_le32(le32_to_cpu(msg->dw0) | HPRE_ALG_CURVE25519_MUL);
	ret = hpre_send(ctx, msg);
	if (likely(!ret))
		return -EINPROGRESS;

clear_all:
	hpre_rm_req_from_ctx(hpre_req);
	hpre_curve25519_hw_data_clr_all(ctx, hpre_req, req->dst, req->src);
	return ret;
}

static unsigned int hpre_curve25519_max_size(struct crypto_kpp *tfm)
{
	struct hpre_ctx *ctx = kpp_tfm_ctx(tfm);

	return ctx->key_sz;
}

static int hpre_curve25519_init_tfm(struct crypto_kpp *tfm)
{
	struct hpre_ctx *ctx = kpp_tfm_ctx(tfm);

	return hpre_ctx_init(ctx, HPRE_V3_ECC_ALG_TYPE);
}

static void hpre_curve25519_exit_tfm(struct crypto_kpp *tfm)
{
	struct hpre_ctx *ctx = kpp_tfm_ctx(tfm);

	hpre_ecc_clear_ctx(ctx, true, false);
}

static struct akcipher_alg rsa = {
	.sign = hpre_rsa_dec,
	.verify = hpre_rsa_enc,
	.encrypt = hpre_rsa_enc,
	.decrypt = hpre_rsa_dec,
	.set_pub_key = hpre_rsa_setpubkey,
	.set_priv_key = hpre_rsa_setprivkey,
	.max_size = hpre_rsa_max_size,
	.init = hpre_rsa_init_tfm,
	.exit = hpre_rsa_exit_tfm,
	.reqsize = sizeof(struct hpre_asym_request) + HPRE_ALIGN_SZ,
	.base = {
		.cra_ctxsize = sizeof(struct hpre_ctx),
		.cra_priority = HPRE_CRYPTO_ALG_PRI,
		.cra_name = "rsa",
		.cra_driver_name = "hpre-rsa",
		.cra_module = THIS_MODULE,
	},
};

static struct kpp_alg dh = {
	.set_secret = hpre_dh_set_secret,
	.generate_public_key = hpre_dh_compute_value,
	.compute_shared_secret = hpre_dh_compute_value,
	.max_size = hpre_dh_max_size,
	.init = hpre_dh_init_tfm,
	.exit = hpre_dh_exit_tfm,
	.reqsize = sizeof(struct hpre_asym_request) + HPRE_ALIGN_SZ,
	.base = {
		.cra_ctxsize = sizeof(struct hpre_ctx),
		.cra_priority = HPRE_CRYPTO_ALG_PRI,
		.cra_name = "dh",
		.cra_driver_name = "hpre-dh",
		.cra_module = THIS_MODULE,
	},
};

static struct kpp_alg ecdh_nist_p192 = {
	.set_secret = hpre_ecdh_set_secret,
	.generate_public_key = hpre_ecdh_compute_value,
	.compute_shared_secret = hpre_ecdh_compute_value,
	.max_size = hpre_ecdh_max_size,
	.init = hpre_ecdh_nist_p192_init_tfm,
	.exit = hpre_ecdh_exit_tfm,
	.reqsize = sizeof(struct hpre_asym_request) + HPRE_ALIGN_SZ,
	.base = {
		.cra_ctxsize = sizeof(struct hpre_ctx),
		.cra_priority = HPRE_CRYPTO_ALG_PRI,
		.cra_name = "ecdh-nist-p192",
		.cra_driver_name = "hpre-ecdh-nist-p192",
		.cra_module = THIS_MODULE,
	},
};

static struct kpp_alg ecdh_nist_p256 = {
	.set_secret = hpre_ecdh_set_secret,
	.generate_public_key = hpre_ecdh_compute_value,
	.compute_shared_secret = hpre_ecdh_compute_value,
	.max_size = hpre_ecdh_max_size,
	.init = hpre_ecdh_nist_p256_init_tfm,
	.exit = hpre_ecdh_exit_tfm,
	.reqsize = sizeof(struct hpre_asym_request) + HPRE_ALIGN_SZ,
	.base = {
		.cra_ctxsize = sizeof(struct hpre_ctx),
		.cra_priority = HPRE_CRYPTO_ALG_PRI,
		.cra_name = "ecdh-nist-p256",
		.cra_driver_name = "hpre-ecdh-nist-p256",
		.cra_module = THIS_MODULE,
	},
};

static struct kpp_alg ecdh_nist_p384 = {
	.set_secret = hpre_ecdh_set_secret,
	.generate_public_key = hpre_ecdh_compute_value,
	.compute_shared_secret = hpre_ecdh_compute_value,
	.max_size = hpre_ecdh_max_size,
	.init = hpre_ecdh_nist_p384_init_tfm,
	.exit = hpre_ecdh_exit_tfm,
	.reqsize = sizeof(struct hpre_asym_request) + HPRE_ALIGN_SZ,
	.base = {
		.cra_ctxsize = sizeof(struct hpre_ctx),
		.cra_priority = HPRE_CRYPTO_ALG_PRI,
		.cra_name = "ecdh-nist-p384",
		.cra_driver_name = "hpre-ecdh-nist-p384",
		.cra_module = THIS_MODULE,
	},
};

static struct kpp_alg curve25519_alg = {
	.set_secret = hpre_curve25519_set_secret,
	.generate_public_key = hpre_curve25519_compute_value,
	.compute_shared_secret = hpre_curve25519_compute_value,
	.max_size = hpre_curve25519_max_size,
	.init = hpre_curve25519_init_tfm,
	.exit = hpre_curve25519_exit_tfm,
	.reqsize = sizeof(struct hpre_asym_request) + HPRE_ALIGN_SZ,
	.base = {
		.cra_ctxsize = sizeof(struct hpre_ctx),
		.cra_priority = HPRE_CRYPTO_ALG_PRI,
		.cra_name = "curve25519",
		.cra_driver_name = "hpre-curve25519",
		.cra_module = THIS_MODULE,
	},
};


static int hpre_register_ecdh(void)
{
	int ret;

	ret = crypto_register_kpp(&ecdh_nist_p192);
	if (ret)
		return ret;

	ret = crypto_register_kpp(&ecdh_nist_p256);
	if (ret)
		goto unregister_ecdh_p192;

	ret = crypto_register_kpp(&ecdh_nist_p384);
	if (ret)
		goto unregister_ecdh_p256;

	return 0;

unregister_ecdh_p256:
	crypto_unregister_kpp(&ecdh_nist_p256);
unregister_ecdh_p192:
	crypto_unregister_kpp(&ecdh_nist_p192);
	return ret;
}

static void hpre_unregister_ecdh(void)
{
	crypto_unregister_kpp(&ecdh_nist_p384);
	crypto_unregister_kpp(&ecdh_nist_p256);
	crypto_unregister_kpp(&ecdh_nist_p192);
}

int hpre_algs_register(struct hisi_qm *qm)
{
	int ret;

	rsa.base.cra_flags = 0;
	ret = crypto_register_akcipher(&rsa);
	if (ret)
		return ret;

	ret = crypto_register_kpp(&dh);
	if (ret)
		goto unreg_rsa;

	if (qm->ver >= QM_HW_V3) {
		ret = hpre_register_ecdh();
		if (ret)
			goto unreg_dh;
		ret = crypto_register_kpp(&curve25519_alg);
		if (ret)
			goto unreg_ecdh;
	}
	return 0;

unreg_ecdh:
	hpre_unregister_ecdh();
unreg_dh:
	crypto_unregister_kpp(&dh);
unreg_rsa:
	crypto_unregister_akcipher(&rsa);
	return ret;
}

void hpre_algs_unregister(struct hisi_qm *qm)
{
	if (qm->ver >= QM_HW_V3) {
		crypto_unregister_kpp(&curve25519_alg);
		hpre_unregister_ecdh();
	}

	crypto_unregister_kpp(&dh);
	crypto_unregister_akcipher(&rsa);
}
