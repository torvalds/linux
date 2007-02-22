/*
 * Copyright (c) 2005 Voltaire Inc.  All rights reserved.
 * Copyright (c) 2002-2005, Network Appliance, Inc. All rights reserved.
 * Copyright (c) 1999-2005, Mellanox Technologies, Inc. All rights reserved.
 * Copyright (c) 2005-2006 Intel Corporation.  All rights reserved.
 *
 * This Software is licensed under one of the following licenses:
 *
 * 1) under the terms of the "Common Public License 1.0" a copy of which is
 *    available from the Open Source Initiative, see
 *    http://www.opensource.org/licenses/cpl.php.
 *
 * 2) under the terms of the "The BSD License" a copy of which is
 *    available from the Open Source Initiative, see
 *    http://www.opensource.org/licenses/bsd-license.php.
 *
 * 3) under the terms of the "GNU General Public License (GPL) Version 2" a
 *    copy of which is available from the Open Source Initiative, see
 *    http://www.opensource.org/licenses/gpl-license.php.
 *
 * Licensee has the right to choose one of the above licenses.
 *
 * Redistributions of source code must retain the above copyright
 * notice and one of the license notices.
 *
 * Redistributions in binary form must reproduce both the above copyright
 * notice, one of the license notices in the documentation
 * and/or other materials provided with the distribution.
 *
 */

#include <linux/completion.h>
#include <linux/in.h>
#include <linux/in6.h>
#include <linux/mutex.h>
#include <linux/random.h>
#include <linux/idr.h>
#include <linux/inetdevice.h>

#include <net/tcp.h>

#include <rdma/rdma_cm.h>
#include <rdma/rdma_cm_ib.h>
#include <rdma/ib_cache.h>
#include <rdma/ib_cm.h>
#include <rdma/ib_sa.h>
#include <rdma/iw_cm.h>

MODULE_AUTHOR("Sean Hefty");
MODULE_DESCRIPTION("Generic RDMA CM Agent");
MODULE_LICENSE("Dual BSD/GPL");

#define CMA_CM_RESPONSE_TIMEOUT 20
#define CMA_MAX_CM_RETRIES 15

static void cma_add_one(struct ib_device *device);
static void cma_remove_one(struct ib_device *device);

static struct ib_client cma_client = {
	.name   = "cma",
	.add    = cma_add_one,
	.remove = cma_remove_one
};

static struct ib_sa_client sa_client;
static struct rdma_addr_client addr_client;
static LIST_HEAD(dev_list);
static LIST_HEAD(listen_any_list);
static DEFINE_MUTEX(lock);
static struct workqueue_struct *cma_wq;
static DEFINE_IDR(sdp_ps);
static DEFINE_IDR(tcp_ps);
static DEFINE_IDR(udp_ps);
static DEFINE_IDR(ipoib_ps);
static int next_port;

struct cma_device {
	struct list_head	list;
	struct ib_device	*device;
	struct completion	comp;
	atomic_t		refcount;
	struct list_head	id_list;
};

enum cma_state {
	CMA_IDLE,
	CMA_ADDR_QUERY,
	CMA_ADDR_RESOLVED,
	CMA_ROUTE_QUERY,
	CMA_ROUTE_RESOLVED,
	CMA_CONNECT,
	CMA_DISCONNECT,
	CMA_ADDR_BOUND,
	CMA_LISTEN,
	CMA_DEVICE_REMOVAL,
	CMA_DESTROYING
};

struct rdma_bind_list {
	struct idr		*ps;
	struct hlist_head	owners;
	unsigned short		port;
};

/*
 * Device removal can occur at anytime, so we need extra handling to
 * serialize notifying the user of device removal with other callbacks.
 * We do this by disabling removal notification while a callback is in process,
 * and reporting it after the callback completes.
 */
struct rdma_id_private {
	struct rdma_cm_id	id;

	struct rdma_bind_list	*bind_list;
	struct hlist_node	node;
	struct list_head	list;
	struct list_head	listen_list;
	struct cma_device	*cma_dev;
	struct list_head	mc_list;

	enum cma_state		state;
	spinlock_t		lock;
	struct completion	comp;
	atomic_t		refcount;
	wait_queue_head_t	wait_remove;
	atomic_t		dev_remove;

	int			backlog;
	int			timeout_ms;
	struct ib_sa_query	*query;
	int			query_id;
	union {
		struct ib_cm_id	*ib;
		struct iw_cm_id	*iw;
	} cm_id;

	u32			seq_num;
	u32			qkey;
	u32			qp_num;
	u8			srq;
};

struct cma_multicast {
	struct rdma_id_private *id_priv;
	union {
		struct ib_sa_multicast *ib;
	} multicast;
	struct list_head	list;
	void			*context;
	struct sockaddr		addr;
	u8			pad[sizeof(struct sockaddr_in6) -
				    sizeof(struct sockaddr)];
};

struct cma_work {
	struct work_struct	work;
	struct rdma_id_private	*id;
	enum cma_state		old_state;
	enum cma_state		new_state;
	struct rdma_cm_event	event;
};

union cma_ip_addr {
	struct in6_addr ip6;
	struct {
		__u32 pad[3];
		__u32 addr;
	} ip4;
};

struct cma_hdr {
	u8 cma_version;
	u8 ip_version;	/* IP version: 7:4 */
	__u16 port;
	union cma_ip_addr src_addr;
	union cma_ip_addr dst_addr;
};

struct sdp_hh {
	u8 bsdh[16];
	u8 sdp_version; /* Major version: 7:4 */
	u8 ip_version;	/* IP version: 7:4 */
	u8 sdp_specific1[10];
	__u16 port;
	__u16 sdp_specific2;
	union cma_ip_addr src_addr;
	union cma_ip_addr dst_addr;
};

struct sdp_hah {
	u8 bsdh[16];
	u8 sdp_version;
};

#define CMA_VERSION 0x00
#define SDP_MAJ_VERSION 0x2

static int cma_comp(struct rdma_id_private *id_priv, enum cma_state comp)
{
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&id_priv->lock, flags);
	ret = (id_priv->state == comp);
	spin_unlock_irqrestore(&id_priv->lock, flags);
	return ret;
}

static int cma_comp_exch(struct rdma_id_private *id_priv,
			 enum cma_state comp, enum cma_state exch)
{
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&id_priv->lock, flags);
	if ((ret = (id_priv->state == comp)))
		id_priv->state = exch;
	spin_unlock_irqrestore(&id_priv->lock, flags);
	return ret;
}

static enum cma_state cma_exch(struct rdma_id_private *id_priv,
			       enum cma_state exch)
{
	unsigned long flags;
	enum cma_state old;

	spin_lock_irqsave(&id_priv->lock, flags);
	old = id_priv->state;
	id_priv->state = exch;
	spin_unlock_irqrestore(&id_priv->lock, flags);
	return old;
}

static inline u8 cma_get_ip_ver(struct cma_hdr *hdr)
{
	return hdr->ip_version >> 4;
}

static inline void cma_set_ip_ver(struct cma_hdr *hdr, u8 ip_ver)
{
	hdr->ip_version = (ip_ver << 4) | (hdr->ip_version & 0xF);
}

static inline u8 sdp_get_majv(u8 sdp_version)
{
	return sdp_version >> 4;
}

static inline u8 sdp_get_ip_ver(struct sdp_hh *hh)
{
	return hh->ip_version >> 4;
}

static inline void sdp_set_ip_ver(struct sdp_hh *hh, u8 ip_ver)
{
	hh->ip_version = (ip_ver << 4) | (hh->ip_version & 0xF);
}

static inline int cma_is_ud_ps(enum rdma_port_space ps)
{
	return (ps == RDMA_PS_UDP || ps == RDMA_PS_IPOIB);
}

static void cma_attach_to_dev(struct rdma_id_private *id_priv,
			      struct cma_device *cma_dev)
{
	atomic_inc(&cma_dev->refcount);
	id_priv->cma_dev = cma_dev;
	id_priv->id.device = cma_dev->device;
	list_add_tail(&id_priv->list, &cma_dev->id_list);
}

static inline void cma_deref_dev(struct cma_device *cma_dev)
{
	if (atomic_dec_and_test(&cma_dev->refcount))
		complete(&cma_dev->comp);
}

static void cma_detach_from_dev(struct rdma_id_private *id_priv)
{
	list_del(&id_priv->list);
	cma_deref_dev(id_priv->cma_dev);
	id_priv->cma_dev = NULL;
}

static int cma_set_qkey(struct ib_device *device, u8 port_num,
			enum rdma_port_space ps,
			struct rdma_dev_addr *dev_addr, u32 *qkey)
{
	struct ib_sa_mcmember_rec rec;
	int ret = 0;

	switch (ps) {
	case RDMA_PS_UDP:
		*qkey = RDMA_UDP_QKEY;
		break;
	case RDMA_PS_IPOIB:
		ib_addr_get_mgid(dev_addr, &rec.mgid);
		ret = ib_sa_get_mcmember_rec(device, port_num, &rec.mgid, &rec);
		*qkey = be32_to_cpu(rec.qkey);
		break;
	default:
		break;
	}
	return ret;
}

static int cma_acquire_dev(struct rdma_id_private *id_priv)
{
	struct rdma_dev_addr *dev_addr = &id_priv->id.route.addr.dev_addr;
	struct cma_device *cma_dev;
	union ib_gid gid;
	int ret = -ENODEV;

	switch (rdma_node_get_transport(dev_addr->dev_type)) {
	case RDMA_TRANSPORT_IB:
		ib_addr_get_sgid(dev_addr, &gid);
		break;
	case RDMA_TRANSPORT_IWARP:
		iw_addr_get_sgid(dev_addr, &gid);
		break;
	default:
		return -ENODEV;
	}

	list_for_each_entry(cma_dev, &dev_list, list) {
		ret = ib_find_cached_gid(cma_dev->device, &gid,
					 &id_priv->id.port_num, NULL);
		if (!ret) {
			ret = cma_set_qkey(cma_dev->device,
					   id_priv->id.port_num,
					   id_priv->id.ps, dev_addr,
					   &id_priv->qkey);
			if (!ret)
				cma_attach_to_dev(id_priv, cma_dev);
			break;
		}
	}
	return ret;
}

static void cma_deref_id(struct rdma_id_private *id_priv)
{
	if (atomic_dec_and_test(&id_priv->refcount))
		complete(&id_priv->comp);
}

static void cma_release_remove(struct rdma_id_private *id_priv)
{
	if (atomic_dec_and_test(&id_priv->dev_remove))
		wake_up(&id_priv->wait_remove);
}

struct rdma_cm_id *rdma_create_id(rdma_cm_event_handler event_handler,
				  void *context, enum rdma_port_space ps)
{
	struct rdma_id_private *id_priv;

	id_priv = kzalloc(sizeof *id_priv, GFP_KERNEL);
	if (!id_priv)
		return ERR_PTR(-ENOMEM);

	id_priv->state = CMA_IDLE;
	id_priv->id.context = context;
	id_priv->id.event_handler = event_handler;
	id_priv->id.ps = ps;
	spin_lock_init(&id_priv->lock);
	init_completion(&id_priv->comp);
	atomic_set(&id_priv->refcount, 1);
	init_waitqueue_head(&id_priv->wait_remove);
	atomic_set(&id_priv->dev_remove, 0);
	INIT_LIST_HEAD(&id_priv->listen_list);
	INIT_LIST_HEAD(&id_priv->mc_list);
	get_random_bytes(&id_priv->seq_num, sizeof id_priv->seq_num);

	return &id_priv->id;
}
EXPORT_SYMBOL(rdma_create_id);

static int cma_init_ud_qp(struct rdma_id_private *id_priv, struct ib_qp *qp)
{
	struct ib_qp_attr qp_attr;
	int qp_attr_mask, ret;

	qp_attr.qp_state = IB_QPS_INIT;
	ret = rdma_init_qp_attr(&id_priv->id, &qp_attr, &qp_attr_mask);
	if (ret)
		return ret;

	ret = ib_modify_qp(qp, &qp_attr, qp_attr_mask);
	if (ret)
		return ret;

	qp_attr.qp_state = IB_QPS_RTR;
	ret = ib_modify_qp(qp, &qp_attr, IB_QP_STATE);
	if (ret)
		return ret;

	qp_attr.qp_state = IB_QPS_RTS;
	qp_attr.sq_psn = 0;
	ret = ib_modify_qp(qp, &qp_attr, IB_QP_STATE | IB_QP_SQ_PSN);

	return ret;
}

static int cma_init_conn_qp(struct rdma_id_private *id_priv, struct ib_qp *qp)
{
	struct ib_qp_attr qp_attr;
	int qp_attr_mask, ret;

	qp_attr.qp_state = IB_QPS_INIT;
	ret = rdma_init_qp_attr(&id_priv->id, &qp_attr, &qp_attr_mask);
	if (ret)
		return ret;

	return ib_modify_qp(qp, &qp_attr, qp_attr_mask);
}

int rdma_create_qp(struct rdma_cm_id *id, struct ib_pd *pd,
		   struct ib_qp_init_attr *qp_init_attr)
{
	struct rdma_id_private *id_priv;
	struct ib_qp *qp;
	int ret;

	id_priv = container_of(id, struct rdma_id_private, id);
	if (id->device != pd->device)
		return -EINVAL;

	qp = ib_create_qp(pd, qp_init_attr);
	if (IS_ERR(qp))
		return PTR_ERR(qp);

	if (cma_is_ud_ps(id_priv->id.ps))
		ret = cma_init_ud_qp(id_priv, qp);
	else
		ret = cma_init_conn_qp(id_priv, qp);
	if (ret)
		goto err;

	id->qp = qp;
	id_priv->qp_num = qp->qp_num;
	id_priv->srq = (qp->srq != NULL);
	return 0;
err:
	ib_destroy_qp(qp);
	return ret;
}
EXPORT_SYMBOL(rdma_create_qp);

void rdma_destroy_qp(struct rdma_cm_id *id)
{
	ib_destroy_qp(id->qp);
}
EXPORT_SYMBOL(rdma_destroy_qp);

static int cma_modify_qp_rtr(struct rdma_cm_id *id)
{
	struct ib_qp_attr qp_attr;
	int qp_attr_mask, ret;

	if (!id->qp)
		return 0;

	/* Need to update QP attributes from default values. */
	qp_attr.qp_state = IB_QPS_INIT;
	ret = rdma_init_qp_attr(id, &qp_attr, &qp_attr_mask);
	if (ret)
		return ret;

	ret = ib_modify_qp(id->qp, &qp_attr, qp_attr_mask);
	if (ret)
		return ret;

	qp_attr.qp_state = IB_QPS_RTR;
	ret = rdma_init_qp_attr(id, &qp_attr, &qp_attr_mask);
	if (ret)
		return ret;

	return ib_modify_qp(id->qp, &qp_attr, qp_attr_mask);
}

static int cma_modify_qp_rts(struct rdma_cm_id *id)
{
	struct ib_qp_attr qp_attr;
	int qp_attr_mask, ret;

	if (!id->qp)
		return 0;

	qp_attr.qp_state = IB_QPS_RTS;
	ret = rdma_init_qp_attr(id, &qp_attr, &qp_attr_mask);
	if (ret)
		return ret;

	return ib_modify_qp(id->qp, &qp_attr, qp_attr_mask);
}

static int cma_modify_qp_err(struct rdma_cm_id *id)
{
	struct ib_qp_attr qp_attr;

	if (!id->qp)
		return 0;

	qp_attr.qp_state = IB_QPS_ERR;
	return ib_modify_qp(id->qp, &qp_attr, IB_QP_STATE);
}

static int cma_ib_init_qp_attr(struct rdma_id_private *id_priv,
			       struct ib_qp_attr *qp_attr, int *qp_attr_mask)
{
	struct rdma_dev_addr *dev_addr = &id_priv->id.route.addr.dev_addr;
	int ret;

	ret = ib_find_cached_pkey(id_priv->id.device, id_priv->id.port_num,
				  ib_addr_get_pkey(dev_addr),
				  &qp_attr->pkey_index);
	if (ret)
		return ret;

	qp_attr->port_num = id_priv->id.port_num;
	*qp_attr_mask = IB_QP_STATE | IB_QP_PKEY_INDEX | IB_QP_PORT;

	if (cma_is_ud_ps(id_priv->id.ps)) {
		qp_attr->qkey = id_priv->qkey;
		*qp_attr_mask |= IB_QP_QKEY;
	} else {
		qp_attr->qp_access_flags = 0;
		*qp_attr_mask |= IB_QP_ACCESS_FLAGS;
	}
	return 0;
}

int rdma_init_qp_attr(struct rdma_cm_id *id, struct ib_qp_attr *qp_attr,
		       int *qp_attr_mask)
{
	struct rdma_id_private *id_priv;
	int ret = 0;

	id_priv = container_of(id, struct rdma_id_private, id);
	switch (rdma_node_get_transport(id_priv->id.device->node_type)) {
	case RDMA_TRANSPORT_IB:
		if (!id_priv->cm_id.ib || cma_is_ud_ps(id_priv->id.ps))
			ret = cma_ib_init_qp_attr(id_priv, qp_attr, qp_attr_mask);
		else
			ret = ib_cm_init_qp_attr(id_priv->cm_id.ib, qp_attr,
						 qp_attr_mask);
		if (qp_attr->qp_state == IB_QPS_RTR)
			qp_attr->rq_psn = id_priv->seq_num;
		break;
	case RDMA_TRANSPORT_IWARP:
		if (!id_priv->cm_id.iw) {
			qp_attr->qp_access_flags = IB_ACCESS_LOCAL_WRITE;
			*qp_attr_mask = IB_QP_STATE | IB_QP_ACCESS_FLAGS;
		} else
			ret = iw_cm_init_qp_attr(id_priv->cm_id.iw, qp_attr,
						 qp_attr_mask);
		break;
	default:
		ret = -ENOSYS;
		break;
	}

	return ret;
}
EXPORT_SYMBOL(rdma_init_qp_attr);

static inline int cma_zero_addr(struct sockaddr *addr)
{
	struct in6_addr *ip6;

	if (addr->sa_family == AF_INET)
		return ZERONET(((struct sockaddr_in *) addr)->sin_addr.s_addr);
	else {
		ip6 = &((struct sockaddr_in6 *) addr)->sin6_addr;
		return (ip6->s6_addr32[0] | ip6->s6_addr32[1] |
			ip6->s6_addr32[2] | ip6->s6_addr32[3]) == 0;
	}
}

static inline int cma_loopback_addr(struct sockaddr *addr)
{
	return LOOPBACK(((struct sockaddr_in *) addr)->sin_addr.s_addr);
}

static inline int cma_any_addr(struct sockaddr *addr)
{
	return cma_zero_addr(addr) || cma_loopback_addr(addr);
}

static inline __be16 cma_port(struct sockaddr *addr)
{
	if (addr->sa_family == AF_INET)
		return ((struct sockaddr_in *) addr)->sin_port;
	else
		return ((struct sockaddr_in6 *) addr)->sin6_port;
}

static inline int cma_any_port(struct sockaddr *addr)
{
	return !cma_port(addr);
}

static int cma_get_net_info(void *hdr, enum rdma_port_space ps,
			    u8 *ip_ver, __u16 *port,
			    union cma_ip_addr **src, union cma_ip_addr **dst)
{
	switch (ps) {
	case RDMA_PS_SDP:
		if (sdp_get_majv(((struct sdp_hh *) hdr)->sdp_version) !=
		    SDP_MAJ_VERSION)
			return -EINVAL;

		*ip_ver	= sdp_get_ip_ver(hdr);
		*port	= ((struct sdp_hh *) hdr)->port;
		*src	= &((struct sdp_hh *) hdr)->src_addr;
		*dst	= &((struct sdp_hh *) hdr)->dst_addr;
		break;
	default:
		if (((struct cma_hdr *) hdr)->cma_version != CMA_VERSION)
			return -EINVAL;

		*ip_ver	= cma_get_ip_ver(hdr);
		*port	= ((struct cma_hdr *) hdr)->port;
		*src	= &((struct cma_hdr *) hdr)->src_addr;
		*dst	= &((struct cma_hdr *) hdr)->dst_addr;
		break;
	}

	if (*ip_ver != 4 && *ip_ver != 6)
		return -EINVAL;
	return 0;
}

static void cma_save_net_info(struct rdma_addr *addr,
			      struct rdma_addr *listen_addr,
			      u8 ip_ver, __u16 port,
			      union cma_ip_addr *src, union cma_ip_addr *dst)
{
	struct sockaddr_in *listen4, *ip4;
	struct sockaddr_in6 *listen6, *ip6;

	switch (ip_ver) {
	case 4:
		listen4 = (struct sockaddr_in *) &listen_addr->src_addr;
		ip4 = (struct sockaddr_in *) &addr->src_addr;
		ip4->sin_family = listen4->sin_family;
		ip4->sin_addr.s_addr = dst->ip4.addr;
		ip4->sin_port = listen4->sin_port;

		ip4 = (struct sockaddr_in *) &addr->dst_addr;
		ip4->sin_family = listen4->sin_family;
		ip4->sin_addr.s_addr = src->ip4.addr;
		ip4->sin_port = port;
		break;
	case 6:
		listen6 = (struct sockaddr_in6 *) &listen_addr->src_addr;
		ip6 = (struct sockaddr_in6 *) &addr->src_addr;
		ip6->sin6_family = listen6->sin6_family;
		ip6->sin6_addr = dst->ip6;
		ip6->sin6_port = listen6->sin6_port;

		ip6 = (struct sockaddr_in6 *) &addr->dst_addr;
		ip6->sin6_family = listen6->sin6_family;
		ip6->sin6_addr = src->ip6;
		ip6->sin6_port = port;
		break;
	default:
		break;
	}
}

static inline int cma_user_data_offset(enum rdma_port_space ps)
{
	switch (ps) {
	case RDMA_PS_SDP:
		return 0;
	default:
		return sizeof(struct cma_hdr);
	}
}

static void cma_cancel_route(struct rdma_id_private *id_priv)
{
	switch (rdma_node_get_transport(id_priv->id.device->node_type)) {
	case RDMA_TRANSPORT_IB:
		if (id_priv->query)
			ib_sa_cancel_query(id_priv->query_id, id_priv->query);
		break;
	default:
		break;
	}
}

static inline int cma_internal_listen(struct rdma_id_private *id_priv)
{
	return (id_priv->state == CMA_LISTEN) && id_priv->cma_dev &&
	       cma_any_addr(&id_priv->id.route.addr.src_addr);
}

static void cma_destroy_listen(struct rdma_id_private *id_priv)
{
	cma_exch(id_priv, CMA_DESTROYING);

	if (id_priv->cma_dev) {
		switch (rdma_node_get_transport(id_priv->id.device->node_type)) {
		case RDMA_TRANSPORT_IB:
			if (id_priv->cm_id.ib && !IS_ERR(id_priv->cm_id.ib))
				ib_destroy_cm_id(id_priv->cm_id.ib);
			break;
		case RDMA_TRANSPORT_IWARP:
			if (id_priv->cm_id.iw && !IS_ERR(id_priv->cm_id.iw))
				iw_destroy_cm_id(id_priv->cm_id.iw);
			break;
		default:
			break;
		}
		cma_detach_from_dev(id_priv);
	}
	list_del(&id_priv->listen_list);

	cma_deref_id(id_priv);
	wait_for_completion(&id_priv->comp);

	kfree(id_priv);
}

static void cma_cancel_listens(struct rdma_id_private *id_priv)
{
	struct rdma_id_private *dev_id_priv;

	mutex_lock(&lock);
	list_del(&id_priv->list);

	while (!list_empty(&id_priv->listen_list)) {
		dev_id_priv = list_entry(id_priv->listen_list.next,
					 struct rdma_id_private, listen_list);
		cma_destroy_listen(dev_id_priv);
	}
	mutex_unlock(&lock);
}

static void cma_cancel_operation(struct rdma_id_private *id_priv,
				 enum cma_state state)
{
	switch (state) {
	case CMA_ADDR_QUERY:
		rdma_addr_cancel(&id_priv->id.route.addr.dev_addr);
		break;
	case CMA_ROUTE_QUERY:
		cma_cancel_route(id_priv);
		break;
	case CMA_LISTEN:
		if (cma_any_addr(&id_priv->id.route.addr.src_addr) &&
		    !id_priv->cma_dev)
			cma_cancel_listens(id_priv);
		break;
	default:
		break;
	}
}

static void cma_release_port(struct rdma_id_private *id_priv)
{
	struct rdma_bind_list *bind_list = id_priv->bind_list;

	if (!bind_list)
		return;

	mutex_lock(&lock);
	hlist_del(&id_priv->node);
	if (hlist_empty(&bind_list->owners)) {
		idr_remove(bind_list->ps, bind_list->port);
		kfree(bind_list);
	}
	mutex_unlock(&lock);
}

static void cma_leave_mc_groups(struct rdma_id_private *id_priv)
{
	struct cma_multicast *mc;

	while (!list_empty(&id_priv->mc_list)) {
		mc = container_of(id_priv->mc_list.next,
				  struct cma_multicast, list);
		list_del(&mc->list);
		ib_sa_free_multicast(mc->multicast.ib);
		kfree(mc);
	}
}

void rdma_destroy_id(struct rdma_cm_id *id)
{
	struct rdma_id_private *id_priv;
	enum cma_state state;

	id_priv = container_of(id, struct rdma_id_private, id);
	state = cma_exch(id_priv, CMA_DESTROYING);
	cma_cancel_operation(id_priv, state);

	mutex_lock(&lock);
	if (id_priv->cma_dev) {
		mutex_unlock(&lock);
		switch (rdma_node_get_transport(id->device->node_type)) {
		case RDMA_TRANSPORT_IB:
			if (id_priv->cm_id.ib && !IS_ERR(id_priv->cm_id.ib))
				ib_destroy_cm_id(id_priv->cm_id.ib);
			break;
		case RDMA_TRANSPORT_IWARP:
			if (id_priv->cm_id.iw && !IS_ERR(id_priv->cm_id.iw))
				iw_destroy_cm_id(id_priv->cm_id.iw);
			break;
		default:
			break;
		}
		cma_leave_mc_groups(id_priv);
		mutex_lock(&lock);
		cma_detach_from_dev(id_priv);
	}
	mutex_unlock(&lock);

	cma_release_port(id_priv);
	cma_deref_id(id_priv);
	wait_for_completion(&id_priv->comp);

	kfree(id_priv->id.route.path_rec);
	kfree(id_priv);
}
EXPORT_SYMBOL(rdma_destroy_id);

static int cma_rep_recv(struct rdma_id_private *id_priv)
{
	int ret;

	ret = cma_modify_qp_rtr(&id_priv->id);
	if (ret)
		goto reject;

	ret = cma_modify_qp_rts(&id_priv->id);
	if (ret)
		goto reject;

	ret = ib_send_cm_rtu(id_priv->cm_id.ib, NULL, 0);
	if (ret)
		goto reject;

	return 0;
reject:
	cma_modify_qp_err(&id_priv->id);
	ib_send_cm_rej(id_priv->cm_id.ib, IB_CM_REJ_CONSUMER_DEFINED,
		       NULL, 0, NULL, 0);
	return ret;
}

static int cma_verify_rep(struct rdma_id_private *id_priv, void *data)
{
	if (id_priv->id.ps == RDMA_PS_SDP &&
	    sdp_get_majv(((struct sdp_hah *) data)->sdp_version) !=
	    SDP_MAJ_VERSION)
		return -EINVAL;

	return 0;
}

static void cma_set_rep_event_data(struct rdma_cm_event *event,
				   struct ib_cm_rep_event_param *rep_data,
				   void *private_data)
{
	event->param.conn.private_data = private_data;
	event->param.conn.private_data_len = IB_CM_REP_PRIVATE_DATA_SIZE;
	event->param.conn.responder_resources = rep_data->responder_resources;
	event->param.conn.initiator_depth = rep_data->initiator_depth;
	event->param.conn.flow_control = rep_data->flow_control;
	event->param.conn.rnr_retry_count = rep_data->rnr_retry_count;
	event->param.conn.srq = rep_data->srq;
	event->param.conn.qp_num = rep_data->remote_qpn;
}

static int cma_ib_handler(struct ib_cm_id *cm_id, struct ib_cm_event *ib_event)
{
	struct rdma_id_private *id_priv = cm_id->context;
	struct rdma_cm_event event;
	int ret = 0;

	atomic_inc(&id_priv->dev_remove);
	if (!cma_comp(id_priv, CMA_CONNECT))
		goto out;

	memset(&event, 0, sizeof event);
	switch (ib_event->event) {
	case IB_CM_REQ_ERROR:
	case IB_CM_REP_ERROR:
		event.event = RDMA_CM_EVENT_UNREACHABLE;
		event.status = -ETIMEDOUT;
		break;
	case IB_CM_REP_RECEIVED:
		event.status = cma_verify_rep(id_priv, ib_event->private_data);
		if (event.status)
			event.event = RDMA_CM_EVENT_CONNECT_ERROR;
		else if (id_priv->id.qp && id_priv->id.ps != RDMA_PS_SDP) {
			event.status = cma_rep_recv(id_priv);
			event.event = event.status ? RDMA_CM_EVENT_CONNECT_ERROR :
						     RDMA_CM_EVENT_ESTABLISHED;
		} else
			event.event = RDMA_CM_EVENT_CONNECT_RESPONSE;
		cma_set_rep_event_data(&event, &ib_event->param.rep_rcvd,
				       ib_event->private_data);
		break;
	case IB_CM_RTU_RECEIVED:
	case IB_CM_USER_ESTABLISHED:
		event.event = RDMA_CM_EVENT_ESTABLISHED;
		break;
	case IB_CM_DREQ_ERROR:
		event.status = -ETIMEDOUT; /* fall through */
	case IB_CM_DREQ_RECEIVED:
	case IB_CM_DREP_RECEIVED:
		if (!cma_comp_exch(id_priv, CMA_CONNECT, CMA_DISCONNECT))
			goto out;
		event.event = RDMA_CM_EVENT_DISCONNECTED;
		break;
	case IB_CM_TIMEWAIT_EXIT:
	case IB_CM_MRA_RECEIVED:
		/* ignore event */
		goto out;
	case IB_CM_REJ_RECEIVED:
		cma_modify_qp_err(&id_priv->id);
		event.status = ib_event->param.rej_rcvd.reason;
		event.event = RDMA_CM_EVENT_REJECTED;
		event.param.conn.private_data = ib_event->private_data;
		event.param.conn.private_data_len = IB_CM_REJ_PRIVATE_DATA_SIZE;
		break;
	default:
		printk(KERN_ERR "RDMA CMA: unexpected IB CM event: %d",
		       ib_event->event);
		goto out;
	}

	ret = id_priv->id.event_handler(&id_priv->id, &event);
	if (ret) {
		/* Destroy the CM ID by returning a non-zero value. */
		id_priv->cm_id.ib = NULL;
		cma_exch(id_priv, CMA_DESTROYING);
		cma_release_remove(id_priv);
		rdma_destroy_id(&id_priv->id);
		return ret;
	}
out:
	cma_release_remove(id_priv);
	return ret;
}

static struct rdma_id_private *cma_new_conn_id(struct rdma_cm_id *listen_id,
					       struct ib_cm_event *ib_event)
{
	struct rdma_id_private *id_priv;
	struct rdma_cm_id *id;
	struct rdma_route *rt;
	union cma_ip_addr *src, *dst;
	__u16 port;
	u8 ip_ver;

	if (cma_get_net_info(ib_event->private_data, listen_id->ps,
			     &ip_ver, &port, &src, &dst))
		goto err;

	id = rdma_create_id(listen_id->event_handler, listen_id->context,
			    listen_id->ps);
	if (IS_ERR(id))
		goto err;

	cma_save_net_info(&id->route.addr, &listen_id->route.addr,
			  ip_ver, port, src, dst);

	rt = &id->route;
	rt->num_paths = ib_event->param.req_rcvd.alternate_path ? 2 : 1;
	rt->path_rec = kmalloc(sizeof *rt->path_rec * rt->num_paths,
			       GFP_KERNEL);
	if (!rt->path_rec)
		goto destroy_id;

	rt->path_rec[0] = *ib_event->param.req_rcvd.primary_path;
	if (rt->num_paths == 2)
		rt->path_rec[1] = *ib_event->param.req_rcvd.alternate_path;

	ib_addr_set_sgid(&rt->addr.dev_addr, &rt->path_rec[0].sgid);
	ib_addr_set_dgid(&rt->addr.dev_addr, &rt->path_rec[0].dgid);
	ib_addr_set_pkey(&rt->addr.dev_addr, be16_to_cpu(rt->path_rec[0].pkey));
	rt->addr.dev_addr.dev_type = RDMA_NODE_IB_CA;

	id_priv = container_of(id, struct rdma_id_private, id);
	id_priv->state = CMA_CONNECT;
	return id_priv;

destroy_id:
	rdma_destroy_id(id);
err:
	return NULL;
}

static struct rdma_id_private *cma_new_udp_id(struct rdma_cm_id *listen_id,
					      struct ib_cm_event *ib_event)
{
	struct rdma_id_private *id_priv;
	struct rdma_cm_id *id;
	union cma_ip_addr *src, *dst;
	__u16 port;
	u8 ip_ver;
	int ret;

	id = rdma_create_id(listen_id->event_handler, listen_id->context,
			    listen_id->ps);
	if (IS_ERR(id))
		return NULL;


	if (cma_get_net_info(ib_event->private_data, listen_id->ps,
			     &ip_ver, &port, &src, &dst))
		goto err;

	cma_save_net_info(&id->route.addr, &listen_id->route.addr,
			  ip_ver, port, src, dst);

	ret = rdma_translate_ip(&id->route.addr.src_addr,
				&id->route.addr.dev_addr);
	if (ret)
		goto err;

	id_priv = container_of(id, struct rdma_id_private, id);
	id_priv->state = CMA_CONNECT;
	return id_priv;
err:
	rdma_destroy_id(id);
	return NULL;
}

static void cma_set_req_event_data(struct rdma_cm_event *event,
				   struct ib_cm_req_event_param *req_data,
				   void *private_data, int offset)
{
	event->param.conn.private_data = private_data + offset;
	event->param.conn.private_data_len = IB_CM_REQ_PRIVATE_DATA_SIZE - offset;
	event->param.conn.responder_resources = req_data->responder_resources;
	event->param.conn.initiator_depth = req_data->initiator_depth;
	event->param.conn.flow_control = req_data->flow_control;
	event->param.conn.retry_count = req_data->retry_count;
	event->param.conn.rnr_retry_count = req_data->rnr_retry_count;
	event->param.conn.srq = req_data->srq;
	event->param.conn.qp_num = req_data->remote_qpn;
}

static int cma_req_handler(struct ib_cm_id *cm_id, struct ib_cm_event *ib_event)
{
	struct rdma_id_private *listen_id, *conn_id;
	struct rdma_cm_event event;
	int offset, ret;

	listen_id = cm_id->context;
	atomic_inc(&listen_id->dev_remove);
	if (!cma_comp(listen_id, CMA_LISTEN)) {
		ret = -ECONNABORTED;
		goto out;
	}

	memset(&event, 0, sizeof event);
	offset = cma_user_data_offset(listen_id->id.ps);
	event.event = RDMA_CM_EVENT_CONNECT_REQUEST;
	if (cma_is_ud_ps(listen_id->id.ps)) {
		conn_id = cma_new_udp_id(&listen_id->id, ib_event);
		event.param.ud.private_data = ib_event->private_data + offset;
		event.param.ud.private_data_len =
				IB_CM_SIDR_REQ_PRIVATE_DATA_SIZE - offset;
	} else {
		conn_id = cma_new_conn_id(&listen_id->id, ib_event);
		cma_set_req_event_data(&event, &ib_event->param.req_rcvd,
				       ib_event->private_data, offset);
	}
	if (!conn_id) {
		ret = -ENOMEM;
		goto out;
	}

	atomic_inc(&conn_id->dev_remove);
	mutex_lock(&lock);
	ret = cma_acquire_dev(conn_id);
	mutex_unlock(&lock);
	if (ret)
		goto release_conn_id;

	conn_id->cm_id.ib = cm_id;
	cm_id->context = conn_id;
	cm_id->cm_handler = cma_ib_handler;

	ret = conn_id->id.event_handler(&conn_id->id, &event);
	if (!ret)
		goto out;

	/* Destroy the CM ID by returning a non-zero value. */
	conn_id->cm_id.ib = NULL;

release_conn_id:
	cma_exch(conn_id, CMA_DESTROYING);
	cma_release_remove(conn_id);
	rdma_destroy_id(&conn_id->id);

out:
	cma_release_remove(listen_id);
	return ret;
}

static __be64 cma_get_service_id(enum rdma_port_space ps, struct sockaddr *addr)
{
	return cpu_to_be64(((u64)ps << 16) + be16_to_cpu(cma_port(addr)));
}

static void cma_set_compare_data(enum rdma_port_space ps, struct sockaddr *addr,
				 struct ib_cm_compare_data *compare)
{
	struct cma_hdr *cma_data, *cma_mask;
	struct sdp_hh *sdp_data, *sdp_mask;
	__u32 ip4_addr;
	struct in6_addr ip6_addr;

	memset(compare, 0, sizeof *compare);
	cma_data = (void *) compare->data;
	cma_mask = (void *) compare->mask;
	sdp_data = (void *) compare->data;
	sdp_mask = (void *) compare->mask;

	switch (addr->sa_family) {
	case AF_INET:
		ip4_addr = ((struct sockaddr_in *) addr)->sin_addr.s_addr;
		if (ps == RDMA_PS_SDP) {
			sdp_set_ip_ver(sdp_data, 4);
			sdp_set_ip_ver(sdp_mask, 0xF);
			sdp_data->dst_addr.ip4.addr = ip4_addr;
			sdp_mask->dst_addr.ip4.addr = ~0;
		} else {
			cma_set_ip_ver(cma_data, 4);
			cma_set_ip_ver(cma_mask, 0xF);
			cma_data->dst_addr.ip4.addr = ip4_addr;
			cma_mask->dst_addr.ip4.addr = ~0;
		}
		break;
	case AF_INET6:
		ip6_addr = ((struct sockaddr_in6 *) addr)->sin6_addr;
		if (ps == RDMA_PS_SDP) {
			sdp_set_ip_ver(sdp_data, 6);
			sdp_set_ip_ver(sdp_mask, 0xF);
			sdp_data->dst_addr.ip6 = ip6_addr;
			memset(&sdp_mask->dst_addr.ip6, 0xFF,
			       sizeof sdp_mask->dst_addr.ip6);
		} else {
			cma_set_ip_ver(cma_data, 6);
			cma_set_ip_ver(cma_mask, 0xF);
			cma_data->dst_addr.ip6 = ip6_addr;
			memset(&cma_mask->dst_addr.ip6, 0xFF,
			       sizeof cma_mask->dst_addr.ip6);
		}
		break;
	default:
		break;
	}
}

static int cma_iw_handler(struct iw_cm_id *iw_id, struct iw_cm_event *iw_event)
{
	struct rdma_id_private *id_priv = iw_id->context;
	struct rdma_cm_event event;
	struct sockaddr_in *sin;
	int ret = 0;

	memset(&event, 0, sizeof event);
	atomic_inc(&id_priv->dev_remove);

	switch (iw_event->event) {
	case IW_CM_EVENT_CLOSE:
		event.event = RDMA_CM_EVENT_DISCONNECTED;
		break;
	case IW_CM_EVENT_CONNECT_REPLY:
		sin = (struct sockaddr_in *) &id_priv->id.route.addr.src_addr;
		*sin = iw_event->local_addr;
		sin = (struct sockaddr_in *) &id_priv->id.route.addr.dst_addr;
		*sin = iw_event->remote_addr;
		switch (iw_event->status) {
		case 0:
			event.event = RDMA_CM_EVENT_ESTABLISHED;
			break;
		case -ECONNRESET:
		case -ECONNREFUSED:
			event.event = RDMA_CM_EVENT_REJECTED;
			break;
		case -ETIMEDOUT:
			event.event = RDMA_CM_EVENT_UNREACHABLE;
			break;
		default:
			event.event = RDMA_CM_EVENT_CONNECT_ERROR;
			break;
		}
		break;
	case IW_CM_EVENT_ESTABLISHED:
		event.event = RDMA_CM_EVENT_ESTABLISHED;
		break;
	default:
		BUG_ON(1);
	}

	event.status = iw_event->status;
	event.param.conn.private_data = iw_event->private_data;
	event.param.conn.private_data_len = iw_event->private_data_len;
	ret = id_priv->id.event_handler(&id_priv->id, &event);
	if (ret) {
		/* Destroy the CM ID by returning a non-zero value. */
		id_priv->cm_id.iw = NULL;
		cma_exch(id_priv, CMA_DESTROYING);
		cma_release_remove(id_priv);
		rdma_destroy_id(&id_priv->id);
		return ret;
	}

	cma_release_remove(id_priv);
	return ret;
}

static int iw_conn_req_handler(struct iw_cm_id *cm_id,
			       struct iw_cm_event *iw_event)
{
	struct rdma_cm_id *new_cm_id;
	struct rdma_id_private *listen_id, *conn_id;
	struct sockaddr_in *sin;
	struct net_device *dev = NULL;
	struct rdma_cm_event event;
	int ret;

	listen_id = cm_id->context;
	atomic_inc(&listen_id->dev_remove);
	if (!cma_comp(listen_id, CMA_LISTEN)) {
		ret = -ECONNABORTED;
		goto out;
	}

	/* Create a new RDMA id for the new IW CM ID */
	new_cm_id = rdma_create_id(listen_id->id.event_handler,
				   listen_id->id.context,
				   RDMA_PS_TCP);
	if (!new_cm_id) {
		ret = -ENOMEM;
		goto out;
	}
	conn_id = container_of(new_cm_id, struct rdma_id_private, id);
	atomic_inc(&conn_id->dev_remove);
	conn_id->state = CMA_CONNECT;

	dev = ip_dev_find(iw_event->local_addr.sin_addr.s_addr);
	if (!dev) {
		ret = -EADDRNOTAVAIL;
		cma_release_remove(conn_id);
		rdma_destroy_id(new_cm_id);
		goto out;
	}
	ret = rdma_copy_addr(&conn_id->id.route.addr.dev_addr, dev, NULL);
	if (ret) {
		cma_release_remove(conn_id);
		rdma_destroy_id(new_cm_id);
		goto out;
	}

	mutex_lock(&lock);
	ret = cma_acquire_dev(conn_id);
	mutex_unlock(&lock);
	if (ret) {
		cma_release_remove(conn_id);
		rdma_destroy_id(new_cm_id);
		goto out;
	}

	conn_id->cm_id.iw = cm_id;
	cm_id->context = conn_id;
	cm_id->cm_handler = cma_iw_handler;

	sin = (struct sockaddr_in *) &new_cm_id->route.addr.src_addr;
	*sin = iw_event->local_addr;
	sin = (struct sockaddr_in *) &new_cm_id->route.addr.dst_addr;
	*sin = iw_event->remote_addr;

	memset(&event, 0, sizeof event);
	event.event = RDMA_CM_EVENT_CONNECT_REQUEST;
	event.param.conn.private_data = iw_event->private_data;
	event.param.conn.private_data_len = iw_event->private_data_len;
	ret = conn_id->id.event_handler(&conn_id->id, &event);
	if (ret) {
		/* User wants to destroy the CM ID */
		conn_id->cm_id.iw = NULL;
		cma_exch(conn_id, CMA_DESTROYING);
		cma_release_remove(conn_id);
		rdma_destroy_id(&conn_id->id);
	}

out:
	if (dev)
		dev_put(dev);
	cma_release_remove(listen_id);
	return ret;
}

static int cma_ib_listen(struct rdma_id_private *id_priv)
{
	struct ib_cm_compare_data compare_data;
	struct sockaddr *addr;
	__be64 svc_id;
	int ret;

	id_priv->cm_id.ib = ib_create_cm_id(id_priv->id.device, cma_req_handler,
					    id_priv);
	if (IS_ERR(id_priv->cm_id.ib))
		return PTR_ERR(id_priv->cm_id.ib);

	addr = &id_priv->id.route.addr.src_addr;
	svc_id = cma_get_service_id(id_priv->id.ps, addr);
	if (cma_any_addr(addr))
		ret = ib_cm_listen(id_priv->cm_id.ib, svc_id, 0, NULL);
	else {
		cma_set_compare_data(id_priv->id.ps, addr, &compare_data);
		ret = ib_cm_listen(id_priv->cm_id.ib, svc_id, 0, &compare_data);
	}

	if (ret) {
		ib_destroy_cm_id(id_priv->cm_id.ib);
		id_priv->cm_id.ib = NULL;
	}

	return ret;
}

static int cma_iw_listen(struct rdma_id_private *id_priv, int backlog)
{
	int ret;
	struct sockaddr_in *sin;

	id_priv->cm_id.iw = iw_create_cm_id(id_priv->id.device,
					    iw_conn_req_handler,
					    id_priv);
	if (IS_ERR(id_priv->cm_id.iw))
		return PTR_ERR(id_priv->cm_id.iw);

	sin = (struct sockaddr_in *) &id_priv->id.route.addr.src_addr;
	id_priv->cm_id.iw->local_addr = *sin;

	ret = iw_cm_listen(id_priv->cm_id.iw, backlog);

	if (ret) {
		iw_destroy_cm_id(id_priv->cm_id.iw);
		id_priv->cm_id.iw = NULL;
	}

	return ret;
}

static int cma_listen_handler(struct rdma_cm_id *id,
			      struct rdma_cm_event *event)
{
	struct rdma_id_private *id_priv = id->context;

	id->context = id_priv->id.context;
	id->event_handler = id_priv->id.event_handler;
	return id_priv->id.event_handler(id, event);
}

static void cma_listen_on_dev(struct rdma_id_private *id_priv,
			      struct cma_device *cma_dev)
{
	struct rdma_id_private *dev_id_priv;
	struct rdma_cm_id *id;
	int ret;

	id = rdma_create_id(cma_listen_handler, id_priv, id_priv->id.ps);
	if (IS_ERR(id))
		return;

	dev_id_priv = container_of(id, struct rdma_id_private, id);

	dev_id_priv->state = CMA_ADDR_BOUND;
	memcpy(&id->route.addr.src_addr, &id_priv->id.route.addr.src_addr,
	       ip_addr_size(&id_priv->id.route.addr.src_addr));

	cma_attach_to_dev(dev_id_priv, cma_dev);
	list_add_tail(&dev_id_priv->listen_list, &id_priv->listen_list);

	ret = rdma_listen(id, id_priv->backlog);
	if (ret)
		goto err;

	return;
err:
	cma_destroy_listen(dev_id_priv);
}

static void cma_listen_on_all(struct rdma_id_private *id_priv)
{
	struct cma_device *cma_dev;

	mutex_lock(&lock);
	list_add_tail(&id_priv->list, &listen_any_list);
	list_for_each_entry(cma_dev, &dev_list, list)
		cma_listen_on_dev(id_priv, cma_dev);
	mutex_unlock(&lock);
}

static int cma_bind_any(struct rdma_cm_id *id, sa_family_t af)
{
	struct sockaddr_in addr_in;

	memset(&addr_in, 0, sizeof addr_in);
	addr_in.sin_family = af;
	return rdma_bind_addr(id, (struct sockaddr *) &addr_in);
}

int rdma_listen(struct rdma_cm_id *id, int backlog)
{
	struct rdma_id_private *id_priv;
	int ret;

	id_priv = container_of(id, struct rdma_id_private, id);
	if (id_priv->state == CMA_IDLE) {
		ret = cma_bind_any(id, AF_INET);
		if (ret)
			return ret;
	}

	if (!cma_comp_exch(id_priv, CMA_ADDR_BOUND, CMA_LISTEN))
		return -EINVAL;

	id_priv->backlog = backlog;
	if (id->device) {
		switch (rdma_node_get_transport(id->device->node_type)) {
		case RDMA_TRANSPORT_IB:
			ret = cma_ib_listen(id_priv);
			if (ret)
				goto err;
			break;
		case RDMA_TRANSPORT_IWARP:
			ret = cma_iw_listen(id_priv, backlog);
			if (ret)
				goto err;
			break;
		default:
			ret = -ENOSYS;
			goto err;
		}
	} else
		cma_listen_on_all(id_priv);

	return 0;
err:
	id_priv->backlog = 0;
	cma_comp_exch(id_priv, CMA_LISTEN, CMA_ADDR_BOUND);
	return ret;
}
EXPORT_SYMBOL(rdma_listen);

static void cma_query_handler(int status, struct ib_sa_path_rec *path_rec,
			      void *context)
{
	struct cma_work *work = context;
	struct rdma_route *route;

	route = &work->id->id.route;

	if (!status) {
		route->num_paths = 1;
		*route->path_rec = *path_rec;
	} else {
		work->old_state = CMA_ROUTE_QUERY;
		work->new_state = CMA_ADDR_RESOLVED;
		work->event.event = RDMA_CM_EVENT_ROUTE_ERROR;
		work->event.status = status;
	}

	queue_work(cma_wq, &work->work);
}

static int cma_query_ib_route(struct rdma_id_private *id_priv, int timeout_ms,
			      struct cma_work *work)
{
	struct rdma_dev_addr *addr = &id_priv->id.route.addr.dev_addr;
	struct ib_sa_path_rec path_rec;

	memset(&path_rec, 0, sizeof path_rec);
	ib_addr_get_sgid(addr, &path_rec.sgid);
	ib_addr_get_dgid(addr, &path_rec.dgid);
	path_rec.pkey = cpu_to_be16(ib_addr_get_pkey(addr));
	path_rec.numb_path = 1;
	path_rec.reversible = 1;

	id_priv->query_id = ib_sa_path_rec_get(&sa_client, id_priv->id.device,
				id_priv->id.port_num, &path_rec,
				IB_SA_PATH_REC_DGID | IB_SA_PATH_REC_SGID |
				IB_SA_PATH_REC_PKEY | IB_SA_PATH_REC_NUMB_PATH |
				IB_SA_PATH_REC_REVERSIBLE,
				timeout_ms, GFP_KERNEL,
				cma_query_handler, work, &id_priv->query);

	return (id_priv->query_id < 0) ? id_priv->query_id : 0;
}

static void cma_work_handler(struct work_struct *_work)
{
	struct cma_work *work = container_of(_work, struct cma_work, work);
	struct rdma_id_private *id_priv = work->id;
	int destroy = 0;

	atomic_inc(&id_priv->dev_remove);
	if (!cma_comp_exch(id_priv, work->old_state, work->new_state))
		goto out;

	if (id_priv->id.event_handler(&id_priv->id, &work->event)) {
		cma_exch(id_priv, CMA_DESTROYING);
		destroy = 1;
	}
out:
	cma_release_remove(id_priv);
	cma_deref_id(id_priv);
	if (destroy)
		rdma_destroy_id(&id_priv->id);
	kfree(work);
}

static int cma_resolve_ib_route(struct rdma_id_private *id_priv, int timeout_ms)
{
	struct rdma_route *route = &id_priv->id.route;
	struct cma_work *work;
	int ret;

	work = kzalloc(sizeof *work, GFP_KERNEL);
	if (!work)
		return -ENOMEM;

	work->id = id_priv;
	INIT_WORK(&work->work, cma_work_handler);
	work->old_state = CMA_ROUTE_QUERY;
	work->new_state = CMA_ROUTE_RESOLVED;
	work->event.event = RDMA_CM_EVENT_ROUTE_RESOLVED;

	route->path_rec = kmalloc(sizeof *route->path_rec, GFP_KERNEL);
	if (!route->path_rec) {
		ret = -ENOMEM;
		goto err1;
	}

	ret = cma_query_ib_route(id_priv, timeout_ms, work);
	if (ret)
		goto err2;

	return 0;
err2:
	kfree(route->path_rec);
	route->path_rec = NULL;
err1:
	kfree(work);
	return ret;
}

int rdma_set_ib_paths(struct rdma_cm_id *id,
		      struct ib_sa_path_rec *path_rec, int num_paths)
{
	struct rdma_id_private *id_priv;
	int ret;

	id_priv = container_of(id, struct rdma_id_private, id);
	if (!cma_comp_exch(id_priv, CMA_ADDR_RESOLVED, CMA_ROUTE_RESOLVED))
		return -EINVAL;

	id->route.path_rec = kmalloc(sizeof *path_rec * num_paths, GFP_KERNEL);
	if (!id->route.path_rec) {
		ret = -ENOMEM;
		goto err;
	}

	memcpy(id->route.path_rec, path_rec, sizeof *path_rec * num_paths);
	return 0;
err:
	cma_comp_exch(id_priv, CMA_ROUTE_RESOLVED, CMA_ADDR_RESOLVED);
	return ret;
}
EXPORT_SYMBOL(rdma_set_ib_paths);

static int cma_resolve_iw_route(struct rdma_id_private *id_priv, int timeout_ms)
{
	struct cma_work *work;

	work = kzalloc(sizeof *work, GFP_KERNEL);
	if (!work)
		return -ENOMEM;

	work->id = id_priv;
	INIT_WORK(&work->work, cma_work_handler);
	work->old_state = CMA_ROUTE_QUERY;
	work->new_state = CMA_ROUTE_RESOLVED;
	work->event.event = RDMA_CM_EVENT_ROUTE_RESOLVED;
	queue_work(cma_wq, &work->work);
	return 0;
}

int rdma_resolve_route(struct rdma_cm_id *id, int timeout_ms)
{
	struct rdma_id_private *id_priv;
	int ret;

	id_priv = container_of(id, struct rdma_id_private, id);
	if (!cma_comp_exch(id_priv, CMA_ADDR_RESOLVED, CMA_ROUTE_QUERY))
		return -EINVAL;

	atomic_inc(&id_priv->refcount);
	switch (rdma_node_get_transport(id->device->node_type)) {
	case RDMA_TRANSPORT_IB:
		ret = cma_resolve_ib_route(id_priv, timeout_ms);
		break;
	case RDMA_TRANSPORT_IWARP:
		ret = cma_resolve_iw_route(id_priv, timeout_ms);
		break;
	default:
		ret = -ENOSYS;
		break;
	}
	if (ret)
		goto err;

	return 0;
err:
	cma_comp_exch(id_priv, CMA_ROUTE_QUERY, CMA_ADDR_RESOLVED);
	cma_deref_id(id_priv);
	return ret;
}
EXPORT_SYMBOL(rdma_resolve_route);

static int cma_bind_loopback(struct rdma_id_private *id_priv)
{
	struct cma_device *cma_dev;
	struct ib_port_attr port_attr;
	union ib_gid gid;
	u16 pkey;
	int ret;
	u8 p;

	mutex_lock(&lock);
	if (list_empty(&dev_list)) {
		ret = -ENODEV;
		goto out;
	}
	list_for_each_entry(cma_dev, &dev_list, list)
		for (p = 1; p <= cma_dev->device->phys_port_cnt; ++p)
			if (!ib_query_port(cma_dev->device, p, &port_attr) &&
			    port_attr.state == IB_PORT_ACTIVE)
				goto port_found;

	p = 1;
	cma_dev = list_entry(dev_list.next, struct cma_device, list);

port_found:
	ret = ib_get_cached_gid(cma_dev->device, p, 0, &gid);
	if (ret)
		goto out;

	ret = ib_get_cached_pkey(cma_dev->device, p, 0, &pkey);
	if (ret)
		goto out;

	ib_addr_set_sgid(&id_priv->id.route.addr.dev_addr, &gid);
	ib_addr_set_pkey(&id_priv->id.route.addr.dev_addr, pkey);
	id_priv->id.port_num = p;
	cma_attach_to_dev(id_priv, cma_dev);
out:
	mutex_unlock(&lock);
	return ret;
}

static void addr_handler(int status, struct sockaddr *src_addr,
			 struct rdma_dev_addr *dev_addr, void *context)
{
	struct rdma_id_private *id_priv = context;
	struct rdma_cm_event event;

	memset(&event, 0, sizeof event);
	atomic_inc(&id_priv->dev_remove);

	/*
	 * Grab mutex to block rdma_destroy_id() from removing the device while
	 * we're trying to acquire it.
	 */
	mutex_lock(&lock);
	if (!cma_comp_exch(id_priv, CMA_ADDR_QUERY, CMA_ADDR_RESOLVED)) {
		mutex_unlock(&lock);
		goto out;
	}

	if (!status && !id_priv->cma_dev)
		status = cma_acquire_dev(id_priv);
	mutex_unlock(&lock);

	if (status) {
		if (!cma_comp_exch(id_priv, CMA_ADDR_RESOLVED, CMA_ADDR_BOUND))
			goto out;
		event.event = RDMA_CM_EVENT_ADDR_ERROR;
		event.status = status;
	} else {
		memcpy(&id_priv->id.route.addr.src_addr, src_addr,
		       ip_addr_size(src_addr));
		event.event = RDMA_CM_EVENT_ADDR_RESOLVED;
	}

	if (id_priv->id.event_handler(&id_priv->id, &event)) {
		cma_exch(id_priv, CMA_DESTROYING);
		cma_release_remove(id_priv);
		cma_deref_id(id_priv);
		rdma_destroy_id(&id_priv->id);
		return;
	}
out:
	cma_release_remove(id_priv);
	cma_deref_id(id_priv);
}

static int cma_resolve_loopback(struct rdma_id_private *id_priv)
{
	struct cma_work *work;
	struct sockaddr_in *src_in, *dst_in;
	union ib_gid gid;
	int ret;

	work = kzalloc(sizeof *work, GFP_KERNEL);
	if (!work)
		return -ENOMEM;

	if (!id_priv->cma_dev) {
		ret = cma_bind_loopback(id_priv);
		if (ret)
			goto err;
	}

	ib_addr_get_sgid(&id_priv->id.route.addr.dev_addr, &gid);
	ib_addr_set_dgid(&id_priv->id.route.addr.dev_addr, &gid);

	if (cma_zero_addr(&id_priv->id.route.addr.src_addr)) {
		src_in = (struct sockaddr_in *)&id_priv->id.route.addr.src_addr;
		dst_in = (struct sockaddr_in *)&id_priv->id.route.addr.dst_addr;
		src_in->sin_family = dst_in->sin_family;
		src_in->sin_addr.s_addr = dst_in->sin_addr.s_addr;
	}

	work->id = id_priv;
	INIT_WORK(&work->work, cma_work_handler);
	work->old_state = CMA_ADDR_QUERY;
	work->new_state = CMA_ADDR_RESOLVED;
	work->event.event = RDMA_CM_EVENT_ADDR_RESOLVED;
	queue_work(cma_wq, &work->work);
	return 0;
err:
	kfree(work);
	return ret;
}

static int cma_bind_addr(struct rdma_cm_id *id, struct sockaddr *src_addr,
			 struct sockaddr *dst_addr)
{
	if (src_addr && src_addr->sa_family)
		return rdma_bind_addr(id, src_addr);
	else
		return cma_bind_any(id, dst_addr->sa_family);
}

int rdma_resolve_addr(struct rdma_cm_id *id, struct sockaddr *src_addr,
		      struct sockaddr *dst_addr, int timeout_ms)
{
	struct rdma_id_private *id_priv;
	int ret;

	id_priv = container_of(id, struct rdma_id_private, id);
	if (id_priv->state == CMA_IDLE) {
		ret = cma_bind_addr(id, src_addr, dst_addr);
		if (ret)
			return ret;
	}

	if (!cma_comp_exch(id_priv, CMA_ADDR_BOUND, CMA_ADDR_QUERY))
		return -EINVAL;

	atomic_inc(&id_priv->refcount);
	memcpy(&id->route.addr.dst_addr, dst_addr, ip_addr_size(dst_addr));
	if (cma_any_addr(dst_addr))
		ret = cma_resolve_loopback(id_priv);
	else
		ret = rdma_resolve_ip(&addr_client, &id->route.addr.src_addr,
				      dst_addr, &id->route.addr.dev_addr,
				      timeout_ms, addr_handler, id_priv);
	if (ret)
		goto err;

	return 0;
err:
	cma_comp_exch(id_priv, CMA_ADDR_QUERY, CMA_ADDR_BOUND);
	cma_deref_id(id_priv);
	return ret;
}
EXPORT_SYMBOL(rdma_resolve_addr);

static void cma_bind_port(struct rdma_bind_list *bind_list,
			  struct rdma_id_private *id_priv)
{
	struct sockaddr_in *sin;

	sin = (struct sockaddr_in *) &id_priv->id.route.addr.src_addr;
	sin->sin_port = htons(bind_list->port);
	id_priv->bind_list = bind_list;
	hlist_add_head(&id_priv->node, &bind_list->owners);
}

static int cma_alloc_port(struct idr *ps, struct rdma_id_private *id_priv,
			  unsigned short snum)
{
	struct rdma_bind_list *bind_list;
	int port, ret;

	bind_list = kmalloc(sizeof *bind_list, GFP_KERNEL);
	if (!bind_list)
		return -ENOMEM;

	do {
		ret = idr_get_new_above(ps, bind_list, snum, &port);
	} while ((ret == -EAGAIN) && idr_pre_get(ps, GFP_KERNEL));

	if (ret)
		goto err1;

	if (port != snum) {
		ret = -EADDRNOTAVAIL;
		goto err2;
	}

	bind_list->ps = ps;
	bind_list->port = (unsigned short) port;
	cma_bind_port(bind_list, id_priv);
	return 0;
err2:
	idr_remove(ps, port);
err1:
	kfree(bind_list);
	return ret;
}

static int cma_alloc_any_port(struct idr *ps, struct rdma_id_private *id_priv)
{
	struct rdma_bind_list *bind_list;
	int port, ret;

	bind_list = kzalloc(sizeof *bind_list, GFP_KERNEL);
	if (!bind_list)
		return -ENOMEM;

retry:
	do {
		ret = idr_get_new_above(ps, bind_list, next_port, &port);
	} while ((ret == -EAGAIN) && idr_pre_get(ps, GFP_KERNEL));

	if (ret)
		goto err1;

	if (port > sysctl_local_port_range[1]) {
		if (next_port != sysctl_local_port_range[0]) {
			idr_remove(ps, port);
			next_port = sysctl_local_port_range[0];
			goto retry;
		}
		ret = -EADDRNOTAVAIL;
		goto err2;
	}

	if (port == sysctl_local_port_range[1])
		next_port = sysctl_local_port_range[0];
	else
		next_port = port + 1;

	bind_list->ps = ps;
	bind_list->port = (unsigned short) port;
	cma_bind_port(bind_list, id_priv);
	return 0;
err2:
	idr_remove(ps, port);
err1:
	kfree(bind_list);
	return ret;
}

static int cma_use_port(struct idr *ps, struct rdma_id_private *id_priv)
{
	struct rdma_id_private *cur_id;
	struct sockaddr_in *sin, *cur_sin;
	struct rdma_bind_list *bind_list;
	struct hlist_node *node;
	unsigned short snum;

	sin = (struct sockaddr_in *) &id_priv->id.route.addr.src_addr;
	snum = ntohs(sin->sin_port);
	if (snum < PROT_SOCK && !capable(CAP_NET_BIND_SERVICE))
		return -EACCES;

	bind_list = idr_find(ps, snum);
	if (!bind_list)
		return cma_alloc_port(ps, id_priv, snum);

	/*
	 * We don't support binding to any address if anyone is bound to
	 * a specific address on the same port.
	 */
	if (cma_any_addr(&id_priv->id.route.addr.src_addr))
		return -EADDRNOTAVAIL;

	hlist_for_each_entry(cur_id, node, &bind_list->owners, node) {
		if (cma_any_addr(&cur_id->id.route.addr.src_addr))
			return -EADDRNOTAVAIL;

		cur_sin = (struct sockaddr_in *) &cur_id->id.route.addr.src_addr;
		if (sin->sin_addr.s_addr == cur_sin->sin_addr.s_addr)
			return -EADDRINUSE;
	}

	cma_bind_port(bind_list, id_priv);
	return 0;
}

static int cma_get_port(struct rdma_id_private *id_priv)
{
	struct idr *ps;
	int ret;

	switch (id_priv->id.ps) {
	case RDMA_PS_SDP:
		ps = &sdp_ps;
		break;
	case RDMA_PS_TCP:
		ps = &tcp_ps;
		break;
	case RDMA_PS_UDP:
		ps = &udp_ps;
		break;
	case RDMA_PS_IPOIB:
		ps = &ipoib_ps;
		break;
	default:
		return -EPROTONOSUPPORT;
	}

	mutex_lock(&lock);
	if (cma_any_port(&id_priv->id.route.addr.src_addr))
		ret = cma_alloc_any_port(ps, id_priv);
	else
		ret = cma_use_port(ps, id_priv);
	mutex_unlock(&lock);

	return ret;
}

int rdma_bind_addr(struct rdma_cm_id *id, struct sockaddr *addr)
{
	struct rdma_id_private *id_priv;
	int ret;

	if (addr->sa_family != AF_INET)
		return -EAFNOSUPPORT;

	id_priv = container_of(id, struct rdma_id_private, id);
	if (!cma_comp_exch(id_priv, CMA_IDLE, CMA_ADDR_BOUND))
		return -EINVAL;

	if (!cma_any_addr(addr)) {
		ret = rdma_translate_ip(addr, &id->route.addr.dev_addr);
		if (ret)
			goto err1;

		mutex_lock(&lock);
		ret = cma_acquire_dev(id_priv);
		mutex_unlock(&lock);
		if (ret)
			goto err1;
	}

	memcpy(&id->route.addr.src_addr, addr, ip_addr_size(addr));
	ret = cma_get_port(id_priv);
	if (ret)
		goto err2;

	return 0;
err2:
	if (!cma_any_addr(addr)) {
		mutex_lock(&lock);
		cma_detach_from_dev(id_priv);
		mutex_unlock(&lock);
	}
err1:
	cma_comp_exch(id_priv, CMA_ADDR_BOUND, CMA_IDLE);
	return ret;
}
EXPORT_SYMBOL(rdma_bind_addr);

static int cma_format_hdr(void *hdr, enum rdma_port_space ps,
			  struct rdma_route *route)
{
	struct sockaddr_in *src4, *dst4;
	struct cma_hdr *cma_hdr;
	struct sdp_hh *sdp_hdr;

	src4 = (struct sockaddr_in *) &route->addr.src_addr;
	dst4 = (struct sockaddr_in *) &route->addr.dst_addr;

	switch (ps) {
	case RDMA_PS_SDP:
		sdp_hdr = hdr;
		if (sdp_get_majv(sdp_hdr->sdp_version) != SDP_MAJ_VERSION)
			return -EINVAL;
		sdp_set_ip_ver(sdp_hdr, 4);
		sdp_hdr->src_addr.ip4.addr = src4->sin_addr.s_addr;
		sdp_hdr->dst_addr.ip4.addr = dst4->sin_addr.s_addr;
		sdp_hdr->port = src4->sin_port;
		break;
	default:
		cma_hdr = hdr;
		cma_hdr->cma_version = CMA_VERSION;
		cma_set_ip_ver(cma_hdr, 4);
		cma_hdr->src_addr.ip4.addr = src4->sin_addr.s_addr;
		cma_hdr->dst_addr.ip4.addr = dst4->sin_addr.s_addr;
		cma_hdr->port = src4->sin_port;
		break;
	}
	return 0;
}

static int cma_sidr_rep_handler(struct ib_cm_id *cm_id,
				struct ib_cm_event *ib_event)
{
	struct rdma_id_private *id_priv = cm_id->context;
	struct rdma_cm_event event;
	struct ib_cm_sidr_rep_event_param *rep = &ib_event->param.sidr_rep_rcvd;
	int ret = 0;

	memset(&event, 0, sizeof event);
	atomic_inc(&id_priv->dev_remove);
	if (!cma_comp(id_priv, CMA_CONNECT))
		goto out;

	switch (ib_event->event) {
	case IB_CM_SIDR_REQ_ERROR:
		event.event = RDMA_CM_EVENT_UNREACHABLE;
		event.status = -ETIMEDOUT;
		break;
	case IB_CM_SIDR_REP_RECEIVED:
		event.param.ud.private_data = ib_event->private_data;
		event.param.ud.private_data_len = IB_CM_SIDR_REP_PRIVATE_DATA_SIZE;
		if (rep->status != IB_SIDR_SUCCESS) {
			event.event = RDMA_CM_EVENT_UNREACHABLE;
			event.status = ib_event->param.sidr_rep_rcvd.status;
			break;
		}
		if (id_priv->qkey != rep->qkey) {
			event.event = RDMA_CM_EVENT_UNREACHABLE;
			event.status = -EINVAL;
			break;
		}
		ib_init_ah_from_path(id_priv->id.device, id_priv->id.port_num,
				     id_priv->id.route.path_rec,
				     &event.param.ud.ah_attr);
		event.param.ud.qp_num = rep->qpn;
		event.param.ud.qkey = rep->qkey;
		event.event = RDMA_CM_EVENT_ESTABLISHED;
		event.status = 0;
		break;
	default:
		printk(KERN_ERR "RDMA CMA: unexpected IB CM event: %d",
		       ib_event->event);
		goto out;
	}

	ret = id_priv->id.event_handler(&id_priv->id, &event);
	if (ret) {
		/* Destroy the CM ID by returning a non-zero value. */
		id_priv->cm_id.ib = NULL;
		cma_exch(id_priv, CMA_DESTROYING);
		cma_release_remove(id_priv);
		rdma_destroy_id(&id_priv->id);
		return ret;
	}
out:
	cma_release_remove(id_priv);
	return ret;
}

static int cma_resolve_ib_udp(struct rdma_id_private *id_priv,
			      struct rdma_conn_param *conn_param)
{
	struct ib_cm_sidr_req_param req;
	struct rdma_route *route;
	int ret;

	req.private_data_len = sizeof(struct cma_hdr) +
			       conn_param->private_data_len;
	req.private_data = kzalloc(req.private_data_len, GFP_ATOMIC);
	if (!req.private_data)
		return -ENOMEM;

	if (conn_param->private_data && conn_param->private_data_len)
		memcpy((void *) req.private_data + sizeof(struct cma_hdr),
		       conn_param->private_data, conn_param->private_data_len);

	route = &id_priv->id.route;
	ret = cma_format_hdr((void *) req.private_data, id_priv->id.ps, route);
	if (ret)
		goto out;

	id_priv->cm_id.ib = ib_create_cm_id(id_priv->id.device,
					    cma_sidr_rep_handler, id_priv);
	if (IS_ERR(id_priv->cm_id.ib)) {
		ret = PTR_ERR(id_priv->cm_id.ib);
		goto out;
	}

	req.path = route->path_rec;
	req.service_id = cma_get_service_id(id_priv->id.ps,
					    &route->addr.dst_addr);
	req.timeout_ms = 1 << (CMA_CM_RESPONSE_TIMEOUT - 8);
	req.max_cm_retries = CMA_MAX_CM_RETRIES;

	ret = ib_send_cm_sidr_req(id_priv->cm_id.ib, &req);
	if (ret) {
		ib_destroy_cm_id(id_priv->cm_id.ib);
		id_priv->cm_id.ib = NULL;
	}
out:
	kfree(req.private_data);
	return ret;
}

static int cma_connect_ib(struct rdma_id_private *id_priv,
			  struct rdma_conn_param *conn_param)
{
	struct ib_cm_req_param req;
	struct rdma_route *route;
	void *private_data;
	int offset, ret;

	memset(&req, 0, sizeof req);
	offset = cma_user_data_offset(id_priv->id.ps);
	req.private_data_len = offset + conn_param->private_data_len;
	private_data = kzalloc(req.private_data_len, GFP_ATOMIC);
	if (!private_data)
		return -ENOMEM;

	if (conn_param->private_data && conn_param->private_data_len)
		memcpy(private_data + offset, conn_param->private_data,
		       conn_param->private_data_len);

	id_priv->cm_id.ib = ib_create_cm_id(id_priv->id.device, cma_ib_handler,
					    id_priv);
	if (IS_ERR(id_priv->cm_id.ib)) {
		ret = PTR_ERR(id_priv->cm_id.ib);
		goto out;
	}

	route = &id_priv->id.route;
	ret = cma_format_hdr(private_data, id_priv->id.ps, route);
	if (ret)
		goto out;
	req.private_data = private_data;

	req.primary_path = &route->path_rec[0];
	if (route->num_paths == 2)
		req.alternate_path = &route->path_rec[1];

	req.service_id = cma_get_service_id(id_priv->id.ps,
					    &route->addr.dst_addr);
	req.qp_num = id_priv->qp_num;
	req.qp_type = IB_QPT_RC;
	req.starting_psn = id_priv->seq_num;
	req.responder_resources = conn_param->responder_resources;
	req.initiator_depth = conn_param->initiator_depth;
	req.flow_control = conn_param->flow_control;
	req.retry_count = conn_param->retry_count;
	req.rnr_retry_count = conn_param->rnr_retry_count;
	req.remote_cm_response_timeout = CMA_CM_RESPONSE_TIMEOUT;
	req.local_cm_response_timeout = CMA_CM_RESPONSE_TIMEOUT;
	req.max_cm_retries = CMA_MAX_CM_RETRIES;
	req.srq = id_priv->srq ? 1 : 0;

	ret = ib_send_cm_req(id_priv->cm_id.ib, &req);
out:
	if (ret && !IS_ERR(id_priv->cm_id.ib)) {
		ib_destroy_cm_id(id_priv->cm_id.ib);
		id_priv->cm_id.ib = NULL;
	}

	kfree(private_data);
	return ret;
}

static int cma_connect_iw(struct rdma_id_private *id_priv,
			  struct rdma_conn_param *conn_param)
{
	struct iw_cm_id *cm_id;
	struct sockaddr_in* sin;
	int ret;
	struct iw_cm_conn_param iw_param;

	cm_id = iw_create_cm_id(id_priv->id.device, cma_iw_handler, id_priv);
	if (IS_ERR(cm_id)) {
		ret = PTR_ERR(cm_id);
		goto out;
	}

	id_priv->cm_id.iw = cm_id;

	sin = (struct sockaddr_in*) &id_priv->id.route.addr.src_addr;
	cm_id->local_addr = *sin;

	sin = (struct sockaddr_in*) &id_priv->id.route.addr.dst_addr;
	cm_id->remote_addr = *sin;

	ret = cma_modify_qp_rtr(&id_priv->id);
	if (ret)
		goto out;

	iw_param.ord = conn_param->initiator_depth;
	iw_param.ird = conn_param->responder_resources;
	iw_param.private_data = conn_param->private_data;
	iw_param.private_data_len = conn_param->private_data_len;
	if (id_priv->id.qp)
		iw_param.qpn = id_priv->qp_num;
	else
		iw_param.qpn = conn_param->qp_num;
	ret = iw_cm_connect(cm_id, &iw_param);
out:
	if (ret && !IS_ERR(cm_id)) {
		iw_destroy_cm_id(cm_id);
		id_priv->cm_id.iw = NULL;
	}
	return ret;
}

int rdma_connect(struct rdma_cm_id *id, struct rdma_conn_param *conn_param)
{
	struct rdma_id_private *id_priv;
	int ret;

	id_priv = container_of(id, struct rdma_id_private, id);
	if (!cma_comp_exch(id_priv, CMA_ROUTE_RESOLVED, CMA_CONNECT))
		return -EINVAL;

	if (!id->qp) {
		id_priv->qp_num = conn_param->qp_num;
		id_priv->srq = conn_param->srq;
	}

	switch (rdma_node_get_transport(id->device->node_type)) {
	case RDMA_TRANSPORT_IB:
		if (cma_is_ud_ps(id->ps))
			ret = cma_resolve_ib_udp(id_priv, conn_param);
		else
			ret = cma_connect_ib(id_priv, conn_param);
		break;
	case RDMA_TRANSPORT_IWARP:
		ret = cma_connect_iw(id_priv, conn_param);
		break;
	default:
		ret = -ENOSYS;
		break;
	}
	if (ret)
		goto err;

	return 0;
err:
	cma_comp_exch(id_priv, CMA_CONNECT, CMA_ROUTE_RESOLVED);
	return ret;
}
EXPORT_SYMBOL(rdma_connect);

static int cma_accept_ib(struct rdma_id_private *id_priv,
			 struct rdma_conn_param *conn_param)
{
	struct ib_cm_rep_param rep;
	struct ib_qp_attr qp_attr;
	int qp_attr_mask, ret;

	if (id_priv->id.qp) {
		ret = cma_modify_qp_rtr(&id_priv->id);
		if (ret)
			goto out;

		qp_attr.qp_state = IB_QPS_RTS;
		ret = ib_cm_init_qp_attr(id_priv->cm_id.ib, &qp_attr,
					 &qp_attr_mask);
		if (ret)
			goto out;

		qp_attr.max_rd_atomic = conn_param->initiator_depth;
		ret = ib_modify_qp(id_priv->id.qp, &qp_attr, qp_attr_mask);
		if (ret)
			goto out;
	}

	memset(&rep, 0, sizeof rep);
	rep.qp_num = id_priv->qp_num;
	rep.starting_psn = id_priv->seq_num;
	rep.private_data = conn_param->private_data;
	rep.private_data_len = conn_param->private_data_len;
	rep.responder_resources = conn_param->responder_resources;
	rep.initiator_depth = conn_param->initiator_depth;
	rep.target_ack_delay = CMA_CM_RESPONSE_TIMEOUT;
	rep.failover_accepted = 0;
	rep.flow_control = conn_param->flow_control;
	rep.rnr_retry_count = conn_param->rnr_retry_count;
	rep.srq = id_priv->srq ? 1 : 0;

	ret = ib_send_cm_rep(id_priv->cm_id.ib, &rep);
out:
	return ret;
}

static int cma_accept_iw(struct rdma_id_private *id_priv,
		  struct rdma_conn_param *conn_param)
{
	struct iw_cm_conn_param iw_param;
	int ret;

	ret = cma_modify_qp_rtr(&id_priv->id);
	if (ret)
		return ret;

	iw_param.ord = conn_param->initiator_depth;
	iw_param.ird = conn_param->responder_resources;
	iw_param.private_data = conn_param->private_data;
	iw_param.private_data_len = conn_param->private_data_len;
	if (id_priv->id.qp) {
		iw_param.qpn = id_priv->qp_num;
	} else
		iw_param.qpn = conn_param->qp_num;

	return iw_cm_accept(id_priv->cm_id.iw, &iw_param);
}

static int cma_send_sidr_rep(struct rdma_id_private *id_priv,
			     enum ib_cm_sidr_status status,
			     const void *private_data, int private_data_len)
{
	struct ib_cm_sidr_rep_param rep;

	memset(&rep, 0, sizeof rep);
	rep.status = status;
	if (status == IB_SIDR_SUCCESS) {
		rep.qp_num = id_priv->qp_num;
		rep.qkey = id_priv->qkey;
	}
	rep.private_data = private_data;
	rep.private_data_len = private_data_len;

	return ib_send_cm_sidr_rep(id_priv->cm_id.ib, &rep);
}

int rdma_accept(struct rdma_cm_id *id, struct rdma_conn_param *conn_param)
{
	struct rdma_id_private *id_priv;
	int ret;

	id_priv = container_of(id, struct rdma_id_private, id);
	if (!cma_comp(id_priv, CMA_CONNECT))
		return -EINVAL;

	if (!id->qp && conn_param) {
		id_priv->qp_num = conn_param->qp_num;
		id_priv->srq = conn_param->srq;
	}

	switch (rdma_node_get_transport(id->device->node_type)) {
	case RDMA_TRANSPORT_IB:
		if (cma_is_ud_ps(id->ps))
			ret = cma_send_sidr_rep(id_priv, IB_SIDR_SUCCESS,
						conn_param->private_data,
						conn_param->private_data_len);
		else if (conn_param)
			ret = cma_accept_ib(id_priv, conn_param);
		else
			ret = cma_rep_recv(id_priv);
		break;
	case RDMA_TRANSPORT_IWARP:
		ret = cma_accept_iw(id_priv, conn_param);
		break;
	default:
		ret = -ENOSYS;
		break;
	}

	if (ret)
		goto reject;

	return 0;
reject:
	cma_modify_qp_err(id);
	rdma_reject(id, NULL, 0);
	return ret;
}
EXPORT_SYMBOL(rdma_accept);

int rdma_notify(struct rdma_cm_id *id, enum ib_event_type event)
{
	struct rdma_id_private *id_priv;
	int ret;

	id_priv = container_of(id, struct rdma_id_private, id);
	if (!cma_comp(id_priv, CMA_CONNECT))
		return -EINVAL;

	switch (id->device->node_type) {
	case RDMA_NODE_IB_CA:
		ret = ib_cm_notify(id_priv->cm_id.ib, event);
		break;
	default:
		ret = 0;
		break;
	}
	return ret;
}
EXPORT_SYMBOL(rdma_notify);

int rdma_reject(struct rdma_cm_id *id, const void *private_data,
		u8 private_data_len)
{
	struct rdma_id_private *id_priv;
	int ret;

	id_priv = container_of(id, struct rdma_id_private, id);
	if (!cma_comp(id_priv, CMA_CONNECT))
		return -EINVAL;

	switch (rdma_node_get_transport(id->device->node_type)) {
	case RDMA_TRANSPORT_IB:
		if (cma_is_ud_ps(id->ps))
			ret = cma_send_sidr_rep(id_priv, IB_SIDR_REJECT,
						private_data, private_data_len);
		else
			ret = ib_send_cm_rej(id_priv->cm_id.ib,
					     IB_CM_REJ_CONSUMER_DEFINED, NULL,
					     0, private_data, private_data_len);
		break;
	case RDMA_TRANSPORT_IWARP:
		ret = iw_cm_reject(id_priv->cm_id.iw,
				   private_data, private_data_len);
		break;
	default:
		ret = -ENOSYS;
		break;
	}
	return ret;
}
EXPORT_SYMBOL(rdma_reject);

int rdma_disconnect(struct rdma_cm_id *id)
{
	struct rdma_id_private *id_priv;
	int ret;

	id_priv = container_of(id, struct rdma_id_private, id);
	if (!cma_comp(id_priv, CMA_CONNECT) &&
	    !cma_comp(id_priv, CMA_DISCONNECT))
		return -EINVAL;

	switch (rdma_node_get_transport(id->device->node_type)) {
	case RDMA_TRANSPORT_IB:
		ret = cma_modify_qp_err(id);
		if (ret)
			goto out;
		/* Initiate or respond to a disconnect. */
		if (ib_send_cm_dreq(id_priv->cm_id.ib, NULL, 0))
			ib_send_cm_drep(id_priv->cm_id.ib, NULL, 0);
		break;
	case RDMA_TRANSPORT_IWARP:
		ret = iw_cm_disconnect(id_priv->cm_id.iw, 0);
		break;
	default:
		ret = -EINVAL;
		break;
	}
out:
	return ret;
}
EXPORT_SYMBOL(rdma_disconnect);

static int cma_ib_mc_handler(int status, struct ib_sa_multicast *multicast)
{
	struct rdma_id_private *id_priv;
	struct cma_multicast *mc = multicast->context;
	struct rdma_cm_event event;
	int ret;

	id_priv = mc->id_priv;
	atomic_inc(&id_priv->dev_remove);
	if (!cma_comp(id_priv, CMA_ADDR_BOUND) &&
	    !cma_comp(id_priv, CMA_ADDR_RESOLVED))
		goto out;

	if (!status && id_priv->id.qp)
		status = ib_attach_mcast(id_priv->id.qp, &multicast->rec.mgid,
					 multicast->rec.mlid);

	memset(&event, 0, sizeof event);
	event.status = status;
	event.param.ud.private_data = mc->context;
	if (!status) {
		event.event = RDMA_CM_EVENT_MULTICAST_JOIN;
		ib_init_ah_from_mcmember(id_priv->id.device,
					 id_priv->id.port_num, &multicast->rec,
					 &event.param.ud.ah_attr);
		event.param.ud.qp_num = 0xFFFFFF;
		event.param.ud.qkey = be32_to_cpu(multicast->rec.qkey);
	} else
		event.event = RDMA_CM_EVENT_MULTICAST_ERROR;

	ret = id_priv->id.event_handler(&id_priv->id, &event);
	if (ret) {
		cma_exch(id_priv, CMA_DESTROYING);
		cma_release_remove(id_priv);
		rdma_destroy_id(&id_priv->id);
		return 0;
	}
out:
	cma_release_remove(id_priv);
	return 0;
}

static void cma_set_mgid(struct rdma_id_private *id_priv,
			 struct sockaddr *addr, union ib_gid *mgid)
{
	unsigned char mc_map[MAX_ADDR_LEN];
	struct rdma_dev_addr *dev_addr = &id_priv->id.route.addr.dev_addr;
	struct sockaddr_in *sin = (struct sockaddr_in *) addr;
	struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *) addr;

	if (cma_any_addr(addr)) {
		memset(mgid, 0, sizeof *mgid);
	} else if ((addr->sa_family == AF_INET6) &&
		   ((be32_to_cpu(sin6->sin6_addr.s6_addr32[0]) & 0xFF10A01B) ==
								 0xFF10A01B)) {
		/* IPv6 address is an SA assigned MGID. */
		memcpy(mgid, &sin6->sin6_addr, sizeof *mgid);
	} else {
		ip_ib_mc_map(sin->sin_addr.s_addr, mc_map);
		if (id_priv->id.ps == RDMA_PS_UDP)
			mc_map[7] = 0x01;	/* Use RDMA CM signature */
		mc_map[8] = ib_addr_get_pkey(dev_addr) >> 8;
		mc_map[9] = (unsigned char) ib_addr_get_pkey(dev_addr);
		*mgid = *(union ib_gid *) (mc_map + 4);
	}
}

static int cma_join_ib_multicast(struct rdma_id_private *id_priv,
				 struct cma_multicast *mc)
{
	struct ib_sa_mcmember_rec rec;
	struct rdma_dev_addr *dev_addr = &id_priv->id.route.addr.dev_addr;
	ib_sa_comp_mask comp_mask;
	int ret;

	ib_addr_get_mgid(dev_addr, &rec.mgid);
	ret = ib_sa_get_mcmember_rec(id_priv->id.device, id_priv->id.port_num,
				     &rec.mgid, &rec);
	if (ret)
		return ret;

	cma_set_mgid(id_priv, &mc->addr, &rec.mgid);
	if (id_priv->id.ps == RDMA_PS_UDP)
		rec.qkey = cpu_to_be32(RDMA_UDP_QKEY);
	ib_addr_get_sgid(dev_addr, &rec.port_gid);
	rec.pkey = cpu_to_be16(ib_addr_get_pkey(dev_addr));
	rec.join_state = 1;

	comp_mask = IB_SA_MCMEMBER_REC_MGID | IB_SA_MCMEMBER_REC_PORT_GID |
		    IB_SA_MCMEMBER_REC_PKEY | IB_SA_MCMEMBER_REC_JOIN_STATE |
		    IB_SA_MCMEMBER_REC_QKEY | IB_SA_MCMEMBER_REC_SL |
		    IB_SA_MCMEMBER_REC_FLOW_LABEL |
		    IB_SA_MCMEMBER_REC_TRAFFIC_CLASS;

	mc->multicast.ib = ib_sa_join_multicast(&sa_client, id_priv->id.device,
						id_priv->id.port_num, &rec,
						comp_mask, GFP_KERNEL,
						cma_ib_mc_handler, mc);
	if (IS_ERR(mc->multicast.ib))
		return PTR_ERR(mc->multicast.ib);

	return 0;
}

int rdma_join_multicast(struct rdma_cm_id *id, struct sockaddr *addr,
			void *context)
{
	struct rdma_id_private *id_priv;
	struct cma_multicast *mc;
	int ret;

	id_priv = container_of(id, struct rdma_id_private, id);
	if (!cma_comp(id_priv, CMA_ADDR_BOUND) &&
	    !cma_comp(id_priv, CMA_ADDR_RESOLVED))
		return -EINVAL;

	mc = kmalloc(sizeof *mc, GFP_KERNEL);
	if (!mc)
		return -ENOMEM;

	memcpy(&mc->addr, addr, ip_addr_size(addr));
	mc->context = context;
	mc->id_priv = id_priv;

	spin_lock(&id_priv->lock);
	list_add(&mc->list, &id_priv->mc_list);
	spin_unlock(&id_priv->lock);

	switch (rdma_node_get_transport(id->device->node_type)) {
	case RDMA_TRANSPORT_IB:
		ret = cma_join_ib_multicast(id_priv, mc);
		break;
	default:
		ret = -ENOSYS;
		break;
	}

	if (ret) {
		spin_lock_irq(&id_priv->lock);
		list_del(&mc->list);
		spin_unlock_irq(&id_priv->lock);
		kfree(mc);
	}
	return ret;
}
EXPORT_SYMBOL(rdma_join_multicast);

void rdma_leave_multicast(struct rdma_cm_id *id, struct sockaddr *addr)
{
	struct rdma_id_private *id_priv;
	struct cma_multicast *mc;

	id_priv = container_of(id, struct rdma_id_private, id);
	spin_lock_irq(&id_priv->lock);
	list_for_each_entry(mc, &id_priv->mc_list, list) {
		if (!memcmp(&mc->addr, addr, ip_addr_size(addr))) {
			list_del(&mc->list);
			spin_unlock_irq(&id_priv->lock);

			if (id->qp)
				ib_detach_mcast(id->qp,
						&mc->multicast.ib->rec.mgid,
						mc->multicast.ib->rec.mlid);
			ib_sa_free_multicast(mc->multicast.ib);
			kfree(mc);
			return;
		}
	}
	spin_unlock_irq(&id_priv->lock);
}
EXPORT_SYMBOL(rdma_leave_multicast);

static void cma_add_one(struct ib_device *device)
{
	struct cma_device *cma_dev;
	struct rdma_id_private *id_priv;

	cma_dev = kmalloc(sizeof *cma_dev, GFP_KERNEL);
	if (!cma_dev)
		return;

	cma_dev->device = device;

	init_completion(&cma_dev->comp);
	atomic_set(&cma_dev->refcount, 1);
	INIT_LIST_HEAD(&cma_dev->id_list);
	ib_set_client_data(device, &cma_client, cma_dev);

	mutex_lock(&lock);
	list_add_tail(&cma_dev->list, &dev_list);
	list_for_each_entry(id_priv, &listen_any_list, list)
		cma_listen_on_dev(id_priv, cma_dev);
	mutex_unlock(&lock);
}

static int cma_remove_id_dev(struct rdma_id_private *id_priv)
{
	struct rdma_cm_event event;
	enum cma_state state;

	/* Record that we want to remove the device */
	state = cma_exch(id_priv, CMA_DEVICE_REMOVAL);
	if (state == CMA_DESTROYING)
		return 0;

	cma_cancel_operation(id_priv, state);
	wait_event(id_priv->wait_remove, !atomic_read(&id_priv->dev_remove));

	/* Check for destruction from another callback. */
	if (!cma_comp(id_priv, CMA_DEVICE_REMOVAL))
		return 0;

	memset(&event, 0, sizeof event);
	event.event = RDMA_CM_EVENT_DEVICE_REMOVAL;
	return id_priv->id.event_handler(&id_priv->id, &event);
}

static void cma_process_remove(struct cma_device *cma_dev)
{
	struct rdma_id_private *id_priv;
	int ret;

	mutex_lock(&lock);
	while (!list_empty(&cma_dev->id_list)) {
		id_priv = list_entry(cma_dev->id_list.next,
				     struct rdma_id_private, list);

		if (cma_internal_listen(id_priv)) {
			cma_destroy_listen(id_priv);
			continue;
		}

		list_del_init(&id_priv->list);
		atomic_inc(&id_priv->refcount);
		mutex_unlock(&lock);

		ret = cma_remove_id_dev(id_priv);
		cma_deref_id(id_priv);
		if (ret)
			rdma_destroy_id(&id_priv->id);

		mutex_lock(&lock);
	}
	mutex_unlock(&lock);

	cma_deref_dev(cma_dev);
	wait_for_completion(&cma_dev->comp);
}

static void cma_remove_one(struct ib_device *device)
{
	struct cma_device *cma_dev;

	cma_dev = ib_get_client_data(device, &cma_client);
	if (!cma_dev)
		return;

	mutex_lock(&lock);
	list_del(&cma_dev->list);
	mutex_unlock(&lock);

	cma_process_remove(cma_dev);
	kfree(cma_dev);
}

static int cma_init(void)
{
	int ret;

	get_random_bytes(&next_port, sizeof next_port);
	next_port = (next_port % (sysctl_local_port_range[1] -
				  sysctl_local_port_range[0])) +
		    sysctl_local_port_range[0];
	cma_wq = create_singlethread_workqueue("rdma_cm");
	if (!cma_wq)
		return -ENOMEM;

	ib_sa_register_client(&sa_client);
	rdma_addr_register_client(&addr_client);

	ret = ib_register_client(&cma_client);
	if (ret)
		goto err;
	return 0;

err:
	rdma_addr_unregister_client(&addr_client);
	ib_sa_unregister_client(&sa_client);
	destroy_workqueue(cma_wq);
	return ret;
}

static void cma_cleanup(void)
{
	ib_unregister_client(&cma_client);
	rdma_addr_unregister_client(&addr_client);
	ib_sa_unregister_client(&sa_client);
	destroy_workqueue(cma_wq);
	idr_destroy(&sdp_ps);
	idr_destroy(&tcp_ps);
	idr_destroy(&udp_ps);
	idr_destroy(&ipoib_ps);
}

module_init(cma_init);
module_exit(cma_cleanup);
