/*
 * Glue Code for x86_64/AVX/AES-NI assembler optimized version of Camellia
 *
 * Copyright © 2012-2013 Jussi Kivilinna <jussi.kivilinna@iki.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/crypto.h>
#include <linux/err.h>
#include <crypto/ablk_helper.h>
#include <crypto/algapi.h>
#include <crypto/ctr.h>
#include <crypto/lrw.h>
#include <crypto/xts.h>
#include <asm/fpu/api.h>
#include <asm/crypto/camellia.h>
#include <asm/crypto/glue_helper.h>

#define CAMELLIA_AESNI_PARALLEL_BLOCKS 16

/* 16-way parallel cipher functions (avx/aes-ni) */
asmlinkage void camellia_ecb_enc_16way(struct camellia_ctx *ctx, u8 *dst,
				       const u8 *src);
EXPORT_SYMBOL_GPL(camellia_ecb_enc_16way);

asmlinkage void camellia_ecb_dec_16way(struct camellia_ctx *ctx, u8 *dst,
				       const u8 *src);
EXPORT_SYMBOL_GPL(camellia_ecb_dec_16way);

asmlinkage void camellia_cbc_dec_16way(struct camellia_ctx *ctx, u8 *dst,
				       const u8 *src);
EXPORT_SYMBOL_GPL(camellia_cbc_dec_16way);

asmlinkage void camellia_ctr_16way(struct camellia_ctx *ctx, u8 *dst,
				   const u8 *src, le128 *iv);
EXPORT_SYMBOL_GPL(camellia_ctr_16way);

asmlinkage void camellia_xts_enc_16way(struct camellia_ctx *ctx, u8 *dst,
				       const u8 *src, le128 *iv);
EXPORT_SYMBOL_GPL(camellia_xts_enc_16way);

asmlinkage void camellia_xts_dec_16way(struct camellia_ctx *ctx, u8 *dst,
				       const u8 *src, le128 *iv);
EXPORT_SYMBOL_GPL(camellia_xts_dec_16way);

void camellia_xts_enc(void *ctx, u128 *dst, const u128 *src, le128 *iv)
{
	glue_xts_crypt_128bit_one(ctx, dst, src, iv,
				  GLUE_FUNC_CAST(camellia_enc_blk));
}
EXPORT_SYMBOL_GPL(camellia_xts_enc);

void camellia_xts_dec(void *ctx, u128 *dst, const u128 *src, le128 *iv)
{
	glue_xts_crypt_128bit_one(ctx, dst, src, iv,
				  GLUE_FUNC_CAST(camellia_dec_blk));
}
EXPORT_SYMBOL_GPL(camellia_xts_dec);

static const struct common_glue_ctx camellia_enc = {
	.num_funcs = 3,
	.fpu_blocks_limit = CAMELLIA_AESNI_PARALLEL_BLOCKS,

	.funcs = { {
		.num_blocks = CAMELLIA_AESNI_PARALLEL_BLOCKS,
		.fn_u = { .ecb = GLUE_FUNC_CAST(camellia_ecb_enc_16way) }
	}, {
		.num_blocks = 2,
		.fn_u = { .ecb = GLUE_FUNC_CAST(camellia_enc_blk_2way) }
	}, {
		.num_blocks = 1,
		.fn_u = { .ecb = GLUE_FUNC_CAST(camellia_enc_blk) }
	} }
};

static const struct common_glue_ctx camellia_ctr = {
	.num_funcs = 3,
	.fpu_blocks_limit = CAMELLIA_AESNI_PARALLEL_BLOCKS,

	.funcs = { {
		.num_blocks = CAMELLIA_AESNI_PARALLEL_BLOCKS,
		.fn_u = { .ctr = GLUE_CTR_FUNC_CAST(camellia_ctr_16way) }
	}, {
		.num_blocks = 2,
		.fn_u = { .ctr = GLUE_CTR_FUNC_CAST(camellia_crypt_ctr_2way) }
	}, {
		.num_blocks = 1,
		.fn_u = { .ctr = GLUE_CTR_FUNC_CAST(camellia_crypt_ctr) }
	} }
};

static const struct common_glue_ctx camellia_enc_xts = {
	.num_funcs = 2,
	.fpu_blocks_limit = CAMELLIA_AESNI_PARALLEL_BLOCKS,

	.funcs = { {
		.num_blocks = CAMELLIA_AESNI_PARALLEL_BLOCKS,
		.fn_u = { .xts = GLUE_XTS_FUNC_CAST(camellia_xts_enc_16way) }
	}, {
		.num_blocks = 1,
		.fn_u = { .xts = GLUE_XTS_FUNC_CAST(camellia_xts_enc) }
	} }
};

static const struct common_glue_ctx camellia_dec = {
	.num_funcs = 3,
	.fpu_blocks_limit = CAMELLIA_AESNI_PARALLEL_BLOCKS,

	.funcs = { {
		.num_blocks = CAMELLIA_AESNI_PARALLEL_BLOCKS,
		.fn_u = { .ecb = GLUE_FUNC_CAST(camellia_ecb_dec_16way) }
	}, {
		.num_blocks = 2,
		.fn_u = { .ecb = GLUE_FUNC_CAST(camellia_dec_blk_2way) }
	}, {
		.num_blocks = 1,
		.fn_u = { .ecb = GLUE_FUNC_CAST(camellia_dec_blk) }
	} }
};

static const struct common_glue_ctx camellia_dec_cbc = {
	.num_funcs = 3,
	.fpu_blocks_limit = CAMELLIA_AESNI_PARALLEL_BLOCKS,

	.funcs = { {
		.num_blocks = CAMELLIA_AESNI_PARALLEL_BLOCKS,
		.fn_u = { .cbc = GLUE_CBC_FUNC_CAST(camellia_cbc_dec_16way) }
	}, {
		.num_blocks = 2,
		.fn_u = { .cbc = GLUE_CBC_FUNC_CAST(camellia_decrypt_cbc_2way) }
	}, {
		.num_blocks = 1,
		.fn_u = { .cbc = GLUE_CBC_FUNC_CAST(camellia_dec_blk) }
	} }
};

static const struct common_glue_ctx camellia_dec_xts = {
	.num_funcs = 2,
	.fpu_blocks_limit = CAMELLIA_AESNI_PARALLEL_BLOCKS,

	.funcs = { {
		.num_blocks = CAMELLIA_AESNI_PARALLEL_BLOCKS,
		.fn_u = { .xts = GLUE_XTS_FUNC_CAST(camellia_xts_dec_16way) }
	}, {
		.num_blocks = 1,
		.fn_u = { .xts = GLUE_XTS_FUNC_CAST(camellia_xts_dec) }
	} }
};

static int ecb_encrypt(struct blkcipher_desc *desc, struct scatterlist *dst,
		       struct scatterlist *src, unsigned int nbytes)
{
	return glue_ecb_crypt_128bit(&camellia_enc, desc, dst, src, nbytes);
}

static int ecb_decrypt(struct blkcipher_desc *desc, struct scatterlist *dst,
		       struct scatterlist *src, unsigned int nbytes)
{
	return glue_ecb_crypt_128bit(&camellia_dec, desc, dst, src, nbytes);
}

static int cbc_encrypt(struct blkcipher_desc *desc, struct scatterlist *dst,
		       struct scatterlist *src, unsigned int nbytes)
{
	return glue_cbc_encrypt_128bit(GLUE_FUNC_CAST(camellia_enc_blk), desc,
				       dst, src, nbytes);
}

static int cbc_decrypt(struct blkcipher_desc *desc, struct scatterlist *dst,
		       struct scatterlist *src, unsigned int nbytes)
{
	return glue_cbc_decrypt_128bit(&camellia_dec_cbc, desc, dst, src,
				       nbytes);
}

static int ctr_crypt(struct blkcipher_desc *desc, struct scatterlist *dst,
		     struct scatterlist *src, unsigned int nbytes)
{
	return glue_ctr_crypt_128bit(&camellia_ctr, desc, dst, src, nbytes);
}

static inline bool camellia_fpu_begin(bool fpu_enabled, unsigned int nbytes)
{
	return glue_fpu_begin(CAMELLIA_BLOCK_SIZE,
			      CAMELLIA_AESNI_PARALLEL_BLOCKS, NULL, fpu_enabled,
			      nbytes);
}

static inline void camellia_fpu_end(bool fpu_enabled)
{
	glue_fpu_end(fpu_enabled);
}

static int camellia_setkey(struct crypto_tfm *tfm, const u8 *in_key,
			   unsigned int key_len)
{
	return __camellia_setkey(crypto_tfm_ctx(tfm), in_key, key_len,
				 &tfm->crt_flags);
}

struct crypt_priv {
	struct camellia_ctx *ctx;
	bool fpu_enabled;
};

static void encrypt_callback(void *priv, u8 *srcdst, unsigned int nbytes)
{
	const unsigned int bsize = CAMELLIA_BLOCK_SIZE;
	struct crypt_priv *ctx = priv;
	int i;

	ctx->fpu_enabled = camellia_fpu_begin(ctx->fpu_enabled, nbytes);

	if (nbytes >= CAMELLIA_AESNI_PARALLEL_BLOCKS * bsize) {
		camellia_ecb_enc_16way(ctx->ctx, srcdst, srcdst);
		srcdst += bsize * CAMELLIA_AESNI_PARALLEL_BLOCKS;
		nbytes -= bsize * CAMELLIA_AESNI_PARALLEL_BLOCKS;
	}

	while (nbytes >= CAMELLIA_PARALLEL_BLOCKS * bsize) {
		camellia_enc_blk_2way(ctx->ctx, srcdst, srcdst);
		srcdst += bsize * CAMELLIA_PARALLEL_BLOCKS;
		nbytes -= bsize * CAMELLIA_PARALLEL_BLOCKS;
	}

	for (i = 0; i < nbytes / bsize; i++, srcdst += bsize)
		camellia_enc_blk(ctx->ctx, srcdst, srcdst);
}

static void decrypt_callback(void *priv, u8 *srcdst, unsigned int nbytes)
{
	const unsigned int bsize = CAMELLIA_BLOCK_SIZE;
	struct crypt_priv *ctx = priv;
	int i;

	ctx->fpu_enabled = camellia_fpu_begin(ctx->fpu_enabled, nbytes);

	if (nbytes >= CAMELLIA_AESNI_PARALLEL_BLOCKS * bsize) {
		camellia_ecb_dec_16way(ctx->ctx, srcdst, srcdst);
		srcdst += bsize * CAMELLIA_AESNI_PARALLEL_BLOCKS;
		nbytes -= bsize * CAMELLIA_AESNI_PARALLEL_BLOCKS;
	}

	while (nbytes >= CAMELLIA_PARALLEL_BLOCKS * bsize) {
		camellia_dec_blk_2way(ctx->ctx, srcdst, srcdst);
		srcdst += bsize * CAMELLIA_PARALLEL_BLOCKS;
		nbytes -= bsize * CAMELLIA_PARALLEL_BLOCKS;
	}

	for (i = 0; i < nbytes / bsize; i++, srcdst += bsize)
		camellia_dec_blk(ctx->ctx, srcdst, srcdst);
}

static int lrw_encrypt(struct blkcipher_desc *desc, struct scatterlist *dst,
		       struct scatterlist *src, unsigned int nbytes)
{
	struct camellia_lrw_ctx *ctx = crypto_blkcipher_ctx(desc->tfm);
	be128 buf[CAMELLIA_AESNI_PARALLEL_BLOCKS];
	struct crypt_priv crypt_ctx = {
		.ctx = &ctx->camellia_ctx,
		.fpu_enabled = false,
	};
	struct lrw_crypt_req req = {
		.tbuf = buf,
		.tbuflen = sizeof(buf),

		.table_ctx = &ctx->lrw_table,
		.crypt_ctx = &crypt_ctx,
		.crypt_fn = encrypt_callback,
	};
	int ret;

	desc->flags &= ~CRYPTO_TFM_REQ_MAY_SLEEP;
	ret = lrw_crypt(desc, dst, src, nbytes, &req);
	camellia_fpu_end(crypt_ctx.fpu_enabled);

	return ret;
}

static int lrw_decrypt(struct blkcipher_desc *desc, struct scatterlist *dst,
		       struct scatterlist *src, unsigned int nbytes)
{
	struct camellia_lrw_ctx *ctx = crypto_blkcipher_ctx(desc->tfm);
	be128 buf[CAMELLIA_AESNI_PARALLEL_BLOCKS];
	struct crypt_priv crypt_ctx = {
		.ctx = &ctx->camellia_ctx,
		.fpu_enabled = false,
	};
	struct lrw_crypt_req req = {
		.tbuf = buf,
		.tbuflen = sizeof(buf),

		.table_ctx = &ctx->lrw_table,
		.crypt_ctx = &crypt_ctx,
		.crypt_fn = decrypt_callback,
	};
	int ret;

	desc->flags &= ~CRYPTO_TFM_REQ_MAY_SLEEP;
	ret = lrw_crypt(desc, dst, src, nbytes, &req);
	camellia_fpu_end(crypt_ctx.fpu_enabled);

	return ret;
}

static int xts_encrypt(struct blkcipher_desc *desc, struct scatterlist *dst,
		       struct scatterlist *src, unsigned int nbytes)
{
	struct camellia_xts_ctx *ctx = crypto_blkcipher_ctx(desc->tfm);

	return glue_xts_crypt_128bit(&camellia_enc_xts, desc, dst, src, nbytes,
				     XTS_TWEAK_CAST(camellia_enc_blk),
				     &ctx->tweak_ctx, &ctx->crypt_ctx);
}

static int xts_decrypt(struct blkcipher_desc *desc, struct scatterlist *dst,
		       struct scatterlist *src, unsigned int nbytes)
{
	struct camellia_xts_ctx *ctx = crypto_blkcipher_ctx(desc->tfm);

	return glue_xts_crypt_128bit(&camellia_dec_xts, desc, dst, src, nbytes,
				     XTS_TWEAK_CAST(camellia_enc_blk),
				     &ctx->tweak_ctx, &ctx->crypt_ctx);
}

static struct crypto_alg cmll_algs[10] = { {
	.cra_name		= "__ecb-camellia-aesni",
	.cra_driver_name	= "__driver-ecb-camellia-aesni",
	.cra_priority		= 0,
	.cra_flags		= CRYPTO_ALG_TYPE_BLKCIPHER |
				  CRYPTO_ALG_INTERNAL,
	.cra_blocksize		= CAMELLIA_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct camellia_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_blkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_u = {
		.blkcipher = {
			.min_keysize	= CAMELLIA_MIN_KEY_SIZE,
			.max_keysize	= CAMELLIA_MAX_KEY_SIZE,
			.setkey		= camellia_setkey,
			.encrypt	= ecb_encrypt,
			.decrypt	= ecb_decrypt,
		},
	},
}, {
	.cra_name		= "__cbc-camellia-aesni",
	.cra_driver_name	= "__driver-cbc-camellia-aesni",
	.cra_priority		= 0,
	.cra_flags		= CRYPTO_ALG_TYPE_BLKCIPHER |
				  CRYPTO_ALG_INTERNAL,
	.cra_blocksize		= CAMELLIA_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct camellia_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_blkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_u = {
		.blkcipher = {
			.min_keysize	= CAMELLIA_MIN_KEY_SIZE,
			.max_keysize	= CAMELLIA_MAX_KEY_SIZE,
			.setkey		= camellia_setkey,
			.encrypt	= cbc_encrypt,
			.decrypt	= cbc_decrypt,
		},
	},
}, {
	.cra_name		= "__ctr-camellia-aesni",
	.cra_driver_name	= "__driver-ctr-camellia-aesni",
	.cra_priority		= 0,
	.cra_flags		= CRYPTO_ALG_TYPE_BLKCIPHER |
				  CRYPTO_ALG_INTERNAL,
	.cra_blocksize		= 1,
	.cra_ctxsize		= sizeof(struct camellia_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_blkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_u = {
		.blkcipher = {
			.min_keysize	= CAMELLIA_MIN_KEY_SIZE,
			.max_keysize	= CAMELLIA_MAX_KEY_SIZE,
			.ivsize		= CAMELLIA_BLOCK_SIZE,
			.setkey		= camellia_setkey,
			.encrypt	= ctr_crypt,
			.decrypt	= ctr_crypt,
		},
	},
}, {
	.cra_name		= "__lrw-camellia-aesni",
	.cra_driver_name	= "__driver-lrw-camellia-aesni",
	.cra_priority		= 0,
	.cra_flags		= CRYPTO_ALG_TYPE_BLKCIPHER |
				  CRYPTO_ALG_INTERNAL,
	.cra_blocksize		= CAMELLIA_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct camellia_lrw_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_blkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_exit		= lrw_camellia_exit_tfm,
	.cra_u = {
		.blkcipher = {
			.min_keysize	= CAMELLIA_MIN_KEY_SIZE +
					  CAMELLIA_BLOCK_SIZE,
			.max_keysize	= CAMELLIA_MAX_KEY_SIZE +
					  CAMELLIA_BLOCK_SIZE,
			.ivsize		= CAMELLIA_BLOCK_SIZE,
			.setkey		= lrw_camellia_setkey,
			.encrypt	= lrw_encrypt,
			.decrypt	= lrw_decrypt,
		},
	},
}, {
	.cra_name		= "__xts-camellia-aesni",
	.cra_driver_name	= "__driver-xts-camellia-aesni",
	.cra_priority		= 0,
	.cra_flags		= CRYPTO_ALG_TYPE_BLKCIPHER |
				  CRYPTO_ALG_INTERNAL,
	.cra_blocksize		= CAMELLIA_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct camellia_xts_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_blkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_u = {
		.blkcipher = {
			.min_keysize	= CAMELLIA_MIN_KEY_SIZE * 2,
			.max_keysize	= CAMELLIA_MAX_KEY_SIZE * 2,
			.ivsize		= CAMELLIA_BLOCK_SIZE,
			.setkey		= xts_camellia_setkey,
			.encrypt	= xts_encrypt,
			.decrypt	= xts_decrypt,
		},
	},
}, {
	.cra_name		= "ecb(camellia)",
	.cra_driver_name	= "ecb-camellia-aesni",
	.cra_priority		= 400,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,
	.cra_blocksize		= CAMELLIA_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct async_helper_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_init		= ablk_init,
	.cra_exit		= ablk_exit,
	.cra_u = {
		.ablkcipher = {
			.min_keysize	= CAMELLIA_MIN_KEY_SIZE,
			.max_keysize	= CAMELLIA_MAX_KEY_SIZE,
			.setkey		= ablk_set_key,
			.encrypt	= ablk_encrypt,
			.decrypt	= ablk_decrypt,
		},
	},
}, {
	.cra_name		= "cbc(camellia)",
	.cra_driver_name	= "cbc-camellia-aesni",
	.cra_priority		= 400,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,
	.cra_blocksize		= CAMELLIA_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct async_helper_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_init		= ablk_init,
	.cra_exit		= ablk_exit,
	.cra_u = {
		.ablkcipher = {
			.min_keysize	= CAMELLIA_MIN_KEY_SIZE,
			.max_keysize	= CAMELLIA_MAX_KEY_SIZE,
			.ivsize		= CAMELLIA_BLOCK_SIZE,
			.setkey		= ablk_set_key,
			.encrypt	= __ablk_encrypt,
			.decrypt	= ablk_decrypt,
		},
	},
}, {
	.cra_name		= "ctr(camellia)",
	.cra_driver_name	= "ctr-camellia-aesni",
	.cra_priority		= 400,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,
	.cra_blocksize		= 1,
	.cra_ctxsize		= sizeof(struct async_helper_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_init		= ablk_init,
	.cra_exit		= ablk_exit,
	.cra_u = {
		.ablkcipher = {
			.min_keysize	= CAMELLIA_MIN_KEY_SIZE,
			.max_keysize	= CAMELLIA_MAX_KEY_SIZE,
			.ivsize		= CAMELLIA_BLOCK_SIZE,
			.setkey		= ablk_set_key,
			.encrypt	= ablk_encrypt,
			.decrypt	= ablk_encrypt,
			.geniv		= "chainiv",
		},
	},
}, {
	.cra_name		= "lrw(camellia)",
	.cra_driver_name	= "lrw-camellia-aesni",
	.cra_priority		= 400,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,
	.cra_blocksize		= CAMELLIA_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct async_helper_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_init		= ablk_init,
	.cra_exit		= ablk_exit,
	.cra_u = {
		.ablkcipher = {
			.min_keysize	= CAMELLIA_MIN_KEY_SIZE +
					  CAMELLIA_BLOCK_SIZE,
			.max_keysize	= CAMELLIA_MAX_KEY_SIZE +
					  CAMELLIA_BLOCK_SIZE,
			.ivsize		= CAMELLIA_BLOCK_SIZE,
			.setkey		= ablk_set_key,
			.encrypt	= ablk_encrypt,
			.decrypt	= ablk_decrypt,
		},
	},
}, {
	.cra_name		= "xts(camellia)",
	.cra_driver_name	= "xts-camellia-aesni",
	.cra_priority		= 400,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,
	.cra_blocksize		= CAMELLIA_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct async_helper_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_init		= ablk_init,
	.cra_exit		= ablk_exit,
	.cra_u = {
		.ablkcipher = {
			.min_keysize	= CAMELLIA_MIN_KEY_SIZE * 2,
			.max_keysize	= CAMELLIA_MAX_KEY_SIZE * 2,
			.ivsize		= CAMELLIA_BLOCK_SIZE,
			.setkey		= ablk_set_key,
			.encrypt	= ablk_encrypt,
			.decrypt	= ablk_decrypt,
		},
	},
} };

static int __init camellia_aesni_init(void)
{
	const char *feature_name;

	if (!cpu_has_xfeatures(XSTATE_SSE | XSTATE_YMM, &feature_name)) {
		pr_info("CPU feature '%s' is not supported.\n", feature_name);
		return -ENODEV;
	}

	return crypto_register_algs(cmll_algs, ARRAY_SIZE(cmll_algs));
}

static void __exit camellia_aesni_fini(void)
{
	crypto_unregister_algs(cmll_algs, ARRAY_SIZE(cmll_algs));
}

module_init(camellia_aesni_init);
module_exit(camellia_aesni_fini);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Camellia Cipher Algorithm, AES-NI/AVX optimized");
MODULE_ALIAS_CRYPTO("camellia");
MODULE_ALIAS_CRYPTO("camellia-asm");
