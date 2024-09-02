#include <linux/kernel.h>
#include <linux/lz4.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

#include "backend_lz4hc.h"

struct lz4hc_ctx {
	void *mem;
	s32 level;
};

static void lz4hc_destroy(void *ctx)
{
	struct lz4hc_ctx *zctx = ctx;

	vfree(zctx->mem);
	kfree(zctx);
}

static void *lz4hc_create(void)
{
	struct lz4hc_ctx *ctx;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return NULL;

	/* @FIXME: using a hardcoded LZ4HC_DEFAULT_CLEVEL for now */
	ctx->level = LZ4HC_DEFAULT_CLEVEL;
	ctx->mem = vmalloc(LZ4HC_MEM_COMPRESS);
	if (!ctx->mem)
		goto error;

	return ctx;
error:
	lz4hc_destroy(ctx);
	return NULL;
}

static int lz4hc_compress(void *ctx, const unsigned char *src, size_t src_len,
			  unsigned char *dst, size_t *dst_len)
{
	struct lz4hc_ctx *zctx = ctx;
	int ret;

	ret = LZ4_compress_HC(src, dst, src_len, *dst_len,
			      zctx->level, zctx->mem);
	if (!ret)
		return -EINVAL;
	*dst_len = ret;
	return 0;
}

static int lz4hc_decompress(void *ctx, const unsigned char *src,
			    size_t src_len, unsigned char *dst, size_t dst_len)
{
	int ret;

	ret = LZ4_decompress_safe(src, dst, src_len, dst_len);
	if (ret < 0)
		return -EINVAL;
	return 0;
}

const struct zcomp_ops backend_lz4hc = {
	.compress	= lz4hc_compress,
	.decompress	= lz4hc_decompress,
	.create_ctx	= lz4hc_create,
	.destroy_ctx	= lz4hc_destroy,
	.name		= "lz4hc",
};
