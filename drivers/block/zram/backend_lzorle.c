// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/lzo.h>

#include "backend_lzorle.h"

static void *lzorle_create(struct zcomp_params *params)
{
	return kzalloc(LZO1X_MEM_COMPRESS, GFP_KERNEL);
}

static void lzorle_destroy(void *ctx)
{
	kfree(ctx);
}

static int lzorle_compress(void *ctx, const unsigned char *src, size_t src_len,
			   unsigned char *dst, size_t *dst_len)
{
	int ret;

	ret = lzorle1x_1_compress(src, src_len, dst, dst_len, ctx);
	return ret == LZO_E_OK ? 0 : ret;
}

static int lzorle_decompress(void *ctx, const unsigned char *src,
			     size_t src_len, unsigned char *dst,
			     size_t dst_len)
{
	int ret;

	ret = lzo1x_decompress_safe(src, src_len, dst, &dst_len);
	return ret == LZO_E_OK ? 0 : ret;
}

const struct zcomp_ops backend_lzorle = {
	.compress	= lzorle_compress,
	.decompress	= lzorle_decompress,
	.create_ctx	= lzorle_create,
	.destroy_ctx	= lzorle_destroy,
	.name		= "lzo-rle",
};
