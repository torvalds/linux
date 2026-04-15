// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2022 Intel Corporation */
#include <linux/crypto.h>
#include <crypto/acompress.h>
#include <crypto/internal/acompress.h>
#include <crypto/scatterwalk.h>
#include <linux/dma-mapping.h>
#include <linux/workqueue.h>
#include <linux/zstd.h>
#include "adf_accel_devices.h"
#include "adf_common_drv.h"
#include "adf_dc.h"
#include "qat_bl.h"
#include "qat_comp_req.h"
#include "qat_compression.h"
#include "qat_algs_send.h"
#include "qat_comp_zstd_utils.h"

#define QAT_ZSTD_SCRATCH_SIZE		524288
#define QAT_ZSTD_MAX_BLOCK_SIZE		65535
#define QAT_ZSTD_MAX_CONTENT_SIZE	4096
#define QAT_LZ4S_MIN_INPUT_SIZE		8192
#define QAT_LZ4S_MAX_OUTPUT_SIZE	QAT_ZSTD_SCRATCH_SIZE
#define QAT_MAX_SEQUENCES		(128 * 1024)

static DEFINE_MUTEX(algs_lock);
static unsigned int active_devs_deflate;
static unsigned int active_devs_lz4s;
static unsigned int active_devs_zstd;

struct qat_zstd_scratch {
	size_t		cctx_buffer_size;
	void		*lz4s;
	void		*literals;
	void		*out_seqs;
	void		*workspace;
	ZSTD_CCtx	*ctx;
};

static void *qat_zstd_alloc_scratch(void)
{
	struct qat_zstd_scratch *scratch;
	ZSTD_parameters params;
	size_t cctx_size;
	ZSTD_CCtx *ctx;
	size_t zret;
	int ret;

	ret = -ENOMEM;
	scratch = kzalloc_obj(*scratch);
	if (!scratch)
		return ERR_PTR(ret);

	scratch->lz4s = kvmalloc(QAT_ZSTD_SCRATCH_SIZE, GFP_KERNEL);
	if (!scratch->lz4s)
		goto error;

	scratch->literals = kvmalloc(QAT_ZSTD_SCRATCH_SIZE, GFP_KERNEL);
	if (!scratch->literals)
		goto error;

	scratch->out_seqs = kvcalloc(QAT_MAX_SEQUENCES, sizeof(ZSTD_Sequence),
				     GFP_KERNEL);
	if (!scratch->out_seqs)
		goto error;

	params = zstd_get_params(zstd_max_clevel(), QAT_ZSTD_SCRATCH_SIZE);
	cctx_size = zstd_cctx_workspace_bound(&params.cParams);

	scratch->workspace = kvmalloc(cctx_size, GFP_KERNEL | __GFP_ZERO);
	if (!scratch->workspace)
		goto error;

	ret = -EINVAL;
	ctx = zstd_init_cctx(scratch->workspace, cctx_size);
	if (!ctx)
		goto error;

	scratch->ctx = ctx;
	scratch->cctx_buffer_size = cctx_size;

	zret = zstd_cctx_set_param(ctx, ZSTD_c_blockDelimiters, ZSTD_sf_explicitBlockDelimiters);
	if (zstd_is_error(zret))
		goto error;

	return scratch;

error:
	kvfree(scratch->lz4s);
	kvfree(scratch->literals);
	kvfree(scratch->out_seqs);
	kvfree(scratch->workspace);
	kfree(scratch);
	return ERR_PTR(ret);
}

static void qat_zstd_free_scratch(void *ctx)
{
	struct qat_zstd_scratch *scratch = ctx;

	if (!scratch)
		return;

	kvfree(scratch->lz4s);
	kvfree(scratch->literals);
	kvfree(scratch->out_seqs);
	kvfree(scratch->workspace);
	kfree(scratch);
}

static struct crypto_acomp_streams qat_zstd_streams = {
	.alloc_ctx = qat_zstd_alloc_scratch,
	.free_ctx = qat_zstd_free_scratch,
};

enum direction {
	DECOMPRESSION = 0,
	COMPRESSION = 1,
};

struct qat_compression_req;

struct qat_callback_params {
	unsigned int produced;
	unsigned int dlen;
	bool plain;
};

struct qat_compression_ctx {
	u8 comp_ctx[QAT_COMP_CTX_SIZE];
	struct qat_compression_instance *inst;
	int (*qat_comp_callback)(struct qat_compression_req *qat_req, void *resp,
				 struct qat_callback_params *params);
	struct crypto_acomp *ftfm;
};

struct qat_compression_req {
	u8 req[QAT_COMP_REQ_SIZE];
	struct qat_compression_ctx *qat_compression_ctx;
	struct acomp_req *acompress_req;
	struct qat_request_buffs buf;
	enum direction dir;
	int actual_dlen;
	struct qat_alg_req alg_req;
};

static int qat_alg_send_dc_message(struct qat_compression_req *qat_req,
				   struct qat_compression_instance *inst,
				   struct crypto_async_request *base)
{
	struct qat_alg_req *alg_req = &qat_req->alg_req;

	alg_req->fw_req = (u32 *)&qat_req->req;
	alg_req->tx_ring = inst->dc_tx;
	alg_req->base = base;
	alg_req->backlog = &inst->backlog;

	return qat_alg_send_message(alg_req);
}

static void qat_comp_generic_callback(struct qat_compression_req *qat_req,
				      void *resp)
{
	struct acomp_req *areq = qat_req->acompress_req;
	struct qat_compression_ctx *ctx = qat_req->qat_compression_ctx;
	struct adf_accel_dev *accel_dev = ctx->inst->accel_dev;
	struct crypto_acomp *tfm = crypto_acomp_reqtfm(areq);
	struct qat_compression_instance *inst = ctx->inst;
	struct qat_callback_params params = { };
	int consumed, produced;
	s8 cmp_err, xlt_err;
	int res = -EBADMSG;
	int status;
	u8 cnv;

	status = qat_comp_get_cmp_status(resp);
	status |= qat_comp_get_xlt_status(resp);
	cmp_err = qat_comp_get_cmp_err(resp);
	xlt_err = qat_comp_get_xlt_err(resp);

	consumed = qat_comp_get_consumed_ctr(resp);
	produced = qat_comp_get_produced_ctr(resp);

	/* Cache parameters for algorithm specific callback */
	params.produced = produced;
	params.dlen = areq->dlen;

	dev_dbg(&GET_DEV(accel_dev),
		"[%s][%s][%s] slen = %8d dlen = %8d consumed = %8d produced = %8d cmp_err = %3d xlt_err = %3d",
		crypto_tfm_alg_driver_name(crypto_acomp_tfm(tfm)),
		qat_req->dir == COMPRESSION ? "comp  " : "decomp",
		status ? "ERR" : "OK ",
		areq->slen, areq->dlen, consumed, produced, cmp_err, xlt_err);

	if (unlikely(status != ICP_QAT_FW_COMN_STATUS_FLAG_OK)) {
		if (cmp_err == ERR_CODE_OVERFLOW_ERROR || xlt_err == ERR_CODE_OVERFLOW_ERROR)
			res = -E2BIG;

		areq->dlen = 0;
		goto end;
	}

	if (qat_req->dir == COMPRESSION) {
		cnv = qat_comp_get_cmp_cnv_flag(resp);
		if (unlikely(!cnv)) {
			dev_err(&GET_DEV(accel_dev),
				"Verified compression not supported\n");
			areq->dlen = 0;
			goto end;
		}

		if (unlikely(produced > qat_req->actual_dlen)) {
			memset(inst->dc_data->ovf_buff, 0,
			       inst->dc_data->ovf_buff_sz);
			dev_dbg(&GET_DEV(accel_dev),
				"Actual buffer overflow: produced=%d, dlen=%d\n",
				produced, qat_req->actual_dlen);

			res = -E2BIG;
			areq->dlen = 0;
			goto end;
		}

		params.plain = !!qat_comp_get_cmp_uncomp_flag(resp);
	}

	res = 0;
	areq->dlen = produced;

	if (ctx->qat_comp_callback)
		res = ctx->qat_comp_callback(qat_req, resp, &params);

end:
	qat_bl_free_bufl(accel_dev, &qat_req->buf);
	acomp_request_complete(areq, res);
	qat_alg_send_backlog(qat_req->alg_req.backlog);
}

void qat_comp_alg_callback(void *resp)
{
	struct qat_compression_req *qat_req =
			(void *)(__force long)qat_comp_get_opaque(resp);

	qat_comp_generic_callback(qat_req, resp);
}

static int qat_comp_alg_init_tfm(struct crypto_acomp *acomp_tfm, int alg)
{
	struct qat_compression_ctx *ctx = acomp_tfm_ctx(acomp_tfm);
	struct crypto_tfm *tfm = crypto_acomp_tfm(acomp_tfm);
	struct qat_compression_instance *inst;
	int node, ret;

	if (tfm->node == NUMA_NO_NODE)
		node = numa_node_id();
	else
		node = tfm->node;

	memset(ctx, 0, sizeof(*ctx));
	inst = qat_compression_get_instance_node(node, alg);
	if (!inst)
		return -EINVAL;
	ctx->inst = inst;

	ret = qat_comp_build_ctx(inst->accel_dev, ctx->comp_ctx, alg);
	if (ret) {
		qat_compression_put_instance(inst);
		memset(ctx, 0, sizeof(*ctx));
	}

	return ret;
}

static int qat_comp_alg_deflate_init_tfm(struct crypto_acomp *acomp_tfm)
{
	return qat_comp_alg_init_tfm(acomp_tfm, QAT_DEFLATE);
}

static void qat_comp_alg_exit_tfm(struct crypto_acomp *acomp_tfm)
{
	struct qat_compression_ctx *ctx = acomp_tfm_ctx(acomp_tfm);

	qat_compression_put_instance(ctx->inst);
	memset(ctx, 0, sizeof(*ctx));
}

static int qat_comp_alg_compress_decompress(struct acomp_req *areq, enum direction dir,
					    unsigned int shdr, unsigned int sftr,
					    unsigned int dhdr, unsigned int dftr)
{
	struct qat_compression_req *qat_req = acomp_request_ctx(areq);
	struct crypto_acomp *acomp_tfm = crypto_acomp_reqtfm(areq);
	struct qat_compression_ctx *ctx = acomp_tfm_ctx(acomp_tfm);
	struct qat_compression_instance *inst = ctx->inst;
	gfp_t f = qat_algs_alloc_flags(&areq->base);
	struct qat_sgl_to_bufl_params params = {0};
	int slen = areq->slen - shdr - sftr;
	int dlen = areq->dlen - dhdr - dftr;
	dma_addr_t sfbuf, dfbuf;
	u8 *req = qat_req->req;
	size_t ovf_buff_sz;
	int ret;

	params.sskip = shdr;
	params.dskip = dhdr;

	if (!areq->src || !slen)
		return -EINVAL;

	if (!areq->dst || !dlen)
		return -EINVAL;

	if (dir == COMPRESSION) {
		params.extra_dst_buff = inst->dc_data->ovf_buff_p;
		ovf_buff_sz = inst->dc_data->ovf_buff_sz;
		params.sz_extra_dst_buff = ovf_buff_sz;
	}

	ret = qat_bl_sgl_to_bufl(ctx->inst->accel_dev, areq->src, areq->dst,
				 &qat_req->buf, &params, f);
	if (unlikely(ret))
		return ret;

	sfbuf = qat_req->buf.blp;
	dfbuf = qat_req->buf.bloutp;
	qat_req->qat_compression_ctx = ctx;
	qat_req->acompress_req = areq;
	qat_req->dir = dir;

	if (dir == COMPRESSION) {
		qat_req->actual_dlen = dlen;
		dlen += ovf_buff_sz;
		qat_comp_create_compression_req(ctx->comp_ctx, req,
						(u64)(__force long)sfbuf, slen,
						(u64)(__force long)dfbuf, dlen,
						(u64)(__force long)qat_req);
	} else {
		qat_comp_create_decompression_req(ctx->comp_ctx, req,
						  (u64)(__force long)sfbuf, slen,
						  (u64)(__force long)dfbuf, dlen,
						  (u64)(__force long)qat_req);
	}

	ret = qat_alg_send_dc_message(qat_req, inst, &areq->base);
	if (ret == -ENOSPC)
		qat_bl_free_bufl(inst->accel_dev, &qat_req->buf);

	return ret;
}

static int qat_comp_alg_compress(struct acomp_req *req)
{
	return qat_comp_alg_compress_decompress(req, COMPRESSION, 0, 0, 0, 0);
}

static int qat_comp_alg_decompress(struct acomp_req *req)
{
	return qat_comp_alg_compress_decompress(req, DECOMPRESSION, 0, 0, 0, 0);
}

static int qat_comp_alg_zstd_decompress(struct acomp_req *req)
{
	struct crypto_acomp *acomp_tfm = crypto_acomp_reqtfm(req);
	struct qat_compression_ctx *ctx = acomp_tfm_ctx(acomp_tfm);
	struct acomp_req *nreq = acomp_request_ctx(req);
	zstd_frame_header header;
	void *buffer;
	size_t zret;
	int ret;

	buffer = kmap_local_page(sg_page(req->src)) + req->src->offset;
	zret = zstd_get_frame_header(&header, buffer, req->src->length);
	kunmap_local(buffer);
	if (zret) {
		dev_err(&GET_DEV(ctx->inst->accel_dev),
			"ZSTD-compressed data has an incomplete frame header\n");
		return -EINVAL;
	}

	if (header.windowSize > QAT_ZSTD_MAX_BLOCK_SIZE ||
	    header.frameContentSize >= QAT_ZSTD_MAX_CONTENT_SIZE) {
		dev_dbg(&GET_DEV(ctx->inst->accel_dev), "Window size=0x%llx\n",
			header.windowSize);

		memcpy(nreq, req, sizeof(*req));
		acomp_request_set_tfm(nreq, ctx->ftfm);

		ret = crypto_acomp_decompress(nreq);
		req->dlen = nreq->dlen;

		return ret;
	}

	return qat_comp_alg_compress_decompress(req, DECOMPRESSION, 0, 0, 0, 0);
}

static int qat_comp_lz4s_zstd_callback(struct qat_compression_req *qat_req, void *resp,
				       struct qat_callback_params *params)
{
	struct qat_compression_ctx *qat_ctx = qat_req->qat_compression_ctx;
	struct acomp_req *areq = qat_req->acompress_req;
	struct qat_zstd_scratch *scratch;
	struct crypto_acomp_stream *s;
	unsigned int lit_len = 0;
	ZSTD_Sequence *out_seqs;
	void *lz4s, *zstd;
	size_t comp_size;
	ZSTD_CCtx *ctx;
	void *literals;
	int seq_count;
	int ret = 0;

	if (params->produced + QAT_ZSTD_LIT_COPY_LEN > QAT_ZSTD_SCRATCH_SIZE) {
		dev_dbg(&GET_DEV(qat_ctx->inst->accel_dev),
			"LZ4s-ZSTD: produced size (%u) + COPY_SIZE > QAT_ZSTD_SCRATCH_SIZE (%u)\n",
			params->produced, QAT_ZSTD_SCRATCH_SIZE);
		areq->dlen = 0;
		return -E2BIG;
	}

	s = crypto_acomp_lock_stream_bh(&qat_zstd_streams);
	scratch = s->ctx;

	lz4s = scratch->lz4s;
	zstd = lz4s;  /* Output buffer is same as lz4s */
	out_seqs = scratch->out_seqs;
	ctx = scratch->ctx;
	literals = scratch->literals;

	if (likely(!params->plain)) {
		if (likely(sg_nents(areq->dst) == 1)) {
			zstd = sg_virt(areq->dst);
			lz4s = zstd;
		} else {
			memcpy_from_sglist(lz4s, areq->dst, 0, params->produced);
		}

		seq_count = qat_alg_dec_lz4s(out_seqs, QAT_MAX_SEQUENCES, lz4s,
					     params->produced, literals, &lit_len);
		if (seq_count < 0) {
			ret = seq_count;
			comp_size = 0;
			goto out;
		}
	} else {
		out_seqs[0].litLength = areq->slen;
		out_seqs[0].offset = 0;
		out_seqs[0].matchLength = 0;

		seq_count = 1;
	}

	comp_size = zstd_compress_sequences_and_literals(ctx, zstd, params->dlen,
							 out_seqs, seq_count,
							 literals, lit_len,
							 QAT_ZSTD_SCRATCH_SIZE,
							 areq->slen);
	if (zstd_is_error(comp_size)) {
		if (comp_size == ZSTD_error_cannotProduce_uncompressedBlock)
			ret = -E2BIG;
		else
			ret = -EOPNOTSUPP;

		comp_size = 0;
		goto out;
	}

	if (comp_size > params->dlen) {
		dev_dbg(&GET_DEV(qat_ctx->inst->accel_dev),
			"LZ4s-ZSTD: compressed_size (%u) > output buffer size (%u)\n",
			(unsigned int)comp_size, params->dlen);
		ret = -EOVERFLOW;
		goto out;
	}

	if (unlikely(sg_nents(areq->dst) != 1))
		memcpy_to_sglist(areq->dst, 0, zstd, comp_size);

out:
	areq->dlen = comp_size;
	crypto_acomp_unlock_stream_bh(s);

	return ret;
}

static int qat_comp_alg_lz4s_zstd_init_tfm(struct crypto_acomp *acomp_tfm)
{
	struct qat_compression_ctx *ctx = acomp_tfm_ctx(acomp_tfm);
	struct crypto_tfm *tfm = crypto_acomp_tfm(acomp_tfm);
	int reqsize;
	int ret;

	/* qat_comp_alg_init_tfm() wipes out the ctx */
	ret = qat_comp_alg_init_tfm(acomp_tfm, QAT_LZ4S);
	if (ret)
		return ret;

	ctx->ftfm = crypto_alloc_acomp_node("zstd", 0, CRYPTO_ALG_NEED_FALLBACK,
					    tfm->node);
	if (IS_ERR(ctx->ftfm)) {
		qat_comp_alg_exit_tfm(acomp_tfm);
		return PTR_ERR(ctx->ftfm);
	}

	reqsize = max(sizeof(struct qat_compression_req),
		      sizeof(struct acomp_req) + crypto_acomp_reqsize(ctx->ftfm));

	acomp_tfm->reqsize = reqsize;

	ctx->qat_comp_callback = qat_comp_lz4s_zstd_callback;

	return 0;
}

static int qat_comp_alg_zstd_init_tfm(struct crypto_acomp *acomp_tfm)
{
	struct qat_compression_ctx *ctx = acomp_tfm_ctx(acomp_tfm);
	struct crypto_tfm *tfm = crypto_acomp_tfm(acomp_tfm);
	int reqsize;
	int ret;

	/* qat_comp_alg_init_tfm() wipes out the ctx */
	ret = qat_comp_alg_init_tfm(acomp_tfm, QAT_ZSTD);
	if (ret)
		return ret;

	ctx->ftfm = crypto_alloc_acomp_node("zstd", 0, CRYPTO_ALG_NEED_FALLBACK,
					    tfm->node);
	if (IS_ERR(ctx->ftfm)) {
		qat_comp_alg_exit_tfm(acomp_tfm);
		return PTR_ERR(ctx->ftfm);
	}

	reqsize = max(sizeof(struct qat_compression_req),
		      sizeof(struct acomp_req) + crypto_acomp_reqsize(ctx->ftfm));

	acomp_tfm->reqsize = reqsize;

	return 0;
}

static void qat_comp_alg_zstd_exit_tfm(struct crypto_acomp *acomp_tfm)
{
	struct qat_compression_ctx *ctx = acomp_tfm_ctx(acomp_tfm);

	if (ctx->ftfm)
		crypto_free_acomp(ctx->ftfm);

	qat_comp_alg_exit_tfm(acomp_tfm);
}

static int qat_comp_alg_lz4s_zstd_compress(struct acomp_req *req)
{
	struct crypto_acomp *acomp_tfm = crypto_acomp_reqtfm(req);
	struct qat_compression_ctx *ctx = acomp_tfm_ctx(acomp_tfm);
	struct acomp_req *nreq = acomp_request_ctx(req);
	int ret;

	if (req->slen >= QAT_LZ4S_MIN_INPUT_SIZE && req->dlen >= QAT_LZ4S_MIN_INPUT_SIZE &&
	    req->slen <= QAT_LZ4S_MAX_OUTPUT_SIZE && req->dlen <= QAT_LZ4S_MAX_OUTPUT_SIZE)
		return qat_comp_alg_compress(req);

	memcpy(nreq, req, sizeof(*req));
	acomp_request_set_tfm(nreq, ctx->ftfm);

	ret = crypto_acomp_compress(nreq);
	req->dlen = nreq->dlen;

	return ret;
}

static int qat_comp_alg_sw_decompress(struct acomp_req *req)
{
	struct crypto_acomp *acomp_tfm = crypto_acomp_reqtfm(req);
	struct qat_compression_ctx *ctx = acomp_tfm_ctx(acomp_tfm);
	struct acomp_req *nreq = acomp_request_ctx(req);
	int ret;

	memcpy(nreq, req, sizeof(*req));
	acomp_request_set_tfm(nreq, ctx->ftfm);

	ret = crypto_acomp_decompress(nreq);
	req->dlen = nreq->dlen;

	return ret;
}

static struct acomp_alg qat_acomp_deflate[] = { {
	.base = {
		.cra_name = "deflate",
		.cra_driver_name = "qat_deflate",
		.cra_priority = 4001,
		.cra_flags = CRYPTO_ALG_ASYNC | CRYPTO_ALG_ALLOCATES_MEMORY,
		.cra_ctxsize = sizeof(struct qat_compression_ctx),
		.cra_reqsize = sizeof(struct qat_compression_req),
		.cra_module = THIS_MODULE,
	},
	.init = qat_comp_alg_deflate_init_tfm,
	.exit = qat_comp_alg_exit_tfm,
	.compress = qat_comp_alg_compress,
	.decompress = qat_comp_alg_decompress,
}};

static struct acomp_alg qat_acomp_zstd_lz4s = {
	.base = {
		.cra_name = "zstd",
		.cra_driver_name = "qat_zstd",
		.cra_priority = 4001,
		.cra_flags = CRYPTO_ALG_ASYNC | CRYPTO_ALG_ALLOCATES_MEMORY |
			     CRYPTO_ALG_NEED_FALLBACK,
		.cra_reqsize = sizeof(struct qat_compression_req),
		.cra_ctxsize = sizeof(struct qat_compression_ctx),
		.cra_module = THIS_MODULE,
	},
	.init = qat_comp_alg_lz4s_zstd_init_tfm,
	.exit = qat_comp_alg_zstd_exit_tfm,
	.compress = qat_comp_alg_lz4s_zstd_compress,
	.decompress = qat_comp_alg_sw_decompress,
};

static struct acomp_alg qat_acomp_zstd_native = {
	.base = {
		.cra_name = "zstd",
		.cra_driver_name = "qat_zstd",
		.cra_priority = 4001,
		.cra_flags = CRYPTO_ALG_ASYNC | CRYPTO_ALG_ALLOCATES_MEMORY |
			     CRYPTO_ALG_NEED_FALLBACK,
		.cra_reqsize = sizeof(struct qat_compression_req),
		.cra_ctxsize = sizeof(struct qat_compression_ctx),
		.cra_module = THIS_MODULE,
	},
	.init = qat_comp_alg_zstd_init_tfm,
	.exit = qat_comp_alg_zstd_exit_tfm,
	.compress = qat_comp_alg_compress,
	.decompress = qat_comp_alg_zstd_decompress,
};

static int qat_comp_algs_register_deflate(void)
{
	int ret = 0;

	mutex_lock(&algs_lock);
	if (++active_devs_deflate == 1) {
		ret = crypto_register_acomps(qat_acomp_deflate,
					     ARRAY_SIZE(qat_acomp_deflate));
		if (ret)
			active_devs_deflate--;
	}
	mutex_unlock(&algs_lock);

	return ret;
}

static void qat_comp_algs_unregister_deflate(void)
{
	mutex_lock(&algs_lock);
	if (--active_devs_deflate == 0)
		crypto_unregister_acomps(qat_acomp_deflate, ARRAY_SIZE(qat_acomp_deflate));
	mutex_unlock(&algs_lock);
}

static int qat_comp_algs_register_lz4s(void)
{
	int ret = 0;

	mutex_lock(&algs_lock);
	if (++active_devs_lz4s == 1) {
		ret = crypto_acomp_alloc_streams(&qat_zstd_streams);
		if (ret) {
			active_devs_lz4s--;
			goto unlock;
		}

		ret = crypto_register_acomp(&qat_acomp_zstd_lz4s);
		if (ret) {
			crypto_acomp_free_streams(&qat_zstd_streams);
			active_devs_lz4s--;
		}
	}
unlock:
	mutex_unlock(&algs_lock);

	return ret;
}

static void qat_comp_algs_unregister_lz4s(void)
{
	mutex_lock(&algs_lock);
	if (--active_devs_lz4s == 0) {
		crypto_unregister_acomp(&qat_acomp_zstd_lz4s);
		crypto_acomp_free_streams(&qat_zstd_streams);
	}
	mutex_unlock(&algs_lock);
}

static int qat_comp_algs_register_zstd(void)
{
	int ret = 0;

	mutex_lock(&algs_lock);
	if (++active_devs_zstd == 1) {
		ret = crypto_register_acomp(&qat_acomp_zstd_native);
		if (ret)
			active_devs_zstd--;
	}
	mutex_unlock(&algs_lock);

	return ret;
}

static void qat_comp_algs_unregister_zstd(void)
{
	mutex_lock(&algs_lock);
	if (--active_devs_zstd == 0)
		crypto_unregister_acomp(&qat_acomp_zstd_native);
	mutex_unlock(&algs_lock);
}

int qat_comp_algs_register(u32 caps)
{
	int ret;

	ret = qat_comp_algs_register_deflate();
	if (ret)
		return ret;

	if (caps & ADF_ACCEL_CAPABILITIES_EXT_ZSTD_LZ4S) {
		ret = qat_comp_algs_register_lz4s();
		if (ret)
			goto err_unregister_deflate;
	}

	if (caps & ADF_ACCEL_CAPABILITIES_EXT_ZSTD) {
		ret = qat_comp_algs_register_zstd();
		if (ret)
			goto err_unregister_lz4s;
	}

	return ret;

err_unregister_lz4s:
	if (caps & ADF_ACCEL_CAPABILITIES_EXT_ZSTD_LZ4S)
		qat_comp_algs_unregister_lz4s();
err_unregister_deflate:
	qat_comp_algs_unregister_deflate();

	return ret;
}

void qat_comp_algs_unregister(u32 caps)
{
	qat_comp_algs_unregister_deflate();

	if (caps & ADF_ACCEL_CAPABILITIES_EXT_ZSTD_LZ4S)
		qat_comp_algs_unregister_lz4s();

	if (caps & ADF_ACCEL_CAPABILITIES_EXT_ZSTD)
		qat_comp_algs_unregister_zstd();
}
