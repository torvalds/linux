/*
 * algif_skcipher: User-space interface for skcipher algorithms
 *
 * This file provides the user-space API for symmetric key ciphers.
 *
 * Copyright (c) 2010 Herbert Xu <herbert@gondor.apana.org.au>
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

#include <crypto/scatterwalk.h>
#include <crypto/skcipher.h>
#include <crypto/if_alg.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/kernel.h>
#include <linux/sched/signal.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/net.h>
#include <net/sock.h>

struct skcipher_tsgl {
	struct list_head list;
	int cur;
	struct scatterlist sg[0];
};

struct skcipher_rsgl {
	struct af_alg_sgl sgl;
	struct list_head list;
	size_t sg_num_bytes;
};

struct skcipher_async_req {
	struct kiocb *iocb;
	struct sock *sk;

	struct skcipher_rsgl first_sgl;
	struct list_head rsgl_list;

	struct scatterlist *tsgl;
	unsigned int tsgl_entries;

	unsigned int areqlen;
	struct skcipher_request req;
};

struct skcipher_tfm {
	struct crypto_skcipher *skcipher;
	bool has_key;
};

struct skcipher_ctx {
	struct list_head tsgl_list;

	void *iv;

	struct af_alg_completion completion;

	size_t used;
	size_t rcvused;

	bool more;
	bool merge;
	bool enc;

	unsigned int len;
};

#define MAX_SGL_ENTS ((4096 - sizeof(struct skcipher_tsgl)) / \
		      sizeof(struct scatterlist) - 1)

static inline int skcipher_sndbuf(struct sock *sk)
{
	struct alg_sock *ask = alg_sk(sk);
	struct skcipher_ctx *ctx = ask->private;

	return max_t(int, max_t(int, sk->sk_sndbuf & PAGE_MASK, PAGE_SIZE) -
			  ctx->used, 0);
}

static inline bool skcipher_writable(struct sock *sk)
{
	return PAGE_SIZE <= skcipher_sndbuf(sk);
}

static inline int skcipher_rcvbuf(struct sock *sk)
{
	struct alg_sock *ask = alg_sk(sk);
	struct skcipher_ctx *ctx = ask->private;

	return max_t(int, max_t(int, sk->sk_rcvbuf & PAGE_MASK, PAGE_SIZE) -
			  ctx->rcvused, 0);
}

static inline bool skcipher_readable(struct sock *sk)
{
	return PAGE_SIZE <= skcipher_rcvbuf(sk);
}

static int skcipher_alloc_tsgl(struct sock *sk)
{
	struct alg_sock *ask = alg_sk(sk);
	struct skcipher_ctx *ctx = ask->private;
	struct skcipher_tsgl *sgl;
	struct scatterlist *sg = NULL;

	sgl = list_entry(ctx->tsgl_list.prev, struct skcipher_tsgl, list);
	if (!list_empty(&ctx->tsgl_list))
		sg = sgl->sg;

	if (!sg || sgl->cur >= MAX_SGL_ENTS) {
		sgl = sock_kmalloc(sk, sizeof(*sgl) +
				       sizeof(sgl->sg[0]) * (MAX_SGL_ENTS + 1),
				   GFP_KERNEL);
		if (!sgl)
			return -ENOMEM;

		sg_init_table(sgl->sg, MAX_SGL_ENTS + 1);
		sgl->cur = 0;

		if (sg)
			sg_chain(sg, MAX_SGL_ENTS + 1, sgl->sg);

		list_add_tail(&sgl->list, &ctx->tsgl_list);
	}

	return 0;
}

static unsigned int skcipher_count_tsgl(struct sock *sk, size_t bytes)
{
	struct alg_sock *ask = alg_sk(sk);
	struct skcipher_ctx *ctx = ask->private;
	struct skcipher_tsgl *sgl, *tmp;
	unsigned int i;
	unsigned int sgl_count = 0;

	if (!bytes)
		return 0;

	list_for_each_entry_safe(sgl, tmp, &ctx->tsgl_list, list) {
		struct scatterlist *sg = sgl->sg;

		for (i = 0; i < sgl->cur; i++) {
			sgl_count++;
			if (sg[i].length >= bytes)
				return sgl_count;

			bytes -= sg[i].length;
		}
	}

	return sgl_count;
}

static void skcipher_pull_tsgl(struct sock *sk, size_t used,
			       struct scatterlist *dst)
{
	struct alg_sock *ask = alg_sk(sk);
	struct skcipher_ctx *ctx = ask->private;
	struct skcipher_tsgl *sgl;
	struct scatterlist *sg;
	unsigned int i;

	while (!list_empty(&ctx->tsgl_list)) {
		sgl = list_first_entry(&ctx->tsgl_list, struct skcipher_tsgl,
				       list);
		sg = sgl->sg;

		for (i = 0; i < sgl->cur; i++) {
			size_t plen = min_t(size_t, used, sg[i].length);
			struct page *page = sg_page(sg + i);

			if (!page)
				continue;

			/*
			 * Assumption: caller created skcipher_count_tsgl(len)
			 * SG entries in dst.
			 */
			if (dst)
				sg_set_page(dst + i, page, plen, sg[i].offset);

			sg[i].length -= plen;
			sg[i].offset += plen;

			used -= plen;
			ctx->used -= plen;

			if (sg[i].length)
				return;

			if (!dst)
				put_page(page);
			sg_assign_page(sg + i, NULL);
		}

		list_del(&sgl->list);
		sock_kfree_s(sk, sgl, sizeof(*sgl) + sizeof(sgl->sg[0]) *
						     (MAX_SGL_ENTS + 1));
	}

	if (!ctx->used)
		ctx->merge = 0;
}

static void skcipher_free_areq_sgls(struct skcipher_async_req *areq)
{
	struct sock *sk = areq->sk;
	struct alg_sock *ask = alg_sk(sk);
	struct skcipher_ctx *ctx = ask->private;
	struct skcipher_rsgl *rsgl, *tmp;
	struct scatterlist *tsgl;
	struct scatterlist *sg;
	unsigned int i;

	list_for_each_entry_safe(rsgl, tmp, &areq->rsgl_list, list) {
		ctx->rcvused -= rsgl->sg_num_bytes;
		af_alg_free_sg(&rsgl->sgl);
		list_del(&rsgl->list);
		if (rsgl != &areq->first_sgl)
			sock_kfree_s(sk, rsgl, sizeof(*rsgl));
	}

	tsgl = areq->tsgl;
	for_each_sg(tsgl, sg, areq->tsgl_entries, i) {
		if (!sg_page(sg))
			continue;
		put_page(sg_page(sg));
	}

	if (areq->tsgl && areq->tsgl_entries)
		sock_kfree_s(sk, tsgl, areq->tsgl_entries * sizeof(*tsgl));
}

static int skcipher_wait_for_wmem(struct sock *sk, unsigned flags)
{
	DEFINE_WAIT_FUNC(wait, woken_wake_function);
	int err = -ERESTARTSYS;
	long timeout;

	if (flags & MSG_DONTWAIT)
		return -EAGAIN;

	sk_set_bit(SOCKWQ_ASYNC_NOSPACE, sk);

	add_wait_queue(sk_sleep(sk), &wait);
	for (;;) {
		if (signal_pending(current))
			break;
		timeout = MAX_SCHEDULE_TIMEOUT;
		if (sk_wait_event(sk, &timeout, skcipher_writable(sk), &wait)) {
			err = 0;
			break;
		}
	}
	remove_wait_queue(sk_sleep(sk), &wait);

	return err;
}

static void skcipher_wmem_wakeup(struct sock *sk)
{
	struct socket_wq *wq;

	if (!skcipher_writable(sk))
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

static int skcipher_wait_for_data(struct sock *sk, unsigned flags)
{
	DEFINE_WAIT_FUNC(wait, woken_wake_function);
	struct alg_sock *ask = alg_sk(sk);
	struct skcipher_ctx *ctx = ask->private;
	long timeout;
	int err = -ERESTARTSYS;

	if (flags & MSG_DONTWAIT) {
		return -EAGAIN;
	}

	sk_set_bit(SOCKWQ_ASYNC_WAITDATA, sk);

	add_wait_queue(sk_sleep(sk), &wait);
	for (;;) {
		if (signal_pending(current))
			break;
		timeout = MAX_SCHEDULE_TIMEOUT;
		if (sk_wait_event(sk, &timeout, ctx->used, &wait)) {
			err = 0;
			break;
		}
	}
	remove_wait_queue(sk_sleep(sk), &wait);

	sk_clear_bit(SOCKWQ_ASYNC_WAITDATA, sk);

	return err;
}

static void skcipher_data_wakeup(struct sock *sk)
{
	struct alg_sock *ask = alg_sk(sk);
	struct skcipher_ctx *ctx = ask->private;
	struct socket_wq *wq;

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

static int skcipher_sendmsg(struct socket *sock, struct msghdr *msg,
			    size_t size)
{
	struct sock *sk = sock->sk;
	struct alg_sock *ask = alg_sk(sk);
	struct sock *psk = ask->parent;
	struct alg_sock *pask = alg_sk(psk);
	struct skcipher_ctx *ctx = ask->private;
	struct skcipher_tfm *skc = pask->private;
	struct crypto_skcipher *tfm = skc->skcipher;
	unsigned ivsize = crypto_skcipher_ivsize(tfm);
	struct skcipher_tsgl *sgl;
	struct af_alg_control con = {};
	long copied = 0;
	bool enc = 0;
	bool init = 0;
	int err;
	int i;

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

	err = -EINVAL;

	lock_sock(sk);
	if (!ctx->more && ctx->used)
		goto unlock;

	if (init) {
		ctx->enc = enc;
		if (con.iv)
			memcpy(ctx->iv, con.iv->iv, ivsize);
	}

	while (size) {
		struct scatterlist *sg;
		unsigned long len = size;
		size_t plen;

		if (ctx->merge) {
			sgl = list_entry(ctx->tsgl_list.prev,
					 struct skcipher_tsgl, list);
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

		if (!skcipher_writable(sk)) {
			err = skcipher_wait_for_wmem(sk, msg->msg_flags);
			if (err)
				goto unlock;
		}

		len = min_t(unsigned long, len, skcipher_sndbuf(sk));

		err = skcipher_alloc_tsgl(sk);
		if (err)
			goto unlock;

		sgl = list_entry(ctx->tsgl_list.prev, struct skcipher_tsgl,
				 list);
		sg = sgl->sg;
		if (sgl->cur)
			sg_unmark_end(sg + sgl->cur - 1);
		do {
			i = sgl->cur;
			plen = min_t(size_t, len, PAGE_SIZE);

			sg_assign_page(sg + i, alloc_page(GFP_KERNEL));
			err = -ENOMEM;
			if (!sg_page(sg + i))
				goto unlock;

			err = memcpy_from_msg(page_address(sg_page(sg + i)),
					      msg, plen);
			if (err) {
				__free_page(sg_page(sg + i));
				sg_assign_page(sg + i, NULL);
				goto unlock;
			}

			sg[i].length = plen;
			len -= plen;
			ctx->used += plen;
			copied += plen;
			size -= plen;
			sgl->cur++;
		} while (len && sgl->cur < MAX_SGL_ENTS);

		if (!size)
			sg_mark_end(sg + sgl->cur - 1);

		ctx->merge = plen & (PAGE_SIZE - 1);
	}

	err = 0;

	ctx->more = msg->msg_flags & MSG_MORE;

unlock:
	skcipher_data_wakeup(sk);
	release_sock(sk);

	return copied ?: err;
}

static ssize_t skcipher_sendpage(struct socket *sock, struct page *page,
				 int offset, size_t size, int flags)
{
	struct sock *sk = sock->sk;
	struct alg_sock *ask = alg_sk(sk);
	struct skcipher_ctx *ctx = ask->private;
	struct skcipher_tsgl *sgl;
	int err = -EINVAL;

	if (flags & MSG_SENDPAGE_NOTLAST)
		flags |= MSG_MORE;

	lock_sock(sk);
	if (!ctx->more && ctx->used)
		goto unlock;

	if (!size)
		goto done;

	if (!skcipher_writable(sk)) {
		err = skcipher_wait_for_wmem(sk, flags);
		if (err)
			goto unlock;
	}

	err = skcipher_alloc_tsgl(sk);
	if (err)
		goto unlock;

	ctx->merge = 0;
	sgl = list_entry(ctx->tsgl_list.prev, struct skcipher_tsgl, list);

	if (sgl->cur)
		sg_unmark_end(sgl->sg + sgl->cur - 1);

	sg_mark_end(sgl->sg + sgl->cur);
	get_page(page);
	sg_set_page(sgl->sg + sgl->cur, page, size, offset);
	sgl->cur++;
	ctx->used += size;

done:
	ctx->more = flags & MSG_MORE;

unlock:
	skcipher_data_wakeup(sk);
	release_sock(sk);

	return err ?: size;
}

static void skcipher_async_cb(struct crypto_async_request *req, int err)
{
	struct skcipher_async_req *areq = req->data;
	struct sock *sk = areq->sk;
	struct kiocb *iocb = areq->iocb;
	unsigned int resultlen;

	lock_sock(sk);

	/* Buffer size written by crypto operation. */
	resultlen = areq->req.cryptlen;

	skcipher_free_areq_sgls(areq);
	sock_kfree_s(sk, areq, areq->areqlen);
	__sock_put(sk);

	iocb->ki_complete(iocb, err ? err : resultlen, 0);

	release_sock(sk);
}

static int _skcipher_recvmsg(struct socket *sock, struct msghdr *msg,
			     size_t ignored, int flags)
{
	struct sock *sk = sock->sk;
	struct alg_sock *ask = alg_sk(sk);
	struct sock *psk = ask->parent;
	struct alg_sock *pask = alg_sk(psk);
	struct skcipher_ctx *ctx = ask->private;
	struct skcipher_tfm *skc = pask->private;
	struct crypto_skcipher *tfm = skc->skcipher;
	unsigned int bs = crypto_skcipher_blocksize(tfm);
	unsigned int areqlen = sizeof(struct skcipher_async_req) +
			       crypto_skcipher_reqsize(tfm);
	struct skcipher_async_req *areq;
	struct skcipher_rsgl *last_rsgl = NULL;
	int err = 0;
	size_t len = 0;

	/* Allocate cipher request for current operation. */
	areq = sock_kmalloc(sk, areqlen, GFP_KERNEL);
	if (unlikely(!areq))
		return -ENOMEM;
	areq->areqlen = areqlen;
	areq->sk = sk;
	INIT_LIST_HEAD(&areq->rsgl_list);
	areq->tsgl = NULL;
	areq->tsgl_entries = 0;

	/* convert iovecs of output buffers into RX SGL */
	while (msg_data_left(msg)) {
		struct skcipher_rsgl *rsgl;
		size_t seglen;

		/* limit the amount of readable buffers */
		if (!skcipher_readable(sk))
			break;

		if (!ctx->used) {
			err = skcipher_wait_for_data(sk, flags);
			if (err)
				goto free;
		}

		seglen = min_t(size_t, ctx->used, msg_data_left(msg));

		if (list_empty(&areq->rsgl_list)) {
			rsgl = &areq->first_sgl;
		} else {
			rsgl = sock_kmalloc(sk, sizeof(*rsgl), GFP_KERNEL);
			if (!rsgl) {
				err = -ENOMEM;
				goto free;
			}
		}

		rsgl->sgl.npages = 0;
		list_add_tail(&rsgl->list, &areq->rsgl_list);

		/* make one iovec available as scatterlist */
		err = af_alg_make_sg(&rsgl->sgl, &msg->msg_iter, seglen);
		if (err < 0)
			goto free;

		/* chain the new scatterlist with previous one */
		if (last_rsgl)
			af_alg_link_sg(&last_rsgl->sgl, &rsgl->sgl);

		last_rsgl = rsgl;
		len += err;
		ctx->rcvused += err;
		rsgl->sg_num_bytes = err;
		iov_iter_advance(&msg->msg_iter, err);
	}

	/* Process only as much RX buffers for which we have TX data */
	if (len > ctx->used)
		len = ctx->used;

	/*
	 * If more buffers are to be expected to be processed, process only
	 * full block size buffers.
	 */
	if (ctx->more || len < ctx->used)
		len -= len % bs;

	/*
	 * Create a per request TX SGL for this request which tracks the
	 * SG entries from the global TX SGL.
	 */
	areq->tsgl_entries = skcipher_count_tsgl(sk, len);
	if (!areq->tsgl_entries)
		areq->tsgl_entries = 1;
	areq->tsgl = sock_kmalloc(sk, sizeof(*areq->tsgl) * areq->tsgl_entries,
				  GFP_KERNEL);
	if (!areq->tsgl) {
		err = -ENOMEM;
		goto free;
	}
	sg_init_table(areq->tsgl, areq->tsgl_entries);
	skcipher_pull_tsgl(sk, len, areq->tsgl);

	/* Initialize the crypto operation */
	skcipher_request_set_tfm(&areq->req, tfm);
	skcipher_request_set_crypt(&areq->req, areq->tsgl,
				   areq->first_sgl.sgl.sg, len, ctx->iv);

	if (msg->msg_iocb && !is_sync_kiocb(msg->msg_iocb)) {
		/* AIO operation */
		areq->iocb = msg->msg_iocb;
		skcipher_request_set_callback(&areq->req,
					      CRYPTO_TFM_REQ_MAY_SLEEP,
					      skcipher_async_cb, areq);
		err = ctx->enc ? crypto_skcipher_encrypt(&areq->req) :
				 crypto_skcipher_decrypt(&areq->req);
	} else {
		/* Synchronous operation */
		skcipher_request_set_callback(&areq->req,
					      CRYPTO_TFM_REQ_MAY_SLEEP |
					      CRYPTO_TFM_REQ_MAY_BACKLOG,
					      af_alg_complete,
					      &ctx->completion);
		err = af_alg_wait_for_completion(ctx->enc ?
					crypto_skcipher_encrypt(&areq->req) :
					crypto_skcipher_decrypt(&areq->req),
						 &ctx->completion);
	}

	/* AIO operation in progress */
	if (err == -EINPROGRESS) {
		sock_hold(sk);
		return -EIOCBQUEUED;
	}

free:
	skcipher_free_areq_sgls(areq);
	if (areq)
		sock_kfree_s(sk, areq, areqlen);

	return err ? err : len;
}

static int skcipher_recvmsg(struct socket *sock, struct msghdr *msg,
			    size_t ignored, int flags)
{
	struct sock *sk = sock->sk;
	int ret = 0;

	lock_sock(sk);
	while (msg_data_left(msg)) {
		int err = _skcipher_recvmsg(sock, msg, ignored, flags);

		/*
		 * This error covers -EIOCBQUEUED which implies that we can
		 * only handle one AIO request. If the caller wants to have
		 * multiple AIO requests in parallel, he must make multiple
		 * separate AIO calls.
		 */
		if (err <= 0) {
			if (err == -EIOCBQUEUED)
				ret = err;
			goto out;
		}

		ret += err;
	}

out:
	skcipher_wmem_wakeup(sk);
	release_sock(sk);
	return ret;
}

static unsigned int skcipher_poll(struct file *file, struct socket *sock,
				  poll_table *wait)
{
	struct sock *sk = sock->sk;
	struct alg_sock *ask = alg_sk(sk);
	struct skcipher_ctx *ctx = ask->private;
	unsigned int mask;

	sock_poll_wait(file, sk_sleep(sk), wait);
	mask = 0;

	if (ctx->used)
		mask |= POLLIN | POLLRDNORM;

	if (skcipher_writable(sk))
		mask |= POLLOUT | POLLWRNORM | POLLWRBAND;

	return mask;
}

static struct proto_ops algif_skcipher_ops = {
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
	.sendmsg	=	skcipher_sendmsg,
	.sendpage	=	skcipher_sendpage,
	.recvmsg	=	skcipher_recvmsg,
	.poll		=	skcipher_poll,
};

static int skcipher_check_key(struct socket *sock)
{
	int err = 0;
	struct sock *psk;
	struct alg_sock *pask;
	struct skcipher_tfm *tfm;
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

static int skcipher_sendmsg_nokey(struct socket *sock, struct msghdr *msg,
				  size_t size)
{
	int err;

	err = skcipher_check_key(sock);
	if (err)
		return err;

	return skcipher_sendmsg(sock, msg, size);
}

static ssize_t skcipher_sendpage_nokey(struct socket *sock, struct page *page,
				       int offset, size_t size, int flags)
{
	int err;

	err = skcipher_check_key(sock);
	if (err)
		return err;

	return skcipher_sendpage(sock, page, offset, size, flags);
}

static int skcipher_recvmsg_nokey(struct socket *sock, struct msghdr *msg,
				  size_t ignored, int flags)
{
	int err;

	err = skcipher_check_key(sock);
	if (err)
		return err;

	return skcipher_recvmsg(sock, msg, ignored, flags);
}

static struct proto_ops algif_skcipher_ops_nokey = {
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
	.sendmsg	=	skcipher_sendmsg_nokey,
	.sendpage	=	skcipher_sendpage_nokey,
	.recvmsg	=	skcipher_recvmsg_nokey,
	.poll		=	skcipher_poll,
};

static void *skcipher_bind(const char *name, u32 type, u32 mask)
{
	struct skcipher_tfm *tfm;
	struct crypto_skcipher *skcipher;

	tfm = kzalloc(sizeof(*tfm), GFP_KERNEL);
	if (!tfm)
		return ERR_PTR(-ENOMEM);

	skcipher = crypto_alloc_skcipher(name, type, mask);
	if (IS_ERR(skcipher)) {
		kfree(tfm);
		return ERR_CAST(skcipher);
	}

	tfm->skcipher = skcipher;

	return tfm;
}

static void skcipher_release(void *private)
{
	struct skcipher_tfm *tfm = private;

	crypto_free_skcipher(tfm->skcipher);
	kfree(tfm);
}

static int skcipher_setkey(void *private, const u8 *key, unsigned int keylen)
{
	struct skcipher_tfm *tfm = private;
	int err;

	err = crypto_skcipher_setkey(tfm->skcipher, key, keylen);
	tfm->has_key = !err;

	return err;
}

static void skcipher_sock_destruct(struct sock *sk)
{
	struct alg_sock *ask = alg_sk(sk);
	struct skcipher_ctx *ctx = ask->private;
	struct sock *psk = ask->parent;
	struct alg_sock *pask = alg_sk(psk);
	struct skcipher_tfm *skc = pask->private;
	struct crypto_skcipher *tfm = skc->skcipher;

	skcipher_pull_tsgl(sk, ctx->used, NULL);
	sock_kzfree_s(sk, ctx->iv, crypto_skcipher_ivsize(tfm));
	sock_kfree_s(sk, ctx, ctx->len);
	af_alg_release_parent(sk);
}

static int skcipher_accept_parent_nokey(void *private, struct sock *sk)
{
	struct skcipher_ctx *ctx;
	struct alg_sock *ask = alg_sk(sk);
	struct skcipher_tfm *tfm = private;
	struct crypto_skcipher *skcipher = tfm->skcipher;
	unsigned int len = sizeof(*ctx);

	ctx = sock_kmalloc(sk, len, GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->iv = sock_kmalloc(sk, crypto_skcipher_ivsize(skcipher),
			       GFP_KERNEL);
	if (!ctx->iv) {
		sock_kfree_s(sk, ctx, len);
		return -ENOMEM;
	}

	memset(ctx->iv, 0, crypto_skcipher_ivsize(skcipher));

	INIT_LIST_HEAD(&ctx->tsgl_list);
	ctx->len = len;
	ctx->used = 0;
	ctx->rcvused = 0;
	ctx->more = 0;
	ctx->merge = 0;
	ctx->enc = 0;
	af_alg_init_completion(&ctx->completion);

	ask->private = ctx;

	sk->sk_destruct = skcipher_sock_destruct;

	return 0;
}

static int skcipher_accept_parent(void *private, struct sock *sk)
{
	struct skcipher_tfm *tfm = private;

	if (!tfm->has_key && crypto_skcipher_has_setkey(tfm->skcipher))
		return -ENOKEY;

	return skcipher_accept_parent_nokey(private, sk);
}

static const struct af_alg_type algif_type_skcipher = {
	.bind		=	skcipher_bind,
	.release	=	skcipher_release,
	.setkey		=	skcipher_setkey,
	.accept		=	skcipher_accept_parent,
	.accept_nokey	=	skcipher_accept_parent_nokey,
	.ops		=	&algif_skcipher_ops,
	.ops_nokey	=	&algif_skcipher_ops_nokey,
	.name		=	"skcipher",
	.owner		=	THIS_MODULE
};

static int __init algif_skcipher_init(void)
{
	return af_alg_register_type(&algif_type_skcipher);
}

static void __exit algif_skcipher_exit(void)
{
	int err = af_alg_unregister_type(&algif_type_skcipher);
	BUG_ON(err);
}

module_init(algif_skcipher_init);
module_exit(algif_skcipher_exit);
MODULE_LICENSE("GPL");
