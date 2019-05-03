// SPDX-License-Identifier: GPL-2.0
#define pr_fmt(fmt) "ASYM-TPM: "fmt
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/seq_file.h>
#include <linux/scatterlist.h>
#include <linux/tpm.h>
#include <linux/tpm_command.h>
#include <crypto/akcipher.h>
#include <crypto/hash.h>
#include <crypto/sha.h>
#include <asm/unaligned.h>
#include <keys/asymmetric-subtype.h>
#include <keys/trusted.h>
#include <crypto/asym_tpm_subtype.h>
#include <crypto/public_key.h>

#define TPM_ORD_FLUSHSPECIFIC	186
#define TPM_ORD_LOADKEY2	65
#define TPM_ORD_UNBIND		30
#define TPM_ORD_SIGN		60
#define TPM_LOADKEY2_SIZE		59
#define TPM_FLUSHSPECIFIC_SIZE		18
#define TPM_UNBIND_SIZE			63
#define TPM_SIGN_SIZE			63

#define TPM_RT_KEY                      0x00000001

/*
 * Load a TPM key from the blob provided by userspace
 */
static int tpm_loadkey2(struct tpm_buf *tb,
			uint32_t keyhandle, unsigned char *keyauth,
			const unsigned char *keyblob, int keybloblen,
			uint32_t *newhandle)
{
	unsigned char nonceodd[TPM_NONCE_SIZE];
	unsigned char enonce[TPM_NONCE_SIZE];
	unsigned char authdata[SHA1_DIGEST_SIZE];
	uint32_t authhandle = 0;
	unsigned char cont = 0;
	uint32_t ordinal;
	int ret;

	ordinal = htonl(TPM_ORD_LOADKEY2);

	/* session for loading the key */
	ret = oiap(tb, &authhandle, enonce);
	if (ret < 0) {
		pr_info("oiap failed (%d)\n", ret);
		return ret;
	}

	/* generate odd nonce */
	ret = tpm_get_random(NULL, nonceodd, TPM_NONCE_SIZE);
	if (ret < 0) {
		pr_info("tpm_get_random failed (%d)\n", ret);
		return ret;
	}

	/* calculate authorization HMAC value */
	ret = TSS_authhmac(authdata, keyauth, SHA1_DIGEST_SIZE, enonce,
			   nonceodd, cont, sizeof(uint32_t), &ordinal,
			   keybloblen, keyblob, 0, 0);
	if (ret < 0)
		return ret;

	/* build the request buffer */
	INIT_BUF(tb);
	store16(tb, TPM_TAG_RQU_AUTH1_COMMAND);
	store32(tb, TPM_LOADKEY2_SIZE + keybloblen);
	store32(tb, TPM_ORD_LOADKEY2);
	store32(tb, keyhandle);
	storebytes(tb, keyblob, keybloblen);
	store32(tb, authhandle);
	storebytes(tb, nonceodd, TPM_NONCE_SIZE);
	store8(tb, cont);
	storebytes(tb, authdata, SHA1_DIGEST_SIZE);

	ret = trusted_tpm_send(tb->data, MAX_BUF_SIZE);
	if (ret < 0) {
		pr_info("authhmac failed (%d)\n", ret);
		return ret;
	}

	ret = TSS_checkhmac1(tb->data, ordinal, nonceodd, keyauth,
			     SHA1_DIGEST_SIZE, 0, 0);
	if (ret < 0) {
		pr_info("TSS_checkhmac1 failed (%d)\n", ret);
		return ret;
	}

	*newhandle = LOAD32(tb->data, TPM_DATA_OFFSET);
	return 0;
}

/*
 * Execute the FlushSpecific TPM command
 */
static int tpm_flushspecific(struct tpm_buf *tb, uint32_t handle)
{
	INIT_BUF(tb);
	store16(tb, TPM_TAG_RQU_COMMAND);
	store32(tb, TPM_FLUSHSPECIFIC_SIZE);
	store32(tb, TPM_ORD_FLUSHSPECIFIC);
	store32(tb, handle);
	store32(tb, TPM_RT_KEY);

	return trusted_tpm_send(tb->data, MAX_BUF_SIZE);
}

/*
 * Decrypt a blob provided by userspace using a specific key handle.
 * The handle is a well known handle or previously loaded by e.g. LoadKey2
 */
static int tpm_unbind(struct tpm_buf *tb,
			uint32_t keyhandle, unsigned char *keyauth,
			const unsigned char *blob, uint32_t bloblen,
			void *out, uint32_t outlen)
{
	unsigned char nonceodd[TPM_NONCE_SIZE];
	unsigned char enonce[TPM_NONCE_SIZE];
	unsigned char authdata[SHA1_DIGEST_SIZE];
	uint32_t authhandle = 0;
	unsigned char cont = 0;
	uint32_t ordinal;
	uint32_t datalen;
	int ret;

	ordinal = htonl(TPM_ORD_UNBIND);
	datalen = htonl(bloblen);

	/* session for loading the key */
	ret = oiap(tb, &authhandle, enonce);
	if (ret < 0) {
		pr_info("oiap failed (%d)\n", ret);
		return ret;
	}

	/* generate odd nonce */
	ret = tpm_get_random(NULL, nonceodd, TPM_NONCE_SIZE);
	if (ret < 0) {
		pr_info("tpm_get_random failed (%d)\n", ret);
		return ret;
	}

	/* calculate authorization HMAC value */
	ret = TSS_authhmac(authdata, keyauth, SHA1_DIGEST_SIZE, enonce,
			   nonceodd, cont, sizeof(uint32_t), &ordinal,
			   sizeof(uint32_t), &datalen,
			   bloblen, blob, 0, 0);
	if (ret < 0)
		return ret;

	/* build the request buffer */
	INIT_BUF(tb);
	store16(tb, TPM_TAG_RQU_AUTH1_COMMAND);
	store32(tb, TPM_UNBIND_SIZE + bloblen);
	store32(tb, TPM_ORD_UNBIND);
	store32(tb, keyhandle);
	store32(tb, bloblen);
	storebytes(tb, blob, bloblen);
	store32(tb, authhandle);
	storebytes(tb, nonceodd, TPM_NONCE_SIZE);
	store8(tb, cont);
	storebytes(tb, authdata, SHA1_DIGEST_SIZE);

	ret = trusted_tpm_send(tb->data, MAX_BUF_SIZE);
	if (ret < 0) {
		pr_info("authhmac failed (%d)\n", ret);
		return ret;
	}

	datalen = LOAD32(tb->data, TPM_DATA_OFFSET);

	ret = TSS_checkhmac1(tb->data, ordinal, nonceodd,
			     keyauth, SHA1_DIGEST_SIZE,
			     sizeof(uint32_t), TPM_DATA_OFFSET,
			     datalen, TPM_DATA_OFFSET + sizeof(uint32_t),
			     0, 0);
	if (ret < 0) {
		pr_info("TSS_checkhmac1 failed (%d)\n", ret);
		return ret;
	}

	memcpy(out, tb->data + TPM_DATA_OFFSET + sizeof(uint32_t),
	       min(outlen, datalen));

	return datalen;
}

/*
 * Sign a blob provided by userspace (that has had the hash function applied)
 * using a specific key handle.  The handle is assumed to have been previously
 * loaded by e.g. LoadKey2.
 *
 * Note that the key signature scheme of the used key should be set to
 * TPM_SS_RSASSAPKCS1v15_DER.  This allows the hashed input to be of any size
 * up to key_length_in_bytes - 11 and not be limited to size 20 like the
 * TPM_SS_RSASSAPKCS1v15_SHA1 signature scheme.
 */
static int tpm_sign(struct tpm_buf *tb,
		    uint32_t keyhandle, unsigned char *keyauth,
		    const unsigned char *blob, uint32_t bloblen,
		    void *out, uint32_t outlen)
{
	unsigned char nonceodd[TPM_NONCE_SIZE];
	unsigned char enonce[TPM_NONCE_SIZE];
	unsigned char authdata[SHA1_DIGEST_SIZE];
	uint32_t authhandle = 0;
	unsigned char cont = 0;
	uint32_t ordinal;
	uint32_t datalen;
	int ret;

	ordinal = htonl(TPM_ORD_SIGN);
	datalen = htonl(bloblen);

	/* session for loading the key */
	ret = oiap(tb, &authhandle, enonce);
	if (ret < 0) {
		pr_info("oiap failed (%d)\n", ret);
		return ret;
	}

	/* generate odd nonce */
	ret = tpm_get_random(NULL, nonceodd, TPM_NONCE_SIZE);
	if (ret < 0) {
		pr_info("tpm_get_random failed (%d)\n", ret);
		return ret;
	}

	/* calculate authorization HMAC value */
	ret = TSS_authhmac(authdata, keyauth, SHA1_DIGEST_SIZE, enonce,
			   nonceodd, cont, sizeof(uint32_t), &ordinal,
			   sizeof(uint32_t), &datalen,
			   bloblen, blob, 0, 0);
	if (ret < 0)
		return ret;

	/* build the request buffer */
	INIT_BUF(tb);
	store16(tb, TPM_TAG_RQU_AUTH1_COMMAND);
	store32(tb, TPM_SIGN_SIZE + bloblen);
	store32(tb, TPM_ORD_SIGN);
	store32(tb, keyhandle);
	store32(tb, bloblen);
	storebytes(tb, blob, bloblen);
	store32(tb, authhandle);
	storebytes(tb, nonceodd, TPM_NONCE_SIZE);
	store8(tb, cont);
	storebytes(tb, authdata, SHA1_DIGEST_SIZE);

	ret = trusted_tpm_send(tb->data, MAX_BUF_SIZE);
	if (ret < 0) {
		pr_info("authhmac failed (%d)\n", ret);
		return ret;
	}

	datalen = LOAD32(tb->data, TPM_DATA_OFFSET);

	ret = TSS_checkhmac1(tb->data, ordinal, nonceodd,
			     keyauth, SHA1_DIGEST_SIZE,
			     sizeof(uint32_t), TPM_DATA_OFFSET,
			     datalen, TPM_DATA_OFFSET + sizeof(uint32_t),
			     0, 0);
	if (ret < 0) {
		pr_info("TSS_checkhmac1 failed (%d)\n", ret);
		return ret;
	}

	memcpy(out, tb->data + TPM_DATA_OFFSET + sizeof(uint32_t),
	       min(datalen, outlen));

	return datalen;
}
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
	if (strcmp(encoding, "pkcs1") == 0) {
		if (!hash_algo) {
			strcpy(alg_name, "pkcs1pad(rsa)");
			return 0;
		}

		if (snprintf(alg_name, CRYPTO_MAX_ALG_NAME, "pkcs1pad(rsa,%s)",
			     hash_algo) >= CRYPTO_MAX_ALG_NAME)
			return -EINVAL;

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

	info->supported_ops = KEYCTL_SUPPORTS_ENCRYPT |
			      KEYCTL_SUPPORTS_DECRYPT |
			      KEYCTL_SUPPORTS_VERIFY |
			      KEYCTL_SUPPORTS_SIGN;

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
 * Decryption operation is performed with the private key in the TPM.
 */
static int tpm_key_decrypt(struct tpm_key *tk,
			   struct kernel_pkey_params *params,
			   const void *in, void *out)
{
	struct tpm_buf *tb;
	uint32_t keyhandle;
	uint8_t srkauth[SHA1_DIGEST_SIZE];
	uint8_t keyauth[SHA1_DIGEST_SIZE];
	int r;

	pr_devel("==>%s()\n", __func__);

	if (params->hash_algo)
		return -ENOPKG;

	if (strcmp(params->encoding, "pkcs1"))
		return -ENOPKG;

	tb = kzalloc(sizeof(*tb), GFP_KERNEL);
	if (!tb)
		return -ENOMEM;

	/* TODO: Handle a non-all zero SRK authorization */
	memset(srkauth, 0, sizeof(srkauth));

	r = tpm_loadkey2(tb, SRKHANDLE, srkauth,
				tk->blob, tk->blob_len, &keyhandle);
	if (r < 0) {
		pr_devel("loadkey2 failed (%d)\n", r);
		goto error;
	}

	/* TODO: Handle a non-all zero key authorization */
	memset(keyauth, 0, sizeof(keyauth));

	r = tpm_unbind(tb, keyhandle, keyauth,
		       in, params->in_len, out, params->out_len);
	if (r < 0)
		pr_devel("tpm_unbind failed (%d)\n", r);

	if (tpm_flushspecific(tb, keyhandle) < 0)
		pr_devel("flushspecific failed (%d)\n", r);

error:
	kzfree(tb);
	pr_devel("<==%s() = %d\n", __func__, r);
	return r;
}

/*
 * Hash algorithm OIDs plus ASN.1 DER wrappings [RFC4880 sec 5.2.2].
 */
static const u8 digest_info_md5[] = {
	0x30, 0x20, 0x30, 0x0c, 0x06, 0x08,
	0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x02, 0x05, /* OID */
	0x05, 0x00, 0x04, 0x10
};

static const u8 digest_info_sha1[] = {
	0x30, 0x21, 0x30, 0x09, 0x06, 0x05,
	0x2b, 0x0e, 0x03, 0x02, 0x1a,
	0x05, 0x00, 0x04, 0x14
};

static const u8 digest_info_rmd160[] = {
	0x30, 0x21, 0x30, 0x09, 0x06, 0x05,
	0x2b, 0x24, 0x03, 0x02, 0x01,
	0x05, 0x00, 0x04, 0x14
};

static const u8 digest_info_sha224[] = {
	0x30, 0x2d, 0x30, 0x0d, 0x06, 0x09,
	0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x04,
	0x05, 0x00, 0x04, 0x1c
};

static const u8 digest_info_sha256[] = {
	0x30, 0x31, 0x30, 0x0d, 0x06, 0x09,
	0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x01,
	0x05, 0x00, 0x04, 0x20
};

static const u8 digest_info_sha384[] = {
	0x30, 0x41, 0x30, 0x0d, 0x06, 0x09,
	0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x02,
	0x05, 0x00, 0x04, 0x30
};

static const u8 digest_info_sha512[] = {
	0x30, 0x51, 0x30, 0x0d, 0x06, 0x09,
	0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x03,
	0x05, 0x00, 0x04, 0x40
};

static const struct asn1_template {
	const char	*name;
	const u8	*data;
	size_t		size;
} asn1_templates[] = {
#define _(X) { #X, digest_info_##X, sizeof(digest_info_##X) }
	_(md5),
	_(sha1),
	_(rmd160),
	_(sha256),
	_(sha384),
	_(sha512),
	_(sha224),
	{ NULL }
#undef _
};

static const struct asn1_template *lookup_asn1(const char *name)
{
	const struct asn1_template *p;

	for (p = asn1_templates; p->name; p++)
		if (strcmp(name, p->name) == 0)
			return p;
	return NULL;
}

/*
 * Sign operation is performed with the private key in the TPM.
 */
static int tpm_key_sign(struct tpm_key *tk,
			struct kernel_pkey_params *params,
			const void *in, void *out)
{
	struct tpm_buf *tb;
	uint32_t keyhandle;
	uint8_t srkauth[SHA1_DIGEST_SIZE];
	uint8_t keyauth[SHA1_DIGEST_SIZE];
	void *asn1_wrapped = NULL;
	uint32_t in_len = params->in_len;
	int r;

	pr_devel("==>%s()\n", __func__);

	if (strcmp(params->encoding, "pkcs1"))
		return -ENOPKG;

	if (params->hash_algo) {
		const struct asn1_template *asn1 =
						lookup_asn1(params->hash_algo);

		if (!asn1)
			return -ENOPKG;

		/* request enough space for the ASN.1 template + input hash */
		asn1_wrapped = kzalloc(in_len + asn1->size, GFP_KERNEL);
		if (!asn1_wrapped)
			return -ENOMEM;

		/* Copy ASN.1 template, then the input */
		memcpy(asn1_wrapped, asn1->data, asn1->size);
		memcpy(asn1_wrapped + asn1->size, in, in_len);

		in = asn1_wrapped;
		in_len += asn1->size;
	}

	if (in_len > tk->key_len / 8 - 11) {
		r = -EOVERFLOW;
		goto error_free_asn1_wrapped;
	}

	r = -ENOMEM;
	tb = kzalloc(sizeof(*tb), GFP_KERNEL);
	if (!tb)
		goto error_free_asn1_wrapped;

	/* TODO: Handle a non-all zero SRK authorization */
	memset(srkauth, 0, sizeof(srkauth));

	r = tpm_loadkey2(tb, SRKHANDLE, srkauth,
			 tk->blob, tk->blob_len, &keyhandle);
	if (r < 0) {
		pr_devel("loadkey2 failed (%d)\n", r);
		goto error_free_tb;
	}

	/* TODO: Handle a non-all zero key authorization */
	memset(keyauth, 0, sizeof(keyauth));

	r = tpm_sign(tb, keyhandle, keyauth, in, in_len, out, params->out_len);
	if (r < 0)
		pr_devel("tpm_sign failed (%d)\n", r);

	if (tpm_flushspecific(tb, keyhandle) < 0)
		pr_devel("flushspecific failed (%d)\n", r);

error_free_tb:
	kzfree(tb);
error_free_asn1_wrapped:
	kfree(asn1_wrapped);
	pr_devel("<==%s() = %d\n", __func__, r);
	return r;
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
	case kernel_pkey_decrypt:
		ret = tpm_key_decrypt(tk, params, in, out);
		break;
	case kernel_pkey_sign:
		ret = tpm_key_sign(tk, params, in, out);
		break;
	default:
		BUG();
	}

	return ret;
}

/*
 * Verify a signature using a public key.
 */
static int tpm_key_verify_signature(const struct key *key,
				    const struct public_key_signature *sig)
{
	const struct tpm_key *tk = key->payload.data[asym_crypto];
	struct crypto_wait cwait;
	struct crypto_akcipher *tfm;
	struct akcipher_request *req;
	struct scatterlist sig_sg, digest_sg;
	char alg_name[CRYPTO_MAX_ALG_NAME];
	uint8_t der_pub_key[PUB_KEY_BUF_SIZE];
	uint32_t der_pub_key_len;
	void *output;
	unsigned int outlen;
	int ret;

	pr_devel("==>%s()\n", __func__);

	BUG_ON(!tk);
	BUG_ON(!sig);
	BUG_ON(!sig->s);

	if (!sig->digest)
		return -ENOPKG;

	ret = determine_akcipher(sig->encoding, sig->hash_algo, alg_name);
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

	ret = -ENOMEM;
	req = akcipher_request_alloc(tfm, GFP_KERNEL);
	if (!req)
		goto error_free_tfm;

	ret = -ENOMEM;
	outlen = crypto_akcipher_maxsize(tfm);
	output = kmalloc(outlen, GFP_KERNEL);
	if (!output)
		goto error_free_req;

	sg_init_one(&sig_sg, sig->s, sig->s_size);
	sg_init_one(&digest_sg, output, outlen);
	akcipher_request_set_crypt(req, &sig_sg, &digest_sg, sig->s_size,
				   outlen);
	crypto_init_wait(&cwait);
	akcipher_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG |
				      CRYPTO_TFM_REQ_MAY_SLEEP,
				      crypto_req_done, &cwait);

	/* Perform the verification calculation.  This doesn't actually do the
	 * verification, but rather calculates the hash expected by the
	 * signature and returns that to us.
	 */
	ret = crypto_wait_req(crypto_akcipher_verify(req), &cwait);
	if (ret)
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
	if (WARN_ON_ONCE(ret > 0))
		ret = -EINVAL;
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
	.verify_signature	= tpm_key_verify_signature,
};
EXPORT_SYMBOL_GPL(asym_tpm_subtype);

MODULE_DESCRIPTION("TPM based asymmetric key subtype");
MODULE_AUTHOR("Intel Corporation");
MODULE_LICENSE("GPL v2");
