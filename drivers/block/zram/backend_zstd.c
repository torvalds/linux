// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/zstd.h>

#include "backend_zstd.h"

struct zstd_ctx {
	zstd_cctx *cctx;
	zstd_dctx *dctx;
	zstd_parameters cprm;
	void *cctx_mem;
	void *dctx_mem;
	s32 level;
};

static void zstd_destroy(struct zcomp_ctx *ctx)
{
	struct zstd_ctx *zctx = ctx->context;

	if (!zctx)
		return;

	vfree(zctx->cctx_mem);
	vfree(zctx->dctx_mem);
	kfree(zctx);
}

static int zstd_create(struct zcomp_params *params, struct zcomp_ctx *ctx)
{
	struct zstd_ctx *zctx;
	zstd_parameters prm;
	size_t sz;

	zctx = kzalloc(sizeof(*zctx), GFP_KERNEL);
	if (!zctx)
		return -ENOMEM;

	ctx->context = zctx;
	if (params->level != ZCOMP_PARAM_NO_LEVEL)
		zctx->level = params->level;
	else
		zctx->level = zstd_default_clevel();

	prm = zstd_get_params(zctx->level, PAGE_SIZE);
	zctx->cprm = zstd_get_params(zctx->level, PAGE_SIZE);
	sz = zstd_cctx_workspace_bound(&prm.cParams);
	zctx->cctx_mem = vzalloc(sz);
	if (!zctx->cctx_mem)
		goto error;

	zctx->cctx = zstd_init_cctx(zctx->cctx_mem, sz);
	if (!zctx->cctx)
		goto error;

	sz = zstd_dctx_workspace_bound();
	zctx->dctx_mem = vzalloc(sz);
	if (!zctx->dctx_mem)
		goto error;

	zctx->dctx = zstd_init_dctx(zctx->dctx_mem, sz);
	if (!zctx->dctx)
		goto error;

	return 0;

error:
	zstd_destroy(ctx);
	return -EINVAL;
}

static int zstd_compress(struct zcomp_ctx *ctx, struct zcomp_req *req)
{
	struct zstd_ctx *zctx = ctx->context;
	size_t ret;

	ret = zstd_compress_cctx(zctx->cctx, req->dst, req->dst_len,
				 req->src, req->src_len, &zctx->cprm);
	if (zstd_is_error(ret))
		return -EINVAL;
	req->dst_len = ret;
	return 0;
}

static int zstd_decompress(struct zcomp_ctx *ctx, struct zcomp_req *req)
{
	struct zstd_ctx *zctx = ctx->context;
	size_t ret;

	ret = zstd_decompress_dctx(zctx->dctx, req->dst, req->dst_len,
				   req->src, req->src_len);
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
