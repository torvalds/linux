// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Support for AES-NI and VAES instructions.  This file contains glue code.
 * The real AES implementations are in aesni-intel_asm.S and other .S files.
 *
 * Copyright (C) 2008, Intel Corp.
 *    Author: Huang Ying <ying.huang@intel.com>
 *
 * Added RFC4106 AES-GCM support for 128-bit keys under the AEAD
 * interface for 64-bit kernels.
 *    Authors: Adrian Hoban <adrian.hoban@intel.com>
 *             Gabriele Paoloni <gabriele.paoloni@intel.com>
 *             Tadeusz Struk (tadeusz.struk@intel.com)
 *             Aidan O'Mahony (aidan.o.mahony@intel.com)
 *    Copyright (c) 2010, Intel Corporation.
 *
 * Copyright 2024 Google LLC
 */

#include <linux/hardirq.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/err.h>
#include <crypto/algapi.h>
#include <crypto/aes.h>
#include <crypto/ctr.h>
#include <crypto/b128ops.h>
#include <crypto/gcm.h>
#include <crypto/xts.h>
#include <asm/cpu_device_id.h>
#include <asm/simd.h>
#include <crypto/scatterwalk.h>
#include <crypto/internal/aead.h>
#include <crypto/internal/simd.h>
#include <crypto/internal/skcipher.h>
#include <linux/jump_label.h>
#include <linux/workqueue.h>
#include <linux/spinlock.h>
#include <linux/static_call.h>


#define AESNI_ALIGN	16
#define AESNI_ALIGN_ATTR __attribute__ ((__aligned__(AESNI_ALIGN)))
#define AES_BLOCK_MASK	(~(AES_BLOCK_SIZE - 1))
#define AESNI_ALIGN_EXTRA ((AESNI_ALIGN - 1) & ~(CRYPTO_MINALIGN - 1))
#define CRYPTO_AES_CTX_SIZE (sizeof(struct crypto_aes_ctx) + AESNI_ALIGN_EXTRA)
#define XTS_AES_CTX_SIZE (sizeof(struct aesni_xts_ctx) + AESNI_ALIGN_EXTRA)

struct aesni_xts_ctx {
	struct crypto_aes_ctx tweak_ctx AESNI_ALIGN_ATTR;
	struct crypto_aes_ctx crypt_ctx AESNI_ALIGN_ATTR;
};

static inline void *aes_align_addr(void *addr)
{
	if (crypto_tfm_ctx_alignment() >= AESNI_ALIGN)
		return addr;
	return PTR_ALIGN(addr, AESNI_ALIGN);
}

asmlinkage void aesni_set_key(struct crypto_aes_ctx *ctx, const u8 *in_key,
			      unsigned int key_len);
asmlinkage void aesni_enc(const void *ctx, u8 *out, const u8 *in);
asmlinkage void aesni_dec(const void *ctx, u8 *out, const u8 *in);
asmlinkage void aesni_ecb_enc(struct crypto_aes_ctx *ctx, u8 *out,
			      const u8 *in, unsigned int len);
asmlinkage void aesni_ecb_dec(struct crypto_aes_ctx *ctx, u8 *out,
			      const u8 *in, unsigned int len);
asmlinkage void aesni_cbc_enc(struct crypto_aes_ctx *ctx, u8 *out,
			      const u8 *in, unsigned int len, u8 *iv);
asmlinkage void aesni_cbc_dec(struct crypto_aes_ctx *ctx, u8 *out,
			      const u8 *in, unsigned int len, u8 *iv);
asmlinkage void aesni_cts_cbc_enc(struct crypto_aes_ctx *ctx, u8 *out,
				  const u8 *in, unsigned int len, u8 *iv);
asmlinkage void aesni_cts_cbc_dec(struct crypto_aes_ctx *ctx, u8 *out,
				  const u8 *in, unsigned int len, u8 *iv);

asmlinkage void aesni_xts_enc(const struct crypto_aes_ctx *ctx, u8 *out,
			      const u8 *in, unsigned int len, u8 *iv);

asmlinkage void aesni_xts_dec(const struct crypto_aes_ctx *ctx, u8 *out,
			      const u8 *in, unsigned int len, u8 *iv);

#ifdef CONFIG_X86_64

asmlinkage void aesni_ctr_enc(struct crypto_aes_ctx *ctx, u8 *out,
			      const u8 *in, unsigned int len, u8 *iv);
DEFINE_STATIC_CALL(aesni_ctr_enc_tfm, aesni_ctr_enc);

asmlinkage void aes_ctr_enc_128_avx_by8(const u8 *in, u8 *iv,
		void *keys, u8 *out, unsigned int num_bytes);
asmlinkage void aes_ctr_enc_192_avx_by8(const u8 *in, u8 *iv,
		void *keys, u8 *out, unsigned int num_bytes);
asmlinkage void aes_ctr_enc_256_avx_by8(const u8 *in, u8 *iv,
		void *keys, u8 *out, unsigned int num_bytes);


asmlinkage void aes_xctr_enc_128_avx_by8(const u8 *in, const u8 *iv,
	const void *keys, u8 *out, unsigned int num_bytes,
	unsigned int byte_ctr);

asmlinkage void aes_xctr_enc_192_avx_by8(const u8 *in, const u8 *iv,
	const void *keys, u8 *out, unsigned int num_bytes,
	unsigned int byte_ctr);

asmlinkage void aes_xctr_enc_256_avx_by8(const u8 *in, const u8 *iv,
	const void *keys, u8 *out, unsigned int num_bytes,
	unsigned int byte_ctr);
#endif

static inline struct crypto_aes_ctx *aes_ctx(void *raw_ctx)
{
	return aes_align_addr(raw_ctx);
}

static inline struct aesni_xts_ctx *aes_xts_ctx(struct crypto_skcipher *tfm)
{
	return aes_align_addr(crypto_skcipher_ctx(tfm));
}

static int aes_set_key_common(struct crypto_aes_ctx *ctx,
			      const u8 *in_key, unsigned int key_len)
{
	int err;

	if (!crypto_simd_usable())
		return aes_expandkey(ctx, in_key, key_len);

	err = aes_check_keylen(key_len);
	if (err)
		return err;

	kernel_fpu_begin();
	aesni_set_key(ctx, in_key, key_len);
	kernel_fpu_end();
	return 0;
}

static int aes_set_key(struct crypto_tfm *tfm, const u8 *in_key,
		       unsigned int key_len)
{
	return aes_set_key_common(aes_ctx(crypto_tfm_ctx(tfm)), in_key,
				  key_len);
}

static void aesni_encrypt(struct crypto_tfm *tfm, u8 *dst, const u8 *src)
{
	struct crypto_aes_ctx *ctx = aes_ctx(crypto_tfm_ctx(tfm));

	if (!crypto_simd_usable()) {
		aes_encrypt(ctx, dst, src);
	} else {
		kernel_fpu_begin();
		aesni_enc(ctx, dst, src);
		kernel_fpu_end();
	}
}

static void aesni_decrypt(struct crypto_tfm *tfm, u8 *dst, const u8 *src)
{
	struct crypto_aes_ctx *ctx = aes_ctx(crypto_tfm_ctx(tfm));

	if (!crypto_simd_usable()) {
		aes_decrypt(ctx, dst, src);
	} else {
		kernel_fpu_begin();
		aesni_dec(ctx, dst, src);
		kernel_fpu_end();
	}
}

static int aesni_skcipher_setkey(struct crypto_skcipher *tfm, const u8 *key,
			         unsigned int len)
{
	return aes_set_key_common(aes_ctx(crypto_skcipher_ctx(tfm)), key, len);
}

static int ecb_encrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct crypto_aes_ctx *ctx = aes_ctx(crypto_skcipher_ctx(tfm));
	struct skcipher_walk walk;
	unsigned int nbytes;
	int err;

	err = skcipher_walk_virt(&walk, req, false);

	while ((nbytes = walk.nbytes)) {
		kernel_fpu_begin();
		aesni_ecb_enc(ctx, walk.dst.virt.addr, walk.src.virt.addr,
			      nbytes & AES_BLOCK_MASK);
		kernel_fpu_end();
		nbytes &= AES_BLOCK_SIZE - 1;
		err = skcipher_walk_done(&walk, nbytes);
	}

	return err;
}

static int ecb_decrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct crypto_aes_ctx *ctx = aes_ctx(crypto_skcipher_ctx(tfm));
	struct skcipher_walk walk;
	unsigned int nbytes;
	int err;

	err = skcipher_walk_virt(&walk, req, false);

	while ((nbytes = walk.nbytes)) {
		kernel_fpu_begin();
		aesni_ecb_dec(ctx, walk.dst.virt.addr, walk.src.virt.addr,
			      nbytes & AES_BLOCK_MASK);
		kernel_fpu_end();
		nbytes &= AES_BLOCK_SIZE - 1;
		err = skcipher_walk_done(&walk, nbytes);
	}

	return err;
}

static int cbc_encrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct crypto_aes_ctx *ctx = aes_ctx(crypto_skcipher_ctx(tfm));
	struct skcipher_walk walk;
	unsigned int nbytes;
	int err;

	err = skcipher_walk_virt(&walk, req, false);

	while ((nbytes = walk.nbytes)) {
		kernel_fpu_begin();
		aesni_cbc_enc(ctx, walk.dst.virt.addr, walk.src.virt.addr,
			      nbytes & AES_BLOCK_MASK, walk.iv);
		kernel_fpu_end();
		nbytes &= AES_BLOCK_SIZE - 1;
		err = skcipher_walk_done(&walk, nbytes);
	}

	return err;
}

static int cbc_decrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct crypto_aes_ctx *ctx = aes_ctx(crypto_skcipher_ctx(tfm));
	struct skcipher_walk walk;
	unsigned int nbytes;
	int err;

	err = skcipher_walk_virt(&walk, req, false);

	while ((nbytes = walk.nbytes)) {
		kernel_fpu_begin();
		aesni_cbc_dec(ctx, walk.dst.virt.addr, walk.src.virt.addr,
			      nbytes & AES_BLOCK_MASK, walk.iv);
		kernel_fpu_end();
		nbytes &= AES_BLOCK_SIZE - 1;
		err = skcipher_walk_done(&walk, nbytes);
	}

	return err;
}

static int cts_cbc_encrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct crypto_aes_ctx *ctx = aes_ctx(crypto_skcipher_ctx(tfm));
	int cbc_blocks = DIV_ROUND_UP(req->cryptlen, AES_BLOCK_SIZE) - 2;
	struct scatterlist *src = req->src, *dst = req->dst;
	struct scatterlist sg_src[2], sg_dst[2];
	struct skcipher_request subreq;
	struct skcipher_walk walk;
	int err;

	skcipher_request_set_tfm(&subreq, tfm);
	skcipher_request_set_callback(&subreq, skcipher_request_flags(req),
				      NULL, NULL);

	if (req->cryptlen <= AES_BLOCK_SIZE) {
		if (req->cryptlen < AES_BLOCK_SIZE)
			return -EINVAL;
		cbc_blocks = 1;
	}

	if (cbc_blocks > 0) {
		skcipher_request_set_crypt(&subreq, req->src, req->dst,
					   cbc_blocks * AES_BLOCK_SIZE,
					   req->iv);

		err = cbc_encrypt(&subreq);
		if (err)
			return err;

		if (req->cryptlen == AES_BLOCK_SIZE)
			return 0;

		dst = src = scatterwalk_ffwd(sg_src, req->src, subreq.cryptlen);
		if (req->dst != req->src)
			dst = scatterwalk_ffwd(sg_dst, req->dst,
					       subreq.cryptlen);
	}

	/* handle ciphertext stealing */
	skcipher_request_set_crypt(&subreq, src, dst,
				   req->cryptlen - cbc_blocks * AES_BLOCK_SIZE,
				   req->iv);

	err = skcipher_walk_virt(&walk, &subreq, false);
	if (err)
		return err;

	kernel_fpu_begin();
	aesni_cts_cbc_enc(ctx, walk.dst.virt.addr, walk.src.virt.addr,
			  walk.nbytes, walk.iv);
	kernel_fpu_end();

	return skcipher_walk_done(&walk, 0);
}

static int cts_cbc_decrypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct crypto_aes_ctx *ctx = aes_ctx(crypto_skcipher_ctx(tfm));
	int cbc_blocks = DIV_ROUND_UP(req->cryptlen, AES_BLOCK_SIZE) - 2;
	struct scatterlist *src = req->src, *dst = req->dst;
	struct scatterlist sg_src[2], sg_dst[2];
	struct skcipher_request subreq;
	struct skcipher_walk walk;
	int err;

	skcipher_request_set_tfm(&subreq, tfm);
	skcipher_request_set_callback(&subreq, skcipher_request_flags(req),
				      NULL, NULL);

	if (req->cryptlen <= AES_BLOCK_SIZE) {
		if (req->cryptlen < AES_BLOCK_SIZE)
			return -EINVAL;
		cbc_blocks = 1;
	}

	if (cbc_blocks > 0) {
		skcipher_request_set_crypt(&subreq, req->src, req->dst,
					   cbc_blocks * AES_BLOCK_SIZE,
					   req->iv);

		err = cbc_decrypt(&subreq);
		if (err)
			return err;

		if (req->cryptlen == AES_BLOCK_SIZE)
			return 0;

		dst = src = scatterwalk_ffwd(sg_src, req->src, subreq.cryptlen);
		if (req->dst != req->src)
			dst = scatterwalk_ffwd(sg_dst, req->dst,
					       subreq.cryptlen);
	}

	/* handle ciphertext stealing */
	skcipher_request_set_crypt(&subreq, src, dst,
				   req->cryptlen - cbc_blocks * AES_BLOCK_SIZE,
				   req->iv);

	err = skcipher_walk_virt(&walk, &subreq, false);
	if (err)
		return err;

	kernel_fpu_begin();
	aesni_cts_cbc_dec(ctx, walk.dst.virt.addr, walk.src.virt.addr,
			  walk.nbytes, walk.iv);
	kernel_fpu_end();

	return skcipher_walk_done(&walk, 0);
}

#ifdef CONFIG_X86_64
static void aesni_ctr_enc_avx_tfm(struct crypto_aes_ctx *ctx, u8 *out,
			      const u8 *in, unsigned int len, u8 *iv)
{
	/*
	 * based on key length, override with the by8 version
	 * of ctr mode encryption/decryption for improved performance
	 * aes_set_key_common() ensures that key length is one of
	 * {128,192,256}
	 */
	if (ctx->key_length == AES_KEYSIZE_128)
		aes_ctr_enc_128_avx_by8(in, iv, (void *)ctx, out, len);
	else if (ctx->key_length == AES_KEYSIZE_192)
		aes_ctr_enc_192_avx_by8(in, iv, (void *)ctx, out, len);
	else
		aes_ctr_enc_256_avx_by8(in, iv, (void *)ctx, out, len);
}

static int ctr_crypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct crypto_aes_ctx *ctx = aes_ctx(crypto_skcipher_ctx(tfm));
	u8 keystream[AES_BLOCK_SIZE];
	struct skcipher_walk walk;
	unsigned int nbytes;
	int err;

	err = skcipher_walk_virt(&walk, req, false);

	while ((nbytes = walk.nbytes) > 0) {
		kernel_fpu_begin();
		if (nbytes & AES_BLOCK_MASK)
			static_call(aesni_ctr_enc_tfm)(ctx, walk.dst.virt.addr,
						       walk.src.virt.addr,
						       nbytes & AES_BLOCK_MASK,
						       walk.iv);
		nbytes &= ~AES_BLOCK_MASK;

		if (walk.nbytes == walk.total && nbytes > 0) {
			aesni_enc(ctx, keystream, walk.iv);
			crypto_xor_cpy(walk.dst.virt.addr + walk.nbytes - nbytes,
				       walk.src.virt.addr + walk.nbytes - nbytes,
				       keystream, nbytes);
			crypto_inc(walk.iv, AES_BLOCK_SIZE);
			nbytes = 0;
		}
		kernel_fpu_end();
		err = skcipher_walk_done(&walk, nbytes);
	}
	return err;
}

static void aesni_xctr_enc_avx_tfm(struct crypto_aes_ctx *ctx, u8 *out,
				   const u8 *in, unsigned int len, u8 *iv,
				   unsigned int byte_ctr)
{
	if (ctx->key_length == AES_KEYSIZE_128)
		aes_xctr_enc_128_avx_by8(in, iv, (void *)ctx, out, len,
					 byte_ctr);
	else if (ctx->key_length == AES_KEYSIZE_192)
		aes_xctr_enc_192_avx_by8(in, iv, (void *)ctx, out, len,
					 byte_ctr);
	else
		aes_xctr_enc_256_avx_by8(in, iv, (void *)ctx, out, len,
					 byte_ctr);
}

static int xctr_crypt(struct skcipher_request *req)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct crypto_aes_ctx *ctx = aes_ctx(crypto_skcipher_ctx(tfm));
	u8 keystream[AES_BLOCK_SIZE];
	struct skcipher_walk walk;
	unsigned int nbytes;
	unsigned int byte_ctr = 0;
	int err;
	__le32 block[AES_BLOCK_SIZE / sizeof(__le32)];

	err = skcipher_walk_virt(&walk, req, false);

	while ((nbytes = walk.nbytes) > 0) {
		kernel_fpu_begin();
		if (nbytes & AES_BLOCK_MASK)
			aesni_xctr_enc_avx_tfm(ctx, walk.dst.virt.addr,
				walk.src.virt.addr, nbytes & AES_BLOCK_MASK,
				walk.iv, byte_ctr);
		nbytes &= ~AES_BLOCK_MASK;
		byte_ctr += walk.nbytes - nbytes;

		if (walk.nbytes == walk.total && nbytes > 0) {
			memcpy(block, walk.iv, AES_BLOCK_SIZE);
			block[0] ^= cpu_to_le32(1 + byte_ctr / AES_BLOCK_SIZE);
			aesni_enc(ctx, keystream, (u8 *)block);
			crypto_xor_cpy(walk.dst.virt.addr + walk.nbytes -
				       nbytes, walk.src.virt.addr + walk.nbytes
				       - nbytes, keystream, nbytes);
			byte_ctr += nbytes;
			nbytes = 0;
		}
		kernel_fpu_end();
		err = skcipher_walk_done(&walk, nbytes);
	}
	return err;
}
#endif

static int xts_setkey_aesni(struct crypto_skcipher *tfm, const u8 *key,
			    unsigned int keylen)
{
	struct aesni_xts_ctx *ctx = aes_xts_ctx(tfm);
	int err;

	err = xts_verify_key(tfm, key, keylen);
	if (err)
		return err;

	keylen /= 2;

	/* first half of xts-key is for crypt */
	err = aes_set_key_common(&ctx->crypt_ctx, key, keylen);
	if (err)
		return err;

	/* second half of xts-key is for tweak */
	return aes_set_key_common(&ctx->tweak_ctx, key + keylen, keylen);
}

typedef void (*xts_encrypt_iv_func)(const struct crypto_aes_ctx *tweak_key,
				    u8 iv[AES_BLOCK_SIZE]);
typedef void (*xts_crypt_func)(const struct crypto_aes_ctx *key,
			       const u8 *src, u8 *dst, unsigned int len,
			       u8 tweak[AES_BLOCK_SIZE]);

/* This handles cases where the source and/or destination span pages. */
static noinline int
xts_crypt_slowpath(struct skcipher_request *req, xts_crypt_func crypt_func)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	const struct aesni_xts_ctx *ctx = aes_xts_ctx(tfm);
	int tail = req->cryptlen % AES_BLOCK_SIZE;
	struct scatterlist sg_src[2], sg_dst[2];
	struct skcipher_request subreq;
	struct skcipher_walk walk;
	struct scatterlist *src, *dst;
	int err;

	/*
	 * If the message length isn't divisible by the AES block size, then
	 * separate off the last full block and the partial block.  This ensures
	 * that they are processed in the same call to the assembly function,
	 * which is required for ciphertext stealing.
	 */
	if (tail) {
		skcipher_request_set_tfm(&subreq, tfm);
		skcipher_request_set_callback(&subreq,
					      skcipher_request_flags(req),
					      NULL, NULL);
		skcipher_request_set_crypt(&subreq, req->src, req->dst,
					   req->cryptlen - tail - AES_BLOCK_SIZE,
					   req->iv);
		req = &subreq;
	}

	err = skcipher_walk_virt(&walk, req, false);

	while (walk.nbytes) {
		kernel_fpu_begin();
		(*crypt_func)(&ctx->crypt_ctx,
			      walk.src.virt.addr, walk.dst.virt.addr,
			      walk.nbytes & ~(AES_BLOCK_SIZE - 1), req->iv);
		kernel_fpu_end();
		err = skcipher_walk_done(&walk,
					 walk.nbytes & (AES_BLOCK_SIZE - 1));
	}

	if (err || !tail)
		return err;

	/* Do ciphertext stealing with the last full block and partial block. */

	dst = src = scatterwalk_ffwd(sg_src, req->src, req->cryptlen);
	if (req->dst != req->src)
		dst = scatterwalk_ffwd(sg_dst, req->dst, req->cryptlen);

	skcipher_request_set_crypt(req, src, dst, AES_BLOCK_SIZE + tail,
				   req->iv);

	err = skcipher_walk_virt(&walk, req, false);
	if (err)
		return err;

	kernel_fpu_begin();
	(*crypt_func)(&ctx->crypt_ctx, walk.src.virt.addr, walk.dst.virt.addr,
		      walk.nbytes, req->iv);
	kernel_fpu_end();

	return skcipher_walk_done(&walk, 0);
}

/* __always_inline to avoid indirect call in fastpath */
static __always_inline int
xts_crypt(struct skcipher_request *req, xts_encrypt_iv_func encrypt_iv,
	  xts_crypt_func crypt_func)
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	const struct aesni_xts_ctx *ctx = aes_xts_ctx(tfm);
	const unsigned int cryptlen = req->cryptlen;
	struct scatterlist *src = req->src;
	struct scatterlist *dst = req->dst;

	if (unlikely(cryptlen < AES_BLOCK_SIZE))
		return -EINVAL;

	kernel_fpu_begin();
	(*encrypt_iv)(&ctx->tweak_ctx, req->iv);

	/*
	 * In practice, virtually all XTS plaintexts and ciphertexts are either
	 * 512 or 4096 bytes, aligned such that they don't span page boundaries.
	 * To optimize the performance of these cases, and also any other case
	 * where no page boundary is spanned, the below fast-path handles
	 * single-page sources and destinations as efficiently as possible.
	 */
	if (likely(src->length >= cryptlen && dst->length >= cryptlen &&
		   src->offset + cryptlen <= PAGE_SIZE &&
		   dst->offset + cryptlen <= PAGE_SIZE)) {
		struct page *src_page = sg_page(src);
		struct page *dst_page = sg_page(dst);
		void *src_virt = kmap_local_page(src_page) + src->offset;
		void *dst_virt = kmap_local_page(dst_page) + dst->offset;

		(*crypt_func)(&ctx->crypt_ctx, src_virt, dst_virt, cryptlen,
			      req->iv);
		kunmap_local(dst_virt);
		kunmap_local(src_virt);
		kernel_fpu_end();
		return 0;
	}
	kernel_fpu_end();
	return xts_crypt_slowpath(req, crypt_func);
}

static void aesni_xts_encrypt_iv(const struct crypto_aes_ctx *tweak_key,
				 u8 iv[AES_BLOCK_SIZE])
{
	aesni_enc(tweak_key, iv, iv);
}

static void aesni_xts_encrypt(const struct crypto_aes_ctx *key,
			      const u8 *src, u8 *dst, unsigned int len,
			      u8 tweak[AES_BLOCK_SIZE])
{
	aesni_xts_enc(key, dst, src, len, tweak);
}

static void aesni_xts_decrypt(const struct crypto_aes_ctx *key,
			      const u8 *src, u8 *dst, unsigned int len,
			      u8 tweak[AES_BLOCK_SIZE])
{
	aesni_xts_dec(key, dst, src, len, tweak);
}

static int xts_encrypt_aesni(struct skcipher_request *req)
{
	return xts_crypt(req, aesni_xts_encrypt_iv, aesni_xts_encrypt);
}

static int xts_decrypt_aesni(struct skcipher_request *req)
{
	return xts_crypt(req, aesni_xts_encrypt_iv, aesni_xts_decrypt);
}

static struct crypto_alg aesni_cipher_alg = {
	.cra_name		= "aes",
	.cra_driver_name	= "aes-aesni",
	.cra_priority		= 300,
	.cra_flags		= CRYPTO_ALG_TYPE_CIPHER,
	.cra_blocksize		= AES_BLOCK_SIZE,
	.cra_ctxsize		= CRYPTO_AES_CTX_SIZE,
	.cra_module		= THIS_MODULE,
	.cra_u	= {
		.cipher	= {
			.cia_min_keysize	= AES_MIN_KEY_SIZE,
			.cia_max_keysize	= AES_MAX_KEY_SIZE,
			.cia_setkey		= aes_set_key,
			.cia_encrypt		= aesni_encrypt,
			.cia_decrypt		= aesni_decrypt
		}
	}
};

static struct skcipher_alg aesni_skciphers[] = {
	{
		.base = {
			.cra_name		= "__ecb(aes)",
			.cra_driver_name	= "__ecb-aes-aesni",
			.cra_priority		= 400,
			.cra_flags		= CRYPTO_ALG_INTERNAL,
			.cra_blocksize		= AES_BLOCK_SIZE,
			.cra_ctxsize		= CRYPTO_AES_CTX_SIZE,
			.cra_module		= THIS_MODULE,
		},
		.min_keysize	= AES_MIN_KEY_SIZE,
		.max_keysize	= AES_MAX_KEY_SIZE,
		.setkey		= aesni_skcipher_setkey,
		.encrypt	= ecb_encrypt,
		.decrypt	= ecb_decrypt,
	}, {
		.base = {
			.cra_name		= "__cbc(aes)",
			.cra_driver_name	= "__cbc-aes-aesni",
			.cra_priority		= 400,
			.cra_flags		= CRYPTO_ALG_INTERNAL,
			.cra_blocksize		= AES_BLOCK_SIZE,
			.cra_ctxsize		= CRYPTO_AES_CTX_SIZE,
			.cra_module		= THIS_MODULE,
		},
		.min_keysize	= AES_MIN_KEY_SIZE,
		.max_keysize	= AES_MAX_KEY_SIZE,
		.ivsize		= AES_BLOCK_SIZE,
		.setkey		= aesni_skcipher_setkey,
		.encrypt	= cbc_encrypt,
		.decrypt	= cbc_decrypt,
	}, {
		.base = {
			.cra_name		= "__cts(cbc(aes))",
			.cra_driver_name	= "__cts-cbc-aes-aesni",
			.cra_priority		= 400,
			.cra_flags		= CRYPTO_ALG_INTERNAL,
			.cra_blocksize		= AES_BLOCK_SIZE,
			.cra_ctxsize		= CRYPTO_AES_CTX_SIZE,
			.cra_module		= THIS_MODULE,
		},
		.min_keysize	= AES_MIN_KEY_SIZE,
		.max_keysize	= AES_MAX_KEY_SIZE,
		.ivsize		= AES_BLOCK_SIZE,
		.walksize	= 2 * AES_BLOCK_SIZE,
		.setkey		= aesni_skcipher_setkey,
		.encrypt	= cts_cbc_encrypt,
		.decrypt	= cts_cbc_decrypt,
#ifdef CONFIG_X86_64
	}, {
		.base = {
			.cra_name		= "__ctr(aes)",
			.cra_driver_name	= "__ctr-aes-aesni",
			.cra_priority		= 400,
			.cra_flags		= CRYPTO_ALG_INTERNAL,
			.cra_blocksize		= 1,
			.cra_ctxsize		= CRYPTO_AES_CTX_SIZE,
			.cra_module		= THIS_MODULE,
		},
		.min_keysize	= AES_MIN_KEY_SIZE,
		.max_keysize	= AES_MAX_KEY_SIZE,
		.ivsize		= AES_BLOCK_SIZE,
		.chunksize	= AES_BLOCK_SIZE,
		.setkey		= aesni_skcipher_setkey,
		.encrypt	= ctr_crypt,
		.decrypt	= ctr_crypt,
#endif
	}, {
		.base = {
			.cra_name		= "__xts(aes)",
			.cra_driver_name	= "__xts-aes-aesni",
			.cra_priority		= 401,
			.cra_flags		= CRYPTO_ALG_INTERNAL,
			.cra_blocksize		= AES_BLOCK_SIZE,
			.cra_ctxsize		= XTS_AES_CTX_SIZE,
			.cra_module		= THIS_MODULE,
		},
		.min_keysize	= 2 * AES_MIN_KEY_SIZE,
		.max_keysize	= 2 * AES_MAX_KEY_SIZE,
		.ivsize		= AES_BLOCK_SIZE,
		.walksize	= 2 * AES_BLOCK_SIZE,
		.setkey		= xts_setkey_aesni,
		.encrypt	= xts_encrypt_aesni,
		.decrypt	= xts_decrypt_aesni,
	}
};

static
struct simd_skcipher_alg *aesni_simd_skciphers[ARRAY_SIZE(aesni_skciphers)];

#ifdef CONFIG_X86_64
/*
 * XCTR does not have a non-AVX implementation, so it must be enabled
 * conditionally.
 */
static struct skcipher_alg aesni_xctr = {
	.base = {
		.cra_name		= "__xctr(aes)",
		.cra_driver_name	= "__xctr-aes-aesni",
		.cra_priority		= 400,
		.cra_flags		= CRYPTO_ALG_INTERNAL,
		.cra_blocksize		= 1,
		.cra_ctxsize		= CRYPTO_AES_CTX_SIZE,
		.cra_module		= THIS_MODULE,
	},
	.min_keysize	= AES_MIN_KEY_SIZE,
	.max_keysize	= AES_MAX_KEY_SIZE,
	.ivsize		= AES_BLOCK_SIZE,
	.chunksize	= AES_BLOCK_SIZE,
	.setkey		= aesni_skcipher_setkey,
	.encrypt	= xctr_crypt,
	.decrypt	= xctr_crypt,
};

static struct simd_skcipher_alg *aesni_simd_xctr;

asmlinkage void aes_xts_encrypt_iv(const struct crypto_aes_ctx *tweak_key,
				   u8 iv[AES_BLOCK_SIZE]);

#define DEFINE_XTS_ALG(suffix, driver_name, priority)			       \
									       \
asmlinkage void								       \
aes_xts_encrypt_##suffix(const struct crypto_aes_ctx *key, const u8 *src,      \
			 u8 *dst, unsigned int len, u8 tweak[AES_BLOCK_SIZE]); \
asmlinkage void								       \
aes_xts_decrypt_##suffix(const struct crypto_aes_ctx *key, const u8 *src,      \
			 u8 *dst, unsigned int len, u8 tweak[AES_BLOCK_SIZE]); \
									       \
static int xts_encrypt_##suffix(struct skcipher_request *req)		       \
{									       \
	return xts_crypt(req, aes_xts_encrypt_iv, aes_xts_encrypt_##suffix);   \
}									       \
									       \
static int xts_decrypt_##suffix(struct skcipher_request *req)		       \
{									       \
	return xts_crypt(req, aes_xts_encrypt_iv, aes_xts_decrypt_##suffix);   \
}									       \
									       \
static struct skcipher_alg aes_xts_alg_##suffix = {			       \
	.base = {							       \
		.cra_name		= "__xts(aes)",			       \
		.cra_driver_name	= "__" driver_name,		       \
		.cra_priority		= priority,			       \
		.cra_flags		= CRYPTO_ALG_INTERNAL,		       \
		.cra_blocksize		= AES_BLOCK_SIZE,		       \
		.cra_ctxsize		= XTS_AES_CTX_SIZE,		       \
		.cra_module		= THIS_MODULE,			       \
	},								       \
	.min_keysize	= 2 * AES_MIN_KEY_SIZE,				       \
	.max_keysize	= 2 * AES_MAX_KEY_SIZE,				       \
	.ivsize		= AES_BLOCK_SIZE,				       \
	.walksize	= 2 * AES_BLOCK_SIZE,				       \
	.setkey		= xts_setkey_aesni,				       \
	.encrypt	= xts_encrypt_##suffix,				       \
	.decrypt	= xts_decrypt_##suffix,				       \
};									       \
									       \
static struct simd_skcipher_alg *aes_xts_simdalg_##suffix

DEFINE_XTS_ALG(aesni_avx, "xts-aes-aesni-avx", 500);
#if defined(CONFIG_AS_VAES) && defined(CONFIG_AS_VPCLMULQDQ)
DEFINE_XTS_ALG(vaes_avx2, "xts-aes-vaes-avx2", 600);
DEFINE_XTS_ALG(vaes_avx10_256, "xts-aes-vaes-avx10_256", 700);
DEFINE_XTS_ALG(vaes_avx10_512, "xts-aes-vaes-avx10_512", 800);
#endif

/* The common part of the x86_64 AES-GCM key struct */
struct aes_gcm_key {
	/* Expanded AES key and the AES key length in bytes */
	struct crypto_aes_ctx aes_key;

	/* RFC4106 nonce (used only by the rfc4106 algorithms) */
	u32 rfc4106_nonce;
};

/* Key struct used by the AES-NI implementations of AES-GCM */
struct aes_gcm_key_aesni {
	/*
	 * Common part of the key.  The assembly code requires 16-byte alignment
	 * for the round keys; we get this by them being located at the start of
	 * the struct and the whole struct being 16-byte aligned.
	 */
	struct aes_gcm_key base;

	/*
	 * Powers of the hash key H^8 through H^1.  These are 128-bit values.
	 * They all have an extra factor of x^-1 and are byte-reversed.  16-byte
	 * alignment is required by the assembly code.
	 */
	u64 h_powers[8][2] __aligned(16);

	/*
	 * h_powers_xored[i] contains the two 64-bit halves of h_powers[i] XOR'd
	 * together.  It's used for Karatsuba multiplication.  16-byte alignment
	 * is required by the assembly code.
	 */
	u64 h_powers_xored[8] __aligned(16);

	/*
	 * H^1 times x^64 (and also the usual extra factor of x^-1).  16-byte
	 * alignment is required by the assembly code.
	 */
	u64 h_times_x64[2] __aligned(16);
};
#define AES_GCM_KEY_AESNI(key)	\
	container_of((key), struct aes_gcm_key_aesni, base)
#define AES_GCM_KEY_AESNI_SIZE	\
	(sizeof(struct aes_gcm_key_aesni) + (15 & ~(CRYPTO_MINALIGN - 1)))

/* Key struct used by the VAES + AVX10 implementations of AES-GCM */
struct aes_gcm_key_avx10 {
	/*
	 * Common part of the key.  The assembly code prefers 16-byte alignment
	 * for the round keys; we get this by them being located at the start of
	 * the struct and the whole struct being 64-byte aligned.
	 */
	struct aes_gcm_key base;

	/*
	 * Powers of the hash key H^16 through H^1.  These are 128-bit values.
	 * They all have an extra factor of x^-1 and are byte-reversed.  This
	 * array is aligned to a 64-byte boundary to make it naturally aligned
	 * for 512-bit loads, which can improve performance.  (The assembly code
	 * doesn't *need* the alignment; this is just an optimization.)
	 */
	u64 h_powers[16][2] __aligned(64);

	/* Three padding blocks required by the assembly code */
	u64 padding[3][2];
};
#define AES_GCM_KEY_AVX10(key)	\
	container_of((key), struct aes_gcm_key_avx10, base)
#define AES_GCM_KEY_AVX10_SIZE	\
	(sizeof(struct aes_gcm_key_avx10) + (63 & ~(CRYPTO_MINALIGN - 1)))

/*
 * These flags are passed to the AES-GCM helper functions to specify the
 * specific version of AES-GCM (RFC4106 or not), whether it's encryption or
 * decryption, and which assembly functions should be called.  Assembly
 * functions are selected using flags instead of function pointers to avoid
 * indirect calls (which are very expensive on x86) regardless of inlining.
 */
#define FLAG_RFC4106	BIT(0)
#define FLAG_ENC	BIT(1)
#define FLAG_AVX	BIT(2)
#if defined(CONFIG_AS_VAES) && defined(CONFIG_AS_VPCLMULQDQ)
#  define FLAG_AVX10_256	BIT(3)
#  define FLAG_AVX10_512	BIT(4)
#else
   /*
    * This should cause all calls to the AVX10 assembly functions to be
    * optimized out, avoiding the need to ifdef each call individually.
    */
#  define FLAG_AVX10_256	0
#  define FLAG_AVX10_512	0
#endif

static inline struct aes_gcm_key *
aes_gcm_key_get(struct crypto_aead *tfm, int flags)
{
	if (flags & (FLAG_AVX10_256 | FLAG_AVX10_512))
		return PTR_ALIGN(crypto_aead_ctx(tfm), 64);
	else
		return PTR_ALIGN(crypto_aead_ctx(tfm), 16);
}

asmlinkage void
aes_gcm_precompute_aesni(struct aes_gcm_key_aesni *key);
asmlinkage void
aes_gcm_precompute_aesni_avx(struct aes_gcm_key_aesni *key);
asmlinkage void
aes_gcm_precompute_vaes_avx10_256(struct aes_gcm_key_avx10 *key);
asmlinkage void
aes_gcm_precompute_vaes_avx10_512(struct aes_gcm_key_avx10 *key);

static void aes_gcm_precompute(struct aes_gcm_key *key, int flags)
{
	/*
	 * To make things a bit easier on the assembly side, the AVX10
	 * implementations use the same key format.  Therefore, a single
	 * function using 256-bit vectors would suffice here.  However, it's
	 * straightforward to provide a 512-bit one because of how the assembly
	 * code is structured, and it works nicely because the total size of the
	 * key powers is a multiple of 512 bits.  So we take advantage of that.
	 *
	 * A similar situation applies to the AES-NI implementations.
	 */
	if (flags & FLAG_AVX10_512)
		aes_gcm_precompute_vaes_avx10_512(AES_GCM_KEY_AVX10(key));
	else if (flags & FLAG_AVX10_256)
		aes_gcm_precompute_vaes_avx10_256(AES_GCM_KEY_AVX10(key));
	else if (flags & FLAG_AVX)
		aes_gcm_precompute_aesni_avx(AES_GCM_KEY_AESNI(key));
	else
		aes_gcm_precompute_aesni(AES_GCM_KEY_AESNI(key));
}

asmlinkage void
aes_gcm_aad_update_aesni(const struct aes_gcm_key_aesni *key,
			 u8 ghash_acc[16], const u8 *aad, int aadlen);
asmlinkage void
aes_gcm_aad_update_aesni_avx(const struct aes_gcm_key_aesni *key,
			     u8 ghash_acc[16], const u8 *aad, int aadlen);
asmlinkage void
aes_gcm_aad_update_vaes_avx10(const struct aes_gcm_key_avx10 *key,
			      u8 ghash_acc[16], const u8 *aad, int aadlen);

static void aes_gcm_aad_update(const struct aes_gcm_key *key, u8 ghash_acc[16],
			       const u8 *aad, int aadlen, int flags)
{
	if (flags & (FLAG_AVX10_256 | FLAG_AVX10_512))
		aes_gcm_aad_update_vaes_avx10(AES_GCM_KEY_AVX10(key), ghash_acc,
					      aad, aadlen);
	else if (flags & FLAG_AVX)
		aes_gcm_aad_update_aesni_avx(AES_GCM_KEY_AESNI(key), ghash_acc,
					     aad, aadlen);
	else
		aes_gcm_aad_update_aesni(AES_GCM_KEY_AESNI(key), ghash_acc,
					 aad, aadlen);
}

asmlinkage void
aes_gcm_enc_update_aesni(const struct aes_gcm_key_aesni *key,
			 const u32 le_ctr[4], u8 ghash_acc[16],
			 const u8 *src, u8 *dst, int datalen);
asmlinkage void
aes_gcm_enc_update_aesni_avx(const struct aes_gcm_key_aesni *key,
			     const u32 le_ctr[4], u8 ghash_acc[16],
			     const u8 *src, u8 *dst, int datalen);
asmlinkage void
aes_gcm_enc_update_vaes_avx10_256(const struct aes_gcm_key_avx10 *key,
				  const u32 le_ctr[4], u8 ghash_acc[16],
				  const u8 *src, u8 *dst, int datalen);
asmlinkage void
aes_gcm_enc_update_vaes_avx10_512(const struct aes_gcm_key_avx10 *key,
				  const u32 le_ctr[4], u8 ghash_acc[16],
				  const u8 *src, u8 *dst, int datalen);

asmlinkage void
aes_gcm_dec_update_aesni(const struct aes_gcm_key_aesni *key,
			 const u32 le_ctr[4], u8 ghash_acc[16],
			 const u8 *src, u8 *dst, int datalen);
asmlinkage void
aes_gcm_dec_update_aesni_avx(const struct aes_gcm_key_aesni *key,
			     const u32 le_ctr[4], u8 ghash_acc[16],
			     const u8 *src, u8 *dst, int datalen);
asmlinkage void
aes_gcm_dec_update_vaes_avx10_256(const struct aes_gcm_key_avx10 *key,
				  const u32 le_ctr[4], u8 ghash_acc[16],
				  const u8 *src, u8 *dst, int datalen);
asmlinkage void
aes_gcm_dec_update_vaes_avx10_512(const struct aes_gcm_key_avx10 *key,
				  const u32 le_ctr[4], u8 ghash_acc[16],
				  const u8 *src, u8 *dst, int datalen);

/* __always_inline to optimize out the branches based on @flags */
static __always_inline void
aes_gcm_update(const struct aes_gcm_key *key,
	       const u32 le_ctr[4], u8 ghash_acc[16],
	       const u8 *src, u8 *dst, int datalen, int flags)
{
	if (flags & FLAG_ENC) {
		if (flags & FLAG_AVX10_512)
			aes_gcm_enc_update_vaes_avx10_512(AES_GCM_KEY_AVX10(key),
							  le_ctr, ghash_acc,
							  src, dst, datalen);
		else if (flags & FLAG_AVX10_256)
			aes_gcm_enc_update_vaes_avx10_256(AES_GCM_KEY_AVX10(key),
							  le_ctr, ghash_acc,
							  src, dst, datalen);
		else if (flags & FLAG_AVX)
			aes_gcm_enc_update_aesni_avx(AES_GCM_KEY_AESNI(key),
						     le_ctr, ghash_acc,
						     src, dst, datalen);
		else
			aes_gcm_enc_update_aesni(AES_GCM_KEY_AESNI(key), le_ctr,
						 ghash_acc, src, dst, datalen);
	} else {
		if (flags & FLAG_AVX10_512)
			aes_gcm_dec_update_vaes_avx10_512(AES_GCM_KEY_AVX10(key),
							  le_ctr, ghash_acc,
							  src, dst, datalen);
		else if (flags & FLAG_AVX10_256)
			aes_gcm_dec_update_vaes_avx10_256(AES_GCM_KEY_AVX10(key),
							  le_ctr, ghash_acc,
							  src, dst, datalen);
		else if (flags & FLAG_AVX)
			aes_gcm_dec_update_aesni_avx(AES_GCM_KEY_AESNI(key),
						     le_ctr, ghash_acc,
						     src, dst, datalen);
		else
			aes_gcm_dec_update_aesni(AES_GCM_KEY_AESNI(key),
						 le_ctr, ghash_acc,
						 src, dst, datalen);
	}
}

asmlinkage void
aes_gcm_enc_final_aesni(const struct aes_gcm_key_aesni *key,
			const u32 le_ctr[4], u8 ghash_acc[16],
			u64 total_aadlen, u64 total_datalen);
asmlinkage void
aes_gcm_enc_final_aesni_avx(const struct aes_gcm_key_aesni *key,
			    const u32 le_ctr[4], u8 ghash_acc[16],
			    u64 total_aadlen, u64 total_datalen);
asmlinkage void
aes_gcm_enc_final_vaes_avx10(const struct aes_gcm_key_avx10 *key,
			     const u32 le_ctr[4], u8 ghash_acc[16],
			     u64 total_aadlen, u64 total_datalen);

/* __always_inline to optimize out the branches based on @flags */
static __always_inline void
aes_gcm_enc_final(const struct aes_gcm_key *key,
		  const u32 le_ctr[4], u8 ghash_acc[16],
		  u64 total_aadlen, u64 total_datalen, int flags)
{
	if (flags & (FLAG_AVX10_256 | FLAG_AVX10_512))
		aes_gcm_enc_final_vaes_avx10(AES_GCM_KEY_AVX10(key),
					     le_ctr, ghash_acc,
					     total_aadlen, total_datalen);
	else if (flags & FLAG_AVX)
		aes_gcm_enc_final_aesni_avx(AES_GCM_KEY_AESNI(key),
					    le_ctr, ghash_acc,
					    total_aadlen, total_datalen);
	else
		aes_gcm_enc_final_aesni(AES_GCM_KEY_AESNI(key),
					le_ctr, ghash_acc,
					total_aadlen, total_datalen);
}

asmlinkage bool __must_check
aes_gcm_dec_final_aesni(const struct aes_gcm_key_aesni *key,
			const u32 le_ctr[4], const u8 ghash_acc[16],
			u64 total_aadlen, u64 total_datalen,
			const u8 tag[16], int taglen);
asmlinkage bool __must_check
aes_gcm_dec_final_aesni_avx(const struct aes_gcm_key_aesni *key,
			    const u32 le_ctr[4], const u8 ghash_acc[16],
			    u64 total_aadlen, u64 total_datalen,
			    const u8 tag[16], int taglen);
asmlinkage bool __must_check
aes_gcm_dec_final_vaes_avx10(const struct aes_gcm_key_avx10 *key,
			     const u32 le_ctr[4], const u8 ghash_acc[16],
			     u64 total_aadlen, u64 total_datalen,
			     const u8 tag[16], int taglen);

/* __always_inline to optimize out the branches based on @flags */
static __always_inline bool __must_check
aes_gcm_dec_final(const struct aes_gcm_key *key, const u32 le_ctr[4],
		  u8 ghash_acc[16], u64 total_aadlen, u64 total_datalen,
		  u8 tag[16], int taglen, int flags)
{
	if (flags & (FLAG_AVX10_256 | FLAG_AVX10_512))
		return aes_gcm_dec_final_vaes_avx10(AES_GCM_KEY_AVX10(key),
						    le_ctr, ghash_acc,
						    total_aadlen, total_datalen,
						    tag, taglen);
	else if (flags & FLAG_AVX)
		return aes_gcm_dec_final_aesni_avx(AES_GCM_KEY_AESNI(key),
						   le_ctr, ghash_acc,
						   total_aadlen, total_datalen,
						   tag, taglen);
	else
		return aes_gcm_dec_final_aesni(AES_GCM_KEY_AESNI(key),
					       le_ctr, ghash_acc,
					       total_aadlen, total_datalen,
					       tag, taglen);
}

/*
 * This is the Integrity Check Value (aka the authentication tag) length and can
 * be 8, 12 or 16 bytes long.
 */
static int common_rfc4106_set_authsize(struct crypto_aead *aead,
				       unsigned int authsize)
{
	switch (authsize) {
	case 8:
	case 12:
	case 16:
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int generic_gcmaes_set_authsize(struct crypto_aead *tfm,
				       unsigned int authsize)
{
	switch (authsize) {
	case 4:
	case 8:
	case 12:
	case 13:
	case 14:
	case 15:
	case 16:
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

/*
 * This is the setkey function for the x86_64 implementations of AES-GCM.  It
 * saves the RFC4106 nonce if applicable, expands the AES key, and precomputes
 * powers of the hash key.
 *
 * To comply with the crypto_aead API, this has to be usable in no-SIMD context.
 * For that reason, this function includes a portable C implementation of the
 * needed logic.  However, the portable C implementation is very slow, taking
 * about the same time as encrypting 37 KB of data.  To be ready for users that
 * may set a key even somewhat frequently, we therefore also include a SIMD
 * assembly implementation, expanding the AES key using AES-NI and precomputing
 * the hash key powers using PCLMULQDQ or VPCLMULQDQ.
 */
static int gcm_setkey(struct crypto_aead *tfm, const u8 *raw_key,
		      unsigned int keylen, int flags)
{
	struct aes_gcm_key *key = aes_gcm_key_get(tfm, flags);
	int err;

	if (flags & FLAG_RFC4106) {
		if (keylen < 4)
			return -EINVAL;
		keylen -= 4;
		key->rfc4106_nonce = get_unaligned_be32(raw_key + keylen);
	}

	/* The assembly code assumes the following offsets. */
	BUILD_BUG_ON(offsetof(struct aes_gcm_key_aesni, base.aes_key.key_enc) != 0);
	BUILD_BUG_ON(offsetof(struct aes_gcm_key_aesni, base.aes_key.key_length) != 480);
	BUILD_BUG_ON(offsetof(struct aes_gcm_key_aesni, h_powers) != 496);
	BUILD_BUG_ON(offsetof(struct aes_gcm_key_aesni, h_powers_xored) != 624);
	BUILD_BUG_ON(offsetof(struct aes_gcm_key_aesni, h_times_x64) != 688);
	BUILD_BUG_ON(offsetof(struct aes_gcm_key_avx10, base.aes_key.key_enc) != 0);
	BUILD_BUG_ON(offsetof(struct aes_gcm_key_avx10, base.aes_key.key_length) != 480);
	BUILD_BUG_ON(offsetof(struct aes_gcm_key_avx10, h_powers) != 512);
	BUILD_BUG_ON(offsetof(struct aes_gcm_key_avx10, padding) != 768);

	if (likely(crypto_simd_usable())) {
		err = aes_check_keylen(keylen);
		if (err)
			return err;
		kernel_fpu_begin();
		aesni_set_key(&key->aes_key, raw_key, keylen);
		aes_gcm_precompute(key, flags);
		kernel_fpu_end();
	} else {
		static const u8 x_to_the_minus1[16] __aligned(__alignof__(be128)) = {
			[0] = 0xc2, [15] = 1
		};
		static const u8 x_to_the_63[16] __aligned(__alignof__(be128)) = {
			[7] = 1,
		};
		be128 h1 = {};
		be128 h;
		int i;

		err = aes_expandkey(&key->aes_key, raw_key, keylen);
		if (err)
			return err;

		/* Encrypt the all-zeroes block to get the hash key H^1 */
		aes_encrypt(&key->aes_key, (u8 *)&h1, (u8 *)&h1);

		/* Compute H^1 * x^-1 */
		h = h1;
		gf128mul_lle(&h, (const be128 *)x_to_the_minus1);

		/* Compute the needed key powers */
		if (flags & (FLAG_AVX10_256 | FLAG_AVX10_512)) {
			struct aes_gcm_key_avx10 *k = AES_GCM_KEY_AVX10(key);

			for (i = ARRAY_SIZE(k->h_powers) - 1; i >= 0; i--) {
				k->h_powers[i][0] = be64_to_cpu(h.b);
				k->h_powers[i][1] = be64_to_cpu(h.a);
				gf128mul_lle(&h, &h1);
			}
			memset(k->padding, 0, sizeof(k->padding));
		} else {
			struct aes_gcm_key_aesni *k = AES_GCM_KEY_AESNI(key);

			for (i = ARRAY_SIZE(k->h_powers) - 1; i >= 0; i--) {
				k->h_powers[i][0] = be64_to_cpu(h.b);
				k->h_powers[i][1] = be64_to_cpu(h.a);
				k->h_powers_xored[i] = k->h_powers[i][0] ^
						       k->h_powers[i][1];
				gf128mul_lle(&h, &h1);
			}
			gf128mul_lle(&h1, (const be128 *)x_to_the_63);
			k->h_times_x64[0] = be64_to_cpu(h1.b);
			k->h_times_x64[1] = be64_to_cpu(h1.a);
		}
	}
	return 0;
}

/*
 * Initialize @ghash_acc, then pass all @assoclen bytes of associated data
 * (a.k.a. additional authenticated data) from @sg_src through the GHASH update
 * assembly function.  kernel_fpu_begin() must have already been called.
 */
static void gcm_process_assoc(const struct aes_gcm_key *key, u8 ghash_acc[16],
			      struct scatterlist *sg_src, unsigned int assoclen,
			      int flags)
{
	struct scatter_walk walk;
	/*
	 * The assembly function requires that the length of any non-last
	 * segment of associated data be a multiple of 16 bytes, so this
	 * function does the buffering needed to achieve that.
	 */
	unsigned int pos = 0;
	u8 buf[16];

	memset(ghash_acc, 0, 16);
	scatterwalk_start(&walk, sg_src);

	while (assoclen) {
		unsigned int len_this_page = scatterwalk_clamp(&walk, assoclen);
		void *mapped = scatterwalk_map(&walk);
		const void *src = mapped;
		unsigned int len;

		assoclen -= len_this_page;
		scatterwalk_advance(&walk, len_this_page);
		if (unlikely(pos)) {
			len = min(len_this_page, 16 - pos);
			memcpy(&buf[pos], src, len);
			pos += len;
			src += len;
			len_this_page -= len;
			if (pos < 16)
				goto next;
			aes_gcm_aad_update(key, ghash_acc, buf, 16, flags);
			pos = 0;
		}
		len = len_this_page;
		if (unlikely(assoclen)) /* Not the last segment yet? */
			len = round_down(len, 16);
		aes_gcm_aad_update(key, ghash_acc, src, len, flags);
		src += len;
		len_this_page -= len;
		if (unlikely(len_this_page)) {
			memcpy(buf, src, len_this_page);
			pos = len_this_page;
		}
next:
		scatterwalk_unmap(mapped);
		scatterwalk_pagedone(&walk, 0, assoclen);
		if (need_resched()) {
			kernel_fpu_end();
			kernel_fpu_begin();
		}
	}
	if (unlikely(pos))
		aes_gcm_aad_update(key, ghash_acc, buf, pos, flags);
}


/* __always_inline to optimize out the branches based on @flags */
static __always_inline int
gcm_crypt(struct aead_request *req, int flags)
{
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	const struct aes_gcm_key *key = aes_gcm_key_get(tfm, flags);
	unsigned int assoclen = req->assoclen;
	struct skcipher_walk walk;
	unsigned int nbytes;
	u8 ghash_acc[16]; /* GHASH accumulator */
	u32 le_ctr[4]; /* Counter in little-endian format */
	int taglen;
	int err;

	/* Initialize the counter and determine the associated data length. */
	le_ctr[0] = 2;
	if (flags & FLAG_RFC4106) {
		if (unlikely(assoclen != 16 && assoclen != 20))
			return -EINVAL;
		assoclen -= 8;
		le_ctr[1] = get_unaligned_be32(req->iv + 4);
		le_ctr[2] = get_unaligned_be32(req->iv + 0);
		le_ctr[3] = key->rfc4106_nonce; /* already byte-swapped */
	} else {
		le_ctr[1] = get_unaligned_be32(req->iv + 8);
		le_ctr[2] = get_unaligned_be32(req->iv + 4);
		le_ctr[3] = get_unaligned_be32(req->iv + 0);
	}

	/* Begin walking through the plaintext or ciphertext. */
	if (flags & FLAG_ENC)
		err = skcipher_walk_aead_encrypt(&walk, req, false);
	else
		err = skcipher_walk_aead_decrypt(&walk, req, false);
	if (err)
		return err;

	/*
	 * Since the AES-GCM assembly code requires that at least three assembly
	 * functions be called to process any message (this is needed to support
	 * incremental updates cleanly), to reduce overhead we try to do all
	 * three calls in the same kernel FPU section if possible.  We close the
	 * section and start a new one if there are multiple data segments or if
	 * rescheduling is needed while processing the associated data.
	 */
	kernel_fpu_begin();

	/* Pass the associated data through GHASH. */
	gcm_process_assoc(key, ghash_acc, req->src, assoclen, flags);

	/* En/decrypt the data and pass the ciphertext through GHASH. */
	while (unlikely((nbytes = walk.nbytes) < walk.total)) {
		/*
		 * Non-last segment.  In this case, the assembly function
		 * requires that the length be a multiple of 16 (AES_BLOCK_SIZE)
		 * bytes.  The needed buffering of up to 16 bytes is handled by
		 * the skcipher_walk.  Here we just need to round down to a
		 * multiple of 16.
		 */
		nbytes = round_down(nbytes, AES_BLOCK_SIZE);
		aes_gcm_update(key, le_ctr, ghash_acc, walk.src.virt.addr,
			       walk.dst.virt.addr, nbytes, flags);
		le_ctr[0] += nbytes / AES_BLOCK_SIZE;
		kernel_fpu_end();
		err = skcipher_walk_done(&walk, walk.nbytes - nbytes);
		if (err)
			return err;
		kernel_fpu_begin();
	}
	/* Last segment: process all remaining data. */
	aes_gcm_update(key, le_ctr, ghash_acc, walk.src.virt.addr,
		       walk.dst.virt.addr, nbytes, flags);
	/*
	 * The low word of the counter isn't used by the finalize, so there's no
	 * need to increment it here.
	 */

	/* Finalize */
	taglen = crypto_aead_authsize(tfm);
	if (flags & FLAG_ENC) {
		/* Finish computing the auth tag. */
		aes_gcm_enc_final(key, le_ctr, ghash_acc, assoclen,
				  req->cryptlen, flags);

		/* Store the computed auth tag in the dst scatterlist. */
		scatterwalk_map_and_copy(ghash_acc, req->dst, req->assoclen +
					 req->cryptlen, taglen, 1);
	} else {
		unsigned int datalen = req->cryptlen - taglen;
		u8 tag[16];

		/* Get the transmitted auth tag from the src scatterlist. */
		scatterwalk_map_and_copy(tag, req->src, req->assoclen + datalen,
					 taglen, 0);
		/*
		 * Finish computing the auth tag and compare it to the
		 * transmitted one.  The assembly function does the actual tag
		 * comparison.  Here, just check the boolean result.
		 */
		if (!aes_gcm_dec_final(key, le_ctr, ghash_acc, assoclen,
				       datalen, tag, taglen, flags))
			err = -EBADMSG;
	}
	kernel_fpu_end();
	if (nbytes)
		skcipher_walk_done(&walk, 0);
	return err;
}

#define DEFINE_GCM_ALGS(suffix, flags, generic_driver_name, rfc_driver_name,   \
			ctxsize, priority)				       \
									       \
static int gcm_setkey_##suffix(struct crypto_aead *tfm, const u8 *raw_key,     \
			       unsigned int keylen)			       \
{									       \
	return gcm_setkey(tfm, raw_key, keylen, (flags));		       \
}									       \
									       \
static int gcm_encrypt_##suffix(struct aead_request *req)		       \
{									       \
	return gcm_crypt(req, (flags) | FLAG_ENC);			       \
}									       \
									       \
static int gcm_decrypt_##suffix(struct aead_request *req)		       \
{									       \
	return gcm_crypt(req, (flags));					       \
}									       \
									       \
static int rfc4106_setkey_##suffix(struct crypto_aead *tfm, const u8 *raw_key, \
				   unsigned int keylen)			       \
{									       \
	return gcm_setkey(tfm, raw_key, keylen, (flags) | FLAG_RFC4106);       \
}									       \
									       \
static int rfc4106_encrypt_##suffix(struct aead_request *req)		       \
{									       \
	return gcm_crypt(req, (flags) | FLAG_RFC4106 | FLAG_ENC);	       \
}									       \
									       \
static int rfc4106_decrypt_##suffix(struct aead_request *req)		       \
{									       \
	return gcm_crypt(req, (flags) | FLAG_RFC4106);			       \
}									       \
									       \
static struct aead_alg aes_gcm_algs_##suffix[] = { {			       \
	.setkey			= gcm_setkey_##suffix,			       \
	.setauthsize		= generic_gcmaes_set_authsize,		       \
	.encrypt		= gcm_encrypt_##suffix,			       \
	.decrypt		= gcm_decrypt_##suffix,			       \
	.ivsize			= GCM_AES_IV_SIZE,			       \
	.chunksize		= AES_BLOCK_SIZE,			       \
	.maxauthsize		= 16,					       \
	.base = {							       \
		.cra_name		= "__gcm(aes)",			       \
		.cra_driver_name	= "__" generic_driver_name,	       \
		.cra_priority		= (priority),			       \
		.cra_flags		= CRYPTO_ALG_INTERNAL,		       \
		.cra_blocksize		= 1,				       \
		.cra_ctxsize		= (ctxsize),			       \
		.cra_module		= THIS_MODULE,			       \
	},								       \
}, {									       \
	.setkey			= rfc4106_setkey_##suffix,		       \
	.setauthsize		= common_rfc4106_set_authsize,		       \
	.encrypt		= rfc4106_encrypt_##suffix,		       \
	.decrypt		= rfc4106_decrypt_##suffix,		       \
	.ivsize			= GCM_RFC4106_IV_SIZE,			       \
	.chunksize		= AES_BLOCK_SIZE,			       \
	.maxauthsize		= 16,					       \
	.base = {							       \
		.cra_name		= "__rfc4106(gcm(aes))",	       \
		.cra_driver_name	= "__" rfc_driver_name,		       \
		.cra_priority		= (priority),			       \
		.cra_flags		= CRYPTO_ALG_INTERNAL,		       \
		.cra_blocksize		= 1,				       \
		.cra_ctxsize		= (ctxsize),			       \
		.cra_module		= THIS_MODULE,			       \
	},								       \
} };									       \
									       \
static struct simd_aead_alg *aes_gcm_simdalgs_##suffix[2]		       \

/* aes_gcm_algs_aesni */
DEFINE_GCM_ALGS(aesni, /* no flags */ 0,
		"generic-gcm-aesni", "rfc4106-gcm-aesni",
		AES_GCM_KEY_AESNI_SIZE, 400);

/* aes_gcm_algs_aesni_avx */
DEFINE_GCM_ALGS(aesni_avx, FLAG_AVX,
		"generic-gcm-aesni-avx", "rfc4106-gcm-aesni-avx",
		AES_GCM_KEY_AESNI_SIZE, 500);

#if defined(CONFIG_AS_VAES) && defined(CONFIG_AS_VPCLMULQDQ)
/* aes_gcm_algs_vaes_avx10_256 */
DEFINE_GCM_ALGS(vaes_avx10_256, FLAG_AVX10_256,
		"generic-gcm-vaes-avx10_256", "rfc4106-gcm-vaes-avx10_256",
		AES_GCM_KEY_AVX10_SIZE, 700);

/* aes_gcm_algs_vaes_avx10_512 */
DEFINE_GCM_ALGS(vaes_avx10_512, FLAG_AVX10_512,
		"generic-gcm-vaes-avx10_512", "rfc4106-gcm-vaes-avx10_512",
		AES_GCM_KEY_AVX10_SIZE, 800);
#endif /* CONFIG_AS_VAES && CONFIG_AS_VPCLMULQDQ */

/*
 * This is a list of CPU models that are known to suffer from downclocking when
 * zmm registers (512-bit vectors) are used.  On these CPUs, the AES mode
 * implementations with zmm registers won't be used by default.  Implementations
 * with ymm registers (256-bit vectors) will be used by default instead.
 */
static const struct x86_cpu_id zmm_exclusion_list[] = {
	X86_MATCH_VFM(INTEL_SKYLAKE_X,		0),
	X86_MATCH_VFM(INTEL_ICELAKE_X,		0),
	X86_MATCH_VFM(INTEL_ICELAKE_D,		0),
	X86_MATCH_VFM(INTEL_ICELAKE,		0),
	X86_MATCH_VFM(INTEL_ICELAKE_L,		0),
	X86_MATCH_VFM(INTEL_ICELAKE_NNPI,	0),
	X86_MATCH_VFM(INTEL_TIGERLAKE_L,	0),
	X86_MATCH_VFM(INTEL_TIGERLAKE,		0),
	/* Allow Rocket Lake and later, and Sapphire Rapids and later. */
	/* Also allow AMD CPUs (starting with Zen 4, the first with AVX-512). */
	{},
};

static int __init register_avx_algs(void)
{
	int err;

	if (!boot_cpu_has(X86_FEATURE_AVX))
		return 0;
	err = simd_register_skciphers_compat(&aes_xts_alg_aesni_avx, 1,
					     &aes_xts_simdalg_aesni_avx);
	if (err)
		return err;
	err = simd_register_aeads_compat(aes_gcm_algs_aesni_avx,
					 ARRAY_SIZE(aes_gcm_algs_aesni_avx),
					 aes_gcm_simdalgs_aesni_avx);
	if (err)
		return err;
#if defined(CONFIG_AS_VAES) && defined(CONFIG_AS_VPCLMULQDQ)
	if (!boot_cpu_has(X86_FEATURE_AVX2) ||
	    !boot_cpu_has(X86_FEATURE_VAES) ||
	    !boot_cpu_has(X86_FEATURE_VPCLMULQDQ) ||
	    !boot_cpu_has(X86_FEATURE_PCLMULQDQ) ||
	    !cpu_has_xfeatures(XFEATURE_MASK_SSE | XFEATURE_MASK_YMM, NULL))
		return 0;
	err = simd_register_skciphers_compat(&aes_xts_alg_vaes_avx2, 1,
					     &aes_xts_simdalg_vaes_avx2);
	if (err)
		return err;

	if (!boot_cpu_has(X86_FEATURE_AVX512BW) ||
	    !boot_cpu_has(X86_FEATURE_AVX512VL) ||
	    !boot_cpu_has(X86_FEATURE_BMI2) ||
	    !cpu_has_xfeatures(XFEATURE_MASK_SSE | XFEATURE_MASK_YMM |
			       XFEATURE_MASK_AVX512, NULL))
		return 0;

	err = simd_register_skciphers_compat(&aes_xts_alg_vaes_avx10_256, 1,
					     &aes_xts_simdalg_vaes_avx10_256);
	if (err)
		return err;
	err = simd_register_aeads_compat(aes_gcm_algs_vaes_avx10_256,
					 ARRAY_SIZE(aes_gcm_algs_vaes_avx10_256),
					 aes_gcm_simdalgs_vaes_avx10_256);
	if (err)
		return err;

	if (x86_match_cpu(zmm_exclusion_list)) {
		int i;

		aes_xts_alg_vaes_avx10_512.base.cra_priority = 1;
		for (i = 0; i < ARRAY_SIZE(aes_gcm_algs_vaes_avx10_512); i++)
			aes_gcm_algs_vaes_avx10_512[i].base.cra_priority = 1;
	}

	err = simd_register_skciphers_compat(&aes_xts_alg_vaes_avx10_512, 1,
					     &aes_xts_simdalg_vaes_avx10_512);
	if (err)
		return err;
	err = simd_register_aeads_compat(aes_gcm_algs_vaes_avx10_512,
					 ARRAY_SIZE(aes_gcm_algs_vaes_avx10_512),
					 aes_gcm_simdalgs_vaes_avx10_512);
	if (err)
		return err;
#endif /* CONFIG_AS_VAES && CONFIG_AS_VPCLMULQDQ */
	return 0;
}

static void unregister_avx_algs(void)
{
	if (aes_xts_simdalg_aesni_avx)
		simd_unregister_skciphers(&aes_xts_alg_aesni_avx, 1,
					  &aes_xts_simdalg_aesni_avx);
	if (aes_gcm_simdalgs_aesni_avx[0])
		simd_unregister_aeads(aes_gcm_algs_aesni_avx,
				      ARRAY_SIZE(aes_gcm_algs_aesni_avx),
				      aes_gcm_simdalgs_aesni_avx);
#if defined(CONFIG_AS_VAES) && defined(CONFIG_AS_VPCLMULQDQ)
	if (aes_xts_simdalg_vaes_avx2)
		simd_unregister_skciphers(&aes_xts_alg_vaes_avx2, 1,
					  &aes_xts_simdalg_vaes_avx2);
	if (aes_xts_simdalg_vaes_avx10_256)
		simd_unregister_skciphers(&aes_xts_alg_vaes_avx10_256, 1,
					  &aes_xts_simdalg_vaes_avx10_256);
	if (aes_gcm_simdalgs_vaes_avx10_256[0])
		simd_unregister_aeads(aes_gcm_algs_vaes_avx10_256,
				      ARRAY_SIZE(aes_gcm_algs_vaes_avx10_256),
				      aes_gcm_simdalgs_vaes_avx10_256);
	if (aes_xts_simdalg_vaes_avx10_512)
		simd_unregister_skciphers(&aes_xts_alg_vaes_avx10_512, 1,
					  &aes_xts_simdalg_vaes_avx10_512);
	if (aes_gcm_simdalgs_vaes_avx10_512[0])
		simd_unregister_aeads(aes_gcm_algs_vaes_avx10_512,
				      ARRAY_SIZE(aes_gcm_algs_vaes_avx10_512),
				      aes_gcm_simdalgs_vaes_avx10_512);
#endif
}
#else /* CONFIG_X86_64 */
static struct aead_alg aes_gcm_algs_aesni[0];
static struct simd_aead_alg *aes_gcm_simdalgs_aesni[0];

static int __init register_avx_algs(void)
{
	return 0;
}

static void unregister_avx_algs(void)
{
}
#endif /* !CONFIG_X86_64 */

static const struct x86_cpu_id aesni_cpu_id[] = {
	X86_MATCH_FEATURE(X86_FEATURE_AES, NULL),
	{}
};
MODULE_DEVICE_TABLE(x86cpu, aesni_cpu_id);

static int __init aesni_init(void)
{
	int err;

	if (!x86_match_cpu(aesni_cpu_id))
		return -ENODEV;
#ifdef CONFIG_X86_64
	if (boot_cpu_has(X86_FEATURE_AVX)) {
		/* optimize performance of ctr mode encryption transform */
		static_call_update(aesni_ctr_enc_tfm, aesni_ctr_enc_avx_tfm);
		pr_info("AES CTR mode by8 optimization enabled\n");
	}
#endif /* CONFIG_X86_64 */

	err = crypto_register_alg(&aesni_cipher_alg);
	if (err)
		return err;

	err = simd_register_skciphers_compat(aesni_skciphers,
					     ARRAY_SIZE(aesni_skciphers),
					     aesni_simd_skciphers);
	if (err)
		goto unregister_cipher;

	err = simd_register_aeads_compat(aes_gcm_algs_aesni,
					 ARRAY_SIZE(aes_gcm_algs_aesni),
					 aes_gcm_simdalgs_aesni);
	if (err)
		goto unregister_skciphers;

#ifdef CONFIG_X86_64
	if (boot_cpu_has(X86_FEATURE_AVX))
		err = simd_register_skciphers_compat(&aesni_xctr, 1,
						     &aesni_simd_xctr);
	if (err)
		goto unregister_aeads;
#endif /* CONFIG_X86_64 */

	err = register_avx_algs();
	if (err)
		goto unregister_avx;

	return 0;

unregister_avx:
	unregister_avx_algs();
#ifdef CONFIG_X86_64
	if (aesni_simd_xctr)
		simd_unregister_skciphers(&aesni_xctr, 1, &aesni_simd_xctr);
unregister_aeads:
#endif /* CONFIG_X86_64 */
	simd_unregister_aeads(aes_gcm_algs_aesni,
			      ARRAY_SIZE(aes_gcm_algs_aesni),
			      aes_gcm_simdalgs_aesni);
unregister_skciphers:
	simd_unregister_skciphers(aesni_skciphers, ARRAY_SIZE(aesni_skciphers),
				  aesni_simd_skciphers);
unregister_cipher:
	crypto_unregister_alg(&aesni_cipher_alg);
	return err;
}

static void __exit aesni_exit(void)
{
	simd_unregister_aeads(aes_gcm_algs_aesni,
			      ARRAY_SIZE(aes_gcm_algs_aesni),
			      aes_gcm_simdalgs_aesni);
	simd_unregister_skciphers(aesni_skciphers, ARRAY_SIZE(aesni_skciphers),
				  aesni_simd_skciphers);
	crypto_unregister_alg(&aesni_cipher_alg);
#ifdef CONFIG_X86_64
	if (boot_cpu_has(X86_FEATURE_AVX))
		simd_unregister_skciphers(&aesni_xctr, 1, &aesni_simd_xctr);
#endif /* CONFIG_X86_64 */
	unregister_avx_algs();
}

late_initcall(aesni_init);
module_exit(aesni_exit);

MODULE_DESCRIPTION("AES cipher and modes, optimized with AES-NI or VAES instructions");
MODULE_LICENSE("GPL");
MODULE_ALIAS_CRYPTO("aes");
