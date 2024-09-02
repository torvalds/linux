#include <linux/kernel.h>
#include <linux/lz4.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

#include "backend_lz4.h"

struct lz4_ctx {
	void *mem;
	s32 level;
};

static void lz4_destroy(void *ctx)
{
	struct lz4_ctx *zctx = ctx;

	vfree(zctx->mem);
	kfree(zctx);
}

static void *lz4_create(void)
{
	struct lz4_ctx *ctx;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return NULL;

	/* @FIXME: using a hardcoded LZ4_ACCELERATION_DEFAULT for now */
	ctx->level = LZ4_ACCELERATION_DEFAULT;
	ctx->mem = vmalloc(LZ4_MEM_COMPRESS);
	if (!ctx->mem)
		goto error;

	return ctx;
error:
	lz4_destroy(ctx);
	return NULL;
}

static int lz4_compress(void *ctx, const unsigned char *src, size_t src_len,
			unsigned char *dst, size_t *dst_len)
{
	struct lz4_ctx *zctx = ctx;
	int ret;

	ret = LZ4_compress_fast(src, dst, src_len, *dst_len,
				zctx->level, zctx->mem);
	if (!ret)
		return -EINVAL;
	*dst_len = ret;
	return 0;
}

static int lz4_decompress(void *ctx, const unsigned char *src,
			  size_t src_len, unsigned char *dst, size_t dst_len)
{
	int ret;

	ret = LZ4_decompress_safe(src, dst, src_len, dst_len);
	if (ret < 0)
		return -EINVAL;
	return 0;
}

const struct zcomp_ops backend_lz4 = {
	.compress	= lz4_compress,
	.decompress	= lz4_decompress,
	.create_ctx	= lz4_create,
	.destroy_ctx	= lz4_destroy,
	.name		= "lz4",
};
