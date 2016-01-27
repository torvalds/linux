/*
 * algif_hash: User-space interface for hash algorithms
 *
 * This file provides the user-space API for hash algorithms.
 *
 * Copyright (c) 2010 Herbert Xu <herbert@gondor.apana.org.au>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 */

#include <crypto/hash.h>
#include <crypto/if_alg.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/net.h>
#include <net/sock.h>

struct hash_ctx {
	struct af_alg_sgl sgl;

	u8 *result;

	struct af_alg_completion completion;

	unsigned int len;
	bool more;

	struct ahash_request req;
};

static int hash_sendmsg(struct kiocb *unused, struct socket *sock,
			struct msghdr *msg, size_t ignored)
{
	int limit = ALG_MAX_PAGES * PAGE_SIZE;
	struct sock *sk = sock->sk;
	struct alg_sock *ask = alg_sk(sk);
	struct hash_ctx *ctx = ask->private;
	unsigned long iovlen;
	struct iovec *iov;
	long copied = 0;
	int err;

	if (limit > sk->sk_sndbuf)
		limit = sk->sk_sndbuf;

	lock_sock(sk);
	if (!ctx->more) {
		err = af_alg_wait_for_completion(crypto_ahash_init(&ctx->req),
						&ctx->completion);
		if (err)
			goto unlock;
	}

	ctx->more = 0;

	for (iov = msg->msg_iov, iovlen = msg->msg_iovlen; iovlen > 0;
	     iovlen--, iov++) {
		unsigned long seglen = iov->iov_len;
		char __user *from = iov->iov_base;

		while (seglen) {
			int len = min_t(unsigned long, seglen, limit);
			int newlen;

			newlen = af_alg_make_sg(&ctx->sgl, from, len, 0);
			if (newlen < 0) {
				err = copied ? 0 : newlen;
				goto unlock;
			}

			ahash_request_set_crypt(&ctx->req, ctx->sgl.sg, NULL,
						newlen);

			err = af_alg_wait_for_completion(
				crypto_ahash_update(&ctx->req),
				&ctx->completion);

			af_alg_free_sg(&ctx->sgl);

			if (err)
				goto unlock;

			seglen -= newlen;
			from += newlen;
			copied += newlen;
		}
	}

	err = 0;

	ctx->more = msg->msg_flags & MSG_MORE;
	if (!ctx->more) {
		ahash_request_set_crypt(&ctx->req, NULL, ctx->result, 0);
		err = af_alg_wait_for_completion(crypto_ahash_final(&ctx->req),
						 &ctx->completion);
	}

unlock:
	release_sock(sk);

	return err ?: copied;
}

static ssize_t hash_sendpage(struct socket *sock, struct page *page,
			     int offset, size_t size, int flags)
{
	struct sock *sk = sock->sk;
	struct alg_sock *ask = alg_sk(sk);
	struct hash_ctx *ctx = ask->private;
	int err;

	if (flags & MSG_SENDPAGE_NOTLAST)
		flags |= MSG_MORE;

	lock_sock(sk);
	sg_init_table(ctx->sgl.sg, 1);
	sg_set_page(ctx->sgl.sg, page, size, offset);

	ahash_request_set_crypt(&ctx->req, ctx->sgl.sg, ctx->result, size);

	if (!(flags & MSG_MORE)) {
		if (ctx->more)
			err = crypto_ahash_finup(&ctx->req);
		else
			err = crypto_ahash_digest(&ctx->req);
	} else {
		if (!ctx->more) {
			err = crypto_ahash_init(&ctx->req);
			err = af_alg_wait_for_completion(err, &ctx->completion);
			if (err)
				goto unlock;
		}

		err = crypto_ahash_update(&ctx->req);
	}

	err = af_alg_wait_for_completion(err, &ctx->completion);
	if (err)
		goto unlock;

	ctx->more = flags & MSG_MORE;

unlock:
	release_sock(sk);

	return err ?: size;
}

static int hash_recvmsg(struct kiocb *unused, struct socket *sock,
			struct msghdr *msg, size_t len, int flags)
{
	struct sock *sk = sock->sk;
	struct alg_sock *ask = alg_sk(sk);
	struct hash_ctx *ctx = ask->private;
	unsigned ds = crypto_ahash_digestsize(crypto_ahash_reqtfm(&ctx->req));
	int err;

	if (len > ds)
		len = ds;
	else if (len < ds)
		msg->msg_flags |= MSG_TRUNC;

	lock_sock(sk);
	if (ctx->more) {
		ctx->more = 0;
		ahash_request_set_crypt(&ctx->req, NULL, ctx->result, 0);
		err = af_alg_wait_for_completion(crypto_ahash_final(&ctx->req),
						 &ctx->completion);
		if (err)
			goto unlock;
	}

	err = memcpy_toiovec(msg->msg_iov, ctx->result, len);

unlock:
	release_sock(sk);

	return err ?: len;
}

static int hash_accept(struct socket *sock, struct socket *newsock, int flags)
{
	struct sock *sk = sock->sk;
	struct alg_sock *ask = alg_sk(sk);
	struct hash_ctx *ctx = ask->private;
	struct ahash_request *req = &ctx->req;
	char state[crypto_ahash_statesize(crypto_ahash_reqtfm(req))];
	struct sock *sk2;
	struct alg_sock *ask2;
	struct hash_ctx *ctx2;
	bool more;
	int err;

	lock_sock(sk);
	more = ctx->more;
	err = more ? crypto_ahash_export(req, state) : 0;
	release_sock(sk);

	if (err)
		return err;

	err = af_alg_accept(ask->parent, newsock);
	if (err)
		return err;

	sk2 = newsock->sk;
	ask2 = alg_sk(sk2);
	ctx2 = ask2->private;
	ctx2->more = more;

	if (!more)
		return err;

	err = crypto_ahash_import(&ctx2->req, state);
	if (err) {
		sock_orphan(sk2);
		sock_put(sk2);
	}

	return err;
}

static struct proto_ops algif_hash_ops = {
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
	.setsockopt	=	sock_no_setsockopt,
	.poll		=	sock_no_poll,

	.release	=	af_alg_release,
	.sendmsg	=	hash_sendmsg,
	.sendpage	=	hash_sendpage,
	.recvmsg	=	hash_recvmsg,
	.accept		=	hash_accept,
};

static void *hash_bind(const char *name, u32 type, u32 mask)
{
	return crypto_alloc_ahash(name, type, mask);
}

static void hash_release(void *private)
{
	crypto_free_ahash(private);
}

static int hash_setkey(void *private, const u8 *key, unsigned int keylen)
{
	return crypto_ahash_setkey(private, key, keylen);
}

static void hash_sock_destruct(struct sock *sk)
{
	struct alg_sock *ask = alg_sk(sk);
	struct hash_ctx *ctx = ask->private;

	sock_kfree_s(sk, ctx->result,
		     crypto_ahash_digestsize(crypto_ahash_reqtfm(&ctx->req)));
	sock_kfree_s(sk, ctx, ctx->len);
	af_alg_release_parent(sk);
}

static int hash_accept_parent(void *private, struct sock *sk)
{
	struct hash_ctx *ctx;
	struct alg_sock *ask = alg_sk(sk);
	unsigned len = sizeof(*ctx) + crypto_ahash_reqsize(private);
	unsigned ds = crypto_ahash_digestsize(private);

	ctx = sock_kmalloc(sk, len, GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->result = sock_kmalloc(sk, ds, GFP_KERNEL);
	if (!ctx->result) {
		sock_kfree_s(sk, ctx, len);
		return -ENOMEM;
	}

	memset(ctx->result, 0, ds);

	ctx->len = len;
	ctx->more = 0;
	af_alg_init_completion(&ctx->completion);

	ask->private = ctx;

	ahash_request_set_tfm(&ctx->req, private);
	ahash_request_set_callback(&ctx->req, CRYPTO_TFM_REQ_MAY_BACKLOG,
				   af_alg_complete, &ctx->completion);

	sk->sk_destruct = hash_sock_destruct;

	return 0;
}

static const struct af_alg_type algif_type_hash = {
	.bind		=	hash_bind,
	.release	=	hash_release,
	.setkey		=	hash_setkey,
	.accept		=	hash_accept_parent,
	.ops		=	&algif_hash_ops,
	.name		=	"hash",
	.owner		=	THIS_MODULE
};

static int __init algif_hash_init(void)
{
	return af_alg_register_type(&algif_type_hash);
}

static void __exit algif_hash_exit(void)
{
	int err = af_alg_unregister_type(&algif_type_hash);
	BUG_ON(err);
}

module_init(algif_hash_init);
module_exit(algif_hash_exit);
MODULE_LICENSE("GPL");
