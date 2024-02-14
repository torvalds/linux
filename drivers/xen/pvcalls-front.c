// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * (c) 2017 Stefano Stabellini <stefano@aporeto.com>
 */

#include <linux/module.h>
#include <linux/net.h>
#include <linux/socket.h>

#include <net/sock.h>

#include <xen/events.h>
#include <xen/grant_table.h>
#include <xen/xen.h>
#include <xen/xenbus.h>
#include <xen/interface/io/pvcalls.h>

#include "pvcalls-front.h"

#define PVCALLS_INVALID_ID UINT_MAX
#define PVCALLS_RING_ORDER XENBUS_MAX_RING_GRANT_ORDER
#define PVCALLS_NR_RSP_PER_RING __CONST_RING_SIZE(xen_pvcalls, XEN_PAGE_SIZE)
#define PVCALLS_FRONT_MAX_SPIN 5000

static struct proto pvcalls_proto = {
	.name	= "PVCalls",
	.owner	= THIS_MODULE,
	.obj_size = sizeof(struct sock),
};

struct pvcalls_bedata {
	struct xen_pvcalls_front_ring ring;
	grant_ref_t ref;
	int irq;

	struct list_head socket_mappings;
	spinlock_t socket_lock;

	wait_queue_head_t inflight_req;
	struct xen_pvcalls_response rsp[PVCALLS_NR_RSP_PER_RING];
};
/* Only one front/back connection supported. */
static struct xenbus_device *pvcalls_front_dev;
static atomic_t pvcalls_refcount;

/* first increment refcount, then proceed */
#define pvcalls_enter() {               \
	atomic_inc(&pvcalls_refcount);      \
}

/* first complete other operations, then decrement refcount */
#define pvcalls_exit() {                \
	atomic_dec(&pvcalls_refcount);      \
}

struct sock_mapping {
	bool active_socket;
	struct list_head list;
	struct socket *sock;
	atomic_t refcount;
	union {
		struct {
			int irq;
			grant_ref_t ref;
			struct pvcalls_data_intf *ring;
			struct pvcalls_data data;
			struct mutex in_mutex;
			struct mutex out_mutex;

			wait_queue_head_t inflight_conn_req;
		} active;
		struct {
		/*
		 * Socket status, needs to be 64-bit aligned due to the
		 * test_and_* functions which have this requirement on arm64.
		 */
#define PVCALLS_STATUS_UNINITALIZED  0
#define PVCALLS_STATUS_BIND          1
#define PVCALLS_STATUS_LISTEN        2
			uint8_t status __attribute__((aligned(8)));
		/*
		 * Internal state-machine flags.
		 * Only one accept operation can be inflight for a socket.
		 * Only one poll operation can be inflight for a given socket.
		 * flags needs to be 64-bit aligned due to the test_and_*
		 * functions which have this requirement on arm64.
		 */
#define PVCALLS_FLAG_ACCEPT_INFLIGHT 0
#define PVCALLS_FLAG_POLL_INFLIGHT   1
#define PVCALLS_FLAG_POLL_RET        2
			uint8_t flags __attribute__((aligned(8)));
			uint32_t inflight_req_id;
			struct sock_mapping *accept_map;
			wait_queue_head_t inflight_accept_req;
		} passive;
	};
};

static inline struct sock_mapping *pvcalls_enter_sock(struct socket *sock)
{
	struct sock_mapping *map;

	if (!pvcalls_front_dev ||
		dev_get_drvdata(&pvcalls_front_dev->dev) == NULL)
		return ERR_PTR(-ENOTCONN);

	map = (struct sock_mapping *)sock->sk->sk_send_head;
	if (map == NULL)
		return ERR_PTR(-ENOTSOCK);

	pvcalls_enter();
	atomic_inc(&map->refcount);
	return map;
}

static inline void pvcalls_exit_sock(struct socket *sock)
{
	struct sock_mapping *map;

	map = (struct sock_mapping *)sock->sk->sk_send_head;
	atomic_dec(&map->refcount);
	pvcalls_exit();
}

static inline int get_request(struct pvcalls_bedata *bedata, int *req_id)
{
	*req_id = bedata->ring.req_prod_pvt & (RING_SIZE(&bedata->ring) - 1);
	if (RING_FULL(&bedata->ring) ||
	    bedata->rsp[*req_id].req_id != PVCALLS_INVALID_ID)
		return -EAGAIN;
	return 0;
}

static bool pvcalls_front_write_todo(struct sock_mapping *map)
{
	struct pvcalls_data_intf *intf = map->active.ring;
	RING_IDX cons, prod, size = XEN_FLEX_RING_SIZE(PVCALLS_RING_ORDER);
	int32_t error;

	error = intf->out_error;
	if (error == -ENOTCONN)
		return false;
	if (error != 0)
		return true;

	cons = intf->out_cons;
	prod = intf->out_prod;
	return !!(size - pvcalls_queued(prod, cons, size));
}

static bool pvcalls_front_read_todo(struct sock_mapping *map)
{
	struct pvcalls_data_intf *intf = map->active.ring;
	RING_IDX cons, prod;
	int32_t error;

	cons = intf->in_cons;
	prod = intf->in_prod;
	error = intf->in_error;
	return (error != 0 ||
		pvcalls_queued(prod, cons,
			       XEN_FLEX_RING_SIZE(PVCALLS_RING_ORDER)) != 0);
}

static irqreturn_t pvcalls_front_event_handler(int irq, void *dev_id)
{
	struct xenbus_device *dev = dev_id;
	struct pvcalls_bedata *bedata;
	struct xen_pvcalls_response *rsp;
	uint8_t *src, *dst;
	int req_id = 0, more = 0, done = 0;

	if (dev == NULL)
		return IRQ_HANDLED;

	pvcalls_enter();
	bedata = dev_get_drvdata(&dev->dev);
	if (bedata == NULL) {
		pvcalls_exit();
		return IRQ_HANDLED;
	}

again:
	while (RING_HAS_UNCONSUMED_RESPONSES(&bedata->ring)) {
		rsp = RING_GET_RESPONSE(&bedata->ring, bedata->ring.rsp_cons);

		req_id = rsp->req_id;
		if (rsp->cmd == PVCALLS_POLL) {
			struct sock_mapping *map = (struct sock_mapping *)(uintptr_t)
						   rsp->u.poll.id;

			clear_bit(PVCALLS_FLAG_POLL_INFLIGHT,
				  (void *)&map->passive.flags);
			/*
			 * clear INFLIGHT, then set RET. It pairs with
			 * the checks at the beginning of
			 * pvcalls_front_poll_passive.
			 */
			smp_wmb();
			set_bit(PVCALLS_FLAG_POLL_RET,
				(void *)&map->passive.flags);
		} else {
			dst = (uint8_t *)&bedata->rsp[req_id] +
			      sizeof(rsp->req_id);
			src = (uint8_t *)rsp + sizeof(rsp->req_id);
			memcpy(dst, src, sizeof(*rsp) - sizeof(rsp->req_id));
			/*
			 * First copy the rest of the data, then req_id. It is
			 * paired with the barrier when accessing bedata->rsp.
			 */
			smp_wmb();
			bedata->rsp[req_id].req_id = req_id;
		}

		done = 1;
		bedata->ring.rsp_cons++;
	}

	RING_FINAL_CHECK_FOR_RESPONSES(&bedata->ring, more);
	if (more)
		goto again;
	if (done)
		wake_up(&bedata->inflight_req);
	pvcalls_exit();
	return IRQ_HANDLED;
}

static void free_active_ring(struct sock_mapping *map);

static void pvcalls_front_free_map(struct pvcalls_bedata *bedata,
				   struct sock_mapping *map)
{
	int i;

	unbind_from_irqhandler(map->active.irq, map);

	spin_lock(&bedata->socket_lock);
	if (!list_empty(&map->list))
		list_del_init(&map->list);
	spin_unlock(&bedata->socket_lock);

	for (i = 0; i < (1 << PVCALLS_RING_ORDER); i++)
		gnttab_end_foreign_access(map->active.ring->ref[i], NULL);
	gnttab_end_foreign_access(map->active.ref, NULL);
	free_active_ring(map);

	kfree(map);
}

static irqreturn_t pvcalls_front_conn_handler(int irq, void *sock_map)
{
	struct sock_mapping *map = sock_map;

	if (map == NULL)
		return IRQ_HANDLED;

	wake_up_interruptible(&map->active.inflight_conn_req);

	return IRQ_HANDLED;
}

int pvcalls_front_socket(struct socket *sock)
{
	struct pvcalls_bedata *bedata;
	struct sock_mapping *map = NULL;
	struct xen_pvcalls_request *req;
	int notify, req_id, ret;

	/*
	 * PVCalls only supports domain AF_INET,
	 * type SOCK_STREAM and protocol 0 sockets for now.
	 *
	 * Check socket type here, AF_INET and protocol checks are done
	 * by the caller.
	 */
	if (sock->type != SOCK_STREAM)
		return -EOPNOTSUPP;

	pvcalls_enter();
	if (!pvcalls_front_dev) {
		pvcalls_exit();
		return -EACCES;
	}
	bedata = dev_get_drvdata(&pvcalls_front_dev->dev);

	map = kzalloc(sizeof(*map), GFP_KERNEL);
	if (map == NULL) {
		pvcalls_exit();
		return -ENOMEM;
	}

	spin_lock(&bedata->socket_lock);

	ret = get_request(bedata, &req_id);
	if (ret < 0) {
		kfree(map);
		spin_unlock(&bedata->socket_lock);
		pvcalls_exit();
		return ret;
	}

	/*
	 * sock->sk->sk_send_head is not used for ip sockets: reuse the
	 * field to store a pointer to the struct sock_mapping
	 * corresponding to the socket. This way, we can easily get the
	 * struct sock_mapping from the struct socket.
	 */
	sock->sk->sk_send_head = (void *)map;
	list_add_tail(&map->list, &bedata->socket_mappings);

	req = RING_GET_REQUEST(&bedata->ring, req_id);
	req->req_id = req_id;
	req->cmd = PVCALLS_SOCKET;
	req->u.socket.id = (uintptr_t) map;
	req->u.socket.domain = AF_INET;
	req->u.socket.type = SOCK_STREAM;
	req->u.socket.protocol = IPPROTO_IP;

	bedata->ring.req_prod_pvt++;
	RING_PUSH_REQUESTS_AND_CHECK_NOTIFY(&bedata->ring, notify);
	spin_unlock(&bedata->socket_lock);
	if (notify)
		notify_remote_via_irq(bedata->irq);

	wait_event(bedata->inflight_req,
		   READ_ONCE(bedata->rsp[req_id].req_id) == req_id);

	/* read req_id, then the content */
	smp_rmb();
	ret = bedata->rsp[req_id].ret;
	bedata->rsp[req_id].req_id = PVCALLS_INVALID_ID;

	pvcalls_exit();
	return ret;
}

static void free_active_ring(struct sock_mapping *map)
{
	if (!map->active.ring)
		return;

	free_pages_exact(map->active.data.in,
			 PAGE_SIZE << map->active.ring->ring_order);
	free_page((unsigned long)map->active.ring);
}

static int alloc_active_ring(struct sock_mapping *map)
{
	void *bytes;

	map->active.ring = (struct pvcalls_data_intf *)
		get_zeroed_page(GFP_KERNEL);
	if (!map->active.ring)
		goto out;

	map->active.ring->ring_order = PVCALLS_RING_ORDER;
	bytes = alloc_pages_exact(PAGE_SIZE << PVCALLS_RING_ORDER,
				  GFP_KERNEL | __GFP_ZERO);
	if (!bytes)
		goto out;

	map->active.data.in = bytes;
	map->active.data.out = bytes +
		XEN_FLEX_RING_SIZE(PVCALLS_RING_ORDER);

	return 0;

out:
	free_active_ring(map);
	return -ENOMEM;
}

static int create_active(struct sock_mapping *map, evtchn_port_t *evtchn)
{
	void *bytes;
	int ret, irq = -1, i;

	*evtchn = 0;
	init_waitqueue_head(&map->active.inflight_conn_req);

	bytes = map->active.data.in;
	for (i = 0; i < (1 << PVCALLS_RING_ORDER); i++)
		map->active.ring->ref[i] = gnttab_grant_foreign_access(
			pvcalls_front_dev->otherend_id,
			pfn_to_gfn(virt_to_pfn(bytes) + i), 0);

	map->active.ref = gnttab_grant_foreign_access(
		pvcalls_front_dev->otherend_id,
		pfn_to_gfn(virt_to_pfn((void *)map->active.ring)), 0);

	ret = xenbus_alloc_evtchn(pvcalls_front_dev, evtchn);
	if (ret)
		goto out_error;
	irq = bind_evtchn_to_irqhandler(*evtchn, pvcalls_front_conn_handler,
					0, "pvcalls-frontend", map);
	if (irq < 0) {
		ret = irq;
		goto out_error;
	}

	map->active.irq = irq;
	map->active_socket = true;
	mutex_init(&map->active.in_mutex);
	mutex_init(&map->active.out_mutex);

	return 0;

out_error:
	if (*evtchn > 0)
		xenbus_free_evtchn(pvcalls_front_dev, *evtchn);
	return ret;
}

int pvcalls_front_connect(struct socket *sock, struct sockaddr *addr,
				int addr_len, int flags)
{
	struct pvcalls_bedata *bedata;
	struct sock_mapping *map = NULL;
	struct xen_pvcalls_request *req;
	int notify, req_id, ret;
	evtchn_port_t evtchn;

	if (addr->sa_family != AF_INET || sock->type != SOCK_STREAM)
		return -EOPNOTSUPP;

	map = pvcalls_enter_sock(sock);
	if (IS_ERR(map))
		return PTR_ERR(map);

	bedata = dev_get_drvdata(&pvcalls_front_dev->dev);
	ret = alloc_active_ring(map);
	if (ret < 0) {
		pvcalls_exit_sock(sock);
		return ret;
	}

	spin_lock(&bedata->socket_lock);
	ret = get_request(bedata, &req_id);
	if (ret < 0) {
		spin_unlock(&bedata->socket_lock);
		free_active_ring(map);
		pvcalls_exit_sock(sock);
		return ret;
	}
	ret = create_active(map, &evtchn);
	if (ret < 0) {
		spin_unlock(&bedata->socket_lock);
		free_active_ring(map);
		pvcalls_exit_sock(sock);
		return ret;
	}

	req = RING_GET_REQUEST(&bedata->ring, req_id);
	req->req_id = req_id;
	req->cmd = PVCALLS_CONNECT;
	req->u.connect.id = (uintptr_t)map;
	req->u.connect.len = addr_len;
	req->u.connect.flags = flags;
	req->u.connect.ref = map->active.ref;
	req->u.connect.evtchn = evtchn;
	memcpy(req->u.connect.addr, addr, sizeof(*addr));

	map->sock = sock;

	bedata->ring.req_prod_pvt++;
	RING_PUSH_REQUESTS_AND_CHECK_NOTIFY(&bedata->ring, notify);
	spin_unlock(&bedata->socket_lock);

	if (notify)
		notify_remote_via_irq(bedata->irq);

	wait_event(bedata->inflight_req,
		   READ_ONCE(bedata->rsp[req_id].req_id) == req_id);

	/* read req_id, then the content */
	smp_rmb();
	ret = bedata->rsp[req_id].ret;
	bedata->rsp[req_id].req_id = PVCALLS_INVALID_ID;
	pvcalls_exit_sock(sock);
	return ret;
}

static int __write_ring(struct pvcalls_data_intf *intf,
			struct pvcalls_data *data,
			struct iov_iter *msg_iter,
			int len)
{
	RING_IDX cons, prod, size, masked_prod, masked_cons;
	RING_IDX array_size = XEN_FLEX_RING_SIZE(PVCALLS_RING_ORDER);
	int32_t error;

	error = intf->out_error;
	if (error < 0)
		return error;
	cons = intf->out_cons;
	prod = intf->out_prod;
	/* read indexes before continuing */
	virt_mb();

	size = pvcalls_queued(prod, cons, array_size);
	if (size > array_size)
		return -EINVAL;
	if (size == array_size)
		return 0;
	if (len > array_size - size)
		len = array_size - size;

	masked_prod = pvcalls_mask(prod, array_size);
	masked_cons = pvcalls_mask(cons, array_size);

	if (masked_prod < masked_cons) {
		len = copy_from_iter(data->out + masked_prod, len, msg_iter);
	} else {
		if (len > array_size - masked_prod) {
			int ret = copy_from_iter(data->out + masked_prod,
				       array_size - masked_prod, msg_iter);
			if (ret != array_size - masked_prod) {
				len = ret;
				goto out;
			}
			len = ret + copy_from_iter(data->out, len - ret, msg_iter);
		} else {
			len = copy_from_iter(data->out + masked_prod, len, msg_iter);
		}
	}
out:
	/* write to ring before updating pointer */
	virt_wmb();
	intf->out_prod += len;

	return len;
}

int pvcalls_front_sendmsg(struct socket *sock, struct msghdr *msg,
			  size_t len)
{
	struct sock_mapping *map;
	int sent, tot_sent = 0;
	int count = 0, flags;

	flags = msg->msg_flags;
	if (flags & (MSG_CONFIRM|MSG_DONTROUTE|MSG_EOR|MSG_OOB))
		return -EOPNOTSUPP;

	map = pvcalls_enter_sock(sock);
	if (IS_ERR(map))
		return PTR_ERR(map);

	mutex_lock(&map->active.out_mutex);
	if ((flags & MSG_DONTWAIT) && !pvcalls_front_write_todo(map)) {
		mutex_unlock(&map->active.out_mutex);
		pvcalls_exit_sock(sock);
		return -EAGAIN;
	}
	if (len > INT_MAX)
		len = INT_MAX;

again:
	count++;
	sent = __write_ring(map->active.ring,
			    &map->active.data, &msg->msg_iter,
			    len);
	if (sent > 0) {
		len -= sent;
		tot_sent += sent;
		notify_remote_via_irq(map->active.irq);
	}
	if (sent >= 0 && len > 0 && count < PVCALLS_FRONT_MAX_SPIN)
		goto again;
	if (sent < 0)
		tot_sent = sent;

	mutex_unlock(&map->active.out_mutex);
	pvcalls_exit_sock(sock);
	return tot_sent;
}

static int __read_ring(struct pvcalls_data_intf *intf,
		       struct pvcalls_data *data,
		       struct iov_iter *msg_iter,
		       size_t len, int flags)
{
	RING_IDX cons, prod, size, masked_prod, masked_cons;
	RING_IDX array_size = XEN_FLEX_RING_SIZE(PVCALLS_RING_ORDER);
	int32_t error;

	cons = intf->in_cons;
	prod = intf->in_prod;
	error = intf->in_error;
	/* get pointers before reading from the ring */
	virt_rmb();

	size = pvcalls_queued(prod, cons, array_size);
	masked_prod = pvcalls_mask(prod, array_size);
	masked_cons = pvcalls_mask(cons, array_size);

	if (size == 0)
		return error ?: size;

	if (len > size)
		len = size;

	if (masked_prod > masked_cons) {
		len = copy_to_iter(data->in + masked_cons, len, msg_iter);
	} else {
		if (len > (array_size - masked_cons)) {
			int ret = copy_to_iter(data->in + masked_cons,
				     array_size - masked_cons, msg_iter);
			if (ret != array_size - masked_cons) {
				len = ret;
				goto out;
			}
			len = ret + copy_to_iter(data->in, len - ret, msg_iter);
		} else {
			len = copy_to_iter(data->in + masked_cons, len, msg_iter);
		}
	}
out:
	/* read data from the ring before increasing the index */
	virt_mb();
	if (!(flags & MSG_PEEK))
		intf->in_cons += len;

	return len;
}

int pvcalls_front_recvmsg(struct socket *sock, struct msghdr *msg, size_t len,
		     int flags)
{
	int ret;
	struct sock_mapping *map;

	if (flags & (MSG_CMSG_CLOEXEC|MSG_ERRQUEUE|MSG_OOB|MSG_TRUNC))
		return -EOPNOTSUPP;

	map = pvcalls_enter_sock(sock);
	if (IS_ERR(map))
		return PTR_ERR(map);

	mutex_lock(&map->active.in_mutex);
	if (len > XEN_FLEX_RING_SIZE(PVCALLS_RING_ORDER))
		len = XEN_FLEX_RING_SIZE(PVCALLS_RING_ORDER);

	while (!(flags & MSG_DONTWAIT) && !pvcalls_front_read_todo(map)) {
		wait_event_interruptible(map->active.inflight_conn_req,
					 pvcalls_front_read_todo(map));
	}
	ret = __read_ring(map->active.ring, &map->active.data,
			  &msg->msg_iter, len, flags);

	if (ret > 0)
		notify_remote_via_irq(map->active.irq);
	if (ret == 0)
		ret = (flags & MSG_DONTWAIT) ? -EAGAIN : 0;
	if (ret == -ENOTCONN)
		ret = 0;

	mutex_unlock(&map->active.in_mutex);
	pvcalls_exit_sock(sock);
	return ret;
}

int pvcalls_front_bind(struct socket *sock, struct sockaddr *addr, int addr_len)
{
	struct pvcalls_bedata *bedata;
	struct sock_mapping *map = NULL;
	struct xen_pvcalls_request *req;
	int notify, req_id, ret;

	if (addr->sa_family != AF_INET || sock->type != SOCK_STREAM)
		return -EOPNOTSUPP;

	map = pvcalls_enter_sock(sock);
	if (IS_ERR(map))
		return PTR_ERR(map);
	bedata = dev_get_drvdata(&pvcalls_front_dev->dev);

	spin_lock(&bedata->socket_lock);
	ret = get_request(bedata, &req_id);
	if (ret < 0) {
		spin_unlock(&bedata->socket_lock);
		pvcalls_exit_sock(sock);
		return ret;
	}
	req = RING_GET_REQUEST(&bedata->ring, req_id);
	req->req_id = req_id;
	map->sock = sock;
	req->cmd = PVCALLS_BIND;
	req->u.bind.id = (uintptr_t)map;
	memcpy(req->u.bind.addr, addr, sizeof(*addr));
	req->u.bind.len = addr_len;

	init_waitqueue_head(&map->passive.inflight_accept_req);

	map->active_socket = false;

	bedata->ring.req_prod_pvt++;
	RING_PUSH_REQUESTS_AND_CHECK_NOTIFY(&bedata->ring, notify);
	spin_unlock(&bedata->socket_lock);
	if (notify)
		notify_remote_via_irq(bedata->irq);

	wait_event(bedata->inflight_req,
		   READ_ONCE(bedata->rsp[req_id].req_id) == req_id);

	/* read req_id, then the content */
	smp_rmb();
	ret = bedata->rsp[req_id].ret;
	bedata->rsp[req_id].req_id = PVCALLS_INVALID_ID;

	map->passive.status = PVCALLS_STATUS_BIND;
	pvcalls_exit_sock(sock);
	return 0;
}

int pvcalls_front_listen(struct socket *sock, int backlog)
{
	struct pvcalls_bedata *bedata;
	struct sock_mapping *map;
	struct xen_pvcalls_request *req;
	int notify, req_id, ret;

	map = pvcalls_enter_sock(sock);
	if (IS_ERR(map))
		return PTR_ERR(map);
	bedata = dev_get_drvdata(&pvcalls_front_dev->dev);

	if (map->passive.status != PVCALLS_STATUS_BIND) {
		pvcalls_exit_sock(sock);
		return -EOPNOTSUPP;
	}

	spin_lock(&bedata->socket_lock);
	ret = get_request(bedata, &req_id);
	if (ret < 0) {
		spin_unlock(&bedata->socket_lock);
		pvcalls_exit_sock(sock);
		return ret;
	}
	req = RING_GET_REQUEST(&bedata->ring, req_id);
	req->req_id = req_id;
	req->cmd = PVCALLS_LISTEN;
	req->u.listen.id = (uintptr_t) map;
	req->u.listen.backlog = backlog;

	bedata->ring.req_prod_pvt++;
	RING_PUSH_REQUESTS_AND_CHECK_NOTIFY(&bedata->ring, notify);
	spin_unlock(&bedata->socket_lock);
	if (notify)
		notify_remote_via_irq(bedata->irq);

	wait_event(bedata->inflight_req,
		   READ_ONCE(bedata->rsp[req_id].req_id) == req_id);

	/* read req_id, then the content */
	smp_rmb();
	ret = bedata->rsp[req_id].ret;
	bedata->rsp[req_id].req_id = PVCALLS_INVALID_ID;

	map->passive.status = PVCALLS_STATUS_LISTEN;
	pvcalls_exit_sock(sock);
	return ret;
}

int pvcalls_front_accept(struct socket *sock, struct socket *newsock, int flags)
{
	struct pvcalls_bedata *bedata;
	struct sock_mapping *map;
	struct sock_mapping *map2 = NULL;
	struct xen_pvcalls_request *req;
	int notify, req_id, ret, nonblock;
	evtchn_port_t evtchn;

	map = pvcalls_enter_sock(sock);
	if (IS_ERR(map))
		return PTR_ERR(map);
	bedata = dev_get_drvdata(&pvcalls_front_dev->dev);

	if (map->passive.status != PVCALLS_STATUS_LISTEN) {
		pvcalls_exit_sock(sock);
		return -EINVAL;
	}

	nonblock = flags & SOCK_NONBLOCK;
	/*
	 * Backend only supports 1 inflight accept request, will return
	 * errors for the others
	 */
	if (test_and_set_bit(PVCALLS_FLAG_ACCEPT_INFLIGHT,
			     (void *)&map->passive.flags)) {
		req_id = READ_ONCE(map->passive.inflight_req_id);
		if (req_id != PVCALLS_INVALID_ID &&
		    READ_ONCE(bedata->rsp[req_id].req_id) == req_id) {
			map2 = map->passive.accept_map;
			goto received;
		}
		if (nonblock) {
			pvcalls_exit_sock(sock);
			return -EAGAIN;
		}
		if (wait_event_interruptible(map->passive.inflight_accept_req,
			!test_and_set_bit(PVCALLS_FLAG_ACCEPT_INFLIGHT,
					  (void *)&map->passive.flags))) {
			pvcalls_exit_sock(sock);
			return -EINTR;
		}
	}

	map2 = kzalloc(sizeof(*map2), GFP_KERNEL);
	if (map2 == NULL) {
		clear_bit(PVCALLS_FLAG_ACCEPT_INFLIGHT,
			  (void *)&map->passive.flags);
		pvcalls_exit_sock(sock);
		return -ENOMEM;
	}
	ret = alloc_active_ring(map2);
	if (ret < 0) {
		clear_bit(PVCALLS_FLAG_ACCEPT_INFLIGHT,
				(void *)&map->passive.flags);
		kfree(map2);
		pvcalls_exit_sock(sock);
		return ret;
	}
	spin_lock(&bedata->socket_lock);
	ret = get_request(bedata, &req_id);
	if (ret < 0) {
		clear_bit(PVCALLS_FLAG_ACCEPT_INFLIGHT,
			  (void *)&map->passive.flags);
		spin_unlock(&bedata->socket_lock);
		free_active_ring(map2);
		kfree(map2);
		pvcalls_exit_sock(sock);
		return ret;
	}

	ret = create_active(map2, &evtchn);
	if (ret < 0) {
		free_active_ring(map2);
		kfree(map2);
		clear_bit(PVCALLS_FLAG_ACCEPT_INFLIGHT,
			  (void *)&map->passive.flags);
		spin_unlock(&bedata->socket_lock);
		pvcalls_exit_sock(sock);
		return ret;
	}
	list_add_tail(&map2->list, &bedata->socket_mappings);

	req = RING_GET_REQUEST(&bedata->ring, req_id);
	req->req_id = req_id;
	req->cmd = PVCALLS_ACCEPT;
	req->u.accept.id = (uintptr_t) map;
	req->u.accept.ref = map2->active.ref;
	req->u.accept.id_new = (uintptr_t) map2;
	req->u.accept.evtchn = evtchn;
	map->passive.accept_map = map2;

	bedata->ring.req_prod_pvt++;
	RING_PUSH_REQUESTS_AND_CHECK_NOTIFY(&bedata->ring, notify);
	spin_unlock(&bedata->socket_lock);
	if (notify)
		notify_remote_via_irq(bedata->irq);
	/* We could check if we have received a response before returning. */
	if (nonblock) {
		WRITE_ONCE(map->passive.inflight_req_id, req_id);
		pvcalls_exit_sock(sock);
		return -EAGAIN;
	}

	if (wait_event_interruptible(bedata->inflight_req,
		READ_ONCE(bedata->rsp[req_id].req_id) == req_id)) {
		pvcalls_exit_sock(sock);
		return -EINTR;
	}
	/* read req_id, then the content */
	smp_rmb();

received:
	map2->sock = newsock;
	newsock->sk = sk_alloc(sock_net(sock->sk), PF_INET, GFP_KERNEL, &pvcalls_proto, false);
	if (!newsock->sk) {
		bedata->rsp[req_id].req_id = PVCALLS_INVALID_ID;
		map->passive.inflight_req_id = PVCALLS_INVALID_ID;
		clear_bit(PVCALLS_FLAG_ACCEPT_INFLIGHT,
			  (void *)&map->passive.flags);
		pvcalls_front_free_map(bedata, map2);
		pvcalls_exit_sock(sock);
		return -ENOMEM;
	}
	newsock->sk->sk_send_head = (void *)map2;

	ret = bedata->rsp[req_id].ret;
	bedata->rsp[req_id].req_id = PVCALLS_INVALID_ID;
	map->passive.inflight_req_id = PVCALLS_INVALID_ID;

	clear_bit(PVCALLS_FLAG_ACCEPT_INFLIGHT, (void *)&map->passive.flags);
	wake_up(&map->passive.inflight_accept_req);

	pvcalls_exit_sock(sock);
	return ret;
}

static __poll_t pvcalls_front_poll_passive(struct file *file,
					       struct pvcalls_bedata *bedata,
					       struct sock_mapping *map,
					       poll_table *wait)
{
	int notify, req_id, ret;
	struct xen_pvcalls_request *req;

	if (test_bit(PVCALLS_FLAG_ACCEPT_INFLIGHT,
		     (void *)&map->passive.flags)) {
		uint32_t req_id = READ_ONCE(map->passive.inflight_req_id);

		if (req_id != PVCALLS_INVALID_ID &&
		    READ_ONCE(bedata->rsp[req_id].req_id) == req_id)
			return EPOLLIN | EPOLLRDNORM;

		poll_wait(file, &map->passive.inflight_accept_req, wait);
		return 0;
	}

	if (test_and_clear_bit(PVCALLS_FLAG_POLL_RET,
			       (void *)&map->passive.flags))
		return EPOLLIN | EPOLLRDNORM;

	/*
	 * First check RET, then INFLIGHT. No barriers necessary to
	 * ensure execution ordering because of the conditional
	 * instructions creating control dependencies.
	 */

	if (test_and_set_bit(PVCALLS_FLAG_POLL_INFLIGHT,
			     (void *)&map->passive.flags)) {
		poll_wait(file, &bedata->inflight_req, wait);
		return 0;
	}

	spin_lock(&bedata->socket_lock);
	ret = get_request(bedata, &req_id);
	if (ret < 0) {
		spin_unlock(&bedata->socket_lock);
		return ret;
	}
	req = RING_GET_REQUEST(&bedata->ring, req_id);
	req->req_id = req_id;
	req->cmd = PVCALLS_POLL;
	req->u.poll.id = (uintptr_t) map;

	bedata->ring.req_prod_pvt++;
	RING_PUSH_REQUESTS_AND_CHECK_NOTIFY(&bedata->ring, notify);
	spin_unlock(&bedata->socket_lock);
	if (notify)
		notify_remote_via_irq(bedata->irq);

	poll_wait(file, &bedata->inflight_req, wait);
	return 0;
}

static __poll_t pvcalls_front_poll_active(struct file *file,
					      struct pvcalls_bedata *bedata,
					      struct sock_mapping *map,
					      poll_table *wait)
{
	__poll_t mask = 0;
	int32_t in_error, out_error;
	struct pvcalls_data_intf *intf = map->active.ring;

	out_error = intf->out_error;
	in_error = intf->in_error;

	poll_wait(file, &map->active.inflight_conn_req, wait);
	if (pvcalls_front_write_todo(map))
		mask |= EPOLLOUT | EPOLLWRNORM;
	if (pvcalls_front_read_todo(map))
		mask |= EPOLLIN | EPOLLRDNORM;
	if (in_error != 0 || out_error != 0)
		mask |= EPOLLERR;

	return mask;
}

__poll_t pvcalls_front_poll(struct file *file, struct socket *sock,
			       poll_table *wait)
{
	struct pvcalls_bedata *bedata;
	struct sock_mapping *map;
	__poll_t ret;

	map = pvcalls_enter_sock(sock);
	if (IS_ERR(map))
		return EPOLLNVAL;
	bedata = dev_get_drvdata(&pvcalls_front_dev->dev);

	if (map->active_socket)
		ret = pvcalls_front_poll_active(file, bedata, map, wait);
	else
		ret = pvcalls_front_poll_passive(file, bedata, map, wait);
	pvcalls_exit_sock(sock);
	return ret;
}

int pvcalls_front_release(struct socket *sock)
{
	struct pvcalls_bedata *bedata;
	struct sock_mapping *map;
	int req_id, notify, ret;
	struct xen_pvcalls_request *req;

	if (sock->sk == NULL)
		return 0;

	map = pvcalls_enter_sock(sock);
	if (IS_ERR(map)) {
		if (PTR_ERR(map) == -ENOTCONN)
			return -EIO;
		else
			return 0;
	}
	bedata = dev_get_drvdata(&pvcalls_front_dev->dev);

	spin_lock(&bedata->socket_lock);
	ret = get_request(bedata, &req_id);
	if (ret < 0) {
		spin_unlock(&bedata->socket_lock);
		pvcalls_exit_sock(sock);
		return ret;
	}
	sock->sk->sk_send_head = NULL;

	req = RING_GET_REQUEST(&bedata->ring, req_id);
	req->req_id = req_id;
	req->cmd = PVCALLS_RELEASE;
	req->u.release.id = (uintptr_t)map;

	bedata->ring.req_prod_pvt++;
	RING_PUSH_REQUESTS_AND_CHECK_NOTIFY(&bedata->ring, notify);
	spin_unlock(&bedata->socket_lock);
	if (notify)
		notify_remote_via_irq(bedata->irq);

	wait_event(bedata->inflight_req,
		   READ_ONCE(bedata->rsp[req_id].req_id) == req_id);

	if (map->active_socket) {
		/*
		 * Set in_error and wake up inflight_conn_req to force
		 * recvmsg waiters to exit.
		 */
		map->active.ring->in_error = -EBADF;
		wake_up_interruptible(&map->active.inflight_conn_req);

		/*
		 * We need to make sure that sendmsg/recvmsg on this socket have
		 * not started before we've cleared sk_send_head here. The
		 * easiest way to guarantee this is to see that no pvcalls
		 * (other than us) is in progress on this socket.
		 */
		while (atomic_read(&map->refcount) > 1)
			cpu_relax();

		pvcalls_front_free_map(bedata, map);
	} else {
		wake_up(&bedata->inflight_req);
		wake_up(&map->passive.inflight_accept_req);

		while (atomic_read(&map->refcount) > 1)
			cpu_relax();

		spin_lock(&bedata->socket_lock);
		list_del(&map->list);
		spin_unlock(&bedata->socket_lock);
		if (READ_ONCE(map->passive.inflight_req_id) != PVCALLS_INVALID_ID &&
			READ_ONCE(map->passive.inflight_req_id) != 0) {
			pvcalls_front_free_map(bedata,
					       map->passive.accept_map);
		}
		kfree(map);
	}
	WRITE_ONCE(bedata->rsp[req_id].req_id, PVCALLS_INVALID_ID);

	pvcalls_exit();
	return 0;
}

static const struct xenbus_device_id pvcalls_front_ids[] = {
	{ "pvcalls" },
	{ "" }
};

static int pvcalls_front_remove(struct xenbus_device *dev)
{
	struct pvcalls_bedata *bedata;
	struct sock_mapping *map = NULL, *n;

	bedata = dev_get_drvdata(&pvcalls_front_dev->dev);
	dev_set_drvdata(&dev->dev, NULL);
	pvcalls_front_dev = NULL;
	if (bedata->irq >= 0)
		unbind_from_irqhandler(bedata->irq, dev);

	list_for_each_entry_safe(map, n, &bedata->socket_mappings, list) {
		map->sock->sk->sk_send_head = NULL;
		if (map->active_socket) {
			map->active.ring->in_error = -EBADF;
			wake_up_interruptible(&map->active.inflight_conn_req);
		}
	}

	smp_mb();
	while (atomic_read(&pvcalls_refcount) > 0)
		cpu_relax();
	list_for_each_entry_safe(map, n, &bedata->socket_mappings, list) {
		if (map->active_socket) {
			/* No need to lock, refcount is 0 */
			pvcalls_front_free_map(bedata, map);
		} else {
			list_del(&map->list);
			kfree(map);
		}
	}
	if (bedata->ref != -1)
		gnttab_end_foreign_access(bedata->ref, NULL);
	kfree(bedata->ring.sring);
	kfree(bedata);
	xenbus_switch_state(dev, XenbusStateClosed);
	return 0;
}

static int pvcalls_front_probe(struct xenbus_device *dev,
			  const struct xenbus_device_id *id)
{
	int ret = -ENOMEM, i;
	evtchn_port_t evtchn;
	unsigned int max_page_order, function_calls, len;
	char *versions;
	grant_ref_t gref_head = 0;
	struct xenbus_transaction xbt;
	struct pvcalls_bedata *bedata = NULL;
	struct xen_pvcalls_sring *sring;

	if (pvcalls_front_dev != NULL) {
		dev_err(&dev->dev, "only one PV Calls connection supported\n");
		return -EINVAL;
	}

	versions = xenbus_read(XBT_NIL, dev->otherend, "versions", &len);
	if (IS_ERR(versions))
		return PTR_ERR(versions);
	if (!len)
		return -EINVAL;
	if (strcmp(versions, "1")) {
		kfree(versions);
		return -EINVAL;
	}
	kfree(versions);
	max_page_order = xenbus_read_unsigned(dev->otherend,
					      "max-page-order", 0);
	if (max_page_order < PVCALLS_RING_ORDER)
		return -ENODEV;
	function_calls = xenbus_read_unsigned(dev->otherend,
					      "function-calls", 0);
	/* See XENBUS_FUNCTIONS_CALLS in pvcalls.h */
	if (function_calls != 1)
		return -ENODEV;
	pr_info("%s max-page-order is %u\n", __func__, max_page_order);

	bedata = kzalloc(sizeof(struct pvcalls_bedata), GFP_KERNEL);
	if (!bedata)
		return -ENOMEM;

	dev_set_drvdata(&dev->dev, bedata);
	pvcalls_front_dev = dev;
	init_waitqueue_head(&bedata->inflight_req);
	INIT_LIST_HEAD(&bedata->socket_mappings);
	spin_lock_init(&bedata->socket_lock);
	bedata->irq = -1;
	bedata->ref = -1;

	for (i = 0; i < PVCALLS_NR_RSP_PER_RING; i++)
		bedata->rsp[i].req_id = PVCALLS_INVALID_ID;

	sring = (struct xen_pvcalls_sring *) __get_free_page(GFP_KERNEL |
							     __GFP_ZERO);
	if (!sring)
		goto error;
	SHARED_RING_INIT(sring);
	FRONT_RING_INIT(&bedata->ring, sring, XEN_PAGE_SIZE);

	ret = xenbus_alloc_evtchn(dev, &evtchn);
	if (ret)
		goto error;

	bedata->irq = bind_evtchn_to_irqhandler(evtchn,
						pvcalls_front_event_handler,
						0, "pvcalls-frontend", dev);
	if (bedata->irq < 0) {
		ret = bedata->irq;
		goto error;
	}

	ret = gnttab_alloc_grant_references(1, &gref_head);
	if (ret < 0)
		goto error;
	ret = gnttab_claim_grant_reference(&gref_head);
	if (ret < 0)
		goto error;
	bedata->ref = ret;
	gnttab_grant_foreign_access_ref(bedata->ref, dev->otherend_id,
					virt_to_gfn((void *)sring), 0);

 again:
	ret = xenbus_transaction_start(&xbt);
	if (ret) {
		xenbus_dev_fatal(dev, ret, "starting transaction");
		goto error;
	}
	ret = xenbus_printf(xbt, dev->nodename, "version", "%u", 1);
	if (ret)
		goto error_xenbus;
	ret = xenbus_printf(xbt, dev->nodename, "ring-ref", "%d", bedata->ref);
	if (ret)
		goto error_xenbus;
	ret = xenbus_printf(xbt, dev->nodename, "port", "%u",
			    evtchn);
	if (ret)
		goto error_xenbus;
	ret = xenbus_transaction_end(xbt, 0);
	if (ret) {
		if (ret == -EAGAIN)
			goto again;
		xenbus_dev_fatal(dev, ret, "completing transaction");
		goto error;
	}
	xenbus_switch_state(dev, XenbusStateInitialised);

	return 0;

 error_xenbus:
	xenbus_transaction_end(xbt, 1);
	xenbus_dev_fatal(dev, ret, "writing xenstore");
 error:
	pvcalls_front_remove(dev);
	return ret;
}

static void pvcalls_front_changed(struct xenbus_device *dev,
			    enum xenbus_state backend_state)
{
	switch (backend_state) {
	case XenbusStateReconfiguring:
	case XenbusStateReconfigured:
	case XenbusStateInitialising:
	case XenbusStateInitialised:
	case XenbusStateUnknown:
		break;

	case XenbusStateInitWait:
		break;

	case XenbusStateConnected:
		xenbus_switch_state(dev, XenbusStateConnected);
		break;

	case XenbusStateClosed:
		if (dev->state == XenbusStateClosed)
			break;
		/* Missed the backend's CLOSING state */
		fallthrough;
	case XenbusStateClosing:
		xenbus_frontend_closed(dev);
		break;
	}
}

static struct xenbus_driver pvcalls_front_driver = {
	.ids = pvcalls_front_ids,
	.probe = pvcalls_front_probe,
	.remove = pvcalls_front_remove,
	.otherend_changed = pvcalls_front_changed,
	.not_essential = true,
};

static int __init pvcalls_frontend_init(void)
{
	if (!xen_domain())
		return -ENODEV;

	pr_info("Initialising Xen pvcalls frontend driver\n");

	return xenbus_register_frontend(&pvcalls_front_driver);
}

module_init(pvcalls_frontend_init);

MODULE_DESCRIPTION("Xen PV Calls frontend driver");
MODULE_AUTHOR("Stefano Stabellini <sstabellini@kernel.org>");
MODULE_LICENSE("GPL");
