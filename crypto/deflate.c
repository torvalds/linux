// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Cryptographic API.
 *
 * Deflate algorithm (RFC 1951), implemented here primarily for use
 * by IPCOMP (RFC 3173 & RFC 2394).
 *
 * Copyright (c) 2003 James Morris <jmorris@intercode.com.au>
 *
 * FIXME: deflate transforms will require up to a total of about 436k of kernel
 * memory on i386 (390k for compression, the rest for decompression), as the
 * current zlib kernel code uses a worst case pre-allocation system by default.
 * This needs to be fixed so that the amount of memory required is properly
 * related to the  winbits and memlevel parameters.
 *
 * The default winbits of 11 should suit most packets, and it may be something
 * to configure on a per-tfm basis in the future.
 *
 * Currently, compression history is not maintained between tfm calls, as
 * it is not needed for IPCOMP and keeps the code simpler.  It can be
 * implemented if someone wants it.
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/crypto.h>
#include <linux/zlib.h>
#include <linux/vmalloc.h>
#include <linux/interrupt.h>
#include <linux/mm.h>
#include <linux/net.h>
#include <crypto/internal/scompress.h>

#define DEFLATE_DEF_LEVEL		Z_DEFAULT_COMPRESSION
#define DEFLATE_DEF_WINBITS		11
#define DEFLATE_DEF_MEMLEVEL		MAX_MEM_LEVEL

struct deflate_ctx {
	struct z_stream_s comp_stream;
	struct z_stream_s decomp_stream;
};

static int deflate_comp_init(struct deflate_ctx *ctx, int format)
{
	int ret = 0;
	struct z_stream_s *stream = &ctx->comp_stream;

	stream->workspace = vzalloc(zlib_deflate_workspacesize(
				    MAX_WBITS, MAX_MEM_LEVEL));
	if (!stream->workspace) {
		ret = -ENOMEM;
		goto out;
	}
	if (format)
		ret = zlib_deflateInit(stream, 3);
	else
		ret = zlib_deflateInit2(stream, DEFLATE_DEF_LEVEL, Z_DEFLATED,
					-DEFLATE_DEF_WINBITS,
					DEFLATE_DEF_MEMLEVEL,
					Z_DEFAULT_STRATEGY);
	if (ret != Z_OK) {
		ret = -EINVAL;
		goto out_free;
	}
out:
	return ret;
out_free:
	vfree(stream->workspace);
	goto out;
}

static int deflate_decomp_init(struct deflate_ctx *ctx, int format)
{
	int ret = 0;
	struct z_stream_s *stream = &ctx->decomp_stream;

	stream->workspace = vzalloc(zlib_inflate_workspacesize());
	if (!stream->workspace) {
		ret = -ENOMEM;
		goto out;
	}
	if (format)
		ret = zlib_inflateInit(stream);
	else
		ret = zlib_inflateInit2(stream, -DEFLATE_DEF_WINBITS);
	if (ret != Z_OK) {
		ret = -EINVAL;
		goto out_free;
	}
out:
	return ret;
out_free:
	vfree(stream->workspace);
	goto out;
}

static void deflate_comp_exit(struct deflate_ctx *ctx)
{
	zlib_deflateEnd(&ctx->comp_stream);
	vfree(ctx->comp_stream.workspace);
}

static void deflate_decomp_exit(struct deflate_ctx *ctx)
{
	zlib_inflateEnd(&ctx->decomp_stream);
	vfree(ctx->decomp_stream.workspace);
}

static int __deflate_init(void *ctx, int format)
{
	int ret;

	ret = deflate_comp_init(ctx, format);
	if (ret)
		goto out;
	ret = deflate_decomp_init(ctx, format);
	if (ret)
		deflate_comp_exit(ctx);
out:
	return ret;
}

static void *gen_deflate_alloc_ctx(struct crypto_scomp *tfm, int format)
{
	struct deflate_ctx *ctx;
	int ret;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return ERR_PTR(-ENOMEM);

	ret = __deflate_init(ctx, format);
	if (ret) {
		kfree(ctx);
		return ERR_PTR(ret);
	}

	return ctx;
}

static void *deflate_alloc_ctx(struct crypto_scomp *tfm)
{
	return gen_deflate_alloc_ctx(tfm, 0);
}

static void *zlib_deflate_alloc_ctx(struct crypto_scomp *tfm)
{
	return gen_deflate_alloc_ctx(tfm, 1);
}

static int deflate_init(struct crypto_tfm *tfm)
{
	struct deflate_ctx *ctx = crypto_tfm_ctx(tfm);

	return __deflate_init(ctx, 0);
}

static void __deflate_exit(void *ctx)
{
	deflate_comp_exit(ctx);
	deflate_decomp_exit(ctx);
}

static void deflate_free_ctx(struct crypto_scomp *tfm, void *ctx)
{
	__deflate_exit(ctx);
	kfree_sensitive(ctx);
}

static void deflate_exit(struct crypto_tfm *tfm)
{
	struct deflate_ctx *ctx = crypto_tfm_ctx(tfm);

	__deflate_exit(ctx);
}

static int __deflate_compress(const u8 *src, unsigned int slen,
			      u8 *dst, unsigned int *dlen, void *ctx)
{
	int ret = 0;
	struct deflate_ctx *dctx = ctx;
	struct z_stream_s *stream = &dctx->comp_stream;

	ret = zlib_deflateReset(stream);
	if (ret != Z_OK) {
		ret = -EINVAL;
		goto out;
	}

	stream->next_in = (u8 *)src;
	stream->avail_in = slen;
	stream->next_out = (u8 *)dst;
	stream->avail_out = *dlen;

	ret = zlib_deflate(stream, Z_FINISH);
	if (ret != Z_STREAM_END) {
		ret = -EINVAL;
		goto out;
	}
	ret = 0;
	*dlen = stream->total_out;
out:
	return ret;
}

static int deflate_compress(struct crypto_tfm *tfm, const u8 *src,
			    unsigned int slen, u8 *dst, unsigned int *dlen)
{
	struct deflate_ctx *dctx = crypto_tfm_ctx(tfm);

	return __deflate_compress(src, slen, dst, dlen, dctx);
}

static int deflate_scompress(struct crypto_scomp *tfm, const u8 *src,
			     unsigned int slen, u8 *dst, unsigned int *dlen,
			     void *ctx)
{
	return __deflate_compress(src, slen, dst, dlen, ctx);
}

static int __deflate_decompress(const u8 *src, unsigned int slen,
				u8 *dst, unsigned int *dlen, void *ctx)
{

	int ret = 0;
	struct deflate_ctx *dctx = ctx;
	struct z_stream_s *stream = &dctx->decomp_stream;

	ret = zlib_inflateReset(stream);
	if (ret != Z_OK) {
		ret = -EINVAL;
		goto out;
	}

	stream->next_in = (u8 *)src;
	stream->avail_in = slen;
	stream->next_out = (u8 *)dst;
	stream->avail_out = *dlen;

	ret = zlib_inflate(stream, Z_SYNC_FLUSH);
	/*
	 * Work around a bug in zlib, which sometimes wants to taste an extra
	 * byte when being used in the (undocumented) raw deflate mode.
	 * (From USAGI).
	 */
	if (ret == Z_OK && !stream->avail_in && stream->avail_out) {
		u8 zerostuff = 0;
		stream->next_in = &zerostuff;
		stream->avail_in = 1;
		ret = zlib_inflate(stream, Z_FINISH);
	}
	if (ret != Z_STREAM_END) {
		ret = -EINVAL;
		goto out;
	}
	ret = 0;
	*dlen = stream->total_out;
out:
	return ret;
}

static int deflate_decompress(struct crypto_tfm *tfm, const u8 *src,
			      unsigned int slen, u8 *dst, unsigned int *dlen)
{
	struct deflate_ctx *dctx = crypto_tfm_ctx(tfm);

	return __deflate_decompress(src, slen, dst, dlen, dctx);
}

static int deflate_sdecompress(struct crypto_scomp *tfm, const u8 *src,
			       unsigned int slen, u8 *dst, unsigned int *dlen,
			       void *ctx)
{
	return __deflate_decompress(src, slen, dst, dlen, ctx);
}

static struct crypto_alg alg = {
	.cra_name		= "deflate",
	.cra_driver_name	= "deflate-generic",
	.cra_flags		= CRYPTO_ALG_TYPE_COMPRESS,
	.cra_ctxsize		= sizeof(struct deflate_ctx),
	.cra_module		= THIS_MODULE,
	.cra_init		= deflate_init,
	.cra_exit		= deflate_exit,
	.cra_u			= { .compress = {
	.coa_compress 		= deflate_compress,
	.coa_decompress  	= deflate_decompress } }
};

static struct scomp_alg scomp[] = { {
	.alloc_ctx		= deflate_alloc_ctx,
	.free_ctx		= deflate_free_ctx,
	.compress		= deflate_scompress,
	.decompress		= deflate_sdecompress,
	.base			= {
		.cra_name	= "deflate",
		.cra_driver_name = "deflate-scomp",
		.cra_module	 = THIS_MODULE,
	}
}, {
	.alloc_ctx		= zlib_deflate_alloc_ctx,
	.free_ctx		= deflate_free_ctx,
	.compress		= deflate_scompress,
	.decompress		= deflate_sdecompress,
	.base			= {
		.cra_name	= "zlib-deflate",
		.cra_driver_name = "zlib-deflate-scomp",
		.cra_module	 = THIS_MODULE,
	}
} };

static int __init deflate_mod_init(void)
{
	int ret;

	ret = crypto_register_alg(&alg);
	if (ret)
		return ret;

	ret = crypto_register_scomps(scomp, ARRAY_SIZE(scomp));
	if (ret) {
		crypto_unregister_alg(&alg);
		return ret;
	}

	return ret;
}

static void __exit deflate_mod_fini(void)
{
	crypto_unregister_alg(&alg);
	crypto_unregister_scomps(scomp, ARRAY_SIZE(scomp));
}

subsys_initcall(deflate_mod_init);
module_exit(deflate_mod_fini);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Deflate Compression Algorithm for IPCOMP");
MODULE_AUTHOR("James Morris <jmorris@intercode.com.au>");
MODULE_ALIAS_CRYPTO("deflate");
