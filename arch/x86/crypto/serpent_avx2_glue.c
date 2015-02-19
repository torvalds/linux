/*
 * Glue Code for x86_64/AVX2 assembler optimized version of Serpent
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
#include <crypto/ablk_helper.h>
#include <crypto/algapi.h>
#include <crypto/ctr.h>
#include <crypto/lrw.h>
#include <crypto/xts.h>
#include <crypto/serpent.h>
#include <asm/xcr.h>
#include <asm/xsave.h>
#include <asm/crypto/serpent-avx.h>
#include <asm/crypto/glue_helper.h>

#define SERPENT_AVX2_PARALLEL_BLOCKS 16

/* 16-way AVX2 parallel cipher functions */
asmlinkage void serpent_ecb_enc_16way(struct serpent_ctx *ctx, u8 *dst,
				      const u8 *src);
asmlinkage void serpent_ecb_dec_16way(struct serpent_ctx *ctx, u8 *dst,
				      const u8 *src);
asmlinkage void serpent_cbc_dec_16way(void *ctx, u128 *dst, const u128 *src);

asmlinkage void serpent_ctr_16way(void *ctx, u128 *dst, const u128 *src,
				  le128 *iv);
asmlinkage void serpent_xts_enc_16way(struct serpent_ctx *ctx, u8 *dst,
				      const u8 *src, le128 *iv);
asmlinkage void serpent_xts_dec_16way(struct serpent_ctx *ctx, u8 *dst,
				      const u8 *src, le128 *iv);

static const struct common_glue_ctx serpent_enc = {
	.num_funcs = 3,
	.fpu_blocks_limit = 8,

	.funcs = { {
		.num_blocks = 16,
		.fn_u = { .ecb = GLUE_FUNC_CAST(serpent_ecb_enc_16way) }
	}, {
		.num_blocks = 8,
		.fn_u = { .ecb = GLUE_FUNC_CAST(serpent_ecb_enc_8way_avx) }
	}, {
		.num_blocks = 1,
		.fn_u = { .ecb = GLUE_FUNC_CAST(__serpent_encrypt) }
	} }
};

static const struct common_glue_ctx serpent_ctr = {
	.num_funcs = 3,
	.fpu_blocks_limit = 8,

	.funcs = { {
		.num_blocks = 16,
		.fn_u = { .ctr = GLUE_CTR_FUNC_CAST(serpent_ctr_16way) }
	},  {
		.num_blocks = 8,
		.fn_u = { .ctr = GLUE_CTR_FUNC_CAST(serpent_ctr_8way_avx) }
	}, {
		.num_blocks = 1,
		.fn_u = { .ctr = GLUE_CTR_FUNC_CAST(__serpent_crypt_ctr) }
	} }
};

static const struct common_glue_ctx serpent_enc_xts = {
	.num_funcs = 3,
	.fpu_blocks_limit = 8,

	.funcs = { {
		.num_blocks = 16,
		.fn_u = { .xts = GLUE_XTS_FUNC_CAST(serpent_xts_enc_16way) }
	}, {
		.num_blocks = 8,
		.fn_u = { .xts = GLUE_XTS_FUNC_CAST(serpent_xts_enc_8way_avx) }
	}, {
		.num_blocks = 1,
		.fn_u = { .xts = GLUE_XTS_FUNC_CAST(serpent_xts_enc) }
	} }
};

static const struct common_glue_ctx serpent_dec = {
	.num_funcs = 3,
	.fpu_blocks_limit = 8,

	.funcs = { {
		.num_blocks = 16,
		.fn_u = { .ecb = GLUE_FUNC_CAST(serpent_ecb_dec_16way) }
	}, {
		.num_blocks = 8,
		.fn_u = { .ecb = GLUE_FUNC_CAST(serpent_ecb_dec_8way_avx) }
	}, {
		.num_blocks = 1,
		.fn_u = { .ecb = GLUE_FUNC_CAST(__serpent_decrypt) }
	} }
};

static const struct common_glue_ctx serpent_dec_cbc = {
	.num_funcs = 3,
	.fpu_blocks_limit = 8,

	.funcs = { {
		.num_blocks = 16,
		.fn_u = { .cbc = GLUE_CBC_FUNC_CAST(serpent_cbc_dec_16way) }
	}, {
		.num_blocks = 8,
		.fn_u = { .cbc = GLUE_CBC_FUNC_CAST(serpent_cbc_dec_8way_avx) }
	}, {
		.num_blocks = 1,
		.fn_u = { .cbc = GLUE_CBC_FUNC_CAST(__serpent_decrypt) }
	} }
};

static const struct common_glue_ctx serpent_dec_xts = {
	.num_funcs = 3,
	.fpu_blocks_limit = 8,

	.funcs = { {
		.num_blocks = 16,
		.fn_u = { .xts = GLUE_XTS_FUNC_CAST(serpent_xts_dec_16way) }
	}, {
		.num_blocks = 8,
		.fn_u = { .xts = GLUE_XTS_FUNC_CAST(serpent_xts_dec_8way_avx) }
	}, {
		.num_blocks = 1,
		.fn_u = { .xts = GLUE_XTS_FUNC_CAST(serpent_xts_dec) }
	} }
};

static int ecb_encrypt(struct blkcipher_desc *desc, struct scatterlist *dst,
		       struct scatterlist *src, unsigned int nbytes)
{
	return glue_ecb_crypt_128bit(&serpent_enc, desc, dst, src, nbytes);
}

static int ecb_decrypt(struct blkcipher_desc *desc, struct scatterlist *dst,
		       struct scatterlist *src, unsigned int nbytes)
{
	return glue_ecb_crypt_128bit(&serpent_dec, desc, dst, src, nbytes);
}

static int cbc_encrypt(struct blkcipher_desc *desc, struct scatterlist *dst,
		       struct scatterlist *src, unsigned int nbytes)
{
	return glue_cbc_encrypt_128bit(GLUE_FUNC_CAST(__serpent_encrypt), desc,
				       dst, src, nbytes);
}

static int cbc_decrypt(struct blkcipher_desc *desc, struct scatterlist *dst,
		       struct scatterlist *src, unsigned int nbytes)
{
	return glue_cbc_decrypt_128bit(&serpent_dec_cbc, desc, dst, src,
				       nbytes);
}

static int ctr_crypt(struct blkcipher_desc *desc, struct scatterlist *dst,
		     struct scatterlist *src, unsigned int nbytes)
{
	return glue_ctr_crypt_128bit(&serpent_ctr, desc, dst, src, nbytes);
}

static inline bool serpent_fpu_begin(bool fpu_enabled, unsigned int nbytes)
{
	/* since reusing AVX functions, starts using FPU at 8 parallel blocks */
	return glue_fpu_begin(SERPENT_BLOCK_SIZE, 8, NULL, fpu_enabled, nbytes);
}

static inline void serpent_fpu_end(bool fpu_enabled)
{
	glue_fpu_end(fpu_enabled);
}

struct crypt_priv {
	struct serpent_ctx *ctx;
	bool fpu_enabled;
};

static void encrypt_callback(void *priv, u8 *srcdst, unsigned int nbytes)
{
	const unsigned int bsize = SERPENT_BLOCK_SIZE;
	struct crypt_priv *ctx = priv;
	int i;

	ctx->fpu_enabled = serpent_fpu_begin(ctx->fpu_enabled, nbytes);

	if (nbytes >= SERPENT_AVX2_PARALLEL_BLOCKS * bsize) {
		serpent_ecb_enc_16way(ctx->ctx, srcdst, srcdst);
		srcdst += bsize * SERPENT_AVX2_PARALLEL_BLOCKS;
		nbytes -= bsize * SERPENT_AVX2_PARALLEL_BLOCKS;
	}

	while (nbytes >= SERPENT_PARALLEL_BLOCKS * bsize) {
		serpent_ecb_enc_8way_avx(ctx->ctx, srcdst, srcdst);
		srcdst += bsize * SERPENT_PARALLEL_BLOCKS;
		nbytes -= bsize * SERPENT_PARALLEL_BLOCKS;
	}

	for (i = 0; i < nbytes / bsize; i++, srcdst += bsize)
		__serpent_encrypt(ctx->ctx, srcdst, srcdst);
}

static void decrypt_callback(void *priv, u8 *srcdst, unsigned int nbytes)
{
	const unsigned int bsize = SERPENT_BLOCK_SIZE;
	struct crypt_priv *ctx = priv;
	int i;

	ctx->fpu_enabled = serpent_fpu_begin(ctx->fpu_enabled, nbytes);

	if (nbytes >= SERPENT_AVX2_PARALLEL_BLOCKS * bsize) {
		serpent_ecb_dec_16way(ctx->ctx, srcdst, srcdst);
		srcdst += bsize * SERPENT_AVX2_PARALLEL_BLOCKS;
		nbytes -= bsize * SERPENT_AVX2_PARALLEL_BLOCKS;
	}

	while (nbytes >= SERPENT_PARALLEL_BLOCKS * bsize) {
		serpent_ecb_dec_8way_avx(ctx->ctx, srcdst, srcdst);
		srcdst += bsize * SERPENT_PARALLEL_BLOCKS;
		nbytes -= bsize * SERPENT_PARALLEL_BLOCKS;
	}

	for (i = 0; i < nbytes / bsize; i++, srcdst += bsize)
		__serpent_decrypt(ctx->ctx, srcdst, srcdst);
}

static int lrw_encrypt(struct blkcipher_desc *desc, struct scatterlist *dst,
		       struct scatterlist *src, unsigned int nbytes)
{
	struct serpent_lrw_ctx *ctx = crypto_blkcipher_ctx(desc->tfm);
	be128 buf[SERPENT_AVX2_PARALLEL_BLOCKS];
	struct crypt_priv crypt_ctx = {
		.ctx = &ctx->serpent_ctx,
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
	serpent_fpu_end(crypt_ctx.fpu_enabled);

	return ret;
}

static int lrw_decrypt(struct blkcipher_desc *desc, struct scatterlist *dst,
		       struct scatterlist *src, unsigned int nbytes)
{
	struct serpent_lrw_ctx *ctx = crypto_blkcipher_ctx(desc->tfm);
	be128 buf[SERPENT_AVX2_PARALLEL_BLOCKS];
	struct crypt_priv crypt_ctx = {
		.ctx = &ctx->serpent_ctx,
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
	serpent_fpu_end(crypt_ctx.fpu_enabled);

	return ret;
}

static int xts_encrypt(struct blkcipher_desc *desc, struct scatterlist *dst,
		       struct scatterlist *src, unsigned int nbytes)
{
	struct serpent_xts_ctx *ctx = crypto_blkcipher_ctx(desc->tfm);

	return glue_xts_crypt_128bit(&serpent_enc_xts, desc, dst, src, nbytes,
				     XTS_TWEAK_CAST(__serpent_encrypt),
				     &ctx->tweak_ctx, &ctx->crypt_ctx);
}

static int xts_decrypt(struct blkcipher_desc *desc, struct scatterlist *dst,
		       struct scatterlist *src, unsigned int nbytes)
{
	struct serpent_xts_ctx *ctx = crypto_blkcipher_ctx(desc->tfm);

	return glue_xts_crypt_128bit(&serpent_dec_xts, desc, dst, src, nbytes,
				     XTS_TWEAK_CAST(__serpent_encrypt),
				     &ctx->tweak_ctx, &ctx->crypt_ctx);
}

static struct crypto_alg srp_algs[10] = { {
	.cra_name		= "__ecb-serpent-avx2",
	.cra_driver_name	= "__driver-ecb-serpent-avx2",
	.cra_priority		= 0,
	.cra_flags		= CRYPTO_ALG_TYPE_BLKCIPHER,
	.cra_blocksize		= SERPENT_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct serpent_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_blkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_list		= LIST_HEAD_INIT(srp_algs[0].cra_list),
	.cra_u = {
		.blkcipher = {
			.min_keysize	= SERPENT_MIN_KEY_SIZE,
			.max_keysize	= SERPENT_MAX_KEY_SIZE,
			.setkey		= serpent_setkey,
			.encrypt	= ecb_encrypt,
			.decrypt	= ecb_decrypt,
		},
	},
}, {
	.cra_name		= "__cbc-serpent-avx2",
	.cra_driver_name	= "__driver-cbc-serpent-avx2",
	.cra_priority		= 0,
	.cra_flags		= CRYPTO_ALG_TYPE_BLKCIPHER,
	.cra_blocksize		= SERPENT_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct serpent_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_blkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_list		= LIST_HEAD_INIT(srp_algs[1].cra_list),
	.cra_u = {
		.blkcipher = {
			.min_keysize	= SERPENT_MIN_KEY_SIZE,
			.max_keysize	= SERPENT_MAX_KEY_SIZE,
			.setkey		= serpent_setkey,
			.encrypt	= cbc_encrypt,
			.decrypt	= cbc_decrypt,
		},
	},
}, {
	.cra_name		= "__ctr-serpent-avx2",
	.cra_driver_name	= "__driver-ctr-serpent-avx2",
	.cra_priority		= 0,
	.cra_flags		= CRYPTO_ALG_TYPE_BLKCIPHER,
	.cra_blocksize		= 1,
	.cra_ctxsize		= sizeof(struct serpent_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_blkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_list		= LIST_HEAD_INIT(srp_algs[2].cra_list),
	.cra_u = {
		.blkcipher = {
			.min_keysize	= SERPENT_MIN_KEY_SIZE,
			.max_keysize	= SERPENT_MAX_KEY_SIZE,
			.ivsize		= SERPENT_BLOCK_SIZE,
			.setkey		= serpent_setkey,
			.encrypt	= ctr_crypt,
			.decrypt	= ctr_crypt,
		},
	},
}, {
	.cra_name		= "__lrw-serpent-avx2",
	.cra_driver_name	= "__driver-lrw-serpent-avx2",
	.cra_priority		= 0,
	.cra_flags		= CRYPTO_ALG_TYPE_BLKCIPHER,
	.cra_blocksize		= SERPENT_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct serpent_lrw_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_blkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_list		= LIST_HEAD_INIT(srp_algs[3].cra_list),
	.cra_exit		= lrw_serpent_exit_tfm,
	.cra_u = {
		.blkcipher = {
			.min_keysize	= SERPENT_MIN_KEY_SIZE +
					  SERPENT_BLOCK_SIZE,
			.max_keysize	= SERPENT_MAX_KEY_SIZE +
					  SERPENT_BLOCK_SIZE,
			.ivsize		= SERPENT_BLOCK_SIZE,
			.setkey		= lrw_serpent_setkey,
			.encrypt	= lrw_encrypt,
			.decrypt	= lrw_decrypt,
		},
	},
}, {
	.cra_name		= "__xts-serpent-avx2",
	.cra_driver_name	= "__driver-xts-serpent-avx2",
	.cra_priority		= 0,
	.cra_flags		= CRYPTO_ALG_TYPE_BLKCIPHER,
	.cra_blocksize		= SERPENT_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct serpent_xts_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_blkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_list		= LIST_HEAD_INIT(srp_algs[4].cra_list),
	.cra_u = {
		.blkcipher = {
			.min_keysize	= SERPENT_MIN_KEY_SIZE * 2,
			.max_keysize	= SERPENT_MAX_KEY_SIZE * 2,
			.ivsize		= SERPENT_BLOCK_SIZE,
			.setkey		= xts_serpent_setkey,
			.encrypt	= xts_encrypt,
			.decrypt	= xts_decrypt,
		},
	},
}, {
	.cra_name		= "ecb(serpent)",
	.cra_driver_name	= "ecb-serpent-avx2",
	.cra_priority		= 600,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,
	.cra_blocksize		= SERPENT_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct async_helper_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_list		= LIST_HEAD_INIT(srp_algs[5].cra_list),
	.cra_init		= ablk_init,
	.cra_exit		= ablk_exit,
	.cra_u = {
		.ablkcipher = {
			.min_keysize	= SERPENT_MIN_KEY_SIZE,
			.max_keysize	= SERPENT_MAX_KEY_SIZE,
			.setkey		= ablk_set_key,
			.encrypt	= ablk_encrypt,
			.decrypt	= ablk_decrypt,
		},
	},
}, {
	.cra_name		= "cbc(serpent)",
	.cra_driver_name	= "cbc-serpent-avx2",
	.cra_priority		= 600,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,
	.cra_blocksize		= SERPENT_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct async_helper_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_list		= LIST_HEAD_INIT(srp_algs[6].cra_list),
	.cra_init		= ablk_init,
	.cra_exit		= ablk_exit,
	.cra_u = {
		.ablkcipher = {
			.min_keysize	= SERPENT_MIN_KEY_SIZE,
			.max_keysize	= SERPENT_MAX_KEY_SIZE,
			.ivsize		= SERPENT_BLOCK_SIZE,
			.setkey		= ablk_set_key,
			.encrypt	= __ablk_encrypt,
			.decrypt	= ablk_decrypt,
		},
	},
}, {
	.cra_name		= "ctr(serpent)",
	.cra_driver_name	= "ctr-serpent-avx2",
	.cra_priority		= 600,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,
	.cra_blocksize		= 1,
	.cra_ctxsize		= sizeof(struct async_helper_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_list		= LIST_HEAD_INIT(srp_algs[7].cra_list),
	.cra_init		= ablk_init,
	.cra_exit		= ablk_exit,
	.cra_u = {
		.ablkcipher = {
			.min_keysize	= SERPENT_MIN_KEY_SIZE,
			.max_keysize	= SERPENT_MAX_KEY_SIZE,
			.ivsize		= SERPENT_BLOCK_SIZE,
			.setkey		= ablk_set_key,
			.encrypt	= ablk_encrypt,
			.decrypt	= ablk_encrypt,
			.geniv		= "chainiv",
		},
	},
}, {
	.cra_name		= "lrw(serpent)",
	.cra_driver_name	= "lrw-serpent-avx2",
	.cra_priority		= 600,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,
	.cra_blocksize		= SERPENT_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct async_helper_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_list		= LIST_HEAD_INIT(srp_algs[8].cra_list),
	.cra_init		= ablk_init,
	.cra_exit		= ablk_exit,
	.cra_u = {
		.ablkcipher = {
			.min_keysize	= SERPENT_MIN_KEY_SIZE +
					  SERPENT_BLOCK_SIZE,
			.max_keysize	= SERPENT_MAX_KEY_SIZE +
					  SERPENT_BLOCK_SIZE,
			.ivsize		= SERPENT_BLOCK_SIZE,
			.setkey		= ablk_set_key,
			.encrypt	= ablk_encrypt,
			.decrypt	= ablk_decrypt,
		},
	},
}, {
	.cra_name		= "xts(serpent)",
	.cra_driver_name	= "xts-serpent-avx2",
	.cra_priority		= 600,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,
	.cra_blocksize		= SERPENT_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct async_helper_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_list		= LIST_HEAD_INIT(srp_algs[9].cra_list),
	.cra_init		= ablk_init,
	.cra_exit		= ablk_exit,
	.cra_u = {
		.ablkcipher = {
			.min_keysize	= SERPENT_MIN_KEY_SIZE * 2,
			.max_keysize	= SERPENT_MAX_KEY_SIZE * 2,
			.ivsize		= SERPENT_BLOCK_SIZE,
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
		pr_info("AVX detected but unusable.\n");
		return -ENODEV;
	}

	return crypto_register_algs(srp_algs, ARRAY_SIZE(srp_algs));
}

static void __exit fini(void)
{
	crypto_unregister_algs(srp_algs, ARRAY_SIZE(srp_algs));
}

module_init(init);
module_exit(fini);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Serpent Cipher Algorithm, AVX2 optimized");
MODULE_ALIAS_CRYPTO("serpent");
MODULE_ALIAS_CRYPTO("serpent-asm");
