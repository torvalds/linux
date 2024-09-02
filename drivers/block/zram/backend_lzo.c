// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/lzo.h>

#include "backend_lzo.h"

static int lzo_create(struct zcomp_params *params, struct zcomp_ctx *ctx)
{
	ctx->context = kzalloc(LZO1X_MEM_COMPRESS, GFP_KERNEL);
	if (!ctx->context)
		return -ENOMEM;
	return 0;
}

static void lzo_destroy(struct zcomp_ctx *ctx)
{
	kfree(ctx->context);
}

static int lzo_compress(struct zcomp_ctx *ctx, struct zcomp_req *req)
{
	int ret;

	ret = lzo1x_1_compress(req->src, req->src_len, req->dst,
			       &req->dst_len, ctx->context);
	return ret == LZO_E_OK ? 0 : ret;
}

static int lzo_decompress(struct zcomp_ctx *ctx, struct zcomp_req *req)
{
	int ret;

	ret = lzo1x_decompress_safe(req->src, req->src_len,
				    req->dst, &req->dst_len);
	return ret == LZO_E_OK ? 0 : ret;
}

const struct zcomp_ops backend_lzo = {
	.compress	= lzo_compress,
	.decompress	= lzo_decompress,
	.create_ctx	= lzo_create,
	.destroy_ctx	= lzo_destroy,
	.name		= "lzo",
};
