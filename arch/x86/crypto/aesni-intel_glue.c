/*
 * Support for Intel AES-NI instructions. This file contains glue
 * code, the real AES implementation is in intel-aes_asm.S.
 *
 * Copyright (C) 2008, Intel Corp.
 *    Author: Huang Ying <ying.huang@intel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/hardirq.h>
#include <linux/types.h>
#include <linux/crypto.h>
#include <linux/err.h>
#include <crypto/algapi.h>
#include <crypto/aes.h>
#include <crypto/cryptd.h>
#include <crypto/ctr.h>
#include <asm/i387.h>
#include <asm/aes.h>

#if defined(CONFIG_CRYPTO_CTR) || defined(CONFIG_CRYPTO_CTR_MODULE)
#define HAS_CTR
#endif

#if defined(CONFIG_CRYPTO_LRW) || defined(CONFIG_CRYPTO_LRW_MODULE)
#define HAS_LRW
#endif

#if defined(CONFIG_CRYPTO_PCBC) || defined(CONFIG_CRYPTO_PCBC_MODULE)
#define HAS_PCBC
#endif

#if defined(CONFIG_CRYPTO_XTS) || defined(CONFIG_CRYPTO_XTS_MODULE)
#define HAS_XTS
#endif

struct async_aes_ctx {
	struct cryptd_ablkcipher *cryptd_tfm;
};

#define AESNI_ALIGN	16
#define AES_BLOCK_MASK	(~(AES_BLOCK_SIZE-1))

asmlinkage int aesni_set_key(struct crypto_aes_ctx *ctx, const u8 *in_key,
			     unsigned int key_len);
asmlinkage void aesni_enc(struct crypto_aes_ctx *ctx, u8 *out,
			  const u8 *in);
asmlinkage void aesni_dec(struct crypto_aes_ctx *ctx, u8 *out,
			  const u8 *in);
asmlinkage void aesni_ecb_enc(struct crypto_aes_ctx *ctx, u8 *out,
			      const u8 *in, unsigned int len);
asmlinkage void aesni_ecb_dec(struct crypto_aes_ctx *ctx, u8 *out,
			      const u8 *in, unsigned int len);
asmlinkage void aesni_cbc_enc(struct crypto_aes_ctx *ctx, u8 *out,
			      const u8 *in, unsigned int len, u8 *iv);
asmlinkage void aesni_cbc_dec(struct crypto_aes_ctx *ctx, u8 *out,
			      const u8 *in, unsigned int len, u8 *iv);
asmlinkage void aesni_ctr_enc(struct crypto_aes_ctx *ctx, u8 *out,
			      const u8 *in, unsigned int len, u8 *iv);

static inline struct crypto_aes_ctx *aes_ctx(void *raw_ctx)
{
	unsigned long addr = (unsigned long)raw_ctx;
	unsigned long align = AESNI_ALIGN;

	if (align <= crypto_tfm_ctx_alignment())
		align = 1;
	return (struct crypto_aes_ctx *)ALIGN(addr, align);
}

static int aes_set_key_common(struct crypto_tfm *tfm, void *raw_ctx,
			      const u8 *in_key, unsigned int key_len)
{
	struct crypto_aes_ctx *ctx = aes_ctx(raw_ctx);
	u32 *flags = &tfm->crt_flags;
	int err;

	if (key_len != AES_KEYSIZE_128 && key_len != AES_KEYSIZE_192 &&
	    key_len != AES_KEYSIZE_256) {
		*flags |= CRYPTO_TFM_RES_BAD_KEY_LEN;
		return -EINVAL;
	}

	if (!irq_fpu_usable())
		err = crypto_aes_expand_key(ctx, in_key, key_len);
	else {
		kernel_fpu_begin();
		err = aesni_set_key(ctx, in_key, key_len);
		kernel_fpu_end();
	}

	return err;
}

static int aes_set_key(struct crypto_tfm *tfm, const u8 *in_key,
		       unsigned int key_len)
{
	return aes_set_key_common(tfm, crypto_tfm_ctx(tfm), in_key, key_len);
}

static void aes_encrypt(struct crypto_tfm *tfm, u8 *dst, const u8 *src)
{
	struct crypto_aes_ctx *ctx = aes_ctx(crypto_tfm_ctx(tfm));

	if (!irq_fpu_usable())
		crypto_aes_encrypt_x86(ctx, dst, src);
	else {
		kernel_fpu_begin();
		aesni_enc(ctx, dst, src);
		kernel_fpu_end();
	}
}

static void aes_decrypt(struct crypto_tfm *tfm, u8 *dst, const u8 *src)
{
	struct crypto_aes_ctx *ctx = aes_ctx(crypto_tfm_ctx(tfm));

	if (!irq_fpu_usable())
		crypto_aes_decrypt_x86(ctx, dst, src);
	else {
		kernel_fpu_begin();
		aesni_dec(ctx, dst, src);
		kernel_fpu_end();
	}
}

static struct crypto_alg aesni_alg = {
	.cra_name		= "aes",
	.cra_driver_name	= "aes-aesni",
	.cra_priority		= 300,
	.cra_flags		= CRYPTO_ALG_TYPE_CIPHER,
	.cra_blocksize		= AES_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct crypto_aes_ctx)+AESNI_ALIGN-1,
	.cra_alignmask		= 0,
	.cra_module		= THIS_MODULE,
	.cra_list		= LIST_HEAD_INIT(aesni_alg.cra_list),
	.cra_u	= {
		.cipher	= {
			.cia_min_keysize	= AES_MIN_KEY_SIZE,
			.cia_max_keysize	= AES_MAX_KEY_SIZE,
			.cia_setkey		= aes_set_key,
			.cia_encrypt		= aes_encrypt,
			.cia_decrypt		= aes_decrypt
		}
	}
};

static void __aes_encrypt(struct crypto_tfm *tfm, u8 *dst, const u8 *src)
{
	struct crypto_aes_ctx *ctx = aes_ctx(crypto_tfm_ctx(tfm));

	aesni_enc(ctx, dst, src);
}

static void __aes_decrypt(struct crypto_tfm *tfm, u8 *dst, const u8 *src)
{
	struct crypto_aes_ctx *ctx = aes_ctx(crypto_tfm_ctx(tfm));

	aesni_dec(ctx, dst, src);
}

static struct crypto_alg __aesni_alg = {
	.cra_name		= "__aes-aesni",
	.cra_driver_name	= "__driver-aes-aesni",
	.cra_priority		= 0,
	.cra_flags		= CRYPTO_ALG_TYPE_CIPHER,
	.cra_blocksize		= AES_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct crypto_aes_ctx)+AESNI_ALIGN-1,
	.cra_alignmask		= 0,
	.cra_module		= THIS_MODULE,
	.cra_list		= LIST_HEAD_INIT(__aesni_alg.cra_list),
	.cra_u	= {
		.cipher	= {
			.cia_min_keysize	= AES_MIN_KEY_SIZE,
			.cia_max_keysize	= AES_MAX_KEY_SIZE,
			.cia_setkey		= aes_set_key,
			.cia_encrypt		= __aes_encrypt,
			.cia_decrypt		= __aes_decrypt
		}
	}
};

static int ecb_encrypt(struct blkcipher_desc *desc,
		       struct scatterlist *dst, struct scatterlist *src,
		       unsigned int nbytes)
{
	struct crypto_aes_ctx *ctx = aes_ctx(crypto_blkcipher_ctx(desc->tfm));
	struct blkcipher_walk walk;
	int err;

	blkcipher_walk_init(&walk, dst, src, nbytes);
	err = blkcipher_walk_virt(desc, &walk);
	desc->flags &= ~CRYPTO_TFM_REQ_MAY_SLEEP;

	kernel_fpu_begin();
	while ((nbytes = walk.nbytes)) {
		aesni_ecb_enc(ctx, walk.dst.virt.addr, walk.src.virt.addr,
			      nbytes & AES_BLOCK_MASK);
		nbytes &= AES_BLOCK_SIZE - 1;
		err = blkcipher_walk_done(desc, &walk, nbytes);
	}
	kernel_fpu_end();

	return err;
}

static int ecb_decrypt(struct blkcipher_desc *desc,
		       struct scatterlist *dst, struct scatterlist *src,
		       unsigned int nbytes)
{
	struct crypto_aes_ctx *ctx = aes_ctx(crypto_blkcipher_ctx(desc->tfm));
	struct blkcipher_walk walk;
	int err;

	blkcipher_walk_init(&walk, dst, src, nbytes);
	err = blkcipher_walk_virt(desc, &walk);
	desc->flags &= ~CRYPTO_TFM_REQ_MAY_SLEEP;

	kernel_fpu_begin();
	while ((nbytes = walk.nbytes)) {
		aesni_ecb_dec(ctx, walk.dst.virt.addr, walk.src.virt.addr,
			      nbytes & AES_BLOCK_MASK);
		nbytes &= AES_BLOCK_SIZE - 1;
		err = blkcipher_walk_done(desc, &walk, nbytes);
	}
	kernel_fpu_end();

	return err;
}

static struct crypto_alg blk_ecb_alg = {
	.cra_name		= "__ecb-aes-aesni",
	.cra_driver_name	= "__driver-ecb-aes-aesni",
	.cra_priority		= 0,
	.cra_flags		= CRYPTO_ALG_TYPE_BLKCIPHER,
	.cra_blocksize		= AES_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct crypto_aes_ctx)+AESNI_ALIGN-1,
	.cra_alignmask		= 0,
	.cra_type		= &crypto_blkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_list		= LIST_HEAD_INIT(blk_ecb_alg.cra_list),
	.cra_u = {
		.blkcipher = {
			.min_keysize	= AES_MIN_KEY_SIZE,
			.max_keysize	= AES_MAX_KEY_SIZE,
			.setkey		= aes_set_key,
			.encrypt	= ecb_encrypt,
			.decrypt	= ecb_decrypt,
		},
	},
};

static int cbc_encrypt(struct blkcipher_desc *desc,
		       struct scatterlist *dst, struct scatterlist *src,
		       unsigned int nbytes)
{
	struct crypto_aes_ctx *ctx = aes_ctx(crypto_blkcipher_ctx(desc->tfm));
	struct blkcipher_walk walk;
	int err;

	blkcipher_walk_init(&walk, dst, src, nbytes);
	err = blkcipher_walk_virt(desc, &walk);
	desc->flags &= ~CRYPTO_TFM_REQ_MAY_SLEEP;

	kernel_fpu_begin();
	while ((nbytes = walk.nbytes)) {
		aesni_cbc_enc(ctx, walk.dst.virt.addr, walk.src.virt.addr,
			      nbytes & AES_BLOCK_MASK, walk.iv);
		nbytes &= AES_BLOCK_SIZE - 1;
		err = blkcipher_walk_done(desc, &walk, nbytes);
	}
	kernel_fpu_end();

	return err;
}

static int cbc_decrypt(struct blkcipher_desc *desc,
		       struct scatterlist *dst, struct scatterlist *src,
		       unsigned int nbytes)
{
	struct crypto_aes_ctx *ctx = aes_ctx(crypto_blkcipher_ctx(desc->tfm));
	struct blkcipher_walk walk;
	int err;

	blkcipher_walk_init(&walk, dst, src, nbytes);
	err = blkcipher_walk_virt(desc, &walk);
	desc->flags &= ~CRYPTO_TFM_REQ_MAY_SLEEP;

	kernel_fpu_begin();
	while ((nbytes = walk.nbytes)) {
		aesni_cbc_dec(ctx, walk.dst.virt.addr, walk.src.virt.addr,
			      nbytes & AES_BLOCK_MASK, walk.iv);
		nbytes &= AES_BLOCK_SIZE - 1;
		err = blkcipher_walk_done(desc, &walk, nbytes);
	}
	kernel_fpu_end();

	return err;
}

static struct crypto_alg blk_cbc_alg = {
	.cra_name		= "__cbc-aes-aesni",
	.cra_driver_name	= "__driver-cbc-aes-aesni",
	.cra_priority		= 0,
	.cra_flags		= CRYPTO_ALG_TYPE_BLKCIPHER,
	.cra_blocksize		= AES_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct crypto_aes_ctx)+AESNI_ALIGN-1,
	.cra_alignmask		= 0,
	.cra_type		= &crypto_blkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_list		= LIST_HEAD_INIT(blk_cbc_alg.cra_list),
	.cra_u = {
		.blkcipher = {
			.min_keysize	= AES_MIN_KEY_SIZE,
			.max_keysize	= AES_MAX_KEY_SIZE,
			.setkey		= aes_set_key,
			.encrypt	= cbc_encrypt,
			.decrypt	= cbc_decrypt,
		},
	},
};

static void ctr_crypt_final(struct crypto_aes_ctx *ctx,
			    struct blkcipher_walk *walk)
{
	u8 *ctrblk = walk->iv;
	u8 keystream[AES_BLOCK_SIZE];
	u8 *src = walk->src.virt.addr;
	u8 *dst = walk->dst.virt.addr;
	unsigned int nbytes = walk->nbytes;

	aesni_enc(ctx, keystream, ctrblk);
	crypto_xor(keystream, src, nbytes);
	memcpy(dst, keystream, nbytes);
	crypto_inc(ctrblk, AES_BLOCK_SIZE);
}

static int ctr_crypt(struct blkcipher_desc *desc,
		     struct scatterlist *dst, struct scatterlist *src,
		     unsigned int nbytes)
{
	struct crypto_aes_ctx *ctx = aes_ctx(crypto_blkcipher_ctx(desc->tfm));
	struct blkcipher_walk walk;
	int err;

	blkcipher_walk_init(&walk, dst, src, nbytes);
	err = blkcipher_walk_virt_block(desc, &walk, AES_BLOCK_SIZE);
	desc->flags &= ~CRYPTO_TFM_REQ_MAY_SLEEP;

	kernel_fpu_begin();
	while ((nbytes = walk.nbytes) >= AES_BLOCK_SIZE) {
		aesni_ctr_enc(ctx, walk.dst.virt.addr, walk.src.virt.addr,
			      nbytes & AES_BLOCK_MASK, walk.iv);
		nbytes &= AES_BLOCK_SIZE - 1;
		err = blkcipher_walk_done(desc, &walk, nbytes);
	}
	if (walk.nbytes) {
		ctr_crypt_final(ctx, &walk);
		err = blkcipher_walk_done(desc, &walk, 0);
	}
	kernel_fpu_end();

	return err;
}

static struct crypto_alg blk_ctr_alg = {
	.cra_name		= "__ctr-aes-aesni",
	.cra_driver_name	= "__driver-ctr-aes-aesni",
	.cra_priority		= 0,
	.cra_flags		= CRYPTO_ALG_TYPE_BLKCIPHER,
	.cra_blocksize		= 1,
	.cra_ctxsize		= sizeof(struct crypto_aes_ctx)+AESNI_ALIGN-1,
	.cra_alignmask		= 0,
	.cra_type		= &crypto_blkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_list		= LIST_HEAD_INIT(blk_ctr_alg.cra_list),
	.cra_u = {
		.blkcipher = {
			.min_keysize	= AES_MIN_KEY_SIZE,
			.max_keysize	= AES_MAX_KEY_SIZE,
			.ivsize		= AES_BLOCK_SIZE,
			.setkey		= aes_set_key,
			.encrypt	= ctr_crypt,
			.decrypt	= ctr_crypt,
		},
	},
};

static int ablk_set_key(struct crypto_ablkcipher *tfm, const u8 *key,
			unsigned int key_len)
{
	struct async_aes_ctx *ctx = crypto_ablkcipher_ctx(tfm);
	struct crypto_ablkcipher *child = &ctx->cryptd_tfm->base;
	int err;

	crypto_ablkcipher_clear_flags(child, CRYPTO_TFM_REQ_MASK);
	crypto_ablkcipher_set_flags(child, crypto_ablkcipher_get_flags(tfm)
				    & CRYPTO_TFM_REQ_MASK);
	err = crypto_ablkcipher_setkey(child, key, key_len);
	crypto_ablkcipher_set_flags(tfm, crypto_ablkcipher_get_flags(child)
				    & CRYPTO_TFM_RES_MASK);
	return err;
}

static int ablk_encrypt(struct ablkcipher_request *req)
{
	struct crypto_ablkcipher *tfm = crypto_ablkcipher_reqtfm(req);
	struct async_aes_ctx *ctx = crypto_ablkcipher_ctx(tfm);

	if (!irq_fpu_usable()) {
		struct ablkcipher_request *cryptd_req =
			ablkcipher_request_ctx(req);
		memcpy(cryptd_req, req, sizeof(*req));
		ablkcipher_request_set_tfm(cryptd_req, &ctx->cryptd_tfm->base);
		return crypto_ablkcipher_encrypt(cryptd_req);
	} else {
		struct blkcipher_desc desc;
		desc.tfm = cryptd_ablkcipher_child(ctx->cryptd_tfm);
		desc.info = req->info;
		desc.flags = 0;
		return crypto_blkcipher_crt(desc.tfm)->encrypt(
			&desc, req->dst, req->src, req->nbytes);
	}
}

static int ablk_decrypt(struct ablkcipher_request *req)
{
	struct crypto_ablkcipher *tfm = crypto_ablkcipher_reqtfm(req);
	struct async_aes_ctx *ctx = crypto_ablkcipher_ctx(tfm);

	if (!irq_fpu_usable()) {
		struct ablkcipher_request *cryptd_req =
			ablkcipher_request_ctx(req);
		memcpy(cryptd_req, req, sizeof(*req));
		ablkcipher_request_set_tfm(cryptd_req, &ctx->cryptd_tfm->base);
		return crypto_ablkcipher_decrypt(cryptd_req);
	} else {
		struct blkcipher_desc desc;
		desc.tfm = cryptd_ablkcipher_child(ctx->cryptd_tfm);
		desc.info = req->info;
		desc.flags = 0;
		return crypto_blkcipher_crt(desc.tfm)->decrypt(
			&desc, req->dst, req->src, req->nbytes);
	}
}

static void ablk_exit(struct crypto_tfm *tfm)
{
	struct async_aes_ctx *ctx = crypto_tfm_ctx(tfm);

	cryptd_free_ablkcipher(ctx->cryptd_tfm);
}

static void ablk_init_common(struct crypto_tfm *tfm,
			     struct cryptd_ablkcipher *cryptd_tfm)
{
	struct async_aes_ctx *ctx = crypto_tfm_ctx(tfm);

	ctx->cryptd_tfm = cryptd_tfm;
	tfm->crt_ablkcipher.reqsize = sizeof(struct ablkcipher_request) +
		crypto_ablkcipher_reqsize(&cryptd_tfm->base);
}

static int ablk_ecb_init(struct crypto_tfm *tfm)
{
	struct cryptd_ablkcipher *cryptd_tfm;

	cryptd_tfm = cryptd_alloc_ablkcipher("__driver-ecb-aes-aesni", 0, 0);
	if (IS_ERR(cryptd_tfm))
		return PTR_ERR(cryptd_tfm);
	ablk_init_common(tfm, cryptd_tfm);
	return 0;
}

static struct crypto_alg ablk_ecb_alg = {
	.cra_name		= "ecb(aes)",
	.cra_driver_name	= "ecb-aes-aesni",
	.cra_priority		= 400,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER|CRYPTO_ALG_ASYNC,
	.cra_blocksize		= AES_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct async_aes_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_list		= LIST_HEAD_INIT(ablk_ecb_alg.cra_list),
	.cra_init		= ablk_ecb_init,
	.cra_exit		= ablk_exit,
	.cra_u = {
		.ablkcipher = {
			.min_keysize	= AES_MIN_KEY_SIZE,
			.max_keysize	= AES_MAX_KEY_SIZE,
			.setkey		= ablk_set_key,
			.encrypt	= ablk_encrypt,
			.decrypt	= ablk_decrypt,
		},
	},
};

static int ablk_cbc_init(struct crypto_tfm *tfm)
{
	struct cryptd_ablkcipher *cryptd_tfm;

	cryptd_tfm = cryptd_alloc_ablkcipher("__driver-cbc-aes-aesni", 0, 0);
	if (IS_ERR(cryptd_tfm))
		return PTR_ERR(cryptd_tfm);
	ablk_init_common(tfm, cryptd_tfm);
	return 0;
}

static struct crypto_alg ablk_cbc_alg = {
	.cra_name		= "cbc(aes)",
	.cra_driver_name	= "cbc-aes-aesni",
	.cra_priority		= 400,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER|CRYPTO_ALG_ASYNC,
	.cra_blocksize		= AES_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct async_aes_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_list		= LIST_HEAD_INIT(ablk_cbc_alg.cra_list),
	.cra_init		= ablk_cbc_init,
	.cra_exit		= ablk_exit,
	.cra_u = {
		.ablkcipher = {
			.min_keysize	= AES_MIN_KEY_SIZE,
			.max_keysize	= AES_MAX_KEY_SIZE,
			.ivsize		= AES_BLOCK_SIZE,
			.setkey		= ablk_set_key,
			.encrypt	= ablk_encrypt,
			.decrypt	= ablk_decrypt,
		},
	},
};

static int ablk_ctr_init(struct crypto_tfm *tfm)
{
	struct cryptd_ablkcipher *cryptd_tfm;

	cryptd_tfm = cryptd_alloc_ablkcipher("__driver-ctr-aes-aesni", 0, 0);
	if (IS_ERR(cryptd_tfm))
		return PTR_ERR(cryptd_tfm);
	ablk_init_common(tfm, cryptd_tfm);
	return 0;
}

static struct crypto_alg ablk_ctr_alg = {
	.cra_name		= "ctr(aes)",
	.cra_driver_name	= "ctr-aes-aesni",
	.cra_priority		= 400,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER|CRYPTO_ALG_ASYNC,
	.cra_blocksize		= 1,
	.cra_ctxsize		= sizeof(struct async_aes_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_list		= LIST_HEAD_INIT(ablk_ctr_alg.cra_list),
	.cra_init		= ablk_ctr_init,
	.cra_exit		= ablk_exit,
	.cra_u = {
		.ablkcipher = {
			.min_keysize	= AES_MIN_KEY_SIZE,
			.max_keysize	= AES_MAX_KEY_SIZE,
			.ivsize		= AES_BLOCK_SIZE,
			.setkey		= ablk_set_key,
			.encrypt	= ablk_encrypt,
			.decrypt	= ablk_encrypt,
			.geniv		= "chainiv",
		},
	},
};

#ifdef HAS_CTR
static int ablk_rfc3686_ctr_init(struct crypto_tfm *tfm)
{
	struct cryptd_ablkcipher *cryptd_tfm;

	cryptd_tfm = cryptd_alloc_ablkcipher(
		"rfc3686(__driver-ctr-aes-aesni)", 0, 0);
	if (IS_ERR(cryptd_tfm))
		return PTR_ERR(cryptd_tfm);
	ablk_init_common(tfm, cryptd_tfm);
	return 0;
}

static struct crypto_alg ablk_rfc3686_ctr_alg = {
	.cra_name		= "rfc3686(ctr(aes))",
	.cra_driver_name	= "rfc3686-ctr-aes-aesni",
	.cra_priority		= 400,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER|CRYPTO_ALG_ASYNC,
	.cra_blocksize		= 1,
	.cra_ctxsize		= sizeof(struct async_aes_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_list		= LIST_HEAD_INIT(ablk_rfc3686_ctr_alg.cra_list),
	.cra_init		= ablk_rfc3686_ctr_init,
	.cra_exit		= ablk_exit,
	.cra_u = {
		.ablkcipher = {
			.min_keysize = AES_MIN_KEY_SIZE+CTR_RFC3686_NONCE_SIZE,
			.max_keysize = AES_MAX_KEY_SIZE+CTR_RFC3686_NONCE_SIZE,
			.ivsize	     = CTR_RFC3686_IV_SIZE,
			.setkey	     = ablk_set_key,
			.encrypt     = ablk_encrypt,
			.decrypt     = ablk_decrypt,
			.geniv	     = "seqiv",
		},
	},
};
#endif

#ifdef HAS_LRW
static int ablk_lrw_init(struct crypto_tfm *tfm)
{
	struct cryptd_ablkcipher *cryptd_tfm;

	cryptd_tfm = cryptd_alloc_ablkcipher("fpu(lrw(__driver-aes-aesni))",
					     0, 0);
	if (IS_ERR(cryptd_tfm))
		return PTR_ERR(cryptd_tfm);
	ablk_init_common(tfm, cryptd_tfm);
	return 0;
}

static struct crypto_alg ablk_lrw_alg = {
	.cra_name		= "lrw(aes)",
	.cra_driver_name	= "lrw-aes-aesni",
	.cra_priority		= 400,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER|CRYPTO_ALG_ASYNC,
	.cra_blocksize		= AES_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct async_aes_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_list		= LIST_HEAD_INIT(ablk_lrw_alg.cra_list),
	.cra_init		= ablk_lrw_init,
	.cra_exit		= ablk_exit,
	.cra_u = {
		.ablkcipher = {
			.min_keysize	= AES_MIN_KEY_SIZE + AES_BLOCK_SIZE,
			.max_keysize	= AES_MAX_KEY_SIZE + AES_BLOCK_SIZE,
			.ivsize		= AES_BLOCK_SIZE,
			.setkey		= ablk_set_key,
			.encrypt	= ablk_encrypt,
			.decrypt	= ablk_decrypt,
		},
	},
};
#endif

#ifdef HAS_PCBC
static int ablk_pcbc_init(struct crypto_tfm *tfm)
{
	struct cryptd_ablkcipher *cryptd_tfm;

	cryptd_tfm = cryptd_alloc_ablkcipher("fpu(pcbc(__driver-aes-aesni))",
					     0, 0);
	if (IS_ERR(cryptd_tfm))
		return PTR_ERR(cryptd_tfm);
	ablk_init_common(tfm, cryptd_tfm);
	return 0;
}

static struct crypto_alg ablk_pcbc_alg = {
	.cra_name		= "pcbc(aes)",
	.cra_driver_name	= "pcbc-aes-aesni",
	.cra_priority		= 400,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER|CRYPTO_ALG_ASYNC,
	.cra_blocksize		= AES_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct async_aes_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_list		= LIST_HEAD_INIT(ablk_pcbc_alg.cra_list),
	.cra_init		= ablk_pcbc_init,
	.cra_exit		= ablk_exit,
	.cra_u = {
		.ablkcipher = {
			.min_keysize	= AES_MIN_KEY_SIZE,
			.max_keysize	= AES_MAX_KEY_SIZE,
			.ivsize		= AES_BLOCK_SIZE,
			.setkey		= ablk_set_key,
			.encrypt	= ablk_encrypt,
			.decrypt	= ablk_decrypt,
		},
	},
};
#endif

#ifdef HAS_XTS
static int ablk_xts_init(struct crypto_tfm *tfm)
{
	struct cryptd_ablkcipher *cryptd_tfm;

	cryptd_tfm = cryptd_alloc_ablkcipher("fpu(xts(__driver-aes-aesni))",
					     0, 0);
	if (IS_ERR(cryptd_tfm))
		return PTR_ERR(cryptd_tfm);
	ablk_init_common(tfm, cryptd_tfm);
	return 0;
}

static struct crypto_alg ablk_xts_alg = {
	.cra_name		= "xts(aes)",
	.cra_driver_name	= "xts-aes-aesni",
	.cra_priority		= 400,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER|CRYPTO_ALG_ASYNC,
	.cra_blocksize		= AES_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct async_aes_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_list		= LIST_HEAD_INIT(ablk_xts_alg.cra_list),
	.cra_init		= ablk_xts_init,
	.cra_exit		= ablk_exit,
	.cra_u = {
		.ablkcipher = {
			.min_keysize	= 2 * AES_MIN_KEY_SIZE,
			.max_keysize	= 2 * AES_MAX_KEY_SIZE,
			.ivsize		= AES_BLOCK_SIZE,
			.setkey		= ablk_set_key,
			.encrypt	= ablk_encrypt,
			.decrypt	= ablk_decrypt,
		},
	},
};
#endif

static int __init aesni_init(void)
{
	int err;

	if (!cpu_has_aes) {
		printk(KERN_INFO "Intel AES-NI instructions are not detected.\n");
		return -ENODEV;
	}
	if ((err = crypto_register_alg(&aesni_alg)))
		goto aes_err;
	if ((err = crypto_register_alg(&__aesni_alg)))
		goto __aes_err;
	if ((err = crypto_register_alg(&blk_ecb_alg)))
		goto blk_ecb_err;
	if ((err = crypto_register_alg(&blk_cbc_alg)))
		goto blk_cbc_err;
	if ((err = crypto_register_alg(&blk_ctr_alg)))
		goto blk_ctr_err;
	if ((err = crypto_register_alg(&ablk_ecb_alg)))
		goto ablk_ecb_err;
	if ((err = crypto_register_alg(&ablk_cbc_alg)))
		goto ablk_cbc_err;
	if ((err = crypto_register_alg(&ablk_ctr_alg)))
		goto ablk_ctr_err;
#ifdef HAS_CTR
	if ((err = crypto_register_alg(&ablk_rfc3686_ctr_alg)))
		goto ablk_rfc3686_ctr_err;
#endif
#ifdef HAS_LRW
	if ((err = crypto_register_alg(&ablk_lrw_alg)))
		goto ablk_lrw_err;
#endif
#ifdef HAS_PCBC
	if ((err = crypto_register_alg(&ablk_pcbc_alg)))
		goto ablk_pcbc_err;
#endif
#ifdef HAS_XTS
	if ((err = crypto_register_alg(&ablk_xts_alg)))
		goto ablk_xts_err;
#endif

	return err;

#ifdef HAS_XTS
ablk_xts_err:
#endif
#ifdef HAS_PCBC
	crypto_unregister_alg(&ablk_pcbc_alg);
ablk_pcbc_err:
#endif
#ifdef HAS_LRW
	crypto_unregister_alg(&ablk_lrw_alg);
ablk_lrw_err:
#endif
#ifdef HAS_CTR
	crypto_unregister_alg(&ablk_rfc3686_ctr_alg);
ablk_rfc3686_ctr_err:
#endif
	crypto_unregister_alg(&ablk_ctr_alg);
ablk_ctr_err:
	crypto_unregister_alg(&ablk_cbc_alg);
ablk_cbc_err:
	crypto_unregister_alg(&ablk_ecb_alg);
ablk_ecb_err:
	crypto_unregister_alg(&blk_ctr_alg);
blk_ctr_err:
	crypto_unregister_alg(&blk_cbc_alg);
blk_cbc_err:
	crypto_unregister_alg(&blk_ecb_alg);
blk_ecb_err:
	crypto_unregister_alg(&__aesni_alg);
__aes_err:
	crypto_unregister_alg(&aesni_alg);
aes_err:
	return err;
}

static void __exit aesni_exit(void)
{
#ifdef HAS_XTS
	crypto_unregister_alg(&ablk_xts_alg);
#endif
#ifdef HAS_PCBC
	crypto_unregister_alg(&ablk_pcbc_alg);
#endif
#ifdef HAS_LRW
	crypto_unregister_alg(&ablk_lrw_alg);
#endif
#ifdef HAS_CTR
	crypto_unregister_alg(&ablk_rfc3686_ctr_alg);
#endif
	crypto_unregister_alg(&ablk_ctr_alg);
	crypto_unregister_alg(&ablk_cbc_alg);
	crypto_unregister_alg(&ablk_ecb_alg);
	crypto_unregister_alg(&blk_ctr_alg);
	crypto_unregister_alg(&blk_cbc_alg);
	crypto_unregister_alg(&blk_ecb_alg);
	crypto_unregister_alg(&__aesni_alg);
	crypto_unregister_alg(&aesni_alg);
}

module_init(aesni_init);
module_exit(aesni_exit);

MODULE_DESCRIPTION("Rijndael (AES) Cipher Algorithm, Intel AES-NI instructions optimized");
MODULE_LICENSE("GPL");
MODULE_ALIAS("aes");
