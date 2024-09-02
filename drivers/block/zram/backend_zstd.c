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
	zstd_custom_mem custom_mem;
	zstd_cdict *cdict;
	zstd_ddict *ddict;
	zstd_parameters cprm;
};

/*
 * For C/D dictionaries we need to provide zstd with zstd_custom_mem,
 * which zstd uses internally to allocate/free memory when needed.
 *
 * This means that allocator.customAlloc() can be called from zcomp_compress()
 * under local-lock (per-CPU compression stream), in which case we must use
 * GFP_ATOMIC.
 *
 * Another complication here is that we can be configured as a swap device.
 */
static void *zstd_custom_alloc(void *opaque, size_t size)
{
	if (!preemptible())
		return kvzalloc(size, GFP_ATOMIC);

	return kvzalloc(size, __GFP_KSWAPD_RECLAIM | __GFP_NOWARN);
}

static void zstd_custom_free(void *opaque, void *address)
{
	kvfree(address);
}

static void zstd_release_params(struct zcomp_params *params)
{
	struct zstd_params *zp = params->drv_data;

	params->drv_data = NULL;
	if (!zp)
		return;

	zstd_free_cdict(zp->cdict);
	zstd_free_ddict(zp->ddict);
	kfree(zp);
}

static int zstd_setup_params(struct zcomp_params *params)
{
	zstd_compression_parameters prm;
	struct zstd_params *zp;

	zp = kzalloc(sizeof(*zp), GFP_KERNEL);
	if (!zp)
		return -ENOMEM;

	params->drv_data = zp;
	if (params->level == ZCOMP_PARAM_NO_LEVEL)
		params->level = zstd_default_clevel();

	zp->cprm = zstd_get_params(params->level, PAGE_SIZE);

	zp->custom_mem.customAlloc = zstd_custom_alloc;
	zp->custom_mem.customFree = zstd_custom_free;

	prm = zstd_get_cparams(params->level, PAGE_SIZE,
			       params->dict_sz);

	zp->cdict = zstd_create_cdict_byreference(params->dict,
						  params->dict_sz,
						  prm,
						  zp->custom_mem);
	if (!zp->cdict)
		goto error;

	zp->ddict = zstd_create_ddict_byreference(params->dict,
						  params->dict_sz,
						  zp->custom_mem);
	if (!zp->ddict)
		goto error;

	return 0;

error:
	zstd_release_params(params);
	return -EINVAL;
}

static void zstd_destroy(struct zcomp_ctx *ctx)
{
	struct zstd_ctx *zctx = ctx->context;

	if (!zctx)
		return;

	/*
	 * If ->cctx_mem and ->dctx_mem were allocated then we didn't use
	 * C/D dictionary and ->cctx / ->dctx were "embedded" into these
	 * buffers.
	 *
	 * If otherwise then we need to explicitly release ->cctx / ->dctx.
	 */
	if (zctx->cctx_mem)
		vfree(zctx->cctx_mem);
	else
		zstd_free_cctx(zctx->cctx);

	if (zctx->dctx_mem)
		vfree(zctx->dctx_mem);
	else
		zstd_free_dctx(zctx->dctx);

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
	if (params->dict_sz == 0) {
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
	} else {
		struct zstd_params *zp = params->drv_data;

		zctx->cctx = zstd_create_cctx_advanced(zp->custom_mem);
		if (!zctx->cctx)
			goto error;

		zctx->dctx = zstd_create_dctx_advanced(zp->custom_mem);
		if (!zctx->dctx)
			goto error;
	}

	return 0;

error:
	zstd_release_params(params);
	zstd_destroy(ctx);
	return -EINVAL;
}

static int zstd_compress(struct zcomp_params *params, struct zcomp_ctx *ctx,
			 struct zcomp_req *req)
{
	struct zstd_params *zp = params->drv_data;
	struct zstd_ctx *zctx = ctx->context;
	size_t ret;

	if (params->dict_sz == 0)
		ret = zstd_compress_cctx(zctx->cctx, req->dst, req->dst_len,
					 req->src, req->src_len, &zp->cprm);
	else
		ret = zstd_compress_using_cdict(zctx->cctx, req->dst,
						req->dst_len, req->src,
						req->src_len,
						zp->cdict);
	if (zstd_is_error(ret))
		return -EINVAL;
	req->dst_len = ret;
	return 0;
}

static int zstd_decompress(struct zcomp_params *params, struct zcomp_ctx *ctx,
			   struct zcomp_req *req)
{
	struct zstd_params *zp = params->drv_data;
	struct zstd_ctx *zctx = ctx->context;
	size_t ret;

	if (params->dict_sz == 0)
		ret = zstd_decompress_dctx(zctx->dctx, req->dst, req->dst_len,
					   req->src, req->src_len);
	else
		ret = zstd_decompress_using_ddict(zctx->dctx, req->dst,
						  req->dst_len, req->src,
						  req->src_len, zp->ddict);
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
