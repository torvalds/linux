/*
 * Copyright (c) 2004 Topspin Communications.  All rights reserved.
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
 *
 * $Id: sa_query.c 1389 2004-12-27 22:56:47Z roland $
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/random.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/kref.h>
#include <linux/idr.h>

#include <ib_pack.h>
#include <ib_sa.h>

MODULE_AUTHOR("Roland Dreier");
MODULE_DESCRIPTION("InfiniBand subnet administration query support");
MODULE_LICENSE("Dual BSD/GPL");

/*
 * These two structures must be packed because they have 64-bit fields
 * that are only 32-bit aligned.  64-bit architectures will lay them
 * out wrong otherwise.  (And unfortunately they are sent on the wire
 * so we can't change the layout)
 */
struct ib_sa_hdr {
	u64			sm_key;
	u16			attr_offset;
	u16			reserved;
	ib_sa_comp_mask		comp_mask;
} __attribute__ ((packed));

struct ib_sa_mad {
	struct ib_mad_hdr	mad_hdr;
	struct ib_rmpp_hdr	rmpp_hdr;
	struct ib_sa_hdr	sa_hdr;
	u8			data[200];
} __attribute__ ((packed));

struct ib_sa_sm_ah {
	struct ib_ah        *ah;
	struct kref          ref;
};

struct ib_sa_port {
	struct ib_mad_agent *agent;
	struct ib_mr        *mr;
	struct ib_sa_sm_ah  *sm_ah;
	struct work_struct   update_task;
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
	struct ib_sa_port  *port;
	struct ib_sa_mad   *mad;
	struct ib_sa_sm_ah *sm_ah;
	DECLARE_PCI_UNMAP_ADDR(mapping)
	int                 id;
};

struct ib_sa_path_query {
	void (*callback)(int, struct ib_sa_path_rec *, void *);
	void *context;
	struct ib_sa_query sa_query;
};

struct ib_sa_mcmember_query {
	void (*callback)(int, struct ib_sa_mcmember_rec *, void *);
	void *context;
	struct ib_sa_query sa_query;
};

static void ib_sa_add_one(struct ib_device *device);
static void ib_sa_remove_one(struct ib_device *device);

static struct ib_client sa_client = {
	.name   = "sa",
	.add    = ib_sa_add_one,
	.remove = ib_sa_remove_one
};

static spinlock_t idr_lock;
static DEFINE_IDR(query_idr);

static spinlock_t tid_lock;
static u32 tid;

enum {
	IB_SA_ATTR_CLASS_PORTINFO    = 0x01,
	IB_SA_ATTR_NOTICE	     = 0x02,
	IB_SA_ATTR_INFORM_INFO	     = 0x03,
	IB_SA_ATTR_NODE_REC	     = 0x11,
	IB_SA_ATTR_PORT_INFO_REC     = 0x12,
	IB_SA_ATTR_SL2VL_REC	     = 0x13,
	IB_SA_ATTR_SWITCH_REC	     = 0x14,
	IB_SA_ATTR_LINEAR_FDB_REC    = 0x15,
	IB_SA_ATTR_RANDOM_FDB_REC    = 0x16,
	IB_SA_ATTR_MCAST_FDB_REC     = 0x17,
	IB_SA_ATTR_SM_INFO_REC	     = 0x18,
	IB_SA_ATTR_LINK_REC	     = 0x20,
	IB_SA_ATTR_GUID_INFO_REC     = 0x30,
	IB_SA_ATTR_SERVICE_REC	     = 0x31,
	IB_SA_ATTR_PARTITION_REC     = 0x33,
	IB_SA_ATTR_RANGE_REC	     = 0x34,
	IB_SA_ATTR_PATH_REC	     = 0x35,
	IB_SA_ATTR_VL_ARB_REC	     = 0x36,
	IB_SA_ATTR_MC_GROUP_REC	     = 0x37,
	IB_SA_ATTR_MC_MEMBER_REC     = 0x38,
	IB_SA_ATTR_TRACE_REC	     = 0x39,
	IB_SA_ATTR_MULTI_PATH_REC    = 0x3a,
	IB_SA_ATTR_SERVICE_ASSOC_REC = 0x3b
};

#define PATH_REC_FIELD(field) \
	.struct_offset_bytes = offsetof(struct ib_sa_path_rec, field),		\
	.struct_size_bytes   = sizeof ((struct ib_sa_path_rec *) 0)->field,	\
	.field_name          = "sa_path_rec:" #field

static const struct ib_field path_rec_table[] = {
	{ RESERVED,
	  .offset_words = 0,
	  .offset_bits  = 0,
	  .size_bits    = 32 },
	{ RESERVED,
	  .offset_words = 1,
	  .offset_bits  = 0,
	  .size_bits    = 32 },
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
	{ RESERVED,
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

static void free_sm_ah(struct kref *kref)
{
	struct ib_sa_sm_ah *sm_ah = container_of(kref, struct ib_sa_sm_ah, ref);

	ib_destroy_ah(sm_ah->ah);
	kfree(sm_ah);
}

static void update_sm_ah(void *port_ptr)
{
	struct ib_sa_port *port = port_ptr;
	struct ib_sa_sm_ah *new_ah, *old_ah;
	struct ib_port_attr port_attr;
	struct ib_ah_attr   ah_attr;

	if (ib_query_port(port->agent->device, port->port_num, &port_attr)) {
		printk(KERN_WARNING "Couldn't query port\n");
		return;
	}

	new_ah = kmalloc(sizeof *new_ah, GFP_KERNEL);
	if (!new_ah) {
		printk(KERN_WARNING "Couldn't allocate new SM AH\n");
		return;
	}

	kref_init(&new_ah->ref);

	memset(&ah_attr, 0, sizeof ah_attr);
	ah_attr.dlid     = port_attr.sm_lid;
	ah_attr.sl       = port_attr.sm_sl;
	ah_attr.port_num = port->port_num;

	new_ah->ah = ib_create_ah(port->agent->qp->pd, &ah_attr);
	if (IS_ERR(new_ah->ah)) {
		printk(KERN_WARNING "Couldn't create new SM AH\n");
		kfree(new_ah);
		return;
	}

	spin_lock_irq(&port->ah_lock);
	old_ah = port->sm_ah;
	port->sm_ah = new_ah;
	spin_unlock_irq(&port->ah_lock);

	if (old_ah)
		kref_put(&old_ah->ref, free_sm_ah);
}

static void ib_sa_event(struct ib_event_handler *handler, struct ib_event *event)
{
	if (event->event == IB_EVENT_PORT_ERR    ||
	    event->event == IB_EVENT_PORT_ACTIVE ||
	    event->event == IB_EVENT_LID_CHANGE  ||
	    event->event == IB_EVENT_PKEY_CHANGE ||
	    event->event == IB_EVENT_SM_CHANGE) {
		struct ib_sa_device *sa_dev =
			ib_get_client_data(event->device, &sa_client);

		schedule_work(&sa_dev->port[event->element.port_num -
					    sa_dev->start_port].update_task);
	}
}

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

	spin_lock_irqsave(&idr_lock, flags);
	if (idr_find(&query_idr, id) != query) {
		spin_unlock_irqrestore(&idr_lock, flags);
		return;
	}
	agent = query->port->agent;
	spin_unlock_irqrestore(&idr_lock, flags);

	ib_cancel_mad(agent, id);
}
EXPORT_SYMBOL(ib_sa_cancel_query);

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

static int send_mad(struct ib_sa_query *query, int timeout_ms)
{
	struct ib_sa_port *port = query->port;
	unsigned long flags;
	int ret;
	struct ib_sge      gather_list;
	struct ib_send_wr *bad_wr, wr = {
		.opcode      = IB_WR_SEND,
		.sg_list     = &gather_list,
		.num_sge     = 1,
		.send_flags  = IB_SEND_SIGNALED,
		.wr	     = {
			 .ud = {
				 .mad_hdr     = &query->mad->mad_hdr,
				 .remote_qpn  = 1,
				 .remote_qkey = IB_QP1_QKEY,
				 .timeout_ms  = timeout_ms
			 }
		 }
	};

retry:
	if (!idr_pre_get(&query_idr, GFP_ATOMIC))
		return -ENOMEM;
	spin_lock_irqsave(&idr_lock, flags);
	ret = idr_get_new(&query_idr, query, &query->id);
	spin_unlock_irqrestore(&idr_lock, flags);
	if (ret == -EAGAIN)
		goto retry;
	if (ret)
		return ret;

	wr.wr_id = query->id;

	spin_lock_irqsave(&port->ah_lock, flags);
	kref_get(&port->sm_ah->ref);
	query->sm_ah = port->sm_ah;
	wr.wr.ud.ah  = port->sm_ah->ah;
	spin_unlock_irqrestore(&port->ah_lock, flags);

	gather_list.addr   = dma_map_single(port->agent->device->dma_device,
					    query->mad,
					    sizeof (struct ib_sa_mad),
					    DMA_TO_DEVICE);
	gather_list.length = sizeof (struct ib_sa_mad);
	gather_list.lkey   = port->mr->lkey;
	pci_unmap_addr_set(query, mapping, gather_list.addr);

	ret = ib_post_send_mad(port->agent, &wr, &bad_wr);
	if (ret) {
		dma_unmap_single(port->agent->device->dma_device,
				 pci_unmap_addr(query, mapping),
				 sizeof (struct ib_sa_mad),
				 DMA_TO_DEVICE);
		kref_put(&query->sm_ah->ref, free_sm_ah);
		spin_lock_irqsave(&idr_lock, flags);
		idr_remove(&query_idr, query->id);
		spin_unlock_irqrestore(&idr_lock, flags);
	}

	return ret;
}

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
		query->callback(status, &rec, query->context);
	} else
		query->callback(status, NULL, query->context);
}

static void ib_sa_path_rec_release(struct ib_sa_query *sa_query)
{
	kfree(sa_query->mad);
	kfree(container_of(sa_query, struct ib_sa_path_query, sa_query));
}

/**
 * ib_sa_path_rec_get - Start a Path get query
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
int ib_sa_path_rec_get(struct ib_device *device, u8 port_num,
		       struct ib_sa_path_rec *rec,
		       ib_sa_comp_mask comp_mask,
		       int timeout_ms, int gfp_mask,
		       void (*callback)(int status,
					struct ib_sa_path_rec *resp,
					void *context),
		       void *context,
		       struct ib_sa_query **sa_query)
{
	struct ib_sa_path_query *query;
	struct ib_sa_device *sa_dev = ib_get_client_data(device, &sa_client);
	struct ib_sa_port   *port   = &sa_dev->port[port_num - sa_dev->start_port];
	struct ib_mad_agent *agent  = port->agent;
	int ret;

	query = kmalloc(sizeof *query, gfp_mask);
	if (!query)
		return -ENOMEM;
	query->sa_query.mad = kmalloc(sizeof *query->sa_query.mad, gfp_mask);
	if (!query->sa_query.mad) {
		kfree(query);
		return -ENOMEM;
	}

	query->callback = callback;
	query->context  = context;

	init_mad(query->sa_query.mad, agent);

	query->sa_query.callback              = ib_sa_path_rec_callback;
	query->sa_query.release               = ib_sa_path_rec_release;
	query->sa_query.port                  = port;
	query->sa_query.mad->mad_hdr.method   = IB_MGMT_METHOD_GET;
	query->sa_query.mad->mad_hdr.attr_id  = cpu_to_be16(IB_SA_ATTR_PATH_REC);
	query->sa_query.mad->sa_hdr.comp_mask = comp_mask;

	ib_pack(path_rec_table, ARRAY_SIZE(path_rec_table),
		rec, query->sa_query.mad->data);

	*sa_query = &query->sa_query;
	ret = send_mad(&query->sa_query, timeout_ms);
	if (ret) {
		*sa_query = NULL;
		kfree(query->sa_query.mad);
		kfree(query);
	}

	return ret ? ret : query->sa_query.id;
}
EXPORT_SYMBOL(ib_sa_path_rec_get);

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
	kfree(sa_query->mad);
	kfree(container_of(sa_query, struct ib_sa_mcmember_query, sa_query));
}

int ib_sa_mcmember_rec_query(struct ib_device *device, u8 port_num,
			     u8 method,
			     struct ib_sa_mcmember_rec *rec,
			     ib_sa_comp_mask comp_mask,
			     int timeout_ms, int gfp_mask,
			     void (*callback)(int status,
					      struct ib_sa_mcmember_rec *resp,
					      void *context),
			     void *context,
			     struct ib_sa_query **sa_query)
{
	struct ib_sa_mcmember_query *query;
	struct ib_sa_device *sa_dev = ib_get_client_data(device, &sa_client);
	struct ib_sa_port   *port   = &sa_dev->port[port_num - sa_dev->start_port];
	struct ib_mad_agent *agent  = port->agent;
	int ret;

	query = kmalloc(sizeof *query, gfp_mask);
	if (!query)
		return -ENOMEM;
	query->sa_query.mad = kmalloc(sizeof *query->sa_query.mad, gfp_mask);
	if (!query->sa_query.mad) {
		kfree(query);
		return -ENOMEM;
	}

	query->callback = callback;
	query->context  = context;

	init_mad(query->sa_query.mad, agent);

	query->sa_query.callback              = ib_sa_mcmember_rec_callback;
	query->sa_query.release               = ib_sa_mcmember_rec_release;
	query->sa_query.port                  = port;
	query->sa_query.mad->mad_hdr.method   = method;
	query->sa_query.mad->mad_hdr.attr_id  = cpu_to_be16(IB_SA_ATTR_MC_MEMBER_REC);
	query->sa_query.mad->sa_hdr.comp_mask = comp_mask;

	ib_pack(mcmember_rec_table, ARRAY_SIZE(mcmember_rec_table),
		rec, query->sa_query.mad->data);

	*sa_query = &query->sa_query;
	ret = send_mad(&query->sa_query, timeout_ms);
	if (ret) {
		*sa_query = NULL;
		kfree(query->sa_query.mad);
		kfree(query);
	}

	return ret ? ret : query->sa_query.id;
}
EXPORT_SYMBOL(ib_sa_mcmember_rec_query);

static void send_handler(struct ib_mad_agent *agent,
			 struct ib_mad_send_wc *mad_send_wc)
{
	struct ib_sa_query *query;
	unsigned long flags;

	spin_lock_irqsave(&idr_lock, flags);
	query = idr_find(&query_idr, mad_send_wc->wr_id);
	spin_unlock_irqrestore(&idr_lock, flags);

	if (!query)
		return;

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

	dma_unmap_single(agent->device->dma_device,
			 pci_unmap_addr(query, mapping),
			 sizeof (struct ib_sa_mad),
			 DMA_TO_DEVICE);
	kref_put(&query->sm_ah->ref, free_sm_ah);

	query->release(query);

	spin_lock_irqsave(&idr_lock, flags);
	idr_remove(&query_idr, mad_send_wc->wr_id);
	spin_unlock_irqrestore(&idr_lock, flags);
}

static void recv_handler(struct ib_mad_agent *mad_agent,
			 struct ib_mad_recv_wc *mad_recv_wc)
{
	struct ib_sa_query *query;
	unsigned long flags;

	spin_lock_irqsave(&idr_lock, flags);
	query = idr_find(&query_idr, mad_recv_wc->wc->wr_id);
	spin_unlock_irqrestore(&idr_lock, flags);

	if (query) {
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

	if (device->node_type == IB_NODE_SWITCH)
		s = e = 0;
	else {
		s = 1;
		e = device->phys_port_cnt;
	}

	sa_dev = kmalloc(sizeof *sa_dev +
			 (e - s + 1) * sizeof (struct ib_sa_port),
			 GFP_KERNEL);
	if (!sa_dev)
		return;

	sa_dev->start_port = s;
	sa_dev->end_port   = e;

	for (i = 0; i <= e - s; ++i) {
		sa_dev->port[i].mr       = NULL;
		sa_dev->port[i].sm_ah    = NULL;
		sa_dev->port[i].port_num = i + s;
		spin_lock_init(&sa_dev->port[i].ah_lock);

		sa_dev->port[i].agent =
			ib_register_mad_agent(device, i + s, IB_QPT_GSI,
					      NULL, 0, send_handler,
					      recv_handler, sa_dev);
		if (IS_ERR(sa_dev->port[i].agent))
			goto err;

		sa_dev->port[i].mr = ib_get_dma_mr(sa_dev->port[i].agent->qp->pd,
						   IB_ACCESS_LOCAL_WRITE);
		if (IS_ERR(sa_dev->port[i].mr)) {
			ib_unregister_mad_agent(sa_dev->port[i].agent);
			goto err;
		}

		INIT_WORK(&sa_dev->port[i].update_task,
			  update_sm_ah, &sa_dev->port[i]);
	}

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

	for (i = 0; i <= e - s; ++i)
		update_sm_ah(&sa_dev->port[i]);

	return;

err:
	while (--i >= 0) {
		ib_dereg_mr(sa_dev->port[i].mr);
		ib_unregister_mad_agent(sa_dev->port[i].agent);
	}

	kfree(sa_dev);

	return;
}

static void ib_sa_remove_one(struct ib_device *device)
{
	struct ib_sa_device *sa_dev = ib_get_client_data(device, &sa_client);
	int i;

	if (!sa_dev)
		return;

	ib_unregister_event_handler(&sa_dev->event_handler);

	for (i = 0; i <= sa_dev->end_port - sa_dev->start_port; ++i) {
		ib_unregister_mad_agent(sa_dev->port[i].agent);
		kref_put(&sa_dev->port[i].sm_ah->ref, free_sm_ah);
	}

	kfree(sa_dev);
}

static int __init ib_sa_init(void)
{
	int ret;

	spin_lock_init(&idr_lock);
	spin_lock_init(&tid_lock);

	get_random_bytes(&tid, sizeof tid);

	ret = ib_register_client(&sa_client);
	if (ret)
		printk(KERN_ERR "Couldn't register ib_sa client\n");

	return ret;
}

static void __exit ib_sa_cleanup(void)
{
	ib_unregister_client(&sa_client);
}

module_init(ib_sa_init);
module_exit(ib_sa_cleanup);
