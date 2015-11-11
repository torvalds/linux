/*
 * Glue Code for x86_64/AVX2 assembler optimized version of Twofish
 *
 * Copyright © 2012-2013 Jussi Kivilinna <jussi.kivilinna@mbnet.fi>
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
#include <crypto/algapi.h>
#include <crypto/ctr.h>
#include <crypto/twofish.h>
#include <crypto/lrw.h>
#include <crypto/xts.h>
#include <asm/xcr.h>
#include <asm/xsave.h>
#include <asm/crypto/twofish.h>
#include <asm/crypto/ablk_helper.h>
#include <asm/crypto/glue_helper.h>
#include <crypto/scatterwalk.h>

#define TF_AVX2_PARALLEL_BLOCKS 16

/* 16-way AVX2 parallel cipher functions */
asmlinkage void twofish_ecb_enc_16way(struct twofish_ctx *ctx, u8 *dst,
				      const u8 *src);
asmlinkage void twofish_ecb_dec_16way(struct twofish_ctx *ctx, u8 *dst,
				      const u8 *src);
asmlinkage void twofish_cbc_dec_16way(void *ctx, u128 *dst, const u128 *src);

asmlinkage void twofish_ctr_16way(void *ctx, u128 *dst, const u128 *src,
				  le128 *iv);

asmlinkage void twofish_xts_enc_16way(struct twofish_ctx *ctx, u8 *dst,
				      const u8 *src, le128 *iv);
asmlinkage void twofish_xts_dec_16way(struct twofish_ctx *ctx, u8 *dst,
				      const u8 *src, le128 *iv);

static inline void twofish_enc_blk_3way(struct twofish_ctx *ctx, u8 *dst,
					const u8 *src)
{
	__twofish_enc_blk_3way(ctx, dst, src, false);
}

static const struct common_glue_ctx twofish_enc = {
	.num_funcs = 4,
	.fpu_blocks_limit = 8,

	.funcs = { {
		.num_blocks = 16,
		.fn_u = { .ecb = GLUE_FUNC_CAST(twofish_ecb_enc_16way) }
	}, {
		.num_blocks = 8,
		.fn_u = { .ecb = GLUE_FUNC_CAST(twofish_ecb_enc_8way) }
	}, {
		.num_blocks = 3,
		.fn_u = { .ecb = GLUE_FUNC_CAST(twofish_enc_blk_3way) }
	}, {
		.num_blocks = 1,
		.fn_u = { .ecb = GLUE_FUNC_CAST(twofish_enc_blk) }
	} }
};

static const struct common_glue_ctx twofish_ctr = {
	.num_funcs = 4,
	.fpu_blocks_limit = 8,

	.funcs = { {
		.num_blocks = 16,
		.fn_u = { .ctr = GLUE_CTR_FUNC_CAST(twofish_ctr_16way) }
	},  {
		.num_blocks = 8,
		.fn_u = { .ctr = GLUE_CTR_FUNC_CAST(twofish_ctr_8way) }
	}, {
		.num_blocks = 3,
		.fn_u = { .ctr = GLUE_CTR_FUNC_CAST(twofish_enc_blk_ctr_3way) }
	}, {
		.num_blocks = 1,
		.fn_u = { .ctr = GLUE_CTR_FUNC_CAST(twofish_enc_blk_ctr) }
	} }
};

static const struct common_glue_ctx twofish_enc_xts = {
	.num_funcs = 3,
	.fpu_blocks_limit = 8,

	.funcs = { {
		.num_blocks = 16,
		.fn_u = { .xts = GLUE_XTS_FUNC_CAST(twofish_xts_enc_16way) }
	}, {
		.num_blocks = 8,
		.fn_u = { .xts = GLUE_XTS_FUNC_CAST(twofish_xts_enc_8way) }
	}, {
		.num_blocks = 1,
		.fn_u = { .xts = GLUE_XTS_FUNC_CAST(twofish_xts_enc) }
	} }
};

static const struct common_glue_ctx twofish_dec = {
	.num_funcs = 4,
	.fpu_blocks_limit = 8,

	.funcs = { {
		.num_blocks = 16,
		.fn_u = { .ecb = GLUE_FUNC_CAST(twofish_ecb_dec_16way) }
	}, {
		.num_blocks = 8,
		.fn_u = { .ecb = GLUE_FUNC_CAST(twofish_ecb_dec_8way) }
	}, {
		.num_blocks = 3,
		.fn_u = { .ecb = GLUE_FUNC_CAST(twofish_dec_blk_3way) }
	}, {
		.num_blocks = 1,
		.fn_u = { .ecb = GLUE_FUNC_CAST(twofish_dec_blk) }
	} }
};

static const struct common_glue_ctx twofish_dec_cbc = {
	.num_funcs = 4,
	.fpu_blocks_limit = 8,

	.funcs = { {
		.num_blocks = 16,
		.fn_u = { .cbc = GLUE_CBC_FUNC_CAST(twofish_cbc_dec_16way) }
	}, {
		.num_blocks = 8,
		.fn_u = { .cbc = GLUE_CBC_FUNC_CAST(twofish_cbc_dec_8way) }
	}, {
		.num_blocks = 3,
		.fn_u = { .cbc = GLUE_CBC_FUNC_CAST(twofish_dec_blk_cbc_3way) }
	}, {
		.num_blocks = 1,
		.fn_u = { .cbc = GLUE_CBC_FUNC_CAST(twofish_dec_blk) }
	} }
};

static const struct common_glue_ctx twofish_dec_xts = {
	.num_funcs = 3,
	.fpu_blocks_limit = 8,

	.funcs = { {
		.num_blocks = 16,
		.fn_u = { .xts = GLUE_XTS_FUNC_CAST(twofish_xts_dec_16way) }
	}, {
		.num_blocks = 8,
		.fn_u = { .xts = GLUE_XTS_FUNC_CAST(twofish_xts_dec_8way) }
	}, {
		.num_blocks = 1,
		.fn_u = { .xts = GLUE_XTS_FUNC_CAST(twofish_xts_dec) }
	} }
};

static int ecb_encrypt(struct blkcipher_desc *desc, struct scatterlist *dst,
		       struct scatterlist *src, unsigned int nbytes)
{
	return glue_ecb_crypt_128bit(&twofish_enc, desc, dst, src, nbytes);
}

static int ecb_decrypt(struct blkcipher_desc *desc, struct scatterlist *dst,
		       struct scatterlist *src, unsigned int nbytes)
{
	return glue_ecb_crypt_128bit(&twofish_dec, desc, dst, src, nbytes);
}

static int cbc_encrypt(struct blkcipher_desc *desc, struct scatterlist *dst,
		       struct scatterlist *src, unsigned int nbytes)
{
	return glue_cbc_encrypt_128bit(GLUE_FUNC_CAST(twofish_enc_blk), desc,
				       dst, src, nbytes);
}

static int cbc_decrypt(struct blkcipher_desc *desc, struct scatterlist *dst,
		       struct scatterlist *src, unsigned int nbytes)
{
	return glue_cbc_decrypt_128bit(&twofish_dec_cbc, desc, dst, src,
				       nbytes);
}

static int ctr_crypt(struct blkcipher_desc *desc, struct scatterlist *dst,
		     struct scatterlist *src, unsigned int nbytes)
{
	return glue_ctr_crypt_128bit(&twofish_ctr, desc, dst, src, nbytes);
}

static inline bool twofish_fpu_begin(bool fpu_enabled, unsigned int nbytes)
{
	/* since reusing AVX functions, starts using FPU at 8 parallel blocks */
	return glue_fpu_begin(TF_BLOCK_SIZE, 8, NULL, fpu_enabled, nbytes);
}

static inline void twofish_fpu_end(bool fpu_enabled)
{
	glue_fpu_end(fpu_enabled);
}

struct crypt_priv {
	struct twofish_ctx *ctx;
	bool fpu_enabled;
};

static void encrypt_callback(void *priv, u8 *srcdst, unsigned int nbytes)
{
	const unsigned int bsize = TF_BLOCK_SIZE;
	struct crypt_priv *ctx = priv;
	int i;

	ctx->fpu_enabled = twofish_fpu_begin(ctx->fpu_enabled, nbytes);

	while (nbytes >= TF_AVX2_PARALLEL_BLOCKS * bsize) {
		twofish_ecb_enc_16way(ctx->ctx, srcdst, srcdst);
		srcdst += bsize * TF_AVX2_PARALLEL_BLOCKS;
		nbytes -= bsize * TF_AVX2_PARALLEL_BLOCKS;
	}

	while (nbytes >= 8 * bsize) {
		twofish_ecb_enc_8way(ctx->ctx, srcdst, srcdst);
		srcdst += bsize * 8;
		nbytes -= bsize * 8;
	}

	while (nbytes >= 3 * bsize) {
		twofish_enc_blk_3way(ctx->ctx, srcdst, srcdst);
		srcdst += bsize * 3;
		nbytes -= bsize * 3;
	}

	for (i = 0; i < nbytes / bsize; i++, srcdst += bsize)
		twofish_enc_blk(ctx->ctx, srcdst, srcdst);
}

static void decrypt_callback(void *priv, u8 *srcdst, unsigned int nbytes)
{
	const unsigned int bsize = TF_BLOCK_SIZE;
	struct crypt_priv *ctx = priv;
	int i;

	ctx->fpu_enabled = twofish_fpu_begin(ctx->fpu_enabled, nbytes);

	while (nbytes >= TF_AVX2_PARALLEL_BLOCKS * bsize) {
		twofish_ecb_dec_16way(ctx->ctx, srcdst, srcdst);
		srcdst += bsize * TF_AVX2_PARALLEL_BLOCKS;
		nbytes -= bsize * TF_AVX2_PARALLEL_BLOCKS;
	}

	while (nbytes >= 8 * bsize) {
		twofish_ecb_dec_8way(ctx->ctx, srcdst, srcdst);
		srcdst += bsize * 8;
		nbytes -= bsize * 8;
	}

	while (nbytes >= 3 * bsize) {
		twofish_dec_blk_3way(ctx->ctx, srcdst, srcdst);
		srcdst += bsize * 3;
		nbytes -= bsize * 3;
	}

	for (i = 0; i < nbytes / bsize; i++, srcdst += bsize)
		twofish_dec_blk(ctx->ctx, srcdst, srcdst);
}

static int lrw_encrypt(struct blkcipher_desc *desc, struct scatterlist *dst,
		       struct scatterlist *src, unsigned int nbytes)
{
	struct twofish_lrw_ctx *ctx = crypto_blkcipher_ctx(desc->tfm);
	be128 buf[TF_AVX2_PARALLEL_BLOCKS];
	struct crypt_priv crypt_ctx = {
		.ctx = &ctx->twofish_ctx,
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
	twofish_fpu_end(crypt_ctx.fpu_enabled);

	return ret;
}

static int lrw_decrypt(struct blkcipher_desc *desc, struct scatterlist *dst,
		       struct scatterlist *src, unsigned int nbytes)
{
	struct twofish_lrw_ctx *ctx = crypto_blkcipher_ctx(desc->tfm);
	be128 buf[TF_AVX2_PARALLEL_BLOCKS];
	struct crypt_priv crypt_ctx = {
		.ctx = &ctx->twofish_ctx,
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
	twofish_fpu_end(crypt_ctx.fpu_enabled);

	return ret;
}

static int xts_encrypt(struct blkcipher_desc *desc, struct scatterlist *dst,
		       struct scatterlist *src, unsigned int nbytes)
{
	struct twofish_xts_ctx *ctx = crypto_blkcipher_ctx(desc->tfm);

	return glue_xts_crypt_128bit(&twofish_enc_xts, desc, dst, src, nbytes,
				     XTS_TWEAK_CAST(twofish_enc_blk),
				     &ctx->tweak_ctx, &ctx->crypt_ctx);
}

static int xts_decrypt(struct blkcipher_desc *desc, struct scatterlist *dst,
		       struct scatterlist *src, unsigned int nbytes)
{
	struct twofish_xts_ctx *ctx = crypto_blkcipher_ctx(desc->tfm);

	return glue_xts_crypt_128bit(&twofish_dec_xts, desc, dst, src, nbytes,
				     XTS_TWEAK_CAST(twofish_enc_blk),
				     &ctx->tweak_ctx, &ctx->crypt_ctx);
}

static struct crypto_alg tf_algs[10] = { {
	.cra_name		= "__ecb-twofish-avx2",
	.cra_driver_name	= "__driver-ecb-twofish-avx2",
	.cra_priority		= 0,
	.cra_flags		= CRYPTO_ALG_TYPE_BLKCIPHER,
	.cra_blocksize		= TF_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct twofish_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_blkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_u = {
		.blkcipher = {
			.min_keysize	= TF_MIN_KEY_SIZE,
			.max_keysize	= TF_MAX_KEY_SIZE,
			.setkey		= twofish_setkey,
			.encrypt	= ecb_encrypt,
			.decrypt	= ecb_decrypt,
		},
	},
}, {
	.cra_name		= "__cbc-twofish-avx2",
	.cra_driver_name	= "__driver-cbc-twofish-avx2",
	.cra_priority		= 0,
	.cra_flags		= CRYPTO_ALG_TYPE_BLKCIPHER,
	.cra_blocksize		= TF_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct twofish_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_blkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_u = {
		.blkcipher = {
			.min_keysize	= TF_MIN_KEY_SIZE,
			.max_keysize	= TF_MAX_KEY_SIZE,
			.setkey		= twofish_setkey,
			.encrypt	= cbc_encrypt,
			.decrypt	= cbc_decrypt,
		},
	},
}, {
	.cra_name		= "__ctr-twofish-avx2",
	.cra_driver_name	= "__driver-ctr-twofish-avx2",
	.cra_priority		= 0,
	.cra_flags		= CRYPTO_ALG_TYPE_BLKCIPHER,
	.cra_blocksize		= 1,
	.cra_ctxsize		= sizeof(struct twofish_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_blkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_u = {
		.blkcipher = {
			.min_keysize	= TF_MIN_KEY_SIZE,
			.max_keysize	= TF_MAX_KEY_SIZE,
			.ivsize		= TF_BLOCK_SIZE,
			.setkey		= twofish_setkey,
			.encrypt	= ctr_crypt,
			.decrypt	= ctr_crypt,
		},
	},
}, {
	.cra_name		= "__lrw-twofish-avx2",
	.cra_driver_name	= "__driver-lrw-twofish-avx2",
	.cra_priority		= 0,
	.cra_flags		= CRYPTO_ALG_TYPE_BLKCIPHER,
	.cra_blocksize		= TF_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct twofish_lrw_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_blkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_exit		= lrw_twofish_exit_tfm,
	.cra_u = {
		.blkcipher = {
			.min_keysize	= TF_MIN_KEY_SIZE +
					  TF_BLOCK_SIZE,
			.max_keysize	= TF_MAX_KEY_SIZE +
					  TF_BLOCK_SIZE,
			.ivsize		= TF_BLOCK_SIZE,
			.setkey		= lrw_twofish_setkey,
			.encrypt	= lrw_encrypt,
			.decrypt	= lrw_decrypt,
		},
	},
}, {
	.cra_name		= "__xts-twofish-avx2",
	.cra_driver_name	= "__driver-xts-twofish-avx2",
	.cra_priority		= 0,
	.cra_flags		= CRYPTO_ALG_TYPE_BLKCIPHER,
	.cra_blocksize		= TF_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct twofish_xts_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_blkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_u = {
		.blkcipher = {
			.min_keysize	= TF_MIN_KEY_SIZE * 2,
			.max_keysize	= TF_MAX_KEY_SIZE * 2,
			.ivsize		= TF_BLOCK_SIZE,
			.setkey		= xts_twofish_setkey,
			.encrypt	= xts_encrypt,
			.decrypt	= xts_decrypt,
		},
	},
}, {
	.cra_name		= "ecb(twofish)",
	.cra_driver_name	= "ecb-twofish-avx2",
	.cra_priority		= 500,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,
	.cra_blocksize		= TF_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct async_helper_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_init		= ablk_init,
	.cra_exit		= ablk_exit,
	.cra_u = {
		.ablkcipher = {
			.min_keysize	= TF_MIN_KEY_SIZE,
			.max_keysize	= TF_MAX_KEY_SIZE,
			.setkey		= ablk_set_key,
			.encrypt	= ablk_encrypt,
			.decrypt	= ablk_decrypt,
		},
	},
}, {
	.cra_name		= "cbc(twofish)",
	.cra_driver_name	= "cbc-twofish-avx2",
	.cra_priority		= 500,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,
	.cra_blocksize		= TF_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct async_helper_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_init		= ablk_init,
	.cra_exit		= ablk_exit,
	.cra_u = {
		.ablkcipher = {
			.min_keysize	= TF_MIN_KEY_SIZE,
			.max_keysize	= TF_MAX_KEY_SIZE,
			.ivsize		= TF_BLOCK_SIZE,
			.setkey		= ablk_set_key,
			.encrypt	= __ablk_encrypt,
			.decrypt	= ablk_decrypt,
		},
	},
}, {
	.cra_name		= "ctr(twofish)",
	.cra_driver_name	= "ctr-twofish-avx2",
	.cra_priority		= 500,
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
			.min_keysize	= TF_MIN_KEY_SIZE,
			.max_keysize	= TF_MAX_KEY_SIZE,
			.ivsize		= TF_BLOCK_SIZE,
			.setkey		= ablk_set_key,
			.encrypt	= ablk_encrypt,
			.decrypt	= ablk_encrypt,
			.geniv		= "chainiv",
		},
	},
}, {
	.cra_name		= "lrw(twofish)",
	.cra_driver_name	= "lrw-twofish-avx2",
	.cra_priority		= 500,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,
	.cra_blocksize		= TF_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct async_helper_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_init		= ablk_init,
	.cra_exit		= ablk_exit,
	.cra_u = {
		.ablkcipher = {
			.min_keysize	= TF_MIN_KEY_SIZE +
					  TF_BLOCK_SIZE,
			.max_keysize	= TF_MAX_KEY_SIZE +
					  TF_BLOCK_SIZE,
			.ivsize		= TF_BLOCK_SIZE,
			.setkey		= ablk_set_key,
			.encrypt	= ablk_encrypt,
			.decrypt	= ablk_decrypt,
		},
	},
}, {
	.cra_name		= "xts(twofish)",
	.cra_driver_name	= "xts-twofish-avx2",
	.cra_priority		= 500,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,
	.cra_blocksize		= TF_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct async_helper_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_init		= ablk_init,
	.cra_exit		= ablk_exit,
	.cra_u = {
		.ablkcipher = {
			.min_keysize	= TF_MIN_KEY_SIZE * 2,
			.max_keysize	= TF_MAX_KEY_SIZE * 2,
			.ivsize		= TF_BLOCK_SIZE,
			.setkey		= ablk_set_key,
			.encrypt	= ablk_encrypt,
			.decrypt	= ablk_decrypt,
		},
	},
} };

static int __init init(void)
{
	u64 xcr0;

	if (!cpu_has_avx2 || !cpu_has_osxsave) {
		pr_info("AVX2 instructions are not detected.\n");
		return -ENODEV;
	}

	xcr0 = xgetbv(XCR_XFEATURE_ENABLED_MASK);
	if ((xcr0 & (XSTATE_SSE | XSTATE_YMM)) != (XSTATE_SSE | XSTATE_YMM)) {
		pr_info("AVX2 detected but unusable.\n");
		return -ENODEV;
	}

	return crypto_register_algs(tf_algs, ARRAY_SIZE(tf_algs));
}

static void __exit fini(void)
{
	crypto_unregister_algs(tf_algs, ARRAY_SIZE(tf_algs));
}

module_init(init);
module_exit(fini);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Twofish Cipher Algorithm, AVX2 optimized");
MODULE_ALIAS("twofish");
MODULE_ALIAS("twofish-asm");
