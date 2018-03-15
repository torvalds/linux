#include <crypto/cryptodev.h>
#include <stdio.h>
#include <string.h>
#include <openssl/aes.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>

//#define DEBUG

#ifdef DEBUG
#  define dbgp(...) { \
	fprintf(stderr, "%s:%d: ", __FILE__, __LINE__); \
	fprintf(stderr, __VA_ARGS__); \
	fprintf(stderr, "\n"); \
}
#else
#  define dbgp(...) /* nothing */
#endif

enum ctx_type {
	ctx_type_none = 0,
	ctx_type_hmac,
	ctx_type_md,
};

union openssl_ctx {
	HMAC_CTX hmac;
	EVP_MD_CTX md;
};

struct ctx_mapping {
	__u32 ses;
	enum ctx_type type;
	union openssl_ctx ctx;
};

static struct ctx_mapping ctx_map[512];

static struct ctx_mapping *find_mapping(__u32 ses)
{
	int i;

	for (i = 0; i < 512; i++) {
		if (ctx_map[i].ses == ses)
			return &ctx_map[i];
	}
	return NULL;
}

static struct ctx_mapping *new_mapping(void)
{
	return find_mapping(0);
}

static void remove_mapping(__u32 ses)
{
	struct ctx_mapping *mapping;

	if (!(mapping = find_mapping(ses))) {
		printf("%s: failed to find mapping for session %d\n", __func__, ses);
		return;
	}
	switch (mapping->type) {
	case ctx_type_none:
		break;
	case ctx_type_hmac:
		dbgp("%s: calling HMAC_CTX_cleanup\n", __func__);
		HMAC_CTX_cleanup(&mapping->ctx.hmac);
		break;
	case ctx_type_md:
		dbgp("%s: calling EVP_MD_CTX_cleanup\n", __func__);
		EVP_MD_CTX_cleanup(&mapping->ctx.md);
		break;
	}
	memset(mapping, 0, sizeof(*mapping));
}

static union openssl_ctx *__ses_to_ctx(__u32 ses)
{
	struct ctx_mapping *mapping;

	if (!(mapping = find_mapping(ses)))
		return NULL;
	return &mapping->ctx;
}

static HMAC_CTX *ses_to_hmac(__u32 ses) { return (HMAC_CTX *)__ses_to_ctx(ses); }
static EVP_MD_CTX *ses_to_md(__u32 ses) { return (EVP_MD_CTX *)__ses_to_ctx(ses); }

static const EVP_MD *sess_to_evp_md(struct session_op *sess)
{
	switch (sess->mac) {
#ifndef OPENSSL_NO_MD5
	case CRYPTO_MD5_HMAC: return EVP_md5();
#endif
#ifndef OPENSSL_NO_SHA
	case CRYPTO_SHA1_HMAC:
	case CRYPTO_SHA1:
		return EVP_sha1();
#endif
#ifndef OPENSSL_NO_RIPEMD
	case CRYPTO_RIPEMD160_HMAC: return EVP_ripemd160();
#endif
#ifndef OPENSSL_NO_SHA256
	case CRYPTO_SHA2_256_HMAC: return EVP_sha256();
#endif
#ifndef OPENSSL_NO_SHA512
	case CRYPTO_SHA2_384_HMAC: return EVP_sha384();
	case CRYPTO_SHA2_512_HMAC: return EVP_sha512();
#endif
	default:
		printf("%s: failed to get an EVP, things will be broken!\n", __func__);
		return NULL;
	}
}

static int openssl_hmac(struct session_op *sess, struct crypt_op *cop)
{
	HMAC_CTX *ctx = ses_to_hmac(sess->ses);

	if (!ctx) {
		struct ctx_mapping *mapping = new_mapping();
		if (!mapping) {
			printf("%s: failed to get new mapping\n", __func__);
			return 1;
		}

		mapping->ses = sess->ses;
		mapping->type = ctx_type_hmac;
		ctx = &mapping->ctx.hmac;

		dbgp("calling HMAC_CTX_init");
		HMAC_CTX_init(ctx);
		dbgp("calling HMAC_Init_ex");
		if (!HMAC_Init_ex(ctx, sess->mackey, sess->mackeylen,
				sess_to_evp_md(sess), NULL)) {
			printf("%s: HMAC_Init_ex failed\n", __func__);
			return 1;
		}
	}

	if (cop->len) {
		dbgp("calling HMAC_Update");
		if (!HMAC_Update(ctx, cop->src, cop->len)) {
			printf("%s: HMAC_Update failed\n", __func__);
			return 1;
		}
	}
	if (cop->flags & COP_FLAG_FINAL ||
	    (cop->len && !(cop->flags & COP_FLAG_UPDATE))) {
		dbgp("calling HMAC_Final");
		if (!HMAC_Final(ctx, cop->mac, 0)) {
			printf("%s: HMAC_Final failed\n", __func__);
			remove_mapping(sess->ses);
			return 1;
		}
		remove_mapping(sess->ses);
	}
	return 0;
}

static int openssl_md(struct session_op *sess, struct crypt_op *cop)
{
	EVP_MD_CTX *ctx = ses_to_md(sess->ses);

	if (!ctx) {
		struct ctx_mapping *mapping = new_mapping();
		if (!mapping) {
			printf("%s: failed to get new mapping\n", __func__);
			return 1;
		}

		mapping->ses = sess->ses;
		mapping->type = ctx_type_md;
		ctx = &mapping->ctx.md;

		dbgp("calling EVP_MD_CTX_init");
		EVP_MD_CTX_init(ctx);
		dbgp("calling EVP_DigestInit");
		EVP_DigestInit(ctx, sess_to_evp_md(sess));
	}

	if (cop->len) {
		dbgp("calling EVP_DigestUpdate");
		EVP_DigestUpdate(ctx, cop->src, cop->len);
	}
	if (cop->flags & COP_FLAG_FINAL ||
	    (cop->len && !(cop->flags & COP_FLAG_UPDATE))) {
		dbgp("calling EVP_DigestFinal");
		EVP_DigestFinal(ctx, cop->mac, 0);
		remove_mapping(sess->ses);
	}

	return 0;
}

static int openssl_aes(struct session_op *sess, struct crypt_op *cop)
{
	AES_KEY key;
	int i, enc;
	unsigned char ivec[AES_BLOCK_SIZE];

	if (cop->len % AES_BLOCK_SIZE) {
		printf("%s: illegal length passed, "
		       "not a multiple of AES_BLOCK_SIZE\n", __func__);
		return 1;
	}

	switch (cop->op) {
	case COP_ENCRYPT:
		AES_set_encrypt_key(sess->key, sess->keylen * 8, &key);
		enc = 1;
		break;
	case COP_DECRYPT:
		AES_set_decrypt_key(sess->key, sess->keylen * 8, &key);
		enc = 0;
		break;
	default:
		printf("%s: unknown cop->op received!\n", __func__);
		return 1;
	}

	switch (sess->cipher) {
	case CRYPTO_AES_CBC:
		memcpy(ivec, cop->iv, AES_BLOCK_SIZE);
		AES_cbc_encrypt(cop->src, cop->dst, cop->len, &key, ivec, enc);
		if (cop->flags & COP_FLAG_WRITE_IV)
			memcpy(cop->iv, ivec, AES_BLOCK_SIZE);
		break;
#if 0
	/* XXX: TODO: implement this stuff */
	case CRYPTO_AES_CTR:
		AES_ctr128_encrypt(cop->src, cop->dst, &key, cop->iv,
	case CRYPTO_AES_XTS:
#endif
	case CRYPTO_AES_ECB:
		for (i = 0; i < cop->len; i += AES_BLOCK_SIZE)
			AES_ecb_encrypt(cop->src + i, cop->dst + i, &key, enc);
		break;
	}
	return 0;
}

int openssl_cioccrypt(struct session_op *sess, struct crypt_op *cop)
{
	if (sess->mac && sess->mackey && sess->mackeylen)
		openssl_hmac(sess, cop);
	else if (sess->mac)
		openssl_md(sess, cop);

	switch (sess->cipher) {
	case CRYPTO_AES_CBC:
	case CRYPTO_AES_CTR:
	case CRYPTO_AES_XTS:
	case CRYPTO_AES_ECB:
		openssl_aes(sess, cop);
		break;
	case 0:
		/* no encryption wanted, everythings fine */
		break;
	default:
		printf("%s: unknown cipher passed!\n", __func__);
		break;
	}

	return 0;
}
