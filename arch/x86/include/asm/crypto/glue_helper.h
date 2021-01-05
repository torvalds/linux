/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Shared glue code for 128bit block ciphers
 */

#ifndef _CRYPTO_GLUE_HELPER_H
#define _CRYPTO_GLUE_HELPER_H

#include <crypto/internal/skcipher.h>
#include <linux/kernel.h>
#include <asm/fpu/api.h>

typedef void (*common_glue_func_t)(const void *ctx, u8 *dst, const u8 *src);
typedef void (*common_glue_cbc_func_t)(const void *ctx, u8 *dst, const u8 *src);

struct common_glue_func_entry {
	unsigned int num_blocks; /* number of blocks that @fn will process */
	union {
		common_glue_func_t ecb;
		common_glue_cbc_func_t cbc;
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

extern int glue_ecb_req_128bit(const struct common_glue_ctx *gctx,
			       struct skcipher_request *req);

extern int glue_cbc_encrypt_req_128bit(const common_glue_func_t fn,
				       struct skcipher_request *req);

extern int glue_cbc_decrypt_req_128bit(const struct common_glue_ctx *gctx,
				       struct skcipher_request *req);

#endif /* _CRYPTO_GLUE_HELPER_H */
