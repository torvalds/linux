/*
 * Shared glue code for 128bit block ciphers
 */

#ifndef _CRYPTO_GLUE_HELPER_H
#define _CRYPTO_GLUE_HELPER_H

#include <crypto/internal/skcipher.h>
#include <linux/kernel.h>
#include <asm/fpu/api.h>
#include <crypto/b128ops.h>

typedef void (*common_glue_func_t)(void *ctx, u8 *dst, const u8 *src);
typedef void (*common_glue_cbc_func_t)(void *ctx, u128 *dst, const u128 *src);
typedef void (*common_glue_ctr_func_t)(void *ctx, u128 *dst, const u128 *src,
				       le128 *iv);
typedef void (*common_glue_xts_func_t)(void *ctx, u128 *dst, const u128 *src,
				       le128 *iv);

#define GLUE_FUNC_CAST(fn) ((common_glue_func_t)(fn))
#define GLUE_CBC_FUNC_CAST(fn) ((common_glue_cbc_func_t)(fn))
#define GLUE_CTR_FUNC_CAST(fn) ((common_glue_ctr_func_t)(fn))
#define GLUE_XTS_FUNC_CAST(fn) ((common_glue_xts_func_t)(fn))

struct common_glue_func_entry {
	unsigned int num_blocks; /* number of blocks that @fn will process */
	union {
		common_glue_func_t ecb;
		common_glue_cbc_func_t cbc;
		common_glue_ctr_func_t ctr;
		common_glue_xts_func_t xts;
	} fn_u;
};

struct common_glue_ctx {
	unsigned int num_funcs;
	int fpu_blocks_limit; /* -1 means fpu not needed at all */

	/*
	 * First funcs entry must have largest num_blocks and last funcs entry
	 * must have num_blocks == 1!
	 */
	struct common_glue_func_entry funcs[];
};

static inline bool glue_fpu_begin(unsigned int bsize, int fpu_blocks_limit,
				  struct blkcipher_desc *desc,
				  bool fpu_enabled, unsigned int nbytes)
{
	if (likely(fpu_blocks_limit < 0))
		return false;

	if (fpu_enabled)
		return true;

	/*
	 * Vector-registers are only used when chunk to be processed is large
	 * enough, so do not enable FPU until it is necessary.
	 */
	if (nbytes < bsize * (unsigned int)fpu_blocks_limit)
		return false;

	if (desc) {
		/* prevent sleeping if FPU is in use */
		desc->flags &= ~CRYPTO_TFM_REQ_MAY_SLEEP;
	}

	kernel_fpu_begin();
	return true;
}

static inline bool glue_skwalk_fpu_begin(unsigned int bsize,
					 int fpu_blocks_limit,
					 struct skcipher_walk *walk,
					 bool fpu_enabled, unsigned int nbytes)
{
	if (likely(fpu_blocks_limit < 0))
		return false;

	if (fpu_enabled)
		return true;

	/*
	 * Vector-registers are only used when chunk to be processed is large
	 * enough, so do not enable FPU until it is necessary.
	 */
	if (nbytes < bsize * (unsigned int)fpu_blocks_limit)
		return false;

	/* prevent sleeping if FPU is in use */
	skcipher_walk_atomise(walk);

	kernel_fpu_begin();
	return true;
}

static inline void glue_fpu_end(bool fpu_enabled)
{
	if (fpu_enabled)
		kernel_fpu_end();
}

static inline void le128_to_be128(be128 *dst, const le128 *src)
{
	dst->a = cpu_to_be64(le64_to_cpu(src->a));
	dst->b = cpu_to_be64(le64_to_cpu(src->b));
}

static inline void be128_to_le128(le128 *dst, const be128 *src)
{
	dst->a = cpu_to_le64(be64_to_cpu(src->a));
	dst->b = cpu_to_le64(be64_to_cpu(src->b));
}

static inline void le128_inc(le128 *i)
{
	u64 a = le64_to_cpu(i->a);
	u64 b = le64_to_cpu(i->b);

	b++;
	if (!b)
		a++;

	i->a = cpu_to_le64(a);
	i->b = cpu_to_le64(b);
}

extern int glue_ecb_crypt_128bit(const struct common_glue_ctx *gctx,
				 struct blkcipher_desc *desc,
				 struct scatterlist *dst,
				 struct scatterlist *src, unsigned int nbytes);

extern int glue_cbc_encrypt_128bit(const common_glue_func_t fn,
				   struct blkcipher_desc *desc,
				   struct scatterlist *dst,
				   struct scatterlist *src,
				   unsigned int nbytes);

extern int glue_cbc_decrypt_128bit(const struct common_glue_ctx *gctx,
				   struct blkcipher_desc *desc,
				   struct scatterlist *dst,
				   struct scatterlist *src,
				   unsigned int nbytes);

extern int glue_ctr_crypt_128bit(const struct common_glue_ctx *gctx,
				 struct blkcipher_desc *desc,
				 struct scatterlist *dst,
				 struct scatterlist *src, unsigned int nbytes);

extern int glue_xts_crypt_128bit(const struct common_glue_ctx *gctx,
				 struct blkcipher_desc *desc,
				 struct scatterlist *dst,
				 struct scatterlist *src, unsigned int nbytes,
				 common_glue_func_t tweak_fn, void *tweak_ctx,
				 void *crypt_ctx);

extern int glue_xts_crypt_128bit(const struct common_glue_ctx *gctx,
				 struct blkcipher_desc *desc,
				 struct scatterlist *dst,
				 struct scatterlist *src, unsigned int nbytes,
				 common_glue_func_t tweak_fn, void *tweak_ctx,
				 void *crypt_ctx);

extern int glue_xts_req_128bit(const struct common_glue_ctx *gctx,
			       struct skcipher_request *req,
			       common_glue_func_t tweak_fn, void *tweak_ctx,
			       void *crypt_ctx);

extern void glue_xts_crypt_128bit_one(void *ctx, u128 *dst, const u128 *src,
				      le128 *iv, common_glue_func_t fn);

#endif /* _CRYPTO_GLUE_HELPER_H */
