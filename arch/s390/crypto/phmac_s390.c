// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright IBM Corp. 2025
 *
 * s390 specific HMAC support for protected keys.
 */

#define KMSG_COMPONENT	"phmac_s390"
#define pr_fmt(fmt)	KMSG_COMPONENT ": " fmt

#include <asm/cpacf.h>
#include <asm/pkey.h>
#include <crypto/engine.h>
#include <crypto/hash.h>
#include <crypto/internal/hash.h>
#include <crypto/sha2.h>
#include <linux/atomic.h>
#include <linux/cpufeature.h>
#include <linux/delay.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/spinlock.h>

static struct crypto_engine *phmac_crypto_engine;
#define MAX_QLEN 10

/*
 * A simple hash walk helper
 */

struct hash_walk_helper {
	struct crypto_hash_walk walk;
	const u8 *walkaddr;
	int walkbytes;
};

/*
 * Prepare hash walk helper.
 * Set up the base hash walk, fill walkaddr and walkbytes.
 * Returns 0 on success or negative value on error.
 */
static inline int hwh_prepare(struct ahash_request *req,
			      struct hash_walk_helper *hwh)
{
	hwh->walkbytes = crypto_hash_walk_first(req, &hwh->walk);
	if (hwh->walkbytes < 0)
		return hwh->walkbytes;
	hwh->walkaddr = hwh->walk.data;
	return 0;
}

/*
 * Advance hash walk helper by n bytes.
 * Progress the walkbytes and walkaddr fields by n bytes.
 * If walkbytes is then 0, pull next hunk from hash walk
 * and update walkbytes and walkaddr.
 * If n is negative, unmap hash walk and return error.
 * Returns 0 on success or negative value on error.
 */
static inline int hwh_advance(struct hash_walk_helper *hwh, int n)
{
	if (n < 0)
		return crypto_hash_walk_done(&hwh->walk, n);

	hwh->walkbytes -= n;
	hwh->walkaddr += n;
	if (hwh->walkbytes > 0)
		return 0;

	hwh->walkbytes = crypto_hash_walk_done(&hwh->walk, 0);
	if (hwh->walkbytes < 0)
		return hwh->walkbytes;

	hwh->walkaddr = hwh->walk.data;
	return 0;
}

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
 * cv   | 224/256 | blocksize/2 |   32 |  u64[8]
 *      | 384/512 |             |   64 | u128[8]
 * imbl | 224/256 | blocksize/8 |    8 |     u64
 *      | 384/512 |             |   16 |    u128
 * key  | 224/256 | blocksize   |   96 |  u8[96]
 *      | 384/512 |             |  160 | u8[160]
 */

#define MAX_DIGEST_SIZE		SHA512_DIGEST_SIZE
#define MAX_IMBL_SIZE		sizeof(u128)
#define MAX_BLOCK_SIZE		SHA512_BLOCK_SIZE

#define SHA2_CV_SIZE(bs)	((bs) >> 1)
#define SHA2_IMBL_SIZE(bs)	((bs) >> 3)

#define SHA2_IMBL_OFFSET(bs)	(SHA2_CV_SIZE(bs))
#define SHA2_KEY_OFFSET(bs)	(SHA2_CV_SIZE(bs) + SHA2_IMBL_SIZE(bs))

#define PHMAC_MAX_KEYSIZE       256
#define PHMAC_SHA256_PK_SIZE	(SHA256_BLOCK_SIZE + 32)
#define PHMAC_SHA512_PK_SIZE	(SHA512_BLOCK_SIZE + 32)
#define PHMAC_MAX_PK_SIZE	PHMAC_SHA512_PK_SIZE

/* phmac protected key struct */
struct phmac_protkey {
	u32 type;
	u32 len;
	u8 protkey[PHMAC_MAX_PK_SIZE];
};

#define PK_STATE_NO_KEY		     0
#define PK_STATE_CONVERT_IN_PROGRESS 1
#define PK_STATE_VALID		     2

/* phmac tfm context */
struct phmac_tfm_ctx {
	/* source key material used to derive a protected key from */
	u8 keybuf[PHMAC_MAX_KEYSIZE];
	unsigned int keylen;

	/* cpacf function code to use with this protected key type */
	long fc;

	/* nr of requests enqueued via crypto engine which use this tfm ctx */
	atomic_t via_engine_ctr;

	/* spinlock to atomic read/update all the following fields */
	spinlock_t pk_lock;

	/* see PK_STATE* defines above, < 0 holds convert failure rc  */
	int pk_state;
	/* if state is valid, pk holds the protected key */
	struct phmac_protkey pk;
};

union kmac_gr0 {
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

struct kmac_sha2_ctx {
	u8 param[MAX_DIGEST_SIZE + MAX_IMBL_SIZE + PHMAC_MAX_PK_SIZE];
	union kmac_gr0 gr0;
	u8 buf[MAX_BLOCK_SIZE];
	u64 buflen[2];
};

enum async_op {
	OP_NOP = 0,
	OP_UPDATE,
	OP_FINAL,
	OP_FINUP,
};

/* phmac request context */
struct phmac_req_ctx {
	struct hash_walk_helper hwh;
	struct kmac_sha2_ctx kmac_ctx;
	enum async_op async_op;
};

/*
 * Pkey 'token' struct used to derive a protected key value from a clear key.
 */
struct hmac_clrkey_token {
	u8  type;
	u8  res0[3];
	u8  version;
	u8  res1[3];
	u32 keytype;
	u32 len;
	u8 key[];
} __packed;

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

/*
 * make_clrkey_token() - wrap the clear key into a pkey clearkey token.
 */
static inline int make_clrkey_token(const u8 *clrkey, size_t clrkeylen,
				    unsigned int digestsize, u8 *dest)
{
	struct hmac_clrkey_token *token = (struct hmac_clrkey_token *)dest;
	unsigned int blocksize;
	int rc;

	token->type = 0x00;
	token->version = 0x02;
	switch (digestsize) {
	case SHA224_DIGEST_SIZE:
	case SHA256_DIGEST_SIZE:
		token->keytype = PKEY_KEYTYPE_HMAC_512;
		blocksize = 64;
		break;
	case SHA384_DIGEST_SIZE:
	case SHA512_DIGEST_SIZE:
		token->keytype = PKEY_KEYTYPE_HMAC_1024;
		blocksize = 128;
		break;
	default:
		return -EINVAL;
	}
	token->len = blocksize;

	if (clrkeylen > blocksize) {
		rc = hash_key(clrkey, clrkeylen, token->key, digestsize);
		if (rc)
			return rc;
	} else {
		memcpy(token->key, clrkey, clrkeylen);
	}

	return 0;
}

/*
 * phmac_tfm_ctx_setkey() - Set key value into tfm context, maybe construct
 * a clear key token digestible by pkey from a clear key value.
 */
static inline int phmac_tfm_ctx_setkey(struct phmac_tfm_ctx *tfm_ctx,
				       const u8 *key, unsigned int keylen)
{
	if (keylen > sizeof(tfm_ctx->keybuf))
		return -EINVAL;

	memcpy(tfm_ctx->keybuf, key, keylen);
	tfm_ctx->keylen = keylen;

	return 0;
}

/*
 * Convert the raw key material into a protected key via PKEY api.
 * This function may sleep - don't call in non-sleeping context.
 */
static inline int convert_key(const u8 *key, unsigned int keylen,
			      struct phmac_protkey *pk)
{
	int rc, i;

	pk->len = sizeof(pk->protkey);

	/*
	 * In case of a busy card retry with increasing delay
	 * of 200, 400, 800 and 1600 ms - in total 3 s.
	 */
	for (rc = -EIO, i = 0; rc && i < 5; i++) {
		if (rc == -EBUSY && msleep_interruptible((1 << i) * 100)) {
			rc = -EINTR;
			goto out;
		}
		rc = pkey_key2protkey(key, keylen,
				      pk->protkey, &pk->len, &pk->type,
				      PKEY_XFLAG_NOMEMALLOC);
	}

out:
	pr_debug("rc=%d\n", rc);
	return rc;
}

/*
 * (Re-)Convert the raw key material from the tfm ctx into a protected
 * key via convert_key() function. Update the pk_state, pk_type, pk_len
 * and the protected key in the tfm context.
 * Please note this function may be invoked concurrently with the very
 * same tfm context. The pk_lock spinlock in the context ensures an
 * atomic update of the pk and the pk state but does not guarantee any
 * order of update. So a fresh converted valid protected key may get
 * updated with an 'old' expired key value. As the cpacf instructions
 * detect this, refuse to operate with an invalid key and the calling
 * code triggers a (re-)conversion this does no harm. This may lead to
 * unnecessary additional conversion but never to invalid data on the
 * hash operation.
 */
static int phmac_convert_key(struct phmac_tfm_ctx *tfm_ctx)
{
	struct phmac_protkey pk;
	int rc;

	spin_lock_bh(&tfm_ctx->pk_lock);
	tfm_ctx->pk_state = PK_STATE_CONVERT_IN_PROGRESS;
	spin_unlock_bh(&tfm_ctx->pk_lock);

	rc = convert_key(tfm_ctx->keybuf, tfm_ctx->keylen, &pk);

	/* update context */
	spin_lock_bh(&tfm_ctx->pk_lock);
	if (rc) {
		tfm_ctx->pk_state = rc;
	} else {
		tfm_ctx->pk_state = PK_STATE_VALID;
		tfm_ctx->pk = pk;
	}
	spin_unlock_bh(&tfm_ctx->pk_lock);

	memzero_explicit(&pk, sizeof(pk));
	pr_debug("rc=%d\n", rc);
	return rc;
}

/*
 * kmac_sha2_set_imbl - sets the input message bit-length based on the blocksize
 */
static inline void kmac_sha2_set_imbl(u8 *param, u64 buflen_lo,
				      u64 buflen_hi, unsigned int blocksize)
{
	u8 *imbl = param + SHA2_IMBL_OFFSET(blocksize);

	switch (blocksize) {
	case SHA256_BLOCK_SIZE:
		*(u64 *)imbl = buflen_lo * BITS_PER_BYTE;
		break;
	case SHA512_BLOCK_SIZE:
		*(u128 *)imbl = (((u128)buflen_hi << 64) + buflen_lo) << 3;
		break;
	default:
		break;
	}
}

static int phmac_kmac_update(struct ahash_request *req, bool maysleep)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct phmac_tfm_ctx *tfm_ctx = crypto_ahash_ctx(tfm);
	struct phmac_req_ctx *req_ctx = ahash_request_ctx(req);
	struct kmac_sha2_ctx *ctx = &req_ctx->kmac_ctx;
	struct hash_walk_helper *hwh = &req_ctx->hwh;
	unsigned int bs = crypto_ahash_blocksize(tfm);
	unsigned int offset, k, n;
	int rc = 0;

	/*
	 * The walk is always mapped when this function is called.
	 * Note that in case of partial processing or failure the walk
	 * is NOT unmapped here. So a follow up task may reuse the walk
	 * or in case of unrecoverable failure needs to unmap it.
	 */

	while (hwh->walkbytes > 0) {
		/* check sha2 context buffer */
		offset = ctx->buflen[0] % bs;
		if (offset + hwh->walkbytes < bs)
			goto store;

		if (offset) {
			/* fill ctx buffer up to blocksize and process this block */
			n = bs - offset;
			memcpy(ctx->buf + offset, hwh->walkaddr, n);
			ctx->gr0.iimp = 1;
			for (;;) {
				k = _cpacf_kmac(&ctx->gr0.reg, ctx->param, ctx->buf, bs);
				if (likely(k == bs))
					break;
				if (unlikely(k > 0)) {
					/*
					 * Can't deal with hunks smaller than blocksize.
					 * And kmac should always return the nr of
					 * processed bytes as 0 or a multiple of the
					 * blocksize.
					 */
					rc = -EIO;
					goto out;
				}
				/* protected key is invalid and needs re-conversion */
				if (!maysleep) {
					rc = -EKEYEXPIRED;
					goto out;
				}
				rc = phmac_convert_key(tfm_ctx);
				if (rc)
					goto out;
				spin_lock_bh(&tfm_ctx->pk_lock);
				memcpy(ctx->param + SHA2_KEY_OFFSET(bs),
				       tfm_ctx->pk.protkey, tfm_ctx->pk.len);
				spin_unlock_bh(&tfm_ctx->pk_lock);
			}
			ctx->buflen[0] += n;
			if (ctx->buflen[0] < n)
				ctx->buflen[1]++;
			rc = hwh_advance(hwh, n);
			if (unlikely(rc))
				goto out;
			offset = 0;
		}

		/* process as many blocks as possible from the walk */
		while (hwh->walkbytes >= bs) {
			n = (hwh->walkbytes / bs) * bs;
			ctx->gr0.iimp = 1;
			k = _cpacf_kmac(&ctx->gr0.reg, ctx->param, hwh->walkaddr, n);
			if (likely(k > 0)) {
				ctx->buflen[0] += k;
				if (ctx->buflen[0] < k)
					ctx->buflen[1]++;
				rc = hwh_advance(hwh, k);
				if (unlikely(rc))
					goto out;
			}
			if (unlikely(k < n)) {
				/* protected key is invalid and needs re-conversion */
				if (!maysleep) {
					rc = -EKEYEXPIRED;
					goto out;
				}
				rc = phmac_convert_key(tfm_ctx);
				if (rc)
					goto out;
				spin_lock_bh(&tfm_ctx->pk_lock);
				memcpy(ctx->param + SHA2_KEY_OFFSET(bs),
				       tfm_ctx->pk.protkey, tfm_ctx->pk.len);
				spin_unlock_bh(&tfm_ctx->pk_lock);
			}
		}

store:
		/* store incomplete block in context buffer */
		if (hwh->walkbytes) {
			memcpy(ctx->buf + offset, hwh->walkaddr, hwh->walkbytes);
			ctx->buflen[0] += hwh->walkbytes;
			if (ctx->buflen[0] < hwh->walkbytes)
				ctx->buflen[1]++;
			rc = hwh_advance(hwh, hwh->walkbytes);
			if (unlikely(rc))
				goto out;
		}

	} /* end of while (hwh->walkbytes > 0) */

out:
	pr_debug("rc=%d\n", rc);
	return rc;
}

static int phmac_kmac_final(struct ahash_request *req, bool maysleep)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct phmac_tfm_ctx *tfm_ctx = crypto_ahash_ctx(tfm);
	struct phmac_req_ctx *req_ctx = ahash_request_ctx(req);
	struct kmac_sha2_ctx *ctx = &req_ctx->kmac_ctx;
	unsigned int ds = crypto_ahash_digestsize(tfm);
	unsigned int bs = crypto_ahash_blocksize(tfm);
	unsigned int k, n;
	int rc = 0;

	n = ctx->buflen[0] % bs;
	ctx->gr0.iimp = 0;
	kmac_sha2_set_imbl(ctx->param, ctx->buflen[0], ctx->buflen[1], bs);
	for (;;) {
		k = _cpacf_kmac(&ctx->gr0.reg, ctx->param, ctx->buf, n);
		if (likely(k == n))
			break;
		if (unlikely(k > 0)) {
			/* Can't deal with hunks smaller than blocksize. */
			rc = -EIO;
			goto out;
		}
		/* protected key is invalid and needs re-conversion */
		if (!maysleep) {
			rc = -EKEYEXPIRED;
			goto out;
		}
		rc = phmac_convert_key(tfm_ctx);
		if (rc)
			goto out;
		spin_lock_bh(&tfm_ctx->pk_lock);
		memcpy(ctx->param + SHA2_KEY_OFFSET(bs),
		       tfm_ctx->pk.protkey, tfm_ctx->pk.len);
		spin_unlock_bh(&tfm_ctx->pk_lock);
	}

	memcpy(req->result, ctx->param, ds);

out:
	pr_debug("rc=%d\n", rc);
	return rc;
}

static int phmac_init(struct ahash_request *req)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct phmac_tfm_ctx *tfm_ctx = crypto_ahash_ctx(tfm);
	struct phmac_req_ctx *req_ctx = ahash_request_ctx(req);
	struct kmac_sha2_ctx *kmac_ctx = &req_ctx->kmac_ctx;
	unsigned int bs = crypto_ahash_blocksize(tfm);
	int rc = 0;

	/* zero request context (includes the kmac sha2 context) */
	memset(req_ctx, 0, sizeof(*req_ctx));

	/*
	 * setkey() should have set a valid fc into the tfm context.
	 * Copy this function code into the gr0 field of the kmac context.
	 */
	if (!tfm_ctx->fc) {
		rc = -ENOKEY;
		goto out;
	}
	kmac_ctx->gr0.fc = tfm_ctx->fc;

	/*
	 * Copy the pk from tfm ctx into kmac ctx. The protected key
	 * may be outdated but update() and final() will handle this.
	 */
	spin_lock_bh(&tfm_ctx->pk_lock);
	memcpy(kmac_ctx->param + SHA2_KEY_OFFSET(bs),
	       tfm_ctx->pk.protkey, tfm_ctx->pk.len);
	spin_unlock_bh(&tfm_ctx->pk_lock);

out:
	pr_debug("rc=%d\n", rc);
	return rc;
}

static int phmac_update(struct ahash_request *req)
{
	struct phmac_req_ctx *req_ctx = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct phmac_tfm_ctx *tfm_ctx = crypto_ahash_ctx(tfm);
	struct kmac_sha2_ctx *kmac_ctx = &req_ctx->kmac_ctx;
	struct hash_walk_helper *hwh = &req_ctx->hwh;
	int rc;

	/* prep the walk in the request context */
	rc = hwh_prepare(req, hwh);
	if (rc)
		goto out;

	/* Try synchronous operation if no active engine usage */
	if (!atomic_read(&tfm_ctx->via_engine_ctr)) {
		rc = phmac_kmac_update(req, false);
		if (rc == 0)
			goto out;
	}

	/*
	 * If sync operation failed or key expired or there are already
	 * requests enqueued via engine, fallback to async. Mark tfm as
	 * using engine to serialize requests.
	 */
	if (rc == 0 || rc == -EKEYEXPIRED) {
		req_ctx->async_op = OP_UPDATE;
		atomic_inc(&tfm_ctx->via_engine_ctr);
		rc = crypto_transfer_hash_request_to_engine(phmac_crypto_engine, req);
		if (rc != -EINPROGRESS)
			atomic_dec(&tfm_ctx->via_engine_ctr);
	}

	if (rc != -EINPROGRESS) {
		hwh_advance(hwh, rc);
		memzero_explicit(kmac_ctx, sizeof(*kmac_ctx));
	}

out:
	pr_debug("rc=%d\n", rc);
	return rc;
}

static int phmac_final(struct ahash_request *req)
{
	struct phmac_req_ctx *req_ctx = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct phmac_tfm_ctx *tfm_ctx = crypto_ahash_ctx(tfm);
	struct kmac_sha2_ctx *kmac_ctx = &req_ctx->kmac_ctx;
	int rc = 0;

	/* Try synchronous operation if no active engine usage */
	if (!atomic_read(&tfm_ctx->via_engine_ctr)) {
		rc = phmac_kmac_final(req, false);
		if (rc == 0)
			goto out;
	}

	/*
	 * If sync operation failed or key expired or there are already
	 * requests enqueued via engine, fallback to async. Mark tfm as
	 * using engine to serialize requests.
	 */
	if (rc == 0 || rc == -EKEYEXPIRED) {
		req_ctx->async_op = OP_FINAL;
		atomic_inc(&tfm_ctx->via_engine_ctr);
		rc = crypto_transfer_hash_request_to_engine(phmac_crypto_engine, req);
		if (rc != -EINPROGRESS)
			atomic_dec(&tfm_ctx->via_engine_ctr);
	}

out:
	if (rc != -EINPROGRESS)
		memzero_explicit(kmac_ctx, sizeof(*kmac_ctx));
	pr_debug("rc=%d\n", rc);
	return rc;
}

static int phmac_finup(struct ahash_request *req)
{
	struct phmac_req_ctx *req_ctx = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct phmac_tfm_ctx *tfm_ctx = crypto_ahash_ctx(tfm);
	struct kmac_sha2_ctx *kmac_ctx = &req_ctx->kmac_ctx;
	struct hash_walk_helper *hwh = &req_ctx->hwh;
	int rc;

	/* prep the walk in the request context */
	rc = hwh_prepare(req, hwh);
	if (rc)
		goto out;

	req_ctx->async_op = OP_FINUP;

	/* Try synchronous operations if no active engine usage */
	if (!atomic_read(&tfm_ctx->via_engine_ctr)) {
		rc = phmac_kmac_update(req, false);
		if (rc == 0)
			req_ctx->async_op = OP_FINAL;
	}
	if (!rc && req_ctx->async_op == OP_FINAL &&
	    !atomic_read(&tfm_ctx->via_engine_ctr)) {
		rc = phmac_kmac_final(req, false);
		if (rc == 0)
			goto out;
	}

	/*
	 * If sync operation failed or key expired or there are already
	 * requests enqueued via engine, fallback to async. Mark tfm as
	 * using engine to serialize requests.
	 */
	if (rc == 0 || rc == -EKEYEXPIRED) {
		/* req->async_op has been set to either OP_FINUP or OP_FINAL */
		atomic_inc(&tfm_ctx->via_engine_ctr);
		rc = crypto_transfer_hash_request_to_engine(phmac_crypto_engine, req);
		if (rc != -EINPROGRESS)
			atomic_dec(&tfm_ctx->via_engine_ctr);
	}

	if (rc != -EINPROGRESS)
		hwh_advance(hwh, rc);

out:
	if (rc != -EINPROGRESS)
		memzero_explicit(kmac_ctx, sizeof(*kmac_ctx));
	pr_debug("rc=%d\n", rc);
	return rc;
}

static int phmac_digest(struct ahash_request *req)
{
	int rc;

	rc = phmac_init(req);
	if (rc)
		goto out;

	rc = phmac_finup(req);

out:
	pr_debug("rc=%d\n", rc);
	return rc;
}

static int phmac_setkey(struct crypto_ahash *tfm,
			const u8 *key, unsigned int keylen)
{
	struct phmac_tfm_ctx *tfm_ctx = crypto_ahash_ctx(tfm);
	unsigned int ds = crypto_ahash_digestsize(tfm);
	unsigned int bs = crypto_ahash_blocksize(tfm);
	unsigned int tmpkeylen;
	u8 *tmpkey = NULL;
	int rc = 0;

	if (!crypto_ahash_tested(tfm)) {
		/*
		 * selftest running: key is a raw hmac clear key and needs
		 * to get embedded into a 'clear key token' in order to have
		 * it correctly processed by the pkey module.
		 */
		tmpkeylen = sizeof(struct hmac_clrkey_token) + bs;
		tmpkey = kzalloc(tmpkeylen, GFP_KERNEL);
		if (!tmpkey) {
			rc = -ENOMEM;
			goto out;
		}
		rc = make_clrkey_token(key, keylen, ds, tmpkey);
		if (rc)
			goto out;
		keylen = tmpkeylen;
		key = tmpkey;
	}

	/* copy raw key into tfm context */
	rc = phmac_tfm_ctx_setkey(tfm_ctx, key, keylen);
	if (rc)
		goto out;

	/* convert raw key into protected key */
	rc = phmac_convert_key(tfm_ctx);
	if (rc)
		goto out;

	/* set function code in tfm context, check for valid pk type */
	switch (ds) {
	case SHA224_DIGEST_SIZE:
		if (tfm_ctx->pk.type != PKEY_KEYTYPE_HMAC_512)
			rc = -EINVAL;
		else
			tfm_ctx->fc = CPACF_KMAC_PHMAC_SHA_224;
		break;
	case SHA256_DIGEST_SIZE:
		if (tfm_ctx->pk.type != PKEY_KEYTYPE_HMAC_512)
			rc = -EINVAL;
		else
			tfm_ctx->fc = CPACF_KMAC_PHMAC_SHA_256;
		break;
	case SHA384_DIGEST_SIZE:
		if (tfm_ctx->pk.type != PKEY_KEYTYPE_HMAC_1024)
			rc = -EINVAL;
		else
			tfm_ctx->fc = CPACF_KMAC_PHMAC_SHA_384;
		break;
	case SHA512_DIGEST_SIZE:
		if (tfm_ctx->pk.type != PKEY_KEYTYPE_HMAC_1024)
			rc = -EINVAL;
		else
			tfm_ctx->fc = CPACF_KMAC_PHMAC_SHA_512;
		break;
	default:
		tfm_ctx->fc = 0;
		rc = -EINVAL;
	}

out:
	kfree(tmpkey);
	pr_debug("rc=%d\n", rc);
	return rc;
}

static int phmac_export(struct ahash_request *req, void *out)
{
	struct phmac_req_ctx *req_ctx = ahash_request_ctx(req);
	struct kmac_sha2_ctx *ctx = &req_ctx->kmac_ctx;

	memcpy(out, ctx, sizeof(*ctx));

	return 0;
}

static int phmac_import(struct ahash_request *req, const void *in)
{
	struct phmac_req_ctx *req_ctx = ahash_request_ctx(req);
	struct kmac_sha2_ctx *ctx = &req_ctx->kmac_ctx;

	memset(req_ctx, 0, sizeof(*req_ctx));
	memcpy(ctx, in, sizeof(*ctx));

	return 0;
}

static int phmac_init_tfm(struct crypto_ahash *tfm)
{
	struct phmac_tfm_ctx *tfm_ctx = crypto_ahash_ctx(tfm);

	memset(tfm_ctx, 0, sizeof(*tfm_ctx));
	spin_lock_init(&tfm_ctx->pk_lock);

	crypto_ahash_set_reqsize(tfm, sizeof(struct phmac_req_ctx));

	return 0;
}

static void phmac_exit_tfm(struct crypto_ahash *tfm)
{
	struct phmac_tfm_ctx *tfm_ctx = crypto_ahash_ctx(tfm);

	memzero_explicit(tfm_ctx->keybuf, sizeof(tfm_ctx->keybuf));
	memzero_explicit(&tfm_ctx->pk, sizeof(tfm_ctx->pk));
}

static int phmac_do_one_request(struct crypto_engine *engine, void *areq)
{
	struct ahash_request *req = ahash_request_cast(areq);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct phmac_tfm_ctx *tfm_ctx = crypto_ahash_ctx(tfm);
	struct phmac_req_ctx *req_ctx = ahash_request_ctx(req);
	struct kmac_sha2_ctx *kmac_ctx = &req_ctx->kmac_ctx;
	struct hash_walk_helper *hwh = &req_ctx->hwh;
	int rc = -EINVAL;

	/*
	 * Three kinds of requests come in here:
	 * 1. req->async_op == OP_UPDATE with req->nbytes > 0
	 * 2. req->async_op == OP_FINUP with req->nbytes > 0
	 * 3. req->async_op == OP_FINAL
	 * For update and finup the hwh walk has already been prepared
	 * by the caller. For final there is no hwh walk needed.
	 */

	switch (req_ctx->async_op) {
	case OP_UPDATE:
	case OP_FINUP:
		rc = phmac_kmac_update(req, true);
		if (rc == -EKEYEXPIRED) {
			/*
			 * Protected key expired, conversion is in process.
			 * Trigger a re-schedule of this request by returning
			 * -ENOSPC ("hardware queue full") to the crypto engine.
			 * To avoid immediately re-invocation of this callback,
			 * tell scheduler to voluntarily give up the CPU here.
			 */
			pr_debug("rescheduling request\n");
			cond_resched();
			return -ENOSPC;
		} else if (rc) {
			hwh_advance(hwh, rc);
			goto out;
		}
		if (req_ctx->async_op == OP_UPDATE)
			break;
		req_ctx->async_op = OP_FINAL;
		fallthrough;
	case OP_FINAL:
		rc = phmac_kmac_final(req, true);
		if (rc == -EKEYEXPIRED) {
			/*
			 * Protected key expired, conversion is in process.
			 * Trigger a re-schedule of this request by returning
			 * -ENOSPC ("hardware queue full") to the crypto engine.
			 * To avoid immediately re-invocation of this callback,
			 * tell scheduler to voluntarily give up the CPU here.
			 */
			pr_debug("rescheduling request\n");
			cond_resched();
			return -ENOSPC;
		}
		break;
	default:
		/* unknown/unsupported/unimplemented asynch op */
		return -EOPNOTSUPP;
	}

out:
	if (rc || req_ctx->async_op == OP_FINAL)
		memzero_explicit(kmac_ctx, sizeof(*kmac_ctx));
	pr_debug("request complete with rc=%d\n", rc);
	local_bh_disable();
	atomic_dec(&tfm_ctx->via_engine_ctr);
	crypto_finalize_hash_request(engine, req, rc);
	local_bh_enable();
	return rc;
}

#define S390_ASYNC_PHMAC_ALG(x)						\
{									\
	.base = {							\
		.init	  = phmac_init,					\
		.update	  = phmac_update,				\
		.final	  = phmac_final,				\
		.finup	  = phmac_finup,				\
		.digest	  = phmac_digest,				\
		.setkey	  = phmac_setkey,				\
		.import	  = phmac_import,				\
		.export	  = phmac_export,				\
		.init_tfm = phmac_init_tfm,				\
		.exit_tfm = phmac_exit_tfm,				\
		.halg = {						\
			.digestsize = SHA##x##_DIGEST_SIZE,		\
			.statesize  = sizeof(struct kmac_sha2_ctx),	\
			.base = {					\
				.cra_name = "phmac(sha" #x ")",		\
				.cra_driver_name = "phmac_s390_sha" #x,	\
				.cra_blocksize = SHA##x##_BLOCK_SIZE,	\
				.cra_priority = 400,			\
				.cra_flags = CRYPTO_ALG_ASYNC |		\
					     CRYPTO_ALG_NO_FALLBACK,	\
				.cra_ctxsize = sizeof(struct phmac_tfm_ctx), \
				.cra_module = THIS_MODULE,		\
			},						\
		},							\
	},								\
	.op = {								\
		.do_one_request = phmac_do_one_request,			\
	},								\
}

static struct phmac_alg {
	unsigned int fc;
	struct ahash_engine_alg alg;
	bool registered;
} phmac_algs[] = {
	{
		.fc = CPACF_KMAC_PHMAC_SHA_224,
		.alg = S390_ASYNC_PHMAC_ALG(224),
	}, {
		.fc = CPACF_KMAC_PHMAC_SHA_256,
		.alg = S390_ASYNC_PHMAC_ALG(256),
	}, {
		.fc = CPACF_KMAC_PHMAC_SHA_384,
		.alg = S390_ASYNC_PHMAC_ALG(384),
	}, {
		.fc = CPACF_KMAC_PHMAC_SHA_512,
		.alg = S390_ASYNC_PHMAC_ALG(512),
	}
};

static struct miscdevice phmac_dev = {
	.name	= "phmac",
	.minor	= MISC_DYNAMIC_MINOR,
};

static void s390_phmac_exit(void)
{
	struct phmac_alg *phmac;
	int i;

	if (phmac_crypto_engine) {
		crypto_engine_stop(phmac_crypto_engine);
		crypto_engine_exit(phmac_crypto_engine);
	}

	for (i = ARRAY_SIZE(phmac_algs) - 1; i >= 0; i--) {
		phmac = &phmac_algs[i];
		if (phmac->registered)
			crypto_engine_unregister_ahash(&phmac->alg);
	}

	misc_deregister(&phmac_dev);
}

static int __init s390_phmac_init(void)
{
	struct phmac_alg *phmac;
	int i, rc;

	/* for selftest cpacf klmd subfunction is needed */
	if (!cpacf_query_func(CPACF_KLMD, CPACF_KLMD_SHA_256))
		return -ENODEV;
	if (!cpacf_query_func(CPACF_KLMD, CPACF_KLMD_SHA_512))
		return -ENODEV;

	/* register a simple phmac pseudo misc device */
	rc = misc_register(&phmac_dev);
	if (rc)
		return rc;

	/* with this pseudo device alloc and start a crypto engine */
	phmac_crypto_engine =
		crypto_engine_alloc_init_and_set(phmac_dev.this_device,
						 true, false, MAX_QLEN);
	if (!phmac_crypto_engine) {
		rc = -ENOMEM;
		goto out_err;
	}
	rc = crypto_engine_start(phmac_crypto_engine);
	if (rc) {
		crypto_engine_exit(phmac_crypto_engine);
		phmac_crypto_engine = NULL;
		goto out_err;
	}

	for (i = 0; i < ARRAY_SIZE(phmac_algs); i++) {
		phmac = &phmac_algs[i];
		if (!cpacf_query_func(CPACF_KMAC, phmac->fc))
			continue;
		rc = crypto_engine_register_ahash(&phmac->alg);
		if (rc)
			goto out_err;
		phmac->registered = true;
		pr_debug("%s registered\n", phmac->alg.base.halg.base.cra_name);
	}

	return 0;

out_err:
	s390_phmac_exit();
	return rc;
}

module_init(s390_phmac_init);
module_exit(s390_phmac_exit);

MODULE_ALIAS_CRYPTO("phmac(sha224)");
MODULE_ALIAS_CRYPTO("phmac(sha256)");
MODULE_ALIAS_CRYPTO("phmac(sha384)");
MODULE_ALIAS_CRYPTO("phmac(sha512)");

MODULE_DESCRIPTION("S390 HMAC driver for protected keys");
MODULE_LICENSE("GPL");
