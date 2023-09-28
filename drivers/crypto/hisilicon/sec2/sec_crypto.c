// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 HiSilicon Limited. */

#include <crypto/aes.h>
#include <crypto/aead.h>
#include <crypto/algapi.h>
#include <crypto/authenc.h>
#include <crypto/des.h>
#include <crypto/hash.h>
#include <crypto/internal/aead.h>
#include <crypto/internal/des.h>
#include <crypto/sha1.h>
#include <crypto/sha2.h>
#include <crypto/skcipher.h>
#include <crypto/xts.h>
#include <linux/crypto.h>
#include <linux/dma-mapping.h>
#include <linux/idr.h>

#include "sec.h"
#include "sec_crypto.h"

#define SEC_PRIORITY		4001
#define SEC_XTS_MIN_KEY_SIZE	(2 * AES_MIN_KEY_SIZE)
#define SEC_XTS_MID_KEY_SIZE	(3 * AES_MIN_KEY_SIZE)
#define SEC_XTS_MAX_KEY_SIZE	(2 * AES_MAX_KEY_SIZE)
#define SEC_DES3_2KEY_SIZE	(2 * DES_KEY_SIZE)
#define SEC_DES3_3KEY_SIZE	(3 * DES_KEY_SIZE)

/* SEC sqe(bd) bit operational relative MACRO */
#define SEC_DE_OFFSET		1
#define SEC_CIPHER_OFFSET	4
#define SEC_SCENE_OFFSET	3
#define SEC_DST_SGL_OFFSET	2
#define SEC_SRC_SGL_OFFSET	7
#define SEC_CKEY_OFFSET		9
#define SEC_CMODE_OFFSET	12
#define SEC_AKEY_OFFSET         5
#define SEC_AEAD_ALG_OFFSET     11
#define SEC_AUTH_OFFSET		6

#define SEC_DE_OFFSET_V3		9
#define SEC_SCENE_OFFSET_V3	5
#define SEC_CKEY_OFFSET_V3	13
#define SEC_CTR_CNT_OFFSET	25
#define SEC_CTR_CNT_ROLLOVER	2
#define SEC_SRC_SGL_OFFSET_V3	11
#define SEC_DST_SGL_OFFSET_V3	14
#define SEC_CALG_OFFSET_V3	4
#define SEC_AKEY_OFFSET_V3	9
#define SEC_MAC_OFFSET_V3	4
#define SEC_AUTH_ALG_OFFSET_V3	15
#define SEC_CIPHER_AUTH_V3	0xbf
#define SEC_AUTH_CIPHER_V3	0x40
#define SEC_FLAG_OFFSET		7
#define SEC_FLAG_MASK		0x0780
#define SEC_TYPE_MASK		0x0F
#define SEC_DONE_MASK		0x0001
#define SEC_ICV_MASK		0x000E
#define SEC_SQE_LEN_RATE_MASK	0x3

#define SEC_TOTAL_IV_SZ(depth)	(SEC_IV_SIZE * (depth))
#define SEC_SGL_SGE_NR		128
#define SEC_CIPHER_AUTH		0xfe
#define SEC_AUTH_CIPHER		0x1
#define SEC_MAX_MAC_LEN		64
#define SEC_MAX_AAD_LEN		65535
#define SEC_MAX_CCM_AAD_LEN	65279
#define SEC_TOTAL_MAC_SZ(depth) (SEC_MAX_MAC_LEN * (depth))

#define SEC_PBUF_SZ			512
#define SEC_PBUF_IV_OFFSET		SEC_PBUF_SZ
#define SEC_PBUF_MAC_OFFSET		(SEC_PBUF_SZ + SEC_IV_SIZE)
#define SEC_PBUF_PKG		(SEC_PBUF_SZ + SEC_IV_SIZE +	\
			SEC_MAX_MAC_LEN * 2)
#define SEC_PBUF_NUM		(PAGE_SIZE / SEC_PBUF_PKG)
#define SEC_PBUF_PAGE_NUM(depth)	((depth) / SEC_PBUF_NUM)
#define SEC_PBUF_LEFT_SZ(depth)		(SEC_PBUF_PKG * ((depth) -	\
				SEC_PBUF_PAGE_NUM(depth) * SEC_PBUF_NUM))
#define SEC_TOTAL_PBUF_SZ(depth)	(PAGE_SIZE * SEC_PBUF_PAGE_NUM(depth) +	\
				SEC_PBUF_LEFT_SZ(depth))

#define SEC_SQE_LEN_RATE	4
#define SEC_SQE_CFLAG		2
#define SEC_SQE_AEAD_FLAG	3
#define SEC_SQE_DONE		0x1
#define SEC_ICV_ERR		0x2
#define MIN_MAC_LEN		4
#define MAC_LEN_MASK		0x1U
#define MAX_INPUT_DATA_LEN	0xFFFE00
#define BITS_MASK		0xFF
#define BYTE_BITS		0x8
#define SEC_XTS_NAME_SZ		0x3
#define IV_CM_CAL_NUM		2
#define IV_CL_MASK		0x7
#define IV_CL_MIN		2
#define IV_CL_MID		4
#define IV_CL_MAX		8
#define IV_FLAGS_OFFSET	0x6
#define IV_CM_OFFSET		0x3
#define IV_LAST_BYTE1		1
#define IV_LAST_BYTE2		2
#define IV_LAST_BYTE_MASK	0xFF
#define IV_CTR_INIT		0x1
#define IV_BYTE_OFFSET		0x8

static DEFINE_MUTEX(sec_algs_lock);
static unsigned int sec_available_devs;

struct sec_skcipher {
	u64 alg_msk;
	struct skcipher_alg alg;
};

struct sec_aead {
	u64 alg_msk;
	struct aead_alg alg;
};

/* Get an en/de-cipher queue cyclically to balance load over queues of TFM */
static inline int sec_alloc_queue_id(struct sec_ctx *ctx, struct sec_req *req)
{
	if (req->c_req.encrypt)
		return (u32)atomic_inc_return(&ctx->enc_qcyclic) %
				 ctx->hlf_q_num;

	return (u32)atomic_inc_return(&ctx->dec_qcyclic) % ctx->hlf_q_num +
				 ctx->hlf_q_num;
}

static inline void sec_free_queue_id(struct sec_ctx *ctx, struct sec_req *req)
{
	if (req->c_req.encrypt)
		atomic_dec(&ctx->enc_qcyclic);
	else
		atomic_dec(&ctx->dec_qcyclic);
}

static int sec_alloc_req_id(struct sec_req *req, struct sec_qp_ctx *qp_ctx)
{
	int req_id;

	spin_lock_bh(&qp_ctx->req_lock);
	req_id = idr_alloc_cyclic(&qp_ctx->req_idr, NULL, 0, qp_ctx->qp->sq_depth, GFP_ATOMIC);
	spin_unlock_bh(&qp_ctx->req_lock);
	if (unlikely(req_id < 0)) {
		dev_err(req->ctx->dev, "alloc req id fail!\n");
		return req_id;
	}

	req->qp_ctx = qp_ctx;
	qp_ctx->req_list[req_id] = req;

	return req_id;
}

static void sec_free_req_id(struct sec_req *req)
{
	struct sec_qp_ctx *qp_ctx = req->qp_ctx;
	int req_id = req->req_id;

	if (unlikely(req_id < 0 || req_id >= qp_ctx->qp->sq_depth)) {
		dev_err(req->ctx->dev, "free request id invalid!\n");
		return;
	}

	qp_ctx->req_list[req_id] = NULL;
	req->qp_ctx = NULL;

	spin_lock_bh(&qp_ctx->req_lock);
	idr_remove(&qp_ctx->req_idr, req_id);
	spin_unlock_bh(&qp_ctx->req_lock);
}

static u8 pre_parse_finished_bd(struct bd_status *status, void *resp)
{
	struct sec_sqe *bd = resp;

	status->done = le16_to_cpu(bd->type2.done_flag) & SEC_DONE_MASK;
	status->icv = (le16_to_cpu(bd->type2.done_flag) & SEC_ICV_MASK) >> 1;
	status->flag = (le16_to_cpu(bd->type2.done_flag) &
					SEC_FLAG_MASK) >> SEC_FLAG_OFFSET;
	status->tag = le16_to_cpu(bd->type2.tag);
	status->err_type = bd->type2.error_type;

	return bd->type_cipher_auth & SEC_TYPE_MASK;
}

static u8 pre_parse_finished_bd3(struct bd_status *status, void *resp)
{
	struct sec_sqe3 *bd3 = resp;

	status->done = le16_to_cpu(bd3->done_flag) & SEC_DONE_MASK;
	status->icv = (le16_to_cpu(bd3->done_flag) & SEC_ICV_MASK) >> 1;
	status->flag = (le16_to_cpu(bd3->done_flag) &
					SEC_FLAG_MASK) >> SEC_FLAG_OFFSET;
	status->tag = le64_to_cpu(bd3->tag);
	status->err_type = bd3->error_type;

	return le32_to_cpu(bd3->bd_param) & SEC_TYPE_MASK;
}

static int sec_cb_status_check(struct sec_req *req,
			       struct bd_status *status)
{
	struct sec_ctx *ctx = req->ctx;

	if (unlikely(req->err_type || status->done != SEC_SQE_DONE)) {
		dev_err_ratelimited(ctx->dev, "err_type[%d], done[%u]\n",
				    req->err_type, status->done);
		return -EIO;
	}

	if (unlikely(ctx->alg_type == SEC_SKCIPHER)) {
		if (unlikely(status->flag != SEC_SQE_CFLAG)) {
			dev_err_ratelimited(ctx->dev, "flag[%u]\n",
					    status->flag);
			return -EIO;
		}
	} else if (unlikely(ctx->alg_type == SEC_AEAD)) {
		if (unlikely(status->flag != SEC_SQE_AEAD_FLAG ||
			     status->icv == SEC_ICV_ERR)) {
			dev_err_ratelimited(ctx->dev,
					    "flag[%u], icv[%u]\n",
					    status->flag, status->icv);
			return -EBADMSG;
		}
	}

	return 0;
}

static void sec_req_cb(struct hisi_qp *qp, void *resp)
{
	struct sec_qp_ctx *qp_ctx = qp->qp_ctx;
	struct sec_dfx *dfx = &qp_ctx->ctx->sec->debug.dfx;
	u8 type_supported = qp_ctx->ctx->type_supported;
	struct bd_status status;
	struct sec_ctx *ctx;
	struct sec_req *req;
	int err;
	u8 type;

	if (type_supported == SEC_BD_TYPE2) {
		type = pre_parse_finished_bd(&status, resp);
		req = qp_ctx->req_list[status.tag];
	} else {
		type = pre_parse_finished_bd3(&status, resp);
		req = (void *)(uintptr_t)status.tag;
	}

	if (unlikely(type != type_supported)) {
		atomic64_inc(&dfx->err_bd_cnt);
		pr_err("err bd type [%u]\n", type);
		return;
	}

	if (unlikely(!req)) {
		atomic64_inc(&dfx->invalid_req_cnt);
		atomic_inc(&qp->qp_status.used);
		return;
	}

	req->err_type = status.err_type;
	ctx = req->ctx;
	err = sec_cb_status_check(req, &status);
	if (err)
		atomic64_inc(&dfx->done_flag_cnt);

	atomic64_inc(&dfx->recv_cnt);

	ctx->req_op->buf_unmap(ctx, req);

	ctx->req_op->callback(ctx, req, err);
}

static int sec_bd_send(struct sec_ctx *ctx, struct sec_req *req)
{
	struct sec_qp_ctx *qp_ctx = req->qp_ctx;
	int ret;

	if (ctx->fake_req_limit <=
	    atomic_read(&qp_ctx->qp->qp_status.used) &&
	    !(req->flag & CRYPTO_TFM_REQ_MAY_BACKLOG))
		return -EBUSY;

	spin_lock_bh(&qp_ctx->req_lock);
	ret = hisi_qp_send(qp_ctx->qp, &req->sec_sqe);
	if (ctx->fake_req_limit <=
	    atomic_read(&qp_ctx->qp->qp_status.used) && !ret) {
		list_add_tail(&req->backlog_head, &qp_ctx->backlog);
		atomic64_inc(&ctx->sec->debug.dfx.send_cnt);
		atomic64_inc(&ctx->sec->debug.dfx.send_busy_cnt);
		spin_unlock_bh(&qp_ctx->req_lock);
		return -EBUSY;
	}
	spin_unlock_bh(&qp_ctx->req_lock);

	if (unlikely(ret == -EBUSY))
		return -ENOBUFS;

	if (likely(!ret)) {
		ret = -EINPROGRESS;
		atomic64_inc(&ctx->sec->debug.dfx.send_cnt);
	}

	return ret;
}

/* Get DMA memory resources */
static int sec_alloc_civ_resource(struct device *dev, struct sec_alg_res *res)
{
	u16 q_depth = res->depth;
	int i;

	res->c_ivin = dma_alloc_coherent(dev, SEC_TOTAL_IV_SZ(q_depth),
					 &res->c_ivin_dma, GFP_KERNEL);
	if (!res->c_ivin)
		return -ENOMEM;

	for (i = 1; i < q_depth; i++) {
		res[i].c_ivin_dma = res->c_ivin_dma + i * SEC_IV_SIZE;
		res[i].c_ivin = res->c_ivin + i * SEC_IV_SIZE;
	}

	return 0;
}

static void sec_free_civ_resource(struct device *dev, struct sec_alg_res *res)
{
	if (res->c_ivin)
		dma_free_coherent(dev, SEC_TOTAL_IV_SZ(res->depth),
				  res->c_ivin, res->c_ivin_dma);
}

static int sec_alloc_aiv_resource(struct device *dev, struct sec_alg_res *res)
{
	u16 q_depth = res->depth;
	int i;

	res->a_ivin = dma_alloc_coherent(dev, SEC_TOTAL_IV_SZ(q_depth),
					 &res->a_ivin_dma, GFP_KERNEL);
	if (!res->a_ivin)
		return -ENOMEM;

	for (i = 1; i < q_depth; i++) {
		res[i].a_ivin_dma = res->a_ivin_dma + i * SEC_IV_SIZE;
		res[i].a_ivin = res->a_ivin + i * SEC_IV_SIZE;
	}

	return 0;
}

static void sec_free_aiv_resource(struct device *dev, struct sec_alg_res *res)
{
	if (res->a_ivin)
		dma_free_coherent(dev, SEC_TOTAL_IV_SZ(res->depth),
				  res->a_ivin, res->a_ivin_dma);
}

static int sec_alloc_mac_resource(struct device *dev, struct sec_alg_res *res)
{
	u16 q_depth = res->depth;
	int i;

	res->out_mac = dma_alloc_coherent(dev, SEC_TOTAL_MAC_SZ(q_depth) << 1,
					  &res->out_mac_dma, GFP_KERNEL);
	if (!res->out_mac)
		return -ENOMEM;

	for (i = 1; i < q_depth; i++) {
		res[i].out_mac_dma = res->out_mac_dma +
				     i * (SEC_MAX_MAC_LEN << 1);
		res[i].out_mac = res->out_mac + i * (SEC_MAX_MAC_LEN << 1);
	}

	return 0;
}

static void sec_free_mac_resource(struct device *dev, struct sec_alg_res *res)
{
	if (res->out_mac)
		dma_free_coherent(dev, SEC_TOTAL_MAC_SZ(res->depth) << 1,
				  res->out_mac, res->out_mac_dma);
}

static void sec_free_pbuf_resource(struct device *dev, struct sec_alg_res *res)
{
	if (res->pbuf)
		dma_free_coherent(dev, SEC_TOTAL_PBUF_SZ(res->depth),
				  res->pbuf, res->pbuf_dma);
}

/*
 * To improve performance, pbuffer is used for
 * small packets (< 512Bytes) as IOMMU translation using.
 */
static int sec_alloc_pbuf_resource(struct device *dev, struct sec_alg_res *res)
{
	u16 q_depth = res->depth;
	int size = SEC_PBUF_PAGE_NUM(q_depth);
	int pbuf_page_offset;
	int i, j, k;

	res->pbuf = dma_alloc_coherent(dev, SEC_TOTAL_PBUF_SZ(q_depth),
				&res->pbuf_dma, GFP_KERNEL);
	if (!res->pbuf)
		return -ENOMEM;

	/*
	 * SEC_PBUF_PKG contains data pbuf, iv and
	 * out_mac : <SEC_PBUF|SEC_IV|SEC_MAC>
	 * Every PAGE contains six SEC_PBUF_PKG
	 * The sec_qp_ctx contains QM_Q_DEPTH numbers of SEC_PBUF_PKG
	 * So we need SEC_PBUF_PAGE_NUM numbers of PAGE
	 * for the SEC_TOTAL_PBUF_SZ
	 */
	for (i = 0; i <= size; i++) {
		pbuf_page_offset = PAGE_SIZE * i;
		for (j = 0; j < SEC_PBUF_NUM; j++) {
			k = i * SEC_PBUF_NUM + j;
			if (k == q_depth)
				break;
			res[k].pbuf = res->pbuf +
				j * SEC_PBUF_PKG + pbuf_page_offset;
			res[k].pbuf_dma = res->pbuf_dma +
				j * SEC_PBUF_PKG + pbuf_page_offset;
		}
	}

	return 0;
}

static int sec_alg_resource_alloc(struct sec_ctx *ctx,
				  struct sec_qp_ctx *qp_ctx)
{
	struct sec_alg_res *res = qp_ctx->res;
	struct device *dev = ctx->dev;
	int ret;

	ret = sec_alloc_civ_resource(dev, res);
	if (ret)
		return ret;

	if (ctx->alg_type == SEC_AEAD) {
		ret = sec_alloc_aiv_resource(dev, res);
		if (ret)
			goto alloc_aiv_fail;

		ret = sec_alloc_mac_resource(dev, res);
		if (ret)
			goto alloc_mac_fail;
	}
	if (ctx->pbuf_supported) {
		ret = sec_alloc_pbuf_resource(dev, res);
		if (ret) {
			dev_err(dev, "fail to alloc pbuf dma resource!\n");
			goto alloc_pbuf_fail;
		}
	}

	return 0;

alloc_pbuf_fail:
	if (ctx->alg_type == SEC_AEAD)
		sec_free_mac_resource(dev, qp_ctx->res);
alloc_mac_fail:
	if (ctx->alg_type == SEC_AEAD)
		sec_free_aiv_resource(dev, res);
alloc_aiv_fail:
	sec_free_civ_resource(dev, res);
	return ret;
}

static void sec_alg_resource_free(struct sec_ctx *ctx,
				  struct sec_qp_ctx *qp_ctx)
{
	struct device *dev = ctx->dev;

	sec_free_civ_resource(dev, qp_ctx->res);

	if (ctx->pbuf_supported)
		sec_free_pbuf_resource(dev, qp_ctx->res);
	if (ctx->alg_type == SEC_AEAD)
		sec_free_mac_resource(dev, qp_ctx->res);
}

static int sec_alloc_qp_ctx_resource(struct hisi_qm *qm, struct sec_ctx *ctx,
				     struct sec_qp_ctx *qp_ctx)
{
	u16 q_depth = qp_ctx->qp->sq_depth;
	struct device *dev = ctx->dev;
	int ret = -ENOMEM;

	qp_ctx->req_list = kcalloc(q_depth, sizeof(struct sec_req *), GFP_KERNEL);
	if (!qp_ctx->req_list)
		return ret;

	qp_ctx->res = kcalloc(q_depth, sizeof(struct sec_alg_res), GFP_KERNEL);
	if (!qp_ctx->res)
		goto err_free_req_list;
	qp_ctx->res->depth = q_depth;

	qp_ctx->c_in_pool = hisi_acc_create_sgl_pool(dev, q_depth, SEC_SGL_SGE_NR);
	if (IS_ERR(qp_ctx->c_in_pool)) {
		dev_err(dev, "fail to create sgl pool for input!\n");
		goto err_free_res;
	}

	qp_ctx->c_out_pool = hisi_acc_create_sgl_pool(dev, q_depth, SEC_SGL_SGE_NR);
	if (IS_ERR(qp_ctx->c_out_pool)) {
		dev_err(dev, "fail to create sgl pool for output!\n");
		goto err_free_c_in_pool;
	}

	ret = sec_alg_resource_alloc(ctx, qp_ctx);
	if (ret)
		goto err_free_c_out_pool;

	return 0;

err_free_c_out_pool:
	hisi_acc_free_sgl_pool(dev, qp_ctx->c_out_pool);
err_free_c_in_pool:
	hisi_acc_free_sgl_pool(dev, qp_ctx->c_in_pool);
err_free_res:
	kfree(qp_ctx->res);
err_free_req_list:
	kfree(qp_ctx->req_list);
	return ret;
}

static void sec_free_qp_ctx_resource(struct sec_ctx *ctx, struct sec_qp_ctx *qp_ctx)
{
	struct device *dev = ctx->dev;

	sec_alg_resource_free(ctx, qp_ctx);
	hisi_acc_free_sgl_pool(dev, qp_ctx->c_out_pool);
	hisi_acc_free_sgl_pool(dev, qp_ctx->c_in_pool);
	kfree(qp_ctx->res);
	kfree(qp_ctx->req_list);
}

static int sec_create_qp_ctx(struct hisi_qm *qm, struct sec_ctx *ctx,
			     int qp_ctx_id, int alg_type)
{
	struct sec_qp_ctx *qp_ctx;
	struct hisi_qp *qp;
	int ret;

	qp_ctx = &ctx->qp_ctx[qp_ctx_id];
	qp = ctx->qps[qp_ctx_id];
	qp->req_type = 0;
	qp->qp_ctx = qp_ctx;
	qp_ctx->qp = qp;
	qp_ctx->ctx = ctx;

	qp->req_cb = sec_req_cb;

	spin_lock_init(&qp_ctx->req_lock);
	idr_init(&qp_ctx->req_idr);
	INIT_LIST_HEAD(&qp_ctx->backlog);

	ret = sec_alloc_qp_ctx_resource(qm, ctx, qp_ctx);
	if (ret)
		goto err_destroy_idr;

	ret = hisi_qm_start_qp(qp, 0);
	if (ret < 0)
		goto err_resource_free;

	return 0;

err_resource_free:
	sec_free_qp_ctx_resource(ctx, qp_ctx);
err_destroy_idr:
	idr_destroy(&qp_ctx->req_idr);
	return ret;
}

static void sec_release_qp_ctx(struct sec_ctx *ctx,
			       struct sec_qp_ctx *qp_ctx)
{
	hisi_qm_stop_qp(qp_ctx->qp);
	sec_free_qp_ctx_resource(ctx, qp_ctx);
	idr_destroy(&qp_ctx->req_idr);
}

static int sec_ctx_base_init(struct sec_ctx *ctx)
{
	struct sec_dev *sec;
	int i, ret;

	ctx->qps = sec_create_qps();
	if (!ctx->qps) {
		pr_err("Can not create sec qps!\n");
		return -ENODEV;
	}

	sec = container_of(ctx->qps[0]->qm, struct sec_dev, qm);
	ctx->sec = sec;
	ctx->dev = &sec->qm.pdev->dev;
	ctx->hlf_q_num = sec->ctx_q_num >> 1;

	ctx->pbuf_supported = ctx->sec->iommu_used;

	/* Half of queue depth is taken as fake requests limit in the queue. */
	ctx->fake_req_limit = ctx->qps[0]->sq_depth >> 1;
	ctx->qp_ctx = kcalloc(sec->ctx_q_num, sizeof(struct sec_qp_ctx),
			      GFP_KERNEL);
	if (!ctx->qp_ctx) {
		ret = -ENOMEM;
		goto err_destroy_qps;
	}

	for (i = 0; i < sec->ctx_q_num; i++) {
		ret = sec_create_qp_ctx(&sec->qm, ctx, i, 0);
		if (ret)
			goto err_sec_release_qp_ctx;
	}

	return 0;

err_sec_release_qp_ctx:
	for (i = i - 1; i >= 0; i--)
		sec_release_qp_ctx(ctx, &ctx->qp_ctx[i]);
	kfree(ctx->qp_ctx);
err_destroy_qps:
	sec_destroy_qps(ctx->qps, sec->ctx_q_num);
	return ret;
}

static void sec_ctx_base_uninit(struct sec_ctx *ctx)
{
	int i;

	for (i = 0; i < ctx->sec->ctx_q_num; i++)
		sec_release_qp_ctx(ctx, &ctx->qp_ctx[i]);

	sec_destroy_qps(ctx->qps, ctx->sec->ctx_q_num);
	kfree(ctx->qp_ctx);
}

static int sec_cipher_init(struct sec_ctx *ctx)
{
	struct sec_cipher_ctx *c_ctx = &ctx->c_ctx;

	c_ctx->c_key = dma_alloc_coherent(ctx->dev, SEC_MAX_KEY_SIZE,
					  &c_ctx->c_key_dma, GFP_KERNEL);
	if (!c_ctx->c_key)
		return -ENOMEM;

	return 0;
}

static void sec_cipher_uninit(struct sec_ctx *ctx)
{
	struct sec_cipher_ctx *c_ctx = &ctx->c_ctx;

	memzero_explicit(c_ctx->c_key, SEC_MAX_KEY_SIZE);
	dma_free_coherent(ctx->dev, SEC_MAX_KEY_SIZE,
			  c_ctx->c_key, c_ctx->c_key_dma);
}

static int sec_auth_init(struct sec_ctx *ctx)
{
	struct sec_auth_ctx *a_ctx = &ctx->a_ctx;

	a_ctx->a_key = dma_alloc_coherent(ctx->dev, SEC_MAX_AKEY_SIZE,
					  &a_ctx->a_key_dma, GFP_KERNEL);
	if (!a_ctx->a_key)
		return -ENOMEM;

	return 0;
}

static void sec_auth_uninit(struct sec_ctx *ctx)
{
	struct sec_auth_ctx *a_ctx = &ctx->a_ctx;

	memzero_explicit(a_ctx->a_key, SEC_MAX_AKEY_SIZE);
	dma_free_coherent(ctx->dev, SEC_MAX_AKEY_SIZE,
			  a_ctx->a_key, a_ctx->a_key_dma);
}

static int sec_skcipher_fbtfm_init(struct crypto_skcipher *tfm)
{
	const char *alg = crypto_tfm_alg_name(&tfm->base);
	struct sec_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct sec_cipher_ctx *c_ctx = &ctx->c_ctx;

	c_ctx->fallback = false;

	/* Currently, only XTS mode need fallback tfm when using 192bit key */
	if (likely(strncmp(alg, "xts", SEC_XTS_NAME_SZ)))
		return 0;

	c_ctx->fbtfm = crypto_alloc_sync_skcipher(alg, 0,
						  CRYPTO_ALG_NEED_FALLBACK);
	if (IS_ERR(c_ctx->fbtfm)) {
		pr_err("failed to alloc xts mode fallback tfm!\n");
		return PTR_ERR(c_ctx->fbtfm);
	}

	return 0;
}

static int sec_skcipher_init(struct crypto_skcipher *tfm)
{
	struct sec_ctx *ctx = crypto_skcipher_ctx(tfm);
	int ret;

	ctx->alg_type = SEC_SKCIPHER;
	crypto_skcipher_set_reqsize(tfm, sizeof(struct sec_req));
	ctx->c_ctx.ivsize = crypto_skcipher_ivsize(tfm);
	if (ctx->c_ctx.ivsize > SEC_IV_SIZE) {
		pr_err("get error skcipher iv size!\n");
		return -EINVAL;
	}

	ret = sec_ctx_base_init(ctx);
	if (ret)
		return ret;

	ret = sec_cipher_init(ctx);
	if (ret)
		goto err_cipher_init;

	ret = sec_skcipher_fbtfm_init(tfm);
	if (ret)
		goto err_fbtfm_init;

	return 0;

err_fbtfm_init:
	sec_cipher_uninit(ctx);
err_cipher_init:
	sec_ctx_base_uninit(ctx);
	return ret;
}

static void sec_skcipher_uninit(struct crypto_skcipher *tfm)
{
	struct sec_ctx *ctx = crypto_skcipher_ctx(tfm);

	if (ctx->c_ctx.fbtfm)
		crypto_free_sync_skcipher(ctx->c_ctx.fbtfm);

	sec_cipher_uninit(ctx);
	sec_ctx_base_uninit(ctx);
}

static int sec_skcipher_3des_setkey(struct crypto_skcipher *tfm, const u8 *key,
				    const u32 keylen,
				    const enum sec_cmode c_mode)
{
	struct sec_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct sec_cipher_ctx *c_ctx = &ctx->c_ctx;
	int ret;

	ret = verify_skcipher_des3_key(tfm, key);
	if (ret)
		return ret;

	switch (keylen) {
	case SEC_DES3_2KEY_SIZE:
		c_ctx->c_key_len = SEC_CKEY_3DES_2KEY;
		break;
	case SEC_DES3_3KEY_SIZE:
		c_ctx->c_key_len = SEC_CKEY_3DES_3KEY;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int sec_skcipher_aes_sm4_setkey(struct sec_cipher_ctx *c_ctx,
				       const u32 keylen,
				       const enum sec_cmode c_mode)
{
	if (c_mode == SEC_CMODE_XTS) {
		switch (keylen) {
		case SEC_XTS_MIN_KEY_SIZE:
			c_ctx->c_key_len = SEC_CKEY_128BIT;
			break;
		case SEC_XTS_MID_KEY_SIZE:
			c_ctx->fallback = true;
			break;
		case SEC_XTS_MAX_KEY_SIZE:
			c_ctx->c_key_len = SEC_CKEY_256BIT;
			break;
		default:
			pr_err("hisi_sec2: xts mode key error!\n");
			return -EINVAL;
		}
	} else {
		if (c_ctx->c_alg == SEC_CALG_SM4 &&
		    keylen != AES_KEYSIZE_128) {
			pr_err("hisi_sec2: sm4 key error!\n");
			return -EINVAL;
		} else {
			switch (keylen) {
			case AES_KEYSIZE_128:
				c_ctx->c_key_len = SEC_CKEY_128BIT;
				break;
			case AES_KEYSIZE_192:
				c_ctx->c_key_len = SEC_CKEY_192BIT;
				break;
			case AES_KEYSIZE_256:
				c_ctx->c_key_len = SEC_CKEY_256BIT;
				break;
			default:
				pr_err("hisi_sec2: aes key error!\n");
				return -EINVAL;
			}
		}
	}

	return 0;
}

static int sec_skcipher_setkey(struct crypto_skcipher *tfm, const u8 *key,
			       const u32 keylen, const enum sec_calg c_alg,
			       const enum sec_cmode c_mode)
{
	struct sec_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct sec_cipher_ctx *c_ctx = &ctx->c_ctx;
	struct device *dev = ctx->dev;
	int ret;

	if (c_mode == SEC_CMODE_XTS) {
		ret = xts_verify_key(tfm, key, keylen);
		if (ret) {
			dev_err(dev, "xts mode key err!\n");
			return ret;
		}
	}

	c_ctx->c_alg  = c_alg;
	c_ctx->c_mode = c_mode;

	switch (c_alg) {
	case SEC_CALG_3DES:
		ret = sec_skcipher_3des_setkey(tfm, key, keylen, c_mode);
		break;
	case SEC_CALG_AES:
	case SEC_CALG_SM4:
		ret = sec_skcipher_aes_sm4_setkey(c_ctx, keylen, c_mode);
		break;
	default:
		return -EINVAL;
	}

	if (ret) {
		dev_err(dev, "set sec key err!\n");
		return ret;
	}

	memcpy(c_ctx->c_key, key, keylen);
	if (c_ctx->fallback && c_ctx->fbtfm) {
		ret = crypto_sync_skcipher_setkey(c_ctx->fbtfm, key, keylen);
		if (ret) {
			dev_err(dev, "failed to set fallback skcipher key!\n");
			return ret;
		}
	}
	return 0;
}

#define GEN_SEC_SETKEY_FUNC(name, c_alg, c_mode)			\
static int sec_setkey_##name(struct crypto_skcipher *tfm, const u8 *key,\
	u32 keylen)							\
{									\
	return sec_skcipher_setkey(tfm, key, keylen, c_alg, c_mode);	\
}

GEN_SEC_SETKEY_FUNC(aes_ecb, SEC_CALG_AES, SEC_CMODE_ECB)
GEN_SEC_SETKEY_FUNC(aes_cbc, SEC_CALG_AES, SEC_CMODE_CBC)
GEN_SEC_SETKEY_FUNC(aes_xts, SEC_CALG_AES, SEC_CMODE_XTS)
GEN_SEC_SETKEY_FUNC(aes_ofb, SEC_CALG_AES, SEC_CMODE_OFB)
GEN_SEC_SETKEY_FUNC(aes_cfb, SEC_CALG_AES, SEC_CMODE_CFB)
GEN_SEC_SETKEY_FUNC(aes_ctr, SEC_CALG_AES, SEC_CMODE_CTR)
GEN_SEC_SETKEY_FUNC(3des_ecb, SEC_CALG_3DES, SEC_CMODE_ECB)
GEN_SEC_SETKEY_FUNC(3des_cbc, SEC_CALG_3DES, SEC_CMODE_CBC)
GEN_SEC_SETKEY_FUNC(sm4_xts, SEC_CALG_SM4, SEC_CMODE_XTS)
GEN_SEC_SETKEY_FUNC(sm4_cbc, SEC_CALG_SM4, SEC_CMODE_CBC)
GEN_SEC_SETKEY_FUNC(sm4_ofb, SEC_CALG_SM4, SEC_CMODE_OFB)
GEN_SEC_SETKEY_FUNC(sm4_cfb, SEC_CALG_SM4, SEC_CMODE_CFB)
GEN_SEC_SETKEY_FUNC(sm4_ctr, SEC_CALG_SM4, SEC_CMODE_CTR)

static int sec_cipher_pbuf_map(struct sec_ctx *ctx, struct sec_req *req,
			struct scatterlist *src)
{
	struct sec_aead_req *a_req = &req->aead_req;
	struct aead_request *aead_req = a_req->aead_req;
	struct sec_cipher_req *c_req = &req->c_req;
	struct sec_qp_ctx *qp_ctx = req->qp_ctx;
	struct device *dev = ctx->dev;
	int copy_size, pbuf_length;
	int req_id = req->req_id;
	struct crypto_aead *tfm;
	size_t authsize;
	u8 *mac_offset;

	if (ctx->alg_type == SEC_AEAD)
		copy_size = aead_req->cryptlen + aead_req->assoclen;
	else
		copy_size = c_req->c_len;

	pbuf_length = sg_copy_to_buffer(src, sg_nents(src),
			qp_ctx->res[req_id].pbuf, copy_size);
	if (unlikely(pbuf_length != copy_size)) {
		dev_err(dev, "copy src data to pbuf error!\n");
		return -EINVAL;
	}
	if (!c_req->encrypt && ctx->alg_type == SEC_AEAD) {
		tfm = crypto_aead_reqtfm(aead_req);
		authsize = crypto_aead_authsize(tfm);
		mac_offset = qp_ctx->res[req_id].pbuf + copy_size - authsize;
		memcpy(a_req->out_mac, mac_offset, authsize);
	}

	req->in_dma = qp_ctx->res[req_id].pbuf_dma;
	c_req->c_out_dma = req->in_dma;

	return 0;
}

static void sec_cipher_pbuf_unmap(struct sec_ctx *ctx, struct sec_req *req,
			struct scatterlist *dst)
{
	struct aead_request *aead_req = req->aead_req.aead_req;
	struct sec_cipher_req *c_req = &req->c_req;
	struct sec_qp_ctx *qp_ctx = req->qp_ctx;
	int copy_size, pbuf_length;
	int req_id = req->req_id;

	if (ctx->alg_type == SEC_AEAD)
		copy_size = c_req->c_len + aead_req->assoclen;
	else
		copy_size = c_req->c_len;

	pbuf_length = sg_copy_from_buffer(dst, sg_nents(dst),
			qp_ctx->res[req_id].pbuf, copy_size);
	if (unlikely(pbuf_length != copy_size))
		dev_err(ctx->dev, "copy pbuf data to dst error!\n");
}

static int sec_aead_mac_init(struct sec_aead_req *req)
{
	struct aead_request *aead_req = req->aead_req;
	struct crypto_aead *tfm = crypto_aead_reqtfm(aead_req);
	size_t authsize = crypto_aead_authsize(tfm);
	u8 *mac_out = req->out_mac;
	struct scatterlist *sgl = aead_req->src;
	size_t copy_size;
	off_t skip_size;

	/* Copy input mac */
	skip_size = aead_req->assoclen + aead_req->cryptlen - authsize;
	copy_size = sg_pcopy_to_buffer(sgl, sg_nents(sgl), mac_out,
				       authsize, skip_size);
	if (unlikely(copy_size != authsize))
		return -EINVAL;

	return 0;
}

static int sec_cipher_map(struct sec_ctx *ctx, struct sec_req *req,
			  struct scatterlist *src, struct scatterlist *dst)
{
	struct sec_cipher_req *c_req = &req->c_req;
	struct sec_aead_req *a_req = &req->aead_req;
	struct sec_qp_ctx *qp_ctx = req->qp_ctx;
	struct sec_alg_res *res = &qp_ctx->res[req->req_id];
	struct device *dev = ctx->dev;
	int ret;

	if (req->use_pbuf) {
		c_req->c_ivin = res->pbuf + SEC_PBUF_IV_OFFSET;
		c_req->c_ivin_dma = res->pbuf_dma + SEC_PBUF_IV_OFFSET;
		if (ctx->alg_type == SEC_AEAD) {
			a_req->a_ivin = res->a_ivin;
			a_req->a_ivin_dma = res->a_ivin_dma;
			a_req->out_mac = res->pbuf + SEC_PBUF_MAC_OFFSET;
			a_req->out_mac_dma = res->pbuf_dma +
					SEC_PBUF_MAC_OFFSET;
		}
		ret = sec_cipher_pbuf_map(ctx, req, src);

		return ret;
	}
	c_req->c_ivin = res->c_ivin;
	c_req->c_ivin_dma = res->c_ivin_dma;
	if (ctx->alg_type == SEC_AEAD) {
		a_req->a_ivin = res->a_ivin;
		a_req->a_ivin_dma = res->a_ivin_dma;
		a_req->out_mac = res->out_mac;
		a_req->out_mac_dma = res->out_mac_dma;
	}

	req->in = hisi_acc_sg_buf_map_to_hw_sgl(dev, src,
						qp_ctx->c_in_pool,
						req->req_id,
						&req->in_dma);
	if (IS_ERR(req->in)) {
		dev_err(dev, "fail to dma map input sgl buffers!\n");
		return PTR_ERR(req->in);
	}

	if (!c_req->encrypt && ctx->alg_type == SEC_AEAD) {
		ret = sec_aead_mac_init(a_req);
		if (unlikely(ret)) {
			dev_err(dev, "fail to init mac data for ICV!\n");
			hisi_acc_sg_buf_unmap(dev, src, req->in);
			return ret;
		}
	}

	if (dst == src) {
		c_req->c_out = req->in;
		c_req->c_out_dma = req->in_dma;
	} else {
		c_req->c_out = hisi_acc_sg_buf_map_to_hw_sgl(dev, dst,
							     qp_ctx->c_out_pool,
							     req->req_id,
							     &c_req->c_out_dma);

		if (IS_ERR(c_req->c_out)) {
			dev_err(dev, "fail to dma map output sgl buffers!\n");
			hisi_acc_sg_buf_unmap(dev, src, req->in);
			return PTR_ERR(c_req->c_out);
		}
	}

	return 0;
}

static void sec_cipher_unmap(struct sec_ctx *ctx, struct sec_req *req,
			     struct scatterlist *src, struct scatterlist *dst)
{
	struct sec_cipher_req *c_req = &req->c_req;
	struct device *dev = ctx->dev;

	if (req->use_pbuf) {
		sec_cipher_pbuf_unmap(ctx, req, dst);
	} else {
		if (dst != src)
			hisi_acc_sg_buf_unmap(dev, src, req->in);

		hisi_acc_sg_buf_unmap(dev, dst, c_req->c_out);
	}
}

static int sec_skcipher_sgl_map(struct sec_ctx *ctx, struct sec_req *req)
{
	struct skcipher_request *sq = req->c_req.sk_req;

	return sec_cipher_map(ctx, req, sq->src, sq->dst);
}

static void sec_skcipher_sgl_unmap(struct sec_ctx *ctx, struct sec_req *req)
{
	struct skcipher_request *sq = req->c_req.sk_req;

	sec_cipher_unmap(ctx, req, sq->src, sq->dst);
}

static int sec_aead_aes_set_key(struct sec_cipher_ctx *c_ctx,
				struct crypto_authenc_keys *keys)
{
	switch (keys->enckeylen) {
	case AES_KEYSIZE_128:
		c_ctx->c_key_len = SEC_CKEY_128BIT;
		break;
	case AES_KEYSIZE_192:
		c_ctx->c_key_len = SEC_CKEY_192BIT;
		break;
	case AES_KEYSIZE_256:
		c_ctx->c_key_len = SEC_CKEY_256BIT;
		break;
	default:
		pr_err("hisi_sec2: aead aes key error!\n");
		return -EINVAL;
	}
	memcpy(c_ctx->c_key, keys->enckey, keys->enckeylen);

	return 0;
}

static int sec_aead_auth_set_key(struct sec_auth_ctx *ctx,
				 struct crypto_authenc_keys *keys)
{
	struct crypto_shash *hash_tfm = ctx->hash_tfm;
	int blocksize, digestsize, ret;

	if (!keys->authkeylen) {
		pr_err("hisi_sec2: aead auth key error!\n");
		return -EINVAL;
	}

	blocksize = crypto_shash_blocksize(hash_tfm);
	digestsize = crypto_shash_digestsize(hash_tfm);
	if (keys->authkeylen > blocksize) {
		ret = crypto_shash_tfm_digest(hash_tfm, keys->authkey,
					      keys->authkeylen, ctx->a_key);
		if (ret) {
			pr_err("hisi_sec2: aead auth digest error!\n");
			return -EINVAL;
		}
		ctx->a_key_len = digestsize;
	} else {
		memcpy(ctx->a_key, keys->authkey, keys->authkeylen);
		ctx->a_key_len = keys->authkeylen;
	}

	return 0;
}

static int sec_aead_setauthsize(struct crypto_aead *aead, unsigned int authsize)
{
	struct crypto_tfm *tfm = crypto_aead_tfm(aead);
	struct sec_ctx *ctx = crypto_tfm_ctx(tfm);
	struct sec_auth_ctx *a_ctx = &ctx->a_ctx;

	if (unlikely(a_ctx->fallback_aead_tfm))
		return crypto_aead_setauthsize(a_ctx->fallback_aead_tfm, authsize);

	return 0;
}

static int sec_aead_fallback_setkey(struct sec_auth_ctx *a_ctx,
				    struct crypto_aead *tfm, const u8 *key,
				    unsigned int keylen)
{
	crypto_aead_clear_flags(a_ctx->fallback_aead_tfm, CRYPTO_TFM_REQ_MASK);
	crypto_aead_set_flags(a_ctx->fallback_aead_tfm,
			      crypto_aead_get_flags(tfm) & CRYPTO_TFM_REQ_MASK);
	return crypto_aead_setkey(a_ctx->fallback_aead_tfm, key, keylen);
}

static int sec_aead_setkey(struct crypto_aead *tfm, const u8 *key,
			   const u32 keylen, const enum sec_hash_alg a_alg,
			   const enum sec_calg c_alg,
			   const enum sec_mac_len mac_len,
			   const enum sec_cmode c_mode)
{
	struct sec_ctx *ctx = crypto_aead_ctx(tfm);
	struct sec_cipher_ctx *c_ctx = &ctx->c_ctx;
	struct sec_auth_ctx *a_ctx = &ctx->a_ctx;
	struct device *dev = ctx->dev;
	struct crypto_authenc_keys keys;
	int ret;

	ctx->a_ctx.a_alg = a_alg;
	ctx->c_ctx.c_alg = c_alg;
	ctx->a_ctx.mac_len = mac_len;
	c_ctx->c_mode = c_mode;

	if (c_mode == SEC_CMODE_CCM || c_mode == SEC_CMODE_GCM) {
		ret = sec_skcipher_aes_sm4_setkey(c_ctx, keylen, c_mode);
		if (ret) {
			dev_err(dev, "set sec aes ccm cipher key err!\n");
			return ret;
		}
		memcpy(c_ctx->c_key, key, keylen);

		if (unlikely(a_ctx->fallback_aead_tfm)) {
			ret = sec_aead_fallback_setkey(a_ctx, tfm, key, keylen);
			if (ret)
				return ret;
		}

		return 0;
	}

	if (crypto_authenc_extractkeys(&keys, key, keylen))
		goto bad_key;

	ret = sec_aead_aes_set_key(c_ctx, &keys);
	if (ret) {
		dev_err(dev, "set sec cipher key err!\n");
		goto bad_key;
	}

	ret = sec_aead_auth_set_key(&ctx->a_ctx, &keys);
	if (ret) {
		dev_err(dev, "set sec auth key err!\n");
		goto bad_key;
	}

	if ((ctx->a_ctx.mac_len & SEC_SQE_LEN_RATE_MASK)  ||
	    (ctx->a_ctx.a_key_len & SEC_SQE_LEN_RATE_MASK)) {
		dev_err(dev, "MAC or AUTH key length error!\n");
		goto bad_key;
	}

	return 0;

bad_key:
	memzero_explicit(&keys, sizeof(struct crypto_authenc_keys));
	return -EINVAL;
}


#define GEN_SEC_AEAD_SETKEY_FUNC(name, aalg, calg, maclen, cmode)	\
static int sec_setkey_##name(struct crypto_aead *tfm, const u8 *key,	\
	u32 keylen)							\
{									\
	return sec_aead_setkey(tfm, key, keylen, aalg, calg, maclen, cmode);\
}

GEN_SEC_AEAD_SETKEY_FUNC(aes_cbc_sha1, SEC_A_HMAC_SHA1,
			 SEC_CALG_AES, SEC_HMAC_SHA1_MAC, SEC_CMODE_CBC)
GEN_SEC_AEAD_SETKEY_FUNC(aes_cbc_sha256, SEC_A_HMAC_SHA256,
			 SEC_CALG_AES, SEC_HMAC_SHA256_MAC, SEC_CMODE_CBC)
GEN_SEC_AEAD_SETKEY_FUNC(aes_cbc_sha512, SEC_A_HMAC_SHA512,
			 SEC_CALG_AES, SEC_HMAC_SHA512_MAC, SEC_CMODE_CBC)
GEN_SEC_AEAD_SETKEY_FUNC(aes_ccm, 0, SEC_CALG_AES,
			 SEC_HMAC_CCM_MAC, SEC_CMODE_CCM)
GEN_SEC_AEAD_SETKEY_FUNC(aes_gcm, 0, SEC_CALG_AES,
			 SEC_HMAC_GCM_MAC, SEC_CMODE_GCM)
GEN_SEC_AEAD_SETKEY_FUNC(sm4_ccm, 0, SEC_CALG_SM4,
			 SEC_HMAC_CCM_MAC, SEC_CMODE_CCM)
GEN_SEC_AEAD_SETKEY_FUNC(sm4_gcm, 0, SEC_CALG_SM4,
			 SEC_HMAC_GCM_MAC, SEC_CMODE_GCM)

static int sec_aead_sgl_map(struct sec_ctx *ctx, struct sec_req *req)
{
	struct aead_request *aq = req->aead_req.aead_req;

	return sec_cipher_map(ctx, req, aq->src, aq->dst);
}

static void sec_aead_sgl_unmap(struct sec_ctx *ctx, struct sec_req *req)
{
	struct aead_request *aq = req->aead_req.aead_req;

	sec_cipher_unmap(ctx, req, aq->src, aq->dst);
}

static int sec_request_transfer(struct sec_ctx *ctx, struct sec_req *req)
{
	int ret;

	ret = ctx->req_op->buf_map(ctx, req);
	if (unlikely(ret))
		return ret;

	ctx->req_op->do_transfer(ctx, req);

	ret = ctx->req_op->bd_fill(ctx, req);
	if (unlikely(ret))
		goto unmap_req_buf;

	return ret;

unmap_req_buf:
	ctx->req_op->buf_unmap(ctx, req);
	return ret;
}

static void sec_request_untransfer(struct sec_ctx *ctx, struct sec_req *req)
{
	ctx->req_op->buf_unmap(ctx, req);
}

static void sec_skcipher_copy_iv(struct sec_ctx *ctx, struct sec_req *req)
{
	struct skcipher_request *sk_req = req->c_req.sk_req;
	struct sec_cipher_req *c_req = &req->c_req;

	memcpy(c_req->c_ivin, sk_req->iv, ctx->c_ctx.ivsize);
}

static int sec_skcipher_bd_fill(struct sec_ctx *ctx, struct sec_req *req)
{
	struct sec_cipher_ctx *c_ctx = &ctx->c_ctx;
	struct sec_cipher_req *c_req = &req->c_req;
	struct sec_sqe *sec_sqe = &req->sec_sqe;
	u8 scene, sa_type, da_type;
	u8 bd_type, cipher;
	u8 de = 0;

	memset(sec_sqe, 0, sizeof(struct sec_sqe));

	sec_sqe->type2.c_key_addr = cpu_to_le64(c_ctx->c_key_dma);
	sec_sqe->type2.c_ivin_addr = cpu_to_le64(c_req->c_ivin_dma);
	sec_sqe->type2.data_src_addr = cpu_to_le64(req->in_dma);
	sec_sqe->type2.data_dst_addr = cpu_to_le64(c_req->c_out_dma);

	sec_sqe->type2.icvw_kmode |= cpu_to_le16(((u16)c_ctx->c_mode) <<
						SEC_CMODE_OFFSET);
	sec_sqe->type2.c_alg = c_ctx->c_alg;
	sec_sqe->type2.icvw_kmode |= cpu_to_le16(((u16)c_ctx->c_key_len) <<
						SEC_CKEY_OFFSET);

	bd_type = SEC_BD_TYPE2;
	if (c_req->encrypt)
		cipher = SEC_CIPHER_ENC << SEC_CIPHER_OFFSET;
	else
		cipher = SEC_CIPHER_DEC << SEC_CIPHER_OFFSET;
	sec_sqe->type_cipher_auth = bd_type | cipher;

	/* Set destination and source address type */
	if (req->use_pbuf) {
		sa_type = SEC_PBUF << SEC_SRC_SGL_OFFSET;
		da_type = SEC_PBUF << SEC_DST_SGL_OFFSET;
	} else {
		sa_type = SEC_SGL << SEC_SRC_SGL_OFFSET;
		da_type = SEC_SGL << SEC_DST_SGL_OFFSET;
	}

	sec_sqe->sdm_addr_type |= da_type;
	scene = SEC_COMM_SCENE << SEC_SCENE_OFFSET;
	if (req->in_dma != c_req->c_out_dma)
		de = 0x1 << SEC_DE_OFFSET;

	sec_sqe->sds_sa_type = (de | scene | sa_type);

	sec_sqe->type2.clen_ivhlen |= cpu_to_le32(c_req->c_len);
	sec_sqe->type2.tag = cpu_to_le16((u16)req->req_id);

	return 0;
}

static int sec_skcipher_bd_fill_v3(struct sec_ctx *ctx, struct sec_req *req)
{
	struct sec_sqe3 *sec_sqe3 = &req->sec_sqe3;
	struct sec_cipher_ctx *c_ctx = &ctx->c_ctx;
	struct sec_cipher_req *c_req = &req->c_req;
	u32 bd_param = 0;
	u16 cipher;

	memset(sec_sqe3, 0, sizeof(struct sec_sqe3));

	sec_sqe3->c_key_addr = cpu_to_le64(c_ctx->c_key_dma);
	sec_sqe3->no_scene.c_ivin_addr = cpu_to_le64(c_req->c_ivin_dma);
	sec_sqe3->data_src_addr = cpu_to_le64(req->in_dma);
	sec_sqe3->data_dst_addr = cpu_to_le64(c_req->c_out_dma);

	sec_sqe3->c_mode_alg = ((u8)c_ctx->c_alg << SEC_CALG_OFFSET_V3) |
						c_ctx->c_mode;
	sec_sqe3->c_icv_key |= cpu_to_le16(((u16)c_ctx->c_key_len) <<
						SEC_CKEY_OFFSET_V3);

	if (c_req->encrypt)
		cipher = SEC_CIPHER_ENC;
	else
		cipher = SEC_CIPHER_DEC;
	sec_sqe3->c_icv_key |= cpu_to_le16(cipher);

	/* Set the CTR counter mode is 128bit rollover */
	sec_sqe3->auth_mac_key = cpu_to_le32((u32)SEC_CTR_CNT_ROLLOVER <<
					SEC_CTR_CNT_OFFSET);

	if (req->use_pbuf) {
		bd_param |= SEC_PBUF << SEC_SRC_SGL_OFFSET_V3;
		bd_param |= SEC_PBUF << SEC_DST_SGL_OFFSET_V3;
	} else {
		bd_param |= SEC_SGL << SEC_SRC_SGL_OFFSET_V3;
		bd_param |= SEC_SGL << SEC_DST_SGL_OFFSET_V3;
	}

	bd_param |= SEC_COMM_SCENE << SEC_SCENE_OFFSET_V3;
	if (req->in_dma != c_req->c_out_dma)
		bd_param |= 0x1 << SEC_DE_OFFSET_V3;

	bd_param |= SEC_BD_TYPE3;
	sec_sqe3->bd_param = cpu_to_le32(bd_param);

	sec_sqe3->c_len_ivin |= cpu_to_le32(c_req->c_len);
	sec_sqe3->tag = cpu_to_le64(req);

	return 0;
}

/* increment counter (128-bit int) */
static void ctr_iv_inc(__u8 *counter, __u8 bits, __u32 nums)
{
	do {
		--bits;
		nums += counter[bits];
		counter[bits] = nums & BITS_MASK;
		nums >>= BYTE_BITS;
	} while (bits && nums);
}

static void sec_update_iv(struct sec_req *req, enum sec_alg_type alg_type)
{
	struct aead_request *aead_req = req->aead_req.aead_req;
	struct skcipher_request *sk_req = req->c_req.sk_req;
	u32 iv_size = req->ctx->c_ctx.ivsize;
	struct scatterlist *sgl;
	unsigned int cryptlen;
	size_t sz;
	u8 *iv;

	if (req->c_req.encrypt)
		sgl = alg_type == SEC_SKCIPHER ? sk_req->dst : aead_req->dst;
	else
		sgl = alg_type == SEC_SKCIPHER ? sk_req->src : aead_req->src;

	if (alg_type == SEC_SKCIPHER) {
		iv = sk_req->iv;
		cryptlen = sk_req->cryptlen;
	} else {
		iv = aead_req->iv;
		cryptlen = aead_req->cryptlen;
	}

	if (req->ctx->c_ctx.c_mode == SEC_CMODE_CBC) {
		sz = sg_pcopy_to_buffer(sgl, sg_nents(sgl), iv, iv_size,
					cryptlen - iv_size);
		if (unlikely(sz != iv_size))
			dev_err(req->ctx->dev, "copy output iv error!\n");
	} else {
		sz = cryptlen / iv_size;
		if (cryptlen % iv_size)
			sz += 1;
		ctr_iv_inc(iv, iv_size, sz);
	}
}

static struct sec_req *sec_back_req_clear(struct sec_ctx *ctx,
				struct sec_qp_ctx *qp_ctx)
{
	struct sec_req *backlog_req = NULL;

	spin_lock_bh(&qp_ctx->req_lock);
	if (ctx->fake_req_limit >=
	    atomic_read(&qp_ctx->qp->qp_status.used) &&
	    !list_empty(&qp_ctx->backlog)) {
		backlog_req = list_first_entry(&qp_ctx->backlog,
				typeof(*backlog_req), backlog_head);
		list_del(&backlog_req->backlog_head);
	}
	spin_unlock_bh(&qp_ctx->req_lock);

	return backlog_req;
}

static void sec_skcipher_callback(struct sec_ctx *ctx, struct sec_req *req,
				  int err)
{
	struct skcipher_request *sk_req = req->c_req.sk_req;
	struct sec_qp_ctx *qp_ctx = req->qp_ctx;
	struct skcipher_request *backlog_sk_req;
	struct sec_req *backlog_req;

	sec_free_req_id(req);

	/* IV output at encrypto of CBC/CTR mode */
	if (!err && (ctx->c_ctx.c_mode == SEC_CMODE_CBC ||
	    ctx->c_ctx.c_mode == SEC_CMODE_CTR) && req->c_req.encrypt)
		sec_update_iv(req, SEC_SKCIPHER);

	while (1) {
		backlog_req = sec_back_req_clear(ctx, qp_ctx);
		if (!backlog_req)
			break;

		backlog_sk_req = backlog_req->c_req.sk_req;
		skcipher_request_complete(backlog_sk_req, -EINPROGRESS);
		atomic64_inc(&ctx->sec->debug.dfx.recv_busy_cnt);
	}

	skcipher_request_complete(sk_req, err);
}

static void set_aead_auth_iv(struct sec_ctx *ctx, struct sec_req *req)
{
	struct aead_request *aead_req = req->aead_req.aead_req;
	struct sec_cipher_req *c_req = &req->c_req;
	struct sec_aead_req *a_req = &req->aead_req;
	size_t authsize = ctx->a_ctx.mac_len;
	u32 data_size = aead_req->cryptlen;
	u8 flage = 0;
	u8 cm, cl;

	/* the specification has been checked in aead_iv_demension_check() */
	cl = c_req->c_ivin[0] + 1;
	c_req->c_ivin[ctx->c_ctx.ivsize - cl] = 0x00;
	memset(&c_req->c_ivin[ctx->c_ctx.ivsize - cl], 0, cl);
	c_req->c_ivin[ctx->c_ctx.ivsize - IV_LAST_BYTE1] = IV_CTR_INIT;

	/* the last 3bit is L' */
	flage |= c_req->c_ivin[0] & IV_CL_MASK;

	/* the M' is bit3~bit5, the Flags is bit6 */
	cm = (authsize - IV_CM_CAL_NUM) / IV_CM_CAL_NUM;
	flage |= cm << IV_CM_OFFSET;
	if (aead_req->assoclen)
		flage |= 0x01 << IV_FLAGS_OFFSET;

	memcpy(a_req->a_ivin, c_req->c_ivin, ctx->c_ctx.ivsize);
	a_req->a_ivin[0] = flage;

	/*
	 * the last 32bit is counter's initial number,
	 * but the nonce uses the first 16bit
	 * the tail 16bit fill with the cipher length
	 */
	if (!c_req->encrypt)
		data_size = aead_req->cryptlen - authsize;

	a_req->a_ivin[ctx->c_ctx.ivsize - IV_LAST_BYTE1] =
			data_size & IV_LAST_BYTE_MASK;
	data_size >>= IV_BYTE_OFFSET;
	a_req->a_ivin[ctx->c_ctx.ivsize - IV_LAST_BYTE2] =
			data_size & IV_LAST_BYTE_MASK;
}

static void sec_aead_set_iv(struct sec_ctx *ctx, struct sec_req *req)
{
	struct aead_request *aead_req = req->aead_req.aead_req;
	struct crypto_aead *tfm = crypto_aead_reqtfm(aead_req);
	size_t authsize = crypto_aead_authsize(tfm);
	struct sec_cipher_req *c_req = &req->c_req;
	struct sec_aead_req *a_req = &req->aead_req;

	memcpy(c_req->c_ivin, aead_req->iv, ctx->c_ctx.ivsize);

	if (ctx->c_ctx.c_mode == SEC_CMODE_CCM) {
		/*
		 * CCM 16Byte Cipher_IV: {1B_Flage,13B_IV,2B_counter},
		 * the  counter must set to 0x01
		 */
		ctx->a_ctx.mac_len = authsize;
		/* CCM 16Byte Auth_IV: {1B_AFlage,13B_IV,2B_Ptext_length} */
		set_aead_auth_iv(ctx, req);
	}

	/* GCM 12Byte Cipher_IV == Auth_IV */
	if (ctx->c_ctx.c_mode == SEC_CMODE_GCM) {
		ctx->a_ctx.mac_len = authsize;
		memcpy(a_req->a_ivin, c_req->c_ivin, SEC_AIV_SIZE);
	}
}

static void sec_auth_bd_fill_xcm(struct sec_auth_ctx *ctx, int dir,
				 struct sec_req *req, struct sec_sqe *sec_sqe)
{
	struct sec_aead_req *a_req = &req->aead_req;
	struct aead_request *aq = a_req->aead_req;

	/* C_ICV_Len is MAC size, 0x4 ~ 0x10 */
	sec_sqe->type2.icvw_kmode |= cpu_to_le16((u16)ctx->mac_len);

	/* mode set to CCM/GCM, don't set {A_Alg, AKey_Len, MAC_Len} */
	sec_sqe->type2.a_key_addr = sec_sqe->type2.c_key_addr;
	sec_sqe->type2.a_ivin_addr = cpu_to_le64(a_req->a_ivin_dma);
	sec_sqe->type_cipher_auth |= SEC_NO_AUTH << SEC_AUTH_OFFSET;

	if (dir)
		sec_sqe->sds_sa_type &= SEC_CIPHER_AUTH;
	else
		sec_sqe->sds_sa_type |= SEC_AUTH_CIPHER;

	sec_sqe->type2.alen_ivllen = cpu_to_le32(aq->assoclen);
	sec_sqe->type2.auth_src_offset = cpu_to_le16(0x0);
	sec_sqe->type2.cipher_src_offset = cpu_to_le16((u16)aq->assoclen);

	sec_sqe->type2.mac_addr = cpu_to_le64(a_req->out_mac_dma);
}

static void sec_auth_bd_fill_xcm_v3(struct sec_auth_ctx *ctx, int dir,
				    struct sec_req *req, struct sec_sqe3 *sqe3)
{
	struct sec_aead_req *a_req = &req->aead_req;
	struct aead_request *aq = a_req->aead_req;

	/* C_ICV_Len is MAC size, 0x4 ~ 0x10 */
	sqe3->c_icv_key |= cpu_to_le16((u16)ctx->mac_len << SEC_MAC_OFFSET_V3);

	/* mode set to CCM/GCM, don't set {A_Alg, AKey_Len, MAC_Len} */
	sqe3->a_key_addr = sqe3->c_key_addr;
	sqe3->auth_ivin.a_ivin_addr = cpu_to_le64(a_req->a_ivin_dma);
	sqe3->auth_mac_key |= SEC_NO_AUTH;

	if (dir)
		sqe3->huk_iv_seq &= SEC_CIPHER_AUTH_V3;
	else
		sqe3->huk_iv_seq |= SEC_AUTH_CIPHER_V3;

	sqe3->a_len_key = cpu_to_le32(aq->assoclen);
	sqe3->auth_src_offset = cpu_to_le16(0x0);
	sqe3->cipher_src_offset = cpu_to_le16((u16)aq->assoclen);
	sqe3->mac_addr = cpu_to_le64(a_req->out_mac_dma);
}

static void sec_auth_bd_fill_ex(struct sec_auth_ctx *ctx, int dir,
			       struct sec_req *req, struct sec_sqe *sec_sqe)
{
	struct sec_aead_req *a_req = &req->aead_req;
	struct sec_cipher_req *c_req = &req->c_req;
	struct aead_request *aq = a_req->aead_req;

	sec_sqe->type2.a_key_addr = cpu_to_le64(ctx->a_key_dma);

	sec_sqe->type2.mac_key_alg =
			cpu_to_le32(ctx->mac_len / SEC_SQE_LEN_RATE);

	sec_sqe->type2.mac_key_alg |=
			cpu_to_le32((u32)((ctx->a_key_len) /
			SEC_SQE_LEN_RATE) << SEC_AKEY_OFFSET);

	sec_sqe->type2.mac_key_alg |=
			cpu_to_le32((u32)(ctx->a_alg) << SEC_AEAD_ALG_OFFSET);

	if (dir) {
		sec_sqe->type_cipher_auth |= SEC_AUTH_TYPE1 << SEC_AUTH_OFFSET;
		sec_sqe->sds_sa_type &= SEC_CIPHER_AUTH;
	} else {
		sec_sqe->type_cipher_auth |= SEC_AUTH_TYPE2 << SEC_AUTH_OFFSET;
		sec_sqe->sds_sa_type |= SEC_AUTH_CIPHER;
	}
	sec_sqe->type2.alen_ivllen = cpu_to_le32(c_req->c_len + aq->assoclen);

	sec_sqe->type2.cipher_src_offset = cpu_to_le16((u16)aq->assoclen);

	sec_sqe->type2.mac_addr = cpu_to_le64(a_req->out_mac_dma);
}

static int sec_aead_bd_fill(struct sec_ctx *ctx, struct sec_req *req)
{
	struct sec_auth_ctx *auth_ctx = &ctx->a_ctx;
	struct sec_sqe *sec_sqe = &req->sec_sqe;
	int ret;

	ret = sec_skcipher_bd_fill(ctx, req);
	if (unlikely(ret)) {
		dev_err(ctx->dev, "skcipher bd fill is error!\n");
		return ret;
	}

	if (ctx->c_ctx.c_mode == SEC_CMODE_CCM ||
	    ctx->c_ctx.c_mode == SEC_CMODE_GCM)
		sec_auth_bd_fill_xcm(auth_ctx, req->c_req.encrypt, req, sec_sqe);
	else
		sec_auth_bd_fill_ex(auth_ctx, req->c_req.encrypt, req, sec_sqe);

	return 0;
}

static void sec_auth_bd_fill_ex_v3(struct sec_auth_ctx *ctx, int dir,
				   struct sec_req *req, struct sec_sqe3 *sqe3)
{
	struct sec_aead_req *a_req = &req->aead_req;
	struct sec_cipher_req *c_req = &req->c_req;
	struct aead_request *aq = a_req->aead_req;

	sqe3->a_key_addr = cpu_to_le64(ctx->a_key_dma);

	sqe3->auth_mac_key |=
			cpu_to_le32((u32)(ctx->mac_len /
			SEC_SQE_LEN_RATE) << SEC_MAC_OFFSET_V3);

	sqe3->auth_mac_key |=
			cpu_to_le32((u32)(ctx->a_key_len /
			SEC_SQE_LEN_RATE) << SEC_AKEY_OFFSET_V3);

	sqe3->auth_mac_key |=
			cpu_to_le32((u32)(ctx->a_alg) << SEC_AUTH_ALG_OFFSET_V3);

	if (dir) {
		sqe3->auth_mac_key |= cpu_to_le32((u32)SEC_AUTH_TYPE1);
		sqe3->huk_iv_seq &= SEC_CIPHER_AUTH_V3;
	} else {
		sqe3->auth_mac_key |= cpu_to_le32((u32)SEC_AUTH_TYPE2);
		sqe3->huk_iv_seq |= SEC_AUTH_CIPHER_V3;
	}
	sqe3->a_len_key = cpu_to_le32(c_req->c_len + aq->assoclen);

	sqe3->cipher_src_offset = cpu_to_le16((u16)aq->assoclen);

	sqe3->mac_addr = cpu_to_le64(a_req->out_mac_dma);
}

static int sec_aead_bd_fill_v3(struct sec_ctx *ctx, struct sec_req *req)
{
	struct sec_auth_ctx *auth_ctx = &ctx->a_ctx;
	struct sec_sqe3 *sec_sqe3 = &req->sec_sqe3;
	int ret;

	ret = sec_skcipher_bd_fill_v3(ctx, req);
	if (unlikely(ret)) {
		dev_err(ctx->dev, "skcipher bd3 fill is error!\n");
		return ret;
	}

	if (ctx->c_ctx.c_mode == SEC_CMODE_CCM ||
	    ctx->c_ctx.c_mode == SEC_CMODE_GCM)
		sec_auth_bd_fill_xcm_v3(auth_ctx, req->c_req.encrypt,
					req, sec_sqe3);
	else
		sec_auth_bd_fill_ex_v3(auth_ctx, req->c_req.encrypt,
				       req, sec_sqe3);

	return 0;
}

static void sec_aead_callback(struct sec_ctx *c, struct sec_req *req, int err)
{
	struct aead_request *a_req = req->aead_req.aead_req;
	struct crypto_aead *tfm = crypto_aead_reqtfm(a_req);
	struct sec_aead_req *aead_req = &req->aead_req;
	struct sec_cipher_req *c_req = &req->c_req;
	size_t authsize = crypto_aead_authsize(tfm);
	struct sec_qp_ctx *qp_ctx = req->qp_ctx;
	struct aead_request *backlog_aead_req;
	struct sec_req *backlog_req;
	size_t sz;

	if (!err && c->c_ctx.c_mode == SEC_CMODE_CBC && c_req->encrypt)
		sec_update_iv(req, SEC_AEAD);

	/* Copy output mac */
	if (!err && c_req->encrypt) {
		struct scatterlist *sgl = a_req->dst;

		sz = sg_pcopy_from_buffer(sgl, sg_nents(sgl),
					  aead_req->out_mac,
					  authsize, a_req->cryptlen +
					  a_req->assoclen);
		if (unlikely(sz != authsize)) {
			dev_err(c->dev, "copy out mac err!\n");
			err = -EINVAL;
		}
	}

	sec_free_req_id(req);

	while (1) {
		backlog_req = sec_back_req_clear(c, qp_ctx);
		if (!backlog_req)
			break;

		backlog_aead_req = backlog_req->aead_req.aead_req;
		aead_request_complete(backlog_aead_req, -EINPROGRESS);
		atomic64_inc(&c->sec->debug.dfx.recv_busy_cnt);
	}

	aead_request_complete(a_req, err);
}

static void sec_request_uninit(struct sec_ctx *ctx, struct sec_req *req)
{
	sec_free_req_id(req);
	sec_free_queue_id(ctx, req);
}

static int sec_request_init(struct sec_ctx *ctx, struct sec_req *req)
{
	struct sec_qp_ctx *qp_ctx;
	int queue_id;

	/* To load balance */
	queue_id = sec_alloc_queue_id(ctx, req);
	qp_ctx = &ctx->qp_ctx[queue_id];

	req->req_id = sec_alloc_req_id(req, qp_ctx);
	if (unlikely(req->req_id < 0)) {
		sec_free_queue_id(ctx, req);
		return req->req_id;
	}

	return 0;
}

static int sec_process(struct sec_ctx *ctx, struct sec_req *req)
{
	struct sec_cipher_req *c_req = &req->c_req;
	int ret;

	ret = sec_request_init(ctx, req);
	if (unlikely(ret))
		return ret;

	ret = sec_request_transfer(ctx, req);
	if (unlikely(ret))
		goto err_uninit_req;

	/* Output IV as decrypto */
	if (!req->c_req.encrypt && (ctx->c_ctx.c_mode == SEC_CMODE_CBC ||
	    ctx->c_ctx.c_mode == SEC_CMODE_CTR))
		sec_update_iv(req, ctx->alg_type);

	ret = ctx->req_op->bd_send(ctx, req);
	if (unlikely((ret != -EBUSY && ret != -EINPROGRESS) ||
		(ret == -EBUSY && !(req->flag & CRYPTO_TFM_REQ_MAY_BACKLOG)))) {
		dev_err_ratelimited(ctx->dev, "send sec request failed!\n");
		goto err_send_req;
	}

	return ret;

err_send_req:
	/* As failing, restore the IV from user */
	if (ctx->c_ctx.c_mode == SEC_CMODE_CBC && !req->c_req.encrypt) {
		if (ctx->alg_type == SEC_SKCIPHER)
			memcpy(req->c_req.sk_req->iv, c_req->c_ivin,
			       ctx->c_ctx.ivsize);
		else
			memcpy(req->aead_req.aead_req->iv, c_req->c_ivin,
			       ctx->c_ctx.ivsize);
	}

	sec_request_untransfer(ctx, req);
err_uninit_req:
	sec_request_uninit(ctx, req);
	return ret;
}

static const struct sec_req_op sec_skcipher_req_ops = {
	.buf_map	= sec_skcipher_sgl_map,
	.buf_unmap	= sec_skcipher_sgl_unmap,
	.do_transfer	= sec_skcipher_copy_iv,
	.bd_fill	= sec_skcipher_bd_fill,
	.bd_send	= sec_bd_send,
	.callback	= sec_skcipher_callback,
	.process	= sec_process,
};

static const struct sec_req_op sec_aead_req_ops = {
	.buf_map	= sec_aead_sgl_map,
	.buf_unmap	= sec_aead_sgl_unmap,
	.do_transfer	= sec_aead_set_iv,
	.bd_fill	= sec_aead_bd_fill,
	.bd_send	= sec_bd_send,
	.callback	= sec_aead_callback,
	.process	= sec_process,
};

static const struct sec_req_op sec_skcipher_req_ops_v3 = {
	.buf_map	= sec_skcipher_sgl_map,
	.buf_unmap	= sec_skcipher_sgl_unmap,
	.do_transfer	= sec_skcipher_copy_iv,
	.bd_fill	= sec_skcipher_bd_fill_v3,
	.bd_send	= sec_bd_send,
	.callback	= sec_skcipher_callback,
	.process	= sec_process,
};

static const struct sec_req_op sec_aead_req_ops_v3 = {
	.buf_map	= sec_aead_sgl_map,
	.buf_unmap	= sec_aead_sgl_unmap,
	.do_transfer	= sec_aead_set_iv,
	.bd_fill	= sec_aead_bd_fill_v3,
	.bd_send	= sec_bd_send,
	.callback	= sec_aead_callback,
	.process	= sec_process,
};

static int sec_skcipher_ctx_init(struct crypto_skcipher *tfm)
{
	struct sec_ctx *ctx = crypto_skcipher_ctx(tfm);
	int ret;

	ret = sec_skcipher_init(tfm);
	if (ret)
		return ret;

	if (ctx->sec->qm.ver < QM_HW_V3) {
		ctx->type_supported = SEC_BD_TYPE2;
		ctx->req_op = &sec_skcipher_req_ops;
	} else {
		ctx->type_supported = SEC_BD_TYPE3;
		ctx->req_op = &sec_skcipher_req_ops_v3;
	}

	return ret;
}

static void sec_skcipher_ctx_exit(struct crypto_skcipher *tfm)
{
	sec_skcipher_uninit(tfm);
}

static int sec_aead_init(struct crypto_aead *tfm)
{
	struct sec_ctx *ctx = crypto_aead_ctx(tfm);
	int ret;

	crypto_aead_set_reqsize(tfm, sizeof(struct sec_req));
	ctx->alg_type = SEC_AEAD;
	ctx->c_ctx.ivsize = crypto_aead_ivsize(tfm);
	if (ctx->c_ctx.ivsize < SEC_AIV_SIZE ||
	    ctx->c_ctx.ivsize > SEC_IV_SIZE) {
		pr_err("get error aead iv size!\n");
		return -EINVAL;
	}

	ret = sec_ctx_base_init(ctx);
	if (ret)
		return ret;
	if (ctx->sec->qm.ver < QM_HW_V3) {
		ctx->type_supported = SEC_BD_TYPE2;
		ctx->req_op = &sec_aead_req_ops;
	} else {
		ctx->type_supported = SEC_BD_TYPE3;
		ctx->req_op = &sec_aead_req_ops_v3;
	}

	ret = sec_auth_init(ctx);
	if (ret)
		goto err_auth_init;

	ret = sec_cipher_init(ctx);
	if (ret)
		goto err_cipher_init;

	return ret;

err_cipher_init:
	sec_auth_uninit(ctx);
err_auth_init:
	sec_ctx_base_uninit(ctx);
	return ret;
}

static void sec_aead_exit(struct crypto_aead *tfm)
{
	struct sec_ctx *ctx = crypto_aead_ctx(tfm);

	sec_cipher_uninit(ctx);
	sec_auth_uninit(ctx);
	sec_ctx_base_uninit(ctx);
}

static int sec_aead_ctx_init(struct crypto_aead *tfm, const char *hash_name)
{
	struct sec_ctx *ctx = crypto_aead_ctx(tfm);
	struct sec_auth_ctx *auth_ctx = &ctx->a_ctx;
	int ret;

	ret = sec_aead_init(tfm);
	if (ret) {
		pr_err("hisi_sec2: aead init error!\n");
		return ret;
	}

	auth_ctx->hash_tfm = crypto_alloc_shash(hash_name, 0, 0);
	if (IS_ERR(auth_ctx->hash_tfm)) {
		dev_err(ctx->dev, "aead alloc shash error!\n");
		sec_aead_exit(tfm);
		return PTR_ERR(auth_ctx->hash_tfm);
	}

	return 0;
}

static void sec_aead_ctx_exit(struct crypto_aead *tfm)
{
	struct sec_ctx *ctx = crypto_aead_ctx(tfm);

	crypto_free_shash(ctx->a_ctx.hash_tfm);
	sec_aead_exit(tfm);
}

static int sec_aead_xcm_ctx_init(struct crypto_aead *tfm)
{
	struct aead_alg *alg = crypto_aead_alg(tfm);
	struct sec_ctx *ctx = crypto_aead_ctx(tfm);
	struct sec_auth_ctx *a_ctx = &ctx->a_ctx;
	const char *aead_name = alg->base.cra_name;
	int ret;

	ret = sec_aead_init(tfm);
	if (ret) {
		dev_err(ctx->dev, "hisi_sec2: aead xcm init error!\n");
		return ret;
	}

	a_ctx->fallback_aead_tfm = crypto_alloc_aead(aead_name, 0,
						     CRYPTO_ALG_NEED_FALLBACK |
						     CRYPTO_ALG_ASYNC);
	if (IS_ERR(a_ctx->fallback_aead_tfm)) {
		dev_err(ctx->dev, "aead driver alloc fallback tfm error!\n");
		sec_aead_exit(tfm);
		return PTR_ERR(a_ctx->fallback_aead_tfm);
	}
	a_ctx->fallback = false;

	return 0;
}

static void sec_aead_xcm_ctx_exit(struct crypto_aead *tfm)
{
	struct sec_ctx *ctx = crypto_aead_ctx(tfm);

	crypto_free_aead(ctx->a_ctx.fallback_aead_tfm);
	sec_aead_exit(tfm);
}

static int sec_aead_sha1_ctx_init(struct crypto_aead *tfm)
{
	return sec_aead_ctx_init(tfm, "sha1");
}

static int sec_aead_sha256_ctx_init(struct crypto_aead *tfm)
{
	return sec_aead_ctx_init(tfm, "sha256");
}

static int sec_aead_sha512_ctx_init(struct crypto_aead *tfm)
{
	return sec_aead_ctx_init(tfm, "sha512");
}

static int sec_skcipher_cryptlen_check(struct sec_ctx *ctx,
	struct sec_req *sreq)
{
	u32 cryptlen = sreq->c_req.sk_req->cryptlen;
	struct device *dev = ctx->dev;
	u8 c_mode = ctx->c_ctx.c_mode;
	int ret = 0;

	switch (c_mode) {
	case SEC_CMODE_XTS:
		if (unlikely(cryptlen < AES_BLOCK_SIZE)) {
			dev_err(dev, "skcipher XTS mode input length error!\n");
			ret = -EINVAL;
		}
		break;
	case SEC_CMODE_ECB:
	case SEC_CMODE_CBC:
		if (unlikely(cryptlen & (AES_BLOCK_SIZE - 1))) {
			dev_err(dev, "skcipher AES input length error!\n");
			ret = -EINVAL;
		}
		break;
	case SEC_CMODE_CFB:
	case SEC_CMODE_OFB:
	case SEC_CMODE_CTR:
		if (unlikely(ctx->sec->qm.ver < QM_HW_V3)) {
			dev_err(dev, "skcipher HW version error!\n");
			ret = -EINVAL;
		}
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static int sec_skcipher_param_check(struct sec_ctx *ctx, struct sec_req *sreq)
{
	struct skcipher_request *sk_req = sreq->c_req.sk_req;
	struct device *dev = ctx->dev;
	u8 c_alg = ctx->c_ctx.c_alg;

	if (unlikely(!sk_req->src || !sk_req->dst ||
		     sk_req->cryptlen > MAX_INPUT_DATA_LEN)) {
		dev_err(dev, "skcipher input param error!\n");
		return -EINVAL;
	}
	sreq->c_req.c_len = sk_req->cryptlen;

	if (ctx->pbuf_supported && sk_req->cryptlen <= SEC_PBUF_SZ)
		sreq->use_pbuf = true;
	else
		sreq->use_pbuf = false;

	if (c_alg == SEC_CALG_3DES) {
		if (unlikely(sk_req->cryptlen & (DES3_EDE_BLOCK_SIZE - 1))) {
			dev_err(dev, "skcipher 3des input length error!\n");
			return -EINVAL;
		}
		return 0;
	} else if (c_alg == SEC_CALG_AES || c_alg == SEC_CALG_SM4) {
		return sec_skcipher_cryptlen_check(ctx, sreq);
	}

	dev_err(dev, "skcipher algorithm error!\n");

	return -EINVAL;
}

static int sec_skcipher_soft_crypto(struct sec_ctx *ctx,
				    struct skcipher_request *sreq, bool encrypt)
{
	struct sec_cipher_ctx *c_ctx = &ctx->c_ctx;
	SYNC_SKCIPHER_REQUEST_ON_STACK(subreq, c_ctx->fbtfm);
	struct device *dev = ctx->dev;
	int ret;

	if (!c_ctx->fbtfm) {
		dev_err_ratelimited(dev, "the soft tfm isn't supported in the current system.\n");
		return -EINVAL;
	}

	skcipher_request_set_sync_tfm(subreq, c_ctx->fbtfm);

	/* software need sync mode to do crypto */
	skcipher_request_set_callback(subreq, sreq->base.flags,
				      NULL, NULL);
	skcipher_request_set_crypt(subreq, sreq->src, sreq->dst,
				   sreq->cryptlen, sreq->iv);
	if (encrypt)
		ret = crypto_skcipher_encrypt(subreq);
	else
		ret = crypto_skcipher_decrypt(subreq);

	skcipher_request_zero(subreq);

	return ret;
}

static int sec_skcipher_crypto(struct skcipher_request *sk_req, bool encrypt)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(sk_req);
	struct sec_req *req = skcipher_request_ctx(sk_req);
	struct sec_ctx *ctx = crypto_skcipher_ctx(tfm);
	int ret;

	if (!sk_req->cryptlen) {
		if (ctx->c_ctx.c_mode == SEC_CMODE_XTS)
			return -EINVAL;
		return 0;
	}

	req->flag = sk_req->base.flags;
	req->c_req.sk_req = sk_req;
	req->c_req.encrypt = encrypt;
	req->ctx = ctx;

	ret = sec_skcipher_param_check(ctx, req);
	if (unlikely(ret))
		return -EINVAL;

	if (unlikely(ctx->c_ctx.fallback))
		return sec_skcipher_soft_crypto(ctx, sk_req, encrypt);

	return ctx->req_op->process(ctx, req);
}

static int sec_skcipher_encrypt(struct skcipher_request *sk_req)
{
	return sec_skcipher_crypto(sk_req, true);
}

static int sec_skcipher_decrypt(struct skcipher_request *sk_req)
{
	return sec_skcipher_crypto(sk_req, false);
}

#define SEC_SKCIPHER_GEN_ALG(sec_cra_name, sec_set_key, sec_min_key_size, \
	sec_max_key_size, ctx_init, ctx_exit, blk_size, iv_size)\
{\
	.base = {\
		.cra_name = sec_cra_name,\
		.cra_driver_name = "hisi_sec_"sec_cra_name,\
		.cra_priority = SEC_PRIORITY,\
		.cra_flags = CRYPTO_ALG_ASYNC |\
		 CRYPTO_ALG_NEED_FALLBACK,\
		.cra_blocksize = blk_size,\
		.cra_ctxsize = sizeof(struct sec_ctx),\
		.cra_module = THIS_MODULE,\
	},\
	.init = ctx_init,\
	.exit = ctx_exit,\
	.setkey = sec_set_key,\
	.decrypt = sec_skcipher_decrypt,\
	.encrypt = sec_skcipher_encrypt,\
	.min_keysize = sec_min_key_size,\
	.max_keysize = sec_max_key_size,\
	.ivsize = iv_size,\
}

#define SEC_SKCIPHER_ALG(name, key_func, min_key_size, \
	max_key_size, blk_size, iv_size) \
	SEC_SKCIPHER_GEN_ALG(name, key_func, min_key_size, max_key_size, \
	sec_skcipher_ctx_init, sec_skcipher_ctx_exit, blk_size, iv_size)

static struct sec_skcipher sec_skciphers[] = {
	{
		.alg_msk = BIT(0),
		.alg = SEC_SKCIPHER_ALG("ecb(aes)", sec_setkey_aes_ecb, AES_MIN_KEY_SIZE,
					AES_MAX_KEY_SIZE, AES_BLOCK_SIZE, 0),
	},
	{
		.alg_msk = BIT(1),
		.alg = SEC_SKCIPHER_ALG("cbc(aes)", sec_setkey_aes_cbc, AES_MIN_KEY_SIZE,
					AES_MAX_KEY_SIZE, AES_BLOCK_SIZE, AES_BLOCK_SIZE),
	},
	{
		.alg_msk = BIT(2),
		.alg = SEC_SKCIPHER_ALG("ctr(aes)", sec_setkey_aes_ctr,	AES_MIN_KEY_SIZE,
					AES_MAX_KEY_SIZE, SEC_MIN_BLOCK_SZ, AES_BLOCK_SIZE),
	},
	{
		.alg_msk = BIT(3),
		.alg = SEC_SKCIPHER_ALG("xts(aes)", sec_setkey_aes_xts,	SEC_XTS_MIN_KEY_SIZE,
					SEC_XTS_MAX_KEY_SIZE, AES_BLOCK_SIZE, AES_BLOCK_SIZE),
	},
	{
		.alg_msk = BIT(4),
		.alg = SEC_SKCIPHER_ALG("ofb(aes)", sec_setkey_aes_ofb,	AES_MIN_KEY_SIZE,
					AES_MAX_KEY_SIZE, SEC_MIN_BLOCK_SZ, AES_BLOCK_SIZE),
	},
	{
		.alg_msk = BIT(5),
		.alg = SEC_SKCIPHER_ALG("cfb(aes)", sec_setkey_aes_cfb,	AES_MIN_KEY_SIZE,
					AES_MAX_KEY_SIZE, SEC_MIN_BLOCK_SZ, AES_BLOCK_SIZE),
	},
	{
		.alg_msk = BIT(12),
		.alg = SEC_SKCIPHER_ALG("cbc(sm4)", sec_setkey_sm4_cbc,	AES_MIN_KEY_SIZE,
					AES_MIN_KEY_SIZE, AES_BLOCK_SIZE, AES_BLOCK_SIZE),
	},
	{
		.alg_msk = BIT(13),
		.alg = SEC_SKCIPHER_ALG("ctr(sm4)", sec_setkey_sm4_ctr, AES_MIN_KEY_SIZE,
					AES_MIN_KEY_SIZE, SEC_MIN_BLOCK_SZ, AES_BLOCK_SIZE),
	},
	{
		.alg_msk = BIT(14),
		.alg = SEC_SKCIPHER_ALG("xts(sm4)", sec_setkey_sm4_xts,	SEC_XTS_MIN_KEY_SIZE,
					SEC_XTS_MIN_KEY_SIZE, AES_BLOCK_SIZE, AES_BLOCK_SIZE),
	},
	{
		.alg_msk = BIT(15),
		.alg = SEC_SKCIPHER_ALG("ofb(sm4)", sec_setkey_sm4_ofb,	AES_MIN_KEY_SIZE,
					AES_MIN_KEY_SIZE, SEC_MIN_BLOCK_SZ, AES_BLOCK_SIZE),
	},
	{
		.alg_msk = BIT(16),
		.alg = SEC_SKCIPHER_ALG("cfb(sm4)", sec_setkey_sm4_cfb,	AES_MIN_KEY_SIZE,
					AES_MIN_KEY_SIZE, SEC_MIN_BLOCK_SZ, AES_BLOCK_SIZE),
	},
	{
		.alg_msk = BIT(23),
		.alg = SEC_SKCIPHER_ALG("ecb(des3_ede)", sec_setkey_3des_ecb, SEC_DES3_3KEY_SIZE,
					SEC_DES3_3KEY_SIZE, DES3_EDE_BLOCK_SIZE, 0),
	},
	{
		.alg_msk = BIT(24),
		.alg = SEC_SKCIPHER_ALG("cbc(des3_ede)", sec_setkey_3des_cbc, SEC_DES3_3KEY_SIZE,
					SEC_DES3_3KEY_SIZE, DES3_EDE_BLOCK_SIZE,
					DES3_EDE_BLOCK_SIZE),
	},
};

static int aead_iv_demension_check(struct aead_request *aead_req)
{
	u8 cl;

	cl = aead_req->iv[0] + 1;
	if (cl < IV_CL_MIN || cl > IV_CL_MAX)
		return -EINVAL;

	if (cl < IV_CL_MID && aead_req->cryptlen >> (BYTE_BITS * cl))
		return -EOVERFLOW;

	return 0;
}

static int sec_aead_spec_check(struct sec_ctx *ctx, struct sec_req *sreq)
{
	struct aead_request *req = sreq->aead_req.aead_req;
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	size_t authsize = crypto_aead_authsize(tfm);
	u8 c_mode = ctx->c_ctx.c_mode;
	struct device *dev = ctx->dev;
	int ret;

	if (unlikely(req->cryptlen + req->assoclen > MAX_INPUT_DATA_LEN ||
	    req->assoclen > SEC_MAX_AAD_LEN)) {
		dev_err(dev, "aead input spec error!\n");
		return -EINVAL;
	}

	if (unlikely((c_mode == SEC_CMODE_GCM && authsize < DES_BLOCK_SIZE) ||
	   (c_mode == SEC_CMODE_CCM && (authsize < MIN_MAC_LEN ||
		authsize & MAC_LEN_MASK)))) {
		dev_err(dev, "aead input mac length error!\n");
		return -EINVAL;
	}

	if (c_mode == SEC_CMODE_CCM) {
		if (unlikely(req->assoclen > SEC_MAX_CCM_AAD_LEN)) {
			dev_err_ratelimited(dev, "CCM input aad parameter is too long!\n");
			return -EINVAL;
		}
		ret = aead_iv_demension_check(req);
		if (ret) {
			dev_err(dev, "aead input iv param error!\n");
			return ret;
		}
	}

	if (sreq->c_req.encrypt)
		sreq->c_req.c_len = req->cryptlen;
	else
		sreq->c_req.c_len = req->cryptlen - authsize;
	if (c_mode == SEC_CMODE_CBC) {
		if (unlikely(sreq->c_req.c_len & (AES_BLOCK_SIZE - 1))) {
			dev_err(dev, "aead crypto length error!\n");
			return -EINVAL;
		}
	}

	return 0;
}

static int sec_aead_param_check(struct sec_ctx *ctx, struct sec_req *sreq)
{
	struct aead_request *req = sreq->aead_req.aead_req;
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	size_t authsize = crypto_aead_authsize(tfm);
	struct device *dev = ctx->dev;
	u8 c_alg = ctx->c_ctx.c_alg;

	if (unlikely(!req->src || !req->dst)) {
		dev_err(dev, "aead input param error!\n");
		return -EINVAL;
	}

	if (ctx->sec->qm.ver == QM_HW_V2) {
		if (unlikely(!req->cryptlen || (!sreq->c_req.encrypt &&
		    req->cryptlen <= authsize))) {
			ctx->a_ctx.fallback = true;
			return -EINVAL;
		}
	}

	/* Support AES or SM4 */
	if (unlikely(c_alg != SEC_CALG_AES && c_alg != SEC_CALG_SM4)) {
		dev_err(dev, "aead crypto alg error!\n");
		return -EINVAL;
	}

	if (unlikely(sec_aead_spec_check(ctx, sreq)))
		return -EINVAL;

	if (ctx->pbuf_supported && (req->cryptlen + req->assoclen) <=
		SEC_PBUF_SZ)
		sreq->use_pbuf = true;
	else
		sreq->use_pbuf = false;

	return 0;
}

static int sec_aead_soft_crypto(struct sec_ctx *ctx,
				struct aead_request *aead_req,
				bool encrypt)
{
	struct sec_auth_ctx *a_ctx = &ctx->a_ctx;
	struct device *dev = ctx->dev;
	struct aead_request *subreq;
	int ret;

	/* Kunpeng920 aead mode not support input 0 size */
	if (!a_ctx->fallback_aead_tfm) {
		dev_err(dev, "aead fallback tfm is NULL!\n");
		return -EINVAL;
	}

	subreq = aead_request_alloc(a_ctx->fallback_aead_tfm, GFP_KERNEL);
	if (!subreq)
		return -ENOMEM;

	aead_request_set_tfm(subreq, a_ctx->fallback_aead_tfm);
	aead_request_set_callback(subreq, aead_req->base.flags,
				  aead_req->base.complete, aead_req->base.data);
	aead_request_set_crypt(subreq, aead_req->src, aead_req->dst,
			       aead_req->cryptlen, aead_req->iv);
	aead_request_set_ad(subreq, aead_req->assoclen);

	if (encrypt)
		ret = crypto_aead_encrypt(subreq);
	else
		ret = crypto_aead_decrypt(subreq);
	aead_request_free(subreq);

	return ret;
}

static int sec_aead_crypto(struct aead_request *a_req, bool encrypt)
{
	struct crypto_aead *tfm = crypto_aead_reqtfm(a_req);
	struct sec_req *req = aead_request_ctx(a_req);
	struct sec_ctx *ctx = crypto_aead_ctx(tfm);
	int ret;

	req->flag = a_req->base.flags;
	req->aead_req.aead_req = a_req;
	req->c_req.encrypt = encrypt;
	req->ctx = ctx;

	ret = sec_aead_param_check(ctx, req);
	if (unlikely(ret)) {
		if (ctx->a_ctx.fallback)
			return sec_aead_soft_crypto(ctx, a_req, encrypt);
		return -EINVAL;
	}

	return ctx->req_op->process(ctx, req);
}

static int sec_aead_encrypt(struct aead_request *a_req)
{
	return sec_aead_crypto(a_req, true);
}

static int sec_aead_decrypt(struct aead_request *a_req)
{
	return sec_aead_crypto(a_req, false);
}

#define SEC_AEAD_ALG(sec_cra_name, sec_set_key, ctx_init,\
			 ctx_exit, blk_size, iv_size, max_authsize)\
{\
	.base = {\
		.cra_name = sec_cra_name,\
		.cra_driver_name = "hisi_sec_"sec_cra_name,\
		.cra_priority = SEC_PRIORITY,\
		.cra_flags = CRYPTO_ALG_ASYNC |\
		 CRYPTO_ALG_NEED_FALLBACK,\
		.cra_blocksize = blk_size,\
		.cra_ctxsize = sizeof(struct sec_ctx),\
		.cra_module = THIS_MODULE,\
	},\
	.init = ctx_init,\
	.exit = ctx_exit,\
	.setkey = sec_set_key,\
	.setauthsize = sec_aead_setauthsize,\
	.decrypt = sec_aead_decrypt,\
	.encrypt = sec_aead_encrypt,\
	.ivsize = iv_size,\
	.maxauthsize = max_authsize,\
}

static struct sec_aead sec_aeads[] = {
	{
		.alg_msk = BIT(6),
		.alg = SEC_AEAD_ALG("ccm(aes)", sec_setkey_aes_ccm, sec_aead_xcm_ctx_init,
				    sec_aead_xcm_ctx_exit, SEC_MIN_BLOCK_SZ, AES_BLOCK_SIZE,
				    AES_BLOCK_SIZE),
	},
	{
		.alg_msk = BIT(7),
		.alg = SEC_AEAD_ALG("gcm(aes)", sec_setkey_aes_gcm, sec_aead_xcm_ctx_init,
				    sec_aead_xcm_ctx_exit, SEC_MIN_BLOCK_SZ, SEC_AIV_SIZE,
				    AES_BLOCK_SIZE),
	},
	{
		.alg_msk = BIT(17),
		.alg = SEC_AEAD_ALG("ccm(sm4)", sec_setkey_sm4_ccm, sec_aead_xcm_ctx_init,
				    sec_aead_xcm_ctx_exit, SEC_MIN_BLOCK_SZ, AES_BLOCK_SIZE,
				    AES_BLOCK_SIZE),
	},
	{
		.alg_msk = BIT(18),
		.alg = SEC_AEAD_ALG("gcm(sm4)", sec_setkey_sm4_gcm, sec_aead_xcm_ctx_init,
				    sec_aead_xcm_ctx_exit, SEC_MIN_BLOCK_SZ, SEC_AIV_SIZE,
				    AES_BLOCK_SIZE),
	},
	{
		.alg_msk = BIT(43),
		.alg = SEC_AEAD_ALG("authenc(hmac(sha1),cbc(aes))", sec_setkey_aes_cbc_sha1,
				    sec_aead_sha1_ctx_init, sec_aead_ctx_exit, AES_BLOCK_SIZE,
				    AES_BLOCK_SIZE, SHA1_DIGEST_SIZE),
	},
	{
		.alg_msk = BIT(44),
		.alg = SEC_AEAD_ALG("authenc(hmac(sha256),cbc(aes))", sec_setkey_aes_cbc_sha256,
				    sec_aead_sha256_ctx_init, sec_aead_ctx_exit, AES_BLOCK_SIZE,
				    AES_BLOCK_SIZE, SHA256_DIGEST_SIZE),
	},
	{
		.alg_msk = BIT(45),
		.alg = SEC_AEAD_ALG("authenc(hmac(sha512),cbc(aes))", sec_setkey_aes_cbc_sha512,
				    sec_aead_sha512_ctx_init, sec_aead_ctx_exit, AES_BLOCK_SIZE,
				    AES_BLOCK_SIZE, SHA512_DIGEST_SIZE),
	},
};

static void sec_unregister_skcipher(u64 alg_mask, int end)
{
	int i;

	for (i = 0; i < end; i++)
		if (sec_skciphers[i].alg_msk & alg_mask)
			crypto_unregister_skcipher(&sec_skciphers[i].alg);
}

static int sec_register_skcipher(u64 alg_mask)
{
	int i, ret, count;

	count = ARRAY_SIZE(sec_skciphers);

	for (i = 0; i < count; i++) {
		if (!(sec_skciphers[i].alg_msk & alg_mask))
			continue;

		ret = crypto_register_skcipher(&sec_skciphers[i].alg);
		if (ret)
			goto err;
	}

	return 0;

err:
	sec_unregister_skcipher(alg_mask, i);

	return ret;
}

static void sec_unregister_aead(u64 alg_mask, int end)
{
	int i;

	for (i = 0; i < end; i++)
		if (sec_aeads[i].alg_msk & alg_mask)
			crypto_unregister_aead(&sec_aeads[i].alg);
}

static int sec_register_aead(u64 alg_mask)
{
	int i, ret, count;

	count = ARRAY_SIZE(sec_aeads);

	for (i = 0; i < count; i++) {
		if (!(sec_aeads[i].alg_msk & alg_mask))
			continue;

		ret = crypto_register_aead(&sec_aeads[i].alg);
		if (ret)
			goto err;
	}

	return 0;

err:
	sec_unregister_aead(alg_mask, i);

	return ret;
}

int sec_register_to_crypto(struct hisi_qm *qm)
{
	u64 alg_mask = sec_get_alg_bitmap(qm, SEC_DRV_ALG_BITMAP_HIGH, SEC_DRV_ALG_BITMAP_LOW);
	int ret = 0;

	mutex_lock(&sec_algs_lock);
	if (sec_available_devs) {
		sec_available_devs++;
		goto unlock;
	}

	ret = sec_register_skcipher(alg_mask);
	if (ret)
		goto unlock;

	ret = sec_register_aead(alg_mask);
	if (ret)
		goto unreg_skcipher;

	sec_available_devs++;
	mutex_unlock(&sec_algs_lock);

	return 0;

unreg_skcipher:
	sec_unregister_skcipher(alg_mask, ARRAY_SIZE(sec_skciphers));
unlock:
	mutex_unlock(&sec_algs_lock);
	return ret;
}

void sec_unregister_from_crypto(struct hisi_qm *qm)
{
	u64 alg_mask = sec_get_alg_bitmap(qm, SEC_DRV_ALG_BITMAP_HIGH, SEC_DRV_ALG_BITMAP_LOW);

	mutex_lock(&sec_algs_lock);
	if (--sec_available_devs)
		goto unlock;

	sec_unregister_aead(alg_mask, ARRAY_SIZE(sec_aeads));
	sec_unregister_skcipher(alg_mask, ARRAY_SIZE(sec_skciphers));

unlock:
	mutex_unlock(&sec_algs_lock);
}
