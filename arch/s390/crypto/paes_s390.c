// SPDX-License-Identifier: GPL-2.0
/*
 * Cryptographic API.
 *
 * s390 implementation of the AES Cipher Algorithm with protected keys.
 *
 * s390 Version:
 *   Copyright IBM Corp. 2017, 2025
 *   Author(s): Martin Schwidefsky <schwidefsky@de.ibm.com>
 *		Harald Freudenberger <freude@de.ibm.com>
 */

#define KMSG_COMPONENT "paes_s390"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/atomic.h>
#include <linux/cpufeature.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <crypto/aes.h>
#include <crypto/algapi.h>
#include <crypto/engine.h>
#include <crypto/internal/skcipher.h>
#include <crypto/xts.h>
#include <asm/cpacf.h>
#include <asm/pkey.h>

/*
 * Key blobs smaller/bigger than these defines are rejected
 * by the common code even before the individual setkey function
 * is called. As paes can handle different kinds of key blobs
 * and padding is also possible, the limits need to be generous.
 */
#define PAES_MIN_KEYSIZE	16
#define PAES_MAX_KEYSIZE	MAXEP11AESKEYBLOBSIZE
#define PAES_256_PROTKEY_SIZE	(32 + 32)	/* key + verification pattern */
#define PXTS_256_PROTKEY_SIZE	(32 + 32 + 32)	/* k1 + k2 + verification pattern */

static u8 *ctrblk;
static DEFINE_MUTEX(ctrblk_lock);

static cpacf_mask_t km_functions, kmc_functions, kmctr_functions;

static struct crypto_engine *paes_crypto_engine;
#define MAX_QLEN 10

/*
 * protected key specific stuff
 */

struct paes_protkey {
	u32 type;
	u32 len;
	u8 protkey[PXTS_256_PROTKEY_SIZE];
};

#define PK_STATE_NO_KEY		     0
#define PK_STATE_CONVERT_IN_PROGRESS 1
#define PK_STATE_VALID		     2

struct s390_paes_ctx {
	/* source key material used to derive a protected key from */
	u8 keybuf[PAES_MAX_KEYSIZE];
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
	struct paes_protkey pk;
};

struct s390_pxts_ctx {
	/* source key material used to derive a protected key from */
	u8 keybuf[2 * PAES_MAX_KEYSIZE];
	unsigned int keylen;

	/* cpacf function code to use with this protected key type */
	long fc;

	/* nr of requests enqueued via crypto engine which use this tfm ctx */
	atomic_t via_engine_ctr;

	/* spinlock to atomic read/update all the following fields */
	spinlock_t pk_lock;

	/* see PK_STATE* defines above, < 0 holds convert failure rc  */
	int pk_state;
	/* if state is valid, pk[] hold(s) the protected key(s) */
	struct paes_protkey pk[2];
};

/*
 * make_clrkey_token() - wrap the raw key ck with pkey clearkey token
 * information.
 * @returns the size of the clearkey token
 */
static inline u32 make_clrkey_token(const u8 *ck, size_t cklen, u8 *dest)
{
	struct clrkey_token {
		u8  type;
		u8  res0[3];
		u8  version;
		u8  res1[3];
		u32 keytype;
		u32 len;
		u8 key[];
	} __packed *token = (struct clrkey_token *)dest;

	token->type = 0x00;
	token->version = 0x02;
	token->keytype = (cklen - 8) >> 3;
	token->len = cklen;
	memcpy(token->key, ck, cklen);

	return sizeof(*token) + cklen;
}

/*
 * paes_ctx_setkey() - Set key value into context, maybe construct
 * a clear key token digestible by pkey from a clear key value.
 */
static inline int paes_ctx_setkey(struct s390_paes_ctx *ctx,
				  const u8 *key, unsigned int keylen)
{
	if (keylen > sizeof(ctx->keybuf))
		return -EINVAL;

	switch (keylen) {
	case 16:
	case 24:
	case 32:
		/* clear key value, prepare pkey clear key token in keybuf */
		memset(ctx->keybuf, 0, sizeof(ctx->keybuf));
		ctx->keylen = make_clrkey_token(key, keylen, ctx->keybuf);
		break;
	default:
		/* other key material, let pkey handle this */
		memcpy(ctx->keybuf, key, keylen);
		ctx->keylen = keylen;
		break;
	}

	return 0;
}

/*
 * pxts_ctx_setkey() - Set key value into context, maybe construct
 * a clear key token digestible by pkey from a clear key value.
 */
static inline int pxts_ctx_setkey(struct s390_pxts_ctx *ctx,
				  const u8 *key, unsigned int keylen)
{
	size_t cklen = keylen / 2;

	if (keylen > sizeof(ctx->keybuf))
		return -EINVAL;

	switch (keylen) {
	case 32:
	case 64:
		/* clear key value, prepare pkey clear key tokens in keybuf */
		memset(ctx->keybuf, 0, sizeof(ctx->keybuf));
		ctx->keylen = make_clrkey_token(key, cklen, ctx->keybuf);
		ctx->keylen += make_clrkey_token(key + cklen, cklen,
						 ctx->keybuf + ctx->keylen);
		break;
	default:
		/* other key material, let pkey handle this */
		memcpy(ctx->keybuf, key, keylen);
		ctx->keylen = keylen;
		break;
	}

	return 0;
}

/*
 * Convert the raw key material into a protected key via PKEY api.
 * This function may sleep - don't call in non-sleeping context.
 */
static inline int convert_key(const u8 *key, unsigned int keylen,
			      struct paes_protkey *pk)
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
 * (Re-)Convert the raw key material from the ctx into a protected key
 * via convert_key() function. Update the pk_state, pk_type, pk_len
 * and the protected key in the tfm context.
 * Please note this function may be invoked concurrently with the very
 * same tfm context. The pk_lock spinlock in the context ensures an
 * atomic update of the pk and the pk state but does not guarantee any
 * order of update. So a fresh converted valid protected key may get
 * updated with an 'old' expired key value. As the cpacf instructions
 * detect this, refuse to operate with an invalid key and the calling
 * code triggers a (re-)conversion this does no harm. This may lead to
 * unnecessary additional conversion but never to invalid data on en-
 * or decrypt operations.
 */
static int paes_convert_key(struct s390_paes_ctx *ctx)
{
	struct paes_protkey pk;
	int rc;

	spin_lock_bh(&ctx->pk_lock);
	ctx->pk_state = PK_STATE_CONVERT_IN_PROGRESS;
	spin_unlock_bh(&ctx->pk_lock);

	rc = convert_key(ctx->keybuf, ctx->keylen, &pk);

	/* update context */
	spin_lock_bh(&ctx->pk_lock);
	if (rc) {
		ctx->pk_state = rc;
	} else {
		ctx->pk_state = PK_STATE_VALID;
		ctx->pk = pk;
	}
	spin_unlock_bh(&ctx->pk_lock);

	memzero_explicit(&pk, sizeof(pk));
	pr_debug("rc=%d\n", rc);
	return rc;
}

/*
 * (Re-)Convert the raw xts key material from the ctx into a
 * protected key via convert_key() function. Update the pk_state,
 * pk_type, pk_len and the protected key in the tfm context.
 * See also comments on function paes_convert_key.
 */
static int pxts_convert_key(struct s390_pxts_ctx *ctx)
{
	struct paes_protkey pk0, pk1;
	size_t split_keylen;
	int rc;

	spin_lock_bh(&ctx->pk_lock);
	ctx->pk_state = PK_STATE_CONVERT_IN_PROGRESS;
	spin_unlock_bh(&ctx->pk_lock);

	rc = convert_key(ctx->keybuf, ctx->keylen, &pk0);
	if (rc)
		goto out;

	switch (pk0.type) {
	case PKEY_KEYTYPE_AES_128:
	case PKEY_KEYTYPE_AES_256:
		/* second keytoken required */
		if (ctx->keylen % 2) {
			rc = -EINVAL;
			goto out;
		}
		split_keylen = ctx->keylen / 2;
		rc = convert_key(ctx->keybuf + split_keylen,
				 split_keylen, &pk1);
		if (rc)
			goto out;
		if (pk0.type != pk1.type) {
			rc = -EINVAL;
			goto out;
		}
		break;
	case PKEY_KEYTYPE_AES_XTS_128:
	case PKEY_KEYTYPE_AES_XTS_256:
		/* single key */
		pk1.type = 0;
		break;
	default:
		/* unsupported protected keytype */
		rc = -EINVAL;
		goto out;
	}

out:
	/* update context */
	spin_lock_bh(&ctx->pk_lock);
	if (rc) {
		ctx->pk_state = rc;
	} else {
		ctx->pk_state = PK_STATE_VALID;
		ctx->pk[0] = pk0;
		ctx->pk[1] = pk1;
	}
	spin_unlock_bh(&ctx->pk_lock);

	memzero_explicit(&pk0, sizeof(pk0));
	memzero_explicit(&pk1, sizeof(pk1));
	pr_debug("rc=%d\n", rc);
	return rc;
}

/*
 * PAES ECB implementation
 */

struct ecb_param {
	u8 key[PAES_256_PROTKEY_SIZE];
} __packed;

struct s390_pecb_req_ctx {
	unsigned long modifier;
	struct skcipher_walk walk;
	bool param_init_done;
	struct ecb_param param;
};

static int ecb_paes_setkey(struct crypto_skcipher *tfm, const u8 *in_key,
			   unsigned int key_len)
{
	struct s390_paes_ctx *ctx = crypto_skcipher_ctx(tfm);
	long fc;
	int rc;

	/* set raw key into context */
	rc = paes_ctx_setkey(ctx, in_key, key_len);
	if (rc)
		goto out;

	/* convert key into protected key */
	rc = paes_convert_key(ctx);
	if (rc)
		goto out;

	/* Pick the correct function code based on the protected key type */
	switch (ctx->pk.type) {
	case PKEY_KEYTYPE_AES_128:
		fc = CPACF_KM_PAES_128;
		break;
	case PKEY_KEYTYPE_AES_192:
		fc = CPACF_KM_PAES_192;
		break;
	case PKEY_KEYTYPE_AES_256:
		fc = CPACF_KM_PAES_256;
		break;
	default:
		fc = 0;
		break;
	}
	ctx->fc = (fc && cpacf_test_func(&km_functions, fc)) ? fc : 0;

	rc = fc ? 0 : -EINVAL;

out:
	pr_debug("rc=%d\n", rc);
	return rc;
}

static int ecb_paes_do_crypt(struct s390_paes_ctx *ctx,
			     struct s390_pecb_req_ctx *req_ctx,
			     bool maysleep)
{
	struct ecb_param *param = &req_ctx->param;
	struct skcipher_walk *walk = &req_ctx->walk;
	unsigned int nbytes, n, k;
	int pk_state, rc = 0;

	if (!req_ctx->param_init_done) {
		/* fetch and check protected key state */
		spin_lock_bh(&ctx->pk_lock);
		pk_state = ctx->pk_state;
		switch (pk_state) {
		case PK_STATE_NO_KEY:
			rc = -ENOKEY;
			break;
		case PK_STATE_CONVERT_IN_PROGRESS:
			rc = -EKEYEXPIRED;
			break;
		case PK_STATE_VALID:
			memcpy(param->key, ctx->pk.protkey, sizeof(param->key));
			req_ctx->param_init_done = true;
			break;
		default:
			rc = pk_state < 0 ? pk_state : -EIO;
			break;
		}
		spin_unlock_bh(&ctx->pk_lock);
	}
	if (rc)
		goto out;

	/*
	 * Note that in case of partial processing or failure the walk
	 * is NOT unmapped here. So a follow up task may reuse the walk
	 * or in case of unrecoverable failure needs to unmap it.
	 */
	while ((nbytes = walk->nbytes) != 0) {
		/* only use complete blocks */
		n = nbytes & ~(AES_BLOCK_SIZE - 1);
		k = cpacf_km(ctx->fc | req_ctx->modifier, param,
			     walk->dst.virt.addr, walk->src.virt.addr, n);
		if (k)
			rc = skcipher_walk_done(walk, nbytes - k);
		if (k < n) {
			if (!maysleep) {
				rc = -EKEYEXPIRED;
				goto out;
			}
			rc = paes_convert_key(ctx);
			if (rc)
				goto out;
			spin_lock_bh(&ctx->pk_lock);
			memcpy(param->key, ctx->pk.protkey, sizeof(param->key));
			spin_unlock_bh(&ctx->pk_lock);
		}
	}

out:
	pr_debug("rc=%d\n", rc);
	return rc;
}

static int ecb_paes_crypt(struct skcipher_request *req, unsigned long modifier)
{
	struct s390_pecb_req_ctx *req_ctx = skcipher_request_ctx(req);
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct s390_paes_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct skcipher_walk *walk = &req_ctx->walk;
	int rc;

	/*
	 * Attempt synchronous encryption first. If it fails, schedule the request
	 * asynchronously via the crypto engine. To preserve execution order,
	 * once a request is queued to the engine, further requests using the same
	 * tfm will also be routed through the engine.
	 */

	rc = skcipher_walk_virt(walk, req, false);
	if (rc)
		goto out;

	req_ctx->modifier = modifier;
	req_ctx->param_init_done = false;

	/* Try synchronous operation if no active engine usage */
	if (!atomic_read(&ctx->via_engine_ctr)) {
		rc = ecb_paes_do_crypt(ctx, req_ctx, false);
		if (rc == 0)
			goto out;
	}

	/*
	 * If sync operation failed or key expired or there are already
	 * requests enqueued via engine, fallback to async. Mark tfm as
	 * using engine to serialize requests.
	 */
	if (rc == 0 || rc == -EKEYEXPIRED) {
		atomic_inc(&ctx->via_engine_ctr);
		rc = crypto_transfer_skcipher_request_to_engine(paes_crypto_engine, req);
		if (rc != -EINPROGRESS)
			atomic_dec(&ctx->via_engine_ctr);
	}

	if (rc != -EINPROGRESS)
		skcipher_walk_done(walk, rc);

out:
	if (rc != -EINPROGRESS)
		memzero_explicit(&req_ctx->param, sizeof(req_ctx->param));
	pr_debug("rc=%d\n", rc);
	return rc;
}

static int ecb_paes_encrypt(struct skcipher_request *req)
{
	return ecb_paes_crypt(req, 0);
}

static int ecb_paes_decrypt(struct skcipher_request *req)
{
	return ecb_paes_crypt(req, CPACF_DECRYPT);
}

static int ecb_paes_init(struct crypto_skcipher *tfm)
{
	struct s390_paes_ctx *ctx = crypto_skcipher_ctx(tfm);

	memset(ctx, 0, sizeof(*ctx));
	spin_lock_init(&ctx->pk_lock);

	crypto_skcipher_set_reqsize(tfm, sizeof(struct s390_pecb_req_ctx));

	return 0;
}

static void ecb_paes_exit(struct crypto_skcipher *tfm)
{
	struct s390_paes_ctx *ctx = crypto_skcipher_ctx(tfm);

	memzero_explicit(ctx, sizeof(*ctx));
}

static int ecb_paes_do_one_request(struct crypto_engine *engine, void *areq)
{
	struct skcipher_request *req = skcipher_request_cast(areq);
	struct s390_pecb_req_ctx *req_ctx = skcipher_request_ctx(req);
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct s390_paes_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct skcipher_walk *walk = &req_ctx->walk;
	int rc;

	/* walk has already been prepared */

	rc = ecb_paes_do_crypt(ctx, req_ctx, true);
	if (rc == -EKEYEXPIRED) {
		/*
		 * Protected key expired, conversion is in process.
		 * Trigger a re-schedule of this request by returning
		 * -ENOSPC ("hardware queue is full") to the crypto engine.
		 * To avoid immediately re-invocation of this callback,
		 * tell the scheduler to voluntarily give up the CPU here.
		 */
		cond_resched();
		pr_debug("rescheduling request\n");
		return -ENOSPC;
	} else if (rc) {
		skcipher_walk_done(walk, rc);
	}

	memzero_explicit(&req_ctx->param, sizeof(req_ctx->param));
	pr_debug("request complete with rc=%d\n", rc);
	local_bh_disable();
	atomic_dec(&ctx->via_engine_ctr);
	crypto_finalize_skcipher_request(engine, req, rc);
	local_bh_enable();
	return rc;
}

static struct skcipher_engine_alg ecb_paes_alg = {
	.base = {
		.base.cra_name	      = "ecb(paes)",
		.base.cra_driver_name = "ecb-paes-s390",
		.base.cra_priority    = 401,	/* combo: aes + ecb + 1 */
		.base.cra_blocksize   = AES_BLOCK_SIZE,
		.base.cra_ctxsize     = sizeof(struct s390_paes_ctx),
		.base.cra_module      = THIS_MODULE,
		.base.cra_list	      = LIST_HEAD_INIT(ecb_paes_alg.base.base.cra_list),
		.init		      = ecb_paes_init,
		.exit		      = ecb_paes_exit,
		.min_keysize	      = PAES_MIN_KEYSIZE,
		.max_keysize	      = PAES_MAX_KEYSIZE,
		.setkey		      = ecb_paes_setkey,
		.encrypt	      = ecb_paes_encrypt,
		.decrypt	      = ecb_paes_decrypt,
	},
	.op = {
		.do_one_request	      = ecb_paes_do_one_request,
	},
};

/*
 * PAES CBC implementation
 */

struct cbc_param {
	u8 iv[AES_BLOCK_SIZE];
	u8 key[PAES_256_PROTKEY_SIZE];
} __packed;

struct s390_pcbc_req_ctx {
	unsigned long modifier;
	struct skcipher_walk walk;
	bool param_init_done;
	struct cbc_param param;
};

static int cbc_paes_setkey(struct crypto_skcipher *tfm, const u8 *in_key,
			   unsigned int key_len)
{
	struct s390_paes_ctx *ctx = crypto_skcipher_ctx(tfm);
	long fc;
	int rc;

	/* set raw key into context */
	rc = paes_ctx_setkey(ctx, in_key, key_len);
	if (rc)
		goto out;

	/* convert raw key into protected key */
	rc = paes_convert_key(ctx);
	if (rc)
		goto out;

	/* Pick the correct function code based on the protected key type */
	switch (ctx->pk.type) {
	case PKEY_KEYTYPE_AES_128:
		fc = CPACF_KMC_PAES_128;
		break;
	case PKEY_KEYTYPE_AES_192:
		fc = CPACF_KMC_PAES_192;
		break;
	case PKEY_KEYTYPE_AES_256:
		fc = CPACF_KMC_PAES_256;
		break;
	default:
		fc = 0;
		break;
	}
	ctx->fc = (fc && cpacf_test_func(&kmc_functions, fc)) ? fc : 0;

	rc = fc ? 0 : -EINVAL;

out:
	pr_debug("rc=%d\n", rc);
	return rc;
}

static int cbc_paes_do_crypt(struct s390_paes_ctx *ctx,
			     struct s390_pcbc_req_ctx *req_ctx,
			     bool maysleep)
{
	struct cbc_param *param = &req_ctx->param;
	struct skcipher_walk *walk = &req_ctx->walk;
	unsigned int nbytes, n, k;
	int pk_state, rc = 0;

	if (!req_ctx->param_init_done) {
		/* fetch and check protected key state */
		spin_lock_bh(&ctx->pk_lock);
		pk_state = ctx->pk_state;
		switch (pk_state) {
		case PK_STATE_NO_KEY:
			rc = -ENOKEY;
			break;
		case PK_STATE_CONVERT_IN_PROGRESS:
			rc = -EKEYEXPIRED;
			break;
		case PK_STATE_VALID:
			memcpy(param->key, ctx->pk.protkey, sizeof(param->key));
			req_ctx->param_init_done = true;
			break;
		default:
			rc = pk_state < 0 ? pk_state : -EIO;
			break;
		}
		spin_unlock_bh(&ctx->pk_lock);
	}
	if (rc)
		goto out;

	memcpy(param->iv, walk->iv, AES_BLOCK_SIZE);

	/*
	 * Note that in case of partial processing or failure the walk
	 * is NOT unmapped here. So a follow up task may reuse the walk
	 * or in case of unrecoverable failure needs to unmap it.
	 */
	while ((nbytes = walk->nbytes) != 0) {
		/* only use complete blocks */
		n = nbytes & ~(AES_BLOCK_SIZE - 1);
		k = cpacf_kmc(ctx->fc | req_ctx->modifier, param,
			      walk->dst.virt.addr, walk->src.virt.addr, n);
		if (k) {
			memcpy(walk->iv, param->iv, AES_BLOCK_SIZE);
			rc = skcipher_walk_done(walk, nbytes - k);
		}
		if (k < n) {
			if (!maysleep) {
				rc = -EKEYEXPIRED;
				goto out;
			}
			rc = paes_convert_key(ctx);
			if (rc)
				goto out;
			spin_lock_bh(&ctx->pk_lock);
			memcpy(param->key, ctx->pk.protkey, sizeof(param->key));
			spin_unlock_bh(&ctx->pk_lock);
		}
	}

out:
	pr_debug("rc=%d\n", rc);
	return rc;
}

static int cbc_paes_crypt(struct skcipher_request *req, unsigned long modifier)
{
	struct s390_pcbc_req_ctx *req_ctx = skcipher_request_ctx(req);
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct s390_paes_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct skcipher_walk *walk = &req_ctx->walk;
	int rc;

	/*
	 * Attempt synchronous encryption first. If it fails, schedule the request
	 * asynchronously via the crypto engine. To preserve execution order,
	 * once a request is queued to the engine, further requests using the same
	 * tfm will also be routed through the engine.
	 */

	rc = skcipher_walk_virt(walk, req, false);
	if (rc)
		goto out;

	req_ctx->modifier = modifier;
	req_ctx->param_init_done = false;

	/* Try synchronous operation if no active engine usage */
	if (!atomic_read(&ctx->via_engine_ctr)) {
		rc = cbc_paes_do_crypt(ctx, req_ctx, false);
		if (rc == 0)
			goto out;
	}

	/*
	 * If sync operation failed or key expired or there are already
	 * requests enqueued via engine, fallback to async. Mark tfm as
	 * using engine to serialize requests.
	 */
	if (rc == 0 || rc == -EKEYEXPIRED) {
		atomic_inc(&ctx->via_engine_ctr);
		rc = crypto_transfer_skcipher_request_to_engine(paes_crypto_engine, req);
		if (rc != -EINPROGRESS)
			atomic_dec(&ctx->via_engine_ctr);
	}

	if (rc != -EINPROGRESS)
		skcipher_walk_done(walk, rc);

out:
	if (rc != -EINPROGRESS)
		memzero_explicit(&req_ctx->param, sizeof(req_ctx->param));
	pr_debug("rc=%d\n", rc);
	return rc;
}

static int cbc_paes_encrypt(struct skcipher_request *req)
{
	return cbc_paes_crypt(req, 0);
}

static int cbc_paes_decrypt(struct skcipher_request *req)
{
	return cbc_paes_crypt(req, CPACF_DECRYPT);
}

static int cbc_paes_init(struct crypto_skcipher *tfm)
{
	struct s390_paes_ctx *ctx = crypto_skcipher_ctx(tfm);

	memset(ctx, 0, sizeof(*ctx));
	spin_lock_init(&ctx->pk_lock);

	crypto_skcipher_set_reqsize(tfm, sizeof(struct s390_pcbc_req_ctx));

	return 0;
}

static void cbc_paes_exit(struct crypto_skcipher *tfm)
{
	struct s390_paes_ctx *ctx = crypto_skcipher_ctx(tfm);

	memzero_explicit(ctx, sizeof(*ctx));
}

static int cbc_paes_do_one_request(struct crypto_engine *engine, void *areq)
{
	struct skcipher_request *req = skcipher_request_cast(areq);
	struct s390_pcbc_req_ctx *req_ctx = skcipher_request_ctx(req);
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct s390_paes_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct skcipher_walk *walk = &req_ctx->walk;
	int rc;

	/* walk has already been prepared */

	rc = cbc_paes_do_crypt(ctx, req_ctx, true);
	if (rc == -EKEYEXPIRED) {
		/*
		 * Protected key expired, conversion is in process.
		 * Trigger a re-schedule of this request by returning
		 * -ENOSPC ("hardware queue is full") to the crypto engine.
		 * To avoid immediately re-invocation of this callback,
		 * tell the scheduler to voluntarily give up the CPU here.
		 */
		cond_resched();
		pr_debug("rescheduling request\n");
		return -ENOSPC;
	} else if (rc) {
		skcipher_walk_done(walk, rc);
	}

	memzero_explicit(&req_ctx->param, sizeof(req_ctx->param));
	pr_debug("request complete with rc=%d\n", rc);
	local_bh_disable();
	atomic_dec(&ctx->via_engine_ctr);
	crypto_finalize_skcipher_request(engine, req, rc);
	local_bh_enable();
	return rc;
}

static struct skcipher_engine_alg cbc_paes_alg = {
	.base = {
		.base.cra_name	      = "cbc(paes)",
		.base.cra_driver_name = "cbc-paes-s390",
		.base.cra_priority    = 402,	/* cbc-paes-s390 + 1 */
		.base.cra_blocksize   = AES_BLOCK_SIZE,
		.base.cra_ctxsize     = sizeof(struct s390_paes_ctx),
		.base.cra_module      = THIS_MODULE,
		.base.cra_list	      = LIST_HEAD_INIT(cbc_paes_alg.base.base.cra_list),
		.init		      = cbc_paes_init,
		.exit		      = cbc_paes_exit,
		.min_keysize	      = PAES_MIN_KEYSIZE,
		.max_keysize	      = PAES_MAX_KEYSIZE,
		.ivsize		      = AES_BLOCK_SIZE,
		.setkey		      = cbc_paes_setkey,
		.encrypt	      = cbc_paes_encrypt,
		.decrypt	      = cbc_paes_decrypt,
	},
	.op = {
		.do_one_request	      = cbc_paes_do_one_request,
	},
};

/*
 * PAES CTR implementation
 */

struct ctr_param {
	u8 key[PAES_256_PROTKEY_SIZE];
} __packed;

struct s390_pctr_req_ctx {
	unsigned long modifier;
	struct skcipher_walk walk;
	bool param_init_done;
	struct ctr_param param;
};

static int ctr_paes_setkey(struct crypto_skcipher *tfm, const u8 *in_key,
			   unsigned int key_len)
{
	struct s390_paes_ctx *ctx = crypto_skcipher_ctx(tfm);
	long fc;
	int rc;

	/* set raw key into context */
	rc = paes_ctx_setkey(ctx, in_key, key_len);
	if (rc)
		goto out;

	/* convert raw key into protected key */
	rc = paes_convert_key(ctx);
	if (rc)
		goto out;

	/* Pick the correct function code based on the protected key type */
	switch (ctx->pk.type) {
	case PKEY_KEYTYPE_AES_128:
		fc = CPACF_KMCTR_PAES_128;
		break;
	case PKEY_KEYTYPE_AES_192:
		fc = CPACF_KMCTR_PAES_192;
		break;
	case PKEY_KEYTYPE_AES_256:
		fc = CPACF_KMCTR_PAES_256;
		break;
	default:
		fc = 0;
		break;
	}
	ctx->fc = (fc && cpacf_test_func(&kmctr_functions, fc)) ? fc : 0;

	rc = fc ? 0 : -EINVAL;

out:
	pr_debug("rc=%d\n", rc);
	return rc;
}

static inline unsigned int __ctrblk_init(u8 *ctrptr, u8 *iv, unsigned int nbytes)
{
	unsigned int i, n;

	/* only use complete blocks, max. PAGE_SIZE */
	memcpy(ctrptr, iv, AES_BLOCK_SIZE);
	n = (nbytes > PAGE_SIZE) ? PAGE_SIZE : nbytes & ~(AES_BLOCK_SIZE - 1);
	for (i = (n / AES_BLOCK_SIZE) - 1; i > 0; i--) {
		memcpy(ctrptr + AES_BLOCK_SIZE, ctrptr, AES_BLOCK_SIZE);
		crypto_inc(ctrptr + AES_BLOCK_SIZE, AES_BLOCK_SIZE);
		ctrptr += AES_BLOCK_SIZE;
	}
	return n;
}

static int ctr_paes_do_crypt(struct s390_paes_ctx *ctx,
			     struct s390_pctr_req_ctx *req_ctx,
			     bool maysleep)
{
	struct ctr_param *param = &req_ctx->param;
	struct skcipher_walk *walk = &req_ctx->walk;
	u8 buf[AES_BLOCK_SIZE], *ctrptr;
	unsigned int nbytes, n, k;
	int pk_state, locked, rc = 0;

	if (!req_ctx->param_init_done) {
		/* fetch and check protected key state */
		spin_lock_bh(&ctx->pk_lock);
		pk_state = ctx->pk_state;
		switch (pk_state) {
		case PK_STATE_NO_KEY:
			rc = -ENOKEY;
			break;
		case PK_STATE_CONVERT_IN_PROGRESS:
			rc = -EKEYEXPIRED;
			break;
		case PK_STATE_VALID:
			memcpy(param->key, ctx->pk.protkey, sizeof(param->key));
			req_ctx->param_init_done = true;
			break;
		default:
			rc = pk_state < 0 ? pk_state : -EIO;
			break;
		}
		spin_unlock_bh(&ctx->pk_lock);
	}
	if (rc)
		goto out;

	locked = mutex_trylock(&ctrblk_lock);

	/*
	 * Note that in case of partial processing or failure the walk
	 * is NOT unmapped here. So a follow up task may reuse the walk
	 * or in case of unrecoverable failure needs to unmap it.
	 */
	while ((nbytes = walk->nbytes) >= AES_BLOCK_SIZE) {
		n = AES_BLOCK_SIZE;
		if (nbytes >= 2 * AES_BLOCK_SIZE && locked)
			n = __ctrblk_init(ctrblk, walk->iv, nbytes);
		ctrptr = (n > AES_BLOCK_SIZE) ? ctrblk : walk->iv;
		k = cpacf_kmctr(ctx->fc, param, walk->dst.virt.addr,
				walk->src.virt.addr, n, ctrptr);
		if (k) {
			if (ctrptr == ctrblk)
				memcpy(walk->iv, ctrptr + k - AES_BLOCK_SIZE,
				       AES_BLOCK_SIZE);
			crypto_inc(walk->iv, AES_BLOCK_SIZE);
			rc = skcipher_walk_done(walk, nbytes - k);
		}
		if (k < n) {
			if (!maysleep) {
				if (locked)
					mutex_unlock(&ctrblk_lock);
				rc = -EKEYEXPIRED;
				goto out;
			}
			rc = paes_convert_key(ctx);
			if (rc) {
				if (locked)
					mutex_unlock(&ctrblk_lock);
				goto out;
			}
			spin_lock_bh(&ctx->pk_lock);
			memcpy(param->key, ctx->pk.protkey, sizeof(param->key));
			spin_unlock_bh(&ctx->pk_lock);
		}
	}
	if (locked)
		mutex_unlock(&ctrblk_lock);

	/* final block may be < AES_BLOCK_SIZE, copy only nbytes */
	if (nbytes) {
		memset(buf, 0, AES_BLOCK_SIZE);
		memcpy(buf, walk->src.virt.addr, nbytes);
		while (1) {
			if (cpacf_kmctr(ctx->fc, param, buf,
					buf, AES_BLOCK_SIZE,
					walk->iv) == AES_BLOCK_SIZE)
				break;
			if (!maysleep) {
				rc = -EKEYEXPIRED;
				goto out;
			}
			rc = paes_convert_key(ctx);
			if (rc)
				goto out;
			spin_lock_bh(&ctx->pk_lock);
			memcpy(param->key, ctx->pk.protkey, sizeof(param->key));
			spin_unlock_bh(&ctx->pk_lock);
		}
		memcpy(walk->dst.virt.addr, buf, nbytes);
		crypto_inc(walk->iv, AES_BLOCK_SIZE);
		rc = skcipher_walk_done(walk, 0);
	}

out:
	pr_debug("rc=%d\n", rc);
	return rc;
}

static int ctr_paes_crypt(struct skcipher_request *req)
{
	struct s390_pctr_req_ctx *req_ctx = skcipher_request_ctx(req);
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct s390_paes_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct skcipher_walk *walk = &req_ctx->walk;
	int rc;

	/*
	 * Attempt synchronous encryption first. If it fails, schedule the request
	 * asynchronously via the crypto engine. To preserve execution order,
	 * once a request is queued to the engine, further requests using the same
	 * tfm will also be routed through the engine.
	 */

	rc = skcipher_walk_virt(walk, req, false);
	if (rc)
		goto out;

	req_ctx->param_init_done = false;

	/* Try synchronous operation if no active engine usage */
	if (!atomic_read(&ctx->via_engine_ctr)) {
		rc = ctr_paes_do_crypt(ctx, req_ctx, false);
		if (rc == 0)
			goto out;
	}

	/*
	 * If sync operation failed or key expired or there are already
	 * requests enqueued via engine, fallback to async. Mark tfm as
	 * using engine to serialize requests.
	 */
	if (rc == 0 || rc == -EKEYEXPIRED) {
		atomic_inc(&ctx->via_engine_ctr);
		rc = crypto_transfer_skcipher_request_to_engine(paes_crypto_engine, req);
		if (rc != -EINPROGRESS)
			atomic_dec(&ctx->via_engine_ctr);
	}

	if (rc != -EINPROGRESS)
		skcipher_walk_done(walk, rc);

out:
	if (rc != -EINPROGRESS)
		memzero_explicit(&req_ctx->param, sizeof(req_ctx->param));
	pr_debug("rc=%d\n", rc);
	return rc;
}

static int ctr_paes_init(struct crypto_skcipher *tfm)
{
	struct s390_paes_ctx *ctx = crypto_skcipher_ctx(tfm);

	memset(ctx, 0, sizeof(*ctx));
	spin_lock_init(&ctx->pk_lock);

	crypto_skcipher_set_reqsize(tfm, sizeof(struct s390_pctr_req_ctx));

	return 0;
}

static void ctr_paes_exit(struct crypto_skcipher *tfm)
{
	struct s390_paes_ctx *ctx = crypto_skcipher_ctx(tfm);

	memzero_explicit(ctx, sizeof(*ctx));
}

static int ctr_paes_do_one_request(struct crypto_engine *engine, void *areq)
{
	struct skcipher_request *req = skcipher_request_cast(areq);
	struct s390_pctr_req_ctx *req_ctx = skcipher_request_ctx(req);
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct s390_paes_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct skcipher_walk *walk = &req_ctx->walk;
	int rc;

	/* walk has already been prepared */

	rc = ctr_paes_do_crypt(ctx, req_ctx, true);
	if (rc == -EKEYEXPIRED) {
		/*
		 * Protected key expired, conversion is in process.
		 * Trigger a re-schedule of this request by returning
		 * -ENOSPC ("hardware queue is full") to the crypto engine.
		 * To avoid immediately re-invocation of this callback,
		 * tell the scheduler to voluntarily give up the CPU here.
		 */
		cond_resched();
		pr_debug("rescheduling request\n");
		return -ENOSPC;
	} else if (rc) {
		skcipher_walk_done(walk, rc);
	}

	memzero_explicit(&req_ctx->param, sizeof(req_ctx->param));
	pr_debug("request complete with rc=%d\n", rc);
	local_bh_disable();
	atomic_dec(&ctx->via_engine_ctr);
	crypto_finalize_skcipher_request(engine, req, rc);
	local_bh_enable();
	return rc;
}

static struct skcipher_engine_alg ctr_paes_alg = {
	.base = {
		.base.cra_name	      =	"ctr(paes)",
		.base.cra_driver_name =	"ctr-paes-s390",
		.base.cra_priority    =	402,	/* ecb-paes-s390 + 1 */
		.base.cra_blocksize   =	1,
		.base.cra_ctxsize     =	sizeof(struct s390_paes_ctx),
		.base.cra_module      =	THIS_MODULE,
		.base.cra_list	      =	LIST_HEAD_INIT(ctr_paes_alg.base.base.cra_list),
		.init		      =	ctr_paes_init,
		.exit		      =	ctr_paes_exit,
		.min_keysize	      =	PAES_MIN_KEYSIZE,
		.max_keysize	      =	PAES_MAX_KEYSIZE,
		.ivsize		      =	AES_BLOCK_SIZE,
		.setkey		      =	ctr_paes_setkey,
		.encrypt	      =	ctr_paes_crypt,
		.decrypt	      =	ctr_paes_crypt,
		.chunksize	      =	AES_BLOCK_SIZE,
	},
	.op = {
		.do_one_request	      = ctr_paes_do_one_request,
	},
};

/*
 * PAES XTS implementation
 */

struct xts_full_km_param {
	u8 key[64];
	u8 tweak[16];
	u8 nap[16];
	u8 wkvp[32];
} __packed;

struct xts_km_param {
	u8 key[PAES_256_PROTKEY_SIZE];
	u8 init[16];
} __packed;

struct xts_pcc_param {
	u8 key[PAES_256_PROTKEY_SIZE];
	u8 tweak[16];
	u8 block[16];
	u8 bit[16];
	u8 xts[16];
} __packed;

struct s390_pxts_req_ctx {
	unsigned long modifier;
	struct skcipher_walk walk;
	bool param_init_done;
	union {
		struct xts_full_km_param full_km_param;
		struct xts_km_param km_param;
	} param;
};

static int xts_paes_setkey(struct crypto_skcipher *tfm, const u8 *in_key,
			   unsigned int in_keylen)
{
	struct s390_pxts_ctx *ctx = crypto_skcipher_ctx(tfm);
	u8 ckey[2 * AES_MAX_KEY_SIZE];
	unsigned int ckey_len;
	long fc;
	int rc;

	if ((in_keylen == 32 || in_keylen == 64) &&
	    xts_verify_key(tfm, in_key, in_keylen))
		return -EINVAL;

	/* set raw key into context */
	rc = pxts_ctx_setkey(ctx, in_key, in_keylen);
	if (rc)
		goto out;

	/* convert raw key(s) into protected key(s) */
	rc = pxts_convert_key(ctx);
	if (rc)
		goto out;

	/*
	 * xts_verify_key verifies the key length is not odd and makes
	 * sure that the two keys are not the same. This can be done
	 * on the two protected keys as well - but not for full xts keys.
	 */
	if (ctx->pk[0].type == PKEY_KEYTYPE_AES_128 ||
	    ctx->pk[0].type == PKEY_KEYTYPE_AES_256) {
		ckey_len = (ctx->pk[0].type == PKEY_KEYTYPE_AES_128) ?
			AES_KEYSIZE_128 : AES_KEYSIZE_256;
		memcpy(ckey, ctx->pk[0].protkey, ckey_len);
		memcpy(ckey + ckey_len, ctx->pk[1].protkey, ckey_len);
		rc = xts_verify_key(tfm, ckey, 2 * ckey_len);
		memzero_explicit(ckey, sizeof(ckey));
		if (rc)
			goto out;
	}

	/* Pick the correct function code based on the protected key type */
	switch (ctx->pk[0].type) {
	case PKEY_KEYTYPE_AES_128:
		fc = CPACF_KM_PXTS_128;
		break;
	case PKEY_KEYTYPE_AES_256:
		fc = CPACF_KM_PXTS_256;
		break;
	case PKEY_KEYTYPE_AES_XTS_128:
		fc = CPACF_KM_PXTS_128_FULL;
		break;
	case PKEY_KEYTYPE_AES_XTS_256:
		fc = CPACF_KM_PXTS_256_FULL;
		break;
	default:
		fc = 0;
		break;
	}
	ctx->fc = (fc && cpacf_test_func(&km_functions, fc)) ? fc : 0;

	rc = fc ? 0 : -EINVAL;

out:
	pr_debug("rc=%d\n", rc);
	return rc;
}

static int xts_paes_do_crypt_fullkey(struct s390_pxts_ctx *ctx,
				     struct s390_pxts_req_ctx *req_ctx,
				     bool maysleep)
{
	struct xts_full_km_param *param = &req_ctx->param.full_km_param;
	struct skcipher_walk *walk = &req_ctx->walk;
	unsigned int keylen, offset, nbytes, n, k;
	int rc = 0;

	/*
	 * The calling function xts_paes_do_crypt() ensures the
	 * protected key state is always PK_STATE_VALID when this
	 * function is invoked.
	 */

	keylen = (ctx->pk[0].type == PKEY_KEYTYPE_AES_XTS_128) ? 32 : 64;
	offset = (ctx->pk[0].type == PKEY_KEYTYPE_AES_XTS_128) ? 32 : 0;

	if (!req_ctx->param_init_done) {
		memset(param, 0, sizeof(*param));
		spin_lock_bh(&ctx->pk_lock);
		memcpy(param->key + offset, ctx->pk[0].protkey, keylen);
		memcpy(param->wkvp, ctx->pk[0].protkey + keylen, sizeof(param->wkvp));
		spin_unlock_bh(&ctx->pk_lock);
		memcpy(param->tweak, walk->iv, sizeof(param->tweak));
		param->nap[0] = 0x01; /* initial alpha power (1, little-endian) */
		req_ctx->param_init_done = true;
	}

	/*
	 * Note that in case of partial processing or failure the walk
	 * is NOT unmapped here. So a follow up task may reuse the walk
	 * or in case of unrecoverable failure needs to unmap it.
	 */
	while ((nbytes = walk->nbytes) != 0) {
		/* only use complete blocks */
		n = nbytes & ~(AES_BLOCK_SIZE - 1);
		k = cpacf_km(ctx->fc | req_ctx->modifier, param->key + offset,
			     walk->dst.virt.addr, walk->src.virt.addr, n);
		if (k)
			rc = skcipher_walk_done(walk, nbytes - k);
		if (k < n) {
			if (!maysleep) {
				rc = -EKEYEXPIRED;
				goto out;
			}
			rc = pxts_convert_key(ctx);
			if (rc)
				goto out;
			spin_lock_bh(&ctx->pk_lock);
			memcpy(param->key + offset, ctx->pk[0].protkey, keylen);
			memcpy(param->wkvp, ctx->pk[0].protkey + keylen, sizeof(param->wkvp));
			spin_unlock_bh(&ctx->pk_lock);
		}
	}

out:
	pr_debug("rc=%d\n", rc);
	return rc;
}

static inline int __xts_2keys_prep_param(struct s390_pxts_ctx *ctx,
					 struct xts_km_param *param,
					 struct skcipher_walk *walk,
					 unsigned int keylen,
					 unsigned int offset, bool maysleep)
{
	struct xts_pcc_param pcc_param;
	unsigned long cc = 1;
	int rc = 0;

	while (cc) {
		memset(&pcc_param, 0, sizeof(pcc_param));
		memcpy(pcc_param.tweak, walk->iv, sizeof(pcc_param.tweak));
		spin_lock_bh(&ctx->pk_lock);
		memcpy(pcc_param.key + offset, ctx->pk[1].protkey, keylen);
		memcpy(param->key + offset, ctx->pk[0].protkey, keylen);
		spin_unlock_bh(&ctx->pk_lock);
		cc = cpacf_pcc(ctx->fc, pcc_param.key + offset);
		if (cc) {
			if (!maysleep) {
				rc = -EKEYEXPIRED;
				break;
			}
			rc = pxts_convert_key(ctx);
			if (rc)
				break;
			continue;
		}
		memcpy(param->init, pcc_param.xts, 16);
	}

	memzero_explicit(pcc_param.key, sizeof(pcc_param.key));
	return rc;
}

static int xts_paes_do_crypt_2keys(struct s390_pxts_ctx *ctx,
				   struct s390_pxts_req_ctx *req_ctx,
				   bool maysleep)
{
	struct xts_km_param *param = &req_ctx->param.km_param;
	struct skcipher_walk *walk = &req_ctx->walk;
	unsigned int keylen, offset, nbytes, n, k;
	int rc = 0;

	/*
	 * The calling function xts_paes_do_crypt() ensures the
	 * protected key state is always PK_STATE_VALID when this
	 * function is invoked.
	 */

	keylen = (ctx->pk[0].type == PKEY_KEYTYPE_AES_128) ? 48 : 64;
	offset = (ctx->pk[0].type == PKEY_KEYTYPE_AES_128) ? 16 : 0;

	if (!req_ctx->param_init_done) {
		rc = __xts_2keys_prep_param(ctx, param, walk,
					    keylen, offset, maysleep);
		if (rc)
			goto out;
		req_ctx->param_init_done = true;
	}

	/*
	 * Note that in case of partial processing or failure the walk
	 * is NOT unmapped here. So a follow up task may reuse the walk
	 * or in case of unrecoverable failure needs to unmap it.
	 */
	while ((nbytes = walk->nbytes) != 0) {
		/* only use complete blocks */
		n = nbytes & ~(AES_BLOCK_SIZE - 1);
		k = cpacf_km(ctx->fc | req_ctx->modifier, param->key + offset,
			     walk->dst.virt.addr, walk->src.virt.addr, n);
		if (k)
			rc = skcipher_walk_done(walk, nbytes - k);
		if (k < n) {
			if (!maysleep) {
				rc = -EKEYEXPIRED;
				goto out;
			}
			rc = pxts_convert_key(ctx);
			if (rc)
				goto out;
			spin_lock_bh(&ctx->pk_lock);
			memcpy(param->key + offset, ctx->pk[0].protkey, keylen);
			spin_unlock_bh(&ctx->pk_lock);
		}
	}

out:
	pr_debug("rc=%d\n", rc);
	return rc;
}

static int xts_paes_do_crypt(struct s390_pxts_ctx *ctx,
			     struct s390_pxts_req_ctx *req_ctx,
			     bool maysleep)
{
	int pk_state, rc = 0;

	/* fetch and check protected key state */
	spin_lock_bh(&ctx->pk_lock);
	pk_state = ctx->pk_state;
	switch (pk_state) {
	case PK_STATE_NO_KEY:
		rc = -ENOKEY;
		break;
	case PK_STATE_CONVERT_IN_PROGRESS:
		rc = -EKEYEXPIRED;
		break;
	case PK_STATE_VALID:
		break;
	default:
		rc = pk_state < 0 ? pk_state : -EIO;
		break;
	}
	spin_unlock_bh(&ctx->pk_lock);
	if (rc)
		goto out;

	/* Call the 'real' crypt function based on the xts prot key type. */
	switch (ctx->fc) {
	case CPACF_KM_PXTS_128:
	case CPACF_KM_PXTS_256:
		rc = xts_paes_do_crypt_2keys(ctx, req_ctx, maysleep);
		break;
	case CPACF_KM_PXTS_128_FULL:
	case CPACF_KM_PXTS_256_FULL:
		rc = xts_paes_do_crypt_fullkey(ctx, req_ctx, maysleep);
		break;
	default:
		rc = -EINVAL;
	}

out:
	pr_debug("rc=%d\n", rc);
	return rc;
}

static inline int xts_paes_crypt(struct skcipher_request *req, unsigned long modifier)
{
	struct s390_pxts_req_ctx *req_ctx = skcipher_request_ctx(req);
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct s390_pxts_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct skcipher_walk *walk = &req_ctx->walk;
	int rc;

	/*
	 * Attempt synchronous encryption first. If it fails, schedule the request
	 * asynchronously via the crypto engine. To preserve execution order,
	 * once a request is queued to the engine, further requests using the same
	 * tfm will also be routed through the engine.
	 */

	rc = skcipher_walk_virt(walk, req, false);
	if (rc)
		goto out;

	req_ctx->modifier = modifier;
	req_ctx->param_init_done = false;

	/* Try synchronous operation if no active engine usage */
	if (!atomic_read(&ctx->via_engine_ctr)) {
		rc = xts_paes_do_crypt(ctx, req_ctx, false);
		if (rc == 0)
			goto out;
	}

	/*
	 * If sync operation failed or key expired or there are already
	 * requests enqueued via engine, fallback to async. Mark tfm as
	 * using engine to serialize requests.
	 */
	if (rc == 0 || rc == -EKEYEXPIRED) {
		atomic_inc(&ctx->via_engine_ctr);
		rc = crypto_transfer_skcipher_request_to_engine(paes_crypto_engine, req);
		if (rc != -EINPROGRESS)
			atomic_dec(&ctx->via_engine_ctr);
	}

	if (rc != -EINPROGRESS)
		skcipher_walk_done(walk, rc);

out:
	if (rc != -EINPROGRESS)
		memzero_explicit(&req_ctx->param, sizeof(req_ctx->param));
	pr_debug("rc=%d\n", rc);
	return rc;
}

static int xts_paes_encrypt(struct skcipher_request *req)
{
	return xts_paes_crypt(req, 0);
}

static int xts_paes_decrypt(struct skcipher_request *req)
{
	return xts_paes_crypt(req, CPACF_DECRYPT);
}

static int xts_paes_init(struct crypto_skcipher *tfm)
{
	struct s390_pxts_ctx *ctx = crypto_skcipher_ctx(tfm);

	memset(ctx, 0, sizeof(*ctx));
	spin_lock_init(&ctx->pk_lock);

	crypto_skcipher_set_reqsize(tfm, sizeof(struct s390_pxts_req_ctx));

	return 0;
}

static void xts_paes_exit(struct crypto_skcipher *tfm)
{
	struct s390_pxts_ctx *ctx = crypto_skcipher_ctx(tfm);

	memzero_explicit(ctx, sizeof(*ctx));
}

static int xts_paes_do_one_request(struct crypto_engine *engine, void *areq)
{
	struct skcipher_request *req = skcipher_request_cast(areq);
	struct s390_pxts_req_ctx *req_ctx = skcipher_request_ctx(req);
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct s390_pxts_ctx *ctx = crypto_skcipher_ctx(tfm);
	struct skcipher_walk *walk = &req_ctx->walk;
	int rc;

	/* walk has already been prepared */

	rc = xts_paes_do_crypt(ctx, req_ctx, true);
	if (rc == -EKEYEXPIRED) {
		/*
		 * Protected key expired, conversion is in process.
		 * Trigger a re-schedule of this request by returning
		 * -ENOSPC ("hardware queue is full") to the crypto engine.
		 * To avoid immediately re-invocation of this callback,
		 * tell the scheduler to voluntarily give up the CPU here.
		 */
		cond_resched();
		pr_debug("rescheduling request\n");
		return -ENOSPC;
	} else if (rc) {
		skcipher_walk_done(walk, rc);
	}

	memzero_explicit(&req_ctx->param, sizeof(req_ctx->param));
	pr_debug("request complete with rc=%d\n", rc);
	local_bh_disable();
	atomic_dec(&ctx->via_engine_ctr);
	crypto_finalize_skcipher_request(engine, req, rc);
	local_bh_enable();
	return rc;
}

static struct skcipher_engine_alg xts_paes_alg = {
	.base = {
		.base.cra_name	      =	"xts(paes)",
		.base.cra_driver_name =	"xts-paes-s390",
		.base.cra_priority    =	402,	/* ecb-paes-s390 + 1 */
		.base.cra_blocksize   =	AES_BLOCK_SIZE,
		.base.cra_ctxsize     =	sizeof(struct s390_pxts_ctx),
		.base.cra_module      =	THIS_MODULE,
		.base.cra_list	      =	LIST_HEAD_INIT(xts_paes_alg.base.base.cra_list),
		.init		      =	xts_paes_init,
		.exit		      =	xts_paes_exit,
		.min_keysize	      =	2 * PAES_MIN_KEYSIZE,
		.max_keysize	      =	2 * PAES_MAX_KEYSIZE,
		.ivsize		      =	AES_BLOCK_SIZE,
		.setkey		      =	xts_paes_setkey,
		.encrypt	      =	xts_paes_encrypt,
		.decrypt	      =	xts_paes_decrypt,
	},
	.op = {
		.do_one_request	      = xts_paes_do_one_request,
	},
};

/*
 * alg register, unregister, module init, exit
 */

static struct miscdevice paes_dev = {
	.name	= "paes",
	.minor	= MISC_DYNAMIC_MINOR,
};

static inline void __crypto_unregister_skcipher(struct skcipher_engine_alg *alg)
{
	if (!list_empty(&alg->base.base.cra_list))
		crypto_engine_unregister_skcipher(alg);
}

static void paes_s390_fini(void)
{
	if (paes_crypto_engine) {
		crypto_engine_stop(paes_crypto_engine);
		crypto_engine_exit(paes_crypto_engine);
	}
	__crypto_unregister_skcipher(&ctr_paes_alg);
	__crypto_unregister_skcipher(&xts_paes_alg);
	__crypto_unregister_skcipher(&cbc_paes_alg);
	__crypto_unregister_skcipher(&ecb_paes_alg);
	if (ctrblk)
		free_page((unsigned long)ctrblk);
	misc_deregister(&paes_dev);
}

static int __init paes_s390_init(void)
{
	int rc;

	/* register a simple paes pseudo misc device */
	rc = misc_register(&paes_dev);
	if (rc)
		return rc;

	/* with this pseudo devie alloc and start a crypto engine */
	paes_crypto_engine =
		crypto_engine_alloc_init_and_set(paes_dev.this_device,
						 true, false, MAX_QLEN);
	if (!paes_crypto_engine) {
		rc = -ENOMEM;
		goto out_err;
	}
	rc = crypto_engine_start(paes_crypto_engine);
	if (rc) {
		crypto_engine_exit(paes_crypto_engine);
		paes_crypto_engine = NULL;
		goto out_err;
	}

	/* Query available functions for KM, KMC and KMCTR */
	cpacf_query(CPACF_KM, &km_functions);
	cpacf_query(CPACF_KMC, &kmc_functions);
	cpacf_query(CPACF_KMCTR, &kmctr_functions);

	if (cpacf_test_func(&km_functions, CPACF_KM_PAES_128) ||
	    cpacf_test_func(&km_functions, CPACF_KM_PAES_192) ||
	    cpacf_test_func(&km_functions, CPACF_KM_PAES_256)) {
		rc = crypto_engine_register_skcipher(&ecb_paes_alg);
		if (rc)
			goto out_err;
		pr_debug("%s registered\n", ecb_paes_alg.base.base.cra_driver_name);
	}

	if (cpacf_test_func(&kmc_functions, CPACF_KMC_PAES_128) ||
	    cpacf_test_func(&kmc_functions, CPACF_KMC_PAES_192) ||
	    cpacf_test_func(&kmc_functions, CPACF_KMC_PAES_256)) {
		rc = crypto_engine_register_skcipher(&cbc_paes_alg);
		if (rc)
			goto out_err;
		pr_debug("%s registered\n", cbc_paes_alg.base.base.cra_driver_name);
	}

	if (cpacf_test_func(&km_functions, CPACF_KM_PXTS_128) ||
	    cpacf_test_func(&km_functions, CPACF_KM_PXTS_256)) {
		rc = crypto_engine_register_skcipher(&xts_paes_alg);
		if (rc)
			goto out_err;
		pr_debug("%s registered\n", xts_paes_alg.base.base.cra_driver_name);
	}

	if (cpacf_test_func(&kmctr_functions, CPACF_KMCTR_PAES_128) ||
	    cpacf_test_func(&kmctr_functions, CPACF_KMCTR_PAES_192) ||
	    cpacf_test_func(&kmctr_functions, CPACF_KMCTR_PAES_256)) {
		ctrblk = (u8 *)__get_free_page(GFP_KERNEL);
		if (!ctrblk) {
			rc = -ENOMEM;
			goto out_err;
		}
		rc = crypto_engine_register_skcipher(&ctr_paes_alg);
		if (rc)
			goto out_err;
		pr_debug("%s registered\n", ctr_paes_alg.base.base.cra_driver_name);
	}

	return 0;

out_err:
	paes_s390_fini();
	return rc;
}

module_init(paes_s390_init);
module_exit(paes_s390_fini);

MODULE_ALIAS_CRYPTO("ecb(paes)");
MODULE_ALIAS_CRYPTO("cbc(paes)");
MODULE_ALIAS_CRYPTO("ctr(paes)");
MODULE_ALIAS_CRYPTO("xts(paes)");

MODULE_DESCRIPTION("Rijndael (AES) Cipher Algorithm with protected keys");
MODULE_LICENSE("GPL");
