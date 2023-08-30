// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2022 Intel Corporation */
#include <linux/crypto.h>
#include <crypto/acompress.h>
#include <crypto/internal/acompress.h>
#include <crypto/scatterwalk.h>
#include <linux/dma-mapping.h>
#include <linux/workqueue.h>
#include "adf_accel_devices.h"
#include "adf_common_drv.h"
#include "qat_bl.h"
#include "qat_comp_req.h"
#include "qat_compression.h"
#include "qat_algs_send.h"

#define QAT_RFC_1950_HDR_SIZE 2
#define QAT_RFC_1950_FOOTER_SIZE 4
#define QAT_RFC_1950_CM_DEFLATE 8
#define QAT_RFC_1950_CM_DEFLATE_CINFO_32K 7
#define QAT_RFC_1950_CM_MASK 0x0f
#define QAT_RFC_1950_CM_OFFSET 4
#define QAT_RFC_1950_DICT_MASK 0x20
#define QAT_RFC_1950_COMP_HDR 0x785e

static DEFINE_MUTEX(algs_lock);
static unsigned int active_devs;

enum direction {
	DECOMPRESSION = 0,
	COMPRESSION = 1,
};

struct qat_compression_req;

struct qat_compression_ctx {
	u8 comp_ctx[QAT_COMP_CTX_SIZE];
	struct qat_compression_instance *inst;
	int (*qat_comp_callback)(struct qat_compression_req *qat_req, void *resp);
};

struct qat_dst {
	bool is_null;
	int resubmitted;
};

struct qat_compression_req {
	u8 req[QAT_COMP_REQ_SIZE];
	struct qat_compression_ctx *qat_compression_ctx;
	struct acomp_req *acompress_req;
	struct qat_request_buffs buf;
	enum direction dir;
	int actual_dlen;
	struct qat_alg_req alg_req;
	struct work_struct resubmit;
	struct qat_dst dst;
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

static void qat_comp_resubmit(struct work_struct *work)
{
	struct qat_compression_req *qat_req =
		container_of(work, struct qat_compression_req, resubmit);
	struct qat_compression_ctx *ctx = qat_req->qat_compression_ctx;
	struct adf_accel_dev *accel_dev = ctx->inst->accel_dev;
	struct qat_request_buffs *qat_bufs = &qat_req->buf;
	struct qat_compression_instance *inst = ctx->inst;
	struct acomp_req *areq = qat_req->acompress_req;
	struct crypto_acomp *tfm = crypto_acomp_reqtfm(areq);
	unsigned int dlen = CRYPTO_ACOMP_DST_MAX;
	u8 *req = qat_req->req;
	dma_addr_t dfbuf;
	int ret;

	areq->dlen = dlen;

	dev_dbg(&GET_DEV(accel_dev), "[%s][%s] retry NULL dst request - dlen = %d\n",
		crypto_tfm_alg_driver_name(crypto_acomp_tfm(tfm)),
		qat_req->dir == COMPRESSION ? "comp" : "decomp", dlen);

	ret = qat_bl_realloc_map_new_dst(accel_dev, &areq->dst, dlen, qat_bufs,
					 qat_algs_alloc_flags(&areq->base));
	if (ret)
		goto err;

	qat_req->dst.resubmitted = true;

	dfbuf = qat_req->buf.bloutp;
	qat_comp_override_dst(req, dfbuf, dlen);

	ret = qat_alg_send_dc_message(qat_req, inst, &areq->base);
	if (ret != -ENOSPC)
		return;

err:
	qat_bl_free_bufl(accel_dev, qat_bufs);
	acomp_request_complete(areq, ret);
}

static void qat_comp_generic_callback(struct qat_compression_req *qat_req,
				      void *resp)
{
	struct acomp_req *areq = qat_req->acompress_req;
	struct qat_compression_ctx *ctx = qat_req->qat_compression_ctx;
	struct adf_accel_dev *accel_dev = ctx->inst->accel_dev;
	struct crypto_acomp *tfm = crypto_acomp_reqtfm(areq);
	struct qat_compression_instance *inst = ctx->inst;
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

	dev_dbg(&GET_DEV(accel_dev),
		"[%s][%s][%s] slen = %8d dlen = %8d consumed = %8d produced = %8d cmp_err = %3d xlt_err = %3d",
		crypto_tfm_alg_driver_name(crypto_acomp_tfm(tfm)),
		qat_req->dir == COMPRESSION ? "comp  " : "decomp",
		status ? "ERR" : "OK ",
		areq->slen, areq->dlen, consumed, produced, cmp_err, xlt_err);

	areq->dlen = 0;

	if (qat_req->dir == DECOMPRESSION && qat_req->dst.is_null) {
		if (cmp_err == ERR_CODE_OVERFLOW_ERROR) {
			if (qat_req->dst.resubmitted) {
				dev_dbg(&GET_DEV(accel_dev),
					"Output does not fit destination buffer\n");
				res = -EOVERFLOW;
				goto end;
			}

			INIT_WORK(&qat_req->resubmit, qat_comp_resubmit);
			adf_misc_wq_queue_work(&qat_req->resubmit);
			return;
		}
	}

	if (unlikely(status != ICP_QAT_FW_COMN_STATUS_FLAG_OK))
		goto end;

	if (qat_req->dir == COMPRESSION) {
		cnv = qat_comp_get_cmp_cnv_flag(resp);
		if (unlikely(!cnv)) {
			dev_err(&GET_DEV(accel_dev),
				"Verified compression not supported\n");
			goto end;
		}

		if (unlikely(produced > qat_req->actual_dlen)) {
			memset(inst->dc_data->ovf_buff, 0,
			       inst->dc_data->ovf_buff_sz);
			dev_dbg(&GET_DEV(accel_dev),
				"Actual buffer overflow: produced=%d, dlen=%d\n",
				produced, qat_req->actual_dlen);
			goto end;
		}
	}

	res = 0;
	areq->dlen = produced;

	if (ctx->qat_comp_callback)
		res = ctx->qat_comp_callback(qat_req, resp);

end:
	qat_bl_free_bufl(accel_dev, &qat_req->buf);
	acomp_request_complete(areq, res);
}

void qat_comp_alg_callback(void *resp)
{
	struct qat_compression_req *qat_req =
			(void *)(__force long)qat_comp_get_opaque(resp);
	struct qat_instance_backlog *backlog = qat_req->alg_req.backlog;

	qat_comp_generic_callback(qat_req, resp);

	qat_alg_send_backlog(backlog);
}

static int qat_comp_alg_init_tfm(struct crypto_acomp *acomp_tfm)
{
	struct crypto_tfm *tfm = crypto_acomp_tfm(acomp_tfm);
	struct qat_compression_ctx *ctx = crypto_tfm_ctx(tfm);
	struct qat_compression_instance *inst;
	int node;

	if (tfm->node == NUMA_NO_NODE)
		node = numa_node_id();
	else
		node = tfm->node;

	memset(ctx, 0, sizeof(*ctx));
	inst = qat_compression_get_instance_node(node);
	if (!inst)
		return -EINVAL;
	ctx->inst = inst;

	ctx->inst->build_deflate_ctx(ctx->comp_ctx);

	return 0;
}

static void qat_comp_alg_exit_tfm(struct crypto_acomp *acomp_tfm)
{
	struct crypto_tfm *tfm = crypto_acomp_tfm(acomp_tfm);
	struct qat_compression_ctx *ctx = crypto_tfm_ctx(tfm);

	qat_compression_put_instance(ctx->inst);
	memset(ctx, 0, sizeof(*ctx));
}

static int qat_comp_alg_compress_decompress(struct acomp_req *areq, enum direction dir,
					    unsigned int shdr, unsigned int sftr,
					    unsigned int dhdr, unsigned int dftr)
{
	struct qat_compression_req *qat_req = acomp_request_ctx(areq);
	struct crypto_acomp *acomp_tfm = crypto_acomp_reqtfm(areq);
	struct crypto_tfm *tfm = crypto_acomp_tfm(acomp_tfm);
	struct qat_compression_ctx *ctx = crypto_tfm_ctx(tfm);
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

	if (areq->dst && !dlen)
		return -EINVAL;

	qat_req->dst.is_null = false;

	/* Handle acomp requests that require the allocation of a destination
	 * buffer. The size of the destination buffer is double the source
	 * buffer (rounded up to the size of a page) to fit the decompressed
	 * output or an expansion on the data for compression.
	 */
	if (!areq->dst) {
		qat_req->dst.is_null = true;

		dlen = round_up(2 * slen, PAGE_SIZE);
		areq->dst = sgl_alloc(dlen, f, NULL);
		if (!areq->dst)
			return -ENOMEM;

		dlen -= dhdr + dftr;
		areq->dlen = dlen;
		qat_req->dst.resubmitted = false;
	}

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

static struct acomp_alg qat_acomp[] = { {
	.base = {
		.cra_name = "deflate",
		.cra_driver_name = "qat_deflate",
		.cra_priority = 4001,
		.cra_flags = CRYPTO_ALG_ASYNC | CRYPTO_ALG_ALLOCATES_MEMORY,
		.cra_ctxsize = sizeof(struct qat_compression_ctx),
		.cra_module = THIS_MODULE,
	},
	.init = qat_comp_alg_init_tfm,
	.exit = qat_comp_alg_exit_tfm,
	.compress = qat_comp_alg_compress,
	.decompress = qat_comp_alg_decompress,
	.dst_free = sgl_free,
	.reqsize = sizeof(struct qat_compression_req),
}};

int qat_comp_algs_register(void)
{
	int ret = 0;

	mutex_lock(&algs_lock);
	if (++active_devs == 1)
		ret = crypto_register_acomps(qat_acomp, ARRAY_SIZE(qat_acomp));
	mutex_unlock(&algs_lock);
	return ret;
}

void qat_comp_algs_unregister(void)
{
	mutex_lock(&algs_lock);
	if (--active_devs == 0)
		crypto_unregister_acomps(qat_acomp, ARRAY_SIZE(qat_acomp));
	mutex_unlock(&algs_lock);
}
