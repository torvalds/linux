/*
 * Support for Intel AES-NI instructions. This file contains glue
 * code, the real AES implementation is in intel-aes_asm.S.
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
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/hardirq.h>
#include <linux/types.h>
#include <linux/crypto.h>
#include <linux/module.h>
#include <linux/err.h>
#include <crypto/algapi.h>
#include <crypto/aes.h>
#include <crypto/cryptd.h>
#include <crypto/ctr.h>
#include <asm/cpu_device_id.h>
#include <asm/i387.h>
#include <asm/aes.h>
#include <crypto/scatterwalk.h>
#include <crypto/internal/aead.h>
#include <linux/workqueue.h>
#include <linux/spinlock.h>

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

/* This data is stored at the end of the crypto_tfm struct.
 * It's a type of per "session" data storage location.
 * This needs to be 16 byte aligned.
 */
struct aesni_rfc4106_gcm_ctx {
	u8 hash_subkey[16];
	struct crypto_aes_ctx aes_key_expanded;
	u8 nonce[4];
	struct cryptd_aead *cryptd_tfm;
};

struct aesni_gcm_set_hash_subkey_result {
	int err;
	struct completion completion;
};

struct aesni_hash_subkey_req_data {
	u8 iv[16];
	struct aesni_gcm_set_hash_subkey_result result;
	struct scatterlist sg;
};

#define AESNI_ALIGN	(16)
#define AES_BLOCK_MASK	(~(AES_BLOCK_SIZE-1))
#define RFC4106_HASH_SUBKEY_SIZE 16

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

int crypto_fpu_init(void);
void crypto_fpu_exit(void);

#ifdef CONFIG_X86_64
asmlinkage void aesni_ctr_enc(struct crypto_aes_ctx *ctx, u8 *out,
			      const u8 *in, unsigned int len, u8 *iv);

/* asmlinkage void aesni_gcm_enc()
 * void *ctx,  AES Key schedule. Starts on a 16 byte boundary.
 * u8 *out, Ciphertext output. Encrypt in-place is allowed.
 * const u8 *in, Plaintext input
 * unsigned long plaintext_len, Length of data in bytes for encryption.
 * u8 *iv, Pre-counter block j0: 4 byte salt (from Security Association)
 *         concatenated with 8 byte Initialisation Vector (from IPSec ESP
 *         Payload) concatenated with 0x00000001. 16-byte aligned pointer.
 * u8 *hash_subkey, the Hash sub key input. Data starts on a 16-byte boundary.
 * const u8 *aad, Additional Authentication Data (AAD)
 * unsigned long aad_len, Length of AAD in bytes. With RFC4106 this
 *          is going to be 8 or 12 bytes
 * u8 *auth_tag, Authenticated Tag output.
 * unsigned long auth_tag_len), Authenticated Tag Length in bytes.
 *          Valid values are 16 (most likely), 12 or 8.
 */
asmlinkage void aesni_gcm_enc(void *ctx, u8 *out,
			const u8 *in, unsigned long plaintext_len, u8 *iv,
			u8 *hash_subkey, const u8 *aad, unsigned long aad_len,
			u8 *auth_tag, unsigned long auth_tag_len);

/* asmlinkage void aesni_gcm_dec()
 * void *ctx, AES Key schedule. Starts on a 16 byte boundary.
 * u8 *out, Plaintext output. Decrypt in-place is allowed.
 * const u8 *in, Ciphertext input
 * unsigned long ciphertext_len, Length of data in bytes for decryption.
 * u8 *iv, Pre-counter block j0: 4 byte salt (from Security Association)
 *         concatenated with 8 byte Initialisation Vector (from IPSec ESP
 *         Payload) concatenated with 0x00000001. 16-byte aligned pointer.
 * u8 *hash_subkey, the Hash sub key input. Data starts on a 16-byte boundary.
 * const u8 *aad, Additional Authentication Data (AAD)
 * unsigned long aad_len, Length of AAD in bytes. With RFC4106 this is going
 * to be 8 or 12 bytes
 * u8 *auth_tag, Authenticated Tag output.
 * unsigned long auth_tag_len) Authenticated Tag Length in bytes.
 * Valid values are 16 (most likely), 12 or 8.
 */
asmlinkage void aesni_gcm_dec(void *ctx, u8 *out,
			const u8 *in, unsigned long ciphertext_len, u8 *iv,
			u8 *hash_subkey, const u8 *aad, unsigned long aad_len,
			u8 *auth_tag, unsigned long auth_tag_len);

static inline struct
aesni_rfc4106_gcm_ctx *aesni_rfc4106_gcm_ctx_get(struct crypto_aead *tfm)
{
	return
		(struct aesni_rfc4106_gcm_ctx *)
		PTR_ALIGN((u8 *)
		crypto_tfm_ctx(crypto_aead_tfm(tfm)), AESNI_ALIGN);
}
#endif

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

#ifdef CONFIG_X86_64
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
#endif

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

static int ablk_cbc_init(struct crypto_tfm *tfm)
{
	struct cryptd_ablkcipher *cryptd_tfm;

	cryptd_tfm = cryptd_alloc_ablkcipher("__driver-cbc-aes-aesni", 0, 0);
	if (IS_ERR(cryptd_tfm))
		return PTR_ERR(cryptd_tfm);
	ablk_init_common(tfm, cryptd_tfm);
	return 0;
}

#ifdef CONFIG_X86_64
static int ablk_ctr_init(struct crypto_tfm *tfm)
{
	struct cryptd_ablkcipher *cryptd_tfm;

	cryptd_tfm = cryptd_alloc_ablkcipher("__driver-ctr-aes-aesni", 0, 0);
	if (IS_ERR(cryptd_tfm))
		return PTR_ERR(cryptd_tfm);
	ablk_init_common(tfm, cryptd_tfm);
	return 0;
}

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
#endif
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
#endif

#ifdef CONFIG_X86_64
static int rfc4106_init(struct crypto_tfm *tfm)
{
	struct cryptd_aead *cryptd_tfm;
	struct aesni_rfc4106_gcm_ctx *ctx = (struct aesni_rfc4106_gcm_ctx *)
		PTR_ALIGN((u8 *)crypto_tfm_ctx(tfm), AESNI_ALIGN);
	struct crypto_aead *cryptd_child;
	struct aesni_rfc4106_gcm_ctx *child_ctx;
	cryptd_tfm = cryptd_alloc_aead("__driver-gcm-aes-aesni", 0, 0);
	if (IS_ERR(cryptd_tfm))
		return PTR_ERR(cryptd_tfm);

	cryptd_child = cryptd_aead_child(cryptd_tfm);
	child_ctx = aesni_rfc4106_gcm_ctx_get(cryptd_child);
	memcpy(child_ctx, ctx, sizeof(*ctx));
	ctx->cryptd_tfm = cryptd_tfm;
	tfm->crt_aead.reqsize = sizeof(struct aead_request)
		+ crypto_aead_reqsize(&cryptd_tfm->base);
	return 0;
}

static void rfc4106_exit(struct crypto_tfm *tfm)
{
	struct aesni_rfc4106_gcm_ctx *ctx =
		(struct aesni_rfc4106_gcm_ctx *)
		PTR_ALIGN((u8 *)crypto_tfm_ctx(tfm), AESNI_ALIGN);
	if (!IS_ERR(ctx->cryptd_tfm))
		cryptd_free_aead(ctx->cryptd_tfm);
	return;
}

static void
rfc4106_set_hash_subkey_done(struct crypto_async_request *req, int err)
{
	struct aesni_gcm_set_hash_subkey_result *result = req->data;

	if (err == -EINPROGRESS)
		return;
	result->err = err;
	complete(&result->completion);
}

static int
rfc4106_set_hash_subkey(u8 *hash_subkey, const u8 *key, unsigned int key_len)
{
	struct crypto_ablkcipher *ctr_tfm;
	struct ablkcipher_request *req;
	int ret = -EINVAL;
	struct aesni_hash_subkey_req_data *req_data;

	ctr_tfm = crypto_alloc_ablkcipher("ctr(aes)", 0, 0);
	if (IS_ERR(ctr_tfm))
		return PTR_ERR(ctr_tfm);

	crypto_ablkcipher_clear_flags(ctr_tfm, ~0);

	ret = crypto_ablkcipher_setkey(ctr_tfm, key, key_len);
	if (ret)
		goto out_free_ablkcipher;

	ret = -ENOMEM;
	req = ablkcipher_request_alloc(ctr_tfm, GFP_KERNEL);
	if (!req)
		goto out_free_ablkcipher;

	req_data = kmalloc(sizeof(*req_data), GFP_KERNEL);
	if (!req_data)
		goto out_free_request;

	memset(req_data->iv, 0, sizeof(req_data->iv));

	/* Clear the data in the hash sub key container to zero.*/
	/* We want to cipher all zeros to create the hash sub key. */
	memset(hash_subkey, 0, RFC4106_HASH_SUBKEY_SIZE);

	init_completion(&req_data->result.completion);
	sg_init_one(&req_data->sg, hash_subkey, RFC4106_HASH_SUBKEY_SIZE);
	ablkcipher_request_set_tfm(req, ctr_tfm);
	ablkcipher_request_set_callback(req, CRYPTO_TFM_REQ_MAY_SLEEP |
					CRYPTO_TFM_REQ_MAY_BACKLOG,
					rfc4106_set_hash_subkey_done,
					&req_data->result);

	ablkcipher_request_set_crypt(req, &req_data->sg,
		&req_data->sg, RFC4106_HASH_SUBKEY_SIZE, req_data->iv);

	ret = crypto_ablkcipher_encrypt(req);
	if (ret == -EINPROGRESS || ret == -EBUSY) {
		ret = wait_for_completion_interruptible
			(&req_data->result.completion);
		if (!ret)
			ret = req_data->result.err;
	}
	kfree(req_data);
out_free_request:
	ablkcipher_request_free(req);
out_free_ablkcipher:
	crypto_free_ablkcipher(ctr_tfm);
	return ret;
}

static int rfc4106_set_key(struct crypto_aead *parent, const u8 *key,
						   unsigned int key_len)
{
	int ret = 0;
	struct crypto_tfm *tfm = crypto_aead_tfm(parent);
	struct aesni_rfc4106_gcm_ctx *ctx = aesni_rfc4106_gcm_ctx_get(parent);
	struct crypto_aead *cryptd_child = cryptd_aead_child(ctx->cryptd_tfm);
	struct aesni_rfc4106_gcm_ctx *child_ctx =
                                 aesni_rfc4106_gcm_ctx_get(cryptd_child);
	u8 *new_key_mem = NULL;

	if (key_len < 4) {
		crypto_tfm_set_flags(tfm, CRYPTO_TFM_RES_BAD_KEY_LEN);
		return -EINVAL;
	}
	/*Account for 4 byte nonce at the end.*/
	key_len -= 4;
	if (key_len != AES_KEYSIZE_128) {
		crypto_tfm_set_flags(tfm, CRYPTO_TFM_RES_BAD_KEY_LEN);
		return -EINVAL;
	}

	memcpy(ctx->nonce, key + key_len, sizeof(ctx->nonce));
	/*This must be on a 16 byte boundary!*/
	if ((unsigned long)(&(ctx->aes_key_expanded.key_enc[0])) % AESNI_ALIGN)
		return -EINVAL;

	if ((unsigned long)key % AESNI_ALIGN) {
		/*key is not aligned: use an auxuliar aligned pointer*/
		new_key_mem = kmalloc(key_len+AESNI_ALIGN, GFP_KERNEL);
		if (!new_key_mem)
			return -ENOMEM;

		new_key_mem = PTR_ALIGN(new_key_mem, AESNI_ALIGN);
		memcpy(new_key_mem, key, key_len);
		key = new_key_mem;
	}

	if (!irq_fpu_usable())
		ret = crypto_aes_expand_key(&(ctx->aes_key_expanded),
		key, key_len);
	else {
		kernel_fpu_begin();
		ret = aesni_set_key(&(ctx->aes_key_expanded), key, key_len);
		kernel_fpu_end();
	}
	/*This must be on a 16 byte boundary!*/
	if ((unsigned long)(&(ctx->hash_subkey[0])) % AESNI_ALIGN) {
		ret = -EINVAL;
		goto exit;
	}
	ret = rfc4106_set_hash_subkey(ctx->hash_subkey, key, key_len);
	memcpy(child_ctx, ctx, sizeof(*ctx));
exit:
	kfree(new_key_mem);
	return ret;
}

/* This is the Integrity Check Value (aka the authentication tag length and can
 * be 8, 12 or 16 bytes long. */
static int rfc4106_set_authsize(struct crypto_aead *parent,
				unsigned int authsize)
{
	struct aesni_rfc4106_gcm_ctx *ctx = aesni_rfc4106_gcm_ctx_get(parent);
	struct crypto_aead *cryptd_child = cryptd_aead_child(ctx->cryptd_tfm);

	switch (authsize) {
	case 8:
	case 12:
	case 16:
		break;
	default:
		return -EINVAL;
	}
	crypto_aead_crt(parent)->authsize = authsize;
	crypto_aead_crt(cryptd_child)->authsize = authsize;
	return 0;
}

static int rfc4106_encrypt(struct aead_request *req)
{
	int ret;
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	struct aesni_rfc4106_gcm_ctx *ctx = aesni_rfc4106_gcm_ctx_get(tfm);

	if (!irq_fpu_usable()) {
		struct aead_request *cryptd_req =
			(struct aead_request *) aead_request_ctx(req);
		memcpy(cryptd_req, req, sizeof(*req));
		aead_request_set_tfm(cryptd_req, &ctx->cryptd_tfm->base);
		return crypto_aead_encrypt(cryptd_req);
	} else {
		struct crypto_aead *cryptd_child = cryptd_aead_child(ctx->cryptd_tfm);
		kernel_fpu_begin();
		ret = cryptd_child->base.crt_aead.encrypt(req);
		kernel_fpu_end();
		return ret;
	}
}

static int rfc4106_decrypt(struct aead_request *req)
{
	int ret;
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	struct aesni_rfc4106_gcm_ctx *ctx = aesni_rfc4106_gcm_ctx_get(tfm);

	if (!irq_fpu_usable()) {
		struct aead_request *cryptd_req =
			(struct aead_request *) aead_request_ctx(req);
		memcpy(cryptd_req, req, sizeof(*req));
		aead_request_set_tfm(cryptd_req, &ctx->cryptd_tfm->base);
		return crypto_aead_decrypt(cryptd_req);
	} else {
		struct crypto_aead *cryptd_child = cryptd_aead_child(ctx->cryptd_tfm);
		kernel_fpu_begin();
		ret = cryptd_child->base.crt_aead.decrypt(req);
		kernel_fpu_end();
		return ret;
	}
}

static int __driver_rfc4106_encrypt(struct aead_request *req)
{
	u8 one_entry_in_sg = 0;
	u8 *src, *dst, *assoc;
	__be32 counter = cpu_to_be32(1);
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	struct aesni_rfc4106_gcm_ctx *ctx = aesni_rfc4106_gcm_ctx_get(tfm);
	void *aes_ctx = &(ctx->aes_key_expanded);
	unsigned long auth_tag_len = crypto_aead_authsize(tfm);
	u8 iv_tab[16+AESNI_ALIGN];
	u8* iv = (u8 *) PTR_ALIGN((u8 *)iv_tab, AESNI_ALIGN);
	struct scatter_walk src_sg_walk;
	struct scatter_walk assoc_sg_walk;
	struct scatter_walk dst_sg_walk;
	unsigned int i;

	/* Assuming we are supporting rfc4106 64-bit extended */
	/* sequence numbers We need to have the AAD length equal */
	/* to 8 or 12 bytes */
	if (unlikely(req->assoclen != 8 && req->assoclen != 12))
		return -EINVAL;
	/* IV below built */
	for (i = 0; i < 4; i++)
		*(iv+i) = ctx->nonce[i];
	for (i = 0; i < 8; i++)
		*(iv+4+i) = req->iv[i];
	*((__be32 *)(iv+12)) = counter;

	if ((sg_is_last(req->src)) && (sg_is_last(req->assoc))) {
		one_entry_in_sg = 1;
		scatterwalk_start(&src_sg_walk, req->src);
		scatterwalk_start(&assoc_sg_walk, req->assoc);
		src = scatterwalk_map(&src_sg_walk);
		assoc = scatterwalk_map(&assoc_sg_walk);
		dst = src;
		if (unlikely(req->src != req->dst)) {
			scatterwalk_start(&dst_sg_walk, req->dst);
			dst = scatterwalk_map(&dst_sg_walk);
		}

	} else {
		/* Allocate memory for src, dst, assoc */
		src = kmalloc(req->cryptlen + auth_tag_len + req->assoclen,
			GFP_ATOMIC);
		if (unlikely(!src))
			return -ENOMEM;
		assoc = (src + req->cryptlen + auth_tag_len);
		scatterwalk_map_and_copy(src, req->src, 0, req->cryptlen, 0);
		scatterwalk_map_and_copy(assoc, req->assoc, 0,
					req->assoclen, 0);
		dst = src;
	}

	aesni_gcm_enc(aes_ctx, dst, src, (unsigned long)req->cryptlen, iv,
		ctx->hash_subkey, assoc, (unsigned long)req->assoclen, dst
		+ ((unsigned long)req->cryptlen), auth_tag_len);

	/* The authTag (aka the Integrity Check Value) needs to be written
	 * back to the packet. */
	if (one_entry_in_sg) {
		if (unlikely(req->src != req->dst)) {
			scatterwalk_unmap(dst);
			scatterwalk_done(&dst_sg_walk, 0, 0);
		}
		scatterwalk_unmap(src);
		scatterwalk_unmap(assoc);
		scatterwalk_done(&src_sg_walk, 0, 0);
		scatterwalk_done(&assoc_sg_walk, 0, 0);
	} else {
		scatterwalk_map_and_copy(dst, req->dst, 0,
			req->cryptlen + auth_tag_len, 1);
		kfree(src);
	}
	return 0;
}

static int __driver_rfc4106_decrypt(struct aead_request *req)
{
	u8 one_entry_in_sg = 0;
	u8 *src, *dst, *assoc;
	unsigned long tempCipherLen = 0;
	__be32 counter = cpu_to_be32(1);
	int retval = 0;
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	struct aesni_rfc4106_gcm_ctx *ctx = aesni_rfc4106_gcm_ctx_get(tfm);
	void *aes_ctx = &(ctx->aes_key_expanded);
	unsigned long auth_tag_len = crypto_aead_authsize(tfm);
	u8 iv_and_authTag[32+AESNI_ALIGN];
	u8 *iv = (u8 *) PTR_ALIGN((u8 *)iv_and_authTag, AESNI_ALIGN);
	u8 *authTag = iv + 16;
	struct scatter_walk src_sg_walk;
	struct scatter_walk assoc_sg_walk;
	struct scatter_walk dst_sg_walk;
	unsigned int i;

	if (unlikely((req->cryptlen < auth_tag_len) ||
		(req->assoclen != 8 && req->assoclen != 12)))
		return -EINVAL;
	/* Assuming we are supporting rfc4106 64-bit extended */
	/* sequence numbers We need to have the AAD length */
	/* equal to 8 or 12 bytes */

	tempCipherLen = (unsigned long)(req->cryptlen - auth_tag_len);
	/* IV below built */
	for (i = 0; i < 4; i++)
		*(iv+i) = ctx->nonce[i];
	for (i = 0; i < 8; i++)
		*(iv+4+i) = req->iv[i];
	*((__be32 *)(iv+12)) = counter;

	if ((sg_is_last(req->src)) && (sg_is_last(req->assoc))) {
		one_entry_in_sg = 1;
		scatterwalk_start(&src_sg_walk, req->src);
		scatterwalk_start(&assoc_sg_walk, req->assoc);
		src = scatterwalk_map(&src_sg_walk);
		assoc = scatterwalk_map(&assoc_sg_walk);
		dst = src;
		if (unlikely(req->src != req->dst)) {
			scatterwalk_start(&dst_sg_walk, req->dst);
			dst = scatterwalk_map(&dst_sg_walk);
		}

	} else {
		/* Allocate memory for src, dst, assoc */
		src = kmalloc(req->cryptlen + req->assoclen, GFP_ATOMIC);
		if (!src)
			return -ENOMEM;
		assoc = (src + req->cryptlen + auth_tag_len);
		scatterwalk_map_and_copy(src, req->src, 0, req->cryptlen, 0);
		scatterwalk_map_and_copy(assoc, req->assoc, 0,
			req->assoclen, 0);
		dst = src;
	}

	aesni_gcm_dec(aes_ctx, dst, src, tempCipherLen, iv,
		ctx->hash_subkey, assoc, (unsigned long)req->assoclen,
		authTag, auth_tag_len);

	/* Compare generated tag with passed in tag. */
	retval = memcmp(src + tempCipherLen, authTag, auth_tag_len) ?
		-EBADMSG : 0;

	if (one_entry_in_sg) {
		if (unlikely(req->src != req->dst)) {
			scatterwalk_unmap(dst);
			scatterwalk_done(&dst_sg_walk, 0, 0);
		}
		scatterwalk_unmap(src);
		scatterwalk_unmap(assoc);
		scatterwalk_done(&src_sg_walk, 0, 0);
		scatterwalk_done(&assoc_sg_walk, 0, 0);
	} else {
		scatterwalk_map_and_copy(dst, req->dst, 0, req->cryptlen, 1);
		kfree(src);
	}
	return retval;
}
#endif

static struct crypto_alg aesni_algs[] = { {
	.cra_name		= "aes",
	.cra_driver_name	= "aes-aesni",
	.cra_priority		= 300,
	.cra_flags		= CRYPTO_ALG_TYPE_CIPHER,
	.cra_blocksize		= AES_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct crypto_aes_ctx) +
				  AESNI_ALIGN - 1,
	.cra_alignmask		= 0,
	.cra_module		= THIS_MODULE,
	.cra_u	= {
		.cipher	= {
			.cia_min_keysize	= AES_MIN_KEY_SIZE,
			.cia_max_keysize	= AES_MAX_KEY_SIZE,
			.cia_setkey		= aes_set_key,
			.cia_encrypt		= aes_encrypt,
			.cia_decrypt		= aes_decrypt
		}
	}
}, {
	.cra_name		= "__aes-aesni",
	.cra_driver_name	= "__driver-aes-aesni",
	.cra_priority		= 0,
	.cra_flags		= CRYPTO_ALG_TYPE_CIPHER,
	.cra_blocksize		= AES_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct crypto_aes_ctx) +
				  AESNI_ALIGN - 1,
	.cra_alignmask		= 0,
	.cra_module		= THIS_MODULE,
	.cra_u	= {
		.cipher	= {
			.cia_min_keysize	= AES_MIN_KEY_SIZE,
			.cia_max_keysize	= AES_MAX_KEY_SIZE,
			.cia_setkey		= aes_set_key,
			.cia_encrypt		= __aes_encrypt,
			.cia_decrypt		= __aes_decrypt
		}
	}
}, {
	.cra_name		= "__ecb-aes-aesni",
	.cra_driver_name	= "__driver-ecb-aes-aesni",
	.cra_priority		= 0,
	.cra_flags		= CRYPTO_ALG_TYPE_BLKCIPHER,
	.cra_blocksize		= AES_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct crypto_aes_ctx) +
				  AESNI_ALIGN - 1,
	.cra_alignmask		= 0,
	.cra_type		= &crypto_blkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_u = {
		.blkcipher = {
			.min_keysize	= AES_MIN_KEY_SIZE,
			.max_keysize	= AES_MAX_KEY_SIZE,
			.setkey		= aes_set_key,
			.encrypt	= ecb_encrypt,
			.decrypt	= ecb_decrypt,
		},
	},
}, {
	.cra_name		= "__cbc-aes-aesni",
	.cra_driver_name	= "__driver-cbc-aes-aesni",
	.cra_priority		= 0,
	.cra_flags		= CRYPTO_ALG_TYPE_BLKCIPHER,
	.cra_blocksize		= AES_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct crypto_aes_ctx) +
				  AESNI_ALIGN - 1,
	.cra_alignmask		= 0,
	.cra_type		= &crypto_blkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_u = {
		.blkcipher = {
			.min_keysize	= AES_MIN_KEY_SIZE,
			.max_keysize	= AES_MAX_KEY_SIZE,
			.setkey		= aes_set_key,
			.encrypt	= cbc_encrypt,
			.decrypt	= cbc_decrypt,
		},
	},
}, {
	.cra_name		= "ecb(aes)",
	.cra_driver_name	= "ecb-aes-aesni",
	.cra_priority		= 400,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,
	.cra_blocksize		= AES_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct async_aes_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
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
}, {
	.cra_name		= "cbc(aes)",
	.cra_driver_name	= "cbc-aes-aesni",
	.cra_priority		= 400,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,
	.cra_blocksize		= AES_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct async_aes_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
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
#ifdef CONFIG_X86_64
}, {
	.cra_name		= "__ctr-aes-aesni",
	.cra_driver_name	= "__driver-ctr-aes-aesni",
	.cra_priority		= 0,
	.cra_flags		= CRYPTO_ALG_TYPE_BLKCIPHER,
	.cra_blocksize		= 1,
	.cra_ctxsize		= sizeof(struct crypto_aes_ctx) +
				  AESNI_ALIGN - 1,
	.cra_alignmask		= 0,
	.cra_type		= &crypto_blkcipher_type,
	.cra_module		= THIS_MODULE,
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
}, {
	.cra_name		= "ctr(aes)",
	.cra_driver_name	= "ctr-aes-aesni",
	.cra_priority		= 400,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,
	.cra_blocksize		= 1,
	.cra_ctxsize		= sizeof(struct async_aes_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
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
}, {
	.cra_name		= "__gcm-aes-aesni",
	.cra_driver_name	= "__driver-gcm-aes-aesni",
	.cra_priority		= 0,
	.cra_flags		= CRYPTO_ALG_TYPE_AEAD,
	.cra_blocksize		= 1,
	.cra_ctxsize		= sizeof(struct aesni_rfc4106_gcm_ctx) +
				  AESNI_ALIGN,
	.cra_alignmask		= 0,
	.cra_type		= &crypto_aead_type,
	.cra_module		= THIS_MODULE,
	.cra_u = {
		.aead = {
			.encrypt	= __driver_rfc4106_encrypt,
			.decrypt	= __driver_rfc4106_decrypt,
		},
	},
}, {
	.cra_name		= "rfc4106(gcm(aes))",
	.cra_driver_name	= "rfc4106-gcm-aesni",
	.cra_priority		= 400,
	.cra_flags		= CRYPTO_ALG_TYPE_AEAD | CRYPTO_ALG_ASYNC,
	.cra_blocksize		= 1,
	.cra_ctxsize		= sizeof(struct aesni_rfc4106_gcm_ctx) +
				  AESNI_ALIGN,
	.cra_alignmask		= 0,
	.cra_type		= &crypto_nivaead_type,
	.cra_module		= THIS_MODULE,
	.cra_init		= rfc4106_init,
	.cra_exit		= rfc4106_exit,
	.cra_u = {
		.aead = {
			.setkey		= rfc4106_set_key,
			.setauthsize	= rfc4106_set_authsize,
			.encrypt	= rfc4106_encrypt,
			.decrypt	= rfc4106_decrypt,
			.geniv		= "seqiv",
			.ivsize		= 8,
			.maxauthsize	= 16,
		},
	},
#ifdef HAS_CTR
}, {
	.cra_name		= "rfc3686(ctr(aes))",
	.cra_driver_name	= "rfc3686-ctr-aes-aesni",
	.cra_priority		= 400,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,
	.cra_blocksize		= 1,
	.cra_ctxsize		= sizeof(struct async_aes_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
	.cra_init		= ablk_rfc3686_ctr_init,
	.cra_exit		= ablk_exit,
	.cra_u = {
		.ablkcipher = {
			.min_keysize = AES_MIN_KEY_SIZE +
				       CTR_RFC3686_NONCE_SIZE,
			.max_keysize = AES_MAX_KEY_SIZE +
				       CTR_RFC3686_NONCE_SIZE,
			.ivsize	     = CTR_RFC3686_IV_SIZE,
			.setkey	     = ablk_set_key,
			.encrypt     = ablk_encrypt,
			.decrypt     = ablk_decrypt,
			.geniv	     = "seqiv",
		},
	},
#endif
#endif
#ifdef HAS_LRW
}, {
	.cra_name		= "lrw(aes)",
	.cra_driver_name	= "lrw-aes-aesni",
	.cra_priority		= 400,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,
	.cra_blocksize		= AES_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct async_aes_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
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
#endif
#ifdef HAS_PCBC
}, {
	.cra_name		= "pcbc(aes)",
	.cra_driver_name	= "pcbc-aes-aesni",
	.cra_priority		= 400,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,
	.cra_blocksize		= AES_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct async_aes_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
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
#endif
#ifdef HAS_XTS
}, {
	.cra_name		= "xts(aes)",
	.cra_driver_name	= "xts-aes-aesni",
	.cra_priority		= 400,
	.cra_flags		= CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,
	.cra_blocksize		= AES_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct async_aes_ctx),
	.cra_alignmask		= 0,
	.cra_type		= &crypto_ablkcipher_type,
	.cra_module		= THIS_MODULE,
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
#endif
} };


static const struct x86_cpu_id aesni_cpu_id[] = {
	X86_FEATURE_MATCH(X86_FEATURE_AES),
	{}
};
MODULE_DEVICE_TABLE(x86cpu, aesni_cpu_id);

static int __init aesni_init(void)
{
	int err, i;

	if (!x86_match_cpu(aesni_cpu_id))
		return -ENODEV;

	err = crypto_fpu_init();
	if (err)
		return err;

	for (i = 0; i < ARRAY_SIZE(aesni_algs); i++)
		INIT_LIST_HEAD(&aesni_algs[i].cra_list);

	return crypto_register_algs(aesni_algs, ARRAY_SIZE(aesni_algs));
}

static void __exit aesni_exit(void)
{
	crypto_unregister_algs(aesni_algs, ARRAY_SIZE(aesni_algs));

	crypto_fpu_exit();
}

module_init(aesni_init);
module_exit(aesni_exit);

MODULE_DESCRIPTION("Rijndael (AES) Cipher Algorithm, Intel AES-NI instructions optimized");
MODULE_LICENSE("GPL");
MODULE_ALIAS("aes");
