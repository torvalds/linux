/*
 * (c) 2017 Stefano Stabellini <stefano@aporeto.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/inet.h>
#include <linux/kthread.h>
#include <linux/list.h>
#include <linux/radix-tree.h>
#include <linux/module.h>
#include <linux/semaphore.h>
#include <linux/wait.h>
#include <net/sock.h>
#include <net/inet_common.h>
#include <net/inet_connection_sock.h>
#include <net/request_sock.h>

#include <xen/events.h>
#include <xen/grant_table.h>
#include <xen/xen.h>
#include <xen/xenbus.h>
#include <xen/interface/io/pvcalls.h>

#define PVCALLS_VERSIONS "1"
#define MAX_RING_ORDER XENBUS_MAX_RING_GRANT_ORDER

struct pvcalls_back_global {
	struct list_head frontends;
	struct semaphore frontends_lock;
} pvcalls_back_global;

/*
 * Per-frontend data structure. It contains pointers to the command
 * ring, its event channel, a list of active sockets and a tree of
 * passive sockets.
 */
struct pvcalls_fedata {
	struct list_head list;
	struct xenbus_device *dev;
	struct xen_pvcalls_sring *sring;
	struct xen_pvcalls_back_ring ring;
	int irq;
	struct list_head socket_mappings;
	struct radix_tree_root socketpass_mappings;
	struct semaphore socket_lock;
};

struct pvcalls_ioworker {
	struct work_struct register_work;
	struct workqueue_struct *wq;
};

struct sock_mapping {
	struct list_head list;
	struct pvcalls_fedata *fedata;
	struct sockpass_mapping *sockpass;
	struct socket *sock;
	uint64_t id;
	grant_ref_t ref;
	struct pvcalls_data_intf *ring;
	void *bytes;
	struct pvcalls_data data;
	uint32_t ring_order;
	int irq;
	atomic_t read;
	atomic_t write;
	atomic_t io;
	atomic_t release;
	void (*saved_data_ready)(struct sock *sk);
	struct pvcalls_ioworker ioworker;
};

struct sockpass_mapping {
	struct list_head list;
	struct pvcalls_fedata *fedata;
	struct socket *sock;
	uint64_t id;
	struct xen_pvcalls_request reqcopy;
	spinlock_t copy_lock;
	struct workqueue_struct *wq;
	struct work_struct register_work;
	void (*saved_data_ready)(struct sock *sk);
};

static irqreturn_t pvcalls_back_conn_event(int irq, void *sock_map);
static int pvcalls_back_release_active(struct xenbus_device *dev,
				       struct pvcalls_fedata *fedata,
				       struct sock_mapping *map);

static void pvcalls_conn_back_read(void *opaque)
{
	struct sock_mapping *map = (struct sock_mapping *)opaque;
	struct msghdr msg;
	struct kvec vec[2];
	RING_IDX cons, prod, size, wanted, array_size, masked_prod, masked_cons;
	int32_t error;
	struct pvcalls_data_intf *intf = map->ring;
	struct pvcalls_data *data = &map->data;
	unsigned long flags;
	int ret;

	array_size = XEN_FLEX_RING_SIZE(map->ring_order);
	cons = intf->in_cons;
	prod = intf->in_prod;
	error = intf->in_error;
	/* read the indexes first, then deal with the data */
	virt_mb();

	if (error)
		return;

	size = pvcalls_queued(prod, cons, array_size);
	if (size >= array_size)
		return;
	spin_lock_irqsave(&map->sock->sk->sk_receive_queue.lock, flags);
	if (skb_queue_empty(&map->sock->sk->sk_receive_queue)) {
		atomic_set(&map->read, 0);
		spin_unlock_irqrestore(&map->sock->sk->sk_receive_queue.lock,
				flags);
		return;
	}
	spin_unlock_irqrestore(&map->sock->sk->sk_receive_queue.lock, flags);
	wanted = array_size - size;
	masked_prod = pvcalls_mask(prod, array_size);
	masked_cons = pvcalls_mask(cons, array_size);

	memset(&msg, 0, sizeof(msg));
	if (masked_prod < masked_cons) {
		vec[0].iov_base = data->in + masked_prod;
		vec[0].iov_len = wanted;
		iov_iter_kvec(&msg.msg_iter, ITER_KVEC|WRITE, vec, 1, wanted);
	} else {
		vec[0].iov_base = data->in + masked_prod;
		vec[0].iov_len = array_size - masked_prod;
		vec[1].iov_base = data->in;
		vec[1].iov_len = wanted - vec[0].iov_len;
		iov_iter_kvec(&msg.msg_iter, ITER_KVEC|WRITE, vec, 2, wanted);
	}

	atomic_set(&map->read, 0);
	ret = inet_recvmsg(map->sock, &msg, wanted, MSG_DONTWAIT);
	WARN_ON(ret > wanted);
	if (ret == -EAGAIN) /* shouldn't happen */
		return;
	if (!ret)
		ret = -ENOTCONN;
	spin_lock_irqsave(&map->sock->sk->sk_receive_queue.lock, flags);
	if (ret > 0 && !skb_queue_empty(&map->sock->sk->sk_receive_queue))
		atomic_inc(&map->read);
	spin_unlock_irqrestore(&map->sock->sk->sk_receive_queue.lock, flags);

	/* write the data, then modify the indexes */
	virt_wmb();
	if (ret < 0)
		intf->in_error = ret;
	else
		intf->in_prod = prod + ret;
	/* update the indexes, then notify the other end */
	virt_wmb();
	notify_remote_via_irq(map->irq);

	return;
}

static void pvcalls_conn_back_write(struct sock_mapping *map)
{
	struct pvcalls_data_intf *intf = map->ring;
	struct pvcalls_data *data = &map->data;
	struct msghdr msg;
	struct kvec vec[2];
	RING_IDX cons, prod, size, array_size;
	int ret;

	cons = intf->out_cons;
	prod = intf->out_prod;
	/* read the indexes before dealing with the data */
	virt_mb();

	array_size = XEN_FLEX_RING_SIZE(map->ring_order);
	size = pvcalls_queued(prod, cons, array_size);
	if (size == 0)
		return;

	memset(&msg, 0, sizeof(msg));
	msg.msg_flags |= MSG_DONTWAIT;
	if (pvcalls_mask(prod, array_size) > pvcalls_mask(cons, array_size)) {
		vec[0].iov_base = data->out + pvcalls_mask(cons, array_size);
		vec[0].iov_len = size;
		iov_iter_kvec(&msg.msg_iter, ITER_KVEC|READ, vec, 1, size);
	} else {
		vec[0].iov_base = data->out + pvcalls_mask(cons, array_size);
		vec[0].iov_len = array_size - pvcalls_mask(cons, array_size);
		vec[1].iov_base = data->out;
		vec[1].iov_len = size - vec[0].iov_len;
		iov_iter_kvec(&msg.msg_iter, ITER_KVEC|READ, vec, 2, size);
	}

	atomic_set(&map->write, 0);
	ret = inet_sendmsg(map->sock, &msg, size);
	if (ret == -EAGAIN || (ret >= 0 && ret < size)) {
		atomic_inc(&map->write);
		atomic_inc(&map->io);
	}
	if (ret == -EAGAIN)
		return;

	/* write the data, then update the indexes */
	virt_wmb();
	if (ret < 0) {
		intf->out_error = ret;
	} else {
		intf->out_error = 0;
		intf->out_cons = cons + ret;
		prod = intf->out_prod;
	}
	/* update the indexes, then notify the other end */
	virt_wmb();
	if (prod != cons + ret)
		atomic_inc(&map->write);
	notify_remote_via_irq(map->irq);
}

static void pvcalls_back_ioworker(struct work_struct *work)
{
	struct pvcalls_ioworker *ioworker = container_of(work,
		struct pvcalls_ioworker, register_work);
	struct sock_mapping *map = container_of(ioworker, struct sock_mapping,
		ioworker);

	while (atomic_read(&map->io) > 0) {
		if (atomic_read(&map->release) > 0) {
			atomic_set(&map->release, 0);
			return;
		}

		if (atomic_read(&map->read) > 0)
			pvcalls_conn_back_read(map);
		if (atomic_read(&map->write) > 0)
			pvcalls_conn_back_write(map);

		atomic_dec(&map->io);
	}
}

static int pvcalls_back_socket(struct xenbus_device *dev,
		struct xen_pvcalls_request *req)
{
	struct pvcalls_fedata *fedata;
	int ret;
	struct xen_pvcalls_response *rsp;

	fedata = dev_get_drvdata(&dev->dev);

	if (req->u.socket.domain != AF_INET ||
	    req->u.socket.type != SOCK_STREAM ||
	    (req->u.socket.protocol != IPPROTO_IP &&
	     req->u.socket.protocol != AF_INET))
		ret = -EAFNOSUPPORT;
	else
		ret = 0;

	/* leave the actual socket allocation for later */

	rsp = RING_GET_RESPONSE(&fedata->ring, fedata->ring.rsp_prod_pvt++);
	rsp->req_id = req->req_id;
	rsp->cmd = req->cmd;
	rsp->u.socket.id = req->u.socket.id;
	rsp->ret = ret;

	return 0;
}

static void pvcalls_sk_state_change(struct sock *sock)
{
	struct sock_mapping *map = sock->sk_user_data;
	struct pvcalls_data_intf *intf;

	if (map == NULL)
		return;

	intf = map->ring;
	intf->in_error = -ENOTCONN;
	notify_remote_via_irq(map->irq);
}

static void pvcalls_sk_data_ready(struct sock *sock)
{
	struct sock_mapping *map = sock->sk_user_data;
	struct pvcalls_ioworker *iow;

	if (map == NULL)
		return;

	iow = &map->ioworker;
	atomic_inc(&map->read);
	atomic_inc(&map->io);
	queue_work(iow->wq, &iow->register_work);
}

static struct sock_mapping *pvcalls_new_active_socket(
		struct pvcalls_fedata *fedata,
		uint64_t id,
		grant_ref_t ref,
		uint32_t evtchn,
		struct socket *sock)
{
	int ret;
	struct sock_mapping *map;
	void *page;

	map = kzalloc(sizeof(*map), GFP_KERNEL);
	if (map == NULL)
		return NULL;

	map->fedata = fedata;
	map->sock = sock;
	map->id = id;
	map->ref = ref;

	ret = xenbus_map_ring_valloc(fedata->dev, &ref, 1, &page);
	if (ret < 0)
		goto out;
	map->ring = page;
	map->ring_order = map->ring->ring_order;
	/* first read the order, then map the data ring */
	virt_rmb();
	if (map->ring_order > MAX_RING_ORDER) {
		pr_warn("%s frontend requested ring_order %u, which is > MAX (%u)\n",
				__func__, map->ring_order, MAX_RING_ORDER);
		goto out;
	}
	ret = xenbus_map_ring_valloc(fedata->dev, map->ring->ref,
				     (1 << map->ring_order), &page);
	if (ret < 0)
		goto out;
	map->bytes = page;

	ret = bind_interdomain_evtchn_to_irqhandler(fedata->dev->otherend_id,
						    evtchn,
						    pvcalls_back_conn_event,
						    0,
						    "pvcalls-backend",
						    map);
	if (ret < 0)
		goto out;
	map->irq = ret;

	map->data.in = map->bytes;
	map->data.out = map->bytes + XEN_FLEX_RING_SIZE(map->ring_order);

	map->ioworker.wq = alloc_workqueue("pvcalls_io", WQ_UNBOUND, 1);
	if (!map->ioworker.wq)
		goto out;
	atomic_set(&map->io, 1);
	INIT_WORK(&map->ioworker.register_work,	pvcalls_back_ioworker);

	down(&fedata->socket_lock);
	list_add_tail(&map->list, &fedata->socket_mappings);
	up(&fedata->socket_lock);

	write_lock_bh(&map->sock->sk->sk_callback_lock);
	map->saved_data_ready = map->sock->sk->sk_data_ready;
	map->sock->sk->sk_user_data = map;
	map->sock->sk->sk_data_ready = pvcalls_sk_data_ready;
	map->sock->sk->sk_state_change = pvcalls_sk_state_change;
	write_unlock_bh(&map->sock->sk->sk_callback_lock);

	return map;
out:
	down(&fedata->socket_lock);
	list_del(&map->list);
	pvcalls_back_release_active(fedata->dev, fedata, map);
	up(&fedata->socket_lock);
	return NULL;
}

static int pvcalls_back_connect(struct xenbus_device *dev,
				struct xen_pvcalls_request *req)
{
	struct pvcalls_fedata *fedata;
	int ret = -EINVAL;
	struct socket *sock;
	struct sock_mapping *map;
	struct xen_pvcalls_response *rsp;
	struct sockaddr *sa = (struct sockaddr *)&req->u.connect.addr;

	fedata = dev_get_drvdata(&dev->dev);

	if (req->u.connect.len < sizeof(sa->sa_family) ||
	    req->u.connect.len > sizeof(req->u.connect.addr) ||
	    sa->sa_family != AF_INET)
		goto out;

	ret = sock_create(AF_INET, SOCK_STREAM, 0, &sock);
	if (ret < 0)
		goto out;
	ret = inet_stream_connect(sock, sa, req->u.connect.len, 0);
	if (ret < 0) {
		sock_release(sock);
		goto out;
	}

	map = pvcalls_new_active_socket(fedata,
					req->u.connect.id,
					req->u.connect.ref,
					req->u.connect.evtchn,
					sock);
	if (!map) {
		ret = -EFAULT;
		sock_release(map->sock);
	}

out:
	rsp = RING_GET_RESPONSE(&fedata->ring, fedata->ring.rsp_prod_pvt++);
	rsp->req_id = req->req_id;
	rsp->cmd = req->cmd;
	rsp->u.connect.id = req->u.connect.id;
	rsp->ret = ret;

	return 0;
}

static int pvcalls_back_release_active(struct xenbus_device *dev,
				       struct pvcalls_fedata *fedata,
				       struct sock_mapping *map)
{
	disable_irq(map->irq);
	if (map->sock->sk != NULL) {
		write_lock_bh(&map->sock->sk->sk_callback_lock);
		map->sock->sk->sk_user_data = NULL;
		map->sock->sk->sk_data_ready = map->saved_data_ready;
		write_unlock_bh(&map->sock->sk->sk_callback_lock);
	}

	atomic_set(&map->release, 1);
	flush_work(&map->ioworker.register_work);

	xenbus_unmap_ring_vfree(dev, map->bytes);
	xenbus_unmap_ring_vfree(dev, (void *)map->ring);
	unbind_from_irqhandler(map->irq, map);

	sock_release(map->sock);
	kfree(map);

	return 0;
}

static int pvcalls_back_release_passive(struct xenbus_device *dev,
					struct pvcalls_fedata *fedata,
					struct sockpass_mapping *mappass)
{
	if (mappass->sock->sk != NULL) {
		write_lock_bh(&mappass->sock->sk->sk_callback_lock);
		mappass->sock->sk->sk_user_data = NULL;
		mappass->sock->sk->sk_data_ready = mappass->saved_data_ready;
		write_unlock_bh(&mappass->sock->sk->sk_callback_lock);
	}
	sock_release(mappass->sock);
	flush_workqueue(mappass->wq);
	destroy_workqueue(mappass->wq);
	kfree(mappass);

	return 0;
}

static int pvcalls_back_release(struct xenbus_device *dev,
				struct xen_pvcalls_request *req)
{
	struct pvcalls_fedata *fedata;
	struct sock_mapping *map, *n;
	struct sockpass_mapping *mappass;
	int ret = 0;
	struct xen_pvcalls_response *rsp;

	fedata = dev_get_drvdata(&dev->dev);

	down(&fedata->socket_lock);
	list_for_each_entry_safe(map, n, &fedata->socket_mappings, list) {
		if (map->id == req->u.release.id) {
			list_del(&map->list);
			up(&fedata->socket_lock);
			ret = pvcalls_back_release_active(dev, fedata, map);
			goto out;
		}
	}
	mappass = radix_tree_lookup(&fedata->socketpass_mappings,
				    req->u.release.id);
	if (mappass != NULL) {
		radix_tree_delete(&fedata->socketpass_mappings, mappass->id);
		up(&fedata->socket_lock);
		ret = pvcalls_back_release_passive(dev, fedata, mappass);
	} else
		up(&fedata->socket_lock);

out:
	rsp = RING_GET_RESPONSE(&fedata->ring, fedata->ring.rsp_prod_pvt++);
	rsp->req_id = req->req_id;
	rsp->u.release.id = req->u.release.id;
	rsp->cmd = req->cmd;
	rsp->ret = ret;
	return 0;
}

static void __pvcalls_back_accept(struct work_struct *work)
{
	struct sockpass_mapping *mappass = container_of(
		work, struct sockpass_mapping, register_work);
	struct sock_mapping *map;
	struct pvcalls_ioworker *iow;
	struct pvcalls_fedata *fedata;
	struct socket *sock;
	struct xen_pvcalls_response *rsp;
	struct xen_pvcalls_request *req;
	int notify;
	int ret = -EINVAL;
	unsigned long flags;

	fedata = mappass->fedata;
	/*
	 * __pvcalls_back_accept can race against pvcalls_back_accept.
	 * We only need to check the value of "cmd" on read. It could be
	 * done atomically, but to simplify the code on the write side, we
	 * use a spinlock.
	 */
	spin_lock_irqsave(&mappass->copy_lock, flags);
	req = &mappass->reqcopy;
	if (req->cmd != PVCALLS_ACCEPT) {
		spin_unlock_irqrestore(&mappass->copy_lock, flags);
		return;
	}
	spin_unlock_irqrestore(&mappass->copy_lock, flags);

	sock = sock_alloc();
	if (sock == NULL)
		goto out_error;
	sock->type = mappass->sock->type;
	sock->ops = mappass->sock->ops;

	ret = inet_accept(mappass->sock, sock, O_NONBLOCK, true);
	if (ret == -EAGAIN) {
		sock_release(sock);
		goto out_error;
	}

	map = pvcalls_new_active_socket(fedata,
					req->u.accept.id_new,
					req->u.accept.ref,
					req->u.accept.evtchn,
					sock);
	if (!map) {
		ret = -EFAULT;
		sock_release(sock);
		goto out_error;
	}

	map->sockpass = mappass;
	iow = &map->ioworker;
	atomic_inc(&map->read);
	atomic_inc(&map->io);
	queue_work(iow->wq, &iow->register_work);

out_error:
	rsp = RING_GET_RESPONSE(&fedata->ring, fedata->ring.rsp_prod_pvt++);
	rsp->req_id = req->req_id;
	rsp->cmd = req->cmd;
	rsp->u.accept.id = req->u.accept.id;
	rsp->ret = ret;
	RING_PUSH_RESPONSES_AND_CHECK_NOTIFY(&fedata->ring, notify);
	if (notify)
		notify_remote_via_irq(fedata->irq);

	mappass->reqcopy.cmd = 0;
}

static void pvcalls_pass_sk_data_ready(struct sock *sock)
{
	struct sockpass_mapping *mappass = sock->sk_user_data;
	struct pvcalls_fedata *fedata;
	struct xen_pvcalls_response *rsp;
	unsigned long flags;
	int notify;

	if (mappass == NULL)
		return;

	fedata = mappass->fedata;
	spin_lock_irqsave(&mappass->copy_lock, flags);
	if (mappass->reqcopy.cmd == PVCALLS_POLL) {
		rsp = RING_GET_RESPONSE(&fedata->ring,
					fedata->ring.rsp_prod_pvt++);
		rsp->req_id = mappass->reqcopy.req_id;
		rsp->u.poll.id = mappass->reqcopy.u.poll.id;
		rsp->cmd = mappass->reqcopy.cmd;
		rsp->ret = 0;

		mappass->reqcopy.cmd = 0;
		spin_unlock_irqrestore(&mappass->copy_lock, flags);

		RING_PUSH_RESPONSES_AND_CHECK_NOTIFY(&fedata->ring, notify);
		if (notify)
			notify_remote_via_irq(mappass->fedata->irq);
	} else {
		spin_unlock_irqrestore(&mappass->copy_lock, flags);
		queue_work(mappass->wq, &mappass->register_work);
	}
}

static int pvcalls_back_bind(struct xenbus_device *dev,
			     struct xen_pvcalls_request *req)
{
	struct pvcalls_fedata *fedata;
	int ret;
	struct sockpass_mapping *map;
	struct xen_pvcalls_response *rsp;

	fedata = dev_get_drvdata(&dev->dev);

	map = kzalloc(sizeof(*map), GFP_KERNEL);
	if (map == NULL) {
		ret = -ENOMEM;
		goto out;
	}

	INIT_WORK(&map->register_work, __pvcalls_back_accept);
	spin_lock_init(&map->copy_lock);
	map->wq = alloc_workqueue("pvcalls_wq", WQ_UNBOUND, 1);
	if (!map->wq) {
		ret = -ENOMEM;
		goto out;
	}

	ret = sock_create(AF_INET, SOCK_STREAM, 0, &map->sock);
	if (ret < 0)
		goto out;

	ret = inet_bind(map->sock, (struct sockaddr *)&req->u.bind.addr,
			req->u.bind.len);
	if (ret < 0)
		goto out;

	map->fedata = fedata;
	map->id = req->u.bind.id;

	down(&fedata->socket_lock);
	ret = radix_tree_insert(&fedata->socketpass_mappings, map->id,
				map);
	up(&fedata->socket_lock);
	if (ret)
		goto out;

	write_lock_bh(&map->sock->sk->sk_callback_lock);
	map->saved_data_ready = map->sock->sk->sk_data_ready;
	map->sock->sk->sk_user_data = map;
	map->sock->sk->sk_data_ready = pvcalls_pass_sk_data_ready;
	write_unlock_bh(&map->sock->sk->sk_callback_lock);

out:
	if (ret) {
		if (map && map->sock)
			sock_release(map->sock);
		if (map && map->wq)
			destroy_workqueue(map->wq);
		kfree(map);
	}
	rsp = RING_GET_RESPONSE(&fedata->ring, fedata->ring.rsp_prod_pvt++);
	rsp->req_id = req->req_id;
	rsp->cmd = req->cmd;
	rsp->u.bind.id = req->u.bind.id;
	rsp->ret = ret;
	return 0;
}

static int pvcalls_back_listen(struct xenbus_device *dev,
			       struct xen_pvcalls_request *req)
{
	struct pvcalls_fedata *fedata;
	int ret = -EINVAL;
	struct sockpass_mapping *map;
	struct xen_pvcalls_response *rsp;

	fedata = dev_get_drvdata(&dev->dev);

	down(&fedata->socket_lock);
	map = radix_tree_lookup(&fedata->socketpass_mappings, req->u.listen.id);
	up(&fedata->socket_lock);
	if (map == NULL)
		goto out;

	ret = inet_listen(map->sock, req->u.listen.backlog);

out:
	rsp = RING_GET_RESPONSE(&fedata->ring, fedata->ring.rsp_prod_pvt++);
	rsp->req_id = req->req_id;
	rsp->cmd = req->cmd;
	rsp->u.listen.id = req->u.listen.id;
	rsp->ret = ret;
	return 0;
}

static int pvcalls_back_accept(struct xenbus_device *dev,
			       struct xen_pvcalls_request *req)
{
	struct pvcalls_fedata *fedata;
	struct sockpass_mapping *mappass;
	int ret = -EINVAL;
	struct xen_pvcalls_response *rsp;
	unsigned long flags;

	fedata = dev_get_drvdata(&dev->dev);

	down(&fedata->socket_lock);
	mappass = radix_tree_lookup(&fedata->socketpass_mappings,
		req->u.accept.id);
	up(&fedata->socket_lock);
	if (mappass == NULL)
		goto out_error;

	/*
	 * Limitation of the current implementation: only support one
	 * concurrent accept or poll call on one socket.
	 */
	spin_lock_irqsave(&mappass->copy_lock, flags);
	if (mappass->reqcopy.cmd != 0) {
		spin_unlock_irqrestore(&mappass->copy_lock, flags);
		ret = -EINTR;
		goto out_error;
	}

	mappass->reqcopy = *req;
	spin_unlock_irqrestore(&mappass->copy_lock, flags);
	queue_work(mappass->wq, &mappass->register_work);

	/* Tell the caller we don't need to send back a notification yet */
	return -1;

out_error:
	rsp = RING_GET_RESPONSE(&fedata->ring, fedata->ring.rsp_prod_pvt++);
	rsp->req_id = req->req_id;
	rsp->cmd = req->cmd;
	rsp->u.accept.id = req->u.accept.id;
	rsp->ret = ret;
	return 0;
}

static int pvcalls_back_poll(struct xenbus_device *dev,
			     struct xen_pvcalls_request *req)
{
	struct pvcalls_fedata *fedata;
	struct sockpass_mapping *mappass;
	struct xen_pvcalls_response *rsp;
	struct inet_connection_sock *icsk;
	struct request_sock_queue *queue;
	unsigned long flags;
	int ret;
	bool data;

	fedata = dev_get_drvdata(&dev->dev);

	down(&fedata->socket_lock);
	mappass = radix_tree_lookup(&fedata->socketpass_mappings,
				    req->u.poll.id);
	up(&fedata->socket_lock);
	if (mappass == NULL)
		return -EINVAL;

	/*
	 * Limitation of the current implementation: only support one
	 * concurrent accept or poll call on one socket.
	 */
	spin_lock_irqsave(&mappass->copy_lock, flags);
	if (mappass->reqcopy.cmd != 0) {
		ret = -EINTR;
		goto out;
	}

	mappass->reqcopy = *req;
	icsk = inet_csk(mappass->sock->sk);
	queue = &icsk->icsk_accept_queue;
	data = queue->rskq_accept_head != NULL;
	if (data) {
		mappass->reqcopy.cmd = 0;
		ret = 0;
		goto out;
	}
	spin_unlock_irqrestore(&mappass->copy_lock, flags);

	/* Tell the caller we don't need to send back a notification yet */
	return -1;

out:
	spin_unlock_irqrestore(&mappass->copy_lock, flags);

	rsp = RING_GET_RESPONSE(&fedata->ring, fedata->ring.rsp_prod_pvt++);
	rsp->req_id = req->req_id;
	rsp->cmd = req->cmd;
	rsp->u.poll.id = req->u.poll.id;
	rsp->ret = ret;
	return 0;
}

static int pvcalls_back_handle_cmd(struct xenbus_device *dev,
				   struct xen_pvcalls_request *req)
{
	int ret = 0;

	switch (req->cmd) {
	case PVCALLS_SOCKET:
		ret = pvcalls_back_socket(dev, req);
		break;
	case PVCALLS_CONNECT:
		ret = pvcalls_back_connect(dev, req);
		break;
	case PVCALLS_RELEASE:
		ret = pvcalls_back_release(dev, req);
		break;
	case PVCALLS_BIND:
		ret = pvcalls_back_bind(dev, req);
		break;
	case PVCALLS_LISTEN:
		ret = pvcalls_back_listen(dev, req);
		break;
	case PVCALLS_ACCEPT:
		ret = pvcalls_back_accept(dev, req);
		break;
	case PVCALLS_POLL:
		ret = pvcalls_back_poll(dev, req);
		break;
	default:
	{
		struct pvcalls_fedata *fedata;
		struct xen_pvcalls_response *rsp;

		fedata = dev_get_drvdata(&dev->dev);
		rsp = RING_GET_RESPONSE(
				&fedata->ring, fedata->ring.rsp_prod_pvt++);
		rsp->req_id = req->req_id;
		rsp->cmd = req->cmd;
		rsp->ret = -ENOTSUPP;
		break;
	}
	}
	return ret;
}

static void pvcalls_back_work(struct pvcalls_fedata *fedata)
{
	int notify, notify_all = 0, more = 1;
	struct xen_pvcalls_request req;
	struct xenbus_device *dev = fedata->dev;

	while (more) {
		while (RING_HAS_UNCONSUMED_REQUESTS(&fedata->ring)) {
			RING_COPY_REQUEST(&fedata->ring,
					  fedata->ring.req_cons++,
					  &req);

			if (!pvcalls_back_handle_cmd(dev, &req)) {
				RING_PUSH_RESPONSES_AND_CHECK_NOTIFY(
					&fedata->ring, notify);
				notify_all += notify;
			}
		}

		if (notify_all) {
			notify_remote_via_irq(fedata->irq);
			notify_all = 0;
		}

		RING_FINAL_CHECK_FOR_REQUESTS(&fedata->ring, more);
	}
}

static irqreturn_t pvcalls_back_event(int irq, void *dev_id)
{
	struct xenbus_device *dev = dev_id;
	struct pvcalls_fedata *fedata = NULL;

	if (dev == NULL)
		return IRQ_HANDLED;

	fedata = dev_get_drvdata(&dev->dev);
	if (fedata == NULL)
		return IRQ_HANDLED;

	pvcalls_back_work(fedata);
	return IRQ_HANDLED;
}

static irqreturn_t pvcalls_back_conn_event(int irq, void *sock_map)
{
	struct sock_mapping *map = sock_map;
	struct pvcalls_ioworker *iow;

	if (map == NULL || map->sock == NULL || map->sock->sk == NULL ||
		map->sock->sk->sk_user_data != map)
		return IRQ_HANDLED;

	iow = &map->ioworker;

	atomic_inc(&map->write);
	atomic_inc(&map->io);
	queue_work(iow->wq, &iow->register_work);

	return IRQ_HANDLED;
}

static int backend_connect(struct xenbus_device *dev)
{
	int err, evtchn;
	grant_ref_t ring_ref;
	struct pvcalls_fedata *fedata = NULL;

	fedata = kzalloc(sizeof(struct pvcalls_fedata), GFP_KERNEL);
	if (!fedata)
		return -ENOMEM;

	fedata->irq = -1;
	err = xenbus_scanf(XBT_NIL, dev->otherend, "port", "%u",
			   &evtchn);
	if (err != 1) {
		err = -EINVAL;
		xenbus_dev_fatal(dev, err, "reading %s/event-channel",
				 dev->otherend);
		goto error;
	}

	err = xenbus_scanf(XBT_NIL, dev->otherend, "ring-ref", "%u", &ring_ref);
	if (err != 1) {
		err = -EINVAL;
		xenbus_dev_fatal(dev, err, "reading %s/ring-ref",
				 dev->otherend);
		goto error;
	}

	err = bind_interdomain_evtchn_to_irq(dev->otherend_id, evtchn);
	if (err < 0)
		goto error;
	fedata->irq = err;

	err = request_threaded_irq(fedata->irq, NULL, pvcalls_back_event,
				   IRQF_ONESHOT, "pvcalls-back", dev);
	if (err < 0)
		goto error;

	err = xenbus_map_ring_valloc(dev, &ring_ref, 1,
				     (void **)&fedata->sring);
	if (err < 0)
		goto error;

	BACK_RING_INIT(&fedata->ring, fedata->sring, XEN_PAGE_SIZE * 1);
	fedata->dev = dev;

	INIT_LIST_HEAD(&fedata->socket_mappings);
	INIT_RADIX_TREE(&fedata->socketpass_mappings, GFP_KERNEL);
	sema_init(&fedata->socket_lock, 1);
	dev_set_drvdata(&dev->dev, fedata);

	down(&pvcalls_back_global.frontends_lock);
	list_add_tail(&fedata->list, &pvcalls_back_global.frontends);
	up(&pvcalls_back_global.frontends_lock);

	return 0;

 error:
	if (fedata->irq >= 0)
		unbind_from_irqhandler(fedata->irq, dev);
	if (fedata->sring != NULL)
		xenbus_unmap_ring_vfree(dev, fedata->sring);
	kfree(fedata);
	return err;
}

static int backend_disconnect(struct xenbus_device *dev)
{
	struct pvcalls_fedata *fedata;
	struct sock_mapping *map, *n;
	struct sockpass_mapping *mappass;
	struct radix_tree_iter iter;
	void **slot;


	fedata = dev_get_drvdata(&dev->dev);

	down(&fedata->socket_lock);
	list_for_each_entry_safe(map, n, &fedata->socket_mappings, list) {
		list_del(&map->list);
		pvcalls_back_release_active(dev, fedata, map);
	}

	radix_tree_for_each_slot(slot, &fedata->socketpass_mappings, &iter, 0) {
		mappass = radix_tree_deref_slot(slot);
		if (!mappass)
			continue;
		if (radix_tree_exception(mappass)) {
			if (radix_tree_deref_retry(mappass))
				slot = radix_tree_iter_retry(&iter);
		} else {
			radix_tree_delete(&fedata->socketpass_mappings,
					  mappass->id);
			pvcalls_back_release_passive(dev, fedata, mappass);
		}
	}
	up(&fedata->socket_lock);

	unbind_from_irqhandler(fedata->irq, dev);
	xenbus_unmap_ring_vfree(dev, fedata->sring);

	list_del(&fedata->list);
	kfree(fedata);
	dev_set_drvdata(&dev->dev, NULL);

	return 0;
}

static int pvcalls_back_probe(struct xenbus_device *dev,
			      const struct xenbus_device_id *id)
{
	int err, abort;
	struct xenbus_transaction xbt;

again:
	abort = 1;

	err = xenbus_transaction_start(&xbt);
	if (err) {
		pr_warn("%s cannot create xenstore transaction\n", __func__);
		return err;
	}

	err = xenbus_printf(xbt, dev->nodename, "versions", "%s",
			    PVCALLS_VERSIONS);
	if (err) {
		pr_warn("%s write out 'versions' failed\n", __func__);
		goto abort;
	}

	err = xenbus_printf(xbt, dev->nodename, "max-page-order", "%u",
			    MAX_RING_ORDER);
	if (err) {
		pr_warn("%s write out 'max-page-order' failed\n", __func__);
		goto abort;
	}

	err = xenbus_printf(xbt, dev->nodename, "function-calls",
			    XENBUS_FUNCTIONS_CALLS);
	if (err) {
		pr_warn("%s write out 'function-calls' failed\n", __func__);
		goto abort;
	}

	abort = 0;
abort:
	err = xenbus_transaction_end(xbt, abort);
	if (err) {
		if (err == -EAGAIN && !abort)
			goto again;
		pr_warn("%s cannot complete xenstore transaction\n", __func__);
		return err;
	}

	if (abort)
		return -EFAULT;

	xenbus_switch_state(dev, XenbusStateInitWait);

	return 0;
}

static void set_backend_state(struct xenbus_device *dev,
			      enum xenbus_state state)
{
	while (dev->state != state) {
		switch (dev->state) {
		case XenbusStateClosed:
			switch (state) {
			case XenbusStateInitWait:
			case XenbusStateConnected:
				xenbus_switch_state(dev, XenbusStateInitWait);
				break;
			case XenbusStateClosing:
				xenbus_switch_state(dev, XenbusStateClosing);
				break;
			default:
				WARN_ON(1);
			}
			break;
		case XenbusStateInitWait:
		case XenbusStateInitialised:
			switch (state) {
			case XenbusStateConnected:
				backend_connect(dev);
				xenbus_switch_state(dev, XenbusStateConnected);
				break;
			case XenbusStateClosing:
			case XenbusStateClosed:
				xenbus_switch_state(dev, XenbusStateClosing);
				break;
			default:
				WARN_ON(1);
			}
			break;
		case XenbusStateConnected:
			switch (state) {
			case XenbusStateInitWait:
			case XenbusStateClosing:
			case XenbusStateClosed:
				down(&pvcalls_back_global.frontends_lock);
				backend_disconnect(dev);
				up(&pvcalls_back_global.frontends_lock);
				xenbus_switch_state(dev, XenbusStateClosing);
				break;
			default:
				WARN_ON(1);
			}
			break;
		case XenbusStateClosing:
			switch (state) {
			case XenbusStateInitWait:
			case XenbusStateConnected:
			case XenbusStateClosed:
				xenbus_switch_state(dev, XenbusStateClosed);
				break;
			default:
				WARN_ON(1);
			}
			break;
		default:
			WARN_ON(1);
		}
	}
}

static void pvcalls_back_changed(struct xenbus_device *dev,
				 enum xenbus_state frontend_state)
{
	switch (frontend_state) {
	case XenbusStateInitialising:
		set_backend_state(dev, XenbusStateInitWait);
		break;

	case XenbusStateInitialised:
	case XenbusStateConnected:
		set_backend_state(dev, XenbusStateConnected);
		break;

	case XenbusStateClosing:
		set_backend_state(dev, XenbusStateClosing);
		break;

	case XenbusStateClosed:
		set_backend_state(dev, XenbusStateClosed);
		if (xenbus_dev_is_online(dev))
			break;
		device_unregister(&dev->dev);
		break;
	case XenbusStateUnknown:
		set_backend_state(dev, XenbusStateClosed);
		device_unregister(&dev->dev);
		break;

	default:
		xenbus_dev_fatal(dev, -EINVAL, "saw state %d at frontend",
				 frontend_state);
		break;
	}
}

static int pvcalls_back_remove(struct xenbus_device *dev)
{
	return 0;
}

static int pvcalls_back_uevent(struct xenbus_device *xdev,
			       struct kobj_uevent_env *env)
{
	return 0;
}

static const struct xenbus_device_id pvcalls_back_ids[] = {
	{ "pvcalls" },
	{ "" }
};

static struct xenbus_driver pvcalls_back_driver = {
	.ids = pvcalls_back_ids,
	.probe = pvcalls_back_probe,
	.remove = pvcalls_back_remove,
	.uevent = pvcalls_back_uevent,
	.otherend_changed = pvcalls_back_changed,
};

static int __init pvcalls_back_init(void)
{
	int ret;

	if (!xen_domain())
		return -ENODEV;

	ret = xenbus_register_backend(&pvcalls_back_driver);
	if (ret < 0)
		return ret;

	sema_init(&pvcalls_back_global.frontends_lock, 1);
	INIT_LIST_HEAD(&pvcalls_back_global.frontends);
	return 0;
}
module_init(pvcalls_back_init);

static void __exit pvcalls_back_fin(void)
{
	struct pvcalls_fedata *fedata, *nfedata;

	down(&pvcalls_back_global.frontends_lock);
	list_for_each_entry_safe(fedata, nfedata,
				 &pvcalls_back_global.frontends, list) {
		backend_disconnect(fedata->dev);
	}
	up(&pvcalls_back_global.frontends_lock);

	xenbus_unregister_driver(&pvcalls_back_driver);
}

module_exit(pvcalls_back_fin);

MODULE_DESCRIPTION("Xen PV Calls backend driver");
MODULE_AUTHOR("Stefano Stabellini <sstabellini@kernel.org>");
MODULE_LICENSE("GPL");
