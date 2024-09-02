#include <linux/kernel.h>
#include <linux/lz4.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

#include "backend_lz4.h"

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
	vfree(ctx->context);
}

static int lz4_create(struct zcomp_params *params, struct zcomp_ctx *ctx)
{
	ctx->context = vmalloc(LZ4_MEM_COMPRESS);
	if (!ctx->context)
		return -ENOMEM;

	return 0;
}

static int lz4_compress(struct zcomp_params *params, struct zcomp_ctx *ctx,
			struct zcomp_req *req)
{
	int ret;

	ret = LZ4_compress_fast(req->src, req->dst, req->src_len,
				req->dst_len, params->level, ctx->context);
	if (!ret)
		return -EINVAL;
	req->dst_len = ret;
	return 0;
}

static int lz4_decompress(struct zcomp_params *params, struct zcomp_ctx *ctx,
			  struct zcomp_req *req)
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
	.setup_params	= lz4_setup_params,
	.release_params	= lz4_release_params,
	.name		= "lz4",
};
