// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/zstd.h>

#include "backend_zstd.h"

struct zstd_ctx {
	zstd_cctx *cctx;
	zstd_dctx *dctx;
	void *cctx_mem;
	void *dctx_mem;
	s32 level;
};

static void zstd_destroy(void *ctx)
{
	struct zstd_ctx *zctx = ctx;

	vfree(zctx->cctx_mem);
	vfree(zctx->dctx_mem);
	kfree(zctx);
}

static void *zstd_create(void)
{
	zstd_parameters params;
	struct zstd_ctx *ctx;
	size_t sz;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return NULL;

	ctx->level = zstd_default_clevel();
	params = zstd_get_params(ctx->level, 0);
	sz = zstd_cctx_workspace_bound(&params.cParams);
	ctx->cctx_mem = vzalloc(sz);
	if (!ctx->cctx_mem)
		goto error;

	ctx->cctx = zstd_init_cctx(ctx->cctx_mem, sz);
	if (!ctx->cctx)
		goto error;

	sz = zstd_dctx_workspace_bound();
	ctx->dctx_mem = vzalloc(sz);
	if (!ctx->dctx_mem)
		goto error;

	ctx->dctx = zstd_init_dctx(ctx->dctx_mem, sz);
	if (!ctx->dctx)
		goto error;

	return ctx;

error:
	zstd_destroy(ctx);
	return NULL;
}

static int zstd_compress(void *ctx, const unsigned char *src, size_t src_len,
			 unsigned char *dst, size_t *dst_len)
{
	struct zstd_ctx *zctx = ctx;
	const zstd_parameters params = zstd_get_params(zctx->level, 0);
	size_t ret;

	ret = zstd_compress_cctx(zctx->cctx, dst, *dst_len,
				 src, src_len, &params);
	if (zstd_is_error(ret))
		return -EINVAL;
	*dst_len = ret;
	return 0;
}

static int zstd_decompress(void *ctx, const unsigned char *src, size_t src_len,
			   unsigned char *dst, size_t dst_len)
{
	struct zstd_ctx *zctx = ctx;
	size_t ret;

	ret = zstd_decompress_dctx(zctx->dctx, dst, dst_len, src, src_len);
	if (zstd_is_error(ret))
		return -EINVAL;
	return 0;
}

const struct zcomp_ops backend_zstd = {
	.compress	= zstd_compress,
	.decompress	= zstd_decompress,
	.create_ctx	= zstd_create,
	.destroy_ctx	= zstd_destroy,
	.name		= "zstd",
};
