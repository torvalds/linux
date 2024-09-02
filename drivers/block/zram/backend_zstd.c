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
};

struct zstd_params {
	zstd_parameters cprm;
};

static void zstd_release_params(struct zcomp_params *params)
{
	kfree(params->drv_data);
}

static int zstd_setup_params(struct zcomp_params *params)
{
	struct zstd_params *zp;

	zp = kzalloc(sizeof(*zp), GFP_KERNEL);
	if (!zp)
		return -ENOMEM;

	if (params->level == ZCOMP_PARAM_NO_LEVEL)
		params->level = zstd_default_clevel();

	zp->cprm = zstd_get_params(params->level, PAGE_SIZE);
	params->drv_data = zp;

	return 0;
}

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
	prm = zstd_get_params(params->level, PAGE_SIZE);
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

static int zstd_compress(struct zcomp_params *params, struct zcomp_ctx *ctx,
			 struct zcomp_req *req)
{
	struct zstd_params *zp = params->drv_data;
	struct zstd_ctx *zctx = ctx->context;
	size_t ret;

	ret = zstd_compress_cctx(zctx->cctx, req->dst, req->dst_len,
				 req->src, req->src_len, &zp->cprm);
	if (zstd_is_error(ret))
		return -EINVAL;
	req->dst_len = ret;
	return 0;
}

static int zstd_decompress(struct zcomp_params *params, struct zcomp_ctx *ctx,
			   struct zcomp_req *req)
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
	.setup_params	= zstd_setup_params,
	.release_params	= zstd_release_params,
	.name		= "zstd",
};
