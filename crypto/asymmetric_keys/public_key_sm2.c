/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * asymmetric public-key algorithm for SM2-with-SM3 certificate
 * as specified by OSCCA GM/T 0003.1-2012 -- 0003.5-2012 SM2 and
 * described at https://tools.ietf.org/html/draft-shen-sm2-ecdsa-02
 *
 * Copyright (c) 2020, Alibaba Group.
 * Authors: Tianjia Zhang <tianjia.zhang@linux.alibaba.com>
 */

#include <crypto/sm3_base.h>
#include <crypto/sm2.h>
#include <crypto/public_key.h>

#if IS_REACHABLE(CONFIG_CRYPTO_SM2)

int cert_sig_digest_update(const struct public_key_signature *sig,
				struct crypto_akcipher *tfm_pkey)
{
	struct crypto_shash *tfm;
	struct shash_desc *desc;
	size_t desc_size;
	unsigned char dgst[SM3_DIGEST_SIZE];
	int ret;

	BUG_ON(!sig->data);

	ret = sm2_compute_z_digest(tfm_pkey, SM2_DEFAULT_USERID,
					SM2_DEFAULT_USERID_LEN, dgst);
	if (ret)
		return ret;

	tfm = crypto_alloc_shash(sig->hash_algo, 0, 0);
	if (IS_ERR(tfm))
		return PTR_ERR(tfm);

	desc_size = crypto_shash_descsize(tfm) + sizeof(*desc);
	desc = kzalloc(desc_size, GFP_KERNEL);
	if (!desc)
		goto error_free_tfm;

	desc->tfm = tfm;

	ret = crypto_shash_init(desc);
	if (ret < 0)
		goto error_free_desc;

	ret = crypto_shash_update(desc, dgst, SM3_DIGEST_SIZE);
	if (ret < 0)
		goto error_free_desc;

	ret = crypto_shash_finup(desc, sig->data, sig->data_size, sig->digest);

error_free_desc:
	kfree(desc);
error_free_tfm:
	crypto_free_shash(tfm);
	return ret;
}

#endif /* ! IS_REACHABLE(CONFIG_CRYPTO_SM2) */
