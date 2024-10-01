// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * algif_hash: User-space interface for hash algorithms
 *
 * This file provides the user-space API for hash algorithms.
 *
 * Copyright (c) 2010 Herbert Xu <herbert@gondor.apana.org.au>
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

	struct crypto_wait wait;

	unsigned int len;
	bool more;

	struct ahash_request req;
};

static int hash_alloc_result(struct sock *sk, struct hash_ctx *ctx)
{
	unsigned ds;

	if (ctx->result)
		return 0;

	ds = crypto_ahash_digestsize(crypto_ahash_reqtfm(&ctx->req));

	ctx->result = sock_kmalloc(sk, ds, GFP_KERNEL);
	if (!ctx->result)
		return -ENOMEM;

	memset(ctx->result, 0, ds);

	return 0;
}

static void hash_free_result(struct sock *sk, struct hash_ctx *ctx)
{
	unsigned ds;

	if (!ctx->result)
		return;

	ds = crypto_ahash_digestsize(crypto_ahash_reqtfm(&ctx->req));

	sock_kzfree_s(sk, ctx->result, ds);
	ctx->result = NULL;
}

static int hash_sendmsg(struct socket *sock, struct msghdr *msg,
			size_t ignored)
{
	struct sock *sk = sock->sk;
	struct alg_sock *ask = alg_sk(sk);
	struct hash_ctx *ctx = ask->private;
	ssize_t copied = 0;
	size_t len, max_pages, npages;
	bool continuing, need_init = false;
	int err;

	max_pages = min_t(size_t, ALG_MAX_PAGES,
			  DIV_ROUND_UP(sk->sk_sndbuf, PAGE_SIZE));

	lock_sock(sk);
	continuing = ctx->more;

	if (!continuing) {
		/* Discard a previous request that wasn't marked MSG_MORE. */
		hash_free_result(sk, ctx);
		if (!msg_data_left(msg))
			goto done; /* Zero-length; don't start new req */
		need_init = true;
	} else if (!msg_data_left(msg)) {
		/*
		 * No data - finalise the prev req if MSG_MORE so any error
		 * comes out here.
		 */
		if (!(msg->msg_flags & MSG_MORE)) {
			err = hash_alloc_result(sk, ctx);
			if (err)
				goto unlock_free_result;
			ahash_request_set_crypt(&ctx->req, NULL,
						ctx->result, 0);
			err = crypto_wait_req(crypto_ahash_final(&ctx->req),
					      &ctx->wait);
			if (err)
				goto unlock_free_result;
		}
		goto done_more;
	}

	while (msg_data_left(msg)) {
		ctx->sgl.sgt.sgl = ctx->sgl.sgl;
		ctx->sgl.sgt.nents = 0;
		ctx->sgl.sgt.orig_nents = 0;

		err = -EIO;
		npages = iov_iter_npages(&msg->msg_iter, max_pages);
		if (npages == 0)
			goto unlock_free;

		sg_init_table(ctx->sgl.sgl, npages);

		ctx->sgl.need_unpin = iov_iter_extract_will_pin(&msg->msg_iter);

		err = extract_iter_to_sg(&msg->msg_iter, LONG_MAX,
					 &ctx->sgl.sgt, npages, 0);
		if (err < 0)
			goto unlock_free;
		len = err;
		sg_mark_end(ctx->sgl.sgt.sgl + ctx->sgl.sgt.nents - 1);

		if (!msg_data_left(msg)) {
			err = hash_alloc_result(sk, ctx);
			if (err)
				goto unlock_free;
		}

		ahash_request_set_crypt(&ctx->req, ctx->sgl.sgt.sgl,
					ctx->result, len);

		if (!msg_data_left(msg) && !continuing &&
		    !(msg->msg_flags & MSG_MORE)) {
			err = crypto_ahash_digest(&ctx->req);
		} else {
			if (need_init) {
				err = crypto_wait_req(
					crypto_ahash_init(&ctx->req),
					&ctx->wait);
				if (err)
					goto unlock_free;
				need_init = false;
			}

			if (msg_data_left(msg) || (msg->msg_flags & MSG_MORE))
				err = crypto_ahash_update(&ctx->req);
			else
				err = crypto_ahash_finup(&ctx->req);
			continuing = true;
		}

		err = crypto_wait_req(err, &ctx->wait);
		if (err)
			goto unlock_free;

		copied += len;
		af_alg_free_sg(&ctx->sgl);
	}

done_more:
	ctx->more = msg->msg_flags & MSG_MORE;
done:
	err = 0;
unlock:
	release_sock(sk);
	return copied ?: err;

unlock_free:
	af_alg_free_sg(&ctx->sgl);
unlock_free_result:
	hash_free_result(sk, ctx);
	ctx->more = false;
	goto unlock;
}

static int hash_recvmsg(struct socket *sock, struct msghdr *msg, size_t len,
			int flags)
{
	struct sock *sk = sock->sk;
	struct alg_sock *ask = alg_sk(sk);
	struct hash_ctx *ctx = ask->private;
	unsigned ds = crypto_ahash_digestsize(crypto_ahash_reqtfm(&ctx->req));
	bool result;
	int err;

	if (len > ds)
		len = ds;
	else if (len < ds)
		msg->msg_flags |= MSG_TRUNC;

	lock_sock(sk);
	result = ctx->result;
	err = hash_alloc_result(sk, ctx);
	if (err)
		goto unlock;

	ahash_request_set_crypt(&ctx->req, NULL, ctx->result, 0);

	if (!result && !ctx->more) {
		err = crypto_wait_req(crypto_ahash_init(&ctx->req),
				      &ctx->wait);
		if (err)
			goto unlock;
	}

	if (!result || ctx->more) {
		ctx->more = false;
		err = crypto_wait_req(crypto_ahash_final(&ctx->req),
				      &ctx->wait);
		if (err)
			goto unlock;
	}

	err = memcpy_to_msg(msg, ctx->result, len);

unlock:
	hash_free_result(sk, ctx);
	release_sock(sk);

	return err ?: len;
}

static int hash_accept(struct socket *sock, struct socket *newsock,
		       struct proto_accept_arg *arg)
{
	struct sock *sk = sock->sk;
	struct alg_sock *ask = alg_sk(sk);
	struct hash_ctx *ctx = ask->private;
	struct ahash_request *req = &ctx->req;
	struct crypto_ahash *tfm;
	struct sock *sk2;
	struct alg_sock *ask2;
	struct hash_ctx *ctx2;
	char *state;
	bool more;
	int err;

	tfm = crypto_ahash_reqtfm(req);
	state = kmalloc(crypto_ahash_statesize(tfm), GFP_KERNEL);
	err = -ENOMEM;
	if (!state)
		goto out;

	lock_sock(sk);
	more = ctx->more;
	err = more ? crypto_ahash_export(req, state) : 0;
	release_sock(sk);

	if (err)
		goto out_free_state;

	err = af_alg_accept(ask->parent, newsock, arg);
	if (err)
		goto out_free_state;

	sk2 = newsock->sk;
	ask2 = alg_sk(sk2);
	ctx2 = ask2->private;
	ctx2->more = more;

	if (!more)
		goto out_free_state;

	err = crypto_ahash_import(&ctx2->req, state);
	if (err) {
		sock_orphan(sk2);
		sock_put(sk2);
	}

out_free_state:
	kfree_sensitive(state);

out:
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
	.mmap		=	sock_no_mmap,
	.bind		=	sock_no_bind,

	.release	=	af_alg_release,
	.sendmsg	=	hash_sendmsg,
	.recvmsg	=	hash_recvmsg,
	.accept		=	hash_accept,
};

static int hash_check_key(struct socket *sock)
{
	int err = 0;
	struct sock *psk;
	struct alg_sock *pask;
	struct crypto_ahash *tfm;
	struct sock *sk = sock->sk;
	struct alg_sock *ask = alg_sk(sk);

	lock_sock(sk);
	if (!atomic_read(&ask->nokey_refcnt))
		goto unlock_child;

	psk = ask->parent;
	pask = alg_sk(ask->parent);
	tfm = pask->private;

	err = -ENOKEY;
	lock_sock_nested(psk, SINGLE_DEPTH_NESTING);
	if (crypto_ahash_get_flags(tfm) & CRYPTO_TFM_NEED_KEY)
		goto unlock;

	atomic_dec(&pask->nokey_refcnt);
	atomic_set(&ask->nokey_refcnt, 0);

	err = 0;

unlock:
	release_sock(psk);
unlock_child:
	release_sock(sk);

	return err;
}

static int hash_sendmsg_nokey(struct socket *sock, struct msghdr *msg,
			      size_t size)
{
	int err;

	err = hash_check_key(sock);
	if (err)
		return err;

	return hash_sendmsg(sock, msg, size);
}

static int hash_recvmsg_nokey(struct socket *sock, struct msghdr *msg,
			      size_t ignored, int flags)
{
	int err;

	err = hash_check_key(sock);
	if (err)
		return err;

	return hash_recvmsg(sock, msg, ignored, flags);
}

static int hash_accept_nokey(struct socket *sock, struct socket *newsock,
			     struct proto_accept_arg *arg)
{
	int err;

	err = hash_check_key(sock);
	if (err)
		return err;

	return hash_accept(sock, newsock, arg);
}

static struct proto_ops algif_hash_ops_nokey = {
	.family		=	PF_ALG,

	.connect	=	sock_no_connect,
	.socketpair	=	sock_no_socketpair,
	.getname	=	sock_no_getname,
	.ioctl		=	sock_no_ioctl,
	.listen		=	sock_no_listen,
	.shutdown	=	sock_no_shutdown,
	.mmap		=	sock_no_mmap,
	.bind		=	sock_no_bind,

	.release	=	af_alg_release,
	.sendmsg	=	hash_sendmsg_nokey,
	.recvmsg	=	hash_recvmsg_nokey,
	.accept		=	hash_accept_nokey,
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

	hash_free_result(sk, ctx);
	sock_kfree_s(sk, ctx, ctx->len);
	af_alg_release_parent(sk);
}

static int hash_accept_parent_nokey(void *private, struct sock *sk)
{
	struct crypto_ahash *tfm = private;
	struct alg_sock *ask = alg_sk(sk);
	struct hash_ctx *ctx;
	unsigned int len = sizeof(*ctx) + crypto_ahash_reqsize(tfm);

	ctx = sock_kmalloc(sk, len, GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->result = NULL;
	ctx->len = len;
	ctx->more = false;
	crypto_init_wait(&ctx->wait);

	ask->private = ctx;

	ahash_request_set_tfm(&ctx->req, tfm);
	ahash_request_set_callback(&ctx->req, CRYPTO_TFM_REQ_MAY_BACKLOG,
				   crypto_req_done, &ctx->wait);

	sk->sk_destruct = hash_sock_destruct;

	return 0;
}

static int hash_accept_parent(void *private, struct sock *sk)
{
	struct crypto_ahash *tfm = private;

	if (crypto_ahash_get_flags(tfm) & CRYPTO_TFM_NEED_KEY)
		return -ENOKEY;

	return hash_accept_parent_nokey(private, sk);
}

static const struct af_alg_type algif_type_hash = {
	.bind		=	hash_bind,
	.release	=	hash_release,
	.setkey		=	hash_setkey,
	.accept		=	hash_accept_parent,
	.accept_nokey	=	hash_accept_parent_nokey,
	.ops		=	&algif_hash_ops,
	.ops_nokey	=	&algif_hash_ops_nokey,
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
MODULE_DESCRIPTION("Userspace interface for hash algorithms");
MODULE_LICENSE("GPL");
