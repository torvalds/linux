// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Glue code for accelerated AES-GCM stitched implementation for ppc64le.
 *
 * Copyright 2022- IBM Inc. All rights reserved
 */

#include <linux/unaligned.h>
#include <asm/simd.h>
#include <asm/switch_to.h>
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

MODULE_DESCRIPTION("PPC64le AES-GCM with Stitched implementation");
MODULE_AUTHOR("Danny Tsen <dtsen@linux.ibm.com");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS_CRYPTO("aes");

asmlinkage int aes_p10_set_encrypt_key(const u8 *userKey, const int bits,
				      void *key);
asmlinkage void aes_p10_encrypt(const u8 *in, u8 *out, const void *key);
asmlinkage void aes_p10_gcm_encrypt(u8 *in, u8 *out, size_t len,
				    void *rkey, u8 *iv, void *Xi);
asmlinkage void aes_p10_gcm_decrypt(u8 *in, u8 *out, size_t len,
				    void *rkey, u8 *iv, void *Xi);
asmlinkage void gcm_init_htable(unsigned char htable[], unsigned char Xi[]);
asmlinkage void gcm_ghash_p10(unsigned char *Xi, unsigned char *Htable,
		unsigned char *aad, unsigned int alen);

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
};
struct Hash_ctx {
	u8 H[16];	/* subkey */
	u8 Htable[256];	/* Xi, Hash table(offset 32) */
};

struct p10_aes_gcm_ctx {
	struct aes_key enc_key;
};

static void vsx_begin(void)
{
	preempt_disable();
	enable_kernel_vsx();
}

static void vsx_end(void)
{
	disable_kernel_vsx();
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

static int p10_aes_gcm_crypt(struct aead_request *req, int enc)
{
	struct crypto_tfm *tfm = req->base.tfm;
	struct p10_aes_gcm_ctx *ctx = crypto_tfm_ctx(tfm);
	u8 databuf[sizeof(struct gcm_ctx) + PPC_ALIGN];
	struct gcm_ctx *gctx = PTR_ALIGN((void *)databuf, PPC_ALIGN);
	u8 hashbuf[sizeof(struct Hash_ctx) + PPC_ALIGN];
	struct Hash_ctx *hash = PTR_ALIGN((void *)hashbuf, PPC_ALIGN);
	struct scatter_walk assoc_sg_walk;
	struct skcipher_walk walk;
	u8 *assocmem = NULL;
	u8 *assoc;
	unsigned int assoclen = req->assoclen;
	unsigned int cryptlen = req->cryptlen;
	unsigned char ivbuf[AES_BLOCK_SIZE+PPC_ALIGN];
	unsigned char *iv = PTR_ALIGN((void *)ivbuf, PPC_ALIGN);
	int ret;
	unsigned long auth_tag_len = crypto_aead_authsize(__crypto_aead_cast(tfm));
	u8 otag[16];
	int total_processed = 0;

	memset(databuf, 0, sizeof(databuf));
	memset(hashbuf, 0, sizeof(hashbuf));
	memset(ivbuf, 0, sizeof(ivbuf));
	memcpy(iv, req->iv, GCM_IV_SIZE);

	/* Linearize assoc, if not already linear */
	if (req->src->length >= assoclen && req->src->length) {
		scatterwalk_start(&assoc_sg_walk, req->src);
		assoc = scatterwalk_map(&assoc_sg_walk);
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

	if (!assocmem)
		scatterwalk_unmap(assoc);
	else
		kfree(assocmem);

	if (enc)
		ret = skcipher_walk_aead_encrypt(&walk, req, false);
	else
		ret = skcipher_walk_aead_decrypt(&walk, req, false);
	if (ret)
		return ret;

	while (walk.nbytes > 0 && ret == 0) {

		vsx_begin();
		if (enc)
			aes_p10_gcm_encrypt(walk.src.virt.addr,
					    walk.dst.virt.addr,
					    walk.nbytes,
					    &ctx->enc_key, gctx->iv, hash->Htable);
		else
			aes_p10_gcm_decrypt(walk.src.virt.addr,
					    walk.dst.virt.addr,
					    walk.nbytes,
					    &ctx->enc_key, gctx->iv, hash->Htable);
		vsx_end();

		total_processed += walk.nbytes;
		ret = skcipher_walk_done(&walk, 0);
	}

	if (ret)
		return ret;

	/* Finalize hash */
	vsx_begin();
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

static int p10_aes_gcm_encrypt(struct aead_request *req)
{
	return p10_aes_gcm_crypt(req, 1);
}

static int p10_aes_gcm_decrypt(struct aead_request *req)
{
	return p10_aes_gcm_crypt(req, 0);
}

static struct aead_alg gcm_aes_alg = {
	.ivsize			= GCM_IV_SIZE,
	.maxauthsize		= 16,

	.setauthsize		= set_authsize,
	.setkey			= p10_aes_gcm_setkey,
	.encrypt		= p10_aes_gcm_encrypt,
	.decrypt		= p10_aes_gcm_decrypt,

	.base.cra_name		= "gcm(aes)",
	.base.cra_driver_name	= "aes_gcm_p10",
	.base.cra_priority	= 2100,
	.base.cra_blocksize	= 1,
	.base.cra_ctxsize	= sizeof(struct p10_aes_gcm_ctx),
	.base.cra_module	= THIS_MODULE,
};

static int __init p10_init(void)
{
	return crypto_register_aead(&gcm_aes_alg);
}

static void __exit p10_exit(void)
{
	crypto_unregister_aead(&gcm_aes_alg);
}

module_cpu_feature_match(PPC_MODULE_FEATURE_P10, p10_init);
module_exit(p10_exit);
