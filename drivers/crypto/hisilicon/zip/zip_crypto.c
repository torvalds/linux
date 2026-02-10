// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 HiSilicon Limited. */
#include <crypto/internal/acompress.h>
#include <linux/bitfield.h>
#include <linux/bitmap.h>
#include <linux/dma-mapping.h>
#include <linux/scatterlist.h>
#include "zip.h"

/* hisi_zip_sqe dw3 */
#define HZIP_BD_STATUS_M			GENMASK(7, 0)
/* hisi_zip_sqe dw7 */
#define HZIP_IN_SGE_DATA_OFFSET_M		GENMASK(23, 0)
#define HZIP_SQE_TYPE_M				GENMASK(31, 28)
/* hisi_zip_sqe dw8 */
#define HZIP_OUT_SGE_DATA_OFFSET_M		GENMASK(23, 0)
/* hisi_zip_sqe dw9 */
#define HZIP_REQ_TYPE_M				GENMASK(7, 0)
#define HZIP_ALG_TYPE_DEFLATE			0x01
#define HZIP_ALG_TYPE_LZ4			0x04
#define HZIP_BUF_TYPE_M				GENMASK(11, 8)
#define HZIP_SGL				0x1
#define HZIP_WIN_SIZE_M				GENMASK(15, 12)
#define HZIP_16K_WINSZ				0x2

#define HZIP_ALG_PRIORITY			300
#define HZIP_SGL_SGE_NR				10

#define HZIP_ALG_DEFLATE			GENMASK(5, 4)
#define HZIP_ALG_LZ4				BIT(8)

static DEFINE_MUTEX(zip_algs_lock);
static unsigned int zip_available_devs;

enum hisi_zip_alg_type {
	HZIP_ALG_TYPE_COMP = 0,
	HZIP_ALG_TYPE_DECOMP = 1,
};

enum {
	HZIP_QPC_COMP,
	HZIP_QPC_DECOMP,
	HZIP_CTX_Q_NUM
};

#define GET_REQ_FROM_SQE(sqe)	((u64)(sqe)->dw26 | (u64)(sqe)->dw27 << 32)
#define COMP_NAME_TO_TYPE(alg_name)					\
	(!strcmp((alg_name), "deflate") ? HZIP_ALG_TYPE_DEFLATE :	\
	(!strcmp((alg_name), "lz4") ? HZIP_ALG_TYPE_LZ4 : 0))

struct hisi_zip_req {
	struct acomp_req *req;
	struct hisi_acc_hw_sgl *hw_src;
	struct hisi_acc_hw_sgl *hw_dst;
	dma_addr_t dma_src;
	dma_addr_t dma_dst;
	struct hisi_zip_qp_ctx *qp_ctx;
	u16 req_id;
};

struct hisi_zip_req_q {
	struct hisi_zip_req *q;
	unsigned long *req_bitmap;
	spinlock_t req_lock;
	u16 size;
};

struct hisi_zip_qp_ctx {
	struct hisi_qp *qp;
	struct hisi_zip_req_q req_q;
	struct hisi_acc_sgl_pool *sgl_pool;
	struct hisi_zip *zip_dev;
	struct hisi_zip_ctx *ctx;
	u8 req_type;
};

struct hisi_zip_sqe_ops {
	u8 sqe_type;
	void (*fill_addr)(struct hisi_zip_sqe *sqe, struct hisi_zip_req *req);
	void (*fill_buf_size)(struct hisi_zip_sqe *sqe, struct hisi_zip_req *req);
	void (*fill_buf_type)(struct hisi_zip_sqe *sqe, u8 buf_type);
	void (*fill_req_type)(struct hisi_zip_sqe *sqe, u8 req_type);
	void (*fill_win_size)(struct hisi_zip_sqe *sqe, u8 win_size);
	void (*fill_tag)(struct hisi_zip_sqe *sqe, struct hisi_zip_req *req);
	void (*fill_sqe_type)(struct hisi_zip_sqe *sqe, u8 sqe_type);
	u32 (*get_status)(struct hisi_zip_sqe *sqe);
	u32 (*get_dstlen)(struct hisi_zip_sqe *sqe);
};

struct hisi_zip_ctx {
	struct hisi_zip_qp_ctx qp_ctx[HZIP_CTX_Q_NUM];
	const struct hisi_zip_sqe_ops *ops;
	bool fallback;
};

static int sgl_sge_nr_set(const char *val, const struct kernel_param *kp)
{
	int ret;
	u16 n;

	if (!val)
		return -EINVAL;

	ret = kstrtou16(val, 10, &n);
	if (ret || n == 0 || n > HISI_ACC_SGL_SGE_NR_MAX)
		return -EINVAL;

	return param_set_ushort(val, kp);
}

static const struct kernel_param_ops sgl_sge_nr_ops = {
	.set = sgl_sge_nr_set,
	.get = param_get_ushort,
};

static u16 sgl_sge_nr = HZIP_SGL_SGE_NR;
module_param_cb(sgl_sge_nr, &sgl_sge_nr_ops, &sgl_sge_nr, 0444);
MODULE_PARM_DESC(sgl_sge_nr, "Number of sge in sgl(1-255)");

static int hisi_zip_fallback_do_work(struct acomp_req *acomp_req, bool is_decompress)
{
	ACOMP_FBREQ_ON_STACK(fbreq, acomp_req);
	int ret;

	if (!is_decompress)
		ret = crypto_acomp_compress(fbreq);
	else
		ret = crypto_acomp_decompress(fbreq);
	if (ret) {
		pr_err("failed to do fallback work, ret=%d\n", ret);
		return ret;
	}

	acomp_req->dlen = fbreq->dlen;
	return ret;
}

static struct hisi_zip_req *hisi_zip_create_req(struct hisi_zip_qp_ctx *qp_ctx,
						struct acomp_req *req)
{
	struct hisi_zip_req_q *req_q = &qp_ctx->req_q;
	struct hisi_zip_req *q = req_q->q;
	struct hisi_zip_req *req_cache;
	int req_id;

	spin_lock(&req_q->req_lock);

	req_id = find_first_zero_bit(req_q->req_bitmap, req_q->size);
	if (req_id >= req_q->size) {
		spin_unlock(&req_q->req_lock);
		dev_dbg(&qp_ctx->qp->qm->pdev->dev, "req cache is full!\n");
		return ERR_PTR(-EAGAIN);
	}
	set_bit(req_id, req_q->req_bitmap);

	spin_unlock(&req_q->req_lock);

	req_cache = q + req_id;
	req_cache->req_id = req_id;
	req_cache->req = req;
	req_cache->qp_ctx = qp_ctx;

	return req_cache;
}

static void hisi_zip_remove_req(struct hisi_zip_qp_ctx *qp_ctx,
				struct hisi_zip_req *req)
{
	struct hisi_zip_req_q *req_q = &qp_ctx->req_q;

	spin_lock(&req_q->req_lock);
	clear_bit(req->req_id, req_q->req_bitmap);
	spin_unlock(&req_q->req_lock);
}

static void hisi_zip_fill_addr(struct hisi_zip_sqe *sqe, struct hisi_zip_req *req)
{
	sqe->source_addr_l = lower_32_bits(req->dma_src);
	sqe->source_addr_h = upper_32_bits(req->dma_src);
	sqe->dest_addr_l = lower_32_bits(req->dma_dst);
	sqe->dest_addr_h = upper_32_bits(req->dma_dst);
}

static void hisi_zip_fill_buf_size(struct hisi_zip_sqe *sqe, struct hisi_zip_req *req)
{
	struct acomp_req *a_req = req->req;

	sqe->input_data_length = a_req->slen;
	sqe->dest_avail_out = a_req->dlen;
}

static void hisi_zip_fill_buf_type(struct hisi_zip_sqe *sqe, u8 buf_type)
{
	u32 val;

	val = sqe->dw9 & ~HZIP_BUF_TYPE_M;
	val |= FIELD_PREP(HZIP_BUF_TYPE_M, buf_type);
	sqe->dw9 = val;
}

static void hisi_zip_fill_req_type(struct hisi_zip_sqe *sqe, u8 req_type)
{
	u32 val;

	val = sqe->dw9 & ~HZIP_REQ_TYPE_M;
	val |= FIELD_PREP(HZIP_REQ_TYPE_M, req_type);
	sqe->dw9 = val;
}

static void hisi_zip_fill_win_size(struct hisi_zip_sqe *sqe, u8 win_size)
{
	u32 val;

	val = sqe->dw9 & ~HZIP_WIN_SIZE_M;
	val |= FIELD_PREP(HZIP_WIN_SIZE_M, win_size);
	sqe->dw9 = val;
}

static void hisi_zip_fill_tag(struct hisi_zip_sqe *sqe, struct hisi_zip_req *req)
{
	sqe->dw26 = lower_32_bits((u64)req);
	sqe->dw27 = upper_32_bits((u64)req);
}

static void hisi_zip_fill_sqe_type(struct hisi_zip_sqe *sqe, u8 sqe_type)
{
	u32 val;

	val = sqe->dw7 & ~HZIP_SQE_TYPE_M;
	val |= FIELD_PREP(HZIP_SQE_TYPE_M, sqe_type);
	sqe->dw7 = val;
}

static void hisi_zip_fill_sqe(struct hisi_zip_ctx *ctx, struct hisi_zip_sqe *sqe,
			      u8 req_type, struct hisi_zip_req *req)
{
	const struct hisi_zip_sqe_ops *ops = ctx->ops;

	memset(sqe, 0, sizeof(struct hisi_zip_sqe));

	ops->fill_addr(sqe, req);
	ops->fill_buf_size(sqe, req);
	ops->fill_buf_type(sqe, HZIP_SGL);
	ops->fill_req_type(sqe, req_type);
	ops->fill_win_size(sqe, HZIP_16K_WINSZ);
	ops->fill_tag(sqe, req);
	ops->fill_sqe_type(sqe, ops->sqe_type);
}

static int hisi_zip_do_work(struct hisi_zip_qp_ctx *qp_ctx,
			    struct hisi_zip_req *req)
{
	struct hisi_acc_sgl_pool *pool = qp_ctx->sgl_pool;
	struct hisi_zip_dfx *dfx = &qp_ctx->zip_dev->dfx;
	struct acomp_req *a_req = req->req;
	struct hisi_qp *qp = qp_ctx->qp;
	struct device *dev = &qp->qm->pdev->dev;
	struct hisi_zip_sqe zip_sqe;
	int ret;

	if (unlikely(!a_req->src || !a_req->slen || !a_req->dst || !a_req->dlen))
		return -EINVAL;

	req->hw_src = hisi_acc_sg_buf_map_to_hw_sgl(dev, a_req->src, pool,
						    req->req_id << 1, &req->dma_src,
						    DMA_TO_DEVICE);
	if (IS_ERR(req->hw_src)) {
		dev_err(dev, "failed to map the src buffer to hw sgl (%ld)!\n",
			PTR_ERR(req->hw_src));
		return PTR_ERR(req->hw_src);
	}

	req->hw_dst = hisi_acc_sg_buf_map_to_hw_sgl(dev, a_req->dst, pool,
						    (req->req_id << 1) + 1,
						    &req->dma_dst, DMA_FROM_DEVICE);
	if (IS_ERR(req->hw_dst)) {
		ret = PTR_ERR(req->hw_dst);
		dev_err(dev, "failed to map the dst buffer to hw sgl (%d)!\n",
			ret);
		goto err_unmap_input;
	}

	hisi_zip_fill_sqe(qp_ctx->ctx, &zip_sqe, qp_ctx->req_type, req);

	/* send command to start a task */
	atomic64_inc(&dfx->send_cnt);
	ret = hisi_qp_send(qp, &zip_sqe);
	if (unlikely(ret < 0)) {
		atomic64_inc(&dfx->send_busy_cnt);
		ret = -EAGAIN;
		dev_dbg_ratelimited(dev, "failed to send request!\n");
		goto err_unmap_output;
	}

	return -EINPROGRESS;

err_unmap_output:
	hisi_acc_sg_buf_unmap(dev, a_req->dst, req->hw_dst, DMA_FROM_DEVICE);
err_unmap_input:
	hisi_acc_sg_buf_unmap(dev, a_req->src, req->hw_src, DMA_TO_DEVICE);
	return ret;
}

static u32 hisi_zip_get_status(struct hisi_zip_sqe *sqe)
{
	return sqe->dw3 & HZIP_BD_STATUS_M;
}

static u32 hisi_zip_get_dstlen(struct hisi_zip_sqe *sqe)
{
	return sqe->produced;
}

static void hisi_zip_acomp_cb(struct hisi_qp *qp, void *data)
{
	struct hisi_zip_sqe *sqe = data;
	struct hisi_zip_req *req = (struct hisi_zip_req *)GET_REQ_FROM_SQE(sqe);
	struct hisi_zip_qp_ctx *qp_ctx = req->qp_ctx;
	const struct hisi_zip_sqe_ops *ops = qp_ctx->ctx->ops;
	struct hisi_zip_dfx *dfx = &qp_ctx->zip_dev->dfx;
	struct device *dev = &qp->qm->pdev->dev;
	struct acomp_req *acomp_req = req->req;
	int err = 0;
	u32 status;

	atomic64_inc(&dfx->recv_cnt);
	status = ops->get_status(sqe);
	if (unlikely(status != 0 && status != HZIP_NC_ERR)) {
		dev_err(dev, "%scompress fail in qp%u: %u, output: %u\n",
			(qp->alg_type == 0) ? "" : "de", qp->qp_id, status,
			sqe->produced);
		atomic64_inc(&dfx->err_bd_cnt);
		err = -EIO;
	}

	hisi_acc_sg_buf_unmap(dev, acomp_req->dst, req->hw_dst, DMA_FROM_DEVICE);
	hisi_acc_sg_buf_unmap(dev, acomp_req->src, req->hw_src, DMA_TO_DEVICE);

	acomp_req->dlen = ops->get_dstlen(sqe);

	if (acomp_req->base.complete)
		acomp_request_complete(acomp_req, err);

	hisi_zip_remove_req(qp_ctx, req);
}

static int hisi_zip_acompress(struct acomp_req *acomp_req)
{
	struct hisi_zip_ctx *ctx = crypto_tfm_ctx(acomp_req->base.tfm);
	struct hisi_zip_qp_ctx *qp_ctx = &ctx->qp_ctx[HZIP_QPC_COMP];
	struct hisi_zip_req *req;
	struct device *dev;
	int ret;

	if (ctx->fallback)
		return hisi_zip_fallback_do_work(acomp_req, 0);

	dev = &qp_ctx->qp->qm->pdev->dev;

	req = hisi_zip_create_req(qp_ctx, acomp_req);
	if (IS_ERR(req))
		return PTR_ERR(req);

	ret = hisi_zip_do_work(qp_ctx, req);
	if (unlikely(ret != -EINPROGRESS)) {
		dev_info_ratelimited(dev, "failed to do compress (%d)!\n", ret);
		hisi_zip_remove_req(qp_ctx, req);
	}

	return ret;
}

static int hisi_zip_adecompress(struct acomp_req *acomp_req)
{
	struct hisi_zip_ctx *ctx = crypto_tfm_ctx(acomp_req->base.tfm);
	struct hisi_zip_qp_ctx *qp_ctx = &ctx->qp_ctx[HZIP_QPC_DECOMP];
	struct hisi_zip_req *req;
	struct device *dev;
	int ret;

	if (ctx->fallback)
		return hisi_zip_fallback_do_work(acomp_req, 1);

	dev = &qp_ctx->qp->qm->pdev->dev;

	req = hisi_zip_create_req(qp_ctx, acomp_req);
	if (IS_ERR(req))
		return PTR_ERR(req);

	ret = hisi_zip_do_work(qp_ctx, req);
	if (unlikely(ret != -EINPROGRESS)) {
		dev_info_ratelimited(dev, "failed to do decompress (%d)!\n",
				     ret);
		hisi_zip_remove_req(qp_ctx, req);
	}

	return ret;
}

static int hisi_zip_decompress(struct acomp_req *acomp_req)
{
	return hisi_zip_fallback_do_work(acomp_req, 1);
}

static const struct hisi_zip_sqe_ops hisi_zip_ops = {
	.sqe_type		= 0x3,
	.fill_addr		= hisi_zip_fill_addr,
	.fill_buf_size		= hisi_zip_fill_buf_size,
	.fill_buf_type		= hisi_zip_fill_buf_type,
	.fill_req_type		= hisi_zip_fill_req_type,
	.fill_win_size		= hisi_zip_fill_win_size,
	.fill_tag		= hisi_zip_fill_tag,
	.fill_sqe_type		= hisi_zip_fill_sqe_type,
	.get_status		= hisi_zip_get_status,
	.get_dstlen		= hisi_zip_get_dstlen,
};

static int hisi_zip_ctx_init(struct hisi_zip_ctx *hisi_zip_ctx, u8 req_type, int node)
{
	struct hisi_qp *qps[HZIP_CTX_Q_NUM] = { NULL };
	struct hisi_zip_qp_ctx *qp_ctx;
	u8 alg_type[HZIP_CTX_Q_NUM];
	struct hisi_zip *hisi_zip;
	int ret, i;

	/* alg_type = 0 for compress, 1 for decompress in hw sqe */
	for (i = 0; i < HZIP_CTX_Q_NUM; i++)
		alg_type[i] = i;

	ret = zip_create_qps(qps, HZIP_CTX_Q_NUM, node, alg_type);
	if (ret) {
		pr_err("failed to create zip qps (%d)!\n", ret);
		return -ENODEV;
	}

	hisi_zip = container_of(qps[0]->qm, struct hisi_zip, qm);

	for (i = 0; i < HZIP_CTX_Q_NUM; i++) {
		qp_ctx = &hisi_zip_ctx->qp_ctx[i];
		qp_ctx->ctx = hisi_zip_ctx;
		qp_ctx->zip_dev = hisi_zip;
		qp_ctx->req_type = req_type;
		qp_ctx->qp = qps[i];
	}

	hisi_zip_ctx->ops = &hisi_zip_ops;

	return 0;
}

static void hisi_zip_ctx_exit(struct hisi_zip_ctx *hisi_zip_ctx)
{
	struct hisi_qp *qps[HZIP_CTX_Q_NUM] = { NULL };
	int i;

	for (i = 0; i < HZIP_CTX_Q_NUM; i++)
		qps[i] = hisi_zip_ctx->qp_ctx[i].qp;

	hisi_qm_free_qps(qps, HZIP_CTX_Q_NUM);
}

static int hisi_zip_create_req_q(struct hisi_zip_ctx *ctx)
{
	u16 q_depth = ctx->qp_ctx[0].qp->sq_depth;
	struct hisi_zip_req_q *req_q;
	int i, ret;

	for (i = 0; i < HZIP_CTX_Q_NUM; i++) {
		req_q = &ctx->qp_ctx[i].req_q;
		req_q->size = q_depth;

		req_q->req_bitmap = bitmap_zalloc(req_q->size, GFP_KERNEL);
		if (!req_q->req_bitmap) {
			ret = -ENOMEM;
			if (i == 0)
				return ret;

			goto err_free_comp_q;
		}
		spin_lock_init(&req_q->req_lock);

		req_q->q = kcalloc(req_q->size, sizeof(struct hisi_zip_req),
				   GFP_KERNEL);
		if (!req_q->q) {
			ret = -ENOMEM;
			if (i == 0)
				goto err_free_comp_bitmap;
			else
				goto err_free_decomp_bitmap;
		}
	}

	return 0;

err_free_decomp_bitmap:
	bitmap_free(ctx->qp_ctx[HZIP_QPC_DECOMP].req_q.req_bitmap);
err_free_comp_q:
	kfree(ctx->qp_ctx[HZIP_QPC_COMP].req_q.q);
err_free_comp_bitmap:
	bitmap_free(ctx->qp_ctx[HZIP_QPC_COMP].req_q.req_bitmap);
	return ret;
}

static void hisi_zip_release_req_q(struct hisi_zip_ctx *ctx)
{
	int i;

	for (i = 0; i < HZIP_CTX_Q_NUM; i++) {
		kfree(ctx->qp_ctx[i].req_q.q);
		bitmap_free(ctx->qp_ctx[i].req_q.req_bitmap);
	}
}

static int hisi_zip_create_sgl_pool(struct hisi_zip_ctx *ctx)
{
	u16 q_depth = ctx->qp_ctx[0].qp->sq_depth;
	struct hisi_zip_qp_ctx *tmp;
	struct device *dev;
	int i;

	for (i = 0; i < HZIP_CTX_Q_NUM; i++) {
		tmp = &ctx->qp_ctx[i];
		dev = &tmp->qp->qm->pdev->dev;
		tmp->sgl_pool = hisi_acc_create_sgl_pool(dev, q_depth << 1,
							 sgl_sge_nr);
		if (IS_ERR(tmp->sgl_pool)) {
			if (i == 1)
				goto err_free_sgl_pool0;
			return -ENOMEM;
		}
	}

	return 0;

err_free_sgl_pool0:
	hisi_acc_free_sgl_pool(&ctx->qp_ctx[HZIP_QPC_COMP].qp->qm->pdev->dev,
			       ctx->qp_ctx[HZIP_QPC_COMP].sgl_pool);
	return -ENOMEM;
}

static void hisi_zip_release_sgl_pool(struct hisi_zip_ctx *ctx)
{
	int i;

	for (i = 0; i < HZIP_CTX_Q_NUM; i++)
		hisi_acc_free_sgl_pool(&ctx->qp_ctx[i].qp->qm->pdev->dev,
				       ctx->qp_ctx[i].sgl_pool);
}

static void hisi_zip_set_acomp_cb(struct hisi_zip_ctx *ctx,
				  void (*fn)(struct hisi_qp *, void *))
{
	int i;

	for (i = 0; i < HZIP_CTX_Q_NUM; i++)
		ctx->qp_ctx[i].qp->req_cb = fn;
}

static int hisi_zip_acomp_init(struct crypto_acomp *tfm)
{
	const char *alg_name = crypto_tfm_alg_name(&tfm->base);
	struct hisi_zip_ctx *ctx = crypto_tfm_ctx(&tfm->base);
	struct device *dev;
	int ret;

	ret = hisi_zip_ctx_init(ctx, COMP_NAME_TO_TYPE(alg_name), tfm->base.node);
	if (ret) {
		pr_err("failed to init ctx (%d)!\n", ret);
		goto switch_to_soft;
	}

	dev = &ctx->qp_ctx[0].qp->qm->pdev->dev;

	ret = hisi_zip_create_req_q(ctx);
	if (ret) {
		dev_err(dev, "failed to create request queue (%d)!\n", ret);
		goto err_ctx_exit;
	}

	ret = hisi_zip_create_sgl_pool(ctx);
	if (ret) {
		dev_err(dev, "failed to create sgl pool (%d)!\n", ret);
		goto err_release_req_q;
	}

	hisi_zip_set_acomp_cb(ctx, hisi_zip_acomp_cb);

	return 0;

err_release_req_q:
	hisi_zip_release_req_q(ctx);
err_ctx_exit:
	hisi_zip_ctx_exit(ctx);
switch_to_soft:
	ctx->fallback = true;
	return 0;
}

static void hisi_zip_acomp_exit(struct crypto_acomp *tfm)
{
	struct hisi_zip_ctx *ctx = crypto_tfm_ctx(&tfm->base);

	if (ctx->fallback)
		return;

	hisi_zip_release_sgl_pool(ctx);
	hisi_zip_release_req_q(ctx);
	hisi_zip_ctx_exit(ctx);
}

static struct acomp_alg hisi_zip_acomp_deflate = {
	.init			= hisi_zip_acomp_init,
	.exit			= hisi_zip_acomp_exit,
	.compress		= hisi_zip_acompress,
	.decompress		= hisi_zip_adecompress,
	.base			= {
		.cra_name		= "deflate",
		.cra_driver_name	= "hisi-deflate-acomp",
		.cra_flags		= CRYPTO_ALG_ASYNC |
					  CRYPTO_ALG_NEED_FALLBACK,
		.cra_module		= THIS_MODULE,
		.cra_priority		= HZIP_ALG_PRIORITY,
		.cra_ctxsize		= sizeof(struct hisi_zip_ctx),
	}
};

static int hisi_zip_register_deflate(struct hisi_qm *qm)
{
	int ret;

	if (!hisi_zip_alg_support(qm, HZIP_ALG_DEFLATE))
		return 0;

	ret = crypto_register_acomp(&hisi_zip_acomp_deflate);
	if (ret)
		dev_err(&qm->pdev->dev, "failed to register to deflate (%d)!\n", ret);

	return ret;
}

static void hisi_zip_unregister_deflate(struct hisi_qm *qm)
{
	if (!hisi_zip_alg_support(qm, HZIP_ALG_DEFLATE))
		return;

	crypto_unregister_acomp(&hisi_zip_acomp_deflate);
}

static struct acomp_alg hisi_zip_acomp_lz4 = {
	.init			= hisi_zip_acomp_init,
	.exit			= hisi_zip_acomp_exit,
	.compress		= hisi_zip_acompress,
	.decompress		= hisi_zip_decompress,
	.base			= {
		.cra_name		= "lz4",
		.cra_driver_name	= "hisi-lz4-acomp",
		.cra_flags		= CRYPTO_ALG_ASYNC |
					  CRYPTO_ALG_NEED_FALLBACK,
		.cra_module		= THIS_MODULE,
		.cra_priority		= HZIP_ALG_PRIORITY,
		.cra_ctxsize		= sizeof(struct hisi_zip_ctx),
	}
};

static int hisi_zip_register_lz4(struct hisi_qm *qm)
{
	int ret;

	if (!hisi_zip_alg_support(qm, HZIP_ALG_LZ4))
		return 0;

	ret = crypto_register_acomp(&hisi_zip_acomp_lz4);
	if (ret)
		dev_err(&qm->pdev->dev, "failed to register to LZ4 (%d)!\n", ret);

	return ret;
}

static void hisi_zip_unregister_lz4(struct hisi_qm *qm)
{
	if (!hisi_zip_alg_support(qm, HZIP_ALG_LZ4))
		return;

	crypto_unregister_acomp(&hisi_zip_acomp_lz4);
}

int hisi_zip_register_to_crypto(struct hisi_qm *qm)
{
	int ret = 0;

	mutex_lock(&zip_algs_lock);
	if (zip_available_devs) {
		zip_available_devs++;
		goto unlock;
	}

	ret = hisi_zip_register_deflate(qm);
	if (ret)
		goto unlock;

	ret = hisi_zip_register_lz4(qm);
	if (ret)
		goto unreg_deflate;

	zip_available_devs++;
	mutex_unlock(&zip_algs_lock);

	return 0;

unreg_deflate:
	hisi_zip_unregister_deflate(qm);
unlock:
	mutex_unlock(&zip_algs_lock);
	return ret;
}

void hisi_zip_unregister_from_crypto(struct hisi_qm *qm)
{
	mutex_lock(&zip_algs_lock);
	if (--zip_available_devs)
		goto unlock;

	hisi_zip_unregister_deflate(qm);
	hisi_zip_unregister_lz4(qm);

unlock:
	mutex_unlock(&zip_algs_lock);
}
