/* n2_core.c: Niagara2 Stream Processing Unit (SPU) crypto support.
 *
 * Copyright (C) 2010, 2011 David S. Miller <davem@davemloft.net>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/cpumask.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/crypto.h>
#include <crypto/md5.h>
#include <crypto/sha.h>
#include <crypto/aes.h>
#include <crypto/des.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/sched.h>

#include <crypto/internal/hash.h>
#include <crypto/scatterwalk.h>
#include <crypto/algapi.h>

#include <asm/hypervisor.h>
#include <asm/mdesc.h>

#include "n2_core.h"

#define DRV_MODULE_NAME		"n2_crypto"
#define DRV_MODULE_VERSION	"0.2"
#define DRV_MODULE_RELDATE	"July 28, 2011"

static const char version[] =
	DRV_MODULE_NAME ".c:v" DRV_MODULE_VERSION " (" DRV_MODULE_RELDATE ")\n";

MODULE_AUTHOR("David S. Miller (davem@davemloft.net)");
MODULE_DESCRIPTION("Niagara2 Crypto driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_MODULE_VERSION);

#define N2_CRA_PRIORITY		200

static DEFINE_MUTEX(spu_lock);

struct spu_queue {
	cpumask_t		sharing;
	unsigned long		qhandle;

	spinlock_t		lock;
	u8			q_type;
	void			*q;
	unsigned long		head;
	unsigned long		tail;
	struct list_head	jobs;

	unsigned long		devino;

	char			irq_name[32];
	unsigned int		irq;

	struct list_head	list;
};

struct spu_qreg {
	struct spu_queue	*queue;
	unsigned long		type;
};

static struct spu_queue **cpu_to_cwq;
static struct spu_queue **cpu_to_mau;

static unsigned long spu_next_offset(struct spu_queue *q, unsigned long off)
{
	if (q->q_type == HV_NCS_QTYPE_MAU) {
		off += MAU_ENTRY_SIZE;
		if (off == (MAU_ENTRY_SIZE * MAU_NUM_ENTRIES))
			off = 0;
	} else {
		off += CWQ_ENTRY_SIZE;
		if (off == (CWQ_ENTRY_SIZE * CWQ_NUM_ENTRIES))
			off = 0;
	}
	return off;
}

struct n2_request_common {
	struct list_head	entry;
	unsigned int		offset;
};
#define OFFSET_NOT_RUNNING	(~(unsigned int)0)

/* An async job request records the final tail value it used in
 * n2_request_common->offset, test to see if that offset is in
 * the range old_head, new_head, inclusive.
 */
static inline bool job_finished(struct spu_queue *q, unsigned int offset,
				unsigned long old_head, unsigned long new_head)
{
	if (old_head <= new_head) {
		if (offset > old_head && offset <= new_head)
			return true;
	} else {
		if (offset > old_head || offset <= new_head)
			return true;
	}
	return false;
}

/* When the HEAD marker is unequal to the actual HEAD, we get
 * a virtual device INO interrupt.  We should process the
 * completed CWQ entries and adjust the HEAD marker to clear
 * the IRQ.
 */
static irqreturn_t cwq_intr(int irq, void *dev_id)
{
	unsigned long off, new_head, hv_ret;
	struct spu_queue *q = dev_id;

	pr_err("CPU[%d]: Got CWQ interrupt for qhdl[%lx]\n",
	       smp_processor_id(), q->qhandle);

	spin_lock(&q->lock);

	hv_ret = sun4v_ncs_gethead(q->qhandle, &new_head);

	pr_err("CPU[%d]: CWQ gethead[%lx] hv_ret[%lu]\n",
	       smp_processor_id(), new_head, hv_ret);

	for (off = q->head; off != new_head; off = spu_next_offset(q, off)) {
		/* XXX ... XXX */
	}

	hv_ret = sun4v_ncs_sethead_marker(q->qhandle, new_head);
	if (hv_ret == HV_EOK)
		q->head = new_head;

	spin_unlock(&q->lock);

	return IRQ_HANDLED;
}

static irqreturn_t mau_intr(int irq, void *dev_id)
{
	struct spu_queue *q = dev_id;
	unsigned long head, hv_ret;

	spin_lock(&q->lock);

	pr_err("CPU[%d]: Got MAU interrupt for qhdl[%lx]\n",
	       smp_processor_id(), q->qhandle);

	hv_ret = sun4v_ncs_gethead(q->qhandle, &head);

	pr_err("CPU[%d]: MAU gethead[%lx] hv_ret[%lu]\n",
	       smp_processor_id(), head, hv_ret);

	sun4v_ncs_sethead_marker(q->qhandle, head);

	spin_unlock(&q->lock);

	return IRQ_HANDLED;
}

static void *spu_queue_next(struct spu_queue *q, void *cur)
{
	return q->q + spu_next_offset(q, cur - q->q);
}

static int spu_queue_num_free(struct spu_queue *q)
{
	unsigned long head = q->head;
	unsigned long tail = q->tail;
	unsigned long end = (CWQ_ENTRY_SIZE * CWQ_NUM_ENTRIES);
	unsigned long diff;

	if (head > tail)
		diff = head - tail;
	else
		diff = (end - tail) + head;

	return (diff / CWQ_ENTRY_SIZE) - 1;
}

static void *spu_queue_alloc(struct spu_queue *q, int num_entries)
{
	int avail = spu_queue_num_free(q);

	if (avail >= num_entries)
		return q->q + q->tail;

	return NULL;
}

static unsigned long spu_queue_submit(struct spu_queue *q, void *last)
{
	unsigned long hv_ret, new_tail;

	new_tail = spu_next_offset(q, last - q->q);

	hv_ret = sun4v_ncs_settail(q->qhandle, new_tail);
	if (hv_ret == HV_EOK)
		q->tail = new_tail;
	return hv_ret;
}

static u64 control_word_base(unsigned int len, unsigned int hmac_key_len,
			     int enc_type, int auth_type,
			     unsigned int hash_len,
			     bool sfas, bool sob, bool eob, bool encrypt,
			     int opcode)
{
	u64 word = (len - 1) & CONTROL_LEN;

	word |= ((u64) opcode << CONTROL_OPCODE_SHIFT);
	word |= ((u64) enc_type << CONTROL_ENC_TYPE_SHIFT);
	word |= ((u64) auth_type << CONTROL_AUTH_TYPE_SHIFT);
	if (sfas)
		word |= CONTROL_STORE_FINAL_AUTH_STATE;
	if (sob)
		word |= CONTROL_START_OF_BLOCK;
	if (eob)
		word |= CONTROL_END_OF_BLOCK;
	if (encrypt)
		word |= CONTROL_ENCRYPT;
	if (hmac_key_len)
		word |= ((u64) (hmac_key_len - 1)) << CONTROL_HMAC_KEY_LEN_SHIFT;
	if (hash_len)
		word |= ((u64) (hash_len - 1)) << CONTROL_HASH_LEN_SHIFT;

	return word;
}

#if 0
static inline bool n2_should_run_async(struct spu_queue *qp, int this_len)
{
	if (this_len >= 64 ||
	    qp->head != qp->tail)
		return true;
	return false;
}
#endif

struct n2_ahash_alg {
	struct list_head	entry;
	const u8		*hash_zero;
	const u32		*hash_init;
	u8			hw_op_hashsz;
	u8			digest_size;
	u8			auth_type;
	u8			hmac_type;
	struct ahash_alg	alg;
};

static inline struct n2_ahash_alg *n2_ahash_alg(struct crypto_tfm *tfm)
{
	struct crypto_alg *alg = tfm->__crt_alg;
	struct ahash_alg *ahash_alg;

	ahash_alg = container_of(alg, struct ahash_alg, halg.base);

	return container_of(ahash_alg, struct n2_ahash_alg, alg);
}

struct n2_hmac_alg {
	const char		*child_alg;
	struct n2_ahash_alg	derived;
};

static inline struct n2_hmac_alg *n2_hmac_alg(struct crypto_tfm *tfm)
{
	struct crypto_alg *alg = tfm->__crt_alg;
	struct ahash_alg *ahash_alg;

	ahash_alg = container_of(alg, struct ahash_alg, halg.base);

	return container_of(ahash_alg, struct n2_hmac_alg, derived.alg);
}

struct n2_hash_ctx {
	struct crypto_ahash		*fallback_tfm;
};

#define N2_HASH_KEY_MAX			32 /* HW limit for all HMAC requests */

struct n2_hmac_ctx {
	struct n2_hash_ctx		base;

	struct crypto_shash		*child_shash;

	int				hash_key_len;
	unsigned char			hash_key[N2_HASH_KEY_MAX];
};

struct n2_hash_req_ctx {
	union {
		struct md5_state	md5;
		struct sha1_state	sha1;
		struct sha256_state	sha256;
	} u;

	struct ahash_request		fallback_req;
};

static int n2_hash_async_init(struct ahash_request *req)
{
	struct n2_hash_req_ctx *rctx = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct n2_hash_ctx *ctx = crypto_ahash_ctx(tfm);

	ahash_request_set_tfm(&rctx->fallback_req, ctx->fallback_tfm);
	rctx->fallback_req.base.flags = req->base.flags & CRYPTO_TFM_REQ_MAY_SLEEP;

	return crypto_ahash_init(&rctx->fallback_req);
}

static int n2_hash_async_update(struct ahash_request *req)
{
	struct n2_hash_req_ctx *rctx = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct n2_hash_ctx *ctx = crypto_ahash_ctx(tfm);

	ahash_request_set_tfm(&rctx->fallback_req, ctx->fallback_tfm);
	rctx->fallback_req.base.flags = req->base.flags & CRYPTO_TFM_REQ_MAY_SLEEP;
	rctx->fallback_req.nbytes = req->nbytes;
	rctx->fallback_req.src = req->src;

	return crypto_ahash_update(&rctx->fallback_req);
}

static int n2_hash_async_final(struct ahash_request *req)
{
	struct n2_hash_req_ctx *rctx = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct n2_hash_ctx *ctx = crypto_ahash_ctx(tfm);

	ahash_request_set_tfm(&rctx->fallback_req, ctx->fallback_tfm);
	rctx->fallback_req.base.flags = req->base.flags & CRYPTO_TFM_REQ_MAY_SLEEP;
	rctx->fallback_req.result = req->result;

	return crypto_ahash_final(&rctx->fallback_req);
}

static int n2_hash_async_finup(struct ahash_request *req)
{
	struct n2_hash_req_ctx *rctx = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct n2_hash_ctx *ctx = crypto_ahash_ctx(tfm);

	ahash_request_set_tfm(&rctx->fallback_req, ctx->fallback_tfm);
	rctx->fallback_req.base.flags = req->base.flags & CRYPTO_TFM_REQ_MAY_SLEEP;
	rctx->fallback_req.nbytes = req->nbytes;
	rctx->fallback_req.src = req->src;
	rctx->fallback_req.result = req->result;

	return crypto_ahash_finup(&rctx->fallback_req);
}

static int n2_hash_cra_init(struct crypto_tfm *tfm)
{
	const char *fallback_driver_name = crypto_tfm_alg_name(tfm);
	struct crypto_ahash *ahash = __crypto_ahash_cast(tfm);
	struct n2_hash_ctx *ctx = crypto_ahash_ctx(ahash);
	struct crypto_ahash *fallback_tfm;
	int err;

	fallback_tfm = crypto_alloc_ahash(fallback_driver_name, 0,
					  CRYPTO_ALG_NEED_FALLBACK);
	if (IS_ERR(fallback_tfm)) {
		pr_warning("Fallback driver '%s' could not be loaded!\n",
			   fallback_driver_name);
		err = PTR_ERR(fallback_tfm);
		goto out;
	}

	crypto_ahash_set_reqsize(ahash, (sizeof(struct n2_hash_req_ctx) +
					 crypto_ahash_reqsize(fallback_tfm)));

	ctx->fallback_tfm = fallback_tfm;
	return 0;

out:
	return err;
}

static void n2_hash_cra_exit(struct crypto_tfm *tfm)
{
	struct crypto_ahash *ahash = __crypto_ahash_cast(tfm);
	struct n2_hash_ctx *ctx = crypto_ahash_ctx(ahash);

	crypto_free_ahash(ctx->fallback_tfm);
}

static int n2_hmac_cra_init(struct crypto_tfm *tfm)
{
	const char *fallback_driver_name = crypto_tfm_alg_name(tfm);
	struct crypto_ahash *ahash = __crypto_ahash_cast(tfm);
	struct n2_hmac_ctx *ctx = crypto_ahash_ctx(ahash);
	struct n2_hmac_alg *n2alg = n2_hmac_alg(tfm);
	struct crypto_ahash *fallback_tfm;
	struct crypto_shash *child_shash;
	int err;

	fallback_tfm = crypto_alloc_ahash(fallback_driver_name, 0,
					  CRYPTO_ALG_NEED_FALLBACK);
	if (IS_ERR(fallback_tfm)) {
		pr_warning("Fallback driver '%s' could not be loaded!\n",
			   fallback_driver_name);
		err = PTR_ERR(fallback_tfm);
		goto out;
	}

	child_shash = crypto_alloc_shash(n2alg->child_alg, 0, 0);
	if (IS_ERR(child_shash)) {
		pr_warning("Child shash '%s' could not be loaded!\n",
			   n2alg->child_alg);
		err = PTR_ERR(child_shash);
		goto out_free_fallback;
	}

	crypto_ahash_set_reqsize(ahash, (sizeof(struct n2_hash_req_ctx) +
					 crypto_ahash_reqsize(fallback_tfm)));

	ctx->child_shash = child_shash;
	ctx->base.fallback_tfm = fallback_tfm;
	return 0;

out_free_fallback:
	crypto_free_ahash(fallback_tfm);

out:
	return err;
}

static void n2_hmac_cra_exit(struct crypto_tfm *tfm)
{
	struct crypto_ahash *ahash = __crypto_ahash_cast(tfm);
	struct n2_hmac_ctx *ctx = crypto_ahash_ctx(ahash);

	crypto_free_ahash(ctx->base.fallback_tfm);
	crypto_free_shash(ctx->child_shash);
}

static int n2_hmac_async_setkey(struct crypto_ahash *tfm, const u8 *key,
				unsigned int keylen)
{
	struct n2_hmac_ctx *ctx = crypto_ahash_ctx(tfm);
	struct crypto_shash *child_shash = ctx->child_shash;
	struct crypto_ahash *fallback_tfm;
	SHASH_DESC_ON_STACK(shash, child_shash);
	int err, bs, ds;

	fallback_tfm = ctx->base.fallback_tfm;
	err = crypto_ahash_setkey(fallback_tfm, key, keylen);
	if (err)
		return err;

	shash->tfm = child_shash;
	shash->flags = crypto_ahash_get_flags(tfm) &
		CRYPTO_TFM_REQ_MAY_SLEEP;

	bs = crypto_shash_blocksize(child_shash);
	ds = crypto_shash_digestsize(child_shash);
	BUG_ON(ds > N2_HASH_KEY_MAX);
	if (keylen > bs) {
		err = crypto_shash_digest(shash, key, keylen,
					  ctx->hash_key);
		if (err)
			return err;
		keylen = ds;
	} else if (keylen <= N2_HASH_KEY_MAX)
		memcpy(ctx->hash_key, key, keylen);

	ctx->hash_key_len = keylen;

	return err;
}

static unsigned long wait_for_tail(struct spu_queue *qp)
{
	unsigned long head, hv_ret;

	do {
		hv_ret = sun4v_ncs_gethead(qp->qhandle, &head);
		if (hv_ret != HV_EOK) {
			pr_err("Hypervisor error on gethead\n");
			break;
		}
		if (head == qp->tail) {
			qp->head = head;
			break;
		}
	} while (1);
	return hv_ret;
}

static unsigned long submit_and_wait_for_tail(struct spu_queue *qp,
					      struct cwq_initial_entry *ent)
{
	unsigned long hv_ret = spu_queue_submit(qp, ent);

	if (hv_ret == HV_EOK)
		hv_ret = wait_for_tail(qp);

	return hv_ret;
}

static int n2_do_async_digest(struct ahash_request *req,
			      unsigned int auth_type, unsigned int digest_size,
			      unsigned int result_size, void *hash_loc,
			      unsigned long auth_key, unsigned int auth_key_len)
{
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct cwq_initial_entry *ent;
	struct crypto_hash_walk walk;
	struct spu_queue *qp;
	unsigned long flags;
	int err = -ENODEV;
	int nbytes, cpu;

	/* The total effective length of the operation may not
	 * exceed 2^16.
	 */
	if (unlikely(req->nbytes > (1 << 16))) {
		struct n2_hash_req_ctx *rctx = ahash_request_ctx(req);
		struct n2_hash_ctx *ctx = crypto_ahash_ctx(tfm);

		ahash_request_set_tfm(&rctx->fallback_req, ctx->fallback_tfm);
		rctx->fallback_req.base.flags =
			req->base.flags & CRYPTO_TFM_REQ_MAY_SLEEP;
		rctx->fallback_req.nbytes = req->nbytes;
		rctx->fallback_req.src = req->src;
		rctx->fallback_req.result = req->result;

		return crypto_ahash_digest(&rctx->fallback_req);
	}

	nbytes = crypto_hash_walk_first(req, &walk);

	cpu = get_cpu();
	qp = cpu_to_cwq[cpu];
	if (!qp)
		goto out;

	spin_lock_irqsave(&qp->lock, flags);

	/* XXX can do better, improve this later by doing a by-hand scatterlist
	 * XXX walk, etc.
	 */
	ent = qp->q + qp->tail;

	ent->control = control_word_base(nbytes, auth_key_len, 0,
					 auth_type, digest_size,
					 false, true, false, false,
					 OPCODE_INPLACE_BIT |
					 OPCODE_AUTH_MAC);
	ent->src_addr = __pa(walk.data);
	ent->auth_key_addr = auth_key;
	ent->auth_iv_addr = __pa(hash_loc);
	ent->final_auth_state_addr = 0UL;
	ent->enc_key_addr = 0UL;
	ent->enc_iv_addr = 0UL;
	ent->dest_addr = __pa(hash_loc);

	nbytes = crypto_hash_walk_done(&walk, 0);
	while (nbytes > 0) {
		ent = spu_queue_next(qp, ent);

		ent->control = (nbytes - 1);
		ent->src_addr = __pa(walk.data);
		ent->auth_key_addr = 0UL;
		ent->auth_iv_addr = 0UL;
		ent->final_auth_state_addr = 0UL;
		ent->enc_key_addr = 0UL;
		ent->enc_iv_addr = 0UL;
		ent->dest_addr = 0UL;

		nbytes = crypto_hash_walk_done(&walk, 0);
	}
	ent->control |= CONTROL_END_OF_BLOCK;

	if (submit_and_wait_for_tail(qp, ent) != HV_EOK)
		err = -EINVAL;
	else
		err = 0;

	spin_unlock_irqrestore(&qp->lock, flags);

	if (!err)
		memcpy(req->result, hash_loc, result_size);
out:
	put_cpu();

	return err;
}

static int n2_hash_async_digest(struct ahash_request *req)
{
	struct n2_ahash_alg *n2alg = n2_ahash_alg(req->base.tfm);
	struct n2_hash_req_ctx *rctx = ahash_request_ctx(req);
	int ds;

	ds = n2alg->digest_size;
	if (unlikely(req->nbytes == 0)) {
		memcpy(req->result, n2alg->hash_zero, ds);
		return 0;
	}
	memcpy(&rctx->u, n2alg->hash_init, n2alg->hw_op_hashsz);

	return n2_do_async_digest(req, n2alg->auth_type,
				  n2alg->hw_op_hashsz, ds,
				  &rctx->u, 0UL, 0);
}

static int n2_hmac_async_digest(struct ahash_request *req)
{
	struct n2_hmac_alg *n2alg = n2_hmac_alg(req->base.tfm);
	struct n2_hash_req_ctx *rctx = ahash_request_ctx(req);
	struct crypto_ahash *tfm = crypto_ahash_reqtfm(req);
	struct n2_hmac_ctx *ctx = crypto_ahash_ctx(tfm);
	int ds;

	ds = n2alg->derived.digest_size;
	if (unlikely(req->nbytes == 0) ||
	    unlikely(ctx->hash_key_len > N2_HASH_KEY_MAX)) {
		struct n2_hash_req_ctx *rctx = ahash_request_ctx(req);
		struct n2_hash_ctx *ctx = crypto_ahash_ctx(tfm);

		ahash_request_set_tfm(&rctx->fallback_req, ctx->fallback_tfm);
		rctx->fallback_req.base.flags =
			req->base.flags & CRYPTO_TFM_REQ_MAY_SLEEP;
		rctx->fallback_req.nbytes = req->nbytes;
		rctx->fallback_req.src = req->src;
		rctx->fallback_req.result = req->result;

		return crypto_ahash_digest(&rctx->fallback_req);
	}
	memcpy(&rctx->u, n2alg->derived.hash_init,
	       n2alg->derived.hw_op_hashsz);

	return n2_do_async_digest(req, n2alg->derived.hmac_type,
				  n2alg->derived.hw_op_hashsz, ds,
				  &rctx->u,
				  __pa(&ctx->hash_key),
				  ctx->hash_key_len);
}

struct n2_cipher_context {
	int			key_len;
	int			enc_type;
	union {
		u8		aes[AES_MAX_KEY_SIZE];
		u8		des[DES_KEY_SIZE];
		u8		des3[3 * DES_KEY_SIZE];
		u8		arc4[258]; /* S-box, X, Y */
	} key;
};

#define N2_CHUNK_ARR_LEN	16

struct n2_crypto_chunk {
	struct list_head	entry;
	unsigned long		iv_paddr : 44;
	unsigned long		arr_len : 20;
	unsigned long		dest_paddr;
	unsigned long		dest_final;
	struct {
		unsigned long	src_paddr : 44;
		unsigned long	src_len : 20;
	} arr[N2_CHUNK_ARR_LEN];
};

struct n2_request_context {
	struct ablkcipher_walk	walk;
	struct list_head	chunk_list;
	struct n2_crypto_chunk	chunk;
	u8			temp_iv[16];
};

/* The SPU allows some level of flexibility for partial cipher blocks
 * being specified in a descriptor.
 *
 * It merely requires that every descriptor's length field is at least
 * as large as the cipher block size.  This means that a cipher block
 * can span at most 2 descriptors.  However, this does not allow a
 * partial block to span into the final descriptor as that would
 * violate the rule (since every descriptor's length must be at lest
 * the block size).  So, for example, assuming an 8 byte block size:
 *
 *	0xe --> 0xa --> 0x8
 *
 * is a valid length sequence, whereas:
 *
 *	0xe --> 0xb --> 0x7
 *
 * is not a valid sequence.
 */

struct n2_cipher_alg {
	struct list_head	entry;
	u8			enc_type;
	struct crypto_alg	alg;
};

static inline struct n2_cipher_alg *n2_cipher_alg(struct crypto_tfm *tfm)
{
	struct crypto_alg *alg = tfm->__crt_alg;

	return container_of(alg, struct n2_cipher_alg, alg);
}

struct n2_cipher_request_context {
	struct ablkcipher_walk	walk;
};

static int n2_aes_setkey(struct crypto_ablkcipher *cipher, const u8 *key,
			 unsigned int keylen)
{
	struct crypto_tfm *tfm = crypto_ablkcipher_tfm(cipher);
	struct n2_cipher_context *ctx = crypto_tfm_ctx(tfm);
	struct n2_cipher_alg *n2alg = n2_cipher_alg(tfm);

	ctx->enc_type = (n2alg->enc_type & ENC_TYPE_CHAINING_MASK);

	switch (keylen) {
	case AES_KEYSIZE_128:
		ctx->enc_type |= ENC_TYPE_ALG_AES128;
		break;
	case AES_KEYSIZE_192:
		ctx->enc_type |= ENC_TYPE_ALG_AES192;
		break;
	case AES_KEYSIZE_256:
		ctx->enc_type |= ENC_TYPE_ALG_AES256;
		break;
	default:
		crypto_ablkcipher_set_flags(cipher, CRYPTO_TFM_RES_BAD_KEY_LEN);
		return -EINVAL;
	}

	ctx->key_len = keylen;
	memcpy(ctx->key.aes, key, keylen);
	return 0;
}

static int n2_des_setkey(struct crypto_ablkcipher *cipher, const u8 *key,
			 unsigned int keylen)
{
	struct crypto_tfm *tfm = crypto_ablkcipher_tfm(cipher);
	struct n2_cipher_context *ctx = crypto_tfm_ctx(tfm);
	struct n2_cipher_alg *n2alg = n2_cipher_alg(tfm);
	u32 tmp[DES_EXPKEY_WORDS];
	int err;

	ctx->enc_type = n2alg->enc_type;

	if (keylen != DES_KEY_SIZE) {
		crypto_ablkcipher_set_flags(cipher, CRYPTO_TFM_RES_BAD_KEY_LEN);
		return -EINVAL;
	}

	err = des_ekey(tmp, key);
	if (err == 0 && (tfm->crt_flags & CRYPTO_TFM_REQ_WEAK_KEY)) {
		tfm->crt_flags |= CRYPTO_TFM_RES_WEAK_KEY;
		return -EINVAL;
	}

	ctx->key_len = keylen;
	memcpy(ctx->key.des, key, keylen);
	return 0;
}

static int n2_3des_setkey(struct crypto_ablkcipher *cipher, const u8 *key,
			  unsigned int keylen)
{
	struct crypto_tfm *tfm = crypto_ablkcipher_tfm(cipher);
	struct n2_cipher_context *ctx = crypto_tfm_ctx(tfm);
	struct n2_cipher_alg *n2alg = n2_cipher_alg(tfm);

	ctx->enc_type = n2alg->enc_type;

	if (keylen != (3 * DES_KEY_SIZE)) {
		crypto_ablkcipher_set_flags(cipher, CRYPTO_TFM_RES_BAD_KEY_LEN);
		return -EINVAL;
	}
	ctx->key_len = keylen;
	memcpy(ctx->key.des3, key, keylen);
	return 0;
}

static int n2_arc4_setkey(struct crypto_ablkcipher *cipher, const u8 *key,
			  unsigned int keylen)
{
	struct crypto_tfm *tfm = crypto_ablkcipher_tfm(cipher);
	struct n2_cipher_context *ctx = crypto_tfm_ctx(tfm);
	struct n2_cipher_alg *n2alg = n2_cipher_alg(tfm);
	u8 *s = ctx->key.arc4;
	u8 *x = s + 256;
	u8 *y = x + 1;
	int i, j, k;

	ctx->enc_type = n2alg->enc_type;

	j = k = 0;
	*x = 0;
	*y = 0;
	for (i = 0; i < 256; i++)
		s[i] = i;
	for (i = 0; i < 256; i++) {
		u8 a = s[i];
		j = (j + key[k] + a) & 0xff;
		s[i] = s[j];
		s[j] = a;
		if (++k >= keylen)
			k = 0;
	}

	return 0;
}

static inline int cipher_descriptor_len(int nbytes, unsigned int block_size)
{
	int this_len = nbytes;

	this_len -= (nbytes & (block_size - 1));
	return this_len > (1 << 16) ? (1 << 16) : this_len;
}

static int __n2_crypt_chunk(struct crypto_tfm *tfm, struct n2_crypto_chunk *cp,
			    struct spu_queue *qp, bool encrypt)
{
	struct n2_cipher_context *ctx = crypto_tfm_ctx(tfm);
	struct cwq_initial_entry *ent;
	bool in_place;
	int i;

	ent = spu_queue_alloc(qp, cp->arr_len);
	if (!ent) {
		pr_info("queue_alloc() of %d fails\n",
			cp->arr_len);
		return -EBUSY;
	}

	in_place = (cp->dest_paddr == cp->arr[0].src_paddr);

	ent->control = control_word_base(cp->arr[0].src_len,
					 0, ctx->enc_type, 0, 0,
					 false, true, false, encrypt,
					 OPCODE_ENCRYPT |
					 (in_place ? OPCODE_INPLACE_BIT : 0));
	ent->src_addr = cp->arr[0].src_paddr;
	ent->auth_key_addr = 0UL;
	ent->auth_iv_addr = 0UL;
	ent->final_auth_state_addr = 0UL;
	ent->enc_key_addr = __pa(&ctx->key);
	ent->enc_iv_addr = cp->iv_paddr;
	ent->dest_addr = (in_place ? 0UL : cp->dest_paddr);

	for (i = 1; i < cp->arr_len; i++) {
		ent = spu_queue_next(qp, ent);

		ent->control = cp->arr[i].src_len - 1;
		ent->src_addr = cp->arr[i].src_paddr;
		ent->auth_key_addr = 0UL;
		ent->auth_iv_addr = 0UL;
		ent->final_auth_state_addr = 0UL;
		ent->enc_key_addr = 0UL;
		ent->enc_iv_addr = 0UL;
		ent->dest_addr = 0UL;
	}
	ent->control |= CONTROL_END_OF_BLOCK;

	return (spu_queue_submit(qp, ent) != HV_EOK) ? -EINVAL : 0;
}

static int n2_compute_chunks(struct ablkcipher_request *req)
{
	struct n2_request_context *rctx = ablkcipher_request_ctx(req);
	struct ablkcipher_walk *walk = &rctx->walk;
	struct n2_crypto_chunk *chunk;
	unsigned long dest_prev;
	unsigned int tot_len;
	bool prev_in_place;
	int err, nbytes;

	ablkcipher_walk_init(walk, req->dst, req->src, req->nbytes);
	err = ablkcipher_walk_phys(req, walk);
	if (err)
		return err;

	INIT_LIST_HEAD(&rctx->chunk_list);

	chunk = &rctx->chunk;
	INIT_LIST_HEAD(&chunk->entry);

	chunk->iv_paddr = 0UL;
	chunk->arr_len = 0;
	chunk->dest_paddr = 0UL;

	prev_in_place = false;
	dest_prev = ~0UL;
	tot_len = 0;

	while ((nbytes = walk->nbytes) != 0) {
		unsigned long dest_paddr, src_paddr;
		bool in_place;
		int this_len;

		src_paddr = (page_to_phys(walk->src.page) +
			     walk->src.offset);
		dest_paddr = (page_to_phys(walk->dst.page) +
			      walk->dst.offset);
		in_place = (src_paddr == dest_paddr);
		this_len = cipher_descriptor_len(nbytes, walk->blocksize);

		if (chunk->arr_len != 0) {
			if (in_place != prev_in_place ||
			    (!prev_in_place &&
			     dest_paddr != dest_prev) ||
			    chunk->arr_len == N2_CHUNK_ARR_LEN ||
			    tot_len + this_len > (1 << 16)) {
				chunk->dest_final = dest_prev;
				list_add_tail(&chunk->entry,
					      &rctx->chunk_list);
				chunk = kzalloc(sizeof(*chunk), GFP_ATOMIC);
				if (!chunk) {
					err = -ENOMEM;
					break;
				}
				INIT_LIST_HEAD(&chunk->entry);
			}
		}
		if (chunk->arr_len == 0) {
			chunk->dest_paddr = dest_paddr;
			tot_len = 0;
		}
		chunk->arr[chunk->arr_len].src_paddr = src_paddr;
		chunk->arr[chunk->arr_len].src_len = this_len;
		chunk->arr_len++;

		dest_prev = dest_paddr + this_len;
		prev_in_place = in_place;
		tot_len += this_len;

		err = ablkcipher_walk_done(req, walk, nbytes - this_len);
		if (err)
			break;
	}
	if (!err && chunk->arr_len != 0) {
		chunk->dest_final = dest_prev;
		list_add_tail(&chunk->entry, &rctx->chunk_list);
	}

	return err;
}

static void n2_chunk_complete(struct ablkcipher_request *req, void *final_iv)
{
	struct n2_request_context *rctx = ablkcipher_request_ctx(req);
	struct n2_crypto_chunk *c, *tmp;

	if (final_iv)
		memcpy(rctx->walk.iv, final_iv, rctx->walk.blocksize);

	ablkcipher_walk_complete(&rctx->walk);
	list_for_each_entry_safe(c, tmp, &rctx->chunk_list, entry) {
		list_del(&c->entry);
		if (unlikely(c != &rctx->chunk))
			kfree(c);
	}

}

static int n2_do_ecb(struct ablkcipher_request *req, bool encrypt)
{
	struct n2_request_context *rctx = ablkcipher_request_ctx(req);
	struct crypto_tfm *tfm = req->base.tfm;
	int err = n2_compute_chunks(req);
	struct n2_crypto_chunk *c, *tmp;
	unsigned long flags, hv_ret;
	struct spu_queue *qp;

	if (err)
		return err;

	qp = cpu_to_cwq[get_cpu()];
	err = -ENODEV;
	if (!qp)
		goto out;

	spin_lock_irqsave(&qp->lock, flags);

	list_for_each_entry_safe(c, tmp, &rctx->chunk_list, entry) {
		err = __n2_crypt_chunk(tfm, c, qp, encrypt);
		if (err)
			break;
		list_del(&c->entry);
		if (unlikely(c != &rctx->chunk))
			kfree(c);
	}
	if (!err) {
		hv_ret = wait_for_tail(qp);
		if (hv_ret != HV_EOK)
			err = -EINVAL;
	}

	spin_unlock_irqrestore(&qp->lock, flags);

out:
	put_cpu();

	n2_chunk_complete(req, NULL);
	return err;
}

static int n2_encrypt_ecb(struct ablkcipher_request *req)
{
	return n2_do_ecb(req, true);
}

static int n2_decrypt_ecb(struct ablkcipher_request *req)
{
	return n2_do_ecb(req, false);
}

static int n2_do_chaining(struct ablkcipher_request *req, bool encrypt)
{
	struct n2_request_context *rctx = ablkcipher_request_ctx(req);
	struct crypto_tfm *tfm = req->base.tfm;
	unsigned long flags, hv_ret, iv_paddr;
	int err = n2_compute_chunks(req);
	struct n2_crypto_chunk *c, *tmp;
	struct spu_queue *qp;
	void *final_iv_addr;

	final_iv_addr = NULL;

	if (err)
		return err;

	qp = cpu_to_cwq[get_cpu()];
	err = -ENODEV;
	if (!qp)
		goto out;

	spin_lock_irqsave(&qp->lock, flags);

	if (encrypt) {
		iv_paddr = __pa(rctx->walk.iv);
		list_for_each_entry_safe(c, tmp, &rctx->chunk_list,
					 entry) {
			c->iv_paddr = iv_paddr;
			err = __n2_crypt_chunk(tfm, c, qp, true);
			if (err)
				break;
			iv_paddr = c->dest_final - rctx->walk.blocksize;
			list_del(&c->entry);
			if (unlikely(c != &rctx->chunk))
				kfree(c);
		}
		final_iv_addr = __va(iv_paddr);
	} else {
		list_for_each_entry_safe_reverse(c, tmp, &rctx->chunk_list,
						 entry) {
			if (c == &rctx->chunk) {
				iv_paddr = __pa(rctx->walk.iv);
			} else {
				iv_paddr = (tmp->arr[tmp->arr_len-1].src_paddr +
					    tmp->arr[tmp->arr_len-1].src_len -
					    rctx->walk.blocksize);
			}
			if (!final_iv_addr) {
				unsigned long pa;

				pa = (c->arr[c->arr_len-1].src_paddr +
				      c->arr[c->arr_len-1].src_len -
				      rctx->walk.blocksize);
				final_iv_addr = rctx->temp_iv;
				memcpy(rctx->temp_iv, __va(pa),
				       rctx->walk.blocksize);
			}
			c->iv_paddr = iv_paddr;
			err = __n2_crypt_chunk(tfm, c, qp, false);
			if (err)
				break;
			list_del(&c->entry);
			if (unlikely(c != &rctx->chunk))
				kfree(c);
		}
	}
	if (!err) {
		hv_ret = wait_for_tail(qp);
		if (hv_ret != HV_EOK)
			err = -EINVAL;
	}

	spin_unlock_irqrestore(&qp->lock, flags);

out:
	put_cpu();

	n2_chunk_complete(req, err ? NULL : final_iv_addr);
	return err;
}

static int n2_encrypt_chaining(struct ablkcipher_request *req)
{
	return n2_do_chaining(req, true);
}

static int n2_decrypt_chaining(struct ablkcipher_request *req)
{
	return n2_do_chaining(req, false);
}

struct n2_cipher_tmpl {
	const char		*name;
	const char		*drv_name;
	u8			block_size;
	u8			enc_type;
	struct ablkcipher_alg	ablkcipher;
};

static const struct n2_cipher_tmpl cipher_tmpls[] = {
	/* ARC4: only ECB is supported (chaining bits ignored) */
	{	.name		= "ecb(arc4)",
		.drv_name	= "ecb-arc4",
		.block_size	= 1,
		.enc_type	= (ENC_TYPE_ALG_RC4_STREAM |
				   ENC_TYPE_CHAINING_ECB),
		.ablkcipher	= {
			.min_keysize	= 1,
			.max_keysize	= 256,
			.setkey		= n2_arc4_setkey,
			.encrypt	= n2_encrypt_ecb,
			.decrypt	= n2_decrypt_ecb,
		},
	},

	/* DES: ECB CBC and CFB are supported */
	{	.name		= "ecb(des)",
		.drv_name	= "ecb-des",
		.block_size	= DES_BLOCK_SIZE,
		.enc_type	= (ENC_TYPE_ALG_DES |
				   ENC_TYPE_CHAINING_ECB),
		.ablkcipher	= {
			.min_keysize	= DES_KEY_SIZE,
			.max_keysize	= DES_KEY_SIZE,
			.setkey		= n2_des_setkey,
			.encrypt	= n2_encrypt_ecb,
			.decrypt	= n2_decrypt_ecb,
		},
	},
	{	.name		= "cbc(des)",
		.drv_name	= "cbc-des",
		.block_size	= DES_BLOCK_SIZE,
		.enc_type	= (ENC_TYPE_ALG_DES |
				   ENC_TYPE_CHAINING_CBC),
		.ablkcipher	= {
			.ivsize		= DES_BLOCK_SIZE,
			.min_keysize	= DES_KEY_SIZE,
			.max_keysize	= DES_KEY_SIZE,
			.setkey		= n2_des_setkey,
			.encrypt	= n2_encrypt_chaining,
			.decrypt	= n2_decrypt_chaining,
		},
	},
	{	.name		= "cfb(des)",
		.drv_name	= "cfb-des",
		.block_size	= DES_BLOCK_SIZE,
		.enc_type	= (ENC_TYPE_ALG_DES |
				   ENC_TYPE_CHAINING_CFB),
		.ablkcipher	= {
			.min_keysize	= DES_KEY_SIZE,
			.max_keysize	= DES_KEY_SIZE,
			.setkey		= n2_des_setkey,
			.encrypt	= n2_encrypt_chaining,
			.decrypt	= n2_decrypt_chaining,
		},
	},

	/* 3DES: ECB CBC and CFB are supported */
	{	.name		= "ecb(des3_ede)",
		.drv_name	= "ecb-3des",
		.block_size	= DES_BLOCK_SIZE,
		.enc_type	= (ENC_TYPE_ALG_3DES |
				   ENC_TYPE_CHAINING_ECB),
		.ablkcipher	= {
			.min_keysize	= 3 * DES_KEY_SIZE,
			.max_keysize	= 3 * DES_KEY_SIZE,
			.setkey		= n2_3des_setkey,
			.encrypt	= n2_encrypt_ecb,
			.decrypt	= n2_decrypt_ecb,
		},
	},
	{	.name		= "cbc(des3_ede)",
		.drv_name	= "cbc-3des",
		.block_size	= DES_BLOCK_SIZE,
		.enc_type	= (ENC_TYPE_ALG_3DES |
				   ENC_TYPE_CHAINING_CBC),
		.ablkcipher	= {
			.ivsize		= DES_BLOCK_SIZE,
			.min_keysize	= 3 * DES_KEY_SIZE,
			.max_keysize	= 3 * DES_KEY_SIZE,
			.setkey		= n2_3des_setkey,
			.encrypt	= n2_encrypt_chaining,
			.decrypt	= n2_decrypt_chaining,
		},
	},
	{	.name		= "cfb(des3_ede)",
		.drv_name	= "cfb-3des",
		.block_size	= DES_BLOCK_SIZE,
		.enc_type	= (ENC_TYPE_ALG_3DES |
				   ENC_TYPE_CHAINING_CFB),
		.ablkcipher	= {
			.min_keysize	= 3 * DES_KEY_SIZE,
			.max_keysize	= 3 * DES_KEY_SIZE,
			.setkey		= n2_3des_setkey,
			.encrypt	= n2_encrypt_chaining,
			.decrypt	= n2_decrypt_chaining,
		},
	},
	/* AES: ECB CBC and CTR are supported */
	{	.name		= "ecb(aes)",
		.drv_name	= "ecb-aes",
		.block_size	= AES_BLOCK_SIZE,
		.enc_type	= (ENC_TYPE_ALG_AES128 |
				   ENC_TYPE_CHAINING_ECB),
		.ablkcipher	= {
			.min_keysize	= AES_MIN_KEY_SIZE,
			.max_keysize	= AES_MAX_KEY_SIZE,
			.setkey		= n2_aes_setkey,
			.encrypt	= n2_encrypt_ecb,
			.decrypt	= n2_decrypt_ecb,
		},
	},
	{	.name		= "cbc(aes)",
		.drv_name	= "cbc-aes",
		.block_size	= AES_BLOCK_SIZE,
		.enc_type	= (ENC_TYPE_ALG_AES128 |
				   ENC_TYPE_CHAINING_CBC),
		.ablkcipher	= {
			.ivsize		= AES_BLOCK_SIZE,
			.min_keysize	= AES_MIN_KEY_SIZE,
			.max_keysize	= AES_MAX_KEY_SIZE,
			.setkey		= n2_aes_setkey,
			.encrypt	= n2_encrypt_chaining,
			.decrypt	= n2_decrypt_chaining,
		},
	},
	{	.name		= "ctr(aes)",
		.drv_name	= "ctr-aes",
		.block_size	= AES_BLOCK_SIZE,
		.enc_type	= (ENC_TYPE_ALG_AES128 |
				   ENC_TYPE_CHAINING_COUNTER),
		.ablkcipher	= {
			.ivsize		= AES_BLOCK_SIZE,
			.min_keysize	= AES_MIN_KEY_SIZE,
			.max_keysize	= AES_MAX_KEY_SIZE,
			.setkey		= n2_aes_setkey,
			.encrypt	= n2_encrypt_chaining,
			.decrypt	= n2_encrypt_chaining,
		},
	},

};
#define NUM_CIPHER_TMPLS ARRAY_SIZE(cipher_tmpls)

static LIST_HEAD(cipher_algs);

struct n2_hash_tmpl {
	const char	*name;
	const u8	*hash_zero;
	const u32	*hash_init;
	u8		hw_op_hashsz;
	u8		digest_size;
	u8		block_size;
	u8		auth_type;
	u8		hmac_type;
};

static const u32 md5_init[MD5_HASH_WORDS] = {
	cpu_to_le32(MD5_H0),
	cpu_to_le32(MD5_H1),
	cpu_to_le32(MD5_H2),
	cpu_to_le32(MD5_H3),
};
static const u32 sha1_init[SHA1_DIGEST_SIZE / 4] = {
	SHA1_H0, SHA1_H1, SHA1_H2, SHA1_H3, SHA1_H4,
};
static const u32 sha256_init[SHA256_DIGEST_SIZE / 4] = {
	SHA256_H0, SHA256_H1, SHA256_H2, SHA256_H3,
	SHA256_H4, SHA256_H5, SHA256_H6, SHA256_H7,
};
static const u32 sha224_init[SHA256_DIGEST_SIZE / 4] = {
	SHA224_H0, SHA224_H1, SHA224_H2, SHA224_H3,
	SHA224_H4, SHA224_H5, SHA224_H6, SHA224_H7,
};

static const struct n2_hash_tmpl hash_tmpls[] = {
	{ .name		= "md5",
	  .hash_zero	= md5_zero_message_hash,
	  .hash_init	= md5_init,
	  .auth_type	= AUTH_TYPE_MD5,
	  .hmac_type	= AUTH_TYPE_HMAC_MD5,
	  .hw_op_hashsz	= MD5_DIGEST_SIZE,
	  .digest_size	= MD5_DIGEST_SIZE,
	  .block_size	= MD5_HMAC_BLOCK_SIZE },
	{ .name		= "sha1",
	  .hash_zero	= sha1_zero_message_hash,
	  .hash_init	= sha1_init,
	  .auth_type	= AUTH_TYPE_SHA1,
	  .hmac_type	= AUTH_TYPE_HMAC_SHA1,
	  .hw_op_hashsz	= SHA1_DIGEST_SIZE,
	  .digest_size	= SHA1_DIGEST_SIZE,
	  .block_size	= SHA1_BLOCK_SIZE },
	{ .name		= "sha256",
	  .hash_zero	= sha256_zero_message_hash,
	  .hash_init	= sha256_init,
	  .auth_type	= AUTH_TYPE_SHA256,
	  .hmac_type	= AUTH_TYPE_HMAC_SHA256,
	  .hw_op_hashsz	= SHA256_DIGEST_SIZE,
	  .digest_size	= SHA256_DIGEST_SIZE,
	  .block_size	= SHA256_BLOCK_SIZE },
	{ .name		= "sha224",
	  .hash_zero	= sha224_zero_message_hash,
	  .hash_init	= sha224_init,
	  .auth_type	= AUTH_TYPE_SHA256,
	  .hmac_type	= AUTH_TYPE_RESERVED,
	  .hw_op_hashsz	= SHA256_DIGEST_SIZE,
	  .digest_size	= SHA224_DIGEST_SIZE,
	  .block_size	= SHA224_BLOCK_SIZE },
};
#define NUM_HASH_TMPLS ARRAY_SIZE(hash_tmpls)

static LIST_HEAD(ahash_algs);
static LIST_HEAD(hmac_algs);

static int algs_registered;

static void __n2_unregister_algs(void)
{
	struct n2_cipher_alg *cipher, *cipher_tmp;
	struct n2_ahash_alg *alg, *alg_tmp;
	struct n2_hmac_alg *hmac, *hmac_tmp;

	list_for_each_entry_safe(cipher, cipher_tmp, &cipher_algs, entry) {
		crypto_unregister_alg(&cipher->alg);
		list_del(&cipher->entry);
		kfree(cipher);
	}
	list_for_each_entry_safe(hmac, hmac_tmp, &hmac_algs, derived.entry) {
		crypto_unregister_ahash(&hmac->derived.alg);
		list_del(&hmac->derived.entry);
		kfree(hmac);
	}
	list_for_each_entry_safe(alg, alg_tmp, &ahash_algs, entry) {
		crypto_unregister_ahash(&alg->alg);
		list_del(&alg->entry);
		kfree(alg);
	}
}

static int n2_cipher_cra_init(struct crypto_tfm *tfm)
{
	tfm->crt_ablkcipher.reqsize = sizeof(struct n2_request_context);
	return 0;
}

static int __n2_register_one_cipher(const struct n2_cipher_tmpl *tmpl)
{
	struct n2_cipher_alg *p = kzalloc(sizeof(*p), GFP_KERNEL);
	struct crypto_alg *alg;
	int err;

	if (!p)
		return -ENOMEM;

	alg = &p->alg;

	snprintf(alg->cra_name, CRYPTO_MAX_ALG_NAME, "%s", tmpl->name);
	snprintf(alg->cra_driver_name, CRYPTO_MAX_ALG_NAME, "%s-n2", tmpl->drv_name);
	alg->cra_priority = N2_CRA_PRIORITY;
	alg->cra_flags = CRYPTO_ALG_TYPE_ABLKCIPHER |
			 CRYPTO_ALG_KERN_DRIVER_ONLY | CRYPTO_ALG_ASYNC;
	alg->cra_blocksize = tmpl->block_size;
	p->enc_type = tmpl->enc_type;
	alg->cra_ctxsize = sizeof(struct n2_cipher_context);
	alg->cra_type = &crypto_ablkcipher_type;
	alg->cra_u.ablkcipher = tmpl->ablkcipher;
	alg->cra_init = n2_cipher_cra_init;
	alg->cra_module = THIS_MODULE;

	list_add(&p->entry, &cipher_algs);
	err = crypto_register_alg(alg);
	if (err) {
		pr_err("%s alg registration failed\n", alg->cra_name);
		list_del(&p->entry);
		kfree(p);
	} else {
		pr_info("%s alg registered\n", alg->cra_name);
	}
	return err;
}

static int __n2_register_one_hmac(struct n2_ahash_alg *n2ahash)
{
	struct n2_hmac_alg *p = kzalloc(sizeof(*p), GFP_KERNEL);
	struct ahash_alg *ahash;
	struct crypto_alg *base;
	int err;

	if (!p)
		return -ENOMEM;

	p->child_alg = n2ahash->alg.halg.base.cra_name;
	memcpy(&p->derived, n2ahash, sizeof(struct n2_ahash_alg));
	INIT_LIST_HEAD(&p->derived.entry);

	ahash = &p->derived.alg;
	ahash->digest = n2_hmac_async_digest;
	ahash->setkey = n2_hmac_async_setkey;

	base = &ahash->halg.base;
	snprintf(base->cra_name, CRYPTO_MAX_ALG_NAME, "hmac(%s)", p->child_alg);
	snprintf(base->cra_driver_name, CRYPTO_MAX_ALG_NAME, "hmac-%s-n2", p->child_alg);

	base->cra_ctxsize = sizeof(struct n2_hmac_ctx);
	base->cra_init = n2_hmac_cra_init;
	base->cra_exit = n2_hmac_cra_exit;

	list_add(&p->derived.entry, &hmac_algs);
	err = crypto_register_ahash(ahash);
	if (err) {
		pr_err("%s alg registration failed\n", base->cra_name);
		list_del(&p->derived.entry);
		kfree(p);
	} else {
		pr_info("%s alg registered\n", base->cra_name);
	}
	return err;
}

static int __n2_register_one_ahash(const struct n2_hash_tmpl *tmpl)
{
	struct n2_ahash_alg *p = kzalloc(sizeof(*p), GFP_KERNEL);
	struct hash_alg_common *halg;
	struct crypto_alg *base;
	struct ahash_alg *ahash;
	int err;

	if (!p)
		return -ENOMEM;

	p->hash_zero = tmpl->hash_zero;
	p->hash_init = tmpl->hash_init;
	p->auth_type = tmpl->auth_type;
	p->hmac_type = tmpl->hmac_type;
	p->hw_op_hashsz = tmpl->hw_op_hashsz;
	p->digest_size = tmpl->digest_size;

	ahash = &p->alg;
	ahash->init = n2_hash_async_init;
	ahash->update = n2_hash_async_update;
	ahash->final = n2_hash_async_final;
	ahash->finup = n2_hash_async_finup;
	ahash->digest = n2_hash_async_digest;

	halg = &ahash->halg;
	halg->digestsize = tmpl->digest_size;

	base = &halg->base;
	snprintf(base->cra_name, CRYPTO_MAX_ALG_NAME, "%s", tmpl->name);
	snprintf(base->cra_driver_name, CRYPTO_MAX_ALG_NAME, "%s-n2", tmpl->name);
	base->cra_priority = N2_CRA_PRIORITY;
	base->cra_flags = CRYPTO_ALG_TYPE_AHASH |
			  CRYPTO_ALG_KERN_DRIVER_ONLY |
			  CRYPTO_ALG_NEED_FALLBACK;
	base->cra_blocksize = tmpl->block_size;
	base->cra_ctxsize = sizeof(struct n2_hash_ctx);
	base->cra_module = THIS_MODULE;
	base->cra_init = n2_hash_cra_init;
	base->cra_exit = n2_hash_cra_exit;

	list_add(&p->entry, &ahash_algs);
	err = crypto_register_ahash(ahash);
	if (err) {
		pr_err("%s alg registration failed\n", base->cra_name);
		list_del(&p->entry);
		kfree(p);
	} else {
		pr_info("%s alg registered\n", base->cra_name);
	}
	if (!err && p->hmac_type != AUTH_TYPE_RESERVED)
		err = __n2_register_one_hmac(p);
	return err;
}

static int n2_register_algs(void)
{
	int i, err = 0;

	mutex_lock(&spu_lock);
	if (algs_registered++)
		goto out;

	for (i = 0; i < NUM_HASH_TMPLS; i++) {
		err = __n2_register_one_ahash(&hash_tmpls[i]);
		if (err) {
			__n2_unregister_algs();
			goto out;
		}
	}
	for (i = 0; i < NUM_CIPHER_TMPLS; i++) {
		err = __n2_register_one_cipher(&cipher_tmpls[i]);
		if (err) {
			__n2_unregister_algs();
			goto out;
		}
	}

out:
	mutex_unlock(&spu_lock);
	return err;
}

static void n2_unregister_algs(void)
{
	mutex_lock(&spu_lock);
	if (!--algs_registered)
		__n2_unregister_algs();
	mutex_unlock(&spu_lock);
}

/* To map CWQ queues to interrupt sources, the hypervisor API provides
 * a devino.  This isn't very useful to us because all of the
 * interrupts listed in the device_node have been translated to
 * Linux virtual IRQ cookie numbers.
 *
 * So we have to back-translate, going through the 'intr' and 'ino'
 * property tables of the n2cp MDESC node, matching it with the OF
 * 'interrupts' property entries, in order to to figure out which
 * devino goes to which already-translated IRQ.
 */
static int find_devino_index(struct platform_device *dev, struct spu_mdesc_info *ip,
			     unsigned long dev_ino)
{
	const unsigned int *dev_intrs;
	unsigned int intr;
	int i;

	for (i = 0; i < ip->num_intrs; i++) {
		if (ip->ino_table[i].ino == dev_ino)
			break;
	}
	if (i == ip->num_intrs)
		return -ENODEV;

	intr = ip->ino_table[i].intr;

	dev_intrs = of_get_property(dev->dev.of_node, "interrupts", NULL);
	if (!dev_intrs)
		return -ENODEV;

	for (i = 0; i < dev->archdata.num_irqs; i++) {
		if (dev_intrs[i] == intr)
			return i;
	}

	return -ENODEV;
}

static int spu_map_ino(struct platform_device *dev, struct spu_mdesc_info *ip,
		       const char *irq_name, struct spu_queue *p,
		       irq_handler_t handler)
{
	unsigned long herr;
	int index;

	herr = sun4v_ncs_qhandle_to_devino(p->qhandle, &p->devino);
	if (herr)
		return -EINVAL;

	index = find_devino_index(dev, ip, p->devino);
	if (index < 0)
		return index;

	p->irq = dev->archdata.irqs[index];

	sprintf(p->irq_name, "%s-%d", irq_name, index);

	return request_irq(p->irq, handler, 0, p->irq_name, p);
}

static struct kmem_cache *queue_cache[2];

static void *new_queue(unsigned long q_type)
{
	return kmem_cache_zalloc(queue_cache[q_type - 1], GFP_KERNEL);
}

static void free_queue(void *p, unsigned long q_type)
{
	kmem_cache_free(queue_cache[q_type - 1], p);
}

static int queue_cache_init(void)
{
	if (!queue_cache[HV_NCS_QTYPE_MAU - 1])
		queue_cache[HV_NCS_QTYPE_MAU - 1] =
			kmem_cache_create("mau_queue",
					  (MAU_NUM_ENTRIES *
					   MAU_ENTRY_SIZE),
					  MAU_ENTRY_SIZE, 0, NULL);
	if (!queue_cache[HV_NCS_QTYPE_MAU - 1])
		return -ENOMEM;

	if (!queue_cache[HV_NCS_QTYPE_CWQ - 1])
		queue_cache[HV_NCS_QTYPE_CWQ - 1] =
			kmem_cache_create("cwq_queue",
					  (CWQ_NUM_ENTRIES *
					   CWQ_ENTRY_SIZE),
					  CWQ_ENTRY_SIZE, 0, NULL);
	if (!queue_cache[HV_NCS_QTYPE_CWQ - 1]) {
		kmem_cache_destroy(queue_cache[HV_NCS_QTYPE_MAU - 1]);
		queue_cache[HV_NCS_QTYPE_MAU - 1] = NULL;
		return -ENOMEM;
	}
	return 0;
}

static void queue_cache_destroy(void)
{
	kmem_cache_destroy(queue_cache[HV_NCS_QTYPE_MAU - 1]);
	kmem_cache_destroy(queue_cache[HV_NCS_QTYPE_CWQ - 1]);
	queue_cache[HV_NCS_QTYPE_MAU - 1] = NULL;
	queue_cache[HV_NCS_QTYPE_CWQ - 1] = NULL;
}

static long spu_queue_register_workfn(void *arg)
{
	struct spu_qreg *qr = arg;
	struct spu_queue *p = qr->queue;
	unsigned long q_type = qr->type;
	unsigned long hv_ret;

	hv_ret = sun4v_ncs_qconf(q_type, __pa(p->q),
				 CWQ_NUM_ENTRIES, &p->qhandle);
	if (!hv_ret)
		sun4v_ncs_sethead_marker(p->qhandle, 0);

	return hv_ret ? -EINVAL : 0;
}

static int spu_queue_register(struct spu_queue *p, unsigned long q_type)
{
	int cpu = cpumask_any_and(&p->sharing, cpu_online_mask);
	struct spu_qreg qr = { .queue = p, .type = q_type };

	return work_on_cpu_safe(cpu, spu_queue_register_workfn, &qr);
}

static int spu_queue_setup(struct spu_queue *p)
{
	int err;

	p->q = new_queue(p->q_type);
	if (!p->q)
		return -ENOMEM;

	err = spu_queue_register(p, p->q_type);
	if (err) {
		free_queue(p->q, p->q_type);
		p->q = NULL;
	}

	return err;
}

static void spu_queue_destroy(struct spu_queue *p)
{
	unsigned long hv_ret;

	if (!p->q)
		return;

	hv_ret = sun4v_ncs_qconf(p->q_type, p->qhandle, 0, &p->qhandle);

	if (!hv_ret)
		free_queue(p->q, p->q_type);
}

static void spu_list_destroy(struct list_head *list)
{
	struct spu_queue *p, *n;

	list_for_each_entry_safe(p, n, list, list) {
		int i;

		for (i = 0; i < NR_CPUS; i++) {
			if (cpu_to_cwq[i] == p)
				cpu_to_cwq[i] = NULL;
		}

		if (p->irq) {
			free_irq(p->irq, p);
			p->irq = 0;
		}
		spu_queue_destroy(p);
		list_del(&p->list);
		kfree(p);
	}
}

/* Walk the backward arcs of a CWQ 'exec-unit' node,
 * gathering cpu membership information.
 */
static int spu_mdesc_walk_arcs(struct mdesc_handle *mdesc,
			       struct platform_device *dev,
			       u64 node, struct spu_queue *p,
			       struct spu_queue **table)
{
	u64 arc;

	mdesc_for_each_arc(arc, mdesc, node, MDESC_ARC_TYPE_BACK) {
		u64 tgt = mdesc_arc_target(mdesc, arc);
		const char *name = mdesc_node_name(mdesc, tgt);
		const u64 *id;

		if (strcmp(name, "cpu"))
			continue;
		id = mdesc_get_property(mdesc, tgt, "id", NULL);
		if (table[*id] != NULL) {
			dev_err(&dev->dev, "%pOF: SPU cpu slot already set.\n",
				dev->dev.of_node);
			return -EINVAL;
		}
		cpumask_set_cpu(*id, &p->sharing);
		table[*id] = p;
	}
	return 0;
}

/* Process an 'exec-unit' MDESC node of type 'cwq'.  */
static int handle_exec_unit(struct spu_mdesc_info *ip, struct list_head *list,
			    struct platform_device *dev, struct mdesc_handle *mdesc,
			    u64 node, const char *iname, unsigned long q_type,
			    irq_handler_t handler, struct spu_queue **table)
{
	struct spu_queue *p;
	int err;

	p = kzalloc(sizeof(struct spu_queue), GFP_KERNEL);
	if (!p) {
		dev_err(&dev->dev, "%pOF: Could not allocate SPU queue.\n",
			dev->dev.of_node);
		return -ENOMEM;
	}

	cpumask_clear(&p->sharing);
	spin_lock_init(&p->lock);
	p->q_type = q_type;
	INIT_LIST_HEAD(&p->jobs);
	list_add(&p->list, list);

	err = spu_mdesc_walk_arcs(mdesc, dev, node, p, table);
	if (err)
		return err;

	err = spu_queue_setup(p);
	if (err)
		return err;

	return spu_map_ino(dev, ip, iname, p, handler);
}

static int spu_mdesc_scan(struct mdesc_handle *mdesc, struct platform_device *dev,
			  struct spu_mdesc_info *ip, struct list_head *list,
			  const char *exec_name, unsigned long q_type,
			  irq_handler_t handler, struct spu_queue **table)
{
	int err = 0;
	u64 node;

	mdesc_for_each_node_by_name(mdesc, node, "exec-unit") {
		const char *type;

		type = mdesc_get_property(mdesc, node, "type", NULL);
		if (!type || strcmp(type, exec_name))
			continue;

		err = handle_exec_unit(ip, list, dev, mdesc, node,
				       exec_name, q_type, handler, table);
		if (err) {
			spu_list_destroy(list);
			break;
		}
	}

	return err;
}

static int get_irq_props(struct mdesc_handle *mdesc, u64 node,
			 struct spu_mdesc_info *ip)
{
	const u64 *ino;
	int ino_len;
	int i;

	ino = mdesc_get_property(mdesc, node, "ino", &ino_len);
	if (!ino) {
		printk("NO 'ino'\n");
		return -ENODEV;
	}

	ip->num_intrs = ino_len / sizeof(u64);
	ip->ino_table = kzalloc((sizeof(struct ino_blob) *
				 ip->num_intrs),
				GFP_KERNEL);
	if (!ip->ino_table)
		return -ENOMEM;

	for (i = 0; i < ip->num_intrs; i++) {
		struct ino_blob *b = &ip->ino_table[i];
		b->intr = i + 1;
		b->ino = ino[i];
	}

	return 0;
}

static int grab_mdesc_irq_props(struct mdesc_handle *mdesc,
				struct platform_device *dev,
				struct spu_mdesc_info *ip,
				const char *node_name)
{
	const unsigned int *reg;
	u64 node;

	reg = of_get_property(dev->dev.of_node, "reg", NULL);
	if (!reg)
		return -ENODEV;

	mdesc_for_each_node_by_name(mdesc, node, "virtual-device") {
		const char *name;
		const u64 *chdl;

		name = mdesc_get_property(mdesc, node, "name", NULL);
		if (!name || strcmp(name, node_name))
			continue;
		chdl = mdesc_get_property(mdesc, node, "cfg-handle", NULL);
		if (!chdl || (*chdl != *reg))
			continue;
		ip->cfg_handle = *chdl;
		return get_irq_props(mdesc, node, ip);
	}

	return -ENODEV;
}

static unsigned long n2_spu_hvapi_major;
static unsigned long n2_spu_hvapi_minor;

static int n2_spu_hvapi_register(void)
{
	int err;

	n2_spu_hvapi_major = 2;
	n2_spu_hvapi_minor = 0;

	err = sun4v_hvapi_register(HV_GRP_NCS,
				   n2_spu_hvapi_major,
				   &n2_spu_hvapi_minor);

	if (!err)
		pr_info("Registered NCS HVAPI version %lu.%lu\n",
			n2_spu_hvapi_major,
			n2_spu_hvapi_minor);

	return err;
}

static void n2_spu_hvapi_unregister(void)
{
	sun4v_hvapi_unregister(HV_GRP_NCS);
}

static int global_ref;

static int grab_global_resources(void)
{
	int err = 0;

	mutex_lock(&spu_lock);

	if (global_ref++)
		goto out;

	err = n2_spu_hvapi_register();
	if (err)
		goto out;

	err = queue_cache_init();
	if (err)
		goto out_hvapi_release;

	err = -ENOMEM;
	cpu_to_cwq = kzalloc(sizeof(struct spu_queue *) * NR_CPUS,
			     GFP_KERNEL);
	if (!cpu_to_cwq)
		goto out_queue_cache_destroy;

	cpu_to_mau = kzalloc(sizeof(struct spu_queue *) * NR_CPUS,
			     GFP_KERNEL);
	if (!cpu_to_mau)
		goto out_free_cwq_table;

	err = 0;

out:
	if (err)
		global_ref--;
	mutex_unlock(&spu_lock);
	return err;

out_free_cwq_table:
	kfree(cpu_to_cwq);
	cpu_to_cwq = NULL;

out_queue_cache_destroy:
	queue_cache_destroy();

out_hvapi_release:
	n2_spu_hvapi_unregister();
	goto out;
}

static void release_global_resources(void)
{
	mutex_lock(&spu_lock);
	if (!--global_ref) {
		kfree(cpu_to_cwq);
		cpu_to_cwq = NULL;

		kfree(cpu_to_mau);
		cpu_to_mau = NULL;

		queue_cache_destroy();
		n2_spu_hvapi_unregister();
	}
	mutex_unlock(&spu_lock);
}

static struct n2_crypto *alloc_n2cp(void)
{
	struct n2_crypto *np = kzalloc(sizeof(struct n2_crypto), GFP_KERNEL);

	if (np)
		INIT_LIST_HEAD(&np->cwq_list);

	return np;
}

static void free_n2cp(struct n2_crypto *np)
{
	kfree(np->cwq_info.ino_table);
	np->cwq_info.ino_table = NULL;

	kfree(np);
}

static void n2_spu_driver_version(void)
{
	static int n2_spu_version_printed;

	if (n2_spu_version_printed++ == 0)
		pr_info("%s", version);
}

static int n2_crypto_probe(struct platform_device *dev)
{
	struct mdesc_handle *mdesc;
	struct n2_crypto *np;
	int err;

	n2_spu_driver_version();

	pr_info("Found N2CP at %pOF\n", dev->dev.of_node);

	np = alloc_n2cp();
	if (!np) {
		dev_err(&dev->dev, "%pOF: Unable to allocate n2cp.\n",
			dev->dev.of_node);
		return -ENOMEM;
	}

	err = grab_global_resources();
	if (err) {
		dev_err(&dev->dev, "%pOF: Unable to grab global resources.\n",
			dev->dev.of_node);
		goto out_free_n2cp;
	}

	mdesc = mdesc_grab();

	if (!mdesc) {
		dev_err(&dev->dev, "%pOF: Unable to grab MDESC.\n",
			dev->dev.of_node);
		err = -ENODEV;
		goto out_free_global;
	}
	err = grab_mdesc_irq_props(mdesc, dev, &np->cwq_info, "n2cp");
	if (err) {
		dev_err(&dev->dev, "%pOF: Unable to grab IRQ props.\n",
			dev->dev.of_node);
		mdesc_release(mdesc);
		goto out_free_global;
	}

	err = spu_mdesc_scan(mdesc, dev, &np->cwq_info, &np->cwq_list,
			     "cwq", HV_NCS_QTYPE_CWQ, cwq_intr,
			     cpu_to_cwq);
	mdesc_release(mdesc);

	if (err) {
		dev_err(&dev->dev, "%pOF: CWQ MDESC scan failed.\n",
			dev->dev.of_node);
		goto out_free_global;
	}

	err = n2_register_algs();
	if (err) {
		dev_err(&dev->dev, "%pOF: Unable to register algorithms.\n",
			dev->dev.of_node);
		goto out_free_spu_list;
	}

	dev_set_drvdata(&dev->dev, np);

	return 0;

out_free_spu_list:
	spu_list_destroy(&np->cwq_list);

out_free_global:
	release_global_resources();

out_free_n2cp:
	free_n2cp(np);

	return err;
}

static int n2_crypto_remove(struct platform_device *dev)
{
	struct n2_crypto *np = dev_get_drvdata(&dev->dev);

	n2_unregister_algs();

	spu_list_destroy(&np->cwq_list);

	release_global_resources();

	free_n2cp(np);

	return 0;
}

static struct n2_mau *alloc_ncp(void)
{
	struct n2_mau *mp = kzalloc(sizeof(struct n2_mau), GFP_KERNEL);

	if (mp)
		INIT_LIST_HEAD(&mp->mau_list);

	return mp;
}

static void free_ncp(struct n2_mau *mp)
{
	kfree(mp->mau_info.ino_table);
	mp->mau_info.ino_table = NULL;

	kfree(mp);
}

static int n2_mau_probe(struct platform_device *dev)
{
	struct mdesc_handle *mdesc;
	struct n2_mau *mp;
	int err;

	n2_spu_driver_version();

	pr_info("Found NCP at %pOF\n", dev->dev.of_node);

	mp = alloc_ncp();
	if (!mp) {
		dev_err(&dev->dev, "%pOF: Unable to allocate ncp.\n",
			dev->dev.of_node);
		return -ENOMEM;
	}

	err = grab_global_resources();
	if (err) {
		dev_err(&dev->dev, "%pOF: Unable to grab global resources.\n",
			dev->dev.of_node);
		goto out_free_ncp;
	}

	mdesc = mdesc_grab();

	if (!mdesc) {
		dev_err(&dev->dev, "%pOF: Unable to grab MDESC.\n",
			dev->dev.of_node);
		err = -ENODEV;
		goto out_free_global;
	}

	err = grab_mdesc_irq_props(mdesc, dev, &mp->mau_info, "ncp");
	if (err) {
		dev_err(&dev->dev, "%pOF: Unable to grab IRQ props.\n",
			dev->dev.of_node);
		mdesc_release(mdesc);
		goto out_free_global;
	}

	err = spu_mdesc_scan(mdesc, dev, &mp->mau_info, &mp->mau_list,
			     "mau", HV_NCS_QTYPE_MAU, mau_intr,
			     cpu_to_mau);
	mdesc_release(mdesc);

	if (err) {
		dev_err(&dev->dev, "%pOF: MAU MDESC scan failed.\n",
			dev->dev.of_node);
		goto out_free_global;
	}

	dev_set_drvdata(&dev->dev, mp);

	return 0;

out_free_global:
	release_global_resources();

out_free_ncp:
	free_ncp(mp);

	return err;
}

static int n2_mau_remove(struct platform_device *dev)
{
	struct n2_mau *mp = dev_get_drvdata(&dev->dev);

	spu_list_destroy(&mp->mau_list);

	release_global_resources();

	free_ncp(mp);

	return 0;
}

static const struct of_device_id n2_crypto_match[] = {
	{
		.name = "n2cp",
		.compatible = "SUNW,n2-cwq",
	},
	{
		.name = "n2cp",
		.compatible = "SUNW,vf-cwq",
	},
	{
		.name = "n2cp",
		.compatible = "SUNW,kt-cwq",
	},
	{},
};

MODULE_DEVICE_TABLE(of, n2_crypto_match);

static struct platform_driver n2_crypto_driver = {
	.driver = {
		.name		=	"n2cp",
		.of_match_table	=	n2_crypto_match,
	},
	.probe		=	n2_crypto_probe,
	.remove		=	n2_crypto_remove,
};

static const struct of_device_id n2_mau_match[] = {
	{
		.name = "ncp",
		.compatible = "SUNW,n2-mau",
	},
	{
		.name = "ncp",
		.compatible = "SUNW,vf-mau",
	},
	{
		.name = "ncp",
		.compatible = "SUNW,kt-mau",
	},
	{},
};

MODULE_DEVICE_TABLE(of, n2_mau_match);

static struct platform_driver n2_mau_driver = {
	.driver = {
		.name		=	"ncp",
		.of_match_table	=	n2_mau_match,
	},
	.probe		=	n2_mau_probe,
	.remove		=	n2_mau_remove,
};

static struct platform_driver * const drivers[] = {
	&n2_crypto_driver,
	&n2_mau_driver,
};

static int __init n2_init(void)
{
	return platform_register_drivers(drivers, ARRAY_SIZE(drivers));
}

static void __exit n2_exit(void)
{
	platform_unregister_drivers(drivers, ARRAY_SIZE(drivers));
}

module_init(n2_init);
module_exit(n2_exit);
