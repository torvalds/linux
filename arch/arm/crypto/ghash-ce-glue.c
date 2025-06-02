// SPDX-License-Identifier: GPL-2.0-only
/*
 * Accelerated GHASH implementation with ARMv8 vmull.p64 instructions.
 *
 * Copyright (C) 2015 - 2018 Linaro Ltd.
 * Copyright (C) 2023 Google LLC.
 */

#include <asm/hwcap.h>
#include <asm/neon.h>
#include <asm/simd.h>
#include <linux/unaligned.h>
#include <crypto/aes.h>
#include <crypto/gcm.h>
#include <crypto/b128ops.h>
#include <crypto/cryptd.h>
#include <crypto/internal/aead.h>
#include <crypto/internal/hash.h>
#include <crypto/internal/simd.h>
#include <crypto/internal/skcipher.h>
#include <crypto/gf128mul.h>
#include <crypto/scatterwalk.h>
#include <linux/cpufeature.h>
#include <linux/crypto.h>
#include <linux/jump_label.h>
#include <linux/module.h>

MODULE_DESCRIPTION("GHASH hash function using ARMv8 Crypto Extensions");
MODULE_AUTHOR("Ard Biesheuvel <ardb@kernel.org>");
MODULE_LICENSE("GPL");
MODULE_ALIAS_CRYPTO("ghash");
MODULE_ALIAS_CRYPTO("gcm(aes)");
MODULE_ALIAS_CRYPTO("rfc4106(gcm(aes))");

#define GHASH_BLOCK_SIZE	16
#define GHASH_DIGEST_SIZE	16

#define RFC4106_NONCE_SIZE	4

struct ghash_key {
	be128	k;
	u64	h[][2];
};

struct gcm_key {
	u64	h[4][2];
	u32	rk[AES_MAX_KEYLENGTH_U32];
	int	rounds;
	u8	nonce[];	// for RFC4106 nonce
};

struct ghash_desc_ctx {
	u64 digest[GHASH_DIGEST_SIZE/sizeof(u64)];
	u8 buf[GHASH_BLOCK_SIZE];
	u32 count;
};

asmlinkage void pmull_ghash_update_p64(int blocks, u64 dg[], const char *src,
				       u64 const h[][2], const char *head);

asmlinkage void pmull_ghash_update_p8(int blocks, u64 dg[], const char *src,
				      u64 const h[][2], const char *head);

static __ro_after_init DEFINE_STATIC_KEY_FALSE(use_p64);

static int ghash_init(struct shash_desc *desc)
{
	struct ghash_desc_ctx *ctx = shash_desc_ctx(desc);

	*ctx = (struct ghash_desc_ctx){};
	return 0;
}

static void ghash_do_update(int blocks, u64 dg[], const char *src,
			    struct ghash_key *key, const char *head)
{
	kernel_neon_begin();
	if (static_branch_likely(&use_p64))
		pmull_ghash_update_p64(blocks, dg, src, key->h, head);
	else
		pmull_ghash_update_p8(blocks, dg, src, key->h, head);
	kernel_neon_end();
}

static int ghash_update(struct shash_desc *desc, const u8 *src,
			unsigned int len)
{
	struct ghash_desc_ctx *ctx = shash_desc_ctx(desc);
	unsigned int partial = ctx->count % GHASH_BLOCK_SIZE;

	ctx->count += len;

	if ((partial + len) >= GHASH_BLOCK_SIZE) {
		struct ghash_key *key = crypto_shash_ctx(desc->tfm);
		int blocks;

		if (partial) {
			int p = GHASH_BLOCK_SIZE - partial;

			memcpy(ctx->buf + partial, src, p);
			src += p;
			len -= p;
		}

		blocks = len / GHASH_BLOCK_SIZE;
		len %= GHASH_BLOCK_SIZE;

		ghash_do_update(blocks, ctx->digest, src, key,
				partial ? ctx->buf : NULL);
		src += blocks * GHASH_BLOCK_SIZE;
		partial = 0;
	}
	if (len)
		memcpy(ctx->buf + partial, src, len);
	return 0;
}

static int ghash_final(struct shash_desc *desc, u8 *dst)
{
	struct ghash_desc_ctx *ctx = shash_desc_ctx(desc);
	unsigned int partial = ctx->count % GHASH_BLOCK_SIZE;

	if (partial) {
		struct ghash_key *key = crypto_shash_ctx(desc->tfm);

		memset(ctx->buf + partial, 0, GHASH_BLOCK_SIZE - partial);
		ghash_do_update(1, ctx->digest, ctx->buf, key, NULL);
	}
	put_unaligned_be64(ctx->digest[1], dst);
	put_unaligned_be64(ctx->digest[0], dst + 8);

	*ctx = (struct ghash_desc_ctx){};
	return 0;
}

static void ghash_reflect(u64 h[], const be128 *k)
{
	u64 carry = be64_to_cpu(k->a) >> 63;

	h[0] = (be64_to_cpu(k->b) << 1) | carry;
	h[1] = (be64_to_cpu(k->a) << 1) | (be64_to_cpu(k->b) >> 63);

	if (carry)
		h[1] ^= 0xc200000000000000UL;
}

static int ghash_setkey(struct crypto_shash *tfm,
			const u8 *inkey, unsigned int keylen)
{
	struct ghash_key *key = crypto_shash_ctx(tfm);

	if (keylen != GHASH_BLOCK_SIZE)
		return -EINVAL;

	/* needed for the fallback */
	memcpy(&key->k, inkey, GHASH_BLOCK_SIZE);
	ghash_reflect(key->h[0], &key->k);

	if (static_branch_likely(&use_p64)) {
		be128 h = key->k;

		gf128mul_lle(&h, &key->k);
		ghash_reflect(key->h[1], &h);

		gf128mul_lle(&h, &key->k);
		ghash_reflect(key->h[2], &h);

		gf128mul_lle(&h, &key->k);
		ghash_reflect(key->h[3], &h);
	}
	return 0;
}

static struct shash_alg ghash_alg = {
	.digestsize		= GHASH_DIGEST_SIZE,
	.init			= ghash_init,
	.update			= ghash_update,
	.final			= ghash_final,
	.setkey			= ghash_setkey,
	.descsize		= sizeof(struct ghash_desc_ctx),

	.base.cra_name		= "ghash",
	.base.cra_driver_name	= "ghash-ce",
	.base.cra_priority	= 300,
	.base.cra_blocksize	= GHASH_BLOCK_SIZE,
	.base.cra_ctxsize	= sizeof(struct ghash_key) + sizeof(u64[2]),
	.base.cra_module	= THIS_MODULE,
};

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
	struct crypto_aes_ctx aes_ctx;
	be128 h, k;
	int ret;

	ret = aes_expandkey(&aes_ctx, inkey, keylen);
	if (ret)
		return -EINVAL;

	aes_encrypt(&aes_ctx, (u8 *)&k, (u8[AES_BLOCK_SIZE]){});

	memcpy(ctx->rk, aes_ctx.key_enc, sizeof(ctx->rk));
	ctx->rounds = 6 + keylen / 4;

	memzero_explicit(&aes_ctx, sizeof(aes_ctx));

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

	if (WARN_ON_ONCE(!may_use_simd()))
		return -EBUSY;

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

	if (WARN_ON_ONCE(!may_use_simd()))
		return -EBUSY;

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
	int err;

	if (!(elf_hwcap & HWCAP_NEON))
		return -ENODEV;

	if (elf_hwcap2 & HWCAP2_PMULL) {
		err = crypto_register_aeads(gcm_aes_algs,
					    ARRAY_SIZE(gcm_aes_algs));
		if (err)
			return err;
		ghash_alg.base.cra_ctxsize += 3 * sizeof(u64[2]);
		static_branch_enable(&use_p64);
	}

	err = crypto_register_shash(&ghash_alg);
	if (err)
		goto err_aead;

	return 0;

err_aead:
	if (elf_hwcap2 & HWCAP2_PMULL)
		crypto_unregister_aeads(gcm_aes_algs,
					ARRAY_SIZE(gcm_aes_algs));
	return err;
}

static void __exit ghash_ce_mod_exit(void)
{
	crypto_unregister_shash(&ghash_alg);
	if (elf_hwcap2 & HWCAP2_PMULL)
		crypto_unregister_aeads(gcm_aes_algs,
					ARRAY_SIZE(gcm_aes_algs));
}

module_init(ghash_ce_mod_init);
module_exit(ghash_ce_mod_exit);
