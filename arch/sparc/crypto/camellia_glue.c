// SPDX-License-Identifier: GPL-2.0-only
/* Glue code for CAMELLIA encryption optimized for sparc64 crypto opcodes.
 *
 * Copyright (C) 2012 David S. Miller <davem@davemloft.net>
 */

#define pr_fmt(fmt)	KBUILD_MODNAME ": " fmt

#include <linux/crypto.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/types.h>
#include <crypto/algapi.h>

#include <asm/fpumacro.h>
#include <asm/pstate.h>
#include <asm/elf.h>

#include "opcodes.h"

#define CAMELLIA_MIN_KEY_SIZE        16
#define CAMELLIA_MAX_KEY_SIZE        32
#define CAMELLIA_BLOCK_SIZE          16
#define CAMELLIA_TABLE_BYTE_LEN     272

struct camellia_sparc64_ctx {
	u64 encrypt_key[CAMELLIA_TABLE_BYTE_LEN / sizeof(u64)];
	u64 decrypt_key[CAMELLIA_TABLE_BYTE_LEN / sizeof(u64)];
	int key_len;
};

extern void camellia_sparc64_key_expand(const u32 *in_key, u64 *encrypt_key,
					unsigned int key_len, u64 *decrypt_key);

static int camellia_set_key(struct crypto_tfm *tfm, const u8 *_in_key,
			    unsigned int key_len)
{
	struct camellia_sparc64_ctx *ctx = crypto_tfm_ctx(tfm);
	const u32 *in_key = (const u32 *) _in_key;
	u32 *flags = &tfm->crt_flags;

	if (key_len != 16 && key_len != 24 && key_len != 32) {
		*flags |= CRYPTO_TFM_RES_BAD_KEY_LEN;
		return -EINVAL;
	}

	ctx->key_len = key_len;

	camellia_sparc64_key_expand(in_key, &ctx->encrypt_key[0],
				    key_len, &ctx->decrypt_key[0]);
	return 0;
}

extern void camellia_sparc64_crypt(const u64 *key, const u32 *input,
				   u32 *output, unsigned int key_len);

static void camellia_encrypt(struct crypto_tfm *tfm, u8 *dst, const u8 *src)
{
	struct camellia_sparc64_ctx *ctx = crypto_tfm_ctx(tfm);

	camellia_sparc64_crypt(&ctx->encrypt_key[0],
			       (const u32 *) src,
			       (u32 *) dst, ctx->key_len);
}

static void camellia_decrypt(struct crypto_tfm *tfm, u8 *dst, const u8 *src)
{
	struct camellia_sparc64_ctx *ctx = crypto_tfm_ctx(tfm);

	camellia_sparc64_crypt(&ctx->decrypt_key[0],
			       (const u32 *) src,
			       (u32 *) dst, ctx->key_len);
}

extern void camellia_sparc64_load_keys(const u64 *key, unsigned int key_len);

typedef void ecb_crypt_op(const u64 *input, u64 *output, unsigned int len,
			  const u64 *key);

extern ecb_crypt_op camellia_sparc64_ecb_crypt_3_grand_rounds;
extern ecb_crypt_op camellia_sparc64_ecb_crypt_4_grand_rounds;

#define CAMELLIA_BLOCK_MASK	(~(CAMELLIA_BLOCK_SIZE - 1))

static int __ecb_crypt(struct blkcipher_desc *desc,
		       struct scatterlist *dst, struct scatterlist *src,
		       unsigned int nbytes, bool encrypt)
{
	struct camellia_sparc64_ctx *ctx = crypto_blkcipher_ctx(desc->tfm);
	struct blkcipher_walk walk;
	ecb_crypt_op *op;
	const u64 *key;
	int err;

	op = camellia_sparc64_ecb_crypt_3_grand_rounds;
	if (ctx->key_len != 16)
		op = camellia_sparc64_ecb_crypt_4_grand_rounds;

	blkcipher_walk_init(&walk, dst, src, nbytes);
	err = blkcipher_walk_virt(desc, &walk);
	desc->flags &= ~CRYPTO_TFM_REQ_MAY_SLEEP;

	if (encrypt)
		key = &ctx->encrypt_key[0];
	else
		key = &ctx->decrypt_key[0];
	camellia_sparc64_load_keys(key, ctx->key_len);
	while ((nbytes = walk.nbytes)) {
		unsigned int block_len = nbytes & CAMELLIA_BLOCK_MASK;

		if (likely(block_len)) {
			const u64 *src64;
			u64 *dst64;

			src64 = (const u64 *)walk.src.virt.addr;
			dst64 = (u64 *) walk.dst.virt.addr;
			op(src64, dst64, block_len, key);
		}
		nbytes &= CAMELLIA_BLOCK_SIZE - 1;
		err = blkcipher_walk_done(desc, &walk, nbytes);
	}
	fprs_write(0);
	return err;
}

static int ecb_encrypt(struct blkcipher_desc *desc,
		       struct scatterlist *dst, struct scatterlist *src,
		       unsigned int nbytes)
{
	return __ecb_crypt(desc, dst, src, nbytes, true);
}

static int ecb_decrypt(struct blkcipher_desc *desc,
		       struct scatterlist *dst, struct scatterlist *src,
		       unsigned int nbytes)
{
	return __ecb_crypt(desc, dst, src, nbytes, false);
}

typedef void cbc_crypt_op(const u64 *input, u64 *output, unsigned int len,
			  const u64 *key, u64 *iv);

extern cbc_crypt_op camellia_sparc64_cbc_encrypt_3_grand_rounds;
extern cbc_crypt_op camellia_sparc64_cbc_encrypt_4_grand_rounds;
extern cbc_crypt_op camellia_sparc64_cbc_decrypt_3_grand_rounds;
extern cbc_crypt_op camellia_sparc64_cbc_decrypt_4_grand_rounds;

static int cbc_encrypt(struct blkcipher_desc *desc,
		       struct scatterlist *dst, struct scatterlist *src,
		       unsigned int nbytes)
{
	struct camellia_sparc64_ctx *ctx = crypto_blkcipher_ctx(desc->tfm);
	struct blkcipher_walk walk;
	cbc_crypt_op *op;
	const u64 *key;
	int err;

	op = camellia_sparc64_cbc_encrypt_3_grand_rounds;
	if (ctx->key_len != 16)
		op = camellia_sparc64_cbc_encrypt_4_grand_rounds;

	blkcipher_walk_init(&walk, dst, src, nbytes);
	err = blkcipher_walk_virt(desc, &walk);
	desc->flags &= ~CRYPTO_TFM_REQ_MAY_SLEEP;

	key = &ctx->encrypt_key[0];
	camellia_sparc64_load_keys(key, ctx->key_len);
	while ((nbytes = walk.nbytes)) {
		unsigned int block_len = nbytes & CAMELLIA_BLOCK_MASK;

		if (likely(block_len)) {
			const u64 *src64;
			u64 *dst64;

			src64 = (const u64 *)walk.src.virt.addr;
			dst64 = (u64 *) walk.dst.virt.addr;
			op(src64, dst64, block_len, key,
			   (u64 *) walk.iv);
		}
		nbytes &= CAMELLIA_BLOCK_SIZE - 1;
		err = blkcipher_walk_done(desc, &walk, nbytes);
	}
	fprs_write(0);
	return err;
}

static int cbc_decrypt(struct blkcipher_desc *desc,
		       struct scatterlist *dst, struct scatterlist *src,
		       unsigned int nbytes)
{
	struct camellia_sparc64_ctx *ctx = crypto_blkcipher_ctx(desc->tfm);
	struct blkcipher_walk walk;
	cbc_crypt_op *op;
	const u64 *key;
	int err;

	op = camellia_sparc64_cbc_decrypt_3_grand_rounds;
	if (ctx->key_len != 16)
		op = camellia_sparc64_cbc_decrypt_4_grand_rounds;

	blkcipher_walk_init(&walk, dst, src, nbytes);
	err = blkcipher_walk_virt(desc, &walk);
	desc->flags &= ~CRYPTO_TFM_REQ_MAY_SLEEP;

	key = &ctx->decrypt_key[0];
	camellia_sparc64_load_keys(key, ctx->key_len);
	while ((nbytes = walk.nbytes)) {
		unsigned int block_len = nbytes & CAMELLIA_BLOCK_MASK;

		if (likely(block_len)) {
			const u64 *src64;
			u64 *dst64;

			src64 = (const u64 *)walk.src.virt.addr;
			dst64 = (u64 *) walk.dst.virt.addr;
			op(src64, dst64, block_len, key,
			   (u64 *) walk.iv);
		}
		nbytes &= CAMELLIA_BLOCK_SIZE - 1;
		err = blkcipher_walk_done(desc, &walk, nbytes);
	}
	fprs_write(0);
	return err;
}

static struct crypto_alg algs[] = { {
	.cra_name		= "camellia",
	.cra_driver_name	= "camellia-sparc64",
	.cra_priority		= SPARC_CR_OPCODE_PRIORITY,
	.cra_flags		= CRYPTO_ALG_TYPE_CIPHER,
	.cra_blocksize		= CAMELLIA_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct camellia_sparc64_ctx),
	.cra_alignmask		= 3,
	.cra_module		= THIS_MODULE,
	.cra_u	= {
		.cipher	= {
			.cia_min_keysize	= CAMELLIA_MIN_KEY_SIZE,
			.cia_max_keysize	= CAMELLIA_MAX_KEY_SIZE,
			.cia_setkey		= camellia_set_key,
			.cia_encrypt		= camellia_encrypt,
			.cia_decrypt		= camellia_decrypt
		}
	}
}, {
	.cra_name		= "ecb(camellia)",
	.cra_driver_name	= "ecb-camellia-sparc64",
	.cra_priority		= SPARC_CR_OPCODE_PRIORITY,
	.cra_flags		= CRYPTO_ALG_TYPE_BLKCIPHER,
	.cra_blocksize		= CAMELLIA_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct camellia_sparc64_ctx),
	.cra_alignmask		= 7,
	.cra_type		= &crypto_blkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_u = {
		.blkcipher = {
			.min_keysize	= CAMELLIA_MIN_KEY_SIZE,
			.max_keysize	= CAMELLIA_MAX_KEY_SIZE,
			.setkey		= camellia_set_key,
			.encrypt	= ecb_encrypt,
			.decrypt	= ecb_decrypt,
		},
	},
}, {
	.cra_name		= "cbc(camellia)",
	.cra_driver_name	= "cbc-camellia-sparc64",
	.cra_priority		= SPARC_CR_OPCODE_PRIORITY,
	.cra_flags		= CRYPTO_ALG_TYPE_BLKCIPHER,
	.cra_blocksize		= CAMELLIA_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct camellia_sparc64_ctx),
	.cra_alignmask		= 7,
	.cra_type		= &crypto_blkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_u = {
		.blkcipher = {
			.min_keysize	= CAMELLIA_MIN_KEY_SIZE,
			.max_keysize	= CAMELLIA_MAX_KEY_SIZE,
			.ivsize		= CAMELLIA_BLOCK_SIZE,
			.setkey		= camellia_set_key,
			.encrypt	= cbc_encrypt,
			.decrypt	= cbc_decrypt,
		},
	},
}
};

static bool __init sparc64_has_camellia_opcode(void)
{
	unsigned long cfr;

	if (!(sparc64_elf_hwcap & HWCAP_SPARC_CRYPTO))
		return false;

	__asm__ __volatile__("rd %%asr26, %0" : "=r" (cfr));
	if (!(cfr & CFR_CAMELLIA))
		return false;

	return true;
}

static int __init camellia_sparc64_mod_init(void)
{
	if (sparc64_has_camellia_opcode()) {
		pr_info("Using sparc64 camellia opcodes optimized CAMELLIA implementation\n");
		return crypto_register_algs(algs, ARRAY_SIZE(algs));
	}
	pr_info("sparc64 camellia opcodes not available.\n");
	return -ENODEV;
}

static void __exit camellia_sparc64_mod_fini(void)
{
	crypto_unregister_algs(algs, ARRAY_SIZE(algs));
}

module_init(camellia_sparc64_mod_init);
module_exit(camellia_sparc64_mod_fini);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Camellia Cipher Algorithm, sparc64 camellia opcode accelerated");

MODULE_ALIAS_CRYPTO("camellia");

#include "crop_devid.c"
