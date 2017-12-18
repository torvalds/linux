/*
 * af_alg: User-space algorithm interface
 *
 * This file provides the user-space API for algorithms.
 *
 * Copyright (c) 2010 Herbert Xu <herbert@gondor.apana.org.au>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 */

#include <linux/atomic.h>
#include <crypto/if_alg.h>
#include <linux/crypto.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/net.h>
#include <linux/rwsem.h>
#include <linux/sched/signal.h>
#include <linux/security.h>

struct alg_type_list {
	const struct af_alg_type *type;
	struct list_head list;
};

static atomic_long_t alg_memory_allocated;

static struct proto alg_proto = {
	.name			= "ALG",
	.owner			= THIS_MODULE,
	.memory_allocated	= &alg_memory_allocated,
	.obj_size		= sizeof(struct alg_sock),
};

static LIST_HEAD(alg_types);
static DECLARE_RWSEM(alg_types_sem);

static const struct af_alg_type *alg_get_type(const char *name)
{
	const struct af_alg_type *type = ERR_PTR(-ENOENT);
	struct alg_type_list *node;

	down_read(&alg_types_sem);
	list_for_each_entry(node, &alg_types, list) {
		if (strcmp(node->type->name, name))
			continue;

		if (try_module_get(node->type->owner))
			type = node->type;
		break;
	}
	up_read(&alg_types_sem);

	return type;
}

int af_alg_register_type(const struct af_alg_type *type)
{
	struct alg_type_list *node;
	int err = -EEXIST;

	down_write(&alg_types_sem);
	list_for_each_entry(node, &alg_types, list) {
		if (!strcmp(node->type->name, type->name))
			goto unlock;
	}

	node = kmalloc(sizeof(*node), GFP_KERNEL);
	err = -ENOMEM;
	if (!node)
		goto unlock;

	type->ops->owner = THIS_MODULE;
	if (type->ops_nokey)
		type->ops_nokey->owner = THIS_MODULE;
	node->type = type;
	list_add(&node->list, &alg_types);
	err = 0;

unlock:
	up_write(&alg_types_sem);

	return err;
}
EXPORT_SYMBOL_GPL(af_alg_register_type);

int af_alg_unregister_type(const struct af_alg_type *type)
{
	struct alg_type_list *node;
	int err = -ENOENT;

	down_write(&alg_types_sem);
	list_for_each_entry(node, &alg_types, list) {
		if (strcmp(node->type->name, type->name))
			continue;

		list_del(&node->list);
		kfree(node);
		err = 0;
		break;
	}
	up_write(&alg_types_sem);

	return err;
}
EXPORT_SYMBOL_GPL(af_alg_unregister_type);

static void alg_do_release(const struct af_alg_type *type, void *private)
{
	if (!type)
		return;

	type->release(private);
	module_put(type->owner);
}

int af_alg_release(struct socket *sock)
{
	if (sock->sk)
		sock_put(sock->sk);
	return 0;
}
EXPORT_SYMBOL_GPL(af_alg_release);

void af_alg_release_parent(struct sock *sk)
{
	struct alg_sock *ask = alg_sk(sk);
	unsigned int nokey = ask->nokey_refcnt;
	bool last = nokey && !ask->refcnt;

	sk = ask->parent;
	ask = alg_sk(sk);

	lock_sock(sk);
	ask->nokey_refcnt -= nokey;
	if (!last)
		last = !--ask->refcnt;
	release_sock(sk);

	if (last)
		sock_put(sk);
}
EXPORT_SYMBOL_GPL(af_alg_release_parent);

static int alg_bind(struct socket *sock, struct sockaddr *uaddr, int addr_len)
{
	const u32 forbidden = CRYPTO_ALG_INTERNAL;
	struct sock *sk = sock->sk;
	struct alg_sock *ask = alg_sk(sk);
	struct sockaddr_alg *sa = (void *)uaddr;
	const struct af_alg_type *type;
	void *private;
	int err;

	if (sock->state == SS_CONNECTED)
		return -EINVAL;

	if (addr_len < sizeof(*sa))
		return -EINVAL;

	sa->salg_type[sizeof(sa->salg_type) - 1] = 0;
	sa->salg_name[sizeof(sa->salg_name) + addr_len - sizeof(*sa) - 1] = 0;

	type = alg_get_type(sa->salg_type);
	if (IS_ERR(type) && PTR_ERR(type) == -ENOENT) {
		request_module("algif-%s", sa->salg_type);
		type = alg_get_type(sa->salg_type);
	}

	if (IS_ERR(type))
		return PTR_ERR(type);

	private = type->bind(sa->salg_name,
			     sa->salg_feat & ~forbidden,
			     sa->salg_mask & ~forbidden);
	if (IS_ERR(private)) {
		module_put(type->owner);
		return PTR_ERR(private);
	}

	err = -EBUSY;
	lock_sock(sk);
	if (ask->refcnt | ask->nokey_refcnt)
		goto unlock;

	swap(ask->type, type);
	swap(ask->private, private);

	err = 0;

unlock:
	release_sock(sk);

	alg_do_release(type, private);

	return err;
}

static int alg_setkey(struct sock *sk, char __user *ukey,
		      unsigned int keylen)
{
	struct alg_sock *ask = alg_sk(sk);
	const struct af_alg_type *type = ask->type;
	u8 *key;
	int err;

	key = sock_kmalloc(sk, keylen, GFP_KERNEL);
	if (!key)
		return -ENOMEM;

	err = -EFAULT;
	if (copy_from_user(key, ukey, keylen))
		goto out;

	err = type->setkey(ask->private, key, keylen);

out:
	sock_kzfree_s(sk, key, keylen);

	return err;
}

static int alg_setsockopt(struct socket *sock, int level, int optname,
			  char __user *optval, unsigned int optlen)
{
	struct sock *sk = sock->sk;
	struct alg_sock *ask = alg_sk(sk);
	const struct af_alg_type *type;
	int err = -EBUSY;

	lock_sock(sk);
	if (ask->refcnt)
		goto unlock;

	type = ask->type;

	err = -ENOPROTOOPT;
	if (level != SOL_ALG || !type)
		goto unlock;

	switch (optname) {
	case ALG_SET_KEY:
		if (sock->state == SS_CONNECTED)
			goto unlock;
		if (!type->setkey)
			goto unlock;

		err = alg_setkey(sk, optval, optlen);
		break;
	case ALG_SET_AEAD_AUTHSIZE:
		if (sock->state == SS_CONNECTED)
			goto unlock;
		if (!type->setauthsize)
			goto unlock;
		err = type->setauthsize(ask->private, optlen);
	}

unlock:
	release_sock(sk);

	return err;
}

int af_alg_accept(struct sock *sk, struct socket *newsock, bool kern)
{
	struct alg_sock *ask = alg_sk(sk);
	const struct af_alg_type *type;
	struct sock *sk2;
	unsigned int nokey;
	int err;

	lock_sock(sk);
	type = ask->type;

	err = -EINVAL;
	if (!type)
		goto unlock;

	sk2 = sk_alloc(sock_net(sk), PF_ALG, GFP_KERNEL, &alg_proto, kern);
	err = -ENOMEM;
	if (!sk2)
		goto unlock;

	sock_init_data(newsock, sk2);
	security_sock_graft(sk2, newsock);
	security_sk_clone(sk, sk2);

	err = type->accept(ask->private, sk2);

	nokey = err == -ENOKEY;
	if (nokey && type->accept_nokey)
		err = type->accept_nokey(ask->private, sk2);

	if (err)
		goto unlock;

	sk2->sk_family = PF_ALG;

	if (nokey || !ask->refcnt++)
		sock_hold(sk);
	ask->nokey_refcnt += nokey;
	alg_sk(sk2)->parent = sk;
	alg_sk(sk2)->type = type;
	alg_sk(sk2)->nokey_refcnt = nokey;

	newsock->ops = type->ops;
	newsock->state = SS_CONNECTED;

	if (nokey)
		newsock->ops = type->ops_nokey;

	err = 0;

unlock:
	release_sock(sk);

	return err;
}
EXPORT_SYMBOL_GPL(af_alg_accept);

static int alg_accept(struct socket *sock, struct socket *newsock, int flags,
		      bool kern)
{
	return af_alg_accept(sock->sk, newsock, kern);
}

static const struct proto_ops alg_proto_ops = {
	.family		=	PF_ALG,
	.owner		=	THIS_MODULE,

	.connect	=	sock_no_connect,
	.socketpair	=	sock_no_socketpair,
	.getname	=	sock_no_getname,
	.ioctl		=	sock_no_ioctl,
	.listen		=	sock_no_listen,
	.shutdown	=	sock_no_shutdown,
	.getsockopt	=	sock_no_getsockopt,
	.mmap		=	sock_no_mmap,
	.sendpage	=	sock_no_sendpage,
	.sendmsg	=	sock_no_sendmsg,
	.recvmsg	=	sock_no_recvmsg,
	.poll		=	sock_no_poll,

	.bind		=	alg_bind,
	.release	=	af_alg_release,
	.setsockopt	=	alg_setsockopt,
	.accept		=	alg_accept,
};

static void alg_sock_destruct(struct sock *sk)
{
	struct alg_sock *ask = alg_sk(sk);

	alg_do_release(ask->type, ask->private);
}

static int alg_create(struct net *net, struct socket *sock, int protocol,
		      int kern)
{
	struct sock *sk;
	int err;

	if (sock->type != SOCK_SEQPACKET)
		return -ESOCKTNOSUPPORT;
	if (protocol != 0)
		return -EPROTONOSUPPORT;

	err = -ENOMEM;
	sk = sk_alloc(net, PF_ALG, GFP_KERNEL, &alg_proto, kern);
	if (!sk)
		goto out;

	sock->ops = &alg_proto_ops;
	sock_init_data(sock, sk);

	sk->sk_family = PF_ALG;
	sk->sk_destruct = alg_sock_destruct;

	return 0;
out:
	return err;
}

static const struct net_proto_family alg_family = {
	.family	=	PF_ALG,
	.create	=	alg_create,
	.owner	=	THIS_MODULE,
};

int af_alg_make_sg(struct af_alg_sgl *sgl, struct iov_iter *iter, int len)
{
	size_t off;
	ssize_t n;
	int npages, i;

	n = iov_iter_get_pages(iter, sgl->pages, len, ALG_MAX_PAGES, &off);
	if (n < 0)
		return n;

	npages = (off + n + PAGE_SIZE - 1) >> PAGE_SHIFT;
	if (WARN_ON(npages == 0))
		return -EINVAL;
	/* Add one extra for linking */
	sg_init_table(sgl->sg, npages + 1);

	for (i = 0, len = n; i < npages; i++) {
		int plen = min_t(int, len, PAGE_SIZE - off);

		sg_set_page(sgl->sg + i, sgl->pages[i], plen, off);

		off = 0;
		len -= plen;
	}
	sg_mark_end(sgl->sg + npages - 1);
	sgl->npages = npages;

	return n;
}
EXPORT_SYMBOL_GPL(af_alg_make_sg);

void af_alg_link_sg(struct af_alg_sgl *sgl_prev, struct af_alg_sgl *sgl_new)
{
	sg_unmark_end(sgl_prev->sg + sgl_prev->npages - 1);
	sg_chain(sgl_prev->sg, sgl_prev->npages + 1, sgl_new->sg);
}
EXPORT_SYMBOL_GPL(af_alg_link_sg);

void af_alg_free_sg(struct af_alg_sgl *sgl)
{
	int i;

	for (i = 0; i < sgl->npages; i++)
		put_page(sgl->pages[i]);
}
EXPORT_SYMBOL_GPL(af_alg_free_sg);

int af_alg_cmsg_send(struct msghdr *msg, struct af_alg_control *con)
{
	struct cmsghdr *cmsg;

	for_each_cmsghdr(cmsg, msg) {
		if (!CMSG_OK(msg, cmsg))
			return -EINVAL;
		if (cmsg->cmsg_level != SOL_ALG)
			continue;

		switch (cmsg->cmsg_type) {
		case ALG_SET_IV:
			if (cmsg->cmsg_len < CMSG_LEN(sizeof(*con->iv)))
				return -EINVAL;
			con->iv = (void *)CMSG_DATA(cmsg);
			if (cmsg->cmsg_len < CMSG_LEN(con->iv->ivlen +
						      sizeof(*con->iv)))
				return -EINVAL;
			break;

		case ALG_SET_OP:
			if (cmsg->cmsg_len < CMSG_LEN(sizeof(u32)))
				return -EINVAL;
			con->op = *(u32 *)CMSG_DATA(cmsg);
			break;

		case ALG_SET_AEAD_ASSOCLEN:
			if (cmsg->cmsg_len < CMSG_LEN(sizeof(u32)))
				return -EINVAL;
			con->aead_assoclen = *(u32 *)CMSG_DATA(cmsg);
			break;

		default:
			return -EINVAL;
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(af_alg_cmsg_send);

/**
 * af_alg_alloc_tsgl - allocate the TX SGL
 *
 * @sk socket of connection to user space
 * @return: 0 upon success, < 0 upon error
 */
int af_alg_alloc_tsgl(struct sock *sk)
{
	struct alg_sock *ask = alg_sk(sk);
	struct af_alg_ctx *ctx = ask->private;
	struct af_alg_tsgl *sgl;
	struct scatterlist *sg = NULL;

	sgl = list_entry(ctx->tsgl_list.prev, struct af_alg_tsgl, list);
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
EXPORT_SYMBOL_GPL(af_alg_alloc_tsgl);

/**
 * aead_count_tsgl - Count number of TX SG entries
 *
 * The counting starts from the beginning of the SGL to @bytes. If
 * an offset is provided, the counting of the SG entries starts at the offset.
 *
 * @sk socket of connection to user space
 * @bytes Count the number of SG entries holding given number of bytes.
 * @offset Start the counting of SG entries from the given offset.
 * @return Number of TX SG entries found given the constraints
 */
unsigned int af_alg_count_tsgl(struct sock *sk, size_t bytes, size_t offset)
{
	struct alg_sock *ask = alg_sk(sk);
	struct af_alg_ctx *ctx = ask->private;
	struct af_alg_tsgl *sgl, *tmp;
	unsigned int i;
	unsigned int sgl_count = 0;

	if (!bytes)
		return 0;

	list_for_each_entry_safe(sgl, tmp, &ctx->tsgl_list, list) {
		struct scatterlist *sg = sgl->sg;

		for (i = 0; i < sgl->cur; i++) {
			size_t bytes_count;

			/* Skip offset */
			if (offset >= sg[i].length) {
				offset -= sg[i].length;
				bytes -= sg[i].length;
				continue;
			}

			bytes_count = sg[i].length - offset;

			offset = 0;
			sgl_count++;

			/* If we have seen requested number of bytes, stop */
			if (bytes_count >= bytes)
				return sgl_count;

			bytes -= bytes_count;
		}
	}

	return sgl_count;
}
EXPORT_SYMBOL_GPL(af_alg_count_tsgl);

/**
 * aead_pull_tsgl - Release the specified buffers from TX SGL
 *
 * If @dst is non-null, reassign the pages to dst. The caller must release
 * the pages. If @dst_offset is given only reassign the pages to @dst starting
 * at the @dst_offset (byte). The caller must ensure that @dst is large
 * enough (e.g. by using af_alg_count_tsgl with the same offset).
 *
 * @sk socket of connection to user space
 * @used Number of bytes to pull from TX SGL
 * @dst If non-NULL, buffer is reassigned to dst SGL instead of releasing. The
 *	caller must release the buffers in dst.
 * @dst_offset Reassign the TX SGL from given offset. All buffers before
 *	       reaching the offset is released.
 */
void af_alg_pull_tsgl(struct sock *sk, size_t used, struct scatterlist *dst,
		      size_t dst_offset)
{
	struct alg_sock *ask = alg_sk(sk);
	struct af_alg_ctx *ctx = ask->private;
	struct af_alg_tsgl *sgl;
	struct scatterlist *sg;
	unsigned int i, j = 0;

	while (!list_empty(&ctx->tsgl_list)) {
		sgl = list_first_entry(&ctx->tsgl_list, struct af_alg_tsgl,
				       list);
		sg = sgl->sg;

		for (i = 0; i < sgl->cur; i++) {
			size_t plen = min_t(size_t, used, sg[i].length);
			struct page *page = sg_page(sg + i);

			if (!page)
				continue;

			/*
			 * Assumption: caller created af_alg_count_tsgl(len)
			 * SG entries in dst.
			 */
			if (dst) {
				if (dst_offset >= plen) {
					/* discard page before offset */
					dst_offset -= plen;
				} else {
					/* reassign page to dst after offset */
					get_page(page);
					sg_set_page(dst + j, page,
						    plen - dst_offset,
						    sg[i].offset + dst_offset);
					dst_offset = 0;
					j++;
				}
			}

			sg[i].length -= plen;
			sg[i].offset += plen;

			used -= plen;
			ctx->used -= plen;

			if (sg[i].length)
				return;

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
EXPORT_SYMBOL_GPL(af_alg_pull_tsgl);

/**
 * af_alg_free_areq_sgls - Release TX and RX SGLs of the request
 *
 * @areq Request holding the TX and RX SGL
 */
void af_alg_free_areq_sgls(struct af_alg_async_req *areq)
{
	struct sock *sk = areq->sk;
	struct alg_sock *ask = alg_sk(sk);
	struct af_alg_ctx *ctx = ask->private;
	struct af_alg_rsgl *rsgl, *tmp;
	struct scatterlist *tsgl;
	struct scatterlist *sg;
	unsigned int i;

	list_for_each_entry_safe(rsgl, tmp, &areq->rsgl_list, list) {
		ctx->rcvused -= rsgl->sg_num_bytes;
		af_alg_free_sg(&rsgl->sgl);
		list_del(&rsgl->list);
		if (rsgl != &areq->first_rsgl)
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
EXPORT_SYMBOL_GPL(af_alg_free_areq_sgls);

/**
 * af_alg_wait_for_wmem - wait for availability of writable memory
 *
 * @sk socket of connection to user space
 * @flags If MSG_DONTWAIT is set, then only report if function would sleep
 * @return 0 when writable memory is available, < 0 upon error
 */
int af_alg_wait_for_wmem(struct sock *sk, unsigned int flags)
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
		if (sk_wait_event(sk, &timeout, af_alg_writable(sk), &wait)) {
			err = 0;
			break;
		}
	}
	remove_wait_queue(sk_sleep(sk), &wait);

	return err;
}
EXPORT_SYMBOL_GPL(af_alg_wait_for_wmem);

/**
 * af_alg_wmem_wakeup - wakeup caller when writable memory is available
 *
 * @sk socket of connection to user space
 */
void af_alg_wmem_wakeup(struct sock *sk)
{
	struct socket_wq *wq;

	if (!af_alg_writable(sk))
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
EXPORT_SYMBOL_GPL(af_alg_wmem_wakeup);

/**
 * af_alg_wait_for_data - wait for availability of TX data
 *
 * @sk socket of connection to user space
 * @flags If MSG_DONTWAIT is set, then only report if function would sleep
 * @return 0 when writable memory is available, < 0 upon error
 */
int af_alg_wait_for_data(struct sock *sk, unsigned flags)
{
	DEFINE_WAIT_FUNC(wait, woken_wake_function);
	struct alg_sock *ask = alg_sk(sk);
	struct af_alg_ctx *ctx = ask->private;
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
		if (sk_wait_event(sk, &timeout, (ctx->used || !ctx->more),
				  &wait)) {
			err = 0;
			break;
		}
	}
	remove_wait_queue(sk_sleep(sk), &wait);

	sk_clear_bit(SOCKWQ_ASYNC_WAITDATA, sk);

	return err;
}
EXPORT_SYMBOL_GPL(af_alg_wait_for_data);

/**
 * af_alg_data_wakeup - wakeup caller when new data can be sent to kernel
 *
 * @sk socket of connection to user space
 */

void af_alg_data_wakeup(struct sock *sk)
{
	struct alg_sock *ask = alg_sk(sk);
	struct af_alg_ctx *ctx = ask->private;
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
EXPORT_SYMBOL_GPL(af_alg_data_wakeup);

/**
 * af_alg_sendmsg - implementation of sendmsg system call handler
 *
 * The sendmsg system call handler obtains the user data and stores it
 * in ctx->tsgl_list. This implies allocation of the required numbers of
 * struct af_alg_tsgl.
 *
 * In addition, the ctx is filled with the information sent via CMSG.
 *
 * @sock socket of connection to user space
 * @msg message from user space
 * @size size of message from user space
 * @ivsize the size of the IV for the cipher operation to verify that the
 *	   user-space-provided IV has the right size
 * @return the number of copied data upon success, < 0 upon error
 */
int af_alg_sendmsg(struct socket *sock, struct msghdr *msg, size_t size,
		   unsigned int ivsize)
{
	struct sock *sk = sock->sk;
	struct alg_sock *ask = alg_sk(sk);
	struct af_alg_ctx *ctx = ask->private;
	struct af_alg_tsgl *sgl;
	struct af_alg_control con = {};
	long copied = 0;
	bool enc = 0;
	bool init = 0;
	int err = 0;

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
	if (!ctx->more && ctx->used) {
		err = -EINVAL;
		goto unlock;
	}

	if (init) {
		ctx->enc = enc;
		if (con.iv)
			memcpy(ctx->iv, con.iv->iv, ivsize);

		ctx->aead_assoclen = con.aead_assoclen;
	}

	while (size) {
		struct scatterlist *sg;
		size_t len = size;
		size_t plen;

		/* use the existing memory in an allocated page */
		if (ctx->merge) {
			sgl = list_entry(ctx->tsgl_list.prev,
					 struct af_alg_tsgl, list);
			sg = sgl->sg + sgl->cur - 1;
			len = min_t(size_t, len,
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

		if (!af_alg_writable(sk)) {
			err = af_alg_wait_for_wmem(sk, msg->msg_flags);
			if (err)
				goto unlock;
		}

		/* allocate a new page */
		len = min_t(unsigned long, len, af_alg_sndbuf(sk));

		err = af_alg_alloc_tsgl(sk);
		if (err)
			goto unlock;

		sgl = list_entry(ctx->tsgl_list.prev, struct af_alg_tsgl,
				 list);
		sg = sgl->sg;
		if (sgl->cur)
			sg_unmark_end(sg + sgl->cur - 1);

		do {
			unsigned int i = sgl->cur;

			plen = min_t(size_t, len, PAGE_SIZE);

			sg_assign_page(sg + i, alloc_page(GFP_KERNEL));
			if (!sg_page(sg + i)) {
				err = -ENOMEM;
				goto unlock;
			}

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
	af_alg_data_wakeup(sk);
	release_sock(sk);

	return copied ?: err;
}
EXPORT_SYMBOL_GPL(af_alg_sendmsg);

/**
 * af_alg_sendpage - sendpage system call handler
 *
 * This is a generic implementation of sendpage to fill ctx->tsgl_list.
 */
ssize_t af_alg_sendpage(struct socket *sock, struct page *page,
			int offset, size_t size, int flags)
{
	struct sock *sk = sock->sk;
	struct alg_sock *ask = alg_sk(sk);
	struct af_alg_ctx *ctx = ask->private;
	struct af_alg_tsgl *sgl;
	int err = -EINVAL;

	if (flags & MSG_SENDPAGE_NOTLAST)
		flags |= MSG_MORE;

	lock_sock(sk);
	if (!ctx->more && ctx->used)
		goto unlock;

	if (!size)
		goto done;

	if (!af_alg_writable(sk)) {
		err = af_alg_wait_for_wmem(sk, flags);
		if (err)
			goto unlock;
	}

	err = af_alg_alloc_tsgl(sk);
	if (err)
		goto unlock;

	ctx->merge = 0;
	sgl = list_entry(ctx->tsgl_list.prev, struct af_alg_tsgl, list);

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
	af_alg_data_wakeup(sk);
	release_sock(sk);

	return err ?: size;
}
EXPORT_SYMBOL_GPL(af_alg_sendpage);

/**
 * af_alg_free_resources - release resources required for crypto request
 */
void af_alg_free_resources(struct af_alg_async_req *areq)
{
	struct sock *sk = areq->sk;

	af_alg_free_areq_sgls(areq);
	sock_kfree_s(sk, areq, areq->areqlen);
}
EXPORT_SYMBOL_GPL(af_alg_free_resources);

/**
 * af_alg_async_cb - AIO callback handler
 *
 * This handler cleans up the struct af_alg_async_req upon completion of the
 * AIO operation.
 *
 * The number of bytes to be generated with the AIO operation must be set
 * in areq->outlen before the AIO callback handler is invoked.
 */
void af_alg_async_cb(struct crypto_async_request *_req, int err)
{
	struct af_alg_async_req *areq = _req->data;
	struct sock *sk = areq->sk;
	struct kiocb *iocb = areq->iocb;
	unsigned int resultlen;

	/* Buffer size written by crypto operation. */
	resultlen = areq->outlen;

	af_alg_free_resources(areq);
	sock_put(sk);

	iocb->ki_complete(iocb, err ? err : resultlen, 0);
}
EXPORT_SYMBOL_GPL(af_alg_async_cb);

/**
 * af_alg_poll - poll system call handler
 */
unsigned int af_alg_poll(struct file *file, struct socket *sock,
			 poll_table *wait)
{
	struct sock *sk = sock->sk;
	struct alg_sock *ask = alg_sk(sk);
	struct af_alg_ctx *ctx = ask->private;
	unsigned int mask;

	sock_poll_wait(file, sk_sleep(sk), wait);
	mask = 0;

	if (!ctx->more || ctx->used)
		mask |= POLLIN | POLLRDNORM;

	if (af_alg_writable(sk))
		mask |= POLLOUT | POLLWRNORM | POLLWRBAND;

	return mask;
}
EXPORT_SYMBOL_GPL(af_alg_poll);

/**
 * af_alg_alloc_areq - allocate struct af_alg_async_req
 *
 * @sk socket of connection to user space
 * @areqlen size of struct af_alg_async_req + crypto_*_reqsize
 * @return allocated data structure or ERR_PTR upon error
 */
struct af_alg_async_req *af_alg_alloc_areq(struct sock *sk,
					   unsigned int areqlen)
{
	struct af_alg_async_req *areq = sock_kmalloc(sk, areqlen, GFP_KERNEL);

	if (unlikely(!areq))
		return ERR_PTR(-ENOMEM);

	areq->areqlen = areqlen;
	areq->sk = sk;
	areq->last_rsgl = NULL;
	INIT_LIST_HEAD(&areq->rsgl_list);
	areq->tsgl = NULL;
	areq->tsgl_entries = 0;

	return areq;
}
EXPORT_SYMBOL_GPL(af_alg_alloc_areq);

/**
 * af_alg_get_rsgl - create the RX SGL for the output data from the crypto
 *		     operation
 *
 * @sk socket of connection to user space
 * @msg user space message
 * @flags flags used to invoke recvmsg with
 * @areq instance of the cryptographic request that will hold the RX SGL
 * @maxsize maximum number of bytes to be pulled from user space
 * @outlen number of bytes in the RX SGL
 * @return 0 on success, < 0 upon error
 */
int af_alg_get_rsgl(struct sock *sk, struct msghdr *msg, int flags,
		    struct af_alg_async_req *areq, size_t maxsize,
		    size_t *outlen)
{
	struct alg_sock *ask = alg_sk(sk);
	struct af_alg_ctx *ctx = ask->private;
	size_t len = 0;

	while (maxsize > len && msg_data_left(msg)) {
		struct af_alg_rsgl *rsgl;
		size_t seglen;
		int err;

		/* limit the amount of readable buffers */
		if (!af_alg_readable(sk))
			break;

		if (!ctx->used) {
			err = af_alg_wait_for_data(sk, flags);
			if (err)
				return err;
		}

		seglen = min_t(size_t, (maxsize - len),
			       msg_data_left(msg));

		if (list_empty(&areq->rsgl_list)) {
			rsgl = &areq->first_rsgl;
		} else {
			rsgl = sock_kmalloc(sk, sizeof(*rsgl), GFP_KERNEL);
			if (unlikely(!rsgl))
				return -ENOMEM;
		}

		rsgl->sgl.npages = 0;
		list_add_tail(&rsgl->list, &areq->rsgl_list);

		/* make one iovec available as scatterlist */
		err = af_alg_make_sg(&rsgl->sgl, &msg->msg_iter, seglen);
		if (err < 0)
			return err;

		/* chain the new scatterlist with previous one */
		if (areq->last_rsgl)
			af_alg_link_sg(&areq->last_rsgl->sgl, &rsgl->sgl);

		areq->last_rsgl = rsgl;
		len += err;
		ctx->rcvused += err;
		rsgl->sg_num_bytes = err;
		iov_iter_advance(&msg->msg_iter, err);
	}

	*outlen = len;
	return 0;
}
EXPORT_SYMBOL_GPL(af_alg_get_rsgl);

static int __init af_alg_init(void)
{
	int err = proto_register(&alg_proto, 0);

	if (err)
		goto out;

	err = sock_register(&alg_family);
	if (err != 0)
		goto out_unregister_proto;

out:
	return err;

out_unregister_proto:
	proto_unregister(&alg_proto);
	goto out;
}

static void __exit af_alg_exit(void)
{
	sock_unregister(PF_ALG);
	proto_unregister(&alg_proto);
}

module_init(af_alg_init);
module_exit(af_alg_exit);
MODULE_LICENSE("GPL");
MODULE_ALIAS_NETPROTO(AF_ALG);
