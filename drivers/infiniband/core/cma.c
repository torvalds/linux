// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright (c) 2005 Voltaire Inc.  All rights reserved.
 * Copyright (c) 2002-2005, Network Appliance, Inc. All rights reserved.
 * Copyright (c) 1999-2019, Mellanox Technologies, Inc. All rights reserved.
 * Copyright (c) 2005-2006 Intel Corporation.  All rights reserved.
 */

#include <linux/completion.h>
#include <linux/in.h>
#include <linux/in6.h>
#include <linux/mutex.h>
#include <linux/random.h>
#include <linux/igmp.h>
#include <linux/xarray.h>
#include <linux/inetdevice.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <net/route.h>

#include <net/net_namespace.h>
#include <net/netns/generic.h>
#include <net/tcp.h>
#include <net/ipv6.h>
#include <net/ip_fib.h>
#include <net/ip6_route.h>

#include <rdma/rdma_cm.h>
#include <rdma/rdma_cm_ib.h>
#include <rdma/rdma_netlink.h>
#include <rdma/ib.h>
#include <rdma/ib_cache.h>
#include <rdma/ib_cm.h>
#include <rdma/ib_sa.h>
#include <rdma/iw_cm.h>

#include "core_priv.h"
#include "cma_priv.h"
#include "cma_trace.h"

MODULE_AUTHOR("Sean Hefty");
MODULE_DESCRIPTION("Generic RDMA CM Agent");
MODULE_LICENSE("Dual BSD/GPL");

#define CMA_CM_RESPONSE_TIMEOUT 20
#define CMA_MAX_CM_RETRIES 15
#define CMA_CM_MRA_SETTING (IB_CM_MRA_FLAG_DELAY | 24)
#define CMA_IBOE_PACKET_LIFETIME 18
#define CMA_PREFERRED_ROCE_GID_TYPE IB_GID_TYPE_ROCE_UDP_ENCAP

static const char * const cma_events[] = {
	[RDMA_CM_EVENT_ADDR_RESOLVED]	 = "address resolved",
	[RDMA_CM_EVENT_ADDR_ERROR]	 = "address error",
	[RDMA_CM_EVENT_ROUTE_RESOLVED]	 = "route resolved ",
	[RDMA_CM_EVENT_ROUTE_ERROR]	 = "route error",
	[RDMA_CM_EVENT_CONNECT_REQUEST]	 = "connect request",
	[RDMA_CM_EVENT_CONNECT_RESPONSE] = "connect response",
	[RDMA_CM_EVENT_CONNECT_ERROR]	 = "connect error",
	[RDMA_CM_EVENT_UNREACHABLE]	 = "unreachable",
	[RDMA_CM_EVENT_REJECTED]	 = "rejected",
	[RDMA_CM_EVENT_ESTABLISHED]	 = "established",
	[RDMA_CM_EVENT_DISCONNECTED]	 = "disconnected",
	[RDMA_CM_EVENT_DEVICE_REMOVAL]	 = "device removal",
	[RDMA_CM_EVENT_MULTICAST_JOIN]	 = "multicast join",
	[RDMA_CM_EVENT_MULTICAST_ERROR]	 = "multicast error",
	[RDMA_CM_EVENT_ADDR_CHANGE]	 = "address change",
	[RDMA_CM_EVENT_TIMEWAIT_EXIT]	 = "timewait exit",
};

static void cma_set_mgid(struct rdma_id_private *id_priv, struct sockaddr *addr,
			 union ib_gid *mgid);

const char *__attribute_const__ rdma_event_msg(enum rdma_cm_event_type event)
{
	size_t index = event;

	return (index < ARRAY_SIZE(cma_events) && cma_events[index]) ?
			cma_events[index] : "unrecognized event";
}
EXPORT_SYMBOL(rdma_event_msg);

const char *__attribute_const__ rdma_reject_msg(struct rdma_cm_id *id,
						int reason)
{
	if (rdma_ib_or_roce(id->device, id->port_num))
		return ibcm_reject_msg(reason);

	if (rdma_protocol_iwarp(id->device, id->port_num))
		return iwcm_reject_msg(reason);

	WARN_ON_ONCE(1);
	return "unrecognized transport";
}
EXPORT_SYMBOL(rdma_reject_msg);

/**
 * rdma_is_consumer_reject - return true if the consumer rejected the connect
 *                           request.
 * @id: Communication identifier that received the REJECT event.
 * @reason: Value returned in the REJECT event status field.
 */
static bool rdma_is_consumer_reject(struct rdma_cm_id *id, int reason)
{
	if (rdma_ib_or_roce(id->device, id->port_num))
		return reason == IB_CM_REJ_CONSUMER_DEFINED;

	if (rdma_protocol_iwarp(id->device, id->port_num))
		return reason == -ECONNREFUSED;

	WARN_ON_ONCE(1);
	return false;
}

const void *rdma_consumer_reject_data(struct rdma_cm_id *id,
				      struct rdma_cm_event *ev, u8 *data_len)
{
	const void *p;

	if (rdma_is_consumer_reject(id, ev->status)) {
		*data_len = ev->param.conn.private_data_len;
		p = ev->param.conn.private_data;
	} else {
		*data_len = 0;
		p = NULL;
	}
	return p;
}
EXPORT_SYMBOL(rdma_consumer_reject_data);

/**
 * rdma_iw_cm_id() - return the iw_cm_id pointer for this cm_id.
 * @id: Communication Identifier
 */
struct iw_cm_id *rdma_iw_cm_id(struct rdma_cm_id *id)
{
	struct rdma_id_private *id_priv;

	id_priv = container_of(id, struct rdma_id_private, id);
	if (id->device->node_type == RDMA_NODE_RNIC)
		return id_priv->cm_id.iw;
	return NULL;
}
EXPORT_SYMBOL(rdma_iw_cm_id);

/**
 * rdma_res_to_id() - return the rdma_cm_id pointer for this restrack.
 * @res: rdma resource tracking entry pointer
 */
struct rdma_cm_id *rdma_res_to_id(struct rdma_restrack_entry *res)
{
	struct rdma_id_private *id_priv =
		container_of(res, struct rdma_id_private, res);

	return &id_priv->id;
}
EXPORT_SYMBOL(rdma_res_to_id);

static int cma_add_one(struct ib_device *device);
static void cma_remove_one(struct ib_device *device, void *client_data);

static struct ib_client cma_client = {
	.name   = "cma",
	.add    = cma_add_one,
	.remove = cma_remove_one
};

static struct ib_sa_client sa_client;
static LIST_HEAD(dev_list);
static LIST_HEAD(listen_any_list);
static DEFINE_MUTEX(lock);
static struct workqueue_struct *cma_wq;
static unsigned int cma_pernet_id;

struct cma_pernet {
	struct xarray tcp_ps;
	struct xarray udp_ps;
	struct xarray ipoib_ps;
	struct xarray ib_ps;
};

static struct cma_pernet *cma_pernet(struct net *net)
{
	return net_generic(net, cma_pernet_id);
}

static
struct xarray *cma_pernet_xa(struct net *net, enum rdma_ucm_port_space ps)
{
	struct cma_pernet *pernet = cma_pernet(net);

	switch (ps) {
	case RDMA_PS_TCP:
		return &pernet->tcp_ps;
	case RDMA_PS_UDP:
		return &pernet->udp_ps;
	case RDMA_PS_IPOIB:
		return &pernet->ipoib_ps;
	case RDMA_PS_IB:
		return &pernet->ib_ps;
	default:
		return NULL;
	}
}

struct cma_device {
	struct list_head	list;
	struct ib_device	*device;
	struct completion	comp;
	refcount_t refcount;
	struct list_head	id_list;
	enum ib_gid_type	*default_gid_type;
	u8			*default_roce_tos;
};

struct rdma_bind_list {
	enum rdma_ucm_port_space ps;
	struct hlist_head	owners;
	unsigned short		port;
};

static int cma_ps_alloc(struct net *net, enum rdma_ucm_port_space ps,
			struct rdma_bind_list *bind_list, int snum)
{
	struct xarray *xa = cma_pernet_xa(net, ps);

	return xa_insert(xa, snum, bind_list, GFP_KERNEL);
}

static struct rdma_bind_list *cma_ps_find(struct net *net,
					  enum rdma_ucm_port_space ps, int snum)
{
	struct xarray *xa = cma_pernet_xa(net, ps);

	return xa_load(xa, snum);
}

static void cma_ps_remove(struct net *net, enum rdma_ucm_port_space ps,
			  int snum)
{
	struct xarray *xa = cma_pernet_xa(net, ps);

	xa_erase(xa, snum);
}

enum {
	CMA_OPTION_AFONLY,
};

void cma_dev_get(struct cma_device *cma_dev)
{
	refcount_inc(&cma_dev->refcount);
}

void cma_dev_put(struct cma_device *cma_dev)
{
	if (refcount_dec_and_test(&cma_dev->refcount))
		complete(&cma_dev->comp);
}

struct cma_device *cma_enum_devices_by_ibdev(cma_device_filter	filter,
					     void		*cookie)
{
	struct cma_device *cma_dev;
	struct cma_device *found_cma_dev = NULL;

	mutex_lock(&lock);

	list_for_each_entry(cma_dev, &dev_list, list)
		if (filter(cma_dev->device, cookie)) {
			found_cma_dev = cma_dev;
			break;
		}

	if (found_cma_dev)
		cma_dev_get(found_cma_dev);
	mutex_unlock(&lock);
	return found_cma_dev;
}

int cma_get_default_gid_type(struct cma_device *cma_dev,
			     u32 port)
{
	if (!rdma_is_port_valid(cma_dev->device, port))
		return -EINVAL;

	return cma_dev->default_gid_type[port - rdma_start_port(cma_dev->device)];
}

int cma_set_default_gid_type(struct cma_device *cma_dev,
			     u32 port,
			     enum ib_gid_type default_gid_type)
{
	unsigned long supported_gids;

	if (!rdma_is_port_valid(cma_dev->device, port))
		return -EINVAL;

	if (default_gid_type == IB_GID_TYPE_IB &&
	    rdma_protocol_roce_eth_encap(cma_dev->device, port))
		default_gid_type = IB_GID_TYPE_ROCE;

	supported_gids = roce_gid_type_mask_support(cma_dev->device, port);

	if (!(supported_gids & 1 << default_gid_type))
		return -EINVAL;

	cma_dev->default_gid_type[port - rdma_start_port(cma_dev->device)] =
		default_gid_type;

	return 0;
}

int cma_get_default_roce_tos(struct cma_device *cma_dev, u32 port)
{
	if (!rdma_is_port_valid(cma_dev->device, port))
		return -EINVAL;

	return cma_dev->default_roce_tos[port - rdma_start_port(cma_dev->device)];
}

int cma_set_default_roce_tos(struct cma_device *cma_dev, u32 port,
			     u8 default_roce_tos)
{
	if (!rdma_is_port_valid(cma_dev->device, port))
		return -EINVAL;

	cma_dev->default_roce_tos[port - rdma_start_port(cma_dev->device)] =
		 default_roce_tos;

	return 0;
}
struct ib_device *cma_get_ib_dev(struct cma_device *cma_dev)
{
	return cma_dev->device;
}

/*
 * Device removal can occur at anytime, so we need extra handling to
 * serialize notifying the user of device removal with other callbacks.
 * We do this by disabling removal notification while a callback is in process,
 * and reporting it after the callback completes.
 */

struct cma_multicast {
	struct rdma_id_private *id_priv;
	union {
		struct ib_sa_multicast *sa_mc;
		struct {
			struct work_struct work;
			struct rdma_cm_event event;
		} iboe_join;
	};
	struct list_head	list;
	void			*context;
	struct sockaddr_storage	addr;
	u8			join_state;
};

struct cma_work {
	struct work_struct	work;
	struct rdma_id_private	*id;
	enum rdma_cm_state	old_state;
	enum rdma_cm_state	new_state;
	struct rdma_cm_event	event;
};

union cma_ip_addr {
	struct in6_addr ip6;
	struct {
		__be32 pad[3];
		__be32 addr;
	} ip4;
};

struct cma_hdr {
	u8 cma_version;
	u8 ip_version;	/* IP version: 7:4 */
	__be16 port;
	union cma_ip_addr src_addr;
	union cma_ip_addr dst_addr;
};

#define CMA_VERSION 0x00

struct cma_req_info {
	struct sockaddr_storage listen_addr_storage;
	struct sockaddr_storage src_addr_storage;
	struct ib_device *device;
	union ib_gid local_gid;
	__be64 service_id;
	int port;
	bool has_gid;
	u16 pkey;
};

static int cma_comp_exch(struct rdma_id_private *id_priv,
			 enum rdma_cm_state comp, enum rdma_cm_state exch)
{
	unsigned long flags;
	int ret;

	/*
	 * The FSM uses a funny double locking where state is protected by both
	 * the handler_mutex and the spinlock. State is not allowed to change
	 * to/from a handler_mutex protected value without also holding
	 * handler_mutex.
	 */
	if (comp == RDMA_CM_CONNECT || exch == RDMA_CM_CONNECT)
		lockdep_assert_held(&id_priv->handler_mutex);

	spin_lock_irqsave(&id_priv->lock, flags);
	if ((ret = (id_priv->state == comp)))
		id_priv->state = exch;
	spin_unlock_irqrestore(&id_priv->lock, flags);
	return ret;
}

static inline u8 cma_get_ip_ver(const struct cma_hdr *hdr)
{
	return hdr->ip_version >> 4;
}

static inline void cma_set_ip_ver(struct cma_hdr *hdr, u8 ip_ver)
{
	hdr->ip_version = (ip_ver << 4) | (hdr->ip_version & 0xF);
}

static int cma_igmp_send(struct net_device *ndev, union ib_gid *mgid, bool join)
{
	struct in_device *in_dev = NULL;

	if (ndev) {
		rtnl_lock();
		in_dev = __in_dev_get_rtnl(ndev);
		if (in_dev) {
			if (join)
				ip_mc_inc_group(in_dev,
						*(__be32 *)(mgid->raw + 12));
			else
				ip_mc_dec_group(in_dev,
						*(__be32 *)(mgid->raw + 12));
		}
		rtnl_unlock();
	}
	return (in_dev) ? 0 : -ENODEV;
}

static void _cma_attach_to_dev(struct rdma_id_private *id_priv,
			       struct cma_device *cma_dev)
{
	cma_dev_get(cma_dev);
	id_priv->cma_dev = cma_dev;
	id_priv->id.device = cma_dev->device;
	id_priv->id.route.addr.dev_addr.transport =
		rdma_node_get_transport(cma_dev->device->node_type);
	list_add_tail(&id_priv->list, &cma_dev->id_list);

	trace_cm_id_attach(id_priv, cma_dev->device);
}

static void cma_attach_to_dev(struct rdma_id_private *id_priv,
			      struct cma_device *cma_dev)
{
	_cma_attach_to_dev(id_priv, cma_dev);
	id_priv->gid_type =
		cma_dev->default_gid_type[id_priv->id.port_num -
					  rdma_start_port(cma_dev->device)];
}

static void cma_release_dev(struct rdma_id_private *id_priv)
{
	mutex_lock(&lock);
	list_del(&id_priv->list);
	cma_dev_put(id_priv->cma_dev);
	id_priv->cma_dev = NULL;
	if (id_priv->id.route.addr.dev_addr.sgid_attr) {
		rdma_put_gid_attr(id_priv->id.route.addr.dev_addr.sgid_attr);
		id_priv->id.route.addr.dev_addr.sgid_attr = NULL;
	}
	mutex_unlock(&lock);
}

static inline struct sockaddr *cma_src_addr(struct rdma_id_private *id_priv)
{
	return (struct sockaddr *) &id_priv->id.route.addr.src_addr;
}

static inline struct sockaddr *cma_dst_addr(struct rdma_id_private *id_priv)
{
	return (struct sockaddr *) &id_priv->id.route.addr.dst_addr;
}

static inline unsigned short cma_family(struct rdma_id_private *id_priv)
{
	return id_priv->id.route.addr.src_addr.ss_family;
}

static int cma_set_qkey(struct rdma_id_private *id_priv, u32 qkey)
{
	struct ib_sa_mcmember_rec rec;
	int ret = 0;

	if (id_priv->qkey) {
		if (qkey && id_priv->qkey != qkey)
			return -EINVAL;
		return 0;
	}

	if (qkey) {
		id_priv->qkey = qkey;
		return 0;
	}

	switch (id_priv->id.ps) {
	case RDMA_PS_UDP:
	case RDMA_PS_IB:
		id_priv->qkey = RDMA_UDP_QKEY;
		break;
	case RDMA_PS_IPOIB:
		ib_addr_get_mgid(&id_priv->id.route.addr.dev_addr, &rec.mgid);
		ret = ib_sa_get_mcmember_rec(id_priv->id.device,
					     id_priv->id.port_num, &rec.mgid,
					     &rec);
		if (!ret)
			id_priv->qkey = be32_to_cpu(rec.qkey);
		break;
	default:
		break;
	}
	return ret;
}

static void cma_translate_ib(struct sockaddr_ib *sib, struct rdma_dev_addr *dev_addr)
{
	dev_addr->dev_type = ARPHRD_INFINIBAND;
	rdma_addr_set_sgid(dev_addr, (union ib_gid *) &sib->sib_addr);
	ib_addr_set_pkey(dev_addr, ntohs(sib->sib_pkey));
}

static int cma_translate_addr(struct sockaddr *addr, struct rdma_dev_addr *dev_addr)
{
	int ret;

	if (addr->sa_family != AF_IB) {
		ret = rdma_translate_ip(addr, dev_addr);
	} else {
		cma_translate_ib((struct sockaddr_ib *) addr, dev_addr);
		ret = 0;
	}

	return ret;
}

static const struct ib_gid_attr *
cma_validate_port(struct ib_device *device, u32 port,
		  enum ib_gid_type gid_type,
		  union ib_gid *gid,
		  struct rdma_id_private *id_priv)
{
	struct rdma_dev_addr *dev_addr = &id_priv->id.route.addr.dev_addr;
	int bound_if_index = dev_addr->bound_dev_if;
	const struct ib_gid_attr *sgid_attr;
	int dev_type = dev_addr->dev_type;
	struct net_device *ndev = NULL;

	if (!rdma_dev_access_netns(device, id_priv->id.route.addr.dev_addr.net))
		return ERR_PTR(-ENODEV);

	if ((dev_type == ARPHRD_INFINIBAND) && !rdma_protocol_ib(device, port))
		return ERR_PTR(-ENODEV);

	if ((dev_type != ARPHRD_INFINIBAND) && rdma_protocol_ib(device, port))
		return ERR_PTR(-ENODEV);

	if (dev_type == ARPHRD_ETHER && rdma_protocol_roce(device, port)) {
		ndev = dev_get_by_index(dev_addr->net, bound_if_index);
		if (!ndev)
			return ERR_PTR(-ENODEV);
	} else {
		gid_type = IB_GID_TYPE_IB;
	}

	sgid_attr = rdma_find_gid_by_port(device, gid, gid_type, port, ndev);
	if (ndev)
		dev_put(ndev);
	return sgid_attr;
}

static void cma_bind_sgid_attr(struct rdma_id_private *id_priv,
			       const struct ib_gid_attr *sgid_attr)
{
	WARN_ON(id_priv->id.route.addr.dev_addr.sgid_attr);
	id_priv->id.route.addr.dev_addr.sgid_attr = sgid_attr;
}

/**
 * cma_acquire_dev_by_src_ip - Acquire cma device, port, gid attribute
 * based on source ip address.
 * @id_priv:	cm_id which should be bound to cma device
 *
 * cma_acquire_dev_by_src_ip() binds cm id to cma device, port and GID attribute
 * based on source IP address. It returns 0 on success or error code otherwise.
 * It is applicable to active and passive side cm_id.
 */
static int cma_acquire_dev_by_src_ip(struct rdma_id_private *id_priv)
{
	struct rdma_dev_addr *dev_addr = &id_priv->id.route.addr.dev_addr;
	const struct ib_gid_attr *sgid_attr;
	union ib_gid gid, iboe_gid, *gidp;
	struct cma_device *cma_dev;
	enum ib_gid_type gid_type;
	int ret = -ENODEV;
	u32 port;

	if (dev_addr->dev_type != ARPHRD_INFINIBAND &&
	    id_priv->id.ps == RDMA_PS_IPOIB)
		return -EINVAL;

	rdma_ip2gid((struct sockaddr *)&id_priv->id.route.addr.src_addr,
		    &iboe_gid);

	memcpy(&gid, dev_addr->src_dev_addr +
	       rdma_addr_gid_offset(dev_addr), sizeof(gid));

	mutex_lock(&lock);
	list_for_each_entry(cma_dev, &dev_list, list) {
		rdma_for_each_port (cma_dev->device, port) {
			gidp = rdma_protocol_roce(cma_dev->device, port) ?
			       &iboe_gid : &gid;
			gid_type = cma_dev->default_gid_type[port - 1];
			sgid_attr = cma_validate_port(cma_dev->device, port,
						      gid_type, gidp, id_priv);
			if (!IS_ERR(sgid_attr)) {
				id_priv->id.port_num = port;
				cma_bind_sgid_attr(id_priv, sgid_attr);
				cma_attach_to_dev(id_priv, cma_dev);
				ret = 0;
				goto out;
			}
		}
	}
out:
	mutex_unlock(&lock);
	return ret;
}

/**
 * cma_ib_acquire_dev - Acquire cma device, port and SGID attribute
 * @id_priv:		cm id to bind to cma device
 * @listen_id_priv:	listener cm id to match against
 * @req:		Pointer to req structure containaining incoming
 *			request information
 * cma_ib_acquire_dev() acquires cma device, port and SGID attribute when
 * rdma device matches for listen_id and incoming request. It also verifies
 * that a GID table entry is present for the source address.
 * Returns 0 on success, or returns error code otherwise.
 */
static int cma_ib_acquire_dev(struct rdma_id_private *id_priv,
			      const struct rdma_id_private *listen_id_priv,
			      struct cma_req_info *req)
{
	struct rdma_dev_addr *dev_addr = &id_priv->id.route.addr.dev_addr;
	const struct ib_gid_attr *sgid_attr;
	enum ib_gid_type gid_type;
	union ib_gid gid;

	if (dev_addr->dev_type != ARPHRD_INFINIBAND &&
	    id_priv->id.ps == RDMA_PS_IPOIB)
		return -EINVAL;

	if (rdma_protocol_roce(req->device, req->port))
		rdma_ip2gid((struct sockaddr *)&id_priv->id.route.addr.src_addr,
			    &gid);
	else
		memcpy(&gid, dev_addr->src_dev_addr +
		       rdma_addr_gid_offset(dev_addr), sizeof(gid));

	gid_type = listen_id_priv->cma_dev->default_gid_type[req->port - 1];
	sgid_attr = cma_validate_port(req->device, req->port,
				      gid_type, &gid, id_priv);
	if (IS_ERR(sgid_attr))
		return PTR_ERR(sgid_attr);

	id_priv->id.port_num = req->port;
	cma_bind_sgid_attr(id_priv, sgid_attr);
	/* Need to acquire lock to protect against reader
	 * of cma_dev->id_list such as cma_netdev_callback() and
	 * cma_process_remove().
	 */
	mutex_lock(&lock);
	cma_attach_to_dev(id_priv, listen_id_priv->cma_dev);
	mutex_unlock(&lock);
	rdma_restrack_add(&id_priv->res);
	return 0;
}

static int cma_iw_acquire_dev(struct rdma_id_private *id_priv,
			      const struct rdma_id_private *listen_id_priv)
{
	struct rdma_dev_addr *dev_addr = &id_priv->id.route.addr.dev_addr;
	const struct ib_gid_attr *sgid_attr;
	struct cma_device *cma_dev;
	enum ib_gid_type gid_type;
	int ret = -ENODEV;
	union ib_gid gid;
	u32 port;

	if (dev_addr->dev_type != ARPHRD_INFINIBAND &&
	    id_priv->id.ps == RDMA_PS_IPOIB)
		return -EINVAL;

	memcpy(&gid, dev_addr->src_dev_addr +
	       rdma_addr_gid_offset(dev_addr), sizeof(gid));

	mutex_lock(&lock);

	cma_dev = listen_id_priv->cma_dev;
	port = listen_id_priv->id.port_num;
	gid_type = listen_id_priv->gid_type;
	sgid_attr = cma_validate_port(cma_dev->device, port,
				      gid_type, &gid, id_priv);
	if (!IS_ERR(sgid_attr)) {
		id_priv->id.port_num = port;
		cma_bind_sgid_attr(id_priv, sgid_attr);
		ret = 0;
		goto out;
	}

	list_for_each_entry(cma_dev, &dev_list, list) {
		rdma_for_each_port (cma_dev->device, port) {
			if (listen_id_priv->cma_dev == cma_dev &&
			    listen_id_priv->id.port_num == port)
				continue;

			gid_type = cma_dev->default_gid_type[port - 1];
			sgid_attr = cma_validate_port(cma_dev->device, port,
						      gid_type, &gid, id_priv);
			if (!IS_ERR(sgid_attr)) {
				id_priv->id.port_num = port;
				cma_bind_sgid_attr(id_priv, sgid_attr);
				ret = 0;
				goto out;
			}
		}
	}

out:
	if (!ret) {
		cma_attach_to_dev(id_priv, cma_dev);
		rdma_restrack_add(&id_priv->res);
	}

	mutex_unlock(&lock);
	return ret;
}

/*
 * Select the source IB device and address to reach the destination IB address.
 */
static int cma_resolve_ib_dev(struct rdma_id_private *id_priv)
{
	struct cma_device *cma_dev, *cur_dev;
	struct sockaddr_ib *addr;
	union ib_gid gid, sgid, *dgid;
	unsigned int p;
	u16 pkey, index;
	enum ib_port_state port_state;
	int i;

	cma_dev = NULL;
	addr = (struct sockaddr_ib *) cma_dst_addr(id_priv);
	dgid = (union ib_gid *) &addr->sib_addr;
	pkey = ntohs(addr->sib_pkey);

	mutex_lock(&lock);
	list_for_each_entry(cur_dev, &dev_list, list) {
		rdma_for_each_port (cur_dev->device, p) {
			if (!rdma_cap_af_ib(cur_dev->device, p))
				continue;

			if (ib_find_cached_pkey(cur_dev->device, p, pkey, &index))
				continue;

			if (ib_get_cached_port_state(cur_dev->device, p, &port_state))
				continue;
			for (i = 0; !rdma_query_gid(cur_dev->device,
						    p, i, &gid);
			     i++) {
				if (!memcmp(&gid, dgid, sizeof(gid))) {
					cma_dev = cur_dev;
					sgid = gid;
					id_priv->id.port_num = p;
					goto found;
				}

				if (!cma_dev && (gid.global.subnet_prefix ==
				    dgid->global.subnet_prefix) &&
				    port_state == IB_PORT_ACTIVE) {
					cma_dev = cur_dev;
					sgid = gid;
					id_priv->id.port_num = p;
					goto found;
				}
			}
		}
	}
	mutex_unlock(&lock);
	return -ENODEV;

found:
	cma_attach_to_dev(id_priv, cma_dev);
	rdma_restrack_add(&id_priv->res);
	mutex_unlock(&lock);
	addr = (struct sockaddr_ib *)cma_src_addr(id_priv);
	memcpy(&addr->sib_addr, &sgid, sizeof(sgid));
	cma_translate_ib(addr, &id_priv->id.route.addr.dev_addr);
	return 0;
}

static void cma_id_get(struct rdma_id_private *id_priv)
{
	refcount_inc(&id_priv->refcount);
}

static void cma_id_put(struct rdma_id_private *id_priv)
{
	if (refcount_dec_and_test(&id_priv->refcount))
		complete(&id_priv->comp);
}

static struct rdma_id_private *
__rdma_create_id(struct net *net, rdma_cm_event_handler event_handler,
		 void *context, enum rdma_ucm_port_space ps,
		 enum ib_qp_type qp_type, const struct rdma_id_private *parent)
{
	struct rdma_id_private *id_priv;

	id_priv = kzalloc(sizeof *id_priv, GFP_KERNEL);
	if (!id_priv)
		return ERR_PTR(-ENOMEM);

	id_priv->state = RDMA_CM_IDLE;
	id_priv->id.context = context;
	id_priv->id.event_handler = event_handler;
	id_priv->id.ps = ps;
	id_priv->id.qp_type = qp_type;
	id_priv->tos_set = false;
	id_priv->timeout_set = false;
	id_priv->min_rnr_timer_set = false;
	id_priv->gid_type = IB_GID_TYPE_IB;
	spin_lock_init(&id_priv->lock);
	mutex_init(&id_priv->qp_mutex);
	init_completion(&id_priv->comp);
	refcount_set(&id_priv->refcount, 1);
	mutex_init(&id_priv->handler_mutex);
	INIT_LIST_HEAD(&id_priv->listen_list);
	INIT_LIST_HEAD(&id_priv->mc_list);
	get_random_bytes(&id_priv->seq_num, sizeof id_priv->seq_num);
	id_priv->id.route.addr.dev_addr.net = get_net(net);
	id_priv->seq_num &= 0x00ffffff;

	rdma_restrack_new(&id_priv->res, RDMA_RESTRACK_CM_ID);
	if (parent)
		rdma_restrack_parent_name(&id_priv->res, &parent->res);

	return id_priv;
}

struct rdma_cm_id *
__rdma_create_kernel_id(struct net *net, rdma_cm_event_handler event_handler,
			void *context, enum rdma_ucm_port_space ps,
			enum ib_qp_type qp_type, const char *caller)
{
	struct rdma_id_private *ret;

	ret = __rdma_create_id(net, event_handler, context, ps, qp_type, NULL);
	if (IS_ERR(ret))
		return ERR_CAST(ret);

	rdma_restrack_set_name(&ret->res, caller);
	return &ret->id;
}
EXPORT_SYMBOL(__rdma_create_kernel_id);

struct rdma_cm_id *rdma_create_user_id(rdma_cm_event_handler event_handler,
				       void *context,
				       enum rdma_ucm_port_space ps,
				       enum ib_qp_type qp_type)
{
	struct rdma_id_private *ret;

	ret = __rdma_create_id(current->nsproxy->net_ns, event_handler, context,
			       ps, qp_type, NULL);
	if (IS_ERR(ret))
		return ERR_CAST(ret);

	rdma_restrack_set_name(&ret->res, NULL);
	return &ret->id;
}
EXPORT_SYMBOL(rdma_create_user_id);

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
	if (id->device != pd->device) {
		ret = -EINVAL;
		goto out_err;
	}

	qp_init_attr->port_num = id->port_num;
	qp = ib_create_qp(pd, qp_init_attr);
	if (IS_ERR(qp)) {
		ret = PTR_ERR(qp);
		goto out_err;
	}

	if (id->qp_type == IB_QPT_UD)
		ret = cma_init_ud_qp(id_priv, qp);
	else
		ret = cma_init_conn_qp(id_priv, qp);
	if (ret)
		goto out_destroy;

	id->qp = qp;
	id_priv->qp_num = qp->qp_num;
	id_priv->srq = (qp->srq != NULL);
	trace_cm_qp_create(id_priv, pd, qp_init_attr, 0);
	return 0;
out_destroy:
	ib_destroy_qp(qp);
out_err:
	trace_cm_qp_create(id_priv, pd, qp_init_attr, ret);
	return ret;
}
EXPORT_SYMBOL(rdma_create_qp);

void rdma_destroy_qp(struct rdma_cm_id *id)
{
	struct rdma_id_private *id_priv;

	id_priv = container_of(id, struct rdma_id_private, id);
	trace_cm_qp_destroy(id_priv);
	mutex_lock(&id_priv->qp_mutex);
	ib_destroy_qp(id_priv->id.qp);
	id_priv->id.qp = NULL;
	mutex_unlock(&id_priv->qp_mutex);
}
EXPORT_SYMBOL(rdma_destroy_qp);

static int cma_modify_qp_rtr(struct rdma_id_private *id_priv,
			     struct rdma_conn_param *conn_param)
{
	struct ib_qp_attr qp_attr;
	int qp_attr_mask, ret;

	mutex_lock(&id_priv->qp_mutex);
	if (!id_priv->id.qp) {
		ret = 0;
		goto out;
	}

	/* Need to update QP attributes from default values. */
	qp_attr.qp_state = IB_QPS_INIT;
	ret = rdma_init_qp_attr(&id_priv->id, &qp_attr, &qp_attr_mask);
	if (ret)
		goto out;

	ret = ib_modify_qp(id_priv->id.qp, &qp_attr, qp_attr_mask);
	if (ret)
		goto out;

	qp_attr.qp_state = IB_QPS_RTR;
	ret = rdma_init_qp_attr(&id_priv->id, &qp_attr, &qp_attr_mask);
	if (ret)
		goto out;

	BUG_ON(id_priv->cma_dev->device != id_priv->id.device);

	if (conn_param)
		qp_attr.max_dest_rd_atomic = conn_param->responder_resources;
	ret = ib_modify_qp(id_priv->id.qp, &qp_attr, qp_attr_mask);
out:
	mutex_unlock(&id_priv->qp_mutex);
	return ret;
}

static int cma_modify_qp_rts(struct rdma_id_private *id_priv,
			     struct rdma_conn_param *conn_param)
{
	struct ib_qp_attr qp_attr;
	int qp_attr_mask, ret;

	mutex_lock(&id_priv->qp_mutex);
	if (!id_priv->id.qp) {
		ret = 0;
		goto out;
	}

	qp_attr.qp_state = IB_QPS_RTS;
	ret = rdma_init_qp_attr(&id_priv->id, &qp_attr, &qp_attr_mask);
	if (ret)
		goto out;

	if (conn_param)
		qp_attr.max_rd_atomic = conn_param->initiator_depth;
	ret = ib_modify_qp(id_priv->id.qp, &qp_attr, qp_attr_mask);
out:
	mutex_unlock(&id_priv->qp_mutex);
	return ret;
}

static int cma_modify_qp_err(struct rdma_id_private *id_priv)
{
	struct ib_qp_attr qp_attr;
	int ret;

	mutex_lock(&id_priv->qp_mutex);
	if (!id_priv->id.qp) {
		ret = 0;
		goto out;
	}

	qp_attr.qp_state = IB_QPS_ERR;
	ret = ib_modify_qp(id_priv->id.qp, &qp_attr, IB_QP_STATE);
out:
	mutex_unlock(&id_priv->qp_mutex);
	return ret;
}

static int cma_ib_init_qp_attr(struct rdma_id_private *id_priv,
			       struct ib_qp_attr *qp_attr, int *qp_attr_mask)
{
	struct rdma_dev_addr *dev_addr = &id_priv->id.route.addr.dev_addr;
	int ret;
	u16 pkey;

	if (rdma_cap_eth_ah(id_priv->id.device, id_priv->id.port_num))
		pkey = 0xffff;
	else
		pkey = ib_addr_get_pkey(dev_addr);

	ret = ib_find_cached_pkey(id_priv->id.device, id_priv->id.port_num,
				  pkey, &qp_attr->pkey_index);
	if (ret)
		return ret;

	qp_attr->port_num = id_priv->id.port_num;
	*qp_attr_mask = IB_QP_STATE | IB_QP_PKEY_INDEX | IB_QP_PORT;

	if (id_priv->id.qp_type == IB_QPT_UD) {
		ret = cma_set_qkey(id_priv, 0);
		if (ret)
			return ret;

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
	if (rdma_cap_ib_cm(id->device, id->port_num)) {
		if (!id_priv->cm_id.ib || (id_priv->id.qp_type == IB_QPT_UD))
			ret = cma_ib_init_qp_attr(id_priv, qp_attr, qp_attr_mask);
		else
			ret = ib_cm_init_qp_attr(id_priv->cm_id.ib, qp_attr,
						 qp_attr_mask);

		if (qp_attr->qp_state == IB_QPS_RTR)
			qp_attr->rq_psn = id_priv->seq_num;
	} else if (rdma_cap_iw_cm(id->device, id->port_num)) {
		if (!id_priv->cm_id.iw) {
			qp_attr->qp_access_flags = 0;
			*qp_attr_mask = IB_QP_STATE | IB_QP_ACCESS_FLAGS;
		} else
			ret = iw_cm_init_qp_attr(id_priv->cm_id.iw, qp_attr,
						 qp_attr_mask);
		qp_attr->port_num = id_priv->id.port_num;
		*qp_attr_mask |= IB_QP_PORT;
	} else {
		ret = -ENOSYS;
	}

	if ((*qp_attr_mask & IB_QP_TIMEOUT) && id_priv->timeout_set)
		qp_attr->timeout = id_priv->timeout;

	if ((*qp_attr_mask & IB_QP_MIN_RNR_TIMER) && id_priv->min_rnr_timer_set)
		qp_attr->min_rnr_timer = id_priv->min_rnr_timer;

	return ret;
}
EXPORT_SYMBOL(rdma_init_qp_attr);

static inline bool cma_zero_addr(const struct sockaddr *addr)
{
	switch (addr->sa_family) {
	case AF_INET:
		return ipv4_is_zeronet(((struct sockaddr_in *)addr)->sin_addr.s_addr);
	case AF_INET6:
		return ipv6_addr_any(&((struct sockaddr_in6 *)addr)->sin6_addr);
	case AF_IB:
		return ib_addr_any(&((struct sockaddr_ib *)addr)->sib_addr);
	default:
		return false;
	}
}

static inline bool cma_loopback_addr(const struct sockaddr *addr)
{
	switch (addr->sa_family) {
	case AF_INET:
		return ipv4_is_loopback(
			((struct sockaddr_in *)addr)->sin_addr.s_addr);
	case AF_INET6:
		return ipv6_addr_loopback(
			&((struct sockaddr_in6 *)addr)->sin6_addr);
	case AF_IB:
		return ib_addr_loopback(
			&((struct sockaddr_ib *)addr)->sib_addr);
	default:
		return false;
	}
}

static inline bool cma_any_addr(const struct sockaddr *addr)
{
	return cma_zero_addr(addr) || cma_loopback_addr(addr);
}

static int cma_addr_cmp(const struct sockaddr *src, const struct sockaddr *dst)
{
	if (src->sa_family != dst->sa_family)
		return -1;

	switch (src->sa_family) {
	case AF_INET:
		return ((struct sockaddr_in *)src)->sin_addr.s_addr !=
		       ((struct sockaddr_in *)dst)->sin_addr.s_addr;
	case AF_INET6: {
		struct sockaddr_in6 *src_addr6 = (struct sockaddr_in6 *)src;
		struct sockaddr_in6 *dst_addr6 = (struct sockaddr_in6 *)dst;
		bool link_local;

		if (ipv6_addr_cmp(&src_addr6->sin6_addr,
					  &dst_addr6->sin6_addr))
			return 1;
		link_local = ipv6_addr_type(&dst_addr6->sin6_addr) &
			     IPV6_ADDR_LINKLOCAL;
		/* Link local must match their scope_ids */
		return link_local ? (src_addr6->sin6_scope_id !=
				     dst_addr6->sin6_scope_id) :
				    0;
	}

	default:
		return ib_addr_cmp(&((struct sockaddr_ib *) src)->sib_addr,
				   &((struct sockaddr_ib *) dst)->sib_addr);
	}
}

static __be16 cma_port(const struct sockaddr *addr)
{
	struct sockaddr_ib *sib;

	switch (addr->sa_family) {
	case AF_INET:
		return ((struct sockaddr_in *) addr)->sin_port;
	case AF_INET6:
		return ((struct sockaddr_in6 *) addr)->sin6_port;
	case AF_IB:
		sib = (struct sockaddr_ib *) addr;
		return htons((u16) (be64_to_cpu(sib->sib_sid) &
				    be64_to_cpu(sib->sib_sid_mask)));
	default:
		return 0;
	}
}

static inline int cma_any_port(const struct sockaddr *addr)
{
	return !cma_port(addr);
}

static void cma_save_ib_info(struct sockaddr *src_addr,
			     struct sockaddr *dst_addr,
			     const struct rdma_cm_id *listen_id,
			     const struct sa_path_rec *path)
{
	struct sockaddr_ib *listen_ib, *ib;

	listen_ib = (struct sockaddr_ib *) &listen_id->route.addr.src_addr;
	if (src_addr) {
		ib = (struct sockaddr_ib *)src_addr;
		ib->sib_family = AF_IB;
		if (path) {
			ib->sib_pkey = path->pkey;
			ib->sib_flowinfo = path->flow_label;
			memcpy(&ib->sib_addr, &path->sgid, 16);
			ib->sib_sid = path->service_id;
			ib->sib_scope_id = 0;
		} else {
			ib->sib_pkey = listen_ib->sib_pkey;
			ib->sib_flowinfo = listen_ib->sib_flowinfo;
			ib->sib_addr = listen_ib->sib_addr;
			ib->sib_sid = listen_ib->sib_sid;
			ib->sib_scope_id = listen_ib->sib_scope_id;
		}
		ib->sib_sid_mask = cpu_to_be64(0xffffffffffffffffULL);
	}
	if (dst_addr) {
		ib = (struct sockaddr_ib *)dst_addr;
		ib->sib_family = AF_IB;
		if (path) {
			ib->sib_pkey = path->pkey;
			ib->sib_flowinfo = path->flow_label;
			memcpy(&ib->sib_addr, &path->dgid, 16);
		}
	}
}

static void cma_save_ip4_info(struct sockaddr_in *src_addr,
			      struct sockaddr_in *dst_addr,
			      struct cma_hdr *hdr,
			      __be16 local_port)
{
	if (src_addr) {
		*src_addr = (struct sockaddr_in) {
			.sin_family = AF_INET,
			.sin_addr.s_addr = hdr->dst_addr.ip4.addr,
			.sin_port = local_port,
		};
	}

	if (dst_addr) {
		*dst_addr = (struct sockaddr_in) {
			.sin_family = AF_INET,
			.sin_addr.s_addr = hdr->src_addr.ip4.addr,
			.sin_port = hdr->port,
		};
	}
}

static void cma_save_ip6_info(struct sockaddr_in6 *src_addr,
			      struct sockaddr_in6 *dst_addr,
			      struct cma_hdr *hdr,
			      __be16 local_port)
{
	if (src_addr) {
		*src_addr = (struct sockaddr_in6) {
			.sin6_family = AF_INET6,
			.sin6_addr = hdr->dst_addr.ip6,
			.sin6_port = local_port,
		};
	}

	if (dst_addr) {
		*dst_addr = (struct sockaddr_in6) {
			.sin6_family = AF_INET6,
			.sin6_addr = hdr->src_addr.ip6,
			.sin6_port = hdr->port,
		};
	}
}

static u16 cma_port_from_service_id(__be64 service_id)
{
	return (u16)be64_to_cpu(service_id);
}

static int cma_save_ip_info(struct sockaddr *src_addr,
			    struct sockaddr *dst_addr,
			    const struct ib_cm_event *ib_event,
			    __be64 service_id)
{
	struct cma_hdr *hdr;
	__be16 port;

	hdr = ib_event->private_data;
	if (hdr->cma_version != CMA_VERSION)
		return -EINVAL;

	port = htons(cma_port_from_service_id(service_id));

	switch (cma_get_ip_ver(hdr)) {
	case 4:
		cma_save_ip4_info((struct sockaddr_in *)src_addr,
				  (struct sockaddr_in *)dst_addr, hdr, port);
		break;
	case 6:
		cma_save_ip6_info((struct sockaddr_in6 *)src_addr,
				  (struct sockaddr_in6 *)dst_addr, hdr, port);
		break;
	default:
		return -EAFNOSUPPORT;
	}

	return 0;
}

static int cma_save_net_info(struct sockaddr *src_addr,
			     struct sockaddr *dst_addr,
			     const struct rdma_cm_id *listen_id,
			     const struct ib_cm_event *ib_event,
			     sa_family_t sa_family, __be64 service_id)
{
	if (sa_family == AF_IB) {
		if (ib_event->event == IB_CM_REQ_RECEIVED)
			cma_save_ib_info(src_addr, dst_addr, listen_id,
					 ib_event->param.req_rcvd.primary_path);
		else if (ib_event->event == IB_CM_SIDR_REQ_RECEIVED)
			cma_save_ib_info(src_addr, dst_addr, listen_id, NULL);
		return 0;
	}

	return cma_save_ip_info(src_addr, dst_addr, ib_event, service_id);
}

static int cma_save_req_info(const struct ib_cm_event *ib_event,
			     struct cma_req_info *req)
{
	const struct ib_cm_req_event_param *req_param =
		&ib_event->param.req_rcvd;
	const struct ib_cm_sidr_req_event_param *sidr_param =
		&ib_event->param.sidr_req_rcvd;

	switch (ib_event->event) {
	case IB_CM_REQ_RECEIVED:
		req->device	= req_param->listen_id->device;
		req->port	= req_param->port;
		memcpy(&req->local_gid, &req_param->primary_path->sgid,
		       sizeof(req->local_gid));
		req->has_gid	= true;
		req->service_id = req_param->primary_path->service_id;
		req->pkey	= be16_to_cpu(req_param->primary_path->pkey);
		if (req->pkey != req_param->bth_pkey)
			pr_warn_ratelimited("RDMA CMA: got different BTH P_Key (0x%x) and primary path P_Key (0x%x)\n"
					    "RDMA CMA: in the future this may cause the request to be dropped\n",
					    req_param->bth_pkey, req->pkey);
		break;
	case IB_CM_SIDR_REQ_RECEIVED:
		req->device	= sidr_param->listen_id->device;
		req->port	= sidr_param->port;
		req->has_gid	= false;
		req->service_id	= sidr_param->service_id;
		req->pkey	= sidr_param->pkey;
		if (req->pkey != sidr_param->bth_pkey)
			pr_warn_ratelimited("RDMA CMA: got different BTH P_Key (0x%x) and SIDR request payload P_Key (0x%x)\n"
					    "RDMA CMA: in the future this may cause the request to be dropped\n",
					    sidr_param->bth_pkey, req->pkey);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static bool validate_ipv4_net_dev(struct net_device *net_dev,
				  const struct sockaddr_in *dst_addr,
				  const struct sockaddr_in *src_addr)
{
	__be32 daddr = dst_addr->sin_addr.s_addr,
	       saddr = src_addr->sin_addr.s_addr;
	struct fib_result res;
	struct flowi4 fl4;
	int err;
	bool ret;

	if (ipv4_is_multicast(saddr) || ipv4_is_lbcast(saddr) ||
	    ipv4_is_lbcast(daddr) || ipv4_is_zeronet(saddr) ||
	    ipv4_is_zeronet(daddr) || ipv4_is_loopback(daddr) ||
	    ipv4_is_loopback(saddr))
		return false;

	memset(&fl4, 0, sizeof(fl4));
	fl4.flowi4_iif = net_dev->ifindex;
	fl4.daddr = daddr;
	fl4.saddr = saddr;

	rcu_read_lock();
	err = fib_lookup(dev_net(net_dev), &fl4, &res, 0);
	ret = err == 0 && FIB_RES_DEV(res) == net_dev;
	rcu_read_unlock();

	return ret;
}

static bool validate_ipv6_net_dev(struct net_device *net_dev,
				  const struct sockaddr_in6 *dst_addr,
				  const struct sockaddr_in6 *src_addr)
{
#if IS_ENABLED(CONFIG_IPV6)
	const int strict = ipv6_addr_type(&dst_addr->sin6_addr) &
			   IPV6_ADDR_LINKLOCAL;
	struct rt6_info *rt = rt6_lookup(dev_net(net_dev), &dst_addr->sin6_addr,
					 &src_addr->sin6_addr, net_dev->ifindex,
					 NULL, strict);
	bool ret;

	if (!rt)
		return false;

	ret = rt->rt6i_idev->dev == net_dev;
	ip6_rt_put(rt);

	return ret;
#else
	return false;
#endif
}

static bool validate_net_dev(struct net_device *net_dev,
			     const struct sockaddr *daddr,
			     const struct sockaddr *saddr)
{
	const struct sockaddr_in *daddr4 = (const struct sockaddr_in *)daddr;
	const struct sockaddr_in *saddr4 = (const struct sockaddr_in *)saddr;
	const struct sockaddr_in6 *daddr6 = (const struct sockaddr_in6 *)daddr;
	const struct sockaddr_in6 *saddr6 = (const struct sockaddr_in6 *)saddr;

	switch (daddr->sa_family) {
	case AF_INET:
		return saddr->sa_family == AF_INET &&
		       validate_ipv4_net_dev(net_dev, daddr4, saddr4);

	case AF_INET6:
		return saddr->sa_family == AF_INET6 &&
		       validate_ipv6_net_dev(net_dev, daddr6, saddr6);

	default:
		return false;
	}
}

static struct net_device *
roce_get_net_dev_by_cm_event(const struct ib_cm_event *ib_event)
{
	const struct ib_gid_attr *sgid_attr = NULL;
	struct net_device *ndev;

	if (ib_event->event == IB_CM_REQ_RECEIVED)
		sgid_attr = ib_event->param.req_rcvd.ppath_sgid_attr;
	else if (ib_event->event == IB_CM_SIDR_REQ_RECEIVED)
		sgid_attr = ib_event->param.sidr_req_rcvd.sgid_attr;

	if (!sgid_attr)
		return NULL;

	rcu_read_lock();
	ndev = rdma_read_gid_attr_ndev_rcu(sgid_attr);
	if (IS_ERR(ndev))
		ndev = NULL;
	else
		dev_hold(ndev);
	rcu_read_unlock();
	return ndev;
}

static struct net_device *cma_get_net_dev(const struct ib_cm_event *ib_event,
					  struct cma_req_info *req)
{
	struct sockaddr *listen_addr =
			(struct sockaddr *)&req->listen_addr_storage;
	struct sockaddr *src_addr = (struct sockaddr *)&req->src_addr_storage;
	struct net_device *net_dev;
	const union ib_gid *gid = req->has_gid ? &req->local_gid : NULL;
	int err;

	err = cma_save_ip_info(listen_addr, src_addr, ib_event,
			       req->service_id);
	if (err)
		return ERR_PTR(err);

	if (rdma_protocol_roce(req->device, req->port))
		net_dev = roce_get_net_dev_by_cm_event(ib_event);
	else
		net_dev = ib_get_net_dev_by_params(req->device, req->port,
						   req->pkey,
						   gid, listen_addr);
	if (!net_dev)
		return ERR_PTR(-ENODEV);

	return net_dev;
}

static enum rdma_ucm_port_space rdma_ps_from_service_id(__be64 service_id)
{
	return (be64_to_cpu(service_id) >> 16) & 0xffff;
}

static bool cma_match_private_data(struct rdma_id_private *id_priv,
				   const struct cma_hdr *hdr)
{
	struct sockaddr *addr = cma_src_addr(id_priv);
	__be32 ip4_addr;
	struct in6_addr ip6_addr;

	if (cma_any_addr(addr) && !id_priv->afonly)
		return true;

	switch (addr->sa_family) {
	case AF_INET:
		ip4_addr = ((struct sockaddr_in *)addr)->sin_addr.s_addr;
		if (cma_get_ip_ver(hdr) != 4)
			return false;
		if (!cma_any_addr(addr) &&
		    hdr->dst_addr.ip4.addr != ip4_addr)
			return false;
		break;
	case AF_INET6:
		ip6_addr = ((struct sockaddr_in6 *)addr)->sin6_addr;
		if (cma_get_ip_ver(hdr) != 6)
			return false;
		if (!cma_any_addr(addr) &&
		    memcmp(&hdr->dst_addr.ip6, &ip6_addr, sizeof(ip6_addr)))
			return false;
		break;
	case AF_IB:
		return true;
	default:
		return false;
	}

	return true;
}

static bool cma_protocol_roce(const struct rdma_cm_id *id)
{
	struct ib_device *device = id->device;
	const u32 port_num = id->port_num ?: rdma_start_port(device);

	return rdma_protocol_roce(device, port_num);
}

static bool cma_is_req_ipv6_ll(const struct cma_req_info *req)
{
	const struct sockaddr *daddr =
			(const struct sockaddr *)&req->listen_addr_storage;
	const struct sockaddr_in6 *daddr6 = (const struct sockaddr_in6 *)daddr;

	/* Returns true if the req is for IPv6 link local */
	return (daddr->sa_family == AF_INET6 &&
		(ipv6_addr_type(&daddr6->sin6_addr) & IPV6_ADDR_LINKLOCAL));
}

static bool cma_match_net_dev(const struct rdma_cm_id *id,
			      const struct net_device *net_dev,
			      const struct cma_req_info *req)
{
	const struct rdma_addr *addr = &id->route.addr;

	if (!net_dev)
		/* This request is an AF_IB request */
		return (!id->port_num || id->port_num == req->port) &&
		       (addr->src_addr.ss_family == AF_IB);

	/*
	 * If the request is not for IPv6 link local, allow matching
	 * request to any netdevice of the one or multiport rdma device.
	 */
	if (!cma_is_req_ipv6_ll(req))
		return true;
	/*
	 * Net namespaces must match, and if the listner is listening
	 * on a specific netdevice than netdevice must match as well.
	 */
	if (net_eq(dev_net(net_dev), addr->dev_addr.net) &&
	    (!!addr->dev_addr.bound_dev_if ==
	     (addr->dev_addr.bound_dev_if == net_dev->ifindex)))
		return true;
	else
		return false;
}

static struct rdma_id_private *cma_find_listener(
		const struct rdma_bind_list *bind_list,
		const struct ib_cm_id *cm_id,
		const struct ib_cm_event *ib_event,
		const struct cma_req_info *req,
		const struct net_device *net_dev)
{
	struct rdma_id_private *id_priv, *id_priv_dev;

	lockdep_assert_held(&lock);

	if (!bind_list)
		return ERR_PTR(-EINVAL);

	hlist_for_each_entry(id_priv, &bind_list->owners, node) {
		if (cma_match_private_data(id_priv, ib_event->private_data)) {
			if (id_priv->id.device == cm_id->device &&
			    cma_match_net_dev(&id_priv->id, net_dev, req))
				return id_priv;
			list_for_each_entry(id_priv_dev,
					    &id_priv->listen_list,
					    listen_list) {
				if (id_priv_dev->id.device == cm_id->device &&
				    cma_match_net_dev(&id_priv_dev->id,
						      net_dev, req))
					return id_priv_dev;
			}
		}
	}

	return ERR_PTR(-EINVAL);
}

static struct rdma_id_private *
cma_ib_id_from_event(struct ib_cm_id *cm_id,
		     const struct ib_cm_event *ib_event,
		     struct cma_req_info *req,
		     struct net_device **net_dev)
{
	struct rdma_bind_list *bind_list;
	struct rdma_id_private *id_priv;
	int err;

	err = cma_save_req_info(ib_event, req);
	if (err)
		return ERR_PTR(err);

	*net_dev = cma_get_net_dev(ib_event, req);
	if (IS_ERR(*net_dev)) {
		if (PTR_ERR(*net_dev) == -EAFNOSUPPORT) {
			/* Assuming the protocol is AF_IB */
			*net_dev = NULL;
		} else {
			return ERR_CAST(*net_dev);
		}
	}

	mutex_lock(&lock);
	/*
	 * Net namespace might be getting deleted while route lookup,
	 * cm_id lookup is in progress. Therefore, perform netdevice
	 * validation, cm_id lookup under rcu lock.
	 * RCU lock along with netdevice state check, synchronizes with
	 * netdevice migrating to different net namespace and also avoids
	 * case where net namespace doesn't get deleted while lookup is in
	 * progress.
	 * If the device state is not IFF_UP, its properties such as ifindex
	 * and nd_net cannot be trusted to remain valid without rcu lock.
	 * net/core/dev.c change_net_namespace() ensures to synchronize with
	 * ongoing operations on net device after device is closed using
	 * synchronize_net().
	 */
	rcu_read_lock();
	if (*net_dev) {
		/*
		 * If netdevice is down, it is likely that it is administratively
		 * down or it might be migrating to different namespace.
		 * In that case avoid further processing, as the net namespace
		 * or ifindex may change.
		 */
		if (((*net_dev)->flags & IFF_UP) == 0) {
			id_priv = ERR_PTR(-EHOSTUNREACH);
			goto err;
		}

		if (!validate_net_dev(*net_dev,
				 (struct sockaddr *)&req->listen_addr_storage,
				 (struct sockaddr *)&req->src_addr_storage)) {
			id_priv = ERR_PTR(-EHOSTUNREACH);
			goto err;
		}
	}

	bind_list = cma_ps_find(*net_dev ? dev_net(*net_dev) : &init_net,
				rdma_ps_from_service_id(req->service_id),
				cma_port_from_service_id(req->service_id));
	id_priv = cma_find_listener(bind_list, cm_id, ib_event, req, *net_dev);
err:
	rcu_read_unlock();
	mutex_unlock(&lock);
	if (IS_ERR(id_priv) && *net_dev) {
		dev_put(*net_dev);
		*net_dev = NULL;
	}
	return id_priv;
}

static inline u8 cma_user_data_offset(struct rdma_id_private *id_priv)
{
	return cma_family(id_priv) == AF_IB ? 0 : sizeof(struct cma_hdr);
}

static void cma_cancel_route(struct rdma_id_private *id_priv)
{
	if (rdma_cap_ib_sa(id_priv->id.device, id_priv->id.port_num)) {
		if (id_priv->query)
			ib_sa_cancel_query(id_priv->query_id, id_priv->query);
	}
}

static void cma_cancel_listens(struct rdma_id_private *id_priv)
{
	struct rdma_id_private *dev_id_priv;

	/*
	 * Remove from listen_any_list to prevent added devices from spawning
	 * additional listen requests.
	 */
	mutex_lock(&lock);
	list_del(&id_priv->list);

	while (!list_empty(&id_priv->listen_list)) {
		dev_id_priv = list_entry(id_priv->listen_list.next,
					 struct rdma_id_private, listen_list);
		/* sync with device removal to avoid duplicate destruction */
		list_del_init(&dev_id_priv->list);
		list_del(&dev_id_priv->listen_list);
		mutex_unlock(&lock);

		rdma_destroy_id(&dev_id_priv->id);
		mutex_lock(&lock);
	}
	mutex_unlock(&lock);
}

static void cma_cancel_operation(struct rdma_id_private *id_priv,
				 enum rdma_cm_state state)
{
	switch (state) {
	case RDMA_CM_ADDR_QUERY:
		rdma_addr_cancel(&id_priv->id.route.addr.dev_addr);
		break;
	case RDMA_CM_ROUTE_QUERY:
		cma_cancel_route(id_priv);
		break;
	case RDMA_CM_LISTEN:
		if (cma_any_addr(cma_src_addr(id_priv)) && !id_priv->cma_dev)
			cma_cancel_listens(id_priv);
		break;
	default:
		break;
	}
}

static void cma_release_port(struct rdma_id_private *id_priv)
{
	struct rdma_bind_list *bind_list = id_priv->bind_list;
	struct net *net = id_priv->id.route.addr.dev_addr.net;

	if (!bind_list)
		return;

	mutex_lock(&lock);
	hlist_del(&id_priv->node);
	if (hlist_empty(&bind_list->owners)) {
		cma_ps_remove(net, bind_list->ps, bind_list->port);
		kfree(bind_list);
	}
	mutex_unlock(&lock);
}

static void destroy_mc(struct rdma_id_private *id_priv,
		       struct cma_multicast *mc)
{
	if (rdma_cap_ib_mcast(id_priv->id.device, id_priv->id.port_num))
		ib_sa_free_multicast(mc->sa_mc);

	if (rdma_protocol_roce(id_priv->id.device, id_priv->id.port_num)) {
		struct rdma_dev_addr *dev_addr =
			&id_priv->id.route.addr.dev_addr;
		struct net_device *ndev = NULL;

		if (dev_addr->bound_dev_if)
			ndev = dev_get_by_index(dev_addr->net,
						dev_addr->bound_dev_if);
		if (ndev) {
			union ib_gid mgid;

			cma_set_mgid(id_priv, (struct sockaddr *)&mc->addr,
				     &mgid);
			cma_igmp_send(ndev, &mgid, false);
			dev_put(ndev);
		}

		cancel_work_sync(&mc->iboe_join.work);
	}
	kfree(mc);
}

static void cma_leave_mc_groups(struct rdma_id_private *id_priv)
{
	struct cma_multicast *mc;

	while (!list_empty(&id_priv->mc_list)) {
		mc = list_first_entry(&id_priv->mc_list, struct cma_multicast,
				      list);
		list_del(&mc->list);
		destroy_mc(id_priv, mc);
	}
}

static void _destroy_id(struct rdma_id_private *id_priv,
			enum rdma_cm_state state)
{
	cma_cancel_operation(id_priv, state);

	if (id_priv->cma_dev) {
		if (rdma_cap_ib_cm(id_priv->id.device, 1)) {
			if (id_priv->cm_id.ib)
				ib_destroy_cm_id(id_priv->cm_id.ib);
		} else if (rdma_cap_iw_cm(id_priv->id.device, 1)) {
			if (id_priv->cm_id.iw)
				iw_destroy_cm_id(id_priv->cm_id.iw);
		}
		cma_leave_mc_groups(id_priv);
		cma_release_dev(id_priv);
	}

	cma_release_port(id_priv);
	cma_id_put(id_priv);
	wait_for_completion(&id_priv->comp);

	if (id_priv->internal_id)
		cma_id_put(id_priv->id.context);

	kfree(id_priv->id.route.path_rec);

	put_net(id_priv->id.route.addr.dev_addr.net);
	rdma_restrack_del(&id_priv->res);
	kfree(id_priv);
}

/*
 * destroy an ID from within the handler_mutex. This ensures that no other
 * handlers can start running concurrently.
 */
static void destroy_id_handler_unlock(struct rdma_id_private *id_priv)
	__releases(&idprv->handler_mutex)
{
	enum rdma_cm_state state;
	unsigned long flags;

	trace_cm_id_destroy(id_priv);

	/*
	 * Setting the state to destroyed under the handler mutex provides a
	 * fence against calling handler callbacks. If this is invoked due to
	 * the failure of a handler callback then it guarentees that no future
	 * handlers will be called.
	 */
	lockdep_assert_held(&id_priv->handler_mutex);
	spin_lock_irqsave(&id_priv->lock, flags);
	state = id_priv->state;
	id_priv->state = RDMA_CM_DESTROYING;
	spin_unlock_irqrestore(&id_priv->lock, flags);
	mutex_unlock(&id_priv->handler_mutex);
	_destroy_id(id_priv, state);
}

void rdma_destroy_id(struct rdma_cm_id *id)
{
	struct rdma_id_private *id_priv =
		container_of(id, struct rdma_id_private, id);

	mutex_lock(&id_priv->handler_mutex);
	destroy_id_handler_unlock(id_priv);
}
EXPORT_SYMBOL(rdma_destroy_id);

static int cma_rep_recv(struct rdma_id_private *id_priv)
{
	int ret;

	ret = cma_modify_qp_rtr(id_priv, NULL);
	if (ret)
		goto reject;

	ret = cma_modify_qp_rts(id_priv, NULL);
	if (ret)
		goto reject;

	trace_cm_send_rtu(id_priv);
	ret = ib_send_cm_rtu(id_priv->cm_id.ib, NULL, 0);
	if (ret)
		goto reject;

	return 0;
reject:
	pr_debug_ratelimited("RDMA CM: CONNECT_ERROR: failed to handle reply. status %d\n", ret);
	cma_modify_qp_err(id_priv);
	trace_cm_send_rej(id_priv);
	ib_send_cm_rej(id_priv->cm_id.ib, IB_CM_REJ_CONSUMER_DEFINED,
		       NULL, 0, NULL, 0);
	return ret;
}

static void cma_set_rep_event_data(struct rdma_cm_event *event,
				   const struct ib_cm_rep_event_param *rep_data,
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

	event->ece.vendor_id = rep_data->ece.vendor_id;
	event->ece.attr_mod = rep_data->ece.attr_mod;
}

static int cma_cm_event_handler(struct rdma_id_private *id_priv,
				struct rdma_cm_event *event)
{
	int ret;

	lockdep_assert_held(&id_priv->handler_mutex);

	trace_cm_event_handler(id_priv, event);
	ret = id_priv->id.event_handler(&id_priv->id, event);
	trace_cm_event_done(id_priv, event, ret);
	return ret;
}

static int cma_ib_handler(struct ib_cm_id *cm_id,
			  const struct ib_cm_event *ib_event)
{
	struct rdma_id_private *id_priv = cm_id->context;
	struct rdma_cm_event event = {};
	enum rdma_cm_state state;
	int ret;

	mutex_lock(&id_priv->handler_mutex);
	state = READ_ONCE(id_priv->state);
	if ((ib_event->event != IB_CM_TIMEWAIT_EXIT &&
	     state != RDMA_CM_CONNECT) ||
	    (ib_event->event == IB_CM_TIMEWAIT_EXIT &&
	     state != RDMA_CM_DISCONNECT))
		goto out;

	switch (ib_event->event) {
	case IB_CM_REQ_ERROR:
	case IB_CM_REP_ERROR:
		event.event = RDMA_CM_EVENT_UNREACHABLE;
		event.status = -ETIMEDOUT;
		break;
	case IB_CM_REP_RECEIVED:
		if (state == RDMA_CM_CONNECT &&
		    (id_priv->id.qp_type != IB_QPT_UD)) {
			trace_cm_send_mra(id_priv);
			ib_send_cm_mra(cm_id, CMA_CM_MRA_SETTING, NULL, 0);
		}
		if (id_priv->id.qp) {
			event.status = cma_rep_recv(id_priv);
			event.event = event.status ? RDMA_CM_EVENT_CONNECT_ERROR :
						     RDMA_CM_EVENT_ESTABLISHED;
		} else {
			event.event = RDMA_CM_EVENT_CONNECT_RESPONSE;
		}
		cma_set_rep_event_data(&event, &ib_event->param.rep_rcvd,
				       ib_event->private_data);
		break;
	case IB_CM_RTU_RECEIVED:
	case IB_CM_USER_ESTABLISHED:
		event.event = RDMA_CM_EVENT_ESTABLISHED;
		break;
	case IB_CM_DREQ_ERROR:
		event.status = -ETIMEDOUT;
		fallthrough;
	case IB_CM_DREQ_RECEIVED:
	case IB_CM_DREP_RECEIVED:
		if (!cma_comp_exch(id_priv, RDMA_CM_CONNECT,
				   RDMA_CM_DISCONNECT))
			goto out;
		event.event = RDMA_CM_EVENT_DISCONNECTED;
		break;
	case IB_CM_TIMEWAIT_EXIT:
		event.event = RDMA_CM_EVENT_TIMEWAIT_EXIT;
		break;
	case IB_CM_MRA_RECEIVED:
		/* ignore event */
		goto out;
	case IB_CM_REJ_RECEIVED:
		pr_debug_ratelimited("RDMA CM: REJECTED: %s\n", rdma_reject_msg(&id_priv->id,
										ib_event->param.rej_rcvd.reason));
		cma_modify_qp_err(id_priv);
		event.status = ib_event->param.rej_rcvd.reason;
		event.event = RDMA_CM_EVENT_REJECTED;
		event.param.conn.private_data = ib_event->private_data;
		event.param.conn.private_data_len = IB_CM_REJ_PRIVATE_DATA_SIZE;
		break;
	default:
		pr_err("RDMA CMA: unexpected IB CM event: %d\n",
		       ib_event->event);
		goto out;
	}

	ret = cma_cm_event_handler(id_priv, &event);
	if (ret) {
		/* Destroy the CM ID by returning a non-zero value. */
		id_priv->cm_id.ib = NULL;
		destroy_id_handler_unlock(id_priv);
		return ret;
	}
out:
	mutex_unlock(&id_priv->handler_mutex);
	return 0;
}

static struct rdma_id_private *
cma_ib_new_conn_id(const struct rdma_cm_id *listen_id,
		   const struct ib_cm_event *ib_event,
		   struct net_device *net_dev)
{
	struct rdma_id_private *listen_id_priv;
	struct rdma_id_private *id_priv;
	struct rdma_cm_id *id;
	struct rdma_route *rt;
	const sa_family_t ss_family = listen_id->route.addr.src_addr.ss_family;
	struct sa_path_rec *path = ib_event->param.req_rcvd.primary_path;
	const __be64 service_id =
		ib_event->param.req_rcvd.primary_path->service_id;
	int ret;

	listen_id_priv = container_of(listen_id, struct rdma_id_private, id);
	id_priv = __rdma_create_id(listen_id->route.addr.dev_addr.net,
				   listen_id->event_handler, listen_id->context,
				   listen_id->ps,
				   ib_event->param.req_rcvd.qp_type,
				   listen_id_priv);
	if (IS_ERR(id_priv))
		return NULL;

	id = &id_priv->id;
	if (cma_save_net_info((struct sockaddr *)&id->route.addr.src_addr,
			      (struct sockaddr *)&id->route.addr.dst_addr,
			      listen_id, ib_event, ss_family, service_id))
		goto err;

	rt = &id->route;
	rt->num_paths = ib_event->param.req_rcvd.alternate_path ? 2 : 1;
	rt->path_rec = kmalloc_array(rt->num_paths, sizeof(*rt->path_rec),
				     GFP_KERNEL);
	if (!rt->path_rec)
		goto err;

	rt->path_rec[0] = *path;
	if (rt->num_paths == 2)
		rt->path_rec[1] = *ib_event->param.req_rcvd.alternate_path;

	if (net_dev) {
		rdma_copy_src_l2_addr(&rt->addr.dev_addr, net_dev);
	} else {
		if (!cma_protocol_roce(listen_id) &&
		    cma_any_addr(cma_src_addr(id_priv))) {
			rt->addr.dev_addr.dev_type = ARPHRD_INFINIBAND;
			rdma_addr_set_sgid(&rt->addr.dev_addr, &rt->path_rec[0].sgid);
			ib_addr_set_pkey(&rt->addr.dev_addr, be16_to_cpu(rt->path_rec[0].pkey));
		} else if (!cma_any_addr(cma_src_addr(id_priv))) {
			ret = cma_translate_addr(cma_src_addr(id_priv), &rt->addr.dev_addr);
			if (ret)
				goto err;
		}
	}
	rdma_addr_set_dgid(&rt->addr.dev_addr, &rt->path_rec[0].dgid);

	id_priv->state = RDMA_CM_CONNECT;
	return id_priv;

err:
	rdma_destroy_id(id);
	return NULL;
}

static struct rdma_id_private *
cma_ib_new_udp_id(const struct rdma_cm_id *listen_id,
		  const struct ib_cm_event *ib_event,
		  struct net_device *net_dev)
{
	const struct rdma_id_private *listen_id_priv;
	struct rdma_id_private *id_priv;
	struct rdma_cm_id *id;
	const sa_family_t ss_family = listen_id->route.addr.src_addr.ss_family;
	struct net *net = listen_id->route.addr.dev_addr.net;
	int ret;

	listen_id_priv = container_of(listen_id, struct rdma_id_private, id);
	id_priv = __rdma_create_id(net, listen_id->event_handler,
				   listen_id->context, listen_id->ps, IB_QPT_UD,
				   listen_id_priv);
	if (IS_ERR(id_priv))
		return NULL;

	id = &id_priv->id;
	if (cma_save_net_info((struct sockaddr *)&id->route.addr.src_addr,
			      (struct sockaddr *)&id->route.addr.dst_addr,
			      listen_id, ib_event, ss_family,
			      ib_event->param.sidr_req_rcvd.service_id))
		goto err;

	if (net_dev) {
		rdma_copy_src_l2_addr(&id->route.addr.dev_addr, net_dev);
	} else {
		if (!cma_any_addr(cma_src_addr(id_priv))) {
			ret = cma_translate_addr(cma_src_addr(id_priv),
						 &id->route.addr.dev_addr);
			if (ret)
				goto err;
		}
	}

	id_priv->state = RDMA_CM_CONNECT;
	return id_priv;
err:
	rdma_destroy_id(id);
	return NULL;
}

static void cma_set_req_event_data(struct rdma_cm_event *event,
				   const struct ib_cm_req_event_param *req_data,
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

	event->ece.vendor_id = req_data->ece.vendor_id;
	event->ece.attr_mod = req_data->ece.attr_mod;
}

static int cma_ib_check_req_qp_type(const struct rdma_cm_id *id,
				    const struct ib_cm_event *ib_event)
{
	return (((ib_event->event == IB_CM_REQ_RECEIVED) &&
		 (ib_event->param.req_rcvd.qp_type == id->qp_type)) ||
		((ib_event->event == IB_CM_SIDR_REQ_RECEIVED) &&
		 (id->qp_type == IB_QPT_UD)) ||
		(!id->qp_type));
}

static int cma_ib_req_handler(struct ib_cm_id *cm_id,
			      const struct ib_cm_event *ib_event)
{
	struct rdma_id_private *listen_id, *conn_id = NULL;
	struct rdma_cm_event event = {};
	struct cma_req_info req = {};
	struct net_device *net_dev;
	u8 offset;
	int ret;

	listen_id = cma_ib_id_from_event(cm_id, ib_event, &req, &net_dev);
	if (IS_ERR(listen_id))
		return PTR_ERR(listen_id);

	trace_cm_req_handler(listen_id, ib_event->event);
	if (!cma_ib_check_req_qp_type(&listen_id->id, ib_event)) {
		ret = -EINVAL;
		goto net_dev_put;
	}

	mutex_lock(&listen_id->handler_mutex);
	if (READ_ONCE(listen_id->state) != RDMA_CM_LISTEN) {
		ret = -ECONNABORTED;
		goto err_unlock;
	}

	offset = cma_user_data_offset(listen_id);
	event.event = RDMA_CM_EVENT_CONNECT_REQUEST;
	if (ib_event->event == IB_CM_SIDR_REQ_RECEIVED) {
		conn_id = cma_ib_new_udp_id(&listen_id->id, ib_event, net_dev);
		event.param.ud.private_data = ib_event->private_data + offset;
		event.param.ud.private_data_len =
				IB_CM_SIDR_REQ_PRIVATE_DATA_SIZE - offset;
	} else {
		conn_id = cma_ib_new_conn_id(&listen_id->id, ib_event, net_dev);
		cma_set_req_event_data(&event, &ib_event->param.req_rcvd,
				       ib_event->private_data, offset);
	}
	if (!conn_id) {
		ret = -ENOMEM;
		goto err_unlock;
	}

	mutex_lock_nested(&conn_id->handler_mutex, SINGLE_DEPTH_NESTING);
	ret = cma_ib_acquire_dev(conn_id, listen_id, &req);
	if (ret) {
		destroy_id_handler_unlock(conn_id);
		goto err_unlock;
	}

	conn_id->cm_id.ib = cm_id;
	cm_id->context = conn_id;
	cm_id->cm_handler = cma_ib_handler;

	ret = cma_cm_event_handler(conn_id, &event);
	if (ret) {
		/* Destroy the CM ID by returning a non-zero value. */
		conn_id->cm_id.ib = NULL;
		mutex_unlock(&listen_id->handler_mutex);
		destroy_id_handler_unlock(conn_id);
		goto net_dev_put;
	}

	if (READ_ONCE(conn_id->state) == RDMA_CM_CONNECT &&
	    conn_id->id.qp_type != IB_QPT_UD) {
		trace_cm_send_mra(cm_id->context);
		ib_send_cm_mra(cm_id, CMA_CM_MRA_SETTING, NULL, 0);
	}
	mutex_unlock(&conn_id->handler_mutex);

err_unlock:
	mutex_unlock(&listen_id->handler_mutex);

net_dev_put:
	if (net_dev)
		dev_put(net_dev);

	return ret;
}

__be64 rdma_get_service_id(struct rdma_cm_id *id, struct sockaddr *addr)
{
	if (addr->sa_family == AF_IB)
		return ((struct sockaddr_ib *) addr)->sib_sid;

	return cpu_to_be64(((u64)id->ps << 16) + be16_to_cpu(cma_port(addr)));
}
EXPORT_SYMBOL(rdma_get_service_id);

void rdma_read_gids(struct rdma_cm_id *cm_id, union ib_gid *sgid,
		    union ib_gid *dgid)
{
	struct rdma_addr *addr = &cm_id->route.addr;

	if (!cm_id->device) {
		if (sgid)
			memset(sgid, 0, sizeof(*sgid));
		if (dgid)
			memset(dgid, 0, sizeof(*dgid));
		return;
	}

	if (rdma_protocol_roce(cm_id->device, cm_id->port_num)) {
		if (sgid)
			rdma_ip2gid((struct sockaddr *)&addr->src_addr, sgid);
		if (dgid)
			rdma_ip2gid((struct sockaddr *)&addr->dst_addr, dgid);
	} else {
		if (sgid)
			rdma_addr_get_sgid(&addr->dev_addr, sgid);
		if (dgid)
			rdma_addr_get_dgid(&addr->dev_addr, dgid);
	}
}
EXPORT_SYMBOL(rdma_read_gids);

static int cma_iw_handler(struct iw_cm_id *iw_id, struct iw_cm_event *iw_event)
{
	struct rdma_id_private *id_priv = iw_id->context;
	struct rdma_cm_event event = {};
	int ret = 0;
	struct sockaddr *laddr = (struct sockaddr *)&iw_event->local_addr;
	struct sockaddr *raddr = (struct sockaddr *)&iw_event->remote_addr;

	mutex_lock(&id_priv->handler_mutex);
	if (READ_ONCE(id_priv->state) != RDMA_CM_CONNECT)
		goto out;

	switch (iw_event->event) {
	case IW_CM_EVENT_CLOSE:
		event.event = RDMA_CM_EVENT_DISCONNECTED;
		break;
	case IW_CM_EVENT_CONNECT_REPLY:
		memcpy(cma_src_addr(id_priv), laddr,
		       rdma_addr_size(laddr));
		memcpy(cma_dst_addr(id_priv), raddr,
		       rdma_addr_size(raddr));
		switch (iw_event->status) {
		case 0:
			event.event = RDMA_CM_EVENT_ESTABLISHED;
			event.param.conn.initiator_depth = iw_event->ird;
			event.param.conn.responder_resources = iw_event->ord;
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
		event.param.conn.initiator_depth = iw_event->ird;
		event.param.conn.responder_resources = iw_event->ord;
		break;
	default:
		goto out;
	}

	event.status = iw_event->status;
	event.param.conn.private_data = iw_event->private_data;
	event.param.conn.private_data_len = iw_event->private_data_len;
	ret = cma_cm_event_handler(id_priv, &event);
	if (ret) {
		/* Destroy the CM ID by returning a non-zero value. */
		id_priv->cm_id.iw = NULL;
		destroy_id_handler_unlock(id_priv);
		return ret;
	}

out:
	mutex_unlock(&id_priv->handler_mutex);
	return ret;
}

static int iw_conn_req_handler(struct iw_cm_id *cm_id,
			       struct iw_cm_event *iw_event)
{
	struct rdma_id_private *listen_id, *conn_id;
	struct rdma_cm_event event = {};
	int ret = -ECONNABORTED;
	struct sockaddr *laddr = (struct sockaddr *)&iw_event->local_addr;
	struct sockaddr *raddr = (struct sockaddr *)&iw_event->remote_addr;

	event.event = RDMA_CM_EVENT_CONNECT_REQUEST;
	event.param.conn.private_data = iw_event->private_data;
	event.param.conn.private_data_len = iw_event->private_data_len;
	event.param.conn.initiator_depth = iw_event->ird;
	event.param.conn.responder_resources = iw_event->ord;

	listen_id = cm_id->context;

	mutex_lock(&listen_id->handler_mutex);
	if (READ_ONCE(listen_id->state) != RDMA_CM_LISTEN)
		goto out;

	/* Create a new RDMA id for the new IW CM ID */
	conn_id = __rdma_create_id(listen_id->id.route.addr.dev_addr.net,
				   listen_id->id.event_handler,
				   listen_id->id.context, RDMA_PS_TCP,
				   IB_QPT_RC, listen_id);
	if (IS_ERR(conn_id)) {
		ret = -ENOMEM;
		goto out;
	}
	mutex_lock_nested(&conn_id->handler_mutex, SINGLE_DEPTH_NESTING);
	conn_id->state = RDMA_CM_CONNECT;

	ret = rdma_translate_ip(laddr, &conn_id->id.route.addr.dev_addr);
	if (ret) {
		mutex_unlock(&listen_id->handler_mutex);
		destroy_id_handler_unlock(conn_id);
		return ret;
	}

	ret = cma_iw_acquire_dev(conn_id, listen_id);
	if (ret) {
		mutex_unlock(&listen_id->handler_mutex);
		destroy_id_handler_unlock(conn_id);
		return ret;
	}

	conn_id->cm_id.iw = cm_id;
	cm_id->context = conn_id;
	cm_id->cm_handler = cma_iw_handler;

	memcpy(cma_src_addr(conn_id), laddr, rdma_addr_size(laddr));
	memcpy(cma_dst_addr(conn_id), raddr, rdma_addr_size(raddr));

	ret = cma_cm_event_handler(conn_id, &event);
	if (ret) {
		/* User wants to destroy the CM ID */
		conn_id->cm_id.iw = NULL;
		mutex_unlock(&listen_id->handler_mutex);
		destroy_id_handler_unlock(conn_id);
		return ret;
	}

	mutex_unlock(&conn_id->handler_mutex);

out:
	mutex_unlock(&listen_id->handler_mutex);
	return ret;
}

static int cma_ib_listen(struct rdma_id_private *id_priv)
{
	struct sockaddr *addr;
	struct ib_cm_id	*id;
	__be64 svc_id;

	addr = cma_src_addr(id_priv);
	svc_id = rdma_get_service_id(&id_priv->id, addr);
	id = ib_cm_insert_listen(id_priv->id.device,
				 cma_ib_req_handler, svc_id);
	if (IS_ERR(id))
		return PTR_ERR(id);
	id_priv->cm_id.ib = id;

	return 0;
}

static int cma_iw_listen(struct rdma_id_private *id_priv, int backlog)
{
	int ret;
	struct iw_cm_id	*id;

	id = iw_create_cm_id(id_priv->id.device,
			     iw_conn_req_handler,
			     id_priv);
	if (IS_ERR(id))
		return PTR_ERR(id);

	id->tos = id_priv->tos;
	id->tos_set = id_priv->tos_set;
	id->afonly = id_priv->afonly;
	id_priv->cm_id.iw = id;

	memcpy(&id_priv->cm_id.iw->local_addr, cma_src_addr(id_priv),
	       rdma_addr_size(cma_src_addr(id_priv)));

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

	/* Listening IDs are always destroyed on removal */
	if (event->event == RDMA_CM_EVENT_DEVICE_REMOVAL)
		return -1;

	id->context = id_priv->id.context;
	id->event_handler = id_priv->id.event_handler;
	trace_cm_event_handler(id_priv, event);
	return id_priv->id.event_handler(id, event);
}

static int cma_listen_on_dev(struct rdma_id_private *id_priv,
			     struct cma_device *cma_dev,
			     struct rdma_id_private **to_destroy)
{
	struct rdma_id_private *dev_id_priv;
	struct net *net = id_priv->id.route.addr.dev_addr.net;
	int ret;

	lockdep_assert_held(&lock);

	*to_destroy = NULL;
	if (cma_family(id_priv) == AF_IB && !rdma_cap_ib_cm(cma_dev->device, 1))
		return 0;

	dev_id_priv =
		__rdma_create_id(net, cma_listen_handler, id_priv,
				 id_priv->id.ps, id_priv->id.qp_type, id_priv);
	if (IS_ERR(dev_id_priv))
		return PTR_ERR(dev_id_priv);

	dev_id_priv->state = RDMA_CM_ADDR_BOUND;
	memcpy(cma_src_addr(dev_id_priv), cma_src_addr(id_priv),
	       rdma_addr_size(cma_src_addr(id_priv)));

	_cma_attach_to_dev(dev_id_priv, cma_dev);
	rdma_restrack_add(&dev_id_priv->res);
	cma_id_get(id_priv);
	dev_id_priv->internal_id = 1;
	dev_id_priv->afonly = id_priv->afonly;
	dev_id_priv->tos_set = id_priv->tos_set;
	dev_id_priv->tos = id_priv->tos;

	ret = rdma_listen(&dev_id_priv->id, id_priv->backlog);
	if (ret)
		goto err_listen;
	list_add_tail(&dev_id_priv->listen_list, &id_priv->listen_list);
	return 0;
err_listen:
	/* Caller must destroy this after releasing lock */
	*to_destroy = dev_id_priv;
	dev_warn(&cma_dev->device->dev, "RDMA CMA: %s, error %d\n", __func__, ret);
	return ret;
}

static int cma_listen_on_all(struct rdma_id_private *id_priv)
{
	struct rdma_id_private *to_destroy;
	struct cma_device *cma_dev;
	int ret;

	mutex_lock(&lock);
	list_add_tail(&id_priv->list, &listen_any_list);
	list_for_each_entry(cma_dev, &dev_list, list) {
		ret = cma_listen_on_dev(id_priv, cma_dev, &to_destroy);
		if (ret) {
			/* Prevent racing with cma_process_remove() */
			if (to_destroy)
				list_del_init(&to_destroy->list);
			goto err_listen;
		}
	}
	mutex_unlock(&lock);
	return 0;

err_listen:
	list_del(&id_priv->list);
	mutex_unlock(&lock);
	if (to_destroy)
		rdma_destroy_id(&to_destroy->id);
	return ret;
}

void rdma_set_service_type(struct rdma_cm_id *id, int tos)
{
	struct rdma_id_private *id_priv;

	id_priv = container_of(id, struct rdma_id_private, id);
	id_priv->tos = (u8) tos;
	id_priv->tos_set = true;
}
EXPORT_SYMBOL(rdma_set_service_type);

/**
 * rdma_set_ack_timeout() - Set the ack timeout of QP associated
 *                          with a connection identifier.
 * @id: Communication identifier to associated with service type.
 * @timeout: Ack timeout to set a QP, expressed as 4.096 * 2^(timeout) usec.
 *
 * This function should be called before rdma_connect() on active side,
 * and on passive side before rdma_accept(). It is applicable to primary
 * path only. The timeout will affect the local side of the QP, it is not
 * negotiated with remote side and zero disables the timer. In case it is
 * set before rdma_resolve_route, the value will also be used to determine
 * PacketLifeTime for RoCE.
 *
 * Return: 0 for success
 */
int rdma_set_ack_timeout(struct rdma_cm_id *id, u8 timeout)
{
	struct rdma_id_private *id_priv;

	if (id->qp_type != IB_QPT_RC)
		return -EINVAL;

	id_priv = container_of(id, struct rdma_id_private, id);
	id_priv->timeout = timeout;
	id_priv->timeout_set = true;

	return 0;
}
EXPORT_SYMBOL(rdma_set_ack_timeout);

/**
 * rdma_set_min_rnr_timer() - Set the minimum RNR Retry timer of the
 *			      QP associated with a connection identifier.
 * @id: Communication identifier to associated with service type.
 * @min_rnr_timer: 5-bit value encoded as Table 45: "Encoding for RNR NAK
 *		   Timer Field" in the IBTA specification.
 *
 * This function should be called before rdma_connect() on active
 * side, and on passive side before rdma_accept(). The timer value
 * will be associated with the local QP. When it receives a send it is
 * not read to handle, typically if the receive queue is empty, an RNR
 * Retry NAK is returned to the requester with the min_rnr_timer
 * encoded. The requester will then wait at least the time specified
 * in the NAK before retrying. The default is zero, which translates
 * to a minimum RNR Timer value of 655 ms.
 *
 * Return: 0 for success
 */
int rdma_set_min_rnr_timer(struct rdma_cm_id *id, u8 min_rnr_timer)
{
	struct rdma_id_private *id_priv;

	/* It is a five-bit value */
	if (min_rnr_timer & 0xe0)
		return -EINVAL;

	if (WARN_ON(id->qp_type != IB_QPT_RC && id->qp_type != IB_QPT_XRC_TGT))
		return -EINVAL;

	id_priv = container_of(id, struct rdma_id_private, id);
	id_priv->min_rnr_timer = min_rnr_timer;
	id_priv->min_rnr_timer_set = true;

	return 0;
}
EXPORT_SYMBOL(rdma_set_min_rnr_timer);

static void cma_query_handler(int status, struct sa_path_rec *path_rec,
			      void *context)
{
	struct cma_work *work = context;
	struct rdma_route *route;

	route = &work->id->id.route;

	if (!status) {
		route->num_paths = 1;
		*route->path_rec = *path_rec;
	} else {
		work->old_state = RDMA_CM_ROUTE_QUERY;
		work->new_state = RDMA_CM_ADDR_RESOLVED;
		work->event.event = RDMA_CM_EVENT_ROUTE_ERROR;
		work->event.status = status;
		pr_debug_ratelimited("RDMA CM: ROUTE_ERROR: failed to query path. status %d\n",
				     status);
	}

	queue_work(cma_wq, &work->work);
}

static int cma_query_ib_route(struct rdma_id_private *id_priv,
			      unsigned long timeout_ms, struct cma_work *work)
{
	struct rdma_dev_addr *dev_addr = &id_priv->id.route.addr.dev_addr;
	struct sa_path_rec path_rec;
	ib_sa_comp_mask comp_mask;
	struct sockaddr_in6 *sin6;
	struct sockaddr_ib *sib;

	memset(&path_rec, 0, sizeof path_rec);

	if (rdma_cap_opa_ah(id_priv->id.device, id_priv->id.port_num))
		path_rec.rec_type = SA_PATH_REC_TYPE_OPA;
	else
		path_rec.rec_type = SA_PATH_REC_TYPE_IB;
	rdma_addr_get_sgid(dev_addr, &path_rec.sgid);
	rdma_addr_get_dgid(dev_addr, &path_rec.dgid);
	path_rec.pkey = cpu_to_be16(ib_addr_get_pkey(dev_addr));
	path_rec.numb_path = 1;
	path_rec.reversible = 1;
	path_rec.service_id = rdma_get_service_id(&id_priv->id,
						  cma_dst_addr(id_priv));

	comp_mask = IB_SA_PATH_REC_DGID | IB_SA_PATH_REC_SGID |
		    IB_SA_PATH_REC_PKEY | IB_SA_PATH_REC_NUMB_PATH |
		    IB_SA_PATH_REC_REVERSIBLE | IB_SA_PATH_REC_SERVICE_ID;

	switch (cma_family(id_priv)) {
	case AF_INET:
		path_rec.qos_class = cpu_to_be16((u16) id_priv->tos);
		comp_mask |= IB_SA_PATH_REC_QOS_CLASS;
		break;
	case AF_INET6:
		sin6 = (struct sockaddr_in6 *) cma_src_addr(id_priv);
		path_rec.traffic_class = (u8) (be32_to_cpu(sin6->sin6_flowinfo) >> 20);
		comp_mask |= IB_SA_PATH_REC_TRAFFIC_CLASS;
		break;
	case AF_IB:
		sib = (struct sockaddr_ib *) cma_src_addr(id_priv);
		path_rec.traffic_class = (u8) (be32_to_cpu(sib->sib_flowinfo) >> 20);
		comp_mask |= IB_SA_PATH_REC_TRAFFIC_CLASS;
		break;
	}

	id_priv->query_id = ib_sa_path_rec_get(&sa_client, id_priv->id.device,
					       id_priv->id.port_num, &path_rec,
					       comp_mask, timeout_ms,
					       GFP_KERNEL, cma_query_handler,
					       work, &id_priv->query);

	return (id_priv->query_id < 0) ? id_priv->query_id : 0;
}

static void cma_iboe_join_work_handler(struct work_struct *work)
{
	struct cma_multicast *mc =
		container_of(work, struct cma_multicast, iboe_join.work);
	struct rdma_cm_event *event = &mc->iboe_join.event;
	struct rdma_id_private *id_priv = mc->id_priv;
	int ret;

	mutex_lock(&id_priv->handler_mutex);
	if (READ_ONCE(id_priv->state) == RDMA_CM_DESTROYING ||
	    READ_ONCE(id_priv->state) == RDMA_CM_DEVICE_REMOVAL)
		goto out_unlock;

	ret = cma_cm_event_handler(id_priv, event);
	WARN_ON(ret);

out_unlock:
	mutex_unlock(&id_priv->handler_mutex);
	if (event->event == RDMA_CM_EVENT_MULTICAST_JOIN)
		rdma_destroy_ah_attr(&event->param.ud.ah_attr);
}

static void cma_work_handler(struct work_struct *_work)
{
	struct cma_work *work = container_of(_work, struct cma_work, work);
	struct rdma_id_private *id_priv = work->id;

	mutex_lock(&id_priv->handler_mutex);
	if (READ_ONCE(id_priv->state) == RDMA_CM_DESTROYING ||
	    READ_ONCE(id_priv->state) == RDMA_CM_DEVICE_REMOVAL)
		goto out_unlock;
	if (work->old_state != 0 || work->new_state != 0) {
		if (!cma_comp_exch(id_priv, work->old_state, work->new_state))
			goto out_unlock;
	}

	if (cma_cm_event_handler(id_priv, &work->event)) {
		cma_id_put(id_priv);
		destroy_id_handler_unlock(id_priv);
		goto out_free;
	}

out_unlock:
	mutex_unlock(&id_priv->handler_mutex);
	cma_id_put(id_priv);
out_free:
	if (work->event.event == RDMA_CM_EVENT_MULTICAST_JOIN)
		rdma_destroy_ah_attr(&work->event.param.ud.ah_attr);
	kfree(work);
}

static void cma_init_resolve_route_work(struct cma_work *work,
					struct rdma_id_private *id_priv)
{
	work->id = id_priv;
	INIT_WORK(&work->work, cma_work_handler);
	work->old_state = RDMA_CM_ROUTE_QUERY;
	work->new_state = RDMA_CM_ROUTE_RESOLVED;
	work->event.event = RDMA_CM_EVENT_ROUTE_RESOLVED;
}

static void enqueue_resolve_addr_work(struct cma_work *work,
				      struct rdma_id_private *id_priv)
{
	/* Balances with cma_id_put() in cma_work_handler */
	cma_id_get(id_priv);

	work->id = id_priv;
	INIT_WORK(&work->work, cma_work_handler);
	work->old_state = RDMA_CM_ADDR_QUERY;
	work->new_state = RDMA_CM_ADDR_RESOLVED;
	work->event.event = RDMA_CM_EVENT_ADDR_RESOLVED;

	queue_work(cma_wq, &work->work);
}

static int cma_resolve_ib_route(struct rdma_id_private *id_priv,
				unsigned long timeout_ms)
{
	struct rdma_route *route = &id_priv->id.route;
	struct cma_work *work;
	int ret;

	work = kzalloc(sizeof *work, GFP_KERNEL);
	if (!work)
		return -ENOMEM;

	cma_init_resolve_route_work(work, id_priv);

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

static enum ib_gid_type cma_route_gid_type(enum rdma_network_type network_type,
					   unsigned long supported_gids,
					   enum ib_gid_type default_gid)
{
	if ((network_type == RDMA_NETWORK_IPV4 ||
	     network_type == RDMA_NETWORK_IPV6) &&
	    test_bit(IB_GID_TYPE_ROCE_UDP_ENCAP, &supported_gids))
		return IB_GID_TYPE_ROCE_UDP_ENCAP;

	return default_gid;
}

/*
 * cma_iboe_set_path_rec_l2_fields() is helper function which sets
 * path record type based on GID type.
 * It also sets up other L2 fields which includes destination mac address
 * netdev ifindex, of the path record.
 * It returns the netdev of the bound interface for this path record entry.
 */
static struct net_device *
cma_iboe_set_path_rec_l2_fields(struct rdma_id_private *id_priv)
{
	struct rdma_route *route = &id_priv->id.route;
	enum ib_gid_type gid_type = IB_GID_TYPE_ROCE;
	struct rdma_addr *addr = &route->addr;
	unsigned long supported_gids;
	struct net_device *ndev;

	if (!addr->dev_addr.bound_dev_if)
		return NULL;

	ndev = dev_get_by_index(addr->dev_addr.net,
				addr->dev_addr.bound_dev_if);
	if (!ndev)
		return NULL;

	supported_gids = roce_gid_type_mask_support(id_priv->id.device,
						    id_priv->id.port_num);
	gid_type = cma_route_gid_type(addr->dev_addr.network,
				      supported_gids,
				      id_priv->gid_type);
	/* Use the hint from IP Stack to select GID Type */
	if (gid_type < ib_network_to_gid_type(addr->dev_addr.network))
		gid_type = ib_network_to_gid_type(addr->dev_addr.network);
	route->path_rec->rec_type = sa_conv_gid_to_pathrec_type(gid_type);

	route->path_rec->roce.route_resolved = true;
	sa_path_set_dmac(route->path_rec, addr->dev_addr.dst_dev_addr);
	return ndev;
}

int rdma_set_ib_path(struct rdma_cm_id *id,
		     struct sa_path_rec *path_rec)
{
	struct rdma_id_private *id_priv;
	struct net_device *ndev;
	int ret;

	id_priv = container_of(id, struct rdma_id_private, id);
	if (!cma_comp_exch(id_priv, RDMA_CM_ADDR_RESOLVED,
			   RDMA_CM_ROUTE_RESOLVED))
		return -EINVAL;

	id->route.path_rec = kmemdup(path_rec, sizeof(*path_rec),
				     GFP_KERNEL);
	if (!id->route.path_rec) {
		ret = -ENOMEM;
		goto err;
	}

	if (rdma_protocol_roce(id->device, id->port_num)) {
		ndev = cma_iboe_set_path_rec_l2_fields(id_priv);
		if (!ndev) {
			ret = -ENODEV;
			goto err_free;
		}
		dev_put(ndev);
	}

	id->route.num_paths = 1;
	return 0;

err_free:
	kfree(id->route.path_rec);
	id->route.path_rec = NULL;
err:
	cma_comp_exch(id_priv, RDMA_CM_ROUTE_RESOLVED, RDMA_CM_ADDR_RESOLVED);
	return ret;
}
EXPORT_SYMBOL(rdma_set_ib_path);

static int cma_resolve_iw_route(struct rdma_id_private *id_priv)
{
	struct cma_work *work;

	work = kzalloc(sizeof *work, GFP_KERNEL);
	if (!work)
		return -ENOMEM;

	cma_init_resolve_route_work(work, id_priv);
	queue_work(cma_wq, &work->work);
	return 0;
}

static int get_vlan_ndev_tc(struct net_device *vlan_ndev, int prio)
{
	struct net_device *dev;

	dev = vlan_dev_real_dev(vlan_ndev);
	if (dev->num_tc)
		return netdev_get_prio_tc_map(dev, prio);

	return (vlan_dev_get_egress_qos_mask(vlan_ndev, prio) &
		VLAN_PRIO_MASK) >> VLAN_PRIO_SHIFT;
}

struct iboe_prio_tc_map {
	int input_prio;
	int output_tc;
	bool found;
};

static int get_lower_vlan_dev_tc(struct net_device *dev,
				 struct netdev_nested_priv *priv)
{
	struct iboe_prio_tc_map *map = (struct iboe_prio_tc_map *)priv->data;

	if (is_vlan_dev(dev))
		map->output_tc = get_vlan_ndev_tc(dev, map->input_prio);
	else if (dev->num_tc)
		map->output_tc = netdev_get_prio_tc_map(dev, map->input_prio);
	else
		map->output_tc = 0;
	/* We are interested only in first level VLAN device, so always
	 * return 1 to stop iterating over next level devices.
	 */
	map->found = true;
	return 1;
}

static int iboe_tos_to_sl(struct net_device *ndev, int tos)
{
	struct iboe_prio_tc_map prio_tc_map = {};
	int prio = rt_tos2priority(tos);
	struct netdev_nested_priv priv;

	/* If VLAN device, get it directly from the VLAN netdev */
	if (is_vlan_dev(ndev))
		return get_vlan_ndev_tc(ndev, prio);

	prio_tc_map.input_prio = prio;
	priv.data = (void *)&prio_tc_map;
	rcu_read_lock();
	netdev_walk_all_lower_dev_rcu(ndev,
				      get_lower_vlan_dev_tc,
				      &priv);
	rcu_read_unlock();
	/* If map is found from lower device, use it; Otherwise
	 * continue with the current netdevice to get priority to tc map.
	 */
	if (prio_tc_map.found)
		return prio_tc_map.output_tc;
	else if (ndev->num_tc)
		return netdev_get_prio_tc_map(ndev, prio);
	else
		return 0;
}

static __be32 cma_get_roce_udp_flow_label(struct rdma_id_private *id_priv)
{
	struct sockaddr_in6 *addr6;
	u16 dport, sport;
	u32 hash, fl;

	addr6 = (struct sockaddr_in6 *)cma_src_addr(id_priv);
	fl = be32_to_cpu(addr6->sin6_flowinfo) & IB_GRH_FLOWLABEL_MASK;
	if ((cma_family(id_priv) != AF_INET6) || !fl) {
		dport = be16_to_cpu(cma_port(cma_dst_addr(id_priv)));
		sport = be16_to_cpu(cma_port(cma_src_addr(id_priv)));
		hash = (u32)sport * 31 + dport;
		fl = hash & IB_GRH_FLOWLABEL_MASK;
	}

	return cpu_to_be32(fl);
}

static int cma_resolve_iboe_route(struct rdma_id_private *id_priv)
{
	struct rdma_route *route = &id_priv->id.route;
	struct rdma_addr *addr = &route->addr;
	struct cma_work *work;
	int ret;
	struct net_device *ndev;

	u8 default_roce_tos = id_priv->cma_dev->default_roce_tos[id_priv->id.port_num -
					rdma_start_port(id_priv->cma_dev->device)];
	u8 tos = id_priv->tos_set ? id_priv->tos : default_roce_tos;


	work = kzalloc(sizeof *work, GFP_KERNEL);
	if (!work)
		return -ENOMEM;

	route->path_rec = kzalloc(sizeof *route->path_rec, GFP_KERNEL);
	if (!route->path_rec) {
		ret = -ENOMEM;
		goto err1;
	}

	route->num_paths = 1;

	ndev = cma_iboe_set_path_rec_l2_fields(id_priv);
	if (!ndev) {
		ret = -ENODEV;
		goto err2;
	}

	rdma_ip2gid((struct sockaddr *)&id_priv->id.route.addr.src_addr,
		    &route->path_rec->sgid);
	rdma_ip2gid((struct sockaddr *)&id_priv->id.route.addr.dst_addr,
		    &route->path_rec->dgid);

	if (((struct sockaddr *)&id_priv->id.route.addr.dst_addr)->sa_family != AF_IB)
		/* TODO: get the hoplimit from the inet/inet6 device */
		route->path_rec->hop_limit = addr->dev_addr.hoplimit;
	else
		route->path_rec->hop_limit = 1;
	route->path_rec->reversible = 1;
	route->path_rec->pkey = cpu_to_be16(0xffff);
	route->path_rec->mtu_selector = IB_SA_EQ;
	route->path_rec->sl = iboe_tos_to_sl(ndev, tos);
	route->path_rec->traffic_class = tos;
	route->path_rec->mtu = iboe_get_mtu(ndev->mtu);
	route->path_rec->rate_selector = IB_SA_EQ;
	route->path_rec->rate = iboe_get_rate(ndev);
	dev_put(ndev);
	route->path_rec->packet_life_time_selector = IB_SA_EQ;
	/* In case ACK timeout is set, use this value to calculate
	 * PacketLifeTime.  As per IBTA 12.7.34,
	 * local ACK timeout = (2 * PacketLifeTime + Local CA’s ACK delay).
	 * Assuming a negligible local ACK delay, we can use
	 * PacketLifeTime = local ACK timeout/2
	 * as a reasonable approximation for RoCE networks.
	 */
	route->path_rec->packet_life_time = id_priv->timeout_set ?
		id_priv->timeout - 1 : CMA_IBOE_PACKET_LIFETIME;

	if (!route->path_rec->mtu) {
		ret = -EINVAL;
		goto err2;
	}

	if (rdma_protocol_roce_udp_encap(id_priv->id.device,
					 id_priv->id.port_num))
		route->path_rec->flow_label =
			cma_get_roce_udp_flow_label(id_priv);

	cma_init_resolve_route_work(work, id_priv);
	queue_work(cma_wq, &work->work);

	return 0;

err2:
	kfree(route->path_rec);
	route->path_rec = NULL;
	route->num_paths = 0;
err1:
	kfree(work);
	return ret;
}

int rdma_resolve_route(struct rdma_cm_id *id, unsigned long timeout_ms)
{
	struct rdma_id_private *id_priv;
	int ret;

	id_priv = container_of(id, struct rdma_id_private, id);
	if (!cma_comp_exch(id_priv, RDMA_CM_ADDR_RESOLVED, RDMA_CM_ROUTE_QUERY))
		return -EINVAL;

	cma_id_get(id_priv);
	if (rdma_cap_ib_sa(id->device, id->port_num))
		ret = cma_resolve_ib_route(id_priv, timeout_ms);
	else if (rdma_protocol_roce(id->device, id->port_num))
		ret = cma_resolve_iboe_route(id_priv);
	else if (rdma_protocol_iwarp(id->device, id->port_num))
		ret = cma_resolve_iw_route(id_priv);
	else
		ret = -ENOSYS;

	if (ret)
		goto err;

	return 0;
err:
	cma_comp_exch(id_priv, RDMA_CM_ROUTE_QUERY, RDMA_CM_ADDR_RESOLVED);
	cma_id_put(id_priv);
	return ret;
}
EXPORT_SYMBOL(rdma_resolve_route);

static void cma_set_loopback(struct sockaddr *addr)
{
	switch (addr->sa_family) {
	case AF_INET:
		((struct sockaddr_in *) addr)->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		break;
	case AF_INET6:
		ipv6_addr_set(&((struct sockaddr_in6 *) addr)->sin6_addr,
			      0, 0, 0, htonl(1));
		break;
	default:
		ib_addr_set(&((struct sockaddr_ib *) addr)->sib_addr,
			    0, 0, 0, htonl(1));
		break;
	}
}

static int cma_bind_loopback(struct rdma_id_private *id_priv)
{
	struct cma_device *cma_dev, *cur_dev;
	union ib_gid gid;
	enum ib_port_state port_state;
	unsigned int p;
	u16 pkey;
	int ret;

	cma_dev = NULL;
	mutex_lock(&lock);
	list_for_each_entry(cur_dev, &dev_list, list) {
		if (cma_family(id_priv) == AF_IB &&
		    !rdma_cap_ib_cm(cur_dev->device, 1))
			continue;

		if (!cma_dev)
			cma_dev = cur_dev;

		rdma_for_each_port (cur_dev->device, p) {
			if (!ib_get_cached_port_state(cur_dev->device, p, &port_state) &&
			    port_state == IB_PORT_ACTIVE) {
				cma_dev = cur_dev;
				goto port_found;
			}
		}
	}

	if (!cma_dev) {
		ret = -ENODEV;
		goto out;
	}

	p = 1;

port_found:
	ret = rdma_query_gid(cma_dev->device, p, 0, &gid);
	if (ret)
		goto out;

	ret = ib_get_cached_pkey(cma_dev->device, p, 0, &pkey);
	if (ret)
		goto out;

	id_priv->id.route.addr.dev_addr.dev_type =
		(rdma_protocol_ib(cma_dev->device, p)) ?
		ARPHRD_INFINIBAND : ARPHRD_ETHER;

	rdma_addr_set_sgid(&id_priv->id.route.addr.dev_addr, &gid);
	ib_addr_set_pkey(&id_priv->id.route.addr.dev_addr, pkey);
	id_priv->id.port_num = p;
	cma_attach_to_dev(id_priv, cma_dev);
	rdma_restrack_add(&id_priv->res);
	cma_set_loopback(cma_src_addr(id_priv));
out:
	mutex_unlock(&lock);
	return ret;
}

static void addr_handler(int status, struct sockaddr *src_addr,
			 struct rdma_dev_addr *dev_addr, void *context)
{
	struct rdma_id_private *id_priv = context;
	struct rdma_cm_event event = {};
	struct sockaddr *addr;
	struct sockaddr_storage old_addr;

	mutex_lock(&id_priv->handler_mutex);
	if (!cma_comp_exch(id_priv, RDMA_CM_ADDR_QUERY,
			   RDMA_CM_ADDR_RESOLVED))
		goto out;

	/*
	 * Store the previous src address, so that if we fail to acquire
	 * matching rdma device, old address can be restored back, which helps
	 * to cancel the cma listen operation correctly.
	 */
	addr = cma_src_addr(id_priv);
	memcpy(&old_addr, addr, rdma_addr_size(addr));
	memcpy(addr, src_addr, rdma_addr_size(src_addr));
	if (!status && !id_priv->cma_dev) {
		status = cma_acquire_dev_by_src_ip(id_priv);
		if (status)
			pr_debug_ratelimited("RDMA CM: ADDR_ERROR: failed to acquire device. status %d\n",
					     status);
		rdma_restrack_add(&id_priv->res);
	} else if (status) {
		pr_debug_ratelimited("RDMA CM: ADDR_ERROR: failed to resolve IP. status %d\n", status);
	}

	if (status) {
		memcpy(addr, &old_addr,
		       rdma_addr_size((struct sockaddr *)&old_addr));
		if (!cma_comp_exch(id_priv, RDMA_CM_ADDR_RESOLVED,
				   RDMA_CM_ADDR_BOUND))
			goto out;
		event.event = RDMA_CM_EVENT_ADDR_ERROR;
		event.status = status;
	} else
		event.event = RDMA_CM_EVENT_ADDR_RESOLVED;

	if (cma_cm_event_handler(id_priv, &event)) {
		destroy_id_handler_unlock(id_priv);
		return;
	}
out:
	mutex_unlock(&id_priv->handler_mutex);
}

static int cma_resolve_loopback(struct rdma_id_private *id_priv)
{
	struct cma_work *work;
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

	rdma_addr_get_sgid(&id_priv->id.route.addr.dev_addr, &gid);
	rdma_addr_set_dgid(&id_priv->id.route.addr.dev_addr, &gid);

	enqueue_resolve_addr_work(work, id_priv);
	return 0;
err:
	kfree(work);
	return ret;
}

static int cma_resolve_ib_addr(struct rdma_id_private *id_priv)
{
	struct cma_work *work;
	int ret;

	work = kzalloc(sizeof *work, GFP_KERNEL);
	if (!work)
		return -ENOMEM;

	if (!id_priv->cma_dev) {
		ret = cma_resolve_ib_dev(id_priv);
		if (ret)
			goto err;
	}

	rdma_addr_set_dgid(&id_priv->id.route.addr.dev_addr, (union ib_gid *)
		&(((struct sockaddr_ib *) &id_priv->id.route.addr.dst_addr)->sib_addr));

	enqueue_resolve_addr_work(work, id_priv);
	return 0;
err:
	kfree(work);
	return ret;
}

static int cma_bind_addr(struct rdma_cm_id *id, struct sockaddr *src_addr,
			 const struct sockaddr *dst_addr)
{
	if (!src_addr || !src_addr->sa_family) {
		src_addr = (struct sockaddr *) &id->route.addr.src_addr;
		src_addr->sa_family = dst_addr->sa_family;
		if (IS_ENABLED(CONFIG_IPV6) &&
		    dst_addr->sa_family == AF_INET6) {
			struct sockaddr_in6 *src_addr6 = (struct sockaddr_in6 *) src_addr;
			struct sockaddr_in6 *dst_addr6 = (struct sockaddr_in6 *) dst_addr;
			src_addr6->sin6_scope_id = dst_addr6->sin6_scope_id;
			if (ipv6_addr_type(&dst_addr6->sin6_addr) & IPV6_ADDR_LINKLOCAL)
				id->route.addr.dev_addr.bound_dev_if = dst_addr6->sin6_scope_id;
		} else if (dst_addr->sa_family == AF_IB) {
			((struct sockaddr_ib *) src_addr)->sib_pkey =
				((struct sockaddr_ib *) dst_addr)->sib_pkey;
		}
	}
	return rdma_bind_addr(id, src_addr);
}

/*
 * If required, resolve the source address for bind and leave the id_priv in
 * state RDMA_CM_ADDR_BOUND. This oddly uses the state to determine the prior
 * calls made by ULP, a previously bound ID will not be re-bound and src_addr is
 * ignored.
 */
static int resolve_prepare_src(struct rdma_id_private *id_priv,
			       struct sockaddr *src_addr,
			       const struct sockaddr *dst_addr)
{
	int ret;

	memcpy(cma_dst_addr(id_priv), dst_addr, rdma_addr_size(dst_addr));
	if (!cma_comp_exch(id_priv, RDMA_CM_ADDR_BOUND, RDMA_CM_ADDR_QUERY)) {
		/* For a well behaved ULP state will be RDMA_CM_IDLE */
		ret = cma_bind_addr(&id_priv->id, src_addr, dst_addr);
		if (ret)
			goto err_dst;
		if (WARN_ON(!cma_comp_exch(id_priv, RDMA_CM_ADDR_BOUND,
					   RDMA_CM_ADDR_QUERY))) {
			ret = -EINVAL;
			goto err_dst;
		}
	}

	if (cma_family(id_priv) != dst_addr->sa_family) {
		ret = -EINVAL;
		goto err_state;
	}
	return 0;

err_state:
	cma_comp_exch(id_priv, RDMA_CM_ADDR_QUERY, RDMA_CM_ADDR_BOUND);
err_dst:
	memset(cma_dst_addr(id_priv), 0, rdma_addr_size(dst_addr));
	return ret;
}

int rdma_resolve_addr(struct rdma_cm_id *id, struct sockaddr *src_addr,
		      const struct sockaddr *dst_addr, unsigned long timeout_ms)
{
	struct rdma_id_private *id_priv =
		container_of(id, struct rdma_id_private, id);
	int ret;

	ret = resolve_prepare_src(id_priv, src_addr, dst_addr);
	if (ret)
		return ret;

	if (cma_any_addr(dst_addr)) {
		ret = cma_resolve_loopback(id_priv);
	} else {
		if (dst_addr->sa_family == AF_IB) {
			ret = cma_resolve_ib_addr(id_priv);
		} else {
			ret = rdma_resolve_ip(cma_src_addr(id_priv), dst_addr,
					      &id->route.addr.dev_addr,
					      timeout_ms, addr_handler,
					      false, id_priv);
		}
	}
	if (ret)
		goto err;

	return 0;
err:
	cma_comp_exch(id_priv, RDMA_CM_ADDR_QUERY, RDMA_CM_ADDR_BOUND);
	return ret;
}
EXPORT_SYMBOL(rdma_resolve_addr);

int rdma_set_reuseaddr(struct rdma_cm_id *id, int reuse)
{
	struct rdma_id_private *id_priv;
	unsigned long flags;
	int ret;

	id_priv = container_of(id, struct rdma_id_private, id);
	spin_lock_irqsave(&id_priv->lock, flags);
	if ((reuse && id_priv->state != RDMA_CM_LISTEN) ||
	    id_priv->state == RDMA_CM_IDLE) {
		id_priv->reuseaddr = reuse;
		ret = 0;
	} else {
		ret = -EINVAL;
	}
	spin_unlock_irqrestore(&id_priv->lock, flags);
	return ret;
}
EXPORT_SYMBOL(rdma_set_reuseaddr);

int rdma_set_afonly(struct rdma_cm_id *id, int afonly)
{
	struct rdma_id_private *id_priv;
	unsigned long flags;
	int ret;

	id_priv = container_of(id, struct rdma_id_private, id);
	spin_lock_irqsave(&id_priv->lock, flags);
	if (id_priv->state == RDMA_CM_IDLE || id_priv->state == RDMA_CM_ADDR_BOUND) {
		id_priv->options |= (1 << CMA_OPTION_AFONLY);
		id_priv->afonly = afonly;
		ret = 0;
	} else {
		ret = -EINVAL;
	}
	spin_unlock_irqrestore(&id_priv->lock, flags);
	return ret;
}
EXPORT_SYMBOL(rdma_set_afonly);

static void cma_bind_port(struct rdma_bind_list *bind_list,
			  struct rdma_id_private *id_priv)
{
	struct sockaddr *addr;
	struct sockaddr_ib *sib;
	u64 sid, mask;
	__be16 port;

	lockdep_assert_held(&lock);

	addr = cma_src_addr(id_priv);
	port = htons(bind_list->port);

	switch (addr->sa_family) {
	case AF_INET:
		((struct sockaddr_in *) addr)->sin_port = port;
		break;
	case AF_INET6:
		((struct sockaddr_in6 *) addr)->sin6_port = port;
		break;
	case AF_IB:
		sib = (struct sockaddr_ib *) addr;
		sid = be64_to_cpu(sib->sib_sid);
		mask = be64_to_cpu(sib->sib_sid_mask);
		sib->sib_sid = cpu_to_be64((sid & mask) | (u64) ntohs(port));
		sib->sib_sid_mask = cpu_to_be64(~0ULL);
		break;
	}
	id_priv->bind_list = bind_list;
	hlist_add_head(&id_priv->node, &bind_list->owners);
}

static int cma_alloc_port(enum rdma_ucm_port_space ps,
			  struct rdma_id_private *id_priv, unsigned short snum)
{
	struct rdma_bind_list *bind_list;
	int ret;

	lockdep_assert_held(&lock);

	bind_list = kzalloc(sizeof *bind_list, GFP_KERNEL);
	if (!bind_list)
		return -ENOMEM;

	ret = cma_ps_alloc(id_priv->id.route.addr.dev_addr.net, ps, bind_list,
			   snum);
	if (ret < 0)
		goto err;

	bind_list->ps = ps;
	bind_list->port = snum;
	cma_bind_port(bind_list, id_priv);
	return 0;
err:
	kfree(bind_list);
	return ret == -ENOSPC ? -EADDRNOTAVAIL : ret;
}

static int cma_port_is_unique(struct rdma_bind_list *bind_list,
			      struct rdma_id_private *id_priv)
{
	struct rdma_id_private *cur_id;
	struct sockaddr  *daddr = cma_dst_addr(id_priv);
	struct sockaddr  *saddr = cma_src_addr(id_priv);
	__be16 dport = cma_port(daddr);

	lockdep_assert_held(&lock);

	hlist_for_each_entry(cur_id, &bind_list->owners, node) {
		struct sockaddr  *cur_daddr = cma_dst_addr(cur_id);
		struct sockaddr  *cur_saddr = cma_src_addr(cur_id);
		__be16 cur_dport = cma_port(cur_daddr);

		if (id_priv == cur_id)
			continue;

		/* different dest port -> unique */
		if (!cma_any_port(daddr) &&
		    !cma_any_port(cur_daddr) &&
		    (dport != cur_dport))
			continue;

		/* different src address -> unique */
		if (!cma_any_addr(saddr) &&
		    !cma_any_addr(cur_saddr) &&
		    cma_addr_cmp(saddr, cur_saddr))
			continue;

		/* different dst address -> unique */
		if (!cma_any_addr(daddr) &&
		    !cma_any_addr(cur_daddr) &&
		    cma_addr_cmp(daddr, cur_daddr))
			continue;

		return -EADDRNOTAVAIL;
	}
	return 0;
}

static int cma_alloc_any_port(enum rdma_ucm_port_space ps,
			      struct rdma_id_private *id_priv)
{
	static unsigned int last_used_port;
	int low, high, remaining;
	unsigned int rover;
	struct net *net = id_priv->id.route.addr.dev_addr.net;

	lockdep_assert_held(&lock);

	inet_get_local_port_range(net, &low, &high);
	remaining = (high - low) + 1;
	rover = prandom_u32() % remaining + low;
retry:
	if (last_used_port != rover) {
		struct rdma_bind_list *bind_list;
		int ret;

		bind_list = cma_ps_find(net, ps, (unsigned short)rover);

		if (!bind_list) {
			ret = cma_alloc_port(ps, id_priv, rover);
		} else {
			ret = cma_port_is_unique(bind_list, id_priv);
			if (!ret)
				cma_bind_port(bind_list, id_priv);
		}
		/*
		 * Remember previously used port number in order to avoid
		 * re-using same port immediately after it is closed.
		 */
		if (!ret)
			last_used_port = rover;
		if (ret != -EADDRNOTAVAIL)
			return ret;
	}
	if (--remaining) {
		rover++;
		if ((rover < low) || (rover > high))
			rover = low;
		goto retry;
	}
	return -EADDRNOTAVAIL;
}

/*
 * Check that the requested port is available.  This is called when trying to
 * bind to a specific port, or when trying to listen on a bound port.  In
 * the latter case, the provided id_priv may already be on the bind_list, but
 * we still need to check that it's okay to start listening.
 */
static int cma_check_port(struct rdma_bind_list *bind_list,
			  struct rdma_id_private *id_priv, uint8_t reuseaddr)
{
	struct rdma_id_private *cur_id;
	struct sockaddr *addr, *cur_addr;

	lockdep_assert_held(&lock);

	addr = cma_src_addr(id_priv);
	hlist_for_each_entry(cur_id, &bind_list->owners, node) {
		if (id_priv == cur_id)
			continue;

		if (reuseaddr && cur_id->reuseaddr)
			continue;

		cur_addr = cma_src_addr(cur_id);
		if (id_priv->afonly && cur_id->afonly &&
		    (addr->sa_family != cur_addr->sa_family))
			continue;

		if (cma_any_addr(addr) || cma_any_addr(cur_addr))
			return -EADDRNOTAVAIL;

		if (!cma_addr_cmp(addr, cur_addr))
			return -EADDRINUSE;
	}
	return 0;
}

static int cma_use_port(enum rdma_ucm_port_space ps,
			struct rdma_id_private *id_priv)
{
	struct rdma_bind_list *bind_list;
	unsigned short snum;
	int ret;

	lockdep_assert_held(&lock);

	snum = ntohs(cma_port(cma_src_addr(id_priv)));
	if (snum < PROT_SOCK && !capable(CAP_NET_BIND_SERVICE))
		return -EACCES;

	bind_list = cma_ps_find(id_priv->id.route.addr.dev_addr.net, ps, snum);
	if (!bind_list) {
		ret = cma_alloc_port(ps, id_priv, snum);
	} else {
		ret = cma_check_port(bind_list, id_priv, id_priv->reuseaddr);
		if (!ret)
			cma_bind_port(bind_list, id_priv);
	}
	return ret;
}

static enum rdma_ucm_port_space
cma_select_inet_ps(struct rdma_id_private *id_priv)
{
	switch (id_priv->id.ps) {
	case RDMA_PS_TCP:
	case RDMA_PS_UDP:
	case RDMA_PS_IPOIB:
	case RDMA_PS_IB:
		return id_priv->id.ps;
	default:

		return 0;
	}
}

static enum rdma_ucm_port_space
cma_select_ib_ps(struct rdma_id_private *id_priv)
{
	enum rdma_ucm_port_space ps = 0;
	struct sockaddr_ib *sib;
	u64 sid_ps, mask, sid;

	sib = (struct sockaddr_ib *) cma_src_addr(id_priv);
	mask = be64_to_cpu(sib->sib_sid_mask) & RDMA_IB_IP_PS_MASK;
	sid = be64_to_cpu(sib->sib_sid) & mask;

	if ((id_priv->id.ps == RDMA_PS_IB) && (sid == (RDMA_IB_IP_PS_IB & mask))) {
		sid_ps = RDMA_IB_IP_PS_IB;
		ps = RDMA_PS_IB;
	} else if (((id_priv->id.ps == RDMA_PS_IB) || (id_priv->id.ps == RDMA_PS_TCP)) &&
		   (sid == (RDMA_IB_IP_PS_TCP & mask))) {
		sid_ps = RDMA_IB_IP_PS_TCP;
		ps = RDMA_PS_TCP;
	} else if (((id_priv->id.ps == RDMA_PS_IB) || (id_priv->id.ps == RDMA_PS_UDP)) &&
		   (sid == (RDMA_IB_IP_PS_UDP & mask))) {
		sid_ps = RDMA_IB_IP_PS_UDP;
		ps = RDMA_PS_UDP;
	}

	if (ps) {
		sib->sib_sid = cpu_to_be64(sid_ps | ntohs(cma_port((struct sockaddr *) sib)));
		sib->sib_sid_mask = cpu_to_be64(RDMA_IB_IP_PS_MASK |
						be64_to_cpu(sib->sib_sid_mask));
	}
	return ps;
}

static int cma_get_port(struct rdma_id_private *id_priv)
{
	enum rdma_ucm_port_space ps;
	int ret;

	if (cma_family(id_priv) != AF_IB)
		ps = cma_select_inet_ps(id_priv);
	else
		ps = cma_select_ib_ps(id_priv);
	if (!ps)
		return -EPROTONOSUPPORT;

	mutex_lock(&lock);
	if (cma_any_port(cma_src_addr(id_priv)))
		ret = cma_alloc_any_port(ps, id_priv);
	else
		ret = cma_use_port(ps, id_priv);
	mutex_unlock(&lock);

	return ret;
}

static int cma_check_linklocal(struct rdma_dev_addr *dev_addr,
			       struct sockaddr *addr)
{
#if IS_ENABLED(CONFIG_IPV6)
	struct sockaddr_in6 *sin6;

	if (addr->sa_family != AF_INET6)
		return 0;

	sin6 = (struct sockaddr_in6 *) addr;

	if (!(ipv6_addr_type(&sin6->sin6_addr) & IPV6_ADDR_LINKLOCAL))
		return 0;

	if (!sin6->sin6_scope_id)
			return -EINVAL;

	dev_addr->bound_dev_if = sin6->sin6_scope_id;
#endif
	return 0;
}

int rdma_listen(struct rdma_cm_id *id, int backlog)
{
	struct rdma_id_private *id_priv =
		container_of(id, struct rdma_id_private, id);
	int ret;

	if (!cma_comp_exch(id_priv, RDMA_CM_ADDR_BOUND, RDMA_CM_LISTEN)) {
		/* For a well behaved ULP state will be RDMA_CM_IDLE */
		id->route.addr.src_addr.ss_family = AF_INET;
		ret = rdma_bind_addr(id, cma_src_addr(id_priv));
		if (ret)
			return ret;
		if (WARN_ON(!cma_comp_exch(id_priv, RDMA_CM_ADDR_BOUND,
					   RDMA_CM_LISTEN)))
			return -EINVAL;
	}

	/*
	 * Once the ID reaches RDMA_CM_LISTEN it is not allowed to be reusable
	 * any more, and has to be unique in the bind list.
	 */
	if (id_priv->reuseaddr) {
		mutex_lock(&lock);
		ret = cma_check_port(id_priv->bind_list, id_priv, 0);
		if (!ret)
			id_priv->reuseaddr = 0;
		mutex_unlock(&lock);
		if (ret)
			goto err;
	}

	id_priv->backlog = backlog;
	if (id->device) {
		if (rdma_cap_ib_cm(id->device, 1)) {
			ret = cma_ib_listen(id_priv);
			if (ret)
				goto err;
		} else if (rdma_cap_iw_cm(id->device, 1)) {
			ret = cma_iw_listen(id_priv, backlog);
			if (ret)
				goto err;
		} else {
			ret = -ENOSYS;
			goto err;
		}
	} else {
		ret = cma_listen_on_all(id_priv);
		if (ret)
			goto err;
	}

	return 0;
err:
	id_priv->backlog = 0;
	/*
	 * All the failure paths that lead here will not allow the req_handler's
	 * to have run.
	 */
	cma_comp_exch(id_priv, RDMA_CM_LISTEN, RDMA_CM_ADDR_BOUND);
	return ret;
}
EXPORT_SYMBOL(rdma_listen);

int rdma_bind_addr(struct rdma_cm_id *id, struct sockaddr *addr)
{
	struct rdma_id_private *id_priv;
	int ret;
	struct sockaddr  *daddr;

	if (addr->sa_family != AF_INET && addr->sa_family != AF_INET6 &&
	    addr->sa_family != AF_IB)
		return -EAFNOSUPPORT;

	id_priv = container_of(id, struct rdma_id_private, id);
	if (!cma_comp_exch(id_priv, RDMA_CM_IDLE, RDMA_CM_ADDR_BOUND))
		return -EINVAL;

	ret = cma_check_linklocal(&id->route.addr.dev_addr, addr);
	if (ret)
		goto err1;

	memcpy(cma_src_addr(id_priv), addr, rdma_addr_size(addr));
	if (!cma_any_addr(addr)) {
		ret = cma_translate_addr(addr, &id->route.addr.dev_addr);
		if (ret)
			goto err1;

		ret = cma_acquire_dev_by_src_ip(id_priv);
		if (ret)
			goto err1;
	}

	if (!(id_priv->options & (1 << CMA_OPTION_AFONLY))) {
		if (addr->sa_family == AF_INET)
			id_priv->afonly = 1;
#if IS_ENABLED(CONFIG_IPV6)
		else if (addr->sa_family == AF_INET6) {
			struct net *net = id_priv->id.route.addr.dev_addr.net;

			id_priv->afonly = net->ipv6.sysctl.bindv6only;
		}
#endif
	}
	daddr = cma_dst_addr(id_priv);
	daddr->sa_family = addr->sa_family;

	ret = cma_get_port(id_priv);
	if (ret)
		goto err2;

	if (!cma_any_addr(addr))
		rdma_restrack_add(&id_priv->res);
	return 0;
err2:
	if (id_priv->cma_dev)
		cma_release_dev(id_priv);
err1:
	cma_comp_exch(id_priv, RDMA_CM_ADDR_BOUND, RDMA_CM_IDLE);
	return ret;
}
EXPORT_SYMBOL(rdma_bind_addr);

static int cma_format_hdr(void *hdr, struct rdma_id_private *id_priv)
{
	struct cma_hdr *cma_hdr;

	cma_hdr = hdr;
	cma_hdr->cma_version = CMA_VERSION;
	if (cma_family(id_priv) == AF_INET) {
		struct sockaddr_in *src4, *dst4;

		src4 = (struct sockaddr_in *) cma_src_addr(id_priv);
		dst4 = (struct sockaddr_in *) cma_dst_addr(id_priv);

		cma_set_ip_ver(cma_hdr, 4);
		cma_hdr->src_addr.ip4.addr = src4->sin_addr.s_addr;
		cma_hdr->dst_addr.ip4.addr = dst4->sin_addr.s_addr;
		cma_hdr->port = src4->sin_port;
	} else if (cma_family(id_priv) == AF_INET6) {
		struct sockaddr_in6 *src6, *dst6;

		src6 = (struct sockaddr_in6 *) cma_src_addr(id_priv);
		dst6 = (struct sockaddr_in6 *) cma_dst_addr(id_priv);

		cma_set_ip_ver(cma_hdr, 6);
		cma_hdr->src_addr.ip6 = src6->sin6_addr;
		cma_hdr->dst_addr.ip6 = dst6->sin6_addr;
		cma_hdr->port = src6->sin6_port;
	}
	return 0;
}

static int cma_sidr_rep_handler(struct ib_cm_id *cm_id,
				const struct ib_cm_event *ib_event)
{
	struct rdma_id_private *id_priv = cm_id->context;
	struct rdma_cm_event event = {};
	const struct ib_cm_sidr_rep_event_param *rep =
				&ib_event->param.sidr_rep_rcvd;
	int ret;

	mutex_lock(&id_priv->handler_mutex);
	if (READ_ONCE(id_priv->state) != RDMA_CM_CONNECT)
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
			pr_debug_ratelimited("RDMA CM: UNREACHABLE: bad SIDR reply. status %d\n",
					     event.status);
			break;
		}
		ret = cma_set_qkey(id_priv, rep->qkey);
		if (ret) {
			pr_debug_ratelimited("RDMA CM: ADDR_ERROR: failed to set qkey. status %d\n", ret);
			event.event = RDMA_CM_EVENT_ADDR_ERROR;
			event.status = ret;
			break;
		}
		ib_init_ah_attr_from_path(id_priv->id.device,
					  id_priv->id.port_num,
					  id_priv->id.route.path_rec,
					  &event.param.ud.ah_attr,
					  rep->sgid_attr);
		event.param.ud.qp_num = rep->qpn;
		event.param.ud.qkey = rep->qkey;
		event.event = RDMA_CM_EVENT_ESTABLISHED;
		event.status = 0;
		break;
	default:
		pr_err("RDMA CMA: unexpected IB CM event: %d\n",
		       ib_event->event);
		goto out;
	}

	ret = cma_cm_event_handler(id_priv, &event);

	rdma_destroy_ah_attr(&event.param.ud.ah_attr);
	if (ret) {
		/* Destroy the CM ID by returning a non-zero value. */
		id_priv->cm_id.ib = NULL;
		destroy_id_handler_unlock(id_priv);
		return ret;
	}
out:
	mutex_unlock(&id_priv->handler_mutex);
	return 0;
}

static int cma_resolve_ib_udp(struct rdma_id_private *id_priv,
			      struct rdma_conn_param *conn_param)
{
	struct ib_cm_sidr_req_param req;
	struct ib_cm_id	*id;
	void *private_data;
	u8 offset;
	int ret;

	memset(&req, 0, sizeof req);
	offset = cma_user_data_offset(id_priv);
	req.private_data_len = offset + conn_param->private_data_len;
	if (req.private_data_len < conn_param->private_data_len)
		return -EINVAL;

	if (req.private_data_len) {
		private_data = kzalloc(req.private_data_len, GFP_ATOMIC);
		if (!private_data)
			return -ENOMEM;
	} else {
		private_data = NULL;
	}

	if (conn_param->private_data && conn_param->private_data_len)
		memcpy(private_data + offset, conn_param->private_data,
		       conn_param->private_data_len);

	if (private_data) {
		ret = cma_format_hdr(private_data, id_priv);
		if (ret)
			goto out;
		req.private_data = private_data;
	}

	id = ib_create_cm_id(id_priv->id.device, cma_sidr_rep_handler,
			     id_priv);
	if (IS_ERR(id)) {
		ret = PTR_ERR(id);
		goto out;
	}
	id_priv->cm_id.ib = id;

	req.path = id_priv->id.route.path_rec;
	req.sgid_attr = id_priv->id.route.addr.dev_addr.sgid_attr;
	req.service_id = rdma_get_service_id(&id_priv->id, cma_dst_addr(id_priv));
	req.timeout_ms = 1 << (CMA_CM_RESPONSE_TIMEOUT - 8);
	req.max_cm_retries = CMA_MAX_CM_RETRIES;

	trace_cm_send_sidr_req(id_priv);
	ret = ib_send_cm_sidr_req(id_priv->cm_id.ib, &req);
	if (ret) {
		ib_destroy_cm_id(id_priv->cm_id.ib);
		id_priv->cm_id.ib = NULL;
	}
out:
	kfree(private_data);
	return ret;
}

static int cma_connect_ib(struct rdma_id_private *id_priv,
			  struct rdma_conn_param *conn_param)
{
	struct ib_cm_req_param req;
	struct rdma_route *route;
	void *private_data;
	struct ib_cm_id	*id;
	u8 offset;
	int ret;

	memset(&req, 0, sizeof req);
	offset = cma_user_data_offset(id_priv);
	req.private_data_len = offset + conn_param->private_data_len;
	if (req.private_data_len < conn_param->private_data_len)
		return -EINVAL;

	if (req.private_data_len) {
		private_data = kzalloc(req.private_data_len, GFP_ATOMIC);
		if (!private_data)
			return -ENOMEM;
	} else {
		private_data = NULL;
	}

	if (conn_param->private_data && conn_param->private_data_len)
		memcpy(private_data + offset, conn_param->private_data,
		       conn_param->private_data_len);

	id = ib_create_cm_id(id_priv->id.device, cma_ib_handler, id_priv);
	if (IS_ERR(id)) {
		ret = PTR_ERR(id);
		goto out;
	}
	id_priv->cm_id.ib = id;

	route = &id_priv->id.route;
	if (private_data) {
		ret = cma_format_hdr(private_data, id_priv);
		if (ret)
			goto out;
		req.private_data = private_data;
	}

	req.primary_path = &route->path_rec[0];
	if (route->num_paths == 2)
		req.alternate_path = &route->path_rec[1];

	req.ppath_sgid_attr = id_priv->id.route.addr.dev_addr.sgid_attr;
	/* Alternate path SGID attribute currently unsupported */
	req.service_id = rdma_get_service_id(&id_priv->id, cma_dst_addr(id_priv));
	req.qp_num = id_priv->qp_num;
	req.qp_type = id_priv->id.qp_type;
	req.starting_psn = id_priv->seq_num;
	req.responder_resources = conn_param->responder_resources;
	req.initiator_depth = conn_param->initiator_depth;
	req.flow_control = conn_param->flow_control;
	req.retry_count = min_t(u8, 7, conn_param->retry_count);
	req.rnr_retry_count = min_t(u8, 7, conn_param->rnr_retry_count);
	req.remote_cm_response_timeout = CMA_CM_RESPONSE_TIMEOUT;
	req.local_cm_response_timeout = CMA_CM_RESPONSE_TIMEOUT;
	req.max_cm_retries = CMA_MAX_CM_RETRIES;
	req.srq = id_priv->srq ? 1 : 0;
	req.ece.vendor_id = id_priv->ece.vendor_id;
	req.ece.attr_mod = id_priv->ece.attr_mod;

	trace_cm_send_req(id_priv);
	ret = ib_send_cm_req(id_priv->cm_id.ib, &req);
out:
	if (ret && !IS_ERR(id)) {
		ib_destroy_cm_id(id);
		id_priv->cm_id.ib = NULL;
	}

	kfree(private_data);
	return ret;
}

static int cma_connect_iw(struct rdma_id_private *id_priv,
			  struct rdma_conn_param *conn_param)
{
	struct iw_cm_id *cm_id;
	int ret;
	struct iw_cm_conn_param iw_param;

	cm_id = iw_create_cm_id(id_priv->id.device, cma_iw_handler, id_priv);
	if (IS_ERR(cm_id))
		return PTR_ERR(cm_id);

	cm_id->tos = id_priv->tos;
	cm_id->tos_set = id_priv->tos_set;
	id_priv->cm_id.iw = cm_id;

	memcpy(&cm_id->local_addr, cma_src_addr(id_priv),
	       rdma_addr_size(cma_src_addr(id_priv)));
	memcpy(&cm_id->remote_addr, cma_dst_addr(id_priv),
	       rdma_addr_size(cma_dst_addr(id_priv)));

	ret = cma_modify_qp_rtr(id_priv, conn_param);
	if (ret)
		goto out;

	if (conn_param) {
		iw_param.ord = conn_param->initiator_depth;
		iw_param.ird = conn_param->responder_resources;
		iw_param.private_data = conn_param->private_data;
		iw_param.private_data_len = conn_param->private_data_len;
		iw_param.qpn = id_priv->id.qp ? id_priv->qp_num : conn_param->qp_num;
	} else {
		memset(&iw_param, 0, sizeof iw_param);
		iw_param.qpn = id_priv->qp_num;
	}
	ret = iw_cm_connect(cm_id, &iw_param);
out:
	if (ret) {
		iw_destroy_cm_id(cm_id);
		id_priv->cm_id.iw = NULL;
	}
	return ret;
}

/**
 * rdma_connect_locked - Initiate an active connection request.
 * @id: Connection identifier to connect.
 * @conn_param: Connection information used for connected QPs.
 *
 * Same as rdma_connect() but can only be called from the
 * RDMA_CM_EVENT_ROUTE_RESOLVED handler callback.
 */
int rdma_connect_locked(struct rdma_cm_id *id,
			struct rdma_conn_param *conn_param)
{
	struct rdma_id_private *id_priv =
		container_of(id, struct rdma_id_private, id);
	int ret;

	if (!cma_comp_exch(id_priv, RDMA_CM_ROUTE_RESOLVED, RDMA_CM_CONNECT))
		return -EINVAL;

	if (!id->qp) {
		id_priv->qp_num = conn_param->qp_num;
		id_priv->srq = conn_param->srq;
	}

	if (rdma_cap_ib_cm(id->device, id->port_num)) {
		if (id->qp_type == IB_QPT_UD)
			ret = cma_resolve_ib_udp(id_priv, conn_param);
		else
			ret = cma_connect_ib(id_priv, conn_param);
	} else if (rdma_cap_iw_cm(id->device, id->port_num)) {
		ret = cma_connect_iw(id_priv, conn_param);
	} else {
		ret = -ENOSYS;
	}
	if (ret)
		goto err_state;
	return 0;
err_state:
	cma_comp_exch(id_priv, RDMA_CM_CONNECT, RDMA_CM_ROUTE_RESOLVED);
	return ret;
}
EXPORT_SYMBOL(rdma_connect_locked);

/**
 * rdma_connect - Initiate an active connection request.
 * @id: Connection identifier to connect.
 * @conn_param: Connection information used for connected QPs.
 *
 * Users must have resolved a route for the rdma_cm_id to connect with by having
 * called rdma_resolve_route before calling this routine.
 *
 * This call will either connect to a remote QP or obtain remote QP information
 * for unconnected rdma_cm_id's.  The actual operation is based on the
 * rdma_cm_id's port space.
 */
int rdma_connect(struct rdma_cm_id *id, struct rdma_conn_param *conn_param)
{
	struct rdma_id_private *id_priv =
		container_of(id, struct rdma_id_private, id);
	int ret;

	mutex_lock(&id_priv->handler_mutex);
	ret = rdma_connect_locked(id, conn_param);
	mutex_unlock(&id_priv->handler_mutex);
	return ret;
}
EXPORT_SYMBOL(rdma_connect);

/**
 * rdma_connect_ece - Initiate an active connection request with ECE data.
 * @id: Connection identifier to connect.
 * @conn_param: Connection information used for connected QPs.
 * @ece: ECE parameters
 *
 * See rdma_connect() explanation.
 */
int rdma_connect_ece(struct rdma_cm_id *id, struct rdma_conn_param *conn_param,
		     struct rdma_ucm_ece *ece)
{
	struct rdma_id_private *id_priv =
		container_of(id, struct rdma_id_private, id);

	id_priv->ece.vendor_id = ece->vendor_id;
	id_priv->ece.attr_mod = ece->attr_mod;

	return rdma_connect(id, conn_param);
}
EXPORT_SYMBOL(rdma_connect_ece);

static int cma_accept_ib(struct rdma_id_private *id_priv,
			 struct rdma_conn_param *conn_param)
{
	struct ib_cm_rep_param rep;
	int ret;

	ret = cma_modify_qp_rtr(id_priv, conn_param);
	if (ret)
		goto out;

	ret = cma_modify_qp_rts(id_priv, conn_param);
	if (ret)
		goto out;

	memset(&rep, 0, sizeof rep);
	rep.qp_num = id_priv->qp_num;
	rep.starting_psn = id_priv->seq_num;
	rep.private_data = conn_param->private_data;
	rep.private_data_len = conn_param->private_data_len;
	rep.responder_resources = conn_param->responder_resources;
	rep.initiator_depth = conn_param->initiator_depth;
	rep.failover_accepted = 0;
	rep.flow_control = conn_param->flow_control;
	rep.rnr_retry_count = min_t(u8, 7, conn_param->rnr_retry_count);
	rep.srq = id_priv->srq ? 1 : 0;
	rep.ece.vendor_id = id_priv->ece.vendor_id;
	rep.ece.attr_mod = id_priv->ece.attr_mod;

	trace_cm_send_rep(id_priv);
	ret = ib_send_cm_rep(id_priv->cm_id.ib, &rep);
out:
	return ret;
}

static int cma_accept_iw(struct rdma_id_private *id_priv,
		  struct rdma_conn_param *conn_param)
{
	struct iw_cm_conn_param iw_param;
	int ret;

	if (!conn_param)
		return -EINVAL;

	ret = cma_modify_qp_rtr(id_priv, conn_param);
	if (ret)
		return ret;

	iw_param.ord = conn_param->initiator_depth;
	iw_param.ird = conn_param->responder_resources;
	iw_param.private_data = conn_param->private_data;
	iw_param.private_data_len = conn_param->private_data_len;
	if (id_priv->id.qp)
		iw_param.qpn = id_priv->qp_num;
	else
		iw_param.qpn = conn_param->qp_num;

	return iw_cm_accept(id_priv->cm_id.iw, &iw_param);
}

static int cma_send_sidr_rep(struct rdma_id_private *id_priv,
			     enum ib_cm_sidr_status status, u32 qkey,
			     const void *private_data, int private_data_len)
{
	struct ib_cm_sidr_rep_param rep;
	int ret;

	memset(&rep, 0, sizeof rep);
	rep.status = status;
	if (status == IB_SIDR_SUCCESS) {
		ret = cma_set_qkey(id_priv, qkey);
		if (ret)
			return ret;
		rep.qp_num = id_priv->qp_num;
		rep.qkey = id_priv->qkey;

		rep.ece.vendor_id = id_priv->ece.vendor_id;
		rep.ece.attr_mod = id_priv->ece.attr_mod;
	}

	rep.private_data = private_data;
	rep.private_data_len = private_data_len;

	trace_cm_send_sidr_rep(id_priv);
	return ib_send_cm_sidr_rep(id_priv->cm_id.ib, &rep);
}

/**
 * rdma_accept - Called to accept a connection request or response.
 * @id: Connection identifier associated with the request.
 * @conn_param: Information needed to establish the connection.  This must be
 *   provided if accepting a connection request.  If accepting a connection
 *   response, this parameter must be NULL.
 *
 * Typically, this routine is only called by the listener to accept a connection
 * request.  It must also be called on the active side of a connection if the
 * user is performing their own QP transitions.
 *
 * In the case of error, a reject message is sent to the remote side and the
 * state of the qp associated with the id is modified to error, such that any
 * previously posted receive buffers would be flushed.
 *
 * This function is for use by kernel ULPs and must be called from under the
 * handler callback.
 */
int rdma_accept(struct rdma_cm_id *id, struct rdma_conn_param *conn_param)
{
	struct rdma_id_private *id_priv =
		container_of(id, struct rdma_id_private, id);
	int ret;

	lockdep_assert_held(&id_priv->handler_mutex);

	if (READ_ONCE(id_priv->state) != RDMA_CM_CONNECT)
		return -EINVAL;

	if (!id->qp && conn_param) {
		id_priv->qp_num = conn_param->qp_num;
		id_priv->srq = conn_param->srq;
	}

	if (rdma_cap_ib_cm(id->device, id->port_num)) {
		if (id->qp_type == IB_QPT_UD) {
			if (conn_param)
				ret = cma_send_sidr_rep(id_priv, IB_SIDR_SUCCESS,
							conn_param->qkey,
							conn_param->private_data,
							conn_param->private_data_len);
			else
				ret = cma_send_sidr_rep(id_priv, IB_SIDR_SUCCESS,
							0, NULL, 0);
		} else {
			if (conn_param)
				ret = cma_accept_ib(id_priv, conn_param);
			else
				ret = cma_rep_recv(id_priv);
		}
	} else if (rdma_cap_iw_cm(id->device, id->port_num)) {
		ret = cma_accept_iw(id_priv, conn_param);
	} else {
		ret = -ENOSYS;
	}
	if (ret)
		goto reject;

	return 0;
reject:
	cma_modify_qp_err(id_priv);
	rdma_reject(id, NULL, 0, IB_CM_REJ_CONSUMER_DEFINED);
	return ret;
}
EXPORT_SYMBOL(rdma_accept);

int rdma_accept_ece(struct rdma_cm_id *id, struct rdma_conn_param *conn_param,
		    struct rdma_ucm_ece *ece)
{
	struct rdma_id_private *id_priv =
		container_of(id, struct rdma_id_private, id);

	id_priv->ece.vendor_id = ece->vendor_id;
	id_priv->ece.attr_mod = ece->attr_mod;

	return rdma_accept(id, conn_param);
}
EXPORT_SYMBOL(rdma_accept_ece);

void rdma_lock_handler(struct rdma_cm_id *id)
{
	struct rdma_id_private *id_priv =
		container_of(id, struct rdma_id_private, id);

	mutex_lock(&id_priv->handler_mutex);
}
EXPORT_SYMBOL(rdma_lock_handler);

void rdma_unlock_handler(struct rdma_cm_id *id)
{
	struct rdma_id_private *id_priv =
		container_of(id, struct rdma_id_private, id);

	mutex_unlock(&id_priv->handler_mutex);
}
EXPORT_SYMBOL(rdma_unlock_handler);

int rdma_notify(struct rdma_cm_id *id, enum ib_event_type event)
{
	struct rdma_id_private *id_priv;
	int ret;

	id_priv = container_of(id, struct rdma_id_private, id);
	if (!id_priv->cm_id.ib)
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
		u8 private_data_len, u8 reason)
{
	struct rdma_id_private *id_priv;
	int ret;

	id_priv = container_of(id, struct rdma_id_private, id);
	if (!id_priv->cm_id.ib)
		return -EINVAL;

	if (rdma_cap_ib_cm(id->device, id->port_num)) {
		if (id->qp_type == IB_QPT_UD) {
			ret = cma_send_sidr_rep(id_priv, IB_SIDR_REJECT, 0,
						private_data, private_data_len);
		} else {
			trace_cm_send_rej(id_priv);
			ret = ib_send_cm_rej(id_priv->cm_id.ib, reason, NULL, 0,
					     private_data, private_data_len);
		}
	} else if (rdma_cap_iw_cm(id->device, id->port_num)) {
		ret = iw_cm_reject(id_priv->cm_id.iw,
				   private_data, private_data_len);
	} else {
		ret = -ENOSYS;
	}

	return ret;
}
EXPORT_SYMBOL(rdma_reject);

int rdma_disconnect(struct rdma_cm_id *id)
{
	struct rdma_id_private *id_priv;
	int ret;

	id_priv = container_of(id, struct rdma_id_private, id);
	if (!id_priv->cm_id.ib)
		return -EINVAL;

	if (rdma_cap_ib_cm(id->device, id->port_num)) {
		ret = cma_modify_qp_err(id_priv);
		if (ret)
			goto out;
		/* Initiate or respond to a disconnect. */
		trace_cm_disconnect(id_priv);
		if (ib_send_cm_dreq(id_priv->cm_id.ib, NULL, 0)) {
			if (!ib_send_cm_drep(id_priv->cm_id.ib, NULL, 0))
				trace_cm_sent_drep(id_priv);
		} else {
			trace_cm_sent_dreq(id_priv);
		}
	} else if (rdma_cap_iw_cm(id->device, id->port_num)) {
		ret = iw_cm_disconnect(id_priv->cm_id.iw, 0);
	} else
		ret = -EINVAL;

out:
	return ret;
}
EXPORT_SYMBOL(rdma_disconnect);

static void cma_make_mc_event(int status, struct rdma_id_private *id_priv,
			      struct ib_sa_multicast *multicast,
			      struct rdma_cm_event *event,
			      struct cma_multicast *mc)
{
	struct rdma_dev_addr *dev_addr;
	enum ib_gid_type gid_type;
	struct net_device *ndev;

	if (!status)
		status = cma_set_qkey(id_priv, be32_to_cpu(multicast->rec.qkey));
	else
		pr_debug_ratelimited("RDMA CM: MULTICAST_ERROR: failed to join multicast. status %d\n",
				     status);

	event->status = status;
	event->param.ud.private_data = mc->context;
	if (status) {
		event->event = RDMA_CM_EVENT_MULTICAST_ERROR;
		return;
	}

	dev_addr = &id_priv->id.route.addr.dev_addr;
	ndev = dev_get_by_index(dev_addr->net, dev_addr->bound_dev_if);
	gid_type =
		id_priv->cma_dev
			->default_gid_type[id_priv->id.port_num -
					   rdma_start_port(
						   id_priv->cma_dev->device)];

	event->event = RDMA_CM_EVENT_MULTICAST_JOIN;
	if (ib_init_ah_from_mcmember(id_priv->id.device, id_priv->id.port_num,
				     &multicast->rec, ndev, gid_type,
				     &event->param.ud.ah_attr)) {
		event->event = RDMA_CM_EVENT_MULTICAST_ERROR;
		goto out;
	}

	event->param.ud.qp_num = 0xFFFFFF;
	event->param.ud.qkey = be32_to_cpu(multicast->rec.qkey);

out:
	if (ndev)
		dev_put(ndev);
}

static int cma_ib_mc_handler(int status, struct ib_sa_multicast *multicast)
{
	struct cma_multicast *mc = multicast->context;
	struct rdma_id_private *id_priv = mc->id_priv;
	struct rdma_cm_event event = {};
	int ret = 0;

	mutex_lock(&id_priv->handler_mutex);
	if (READ_ONCE(id_priv->state) == RDMA_CM_DEVICE_REMOVAL ||
	    READ_ONCE(id_priv->state) == RDMA_CM_DESTROYING)
		goto out;

	cma_make_mc_event(status, id_priv, multicast, &event, mc);
	ret = cma_cm_event_handler(id_priv, &event);
	rdma_destroy_ah_attr(&event.param.ud.ah_attr);
	WARN_ON(ret);

out:
	mutex_unlock(&id_priv->handler_mutex);
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
		   ((be32_to_cpu(sin6->sin6_addr.s6_addr32[0]) & 0xFFF0FFFF) ==
								 0xFF10A01B)) {
		/* IPv6 address is an SA assigned MGID. */
		memcpy(mgid, &sin6->sin6_addr, sizeof *mgid);
	} else if (addr->sa_family == AF_IB) {
		memcpy(mgid, &((struct sockaddr_ib *) addr)->sib_addr, sizeof *mgid);
	} else if (addr->sa_family == AF_INET6) {
		ipv6_ib_mc_map(&sin6->sin6_addr, dev_addr->broadcast, mc_map);
		if (id_priv->id.ps == RDMA_PS_UDP)
			mc_map[7] = 0x01;	/* Use RDMA CM signature */
		*mgid = *(union ib_gid *) (mc_map + 4);
	} else {
		ip_ib_mc_map(sin->sin_addr.s_addr, dev_addr->broadcast, mc_map);
		if (id_priv->id.ps == RDMA_PS_UDP)
			mc_map[7] = 0x01;	/* Use RDMA CM signature */
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

	ret = cma_set_qkey(id_priv, 0);
	if (ret)
		return ret;

	cma_set_mgid(id_priv, (struct sockaddr *) &mc->addr, &rec.mgid);
	rec.qkey = cpu_to_be32(id_priv->qkey);
	rdma_addr_get_sgid(dev_addr, &rec.port_gid);
	rec.pkey = cpu_to_be16(ib_addr_get_pkey(dev_addr));
	rec.join_state = mc->join_state;

	comp_mask = IB_SA_MCMEMBER_REC_MGID | IB_SA_MCMEMBER_REC_PORT_GID |
		    IB_SA_MCMEMBER_REC_PKEY | IB_SA_MCMEMBER_REC_JOIN_STATE |
		    IB_SA_MCMEMBER_REC_QKEY | IB_SA_MCMEMBER_REC_SL |
		    IB_SA_MCMEMBER_REC_FLOW_LABEL |
		    IB_SA_MCMEMBER_REC_TRAFFIC_CLASS;

	if (id_priv->id.ps == RDMA_PS_IPOIB)
		comp_mask |= IB_SA_MCMEMBER_REC_RATE |
			     IB_SA_MCMEMBER_REC_RATE_SELECTOR |
			     IB_SA_MCMEMBER_REC_MTU_SELECTOR |
			     IB_SA_MCMEMBER_REC_MTU |
			     IB_SA_MCMEMBER_REC_HOP_LIMIT;

	mc->sa_mc = ib_sa_join_multicast(&sa_client, id_priv->id.device,
					 id_priv->id.port_num, &rec, comp_mask,
					 GFP_KERNEL, cma_ib_mc_handler, mc);
	return PTR_ERR_OR_ZERO(mc->sa_mc);
}

static void cma_iboe_set_mgid(struct sockaddr *addr, union ib_gid *mgid,
			      enum ib_gid_type gid_type)
{
	struct sockaddr_in *sin = (struct sockaddr_in *)addr;
	struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)addr;

	if (cma_any_addr(addr)) {
		memset(mgid, 0, sizeof *mgid);
	} else if (addr->sa_family == AF_INET6) {
		memcpy(mgid, &sin6->sin6_addr, sizeof *mgid);
	} else {
		mgid->raw[0] =
			(gid_type == IB_GID_TYPE_ROCE_UDP_ENCAP) ? 0 : 0xff;
		mgid->raw[1] =
			(gid_type == IB_GID_TYPE_ROCE_UDP_ENCAP) ? 0 : 0x0e;
		mgid->raw[2] = 0;
		mgid->raw[3] = 0;
		mgid->raw[4] = 0;
		mgid->raw[5] = 0;
		mgid->raw[6] = 0;
		mgid->raw[7] = 0;
		mgid->raw[8] = 0;
		mgid->raw[9] = 0;
		mgid->raw[10] = 0xff;
		mgid->raw[11] = 0xff;
		*(__be32 *)(&mgid->raw[12]) = sin->sin_addr.s_addr;
	}
}

static int cma_iboe_join_multicast(struct rdma_id_private *id_priv,
				   struct cma_multicast *mc)
{
	struct rdma_dev_addr *dev_addr = &id_priv->id.route.addr.dev_addr;
	int err = 0;
	struct sockaddr *addr = (struct sockaddr *)&mc->addr;
	struct net_device *ndev = NULL;
	struct ib_sa_multicast ib;
	enum ib_gid_type gid_type;
	bool send_only;

	send_only = mc->join_state == BIT(SENDONLY_FULLMEMBER_JOIN);

	if (cma_zero_addr(addr))
		return -EINVAL;

	gid_type = id_priv->cma_dev->default_gid_type[id_priv->id.port_num -
		   rdma_start_port(id_priv->cma_dev->device)];
	cma_iboe_set_mgid(addr, &ib.rec.mgid, gid_type);

	ib.rec.pkey = cpu_to_be16(0xffff);
	if (id_priv->id.ps == RDMA_PS_UDP)
		ib.rec.qkey = cpu_to_be32(RDMA_UDP_QKEY);

	if (dev_addr->bound_dev_if)
		ndev = dev_get_by_index(dev_addr->net, dev_addr->bound_dev_if);
	if (!ndev)
		return -ENODEV;

	ib.rec.rate = iboe_get_rate(ndev);
	ib.rec.hop_limit = 1;
	ib.rec.mtu = iboe_get_mtu(ndev->mtu);

	if (addr->sa_family == AF_INET) {
		if (gid_type == IB_GID_TYPE_ROCE_UDP_ENCAP) {
			ib.rec.hop_limit = IPV6_DEFAULT_HOPLIMIT;
			if (!send_only) {
				err = cma_igmp_send(ndev, &ib.rec.mgid,
						    true);
			}
		}
	} else {
		if (gid_type == IB_GID_TYPE_ROCE_UDP_ENCAP)
			err = -ENOTSUPP;
	}
	dev_put(ndev);
	if (err || !ib.rec.mtu)
		return err ?: -EINVAL;

	rdma_ip2gid((struct sockaddr *)&id_priv->id.route.addr.src_addr,
		    &ib.rec.port_gid);
	INIT_WORK(&mc->iboe_join.work, cma_iboe_join_work_handler);
	cma_make_mc_event(0, id_priv, &ib, &mc->iboe_join.event, mc);
	queue_work(cma_wq, &mc->iboe_join.work);
	return 0;
}

int rdma_join_multicast(struct rdma_cm_id *id, struct sockaddr *addr,
			u8 join_state, void *context)
{
	struct rdma_id_private *id_priv =
		container_of(id, struct rdma_id_private, id);
	struct cma_multicast *mc;
	int ret;

	/* Not supported for kernel QPs */
	if (WARN_ON(id->qp))
		return -EINVAL;

	/* ULP is calling this wrong. */
	if (!id->device || (READ_ONCE(id_priv->state) != RDMA_CM_ADDR_BOUND &&
			    READ_ONCE(id_priv->state) != RDMA_CM_ADDR_RESOLVED))
		return -EINVAL;

	mc = kzalloc(sizeof(*mc), GFP_KERNEL);
	if (!mc)
		return -ENOMEM;

	memcpy(&mc->addr, addr, rdma_addr_size(addr));
	mc->context = context;
	mc->id_priv = id_priv;
	mc->join_state = join_state;

	if (rdma_protocol_roce(id->device, id->port_num)) {
		ret = cma_iboe_join_multicast(id_priv, mc);
		if (ret)
			goto out_err;
	} else if (rdma_cap_ib_mcast(id->device, id->port_num)) {
		ret = cma_join_ib_multicast(id_priv, mc);
		if (ret)
			goto out_err;
	} else {
		ret = -ENOSYS;
		goto out_err;
	}

	spin_lock(&id_priv->lock);
	list_add(&mc->list, &id_priv->mc_list);
	spin_unlock(&id_priv->lock);

	return 0;
out_err:
	kfree(mc);
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
		if (memcmp(&mc->addr, addr, rdma_addr_size(addr)) != 0)
			continue;
		list_del(&mc->list);
		spin_unlock_irq(&id_priv->lock);

		WARN_ON(id_priv->cma_dev->device != id->device);
		destroy_mc(id_priv, mc);
		return;
	}
	spin_unlock_irq(&id_priv->lock);
}
EXPORT_SYMBOL(rdma_leave_multicast);

static int cma_netdev_change(struct net_device *ndev, struct rdma_id_private *id_priv)
{
	struct rdma_dev_addr *dev_addr;
	struct cma_work *work;

	dev_addr = &id_priv->id.route.addr.dev_addr;

	if ((dev_addr->bound_dev_if == ndev->ifindex) &&
	    (net_eq(dev_net(ndev), dev_addr->net)) &&
	    memcmp(dev_addr->src_dev_addr, ndev->dev_addr, ndev->addr_len)) {
		pr_info("RDMA CM addr change for ndev %s used by id %p\n",
			ndev->name, &id_priv->id);
		work = kzalloc(sizeof *work, GFP_KERNEL);
		if (!work)
			return -ENOMEM;

		INIT_WORK(&work->work, cma_work_handler);
		work->id = id_priv;
		work->event.event = RDMA_CM_EVENT_ADDR_CHANGE;
		cma_id_get(id_priv);
		queue_work(cma_wq, &work->work);
	}

	return 0;
}

static int cma_netdev_callback(struct notifier_block *self, unsigned long event,
			       void *ptr)
{
	struct net_device *ndev = netdev_notifier_info_to_dev(ptr);
	struct cma_device *cma_dev;
	struct rdma_id_private *id_priv;
	int ret = NOTIFY_DONE;

	if (event != NETDEV_BONDING_FAILOVER)
		return NOTIFY_DONE;

	if (!netif_is_bond_master(ndev))
		return NOTIFY_DONE;

	mutex_lock(&lock);
	list_for_each_entry(cma_dev, &dev_list, list)
		list_for_each_entry(id_priv, &cma_dev->id_list, list) {
			ret = cma_netdev_change(ndev, id_priv);
			if (ret)
				goto out;
		}

out:
	mutex_unlock(&lock);
	return ret;
}

static struct notifier_block cma_nb = {
	.notifier_call = cma_netdev_callback
};

static void cma_send_device_removal_put(struct rdma_id_private *id_priv)
{
	struct rdma_cm_event event = { .event = RDMA_CM_EVENT_DEVICE_REMOVAL };
	enum rdma_cm_state state;
	unsigned long flags;

	mutex_lock(&id_priv->handler_mutex);
	/* Record that we want to remove the device */
	spin_lock_irqsave(&id_priv->lock, flags);
	state = id_priv->state;
	if (state == RDMA_CM_DESTROYING || state == RDMA_CM_DEVICE_REMOVAL) {
		spin_unlock_irqrestore(&id_priv->lock, flags);
		mutex_unlock(&id_priv->handler_mutex);
		cma_id_put(id_priv);
		return;
	}
	id_priv->state = RDMA_CM_DEVICE_REMOVAL;
	spin_unlock_irqrestore(&id_priv->lock, flags);

	if (cma_cm_event_handler(id_priv, &event)) {
		/*
		 * At this point the ULP promises it won't call
		 * rdma_destroy_id() concurrently
		 */
		cma_id_put(id_priv);
		mutex_unlock(&id_priv->handler_mutex);
		trace_cm_id_destroy(id_priv);
		_destroy_id(id_priv, state);
		return;
	}
	mutex_unlock(&id_priv->handler_mutex);

	/*
	 * If this races with destroy then the thread that first assigns state
	 * to a destroying does the cancel.
	 */
	cma_cancel_operation(id_priv, state);
	cma_id_put(id_priv);
}

static void cma_process_remove(struct cma_device *cma_dev)
{
	mutex_lock(&lock);
	while (!list_empty(&cma_dev->id_list)) {
		struct rdma_id_private *id_priv = list_first_entry(
			&cma_dev->id_list, struct rdma_id_private, list);

		list_del(&id_priv->listen_list);
		list_del_init(&id_priv->list);
		cma_id_get(id_priv);
		mutex_unlock(&lock);

		cma_send_device_removal_put(id_priv);

		mutex_lock(&lock);
	}
	mutex_unlock(&lock);

	cma_dev_put(cma_dev);
	wait_for_completion(&cma_dev->comp);
}

static bool cma_supported(struct ib_device *device)
{
	u32 i;

	rdma_for_each_port(device, i) {
		if (rdma_cap_ib_cm(device, i) || rdma_cap_iw_cm(device, i))
			return true;
	}
	return false;
}

static int cma_add_one(struct ib_device *device)
{
	struct rdma_id_private *to_destroy;
	struct cma_device *cma_dev;
	struct rdma_id_private *id_priv;
	unsigned long supported_gids = 0;
	int ret;
	u32 i;

	if (!cma_supported(device))
		return -EOPNOTSUPP;

	cma_dev = kmalloc(sizeof(*cma_dev), GFP_KERNEL);
	if (!cma_dev)
		return -ENOMEM;

	cma_dev->device = device;
	cma_dev->default_gid_type = kcalloc(device->phys_port_cnt,
					    sizeof(*cma_dev->default_gid_type),
					    GFP_KERNEL);
	if (!cma_dev->default_gid_type) {
		ret = -ENOMEM;
		goto free_cma_dev;
	}

	cma_dev->default_roce_tos = kcalloc(device->phys_port_cnt,
					    sizeof(*cma_dev->default_roce_tos),
					    GFP_KERNEL);
	if (!cma_dev->default_roce_tos) {
		ret = -ENOMEM;
		goto free_gid_type;
	}

	rdma_for_each_port (device, i) {
		supported_gids = roce_gid_type_mask_support(device, i);
		WARN_ON(!supported_gids);
		if (supported_gids & (1 << CMA_PREFERRED_ROCE_GID_TYPE))
			cma_dev->default_gid_type[i - rdma_start_port(device)] =
				CMA_PREFERRED_ROCE_GID_TYPE;
		else
			cma_dev->default_gid_type[i - rdma_start_port(device)] =
				find_first_bit(&supported_gids, BITS_PER_LONG);
		cma_dev->default_roce_tos[i - rdma_start_port(device)] = 0;
	}

	init_completion(&cma_dev->comp);
	refcount_set(&cma_dev->refcount, 1);
	INIT_LIST_HEAD(&cma_dev->id_list);
	ib_set_client_data(device, &cma_client, cma_dev);

	mutex_lock(&lock);
	list_add_tail(&cma_dev->list, &dev_list);
	list_for_each_entry(id_priv, &listen_any_list, list) {
		ret = cma_listen_on_dev(id_priv, cma_dev, &to_destroy);
		if (ret)
			goto free_listen;
	}
	mutex_unlock(&lock);

	trace_cm_add_one(device);
	return 0;

free_listen:
	list_del(&cma_dev->list);
	mutex_unlock(&lock);

	/* cma_process_remove() will delete to_destroy */
	cma_process_remove(cma_dev);
	kfree(cma_dev->default_roce_tos);
free_gid_type:
	kfree(cma_dev->default_gid_type);

free_cma_dev:
	kfree(cma_dev);
	return ret;
}

static void cma_remove_one(struct ib_device *device, void *client_data)
{
	struct cma_device *cma_dev = client_data;

	trace_cm_remove_one(device);

	mutex_lock(&lock);
	list_del(&cma_dev->list);
	mutex_unlock(&lock);

	cma_process_remove(cma_dev);
	kfree(cma_dev->default_roce_tos);
	kfree(cma_dev->default_gid_type);
	kfree(cma_dev);
}

static int cma_init_net(struct net *net)
{
	struct cma_pernet *pernet = cma_pernet(net);

	xa_init(&pernet->tcp_ps);
	xa_init(&pernet->udp_ps);
	xa_init(&pernet->ipoib_ps);
	xa_init(&pernet->ib_ps);

	return 0;
}

static void cma_exit_net(struct net *net)
{
	struct cma_pernet *pernet = cma_pernet(net);

	WARN_ON(!xa_empty(&pernet->tcp_ps));
	WARN_ON(!xa_empty(&pernet->udp_ps));
	WARN_ON(!xa_empty(&pernet->ipoib_ps));
	WARN_ON(!xa_empty(&pernet->ib_ps));
}

static struct pernet_operations cma_pernet_operations = {
	.init = cma_init_net,
	.exit = cma_exit_net,
	.id = &cma_pernet_id,
	.size = sizeof(struct cma_pernet),
};

static int __init cma_init(void)
{
	int ret;

	/*
	 * There is a rare lock ordering dependency in cma_netdev_callback()
	 * that only happens when bonding is enabled. Teach lockdep that rtnl
	 * must never be nested under lock so it can find these without having
	 * to test with bonding.
	 */
	if (IS_ENABLED(CONFIG_LOCKDEP)) {
		rtnl_lock();
		mutex_lock(&lock);
		mutex_unlock(&lock);
		rtnl_unlock();
	}

	cma_wq = alloc_ordered_workqueue("rdma_cm", WQ_MEM_RECLAIM);
	if (!cma_wq)
		return -ENOMEM;

	ret = register_pernet_subsys(&cma_pernet_operations);
	if (ret)
		goto err_wq;

	ib_sa_register_client(&sa_client);
	register_netdevice_notifier(&cma_nb);

	ret = ib_register_client(&cma_client);
	if (ret)
		goto err;

	ret = cma_configfs_init();
	if (ret)
		goto err_ib;

	return 0;

err_ib:
	ib_unregister_client(&cma_client);
err:
	unregister_netdevice_notifier(&cma_nb);
	ib_sa_unregister_client(&sa_client);
	unregister_pernet_subsys(&cma_pernet_operations);
err_wq:
	destroy_workqueue(cma_wq);
	return ret;
}

static void __exit cma_cleanup(void)
{
	cma_configfs_exit();
	ib_unregister_client(&cma_client);
	unregister_netdevice_notifier(&cma_nb);
	ib_sa_unregister_client(&sa_client);
	unregister_pernet_subsys(&cma_pernet_operations);
	destroy_workqueue(cma_wq);
}

module_init(cma_init);
module_exit(cma_cleanup);
