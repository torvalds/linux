// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/sw842.h>
#include <linux/vmalloc.h>

#include "backend_842.h"

struct sw842_ctx {
	void *mem;
};

static void destroy_842(void *ctx)
{
	struct sw842_ctx *zctx = ctx;

	kfree(zctx->mem);
	kfree(zctx);
}

static void *create_842(void)
{
	struct sw842_ctx *ctx;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return NULL;

	ctx->mem = kmalloc(SW842_MEM_COMPRESS, GFP_KERNEL);
	if (!ctx->mem)
		goto error;

	return ctx;

error:
	destroy_842(ctx);
	return NULL;
}

static int compress_842(void *ctx, const unsigned char *src, size_t src_len,
			unsigned char *dst, size_t *dst_len)
{
	struct sw842_ctx *zctx = ctx;
	unsigned int dlen = *dst_len;
	int ret;

	ret = sw842_compress(src, src_len, dst, &dlen, zctx->mem);
	if (ret == 0)
		*dst_len = dlen;
	return ret;
}

static int decompress_842(void *ctx, const unsigned char *src, size_t src_len,
			  unsigned char *dst, size_t dst_len)
{
	unsigned int dlen = dst_len;

	return sw842_decompress(src, src_len, dst, &dlen);
}

const struct zcomp_ops backend_842 = {
	.compress	= compress_842,
	.decompress	= decompress_842,
	.create_ctx	= create_842,
	.destroy_ctx	= destroy_842,
	.name		= "842",
};
