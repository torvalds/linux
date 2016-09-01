/* In-software asymmetric public-key crypto subtype
 *
 * See Documentation/crypto/asymmetric-keys.txt
 *
 * Copyright (C) 2012 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

#define pr_fmt(fmt) "PKEY: "fmt
#include <linux/module.h>
#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/seq_file.h>
#include <linux/scatterlist.h>
#include <keys/asymmetric-subtype.h>
#include <crypto/public_key.h>
#include <crypto/akcipher.h>

MODULE_LICENSE("GPL");

/*
 * Provide a part of a description of the key for /proc/keys.
 */
static void public_key_describe(const struct key *asymmetric_key,
				struct seq_file *m)
{
	struct public_key *key = asymmetric_key->payload.data[asym_crypto];

	if (key)
		seq_printf(m, "%s.%s", key->id_type, key->pkey_algo);
}

/*
 * Destroy a public key algorithm key.
 */
void public_key_free(struct public_key *key)
{
	if (key) {
		kfree(key->key);
		kfree(key);
	}
}
EXPORT_SYMBOL_GPL(public_key_free);

/*
 * Destroy a public key algorithm key.
 */
static void public_key_destroy(void *payload0, void *payload3)
{
	public_key_free(payload0);
	public_key_signature_free(payload3);
}

/*
 * Determine the crypto algorithm name.
 */
static
int software_key_determine_akcipher(const char *encoding,
				    const char *hash_algo,
				    const struct public_key *pkey,
				    char alg_name[CRYPTO_MAX_ALG_NAME])
{
	int n;

	if (strcmp(encoding, "pkcs1") == 0) {
		/* The data wangled by the RSA algorithm is typically padded
		 * and encoded in some manner, such as EMSA-PKCS1-1_5 [RFC3447
		 * sec 8.2].
		 */
		if (!hash_algo)
			n = snprintf(alg_name, CRYPTO_MAX_ALG_NAME,
				     "pkcs1pad(%s)",
				     pkey->pkey_algo);
		else
			n = snprintf(alg_name, CRYPTO_MAX_ALG_NAME,
				     "pkcs1pad(%s,%s)",
				     pkey->pkey_algo, hash_algo);
		return n >= CRYPTO_MAX_ALG_NAME ? -EINVAL : 0;
	}

	if (strcmp(encoding, "raw") == 0) {
		strcpy(alg_name, pkey->pkey_algo);
		return 0;
	}

	return -ENOPKG;
}

/*
 * Query information about a key.
 */
static int software_key_query(const struct kernel_pkey_params *params,
			      struct kernel_pkey_query *info)
{
	struct crypto_akcipher *tfm;
	struct public_key *pkey = params->key->payload.data[asym_crypto];
	char alg_name[CRYPTO_MAX_ALG_NAME];
	int ret, len;

	ret = software_key_determine_akcipher(params->encoding,
					      params->hash_algo,
					      pkey, alg_name);
	if (ret < 0)
		return ret;

	tfm = crypto_alloc_akcipher(alg_name, 0, 0);
	if (IS_ERR(tfm))
		return PTR_ERR(tfm);

	if (pkey->key_is_private)
		ret = crypto_akcipher_set_priv_key(tfm,
						   pkey->key, pkey->keylen);
	else
		ret = crypto_akcipher_set_pub_key(tfm,
						  pkey->key, pkey->keylen);
	if (ret < 0)
		goto error_free_tfm;

	len = crypto_akcipher_maxsize(tfm);
	info->key_size = len * 8;
	info->max_data_size = len;
	info->max_sig_size = len;
	info->max_enc_size = len;
	info->max_dec_size = len;
	info->supported_ops = (KEYCTL_SUPPORTS_ENCRYPT |
			       KEYCTL_SUPPORTS_VERIFY);
	if (pkey->key_is_private)
		info->supported_ops |= (KEYCTL_SUPPORTS_DECRYPT |
					KEYCTL_SUPPORTS_SIGN);
	ret = 0;

error_free_tfm:
	crypto_free_akcipher(tfm);
	pr_devel("<==%s() = %d\n", __func__, ret);
	return ret;
}

struct public_key_completion {
	struct completion completion;
	int err;
};

static void public_key_crypto_done(struct crypto_async_request *req, int err)
{
	struct public_key_completion *compl = req->data;

	if (err == -EINPROGRESS)
		return;

	compl->err = err;
	complete(&compl->completion);
}

/*
 * Do encryption, decryption and signing ops.
 */
static int software_key_eds_op(struct kernel_pkey_params *params,
			       const void *in, void *out)
{
	struct public_key_completion compl;
	const struct public_key *pkey = params->key->payload.data[asym_crypto];
	struct akcipher_request *req;
	struct crypto_akcipher *tfm;
	struct scatterlist in_sg, out_sg;
	char alg_name[CRYPTO_MAX_ALG_NAME];
	int ret;

	pr_devel("==>%s()\n", __func__);

	ret = software_key_determine_akcipher(params->encoding,
					      params->hash_algo,
					      pkey, alg_name);
	if (ret < 0)
		return ret;

	tfm = crypto_alloc_akcipher(alg_name, 0, 0);
	if (IS_ERR(tfm))
		return PTR_ERR(tfm);

	req = akcipher_request_alloc(tfm, GFP_KERNEL);
	if (!req)
		goto error_free_tfm;

	if (pkey->key_is_private)
		ret = crypto_akcipher_set_priv_key(tfm,
						   pkey->key, pkey->keylen);
	else
		ret = crypto_akcipher_set_pub_key(tfm,
						  pkey->key, pkey->keylen);
	if (ret)
		goto error_free_req;

	sg_init_one(&in_sg, in, params->in_len);
	sg_init_one(&out_sg, out, params->out_len);
	akcipher_request_set_crypt(req, &in_sg, &out_sg, params->in_len,
				   params->out_len);
	init_completion(&compl.completion);
	akcipher_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG |
				      CRYPTO_TFM_REQ_MAY_SLEEP,
				      public_key_crypto_done, &compl);

	/* Perform the encryption calculation. */
	switch (params->op) {
	case kernel_pkey_encrypt:
		ret = crypto_akcipher_encrypt(req);
		break;
	case kernel_pkey_decrypt:
		ret = crypto_akcipher_decrypt(req);
		break;
	case kernel_pkey_sign:
		ret = crypto_akcipher_sign(req);
		break;
	default:
		BUG();
	}
	if (ret == -EINPROGRESS) {
		wait_for_completion(&compl.completion);
		ret = compl.err;
	}

	if (ret == 0)
		ret = req->dst_len;

error_free_req:
	akcipher_request_free(req);
error_free_tfm:
	crypto_free_akcipher(tfm);
	pr_devel("<==%s() = %d\n", __func__, ret);
	return ret;
}

/*
 * Verify a signature using a public key.
 */
int public_key_verify_signature(const struct public_key *pkey,
				const struct public_key_signature *sig)
{
	struct public_key_completion compl;
	struct crypto_akcipher *tfm;
	struct akcipher_request *req;
	struct scatterlist sig_sg, digest_sg;
	char alg_name[CRYPTO_MAX_ALG_NAME];
	void *output;
	unsigned int outlen;
	int ret = -ENOMEM;

	pr_devel("==>%s()\n", __func__);

	BUG_ON(!pkey);
	BUG_ON(!sig);
	BUG_ON(!sig->digest);
	BUG_ON(!sig->s);

	ret = software_key_determine_akcipher(sig->encoding,
					      sig->hash_algo,
					      pkey, alg_name);
	if (ret < 0)
		return ret;

	tfm = crypto_alloc_akcipher(alg_name, 0, 0);
	if (IS_ERR(tfm))
		return PTR_ERR(tfm);

	req = akcipher_request_alloc(tfm, GFP_KERNEL);
	if (!req)
		goto error_free_tfm;

	if (pkey->key_is_private)
		ret = crypto_akcipher_set_priv_key(tfm,
						   pkey->key, pkey->keylen);
	else
		ret = crypto_akcipher_set_pub_key(tfm,
						  pkey->key, pkey->keylen);
	if (ret)
		goto error_free_req;

	outlen = crypto_akcipher_maxsize(tfm);
	output = kmalloc(outlen, GFP_KERNEL);
	if (!output)
		goto error_free_req;

	sg_init_one(&sig_sg, sig->s, sig->s_size);
	sg_init_one(&digest_sg, output, outlen);
	akcipher_request_set_crypt(req, &sig_sg, &digest_sg, sig->s_size,
				   outlen);
	init_completion(&compl.completion);
	akcipher_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG |
				      CRYPTO_TFM_REQ_MAY_SLEEP,
				      public_key_crypto_done, &compl);

	/* Perform the verification calculation.  This doesn't actually do the
	 * verification, but rather calculates the hash expected by the
	 * signature and returns that to us.
	 */
	ret = crypto_akcipher_verify(req);
	if (ret == -EINPROGRESS) {
		wait_for_completion(&compl.completion);
		ret = compl.err;
	}
	if (ret < 0)
		goto out_free_output;

	/* Do the actual verification step. */
	if (req->dst_len != sig->digest_size ||
	    memcmp(sig->digest, output, sig->digest_size) != 0)
		ret = -EKEYREJECTED;

out_free_output:
	kfree(output);
error_free_req:
	akcipher_request_free(req);
error_free_tfm:
	crypto_free_akcipher(tfm);
	pr_devel("<==%s() = %d\n", __func__, ret);
	return ret;
}
EXPORT_SYMBOL_GPL(public_key_verify_signature);

static int public_key_verify_signature_2(const struct key *key,
					 const struct public_key_signature *sig)
{
	const struct public_key *pk = key->payload.data[asym_crypto];
	return public_key_verify_signature(pk, sig);
}

/*
 * Public key algorithm asymmetric key subtype
 */
struct asymmetric_key_subtype public_key_subtype = {
	.owner			= THIS_MODULE,
	.name			= "public_key",
	.name_len		= sizeof("public_key") - 1,
	.describe		= public_key_describe,
	.destroy		= public_key_destroy,
	.query			= software_key_query,
	.eds_op			= software_key_eds_op,
	.verify_signature	= public_key_verify_signature_2,
};
EXPORT_SYMBOL_GPL(public_key_subtype);
