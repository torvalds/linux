// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright IBM Corp. 2024
 *
 * s390 specific HMAC support.
 */

#define KMSG_COMPONENT	"hmac_s390"
#define pr_fmt(fmt)	KMSG_COMPONENT ": " fmt

#include <asm/cpacf.h>
#include <crypto/sha2.h>
#include <crypto/internal/hash.h>
#include <linux/cpufeature.h>
#include <linux/module.h>

/*
 * KMAC param block layout for sha2 function codes:
 * The layout of the param block for the KMAC instruction depends on the
 * blocksize of the used hashing sha2-algorithm function codes. The param block
 * contains the hash chaining value (cv), the input message bit-length (imbl)
 * and the hmac-secret (key). To prevent code duplication, the sizes of all
 * these are calculated based on the blocksize.
 *
 * param-block:
 * +-------+
 * | cv    |
 * +-------+
 * | imbl  |
 * +-------+
 * | key   |
 * +-------+
 *
 * sizes:
 * part | sh2-alg | calculation | size | type
 * -----+---------+-------------+------+--------
 * cv	| 224/256 | blocksize/2 |   32 |  u64[8]
 *	| 384/512 |		|   64 | u128[8]
 * imbl | 224/256 | blocksize/8 |    8 |     u64
 *	| 384/512 |		|   16 |    u128
 * key	| 224/256 | blocksize	|   64 |  u8[64]
 *	| 384/512 |		|  128 | u8[128]
 */

#define MAX_DIGEST_SIZE		SHA512_DIGEST_SIZE
#define MAX_IMBL_SIZE		sizeof(u128)
#define MAX_BLOCK_SIZE		SHA512_BLOCK_SIZE

#define SHA2_CV_SIZE(bs)	((bs) >> 1)
#define SHA2_IMBL_SIZE(bs)	((bs) >> 3)

#define SHA2_IMBL_OFFSET(bs)	(SHA2_CV_SIZE(bs))
#define SHA2_KEY_OFFSET(bs)	(SHA2_CV_SIZE(bs) + SHA2_IMBL_SIZE(bs))

struct s390_hmac_ctx {
	u8 key[MAX_BLOCK_SIZE];
};

union s390_kmac_gr0 {
	unsigned long reg;
	struct {
		unsigned long		: 48;
		unsigned long ikp	:  1;
		unsigned long iimp	:  1;
		unsigned long ccup	:  1;
		unsigned long		:  6;
		unsigned long fc	:  7;
	};
};

struct s390_kmac_sha2_ctx {
	u8 param[MAX_DIGEST_SIZE + MAX_IMBL_SIZE + MAX_BLOCK_SIZE];
	union s390_kmac_gr0 gr0;
	u8 buf[MAX_BLOCK_SIZE];
	unsigned int buflen;
};

/*
 * kmac_sha2_set_imbl - sets the input message bit-length based on the blocksize
 */
static inline void kmac_sha2_set_imbl(u8 *param, unsigned int buflen,
				      unsigned int blocksize)
{
	u8 *imbl = param + SHA2_IMBL_OFFSET(blocksize);

	switch (blocksize) {
	case SHA256_BLOCK_SIZE:
		*(u64 *)imbl = (u64)buflen * BITS_PER_BYTE;
		break;
	case SHA512_BLOCK_SIZE:
		*(u128 *)imbl = (u128)buflen * BITS_PER_BYTE;
		break;
	default:
		break;
	}
}

static int hash_key(const u8 *in, unsigned int inlen,
		    u8 *digest, unsigned int digestsize)
{
	unsigned long func;
	union {
		struct sha256_paramblock {
			u32 h[8];
			u64 mbl;
		} sha256;
		struct sha512_paramblock {
			u64 h[8];
			u128 mbl;
		} sha512;
	} __packed param;

#define PARAM_INIT(x, y, z)		   \
	param.sha##x.h[0] = SHA##y ## _H0; \
	param.sha##x.h[1] = SHA##y ## _H1; \
	param.sha##x.h[2] = SHA##y ## _H2; \
	param.sha##x.h[3] = SHA##y ## _H3; \
	param.sha##x.h[4] = SHA##y ## _H4; \
	param.sha##x.h[5] = SHA##y ## _H5; \
	param.sha##x.h[6] = SHA##y ## _H6; \
	param.sha##x.h[7] = SHA##y ## _H7; \
	param.sha##x.mbl = (z)

	switch (digestsize) {
	case SHA224_DIGEST_SIZE:
		func = CPACF_KLMD_SHA_256;
		PARAM_INIT(256, 224, inlen * 8);
		break;
	case SHA256_DIGEST_SIZE:
		func = CPACF_KLMD_SHA_256;
		PARAM_INIT(256, 256, inlen * 8);
		break;
	case SHA384_DIGEST_SIZE:
		func = CPACF_KLMD_SHA_512;
		PARAM_INIT(512, 384, inlen * 8);
		break;
	case SHA512_DIGEST_SIZE:
		func = CPACF_KLMD_SHA_512;
		PARAM_INIT(512, 512, inlen * 8);
		break;
	default:
		return -EINVAL;
	}

#undef PARAM_INIT

	cpacf_klmd(func, &param, in, inlen);

	memcpy(digest, &param, digestsize);

	return 0;
}

static int s390_hmac_sha2_setkey(struct crypto_shash *tfm,
				 const u8 *key, unsigned int keylen)
{
	struct s390_hmac_ctx *tfm_ctx = crypto_shash_ctx(tfm);
	unsigned int ds = crypto_shash_digestsize(tfm);
	unsigned int bs = crypto_shash_blocksize(tfm);

	memset(tfm_ctx, 0, sizeof(*tfm_ctx));

	if (keylen > bs)
		return hash_key(key, keylen, tfm_ctx->key, ds);

	memcpy(tfm_ctx->key, key, keylen);
	return 0;
}

static int s390_hmac_sha2_init(struct shash_desc *desc)
{
	struct s390_hmac_ctx *tfm_ctx = crypto_shash_ctx(desc->tfm);
	struct s390_kmac_sha2_ctx *ctx = shash_desc_ctx(desc);
	unsigned int bs = crypto_shash_blocksize(desc->tfm);

	memcpy(ctx->param + SHA2_KEY_OFFSET(bs),
	       tfm_ctx->key, bs);

	ctx->buflen = 0;
	ctx->gr0.reg = 0;
	switch (crypto_shash_digestsize(desc->tfm)) {
	case SHA224_DIGEST_SIZE:
		ctx->gr0.fc = CPACF_KMAC_HMAC_SHA_224;
		break;
	case SHA256_DIGEST_SIZE:
		ctx->gr0.fc = CPACF_KMAC_HMAC_SHA_256;
		break;
	case SHA384_DIGEST_SIZE:
		ctx->gr0.fc = CPACF_KMAC_HMAC_SHA_384;
		break;
	case SHA512_DIGEST_SIZE:
		ctx->gr0.fc = CPACF_KMAC_HMAC_SHA_512;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int s390_hmac_sha2_update(struct shash_desc *desc,
				 const u8 *data, unsigned int len)
{
	struct s390_kmac_sha2_ctx *ctx = shash_desc_ctx(desc);
	unsigned int bs = crypto_shash_blocksize(desc->tfm);
	unsigned int offset, n;

	/* check current buffer */
	offset = ctx->buflen % bs;
	ctx->buflen += len;
	if (offset + len < bs)
		goto store;

	/* process one stored block */
	if (offset) {
		n = bs - offset;
		memcpy(ctx->buf + offset, data, n);
		ctx->gr0.iimp = 1;
		_cpacf_kmac(&ctx->gr0.reg, ctx->param, ctx->buf, bs);
		data += n;
		len -= n;
		offset = 0;
	}
	/* process as many blocks as possible */
	if (len >= bs) {
		n = (len / bs) * bs;
		ctx->gr0.iimp = 1;
		_cpacf_kmac(&ctx->gr0.reg, ctx->param, data, n);
		data += n;
		len -= n;
	}
store:
	/* store incomplete block in buffer */
	if (len)
		memcpy(ctx->buf + offset, data, len);

	return 0;
}

static int s390_hmac_sha2_final(struct shash_desc *desc, u8 *out)
{
	struct s390_kmac_sha2_ctx *ctx = shash_desc_ctx(desc);
	unsigned int bs = crypto_shash_blocksize(desc->tfm);

	ctx->gr0.iimp = 0;
	kmac_sha2_set_imbl(ctx->param, ctx->buflen, bs);
	_cpacf_kmac(&ctx->gr0.reg, ctx->param, ctx->buf, ctx->buflen % bs);
	memcpy(out, ctx->param, crypto_shash_digestsize(desc->tfm));

	return 0;
}

static int s390_hmac_sha2_digest(struct shash_desc *desc,
				 const u8 *data, unsigned int len, u8 *out)
{
	struct s390_kmac_sha2_ctx *ctx = shash_desc_ctx(desc);
	unsigned int ds = crypto_shash_digestsize(desc->tfm);
	int rc;

	rc = s390_hmac_sha2_init(desc);
	if (rc)
		return rc;

	ctx->gr0.iimp = 0;
	kmac_sha2_set_imbl(ctx->param, len,
			   crypto_shash_blocksize(desc->tfm));
	_cpacf_kmac(&ctx->gr0.reg, ctx->param, data, len);
	memcpy(out, ctx->param, ds);

	return 0;
}

#define S390_HMAC_SHA2_ALG(x) {						\
	.fc = CPACF_KMAC_HMAC_SHA_##x,					\
	.alg = {							\
		.init = s390_hmac_sha2_init,				\
		.update = s390_hmac_sha2_update,			\
		.final = s390_hmac_sha2_final,				\
		.digest = s390_hmac_sha2_digest,			\
		.setkey = s390_hmac_sha2_setkey,			\
		.descsize = sizeof(struct s390_kmac_sha2_ctx),		\
		.halg = {						\
			.digestsize = SHA##x##_DIGEST_SIZE,		\
			.base = {					\
				.cra_name = "hmac(sha" #x ")",		\
				.cra_driver_name = "hmac_s390_sha" #x,	\
				.cra_blocksize = SHA##x##_BLOCK_SIZE,	\
				.cra_priority = 400,			\
				.cra_ctxsize = sizeof(struct s390_hmac_ctx), \
				.cra_module = THIS_MODULE,		\
			},						\
		},							\
	},								\
}

static struct s390_hmac_alg {
	bool registered;
	unsigned int fc;
	struct shash_alg alg;
} s390_hmac_algs[] = {
	S390_HMAC_SHA2_ALG(224),
	S390_HMAC_SHA2_ALG(256),
	S390_HMAC_SHA2_ALG(384),
	S390_HMAC_SHA2_ALG(512),
};

static __always_inline void _s390_hmac_algs_unregister(void)
{
	struct s390_hmac_alg *hmac;
	int i;

	for (i = ARRAY_SIZE(s390_hmac_algs) - 1; i >= 0; i--) {
		hmac = &s390_hmac_algs[i];
		if (!hmac->registered)
			continue;
		crypto_unregister_shash(&hmac->alg);
	}
}

static int __init hmac_s390_init(void)
{
	struct s390_hmac_alg *hmac;
	int i, rc = -ENODEV;

	if (!cpacf_query_func(CPACF_KLMD, CPACF_KLMD_SHA_256))
		return -ENODEV;
	if (!cpacf_query_func(CPACF_KLMD, CPACF_KLMD_SHA_512))
		return -ENODEV;

	for (i = 0; i < ARRAY_SIZE(s390_hmac_algs); i++) {
		hmac = &s390_hmac_algs[i];
		if (!cpacf_query_func(CPACF_KMAC, hmac->fc))
			continue;

		rc = crypto_register_shash(&hmac->alg);
		if (rc) {
			pr_err("unable to register %s\n",
			       hmac->alg.halg.base.cra_name);
			goto out;
		}
		hmac->registered = true;
		pr_debug("registered %s\n", hmac->alg.halg.base.cra_name);
	}
	return rc;
out:
	_s390_hmac_algs_unregister();
	return rc;
}

static void __exit hmac_s390_exit(void)
{
	_s390_hmac_algs_unregister();
}

module_cpu_feature_match(S390_CPU_FEATURE_MSA, hmac_s390_init);
module_exit(hmac_s390_exit);

MODULE_DESCRIPTION("S390 HMAC driver");
MODULE_LICENSE("GPL");
