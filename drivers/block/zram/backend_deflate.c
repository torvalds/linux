// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/zlib.h>

#include "backend_deflate.h"

/* Use the same value as crypto API */
#define DEFLATE_DEF_WINBITS		11
#define DEFLATE_DEF_MEMLEVEL		MAX_MEM_LEVEL

struct deflate_ctx {
	struct z_stream_s cctx;
	struct z_stream_s dctx;
	s32 level;
};

static void deflate_destroy(struct zcomp_ctx *ctx)
{
	struct deflate_ctx *zctx = ctx->context;

	if (!zctx)
		return;

	if (zctx->cctx.workspace) {
		zlib_deflateEnd(&zctx->cctx);
		vfree(zctx->cctx.workspace);
	}
	if (zctx->dctx.workspace) {
		zlib_inflateEnd(&zctx->dctx);
		vfree(zctx->dctx.workspace);
	}
	kfree(zctx);
}

static int deflate_create(struct zcomp_params *params, struct zcomp_ctx *ctx)
{
	struct deflate_ctx *zctx;
	size_t sz;
	int ret;

	zctx = kzalloc(sizeof(*zctx), GFP_KERNEL);
	if (!zctx)
		return -ENOMEM;

	ctx->context = zctx;
	if (params->level != ZCOMP_PARAM_NO_LEVEL)
		zctx->level = params->level;
	else
		zctx->level = Z_DEFAULT_COMPRESSION;

	sz = zlib_deflate_workspacesize(-DEFLATE_DEF_WINBITS, MAX_MEM_LEVEL);
	zctx->cctx.workspace = vzalloc(sz);
	if (!zctx->cctx.workspace)
		goto error;

	ret = zlib_deflateInit2(&zctx->cctx, zctx->level, Z_DEFLATED,
				-DEFLATE_DEF_WINBITS, DEFLATE_DEF_MEMLEVEL,
				Z_DEFAULT_STRATEGY);
	if (ret != Z_OK)
		goto error;

	sz = zlib_inflate_workspacesize();
	zctx->dctx.workspace = vzalloc(sz);
	if (!zctx->dctx.workspace)
		goto error;

	ret = zlib_inflateInit2(&zctx->dctx, -DEFLATE_DEF_WINBITS);
	if (ret != Z_OK)
		goto error;

	return 0;

error:
	deflate_destroy(ctx);
	return -EINVAL;
}

static int deflate_compress(struct zcomp_ctx *ctx, struct zcomp_req *req)
{
	struct deflate_ctx *zctx = ctx->context;
	struct z_stream_s *deflate;
	int ret;

	deflate = &zctx->cctx;
	ret = zlib_deflateReset(deflate);
	if (ret != Z_OK)
		return -EINVAL;

	deflate->next_in = (u8 *)req->src;
	deflate->avail_in = req->src_len;
	deflate->next_out = (u8 *)req->dst;
	deflate->avail_out = req->dst_len;

	ret = zlib_deflate(deflate, Z_FINISH);
	if (ret != Z_STREAM_END)
		return -EINVAL;

	req->dst_len = deflate->total_out;
	return 0;
}

static int deflate_decompress(struct zcomp_ctx *ctx, struct zcomp_req *req)
{
	struct deflate_ctx *zctx = ctx->context;
	struct z_stream_s *inflate;
	int ret;

	inflate = &zctx->dctx;

	ret = zlib_inflateReset(inflate);
	if (ret != Z_OK)
		return -EINVAL;

	inflate->next_in = (u8 *)req->src;
	inflate->avail_in = req->src_len;
	inflate->next_out = (u8 *)req->dst;
	inflate->avail_out = req->dst_len;

	ret = zlib_inflate(inflate, Z_SYNC_FLUSH);
	if (ret != Z_STREAM_END)
		return -EINVAL;

	return 0;
}

const struct zcomp_ops backend_deflate = {
	.compress	= deflate_compress,
	.decompress	= deflate_decompress,
	.create_ctx	= deflate_create,
	.destroy_ctx	= deflate_destroy,
	.name		= "deflate",
};
