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
	};
};

static inline int get_request(struct pvcalls_bedata *bedata, int *req_id)
{
	*req_id = bedata->ring.req_prod_pvt & (RING_SIZE(&bedata->ring) - 1);
	if (RING_FULL(&bedata->ring) ||
	    bedata->rsp[*req_id].req_id != PVCALLS_INVALID_ID)
		return -EAGAIN;
	return 0;
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
		dst = (uint8_t *)&bedata->rsp[req_id] + sizeof(rsp->req_id);
		src = (uint8_t *)rsp + sizeof(rsp->req_id);
		memcpy(dst, src, sizeof(*rsp) - sizeof(rsp->req_id));
		/*
		 * First copy the rest of the data, then req_id. It is
		 * paired with the barrier when accessing bedata->rsp.
		 */
		smp_wmb();
		bedata->rsp[req_id].req_id = rsp->req_id;

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

static void pvcalls_front_free_map(struct pvcalls_bedata *bedata,
				   struct sock_mapping *map)
{
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

static int create_active(struct sock_mapping *map, int *evtchn)
{
	void *bytes;
	int ret = -ENOMEM, irq = -1, i;

	*evtchn = -1;
	init_waitqueue_head(&map->active.inflight_conn_req);

	map->active.ring = (struct pvcalls_data_intf *)
		__get_free_page(GFP_KERNEL | __GFP_ZERO);
	if (map->active.ring == NULL)
		goto out_error;
	map->active.ring->ring_order = PVCALLS_RING_ORDER;
	bytes = (void *)__get_free_pages(GFP_KERNEL | __GFP_ZERO,
					PVCALLS_RING_ORDER);
	if (bytes == NULL)
		goto out_error;
	for (i = 0; i < (1 << PVCALLS_RING_ORDER); i++)
		map->active.ring->ref[i] = gnttab_grant_foreign_access(
			pvcalls_front_dev->otherend_id,
			pfn_to_gfn(virt_to_pfn(bytes) + i), 0);

	map->active.ref = gnttab_grant_foreign_access(
		pvcalls_front_dev->otherend_id,
		pfn_to_gfn(virt_to_pfn((void *)map->active.ring)), 0);

	map->active.data.in = bytes;
	map->active.data.out = bytes +
		XEN_FLEX_RING_SIZE(PVCALLS_RING_ORDER);

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
	if (irq >= 0)
		unbind_from_irqhandler(irq, map);
	else if (*evtchn >= 0)
		xenbus_free_evtchn(pvcalls_front_dev, *evtchn);
	kfree(map->active.data.in);
	kfree(map->active.ring);
	return ret;
}

int pvcalls_front_connect(struct socket *sock, struct sockaddr *addr,
				int addr_len, int flags)
{
	struct pvcalls_bedata *bedata;
	struct sock_mapping *map = NULL;
	struct xen_pvcalls_request *req;
	int notify, req_id, ret, evtchn;

	if (addr->sa_family != AF_INET || sock->type != SOCK_STREAM)
		return -EOPNOTSUPP;

	pvcalls_enter();
	if (!pvcalls_front_dev) {
		pvcalls_exit();
		return -ENOTCONN;
	}

	bedata = dev_get_drvdata(&pvcalls_front_dev->dev);

	map = (struct sock_mapping *)sock->sk->sk_send_head;
	if (!map) {
		pvcalls_exit();
		return -ENOTSOCK;
	}

	spin_lock(&bedata->socket_lock);
	ret = get_request(bedata, &req_id);
	if (ret < 0) {
		spin_unlock(&bedata->socket_lock);
		pvcalls_exit();
		return ret;
	}
	ret = create_active(map, &evtchn);
	if (ret < 0) {
		spin_unlock(&bedata->socket_lock);
		pvcalls_exit();
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
	pvcalls_exit();
	return ret;
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
	if (bedata->ref >= 0)
		gnttab_end_foreign_access(bedata->ref, 0, 0);
	kfree(bedata->ring.sring);
	kfree(bedata);
	xenbus_switch_state(dev, XenbusStateClosed);
	return 0;
}

static int pvcalls_front_probe(struct xenbus_device *dev,
			  const struct xenbus_device_id *id)
{
	int ret = -ENOMEM, evtchn, i;
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
	bedata->ref = gnttab_claim_grant_reference(&gref_head);
	if (bedata->ref < 0) {
		ret = bedata->ref;
		goto error;
	}
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
		/* Missed the backend's CLOSING state -- fallthrough */
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
};

static int __init pvcalls_frontend_init(void)
{
	if (!xen_domain())
		return -ENODEV;

	pr_info("Initialising Xen pvcalls frontend driver\n");

	return xenbus_register_frontend(&pvcalls_front_driver);
}

module_init(pvcalls_frontend_init);
