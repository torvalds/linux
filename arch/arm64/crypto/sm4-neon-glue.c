/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * SM4 Cipher Algorithm, using ARMv8 NEON
 * as specified in
 * https://tools.ietf.org/id/draft-ribose-cfrg-sm4-10.html
 *
 * Copyright (C) 2022, Alibaba Group.
 * Copyright (C) 2022 Tianjia Zhang <tianjia.zhang@linux.alibaba.com>
 */

#include <linux/module.h>
#include <linux/crypto.h>
#include <linux/kernel.h>
#include <linux/cpufeature.h>
#include <asm/neon.h>
#include <asm/simd.h>
#include <crypto/internal/simd.h>
#include <crypto/internal/skcipher.h>
#include <crypto/sm4.h>

#define BYTES2BLKS(nbytes)	((nbytes) >> 4)
#define BYTES2BLK8(nbytes)	(((nbytes) >> 4) & ~(8 - 1))

asmlinkage void sm4_neon_crypt_blk1_8(const u32 *rkey, u8 *dst, const u8 *src,
				      unsigned int nblks);
asmlinkage void sm4_neon_crypt_blk8(const u32 *rkey, u8 *dst, const u8 *src,
				    unsigned int nblks);
asmlinkage void sm4_neon_cbc_dec_blk8(const u32 *rkey, u8 *dst, const u8 *src,
				      u8 *iv, unsigned int nblks);
asmlinkage void sm4_neon_cfb_dec_blk8(const u32 *rkey, u8 *dst, const u8 *src,
				      u8 *iv, unsigned int nblks);
asmlinkage void sm4_neon_ctr_enc_blk8(const u32 *rkey, u8 *dst, const u8 *src,
				      u8 *iv, unsigned int nblks);

static int sm4_setkey(struct crypto_skcipher *tfm, const u8 *key,
		      unsigned int key_len)
{
	struct sm4_ctx *ctx = crypto_skcipher_ctx(tfm);

	return sm4_expandkey(ctx, key, key_len);
}

static int sm4_ecb_do_crypt(struct skcipher_request *req, const u32 *rkey)
{
	struct skcipher_walk walk;
	unsigned int nbytes;
	int err;

	err = skcipher_walk_virt(&walk, req, false);

	while ((nbytes = walk.nbytes) > 0) {
		const u8 *src = walk.src.virt.addr;
		u8 *dst = walk.dst.virt.addr;
		unsigned int nblks;

		kernel_neon_begin();

		nblks = BYTES2BLK8(nbytes);
		if (nblks) {
			sm4_neon_crypt_blk8(rkey, dst, src, nblks);
			dst += nblks * SM4_BLOCK_SIZE;
			src += nblks * SM4_BLOCK_SIZE;
			nbytes -= nblks * SM4_BLOCK_SIZE;
		}

		nblks = BYTES2BLKS(nbytes);
		if (nblks) {
			sm4_neon_crypt_blk1_8(rkey, dst, src, nblks);
			nbytes -= nblks * SM4_BLOCK_SIZE;
		}

		kernel_neon_end();

		err = skcipher_walk_done(&walk, nbytes);
	}

	return err;
}

static int sm4_ecb_encrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct sm4_ctx *ctx = crypto_skcipher_ctx(tfm);

	return sm4_ecb_do_crypt(req, ctx->rkey_enc);
}

static int sm4_ecb_decrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct sm4_ctx *ctx = crypto_skcipher_ctx(tfm);

	return sm4_ecb_do_crypt(req, ctx->rkey_dec);
}

static int sm4_cbc_encrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct sm4_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct skcipher_walk walk;
	unsigned int nbytes;
	int err;

	err = skcipher_walk_virt(&walk, req, false);

	while ((nbytes = walk.nbytes) > 0) {
		const u8 *iv = walk.iv;
		const u8 *src = walk.src.virt.addr;
		u8 *dst = walk.dst.virt.addr;

		while (nbytes >= SM4_BLOCK_SIZE) {
			crypto_xor_cpy(dst, src, iv, SM4_BLOCK_SIZE);
			sm4_crypt_block(ctx->rkey_enc, dst, dst);
			iv = dst;
			src += SM4_BLOCK_SIZE;
			dst += SM4_BLOCK_SIZE;
			nbytes -= SM4_BLOCK_SIZE;
		}
		if (iv != walk.iv)
			memcpy(walk.iv, iv, SM4_BLOCK_SIZE);

		err = skcipher_walk_done(&walk, nbytes);
	}

	return err;
}

static int sm4_cbc_decrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct sm4_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct skcipher_walk walk;
	unsigned int nbytes;
	int err;

	err = skcipher_walk_virt(&walk, req, false);

	while ((nbytes = walk.nbytes) > 0) {
		const u8 *src = walk.src.virt.addr;
		u8 *dst = walk.dst.virt.addr;
		unsigned int nblks;

		kernel_neon_begin();

		nblks = BYTES2BLK8(nbytes);
		if (nblks) {
			sm4_neon_cbc_dec_blk8(ctx->rkey_dec, dst, src,
					walk.iv, nblks);
			dst += nblks * SM4_BLOCK_SIZE;
			src += nblks * SM4_BLOCK_SIZE;
			nbytes -= nblks * SM4_BLOCK_SIZE;
		}

		nblks = BYTES2BLKS(nbytes);
		if (nblks) {
			u8 keystream[SM4_BLOCK_SIZE * 8];
			u8 iv[SM4_BLOCK_SIZE];
			int i;

			sm4_neon_crypt_blk1_8(ctx->rkey_dec, keystream,
					src, nblks);

			src += ((int)nblks - 2) * SM4_BLOCK_SIZE;
			dst += (nblks - 1) * SM4_BLOCK_SIZE;
			memcpy(iv, src + SM4_BLOCK_SIZE, SM4_BLOCK_SIZE);

			for (i = nblks - 1; i > 0; i--) {
				crypto_xor_cpy(dst, src,
					&keystream[i * SM4_BLOCK_SIZE],
					SM4_BLOCK_SIZE);
				src -= SM4_BLOCK_SIZE;
				dst -= SM4_BLOCK_SIZE;
			}
			crypto_xor_cpy(dst, walk.iv,
					keystream, SM4_BLOCK_SIZE);
			memcpy(walk.iv, iv, SM4_BLOCK_SIZE);
			nbytes -= nblks * SM4_BLOCK_SIZE;
		}

		kernel_neon_end();

		err = skcipher_walk_done(&walk, nbytes);
	}

	return err;
}

static int sm4_cfb_encrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct sm4_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct skcipher_walk walk;
	unsigned int nbytes;
	int err;

	err = skcipher_walk_virt(&walk, req, false);

	while ((nbytes = walk.nbytes) > 0) {
		u8 keystream[SM4_BLOCK_SIZE];
		const u8 *iv = walk.iv;
		const u8 *src = walk.src.virt.addr;
		u8 *dst = walk.dst.virt.addr;

		while (nbytes >= SM4_BLOCK_SIZE) {
			sm4_crypt_block(ctx->rkey_enc, keystream, iv);
			crypto_xor_cpy(dst, src, keystream, SM4_BLOCK_SIZE);
			iv = dst;
			src += SM4_BLOCK_SIZE;
			dst += SM4_BLOCK_SIZE;
			nbytes -= SM4_BLOCK_SIZE;
		}
		if (iv != walk.iv)
			memcpy(walk.iv, iv, SM4_BLOCK_SIZE);

		/* tail */
		if (walk.nbytes == walk.total && nbytes > 0) {
			sm4_crypt_block(ctx->rkey_enc, keystream, walk.iv);
			crypto_xor_cpy(dst, src, keystream, nbytes);
			nbytes = 0;
		}

		err = skcipher_walk_done(&walk, nbytes);
	}

	return err;
}

static int sm4_cfb_decrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct sm4_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct skcipher_walk walk;
	unsigned int nbytes;
	int err;

	err = skcipher_walk_virt(&walk, req, false);

	while ((nbytes = walk.nbytes) > 0) {
		const u8 *src = walk.src.virt.addr;
		u8 *dst = walk.dst.virt.addr;
		unsigned int nblks;

		kernel_neon_begin();

		nblks = BYTES2BLK8(nbytes);
		if (nblks) {
			sm4_neon_cfb_dec_blk8(ctx->rkey_enc, dst, src,
					walk.iv, nblks);
			dst += nblks * SM4_BLOCK_SIZE;
			src += nblks * SM4_BLOCK_SIZE;
			nbytes -= nblks * SM4_BLOCK_SIZE;
		}

		nblks = BYTES2BLKS(nbytes);
		if (nblks) {
			u8 keystream[SM4_BLOCK_SIZE * 8];

			memcpy(keystream, walk.iv, SM4_BLOCK_SIZE);
			if (nblks > 1)
				memcpy(&keystream[SM4_BLOCK_SIZE], src,
					(nblks - 1) * SM4_BLOCK_SIZE);
			memcpy(walk.iv, src + (nblks - 1) * SM4_BLOCK_SIZE,
				SM4_BLOCK_SIZE);

			sm4_neon_crypt_blk1_8(ctx->rkey_enc, keystream,
					keystream, nblks);

			crypto_xor_cpy(dst, src, keystream,
					nblks * SM4_BLOCK_SIZE);
			dst += nblks * SM4_BLOCK_SIZE;
			src += nblks * SM4_BLOCK_SIZE;
			nbytes -= nblks * SM4_BLOCK_SIZE;
		}

		kernel_neon_end();

		/* tail */
		if (walk.nbytes == walk.total && nbytes > 0) {
			u8 keystream[SM4_BLOCK_SIZE];

			sm4_crypt_block(ctx->rkey_enc, keystream, walk.iv);
			crypto_xor_cpy(dst, src, keystream, nbytes);
			nbytes = 0;
		}

		err = skcipher_walk_done(&walk, nbytes);
	}

	return err;
}

static int sm4_ctr_crypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct sm4_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct skcipher_walk walk;
	unsigned int nbytes;
	int err;

	err = skcipher_walk_virt(&walk, req, false);

	while ((nbytes = walk.nbytes) > 0) {
		const u8 *src = walk.src.virt.addr;
		u8 *dst = walk.dst.virt.addr;
		unsigned int nblks;

		kernel_neon_begin();

		nblks = BYTES2BLK8(nbytes);
		if (nblks) {
			sm4_neon_ctr_enc_blk8(ctx->rkey_enc, dst, src,
					walk.iv, nblks);
			dst += nblks * SM4_BLOCK_SIZE;
			src += nblks * SM4_BLOCK_SIZE;
			nbytes -= nblks * SM4_BLOCK_SIZE;
		}

		nblks = BYTES2BLKS(nbytes);
		if (nblks) {
			u8 keystream[SM4_BLOCK_SIZE * 8];
			int i;

			for (i = 0; i < nblks; i++) {
				memcpy(&keystream[i * SM4_BLOCK_SIZE],
					walk.iv, SM4_BLOCK_SIZE);
				crypto_inc(walk.iv, SM4_BLOCK_SIZE);
			}
			sm4_neon_crypt_blk1_8(ctx->rkey_enc, keystream,
					keystream, nblks);

			crypto_xor_cpy(dst, src, keystream,
					nblks * SM4_BLOCK_SIZE);
			dst += nblks * SM4_BLOCK_SIZE;
			src += nblks * SM4_BLOCK_SIZE;
			nbytes -= nblks * SM4_BLOCK_SIZE;
		}

		kernel_neon_end();

		/* tail */
		if (walk.nbytes == walk.total && nbytes > 0) {
			u8 keystream[SM4_BLOCK_SIZE];

			sm4_crypt_block(ctx->rkey_enc, keystream, walk.iv);
			crypto_inc(walk.iv, SM4_BLOCK_SIZE);
			crypto_xor_cpy(dst, src, keystream, nbytes);
			nbytes = 0;
		}

		err = skcipher_walk_done(&walk, nbytes);
	}

	return err;
}

static struct skcipher_alg sm4_algs[] = {
	{
		.base = {
			.cra_name		= "ecb(sm4)",
			.cra_driver_name	= "ecb-sm4-neon",
			.cra_priority		= 200,
			.cra_blocksize		= SM4_BLOCK_SIZE,
			.cra_ctxsize		= sizeof(struct sm4_ctx),
			.cra_module		= THIS_MODULE,
		},
		.min_keysize	= SM4_KEY_SIZE,
		.max_keysize	= SM4_KEY_SIZE,
		.setkey		= sm4_setkey,
		.encrypt	= sm4_ecb_encrypt,
		.decrypt	= sm4_ecb_decrypt,
	}, {
		.base = {
			.cra_name		= "cbc(sm4)",
			.cra_driver_name	= "cbc-sm4-neon",
			.cra_priority		= 200,
			.cra_blocksize		= SM4_BLOCK_SIZE,
			.cra_ctxsize		= sizeof(struct sm4_ctx),
			.cra_module		= THIS_MODULE,
		},
		.min_keysize	= SM4_KEY_SIZE,
		.max_keysize	= SM4_KEY_SIZE,
		.ivsize		= SM4_BLOCK_SIZE,
		.setkey		= sm4_setkey,
		.encrypt	= sm4_cbc_encrypt,
		.decrypt	= sm4_cbc_decrypt,
	}, {
		.base = {
			.cra_name		= "cfb(sm4)",
			.cra_driver_name	= "cfb-sm4-neon",
			.cra_priority		= 200,
			.cra_blocksize		= 1,
			.cra_ctxsize		= sizeof(struct sm4_ctx),
			.cra_module		= THIS_MODULE,
		},
		.min_keysize	= SM4_KEY_SIZE,
		.max_keysize	= SM4_KEY_SIZE,
		.ivsize		= SM4_BLOCK_SIZE,
		.chunksize	= SM4_BLOCK_SIZE,
		.setkey		= sm4_setkey,
		.encrypt	= sm4_cfb_encrypt,
		.decrypt	= sm4_cfb_decrypt,
	}, {
		.base = {
			.cra_name		= "ctr(sm4)",
			.cra_driver_name	= "ctr-sm4-neon",
			.cra_priority		= 200,
			.cra_blocksize		= 1,
			.cra_ctxsize		= sizeof(struct sm4_ctx),
			.cra_module		= THIS_MODULE,
		},
		.min_keysize	= SM4_KEY_SIZE,
		.max_keysize	= SM4_KEY_SIZE,
		.ivsize		= SM4_BLOCK_SIZE,
		.chunksize	= SM4_BLOCK_SIZE,
		.setkey		= sm4_setkey,
		.encrypt	= sm4_ctr_crypt,
		.decrypt	= sm4_ctr_crypt,
	}
};

static int __init sm4_init(void)
{
	return crypto_register_skciphers(sm4_algs, ARRAY_SIZE(sm4_algs));
}

static void __exit sm4_exit(void)
{
	crypto_unregister_skciphers(sm4_algs, ARRAY_SIZE(sm4_algs));
}

module_init(sm4_init);
module_exit(sm4_exit);

MODULE_DESCRIPTION("SM4 ECB/CBC/CFB/CTR using ARMv8 NEON");
MODULE_ALIAS_CRYPTO("sm4-neon");
MODULE_ALIAS_CRYPTO("sm4");
MODULE_ALIAS_CRYPTO("ecb(sm4)");
MODULE_ALIAS_CRYPTO("cbc(sm4)");
MODULE_ALIAS_CRYPTO("cfb(sm4)");
MODULE_ALIAS_CRYPTO("ctr(sm4)");
MODULE_AUTHOR("Tianjia Zhang <tianjia.zhang@linux.alibaba.com>");
MODULE_LICENSE("GPL v2");
