// SPDX-License-Identifier: GPL-2.0
#define pr_fmt(fmt) "ASYM-TPM: "fmt
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/seq_file.h>
#include <linux/scatterlist.h>
#include <linux/tpm.h>
#include <crypto/akcipher.h>
#include <asm/unaligned.h>
#include <keys/asymmetric-subtype.h>
#include <crypto/asym_tpm_subtype.h>

/*
 * Maximum buffer size for the BER/DER encoded public key.  The public key
 * is of the form SEQUENCE { INTEGER n, INTEGER e } where n is a maximum 2048
 * bit key and e is usually 65537
 * The encoding overhead is:
 * - max 4 bytes for SEQUENCE
 *   - max 4 bytes for INTEGER n type/length
 *     - 257 bytes of n
 *   - max 2 bytes for INTEGER e type/length
 *     - 3 bytes of e
 */
#define PUB_KEY_BUF_SIZE (4 + 4 + 257 + 2 + 3)

/*
 * Provide a part of a description of the key for /proc/keys.
 */
static void asym_tpm_describe(const struct key *asymmetric_key,
			      struct seq_file *m)
{
	struct tpm_key *tk = asymmetric_key->payload.data[asym_crypto];

	if (!tk)
		return;

	seq_printf(m, "TPM1.2/Blob");
}

static void asym_tpm_destroy(void *payload0, void *payload3)
{
	struct tpm_key *tk = payload0;

	if (!tk)
		return;

	kfree(tk->blob);
	tk->blob_len = 0;

	kfree(tk);
}

/* How many bytes will it take to encode the length */
static inline uint32_t definite_length(uint32_t len)
{
	if (len <= 127)
		return 1;
	if (len <= 255)
		return 2;
	return 3;
}

static inline uint8_t *encode_tag_length(uint8_t *buf, uint8_t tag,
					 uint32_t len)
{
	*buf++ = tag;

	if (len <= 127) {
		buf[0] = len;
		return buf + 1;
	}

	if (len <= 255) {
		buf[0] = 0x81;
		buf[1] = len;
		return buf + 2;
	}

	buf[0] = 0x82;
	put_unaligned_be16(len, buf + 1);
	return buf + 3;
}

static uint32_t derive_pub_key(const void *pub_key, uint32_t len, uint8_t *buf)
{
	uint8_t *cur = buf;
	uint32_t n_len = definite_length(len) + 1 + len + 1;
	uint32_t e_len = definite_length(3) + 1 + 3;
	uint8_t e[3] = { 0x01, 0x00, 0x01 };

	/* SEQUENCE */
	cur = encode_tag_length(cur, 0x30, n_len + e_len);
	/* INTEGER n */
	cur = encode_tag_length(cur, 0x02, len + 1);
	cur[0] = 0x00;
	memcpy(cur + 1, pub_key, len);
	cur += len + 1;
	cur = encode_tag_length(cur, 0x02, sizeof(e));
	memcpy(cur, e, sizeof(e));
	cur += sizeof(e);

	return cur - buf;
}

/*
 * Determine the crypto algorithm name.
 */
static int determine_akcipher(const char *encoding, const char *hash_algo,
			      char alg_name[CRYPTO_MAX_ALG_NAME])
{
	/* TODO: We don't support hashing yet */
	if (hash_algo)
		return -ENOPKG;

	if (strcmp(encoding, "pkcs1") == 0) {
		strcpy(alg_name, "pkcs1pad(rsa)");
		return 0;
	}

	if (strcmp(encoding, "raw") == 0) {
		strcpy(alg_name, "rsa");
		return 0;
	}

	return -ENOPKG;
}

/*
 * Query information about a key.
 */
static int tpm_key_query(const struct kernel_pkey_params *params,
			 struct kernel_pkey_query *info)
{
	struct tpm_key *tk = params->key->payload.data[asym_crypto];
	int ret;
	char alg_name[CRYPTO_MAX_ALG_NAME];
	struct crypto_akcipher *tfm;
	uint8_t der_pub_key[PUB_KEY_BUF_SIZE];
	uint32_t der_pub_key_len;
	int len;

	/* TPM only works on private keys, public keys still done in software */
	ret = determine_akcipher(params->encoding, params->hash_algo, alg_name);
	if (ret < 0)
		return ret;

	tfm = crypto_alloc_akcipher(alg_name, 0, 0);
	if (IS_ERR(tfm))
		return PTR_ERR(tfm);

	der_pub_key_len = derive_pub_key(tk->pub_key, tk->pub_key_len,
					 der_pub_key);

	ret = crypto_akcipher_set_pub_key(tfm, der_pub_key, der_pub_key_len);
	if (ret < 0)
		goto error_free_tfm;

	len = crypto_akcipher_maxsize(tfm);

	info->key_size = tk->key_len;
	info->max_data_size = tk->key_len / 8;
	info->max_sig_size = len;
	info->max_enc_size = len;
	info->max_dec_size = tk->key_len / 8;

	info->supported_ops = KEYCTL_SUPPORTS_ENCRYPT;

	ret = 0;
error_free_tfm:
	crypto_free_akcipher(tfm);
	pr_devel("<==%s() = %d\n", __func__, ret);
	return ret;
}

/*
 * Encryption operation is performed with the public key.  Hence it is done
 * in software
 */
static int tpm_key_encrypt(struct tpm_key *tk,
			   struct kernel_pkey_params *params,
			   const void *in, void *out)
{
	char alg_name[CRYPTO_MAX_ALG_NAME];
	struct crypto_akcipher *tfm;
	struct akcipher_request *req;
	struct crypto_wait cwait;
	struct scatterlist in_sg, out_sg;
	uint8_t der_pub_key[PUB_KEY_BUF_SIZE];
	uint32_t der_pub_key_len;
	int ret;

	pr_devel("==>%s()\n", __func__);

	ret = determine_akcipher(params->encoding, params->hash_algo, alg_name);
	if (ret < 0)
		return ret;

	tfm = crypto_alloc_akcipher(alg_name, 0, 0);
	if (IS_ERR(tfm))
		return PTR_ERR(tfm);

	der_pub_key_len = derive_pub_key(tk->pub_key, tk->pub_key_len,
					 der_pub_key);

	ret = crypto_akcipher_set_pub_key(tfm, der_pub_key, der_pub_key_len);
	if (ret < 0)
		goto error_free_tfm;

	req = akcipher_request_alloc(tfm, GFP_KERNEL);
	if (!req)
		goto error_free_tfm;

	sg_init_one(&in_sg, in, params->in_len);
	sg_init_one(&out_sg, out, params->out_len);
	akcipher_request_set_crypt(req, &in_sg, &out_sg, params->in_len,
				   params->out_len);
	crypto_init_wait(&cwait);
	akcipher_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG |
				      CRYPTO_TFM_REQ_MAY_SLEEP,
				      crypto_req_done, &cwait);

	ret = crypto_akcipher_encrypt(req);
	ret = crypto_wait_req(ret, &cwait);

	if (ret == 0)
		ret = req->dst_len;

	akcipher_request_free(req);
error_free_tfm:
	crypto_free_akcipher(tfm);
	pr_devel("<==%s() = %d\n", __func__, ret);
	return ret;
}

/*
 * Do encryption, decryption and signing ops.
 */
static int tpm_key_eds_op(struct kernel_pkey_params *params,
			  const void *in, void *out)
{
	struct tpm_key *tk = params->key->payload.data[asym_crypto];
	int ret = -EOPNOTSUPP;

	/* Perform the encryption calculation. */
	switch (params->op) {
	case kernel_pkey_encrypt:
		ret = tpm_key_encrypt(tk, params, in, out);
		break;
	default:
		BUG();
	}

	return ret;
}

/*
 * Parse enough information out of TPM_KEY structure:
 * TPM_STRUCT_VER -> 4 bytes
 * TPM_KEY_USAGE -> 2 bytes
 * TPM_KEY_FLAGS -> 4 bytes
 * TPM_AUTH_DATA_USAGE -> 1 byte
 * TPM_KEY_PARMS -> variable
 * UINT32 PCRInfoSize -> 4 bytes
 * BYTE* -> PCRInfoSize bytes
 * TPM_STORE_PUBKEY
 * UINT32 encDataSize;
 * BYTE* -> encDataSize;
 *
 * TPM_KEY_PARMS:
 * TPM_ALGORITHM_ID -> 4 bytes
 * TPM_ENC_SCHEME -> 2 bytes
 * TPM_SIG_SCHEME -> 2 bytes
 * UINT32 parmSize -> 4 bytes
 * BYTE* -> variable
 */
static int extract_key_parameters(struct tpm_key *tk)
{
	const void *cur = tk->blob;
	uint32_t len = tk->blob_len;
	const void *pub_key;
	uint32_t sz;
	uint32_t key_len;

	if (len < 11)
		return -EBADMSG;

	/* Ensure this is a legacy key */
	if (get_unaligned_be16(cur + 4) != 0x0015)
		return -EBADMSG;

	/* Skip to TPM_KEY_PARMS */
	cur += 11;
	len -= 11;

	if (len < 12)
		return -EBADMSG;

	/* Make sure this is an RSA key */
	if (get_unaligned_be32(cur) != 0x00000001)
		return -EBADMSG;

	/* Make sure this is TPM_ES_RSAESPKCSv15 encoding scheme */
	if (get_unaligned_be16(cur + 4) != 0x0002)
		return -EBADMSG;

	/* Make sure this is TPM_SS_RSASSAPKCS1v15_DER signature scheme */
	if (get_unaligned_be16(cur + 6) != 0x0003)
		return -EBADMSG;

	sz = get_unaligned_be32(cur + 8);
	if (len < sz + 12)
		return -EBADMSG;

	/* Move to TPM_RSA_KEY_PARMS */
	len -= 12;
	cur += 12;

	/* Grab the RSA key length */
	key_len = get_unaligned_be32(cur);

	switch (key_len) {
	case 512:
	case 1024:
	case 1536:
	case 2048:
		break;
	default:
		return -EINVAL;
	}

	/* Move just past TPM_KEY_PARMS */
	cur += sz;
	len -= sz;

	if (len < 4)
		return -EBADMSG;

	sz = get_unaligned_be32(cur);
	if (len < 4 + sz)
		return -EBADMSG;

	/* Move to TPM_STORE_PUBKEY */
	cur += 4 + sz;
	len -= 4 + sz;

	/* Grab the size of the public key, it should jive with the key size */
	sz = get_unaligned_be32(cur);
	if (sz > 256)
		return -EINVAL;

	pub_key = cur + 4;

	tk->key_len = key_len;
	tk->pub_key = pub_key;
	tk->pub_key_len = sz;

	return 0;
}

/* Given the blob, parse it and load it into the TPM */
struct tpm_key *tpm_key_create(const void *blob, uint32_t blob_len)
{
	int r;
	struct tpm_key *tk;

	r = tpm_is_tpm2(NULL);
	if (r < 0)
		goto error;

	/* We don't support TPM2 yet */
	if (r > 0) {
		r = -ENODEV;
		goto error;
	}

	r = -ENOMEM;
	tk = kzalloc(sizeof(struct tpm_key), GFP_KERNEL);
	if (!tk)
		goto error;

	tk->blob = kmemdup(blob, blob_len, GFP_KERNEL);
	if (!tk->blob)
		goto error_memdup;

	tk->blob_len = blob_len;

	r = extract_key_parameters(tk);
	if (r < 0)
		goto error_extract;

	return tk;

error_extract:
	kfree(tk->blob);
	tk->blob_len = 0;
error_memdup:
	kfree(tk);
error:
	return ERR_PTR(r);
}
EXPORT_SYMBOL_GPL(tpm_key_create);

/*
 * TPM-based asymmetric key subtype
 */
struct asymmetric_key_subtype asym_tpm_subtype = {
	.owner			= THIS_MODULE,
	.name			= "asym_tpm",
	.name_len		= sizeof("asym_tpm") - 1,
	.describe		= asym_tpm_describe,
	.destroy		= asym_tpm_destroy,
	.query			= tpm_key_query,
	.eds_op			= tpm_key_eds_op,
};
EXPORT_SYMBOL_GPL(asym_tpm_subtype);

MODULE_DESCRIPTION("TPM based asymmetric key subtype");
MODULE_AUTHOR("Intel Corporation");
MODULE_LICENSE("GPL v2");
