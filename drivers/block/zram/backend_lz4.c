#include <linux/kernel.h>
#include <linux/lz4.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

#include "backend_lz4.h"

struct lz4_ctx {
	void *mem;

	LZ4_streamDecode_t *dstrm;
	LZ4_stream_t *cstrm;
};

static void lz4_release_params(struct zcomp_params *params)
{
}

static int lz4_setup_params(struct zcomp_params *params)
{
	if (params->level == ZCOMP_PARAM_NO_LEVEL)
		params->level = LZ4_ACCELERATION_DEFAULT;

	return 0;
}

static void lz4_destroy(struct zcomp_ctx *ctx)
{
	struct lz4_ctx *zctx = ctx->context;

	if (!zctx)
		return;

	vfree(zctx->mem);
	kfree(zctx->dstrm);
	kfree(zctx->cstrm);
	kfree(zctx);
}

static int lz4_create(struct zcomp_params *params, struct zcomp_ctx *ctx)
{
	struct lz4_ctx *zctx;

	zctx = kzalloc(sizeof(*zctx), GFP_KERNEL);
	if (!zctx)
		return -ENOMEM;

	ctx->context = zctx;
	if (params->dict_sz == 0) {
		zctx->mem = vmalloc(LZ4_MEM_COMPRESS);
		if (!zctx->mem)
			goto error;
	} else {
		zctx->dstrm = kzalloc(sizeof(*zctx->dstrm), GFP_KERNEL);
		if (!zctx->dstrm)
			goto error;

		zctx->cstrm = kzalloc(sizeof(*zctx->cstrm), GFP_KERNEL);
		if (!zctx->cstrm)
			goto error;
	}

	return 0;

error:
	lz4_destroy(ctx);
	return -ENOMEM;
}

static int lz4_compress(struct zcomp_params *params, struct zcomp_ctx *ctx,
			struct zcomp_req *req)
{
	struct lz4_ctx *zctx = ctx->context;
	int ret;

	if (!zctx->cstrm) {
		ret = LZ4_compress_fast(req->src, req->dst, req->src_len,
					req->dst_len, params->level,
					zctx->mem);
	} else {
		/* Cstrm needs to be reset */
		ret = LZ4_loadDict(zctx->cstrm, params->dict, params->dict_sz);
		if (ret != params->dict_sz)
			return -EINVAL;
		ret = LZ4_compress_fast_continue(zctx->cstrm, req->src,
						 req->dst, req->src_len,
						 req->dst_len, params->level);
	}
	if (!ret)
		return -EINVAL;
	req->dst_len = ret;
	return 0;
}

static int lz4_decompress(struct zcomp_params *params, struct zcomp_ctx *ctx,
			  struct zcomp_req *req)
{
	struct lz4_ctx *zctx = ctx->context;
	int ret;

	if (!zctx->dstrm) {
		ret = LZ4_decompress_safe(req->src, req->dst, req->src_len,
					  req->dst_len);
	} else {
		/* Dstrm needs to be reset */
		ret = LZ4_setStreamDecode(zctx->dstrm, params->dict,
					  params->dict_sz);
		if (!ret)
			return -EINVAL;
		ret = LZ4_decompress_safe_continue(zctx->dstrm, req->src,
						   req->dst, req->src_len,
						   req->dst_len);
	}
	if (ret < 0)
		return -EINVAL;
	return 0;
}

const struct zcomp_ops backend_lz4 = {
	.compress	= lz4_compress,
	.decompress	= lz4_decompress,
	.create_ctx	= lz4_create,
	.destroy_ctx	= lz4_destroy,
	.setup_params	= lz4_setup_params,
	.release_params	= lz4_release_params,
	.name		= "lz4",
};
