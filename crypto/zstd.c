// SPDX-License-Identifier: GPL-2.0-only
/*
 * Cryptographic API.
 *
 * Copyright (c) 2017-present, Facebook, Inc.
 */
#include <linux/crypto.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/net.h>
#include <linux/vmalloc.h>
#include <linux/zstd.h>
#include <crypto/internal/acompress.h>
#include <crypto/scatterwalk.h>


#define ZSTD_DEF_LEVEL		3
#define ZSTD_MAX_WINDOWLOG	18
#define ZSTD_MAX_SIZE		BIT(ZSTD_MAX_WINDOWLOG)

struct zstd_ctx {
	zstd_cctx *cctx;
	zstd_dctx *dctx;
	size_t wksp_size;
	zstd_parameters params;
	u8 wksp[] __aligned(8);
};

static DEFINE_MUTEX(zstd_stream_lock);

static void *zstd_alloc_stream(void)
{
	zstd_parameters params;
	struct zstd_ctx *ctx;
	size_t wksp_size;

	params = zstd_get_params(ZSTD_DEF_LEVEL, ZSTD_MAX_SIZE);

	wksp_size = max_t(size_t,
			  zstd_cstream_workspace_bound(&params.cParams),
			  zstd_dstream_workspace_bound(ZSTD_MAX_SIZE));
	if (!wksp_size)
		return ERR_PTR(-EINVAL);

	ctx = kvmalloc(sizeof(*ctx) + wksp_size, GFP_KERNEL);
	if (!ctx)
		return ERR_PTR(-ENOMEM);

	ctx->params = params;
	ctx->wksp_size = wksp_size;

	return ctx;
}

static void zstd_free_stream(void *ctx)
{
	kvfree(ctx);
}

static struct crypto_acomp_streams zstd_streams = {
	.alloc_ctx = zstd_alloc_stream,
	.free_ctx = zstd_free_stream,
};

static int zstd_init(struct crypto_acomp *acomp_tfm)
{
	int ret = 0;

	mutex_lock(&zstd_stream_lock);
	ret = crypto_acomp_alloc_streams(&zstd_streams);
	mutex_unlock(&zstd_stream_lock);

	return ret;
}

static void zstd_exit(struct crypto_acomp *acomp_tfm)
{
	crypto_acomp_free_streams(&zstd_streams);
}

static int zstd_compress_one(struct acomp_req *req, struct zstd_ctx *ctx,
			     const void *src, void *dst, unsigned int *dlen)
{
	size_t out_len;

	ctx->cctx = zstd_init_cctx(ctx->wksp, ctx->wksp_size);
	if (!ctx->cctx)
		return -EINVAL;

	out_len = zstd_compress_cctx(ctx->cctx, dst, req->dlen, src, req->slen,
				     &ctx->params);
	if (zstd_is_error(out_len))
		return -EINVAL;

	*dlen = out_len;

	return 0;
}

static int zstd_compress(struct acomp_req *req)
{
	struct crypto_acomp_stream *s;
	unsigned int pos, scur, dcur;
	unsigned int total_out = 0;
	bool data_available = true;
	zstd_out_buffer outbuf;
	struct acomp_walk walk;
	zstd_in_buffer inbuf;
	struct zstd_ctx *ctx;
	size_t pending_bytes;
	size_t num_bytes;
	int ret;

	s = crypto_acomp_lock_stream_bh(&zstd_streams);
	ctx = s->ctx;

	ret = acomp_walk_virt(&walk, req, true);
	if (ret)
		goto out;

	ctx->cctx = zstd_init_cstream(&ctx->params, 0, ctx->wksp, ctx->wksp_size);
	if (!ctx->cctx) {
		ret = -EINVAL;
		goto out;
	}

	do {
		dcur = acomp_walk_next_dst(&walk);
		if (!dcur) {
			ret = -ENOSPC;
			goto out;
		}

		outbuf.pos = 0;
		outbuf.dst = (u8 *)walk.dst.virt.addr;
		outbuf.size = dcur;

		do {
			scur = acomp_walk_next_src(&walk);
			if (dcur == req->dlen && scur == req->slen) {
				ret = zstd_compress_one(req, ctx, walk.src.virt.addr,
							walk.dst.virt.addr, &total_out);
				acomp_walk_done_src(&walk, scur);
				acomp_walk_done_dst(&walk, dcur);
				goto out;
			}

			if (scur) {
				inbuf.pos = 0;
				inbuf.src = walk.src.virt.addr;
				inbuf.size = scur;
			} else {
				data_available = false;
				break;
			}

			num_bytes = zstd_compress_stream(ctx->cctx, &outbuf, &inbuf);
			if (ZSTD_isError(num_bytes)) {
				ret = -EIO;
				goto out;
			}

			pending_bytes = zstd_flush_stream(ctx->cctx, &outbuf);
			if (ZSTD_isError(pending_bytes)) {
				ret = -EIO;
				goto out;
			}
			acomp_walk_done_src(&walk, inbuf.pos);
		} while (dcur != outbuf.pos);

		total_out += outbuf.pos;
		acomp_walk_done_dst(&walk, dcur);
	} while (data_available);

	pos = outbuf.pos;
	num_bytes = zstd_end_stream(ctx->cctx, &outbuf);
	if (ZSTD_isError(num_bytes))
		ret = -EIO;
	else
		total_out += (outbuf.pos - pos);

out:
	if (ret)
		req->dlen = 0;
	else
		req->dlen = total_out;

	crypto_acomp_unlock_stream_bh(s);

	return ret;
}

static int zstd_decompress_one(struct acomp_req *req, struct zstd_ctx *ctx,
			       const void *src, void *dst, unsigned int *dlen)
{
	size_t out_len;

	ctx->dctx = zstd_init_dctx(ctx->wksp, ctx->wksp_size);
	if (!ctx->dctx)
		return -EINVAL;

	out_len = zstd_decompress_dctx(ctx->dctx, dst, req->dlen, src, req->slen);
	if (zstd_is_error(out_len))
		return -EINVAL;

	*dlen = out_len;

	return 0;
}

static int zstd_decompress(struct acomp_req *req)
{
	struct crypto_acomp_stream *s;
	unsigned int total_out = 0;
	unsigned int scur, dcur;
	zstd_out_buffer outbuf;
	struct acomp_walk walk;
	zstd_in_buffer inbuf;
	struct zstd_ctx *ctx;
	size_t pending_bytes;
	int ret;

	s = crypto_acomp_lock_stream_bh(&zstd_streams);
	ctx = s->ctx;

	ret = acomp_walk_virt(&walk, req, true);
	if (ret)
		goto out;

	ctx->dctx = zstd_init_dstream(ZSTD_MAX_SIZE, ctx->wksp, ctx->wksp_size);
	if (!ctx->dctx) {
		ret = -EINVAL;
		goto out;
	}

	do {
		scur = acomp_walk_next_src(&walk);
		if (scur) {
			inbuf.pos = 0;
			inbuf.size = scur;
			inbuf.src = walk.src.virt.addr;
		} else {
			break;
		}

		do {
			dcur = acomp_walk_next_dst(&walk);
			if (dcur == req->dlen && scur == req->slen) {
				ret = zstd_decompress_one(req, ctx, walk.src.virt.addr,
							  walk.dst.virt.addr, &total_out);
				acomp_walk_done_dst(&walk, dcur);
				acomp_walk_done_src(&walk, scur);
				goto out;
			}

			if (!dcur) {
				ret = -ENOSPC;
				goto out;
			}

			outbuf.pos = 0;
			outbuf.dst = (u8 *)walk.dst.virt.addr;
			outbuf.size = dcur;

			pending_bytes = zstd_decompress_stream(ctx->dctx, &outbuf, &inbuf);
			if (ZSTD_isError(pending_bytes)) {
				ret = -EIO;
				goto out;
			}

			total_out += outbuf.pos;

			acomp_walk_done_dst(&walk, outbuf.pos);
		} while (inbuf.pos != scur);

		acomp_walk_done_src(&walk, scur);
	} while (ret == 0);

out:
	if (ret)
		req->dlen = 0;
	else
		req->dlen = total_out;

	crypto_acomp_unlock_stream_bh(s);

	return ret;
}

static struct acomp_alg zstd_acomp = {
	.base = {
		.cra_name = "zstd",
		.cra_driver_name = "zstd-generic",
		.cra_flags = CRYPTO_ALG_REQ_VIRT,
		.cra_module = THIS_MODULE,
	},
	.init = zstd_init,
	.exit = zstd_exit,
	.compress = zstd_compress,
	.decompress = zstd_decompress,
};

static int __init zstd_mod_init(void)
{
	return crypto_register_acomp(&zstd_acomp);
}

static void __exit zstd_mod_fini(void)
{
	crypto_unregister_acomp(&zstd_acomp);
}

module_init(zstd_mod_init);
module_exit(zstd_mod_fini);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Zstd Compression Algorithm");
MODULE_ALIAS_CRYPTO("zstd");
