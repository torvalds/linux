/*
 * algif_rng: User-space interface for random number generators
 *
 * This file provides the user-space API for random number generators.
 *
 * Copyright (C) 2014, Stephan Mueller <smueller@chronox.de>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, and the entire permission notice in its entirety,
 *    including the disclaimer of warranties.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote
 *    products derived from this software without specific prior
 *    written permission.
 *
 * ALTERNATIVELY, this product may be distributed under the terms of
 * the GNU General Public License, in which case the provisions of the GPL2
 * are required INSTEAD OF the above restrictions.  (This clause is
 * necessary due to a potential bad interaction between the GPL and
 * the restrictions contained in a BSD-style copyright.)
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE, ALL OF
 * WHICH ARE HEREBY DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OF THIS SOFTWARE, EVEN IF NOT ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 */

#include <linux/capability.h>
#include <linux/module.h>
#include <crypto/rng.h>
#include <linux/random.h>
#include <crypto/if_alg.h>
#include <linux/net.h>
#include <net/sock.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Stephan Mueller <smueller@chronox.de>");
MODULE_DESCRIPTION("User-space interface for random number generators");

struct rng_ctx {
#define MAXSIZE 128
	unsigned int len;
	struct crypto_rng *drng;
	u8 *addtl;
	size_t addtl_len;
};

struct rng_parent_ctx {
	struct crypto_rng *drng;
	u8 *entropy;
};

static void rng_reset_addtl(struct rng_ctx *ctx)
{
	kfree_sensitive(ctx->addtl);
	ctx->addtl = NULL;
	ctx->addtl_len = 0;
}

static int _rng_recvmsg(struct crypto_rng *drng, struct msghdr *msg, size_t len,
			u8 *addtl, size_t addtl_len)
{
	int err = 0;
	int genlen = 0;
	u8 result[MAXSIZE];

	if (len == 0)
		return 0;
	if (len > MAXSIZE)
		len = MAXSIZE;

	/*
	 * although not strictly needed, this is a precaution against coding
	 * errors
	 */
	memset(result, 0, len);

	/*
	 * The enforcement of a proper seeding of an RNG is done within an
	 * RNG implementation. Some RNGs (DRBG, krng) do not need specific
	 * seeding as they automatically seed. The X9.31 DRNG will return
	 * an error if it was not seeded properly.
	 */
	genlen = crypto_rng_generate(drng, addtl, addtl_len, result, len);
	if (genlen < 0)
		return genlen;

	err = memcpy_to_msg(msg, result, len);
	memzero_explicit(result, len);

	return err ? err : len;
}

static int rng_recvmsg(struct socket *sock, struct msghdr *msg, size_t len,
		       int flags)
{
	struct sock *sk = sock->sk;
	struct alg_sock *ask = alg_sk(sk);
	struct rng_ctx *ctx = ask->private;

	return _rng_recvmsg(ctx->drng, msg, len, NULL, 0);
}

static int rng_test_recvmsg(struct socket *sock, struct msghdr *msg, size_t len,
			    int flags)
{
	struct sock *sk = sock->sk;
	struct alg_sock *ask = alg_sk(sk);
	struct rng_ctx *ctx = ask->private;
	int ret;

	lock_sock(sock->sk);
	ret = _rng_recvmsg(ctx->drng, msg, len, ctx->addtl, ctx->addtl_len);
	rng_reset_addtl(ctx);
	release_sock(sock->sk);

	return ret;
}

static int rng_test_sendmsg(struct socket *sock, struct msghdr *msg, size_t len)
{
	int err;
	struct alg_sock *ask = alg_sk(sock->sk);
	struct rng_ctx *ctx = ask->private;

	lock_sock(sock->sk);
	if (len > MAXSIZE) {
		err = -EMSGSIZE;
		goto unlock;
	}

	rng_reset_addtl(ctx);
	ctx->addtl = kmalloc(len, GFP_KERNEL);
	if (!ctx->addtl) {
		err = -ENOMEM;
		goto unlock;
	}

	err = memcpy_from_msg(ctx->addtl, msg, len);
	if (err) {
		rng_reset_addtl(ctx);
		goto unlock;
	}
	ctx->addtl_len = len;

unlock:
	release_sock(sock->sk);
	return err ? err : len;
}

static struct proto_ops algif_rng_ops = {
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
	.sendmsg	=	sock_no_sendmsg,

	.release	=	af_alg_release,
	.recvmsg	=	rng_recvmsg,
};

static struct proto_ops __maybe_unused algif_rng_test_ops = {
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
	.recvmsg	=	rng_test_recvmsg,
	.sendmsg	=	rng_test_sendmsg,
};

static void *rng_bind(const char *name, u32 type, u32 mask)
{
	struct rng_parent_ctx *pctx;
	struct crypto_rng *rng;

	pctx = kzalloc(sizeof(*pctx), GFP_KERNEL);
	if (!pctx)
		return ERR_PTR(-ENOMEM);

	rng = crypto_alloc_rng(name, type, mask);
	if (IS_ERR(rng)) {
		kfree(pctx);
		return ERR_CAST(rng);
	}

	pctx->drng = rng;
	return pctx;
}

static void rng_release(void *private)
{
	struct rng_parent_ctx *pctx = private;

	if (unlikely(!pctx))
		return;
	crypto_free_rng(pctx->drng);
	kfree_sensitive(pctx->entropy);
	kfree_sensitive(pctx);
}

static void rng_sock_destruct(struct sock *sk)
{
	struct alg_sock *ask = alg_sk(sk);
	struct rng_ctx *ctx = ask->private;

	rng_reset_addtl(ctx);
	sock_kfree_s(sk, ctx, ctx->len);
	af_alg_release_parent(sk);
}

static int rng_accept_parent(void *private, struct sock *sk)
{
	struct rng_ctx *ctx;
	struct rng_parent_ctx *pctx = private;
	struct alg_sock *ask = alg_sk(sk);
	unsigned int len = sizeof(*ctx);

	ctx = sock_kmalloc(sk, len, GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->len = len;
	ctx->addtl = NULL;
	ctx->addtl_len = 0;

	/*
	 * No seeding done at that point -- if multiple accepts are
	 * done on one RNG instance, each resulting FD points to the same
	 * state of the RNG.
	 */

	ctx->drng = pctx->drng;
	ask->private = ctx;
	sk->sk_destruct = rng_sock_destruct;

	/*
	 * Non NULL pctx->entropy means that CAVP test has been initiated on
	 * this socket, replace proto_ops algif_rng_ops with algif_rng_test_ops.
	 */
	if (IS_ENABLED(CONFIG_CRYPTO_USER_API_RNG_CAVP) && pctx->entropy)
		sk->sk_socket->ops = &algif_rng_test_ops;

	return 0;
}

static int rng_setkey(void *private, const u8 *seed, unsigned int seedlen)
{
	struct rng_parent_ctx *pctx = private;
	/*
	 * Check whether seedlen is of sufficient size is done in RNG
	 * implementations.
	 */
	return crypto_rng_reset(pctx->drng, seed, seedlen);
}

static int __maybe_unused rng_setentropy(void *private, sockptr_t entropy,
					 unsigned int len)
{
	struct rng_parent_ctx *pctx = private;
	u8 *kentropy = NULL;

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;

	if (pctx->entropy)
		return -EINVAL;

	if (len > MAXSIZE)
		return -EMSGSIZE;

	if (len) {
		kentropy = memdup_sockptr(entropy, len);
		if (IS_ERR(kentropy))
			return PTR_ERR(kentropy);
	}

	crypto_rng_alg(pctx->drng)->set_ent(pctx->drng, kentropy, len);
	/*
	 * Since rng doesn't perform any memory management for the entropy
	 * buffer, save kentropy pointer to pctx now to free it after use.
	 */
	pctx->entropy = kentropy;
	return 0;
}

static const struct af_alg_type algif_type_rng = {
	.bind		=	rng_bind,
	.release	=	rng_release,
	.accept		=	rng_accept_parent,
	.setkey		=	rng_setkey,
#ifdef CONFIG_CRYPTO_USER_API_RNG_CAVP
	.setentropy	=	rng_setentropy,
#endif
	.ops		=	&algif_rng_ops,
	.name		=	"rng",
	.owner		=	THIS_MODULE
};

static int __init rng_init(void)
{
	return af_alg_register_type(&algif_type_rng);
}

static void __exit rng_exit(void)
{
	int err = af_alg_unregister_type(&algif_type_rng);
	BUG_ON(err);
}

module_init(rng_init);
module_exit(rng_exit);
