#include <linux/kernel.h>
#include <linux/lz4.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

#include "backend_lz4.h"

struct lz4_ctx {
	void *mem;
	s32 level;
};

static void lz4_destroy(struct zcomp_ctx *ctx)
{
	struct lz4_ctx *zctx = ctx->context;

	if (!zctx)
		return;

	vfree(zctx->mem);
	kfree(zctx);
}

static int lz4_create(struct zcomp_params *params, struct zcomp_ctx *ctx)
{
	struct lz4_ctx *zctx;

	zctx = kzalloc(sizeof(*zctx), GFP_KERNEL);
	if (!zctx)
		return -ENOMEM;

	ctx->context = zctx;
	if (params->level != ZCOMP_PARAM_NO_LEVEL)
		zctx->level = params->level;
	else
		zctx->level = LZ4_ACCELERATION_DEFAULT;

	zctx->mem = vmalloc(LZ4_MEM_COMPRESS);
	if (!zctx->mem)
		goto error;

	return 0;
error:
	lz4_destroy(ctx);
	return -EINVAL;
}

static int lz4_compress(struct zcomp_ctx *ctx, struct zcomp_req *req)
{
	struct lz4_ctx *zctx = ctx->context;
	int ret;

	ret = LZ4_compress_fast(req->src, req->dst, req->src_len,
				req->dst_len, zctx->level, zctx->mem);
	if (!ret)
		return -EINVAL;
	req->dst_len = ret;
	return 0;
}

static int lz4_decompress(struct zcomp_ctx *ctx, struct zcomp_req *req)
{
	int ret;

	ret = LZ4_decompress_safe(req->src, req->dst, req->src_len,
				  req->dst_len);
	if (ret < 0)
		return -EINVAL;
	return 0;
}

const struct zcomp_ops backend_lz4 = {
	.compress	= lz4_compress,
	.decompress	= lz4_decompress,
	.create_ctx	= lz4_create,
	.destroy_ctx	= lz4_destroy,
	.name		= "lz4",
};
