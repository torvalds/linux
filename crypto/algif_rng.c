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
};

static int rng_recvmsg(struct kiocb *unused, struct socket *sock,
		       struct msghdr *msg, size_t len, int flags)
{
	struct sock *sk = sock->sk;
	struct alg_sock *ask = alg_sk(sk);
	struct rng_ctx *ctx = ask->private;
	int err = -EFAULT;
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
	genlen = crypto_rng_get_bytes(ctx->drng, result, len);
	if (genlen < 0)
		return genlen;

	err = memcpy_to_msg(msg, result, len);
	memzero_explicit(result, len);

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
	.getsockopt	=	sock_no_getsockopt,
	.mmap		=	sock_no_mmap,
	.bind		=	sock_no_bind,
	.accept		=	sock_no_accept,
	.setsockopt	=	sock_no_setsockopt,
	.poll		=	sock_no_poll,
	.sendmsg	=	sock_no_sendmsg,
	.sendpage	=	sock_no_sendpage,

	.release	=	af_alg_release,
	.recvmsg	=	rng_recvmsg,
};

static void *rng_bind(const char *name, u32 type, u32 mask)
{
	return crypto_alloc_rng(name, type, mask);
}

static void rng_release(void *private)
{
	crypto_free_rng(private);
}

static void rng_sock_destruct(struct sock *sk)
{
	struct alg_sock *ask = alg_sk(sk);
	struct rng_ctx *ctx = ask->private;

	sock_kfree_s(sk, ctx, ctx->len);
	af_alg_release_parent(sk);
}

static int rng_accept_parent(void *private, struct sock *sk)
{
	struct rng_ctx *ctx;
	struct alg_sock *ask = alg_sk(sk);
	unsigned int len = sizeof(*ctx);

	ctx = sock_kmalloc(sk, len, GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->len = len;

	/*
	 * No seeding done at that point -- if multiple accepts are
	 * done on one RNG instance, each resulting FD points to the same
	 * state of the RNG.
	 */

	ctx->drng = private;
	ask->private = ctx;
	sk->sk_destruct = rng_sock_destruct;

	return 0;
}

static int rng_setkey(void *private, const u8 *seed, unsigned int seedlen)
{
	/*
	 * Check whether seedlen is of sufficient size is done in RNG
	 * implementations.
	 */
	return crypto_rng_reset(private, (u8 *)seed, seedlen);
}

static const struct af_alg_type algif_type_rng = {
	.bind		=	rng_bind,
	.release	=	rng_release,
	.accept		=	rng_accept_parent,
	.setkey		=	rng_setkey,
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
