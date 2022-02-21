// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * algif_akcipher: User-space interface for asymmetric cipher algorithms
 *
 * Copyright (C) 2017, Stephan Mueller <smueller@chronox.de>
 *
 * This file provides the user-space API for asymmetric ciphers.
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

#include <crypto/akcipher.h>
#include <crypto/scatterwalk.h>
#include <crypto/if_alg.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/net.h>
#include <net/sock.h>

struct akcipher_tfm {
	struct crypto_akcipher *akcipher;
	bool has_key;
};

static int akcipher_sendmsg(struct socket *sock, struct msghdr *msg,
			    size_t size)
{
	return af_alg_sendmsg(sock, msg, size, 0);
}

static int _akcipher_recvmsg(struct socket *sock, struct msghdr *msg,
			     size_t ignored, int flags)
{
	struct sock *sk = sock->sk;
	struct alg_sock *ask = alg_sk(sk);
	struct sock *psk = ask->parent;
	struct alg_sock *pask = alg_sk(psk);
	struct af_alg_ctx *ctx = ask->private;
	struct akcipher_tfm *akc = pask->private;
	struct crypto_akcipher *tfm = akc->akcipher;
	struct af_alg_async_req *areq;
	int err = 0;
	int maxsize;
	size_t len = 0;
	size_t used = 0;

	maxsize = crypto_akcipher_maxsize(tfm);
	if (maxsize < 0)
		return maxsize;

	/* Allocate cipher request for current operation. */
	areq = af_alg_alloc_areq(sk, sizeof(struct af_alg_async_req) +
				     crypto_akcipher_reqsize(tfm));
	if (IS_ERR(areq))
		return PTR_ERR(areq);

	/* convert iovecs of output buffers into RX SGL */
	err = af_alg_get_rsgl(sk, msg, flags, areq, maxsize, &len);
	if (err)
		goto free;

	/* ensure output buffer is sufficiently large */
	if (len < maxsize) {
		pr_err("%s: output buffer is not large enough. len:0x%x, maxsize:0x%x\n",
		       __func__, len, maxsize);
		err = -EMSGSIZE;
		goto free;
	}

	/*
	 * Create a per request TX SGL for this request which tracks the
	 * SG entries from the global TX SGL.
	 */
	used = ctx->used;
	areq->tsgl_entries = af_alg_count_tsgl(sk, used, 0);
	if (!areq->tsgl_entries)
		areq->tsgl_entries = 1;
	areq->tsgl = sock_kmalloc(sk, sizeof(*areq->tsgl) * areq->tsgl_entries,
				  GFP_KERNEL);
	if (!areq->tsgl) {
		err = -ENOMEM;
		goto free;
	}
	sg_init_table(areq->tsgl, areq->tsgl_entries);
	af_alg_pull_tsgl(sk, used, areq->tsgl, 0);

	/* Initialize the crypto operation */
	akcipher_request_set_tfm(&areq->cra_u.akcipher_req, tfm);
	akcipher_request_set_crypt(&areq->cra_u.akcipher_req, areq->tsgl,
				   areq->first_rsgl.sgl.sg, used, len);

	if (msg->msg_iocb && !is_sync_kiocb(msg->msg_iocb)) {
		/* AIO operation */
		areq->iocb = msg->msg_iocb;
		akcipher_request_set_callback(&areq->cra_u.akcipher_req,
					      CRYPTO_TFM_REQ_MAY_SLEEP,
					      af_alg_async_cb, areq);
	} else {
		/* Synchronous operation */
		akcipher_request_set_callback(&areq->cra_u.akcipher_req,
					      CRYPTO_TFM_REQ_MAY_SLEEP |
					      CRYPTO_TFM_REQ_MAY_BACKLOG,
					      crypto_req_done,
					      &ctx->wait);
	}

	switch (ctx->op) {
	case ALG_OP_ENCRYPT:
		err = crypto_akcipher_encrypt(&areq->cra_u.akcipher_req);
		break;
	case ALG_OP_DECRYPT:
		err = crypto_akcipher_decrypt(&areq->cra_u.akcipher_req);
		break;
	case ALG_OP_SIGN:
		err = crypto_akcipher_sign(&areq->cra_u.akcipher_req);
		break;
	case ALG_OP_VERIFY:
		err = crypto_akcipher_verify(&areq->cra_u.akcipher_req);
		break;
	default:
		err = -EOPNOTSUPP;
		goto free;
	}

	if (msg->msg_iocb && !is_sync_kiocb(msg->msg_iocb)) {
		/* AIO operation in progress */
		if (err == -EINPROGRESS) {
			pr_info("%s: AIO operation in progress\n", __func__);
			sock_hold(sk);

			/* Remember output size that will be generated. */
			areq->outlen = areq->cra_u.akcipher_req.dst_len;

			return -EIOCBQUEUED;
		}
	} else {
		/* Wait for synchronous operation completion */
		err = crypto_wait_req(err, &ctx->wait);
	}

free:
	af_alg_free_resources(areq);

	return err ? err : areq->cra_u.akcipher_req.dst_len;
}

static int akcipher_recvmsg(struct socket *sock, struct msghdr *msg,
			    size_t ignored, int flags)
{
	struct sock *sk = sock->sk;
	struct alg_sock *ask = alg_sk(sk);
	struct sock *psk = ask->parent;
	struct alg_sock *pask = alg_sk(psk);
	struct akcipher_tfm *akc = pask->private;
	struct crypto_akcipher *tfm = akc->akcipher;

	int ret = 0;

	lock_sock(sk);

	while (msg_data_left(msg)) {
		int err = _akcipher_recvmsg(sock, msg, ignored, flags);

		/*
		 * This error covers -EIOCBQUEUED which implies that we can
		 * only handle one AIO request. If the caller wants to have
		 * multiple AIO requests in parallel, he must make multiple
		 * separate AIO calls.
		 */
		if (err <= 0) {
			if (err == -EIOCBQUEUED || err == -EBADMSG || !ret)
				ret = err;
			goto out;
		}

		ret += err;

		/*
		 * The caller must provide crypto_akcipher_maxsize per request.
		 * If he provides more, we conclude that multiple akcipher
		 * operations are requested.
		 */
		iov_iter_advance(&msg->msg_iter,
				 crypto_akcipher_maxsize(tfm) - err);
	}

out:
	af_alg_wmem_wakeup(sk);
	release_sock(sk);
	return ret;
}

static struct proto_ops algif_akcipher_ops = {
	.family		=	PF_ALG,

	.connect	=	sock_no_connect,
	.socketpair	=	sock_no_socketpair,
	.getname	=	sock_no_getname,
	.ioctl		=	sock_no_ioctl,
	.listen		=	sock_no_listen,
	.shutdown	=	sock_no_shutdown,
	.mmap		=	sock_no_mmap,
	.bind		=	sock_no_bind,
	.accept		=	sock_no_accept,

	.release	=	af_alg_release,
	.sendmsg	=	akcipher_sendmsg,
	.sendpage	=	af_alg_sendpage,
	.recvmsg	=	akcipher_recvmsg,
	.poll		=	af_alg_poll,
};

static int akcipher_check_key(struct socket *sock)
{
	int err = 0;
	struct sock *psk;
	struct alg_sock *pask;
	struct akcipher_tfm *tfm;
	struct sock *sk = sock->sk;
	struct alg_sock *ask = alg_sk(sk);

	lock_sock(sk);
	if (atomic_read(&ask->refcnt))
		goto unlock_child;

	psk = ask->parent;
	pask = alg_sk(ask->parent);
	tfm = pask->private;

	err = -ENOKEY;
	lock_sock_nested(psk, SINGLE_DEPTH_NESTING);
	if (!tfm->has_key)
		goto unlock;

	atomic_inc(&pask->refcnt);
	if (!atomic_read(&pask->refcnt))
		sock_hold(psk);

	atomic_set(&ask->refcnt, 1);
	sock_put(psk);

	err = 0;

unlock:
	release_sock(psk);
unlock_child:
	release_sock(sk);

	return err;
}

static int akcipher_sendmsg_nokey(struct socket *sock, struct msghdr *msg,
				  size_t size)
{
	int err;

	err = akcipher_check_key(sock);
	if (err)
		return err;

	return akcipher_sendmsg(sock, msg, size);
}

static ssize_t akcipher_sendpage_nokey(struct socket *sock, struct page *page,
				       int offset, size_t size, int flags)
{
	int err;

	err = akcipher_check_key(sock);
	if (err)
		return err;

	return af_alg_sendpage(sock, page, offset, size, flags);
}

static int akcipher_recvmsg_nokey(struct socket *sock, struct msghdr *msg,
				  size_t ignored, int flags)
{
	int err;

	err = akcipher_check_key(sock);
	if (err)
		return err;

	return akcipher_recvmsg(sock, msg, ignored, flags);
}

static struct proto_ops algif_akcipher_ops_nokey = {
	.family		=	PF_ALG,

	.connect	=	sock_no_connect,
	.socketpair	=	sock_no_socketpair,
	.getname	=	sock_no_getname,
	.ioctl		=	sock_no_ioctl,
	.listen		=	sock_no_listen,
	.shutdown	=	sock_no_shutdown,
	.mmap		=	sock_no_mmap,
	.bind		=	sock_no_bind,
	.accept		=	sock_no_accept,

	.release	=	af_alg_release,
	.sendmsg	=	akcipher_sendmsg_nokey,
	.sendpage	=	akcipher_sendpage_nokey,
	.recvmsg	=	akcipher_recvmsg_nokey,
	.poll		=	af_alg_poll,
};

static void *akcipher_bind(const char *name, u32 type, u32 mask)
{
	struct akcipher_tfm *tfm;
	struct crypto_akcipher *akcipher;

	tfm = kzalloc(sizeof(*tfm), GFP_KERNEL);
	if (!tfm)
		return ERR_PTR(-ENOMEM);

	akcipher = crypto_alloc_akcipher(name, type, mask);
	if (IS_ERR(akcipher)) {
		kfree(tfm);
		return ERR_CAST(akcipher);
	}

	tfm->akcipher = akcipher;

	return tfm;
}

static void akcipher_release(void *private)
{
	struct akcipher_tfm *tfm = private;
	struct crypto_akcipher *akcipher = tfm->akcipher;

	crypto_free_akcipher(akcipher);
	kfree(tfm);
}

static int akcipher_setprivkey(void *private, const u8 *key,
			       unsigned int keylen)
{
	struct akcipher_tfm *tfm = private;
	struct crypto_akcipher *akcipher = tfm->akcipher;
	int err;

	err = crypto_akcipher_set_priv_key(akcipher, key, keylen);
	tfm->has_key = !err;

	/* Return the maximum size of the akcipher operation. */
	if (!err)
		err = crypto_akcipher_maxsize(akcipher);

	return err;
}

static int akcipher_setpubkey(void *private, const u8 *key, unsigned int keylen)
{
	struct akcipher_tfm *tfm = private;
	struct crypto_akcipher *akcipher = tfm->akcipher;
	int err;

	err = crypto_akcipher_set_pub_key(akcipher, key, keylen);
	tfm->has_key = !err;

	/* Return the maximum size of the akcipher operation. */
	if (!err)
		err = crypto_akcipher_maxsize(akcipher);

	return err;
}

static void akcipher_sock_destruct(struct sock *sk)
{
	struct alg_sock *ask = alg_sk(sk);
	struct af_alg_ctx *ctx = ask->private;

	af_alg_pull_tsgl(sk, ctx->used, NULL, 0);
	sock_kfree_s(sk, ctx, ctx->len);
	af_alg_release_parent(sk);
}

static int akcipher_accept_parent_nokey(void *private, struct sock *sk)
{
	struct af_alg_ctx *ctx;
	struct alg_sock *ask = alg_sk(sk);
	unsigned int len = sizeof(*ctx);

	ctx = sock_kmalloc(sk, len, GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;
	memset(ctx, 0, len);

	INIT_LIST_HEAD(&ctx->tsgl_list);
	ctx->len = len;
	ctx->used = 0;
	atomic_set(&ctx->rcvused, 0);
	ctx->more = 0;
	ctx->merge = 0;
	ctx->op = 0;
	crypto_init_wait(&ctx->wait);

	ask->private = ctx;

	sk->sk_destruct = akcipher_sock_destruct;

	return 0;
}

static int akcipher_accept_parent(void *private, struct sock *sk)
{
	struct akcipher_tfm *tfm = private;

	if (!tfm->has_key)
		return -ENOKEY;

	return akcipher_accept_parent_nokey(private, sk);
}

static const struct af_alg_type algif_type_akcipher = {
	.bind		=	akcipher_bind,
	.release	=	akcipher_release,
	.setkey		=	akcipher_setprivkey,
	.setpubkey	=	akcipher_setpubkey,
	.setauthsize	=	NULL,
	.accept		=	akcipher_accept_parent,
	.accept_nokey	=	akcipher_accept_parent_nokey,
	.ops		=	&algif_akcipher_ops,
	.ops_nokey	=	&algif_akcipher_ops_nokey,
	.name		=	"akcipher",
	.owner		=	THIS_MODULE
};

static int __init algif_akcipher_init(void)
{
	return af_alg_register_type(&algif_type_akcipher);
}

static void __exit algif_akcipher_exit(void)
{
	af_alg_unregister_type(&algif_type_akcipher);
}

module_init(algif_akcipher_init);
module_exit(algif_akcipher_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Stephan Mueller <smueller@chronox.de>");
MODULE_DESCRIPTION("Asymmetric kernel crypto API user space interface");
