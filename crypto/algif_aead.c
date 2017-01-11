/*
 * algif_aead: User-space interface for AEAD algorithms
 *
 * Copyright (C) 2014, Stephan Mueller <smueller@chronox.de>
 *
 * This file provides the user-space API for AEAD ciphers.
 *
 * This file is derived from algif_skcipher.c.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#include <crypto/internal/aead.h>
#include <crypto/scatterwalk.h>
#include <crypto/if_alg.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/net.h>
#include <net/sock.h>

struct aead_sg_list {
	unsigned int cur;
	struct scatterlist sg[ALG_MAX_PAGES];
};

struct aead_async_rsgl {
	struct af_alg_sgl sgl;
	struct list_head list;
};

struct aead_async_req {
	struct scatterlist *tsgl;
	struct aead_async_rsgl first_rsgl;
	struct list_head list;
	struct kiocb *iocb;
	unsigned int tsgls;
	char iv[];
};

struct aead_ctx {
	struct aead_sg_list tsgl;
	struct aead_async_rsgl first_rsgl;
	struct list_head list;

	void *iv;

	struct af_alg_completion completion;

	unsigned long used;

	unsigned int len;
	bool more;
	bool merge;
	bool enc;

	size_t aead_assoclen;
	struct aead_request aead_req;
};

static inline int aead_sndbuf(struct sock *sk)
{
	struct alg_sock *ask = alg_sk(sk);
	struct aead_ctx *ctx = ask->private;

	return max_t(int, max_t(int, sk->sk_sndbuf & PAGE_MASK, PAGE_SIZE) -
			  ctx->used, 0);
}

static inline bool aead_writable(struct sock *sk)
{
	return PAGE_SIZE <= aead_sndbuf(sk);
}

static inline bool aead_sufficient_data(struct aead_ctx *ctx)
{
	unsigned as = crypto_aead_authsize(crypto_aead_reqtfm(&ctx->aead_req));

	/*
	 * The minimum amount of memory needed for an AEAD cipher is
	 * the AAD and in case of decryption the tag.
	 */
	return ctx->used >= ctx->aead_assoclen + (ctx->enc ? 0 : as);
}

static void aead_reset_ctx(struct aead_ctx *ctx)
{
	struct aead_sg_list *sgl = &ctx->tsgl;

	sg_init_table(sgl->sg, ALG_MAX_PAGES);
	sgl->cur = 0;
	ctx->used = 0;
	ctx->more = 0;
	ctx->merge = 0;
}

static void aead_put_sgl(struct sock *sk)
{
	struct alg_sock *ask = alg_sk(sk);
	struct aead_ctx *ctx = ask->private;
	struct aead_sg_list *sgl = &ctx->tsgl;
	struct scatterlist *sg = sgl->sg;
	unsigned int i;

	for (i = 0; i < sgl->cur; i++) {
		if (!sg_page(sg + i))
			continue;

		put_page(sg_page(sg + i));
		sg_assign_page(sg + i, NULL);
	}
	aead_reset_ctx(ctx);
}

static void aead_wmem_wakeup(struct sock *sk)
{
	struct socket_wq *wq;

	if (!aead_writable(sk))
		return;

	rcu_read_lock();
	wq = rcu_dereference(sk->sk_wq);
	if (skwq_has_sleeper(wq))
		wake_up_interruptible_sync_poll(&wq->wait, POLLIN |
							   POLLRDNORM |
							   POLLRDBAND);
	sk_wake_async(sk, SOCK_WAKE_WAITD, POLL_IN);
	rcu_read_unlock();
}

static int aead_wait_for_data(struct sock *sk, unsigned flags)
{
	DEFINE_WAIT_FUNC(wait, woken_wake_function);
	struct alg_sock *ask = alg_sk(sk);
	struct aead_ctx *ctx = ask->private;
	long timeout;
	int err = -ERESTARTSYS;

	if (flags & MSG_DONTWAIT)
		return -EAGAIN;

	sk_set_bit(SOCKWQ_ASYNC_WAITDATA, sk);
	add_wait_queue(sk_sleep(sk), &wait);
	for (;;) {
		if (signal_pending(current))
			break;
		timeout = MAX_SCHEDULE_TIMEOUT;
		if (sk_wait_event(sk, &timeout, !ctx->more, &wait)) {
			err = 0;
			break;
		}
	}
	remove_wait_queue(sk_sleep(sk), &wait);

	sk_clear_bit(SOCKWQ_ASYNC_WAITDATA, sk);

	return err;
}

static void aead_data_wakeup(struct sock *sk)
{
	struct alg_sock *ask = alg_sk(sk);
	struct aead_ctx *ctx = ask->private;
	struct socket_wq *wq;

	if (ctx->more)
		return;
	if (!ctx->used)
		return;

	rcu_read_lock();
	wq = rcu_dereference(sk->sk_wq);
	if (skwq_has_sleeper(wq))
		wake_up_interruptible_sync_poll(&wq->wait, POLLOUT |
							   POLLRDNORM |
							   POLLRDBAND);
	sk_wake_async(sk, SOCK_WAKE_SPACE, POLL_OUT);
	rcu_read_unlock();
}

static int aead_sendmsg(struct socket *sock, struct msghdr *msg, size_t size)
{
	struct sock *sk = sock->sk;
	struct alg_sock *ask = alg_sk(sk);
	struct aead_ctx *ctx = ask->private;
	unsigned ivsize =
		crypto_aead_ivsize(crypto_aead_reqtfm(&ctx->aead_req));
	struct aead_sg_list *sgl = &ctx->tsgl;
	struct af_alg_control con = {};
	long copied = 0;
	bool enc = 0;
	bool init = 0;
	int err = -EINVAL;

	if (msg->msg_controllen) {
		err = af_alg_cmsg_send(msg, &con);
		if (err)
			return err;

		init = 1;
		switch (con.op) {
		case ALG_OP_ENCRYPT:
			enc = 1;
			break;
		case ALG_OP_DECRYPT:
			enc = 0;
			break;
		default:
			return -EINVAL;
		}

		if (con.iv && con.iv->ivlen != ivsize)
			return -EINVAL;
	}

	lock_sock(sk);
	if (!ctx->more && ctx->used)
		goto unlock;

	if (init) {
		ctx->enc = enc;
		if (con.iv)
			memcpy(ctx->iv, con.iv->iv, ivsize);

		ctx->aead_assoclen = con.aead_assoclen;
	}

	while (size) {
		size_t len = size;
		struct scatterlist *sg = NULL;

		/* use the existing memory in an allocated page */
		if (ctx->merge) {
			sg = sgl->sg + sgl->cur - 1;
			len = min_t(unsigned long, len,
				    PAGE_SIZE - sg->offset - sg->length);
			err = memcpy_from_msg(page_address(sg_page(sg)) +
					      sg->offset + sg->length,
					      msg, len);
			if (err)
				goto unlock;

			sg->length += len;
			ctx->merge = (sg->offset + sg->length) &
				     (PAGE_SIZE - 1);

			ctx->used += len;
			copied += len;
			size -= len;
			continue;
		}

		if (!aead_writable(sk)) {
			/* user space sent too much data */
			aead_put_sgl(sk);
			err = -EMSGSIZE;
			goto unlock;
		}

		/* allocate a new page */
		len = min_t(unsigned long, size, aead_sndbuf(sk));
		while (len) {
			size_t plen = 0;

			if (sgl->cur >= ALG_MAX_PAGES) {
				aead_put_sgl(sk);
				err = -E2BIG;
				goto unlock;
			}

			sg = sgl->sg + sgl->cur;
			plen = min_t(size_t, len, PAGE_SIZE);

			sg_assign_page(sg, alloc_page(GFP_KERNEL));
			err = -ENOMEM;
			if (!sg_page(sg))
				goto unlock;

			err = memcpy_from_msg(page_address(sg_page(sg)),
					      msg, plen);
			if (err) {
				__free_page(sg_page(sg));
				sg_assign_page(sg, NULL);
				goto unlock;
			}

			sg->offset = 0;
			sg->length = plen;
			len -= plen;
			ctx->used += plen;
			copied += plen;
			sgl->cur++;
			size -= plen;
			ctx->merge = plen & (PAGE_SIZE - 1);
		}
	}

	err = 0;

	ctx->more = msg->msg_flags & MSG_MORE;
	if (!ctx->more && !aead_sufficient_data(ctx)) {
		aead_put_sgl(sk);
		err = -EMSGSIZE;
	}

unlock:
	aead_data_wakeup(sk);
	release_sock(sk);

	return err ?: copied;
}

static ssize_t aead_sendpage(struct socket *sock, struct page *page,
			     int offset, size_t size, int flags)
{
	struct sock *sk = sock->sk;
	struct alg_sock *ask = alg_sk(sk);
	struct aead_ctx *ctx = ask->private;
	struct aead_sg_list *sgl = &ctx->tsgl;
	int err = -EINVAL;

	if (flags & MSG_SENDPAGE_NOTLAST)
		flags |= MSG_MORE;

	if (sgl->cur >= ALG_MAX_PAGES)
		return -E2BIG;

	lock_sock(sk);
	if (!ctx->more && ctx->used)
		goto unlock;

	if (!size)
		goto done;

	if (!aead_writable(sk)) {
		/* user space sent too much data */
		aead_put_sgl(sk);
		err = -EMSGSIZE;
		goto unlock;
	}

	ctx->merge = 0;

	get_page(page);
	sg_set_page(sgl->sg + sgl->cur, page, size, offset);
	sgl->cur++;
	ctx->used += size;

	err = 0;

done:
	ctx->more = flags & MSG_MORE;
	if (!ctx->more && !aead_sufficient_data(ctx)) {
		aead_put_sgl(sk);
		err = -EMSGSIZE;
	}

unlock:
	aead_data_wakeup(sk);
	release_sock(sk);

	return err ?: size;
}

#define GET_ASYM_REQ(req, tfm) (struct aead_async_req *) \
		((char *)req + sizeof(struct aead_request) + \
		 crypto_aead_reqsize(tfm))

 #define GET_REQ_SIZE(tfm) sizeof(struct aead_async_req) + \
	crypto_aead_reqsize(tfm) + crypto_aead_ivsize(tfm) + \
	sizeof(struct aead_request)

static void aead_async_cb(struct crypto_async_request *_req, int err)
{
	struct sock *sk = _req->data;
	struct alg_sock *ask = alg_sk(sk);
	struct aead_ctx *ctx = ask->private;
	struct crypto_aead *tfm = crypto_aead_reqtfm(&ctx->aead_req);
	struct aead_request *req = aead_request_cast(_req);
	struct aead_async_req *areq = GET_ASYM_REQ(req, tfm);
	struct scatterlist *sg = areq->tsgl;
	struct aead_async_rsgl *rsgl;
	struct kiocb *iocb = areq->iocb;
	unsigned int i, reqlen = GET_REQ_SIZE(tfm);

	list_for_each_entry(rsgl, &areq->list, list) {
		af_alg_free_sg(&rsgl->sgl);
		if (rsgl != &areq->first_rsgl)
			sock_kfree_s(sk, rsgl, sizeof(*rsgl));
	}

	for (i = 0; i < areq->tsgls; i++)
		put_page(sg_page(sg + i));

	sock_kfree_s(sk, areq->tsgl, sizeof(*areq->tsgl) * areq->tsgls);
	sock_kfree_s(sk, req, reqlen);
	__sock_put(sk);
	iocb->ki_complete(iocb, err, err);
}

static int aead_recvmsg_async(struct socket *sock, struct msghdr *msg,
			      int flags)
{
	struct sock *sk = sock->sk;
	struct alg_sock *ask = alg_sk(sk);
	struct aead_ctx *ctx = ask->private;
	struct crypto_aead *tfm = crypto_aead_reqtfm(&ctx->aead_req);
	struct aead_async_req *areq;
	struct aead_request *req = NULL;
	struct aead_sg_list *sgl = &ctx->tsgl;
	struct aead_async_rsgl *last_rsgl = NULL, *rsgl;
	unsigned int as = crypto_aead_authsize(tfm);
	unsigned int i, reqlen = GET_REQ_SIZE(tfm);
	int err = -ENOMEM;
	unsigned long used;
	size_t outlen = 0;
	size_t usedpages = 0;

	lock_sock(sk);
	if (ctx->more) {
		err = aead_wait_for_data(sk, flags);
		if (err)
			goto unlock;
	}

	if (!aead_sufficient_data(ctx))
		goto unlock;

	used = ctx->used;
	if (ctx->enc)
		outlen = used + as;
	else
		outlen = used - as;

	req = sock_kmalloc(sk, reqlen, GFP_KERNEL);
	if (unlikely(!req))
		goto unlock;

	areq = GET_ASYM_REQ(req, tfm);
	memset(&areq->first_rsgl, '\0', sizeof(areq->first_rsgl));
	INIT_LIST_HEAD(&areq->list);
	areq->iocb = msg->msg_iocb;
	memcpy(areq->iv, ctx->iv, crypto_aead_ivsize(tfm));
	aead_request_set_tfm(req, tfm);
	aead_request_set_ad(req, ctx->aead_assoclen);
	aead_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG,
				  aead_async_cb, sk);
	used -= ctx->aead_assoclen;

	/* take over all tx sgls from ctx */
	areq->tsgl = sock_kmalloc(sk,
				  sizeof(*areq->tsgl) * max_t(u32, sgl->cur, 1),
				  GFP_KERNEL);
	if (unlikely(!areq->tsgl))
		goto free;

	sg_init_table(areq->tsgl, max_t(u32, sgl->cur, 1));
	for (i = 0; i < sgl->cur; i++)
		sg_set_page(&areq->tsgl[i], sg_page(&sgl->sg[i]),
			    sgl->sg[i].length, sgl->sg[i].offset);

	areq->tsgls = sgl->cur;

	/* create rx sgls */
	while (outlen > usedpages && iov_iter_count(&msg->msg_iter)) {
		size_t seglen = min_t(size_t, iov_iter_count(&msg->msg_iter),
				      (outlen - usedpages));

		if (list_empty(&areq->list)) {
			rsgl = &areq->first_rsgl;

		} else {
			rsgl = sock_kmalloc(sk, sizeof(*rsgl), GFP_KERNEL);
			if (unlikely(!rsgl)) {
				err = -ENOMEM;
				goto free;
			}
		}
		rsgl->sgl.npages = 0;
		list_add_tail(&rsgl->list, &areq->list);

		/* make one iovec available as scatterlist */
		err = af_alg_make_sg(&rsgl->sgl, &msg->msg_iter, seglen);
		if (err < 0)
			goto free;

		usedpages += err;

		/* chain the new scatterlist with previous one */
		if (last_rsgl)
			af_alg_link_sg(&last_rsgl->sgl, &rsgl->sgl);

		last_rsgl = rsgl;

		iov_iter_advance(&msg->msg_iter, err);
	}

	/* ensure output buffer is sufficiently large */
	if (usedpages < outlen) {
		err = -EINVAL;
		goto unlock;
	}

	aead_request_set_crypt(req, areq->tsgl, areq->first_rsgl.sgl.sg, used,
			       areq->iv);
	err = ctx->enc ? crypto_aead_encrypt(req) : crypto_aead_decrypt(req);
	if (err) {
		if (err == -EINPROGRESS) {
			sock_hold(sk);
			err = -EIOCBQUEUED;
			aead_reset_ctx(ctx);
			goto unlock;
		} else if (err == -EBADMSG) {
			aead_put_sgl(sk);
		}
		goto free;
	}
	aead_put_sgl(sk);

free:
	list_for_each_entry(rsgl, &areq->list, list) {
		af_alg_free_sg(&rsgl->sgl);
		if (rsgl != &areq->first_rsgl)
			sock_kfree_s(sk, rsgl, sizeof(*rsgl));
	}
	if (areq->tsgl)
		sock_kfree_s(sk, areq->tsgl, sizeof(*areq->tsgl) * areq->tsgls);
	if (req)
		sock_kfree_s(sk, req, reqlen);
unlock:
	aead_wmem_wakeup(sk);
	release_sock(sk);
	return err ? err : outlen;
}

static int aead_recvmsg_sync(struct socket *sock, struct msghdr *msg, int flags)
{
	struct sock *sk = sock->sk;
	struct alg_sock *ask = alg_sk(sk);
	struct aead_ctx *ctx = ask->private;
	unsigned as = crypto_aead_authsize(crypto_aead_reqtfm(&ctx->aead_req));
	struct aead_sg_list *sgl = &ctx->tsgl;
	struct aead_async_rsgl *last_rsgl = NULL;
	struct aead_async_rsgl *rsgl, *tmp;
	int err = -EINVAL;
	unsigned long used = 0;
	size_t outlen = 0;
	size_t usedpages = 0;

	lock_sock(sk);

	/*
	 * Please see documentation of aead_request_set_crypt for the
	 * description of the AEAD memory structure expected from the caller.
	 */

	if (ctx->more) {
		err = aead_wait_for_data(sk, flags);
		if (err)
			goto unlock;
	}

	/* data length provided by caller via sendmsg/sendpage */
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
	if (!aead_sufficient_data(ctx))
		goto unlock;

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

	/* convert iovecs of output buffers into scatterlists */
	while (outlen > usedpages && iov_iter_count(&msg->msg_iter)) {
		size_t seglen = min_t(size_t, iov_iter_count(&msg->msg_iter),
				      (outlen - usedpages));

		if (list_empty(&ctx->list)) {
			rsgl = &ctx->first_rsgl;
		} else {
			rsgl = sock_kmalloc(sk, sizeof(*rsgl), GFP_KERNEL);
			if (unlikely(!rsgl)) {
				err = -ENOMEM;
				goto unlock;
			}
		}
		rsgl->sgl.npages = 0;
		list_add_tail(&rsgl->list, &ctx->list);

		/* make one iovec available as scatterlist */
		err = af_alg_make_sg(&rsgl->sgl, &msg->msg_iter, seglen);
		if (err < 0)
			goto unlock;
		usedpages += err;
		/* chain the new scatterlist with previous one */
		if (last_rsgl)
			af_alg_link_sg(&last_rsgl->sgl, &rsgl->sgl);

		last_rsgl = rsgl;

		iov_iter_advance(&msg->msg_iter, err);
	}

	/* ensure output buffer is sufficiently large */
	if (usedpages < outlen) {
		err = -EINVAL;
		goto unlock;
	}

	sg_mark_end(sgl->sg + sgl->cur - 1);
	aead_request_set_crypt(&ctx->aead_req, sgl->sg, ctx->first_rsgl.sgl.sg,
			       used, ctx->iv);
	aead_request_set_ad(&ctx->aead_req, ctx->aead_assoclen);

	err = af_alg_wait_for_completion(ctx->enc ?
					 crypto_aead_encrypt(&ctx->aead_req) :
					 crypto_aead_decrypt(&ctx->aead_req),
					 &ctx->completion);

	if (err) {
		/* EBADMSG implies a valid cipher operation took place */
		if (err == -EBADMSG)
			aead_put_sgl(sk);

		goto unlock;
	}

	aead_put_sgl(sk);
	err = 0;

unlock:
	list_for_each_entry_safe(rsgl, tmp, &ctx->list, list) {
		af_alg_free_sg(&rsgl->sgl);
		if (rsgl != &ctx->first_rsgl)
			sock_kfree_s(sk, rsgl, sizeof(*rsgl));
		list_del(&rsgl->list);
	}
	INIT_LIST_HEAD(&ctx->list);
	aead_wmem_wakeup(sk);
	release_sock(sk);

	return err ? err : outlen;
}

static int aead_recvmsg(struct socket *sock, struct msghdr *msg, size_t ignored,
			int flags)
{
	return (msg->msg_iocb && !is_sync_kiocb(msg->msg_iocb)) ?
		aead_recvmsg_async(sock, msg, flags) :
		aead_recvmsg_sync(sock, msg, flags);
}

static unsigned int aead_poll(struct file *file, struct socket *sock,
			      poll_table *wait)
{
	struct sock *sk = sock->sk;
	struct alg_sock *ask = alg_sk(sk);
	struct aead_ctx *ctx = ask->private;
	unsigned int mask;

	sock_poll_wait(file, sk_sleep(sk), wait);
	mask = 0;

	if (!ctx->more)
		mask |= POLLIN | POLLRDNORM;

	if (aead_writable(sk))
		mask |= POLLOUT | POLLWRNORM | POLLWRBAND;

	return mask;
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
	.sendpage	=	aead_sendpage,
	.recvmsg	=	aead_recvmsg,
	.poll		=	aead_poll,
};

static void *aead_bind(const char *name, u32 type, u32 mask)
{
	return crypto_alloc_aead(name, type, mask);
}

static void aead_release(void *private)
{
	crypto_free_aead(private);
}

static int aead_setauthsize(void *private, unsigned int authsize)
{
	return crypto_aead_setauthsize(private, authsize);
}

static int aead_setkey(void *private, const u8 *key, unsigned int keylen)
{
	return crypto_aead_setkey(private, key, keylen);
}

static void aead_sock_destruct(struct sock *sk)
{
	struct alg_sock *ask = alg_sk(sk);
	struct aead_ctx *ctx = ask->private;
	unsigned int ivlen = crypto_aead_ivsize(
				crypto_aead_reqtfm(&ctx->aead_req));

	WARN_ON(atomic_read(&sk->sk_refcnt) != 0);
	aead_put_sgl(sk);
	sock_kzfree_s(sk, ctx->iv, ivlen);
	sock_kfree_s(sk, ctx, ctx->len);
	af_alg_release_parent(sk);
}

static int aead_accept_parent(void *private, struct sock *sk)
{
	struct aead_ctx *ctx;
	struct alg_sock *ask = alg_sk(sk);
	unsigned int len = sizeof(*ctx) + crypto_aead_reqsize(private);
	unsigned int ivlen = crypto_aead_ivsize(private);

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

	ctx->len = len;
	ctx->used = 0;
	ctx->more = 0;
	ctx->merge = 0;
	ctx->enc = 0;
	ctx->tsgl.cur = 0;
	ctx->aead_assoclen = 0;
	af_alg_init_completion(&ctx->completion);
	sg_init_table(ctx->tsgl.sg, ALG_MAX_PAGES);
	INIT_LIST_HEAD(&ctx->list);

	ask->private = ctx;

	aead_request_set_tfm(&ctx->aead_req, private);
	aead_request_set_callback(&ctx->aead_req, CRYPTO_TFM_REQ_MAY_BACKLOG,
				  af_alg_complete, &ctx->completion);

	sk->sk_destruct = aead_sock_destruct;

	return 0;
}

static const struct af_alg_type algif_type_aead = {
	.bind		=	aead_bind,
	.release	=	aead_release,
	.setkey		=	aead_setkey,
	.setauthsize	=	aead_setauthsize,
	.accept		=	aead_accept_parent,
	.ops		=	&algif_aead_ops,
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
