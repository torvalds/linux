#include <linux/kernel.h>
#include <linux/lz4.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

#include "backend_lz4hc.h"

struct lz4hc_ctx {
	void *mem;
	s32 level;
};

static void lz4hc_destroy(struct zcomp_ctx *ctx)
{
	struct lz4hc_ctx *zctx = ctx->context;

	if (!zctx)
		return;

	vfree(zctx->mem);
	kfree(zctx);
}

static int lz4hc_create(struct zcomp_params *params, struct zcomp_ctx *ctx)
{
	struct lz4hc_ctx *zctx;

	zctx = kzalloc(sizeof(*zctx), GFP_KERNEL);
	if (!zctx)
		return -ENOMEM;

	ctx->context = zctx;
	if (params->level != ZCOMP_PARAM_NO_LEVEL)
		zctx->level = params->level;
	else
		zctx->level = LZ4HC_DEFAULT_CLEVEL;

	zctx->mem = vmalloc(LZ4HC_MEM_COMPRESS);
	if (!zctx->mem)
		goto error;

	return 0;
error:
	lz4hc_destroy(ctx);
	return -EINVAL;
}

static int lz4hc_compress(struct zcomp_ctx *ctx, struct zcomp_req *req)
{
	struct lz4hc_ctx *zctx = ctx->context;
	int ret;

	ret = LZ4_compress_HC(req->src, req->dst, req->src_len, req->dst_len,
			      zctx->level, zctx->mem);
	if (!ret)
		return -EINVAL;
	req->dst_len = ret;
	return 0;
}

static int lz4hc_decompress(struct zcomp_ctx *ctx, struct zcomp_req *req)
{
	int ret;

	ret = LZ4_decompress_safe(req->src, req->dst, req->src_len,
				  req->dst_len);
	if (ret < 0)
		return -EINVAL;
	return 0;
}

const struct zcomp_ops backend_lz4hc = {
	.compress	= lz4hc_compress,
	.decompress	= lz4hc_decompress,
	.create_ctx	= lz4hc_create,
	.destroy_ctx	= lz4hc_destroy,
	.name		= "lz4hc",
};
