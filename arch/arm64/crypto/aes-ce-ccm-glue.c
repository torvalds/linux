// SPDX-License-Identifier: GPL-2.0-only
/*
 * aes-ce-ccm-glue.c - AES-CCM transform for ARMv8 with Crypto Extensions
 *
 * Copyright (C) 2013 - 2017 Linaro Ltd.
 * Copyright (C) 2024 Google LLC
 *
 * Author: Ard Biesheuvel <ardb@kernel.org>
 */

#include <asm/neon.h>
#include <linux/unaligned.h>
#include <crypto/aes.h>
#include <crypto/scatterwalk.h>
#include <crypto/internal/aead.h>
#include <crypto/internal/skcipher.h>
#include <linux/module.h>

#include "aes-ce-setkey.h"

MODULE_IMPORT_NS(CRYPTO_INTERNAL);

static int num_rounds(struct crypto_aes_ctx *ctx)
{
	/*
	 * # of rounds specified by AES:
	 * 128 bit key		10 rounds
	 * 192 bit key		12 rounds
	 * 256 bit key		14 rounds
	 * => n byte key	=> 6 + (n/4) rounds
	 */
	return 6 + ctx->key_length / 4;
}

asmlinkage u32 ce_aes_mac_update(u8 const in[], u32 const rk[], int rounds,
				 int blocks, u8 dg[], int enc_before,
				 int enc_after);

asmlinkage void ce_aes_ccm_encrypt(u8 out[], u8 const in[], u32 cbytes,
				   u32 const rk[], u32 rounds, u8 mac[],
				   u8 ctr[], u8 const final_iv[]);

asmlinkage void ce_aes_ccm_decrypt(u8 out[], u8 const in[], u32 cbytes,
				   u32 const rk[], u32 rounds, u8 mac[],
				   u8 ctr[], u8 const final_iv[]);

static int ccm_setkey(struct crypto_aead *tfm, const u8 *in_key,
		      unsigned int key_len)
{
	struct crypto_aes_ctx *ctx = crypto_aead_ctx(tfm);

	return ce_aes_expandkey(ctx, in_key, key_len);
}

static int ccm_setauthsize(struct crypto_aead *tfm, unsigned int authsize)
{
	if ((authsize & 1) || authsize < 4)
		return -EINVAL;
	return 0;
}

static int ccm_init_mac(struct aead_request *req, u8 maciv[], u32 msglen)
{
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	__be32 *n = (__be32 *)&maciv[AES_BLOCK_SIZE - 8];
	u32 l = req->iv[0] + 1;

	/* verify that CCM dimension 'L' is set correctly in the IV */
	if (l < 2 || l > 8)
		return -EINVAL;

	/* verify that msglen can in fact be represented in L bytes */
	if (l < 4 && msglen >> (8 * l))
		return -EOVERFLOW;

	/*
	 * Even if the CCM spec allows L values of up to 8, the Linux cryptoapi
	 * uses a u32 type to represent msglen so the top 4 bytes are always 0.
	 */
	n[0] = 0;
	n[1] = cpu_to_be32(msglen);

	memcpy(maciv, req->iv, AES_BLOCK_SIZE - l);

	/*
	 * Meaning of byte 0 according to CCM spec (RFC 3610/NIST 800-38C)
	 * - bits 0..2	: max # of bytes required to represent msglen, minus 1
	 *                (already set by caller)
	 * - bits 3..5	: size of auth tag (1 => 4 bytes, 2 => 6 bytes, etc)
	 * - bit 6	: indicates presence of authenticate-only data
	 */
	maciv[0] |= (crypto_aead_authsize(aead) - 2) << 2;
	if (req->assoclen)
		maciv[0] |= 0x40;

	memset(&req->iv[AES_BLOCK_SIZE - l], 0, l);
	return 0;
}

static u32 ce_aes_ccm_auth_data(u8 mac[], u8 const in[], u32 abytes,
				u32 macp, u32 const rk[], u32 rounds)
{
	int enc_after = (macp + abytes) % AES_BLOCK_SIZE;

	do {
		u32 blocks = abytes / AES_BLOCK_SIZE;

		if (macp == AES_BLOCK_SIZE || (!macp && blocks > 0)) {
			u32 rem = ce_aes_mac_update(in, rk, rounds, blocks, mac,
						    macp, enc_after);
			u32 adv = (blocks - rem) * AES_BLOCK_SIZE;

			macp = enc_after ? 0 : AES_BLOCK_SIZE;
			in += adv;
			abytes -= adv;

			if (unlikely(rem)) {
				kernel_neon_end();
				kernel_neon_begin();
				macp = 0;
			}
		} else {
			u32 l = min(AES_BLOCK_SIZE - macp, abytes);

			crypto_xor(&mac[macp], in, l);
			in += l;
			macp += l;
			abytes -= l;
		}
	} while (abytes > 0);

	return macp;
}

static void ccm_calculate_auth_mac(struct aead_request *req, u8 mac[])
{
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	struct crypto_aes_ctx *ctx = crypto_aead_ctx(aead);
	struct __packed { __be16 l; __be32 h; u16 len; } ltag;
	struct scatter_walk walk;
	u32 len = req->assoclen;
	u32 macp = AES_BLOCK_SIZE;

	/* prepend the AAD with a length tag */
	if (len < 0xff00) {
		ltag.l = cpu_to_be16(len);
		ltag.len = 2;
	} else  {
		ltag.l = cpu_to_be16(0xfffe);
		put_unaligned_be32(len, &ltag.h);
		ltag.len = 6;
	}

	macp = ce_aes_ccm_auth_data(mac, (u8 *)&ltag, ltag.len, macp,
				    ctx->key_enc, num_rounds(ctx));
	scatterwalk_start(&walk, req->src);

	do {
		u32 n = scatterwalk_clamp(&walk, len);
		u8 *p;

		if (!n) {
			scatterwalk_start(&walk, sg_next(walk.sg));
			n = scatterwalk_clamp(&walk, len);
		}
		p = scatterwalk_map(&walk);

		macp = ce_aes_ccm_auth_data(mac, p, n, macp, ctx->key_enc,
					    num_rounds(ctx));

		len -= n;

		scatterwalk_unmap(p);
		scatterwalk_advance(&walk, n);
		scatterwalk_done(&walk, 0, len);
	} while (len);
}

static int ccm_encrypt(struct aead_request *req)
{
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	struct crypto_aes_ctx *ctx = crypto_aead_ctx(aead);
	struct skcipher_walk walk;
	u8 __aligned(8) mac[AES_BLOCK_SIZE];
	u8 orig_iv[AES_BLOCK_SIZE];
	u32 len = req->cryptlen;
	int err;

	err = ccm_init_mac(req, mac, len);
	if (err)
		return err;

	/* preserve the original iv for the final round */
	memcpy(orig_iv, req->iv, AES_BLOCK_SIZE);

	err = skcipher_walk_aead_encrypt(&walk, req, false);
	if (unlikely(err))
		return err;

	kernel_neon_begin();

	if (req->assoclen)
		ccm_calculate_auth_mac(req, mac);

	do {
		u32 tail = walk.nbytes % AES_BLOCK_SIZE;
		const u8 *src = walk.src.virt.addr;
		u8 *dst = walk.dst.virt.addr;
		u8 buf[AES_BLOCK_SIZE];
		u8 *final_iv = NULL;

		if (walk.nbytes == walk.total) {
			tail = 0;
			final_iv = orig_iv;
		}

		if (unlikely(walk.nbytes < AES_BLOCK_SIZE))
			src = dst = memcpy(&buf[sizeof(buf) - walk.nbytes],
					   src, walk.nbytes);

		ce_aes_ccm_encrypt(dst, src, walk.nbytes - tail,
				   ctx->key_enc, num_rounds(ctx),
				   mac, walk.iv, final_iv);

		if (unlikely(walk.nbytes < AES_BLOCK_SIZE))
			memcpy(walk.dst.virt.addr, dst, walk.nbytes);

		if (walk.nbytes) {
			err = skcipher_walk_done(&walk, tail);
		}
	} while (walk.nbytes);

	kernel_neon_end();

	if (unlikely(err))
		return err;

	/* copy authtag to end of dst */
	scatterwalk_map_and_copy(mac, req->dst, req->assoclen + req->cryptlen,
				 crypto_aead_authsize(aead), 1);

	return 0;
}

static int ccm_decrypt(struct aead_request *req)
{
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	struct crypto_aes_ctx *ctx = crypto_aead_ctx(aead);
	unsigned int authsize = crypto_aead_authsize(aead);
	struct skcipher_walk walk;
	u8 __aligned(8) mac[AES_BLOCK_SIZE];
	u8 orig_iv[AES_BLOCK_SIZE];
	u32 len = req->cryptlen - authsize;
	int err;

	err = ccm_init_mac(req, mac, len);
	if (err)
		return err;

	/* preserve the original iv for the final round */
	memcpy(orig_iv, req->iv, AES_BLOCK_SIZE);

	err = skcipher_walk_aead_decrypt(&walk, req, false);
	if (unlikely(err))
		return err;

	kernel_neon_begin();

	if (req->assoclen)
		ccm_calculate_auth_mac(req, mac);

	do {
		u32 tail = walk.nbytes % AES_BLOCK_SIZE;
		const u8 *src = walk.src.virt.addr;
		u8 *dst = walk.dst.virt.addr;
		u8 buf[AES_BLOCK_SIZE];
		u8 *final_iv = NULL;

		if (walk.nbytes == walk.total) {
			tail = 0;
			final_iv = orig_iv;
		}

		if (unlikely(walk.nbytes < AES_BLOCK_SIZE))
			src = dst = memcpy(&buf[sizeof(buf) - walk.nbytes],
					   src, walk.nbytes);

		ce_aes_ccm_decrypt(dst, src, walk.nbytes - tail,
				   ctx->key_enc, num_rounds(ctx),
				   mac, walk.iv, final_iv);

		if (unlikely(walk.nbytes < AES_BLOCK_SIZE))
			memcpy(walk.dst.virt.addr, dst, walk.nbytes);

		if (walk.nbytes) {
			err = skcipher_walk_done(&walk, tail);
		}
	} while (walk.nbytes);

	kernel_neon_end();

	if (unlikely(err))
		return err;

	/* compare calculated auth tag with the stored one */
	scatterwalk_map_and_copy(orig_iv, req->src,
				 req->assoclen + req->cryptlen - authsize,
				 authsize, 0);

	if (crypto_memneq(mac, orig_iv, authsize))
		return -EBADMSG;
	return 0;
}

static struct aead_alg ccm_aes_alg = {
	.base = {
		.cra_name		= "ccm(aes)",
		.cra_driver_name	= "ccm-aes-ce",
		.cra_priority		= 300,
		.cra_blocksize		= 1,
		.cra_ctxsize		= sizeof(struct crypto_aes_ctx),
		.cra_module		= THIS_MODULE,
	},
	.ivsize		= AES_BLOCK_SIZE,
	.chunksize	= AES_BLOCK_SIZE,
	.maxauthsize	= AES_BLOCK_SIZE,
	.setkey		= ccm_setkey,
	.setauthsize	= ccm_setauthsize,
	.encrypt	= ccm_encrypt,
	.decrypt	= ccm_decrypt,
};

static int __init aes_mod_init(void)
{
	if (!cpu_have_named_feature(AES))
		return -ENODEV;
	return crypto_register_aead(&ccm_aes_alg);
}

static void __exit aes_mod_exit(void)
{
	crypto_unregister_aead(&ccm_aes_alg);
}

module_init(aes_mod_init);
module_exit(aes_mod_exit);

MODULE_DESCRIPTION("Synchronous AES in CCM mode using ARMv8 Crypto Extensions");
MODULE_AUTHOR("Ard Biesheuvel <ardb@kernel.org>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS_CRYPTO("ccm(aes)");
