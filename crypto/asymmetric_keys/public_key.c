// SPDX-License-Identifier: GPL-2.0-or-later
/* In-software asymmetric public-key crypto subtype
 *
 * See Documentation/crypto/asymmetric-keys.rst
 *
 * Copyright (C) 2012 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#define pr_fmt(fmt) "PKEY: "fmt
#include <linux/module.h>
#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/seq_file.h>
#include <linux/scatterlist.h>
#include <linux/asn1.h>
#include <keys/asymmetric-subtype.h>
#include <crypto/public_key.h>
#include <crypto/akcipher.h>
#include <crypto/sm2.h>
#include <crypto/sm3_base.h>

MODULE_DESCRIPTION("In-software asymmetric public-key subtype");
MODULE_AUTHOR("Red Hat, Inc.");
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
		kfree(key->params);
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
 * Given a public_key, and an encoding and hash_algo to be used for signing
 * and/or verification with that key, determine the name of the corresponding
 * akcipher algorithm.  Also check that encoding and hash_algo are allowed.
 */
static int
software_key_determine_akcipher(const struct public_key *pkey,
				const char *encoding, const char *hash_algo,
				char alg_name[CRYPTO_MAX_ALG_NAME])
{
	int n;

	if (!encoding)
		return -EINVAL;

	if (strcmp(pkey->pkey_algo, "rsa") == 0) {
		/*
		 * RSA signatures usually use EMSA-PKCS1-1_5 [RFC3447 sec 8.2].
		 */
		if (strcmp(encoding, "pkcs1") == 0) {
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
		if (strcmp(encoding, "raw") != 0)
			return -EINVAL;
		/*
		 * Raw RSA cannot differentiate between different hash
		 * algorithms.
		 */
		if (hash_algo)
			return -EINVAL;
	} else if (strncmp(pkey->pkey_algo, "ecdsa", 5) == 0) {
		if (strcmp(encoding, "x962") != 0)
			return -EINVAL;
		/*
		 * ECDSA signatures are taken over a raw hash, so they don't
		 * differentiate between different hash algorithms.  That means
		 * that the verifier should hard-code a specific hash algorithm.
		 * Unfortunately, in practice ECDSA is used with multiple SHAs,
		 * so we have to allow all of them and not just one.
		 */
		if (!hash_algo)
			return -EINVAL;
		if (strcmp(hash_algo, "sha1") != 0 &&
		    strcmp(hash_algo, "sha224") != 0 &&
		    strcmp(hash_algo, "sha256") != 0 &&
		    strcmp(hash_algo, "sha384") != 0 &&
		    strcmp(hash_algo, "sha512") != 0)
			return -EINVAL;
	} else if (strcmp(pkey->pkey_algo, "sm2") == 0) {
		if (strcmp(encoding, "raw") != 0)
			return -EINVAL;
		if (!hash_algo)
			return -EINVAL;
		if (strcmp(hash_algo, "sm3") != 0)
			return -EINVAL;
	} else if (strcmp(pkey->pkey_algo, "ecrdsa") == 0) {
		if (strcmp(encoding, "raw") != 0)
			return -EINVAL;
		if (!hash_algo)
			return -EINVAL;
		if (strcmp(hash_algo, "streebog256") != 0 &&
		    strcmp(hash_algo, "streebog512") != 0)
			return -EINVAL;
	} else {
		/* Unknown public key algorithm */
		return -ENOPKG;
	}
	if (strscpy(alg_name, pkey->pkey_algo, CRYPTO_MAX_ALG_NAME) < 0)
		return -EINVAL;
	return 0;
}

static u8 *pkey_pack_u32(u8 *dst, u32 val)
{
	memcpy(dst, &val, sizeof(val));
	return dst + sizeof(val);
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
	u8 *key, *ptr;
	int ret, len;

	ret = software_key_determine_akcipher(pkey, params->encoding,
					      params->hash_algo, alg_name);
	if (ret < 0)
		return ret;

	tfm = crypto_alloc_akcipher(alg_name, 0, 0);
	if (IS_ERR(tfm))
		return PTR_ERR(tfm);

	ret = -ENOMEM;
	key = kmalloc(pkey->keylen + sizeof(u32) * 2 + pkey->paramlen,
		      GFP_KERNEL);
	if (!key)
		goto error_free_tfm;
	memcpy(key, pkey->key, pkey->keylen);
	ptr = key + pkey->keylen;
	ptr = pkey_pack_u32(ptr, pkey->algo);
	ptr = pkey_pack_u32(ptr, pkey->paramlen);
	memcpy(ptr, pkey->params, pkey->paramlen);

	if (pkey->key_is_private)
		ret = crypto_akcipher_set_priv_key(tfm, key, pkey->keylen);
	else
		ret = crypto_akcipher_set_pub_key(tfm, key, pkey->keylen);
	if (ret < 0)
		goto error_free_key;

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

error_free_key:
	kfree(key);
error_free_tfm:
	crypto_free_akcipher(tfm);
	pr_devel("<==%s() = %d\n", __func__, ret);
	return ret;
}

/*
 * Do encryption, decryption and signing ops.
 */
static int software_key_eds_op(struct kernel_pkey_params *params,
			       const void *in, void *out)
{
	const struct public_key *pkey = params->key->payload.data[asym_crypto];
	struct akcipher_request *req;
	struct crypto_akcipher *tfm;
	struct crypto_wait cwait;
	struct scatterlist in_sg, out_sg;
	char alg_name[CRYPTO_MAX_ALG_NAME];
	char *key, *ptr;
	int ret;

	pr_devel("==>%s()\n", __func__);

	ret = software_key_determine_akcipher(pkey, params->encoding,
					      params->hash_algo, alg_name);
	if (ret < 0)
		return ret;

	tfm = crypto_alloc_akcipher(alg_name, 0, 0);
	if (IS_ERR(tfm))
		return PTR_ERR(tfm);

	ret = -ENOMEM;
	req = akcipher_request_alloc(tfm, GFP_KERNEL);
	if (!req)
		goto error_free_tfm;

	key = kmalloc(pkey->keylen + sizeof(u32) * 2 + pkey->paramlen,
		      GFP_KERNEL);
	if (!key)
		goto error_free_req;

	memcpy(key, pkey->key, pkey->keylen);
	ptr = key + pkey->keylen;
	ptr = pkey_pack_u32(ptr, pkey->algo);
	ptr = pkey_pack_u32(ptr, pkey->paramlen);
	memcpy(ptr, pkey->params, pkey->paramlen);

	if (pkey->key_is_private)
		ret = crypto_akcipher_set_priv_key(tfm, key, pkey->keylen);
	else
		ret = crypto_akcipher_set_pub_key(tfm, key, pkey->keylen);
	if (ret)
		goto error_free_key;

	sg_init_one(&in_sg, in, params->in_len);
	sg_init_one(&out_sg, out, params->out_len);
	akcipher_request_set_crypt(req, &in_sg, &out_sg, params->in_len,
				   params->out_len);
	crypto_init_wait(&cwait);
	akcipher_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG |
				      CRYPTO_TFM_REQ_MAY_SLEEP,
				      crypto_req_done, &cwait);

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

	ret = crypto_wait_req(ret, &cwait);
	if (ret == 0)
		ret = req->dst_len;

error_free_key:
	kfree(key);
error_free_req:
	akcipher_request_free(req);
error_free_tfm:
	crypto_free_akcipher(tfm);
	pr_devel("<==%s() = %d\n", __func__, ret);
	return ret;
}

#if IS_REACHABLE(CONFIG_CRYPTO_SM2)
static int cert_sig_digest_update(const struct public_key_signature *sig,
				  struct crypto_akcipher *tfm_pkey)
{
	struct crypto_shash *tfm;
	struct shash_desc *desc;
	size_t desc_size;
	unsigned char dgst[SM3_DIGEST_SIZE];
	int ret;

	BUG_ON(!sig->data);

	/* SM2 signatures always use the SM3 hash algorithm */
	if (!sig->hash_algo || strcmp(sig->hash_algo, "sm3") != 0)
		return -EINVAL;

	ret = sm2_compute_z_digest(tfm_pkey, SM2_DEFAULT_USERID,
					SM2_DEFAULT_USERID_LEN, dgst);
	if (ret)
		return ret;

	tfm = crypto_alloc_shash(sig->hash_algo, 0, 0);
	if (IS_ERR(tfm))
		return PTR_ERR(tfm);

	desc_size = crypto_shash_descsize(tfm) + sizeof(*desc);
	desc = kzalloc(desc_size, GFP_KERNEL);
	if (!desc) {
		ret = -ENOMEM;
		goto error_free_tfm;
	}

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
#else
static inline int cert_sig_digest_update(
	const struct public_key_signature *sig,
	struct crypto_akcipher *tfm_pkey)
{
	return -ENOTSUPP;
}
#endif /* ! IS_REACHABLE(CONFIG_CRYPTO_SM2) */

/*
 * Verify a signature using a public key.
 */
int public_key_verify_signature(const struct public_key *pkey,
				const struct public_key_signature *sig)
{
	struct crypto_wait cwait;
	struct crypto_akcipher *tfm;
	struct akcipher_request *req;
	struct scatterlist src_sg[2];
	char alg_name[CRYPTO_MAX_ALG_NAME];
	char *key, *ptr;
	int ret;

	pr_devel("==>%s()\n", __func__);

	BUG_ON(!pkey);
	BUG_ON(!sig);
	BUG_ON(!sig->s);

	/*
	 * If the signature specifies a public key algorithm, it *must* match
	 * the key's actual public key algorithm.
	 *
	 * Small exception: ECDSA signatures don't specify the curve, but ECDSA
	 * keys do.  So the strings can mismatch slightly in that case:
	 * "ecdsa-nist-*" for the key, but "ecdsa" for the signature.
	 */
	if (sig->pkey_algo) {
		if (strcmp(pkey->pkey_algo, sig->pkey_algo) != 0 &&
		    (strncmp(pkey->pkey_algo, "ecdsa-", 6) != 0 ||
		     strcmp(sig->pkey_algo, "ecdsa") != 0))
			return -EKEYREJECTED;
	}

	ret = software_key_determine_akcipher(pkey, sig->encoding,
					      sig->hash_algo, alg_name);
	if (ret < 0)
		return ret;

	tfm = crypto_alloc_akcipher(alg_name, 0, 0);
	if (IS_ERR(tfm))
		return PTR_ERR(tfm);

	ret = -ENOMEM;
	req = akcipher_request_alloc(tfm, GFP_KERNEL);
	if (!req)
		goto error_free_tfm;

	key = kmalloc(pkey->keylen + sizeof(u32) * 2 + pkey->paramlen,
		      GFP_KERNEL);
	if (!key)
		goto error_free_req;

	memcpy(key, pkey->key, pkey->keylen);
	ptr = key + pkey->keylen;
	ptr = pkey_pack_u32(ptr, pkey->algo);
	ptr = pkey_pack_u32(ptr, pkey->paramlen);
	memcpy(ptr, pkey->params, pkey->paramlen);

	if (pkey->key_is_private)
		ret = crypto_akcipher_set_priv_key(tfm, key, pkey->keylen);
	else
		ret = crypto_akcipher_set_pub_key(tfm, key, pkey->keylen);
	if (ret)
		goto error_free_key;

	if (strcmp(pkey->pkey_algo, "sm2") == 0 && sig->data_size) {
		ret = cert_sig_digest_update(sig, tfm);
		if (ret)
			goto error_free_key;
	}

	sg_init_table(src_sg, 2);
	sg_set_buf(&src_sg[0], sig->s, sig->s_size);
	sg_set_buf(&src_sg[1], sig->digest, sig->digest_size);
	akcipher_request_set_crypt(req, src_sg, NULL, sig->s_size,
				   sig->digest_size);
	crypto_init_wait(&cwait);
	akcipher_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG |
				      CRYPTO_TFM_REQ_MAY_SLEEP,
				      crypto_req_done, &cwait);
	ret = crypto_wait_req(crypto_akcipher_verify(req), &cwait);

error_free_key:
	kfree(key);
error_free_req:
	akcipher_request_free(req);
error_free_tfm:
	crypto_free_akcipher(tfm);
	pr_devel("<==%s() = %d\n", __func__, ret);
	if (WARN_ON_ONCE(ret > 0))
		ret = -EINVAL;
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
