/*
 * Copyright (c) 2004 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005 Voltaire, Inc.  All rights reserved.
 * Copyright (c) 2006 Intel Corporation.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/random.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/dma-mapping.h>
#include <linux/kref.h>
#include <linux/idr.h>
#include <linux/workqueue.h>
#include <uapi/linux/if_ether.h>
#include <rdma/ib_pack.h>
#include <rdma/ib_cache.h>
#include <rdma/rdma_netlink.h>
#include <net/netlink.h>
#include <uapi/rdma/ib_user_sa.h>
#include <rdma/ib_marshall.h>
#include <rdma/ib_addr.h>
#include "sa.h"
#include "core_priv.h"

#define IB_SA_LOCAL_SVC_TIMEOUT_MIN		100
#define IB_SA_LOCAL_SVC_TIMEOUT_DEFAULT		2000
#define IB_SA_LOCAL_SVC_TIMEOUT_MAX		200000
static int sa_local_svc_timeout_ms = IB_SA_LOCAL_SVC_TIMEOUT_DEFAULT;

struct ib_sa_sm_ah {
	struct ib_ah        *ah;
	struct kref          ref;
	u16		     pkey_index;
	u8		     src_path_mask;
};

struct ib_sa_classport_cache {
	bool valid;
	struct ib_class_port_info data;
};

struct ib_sa_port {
	struct ib_mad_agent *agent;
	struct ib_sa_sm_ah  *sm_ah;
	struct work_struct   update_task;
	struct ib_sa_classport_cache classport_info;
	spinlock_t                   classport_lock; /* protects class port info set */
	spinlock_t           ah_lock;
	u8                   port_num;
};

struct ib_sa_device {
	int                     start_port, end_port;
	struct ib_event_handler event_handler;
	struct ib_sa_port port[0];
};

struct ib_sa_query {
	void (*callback)(struct ib_sa_query *, int, struct ib_sa_mad *);
	void (*release)(struct ib_sa_query *);
	struct ib_sa_client    *client;
	struct ib_sa_port      *port;
	struct ib_mad_send_buf *mad_buf;
	struct ib_sa_sm_ah     *sm_ah;
	int			id;
	u32			flags;
	struct list_head	list; /* Local svc request list */
	u32			seq; /* Local svc request sequence number */
	unsigned long		timeout; /* Local svc timeout */
	u8			path_use; /* How will the pathrecord be used */
};

#define IB_SA_ENABLE_LOCAL_SERVICE	0x00000001
#define IB_SA_CANCEL			0x00000002

struct ib_sa_service_query {
	void (*callback)(int, struct ib_sa_service_rec *, void *);
	void *context;
	struct ib_sa_query sa_query;
};

struct ib_sa_path_query {
	void (*callback)(int, struct ib_sa_path_rec *, void *);
	void *context;
	struct ib_sa_query sa_query;
};

struct ib_sa_guidinfo_query {
	void (*callback)(int, struct ib_sa_guidinfo_rec *, void *);
	void *context;
	struct ib_sa_query sa_query;
};

struct ib_sa_classport_info_query {
	void (*callback)(int, struct ib_class_port_info *, void *);
	void *context;
	struct ib_sa_query sa_query;
};

struct ib_sa_mcmember_query {
	void (*callback)(int, struct ib_sa_mcmember_rec *, void *);
	void *context;
	struct ib_sa_query sa_query;
};

static LIST_HEAD(ib_nl_request_list);
static DEFINE_SPINLOCK(ib_nl_request_lock);
static atomic_t ib_nl_sa_request_seq;
static struct workqueue_struct *ib_nl_wq;
static struct delayed_work ib_nl_timed_work;
static const struct nla_policy ib_nl_policy[LS_NLA_TYPE_MAX] = {
	[LS_NLA_TYPE_PATH_RECORD]	= {.type = NLA_BINARY,
		.len = sizeof(struct ib_path_rec_data)},
	[LS_NLA_TYPE_TIMEOUT]		= {.type = NLA_U32},
	[LS_NLA_TYPE_SERVICE_ID]	= {.type = NLA_U64},
	[LS_NLA_TYPE_DGID]		= {.type = NLA_BINARY,
		.len = sizeof(struct rdma_nla_ls_gid)},
	[LS_NLA_TYPE_SGID]		= {.type = NLA_BINARY,
		.len = sizeof(struct rdma_nla_ls_gid)},
	[LS_NLA_TYPE_TCLASS]		= {.type = NLA_U8},
	[LS_NLA_TYPE_PKEY]		= {.type = NLA_U16},
	[LS_NLA_TYPE_QOS_CLASS]		= {.type = NLA_U16},
};


static void ib_sa_add_one(struct ib_device *device);
static void ib_sa_remove_one(struct ib_device *device, void *client_data);

static struct ib_client sa_client = {
	.name   = "sa",
	.add    = ib_sa_add_one,
	.remove = ib_sa_remove_one
};

static DEFINE_SPINLOCK(idr_lock);
static DEFINE_IDR(query_idr);

static DEFINE_SPINLOCK(tid_lock);
static u32 tid;

#define PATH_REC_FIELD(field) \
	.struct_offset_bytes = offsetof(struct ib_sa_path_rec, field),		\
	.struct_size_bytes   = sizeof ((struct ib_sa_path_rec *) 0)->field,	\
	.field_name          = "sa_path_rec:" #field

static const struct ib_field path_rec_table[] = {
	{ PATH_REC_FIELD(service_id),
	  .offset_words = 0,
	  .offset_bits  = 0,
	  .size_bits    = 64 },
	{ PATH_REC_FIELD(dgid),
	  .offset_words = 2,
	  .offset_bits  = 0,
	  .size_bits    = 128 },
	{ PATH_REC_FIELD(sgid),
	  .offset_words = 6,
	  .offset_bits  = 0,
	  .size_bits    = 128 },
	{ PATH_REC_FIELD(dlid),
	  .offset_words = 10,
	  .offset_bits  = 0,
	  .size_bits    = 16 },
	{ PATH_REC_FIELD(slid),
	  .offset_words = 10,
	  .offset_bits  = 16,
	  .size_bits    = 16 },
	{ PATH_REC_FIELD(raw_traffic),
	  .offset_words = 11,
	  .offset_bits  = 0,
	  .size_bits    = 1 },
	{ RESERVED,
	  .offset_words = 11,
	  .offset_bits  = 1,
	  .size_bits    = 3 },
	{ PATH_REC_FIELD(flow_label),
	  .offset_words = 11,
	  .offset_bits  = 4,
	  .size_bits    = 20 },
	{ PATH_REC_FIELD(hop_limit),
	  .offset_words = 11,
	  .offset_bits  = 24,
	  .size_bits    = 8 },
	{ PATH_REC_FIELD(traffic_class),
	  .offset_words = 12,
	  .offset_bits  = 0,
	  .size_bits    = 8 },
	{ PATH_REC_FIELD(reversible),
	  .offset_words = 12,
	  .offset_bits  = 8,
	  .size_bits    = 1 },
	{ PATH_REC_FIELD(numb_path),
	  .offset_words = 12,
	  .offset_bits  = 9,
	  .size_bits    = 7 },
	{ PATH_REC_FIELD(pkey),
	  .offset_words = 12,
	  .offset_bits  = 16,
	  .size_bits    = 16 },
	{ PATH_REC_FIELD(qos_class),
	  .offset_words = 13,
	  .offset_bits  = 0,
	  .size_bits    = 12 },
	{ PATH_REC_FIELD(sl),
	  .offset_words = 13,
	  .offset_bits  = 12,
	  .size_bits    = 4 },
	{ PATH_REC_FIELD(mtu_selector),
	  .offset_words = 13,
	  .offset_bits  = 16,
	  .size_bits    = 2 },
	{ PATH_REC_FIELD(mtu),
	  .offset_words = 13,
	  .offset_bits  = 18,
	  .size_bits    = 6 },
	{ PATH_REC_FIELD(rate_selector),
	  .offset_words = 13,
	  .offset_bits  = 24,
	  .size_bits    = 2 },
	{ PATH_REC_FIELD(rate),
	  .offset_words = 13,
	  .offset_bits  = 26,
	  .size_bits    = 6 },
	{ PATH_REC_FIELD(packet_life_time_selector),
	  .offset_words = 14,
	  .offset_bits  = 0,
	  .size_bits    = 2 },
	{ PATH_REC_FIELD(packet_life_time),
	  .offset_words = 14,
	  .offset_bits  = 2,
	  .size_bits    = 6 },
	{ PATH_REC_FIELD(preference),
	  .offset_words = 14,
	  .offset_bits  = 8,
	  .size_bits    = 8 },
	{ RESERVED,
	  .offset_words = 14,
	  .offset_bits  = 16,
	  .size_bits    = 48 },
};

#define MCMEMBER_REC_FIELD(field) \
	.struct_offset_bytes = offsetof(struct ib_sa_mcmember_rec, field),	\
	.struct_size_bytes   = sizeof ((struct ib_sa_mcmember_rec *) 0)->field,	\
	.field_name          = "sa_mcmember_rec:" #field

static const struct ib_field mcmember_rec_table[] = {
	{ MCMEMBER_REC_FIELD(mgid),
	  .offset_words = 0,
	  .offset_bits  = 0,
	  .size_bits    = 128 },
	{ MCMEMBER_REC_FIELD(port_gid),
	  .offset_words = 4,
	  .offset_bits  = 0,
	  .size_bits    = 128 },
	{ MCMEMBER_REC_FIELD(qkey),
	  .offset_words = 8,
	  .offset_bits  = 0,
	  .size_bits    = 32 },
	{ MCMEMBER_REC_FIELD(mlid),
	  .offset_words = 9,
	  .offset_bits  = 0,
	  .size_bits    = 16 },
	{ MCMEMBER_REC_FIELD(mtu_selector),
	  .offset_words = 9,
	  .offset_bits  = 16,
	  .size_bits    = 2 },
	{ MCMEMBER_REC_FIELD(mtu),
	  .offset_words = 9,
	  .offset_bits  = 18,
	  .size_bits    = 6 },
	{ MCMEMBER_REC_FIELD(traffic_class),
	  .offset_words = 9,
	  .offset_bits  = 24,
	  .size_bits    = 8 },
	{ MCMEMBER_REC_FIELD(pkey),
	  .offset_words = 10,
	  .offset_bits  = 0,
	  .size_bits    = 16 },
	{ MCMEMBER_REC_FIELD(rate_selector),
	  .offset_words = 10,
	  .offset_bits  = 16,
	  .size_bits    = 2 },
	{ MCMEMBER_REC_FIELD(rate),
	  .offset_words = 10,
	  .offset_bits  = 18,
	  .size_bits    = 6 },
	{ MCMEMBER_REC_FIELD(packet_life_time_selector),
	  .offset_words = 10,
	  .offset_bits  = 24,
	  .size_bits    = 2 },
	{ MCMEMBER_REC_FIELD(packet_life_time),
	  .offset_words = 10,
	  .offset_bits  = 26,
	  .size_bits    = 6 },
	{ MCMEMBER_REC_FIELD(sl),
	  .offset_words = 11,
	  .offset_bits  = 0,
	  .size_bits    = 4 },
	{ MCMEMBER_REC_FIELD(flow_label),
	  .offset_words = 11,
	  .offset_bits  = 4,
	  .size_bits    = 20 },
	{ MCMEMBER_REC_FIELD(hop_limit),
	  .offset_words = 11,
	  .offset_bits  = 24,
	  .size_bits    = 8 },
	{ MCMEMBER_REC_FIELD(scope),
	  .offset_words = 12,
	  .offset_bits  = 0,
	  .size_bits    = 4 },
	{ MCMEMBER_REC_FIELD(join_state),
	  .offset_words = 12,
	  .offset_bits  = 4,
	  .size_bits    = 4 },
	{ MCMEMBER_REC_FIELD(proxy_join),
	  .offset_words = 12,
	  .offset_bits  = 8,
	  .size_bits    = 1 },
	{ RESERVED,
	  .offset_words = 12,
	  .offset_bits  = 9,
	  .size_bits    = 23 },
};

#define SERVICE_REC_FIELD(field) \
	.struct_offset_bytes = offsetof(struct ib_sa_service_rec, field),	\
	.struct_size_bytes   = sizeof ((struct ib_sa_service_rec *) 0)->field,	\
	.field_name          = "sa_service_rec:" #field

static const struct ib_field service_rec_table[] = {
	{ SERVICE_REC_FIELD(id),
	  .offset_words = 0,
	  .offset_bits  = 0,
	  .size_bits    = 64 },
	{ SERVICE_REC_FIELD(gid),
	  .offset_words = 2,
	  .offset_bits  = 0,
	  .size_bits    = 128 },
	{ SERVICE_REC_FIELD(pkey),
	  .offset_words = 6,
	  .offset_bits  = 0,
	  .size_bits    = 16 },
	{ SERVICE_REC_FIELD(lease),
	  .offset_words = 7,
	  .offset_bits  = 0,
	  .size_bits    = 32 },
	{ SERVICE_REC_FIELD(key),
	  .offset_words = 8,
	  .offset_bits  = 0,
	  .size_bits    = 128 },
	{ SERVICE_REC_FIELD(name),
	  .offset_words = 12,
	  .offset_bits  = 0,
	  .size_bits    = 64*8 },
	{ SERVICE_REC_FIELD(data8),
	  .offset_words = 28,
	  .offset_bits  = 0,
	  .size_bits    = 16*8 },
	{ SERVICE_REC_FIELD(data16),
	  .offset_words = 32,
	  .offset_bits  = 0,
	  .size_bits    = 8*16 },
	{ SERVICE_REC_FIELD(data32),
	  .offset_words = 36,
	  .offset_bits  = 0,
	  .size_bits    = 4*32 },
	{ SERVICE_REC_FIELD(data64),
	  .offset_words = 40,
	  .offset_bits  = 0,
	  .size_bits    = 2*64 },
};

#define CLASSPORTINFO_REC_FIELD(field) \
	.struct_offset_bytes = offsetof(struct ib_class_port_info, field),	\
	.struct_size_bytes   = sizeof((struct ib_class_port_info *)0)->field,	\
	.field_name          = "ib_class_port_info:" #field

static const struct ib_field classport_info_rec_table[] = {
	{ CLASSPORTINFO_REC_FIELD(base_version),
	  .offset_words = 0,
	  .offset_bits  = 0,
	  .size_bits    = 8 },
	{ CLASSPORTINFO_REC_FIELD(class_version),
	  .offset_words = 0,
	  .offset_bits  = 8,
	  .size_bits    = 8 },
	{ CLASSPORTINFO_REC_FIELD(capability_mask),
	  .offset_words = 0,
	  .offset_bits  = 16,
	  .size_bits    = 16 },
	{ CLASSPORTINFO_REC_FIELD(cap_mask2_resp_time),
	  .offset_words = 1,
	  .offset_bits  = 0,
	  .size_bits    = 32 },
	{ CLASSPORTINFO_REC_FIELD(redirect_gid),
	  .offset_words = 2,
	  .offset_bits  = 0,
	  .size_bits    = 128 },
	{ CLASSPORTINFO_REC_FIELD(redirect_tcslfl),
	  .offset_words = 6,
	  .offset_bits  = 0,
	  .size_bits    = 32 },
	{ CLASSPORTINFO_REC_FIELD(redirect_lid),
	  .offset_words = 7,
	  .offset_bits  = 0,
	  .size_bits    = 16 },
	{ CLASSPORTINFO_REC_FIELD(redirect_pkey),
	  .offset_words = 7,
	  .offset_bits  = 16,
	  .size_bits    = 16 },

	{ CLASSPORTINFO_REC_FIELD(redirect_qp),
	  .offset_words = 8,
	  .offset_bits  = 0,
	  .size_bits    = 32 },
	{ CLASSPORTINFO_REC_FIELD(redirect_qkey),
	  .offset_words = 9,
	  .offset_bits  = 0,
	  .size_bits    = 32 },

	{ CLASSPORTINFO_REC_FIELD(trap_gid),
	  .offset_words = 10,
	  .offset_bits  = 0,
	  .size_bits    = 128 },
	{ CLASSPORTINFO_REC_FIELD(trap_tcslfl),
	  .offset_words = 14,
	  .offset_bits  = 0,
	  .size_bits    = 32 },

	{ CLASSPORTINFO_REC_FIELD(trap_lid),
	  .offset_words = 15,
	  .offset_bits  = 0,
	  .size_bits    = 16 },
	{ CLASSPORTINFO_REC_FIELD(trap_pkey),
	  .offset_words = 15,
	  .offset_bits  = 16,
	  .size_bits    = 16 },

	{ CLASSPORTINFO_REC_FIELD(trap_hlqp),
	  .offset_words = 16,
	  .offset_bits  = 0,
	  .size_bits    = 32 },
	{ CLASSPORTINFO_REC_FIELD(trap_qkey),
	  .offset_words = 17,
	  .offset_bits  = 0,
	  .size_bits    = 32 },
};

#define GUIDINFO_REC_FIELD(field) \
	.struct_offset_bytes = offsetof(struct ib_sa_guidinfo_rec, field),	\
	.struct_size_bytes   = sizeof((struct ib_sa_guidinfo_rec *) 0)->field,	\
	.field_name          = "sa_guidinfo_rec:" #field

static const struct ib_field guidinfo_rec_table[] = {
	{ GUIDINFO_REC_FIELD(lid),
	  .offset_words = 0,
	  .offset_bits  = 0,
	  .size_bits    = 16 },
	{ GUIDINFO_REC_FIELD(block_num),
	  .offset_words = 0,
	  .offset_bits  = 16,
	  .size_bits    = 8 },
	{ GUIDINFO_REC_FIELD(res1),
	  .offset_words = 0,
	  .offset_bits  = 24,
	  .size_bits    = 8 },
	{ GUIDINFO_REC_FIELD(res2),
	  .offset_words = 1,
	  .offset_bits  = 0,
	  .size_bits    = 32 },
	{ GUIDINFO_REC_FIELD(guid_info_list),
	  .offset_words = 2,
	  .offset_bits  = 0,
	  .size_bits    = 512 },
};

static inline void ib_sa_disable_local_svc(struct ib_sa_query *query)
{
	query->flags &= ~IB_SA_ENABLE_LOCAL_SERVICE;
}

static inline int ib_sa_query_cancelled(struct ib_sa_query *query)
{
	return (query->flags & IB_SA_CANCEL);
}

static void ib_nl_set_path_rec_attrs(struct sk_buff *skb,
				     struct ib_sa_query *query)
{
	struct ib_sa_path_rec *sa_rec = query->mad_buf->context[1];
	struct ib_sa_mad *mad = query->mad_buf->mad;
	ib_sa_comp_mask comp_mask = mad->sa_hdr.comp_mask;
	u16 val16;
	u64 val64;
	struct rdma_ls_resolve_header *header;

	query->mad_buf->context[1] = NULL;

	/* Construct the family header first */
	header = (struct rdma_ls_resolve_header *)
		skb_put(skb, NLMSG_ALIGN(sizeof(*header)));
	memcpy(header->device_name, query->port->agent->device->name,
	       LS_DEVICE_NAME_MAX);
	header->port_num = query->port->port_num;

	if ((comp_mask & IB_SA_PATH_REC_REVERSIBLE) &&
	    sa_rec->reversible != 0)
		query->path_use = LS_RESOLVE_PATH_USE_GMP;
	else
		query->path_use = LS_RESOLVE_PATH_USE_UNIDIRECTIONAL;
	header->path_use = query->path_use;

	/* Now build the attributes */
	if (comp_mask & IB_SA_PATH_REC_SERVICE_ID) {
		val64 = be64_to_cpu(sa_rec->service_id);
		nla_put(skb, RDMA_NLA_F_MANDATORY | LS_NLA_TYPE_SERVICE_ID,
			sizeof(val64), &val64);
	}
	if (comp_mask & IB_SA_PATH_REC_DGID)
		nla_put(skb, RDMA_NLA_F_MANDATORY | LS_NLA_TYPE_DGID,
			sizeof(sa_rec->dgid), &sa_rec->dgid);
	if (comp_mask & IB_SA_PATH_REC_SGID)
		nla_put(skb, RDMA_NLA_F_MANDATORY | LS_NLA_TYPE_SGID,
			sizeof(sa_rec->sgid), &sa_rec->sgid);
	if (comp_mask & IB_SA_PATH_REC_TRAFFIC_CLASS)
		nla_put(skb, RDMA_NLA_F_MANDATORY | LS_NLA_TYPE_TCLASS,
			sizeof(sa_rec->traffic_class), &sa_rec->traffic_class);

	if (comp_mask & IB_SA_PATH_REC_PKEY) {
		val16 = be16_to_cpu(sa_rec->pkey);
		nla_put(skb, RDMA_NLA_F_MANDATORY | LS_NLA_TYPE_PKEY,
			sizeof(val16), &val16);
	}
	if (comp_mask & IB_SA_PATH_REC_QOS_CLASS) {
		val16 = be16_to_cpu(sa_rec->qos_class);
		nla_put(skb, RDMA_NLA_F_MANDATORY | LS_NLA_TYPE_QOS_CLASS,
			sizeof(val16), &val16);
	}
}

static int ib_nl_get_path_rec_attrs_len(ib_sa_comp_mask comp_mask)
{
	int len = 0;

	if (comp_mask & IB_SA_PATH_REC_SERVICE_ID)
		len += nla_total_size(sizeof(u64));
	if (comp_mask & IB_SA_PATH_REC_DGID)
		len += nla_total_size(sizeof(struct rdma_nla_ls_gid));
	if (comp_mask & IB_SA_PATH_REC_SGID)
		len += nla_total_size(sizeof(struct rdma_nla_ls_gid));
	if (comp_mask & IB_SA_PATH_REC_TRAFFIC_CLASS)
		len += nla_total_size(sizeof(u8));
	if (comp_mask & IB_SA_PATH_REC_PKEY)
		len += nla_total_size(sizeof(u16));
	if (comp_mask & IB_SA_PATH_REC_QOS_CLASS)
		len += nla_total_size(sizeof(u16));

	/*
	 * Make sure that at least some of the required comp_mask bits are
	 * set.
	 */
	if (WARN_ON(len == 0))
		return len;

	/* Add the family header */
	len += NLMSG_ALIGN(sizeof(struct rdma_ls_resolve_header));

	return len;
}

static int ib_nl_send_msg(struct ib_sa_query *query, gfp_t gfp_mask)
{
	struct sk_buff *skb = NULL;
	struct nlmsghdr *nlh;
	void *data;
	int ret = 0;
	struct ib_sa_mad *mad;
	int len;

	mad = query->mad_buf->mad;
	len = ib_nl_get_path_rec_attrs_len(mad->sa_hdr.comp_mask);
	if (len <= 0)
		return -EMSGSIZE;

	skb = nlmsg_new(len, gfp_mask);
	if (!skb)
		return -ENOMEM;

	/* Put nlmsg header only for now */
	data = ibnl_put_msg(skb, &nlh, query->seq, 0, RDMA_NL_LS,
			    RDMA_NL_LS_OP_RESOLVE, NLM_F_REQUEST);
	if (!data) {
		nlmsg_free(skb);
		return -EMSGSIZE;
	}

	/* Add attributes */
	ib_nl_set_path_rec_attrs(skb, query);

	/* Repair the nlmsg header length */
	nlmsg_end(skb, nlh);

	ret = ibnl_multicast(skb, nlh, RDMA_NL_GROUP_LS, gfp_mask);
	if (!ret)
		ret = len;
	else
		ret = 0;

	return ret;
}

static int ib_nl_make_request(struct ib_sa_query *query, gfp_t gfp_mask)
{
	unsigned long flags;
	unsigned long delay;
	int ret;

	INIT_LIST_HEAD(&query->list);
	query->seq = (u32)atomic_inc_return(&ib_nl_sa_request_seq);

	/* Put the request on the list first.*/
	spin_lock_irqsave(&ib_nl_request_lock, flags);
	delay = msecs_to_jiffies(sa_local_svc_timeout_ms);
	query->timeout = delay + jiffies;
	list_add_tail(&query->list, &ib_nl_request_list);
	/* Start the timeout if this is the only request */
	if (ib_nl_request_list.next == &query->list)
		queue_delayed_work(ib_nl_wq, &ib_nl_timed_work, delay);
	spin_unlock_irqrestore(&ib_nl_request_lock, flags);

	ret = ib_nl_send_msg(query, gfp_mask);
	if (ret <= 0) {
		ret = -EIO;
		/* Remove the request */
		spin_lock_irqsave(&ib_nl_request_lock, flags);
		list_del(&query->list);
		spin_unlock_irqrestore(&ib_nl_request_lock, flags);
	} else {
		ret = 0;
	}

	return ret;
}

static int ib_nl_cancel_request(struct ib_sa_query *query)
{
	unsigned long flags;
	struct ib_sa_query *wait_query;
	int found = 0;

	spin_lock_irqsave(&ib_nl_request_lock, flags);
	list_for_each_entry(wait_query, &ib_nl_request_list, list) {
		/* Let the timeout to take care of the callback */
		if (query == wait_query) {
			query->flags |= IB_SA_CANCEL;
			query->timeout = jiffies;
			list_move(&query->list, &ib_nl_request_list);
			found = 1;
			mod_delayed_work(ib_nl_wq, &ib_nl_timed_work, 1);
			break;
		}
	}
	spin_unlock_irqrestore(&ib_nl_request_lock, flags);

	return found;
}

static void send_handler(struct ib_mad_agent *agent,
			 struct ib_mad_send_wc *mad_send_wc);

static void ib_nl_process_good_resolve_rsp(struct ib_sa_query *query,
					   const struct nlmsghdr *nlh)
{
	struct ib_mad_send_wc mad_send_wc;
	struct ib_sa_mad *mad = NULL;
	const struct nlattr *head, *curr;
	struct ib_path_rec_data  *rec;
	int len, rem;
	u32 mask = 0;
	int status = -EIO;

	if (query->callback) {
		head = (const struct nlattr *) nlmsg_data(nlh);
		len = nlmsg_len(nlh);
		switch (query->path_use) {
		case LS_RESOLVE_PATH_USE_UNIDIRECTIONAL:
			mask = IB_PATH_PRIMARY | IB_PATH_OUTBOUND;
			break;

		case LS_RESOLVE_PATH_USE_ALL:
		case LS_RESOLVE_PATH_USE_GMP:
		default:
			mask = IB_PATH_PRIMARY | IB_PATH_GMP |
				IB_PATH_BIDIRECTIONAL;
			break;
		}
		nla_for_each_attr(curr, head, len, rem) {
			if (curr->nla_type == LS_NLA_TYPE_PATH_RECORD) {
				rec = nla_data(curr);
				/*
				 * Get the first one. In the future, we may
				 * need to get up to 6 pathrecords.
				 */
				if ((rec->flags & mask) == mask) {
					mad = query->mad_buf->mad;
					mad->mad_hdr.method |=
						IB_MGMT_METHOD_RESP;
					memcpy(mad->data, rec->path_rec,
					       sizeof(rec->path_rec));
					status = 0;
					break;
				}
			}
		}
		query->callback(query, status, mad);
	}

	mad_send_wc.send_buf = query->mad_buf;
	mad_send_wc.status = IB_WC_SUCCESS;
	send_handler(query->mad_buf->mad_agent, &mad_send_wc);
}

static void ib_nl_request_timeout(struct work_struct *work)
{
	unsigned long flags;
	struct ib_sa_query *query;
	unsigned long delay;
	struct ib_mad_send_wc mad_send_wc;
	int ret;

	spin_lock_irqsave(&ib_nl_request_lock, flags);
	while (!list_empty(&ib_nl_request_list)) {
		query = list_entry(ib_nl_request_list.next,
				   struct ib_sa_query, list);

		if (time_after(query->timeout, jiffies)) {
			delay = query->timeout - jiffies;
			if ((long)delay <= 0)
				delay = 1;
			queue_delayed_work(ib_nl_wq, &ib_nl_timed_work, delay);
			break;
		}

		list_del(&query->list);
		ib_sa_disable_local_svc(query);
		/* Hold the lock to protect against query cancellation */
		if (ib_sa_query_cancelled(query))
			ret = -1;
		else
			ret = ib_post_send_mad(query->mad_buf, NULL);
		if (ret) {
			mad_send_wc.send_buf = query->mad_buf;
			mad_send_wc.status = IB_WC_WR_FLUSH_ERR;
			spin_unlock_irqrestore(&ib_nl_request_lock, flags);
			send_handler(query->port->agent, &mad_send_wc);
			spin_lock_irqsave(&ib_nl_request_lock, flags);
		}
	}
	spin_unlock_irqrestore(&ib_nl_request_lock, flags);
}

int ib_nl_handle_set_timeout(struct sk_buff *skb,
			     struct netlink_callback *cb)
{
	const struct nlmsghdr *nlh = (struct nlmsghdr *)cb->nlh;
	int timeout, delta, abs_delta;
	const struct nlattr *attr;
	unsigned long flags;
	struct ib_sa_query *query;
	long delay = 0;
	struct nlattr *tb[LS_NLA_TYPE_MAX];
	int ret;

	if (!(nlh->nlmsg_flags & NLM_F_REQUEST) ||
	    !(NETLINK_CB(skb).sk) ||
	    !netlink_capable(skb, CAP_NET_ADMIN))
		return -EPERM;

	ret = nla_parse(tb, LS_NLA_TYPE_MAX - 1, nlmsg_data(nlh),
			nlmsg_len(nlh), ib_nl_policy);
	attr = (const struct nlattr *)tb[LS_NLA_TYPE_TIMEOUT];
	if (ret || !attr)
		goto settimeout_out;

	timeout = *(int *) nla_data(attr);
	if (timeout < IB_SA_LOCAL_SVC_TIMEOUT_MIN)
		timeout = IB_SA_LOCAL_SVC_TIMEOUT_MIN;
	if (timeout > IB_SA_LOCAL_SVC_TIMEOUT_MAX)
		timeout = IB_SA_LOCAL_SVC_TIMEOUT_MAX;

	delta = timeout - sa_local_svc_timeout_ms;
	if (delta < 0)
		abs_delta = -delta;
	else
		abs_delta = delta;

	if (delta != 0) {
		spin_lock_irqsave(&ib_nl_request_lock, flags);
		sa_local_svc_timeout_ms = timeout;
		list_for_each_entry(query, &ib_nl_request_list, list) {
			if (delta < 0 && abs_delta > query->timeout)
				query->timeout = 0;
			else
				query->timeout += delta;

			/* Get the new delay from the first entry */
			if (!delay) {
				delay = query->timeout - jiffies;
				if (delay <= 0)
					delay = 1;
			}
		}
		if (delay)
			mod_delayed_work(ib_nl_wq, &ib_nl_timed_work,
					 (unsigned long)delay);
		spin_unlock_irqrestore(&ib_nl_request_lock, flags);
	}

settimeout_out:
	return skb->len;
}

static inline int ib_nl_is_good_resolve_resp(const struct nlmsghdr *nlh)
{
	struct nlattr *tb[LS_NLA_TYPE_MAX];
	int ret;

	if (nlh->nlmsg_flags & RDMA_NL_LS_F_ERR)
		return 0;

	ret = nla_parse(tb, LS_NLA_TYPE_MAX - 1, nlmsg_data(nlh),
			nlmsg_len(nlh), ib_nl_policy);
	if (ret)
		return 0;

	return 1;
}

int ib_nl_handle_resolve_resp(struct sk_buff *skb,
			      struct netlink_callback *cb)
{
	const struct nlmsghdr *nlh = (struct nlmsghdr *)cb->nlh;
	unsigned long flags;
	struct ib_sa_query *query;
	struct ib_mad_send_buf *send_buf;
	struct ib_mad_send_wc mad_send_wc;
	int found = 0;
	int ret;

	if ((nlh->nlmsg_flags & NLM_F_REQUEST) ||
	    !(NETLINK_CB(skb).sk) ||
	    !netlink_capable(skb, CAP_NET_ADMIN))
		return -EPERM;

	spin_lock_irqsave(&ib_nl_request_lock, flags);
	list_for_each_entry(query, &ib_nl_request_list, list) {
		/*
		 * If the query is cancelled, let the timeout routine
		 * take care of it.
		 */
		if (nlh->nlmsg_seq == query->seq) {
			found = !ib_sa_query_cancelled(query);
			if (found)
				list_del(&query->list);
			break;
		}
	}

	if (!found) {
		spin_unlock_irqrestore(&ib_nl_request_lock, flags);
		goto resp_out;
	}

	send_buf = query->mad_buf;

	if (!ib_nl_is_good_resolve_resp(nlh)) {
		/* if the result is a failure, send out the packet via IB */
		ib_sa_disable_local_svc(query);
		ret = ib_post_send_mad(query->mad_buf, NULL);
		spin_unlock_irqrestore(&ib_nl_request_lock, flags);
		if (ret) {
			mad_send_wc.send_buf = send_buf;
			mad_send_wc.status = IB_WC_GENERAL_ERR;
			send_handler(query->port->agent, &mad_send_wc);
		}
	} else {
		spin_unlock_irqrestore(&ib_nl_request_lock, flags);
		ib_nl_process_good_resolve_rsp(query, nlh);
	}

resp_out:
	return skb->len;
}

static void free_sm_ah(struct kref *kref)
{
	struct ib_sa_sm_ah *sm_ah = container_of(kref, struct ib_sa_sm_ah, ref);

	ib_destroy_ah(sm_ah->ah);
	kfree(sm_ah);
}

static void update_sm_ah(struct work_struct *work)
{
	struct ib_sa_port *port =
		container_of(work, struct ib_sa_port, update_task);
	struct ib_sa_sm_ah *new_ah;
	struct ib_port_attr port_attr;
	struct ib_ah_attr   ah_attr;

	if (ib_query_port(port->agent->device, port->port_num, &port_attr)) {
		pr_warn("Couldn't query port\n");
		return;
	}

	new_ah = kmalloc(sizeof *new_ah, GFP_KERNEL);
	if (!new_ah) {
		return;
	}

	kref_init(&new_ah->ref);
	new_ah->src_path_mask = (1 << port_attr.lmc) - 1;

	new_ah->pkey_index = 0;
	if (ib_find_pkey(port->agent->device, port->port_num,
			 IB_DEFAULT_PKEY_FULL, &new_ah->pkey_index))
		pr_err("Couldn't find index for default PKey\n");

	memset(&ah_attr, 0, sizeof ah_attr);
	ah_attr.dlid     = port_attr.sm_lid;
	ah_attr.sl       = port_attr.sm_sl;
	ah_attr.port_num = port->port_num;
	if (port_attr.grh_required) {
		ah_attr.ah_flags = IB_AH_GRH;
		ah_attr.grh.dgid.global.subnet_prefix = cpu_to_be64(port_attr.subnet_prefix);
		ah_attr.grh.dgid.global.interface_id = cpu_to_be64(IB_SA_WELL_KNOWN_GUID);
	}

	new_ah->ah = ib_create_ah(port->agent->qp->pd, &ah_attr);
	if (IS_ERR(new_ah->ah)) {
		pr_warn("Couldn't create new SM AH\n");
		kfree(new_ah);
		return;
	}

	spin_lock_irq(&port->ah_lock);
	if (port->sm_ah)
		kref_put(&port->sm_ah->ref, free_sm_ah);
	port->sm_ah = new_ah;
	spin_unlock_irq(&port->ah_lock);

}

static void ib_sa_event(struct ib_event_handler *handler, struct ib_event *event)
{
	if (event->event == IB_EVENT_PORT_ERR    ||
	    event->event == IB_EVENT_PORT_ACTIVE ||
	    event->event == IB_EVENT_LID_CHANGE  ||
	    event->event == IB_EVENT_PKEY_CHANGE ||
	    event->event == IB_EVENT_SM_CHANGE   ||
	    event->event == IB_EVENT_CLIENT_REREGISTER) {
		unsigned long flags;
		struct ib_sa_device *sa_dev =
			container_of(handler, typeof(*sa_dev), event_handler);
		struct ib_sa_port *port =
			&sa_dev->port[event->element.port_num - sa_dev->start_port];

		if (!rdma_cap_ib_sa(handler->device, port->port_num))
			return;

		spin_lock_irqsave(&port->ah_lock, flags);
		if (port->sm_ah)
			kref_put(&port->sm_ah->ref, free_sm_ah);
		port->sm_ah = NULL;
		spin_unlock_irqrestore(&port->ah_lock, flags);

		if (event->event == IB_EVENT_SM_CHANGE ||
		    event->event == IB_EVENT_CLIENT_REREGISTER ||
		    event->event == IB_EVENT_LID_CHANGE) {
			spin_lock_irqsave(&port->classport_lock, flags);
			port->classport_info.valid = false;
			spin_unlock_irqrestore(&port->classport_lock, flags);
		}
		queue_work(ib_wq, &sa_dev->port[event->element.port_num -
					    sa_dev->start_port].update_task);
	}
}

void ib_sa_register_client(struct ib_sa_client *client)
{
	atomic_set(&client->users, 1);
	init_completion(&client->comp);
}
EXPORT_SYMBOL(ib_sa_register_client);

void ib_sa_unregister_client(struct ib_sa_client *client)
{
	ib_sa_client_put(client);
	wait_for_completion(&client->comp);
}
EXPORT_SYMBOL(ib_sa_unregister_client);

/**
 * ib_sa_cancel_query - try to cancel an SA query
 * @id:ID of query to cancel
 * @query:query pointer to cancel
 *
 * Try to cancel an SA query.  If the id and query don't match up or
 * the query has already completed, nothing is done.  Otherwise the
 * query is canceled and will complete with a status of -EINTR.
 */
void ib_sa_cancel_query(int id, struct ib_sa_query *query)
{
	unsigned long flags;
	struct ib_mad_agent *agent;
	struct ib_mad_send_buf *mad_buf;

	spin_lock_irqsave(&idr_lock, flags);
	if (idr_find(&query_idr, id) != query) {
		spin_unlock_irqrestore(&idr_lock, flags);
		return;
	}
	agent = query->port->agent;
	mad_buf = query->mad_buf;
	spin_unlock_irqrestore(&idr_lock, flags);

	/*
	 * If the query is still on the netlink request list, schedule
	 * it to be cancelled by the timeout routine. Otherwise, it has been
	 * sent to the MAD layer and has to be cancelled from there.
	 */
	if (!ib_nl_cancel_request(query))
		ib_cancel_mad(agent, mad_buf);
}
EXPORT_SYMBOL(ib_sa_cancel_query);

static u8 get_src_path_mask(struct ib_device *device, u8 port_num)
{
	struct ib_sa_device *sa_dev;
	struct ib_sa_port   *port;
	unsigned long flags;
	u8 src_path_mask;

	sa_dev = ib_get_client_data(device, &sa_client);
	if (!sa_dev)
		return 0x7f;

	port  = &sa_dev->port[port_num - sa_dev->start_port];
	spin_lock_irqsave(&port->ah_lock, flags);
	src_path_mask = port->sm_ah ? port->sm_ah->src_path_mask : 0x7f;
	spin_unlock_irqrestore(&port->ah_lock, flags);

	return src_path_mask;
}

int ib_init_ah_from_path(struct ib_device *device, u8 port_num,
			 struct ib_sa_path_rec *rec, struct ib_ah_attr *ah_attr)
{
	int ret;
	u16 gid_index;
	int use_roce;
	struct net_device *ndev = NULL;

	memset(ah_attr, 0, sizeof *ah_attr);
	ah_attr->dlid = be16_to_cpu(rec->dlid);
	ah_attr->sl = rec->sl;
	ah_attr->src_path_bits = be16_to_cpu(rec->slid) &
				 get_src_path_mask(device, port_num);
	ah_attr->port_num = port_num;
	ah_attr->static_rate = rec->rate;

	use_roce = rdma_cap_eth_ah(device, port_num);

	if (use_roce) {
		struct net_device *idev;
		struct net_device *resolved_dev;
		struct rdma_dev_addr dev_addr = {.bound_dev_if = rec->ifindex,
						 .net = rec->net ? rec->net :
							 &init_net};
		union {
			struct sockaddr     _sockaddr;
			struct sockaddr_in  _sockaddr_in;
			struct sockaddr_in6 _sockaddr_in6;
		} sgid_addr, dgid_addr;

		if (!device->get_netdev)
			return -EOPNOTSUPP;

		rdma_gid2ip(&sgid_addr._sockaddr, &rec->sgid);
		rdma_gid2ip(&dgid_addr._sockaddr, &rec->dgid);

		/* validate the route */
		ret = rdma_resolve_ip_route(&sgid_addr._sockaddr,
					    &dgid_addr._sockaddr, &dev_addr);
		if (ret)
			return ret;

		if ((dev_addr.network == RDMA_NETWORK_IPV4 ||
		     dev_addr.network == RDMA_NETWORK_IPV6) &&
		    rec->gid_type != IB_GID_TYPE_ROCE_UDP_ENCAP)
			return -EINVAL;

		idev = device->get_netdev(device, port_num);
		if (!idev)
			return -ENODEV;

		resolved_dev = dev_get_by_index(dev_addr.net,
						dev_addr.bound_dev_if);
		if (resolved_dev->flags & IFF_LOOPBACK) {
			dev_put(resolved_dev);
			resolved_dev = idev;
			dev_hold(resolved_dev);
		}
		ndev = ib_get_ndev_from_path(rec);
		rcu_read_lock();
		if ((ndev && ndev != resolved_dev) ||
		    (resolved_dev != idev &&
		     !rdma_is_upper_dev_rcu(idev, resolved_dev)))
			ret = -EHOSTUNREACH;
		rcu_read_unlock();
		dev_put(idev);
		dev_put(resolved_dev);
		if (ret) {
			if (ndev)
				dev_put(ndev);
			return ret;
		}
	}

	if (rec->hop_limit > 0 || use_roce) {
		ah_attr->ah_flags = IB_AH_GRH;
		ah_attr->grh.dgid = rec->dgid;

		ret = ib_find_cached_gid_by_port(device, &rec->sgid,
						 rec->gid_type, port_num, ndev,
						 &gid_index);
		if (ret) {
			if (ndev)
				dev_put(ndev);
			return ret;
		}

		ah_attr->grh.sgid_index    = gid_index;
		ah_attr->grh.flow_label    = be32_to_cpu(rec->flow_label);
		ah_attr->grh.hop_limit     = rec->hop_limit;
		ah_attr->grh.traffic_class = rec->traffic_class;
		if (ndev)
			dev_put(ndev);
	}

	if (use_roce)
		memcpy(ah_attr->dmac, rec->dmac, ETH_ALEN);

	return 0;
}
EXPORT_SYMBOL(ib_init_ah_from_path);

static int alloc_mad(struct ib_sa_query *query, gfp_t gfp_mask)
{
	unsigned long flags;

	spin_lock_irqsave(&query->port->ah_lock, flags);
	if (!query->port->sm_ah) {
		spin_unlock_irqrestore(&query->port->ah_lock, flags);
		return -EAGAIN;
	}
	kref_get(&query->port->sm_ah->ref);
	query->sm_ah = query->port->sm_ah;
	spin_unlock_irqrestore(&query->port->ah_lock, flags);

	query->mad_buf = ib_create_send_mad(query->port->agent, 1,
					    query->sm_ah->pkey_index,
					    0, IB_MGMT_SA_HDR, IB_MGMT_SA_DATA,
					    gfp_mask,
					    IB_MGMT_BASE_VERSION);
	if (IS_ERR(query->mad_buf)) {
		kref_put(&query->sm_ah->ref, free_sm_ah);
		return -ENOMEM;
	}

	query->mad_buf->ah = query->sm_ah->ah;

	return 0;
}

static void free_mad(struct ib_sa_query *query)
{
	ib_free_send_mad(query->mad_buf);
	kref_put(&query->sm_ah->ref, free_sm_ah);
}

static void init_mad(struct ib_sa_mad *mad, struct ib_mad_agent *agent)
{
	unsigned long flags;

	memset(mad, 0, sizeof *mad);

	mad->mad_hdr.base_version  = IB_MGMT_BASE_VERSION;
	mad->mad_hdr.mgmt_class    = IB_MGMT_CLASS_SUBN_ADM;
	mad->mad_hdr.class_version = IB_SA_CLASS_VERSION;

	spin_lock_irqsave(&tid_lock, flags);
	mad->mad_hdr.tid           =
		cpu_to_be64(((u64) agent->hi_tid) << 32 | tid++);
	spin_unlock_irqrestore(&tid_lock, flags);
}

static int send_mad(struct ib_sa_query *query, int timeout_ms, gfp_t gfp_mask)
{
	bool preload = gfpflags_allow_blocking(gfp_mask);
	unsigned long flags;
	int ret, id;

	if (preload)
		idr_preload(gfp_mask);
	spin_lock_irqsave(&idr_lock, flags);

	id = idr_alloc(&query_idr, query, 0, 0, GFP_NOWAIT);

	spin_unlock_irqrestore(&idr_lock, flags);
	if (preload)
		idr_preload_end();
	if (id < 0)
		return id;

	query->mad_buf->timeout_ms  = timeout_ms;
	query->mad_buf->context[0] = query;
	query->id = id;

	if (query->flags & IB_SA_ENABLE_LOCAL_SERVICE) {
		if (!ibnl_chk_listeners(RDMA_NL_GROUP_LS)) {
			if (!ib_nl_make_request(query, gfp_mask))
				return id;
		}
		ib_sa_disable_local_svc(query);
	}

	ret = ib_post_send_mad(query->mad_buf, NULL);
	if (ret) {
		spin_lock_irqsave(&idr_lock, flags);
		idr_remove(&query_idr, id);
		spin_unlock_irqrestore(&idr_lock, flags);
	}

	/*
	 * It's not safe to dereference query any more, because the
	 * send may already have completed and freed the query in
	 * another context.
	 */
	return ret ? ret : id;
}

void ib_sa_unpack_path(void *attribute, struct ib_sa_path_rec *rec)
{
	ib_unpack(path_rec_table, ARRAY_SIZE(path_rec_table), attribute, rec);
}
EXPORT_SYMBOL(ib_sa_unpack_path);

void ib_sa_pack_path(struct ib_sa_path_rec *rec, void *attribute)
{
	ib_pack(path_rec_table, ARRAY_SIZE(path_rec_table), rec, attribute);
}
EXPORT_SYMBOL(ib_sa_pack_path);

static void ib_sa_path_rec_callback(struct ib_sa_query *sa_query,
				    int status,
				    struct ib_sa_mad *mad)
{
	struct ib_sa_path_query *query =
		container_of(sa_query, struct ib_sa_path_query, sa_query);

	if (mad) {
		struct ib_sa_path_rec rec;

		ib_unpack(path_rec_table, ARRAY_SIZE(path_rec_table),
			  mad->data, &rec);
		rec.net = NULL;
		rec.ifindex = 0;
		rec.gid_type = IB_GID_TYPE_IB;
		eth_zero_addr(rec.dmac);
		query->callback(status, &rec, query->context);
	} else
		query->callback(status, NULL, query->context);
}

static void ib_sa_path_rec_release(struct ib_sa_query *sa_query)
{
	kfree(container_of(sa_query, struct ib_sa_path_query, sa_query));
}

/**
 * ib_sa_path_rec_get - Start a Path get query
 * @client:SA client
 * @device:device to send query on
 * @port_num: port number to send query on
 * @rec:Path Record to send in query
 * @comp_mask:component mask to send in query
 * @timeout_ms:time to wait for response
 * @gfp_mask:GFP mask to use for internal allocations
 * @callback:function called when query completes, times out or is
 * canceled
 * @context:opaque user context passed to callback
 * @sa_query:query context, used to cancel query
 *
 * Send a Path Record Get query to the SA to look up a path.  The
 * callback function will be called when the query completes (or
 * fails); status is 0 for a successful response, -EINTR if the query
 * is canceled, -ETIMEDOUT is the query timed out, or -EIO if an error
 * occurred sending the query.  The resp parameter of the callback is
 * only valid if status is 0.
 *
 * If the return value of ib_sa_path_rec_get() is negative, it is an
 * error code.  Otherwise it is a query ID that can be used to cancel
 * the query.
 */
int ib_sa_path_rec_get(struct ib_sa_client *client,
		       struct ib_device *device, u8 port_num,
		       struct ib_sa_path_rec *rec,
		       ib_sa_comp_mask comp_mask,
		       int timeout_ms, gfp_t gfp_mask,
		       void (*callback)(int status,
					struct ib_sa_path_rec *resp,
					void *context),
		       void *context,
		       struct ib_sa_query **sa_query)
{
	struct ib_sa_path_query *query;
	struct ib_sa_device *sa_dev = ib_get_client_data(device, &sa_client);
	struct ib_sa_port   *port;
	struct ib_mad_agent *agent;
	struct ib_sa_mad *mad;
	int ret;

	if (!sa_dev)
		return -ENODEV;

	port  = &sa_dev->port[port_num - sa_dev->start_port];
	agent = port->agent;

	query = kzalloc(sizeof(*query), gfp_mask);
	if (!query)
		return -ENOMEM;

	query->sa_query.port     = port;
	ret = alloc_mad(&query->sa_query, gfp_mask);
	if (ret)
		goto err1;

	ib_sa_client_get(client);
	query->sa_query.client = client;
	query->callback        = callback;
	query->context         = context;

	mad = query->sa_query.mad_buf->mad;
	init_mad(mad, agent);

	query->sa_query.callback = callback ? ib_sa_path_rec_callback : NULL;
	query->sa_query.release  = ib_sa_path_rec_release;
	mad->mad_hdr.method	 = IB_MGMT_METHOD_GET;
	mad->mad_hdr.attr_id	 = cpu_to_be16(IB_SA_ATTR_PATH_REC);
	mad->sa_hdr.comp_mask	 = comp_mask;

	ib_pack(path_rec_table, ARRAY_SIZE(path_rec_table), rec, mad->data);

	*sa_query = &query->sa_query;

	query->sa_query.flags |= IB_SA_ENABLE_LOCAL_SERVICE;
	query->sa_query.mad_buf->context[1] = rec;

	ret = send_mad(&query->sa_query, timeout_ms, gfp_mask);
	if (ret < 0)
		goto err2;

	return ret;

err2:
	*sa_query = NULL;
	ib_sa_client_put(query->sa_query.client);
	free_mad(&query->sa_query);

err1:
	kfree(query);
	return ret;
}
EXPORT_SYMBOL(ib_sa_path_rec_get);

static void ib_sa_service_rec_callback(struct ib_sa_query *sa_query,
				    int status,
				    struct ib_sa_mad *mad)
{
	struct ib_sa_service_query *query =
		container_of(sa_query, struct ib_sa_service_query, sa_query);

	if (mad) {
		struct ib_sa_service_rec rec;

		ib_unpack(service_rec_table, ARRAY_SIZE(service_rec_table),
			  mad->data, &rec);
		query->callback(status, &rec, query->context);
	} else
		query->callback(status, NULL, query->context);
}

static void ib_sa_service_rec_release(struct ib_sa_query *sa_query)
{
	kfree(container_of(sa_query, struct ib_sa_service_query, sa_query));
}

/**
 * ib_sa_service_rec_query - Start Service Record operation
 * @client:SA client
 * @device:device to send request on
 * @port_num: port number to send request on
 * @method:SA method - should be get, set, or delete
 * @rec:Service Record to send in request
 * @comp_mask:component mask to send in request
 * @timeout_ms:time to wait for response
 * @gfp_mask:GFP mask to use for internal allocations
 * @callback:function called when request completes, times out or is
 * canceled
 * @context:opaque user context passed to callback
 * @sa_query:request context, used to cancel request
 *
 * Send a Service Record set/get/delete to the SA to register,
 * unregister or query a service record.
 * The callback function will be called when the request completes (or
 * fails); status is 0 for a successful response, -EINTR if the query
 * is canceled, -ETIMEDOUT is the query timed out, or -EIO if an error
 * occurred sending the query.  The resp parameter of the callback is
 * only valid if status is 0.
 *
 * If the return value of ib_sa_service_rec_query() is negative, it is an
 * error code.  Otherwise it is a request ID that can be used to cancel
 * the query.
 */
int ib_sa_service_rec_query(struct ib_sa_client *client,
			    struct ib_device *device, u8 port_num, u8 method,
			    struct ib_sa_service_rec *rec,
			    ib_sa_comp_mask comp_mask,
			    int timeout_ms, gfp_t gfp_mask,
			    void (*callback)(int status,
					     struct ib_sa_service_rec *resp,
					     void *context),
			    void *context,
			    struct ib_sa_query **sa_query)
{
	struct ib_sa_service_query *query;
	struct ib_sa_device *sa_dev = ib_get_client_data(device, &sa_client);
	struct ib_sa_port   *port;
	struct ib_mad_agent *agent;
	struct ib_sa_mad *mad;
	int ret;

	if (!sa_dev)
		return -ENODEV;

	port  = &sa_dev->port[port_num - sa_dev->start_port];
	agent = port->agent;

	if (method != IB_MGMT_METHOD_GET &&
	    method != IB_MGMT_METHOD_SET &&
	    method != IB_SA_METHOD_DELETE)
		return -EINVAL;

	query = kzalloc(sizeof(*query), gfp_mask);
	if (!query)
		return -ENOMEM;

	query->sa_query.port     = port;
	ret = alloc_mad(&query->sa_query, gfp_mask);
	if (ret)
		goto err1;

	ib_sa_client_get(client);
	query->sa_query.client = client;
	query->callback        = callback;
	query->context         = context;

	mad = query->sa_query.mad_buf->mad;
	init_mad(mad, agent);

	query->sa_query.callback = callback ? ib_sa_service_rec_callback : NULL;
	query->sa_query.release  = ib_sa_service_rec_release;
	mad->mad_hdr.method	 = method;
	mad->mad_hdr.attr_id	 = cpu_to_be16(IB_SA_ATTR_SERVICE_REC);
	mad->sa_hdr.comp_mask	 = comp_mask;

	ib_pack(service_rec_table, ARRAY_SIZE(service_rec_table),
		rec, mad->data);

	*sa_query = &query->sa_query;

	ret = send_mad(&query->sa_query, timeout_ms, gfp_mask);
	if (ret < 0)
		goto err2;

	return ret;

err2:
	*sa_query = NULL;
	ib_sa_client_put(query->sa_query.client);
	free_mad(&query->sa_query);

err1:
	kfree(query);
	return ret;
}
EXPORT_SYMBOL(ib_sa_service_rec_query);

static void ib_sa_mcmember_rec_callback(struct ib_sa_query *sa_query,
					int status,
					struct ib_sa_mad *mad)
{
	struct ib_sa_mcmember_query *query =
		container_of(sa_query, struct ib_sa_mcmember_query, sa_query);

	if (mad) {
		struct ib_sa_mcmember_rec rec;

		ib_unpack(mcmember_rec_table, ARRAY_SIZE(mcmember_rec_table),
			  mad->data, &rec);
		query->callback(status, &rec, query->context);
	} else
		query->callback(status, NULL, query->context);
}

static void ib_sa_mcmember_rec_release(struct ib_sa_query *sa_query)
{
	kfree(container_of(sa_query, struct ib_sa_mcmember_query, sa_query));
}

int ib_sa_mcmember_rec_query(struct ib_sa_client *client,
			     struct ib_device *device, u8 port_num,
			     u8 method,
			     struct ib_sa_mcmember_rec *rec,
			     ib_sa_comp_mask comp_mask,
			     int timeout_ms, gfp_t gfp_mask,
			     void (*callback)(int status,
					      struct ib_sa_mcmember_rec *resp,
					      void *context),
			     void *context,
			     struct ib_sa_query **sa_query)
{
	struct ib_sa_mcmember_query *query;
	struct ib_sa_device *sa_dev = ib_get_client_data(device, &sa_client);
	struct ib_sa_port   *port;
	struct ib_mad_agent *agent;
	struct ib_sa_mad *mad;
	int ret;

	if (!sa_dev)
		return -ENODEV;

	port  = &sa_dev->port[port_num - sa_dev->start_port];
	agent = port->agent;

	query = kzalloc(sizeof(*query), gfp_mask);
	if (!query)
		return -ENOMEM;

	query->sa_query.port     = port;
	ret = alloc_mad(&query->sa_query, gfp_mask);
	if (ret)
		goto err1;

	ib_sa_client_get(client);
	query->sa_query.client = client;
	query->callback        = callback;
	query->context         = context;

	mad = query->sa_query.mad_buf->mad;
	init_mad(mad, agent);

	query->sa_query.callback = callback ? ib_sa_mcmember_rec_callback : NULL;
	query->sa_query.release  = ib_sa_mcmember_rec_release;
	mad->mad_hdr.method	 = method;
	mad->mad_hdr.attr_id	 = cpu_to_be16(IB_SA_ATTR_MC_MEMBER_REC);
	mad->sa_hdr.comp_mask	 = comp_mask;

	ib_pack(mcmember_rec_table, ARRAY_SIZE(mcmember_rec_table),
		rec, mad->data);

	*sa_query = &query->sa_query;

	ret = send_mad(&query->sa_query, timeout_ms, gfp_mask);
	if (ret < 0)
		goto err2;

	return ret;

err2:
	*sa_query = NULL;
	ib_sa_client_put(query->sa_query.client);
	free_mad(&query->sa_query);

err1:
	kfree(query);
	return ret;
}

/* Support GuidInfoRecord */
static void ib_sa_guidinfo_rec_callback(struct ib_sa_query *sa_query,
					int status,
					struct ib_sa_mad *mad)
{
	struct ib_sa_guidinfo_query *query =
		container_of(sa_query, struct ib_sa_guidinfo_query, sa_query);

	if (mad) {
		struct ib_sa_guidinfo_rec rec;

		ib_unpack(guidinfo_rec_table, ARRAY_SIZE(guidinfo_rec_table),
			  mad->data, &rec);
		query->callback(status, &rec, query->context);
	} else
		query->callback(status, NULL, query->context);
}

static void ib_sa_guidinfo_rec_release(struct ib_sa_query *sa_query)
{
	kfree(container_of(sa_query, struct ib_sa_guidinfo_query, sa_query));
}

int ib_sa_guid_info_rec_query(struct ib_sa_client *client,
			      struct ib_device *device, u8 port_num,
			      struct ib_sa_guidinfo_rec *rec,
			      ib_sa_comp_mask comp_mask, u8 method,
			      int timeout_ms, gfp_t gfp_mask,
			      void (*callback)(int status,
					       struct ib_sa_guidinfo_rec *resp,
					       void *context),
			      void *context,
			      struct ib_sa_query **sa_query)
{
	struct ib_sa_guidinfo_query *query;
	struct ib_sa_device *sa_dev = ib_get_client_data(device, &sa_client);
	struct ib_sa_port *port;
	struct ib_mad_agent *agent;
	struct ib_sa_mad *mad;
	int ret;

	if (!sa_dev)
		return -ENODEV;

	if (method != IB_MGMT_METHOD_GET &&
	    method != IB_MGMT_METHOD_SET &&
	    method != IB_SA_METHOD_DELETE) {
		return -EINVAL;
	}

	port  = &sa_dev->port[port_num - sa_dev->start_port];
	agent = port->agent;

	query = kzalloc(sizeof(*query), gfp_mask);
	if (!query)
		return -ENOMEM;

	query->sa_query.port = port;
	ret = alloc_mad(&query->sa_query, gfp_mask);
	if (ret)
		goto err1;

	ib_sa_client_get(client);
	query->sa_query.client = client;
	query->callback        = callback;
	query->context         = context;

	mad = query->sa_query.mad_buf->mad;
	init_mad(mad, agent);

	query->sa_query.callback = callback ? ib_sa_guidinfo_rec_callback : NULL;
	query->sa_query.release  = ib_sa_guidinfo_rec_release;

	mad->mad_hdr.method	 = method;
	mad->mad_hdr.attr_id	 = cpu_to_be16(IB_SA_ATTR_GUID_INFO_REC);
	mad->sa_hdr.comp_mask	 = comp_mask;

	ib_pack(guidinfo_rec_table, ARRAY_SIZE(guidinfo_rec_table), rec,
		mad->data);

	*sa_query = &query->sa_query;

	ret = send_mad(&query->sa_query, timeout_ms, gfp_mask);
	if (ret < 0)
		goto err2;

	return ret;

err2:
	*sa_query = NULL;
	ib_sa_client_put(query->sa_query.client);
	free_mad(&query->sa_query);

err1:
	kfree(query);
	return ret;
}
EXPORT_SYMBOL(ib_sa_guid_info_rec_query);

/* Support get SA ClassPortInfo */
static void ib_sa_classport_info_rec_callback(struct ib_sa_query *sa_query,
					      int status,
					      struct ib_sa_mad *mad)
{
	unsigned long flags;
	struct ib_sa_classport_info_query *query =
		container_of(sa_query, struct ib_sa_classport_info_query, sa_query);

	if (mad) {
		struct ib_class_port_info rec;

		ib_unpack(classport_info_rec_table,
			  ARRAY_SIZE(classport_info_rec_table),
			  mad->data, &rec);

		spin_lock_irqsave(&sa_query->port->classport_lock, flags);
		if (!status && !sa_query->port->classport_info.valid) {
			memcpy(&sa_query->port->classport_info.data, &rec,
			       sizeof(sa_query->port->classport_info.data));

			sa_query->port->classport_info.valid = true;
		}
		spin_unlock_irqrestore(&sa_query->port->classport_lock, flags);

		query->callback(status, &rec, query->context);
	} else {
		query->callback(status, NULL, query->context);
	}
}

static void ib_sa_portclass_info_rec_release(struct ib_sa_query *sa_query)
{
	kfree(container_of(sa_query, struct ib_sa_classport_info_query,
			   sa_query));
}

int ib_sa_classport_info_rec_query(struct ib_sa_client *client,
				   struct ib_device *device, u8 port_num,
				   int timeout_ms, gfp_t gfp_mask,
				   void (*callback)(int status,
						    struct ib_class_port_info *resp,
						    void *context),
				   void *context,
				   struct ib_sa_query **sa_query)
{
	struct ib_sa_classport_info_query *query;
	struct ib_sa_device *sa_dev = ib_get_client_data(device, &sa_client);
	struct ib_sa_port *port;
	struct ib_mad_agent *agent;
	struct ib_sa_mad *mad;
	struct ib_class_port_info cached_class_port_info;
	int ret;
	unsigned long flags;

	if (!sa_dev)
		return -ENODEV;

	port  = &sa_dev->port[port_num - sa_dev->start_port];
	agent = port->agent;

	/* Use cached ClassPortInfo attribute if valid instead of sending mad */
	spin_lock_irqsave(&port->classport_lock, flags);
	if (port->classport_info.valid && callback) {
		memcpy(&cached_class_port_info, &port->classport_info.data,
		       sizeof(cached_class_port_info));
		spin_unlock_irqrestore(&port->classport_lock, flags);
		callback(0, &cached_class_port_info, context);
		return 0;
	}
	spin_unlock_irqrestore(&port->classport_lock, flags);

	query = kzalloc(sizeof(*query), gfp_mask);
	if (!query)
		return -ENOMEM;

	query->sa_query.port = port;
	ret = alloc_mad(&query->sa_query, gfp_mask);
	if (ret)
		goto err1;

	ib_sa_client_get(client);
	query->sa_query.client = client;
	query->callback        = callback;
	query->context         = context;

	mad = query->sa_query.mad_buf->mad;
	init_mad(mad, agent);

	query->sa_query.callback = callback ? ib_sa_classport_info_rec_callback : NULL;

	query->sa_query.release  = ib_sa_portclass_info_rec_release;
	/* support GET only */
	mad->mad_hdr.method	 = IB_MGMT_METHOD_GET;
	mad->mad_hdr.attr_id	 = cpu_to_be16(IB_SA_ATTR_CLASS_PORTINFO);
	mad->sa_hdr.comp_mask	 = 0;
	*sa_query = &query->sa_query;

	ret = send_mad(&query->sa_query, timeout_ms, gfp_mask);
	if (ret < 0)
		goto err2;

	return ret;

err2:
	*sa_query = NULL;
	ib_sa_client_put(query->sa_query.client);
	free_mad(&query->sa_query);

err1:
	kfree(query);
	return ret;
}
EXPORT_SYMBOL(ib_sa_classport_info_rec_query);

static void send_handler(struct ib_mad_agent *agent,
			 struct ib_mad_send_wc *mad_send_wc)
{
	struct ib_sa_query *query = mad_send_wc->send_buf->context[0];
	unsigned long flags;

	if (query->callback)
		switch (mad_send_wc->status) {
		case IB_WC_SUCCESS:
			/* No callback -- already got recv */
			break;
		case IB_WC_RESP_TIMEOUT_ERR:
			query->callback(query, -ETIMEDOUT, NULL);
			break;
		case IB_WC_WR_FLUSH_ERR:
			query->callback(query, -EINTR, NULL);
			break;
		default:
			query->callback(query, -EIO, NULL);
			break;
		}

	spin_lock_irqsave(&idr_lock, flags);
	idr_remove(&query_idr, query->id);
	spin_unlock_irqrestore(&idr_lock, flags);

	free_mad(query);
	ib_sa_client_put(query->client);
	query->release(query);
}

static void recv_handler(struct ib_mad_agent *mad_agent,
			 struct ib_mad_send_buf *send_buf,
			 struct ib_mad_recv_wc *mad_recv_wc)
{
	struct ib_sa_query *query;

	if (!send_buf)
		return;

	query = send_buf->context[0];
	if (query->callback) {
		if (mad_recv_wc->wc->status == IB_WC_SUCCESS)
			query->callback(query,
					mad_recv_wc->recv_buf.mad->mad_hdr.status ?
					-EINVAL : 0,
					(struct ib_sa_mad *) mad_recv_wc->recv_buf.mad);
		else
			query->callback(query, -EIO, NULL);
	}

	ib_free_recv_mad(mad_recv_wc);
}

static void ib_sa_add_one(struct ib_device *device)
{
	struct ib_sa_device *sa_dev;
	int s, e, i;
	int count = 0;

	s = rdma_start_port(device);
	e = rdma_end_port(device);

	sa_dev = kzalloc(sizeof *sa_dev +
			 (e - s + 1) * sizeof (struct ib_sa_port),
			 GFP_KERNEL);
	if (!sa_dev)
		return;

	sa_dev->start_port = s;
	sa_dev->end_port   = e;

	for (i = 0; i <= e - s; ++i) {
		spin_lock_init(&sa_dev->port[i].ah_lock);
		if (!rdma_cap_ib_sa(device, i + 1))
			continue;

		sa_dev->port[i].sm_ah    = NULL;
		sa_dev->port[i].port_num = i + s;

		spin_lock_init(&sa_dev->port[i].classport_lock);
		sa_dev->port[i].classport_info.valid = false;

		sa_dev->port[i].agent =
			ib_register_mad_agent(device, i + s, IB_QPT_GSI,
					      NULL, 0, send_handler,
					      recv_handler, sa_dev, 0);
		if (IS_ERR(sa_dev->port[i].agent))
			goto err;

		INIT_WORK(&sa_dev->port[i].update_task, update_sm_ah);

		count++;
	}

	if (!count)
		goto free;

	ib_set_client_data(device, &sa_client, sa_dev);

	/*
	 * We register our event handler after everything is set up,
	 * and then update our cached info after the event handler is
	 * registered to avoid any problems if a port changes state
	 * during our initialization.
	 */

	INIT_IB_EVENT_HANDLER(&sa_dev->event_handler, device, ib_sa_event);
	if (ib_register_event_handler(&sa_dev->event_handler))
		goto err;

	for (i = 0; i <= e - s; ++i) {
		if (rdma_cap_ib_sa(device, i + 1))
			update_sm_ah(&sa_dev->port[i].update_task);
	}

	return;

err:
	while (--i >= 0) {
		if (rdma_cap_ib_sa(device, i + 1))
			ib_unregister_mad_agent(sa_dev->port[i].agent);
	}
free:
	kfree(sa_dev);
	return;
}

static void ib_sa_remove_one(struct ib_device *device, void *client_data)
{
	struct ib_sa_device *sa_dev = client_data;
	int i;

	if (!sa_dev)
		return;

	ib_unregister_event_handler(&sa_dev->event_handler);

	flush_workqueue(ib_wq);

	for (i = 0; i <= sa_dev->end_port - sa_dev->start_port; ++i) {
		if (rdma_cap_ib_sa(device, i + 1)) {
			ib_unregister_mad_agent(sa_dev->port[i].agent);
			if (sa_dev->port[i].sm_ah)
				kref_put(&sa_dev->port[i].sm_ah->ref, free_sm_ah);
		}

	}

	kfree(sa_dev);
}

int ib_sa_init(void)
{
	int ret;

	get_random_bytes(&tid, sizeof tid);

	atomic_set(&ib_nl_sa_request_seq, 0);

	ret = ib_register_client(&sa_client);
	if (ret) {
		pr_err("Couldn't register ib_sa client\n");
		goto err1;
	}

	ret = mcast_init();
	if (ret) {
		pr_err("Couldn't initialize multicast handling\n");
		goto err2;
	}

	ib_nl_wq = alloc_ordered_workqueue("ib_nl_sa_wq", WQ_MEM_RECLAIM);
	if (!ib_nl_wq) {
		ret = -ENOMEM;
		goto err3;
	}

	INIT_DELAYED_WORK(&ib_nl_timed_work, ib_nl_request_timeout);

	return 0;

err3:
	mcast_cleanup();
err2:
	ib_unregister_client(&sa_client);
err1:
	return ret;
}

void ib_sa_cleanup(void)
{
	cancel_delayed_work(&ib_nl_timed_work);
	flush_workqueue(ib_nl_wq);
	destroy_workqueue(ib_nl_wq);
	mcast_cleanup();
	ib_unregister_client(&sa_client);
	idr_destroy(&query_idr);
}
