/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * SM4-CCM AEAD Algorithm using ARMv8 Crypto Extensions
 * as specified in rfc8998
 * https://datatracker.ietf.org/doc/html/rfc8998
 *
 * Copyright (C) 2022 Tianjia Zhang <tianjia.zhang@linux.alibaba.com>
 */

#include <linux/module.h>
#include <linux/crypto.h>
#include <linux/kernel.h>
#include <linux/cpufeature.h>
#include <asm/neon.h>
#include <crypto/scatterwalk.h>
#include <crypto/internal/aead.h>
#include <crypto/internal/skcipher.h>
#include <crypto/sm4.h>
#include "sm4-ce.h"

asmlinkage void sm4_ce_cbcmac_update(const u32 *rkey_enc, u8 *mac,
				     const u8 *src, unsigned int nblocks);
asmlinkage void sm4_ce_ccm_enc(const u32 *rkey_enc, u8 *dst, const u8 *src,
			       u8 *iv, unsigned int nbytes, u8 *mac);
asmlinkage void sm4_ce_ccm_dec(const u32 *rkey_enc, u8 *dst, const u8 *src,
			       u8 *iv, unsigned int nbytes, u8 *mac);
asmlinkage void sm4_ce_ccm_final(const u32 *rkey_enc, u8 *iv, u8 *mac);


static int ccm_setkey(struct crypto_aead *tfm, const u8 *key,
		      unsigned int key_len)
{
	struct sm4_ctx *ctx = crypto_aead_ctx(tfm);

	if (key_len != SM4_KEY_SIZE)
		return -EINVAL;

	kernel_neon_begin();
	sm4_ce_expand_key(key, ctx->rkey_enc, ctx->rkey_dec,
			  crypto_sm4_fk, crypto_sm4_ck);
	kernel_neon_end();

	return 0;
}

static int ccm_setauthsize(struct crypto_aead *tfm, unsigned int authsize)
{
	if ((authsize & 1) || authsize < 4)
		return -EINVAL;
	return 0;
}

static int ccm_format_input(u8 info[], struct aead_request *req,
			    unsigned int msglen)
{
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	unsigned int l = req->iv[0] + 1;
	unsigned int m;
	__be32 len;

	/* verify that CCM dimension 'L': 2 <= L <= 8 */
	if (l < 2 || l > 8)
		return -EINVAL;
	if (l < 4 && msglen >> (8 * l))
		return -EOVERFLOW;

	memset(&req->iv[SM4_BLOCK_SIZE - l], 0, l);

	memcpy(info, req->iv, SM4_BLOCK_SIZE);

	m = crypto_aead_authsize(aead);

	/* format flags field per RFC 3610/NIST 800-38C */
	*info |= ((m - 2) / 2) << 3;
	if (req->assoclen)
		*info |= (1 << 6);

	/*
	 * format message length field,
	 * Linux uses a u32 type to represent msglen
	 */
	if (l >= 4)
		l = 4;

	len = cpu_to_be32(msglen);
	memcpy(&info[SM4_BLOCK_SIZE - l], (u8 *)&len + 4 - l, l);

	return 0;
}

static void ccm_calculate_auth_mac(struct aead_request *req, u8 mac[])
{
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	struct sm4_ctx *ctx = crypto_aead_ctx(aead);
	struct __packed { __be16 l; __be32 h; } aadlen;
	u32 assoclen = req->assoclen;
	struct scatter_walk walk;
	unsigned int len;

	if (assoclen < 0xff00) {
		aadlen.l = cpu_to_be16(assoclen);
		len = 2;
	} else {
		aadlen.l = cpu_to_be16(0xfffe);
		put_unaligned_be32(assoclen, &aadlen.h);
		len = 6;
	}

	sm4_ce_crypt_block(ctx->rkey_enc, mac, mac);
	crypto_xor(mac, (const u8 *)&aadlen, len);

	scatterwalk_start(&walk, req->src);

	do {
		unsigned int n, orig_n;
		const u8 *p;

		orig_n = scatterwalk_next(&walk, assoclen);
		p = walk.addr;
		n = orig_n;

		while (n > 0) {
			unsigned int l, nblocks;

			if (len == SM4_BLOCK_SIZE) {
				if (n < SM4_BLOCK_SIZE) {
					sm4_ce_crypt_block(ctx->rkey_enc,
							   mac, mac);

					len = 0;
				} else {
					nblocks = n / SM4_BLOCK_SIZE;
					sm4_ce_cbcmac_update(ctx->rkey_enc,
							     mac, p, nblocks);

					p += nblocks * SM4_BLOCK_SIZE;
					n %= SM4_BLOCK_SIZE;

					continue;
				}
			}

			l = min(n, SM4_BLOCK_SIZE - len);
			if (l) {
				crypto_xor(mac + len, p, l);
				len += l;
				p += l;
				n -= l;
			}
		}

		scatterwalk_done_src(&walk, orig_n);
		assoclen -= orig_n;
	} while (assoclen);
}

static int ccm_crypt(struct aead_request *req, struct skcipher_walk *walk,
		     u32 *rkey_enc, u8 mac[],
		     void (*sm4_ce_ccm_crypt)(const u32 *rkey_enc, u8 *dst,
					const u8 *src, u8 *iv,
					unsigned int nbytes, u8 *mac))
{
	u8 __aligned(8) ctr0[SM4_BLOCK_SIZE];
	int err = 0;

	/* preserve the initial ctr0 for the TAG */
	memcpy(ctr0, walk->iv, SM4_BLOCK_SIZE);
	crypto_inc(walk->iv, SM4_BLOCK_SIZE);

	kernel_neon_begin();

	if (req->assoclen)
		ccm_calculate_auth_mac(req, mac);

	while (walk->nbytes && walk->nbytes != walk->total) {
		unsigned int tail = walk->nbytes % SM4_BLOCK_SIZE;

		sm4_ce_ccm_crypt(rkey_enc, walk->dst.virt.addr,
				 walk->src.virt.addr, walk->iv,
				 walk->nbytes - tail, mac);

		kernel_neon_end();

		err = skcipher_walk_done(walk, tail);

		kernel_neon_begin();
	}

	if (walk->nbytes) {
		sm4_ce_ccm_crypt(rkey_enc, walk->dst.virt.addr,
				 walk->src.virt.addr, walk->iv,
				 walk->nbytes, mac);

		sm4_ce_ccm_final(rkey_enc, ctr0, mac);

		kernel_neon_end();

		err = skcipher_walk_done(walk, 0);
	} else {
		sm4_ce_ccm_final(rkey_enc, ctr0, mac);

		kernel_neon_end();
	}

	return err;
}

static int ccm_encrypt(struct aead_request *req)
{
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	struct sm4_ctx *ctx = crypto_aead_ctx(aead);
	u8 __aligned(8) mac[SM4_BLOCK_SIZE];
	struct skcipher_walk walk;
	int err;

	err = ccm_format_input(mac, req, req->cryptlen);
	if (err)
		return err;

	err = skcipher_walk_aead_encrypt(&walk, req, false);
	if (err)
		return err;

	err = ccm_crypt(req, &walk, ctx->rkey_enc, mac, sm4_ce_ccm_enc);
	if (err)
		return err;

	/* copy authtag to end of dst */
	scatterwalk_map_and_copy(mac, req->dst, req->assoclen + req->cryptlen,
				 crypto_aead_authsize(aead), 1);

	return 0;
}

static int ccm_decrypt(struct aead_request *req)
{
	struct crypto_aead *aead = crypto_aead_reqtfm(req);
	unsigned int authsize = crypto_aead_authsize(aead);
	struct sm4_ctx *ctx = crypto_aead_ctx(aead);
	u8 __aligned(8) mac[SM4_BLOCK_SIZE];
	u8 authtag[SM4_BLOCK_SIZE];
	struct skcipher_walk walk;
	int err;

	err = ccm_format_input(mac, req, req->cryptlen - authsize);
	if (err)
		return err;

	err = skcipher_walk_aead_decrypt(&walk, req, false);
	if (err)
		return err;

	err = ccm_crypt(req, &walk, ctx->rkey_enc, mac, sm4_ce_ccm_dec);
	if (err)
		return err;

	/* compare calculated auth tag with the stored one */
	scatterwalk_map_and_copy(authtag, req->src,
				 req->assoclen + req->cryptlen - authsize,
				 authsize, 0);

	if (crypto_memneq(authtag, mac, authsize))
		return -EBADMSG;

	return 0;
}

static struct aead_alg sm4_ccm_alg = {
	.base = {
		.cra_name		= "ccm(sm4)",
		.cra_driver_name	= "ccm-sm4-ce",
		.cra_priority		= 400,
		.cra_blocksize		= 1,
		.cra_ctxsize		= sizeof(struct sm4_ctx),
		.cra_module		= THIS_MODULE,
	},
	.ivsize		= SM4_BLOCK_SIZE,
	.chunksize	= SM4_BLOCK_SIZE,
	.maxauthsize	= SM4_BLOCK_SIZE,
	.setkey		= ccm_setkey,
	.setauthsize	= ccm_setauthsize,
	.encrypt	= ccm_encrypt,
	.decrypt	= ccm_decrypt,
};

static int __init sm4_ce_ccm_init(void)
{
	return crypto_register_aead(&sm4_ccm_alg);
}

static void __exit sm4_ce_ccm_exit(void)
{
	crypto_unregister_aead(&sm4_ccm_alg);
}

module_cpu_feature_match(SM4, sm4_ce_ccm_init);
module_exit(sm4_ce_ccm_exit);

MODULE_DESCRIPTION("Synchronous SM4 in CCM mode using ARMv8 Crypto Extensions");
MODULE_ALIAS_CRYPTO("ccm(sm4)");
MODULE_AUTHOR("Tianjia Zhang <tianjia.zhang@linux.alibaba.com>");
MODULE_LICENSE("GPL v2");
