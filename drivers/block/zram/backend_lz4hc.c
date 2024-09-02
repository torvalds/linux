#include <linux/kernel.h>
#include <linux/lz4.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

#include "backend_lz4hc.h"

static void lz4hc_release_params(struct zcomp_params *params)
{
}

static int lz4hc_setup_params(struct zcomp_params *params)
{
	if (params->level == ZCOMP_PARAM_NO_LEVEL)
		params->level = LZ4HC_DEFAULT_CLEVEL;

	return 0;
}

static void lz4hc_destroy(struct zcomp_ctx *ctx)
{
	vfree(ctx->context);
}

static int lz4hc_create(struct zcomp_params *params, struct zcomp_ctx *ctx)
{
	ctx->context = vmalloc(LZ4HC_MEM_COMPRESS);
	if (!ctx->context)
		return -ENOMEM;

	return 0;
}

static int lz4hc_compress(struct zcomp_params *params, struct zcomp_ctx *ctx,
			  struct zcomp_req *req)
{
	int ret;

	ret = LZ4_compress_HC(req->src, req->dst, req->src_len, req->dst_len,
			      params->level, ctx->context);
	if (!ret)
		return -EINVAL;
	req->dst_len = ret;
	return 0;
}

static int lz4hc_decompress(struct zcomp_params *params, struct zcomp_ctx *ctx,
			    struct zcomp_req *req)
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
	.setup_params	= lz4hc_setup_params,
	.release_params	= lz4hc_release_params,
	.name		= "lz4hc",
};
