// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/sw842.h>
#include <linux/vmalloc.h>

#include "backend_842.h"

static void destroy_842(struct zcomp_ctx *ctx)
{
	kfree(ctx->context);
}

static int create_842(struct zcomp_params *params, struct zcomp_ctx *ctx)
{
	ctx->context = kmalloc(SW842_MEM_COMPRESS, GFP_KERNEL);
	if (!ctx->context)
		return -ENOMEM;
	return 0;
}

static int compress_842(struct zcomp_ctx *ctx, struct zcomp_req *req)
{
	unsigned int dlen = req->dst_len;
	int ret;

	ret = sw842_compress(req->src, req->src_len, req->dst, &dlen,
			     ctx->context);
	if (ret == 0)
		req->dst_len = dlen;
	return ret;
}

static int decompress_842(struct zcomp_ctx *ctx, struct zcomp_req *req)
{
	unsigned int dlen = req->dst_len;

	return sw842_decompress(req->src, req->src_len, req->dst, &dlen);
}

const struct zcomp_ops backend_842 = {
	.compress	= compress_842,
	.decompress	= decompress_842,
	.create_ctx	= create_842,
	.destroy_ctx	= destroy_842,
	.name		= "842",
};
