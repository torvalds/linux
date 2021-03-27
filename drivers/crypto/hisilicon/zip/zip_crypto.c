// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 HiSilicon Limited. */
#include <crypto/internal/acompress.h>
#include <linux/bitfield.h>
#include <linux/dma-mapping.h>
#include <linux/scatterlist.h>
#include "zip.h"

/* hisi_zip_sqe dw3 */
#define HZIP_BD_STATUS_M			GENMASK(7, 0)
/* hisi_zip_sqe dw7 */
#define HZIP_IN_SGE_DATA_OFFSET_M		GENMASK(23, 0)
/* hisi_zip_sqe dw8 */
#define HZIP_OUT_SGE_DATA_OFFSET_M		GENMASK(23, 0)
/* hisi_zip_sqe dw9 */
#define HZIP_REQ_TYPE_M				GENMASK(7, 0)
#define HZIP_ALG_TYPE_ZLIB			0x02
#define HZIP_ALG_TYPE_GZIP			0x03
#define HZIP_BUF_TYPE_M				GENMASK(11, 8)
#define HZIP_PBUFFER				0x0
#define HZIP_SGL				0x1

#define HZIP_ZLIB_HEAD_SIZE			2
#define HZIP_GZIP_HEAD_SIZE			10

#define GZIP_HEAD_FHCRC_BIT			BIT(1)
#define GZIP_HEAD_FEXTRA_BIT			BIT(2)
#define GZIP_HEAD_FNAME_BIT			BIT(3)
#define GZIP_HEAD_FCOMMENT_BIT			BIT(4)

#define GZIP_HEAD_FLG_SHIFT			3
#define GZIP_HEAD_FEXTRA_SHIFT			10
#define GZIP_HEAD_FEXTRA_XLEN			2UL
#define GZIP_HEAD_FHCRC_SIZE			2

#define HZIP_GZIP_HEAD_BUF			256
#define HZIP_ALG_PRIORITY			300
#define HZIP_SGL_SGE_NR				10

static const u8 zlib_head[HZIP_ZLIB_HEAD_SIZE] = {0x78, 0x9c};
static const u8 gzip_head[HZIP_GZIP_HEAD_SIZE] = {
	0x1f, 0x8b, 0x08, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x03
};

enum hisi_zip_alg_type {
	HZIP_ALG_TYPE_COMP = 0,
	HZIP_ALG_TYPE_DECOMP = 1,
};

enum {
	HZIP_QPC_COMP,
	HZIP_QPC_DECOMP,
	HZIP_CTX_Q_NUM
};

#define COMP_NAME_TO_TYPE(alg_name)					\
	(!strcmp((alg_name), "zlib-deflate") ? HZIP_ALG_TYPE_ZLIB :	\
	 !strcmp((alg_name), "gzip") ? HZIP_ALG_TYPE_GZIP : 0)		\

#define TO_HEAD_SIZE(req_type)						\
	(((req_type) == HZIP_ALG_TYPE_ZLIB) ? sizeof(zlib_head) :	\
	 ((req_type) == HZIP_ALG_TYPE_GZIP) ? sizeof(gzip_head) : 0)	\

#define TO_HEAD(req_type)						\
	(((req_type) == HZIP_ALG_TYPE_ZLIB) ? zlib_head :		\
	 ((req_type) == HZIP_ALG_TYPE_GZIP) ? gzip_head : NULL)		\

struct hisi_zip_req {
	struct acomp_req *req;
	u32 sskip;
	u32 dskip;
	struct hisi_acc_hw_sgl *hw_src;
	struct hisi_acc_hw_sgl *hw_dst;
	dma_addr_t dma_src;
	dma_addr_t dma_dst;
	u16 req_id;
};

struct hisi_zip_req_q {
	struct hisi_zip_req *q;
	unsigned long *req_bitmap;
	rwlock_t req_lock;
	u16 size;
};

struct hisi_zip_qp_ctx {
	struct hisi_qp *qp;
	struct hisi_zip_req_q req_q;
	struct hisi_acc_sgl_pool *sgl_pool;
	struct hisi_zip *zip_dev;
	struct hisi_zip_ctx *ctx;
};

struct hisi_zip_ctx {
	struct hisi_zip_qp_ctx qp_ctx[HZIP_CTX_Q_NUM];
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

	return param_set_int(val, kp);
}

static const struct kernel_param_ops sgl_sge_nr_ops = {
	.set = sgl_sge_nr_set,
	.get = param_get_int,
};

static u16 sgl_sge_nr = HZIP_SGL_SGE_NR;
module_param_cb(sgl_sge_nr, &sgl_sge_nr_ops, &sgl_sge_nr, 0444);
MODULE_PARM_DESC(sgl_sge_nr, "Number of sge in sgl(1-255)");

static u16 get_extra_field_size(const u8 *start)
{
	return *((u16 *)start) + GZIP_HEAD_FEXTRA_XLEN;
}

static u32 get_name_field_size(const u8 *start)
{
	return strlen(start) + 1;
}

static u32 get_comment_field_size(const u8 *start)
{
	return strlen(start) + 1;
}

static u32 __get_gzip_head_size(const u8 *src)
{
	u8 head_flg = *(src + GZIP_HEAD_FLG_SHIFT);
	u32 size = GZIP_HEAD_FEXTRA_SHIFT;

	if (head_flg & GZIP_HEAD_FEXTRA_BIT)
		size += get_extra_field_size(src + size);
	if (head_flg & GZIP_HEAD_FNAME_BIT)
		size += get_name_field_size(src + size);
	if (head_flg & GZIP_HEAD_FCOMMENT_BIT)
		size += get_comment_field_size(src + size);
	if (head_flg & GZIP_HEAD_FHCRC_BIT)
		size += GZIP_HEAD_FHCRC_SIZE;

	return size;
}

static size_t __maybe_unused get_gzip_head_size(struct scatterlist *sgl)
{
	char buf[HZIP_GZIP_HEAD_BUF];

	sg_copy_to_buffer(sgl, sg_nents(sgl), buf, sizeof(buf));

	return __get_gzip_head_size(buf);
}

static int add_comp_head(struct scatterlist *dst, u8 req_type)
{
	int head_size = TO_HEAD_SIZE(req_type);
	const u8 *head = TO_HEAD(req_type);
	int ret;

	ret = sg_copy_from_buffer(dst, sg_nents(dst), head, head_size);
	if (ret != head_size) {
		pr_err("the head size of buffer is wrong (%d)!\n", ret);
		return -ENOMEM;
	}

	return head_size;
}

static int get_comp_head_size(struct acomp_req *acomp_req, u8 req_type)
{
	if (!acomp_req->src || !acomp_req->slen)
		return -EINVAL;

	if (req_type == HZIP_ALG_TYPE_GZIP &&
	    acomp_req->slen < GZIP_HEAD_FEXTRA_SHIFT)
		return -EINVAL;

	switch (req_type) {
	case HZIP_ALG_TYPE_ZLIB:
		return TO_HEAD_SIZE(HZIP_ALG_TYPE_ZLIB);
	case HZIP_ALG_TYPE_GZIP:
		return TO_HEAD_SIZE(HZIP_ALG_TYPE_GZIP);
	default:
		pr_err("request type does not support!\n");
		return -EINVAL;
	}
}

static struct hisi_zip_req *hisi_zip_create_req(struct acomp_req *req,
						struct hisi_zip_qp_ctx *qp_ctx,
						size_t head_size, bool is_comp)
{
	struct hisi_zip_req_q *req_q = &qp_ctx->req_q;
	struct hisi_zip_req *q = req_q->q;
	struct hisi_zip_req *req_cache;
	int req_id;

	write_lock(&req_q->req_lock);

	req_id = find_first_zero_bit(req_q->req_bitmap, req_q->size);
	if (req_id >= req_q->size) {
		write_unlock(&req_q->req_lock);
		dev_dbg(&qp_ctx->qp->qm->pdev->dev, "req cache is full!\n");
		return ERR_PTR(-EAGAIN);
	}
	set_bit(req_id, req_q->req_bitmap);

	req_cache = q + req_id;
	req_cache->req_id = req_id;
	req_cache->req = req;

	if (is_comp) {
		req_cache->sskip = 0;
		req_cache->dskip = head_size;
	} else {
		req_cache->sskip = head_size;
		req_cache->dskip = 0;
	}

	write_unlock(&req_q->req_lock);

	return req_cache;
}

static void hisi_zip_remove_req(struct hisi_zip_qp_ctx *qp_ctx,
				struct hisi_zip_req *req)
{
	struct hisi_zip_req_q *req_q = &qp_ctx->req_q;

	write_lock(&req_q->req_lock);
	clear_bit(req->req_id, req_q->req_bitmap);
	memset(req, 0, sizeof(struct hisi_zip_req));
	write_unlock(&req_q->req_lock);
}

static void hisi_zip_config_buf_type(struct hisi_zip_sqe *sqe, u8 buf_type)
{
	u32 val;

	val = (sqe->dw9) & ~HZIP_BUF_TYPE_M;
	val |= FIELD_PREP(HZIP_BUF_TYPE_M, buf_type);
	sqe->dw9 = val;
}

static void hisi_zip_config_tag(struct hisi_zip_sqe *sqe, u32 tag)
{
	sqe->tag = tag;
}

static void hisi_zip_fill_sqe(struct hisi_zip_sqe *sqe, u8 req_type,
			      dma_addr_t s_addr, dma_addr_t d_addr, u32 slen,
			      u32 dlen, u32 sskip, u32 dskip)
{
	memset(sqe, 0, sizeof(struct hisi_zip_sqe));

	sqe->input_data_length = slen - sskip;
	sqe->dw7 = FIELD_PREP(HZIP_IN_SGE_DATA_OFFSET_M, sskip);
	sqe->dw8 = FIELD_PREP(HZIP_OUT_SGE_DATA_OFFSET_M, dskip);
	sqe->dw9 = FIELD_PREP(HZIP_REQ_TYPE_M, req_type);
	sqe->dest_avail_out = dlen - dskip;
	sqe->source_addr_l = lower_32_bits(s_addr);
	sqe->source_addr_h = upper_32_bits(s_addr);
	sqe->dest_addr_l = lower_32_bits(d_addr);
	sqe->dest_addr_h = upper_32_bits(d_addr);
}

static int hisi_zip_do_work(struct hisi_zip_req *req,
			    struct hisi_zip_qp_ctx *qp_ctx)
{
	struct hisi_acc_sgl_pool *pool = qp_ctx->sgl_pool;
	struct hisi_zip_dfx *dfx = &qp_ctx->zip_dev->dfx;
	struct acomp_req *a_req = req->req;
	struct hisi_qp *qp = qp_ctx->qp;
	struct device *dev = &qp->qm->pdev->dev;
	struct hisi_zip_sqe zip_sqe;
	dma_addr_t input, output;
	int ret;

	if (!a_req->src || !a_req->slen || !a_req->dst || !a_req->dlen)
		return -EINVAL;

	req->hw_src = hisi_acc_sg_buf_map_to_hw_sgl(dev, a_req->src, pool,
						    req->req_id << 1, &input);
	if (IS_ERR(req->hw_src)) {
		dev_err(dev, "failed to map the src buffer to hw sgl (%ld)!\n",
			PTR_ERR(req->hw_src));
		return PTR_ERR(req->hw_src);
	}
	req->dma_src = input;

	req->hw_dst = hisi_acc_sg_buf_map_to_hw_sgl(dev, a_req->dst, pool,
						    (req->req_id << 1) + 1,
						    &output);
	if (IS_ERR(req->hw_dst)) {
		ret = PTR_ERR(req->hw_dst);
		dev_err(dev, "failed to map the dst buffer to hw slg (%d)!\n",
			ret);
		goto err_unmap_input;
	}
	req->dma_dst = output;

	hisi_zip_fill_sqe(&zip_sqe, qp->req_type, input, output, a_req->slen,
			  a_req->dlen, req->sskip, req->dskip);
	hisi_zip_config_buf_type(&zip_sqe, HZIP_SGL);
	hisi_zip_config_tag(&zip_sqe, req->req_id);

	/* send command to start a task */
	atomic64_inc(&dfx->send_cnt);
	ret = hisi_qp_send(qp, &zip_sqe);
	if (ret < 0) {
		atomic64_inc(&dfx->send_busy_cnt);
		ret = -EAGAIN;
		dev_dbg_ratelimited(dev, "failed to send request!\n");
		goto err_unmap_output;
	}

	return -EINPROGRESS;

err_unmap_output:
	hisi_acc_sg_buf_unmap(dev, a_req->dst, req->hw_dst);
err_unmap_input:
	hisi_acc_sg_buf_unmap(dev, a_req->src, req->hw_src);
	return ret;
}

static void hisi_zip_acomp_cb(struct hisi_qp *qp, void *data)
{
	struct hisi_zip_qp_ctx *qp_ctx = qp->qp_ctx;
	struct hisi_zip_dfx *dfx = &qp_ctx->zip_dev->dfx;
	struct hisi_zip_req_q *req_q = &qp_ctx->req_q;
	struct device *dev = &qp->qm->pdev->dev;
	struct hisi_zip_sqe *sqe = data;
	struct hisi_zip_req *req = req_q->q + sqe->tag;
	struct acomp_req *acomp_req = req->req;
	u32 status, dlen, head_size;
	int err = 0;

	atomic64_inc(&dfx->recv_cnt);
	status = sqe->dw3 & HZIP_BD_STATUS_M;
	if (status != 0 && status != HZIP_NC_ERR) {
		dev_err(dev, "%scompress fail in qp%u: %u, output: %u\n",
			(qp->alg_type == 0) ? "" : "de", qp->qp_id, status,
			sqe->produced);
		atomic64_inc(&dfx->err_bd_cnt);
		err = -EIO;
	}
	dlen = sqe->produced;

	hisi_acc_sg_buf_unmap(dev, acomp_req->src, req->hw_src);
	hisi_acc_sg_buf_unmap(dev, acomp_req->dst, req->hw_dst);

	head_size = (qp->alg_type == 0) ? TO_HEAD_SIZE(qp->req_type) : 0;
	acomp_req->dlen = dlen + head_size;

	if (acomp_req->base.complete)
		acomp_request_complete(acomp_req, err);

	hisi_zip_remove_req(qp_ctx, req);
}

static int hisi_zip_acompress(struct acomp_req *acomp_req)
{
	struct hisi_zip_ctx *ctx = crypto_tfm_ctx(acomp_req->base.tfm);
	struct hisi_zip_qp_ctx *qp_ctx = &ctx->qp_ctx[HZIP_QPC_COMP];
	struct device *dev = &qp_ctx->qp->qm->pdev->dev;
	struct hisi_zip_req *req;
	int head_size;
	int ret;

	/* let's output compression head now */
	head_size = add_comp_head(acomp_req->dst, qp_ctx->qp->req_type);
	if (head_size < 0) {
		dev_err_ratelimited(dev, "failed to add comp head (%d)!\n",
				    head_size);
		return head_size;
	}

	req = hisi_zip_create_req(acomp_req, qp_ctx, head_size, true);
	if (IS_ERR(req))
		return PTR_ERR(req);

	ret = hisi_zip_do_work(req, qp_ctx);
	if (ret != -EINPROGRESS) {
		dev_info_ratelimited(dev, "failed to do compress (%d)!\n", ret);
		hisi_zip_remove_req(qp_ctx, req);
	}

	return ret;
}

static int hisi_zip_adecompress(struct acomp_req *acomp_req)
{
	struct hisi_zip_ctx *ctx = crypto_tfm_ctx(acomp_req->base.tfm);
	struct hisi_zip_qp_ctx *qp_ctx = &ctx->qp_ctx[HZIP_QPC_DECOMP];
	struct device *dev = &qp_ctx->qp->qm->pdev->dev;
	struct hisi_zip_req *req;
	int head_size, ret;

	head_size = get_comp_head_size(acomp_req, qp_ctx->qp->req_type);
	if (head_size < 0) {
		dev_err_ratelimited(dev, "failed to get comp head size (%d)!\n",
				    head_size);
		return head_size;
	}

	req = hisi_zip_create_req(acomp_req, qp_ctx, head_size, false);
	if (IS_ERR(req))
		return PTR_ERR(req);

	ret = hisi_zip_do_work(req, qp_ctx);
	if (ret != -EINPROGRESS) {
		dev_info_ratelimited(dev, "failed to do decompress (%d)!\n",
				     ret);
		hisi_zip_remove_req(qp_ctx, req);
	}

	return ret;
}

static int hisi_zip_start_qp(struct hisi_qp *qp, struct hisi_zip_qp_ctx *ctx,
			     int alg_type, int req_type)
{
	struct device *dev = &qp->qm->pdev->dev;
	int ret;

	qp->req_type = req_type;
	qp->alg_type = alg_type;
	qp->qp_ctx = ctx;

	ret = hisi_qm_start_qp(qp, 0);
	if (ret < 0) {
		dev_err(dev, "failed to start qp (%d)!\n", ret);
		return ret;
	}

	ctx->qp = qp;

	return 0;
}

static void hisi_zip_release_qp(struct hisi_zip_qp_ctx *ctx)
{
	hisi_qm_stop_qp(ctx->qp);
	hisi_qm_release_qp(ctx->qp);
}

static int hisi_zip_ctx_init(struct hisi_zip_ctx *hisi_zip_ctx, u8 req_type, int node)
{
	struct hisi_qp *qps[HZIP_CTX_Q_NUM] = { NULL };
	struct hisi_zip *hisi_zip;
	int ret, i, j;

	ret = zip_create_qps(qps, HZIP_CTX_Q_NUM, node);
	if (ret) {
		pr_err("failed to create zip qps (%d)!\n", ret);
		return -ENODEV;
	}

	hisi_zip = container_of(qps[0]->qm, struct hisi_zip, qm);

	for (i = 0; i < HZIP_CTX_Q_NUM; i++) {
		/* alg_type = 0 for compress, 1 for decompress in hw sqe */
		ret = hisi_zip_start_qp(qps[i], &hisi_zip_ctx->qp_ctx[i], i,
					req_type);
		if (ret) {
			for (j = i - 1; j >= 0; j--)
				hisi_qm_stop_qp(hisi_zip_ctx->qp_ctx[j].qp);

			hisi_qm_free_qps(qps, HZIP_CTX_Q_NUM);
			return ret;
		}

		hisi_zip_ctx->qp_ctx[i].zip_dev = hisi_zip;
	}

	return 0;
}

static void hisi_zip_ctx_exit(struct hisi_zip_ctx *hisi_zip_ctx)
{
	int i;

	for (i = 1; i >= 0; i--)
		hisi_zip_release_qp(&hisi_zip_ctx->qp_ctx[i]);
}

static int hisi_zip_create_req_q(struct hisi_zip_ctx *ctx)
{
	struct hisi_zip_req_q *req_q;
	int i, ret;

	for (i = 0; i < HZIP_CTX_Q_NUM; i++) {
		req_q = &ctx->qp_ctx[i].req_q;
		req_q->size = QM_Q_DEPTH;

		req_q->req_bitmap = kcalloc(BITS_TO_LONGS(req_q->size),
					    sizeof(long), GFP_KERNEL);
		if (!req_q->req_bitmap) {
			ret = -ENOMEM;
			if (i == 0)
				return ret;

			goto err_free_loop0;
		}
		rwlock_init(&req_q->req_lock);

		req_q->q = kcalloc(req_q->size, sizeof(struct hisi_zip_req),
				   GFP_KERNEL);
		if (!req_q->q) {
			ret = -ENOMEM;
			if (i == 0)
				goto err_free_bitmap;
			else
				goto err_free_loop1;
		}
	}

	return 0;

err_free_loop1:
	kfree(ctx->qp_ctx[HZIP_QPC_DECOMP].req_q.req_bitmap);
err_free_loop0:
	kfree(ctx->qp_ctx[HZIP_QPC_COMP].req_q.q);
err_free_bitmap:
	kfree(ctx->qp_ctx[HZIP_QPC_COMP].req_q.req_bitmap);
	return ret;
}

static void hisi_zip_release_req_q(struct hisi_zip_ctx *ctx)
{
	int i;

	for (i = 0; i < HZIP_CTX_Q_NUM; i++) {
		kfree(ctx->qp_ctx[i].req_q.q);
		kfree(ctx->qp_ctx[i].req_q.req_bitmap);
	}
}

static int hisi_zip_create_sgl_pool(struct hisi_zip_ctx *ctx)
{
	struct hisi_zip_qp_ctx *tmp;
	struct device *dev;
	int i;

	for (i = 0; i < HZIP_CTX_Q_NUM; i++) {
		tmp = &ctx->qp_ctx[i];
		dev = &tmp->qp->qm->pdev->dev;
		tmp->sgl_pool = hisi_acc_create_sgl_pool(dev, QM_Q_DEPTH << 1,
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
		return ret;
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
	return ret;
}

static void hisi_zip_acomp_exit(struct crypto_acomp *tfm)
{
	struct hisi_zip_ctx *ctx = crypto_tfm_ctx(&tfm->base);

	hisi_zip_set_acomp_cb(ctx, NULL);
	hisi_zip_release_sgl_pool(ctx);
	hisi_zip_release_req_q(ctx);
	hisi_zip_ctx_exit(ctx);
}

static struct acomp_alg hisi_zip_acomp_zlib = {
	.init			= hisi_zip_acomp_init,
	.exit			= hisi_zip_acomp_exit,
	.compress		= hisi_zip_acompress,
	.decompress		= hisi_zip_adecompress,
	.base			= {
		.cra_name		= "zlib-deflate",
		.cra_driver_name	= "hisi-zlib-acomp",
		.cra_module		= THIS_MODULE,
		.cra_priority           = HZIP_ALG_PRIORITY,
		.cra_ctxsize		= sizeof(struct hisi_zip_ctx),
	}
};

static struct acomp_alg hisi_zip_acomp_gzip = {
	.init			= hisi_zip_acomp_init,
	.exit			= hisi_zip_acomp_exit,
	.compress		= hisi_zip_acompress,
	.decompress		= hisi_zip_adecompress,
	.base			= {
		.cra_name		= "gzip",
		.cra_driver_name	= "hisi-gzip-acomp",
		.cra_module		= THIS_MODULE,
		.cra_priority           = HZIP_ALG_PRIORITY,
		.cra_ctxsize		= sizeof(struct hisi_zip_ctx),
	}
};

int hisi_zip_register_to_crypto(struct hisi_qm *qm)
{
	int ret;

	ret = crypto_register_acomp(&hisi_zip_acomp_zlib);
	if (ret) {
		pr_err("failed to register to zlib (%d)!\n", ret);
		return ret;
	}

	ret = crypto_register_acomp(&hisi_zip_acomp_gzip);
	if (ret) {
		pr_err("failed to register to gzip (%d)!\n", ret);
		crypto_unregister_acomp(&hisi_zip_acomp_zlib);
	}

	return ret;
}

void hisi_zip_unregister_from_crypto(struct hisi_qm *qm)
{
	crypto_unregister_acomp(&hisi_zip_acomp_gzip);
	crypto_unregister_acomp(&hisi_zip_acomp_zlib);
}
