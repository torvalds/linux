// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Glue code for accelerated AES-GCM stitched implementation for ppc64le.
 *
 * Copyright 2022- IBM Inc. All rights reserved
 */

#include <linux/unaligned.h>
#include <asm/simd.h>
#include <asm/switch_to.h>
#include <crypto/gcm.h>
#include <crypto/aes.h>
#include <crypto/algapi.h>
#include <crypto/b128ops.h>
#include <crypto/gf128mul.h>
#include <crypto/internal/simd.h>
#include <crypto/internal/aead.h>
#include <crypto/internal/hash.h>
#include <crypto/internal/skcipher.h>
#include <crypto/scatterwalk.h>
#include <linux/cpufeature.h>
#include <linux/crypto.h>
#include <linux/module.h>
#include <linux/types.h>

#define	PPC_ALIGN		16
#define GCM_IV_SIZE		12
#define RFC4106_NONCE_SIZE	4

MODULE_DESCRIPTION("PPC64le AES-GCM with Stitched implementation");
MODULE_AUTHOR("Danny Tsen <dtsen@linux.ibm.com");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS_CRYPTO("aes");

asmlinkage int aes_p10_set_encrypt_key(const u8 *userKey, const int bits,
				       void *key);
asmlinkage void aes_p10_encrypt(const u8 *in, u8 *out, const void *key);
asmlinkage void aes_p10_gcm_encrypt(const u8 *in, u8 *out, size_t len,
				    void *rkey, u8 *iv, void *Xi);
asmlinkage void aes_p10_gcm_decrypt(const u8 *in, u8 *out, size_t len,
				    void *rkey, u8 *iv, void *Xi);
asmlinkage void gcm_init_htable(unsigned char htable[], unsigned char Xi[]);
asmlinkage void gcm_ghash_p10(unsigned char *Xi, unsigned char *Htable,
			      unsigned char *aad, unsigned int alen);
asmlinkage void gcm_update(u8 *iv, void *Xi);

struct aes_key {
	u8 key[AES_MAX_KEYLENGTH];
	u64 rounds;
};

struct gcm_ctx {
	u8 iv[16];
	u8 ivtag[16];
	u8 aad_hash[16];
	u64 aadLen;
	u64 Plen;	/* offset 56 - used in aes_p10_gcm_{en/de}crypt */
	u8 pblock[16];
};
struct Hash_ctx {
	u8 H[16];	/* subkey */
	u8 Htable[256];	/* Xi, Hash table(offset 32) */
};

struct p10_aes_gcm_ctx {
	struct aes_key enc_key;
	u8 nonce[RFC4106_NONCE_SIZE];
};

static void vsx_begin(void)
{
	preempt_disable();
	pagefault_disable();
	enable_kernel_vsx();
}

static void vsx_end(void)
{
	disable_kernel_vsx();
	pagefault_enable();
	preempt_enable();
}

static void set_subkey(unsigned char *hash)
{
	*(u64 *)&hash[0] = be64_to_cpup((__be64 *)&hash[0]);
	*(u64 *)&hash[8] = be64_to_cpup((__be64 *)&hash[8]);
}

/*
 * Compute aad if any.
 *   - Hash aad and copy to Xi.
 */
static void set_aad(struct gcm_ctx *gctx, struct Hash_ctx *hash,
		    unsigned char *aad, int alen)
{
	int i;
	u8 nXi[16] = {0, };

	gctx->aadLen = alen;
	i = alen & ~0xf;
	if (i) {
		gcm_ghash_p10(nXi, hash->Htable+32, aad, i);
		aad += i;
		alen -= i;
	}
	if (alen) {
		for (i = 0; i < alen; i++)
			nXi[i] ^= aad[i];

		memset(gctx->aad_hash, 0, 16);
		gcm_ghash_p10(gctx->aad_hash, hash->Htable+32, nXi, 16);
	} else {
		memcpy(gctx->aad_hash, nXi, 16);
	}

	memcpy(hash->Htable, gctx->aad_hash, 16);
}

static void gcmp10_init(struct gcm_ctx *gctx, u8 *iv, unsigned char *rdkey,
			struct Hash_ctx *hash, u8 *assoc, unsigned int assoclen)
{
	__be32 counter = cpu_to_be32(1);

	aes_p10_encrypt(hash->H, hash->H, rdkey);
	set_subkey(hash->H);
	gcm_init_htable(hash->Htable+32, hash->H);

	*((__be32 *)(iv+12)) = counter;

	gctx->Plen = 0;

	/*
	 * Encrypt counter vector as iv tag and increment counter.
	 */
	aes_p10_encrypt(iv, gctx->ivtag, rdkey);

	counter = cpu_to_be32(2);
	*((__be32 *)(iv+12)) = counter;
	memcpy(gctx->iv, iv, 16);

	gctx->aadLen = assoclen;
	memset(gctx->aad_hash, 0, 16);
	if (assoclen)
		set_aad(gctx, hash, assoc, assoclen);
}

static void finish_tag(struct gcm_ctx *gctx, struct Hash_ctx *hash, int len)
{
	int i;
	unsigned char len_ac[16 + PPC_ALIGN];
	unsigned char *aclen = PTR_ALIGN((void *)len_ac, PPC_ALIGN);
	__be64 clen = cpu_to_be64(len << 3);
	__be64 alen = cpu_to_be64(gctx->aadLen << 3);

	if (len == 0 && gctx->aadLen == 0) {
		memcpy(hash->Htable, gctx->ivtag, 16);
		return;
	}

	/*
	 * Len is in bits.
	 */
	*((__be64 *)(aclen)) = alen;
	*((__be64 *)(aclen+8)) = clen;

	/*
	 * hash (AAD len and len)
	 */
	gcm_ghash_p10(hash->Htable, hash->Htable+32, aclen, 16);

	for (i = 0; i < 16; i++)
		hash->Htable[i] ^= gctx->ivtag[i];
}

static int set_authsize(struct crypto_aead *tfm, unsigned int authsize)
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

static int p10_aes_gcm_setkey(struct crypto_aead *aead, const u8 *key,
			      unsigned int keylen)
{
	struct crypto_tfm *tfm = crypto_aead_tfm(aead);
	struct p10_aes_gcm_ctx *ctx = crypto_tfm_ctx(tfm);
	int ret;

	vsx_begin();
	ret = aes_p10_set_encrypt_key(key, keylen * 8, &ctx->enc_key);
	vsx_end();

	return ret ? -EINVAL : 0;
}

static int p10_aes_gcm_crypt(struct aead_request *req, u8 *riv,
			     int assoclen, int enc)
{
	struct crypto_tfm *tfm = req->base.tfm;
	struct p10_aes_gcm_ctx *ctx = crypto_tfm_ctx(tfm);
	u8 databuf[sizeof(struct gcm_ctx) + PPC_ALIGN];
	struct gcm_ctx *gctx = PTR_ALIGN((void *)databuf, PPC_ALIGN);
	u8 hashbuf[sizeof(struct Hash_ctx) + PPC_ALIGN];
	struct Hash_ctx *hash = PTR_ALIGN((void *)hashbuf, PPC_ALIGN);
	struct skcipher_walk walk;
	u8 *assocmem = NULL;
	u8 *assoc;
	unsigned int cryptlen = req->cryptlen;
	unsigned char ivbuf[AES_BLOCK_SIZE+PPC_ALIGN];
	unsigned char *iv = PTR_ALIGN((void *)ivbuf, PPC_ALIGN);
	int ret;
	unsigned long auth_tag_len = crypto_aead_authsize(__crypto_aead_cast(tfm));
	u8 otag[16];
	int total_processed = 0;
	int nbytes;

	memset(databuf, 0, sizeof(databuf));
	memset(hashbuf, 0, sizeof(hashbuf));
	memset(ivbuf, 0, sizeof(ivbuf));
	memcpy(iv, riv, GCM_IV_SIZE);

	/* Linearize assoc, if not already linear */
	if (req->src->length >= assoclen && req->src->length) {
		assoc = sg_virt(req->src); /* ppc64 is !HIGHMEM */
	} else {
		gfp_t flags = (req->base.flags & CRYPTO_TFM_REQ_MAY_SLEEP) ?
			      GFP_KERNEL : GFP_ATOMIC;

		/* assoc can be any length, so must be on heap */
		assocmem = kmalloc(assoclen, flags);
		if (unlikely(!assocmem))
			return -ENOMEM;
		assoc = assocmem;

		scatterwalk_map_and_copy(assoc, req->src, 0, assoclen, 0);
	}

	vsx_begin();
	gcmp10_init(gctx, iv, (unsigned char *) &ctx->enc_key, hash, assoc, assoclen);
	vsx_end();

	kfree(assocmem);

	if (enc)
		ret = skcipher_walk_aead_encrypt(&walk, req, false);
	else
		ret = skcipher_walk_aead_decrypt(&walk, req, false);
	if (ret)
		return ret;

	while ((nbytes = walk.nbytes) > 0 && ret == 0) {
		const u8 *src = walk.src.virt.addr;
		u8 *dst = walk.dst.virt.addr;
		u8 buf[AES_BLOCK_SIZE];

		if (unlikely(nbytes > 0 && nbytes < AES_BLOCK_SIZE))
			src = dst = memcpy(buf, src, nbytes);

		vsx_begin();
		if (enc)
			aes_p10_gcm_encrypt(src, dst, nbytes,
					    &ctx->enc_key, gctx->iv, hash->Htable);
		else
			aes_p10_gcm_decrypt(src, dst, nbytes,
					    &ctx->enc_key, gctx->iv, hash->Htable);

		if (unlikely(nbytes > 0 && nbytes < AES_BLOCK_SIZE))
			memcpy(walk.dst.virt.addr, buf, nbytes);

		vsx_end();

		total_processed += walk.nbytes;
		ret = skcipher_walk_done(&walk, 0);
	}

	if (ret)
		return ret;

	/* Finalize hash */
	vsx_begin();
	gcm_update(gctx->iv, hash->Htable);
	finish_tag(gctx, hash, total_processed);
	vsx_end();

	/* copy Xi to end of dst */
	if (enc)
		scatterwalk_map_and_copy(hash->Htable, req->dst, req->assoclen + cryptlen,
					 auth_tag_len, 1);
	else {
		scatterwalk_map_and_copy(otag, req->src,
					 req->assoclen + cryptlen - auth_tag_len,
					 auth_tag_len, 0);

		if (crypto_memneq(otag, hash->Htable, auth_tag_len)) {
			memzero_explicit(hash->Htable, 16);
			return -EBADMSG;
		}
	}

	return 0;
}

static int rfc4106_setkey(struct crypto_aead *tfm, const u8 *inkey,
			  unsigned int keylen)
{
	struct p10_aes_gcm_ctx *ctx = crypto_aead_ctx(tfm);
	int err;

	keylen -= RFC4106_NONCE_SIZE;
	err = p10_aes_gcm_setkey(tfm, inkey, keylen);
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
	struct p10_aes_gcm_ctx *ctx = crypto_aead_ctx(aead);
	u8 iv[AES_BLOCK_SIZE];

	memcpy(iv, ctx->nonce, RFC4106_NONCE_SIZE);
	memcpy(iv + RFC4106_NONCE_SIZE, req->iv, GCM_RFC4106_IV_SIZE);

	return crypto_ipsec_check_assoclen(req->assoclen) ?:
	       p10_aes_gcm_crypt(req, iv, req->assoclen - GCM_RFC4106_IV_SIZE, 1);
}

static int rfc4106_decrypt(struct aead_request *req)
{
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	struct p10_aes_gcm_ctx *ctx = crypto_aead_ctx(aead);
	u8 iv[AES_BLOCK_SIZE];

	memcpy(iv, ctx->nonce, RFC4106_NONCE_SIZE);
	memcpy(iv + RFC4106_NONCE_SIZE, req->iv, GCM_RFC4106_IV_SIZE);

	return crypto_ipsec_check_assoclen(req->assoclen) ?:
	       p10_aes_gcm_crypt(req, iv, req->assoclen - GCM_RFC4106_IV_SIZE, 0);
}

static int p10_aes_gcm_encrypt(struct aead_request *req)
{
	return p10_aes_gcm_crypt(req, req->iv, req->assoclen, 1);
}

static int p10_aes_gcm_decrypt(struct aead_request *req)
{
	return p10_aes_gcm_crypt(req, req->iv, req->assoclen, 0);
}

static struct aead_alg gcm_aes_algs[] = {{
	.ivsize			= GCM_IV_SIZE,
	.maxauthsize		= 16,

	.setauthsize		= set_authsize,
	.setkey			= p10_aes_gcm_setkey,
	.encrypt		= p10_aes_gcm_encrypt,
	.decrypt		= p10_aes_gcm_decrypt,

	.base.cra_name		= "__gcm(aes)",
	.base.cra_driver_name	= "__aes_gcm_p10",
	.base.cra_priority	= 2100,
	.base.cra_blocksize	= 1,
	.base.cra_ctxsize	= sizeof(struct p10_aes_gcm_ctx)+
				  4 * sizeof(u64[2]),
	.base.cra_module	= THIS_MODULE,
	.base.cra_flags		= CRYPTO_ALG_INTERNAL,
}, {
	.ivsize			= GCM_RFC4106_IV_SIZE,
	.maxauthsize		= 16,
	.setkey			= rfc4106_setkey,
	.setauthsize		= rfc4106_setauthsize,
	.encrypt		= rfc4106_encrypt,
	.decrypt		= rfc4106_decrypt,

	.base.cra_name		= "__rfc4106(gcm(aes))",
	.base.cra_driver_name	= "__rfc4106_aes_gcm_p10",
	.base.cra_priority	= 2100,
	.base.cra_blocksize	= 1,
	.base.cra_ctxsize	= sizeof(struct p10_aes_gcm_ctx) +
				  4 * sizeof(u64[2]),
	.base.cra_module	= THIS_MODULE,
	.base.cra_flags		= CRYPTO_ALG_INTERNAL,
}};

static struct simd_aead_alg *p10_simd_aeads[ARRAY_SIZE(gcm_aes_algs)];

static int __init p10_init(void)
{
	int ret;

	if (!cpu_has_feature(CPU_FTR_ARCH_31))
		return 0;

	ret = simd_register_aeads_compat(gcm_aes_algs,
					 ARRAY_SIZE(gcm_aes_algs),
					 p10_simd_aeads);
	if (ret) {
		simd_unregister_aeads(gcm_aes_algs, ARRAY_SIZE(gcm_aes_algs),
				      p10_simd_aeads);
		return ret;
	}
	return 0;
}

static void __exit p10_exit(void)
{
	simd_unregister_aeads(gcm_aes_algs, ARRAY_SIZE(gcm_aes_algs),
			      p10_simd_aeads);
}

module_init(p10_init);
module_exit(p10_exit);
