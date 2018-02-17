/*
 * Shared async block cipher helpers
 *
 * Copyright (c) 2012 Jussi Kivilinna <jussi.kivilinna@mbnet.fi>
 *
 * Based on aesni-intel_glue.c by:
 *  Copyright (C) 2008, Intel Corp.
 *    Author: Huang Ying <ying.huang@intel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <linux/kernel.h>
#include <linux/crypto.h>
#include <linux/init.h>
#include <linux/module.h>
#include <crypto/algapi.h>
#include <crypto/cryptd.h>
#include <crypto/ablk_helper.h>
#include <asm/simd.h>

int ablk_set_key(struct crypto_ablkcipher *tfm, const u8 *key,
		 unsigned int key_len)
{
	struct async_helper_ctx *ctx = crypto_ablkcipher_ctx(tfm);
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
EXPORT_SYMBOL_GPL(ablk_set_key);

int __ablk_encrypt(struct ablkcipher_request *req)
{
	struct crypto_ablkcipher *tfm = crypto_ablkcipher_reqtfm(req);
	struct async_helper_ctx *ctx = crypto_ablkcipher_ctx(tfm);
	struct blkcipher_desc desc;

	desc.tfm = cryptd_ablkcipher_child(ctx->cryptd_tfm);
	desc.info = req->info;
	desc.flags = 0;

	return crypto_blkcipher_crt(desc.tfm)->encrypt(
		&desc, req->dst, req->src, req->nbytes);
}
EXPORT_SYMBOL_GPL(__ablk_encrypt);

int ablk_encrypt(struct ablkcipher_request *req)
{
	struct crypto_ablkcipher *tfm = crypto_ablkcipher_reqtfm(req);
	struct async_helper_ctx *ctx = crypto_ablkcipher_ctx(tfm);

	if (!may_use_simd() ||
	    (in_atomic() && cryptd_ablkcipher_queued(ctx->cryptd_tfm))) {
		struct ablkcipher_request *cryptd_req =
			ablkcipher_request_ctx(req);

		*cryptd_req = *req;
		ablkcipher_request_set_tfm(cryptd_req, &ctx->cryptd_tfm->base);

		return crypto_ablkcipher_encrypt(cryptd_req);
	} else {
		return __ablk_encrypt(req);
	}
}
EXPORT_SYMBOL_GPL(ablk_encrypt);

int ablk_decrypt(struct ablkcipher_request *req)
{
	struct crypto_ablkcipher *tfm = crypto_ablkcipher_reqtfm(req);
	struct async_helper_ctx *ctx = crypto_ablkcipher_ctx(tfm);

	if (!may_use_simd() ||
	    (in_atomic() && cryptd_ablkcipher_queued(ctx->cryptd_tfm))) {
		struct ablkcipher_request *cryptd_req =
			ablkcipher_request_ctx(req);

		*cryptd_req = *req;
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
EXPORT_SYMBOL_GPL(ablk_decrypt);

void ablk_exit(struct crypto_tfm *tfm)
{
	struct async_helper_ctx *ctx = crypto_tfm_ctx(tfm);

	cryptd_free_ablkcipher(ctx->cryptd_tfm);
}
EXPORT_SYMBOL_GPL(ablk_exit);

int ablk_init_common(struct crypto_tfm *tfm, const char *drv_name)
{
	struct async_helper_ctx *ctx = crypto_tfm_ctx(tfm);
	struct cryptd_ablkcipher *cryptd_tfm;

	cryptd_tfm = cryptd_alloc_ablkcipher(drv_name, CRYPTO_ALG_INTERNAL,
					     CRYPTO_ALG_INTERNAL);
	if (IS_ERR(cryptd_tfm))
		return PTR_ERR(cryptd_tfm);

	ctx->cryptd_tfm = cryptd_tfm;
	tfm->crt_ablkcipher.reqsize = sizeof(struct ablkcipher_request) +
		crypto_ablkcipher_reqsize(&cryptd_tfm->base);

	return 0;
}
EXPORT_SYMBOL_GPL(ablk_init_common);

int ablk_init(struct crypto_tfm *tfm)
{
	char drv_name[CRYPTO_MAX_ALG_NAME];

	snprintf(drv_name, sizeof(drv_name), "__driver-%s",
					crypto_tfm_alg_driver_name(tfm));

	return ablk_init_common(tfm, drv_name);
}
EXPORT_SYMBOL_GPL(ablk_init);

MODULE_LICENSE("GPL");
