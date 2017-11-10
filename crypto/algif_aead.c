/*
 * algif_aead: User-space interface for AEAD algorithms
 *
 * Copyright (C) 2014, Stephan Mueller <smueller@chronox.de>
 *
 * This file provides the user-space API for AEAD ciphers.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * The following concept of the memory management is used:
 *
 * The kernel maintains two SGLs, the TX SGL and the RX SGL. The TX SGL is
 * filled by user space with the data submitted via sendpage/sendmsg. Filling
 * up the TX SGL does not cause a crypto operation -- the data will only be
 * tracked by the kernel. Upon receipt of one recvmsg call, the caller must
 * provide a buffer which is tracked with the RX SGL.
 *
 * During the processing of the recvmsg operation, the cipher request is
 * allocated and prepared. As part of the recvmsg operation, the processed
 * TX buffers are extracted from the TX SGL into a separate SGL.
 *
 * After the completion of the crypto operation, the RX SGL and the cipher
 * request is released. The extracted TX SGL parts are released together with
 * the RX SGL release.
 */

#include <crypto/internal/aead.h>
#include <crypto/scatterwalk.h>
#include <crypto/if_alg.h>
#include <crypto/skcipher.h>
#include <crypto/null.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/net.h>
#include <net/sock.h>

struct aead_tfm {
	struct crypto_aead *aead;
	bool has_key;
	struct crypto_skcipher *null_tfm;
};

static inline bool aead_sufficient_data(struct sock *sk)
{
	struct alg_sock *ask = alg_sk(sk);
	struct sock *psk = ask->parent;
	struct alg_sock *pask = alg_sk(psk);
	struct af_alg_ctx *ctx = ask->private;
	struct aead_tfm *aeadc = pask->private;
	struct crypto_aead *tfm = aeadc->aead;
	unsigned int as = crypto_aead_authsize(tfm);

	/*
	 * The minimum amount of memory needed for an AEAD cipher is
	 * the AAD and in case of decryption the tag.
	 */
	return ctx->used >= ctx->aead_assoclen + (ctx->enc ? 0 : as);
}

static int aead_sendmsg(struct socket *sock, struct msghdr *msg, size_t size)
{
	struct sock *sk = sock->sk;
	struct alg_sock *ask = alg_sk(sk);
	struct sock *psk = ask->parent;
	struct alg_sock *pask = alg_sk(psk);
	struct aead_tfm *aeadc = pask->private;
	struct crypto_aead *tfm = aeadc->aead;
	unsigned int ivsize = crypto_aead_ivsize(tfm);

	return af_alg_sendmsg(sock, msg, size, ivsize);
}

static int crypto_aead_copy_sgl(struct crypto_skcipher *null_tfm,
				struct scatterlist *src,
				struct scatterlist *dst, unsigned int len)
{
	SKCIPHER_REQUEST_ON_STACK(skreq, null_tfm);

	skcipher_request_set_tfm(skreq, null_tfm);
	skcipher_request_set_callback(skreq, CRYPTO_TFM_REQ_MAY_BACKLOG,
				      NULL, NULL);
	skcipher_request_set_crypt(skreq, src, dst, len, NULL);

	return crypto_skcipher_encrypt(skreq);
}

static int _aead_recvmsg(struct socket *sock, struct msghdr *msg,
			 size_t ignored, int flags)
{
	struct sock *sk = sock->sk;
	struct alg_sock *ask = alg_sk(sk);
	struct sock *psk = ask->parent;
	struct alg_sock *pask = alg_sk(psk);
	struct af_alg_ctx *ctx = ask->private;
	struct aead_tfm *aeadc = pask->private;
	struct crypto_aead *tfm = aeadc->aead;
	struct crypto_skcipher *null_tfm = aeadc->null_tfm;
	unsigned int i, as = crypto_aead_authsize(tfm);
	struct af_alg_async_req *areq;
	struct af_alg_tsgl *tsgl, *tmp;
	struct scatterlist *rsgl_src, *tsgl_src = NULL;
	int err = 0;
	size_t used = 0;		/* [in]  TX bufs to be en/decrypted */
	size_t outlen = 0;		/* [out] RX bufs produced by kernel */
	size_t usedpages = 0;		/* [in]  RX bufs to be used from user */
	size_t processed = 0;		/* [in]  TX bufs to be consumed */

	/*
	 * Data length provided by caller via sendmsg/sendpage that has not
	 * yet been processed.
	 */
	used = ctx->used;

	/*
	 * Make sure sufficient data is present -- note, the same check is
	 * is also present in sendmsg/sendpage. The checks in sendpage/sendmsg
	 * shall provide an information to the data sender that something is
	 * wrong, but they are irrelevant to maintain the kernel integrity.
	 * We need this check here too in case user space decides to not honor
	 * the error message in sendmsg/sendpage and still call recvmsg. This
	 * check here protects the kernel integrity.
	 */
	if (!aead_sufficient_data(sk))
		return -EINVAL;

	/*
	 * Calculate the minimum output buffer size holding the result of the
	 * cipher operation. When encrypting data, the receiving buffer is
	 * larger by the tag length compared to the input buffer as the
	 * encryption operation generates the tag. For decryption, the input
	 * buffer provides the tag which is consumed resulting in only the
	 * plaintext without a buffer for the tag returned to the caller.
	 */
	if (ctx->enc)
		outlen = used + as;
	else
		outlen = used - as;

	/*
	 * The cipher operation input data is reduced by the associated data
	 * length as this data is processed separately later on.
	 */
	used -= ctx->aead_assoclen;

	/* Allocate cipher request for current operation. */
	areq = af_alg_alloc_areq(sk, sizeof(struct af_alg_async_req) +
				     crypto_aead_reqsize(tfm));
	if (IS_ERR(areq))
		return PTR_ERR(areq);

	/* convert iovecs of output buffers into RX SGL */
	err = af_alg_get_rsgl(sk, msg, flags, areq, outlen, &usedpages);
	if (err)
		goto free;

	/*
	 * Ensure output buffer is sufficiently large. If the caller provides
	 * less buffer space, only use the relative required input size. This
	 * allows AIO operation where the caller sent all data to be processed
	 * and the AIO operation performs the operation on the different chunks
	 * of the input data.
	 */
	if (usedpages < outlen) {
		size_t less = outlen - usedpages;

		if (used < less) {
			err = -EINVAL;
			goto free;
		}
		used -= less;
		outlen -= less;
	}

	processed = used + ctx->aead_assoclen;
	list_for_each_entry_safe(tsgl, tmp, &ctx->tsgl_list, list) {
		for (i = 0; i < tsgl->cur; i++) {
			struct scatterlist *process_sg = tsgl->sg + i;

			if (!(process_sg->length) || !sg_page(process_sg))
				continue;
			tsgl_src = process_sg;
			break;
		}
		if (tsgl_src)
			break;
	}
	if (processed && !tsgl_src) {
		err = -EFAULT;
		goto free;
	}

	/*
	 * Copy of AAD from source to destination
	 *
	 * The AAD is copied to the destination buffer without change. Even
	 * when user space uses an in-place cipher operation, the kernel
	 * will copy the data as it does not see whether such in-place operation
	 * is initiated.
	 *
	 * To ensure efficiency, the following implementation ensure that the
	 * ciphers are invoked to perform a crypto operation in-place. This
	 * is achieved by memory management specified as follows.
	 */

	/* Use the RX SGL as source (and destination) for crypto op. */
	rsgl_src = areq->first_rsgl.sgl.sg;

	if (ctx->enc) {
		/*
		 * Encryption operation - The in-place cipher operation is
		 * achieved by the following operation:
		 *
		 * TX SGL: AAD || PT
		 *	    |	   |
		 *	    | copy |
		 *	    v	   v
		 * RX SGL: AAD || PT || Tag
		 */
		err = crypto_aead_copy_sgl(null_tfm, tsgl_src,
					   areq->first_rsgl.sgl.sg, processed);
		if (err)
			goto free;
		af_alg_pull_tsgl(sk, processed, NULL, 0);
	} else {
		/*
		 * Decryption operation - To achieve an in-place cipher
		 * operation, the following  SGL structure is used:
		 *
		 * TX SGL: AAD || CT || Tag
		 *	    |	   |	 ^
		 *	    | copy |	 | Create SGL link.
		 *	    v	   v	 |
		 * RX SGL: AAD || CT ----+
		 */

		 /* Copy AAD || CT to RX SGL buffer for in-place operation. */
		err = crypto_aead_copy_sgl(null_tfm, tsgl_src,
					   areq->first_rsgl.sgl.sg, outlen);
		if (err)
			goto free;

		/* Create TX SGL for tag and chain it to RX SGL. */
		areq->tsgl_entries = af_alg_count_tsgl(sk, processed,
						       processed - as);
		if (!areq->tsgl_entries)
			areq->tsgl_entries = 1;
		areq->tsgl = sock_kmalloc(sk, sizeof(*areq->tsgl) *
					      areq->tsgl_entries,
					  GFP_KERNEL);
		if (!areq->tsgl) {
			err = -ENOMEM;
			goto free;
		}
		sg_init_table(areq->tsgl, areq->tsgl_entries);

		/* Release TX SGL, except for tag data and reassign tag data. */
		af_alg_pull_tsgl(sk, processed, areq->tsgl, processed - as);

		/* chain the areq TX SGL holding the tag with RX SGL */
		if (usedpages) {
			/* RX SGL present */
			struct af_alg_sgl *sgl_prev = &areq->last_rsgl->sgl;

			sg_unmark_end(sgl_prev->sg + sgl_prev->npages - 1);
			sg_chain(sgl_prev->sg, sgl_prev->npages + 1,
				 areq->tsgl);
		} else
			/* no RX SGL present (e.g. authentication only) */
			rsgl_src = areq->tsgl;
	}

	/* Initialize the crypto operation */
	aead_request_set_crypt(&areq->cra_u.aead_req, rsgl_src,
			       areq->first_rsgl.sgl.sg, used, ctx->iv);
	aead_request_set_ad(&areq->cra_u.aead_req, ctx->aead_assoclen);
	aead_request_set_tfm(&areq->cra_u.aead_req, tfm);

	if (msg->msg_iocb && !is_sync_kiocb(msg->msg_iocb)) {
		/* AIO operation */
		sock_hold(sk);
		areq->iocb = msg->msg_iocb;
		aead_request_set_callback(&areq->cra_u.aead_req,
					  CRYPTO_TFM_REQ_MAY_BACKLOG,
					  af_alg_async_cb, areq);
		err = ctx->enc ? crypto_aead_encrypt(&areq->cra_u.aead_req) :
				 crypto_aead_decrypt(&areq->cra_u.aead_req);

		/* AIO operation in progress */
		if (err == -EINPROGRESS || err == -EBUSY) {
			/* Remember output size that will be generated. */
			areq->outlen = outlen;

			return -EIOCBQUEUED;
		}

		sock_put(sk);
	} else {
		/* Synchronous operation */
		aead_request_set_callback(&areq->cra_u.aead_req,
					  CRYPTO_TFM_REQ_MAY_BACKLOG,
					  af_alg_complete, &ctx->completion);
		err = af_alg_wait_for_completion(ctx->enc ?
				crypto_aead_encrypt(&areq->cra_u.aead_req) :
				crypto_aead_decrypt(&areq->cra_u.aead_req),
						 &ctx->completion);
	}


free:
	af_alg_free_resources(areq);

	return err ? err : outlen;
}

static int aead_recvmsg(struct socket *sock, struct msghdr *msg,
			size_t ignored, int flags)
{
	struct sock *sk = sock->sk;
	int ret = 0;

	lock_sock(sk);
	while (msg_data_left(msg)) {
		int err = _aead_recvmsg(sock, msg, ignored, flags);

		/*
		 * This error covers -EIOCBQUEUED which implies that we can
		 * only handle one AIO request. If the caller wants to have
		 * multiple AIO requests in parallel, he must make multiple
		 * separate AIO calls.
		 *
		 * Also return the error if no data has been processed so far.
		 */
		if (err <= 0) {
			if (err == -EIOCBQUEUED || err == -EBADMSG || !ret)
				ret = err;
			goto out;
		}

		ret += err;
	}

out:
	af_alg_wmem_wakeup(sk);
	release_sock(sk);
	return ret;
}

static struct proto_ops algif_aead_ops = {
	.family		=	PF_ALG,

	.connect	=	sock_no_connect,
	.socketpair	=	sock_no_socketpair,
	.getname	=	sock_no_getname,
	.ioctl		=	sock_no_ioctl,
	.listen		=	sock_no_listen,
	.shutdown	=	sock_no_shutdown,
	.getsockopt	=	sock_no_getsockopt,
	.mmap		=	sock_no_mmap,
	.bind		=	sock_no_bind,
	.accept		=	sock_no_accept,
	.setsockopt	=	sock_no_setsockopt,

	.release	=	af_alg_release,
	.sendmsg	=	aead_sendmsg,
	.sendpage	=	af_alg_sendpage,
	.recvmsg	=	aead_recvmsg,
	.poll		=	af_alg_poll,
};

static int aead_check_key(struct socket *sock)
{
	int err = 0;
	struct sock *psk;
	struct alg_sock *pask;
	struct aead_tfm *tfm;
	struct sock *sk = sock->sk;
	struct alg_sock *ask = alg_sk(sk);

	lock_sock(sk);
	if (ask->refcnt)
		goto unlock_child;

	psk = ask->parent;
	pask = alg_sk(ask->parent);
	tfm = pask->private;

	err = -ENOKEY;
	lock_sock_nested(psk, SINGLE_DEPTH_NESTING);
	if (!tfm->has_key)
		goto unlock;

	if (!pask->refcnt++)
		sock_hold(psk);

	ask->refcnt = 1;
	sock_put(psk);

	err = 0;

unlock:
	release_sock(psk);
unlock_child:
	release_sock(sk);

	return err;
}

static int aead_sendmsg_nokey(struct socket *sock, struct msghdr *msg,
				  size_t size)
{
	int err;

	err = aead_check_key(sock);
	if (err)
		return err;

	return aead_sendmsg(sock, msg, size);
}

static ssize_t aead_sendpage_nokey(struct socket *sock, struct page *page,
				       int offset, size_t size, int flags)
{
	int err;

	err = aead_check_key(sock);
	if (err)
		return err;

	return af_alg_sendpage(sock, page, offset, size, flags);
}

static int aead_recvmsg_nokey(struct socket *sock, struct msghdr *msg,
				  size_t ignored, int flags)
{
	int err;

	err = aead_check_key(sock);
	if (err)
		return err;

	return aead_recvmsg(sock, msg, ignored, flags);
}

static struct proto_ops algif_aead_ops_nokey = {
	.family		=	PF_ALG,

	.connect	=	sock_no_connect,
	.socketpair	=	sock_no_socketpair,
	.getname	=	sock_no_getname,
	.ioctl		=	sock_no_ioctl,
	.listen		=	sock_no_listen,
	.shutdown	=	sock_no_shutdown,
	.getsockopt	=	sock_no_getsockopt,
	.mmap		=	sock_no_mmap,
	.bind		=	sock_no_bind,
	.accept		=	sock_no_accept,
	.setsockopt	=	sock_no_setsockopt,

	.release	=	af_alg_release,
	.sendmsg	=	aead_sendmsg_nokey,
	.sendpage	=	aead_sendpage_nokey,
	.recvmsg	=	aead_recvmsg_nokey,
	.poll		=	af_alg_poll,
};

static void *aead_bind(const char *name, u32 type, u32 mask)
{
	struct aead_tfm *tfm;
	struct crypto_aead *aead;
	struct crypto_skcipher *null_tfm;

	tfm = kzalloc(sizeof(*tfm), GFP_KERNEL);
	if (!tfm)
		return ERR_PTR(-ENOMEM);

	aead = crypto_alloc_aead(name, type, mask);
	if (IS_ERR(aead)) {
		kfree(tfm);
		return ERR_CAST(aead);
	}

	null_tfm = crypto_get_default_null_skcipher2();
	if (IS_ERR(null_tfm)) {
		crypto_free_aead(aead);
		kfree(tfm);
		return ERR_CAST(null_tfm);
	}

	tfm->aead = aead;
	tfm->null_tfm = null_tfm;

	return tfm;
}

static void aead_release(void *private)
{
	struct aead_tfm *tfm = private;

	crypto_free_aead(tfm->aead);
	kfree(tfm);
}

static int aead_setauthsize(void *private, unsigned int authsize)
{
	struct aead_tfm *tfm = private;

	return crypto_aead_setauthsize(tfm->aead, authsize);
}

static int aead_setkey(void *private, const u8 *key, unsigned int keylen)
{
	struct aead_tfm *tfm = private;
	int err;

	err = crypto_aead_setkey(tfm->aead, key, keylen);
	tfm->has_key = !err;

	return err;
}

static void aead_sock_destruct(struct sock *sk)
{
	struct alg_sock *ask = alg_sk(sk);
	struct af_alg_ctx *ctx = ask->private;
	struct sock *psk = ask->parent;
	struct alg_sock *pask = alg_sk(psk);
	struct aead_tfm *aeadc = pask->private;
	struct crypto_aead *tfm = aeadc->aead;
	unsigned int ivlen = crypto_aead_ivsize(tfm);

	af_alg_pull_tsgl(sk, ctx->used, NULL, 0);
	crypto_put_default_null_skcipher2();
	sock_kzfree_s(sk, ctx->iv, ivlen);
	sock_kfree_s(sk, ctx, ctx->len);
	af_alg_release_parent(sk);
}

static int aead_accept_parent_nokey(void *private, struct sock *sk)
{
	struct af_alg_ctx *ctx;
	struct alg_sock *ask = alg_sk(sk);
	struct aead_tfm *tfm = private;
	struct crypto_aead *aead = tfm->aead;
	unsigned int len = sizeof(*ctx);
	unsigned int ivlen = crypto_aead_ivsize(aead);

	ctx = sock_kmalloc(sk, len, GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;
	memset(ctx, 0, len);

	ctx->iv = sock_kmalloc(sk, ivlen, GFP_KERNEL);
	if (!ctx->iv) {
		sock_kfree_s(sk, ctx, len);
		return -ENOMEM;
	}
	memset(ctx->iv, 0, ivlen);

	INIT_LIST_HEAD(&ctx->tsgl_list);
	ctx->len = len;
	ctx->used = 0;
	ctx->rcvused = 0;
	ctx->more = 0;
	ctx->merge = 0;
	ctx->enc = 0;
	ctx->aead_assoclen = 0;
	af_alg_init_completion(&ctx->completion);

	ask->private = ctx;

	sk->sk_destruct = aead_sock_destruct;

	return 0;
}

static int aead_accept_parent(void *private, struct sock *sk)
{
	struct aead_tfm *tfm = private;

	if (!tfm->has_key)
		return -ENOKEY;

	return aead_accept_parent_nokey(private, sk);
}

static const struct af_alg_type algif_type_aead = {
	.bind		=	aead_bind,
	.release	=	aead_release,
	.setkey		=	aead_setkey,
	.setauthsize	=	aead_setauthsize,
	.accept		=	aead_accept_parent,
	.accept_nokey	=	aead_accept_parent_nokey,
	.ops		=	&algif_aead_ops,
	.ops_nokey	=	&algif_aead_ops_nokey,
	.name		=	"aead",
	.owner		=	THIS_MODULE
};

static int __init algif_aead_init(void)
{
	return af_alg_register_type(&algif_type_aead);
}

static void __exit algif_aead_exit(void)
{
	int err = af_alg_unregister_type(&algif_type_aead);
	BUG_ON(err);
}

module_init(algif_aead_init);
module_exit(algif_aead_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Stephan Mueller <smueller@chronox.de>");
MODULE_DESCRIPTION("AEAD kernel crypto API user space interface");
