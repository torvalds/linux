/*
 * Cryptographic API.
 *
 * Deflate algorithm (RFC 1951), implemented here primarily for use
 * by IPCOMP (RFC 3173 & RFC 2394).
 *
 * Copyright (c) 2003 James Morris <jmorris@intercode.com.au>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
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
#include <linux/slab.h>

#define DEFLATE_DEF_LEVEL		Z_DEFAULT_COMPRESSION
#define DEFLATE_DEF_WINBITS		11
#define DEFLATE_DEF_MEMLEVEL		MAX_MEM_LEVEL

struct deflate_ctx {
	struct z_stream_s comp_stream;
	struct z_stream_s decomp_stream;
};

static int deflate_comp_init(struct deflate_ctx *ctx)
{
	int ret = 0;
	struct z_stream_s *stream = &ctx->comp_stream;

	stream->workspace = vzalloc(zlib_deflate_workspacesize(
				-DEFLATE_DEF_WINBITS, DEFLATE_DEF_MEMLEVEL));
	if (!stream->workspace) {
		ret = -ENOMEM;
		goto out;
	}
	ret = zlib_deflateInit2(stream, DEFLATE_DEF_LEVEL, Z_DEFLATED,
	                        -DEFLATE_DEF_WINBITS, DEFLATE_DEF_MEMLEVEL,
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

static int deflate_decomp_init(struct deflate_ctx *ctx)
{
	int ret = 0;
	struct z_stream_s *stream = &ctx->decomp_stream;

	stream->workspace = kzalloc(zlib_inflate_workspacesize(), GFP_KERNEL);
	if (!stream->workspace) {
		ret = -ENOMEM;
		goto out;
	}
	ret = zlib_inflateInit2(stream, -DEFLATE_DEF_WINBITS);
	if (ret != Z_OK) {
		ret = -EINVAL;
		goto out_free;
	}
out:
	return ret;
out_free:
	kfree(stream->workspace);
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
	kfree(ctx->decomp_stream.workspace);
}

static int deflate_init(struct crypto_tfm *tfm)
{
	struct deflate_ctx *ctx = crypto_tfm_ctx(tfm);
	int ret;

	ret = deflate_comp_init(ctx);
	if (ret)
		goto out;
	ret = deflate_decomp_init(ctx);
	if (ret)
		deflate_comp_exit(ctx);
out:
	return ret;
}

static void deflate_exit(struct crypto_tfm *tfm)
{
	struct deflate_ctx *ctx = crypto_tfm_ctx(tfm);

	deflate_comp_exit(ctx);
	deflate_decomp_exit(ctx);
}

static int deflate_compress(struct crypto_tfm *tfm, const u8 *src,
			    unsigned int slen, u8 *dst, unsigned int *dlen)
{
	int ret = 0;
	struct deflate_ctx *dctx = crypto_tfm_ctx(tfm);
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

static int deflate_decompress(struct crypto_tfm *tfm, const u8 *src,
			      unsigned int slen, u8 *dst, unsigned int *dlen)
{

	int ret = 0;
	struct deflate_ctx *dctx = crypto_tfm_ctx(tfm);
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

static struct crypto_alg alg = {
	.cra_name		= "deflate",
	.cra_flags		= CRYPTO_ALG_TYPE_COMPRESS,
	.cra_ctxsize		= sizeof(struct deflate_ctx),
	.cra_module		= THIS_MODULE,
	.cra_list		= LIST_HEAD_INIT(alg.cra_list),
	.cra_init		= deflate_init,
	.cra_exit		= deflate_exit,
	.cra_u			= { .compress = {
	.coa_compress 		= deflate_compress,
	.coa_decompress  	= deflate_decompress } }
};

static int __init deflate_mod_init(void)
{
	return crypto_register_alg(&alg);
}

static void __exit deflate_mod_fini(void)
{
	crypto_unregister_alg(&alg);
}

module_init(deflate_mod_init);
module_exit(deflate_mod_fini);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Deflate Compression Algorithm for IPCOMP");
MODULE_AUTHOR("James Morris <jmorris@intercode.com.au>");

