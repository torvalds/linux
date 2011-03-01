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
 */

#include <crypto/scatterwalk.h>
#include <crypto/skcipher.h>
#include <crypto/if_alg.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/net.h>
#include <net/sock.h>

struct skcipher_sg_list {
	struct list_head list;

	int cur;

	struct scatterlist sg[0];
};

struct skcipher_ctx {
	struct list_head tsgl;
	struct af_alg_sgl rsgl;

	void *iv;

	struct af_alg_completion completion;

	unsigned used;

	unsigned int len;
	bool more;
	bool merge;
	bool enc;

	struct ablkcipher_request req;
};

#define MAX_SGL_ENTS ((PAGE_SIZE - sizeof(struct skcipher_sg_list)) / \
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

static int skcipher_alloc_sgl(struct sock *sk)
{
	struct alg_sock *ask = alg_sk(sk);
	struct skcipher_ctx *ctx = ask->private;
	struct skcipher_sg_list *sgl;
	struct scatterlist *sg = NULL;

	sgl = list_entry(ctx->tsgl.prev, struct skcipher_sg_list, list);
	if (!list_empty(&ctx->tsgl))
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
			scatterwalk_sg_chain(sg, MAX_SGL_ENTS + 1, sgl->sg);

		list_add_tail(&sgl->list, &ctx->tsgl);
	}

	return 0;
}

static void skcipher_pull_sgl(struct sock *sk, int used)
{
	struct alg_sock *ask = alg_sk(sk);
	struct skcipher_ctx *ctx = ask->private;
	struct skcipher_sg_list *sgl;
	struct scatterlist *sg;
	int i;

	while (!list_empty(&ctx->tsgl)) {
		sgl = list_first_entry(&ctx->tsgl, struct skcipher_sg_list,
				       list);
		sg = sgl->sg;

		for (i = 0; i < sgl->cur; i++) {
			int plen = min_t(int, used, sg[i].length);

			if (!sg_page(sg + i))
				continue;

			sg[i].length -= plen;
			sg[i].offset += plen;

			used -= plen;
			ctx->used -= plen;

			if (sg[i].length)
				return;

			put_page(sg_page(sg + i));
			sg_assign_page(sg + i, NULL);
		}

		list_del(&sgl->list);
		sock_kfree_s(sk, sgl,
			     sizeof(*sgl) + sizeof(sgl->sg[0]) *
					    (MAX_SGL_ENTS + 1));
	}

	if (!ctx->used)
		ctx->merge = 0;
}

static void skcipher_free_sgl(struct sock *sk)
{
	struct alg_sock *ask = alg_sk(sk);
	struct skcipher_ctx *ctx = ask->private;

	skcipher_pull_sgl(sk, ctx->used);
}

static int skcipher_wait_for_wmem(struct sock *sk, unsigned flags)
{
	long timeout;
	DEFINE_WAIT(wait);
	int err = -ERESTARTSYS;

	if (flags & MSG_DONTWAIT)
		return -EAGAIN;

	set_bit(SOCK_ASYNC_NOSPACE, &sk->sk_socket->flags);

	for (;;) {
		if (signal_pending(current))
			break;
		prepare_to_wait(sk_sleep(sk), &wait, TASK_INTERRUPTIBLE);
		timeout = MAX_SCHEDULE_TIMEOUT;
		if (sk_wait_event(sk, &timeout, skcipher_writable(sk))) {
			err = 0;
			break;
		}
	}
	finish_wait(sk_sleep(sk), &wait);

	return err;
}

static void skcipher_wmem_wakeup(struct sock *sk)
{
	struct socket_wq *wq;

	if (!skcipher_writable(sk))
		return;

	rcu_read_lock();
	wq = rcu_dereference(sk->sk_wq);
	if (wq_has_sleeper(wq))
		wake_up_interruptible_sync_poll(&wq->wait, POLLIN |
							   POLLRDNORM |
							   POLLRDBAND);
	sk_wake_async(sk, SOCK_WAKE_WAITD, POLL_IN);
	rcu_read_unlock();
}

static int skcipher_wait_for_data(struct sock *sk, unsigned flags)
{
	struct alg_sock *ask = alg_sk(sk);
	struct skcipher_ctx *ctx = ask->private;
	long timeout;
	DEFINE_WAIT(wait);
	int err = -ERESTARTSYS;

	if (flags & MSG_DONTWAIT) {
		return -EAGAIN;
	}

	set_bit(SOCK_ASYNC_WAITDATA, &sk->sk_socket->flags);

	for (;;) {
		if (signal_pending(current))
			break;
		prepare_to_wait(sk_sleep(sk), &wait, TASK_INTERRUPTIBLE);
		timeout = MAX_SCHEDULE_TIMEOUT;
		if (sk_wait_event(sk, &timeout, ctx->used)) {
			err = 0;
			break;
		}
	}
	finish_wait(sk_sleep(sk), &wait);

	clear_bit(SOCK_ASYNC_WAITDATA, &sk->sk_socket->flags);

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
	if (wq_has_sleeper(wq))
		wake_up_interruptible_sync_poll(&wq->wait, POLLOUT |
							   POLLRDNORM |
							   POLLRDBAND);
	sk_wake_async(sk, SOCK_WAKE_SPACE, POLL_OUT);
	rcu_read_unlock();
}

static int skcipher_sendmsg(struct kiocb *unused, struct socket *sock,
			    struct msghdr *msg, size_t size)
{
	struct sock *sk = sock->sk;
	struct alg_sock *ask = alg_sk(sk);
	struct skcipher_ctx *ctx = ask->private;
	struct crypto_ablkcipher *tfm = crypto_ablkcipher_reqtfm(&ctx->req);
	unsigned ivsize = crypto_ablkcipher_ivsize(tfm);
	struct skcipher_sg_list *sgl;
	struct af_alg_control con = {};
	long copied = 0;
	bool enc = 0;
	int err;
	int i;

	if (msg->msg_controllen) {
		err = af_alg_cmsg_send(msg, &con);
		if (err)
			return err;

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

	if (!ctx->used) {
		ctx->enc = enc;
		if (con.iv)
			memcpy(ctx->iv, con.iv->iv, ivsize);
	}

	while (size) {
		struct scatterlist *sg;
		unsigned long len = size;
		int plen;

		if (ctx->merge) {
			sgl = list_entry(ctx->tsgl.prev,
					 struct skcipher_sg_list, list);
			sg = sgl->sg + sgl->cur - 1;
			len = min_t(unsigned long, len,
				    PAGE_SIZE - sg->offset - sg->length);

			err = memcpy_fromiovec(page_address(sg_page(sg)) +
					       sg->offset + sg->length,
					       msg->msg_iov, len);
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

		err = skcipher_alloc_sgl(sk);
		if (err)
			goto unlock;

		sgl = list_entry(ctx->tsgl.prev, struct skcipher_sg_list, list);
		sg = sgl->sg;
		do {
			i = sgl->cur;
			plen = min_t(int, len, PAGE_SIZE);

			sg_assign_page(sg + i, alloc_page(GFP_KERNEL));
			err = -ENOMEM;
			if (!sg_page(sg + i))
				goto unlock;

			err = memcpy_fromiovec(page_address(sg_page(sg + i)),
					       msg->msg_iov, plen);
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

		ctx->merge = plen & (PAGE_SIZE - 1);
	}

	err = 0;

	ctx->more = msg->msg_flags & MSG_MORE;
	if (!ctx->more && !list_empty(&ctx->tsgl))
		sgl = list_entry(ctx->tsgl.prev, struct skcipher_sg_list, list);

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
	struct skcipher_sg_list *sgl;
	int err = -EINVAL;

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

	err = skcipher_alloc_sgl(sk);
	if (err)
		goto unlock;

	ctx->merge = 0;
	sgl = list_entry(ctx->tsgl.prev, struct skcipher_sg_list, list);

	get_page(page);
	sg_set_page(sgl->sg + sgl->cur, page, size, offset);
	sgl->cur++;
	ctx->used += size;

done:
	ctx->more = flags & MSG_MORE;
	if (!ctx->more && !list_empty(&ctx->tsgl))
		sgl = list_entry(ctx->tsgl.prev, struct skcipher_sg_list, list);

unlock:
	skcipher_data_wakeup(sk);
	release_sock(sk);

	return err ?: size;
}

static int skcipher_recvmsg(struct kiocb *unused, struct socket *sock,
			    struct msghdr *msg, size_t ignored, int flags)
{
	struct sock *sk = sock->sk;
	struct alg_sock *ask = alg_sk(sk);
	struct skcipher_ctx *ctx = ask->private;
	unsigned bs = crypto_ablkcipher_blocksize(crypto_ablkcipher_reqtfm(
		&ctx->req));
	struct skcipher_sg_list *sgl;
	struct scatterlist *sg;
	unsigned long iovlen;
	struct iovec *iov;
	int err = -EAGAIN;
	int used;
	long copied = 0;

	lock_sock(sk);
	for (iov = msg->msg_iov, iovlen = msg->msg_iovlen; iovlen > 0;
	     iovlen--, iov++) {
		unsigned long seglen = iov->iov_len;
		char __user *from = iov->iov_base;

		while (seglen) {
			sgl = list_first_entry(&ctx->tsgl,
					       struct skcipher_sg_list, list);
			sg = sgl->sg;

			while (!sg->length)
				sg++;

			used = ctx->used;
			if (!used) {
				err = skcipher_wait_for_data(sk, flags);
				if (err)
					goto unlock;
			}

			used = min_t(unsigned long, used, seglen);

			used = af_alg_make_sg(&ctx->rsgl, from, used, 1);
			err = used;
			if (err < 0)
				goto unlock;

			if (ctx->more || used < ctx->used)
				used -= used % bs;

			err = -EINVAL;
			if (!used)
				goto free;

			ablkcipher_request_set_crypt(&ctx->req, sg,
						     ctx->rsgl.sg, used,
						     ctx->iv);

			err = af_alg_wait_for_completion(
				ctx->enc ?
					crypto_ablkcipher_encrypt(&ctx->req) :
					crypto_ablkcipher_decrypt(&ctx->req),
				&ctx->completion);

free:
			af_alg_free_sg(&ctx->rsgl);

			if (err)
				goto unlock;

			copied += used;
			from += used;
			seglen -= used;
			skcipher_pull_sgl(sk, used);
		}
	}

	err = 0;

unlock:
	skcipher_wmem_wakeup(sk);
	release_sock(sk);

	return copied ?: err;
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

static void *skcipher_bind(const char *name, u32 type, u32 mask)
{
	return crypto_alloc_ablkcipher(name, type, mask);
}

static void skcipher_release(void *private)
{
	crypto_free_ablkcipher(private);
}

static int skcipher_setkey(void *private, const u8 *key, unsigned int keylen)
{
	return crypto_ablkcipher_setkey(private, key, keylen);
}

static void skcipher_sock_destruct(struct sock *sk)
{
	struct alg_sock *ask = alg_sk(sk);
	struct skcipher_ctx *ctx = ask->private;
	struct crypto_ablkcipher *tfm = crypto_ablkcipher_reqtfm(&ctx->req);

	skcipher_free_sgl(sk);
	sock_kfree_s(sk, ctx->iv, crypto_ablkcipher_ivsize(tfm));
	sock_kfree_s(sk, ctx, ctx->len);
	af_alg_release_parent(sk);
}

static int skcipher_accept_parent(void *private, struct sock *sk)
{
	struct skcipher_ctx *ctx;
	struct alg_sock *ask = alg_sk(sk);
	unsigned int len = sizeof(*ctx) + crypto_ablkcipher_reqsize(private);

	ctx = sock_kmalloc(sk, len, GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->iv = sock_kmalloc(sk, crypto_ablkcipher_ivsize(private),
			       GFP_KERNEL);
	if (!ctx->iv) {
		sock_kfree_s(sk, ctx, len);
		return -ENOMEM;
	}

	memset(ctx->iv, 0, crypto_ablkcipher_ivsize(private));

	INIT_LIST_HEAD(&ctx->tsgl);
	ctx->len = len;
	ctx->used = 0;
	ctx->more = 0;
	ctx->merge = 0;
	ctx->enc = 0;
	af_alg_init_completion(&ctx->completion);

	ask->private = ctx;

	ablkcipher_request_set_tfm(&ctx->req, private);
	ablkcipher_request_set_callback(&ctx->req, CRYPTO_TFM_REQ_MAY_BACKLOG,
					af_alg_complete, &ctx->completion);

	sk->sk_destruct = skcipher_sock_destruct;

	return 0;
}

static const struct af_alg_type algif_type_skcipher = {
	.bind		=	skcipher_bind,
	.release	=	skcipher_release,
	.setkey		=	skcipher_setkey,
	.accept		=	skcipher_accept_parent,
	.ops		=	&algif_skcipher_ops,
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
