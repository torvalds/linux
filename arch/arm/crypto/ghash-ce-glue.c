// SPDX-License-Identifier: GPL-2.0-only
/*
 * AES-GCM using ARMv8 Crypto Extensions
 *
 * Copyright (C) 2015 - 2018 Linaro Ltd.
 * Copyright (C) 2023 Google LLC.
 */

#include <asm/hwcap.h>
#include <asm/neon.h>
#include <crypto/aes.h>
#include <crypto/b128ops.h>
#include <crypto/gcm.h>
#include <crypto/gf128mul.h>
#include <crypto/ghash.h>
#include <crypto/internal/aead.h>
#include <crypto/internal/skcipher.h>
#include <crypto/scatterwalk.h>
#include <linux/cpufeature.h>
#include <linux/errno.h>
#include <linux/jump_label.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/unaligned.h>

MODULE_DESCRIPTION("AES-GCM using ARMv8 Crypto Extensions");
MODULE_AUTHOR("Ard Biesheuvel <ardb@kernel.org>");
MODULE_LICENSE("GPL");
MODULE_ALIAS_CRYPTO("gcm(aes)");
MODULE_ALIAS_CRYPTO("rfc4106(gcm(aes))");

#define RFC4106_NONCE_SIZE	4

struct gcm_key {
	u64	h[4][2];
	u32	rk[AES_MAX_KEYLENGTH_U32];
	int	rounds;
	u8	nonce[];	// for RFC4106 nonce
};

asmlinkage void pmull_ghash_update_p64(int blocks, u64 dg[], const char *src,
				       u64 const h[4][2], const char *head);

static void ghash_reflect(u64 h[], const be128 *k)
{
	u64 carry = be64_to_cpu(k->a) >> 63;

	h[0] = (be64_to_cpu(k->b) << 1) | carry;
	h[1] = (be64_to_cpu(k->a) << 1) | (be64_to_cpu(k->b) >> 63);

	if (carry)
		h[1] ^= 0xc200000000000000UL;
}

void pmull_gcm_encrypt(int blocks, u64 dg[], const char *src,
		       struct gcm_key const *k, char *dst,
		       const char *iv, int rounds, u32 counter);

void pmull_gcm_enc_final(int blocks, u64 dg[], char *tag,
			 struct gcm_key const *k, char *head,
			 const char *iv, int rounds, u32 counter);

void pmull_gcm_decrypt(int bytes, u64 dg[], const char *src,
		       struct gcm_key const *k, char *dst,
		       const char *iv, int rounds, u32 counter);

int pmull_gcm_dec_final(int bytes, u64 dg[], char *tag,
			struct gcm_key const *k, char *head,
			const char *iv, int rounds, u32 counter,
			const char *otag, int authsize);

static int gcm_aes_setkey(struct crypto_aead *tfm, const u8 *inkey,
			  unsigned int keylen)
{
	struct gcm_key *ctx = crypto_aead_ctx(tfm);
	struct aes_enckey aes_key;
	be128 h, k;
	int ret;

	ret = aes_prepareenckey(&aes_key, inkey, keylen);
	if (ret)
		return -EINVAL;

	aes_encrypt(&aes_key, (u8 *)&k, (u8[AES_BLOCK_SIZE]){});

	/*
	 * Note: this assumes that the arm implementation of the AES library
	 * stores the standard round keys in k.rndkeys.
	 */
	memcpy(ctx->rk, aes_key.k.rndkeys, sizeof(ctx->rk));
	ctx->rounds = 6 + keylen / 4;

	memzero_explicit(&aes_key, sizeof(aes_key));

	ghash_reflect(ctx->h[0], &k);

	h = k;
	gf128mul_lle(&h, &k);
	ghash_reflect(ctx->h[1], &h);

	gf128mul_lle(&h, &k);
	ghash_reflect(ctx->h[2], &h);

	gf128mul_lle(&h, &k);
	ghash_reflect(ctx->h[3], &h);

	return 0;
}

static int gcm_aes_setauthsize(struct crypto_aead *tfm, unsigned int authsize)
{
	return crypto_gcm_check_authsize(authsize);
}

static void gcm_update_mac(u64 dg[], const u8 *src, int count, u8 buf[],
			   int *buf_count, struct gcm_key *ctx)
{
	if (*buf_count > 0) {
		int buf_added = min(count, GHASH_BLOCK_SIZE - *buf_count);

		memcpy(&buf[*buf_count], src, buf_added);

		*buf_count += buf_added;
		src += buf_added;
		count -= buf_added;
	}

	if (count >= GHASH_BLOCK_SIZE || *buf_count == GHASH_BLOCK_SIZE) {
		int blocks = count / GHASH_BLOCK_SIZE;

		pmull_ghash_update_p64(blocks, dg, src, ctx->h,
				       *buf_count ? buf : NULL);

		src += blocks * GHASH_BLOCK_SIZE;
		count %= GHASH_BLOCK_SIZE;
		*buf_count = 0;
	}

	if (count > 0) {
		memcpy(buf, src, count);
		*buf_count = count;
	}
}

static void gcm_calculate_auth_mac(struct aead_request *req, u64 dg[], u32 len)
{
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	struct gcm_key *ctx = crypto_aead_ctx(aead);
	u8 buf[GHASH_BLOCK_SIZE];
	struct scatter_walk walk;
	int buf_count = 0;

	scatterwalk_start(&walk, req->src);

	do {
		unsigned int n;

		n = scatterwalk_next(&walk, len);
		gcm_update_mac(dg, walk.addr, n, buf, &buf_count, ctx);
		scatterwalk_done_src(&walk,  n);

		if (unlikely(len / SZ_4K > (len - n) / SZ_4K)) {
			kernel_neon_end();
			kernel_neon_begin();
		}

		len -= n;
	} while (len);

	if (buf_count) {
		memset(&buf[buf_count], 0, GHASH_BLOCK_SIZE - buf_count);
		pmull_ghash_update_p64(1, dg, buf, ctx->h, NULL);
	}
}

static int gcm_encrypt(struct aead_request *req, const u8 *iv, u32 assoclen)
{
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	struct gcm_key *ctx = crypto_aead_ctx(aead);
	struct skcipher_walk walk;
	u8 buf[AES_BLOCK_SIZE];
	u32 counter = 2;
	u64 dg[2] = {};
	be128 lengths;
	const u8 *src;
	u8 *tag, *dst;
	int tail, err;

	err = skcipher_walk_aead_encrypt(&walk, req, false);

	kernel_neon_begin();

	if (assoclen)
		gcm_calculate_auth_mac(req, dg, assoclen);

	src = walk.src.virt.addr;
	dst = walk.dst.virt.addr;

	while (walk.nbytes >= AES_BLOCK_SIZE) {
		int nblocks = walk.nbytes / AES_BLOCK_SIZE;

		pmull_gcm_encrypt(nblocks, dg, src, ctx, dst, iv,
				  ctx->rounds, counter);
		counter += nblocks;

		if (walk.nbytes == walk.total) {
			src += nblocks * AES_BLOCK_SIZE;
			dst += nblocks * AES_BLOCK_SIZE;
			break;
		}

		kernel_neon_end();

		err = skcipher_walk_done(&walk,
					 walk.nbytes % AES_BLOCK_SIZE);
		if (err)
			return err;

		src = walk.src.virt.addr;
		dst = walk.dst.virt.addr;

		kernel_neon_begin();
	}


	lengths.a = cpu_to_be64(assoclen * 8);
	lengths.b = cpu_to_be64(req->cryptlen * 8);

	tag = (u8 *)&lengths;
	tail = walk.nbytes % AES_BLOCK_SIZE;

	/*
	 * Bounce via a buffer unless we are encrypting in place and src/dst
	 * are not pointing to the start of the walk buffer. In that case, we
	 * can do a NEON load/xor/store sequence in place as long as we move
	 * the plain/ciphertext and keystream to the start of the register. If
	 * not, do a memcpy() to the end of the buffer so we can reuse the same
	 * logic.
	 */
	if (unlikely(tail && (tail == walk.nbytes || src != dst)))
		src = memcpy(buf + sizeof(buf) - tail, src, tail);

	pmull_gcm_enc_final(tail, dg, tag, ctx, (u8 *)src, iv,
			    ctx->rounds, counter);
	kernel_neon_end();

	if (unlikely(tail && src != dst))
		memcpy(dst, src, tail);

	if (walk.nbytes) {
		err = skcipher_walk_done(&walk, 0);
		if (err)
			return err;
	}

	/* copy authtag to end of dst */
	scatterwalk_map_and_copy(tag, req->dst, req->assoclen + req->cryptlen,
				 crypto_aead_authsize(aead), 1);

	return 0;
}

static int gcm_decrypt(struct aead_request *req, const u8 *iv, u32 assoclen)
{
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	struct gcm_key *ctx = crypto_aead_ctx(aead);
	int authsize = crypto_aead_authsize(aead);
	struct skcipher_walk walk;
	u8 otag[AES_BLOCK_SIZE];
	u8 buf[AES_BLOCK_SIZE];
	u32 counter = 2;
	u64 dg[2] = {};
	be128 lengths;
	const u8 *src;
	u8 *tag, *dst;
	int tail, err, ret;

	scatterwalk_map_and_copy(otag, req->src,
				 req->assoclen + req->cryptlen - authsize,
				 authsize, 0);

	err = skcipher_walk_aead_decrypt(&walk, req, false);

	kernel_neon_begin();

	if (assoclen)
		gcm_calculate_auth_mac(req, dg, assoclen);

	src = walk.src.virt.addr;
	dst = walk.dst.virt.addr;

	while (walk.nbytes >= AES_BLOCK_SIZE) {
		int nblocks = walk.nbytes / AES_BLOCK_SIZE;

		pmull_gcm_decrypt(nblocks, dg, src, ctx, dst, iv,
				  ctx->rounds, counter);
		counter += nblocks;

		if (walk.nbytes == walk.total) {
			src += nblocks * AES_BLOCK_SIZE;
			dst += nblocks * AES_BLOCK_SIZE;
			break;
		}

		kernel_neon_end();

		err = skcipher_walk_done(&walk,
					 walk.nbytes % AES_BLOCK_SIZE);
		if (err)
			return err;

		src = walk.src.virt.addr;
		dst = walk.dst.virt.addr;

		kernel_neon_begin();
	}

	lengths.a = cpu_to_be64(assoclen * 8);
	lengths.b = cpu_to_be64((req->cryptlen - authsize) * 8);

	tag = (u8 *)&lengths;
	tail = walk.nbytes % AES_BLOCK_SIZE;

	if (unlikely(tail && (tail == walk.nbytes || src != dst)))
		src = memcpy(buf + sizeof(buf) - tail, src, tail);

	ret = pmull_gcm_dec_final(tail, dg, tag, ctx, (u8 *)src, iv,
				  ctx->rounds, counter, otag, authsize);
	kernel_neon_end();

	if (unlikely(tail && src != dst))
		memcpy(dst, src, tail);

	if (walk.nbytes) {
		err = skcipher_walk_done(&walk, 0);
		if (err)
			return err;
	}

	return ret ? -EBADMSG : 0;
}

static int gcm_aes_encrypt(struct aead_request *req)
{
	return gcm_encrypt(req, req->iv, req->assoclen);
}

static int gcm_aes_decrypt(struct aead_request *req)
{
	return gcm_decrypt(req, req->iv, req->assoclen);
}

static int rfc4106_setkey(struct crypto_aead *tfm, const u8 *inkey,
			  unsigned int keylen)
{
	struct gcm_key *ctx = crypto_aead_ctx(tfm);
	int err;

	keylen -= RFC4106_NONCE_SIZE;
	err = gcm_aes_setkey(tfm, inkey, keylen);
	if (err)
		return err;

	memcpy(ctx->nonce, inkey + keylen, RFC4106_NONCE_SIZE);
	return 0;
}

static int rfc4106_setauthsize(struct crypto_aead *tfm, unsigned int authsize)
{
	return crypto_rfc4106_check_authsize(authsize);
}

static int rfc4106_encrypt(struct aead_request *req)
{
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	struct gcm_key *ctx = crypto_aead_ctx(aead);
	u8 iv[GCM_AES_IV_SIZE];

	memcpy(iv, ctx->nonce, RFC4106_NONCE_SIZE);
	memcpy(iv + RFC4106_NONCE_SIZE, req->iv, GCM_RFC4106_IV_SIZE);

	return crypto_ipsec_check_assoclen(req->assoclen) ?:
	       gcm_encrypt(req, iv, req->assoclen - GCM_RFC4106_IV_SIZE);
}

static int rfc4106_decrypt(struct aead_request *req)
{
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	struct gcm_key *ctx = crypto_aead_ctx(aead);
	u8 iv[GCM_AES_IV_SIZE];

	memcpy(iv, ctx->nonce, RFC4106_NONCE_SIZE);
	memcpy(iv + RFC4106_NONCE_SIZE, req->iv, GCM_RFC4106_IV_SIZE);

	return crypto_ipsec_check_assoclen(req->assoclen) ?:
	       gcm_decrypt(req, iv, req->assoclen - GCM_RFC4106_IV_SIZE);
}

static struct aead_alg gcm_aes_algs[] = {{
	.ivsize			= GCM_AES_IV_SIZE,
	.chunksize		= AES_BLOCK_SIZE,
	.maxauthsize		= AES_BLOCK_SIZE,
	.setkey			= gcm_aes_setkey,
	.setauthsize		= gcm_aes_setauthsize,
	.encrypt		= gcm_aes_encrypt,
	.decrypt		= gcm_aes_decrypt,

	.base.cra_name		= "gcm(aes)",
	.base.cra_driver_name	= "gcm-aes-ce",
	.base.cra_priority	= 400,
	.base.cra_blocksize	= 1,
	.base.cra_ctxsize	= sizeof(struct gcm_key),
	.base.cra_module	= THIS_MODULE,
}, {
	.ivsize			= GCM_RFC4106_IV_SIZE,
	.chunksize		= AES_BLOCK_SIZE,
	.maxauthsize		= AES_BLOCK_SIZE,
	.setkey			= rfc4106_setkey,
	.setauthsize		= rfc4106_setauthsize,
	.encrypt		= rfc4106_encrypt,
	.decrypt		= rfc4106_decrypt,

	.base.cra_name		= "rfc4106(gcm(aes))",
	.base.cra_driver_name	= "rfc4106-gcm-aes-ce",
	.base.cra_priority	= 400,
	.base.cra_blocksize	= 1,
	.base.cra_ctxsize	= sizeof(struct gcm_key) + RFC4106_NONCE_SIZE,
	.base.cra_module	= THIS_MODULE,
}};

static int __init ghash_ce_mod_init(void)
{
	if (!(elf_hwcap & HWCAP_NEON) || !(elf_hwcap2 & HWCAP2_PMULL))
		return -ENODEV;

	return crypto_register_aeads(gcm_aes_algs, ARRAY_SIZE(gcm_aes_algs));
}

static void __exit ghash_ce_mod_exit(void)
{
	crypto_unregister_aeads(gcm_aes_algs, ARRAY_SIZE(gcm_aes_algs));
}

module_init(ghash_ce_mod_init);
module_exit(ghash_ce_mod_exit);
