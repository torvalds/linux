// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/lzo.h>

#include "backend_lzorle.h"

static void lzorle_release_params(struct zcomp_params *params)
{
}

static int lzorle_setup_params(struct zcomp_params *params)
{
	return 0;
}

static int lzorle_create(struct zcomp_params *params, struct zcomp_ctx *ctx)
{
	ctx->context = kzalloc(LZO1X_MEM_COMPRESS, GFP_KERNEL);
	if (!ctx->context)
		return -ENOMEM;
	return 0;
}

static void lzorle_destroy(struct zcomp_ctx *ctx)
{
	kfree(ctx->context);
}

static int lzorle_compress(struct zcomp_params *params, struct zcomp_ctx *ctx,
			   struct zcomp_req *req)
{
	int ret;

	ret = lzorle1x_1_compress(req->src, req->src_len, req->dst,
				  &req->dst_len, ctx->context);
	return ret == LZO_E_OK ? 0 : ret;
}

static int lzorle_decompress(struct zcomp_params *params, struct zcomp_ctx *ctx,
			     struct zcomp_req *req)
{
	int ret;

	ret = lzo1x_decompress_safe(req->src, req->src_len,
				    req->dst, &req->dst_len);
	return ret == LZO_E_OK ? 0 : ret;
}

const struct zcomp_ops backend_lzorle = {
	.compress	= lzorle_compress,
	.decompress	= lzorle_decompress,
	.create_ctx	= lzorle_create,
	.destroy_ctx	= lzorle_destroy,
	.setup_params	= lzorle_setup_params,
	.release_params	= lzorle_release_params,
	.name		= "lzo-rle",
};
