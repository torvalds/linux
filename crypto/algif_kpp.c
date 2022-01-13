/*
 * algif_kpp: User-space interface for key protocol primitives algorithms
 *
 * Copyright (C) 2018 - 2020, Stephan Mueller <smueller@chronox.de>
 *
 * This file provides the user-space API for key protocol primitives.
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

#include <crypto/dh.h>
#include <crypto/ecdh.h>
#include <crypto/kpp.h>
#include <crypto/rng.h>
#include <crypto/if_alg.h>
#include <crypto/scatterwalk.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/net.h>
#include <net/sock.h>

struct kpp_tfm {
	struct crypto_kpp *kpp;
	bool has_key;

#define KPP_NO_PARAMS	0
#define KPP_DH_PARAMS	1
#define KPP_ECDH_PARAMS	2
	int has_params;		/* Type of KPP mechanism */
};

static int kpp_sendmsg(struct socket *sock, struct msghdr *msg, size_t size)
{
	return af_alg_sendmsg(sock, msg, size, 0);
}

static inline int kpp_cipher_op(struct af_alg_ctx *ctx,
				struct af_alg_async_req *areq)
{
	switch (ctx->op) {
	case ALG_OP_KEYGEN:
		return crypto_kpp_generate_public_key(&areq->cra_u.kpp_req);
	case ALG_OP_SSGEN:
		return crypto_kpp_compute_shared_secret(&areq->cra_u.kpp_req);
	default:
		return -EOPNOTSUPP;
	}
}

static int _kpp_recvmsg(struct socket *sock, struct msghdr *msg,
			     size_t ignored, int flags)
{
	struct sock *sk = sock->sk;
	struct alg_sock *ask = alg_sk(sk);
	struct sock *psk = ask->parent;
	struct alg_sock *pask = alg_sk(psk);
	struct af_alg_ctx *ctx = ask->private;
	struct kpp_tfm *kpp = pask->private;
	struct crypto_kpp *tfm = kpp->kpp;
	struct af_alg_async_req *areq;
	size_t len;
	size_t used = 0;
	int err;
	int maxsize;

	if (!ctx->used) {
		err = af_alg_wait_for_data(sk, flags, 0);
		if (err)
			return err;
	}

	maxsize = crypto_kpp_maxsize(tfm);
	if (maxsize < 0)
		return maxsize;

	/* Allocate cipher request for current operation. */
	areq = af_alg_alloc_areq(sk, sizeof(struct af_alg_async_req) +
					    crypto_kpp_reqsize(tfm));
	if (IS_ERR(areq))
		return PTR_ERR(areq);

	/* convert iovecs of output buffers into RX SGL */
	err = af_alg_get_rsgl(sk, msg, flags, areq, maxsize, &len);
	if (err)
		goto free;

	/* ensure output buffer is sufficiently large */
	if (len < maxsize) {
		err = -EMSGSIZE;
		goto free;
	}

	/*
	 * Create a per request TX SGL for this request which tracks the
	 * SG entries from the global TX SGL.
	 */
	if (ctx->op == ALG_OP_SSGEN) {
		used = ctx->used;

		areq->tsgl_entries = af_alg_count_tsgl(sk, used, 0);
		if (!areq->tsgl_entries)
			areq->tsgl_entries = 1;
		areq->tsgl = sock_kmalloc(
				sk, sizeof(*areq->tsgl) * areq->tsgl_entries,
				GFP_KERNEL);
		if (!areq->tsgl) {
			err = -ENOMEM;
			goto free;
		}
		sg_init_table(areq->tsgl, areq->tsgl_entries);
		af_alg_pull_tsgl(sk, used, areq->tsgl, 0);
	}

	/* Initialize the crypto operation */
	kpp_request_set_input(&areq->cra_u.kpp_req, areq->tsgl, used);
	kpp_request_set_output(&areq->cra_u.kpp_req, areq->first_rsgl.sgl.sg,
			       len);
	kpp_request_set_tfm(&areq->cra_u.kpp_req, tfm);

	if (msg->msg_iocb && !is_sync_kiocb(msg->msg_iocb)) {
		/* AIO operation */
		sock_hold(sk);
		areq->iocb = msg->msg_iocb;

		/* Remember output size that will be generated. */
		areq->outlen = len;

		kpp_request_set_callback(&areq->cra_u.kpp_req,
					      CRYPTO_TFM_REQ_MAY_SLEEP,
					      af_alg_async_cb, areq);
		err = kpp_cipher_op(ctx, areq);

		/* AIO operation in progress */
		if (err == -EINPROGRESS || err == -EBUSY)
			return -EIOCBQUEUED;

		sock_put(sk);
	} else {
		/* Synchronous operation */
		kpp_request_set_callback(&areq->cra_u.kpp_req,
					      CRYPTO_TFM_REQ_MAY_SLEEP |
					      CRYPTO_TFM_REQ_MAY_BACKLOG,
					      crypto_req_done,
					      &ctx->wait);
		err = crypto_wait_req(kpp_cipher_op(ctx, areq), &ctx->wait);
	}

free:
	af_alg_free_resources(areq);

	return err ? err : len;
}

static int kpp_recvmsg(struct socket *sock, struct msghdr *msg,
			    size_t ignored, int flags)
{
	struct sock *sk = sock->sk;
	struct alg_sock *ask = alg_sk(sk);
	struct sock *psk = ask->parent;
	struct alg_sock *pask = alg_sk(psk);
	struct kpp_tfm *kpp = pask->private;
	struct crypto_kpp *tfm = kpp->kpp;
	int ret = 0;
	int err;

	lock_sock(sk);

	while (msg_data_left(msg)) {
		err = _kpp_recvmsg(sock, msg, ignored, flags);

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
		 * The caller must provide crypto_kpp_maxsize per request.
		 * If he provides more, we conclude that multiple kpp
		 * operations are requested.
		 */
		iov_iter_advance(&msg->msg_iter,
				 crypto_kpp_maxsize(tfm) - err);
	}

out:

	af_alg_wmem_wakeup(sk);
	release_sock(sk);
	return ret;
}

static struct proto_ops algif_kpp_ops = {
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
	.sendmsg	=	kpp_sendmsg,
	.sendpage	=	af_alg_sendpage,
	.recvmsg	=	kpp_recvmsg,
	.poll		=	af_alg_poll,
};

static int kpp_check_key(struct socket *sock)
{
	struct sock *psk;
	struct alg_sock *pask;
	struct kpp_tfm *tfm;
	struct sock *sk = sock->sk;
	struct alg_sock *ask = alg_sk(sk);
	int err = 0;

	lock_sock(sk);
	if (!atomic_read(&ask->refcnt))
		goto unlock_child;

	psk = ask->parent;
	pask = alg_sk(ask->parent);
	tfm = pask->private;

	lock_sock_nested(psk, SINGLE_DEPTH_NESTING);
	if (!tfm->has_key || (tfm->has_params == KPP_NO_PARAMS)) {
		err = -ENOKEY;
		goto unlock;
	}

	atomic_dec(&pask->refcnt);
	atomic_set(&ask->refcnt, 0);

	err = 0;

unlock:
	release_sock(psk);
unlock_child:
	release_sock(sk);

	return err;
}

static int kpp_sendmsg_nokey(struct socket *sock, struct msghdr *msg,
				  size_t size)
{
	int err;

	err = kpp_check_key(sock);
	if (err)
		return err;

	return kpp_sendmsg(sock, msg, size);
}

static ssize_t kpp_sendpage_nokey(struct socket *sock, struct page *page,
				       int offset, size_t size, int flags)
{
	int err;

	err = kpp_check_key(sock);
	if (err)
		return err;

	return af_alg_sendpage(sock, page, offset, size, flags);
}

static int kpp_recvmsg_nokey(struct socket *sock, struct msghdr *msg,
				  size_t ignored, int flags)
{
	int err;

	err = kpp_check_key(sock);
	if (err)
		return err;

	return kpp_recvmsg(sock, msg, ignored, flags);
}

static struct proto_ops algif_kpp_ops_nokey = {
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
	.sendmsg	=	kpp_sendmsg_nokey,
	.sendpage	=	kpp_sendpage_nokey,
	.recvmsg	=	kpp_recvmsg_nokey,
	.poll		=	af_alg_poll,
};

static void *kpp_bind(const char *name, u32 type, u32 mask)
{
	struct kpp_tfm *tfm;
	struct crypto_kpp *kpp;

	tfm = kmalloc(sizeof(*tfm), GFP_KERNEL);
	if (!tfm)
		return ERR_PTR(-ENOMEM);

	kpp = crypto_alloc_kpp(name, type, mask);
	if (IS_ERR(kpp)) {
		kfree(tfm);
		return ERR_CAST(kpp);
	}

	tfm->kpp = kpp;
	tfm->has_key = false;
	tfm->has_params = KPP_NO_PARAMS;

	return tfm;
}

static void kpp_release(void *private)
{
	struct kpp_tfm *tfm = private;
	struct crypto_kpp *kpp = tfm->kpp;

	crypto_free_kpp(kpp);
	kfree(tfm);
}

static int kpp_dh_set_secret(struct crypto_kpp *tfm, struct dh *params)
{
	char *packed_key = NULL;
	unsigned int packed_key_len;
	int ret;

	packed_key_len = crypto_dh_key_len(params);
	packed_key = kmalloc(packed_key_len, GFP_KERNEL);
	if (!packed_key)
		return -ENOMEM;

	ret = crypto_dh_encode_key(packed_key, packed_key_len, params);
	if (ret)
		goto out;

	ret = crypto_kpp_set_secret(tfm, packed_key, packed_key_len);

out:
	kfree(packed_key);
	return ret;
}

static int kpp_dh_set_privkey(struct crypto_kpp *tfm, const u8 *key,
			      unsigned int keylen)
{
	struct dh params = {
		.key = key,
		.key_size = keylen,
		.p = NULL,
		.p_size = 0,
		.g = NULL,
		.g_size = 0,
	};

	return kpp_dh_set_secret(tfm, &params);
}

static int kpp_ecdh_set_secret(struct crypto_kpp *tfm, struct ecdh *params)
{
	char *packed_key = NULL;
	unsigned int packed_key_len;
	int ret;

	packed_key_len = crypto_ecdh_key_len(params);
	packed_key = kmalloc(packed_key_len, GFP_KERNEL);
	if (!packed_key)
		return -ENOMEM;

	ret = crypto_ecdh_encode_key(packed_key, packed_key_len, params);
	if (ret)
		goto out;

	ret = crypto_kpp_set_secret(tfm, packed_key, packed_key_len);

out:
	kfree(packed_key);
	return ret;
}

static int kpp_ecdh_set_privkey(struct crypto_kpp *tfm, const u8 *key,
				unsigned int keylen)
{
	struct ecdh params = {
		.curve_id = 0,
		.key = key,
		.key_size = keylen,
	};

	return kpp_ecdh_set_secret(tfm, &params);
}

static int kpp_setprivkey(void *private, const u8 *key, unsigned int keylen)
{
	struct kpp_tfm *kpp = private;
	struct crypto_kpp *tfm = kpp->kpp;
	int err;

	if (kpp->has_params == KPP_NO_PARAMS)
		return -ENOKEY;

	/* The DH code cannot generate private keys. ECDH can do that */
	if ((!key || !keylen) && (kpp->has_params == KPP_DH_PARAMS)) {
		kpp->has_key = false;
		return -EOPNOTSUPP;
	}

	switch (kpp->has_params) {
	case KPP_DH_PARAMS:
		err = kpp_dh_set_privkey(tfm, key, keylen);
		break;
	case KPP_ECDH_PARAMS:
		err = kpp_ecdh_set_privkey(tfm, key, keylen);
		break;
	default:
		err = -EFAULT;
	}

	kpp->has_key = !err;

	/* Return the maximum size of the kpp operation. */
	if (!err)
		err = crypto_kpp_maxsize(tfm);

	return err;
}

static int kpp_dh_setparams_pkcs3(void *private, const u8 *params,
				  unsigned int paramslen)
{
	struct kpp_tfm *kpp = private;
	struct crypto_kpp *tfm = kpp->kpp;
	int err;

	/* If parameters were already set, disallow setting them again. */
	if (kpp->has_params != KPP_NO_PARAMS)
		return -EINVAL;

	err = crypto_kpp_set_params(tfm, params, paramslen);
	if (!err) {
		kpp->has_params = KPP_DH_PARAMS;
		/* Return the maximum size of the kpp operation. */
		err = crypto_kpp_maxsize(tfm);
	} else
		kpp->has_params = KPP_NO_PARAMS;

	return err;
}

static int kpp_ecdh_setcurve(void *private, const u8 *curveid,
			     unsigned int curveidlen)
{
	struct kpp_tfm *kpp = private;
	struct crypto_kpp *tfm = kpp->kpp;
	int err;
	struct ecdh params = {
		.key = NULL,
		.key_size = 0,
	};

	/* If parameters were already set, disallow setting them again. */
	if (kpp->has_params != KPP_NO_PARAMS)
		return -EINVAL;

	if (curveidlen != sizeof(unsigned long))
		return -EINVAL;

	err = kstrtou16(curveid, 10, &params.curve_id);
	if (err)
		return err;

	err = kpp_ecdh_set_secret(tfm, &params);
	if (!err) {
		kpp->has_params = KPP_ECDH_PARAMS;
		/* Return the maximum size of the kpp operation. */
		err = crypto_kpp_maxsize(tfm);
	} else
		kpp->has_params = KPP_NO_PARAMS;

	return err;
}

static void kpp_sock_destruct(struct sock *sk)
{
	struct alg_sock *ask = alg_sk(sk);
	struct af_alg_ctx *ctx = ask->private;

	af_alg_pull_tsgl(sk, ctx->used, NULL, 0);
	sock_kfree_s(sk, ctx, ctx->len);
	af_alg_release_parent(sk);
}

static int kpp_accept_parent_nokey(void *private, struct sock *sk)
{
	struct af_alg_ctx *ctx;
	struct alg_sock *ask = alg_sk(sk);
	unsigned int len = sizeof(*ctx);

	ctx = sock_kmalloc(sk, len, GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	INIT_LIST_HEAD(&ctx->tsgl_list);
	ctx->len = len;
	ctx->used = 0;
	atomic_set(&ctx->rcvused, 0);
	ctx->more = 0;
	ctx->merge = 0;
	ctx->op = 0;
	crypto_init_wait(&ctx->wait);

	ask->private = ctx;

	sk->sk_destruct = kpp_sock_destruct;

	return 0;
}

static int kpp_accept_parent(void *private, struct sock *sk)
{
	struct kpp_tfm *tfm = private;

	if (!tfm->has_key || (tfm->has_params == KPP_NO_PARAMS))
		return -ENOKEY;

	return kpp_accept_parent_nokey(private, sk);
}

static const struct af_alg_type algif_type_kpp = {
	.bind		=	kpp_bind,
	.release	=	kpp_release,
	.setkey		=	kpp_setprivkey,
	.setpubkey	=	NULL,
	.dhparams	=	kpp_dh_setparams_pkcs3,
	.ecdhcurve	=	kpp_ecdh_setcurve,
	.setauthsize	=	NULL,
	.accept		=	kpp_accept_parent,
	.accept_nokey	=	kpp_accept_parent_nokey,
	.ops		=	&algif_kpp_ops,
	.ops_nokey	=	&algif_kpp_ops_nokey,
	.name		=	"kpp",
	.owner		=	THIS_MODULE
};

static int __init algif_kpp_init(void)
{
	return af_alg_register_type(&algif_type_kpp);
}

static void __exit algif_kpp_exit(void)
{
	int err = af_alg_unregister_type(&algif_type_kpp);

	BUG_ON(err);
}

module_init(algif_kpp_init);
module_exit(algif_kpp_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Stephan Mueller <smueller@chronox.de>");
MODULE_DESCRIPTION("Key protocol primitives kernel crypto API user space interface");

